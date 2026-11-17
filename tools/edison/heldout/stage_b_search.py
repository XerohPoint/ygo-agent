#!/usr/bin/env python3
"""M3 Stage B's search check: reported only, after the verdict. docs/edison-m3b-protocol.md, "Search check",
and docs/edison-m3b-search.md for what is fixed here.

M2's search at its frozen setting pilots drawn held-out builds against the same Stage B network without search,
and each search game is compared with the plain game on the same seed, decks and network.

  export    write the three sealed networks as native files
  parity    development: the network through the search binary equals it through the certified module, on
            held-out builds, and the comparison fails when the Python side skips the identity map
  values    development: the native forward pass equals JAX on recorded v2 decisions, for each sealed network,
            logits and values, and a planted fault in the native identity lookup is caught
  smoke     development: a few search games at the frozen setting, twice, on two worker counts
  selftest  the job design
  freeze    write m3b-search-manifest.json on a clean commit
  run       the check, on the commit that holds only that manifest

Development seeds are 89700000-89799999. The check's are 90300000 onward.
"""
import os

os.environ["EDISON_CODE_LIST"] = "v2"  # before decks is imported, here or by a module below

import argparse  # noqa: E402
from collections import Counter  # noqa: E402
import hashlib  # noqa: E402
import json  # noqa: E402
from pathlib import Path  # noqa: E402
import random  # noqa: E402
import subprocess  # noqa: E402
import sys  # noqa: E402
import zlib  # noqa: E402

import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path[:0] = [str(HERE), str(ROOT / "tools/edison/search"), str(ROOT / "tools/edison/learner")]
import stage_a as A  # noqa: E402  (the group packing and the paired summary)
import stage_b as B  # noqa: E402  (paths, hashes and the list file)
from stage_a import git, sha, text_sha  # noqa: E402
import decks as deck_registry  # noqa: E402
import search_common as sc  # noqa: E402

SEARCH_SEED, SEARCH_DRAW, SEARCH_LISTS, SEARCH_OPPONENTS = 90300000, 90310001, 60, 4
SEARCH_SETTING = (64, 48, 4, 1.0)  # rollouts, depth, candidates, margin: M2's frozen setting
RESAMPLES = 10000
DEV_SEED, DEV_END = 89700000, 89799999
SEATS = (0, 1)
OPTIMIZERS = B.OPTIMIZERS

BIN = ROOT / "build/forward_model/edison_search"
CORE_PIN = ROOT / "build/forward_model/core-pin.txt"
NETS = B.BUILD / "search-nets"
EXPORT = NETS / "export.json"
DEV = B.BUILD / "search-dev"
MANIFEST = HERE / "m3b-search-manifest.json"
STAGE_B_RESULT = ROOT / "tests/edison/reports/m3-stage-b-2026-11-12/stage-b-result.json"
DEPENDS = ("tools/edison/heldout/stage_b_search.py", "tools/edison/heldout/stage_b.py", "tools/edison/heldout/stage_a.py",
           "tools/edison/heldout/stage-b-lists.json", "tools/edison/heldout/m3b-manifest.json", "tools/edison/heldout/m3b-checkpoints.json",
           "docs/edison-m3b-protocol.md", "docs/edison-m3b-search.md",
           "tools/edison/search/search_common.py", "tools/edison/search/search_study.py", "tools/edison/search/export_net.py",
           "tools/edison/search/edison_search.cpp", "tools/edison/search/pimc.h", "tools/edison/search/edison_net.h",
           "tools/edison/forward_model/fm_world.h", "tools/edison/forward_model/fm_world.cpp", "tools/edison/forward_model/fm_env.h",
           "tools/edison/forward_model/fm_deal.h", "tools/edison/forward_model/fm_hidden.h", "tools/edison/forward_model/arena.h",
           "tools/edison/forward_model/build.sh", "ygoenv/ygoenv/ygopro/ygopro.h",
           "tools/edison/learner/actor.py", "tools/edison/learner/decks.py", "tools/edison/learner/obs.py", "tools/edison/learner/net.py",
           "tools/edison/learner/codes_v2.txt", "tools/edison/learner/identity_v2.json")
