#!/usr/bin/env python3
"""Generate legal Edison decks from the full pool. Production validation runs at init_module."""
import random, re
from collections import Counter, defaultdict
from common import *

POLY = 24094653

class Pool:
    def __init__(self):
        self.entries, self.by_id, _ = load_pool()
        self.blocked = set()
        self.playable = [e for e in self.entries if e["limit"] > 0]
        self.forbidden = [e for e in self.entries if e["limit"] == 0]
        self.main, self.extra = [], []
        for e in self.playable:
            t = e["rows"][0]["type"]
            (self.extra if t & (TYPE_FUSION | TYPE_SYNCHRO) else self.main).append(e)
        self.monsters = [e for e in self.main if e["rows"][0]["type"] & TYPE_MONSTER]
        self.spells = [e for e in self.main if e["rows"][0]["type"] & TYPE_SPELL]
        self.traps = [e for e in self.main if e["rows"][0]["type"] & TYPE_TRAP]
        self.tuners = [e for e in self.monsters if e["rows"][0]["type"] & TYPE_TUNER]
        self.themes = defaultdict(list)
        for e in self.playable:
            row = e["rows"][0]
            setcode = row["setcode"] or 0
            while setcode:
                self.themes[f"set{setcode & 0xfff:03x}"].append(e)
                setcode >>= 16
            if row["type"] & TYPE_MONSTER:
                self.themes[f"race{row['race']}"].append(e)
                self.themes[f"attr{row['attribute']}"].append(e)
        self.themes = {k: list({id(x): x for x in v}.values()) for k, v in self.themes.items() if len(v) >= 6}
        self.theme_keys = sorted(self.themes)
        self.mentions, self.mentioned_by = defaultdict(set), defaultdict(set)
        for e in self.entries:
            path = SCRIPTS / f"c{e['base']}.lua"
            if not path.exists(): continue
            for token in set(re.findall(r"\b(\d{5,9})\b", path.read_text(errors="replace"))):
                other = self.by_id.get(int(token))
                if other is not None and other is not e:
                    self.mentions[e["base"]].add(other["base"])
                    self.mentioned_by[other["base"]].add(e["base"])

class Builder:
    def __init__(self, pool, rng):
        self.pool, self.rng, self.count = pool, rng, Counter()
        self.allow = set()
        self.main, self.extra, self.side = [], [], []

    def add(self, entry, copies=1, zone=None):
        if entry["base"] in self.pool.blocked and entry["base"] not in self.allow: return 0
        zone = zone if zone is not None else (self.extra if entry in self.pool.extra else self.main)
        limit = 15 if zone is self.extra or zone is self.side else 60
        added = 0
        for _ in range(copies):
            if len(zone) >= limit or self.count[entry["copy_key"]] >= entry["limit"]: break
            ids = entry["ids"]
            code = self.rng.choice(ids) if len(ids) > 1 and self.rng.random() < 0.25 else entry["base"]
            zone.append(code); self.count[entry["copy_key"]] += 1; added += 1
        return added

    def fill(self, candidates, total, weights=None, copies=(1,)):
        if not candidates: return
        attempts = 0
        while len(self.main) < total and attempts < total * 30:
            attempts += 1
            entry = self.rng.choices(candidates, weights=weights)[0] if weights else self.rng.choice(candidates)
            self.add(entry, self.rng.choice(copies))

    def finish_extra(self, weight=None, size=None):
        size = self.rng.randint(5, 15) if size is None else size
        extra = self.pool.extra
        weights = [weight(e) for e in extra] if weight else None
        attempts = 0
        while len(self.extra) < size and attempts < 120:
            attempts += 1
            self.add(self.rng.choices(extra, weights=weights)[0] if weights else self.rng.choice(extra), 1, self.extra)

    def finish_side(self):
        for _ in range(self.rng.randint(0, 15)):
            self.add(self.rng.choice(self.pool.playable), 1, self.side)

    def result(self):
        if not 40 <= len(self.main) <= 60: raise ValueError("main size")
        return {"main": self.main, "extra": self.extra, "side": self.side}

