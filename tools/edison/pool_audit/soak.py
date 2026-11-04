#!/usr/bin/env python3
"""Random-play soak across the legal Edison pool, using the production deck validator.

Stages use the 71000000 seed block. Each batch is a separate process; a crash loses only
that process. The crashing duel is recorded and the remaining duels are re-queued.
"""
import argparse, json, random, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import gen_decks

SEED_BASE = 71000000
STATE_DIR = OUT / "soak"

class State:
    def __init__(self):
        self.path = STATE_DIR / "state.json"
        self.chain = Counter(); self.summon = Counter(); self.spsummon = Counter(); self.flip = Counter()
        self.chosen = Counter(); self.offered = Counter()
        self.in_deck = Counter(); self.lua = Counter(); self.crash_cards = Counter()
        self.statuses = Counter(); self.next_seed = SEED_BASE
        self.max_options = 0; self.total_steps = 0
        self.violations = Counter()
        self.max_repeat_top = []
        self.failures = []
        self.diag = Counter()
        self.lua_samples = {}
        self.stage_stats = {}
        self.confirm = {}
        self.gauntlet = {}

    def to_json(self):
        return {"chain": self.chain, "summon": self.summon, "spsummon": self.spsummon, "flip": self.flip,
                "chosen": {f"{k[0]}:{k[1]}": v for k, v in self.chosen.items()}, "offered": self.offered,
                "in_deck": self.in_deck, "lua": self.lua, "crash_cards": self.crash_cards,
                "statuses": self.statuses, "next_seed": self.next_seed,
                "max_options": self.max_options, "total_steps": self.total_steps,
                "violations": self.violations, "max_repeat_top": self.max_repeat_top[:40],
                "failures": self.failures, "diag": self.diag, "lua_samples": self.lua_samples,
                "stage_stats": self.stage_stats, "confirm": self.confirm, "gauntlet": self.gauntlet}

    def save(self):
        self.path.write_text(json.dumps(self.to_json()))

    def load(self):
        data = json.loads(self.path.read_text())
        for key in ("chain", "summon", "spsummon", "flip", "offered", "in_deck", "lua", "crash_cards", "statuses", "violations", "diag"):
            setattr(self, key, Counter({(int(k) if k.lstrip("-").isdigit() else k): v for k, v in data[key].items()}))
        self.chosen = Counter({(int(k.split(":")[0]), k.split(":")[1]): v for k, v in data["chosen"].items()})
        for key in ("next_seed", "max_options", "total_steps", "max_repeat_top", "failures", "lua_samples", "stage_stats", "confirm", "gauntlet"):
            setattr(self, key, data.get(key, {} if key in ("confirm", "gauntlet") else None))

def parse_audit(path):
    """Return {duel_id: {tag: Counter}} and per-duel Lua diagnostics."""
    per, current = defaultdict(lambda: defaultdict(Counter)), None
    last_chain = {}
    if not path.exists(): return per, last_chain
    with path.open(errors="replace") as handle:
        for line in handle:
            tag, _, rest = line.rstrip("\n").partition(" ")
            if tag == "D": current = rest
            elif current is None: continue
            elif tag in ("C", "S", "P", "F"):
                per[current][tag][int(rest)] += 1
                if tag == "C": last_chain[current] = int(rest)
            elif tag == "L":
                per[current]["L"][rest] += 1
    return per, last_chain

def crash_signature(err, out, kind):
    """Stable text for a native failure: the what() line, else the end of the output."""
    lines = [l for l in err.splitlines() if "what():" in l or "Traceback" in l or "Error" in l or "Segmentation" in l]
    tail = [l for l in (err + "\n" + out).splitlines() if l.strip()]
    text = lines[-1].strip() if lines else (tail[-1].strip() if tail else kind)
    return f"{kind}: {text[:240]}"

