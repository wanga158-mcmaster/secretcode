"""Synthetic deterministic evaluator only for C1 recovery tests; not a game evaluator."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def factor(tree, features):
    while "factor" not in tree:
        tree = tree["left"] if features[tree["feature"]] <= tree["threshold"] else tree["right"]
    return tree["factor"]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--request", type=Path, required=True)
    p.add_argument("--response", type=Path, required=True)
    p.add_argument("--repo")
    p.add_argument("--fail-once", type=Path)
    p.add_argument("--identity-file", type=Path)
    p.add_argument("--fault",choices=["missing-case","duplicate-case","foreign-case","summary-fitness","raw-health","map-alias"])
    args = p.parse_args()
    request = json.loads(args.request.read_text(encoding="utf8"))
    identity = "synthetic-c1-fixture-v1:" + hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if args.identity_file:
        identity += ":" + args.identity_file.read_text(encoding="utf8")
    cases=[]
    for case in request["cases"]:
        map_hash=hashlib.sha256(("same-map" if args.fault=="map-alias" else case["map"]).encode()).hexdigest()
        cases.append(dict(map=case["map"],side=case["side"],map_sha256=map_hash,opponent=dict(key=case["opponent"])))
    context=dict(controller=dict(key="frozen-controller@abc"),baseline=dict(key="frozen-baseline@def"),cases=cases)
    if request["kind"] == "identity":
        result = {"environment_identity": identity,"identity":context,"objective": 0.0, "healthy": True}
    else:
        if args.fail_once and not args.fail_once.exists():
            args.fail_once.write_text("failure injected", encoding="utf8")
            raise RuntimeError("intentional evaluator interruption after pending checkpoint")
        tree = request["candidate"]["tree"]
        errors = []
        for hp in (0.125, 0.375, 0.625, 0.875):
            features = [0.25] * 12
            features[1] = hp
            target = 2.0 if hp <= 0.5 else 0.5
            errors.append((math.log2(factor(tree, features)) - math.log2(target)) ** 2)
        score = -math.tanh(sum(errors) / len(errors))
        fixture_source = {"role": "synthetic_fixture_source", "path": str(Path(__file__).resolve()),
                          "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        rows=[]
        for case in cases:
            rows.append(dict(map=case["map"],side=case["side"],opponent=case["opponent"]["key"],complete=True,
                payoff=score,baseline_payoff=-0.25,margin=score*100,rounds=100,
                health=[],opponent_health=[],baseline_health=[],baseline_opponent_health=[],
                identity=dict(map_sha256=case["map_sha256"],opponent_key=case["opponent"]["key"],baseline_key=context["baseline"]["key"],subject_key="fixture-candidate")))
        if args.fault=="missing-case":rows=[]
        elif args.fault=="duplicate-case":rows.append(rows[0])
        elif args.fault=="foreign-case":rows[0]["identity"]["map_sha256"]="foreign"
        elif args.fault=="raw-health":rows[0]["baseline_opponent_health"]=["fuel_exhausted"]
        result = {"schema": "synthetic-c1-fixture/v1", "environment_identity": identity,"identity":context,
                  "objective": score+(1 if args.fault=="summary-fitness" else 0), "paired_objective": score + .25, "healthy": True,
                  "selection_eligible": True, "cases": rows,
                  "export": {"test_only": True, "native_game_evidence": False, "fixture": fixture_source},
                  "artifacts": [fixture_source]}
    args.response.write_text(json.dumps(result, sort_keys=True), encoding="utf8")


if __name__ == "__main__":
    main()
