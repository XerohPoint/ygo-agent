"""Shared paths and card-pool metadata for the Edison pool audit. Reads only."""
import json, os, re, sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/edison-pool-audit-2026-11-04"
SCRIPTS = Path(os.environ.get("EDISON_SCRIPTS", "/home/mada/ygopro-scripts"))
DB = ROOT / "assets/locale/en/cards.cdb"
LEGALITY = ROOT / "assets/edison/legality.json"
HISTORICAL = ROOT / "ygoenv/ygoenv/ygopro/historical_scripts.h"
FULL_CODES = OUT / "codes_full.txt"
AUDIT_SO_DIR = OUT / "audit_module"

TYPE_MONSTER, TYPE_SPELL, TYPE_TRAP = 0x1, 0x2, 0x4
TYPE_NORMAL, TYPE_EFFECT, TYPE_FUSION, TYPE_RITUAL = 0x10, 0x20, 0x40, 0x80
TYPE_TUNER, TYPE_SYNCHRO, TYPE_TOKEN, TYPE_PENDULUM = 0x1000, 0x2000, 0x4000, 0x1000000
ARTWORK_OFFSET = 20  # ygopro-core card_data.h CARD_ARTWORK_VERSIONS_OFFSET

def is_artwork(code, alias):
    return bool(alias) and alias < code + ARTWORK_OFFSET and code < alias + ARTWORK_OFFSET

def script_code(code, alias):
    """The id whose c<id>.lua the core loads (interpreter::register_card)."""
    return alias if is_artwork(code, alias) else code

def needs_script(card_type):
    return not (card_type & TYPE_NORMAL) or bool(card_type & TYPE_PENDULUM)  # is_load_script

def historical_overrides():
    text = HISTORICAL.read_text()
    found = set(int(x) for x in re.findall(r'name\s*[!=]=\s*"c(\d+)\.lua"', text))
    return found

def connect():
    db = sqlite3.connect(DB)
    db.row_factory = sqlite3.Row
    return db

def load_pool():
    """Return manifest entries; each has every accepted id with its database row."""
    manifest = json.loads(LEGALITY.read_text())
    limits = {r["code"]: r["limit"] for r in manifest["restrictions"]}
    db = connect()
    entries, by_id = [], {}
    for card in manifest["cards"]:
        rows = []
        for code in card["accepted_ids"]:
            row = db.execute(
                "select d.id,d.alias,d.type,d.ot,d.setcode,d.atk,d.def,d.level,d.race,d.attribute,t.name,t.desc "
                "from datas d join texts t on d.id=t.id where d.id=?", (code,)).fetchone()
            rows.append(dict(row) if row else None)
        base = next((r for r in rows if r and not is_artwork(r["id"], r["alias"])), None) or rows[0]
        name_key = base["alias"] or base["id"]
        entry = {"entry_code": card["code"], "name": card["name"], "ids": card["accepted_ids"],
                 "rows": rows, "base": base["id"], "copy_key": name_key,
                 "limit": limits.get(name_key, 3)}
        entries.append(entry)
        for code in card["accepted_ids"]:
            by_id[code] = entry
    return entries, by_id, manifest
