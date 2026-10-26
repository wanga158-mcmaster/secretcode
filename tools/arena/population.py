"""Exact-version behavior catalog and reproducible local regression panels.

Suite adapters expose contracts() -> list[dict], and may expose
evaluate(contract, evidence, root=ROOT) -> dict. evaluate must reconstruct native
positive/refusal/distinct/fresh observations from the pinned artifacts; it must
return each role as {status: 'pass', artifacts: [artifact names], ...}. Positive
requires opportunities/activations/continuations > 0, refusal requires refusals
> 0, distinct requires a physical_difference and replacement_removes_coverage,
fresh requires activations/continuations > 0 and a distinct (map, opponent).
Positive/fresh condition is {map, map_sha256, opponent_snapshot}; changing only
the round or side is not fresh applicability.
No generic strategy or log-message-only observer is supplied here.

Evidence JSON schema 1: behavior, identity (from identity()), artifacts mapping
names to {path, sha256}, build {status, snapshot}, tests {status, artifact},
health {artifact, opponents: [snapshot keys]}, and witness inputs for the adapter.
Health artifact is arena games.jsonl, with exact subject/opponent keys and engine.
health.maps binds map names/hashes. The tests artifact is JSON with exit_code=0,
test_cases>0, runner='bots/<suite>/tests/run.py', snapshot and
identity=canonical(identity(contract)), captured around an actual production test
invocation at a stable source boundary. A zero-exit runner with zero cases fails.
For a variant, those tests must execute the applied variant; a default-off test
result cannot be relabeled with the variant identity.
All referenced artifacts must be pinned; a suite observer must independently
reconstruct its witnesses. Evidence is kept under results/population/evidence/.
Experimental panels are explicitly unadmitted and never become ready by playing.

A health row artifact descriptor may add subject_snapshot: the exact current
identity.snapshot, never a bot ID or another variant. The entire JSONL file is
hash-verified and every nonblank row must be an object. Only rows whose physical
a or b key equals that snapshot are selected; empty or ambiguous self-play
selections fail. Omit subject_snapshot to retain strict all-rows validation.
Admission uses artifacts[health.artifact]; diagnostics use record.rows. Reports
include selection counts and the full-file hash. Behavior evaluators must still
select their native witnesses independently; this selector applies to health.

Optional contract.overrides is an exact dict[str,str] (omitted means {}; null is
invalid). identity.working_source hashes the raw working closure; identity.source
hashes its applied variant. identity.overrides and the snapshot key bind the exact
override values. Historical panels replay the pinned applied closure: their raw
working_source is bound provenance, not an independent rehash of historical bytes.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

import core
from core import ArenaError, Bot, Entrant, ROOT, CACHE

SCHEMA = 1
EXCLUDED = {"main", "anchors", "surprise", "contenders"}
CONTRACT_FIELDS = ("id", "bot", "interaction", "preconditions", "activation", "continuation",
                   "refusal", "control", "neighbor", "positive_fixture", "refusal_fixture", "observer")
ROLE_FIELDS = ("positive", "refusal", "distinct", "fresh")
OFFICIAL_ASSETS = Path(os.environ.get("LOCALAPPDATA") or core.HOME / "AppData/Local") / "cambc/assets"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _path(value: str | Path) -> Path:
    p = Path(value)
    p = (ROOT / p).resolve() if not p.is_absolute() else p.resolve()
    if not p.is_relative_to(ROOT.resolve()):
        raise ArenaError(f"population path outside repository: {value}")
    return p


def _rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def _write(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def _load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ArenaError(f"unavailable population record {_rel(path)}: {exc}") from exc


def _adapter(suite: str):
    path = ROOT / "bots" / suite / "tests" / "population.py"
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location("arena_population_" + suite, path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as exc:
        raise ArenaError(f"population adapter {suite} failed: {exc}") from exc
    return module


def _audit() -> dict:
    for path in (ROOT / "results/population/inventory.json", CACHE / "orchestration/inventory-audit.json"):
        if path.is_file():
            data = _load(path)
            rows = data if isinstance(data, list) else data.get("bots", [])
            return {r["bot"]: r for r in rows if isinstance(r, dict) and "bot" in r}
    return {}


def _audit_provenance() -> dict:
    path = ROOT / "results/population/inventory.json"
    if not path.is_file():
        return {"valid": False, "reason": "no durable audit"}
    try:
        data = _load(path)
        source = data.get("observation_sources") or {}
        ledger = _path(source["ladder"])
        count = source["ladder_bytes_read"]
        if type(count) is not int or count < 0:
            raise ArenaError("invalid audit ledger byte count")
        with ledger.open("rb") as stream:
            prefix = stream.read(count)
        valid = (len(prefix) == count and hashlib.sha256(prefix).hexdigest() == source["ladder_sha256"] and
                 source["bundle_sha256"] == digest(ROOT / "docs/reference/current-version.json"))
        return {"valid": valid, "engine": source.get("engine"), "ledger": source["ladder"],
                "bytes": count, "sha256": source["ladder_sha256"],
                "reason": "pinned ledger prefix and bundle verified" if valid else "audit ledger prefix or bundle changed"}
    except (ArenaError, OSError, KeyError, TypeError, AttributeError) as exc:
        return {"valid": False, "reason": str(exc)}


def contracts(selector: str | None = None) -> list[dict]:
    """Every in-scope bot appears, including explicit gaps for absent adapters."""
    bots = [b for b in core.select(selector or "all") if b.suite.casefold() not in EXCLUDED and not b.name.startswith("_")]
    audit, adapters, out = _audit(), {}, []
    for b in bots:
        if b.suite not in adapters:
            module = _adapter(b.suite)
            adapters[b.suite] = list(module.contracts()) if module else []
        declared = [dict(c) for c in adapters[b.suite] if c.get("bot") == b.id]
        if declared:
            out.extend(declared)
        else:
            row = audit.get(b.id, {})
            out.append({"id": b.id, "bot": b.id,
                        "interaction": row.get("interaction") or row.get("declared_interaction") or
                                       "Unassessed: read the suite charter and production path",
                        "gap": row.get("gap") or row.get("gaps") or "No executable behavior contract or native observer",
                        "next_check": row.get("next_check") or "Define a reachable production fixture and native witness",
                        "control": row.get("control", ""), "neighbor": row.get("neighbor", ""),
                        "scope": row.get("scope", "unverified intent"), "adapter_missing": True,
                        "externally_owned": row.get("externally_owned", b.suite == "contenders"),
                        "ownership_note": row.get("ownership_note", "")})
    ids = [c.get("id") for c in out]
    if len(ids) != len(set(ids)):
        raise ArenaError("duplicate population behavior IDs")
    return out


def _files(paths) -> dict:
    found = {}
    for value in paths:
        p = _path(value)
        if p.is_dir():
            found.update({_rel(f): digest(f) for f in p.rglob("*") if f.is_file() and
                          "__pycache__" not in f.parts and f.suffix != ".pyc"})
        elif p.is_file():
            found[_rel(p)] = digest(p)
        else:
            found[_rel(p)] = None
    return dict(sorted(found.items()))


def _measurement_files(contract: dict) -> dict:
    """Rediscover the configured test/observer roots without reading bot sources."""
    ident = contract["bot"]
    if not isinstance(ident, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", ident):
        raise ArenaError("invalid contract bot identity")
    suite, name = ident.split("/", 1)
    bot_tests = ROOT / "bots" / suite / name / "tests"
    tests = [f"bots/{suite}/tests", "tools/arena/wasi.py", "tools/arena/wasi-run.mjs", "tools/arena/gate.py"] + list(contract.get("test_paths", []))
    if bot_tests.is_dir():
        tests.append(_rel(bot_tests))
    observers = ["tools/arena/population.py", "tools/arena/games.py"] + list(contract.get("observer_paths", []))
    observer = contract.get("observer")
    if isinstance(observer, str) and (ROOT / observer).is_file():
        observers.append(observer)
    return {"tests": _files(tests), "observers": _files(observers)}


def _overrides(value, where: str = "contract") -> dict[str, str]:
    if type(value) is not dict or any(type(k) is not str or type(v) is not str for k, v in value.items()):
        raise ArenaError(f"{where} overrides must be a dict of string names to string values (null is invalid)")
    return dict(sorted(value.items()))


def _source_hashes(contents: dict[Path, bytes]) -> dict[str, str]:
    return {_rel(p): hashlib.sha256(data).hexdigest() for p, data in contents.items()}


def identity(contract: dict) -> dict:
    ident = contract["bot"]
    if not isinstance(ident, str) or ident.split("/", 1)[0].casefold() in EXCLUDED:
        raise ArenaError("contract bot is outside the population mandate")
    overrides = _overrides(contract.get("overrides", {}))
    b = core.bot(ident)
    raw = core.read_closure(b)
    contents = core.apply_overrides(b, raw, overrides) if overrides else raw
    source = _source_hashes(contents)
    measurement = _measurement_files(contract)
    return {"snapshot": f"{b.id}@{core.version_of(contents, overrides)}", "source": source,
            "working_source": _source_hashes(raw), "overrides": overrides,
            "contract": canonical(contract), "toolchain": core.toolchain(),
            "bundle": digest(ROOT / "docs/reference/current-version.json"),
            "engine": _load(ROOT / "docs/reference/current-version.json")["engine"],
            **measurement,
            "maps": {m: digest(core.map_path(m)) for m in core.pool_maps()}}


def evidence_path(contract: dict) -> Path:
    slug = re.sub(r"[^A-Za-z0-9._-]", "--", contract["id"])
    return ROOT / "results/population/evidence" / (slug + ".json")


def _snapshot(key: str, *, snapshot_root=None, snapshot_owner=None) -> Entrant:
    """Load an existing immutable snapshot; never substitute current working sources."""
    if snapshot_root is None and snapshot_owner is not None:
        raise ArenaError("snapshot owner requires an explicit root")
    try:
        ident, version = key.rsplit("@", 1)
        suite, name = ident.split("/", 1)
    except (ValueError, AttributeError) as exc:
        raise ArenaError(f"invalid snapshot key: {key!r}") from exc
    if not re.fullmatch(r"[0-9a-f]{12}", version) or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", ident):
        raise ArenaError(f"invalid snapshot key: {key!r}")
    if suite.casefold() in EXCLUDED - {"anchors"}:
        raise ArenaError("snapshot bot is outside the population mandate")
    snap = (CACHE / "snap" if snapshot_root is None else core.resolve_snapshot_root(snapshot_root, snapshot_owner)) / version
    if snapshot_root is not None and snap.resolve() != snap:
        raise ArenaError("snapshot directory is redirected")
    meta = _load(snap / "meta.json")
    if meta.get("bot") != ident or meta.get("version") != version:
        raise ArenaError(f"snapshot identity mismatch: {key}")
    if not (snap / ".ready").is_file() or not (snap / ".toolchain").is_file():
        raise ArenaError(f"snapshot unavailable or unbuilt: {key}")
    if (snap / ".toolchain").read_text(encoding="utf-8").strip() != core.toolchain():
        raise ArenaError(f"snapshot toolchain is stale: {key}")
    overrides = _overrides(meta.get("overrides", {}), "snapshot metadata")
    ent = Entrant(Bot(suite, name), version, snap / "bots" / suite / name, True,
                  overrides=tuple(overrides.items()))
    source = {ROOT / rel: (snap / rel).read_bytes() for rel in
              _snapshot_files(ent, **({"snapshot_root": snapshot_root, "snapshot_owner": snapshot_owner} if snapshot_root is not None else {}))}
    if not source or core.version_of(source, dict(ent.overrides)) != version:
        raise ArenaError(f"snapshot source digest mismatch: {key}")
    return ent


def _snapshot_files(ent: Entrant, *, snapshot_root=None, snapshot_owner=None) -> dict:
    """Hash copied source files, excluding mutable build outputs and SDK junctions."""
    if snapshot_root is None and snapshot_owner is not None:
        raise ArenaError("snapshot owner requires an explicit root")
    snap = (CACHE / "snap" if snapshot_root is None else core.resolve_snapshot_root(snapshot_root, snapshot_owner)) / ent.version
    if snapshot_root is not None:
        if snap.resolve() != snap or ent.path != snap / "bots" / ent.bot.suite / ent.bot.name:
            raise ArenaError("snapshot source directory is redirected or mismatched")
        files, pending = set(), [ent.path / "src"]
        while pending:
            directory = pending.pop()
            if directory.resolve() != directory or core._is_reparse(directory):
                raise ArenaError("snapshot source directory is redirected")
            with os.scandir(directory) as entries:
                for entry in entries:
                    path = Path(entry.path)
                    if path.name == ".cambc":
                        continue
                    if path.resolve() != path or core._is_reparse(path):
                        raise ArenaError("snapshot source file is redirected")
                    if entry.is_dir(follow_symlinks=False):
                        pending.append(path)
                    elif entry.is_file(follow_symlinks=False):
                        files.add(path)
    else:
        files = {p.resolve() for p in (ent.path / "src").rglob("*") if p.is_file() and ".cambc" not in p.parts}
    stack = [p for p in files if p.suffix in core.SOURCE_SUFFIXES]
    while stack:
        source = stack.pop()
        for include in core.INCLUDE.findall(source.read_text(encoding="utf-8", errors="surrogateescape")):
            lexical = Path(os.path.abspath(source.parent / include))
            candidate = lexical.resolve()
            if snapshot_root is not None and candidate.is_relative_to(snap) and lexical != candidate:
                raise ArenaError("snapshot source include is redirected")
            if candidate.is_file() and candidate not in files and candidate.is_relative_to(snap.resolve()):
                files.add(candidate)
                if candidate.suffix in core.SOURCE_SUFFIXES:
                    stack.append(candidate)
    return {p.relative_to(snap.resolve()).as_posix(): digest(p) for p in sorted(files)}


def _artifact(record: dict) -> Path:
    if not isinstance(record, dict) or set(("path", "sha256")) - set(record):
        raise ArenaError("artifact needs path and sha256")
    path = _path(record["path"])
    if not path.is_file() or digest(path) != record["sha256"]:
        raise ArenaError(f"missing or changed artifact: {record['path']}")
    return path


class _HealthSelectionError(ArenaError):
    def __init__(self, message: str, selection: dict):
        super().__init__(message)
        self.selection = selection


def read_health_rows(descriptor: dict, subject_snapshot: str) -> tuple[list[dict], dict]:
    """Read one pinned full log, with an optional exact-subject health selection."""
    path = _artifact(descriptor)
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != descriptor["sha256"]:
        raise ArenaError("health artifact changed while reading the full file")
    rows = [json.loads(line) for line in raw.decode("utf-8").splitlines() if line.strip()]
    if any(not isinstance(row, dict) for row in rows):
        raise ArenaError("health artifact rows must all be objects, including excluded rows")
    explicit = "subject_snapshot" in descriptor
    if explicit and (type(descriptor["subject_snapshot"]) is not str or
                     descriptor["subject_snapshot"] != subject_snapshot):
        raise ArenaError("health artifact subject_snapshot selector must exactly equal the current snapshot")
    selected = ([row for row in rows if row.get("a") == subject_snapshot or row.get("b") == subject_snapshot]
                if explicit else rows)
    selection = {"mode": "exact-subject" if explicit else "all-rows",
                 "subject_snapshot": descriptor["subject_snapshot"] if explicit else None,
                 "expected_subject_snapshot": subject_snapshot, "total_rows": len(rows),
                 "included_rows": len(selected), "excluded_rows": len(rows) - len(selected),
                 "artifact": {"path": _rel(path), "sha256": descriptor["sha256"]}}
    if not selected:
        raise _HealthSelectionError("health artifact has no selected games", selection)
    if explicit and any(row.get("a") == subject_snapshot == row.get("b") for row in selected):
        raise _HealthSelectionError("exact-subject health selection is ambiguous for self-play", selection)
    return selected, selection


def _valid_condition(condition, ident: dict) -> bool:
    if not isinstance(condition, dict) or not all(isinstance(condition.get(k), str) and condition[k]
            for k in ("map", "map_sha256", "opponent_snapshot")):
        return False
    try:
        actual = ident["maps"].get(condition["map"])
        if actual is None:
            actual = digest(core.map_path(condition["map"]))
        _snapshot(condition["opponent_snapshot"])
        return actual == condition["map_sha256"]
    except (ArenaError, OSError):
        return False


def assess(contract: dict, evidence: dict | str | Path | None = None) -> dict:
    """Fail closed; a healthy loss is eligible, a missing production test is not."""
    checks, gaps = {}, []
    result = {"bot": contract["bot"], "behavior": contract["id"], "ready": False,
              "state": "unassessed", "checks": checks, "gaps": gaps}
    try:
        current = identity(contract)
        result["identity"] = current
    except (OSError, KeyError, ArenaError) as exc:
        gaps.append(f"identity unavailable: {exc}")
        return result
    missing = [k for k in CONTRACT_FIELDS if not contract.get(k)]
    if missing:
        gaps.append("incomplete contract: " + ", ".join(missing))
    if evidence is None:
        evidence = evidence_path(contract)
    if isinstance(evidence, (str, Path)):
        result["evidence_path"] = _rel(_path(evidence))
        if not _path(evidence).is_file():
            gaps.append("no exact-version admission evidence")
            return result
        try:
            evidence = _load(_path(evidence))
        except ArenaError as exc:
            gaps.append(str(exc))
            result["state"] = "stale"
            return result
    if not isinstance(evidence, dict):
        gaps.append("evidence must be a JSON object")
        result["state"] = "stale"
        return result
    if evidence.get("schema") != SCHEMA or evidence.get("behavior") != contract["id"]:
        gaps.append("evidence schema or behavior mismatch")
    if evidence.get("identity") != current:
        gaps.append("evidence identity is stale (source, contract, tests, observer, engine, maps or toolchain)")
        result["state"] = "stale"
        return result
    artifacts = evidence.get("artifacts") or {}
    if not isinstance(artifacts, dict) or any(not isinstance(evidence.get(k, {}), dict)
                                               for k in ("build", "tests", "health")):
        gaps.append("invalid artifact/build/tests/health record schema")
        result["state"] = "stale"
        return result
    paths = {}
    try:
        for name, artifact in artifacts.items():
            paths[name] = _artifact(artifact)
        ent = _snapshot(current["snapshot"])
        if dict(getattr(ent, "overrides", ())) != current.get("overrides", {}):
            raise ArenaError("snapshot override values differ from evaluated contract")
        if _snapshot_files(ent) != current["source"]:
            raise ArenaError("snapshot source differs from evaluated source closure")
        build = evidence.get("build") or {}
        checks["build"] = build.get("status") == "pass" and build.get("snapshot") == ent.key
    except (ArenaError, OSError) as exc:
        gaps.append(str(exc))
        result["state"] = "stale"
        return result
    tests = evidence.get("tests") or {}
    checks["tests"] = (tests.get("status") == "pass" and isinstance(tests.get("artifact"), str) and tests.get("artifact") in paths and
                       bool(current["tests"]) and all(current["tests"].values()) and
                       f"bots/{core.bot(contract['bot']).suite}/tests/run.py" in current["tests"])
    if checks["tests"]:
        try:
            test_record = _load(paths[tests["artifact"]])
            checks["tests"] = (test_record.get("exit_code") == 0 and
                               isinstance(test_record.get("test_cases"), int) and test_record["test_cases"] > 0 and
                               test_record.get("snapshot") == current["snapshot"] and
                               test_record.get("identity") == canonical(current) and
                               test_record.get("runner") == f"bots/{core.bot(contract['bot']).suite}/tests/run.py")
        except (ArenaError, AttributeError, TypeError):
            checks["tests"] = False
    h = evidence.get("health") or {}
    rows, selection = [], None
    if isinstance(h.get("artifact"), str) and h.get("artifact") in paths:
        try:
            rows, selection = read_health_rows(artifacts[h["artifact"]], current["snapshot"])
        except (ArenaError, ValueError, OSError) as exc:
            selection = getattr(exc, "selection", None)
            gaps.append(f"health artifact is unavailable: {exc}")
    health_result = health_assessment(rows, current, h.get("opponents") or [])
    health_result["selection"] = selection
    if h.get("maps") != current["maps"]:
        health_result["pass"] = False
        health_result["gaps"].append("health artifact must bind current map names and hashes")
    checks["health"] = health_result["pass"]
    result["health"] = health_result
    if not checks["health"]:
        gaps.extend(health_result["gaps"])
    adapter = _adapter(core.bot(contract["bot"]).suite)
    observed = {}
    if adapter and callable(getattr(adapter, "evaluate", None)) and current["observers"] and all(current["observers"].values()):
        try:
            observed = adapter.evaluate(contract, evidence, root=ROOT)
            if not isinstance(observed, dict):
                gaps.append("native evaluator returned a malformed observation")
                observed = {}
        except Exception as exc:
            gaps.append(f"native observer failed: {exc}")
    else:
        gaps.append("no executable native evidence evaluator")
    for role in ROLE_FIELDS:
        entry = observed.get(role) or {}
        if not isinstance(entry, dict):
            checks[role] = False
            continue
        refs = entry.get("artifacts") or []
        valid = (entry.get("status") == "pass" and isinstance(refs, list) and bool(refs) and
                 all(isinstance(ref, str) and ref in paths for ref in refs))
        if role in ("positive", "fresh"):
            valid = valid and all(type(entry.get(k)) is int and entry[k] > 0
                                  for k in ("opportunities", "activations", "continuations"))
            valid = valid and _valid_condition(entry.get("condition"), current)
        elif role == "refusal":
            valid = valid and type(entry.get("refusals")) is int and entry["refusals"] > 0
        else:
            valid = valid and bool(entry.get("physical_difference")) and entry.get("replacement_removes_coverage") is True
            valid = valid and bool(entry.get("control_snapshot")) and bool(entry.get("neighbor_snapshot"))
            if valid:
                try:
                    _snapshot(entry["control_snapshot"])
                    _snapshot(entry["neighbor_snapshot"])
                except (ArenaError, OSError):
                    valid = False
        checks[role] = bool(valid)
    if checks.get("fresh") and checks.get("positive"):
        fresh, positive = observed["fresh"]["condition"], observed["positive"]["condition"]
        if all(fresh[k] == positive[k] for k in ("map", "opponent_snapshot")):
            checks["fresh"] = False
    result["observations"] = observed
    for name, passed in checks.items():
        if not passed:
            gaps.append(f"{name}: incomplete or failed")
    result["ready"] = not gaps and all(checks.values())
    if result["ready"] and identity(contract) != current:
        result["ready"] = False
        result["state"] = "stale"
        gaps.append("source, tests, contract or observer changed during evaluation")
        return result
    if result["ready"]:
        result["state"] = "ready"
    elif not checks.get("build") or not checks.get("tests") or not checks.get("health"):
        result["state"] = "repair"
    elif not checks.get("positive") or not checks.get("refusal"):
        result["state"] = "activate"
    else:
        result["state"] = "differentiate"
    return result


def health_assessment(rows: list[dict], ident: dict, opponents: list[str]) -> dict:
    from games import health
    gaps, covered, opponent_faults = [], set(), []
    if not isinstance(opponents, list) or any(not isinstance(k, str) for k in opponents):
        return {"pass": False, "games": len(rows), "covered": 0, "required": 0,
                "gaps": ["invalid health opponent keys"], "opponent_faults": []}
    if not opponents or not any(k.startswith("anchors/") for k in opponents) or not any(
            k.split("/", 1)[0] not in EXCLUDED | {ident["snapshot"].split("/", 1)[0]} for k in opponents):
        gaps.append("health needs frozen references and a healthy cross-suite opponent")
    for key in opponents:
        try:
            _snapshot(key)
        except (ArenaError, OSError) as exc:
            gaps.append(str(exc))
    for r in rows:
        if not isinstance(r, dict):
            gaps.append("health row must be an object")
            continue
        side = "A" if r.get("a") == ident["snapshot"] else "B" if r.get("b") == ident["snapshot"] else None
        if side is None:
            gaps.append("health row has another subject snapshot")
            continue
        other = "B" if side == "A" else "A"
        opp = r.get(other.lower())
        if opp not in opponents or r.get("map") not in ident["maps"]:
            gaps.append("health row is outside declared panel")
            continue
        if r.get("winner") not in ("A", "B", "draw") or r.get("truncated") or type(r.get("rounds")) is not int or r["rounds"] <= 0:
            gaps.append("health row failed, missing, or truncated")
        elif r.get("engine") != str(ident["engine"]["version"]):
            gaps.append("health row engine mismatch")
        else:
            required = ("faults", "exhausted", "errors", "clears", "fuel_max", "fuel_at_budget")
            valid_fields = True
            for team in (side, other):
                measured = r.get(team)
                if not isinstance(measured, dict) or any(type(measured.get(k)) is not int or measured[k] < 0 for k in required):
                    gaps.append(f"health row lacks valid measured fields for {team}")
                    valid_fields = False
            if not valid_fields:
                continue
            problems = health(r, side)
            if problems:
                gaps.append(f"subject {r['map']} {side}: {', '.join(problems)}")
            opponent_problems = health(r, other)
            if opponent_problems:
                # Reference zero-clear can result from a healthy subject's play.
                # Keep the raw flag; it cannot qualify a healthy cross-suite
                # opponent or waive any subject or technical health failure.
                reference_stall = opp.startswith("anchors/") and opponent_problems == ["stall"]
                opponent_faults.append({"map": r["map"], "opponent": opp, "problems": opponent_problems,
                                        "blocks": not reference_stall, "reference_only_stall": reference_stall})
                if not reference_stall:
                    gaps.append(f"opponent unhealthy {opp} on {r['map']}")
            covered.add((opp, r["map"], side))
    required = {(o, m, s) for o in opponents for m in ident["maps"] for s in "AB"}
    missing = required - covered
    if missing:
        gaps.append(f"health coverage missing {len(missing)} of {len(required)} map/side/opponent keys")
    return {"pass": not gaps and bool(rows), "games": len(rows), "covered": len(covered),
            "required": len(required), "gaps": list(dict.fromkeys(gaps)), "opponent_faults": opponent_faults}


def inventory(selector: str | None = None) -> list[dict]:
    out = []
    audit, provenance = _audit(), _audit_provenance()
    for contract in contracts(selector):
        assessed = assess(contract)
        audited = audit.get(contract["bot"], {})
        observed = audited.get("observed_health")
        observation = None
        if isinstance(observed, dict):
            snapshot = audited.get("source_snapshot")
            current = assessed.get("identity") or {}
            exact = (snapshot in (current.get("snapshot"), (current.get("snapshot") or "").rsplit("@", 1)[-1]) and
                     provenance.get("valid") and provenance.get("engine") == str((current.get("engine") or {}).get("version")))
            observation = {"status": "current" if exact else "historical", "source_snapshot": snapshot,
                           "provenance": provenance, "health": observed,
                           "qualification": "triage only; partial clean observations never establish admission"}
            failures = sum(observed.get(k, 0) for k in ("faults", "errors", "exhausted", "stalls", "suspended_games", "failed_games")
                           if type(observed.get(k, 0)) is int)
            if exact and failures and not assessed["ready"]:
                assessed["state"] = "repair"
                assessed["gaps"].append("current exact-source audit has measured health failures (triage; see audit_observation)")
        diagnostic = diagnostic_health(contract, assessed.get("identity") or {})
        if diagnostic and diagnostic.get("status") == "current":
            observation = diagnostic
            failures = sum(diagnostic["health"].get(k, 0) for k in
                           ("faults", "errors", "exhausted", "stalls", "suspended_games", "failed_games"))
            if failures:
                assessed["ready"] = False
                assessed["state"] = "repair"
                assessed["gaps"].append("current diagnostic native games have measured health failures (see audit_observation)")
        extra = contract.get("gap") or []
        extra = [extra] if isinstance(extra, str) else extra
        if observation and observation["status"] == "historical":
            extra = [g for g in extra if "Existing exact-version ladder games have health flags" not in g]
        out.append({"bot": contract["bot"], "state": assessed["state"], "contract": contract,
                    "gaps": assessed["gaps"] + extra, "identity": assessed.get("identity"),
                    "assessment": assessed, "audit_observation": observation,
                    "externally_owned": contract.get("externally_owned", False)})
    return out


def diagnostic_health(contract: dict, current: dict) -> dict | None:
    """Ingest an exact-source native diagnostic without confusing it with admission.

    Optional contract.health_evidence names a durable JSON record containing
    snapshot, engine, bundle_sha256, map_hashes and rows:{path,sha256}. An optional
    rows.subject_snapshot must equal current.snapshot and selects exact a/b keys
    from a fully hash-verified mixed log; omitted means strict all-rows checking.
    It may
    instead contain those fields in population_health. All totals are recomputed
    from the pinned arena rows; reported summary counters are never trusted.
    """
    if not contract.get("health_evidence"):
        return None
    selection = None
    try:
        record_path = _path(contract["health_evidence"])
        record = _load(record_path)
        if "population_health" in record:
            record = record["population_health"]
        if (record.get("snapshot") != current.get("snapshot") or
                record.get("engine") != str((current.get("engine") or {}).get("version")) or
                record.get("bundle_sha256") != current.get("bundle")):
            raise ArenaError("diagnostic source or engine bundle changed")
        maps = record.get("map_hashes")
        if not isinstance(maps, dict) or not maps or any(current.get("maps", {}).get(m) != sha for m, sha in maps.items()):
            raise ArenaError("diagnostic map identity missing or changed")
        rows, selection = read_health_rows(record["rows"], current["snapshot"])
        totals = {k: 0 for k in ("faults", "errors", "exhausted", "stalls", "suspended_games", "failed_games")}
        seen_maps = set()
        for row in rows:
            if not isinstance(row, dict) or row.get("engine") != record["engine"] or row.get("map") not in maps:
                raise ArenaError("diagnostic row engine/map mismatch")
            side = "A" if row.get("a") == current["snapshot"] else "B" if row.get("b") == current["snapshot"] else None
            if side is None:
                raise ArenaError("diagnostic row subject mismatch")
            measured = row.get(side)
            fields = ("faults", "errors", "exhausted", "clears", "fuel_at_budget")
            if not isinstance(measured, dict) or any(type(measured.get(k)) is not int or measured[k] < 0 for k in fields):
                raise ArenaError("diagnostic row lacks measured health fields")
            for key in ("faults", "errors", "exhausted"):
                totals[key] += measured[key]
            failed = row.get("winner") not in ("A", "B", "draw") or row.get("truncated")
            totals["failed_games"] += bool(failed)
            totals["stalls"] += not failed and measured["clears"] == 0
            totals["suspended_games"] += measured["fuel_at_budget"] > 0
            seen_maps.add(row["map"])
        totals.update(games=len(rows), maps=len(seen_maps))
        return {"status": "current", "source_snapshot": current["snapshot"], "health": totals,
                "selection": selection,
                "provenance": {"record": _rel(record_path), "record_sha256": digest(record_path),
                               "rows": record["rows"], "map_hashes": maps, "selection": selection},
                "qualification": "native diagnostic triage only; clean games do not establish admission"}
    except (ArenaError, OSError, KeyError, TypeError, AttributeError, ValueError) as exc:
        return {"status": "unavailable", "reason": str(exc), "selection": getattr(exc, "selection", selection)}


def make_panel(selector: str | None = None, mode: str = "quick", experimental: bool = False) -> dict:
    rows = inventory(selector)
    eligible = [r for r in rows if not r["contract"].get("externally_owned") and
                (r["assessment"]["ready"] or experimental)]
    if not eligible:
        raise ArenaError("no ready population entries; use --experimental to pin an explicitly unadmitted panel")
    chosen = []
    for row in eligible:
        if row["bot"].split("/", 1)[0].casefold() in EXCLUDED:
            raise ArenaError("panel contains a suite outside the population mandate")
        if not isinstance(row.get("identity"), dict):
            raise ArenaError(f"cannot pin {row['bot']}: exact variant identity is unavailable")
        b = core.bot(row["bot"])
        overrides = _overrides(row["contract"].get("overrides", {}))
        raw = core.read_closure(b)
        if _source_hashes(raw) != (row["identity"].get("working_source") or row["identity"]["source"]):
            raise ArenaError(f"working source changed before pinning {b.id}; wait for a stable boundary")
        ent = core.prepare(b, overrides or None, contents=raw)
        if not ent.ok:
            raise ArenaError(f"cannot pin {b.id}: build failed: {ent.log[-500:]}")
        expected = row["identity"]["snapshot"] if row["identity"] else None
        if ent.key != expected:
            raise ArenaError(f"source changed while pinning {b.id}; rerun after the owner reaches a stable boundary")
        if identity(row["contract"]) != row["identity"]:
            raise ArenaError(f"tests or observer changed while pinning {b.id}; rerun after a stable boundary")
        source = _snapshot_files(ent)
        if source != row["identity"]["source"] or dict(getattr(ent, "overrides", ())) != overrides:
            raise ArenaError(f"prepared snapshot source or overrides disagree with identity: {b.id}")
        declared = row["contract"].get("match_maps") or row["contract"].get("maps")
        if isinstance(declared, str):
            declared = core.map_set(declared)
        maps = core.pool_maps() if mode == "full" else list(declared or core.pool_maps()[:2])
        chosen.append({"bot": b.id, "behavior": row["contract"]["id"], "snapshot": ent.key,
                       "source": source, "overrides": overrides, "maps": {m: digest(core.map_path(m)) for m in maps},
                       "sides": ["A", "B"], "admitted": row["assessment"]["ready"],
                       "contract": row["contract"], "identity": row["identity"],
                       "applicability": "declared" if declared else "unknown: fallback maps, no witnessed trigger guarantee",
                       "gaps": row["gaps"]})
    return {"schema": SCHEMA, "kind": "population-panel", "mode": mode,
            "experimental": experimental, "engine": _load(ROOT / "docs/reference/current-version.json")["engine"],
            "toolchain": core.toolchain(), "bundle": digest(ROOT / "docs/reference/current-version.json"),
            "entries": chosen, "omitted": [r["bot"] for r in rows if r["bot"] not in {c['bot'] for c in chosen}],
            "coverage_note": "Both modes preserve every selected contribution; quick uses declared targeted maps or fallback maps, full uses pool. Unadmitted behavior remains unverified."}


def validate_panel(panel: dict) -> list[Entrant]:
    if panel.get("schema") != SCHEMA or panel.get("kind") != "population-panel" or not panel.get("entries"):
        raise ArenaError("invalid or empty population panel")
    if panel.get("toolchain") != core.toolchain() or panel.get("bundle") != digest(ROOT / "docs/reference/current-version.json"):
        raise ArenaError("panel engine/toolchain bundle is stale")
    out = []
    for entry in panel["entries"]:
        if not isinstance(entry, dict):
            raise ArenaError("invalid panel entry")
        bot_id, snapshot = entry.get("bot"), entry.get("snapshot")
        if not isinstance(bot_id, str) or not isinstance(snapshot, str):
            raise ArenaError("panel entry needs bot and snapshot identities")
        snapshot_bot = snapshot.rsplit("@", 1)[0]
        if (bot_id.split("/", 1)[0].casefold() in EXCLUDED or
                snapshot_bot.split("/", 1)[0].casefold() in EXCLUDED):
            raise ArenaError("panel contains a suite outside the population mandate")
        contract, recorded = entry.get("contract"), entry.get("identity")
        if (bot_id != snapshot_bot or not isinstance(contract, dict) or not isinstance(recorded, dict) or
                contract.get("bot") != bot_id or entry.get("behavior") != contract.get("id") or
                recorded.get("snapshot") != snapshot or canonical(contract) != recorded.get("contract")):
            raise ArenaError("panel bot, snapshot, behavior, contract or identity disagree")
        overrides = _overrides(contract.get("overrides", {}))
        if (_overrides(recorded.get("overrides", {}), "identity") != overrides or
                _overrides(entry.get("overrides", {}), "panel entry") != overrides or
                entry.get("source") != recorded.get("source")):
            raise ArenaError("panel applied source or override values disagree with identity")
        working_source = recorded.get("working_source")
        if (not isinstance(working_source, dict) or set(working_source) != set(recorded.get("source") or {}) or
                any(type(k) is not str or type(v) is not str for k, v in working_source.items())):
            raise ArenaError("panel working-source provenance is missing or malformed")
        ent = _snapshot(snapshot)
        if dict(getattr(ent, "overrides", ())) != overrides:
            raise ArenaError("snapshot metadata overrides disagree with panel contract")
        if _snapshot_files(ent) != entry.get("source"):
            raise ArenaError(f"snapshot source missing or changed: {ent.key}")
        if not entry.get("maps") or set(entry.get("sides", [])) != {"A", "B"}:
            raise ArenaError("panel requires declared maps and both sides")
        measurement = _measurement_files(contract)
        if not recorded.get("tests") or any(measurement[key] != recorded.get(key, {})
                                             for key in ("tests", "observers")):
            raise ArenaError(f"panel tests or observers changed or are missing: {ent.key}")
        for name, sha in entry["maps"].items():
            if digest(core.map_path(name)) != sha:
                raise ArenaError(f"panel map changed: {name}")
        out.append(ent)
    return out


def regress(subject: str, panel: dict, out: Path, jobs: int = 0) -> dict:
    from games import GameSpec, play, health
    if subject.split("/", 1)[0].casefold() in EXCLUDED - {"anchors"}:
        raise ArenaError("main, surprise and contenders are outside this orchestration mandate")
    opponents = validate_panel(panel)
    b = core.bot(subject)
    ent = core.prepare(b)
    if not ent.ok:
        raise ArenaError(f"subject build failed: {ent.log[-500:]}")
    specs = []
    for entry, opp in zip(panel["entries"], opponents):
        for name in entry["maps"]:
            for side in entry["sides"]:
                specs.append(GameSpec(*((ent, opp) if side == "A" else (opp, ent)), name,
                                      {"subject": side, "population_behavior": entry["behavior"],
                                       "population_side": "B" if side == "A" else "A"}))
    rows = play(specs, out, jobs=jobs, rounds=0, keep_replays="all", resume=True)
    def outcomes(items):
        result = {"wins": 0, "losses": 0, "draws": 0, "failed": 0}
        for row in items:
            side = (row.get("tags") or {}).get("subject")
            winner = row.get("winner")
            key = ("failed" if side not in ("A", "B") else "draws" if winner == "draw" else
                   "wins" if winner == side else "losses" if winner in ("A", "B") else "failed")
            result[key] += 1
        return result
    coverage = []
    for entry in panel["entries"]:
        selected = [r for r in rows if (r.get("tags") or {}).get("population_behavior") == entry["behavior"]]
        adapter = _adapter(entry["bot"].split("/", 1)[0])
        observed = {"status": "unknown", "reason": "no native match observer"}
        if adapter and callable(getattr(adapter, "observe", None)):
            try:
                observed = adapter.observe(entry["contract"], selected, root=ROOT)
            except Exception as exc:
                observed = {"status": "error", "reason": str(exc)}
        coverage.append({"behavior": entry["behavior"], "snapshot": entry["snapshot"],
                         "admitted": entry["admitted"], "games": len(selected), "outcomes": outcomes(selected),
                         "observation": observed})
    problems = [{"map": r.get("map"), "side": s, "role": "subject" if s == (r.get("tags") or {}).get("subject") else "opponent",
                 "issues": health(r, s)} for r in rows for s in "AB" if health(r, s)]
    failed = [r for r in rows if r.get("winner") not in ("A", "B", "draw") or r.get("truncated")]
    execution_pass = not problems and not failed and len(rows) == len(specs)
    coverage_pass = bool(coverage) and all(isinstance(c["observation"], dict) and
                        all(type(c["observation"].get(k)) is int and c["observation"][k] > 0
                            for k in ("opportunities", "activations", "continuations")) and
                        c["observation"].get("verified") is True for c in coverage)
    report = {"schema": SCHEMA, "subject": ent.key, "panel": panel, "games": len(rows),
              "outcomes": outcomes(rows),
              "expected_games": len(specs), "health_problems": problems, "failed_games": len(failed),
              "subject_health_problems": [r for r in problems if r["role"] == "subject"],
              "opponent_health_problems": [r for r in problems if r["role"] == "opponent"],
              "behavior_coverage": coverage, "execution_pass": execution_pass, "coverage_pass": coverage_pass,
              "pass": execution_pass and coverage_pass}
    _write(out / "population-regression.json", report)
    return report


def _official_asset_paths(assets) -> set[Path]:
    """Validate declared decoder inputs; external cache assets are not prune targets.

    Validate the declared cache location, including the desktop package alias,
    without resolving external files:
    Windows package redirection can give a file a different resolved location
    from its parent cache directory. Availability and bytes are checked by the
    native evidence runner. Only frontend modules (.js/.mjs/.wasm) use this
    exception; broader asset types require deliberate schema support. Ordinary
    evidence/replay paths still use _path().
    """
    if not isinstance(assets, dict):
        raise ArenaError("officialAssets must be a mapping of filenames to pinned asset descriptors")
    retained = set()
    asset_root = OFFICIAL_ASSETS.absolute()
    asset_roots = [asset_root]
    if asset_root.name.casefold() == "assets" and asset_root.parent.name.casefold() == "cambc":
        # Windows redirects this desktop package's files under the same local
        # cache. Accept this exact alias, not arbitrary packages or locations.
        asset_roots.append(asset_root.parent.parent /
                           "Packages/OpenAI.Codex_2p2nqsd0c76g0/LocalCache/Local/cambc/assets")
    for name, record in assets.items():
        if (type(name) is not str or name in (".", "..") or not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or
                Path(name).suffix not in (".js", ".mjs", ".wasm") or
                not isinstance(record, dict) or set(record) != {"path", "sha256"} or
                type(record["path"]) is not str or type(record["sha256"]) is not str or
                not re.fullmatch(r"[0-9a-fA-F]{64}", record["sha256"])):
            raise ArenaError("malformed officialAssets descriptor (expected filename: {path, sha256})")
        path = Path(record["path"])
        if not path.is_absolute():
            raise ArenaError("officialAssets paths must be absolute official cache inputs")
        if path.drive.casefold() != OFFICIAL_ASSETS.drive.casefold() or ".." in path.parts:
            raise ArenaError("officialAssets paths must use the configured local cache without traversal")
        declared_root = next((root for root in asset_roots if path.is_relative_to(root)), None)
        if declared_root is None:
            raise ArenaError("officialAssets path is outside the configured cambc asset cache")
        parts = path.relative_to(declared_root).parts
        if (len(parts) != 4 or not re.fullmatch(r"[0-9a-fA-F]{64}", parts[0]) or
                parts[1] not in ("visualiser", "mapeditor") or parts[2] != "assets" or path.name != name):
            raise ArenaError("officialAssets path does not identify a named pinned visualiser/mapeditor asset")
        if path.is_relative_to(ROOT.absolute()):
            retained.add(_path(path))  # In-repository inputs keep ordinary resolved containment.
    return retained


def protected_paths() -> set[Path]:
    """Prune hook; malformed records fail closed, declared official inputs stay external."""
    root = ROOT / "results/population"
    protected = set()
    if not root.exists():
        return protected
    def walk(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key == "officialAssets":
                    protected.update(_official_asset_paths(item))
                    continue
                if key in ("inputs", "pins") and isinstance(item, list):
                    # Native oracle dependency lists use {path, sha256, bytes},
                    # unlike the filename-keyed officialAssets publication map.
                    for pin in item:
                        if isinstance(pin, dict) and type(pin.get("path")) is str:
                            declared = Path(pin["path"])
                            if declared.is_absolute() and not declared.is_relative_to(ROOT.absolute()):
                                if (set(pin) != {"path", "sha256", "bytes"} or
                                        type(pin["bytes"]) is not int or pin["bytes"] < 0):
                                    raise ArenaError("malformed official input pin (expected path, sha256, bytes)")
                                protected.update(_official_asset_paths({declared.name: {
                                    "path": pin["path"], "sha256": pin["sha256"]}}))
                                continue
                        walk(pin)
                    continue
                if key in ("path", "replay", "evidence_path") and isinstance(item, str):
                    protected.add(_path(item))
                if key in ("evidence_paths", "replays") and isinstance(item, list):
                    protected.update(_path(p) for p in item if isinstance(p, str))
                if key in ("snapshot", "control_snapshot", "neighbor_snapshot") and isinstance(item, str) and "@" in item:
                    protected.add(CACHE / "snap" / item.rsplit("@", 1)[1])
                if key == "opponents" and isinstance(item, list):
                    for key_value in item:
                        if isinstance(key_value, str) and "@" in key_value:
                            protected.add(CACHE / "snap" / key_value.rsplit("@", 1)[1])
                walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)
    for path in root.rglob("*.json"):
        protected.add(path)
        walk(_load(path))
    return {p.resolve() for p in protected}


def _cmd_inspect(args) -> int:
    rows = inventory(args.select)
    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            print(f"{row['state']:<14} {row['bot']:<35} {row['gaps'][0] if row['gaps'] else row['contract']['interaction']}")
        print(f"{sum(r['state'] == 'ready' for r in rows)}/{len(rows)} ready; health, activation and contribution assessed separately")
    return 0


def _cmd_check(args) -> int:
    selected = contracts(args.bot)
    results = [assess(c, args.evidence) for c in selected]
    print(json.dumps(results, indent=2))
    return 0 if results and all(r["ready"] for r in results) else 1


def _cmd_panel(args) -> int:
    target = _path(args.out)
    if not target.is_relative_to((ROOT / "results/population").resolve()):
        raise ArenaError("pinned panel manifests must be under results/population so prune can protect their snapshots")
    data = make_panel(args.select, args.mode, args.experimental)
    _write(target, data)
    print(f"pinned {len(data['entries'])} entries ({'experimental' if data['experimental'] else 'admitted'}) to {args.out}")
    return 0


def _cmd_regress(args) -> int:
    data = regress(args.subject, _load(_path(args.panel)), _path(args.out), args.jobs)
    print(json.dumps({k: v for k, v in data.items() if k != "panel"}, indent=2))
    return 0 if data["pass"] else 1


def register(sub) -> None:
    parser = sub.add_parser("population", help="inspect exact-version behavior evidence and run pinned regression panels")
    actions = parser.add_subparsers(dest="population_action", required=True)
    p = actions.add_parser("inspect")
    p.add_argument("select", nargs="?", default=None)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=_cmd_inspect)
    p = actions.add_parser("check")
    p.add_argument("bot")
    p.add_argument("--evidence")
    p.set_defaults(func=_cmd_check)
    p = actions.add_parser("panel")
    p.add_argument("--select", default=None)
    p.add_argument("--mode", choices=("quick", "full"), default="quick")
    p.add_argument("--experimental", action="store_true")
    p.add_argument("--out", required=True)
    p.set_defaults(func=_cmd_panel)
    p = actions.add_parser("regress")
    p.add_argument("subject")
    p.add_argument("--panel", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=0)
    p.set_defaults(func=_cmd_regress)
