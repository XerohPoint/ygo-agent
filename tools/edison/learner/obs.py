"""Fail-closed observation audit and packing for the Edison network.

Only the whitelisted observation keys in OBS_KEYS and the whitelisted info keys in
INFO_KEYS are read. `info:deck` and `mask_` are never read. Hidden deck rows that
nothing refers to carry no information beyond the public counts in global_, so they
are dropped to make attention cheaper. That drops 60% of rows at the median.
"""
import numpy as np

OBS_KEYS = ("cards_", "global_", "actions_", "h_actions_", "decision_", "selection_")
INFO_KEYS = ("env_id", "num_options", "legal_options", "action_overflow", "to_play", "winner", "win_reason")
MAX_OPTIONS = 256
MAX_ROWS = 160

# Bounds for embedded columns. nn.Embed returns NaN on an out-of-range id, so a value
# outside these bounds is a contract failure here rather than a silent NaN later.
CARD_BOUNDS = {2: 8, 3: 79, 4: 1, 5: 8, 6: 1, 7: 7, 8: 26, 9: 13, 10: 15, 11: 1}
ACTION_BOUNDS = {3: 16, 4: 9, 5: 1, 6: 255, 7: 3, 8: 8, 9: 13, 10: 31, 11: 9}
HIST_BOUNDS = {**ACTION_BOUNDS, 12: 255, 13: 10}
GLOBAL_BOUNDS = {4: 16, 5: 10, 6: 1, 7: 1}
# Standard hint range from docs/edison-decision-context.md, plus 0 for unknown.
HINT_MIN, HINT_MAX = 500, 575
N_HINTS = HINT_MAX - HINT_MIN + 2


class ContractError(RuntimeError):
    """An observation broke the audited contract. The game cannot count."""


def row_lookup(cards, selection, actions, counts, decision):
    """Which rows to keep, and the old row -> new one-based index mapping."""
    batch = cards.shape[0]
    ident = (cards[:, :, 0].astype(np.int32) << 8) + cards[:, :, 1]
    loc = cards[:, :, 2]
    hidden_private = (ident == 0) & ((loc == 1) | (loc == 2) | (loc == 7))
    keep = (loc > 0) & ~hidden_private
    keep |= selection.any(axis=2)
    rows = np.arange(batch)
    sources = decision[:, 3]
    has_source = sources > 0
    keep[rows[has_source], sources[has_source] - 1] = True
    width = actions.shape[1]
    used = np.arange(width)[None, :] < counts[:, None]
    refs = actions[:, :, 0].astype(np.int32)
    active = used & (refs > 0)
    hit_rows, hit_cols = np.nonzero(active)
    keep[hit_rows, refs[hit_rows, hit_cols] - 1] = True
    return keep


