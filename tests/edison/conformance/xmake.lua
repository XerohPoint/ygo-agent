-- Standalone project, so these fixtures stay out of the certification gate and its fingerprints.
-- Run these FROM THIS DIRECTORY with `-P .`. Without it, or with a relative path from
-- the repository root, xmake walks up to a parent .xmake and overwrites that checkout's
-- configuration (this happened during development; a git worktree is especially exposed).
--   cd tests/edison/conformance
--   xmake f -P . -o ../../../build/edison-conformance -y
--   xmake b -P . -j 3
add_rules("mode.release")
set_defaultmode("release")
add_repositories("my-repo ../../../repo")
add_requires("ygopro-core 0.0.2", {configs = {edison_early_trigger = true, edison_hardening = "r4"}})
add_requires("sqlite3 3.43.0+200", "sqlitecpp 3.2.1")

target("edison_conformance")
    set_kind("binary")
    set_languages("c++17")
    add_files("conformance.cpp")
    add_includedirs("../../../ygoenv")
    add_packages("ygopro-core", "sqlitecpp")
