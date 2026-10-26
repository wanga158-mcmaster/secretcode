"""Specialists' executable population contracts; never infer a mechanism from score.

Fireflies' existing production BC26T stream is bounded and can be censored. Counts
below are witnessed lower bounds, not estimates of all eligible turns. Native
activation needs a threshold-only request, an actual tribute start/completion,
and an independently decoded living champion moving with the bag then losing it
at the altar. No probe-mode or alternate logging build supplies admission evidence.
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import re
import subprocess
import sys
import threading
import zipfile
from functools import lru_cache
from pathlib import Path

import core

ROOT = Path(__file__).resolve().parents[3]
BOT = "specialists/fireflies"
OBSERVER = "bots/specialists/tests/population.py"
# Explicit repository-local observer import/launcher closure. The Python bridge
# eagerly imports compiled_runner and its local modules even though this path
# does not call their game/evaluation functions. Decoder assets, Node/Python and
# each replay/map remain separately pinned runtime/input identities.
OBSERVER_PATHS = (
    OBSERVER,
    "tools/inspect-strategy-replay.mjs",
    "tools/replay-interaction-native.mjs",
    "tools/replay-interaction-events.mjs",
    "tools/replay-native-slot.py",
    "tools/replay-profiles.mjs",
    "tools/replay-profiles.json",
    "tools/arena/core.py",
    "tools/arena/compiled_runner.py",
    "tools/arena/games.py",
    "tools/arena/health_context.py",
    "tools/arena/panel_executables.py",
    "tools/arena/population.py",
    "maps/geometry.json",
    "docs/reference/runtime-catalog.json",
)
ACTORS = {"tavi": 11, "varg": 10, "vendetta": 2, "vesper_vale": 1, "marcella": 7}
FRAME = re.compile(rb"BC26T(\{[^\x00\n]*\})")
# Dynamic adapter instances share the canonical bridge and its import lock.
_BRIDGE_NAME = "_arena_specialists_replay_native_slot"
_BRIDGE_IMPORT_LOCK = core.__dict__.setdefault("_specialists_replay_bridge_import_lock", threading.Lock())


def contracts():
    legacy = {
        "id": "specialists/fireflies/small-bag-bank",
        "bot": BOT,
        "interaction": "Leave production for an altar with 12-23 gems while farther than four tiles away; finish the small tribute and return to work.",
        "preconditions": "Alive, not capturing/recovering/channeling, an altar exists, bag 12-23, nearest altar distance >4, before the final 100 rounds.",
        "activation": "Production bank() requests threshold 12 and physically carries the bag toward the altar; tribute_start succeeds.",
        "continuation": "The same actor finishes tribute alive with zero bag, then resumes a moving production turn.",
        "refusal": "Production bank_reason rejects empty bags and bags below 12 outside nearby/final-round exceptions; an empty living actor keeps working in native history.",
        "control": "core_enabled=false (threshold 24), same source closure and roster",
        "neighbor": "specialists/breakpoint-brigade (same threshold-24 banking chassis, different purchase rule)",
        "positive_fixture": "bots/specialists/fireflies/tests/fireflies-policy.cpp:12-23 threshold boundary",
        "refusal_fixture": "bots/specialists/fireflies/tests/fireflies-policy.cpp:0/11 refusal and unchanged nearby/final-round clauses",
        "observer": OBSERVER,
        "test_paths": ["bots/specialists/fireflies/tests", "tools/arena/wasi.py", "tools/arena/wasi-run.mjs"],
        "observer_paths": list(OBSERVER_PATHS),
        "scope": "Small-bag conversion; native counts censored by the existing 32-detail-per-actor telemetry cap. Health and healthy cross-suite coverage are separate.",
        "match_maps": ["00lost-world", "00collision"],
        "health_evidence": "results/population/fireflies-startup/default-health.json",
        "gap": "80 full-length pool/fundamentals games found 20 round-one exhaustions on road-of-riches/islands and one islands-B stall. Healthy cross-suite coverage is absent.",
        "next_check": "Use the explicit three-knob repair variant to exercise small-bag banking; working-source defaults retain this original unhealthy executable.",
    }
    repaired = dict(legacy)
    repaired.update({
        "id": "specialists/fireflies/small-bag-bank-repaired",
        "overrides": {"FIREFLIES_BOUNDED_STARTUP": "1", "FIREFLIES_CONNECTED_CATALOG": "1", "FIREFLIES_CAPTURE_DEFENSE": "1"},
        "control": "The same three repair overrides plus core_enabled=false; threshold 24 on the same roster and repaired executor.",
        "scope": "Explicit experimental executable: bounded immutable setup, static ANY-member connectivity, and in-place capture defense/finite failed-progress hold. Banking remains threshold 12; all working-source defaults remain off.",
        "health_evidence": "results/population/fireflies-startup/repaired-health.json",
        "gap": "The exact repaired executable is clean in 80 frozen-reference games and 80 Harvest-and-Exit games, each across all 40 pool maps and both sides. Harvest-and-Exit has 10 exhaustions and 12 stalls, so it does not qualify healthy cross-suite coverage. A reached-checkpoint Collision-B comparison establishes scoped conditional control/neighbor physical distinction at B470; it does not establish the historical Breakpoint decision, autonomous joint reachability, native guest fuel safety or fresh applicability of that comparison.",
        "next_check": "Qualify an independently healthy cross-suite opponent and test applicability under a new condition. Keep all three working-source defaults off; the conditional distinction does not waive native activation/continuation or health gates.",
        "conditional_comparison_evidence": "results/population/fireflies-caller/comparison.json",
    })
    return [legacy, repaired]


def _path(root, value):
    base = Path(root).resolve()
    path = Path(value)
    path = (base / path).resolve() if not path.is_absolute() else path.resolve()
    if not path.is_relative_to(base):
        raise ValueError("specialists observer path is outside repository")
    return path


def records(replay, side):
    if side not in ("A", "B"):
        raise ValueError("witness side must be A or B")
    with zipfile.ZipFile(replay) as archive:
        raw = archive.read(f"team-{side.lower()}.stdout")
    rows = [json.loads(match) for match in FRAME.findall(raw)]
    return [r for r in rows if r.get("s") == 1 and r.get("c") == "fireflies"]


def _settings(root):
    return json.loads((Path(root) / "docs/reference/runtime-catalog.json").read_text(encoding="utf-8"))["result"]["settings"]


def _geometry(root, map_name):
    rows = json.loads((Path(root) / "maps/geometry.json").read_text(encoding="utf-8"))["maps"]
    geo = next(m for m in rows if m["id"].replace("_", "-") == map_name.replace("_", "-"))
    actual = hashlib.sha256(core.map_path(map_name).read_bytes()).hexdigest()
    if geo["mapSha256"] != actual:
        raise ValueError("altar geometry does not match the current map")
    width = geo["width"]
    return geo, [(tile % width, tile // width) for tile in geo["terrain"].get("altar", [])]


def _distance(sample, altars):
    return min(max(abs(sample[1] - x), abs(sample[2] - y)) for x, y in altars)


def _replay_bridge():
    path = ROOT / "tools/replay-native-slot.py"
    with _BRIDGE_IMPORT_LOCK:
        bridge = sys.modules.get(_BRIDGE_NAME)
        if bridge is None:
            spec = importlib.util.spec_from_file_location(_BRIDGE_NAME, path)
            if spec is None or spec.loader is None:
                raise ValueError("official native history bridge is unavailable")
            bridge = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(bridge)
            sys.modules[_BRIDGE_NAME] = bridge
        if Path(bridge.__file__).resolve() != path.resolve() or not callable(getattr(bridge, "run", None)):
            raise ValueError("official native history bridge identity mismatch")
        return bridge


@lru_cache(maxsize=64)
def _decode(replay, first, last):
    command = [str(core.NODE), "--disable-warning=ExperimentalWarning", str(ROOT / "tools/inspect-strategy-replay.mjs"),
               "--replay", str(replay), "--actor", "all", "--from-round", str(max(1, first)),
               "--to-round", str(min(2000, last)), "--captures", "0", "--seek", "1"]
    stdout, stderr = io.BytesIO(), io.BytesIO()
    try:
        status = _replay_bridge().run(command, stdout=stdout, stderr=stderr)
    except Exception as error:
        detail = stderr.getvalue().decode("utf-8", errors="replace")[-300:]
        raise ValueError("official native history decoder transport failed: " + repr(error)[-300:] + "; " + detail) from error
    if status:
        raise ValueError("official native history decoder failed: " + stderr.getvalue().decode("utf-8", errors="replace")[-300:])
    decoded = json.loads(stdout.getvalue())
    if decoded.get("replaySha256") != hashlib.sha256(Path(replay).read_bytes()).hexdigest():
        raise ValueError("decoded replay hash mismatch")
    return decoded


def _track(decoded, side, actor):
    team = 0 if side == "A" else 1
    return next(t["samples"] for t in decoded["tracks"] if t["team"] == team and t["type"] == ACTORS[actor])


def _physical_chain(replay, side, activation, start, complete, root, map_name):
    _, altars = _geometry(root, map_name)
    settings = _settings(root)
    first, last = activation["r"], complete["r"]
    if last - first > 64:
        return None
    decoded = _decode(str(replay), max(1, first - 1), last + 3)
    samples = _track(decoded, side, activation["a"])
    by_round = {s[0]: s for s in samples}
    before, arrived, after = (by_round.get(n) for n in (first, start["r"], last))
    bag = activation["d"]["bag"]
    if not before or not arrived or not after or any(s[3] <= 0 for s in (before, arrived, after)):
        return None
    if before[5] != bag or arrived[5] != bag or after[5] != 0:
        return None
    if _distance(before, altars) <= 4 or _distance(arrived, altars) > settings["submit_gems_range"]:
        return None
    if before[1:3] == arrived[1:3] or activation["d"]["nearest"] != _distance(before, altars):
        return None
    resumed = next((s for s in samples if s[0] > last and s[3] > 0 and s[1:3] != after[1:3]), None)
    if resumed is None:
        return None
    return {"actor": activation["a"], "activation_round": first, "tribute_round": start["r"],
            "complete_round": last, "resume_round": resumed[0], "bag": bag,
            "start_position": before[1:3], "altar_position": arrived[1:3], "resume_position": resumed[1:3]}


def native_witnesses(row, replay, side, root=ROOT, physical=True):
    stream = records(replay, side)
    settings = _settings(root)
    profiles = [r for r in stream if r.get("k") == "start" and r.get("d", {}).get("subtype") == "profile"]
    if not profiles or any(r["d"].get("probe_mode") is not False for r in profiles):
        raise ValueError("native witness is missing production profiles or uses probe_mode")
    activations = [r for r in stream if r.get("k") == "activation" and r.get("d", {}).get("subtype") == "fireflies_bank"
                   and r["d"].get("reason") == "threshold" and r["d"].get("threshold") == 12
                   and 12 <= r["d"].get("bag", 0) < 24 and r["d"].get("nearest", 0) > 4
                   and r["r"] < settings["max_rounds"] - 100]
    continuations, witnessed = [], []
    for activation in activations:
        following = [r for r in stream if r.get("a") == activation["a"] and r["r"] >= activation["r"]]
        start = next((r for r in following if r.get("d", {}).get("subtype") == "tribute_start"
                      and r["d"].get("bag") == activation["d"]["bag"]), None)
        if not start:
            continue
        complete = next((r for r in following if r.get("d", {}).get("subtype") == "tribute_result"
                         and r["d"].get("start_round") == start["r"] and r["d"].get("complete") is True
                         and r["d"].get("remaining_bag") == 0 and r["d"].get("starting_bag") == activation["d"]["bag"]
                         and r["d"].get("team_score_delta", 0) >= activation["d"]["bag"]), None)
        if not complete:
            continue
        continuations.append((activation, start, complete))
        if physical and not witnessed:
            chain = _physical_chain(replay, side, activation, start, complete, root, row["map"])
            if chain:
                witnessed.append(chain)
    return {"opportunities": len(activations), "activations": len(activations), "continuations": len(continuations),
            "opportunities_are_lower_bound": True, "physical_witnesses": witnessed,
            "condition": {"map": row["map"], "map_sha256": hashlib.sha256(core.map_path(row["map"]).read_bytes()).hexdigest(),
                          "opponent_snapshot": row["b" if side == "A" else "a"]},
            "coverage_note": "Counts include recorded threshold-only admissions, censored by production detail cap; absent logs do not establish zero opportunities."}


def observe(contract, rows, root=ROOT):
    results, missing = [], []
    expected_key = f"{BOT}@{core.version(core.bot(BOT), contract.get('overrides') or None)}"
    for row in rows:
        side = "A" if row.get("a") == expected_key else "B" if row.get("b") == expected_key else None
        if not side:
            continue
        if not row.get("replay"):
            missing.append({"map": row.get("map"), "side": side, "reason": "native replay unavailable"})
            continue
        try:
            result = native_witnesses(row, _path(root, row["replay"]), side, root)
            results.append(dict(result, map=row["map"], side=side))
        except (OSError, ValueError, KeyError, StopIteration, zipfile.BadZipFile) as exc:
            missing.append({"map": row.get("map"), "side": side, "reason": str(exc)})
    verified = any(r["physical_witnesses"] for r in results)
    return {"behavior": contract["id"], "games": results, "unavailable": missing,
            "verified": verified, "opportunities": sum(r["opportunities"] for r in results),
            "activations": sum(r["activations"] for r in results),
            "continuations": sum(len(r["physical_witnesses"]) for r in results),
            "physical_continuations": sum(len(r["physical_witnesses"]) for r in results),
            "status": "witnessed" if verified else "not established"}


def _witness(evidence, role, root):
    spec = evidence.get("witnesses", {}).get(role, {})
    artifacts = evidence["artifacts"]
    row_name, replay_name = spec["row"], spec["replay"]
    row = json.loads(_path(root, artifacts[row_name]["path"]).read_text(encoding="utf-8"))
    replay = _path(root, artifacts[replay_name]["path"])
    side = spec["side"]
    if row.get("replay") != str(replay.relative_to(Path(root))).replace("\\", "/"):
        raise ValueError("witness row does not identify its pinned replay")
    return row, replay, side, [row_name, replay_name]


def _conditional_distinct(contract, evidence, root):
    """Re-execute actual production callers; a saved case/pass flag is insufficient."""
    if Path(root).resolve() != ROOT.resolve():
        raise ValueError("conditional caller verification requires the current local repository")
    expected = {"FIREFLIES_BOUNDED_STARTUP": "1", "FIREFLIES_CONNECTED_CATALOG": "1", "FIREFLIES_CAPTURE_DEFENSE": "1"}
    if contract.get("overrides") != expected or evidence["identity"]["snapshot"] != "specialists/fireflies@f6c57ee622a9":
        raise ValueError("conditional caller proof covers only the exact repaired snapshot")
    spec = evidence["conditional_caller"]
    if not isinstance(spec, dict) or set(spec) != {"comparison", "supporting"}:
        raise ValueError("conditional caller inputs must name pinned comparison and supporting artifacts")
    descriptors = []
    for role in ("comparison", "supporting"):
        name = spec[role]
        descriptor = evidence["artifacts"][name]
        if set(descriptor) != {"path", "sha256"}:
            raise ValueError("conditional caller descriptor has unexpected fields")
        path = _path(root, descriptor["path"])
        if hashlib.sha256(path.read_bytes()).hexdigest() != descriptor["sha256"]:
            raise ValueError("conditional caller artifact hash mismatch")
        descriptors.append((name, path, descriptor["sha256"]))
    command = [sys.executable, "-B", str(ROOT / "bots/specialists/fireflies/tests/caller-verify.py"),
               "--report", str(descriptors[0][1]), "--sha256", descriptors[0][2],
               "--supporting", str(descriptors[1][1]), "--supporting-sha256", descriptors[1][2]]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise ValueError("independent actual-caller reconstruction failed: " + (result.stdout + result.stderr)[-500:])
    proof = json.loads(result.stdout)
    if proof.get("status") != "pass" or proof.get("snapshot") != evidence["identity"]["snapshot"]:
        raise ValueError("conditional actual callers did not establish a physical replacement difference")
    proof["artifacts"] = [name for name, _, _ in descriptors]
    test = evidence.get("tests", {}).get("artifact")
    if test:
        proof["artifacts"].append(test)
    proof["historical_mode"] = "incomplete; matching position/bag does not reveal the historical neighbor's caller preconditions"
    return proof


def evaluate(contract, evidence, root=ROOT):
    """Reconstruct only completed physical cases; unresolved comparisons stay false."""
    out = {}
    for role in ("positive", "fresh"):
        try:
            row, replay, side, artifacts = _witness(evidence, role, root)
            if row[side.lower()] != evidence["identity"]["snapshot"]:
                raise ValueError("witness has another subject snapshot")
            seen = native_witnesses(row, replay, side, root)
            out[role] = dict(seen, artifacts=artifacts, status="pass" if seen["physical_witnesses"] else "incomplete")
        except (OSError, ValueError, KeyError, StopIteration, zipfile.BadZipFile) as exc:
            out[role] = {"status": "incomplete", "reason": str(exc)}
    # Production kernel boundaries are exercised by the pinned runner. Native
    # empty-bag work is reconstructed independently, rather than inferring a
    # refusal from absence of a bounded telemetry frame.
    try:
        row, replay, side, artifacts = _witness(evidence, "positive", root)
        decoded = _decode(str(replay), 1, 12)
        live_work = [s for t in decoded["tracks"] if t["team"] == (0 if side == "A" else 1)
                     for s, n in zip(t["samples"], t["samples"][1:]) if s[3] > 0 and n[3] > 0
                     and s[5] == 0 and n[5] == 0 and s[1:3] != n[1:3]]
        test_name = evidence.get("tests", {}).get("artifact")
        out["refusal"] = {"status": "pass" if live_work and test_name else "incomplete",
                          "artifacts": artifacts + ([test_name] if test_name else []),
                          "refusals": len(live_work),
                          "scope": "Empty living actors keep moving; production fixture separately checks 11-gem refusal and shared exception clauses."}
    except (OSError, ValueError, KeyError, StopIteration, zipfile.BadZipFile) as exc:
        out["refusal"] = {"status": "incomplete", "reason": str(exc)}
    # This historical mode searches illustrative own-state matches. It does not
    # establish the neighbor's complete caller preconditions and never passes
    # distinction. Section9 also allows a declared conditional native-world
    # intervention with compatible reached private memory. That separate route
    # below reconstructs complete actual callers and checked native actions.
    try:
        active, replay, side, artifacts = _witness(evidence, "positive", root)
        chain = out["positive"]["physical_witnesses"][0]
        first, last, actor = chain["activation_round"], chain["complete_round"], chain["actor"]
        active_track = _track(_decode(str(replay), first, last), side, actor)
        active_first = next(s for s in active_track if s[0] == first)
        comparisons, snapshots = [], {}
        for role in ("control", "neighbor"):
            row, compare_replay, compare_side, refs = _witness(evidence, role, root)
            opponent = "b" if side == "A" else "a"
            if compare_side != side or row["map"] != active["map"] or row[opponent] != active[opponent]:
                raise ValueError("counterfactual games do not share map, side and opponent")
            key = row[side.lower()]
            if role == "control":
                if not key.startswith(BOT + "@"):
                    raise ValueError("control is not Fireflies")
                meta = json.loads((Path(root) / ".cache/arena/snap" / key.rsplit("@", 1)[1] / "meta.json").read_text(encoding="utf-8"))
                expected_overrides = dict(contract.get("overrides") or {})
                expected_overrides["core_enabled"] = "false"
                if meta.get("overrides") != expected_overrides:
                    raise ValueError("control has extra treatments or did not disable the commitment")
            elif not key.startswith("specialists/breakpoint-brigade@"):
                raise ValueError("wrong nearest neighbor")
            decoded = _decode(str(compare_replay), max(1, first - 8), min(2000, last + 32))
            samples = _track(decoded, side, actor)
            same = next((s for s in samples if s[1:] == active_first[1:] and (role != "control" or s[0] == first)), None)
            if same is None:
                raise ValueError("no same loaded own-state in comparator")
            at_completion = next((s for s in samples if s[0] == same[0] + last - first), None)
            if not at_completion or at_completion[3] <= 0 or at_completion[5] != chain["bag"]:
                raise ValueError("comparator does not retain the bag through active completion")
            comparisons.append({"role": role, "matched_round": same[0], "active_round": first,
                                "bag_still_carried_at_offset": last - first, "bag": at_completion[5]})
            snapshots[role] = key
            artifacts.extend(refs)
        test_name = evidence.get("tests", {}).get("artifact")
        out["distinct"] = {"status": "incomplete",
                           "artifacts": artifacts + ([test_name] if test_name else []),
                           "physical_difference": "The 21-gem holder leaves for tribute and finishes before threshold-24 control and neighbor from the same loaded own-state.",
                           "replacement_removes_coverage": True, "control_snapshot": snapshots["control"],
                           "neighbor_snapshot": snapshots["neighbor"], "comparisons": comparisons,
                           "scope": "One map/side/opponent own-state comparison; neighbor reaches it later, so this is not a globally identical game state.",
                           "reason": "Control/kernel evidence supports the threshold cause; neighbor full branch-preemption inputs are not observed, so its native comparison remains illustrative."}
    except (OSError, ValueError, KeyError, IndexError, StopIteration, zipfile.BadZipFile) as exc:
        out["distinct"] = {"status": "incomplete", "reason": str(exc)}
    if evidence.get("conditional_caller") is not None:
        try:
            out["distinct"] = _conditional_distinct(contract, evidence, root)
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
            out["distinct"] = {"status": "incomplete", "reason": str(exc), "scope": "conditional actual-caller reconstruction"}
    return out