def audit(obs, counts, legal, overflow, n_codes):
    """Vectorized port of the worker contract in tests/edison/agent_match.py."""
    cards, actions, decision, selection, glob = (obs["cards_"], obs["actions_"], obs["decision_"],
                                                 obs["selection_"], obs["global_"])
    if np.any(counts <= 0) or np.any(counts > MAX_OPTIONS) or np.any(overflow) or not np.array_equal(counts, legal):
        raise ContractError("action overflow or option-count mismatch")
    ident = (cards[:, :, 0].astype(np.int32) << 8) + cards[:, :, 1]
    if ident.max() > n_codes:
        raise ContractError("visible id beyond the code list")
    own_hand = (cards[:, :, 2] == 2) & (cards[:, :, 4] == 0)
    if np.any(own_hand & (ident == 0)):
        raise ContractError("own hand identity missing")
    hidden = (ident == 0) & (cards[:, :, 2] > 0)
    if np.any(cards[:, :, 7:][hidden]):
        raise ContractError("hidden statistics leaked")
    for col, bound in CARD_BOUNDS.items():
        if cards[:, :, col].max() > bound:
            raise ContractError(f"card column {col} out of range")
    used = np.arange(actions.shape[1])[None, :] < counts[:, None]
    act_used = actions[used]
    for col, bound in ACTION_BOUNDS.items():
        if act_used[:, col].max() > bound:
            raise ContractError(f"action column {col} out of range")
    if np.any(actions[~used]):
        raise ContractError("data beyond offered actions")
    if ((act_used[:, 1].astype(np.int32) << 8) + act_used[:, 2]).max() > n_codes:
        raise ContractError("action id beyond the code list")
    refs = actions[:, :, 0].astype(np.int32)
    row, col = np.nonzero(used & (refs > 0))
    targets = refs[row, col] - 1
    if np.any(cards[row, targets, 0] != actions[row, col, 1]) or np.any(cards[row, targets, 1] != actions[row, col, 2]):
        raise ContractError("action/card identity mismatch")
    if np.any(decision[:, 0] != 1):
        raise ContractError("unknown decision schema")
    if np.any(decision[:, 6] != selection[:, :, 1].sum(axis=1)) or np.any(decision[:, 7] != selection[:, :, 2].sum(axis=1)):
        raise ContractError("selection counters")
    if np.any(selection > 1) or np.any(selection[:, :, 2] > selection[:, :, 1]):
        raise ContractError("selection flags")
    if np.any(selection.any(axis=2) & (cards[:, :, 2] == 0)):
        raise ContractError("selection points to padding")
    active = decision[:, 11] != 0
    if np.any(decision[~active][:, 1:]) or np.any(selection[~active]):
        raise ContractError("stale selection context")
    if active.any():
        hint, stage = decision[active, 1], decision[active, 2]
        if np.any((hint != 0) & ((hint < HINT_MIN) | (hint > HINT_MAX))):
            raise ContractError("private hint payload")
        if np.any((stage < 0) | (stage > 3)):
            raise ContractError("unknown stage")
        if np.any(decision[active, 4] < 0) or np.any(decision[active, 5] < decision[active, 4]):
            raise ContractError("selection bounds")
        src_rows = np.nonzero(active & (decision[:, 3] > 0))[0]
        if src_rows.size and np.any(ident[src_rows, decision[src_rows, 3] - 1] <= 0):
            raise ContractError("source reveals a private row")
        expected = np.zeros(selection.shape[:2], dtype=np.uint8)
        expected[row, targets] = 1
        if np.any(selection[active, :, 0] != expected[active]):
            raise ContractError("selection candidates disagree with offered actions")
        offered_finish = (actions[:, :, 5] != 0).any(axis=1)
        offered_cancel = (actions[:, :, 4] == 9).any(axis=1)
        if np.any(decision[active, 8] != offered_finish[active]) or np.any(decision[active, 9] != offered_cancel[active]):
            raise ContractError("finish/cancel flags")
    for col, bound in GLOBAL_BOUNDS.items():
        if glob[:, col].max() > bound:
            raise ContractError(f"global column {col} out of range")
    hist = obs["h_actions_"]
    for col, bound in HIST_BOUNDS.items():
        if hist[:, :, col].max() > bound:
            raise ContractError(f"history column {col} out of range")
    hist_ids = (hist[:, :, 1].astype(np.int32) << 8) + hist[:, :, 2]
    if hist_ids.max() > n_codes:
        raise ContractError("history id beyond the code list")


def pack(obs, counts):
    """Trim one batch into left-packed rows and flat action rows, both uint8 or int32.

    Returned cards hold the 41 card columns plus 4 flags: three selection_ columns and
    is-source. Row references in actions and decision are remapped to trimmed rows.
    """
    cards, selection, actions, decision = obs["cards_"], obs["selection_"], obs["actions_"], obs["decision_"]
    batch = cards.shape[0]
    keep = row_lookup(cards, selection, actions, counts, decision)
    n_rows = keep.sum(axis=1)
    width = int(n_rows.max())
    order = np.argsort(~keep, axis=1, kind="stable")[:, :width]
    rows = np.arange(batch)[:, None]
    packed = np.zeros((batch, width, 45), dtype=np.uint8)
    packed[:, :, :41] = cards[rows, order]
    packed[:, :, 41:44] = selection[rows, order]
    mask = np.arange(width)[None, :] < n_rows[:, None]
    packed[~mask] = 0
    new_index = np.zeros((batch, MAX_ROWS + 1), dtype=np.int32)
    new_index[rows, order + 1] = np.arange(1, width + 1)[None, :]
    new_index[~np.pad(keep, ((0, 0), (1, 0)))] = 0
    source = new_index[np.arange(batch), decision[:, 3]]
    has_source = source > 0
    packed[np.arange(batch)[has_source], source[has_source] - 1, 44] = 1
    used = np.arange(actions.shape[1])[None, :] < counts[:, None]
    seg, col = np.nonzero(used)
    flat = actions[seg, col].astype(np.int32)
    flat[:, 0] = new_index[seg, flat[:, 0]]
    offsets = np.zeros(batch + 1, dtype=np.int64)
    offsets[1:] = np.cumsum(counts)
    dec = decision.copy()
    dec[:, 3] = source
    return {"cards": packed, "n_rows": n_rows, "actions": flat.astype(np.uint8), "offsets": offsets,
            "hist": obs["h_actions_"].copy(), "decision": dec, "global": obs["global_"].copy()}


