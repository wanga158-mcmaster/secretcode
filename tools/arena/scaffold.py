"""Scaffolding for new suites and bots.

  arena new suite NAME
      bots/NAME/README.md (charter template: Doctrine, Bots, Distinctness rules, Layout and
      checks, Leads) and bots/NAME/tests/run.py (a WASI fixture runner with no cases yet).
  arena new bot SUITE/NAME [--from SEL] [--force]
      Copy src/ of an existing bot (default anchors/starter-cpp) to bots/SUITE/NAME/src. A source
      in the same suite keeps using that suite's shared modules; a source from another suite must
      be self-contained (its closure inside its own src/), since suites never share code. Refuses
      to overwrite anything, and refuses SUITE anchors (frozen references) unless --force.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import BOTS, FROZEN_SUITES, ROOT, ArenaError, bot, closure, version  # noqa: E402

NAME = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
RESERVED = {"tests", "tools", "src", "all"}
DEFAULT_SOURCE = "anchors/starter-cpp"

README = """\
# {name}: <the doctrine question this suite answers>

## Doctrine
State the question in one or two sentences: which way of winning this suite explores and what
would count as an answer. List what it holds fixed (draft, economy, combat rules), the axis its
bots vary, and why its results should differ from its neighbours in the
[suite index](../README.md).

## Bots
| Bot | Decision rule that makes it play differently | State |
| --- | --- | --- |

Add a row per bot (`arena new bot {name}/<bot> --from <suite>/<bot>`). A bot earns its row with a
decision rule that changes its games; two bots that make the same choices count as one.

## Distinctness rules
- Code is shared only inside this suite (`bots/{name}/<module>/`); never include another suite.
- Check `arena ladder styles` after the first ladder games: a bot whose style sits next to a
  neighbour with a near-even head-to-head is not distinct.
- <the rules that keep this suite apart from its nearest neighbours>

## Layout and checks
- `bots/{name}/<bot>/src/main.cpp` is a bot (id `{name}/<bot>`); shared modules live in
  `bots/{name}/<module>/` and are included by relative paths.
- `bots/{name}/tests/run.py` holds the focused fixtures (C++ compiled to WASI by
  `tools/arena/wasi.py`): `arena test {name}`.
- `arena build {name}`, then measure with `arena play`, `arena sweep` and `arena gate`
  ([workflow](../../docs/workflow.md)).

## Leads
- <open questions and the next experiment, each with the evidence that motivates it>
"""

RUN_PY = '''\
"""Focused tests for the {name} suite: python bots/{name}/tests/run.py [bot ...]  (or arena test {name}).

