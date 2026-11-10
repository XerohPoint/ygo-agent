#!/usr/bin/env python3
"""Scan committed corpus outputs for any player name, handle or id seen in the raw responses.

Collects every builder, winner, team and replay player field (names, user ids, Discord ids)
from the raw responses, then searches the given files for each as a whole word, ignoring case.
It fails if the raw cache is missing or yields nothing, or on any hit outside ALLOWED: words that
are also card, archetype or community names, each reviewed by hand.

Strings shorter than 3 characters are not searched, and numeric ids are not searched as text,
because they collide with passcodes and deck ids. Instead, every committed JSON file is checked
structurally: no key may name a person field.
"""
import argparse, json, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from corpus_common import RAW, ROOT  # noqa: E402

FIELDS = {"builderName", "builderId", "winnerName", "winnerId", "winningTeamName", "winningTeamId",
          "discordId", "discordPfp", "winnerId", "loserId", "name", "id", "pfp"}
PERSON = {"builder", "winner", "loser", "winningTeam", "player", "user"}


def walk(value, inside=False, out=None):
    """Strings under person-like objects, plus *Name/*Id fields naming a person."""
    out = set() if out is None else out
    if isinstance(value, dict):
        for key, item in value.items():
            person = inside or key in PERSON
            if isinstance(item, (dict, list)):
                walk(item, person, out)
            elif isinstance(item, str) and item.strip():
                if key in ("builderName", "builderId", "winnerName", "winnerId", "loserId", "winningTeamName",
                           "winningTeamId", "discordId", "discordPfp") or (person and key in FIELDS):
                    out.add(item.strip())
    elif isinstance(value, list):
        for item in value:
            walk(item, inside, out)
    return out


ALLOWED = {"jinzo", "rose", "soul", "beastmode"}  # card, archetype and community words; reviewed November 9, 2026


def person_keys(value, path="", out=None):
    """JSON keys that would hold a person's name or id."""
    out = [] if out is None else out
    if isinstance(value, dict):
        for key, item in value.items():
            if key in FIELDS - {"name", "id"} or key in PERSON:
                out.append(f"{path}/{key}")
            person_keys(item, f"{path}/{key}", out)
    elif isinstance(value, list):
        for item in value:
            person_keys(item, path, out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+")
    parser.add_argument("--raw", type=Path, default=RAW)
    args = parser.parse_args()
    raw = sorted(args.raw.rglob("*.json")) if args.raw.is_dir() else []
    if not raw:
        print(f"no raw responses under {args.raw}")
        return 1
    secrets = set()
    for path in raw:
        try:
            secrets |= walk(json.loads(path.read_text()))
        except ValueError:
            continue
    secrets = {s for s in secrets if len(s) >= 3}
    files = [p.resolve() for root in args.paths for p in ([root] if root.is_file() else sorted(root.rglob("*"))) if p.is_file() and "__pycache__" not in p.parts]
    text = {p: p.read_text(errors="replace") for p in files}
    if not secrets:
        print("the raw responses yielded no name or id to search for")
        return 1
    hits, unreviewed = {}, []
    for secret in sorted(secrets):
        pattern = re.compile(r"(?<![0-9A-Za-z])" + re.escape(secret) + r"(?![0-9A-Za-z])", re.IGNORECASE)
        found = [str(p.relative_to(ROOT)) for p, body in text.items() if pattern.search(body)]
        if found:
            hits[secret] = found[:5]
            if secret.casefold() not in ALLOWED:
                unreviewed.append(secret)
    keys = {}
    for p, body in text.items():
        if p.suffix == ".json":
            found = person_keys(json.loads(body))
            if found:
                keys[str(p.relative_to(ROOT))] = found[:5]
    print(json.dumps({"raw_files": len(raw), "strings_checked": len(secrets), "files": len(files), "json_files": sum(p.suffix == ".json" for p in files),
                      "hits": hits, "unreviewed_hits": len(unreviewed), "person_keys": keys}, indent=1))
    return 1 if unreviewed or keys else 0


if __name__ == "__main__":
    raise SystemExit(main())
