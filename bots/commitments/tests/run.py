"""Focused tests for the commitments suite (no games, no network).

    python bots/commitments/tests/run.py [bot|case ...]

No arguments runs every case. A bot name (with or without "commitments/") selects the shared
cases plus the design fixture that covers that bot; a case name selects that case; "common" (or
"commitments") selects everything, because a shared-module change moves every bot.

Cases:
  layout          every bot's build closure stays inside the suite, every quoted include
                  resolves, ids/slugs/namespaces agree, main.cpp runs its own policy (never a
                  control arm), no two policies are identical, and a design fixture covers each bot
  competition     common/src/competition.h: source choice, banking, exploration, exits, recovery
  receipts        common/src/receipts.h + contract.h: tribute receipts, campaign caps,
                  response labels, the four-response x three-deadline value table
  designs-01-05   live-policy.h of cards 01-05, active and control arms
  designs-06-10   live-policy.h of cards 06-10, active and control arms
  designs-11-16   live-policy.h of cards 11-16, active and control arms

C++ fixtures compile with the pinned wasi-sdk clang through tools/arena/wasi.py, adding -O3
-DNDEBUG (the release optimization; the fixtures use explicit checks, never assert) with
warnings as errors, and run under Node with only WASI imports. One line per case; exit 1 on any
failure, 2 on an unknown name. A failure prints at most 16 lines: the first lines naming the
failure (a fixture's FAIL line, the first compiler errors), then the tail.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CACHE, INCLUDE, SOURCE_SUFFIXES, Bot, closure  # noqa: E402
from wasi import run_case  # noqa: E402

FLAGS = ["-O3", "-DNDEBUG"]  # after wasi.py's -O2 -UNDEBUG, so these win
OUT = CACHE / "wasi" / "commitments"
SHARED = {"competition": "plan9-competition.cpp", "receipts": "plan9-live-receipts.cpp"}
DESIGNS = {
    "designs-01-05": ("plan9-designs-01-05.cpp",
                      ["twin-deadlines", "capture-fork", "cross-type-fork", "counterclaim", "banked-vanguard"]),
    "designs-06-10": ("plan9-designs-06-10.cpp",
                      ["dividend-redeployment", "decoupled-prizes", "bounty-syndicate", "spent-response-exchange",
                       "prepaid-column"]),
    "designs-11-16": ("plan9-designs-11-16.cpp",
                      ["committed-capacity", "successive-waves", "lock-work-exchange", "forbearance-frontier",
                       "capped-contest-investment", "critical-mass-conversion"]),
}
ORDER = ["layout", *SHARED, *DESIGNS]
PROFILE = re.compile(r'\bprofile\s*\{\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\{([^}]*)\}')
NAMESPACE = re.compile(r"^[ \t]*namespace\s+p9::(\w+)\s*\{", re.M)  # not "using namespace p9::live;"
SIGNAL = re.compile(r"FAIL|error|Error|failed|unreachable|abort")
ENTRY = re.compile(r"run<\s*p9::(\w+)::Policy\s*>\s*\(\s*p9::(\w+)::profile\s*,")


def bots() -> list[str]:
    return sorted(p.parent.parent.name for p in SUITE.glob("*/src/main.cpp"))


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def layout() -> tuple[bool, str]:
    problems: list[str] = []
    names = bots()
    sources = [p for p in SUITE.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES and ".cambc" not in p.parts]
    for src in sources:
        for inc in INCLUDE.findall(text(src)):
            target = (src.parent / inc).resolve()
            if not target.is_file():
                problems.append(f"{src.relative_to(SUITE).as_posix()}: unresolved include {inc}")
            elif not target.is_relative_to(SUITE) and not target.is_relative_to(ROOT / "sdk"):
                problems.append(f"{src.relative_to(SUITE).as_posix()}: include leaves the suite: {inc}")
    ids: dict[int, str] = {}
    policies: dict[str, str] = {}
    covered = {b for _, members in DESIGNS.values() for b in members}
    for name in names:
        src = SUITE / name / "src"
        outside = [p for p in closure(Bot(SUITE.name, name))
                   if not p.is_relative_to(SUITE) and not p.is_relative_to(ROOT / "sdk")]
        problems += [f"{name}: closure reaches {p.relative_to(ROOT).as_posix()}" for p in outside]
        try:
            profile, policy, entry = (text(src / f) for f in ("live-profile.h", "live-policy.h", "main.cpp"))
        except OSError as exc:
            problems.append(f"{name}: {exc.strerror}: {Path(exc.filename).name}")
            continue
        ns = name.replace("-", "_")
        found = PROFILE.search(profile)
        if not found:
            problems.append(f"{name}: no profile{{id, slug, draft}} in live-profile.h")
        else:
            design, slug, draft = int(found[1]), found[2], re.findall(r"Champion::(\w+)", found[3])
            if slug != name:
                problems.append(f"{name}: profile slug {slug!r}")
            if design in ids:
                problems.append(f"{name}: design id {design} also used by {ids[design]}")
            ids[design] = name
            if len(draft) != 5 or len(set(draft)) != 5:
                problems.append(f"{name}: draft must name five distinct champions, has {draft}")
        if set(NAMESPACE.findall(profile + policy)) != {ns}:
            problems.append(f"{name}: namespace must be p9::{ns}")
        if [m.groups() for m in ENTRY.finditer(entry)] != [(ns, ns)] or "Arm::control" in entry:
            problems.append(f"{name}: main.cpp must run p9::{ns}::Policy with its own profile, not a control arm")
        body = policy
        for declared in set(NAMESPACE.findall(policy)):  # a renamed copy is still the same policy
            body = re.sub(rf"\b{declared}\b", "NS", body)
        body = re.sub(r"\s+", "", body)
        if body in policies:
            problems.append(f"{name}: live-policy.h is identical to {policies[body]}'s")
        policies.setdefault(body, name)
        if name not in covered:
            problems.append(f"{name}: no design fixture covers it (add it to DESIGNS in tests/run.py)")
    for name in sorted(covered - set(names)):
        problems.append(f"DESIGNS lists {name}, which is not a bot")
    if problems:
        return False, "\n".join(problems[:20]) + (f"\n... {len(problems) - 20} more" if len(problems) > 20 else "")
    return True, f"{len(names)} bots self-contained, distinct ids/slugs/policies, all covered by fixtures"


def fixture(name: str) -> tuple[bool, str]:
    source = SHARED.get(name) or DESIGNS[name][0]
    return run_case(name, HERE / source, flags=FLAGS, out_dir=OUT)


def digest(log: str, limit: int = 16) -> list[str]:
    """The first lines naming a failure (a fixture's FAIL line, the first compiler errors), then the tail."""
    lines = [line[:200] for line in log.strip().splitlines()]
    keep = [i for i, line in enumerate(lines) if SIGNAL.search(line)][:limit // 2]
    keep += range(max(0, len(lines) - (limit - len(keep))), len(lines))
    return [lines[i] for i in sorted(set(keep))]


def select(args: list[str]) -> list[str] | None:
    if not args:
        return ORDER
    names, chosen = bots(), {"layout"}
    for raw in args:
        arg = raw.removeprefix(f"{SUITE.name}/").strip("/")
        if arg in ORDER:
            chosen.add(arg)
        elif arg in ("common", SUITE.name):
            chosen.update(ORDER)
        elif arg in names:
            chosen.update(SHARED)
            chosen.update(case for case, (_, members) in DESIGNS.items() if arg in members)
        else:
            print(f"unknown bot or case {raw!r}; bots: {' '.join(names)}; cases: {' '.join(ORDER)}")
            return None
    return [case for case in ORDER if case in chosen]


def main(argv: list[str]) -> int:
    cases = select(argv)
    if cases is None:
        return 2
    failed = 0
    for case in cases:
        try:
            ok, log = layout() if case == "layout" else fixture(case)
        except Exception as exc:  # a broken toolchain or source tree is a failure, not a crash
            ok, log = False, f"{type(exc).__name__}: {exc}"
        if ok:
            lines = log.strip().splitlines()
            print(f"ok   {case}" + (f": {lines[-1][:160]}" if lines else ""))
        else:
            failed += 1
            print(f"FAIL {case}\n    " + "\n    ".join(digest(log)))
        sys.stdout.flush()
    print(f"{len(cases) - failed}/{len(cases)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