def normalize_history_turn(hist, glob):
    """Column 12 means turns ago only after the ring buffer wraps; before that it is absolute.

    The native loop stops at the first empty row, and unfilled rows come first. Rewrite
    both cases to turns-ago using the saturated public turn counter.
    """
    hist = hist.copy()
    unfilled = hist[:, 0, 3] == 0
    present = hist[:, :, 3] != 0
    turn = glob[:, 4].astype(np.int32)[:, None]
    absolute = np.clip(turn - hist[:, :, 12].astype(np.int32), 0, 16)
    use = unfilled[:, None] & present
    hist[:, :, 12] = np.where(use, absolute, hist[:, :, 12])
    if hist[:, :, 12].max() > 16:
        raise ContractError("history turn offset out of range")
    return hist


def segment_distribution(logits, offsets):
    """Log-probabilities per offered action. Unoffered actions never enter."""
    sizes = np.diff(offsets)
    groups = np.repeat(np.arange(len(sizes)), sizes)
    shifted = logits - np.maximum.reduceat(logits, offsets[:-1])[groups]
    exp = np.exp(shifted)
    logp = shifted - np.log(np.add.reduceat(exp, offsets[:-1]))[groups]
    return logp, groups


def bucket(value, sizes):
    for size in sizes:
        if value <= size:
            return size
    raise ContractError(f"{value} exceeds bucket limit {sizes[-1]}")


ROW_BUCKETS = (48, 72, 112, 160)
ACTION_BUCKETS = (16, 64, 256, 1024, 4096, 16384, 65536)
BATCH_BUCKETS = (1, 4, 16, 64, 256, 1024)


def pad_batch(packed, batch_sizes=BATCH_BUCKETS):
    """Pad a packed batch to a shape from a small fixed set, so jit compiles few variants."""
    batch = packed["cards"].shape[0]
    bsize = bucket(batch, batch_sizes)
    msize = bucket(packed["cards"].shape[1], ROW_BUCKETS)
    asize = bucket(int(packed["offsets"][-1]), ACTION_BUCKETS)
    cards = np.zeros((bsize, msize, 45), dtype=np.uint8)
    cards[:batch, :packed["cards"].shape[1]] = packed["cards"]
    row_mask = np.arange(msize)[None, :] < np.r_[packed["n_rows"], np.zeros(bsize - batch, dtype=np.int64)][:, None]
    n = int(packed["offsets"][-1])
    actions = np.zeros((asize, 12), dtype=np.uint8)
    actions[:n] = packed["actions"]
    segments = np.full(asize, bsize, dtype=np.int32)
    segments[:n] = np.repeat(np.arange(batch), np.diff(packed["offsets"]))
    hist = np.zeros((bsize, 16, 14), dtype=np.uint8)
    hist[:batch] = packed["hist"]
    decision = np.zeros((bsize, 12), dtype=np.int32)
    decision[:batch] = packed["decision"]
    glob = np.zeros((bsize, 23), dtype=np.uint8)
    glob[:batch] = packed["global"]
    return {"cards": cards, "row_mask": row_mask, "actions": actions, "segments": segments,
            "hist": hist, "decision": decision, "global": glob}
