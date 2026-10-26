"""Screen retained arena statistics for long tails without own cumulative activity.

  python -B bots/main/tools/liveness.py RUN [--variant NAME] [--out DIR_OR_JSON]

The screen reads games.jsonl and its exact retained statistics companions, not replays.
SUSPECT means every measured own resource/combat/healing/death counter is unchanged for
at least --min-tail rounds. It is not a confirmed action or position stall: travel,
channels, waiting and sieges may legitimately look alike. Only the final tail is screened;
earlier transient stalls can be missed, and counter activity can conceal immobility.

Exit 0 means the screen completed (possibly with suspects); exit 2 means some selected
coverage is unavailable. No missing file, metric or subject is silently called clean.
"""
from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal, InvalidOperation
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
CHAMPION_METRICS = (
    "earnedGold", "spentGold", "saleRefunds", "collectedGems", "submittedGems",
    "kills", "deaths", "assists", "monsterDamageDone", "monsterDamageTaken",
    "championDamageDone", "championDamageTaken", "healingDone", "healingReceived",
    "shieldingDone",
)
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_TIMELINES = 10000
MAX_RUNS = 100000
MAX_GAME_ROWS = 100000
LIMITS = (
    "SUSPECT is a statistics-only unproductive-tail screen, never a confirmed stall.",
    "Travel, channels, waiting, recovery and sieges require native action/position inspection.",
    "Only the final cumulative-counter tail is screened; earlier transient stalls may be missed.",
    "Coverage describes selected rows retained when read; it does not prove every planned game has finished.",
    "Combat, healing or other counter activity can conceal stationary or otherwise useless play.",
    "Ownership comes only from explicit tags.subject A/B and the statistics roster; no name-based inference.",
)


class Unavailable(ValueError):
    pass


def integer(value, label, minimum=0):
    if type(value) is not int or value < minimum:
        raise Unavailable(f"{label} must be an integer >= {minimum}")
    return value


