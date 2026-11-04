#!/usr/bin/env python3
"""Diagnostic production worker: the unmodified agent_match.worker with observers.

This imports tests/edison/agent_match.py and calls its own main(), so the audits,
hashes and trace records are the certified code path, not a copy. Before the call
it swaps the agent classes that worker() imports for thin subclasses:

- mode "observe": the choice comes from the parent class untouched. Probabilities
  are recomputed from the returned features (a pure function, no RNG), and heuristic
  scores are captured from the analyze() call the encoder already makes. Tested by
  byte comparison of trace-N.jsonl against a plain worker (loss_review.py verify).
- modes "argmax", "argmax:<class>", "hybrid:<class>" and "resample:<k>": policy
  surgery, used only for separately labelled runs. These intentionally change the
  games. "resample" keeps the policy and only changes the action RNG stream.

Output (next to worker-N.json, build/ only):
- diag-N.jsonl.gz: one record per decision of either seat.
  Learner records hold the learner's view. Opponent records hold ONLY events a real
  duel makes public; see public_event(). Nothing from the opponent's hand, deck, or
  face-down cards is written, and declined opportunities are not recorded.
- features-N.npz (observe mode): learner feature rows, for offline analysis.

Configuration comes from the environment because run_worker() fixes the CLI:
EDISON_DIAG_MODE (default observe).
"""
import atexit
import gzip
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/edison"))
import agent_match  # noqa: E402  (also sets up sys.path for ygoai/ygoenv)
import numpy as np  # noqa: E402
import ygoai.edison_policy as policy_module  # noqa: E402
import ygoai.edison_rl as rl_module  # noqa: E402
from ygoai.edison_policy import power, u16  # noqa: E402

MODE = os.environ.get("EDISON_DIAG_MODE", "observe")
HYBRID_CLASSES = ("idle", "battle", "chain", "select", "other")
LOCATIONS = {1: "deck", 2: "hand", 3: "mzone", 4: "szone", 5: "grave", 6: "banished", 7: "extra"}
POSITIONS = {0: "", 1: "ATK", 2: "fd-ATK", 3: "ATK", 4: "DEF", 5: "fu", 6: "set", 7: "set", 8: "DEF"}
PHASES = {0: "draw", 1: "standby", 2: "main1", 3: "battle-start", 4: "battle-step", 5: "damage",
          6: "damage-calc", 7: "battle", 8: "main2", 9: "end"}
MESSAGES = {1: "idle", 2: "chain", 3: "select_card", 4: "select_tribute", 5: "select_position",
            6: "effect_yn", 7: "yes_no", 8: "battle", 9: "select_unselect", 10: "option",
            11: "place", 12: "select_sum", 13: "disfield", 14: "announce_attrib",
            15: "announce_number", 16: "announce_card"}
ACTS = {0: "none", 1: "set", 2: "reposition", 3: "special_summon", 4: "normal_summon", 5: "monster_set",
        6: "attack", 7: "direct_attack", 8: "activate", 9: "decline"}
ACTION_PHASE = {1: "to_battle", 2: "to_main2", 3: "to_end"}
HINTS = {500: "release", 501: "discard", 502: "destroy", 503: "banish", 504: "to_grave",
         505: "to_hand", 506: "add_to_hand", 507: "to_deck", 508: "to_field", 509: "special_summon",
         510: "set", 511: "tribute?", 512: "synchro_material", 513: "material", 520: "negate?",
         528: "hint528", 531: "tribute", 533: "hint533", 549: "attack_target", 551: "effect_target",
         560: "hint560"}


def decision_class(msg):
    if msg == 1:
        return "idle"
    if msg == 8:
        return "battle"
    if msg == 2:
        return "chain"
    if msg in (3, 4, 9, 12):
        return "select"
    return "other"


class State:
    """Per-process diagnostic state shared by the agents of one worker."""
    base_seed = None
    learner = None
    names = {}
    log = None
    steps = {}
    turns = {}
    feature_rows = []
    feature_meta = []
    offsets = [0]
    run = 1
    output = None