def uniform_deck(pool, rng):
    b = Builder(pool, rng)
    b.fill(pool.monsters, 20); b.fill(pool.spells, 32); b.fill(pool.traps, 40)
    b.finish_extra(); b.finish_side()
    return b.result()

def themed_deck(pool, rng, weight=None):
    b = Builder(pool, rng)
    key = rng.choice(pool.theme_keys)
    theme = pool.themes[key]
    main_theme = [e for e in theme if e in pool.main or e not in pool.extra]
    b.fill(main_theme, rng.randint(14, 24), copies=(1, 2, 3))
    for e in theme:
        if e in pool.extra: b.add(e, 1, b.extra)
    wfun = weight or (lambda e: 1.0)
    monsters = len([c for c in b.main if pool.by_id[c]["rows"][0]["type"] & TYPE_MONSTER])
    if monsters < 16:
        b.fill(pool.monsters, len(b.main) + 16 - monsters, weights=[wfun(e) for e in pool.monsters])
    rest = pool.spells + pool.traps
    b.fill(rest, 40, weights=[wfun(e) for e in rest])
    b.fill(pool.monsters, 40)
    b.finish_extra(wfun); b.finish_side()
    return b.result(), key

def add_synchro_support(b, target, rng):
    pool, level = b.pool, target["rows"][0]["level"] & 0xff
    tuners = [e for e in pool.tuners if (e["rows"][0]["level"] & 0xff) < level]
    if not tuners: return
    for tuner in rng.sample(tuners, min(2, len(tuners))):
        b.add(tuner, 3)
        need = level - (tuner["rows"][0]["level"] & 0xff)
        fits = [e for e in pool.monsters if (e["rows"][0]["level"] & 0xff) == need and not e["rows"][0]["type"] & TYPE_TUNER]
        if fits: b.add(rng.choice(fits), 3)

def targeted_deck(pool, rng, targets, weight):
    """Up to several targets at maximum legal copies, plus cards their scripts name."""
    b = Builder(pool, rng)
    b.allow = {t["base"] for t in targets}
    for target in targets:
        kind = target["rows"][0]["type"]
        if target in pool.extra:
            b.add(target, 3, b.extra)
            if kind & TYPE_FUSION:
                b.add(pool.by_id[POLY], 3)
            else:
                add_synchro_support(b, target, rng)
        else:
            b.add(target, 3)
        related = pool.mentions[target["base"]] | pool.mentioned_by[target["base"]]
        for code in sorted(related)[:8]:
            other = pool.by_id[code]
            if other["limit"] > 0 and len(b.main) < 34:
                b.add(other, 3 if other not in pool.extra else 1)
        row = target["rows"][0]
        setcode = row["setcode"] or 0
        if setcode:
            theme = pool.themes.get(f"set{setcode & 0xfff:03x}", [])
            for other in rng.sample(theme, min(4, len(theme))):
                if len(b.main) < 34: b.add(other, 2 if other not in pool.extra else 1)
    monsters = len([c for c in b.main if pool.by_id[c]["rows"][0]["type"] & TYPE_MONSTER])
    low = [e for e in pool.monsters if (e["rows"][0]["level"] & 0xff) <= 4 and e["rows"][0]["atk"] >= 0]
    if monsters < 16:
        b.fill(low, len(b.main) + 16 - monsters, weights=[weight(e) for e in low])
    rest = pool.main
    b.fill(rest, 40, weights=[weight(e) for e in rest])
    b.finish_extra(weight); b.finish_side()
    return b.result()

def write_ydk(deck, path):
    lines = ["#main", *map(str, deck["main"]), "#extra", *map(str, deck["extra"]), "!side", *map(str, deck["side"])]
    path.write_text("\n".join(lines) + "\n")