STRIP = ("search_cpu", "total_cpu")  # a game's fields that are timings, not results
DEV_CHECKS = ("parity", "values", "smoke")
FALLBACK_LIMIT = 0.25  # a smoke run where the search gives up this often is not exercising the search
DECISIONS = ROOT / "build/edison-stage-b/identity-4/v2/decisions.bin"  # the identity differential's recorded v2 observations
NET_BIN = ROOT / "build/edison-native-net/bin"
# What a search game must share with the Stage B test, by its key in m3b-manifest.json's inputs.
SAME_AS_STAGE_B = ("tools/edison/learner/codes_v2.txt", "tools/edison/learner/identity_v2.json", "tools/edison/learner/actor.py",
                   "tools/edison/learner/obs.py", "tools/edison/learner/net.py", "tools/edison/learner/decks.py",
                   "tools/edison/heldout/stage-b-lists.json", "tools/edison/heldout/stage_b.py", "tools/edison/heldout/stage_a.py")


def out_dir():
    """Results sit under the manifest they were played on."""
    return B.BUILD / f"search-check-{sha(MANIFEST)[:16]}"


def all_decks():
    return {**deck_registry.registry(), **json.loads(B.EXTRA.read_text())}


def lists():
    data = json.loads(B.LISTS.read_text())
    return data["lists"], sorted(r["name"] for r in data["lists"]), sorted(data["training_lists"])


# ---------------------------------------------------------------- networks

def export_nets():
    """The sealed checkpoints as native files. The export is a function of the checkpoint and the identity map, so a second run writes the same bytes."""
    from export_net import export
    seal = json.loads(B.SEAL.read_text())
    NETS.mkdir(parents=True, exist_ok=True)
    out = {}
    for k in OPTIMIZERS:
        stem = B.RUNS / f"main-{k}/checkpoint-{B.TRAIN['rounds']:04d}"
        info = export(str(stem), NETS / f"gauntlet-{k}.new", seal["checkpoints"][k]["params_sha256"])
        if info["codes_sha256"] != B.CODES_SHA or info["identity_sha256"] != B.IDENTITY_SHA:
            raise SystemExit(f"{stem} is not on code list v2 and its identity map")
        out[k] = info
    if len({v["id_row_sha256"] for v in out.values()}) != 1:
        raise SystemExit("the networks carry different identity maps")
    if EXPORT.exists() and json.loads(EXPORT.read_text()) != out:
        raise SystemExit("a second export differs from the first, which is left in place")
    for k in OPTIMIZERS:
        (NETS / f"gauntlet-{k}.new").replace(NETS / f"gauntlet-{k}.ednet")
    EXPORT.write_text(json.dumps(out, indent=1) + "\n")
    print(json.dumps({k: {f: v[f] for f in ("params_sha256", "file_sha256", "id_row_sha256")} for k, v in out.items()}, indent=1))


def configure():
    """Point search_common at the exported Stage B networks, and check they are the sealed ones."""
    if not EXPORT.exists():
        raise SystemExit("run export first")
    info = json.loads(EXPORT.read_text())
    seal = json.loads(B.SEAL.read_text())
    for k in OPTIMIZERS:
        if info[k]["params_sha256"] != seal["checkpoints"][k]["params_sha256"] or sha(NETS / f"gauntlet-{k}.ednet") != info[k]["file_sha256"]:
            raise SystemExit(f"the exported network {k} is not the sealed one")
    sc.use_code_list_v2({f"n{k}": info[k]["params_sha256"] for k in OPTIMIZERS}, NETS, info[OPTIMIZERS[0]]["id_row_sha256"])
    if sc.BIN != BIN or sc.CODES_SHA != B.CODES_SHA:
        raise SystemExit("search_common points at another binary or code list")
    return info


def binary_inputs():
    pin = CORE_PIN.read_text().strip() if CORE_PIN.exists() else None
    return {"sha256": sha(BIN), "id": subprocess.run([str(BIN), "id"], check=True, capture_output=True, text=True).stdout.strip(), "core_pin": pin}


def dev_key():
    """What a development check ran on. freeze accepts a check only if this is still what it would freeze."""
    return {"binary": binary_inputs(), "nets": {k: v["file_sha256"] for k, v in json.loads(EXPORT.read_text()).items()},
            "files": {p: sha(ROOT / p) for p in DEPENDS if not p.startswith("docs/")}}


