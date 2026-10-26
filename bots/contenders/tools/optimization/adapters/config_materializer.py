"""Bounded contextual constexpr configurations, built as virtual arena closures.

One public-context tree selects an independently compiled controller configuration.
Draft selection is repeated against the revealed draft; gameplay selects once from
public immutable identities and retains that controller for the entire instance.
No working bot, reference source, account settings, or online service is modified.
"""
from __future__ import annotations

import copy
import hashlib
import math
import os
from pathlib import Path
import re

from native_support import EvaluationError, canonical, entrant_identity, exact_entrant, knobs

POLICY_SCHEMA = "contenders-contextual-config/v1"
FEATURE_SCHEMA = "contenders-config-context/v1"
MATERIALIZATION_SCHEMA = "contenders-config-materialization/v1"
MAX_DEPTH, MAX_LEAVES = 3, 4
PICKS = ("ryth", "vesper_vale", "vendetta", "cassi_and_issac", "stellar", "prism",
         "marcella", "willow", "ren", "varg", "tavi", "heart_of_ruin")
MAP_FEATURES = ("width", "height", "area", "group_count", "camp_count", "total_gems",
                "total_gold", "max_group_gems", "max_group_gold", "gem_group_count",
                "mean_group_size", "spawn_distance")
FEATURE_NAMES = MAP_FEATURES + ("team_size", "ally_pick_count", "enemy_pick_count") + tuple(
    f"{side}_has_{pick}" for side in ("ally", "enemy") for pick in PICKS)

# Exact supported frozen entrypoint. Patching an unfamiliar turn loop would risk
# losing legality, exception handling, or yielding, so reject instead of guessing.
FROZEN_MAIN = '''int main()
{
    mb::Brain* brain = nullptr;
    while (true)
    {
        try
        {
            if (cambc::is_draft())
                mb::play_draft();
            else
            {
                if (!brain) brain = new mb::Brain();
                brain->turn();
            }
        }
        catch (const std::exception& error)
        {
            MB_LOG("turn error r%u: %s\\n", cambc::is_draft() ? 0u : cambc::round_number(), error.what());
        }
        catch (...)
        {
            MB_LOG("turn error: unknown exception\\n");
        }
        cambc::end_turn();
    }
}
'''


def validate_policy(policy: dict) -> dict:
    """Return a finite declarative policy; arbitrary C++ never enters the tree."""
    if not isinstance(policy, dict) or set(policy) - {"schema", "feature_schema", "tree"}:
        raise EvaluationError("contextual configuration requires schema/feature_schema/tree only")
    if policy.get("schema", POLICY_SCHEMA) != POLICY_SCHEMA:
        raise EvaluationError("unsupported contextual configuration schema")
    if policy.get("feature_schema", FEATURE_SCHEMA) != FEATURE_SCHEMA:
        raise EvaluationError("unsupported contextual configuration feature schema")
    leaves = 0

    def visit(node, depth):
        nonlocal leaves
        if depth > MAX_DEPTH or not isinstance(node, dict):
            raise EvaluationError(f"contextual tree requires objects and depth <= {MAX_DEPTH}")
        if set(node) == {"knobs"}:
            leaves += 1
            if leaves > MAX_LEAVES:
                raise EvaluationError(f"contextual tree requires at most {MAX_LEAVES} leaves")
            knobs(node["knobs"])  # strict numeric/bool/flat-array C++ value grammar
            if not isinstance(node["knobs"], dict):
                raise EvaluationError("contextual leaf knobs must be an object")
            return {"knobs": copy.deepcopy(dict(sorted(node["knobs"].items())))}
        if set(node) != {"feature", "threshold", "left", "right"}:
            raise EvaluationError("contextual node requires feature/threshold/left/right or knobs")
        if node["feature"] not in FEATURE_NAMES:
            raise EvaluationError(f"unsupported public context feature {node['feature']!r}")
        threshold = node["threshold"]
        if isinstance(threshold, bool) or not isinstance(threshold, (int, float)):
            raise EvaluationError("context threshold must be finite numeric")
        threshold = float(threshold)
        if not math.isfinite(threshold) or abs(threshold) > 1e12:
            raise EvaluationError("context threshold must be finite and within +/-1e12")
        left, right = visit(node["left"], depth + 1), visit(node["right"], depth + 1)
        if left == right:
            return left
        return {"feature": node["feature"], "threshold": threshold, "left": left, "right": right}

    result = {"schema": POLICY_SCHEMA, "feature_schema": FEATURE_SCHEMA, "tree": visit(policy.get("tree"), 0)}
    canonical(result)  # additionally rejects non-JSON/nonfinite knob values
    return result