def absolute_turn(eid, glob):
    me = 0 if int(glob[6]) == 1 else 1
    turn_player = me if int(glob[7]) else 1 - me
    shown = int(glob[4])  # saturates at 16 in the observation
    state = State.turns.get(eid)
    if state is None or shown < 16:
        turn = shown
    else:
        # A lower bound: a turn with only single-option prompts is auto-played unseen.
        turn = max(shown, state["turn"] + int(turn_player != state["tp"]))
    State.turns[eid] = {"turn": turn, "tp": turn_player}
    return turn, turn_player


def card_name(cid):
    return State.names.get(int(cid), {}).get("name", f"unknown#{int(cid)}") if cid else ""


def board_view(cards, glob):
    """The learner's view: own hand, both fields, public piles, hidden counts."""
    view = {"hand": [], "field": [], "backrow": [], "opp_field": [], "opp_backrow": [],
            "grave": [], "opp_grave": [], "banished": [], "opp_banished": [],
            "opp_hand_revealed": [], "own_deck_visible": []}
    for card in cards:
        location = int(card[2])
        if not location:
            continue
        cid = u16(card, 0)
        enemy = bool(card[4])
        name = card_name(cid) if cid else None
        if location == 1:
            if not enemy and cid:
                view["own_deck_visible"].append(name)
        elif location == 2:
            if enemy:
                if cid:
                    view["opp_hand_revealed"].append(name)
            else:
                view["hand"].append(name)
        elif location == 3:
            entry = {"name": name, "pos": POSITIONS.get(int(card[5]), "?"), "zone": int(card[3]),
                     "atk": power(card) if cid else None, "def": power(card, 14) if cid else None,
                     "disabled": int(card[11]) if cid else 0}
            view["opp_field" if enemy else "field"].append(entry)
        elif location == 4:
            entry = {"name": name, "pos": POSITIONS.get(int(card[5]), "?"), "zone": int(card[3])}
            view["opp_backrow" if enemy else "backrow"].append(entry)
        elif location == 5:
            view["opp_grave" if enemy else "grave"].append(name or "?")
        elif location == 6:
            view["opp_banished" if enemy else "banished"].append(name or "face-down")
    # Global layout: 8.. = my deck, hand, mzone, szone, grave, banished, extra; then opponent.
    view["counts"] = {"my_deck": int(glob[8]), "my_hand": int(glob[9]), "my_extra": int(glob[14]),
                      "opp_deck": int(glob[15]), "opp_hand": int(glob[16]), "opp_extra": int(glob[21])}
    return view


def describe_action(action, cards):
    row = int(action[0])
    cid = u16(action, 1)
    msg, act, phase, position = int(action[3]), int(action[4]), int(action[7]), int(action[8])
    card = cards[row - 1] if row else None
    out = {"msg": MESSAGES.get(msg, str(msg)), "act": ACTS.get(act, str(act))}
    if phase:
        out["phase"] = ACTION_PHASE.get(phase, str(phase))
    if action[5]:
        out["finish"] = True
    if cid:
        out["card"] = card_name(cid)
    if card is not None:
        out["where"] = ("opp_" if card[4] else "") + LOCATIONS.get(int(card[2]), "?")
        out["card_pos"] = POSITIONS.get(int(card[5]), "?")
        if not cid:
            out["card"] = "(hidden)"
        else:
            out["atk"], out["def"] = power(card), power(card, 14)
    if msg == 5:
        out["position"] = POSITIONS.get(position, str(position))
    if int(action[6]):
        out["effect"] = int(action[6])
    if int(action[9]):
        out["number"] = int(action[9])
    if int(action[10]):
        out["place"] = int(action[10])
    if int(action[11]):
        out["attribute"] = int(action[11])
    return out


