"""mk_g4.py: specs/g4-NNN.json, new duels for the Gold Sarcophagus fix.

Every corpus list with Gold Sarcophagus or Jinzo - Returner in its Main Deck plays 10 duels against
opponents drawn from the production-legal corpus with random.Random(4). Seed s: even seeds put the target list
first, odd seeds second; the policy is (uniform, eager, heuristic)[s % 3]. Seeds 91000000 upward.
"""
import json, random
from pathlib import Path
W = Path(__file__).resolve().parents[4]
Y = W / "assets/edison/corpus/ydk"
S = W / "build/sarc/specs"; S.mkdir(parents=True, exist_ok=True)
POL = ("uniform", "eager", "heuristic")

def main_deck(f):
    out, sec = [], None
    for line in f.read_text().splitlines():
        line = line.strip()
        if line.startswith(("#", "!")): sec = line; continue
        if sec == "#main" and line.isdigit(): out.append(int(line))
    return out

manifest = json.loads((W / "assets/edison/corpus/manifest.json").read_text())
names = sorted(r["list_id"] for r in manifest["lists"] if r["production_legal"])
targets = [n for n in names if {75500286, 9418534} & set(main_deck(Y / f"{n}.ydk"))]
rng = random.Random(4)
pairs = [(t, rng.choice([n for n in names if n != t])) for t in targets for _ in range(10)]
first = 91000000
for k in range(0, len(pairs), 50):
    duels, decks = [], {}
    for i, (t, o) in enumerate(pairs[k:k + 50]):
        s = first + k + i
        a, b = (t, o) if s % 2 == 0 else (o, t)
        decks[a] = str(Y / f"{a}.ydk"); decks[b] = str(Y / f"{b}.ydk")
        duels.append({"id": str(s), "seed": s, "deck0": a, "deck1": b, "policy": POL[s % 3], "lists": [t, o]})
    (S / f"g4-{k // 50:03d}.json").write_text(json.dumps({"decks": decks, "duels": duels}))
print(len(targets), "target lists,", len(pairs), "duels")
