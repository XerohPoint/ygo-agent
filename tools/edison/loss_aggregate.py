#!/usr/bin/env python3
"""Aggregate the diagnostic decisions of ALL validation games (wins, losses, draws).

Reads build/edison-loss-review-2026-11-04/diag/. Writes per-game rows and summary
tables as JSON. Mechanical "blunder" detectors are post-hoc: they were written after
reading sampled games, and are labelled so in the report. Heuristic scores are a
benchmark at the same decision, not ground truth.
"""
from collections import Counter, defaultdict
import gzip
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/edison"))
from loss_review import OUT, EVIDENCE, cell_dir, cells, game_id, load_games  # noqa: E402

GAME_KEYS = ("optimizer", "deck", "opp_deck", "seat", "env")


def records_of(game, kind="diag", cache={}):
    key = (game["optimizer"], game["deck0"], game["deck1"], game["seat"])
    if key not in cache:
        cache.clear()
        by_env = defaultdict(list)
        with gzip.open(cell_dir(kind, *key) / "diag-1.jsonl.gz", "rt") as stream:
            for record in map(json.loads, stream):
                by_env[record["env"]].append(record)
        cache[key] = by_env
    return cache[key][game["env"]]


def rate(numerator, denominator):
    return round(numerator / denominator, 4) if denominator else None


def is_phase(option):
    return "phase" in option


def analyse_decision(record, tally):
    """Update per-game tallies from one learner decision."""
    options, choice, scores, probs = record["options"], record["choice"], record["scores"], record["probs"]
    chosen = options[choice]
    board = record["board"]
    msg = record["msg"]
    best = max(scores)
    multi = len(options) > 1
    if multi and msg != "place":
        tally["choice_prompts"] += 1
        tally["agree"] += scores[choice] == best
        tally["gap_sum"] += best - scores[choice]
        tally["big_gap"] += best - scores[choice] >= 5
        tally["low_p"] += probs[choice] < 0.10
        tally["low_p_vs_modal"] += probs[choice] < 0.10 and max(probs) >= 0.5
    if msg == "idle":
        acts = {o["act"] for o in options if not is_phase(o)}
        if record["my_turn"]:
            if "normal_summon" in acts:
                tally["idle_ns_offered"] += 1
                tally["idle_ns_taken"] += chosen["act"] == "normal_summon"
            if "special_summon" in acts:
                tally["idle_ss_offered"] += 1
                tally["idle_ss_taken"] += chosen["act"] == "special_summon"
            if acts - {"reposition"}:
                tally["idle_action_offered"] += 1
                tally["idle_pass"] += is_phase(chosen)
            summons = [o for o in options if o["act"] == "normal_summon"]
            if len(summons) > 1 and chosen["act"] == "normal_summon":
                powers = [max(o.get("atk", 0), 0) for o in summons]
                tally["ns_multi"] += 1
                tally["ns_highest"] += chosen.get("atk", 0) == max(powers)
                tally["ns_lowest"] += chosen.get("atk", 0) == min(powers)
            # A turn ended with no monster, with a summon available and nothing done.
            if is_phase(chosen) and not board["field"] and ({"normal_summon", "monster_set", "special_summon"} & acts):
                tally["pass_empty_board_with_summon"] += 1
    if msg == "battle":
        attacks = [o for o in options if o["act"] in ("attack", "direct_attack")]
        direct = [o for o in options if o["act"] == "direct_attack"]
        if attacks:
            tally["battle_attack_offered"] += 1
            tally["battle_attack_taken"] += chosen["act"] in ("attack", "direct_attack")
        if direct:
            tally["battle_direct_offered"] += 1
            tally["battle_direct_taken"] += chosen["act"] == "direct_attack"
    if msg == "chain":
        activations = [i for i, o in enumerate(options) if o["act"] == "activate"]
        if activations and any(o["act"] == "decline" for o in options):
            safe = any(scores[i] > 0 for i in activations)
            label = "chain_safe" if safe else "chain_unsafe"
            tally[label + "_offered"] += 1
            tally[label + "_taken"] += chosen["act"] == "activate"
    if msg == "select_position":
        tally["position_prompts"] += 1
        tally["position_defense_highatk"] += chosen.get("position") == "DEF" and chosen.get("atk", 0) >= 1600
    context = record["context"]
    if context["active"] and multi:
        enemy = [(i, o) for i, o in enumerate(options) if o.get("where") == "opp_mzone" and o.get("card") != "(hidden)"]
        if enemy and all(o.get("where") == "opp_mzone" for o in options if not o.get("finish")) and context["hint"] != 549:
            values = [max(o.get("atk", 0), o.get("def", 0)) for _, o in enemy]
            if max(values) > min(values):
                value = max(chosen.get("atk", 0), chosen.get("def", 0))
                key = f"enemy_target:{context['source'] or 'unknown'}"
                tally["enemy_target_prompts"] += 1
                tally["enemy_target_max"] += value == max(values)
                tally["enemy_target_min"] += value == min(values)
                tally["heuristic_top_max"] += 1
    if context["hint"] == 549 and multi:
        tally["attack_target_prompts"] += 1
    # Post-hoc blunder detectors (see module docstring).
    name = chosen.get("card")
    if chosen["act"] == "activate" and name:
        own_back = len(board["backrow"])
        opp_back = len(board["opp_backrow"])
        own_mon, opp_mon = len(board["field"]), len(board["opp_field"])
        where = chosen.get("where")
        if name == "Heavy Storm":
            # The Storm itself is one of "own back" only when it was set first.
            own_other = own_back - (1 if where == "szone" else 0)
            tally["heavy_storm"] += 1
            tally["heavy_storm_self_hurt"] += own_other > opp_back
        if name == "Black Rose Dragon":
            tally["black_rose_nuke"] += 1
            tally["black_rose_self_hurt"] += own_mon + own_back > opp_mon + opp_back
        if name == "Torrential Tribute":
            tally["torrential"] += 1
            tally["torrential_self_hurt"] += own_mon > opp_mon
        if name == "Soul Exchange":
            tally["soul_exchange"] += 1
        if name == "Solemn Judgment":
            tally["solemn"] += 1
        if name == "Enemy Controller":
            tally["enemy_controller"] += 1
        tally["activate_total"] += 1
        tally["activate_unsafe"] += scores[choice] < 0


