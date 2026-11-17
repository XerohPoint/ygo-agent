"""Shared by the search's tests and study: the nets, the job format, and running the native binary."""
import hashlib
import json
from pathlib import Path
import shutil
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
NET_DIR = OUT / "nets"
CODES_FILE = ROOT / "tools/edison/learner/codes_v1.txt"
CODE_ARGS = []  # what selects the code list in the binary. Empty is v1, as M2 and M3 Stage A ran


def use_code_list_v2(params_sha, net_dir, id_row_sha):
    """Point this module at code list v2 networks: {name: params sha}, where their exported files are, and the identity map they carry.
    The caller's process must have EDISON_CODE_LIST=v2, so the Python side reads the same list the binary is told to."""
    global CODES_SHA, PARAMS_SHA, NET_DIR, CODES_FILE, CODE_ARGS
    if deck_registry.CODE_LIST != "v2":
        raise SystemExit("use_code_list_v2 needs EDISON_CODE_LIST=v2 before decks is imported")
    CODES_FILE = Path(deck_registry.CODES)
    CODES_SHA = file_sha(CODES_FILE)
    if CODES_SHA != deck_registry.load_codes()[0]:
        raise SystemExit("the code list file is not the one decks loads")
    PARAMS_SHA, NET_DIR, CODE_ARGS = dict(params_sha), Path(net_dir), ["--code-list", "v2", "--id-row-sha", id_row_sha]
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
        args += ["--net", f"{name}={NET_DIR / ('gauntlet-' + name[1:] + '.ednet')}={PARAMS_SHA[name]}"]
    return args


def job_line(job_id, deck0, deck1, seed, seat, net, budget=0, depth=0, candidates=0, z=0.0, halving=1, search_seed=0, mode=0, opponent_net=None):
    if any(low <= seed <= high for low, high in FORBIDDEN):
        raise SystemExit(f"seed {seed} is forbidden")
    return f"{job_id}\t{deck0}\t{deck1}\t{seed}\t{seat}\t{net}\t{budget}\t{depth}\t{candidates}\t{z}\t{halving}\t{search_seed}\t{mode}\t{opponent_net or net}\n"


def file_sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inputs(binary, cwd, extra=()):
    """Everything a game's result depends on besides its job line. Only this entry point checks all of it: run directly, the binary checks the job and its own build."""
    scripts = hashlib.sha256()
    for path in sorted((Path(cwd) / "script").resolve().glob("*.lua")):
        scripts.update(path.name.encode() + b"\0" + path.read_bytes())
    lists = hashlib.sha256()
    for line in sorted(REGISTRY.read_text().splitlines()):
        name, path = line.split(" ", 1)
        lists.update(name.encode() + b"\0" + Path(path).read_bytes())
    built = {"binary": file_sha(binary), "scripts": scripts.hexdigest(), "database": file_sha(ROOT / "assets/locale/en/cards.cdb"), "lists": lists.hexdigest(), "codes": file_sha(CODES_FILE),
             "nets": {name: file_sha(NET_DIR / f"gauntlet-{name[1:]}.ednet") for name in PARAMS_SHA}, "extra": list(extra)}
    if built["codes"] != CODES_SHA:
        raise SystemExit(f"{CODES_FILE} is not the code list the nets are checked against")
    if CODE_ARGS:
        built["code_list"] = CODE_ARGS
    return built


def attempt_identity(built, lines):
    """What an attempt ran: these inputs on these jobs."""
    return hashlib.sha256(json.dumps({"inputs": built, "jobs": "".join(lines)}, sort_keys=True).encode()).hexdigest()


def play(lines, out_dir, workers, binary=BIN, cwd=SCRIPTS, timeout=3600, nets=None, fresh=False, extra=()):
    """Games in out_dir must come from these jobs and these inputs, so a directory resumes one run and nothing else. fresh discards it first.
    Every attempt is logged beside the directory, even one that dies, under what it ran on. A worker's failure stays as that game's void file, so a resume can't erase it.
    fresh starts a new run, so it clears that log too."""
    out_dir = Path(out_dir)
    jobs = out_dir.with_suffix(".tsv")
    meta = out_dir.with_suffix(".inputs")
    attempts = out_dir.with_suffix(".attempts")
    built = inputs(binary, cwd, extra)
    if fresh:
        attempts.unlink(missing_ok=True)
    if fresh and out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    if any(out_dir.glob("*.json")):
        known = json.loads(meta.read_text()) if meta.exists() else {}
        changed = [key for key in built if known.get(key) != built[key]]
        if changed:
            raise SystemExit(f"{out_dir} holds games from other inputs ({', '.join(changed)}). Use another directory")
        if not jobs.exists() or jobs.read_text() != "".join(lines):
            raise SystemExit(f"{out_dir} holds games from other jobs. Use another directory")
    meta.write_text(json.dumps(built, indent=1) + "\n")
    jobs.write_text("".join(lines))
    identity = attempt_identity(built, lines)
    with attempts.open("a") as log:
        log.write(json.dumps({"event": "start", "inputs": identity}) + "\n")
    done = subprocess.run([str(binary), "play", "--root", str(ROOT), "--registry", str(REGISTRY), *net_args(nets), "--codes-sha", CODES_SHA, *CODE_ARGS, "--jobs", str(jobs),
                           "--out", str(out_dir), "--workers", str(workers), "--timeout", str(timeout)] + list(extra), cwd=cwd, stdout=subprocess.PIPE, text=True)
    try:
        counts = json.loads(done.stdout)
    except ValueError:
        counts = None
    with attempts.open("a") as log:
        log.write(json.dumps({"event": "end", "inputs": identity, "returncode": done.returncode, "counts": counts}) + "\n")
    done.check_returncode()
    wanted = {line.split("\t")[0] for line in lines}
    return {k: v for k, v in load(out_dir).items() if k in wanted}


def load(out_dir):
    return {p.stem: json.loads(p.read_text()) for p in sorted(Path(out_dir).glob("*.json"))}


def rank(deck0, deck1, seed, moves, net="n1702", binary=BIN, cwd=SCRIPTS, **options):
    command = [str(binary), "rank", "--root", str(ROOT), "--registry", str(REGISTRY), *net_args([net]), "--codes-sha", CODES_SHA, *CODE_ARGS, "--deck0", deck0, "--deck1", deck1,
               "--seed", str(seed), "--moves", ",".join(map(str, moves)) or ","]
    for key, value in options.items():
        command += ["--" + key.replace("_", "-"), str(value)]
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True).stdout


def neighbour_pairs(names, steps=4):
    pairs = [(names[i], names[(i + step) % len(names)]) for i in range(len(names)) for step in range(1, steps + 1)]
    return pairs + [(b, a) for a, b in pairs]
