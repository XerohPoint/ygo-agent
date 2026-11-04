#!/usr/bin/env python3
"""Add the 13 cards the YGOPRODeck filter omits to assets/edison/legality.json. Run once.

Evidence comes from docs/edison-pool-audit.md: each card is in a set on edisonformat.com's legal
sets page, and each has a TCG date before the format's April 24, 2010 date. The set and date below
are from the dated YGOPRODeck listing the audit saved. Format Library's card API gave all 13 a date
between December 2007 and February 2010 in the supervisor's check, but per-card values from that
check were not archived, so this does not restate them. Restrictions are not changed.
"""
import json
from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "assets/edison/legality.json"

ADDED = [
    (52352005, "XX-Saber Gottoms", "ANPR", "2009-09-01"),
    (46480475, "Discord", "ANPR", "2009-09-01"),
    (39980304, "Chain Material", "PTDN", "2008-01-26"),
    (49808196, "Metabo Globster", "PTDN", "2008-01-26"),
    (11508758, "Mutant Mindmaster", "PTDN", "2008-03-12"),
    (3891471, "Psychic Tuning", "CRMS", "2009-02-28"),
    (35984222, "Ido the Supreme Magical Force", "CRMS", "2009-02-28"),
    (88289295, "Synchro Control", "ABPF", "2010-02-12"),
    (70284332, "Tuner's Scheme", "ABPF", "2010-02-12"),
    (52496105, "Gladiator Beast's Battle Manica", "GLAS", "2007-11-14"),
    (34351849, "Froggy Forcefield", "LODT", "2008-05-03"),
    (27416701, "Shiba-Warrior Taro", "YAP1", "2008-12-08"),
    (79205581, "Scrubbed Raid", "DP09", "2010-01-15"),
]

def main():
    data = json.loads(MANIFEST.read_text())
    present = {card["code"] for card in data["cards"]}
    db = sqlite3.connect(ROOT / "assets/locale/en/cards.cdb")
    for code, name, _, date in ADDED:
        if code in present:
            raise SystemExit(f"Already present: {name}")
        row = db.execute("select t.name,d.alias from datas d join texts t using(id) where d.id=?", (code,)).fetchone()
        if not row or row[0] != name or row[1]:
            raise SystemExit(f"Database disagrees for {code}: {row}")
        if not date < "2010-04-24":
            raise SystemExit(f"Dated after the format: {name}")
    data["cards"] = sorted(data["cards"] + [{"code": c, "name": n, "accepted_ids": [c]} for c, n, _, _ in ADDED],
                           key=lambda card: card["code"])
    data["sources"] += [
        {"url": "https://www.edisonformat.com/legal-sets.html",
         "selection_basis": "Pool authority: a card is in the pool if it is in a listed legal set and released in the TCG before the format date. Page read by docs/edison-pool-audit.md; no hash archived."},
        {"url": "https://formatlibrary.com/api",
         "selection_basis": "Format date April 24, 2010, and TCG dates for the 13 additions, checked by the project supervisor; see docs/edison-pool-audit.md. Per-card values from that check were not archived."},
        {"url": "https://db.ygoprodeck.com/api/v7/cardinfo.php",
         "selection_basis": "Dated listing the audit used to find the additions; tests/edison/reports/pool-audit-2026-11-04/pool_crosscheck.json.gz records each card's set and date."},
    ]
    data["notes"] += [
        "Pool authority is the listed legal sets plus Format Library's TCG dates, as decided in docs/edison-roadmap.md. The YGOPRODeck Edison filter named in sources[0] is the starting list; it omits cards that meet that test, such as XX-Saber Gottoms, which is in 8 of 75 sampled tournament lists.",
        "Only XX-Saber Gottoms is proven in real lists. The other 12 are inferred from set listings and dates. Reasons a card might still be excluded were not checked, and owner review of the authority is still open.",
        "Duel Terminal printings are not on the legal sets page, so cards only in DT01 stay out. Discord's DT04 printing is not its basis; ANPR is.",
    ]
    data["additions"] = [
        {"code": c, "name": n, "legal_set": s, "tcg_date": d,
         "basis": "docs/edison-pool-audit.md: in a listed legal set, dated before 2010-04-24"}
        for c, n, s, d in ADDED]
    MANIFEST.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    print(f"cards: {len(data['cards'])}; restrictions unchanged: {len(data['restrictions'])}")

if __name__ == "__main__":
    main()
