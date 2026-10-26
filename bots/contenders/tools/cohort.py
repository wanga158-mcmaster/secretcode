"""Summarize saved arena rows without games, builds, or replay decoding.

  python -B -I -X utf8 bots/contenders/tools/cohort.py RUN ... [--subject BOT_GLOB]
         [--json OUT]

Cohorts split exact subject entrant, engine, variant and phase. Outcomes pair only
on the same recorded engine, exact opponent entrant, map, subject side and seed.
Unknown engines never pair. Conflicting duplicate cells are excluded, not replaced.
This is a source-identity comparison: rows do not bind executable or map bytes.
Terminal gold/gems are not r400 fundamentals; decision distinctness needs replays.
"""
from __future__ import annotations

import argparse
import collections
import fnmatch
import hashlib
import itertools
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEALTH_FIELDS = ("faults", "exhausted", "errors", "fuel_at_budget", "clears")
METRICS = ("gems", "gold", "kills", "deaths", "clears")


def subject_side(row: dict, selector: str | None = None) -> str | None:
    if selector is None:
        side = str((row.get("tags") or {}).get("subject") or "").upper()
        return side if side in ("A", "B") else None
    if selector.upper() in ("A", "B"):
        return selector.upper()
    hits = []
    for side in "AB":
        entrant = str(row.get(side.lower()) or "")
        names = (entrant, entrant.split("@")[0], str(row.get(side.lower() + "_name") or ""))
        if any(fnmatch.fnmatchcase(name, selector) for name in names):
            hits.append(side)
    return hits[0] if len(hits) == 1 else None


def terminal(row: dict) -> bool:
    return row.get("winner") in ("A", "B", "draw") and not row.get("truncated")


def _cell(row: dict, side: str) -> tuple:
    other = "B" if side == "A" else "A"
    return (str(row.get(other.lower()) or "?"), row.get("map"), side,
            (row.get("tags") or {}).get("seed") or 0)


def _fingerprint(row: dict) -> str:
    return json.dumps({key: row.get(key) for key in
                       ("winner", "rounds", "truncated", "A", "B", "note")}, sort_keys=True)


def collect(paths: list[Path], selector: str | None = None) -> tuple[list[dict], dict]:
    groups, coverage, inputs = {}, collections.Counter(), []
    for path in paths:
        file = path / "games.jsonl" if path.is_dir() else path
        body = file.read_bytes()
        inputs.append({"path": str(file.resolve()), "size": len(body),
                       "sha256": hashlib.sha256(body).hexdigest()})
        for line_number, line in enumerate(body.decode("utf-8").splitlines(), 1):
            if not line.strip():
                continue
            coverage["rows"] += 1
            try:
                row = json.loads(line)
            except ValueError:
                coverage["malformed_rows"] += 1
                continue
            if not isinstance(row, dict):
                coverage["malformed_rows"] += 1
                continue
            side = subject_side(row, selector)
            if side is None:
                coverage["unknown_subject"] += 1
                continue
            tags = row.get("tags") or {}
            identity = (str(row.get(side.lower()) or "?"), str(row.get("engine") or "unknown"),
                        str(tags.get("variant") or "base"), str(tags.get("phase") or "untagged"))
            group = groups.setdefault(identity, {"identity": identity, "sources": set(),
                                                "cells": {}, "conflicts": set(), "duplicates": 0})
            group["sources"].add(str(file.resolve()))
            key = _cell(row, side)
            if key in group["conflicts"]:
                continue
            item = {"row": row, "side": side, "location": f"{file}:{line_number}"}
            old = group["cells"].get(key)
            if old is not None:
                if _fingerprint(old["row"]) == _fingerprint(row):
                    group["duplicates"] += 1
                else:
                    group["conflicts"].add(key)
                    del group["cells"][key]
                continue
            group["cells"][key] = item
    return [groups[key] for key in sorted(groups)], dict(coverage, files=inputs)


