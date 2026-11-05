"""Binary formats shared by the Python dumpers and net_diff.cpp, plus Python's verdicts.

A decisions file is an array of RECORD. An expect file and a reference file each hold
one variable-length entry per record, in the same order.
"""
from pathlib import Path
import re
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "learner"))
from obs import ContractError, audit, normalize_history_turn, pack  # noqa: E402

RECORD = np.dtype([("cards", "u1", (160, 41)), ("selection", "u1", (160, 3)), ("actions", "u1", (256, 12)),
                   ("hist", "u1", (16, 14)), ("decision", "<i4", (12,)), ("global", "u1", (23,)), ("pad", "u1"),
                   ("counts", "<i4"), ("legal", "<i4"), ("overflow", "<i4"), ("to_play", "<i4"),
                   ("deck0", "<i4"), ("deck1", "<i4"), ("game", "<i4"), ("step", "<i4")])
assert RECORD.itemsize == 10440

# The code net_diff expects for each message obs.py raises. See Status in edison_net.h.
RULES = {"action overflow or option-count mismatch": 1, "visible id beyond the code list": 2,
         "own hand identity missing": 3, "hidden statistics leaked": 4, "data beyond offered actions": 7,
         "action id beyond the code list": 8, "action/card identity mismatch": 10, "unknown decision schema": 11,
         "selection counters": 12, "selection flags": 13, "selection points to padding": 14,
         "stale selection context": 15, "private hint payload": 16, "unknown stage": 17, "selection bounds": 18,
         "source reveals a private row": 20, "selection candidates disagree with offered actions": 21,
         "finish/cancel flags": 22, "history id beyond the code list": 25, "history turn offset out of range": 26}
COLUMN_RULES = {"card": 100, "action": 200, "global": 300, "history": 400}
ROW_REF, SOURCE_REF = 9, 19


def verdict_code(message):
    found = re.fullmatch(r"(card|action|global|history) column (\d+) out of range", message)
    if found:
        return COLUMN_RULES[found.group(1)] + int(found.group(2))
    return RULES[message]


def as_batch(record):
    """A record as the batch of one that actor.py hands to obs.py."""
    obs = {"cards_": record["cards"][None], "selection_": record["selection"][None], "actions_": record["actions"][None],
           "h_actions_": record["hist"][None], "decision_": record["decision"][None], "global_": record["global"][None]}
    counts = np.array([record["counts"]], dtype=np.int32)
    legal = np.array([record["legal"]], dtype=np.int32)
    overflow = np.array([record["overflow"] != 0])
    return obs, counts, legal, overflow


def python_pipeline(record, n_codes):
    """audit, normalize_history_turn and pack, in actor.py's order. Returns (verdict, packed)."""
    obs, counts, legal, overflow = as_batch(record)
    try:
        audit(obs, counts, legal, overflow, n_codes)
    except ContractError as error:
        return verdict_code(str(error)), None
    except IndexError:
        used = np.arange(256) < counts[0]
        return (ROW_REF if np.any(record["actions"][used, 0] > 160) else SOURCE_REF), None
    obs = dict(obs)
    try:
        obs["h_actions_"] = normalize_history_turn(obs["h_actions_"], obs["global_"])
    except ContractError as error:
        return verdict_code(str(error)), None
    return 0, pack(obs, counts)


def write_expect(out, verdict, packed):
    np.array([verdict], dtype="<i4").tofile(out)
    if verdict:
        return
    n_rows, count = int(packed["n_rows"][0]), int(packed["offsets"][-1])
    np.array([n_rows, count], dtype="<i4").tofile(out)
    out.write(packed["hist"][0].tobytes())
    out.write(packed["global"][0].tobytes())
    out.write(np.asarray(packed["decision"][0], dtype="<i4").tobytes())
    out.write(packed["cards"][0].tobytes())
    out.write(packed["actions"].tobytes())


def read_reference(path, channels, n_rows=None):
    """Yield (value, value_only, greedy, logits) per record from a reference.py file. n_rows per record."""
    with open(path, "rb") as f:
        index = 0
        while True:
            head = f.read(12)
            if not head:
                return
            n, best, has_rows = np.frombuffer(head, dtype="<i4")
            value, only = np.frombuffer(f.read(8), dtype="<f4")
            logits = np.frombuffer(f.read(4 * int(n)), dtype="<f4")
            if has_rows:
                f.seek(4 * channels * (int(n_rows[index]) + 2), 1)
            yield value, only, int(best), logits
            index += 1
