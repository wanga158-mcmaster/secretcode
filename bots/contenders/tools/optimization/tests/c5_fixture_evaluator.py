"""Deterministic evaluator protocol fixture, never native game evidence."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random

p = argparse.ArgumentParser()
p.add_argument("--request", type=Path, required=True)
p.add_argument("--response", type=Path, required=True)
p.add_argument("--repo", type=Path)
a = p.parse_args()
r = json.loads(a.request.read_text(encoding="utf-8"))
identity = "c5-diagnostic-evaluator/v1"
if r["kind"] == "identity":
    result = {"environment_identity": identity}
else:
    policy = r["candidate"]["policy"]
    weights = policy["weights"]
    rng = random.Random(policy["seed"])
    episodes = []
    for index, case in enumerate(r["cases"]):
        observations = []
        correct = 0
        for decision in range(1, 33):
            features = [0.0] * 12
            features[0] = 0.25 if index % 2 == 0 else 0.75
            features[1] = 0.8
            features[11] = 0.4
            logits = [max(-20, min(20, row[12] + sum(x*w for x, w in zip(features, row)))) for row in weights]
            exps = [math.exp(z-max(logits)) for z in logits]
            probabilities = [v/sum(exps) for v in exps]
            coin = rng.random()
            action = 4
            for j, v in enumerate(probabilities):
                coin -= v
                if coin <= 0:
                    action = j
                    break
            correct += action == 2
            observations.append({"actor": 0, "round": decision, "decision": decision,
                                 "features": features, "action": action, "probabilities": probabilities,
                                 "legal_mask": [True]*5})
        payoff = (2*correct/len(observations))-1
        if not r.get("collect_rollouts", True) and r.get("diagnostic_constant_validation", False):
            payoff = 0
        episodes.append({"trajectory_id": f"{case['map']}/{case['side']}/{policy['seed']}",
                         "family": case.get("family", case["map"]), "source_key": "diagnostic-policy",
                         "map": case["map"], "side": case["side"], "opponent": case["opponent"],
                         "engine": "diagnostic-fixture", "feature_schema": "contenders-actor-context/v1",
                         "subject_healthy": True, "opponent_healthy": True, "complete": True,
                         "logging_complete": True, "payoff": payoff, "baseline_payoff": 0,
                         "baseline_trajectory_id": f"diagnostic-baseline/{case['map']}/{case['side']}",
                         "baseline_source_key": "diagnostic/reference", "baseline_healthy": True,
                         "baseline_provenance": {"map_sha256": hashlib.sha256(case['map'].encode()).hexdigest(),
                                                 "side": case['side'], "opponent_key": case['opponent'],
                                                 "engine": "diagnostic-fixture", "action_independent": True},
                         "observations": observations if r.get("collect_rollouts", True) else []})
        if r.get("diagnostic_omit_paired_baseline", False):
            episodes[-1].pop("baseline_payoff")
            episodes[-1].pop("baseline_trajectory_id")
    key = hashlib.sha256(json.dumps(policy, sort_keys=True).encode()).hexdigest()[:12]
    objective = sum(x["payoff"] for x in episodes)/len(episodes)
    result = {"environment_identity": identity, "healthy": True,
              "objective": objective, "paired_objective": objective,
              "candidate_identity": {"key": "diagnostic/policy@"+key}, "episodes": episodes}
a.response.parent.mkdir(parents=True, exist_ok=True)
a.response.write_text(json.dumps(result), encoding="utf-8")
