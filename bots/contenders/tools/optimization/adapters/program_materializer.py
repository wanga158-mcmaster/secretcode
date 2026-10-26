"""Materialize bounded, information-safe native planner programs for optimization.

The generated bot keeps the selected frozen controller's legality and execution. C1
changes its positive camp-job scores through a bounded conditional tree. C5/C6 use a
small actor choosing one of five job preferences at most once per scoring turn.
This is a hierarchical planning interface, not a primitive-action RL environment.

The caller must hold the native evaluator's materialization lock until arena has
captured the source closure. There is exactly one generated working bot; arena owns
immutable content-keyed snapshots. No code is written into a reference bot.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
from typing import Any

SCHEMA = "contenders-program-materialization/v1"
POLICY_SCHEMA = "contenders-policy/v1"
TRACE_SCHEMA = "contenders-actor-observation/v1"
ACTOR_FEATURE_SCHEMA = "contenders-actor-context/v1"
TREE_FEATURE_SCHEMA = "contenders-job-context/v1"
FEATURE_NAMES = ["roundFraction", "hpFraction", "bagFraction", "goldFraction",
                 "enemyVisibleFraction", "alliesVisibleFraction", "travelFraction",
                 "groupGemFraction", "groupGoldFraction", "groupHpFraction",
                 "groupHarmless", "homeDistanceFraction"]
ACTION_NAMES = ["neutral", "gems", "gold", "short", "home"]
GENERATED_BOT = "contenders/_optimizer-program"
MAX_DEPTH, MAX_NODES = 6, 127
MAX_FACTOR, MIN_FACTOR = 8.0, 0.125
SELECTOR = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")


def _number(value: Any, label: str, low: float, high: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    value = float(value)
    if not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"{label} must be finite and in [{low}, {high}]")
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _integer(value: Any, label: str, low: int, high: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{label} must be an integer in [{low}, {high}]")
    return value


def validate_tree(tree: Any) -> dict:
    """Return the exact float32 tree accepted by the generated C++ evaluator."""
    count = 0

    def visit(node: Any, depth: int) -> dict:
        nonlocal count
        count += 1
        if depth > MAX_DEPTH or count > MAX_NODES:
            raise ValueError(f"program exceeds depth {MAX_DEPTH} / nodes {MAX_NODES}")
        if not isinstance(node, dict):
            raise ValueError("a program node must be an object")
        if set(node) == {"factor"}:
            return {"factor": _number(node["factor"], "leaf factor", MIN_FACTOR, MAX_FACTOR)}
        if set(node) != {"feature", "threshold", "left", "right"}:
            raise ValueError("node must be a factor leaf or feature/threshold/left/right branch")
        return {"feature": _integer(node["feature"], "feature", 0, 11),
                "threshold": _number(node["threshold"], "threshold", 0.0, 1.0),
                "left": visit(node["left"], depth + 1),
                "right": visit(node["right"], depth + 1)}

    return visit(tree, 0)


def _float_literal(value: float) -> str:
    # Hexadecimal float literals preserve the exact stored float32 value, including
    # threshold-edge behavior. Never round the program again when generating C++.
    return float(value).hex() + "f"


def _tree_cpp(tree: dict) -> str:
    if "factor" in tree:
        return _float_literal(tree["factor"])
    return (f"(x[{tree['feature']}] <= {_float_literal(tree['threshold'])} ? "
            f"{_tree_cpp(tree['left'])} : {_tree_cpp(tree['right'])})")


def _max_factor(tree: dict) -> float:
    return tree["factor"] if "factor" in tree else max(_max_factor(tree["left"]), _max_factor(tree["right"]))


def validate_policy(candidate: dict) -> dict:
    raw = candidate.get("policy", candidate.get("actor", candidate))
    if not isinstance(raw, dict):
        raise ValueError("policy must be an object")
    if raw.get("schema", POLICY_SCHEMA) != POLICY_SCHEMA:
        raise ValueError("unsupported policy schema")
    if raw.get("feature_schema", ACTOR_FEATURE_SCHEMA) != ACTOR_FEATURE_SCHEMA:
        raise ValueError("policy feature_schema does not match the native actor interface")
    # Native inference clamps logits at twenty. Imported policy metadata must not
    # request another distribution and silently execute this fixed runtime instead.
    logit_clip = _number(raw.get("logit_clip", 20.0), "logit_clip", 20.0, 20.0)
    if "feature_names" in raw and raw["feature_names"] != FEATURE_NAMES:
        raise ValueError("policy feature_names do not match the native actor interface")
    if "action_names" in raw and raw["action_names"] != ACTION_NAMES:
        raise ValueError("policy action_names do not match the native actor interface")
    rows = raw.get("weights")
    if not isinstance(rows, list) or len(rows) != 5 or any(not isinstance(row, list) or len(row) != 13 for row in rows):
        raise ValueError("policy weights must have shape [5][13] (12 features then bias)")
    weights = [[_number(value, f"weights[{a}][{j}]", -16.0, 16.0)
                for j, value in enumerate(row)] for a, row in enumerate(rows)]
    seed = _integer(raw.get("seed", candidate.get("seed", 1)), "policy seed", 0, 0xFFFFFFFF)
    maximum = _integer(raw.get("max_decisions", 128), "max_decisions", 1, 256)
    stochastic, trace = raw.get("stochastic", True), raw.get("trace", candidate.get("collect_rollouts", False))
    if not isinstance(stochastic, bool) or not isinstance(trace, bool):
        raise ValueError("stochastic and trace must be booleans")
    return {"schema": POLICY_SCHEMA, "feature_schema": ACTOR_FEATURE_SCHEMA,
            "logit_clip": logit_clip, "feature_names": FEATURE_NAMES, "action_names": ACTION_NAMES,
            "weights": weights, "seed": seed, "max_decisions": maximum,
            "stochastic": stochastic, "trace": trace}


def _config_header(kind: str, tree: dict | None, policy: dict | None) -> str:
    is_policy = kind == "policy"
    upper = 4.0 if is_policy else _max_factor(tree or {"factor": 1.0})
    weights = policy["weights"] if policy else [[0.0] * 13 for _ in range(5)]
    rows = ",\n".join("    {" + ",".join(_float_literal(x) for x in row) + "}" for row in weights)
    expression = _tree_cpp(tree or {"factor": 1.0})
    return f'''#pragma once
// Generated from validated JSON. Float32 literals are exact; do not hand edit.
namespace mb::optimizer::generated {{
inline constexpr bool is_policy = {str(is_policy).lower()};
inline constexpr bool trace = {str(bool(policy and policy["trace"])).lower()};
inline constexpr bool stochastic = {str(bool(policy and policy["stochastic"])).lower()};
inline constexpr unsigned seed = {policy["seed"] if policy else 1}u;
inline constexpr unsigned max_decisions = {policy["max_decisions"] if policy else 128}u;
inline constexpr float bound = {_float_literal(upper)};
inline constexpr float weights[5][13] = {{
{rows}
}};
inline float program(const float x[12]) {{ return {expression}; }}
}} // namespace mb::optimizer::generated
'''


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".materializing")
    temporary.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    os.replace(temporary, path)


def materialize_program(repo: Path, candidate: dict, kind: str = "program") -> dict:
    if kind not in {"program", "policy"} or not isinstance(candidate, dict):
        raise ValueError("materializer needs program/policy kind and object candidate")
    repo = Path(repo).resolve()
    base = candidate.get("base", "anchors/main-v42")
    if not isinstance(base, str) or not SELECTOR.fullmatch(base) or base == GENERATED_BOT:
        raise ValueError("base must be a distinct registered suite/bot selector")
    base_path = (repo / "bots" / base).resolve()
    if not base_path.is_relative_to(repo / "bots") or not (base_path / "src/main.cpp").is_file():
        raise ValueError("base source is unavailable or outside the repository")
    target = (repo / "bots" / GENERATED_BOT).resolve()
    if not target.is_relative_to(repo / "bots/contenders") or target.name != "_optimizer-program":
        raise ValueError("generated bot destination escaped its owned path")
    tree = validate_tree(candidate.get("tree", {"factor": 1.0})) if kind == "program" else None
    policy = validate_policy(candidate) if kind == "policy" else None
    neutral = kind == "program" and tree == {"factor": 1.0}

    files = sorted(path for path in (base_path / "src").rglob("*") if path.is_file() and ".cambc" not in path.parts)
    if not files or sum(path.stat().st_size for path in files) > 32 * 1024 * 1024:
        raise ValueError("unsupported empty or excessively large controller source closure")
    relative = [path.relative_to(base_path).as_posix() for path in files]
    if any(path.suffix not in {".h", ".hpp", ".cpp", ".c", ".cc", ".inc"} for path in files):
        raise ValueError("materializer accepts self-contained C++ source controllers")
    for path in files:
        for include in re.findall(r'^\s*#\s*include\s*"([^"\r\n]+)"', path.read_text(encoding="utf-8-sig"), re.MULTILINE):
            resolved = (path.parent / include).resolve()
            if not resolved.is_relative_to(base_path / "src") and not resolved.is_relative_to(repo / "sdk"):
                raise ValueError(f"materializer base has unsupported external source include {include}")
    marker = target / ".optimizer-materializer.json"
    if marker.is_file():
        previous = json.loads(marker.read_text(encoding="utf-8"))
        if previous.get("base_files") != relative:
            raise ValueError("base source-file set changed; use a fresh worker checkout rather than deleting retired content")
    base_hash = hashlib.sha256()
    for path, rel in zip(files, relative):
        data = path.read_bytes()
        base_hash.update(rel.encode() + b"\0" + data + b"\0")
        _write(target / rel, data.decode("utf-8-sig"))
    brain_path = target / "src/brain.cpp"
    brain = brain_path.read_text(encoding="utf-8")
    if not neutral:
        inclusion = '#include "world.h"\n'
        start, end = "float Brain::score_group(", "\nint Brain::choose_target("
        if brain.count(inclusion) != 1 or start not in brain or end not in brain:
            raise ValueError("controller does not expose the compatible frozen planner interface")
        lo, hi = brain.index(start), brain.index(end, brain.index(start))
        score = brain[lo:hi]
        if score.count("    return rate;\n}") != 1:
            raise ValueError("unrecognized score_group return; no guessed patch applied")
        score = score.replace("    return rate;\n}",
                              "    return rate * optimizer::factor(ctx, info, belief, travel);\n}")
        brain = brain[:lo] + score + brain[hi:]
        bound = "bound[g] = reward / static_cast<float>(std::max(1, travel + capture));"
        if brain.count(bound) != 1:
            raise ValueError("unrecognized planner upper bound; weighted program would invalidate pruning")
        brain = brain.replace(bound, "bound[g] = reward / static_cast<float>(std::max(1, travel + capture)) * optimizer::upper_bound(ctx, info, Knowledge::get().groups[g], travel);")
        brain = brain.replace(inclusion, inclusion + '#include "optimizer_runtime.h"\n')
        _write(brain_path, brain)

    runtime_path = Path(__file__).with_name("program_runtime.h")
    _write(target / "src/optimizer_runtime.h", runtime_path.read_text(encoding="utf-8"))
    header = _config_header(kind, tree, policy)
    _write(target / "src/optimizer_config.h", header)
    normalized = {"kind": kind, "base": base, "tree": tree, "policy": policy}
    digest = hashlib.sha256(json.dumps(normalized, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    metadata = {"schema": SCHEMA, "kind": kind, "base": base, "base_files": relative,
                "base_source_sha256": base_hash.hexdigest(), "candidate_sha256": digest,
                "tree_feature_schema": TREE_FEATURE_SCHEMA, "actor_feature_schema": ACTOR_FEATURE_SCHEMA,
                "feature_names": FEATURE_NAMES, "action_names": ACTION_NAMES,
                "tree_home_distance": "candidate group center to own spawn, Chebyshev/100, clipped",
                "actor_home_distance": "current self to own spawn, Chebyshev/100, clipped",
                "actor_zero_features": [6, 7, 8, 9, 10],
                "unknown_group_hp": "remembered legal observation; unknown/full=1; not fresh hidden state",
                "identity_program_unpatched": neutral,
                "pruning_upper_bound": "exact_current_preference_factor" if kind == "policy" else _max_factor(tree),
                "actor_sampling_fuel_floor": 65000,
                "tree_limits": {"depth": MAX_DEPTH, "nodes": MAX_NODES, "factor": [MIN_FACTOR, MAX_FACTOR]},
                "policy": policy,
                "emission_contract": {"trace": bool(policy and policy["trace"]),
                    "one_decision_per_actor_scoring_turn": True, "minimum_fuel": 65000,
                    "cap_per_actor": policy["max_decisions"] if policy else 0,
                    "no_unlogged_sampled_decisions": True,
                    "outside_eligibility": "neutral/no actor decision",
                    "untraced_eligibility_gate_identical": True,
                    "trace_can_change_later_fuel_dependent_control": True},
                "scope": "positive camp-job ranking only; inherited legality, combat, channels and safety unchanged"}
    _write(marker, json.dumps(metadata, indent=2) + "\n")
    return {"bot": GENERATED_BOT, "base": base, "metadata": metadata,
            "rollout_parser": "program_materializer.parse_rollouts"}


def parse_rollouts(stdout: str) -> list[dict]:
    """Parse complete native macro choices; malformed/missing choices are fatal.

    Completeness means every *sampled actor decision* is emitted immediately. Fuel
    gates and the declared cap use neutral fallback without sampling. It does not
    mean every primitive game action is represented or a long game is truncated.
    """
    if not isinstance(stdout, str) or len(stdout.encode("utf-8")) > 32 * 1024 * 1024:
        raise ValueError("invalid or excessively large native stdout")
    records: list[dict] = []
    previous: dict[int, tuple[int, int]] = {}
    for line in stdout.splitlines():
        if "OPT_TRAJ" not in line:
            continue
        if "OPT_TRAJ " not in line:
            raise ValueError("malformed OPT_TRAJ prefix")
        record = json.loads(line.split("OPT_TRAJ ", 1)[1])
        if not isinstance(record, dict) or record.get("schema") != TRACE_SCHEMA or record.get("feature_schema") != ACTOR_FEATURE_SCHEMA:
            raise ValueError("unsupported native actor observation schema")
        actor = _integer(record.get("actor"), "actor", 0, 0xFFFFFFFF)
        decision = _integer(record.get("decision"), "decision", 1, 256)
        round_number = _integer(record.get("round"), "round", 0, 1000000)
        action = _integer(record.get("action"), "action", 0, 4)
        if "encoding" in record:
            if record["encoding"] != "float32-bits" or any(key in record for key in ["features", "probabilities", "log_prob"]):
                raise ValueError("unsupported or ambiguous native actor encoding")
            def decode_bits(key: str, count: int) -> list[float]:
                values = record.get(key)
                if not isinstance(values, list) or len(values) != count:
                    raise ValueError(f"native {key} must have length {count}")
                return [struct.unpack("<f", struct.pack("<I", _integer(value, key, 0, 0xFFFFFFFF)))[0]
                        for value in values]
            record["features"] = decode_bits("feature_bits", 12)
            record["probabilities"] = decode_bits("probability_bits", 5)
            probability = record["probabilities"][action]
            if not math.isfinite(probability) or probability <= 0:
                raise ValueError("chosen native probability is not positive and finite")
            record["log_prob"] = math.log(probability)
        features, probabilities, mask = record.get("features"), record.get("probabilities"), record.get("legal_mask")
        if not isinstance(features, list) or len(features) != 12:
            raise ValueError("native actor features must have length 12")
        for value in features:
            _number(value, "native feature", 0, 1)
        if any(features[index] != 0 for index in [6, 7, 8, 9, 10]):
            raise ValueError("native actor leaked job-dependent features")
        if not isinstance(probabilities, list) or len(probabilities) != 5:
            raise ValueError("native actor probabilities must have length 5")
        for value in probabilities:
            _number(value, "native probability", 0, 1)
        if abs(sum(probabilities) - 1.0) > 0.0001 or probabilities[action] <= 0:
            raise ValueError("native actor probabilities do not normalize")
        if mask != [True] * 5:
            raise ValueError("native actor legal mask is incompatible")
        log_prob = _number(record.get("log_prob"), "log_prob", -100, 0.000001)
        if abs(log_prob - math.log(probabilities[action])) > 0.002:
            raise ValueError("native log probability differs from chosen probability")
        last_decision, last_round = previous.get(actor, (0, -1))
        if decision != last_decision + 1 or round_number <= last_round:
            raise ValueError("missing, duplicated or unordered native actor decisions")
        previous[actor] = (decision, round_number)
        records.append(record)
    return records