def public_event(cards, action):
    """Only what a real duel reveals. Returns None for anything else."""
    msg, act, phase = int(action[3]), int(action[4]), int(action[7])
    cid = u16(action, 1)
    if phase:
        return {"event": ACTION_PHASE.get(phase, str(phase))}
    if msg in (1, 8, 2):
        if act in (3, 4, 6, 7, 8):
            return {"event": ACTS[act], "card": card_name(cid)}
        if act == 1:
            return {"event": "sets_spell_trap"}
        if act == 5:
            return {"event": "sets_monster"}
        if act == 2:
            return {"event": "changes_position"}
        return None  # includes declining a chain: never reveal that an offer existed
    if msg in (3, 4, 9, 12) and int(action[0]):
        card = cards[int(action[0]) - 1]
        location, position = int(card[2]), int(card[5])
        face_up = position in (1, 3, 4, 5, 8)
        if location in (5, 6) or (location in (3, 4) and face_up):
            return {"event": "selects", "card": card_name(u16(card, 0)) or "(unknown)",
                    "theirs": not bool(card[4])}
        if location in (3, 4):
            return {"event": "selects", "card": "(face-down)", "theirs": not bool(card[4])}
    return None  # hand, deck, extra deck and all other prompts stay private


def next_step(eid, glob):
    step = State.steps.get(eid, 0)
    State.steps[eid] = step + 1
    turn, _ = absolute_turn(eid, glob)
    return step, turn


class DiagEncoder(rl_module.ContextEncoder):
    """Keeps the score array computed by the encode() call that already happens."""
    last_scores = None

    def analyze(self, cards, glob, actions):
        scores, features = super().analyze(cards, glob, actions)
        self.last_scores = scores
        return scores, features


class DiagRLAgent(rl_module.RLAgent):
    def __init__(self, metadata, seed, model):
        super().__init__(metadata, seed, model)
        self.encoder = DiagEncoder(metadata, seed)
        offset = seed - State.base_seed
        self.eid, self.player = offset // 2, offset % 2
        State.learner = self.player
        if MODE.startswith("resample:"):
            # Same policy, another action stream: measures sampling luck, changes no parameter.
            self.rng = np.random.default_rng((seed ^ 0x50504F) + 1000003 * int(MODE.split(":", 1)[1]))

    def _surgical_choice(self, cards, glob, actions, decision, selection):
        x = self.model.mask_features(self.encoder.encode(cards, glob, actions, decision, selection))
        logits, _ = self.model.forward(x)
        probs, logp, _ = rl_module.distribution(logits, np.array([0, len(x)]))
        sampled = int(self.rng.choice(len(x), p=probs))  # keep RNG consumption aligned
        choice = sampled
        if MODE == "argmax":
            choice = int(np.argmax(probs))
        elif MODE.startswith("argmax:"):
            if decision_class(int(actions[0, 3])) == MODE.split(":", 1)[1]:
                choice = int(np.argmax(probs))
        elif MODE.startswith("hybrid:"):
            if decision_class(int(actions[0, 3])) == MODE.split(":", 1)[1]:
                choice = int(np.argmax(self.encoder.last_scores))
        value = float(self.model.value(x.mean(axis=0)))
        self.encoder.observe(cards, actions[choice])
        return choice, x, float(logp[choice]), value

    def choose(self, cards, glob, actions, decision, selection):
        if MODE == "observe" or MODE.startswith("resample:"):
            choice, x, logp, value = super().choose(cards, glob, actions, decision, selection)
        else:
            choice, x, logp, value = self._surgical_choice(cards, glob, actions, decision, selection)
        logits, _ = self.model.forward(x)
        probs, recomputed, _ = rl_module.distribution(logits, np.array([0, len(x)]))
        assert abs(float(recomputed[choice]) - logp) < 1e-9, "Diagnostic probabilities disagree"
        scores = self.encoder.last_scores
        step, turn = next_step(self.eid, glob)
        source = int(decision[3])
        record = {
            "env": self.eid, "step": step, "seat": self.player, "role": "learner",
            "turn": turn, "my_turn": bool(glob[7]), "phase": PHASES.get(int(glob[5]), str(int(glob[5]))),
            "lp": [u16(glob, 0), u16(glob, 2)], "board": board_view(cards, glob),
            "class": decision_class(int(actions[0, 3])),
            "msg": MESSAGES.get(int(actions[0, 3]), str(int(actions[0, 3]))),
            "context": {"hint": int(decision[1]), "hint_name": HINTS.get(int(decision[1]), int(decision[1])),
                        "stage": int(decision[2]),
                        "source": card_name(u16(cards[source - 1], 0)) if source else None,
                        "bounds": [int(decision[4]), int(decision[5])], "selected": int(decision[6]),
                        "mandatory": int(decision[7]), "finish": int(decision[8]),
                        "cancel": int(decision[9]), "sum": int(decision[10]), "active": int(decision[11])},
            "options": [describe_action(a, cards) for a in actions],
            "probs": [round(float(p), 4) for p in probs],
            "scores": [round(float(s), 3) for s in scores],
            "choice": int(choice), "value": round(value, 4),
        }
        State.log.write(json.dumps(record, separators=(",", ":")) + "\n")
        if MODE == "observe":
            State.feature_rows.append(np.asarray(x, dtype=np.float32))
            State.offsets.append(State.offsets[-1] + len(x))
            State.feature_meta.append((self.eid, step, int(choice), int(np.argmax(scores))))
        return choice, x, logp, value