def _leaves(tree: dict) -> list[dict]:
    if "knobs" in tree:
        return [tree["knobs"]]
    return _leaves(tree["left"]) + _leaves(tree["right"])


def _features(tree: dict) -> set[str]:
    return set() if "knobs" in tree else {tree["feature"]} | _features(tree["left"]) | _features(tree["right"])


def select_configuration(policy: dict, features: dict) -> dict:
    """Host reference for threshold/leaf semantics, used by search and tests."""
    node = validate_policy(policy)["tree"]
    while "knobs" not in node:
        value = features.get(node["feature"])
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise EvaluationError("selector needs finite declared public features")
        node = node["left" if value <= node["threshold"] else "right"]
    return node["knobs"]


_CPP_TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b[A-Za-z_]\w*\b', re.S)


def _namespace(text: str, name: str) -> str:
    return _CPP_TOKENS.sub(lambda m: name if m.group() == "mb" else m.group(), text)


def _leaf_main(text: str) -> str:
    text = text.replace("\r\n", "\n")
    marker = "int main()\n"
    if text.count(marker) != 1:
        raise EvaluationError("contextual controller entrypoint is not the recognized frozen loop")
    start = text.index(marker)
    if text[start:] != FROZEN_MAIN:
        raise EvaluationError("contextual controller entrypoint changed; no guessed main rewrite")
    return text[:start] + '''namespace mb {
void optimizer_draft_turn() { play_draft(); }
void optimizer_game_turn()
{
    static Brain* brain = nullptr;
    if (!brain) brain = new Brain();
    brain->turn();
}
} // namespace mb
'''


def _expression(tree: dict, leaf_index: dict[bytes, int]) -> str:
    if "knobs" in tree:
        return str(leaf_index[canonical(tree["knobs"])])
    index = FEATURE_NAMES.index(tree["feature"])
    return f"(x[{index}] <= {float(tree['threshold']).hex()} ? {_expression(tree['left'], leaf_index)} : {_expression(tree['right'], leaf_index)})"


