#!/usr/bin/env python3
"""Print soak crash classes with the cards most associated with each."""
import json, re, sys
from collections import Counter, defaultdict
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import *
state = json.loads((OUT / "soak/state.json").read_text())
db = connect()
name = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or ["?"])[0]
sig, cards = Counter(), defaultdict(Counter)
for f in state["failures"]:
    s = f.get("signature") or f["kind"]
    s = re.sub(r"Env \d+", "Env N", s)
    sig[s] += 1
    for c in f.get("attributed") or []: cards[s][c] += 1
print("statuses", state["statuses"], "steps", state["total_steps"], "max_options", state["max_options"])
print("violations", state["violations"])
print("lua", len(state["lua"]), list(state["lua"].items())[:20])
print("diag", list(state["diag"].items())[:20])
for s, n in sig.most_common():
    print(n, s, [(c, name(c), k) for c, k in cards[s].most_common(5)])
