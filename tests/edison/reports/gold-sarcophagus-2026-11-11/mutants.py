#!/usr/bin/env python3
"""mutants5.py: build the rules binary against mutants of the core fix and run tests 052 and 090.

Each mutant edits files of core-r4b (plain build), recompiles that object only, relinks libcore.a from
out-plain-r4b's other objects, and builds edison_rules from this worktree. Output: mutants5/<name>/.
With --full, also runs the whole suite on each mutant. Results go to mutants5.txt.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

W = Path(__file__).resolve().parents[4]
S = W / "build/sarc"

FIX = """		if(!core.reseted_effects.empty()) {
			for(auto& pcard : pduel->cards) {
				for(card_state* pstate : {&pcard->current, &pcard->temp, &pcard->previous, &pcard->spsummon}) {
					if(pstate->reason_effect && core.reseted_effects.count(pstate->reason_effect)) {
						// ReturnToField checks which card banished it temporarily, so keep that card.
						if(pstate == &pcard->current && (pcard->current.reason & REASON_TEMPORARY))
							pcard->temp_remove_owner = pstate->reason_effect->owner;
						pstate->reason_effect = nullptr;
					}
				}
			}
		}
"""
DELETE = """		for(auto& peffect : core.reseted_effects) {
			pduel->delete_effect(peffect);
		}
		core.reseted_effects.clear();
"""
COND = "if(pstate->reason_effect && core.reseted_effects.count(pstate->reason_effect)) {"
LOOP = "			for(auto& pcard : pduel->cards) {\n"
SAVE = """						// ReturnToField checks which card banished it temporarily, so keep that card.
						if(pstate == &pcard->current && (pcard->current.reason & REASON_TEMPORARY))
							pcard->temp_remove_owner = pstate->reason_effect->owner;