def attack_outcomes(records, tally):
    """Suicide attacks: chosen target known to beat the attacker, at declaration."""
    for index, record in enumerate(records):
        if record["role"] != "learner":
            continue
        context = record["context"]
        options, chosen = record["options"], record["options"][record["choice"]]
        if context["hint"] == 549:
            attacker = None
            # Source is the attacker; its ATK is on the visible board.
            source = context["source"]
            for entry in record["board"]["field"]:
                if entry.get("name") == source:
                    attacker = entry["atk"] if attacker is None else max(attacker, entry["atk"])
            if attacker is None or chosen.get("card") in (None, "(hidden)"):
                tally["attack_into_facedown"] += chosen.get("card") == "(hidden)"
                continue
            value = chosen.get("atk", 0) if chosen.get("card_pos") == "ATK" else chosen.get("def", 0)
            tally["attacks_with_target_known"] += 1
            tally["suicide_attacks"] += attacker < value
        elif record["msg"] == "battle" and chosen["act"] == "attack":
            tally["attacks_declared"] += 1
            atk = chosen.get("atk", 0)
            visible = [e for e in record["board"]["opp_field"]]
            known = [e for e in visible if e.get("name")]
            if known and len(known) == len(visible):
                values = [e["atk"] if e["pos"] == "ATK" else e["def"] for e in known]
                tally["attack_declared_all_known"] += 1
                tally["attack_declared_no_favourable"] += atk < min(values)
        elif record["msg"] == "battle" and chosen["act"] == "direct_attack":
            tally["direct_attacks_declared"] += 1


MONARCHS = {"Caius the Shadow Monarch", "Raiza the Storm Monarch", "Thestalos the Firestorm Monarch",
            "Light and Darkness Dragon", "Gorz the Emissary of Darkness"}


