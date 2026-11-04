#!/usr/bin/env python3
"""Block until files contain a target line or a deadline passes. Prints the last line of each."""
import argparse
from pathlib import Path
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("needle")
parser.add_argument("files", nargs="+", type=Path)
parser.add_argument("--seconds", type=float, default=570)
args = parser.parse_args()
deadline = time.monotonic() + args.seconds


def done(path):
    if not path.exists():
        return False
    text = path.read_text(errors="replace")
    return args.needle in text or "Traceback" in text


while time.monotonic() < deadline and not all(done(p) for p in args.files):
    time.sleep(5)
for path in args.files:
    lines = path.read_text(errors="replace").strip().splitlines() if path.exists() else ["(missing)"]
    print(path.name, "|", lines[-1][:170])
