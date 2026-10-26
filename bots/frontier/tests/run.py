"""Frontier suite tests. Starts no engine and no game.

    python bots/frontier/tests/run.py [bot ...] [--case NAME ...] [--list] [--jobs N]

Cases:
  <fixture>         tests/plan10-<fixture>.cpp compiled with the pinned wasi-sdk clang and run under
                    Node with WASI imports only (tools/arena/wasi.py); an assert or non-zero exit fails.
  native-executor   the saved common Runtime (made public through a generated test-access copy of
                    common/src/runtime.h) executing against fail-closed SDK mocks
                    (tests/plan10-native-executor.mjs), in both arms.
  syntax-<bot>      strict -Wall -Wextra -Werror -fsyntax-only compile of <bot>/src/main.cpp.
  native-analysis   unit tests of tools/analyze_native.py on synthetic statistics/replays.

Bot names select the cases whose relative-include closure reaches that bot's src/, plus its
syntax case. Shared-module and tool cases run in a whole-suite run (no names): a shared change
affects every bot and is re-tested suite-wide. Prints one line per case; exits 1 on any failure.
Build outputs go to .cache/frontier-tests/run-<pid>/, removed after a passing run.
"""
from __future__ import annotations

import argparse
import os
import re
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
from core import CLANG, NODE, Slots, slot_count  # noqa: E402
from wasi import compile_fixture, run_case  # noqa: E402

WORK = ROOT / ".cache" / "frontier-tests" / f"run-{os.getpid()}"
BOTS = sorted(p.parents[1].name for p in SUITE.glob("*/src/main.cpp"))
FIXTURES = ["common", "mobile-work", "partial-work", "cards-01-05", "cards-06-10", "cards-11-16",
            "cards-b-01-05", "cards-b-06-10", "cards-b-11-16", "forge-bootstrap", "native-observation",
            "native-navigation", "native-trace", "cards-c-02-05", "cards-c-06-10", "cards-c-11-16",
            "c-matched-controls", "c-forge-current", "c-capital-ownership", "reset-stock", "remote-work"]
SYNTAX_FLAGS = ["-std=c++20", "-O3", "-Wall", "-Wextra", "-Werror", "-fwasm-exceptions", "-mllvm",
                "-wasm-use-legacy-eh=false", "-fsyntax-only"]
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.M)
RUNTIME_SEAM = b"template<class Policy> class Runtime {"
DETAIL_LINES, DETAIL_BUDGET = 12, 60  # failure tail per case, and in total


def reached_bots(source: Path) -> set[str]:
    """Bots whose src/ the relative-include closure of `source` reaches (conditionals ignored)."""
    seen, stack = set(), [source.resolve()]
    while stack:
        f = stack.pop()
        for inc in INCLUDE.findall(f.read_text(encoding="utf-8", errors="replace")):
            cand = (f.parent / inc).resolve()
            if cand.is_file() and cand not in seen and cand.is_relative_to(SUITE):
                seen.add(cand)
                stack.append(cand)
    return {p.relative_to(SUITE).parts[0] for p in seen} & set(BOTS)


def _process(cmd: list, timeout: int = 600) -> tuple[bool, str]:
    try:
        proc = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace",
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timed out after {timeout}s"
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip()[-4000:]


def fixture(name: str):
    source = HERE / f"plan10-{name}.cpp"
    return name, reached_bots(source), lambda: run_case(name, source, out_dir=WORK)


def caller_trace():
    source = HERE / "plan10-reset-stock.cpp"
    return "reset-stock-caller", reached_bots(source), lambda: run_case(
        "reset-stock-caller", source, defines=["P10_DEPOT_CALLER_TRACE=1"], out_dir=WORK)


def native_executor():
    source = HERE / "plan10-native-executor.cpp"
    name = "native-executor"

    def go() -> tuple[bool, str]:
        runtime = (SUITE / "common" / "src" / "runtime.h").read_bytes()
        if runtime.count(RUNTIME_SEAM) != 1:
            return False, f"common/src/runtime.h must contain {RUNTIME_SEAM.decode()!r} exactly once"
        # Test-only access seam: changes C++ access, never the saved action semantics.
        WORK.mkdir(parents=True, exist_ok=True)
        facade = WORK / (name + "-runtime.h")
        facade.write_bytes(runtime.replace(RUNTIME_SEAM, RUNTIME_SEAM + b"\npublic: // Test-only access seam.", 1))
        wasm = WORK / (name + ".wasm")
        ok, log = compile_fixture(source, wasm, includes=[SUITE / "common" / "src"],
                                  defines=[f'ACTOR_RUNTIME_HEADER="{facade.as_posix()}"'])
        if not ok:
            return False, "COMPILE FAILED\n" + log
        return _process([NODE, "--disable-warning=ExperimentalWarning", HERE / "plan10-native-executor.mjs",
                         wasm, WORK / (name + "-sdk-calls.json")])
    return name, reached_bots(source), go


