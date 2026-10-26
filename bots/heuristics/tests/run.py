"""Focused tests for the heuristics suite: python bots/heuristics/tests/run.py [bot ...]  (or arena test heuristics).

Each case is (name, sources[, options]): C++ fixtures compiled with the pinned wasi-sdk clang and
run under Node with only WASI imports (tools/arena/wasi.py). A fixture fails by returning
non-zero from main or by an uncaught exception. Options: includes, defines, flags, args, werror,
and "bot" (the bot a case covers, so `arena test heuristics/<bot>` runs only that bot's cases).
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from wasi import run_cases  # noqa: E402

CASES = [
    (p.stem, [p], {"bot": {"reset-ranged-defence": "reset-pursuit"}.get(p.stem, p.stem), "werror": True}) for p in sorted(HERE.glob("*.cpp"))
]


def main(argv: list[str]) -> int:
    cases = [c for c in CASES if not argv or c[0]=="common" or (c[2] if len(c) > 2 else {}).get("bot") in argv]
    if not cases:
        print("no test cases" + (f" for {', '.join(argv)}" if argv else "") + " (add fixtures to CASES)")
        return 0
    return run_cases(cases)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
