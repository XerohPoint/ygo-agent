#!/usr/bin/env python3
"""Print the Markdown tables used by docs/edison-pool-audit.md from committed evidence."""
import collections, csv, gzip, json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
REP = ROOT / "tests/edison/reports/pool-audit-2026-11-04"

def main():
    rows = list(csv.DictReader(gzip.open(REP / "per-card.csv.gz", "rt")))
    summary = json.loads((REP / "summary.json").read_text())
    confirmed = json.loads((REP / "confirmed-crash-cards.json").read_text())
    print("### by kind\n\n| Kind | Entries | Forbidden | Chain seen | Summoned, no chain | Offered only | In deck only |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
    kinds = collections.defaultdict(collections.Counter)
    for r in rows: kinds[r["kind"]][r["status"]] += 1
    for kind, c in sorted(kinds.items()):
        print(f"| {kind} | {sum(c.values())} | {c['forbidden_no_production_duel']} | {c['activated']} | {c['summoned'] + c['summoned_not_activated']} | {c['offered_only']} | {c['in_deck_only']} |")
    print("\n### crash classes\n\n| Failure | Duels | Cards confirmed | Examples |\n| --- | ---: | ---: | --- |")
    merged, cards = collections.Counter(), collections.defaultdict(list)
    norm = lambda s: re.sub(r"Number \d+", "Number N", re.sub(r", length \d+, dp 1", "", s))
    for c in summary["crash_classes"]:
        merged[norm(c["signature"])] += c["count"]
    for sig, names in confirmed["classes"].items():
        cards[norm(sig)].extend(names)
    for sig, n in merged.most_common():
        print(f"| `{sig}` | {n} | {len(cards[sig])} | {', '.join(sorted(cards[sig])[:5])} |")
    print("\nconfirmed", len(confirmed["cards"]), "crash duels", sum(merged.values()))
    names = {v["name"] for v in confirmed["cards"].values()}
    low = [r for r in rows if r["status"] in ("offered_only", "in_deck_only")]
    print("no chain or summon:", len(low), "of which crash-confirmed", sum(r["name"] in names for r in low))
    print("activatable never chained:", sum(r["has_activated_effect"] == "1" and r["limit"] != "0" and r["chains"] == "0" for r in rows))
    print("median soak decks", sorted(int(r["soak_decks"]) for r in rows if r["limit"] != "0")[1808])
    data = json.loads((REP / "gauntlet.json").read_text())
    print("\n### gauntlet\n\n| Deck | Replay share | Event | Date | Place | Players | Source | Production legal | Exercised in soak | Random duels crash-free, main | Side-in crashes | Confirmed crash cards |\n| --- | ---: | --- | --- | ---: | ---: | --- | --- | ---: | ---: | ---: | --- |")
    for d in data["decks"]:
        print(f"| {d['name']} | {d['share']*100:.2f}% | {d['event']} | {d['date']} | {d['placement']} | {d['size']} | [{d['url'].split('/')[-1]}](https://formatlibrary.com/api/decks/{d['url'].split('/')[-1]}) | {'yes' if d['legal'] else 'NO: XX-Saber Gottoms'} | {d['coverage'][0]}/{d['coverage'][1]} | {d['main']['ok']}/{d['main']['ok']+d['main']['crash']} | {d['sidein']['crash']} | {', '.join(d['interface']) or 'none'} |")
    print("\nshare", data["replay_share_all_archetypes"], data["replay_share_legal_lists_only"])
    print("\n### e35\n")
    for d in data["decks"]:
        print(f"- {d['name']}: {', '.join(sorted(d['e35'])) or 'none'}" + (f" (other: {', '.join(d['other_risk'])})" if d["other_risk"] else ""))

if __name__ == "__main__":
    main()