"""
RETURN_NEW = """		if((ret == RETURN_TEMP_REMOVE_TO_FIELD) && (!(target->current.reason & REASON_TEMPORARY)
		        || ((target->current.reason_effect ? target->current.reason_effect->owner : target->temp_remove_owner) != core.reason_effect->owner)))"""
RETURN_OLD = """		if((ret == RETURN_TEMP_REMOVE_TO_FIELD) && (!(target->current.reason & REASON_TEMPORARY) || (target->current.reason_effect->owner != core.reason_effect->owner)))"""
RETURN_GUARD = """		if((ret == RETURN_TEMP_REMOVE_TO_FIELD) && (!(target->current.reason & REASON_TEMPORARY) || (target->current.reason_effect && target->current.reason_effect->owner != core.reason_effect->owner)))"""


def sub(old, new):
    def edit(t):
        assert t.count(old) == 1, old[:60]
        return t.replace(old, new)
    return edit


def no_fix(t):
    return sub(FIX, "")(t)


def states(names):
    return sub("{&pcard->current, &pcard->temp, &pcard->previous, &pcard->spsummon}", "{" + ", ".join(names) + "}")


def after_delete(t):
    # Clear after the set is emptied: the pointers are compared too late, so nothing is cleared.
    return sub(DELETE, DELETE + FIX)(sub(FIX, "")(t))


def cond(extra):
    return sub(COND, "if(" + extra + ") {")


def skip_cards(test):
    return sub(LOOP, LOOP + "\t\t\t\tif(" + test + ") continue;\n")


def at_reset(name):
    # Clear when the effect is reset instead of when it is deleted: the card loses a live reason effect early.
    def edit(t):
        anchor = "core.reseted_effects.insert(peffect);" if name == "field.cpp" else "pduel->game_field->core.reseted_effects.insert(peffect);\n}"
        clear = ("for(auto& pc : pduel->cards) for(card_state* ps : {&pc->current, &pc->temp, &pc->previous, &pc->spsummon}) "
                 "if(ps->reason_effect == peffect) ps->reason_effect = nullptr;\n\t\t")
        assert t.count(anchor) == 1, (name, anchor)
        if name == "field.cpp":
            return t.replace(anchor, clear + anchor)
        return t.replace(anchor, clear.replace("\t\t", "\t") + anchor)
    return edit


IN_SET = "core.reseted_effects.count(pstate->reason_effect)"
MUTANTS = [
    ("no_fix", [("processor.cpp", no_fix)]),
    ("r4_no_owner_kept", [("processor.cpp", sub(SAVE, "")), ("operations.cpp", sub(RETURN_NEW, RETURN_OLD))]),
    ("owner_not_kept", [("processor.cpp", sub(SAVE, ""))]),
    ("return_reads_reason_effect", [("operations.cpp", sub(RETURN_NEW, RETURN_OLD))]),
    ("return_null_guard_only", [("processor.cpp", sub(SAVE, "")), ("operations.cpp", sub(RETURN_NEW, RETURN_GUARD))]),
    ("current_only", [("processor.cpp", states(["&pcard->current"]))]),
    ("current_temp", [("processor.cpp", states(["&pcard->current", "&pcard->temp"]))]),
    ("no_spsummon", [("processor.cpp", states(["&pcard->current", "&pcard->temp", "&pcard->previous"]))]),
    ("after_delete", [("processor.cpp", after_delete)]),
    ("clear_all", [("processor.cpp", cond("pstate->reason_effect"))]),
    ("clear_current_always", [("processor.cpp", cond("pstate->reason_effect && (pstate == &pcard->current || " + IN_SET + ")"))]),
    ("clear_spsummon_always", [("processor.cpp", cond("pstate->reason_effect && (pstate == &pcard->spsummon || " + IN_SET + ")"))]),
    ("phase_reset_only", [("processor.cpp", cond("pstate->reason_effect && " + IN_SET + " && (pstate->reason_effect->reset_flag & RESET_PHASE)"))]),
    ("field_type_only", [("processor.cpp", cond("pstate->reason_effect && " + IN_SET + " && (pstate->reason_effect->type & EFFECT_TYPE_FIELD)"))]),
    ("owner0_only", [("processor.cpp", skip_cards("pcard->owner != 0"))]),
    ("ended_turn_owner_only", [("processor.cpp", skip_cards("pcard->owner == turn_player"))]),
    ("at_reset", [("processor.cpp", no_fix), ("card.cpp", at_reset("card.cpp")), ("field.cpp", at_reset("field.cpp"))]),
]

PK = "/home/mada/.xmake/packages"
LUA = S / "lua-5.5.1/src"


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def build(name, edits):
    out = S / "mutants5" / name
    core = out / "core"
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(S / "core-r4b", core)
    objs = {p.name: p for p in (S / "out-plain-r4b/obj/core").glob("*.o")}
    for f, edit in edits:
        text = (core / f).read_text()
        new = edit(text)
        assert new != text, (name, f)
        (core / f).write_text(new)
    (out / "obj").mkdir()
    for f, _ in edits:
        o = out / "obj" / (Path(f).stem + ".o")
        run(["nice", "-n", "19", "g++", "-std=c++17", "-g", "-O1", "-fno-omit-frame-pointer", "-fPIC", f"-I{LUA}", "-c", str(core / f), "-o", str(o)])
        objs[o.name] = o
    lib = out / "libcore.a"
    lua_objs = sorted((S / "out-plain-r4b/obj/lua").glob("*.o"))
    run(["ar", "rcs", str(lib)] + [str(p) for p in objs.values()] + [str(p) for p in lua_objs])
    inc = out / "include"
    inc.mkdir()
    os.symlink(core, inc / "ygopro-core")
    run(["nice", "-n", "19", "g++", "-std=c++17", "-g", "-O1", f"-I{W}/ygoenv", f"-I{inc}", f"-I{LUA}",
         f"-I{PK}/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085/include",
         f"-I{PK}/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811/include",
         str(W / "tests/edison/rules.cpp"), "-o", str(out / "edison_rules"), str(lib),
         f"-L{PK}/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085/lib", "-lSQLiteCpp",
         f"-L{PK}/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811/lib", "-lsqlite3", "-lpthread", "-ldl", "-lm"],
        stderr=subprocess.DEVNULL)
    return out / "edison_rules"


def results(binary, extra):
    # Unbuffered, so a crash still shows the case it happened in.
    proc = subprocess.run(["stdbuf", "-o0", str(binary), str(W / "assets/locale/en/cards.cdb"), str(W / "scripts/script")] + extra,
                          capture_output=True, text=True)
    rows, name = [], None
    for line in proc.stdout.splitlines():
        if re.match(r"^(EDISON RULE TEST|CONTROL) ", line):
            name = line.split(" - ")[0]
        elif line.startswith("RESULT: FAIL") and name:
            rows.append(f"{name}: {line[len('RESULT: FAIL — '):]}")
    summary = [l for l in proc.stdout.splitlines() if l.startswith("SUMMARY")]
    if proc.returncode < 0 or proc.returncode >= 128:
        return rows, f"crashed with signal {-proc.returncode if proc.returncode < 0 else proc.returncode - 128} in {name}"
    return rows, summary[-1] if summary else proc.stderr.strip()[-200:]


def main():
    full = "--full" in sys.argv
    lines = []
    for name, edits in MUTANTS:
        binary = build(name, edits)
        lines.append(f"== {name}")
        runs = [["--test=090"], ["--test=052"]] + ([[]] if full else [])
        for args in runs:
            fails, summary = results(binary, args)
            lines.append(f"  {' '.join(args) or 'full suite'}: {summary}")
            lines.extend("    " + f for f in fails)
        print(f"{name} done", flush=True)
    (S / "mutants5.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
