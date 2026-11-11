#!/usr/bin/env python3
"""prep_core.py SRC DST [--trace]: copy patched ygopro-core sources for a hand build.

Applies the two header inserts the xmake recipe makes (field.h, interpreter.h). With --trace,
adds a report-only check at the turn-start delete in process_turn: every holder of an effect
pointer that still points at an effect about to be deleted prints one "@@stale" line on stderr.
"""
import pathlib
import shutil
import sys

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
trace = "--trace" in sys.argv[3:]
if dst.exists():
    shutil.rmtree(dst)
dst.mkdir(parents=True)
for f in src.iterdir():
    if f.is_file() and f.suffix in {".cpp", ".h"}:
        shutil.copy2(f, dst / f.name)


def check_and_insert(name, line, text):
    path = dst / name
    lines = path.read_text().split("\n")
    if lines[line - 1] != text:
        lines.insert(line - 1, text)
        path.write_text("\n".join(lines))


check_and_insert("field.h", 14, "#include <cstring>")
check_and_insert("interpreter.h", 11, 'extern "C" {')
check_and_insert("interpreter.h", 15, "}")

TRACE = r"""
		// TRACE (sanitizer study only): report every pointer that still names an effect deleted below.
		{
			auto dead = [&](effect* pe) { return pe && core.reseted_effects.count(pe); };
			auto rep = [&](const char* where, effect* pe, card* pc) {
				fprintf(stderr, "@@stale turn %d %s card %u loc 0x%x effect owner %u code %u type 0x%x\n", (int)infos.turn_id, where,
					pc ? pc->data.code : 0, pc ? pc->current.location : 0, pe->owner ? pe->owner->data.code : 0, pe->code, pe->type);
			};
			for(auto* pc : pduel->cards) {
				if(dead(pc->current.reason_effect)) rep("card_current", pc->current.reason_effect, pc);
				if(dead(pc->temp.reason_effect)) rep("card_temp", pc->temp.reason_effect, pc);
				if(dead(pc->previous.reason_effect)) rep("card_previous", pc->previous.reason_effect, pc);
				if(dead(pc->spsummon.reason_effect)) rep("card_spsummon", pc->spsummon.reason_effect, pc);
				for(auto& rel : pc->relate_effect)
					if(dead(rel.first)) rep(rel.second ? "card_relation_chain" : "card_relation", rel.first, pc);
			}
			const std::pair<const char*, const event_list*> events[] = {
				{"point_event", &core.point_event}, {"instant_event", &core.instant_event}, {"queue_event", &core.queue_event},
				{"delayed_activate_event", &core.delayed_activate_event}, {"full_event", &core.full_event},
				{"single_event", &core.single_event}, {"solving_event", &core.solving_event}, {"sub_solving_event", &core.sub_solving_event}};
			for(auto& el : events)
				for(auto& ev : *el.second)
					if(dead(ev.reason_effect)) rep(el.first, ev.reason_effect, ev.trigger_card);
			const std::pair<const char*, const chain_list*> chains[] = {
				{"continuous_chain", &core.continuous_chain}, {"solving_continuous", &core.solving_continuous},
				{"sub_solving_continuous", &core.sub_solving_continuous}, {"delayed_continuous_tp", &core.delayed_continuous_tp},
				{"delayed_continuous_ntp", &core.delayed_continuous_ntp}, {"desrep_chain", &core.desrep_chain},
				{"new_fchain", &core.new_fchain}, {"new_fchain_s", &core.new_fchain_s}, {"new_ochain", &core.new_ochain},
				{"new_ochain_s", &core.new_ochain_s}, {"new_fchain_b", &core.new_fchain_b}, {"new_ochain_b", &core.new_ochain_b},
				{"new_ochain_h", &core.new_ochain_h}, {"new_chains", &core.new_chains}};
			for(auto& cl : chains)
				for(auto& ch : *cl.second) {
					if(dead(ch.triggering_effect)) rep(cl.first, ch.triggering_effect, nullptr);
					if(dead(ch.evt.reason_effect)) rep(cl.first, ch.evt.reason_effect, nullptr);
				}
			for(auto& ch : core.current_chain) {
				if(dead(ch.triggering_effect)) rep("current_chain", ch.triggering_effect, nullptr);
				if(dead(ch.evt.reason_effect)) rep("current_chain_event", ch.evt.reason_effect, nullptr);
			}
			for(auto* dq : {&core.delayed_quick, &core.delayed_quick_tmp})
				for(auto& q : *dq) {
					if(dead(q.first)) rep("delayed_quick", q.first, nullptr);
					if(dead(q.second.reason_effect)) rep("delayed_quick_event", q.second.reason_effect, nullptr);
				}
			for(auto& q : core.quick_f_chain)
				if(dead(q.first)) rep("quick_f_chain", q.first, nullptr);
			if(dead(core.reason_effect)) rep("core_reason_effect", core.reason_effect, nullptr);
			for(auto& o : effects.oath)
				if(dead(o.second)) rep("oath_value", o.second, nullptr);
			for(auto& ge : effects.grant_effect)
				for(auto& g : ge.second)
					if(dead(g.second)) rep("grant_value", g.second, g.first);
			fprintf(stderr, "@@check turn %d deleted %zu\n", (int)infos.turn_id, core.reseted_effects.size());
		}
"""

if trace:
    path = dst / "processor.cpp"
    text = path.read_text()
    anchor = "\t\tfor(auto& peffect : core.reseted_effects) {\n\t\t\tpduel->delete_effect(peffect);\n"
    assert text.count(anchor) == 1, "turn-start delete not found"
    text = text.replace(anchor, TRACE + anchor)
    text = text.replace('#include "field.h"\n', '#include <cstdio>\n#include "field.h"\n', 1)
    path.write_text(text)
print(dst)
