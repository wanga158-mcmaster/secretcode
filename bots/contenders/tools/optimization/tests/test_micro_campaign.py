"""C12 campaign scheduling and resume tests, with synthetic child/inventory fixtures.

No native games are run here. The C++ harness links the production campaign and
runtime; its same-executable single-case handler records deterministic progress.
Compilation uses arena's machine-wide slot manager. An existing harness may be
passed through MICRO_CAMPAIGN_TEST_EXE to avoid compiling it again.
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[5]
PACKAGE = ROOT / "bots/contenders/tools/optimization"
TESTS = PACKAGE / "tests"
SCRATCH = ROOT / ".cache/contenders/optimization/micro-campaign-tests"


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def lines(path):
    return [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines()]


class CampaignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        supplied = os.environ.get("MICRO_CAMPAIGN_TEST_EXE")
        if supplied:
            cls.executable = Path(supplied).resolve()
            if not cls.executable.is_file():
                raise RuntimeError("MICRO_CAMPAIGN_TEST_EXE does not exist")
            return
        cls.executable = SCRATCH / ("campaign-fixture.exe" if os.name == "nt" else "campaign-fixture")
        compiler = os.environ.get("CXX") or shutil.which("g++")
        if not compiler and os.name == "nt":
            candidate = Path("C:/msys64/ucrt64/bin/g++.exe")
            if candidate.is_file():
                compiler = str(candidate)
        if not compiler:
            raise unittest.SkipTest("C++20 host compiler unavailable")
        command = [sys.executable, "-B", "-I", str(PACKAGE / "adapters/build_host.py"),
                   "--repo", str(ROOT), "--", compiler, "-std=c++20", "-O1", "-pthread",
                   "-Wall", "-Wextra", "-Wpedantic", "-I", str(PACKAGE / "include"),
                   str(PACKAGE / "src/common.cpp"), str(PACKAGE / "src/micro_campaign.cpp"),
                   str(TESTS / "micro_campaign_fixture.cpp"), "-o", str(cls.executable)]
        if os.name == "nt":
            command.extend(["-static", "-static-libgcc", "-static-libstdc++"])
        built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        if built.returncode:
            raise RuntimeError("Fixture compilation failed:\n" + built.stdout[-12000:] + built.stderr[-12000:])

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="case-", dir=SCRATCH)
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.replay = self.directory / "fixture-not-a-game.repl26a"
        self.replay.write_bytes(b"campaign orchestration fixture; not a real replay\x00\x01")
        self.out = self.directory / "output"
        self.input = self.directory / "input.json"
        self.inventory_log = self.directory / "inventory-calls.jsonl"

    def config(self):
        return dict(schema="contenders-micro-campaign/v1",
                    defaults=dict(search=dict(max_nodes=4), generation=dict(ground_full=True),
                                  scenario=dict(background_policy="yield")),
                    campaign=dict(nodes_per_case=4))

    def explicit_config(self):
        config = self.config()
        combat = dict(id="combat", scenario=dict(replay=str(self.replay), input=10,
                                                actor=0, participants=[0, 1]),
                      objective=dict(kind="combat"))
        farm = dict(id="farm", scenario=dict(replay=str(self.replay), input=30,
                                            actor=4, participants=[4]),
                    objective=dict(kind="farm", group=7, require_capture=True))
        duplicate = copy.deepcopy(combat)
        duplicate.update(id="same-case-another-label", metadata=dict(note="label does not change a case"))
        config["cases"] = [combat, farm, duplicate]
        return config

    def launch(self, config, budget, resume=False, expect=0, seconds=0, environment=None):
        if not resume:
            self.input.write_text(json.dumps(config, sort_keys=True), encoding="utf-8")
        command = [str(self.executable), "c12", "--repo", str(ROOT), "--input", str(self.input),
                   "--out", str(self.out), "--iterations", str(budget), "--seed", "41",
                   "--evaluator", sys.executable, "--evaluator-arg", str(TESTS / "micro_campaign_fixture.py")]
        if resume:
            command.append("--resume")
        if seconds:
            command.extend(("--seconds", str(seconds)))
        env = dict(os.environ, MICRO_CAMPAIGN_FIXTURE_LOG=str(self.inventory_log))
        if environment:
            env.update(environment)
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=40, env=env)
        if expect == 0:
            self.assertEqual(result.returncode, 0, result.stdout[-5000:] + result.stderr[-5000:])
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout[-5000:] + result.stderr[-5000:])
        return result

    def comparisons(self):
        return read(self.out / "case-comparisons.json")

    def test_exact_dedup_fair_budget_and_resume(self):
        config = self.explicit_config()
        self.launch(config, 4)
        report = read(self.out / "report.json")
        self.assertEqual((report["cases"], report["duplicate_cases"], report["native_evaluations"]), (2, 1, 4))
        comparisons = self.comparisons()
        self.assertEqual(comparisons["task_complete_cases"], 2)
        self.assertEqual(comparisons["improved_local_objective_cases"], 2)
        for row in comparisons["cases"]:
            self.assertEqual(row["search"]["evaluations"], 2)
            self.assertEqual(row["comparison"]["baseline"]["value"], 10)
            self.assertEqual(row["comparison"]["best"]["value"], 12)
            cfg = read(self.out / row["config"])
            self.assertEqual(cfg["schema"], "contenders-micro/v1")
            self.assertEqual(cfg["scenario"]["background_policy"], "yield")
            self.assertTrue(cfg["generation"]["ground_full"])
            self.assertNotIn("cases", cfg)
            self.assertNotIn("campaign", cfg)
            retained = Path(cfg["scenario"]["replay"])
            self.assertTrue(retained.is_relative_to(self.out))
            self.assertEqual(hashlib.sha256(retained.read_bytes()).hexdigest(), cfg["scenario"]["replay_sha256"])
        self.launch(config, 8, resume=True)
        self.assertEqual(read(self.out / "checkpoint.json")["iteration"], 8)
        self.assertEqual(read(self.out / "report.json")["native_evaluations"], 8)
        for row in self.comparisons()["cases"]:
            self.assertEqual(row["search"]["evaluations"], 4)
            self.assertEqual(row["search"]["status"], "exhausted")
            self.assertEqual(row["search"]["child_terminal_status"], "exhausted_declared_tree")
            calls = lines(self.out / row["directory"] / "fixture-invocations.jsonl")
            self.assertEqual([(x["previous"], x["target"], x["resume"]) for x in calls],
                             [(0, 2, False), (2, 4, True)])

    def test_inventory_selects_diverse_native_boundaries_once(self):
        config = self.config()
        config["inventory"] = dict(sources=[dict(id="retained", family="fixture-family",
                                                replay=str(self.replay), first_input=10, last_input=80)],
                                   max_cases=2, per_source=2, min_input_gap=8,
                                   combat_radius=10, farm_radius=12)
        self.launch(config, 4)
        report = read(self.out / "report.json")
        self.assertTrue(report["mining_complete"])
        self.assertEqual(report["cases"], 2)
        mining = read(self.out / "mined-cases.json")
        self.assertEqual(mining["candidate_contexts"], 4)
        self.assertEqual({x["kit"] for x in mining["selected"]}, {2, 10})
        selected = [x["source_input"] for x in mining["selected"]]
        self.assertGreaterEqual(abs(selected[0] - selected[1]), 8)
        rows = self.comparisons()["cases"]
        self.assertEqual({x["kind"] for x in rows}, {"combat", "farm"})
        for row in rows:
            self.assertEqual(row["metadata"]["selection"], "actual_owner_visible_replay_inventory")
            self.assertIn(row["scenario"]["input"], (10, 11, 30, 35))
            self.assertEqual(row["scenario"]["actor"], row["metadata"]["owner"])
        self.assertEqual(len(lines(self.inventory_log)), 1)
        self.launch(config, 8, resume=True)
        self.assertEqual(len(lines(self.inventory_log)), 1)
        self.assertEqual(read(self.out / "report.json")["native_evaluations"], 8)
        checkpoint = read(self.out / "checkpoint.json")
        saved = next(iter(checkpoint["state"]["inventories"].values()))
        retained_inventory = self.out / saved["path"]
        tampered = read(retained_inventory)
        tampered["states"][0]["owner"] = 9
        retained_inventory.write_text(json.dumps(tampered), encoding="utf-8")
        failed = self.launch(config, 8, resume=True, expect=1)
        self.assertIn("inventory", failed.stderr)
        self.assertEqual(len(lines(self.inventory_log)), 1)

    def test_reconcile_committed_child_before_cursor_and_global_stop(self):
        config = self.explicit_config()
        self.launch(config, 4)
        rows = self.comparisons()["cases"]
        calls_before = {row["key"]: lines(self.out / row["directory"] / "fixture-invocations.jsonl") for row in rows}
        # The child may commit its last prefix before producing any final report.
        (self.out / rows[0]["directory"] / "report.json").unlink()
        stale = subprocess.run([str(self.executable), "fixture-rewind", str(self.out / "checkpoint.json"),
                                rows[0]["key"], "1"], cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(stale.returncode, 0, stale.stdout + stale.stderr)
        self.assertEqual(read(self.out / "checkpoint.json")["iteration"], 2)
        self.launch(config, 4, resume=True)
        self.assertEqual(read(self.out / "report.json")["native_evaluations"], 4)
        self.assertEqual(read(self.out / "checkpoint.json")["iteration"], 4)
        for row in self.comparisons()["cases"]:
            self.assertEqual(row["search"]["evaluations"], 2)
            self.assertEqual(lines(self.out / row["directory"] / "fixture-invocations.jsonl"), calls_before[row["key"]])
        self.assertNotIn("comparison", self.comparisons()["cases"][0])
        self.launch(config, 8, resume=True)
        self.assertEqual(read(self.out / "report.json")["native_evaluations"], 8)
        self.assertTrue(all(row["comparison"]["native_evaluations"] == 4 for row in self.comparisons()["cases"]))

    def test_changed_retained_replay_fails_closed(self):
        config = self.explicit_config()
        self.launch(config, 4)
        first = self.comparisons()["cases"][0]
        retained = Path(first["scenario"]["replay"])
        retained.write_bytes(b"modified retained fixture bytes")
        failed = self.launch(config, 8, resume=True, expect=1)
        self.assertRegex(failed.stderr, "replay|identity|checksum|binding")

    def test_changed_child_input_fails_even_at_finished_global_budget(self):
        config = self.explicit_config()
        self.launch(config, 4)
        first = self.comparisons()["cases"][0]
        retained_input = self.out / first["config"]
        altered = read(retained_input)
        altered["objective"]["kind"] = "farm"
        retained_input.write_text(json.dumps(altered), encoding="utf-8")
        failed = self.launch(config, 4, resume=True, expect=1)
        self.assertRegex(failed.stderr, "input|config|identity|checksum")

    def test_exhausted_children_stop_without_repeating_forever(self):
        config = self.explicit_config()
        config["defaults"]["search"]["max_nodes"] = 1
        self.launch(config, 8)
        report = read(self.out / "report.json")
        self.assertEqual(report["native_evaluations"], 2)
        self.assertEqual(self.comparisons()["failed_cases"], 0)
        for row in self.comparisons()["cases"]:
            self.assertEqual(row["search"]["evaluations"], 1)
            self.assertEqual(row["search"]["status"], "exhausted")
            self.assertEqual(row["search"]["child_terminal_status"], "exhausted_declared_tree")
            calls = lines(self.out / row["directory"] / "fixture-invocations.jsonl")
            self.assertEqual(len(calls), 1)

    def test_search_resource_cap_is_not_declared_tree_exhaustion(self):
        config = self.explicit_config()
        config["defaults"]["search"]["max_nodes"] = 1
        config["defaults"]["fixture"] = dict(cap_status="resource_limited")
        self.launch(config, 8)
        report = read(self.out / "report.json")
        self.assertEqual(report["status"], "resource_limited")
        self.assertEqual(report["resource_capped_cases"], 2)
        self.assertEqual(report["completed_or_exhausted_cases"], 0)
        self.assertEqual(report["failed_cases"], 0)
        self.assertEqual(report["native_evaluations"], 2)
        for row in self.comparisons()["cases"]:
            self.assertEqual(row["search"]["status"], "resource_limited")
            self.assertEqual(row["search"]["child_terminal_status"], "resource_limited")
            self.assertTrue(row["comparison"]["native_health"]["report_matches_checkpoint"])
        self.launch(config, 8, resume=True)
        for row in self.comparisons()["cases"]:
            self.assertEqual(len(lines(self.out / row["directory"] / "fixture-invocations.jsonl")), 1)

    def test_unknown_no_progress_pauses_without_exhaustion_claim(self):
        config = self.explicit_config()
        config["cases"] = config["cases"][:1]
        config["defaults"]["search"]["max_nodes"] = 0
        config["defaults"]["fixture"] = dict(cap_status="checkpointed")
        for resumed in (False, True):
            self.launch(config, 8, resume=resumed)
            report = read(self.out / "report.json")
            self.assertEqual(report["status"], "paused_anytime_with_checkpoints")
            self.assertEqual(report["native_evaluations"], 0)
            self.assertEqual(report["resource_capped_cases"], 0)
            self.assertEqual(report["completed_or_exhausted_cases"], 0)
            row = self.comparisons()["cases"][0]
            self.assertEqual(row["search"]["status"], "paused_anytime")
            self.assertEqual(row["search"]["child_terminal_status"], "checkpointed")
            self.assertEqual(row["search"]["attempts"], 2 if resumed else 1)

    def test_startup_deadline_preserves_manifest_then_resumes_without_checkpoint(self):
        config = self.explicit_config()
        marker = self.directory / "startup-deadline-attempted"
        environment = {"MICRO_CAMPAIGN_FIXTURE_STARTUP_DEADLINE_ONCE": str(marker)}
        self.launch(config, 4, seconds=5, environment=environment)
        report = read(self.out / "report.json")
        self.assertEqual(report["status"], "stopped_at_limit_with_checkpoints")
        self.assertEqual(report["native_evaluations"], 0)
        self.assertEqual(report["failed_cases"], 0)
        first = self.comparisons()["cases"][0]
        child_directory = self.out / first["directory"]
        self.assertTrue((child_directory / "run.json").is_file())
        self.assertFalse((child_directory / "checkpoint.json").exists())
        self.assertEqual(first["search"]["status"], "paused_at_limit")
        self.assertEqual(read(child_directory / "run-status.json")["status"], "time_limit_checkpointed")
        self.assertFalse(first["comparison"]["native_health"]["report_matches_checkpoint"])
        self.launch(config, 4, resume=True, environment=environment)
        self.assertEqual(read(self.out / "report.json")["native_evaluations"], 4)
        self.assertTrue((child_directory / "checkpoint.json").is_file())
        calls = lines(child_directory / "fixture-invocations.jsonl")
        self.assertEqual([(call["previous"], call["resume"]) for call in calls], [(0, False), (0, True)])

    def test_ordinary_startup_failure_remains_a_failure(self):
        config = self.explicit_config()
        marker = self.directory / "startup-failure-attempted"
        self.launch(config, 4, environment={"MICRO_CAMPAIGN_FIXTURE_STARTUP_FAILURE_ONCE": str(marker)})
        report = read(self.out / "report.json")
        self.assertEqual(report["status"], "completed_with_case_failures")
        self.assertEqual(report["failed_cases"], 1)
        first = self.comparisons()["cases"][0]
        self.assertEqual(first["search"]["status"], "failed")
        self.assertEqual(read(self.out / first["directory"] / "run-status.json")["status"], "failed_checkpoint_preserved")
        self.assertFalse(first["comparison"]["native_health"]["report_matches_checkpoint"])

    def test_corrupted_child_checkpoint_is_not_accounted_as_work(self):
        config = self.explicit_config()
        self.launch(config, 4)
        first = self.comparisons()["cases"][0]
        checkpoint = self.out / first["directory"] / "checkpoint.json"
        corrupted = read(checkpoint)
        corrupted["iteration"] += 100
        checkpoint.write_text(json.dumps(corrupted), encoding="utf-8")
        failed = self.launch(config, 4, resume=True, expect=1)
        self.assertRegex(failed.stderr, "child|checkpoint|checksum")

    def test_checkpoint_from_another_case_fails_identity_check(self):
        config = self.explicit_config()
        self.launch(config, 4)
        rows = self.comparisons()["cases"]
        source = self.out / rows[0]["directory"] / "checkpoint.json"
        wrong_case = self.out / rows[1]["directory"] / "checkpoint.json"
        wrong_case.write_bytes(source.read_bytes())
        failed = self.launch(config, 4, resume=True, expect=1)
        self.assertRegex(failed.stderr, "child|checkpoint|identity")

    def test_inventory_per_source_limit_is_independent_of_global_limit(self):
        config = self.config()
        other_replay = self.directory / "another-fixture-not-a-game.repl26a"
        other_replay.write_bytes(self.replay.read_bytes() + b"different retained world")
        config["inventory"] = dict(sources=[dict(id=f"source-{i}", family=f"family-{i}",
                                                replay=str(replay), first_input=10, last_input=80)
                                            for i, replay in enumerate((self.replay, other_replay))],
                                   max_cases=4, per_source=1, min_input_gap=8)
        self.launch(config, 4)
        rows = self.comparisons()["cases"]
        self.assertEqual(len(rows), 2)
        self.assertEqual({row["metadata"]["source_id"] for row in rows}, {"source-0", "source-1"})
        self.assertEqual(len(lines(self.inventory_log)), 2)


if __name__ == "__main__":
    unittest.main()