def run_batch(batch_id, stage_dir, duels, decks, codes, module, timeout):
    bdir = stage_dir / f"b{batch_id:05d}"
    bdir.mkdir(parents=True, exist_ok=True)
    deck_paths = {}
    for name, deck in decks.items():
        path = bdir / f"{name}.ydk"
        gen_decks.write_ydk(deck, path)
        deck_paths[name] = str(path)
    results, begun = [], []
    remaining = list(duels)
    crash_events = []
    attempt = 0
    while remaining:
        attempt += 1
        spec = bdir / f"spec{attempt}.json"
        spec.write_text(json.dumps({"decks": {d: deck_paths[d] for x in remaining for d in (x["deck0"], x["deck1"])}, "duels": remaining}))
        log, audit = bdir / f"log{attempt}.jsonl", bdir / f"audit{attempt}.log"
        cmd = [sys.executable, str(Path(__file__).resolve().parent / "soak_worker.py"), "--spec", str(spec), "--log", str(log),
               "--audit-log", str(audit), "--codes", str(codes), "--module", module]
        started = time.time()
        rc, out, err, timed_out = None, "", "", False
        try:
            proc = subprocess.run(cmd, cwd=ROOT / "scripts", capture_output=True, text=True, timeout=timeout, errors="replace")
            rc, out, err = proc.returncode, proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            out = (exc.stdout or b"").decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            err = (exc.stderr or b"").decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        ended, last_begin, last_step = {}, None, None
        if log.exists():
            for line in log.read_text(errors="replace").splitlines():
                try: rec = json.loads(line)
                except ValueError: continue
                if "begin" in rec: last_begin, last_step = rec["begin"], None
                elif "last" in rec: last_step = rec["last"]
                elif "end" in rec: ended[rec["end"]["id"]] = rec["end"]
        per, last_chain = parse_audit(audit)
        results.extend((rec, {k: dict(v) for k, v in per.get(rec["id"], {}).items()}) for rec in ended.values())
        unfinished = [d for d in remaining if d["id"] not in ended]
        crash_id = last_begin if last_begin not in ended else None
        output_lines = Counter(l.strip() for l in (out + "\n" + err).splitlines() if l.strip())
        if rc == 0 and not timed_out and not unfinished:
            shutil.rmtree(bdir / "x", ignore_errors=True)
            return results, crash_events, output_lines
        kind = "timeout" if timed_out else f"exit{rc}"
        duel = next((d for d in remaining if d["id"] == crash_id), None)
        crash_events.append({
            "kind": kind, "duel": crash_id, "seed": duel["seed"] if duel else None,
            "policy": duel["policy"] if duel else None,
            "last_chain": last_chain.get(crash_id), "last_step": last_step,
            "signature": crash_signature(err, out, kind),
            "lua": list(per.get(crash_id, {}).get("L", {}))[:3]})
        if crash_id is None:
            # Startup failure: nothing ran, so retrying cannot help. Report every unfinished duel.
            for dead in unfinished:
                crash_events.append({"kind": "startup", "duel": dead["id"], "signature": crash_signature(err, out, "startup")})
            break
        remaining = [d for d in unfinished if d["id"] != crash_id]
    return results, crash_events, Counter()

def attribute_crash(event, pool):
    """Best-effort: the card in the last logged action, the last chain link, and any card code in text."""
    cards = set()
    if event.get("last_chain"): cards.add(event["last_chain"])
    step = event.get("last_step") or {}
    if step.get("code"): cards.add(step["code"])
    return sorted(c for c in cards if c > 0)

def plan_stage(stage, pool, state, count, rng, targets_per_deck=4):
    duels, decks = [], {}
    weight_map = {}
    static_path = OUT / "static_features.json"
    static = json.loads(static_path.read_text()) if static_path.exists() else {}
    def score(entry):
        if static.get(str(entry["base"]), {}).get("activated_effect"):
            return sum(state.chain[c] for c in entry["ids"])
        return sum(state.chain[c] + state.summon[c] + state.spsummon[c] + state.flip[c] for c in entry["ids"])
    for entry in pool.playable: weight_map[entry["base"]] = 1.0 / (1 + score(entry)) ** 1.5 + 1e-4
    weight = lambda e: weight_map[e["base"]]
    unexercised = sorted([e for e in pool.playable if score(e) == 0], key=lambda e: e["base"])
    pool.blocked = set()
    if stage.get("quarantine_file"):
        pool.blocked = set(json.loads(Path(stage["quarantine_file"]).read_text()))
    unexercised = [e for e in unexercised if e["base"] not in pool.blocked]
    weighted_source = [e for e in pool.playable if e["base"] not in pool.blocked]
    if stage["kind"] == "confirm":
        return plan_confirm(stage, pool, state)
    if stage["kind"] == "gauntlet":
        pool.blocked = set()
        return plan_gauntlet(stage, pool, state)
    for i in range(count):
        seed = state.next_seed; state.next_seed += 1
        drng = random.Random(seed)
        pair = []
        for side in (0, 1):
            kind = stage["kind"]
            for attempt in range(25):
                try:
                    if kind == "uniform":
                        deck = gen_decks.uniform_deck(pool, drng)
                    elif kind == "themed":
                        deck, _ = gen_decks.themed_deck(pool, drng, weight)
                    elif kind == "targeted":
                        source = unexercised or weighted_source
                        picks = drng.sample(source, min(targets_per_deck, len(source)))
                        deck = gen_decks.targeted_deck(pool, drng, picks, weight)
                    else:
                        raise ValueError(kind)
                    break
                except ValueError:
                    continue
            else:
                raise RuntimeError("deck generation failed")
            name = f"s{seed}_{side}"
            decks[name] = deck; pair.append(name)
        duels.append({"id": str(seed), "seed": seed, "deck0": pair[0], "deck1": pair[1], "policy": stage.get("policy", "uniform")})
    return duels, decks

