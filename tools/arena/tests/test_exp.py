"""Experiment evidence integrity and replay identity; no native games or builds."""
from __future__ import annotations

import copy
from contextlib import contextmanager, redirect_stdout, redirect_stderr
from concurrent.futures import ThreadPoolExecutor
import io
import hashlib
import json
from pathlib import Path
import sys
import shutil
import subprocess
import threading
import time
import uuid
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import exp
import core
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import checkup


class ExperimentPaths(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/review-experiments"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("path-test-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        mock = patch.object(exp, "ROOT", self.root)
        mock.start(); self.addCleanup(mock.stop)

    def test_normal_names_and_embedded_dots_stay_in_the_owner_experiment_directory(self):
        for name in ("owner/sample", "owner.v2/case.1", "_owner/.probe", "owner/v1..fix"):
            with self.subTest(name=name):
                owner, short = name.split("/")
                self.assertEqual(exp.exp_dir(name), self.root / ".cache" / owner / "exp" / short)

    def test_dot_segments_are_rejected_before_locks_or_mutation_and_keep_owner_data(self):
        sentinel = self.root / ".cache/owner/keep.txt"
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(b"unrelated owner data")
        for name in ("./sample", "../sample", "owner/.", "owner/..", "../..", "./."):
            for force in (False, True):
                with self.subTest(name=name, force=force), \
                        patch.object(exp, "operation_lock") as lock, \
                        patch.object(exp, "_run_experiment") as run, \
                        patch.object(exp.shutil, "rmtree") as remove:
                    with self.assertRaisesRegex(core.ArenaError, "cannot be"):
                        exp.cmd_run(SimpleNamespace(name=name, force=force))
                    lock.assert_not_called()
                    run.assert_not_called()
                    remove.assert_not_called()
                    self.assertEqual(sentinel.read_bytes(), b"unrelated owner data")

    def test_windows_aliases_and_device_names_are_rejected_before_locking(self):
        names = ("owner/...", "owner/name.", "owner./name", ".../sample",
                 "owner/CON", "con.txt/sample", "owner/PrN", "owner/AUX.data", "NUL/sample",
                 "owner/COM1", "com9.txt/sample", "owner/lpt1", "LPT9.log/sample")
        for name in names:
            with self.subTest(name=name), patch.object(exp, "operation_lock") as lock, \
                    patch.object(exp, "_run_experiment") as run:
                with self.assertRaises(core.ArenaError):
                    exp.cmd_run(SimpleNamespace(name=name, force=True))
                lock.assert_not_called()
                run.assert_not_called()

    def test_redirects_at_cache_owner_exp_or_name_are_rejected_before_locking(self):
        actual_resolve = Path.resolve
        cache = self.root / ".cache"
        for source in (cache, cache / "owner", cache / "owner/exp", cache / "owner/exp/sample"):
            # Model symlink/junction resolution without creating links or touching
            # another owner. Include a redirect that remains inside .cache.
            for target in (self.root / "outside", cache / "other/exp/other"):
                def redirected(path, *args, **kwargs):
                    if path == source or path.is_relative_to(source):
                        return target / path.relative_to(source)
                    return actual_resolve(path, *args, **kwargs)
                with self.subTest(source=source, target=target), \
                        patch.object(Path, "resolve", redirected), \
                        patch.object(exp, "operation_lock") as lock, \
                        patch.object(exp, "_run_experiment") as run:
                    with self.assertRaisesRegex(core.ArenaError, "redirects"):
                        exp.cmd_run(SimpleNamespace(name="owner/sample", force=True))
                    lock.assert_not_called()
                    run.assert_not_called()


class Audit(unittest.TestCase):
    def setUp(self):
        self.bots = [core.Bot("odd", "one"), core.Bot("odd", "two"), core.Bot("odd", "new")]
        self.spec = {"name": "owner/sample", "subjects": [
            {"label": "first", "bot": "odd/one", "set": {}, "weights": None},
            {"label": "second", "bot": "odd/two", "set": {}, "weights": None}],
            "entrants": {"first": "odd/one@aaaaaaaaaaaa", "second": "odd/two@aaaaaaaaaaaa",
                         "anchors/ref": "anchors/ref@bbbbbbbbbbbb"},
            "opponent_ids": ["anchors/ref"], "maps": "map-one", "sides": "both", "rounds": 0, "games": 4}
        self.rows = [{"subject": label, "opponent": "anchors/ref", "map": "map-one", "side": side,
                      "result": "loss", "rounds": 1000, "gems": 0, "opp_gems": 1000,
                      "health": {}, "opp_health": {}}
                     for label in ("first", "second") for side in ("A", "B")]
        for target, value in (("select", self.bots), ("version", "aaaaaaaaaaaa"), ("map_set", ["map-one"])):
            mock = patch.object(core, target, return_value=value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "provenance_gaps", return_value=["legacy experiment: unpinned"])
        mock.start(); self.addCleanup(mock.stop)

    def audit(self):
        return exp.audit(self.spec, self.rows)

    def test_accounts_every_current_bot_without_admitting_telemetry_twins(self):
        report = self.audit()
        self.assertTrue(report["integrity_pass"])
        self.assertEqual(report["status_counts"], {"observed-clean": 2, "missing": 1})
        self.assertEqual(report["identical_observed_rows"], [["first", "second"]])
        self.assertTrue(all(row["contribution"] == "unverified" for row in report["bots"]))
        self.assertTrue(report["provenance_gaps"])

    def test_missing_and_duplicate_keys_cannot_cancel_out(self):
        self.rows[1] = copy.deepcopy(self.rows[0])
        report = self.audit()
        self.assertFalse(report["integrity_pass"])
        observed = report["bots"][0]["observations"][0]
        self.assertEqual((observed["missing_keys"], observed["duplicate_keys"]), (1, 1))
        self.assertEqual(report["bots"][0]["status"], "incomplete")

    def test_stale_sources_and_nondefault_variants_are_explicit(self):
        self.spec["entrants"]["first"] = "odd/one@cccccccccccc"
        self.spec["subjects"][1]["set"] = {"odd": "1"}
        report = self.audit()
        self.assertEqual(report["status_counts"], {"stale": 2, "missing": 1})
        self.assertTrue(report["bots"][1]["observations"][0]["source_current"])
        self.assertFalse(report["bots"][1]["observations"][0]["default_configuration"])

    def test_own_opponent_health_runner_error_and_truncation_stay_separate(self):
        self.rows[0]["health"] = {"errors": 1, "runner_error": "failed"}
        self.rows[1]["opp_health"] = {"exhausted": 2}
        self.rows[2]["truncated"] = True
        report = self.audit()
        self.assertEqual(report["status_counts"], {"unhealthy": 2, "missing": 1})
        observed = report["bots"][0]["observations"][0]
        self.assertEqual(observed["own_flagged_games"], 1)
        self.assertEqual(observed["opponent_flagged_games"], 1)
        self.assertEqual(observed["opponent_health"], {"exhausted": 2})

    def test_unknown_key_and_invalid_health_fail_integrity(self):
        self.rows[0]["side"] = "C"
        self.rows[1]["health"] = {"errors": "zero"}
        report = self.audit()
        self.assertFalse(report["integrity_pass"])
        self.assertEqual(sum("invalid or unexpected" in gap for gap in report["integrity_issues"]), 2)

    def test_malformed_identity_and_health_shapes_fail_closed(self):
        self.rows[0]["subject"] = []
        self.rows[1]["health"] = "bad"
        self.assertFalse(self.audit()["integrity_pass"])

    def test_old_spec_uses_recorded_opponents_without_expanding_current_gauntlet(self):
        del self.spec["opponent_ids"]
        self.spec["opponents"] = ["gauntlet"]
        self.assertEqual(self.audit()["coverage"]["opponents"], ["anchors/ref"])


class WinFloor(unittest.TestCase):
    def setUp(self):
        self.subject = core.Bot("odd", "one")
        self.target = core.Bot("anchors", "main")
        self.pool = [f"pool-{i:02}" for i in range(40)]
        self.spec = {"name": "owner/floor", "subjects": [
            {"label": "first", "bot": self.subject.id, "set": {}, "weights": None}],
            "entrants": {"first": "odd/one@aaaaaaaaaaaa", "anchors/main": "anchors/main@bbbbbbbbbbbb"},
            "opponent_ids": [self.target.id], "maps": "pool", "sides": "both", "rounds": 0, "games": 80,
            "provenance": {"maps": dict.fromkeys(self.pool, "pinned")}}
        self.rows = [{"subject": "first", "opponent": self.target.id, "map": name, "side": side,
                      "result": "loss", "rounds": 1000, "gems": 0, "opp_gems": 1000,
                      "health": {}, "opp_health": {}} for name in self.pool for side in ("A", "B")]
        self.rows[0].update(result="win", gems=1000, opp_gems=800)
        for target, value in (("select", [self.subject]), ("bot", self.target), ("map_set", self.pool)):
            mock = patch.object(core, target, return_value=value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(core, "version", side_effect=lambda bot, *args:
                            "bbbbbbbbbbbb" if bot.id == self.target.id else "aaaaaaaaaaaa")
        self.version = mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "provenance_gaps", return_value=[])
        self.provenance = mock.start(); self.addCleanup(mock.stop)

    def audit(self, opponent=None):
        return exp.audit(self.spec, self.rows, win_floor_vs=opponent or self.target.id)

    def floor(self, opponent=None):
        return self.audit(opponent)["bots"][0]["win_floor"]

    def test_complete_current_default_healthy_win_passes_only_measured_floor(self):
        report = self.audit()
        self.assertTrue(report["win_floor"]["all_passed"])
        floor = report["bots"][0]["win_floor"]
        self.assertEqual((floor["wins"], floor["draws"], floor["losses"], floor["healthy_wins"]), (1, 0, 79, 1))
        observed = report["bots"][0]["observations"][0]
        self.assertEqual(observed["by_opponent"][self.target.id]["strict_win_rate"], 1 / 80)
        self.assertEqual(report["bots"][0]["contribution"], "unverified")

    def test_draws_never_count_as_strict_or_healthy_wins(self):
        for row in self.rows:
            row["result"] = "draw"
        floor = self.floor()
        self.assertFalse(floor["qualified"])
        self.assertEqual((floor["wins"], floor["draws"], floor["healthy_wins"]), (0, 80, 0))

    def test_wins_against_other_opponent_do_not_satisfy_target(self):
        self.rows[0]["result"] = "loss"
        others = [dict(row, opponent="anchors/fundamentals", result="win") for row in self.rows]
        self.rows.extend(others)
        self.spec["opponent_ids"].append("anchors/fundamentals")
        self.spec["entrants"]["anchors/fundamentals"] = "anchors/fundamentals@cccccccccccc"
        self.spec["games"] = 160
        report = self.audit()
        self.assertTrue(report["integrity_pass"])
        observed = report["bots"][0]["observations"][0]
        self.assertEqual(observed["wins"], 80)
        self.assertEqual(observed["by_opponent"]["anchors/fundamentals"]["wins"], 80)
        self.assertEqual(report["bots"][0]["win_floor"]["wins"], 0)
        self.assertFalse(report["win_floor"]["all_passed"])
        self.assertFalse(self.floor("anchors/missing")["qualified"])

    def test_health_on_any_subject_turn_blocks_panel_and_unhealthy_opponent_win_does_not_count(self):
        self.rows[1]["health"] = {"exhausted": 1}
        floor = self.floor()
        self.assertEqual(floor["healthy_wins"], 1)
        self.assertFalse(floor["qualified"])
        self.rows[1]["health"] = {}
        self.rows[0]["opp_health"] = {"errors": 1}
        floor = self.floor()
        self.assertEqual((floor["wins"], floor["healthy_wins"]), (1, 0))
        self.assertFalse(floor["qualified"])

    def test_exact_pool_names_both_sides_and_complete_games_are_required(self):
        original_spec, original_rows = copy.deepcopy(self.spec), copy.deepcopy(self.rows)
        cases = ("partial", "wrong-map", "one-side", "truncated", "round-limit")
        for case in cases:
            with self.subTest(case=case):
                self.spec, self.rows = copy.deepcopy(original_spec), copy.deepcopy(original_rows)
                if case == "partial":
                    self.spec["provenance"]["maps"].pop(self.pool[-1])
                    self.rows = [r for r in self.rows if r["map"] != self.pool[-1]]
                    self.spec["games"] = 78
                elif case == "wrong-map":
                    self.spec["provenance"]["maps"].pop(self.pool[-1])
                    self.spec["provenance"]["maps"]["different-map"] = "pinned"
                    for row in self.rows:
                        if row["map"] == self.pool[-1]:
                            row["map"] = "different-map"
                elif case == "one-side":
                    self.spec["sides"] = "A"
                    self.rows = [r for r in self.rows if r["side"] == "A"]
                    self.spec["games"] = 40
                elif case == "truncated":
                    self.rows[1]["truncated"] = True
                else:
                    self.spec["rounds"] = 1000
                self.assertFalse(self.floor()["qualified"])

    def test_nondefault_variant_weights_or_stale_subject_cannot_pass(self):
        self.spec["subjects"][0]["set"] = {"KNOB": "1"}
        self.assertFalse(self.floor()["qualified"])
        self.spec["subjects"][0]["set"] = {}
        self.spec["subjects"][0]["weights"] = "weights.txt"
        self.assertFalse(self.floor()["qualified"])
        self.spec["subjects"][0]["weights"] = None
        self.spec["entrants"]["first"] = "odd/one@cccccccccccc"
        self.assertFalse(self.floor()["qualified"])

    def test_target_identity_and_runtime_drift_block_floor(self):
        self.spec["entrants"][self.target.id] = "anchors/main@cccccccccccc"
        self.assertFalse(self.floor()["qualified"])
        self.spec["entrants"][self.target.id] = "anchors/main@bbbbbbbbbbbb"
        self.provenance.return_value = ["runner changed"]
        self.assertFalse(self.floor()["qualified"])

    def test_duplicate_missing_and_ambiguous_default_panels_cannot_pass(self):
        original = copy.deepcopy(self.rows)
        self.rows[1] = copy.deepcopy(self.rows[0])
        self.assertFalse(self.floor()["qualified"])
        self.rows = original + [dict(row, subject="second") for row in original]
        self.spec["subjects"].append(dict(self.spec["subjects"][0], label="second"))
        self.spec["entrants"]["second"] = self.spec["entrants"]["first"]
        self.spec["games"] = 160
        report = self.audit()
        self.assertTrue(report["integrity_pass"])
        self.assertFalse(report["bots"][0]["win_floor"]["qualified"])
        self.assertIsNone(report["bots"][0]["win_floor"]["wins"])

    def test_transient_subject_identity_failure_or_change_cannot_pass(self):
        for first in (core.ArenaError("source busy"), "cccccccccccc"):
            with self.subTest(first=first):
                calls = iter([first, "aaaaaaaaaaaa"])
                def version(bot, *args):
                    if bot.id == self.target.id:
                        return "bbbbbbbbbbbb"
                    value = next(calls)
                    if isinstance(value, Exception):
                        raise value
                    return value
                self.version.side_effect = version
                self.assertFalse(self.floor()["qualified"])

    def test_census_prints_draws_but_zero_strict_win_rate(self):
        for row in self.rows:
            row["result"] = "draw"
        output = io.StringIO()
        with redirect_stdout(output):
            exp.census(self.rows)
        self.assertIn("W-D-L win%", output.getvalue())
        self.assertIn("0-80-0 0.0%", output.getvalue())
        self.assertNotIn("50.0%", output.getvalue())

    def test_cli_floor_returns_failure_without_changing_plain_audit_semantics(self):
        directory = core.ROOT / ".cache/arena-tests/floor-command"
        args = SimpleNamespace(name="owner/floor", subjects=exp.DEVELOPMENT, out=None,
                               json=False, win_floor_vs=self.target.id)
        with patch.object(exp, "load", return_value=(directory, self.spec)), \
                patch.object(exp, "read_rows", return_value=self.rows), \
                patch.object(exp, "sha256", return_value="pinned"), redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_audit(args), 0)
            self.rows[0]["result"] = "draw"
            self.assertEqual(exp.cmd_audit(args), 2)
            args.win_floor_vs = None
            self.assertEqual(exp.cmd_audit(args), 0)


