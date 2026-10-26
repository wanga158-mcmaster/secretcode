#!/usr/bin/env python3
"""File-protocol native complete-game evaluation for the C++ optimization engines.

  python native_evaluator.py --request request.json --response result.json --repo ROOT

Every cache cell binds exact project/WASM, engine/tool bytes, map bytes and side.
Full native replay+statistics are compressed and retained, and revalidated on reuse.
This is a local adapter. No login, upload, server, or account commands exist here.
"""
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import sys
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor

# Isolated Python (-I), used by the portable C++ runner, omits the script directory.
# Add only this known adapter directory; never depend on the caller's working directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from native_support import (EvaluationError, canonical, descriptor, digest, entrant_identity,
                            environment_files, exact_entrant, export_entrant, file_digest,
                            knobs, load_arena, load_materializer, read_json, write_json)

SCHEMA = "contenders-eval/v1"
RESULT = "contenders-eval-result/v1"
VERSION = 1


def frozen_default(repo: Path) -> str:
    live = read_json(repo / "results/releases/LIVE.json")
    anchor = live.get("anchor")
    if not isinstance(anchor, str) or not anchor.startswith("anchors/"):
        raise EvaluationError("LIVE.json has no frozen anchor")
    return anchor


def normalize_cases(request: dict, core, default: str, deduplicate: bool = False) -> list[dict]:
    values = request.get("cases")
    if values is None:
        maps = request.get("maps", ["00ambush"])
        if isinstance(maps, str):
            maps = core.map_set(maps)
        opponents = request.get("opponents", [default])
        if isinstance(opponents, str):
            opponents = opponents.split(",")
        values = [{"map": m, "side": s, "opponent": o} for m in maps
                  for s in request.get("sides", ["A", "B"]) for o in opponents]
    if not isinstance(values, list) or not values:
        raise EvaluationError("cases must be a nonempty array")
    out, seen = [], set()
    for value in values:
        if not isinstance(value, dict):
            raise EvaluationError("case must be an object")
        m = value.get("map")
        side = str(value.get("side", "")).upper()
        opponent = value.get("opponent", default)
        if not isinstance(m, str) or side not in ("A", "B"):
            raise EvaluationError("each case needs map and side A/B")
        if value.get("seed", 0) not in (0, None):
            raise EvaluationError("external game seeds are unsupported; expose policy seed/noise_burn in candidate source")
        # Qualification emits paths relative to maps/, while arena also accepts
        # stems and existing paths. Resolve packet-local map files independently
        # of the caller's working directory before using arena's stem lookup.
        map_root = (core.ROOT / "maps").resolve()
        mp = next((p for p in ((core.ROOT / m).resolve(), (map_root / m).resolve())
                   if p.is_relative_to(map_root) and p.is_file()), None)
        if mp is None:
            mp = core.map_path(m).resolve()
        if not mp.is_relative_to(core.ROOT):
            raise EvaluationError("map must be inside the repository/worker packet")
        actual_map_sha = file_digest(mp)
        if "map_sha256" in value:
            expected_map_sha = value["map_sha256"]
            if not isinstance(expected_map_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_map_sha):
                raise EvaluationError("case.map_sha256 must be a lowercase SHA256 digest")
            if actual_map_sha != expected_map_sha:
                raise EvaluationError(f"map bytes differ from requested map_sha256 for {m}: expected {expected_map_sha}, got {actual_map_sha}")
        identity = (mp.relative_to(core.ROOT).as_posix(), side, opponent)
        if identity in seen:
            if deduplicate:
                continue
            raise EvaluationError(f"duplicate deterministic case {identity}")
        seen.add(identity)
        out.append({"map": m, "map_name": mp.stem, "map_path": mp,
                    "map_relative": mp.relative_to(core.ROOT).as_posix(),
                    "map_sha256": actual_map_sha, "side": side, "opponent": opponent})
    return out


