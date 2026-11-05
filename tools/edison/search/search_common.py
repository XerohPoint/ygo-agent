"""Shared by the search's tests and study: the nets, the job format, and running the native binary."""
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools/edison/learner"))
import decks as deck_registry  # noqa: E402

CODES_SHA = "53fcea4df9298ee7c3e0eb548c533048932eb870f090eb456a2ecac031fcbfab"
PARAMS_SHA = {"n1702": "cb4cb1ad86aa5f6e33a339809e421f726225c9c13b70eaed5394f38cec6798cd",
              "n1703": "03132b3f63c97f6aeec7b54c544ce45e450080f235389ac05075950f33f28ce0",
              "n1704": "ddbdcee21ccceb9a60485aa785888b08c8c735ba79b46550ff18f83a9412a3a8"}
OUT = ROOT / "build/edison-search"
BIN = ROOT / "build/forward_model/edison_search"
TEST_BIN = ROOT / "build/forward_model/edison_search_test"
REGISTRY = OUT / "registry.txt"
SCRIPTS = ROOT / "scripts"
FORBIDDEN = [(70000000, 70999999), (79000000, 79999999), (83000000, 83999999)]


def write_registry():
    OUT.mkdir(parents=True, exist_ok=True)
    names = deck_registry.registry()
    REGISTRY.write_text("".join(f"{k} {Path(v).resolve()}\n" for k, v in names.items()))
    return sorted(names)


def net_args(names=None):
    args = []
    for name in names or PARAMS_SHA:
        args += ["--net", f"{name}={OUT / 'nets' / ('gauntlet-' + name[1:] + '.ednet')}={PARAMS_SHA[name]}"]
    return args


def job_line(job_id, deck0, deck1, seed, seat, net, budget=0, depth=0, candidates=0, z=0.0, halving=1, search_seed=0, mode=0):
    if any(low <= seed <= high for low, high in FORBIDDEN):
        raise SystemExit(f"seed {seed} is forbidden")
    return f"{job_id}\t{deck0}\t{deck1}\t{seed}\t{seat}\t{net}\t{budget}\t{depth}\t{candidates}\t{z}\t{halving}\t{search_seed}\t{mode}\n"


def play(lines, out_dir, workers, binary=BIN, cwd=SCRIPTS, timeout=3600, nets=None):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    jobs = out_dir.with_suffix(".tsv")
    jobs.write_text("".join(lines))
    subprocess.run([str(binary), "play", "--root", str(ROOT), "--registry", str(REGISTRY), *net_args(nets), "--codes-sha", CODES_SHA, "--jobs", str(jobs),
                    "--out", str(out_dir), "--workers", str(workers), "--timeout", str(timeout)], cwd=cwd, check=True, stdout=subprocess.DEVNULL)
    return load(out_dir)


def load(out_dir):
    return {p.stem: json.loads(p.read_text()) for p in sorted(Path(out_dir).glob("*.json"))}


def rank(deck0, deck1, seed, moves, net="n1702", binary=BIN, cwd=SCRIPTS, **options):
    command = [str(binary), "rank", "--root", str(ROOT), "--registry", str(REGISTRY), *net_args([net]), "--codes-sha", CODES_SHA, "--deck0", deck0, "--deck1", deck1,
               "--seed", str(seed), "--moves", ",".join(map(str, moves)) or ","]
    for key, value in options.items():
        command += ["--" + key.replace("_", "-"), str(value)]
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True).stdout


def neighbour_pairs(names, steps=4):
    pairs = [(names[i], names[(i + step) % len(names)]) for i in range(len(names)) for step in range(1, steps + 1)]
    return pairs + [(b, a) for a, b in pairs]
