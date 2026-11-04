#!/usr/bin/env python3
"""Do the real networks now see the true card at Deck prompts? Measures labels, not playing strength.

Plays saved checkpoints greedily against the heuristic and counts, as frog_line.py does, whether the
label on the option a player chose matches the card the engine then asks to place, which the position
prompt reads directly from its own code. The prompt differential runs alongside, for every prompt.
Win rates are deliberately not reported: these networks trained on wrong labels.

Seed block 80100000 and up. Point --so at another module file to run the same games on that build.
"""
import argparse
import collections
import concurrent.futures
import json
import multiprocessing
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from actor import MAX_OPTIONS, MAX_STEPS, NetPolicy, configure_child_environment  # noqa: E402
from decks import ROOT  # noqa: E402

RUNTIME = {}
SELECT, POSITION, IDLE = 3, 5, 1


def setup(so):
    os.environ.update(configure_child_environment())
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    os.chdir(ROOT / "scripts")
    if so:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)
    import ygoenv
    from ygoenv.ygopro import init_module
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in deck_registry.registry().items()})
    RUNTIME.update(ygoenv=ygoenv, metadata=deck_registry.card_metadata(), nets={})


def code(row):
    return int(row[0]) * 256 + int(row[1])


def play(cell):
    import numpy as np
    from obs import OBS_KEYS, normalize_history_turn, pack, segment_distribution
    from ygoai.edison_policy import Heuristic
    seed, games, seat = cell["seed"], cell["games"], cell["seat"]
    deck0, deck1 = (cell["deck"], cell["other"]) if seat == 0 else (cell["other"], cell["deck"])
    env = RUNTIME["ygoenv"].make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=1, seed=seed, deck1=deck0, deck2=deck1, play_mode="self",
                                 player=-1, async_reset=False, greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                                 max_episode_steps=MAX_STEPS, timeout=10, verbose=False)
    nets = RUNTIME["nets"]
    sha = json.loads(Path(cell["checkpoint"]).with_suffix(".json").read_text())["params_sha256"]
    net = nets.setdefault(sha, NetPolicy(cell["checkpoint"], sha))
    heuristics = {eid: Heuristic(RUNTIME["metadata"], seed + eid * 2 + (1 - seat)) for eid in range(games)}
    frogs = {i for i, v in RUNTIME["metadata"].items() if v["name"] in ("Substitoad", "Treeborn Frog", "Swap Frog", "Dupe Frog", "Des Frog", "Flip Flop Frog", "Unifrog")}
    substitoad = next(i for i, v in RUNTIME["metadata"].items() if v["name"] == "Substitoad")
    counts = collections.Counter()
    # What the network last chose at a Deck-looking prompt, per game, and since which Substitoad activation.
    pending = {}
    done = set()
    obs, info = env.reset()
    while len(done) < games:
        ids = np.asarray(info["env_id"], dtype=np.int32)
        order = np.argsort(ids)
        ids = ids[order]
        sub = {key: obs[key][order] for key in OBS_KEYS}
        offered_counts = np.asarray(info["num_options"])[order]
        seats = np.asarray(info["to_play"])[order]
        sub["h_actions_"] = normalize_history_turn(sub["h_actions_"], sub["global_"])
        actions = np.zeros(len(ids), dtype=np.int64)
        rows = np.nonzero(seats == seat)[0]
        if rows.size:
            packed = pack({key: sub[key][rows] for key in OBS_KEYS}, offered_counts[rows])
            logits, _ = net.infer(packed)
            logp, _ = segment_distribution(logits, packed["offsets"])
            for i, row in enumerate(rows):
                actions[row] = int(np.argmax(logp[packed["offsets"][i]:packed["offsets"][i + 1]]))
        for row, (eid, player) in enumerate(zip(ids.tolist(), seats.tolist())):
            count = int(offered_counts[row])
            offered = sub["actions_"][row, :count]
            cards = sub["cards_"][row]
            if player != seat:
                actions[row] = heuristics[eid].choose(cards, sub["global_"][row], offered)[0]
                continue
            chosen = offered[int(actions[row])]
            first = int(offered[0, 3])
            state = pending.get(eid)
            if int(chosen[3]) in (IDLE, 2) and int(chosen[4]) == 8 and code(chosen[1:3]) == substitoad:
                pending[eid] = {"prompt": None}
            elif state is not None and first == SELECT:
                labels = [code(a[1:3]) for a in offered]
                rows_of = [int(a[0]) for a in offered]
                # The same prompt the old count used, plus whether every option is really in the own Deck.
                on_deck = bool(rows_of) and all(r and int(cards[r - 1, 2]) == 1 and int(cards[r - 1, 4]) == 0 for r in rows_of)
                state["prompt"] = {"labels": labels, "chosen": int(actions[row]), "on_deck": on_deck}
                counts["select_prompts_seen"] += 1
                if on_deck:
                    counts["deck_prompts_seen"] += 1
                    counts["options_shown"] += len(labels)
                    counts["options_frog_label"] += sum(x in frogs for x in labels)
                    counts["chance_x1000"] += 1000 // len(labels)
            elif state is not None and first == POSITION:
                if state["prompt"] is not None:
                    right = state["prompt"]["labels"][state["prompt"]["chosen"]] == code(offered[0, 1:3])
                    # frog_line.py's count takes the last card prompt, which is not always a Deck prompt.
                    counts["last_card_prompts"] += 1
                    counts["last_card_prompt_right"] += right
                    if state["prompt"]["on_deck"]:
                        counts["deck_prompts"] += 1
                        counts["label_right"] += right
                pending.pop(eid)
            elif state is not None and first == IDLE:
                pending.pop(eid)
        obs, _, terminated, truncated, info = env.step(actions, ids)
        if np.any(truncated):
            raise RuntimeError("truncated")
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if terminated[row]:
                done.add(eid)
            else:
                keep.append(row)
        if keep:
            obs = {key: obs[key][keep] for key in OBS_KEYS}
            info = {key: np.asarray(info[key])[keep] for key in ("env_id", "num_options", "legal_options", "action_overflow", "to_play")}
    return dict(counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", type=Path, required=True, help="Directory holding gauntlet-<optimizer>/checkpoint-<round>")
    parser.add_argument("--round", type=int, default=1280)
    parser.add_argument("--seed", type=int, default=80100000)
    parser.add_argument("--games", type=int, default=64)
    parser.add_argument("--cells", type=int, default=4, help="Cells per optimizer and seat")
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--so", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 80100000 <= args.seed < 80200000:
        parser.error("This check's seeds stay inside the 80100000 block")
    plan, seed = [], args.seed
    for optimizer in (1702, 1703, 1704):
        for seat in (0, 1):
            for cell in range(args.cells):
                plan.append({"checkpoint": str(args.runs / f"gauntlet-{optimizer}" / f"checkpoint-{args.round}"), "deck": "frog_monarch",
                             "other": ("machina_gadget", "blackwing")[cell % 2], "seat": seat, "seed": seed, "games": args.games, "optimizer": optimizer})
                seed += args.games
    with tempfile.TemporaryDirectory(prefix="edison-labels-") as tmp:
        os.environ["EDISON_PROMPT_AUDIT"] = tmp
        context = multiprocessing.get_context("spawn")
        totals = collections.Counter()
        so = str(args.so.resolve()) if args.so else None
        with concurrent.futures.ProcessPoolExecutor(args.workers, mp_context=context, initializer=setup, initargs=(so,)) as pool:
            for counts in pool.map(play, plan):
                totals.update(counts)
        sys.path.insert(0, str(ROOT / "tests/edison"))
        from prompt_audit import summarize
        differential = summarize([tmp])
    right, prompts = totals["label_right"], totals["deck_prompts"]
    report = {"seeds": [args.seed, seed - 1], "games": len(plan) * args.games, "checkpoints": sorted({p["checkpoint"] for p in plan}),
              "module": so or "checkout", "counts": dict(totals),
              "last_card_prompt_right_percent": round(100 * totals["last_card_prompt_right"] / totals["last_card_prompts"], 2) if totals["last_card_prompts"] else None,
              "label_right_percent": round(100 * right / prompts, 2) if prompts else None,
              "chance_percent": round(100 * totals["chance_x1000"] / 1000 / totals["deck_prompts_seen"], 2) if totals["deck_prompts_seen"] else None,
              "frog_label_percent": round(100 * totals["options_frog_label"] / totals["options_shown"], 2) if totals["options_shown"] else None,
              "differential": {key: differential[key] for key in ("duels", "decisions", "decisions_listing_cards", "mismatch_total", "by_field",
                                                                  "extra_visible_own_deck_rows", "prompts_with_extra_rows")},
              "differential_deck": {key: row for key, row in differential["by_kind"].items() if "|deck|" in key}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