def configuration(core, controller: str, data: dict, label: str):
    """Virtual arena source closure avoids shared mutable config-bot files entirely."""
    base = exact_entrant(core, controller)
    # Read the exact frozen snapshot, never substitute its present working directory.
    source = base.path / "src"
    # A controller with cross-directory relative source dependencies needs a distinct
    # materializer. Reject that unsupported domain rather than silently omitting files.
    for src in source.rglob("*"):
        if src.is_file() and src.suffix in core.SOURCE_SUFFIXES:
            for include in core.INCLUDE.findall(src.read_text(encoding="utf-8")):
                target = (src.parent / include).resolve()
                if target.is_file() and not target.is_relative_to(source.resolve()) and not target.is_relative_to((core.ROOT / "sdk").resolve()):
                    raise EvaluationError(f"configuration base needs unsupported external relative include {include}")
    b = core.Bot("contenders", "_optimizer-config")
    contents = {b.path / "src" / p.relative_to(source): p.read_bytes()
                for p in source.rglob("*") if p.is_file()}
    ent = core.prepare(b, overrides=knobs(data), label=label, contents=contents)
    if not ent.ok:
        raise EvaluationError(f"candidate configuration build failed: {ent.log[-3000:]}")
    return ent


def baseline_spec(request: dict, controller: str) -> tuple[str, dict]:
    base = request.get("baseline", controller)
    if isinstance(base, str):
        return base, {}
    if not isinstance(base, dict):
        raise EvaluationError("baseline must be a bot selector or object with bot/knobs")
    return str(base.get("bot", base.get("controller", controller))), base.get("knobs", {})


def contextual_configuration(core, controller: str, policy: dict):
    from config_materializer import materialize_configuration
    return materialize_configuration(core, controller, policy)


def materializer_base(core, selector: str) -> str:
    """The existing materializer copies registered source, so verify an exact pin first."""
    if "@" not in selector:
        return selector
    bare = selector.rsplit("@", 1)[0]
    actual = exact_entrant(core, bare, "materializer-base")
    if actual.key != selector:
        raise EvaluationError(f"materializer base source changed: expected {selector}, got {actual.key}")
    return bare


def program_lock_path(core) -> Path:
    # The generated working bot is shared by this repository, even when callers
    # choose separate game-cache directories or immutable adapter closures.
    return core.ROOT / ".cache/contenders/optimization/native/locks/materialize-program.lock"


