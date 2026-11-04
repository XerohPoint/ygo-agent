#!/usr/bin/env python3
"""Build an audit-only copy of the production env module with message visibility.

The production module never calls set_message_handler, so ygopro-core Lua errors are
discarded (default_message_handler returns 0). It also skips MSG_CHAINING unless
verbose. This tool derives a patched copy of ygopro.h under the build directory, and
compiles it to a separate .so. It does not touch ygoenv/ or the production binaries.

Patch content: (1) install a message handler, (2) observe each message before the
production handler consumes it. Neither changes a response or RNG draw; soak.py
--equivalence compares decision traces against the production .so.
"""
import argparse, hashlib, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "ygoenv/ygoenv/ygopro"
OUT = ROOT / "build/edison-pool-audit-2026-11-04/audit_module"
PK = Path.home() / ".xmake/packages"
CORE_CONFIG = "bedb42dd1c644c7482fd8520cd0c023e"  # edison_early_trigger = true
PACKAGES = {
    "pybind11": "p/pybind11/v2.13.6/8d7565588f4b45f7a0635954fd1f2205",
    "gflags": "g/gflags/v2.3.1/8338b247002044cbaa6bcca58a29a392",
    "fmt": "f/fmt/10.2.1/661aacf15d164223b9da55feb5616065",
    "glog": "g/glog/v0.6.0/98bb9424878c424382eb29e8237853f9",
    "concurrentqueue": "c/concurrentqueue/v1.0.4/40b39cfb949741cfbb04b0eff8ec6596",
    "unordered_dense": "u/unordered_dense/v4.4.0/658322d51189425c89b903e244f839f8",
    "sqlite3": "s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811",
    "sqlitecpp": "s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085",
    "lua": "l/lua/v5.5.1/3754d30faef64f908abcf74341f1054e",
    "core": f"y/ygopro-core/0.0.2/{CORE_CONFIG}",
}

AUDIT_HEADER = r'''
// ---- AUDIT ONLY: not production source ----
namespace pool_audit {
inline int fd() {
  static int value = [] {
    const char *path = std::getenv("EDISON_AUDIT_LOG");
    return path ? ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1;
  }();
  return value;
}
inline void line(const std::string &text) {
  if (fd() < 0) return;
  std::string out = text + "\n";
  (void)!::write(fd(), out.data(), out.size());
}
inline uint32_t handler(intptr_t pduel, uint32_t type) {
  char buffer[1024] = {};
  get_log_message(pduel, buffer);
  std::string text(buffer);
  for (auto &ch : text) if (ch == '\n' || ch == '\r') ch = ' ';
  line("L " + std::to_string(type) + " " + text);
  return 0;
}
inline void on_message(int msg, const unsigned char *data, int len) {
  const char *tag = nullptr;
  if (msg == 70) tag = "C";        // MSG_CHAINING
  else if (msg == 60) tag = "S";   // MSG_SUMMONING
  else if (msg == 62) tag = "P";   // MSG_SPSUMMONING
  else if (msg == 64) tag = "F";   // MSG_FLIPSUMMONING
  if (!tag || len < 4) return;
  uint32_t code;
  std::memcpy(&code, data, 4);
  line(std::string(tag) + " " + std::to_string(code));
}
}  // namespace pool_audit
// ---- END AUDIT ONLY ----
'''

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"Patch anchor must occur exactly once: {old!r} ({text.count(old)})")
    return text.replace(old, new)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    src_dir = OUT / "src/ygoenv/ygopro"
    if src_dir.exists(): shutil.rmtree(src_dir)
    src_dir.mkdir(parents=True)
    for path in SRC.glob("*.h"):
        shutil.copy(path, src_dir / path.name)
    shutil.copy(SRC / "ygopro.cpp", src_dir / "ygopro.cpp")
    text = (SRC / "ygopro.h").read_text()
    text = "#include <cstdlib>\n#include <fcntl.h>\n#include <unistd.h>\n" + text
    text = replace_once(text, "static ankerl::unordered_dense::map<CardCode, Card> cards_;",
                        AUDIT_HEADER + "\nstatic ankerl::unordered_dense::map<CardCode, Card> cards_;")
    text = replace_once(text, "  set_script_reader(script_reader_callback);\n}",
                        "  set_script_reader(script_reader_callback);\n"
                        "  set_message_handler(pool_audit::handler);\n}")
    text = replace_once(text,
        "    legal_actions_ = {};\n\n    if (verbose_) {\n      fmt::println(\"Message {}, length {}, dp {}\", msg_to_string(msg_), dl_, dp_);",
        "    legal_actions_ = {};\n    pool_audit::on_message(msg_, data_ + dp_, dl_ - dp_);\n\n"
        "    if (verbose_) {\n      fmt::println(\"Message {}, length {}, dp {}\", msg_to_string(msg_), dl_, dp_);")
    (src_dir / "ygopro.h").write_text(text)
    includes = [OUT / "src", ROOT / "ygoenv", "/usr/include/python3.10"]
    libs = []
    for key, rel in PACKAGES.items():
        base = PK / rel
        includes.append(base / "include")
        if key not in ("pybind11", "concurrentqueue", "unordered_dense"):
            libs.append(base / "lib")
    includes.append(PK / PACKAGES["lua"] / "include/lua")
    target = OUT / "ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
    cmd = ["g++", "-std=c++17", "-O2", "-fPIC", "-shared", "-fvisibility=hidden",
           "-march=native", "-DGLOG_NO_ABBREVIATED_SEVERITIES", "-DGOOGLE_GLOG_DLL_DECL=",
           "-DGFLAGS_DLL_DECLARE_FLAG=", "-DGFLAGS_DLL_DEFINE_FLAG=",
           *[f"-I{p}" for p in includes], str(src_dir / "ygopro.cpp"),
           "-o", str(target), *[f"-L{p}" for p in libs],
           "-lygopro-core", "-llua", "-lSQLiteCpp", "-lsqlite3", "-lfmt", "-lglog", "-lgflags", "-lpthread", "-ldl"]
    print(" ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)
    manifest = {"source_ygopro_h_sha256": hashlib.sha256((SRC / "ygopro.h").read_bytes()).hexdigest(),
                "patched_ygopro_h_sha256": hashlib.sha256(text.encode()).hexdigest()}
    (OUT / "manifest.txt").write_text("\n".join(f"{k} {v}" for k, v in manifest.items()) + "\n")
    print(target)

if __name__ == "__main__":
    main()
