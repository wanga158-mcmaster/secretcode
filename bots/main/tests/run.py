"""bots/main tests: every bot's own unit tests, plus the main bot's game fixtures on request.

Usage: python -B bots/main/tests/run.py [bot ...] [--fixtures] [--fixture NAME ...] [--keep]
  bot             a bot of this suite (e.g. main, or main/main); default: all of them
  --fixtures      also run the game fixtures (heal, watchdog, spawn, capture, retaliation, liveness)
  --fixture NAME  run only this fixture (repeatable; implies --fixtures)
  --keep          keep the replays of passing fixtures (failing ones are always kept)
Unit tests: each bot owns bots/main/<bot>/tests/*.cpp, compiled against its own src/
(-I bots/main/<bot>/src) with the pinned wasi-sdk clang and run under Node (tools/arena/wasi.py).
Fixtures: main-<name>-fixture.py --bot main/<bot> replays past bugs of the main bot's chassis in
full release games against anchors (minutes each; they run inside the arena slot pool). By
default they test the main bot only; name other bots to test them too. One line per case;
the exit status is non-zero on any failure. `arena test main` runs the unit tests only.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import slot_count  # noqa: E402
from wasi import run_cases  # noqa: E402

MAIN = "main"  # the bot the fixtures were written for
FIXTURES = ["heal", "watchdog", "spawn", "capture", "retaliation", "liveness"]  # main-<name>-fixture.py
FIXTURE_TIMEOUT = 3 * 3600


def suite_bots() -> list[str]:
    return sorted(p.parent.parent.name for p in SUITE.glob("*/src/main.cpp"))


def run_fixture(bot: str, name: str, keep: bool) -> tuple[bool, list[str]]:
    cmd = [sys.executable, "-B", str(HERE / f"main-{name}-fixture.py"), "--bot", f"main/{bot}"]
    try:
        proc = subprocess.run(cmd + (["--keep"] if keep else []), cwd=ROOT, capture_output=True, text=True,
                              errors="replace", timeout=FIXTURE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, [f"timed out after {FIXTURE_TIMEOUT}s"]
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip().splitlines() or ["(no output)"]


def run_fixtures(bots: list[str], names: list[str], keep: bool) -> int:
    jobs = [(bot, name) for bot in bots for name in names]
    failed = 0
    with ThreadPoolExecutor(max_workers=max(1, min(len(jobs), slot_count() // 2, 6))) as pool:
        futures = [pool.submit(run_fixture, bot, name, keep) for bot, name in jobs]
        for (bot, name), future in zip(jobs, futures):
            ok, out = future.result()
            games = " | ".join(out[:-1]) if len(out) > 1 else out[0]
            print(f"{'ok  ' if ok else 'FAIL'} {bot}-fixture-{name}: {games[:300]}", flush=True)
            if not ok:
                failed += 1
                print("    " + "\n    ".join(out[-12:]), flush=True)
    print(f"fixtures {len(jobs) - failed}/{len(jobs)} passed")
    return 1 if failed else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("bots", nargs="*", help="bots of this suite to test (default: all)")
    p.add_argument("--fixtures", action="store_true", help="also run every game fixture")
    p.add_argument("--fixture", action="append", default=[], choices=FIXTURES, help="run this fixture")
    p.add_argument("--keep", action="store_true", help="keep the replays of passing fixtures")
    args = p.parse_args()
    known = suite_bots()
    chosen: list[str] = []
    for item in args.bots or known:
        name = item[len("main/"):] if item.startswith("main/") else item
        if name not in known:
            print(f"unknown bot {item!r} (bots: {', '.join(known)})")
            return 2
        if name not in chosen:
            chosen.append(name)
    cases = [(f"{bot}-{t.stem}", t, {"includes": [SUITE / bot / "src"]})
             for bot in chosen for t in sorted((SUITE / bot / "tests").glob("*.cpp"))]
    status = run_cases(cases) if cases else 0
    if MAIN in chosen:
        online_tests = subprocess.run([sys.executable, "-B", str(HERE / "main-online-test.py")], cwd=ROOT)
        status = max(status, online_tests.returncode)
        liveness_tests = subprocess.run([sys.executable, "-B", str(HERE / "main-liveness-test.py")], cwd=ROOT)
        status = max(status, liveness_tests.returncode)
    if not cases:
        print("no unit tests for: " + ", ".join(chosen))
    fixtures = list(dict.fromkeys(args.fixture)) or (FIXTURES if args.fixtures else [])
    if fixtures:
        targets = chosen if args.bots else [MAIN]
        status = max(status, run_fixtures(targets, fixtures, args.keep))
    return status


if __name__ == "__main__":
    sys.exit(main())
