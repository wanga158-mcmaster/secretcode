"""Exactness check of fastrun against the official CLI: the same games through both, compared game by game.

  python tools/fastrun/validate.py [--a anchors/main-v44] [--b anchors/fundamentals] [--maps pool]
                                [--runner .tools/fastrun/fastrun-dev.exe]

Plays A vs B on every map, both sides, through the arena's CLI path (`games.play`, the pinned cambc)
and through fastrun, and compares the winning team, rounds completed and both teams' tributed gems,
error lines, faults and fuel exhaustion. Missing, duplicate, extra and unsuccessful rows fail.
Run it after every CLI/SDK/engine update and after any fastrun change: experiments trust fastrun only
while every game is identical. Exit status 1 on any difference.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import shutil
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "arena"))
import core  # noqa: E402
import exp  # noqa: E402
import games  # noqa: E402


HEALTH = ("errors", "faults", "exhausted")


@contextmanager
def scratch_directory():
    # Inherit the workspace ACL: tempfile's private mode is inaccessible to some
    # restricted Windows child tokens used by the local runners.
    base = (core.ROOT / ".cache" / "runner-validation").resolve()
    tmp = base / uuid.uuid4().hex
    tmp.mkdir(parents=True)
    try:
        yield tmp
    finally:
        if not tmp.resolve().is_relative_to(base):
            raise core.ArenaError("validation scratch escaped its cache directory")
        shutil.rmtree(tmp)


def _values(row: dict, fast: bool, side: str) -> tuple:
    """Return physical-team values; fastrun arrays are always team A, then B."""
    if row.get("error") or row.get("truncated"):
        raise ValueError("runner error or truncated game")
    if fast:
        if row.get("a_side") != side or row.get("winner") not in {"a", "b", "draw"}:
            raise ValueError("invalid winner or subject side")
        winner = row["winner"]
        team = "draw" if winner == "draw" else side if winner == "a" else ("B" if side == "A" else "A")
        values = [row["rounds"]]
        for field in ("gems",) + HEALTH:
            pair = row[field]
            if not isinstance(pair, list) or len(pair) != 2:
                raise ValueError(f"invalid {field} team pair")
            values.extend(pair)
    else:
        team = row["winner"]
        if team not in {"A", "B", "draw"}:
            raise ValueError("unsuccessful CLI game")
        values = [row["rounds"]]
        for field in ("gems",) + HEALTH:
            values.extend(row[t][field] for t in ("A", "B"))
    if any(type(value) is not int or value < 0 for value in values):
        raise ValueError("missing or invalid game metrics")
    return (team, *values)


def compare_rows(cli: list[dict], fast: list[dict], expected: dict[str, str]) -> dict:
    """Check the complete requested matrix without allowing matching failures to pass."""
    issues = []
    indexes = []
    if not expected:
        issues.append("no expected games")
    for name, rows in (("CLI", cli), ("fastrun", fast)):
        index = {}
        for ordinal, row in enumerate(rows):
            try:
                key = row["tag"] if name == "fastrun" else row["tags"]["case"]
                if not isinstance(key, str):
                    raise ValueError("key must be a string")
            except (KeyError, TypeError, ValueError):
                issues.append(f"{name} row {ordinal}: invalid game key")
                continue
            if key in index:
                issues.append(f"{name} duplicate row: {key}")
            else:
                index[key] = row
        for key in sorted(set(index) - set(expected)):
            issues.append(f"{name} unexpected row: {key}")
        for key in sorted(set(expected) - set(index)):
            issues.append(f"{name} missing row: {key}")
        indexes.append(index)
    same = 0
    for key, side in expected.items():
        if key not in indexes[0] or key not in indexes[1]:
            continue
        try:
            ref = _values(indexes[0][key], False, side)
            got = _values(indexes[1][key], True, side)
        except (KeyError, TypeError, ValueError) as exc:
            issues.append(f"{key}: invalid game result ({exc})")
            continue
        if ref == got:
            same += 1
        else:
            issues.append(f"DIFF {key}: cli {ref} fastrun {got}")
    return {"identical": same, "expected": len(expected), "issues": issues,
            "ok": bool(expected) and same == len(expected) and not issues}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--a", default="anchors/main-v44")
    p.add_argument("--b", default="anchors/fundamentals")
    p.add_argument("--maps", default="pool")
    p.add_argument("--runner", type=Path, help="candidate fastrun executable; does not replace the installed runner")
    a = p.parse_args(argv)
    if a.runner:
        candidate = a.runner.resolve()
        if not candidate.is_file():
            p.error(f"runner does not exist: {candidate}")
    ea, eb = core.prepare_many([(core.bot(a.a), {}, a.a), (core.bot(a.b), {}, a.b)])
    if not (ea.ok and eb.ok):
        sys.exit("build failed")
    maps = core.map_set(a.maps)
    expected = {f"{m}.{side}": side for m in maps for side in ("A", "B")}
    if not expected or len(expected) != 2 * len(maps):
        p.error("map selection must contain distinct maps and at least one map")
    with scratch_directory() as tmp:
        specs = []
        for m in maps:
            specs.append(games.GameSpec(ea, eb, m, {"case": f"{m}.A"}))  # A's bot on team A
            specs.append(games.GameSpec(eb, ea, m, {"case": f"{m}.B"}))  # A's bot on team B
        cli = games.play(specs, tmp / "cli", keep_replays="none", quiet=True)
        lines = [f"{core.map_path(m)} {side} a - b - {m}.{side}" for m in maps for side in ("A", "B")]
        installed = exp.FASTRUN
        try:
            if a.runner:
                exp.FASTRUN = candidate
            fast = exp.run_games(lines, {"a": exp.wasm(ea), "b": exp.wasm(eb)}, tmp / "fast")
        finally:
            exp.FASTRUN = installed
    result = compare_rows(cli, fast, expected)
    for issue in result["issues"]:
        print(issue)
    print(f"identical {result['identical']}/{result['expected']}, issues {len(result['issues'])} "
          f"({a.a} vs {a.b}, maps {a.maps}, both sides; outcome and health)")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
