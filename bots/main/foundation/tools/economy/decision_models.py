"""Pure discriminators for productive-decision models; never launches a game.

This records conditional algebra and counterexamples, not caller or engine calibration.
Use the runtime printed by arena env. Output must be fresh.
"""
from __future__ import annotations

import argparse
from fractions import Fraction as Q
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scalar_labour(*, n, travel, wait=0, clear=0, damage=0, max_hp=1000,
                  home=0, heal_first=False, capture=6):
    """The audited scalar algebra, in exact rationals rather than native floats."""
    n, travel = Q(n), Q(travel)
    labour = n * (travel + Q(wait) + Q(clear)) + Q(capture)
    labour += Q(damage) / (Q(4, 5) * Q(max_hp)) * (7 + Q(home))
    if heal_first:
        labour += n * (6 + Q(home) - travel / 2)
    return labour


def number(value):
    return {"exact": str(value), "decimal": float(value)}


def scalar_cases():
    # Cleared reward: original health is no remaining service-time floor.
    reward_rate = Q(120, 2 + 6)
    full_hp_bound = Q(120, 2 + 6 + 9000 // 100)
    assert full_hp_bound < reward_rate
    # A missing ownership multiplier is an underbound even with zero travel.
    owned_rate, unowned_bound = Q(115, 6), Q(100, 6)
    assert unowned_bound < owned_rate
    # The existing heal-first discount can make full travel an invalid denominator floor.
    heal_labour = scalar_labour(n=1, travel=100, clear=5, damage=2000, home=1, heal_first=True)
    proposed_floor = Q(6 + 100 // 2)
    full_travel_floor = Q(6 + 100)
    assert proposed_floor <= heal_labour < full_travel_floor
    samples = 0
    for n in range(1, 7):
        for travel in (0, 1, 2, 20, 100, 300):
            for home in (0, 1, 300):
                for wait in (0, 20):
                    for clear in (0, 1, 100):
                        for damage in (0, 2000):
                            for heal_first in (False, True):
                                labour = scalar_labour(n=n, travel=travel, wait=wait, clear=clear,
                                                       damage=damage, home=home, heal_first=heal_first)
                                assert labour >= 6 + Q(travel, 2)
                                samples += 1
    candidates = [{"upper": 100, "actual": 1} for _ in range(10)] + [{"upper": 99, "actual": 99}]
    assert max(c["actual"] for c in candidates[:10]) < max(c["actual"] for c in candidates)
    return {
        "proof": "Without heal: L>=n*t+C. With heal: L=n*(t/2+w+clear+6+home)+C+damage*trip/(0.8*maxhp). Thus L>=C+t/2 for n>=1 and all stated nonnegative terms.",
        "domain": ["t>=0,home>=0,wait>=0,wait_weight>=0,clear>=0,damage>=0,trip>=0,maxhp>0,C>0,n>=1",
                   "Value is nonnegative and includes every actual score multiplier; total call multiplier at most1.3.",
                   "Audited defaults: capture6,wait_weight1,trip7+home; Field::get returns nonnegative distance or positive sentinel.",
                   "Exact rational algebra is not a bit-level proof of all native float inputs or a model calibration."],
        "safe_model_upper_bound": "1.3*current_value/(C+max(0,t)/2); use the SAME t and value conventions as the scored candidate.",
        "synthetic_cases": {
            "cleared_reward_full_hp_floor": {"actual_model_rate": number(reward_rate), "old_full_hp_bound": number(full_hp_bound)},
            "missing_owner_factor": {"actual_model_rate": number(owned_rate), "bound_without_factor": number(unowned_bound)},
            "negative_heal_discount": {"labour": number(heal_labour), "proposed_floor": number(proposed_floor),
                                       "invalid_full_travel_floor": number(full_travel_floor)},
            "fixed_top10_not_global_argmax": {"top10_best": 1, "eleventh_actual": 99,
                                             "all_individual_bounds_valid": True},
        },
        "rational_domain_samples": samples,
        "limits": ["Synthetic contexts, not observed native game states.",
                   "An admissible per-candidate bound does not make a fixed shortlist exact.",
                   "A fuel cutoff may still make search incomplete; report it rather than call the chosen score globally optimal.",
                   "Current model rates are not guaranteed realized economic throughput."],
    }


def route_cases():
    p, roots = 0, (5, 6, 7)
    rows = []
    for goal in (12, -12):
        via = min(abs(p - a) + abs(a - goal) for a in roots)
        direct = abs(p - goal)
        lo = max(0, min(abs(p - a) for a in roots) + min(abs(a - goal) for a in roots) - direct)
        hi = max(0, min(abs(p - a) for a in roots) + max(abs(a - goal) for a in roots) - direct)
        delta = via - direct
        assert lo <= delta <= hi
        rows.append({"position": p, "tribute_roots": list(roots), "next_work": goal,
                     "distance_to_roots": 5, "minimum_extra_travel": delta,
                     "cheap_lower": lo, "cheap_upper": hi})
    assert [row["minimum_extra_travel"] for row in rows] == [0, 10]
    # Tree: P--...10edges...--T, with one tribute-root leaf one edge off each endpoint.
    # Choosing different roots for independent leg minima invents a teleport between leaves.
    independent_via, actual_via, direct = 2, 12, 10
    assert independent_via < direct < actual_via
    return {
        "definition": "delta=min_a[d(P,a)+d(a,T)]-d(P,T), using one legal tribute visit root a and a consistent finite travel graph/goal convention.",
        "cheap_band": "max(0,DA(P)+min_a DT(a)-DT(P)) <= delta <= max(0,DA(P)+max_a DT(a)-DT(P)); requires exact distances on the same fixed graph and finite relevant roots.",
        "admission_direction": "A low lower bound cannot certify a cheap detour. Use an upper bound or a witnessed consistent-root path for that claim.",
        "corridor_geometry": "Only y=0 is walkable; P=(0,0), altar=(6,1), submit range1 -> tribute roots x5,6,7. Goals are work-approach tiles, not live occupied camp centers.",
        "corridor_cases": rows,
        "independent_root_tree_case": {"direct": direct, "independently_minimized_via": independent_via,
                                      "actual_min_via": actual_via, "actual_extra_travel": actual_via - direct},
        "source_audit": ["Compound altar roots prefer noncamp tribute tiles; gated roots use camp tiles only when no free root exists.",
                         "Its altar field opens altar roots; group field opens target-group members: these are different obstacle graphs.",
                         "Cold nearest_altar uses Chebyshev distance to altar center; warm field distance is to legal tribute roots.",
                         "A camp-tile starting position may be blocked in an altar field; unavailable distance needs an explicit departure/unknown policy."],
        "limits": ["Pure travel geometry, not a native route or bank result.",
                   "Add channel, lock, recovery, safety, income-on-bank and deadline effects separately.",
                   "The band is not certified by reusing fields with mismatched graphs or goal/range projections.",
                   "Static geometry does not guarantee freedom from transient units or live gated camps."],
    }


def arrival_cases():
    rows = [{"ordinary_steps_to_attack_set": d, "melee_first_owner_index": max(0, d - 1),
             "ranged_first_owner_index": d} for d in (0, 1, 2, 4)]
    assert rows[1]["melee_first_owner_index"] == 0 and rows[1]["ranged_first_owner_index"] == 1
    # Actor-event order matters even when both advertised arrivals are in one round.
    death = (4, 1)
    assert (4, 0) < death < (4, 2)
    return {
        "conditional_first_basic": rows,
        "domain": ["Fixed target; D ordinary legal steps to a usable basic-attack tile; current owner opportunity is index0.",
                   "Current full movement/attack permission, no channels/stuns/abilities, one ordinary step per owner opportunity.",
                   "Melee can move+basic, ranged ordinary movement consumes attack; fresh allowance checks still required."],
        "same_round_actor_example": {"death_event": list(death), "early_support": [4, 0], "late_support": [4, 2]},
        "party_counterexample": {"solo_clear_round": 10, "helper_first_possible_hit_round": 12,
                                 "both_present_model_clear_round": 5, "actual_helper_contribution_before_solo_clear": 0,
                                 "reason": "An in-place two-body clear-time saving is not a useful-arrival witness; diversion also sacrifices the helper's alternative work."},
        "limits": ["No future target motion or actual caller/native hit is established.",
                   "Reject only the scoped no-ability plan with this ETA; dashes, resets and spells can create different paths.",
                   "Unseen allied state/vision/readiness is unknown; a leader's focus claim is not shared vision or guaranteed ally damage.",
                   "Compare full actor-event order to target-death horizon; round numbers alone can credit damage after death.",
                   "Arrival before death is necessary for damage, not sufficient for positive economic opportunity cost or safety."],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit("output exists; use a fresh path")
    paths = [
        "bots/contenders/timetable/src/bot.cpp", "bots/contenders/timetable/src/model.h",
        "bots/contenders/timetable/src/world.h", "bots/contenders/timetable/src/config.h",
        "bots/contenders/compound/src/econ.cpp", "bots/contenders/compound/src/world.cpp",
        "sdk/cpp/cambc.h", "docs/reference/GameRules.md", "docs/reference/current-version.json",
        "bots/main/foundation/mechanics.md", "bots/main/foundation/micro.md",
        "bots/main/foundation/tools/economy/decision_models.py",
    ]
    data = {"purpose": "conditional decision-model algebra and discriminating counterexamples",
            "source_pins": {p: sha(ROOT / p) for p in paths},
            "bundle": {"cli_sdk": "3.0.5", "engine": "1.0.3"},
            "native_games": 0, "observer_runs": 0, "actual_caller_probes": 0,
            "scalar_bounds": scalar_cases(), "bank_route": route_cases(), "useful_arrival": arrival_cases()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"out": str(args.out), "rational_samples": data["scalar_bounds"]["rational_domain_samples"],
                      "model_counterexamples": 7, "native_games": 0, "actual_caller_probes": 0}))


if __name__ == "__main__":
    main()