# ---------------------------------------------------------------- design

def search_jobs(held, training):
    """60 held-out builds, each against 4 training lists, drawn once. The search side plays the held-out build in both seats, the net rotating by pair.
    A job is search_common.job_line's arguments; the plain game has no search seat."""
    rng = random.Random(SEARCH_DRAW)
    picked = rng.sample(sorted(held), SEARCH_LISTS)
    pairs = [(d, o) for d in picked for o in rng.sample(sorted(training), SEARCH_OPPONENTS)]
    nets = tuple(f"n{k}" for k in OPTIMIZERS)
    budget, depth, candidates, z = SEARCH_SETTING
    search, plain = [], []
    for p, (d, o) in enumerate(pairs):
        for seat in SEATS:
            d0, d1 = (d, o) if seat == 0 else (o, d)
            search.append([f"g{p}_{seat}", d0, d1, SEARCH_SEED + p, seat, nets[p % 3], budget, depth, candidates, z, 1, 5000 + 2 * p + seat, 0])
            plain.append([f"p{p}_{seat}", d0, d1, SEARCH_SEED + p, -1, nets[p % 3]])
    return pairs, search, plain


def play_groups(groups, jobs, group_of, out, workers, fresh=False):
    """The binary loads exactly 21 lists, so each run gets its own registry. group_of gives a job's held-out build."""
    decks = all_decks()
    games = {}
    for n, names in enumerate(groups):
        if len(set(names)) != 21:
            raise SystemExit("a group does not hold 21 lists")
        sc.REGISTRY = Path(out) / f"registry-{n:02d}.txt"
        sc.REGISTRY.parent.mkdir(parents=True, exist_ok=True)
        sc.REGISTRY.write_text("".join(f"{k} {Path(decks[k]).resolve()}\n" for k in sorted(names)))
        mine = [j for j in jobs if group_of(j) in names]
        if mine:
            games.update(sc.play([sc.job_line(*j) for j in mine], Path(out) / f"group-{n:02d}", workers, fresh=fresh))
    return games


def summary(games, pairs, heavy):
    import search_study as study
    comparisons, missing, void, reasons = [], Counter(), Counter(), Counter()
    for p in range(len(pairs)):
        for seat in SEATS:
            g, b = games.get(f"g{p}_{seat}"), games.get(f"p{p}_{seat}")
            for arm, game in (("search", g), ("plain", b)):
                if game is None:
                    missing[arm] += 1
                elif game.get("status") != "OK":
                    void[arm] += 1
                    reasons[f"{arm}: {str(game.get('reason') or game.get('status'))[:80]}"] += 1
            a = study.score(g, seat) if g else None
            q = study.score(b, seat) if b else None
            if a is not None and q is not None:
                comparisons.append((p, seat, a, q))
    clusters = [d for d, _ in pairs]
    rng = np.random.default_rng(SEARCH_DRAW)
    return {"pairs": len(pairs), "comparisons_planned": 2 * len(pairs), "missing": dict(missing), "void": dict(void), "void_reasons": dict(reasons),
            "all": A.paired_summary(comparisons, clusters, rng, RESAMPLES),
            "by_seat": {s: A.paired_summary([c for c in comparisons if c[1] == s], clusters, rng, RESAMPLES) for s in SEATS},
            "heavy": A.paired_summary([c for c in comparisons if clusters[c[0]] in heavy], clusters, rng, RESAMPLES),
            "not_heavy": A.paired_summary([c for c in comparisons if clusters[c[0]] not in heavy], clusters, rng, RESAMPLES),
            "search_games": study.summarise({k: v for k, v in games.items() if k.startswith("g")}),
            "note": "Reported only. The interval resamples held-out builds. The search is told the opposing list and deals hidden cards uniformly, as in M2."}


# ---------------------------------------------------------------- development checks

