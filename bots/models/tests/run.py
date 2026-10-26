"""Focused tests of the models suite; never starts a game.

    python bots/models/tests/run.py [bot ...] [-k TEXT] [--list] [-q] [-j N]

No bot names: every case. Bot names (e.g. foundry, models/kernel): the shared-module cases plus
the cases that reach that bot's own src/. Cases:
  cpp:<x>   tests/plan4-<x>.cpp compiled to WASI (tools/arena/wasi.py) at -O3 with the bot
            link default 64 KiB stack and run under Node with WASI imports only
  sdk:<x>   -Wall -Wextra -Werror syntax check against the pinned sdk/cpp (bot and fixture mains)
  host:<x>  pinned-SDK module run under a restricted Node host (tests/plan4-<x>.mjs)
  py:<x>    tests/plan4-<x>.py (Python harnesses that compile source-bound C++ against SDK doubles)
Prints one line per case (failure tails are bounded) and exits non-zero on any failure.
Scratch lives in .cache/models-tests/ and is removed on success.
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
from dataclasses import dataclass, field
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CLANG, NODE, Slots, slot_count  # noqa: E402
from wasi import compile_fixture, run_case  # noqa: E402

OUT = ROOT / ".cache" / "models-tests"
RUN_DIR = OUT / f"run-{os.getpid()}"  # this process's modules, one directory per case
SDK = ROOT / "sdk" / "cpp"
ONLINE_SDK = HERE / "fixtures" / "plan4-online-sdk"
OWN_ACQUISITION_SDK = HERE / "fixtures" / "plan4-own-acquisition-sdk"
# Real bots link with wasm-ld's default 64 KiB stack; the later flag overrides wasi.py's 8 MiB.
WASI_FLAGS = ["-O3", "-Wl,-z,stack-size=65536"]
STRICT = ["-std=c++20", "-O3", "-Wall", "-Wextra", "-Werror", "-Wno-unused-private-field"]
DOUBLES = {name: [ONLINE_SDK] for name in
           ("online", "actors", "spatial", "navigation", "foundry-coverage", "habitat-caller",
            "observer-state-boundary", "actor-bind-diagnostics", "actor-packet-block", "observer-actor-packets",
            "observer-actor-packets-off", "producer-actor-packets", "observer-local-masks", "observer-local-masks-actors")}
DOUBLES.update({name: [OWN_ACQUISITION_SDK] for name in ("own-acquisition-sdk", "own-acquisition-hooks")})
SYNTAX_ONLY = ("actors-sdk", "spatial-sdk")      # pinned-SDK compile checks of SDK-facing collectors
HOSTED = ("common-constructor",)                   # pinned-SDK module + restricted host plan4-<x>.mjs
# Python harnesses: name -> (takes --compiler/--node, owners; empty = shared-module case).
PYTHON = {
    "draft-profiles": (False, ()), "kernel-attempt-log": (False, ("kernel",)),
    "draft-sdk": (True, ()), "attempt-hooks": (True, ()), "attempt-preaction-sdk": (True, ()),
    "actor-telemetry": (True, ()), "movement-telemetry": (True, ()), "observer-budget": (True, ()),
    "contract-turn-hooks": (True, ("covenant",)),
    "producer-order": (True, ()),
    "startup-handoff": (True, ()),
    "fresh-mask-hooks": (True, ()),
}
# The constructor regression measures the shared Bot constructor (through Archipelago's runtime).
SHARED_OVERRIDE = {"host:common-constructor"}
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.M)
TAIL_LINES, FULL_TAILS = 16, 6  # failure tail lines for the first FULL_TAILS failures, then 2 lines each


@dataclass
class Case:
    name: str
    run: object
    sources: list = field(default_factory=list)
    owners: frozenset = frozenset()


def bots() -> list[str]:
    return sorted(p.parent.parent.name for p in SUITE.glob("*/src/main.cpp"))


def closure(sources) -> set[Path]:
    files, stack = set(), [Path(s).resolve() for s in sources]
    while stack:
        f = stack.pop()
        if f in files or not f.is_file():
            continue
        files.add(f)
        for inc in INCLUDE.findall(f.read_text(encoding="utf-8", errors="replace")):
            cand = (f.parent / inc).resolve()
            if cand.is_file() and cand.is_relative_to(SUITE):
                stack.append(cand)
    return files


def owned_files() -> dict[str, set[Path]]:
    """Each bot's own src/ files, minus files every bot reaches through the shared modules
    (e.g. kernel/src/exact-kit.h, which common/attempt-preaction-sdk.h includes)."""
    shared = closure([SUITE / "reference" / "src" / "main.cpp"])
    return {b: {p.resolve() for p in (SUITE / b / "src").rglob("*") if p.is_file()} - shared for b in bots()}


def tail(text: str, count: int) -> str:
    lines = [line[:240] for line in text.strip().splitlines()] or ["(no output)"]
    return "\n".join(lines[-count:])


def wasi_case(stem: str):
    def run():
        out = RUN_DIR / stem
        ok, log = run_case(stem, HERE / f"plan4-{stem}.cpp", includes=DOUBLES.get(stem, []), flags=WASI_FLAGS,
                           out_dir=out)
        if ok or not any(out.glob("*.wasm")):
            shutil.rmtree(out, ignore_errors=True)
        return ok, log
    return run


def syntax_case(source: Path):
    def run():
        proc = subprocess.run([str(CLANG), *STRICT, "-fsyntax-only", f"-I{SDK}", str(source)],
                              capture_output=True, text=True, errors="replace")
        return proc.returncode == 0, proc.stdout + proc.stderr
    return run


def hosted_case(stem: str):
    def run():
        out = RUN_DIR / f"host-{stem}"
        out.mkdir(parents=True, exist_ok=True)
        wasm = out / f"{stem}.wasm"
        ok, log = compile_fixture(HERE / f"plan4-{stem}.cpp", wasm, flags=[*WASI_FLAGS, "-Wno-unused-private-field"])
        if not ok:
            shutil.rmtree(out, ignore_errors=True)
            return False, "COMPILE FAILED\n" + log
        ok, log = run_wasm_host(HERE / f"plan4-{stem}.mjs", wasm)
        if ok:
            shutil.rmtree(out, ignore_errors=True)
        return ok, log
    return run


def run_wasm_host(host: Path, wasm: Path) -> tuple[bool, str]:
    try:
        proc = subprocess.run([str(NODE), "--disable-warning=ExperimentalWarning", str(host), str(wasm)],
                              capture_output=True, text=True, errors="replace", timeout=600)
    except subprocess.TimeoutExpired:
        return False, "timed out after 600s"
    return proc.returncode == 0, proc.stdout + proc.stderr


def python_case(stem: str, tools: bool):
    def run():
        cmd = [sys.executable, "-B", str(HERE / f"plan4-{stem}.py")]
        if tools:
            cmd += ["--compiler", str(CLANG), "--node", str(NODE)]
        try:
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=1200)
        except subprocess.TimeoutExpired:
            return False, "timed out after 1200s"
        return proc.returncode == 0, proc.stdout + proc.stderr
    return run


def cases() -> list[Case]:
    found: list[Case] = []
    for path in sorted(HERE.glob("plan4-*.cpp")):
        stem = path.stem[len("plan4-"):]
        if stem in SYNTAX_ONLY:
            found.append(Case(f"sdk:{stem}", syntax_case(path), [path]))
        elif stem in HOSTED:
            found.append(Case(f"host:{stem}", hosted_case(stem), [path]))
        else:
            found.append(Case(f"cpp:{stem}", wasi_case(stem), [path]))
    for stem, (tools, owners) in PYTHON.items():
        path = HERE / f"plan4-{stem}.py"
        if not path.is_file():
            raise SystemExit(f"missing Python case {path.relative_to(ROOT)}")
        found.append(Case(f"py:{stem}", python_case(stem, tools), [], frozenset(owners)))
    for name in bots():
        main = SUITE / name / "src" / "main.cpp"
        found.append(Case(f"sdk:{name}", syntax_case(main), [main], frozenset({name})))
    for main in sorted(HERE.glob("fixtures/*/src/main.cpp")):
        found.append(Case(f"sdk:fixture-{main.parent.parent.name.removesuffix('-fixture')}",
                          syntax_case(main), [main]))
    own = owned_files()
    for case in found:
        if case.sources and not case.owners and case.name not in SHARED_OVERRIDE:
            reach = closure(case.sources)
            # A staged fixture tests/fixtures/<bot>-fixture/ belongs to <bot> even when it reuses only shared code.
            staged = {p.relative_to(HERE / "fixtures").parts[0].removesuffix("-fixture")
                      for p in reach if p.is_relative_to(HERE / "fixtures")}
            case.owners = frozenset({b for b, files in own.items() if files & reach} | (staged & set(own)))
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bots", nargs="*", help="bot names (default: all cases)")
    parser.add_argument("-k", dest="match", help="only cases whose name contains this text")
    parser.add_argument("--list", action="store_true", help="list the selected cases and their owners")
    parser.add_argument("-q", "--quiet", action="store_true", help="print only failures and the summary")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(8, slot_count() // 2)))
    args = parser.parse_args(argv)
    known = bots()
    named = [b.split("/", 1)[1] if b.startswith("models/") else b for b in args.bots]
    unknown = [b for b in named if b not in known]
    if unknown:
        print(f"unknown models bot(s): {', '.join(unknown)} (known: {' '.join(known)})", file=sys.stderr)
        return 2
    chosen = [c for c in cases() if not named or not c.owners or c.owners & set(named)]
    if args.match:
        chosen = [c for c in chosen if args.match in c.name]
    if args.list:
        for c in chosen:
            print(f"{c.name:34} {' '.join(sorted(c.owners)) or '(shared)'}")
        print(f"{len(chosen)} cases")
        return 0
    if not chosen:
        print("no cases selected")
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()

    def execute(case: Case):
        t = time.perf_counter()
        with Slots(1, quiet=True):
            try:
                ok, log = case.run()
            except Exception as exc:  # a broken harness is a failed case, not a crashed runner
                ok, log = False, f"{type(exc).__name__}: {exc}"
        return ok, log, time.perf_counter() - t

    failed = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [(c, pool.submit(execute, c)) for c in chosen]
        for case, future in futures:
            ok, log, seconds = future.result()
            if not ok or not args.quiet:
                print(f"{'ok  ' if ok else 'FAIL'} {case.name:34} {seconds:6.1f}s", flush=True)
            if not ok:
                failed.append(case.name)
                lines = TAIL_LINES if len(failed) <= FULL_TAILS else 2
                print("    " + tail(log, lines).replace("\n", "\n    "), flush=True)
    try:
        RUN_DIR.rmdir()  # empty unless a failed case left its module for inspection
    except OSError:
        if RUN_DIR.exists():
            print(f"failed modules kept in {RUN_DIR.relative_to(ROOT)}")
    scope = f" for {' '.join(named)}" if named else ""
    print(f"models{scope}: {len(chosen) - len(failed)}/{len(chosen)} passed in {time.perf_counter() - started:.1f}s"
          + (f"; failed: {' '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
