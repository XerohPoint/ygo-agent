#!/usr/bin/env python3
"""Test production init_module in fresh processes, without creating a training pool."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def worker(case):
    database = ROOT / "assets/locale/en/cards.cdb"
    db = sqlite3.connect(database)
    manifest = json.loads((ROOT / "assets/edison/legality.json").read_text())
    pool = {code for card in manifest["cards"] for code in card["accepted_ids"]}
    limited = {card["code"] for card in manifest["restrictions"]}
    base = [row[0] for row in db.execute(
        "SELECT id FROM datas WHERE type=17 AND alias=0 AND (ot&2)!=0 ORDER BY id")
        if row[0] in pool and row[0] not in limited][:40]
    assert len(base) == 40
    main, extra, side = base.copy(), [], []
    if case == 2:
        main[0] = 23434538
    elif case == 3:
        main[:2] = [26202165, 26202165]
    elif case == 4:
        extra = [84013237]
    elif case == 5:
        side = [55144522]
    elif case == 9:
        main[0] = 2819435
    with tempfile.TemporaryDirectory(prefix="edison-init-") as directory:
        directory = Path(directory)
        (directory / "script").symlink_to((ROOT / "scripts/script").resolve(), target_is_directory=True)
        newline = "\r\n" if case == 6 else "\n"
        content = "#main\n" + "\n".join(map(str, main)) + "\n#extra\n"
        content += "\n".join(map(str, extra)) + "\n!side\n" + "\n".join(map(str, side)) + "\n"
        deck_path = directory / "fixture.ydk"
        deck_path.write_bytes(content.replace("\n", newline).encode())
        codes = sorted(set(base + main + extra + side))
        code_path = directory / "codes.txt"
        # This is an ingestion test: no duel or card effect is executed.
        code_path.write_text("".join(f"{code} 0\n" for code in codes))
        decks = {"fixture": str(deck_path)}
        if case in (1, 8):
            decks["_tokens"] = str(ROOT / "assets/deck/unsupported/_tokens.ydk")
        if case == 7:
            decks = {"": str(deck_path)}
        if case == 8:
            del decks["fixture"]
        binary = next((ROOT / "build/linux/x86_64/release").glob("ygopro_ygoenv*.so"))
        spec = importlib.util.spec_from_file_location("ygopro_ygoenv", binary)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        os.chdir(directory)
        try:
            module.init_module(str(database), str(code_path), decks)
        except RuntimeError as error:
            message = str(error).split(", file:")[0]
            print(json.dumps({"accepted": False, "error": message}))
        else:
            print(json.dumps({"accepted": True, "error": ""}))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=int)
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()
    if args.worker is not None:
        worker(args.worker)
        return 0
    expected = [
        None, None, "Outside Edison card pool", "Combined Main/Extra/Side",
        "Invalid Extra Deck", "Combined Main/Extra/Side", None,
        "Deck name cannot be empty", "No playable decks", "Outside Edison card pool"]
    descriptions = ["legal deck", "legal deck plus reserved token resource", "modern Main Deck card",
                    "Sangan across copy limit", "Xyz in Extra Deck", "forbidden Side Deck card",
                    "Windows line endings", "empty deck name", "resource-only input",
                    "modern card with historical permanent name"]
    passed = failed = 0
    for case, reason in enumerate(expected):
        print(f"\nEDISON RULE TEST 042{chr(65+case)} - PRODUCTION DECK INGESTION")
        print(f"Expected: {descriptions[case]}: {'accept' if reason is None else 'reject'}.")
        result = subprocess.run([sys.executable, __file__, "--worker", str(case)],
                                capture_output=True, text=True, timeout=20)
        try:
            if result.returncode:
                raise RuntimeError(f"production worker exited {result.returncode}: {result.stderr.strip()}")
            observed = json.loads(result.stdout.strip())
            if observed["accepted"] != (reason is None):
                raise RuntimeError(f"unexpected ingestion result: {observed}")
            if reason is not None and not observed["error"].startswith(reason):
                raise RuntimeError(f"wrong rejection: {observed['error']}")
            print("Observed: " + ("accepted" if observed["accepted"] else observed["error"]))
            print("RESULT: PASS")
            passed += 1
        except (ValueError, RuntimeError) as error:
            print(f"RESULT: FAIL — {error}")
            failed += 1
            if args.fail_fast:
                break
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)

if __name__ == "__main__":
    raise SystemExit(main())
