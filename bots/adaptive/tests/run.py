"""Focused tests of the adaptive suite; no games are launched and the game CLI is never invoked.

    python bots/adaptive/tests/run.py [bot|case ...] [--list] [--jobs N]

Cases:
  layout        every quoted #include in the selected files resolves inside bots/adaptive or sdk/cpp
  control-knob  per bot: the arena knob P8_CONTROL=1 (core.apply_overrides) edits the first
                P8_CONTROL definition the preprocessor reaches, so it really builds the control arm
  <fixture>     tests/plan8-<fixture>.cpp, a self-contained C++ program compiled with the pinned
                wasi-sdk clang (C++20, -O2, assertions on, -Wall -Wextra -Werror) and run under Node
                with only WASI imports (tools/arena/wasi.py); it fails by a non-zero exit or an
                uncaught exception. COMPILE_ONLY fixtures are compiled and linked but not run.

No arguments: every case. Named bots (canary-and-cashflow or adaptive/canary-and-cashflow) select
the static cases for those bots and each fixture that includes, directly, a header the bot's
main.cpp compiles or a header in the bot's own src/ (see --list). A change to a shared header
needs the whole suite. Fixture names (evidence or plan8-evidence) select themselves. Prints one
line per case and a summary; failing logs are tailed and kept whole in .cache/arena/adaptive-tests/.
Exit 1 on a failure, 2 on an unknown name.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402
from wasi import compile_fixture, run_case  # noqa: E402

PREFIX = "plan8-"
SDK = (ROOT / "sdk" / "cpp").resolve()
LOGS = core.CACHE / "adaptive-tests"
KNOB = "P8_CONTROL"
DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*(?:include[ \t]*"([^"\n]+)"|define[ \t]+' + KNOB + r'\b)', re.M)
STATIC = ("layout", "control-knob")
# Fixtures that cannot run under pure WASI; they still have to compile and link.
COMPILE_ONLY = {
    "context-geometry": "compile only: a namespace-scope initializer in its include closure calls "
                        "the SDK (game_settings) at startup",
}
TAIL_LINES, FULL_TAILS = 12, 3


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def reach(path: Path) -> set[Path]:
    """The file plus every file it reaches through quoted relative #include paths (SDK excluded)."""
    seen: set[Path] = set()
    stack = [path.resolve()]
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        for inc in core.INCLUDE.findall(text(f)):
            cand = (f.parent / inc).resolve()
            if cand.is_file() and not cand.is_relative_to(SDK):
                stack.append(cand)
    return seen


def layout_problems(files) -> list[str]:
    """Quoted includes that do not resolve, or that resolve outside bots/adaptive and the SDK."""
    problems = []
    for f in sorted(files):
        if f.suffix not in core.SOURCE_SUFFIXES:
            continue
        for inc in core.INCLUDE.findall(text(f)):
            cand = (f.parent / inc).resolve()
            if cand.is_file():
                if not (cand.is_relative_to(SUITE) or cand.is_relative_to(SDK)):
                    problems.append(f'{rel(f)}: #include "{inc}" leaves the suite ({rel(cand)})')
            elif not (SDK / inc).is_file():
                problems.append(f'{rel(f)}: #include "{inc}" does not resolve')
    return problems


def first_knob_definition(main: Path) -> Path | None:
    """File holding the first #define P8_CONTROL in preprocessing order (#pragma once headers)."""
    seen: set[Path] = set()

    def walk(f: Path) -> Path | None:
        if f in seen:
            return None
        seen.add(f)
        for m in DIRECTIVE.finditer(text(f)):
            if m.group(1) is None:
                return f
            cand = (f.parent / m.group(1)).resolve()
            if cand.is_file() and not cand.is_relative_to(SDK):
                hit = walk(cand)
                if hit:
                    return hit
        return None
    return walk(main.resolve())


def knob_problem(b: core.Bot) -> str | None:
    first = first_knob_definition(b.path / "src" / "main.cpp")
    if first is None:
        return f"{b.name}: main.cpp reaches no #define {KNOB}"
    contents = core.read_closure(b)
    try:
        changed = core.apply_overrides(b, contents, {KNOB: "1"})
    except core.ArenaError as exc:
        return f"{b.name}: {exc}"
    edited = [p for p in contents if changed[p] != contents[p]]
    # arena rewrites every identical guarded default, so the effective (first reached) one must be among them
    if first not in edited:
        return (f"{b.name}: the override edits {', '.join(rel(p) for p in edited) or 'nothing'}, but the "
                f"first definition reached is in {rel(first)}")
    return None


