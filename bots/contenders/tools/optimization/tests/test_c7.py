"""C7 deterministic search/resume and health guards. Fixture results are not game evidence."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    scratch = root / ".cache/contenders/optimization/c7"
    scratch.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).with_name("c7_fixture_evaluator.py")
    checks = 0

    def check(value, message):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError(message)

    with tempfile.TemporaryDirectory(prefix="checks-", dir=scratch) as temporary:
        base = Path(temporary)
        config = {"controller": "fixture/controller", "baseline": "fixture/base",
                  "opponents": ["fixture/rock", "fixture/paper", "fixture/scissors"],
                  "families": [{"id": "training", "maps": ["fixture-map"]}],
                  "validation_opponents": ["fixture/rock", "fixture/paper", "fixture/scissors"],
                  "validation_families": [{"id": "validation", "maps": ["other-map"]}],
                  "sides": ["A", "B"], "admit_generated_opponents": False,
                  "matrix_steps": 5000, "validation_every": 1,
                  "seeds": [{"draft_mode": 5}],
                  "search": {"space": [{"name": "draft_mode", "type": "categorical", "default": 0, "values": [0, 1, 2, 3, 5]}]}}
        path = base / "config.json"
        path.write_text(json.dumps(config), encoding="utf-8")

        def launch(output, iterations, resume=False, input_path=path, expected=0):
            command = [str(args.exe.resolve()), "c7", "--repo", str(root), "--input", str(input_path), "--out", str(output),
                       "--iterations", str(iterations), "--seed", "17", "--evaluator", sys.executable,
                       "--evaluator-arg=-B", "--evaluator-arg=-I", "--evaluator-arg", str(fixture)]
            if resume:
                command.append("--resume")
            completed = subprocess.run(command, cwd=root, text=True, capture_output=True, timeout=120)
            check(completed.returncode == expected, f"C7 exit {completed.returncode}, expected {expected}: {completed.stderr[-2000:]}")
            return json.loads((output / "checkpoint.json").read_text()), json.loads((output / "report.json").read_text())

        uninterrupted, report = launch(base / "full", 60)
        first, _ = launch(base / "split", 7)
        resumed, report_resumed = launch(base / "split", 60, True)

        def canonical(value):
            if isinstance(value, dict):
                return {key: canonical(item) for key, item in value.items() if key not in {"response_path"}}
            if isinstance(value, list):
                return [canonical(item) for item in value]
            return value

        check(canonical(uninterrupted["state"]) == canonical(resumed["state"]), "split native-protocol search state must resume exactly")
        check(uninterrupted["rng"] == resumed["rng"], "resumed RNG must match uninterrupted search")
        check(first["iteration"] == 7, "search cap is cumulative native batches")
        check(report["mixture"] is not None, "native-protocol campaign must return useful mixture")
        check(report["best_pure_empirical_worst_family_payoff"] >= 0.19, "league should discover robust safe candidate")
        check(all(row["knobs"]["draft_mode"] != 5 for row in report["mixture"]["policies"]), "failed-health policy must never enter mixture")
        check(report["validation_used_for_selection"] is False, "validation cannot select policy")
        check((base / "full/best-overrides.json").is_file(), "export best overrides")
        check((base / "full/best-config.hpp").is_file(), "export compilable C++ policy artifact")
        check((base / "full/best-native-request.json").is_file(), "export exact replay request")

        generated = dict(config)
        generated["admit_generated_opponents"] = True
        generated_path = base / "generated.json"
        generated_path.write_text(json.dumps(generated), encoding="utf-8")
        generated_state, generated_report = launch(base / "generated", 60, input_path=generated_path)
        check(len(generated_state["state"]["columns"]) > 3, "successful candidate becomes an immutable opponent snapshot")
        check(all(column["opponent"].startswith("fixture/") for column in generated_report["mixture"]["columns"]), "expanded payoff matrix remains explicit")
        replay_request = json.loads((base / "generated/best-native-request.json").read_text())
        recipes = {recipe["key"]: recipe for recipe in replay_request["population_configs"]}
        generated_opponents = {column["opponent"] for column in generated_report["mixture"]["columns"]
                               if column["opponent"].startswith("fixture/policy@")}
        check(generated_opponents <= recipes.keys(), "export contains reconstruction recipes for every generated opponent")
        check(all(recipe["identity"]["key"] == key and recipe["identity"].get("source_sha256")
                  and "@" in recipe["controller"] for key, recipe in recipes.items()), "recipes freeze original controller, source and module identities")
        check(all(row.get("candidate_identity") and row.get("candidate_controller")
                  for row in generated_state["state"]["rows"]), "checkpoint retains exact opponent reconstruction provenance")

        bad = dict(config)
        bad["opponents"] = ["fixture/unhealthy"]
        bad.pop("validation_opponents")
        bad.pop("validation_families")
        bad_path = base / "bad-health.json"
        bad_path.write_text(json.dumps(bad), encoding="utf-8")
        _, bad_report = launch(base / "bad-health", 10, input_path=bad_path, expected=2)
        check(bad_report["mixture"] is None, "opponent faults must not enter a payoff matrix")
        check(bad_report["status"] == "blocked_opponent_or_baseline_health", "bad baseline/opponent stops pointless search")

        overlap = dict(config)
        overlap["validation_families"] = overlap["families"]
        overlap_path = base / "overlap.json"
        overlap_path.write_text(json.dumps(overlap), encoding="utf-8")
        command = [str(args.exe.resolve()), "c7", "--repo", str(root), "--input", str(overlap_path), "--out", str(base / "overlap"), "--iterations", "1"]
        completed = subprocess.run(command, text=True, capture_output=True, timeout=20)
        check(completed.returncode != 0 and "overlap" in completed.stderr, "train/validation leakage rejected before any game")
    print(f"C7 integration: {checks} checks passed (synthetic evaluator; no native games)")


if __name__ == "__main__":
    main()
