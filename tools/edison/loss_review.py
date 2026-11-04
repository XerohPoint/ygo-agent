#!/usr/bin/env python3
"""Reproducible Edison loss review. See docs/edison-loss-review.md.

Subcommands (run in order; all write under build/edison-loss-review-2026-11-04):
  run        plain and diagnostic workers for all validation games
  surgery    separately labelled argmax / hybrid policy-surgery runs
  verify     byte-compare diagnostic against plain traces, and against the archive
  select     apply the predeclared sampling rule from sampling-rule.md
  aggregate  all-games aggregates (JSON and tables)
  render     readable logs for selected games, or one game by --game
  surgery-report  win rates and paired bootstrap for the surgery runs
  curve      argmax play of intermediate checkpoints next to archived sampled results
  audit      redaction audit over all diagnostic records
  controls   first-action control, mirror-start splits, hand-limit and double-summon counts
Only validation seeds 40000000-40000007 are permitted. Uses at most --jobs processes,
each worker single-threaded.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/edison"))
import agent_match  # noqa: E402

OUT = ROOT / "build/edison-loss-review-2026-11-04"
EVIDENCE = ROOT / "tests/edison/reports/loss-review-2026-11-04"
RULE = EVIDENCE / "sampling-rule.md"
RULE_SHA = "3b1442a1ebcc9a5e1b66e2a688577f19c60c86afcdfc776336784231c703847b"
OPTIMIZERS = (1702, 1703, 1704)
DECKS = ("blackwing", "frog_monarch", "machina_gadget")
SEATS = (0, 1)
BASE_SEED, COUNT = 40000000, 8
SURGERY = ("argmax", "hybrid:idle", "hybrid:battle", "hybrid:chain", "hybrid:select", "hybrid:other")


def model_path(optimizer):
    return ROOT / f"tests/edison/reports/rl-budget-2026-11-02/seed-{optimizer}/final-policy.json"


def cells():
    for optimizer in OPTIMIZERS:
        for deck0, deck1 in itertools.permutations(DECKS, 2):
            for seat in SEATS:
                yield optimizer, deck0, deck1, seat


def cell_dir(kind, optimizer, deck0, deck1, seat):
    return OUT / kind / f"seed-{optimizer}" / f"{deck0}-{deck1}-seat{seat}"


def run_cell(job):
    """Run one worker through agent_match.run_worker(), the existing guarded wrapper."""
    kind, mode, optimizer, deck0, deck1, seat = job[:6]
    opponent = job[6] if len(job) > 6 else "heuristic"
    model = job[7] if len(job) > 7 else model_path(optimizer)
    policy0, policy1 = ("rl", opponent) if seat == 0 else (opponent, "rl")
    output = cell_dir(kind, optimizer, deck0, deck1, seat)
    if mode is None:
        agent_match.__file__ = str(ROOT / "tests/edison/agent_match.py")
    else:
        # run_worker() spawns Path(__file__); point it at the wrapper around the same worker.
        agent_match.__file__ = str(ROOT / "tools/edison/diag_worker.py")
        os.environ["EDISON_DIAG_MODE"] = mode
    result = agent_match.run_worker(BASE_SEED, COUNT, 1, deck0, deck1, output, 1,
                                    policy0, policy1, model)
    return {"kind": kind, "optimizer": optimizer, "deck0": deck0, "deck1": deck1, "seat": seat,
            "wins": sum(d["winner"] == seat for d in result["duels"]),
            "decisions": result["decisions"], "elapsed": result["elapsed_seconds"]}


def execute(jobs, workers):
    with ProcessPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(run_cell, jobs))


def cmd_run(args):
    assert not (OUT / "diag").exists() or args.force, "Refusing to overwrite; use a fresh output"
    jobs = [("plain", None, *c) for c in cells()] + [("diag", "observe", *c) for c in cells()]
    results = execute(jobs, args.jobs)
    plain = [r for r in results if r["kind"] == "plain"]
    diag = [r for r in results if r["kind"] == "diag"]
    speed = {k: sum(r["decisions"] for r in v) / sum(r["elapsed"] for r in v)
             for k, v in (("plain", plain), ("diagnostic", diag))}
    print("games", sum(COUNT for _ in plain), "learner wins", sum(r["wins"] for r in plain))
    print("single-thread decisions/s (diagnostic overhead, not a benchmark):", speed)
    (OUT / "run.json").write_text(json.dumps({"results": results, "decisions_per_second": speed}, indent=1))


def run_control(job):
    """Frozen heuristic against itself: no learner, no diagnostics, the plain wrapper."""
    deck0, deck1 = job
    agent_match.__file__ = str(ROOT / "tests/edison/agent_match.py")
    output = OUT / "surgery" / "control-heuristic" / f"{deck0}-{deck1}"
    return deck0, deck1, agent_match.run_worker(BASE_SEED, COUNT, 1, deck0, deck1, output, 1, "heuristic", "heuristic")


def cmd_surgery(args):
    path = OUT / "surgery.json"
    report = json.loads(path.read_text()) if path.exists() else {}
    for mode in args.modes or SURGERY:
        family, _, opponent = mode.partition("@")
        opponent = opponent or "heuristic"
        kind = "surgery/" + mode.replace(":", "-").replace("@", "-vs-")
        worker_mode = "observe" if family == "sampled" else family
        results = execute([(kind, worker_mode, *c, opponent) for c in cells()], args.jobs)
        wins = {o: sum(r["wins"] for r in results if r["optimizer"] == o) for o in OPTIMIZERS}
        report[mode] = {"wins_by_optimizer": wins, "wins": sum(wins.values()), "games": 288}
        print(mode, report[mode], flush=True)
        path.write_text(json.dumps(report, indent=1))
    if args.control:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(run_control, list(itertools.permutations(DECKS, 2))))
        control = {"note": "heuristic vs heuristic; each pairing is one deterministic set of 8 games"}
        by_deck = {}
        for deck0, deck1, result in rows:
            for duel in result["duels"]:
                by_deck.setdefault(f"{deck0} (seat0) vs {deck1} (seat1)", []).append(duel["winner"])
        control["winners"] = by_deck
        report["control"] = control
        path.write_text(json.dumps(report, indent=1))
        print(json.dumps(control))


def cmd_verify(args):
    report = {"cells": 0, "games": 0, "byte_identical_traces": 0, "duels_identical": 0,
              "archive_cells_matched": 0, "archive_checked": 0, "mismatches": []}
    for cell in cells():
        plain, diag = cell_dir("plain", *cell), cell_dir("diag", *cell)
        report["cells"] += 1
        same = (plain / "trace-1.jsonl").read_bytes() == (diag / "trace-1.jsonl").read_bytes()
        p, d = (json.loads((x / "worker-1.json").read_text()) for x in (plain, diag))
        report["byte_identical_traces"] += same
        report["duels_identical"] += p["duels"] == d["duels"]
        report["games"] += len(p["duels"])
        if not same or p["duels"] != d["duels"]:
            report["mismatches"].append(str(cell))
    archive_root = ROOT / "tests/edison/reports/rl-budget-2026-11-02/validation"
    for optimizer in OPTIMIZERS:
        archive = json.loads(gzip.open(archive_root / str(optimizer) / "round-32-vs-heuristic.json.gz", "rt").read())
        for entry in archive["cells"]:
            cell = (optimizer, entry["deck0"], entry["deck1"], entry["seat"])
            plain = json.loads((cell_dir("plain", *cell) / "worker-1.json").read_text())
            archived = entry["runs"][0]["duels"]
            report["archive_checked"] += 1
            if archived == plain["duels"]:
                report["archive_cells_matched"] += 1
            else:
                report["mismatches"].append("archive:" + str(cell))
    (OUT / "verify.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    assert not report["mismatches"], "Diagnostic or archive mismatch"


def load_games(kind="plain"):
    """All games with outcome only; one row per game, in fixed cell order."""
    games = []
    for optimizer, deck0, deck1, seat in cells():
        result = json.loads((cell_dir(kind, optimizer, deck0, deck1, seat) / "worker-1.json").read_text())
        for duel in result["duels"]:
            games.append({"optimizer": optimizer, "deck0": deck0, "deck1": deck1, "seat": seat,
                          "deck": deck0 if seat == 0 else deck1,
                          "opp_deck": deck1 if seat == 0 else deck0,
                          "env": duel["env"], "seed": duel["seed"], "decisions": duel["steps"],
                          "winner": duel["winner"], "win_reason": duel["win_reason"],
                          "outcome": "draw" if duel["winner"] == 2 else
                                     "win" if duel["winner"] == seat else "loss"})
    return games


def rank(game):
    key = "|".join(("edison-loss-review-2026-11-04", str(game["optimizer"]), game["deck0"],
                    game["deck1"], str(game["seat"]), str(game["seed"])))
    return hashlib.sha256(key.encode()).hexdigest()


def game_id(game):
    return f"{game['optimizer']}/{game['deck0']}-{game['deck1']}-seat{game['seat']}/{game['seed']}"


def cmd_select(args):
    # Amendments are appended below this heading; the rule above it must stay frozen.
    frozen = RULE.read_text().split("## Amendments")[0] + "## Amendments\n"
    actual = hashlib.sha256(frozen.encode()).hexdigest()
    assert actual == RULE_SHA, "Predeclared sampling rule was changed"
    games = sorted(load_games(), key=rank)
    chosen, empty = [], []
    for optimizer in OPTIMIZERS:
        for deck in DECKS:
            for seat in SEATS:
                pool = [g for g in games if (g["optimizer"], g["deck"], g["seat"]) == (optimizer, deck, seat)
                        and g["outcome"] == "loss"]
                if pool:
                    chosen.append({**pool[0], "tier": "A"})
                else:
                    empty.append(["A", optimizer, deck, seat])
    picked = {game_id(g) for g in chosen}
    for deck in DECKS:
        pool = [g for g in games if g["deck"] == deck and g["outcome"] == "loss" and game_id(g) not in picked]
        for label, key in (("B-short", lambda g: g["decisions"]), ("B-long", lambda g: -g["decisions"])):
            pool = [g for g in pool if game_id(g) not in picked]
            if pool:
                game = min(pool, key=key)  # games is rank-sorted and min is stable
                chosen.append({**game, "tier": label})
                picked.add(game_id(game))
            else:
                empty.append([label, deck])
    rotation = itertools.cycle(OPTIMIZERS)
    for deck in DECKS:
        for seat in SEATS:
            first = next(rotation)
            order = OPTIMIZERS[OPTIMIZERS.index(first):] + OPTIMIZERS[:OPTIMIZERS.index(first)]
            for optimizer in order:
                pool = [g for g in games if (g["optimizer"], g["deck"], g["seat"]) == (optimizer, deck, seat)
                        and g["outcome"] == "win"]
                if pool:
                    chosen.append({**pool[0], "tier": "C-win"})
                    break
            else:
                empty.append(["C", deck, seat])
    report = {"rule_sha256_frozen_part": RULE_SHA, "empty": empty, "selected": chosen}
    (OUT / "selected.json").write_text(json.dumps(report, indent=1))
    (EVIDENCE / "selected-games.json").write_text(json.dumps(report, indent=1))
    for game in chosen:
        print(game["tier"], game_id(game), game["deck"], "vs", game["opp_deck"], "seat", game["seat"],
              game["outcome"], game["decisions"], "reason", game["win_reason"])
    print("empty strata:", empty)


def load_diag(game):
    path = cell_dir("diag", game["optimizer"], game["deck0"], game["deck1"], game["seat"]) / "diag-1.jsonl.gz"
    with gzip.open(path, "rt") as stream:
        return [r for r in map(json.loads, stream) if r["env"] == game["env"]]


def field_text(entries):
    out = []
    for e in entries:
        if e.get("name"):
            if "atk" in e and e["atk"] is not None:
                out.append(f"{e['name']} {e['atk']}/{e['def']} {e['pos']}" + (" [disabled]" if e.get("disabled") else ""))
            else:
                out.append(f"{e['name']} ({e['pos']})")
        else:
            out.append(f"face-down({e['pos']})")
    return ", ".join(out) or "-"


def board_text(record):
    board, lp = record["board"], record["lp"]
    counts = board["counts"]
    lines = [f"    LP me {lp[0]} / opp {lp[1]} | deck {counts['my_deck']}/{counts['opp_deck']} | opp hand {counts['opp_hand']}",
             f"    hand: {', '.join(board['hand']) or '-'}",
             f"    my monsters: {field_text(board['field'])}",
             f"    my back row: {field_text(board['backrow'])}",
             f"    opp monsters: {field_text(board['opp_field'])}",
             f"    opp back row: {field_text(board['opp_backrow'])}",
             f"    my GY: {', '.join(board['grave']) or '-'} | opp GY: {', '.join(board['opp_grave']) or '-'}"]
    if board["banished"] or board["opp_banished"]:
        lines.append(f"    banished me: {', '.join(board['banished']) or '-'} | opp: {', '.join(board['opp_banished']) or '-'}")
    if board["opp_hand_revealed"]:
        lines.append(f"    opp hand revealed to me: {', '.join(board['opp_hand_revealed'])}")
    return lines


def option_text(option):
    parts = []
    if option.get("phase"):
        return option["phase"]
    if option.get("finish"):
        return "finish selection"
    if option["act"] != "none":
        parts.append(option["act"])
    if option.get("card"):
        parts.append(option["card"])
    if option.get("where"):
        shown_pos = option.get("card_pos") if option["where"] not in ("hand", "deck", "extra") else ""
        parts.append(f"[{option['where']}" + (f" {shown_pos}" if shown_pos else "") + "]")
    if "atk" in option and option.get("where") in ("mzone", "opp_mzone"):
        parts.append(f"{option['atk']}/{option['def']}")
    if option.get("position"):
        parts.append("position " + option["position"])
    if option.get("effect"):
        parts.append(f"effect#{option['effect']}")
    for key in ("number", "place", "attribute"):
        if option.get(key):
            parts.append(f"{key} {option[key]}")
    return " ".join(parts) or option["msg"]


def public_text(event):
    name = event.get("card", "")
    words = {"to_battle": "enters Battle Phase", "to_main2": "enters Main Phase 2", "to_end": "ends turn",
             "normal_summon": f"Normal Summons {name}", "special_summon": f"Special Summons {name}",
             "activate": f"activates {name}", "attack": f"attacks with {name}",
             "direct_attack": f"attacks directly with {name}", "sets_monster": "sets a monster",
             "sets_spell_trap": "sets a Spell/Trap", "changes_position": "changes a monster's position",
             "selects": f"selects {name}"}
    return words.get(event["event"], event["event"])


def render_game(game, detail=True):
    records = load_diag(game)
    lines = [f"# Game {game_id(game)} tier={game.get('tier', 'lookup')}",
             f"# Learner: {game['deck']} seat {game['seat']} optimizer {game['optimizer']}; opponent: heuristic with {game['opp_deck']}",
             f"# Result: learner {game['outcome'].upper()} win_reason={game['win_reason']} decisions={game['decisions']}",
             "# Redaction: learner's view only. Opponent lines are events a real duel makes public.",
             "# Each offered choice: probability / heuristic score. '>>' chosen, 'H' top heuristic score."]
    last_board, last_turn = None, None
    pending = []
    for record in records:
        if record["turn"] != last_turn:
            lines += pending; pending = []
            last_turn = record["turn"]
            lines.append(f"== Turn {record['turn']} ==")
        if record["role"] == "opponent":
            if "public" in record:
                pending.append(f"  opp {record['step']}: {public_text(record['public'])} ({record['phase']})")
            continue
        lines += pending; pending = []
        board_lines = board_text(record)
        if board_lines != last_board:
            lines += board_lines
            last_board = board_lines
        context = record["context"]
        head = f"  #{record['step']} {record['phase']} {record['msg']}"
        if context["active"]:
            head += (f" hint={context['hint_name']} stage={context['stage']} source={context['source']}"
                     f" bounds={context['bounds']} selected={context['selected']}")
        lines.append(head)
        best = max(record["scores"])
        for index, option in enumerate(record["options"]):
            mark = ">>" if index == record["choice"] else "  "
            top = "H" if record["scores"][index] == best else " "
            lines.append(f"   {mark}{top} p={record['probs'][index]:.2f} s={record['scores'][index]:+.2f} {option_text(option)}")
    lines += pending
    lines.append(f"== END: learner {game['outcome'].upper()} win_reason={game['win_reason']} ==")
    return "\n".join(lines) + "\n"


def short(name):
    if not name:
        return "fd"
    for old, new in (("Blackwing - ", "BW:"), ("the Emblem of Honor", ""), ("the Moon Shadow", ""), ("the Spear", ""),
                     ("the Blue Flame", ""), ("the Dawn", ""), ("the Far North", ""), ("the Whirlwind", ""),
                     ("the Shadow Monarch", ""), ("the Storm Monarch", ""), ("the Firestorm Monarch", ""),
                     ("the Emissary of Darkness", ""), ("Mystical Space Typhoon", "MST"),
                     ("Compulsory Evacuation Device", "Compulsory"), ("Bottomless Trap Hole", "Bottomless"),
                     ("Torrential Tribute", "Torrential"), ("Light and Darkness Dragon", "LADD")):
        name = name.replace(old, new)
    return name.strip()


def compact_field(entries):
    return ",".join((f"{short(e['name'])} {e['atk']}{'d' if e['pos'] != 'ATK' else ''}" if e.get("atk") is not None else
                     short(e.get("name")) + ("(fd)" if e.get("name") else "")) for e in entries) or "-"


def compact_board(record):
    board, lp = record["board"], record["lp"]
    return (f" {lp[0]}/{lp[1]} oH{board['counts']['opp_hand']} H[{','.join(short(n) for n in board['hand'])}] "
            f"M[{compact_field(board['field'])}] B[{compact_field(board['backrow'])}] "
            f"oM[{compact_field(board['opp_field'])}] oB[{compact_field(board['opp_backrow'])}] "
            f"GY{len(board['grave'])}/{len(board['opp_grave'])} dk{board['counts']['my_deck']}/{board['counts']['opp_deck']}")


def compact_option(option):
    for old, new in (("normal_summon", "NS"), ("special_summon", "SS"), ("monster_set", "MSET"), ("activate", "ACT"),
                     ("direct_attack", "DIRECT"), ("attack", "ATK"), ("reposition", "REPO")):
        if option["act"] == old:
            option = {**option, "act": new}
    text = option_text({**option, "card": short(option.get("card")) if option.get("card") else None,
                         "card_pos": "" if option.get("where") == "hand" else option.get("card_pos")})
    return text.replace(" [hand]", "").replace(" [deck]", "@deck").replace(" [extra]", "@xtra").replace("[opp_mzone ", "[o ").replace("[mzone ", "[").strip()


def render_compact(game):
    lines = [f"# {game_id(game)} tier={game.get('tier')} {game['deck']} vs {game['opp_deck']} seat{game['seat']} "
             f"{game['outcome'].upper()} reason={game['win_reason']} dec={game['decisions']}"]
    last_turn, last_board, pending = None, None, []
    for record in load_diag(game):
        if record["turn"] != last_turn:
            lines += ["".join(pending)] if pending else []
            pending = []
            last_turn = record["turn"]
            lines.append(f"T{record['turn']}")
        if record["role"] == "opponent":
            if "public" in record and record["public"]["event"] not in ("to_end",):
                pending.append(f" o:{public_text(record['public'])};")
            continue
        if record["class"] == "other" and record["msg"] == "place":
            continue
        options = record["options"]
        if len(options) == 1:
            continue
        if pending:
            lines.append("".join(pending)); pending = []
        board = compact_board(record)
        if board != last_board:
            lines.append(board); last_board = board
        choice, scores, probs = record["choice"], record["scores"], record["probs"]
        top = max(range(len(scores)), key=lambda i: scores[i])
        modal = max(range(len(probs)), key=lambda i: probs[i])
        context = record["context"]
        hint = f" {context['hint_name']}" if context["active"] else ""
        source = f" src={short(context['source'])}" if context.get("source") else ""
        text = f"#{record['step']} {record['phase'].replace('main', 'M').replace('battle-', 'B')[:6]}/{record['msg'].replace('select_', '')[:6]}{hint}{source} n={len(options)} >> {compact_option(options[choice])} p{probs[choice]:.2f} s{scores[choice]:+.1f}"
        if scores[top] > scores[choice]:
            text += f" | H: {compact_option(options[top])} p{probs[top]:.2f} s{scores[top]:+.1f}"
        if modal not in (choice, top):
            text += f" | modal: {compact_option(options[modal])} p{probs[modal]:.2f}"
        lines.append(text)
    lines += ["".join(pending)] if pending else []
    lines.append(f"END learner {game['outcome'].upper()}")
    return "\n".join(lines) + "\n"


def cmd_render(args):
    selected = json.loads((OUT / "selected.json").read_text())["selected"]
    all_games = {game_id(g): g for g in load_games()}
    targets = [all_games[args.game]] if args.game else selected
    directory = OUT / "games"
    directory.mkdir(exist_ok=True)
    for folder in ("games-compact", "games-filtered"):
        (OUT / folder).mkdir(exist_ok=True)
    for game in targets:
        compact = render_compact(game)
        (OUT / "games-compact" / (game_id(game).replace("/", "_") + ".log")).write_text(compact)
        (OUT / "games-filtered" / (game_id(game).replace("/", "_") + ".log")).write_text(filtered_view(compact))
        text = render_game(game)
        name = game_id(game).replace("/", "_") + ".log"
        (directory / name).write_text(text)
        if args.game:
            print(text)
    print("rendered", len(targets), "to", directory)


def cmd_curve(args):
    """Argmax play of archived intermediate checkpoints; sampled numbers come from the archive."""
    curve = {}
    for round_ in (8, 16, 24):
        jobs = []
        for optimizer, deck0, deck1, seat in cells():
            source = ROOT / f"tests/edison/reports/rl-budget-2026-11-02/seed-{optimizer}/checkpoint-{round_}.json.gz"
            target = OUT / "checkpoints" / f"seed-{optimizer}-round-{round_}.json"
            target.parent.mkdir(exist_ok=True)
            if not target.exists():
                target.write_bytes(gzip.open(source).read())
            jobs.append((f"curve/round-{round_}", "argmax", optimizer, deck0, deck1, seat, "heuristic", target))
        results = execute(jobs, args.jobs)
        curve[str(round_)] = {str(o): sum(r["wins"] for r in results if r["optimizer"] == o) for o in OPTIMIZERS}
        print(round_, curve[str(round_)], flush=True)
    surgery = json.loads((OUT / "surgery.json").read_text())
    curve["32"] = {k: v for k, v in surgery["argmax"]["wins_by_optimizer"].items()}
    archive_root = ROOT / "tests/edison/reports/rl-budget-2026-11-02/validation"
    sampled = {}
    for round_ in (0, 8, 16, 24, 32):
        sampled[str(round_)] = {str(o): json.loads(gzip.open(archive_root / str(o) / f"round-{round_}-vs-heuristic.json.gz", "rt").read())["wins"]
                                for o in OPTIMIZERS}
    entropy = {}
    for optimizer in OPTIMIZERS:
        report = json.loads(gzip.open(ROOT / f"tests/edison/reports/rl-budget-2026-11-02/seed-{optimizer}/training-report.json.gz", "rt").read())
        entropy[str(optimizer)] = {str(r["round"]): round(r["history"][0]["entropy"], 3) for r in report["rounds"] if r["round"] in (1, 8, 16, 24, 32)}
    output = {"argmax_wins_of_96": curve, "sampled_wins_of_96_archive": sampled, "weighted_entropy_first_epoch": entropy}
    (OUT / "curve.json").write_text(json.dumps(output, indent=1))
    (EVIDENCE / "curve.json").write_text(json.dumps(output, indent=1))
    print(json.dumps(output))


def outcomes_by_key(kind):
    return {(g["optimizer"], g["deck0"], g["deck1"], g["seat"], g["seed"]): g for g in load_games(kind)}


def cmd_surgery_report(args):
    import random
    surgery = json.loads((OUT / "surgery.json").read_text())
    streams = ["plain", "surgery/resample-1", "surgery/resample-2", "surgery/resample-3"]
    baseline = {}
    for kind in streams:
        for key, game in outcomes_by_key(kind).items():
            baseline.setdefault(key, []).append(game["outcome"] == "win")
    base_rate = {k: sum(v) / len(v) for k, v in baseline.items()}
    keys = sorted(base_rate)
    units = sorted({k[1:] for k in keys})
    rng = random.Random(40000000)
    report = {"baseline_streams": streams, "baseline_mean_wins": round(sum(base_rate.values()), 2),
              "baseline_stream_wins": {s: sum(g["outcome"] == "win" for g in outcomes_by_key(s).values()) for s in streams},
              "variants": {}, "note": "Bootstrap resamples 96 (pairing, seat, duel seed) units with their 3 optimizers. "
              "It ignores optimizer-seed variance and treats the validation seeds as a sample; descriptive only."}
    report["baseline_by_optimizer"] = {str(o): round(sum(v for k, v in base_rate.items() if k[0] == o), 2) for o in OPTIMIZERS}
    def split(values):
        out = {}
        for label, select in (("deck", lambda g: g["deck"]), ("seat", lambda g: g["seat"]), ("opp_deck", lambda g: g["opp_deck"]),
                              ("deck_seat", lambda g: f"{g['deck']} seat{g['seat']}")):
            table = {}
            for key, value in values.items():
                game = plain[key]
                table.setdefault(str(select(game)), []).append(value)
            out[label] = {k: round(sum(v) / len(v), 4) for k, v in sorted(table.items())}
        return out
    plain = outcomes_by_key("plain")
    report["baseline_splits"] = split(base_rate)
    for mode, entry in surgery.items():
        if mode == "control" or "@" in mode or mode.startswith("resample"):
            continue
        games = outcomes_by_key("surgery/" + mode.replace(":", "-"))
        wins = {k: float(g["outcome"] == "win") for k, g in games.items()}
        by_unit = {u: sum(wins[(o, *u)] - base_rate[(o, *u)] for o in OPTIMIZERS) for u in units}
        draws = []
        for _ in range(4000):
            sample = [by_unit[rng.choice(units)] for _ in units]
            draws.append(sum(sample) / (3 * len(units)))
        draws.sort()
        per_optimizer = {str(o): round(sum(wins[k] - base_rate[k] for k in keys if k[0] == o) / 96, 4) for o in OPTIMIZERS}
        turns = sorted(g["decisions"] for g in games.values())
        report["variants"][mode] = {"wins": entry["wins"], "games": 288, "rate": round(entry["wins"] / 288, 4),
                                    "gain_vs_baseline_mean": round(sum(wins[k] - base_rate[k] for k in keys) / 288, 4),
                                    "bootstrap_95": [round(draws[100], 4), round(draws[3899], 4)],
                                    "gain_by_optimizer": per_optimizer, "median_decisions": turns[144],
                                    "max_decisions": turns[-1], "splits": split(wins)}
    control = surgery["control"]["winners"]
    deck_wins = {}
    for pairing, winners in control.items():
        deck0 = pairing.split(" ")[0]; deck1 = pairing.split(" ")[-2]
        for winner in winners:
            for seat, deck in ((0, deck0), (1, deck1)):
                bucket = deck_wins.setdefault(deck, [0, 0])
                bucket[0] += winner == seat; bucket[1] += 1
                bucket = deck_wins.setdefault(f"{deck} seat{seat}", [0, 0])
                bucket[0] += winner == seat; bucket[1] += 1
    report["control_heuristic_mirror"] = {"winners": control, "by_deck": deck_wins,
                                          "seat0_wins": sum(w == 0 for v in control.values() for w in v), "games": 48}
    report["vs_random"] = {k: v for k, v in surgery.items() if "@" in k}
    (OUT / "surgery-report.json").write_text(json.dumps(report, indent=1))
    (EVIDENCE / "surgery-report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps({k: v for k, v in report.items() if k != "variants"}, indent=1))
    for mode, value in report["variants"].items():
        print(mode, value["wins"], value["gain_vs_baseline_mean"], value["bootstrap_95"], value["gain_by_optimizer"], value["median_decisions"], value["max_decisions"])
        print("   ", value["splits"]["deck_seat"])


def cmd_audit(args):
    """Redaction audit over every diagnostic record, and over rendered sample logs."""
    allowed_opponent = {"env", "step", "seat", "role", "turn", "phase", "public"}
    allowed_events = {"to_battle", "to_main2", "to_end", "normal_summon", "special_summon", "activate", "attack",
                      "direct_attack", "sets_monster", "sets_spell_trap", "changes_position", "selects"}
    audit = {"learner_records": 0, "opponent_records": 0, "opponent_public": 0, "opp_hand_revealed_records": 0,
             "opp_facedown_with_name": 0, "opponent_deck_or_hand_option_named": 0, "violations": []}
    for cell in cells():
        with gzip.open(cell_dir("diag", *cell) / "diag-1.jsonl.gz", "rt") as stream:
            for record in map(json.loads, stream):
                if record["role"] == "opponent":
                    audit["opponent_records"] += 1
                    if set(record) - allowed_opponent:
                        audit["violations"].append(["opponent keys", str(cell), record["step"]])
                    if "public" in record:
                        audit["opponent_public"] += 1
                        if record["public"]["event"] not in allowed_events:
                            audit["violations"].append(["event", str(cell), record["step"]])
                    continue
                audit["learner_records"] += 1
                board = record["board"]
                audit["opp_hand_revealed_records"] += bool(board["opp_hand_revealed"])
                audit["opp_facedown_with_name"] += sum(1 for e in board["opp_field"] + board["opp_backrow"] if e["pos"] == "set" and e["name"])
                for option in record["options"]:
                    if option.get("where") in ("opp_hand", "opp_deck") and option.get("card") != "(hidden)":
                        audit["opponent_deck_or_hand_option_named"] += 1
    (OUT / "redaction-audit.json").write_text(json.dumps(audit, indent=1))
    (EVIDENCE / "redaction-audit.json").write_text(json.dumps(audit, indent=1))
    print(json.dumps(audit, indent=1))
    assert not audit["violations"]


def filtered_view(text):
    """The reduced reading view used for 17 of the 30 reviewed games."""
    keep, first = [], True
    for line in text.splitlines():
        if (line.startswith("#") and not line[1:2].isdigit()) or re.fullmatch(r"T\d+", line):
            first = first or bool(re.fullmatch(r"T\d+", line))
            keep.append(line)
        elif line.startswith(" o:") or line.startswith("END"):
            keep.append(line[:220])
        elif line.startswith(" "):
            if first:
                keep.append(line)
                first = False
        else:
            chosen = re.search(r">> .*? p([\d.]+) s([+-][\d.]+)", line)
            top = re.search(r"\| H: .*? p[\d.]+ s([+-][\d.]+)", line)
            if chosen and (float(chosen.group(1)) < 0.15 or (top and float(top.group(1)) - float(chosen.group(2)) >= 5)):
                keep.append(line)
    return "\n".join(keep) + "\n"


def cmd_controls(args):
    """Reference numbers cited in the report: first-action policy, mirror starts, hand limit, summons."""
    jobs = [("curve/round-0", "argmax", o, d0, d1, seat, "heuristic",
             ROOT / f"tests/edison/reports/rl-budget-2026-11-02/seed-{o}/initial-policy.json")
            for o, d0, d1, seat in cells() if o == OPTIMIZERS[0]]
    first_action = execute(jobs, args.jobs)
    report = {"first_offered_action_wins": sum(r["wins"] for r in first_action), "first_offered_action_games": 8 * len(first_action)}
    winners = json.loads((OUT / "surgery.json").read_text())["control"]["winners"]
    mirror = {}
    for pairing, row in winners.items():
        deck0, deck1 = pairing.split(" ")[0], pairing.split(" ")[-2]
        for index, winner in enumerate(row):
            mirror[(deck0, deck1, BASE_SEED + index)] = winner
    streams = {"sampled_4_streams": ["plain", "surgery/resample-1", "surgery/resample-2", "surgery/resample-3"],
               "greedy": ["surgery/argmax"]}
    report["by_mirror_start"] = {}
    for label, kinds in streams.items():
        table = {}
        for kind in kinds:
            for game in load_games(kind):
                key = "mirror_won" if mirror[(game["deck0"], game["deck1"], game["seed"])] == game["seat"] else "mirror_lost"
                bucket = table.setdefault(key, {"win": 0, "loss": 0, "draw": 0})
                bucket[game["outcome"]] += 1
        report["by_mirror_start"][label] = table
    plain = {game_id(g): g["outcome"] for g in load_games("plain")}
    greedy = {game_id(g): g["outcome"] for g in load_games("surgery/argmax")}
    flips = {}
    for key, outcome in plain.items():
        name = f"sampled_{outcome}_greedy_{greedy[key]}"
        flips[name] = flips.get(name, 0) + 1
    report["same_start_sampled_vs_greedy"] = flips
    own_turns = over_limit = 0
    summons = {"swap_frog_same_turn": 0, "no_swap_frog": []}
    for game in load_games("plain"):
        records = load_diag(game)
        last = {}
        seen = {}
        for record in records:
            if record["role"] == "learner":
                choice = record["options"][record["choice"]]
                who, act, card = "learner", choice["act"], choice.get("card")
                if record["my_turn"]:
                    last[record["turn"]] = record
            else:
                event = record.get("public")
                if not event:
                    continue
                who, act, card = "opponent", event["event"], event.get("card")
            entry = seen.setdefault((who, record["turn"]), {"count": 0, "swap": False})
            entry["swap"] |= act == "activate" and card == "Swap Frog"
            if act == "normal_summon":
                entry["count"] += 1
                if entry["count"] == 2:
                    if entry["swap"]:
                        summons["swap_frog_same_turn"] += 1
                    else:
                        summons["no_swap_frog"].append([game_id(game), who, record["turn"]])
        own_turns += len(last)
        over_limit += sum(len(r["board"]["hand"]) > 6 for r in last.values())
    report["own_turns"], report["hand_over_6_at_last_decision_of_own_turn"] = own_turns, over_limit
    report["double_normal_summon_labels"] = summons
    (OUT / "controls.json").write_text(json.dumps(report, indent=1))
    (EVIDENCE / "controls.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    sys.path.insert(0, str(ROOT / "tools/edison"))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("run"); p.add_argument("--jobs", type=int, default=6); p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_run)
    p = sub.add_parser("surgery"); p.add_argument("--jobs", type=int, default=6); p.add_argument("--modes", nargs="*"); p.add_argument("--control", action="store_true")
    p.set_defaults(func=cmd_surgery)
    p = sub.add_parser("verify"); p.set_defaults(func=cmd_verify)
    p = sub.add_parser("select"); p.set_defaults(func=cmd_select)
    p = sub.add_parser("render"); p.add_argument("--game"); p.set_defaults(func=cmd_render)
    p = sub.add_parser("curve"); p.add_argument("--jobs", type=int, default=5); p.set_defaults(func=cmd_curve)
    p = sub.add_parser("surgery-report"); p.set_defaults(func=cmd_surgery_report)
    p = sub.add_parser("audit"); p.set_defaults(func=cmd_audit)
    p = sub.add_parser("controls"); p.add_argument("--jobs", type=int, default=6); p.set_defaults(func=cmd_controls)
    args = parser.parse_args()
    args.func(args)
