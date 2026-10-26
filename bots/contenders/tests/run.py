"""Unit tests of the contenders suite: python bots/contenders/tests/run.py [bot ...]  (or arena test contenders).

Each bot owns its tests: every bots/contenders/<bot>/tests/*.cpp is one case, compiled against that
bot's own src/ with the pinned wasi-sdk clang and run under Node with only WASI imports
(tools/arena/wasi.py). A case fails by returning non-zero from main or by an uncaught exception.
Name bots as `<bot>` or `contenders/<bot>` (default: all of them). Adding a test needs no edit
to this shared runner: drop the .cpp into your bot's tests/ directory.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from wasi import run_cases  # noqa: E402


def suite_bots() -> list[str]:
    return sorted(p.parent.parent.name for p in SUITE.glob("*/src/main.cpp"))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("bots", nargs="*", help="bots of this suite to test (default: all)")
    args = p.parse_args()
    known, prefix = suite_bots(), f"{SUITE.name}/"
    chosen: list[str] = []
    for item in args.bots or known:
        name = item[len(prefix):] if item.startswith(prefix) else item
        if name not in known:
            print(f"unknown bot {item!r} (bots: {', '.join(known)})")
            return 2
        if name not in chosen:
            chosen.append(name)
    cases = [(f"{bot}-{t.stem}", t, {"includes": [SUITE / bot / "src"]})
             for bot in chosen for t in sorted((SUITE / bot / "tests").glob("*.cpp"))]
    if not cases:
        print("no unit tests for: " + ", ".join(chosen))
        return 0
    return run_cases(cases)


if __name__ == "__main__":
    sys.exit(main())