def plan_confirm(stage, pool, state):
    """One target at a time; every other quarantined card is kept out so the cause is clear."""
    duels, decks = [], {}
    targets = json.loads(Path(stage["targets_file"]).read_text())
    for base in targets:
        for _ in range(stage.get("per_target", 3)):
            seed = state.next_seed; state.next_seed += 1
            drng = random.Random(seed)
            target = pool.by_id[base]
            weight = lambda e: 1.0
            deck0 = gen_decks.targeted_deck(pool, drng, [target], weight)
            deck1 = gen_decks.targeted_deck(pool, drng, [target], weight)
            names = (f"s{seed}_0", f"s{seed}_1")
            decks[names[0]], decks[names[1]] = deck0, deck1
            duels.append({"id": str(seed), "seed": seed, "deck0": names[0], "deck1": names[1],
                          "policy": "eager", "target": base})
    return duels, decks

def load_gauntlet(pool):
    """Candidate lists, with XX-Saber Gottoms removed only in memory where the validator rejects it."""
    import build_gauntlet as bg
    manifest = json.loads((bg.DEST / "manifest.json").read_text())
    decks = {}
    for row in manifest["decks"]:
        deck = bg.parse_ydk((bg.DEST / f"{row['name']}.ydk").read_text())
        if any(c not in pool.by_id for zone in deck.values() for c in zone):
            deck = {z: [c for c in codes if c in pool.by_id] for z, codes in deck.items()}
        decks[row["name"]] = deck
    return decks

def side_in(deck, pool, rng):
    """Move the whole Side into Main and Extra, keeping zone sizes; removed cards become the Side."""
    main, extra, side = list(deck["main"]), list(deck["extra"]), []
    for code in deck["side"]:
        zone = extra if pool.by_id[code]["rows"][0]["type"] & (TYPE_FUSION | TYPE_SYNCHRO) else main
        side.append(zone.pop(rng.randrange(len(zone))))
        zone.append(code)
    return {"main": main, "extra": extra, "side": side}

def plan_gauntlet(stage, pool, state):
    duels, decks = [], {}
    base = load_gauntlet(pool)
    names = sorted(base)
    per_pair, variant = stage.get("per_pair", 4), stage.get("variant", "main")
    for a in names:
        for b in names:
            for repeat in range(per_pair):
                seed = state.next_seed; state.next_seed += 1
                drng = random.Random(seed)
                d0, d1 = base[a], base[b]
                if variant == "sidein":
                    d0, d1 = side_in(d0, pool, drng), side_in(d1, pool, drng)
                n0, n1 = f"g{seed}_0", f"g{seed}_1"
                decks[n0], decks[n1] = d0, d1
                duels.append({"id": str(seed), "seed": seed, "deck0": n0, "deck1": n1,
                              "policy": "eager" if repeat % 2 else "uniform", "gauntlet": [a, b]})
    return duels, decks

