#!/usr/bin/env python3
"""Verify assets, run isolated native fixtures twice, and save reproducible evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/linux/x86_64/release/edison_rules")
    parser.add_argument("--database", type=Path, default=ROOT / "assets/locale/en/cards.cdb")
    parser.add_argument("--scripts", type=Path, default=ROOT / "scripts/script")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--test", dest="test_id", metavar="NNN",
                        help="Run only this test number and its comparison controls")
    parser.add_argument("--fail-fast", action="store_true",
                        help="Stop after the first failed case; do not replay a failed run")
    args = parser.parse_args()
    if args.test_id is not None and (len(args.test_id) != 3 or
                                    any(c not in "0123456789" for c in args.test_id)):
        parser.error("--test requires a three-digit test number")
    if args.output is None:
        args.output = ROOT / "build/edison-certification"
        if args.test_id is not None:
            args.output /= f"test-{args.test_id}"
    args.output.mkdir(parents=True, exist_ok=True)
    report_path = args.output / "report.json"
    report = {"status": "FAIL", "seed": 20100424,
              "scope": "full_suite" if args.test_id is None else "selected_test",
              "test_id": args.test_id, "fail_fast": args.fail_fast}
    try:
        lock = json.loads((Path(__file__).with_name("assets.lock.json")).read_text())
        report["versions"] = lock
        from generate_legality import render, HEADER
        if HEADER.read_text() != render():
            raise RuntimeError("Generated legality header differs from reviewed manifest")
        for relative, expected in lock["legality_sha256"].items():
            if digest(ROOT / relative) != expected:
                raise RuntimeError(f"Legality metadata hash mismatch: {relative}")
        assets = {args.database.resolve(): lock["database_sha256"]}
        assets.update({(args.scripts / name).resolve(): value for name, value in lock["scripts_sha256"].items()})
        observed = {str(path): digest(path) for path in assets}
        report["asset_sha256"] = observed
        for path, expected in assets.items():
            if observed[str(path)] != expected:
                raise RuntimeError(f"Asset hash mismatch: {path}; inspect changes before updating the lock")
        recipe = ROOT / "repo/packages/y/ygopro-core/xmake.lua"
        pin = f'add_versions("{lock["core_version"]}", "{lock["core_commit"]}")'
        if pin not in recipe.read_text():
            raise RuntimeError("Core recipe no longer matches the recorded source pin")
        patch = ROOT / "repo/packages/y/ygopro-core/early-trigger.patch"
        report["core_patch_sha256"] = digest(patch)
        if report["core_patch_sha256"] != lock["core_patch_sha256"]:
            raise RuntimeError("Core patch hash differs from certification lock")
        report["core_recipe_sha256"] = digest(recipe)
        report["binary"] = str(args.binary.resolve())
        report["binary_sha256"] = digest(args.binary)
        sources = [ROOT / "xmake.lua", ROOT / "ygoenv/ygoenv/ygopro/ygopro.h",
                   ROOT / "ygoenv/ygoenv/ygopro/duel_config.h",
                   ROOT / "ygoenv/ygoenv/ygopro/observation_visibility.h",
                   ROOT / "ygoenv/ygoenv/ygopro/decision_context.h",
                   ROOT / "ygoenv/ygoenv/ygopro/historical_scripts.h",
                   ROOT / "ygoenv/ygoenv/ygopro/script_loader.h",
                   ROOT / "ygoenv/ygoenv/ygopro/deck_validation.h",
                   ROOT / "ygoenv/ygoenv/ygopro/deck_file.h",
                   ROOT / "ygoenv/ygoenv/ygopro/edison_legality_data.h"]
        sources += [ROOT / "ygoai/edison_policy.py"]
        sources += sorted(p for p in Path(__file__).parent.iterdir()
                          if p.suffix in (".h", ".cpp", ".py"))
        report["source_sha256"] = {str(path.relative_to(ROOT)): digest(path) for path in sources}
        command = [str(args.binary.resolve()), str(args.database.resolve()), str(args.scripts.resolve()), "--trace"]
        if args.test_id is not None:
            command.append(f"--test={args.test_id}")
        if args.fail_fast:
            command.append("--fail-fast")
        commands = [] if args.test_id in ("042", "077", "078", "079", "082") else [command]
        if args.test_id in (None, "042", "077", "078", "079", "082"):
            production = sorted((ROOT / "build/linux/x86_64/release").glob("ygopro_ygoenv*.so"))
            if len(production) != 1:
                raise RuntimeError("Build the production module before production certification")
            report["production_binary"] = str(production[0])
            report["production_binary_sha256"] = digest(production[0])
            report["token_resource_sha256"] = digest(ROOT / "assets/deck/unsupported/_tokens.ydk")
        if args.test_id in (None, "042"):
            probe = [sys.executable, str(Path(__file__).with_name("production_deck_tests.py"))]
            if args.fail_fast:
                probe.append("--fail-fast")
            commands.append(probe)
        if args.test_id in (None, "077"):
            smoke_lock = json.loads(Path(__file__).with_name("smoke_assets.lock.json").read_text())
            report["smoke_assets_sha256"] = smoke_lock
            for relative, expected in smoke_lock.items():
                if digest(ROOT / relative) != expected:
                    raise RuntimeError("Smoke asset hash mismatch: "+relative)
            probe = [sys.executable, str(Path(__file__).with_name("production_interaction_tests.py"))]
            if args.fail_fast:
                probe.append("--fail-fast")
            commands.append(probe)
        if args.test_id in (None, "078"):
            commands.append([sys.executable, str(Path(__file__).with_name("production_observation_tests.py"))])
        if args.test_id in (None, "079", "082"):
            agent_lock = json.loads(Path(__file__).with_name("agent_assets.lock.json").read_text())
            report["agent_assets_sha256"] = agent_lock
            for relative, expected in agent_lock.items():
                if digest(ROOT / relative) != expected:
                    raise RuntimeError("Agent asset hash mismatch: "+relative)
            if args.test_id in (None, "079"):
                commands.append([sys.executable, str(Path(__file__).with_name("agent_policy_tests.py"))])
            if args.test_id in (None, "082"):
                commands.append([sys.executable, str(Path(__file__).with_name("production_context_tests.py"))])
        report["command"] = commands[0]
        report["commands"] = commands
        outputs = []
        report["runs"] = []
        for iteration in (1, 2):
            output = ""
            returncode = 0
            for selected_command in commands:
                result = subprocess.run(selected_command, cwd=ROOT, text=True, capture_output=True, timeout=30)
                output += result.stdout + result.stderr
                returncode = returncode or result.returncode
                if args.fail_fast and result.returncode:
                    break
            trace_path = args.output / f"run-{iteration}.txt"
            trace_path.write_text(output)
            outputs.append(output)
            report["runs"].append({"returncode": returncode, "trace": str(trace_path.resolve()),
                                   "sha256": digest(trace_path)})
            if iteration == 1:
                print("\n".join(line for line in output.splitlines() if not line.startswith("TRACE ")))
            if args.fail_fast and returncode:
                report["stopped_on_failure"] = True
                report["deterministic_replay"] = None
                print("DETERMINISTIC REPLAY: SKIPPED (stop-on-failure requested)")
                raise RuntimeError(f"Stopped on failing certification run {iteration}; later cases and replay skipped")
        if outputs[0] != outputs[1]:
            raise RuntimeError("Repeated runs produced different event/response traces")
        report["deterministic_replay"] = True
        print("DETERMINISTIC REPLAY: PASS (two identical event/response traces)")
        failed = [i + 1 for i, run in enumerate(report["runs"]) if run["returncode"]]
        if failed:
            raise RuntimeError(f"Certification failed on runs {failed}; see saved traces")
        report["status"] = "PASS"
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"RESULT: FAIL — {error}", file=sys.stderr)
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Evidence: {report_path.resolve()}")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
