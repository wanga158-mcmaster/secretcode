"""Deterministic orchestration fixture, never a Battlecode fitness evaluator."""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo")
    args = parser.parse_args()
    request = json.loads(args.request.read_text(encoding="utf-8"))
    context_cases = request.get("context_cases", request["cases"])
    version = Path(args.repo) / "fixture-context-version"
    context = hashlib.sha256((json.dumps(context_cases, sort_keys=True)
                             + (version.read_text() if version.exists() else "v1")).encode()).hexdigest()
    if request["kind"] == "identity":
        response = {"context_identity": context}
    else:
        root = next(parent for parent in args.response.parents if (parent / "checkpoint.json").is_file())
        marker = root / "fixture_failed_once"
        if request.get("evaluation", {}).get("fixture_fail_once") and not marker.exists():
            marker.write_text("injected evaluator interruption", encoding="utf-8")
            return 29
        def configuration(case):
            candidate = request["candidate"]
            if "knobs" in candidate:
                return candidate["knobs"]
            node = candidate["config_policy"]["tree"]
            # Explicit synthetic fixture context, never game-world measurements.
            features = {"width": 32 if case["map"].endswith("a") else 64,
                        "enemy_has_ryth": 0 if case["side"] == "A" else 1}
            while "knobs" not in node:
                node = node["left" if features[node["feature"]] <= node["threshold"] else "right"]
            return node["knobs"]
        healthy = True
        rows = []
        for case in request["cases"]:
            knobs = configuration(case)
            x, y = knobs.get("x", 0), knobs.get("y", 0)
            interaction = (x == 3 and y == 2) or (x == 1 and y == 4)
            healthy = healthy and x != 7
            payoff = 1 if interaction or x == 7 else -1 if x + y < 3 else 0
            rows.append({**case, "complete":True, "payoff": payoff, "baseline_payoff": 0, "margin": payoff * 100,
                         "baseline_margin": 0, "health": {"clean": healthy}, "opponent_health": {"clean": True}})
        objective = sum(row["payoff"] for row in rows) / len(rows)
        body = json.dumps(request["candidate"], sort_keys=True).encode()
        artifact_hash = hashlib.sha256(body).hexdigest()
        artifact = root / "fixture-artifacts" / artifact_hash / "native.zip"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_bytes(body)
        response = {"schema": "contenders-eval/v1", "context_identity": context,
                    "objective": objective, "paired_objective": objective, "healthy": healthy,
                    "subject_healthy": healthy, "opponent_healthy": True, "cases": rows,
                    "artifacts":[{"path":str(artifact),"sha256":artifact_hash,"size":len(body),"role":"fixture_only"}],
                    "scope": "deterministic C2 orchestration fixture, no native game"}
    args.response.parent.mkdir(parents=True, exist_ok=True)
    args.response.write_text(json.dumps(response, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
