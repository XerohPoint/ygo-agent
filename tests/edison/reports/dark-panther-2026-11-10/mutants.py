"""Build test 089 against eight one-edit mutants of the Dark Panther fix, and a baseline, and run each. Usage: mutants.py [OUT_DIR] [NAME ...].
The core, Lua and SQLite package paths are this machine's xmake packages."""
import os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
W = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../..'))
OUT = sys.argv.pop(1) if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]) else f'{W}/build/dark-panther-mutants'
P = '/home/mada/.xmake/packages'
R3 = f'{P}/y/ygopro-core/0.0.2/792b98d401b34303be27a588cc4c8318'
R2 = f'{P}/y/ygopro-core/0.0.2/5fe29f6ad6dd4ebb83a1f1b5123c1837'
LUA = f'{P}/l/lua/v5.5.1/3754d30faef64f908abcf74341f1054e'
SQLCPP = f'{P}/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085'
SQL = f'{P}/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811'
src = open(f'{W}/ygoenv/ygoenv/ygopro/historical_scripts.h').read()
start = src.index('  // Neo-Spacian Dark Panther is once per turn')
end = src.index('  if(name != "c26202165.lua") return {};')
block = src[start:end]
RC = 'RESET_EVENT+RESETS_STANDARD+RESET_CONTROL+RESET_PHASE+PHASE_END'
RN = 'RESET_EVENT+RESETS_STANDARD+RESET_PHASE+PHASE_END'
def sub(old, new, count=1):
    def f(b):
        assert b.count(old) == count, (old, b.count(old))
        return b.replace(old, new)
    return f
mutants = {
    'baseline': (lambda b: b, R3),
    'r2': (lambda b: b, R2),
    'noguard': (lambda b: '', R3),
    'noend': (sub('c:RegisterFlagEffect(43237273,RESET_EVENT+RESETS_STANDARD+RESET_PHASE+PHASE_END,0,1)',
                  'c:RegisterFlagEffect(43237273,RESET_EVENT+RESETS_STANDARD,0,1)'), R3),
    'nocontrol': (sub(RC, RN, 3), R3),
    'copyctl': (sub(f'cid=c:CopyEffect(code,{RC},1)', f'cid=c:CopyEffect(code,{RN},1)'), R3),
    'namectl': (sub(f'e1:SetReset({RC})\n\t\te1:SetCode(EFFECT_CHANGE_CODE)', f'e1:SetReset({RN})\n\t\te1:SetCode(EFFECT_CHANGE_CODE)'), R3),
    'cleanctl': (sub(f'e2:SetReset({RC})', f'e2:SetReset({RN})'), R3),
    'noctlcheck': (sub('c:IsFaceup() and c:IsControler(tp) and', 'c:IsFaceup() and'), R3),
}
def build(name):
    edit, core = mutants[name]
    d = f'{OUT}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(f'{d}/ygoenv/ygopro')
    for f in os.listdir(f'{W}/ygoenv/ygoenv/ygopro'):
        if f.endswith('.h'): shutil.copy(f'{W}/ygoenv/ygoenv/ygopro/{f}', f'{d}/ygoenv/ygopro/{f}')
    open(f'{d}/ygoenv/ygopro/historical_scripts.h', 'w').write(src[:start] + edit(block) + src[end:])
    cmd = ['g++', '-std=c++17', '-O1', '-w', f'-I{d}', f'-I{W}/ygoenv', f'-I{W}/tests/edison', f'-I{core}/include',
           f'-I{LUA}/include', f'-I{LUA}/include/lua', f'-I{SQLCPP}/include', f'-I{SQL}/include',
           f'{W}/tests/edison/rules.cpp', '-o', f'{d}/rules', f'{core}/lib/libygopro-core.a', f'{LUA}/lib/liblua.a',
           f'{SQLCPP}/lib/libSQLiteCpp.a', f'{SQL}/lib/libsqlite3.a', '-lpthread', '-ldl', '-lm']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: return name, 'BUILD FAILED\n' + r.stderr[-2000:]
    r = subprocess.run([f'{d}/rules', '/home/mada/ygo-agent/assets/locale/en/cards.cdb', '/home/mada/ygopro-scripts', '--test=089'],
                       capture_output=True, text=True, cwd=d)
    lines, cur = [], None
    for line in r.stdout.splitlines():
        if line.startswith(('EDISON RULE TEST 089', 'CONTROL 089')): cur = line
        elif line.startswith('RESULT:') and cur: lines.append(f'{cur}\t{line}'); cur = None
        elif line.startswith('SUMMARY'): lines.append(line)
    return name, '\n'.join(lines) + ('' if lines else r.stdout[-1500:] + r.stderr[-1500:])
names = sys.argv[1:] or list(mutants)
with ThreadPoolExecutor(6) as ex:
    for name, text in ex.map(build, names):
        print(f'== rules-{name}\n{text}')