def read_json(path):
    if not path.is_file():
        raise Unavailable(f"statistics file missing: {path}")
    if path.stat().st_size > MAX_JSON_BYTES:
        raise Unavailable(f"statistics file exceeds {MAX_JSON_BYTES} bytes: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise Unavailable(f"statistics unreadable: {path}: {exc}") from exc


def timeline_last_change(timeline, final_round):
    """RLE sample 0 is initial state; a new run changes at cumulative duration.

    No expansion and no assumption that every run represents a change. Adjacent equal
    runs are harmless. The full run/sample count must agree and include the final round.
    """
    runs = timeline.get("runs")
    if not isinstance(runs, list) or not runs or len(runs) > MAX_RUNS:
        raise Unavailable("timeline runs missing, empty or beyond bounded run count")
    sample_count = integer(timeline.get("sampleCount"), "sampleCount", 1)
    end, last_change, previous = 0, 0, None
    for run in runs:
        if not isinstance(run, dict):
            raise Unavailable("timeline run must be an object")
        duration = integer(run.get("duration"), "run duration", 1)
        raw = run.get("value")
        if isinstance(raw, bool) or not isinstance(raw, (str, int, float)) or len(str(raw)) > 128:
            raise Unavailable("timeline value is not a bounded number")
        try:
            value = Decimal(str(raw))
        except InvalidOperation as exc:
            raise Unavailable("timeline value is not numeric") from exc
        if not value.is_finite() or value < 0:
            raise Unavailable("cumulative timeline value is negative or nonfinite")
        if previous is not None:
            if value < previous:
                raise Unavailable("cumulative timeline decreases")
            if value != previous and end <= final_round:
                last_change = end
        previous = value
        end += duration
    if end != sample_count:
        raise Unavailable(f"RLE duration {end} differs from sampleCount {sample_count}")
    if sample_count != final_round + 1:
        raise Unavailable(f"sampleCount {sample_count} does not cover rounds 0..{final_round} exactly")
    return {"last_change_round": last_change, "sample_count": sample_count,
            "final_value": str(previous), "runs": len(runs)}


def screen_statistics(stats, subject, min_tail=120):
    """Return an available screen or an explicit UNAVAILABLE result."""
    try:
        if subject not in ("A", "B"):
            raise Unavailable("explicit tags.subject A/B is required")
        if not isinstance(stats, dict):
            raise Unavailable("statistics report is not an object")
        match = stats.get("match") or {}
        completed = integer(match.get("roundsCompleted"), "roundsCompleted")
        final_round = integer(match.get("roundsStarted"), "roundsStarted")
        if final_round < completed or final_round > completed + 1:
            raise Unavailable("roundsStarted/roundsCompleted disagree")
        team = "team" + subject
        roster = (stats.get("roster") or {}).get("champions")
        if not isinstance(roster, list):
            raise Unavailable("champion roster missing")
        champions = [c for c in roster if isinstance(c, dict) and c.get("team") == team]
        if not champions:
            raise Unavailable(f"no {team} champions in roster")
        ids = [c.get("id") for c in champions]
        if any(type(cid) is not int for cid in ids) or len(set(ids)) != len(ids):
            raise Unavailable("own roster ids missing or duplicated")
        champion_timelines = stats.get("championTimelines")
        team_timelines = stats.get("teamTimelines")
        if not isinstance(champion_timelines, list) or not isinstance(team_timelines, list):
            raise Unavailable("championTimelines or teamTimelines missing")
        if len(champion_timelines) + len(team_timelines) > MAX_TIMELINES:
            raise Unavailable("too many timelines for bounded screen")
        selected, missing = {}, []
        for cid in ids:
            for metric in CHAMPION_METRICS:
                candidates = [t for t in champion_timelines if isinstance(t, dict) and
                              isinstance(t.get("owner"), dict) and
                              t["owner"].get("kind") == "champion" and
                              t["owner"].get("id") == cid and t.get("metric") == metric]
                key = f"champion:{cid}:{metric}"
                if len(candidates) != 1:
                    missing.append(f"{key}: expected one timeline, found {len(candidates)}")
                    continue
                if candidates[0]["owner"].get("team") != team:
                    missing.append(f"{key}: owner team disagrees with roster")
                    continue
                selected[key] = candidates[0]
        team_candidates = [t for t in team_timelines if isinstance(t, dict) and
                           t.get("owner") == team and t.get("metric") == "submittedGems"]
        if len(team_candidates) != 1:
            missing.append(f"{team}:submittedGems: expected one timeline, found {len(team_candidates)}")
        else:
            selected[f"{team}:submittedGems"] = team_candidates[0]
        if missing:
            return {"status": "UNAVAILABLE", "reason": "required metric coverage incomplete",
                    "subject": subject, "team": team, "champion_ids": ids, "missing": missing}
        summaries, errors = {}, []
        for name, timeline in selected.items():
            try:
                summaries[name] = timeline_last_change(timeline, final_round)
            except Unavailable as exc:
                errors.append(f"{name}: {exc}")
        if errors:
            return {"status": "UNAVAILABLE", "reason": "timeline validation failed",
                    "subject": subject, "team": team, "champion_ids": ids, "errors": errors}
        last = max(v["last_change_round"] for v in summaries.values())
        tail = final_round - last
        return {"status": "SUSPECT" if tail >= min_tail else "NO_SUSPECT",
                "classification": "unconfirmed_statistics_tail" if tail >= min_tail else "no_long_final_counter_tail",
                "subject": subject, "team": team, "champion_ids": ids,
                "rounds_completed": completed, "final_observed_round": final_round,
                "final_round_partial": match.get("finalRoundPartial"),
                "last_change_round": last, "tail_rounds": tail, "threshold_rounds": min_tail,
                "last_changed_metrics": [key for key, value in summaries.items() if value["last_change_round"] == last],
                "metric_count": len(summaries), "metrics": summaries,
                "confirmation_required": ["native actions and positions", "travel/channel/wait/siege context"] if tail >= min_tail else []}
    except (Unavailable, TypeError, AttributeError) as exc:
        return {"status": "UNAVAILABLE", "reason": str(exc), "subject": subject}


def statistics_path(row, run):
    value = row.get("statistics") or row.get("stats") or row.get("replay")
    if not isinstance(value, str) or not value:
        raise Unavailable("row has no exact statistics/replay path")
    path = Path(value).with_suffix(".json")
    if path.is_absolute():
        return path
    choices = [ROOT / path, run / path, run / "games" / path.name]
    existing = [p.resolve() for p in choices if p.is_file()]
    unique = list(dict.fromkeys(existing))
    if len(unique) > 1:
        raise Unavailable(f"ambiguous retained statistics path: {value}")
    return unique[0] if unique else choices[0].resolve()


def screen_run(run, variant=None, min_tail=120):
    run = Path(run).resolve()
    log = run / "games.jsonl"
    if not log.is_file() or log.stat().st_size > MAX_JSON_BYTES:
        raise Unavailable(f"games.jsonl missing or too large: {log}")
    results, skipped, total_rows = [], 0, 0
    try:
        with log.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                total_rows += 1
                if total_rows > MAX_GAME_ROWS:
                    raise Unavailable(f"game rows exceed bounded count {MAX_GAME_ROWS}")
                try:
                    row = json.loads(line)
                    if not isinstance(row, dict):
                        raise ValueError("game row is not an object")
                except (ValueError, TypeError) as exc:
                    results.append({"line": line_number, "status": "UNAVAILABLE", "reason": f"invalid game row: {exc}"})
                    continue
                tags = row.get("tags") or {}
                if not isinstance(tags, dict):
                    tags = {}
                arm = tags.get("variant")
                if variant is not None and arm != variant:
                    skipped += 1
                    continue
                subject = tags.get("subject")
                details = {"line": line_number, "variant": arm, "map": row.get("map"),
                           "subject": subject, "opponent": tags.get("opponent"),
                           "subject_snapshot": row.get("a" if subject == "A" else "b") if subject in ("A", "B") else None,
                           "replay": row.get("replay"), "row_rounds": row.get("rounds")}
                try:
                    if subject not in ("A", "B"):
                        raise Unavailable("explicit tags.subject A/B is required")
                    if row.get("error") or row.get("truncated"):
                        raise Unavailable("arena game has an error or is truncated")
                    path = statistics_path(row, run)
                    details["statistics"] = str(path)
                    stats = read_json(path)
                    outcome = screen_statistics(stats, subject, min_tail)
                except (Unavailable, OSError) as exc:
                    outcome = {"status": "UNAVAILABLE", "reason": str(exc)}
                results.append({**details, **outcome})
    except OSError as exc:
        raise Unavailable(f"cannot read {log}: {exc}") from exc
    counts = Counter(row["status"] for row in results)
    coverage_complete = bool(results) and counts["UNAVAILABLE"] == 0
    variants = {}
    for row in results:
        key = str(row.get("variant"))
        variants.setdefault(key, Counter())[row["status"]] += 1
    return {"schema": "main-final-counter-tail-screen/v1", "run": str(run), "variant_filter": variant,
            "coverage_scope": "selected retained games.jsonl rows; planned job completion is not inferred",
            "coverage_notes": [] if results else ["no retained rows matched the requested variant"],
            "threshold_rounds": min_tail, "coverage_complete": coverage_complete,
            "rows_read": total_rows, "rows_skipped_by_variant": skipped, "selected_rows": len(results),
            "counts": dict(counts), "variants": {key: dict(value) for key, value in variants.items()},
            "limits": list(LIMITS), "games": results}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path)
    parser.add_argument("--variant", help="Select exactly tags.variant; subject remains explicit per row")
    parser.add_argument("--min-tail", type=int, default=120)
    parser.add_argument("--out", type=Path, help="Report directory or explicit .json filename")
    args = parser.parse_args(argv)
    if args.min_tail < 1:
        parser.error("--min-tail must be positive")
    try:
        report = screen_run(args.run, args.variant, args.min_tail)
    except (Unavailable, OSError) as exc:
        report = {"schema": "main-final-counter-tail-screen/v1", "run": str(args.run.resolve()),
                  "coverage_complete": False, "counts": {"UNAVAILABLE": 1}, "error": str(exc), "limits": list(LIMITS)}
    if args.out:
        target = args.out if args.out.suffix.lower() == ".json" else args.out / "report.json"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"report {target.resolve()}")
    print(f"coverage_complete={report['coverage_complete']} counts={json.dumps(report['counts'], sort_keys=True)}")
    for arm, counts in report.get("variants", {}).items():
        print(f"variant={arm} {json.dumps(counts, sort_keys=True)}")
    suspects = sorted([r for r in report.get("games", []) if r["status"] == "SUSPECT"], key=lambda r: -r["tail_rounds"])
    for game in suspects[:30]:
        print(f"SUSPECT variant={game['variant']} map={game['map']} side={game['subject']} last={game['last_change_round']} final={game['final_observed_round']} tail={game['tail_rounds']}")
    if len(suspects) > 30:
        print(f"... {len(suspects)-30} more suspects in report")
    return 0 if report["coverage_complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
