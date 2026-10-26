"""Focused tests of the events suite; no games are launched.

    python bots/events/tests/run.py [bot ...] [--case NAME] [--no-syntax] [--list] [--jobs N]

Fixtures are self-contained C++ programs, tests/plan11-<case>.cpp. Each is compiled with the
pinned wasi-sdk clang (C++20, -O2, assertions on, -Wall -Wextra -Werror) and run under Node with
only WASI imports (tools/arena/wasi.py); it fails by a non-zero exit or an uncaught exception.
The syntax-<bot> cases compile <bot>/src/main.cpp, i.e. the whole runtime.h caller path that no
fixture includes, against the pinned SDK with the same warnings (-fsyntax-only).
The syntax-bounded-init-<bot> cases also compile the default-off staged initializer enabled;
native off/on games, not syntax checks, establish whether it avoids fuel exhaustion.

No arguments: every case. Named bots (tempo or events/tempo; the module name common works too)
select the cases whose include closure reaches that directory's src/, plus the runtime fixtures
that include nothing outside common/. Naming tempo therefore re-runs the fixtures and syntax
cases of every bot built on Tempo's Policy. Prints one line per case; exit 1 on any failure.
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
from core import CACHE, CLANG, INCLUDE, NODE, Slots, slot_count  # noqa: E402
from wasi import run_case  # noqa: E402

PREFIX = "plan11-"
RUNTIME = "common"
SYNTAX_FLAGS = ["-std=c++20", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-fwasm-exceptions",
                "-mllvm", "-wasm-use-legacy-eh=false", "-fsyntax-only"]
TAIL_LINES, DETAILED_FAILURES = 15, 5  # bound the output: later failures get one log line each


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


def discover() -> tuple[dict[str, Path], list[tuple[str, str, Path, frozenset[str]]]]:
    """Units (bots and shared modules: suite dirs with src/) and every case as
    (name, kind, source, units whose src/ the case's include closure reaches)."""
    units = {d.name: (d / "src").resolve() for d in sorted(SUITE.iterdir()) if (d / "src").is_dir()}

    def reach(src: Path) -> frozenset[str]:
        files = closure(src)
        return frozenset(u for u, d in units.items() if any(f.is_relative_to(d) for f in files))
    cases = [(src.stem.removeprefix(PREFIX), "fixture", src, reach(src))
             for src in sorted(HERE.glob(PREFIX + "*.cpp"))]
    cases += [(f"syntax-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-bounded-init-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-public-work-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-move-fuel-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-expedition-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-altar-minimum-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(f"syntax-altar-minimum-legacy-{u}", "syntax", d / "main.cpp", reach(d / "main.cpp"))
              for u, d in units.items() if (d / "main.cpp").is_file()]
    cases += [(name.removesuffix('.py'), "python", HERE / name, frozenset({RUNTIME}))
              for name in ('altar_minimum_maps.py','altar_minimum_caller.py','altar_minimum_initialization.py')
              if (HERE / name).is_file()]
    return units, cases


def selected(reach: frozenset[str], kind: str, named: set[str]) -> bool:
    """Every case without names; otherwise cases reaching a named src/, plus runtime-only fixtures."""
    return not named or bool(reach & named) or (kind == "fixture" and reach == {RUNTIME})


def syntax(main: Path, bounded_initialization: bool = False, cached_public_work: bool = False,
           guarded_movement: bool = False, expedition_receipt: bool = False,
           altar_minimum: bool = False) -> tuple[bool, str]:
    try:
        defines = ["-DP11_BOUNDED_INITIALIZATION=1"] if bounded_initialization else []
        if cached_public_work:
            defines.append("-DP11_CACHE_PUBLIC_WORK=1")
        if guarded_movement:
            defines.append("-DP11_MOVE_FUEL_GUARD=1")
        if expedition_receipt:
            defines.append("-DP11_EXPEDITION_RECEIPT=1")
        if altar_minimum:
            defines.append("-DP11_CACHE_ALTAR_MINIMUM=1")
        proc = subprocess.run([str(CLANG), *SYNTAX_FLAGS, *defines, f"-I{ROOT / 'sdk' / 'cpp'}", str(main)],
                              capture_output=True, text=True, errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return False, "syntax check timed out after 900s"
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip()[-4000:]


def detail(log: str, full: bool) -> str:
    """The log tail, or only its first error/assertion line once enough failures were shown."""
    lines = [line for line in log.strip().splitlines() if line.strip()]
    if not full:
        lines = [next((line for line in lines if "error" in line.lower() or "assert" in line.lower()),
                      lines[-1] if lines else "")]
    return "".join("\n    " + line[:200] for line in lines[-TAIL_LINES:])


def main() -> int:
    units, cases = discover()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bots", nargs="*", metavar="bot", help="bot or module (tempo, events/tempo, common)")
    parser.add_argument("--case", action="append", default=[], metavar="NAME",
                        help="only this case, a fixture name or syntax-<bot> (repeatable)")
    parser.add_argument("--no-syntax", action="store_true", help="skip the syntax-<bot> cases")
    parser.add_argument("--list", action="store_true", help="show the selected cases and what they cover")
    parser.add_argument("--jobs", type=int, default=0, help="cases run in parallel (default: min(6, slots))")
    args = parser.parse_args()

    named = {name.removeprefix("events/") for name in args.bots}
    unknown = sorted(named - set(units))
    if unknown:
        parser.error(f"unknown bot or module {', '.join(unknown)}; choose from {', '.join(units)}")
    unknown = sorted(set(args.case) - {c[0] for c in cases})
    if unknown:
        parser.error(f"unknown case {', '.join(unknown)}; see --list")
    chosen = [c for c in cases if selected(c[3], c[1], named)
              and not (args.no_syntax and c[1] == "syntax") and (not args.case or c[0] in args.case)]
    if args.list:
        for case, _, _, reach in chosen:
            print(f"{case:26} {'runtime: every bot' if reach == {RUNTIME} else ' '.join(sorted(reach - {RUNTIME}))}")
        print(f"{len(chosen)} cases")
        return 0
    if not chosen:
        print("no cases selected")
        return 1
    missing = [str(tool) for tool in (CLANG, NODE) if not tool.is_file()]
    if missing:
        print(f"missing toolchain: {', '.join(missing)} (see arena env)")
        return 1

    out = CACHE / "wasi" / f"events-{os.getpid()}"

    def run(item):
        case, kind, src, _ = item
        start = time.monotonic()
        if kind == "python":
            # Each production-extraction driver acquires Slots around its
            # compiler/runner; do not hold a second slot while it waits.
            proc = subprocess.run([sys.executable, '-B', str(src)],capture_output=True,text=True,
                                  encoding='utf-8',errors='replace',timeout=900)
            ok,log=proc.returncode==0,(proc.stdout+proc.stderr).strip()[-4000:]
        else:
            with Slots(1, quiet=True):
                altar=case.startswith('syntax-altar-minimum-')
                ok, log = run_case(case, src, out_dir=out / case) if kind == "fixture" else syntax(
                    src, bounded_initialization=case.startswith(("syntax-bounded-init-", "syntax-public-work-", "syntax-move-fuel-", "syntax-expedition-")) or (altar and not case.startswith('syntax-altar-minimum-legacy-')),
                    cached_public_work=case.startswith(("syntax-public-work-", "syntax-move-fuel-", "syntax-expedition-")) or altar,
                    guarded_movement=case.startswith(("syntax-move-fuel-", "syntax-expedition-")) or altar,
                    expedition_receipt=case.startswith("syntax-expedition-"),altar_minimum=altar)
        if ok:
            shutil.rmtree(out / case, ignore_errors=True)
        else:
            try:
                (out / case).rmdir()  # keep a failing fixture's binary, drop an empty directory
            except OSError:
                pass
        return case, ok, log, time.monotonic() - start

    started, failed = time.monotonic(), []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs or min(6, slot_count()))) as pool:
        for case, ok, log, seconds in pool.map(run, chosen):
            extra = "" if ok else detail(log, len(failed) < DETAILED_FAILURES)
            print(f"{'ok  ' if ok else 'FAIL'} {case:26} {seconds:5.1f}s{extra}", flush=True)
            if not ok:
                failed.append(case)
    try:
        out.rmdir()  # only succeeds when every kept failure directory is gone
    except OSError:
        pass
    print(f"{len(chosen) - len(failed)}/{len(chosen)} passed in {time.monotonic() - started:.0f}s"
          + (f"; failed: {' '.join(failed)}" if failed else "") + (f"; binaries kept in {out}" if out.is_dir() else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
