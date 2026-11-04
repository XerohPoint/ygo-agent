#!/usr/bin/env python3
"""Can the 32-unit model and 128 features represent the heuristic's choices at all?

Offline supervised fit on logged learner-view states, using cross entropy against the
heuristic's top-scored action. Whole duel seeds are held out, and play is only on held
out validation seeds. This is a capacity probe, not a proposed policy. Models stay in
build/. Also measures feature aliasing: offered actions the encoder cannot tell apart.
"""
from concurrent.futures import ProcessPoolExecutor
from collections import Counter
import gzip
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/edison"))
from loss_review import DECKS, OUT, EVIDENCE, cell_dir, cells  # noqa: E402
import agent_match  # noqa: E402
import numpy as np  # noqa: E402
from ygoai.edison_rl import Adam, RLModel, distribution  # noqa: E402

FOLDS = ((0, 1), (2, 3), (4, 5), (6, 7))
SEATS = (0, 1)
STEPS = 2500
BASE = 40000000


def load_rows():
    xs, offsets, games, targets, msgs, optimizer_labels = [], [0], [], [], [], []
    alias = Counter()
    alias_examples = Counter()
    for cell in cells():
        directory = cell_dir("diag", *cell)
        data = np.load(directory / "features-1.npz")
        x, off, meta = data["x"], data["offsets"], data["meta"]
        with gzip.open(directory / "diag-1.jsonl.gz", "rt") as stream:
            learner = [r for r in map(json.loads, stream) if r["role"] == "learner"]
        assert len(learner) == len(meta)
        for index, (record, row) in enumerate(zip(learner, meta)):
            assert record["env"] == row[0] and record["step"] == row[1]
            segment = x[off[index]:off[index + 1]]
            xs.append(segment)
            offsets.append(offsets[-1] + len(segment))
            games.append(int(row[0]))
            targets.append(int(row[3]))
            msgs.append(record["msg"])
            if len(segment) > 1 and record["msg"] != "place":
                alias["prompts"] += 1
                seen = {}
                collided = False
                for option, features in zip(record["options"], segment):
                    key = features.tobytes()
                    other = seen.get(key)
                    label = (option.get("card"), option["act"], option.get("effect"), option.get("phase"))
                    if other is not None and other != label:
                        collided = True
                        if other[0] != label[0]:
                            alias_examples[f"{record['msg']}: {other[0]} = {label[0]} ({label[1]})"] += 1
                    seen[key] = label
                alias["with_alias"] += collided
                alias["with_alias_" + record["msg"]] += collided
                alias["prompts_" + record["msg"]] += 1
    return (np.concatenate(xs), np.asarray(offsets), np.asarray(games), np.asarray(targets),
            np.asarray(msgs), alias, alias_examples)


def subset(x, offsets, keep):
    rows, new_offsets = [], [0]
    for index in keep:
        rows.append(x[offsets[index]:offsets[index + 1]])
        new_offsets.append(new_offsets[-1] + len(rows[-1]))
    return np.concatenate(rows), np.asarray(new_offsets)


def fit(x, offsets, targets, seed):
    model = RLModel(seed=seed)
    optimizer = Adam(model, lr=0.002)
    positions = offsets[:-1] + targets
    for step in range(STEPS):
        logits, h = model.forward(x)
        probs, logp, groups = distribution(logits, offsets)
        gradient = probs.copy()
        gradient[positions] -= 1
        gradient /= len(targets)
        grad = {"out": h.T @ gradient}
        dh = (gradient[:, None] * model.p["out"]) * (1 - h * h)
        grad["w"] = x.T @ dh
        grad["b"] = dh.sum(axis=0)
        grad["v"] = np.zeros_like(model.p["v"])
        grad["vb"] = np.zeros(1)
        optimizer.step(model, grad)
    return model


def predict(model, x, offsets):
    logits, _ = model.forward(x)
    return np.asarray([int(np.argmax(logits[offsets[i]:offsets[i + 1]])) for i in range(len(offsets) - 1)])


def play(job):
    fold, seed_index, deck0, deck1, seat, model_path = job
    agent_match.__file__ = str(ROOT / "tools/edison/diag_worker.py")
    os.environ["EDISON_DIAG_MODE"] = "argmax"
    output = OUT / "capacity-probe" / f"fold{fold}-init{seed_index}" / f"{deck0}-{deck1}-seat{seat}"
    policy0, policy1 = ("rl", "heuristic") if seat == 0 else ("heuristic", "rl")
    result = agent_match.run_worker(BASE + FOLDS[fold][0], 2, 1, deck0, deck1, output, 1, policy0, policy1, model_path)
    return {"fold": fold, "init": seed_index, "deck": deck0 if seat == 0 else deck1, "seat": seat,
            "wins": sum(d["winner"] == seat for d in result["duels"]), "games": 2}


def main():
    x, offsets, games, targets, msgs, alias, examples = load_rows()
    report = {"decisions": len(targets), "steps": STEPS,
              "aliasing": dict(alias), "alias_examples": examples.most_common(25)}
    size = np.diff(offsets)
    scored = (size > 1) & (msgs != "place")
    baseline = {}
    # How well does the actual trained model's modal action match the same labels?
    jobs, folds = [], []
    for fold, held in enumerate(FOLDS):
        train_index = np.flatnonzero(~np.isin(games, held))
        test_index = np.flatnonzero(np.isin(games, held))
        x_train, off_train = subset(x, offsets, train_index)
        x_test, off_test = subset(x, offsets, test_index)
        for init in range(1):
            path = OUT / "capacity-probe" / f"fold{fold}-init{init}.json"
            # A finished fit is reused, since each one takes minutes on a shared machine.
            model = RLModel.load(path) if path.exists() else fit(x_train, off_train, targets[train_index], 9100 + init)
            test_pred = predict(model, x_test, off_test)
            train_pred = predict(model, x_train, off_train)
            mask_test, mask_train = scored[test_index], scored[train_index]
            agreement = float((test_pred == targets[test_index])[mask_test].mean())
            by_msg = {m: round(float((test_pred == targets[test_index])[mask_test & (msgs[test_index] == m)].mean()), 4)
                      for m in sorted(set(msgs[test_index][mask_test]))}
            path.parent.mkdir(exist_ok=True)
            model.save(path, {"capacity_probe": True, "held_out_seeds": list(held)})
            folds.append({"fold": fold, "init": init, "train_agreement": round(float((train_pred == targets[train_index])[mask_train].mean()), 4),
                          "heldout_agreement": round(agreement, 4), "heldout_by_msg": by_msg})
            print(folds[-1], flush=True)
            for deck0 in DECKS:
                for deck1 in DECKS:
                    if deck0 != deck1:
                        for seat in SEATS:
                            jobs.append((fold, init, deck0, deck1, seat, str(path)))
    report["fits"] = folds
    with ProcessPoolExecutor(max_workers=6) as pool:
        played = list(pool.map(play, jobs))
    report["played_games"] = sum(p["games"] for p in played)
    report["played_wins"] = sum(p["wins"] for p in played)
    for field in ("deck", "seat", "init"):
        report["played_by_" + field] = {str(k): {"wins": sum(p["wins"] for p in played if p[field] == k),
                                                  "games": sum(p["games"] for p in played if p[field] == k)}
                                        for k in sorted({p[field] for p in played})}
    (OUT / "capacity-probe.json").write_text(json.dumps(report, indent=1))
    (EVIDENCE / "capacity-probe.json").write_text(json.dumps(report, indent=1))
    print(json.dumps({k: v for k, v in report.items() if k != "fits"}, indent=1))


if __name__ == "__main__":
    main()