def card_patterns(records, learner, tally):
    """Post-hoc card-level patterns seen in hand review, counted over every game."""
    for index, record in enumerate(records):
        if record["role"] != "learner":
            continue
        options, chosen = record["options"], record["options"][record["choice"]]
        if len(options) > 1 and record["msg"] != "place":
            deviated = record["choice"] != record["probs"].index(max(record["probs"]))
            tally["deviate_" + record["class"]] += deviated
            tally["prompts_" + record["class"]] += 1
            tally["expected_deviate_" + record["class"] + "_x1000"] += int(round(1000 * (1 - max(record["probs"]))))
            if deviated and record["scores"][record["choice"]] < record["scores"][record["probs"].index(max(record["probs"]))]:
                tally["deviate_to_lower_score"] += 1
        name = chosen.get("card")
        if chosen["act"] == "activate" and record["phase"] == "draw":
            tally["draw_phase_activation"] += 1
            if chosen.get("where") == "hand":
                tally["draw_phase_activation_from_hand"] += 1
        if chosen["act"] == "activate" and name == "Solemn Judgment":
            # Find what is being negated: walk back through this chain.
            own_activation = False
            cause = "other"
            for earlier in reversed(records[:index]):
                if earlier["role"] == "learner":
                    pick = earlier["options"][earlier["choice"]]
                    if pick["act"] == "activate" and earlier["turn"] == record["turn"]:
                        own_activation = True
                        break
                else:
                    event = earlier.get("public", {}).get("event")
                    if event in ("normal_summon", "special_summon", "activate", "sets_monster", "sets_spell_trap"):
                        cause = event
                        break
            label = "own_card" if own_activation else cause
            tally["solemn_on_" + label] += 1
            tally["solemn_lp_paid"] += record["lp"][0] // 2
        if chosen["act"] == "activate" and name == "Soul Exchange":
            turn_later = [r for r in records[index + 1:] if r["turn"] == record["turn"] and r["role"] == "learner"]
            used = any(r["options"][r["choice"]]["act"] == "normal_summon" and
                       r["options"][r["choice"]].get("card") in MONARCHS for r in turn_later)
            tally["soul_exchange_followed_by_tribute_summon"] += used
        if chosen["act"] == "activate" and name == "Enemy Controller":
            tally["enemy_controller_draw_phase"] += record["phase"] == "draw"
            tally["enemy_controller_opp_turn"] += not record["my_turn"]
        if record["msg"] == "idle" and chosen.get("phase"):
            # Holding a settable card through the turn end.
            if chosen["phase"] == "to_end" and any(o["act"] == "set" for o in options):
                tally["end_turn_with_set_offered"] += 1
            if chosen["phase"] == "to_end":
                tally["end_turn_idle"] += 1
            if chosen["phase"] == "to_end" and len(record["board"]["hand"]) > 6:
                tally["end_turn_over_hand_limit"] += 1
    # Tempo over the learner's first own turns.
    own_turns = sorted({r["turn"] for r in learner if r["my_turn"]})
    for number, turn in enumerate(own_turns[:6], 1):
        in_turn = [r for r in learner if r["turn"] == turn]
        picks = [r["options"][r["choice"]]["act"] for r in in_turn]
        tally[f"t{number}_seen"] += 1
        tally[f"t{number}_summon_or_set_monster"] += any(a in ("normal_summon", "special_summon", "monster_set") for a in picks)
        tally[f"t{number}_attack"] += any(a in ("attack", "direct_attack") for a in picks)
        tally[f"t{number}_field_end"] += len(in_turn[-1]["board"]["field"]) if in_turn else 0
        tally[f"t{number}_oppfield_end"] += len(in_turn[-1]["board"]["opp_field"]) if in_turn else 0


def analyse_game(game, kind="diag"):
    records = records_of(game, kind)
    tally = Counter()
    learner = [r for r in records if r["role"] == "learner"]
    opponent = [r for r in records if r["role"] == "opponent"]
    for record in learner:
        analyse_decision(record, tally)
    attack_outcomes(records, tally)
    card_patterns(records, learner, tally)
    public = [r["public"] for r in opponent if "public" in r]
    for event in public:
        tally["opp_" + event["event"]] += 1
    last_turn = max(r["turn"] for r in records)
    my_turns = sorted({r["turn"] for r in learner if r["my_turn"]})
    # LP as seen at the learner's last decision of each turn.
    lp_by_turn = {}
    for record in learner:
        lp_by_turn[record["turn"]] = record["lp"]
    first_blood = None
    for turn in sorted(lp_by_turn):
        me, opp = lp_by_turn[turn]
        if me < 8000 or opp < 8000:
            first_blood = "me" if me < 8000 and opp == 8000 else "opp" if opp < 8000 and me == 8000 else "both"
            break
    row = {k: game[k] for k in GAME_KEYS}
    final_events = [e["event"] for e in public[-2:]]
    row.update(id=game_id(game), outcome=game["outcome"], decisions=game["decisions"],
               learner_decisions=len(learner), turns=last_turn, my_turns=len(my_turns),
               win_reason=game["win_reason"], lp_last_seen=learner[-1]["lp"] if learner else None,
               first_damage_taken_by=first_blood, last_opp_events=final_events,
               learner_ns=sum(1 for r in learner if r["options"][r["choice"]]["act"] == "normal_summon"),
               learner_ss=sum(1 for r in learner if r["options"][r["choice"]]["act"] == "special_summon"),
               learner_mset=sum(1 for r in learner if r["options"][r["choice"]]["act"] == "monster_set"),
               learner_activate=sum(1 for r in learner if r["options"][r["choice"]]["act"] == "activate"),
               lp_by_turn={str(k): v for k, v in lp_by_turn.items()}, tally=dict(tally))
    row["decision_rows"] = decision_rows(game, learner)
    return row


