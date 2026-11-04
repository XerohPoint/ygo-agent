#!/usr/bin/env python3
"""Write the audit code list: the legal pool plus every card a pool script can request.

Format matches init_module: "<code> <has_script>"; line number + 1 is the card id. The
core asks for token and changed-name metadata, and for alias base scripts, so a list of
legal ids alone is not closed. See report sections on resource closure.
"""
import json, re
from common import *

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    entries, by_id, _ = load_pool()
    db = connect()
    all_rows = {r["id"]: r for r in db.execute("select id,alias,type from datas")}
    pool = sorted(by_id)
    closure, reasons = set(pool), {}
    def add(code, why):
        if code in all_rows and code not in closure:
            closure.add(code); reasons.setdefault(code, set()).add(why)
    tokens = [c for c, r in all_rows.items() if r["type"] & TYPE_TOKEN]
    for code in tokens: add(code, "database token")
    for code in pool:
        row = all_rows[code]
        if row["alias"]: add(row["alias"], "alias of pool card")
        path = SCRIPTS / f"c{script_code(code, row['alias'])}.lua"
        if not path.exists(): continue
        for number in set(re.findall(r"\b(\d{5,9})\b", path.read_text(errors="replace"))):
            add(int(number), f"referenced by script c{code}")
    lines = []
    for code in sorted(closure):
        lines.append(f"{code} {1 if needs_script(all_rows[code]['type']) else 0}")
    FULL_CODES.write_text("\n".join(lines) + "\n")
    pool_lines = [f"{c} {1 if needs_script(all_rows[c]['type']) else 0}" for c in pool]
    (OUT / "codes_pool_only.txt").write_text("\n".join(pool_lines) + "\n")
    (OUT / "codes_closure_reasons.json").write_text(json.dumps(
        {str(k): sorted(v) for k, v in reasons.items()}, indent=0))
    print(f"pool ids {len(pool)}; closure {len(closure)}; added {len(closure)-len(pool)}; tokens {len(tokens)}")

if __name__ == "__main__":
    main()
