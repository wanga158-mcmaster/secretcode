"""End-to-end C11 oracle determinism, refusal, provenance and unsupported-case tests."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    out = args.out or repo / ".cache/contenders/optimization/c11/native-tests"
    out.mkdir(parents=True, exist_ok=True)
    adapter = repo / "bots/contenders/tools/optimization/adapters/native_oracle.py"
    case = {"id": "shield-actual", "actor_hp": 500, "target_hp": 300, "target_shield": 25,
            "actions": [{"type": "basic"}]}
    request = out / "request.json"
    response = out / "response.json"
    reset_case = {"id": "reset-available-refused", "actor_hp": 500, "target_hp": 300,
                  "target_position": [31, 18],
                  "actions": [{"type": "basic"}, {"type": "reset"}]}
    unsupported_case = dict(case, id="arbitrary-cooldown-refused", cooldowns=[0, 0])
    request.write_text(json.dumps({"kind": "native_transition_batch", "cases": [case, case, reset_case, unsupported_case]}), encoding="utf-8")
    command = [sys.executable, "-B", "-I", "-X", "utf8", str(adapter), "--request", str(request),
               "--response", str(response), "--repo", str(repo)]
    subprocess.run(command, check=True)
    body = json.loads(response.read_text(encoding="utf-8"))
    assert body["request_sha256"] == hashlib.sha256(request.read_bytes()).hexdigest()
    assert body["original_seed_unchanged"] and body["independent_source_prefixes"] == 1
    assert body["results"][0] == body["results"][1], "Independent branches must reproduce all public projections"
    item = body["results"][0]
    assert item["supported"] and item["status"] == "ok"
    before = item["transitions"][0]["before"]["actors"][1]
    after = item["transitions"][0]["after"]["actors"][1]
    assert before["shield"] == 25 and after["shield"] == 0 and before["hp"] - after["hp"] == 22
    assert item["query_status"]["errors"] == 0
    refused = body["results"][2]["transitions"]
    assert not refused[0]["applied"] and not refused[1]["applied"]
    assert refused[1]["code"] == 26 and "available attack" in refused[1]["error"]
    assert not body["results"][3]["supported"] and "Unsupported native setup field" in body["results"][3]["failure"]
    (out / "native-response.json").write_text(json.dumps(body, indent=2), encoding="utf-8")
    request.write_text(json.dumps({"kind": "scenario_validation", "scenario_sha256": "a", "trace_sha256": "b",
                                   "scenario": {"camps": []}, "actions": []}), encoding="utf-8")
    subprocess.run(command, check=True)
    unsupported = json.loads(response.read_text(encoding="utf-8"))
    assert not unsupported["native"] and not unsupported["supported"] and not unsupported["passed"]
    assert unsupported["scenario_sha256"] == "a" and unsupported["trace_sha256"] == "b"
    print("C11 native oracle tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