def health(items: list[dict], opponent: bool = False) -> dict:
    totals, unknown, flagged, peak = collections.Counter(), 0, 0, 0
    for item in items:
        row, side = item["row"], item["side"]
        if opponent:
            side = "B" if side == "A" else "A"
        team = row.get(side) or {}
        if any(key not in team or team[key] is None for key in HEALTH_FIELDS):
            unknown += 1
        problems = {}
        for key in ("faults", "exhausted", "errors"):
            problems[key] = int(team.get(key) or 0)
        problems["suspended"] = int(team.get("fuel_at_budget") or 0) if not problems["exhausted"] else 0
        problems["stalls"] = int(terminal(row) and team.get("clears") == 0)
        totals.update(problems)
        flagged += int(any(problems.values()))
        peak = max(peak, int(team.get("fuel_max") or 0))
    return dict(totals, flagged_games=flagged, unknown_games=unknown, peak_fuel=peak,
                status="failed" if flagged else "unknown" if unknown else "clean")


def describe(items: list[dict], pool: set[str]) -> dict:
    done = [item for item in items if terminal(item["row"])]
    results = collections.Counter("draw" if item["row"]["winner"] == "draw" else
                                  "win" if item["row"]["winner"] == item["side"] else "loss" for item in done)
    cells = {(item["row"].get("map"), item["side"],
              (item["row"].get("tags") or {}).get("seed") or 0) for item in done}
    seeds = {cell[2] for cell in cells}
    expected = {(map_name, side, seed) for map_name in pool for side in "AB" for seed in seeds}
    metrics = {}
    for key in METRICS:
        values = [(item["row"].get(item["side"]) or {}).get(key) for item in done]
        values = [value for value in values if value is not None]
        metrics[key] = {"n": len(values), "mean": statistics.mean(values) if values else None}
    return {"observed_cells": len(items), "finished": len(done), "unfinished": len(items) - len(done),
            "results": {key: results[key] for key in ("win", "loss", "draw")},
            "maps": sorted({cell[0] for cell in cells}, key=str),
            "sides": dict(collections.Counter(item["side"] for item in done)),
            "seeds": sorted(seeds, key=str), "full_pool_both_sides": bool(expected) and expected <= cells,
            "missing_pool_cells": sorted(expected - cells, key=str),
            "subject_health": health(items), "opponent_health": health(items, True),
            "terminal_metrics": metrics}


def summarize(group: dict, pool: set[str]) -> dict:
    by_opponent = collections.defaultdict(list)
    for key, item in group["cells"].items():
        by_opponent[key[0]].append(item)
    entrant, engine, variant, phase = group["identity"]
    out = {"subject": entrant, "engine": engine, "variant": variant, "phase": phase,
           "sources": sorted(group["sources"]), "duplicate_rows": group["duplicates"],
           "conflicting_cells": sorted(group["conflicts"], key=str),
           "opponents": {opponent: describe(items, pool) for opponent, items in sorted(by_opponent.items())}}
    speedrun = []
    for opponent, items in by_opponent.items():
        if opponent.split("@")[0] == "anchors/starter-cpp":
            for item in items:
                row = item["row"]
                if terminal(row):
                    speedrun.append(row["rounds"] if row["winner"] == item["side"] and
                                    (row.get(item["side"]) or {}).get("gems", 0) >= 1000 else 2000)
    out["speedrun"] = {"n": len(speedrun), "mean": statistics.mean(speedrun) if speedrun else None,
                       "unfinished": sum(value >= 2000 for value in speedrun)}
    return out