class PreparationStartup(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/arena-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("preparation-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        for module, name, value in ((exp, "ROOT", self.root), (core, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena"),
                                    (checkup, "ROOT", self.root), (checkup, "CACHE", self.root / ".cache"),
                                    (checkup, "SKIP_DIRS", {self.root / ".cache/arena/snap"})):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)
        self.args = SimpleNamespace(name="owner/preparing", question="does this complete?", force=False,
                                    subject=["base=odd/one"], subjects=None, vs=["anchors/ref"],
                                    maps="map-one", sides="both", rounds=0, keep="none")
        self.directory = exp.exp_dir(self.args.name)
        self.map = self.root / "maps/map-one.map26a"
        self.map.parent.mkdir(); self.map.write_bytes(b"map")
        self.subject = SimpleNamespace(key="odd/one@aaaaaaaaaaaa", bot=core.Bot("odd", "one"), path=self.root / "subject")
        self.opponent = SimpleNamespace(key="anchors/ref@bbbbbbbbbbbb", bot=core.Bot("anchors", "ref"), path=self.root / "opponent")
        for name, value in (("map_set", ["map-one"]), ("map_path", self.map)):
            mock = patch.object(core, name, return_value=value)
            mock.start(); self.addCleanup(mock.stop)
        self.pins = {"fixed": "exact input pins", "maps": {"map-one": "pinned"}}
        mock = patch.object(exp, "provenance", return_value=self.pins)
        self.provenance = mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "provenance_gaps", return_value=[])
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "summarize", return_value={"games": 2, "subjects": {}})
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "show")
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "run_games", side_effect=self.run_games)
        self.games = mock.start(); self.addCleanup(mock.stop)

    def pending_during_build(self):
        pending = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(pending["status"], "preparing")
        self.assertEqual(pending["question"], self.args.question)
        self.assertEqual(pending["subjects"][0]["bot"], "odd/one")
        self.assertEqual(pending["opponents"], ["anchors/ref"])
        self.assertEqual((pending["maps"], pending["sides"]), ("map-one", "both"))
        self.assertIn("not proof of a live worker", pending["preparation"]["notice"])
        self.assertNotIn("entrants", pending)
        # Make the preparation directory old enough to exercise the original
        # destructive empty-dir pass. Its persisted metadata must retain it.
        future = time.time() + 13 * 3600
        with patch.object(checkup.time, "time", return_value=future), redirect_stdout(io.StringIO()):
            checkup.slim(12, False)
        self.assertTrue((self.directory / "spec.json").is_file())
        self.assertFalse(list(self.directory.glob(".spec-*.tmp")))

    def run_games(self, *args, **kwargs):
        ready = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(ready["status"], "open")
        self.assertEqual(ready["preparation"]["state"], "complete")
        self.assertEqual(ready["provenance"], self.pins)
        return [{"tag": "s0.o0", "map": "map-one", "a_side": side, "winner": "a", "rounds": 40,
                 "gems": [1000, 0] if side == "A" else [0, 1000],
                 "exhausted": [0, 0], "faults": [0, 0], "errors": [0, 0]} for side in ("A", "B")]

    def test_pending_spec_survives_slim_during_build_and_completes_normally(self):
        def build(spec):
            self.pending_during_build()
            return [self.subject], [self.opponent]
        with patch.object(exp, "build", side_effect=build), redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_run(self.args), 0)
        spec = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(spec["games"], 2)
        self.assertEqual(spec["entrants"], {"base": self.subject.key, "anchors/ref": self.opponent.key})
        self.assertEqual(spec["opponent_descriptors"], {"anchors/ref": {"bot": "anchors/ref", "set": {}, "weights": None}})
        self.assertEqual(spec["status"], "open")
        self.assertTrue((self.directory / "rows.jsonl").is_file())
        self.assertTrue((self.directory / "summary.json").is_file())

    def test_preparation_records_actual_opponent_overrides(self):
        self.opponent.overrides = (("FIRST", "1"), ("SECOND", "1"), ("THIRD", "1"))
        with patch.object(exp, "build", return_value=([self.subject], [self.opponent])), redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_run(self.args), 0)
        spec = exp.read_spec(self.directory / "spec.json")
        self.assertEqual(spec["opponent_descriptors"]["anchors/ref"]["set"], dict(self.opponent.overrides))

    def test_failed_build_preserves_intent_details_and_original_error_after_slim(self):
        failure = core.ArenaError("controlled build failure")
        def build(spec):
            self.pending_during_build()
            raise failure
        with patch.object(exp, "build", side_effect=build), self.assertRaises(core.ArenaError) as caught:
            exp.cmd_run(self.args)
        self.assertIs(caught.exception, failure)
        spec = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(spec["status"], "preparation-failed")
        self.assertEqual(spec["preparation"]["error"], {"type": "ArenaError", "message": str(failure)})
        self.games.assert_not_called()

    def test_interrupted_build_is_recorded_without_live_job_claim(self):
        with patch.object(exp, "build", side_effect=KeyboardInterrupt("controlled stop")), self.assertRaises(KeyboardInterrupt):
            exp.cmd_run(self.args)
        spec = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(spec["status"], "preparation-interrupted")
        self.assertEqual(spec["preparation"]["state"], "interrupted")
        self.assertEqual(spec["preparation"]["error"]["type"], "KeyboardInterrupt")
        self.games.assert_not_called()

    def test_provenance_preparation_failure_retains_known_snapshot_identities(self):
        self.provenance.side_effect = ValueError("bad provenance")
        with patch.object(exp, "build", return_value=([self.subject], [self.opponent])), \
                self.assertRaisesRegex(ValueError, "bad provenance"):
            exp.cmd_run(self.args)
        spec = json.loads((self.directory / "spec.json").read_text())
        self.assertEqual(spec["status"], "preparation-failed")
        self.assertEqual(spec["entrants"]["base"], self.subject.key)
        self.assertEqual(spec["preparation"]["error"]["type"], "ValueError")
        self.games.assert_not_called()

    def test_atomic_spec_update_never_exposes_partly_written_json(self):
        self.directory.mkdir(parents=True)
        exp.write_spec(self.directory, {"generation": 0, "body": "initial"})
        stop, started = threading.Event(), threading.Event()
        errors = []
        def reader():
            started.set()
            while not stop.is_set():
                try:
                    value = exp.read_spec(self.directory / "spec.json")
                    if not isinstance(value.get("generation"), int):
                        errors.append("partial metadata")
                except Exception as error:
                    errors.append(error)
        worker = threading.Thread(target=reader)
        worker.start(); self.assertTrue(started.wait(timeout=5))
        try:
            for generation in range(1, 25):
                exp.write_spec(self.directory, {"generation": generation, "body": "x" * 10000})
        finally:
            stop.set(); worker.join(timeout=5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(json.loads((self.directory / "spec.json").read_text())["generation"], 24)
        self.assertFalse(list(self.directory.glob(".spec-*.tmp")))


class ReplayIsolation(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/arena-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("replay-isolation-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        self.directory = self.root / ".cache/owner/exp/sample"
        self.directory.mkdir(parents=True)
        self.map = self.root / "maps/map-one.map26a"
        self.map.parent.mkdir()
        self.map.write_bytes(b"fixed map")
        self.spec = {"name": "owner/sample", "question": "one replay", "created": "today", "status": "open",
                     "subjects": [{"label": "first", "bot": "odd/one", "set": {}, "weights": None},
                                  {"label": "second", "bot": "odd/two", "set": {}, "weights": None}],
                     "entrants": {"first": "odd/one@aaaaaaaaaaaa", "second": "odd/two@cccccccccccc",
                                  "anchors/ref": "anchors/ref@bbbbbbbbbbbb", "anchors/other": "anchors/other@dddddddddddd"},
                     "opponent_ids": ["anchors/ref", "anchors/other"], "maps": "map-one", "sides": "both", "rounds": 0,
                     "provenance": {"bundle": "bundle", "engine": "engine", "runner": "runner", "toolchain": "tool",
                                    "runner_libraries": {}, "maps": {"map-one": exp.sha256(self.map)}, "wasm": {}}}
        self.entries = {}
        for label, key in self.spec["entrants"].items():
            entry = SimpleNamespace(key=key, path=self.root / "modules" / key.rsplit("@", 1)[1])
            binary = Path(exp.wasm(entry)); binary.parent.mkdir(parents=True)
            binary.write_bytes(key.encode())
            self.entries[label] = entry
            self.spec["provenance"]["wasm"][label] = exp.sha256(binary)
        (self.directory / "spec.json").write_text(json.dumps(self.spec), encoding="utf-8")
        for module, name, value in ((exp, "ROOT", self.root), (core, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena")):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "load", return_value=(self.directory, self.spec))
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(core, "map_path", return_value=self.map)
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "provenance_gaps", return_value=[])
        self.provenance = mock.start(); self.addCleanup(mock.stop)
        self.local = threading.local()
        self.attempts = 0
        self.guard = threading.Lock()
        self.second_attempt = threading.Event()
        actual_case_lock = exp.replay_case_lock

        @contextmanager
        def marked_case(*args):
            with self.guard:
                self.attempts += 1
                if self.attempts == 2:
                    self.second_attempt.set()
            with actual_case_lock(*args):
                self.local.locked = True
                try:
                    yield
                finally:
                    self.local.locked = False

        mock = patch.object(exp, "replay_case_lock", side_effect=marked_case)
        mock.start(); self.addCleanup(mock.stop)

        def entrant(spec, label, subject):
            self.assertTrue(self.local.locked, "case lock must precede a possible snapshot build")
            if label in spec["opponent_ids"]:
                self.assertIsNone(subject, "opponents must resolve their recorded descriptor")
            return self.entries[label]
        mock = patch.object(exp, "replay_entrant", side_effect=entrant)
        mock.start(); self.addCleanup(mock.stop)

    def args(self, subject="first", opponent="anchors/ref", side="A"):
        return SimpleNamespace(name="owner/sample", subject=subject, opponent=opponent, map="map-one", side=side)

    def write_bundle(self, lines, bots, out, rounds, bundles, body=None):
        self.assertTrue(self.local.locked, "case lock must precede the game CPU-slot request")
        parts = lines[0].split()
        leaf = Path(bundles) / f"{Path(parts[0]).stem}_{parts[1]}_{parts[-1]}"
        leaf.mkdir(parents=True)
        payload = body if body is not None else (bots["s"] + "\n" + bots["o"]).encode()
        for name in ("replay.repl26a", "team-a.stdout", "team-b.stdout", "team-a.fuel", "team-b.fuel"):
            (leaf / name).write_bytes(payload if name != "team-b.stdout" else b"")
        return leaf

    def test_distinct_subjects_and_opponents_can_run_concurrently_without_clobbering(self):
        barrier = threading.Barrier(3)
        prefixes = []
        def run(lines, bots, out, rounds, bundles):
            with self.guard:
                prefixes.append(Path(out))
            barrier.wait(timeout=10)
            self.write_bundle(lines, bots, out, rounds, bundles)
            return [{}]
        requests = [self.args(), self.args("second"), self.args(opponent="anchors/other")]
        output = io.StringIO()
        with patch.object(exp, "run_games", side_effect=run), redirect_stdout(output), ThreadPoolExecutor(3) as executor:
            futures = [executor.submit(exp.cmd_replay, args) for args in requests]
            self.assertEqual([f.result(timeout=15) for f in futures], [0, 0, 0])
        published = list((self.directory / "replays").iterdir())
        self.assertEqual(len(published), 3)
        self.assertEqual(len(set(prefixes)), 3)
        actual = {(p / "replay.repl26a").read_bytes() for p in published}
        expected = {(exp.wasm(self.entries[a.subject]) + "\n" + exp.wasm(self.entries[a.opponent])).encode()
                    for a in requests}
        self.assertEqual(actual, expected)
        self.assertTrue(all(str(p.relative_to(self.root)) in output.getvalue() for p in published))
        self.assertFalse(list((self.directory / "replay-scratch").iterdir()))

    def test_same_recipe_serializes_before_slots_and_keeps_one_immutable_bundle(self):
        started, release = threading.Event(), threading.Event()
        prefixes = []
        active = maximum = 0
        def run(lines, bots, out, rounds, bundles):
            nonlocal active, maximum
            with self.guard:
                prefixes.append(Path(out)); active += 1; maximum = max(maximum, active)
                first = len(prefixes) == 1
            try:
                if first:
                    started.set()
                    self.assertTrue(release.wait(timeout=10))
                self.write_bundle(lines, bots, out, rounds, bundles)
                return [{}]
            finally:
                with self.guard:
                    active -= 1
        with patch.object(exp, "run_games", side_effect=run), redirect_stdout(io.StringIO()), ThreadPoolExecutor(2) as executor:
            first = executor.submit(exp.cmd_replay, self.args())
            self.assertTrue(started.wait(timeout=5))
            second = executor.submit(exp.cmd_replay, self.args())
            self.assertTrue(self.second_attempt.wait(timeout=5))
            try:
                self.assertEqual(len(prefixes), 1)
            finally:
                release.set()
            self.assertEqual((first.result(timeout=10), second.result(timeout=10)), (0, 0))
        self.assertEqual(maximum, 1)
        self.assertEqual(len(set(prefixes)), 2)
        self.assertEqual(len(list((self.directory / "replays").iterdir())), 1)
        self.assertFalse(list((self.directory / "replay-scratch").iterdir()))

    def test_failed_or_incomplete_replay_never_publishes_partial_output(self):
        for failure in ("exception", "missing-fuel", "empty-replay"):
            with self.subTest(failure=failure):
                def run(lines, bots, out, rounds, bundles):
                    leaf = self.write_bundle(lines, bots, out, rounds, bundles)
                    if failure == "exception":
                        raise core.ArenaError("fake native failure")
                    if failure == "missing-fuel":
                        (leaf / "team-a.fuel").unlink()
                    else:
                        (leaf / "replay.repl26a").write_bytes(b"")
                    return [{}]
                with patch.object(exp, "run_games", side_effect=run), self.assertRaises(core.ArenaError):
                    exp.cmd_replay(self.args())
                self.assertFalse((self.directory / "replays").exists())
                self.assertFalse(list((self.directory / "replay-scratch").iterdir()))

    def test_changed_inputs_or_nondeterministic_bytes_do_not_replace_completed_bundle(self):
        def run(lines, bots, out, rounds, bundles):
            self.write_bundle(lines, bots, out, rounds, bundles, b"original")
            return [{}]
        with patch.object(exp, "run_games", side_effect=run), redirect_stdout(io.StringIO()):
            exp.cmd_replay(self.args())
        published = next((self.directory / "replays").iterdir())
        def changed(lines, bots, out, rounds, bundles):
            self.write_bundle(lines, bots, out, rounds, bundles, b"changed")
            return [{}]
        with patch.object(exp, "run_games", side_effect=changed), self.assertRaisesRegex(core.ArenaError, "different bundle bytes"):
            exp.cmd_replay(self.args())
        self.assertEqual((published / "replay.repl26a").read_bytes(), b"original")
        def drift(lines, bots, out, rounds, bundles):
            self.write_bundle(lines, bots, out, rounds, bundles, b"changed")
            self.map.write_bytes(b"changed map")
            return [{}]
        with patch.object(exp, "run_games", side_effect=drift), self.assertRaisesRegex(core.ArenaError, "inputs changed"):
            exp.cmd_replay(self.args())
        self.assertEqual((published / "replay.repl26a").read_bytes(), b"original")
        self.map.write_bytes(b"fixed map")
        def runtime_drift(lines, bots, out, rounds, bundles):
            self.write_bundle(lines, bots, out, rounds, bundles, b"changed")
            self.provenance.return_value = ["runner changed"]
            return [{}]
        with patch.object(exp, "run_games", side_effect=runtime_drift), self.assertRaisesRegex(core.ArenaError, "inputs changed"):
            exp.cmd_replay(self.args())
        self.assertEqual((published / "replay.repl26a").read_bytes(), b"original")

    def test_legacy_recipe_binds_actual_module_and_runtime_bytes(self):
        del self.spec["provenance"]
        exp.write_spec(self.directory, self.spec)
        bundle = self.root / "docs/reference/current-version.json"
        bundle.parent.mkdir(parents=True); bundle.write_text("bundle")
        engine, runner = self.root / "engine.wasm", self.root / "runner.exe"
        engine.write_bytes(b"engine"); runner.write_bytes(b"runner")
        def run(lines, bots, out, rounds, bundles):
            self.write_bundle(lines, bots, out, rounds, bundles)
            return [{}]
        with patch.object(exp, "engine_wasm", return_value=engine), \
                patch.object(exp, "fastrun", return_value=runner), \
                patch.object(core, "toolchain", return_value="tool"), \
                patch.object(exp, "run_games", side_effect=run), redirect_stdout(io.StringIO()):
            exp.cmd_replay(self.args())
            Path(exp.wasm(self.entries["first"])).write_bytes(b"different actual module")
            exp.cmd_replay(self.args())
            engine.write_bytes(b"different actual engine")
            exp.cmd_replay(self.args())
        published = list((self.directory / "replays").iterdir())
        self.assertEqual(len(published), 3)
        recipes = [json.loads((p / "recipe.json").read_text()) for p in published]
        self.assertEqual(len({r["wasm"]["subject"] for r in recipes}), 2)
        self.assertEqual(len({r["runtime"]["engine"] for r in recipes}), 2)
        self.assertTrue(all(r["wasm"]["subject"] and r["wasm"]["opponent"] for r in recipes))

    def test_recipe_changes_for_modules_side_map_weights_and_runtime_pins(self):
        original_id, _ = exp.replay_recipe(self.spec, "first", "anchors/ref", self.map, "A")
        variants = [exp.replay_recipe(self.spec, "second", "anchors/ref", self.map, "A")[0],
                    exp.replay_recipe(self.spec, "first", "anchors/other", self.map, "A")[0],
                    exp.replay_recipe(self.spec, "first", "anchors/ref", self.map, "B")[0]]
        self.map.write_bytes(b"changed map")
        variants.append(exp.replay_recipe(self.spec, "first", "anchors/ref", self.map, "A")[0])
        self.map.write_bytes(b"fixed map")
        other_map = self.map.with_name("map-two.map26a"); other_map.write_bytes(b"fixed map")
        variants.append(exp.replay_recipe(self.spec, "first", "anchors/ref", other_map, "A")[0])
        weights = self.root / "weights.txt"; weights.write_text("weights")
        self.spec["subjects"][0]["weights"] = str(weights)
        variants.append(exp.replay_recipe(self.spec, "first", "anchors/ref", self.map, "A")[0])
        self.spec["subjects"][0]["weights"] = None
        self.spec["provenance"]["runner"] = "changed runner"
        variants.append(exp.replay_recipe(self.spec, "first", "anchors/ref", self.map, "A")[0])
        self.assertEqual(len(set([original_id] + variants)), 8)

    def test_close_waits_for_active_case_and_cannot_delete_its_lock_or_scratch(self):
        started, release, closing = threading.Event(), threading.Event(), threading.Event()
        def run(lines, bots, out, rounds, bundles):
            started.set()
            self.assertTrue(release.wait(timeout=10))
            self.write_bundle(lines, bots, out, rounds, bundles)
            return [{}]
        def close():
            closing.set()
            return exp.cmd_close(SimpleNamespace(name="owner/sample", note="done"))
        with patch.object(exp, "run_games", side_effect=run), redirect_stdout(io.StringIO()), ThreadPoolExecutor(2) as executor:
            replay = executor.submit(exp.cmd_replay, self.args())
            self.assertTrue(started.wait(timeout=5))
            ending = executor.submit(close)
            self.assertTrue(closing.wait(timeout=5))
            try:
                self.assertFalse(ending.done())
                self.assertTrue(list((self.directory / "replay-scratch").iterdir()))
            finally:
                release.set()
            self.assertEqual((replay.result(timeout=10), ending.result(timeout=10)), (0, 0))
        self.assertTrue(exp.replay_lock_root(self.directory).is_dir())
        self.assertTrue((self.directory / "spec.json").is_file())
        self.assertFalse((self.directory / "replays").exists())


class NativeReplay(unittest.TestCase):
    def setUp(self):
        ReplayIsolation.setUp(self)
        bundle = self.root / "docs/reference/current-version.json"
        bundle.parent.mkdir(parents=True); bundle.write_bytes(b"bundle")
        self.engine = self.root / "engine.wasm"; self.engine.write_bytes(b"engine")
        self.spec["provenance"].update(bundle=exp.sha256(bundle), engine=exp.sha256(self.engine), toolchain="tool")
        self.dependencies = [{"path": "native-runtime", "sha256": "e" * 64, "size": 1}]
        self.native_engine = {"version": "1.0.3", "buildHash": "f" * 64}
        for entry in self.entries.values():
            Path(exp.wasm(entry)).with_name("bot-injected.wasm").write_bytes(b"injected:" + entry.key.encode())

        def entrant(spec, label, subject, *, cached_only=False):
            self.assertTrue(cached_only, "native reproduction must never rebuild a recorded snapshot")
            return self.entries[label]

        def capture(key, role):
            entry = next(e for e in self.entries.values() if e.key == key)
            files = {}
            for name, filename in (("raw", "bot.wasm"), ("injected", "bot-injected.wasm")):
                path = Path(exp.wasm(entry)).with_name(filename)
                if not path.is_file():
                    raise core.ArenaError("cached executable missing: " + str(path))
                files[name] = {"path": str(path), "sha256": exp.sha256(path), "size": path.stat().st_size}
            payload = Path(files[role]["path"]).read_bytes()
            return exp.native.Binding(key, files["raw"], files["injected"], payload, role,
                                      [{"path": key + "/src/main.cpp", "sha256": "c" * 64, "size": 8}])

        for module, name, options in ((exp, "replay_entrant", {"side_effect": entrant}),
                                      (exp.native, "_capture", {"side_effect": capture}),
                                      (exp.native, "_dependencies", {"side_effect": lambda: (self.root / "cli.exe", self.native_engine, copy.deepcopy(self.dependencies))}),
                                      (exp, "engine_wasm", {"return_value": self.engine}),
                                      (core, "toolchain", {"return_value": "tool"}),
                                      (core, "build", {"side_effect": AssertionError("unexpected build")}),
                                      (core, "prepare", {"side_effect": AssertionError("unexpected prepare")})):
            mock = patch.object(module, name, **options)
            mock.start(); self.addCleanup(mock.stop)
        self.plays = []
        self.play_mock = patch.object(exp.native, "play", side_effect=self.play)
        self.native_play = self.play_mock.start(); self.addCleanup(self.play_mock.stop)
        exp.write_spec(self.directory, self.spec)

    def args(self, subject="first", opponent="anchors/ref", side="A", role="injected"):
        args = ReplayIsolation.args(self, subject, opponent, side)
        args.native_role = role
        return args

    def play(self, a, b, map_file, out, *, role, jobs, timeout):
        self.assertTrue(self.local.locked, "native worker slots require the replay case lock")
        self.assertEqual(jobs, 1)
        self.plays.append((a, b, role))
        job = Path(out) / "jobs/game-000000"; job.mkdir(parents=True)
        (job / "output.zip").write_bytes((a + b + role).encode())
        (job / "summary.json").write_text('{"match": "complete"}', encoding="utf-8")
        artifacts = {field: {"path": str(path), "sha256": exp.sha256(path), "size": path.stat().st_size}
                     for field, path in (("archive", job / "output.zip"), ("statistics", job / "summary.json"))}
        return {"selected_role": role, "engine": self.native_engine,
                "dependencies": copy.deepcopy(self.dependencies),
                "bindings": [exp.native._capture(key, role).record() for key in dict.fromkeys((a, b))],
                "execution_pass": True, "health_pass": True, "stopped": True, "jobs": False,
                "unconfirmed_process_pid": None,
                "rows": [{"a": a, "b": b, "map": map_file.stem, "execution_complete": True,
                          "measurement_complete": True, "health": {"A": [], "B": []},
                          "native_match": {"mapHash": exp.sha256(map_file),
                                           "engineBuildHash": self.native_engine["buildHash"],
                                           "engineVersion": self.native_engine["version"]}, **artifacts}]}

    def test_roles_have_distinct_recipes_and_preserve_source_raw_experiment(self):
        before = copy.deepcopy(self.spec)
        rows = self.directory / "rows.jsonl"; rows.write_bytes(b'{"measured": "raw"}\n')
        with redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_replay(self.args(role="raw")), 0)
            self.assertEqual(exp.cmd_replay(self.args()), 0)
            self.assertEqual(exp.cmd_replay(self.args(side="B")), 0)
        bundles = list((self.directory / "replays").iterdir())
        self.assertEqual(len(bundles), 3)
        recipes = [json.loads((p / "recipe.json").read_text()) for p in bundles]
        self.assertEqual({r["transport"] for r in recipes}, {"native-cli"})
        self.assertEqual({r["selected_role"] for r in recipes}, {"raw", "injected"})
        self.assertNotEqual(recipes[0]["bindings"]["subject"]["raw"]["sha256"],
                            recipes[0]["bindings"]["subject"]["injected"]["sha256"])
        self.assertEqual(self.plays[-1][:2], (self.entries["anchors/ref"].key, self.entries["first"].key))
        self.assertEqual(self.spec, before)
        self.assertEqual(rows.read_bytes(), b'{"measured": "raw"}\n')
        self.assertFalse(list((self.directory / "replay-scratch").iterdir()))

    def test_cli_option_is_explicit_and_absent_by_default(self):
        import argparse
        parser = argparse.ArgumentParser()
        exp.register(parser.add_subparsers())
        command = ["exp", "replay", "owner/sample", "map-one", "A", "first", "anchors/ref"]
        self.assertIsNone(parser.parse_args(command).native_role)
        self.assertEqual(parser.parse_args(command + ["--native-role", "injected"]).native_role, "injected")
        self.assertEqual(parser.parse_args(command + ["--native-role", "raw"]).native_role, "raw")

    def test_completed_native_replay_is_reused_and_tampering_fails(self):
        with redirect_stdout(io.StringIO()):
            exp.cmd_replay(self.args()); exp.cmd_replay(self.args())
        self.assertEqual(self.native_play.call_count, 1)
        published = next((self.directory / "replays").iterdir())
        (published / "output.zip").write_bytes(b"changed")
        with self.assertRaisesRegex(core.ArenaError, "bundle bytes changed"):
            exp.cmd_replay(self.args())
        self.assertEqual(self.native_play.call_count, 1)

    def test_native_replay_rejects_unsupported_recipe_features_before_capture(self):
        for feature in ("rounds", "subject-weight", "opponent-weight"):
            with self.subTest(feature=feature):
                self.spec["rounds"] = 2 if feature == "rounds" else 0
                self.spec["subjects"][0]["weights"] = "weights.txt" if feature == "subject-weight" else None
                with patch.object(exp, "opponent_weights", return_value="weights.txt" if feature == "opponent-weight" else None), \
                        self.assertRaisesRegex(core.ArenaError, "full game without"):
                    exp.cmd_replay(self.args())
        self.native_play.assert_not_called()

    def test_missing_injected_module_fails_without_build_or_raw_substitution(self):
        Path(exp.wasm(self.entries["first"])).with_name("bot-injected.wasm").unlink()
        for role in ("raw", "injected"):
            with self.subTest(role=role), self.assertRaisesRegex(core.ArenaError, "cached executable missing"):
                exp.cmd_replay(self.args(role=role))
        self.native_play.assert_not_called()

    def test_changed_selected_inputs_or_runtime_prevent_publication(self):
        original = self.play
        for change in ("map", "dependencies", "binding", "native-map"):
            with self.subTest(change=change):
                def run(*args, **kwargs):
                    result = original(*args, **kwargs)
                    if change == "map": self.map.write_bytes(b"different map")
                    if change == "dependencies": self.dependencies[0]["sha256"] = "d" * 64
                    if change == "binding": result["bindings"][0]["inner_bot_sha256"] = "0" * 64
                    if change == "native-map": result["rows"][0]["native_match"]["mapHash"] = "0" * 64
                    return result
                with patch.object(exp.native, "play", side_effect=run), redirect_stderr(io.StringIO()), \
                        self.assertRaises(core.ArenaError):
                    exp.cmd_replay(self.args())
                self.assertFalse((self.directory / "replays").exists())
                self.map.write_bytes(b"fixed map")
                self.dependencies[0]["sha256"] = "e" * 64

    def test_failed_execution_or_unconfirmed_worker_is_never_published(self):
        for field, value in (("execution_pass", False), ("stopped", False), ("jobs", True),
                             ("unconfirmed_process_pid", 123)):
            with self.subTest(field=field):
                def run(*args, **kwargs):
                    result = self.play(*args, **kwargs); result[field] = value
                    return result
                with patch.object(exp.native, "play", side_effect=run), redirect_stderr(io.StringIO()), \
                        self.assertRaisesRegex(core.ArenaError, "confirmed worker cleanup"):
                    exp.cmd_replay(self.args())
                self.assertFalse((self.directory / "replays").exists())
        self.assertEqual(len(list((self.directory / "replay-scratch").iterdir())), 4)

    def test_selected_outputs_must_match_the_completed_job_descriptors(self):
        def run(*args, **kwargs):
            result = self.play(*args, **kwargs)
            Path(result["rows"][0]["archive"]["path"]).write_bytes(b"changed after completion")
            return result
        with patch.object(exp.native, "play", side_effect=run), redirect_stderr(io.StringIO()), \
                self.assertRaisesRegex(core.ArenaError, "differs from its completed job"):
            exp.cmd_replay(self.args())
        self.assertFalse((self.directory / "replays").exists())

    def test_case_guard_includes_pending_worker_cleanup_and_blocks_close(self):
        cleaning, release, closing = threading.Event(), threading.Event(), threading.Event()
        def cleanup():
            self.assertTrue(self.local.locked)
            cleaning.set(); self.assertTrue(release.wait(timeout=10))
        def close():
            closing.set()
            return exp.cmd_close(SimpleNamespace(name="owner/sample", note="done"))
        with patch.object(exp, "recover_native_cleanup", side_effect=cleanup), \
                redirect_stdout(io.StringIO()), ThreadPoolExecutor(2) as executor:
            replay = executor.submit(exp.cmd_replay, self.args())
            self.assertTrue(cleaning.wait(timeout=5))
            ending = executor.submit(close); self.assertTrue(closing.wait(timeout=5))
            try:
                self.assertFalse(ending.done())
            finally:
                release.set()
            self.assertEqual((replay.result(timeout=10), ending.result(timeout=10)), (0, 0))
        self.assertFalse((self.directory / "replays").exists())


class ReplayProvenance(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/arena-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("selected-provenance-" + uuid.uuid4().hex)
        self.root.mkdir(); self.addCleanup(shutil.rmtree, self.root)
        for module, name, value in ((exp, "ROOT", self.root), (core, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena")):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)
        self.files = {}

        def file(label, relative):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(label.encode())
            self.files[label] = path
            return path

        file("bundle", "docs/reference/current-version.json")
        file("engine", "runtime/engine.wasm"); file("runner", "runtime/fastrun.exe")
        file("library", "runtime/wasmtime.dll"); file("source", "tools/fastrun/fastrun.cpp")
        self.maps = {name: file(name, f"maps/{name}.map26a") for name in ("map-one", "map-two")}
        file("selected-weight", "weights/selected.json"); file("other-weight", "weights/other.json")
        for module, name, options in ((exp, "engine_wasm", {"return_value": self.files["engine"]}),
                                      (exp, "fastrun", {"return_value": self.files["runner"]}),
                                      (core, "toolchain", {"return_value": "tool"}),
                                      (core, "map_path", {"side_effect": self.maps.__getitem__})):
            mock = patch.object(module, name, **options)
            mock.start(); self.addCleanup(mock.stop)
        self.spec = {"name": "owner/selected", "status": "open", "rounds": 0, "sides": "both", "maps": "maps",
                     "subjects": [{"label": "first", "bot": "odd/one", "set": {}, "weights": str(self.files["selected-weight"])},
                                  {"label": "second", "bot": "odd/two", "set": {}, "weights": str(self.files["other-weight"])}],
                     "entrants": {"first": "odd/one@111111111111", "second": "odd/two@222222222222",
                                  "anchors/ref": "anchors/ref@333333333333", "anchors/other": "anchors/other@444444444444"},
                     "opponent_ids": ["anchors/ref", "anchors/other"], "opponents": ["anchors/ref", "anchors/other"], "games": 16}
        self.entries = {}
        for label, key in self.spec["entrants"].items():
            ident, version = key.split("@")
            entry = SimpleNamespace(key=key, path=core.CACHE / "snap" / version / "bots" / ident)
            module = Path(exp.wasm(entry)); module.parent.mkdir(parents=True); module.write_bytes(label.encode())
            self.entries[label] = entry; self.files[label] = module
        self.spec["provenance"] = exp.provenance(self.spec["subjects"], list(self.maps.values()), self.entries)
        self.directory = exp.exp_dir(self.spec["name"]); self.directory.mkdir(parents=True)
        exp.write_spec(self.directory, self.spec)
        self.args = SimpleNamespace(name=self.spec["name"], subject="first", opponent="anchors/ref", map="map-one", side="A")

    def gaps(self, spec=None, maps="map-one"):
        return exp.replay_provenance_gaps(self.spec if spec is None else spec, "first", "anchors/ref", maps)

    def replay(self, mutate=None):
        def run(lines, bots, out, rounds, bundles):
            parts = lines[0].split()
            leaf = Path(bundles) / f"{Path(parts[0]).stem}_{parts[1]}_{parts[-1]}"
            leaf.mkdir(parents=True)
            for name in ("replay.repl26a", "team-a.stdout", "team-b.stdout", "team-a.fuel", "team-b.fuel"):
                (leaf / name).write_bytes(b"complete")
            if mutate is not None: mutate()
            return [{}]

        with patch.object(exp, "load", return_value=(self.directory, self.spec)), \
                patch.object(exp, "replay_entrant", side_effect=lambda spec, label, subject: self.entries[label]), \
                patch.object(exp, "run_games", side_effect=run) as games, redirect_stdout(io.StringIO()):
            result = exp.cmd_replay(self.args)
        return result, games.call_count

    def test_unselected_pruning_allows_replay_without_weakening_global_validation_or_recipe(self):
        self.assertEqual(exp.provenance_gaps(self.spec), [])
        before = copy.deepcopy(self.spec)
        recipe = exp.replay_recipe(self.spec, "first", "anchors/ref", self.maps["map-one"], "A")
        for name in ("second", "anchors/other", "other-weight", "map-two"):
            self.files[name].unlink()
        self.assertTrue(exp.provenance_gaps(self.spec))
        self.assertEqual(self.gaps(), [])
        self.assertEqual(self.spec, before, "selection must not rewrite the broad source experiment")
        self.assertEqual(exp.replay_recipe(self.spec, "first", "anchors/ref", self.maps["map-one"], "A"), recipe)
        self.assertEqual(self.replay(), (0, 1))

    def test_every_requested_map_is_required_for_multi_map_report_scope(self):
        self.assertEqual(self.gaps(maps=["map-one", "map-two"]), [])
        changed = copy.deepcopy(self.spec)
        del changed["provenance"]["maps"]["map-two"]
        self.assertTrue(self.gaps(changed, ["map-one", "map-two"]))
        self.files["map-two"].unlink()
        self.assertTrue(self.gaps(maps=["map-one", "map-two"]))
        self.assertEqual(self.gaps(), [])

    def test_missing_or_changed_selected_inputs_and_missing_pins_fail_closed(self):
        for name in ("first", "anchors/ref", "map-one", "selected-weight"):
            with self.subTest(name=name):
                path = self.files[name]; contents = path.read_bytes()
                path.unlink(); self.assertTrue(self.gaps())
                path.write_bytes(b"changed"); self.assertTrue(self.gaps())
                path.write_bytes(contents); self.assertEqual(self.gaps(), [])
        for category, label in (("wasm", "first"), ("wasm", "anchors/ref"), ("maps", "map-one"), ("weights", "first")):
            with self.subTest(pin=(category, label)):
                changed = copy.deepcopy(self.spec)
                del changed["provenance"][category][label]
                self.assertTrue(self.gaps(changed))

    def test_all_shared_runtime_bindings_remain_required(self):
        for name in ("bundle", "engine", "runner", "library", "source"):
            with self.subTest(runtime=name):
                path = self.files[name]; contents = path.read_bytes()
                path.write_bytes(b"changed runtime"); self.assertTrue(self.gaps())
                path.write_bytes(contents); self.assertEqual(self.gaps(), [])
        with patch.object(core, "toolchain", return_value="changed toolchain"):
            self.assertTrue(self.gaps())
        for name in ("bundle", "engine", "runner", "runner_libraries", "toolchain"):
            with self.subTest(missing_runtime_pin=name):
                changed = copy.deepcopy(self.spec); del changed["provenance"][name]
                self.assertTrue(self.gaps(changed))

    def test_actual_selected_weight_path_must_match_pinned_bytes_before_launch(self):
        other = self.root / "weights/changed-selection.json"; other.write_bytes(b"different weights")
        self.spec["subjects"][0]["weights"] = str(other)
        exp.write_spec(self.directory, self.spec)
        self.assertEqual(exp.provenance_gaps(self.spec), [], "old recorded weight path still exists")
        self.assertTrue(self.gaps())
        with self.assertRaisesRegex(core.ArenaError, "cannot replay changed inputs"):
            self.replay()

    def test_unrelated_pruning_during_execution_does_not_block_publication(self):
        def prune():
            for name in ("second", "anchors/other", "other-weight", "map-two"):
                self.files[name].unlink()
        self.assertEqual(self.replay(prune), (0, 1))
        self.assertTrue(list((self.directory / "replays").iterdir()))
        self.assertTrue(exp.provenance_gaps(self.spec))

    def test_selected_or_shared_runtime_mutation_during_execution_blocks_publication(self):
        for name in ("first", "anchors/ref", "map-one", "selected-weight", "engine"):
            with self.subTest(name=name):
                path = self.files[name]; contents = path.read_bytes()
                with self.assertRaisesRegex(core.ArenaError, "inputs changed"):
                    self.replay(lambda: path.write_bytes(b"changed during replay"))
                self.assertFalse((self.directory / "replays").exists())
                self.assertFalse(list((self.directory / "replay-scratch").iterdir()))
                path.write_bytes(contents)


class Resume(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/arena-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("resume-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        for module, name, value in ((exp, "ROOT", self.root), (core, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena")):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)
        self.spec = {"name": "owner/resumable", "question": "recover exact inputs", "status": "open", "created": "synthetic",
                     "preparation": {"state": "complete"}, "subjects": [
                         {"label": "base", "bot": "odd/one", "set": {}, "weights": None}],
                     "opponents": ["anchors/ref"], "opponent_ids": ["anchors/ref"],
                     "entrants": {"base": "odd/one@aaaaaaaaaaaa", "anchors/ref": "anchors/ref@bbbbbbbbbbbb"},
                     "maps": "two-recorded-maps", "sides": "both", "rounds": 0, "games": 4, "keep": "none",
                     "provenance": {"maps": {"one": "1" * 64, "two": "2" * 64}, "bundle": "bundle",
                                    "engine": "engine", "runner": "runner", "runner_source": "source",
                                    "toolchain": "toolchain", "runner_libraries": {}, "weights": {},
                                    "wasm": {"base": "a" * 64, "anchors/ref": "b" * 64}}}
        self.directory = exp.exp_dir(self.spec["name"])
        self.directory.mkdir(parents=True)
        exp.write_spec(self.directory, self.spec)
        self.args = SimpleNamespace(name=self.spec["name"], progress_from=None)
        mock = patch.object(core, "map_path", side_effect=lambda name: self.root / (name + ".map26a"))
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "provenance_gaps", return_value=[])
        self.gaps = mock.start(); self.addCleanup(mock.stop)
        self.real_entrant = exp.replay_entrant
        mock = patch.object(exp, "replay_entrant", side_effect=lambda spec, label, subject, **kw:
                            SimpleNamespace(path=self.root / label))
        self.entrant = mock.start(); self.addCleanup(mock.stop)
        for name in ("prepare", "prepare_many", "select", "version", "map_set"):
            mock = patch.object(core, name, side_effect=AssertionError("resume must not use current sources/selectors"))
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(exp, "show")
        mock.start(); self.addCleanup(mock.stop)
        self.games, self.keys = exp.game_plan(self.spec)
        self.raw = self.raw_for(self.games)
        self.rows = exp.normalized_chunk(self.spec, self.games, self.raw)

    def test_resume_validates_recorded_overridden_opponent_without_current_selection_or_build(self):
        settings = {"FIRST": "1", "SECOND": "1", "THIRD": "1"}
        source = self.root / "bots/anchors/ref/src/main.cpp"
        contents = b"#define FIRST 1\n#define SECOND 1\n#define THIRD 1\n"
        version = core.version_of({source: contents}, settings)
        snap = core.CACHE / "snap" / version
        target = snap / "bots/anchors/ref"
        (target / "src").mkdir(parents=True)
        (target / "src/main.cpp").write_bytes(contents)
        module = target / ".cambc/build-release/bot.wasm"
        module.parent.mkdir(parents=True); module.write_bytes(b"captured overridden opponent")
        (snap / "meta.json").write_text(json.dumps({"bot": "anchors/ref", "version": version, "overrides": settings}))
        (snap / ".ready").touch(); (snap / ".toolchain").write_text("toolchain")
        self.spec["entrants"]["anchors/ref"] = f"anchors/ref@{version}"
        self.spec["provenance"]["wasm"]["anchors/ref"] = exp.sha256(module)
        self.spec["opponent_descriptors"] = {"anchors/ref": {"bot": "anchors/ref", "set": settings, "weights": None}}
        exp.write_spec(self.directory, self.spec)
        self.progress(self.rows)

        def entrant(spec, label, subject, **kwargs):
            self.assertEqual(kwargs, {"cached_only": True})
            if label == "anchors/ref":
                self.assertIsNone(subject)
                ent = self.real_entrant(spec, label, subject, **kwargs)
                self.assertEqual(ent.overrides, tuple(sorted(settings.items())))
                return ent
            return SimpleNamespace(path=self.root / label)

        self.entrant.side_effect = entrant
        with patch.object(core, "toolchain", return_value="toolchain"), patch.object(exp, "run_games") as games, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        games.assert_not_called()
        self.assertEqual(exp.read_rows(self.directory / "rows.jsonl"), self.rows)

    def test_opponent_descriptors_bind_new_progress_without_changing_legacy_fingerprints(self):
        fields = ("subjects", "entrants", "opponent_ids", "opponents", "maps", "sides", "rounds", "games", "provenance")
        old = hashlib.sha256(json.dumps({k: self.spec.get(k) for k in fields}, sort_keys=True,
                                       separators=(",", ":"), allow_nan=False).encode("utf-8")).hexdigest()
        self.assertEqual(exp.progress_fingerprint(self.spec), old)
        self.spec["opponent_descriptors"] = {"anchors/ref": {"bot": "anchors/ref", "set": {}, "weights": None}}
        self.assertNotEqual(exp.progress_fingerprint(self.spec), old)
        path = self.progress(self.rows)
        self.spec["opponent_descriptors"]["anchors/ref"]["set"] = {"FIRST": "1"}
        with self.assertRaisesRegex(core.ArenaError, "fingerprint"):
            exp.read_progress(path, self.spec, set(self.keys))

    def raw_for(self, games):
        return [{"map": Path(f[0]).stem, "a_side": f[1], "tag": f[6], "winner": "a", "rounds": 100,
                 "gems": [1000, 800], "exhausted": [0, 0], "faults": [0, 0], "errors": [0, 0]}
                for f in (game.split() for game in games)]

    def progress(self, rows, path=None):
        path = path or self.directory / "progress.json"
        exp.write_document(path, {"schema": "arena-experiment-progress/v1",
                                 "fingerprint": exp.progress_fingerprint(self.spec), "rows": rows})
        return path

    def test_interrupted_chunks_checkpoint_before_raw_cleanup_and_resume_only_missing(self):
        calls = []
        def native(argv, cwd, directory, timeout):
            listing = Path(argv[argv.index("--games") + 1])
            lines = listing.read_text().splitlines()
            calls.append(lines)
            if len(calls) == 2:
                raise KeyboardInterrupt("bounded interruption")
            destination = Path(argv[argv.index("--out") + 1])
            destination.write_text("".join(json.dumps(r) + "\n" for r in self.raw_for(lines)))
            return {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True}
        with patch.object(exp, "CHUNK", 2), patch.object(exp, "engine_wasm", return_value=self.root / "engine"), \
                patch.object(exp, "fastrun", return_value=self.root / "runner"), \
                patch.object(core, "Slots") as slots, patch.object(exp.native, "_command", side_effect=native):
            slots.return_value.count = 1
            with self.assertRaises(KeyboardInterrupt):
                exp.execute_prepared(self.directory, self.spec, self.games, {})
        saved = exp.read_progress(self.directory / "progress.json", self.spec, set(self.keys))
        self.assertEqual(set(saved), set(self.keys[:2]))
        self.assertFalse((self.directory / "run.rows0.jsonl").exists())
        self.assertFalse((self.directory / "rows.jsonl").exists())
        def remaining(games, bots, out, rounds=0, *, on_chunk):
            self.assertEqual(games, self.games[2:])
            raw = self.raw_for(games)
            on_chunk(games, raw)
            return raw
        with patch.object(exp, "run_games", side_effect=remaining), redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        self.assertEqual(exp.read_rows(self.directory / "rows.jsonl"), self.rows)
        self.assertTrue(all(call.kwargs == {"cached_only": True} for call in self.entrant.call_args_list))

    def test_import_saved_unhealthy_outcome_without_replaying_it(self):
        imported = copy.deepcopy(self.rows)
        imported[0]["health"] = {"faults": 1}
        imported[1]["opp_health"] = {"errors": 2}
        imported[2]["truncated"] = True
        self.args.progress_from = str(self.progress(imported, self.root / "observer.json"))
        self.spec["keep"] = "flagged"
        exp.write_spec(self.directory, self.spec)
        with patch.object(exp, "run_games") as games, redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        games.assert_not_called()
        self.assertEqual(exp.read_rows(self.directory / "rows.jsonl"), imported)

    def test_progress_input_drift_and_runtime_drift_rejected(self):
        self.progress(self.rows[:1])
        for field, value in (("rounds", 99), ("sides", "A"), ("subjects", []), ("entrants", {})):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.spec); changed[field] = value
                with self.assertRaisesRegex(core.ArenaError, "fingerprint"):
                    exp.read_progress(self.directory / "progress.json", changed, set(self.keys))
        self.gaps.return_value = ["runner identity changed"]
        with patch.object(exp, "run_games") as games, self.assertRaisesRegex(core.ArenaError, "changed inputs"):
            exp.cmd_resume(self.args)
        games.assert_not_called(); self.entrant.assert_not_called()

    def test_runtime_drift_at_later_checkpoint_preserves_only_verified_chunk(self):
        self.gaps.side_effect = [[], ["map identity changed: two"]]
        def fake(games, bots, out, rounds=0, *, on_chunk):
            on_chunk(games[:2], self.raw_for(games[:2]))
            on_chunk(games[2:], self.raw_for(games[2:]))
            return self.raw_for(games)
        with patch.object(exp, "run_games", side_effect=fake), self.assertRaisesRegex(core.ArenaError, "inputs changed"):
            exp.execute_prepared(self.directory, self.spec, self.games, {})
        saved = exp.read_progress(self.directory / "progress.json", self.spec, set(self.keys))
        self.assertEqual(set(saved), set(self.keys[:2]))
        self.assertFalse((self.directory / "rows.jsonl").exists())

    def test_wrong_duplicate_torn_or_invalid_progress_never_launches_games(self):
        wrong = copy.deepcopy(self.rows[:1]); wrong[0]["opponent"] = "anchors/wrong"
        bad_type = copy.deepcopy(self.rows[:1]); bad_type[0]["rounds"] = True
        bad_health = copy.deepcopy(self.rows[:1]); bad_health[0]["health"] = {"errors": -1}
        bad_damage = copy.deepcopy(self.rows[:1]); bad_damage[0]["champion_damage"] = float("inf")
        for rows in (wrong, [self.rows[0], self.rows[0]], bad_type, bad_health, bad_damage):
            with self.subTest(rows=rows):
                (self.directory / "progress.json").write_text(json.dumps({"schema": "arena-experiment-progress/v1",
                    "fingerprint": exp.progress_fingerprint(self.spec), "rows": rows}))
                with patch.object(exp, "run_games") as games, self.assertRaises(core.ArenaError):
                    exp.cmd_resume(self.args)
                games.assert_not_called()
        (self.directory / "progress.json").write_text('{"schema":')
        with patch.object(exp, "run_games") as games, self.assertRaisesRegex(core.ArenaError, "invalid progress"):
            exp.cmd_resume(self.args)
        games.assert_not_called()

    def test_import_conflict_and_duplicates_are_not_silently_deduplicated(self):
        self.progress(self.rows[:1])
        conflict = copy.deepcopy(self.rows[:1]); conflict[0]["gems"] += 1
        self.args.progress_from = str(self.progress(conflict, self.root / "observer.json"))
        with self.assertRaisesRegex(core.ArenaError, "conflicting"):
            exp.cmd_resume(self.args)

    def test_complete_canonical_panel_is_noop_and_missing_summary_regenerated(self):
        exp.write_document(self.directory / "rows.jsonl", "".join(json.dumps(r) + "\n" for r in self.rows))
        exp.write_document(self.directory / "summary.json", exp.summarize(self.spec, self.rows))
        before = {p.name: p.read_bytes() for p in self.directory.iterdir() if p.is_file()}
        with patch.object(exp, "run_games") as games, redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        games.assert_not_called()
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir() if p.is_file()})
        (self.directory / "summary.json").unlink()
        with patch.object(exp, "run_games") as games, redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        games.assert_not_called()
        self.assertEqual(exp.read_spec(self.directory / "summary.json"), exp.summarize(self.spec, self.rows))
        (self.directory / "summary.json").write_text('{"torn":')
        with patch.object(exp, "run_games") as games, redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        games.assert_not_called()
        self.assertEqual(exp.read_spec(self.directory / "summary.json"), exp.summarize(self.spec, self.rows))

    def test_active_writer_excludes_resume_close_and_force_and_lock_survives_cleanup(self):
        with exp.operation_lock(self.directory), ThreadPoolExecutor(1) as executor:
            for command, args in ((exp.cmd_resume, self.args),
                                  (exp.cmd_close, SimpleNamespace(name=self.args.name, note="done")),
                                  (exp.cmd_run, SimpleNamespace(name=self.args.name, force=True))):
                with self.assertRaisesRegex(core.ArenaError, "timed out waiting"):
                    executor.submit(command, args).result(timeout=3)
        with redirect_stdout(io.StringIO()):
            exp.cmd_close(SimpleNamespace(name=self.args.name, note="bounded synthetic experiment"))
        self.assertTrue(exp.operation_lock(self.directory).path.is_file())

    def test_closed_unprepared_and_missing_provenance_are_rejected(self):
        for change in ({"status": "closed"}, {"preparation": {"state": "started"}},
                       {"provenance": {"maps": {"one": "pin"}}}):
            spec = copy.deepcopy(self.spec); spec.update(change)
            exp.write_spec(self.directory, spec)
            with self.assertRaises(core.ArenaError):
                exp.cmd_resume(self.args)

    def test_force_waits_for_active_replay_before_replacing_tree(self):
        entered = threading.Event()
        def build(spec):
            entered.set()
            raise core.ArenaError("stop after safe replacement")
        args = SimpleNamespace(name=self.args.name, force=True, subject=["base=odd/one"], subjects=None,
                               vs=["anchors/ref"], question="replace", maps="one", sides="both", rounds=0, keep="none")
        with ThreadPoolExecutor(1) as executor, patch.object(exp, "build", side_effect=build):
            with exp.replay_case_lock(self.directory, "f" * 32):
                future = executor.submit(exp.cmd_run, args)
                self.assertFalse(entered.wait(timeout=.1))
                self.assertEqual(exp.read_spec(self.directory / "spec.json"), self.spec)
            with self.assertRaisesRegex(core.ArenaError, "safe replacement"):
                future.result(timeout=5)
        self.assertTrue(entered.is_set())

    def test_wrong_or_duplicate_returned_keys_block_checkpoint_and_preserve_raw(self):
        for raw in ([self.raw[0], self.raw[0], *self.raw[2:]],
                    [{**self.raw[0], "map": "wrong"}, *self.raw[1:]]):
            def fake(games, bots, out, rounds=0, *, on_chunk):
                on_chunk(games, raw)
                return raw
            with patch.object(exp, "run_games", side_effect=fake), self.assertRaises(core.ArenaError):
                exp.execute_prepared(self.directory, self.spec, self.games, {})
            self.assertFalse((self.directory / "progress.json").exists())

    def test_native_interrupt_retains_exact_job_slots_and_writer_lock_until_empty(self):
        original = KeyboardInterrupt("parent interrupted with live descendants")
        entered, allow = threading.Event(), threading.Event()
        state = SimpleNamespace(held=False, exits=0, confirms=0, process=None, slots=None, stops=0, worker_id=None)
        sleep = exp.time.sleep
        def repeated_cancel(seconds):
            if threading.get_ident() == state.worker_id:
                raise KeyboardInterrupt("repeated cancellation")
            sleep(seconds)
        case = self
        class HeldSlots:
            count = 2
            def __init__(self, want, **kwargs):
                case.assertEqual((want, kwargs), (4, {"minimum": 1, "poll": .05, "quiet": True}))
                case.assertIsNone(state.slots)
                state.slots = self
            def __enter__(self):
                state.held = True
                return self
            def __exit__(self, *args):
                case.assertTrue(state.process.empty_confirmed)
                state.held = False; state.exits += 1
        class OwnedProcess:
            pid, assigned, returncode = 99901, True, None
            empty_confirmed, forced, closed = False, False, False
            def __init__(self, argv, cwd, out, err):
                case.assertTrue(state.held)
                case.assertEqual(argv[-2:], ["--procs", "2"])
                out.write(b"retained raw stdout"); err.write(b"retained raw stderr")
                state.process = self
                state.worker_id = threading.get_ident()
            def wait(self, timeout):
                case.assertEqual(timeout, 7200)
                raise original
            def terminate_tree(self):
                state.stops += 1
                if not allow.is_set():
                    raise OSError("descendants not yet confirmed empty")
                self.empty_confirmed, self.forced, self.returncode = True, True, 130
            def close(self):
                case.assertTrue(self.empty_confirmed)
                self.closed = True
            def record(self):
                return {"initial_pid": self.pid, "empty_confirmed": self.empty_confirmed,
                        "forced_termination": self.forced, "kind": "fake-owned-job"}
        confirm = exp.native.confirm_pending_cleanup
        def recovery():
            state.confirms += 1
            pending = exp.native._PENDING_CLEANUP
            self.assertEqual(len(pending), 1)
            self.assertIs(pending[0]["process"], state.process)
            self.assertIs(pending[0]["slots"], state.slots)
            self.assertIs(pending[0]["error"], original)
            self.assertTrue(state.held)
            if state.confirms == 1:
                entered.set()
                self.assertTrue(allow.wait(timeout=5))
                return False
            return confirm()
        self.assertFalse(exp.native._PENDING_CLEANUP)
        with patch.object(exp, "engine_wasm", return_value=self.root / "engine"), \
                patch.object(exp, "fastrun", return_value=self.root / "runner"), \
                patch.object(core, "Slots", HeldSlots), patch.object(exp.native, "_WinJobProcess", OwnedProcess), \
                patch.object(exp.native, "confirm_pending_cleanup", side_effect=recovery), \
                patch.object(exp.time, "sleep", side_effect=repeated_cancel), \
                ThreadPoolExecutor(1) as executor:
            future = executor.submit(exp.cmd_resume, self.args)
            try:
                self.assertTrue(entered.wait(timeout=5))
                self.assertTrue(state.held)
                self.assertFalse(future.done())
                self.assertEqual(state.exits, 0)
                with self.assertRaisesRegex(core.ArenaError, "timed out waiting"):
                    with exp.operation_lock(self.directory):
                        self.fail("writer ownership escaped before Job cleanup")
            finally:
                allow.set()
            with self.assertRaises(KeyboardInterrupt) as caught:
                future.result(timeout=5)
            self.assertIs(caught.exception, original)
        self.assertEqual((state.confirms, state.stops, state.exits), (2, 2, 1))
        self.assertTrue(state.process.empty_confirmed and state.process.closed)
        self.assertFalse(state.held or exp.native._PENDING_CLEANUP)
        self.assertFalse((self.directory / "rows.jsonl").exists())
        self.assertFalse((self.directory / "progress.json").exists())
        transport, = (self.directory / ".native-commands").iterdir()
        self.assertEqual((transport / "stdout.bin").read_bytes(), b"retained raw stdout")
        self.assertFalse(original.compiled_process_record["tree_exit_confirmed"])
        self.assertTrue(exp.read_spec(transport / "cleanup-confirmed.json")["empty_confirmed"])
        with exp.operation_lock(self.directory):
            pass


class Identity(unittest.TestCase):
    def setUp(self):
        scratch = core.ROOT / ".cache/arena-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.root = scratch / ("exp-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        for module, name, value in ((exp, "ROOT", self.root), (core, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena")):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)

    def test_game_batch_requests_only_useful_capacity_and_respects_granted_workers(self):
        for game_count, wanted, granted in ((1, 1, 1), (3, 3, 1), (4, 4, 1), (5, 5, 4), (40, 32, 5)):
            with self.subTest(games=game_count):
                def run(argv, cwd, directory, timeout):
                    destination = Path(argv[argv.index("--out") + 1])
                    destination.write_text("".join(json.dumps({"tag": f"game-{i}"}) + "\n"
                                                   for i in range(game_count)), encoding="utf-8")
                    self.assertEqual(argv[argv.index("--procs") + 1], str(granted))
                    return {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True}

                with patch.object(exp, "engine_wasm", return_value=self.root / "engine.wasm"), \
                        patch.object(exp, "fastrun", return_value=self.root / "fastrun.exe"), \
                        patch.object(core, "slot_count", return_value=32), \
                        patch.object(core, "Slots") as slots, \
                        patch.object(exp.native, "_command", side_effect=run):
                    slots.return_value.count = granted
                    rows = exp.run_games([f"map A a - b - game-{i}" for i in range(game_count)],
                                         {"a": "one.wasm", "b": "two.wasm"}, self.root / "batch")
                slots.assert_called_once_with(wanted, minimum=1 if game_count <= 4 else min(4, wanted),
                                              poll=0.05 if game_count <= 4 else None, quiet=True)
                self.assertEqual([r["tag"] for r in rows], [f"game-{i}" for i in range(game_count)])
                self.assertFalse(list(self.root.glob("batch.*")), "temporary batch files must be removed")

    def test_strict_rows_reject_torn_or_nonobject_lines(self):
        path = self.root / "rows.jsonl"
        path.write_text('{}\n{"torn":', encoding="utf-8")
        with self.assertRaisesRegex(core.ArenaError, "row 2"):
            exp.read_rows(path)
        path.write_text('[]\n', encoding="utf-8")
        with self.assertRaisesRegex(core.ArenaError, "not an object"):
            exp.read_rows(path)

    def test_failed_checkpoint_keeps_raw_chunk_for_recovery(self):
        def run(argv, cwd, directory, timeout):
            destination = Path(argv[argv.index("--out") + 1])
            destination.write_text('{"tag":"complete"}\n', encoding="utf-8")
            return {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True}
        def checkpoint(part, got):
            self.assertTrue((self.root / "batch.rows0.jsonl").is_file())
            raise OSError("controlled checkpoint publication failure")
        with patch.object(exp, "engine_wasm", return_value=self.root / "engine"), \
                patch.object(exp, "fastrun", return_value=self.root / "runner"), \
                patch.object(core, "Slots") as slots, patch.object(exp.native, "_command", side_effect=run):
            slots.return_value.count = 1
            with self.assertRaisesRegex(OSError, "publication failure"):
                exp.run_games(["map A a - b - complete"], {}, self.root / "batch", on_chunk=checkpoint)
        self.assertTrue((self.root / "batch.rows0.jsonl").is_file())
        self.assertTrue((self.root / "batch.games0.txt").is_file())

    def test_legacy_slotted_helper_defaults_and_callable_timeout_are_unchanged(self):
        receipt = {"exact": "unchanged"}
        with patch.object(core, "Slots") as slots, patch.object(exp.native, "_command", return_value=receipt) as command:
            slots.return_value.count = 2
            got = exp.native._slotted_command(lambda count: ["program", str(count)], self.root,
                                              self.root, lambda count: 90 + count, jobs=3)
        self.assertIs(got, receipt)
        slots.assert_called_once_with(3, quiet=True)
        slots.return_value.__enter__.assert_called_once_with()
        slots.return_value.__exit__.assert_called_once_with(None, None, None)
        command.assert_called_once_with(["program", "2"], self.root, self.root, 92)

    def test_native_transport_preserves_exact_argv_and_uses_one_slot_admission(self):
        target, engine, runner = self.root / "exact", self.root / "engine", self.root / "fastrun"
        bundles = self.root / "bundles"
        game = "map B a weights.json b - exact-tag"
        raw = {"tag": "exact-tag"}
        def command(argv, cwd, directory, timeout):
            self.assertEqual(argv, [str(runner), "--engine", str(engine), "--games", str(target.with_suffix(".games0.txt")),
                                   "--out", str(target.with_suffix(".rows0.jsonl")), "--bot", "a=one.wasm", "--bot", "b=two.wasm",
                                   "--rounds", "29", "--bundles", str(bundles), "--procs", "1"])
            self.assertEqual((cwd, timeout), (self.root, 7200))
            self.assertEqual(target.with_suffix(".games0.txt").read_text(), game + "\n")
            target.with_suffix(".rows0.jsonl").write_text(json.dumps(raw) + "\n")
            return {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True, "forced_termination": False}
        with patch.object(exp, "engine_wasm", return_value=engine), patch.object(exp, "fastrun", return_value=runner), \
                patch.object(core, "Slots") as slots, patch.object(exp.native, "_command", side_effect=command):
            slots.return_value.count = 1
            self.assertEqual(exp.run_games([game], {"a": "one.wasm", "b": "two.wasm"}, target, rounds=29, bundles=bundles), [raw])
        slots.assert_called_once_with(1, minimum=1, poll=.05, quiet=True)
        slots.return_value.__enter__.assert_called_once_with()
        slots.return_value.__exit__.assert_called_once_with(None, None, None)

    def test_nonzero_timeout_and_forced_tree_cleanup_preserve_receipts_and_raw_without_checkpoint(self):
        case = self
        for label in ("nonzero", "timeout", "forced-zero"):
            with self.subTest(kind=label):
                class Process:
                    pid, assigned, empty_confirmed, forced = 99902, True, False, False
                    returncode = None
                    def __init__(self, argv, cwd, out, err):
                        self.argv = argv
                        destination = Path(argv[argv.index("--out") + 1])
                        destination.write_text('{"torn":' if label == "timeout" else '{"tag":"complete"}\n')
                        err.write(b"exact native diagnostic")
                    def wait(self, timeout):
                        case.assertEqual(timeout, 7200)
                        if label == "timeout":
                            raise subprocess.TimeoutExpired(self.argv, timeout)
                        self.returncode = 7 if label == "nonzero" else 0
                        self.empty_confirmed, self.forced = True, label == "forced-zero"
                        return self.returncode
                    def terminate_tree(self):
                        self.empty_confirmed, self.forced, self.returncode = True, True, 1
                    def close(self):
                        case.assertTrue(self.empty_confirmed)
                    def record(self):
                        return {"empty_confirmed": self.empty_confirmed, "forced_termination": self.forced}
                target = self.root / label
                with patch.object(exp, "engine_wasm", return_value=self.root / "engine"), \
                        patch.object(exp, "fastrun", return_value=self.root / "runner"), \
                        patch.object(core, "Slots") as slots, patch.object(exp.native, "_WinJobProcess", Process), \
                        patch.object(exp, "recover_native_cleanup", wraps=exp.recover_native_cleanup), \
                        patch.object(exp, "normalized_chunk") as checkpoint:
                    slots.return_value.count = 1
                    with self.assertRaisesRegex(core.ArenaError, "exact native diagnostic") as caught:
                        exp.run_games(["map A a - b - complete"], {}, target, on_chunk=checkpoint)
                receipt = caught.exception.compiled_process_record
                self.assertTrue(receipt["tree_exit_confirmed"])
                self.assertEqual(receipt["timed_out"], label == "timeout")
                self.assertEqual(receipt["returncode"], {"nonzero": 7, "timeout": 1, "forced-zero": 0}[label])
                checkpoint.assert_not_called()
                slots.assert_called_once_with(1, minimum=1, poll=.05, quiet=True)
                slots.return_value.__exit__.assert_called_once_with(None, None, None)
                self.assertTrue(target.with_suffix(".rows0.jsonl").is_file())
                self.assertTrue(target.with_suffix(".games0.txt").is_file())
                self.assertFalse(exp.native._PENDING_CLEANUP)

    def test_unavailable_historical_snapshot_never_rebuilds_current_drift(self):
        spec = {"entrants": {"one": "odd/one@aaaaaaaaaaaa"}}
        with patch.object(core, "version", return_value="bbbbbbbbbbbb"), patch.object(core, "prepare") as prepare:
            with self.assertRaisesRegex(core.ArenaError, "sources drifted"):
                exp.replay_entrant(spec, "one", {"bot": "odd/one", "set": {}})
            prepare.assert_not_called()

    def test_resume_missing_snapshot_never_checks_or_builds_current_sources(self):
        spec = {"entrants": {"one": "odd/one@aaaaaaaaaaaa"}}
        with patch.object(core, "version") as version, patch.object(core, "prepare") as prepare:
            with self.assertRaisesRegex(core.ArenaError, "resume never builds"):
                exp.replay_entrant(spec, "one", {"bot": "odd/one", "set": {}}, cached_only=True)
            version.assert_not_called(); prepare.assert_not_called()

    def snapshot(self, overrides=None):
        source = self.root / "bots/odd/one/src/main.cpp"
        contents = b"old source\n" + "".join(f"#define {k} {v}\n" for k, v in (overrides or {}).items()).encode()
        version = core.version_of({source: contents}, overrides)
        snap = core.CACHE / "snap" / version
        target = snap / "bots/odd/one"
        (target / "src").mkdir(parents=True)
        (target / "src/main.cpp").write_bytes(contents)
        (target / ".cambc/build-release").mkdir(parents=True)
        (target / ".cambc/build-release/bot.wasm").write_bytes(b"exact binary")
        (snap / "meta.json").write_text(json.dumps({"bot": "odd/one", "version": version, "overrides": overrides or {}}))
        (snap / ".ready").touch()
        (snap / ".toolchain").write_text("pinned")
        return snap, {"entrants": {"one": f"odd/one@{version}"}}

    def opponent_snapshot(self, settings):
        snap, spec = self.snapshot(settings)
        spec["entrants"]["odd/one"] = spec["entrants"].pop("one")
        spec["opponent_ids"] = ["odd/one"]
        spec["provenance"] = {"wasm": {"odd/one": exp.sha256(snap / "bots/odd/one/.cambc/build-release/bot.wasm")}}
        return snap, spec

    def test_cached_opponent_infers_exact_three_overrides_without_current_sources_or_build(self):
        settings = {"FIRST": "1", "SECOND": "1", "THIRD": "1"}
        _, spec = self.opponent_snapshot(settings)
        before = copy.deepcopy(spec)
        with patch.object(core, "toolchain", return_value="pinned"), \
                patch.object(core, "version", side_effect=AssertionError("no current source")), \
                patch.object(core, "select", side_effect=AssertionError("no current selector")), \
                patch.object(core, "prepare", side_effect=AssertionError("no rebuild")):
            ent = exp.replay_entrant(spec, "odd/one", None, cached_only=True)
        self.assertEqual(ent.key, spec["entrants"]["odd/one"])
        self.assertEqual(ent.overrides, tuple(sorted(settings.items())))
        self.assertEqual(spec, before, "legacy inference must remain ephemeral")

    def test_cached_default_opponent_keeps_empty_recorded_settings(self):
        _, spec = self.opponent_snapshot({})
        with patch.object(core, "toolchain", return_value="pinned"), \
                patch.object(core, "prepare", side_effect=AssertionError("no rebuild")):
            ent = exp.replay_entrant(spec, "odd/one", None, cached_only=True)
        self.assertEqual(ent.key, spec["entrants"]["odd/one"])
        self.assertEqual(ent.overrides, ())

    def test_explicit_and_legacy_opponent_recipes_validate_but_never_fall_back_on_mismatch(self):
        settings = {"FIRST": "1", "SECOND": "1", "THIRD": "1"}
        _, spec = self.opponent_snapshot(settings)
        spec["legacy_opponent_overrides"] = {"odd/one": settings}
        with patch.object(core, "toolchain", return_value="pinned"):
            self.assertEqual(exp.replay_entrant(spec, "odd/one", None).overrides, tuple(sorted(settings.items())))
            spec["opponent_descriptors"] = {"odd/one": {"bot": "odd/one", "set": settings, "weights": None}}
            self.assertEqual(exp.replay_entrant(spec, "odd/one", None).overrides, tuple(sorted(settings.items())))
            spec["legacy_opponent_overrides"] = {"odd/one": {"FIRST": "0"}}
            with self.assertRaisesRegex(core.ArenaError, "conflicting"):
                exp.replay_entrant(spec, "odd/one", None)
            del spec["legacy_opponent_overrides"]
            for descriptor in ({"bot": "odd/one", "set": {}, "weights": None},
                               {"bot": "odd/other", "set": settings},
                               {"bot": "odd/one", "set": settings, "weights": "unexpected"},
                               {"bot": "odd/one", "set": {"FIRST": 1}}, None):
                with self.subTest(descriptor=descriptor):
                    spec["opponent_descriptors"]["odd/one"] = descriptor
                    with self.assertRaises(core.ArenaError):
                        exp.replay_entrant(spec, "odd/one", None)

    def test_inferred_missing_opponent_recipe_never_selects_or_builds_defaults(self):
        spec = {"entrants": {"odd/one": "odd/one@aaaaaaaaaaaa"}}
        with patch.object(core, "version", side_effect=AssertionError("no current source")), \
                patch.object(core, "select", side_effect=AssertionError("no current selector")), \
                patch.object(core, "prepare", side_effect=AssertionError("no rebuild")):
            with self.assertRaisesRegex(core.ArenaError, "recipe unavailable"):
                exp.replay_entrant(spec, "odd/one", None)

    def test_cached_overridden_opponent_tampering_and_missing_bindings_fail_closed(self):
        cases = ("metadata", "overrides", "override-type", "source", "wasm", "missing-wasm",
                 "toolchain", "ready", "missing-meta", "missing-pin")
        with patch.object(core, "toolchain", return_value="pinned"), \
                patch.object(core, "version", side_effect=AssertionError("no current source")), \
                patch.object(core, "select", side_effect=AssertionError("no current selector")), \
                patch.object(core, "prepare", side_effect=AssertionError("no rebuild")):
            for index, case in enumerate(cases):
                with self.subTest(case=case):
                    settings = {"FIRST": "1", "SECOND": "1", "THIRD": str(index)}
                    snap, spec = self.opponent_snapshot(settings)
                    meta = json.loads((snap / "meta.json").read_text())
                    if case in ("metadata", "overrides", "override-type"):
                        if case == "metadata": meta["bot"] = "odd/other"
                        else: meta["overrides"]["FIRST"] = "changed" if case == "overrides" else 1
                        (snap / "meta.json").write_text(json.dumps(meta))
                    elif case == "source": (snap / "bots/odd/one/src/main.cpp").write_text("changed source")
                    elif case == "wasm": (snap / "bots/odd/one/.cambc/build-release/bot.wasm").write_bytes(b"changed")
                    elif case == "missing-wasm": (snap / "bots/odd/one/.cambc/build-release/bot.wasm").unlink()
                    elif case == "toolchain": (snap / ".toolchain").write_text("changed")
                    elif case == "ready": (snap / ".ready").unlink()
                    elif case == "missing-meta": (snap / "meta.json").unlink()
                    else: spec["provenance"]["wasm"] = {}
                    with self.assertRaises(core.ArenaError):
                        exp.replay_entrant(spec, "odd/one", None, cached_only=True)

    def test_cached_historical_closure_replays_without_reading_current_source_or_building(self):
        snap, spec = self.snapshot()
        with patch.object(core, "toolchain", return_value="pinned"), patch.object(core, "version") as current, \
                patch.object(core, "prepare") as prepare:
            ent = exp.replay_entrant(spec, "one", {"bot": "odd/one", "set": {}})
            self.assertEqual(ent.key, spec["entrants"]["one"])
            current.assert_not_called(); prepare.assert_not_called()

    def test_modified_snapshot_closure_fails_instead_of_relabeling(self):
        snap, spec = self.snapshot()
        (snap / "bots/odd/one/src/main.cpp").write_text("changed")
        with patch.object(core, "toolchain", return_value="pinned"):
            with self.assertRaisesRegex(core.ArenaError, "source digest mismatch"):
                exp.replay_entrant(spec, "one", {"bot": "odd/one", "set": {}})

    def test_legacy_gap_and_pinned_engine_map_runner_drift(self):
        self.assertIn("legacy", exp.provenance_gaps({})[0])
        files = {}
        for name in ("engine", "runner", "wasmtime.dll", "map", "docs/reference/current-version.json"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
            files[name] = path
        spec = {"subjects": [], "entrants": {}, "provenance": {
            "bundle": exp.sha256(files["docs/reference/current-version.json"]),
            "engine": exp.sha256(files["engine"]), "runner": exp.sha256(files["runner"]),
            "runner_libraries": {"wasmtime.dll": exp.sha256(files["wasmtime.dll"])},
            "toolchain": "tool", "maps": {"map-one": exp.sha256(files["map"])}, "wasm": {}, "weights": {}}}
        with patch.object(exp, "engine_wasm", return_value=files["engine"]), \
                patch.object(exp, "fastrun", return_value=files["runner"]), \
                patch.object(core, "toolchain", return_value="tool"), patch.object(core, "map_path", return_value=files["map"]):
            self.assertEqual(exp.provenance_gaps(spec), [])
            files["map"].write_text("new map")
            files["engine"].write_text("new engine")
            files["runner"].write_text("new runner")
            files["wasmtime.dll"].write_text("new runtime")
            gaps = exp.provenance_gaps(spec)
            self.assertTrue(any("engine" in gap for gap in gaps))
            self.assertTrue(any("runner" in gap for gap in gaps))
            self.assertTrue(any("runner_libraries" in gap for gap in gaps))
            self.assertTrue(any("map identity" in gap for gap in gaps))

    def test_new_provenance_binds_actual_binary_weights_and_runner_bytes(self):
        files = {}
        for name in ("engine", "runner", "wasmtime.dll", "map-one.map26a", "weights.txt", "tools/fastrun/fastrun.cpp",
                     "docs/reference/current-version.json", "bot/.cambc/build-release/bot.wasm"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
            files[name] = path
        with patch.object(exp, "engine_wasm", return_value=files["engine"]), \
                patch.object(exp, "fastrun", return_value=files["runner"]), \
                patch.object(core, "toolchain", return_value="tool"):
            record = exp.provenance([{"label": "one", "weights": "weights.txt"}],
                                    [files["map-one.map26a"]], {"one": SimpleNamespace(path=self.root / "bot")})
        self.assertEqual(record["runner"], exp.sha256(files["runner"]))
        self.assertEqual(record["runner_libraries"], {"wasmtime.dll": exp.sha256(files["wasmtime.dll"])})
        self.assertEqual(record["runner_source"], exp.sha256(files["tools/fastrun/fastrun.cpp"]))
        self.assertEqual(record["wasm"]["one"], exp.sha256(files["bot/.cambc/build-release/bot.wasm"]))
        self.assertEqual(record["weights"]["one"]["sha256"], exp.sha256(files["weights.txt"]))
        self.assertEqual(record["maps"], {"map-one": exp.sha256(files["map-one.map26a"])})


class DynamicCurrentPool(unittest.TestCase):
    def setUp(self):
        self.f=WinFloor('test_complete_current_default_healthy_win_passes_only_measured_floor')
        self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.base_row=copy.deepcopy(self.f.rows[1])

    def resize(self,count):
        f=self.f
        f.pool[:]=[f'pool-{i:02}' for i in range(count)]
        f.spec['provenance']['maps']=dict.fromkeys(f.pool,'pinned')
        f.spec['games']=2*count
        template=copy.deepcopy(self.base_row)
        f.rows=[dict(template,map=name,side=side) for name in f.pool for side in ('A','B')]
        if f.rows:f.rows[0].update(result='win',gems=1000,opp_gems=800)

    def test_seventy_maps_require_one_hundred_forty_unique_keys(self):
        self.resize(70);report=self.f.audit();floor=report['bots'][0]['win_floor']
        self.assertTrue(report['win_floor']['all_passed'])
        self.assertEqual((floor['required_games'],floor['wins'],floor['losses']),(140,1,139))
        self.assertEqual(report['win_floor']['required_games_per_bot'],140)
        self.assertEqual(len(report['win_floor']['required_maps']),70)
        self.assertEqual(report['bots'][0]['contribution'],'unverified')

    def test_old_forty_map_panel_is_historical_under_expanded_pool(self):
        f=self.f
        old_spec,old_rows=copy.deepcopy(f.spec),copy.deepcopy(f.rows)
        f.pool.extend(f'pool-{i:02}' for i in range(40,70))
        report=f.audit();floor=report['bots'][0]['win_floor']
        self.assertEqual(floor['required_games'],140)
        self.assertFalse(floor['qualified'])
        self.assertTrue(any('70 current pool maps' in r for r in floor['reasons']))
        self.assertTrue(any('140 unique' in r for r in floor['reasons']))
        self.assertEqual(f.spec,old_spec);self.assertEqual(f.rows,old_rows)

    def test_empty_or_duplicate_current_pool_fails_closed(self):
        for pool in ([],['same','same']):
            with self.subTest(pool=pool),patch.object(core,'map_set',return_value=pool):
                floor=self.f.floor()
                self.assertFalse(floor['qualified'])
                self.assertTrue(any('nonempty and unique' in r for r in floor['reasons']))

    def test_seventy_map_missing_duplicate_wrong_map_and_one_side_rejected(self):
        for case in ('missing','duplicate','wrong-map','one-side'):
            with self.subTest(case=case):
                self.resize(70);f=self.f
                if case=='missing':f.rows.pop()
                elif case=='duplicate':f.rows[-1]=dict(f.rows[-2])
                elif case=='wrong-map':
                    f.spec['provenance']['maps'].pop(f.pool[-1])
                    f.spec['provenance']['maps']['unlisted']='pinned'
                    for row in f.rows:
                        if row['map']==f.pool[-1]:row['map']='unlisted'
                else:
                    f.spec['sides']='A';f.spec['games']=70
                    f.rows=[row for row in f.rows if row['side']=='A']
                self.assertFalse(f.floor()['qualified'])

    def test_seventy_map_health_draw_and_diagnostic_bars_preserved(self):
        for case in ('subject-health','opponent-health','draws','private','private-roots','truncated','round-cap','nondefault'):
            with self.subTest(case=case):
                self.resize(70);f=self.f
                f.spec.pop('private_snapshots',None)
                f.spec['provenance'].pop('snapshot_roots',None)
                f.spec['rounds']=0;f.spec['subjects'][0]['set']={}
                if case=='subject-health':f.rows[1]['health']={'exhausted':1}
                elif case=='opponent-health':f.rows[0]['opp_health']={'errors':1}
                elif case=='draws':
                    for row in f.rows:row['result']='draw'
                elif case=='private':f.spec['private_snapshots']=True
                elif case=='private-roots':f.spec['provenance']['snapshot_roots']={}
                elif case=='truncated':f.rows[1]['truncated']=True
                elif case=='round-cap':f.spec['rounds']=100
                else:f.spec['subjects'][0]['set']={'KNOB':'1'}
                self.assertFalse(f.floor()['qualified'])



if __name__ == "__main__":
    unittest.main()