def dev_lists(rows, training, count=12):
    """Held-out builds for the development checks: the ones with the most unknown Main Deck copies, then ones that
    play Mind Crush, whose prompt needs the wider action tables. The other 21 - count are training lists."""
    decks = all_decks()
    heavy = sorted(rows, key=lambda r: (-r["unknown_main_copies"], r["name"]))[:count - 4]
    crush = [r for r in sorted(rows, key=lambda r: r["name"]) if "\n15800838" in Path(decks[r["name"]]).read_text() and r not in heavy][:4]
    held = [r["name"] for r in heavy + crush]
    return held, training[:21 - len(held)]


def dev_seed(n):
    if not DEV_SEED <= DEV_SEED + n <= DEV_END:
        raise SystemExit("out of development seeds")
    return DEV_SEED + n


class SkipsTheMap:
    """The planted defect: a network that reads every id's own row, as a caller that forgot the identity map would."""

    def __init__(self, net):
        self.net, self.own = net, np.arange(len(net.row_of), dtype=net.row_of.dtype)

    def infer(self, packed):
        return self.net.infer(packed, np.broadcast_to(self.own, (packed["cards"].shape[0], len(self.own))))


def production_games(jobs, nets, wide):
    """Each plain job through the certified module with the JAX network on both seats: [crc of every observation, winner, decisions]."""
    import collect_decisions as cd
    import ygoenv
    from actor import MAX_OPTIONS, MAX_STEPS
    metadata = deck_registry.card_metadata()

    def make_env(cell, count):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=count, num_threads=1, seed=cell["seed"], deck1=cell["deck0"], deck2=cell["deck1"], play_mode="self",
                           player=-1, async_reset=False, greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS,
                           timeout=10, verbose=False)
    out = {}
    for job in jobs:
        job_id, deck0, deck1, seed, _, net = job[:6]
        k = int(net[1:])
        cell = {"index": 0, "deck0": deck0, "deck1": deck1, "seed": seed, "deck0_id": 0, "deck1_id": 0, "policies": (f"net-greedy:{k}", f"net-greedy:{k}")}
        buffers, finished = cd.play_cell(cell, 1, nets, metadata, len(metadata), make_env)
        crc = 0
        for rec in buffers[0]:
            r = rec[0]
            used = r["actions"][:int(r["counts"])]
            wide[job_id] += bool(len(used) and (used[:, 3].max() > 16 or used[:, 9].max() > 13 or used[:, 10].max() > 31))
            for field, size in (("cards", None), ("selection", None), ("actions", int(r["counts"])), ("hist", None), ("decision", None), ("global", None)):
                crc = zlib.crc32(r[field][:size].tobytes() if size is not None else r[field].tobytes(), crc)
        out[job_id] = [crc, finished[0], len(buffers[0])]
    return out