def matched(left: dict, right: dict) -> dict:
    a_id, b_id = left["identity"], right["identity"]
    out = {"a": a_id, "b": b_id, "n": 0}
    if a_id[1] == "unknown" or b_id[1] == "unknown" or a_id[1] != b_id[1]:
        return dict(out, refusal="different or unknown recorded engine")
    keys = sorted(set(left["cells"]) & set(right["cells"]), key=str)
    pairs = [(left["cells"][key], right["cells"][key]) for key in keys]
    pairs = [(a, b) for a, b in pairs if terminal(a["row"]) and terminal(b["row"])]
    if not pairs:
        return dict(out, refusal="no identical finished opponent/map/side/seed cells")
    score = lambda item: .5 if item["row"]["winner"] == "draw" else int(item["row"]["winner"] == item["side"])
    def margin(item):
        row, side = item["row"], item["side"]
        own = (row.get(side) or {}).get("gems")
        other = (row.get("B" if side == "A" else "A") or {}).get("gems")
        return own - other if own is not None and other is not None else None
    deltas = [score(b) - score(a) for a, b in pairs]
    margins = [(margin(a), margin(b)) for a, b in pairs]
    margin_deltas = [b - a for a, b in margins if a is not None and b is not None]
    by_opponent = collections.defaultdict(list)
    for a, b in pairs:
        by_opponent[_cell(a["row"], a["side"])[0]].append((a, b))
    per_opponent = {}
    for opponent, subset in sorted(by_opponent.items()):
        changes = [score(b) - score(a) for a, b in subset]
        per_opponent[opponent] = {"n": len(subset), "gained": sum(value > 0 for value in changes),
                                  "lost": sum(value < 0 for value in changes),
                                  "health_a": health([a for a, _ in subset]),
                                  "health_b": health([b for _, b in subset])}
    return dict(out, n=len(pairs), gained=sum(value > 0 for value in deltas),
                lost=sum(value < 0 for value in deltas), score_delta=statistics.mean(deltas),
                gem_margin_delta=statistics.mean(margin_deltas) if margin_deltas else None,
                gem_margin_pairs=len(margin_deltas), by_opponent=per_opponent,
                health_a=health([a for a, _ in pairs]), health_b=health([b for _, b in pairs]),
                interpretation="descriptive paired outcomes; health failures block qualification")


def report(paths: list[Path], selector: str | None = None) -> dict:
    groups, coverage = collect(paths, selector)
    pool = {path.stem for path in (ROOT / "maps").glob("*.map26a")}
    return {"schema": 1, "inputs": coverage, "pool_maps": len(pool),
            "measurement": {"path": str(Path(__file__).resolve()),
                            "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
            "cohorts": [summarize(group, pool) for group in groups],
            "matched": [matched(a, b) for a, b in itertools.combinations(groups, 2)],
            "limitations": ["Recorded source and engine identities, not executable/map-byte bindings.",
                            "Only observed saved cells; this does not attest planned run completion.",
                            "Terminal metrics do not establish r400 economy or decision distinctness.",
                            "No games, builds, replay decodes, online actions or health promotion."]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", nargs="+", type=Path)
    parser.add_argument("--subject")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = report(args.runs, args.subject)
    for group in result["cohorts"]:
        print(f"{group['subject']} engine={group['engine']} variant={group['variant']} phase={group['phase']}")
        for opponent, row in group["opponents"].items():
            scores, health_result = row["results"], row["subject_health"]
            flags = "/".join(str(health_result[key]) for key in ("faults", "exhausted", "errors", "stalls"))
            print(f"  vs {opponent}: {scores['win']}-{scores['loss']}-{scores['draw']}, "
                  f"n={row['finished']}, maps={len(row['maps'])}, sides={row['sides']}, "
                  f"pool={row['full_pool_both_sides']}, health={health_result['status']} f/x/e/s={flags}")
        if group["conflicting_cells"]:
            print(f"  EXCLUDED conflicting cells: {len(group['conflicting_cells'])}")
    for pair in result["matched"][:20]:
        print(f"paired {pair['a'][0]} -> {pair['b'][0]}: " +
              (pair["refusal"] if "refusal" in pair else f"n={pair['n']} +{pair['gained']}/-{pair['lost']}"))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