def run_stage(stage, pool, state, args):
    name = stage["name"]
    stage_dir = STATE_DIR / name
    stage_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(stage.get("seed", 0) ^ 0x5EED)
    start_seed = state.next_seed
    duels, decks = plan_stage(stage, pool, state, stage.get("count", 0), rng)
    target_of = {d["id"]: d.get("target") for d in duels}
    gauntlet_of = {d["id"]: d.get("gauntlet") for d in duels}
    def gauntlet_row(deck):
        return state.gauntlet.setdefault(stage.get("variant", "main"), {}).setdefault(deck, {"ok": 0, "crash": {}, "other": {}, "steps": 0})
    (stage_dir / "seeds.json").write_text(json.dumps({"first": start_seed, "last": state.next_seed - 1, "stage": stage}))
    deck_file = stage_dir / "decks.jsonl"
    with deck_file.open("w") as handle:
        for dname, deck in decks.items():
            handle.write(json.dumps({"name": dname, **deck}) + "\n")
    for deck in decks.values():
        for zone in deck.values():
            for code in set(zone): state.in_deck[code] += 1
    size = args.batch
    batches = [duels[i:i + size] for i in range(0, len(duels), size)]
    t0 = time.time()
    stats = Counter()
    with ThreadPoolExecutor(args.workers) as ex:
        futures = {}
        for i, batch in enumerate(batches):
            names = {d for x in batch for d in (x["deck0"], x["deck1"])}
            futures[ex.submit(run_batch, i, stage_dir, batch, {n: decks[n] for n in names}, args.codes, stage.get("module", "audit"), max(600, 60 * len(batch)))] = i
        for fut in as_completed(futures):
            results, crashes, lines = fut.result()
            for key, n in lines.items(): state.diag[key[:300]] += n
            for rec, audit in results:
                stats[rec["status"]] += 1; state.statuses[rec["status"]] += 1
                state.total_steps += rec["steps"]; state.max_options = max(state.max_options, rec["max_options"])
                for code, act, n in rec["chosen"]: state.chosen[(code, act)] += n
                for code, n in rec["offered"]: state.offered[code] += n
                for key, n in rec["violations"].items(): state.violations[key] += n
                if rec["max_repeat"] >= 100:
                    state.max_repeat_top.append([rec["max_repeat"], rec["id"], rec["steps"], rec["status"]])
                    state.max_repeat_top.sort(reverse=True); del state.max_repeat_top[40:]
                for code, n in audit.get("C", {}).items(): state.chain[int(code)] += n
                for code, n in audit.get("S", {}).items(): state.summon[int(code)] += n
                for code, n in audit.get("P", {}).items(): state.spsummon[int(code)] += n
                for code, n in audit.get("F", {}).items(): state.flip[int(code)] += n
                for text, n in audit.get("L", {}).items():
                    state.lua[text[:300]] += n
                    state.lua_samples.setdefault(text[:300], rec["id"])
                for deck in gauntlet_of.get(rec["id"]) or []:
                    row = gauntlet_row(deck)
                    row["steps"] += rec["steps"]
                    if rec["status"] == "ok": row["ok"] += 1
                    else: row["other"][rec["status"]] = row["other"].get(rec["status"], 0) + 1
                if target_of.get(rec["id"]):
                    row = state.confirm.setdefault(str(target_of[rec["id"]]), {"ok": 0, "crash": {}, "chains": 0})
                    row["ok"] += rec["status"] == "ok"
                    row["chains"] += audit.get("C", {}).get(target_of[rec["id"]], 0)
                if rec["status"] != "ok":
                    state.failures.append({"stage": name, "kind": rec["status"], "duel": rec["id"], "steps": rec["steps"], "max_options": rec["max_options"]})
            for event in crashes:
                stats["crash"] += 1; state.statuses["crash"] += 1
                event["stage"] = name
                for deck in gauntlet_of.get(event.get("duel")) or []:
                    row = gauntlet_row(deck)
                    row["crash"][event["signature"]] = row["crash"].get(event["signature"], 0) + 1
                if target_of.get(event.get("duel")):
                    row = state.confirm.setdefault(str(target_of[event["duel"]]), {"ok": 0, "crash": {}, "chains": 0})
                    row["crash"][event["signature"]] = row["crash"].get(event["signature"], 0) + 1
                    event["target"] = target_of[event["duel"]]
                cards = attribute_crash(event, pool)
                event["attributed"] = cards
                for code in cards: state.crash_cards[code] += 1
                state.failures.append(event)
    state.stage_stats[name] = {"duels": len(duels), "elapsed": time.time() - t0, **stats}
    state.save()
    # Deck and worker files are reproducible from the seed; keep only logs that show failures.
    for bdir in stage_dir.glob("b*"):
        shutil.rmtree(bdir, ignore_errors=True)
    print(name, state.stage_stats[name], "unexercised", sum(
        1 for e in pool.playable if not any(state.chain[c] + state.summon[c] + state.spsummon[c] + state.flip[c] for c in e["ids"])), flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True, help="JSON list of stage objects")
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--batch", type=int, default=40)
    parser.add_argument("--codes", type=Path, default=FULL_CODES)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    state = State()
    if args.resume: state.load()
    pool = gen_decks.Pool()
    for stage in json.loads(args.plan.read_text()):
        if stage["name"] in state.stage_stats: continue
        run_stage(stage, pool, state, args)

if __name__ == "__main__":
    main()