def decision_rows(game, learner):
    rows = []
    for record in learner:
        if len(record["options"]) < 2:
            continue
        choice, scores, probs = record["choice"], record["scores"], record["probs"]
        chosen = record["options"][choice]
        entropy = -sum(p * math.log(p) for p in probs if p > 0)
        rows.append({"msg": record["msg"], "cls": record["class"], "phase": record["phase"],
                     "turn": record["turn"], "my_turn": record["my_turn"],
                     "agree": scores[choice] == max(scores), "gap": max(scores) - scores[choice],
                     "p": probs[choice], "maxp": max(probs), "modal_agree": scores[probs.index(max(probs))] == max(scores),
                     "entropy": entropy, "n": len(probs),
                     "hint": record["context"]["hint"] if record["context"]["active"] else None,
                     "act": chosen["act"], "phasechoice": chosen.get("phase"), "card": chosen.get("card"),
                     "top_act": record["options"][scores.index(max(scores))]["act"],
                     "top_phase": record["options"][scores.index(max(scores))].get("phase"),
                     "top_card": record["options"][scores.index(max(scores))].get("card")})
    return rows


def group(rows, key, value=lambda r: r):
    out = defaultdict(list)
    for row in rows:
        out[key(row)].append(value(row))
    return out


def decision_table(games, key):
    table = {}
    grouped = defaultdict(lambda: defaultdict(list))
    for game in games:
        for row in game["decision_rows"]:
            if row["msg"] == "place":
                continue
            grouped[key(row, game)][game["outcome"]].append(row)
    for label, by_outcome in sorted(grouped.items(), key=lambda kv: str(kv[0])):
        entry = {}
        for outcome, rows in by_outcome.items():
            count = len(rows)
            entry[outcome] = {"prompts": count,
                              "agree": rate(sum(r["agree"] for r in rows), count),
                              "mean_gap": rate(sum(r["gap"] for r in rows), count),
                              "gap_ge5": rate(sum(r["gap"] >= 5 for r in rows), count),
                              "mean_maxp": rate(sum(r["maxp"] for r in rows), count),
                              "modal_agree": rate(sum(r["modal_agree"] for r in rows), count),
                              "p_lt_10": rate(sum(r["p"] < 0.1 for r in rows), count)}
        table[str(label)] = entry
    return table