def _dispatcher(tree: dict, leaves: list[dict]) -> str:
    indices = {canonical(leaf): i for i, leaf in enumerate(leaves)}
    expression = _expression(tree, indices)
    declarations = "\n".join(f"namespace optimizer_leaf{i} {{ void optimizer_draft_turn(); void optimizer_game_turn(); }}" for i in range(len(leaves)))
    draft_cases = "\n".join(f"            case {i}: optimizer_leaf{i}::optimizer_draft_turn(); break;" for i in range(len(leaves)))
    game_cases = "\n".join(f"            case {i}: optimizer_leaf{i}::optimizer_game_turn(); break;" for i in range(len(leaves)))
    roster_cases = "\n".join(f"    case cambc::champion_type::{('cassi' if pick == 'cassi_and_issac' else pick)}: return {i};" for i, pick in enumerate(PICKS))
    return f'''// Generated bounded public-context configuration dispatcher.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
{declarations}
namespace optimizer_context {{
using Context = std::array<double, {len(FEATURE_NAMES)}>;
const Context& static_context()
{{
    static const Context value = [] {{
        Context x{{}};
        x[0] = cambc::map::width(); x[1] = cambc::map::height(); x[2] = x[0] * x[1];
        const auto groups = cambc::map::camp_groups();
        x[3] = groups.size();
        for (const auto& group : groups)
        {{
            x[4] += group.camps().size(); x[5] += group.gem_value(); x[6] += group.gold_value();
            x[7] = std::max(x[7], static_cast<double>(group.gem_value()));
            x[8] = std::max(x[8], static_cast<double>(group.gold_value()));
            if (group.gem_value() > 0) x[9] += 1;
        }}
        x[10] = x[3] > 0 ? x[4] / x[3] : 0;
        const auto ally = cambc::map::spawn_region(cambc::team::ally).positions();
        const auto enemy = cambc::map::spawn_region(cambc::team::enemy).positions();
        double ax = 0, ay = 0, ex = 0, ey = 0;
        for (auto p : ally) {{ ax += p.x; ay += p.y; }}
        for (auto p : enemy) {{ ex += p.x; ey += p.y; }}
        if (!ally.empty() && !enemy.empty())
            x[11] = std::max(std::abs(ax / ally.size() - ex / enemy.size()), std::abs(ay / ally.size() - ey / enemy.size()));
        return x;
    }}();
    return value;
}}
int pick_of(cambc::champion_type type)
{{
    switch (type) {{
{roster_cases}
    case cambc::champion_type::issac: return 3;
    default: return -1;
    }}
}}
int select()
{{
    auto x = static_context();
    if (cambc::is_draft())
    {{
        x[12] = cambc::draft::team_size();
        const auto ally = cambc::draft::current_draft(cambc::team::ally);
        const auto enemy = cambc::draft::current_draft(cambc::team::enemy);
        x[13] = ally.size(); x[14] = enemy.size();
        for (auto pick : ally) {{ const auto k = static_cast<int>(pick); if (k >= 0 && k < 12) x[15 + k] = 1; }}
        for (auto pick : enemy) {{ const auto k = static_cast<int>(pick); if (k >= 0 && k < 12) x[27 + k] = 1; }}
    }}
    else
    {{
        // Immutable public identities are legal outside vision. No live position,
        // health or equipment queries and no draft-only host call occurs here.
        for (const auto& champion : cambc::champions(cambc::team::ally))
        {{ const int k = pick_of(champion.champion_name()); if (k >= 0) x[15 + k] = 1; }}
        for (const auto& champion : cambc::champions(cambc::team::enemy))
        {{ const int k = pick_of(champion.champion_name()); if (k >= 0) x[27 + k] = 1; }}
        for (int k = 0; k < 12; ++k) {{ x[13] += x[15 + k]; x[14] += x[27 + k]; }}
        x[12] = x[13];
    }}
    const int leaf = {expression};
    if (cambc::is_draft())
    {{
        static unsigned emitted = 0;
        if (++emitted <= 12)
            std::printf("OPT_CONFIG {{\\\"schema\\\":\\\"contenders-config-choice/v1\\\",\\\"phase\\\":\\\"draft\\\",\\\"leaf\\\":%d}}\\n", leaf);
    }}
    else
    {{
        const auto actor = static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()));
        std::printf("OPT_CONFIG {{\\\"schema\\\":\\\"contenders-config-choice/v1\\\",\\\"phase\\\":\\\"gameplay\\\",\\\"leaf\\\":%d,\\\"actor\\\":%u}}\\n", leaf, actor);
    }}
    return leaf;
}}
}} // namespace optimizer_context
int main()
{{
    int gameplay_leaf = -1;
    unsigned errors = 0;
    while (true)
    {{
        try
        {{
            if (cambc::is_draft())
            {{
                switch (optimizer_context::select())
                {{
{draft_cases}
                }}
            }}
            else
            {{
                if (gameplay_leaf < 0) gameplay_leaf = optimizer_context::select();
                switch (gameplay_leaf)
                {{
{game_cases}
                }}
            }}
        }}
        catch (const std::exception& error)
        {{ if (++errors <= 400) std::printf("turn error: %s\\n", error.what()); }}
        catch (...)
        {{ if (++errors <= 400) std::printf("turn error: unknown exception\\n"); }}
        cambc::end_turn();
    }}
}}
'''


