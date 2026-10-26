"""Saved-data activity triage. No games, subprocesses or health mutations.

Quiet means unchanged sampled x/y/HP/gold/gems while living. It is a candidate
for bounded inspection, never proof of readiness, a stall, refusal or admission.
The saved native runner's existing strict full-CAMSTAT validator remains required.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import zipfile

ENGINE = "1.0.3"
METRICS = ("monsterDamageDone", "championDamageDone", "earnedGold", "collectedGems",
           "submittedGems", "deaths", "kills")
INTEGER_METRICS = set(METRICS) - {"monsterDamageDone", "championDamageDone"}
EVENTS = ("campClear", "gemCollection", "gemSubmission", "death", "fuelExhausted")
FIELDS = ["round", "x", "y", "health", "gold", "gems"]

class ValidationError(ValueError):
    pass

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValidationError("duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValidationError("nonfinite JSON")))

def uint(value):
    if type(value) is int and value >= 0:
        return value
    if type(value) is str and re.fullmatch(r"[0-9]{1,20}", value):
        return int(value)
    raise ValidationError("missing/invalid unsigned number")

def number(value):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValidationError("missing/invalid finite nonnegative number")
    return value

def local(root, raw):
    if not isinstance(raw, str) or not raw:
        raise ValidationError("missing path")
    path = Path(raw)
    path = (path if path.is_absolute() else root / path).resolve()
    if not path.is_relative_to(root):
        raise ValidationError("evidence path leaves root")
    parts = [part.casefold() for part in path.parts]
    if any(part == "bots" and parts[i + 1] in {"main", "surprise", "contenders"}
           for i, part in enumerate(parts[:-1])):
        raise ValidationError("excluded source path")
    return path

def pinned(root, descriptor):
    if not isinstance(descriptor, dict) or not re.fullmatch(r"[0-9a-f]{64}", descriptor.get("sha256", "")):
        raise ValidationError("missing SHA256 evidence descriptor")
    size = descriptor.get("size", descriptor.get("bytes"))
    if type(size) is not int or size < 0:
        raise ValidationError("missing evidence size")
    path = local(root, descriptor.get("path"))
    raw = path.read_bytes()
    if len(raw) != size or digest(raw) != descriptor["sha256"]:
        raise ValidationError("evidence identity mismatch: " + str(path))
    return path, raw

def sample_activity(samples, maximum_round, minimum_samples=100):
    """Return exact sample boundaries; a death or missing round breaks a run.

    A run of N samples spans N-1 round labels. Equal longest runs retain the
    earliest one and report their tie count. No alive samples yields no interval.
    """
    if type(minimum_samples) is not int or minimum_samples < 2 or type(maximum_round) is not int:
        raise ValidationError("invalid interval bounds")
    if not isinstance(samples, list) or not samples:
        raise ValidationError("missing position samples")
    previous_round = None
    previous_state = None
    previous_living = False
    run = 0
    start = None
    best = None
    ties = 0
    living_count = dead_count = changes = gaps = 0
    visited = set()
    for row in samples:
        if not isinstance(row, list) or len(row) != 6:
            raise ValidationError("malformed position sample")
        round_, x, y, hp, gold, gems = row
        if type(round_) is not int or not 1 <= round_ <= maximum_round:
            raise ValidationError("invalid sample round")
        if previous_round is not None and round_ <= previous_round:
            raise ValidationError("nonmonotonic position samples")
        if type(x) is not int or type(y) is not int:
            raise ValidationError("invalid sample coordinates")
        number(hp)
        uint(gold)
        uint(gems)
        living = hp > 0
        if living and (x < 0 or y < 0):
            raise ValidationError("living sample has no position")
        contiguous = previous_round is not None and round_ == previous_round + 1
        if previous_round is not None and not contiguous:
            gaps += 1
        state = row[1:]
        if living:
            living_count += 1
            visited.add((x, y))
            if previous_living and contiguous and state[:2] != previous_state[:2]:
                changes += 1
            if previous_living and contiguous and state == previous_state:
                run += 1
            else:
                run, start = 1, round_
            interval = {"first_round": start, "last_round": round_, "sample_count": run,
                        "round_label_span": round_ - start, "state_xy_hp_gold_gems": state}
            if best is None or run > best["sample_count"]:
                best, ties = interval, 1
            elif run == best["sample_count"]:
                ties += 1
        else:
            dead_count += 1
            run, start = 0, None
        previous_round, previous_state, previous_living = round_, state, living
    return {"living_samples": living_count, "dead_samples": dead_count,
            "first_sample_round": samples[0][0], "last_sample_round": samples[-1][0],
            "sample_count": len(samples), "missing_round_gaps": gaps,
            "distinct_living_positions": len(visited), "living_sample_displacements": changes,
            "longest_unchanged_living_interval": best, "longest_interval_ties": ties,
            "meets_sample_threshold": best is not None and best["sample_count"] >= minimum_samples,
            "timestamp_basis": "owner-start sampled native round labels; input/substep timestamps are not in this sample format",
            "availability_channel_refusal": "unknown; samples contain none of these fields"}

def activity_tables(stats):
    """Validate additional dense activity data; absent fields stay unknown."""
    issues = []
    started = uint(stats["match"]["roundsStarted"])
    roster = {r["id"]: r for r in stats["roster"]["champions"]}
    champions = {r["id"]: r for r in stats["champions"]}
    timelines = {}
    timeline_rows = stats.get("championTimelines")
    if not isinstance(timeline_rows, list):
        issues.append("dense activity timeline table missing")
        timeline_rows = []
    for item in timeline_rows:
        owner = item.get("owner", {})
        if item.get("metric") not in METRICS:
            continue
        key = (owner.get("id"), item["metric"])
        if key in timelines:
            raise ValidationError("duplicate activity timeline")
        identity = roster.get(owner.get("id"))
        if identity is None or owner.get("kind") != "champion" or any(owner.get(k) != identity[k] for k in ("team", "name")):
            raise ValidationError("activity timeline owner mismatch")
        timelines[key] = item
    events = {}
    for group in stats["events"]:
        if group["kind"] not in EVENTS:
            continue
        owner = group.get("owner", {})
        key = (owner.get("id"), group["kind"])
        identity = roster.get(owner.get("id"))
        if identity is None or owner.get("kind") != "champion" or any(owner.get(k) != identity[k] for k in ("team", "name")):
            raise ValidationError("activity event owner mismatch")
        if key in events:
            raise ValidationError("duplicate activity event group")
        events[key] = group["entries"]
    result = {}
    for actor, identity in roster.items():
        observed, deltas, event_counts, event_sums = {}, {}, {}, {}
        actor_issues = []
        for metric in METRICS:
            value = champions[actor].get("metrics", {}).get(metric)
            try:
                observed[metric] = uint(value) if metric in INTEGER_METRICS else number(value)
            except ValidationError:
                observed[metric] = None
                actor_issues.append("final metric missing/invalid: " + metric)
            timeline = timelines.get((actor, metric))
            delta = None
            try:
                if not isinstance(timeline, dict) or uint(timeline["sampleCount"]) != started + 1:
                    raise ValidationError("dense current1.0.3 timeline must include initial sample")
                runs = timeline["runs"]
                if not isinstance(runs, list) or not runs:
                    raise ValidationError("missing runs")
                total, values = 0, []
                for run in runs:
                    duration = uint(run["duration"])
                    if duration == 0:
                        raise ValidationError("zero duration")
                    total += duration
                    values.append(uint(run["value"]) if metric in INTEGER_METRICS else number(run["value"]))
                if total != started + 1 or any(b < a for a, b in zip(values, values[1:])) or values[-1] != observed[metric]:
                    raise ValidationError("timeline coverage/final/cumulative value mismatch")
                delta = values[-1] - values[0]
            except (KeyError, TypeError, ValidationError):
                actor_issues.append("dense timeline unknown: " + metric)
            deltas[metric] = delta
        for kind in EVENTS:
            entries = events.get((actor, kind))
            if entries is None:
                event_counts[kind] = None
                event_sums[kind] = None
                actor_issues.append("dense activity event group missing: " + kind)
                continue
            if not isinstance(entries, list):
                raise ValidationError("malformed activity event entries")
            previous = None
            count = total_value = 0
            for entry in entries:
                stamp = entry.get("timestamp", {})
                if set(stamp) != {"round", "input", "substep", "phase", "ordinal"} or not isinstance(stamp.get("phase"), str) or not stamp["phase"]:
                    raise ValidationError("activity event timestamp incomplete")
                for field in ("round", "input", "substep", "ordinal"):
                    uint(stamp[field])
                moment = (stamp["round"], stamp["input"], stamp["ordinal"])
                if stamp["round"] > started or (previous is not None and moment < previous):
                    raise ValidationError("activity event timestamps nonmonotonic/out of bounds")
                previous = moment
                uint(entry["value"])
                if kind != "gemCollection" or entry.get("context") == "campGroup":
                    count += 1
                    total_value += uint(entry["value"])
            event_counts[kind] = count
            event_sums[kind] = total_value
        for kind, metric in (("gemSubmission", "submittedGems"), ("death", "deaths")):
            if event_sums.get(kind) is not None and observed[metric] is not None and event_sums[kind] != observed[metric]:
                actor_issues.append("dense event/final total differs: " + kind)
        issues.extend("actor %s: %s" % (actor, x) for x in actor_issues)
        result[actor] = {"actor": actor, "team": identity["team"], "champion": identity["name"],
                         "totals": observed, "whole_game_deltas": deltas,
                         "event_counts": event_counts, "event_value_sums": event_sums,
                         "capture_count_semantics": "gemCollection entries with native campGroup context; zero-gem gold payouts still count",
                         "issues": actor_issues}
    return result, issues

def analyze_saved(spec, root, minimum_samples=100):
    """Analyze one pinned compiled-runner case and an optional pinned position file."""
    root = Path(root).resolve()
    subject = spec.get("subject_snapshot")
    if not isinstance(subject, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{12}", subject):
        raise ValidationError("missing/malformed subject snapshot")
    if subject.split("/", 1)[0].casefold() in {"main", "surprise", "contenders"}:
        raise ValidationError("excluded bot identity")
    documents = {}
    paths = {}
    for name in ("run", "rows"):
        paths[name], raw = pinned(root, spec.get(name))
        documents[name] = raw
    run = strict_json(documents["run"])
    rows = [strict_json(line) for line in documents["rows"].splitlines() if line.strip()]
    selected = [r for r in rows if r.get("id") == spec.get("case")]
    planned = [r for r in run.get("planned_jobs", []) if r.get("id") == spec.get("case")]
    if len(selected) != 1 or len(planned) != 1:
        raise ValidationError("case identity missing/duplicate")
    row, plan = selected[0], planned[0]
    side = spec.get("subject_side")
    if side not in ("A", "B") or spec.get("subject_snapshot") != row["a" if side == "A" else "b"]:
        raise ValidationError("subject identity mismatch")
    for key in (row["a"], row["b"]):
        if key.split("/", 1)[0].casefold() in {"main", "surprise", "contenders"}:
            raise ValidationError("excluded bot identity")
    if any(plan[k] != row[k] for k in ("a", "b", "map")) or row.get("engine") != ENGINE:
        raise ValidationError("planned/native map or engine identity mismatch")
    engine = run.get("engine", {})
    if engine.get("version") != ENGINE:
        raise ValidationError("unsupported/mismatched engine")
    for name in ("statistics", "archive"):
        paths[name], documents[name] = pinned(root, spec.get(name))
    for name, key in (("statistics", "statistics"), ("archive", "archive")):
        expected = row.get(key, {})
        if expected.get("sha256") != spec[name]["sha256"] or local(root, expected.get("path")) != paths[name]:
            raise ValidationError("saved row artifact identity mismatch")
    role = run.get("selected_role")
    identity_issues = []
    if role not in ("raw", "injected"):
        identity_issues.append("consumed role unknown")
    elif spec.get("role") != role:
        raise ValidationError("consumed role identity mismatch")
    manifests = []
    for path in paths["run"].parent.glob("batches/*/input.jsonl"):
        manifests.extend(r for line in path.read_bytes().splitlines() if line.strip()
                         if (r := strict_json(line)).get("id") == spec["case"])
    if len(manifests) != 1:
        raise ValidationError("consumed batch job missing/duplicate")
    manifest = manifests[0]
    if local(root, manifest["replay"]) != paths["archive"]:
        raise ValidationError("consumed replay identity mismatch")
    map_path = local(root, manifest["map"])
    if digest(map_path.read_bytes()) != plan["map_sha256"]:
        raise ValidationError("consumed map bytes differ")
    bindings = {b["snapshot"]: b for b in run["bindings"]}
    if len(bindings) != len(run["bindings"]):
        raise ValidationError("duplicate consumed snapshot binding")
    consumed = {}
    for team, key in (("A", row["a"]), ("B", row["b"])):
        package = local(root, manifest["team" + team]["path"])
        if not package.is_relative_to(paths["run"].parent / "inputs"):
            raise ValidationError("package is not private consumed input")
        with zipfile.ZipFile(package) as z:
            if sorted(z.namelist()) != ["bot.wasm", "manifest.json"]:
                raise ValidationError("portable package members incomplete")
            portable = strict_json(z.read("manifest.json"))
            if portable != {"apiVersion": 1, "status": "ready", "language": "cpp"}:
                raise ValidationError("unsupported portable package")
            payload_hash = digest(z.read("bot.wasm"))
        binding = bindings[key]
        if role in ("raw", "injected") and (binding.get("selected_role") != role or binding[role]["sha256"] != payload_hash or binding.get("inner_bot_sha256") != payload_hash):
            raise ValidationError("consumed payload/role identity mismatch")
        consumed[team] = {"snapshot": key, "role": role, "payload_sha256": payload_hash}
    stats = strict_json(documents["statistics"])
    with zipfile.ZipFile(paths["archive"]) as z:
        brief = strict_json(z.read("result.json"))
    if brief != row["brief_result"] or stats["match"] != row["native_match"]:
        raise ValidationError("native archive/row/statistics identity mismatch")
    # Existing native completeness/health contract. No relaxed replacement here.
    sys.path.insert(0, str(root / "tools/arena"))
    import compiled_runner
    try:
        compiled_runner._statistics(stats, brief, plan["map_sha256"], engine)
    except (KeyError, TypeError, compiled_runner.ArenaError) as exc:
        raise ValidationError("existing native statistics contract rejected: " + str(exc)) from exc
    if row.get("execution_complete") is not True or row.get("measurement_complete") is not True:
        raise ValidationError("saved native completion unknown")
    actors, metric_issues = activity_tables(stats)
    team_name = "team" + side
    selected_actors = {a: r for a, r in actors.items() if r["team"] == team_name}
    position_issues = []
    if spec.get("positions") is None:
        position_issues.append("position evidence absent")
    else:
        _, raw = pinned(root, spec["positions"])
        doc = strict_json(raw)
        if doc.get("replaySha256") != spec["archive"]["sha256"] or doc.get("trackFields") != FIELDS or doc.get("team") != (0 if side == "A" else 1):
            raise ValidationError("position replay/fields/team identity mismatch")
        if doc.get("readOnly") is not True or doc.get("decoderProfile") != "cli-3.0.5" or doc.get("stopped") != "timeline_end" or doc.get("fromRound") != 0 or doc.get("toRound", -1) < uint(stats["match"]["roundsStarted"]):
            position_issues.append("full current position observation unknown")
        tracks = doc.get("tracks", [])
        if len({t["id"] for t in tracks}) != len(tracks) or {t["id"] for t in tracks} != set(selected_actors):
            raise ValidationError("position roster mismatch")
        for track in tracks:
            if track.get("team") != (0 if side == "A" else 1):
                raise ValidationError("position actor team mismatch")
            activity = sample_activity(track["samples"], uint(stats["match"]["roundsStarted"]), minimum_samples)
            if activity["first_sample_round"] != 1 or activity["last_sample_round"] not in {uint(stats["match"]["roundsCompleted"]), uint(stats["match"]["roundsStarted"])} or activity["missing_round_gaps"]:
                position_issues.append("actor %s position coverage unknown" % track["id"])
            selected_actors[track["id"]]["positions"] = activity
    complete = not identity_issues and not metric_issues and not position_issues
    for actor in selected_actors.values():
        position = actor.get("positions")
        actor["quiet_candidate"] = position["meets_sample_threshold"] if complete and position else None
        actor["classification"] = "quiet-candidate" if actor["quiet_candidate"] is True else "no-long-unchanged-living-sample-interval" if actor["quiet_candidate"] is False else "unknown"
    return {"schema": "arena-activity-metrics-prototype/v0", "status": "complete" if complete else "unknown",
            "case": spec["case"], "subject_side": side, "subject_snapshot": spec["subject_snapshot"],
            "native_match": stats["match"], "map_file_sha256": plan["map_sha256"], "consumed": consumed,
            "actors": list(selected_actors.values()), "issues": identity_issues + metric_issues + position_issues,
            "existing_team_health": row["health"], "health_rules_unchanged": True,
            "existing_health_scope": "saved whole-team flags only; not per-actor readiness or stall freedom",
            "minimum_quiet_samples": minimum_samples, "input_descriptors": spec,
            "limits": ["A quiet candidate is unchanged sampled position/HP/gold/gems while living, not an action-free or ready interval.",
                       "Damage/capture/bank totals can describe production outside the candidate interval; no action attribution is inferred.",
                       "Position displacements include recalls and mobility, not only ordinary moves. Gaps and deaths break intervals.",
                       "Readiness, channels, safety refusal, stalls and admission remain unknown without bounded native action/state evidence.",
                       "This reports saved existing health and never changes a health rule, release decision or admission."]}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-spec", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--quiet-min-samples", type=int, default=100)
    args = parser.parse_args()
    report = analyze_saved(strict_json(Path(args.case_spec).read_bytes()), args.root, args.quiet_min_samples)
    with Path(args.out).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"status": report["status"], "actors": len(report["actors"]),
                      "quiet_candidates": sum(a["quiet_candidate"] is True for a in report["actors"])}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