class DiagHeuristic(policy_module.Heuristic):
    """Opponent seat. Choice is untouched; only public events reach the log."""

    def __init__(self, metadata, seed=0, weights=None):
        super().__init__(metadata, seed, weights)
        offset = seed - State.base_seed
        self.eid, self.player = offset // 2, offset % 2

    def choose(self, cards, glob, actions):
        result = super().choose(cards, glob, actions)
        step, turn = next_step(self.eid, glob)
        event = public_event(cards, actions[result[0]])
        record = {"env": self.eid, "step": step, "seat": self.player, "role": "opponent",
                  "turn": turn, "phase": PHASES.get(int(glob[5]), str(int(glob[5])))}
        if event:
            record["public"] = event
        State.log.write(json.dumps(record, separators=(",", ":")) + "\n")
        return result


def main():
    argv = sys.argv
    output = Path(argv[argv.index("--output") + 1]).resolve()
    run = int(argv[argv.index("--worker") + 1])
    State.base_seed = int(argv[argv.index("--seed") + 1])
    envs = int(argv[argv.index("--num-envs") + 1])
    assert State.base_seed >= 40000000 and State.base_seed + envs <= 40000008, "Validation seeds only"
    State.run, State.output = run, output
    if MODE not in ("observe", "argmax"):
        family, _, detail = MODE.partition(":")
        assert (family in ("hybrid", "argmax") and detail in HYBRID_CLASSES) or (
            family == "resample" and detail.isdigit()), MODE
    output.mkdir(parents=True, exist_ok=True)
    profile = agent_match.PROFILE
    import sqlite3
    db = sqlite3.connect(ROOT / "assets/locale/en/cards.cdb")
    for cid, line in enumerate(profile.read_text().splitlines(), 1):
        code = int(line.split()[0])
        name = db.execute("select name from texts where id=?", (code,)).fetchone()[0]
        State.names[cid] = {"name": name, "code": code}
    db.close()
    State.log = gzip.open(output / f"diag-{run}.jsonl.gz", "wt", compresslevel=3)
    rl_module.RLAgent = DiagRLAgent
    policy_module.Heuristic = DiagHeuristic
    try:
        return agent_match.main()
    finally:
        State.log.close()
        if MODE == "observe" and State.feature_rows:
            np.savez_compressed(output / f"features-{run}.npz",
                                x=np.concatenate(State.feature_rows),
                                offsets=np.asarray(State.offsets),
                                meta=np.asarray(State.feature_meta, dtype=np.int32))


if __name__ == "__main__":
    raise SystemExit(main())
