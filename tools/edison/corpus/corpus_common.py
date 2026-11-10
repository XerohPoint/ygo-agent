"""Shared paths and rules for the M3 deck corpus. Reads only, except where a tool says so."""
import json, re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build/edison-corpus"
RAW = BUILD / "raw"  # Untracked: raw responses hold player names.
DEST = ROOT / "assets/edison/corpus"
YDK_DIR = DEST / "ydk"
REPORT = ROOT / "tests/edison/reports/corpus-2026-11-09"
GAUNTLET = ROOT / "assets/edison/gauntlet_candidates"
SEED_FIRST, SEED_LAST = 88000000, 88999999

HISTORICAL = {"SJCED10"}  # Played in April 2010 under the official rules of the time.
RULESET_COMMUNITY = "Community traditional Edison"
RULESET_HISTORICAL = "2010 official event (Konami SJC, April 2010), rules as judged then"


def excluded_event(meta):
    """Return the reason an event is kept out of the corpus, or None."""
    name = (meta.get("name") or "").lower()
    abbr = meta.get("abbreviation") or ""
    year = (meta.get("endDate") or "")[:4]
    if "time wizard" in name or abbr.upper().startswith("UTWT"):
        return "Konami Time Wizard event: modern mechanics, not traditional Edison"
    if (meta.get("communityName") or "") == "Konami" and abbr not in HISTORICAL:
        return "Konami event after 2010 with no stated traditional ruleset"
    if (meta.get("formatName") or "Edison") != "Edison":
        return f"Format is {meta.get('formatName')}, not Edison"
    return None


def ruleset(meta):
    return RULESET_HISTORICAL if meta.get("abbreviation") in HISTORICAL else RULESET_COMMUNITY


def normalize_label(label):
    """Casefold, '&' to 'and', drop punctuation, words joined by '_'. Nothing is merged by meaning."""
    text = (label or "").casefold().replace("&", " and ")
    text = re.sub(r"[^0-9a-z]+", " ", text)
    return "_".join(text.split()) or "unlabelled"


def parse_ydk_text(text):
    """Card lines in source order by zone. Lines before '#main' and comments are ignored."""
    zones, zone = {"main": [], "extra": [], "side": []}, None
    for raw in text.splitlines():
        line = raw.strip()
        if line == "#main": zone = "main"
        elif line == "#extra": zone = "extra"
        elif line == "!side": zone = "side"
        elif re.fullmatch(r"\d+", line) and zone: zones[zone].append(int(line))
    return zones


def multiset_key(zones):
    return tuple(tuple(sorted(zones[z])) for z in ("main", "extra", "side"))


def main_extra(zones):
    return Counter(zones["main"]) + Counter(zones["extra"])


def distance(a, b):
    """Card differences between two Main+Extra multisets, counted with multiplicity."""
    return sum(((a - b) + (b - a)).values())


def load_json(path):
    return json.loads(Path(path).read_text())
