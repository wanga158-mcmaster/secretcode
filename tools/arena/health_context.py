"""Bounded native exhaustion context from existing statistics; no games or engine queries.

Explicit capture: python tools/arena/health_context.py capture RUN --out NEW.json
Capture never edits the source run, a shared log, or admission/health metrics. Ladder uses
the same extractor after the frozen runner has retained its outputs temporarily.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import heapq
import json
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import ROOT, ArenaError, FileLock, map_path, read_jsonl  # noqa: E402
import games  # noqa: E402

SCHEMA = "arena-health-context/v1"
ARTIFACT = "health-context.json"
MAX_EVENTS = 32                 # retained native events per team; not a game allowance
MAX_STATS_BYTES = 64 * 1024 * 1024
CANONICAL = ("a", "b", "engine", "map", "winner", "rounds", "A", "B")
NATIVE_METRICS = ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted")
STAMP = ("round", "input", "substep", "phase", "ordinal")


def _sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _json(value) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def _relative(path: Path) -> str:
    path = Path(path).resolve()
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def _integer(value) -> bool:
    return type(value) is int and 0 <= value <= (1 << 64) - 1


def _text(value, maximum=128) -> bool:
    return type(value) is str and 0 < len(value) <= maximum


def canonical(row: dict) -> dict:
    return {key: copy.deepcopy(row.get(key)) for key in CANONICAL}


def producer() -> dict:
    """Keep producer bytes once per diagnostic artifact, rather than a mutable-path pin."""
    here = Path(__file__).resolve()
    runner = Path(games.__file__).resolve()
    raw, runner_raw = here.read_bytes(), runner.read_bytes()
    return {"module": {"original_path": _relative(here), "sha256": _sha(raw), "utf8": raw.decode("utf-8")},
            "canonical_runner": {"original_path": _relative(runner), "sha256": _sha(runner_raw),
                                 "utf8": runner_raw.decode("utf-8")}}


def extract(stats: dict, statistics_sha256: str, cap: int = MAX_EVENTS) -> dict:
    """Pure native fuelExhausted extraction. Missing fields never become actor/round zero.

    Coordinates/context/target are deliberately excluded: generic event zeros do not
    locate a champion or identify a guest function. The event ordinal is not a replay frame.
    """
    if type(cap) is not int or not 1 <= cap <= MAX_EVENTS:
        raise ValueError(f"event cap must be 1..{MAX_EVENTS}")
    if not isinstance(stats, dict):
        raise ValueError("statistics must be an object")
    if not isinstance(statistics_sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", statistics_sha256):
        raise ValueError("statistics SHA256 is missing or invalid")
    match = stats.get("match")
    match_fields = ("engineVersion", "engineBuildHash", "mapHash", "seed", "metricSemanticsVersion",
                    "roundsCompleted", "roundsStarted", "finalInput", "finalSubstep", "finalRoundPartial",
                    "outcome", "width", "height")
    native_match = {}
    if isinstance(match, dict):
        for key in match_fields:
            value = match.get(key)
            if (type(value) in (int, bool) or _text(value, 128)):
                native_match[key] = value
    result = {"schema": SCHEMA, "status": "complete", "statistics_sha256": statistics_sha256,
              "formats": {key: stats.get(key) if _text(stats.get(key), 32) else None
                          for key in ("formatVersion", "jsonFormatVersion")},
              "native_match": native_match, "event_kind": "fuelExhausted",
              "cap_per_team": cap, "teams": {}, "unknown_team_entries": 0,
              "invalid_examples": [], "issues": [],
              "limit": "Native actor and engine event time only; no position, guest callsite, private state or admission inference."}
    diagnostics = stats.get("diagnostics")
    detailed = diagnostics.get("detailedCaptureComplete") if isinstance(diagnostics, dict) else None
    result["detailed_capture_complete"] = detailed if type(detailed) is bool else None
    if detailed is not True:
        result["issues"].append("native detailed capture is not explicitly complete")
    required_match = {
        "engineVersion": lambda v: _text(v, 32),
        "engineBuildHash": lambda v: type(v) is str and bool(re.fullmatch(r"[0-9a-fA-F]{64}", v)),
        "mapHash": lambda v: type(v) is str and bool(re.fullmatch(r"[0-9a-fA-F]{64}", v)),
        "seed": lambda v: _integer(v) or (type(v) is str and len(v) <= 20 and v.isascii() and v.isdigit() and int(v) <= (1 << 64) - 1),
        "metricSemanticsVersion": _integer,
    }
    for field, valid_field in required_match.items():
        if not isinstance(match, dict) or not valid_field(match.get(field)):
            native_match.pop(field, None)
            result["issues"].append("native match identity missing/invalid: " + field)
    if any(not _text(value, 32) for value in result["formats"].values()):
        result["issues"].append("native statistics format unavailable")
    roster = (stats.get("roster") or {}).get("champions")
    identities = {}
    if isinstance(roster, list):
        for row in roster:
            if not isinstance(row, dict) or not _integer(row.get("id")):
                result["issues"].append("malformed roster identity")
                continue
            actor = row["id"]
            if actor in identities:
                identities[actor] = None
                result["issues"].append("duplicate roster actor")
            else:
                identities[actor] = row
    else:
        result["issues"].append("roster unavailable")
    buckets = {side: {"total": 0, "valid": 0, "invalid": 0, "kept": 0,
                      "omitted": 0, "events": []} for side in ("A", "B")}
    valid = {side: [] for side in buckets}
    groups = stats.get("events")
    if not isinstance(groups, list):
        groups = []
        result["issues"].append("native event groups unavailable")
    for group_index, group in enumerate(groups):
        if not isinstance(group, dict) or group.get("kind") != "fuelExhausted":
            continue
        owner, entries = group.get("owner"), group.get("entries")
        owner = owner if isinstance(owner, dict) else {}
        side = {"teamA": "A", "teamB": "B"}.get(owner.get("team"))
        if not isinstance(entries, list):
            result["issues"].append("exhaustion entry list unavailable")
            continue
        for entry_index, entry in enumerate(entries):
            if side:
                buckets[side]["total"] += 1
            else:
                result["unknown_team_entries"] += 1
            issues = []
            if side is None:
                issues.append("owner.team")
            if not _integer(owner.get("id")):
                issues.append("owner.id")
            if not _text(owner.get("kind"), 32):
                issues.append("owner.kind")
            if not _text(owner.get("name")):
                issues.append("owner.name")
            stamp = entry.get("timestamp") if isinstance(entry, dict) else None
            if not isinstance(stamp, dict):
                stamp = {}
            for field in STAMP:
                if not (_text(stamp.get(field), 32) if field == "phase" else _integer(stamp.get(field))):
                    issues.append("timestamp." + field)
            joined = identities.get(owner.get("id")) if _integer(owner.get("id")) else None
            if owner.get("kind") == "champion":
                if (not isinstance(joined, dict) or joined.get("team") != owner.get("team") or
                        joined.get("name") != owner.get("name") or not _integer(joined.get("draftPick"))):
                    issues.append("roster join")
            if issues:
                if side:
                    buckets[side]["invalid"] += 1
                if len(result["invalid_examples"]) < 8:
                    result["invalid_examples"].append({"group_index": group_index, "entry_index": entry_index,
                                                       "team": side, "fields": issues})
                continue
            record = {"owner": {key: owner[key] for key in ("id", "team", "kind", "name")},
                      "timestamp": {key: stamp[key] for key in STAMP},
                      "roster_join": ({"status": "matched", **{key: joined[key] for key in ("id", "team", "name", "draftPick")}}
                                      if owner["kind"] == "champion" else {"status": "not_champion"})}
            valid[side].append((stamp["ordinal"], stamp["input"], stamp["substep"], group_index, entry_index, record))
    for side, bucket in buckets.items():
        kept = heapq.nsmallest(cap, valid[side], key=lambda item: item[:-1])
        bucket.update(valid=len(valid[side]), kept=len(kept), omitted=len(valid[side]) - len(kept),
                      events=[item[-1] for item in kept])
    result["teams"] = buckets
    result["issues"] = sorted(set(result["issues"]))
    if (result["issues"] or result["unknown_team_entries"] or
            any(x["invalid"] or x["omitted"] for x in buckets.values())):
        result["status"] = "partial"
    return result


def _unavailable(reason: str) -> dict:
    return {"schema": SCHEMA, "status": "unavailable", "reason": reason[:300],
            "event_kind": "fuelExhausted", "teams": None,
            "limit": "No actor or engine timestamp inferred from aggregate health/fuel."}


def _source_paths(row: dict, run: Path) -> tuple[Path, Path]:
    value = row.get("replay")
    if not isinstance(value, str) or not value:
        raise ValueError("row has no replay/statistics location")
    replay = Path(value)
    replay = (replay if replay.is_absolute() else ROOT / replay).resolve()
    directory = (run / "games").resolve()
    if not replay.is_relative_to(directory) or replay.suffix != ".repl26a":
        raise ValueError("replay is outside this run's games directory")
    return replay, replay.with_suffix(".json")


def capture_row(row: dict, run: Path, publication_id: str, cap: int = MAX_EVENTS) -> dict:
    copied = copy.deepcopy(row)
    try:
        replay, stats_path = _source_paths(row, run)
        if stats_path.stat().st_size > MAX_STATS_BYTES:
            raise ValueError("statistics exceed capture size bound")
        raw = stats_path.read_bytes()
        stats = json.loads(raw.decode("utf-8"))
        parsed = games.parse_statistics(stats)
        if parsed["winner"] != row.get("winner") or parsed["rounds"] != row.get("rounds"):
            raise ValueError("statistics outcome/rounds disagree with canonical row")
        for side in "AB":
            if any(parsed[side][key] != (row.get(side) or {}).get(key) for key in NATIVE_METRICS):
                raise ValueError("statistics native metrics disagree with canonical row")
        if row.get("engine") is not None and stats["match"].get("engineVersion") != row["engine"]:
            raise ValueError("statistics engine disagrees with canonical row")
        declared_map = row.get("map")
        if not _text(declared_map, 1024):
            raise ValueError("canonical row has no valid declared map")
        expected_map_sha256 = _sha(map_path(declared_map).read_bytes())
        native_map_sha256 = stats["match"].get("mapHash")
        if not isinstance(native_map_sha256, str) or native_map_sha256.lower() != expected_map_sha256:
            raise ValueError("statistics map hash disagrees with declared map bytes")
        context = extract(stats, _sha(raw), cap)
        context["declared_map_sha256"] = expected_map_sha256
        context["statistics_name"] = stats_path.name
        context["replay_sha256"] = _sha(replay.read_bytes()) if replay.is_file() else None
        if stats_path.read_bytes() != raw:
            raise ValueError("statistics changed during capture")
    except Exception as exc:
        # Only retained-file reading/extraction is inside this try. Publication
        # I/O happens separately and must propagate instead of becoming metadata.
        context = _unavailable(f"{type(exc).__name__}: {exc}")
    context["publication_id"] = publication_id
    copied["health_context"] = context
    assert canonical(copied) == canonical(row)
    return copied


def _raw_rows(run: Path) -> tuple[bytes, list[dict], list[str]]:
    path = run / "games.jsonl"
    if not path.is_file():
        return b"", [], []
    raw = path.read_bytes()
    rows, issues = [], []
    for index, line in enumerate(raw.splitlines()):
        if not line.strip():
            continue
        try:
            item = json.loads(line)
        except (ValueError, UnicodeError):
            issues.append(f"unreadable raw game row {index + 1}; only the completed prefix was captured")
            break
        if not isinstance(item, dict):
            issues.append(f"raw game row {index + 1} is not an object; only the completed prefix was captured")
            break
        rows.append(item)
    return raw, rows, issues


def _publication_id(run: Path, index: int, row: dict) -> str:
    # Per-run row occurrence, not a global game-key dedupe: legitimate retries survive.
    return _sha(_json([str(run.resolve()), index, row]))


def _write_artifact(path: Path, artifact: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = _json(artifact)
    # A failed write leaves its fresh temporary file for inspection. Existing
    # output records are verified by capture_run and never silently overwritten.
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix="." + path.name + ".", suffix=".tmp", delete=False) as fh:
        temporary = Path(fh.name)
        fh.write(raw)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(temporary, path)


def capture_run(run: Path, output: Path | None = None, cap: int = MAX_EVENTS) -> dict:
    """Capture existing rows to one artifact, idempotently. Never clean or publish shared rows."""
    run = Path(run).resolve()
    output = Path(output).resolve() if output else run / ARTIFACT
    if output == run / "games.jsonl":
        raise ArenaError("capture output cannot replace its raw game log")
    with FileLock(output.with_name(output.name + ".lock")):
        raw, rows, log_issues = _raw_rows(run)
        fingerprint = producer()
        if output.is_file():
            artifact = json.loads(output.read_bytes())
            if (artifact.get("schema") != SCHEMA or artifact.get("raw_log_sha256") != _sha(raw) or
                    artifact.get("run") != _relative(run) or artifact.get("cap_per_team") != cap or
                    artifact.get("producer") != fingerprint or artifact.get("source_log_issues") != log_issues or
                    len(artifact.get("rows", [])) != len(rows)):
                raise ArenaError("existing diagnostic artifact has different inputs/producer; choose a new output")
            for index, (original, captured) in enumerate(zip(rows, artifact["rows"])):
                if (canonical(original) != canonical(captured) or
                        (captured.get("health_context") or {}).get("publication_id") != _publication_id(run, index, original)):
                    raise ArenaError("existing diagnostic rows disagree with raw producer log")
            return artifact
        captured = [capture_row(row, run, _publication_id(run, index, row), cap) for index, row in enumerate(rows)]
        for row in captured:
            row["health_context"]["producer_sha256"] = {
                name: part["sha256"] for name, part in fingerprint.items()}
        artifact = {"schema": SCHEMA, "run": _relative(run), "raw_log_sha256": _sha(raw),
                    "producer": fingerprint, "cap_per_team": cap, "rows": captured, "source_log_issues": log_issues,
                    "counts": {status: sum(r["health_context"]["status"] == status for r in captured)
                               for status in ("complete", "partial", "unavailable")},
                    "scope": "Retained native health context only; canonical game metrics and admission are unchanged."}
        if _raw_rows(run)[0] != raw:
            raise ArenaError("raw run log changed during capture; source retained")
        _write_artifact(output, artifact)
        return artifact


def _append_rows_unlocked(path: Path, rows: list[dict]) -> None:
    torn = path.is_file() and path.stat().st_size and not path.read_bytes().endswith(b"\n")
    with path.open("ab") as fh:
        if torn:
            fh.write(b"\n")
        for row in rows:
            fh.write(_json(row))
            fh.flush()
        os.fsync(fh.fileno())


def _publish_once(log: Path, rows: list[dict]) -> None:
    log = Path(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    with FileLock(log.with_name(log.name + ".lock")):
        known = {}
        for row in read_jsonl(log):
            if isinstance(row, dict):
                context = row.get("health_context")
                identity = context.get("publication_id") if isinstance(context, dict) else None
                if identity:
                    known[identity] = canonical(row)
        pending = []
        for row in rows:
            identity = row["health_context"]["publication_id"]
            if identity in known:
                if known[identity] != canonical(row):
                    raise ArenaError("health-context publication identity collision")
            else:
                pending.append(row)
                known[identity] = canonical(row)
        if pending:
            _append_rows_unlocked(log, pending)


def _cleanup_flagged(run: Path, rows: list[dict]) -> list[str]:
    """Remove only verified files under this regenerable run. Never follow an imported path."""
    cache = (ROOT / ".cache").resolve()
    run = run.resolve()
    if not run.is_relative_to(cache) or run == cache:
        raise ArenaError("physical cleanup is restricted to a ladder run inside .cache")
    targets, recorded = [], set()
    for row in rows:
        if not row.get("replay"):
            continue
        replay, stats = _source_paths(row, run)
        recorded.add(replay)
        context = row["health_context"]
        for path, expected in ((stats, context.get("statistics_sha256")),
                               (replay, context.get("replay_sha256"))):
            resolved = path.resolve()
            if not resolved.is_relative_to(run / "games") or not resolved.is_relative_to(cache):
                raise ArenaError("cleanup target escaped this regenerable run")
            if path.is_file() and (not expected or _sha(path.read_bytes()) != expected):
                raise ArenaError("cleanup input changed after capture; source retained")
        targets.append(stats)
        if not games.flagged(row):
            targets.append(replay)
    failures = []
    for path in targets:
        # Re-resolve immediately before each unlink, including on Windows.
        resolved = path.resolve()
        if not resolved.is_relative_to(run / "games") or not resolved.is_relative_to(cache):
            raise ArenaError("cleanup target escaped before unlink")
        try:
            resolved.unlink(missing_ok=True)
        except OSError as exc:
            failures.append(f"{_relative(path)}: {exc}")
    # The frozen keep-all runner retains manifests even for clean chunks. Restore
    # flagged retention only for manifests wholly covered by recorded rows.
    for manifest in (run / "games").glob("batch-*.txt"):
        resolved = manifest.resolve()
        if not resolved.is_relative_to(run / "games") or not resolved.is_relative_to(cache):
            raise ArenaError("manifest cleanup target escaped this regenerable run")
        try:
            tokens = [line.split() for line in manifest.read_text(encoding="utf-8").splitlines() if line.strip()]
            if not tokens or any(len(t) != 7 or t[4] != "--statistics" or t[6] != "--json" for t in tokens):
                continue
            replays = [Path(t[3]).resolve() for t in tokens]
            if all(p in recorded and not p.exists() for p in replays):
                resolved.unlink(missing_ok=True)
        except OSError as exc:
            failures.append(f"{_relative(manifest)}: {exc}")
    return failures


def finalize_ladder_run(run: Path, log: Path) -> list[dict]:
    """Publish completed raw rows exactly once, then apply physical flagged retention.

    Called even after play raises. Raw files from an unrecorded current chunk are never
    invented, captured or removed. Publication failures propagate and prevent cleanup.
    """
    run = Path(run).resolve()
    artifact = capture_run(run)
    rows = artifact["rows"]
    _publish_once(Path(log), rows)
    if artifact["counts"]["partial"] or artifact["counts"]["unavailable"] or artifact["source_log_issues"]:
        print(f"health context: incomplete capture; retained raw artifacts in {_relative(run)}", file=sys.stderr)
    else:
        failures = _cleanup_flagged(run, rows)
        for failure in failures:
            print("health context cleanup retained " + failure, file=sys.stderr)
    # The durable copies describe the raw producer path. Returned clean rows can use
    # the same null replay convention as the prior ladder after physical cleanup.
    returned = copy.deepcopy(rows)
    for row in returned:
        if row.get("replay") and not games.flagged(row):
            path = Path(row["replay"])
            path = path if path.is_absolute() else ROOT / path
            if not path.exists():
                row["replay"] = None
    return returned


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    capture = sub.add_parser("capture", help="capture an existing keep-all run without games, cleanup or shared-log writes")
    capture.add_argument("run", type=Path)
    capture.add_argument("--out", type=Path, required=True, help="new diagnostic artifact (source run stays unchanged)")
    args = parser.parse_args(argv)
    try:
        artifact = capture_run(args.run, args.out)
    except (OSError, ValueError, ArenaError) as exc:
        print(f"health context error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"output": _relative(args.out), "counts": artifact["counts"], "rows": len(artifact["rows"]),
                      "native_queries": 0, "games": 0, "builds": 0}))
    return 0 if not (artifact["counts"]["partial"] or artifact["counts"]["unavailable"] or artifact["source_log_issues"]) else 2


if __name__ == "__main__":
    sys.exit(main())