def per_game_summary(games, outcome_filter=None):
    subset = [g for g in games if outcome_filter is None or g["outcome"] == outcome_filter]
    if not subset:
        return {}
    summary = {"games": len(subset)}
    for field in ("decisions", "learner_decisions", "turns", "my_turns", "learner_ns", "learner_ss", "learner_mset", "learner_activate"):
        values = sorted(g[field] for g in subset)
        summary[field] = {"mean": round(sum(values) / len(values), 2), "median": values[len(values) // 2],
                          "min": values[0], "max": values[-1]}
    keys = sorted({k for g in subset for k in g["tally"]})
    summary["per_game_mean"] = {k: round(sum(g["tally"].get(k, 0) for g in subset) / len(subset), 3) for k in keys}
    return summary


def main():
    # Default: the sampled games. --kind surgery/argmax gives the same tables for that run.
    kind = sys.argv[sys.argv.index("--kind") + 1] if "--kind" in sys.argv else "diag"
    suffix = "" if kind == "diag" else "-" + kind.split("/")[-1]
    all_games = load_games("plain" if kind == "diag" else kind)
    games = [analyse_game(g, kind) for g in all_games]
    report = {"note": "all 288 games; post-hoc detectors labelled in report"}
    outcomes = Counter(g["outcome"] for g in games)
    report["outcomes"] = dict(outcomes)
    report["win_reason"] = {k: dict(Counter(g["win_reason"] for g in games if g["outcome"] == k)) for k in outcomes}
    def split(key):
        out = {}
        for label, rows in sorted(group(games, key).items(), key=lambda kv: str(kv[0])):
            wins = sum(r["outcome"] == "win" for r in rows)
            out[str(label)] = {"wins": wins, "games": len(rows), "rate": rate(wins, len(rows))}
        return out
    report["by_optimizer"] = split(lambda g: g["optimizer"])
    report["by_deck"] = split(lambda g: g["deck"])
    report["by_seat"] = split(lambda g: g["seat"])
    report["by_opp_deck"] = split(lambda g: g["opp_deck"])
    report["by_matchup"] = split(lambda g: f"{g['deck']} vs {g['opp_deck']}")
    report["by_deck_seat"] = split(lambda g: f"{g['deck']} seat{g['seat']}")
    report["by_seed"] = split(lambda g: 40000000 + g["env"])
    report["first_damage"] = {k: dict(Counter(g["first_damage_taken_by"] for g in games if g["outcome"] == k)) for k in outcomes}
    report["summary_win"] = per_game_summary(games, "win")
    report["summary_loss"] = per_game_summary(games, "loss")
    report["decisions_by_msg"] = decision_table(games, lambda r, g: r["msg"])
    report["decisions_by_class_deck"] = decision_table(games, lambda r, g: f"{r['cls']}|{g['deck']}")
    report["decisions_by_class_seat"] = decision_table(games, lambda r, g: f"{r['cls']}|seat{g['seat']}")
    report["decisions_by_phase"] = decision_table(games, lambda r, g: r["phase"])
    report["decisions_by_turn"] = decision_table(games, lambda r, g: "T%02d-%02d" % ((min(r["turn"], 25) - 1) // 5 * 5 + 1, (min(r["turn"], 25) - 1) // 5 * 5 + 5))
    report["decisions_by_my_turn_class"] = decision_table(games, lambda r, g: f"{'own' if r['my_turn'] else 'opp'}|{r['cls']}")
    # Which disagreements carry the score gap, losses only and over all games.
    disagree = Counter(); disagree_win = Counter(); total_gap = Counter()
    for game in games:
        for row in game["decision_rows"]:
            if row["msg"] == "place" or row["agree"]:
                continue
            top = row["top_phase"] or row["top_act"]
            label = f"{row['msg']}: heuristic {top} / learner {row['phasechoice'] or row['act']}"
            (disagree if game["outcome"] == "loss" else disagree_win)[label] += 1
            total_gap[label] += row["gap"]
    n_loss = outcomes["loss"]; n_win = outcomes["win"]
    report["disagreements"] = [{"label": k, "per_loss": round(v / n_loss, 3), "per_win": round(disagree_win[k] / n_win, 3),
                                "losses_count": v, "wins_count": disagree_win[k], "total_gap": round(total_gap[k], 1)}
                               for k, v in disagree.most_common(40)]
    # Card-level activation choices.
    card_stats = defaultdict(lambda: Counter())
    for game in games:
        for row in game["decision_rows"]:
            if row["act"] == "activate" and row["card"]:
                card_stats[row["card"]]["activate_" + game["outcome"]] += 1
                card_stats[row["card"]]["unsafe"] += row["gap"] > 0
    report["activations_by_card"] = {k: dict(v) for k, v in sorted(card_stats.items(), key=lambda kv: -sum(kv[1].values()))}
    # LP lead over time.
    lead = defaultdict(Counter)
    for game in games:
        for turn, (me, opp) in game["lp_by_turn"].items():
            bucket = min(int(turn), 30)
            bucket = f"T{(bucket - 1) // 5 * 5 + 1:02d}"
            lead[bucket][("ahead" if me > opp else "behind" if me < opp else "even") + "_" + game["outcome"]] += 1
    report["lp_lead"] = {k: dict(v) for k, v in sorted(lead.items())}
    rows = [{k: v for k, v in g.items() if k != "decision_rows"} for g in games]
    (OUT / f"games{suffix}.json").write_text(json.dumps(rows))
    (OUT / f"aggregate{suffix}.json").write_text(json.dumps(report, indent=1))
    (EVIDENCE / f"aggregate{suffix}.json").write_text(json.dumps(report, indent=1))
    print("wrote", OUT / f"aggregate{suffix}.json")


if __name__ == "__main__":
    main()
