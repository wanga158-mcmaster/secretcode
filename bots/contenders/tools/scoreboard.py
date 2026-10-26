"""Read-only six-map v42 policy tournament scoreboard; never launches games.

  python -B -I -X utf8 bots/contenders/tools/scoreboard.py RUN ... --json OUT

Each package has24 training keys: six maps, both sides, v42 and starter.
Exact source/engine/variant/phase cohorts stay separate. This is training evidence,
not full-pool, held-out or eventual current-main qualification.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cohort

TRAINING_MAPS = ("00ambush", "00eight", "00gem-fields", "00lost-world", "01big-world", "01corridors")
ANCHOR = "anchors/main-v42@9fc5d7469074"
STARTER = "anchors/starter-cpp@3785135fc7c7"
OPPONENTS = (ANCHOR, STARTER)
ENGINE = "1.0.3"


def _training(item: dict) -> bool:
    row, side = item["row"], item["side"]
    other = "B" if side == "A" else "A"
    return (row.get("map") in TRAINING_MAPS and row.get(other.lower()) in OPPONENTS
            and ((row.get("tags") or {}).get("seed") or 0) == 0)


def _values(values: list[float]) -> dict:
    return {"n": len(values), "mean": statistics.mean(values) if values else None,
            "se": statistics.stdev(values) / len(values) ** .5 if len(values) > 1 else None}


def _margin(item: dict) -> float | None:
    row, side = item["row"], item["side"]
    own = (row.get(side) or {}).get("gems")
    other = (row.get("B" if side == "A" else "A") or {}).get("gems")
    return own - other if own is not None and other is not None else None


def _completed(item: dict) -> bool:
    row, side = item["row"], item["side"]
    return (cohort.terminal(row) and row["winner"] == side
            and (row.get(side) or {}).get("gems", 0) >= 1000
            and row.get("rounds") is not None)


def _speedrun(item: dict) -> int:
    return min(2000, int(item["row"]["rounds"])) if _completed(item) else 2000


def _results(items: list[dict]) -> dict:
    done = [item for item in items if cohort.terminal(item["row"])]
    wins = sum(item["row"]["winner"] == item["side"] for item in done)
    draws = sum(item["row"]["winner"] == "draw" for item in done)
    margins = [value for item in done if (value := _margin(item)) is not None]
    completed = [item["row"]["rounds"] for item in done if _completed(item)]
    return {"observed": len(items), "finished": len(done), "wins": wins,
            "losses": len(done) - wins - draws, "draws": draws,
            "gem_margin": _values(margins), "health": cohort.health(items),
            "completion": {"completed": len(completed), "not_completed": len(items) - len(completed),
                           "completed_rounds": _values(completed),
                           "capped_rounds": _values([_speedrun(item) for item in done])}}


def summarize_package(group: dict) -> dict:
    items = list(group["cells"].values())
    selected = [item for item in items if _training(item)]
    expected = {(opponent, map_name, side, 0) for opponent in OPPONENTS
                for map_name in TRAINING_MAPS for side in "AB"}
    observed = {cohort._cell(item["row"], item["side"]) for item in selected if cohort.terminal(item["row"])}
    outside = [cohort._cell(item["row"], item["side"]) for item in items if not _training(item)]
    opponents = {}
    for opponent in OPPONENTS:
        own = [item for item in selected if cohort._cell(item["row"], item["side"])[0] == opponent]
        opponents[opponent] = _results(own)
        opponents[opponent]["expected_keys"] = 12
        opponents[opponent]["missing_keys"] = sorted([cell for cell in expected - observed if cell[0] == opponent])
    entrant, engine, variant, phase = group["identity"]
    return {"subject": entrant, "engine": engine, "variant": variant, "phase": phase,
            "sources": sorted(group["sources"]), "expected_training_keys": 24,
            "finished_training_keys": len(observed), "missing_keys": sorted(expected - observed),
            "outside_training_cells": sorted(outside), "duplicate_rows": group["duplicates"],
            "conflicting_cells": sorted(group["conflicts"], key=str),
            "training_complete": engine == ENGINE and observed == expected and not outside and not group["conflicts"],
            "opponents": opponents, "training_health": cohort.health(selected)}


def paired_training(base: dict, arm: dict) -> dict:
    result = {"baseline": base["identity"], "candidate": arm["identity"], "n": 0}
    if base["identity"][1] == "unknown" or base["identity"][1] != arm["identity"][1]:
        return dict(result, refusal="different or unknown engine")
    keys = set(base["cells"]) & set(arm["cells"])
    pairs = [(base["cells"][key], arm["cells"][key]) for key in sorted(keys, key=str)]
    pairs = [(left, right) for left, right in pairs if _training(left) and _training(right)
             and cohort.terminal(left["row"]) and cohort.terminal(right["row"])]
    if not pairs:
        return dict(result, refusal="no identical finished training keys")
    opponents = {}
    for opponent in OPPONENTS:
        subset = [(left, right) for left, right in pairs if cohort._cell(left["row"], left["side"])[0] == opponent]
        score = lambda item: .5 if item["row"]["winner"] == "draw" else int(item["row"]["winner"] == item["side"])
        changes = [score(right) - score(left) for left, right in subset]
        margin_changes = [right - left for a, b in subset
                          if (left := _margin(a)) is not None and (right := _margin(b)) is not None]
        row = {"n": len(subset), "gained": sum(change > 0 for change in changes),
               "lost": sum(change < 0 for change in changes), "score_delta": _values(changes),
               "gem_margin_delta": _values(margin_changes)}
        if opponent == STARTER:
            row["starter_completion"] = {
                "baseline_completed": sum(_completed(left) for left, _ in subset),
                "candidate_completed": sum(_completed(right) for _, right in subset),
                "capped_round_delta": _values([_speedrun(right) - _speedrun(left) for left, right in subset]),
                "interpretation": "candidate minus baseline; non-completion is capped2000, not an observed finish"}
        opponents[opponent] = row
    return dict(result, n=len(pairs), opponents=opponents)


def report(paths: list[Path], baseline: str = "base") -> dict:
    groups, inputs = cohort.collect(paths)
    packages = [summarize_package(group) for group in groups]
    owners = collections.Counter(group["identity"][0].split("@")[0] for group in groups)
    comparisons = []
    for arm in groups:
        identity = arm["identity"]
        if identity[2] == baseline:
            continue
        candidates = [group for group in groups if group["identity"][0].split("@")[0] == identity[0].split("@")[0]
                      and group["identity"][1] == identity[1] and group["identity"][3] == identity[3]
                      and group["identity"][2] == baseline]
        comparisons.append(paired_training(candidates[0], arm) if len(candidates) == 1 else
                           {"candidate": identity, "n": 0, "refusal": "baseline missing or ambiguous"})
    return {"schema": "policy-training-scoreboard/1", "inputs": inputs,
            "measurement": {"path": str(Path(__file__).resolve()),
                            "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
            "scope": {"maps": list(TRAINING_MAPS), "opponents": list(OPPONENTS), "engine": ENGINE,
                      "keys_per_package": 24,
                      "observed_source_cohorts_per_owner": dict(owners),
                      "observed_finished_training_keys": sum(package["finished_training_keys"] for package in packages),
                      "initial_tournament_protocol": {"packages_per_owner": 4, "games_per_owner": 96, "all_owner_games": 288},
                      "baseline_variant": baseline,
                      "classification": "six-map training; full40pool and fresh17legacy qualification not represented"},
            "packages": packages, "paired": comparisons,
            "limits": ["Actual saved rows only; incomplete packages remain explicitly incomplete.",
                       "Exact opponent/source/engine cells are not relabelled or merged.",
                       "Per-opponent health is subject-side; zero clears is a stall proxy.",
                       "No full-pool, held-out or strength claim follows from these training results."]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", nargs="+", type=Path)
    parser.add_argument("--baseline", default="base")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    data = report(args.runs, args.baseline)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8", newline="\n")
    for package in data["packages"]:
        anchor, passive = (package["opponents"][opponent] for opponent in OPPONENTS)
        print(f"{package['subject']} {package['variant']} {package['finished_training_keys']}/24 "
              f"v42={anchor['wins']}/{anchor['finished']} starter={passive['wins']}/{passive['finished']} "
              f"completed={passive['completion']['completed']}/12 health={package['training_health']['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
