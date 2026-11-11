"""cmp_prod.py RUNS OLD NEW PREFIX...: compare two trees' worker logs duel by duel (every line between begin and end)."""
import json, sys
from pathlib import Path
R = Path(sys.argv[1]); old, new = sys.argv[2], sys.argv[3]

def duels(path):
    out, cur, lines = {}, None, []
    for line in path.read_text().splitlines():
        if line.startswith('{"begin"'):
            cur, lines = json.loads(line)["begin"], []
        elif line.startswith('{"end"'):
            out[cur] = (lines, json.loads(line)["end"])
            cur = None
        elif cur is not None:
            lines.append(line)
    return out

report = {}
for pre in sys.argv[4:]:
    same, differ, missing, decisions = 0, [], 0, 0
    for a in sorted(R.glob(f"{old}-{pre}-*.log")):
        b = R / a.name.replace(f"{old}-", f"{new}-", 1)
        da, db = duels(a), duels(b) if b.exists() else {}
        for k, (lines, end) in da.items():
            if k not in db: missing += 1; continue
            decisions += len(lines)
            if db[k][0] == lines and db[k][1] == end: same += 1
            else:
                first = next((i for i, (x, y) in enumerate(zip(lines, db[k][0])) if x != y), min(len(lines), len(db[k][0])))
                differ.append({"duel": k, "first_difference_at_line": first, "lines": [len(lines), len(db[k][0])]})
    report[pre] = {"same": same, "differ": differ, "missing": missing, "decision_lines": decisions}
    print(f"{pre}: {same} identical, {len(differ)} differ, {missing} missing, {decisions} lines" + (f"; differ: {[d['duel'] for d in differ]}" if differ else ""))
Path(R.parent / "gates" / f"compare-{old}-{new}.json").write_text(json.dumps(report, indent=1) + "\n")
