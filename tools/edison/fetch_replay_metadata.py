#!/usr/bin/env python3
"""Summarize public Format Library Edison replay metadata by archetype and matchup.

Reads only the public listing: no replay contents and no Dueling Book requests.
Player names and identifiers are dropped before anything is written.
"""
import argparse
import collections
import datetime
import json
import math
import time
import urllib.parse
import urllib.request
from pathlib import Path

API = "https://formatlibrary.com/api/replays"
COUNT_API = API + "/count"
FILTER = "formatName:eq:Edison"
PAGE_SIZE = 100
KEEP = ("id", "url", "winningDeckTypeName", "losingDeckTypeName", "eventAbbreviation",
        "eventId", "roundName", "roundAbs", "publishDate")


def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def fetch(delay):
    query = urllib.parse.urlencode({"filter": FILTER})
    expected = get_json(f"{COUNT_API}?{query}")
    replays, page = {}, 1
    while len(replays) < expected:
        rows = get_json(f"{API}?{query}&limit={PAGE_SIZE}&page={page}")
        if not rows:
            break
        for row in rows:
            replays[row["id"]] = {key: row.get(key) for key in KEEP}
        page += 1
        time.sleep(delay)
    return expected, sorted(replays.values(), key=lambda row: row["id"])


def wilson(wins, games, z=1.96):
    if not games:
        return None
    rate = wins / games
    denominator = 1 + z * z / games
    center = rate + z * z / (2 * games)
    margin = z * math.sqrt(rate * (1 - rate) / games + z * z / (4 * games * games))
    return [round((center - margin) / denominator, 4), round((center + margin) / denominator, 4)]


def summarize(replays):
    wins, losses, pairs = collections.Counter(), collections.Counter(), collections.Counter()
    unlabeled = mirrors = 0
    for row in replays:
        winner, loser = row["winningDeckTypeName"], row["losingDeckTypeName"]
        if not winner or not loser:
            unlabeled += 1
            continue
        wins[winner] += 1
        losses[loser] += 1
        if winner == loser:
            mirrors += 1
        else:
            pairs[winner, loser] += 1
    labeled = sum(wins.values())
    archetypes = []
    for name in sorted(wins | losses, key=lambda deck: -(wins[deck] + losses[deck])):
        games = wins[name] + losses[name]
        archetypes.append({"archetype": name, "replay_appearances": games, "wins": wins[name],
                           "losses": losses[name], "win_rate": round(wins[name] / games, 4),
                           "win_rate_wilson95": wilson(wins[name], games),
                           "appearance_share": round(games / (2 * labeled), 4)})
    matchups, seen = [], set()
    for first, second in pairs:
        key = tuple(sorted((first, second)))
        if key in seen:
            continue
        seen.add(key)
        a_wins, b_wins = pairs[key], pairs[key[1], key[0]]
        games = a_wins + b_wins
        matchups.append({"a": key[0], "b": key[1], "games": games, "a_wins": a_wins,
                         "a_win_rate": round(a_wins / games, 4),
                         "a_win_rate_wilson95": wilson(a_wins, games)})
    matchups.sort(key=lambda row: (-row["games"], row["a"], row["b"]))
    dates = sorted(row["publishDate"] for row in replays if row["publishDate"])
    return {"replays": len(replays), "labeled": labeled, "unlabeled": unlabeled, "mirrors": mirrors,
            "publish_date_min": dates[0] if dates else None,
            "publish_date_max": dates[-1] if dates else None,
            "events": len({row["eventId"] for row in replays if row["eventId"]}),
            "archetypes": archetypes, "matchups": matchups}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--delay", type=float, default=2.0, help="Seconds between page requests")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    expected, replays = fetch(args.delay)
    summary = summarize(replays)
    summary.update(source=f"{API}?filter={FILTER}", expected_count=expected,
                   retrieved_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   caveat="Published replays are a selected sample of event games, not all games; "
                          "game-level results share players and events, so intervals are descriptive.")
    (args.output / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    (args.output / "replays.json").write_text(json.dumps(replays, indent=0) + "\n")
    print(f"fetched {len(replays)}/{expected}; labeled {summary['labeled']}; "
          f"unlabeled {summary['unlabeled']}; mirrors {summary['mirrors']}; "
          f"events {summary['events']}; {summary['publish_date_min']} .. {summary['publish_date_max']}")
    for row in summary["archetypes"][:20]:
        low, high = row["win_rate_wilson95"]
        print(f"{row['archetype']:28} {row['replay_appearances']:5} share {row['appearance_share']:.1%} "
              f"win {row['win_rate']:.1%} [{low:.0%},{high:.0%}]")
    print("Most-played non-mirror matchups:")
    for row in summary["matchups"][:12]:
        print(f"{row['a']:22} vs {row['b']:22} {row['a_wins']:3}-{row['games'] - row['a_wins']:<3}")


if __name__ == "__main__":
    main()
