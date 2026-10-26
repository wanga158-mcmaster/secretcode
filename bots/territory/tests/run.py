"""Focused tests of the territory suite; never launches a game or the cambc CLI.

    python bots/territory/tests/run.py [bot ...]      (bot = concentric or territory/concentric)

Cases, one output line each:
  layout         suite self-containment (every quoted include resolves inside the suite or the SDK),
                 the Design enum <-> bot directory mapping and the runtime's controller dispatch
  wiring <bot>   entrypoint, native profile, legal five-pick draft, kit roles, choose_NN dispatch and a
                 build closure that stays inside the suite
  fixture <name> WASI fixtures of the shared modules (tools/arena/wasi.py): live-a = doctrines_a.h
                 (choose_01..08), live-b = doctrines_b.h (choose_09..16), native-nav = native_nav.h,
                 recovery = bounded escape envelopes/occupancy/cutoff; competitive = competitive.h
                 arbitration with both doctrine headers;
                 population = Islander controller/arbitration/refusal plus native shortlist obstruction;
                 partial-admission-off/on = production eligibility and discovery, certificates and fallback
Named bots restrict the wiring cases to those bots and the doctrine fixtures to the header holding
their controllers; layout, native-nav and competitive always run (every bot uses those modules).
Exit status: 0 all passed, 1 any failure, 2 usage error.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
COMMON = SUITE / "common" / "src"
SDK = ROOT / "sdk"
sys.dont_write_bytecode = True  # no __pycache__ in the toolkit when run without -B
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CACHE, Bot, Slots, closure  # noqa: E402
from wasi import run_case  # noqa: E402

MOBILE = {"vendetta", "ren", "vesper_vale", "heart_of_ruin"}  # kits with a native dash or teleport
# Draft ordinals that the controllers address by role (core picks play in three-actor games).
KIT_ROLES = {"sanctuary": {1: "willow"}, "causeway": {2: "marcella"}}
FIXTURES = (  # case, source, controller numbers covered (None: every bot)
    ("live-a", "plan7-live-a.cpp", range(1, 9)),
    ("live-b", "plan7-live-b.cpp", range(9, 17)),
    ("native-nav", "plan7-native-nav.cpp", None),
    ("recovery", "plan7-recovery.cpp", None),
    ("competitive", "plan7-competitive.cpp", None),
    ("population", "plan7-population.cpp", None),
    ("partial-admission-off", "plan7-partial-admission-off.cpp", None),
    ("partial-admission-on", "plan7-partial-admission-on.cpp", None),
)
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inc"}
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.M)
LOG_LINES = 12


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def enum_members(path: Path, name: str) -> list[str]:
    block = re.search(rf"enum class {name}\s*:\s*(?:std::)?[a-z0-9_]+\s*\{{(.*?)\}};", text(path), re.S)
    if not block:
        raise ValueError(f"no enum {name} in {path.relative_to(ROOT).as_posix()}")
    clean = re.sub(r"//[^\n]*", "", block.group(1))
    return [e.strip().split("=")[0].strip() for e in clean.split(",") if e.strip()]


def designs() -> list[str]:
    """Design enum members in order; member k (1-based) is dispatched to choose_kk."""
    return enum_members(COMMON / "native_types.h", "Design")


def slug(design: str) -> str:
    return design.replace("_", "-")


def bot_dirs() -> list[str]:
    return sorted(p.parent.parent.name for p in SUITE.glob("*/src/main.cpp"))


def check_layout() -> list[str]:
    problems = []
    for f in sorted(SUITE.rglob("*")):
        if not f.is_file() or f.suffix not in SOURCE_SUFFIXES or ".cambc" in f.parts:
            continue
        for inc in INCLUDE.findall(text(f)):
            target = (f.parent / inc).resolve()
            local = target.is_file() and target.is_relative_to(SUITE)
            sdk = (SDK / "cpp" / inc).is_file()
            if not (local or sdk):
                problems.append(f"{f.relative_to(SUITE).as_posix()} includes {inc!r} outside the suite")
    names = designs()
    if len(names) != 16:
        problems.append(f"expected 16 designs, found {len(names)}")
    expected, found = {slug(d) for d in names}, set(bot_dirs())
    if expected != found:
        problems.append(f"design/bot mismatch: no bot for {sorted(expected - found)}, "
                        f"no design for {sorted(found - expected)}")
    runtime = text(COMMON / "runtime.h")
    for header in ("competitive.h", "doctrines_a.h", "doctrines_b.h", "native_nav.h"):
        if f'#include "{header}"' not in runtime:
            problems.append(f"runtime.h does not include {header}")
    dispatch = dict(re.findall(r"case\s+Design::(\w+):\s*return\s+choose_(\d\d)\(f,\s*s\);", runtime))
    if sorted(dispatch.values()) != [f"{n:02d}" for n in range(1, len(names) + 1)]:
        problems.append(f"runtime dispatch is not one controller per design: {sorted(dispatch.values())}")
    return problems


def check_bot(name: str, legal: set[str]) -> list[str]:
    problems: list[str] = []
    names = designs()
    design = name.replace("-", "_")
    if design not in names:
        return [f"no Design::{design} in native_types.h"]
    number = names.index(design) + 1
    src = SUITE / name / "src"
    main, profile = text(src / "main.cpp"), text(src / "native_profile.h")
    for token in ('#include "native_profile.h"', '#include "../../common/src/runtime.h"'):
        if token not in main:
            problems.append(f"main.cpp lacks {token}")
    if not re.search(r"#ifndef P7_CONTROL\s*#define P7_CONTROL 0\s*#endif", main):
        problems.append("main.cpp lacks the active default P7_CONTROL 0")
    if not re.search(r"int main\(\)\s*\{\s*return p7::live::run\(p7::profile::design,\s*P7_CONTROL != 0,"
                     r"\s*p7::profile::draft_order\);\s*\}", main):
        problems.append("main() does not call p7::live::run(design, P7_CONTROL != 0, draft_order)")
    if '#include "../../common/src/native_types.h"' not in profile:
        problems.append("native_profile.h lacks the common native_types.h include")
    if not re.search(rf"design\s*=\s*live::Design::{design}\s*;", profile):
        problems.append(f"native_profile.h does not select Design::{design}")
    for field in ("name", "role_summary"):
        if not re.search(rf"char\s+{field}\[\]\s*=\s*\"[^\"]+\"\s*;", profile):
            problems.append(f"native_profile.h lacks {field}")
    draft = re.search(r"array<const char\*,\s*5>\s+draft_order\s*\{([^}]+)\}", profile)
    picks = re.findall(r'"([a-z_]+)"', draft.group(1)) if draft else []
    if len(picks) != 5 or len(set(picks)) != 5:
        problems.append(f"draft is not five distinct picks: {picks}")
    if set(picks) - legal:
        problems.append(f"draft names non-SDK champions {sorted(set(picks) - legal)}")
    if "cassi_and_issac" in picks:
        problems.append("draft uses the linked pair, which the runtime does not model")
    for ordinal, champion in KIT_ROLES.get(name, {}).items():
        if len(picks) > ordinal and picks[ordinal] != champion:
            problems.append(f"pick {ordinal} must be {champion} (controller role)")
    if name == "islander" and not set(picks[:3]) <= MOBILE:
        problems.append(f"core picks {picks[:3]} need native mobility")
    runtime = text(COMMON / "runtime.h")
    if not re.search(rf"case\s+Design::{design}:\s*return\s+choose_{number:02d}\(f,\s*s\);", runtime):
        problems.append(f"runtime does not dispatch Design::{design} to choose_{number:02d}")
    header = COMMON / ("doctrines_a.h" if number <= 8 else "doctrines_b.h")
    if not re.search(rf"inline\s+Task\s+choose_{number:02d}\s*\(", text(header)):
        problems.append(f"choose_{number:02d} is not defined in {header.name}")
    allowed = (src.resolve(), COMMON.resolve())
    outside = [p for p in closure(Bot("territory", name)) if not any(p.is_relative_to(a) for a in allowed)]
    if outside:
        problems.append("build closure leaves the bot and common/src: " +
                        ", ".join(p.relative_to(ROOT).as_posix() for p in outside[:3]))
    return problems


def report(case: str, problems: list[str]) -> bool:
    if problems:
        shown = "; ".join(problems[:4]) + (f"; +{len(problems) - 4} more" if len(problems) > 4 else "")
        print(f"FAIL {case}: {shown}", flush=True)
    else:
        print(f"ok   {case}", flush=True)
    return not problems


def guarded(fn, *args) -> list[str]:
    try:
        return fn(*args)
    except (OSError, ValueError, UnicodeDecodeError) as exc:
        return [f"{type(exc).__name__}: {exc}"]


def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(errors="replace")
    if any(a in ("-h", "--help") for a in argv):
        print(__doc__.strip())
        return 0
    available = bot_dirs()
    wanted = [a.split("/", 1)[1] if a.startswith("territory/") else a for a in argv]
    unknown = [a for a in wanted if a not in available]
    if unknown:
        print(f"unknown territory bot(s) {unknown}; choose from {' '.join(available)}", file=sys.stderr)
        return 2
    chosen = sorted(set(wanted)) if wanted else available
    passed = total = 0

    layout = guarded(check_layout)
    legal: set[str] = set()
    try:
        legal = set(enum_members(SDK / "cpp" / "cambc.h", "champion_draft"))
    except (OSError, ValueError) as exc:
        layout.append(f"SDK champion list: {exc}")
    total += 1
    passed += report("layout", layout)
    for name in chosen:
        total += 1
        passed += report(f"wiring {name}", guarded(check_bot, name, legal))

    for case, filename in (("startup-production-driver", "startup_driver.py"),
                           ("live-production-driver", "live_driver.py"),
                           ("phase-production-driver", "phase_driver.py"),
                           ("reciprocal-production-driver", "reciprocal_driver.py")):
        total += 1
        try:
            with Slots(1, quiet=True):
                driver = subprocess.run([sys.executable, "-B", str(HERE / filename), "--slot-held"],
                                        capture_output=True, text=True, errors="replace", timeout=300)
            messages = [] if driver.returncode == 0 else [driver.stdout[-1600:] + driver.stderr[-1600:]]
        except (OSError, subprocess.TimeoutExpired) as exc:
            messages = [f"{type(exc).__name__}: {exc}"]
        passed += report(case, messages)

    try:
        numbers = {designs().index(n.replace("-", "_")) + 1 for n in chosen}
    except (OSError, ValueError):
        numbers = set(range(1, 17))
    cases = [(c, s) for c, s, cover in FIXTURES if cover is None or numbers & set(cover)]
    out = CACHE / "wasi" / f"territory-{os.getpid()}"

    def one(case):
        try:
            with Slots(1, quiet=True):
                return run_case(case[0], HERE / case[1], out_dir=out)
        except Exception as exc:  # a missing toolchain is a failed case, not a traceback
            return False, f"{type(exc).__name__}: {exc}"

    with ThreadPoolExecutor(max_workers=max(1, len(cases))) as pool:
        results = list(pool.map(one, cases))
    fixture_failed = False
    for (case, _), (ok, log) in zip(cases, results):
        total += 1
        lines = [l for l in log.strip().splitlines() if l.strip()]
        if ok:
            passed += 1
            print(f"ok   fixture {case}" + (f": {lines[-1].strip()[:100]}" if lines else ""))
        else:
            fixture_failed = True
            print(f"FAIL fixture {case}" + ("" if lines else " (non-zero exit, no output)"))
            for line in lines[-LOG_LINES:]:
                print(f"    {line[:160]}")
    if fixture_failed:
        print(f"fixture modules kept in {out.relative_to(ROOT).as_posix()}")
    else:
        shutil.rmtree(out, ignore_errors=True)
    print(f"territory: {passed}/{total} passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
