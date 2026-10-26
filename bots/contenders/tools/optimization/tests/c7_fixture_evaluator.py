"""Deterministic protocol fixture for C7 control-flow tests; never native evidence."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo")
    args = parser.parse_args()
    request = json.loads(args.request.read_text())
    knobs = request.get("candidate", {}).get("knobs", {})
    action = int(knobs.get("draft_mode", 0))
    matrix = [[0.0, -1.0, 1.0, -0.2], [1.0, 0.0, -1.0, -0.2], [-1.0, 1.0, 0.0, -0.2], [0.2, 0.2, 0.2, 0.0]]
    cases = request["cases"]
    context = {"controller": request.get("controller"), "baseline": request.get("baseline"), "cases": cases}
    if request["kind"] == "identity":
        response = {"environment_identity": digest(context), "schema": "contenders-eval-result/v1", "games_played": 0}
    else:
        rows = []
        candidate = "fixture/policy@" + digest(knobs)[:12]
        for case in cases:
            opponent = case["opponent"]
            if opponent.startswith("fixture/policy@"):
                recipes = {item["key"]: item for item in request.get("population_configs", [])}
                if opponent not in recipes:
                    raise RuntimeError("generated opponent lacks frozen reconstruction recipe")
                recipe = recipes[opponent]
                if recipe["identity"]["key"] != opponent or recipe["controller"] != "fixture/controller@000000000000":
                    raise RuntimeError("generated opponent recipe has wrong immutable binding")
                if opponent != "fixture/policy@" + digest(recipe["knobs"])[:12]:
                    raise RuntimeError("generated opponent recipe does not reconstruct exact bytes")
            column = {"fixture/rock": 0, "fixture/paper": 1, "fixture/scissors": 2}.get(opponent.split("@", 1)[0], 0)
            for policy in range(6):
                if opponent == "fixture/policy@" + digest({"draft_mode": policy})[:12]:
                    column = policy % 4
            payoff = matrix[action % len(matrix)][column]
            subject_health = ["injected_subject_fault"] if action == 5 else []
            opponent_health = ["injected_opponent_fault"] if opponent.split("@", 1)[0] == "fixture/unhealthy" else []
            if request.get("phase")=="validation" and request.get("evaluation",{}).get("fixture_validation_infrastructure_failure"):
                opponent_health=["injected_validation_opponent_fault"]
            rows.append({"map": case["map"], "side": case["side"], "opponent": opponent + ("@000000000000" if "@" not in opponent else ""),
                         "complete": True,
                         "payoff": payoff, "baseline_payoff": matrix[0][column], "health": subject_health,
                         "opponent_health": opponent_health, "baseline_health": [], "baseline_opponent_health": [],
                         "identity": {"subject_key": candidate, "baseline_key": "fixture/base@000000000000",
                                      "opponent_key": opponent + ("@000000000000" if "@" not in opponent else ""),
                                      "map_sha256": digest(case["map"]), "engine": "fixture-only", "toolchain": "fixture-only"}})
        subject = all(not row["health"] for row in rows)
        opponent = all(not row["opponent_health"] for row in rows)
        response = {"schema": "contenders-eval-result/v1", "environment_identity": digest(context),
                    "candidate_identity": {"key": candidate, "source_sha256": digest(knobs),
                                           "modules": {"raw": {"sha256": digest(knobs)}, "injected": {"sha256": digest(knobs)}}},
                    "identity": {"controller": {"key": "fixture/controller@000000000000"}},
                    "objective": sum(row["payoff"] for row in rows) / len(rows), "subject_healthy": subject,
                    "opponent_healthy": opponent, "baseline_healthy": True, "healthy": subject and opponent,
                    "selection_eligible": subject and opponent, "cases": rows, "games_played": 0, "artifacts": []}
    args.response.parent.mkdir(parents=True, exist_ok=True)
    args.response.write_text(json.dumps(response), encoding="utf-8")


if __name__ == "__main__":
    main()