def parity(workers):
    """The network through the search binary's Referee equals the certified module under JAX on every observation byte, winner and length.
    It must also fail: the same comparison with a JAX network that skips the identity map has to differ."""
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    from ygoenv.ygopro import init_module, ygopro_ygoenv
    from actor import NetPolicy
    if sha(ygopro_ygoenv.__file__) != B.NATIVE_SHA:
        raise SystemExit("the native module is not the certified one")
    info = configure()
    rows, _, training = lists()
    held, known = dev_lists(rows, training)
    jobs = []
    for i, d in enumerate(held):
        for step, o in enumerate((known[i % len(known)], known[(i + 4) % len(known)])):
            for seat in SEATS:
                n = len(jobs)
                jobs.append([f"h{n}", *((d, o) if seat == 0 else (o, d)), dev_seed(n), -1, f"n{OPTIMIZERS[n % 3]}"])
    for i in range(12):
        n = len(jobs)
        jobs.append([f"k{n}", known[i % len(known)], known[(i + 1) % len(known)], dev_seed(n), -1, f"n{OPTIMIZERS[n % 3]}"])
    native = play_groups([held + known], jobs, lambda j: held[0], DEV / "parity", workers, fresh=True)
    os.chdir(sc.SCRIPTS)
    decks = all_decks()
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(decks[k]) for k in held + known})
    nets = {int(k): NetPolicy(str(B.RUNS / f"main-{k}/checkpoint-{B.TRAIN['rounds']:04d}"), info[k]["params_sha256"]) for k in OPTIMIZERS}
    wide = Counter()
    jax = production_games(jobs, nets, wide)
    bent = production_games([j for j in jobs if j[0].startswith("h")], {k: SkipsTheMap(v) for k, v in nets.items()}, Counter())
    seen = {j[0]: [native[j[0]]["crc"], native[j[0]]["winner"], native[j[0]]["decisions"]] if native[j[0]]["status"] == "OK" else native[j[0]]["status"] for j in jobs}
    diffs = [{"job": j[0], "decks": j[1:3], "native": seen[j[0]], "jax": jax[j[0]]} for j in jobs if seen[j[0]] != jax[j[0]]]
    caught = sum(seen[k] != v for k, v in bent.items())
    result = {"held_out_builds": held, "games": len(jobs), "held_out_games": len(bent), "decisions": sum(v[2] for v in jax.values()),
              "games_with_a_prompt_only_the_wider_tables_hold": sum(v > 0 for v in wide.values()), "decisions_with_one": sum(wide.values()),
              "games_differing": len(diffs), "diffs": diffs[:12],
              "control": "the JAX side reads each id's own row instead of the identity map", "control_games_differing": caught,
              "ran_on": dev_key(), "seeds": [dev_seed(0), dev_seed(len(jobs) - 1)]}
    result["verdict"] = "PASS" if not diffs and caught >= len(bent) // 2 and result["decisions_with_one"] > 0 else "FAIL"
    (DEV / "parity.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k not in ("held_out_builds", "ran_on")}, indent=1))
    return result["verdict"] == "PASS"


def values():
    """Plain games compare moves, which follow the logits. The search also reads the value head, at every rollout's leaf.
    So: each sealed network's native logits and values against JAX on recorded v2 observations of lists with untrained cards,
    under the identity differential's gates. A build with the identity lookup of card rows removed must fail them."""
    import identity_check as ic
    info = configure()
    out = DEV / "values"
    out.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "JAX_PLATFORMS": "cpu", "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1", "TF_CPP_MIN_LOG_LEVEL": "2",
           "XLA_FLAGS": "--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1"}

    def call(command):
        subprocess.run([str(c) for c in command], check=True, env=env, stdout=subprocess.DEVNULL)
    call([sys.executable, ROOT / "tools/edison/search/reference.py", "expect", "--decisions", DECISIONS, "--output", out / "decisions.expect"])
    report = {"decisions_sha256": sha(DECISIONS), "nets": {}}
    for k in OPTIMIZERS:
        stem = B.RUNS / f"main-{k}/checkpoint-{B.TRAIN['rounds']:04d}"
        call([sys.executable, ROOT / "tools/edison/search/reference.py", "net", "--decisions", DECISIONS, "--checkpoint", stem, "--output", out / f"{k}.ref"])
        c = info[k]["config"]
        shape = f"{c['n_codes']},{c['id_dim']},{c['channels']},{c['layers']},{c['heads']},{c['critic_hidden']},1"

        def diff(binary, name):
            target = out / f"{k}-{name}.json"
            call([NET_BIN / binary, "--net", NETS / f"gauntlet-{k}.ednet", "--params-sha", info[k]["params_sha256"], "--codes-sha", info[k]["codes_sha256"],
                  "--decisions", DECISIONS, "--expect", out / "decisions.expect", "--reference", out / f"{k}.ref", "--json", target,
                  "--config", shape, "--id-row-sha", info[k]["id_row_sha256"]])
            got = json.loads(target.read_text())
            return {"failed_gates": sorted(ic.gate_failures(got)), "records": got["records"], "greedy_mismatches": got["greedy_mismatch_count"],
                    "value_error_max": got["value_error"]["max"], "logit_error_scaled_max": got["logit_error_scaled"]["max"]}
        report["nets"][k] = {"correct": diff("net_diff", "correct"), "fault_in_card_rows": diff("net_diff_fault16", "fault16")}
    good = all(not v["correct"]["failed_gates"] and not v["correct"]["greedy_mismatches"] and v["correct"]["records"] > 0 and v["fault_in_card_rows"]["failed_gates"]
               for v in report["nets"].values())
    report.update({"net_diff_sha256": sha(NET_BIN / "net_diff"), "ran_on": dev_key(), "verdict": "PASS" if good else "FAIL"})
    (DEV / "values.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "ran_on"}, indent=1))
    return good


def smoke(workers):
    """A few search games at the frozen setting on held-out builds, on two workers and on many: the games must be the same and none void."""
    configure()
    rows, _, training = lists()
    held, known = dev_lists(rows, training)
    pairs = [(d, known[(3 * i) % len(known)]) for i, d in enumerate(held)]
    budget, depth, candidates, z = SEARCH_SETTING
    jobs = []
    for p, (d, o) in enumerate(pairs):
        for seat in SEATS:
            d0, d1 = (d, o) if seat == 0 else (o, d)
            net = f"n{OPTIMIZERS[p % 3]}"
            jobs.append([f"g{p}_{seat}", d0, d1, dev_seed(1000 + p), seat, net, budget, depth, candidates, z, 1, 7000 + 2 * p + seat, 0])
            jobs.append([f"p{p}_{seat}", d0, d1, dev_seed(1000 + p), -1, net])
    one = play_groups([held + known], jobs, lambda j: held[0], DEV / "smoke-one", 2, fresh=True)
    many = play_groups([held + known], jobs, lambda j: held[0], DEV / "smoke-many", workers, fresh=True)
    strip = lambda g: {k: v for k, v in g.items() if k not in STRIP}  # noqa: E731
    differing = [k for k in one if strip(one[k]) != strip(many[k])]
    result = {**summary(many, pairs, set()), "games_differing_between_worker_counts": differing, "ran_on": dev_key()}
    g = result["search_games"]
    # A network refusal inside a rollout drops the sample, and a search with no sample left plays the network's move in a game that still ends OK.
    healthy = g["searched"] > 0 and g["net_errors"] == 0 and g["option_mismatch"] == 0 and g["fell_back"] <= FALLBACK_LIMIT * g["searched"]
    result["verdict"] = "PASS" if not differing and not result["void"] and not result["missing"] and healthy else "FAIL"
    (DEV / "smoke.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k not in ("by_seat", "heavy", "not_heavy", "ran_on")}, indent=1))
    return result["verdict"] == "PASS"


def selftest():
    rows, held, training = lists()
    pairs, search, plain = search_jobs(held, training)
    groups = A.search_groups(pairs, training)
    checks = []

    def check(name, ok, detail=""):
        checks.append((name, bool(ok), detail))
    seeds = [j[3] for j in search]
    lo, hi = B.OTHER_BLOCKS["search check"]
    check("240 pairs, 480 search games and 480 plain games", (len(pairs), len(search), len(plain)) == (240, 480, 480))
    check("60 distinct held-out builds, 4 distinct training lists each", len({d for d, _ in pairs}) == 60 and all(len({o for d2, o in pairs if d2 == d}) == 4 for d, _ in pairs))
    check("seeds are 90300000 onward, one per pair, inside the block Stage B set aside", sorted(set(seeds)) == list(range(SEARCH_SEED, SEARCH_SEED + 240)) and lo <= min(seeds) and max(seeds) <= hi)
    check("no seed of the Stage B test", min(seeds) > B.KNOWN_END and min(seeds) > B.HELD_END, f"{min(seeds)}-{max(seeds)}")
    check("development seeds are outside every other block", all(b < DEV_SEED or a > DEV_END for a, b in B.OTHER_BLOCKS.values()) and DEV_END < B.HELD_SEED)
    check("search games pair with plain games", A.pairing_ok(search, plain))
    bent = [list(j) for j in plain]
    bent[5][5] = "n1702" if bent[5][5] != "n1702" else "n1703"
    check("the pairing check fails when one plain game uses another net", not A.pairing_ok(search, bent))
    check("the search side pilots the held-out build", all((j[1] if j[4] == 0 else j[2]) == pairs[int(j[0][1:].split("_")[0])][0] for j in search))
    check("the setting is M2's", SEARCH_SETTING == A.SEARCH_SETTING and all(tuple(j[6:10]) == SEARCH_SETTING for j in search))
    check("each net plays a third of the pairs", Counter(j[5] for j in search) == {f"n{k}": 160 for k in OPTIMIZERS})
    covered = [d for g in groups for d in g if d.startswith("h_")]
    check("groups hold 21 lists, each drawn build once, with the lists it meets", all(len(set(g)) == 21 for g in groups) and sorted(covered) == sorted({d for d, _ in pairs})
          and all(o in g for g in groups for d, o in pairs if d in g), str(len(groups)))
    heavy = {r["name"] for r in rows if r["unknown_main_copies"] >= B.HEAVY}
    check("the draw holds heavy builds", sum(d in heavy for d in {d for d, _ in pairs}) >= 5, str(sum(d in heavy for d in {d for d, _ in pairs})))
    made = {}
    for p in range(len(pairs)):
        for seat in SEATS:
            made[f"g{p}_{seat}"] = {"status": "OK", "winner": seat if p % 4 else 1 - seat, "search_seat": seat}
            made[f"p{p}_{seat}"] = {"status": "OK", "winner": seat if p % 2 else 1 - seat}
    made["g7_0"] = {"status": "VOID", "reason": "planted"}
    del made["p9_1"]
    s = summary(made, pairs, heavy)
    check("a void search game and a missing plain game drop their comparisons and are counted", s["all"]["comparisons"] == 478 and s["void"] == {"search": 1} and s["missing"] == {"plain": 1})
    check("the summary scores the search seat", abs(s["all"]["search"] - (360 - 1) / 478) < 0.01 and abs(s["all"]["plain"] - 0.5) < 0.01, f'{s["all"]["search"]:.4f} {s["all"]["plain"]:.4f}')
    check("heavy and the rest split the comparisons", s["heavy"]["comparisons"] + s["not_heavy"]["comparisons"] == 478)
    for name, ok, detail in checks:
        print(("ok   " if ok else "FAIL ") + name + (f"  [{detail}]" if detail else ""))
    print(f"{sum(ok for _, ok, _ in checks)} of {len(checks)} checks pass")
    return all(ok for _, ok, _ in checks)


# ---------------------------------------------------------------- the check

def inputs():
    rows, held, training = lists()
    pairs, search, plain = search_jobs(held, training)
    decks = all_decks()
    used = sorted({name for j in search for name in j[1:3]})
    lua = hashlib.sha256()
    for path in sorted((ROOT / "scripts/script").resolve().glob("*.lua")):
        lua.update(path.name.encode() + b"\0" + path.read_bytes())
    return {"binary": binary_inputs(), "nets": json.loads(EXPORT.read_text()), "native_module": sha(B.NATIVE), "database": sha(deck_registry.DB), "lua_scripts": lua.hexdigest(),
            "files": {p: sha(ROOT / p) for p in DEPENDS}, "stage_b_result": sha(STAGE_B_RESULT), "build": str(B.BUILD),
            "decks": {name: sha(decks[name]) for name in used}, "jobs": text_sha(json.dumps([search, plain])), "groups": A.search_groups(pairs, training),
            "constants": {"SEARCH_SEED": SEARCH_SEED, "SEARCH_DRAW": SEARCH_DRAW, "SEARCH_LISTS": SEARCH_LISTS, "SEARCH_OPPONENTS": SEARCH_OPPONENTS,
                          "SEARCH_SETTING": SEARCH_SETTING, "RESAMPLES": RESAMPLES}}


def committed():
    """Every file the check depends on is in HEAD as it is on disk. git status alone would pass a file that was never added."""
    for p in DEPENDS:
        if subprocess.run(["git", "ls-files", "--error-unmatch", p], cwd=ROOT, capture_output=True).returncode or git("status", "--porcelain", "--", p):
            raise SystemExit(f"{p} is not committed as it stands")


def same_as_stage_b(now):
    """The search games must run on what the Stage B test ran on: the module, database, scripts, decks, code list, identity map and interface code."""
    then = json.loads(B.MANIFEST.read_text())["inputs"]
    differing = [k for k, t in (("native_module", "native"), ("database", "database"), ("lua_scripts", "lua_scripts")) if now[k] != then[t]]
    differing += [name for name, value in now["decks"].items() if then["decks"].get(name) != value]
    differing += [p for p in SAME_AS_STAGE_B if now["files"][p] != then["files"][p]]
    if differing:
        raise SystemExit(f"inputs differ from the Stage B test's: {differing[:8]}")


def freeze():
    if git("status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("commit everything first: the manifest names one commit")
    committed()
    if MANIFEST.exists() or any(B.BUILD.glob("search-check-*")):
        raise SystemExit("a manifest or results exist already: the check runs once")
    if json.loads(STAGE_B_RESULT.read_text()).get("seal_sha256") != sha(B.SEAL):
        raise SystemExit("the Stage B verdict for this seal is not in the repository: the search check comes after it")
    configure()
    # The binary must be what the committed sources build. The build is reproducible, so building again must change nothing.
    before = binary_inputs()
    subprocess.run(["bash", str(ROOT / "tools/edison/forward_model/build.sh"), "edison_search"], check=True, stdout=subprocess.DEVNULL)
    if binary_inputs() != before:
        raise SystemExit("the search binary was not built from these sources: it has been rebuilt, so run the development checks again")
    key = dev_key()
    for name in DEV_CHECKS:
        done = json.loads((DEV / f"{name}.json").read_text()) if (DEV / f"{name}.json").exists() else {}
        if done.get("verdict") != "PASS" or done.get("ran_on") != key:
            raise SystemExit(f"the {name} check has not passed on this binary, these networks and these sources")
    same_as_stage_b(json.loads(json.dumps(inputs())))
    manifest = {"commit": git("rev-parse", "HEAD"), "inputs": json.loads(json.dumps(inputs()))}
    MANIFEST.write_text(json.dumps(manifest, indent=1) + "\n")
    print(json.dumps({k: v for k, v in manifest["inputs"].items() if k not in ("decks", "files", "groups", "nets")} | {"commit": manifest["commit"]}, indent=1))


def check_frozen():
    manifest = json.loads(MANIFEST.read_text())
    path = str(MANIFEST.relative_to(ROOT))
    if git("status", "--porcelain", "--untracked-files=no") or git("status", "--porcelain", "--", path):
        raise SystemExit("uncommitted changes, or an uncommitted manifest")
    if git("rev-parse", "HEAD^") != manifest["commit"] or git("diff", "--name-only", "HEAD^", "HEAD").split() != [path]:
        raise SystemExit("the manifest must be committed alone, directly on the commit it names")
    committed()
    configure()
    now = json.loads(json.dumps(inputs()))
    same_as_stage_b(now)
    changed = [k for k in now if now[k] != manifest["inputs"][k]]
    if changed:
        raise SystemExit(f"inputs differ from the manifest: {changed}")
    return manifest


def run(workers):
    manifest = check_frozen()
    rows, held, training = lists()
    pairs, search, plain = search_jobs(held, training)
    out = out_dir()
    out.mkdir(parents=True, exist_ok=True)
    with (out / "attempts.jsonl").open("a") as log:
        log.write(json.dumps({"head": git("rev-parse", "HEAD"), "manifest": sha(MANIFEST), "workers": workers}) + "\n")
    build_of = lambda j: pairs[int(j[0][1:].split("_")[0])][0]  # noqa: E731
    games = play_groups(manifest["inputs"]["groups"], search + plain, build_of, out, workers)
    heavy = {r["name"] for r in rows if r["unknown_main_copies"] >= B.HEAVY}
    result = {**summary(games, pairs, heavy), "manifest_sha256": sha(MANIFEST), "seal_sha256": sha(B.SEAL), "binary": manifest["inputs"]["binary"],
              "heavy_builds_drawn": sum(d in heavy for d in {d for d, _ in pairs}), "attempts": len((out / "attempts.jsonl").read_text().splitlines())}
    (out / "search-check-result.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "search_games"}, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("export", "parity", "values", "smoke", "selftest", "freeze", "run"))
    parser.add_argument("--workers", type=int, default=15)
    args = parser.parse_args()
    if args.command == "export":
        export_nets()
    elif args.command in ("parity", "smoke"):
        raise SystemExit(0 if {"parity": parity, "smoke": smoke}[args.command](args.workers) else 1)
    elif args.command == "values":
        raise SystemExit(0 if values() else 1)
    elif args.command == "selftest":
        raise SystemExit(0 if selftest() else 1)
    elif args.command == "freeze":
        freeze()
    else:
        run(args.workers)


if __name__ == "__main__":
    main()
