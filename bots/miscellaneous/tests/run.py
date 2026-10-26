"""Focused tests for the miscellaneous suite: python bots/miscellaneous/tests/run.py [bot ...]  (or arena test miscellaneous).

Each case is (name, sources[, options]): C++ fixtures compiled with the pinned wasi-sdk clang and
run under Node with only WASI imports (tools/arena/wasi.py). A fixture fails by returning
non-zero from main or by an uncaught exception. Options: includes, defines, flags, args, werror,
and "bot" (the bot a case covers, so `arena test miscellaneous/<bot>` runs only that bot's cases).
Selecting no registered cases fails, so a release gate cannot pass without running checks.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from wasi import run_cases  # noqa: E402

CASES = [
    ("timetable-health-dispatch", [SUITE / "timetable/tests/health_dispatch.cpp"],
     {"bot": "timetable", "includes": [SUITE / "timetable/src"]}),
    ("warband-model", [SUITE / "warband/tests/model.cpp"],
     {"bot": "warband", "includes": [SUITE / "warband/src"]}),
    ("warband-camp-actions", [SUITE / "warband/tests/camp_actions.cpp"],
     {"bot": "warband", "includes": [SUITE / "warband/src"]}),
    ("warband-nav-journal", [SUITE / "warband/tests/nav_journal.cpp"],
     {"bot": "warband", "includes": [SUITE / "warband/src"]}),
    ("warband-follow-route", [SUITE / "warband/tests/follow_route.cpp"],
     {"bot": "warband", "includes": [SUITE / "warband/src"]}),
]


def main(argv: list[str]) -> int:
    cases = [c for c in CASES if not argv or (c[2] if len(c) > 2 else {}).get("bot") in argv]
    if not cases:
        print("no test cases" + (f" for {', '.join(argv)}" if argv else "") + " (add fixtures to CASES)")
        return 1
    return run_cases(cases)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