def fixtures(bots: dict[str, core.Bot]) -> list[tuple[str, Path, frozenset[str]]]:
    """(case, source, bots it covers) for every tests/*.cpp. A fixture covers each bot whose
    main.cpp compiles a header the fixture includes directly, and the bot whose own src/ holds it."""
    built = {n: reach(b.path / "src" / "main.cpp") for n, b in bots.items()}
    rows = []
    for src in sorted(HERE.glob("*.cpp")):
        direct = {(src.parent / inc).resolve() for inc in core.INCLUDE.findall(text(src))}
        direct = {d for d in direct if d.is_file() and d.is_relative_to(SUITE) and not d.is_relative_to(HERE)}
        cover = {n for n, files in built.items() if files & direct}
        cover |= {d.relative_to(SUITE).parts[0] for d in direct} & set(bots)
        rows.append((src.stem.removeprefix(PREFIX), src, frozenset(cover or bots)))
    return rows


def last_line(log: str) -> str:
    lines = [line.strip() for line in log.strip().splitlines() if line.strip()]
    if not lines:
        return ""
    return lines[-1][:110] + ("..." if len(lines[-1]) > 110 else "")


def run_fixture(name: str, src: Path) -> tuple[bool, str]:
    with core.Slots(1, quiet=True):
        if name not in COMPILE_ONLY:
            return run_case(name, src)
        out = core.CACHE / "wasi" / f"{name}-compile-only-{os.getpid()}.wasm"
        out.parent.mkdir(parents=True, exist_ok=True)
        try:
            ok, log = compile_fixture(src, out)
        finally:
            out.unlink(missing_ok=True)
        return ok, (COMPILE_ONLY[name] if ok else "COMPILE FAILED\n" + log)


def report(name: str, ok: bool, log: str, shown: list[int]) -> None:
    if ok:
        print(f"ok   {name}  {last_line(log)}", flush=True)
        return
    shown[0] += 1
    if shown[0] <= FULL_TAILS:
        tail = [line[:200] for line in log.strip().splitlines()[-TAIL_LINES:]]
        print(f"FAIL {name}" + "".join("\n    " + line for line in tail), flush=True)
    else:
        print(f"FAIL {name}  (log: {rel(LOGS / (name + '.log'))})", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("names", nargs="*", help="bots (x or adaptive/x) or cases")
    ap.add_argument("--list", action="store_true", help="print each fixture and the bots it covers")
    ap.add_argument("--jobs", type=int, default=0, help="parallel fixtures (default: up to 4 slots)")
    args = ap.parse_args()

    bots = {b.name: b for b in core.select("adaptive")}
    cases = fixtures(bots)
    if args.list:
        for name, _, cover in cases:
            note = " (compile only)" if name in COMPILE_ONLY else ""
            print(f"{name + note:40} {'all bots' if cover == frozenset(bots) else ' '.join(sorted(cover))}")
        return 0

    chosen_bots: set[str] = set()
    chosen_cases: set[str] = set()
    for raw in args.names:
        name = raw.removeprefix("adaptive/").removesuffix(".cpp").removeprefix(PREFIX)
        if name in bots:
            chosen_bots.add(name)
        elif name in STATIC or any(name == c[0] for c in cases):
            chosen_cases.add(name)
        else:
            print(f"unknown bot or case {raw!r}; bots: {' '.join(bots)}", file=sys.stderr)
            return 2
    everything = not args.names
    selected = [c for c in cases if everything or c[0] in chosen_cases or c[2] & chosen_bots]
    static_bots = sorted(chosen_bots) if chosen_bots else sorted(bots)
    started, results, shown = time.time(), [], [0]
    LOGS.mkdir(parents=True, exist_ok=True)

    def record(name: str, ok: bool, log: str) -> None:
        (LOGS / f"{name}.log").write_text(log + "\n", encoding="utf-8", errors="replace")
        results.append((name, ok))
        report(name, ok, log, shown)

    if everything or chosen_bots or "layout" in chosen_cases:
        if everything or "layout" in chosen_cases:
            files = {p.resolve() for p in SUITE.rglob("*") if p.is_file() and ".cambc" not in p.parts}
        else:
            files = set().union(*(set(core.closure(bots[n])) for n in static_bots),
                                *(reach(c[1]) for c in selected))
        problems = layout_problems(files)
        record("layout", not problems, "\n".join(problems) or
               f"{len(files)} files: includes resolve inside bots/adaptive or sdk/cpp")
    if everything or chosen_bots or "control-knob" in chosen_cases:
        problems = [p for p in (knob_problem(bots[n]) for n in static_bots) if p]
        record("control-knob", not problems, "\n".join(problems) or
               f"{len(static_bots)} bot(s): {KNOB}=1 edits the first definition reached")

    jobs = args.jobs or max(1, min(4, core.slot_count()))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for (name, _, _), (ok, log) in zip(selected, pool.map(lambda c: run_fixture(c[0], c[1]), selected)):
            record(name, ok, log)

    failed = [n for n, ok in results if not ok]
    print(f"{len(results) - len(failed)}/{len(results)} passed in {time.time() - started:.0f}s"
          + (f"; failed: {' '.join(failed)}" if failed else ""))
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
