"""Focused tests of the doctrines suite; no games are launched and the game CLI is never invoked.

    python bots/doctrines/tests/run.py [bot|module|case ...] [--list] [--jobs N]

Every tests/plan5-<case>.cpp is a self-contained C++ fixture with its own CHECK macro, compiled
with the pinned wasi-sdk clang and run under Node with only WASI imports (no game, files or
network). A fixture fails by a non-zero exit, an abort or an uncaught exception.
  pure fixtures  use the defaults of tools/arena/wasi.py: C++20, -O2, assertions on, -Wall
                 -Wextra -Werror, wasm exceptions.
  host fixtures  include <cambc.h> themselves (inspection-route, runoff-native) and drive a bot's
                 native.h over reference/src/runtime.h through a deterministic in-process SDK host.
                 They need -fno-access-control and define their own aborting __cxa_throw, so they
                 compile here with the same options minus the wasm exception runtime, then run
                 under the same Node driver.

No arguments: every case. A bot or module name (dividend, doctrines/dividend, reference, common)
selects the fixtures whose include closure reaches that directory plus the shared fixtures that
reach no bot directory (they test common/, which every bot builds); common or all selects everything.
A case name (runoff-native, plan5-runoff-native) selects itself. Prints one line per case and a
summary; failure logs are tailed here and kept whole in .cache/arena/doctrines-tests/.
Exit 0 when every selected case passes, 1 on a failure, 2 on an unknown name.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CACHE, CLANG, INCLUDE, Slots  # noqa: E402
from wasi import run_case, run_wasm  # noqa: E402

PREFIX = "plan5-"
LOGS = CACHE / "doctrines-tests"
WASM = CACHE / "wasi"
TAIL = 12
HOST_FLAGS = ["-std=c++20", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-fno-access-control",
              "--no-wasm-opt", "-Wl,-z,stack-size=1048576", f"-I{ROOT / 'sdk' / 'cpp'}"]
SDK_INCLUDE = re.compile(r"^[ \t]*#[ \t]*include[ \t]*<cambc\.h>", re.M)
SUMMARY = re.compile(r"checks|passed|\bpass\b", re.I)
UNITS = sorted(d.name for d in SUITE.iterdir() if (d / "src").is_dir())  # bots and shared modules
SHARED_UNITS = {"common"}  # built by every bot, so it does not narrow a fixture to one bot


@dataclass(frozen=True)
class Case:
    name: str
    path: Path
    units: frozenset  # suite directories whose src/ the fixture's include closure reaches
    host: bool

    @property
    def bots(self) -> frozenset:
        return self.units - SHARED_UNITS


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def reach(path: Path) -> set[Path]:
    seen, stack = set(), [path.resolve()]
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        for inc in INCLUDE.findall(text(f)):
            cand = (f.parent / inc).resolve()
            if cand.is_file():
                stack.append(cand)
    return seen


def discover() -> list[Case]:
    cases = []
    for path in sorted(HERE.glob(PREFIX + "*.cpp")):
        files = reach(path)
        units = frozenset(u for u in UNITS if any(f.is_relative_to((SUITE / u / "src").resolve()) for f in files))
        cases.append(Case(path.stem[len(PREFIX):], path, units, bool(SDK_INCLUDE.search(text(path)))))
    return sorted(cases, key=lambda c: c.name)


def choose(names: list[str], cases: list[Case]) -> list[Case] | None:
    if not names:
        return cases
    by_name = {c.name: c for c in cases}
    picked: set[str] = set()
    for raw in names:
        name = raw.replace("\\", "/").removeprefix("bots/").removeprefix("doctrines/").strip("/")
        explicit = name.startswith(PREFIX) or name.endswith(".cpp")
        name = name.removesuffix(".cpp").removeprefix(PREFIX)
        if name in ("", "all", "doctrines", "common") and not explicit:
            hit = cases
        elif name in UNITS and not explicit:
            hit = [c for c in cases if name in c.units or not c.bots]
        elif name in by_name:
            hit = [by_name[name]]
        else:
            print(f"unknown bot, module or case {raw!r}; units: {' '.join(UNITS)}; cases: {' '.join(by_name)}")
            return None
        picked.update(c.name for c in hit)
    return [c for c in cases if c.name in picked]


def run_host(case: Case, out_dir: Path) -> tuple[bool, str]:
    wasm = out_dir / f"{case.name}.wasm"
    try:
        proc = subprocess.run([str(CLANG), *HOST_FLAGS, str(case.path), "-o", str(wasm)], capture_output=True,
                              text=True, errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return False, "COMPILE TIMED OUT after 900s"
    if proc.returncode:
        return False, "COMPILE FAILED\n" + (proc.stdout + proc.stderr)[-4000:]
    return run_wasm(wasm)


def run_one(case: Case, out_dir: Path) -> tuple[bool, str, float]:
    start = time.monotonic()
    try:
        ok, log = run_host(case, out_dir) if case.host else run_case(case.name, case.path, out_dir=out_dir)
    except OSError as exc:  # e.g. a missing compiler or Node: report it as this case's failure
        ok, log = False, f"runner error: {exc!r}"
    return ok, log, time.monotonic() - start


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description="doctrines suite fixtures (see module docstring)")
    parser.add_argument("names", nargs="*", help="bot, module or case names (default: every case)")
    parser.add_argument("--list", action="store_true", help="list the selected cases and exit")
    parser.add_argument("--jobs", type=int, default=4, help="parallel fixtures, within free arena slots")
    args = parser.parse_args()
    cases = discover()
    chosen = choose(args.names, cases)
    if chosen is None:
        return 2
    if args.list:
        for c in chosen:
            print(f"{c.name:20} {'host' if c.host else 'pure'}  {' '.join(sorted(c.bots)) or 'shared (every bot)'}")
        return 0
    if not chosen:
        print("no cases selected")
        return 1
    LOGS.mkdir(parents=True, exist_ok=True)
    WASM.mkdir(parents=True, exist_ok=True)
    failed = []
    with Slots(min(max(1, args.jobs), len(chosen)), quiet=True) as slots, \
            tempfile.TemporaryDirectory(prefix="doctrines-", dir=WASM, ignore_cleanup_errors=True) as tmp:
        with ThreadPoolExecutor(max_workers=slots.count) as pool:
            for case, (ok, log, secs) in zip(chosen, pool.map(lambda c: run_one(c, Path(tmp)), chosen)):
                lines, saved = log.strip().splitlines(), LOGS / f"{case.name}.log"
                if ok:  # show the fixture's own summary line (stdout and stderr are joined)
                    saved.unlink(missing_ok=True)  # drop a stale failure log
                    last = next((l for l in reversed(lines) if SUMMARY.search(l)), lines[-1] if lines else "")
                    print(f"ok   {case.name:20} {secs:5.1f}s  {last.strip()[:90]}", flush=True)
                    continue
                failed.append(case.name)
                saved.write_text(log, encoding="utf-8", errors="replace")
                print(f"FAIL {case.name:20} {secs:5.1f}s  full log: {saved.relative_to(ROOT).as_posix()}")
                for line in lines[-TAIL:]:
                    print("     " + line[:200])
    print(f"{len(chosen) - len(failed)}/{len(chosen)} passed" + (f"; failed: {' '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