def runtime_receipt():
    source = HERE / "plan10-runtime-receipt.cpp"

    def go() -> tuple[bool, str]:
        runtime = (SUITE / "common" / "src" / "runtime.h").read_bytes()
        if runtime.count(RUNTIME_SEAM) != 1:
            return False, "runtime test access seam changed"
        WORK.mkdir(parents=True, exist_ok=True)
        facade = WORK / "receipt-runtime.h"
        facade.write_bytes(runtime.replace(RUNTIME_SEAM, RUNTIME_SEAM + b"\npublic: // Test-only access seam.", 1))
        wasm = WORK / "runtime-receipt.wasm"
        ok, log = compile_fixture(source, wasm, includes=[SUITE / "common" / "src"], defines=[
            f'ACTOR_RUNTIME_HEADER="{facade.as_posix()}"', "P10_RUNTIME_RECEIPT_ENABLE=1",
            "P10_RUNTIME_RECEIPT_FROM=0", "P10_RUNTIME_RECEIPT_TO=6", "P10_RUNTIME_RECEIPT_ACTORS=33"])
        if not ok:
            return False, "COMPILE FAILED\n" + log
        return _process([NODE, "--disable-warning=ExperimentalWarning", HERE / "plan10-runtime-receipt.mjs",
                         wasm, WORK / "runtime-receipt-sdk-calls.json"])
    return "runtime-receipt", set(BOTS), go


def firstdash_executor():
    source = HERE / "plan10-firstdash.cpp"

    def go() -> tuple[bool, str]:
        runtime = (SUITE / "common" / "src" / "runtime.h").read_bytes()
        start, end = b" bool dash_to(", b" bool cast_directive("
        if runtime.count(start) != 1 or runtime.count(end) != 1:
            return False, "production dash extraction seam changed"
        begin = runtime.index(start)
        finish = runtime.index(end, begin)
        WORK.mkdir(parents=True, exist_ok=True)
        body = WORK / "firstdash-body.inc"
        # Verbatim production method, compiled with explicit SDK doubles. The
        # fixture does not substitute its own landing selection algorithm.
        body.write_bytes(runtime[begin:finish])
        return run_case("firstdash-executor", source, defines=[
            f'FIRSTDASH_BODY_HEADER="{body.as_posix()}"'], out_dir=WORK)
    return "firstdash-executor", {"catalyst-depots"}, go


def syntax(bot: str):
    return f"syntax-{bot}", {bot}, lambda: _process([CLANG, *SYNTAX_FLAGS, SUITE / bot / "src" / "main.cpp"])


def post_export_guard(enabled: int):
    name = f"post-export-guard-{enabled}"
    source = HERE / "plan10-post-export.cpp"

    def go() -> tuple[bool, str]:
        navigation = (SUITE / "common/src/navigation.h").read_bytes()
        start, end = b" bool walk(", b" DraftContext draft_context("
        if navigation.count(start) != 1 or navigation.count(end) != 1:
            return False, "production walk extraction seam changed"
        begin = navigation.index(start)
        body = WORK / (name + "-body.inc")
        WORK.mkdir(parents=True, exist_ok=True)
        body.write_bytes(navigation[begin:navigation.index(end, begin)])
        return run_case(name, source, defines=[f'POST_EXPORT_BODY_HEADER="{body.as_posix()}"',
            f"P10_POST_EXPORT_GUARD={enabled}"], out_dir=WORK)
    return name, set(BOTS), go


def native_analysis():
    return "native-analysis", set(), lambda: _process([sys.executable, "-B", HERE / "plan10-native-analysis.py"])


def all_cases() -> list[tuple]:
    return ([fixture(n) for n in FIXTURES] + [caller_trace(), native_executor(), runtime_receipt(), firstdash_executor(), post_export_guard(0), post_export_guard(1)] + [syntax(b) for b in BOTS]
            + [native_analysis()])


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("bots", nargs="*", help="frontier bot names or frontier/<bot> ids (default: whole suite)")
    p.add_argument("--case", action="append", default=[], help="run only this case (repeatable)")
    p.add_argument("--list", action="store_true", help="list cases and the bots they cover")
    p.add_argument("--jobs", type=int, default=0, help="parallel cases (default: up to 8)")
    a = p.parse_args(argv)
    names = [b.rsplit("/", 1)[-1] for b in a.bots]
    unknown = sorted(set(names) - set(BOTS))
    if unknown:
        p.error(f"unknown frontier bot(s): {', '.join(unknown)}; known: {', '.join(BOTS)}")
    cases = all_cases()
    if a.case:
        missing = sorted(set(a.case) - {c[0] for c in cases})
        if missing:
            p.error(f"unknown case(s): {', '.join(missing)} (see --list)")
        cases = [c for c in cases if c[0] in a.case]
    if names:
        cases = [c for c in cases if c[1] & set(names)]
    if a.list:
        for name, bots, _ in cases:
            print(f"{name:22} {' '.join(sorted(bots)) if bots else '(suite-wide)'}")
        return 0
    if not cases:
        print("no cases selected")
        return 1

    def run_one(case) -> tuple[bool, str, float]:
        start = time.monotonic()
        with Slots(1, quiet=True):
            try:
                ok, log = case[2]()
            except Exception as exc:  # a broken case reports instead of hiding the others
                ok, log = False, f"{type(exc).__name__}: {exc}"
        return ok, log, time.monotonic() - start

    failed, budget = [], DETAIL_BUDGET
    jobs = a.jobs or max(1, min(8, slot_count() // 2))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_one, c) for c in cases]
        for case, future in zip(cases, futures):
            ok, log, seconds = future.result()
            print(f"{'ok  ' if ok else 'FAIL'} {case[0]} ({seconds:.0f}s)", flush=True)
            if not ok:
                failed.append(case[0])
                tail = [line[:200] for line in log.splitlines()[-DETAIL_LINES:]][:max(0, budget)]
                budget -= len(tail)
                for line in tail:
                    print("    " + line)
    if failed:
        print(f"{len(cases) - len(failed)}/{len(cases)} passed; failed: {' '.join(failed)}"
              + (f"; outputs kept in {WORK}" if WORK.is_dir() else ""))
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print(f"{len(cases)}/{len(cases)} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
