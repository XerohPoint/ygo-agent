#!/usr/bin/env python3
"""Scan committed corpus outputs for any player name, handle or id seen in the raw responses.

Collects every builder, winner, team and replay player field (names, user ids, Discord ids)
from build/edison-corpus/raw/, then searches the given files for each as a whole word,
ignoring case. Names that are also card or archetype words will match; each hit is printed
for review, and the script exits nonzero if any id-like string (10+ characters) matches.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    secrets = set()
    for path in RAW.rglob("*.json"):
        try:
            secrets |= walk(json.loads(path.read_text()))
        except ValueError:
            continue
    secrets = {s for s in secrets if len(s) >= 3}
    files = [p for root in args.paths for p in ([root] if root.is_file() else sorted(root.rglob("*"))) if p.is_file()]
    text = {p: p.read_text(errors="replace") for p in files}
    hits, hard = {}, 0
    for secret in sorted(secrets):
        pattern = re.compile(r"(?<![0-9A-Za-z])" + re.escape(secret) + r"(?![0-9A-Za-z])", re.IGNORECASE)
        found = [str(p.relative_to(ROOT)) for p, body in text.items() if pattern.search(body)]
        if found:
            hits[secret] = found[:5]
            hard += len(secret) >= 10 and any(ch.isdigit() for ch in secret)
    print(json.dumps({"strings_checked": len(secrets), "files": len(files), "hits": hits}, indent=1))
    return 1 if hard else 0


if __name__ == "__main__":
    raise SystemExit(main())