Each case is (name, sources[, options]): C++ fixtures compiled with the pinned wasi-sdk clang and
run under Node with only WASI imports (tools/arena/wasi.py). A fixture fails by returning
non-zero from main or by an uncaught exception. Options: includes, defines, flags, args, werror,
and "bot" (the bot a case covers, so `arena test {name}/<bot>` runs only that bot's cases).
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from wasi import run_cases  # noqa: E402

CASES = [
    # ("probe-model", [HERE / "probe-model.cpp"], {{"bot": "probe", "includes": [SUITE / "common"]}}),
]


def main(argv: list[str]) -> int:
    cases = [c for c in CASES if not argv or (c[2] if len(c) > 2 else {{}}).get("bot") in argv]
    if not cases:
        print("no test cases" + (f" for {{', '.join(argv)}}" if argv else "") + " (add fixtures to CASES)")
        return 0
    return run_cases(cases)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
'''


def _rel(p: Path) -> str:
    try:
        return Path(p).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(p)


def check_name(name: str, what: str) -> str:
    if not NAME.fullmatch(name or "") or len(name) > 40 or name in RESERVED:
        raise ArenaError(f"invalid {what} name {name!r}: lowercase letters, digits and inner hyphens"
                         f" (at most 40), not {', '.join(sorted(RESERVED))}")
    return name


def new_suite(name: str, bots_dir: Path = BOTS) -> list[Path]:
    """Create bots/NAME/README.md and bots/NAME/tests/run.py; refuse an existing directory."""
    check_name(name, "suite")
    suite = Path(bots_dir) / name
    if suite.exists():
        raise ArenaError(f"{_rel(suite)} already exists")
    (suite / "tests").mkdir(parents=True)
    readme, runner = suite / "README.md", suite / "tests" / "run.py"
    readme.write_text(README.format(name=name), encoding="utf-8", newline="\n")
    runner.write_text(RUN_PY.format(name=name), encoding="utf-8", newline="\n")
    return [readme, runner]


def plan_bot(spec: str, source: str = DEFAULT_SOURCE, force: bool = False,
             bots_dir: Path = BOTS) -> tuple[object, Path, list[tuple[Path, Path]], list[Path]]:
    """Work out a `new bot` copy without writing: (source bot, target dir, [(from, to)], already
    present shared files). Raises ArenaError for anything that would overwrite or break."""
    if spec.count("/") != 1:
        raise ArenaError("use arena new bot SUITE/NAME")
    suite, name = spec.split("/")
    check_name(suite, "suite")
    check_name(name, "bot")
    if suite in FROZEN_SUITES and not force:
        raise ArenaError(f"{suite} holds frozen references; add a bot there only on purpose (--force)")
    bots_dir = Path(bots_dir)
    suite_dir = bots_dir / suite
    if not suite_dir.is_dir():
        raise ArenaError(f"no suite {suite!r}; create it first with `arena new suite {suite}`")
    target = suite_dir / name
    if target.exists():
        raise ArenaError(f"{_rel(target)} already exists")
    src_bot = bot(source)
    own = (src_bot.path / "src").resolve()
    src_suite = src_bot.path.parent.resolve()
    same_suite = src_suite == suite_dir.resolve()
    copies, present, conflicts = [], [], []
    for f in closure(src_bot):
        if f.is_relative_to(own):
            copies.append((f, target / "src" / f.relative_to(own)))
            continue
        if not f.is_relative_to(src_suite):
            raise ArenaError(f"{src_bot.id} includes {_rel(f)} outside its suite; fix the source first")
        if same_suite:
            continue  # the relative include still reaches the same shared module
        # Copying another suite's shared module into this suite would plant code other bots may own
        # (or, in suites without shared modules such as main, break their rule): refuse instead.
        raise ArenaError(f"{src_bot.id} reaches shared code outside its own src/ ({_rel(f)}); a bot in "
                         f"{suite} cannot take another suite's modules. Start from a self-contained bot "
                         f"(an anchor, main/main, specialists/*) or one of {suite}'s own bots")
        dst = suite_dir / f.relative_to(src_suite)
        if dst.exists():
            (present if dst.is_file() and dst.read_bytes() == f.read_bytes() else conflicts).append(dst)
        else:
            copies.append((f, dst))
    if conflicts:
        raise ArenaError(f"{src_bot.id} includes shared files that differ in {suite}: "
                         + ", ".join(_rel(c) for c in conflicts[:8]) + ("" if len(conflicts) <= 8 else ", ..."))
    return src_bot, target, copies, present


def new_bot(spec: str, source: str = DEFAULT_SOURCE, force: bool = False, bots_dir: Path = BOTS) -> list[str]:
    src_bot, target, copies, present = plan_bot(spec, source, force, bots_dir)
    for f, dst in copies:
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(f.read_bytes())
    suite = target.parent.name
    own = [d for _, d in copies if d.is_relative_to(target)]
    shared = [d for _, d in copies if not d.is_relative_to(target)]
    lines = [f"created {_rel(target / 'src')} from {src_bot.id}@{version(src_bot)} ({len(own)} files)"]
    if shared:
        lines.append(f"copied {len(shared)} shared module files into {_rel(target.parent)}: "
                     + ", ".join(d.relative_to(target.parent).as_posix() for d in shared[:6])
                     + (", ..." if len(shared) > 6 else ""))
    if present:
        lines.append(f"reused {len(present)} identical shared files already in {_rel(target.parent)}")
    lines += ["next steps:",
              f"  1. add a `{target.name}` row to the Bots table in bots/{suite}/README.md"
              " (the decision rule that makes it play differently)",
              f"  2. ./arena.cmd build {suite}/{target.name}",
              f"  3. add fixtures to bots/{suite}/tests/run.py, then ./arena.cmd test {suite}/{target.name}"]
    return lines


def _cmd_suite(args) -> int:
    made = new_suite(args.name)
    print("created " + ", ".join(_rel(p) for p in made))
    print("next steps:")
    print(f"  1. write the charter in bots/{args.name}/README.md (a doctrine axis no other suite covers)")
    print("  2. ask the integrator to list the suite in bots/README.md")
    print(f"  3. ./arena.cmd new bot {args.name}/<bot> --from <suite>/<bot>")
    return 0


def _cmd_bot(args) -> int:
    print("\n".join(new_bot(args.spec, args.source, args.force)))
    return 0


def register(sub) -> None:
    fmt = argparse.RawDescriptionHelpFormatter
    p = sub.add_parser("new", help="scaffold a suite (charter + tests) or a bot (copy of a bot's src/)",
                       description=__doc__, formatter_class=fmt)
    kinds = p.add_subparsers(dest="what", required=True, metavar="suite|bot")
    s = kinds.add_parser("suite", help="bots/NAME/README.md charter template and tests/run.py stub")
    s.add_argument("name")
    s.set_defaults(func=_cmd_suite)
    b = kinds.add_parser("bot", help="copy src/ of an existing bot into bots/SUITE/NAME/src")
    b.add_argument("spec", metavar="SUITE/NAME")
    b.add_argument("--from", dest="source", default=DEFAULT_SOURCE, help=f"source bot (default {DEFAULT_SOURCE})")
    b.add_argument("--force", action="store_true", help="allow adding a bot to the frozen anchors suite")
    b.set_defaults(func=_cmd_bot)