def configuration_sources(core, base, policy: dict) -> tuple[dict[Path, bytes], dict]:
    normalized = validate_policy(policy)
    source = (base.path / "src").resolve()
    files = sorted(p for p in source.rglob("*") if p.is_file())
    if not files or sum(p.stat().st_size for p in files) > 32 * 1024 * 1024:
        raise EvaluationError("contextual controller source closure is empty or too large")
    frozen = {p: p.read_bytes() for p in files}
    if sum(map(len, frozen.values())) > 32 * 1024 * 1024:
        raise EvaluationError("contextual controller source closure is too large")
    for p in files:
        if p.suffix not in core.SOURCE_SUFFIXES:
            raise EvaluationError("contextual controller source closure must contain only C++ sources")
        for include in core.INCLUDE.findall(frozen[p].decode("utf-8")):
            target = (p.parent / include).resolve()
            if target.is_file() and not target.is_relative_to(source) and not target.is_relative_to((core.ROOT / "sdk").resolve()):
                raise EvaluationError("contextual controller requires unsupported external relative includes")
    b = core.Bot("contenders", "_optimizer-config")
    leaf_configs = list({canonical(leaf): leaf for leaf in _leaves(normalized["tree"])}.values())
    source_hash = hashlib.sha256()
    for p in files:
        source_hash.update(p.relative_to(base.path).as_posix().encode() + b"\0" + frozen[p] + b"\0")
    metadata = {"schema": MATERIALIZATION_SCHEMA, "controller_key": base.key,
                "controller_source_sha256": source_hash.hexdigest(), "policy": normalized,
                "feature_schema": FEATURE_SCHEMA, "feature_names": list(FEATURE_NAMES),
                "used_features": sorted(_features(normalized["tree"])), "leaf_configs": leaf_configs,
                "leaves": len(_leaves(normalized["tree"])), "compiled_leaves": len(leaf_configs),
                "limits": {"depth": MAX_DEPTH, "leaves": MAX_LEAVES},
                "selection": "revealed draft each draft turn; public immutable map/roster once on first gameplay turn",
                "public_roster": "SDK champions(team) immutable champion_name; Cassi/Issac collapse to one pick",
                "gameplay_leaf_fixed": True, "collapsed_constant": len(leaf_configs) == 1,
                "selection_trace": {"schema": "contenders-config-choice/v1", "prefix": "OPT_CONFIG ",
                    "max_draft_records": 12, "gameplay_records_per_actor": 1,
                    "constant_policy_untraced": True}}
    if len(leaf_configs) == 1:
        contents = {b.path / "src" / p.relative_to(source): frozen[p] for p in files}
        return core.apply_overrides(b, contents, knobs(leaf_configs[0])), metadata
    contents = {}
    for i, leaf in enumerate(leaf_configs):
        root = b.path / "src" / f"leaf{i}"
        leaf_contents = {root / p.relative_to(source): frozen[p] for p in files}
        leaf_contents = core.apply_overrides(b, leaf_contents, knobs(leaf))
        for p, raw in leaf_contents.items():
            text = raw.decode("utf-8").replace("\r\n", "\n")
            if p == root / "main.cpp":
                text = _leaf_main(text)
            contents[p] = _namespace(text, f"optimizer_leaf{i}").encode("utf-8")
    contents[b.path / "src/main.cpp"] = _dispatcher(normalized["tree"], leaf_configs).encode("utf-8")
    return contents, metadata


def materialize_configuration(core, controller: str, policy: dict):
    """Return (exact arena Entrant, metadata), with no mutable working bot writes."""
    os.environ["CAMBC_OFFLINE"] = "true"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "true"
    base = exact_entrant(core, controller, "contextual-controller")
    base_identity = entrant_identity(core, base)
    contents, metadata = configuration_sources(core, base, policy)
    if metadata["controller_source_sha256"] != base_identity["source_sha256"]:
        raise EvaluationError("contextual controller source changed while capturing its closure")
    metadata["controller_identity"] = base_identity
    # Arena's version includes override metadata as well as resulting source bytes.
    # Preserve ordinary fixed-config identity when the contextual tree is constant.
    overrides = knobs(metadata["leaf_configs"][0]) if metadata["collapsed_constant"] else None
    result = core.prepare(core.Bot("contenders", "_optimizer-config"), overrides=overrides,
                          label="contextual-configuration", contents=contents)
    if not result.ok:
        raise EvaluationError("contextual configuration build failed: " + result.log[-4000:])
    if entrant_identity(core, base) != base_identity:
        raise EvaluationError("contextual controller source or executable changed during candidate build")
    return result, metadata
