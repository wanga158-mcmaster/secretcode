"""Focused tests of the distributed suite; no games are launched.

    python bots/distributed/tests/run.py [bot ...] [--case NAME] [--no-syntax] [--list] [--jobs N]

Fixtures are self-contained C++ programs, tests/plan6-<case>.cpp, with their own check macros
(active under NDEBUG). Each is compiled through tools/arena/wasi.py with the pinned wasi-sdk
clang (C++20, -Wall -Wextra -Werror, wasm exceptions) at the bots' Release optimisation
(-O3 -DNDEBUG), then run under Node with only WASI imports; it fails by a non-zero exit or an
uncaught exception. syntax-<bot> compiles <bot>/src/main.cpp, the whole runtime.h caller path
that no fixture includes, against the pinned SDK with the same flags (-fsyntax-only);
syntax-combat-sdk compiles the pinned-SDK block of the runtime-combat fixture
(P6_COMBAT_SDK_CHECK).

No arguments: every case. Named bots (caravan or distributed/caravan; the module name common
works too) select the fixtures whose include closure reaches that directory's src/, the runtime
fixtures that reach nothing outside common/ and reference/ (every bot compiles both: runtime.h
includes reference/src/policy.h), and the named bots' syntax cases. Prints one line per case;
exit 1 on a failure.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CACHE, CLANG, INCLUDE, Slots, slot_count  # noqa: E402
from wasi import run_case  # noqa: E402

PREFIX = "plan6-"
RUNTIME = frozenset({"common", "reference"})
FIXTURE_FLAGS = ["-O3", "-DNDEBUG"]  # appended after wasi.py's -O2 -UNDEBUG, so these win
SYNTAX_FLAGS = ["-std=c++20", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Werror", "-msimd128",
                "-mbulk-memory", "-fwasm-exceptions", "-mllvm", "-wasm-use-legacy-eh=false", "-fsyntax-only"]
COMBAT_SDK = ("syntax-combat-sdk", HERE / f"{PREFIX}runtime-combat.cpp", ["-DP6_COMBAT_SDK_CHECK"])
TAIL_LINES = 15


def closure(path: Path) -> set[Path]:
    """The file plus every file it reaches through quoted relative #include paths."""
    seen: set[Path] = set()
    stack = [path.resolve()]
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        for inc in INCLUDE.findall(f.read_text(encoding="utf-8", errors="replace")):
            cand = (f.parent / inc).resolve()
            if cand.is_file():
                stack.append(cand)
    return seen


def units() -> dict[str, Path]:
    """Bots (dirs with src/main.cpp) and shared modules (other dirs with src/) of the suite."""
    return {d.name: (d / "src").resolve() for d in sorted(SUITE.iterdir()) if (d / "src").is_dir()}


def fixtures(unit_dirs: dict[str, Path]) -> list[tuple[str, Path, frozenset[str]]]:
    """(case, source, units whose src/ the fixture's include closure reaches)."""
    rows = []
    for src in sorted(HERE.glob(PREFIX + "*.cpp")):
        files = closure(src)
        reach = frozenset(u for u, d in unit_dirs.items() if any(f.is_relative_to(d) for f in files))
        rows.append((src.stem.removeprefix(PREFIX), src, reach))
    return rows


def syntax(source: Path, extra=()) -> tuple[bool, str]:
    proc = subprocess.run([str(CLANG), *SYNTAX_FLAGS, *extra, f"-I{ROOT / 'sdk' / 'cpp'}", str(source)],
                          capture_output=True, text=True, errors="replace")
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip()[-4000:]


def tail(log: str) -> str:
    lines = [line[:200] for line in log.strip().splitlines()[-TAIL_LINES:]]
    return "".join("\n    " + line for line in lines)


def main() -> int:
    unit_dirs = units()
    bots = [u for u in unit_dirs if (unit_dirs[u] / "main.cpp").is_file()]
    rows = fixtures(unit_dirs)
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bots", nargs="*", metavar="bot", help="bot or module name (caravan, distributed/caravan, common)")
    parser.add_argument("--case", action="append", default=[], metavar="NAME",
                        help="run only this case (repeatable): a fixture name or syntax-<bot>")
    parser.add_argument("--no-syntax", action="store_true", help="skip the syntax-* cases")
    parser.add_argument("--list", action="store_true", help="show each case and the units it covers")
    parser.add_argument("--jobs", type=int, default=0, help="parallel cases (default: min(6, slots))")
    args = parser.parse_args()

    named = set()
    for name in args.bots:
        unit = name.removeprefix("distributed/")
        if unit not in unit_dirs:
            parser.error(f"unknown bot or module {name!r}; choose from {', '.join(unit_dirs)}")
        named.add(unit)
    cases = [(case, "fixture", src, reach, ()) for case, src, reach in rows
             if not named or reach & named or reach <= RUNTIME]
    if not args.no_syntax:
        cases += [(f"syntax-{b}", "syntax", unit_dirs[b] / "main.cpp", frozenset({b}), ())
                  for b in bots if not named or b in named or "common" in named]
        if not named or "common" in named:
            cases.append((COMBAT_SDK[0], "syntax", COMBAT_SDK[1], frozenset({"common"}), tuple(COMBAT_SDK[2])))
    if args.case:
        known = ({c[0] for c in cases} | {case for case, _, _ in rows} | {f"syntax-{b}" for b in bots}
                 | {COMBAT_SDK[0]})
        unknown = [c for c in args.case if c not in known]
        if unknown:
            parser.error(f"unknown case(s) {', '.join(unknown)}; see --list")
        cases = [c for c in cases if c[0] in args.case]
    if args.list:
        for case, kind, src, reach, _ in cases:
            if kind == "syntax":
                scope = "runtime: every bot" if reach == {"common"} else " ".join(sorted(reach))
            else:
                scope = "runtime: every bot" if reach <= RUNTIME else " ".join(sorted(reach - RUNTIME))
            print(f"{case:26} {scope}")
        print(f"{len(cases)} cases")
        return 0
    if not cases:
        print("no cases selected")
        return 1

    out = CACHE / "wasi" / f"distributed-{os.getpid()}"

    def run(item):
        case, kind, src, _, extra = item
        start = time.monotonic()
        with Slots(1, quiet=True):
            if kind == "fixture":
                ok, log = run_case(case, src, flags=FIXTURE_FLAGS, out_dir=out)
            else:
                ok, log = syntax(src, extra)
        return case, ok, log, time.monotonic() - start

    started, failed = time.monotonic(), []
    with ThreadPoolExecutor(max_workers=args.jobs or min(6, slot_count())) as pool:
        for case, ok, log, seconds in pool.map(run, cases):
            print(f"{'ok  ' if ok else 'FAIL'} {case:26} {seconds:5.1f}s" + ("" if ok else tail(log)), flush=True)
            if not ok:
                failed.append(case)
    print(f"{len(cases) - len(failed)}/{len(cases)} passed in {time.monotonic() - started:.0f}s"
          + (f"; failed: {' '.join(failed)}; binaries kept in {out}" if failed else ""))
    if not failed:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
