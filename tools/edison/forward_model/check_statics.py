#!/usr/bin/env python3
"""Map the static data a playout changes to its symbols, and fail on any that could carry duel state.

`fm_bench statics` runs playouts after a restore and prints each byte range of the program's .data and
.bss that changed. The arena covers the heap, so only a static could carry duel state past a restore.
Allowed: the test bed's own scratch and counters, and C library stream state.
"""
import argparse
import bisect
import json
import re
import subprocess
import sys

ALLOWED = (
    "fm::g_query_buf",    # scratch for the privileged deep hash
    "fm::g_diagnostics",  # counts failures on purpose
    "fm::g_active",       # the arena switch
    "fm::Sha256",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("report")
    args = parser.parse_args()
    symbols = []
    for line in subprocess.run(["nm", "-C", "-n", args.binary], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[1] in "bBdDsSuV":
            symbols.append((int(parts[0], 16), parts[2]))
    start = next(address for address, name in symbols if name == "__data_start")
    addresses = [address for address, _ in symbols]
    changed = {}
    for line in open(args.report):
        match = re.match(r"changed (\d+) size (\d+)", line)
        if not match:
            continue
        offset, size = int(match.group(1)), int(match.group(2))
        index = bisect.bisect_right(addresses, start + offset) - 1
        changed[symbols[index][1]] = changed.get(symbols[index][1], 0) + size
    bad = {name: size for name, size in changed.items() if not name.startswith(ALLOWED)}
    print(json.dumps({"statics_changed": changed, "not_allowed": bad}))
    return 1 if bad or not changed else 0


if __name__ == "__main__":
    sys.exit(main())
