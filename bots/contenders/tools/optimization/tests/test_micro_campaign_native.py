"""Focused real-native multi-world mining/search/resume check, with no synthetic backend.

Pass a frozen executable containing the production micro and campaign sources.
Native adapter calls acquire the existing shared slots; this driver reserves none.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def read(path):
    return json.loads(Path(path).read_text(encoding="utf8"))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--seconds", type=float, default=240)
    args = parser.parse_args()
    repo, out, executable = args.repo.resolve(), args.out.resolve(), args.executable.resolve()
    package = repo / "bots/contenders/tools/optimization"
    config = package / "examples/micro-worlds.json"
    if out.exists():
        raise RuntimeError("Use a new output directory; this check owns the initial and resumed invocations")
    commands = []
    def launch(iterations, resume=False):
        command = [str(executable), "c12", "--repo", str(repo), "--input", str(config),
                   "--out", str(out), "--iterations", str(iterations), "--seed", "73",
                   "--seconds", str(args.seconds)]
        if resume:
            command.append("--resume")
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                                encoding="utf8", timeout=args.seconds + 45)
        commands.append(command)
        if out.is_dir():
            (out / f"verification-{iterations}.stdout.log").write_text(result.stdout, encoding="utf8")
            (out / f"verification-{iterations}.stderr.log").write_text(result.stderr, encoding="utf8")
        if result.returncode:
            raise RuntimeError(f"Native campaign exit{result.returncode}: {result.stderr[-3000:]}")
        report = read(out / "report.json")
        if report["failed_cases"]:
            raise RuntimeError("Native child failures; inspect case-calls and case comparisons")
        assert report["cases"] == 4 and report["native_evaluations"] == iterations, report
        return report, read(out / "checkpoint.json")

    first, saved = launch(8)
    initial_keys = [row["key"] for row in saved["state"]["cases"]]
    inventories = saved["state"]["inventories"]
    assert len(inventories) == 2
    assert sorted(row["evaluations"] for row in saved["state"]["case_states"].values()) == [2] * 4
    final, resumed = launch(16, True)
    assert [row["key"] for row in resumed["state"]["cases"]] == initial_keys
    assert resumed["state"]["inventories"] == inventories, "Resume changed the selected inventory/corpus"
    assert sorted(row["evaluations"] for row in resumed["state"]["case_states"].values()) == [4] * 4
    comparisons = read(out / "case-comparisons.json")
    evidence = []
    for row in comparisons["cases"]:
        folder = out / row["directory"]
        report = read(folder / "report.json")
        root = read(folder / "root-state.json")
        actor = next(champion for champion in root["champions"] if champion["id"] == row["scenario"]["actor"])
        assert root["current_owner"] == actor["id"]
        assert actor["type"] == row["metadata"]["kit"] and actor["items"] == row["metadata"]["items"]
        assert sha(row["scenario"]["replay"]) == row["scenario"]["replay_sha256"]
        assert report["backend_authority"] == "native_gameplay" and report["native_evaluations"] == 4
        assert row["comparison"]["native_health"]["last_child_exit_code"] == 0
        assert row["comparison"]["native_health"]["report_matches_checkpoint"]
        assert (folder / "policy.json").is_file() and (folder / "frontier.json").is_file()
        evidence.append({"case": row["id"], "kind": row["kind"], "family": row["metadata"]["family"],
            "input": row["scenario"]["input"], "actor": actor["id"], "kit": actor["type"], "items": actor["items"],
            "health": actor["health"], "position": [actor["x"], actor["y"]], "cooldowns": actor["cooldowns"],
            "native_evaluations": report["native_evaluations"], "status": report["status"],
            "baseline": report["baseline"], "best": report["best"],
            "report": (Path(row["directory"]) / "report.json").as_posix(),
            "root_state": (Path(row["directory"]) / "root-state.json").as_posix(),
            "policy": (Path(row["directory"]) / "policy.json").as_posix(),
            "frontier": (Path(row["directory"]) / "frontier.json").as_posix()})
    assert {row["kind"] for row in evidence} == {"combat", "farm"}
    sources = ["src/micro.cpp", "src/micro_campaign.cpp", "include/opt/micro.hpp", "include/opt/micro_campaign.hpp",
               "adapters/native_micro.py", "adapters/native_micro.mjs", "examples/micro-worlds.json",
               "tests/test_micro_campaign_native.py"]
    record = {"schema": "contenders-micro-campaign-implementation-check/v1", "passed": True,
        "backend": "pinned native gameplay engine; no fixture or constructed-state restore",
        "executable": {"path": str(executable), "sha256": sha(executable)},
        "source_sha256": {name: sha(package / name) for name in sources},
        "commands": commands, "native_search_evaluations": final["native_evaluations"],
        "native_setup_and_feasible_control_calls_are_outside_search_counter": True,
        "resume": {"initial_budget": 8, "final_budget": 16, "same_cases_and_inventory": True,
                   "per_case_evaluations": [4] * 4},
        "coverage": comparisons["coverage"], "cases": evidence,
        "artifacts": ["case-comparisons.json", "mined-cases.json", "campaign.md", "checkpoint.json", "replays/"],
        "qualification": "Focused implementation check only. Per-case finite-horizon heuristics and feasible controls are not complete-game strength or optimality proofs."}
    (out / "implementation-check.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"passed": True, "cases": 4, "prefix_evaluations": 16,
                      "evidence": str(out / "implementation-check.json")}))


if __name__ == "__main__":
    main()