def restore_population(core, request: dict, required: set[str], cache: Path) -> list[dict]:
    """Rebuild an archived opponent only from its exact, hash-checked recipe.

    Campaign exports carry these small recipes with the evaluated WASM/source
    artifacts. This makes generated populations usable after disposable arena
    caches are pruned, without treating a current working bot as an old snapshot.
    """
    recipes = {}
    for field, kind in (("population_policies", "policy"), ("population_configs", "config")):
        values = request.get(field, [])
        if not isinstance(values, list) or len(values) > 4096:
            raise EvaluationError(f"{field} must be an array of at most 4096 exact recipes")
        for value in values:
            if not isinstance(value, dict):
                raise EvaluationError(f"{field} recipe must be an object")
            key = value.get("key")
            prefix = "contenders/_optimizer-program" if kind == "policy" else "contenders/_optimizer-config"
            if not isinstance(key, str) or not re.fullmatch(re.escape(prefix) + r"@[0-9a-f]{12}", key):
                raise EvaluationError(f"invalid exact {kind} population key {key!r}")
            if key in recipes and canonical(recipes[key][1]) != canonical(value):
                raise EvaluationError(f"conflicting restoration recipes for {key}")
            recipes[key] = kind, value
    receipts = []
    for key in sorted(required & recipes.keys()):
        kind, recipe = recipes[key]
        expected = recipe.get("identity")
        if not isinstance(expected, dict) or expected.get("key") != key:
            raise EvaluationError(f"restoration needs full original candidate_identity for {key}")
        for role in ("raw", "injected"):
            sha = expected.get("modules", {}).get(role, {}).get("sha256", "")
            if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{64}", sha):
                raise EvaluationError(f"restoration needs exact {role} module SHA256 for {key}")
        source_sha = expected.get("source_sha256", "")
        if not isinstance(source_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", source_sha):
            raise EvaluationError(f"restoration needs exact source SHA256 for {key}")
        with core.FileLock(cache / "locks" / f"restore-{digest(key)}.lock"):
            version = key.rsplit("@", 1)[1]
            missing = not (core.CACHE / "snap" / version / ".ready").is_file()
            if missing:
                controller = recipe.get("controller")
                if not isinstance(controller, str) or "/_optimizer-" in controller:
                    raise EvaluationError(f"restoration needs its original frozen base for {key}")
                # Exported exact anchor selectors can also have lost their cached
                # build. Recreate from immutable source, then check the exact key.
                if "@" in controller:
                    bare, base_version = controller.rsplit("@", 1)
                    if not (core.CACHE / "snap" / base_version / ".ready").is_file():
                        rebuilt = exact_entrant(core, bare, "restored-base")
                        if rebuilt.key != controller:
                            raise EvaluationError(f"frozen restoration base changed: expected {controller}, got {rebuilt.key}")
                if kind == "config":
                    if "config_policy" in recipe:
                        ent, _ = contextual_configuration(core, controller, recipe["config_policy"])
                    else:
                        ent = configuration(core, controller, recipe.get("knobs", {}), "restored-opponent")
                else:
                    policy = recipe.get("policy")
                    if not isinstance(policy, dict):
                        raise EvaluationError(f"restoration needs the exact actor policy for {key}")
                    materializer = load_materializer()
                    with core.FileLock(program_lock_path(core)):
                        candidate = {"base": materializer_base(core, controller), "policy": policy,
                                     "collect_rollouts": bool(policy.get("trace", False))}
                        materialized = materializer.materialize_program(core.ROOT, candidate, "policy")
                        b = core.bot(materialized["bot"])
                        ent = core.prepare(b, label="restored-opponent", contents=core.read_closure(b))
                        if not ent.ok:
                            raise EvaluationError(f"archived policy rebuild failed: {ent.log[-3000:]}")
                if ent.key != key:
                    raise EvaluationError(f"archived opponent source changed: expected {key}, got {ent.key}")
            ent = exact_entrant(core, key, "opponent")
            actual = entrant_identity(core, ent)
            if any(actual[field] != expected[field] for field in ("key", "source_sha256", "modules")):
                raise EvaluationError(f"archived opponent source/WASM identity mismatch: {key}")
            receipts.append({"key": key, "restored": missing, "identity": actual})
    return receipts


def parse_evidence(games, archive: Path, side: str) -> tuple[dict, str]:
    with zipfile.ZipFile(archive) as z:
        raw = z.read("statistics.json")
        raw_statistics = json.loads(raw)
        diagnostics = raw_statistics.get("diagnostics", {})
        if diagnostics.get("detailedCaptureComplete") is False:
            raise EvaluationError("native statistics report incomplete capture")
        diagnostic_text = json.dumps(diagnostics.get("codes", [])).lower()
        if any(word in diagnostic_text for word in ("truncat", "output_limit", "stdout_limit", "overflow")):
            raise EvaluationError("native diagnostics report truncated/output-limited evidence")
        parsed = games.parse_statistics(raw_statistics)
        # The replay retains independent stdout/fuel diagnostics not present in statistics.
        replay = z.read("replay.repl26a")
    import tempfile
    # parse_replay accepts a file. This temporary is only regenerable cache evidence.
    fd, tmp = tempfile.mkstemp(prefix="health-", suffix=".repl26a", dir=archive.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(replay)
        measured, notes = games.parse_replay(Path(tmp), parsed["roster"])
    finally:
        Path(tmp).unlink(missing_ok=True)
    if notes:
        raise EvaluationError("incomplete native health evidence: " + "; ".join(notes))
    row = {"winner": parsed["winner"], "rounds": parsed["rounds"],
           "A": dict(parsed["A"], **measured["A"]),
           "B": dict(parsed["B"], **measured["B"])}
    if row["winner"] not in ("A", "B", "draw") or row["rounds"] <= 0:
        raise EvaluationError("not a complete native terminal game")
    return row, hashlib_bytes(raw)


def hashlib_bytes(data: bytes) -> str:
    import hashlib
    return hashlib.sha256(data).hexdigest()


def verify_game_inputs(core, case: dict, environment: dict) -> None:
    if file_digest(case["map_path"]) != case["map_sha256"]:
        raise EvaluationError("native map bytes changed during evaluation")
    if file_digest(core.CAMBC) != environment["cli"]:
        raise EvaluationError("native CLI bytes changed during evaluation")


def cache_cell(core, games, subject, opponent, case: dict, environment: dict, cache: Path) -> dict:
    side = case["side"]
    subject_id, opponent_id = entrant_identity(core, subject), entrant_identity(core, opponent)
    identity = {"schema": "contenders-native-game/v1", "engine": games.ENGINE,
                "tools": environment, "subject": subject_id, "opponent": opponent_id,
                "map": case["map_relative"], "map_sha256": case["map_sha256"], "side": side}
    key = digest(identity)
    out = cache / "games" / key
    out.mkdir(parents=True, exist_ok=True)
    record_path, archive = out / "record.json", out / "native.zip"
    with core.FileLock(cache / "locks" / f"game-{key}.lock"):
        verify_game_inputs(core, case, environment)
        if record_path.is_file():
            record = read_json(record_path)
            if record.get("identity") != identity or record.get("key") != key:
                raise EvaluationError(f"cache identity mismatch: {record_path}")
            if not archive.is_file() or file_digest(archive) != record.get("archive_sha256"):
                raise EvaluationError(f"cache evidence corrupt/missing: {archive}")
            row, stats_sha = parse_evidence(games, archive, side)
            if digest(row) != record.get("native_summary_sha256") or stats_sha != record.get("statistics_sha256"):
                raise EvaluationError(f"cached native summary mismatch: {record_path}")
            return dict(record, row=row, cache_hit=True, archive=str(archive))
        if shutil.disk_usage(cache).free < 256 * 1024 * 1024:
            raise EvaluationError("native cache has less than 256 MiB free; archive outputs or choose another cache volume")
        a, b = (subject, opponent) if side == "A" else (opponent, subject)
        spec = games.GameSpec(a, b, str(case["map_path"]), {"subject": side, "phase": "optimization", "native_key": key})
        stage = out / "run"
        rows = games.play([spec], stage, jobs=1, rounds=0, keep_replays="all", quiet=True, resume=False)
        verify_game_inputs(core, case, environment)
        if len(rows) != 1:
            raise EvaluationError("arena returned incorrect row count")
        original = rows[0]
        if original.get("winner") not in ("A", "B", "draw") or original.get("truncated"):
            return {"key": key, "identity": identity, "row": original, "cache_hit": False,
                    "error": original.get("error", "incomplete native game"), "archive": None}
        replay = core.ROOT / original["replay"]
        stats_path = replay.with_suffix(".json")
        if not replay.is_file() or not stats_path.is_file():
            raise EvaluationError("arena full-game evidence missing")
        # CLI owns selection between project build roles. Pin both before and after the call.
        if entrant_identity(core, subject) != subject_id or entrant_identity(core, opponent) != opponent_id:
            raise EvaluationError("run-batch changed a pinned executable; native result not admitted")
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            z.writestr("statistics.json", stats_path.read_bytes())
            z.writestr("replay.repl26a", replay.read_bytes(), compress_type=zipfile.ZIP_STORED)
        row, stats_sha = parse_evidence(games, archive, side)
        record = {"schema": "contenders-native-cache/v1", "key": key, "identity": identity,
                  "row": row, "archive_sha256": file_digest(archive),
                  "statistics_sha256": stats_sha, "native_summary_sha256": digest(row),
                  "native_row": original, "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
        write_json(record_path, record)
        # Originals are regenerable cache only. The archive retains their full bytes.
        stats_path.unlink()
        replay.unlink()
        return dict(record, cache_hit=False, archive=str(archive))


def payoff(row: dict, side: str) -> float:
    winner = row.get("winner")
    return 1.0 if winner == side else (-1.0 if winner in ("A", "B") else 0.0)


def subject_stdout(cell: dict, side: str) -> str:
    if not cell.get("archive"):
        return ""
    import io
    with zipfile.ZipFile(cell["archive"]) as z:
        raw = z.read("replay.repl26a")
    with zipfile.ZipFile(io.BytesIO(raw)) as replay:
        return replay.read(f"team-{side.lower()}.stdout").decode("utf-8", errors="strict")


def episodes_for(module, cell: dict, case: dict, subject_key: str, engine: str,
                 healthy: bool, opponent_healthy: bool, collect: bool = True) -> dict:
    stdout = subject_stdout(cell, case["side"]) if collect else ""
    lower_stdout = stdout.lower()
    if any(marker in lower_stdout for marker in ("output truncated", "stdout truncated", "stdout limit", "output limit exceeded")):
        raise EvaluationError("policy stdout indicates truncation/output limit; trajectory rejected")
    observations = module.parse_rollouts(stdout) if collect else []
    if not isinstance(observations, list):
        raise EvaluationError("materializer rollout parser must return an array")
    observed = len(observations)
    recognized = sum(1 for line in stdout.splitlines() if "OPT_TRAJ" in line)
    logging_complete = healthy and opponent_healthy and observed > 0 and observed == recognized
    return {"trajectory_id": cell["key"], "source_key": subject_key, "subject_key": subject_key,
            "engine": engine, "feature_schema": "contenders-actor-context/v1",
            "schema": "contenders-policy-episode/v1", "family": case["map_name"],
            "map": case["map_name"], "side": case["side"],
            "opponent": cell["identity"]["opponent"]["key"],
            "payoff": payoff(cell["row"], case["side"]),
            "outcome": "win" if payoff(cell["row"], case["side"]) > 0 else "loss" if payoff(cell["row"], case["side"]) < 0 else "draw",
            "subject_healthy": healthy, "opponent_healthy": opponent_healthy,
            "complete": healthy and opponent_healthy, "logging_complete": logging_complete,
            "emitted_decisions": recognized, "parsed_decisions": observed,
            "logging_scope": "all sampled hierarchical choices under source emission contract, not all primitive SDK actions",
            "observations": observations}


def evaluate_request(request: dict, repo: Path, work: Path | None = None) -> dict:
    if not isinstance(request, dict) or request.get("schema", SCHEMA) != SCHEMA:
        raise EvaluationError(f"request schema must be {SCHEMA}")
    repo = Path(repo).resolve()
    # The local optimization engines never authorize updater/network work. Missing
    # pinned assets are an actionable error rather than an implicit installation.
    os.environ["CAMBC_OFFLINE"] = "true"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "true"
    kind = request.get("kind", "config")
    if kind == "fuel":
        fuel_path = Path(__file__).with_name("fuel_evaluator.py")
        spec = importlib.util.spec_from_file_location("optimizer_fuel_evaluator", fuel_path)
        if not spec or not spec.loader:
            raise EvaluationError("fuel adapter not installed")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        if hasattr(module, "evaluate_request"):
            return module.evaluate_request(request, repo, work or repo / ".cache/contenders/optimization/fuel")
        raise EvaluationError("fuel adapter does not expose evaluate_request")
    if kind not in ("config", "program", "policy", "identity"):
        raise EvaluationError(f"unsupported evaluation kind {kind!r}")
    expected_context = request.get("expected_context_identity")
    if "expected_context_identity" in request and (not isinstance(expected_context, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_context)):
        raise EvaluationError("expected_context_identity must be a lowercase SHA256 digest")
    core, games = load_arena(repo)
    default = frozen_default(repo)
    candidate = copy.deepcopy(request.get("candidate", {}))
    if not isinstance(candidate, dict):
        raise EvaluationError("candidate must be an object")
    controller = str(request.get("controller", candidate.get("base", default)))
    cache_arg = request.get("cache")
    cache = (Path(cache_arg) if cache_arg else repo / ".cache/contenders/optimization/native").resolve()
    if not cache.is_relative_to(repo / ".cache/contenders/optimization"):
        raise EvaluationError("native cache must be beneath .cache/contenders/optimization in this worker packet")
    cache.mkdir(parents=True, exist_ok=True)
    cases = normalize_cases(request, core, default)
    context_cases = normalize_cases(dict(request, cases=request.get("context_cases", request.get("cases"))),
                                    core, default, deduplicate=True)
    restoration = restore_population(core, request, {c["opponent"] for c in cases + context_cases}, cache)
    controller_ent = exact_entrant(core, controller)
    base_selector, base_knobs = baseline_spec(request, controller)
    baseline = configuration(core, base_selector, base_knobs, "baseline") if base_knobs else exact_entrant(core, base_selector, "baseline")
    opponents = {name: exact_entrant(core, name, "opponent")
                 for name in sorted({c["opponent"] for c in cases + context_cases})}
    resolved = [(c["map_relative"], c["side"], opponents[c["opponent"]].key) for c in cases]
    if len(resolved) != len(set(resolved)):
        raise EvaluationError("duplicate deterministic case after resolving opponent aliases")
    opponent_modules = {name: digest(entrant_identity(core, ent)["modules"]) for name, ent in opponents.items()}
    executable_cases = [(c["map_sha256"], c["side"], opponent_modules[c["opponent"]]) for c in cases]
    if len(executable_cases) != len(set(executable_cases)):
        raise EvaluationError("duplicate deterministic case uses byte-identical opponent modules")
    context_cases = list({(c["map_relative"], c["side"], opponents[c["opponent"]].key): c
                          for c in context_cases}.values())
    context_keys = {(c["map_relative"], c["side"], opponents[c["opponent"]].key) for c in context_cases}
    if any((c["map_relative"], c["side"], opponents[c["opponent"]].key) not in context_keys for c in cases):
        raise EvaluationError("played case is outside context_cases")
    target_kind = kind if kind != "identity" else request.get("target_kind", "policy" if "policy" in candidate else "program" if "tree" in candidate else "config")
    if target_kind not in ("config", "program", "policy"):
        raise EvaluationError("identity target_kind must be config, program, or policy")
    tools = environment_files(repo, core, target_kind in ("program", "policy"))
    # A physical native game depends on exact source/WASM plus engine/build/reader
    # semantics. A materializer's interpretation is additionally part of campaign
    # fitness context, but must not duplicate unchanged anchor games across C1/C2/C5.
    game_tools = environment_files(repo, core, False)
    context = {"schema": "contenders-native-context/v1", "engine": games.ENGINE,
               "tools": tools, "controller": entrant_identity(core, controller_ent),
               "baseline": entrant_identity(core, baseline),
               "cases": sorted([{"map": c["map_relative"], "map_sha256": c["map_sha256"],
                                  "side": c["side"], "opponent": entrant_identity(core, opponents[c["opponent"]])}
                                 for c in context_cases], key=canonical)}
    context_hash = digest(context)
    if expected_context is not None and context_hash != expected_context:
        raise EvaluationError(f"native context identity mismatch: expected {expected_context}, got {context_hash}; no candidate build or game launched")
    if kind == "identity":
        return {"schema": RESULT, "kind": kind, "healthy": True, "selection_eligible": True,
                "objective": 0.0, "environment_identity": context_hash, "context_identity": context_hash,
                "identity": context, "games_played": 0, "population_restoration": restoration}
    materializer, materialized = None, None
    if kind == "config":
        if "config_policy" in candidate:
            if candidate.get("knobs") or candidate.get("overrides"):
                raise EvaluationError("config_policy leaves contain knobs; top-level knob overrides are ambiguous")
            subject, metadata = contextual_configuration(core, controller_ent.key, candidate["config_policy"])
            materialized = {"kind": "config_policy", "metadata": metadata}
            candidate["config_policy"] = metadata["policy"]
        else:
            subject = configuration(core, controller, candidate.get("knobs", candidate.get("overrides", {})), "candidate")
    else:
        materializer = load_materializer()
        if "base" in candidate and exact_entrant(core, candidate["base"]).key != controller_ent.key:
            raise EvaluationError("candidate.base and request.controller resolve to different frozen sources")
        candidate.setdefault("base", controller)
        candidate["collect_rollouts"] = bool(request.get("collect_rollouts", False))
        if kind == "policy" and "seed" in request:
            candidate.setdefault("seed", request["seed"])
        with core.FileLock(program_lock_path(core)):
            materializer_candidate = dict(candidate, base=materializer_base(core, controller_ent.key))
            materialized = materializer.materialize_program(repo, materializer_candidate, kind)
            if not isinstance(materialized, dict) or not materialized.get("bot"):
                raise EvaluationError("materializer returned no generated bot selector")
            if materialized.get("metadata", {}).get("base_source_sha256") != context["controller"]["source_sha256"]:
                raise EvaluationError("materializer copied base source different from the pinned controller")
            b = core.bot(materialized["bot"])
            if b.suite != "contenders" or not b.name.startswith("_optimizer-"):
                raise EvaluationError("materializer escaped the owned optimizer bot namespace")
            contents = core.read_closure(b)
            subject = core.prepare(b, label="candidate", contents=contents)
            if not subject.ok:
                raise EvaluationError(f"program build failed: {subject.log[-3000:]}")
    candidate_identity = entrant_identity(core, subject)
    def evaluate_case(case):
        opponent = opponents[case["opponent"]]
        candidate_cell = cache_cell(core, games, subject, opponent, case, game_tools, cache)
        baseline_cell = cache_cell(core, games, baseline, opponent, case, game_tools, cache)
        side, other = case["side"], "B" if case["side"] == "A" else "A"
        row, base_row = candidate_cell["row"], baseline_cell["row"]
        subject_health, opponent_health = games.health(row, side), games.health(row, other)
        baseline_health, baseline_opponent_health = games.health(base_row, side), games.health(base_row, other)
        complete = all(r.get("winner") in ("A", "B", "draw") and not r.get("truncated") for r in (row, base_row))
        if not complete:
            subject_health = subject_health + ["incomplete_game"]
        own, enemy = row.get(side, {}), row.get(other, {})
        bo, be = base_row.get(side, {}), base_row.get(other, {})
        result = {"case_id": digest({"map": case["map_relative"], "side": side, "opponent": opponent.key}),
                  "map": case["map_name"], "side": side, "opponent": opponent.key,
                  "requested_opponent": case["opponent"], "complete": complete,
                  "payoff": payoff(row, side), "baseline_payoff": payoff(base_row, side),
                  "margin": float(own.get("gems", 0) - enemy.get("gems", 0)),
                  "baseline_margin": float(bo.get("gems", 0) - be.get("gems", 0)),
                  "rounds": row.get("rounds", 0), "baseline_rounds": base_row.get("rounds", 0),
                  "health": subject_health, "opponent_health": opponent_health,
                  "baseline_health": baseline_health, "baseline_opponent_health": baseline_opponent_health,
                  "identity": {"subject_key": subject.key, "baseline_key": baseline.key,
                               "opponent_key": opponent.key, "map_sha256": case["map_sha256"],
                               "engine": games.ENGINE, "toolchain": digest(game_tools)},
                  "candidate_cache": {"key": candidate_cell["key"], "hit": candidate_cell["cache_hit"],
                                      "archive": candidate_cell.get("archive")},
                  "baseline_cache": {"key": baseline_cell["key"], "hit": baseline_cell["cache_hit"],
                                     "archive": baseline_cell.get("archive")}}
        episode = None
        if kind == "policy" and materializer is not None:
            episode = episodes_for(materializer, candidate_cell, case, subject.key, games.ENGINE,
                                   complete and not subject_health, not opponent_health,
                                   bool(request.get("collect_rollouts", False)))
            episode.update({"baseline_payoff": payoff(base_row, side),
                            "baseline_trajectory_id": baseline_cell["key"],
                            "baseline_source_key": baseline.key,
                            "baseline_healthy": complete and not baseline_health and not baseline_opponent_health,
                            "baseline_provenance": {"map_sha256": case["map_sha256"], "side": side,
                                                    "opponent_key": opponent.key, "engine": games.ENGINE,
                                                    "action_independent": True}})
        return result, episode, [candidate_cell, baseline_cell]
    parallel = request.get("parallel_cases", request.get("jobs", min(4, len(cases))))
    if isinstance(parallel, bool) or not isinstance(parallel, int) or parallel < 1 or parallel > 256:
        raise EvaluationError("parallel_cases must be an integer from 1 to 256")
    # Each actual match obtains arena's existing machine-wide slot. This pool is a
    # bound on in-flight case work, never a reservation of cores or memory.
    with ThreadPoolExecutor(max_workers=min(parallel, len(cases))) as pool:
        evaluated = list(pool.map(evaluate_case, cases))
    results = [item[0] for item in evaluated]
    episodes = [item[1] for item in evaluated if item[1] is not None]
    all_cells = [cell for item in evaluated for cell in item[2]]
    subject_healthy = all(r["complete"] and not r["health"] for r in results)
    opponent_healthy = all(not r["opponent_health"] and not r["baseline_opponent_health"] for r in results)
    baseline_healthy = all(r["complete"] and not r["baseline_health"] for r in results)
    healthy = subject_healthy and opponent_healthy and baseline_healthy
    if environment_files(repo, core, target_kind in ("program", "policy")) != tools:
        raise EvaluationError("native evaluation context files changed while evaluating cases")
    if entrant_identity(core, controller_ent) != context["controller"] or entrant_identity(core, baseline) != context["baseline"]:
        raise EvaluationError("pinned controller or baseline bytes changed during evaluation")
    for case in cases:
        verify_game_inputs(core, case, game_tools)
    exported, artifacts = export_entrant(core, subject, cache / "artifacts")
    specification = {"schema": SCHEMA, "kind": kind, "controller": controller, "candidate": candidate,
                     "baseline": request.get("baseline", controller),
                     "cases": [{"map": c["map"], "side": c["side"], "opponent": opponents[c["opponent"]].key} for c in cases],
                     "environment_identity": context_hash}
    for field in ("population_policies", "population_configs"):
        if field in request:
            specification[field] = request[field]
    spec_path = cache / "artifacts" / f"{digest(specification)}.candidate.json"
    if not spec_path.exists():
        write_json(spec_path, specification)
    artifacts.append(descriptor(spec_path, "candidate_specification"))
    # Search engines copy finalist artifacts to durable output. Nonfinalists reuse this
    # shared archive; no uncompressed per-candidate replay/statistics duplication is needed.
    archive_paths = sorted({cell["archive"] for cell in all_cells if cell.get("archive")})
    artifacts.extend(descriptor(Path(path), "native_evidence") for path in archive_paths)
    return {"schema": RESULT, "adapter_version": VERSION, "kind": kind,
            "objective": statistics.mean(r["payoff"] for r in results),
            "paired_objective": statistics.mean(r["payoff"] - r["baseline_payoff"] for r in results),
            "mean_margin": statistics.mean(r["margin"] for r in results),
            "paired_margin": statistics.mean(r["margin"] - r["baseline_margin"] for r in results),
            "healthy": healthy, "selection_eligible": healthy,
            "subject_healthy": subject_healthy, "opponent_healthy": opponent_healthy,
            "baseline_healthy": baseline_healthy, "cases": results,
            "environment_identity": context_hash, "context_identity": context_hash, "identity": context,
            "candidate_identity": candidate_identity, "subject_key": subject.key,
            "export": dict(exported, candidate_spec=descriptor(spec_path, "candidate_specification")),
            "artifacts": artifacts, "episodes": episodes,
            "rollouts": [obs for episode in episodes for obs in episode["observations"]],
            "materialization": materialized,
            "population_restoration": restoration,
            "games_played": sum(not cell["cache_hit"] for cell in all_cells),
            "cache_hits": sum(cell["cache_hit"] for cell in all_cells),
            "coverage": {"cases": len(results), "maps": len({r["map"] for r in results}),
                         "sides": sorted({r["side"] for r in results}),
                         "unique_opponent_modules": len({opponent_modules[c["opponent"]] for c in cases}),
                         "opponents": sorted({r["opponent"] for r in results})},
            "limitations": ["Native terminal outcomes on this explicit case distribution, not fresh independent samples.",
                            "Selection rejects subject AND opponent health contamination in either paired arm.",
                            "Project-path input pins both raw/injected modules; no unsupported consumed-role inference.",
                            "Hierarchical policy logs cover sampled choices under the declared emission contract, not all SDK actions."]}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--request", type=Path, required=True)
    p.add_argument("--response", type=Path, required=True)
    p.add_argument("--repo", type=Path, required=True)
    args = p.parse_args()
    started = time.monotonic()
    try:
        request = read_json(args.request)
        result = evaluate_request(request, args.repo, args.response.parent)
        result["request_sha256"] = digest(request)
        if "request_id" in request:
            result["request_id"] = request["request_id"]
        result["elapsed_seconds"] = time.monotonic() - started
        write_json(args.response, result)
        return 0
    except Exception as exc:
        write_json(args.response, {"schema": RESULT, "healthy": False, "selection_eligible": False,
                   "objective": 0.0, "error": f"{type(exc).__name__}: {exc}",
                   "elapsed_seconds": time.monotonic() - started})
        print(f"native evaluator error: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
