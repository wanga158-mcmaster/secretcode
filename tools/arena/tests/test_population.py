"""Deterministic population semantics; no builds, games, slots or online access."""
from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import population as p
from core import ArenaError, Bot, Entrant


class Evidence(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.ident = {"snapshot": "odd/loser@aaaaaaaaaaaa", "source": {"bots/odd/loser/src/main.cpp": "hash"},
                      "contract": "contract", "toolchain": "tool", "bundle": "bundle", "engine": {"version": "1"},
                      "tests": {"bots/odd/tests/run.py": "testhash"}, "observers": {"observer.py": "obshash"},
                      "maps": {"map-one": "maphash1", "map-two": "maphash2"}}
        self.opponents = ["anchors/frozen@bbbbbbbbbbbb", "other/healthy@cccccccccccc"]
        self.contract = {k: "declared" for k in p.CONTRACT_FIELDS}
        self.contract.update(id="odd-loser", bot="odd/loser")
        self.rows = [self.row(o, m, s) for o in self.opponents for m in self.ident["maps"] for s in "AB"]
        self.test_record = {"exit_code": 0, "test_cases": 2, "snapshot": self.ident["snapshot"],
                            "identity": p.canonical(self.ident), "runner": "bots/odd/tests/run.py"}
        self.evidence = {"schema": 1, "behavior": "odd-loser", "identity": self.ident,
                         "build": {"status": "pass", "snapshot": self.ident["snapshot"]},
                         "tests": {"status": "pass", "artifact": "tests"},
                         "health": {"artifact": "games", "opponents": self.opponents, "maps": self.ident["maps"]}, "artifacts": {}}
        self.artifact("tests", json.dumps(self.test_record))
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        self.artifact("native", "native replay fixture")
        self.observed = {
            "positive": {"status": "pass", "artifacts": ["native"], "opportunities": 2,
                         "activations": 2, "continuations": 2, "condition": {"map": "map-one", "map_sha256": "maphash1", "opponent_snapshot": self.opponents[0]}},
            "refusal": {"status": "pass", "artifacts": ["native"], "refusals": 1},
            "distinct": {"status": "pass", "artifacts": ["native"], "physical_difference": "retains bank timing",
                         "replacement_removes_coverage": True, "control_snapshot": "odd/control@dddddddddddd",
                         "neighbor_snapshot": "odd/neighbor@eeeeeeeeeeee"},
            "fresh": {"status": "pass", "artifacts": ["native"], "opportunities": 1,
                      "activations": 1, "continuations": 1, "condition": {"map": "map-two", "map_sha256": "maphash2", "opponent_snapshot": self.opponents[1]}}}
        for target, value in (("ROOT", self.root), ("CACHE", self.root / ".cache/arena")):
            patcher = patch.object(p, target, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = patch.object(p, "identity", return_value=self.ident)
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = patch.object(p, "_snapshot", side_effect=lambda key: SimpleNamespace(key=key))
        self.snapshot = patcher.start()
        self.addCleanup(patcher.stop)
        patcher = patch.object(p, "_snapshot_files", return_value=self.ident["source"])
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = patch.object(p.core, "bot", side_effect=lambda key: Bot(*key.split("/")))
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = patch.object(p, "_adapter", return_value=SimpleNamespace(evaluate=lambda *a, **k: self.observed))
        self.adapter = patcher.start()
        self.addCleanup(patcher.stop)

    def row(self, opponent, name, side):
        other = "B" if side == "A" else "A"
        metrics = {"clears": 1, "faults": 0, "exhausted": 0, "errors": 0, "fuel_max": 1000, "fuel_at_budget": 0}
        return {side.lower(): self.ident["snapshot"], other.lower(): opponent, "map": name,
                "winner": other, "engine": "1", "rounds": 2000, "A": dict(metrics), "B": dict(metrics)}

    def artifact(self, name, text):
        path = self.root / (name + ".txt")
        path.write_text(text, encoding="utf-8")
        self.evidence["artifacts"][name] = {"path": path.name, "sha256": p.digest(path)}

    def assess(self):
        return p.assess(self.contract, self.evidence)

    def test_healthy_losing_peculiar_bot_passes(self):
        result = self.assess()
        self.assertTrue(result["ready"], result)
        self.assertEqual(result["state"], "ready")
        self.assertEqual(result["health"]["covered"], 8)

    def test_explicit_variant_uses_the_same_full_admission_requirements(self):
        self.contract["overrides"] = {"REPAIR_ENABLED": "1"}
        self.ident.update(overrides={"REPAIR_ENABLED": "1"}, working_source={"bots/odd/loser/src/main.cpp": "rawhash"})
        self.snapshot.side_effect = lambda key: SimpleNamespace(key=key, overrides=(("REPAIR_ENABLED", "1"),))
        self.test_record["identity"] = p.canonical(self.ident)
        self.artifact("tests", json.dumps(self.test_record))
        self.assertTrue(self.assess()["ready"])
        self.rows[0]["A"]["exhausted"] = 1
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        self.assertFalse(self.assess()["ready"])

    def test_missing_test_step_does_not_pass(self):
        for status in (None, "skip", "part", "fail"):
            with self.subTest(status=status):
                self.evidence["tests"]["status"] = status
                self.assertFalse(self.assess()["ready"])

    def test_false_pass_or_other_revision_tests_do_not_pass(self):
        for field, value in (("exit_code", 1), ("test_cases", 0), ("identity", "old"), ("snapshot", "other@key"), ("runner", "fake.py")):
            with self.subTest(field=field):
                record = dict(self.test_record)
                record[field] = value
                self.artifact("tests", json.dumps(record))
                self.assertFalse(self.assess()["checks"]["tests"])

    def test_missing_and_stale_artifacts_do_not_pass(self):
        path = self.root / "native.txt"
        path.write_text("changed", encoding="utf-8")
        self.assertEqual(self.assess()["state"], "stale")
        path.unlink()
        self.assertFalse(self.assess()["ready"])

    def test_missing_evidence_is_unassessed(self):
        result = p.assess(self.contract, self.root / "not-there.json")
        self.assertEqual(result["state"], "unassessed")
        self.assertFalse(result["ready"])

    def test_every_identity_dimension_invalidates_evidence(self):
        for key in self.ident:
            with self.subTest(key=key):
                evidence = copy.deepcopy(self.evidence)
                evidence["identity"][key] = "changed"
                self.assertEqual(p.assess(self.contract, evidence)["state"], "stale")

    def test_unexecuted_proposal_is_rejected(self):
        self.observed["positive"]["activations"] = 0
        self.assertFalse(self.assess()["ready"])

    def test_missing_continuation_and_refusal_rejected(self):
        self.observed["positive"]["continuations"] = 0
        self.observed["refusal"]["refusals"] = 0
        result = self.assess()
        self.assertFalse(result["checks"]["positive"])
        self.assertFalse(result["checks"]["refusal"])

    def test_duplicate_has_no_distinct_contribution(self):
        self.observed["distinct"]["replacement_removes_coverage"] = False
        self.assertFalse(self.assess()["checks"]["distinct"])

    def test_missing_control_snapshot_rejected(self):
        self.snapshot.side_effect = ArenaError("missing immutable control")
        self.assertFalse(self.assess()["ready"])

    def test_fresh_means_new_conditions(self):
        self.observed["fresh"]["condition"] = self.observed["positive"]["condition"]
        self.assertFalse(self.assess()["checks"]["fresh"])

    def test_round_only_freshness_or_wrong_map_hash_rejected(self):
        self.observed["fresh"]["condition"] = dict(self.observed["positive"]["condition"], round=99)
        self.assertFalse(self.assess()["checks"]["fresh"])
        self.observed["fresh"]["condition"]["map_sha256"] = "wrong"
        self.assertFalse(self.assess()["checks"]["fresh"])

    def test_diagnostic_summary_without_evaluator_is_not_admission(self):
        self.adapter.return_value = SimpleNamespace(observe=lambda *a, **kw: {"activations": 100})
        self.assertFalse(self.assess()["ready"])

    def test_fault_exhaustion_error_and_stall_block_admission(self):
        for field, value in (("faults", 1), ("exhausted", 1), ("errors", 1), ("clears", 0), ("fuel_at_budget", 1)):
            with self.subTest(field=field):
                rows = copy.deepcopy(self.rows)
                rows[0]["A"][field] = value
                self.artifact("games", "\n".join(json.dumps(r) for r in rows))
                self.assertFalse(self.assess()["checks"]["health"])

    def test_both_sides_all_pool_maps_required(self):
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows[:-1]))
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertEqual(result["health"]["covered"], 7)

    def test_truncation_or_other_engine_is_not_health(self):
        for field, value in (("truncated", True), ("engine", "old"), ("winner", "error")):
            with self.subTest(field=field):
                rows = copy.deepcopy(self.rows)
                rows[0][field] = value
                self.artifact("games", "\n".join(json.dumps(r) for r in rows))
                self.assertFalse(self.assess()["checks"]["health"])

    def test_opponent_failure_separate_and_blocks_panel(self):
        self.rows[0]["B"]["faults"] = 1
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertEqual(len(result["health"]["opponent_faults"]), 1)

    def test_frozen_zero_clear_is_reported_without_subject_failure(self):
        self.rows[0]["B"]["clears"] = 0
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        result = self.assess()
        self.assertTrue(result["ready"], result)
        flag = result["health"]["opponent_faults"][0]
        self.assertEqual(flag["problems"], ["stall"])
        self.assertTrue(flag["reference_only_stall"])
        self.assertFalse(flag["blocks"])

    def test_cross_suite_zero_clear_still_blocks_admission(self):
        self.rows[4]["B"]["clears"] = 0
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertTrue(result["health"]["opponent_faults"][0]["blocks"])

    def test_frozen_stall_with_technical_failure_still_blocks(self):
        self.rows[0]["B"].update(clears=0, exhausted=1)
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertTrue(result["health"]["opponent_faults"][0]["blocks"])

    def test_frozen_stall_cannot_replace_healthy_cross_suite_coverage(self):
        rows = copy.deepcopy(self.rows[:4])
        rows[0]["B"]["clears"] = 0
        result = p.health_assessment(rows, self.ident, self.opponents[:1])
        self.assertFalse(result["pass"])
        self.assertIn("healthy cross-suite opponent", " ".join(result["gaps"]))

    def test_malformed_evidence_fails_without_crashing(self):
        for value in ([], "text", 7, None):
            if value is None:
                continue
            with self.subTest(value=value):
                self.assertFalse(p.assess(self.contract, value)["ready"])
        for field in ("build", "tests", "health", "artifacts"):
            value = copy.deepcopy(self.evidence)
            value[field] = ["invalid"]
            self.assertFalse(p.assess(self.contract, value)["ready"])
        self.artifact("tests", "[]")
        self.assertFalse(self.assess()["ready"])

    def test_missing_opponent_health_field_and_negative_metric_rejected(self):
        rows = copy.deepcopy(self.rows)
        del rows[0]["B"]["faults"]
        self.artifact("games", "\n".join(json.dumps(r) for r in rows))
        self.assertFalse(self.assess()["checks"]["health"])
        rows[0]["B"]["faults"] = -1
        self.artifact("games", "\n".join(json.dumps(r) for r in rows))
        self.assertFalse(self.assess()["checks"]["health"])

    def test_health_map_hash_binding_required(self):
        del self.evidence["health"]["maps"]
        self.assertFalse(self.assess()["checks"]["health"])

    def test_observer_boolean_cannot_replace_measured_counts(self):
        self.observed["positive"]["activations"] = True
        self.assertFalse(self.assess()["checks"]["positive"])

    def test_malformed_observer_roles_rejected(self):
        self.observed["positive"] = ["not a role"]
        self.observed["refusal"]["artifacts"] = [{}]
        self.assertFalse(self.assess()["ready"])

    def test_native_diagnostic_recomputes_rows_instead_of_trusting_summary(self):
        self.rows[0]["A"]["exhausted"] = 5
        self.artifact("games", "\n".join(json.dumps(r) for r in self.rows))
        record = {"snapshot": self.ident["snapshot"], "engine": "1", "bundle_sha256": "bundle",
                  "map_hashes": self.ident["maps"], "rows": self.evidence["artifacts"]["games"], "exhausted": 0}
        p._write(self.root / "diagnostic.json", record)
        self.contract["health_evidence"] = "diagnostic.json"
        observed = p.diagnostic_health(self.contract, self.ident)
        self.assertEqual(observed["status"], "current")
        self.assertEqual(observed["health"]["exhausted"], 5)
        self.assertEqual(observed["health"]["games"], 8)

    def test_stale_diagnostic_cannot_supply_current_health(self):
        record = {"snapshot": "odd/loser@old", "engine": "1", "bundle_sha256": "bundle"}
        p._write(self.root / "diagnostic.json", record)
        self.contract["health_evidence"] = "diagnostic.json"
        self.assertEqual(p.diagnostic_health(self.contract, self.ident)["status"], "unavailable")

    def mixed_log(self, selected=None, explicit=True):
        selected = copy.deepcopy(self.rows if selected is None else selected)
        others = copy.deepcopy(self.rows)
        for row in others:
            for side in "AB":
                if row.get(side.lower()) == self.ident["snapshot"]:
                    row[side.lower()] = "odd/loser@ffffffffffff"
                    row[side].update(exhausted=50, errors=2, clears=0)
        self.artifact("games", "\n".join(json.dumps(row) for row in selected + others))
        if explicit:
            self.evidence["artifacts"]["games"]["subject_snapshot"] = self.ident["snapshot"]
        return selected + others

    def diagnostic(self):
        record = {"snapshot": self.ident["snapshot"], "engine": "1", "bundle_sha256": "bundle",
                  "map_hashes": self.ident["maps"], "rows": self.evidence["artifacts"]["games"]}
        p._write(self.root / "diagnostic.json", record)
        self.contract["health_evidence"] = "diagnostic.json"
        return p.diagnostic_health(self.contract, self.ident)

    def test_mixed_health_log_selects_exact_variant_and_reports_full_file_provenance(self):
        self.mixed_log()
        result = self.assess()
        self.assertTrue(result["ready"], result)
        self.assertEqual(result["health"]["games"], 8)
        selection = result["health"]["selection"]
        self.assertEqual(selection["mode"], "exact-subject")
        self.assertEqual((selection["total_rows"], selection["included_rows"], selection["excluded_rows"]), (16, 8, 8))
        self.assertEqual(selection["subject_snapshot"], self.ident["snapshot"])
        self.assertEqual(selection["artifact"]["sha256"], p.digest(self.root / "games.txt"))
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "current", diagnostic)
        self.assertEqual(diagnostic["health"]["games"], 8)
        self.assertEqual(diagnostic["health"]["exhausted"], 0)
        self.assertEqual(diagnostic["provenance"]["selection"], selection)

    def test_selected_failure_survives_clean_duplicate_and_misleading_tags(self):
        selected = copy.deepcopy(self.rows)
        clean_duplicate = copy.deepcopy(selected[1])
        selected[1]["B"]["exhausted"] = 3
        selected[1]["tags"] = {"subject": "A"}  # physical snapshot is B
        self.mixed_log(selected + [clean_duplicate])
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertEqual(result["health"]["games"], 9)
        self.assertIn("exhausted 3", " ".join(result["health"]["gaps"]))
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "current")
        self.assertEqual(diagnostic["health"]["exhausted"], 3)

    def test_absent_subject_selector_preserves_strict_all_rows_rejection(self):
        self.mixed_log(explicit=False)
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertIn("another subject snapshot", " ".join(result["health"]["gaps"]))
        selection = result["health"]["selection"]
        self.assertEqual(selection["mode"], "all-rows")
        self.assertEqual((selection["included_rows"], selection["excluded_rows"]), (16, 0))
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "unavailable")
        self.assertIn("subject mismatch", diagnostic["reason"])

    def test_wrong_or_null_selector_cannot_select_a_clean_other_variant(self):
        for selector in (None, [], True, "odd/loser", "odd/loser@ffffffffffff"):
            with self.subTest(selector=selector):
                self.mixed_log()
                self.evidence["artifacts"]["games"]["subject_snapshot"] = selector
                result = self.assess()
                self.assertFalse(result["checks"]["health"])
                self.assertIn("must exactly equal", " ".join(result["gaps"]))
                diagnostic = self.diagnostic()
                self.assertEqual(diagnostic["status"], "unavailable")
                self.assertIn("must exactly equal", diagnostic["reason"])

    def test_empty_exact_subject_selection_fails_and_reports_exclusions(self):
        self.mixed_log(selected=[])
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        selection = result["health"]["selection"]
        self.assertEqual((selection["total_rows"], selection["included_rows"], selection["excluded_rows"]), (8, 0, 8))
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "unavailable")
        self.assertIn("no selected games", diagnostic["reason"])
        self.assertEqual(diagnostic["selection"], selection)

    def test_excluded_variants_cannot_fill_missing_health_coverage(self):
        self.mixed_log(selected=self.rows[:-1])
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertEqual(result["health"]["covered"], 7)
        self.assertEqual(result["health"]["required"], 8)
        self.assertEqual(result["health"]["selection"]["excluded_rows"], 8)

    def test_selected_bad_rows_are_never_filtered_out(self):
        for field, value in (("winner", "error"), ("truncated", True), ("engine", "old")):
            with self.subTest(field=field):
                selected = copy.deepcopy(self.rows)
                selected[0][field] = value
                self.mixed_log(selected=selected)
                self.assertFalse(self.assess()["checks"]["health"])

    def test_full_log_hash_includes_excluded_variants(self):
        self.mixed_log()
        path = self.root / "games.txt"
        path.write_text(path.read_text(encoding="utf-8").replace('"exhausted": 50', '"exhausted": 51'), encoding="utf-8")
        self.assertEqual(self.assess()["state"], "stale")
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "unavailable")
        self.assertIn("missing or changed artifact", diagnostic["reason"])

    def test_malformed_excluded_rows_fail_even_with_a_correct_selector(self):
        for bad in ("null", "{bad json"):
            with self.subTest(bad=bad):
                rows = self.mixed_log()
                self.artifact("games", "\n".join(json.dumps(row) for row in rows) + "\n" + bad)
                self.evidence["artifacts"]["games"]["subject_snapshot"] = self.ident["snapshot"]
                self.assertFalse(self.assess()["checks"]["health"])
                self.assertEqual(self.diagnostic()["status"], "unavailable")

    def test_exact_subject_self_play_is_rejected_instead_of_hiding_second_side_failure(self):
        selected = copy.deepcopy(self.rows)
        selected[0]["b"] = self.ident["snapshot"]
        selected[0]["B"]["exhausted"] = 10
        self.mixed_log(selected=selected)
        result = self.assess()
        self.assertFalse(result["checks"]["health"])
        self.assertIn("ambiguous for self-play", " ".join(result["gaps"]))
        diagnostic = self.diagnostic()
        self.assertEqual(diagnostic["status"], "unavailable")
        self.assertIn("ambiguous for self-play", diagnostic["reason"])


class Infrastructure(unittest.TestCase):
    def test_inventory_includes_unknowns_and_excludes_protected_suites(self):
        bots = [Bot("odd", "one"), Bot("heuristics", "satisficer"),
                Bot("main", "main"), Bot("MAIN", "main"), Bot("anchors", "ref"), Bot("ANCHORS", "ref"),
                Bot("surprise", "private"), Bot("SURPRISE", "private"),
                Bot("contenders", "economy"), Bot("CONTENDERS", "economy")]
        with patch.object(p.core, "select", return_value=bots), patch.object(p, "_audit", return_value={}), \
                patch.object(p, "_adapter", return_value=None):
            rows = p.contracts()
        self.assertEqual({r["bot"] for r in rows}, {"odd/one", "heuristics/satisficer"})

    def test_no_ready_entries_is_not_silently_experimental(self):
        row = {"bot": "odd/one", "contract": {}, "assessment": {"ready": False}}
        with patch.object(p, "inventory", return_value=[row]):
            with self.assertRaises(ArenaError):
                p.make_panel()

    def test_externally_owned_not_in_experimental_panel(self):
        row = {"bot": "contenders/economy", "contract": {"externally_owned": True}, "assessment": {"ready": False}}
        with patch.object(p, "inventory", return_value=[row]):
            with self.assertRaises(ArenaError):
                p.make_panel(experimental=True)

    def test_direct_excluded_subject_rejected_before_source_read(self):
        for subject in ("main/main", "MAIN/main", "surprise/private", "SURPRISE/private",
                        "contenders/economy", "CONTENDERS/economy"):
            with patch.object(p.core, "bot") as bot, patch.object(p, "validate_panel") as validate:
                with self.assertRaises(ArenaError):
                    p.regress(subject, {}, Path("unused"))
                bot.assert_not_called()
                validate.assert_not_called()

    def test_heuristics_and_anchor_subjects_reach_panel_validation(self):
        for subject in ("heuristics/new", "anchors/ref"):
            with self.subTest(subject=subject), \
                    patch.object(p, "validate_panel", side_effect=ArenaError("allowed panel lookup")) as validate, \
                    patch.object(p.core, "bot") as lookup:
                with self.assertRaisesRegex(ArenaError, "allowed panel lookup"):
                    p.regress(subject, {}, Path("unused"))
                validate.assert_called_once_with({})
                lookup.assert_not_called()

    def test_excluded_panel_creation_rejected_before_source_lookup(self):
        for bot in ("main/x", "MAIN/x", "surprise/x", "SURPRISE/x",
                    "contenders/x", "CONTENDERS/x", "anchors/x", "ANCHORS/x"):
            row = {"bot": bot, "contract": {}, "assessment": {"ready": False}, "identity": {}}
            with self.subTest(bot=bot), patch.object(p, "inventory", return_value=[row]), \
                    patch.object(p.core, "bot", side_effect=AssertionError("excluded source lookup")) as lookup:
                with self.assertRaisesRegex(ArenaError, "outside the population mandate"):
                    p.make_panel(experimental=True)
                lookup.assert_not_called()

    def test_loaded_excluded_opponent_rejected_before_snapshot_read(self):
        for bot in ("main/x", "MAIN/x", "surprise/x", "SURPRISE/x",
                    "contenders/x", "CONTENDERS/x", "anchors/x", "ANCHORS/x"):
            key = bot + "@" + "a" * 12
            contract = {"bot": bot, "id": "excluded"}
            entry = {"bot": bot, "snapshot": key, "behavior": "excluded", "contract": contract,
                     "source": {}, "identity": {"snapshot": key, "contract": p.canonical(contract), "source": {}}}
            panel = {"schema": 1, "kind": "population-panel", "entries": [entry],
                     "toolchain": "tool", "bundle": "bundle"}
            with self.subTest(bot=bot), patch.object(p.core, "toolchain", return_value="tool"), \
                    patch.object(p, "digest", return_value="bundle"), \
                    patch.object(p, "_snapshot", side_effect=AssertionError("excluded snapshot read")) as snapshot:
                with self.assertRaisesRegex(ArenaError, "outside the population mandate"):
                    p.validate_panel(panel)
                snapshot.assert_not_called()

    def test_excluded_snapshot_rejected_before_metadata_read_but_references_are_allowed(self):
        for suite in ("main", "MAIN", "surprise", "SURPRISE", "contenders", "CONTENDERS"):
            with self.subTest(suite=suite), patch.object(p, "_load") as load:
                with self.assertRaisesRegex(ArenaError, "outside the population mandate"):
                    p._snapshot(suite + "/x@0123456789ab")
                load.assert_not_called()
        for suite in ("heuristics", "anchors"):
            with self.subTest(suite=suite), \
                    patch.object(p, "_load", side_effect=ArenaError("allowed metadata lookup")) as load:
                with self.assertRaisesRegex(ArenaError, "allowed metadata lookup"):
                    p._snapshot(suite + "/x@0123456789ab")
                load.assert_called_once()

    def test_heuristics_identity_reaches_source_lookup(self):
        with patch.object(p.core, "bot", side_effect=ArenaError("allowed source lookup")) as lookup:
            with self.assertRaisesRegex(ArenaError, "allowed source lookup"):
                p.identity({"bot": "heuristics/new"})
            lookup.assert_called_once_with("heuristics/new")

    def test_panel_crossfield_mismatch_rejected_before_snapshot_read(self):
        contract = {"id": "odd-one", "bot": "odd/one"}
        key = "odd/one@" + "a" * 12
        base = {"bot": "odd/one", "snapshot": key, "behavior": "odd-one", "contract": contract,
                "identity": {"snapshot": key, "contract": p.canonical(contract)}}
        variants = []
        for field, value in (("bot", "other/two"), ("snapshot", "contenders/economy@" + "a" * 12),
                             ("behavior", "other-behavior")):
            entry = copy.deepcopy(base)
            entry[field] = value
            variants.append(entry)
        for field, value in (("snapshot", "odd/one@" + "b" * 12), ("contract", "wronghash")):
            entry = copy.deepcopy(base)
            entry["identity"][field] = value
            variants.append(entry)
        entry = copy.deepcopy(base)
        entry["contract"]["bot"] = "other/two"
        variants.append(entry)
        for entry in variants:
            with self.subTest(entry=entry), patch.object(p.core, "toolchain", return_value="tool"), \
                    patch.object(p, "digest", return_value="bundle"), patch.object(p, "_snapshot") as snapshot:
                panel = {"schema": 1, "kind": "population-panel", "entries": [entry], "toolchain": "tool", "bundle": "bundle"}
                with self.assertRaises(ArenaError):
                    p.validate_panel(panel)
                snapshot.assert_not_called()

    def test_new_autodiscovered_tests_invalidate_pinned_panel(self):
        for new_test in ("bots/odd/tests/added.cpp", "bots/odd/one/tests/added.cpp"):
            with self.subTest(new_test=new_test), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                existing = root / "bots/odd/tests/run.py"
                existing.parent.mkdir(parents=True)
                existing.write_text("existing test runner", encoding="utf-8")
                contract = {"id": "odd-one", "bot": "odd/one"}
                key = "odd/one@" + "a" * 12
                ent = SimpleNamespace(key=key)
                with patch.object(p, "ROOT", root), patch.object(p.core, "toolchain", return_value="tool"), \
                        patch.object(p, "digest", return_value="hash"), patch.object(p, "_snapshot", return_value=ent), \
                        patch.object(p, "_snapshot_files", return_value={"source": "hash"}), patch.object(p.core, "map_path", return_value=Path("map")), \
                        patch.object(p.core, "read_closure") as source_read:
                    recorded = {"snapshot": key, "contract": p.canonical(contract), "source": {"source": "hash"},
                                "working_source": {"source": "hash"}, "overrides": {}, **p._measurement_files(contract)}
                    entry = {"bot": "odd/one", "snapshot": key, "behavior": "odd-one", "contract": contract,
                             "identity": recorded, "source": {"source": "hash"}, "maps": {"map": "hash"}, "sides": ["A", "B"]}
                    panel = {"schema": 1, "kind": "population-panel", "entries": [entry], "toolchain": "tool", "bundle": "hash"}
                    self.assertEqual(p.validate_panel(panel), [ent])
                    added = root / new_test
                    added.parent.mkdir(parents=True, exist_ok=True)
                    added.write_text("new production fixture", encoding="utf-8")
                    with self.assertRaisesRegex(ArenaError, "tests or observers changed"):
                        p.validate_panel(panel)
                    source_read.assert_not_called()

    def test_panel_outside_durable_tree_rejected_before_build(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(p, "ROOT", Path(temp)), patch.object(p, "make_panel") as build:
            with self.assertRaises(ArenaError):
                p._cmd_panel(SimpleNamespace(out=".cache/temporary-panel.json", select=None, mode="quick", experimental=True))
            build.assert_not_called()

    def test_current_health_failures_triage_repair_but_clean_is_not_admission(self):
        contract = {"id": "odd/one", "bot": "odd/one"}
        for source, failures, state, status in (("a" * 12, 1, "repair", "current"),
                                                ("b" * 12, 1, "unassessed", "historical"),
                                                ("a" * 12, 0, "unassessed", "current")):
            result = {"state": "unassessed", "ready": False, "gaps": [],
                      "identity": {"snapshot": "odd/one@" + "a" * 12, "engine": {"version": "1"}}}
            audit = {"odd/one": {"source_snapshot": source, "observed_health": {"faults": failures}}}
            with patch.object(p, "contracts", return_value=[contract]), patch.object(p, "assess", return_value=result), \
                    patch.object(p, "_audit", return_value=audit), patch.object(p, "_audit_provenance", return_value={"valid": True, "engine": "1"}):
                row = p.inventory()[0]
            self.assertEqual(row["state"], state)
            self.assertEqual(row["audit_observation"]["status"], status)
            self.assertFalse(row["assessment"]["ready"])

    def test_prune_protects_transitive_artifacts_and_snapshots(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / "results/population/evidence/case.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"identity": {"snapshot": "odd/one@aaaaaaaaaaaa"},
                                        "artifacts": {"replay": {"path": ".cache/run/witness.repl26a", "sha256": "x"}},
                                        "health": {"opponents": ["other/two@bbbbbbbbbbbb"]}}), encoding="utf-8")
            with patch.object(p, "ROOT", root), patch.object(p, "CACHE", root / ".cache/arena"):
                protected = p.protected_paths()
            self.assertIn((root / ".cache/arena/snap/aaaaaaaaaaaa").resolve(), protected)
            self.assertIn((root / ".cache/arena/snap/bbbbbbbbbbbb").resolve(), protected)
            self.assertIn((root / ".cache/run/witness.repl26a").resolve(), protected)

    def test_unreadable_durable_record_blocks_prune(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / "results/population/broken.json"
            path.parent.mkdir(parents=True)
            path.write_text("broken", encoding="utf-8")
            with patch.object(p, "ROOT", root), self.assertRaises(ArenaError):
                p.protected_paths()

    def test_declared_official_external_assets_do_not_hide_repo_replay_or_snapshot(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, assets = base / "repo", base / "official-cache"
            official = assets / ("d" * 64) / "visualiser/assets/engine.wasm"
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"officialAssets": {"engine.wasm": {"path": str(official), "sha256": "a" * 64}},
                                        "replay": ".cache/native/witness.repl26a",
                                        "snapshot": "odd/one@aaaaaaaaaaaa"}), encoding="utf-8")
            with patch.object(p, "ROOT", root), patch.object(p, "CACHE", root / ".cache/arena"), \
                    patch.object(p, "OFFICIAL_ASSETS", assets), patch.object(p, "digest") as hash_asset:
                protected = p.protected_paths()
                hash_asset.assert_not_called()
            self.assertNotIn(official.resolve(), protected)
            self.assertIn((root / ".cache/native/witness.repl26a").resolve(), protected)
            self.assertIn((root / ".cache/arena/snap/aaaaaaaaaaaa").resolve(), protected)

    def test_official_external_asset_files_are_not_resolved_or_read_by_prune(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, assets = base / "repo", base / "official-cache"
            official = assets / ("d" * 64) / "visualiser/assets/engine.wasm"
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"officialAssets": {"engine.wasm": {"path": str(official), "sha256": "a" * 64}},
                                        "replay": ".cache/native/witness.repl26a"}), encoding="utf-8")
            resolve = Path.resolve
            def package_redirect(self, *args, **kwargs):
                if self == official:
                    raise AssertionError("external package asset must not be resolved by prune")
                return resolve(self, *args, **kwargs)
            with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), patch.object(Path, "resolve", package_redirect):
                protected = p.protected_paths()
            self.assertIn((root / ".cache/native/witness.repl26a").resolve(), protected)

    def test_packaged_official_module_alias_is_metadata_only(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, local = base / "repo", base / "Local"
            assets = local / "cambc/assets"
            alias = local / "Packages/OpenAI.Codex_2p2nqsd0c76g0/LocalCache/Local/cambc/assets"
            official = alias / ("d" * 64) / "visualiser/assets/engine.wasm"
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"officialAssets": {"engine.wasm": {
                "path": str(official), "sha256": "a" * 64}},
                "replay": ".cache/native/witness.repl26a"}), encoding="utf-8")
            resolve = Path.resolve
            def no_external_resolution(self, *args, **kwargs):
                if self == official:
                    raise AssertionError("package module must not be resolved by prune")
                return resolve(self, *args, **kwargs)
            with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), \
                    patch.object(Path, "resolve", no_external_resolution), patch.object(p, "digest") as hash_asset:
                protected = p.protected_paths()
                hash_asset.assert_not_called()
            self.assertIn((root / ".cache/native/witness.repl26a").resolve(), protected)
            self.assertNotIn(official, protected)

    def test_package_alias_does_not_allow_other_roots_or_ordinary_external_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, local = base / "repo", base / "Local"
            assets = local / "cambc/assets"
            package = local / "Packages/OpenAI.Codex_2p2nqsd0c76g0"
            alias = package / "LocalCache/Local/cambc/assets"
            tail = Path("d" * 64) / "visualiser/assets/engine.wasm"
            official = alias / tail
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            wrong_roots = [
                local / "Packages/OpenAI.Codex_other/LocalCache/Local/cambc/assets",
                local / "Packages/OpenAI.Codex_2p2nqsd0c76g0-evil/LocalCache/Local/cambc/assets",
                package / "LocalCache/Other/cambc/assets",
                package / "LocalCache/Local/other/assets",
                package / "LocalCache/Local/cambc/assets-evil",
                base / "OtherLocal/Packages/OpenAI.Codex_2p2nqsd0c76g0/LocalCache/Local/cambc/assets",
            ]
            for wrong in wrong_roots:
                with self.subTest(root=wrong):
                    path.write_text(json.dumps({"officialAssets": {"engine.wasm": {
                        "path": str(wrong / tail), "sha256": "a" * 64}}}), encoding="utf-8")
                    with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaises(ArenaError):
                        p.protected_paths()
            for field in ("replay", "path", "evidence_path"):
                with self.subTest(field=field):
                    path.write_text(json.dumps({"officialAssets": {"engine.wasm": {
                        "path": str(official), "sha256": "a" * 64}}, field: str(official)}), encoding="utf-8")
                    with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaisesRegex(ArenaError, "outside repository"):
                        p.protected_paths()

    def test_malformed_official_external_descriptors_block_prune(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, assets = base / "repo", base / "official-cache"
            official = assets / ("d" * 64) / "visualiser/assets/engine.wasm"
            descriptor = {"path": str(official), "sha256": "a" * 64}
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            malformed = [None, [], {"engine.wasm": {"path": str(official)}},
                         {"engine.wasm": dict(descriptor, sha256="not-a-hash")},
                         {"engine.wasm": dict(descriptor, replay=str(base / "outside.repl26a"))},
                         {"engine.wasm": dict(descriptor, path="relative/engine.wasm")},
                         {"engine.wasm": dict(descriptor, path=str(base / "outside/engine.wasm"))},
                         {"engine.wasm": dict(descriptor, path=str(base / "official-cache-evil" / ("d" * 64) / "visualiser/assets/engine.wasm"))},
                         {"engine.wasm": dict(descriptor, path=str(assets / ".." / "outside/engine.wasm"))},
                         {"wrong-name.wasm": descriptor},
                         {"witness.repl26a": dict(descriptor, path=str(official.parent / "witness.repl26a"))},
                         {"evidence.json": dict(descriptor, path=str(official.parent / "evidence.json"))},
                         {"games.jsonl": dict(descriptor, path=str(official.parent / "games.jsonl"))},
                         {"engine.wasm:stream": dict(descriptor, path=str(official) + ":stream")},
                         {"../engine.wasm": descriptor},
                         {"engine.wasm": dict(descriptor, path=str(assets / "not-a-release/visualiser/assets/engine.wasm"))},
                         {"engine.wasm": dict(descriptor, path=str(assets / ("d" * 64) / "visualiser/assets/extra/engine.wasm"))},
                         {"engine.wasm": dict(descriptor, path=str(assets / ("d" * 64) / "other/assets/engine.wasm"))}]
            for bad_name in ("engine.wasm:payload.wasm", "engine?.wasm", "engine*.wasm", "engine\x00.wasm",
                             "engine\n.wasm", "engine.wasm.", "engine.wasm ", "engine/other.wasm", "engine\\other.wasm"):
                malformed.append({bad_name: dict(descriptor, path=str(official.parent / bad_name))})
            for value in malformed:
                with self.subTest(value=value):
                    path.write_text(json.dumps({"officialAssets": value}), encoding="utf-8")
                    with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaises(ArenaError):
                        p.protected_paths()

    def test_ordinary_external_replay_or_evidence_still_blocks_prune(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, assets = base / "repo", base / "official-cache"
            official = assets / ("d" * 64) / "visualiser/assets/engine.wasm"
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            for field in ("replay", "path", "evidence_path"):
                with self.subTest(field=field):
                    path.write_text(json.dumps({"officialAssets": {"engine.wasm": {"path": str(official), "sha256": "a" * 64}},
                                                field: str(official)}), encoding="utf-8")
                    with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaisesRegex(ArenaError, "outside repository"):
                        p.protected_paths()

    def test_official_asset_cache_inside_repo_is_still_protected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            assets = root / ".cache/official-cache"
            official = assets / ("d" * 64) / "mapeditor/assets/engine.wasm"
            path = root / "results/population/native/case.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"officialAssets": {"engine.wasm": {"path": str(official), "sha256": "a" * 64}}}), encoding="utf-8")
            with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets):
                self.assertIn(official.resolve(), p.protected_paths())

    def test_quick_keeps_different_behaviors_in_same_suite(self):
        rows = [{"bot": "odd/" + name, "contract": {"id": name, "maps": ["rare-map"]},
                 "assessment": {"ready": False}, "identity": {"snapshot": "odd/" + name + "@" + version,
                                                                    "source": {"source": "hash"}}, "gaps": []}
                for name, version in (("one", "a" * 12), ("two", "b" * 12))]
        def prepare(bot, overrides=None, contents=None):
            return Entrant(bot, "a" * 12 if bot.name == "one" else "b" * 12, Path("unused"), True)
        with patch.object(p, "inventory", return_value=rows), patch.object(p.core, "prepare", side_effect=prepare), \
                patch.object(p.core, "read_closure", return_value={}), patch.object(p, "_source_hashes", return_value={"source": "hash"}), \
                patch.object(p.core, "bot", side_effect=lambda key: Bot(*key.split("/"))), \
                patch.object(p, "identity", side_effect=lambda contract: next(r["identity"] for r in rows if r["contract"] == contract)), \
                patch.object(p, "_snapshot_files", return_value={"source": "hash"}), \
                patch.object(p.core, "pool_maps", return_value=["fallback-one", "fallback-two"]), \
                patch.object(p.core, "map_path", side_effect=Path), patch.object(p, "digest", return_value="hash"), \
                patch.object(p, "_load", return_value={"engine": {"version": "1"}}):
            panel = p.make_panel(experimental=True)
        self.assertEqual(len(panel["entries"]), 2)
        self.assertEqual(list(panel["entries"][0]["maps"]), ["rare-map"])

    def test_missing_pinned_snapshot_never_prepares_working_tree(self):
        panel = {"schema": 1, "kind": "population-panel", "entries": [{"snapshot": "odd/one@" + "a" * 12}],
                 "toolchain": "tool", "bundle": "bundle"}
        with patch.object(p.core, "toolchain", return_value="tool"), patch.object(p, "digest", return_value="bundle"), \
                patch.object(p, "_snapshot", side_effect=ArenaError("snapshot missing")), patch.object(p.core, "prepare") as prepare:
            with self.assertRaises(ArenaError):
                p.validate_panel(panel)
            prepare.assert_not_called()

    def test_regression_unknown_coverage_cannot_pass(self):
        import games
        for observation, expected in (({"status": "unknown"}, False),
                                      ({"verified": True, "opportunities": 1, "activations": 1, "continuations": 1}, True)):
            with self.subTest(observation=observation), tempfile.TemporaryDirectory() as temp:
                opponent = Entrant(Bot("odd", "one"), "a" * 12, Path(temp), True)
                subject = Entrant(Bot("anchors", "ref"), "b" * 12, Path(temp), True)
                entry = {"bot": "odd/one", "behavior": "odd-one", "snapshot": opponent.key,
                         "maps": {"map": "hash"}, "sides": ["A", "B"], "contract": {}, "admitted": False}
                metrics = {"clears": 1, "faults": 0, "exhausted": 0, "errors": 0, "fuel_max": 1000, "fuel_at_budget": 0}
                rows = [{"map": "map", "winner": side, "A": metrics, "B": metrics,
                         "tags": {"subject": side, "population_behavior": "odd-one"}} for side in "AB"]
                with patch.object(p, "validate_panel", return_value=[opponent]), patch.object(p.core, "prepare", return_value=subject), \
                        patch.object(p.core, "bot", return_value=subject.bot), patch.object(games, "play", return_value=rows), \
                        patch.object(p, "_adapter", return_value=SimpleNamespace(observe=lambda *a, **kw: observation)):
                    report = p.regress("anchors/ref", {"entries": [entry]}, Path(temp))
                self.assertTrue(report["execution_pass"])
                self.assertEqual(report["coverage_pass"], expected)
                self.assertEqual(report["pass"], expected)


class OfficialOraclePins(unittest.TestCase):
    def test_known_decision_endpoint_oracle_pin_projection_is_compatible(self):
        # results/population/decision-endpoint/index.json:1716, copied from the
        # personal-camp-oracle dependency record into the retained `pins` list.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            descriptor = {
                "path": str(p.OFFICIAL_ASSETS / "2efb693d1f4306c9339c1429507c5253f7a8ff144a7939ed4dbd83d2ab02b909" /
                            "visualiser/assets/v1_0_3-DRqNt1t2.wasm"),
                "sha256": "029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48",
                "bytes": 638472}
            record = root / "results/population/decision-endpoint/index.json"
            record.parent.mkdir(parents=True)
            record.write_text(json.dumps({"schema": "decision-identity-join-bounded-endpoint/v1",
                                          "oracle": {"pins": [descriptor]},
                                          "snapshot": "decision/job@aaaaaaaaaaaa"}), encoding="utf-8")
            with patch.object(p, "ROOT", root), patch.object(p, "CACHE", root / ".cache/arena"):
                protected = p.protected_paths()
            self.assertIn((root / ".cache/arena/snap/aaaaaaaaaaaa").resolve(), protected)
            self.assertNotIn(Path(descriptor["path"]), protected)

    def test_native_input_and_retained_pin_lists_preserve_repo_inputs_without_external_io(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, local = base / "repo", base / "Local"
            assets = local / "cambc/assets"
            alias = local / "Packages/OpenAI.Codex_2p2nqsd0c76g0/LocalCache/Local/cambc/assets"
            record = root / "results/population/native/case.json"
            record.parent.mkdir(parents=True)
            for field in ("inputs", "pins"):
                for cache in (assets, alias):
                    with self.subTest(field=field, cache=cache):
                        official = cache / ("d" * 64) / "visualiser/assets/engine.wasm"
                        descriptor = {"path": str(official), "sha256": "a" * 64, "bytes": 638472}
                        record.write_text(json.dumps({field: [descriptor, {
                            "path": "tools/observer.mjs", "sha256": "b" * 64, "bytes": 123}],
                            "snapshot": "odd/one@aaaaaaaaaaaa"}), encoding="utf-8")
                        resolve = Path.resolve
                        def no_external_resolution(path, *args, **kwargs):
                            if path == official:
                                raise AssertionError("official oracle input must not be resolved")
                            return resolve(path, *args, **kwargs)
                        with patch.object(p, "ROOT", root), patch.object(p, "CACHE", root / ".cache/arena"), \
                                patch.object(p, "OFFICIAL_ASSETS", assets), \
                                patch.object(Path, "resolve", no_external_resolution), patch.object(p, "digest") as read_asset:
                            protected = p.protected_paths()
                            read_asset.assert_not_called()
                        self.assertNotIn(official, protected)
                        self.assertIn((root / "tools/observer.mjs").resolve(), protected)
                        self.assertIn((root / ".cache/arena/snap/aaaaaaaaaaaa").resolve(), protected)

    def test_malformed_or_unsupported_external_input_pins_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root, assets = base / "repo", base / "official-cache"
            official = assets / ("d" * 64) / "visualiser/assets/engine.wasm"
            descriptor = {"path": str(official), "sha256": "a" * 64, "bytes": 638472}
            record = root / "results/population/native/case.json"
            record.parent.mkdir(parents=True)
            malformed = [{"path": str(official), "sha256": "a" * 64},
                         dict(descriptor, bytes=-1), dict(descriptor, bytes=True),
                         dict(descriptor, bytes=1.5), dict(descriptor, sha256="bad"),
                         dict(descriptor, extra="unknown"),
                         dict(descriptor, path=str(base / "outside/engine.wasm")),
                         dict(descriptor, path=str(assets / ".." / "outside/engine.wasm")),
                         dict(descriptor, path=str(official.parent / "evidence.json")),
                         dict(descriptor, path=str(official.parent / "replay.repl26a")),
                         dict(descriptor, path=str(assets / "bad-release/visualiser/assets/engine.wasm"))]
            for field in ("inputs", "pins"):
                for bad in malformed:
                    with self.subTest(field=field, descriptor=bad):
                        record.write_text(json.dumps({field: [bad]}), encoding="utf-8")
                        with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaises(ArenaError):
                            p.protected_paths()
            for value in (descriptor, {"artifacts": [descriptor]}, {"replay": str(official)}):
                with self.subTest(unrecognized_context=value):
                    record.write_text(json.dumps(value), encoding="utf-8")
                    with patch.object(p, "ROOT", root), patch.object(p, "OFFICIAL_ASSETS", assets), self.assertRaisesRegex(ArenaError, "outside repository"):
                        p.protected_paths()


class Variants(unittest.TestCase):
    """Use real knob application/hash/snapshot loading, with a fake compiler only."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for target, value in (("ROOT", self.root), ("CACHE", self.root / ".cache/arena")):
            patcher = patch.object(p, target, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        for target, value in (("ROOT", self.root), ("BOTS", self.root / "bots"), ("CACHE", self.root / ".cache/arena")):
            patcher = patch.object(p.core, target, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.source = self.root / "bots/variants/repair/src/main.cpp"
        self.source.parent.mkdir(parents=True)
        self.source.write_bytes(b"#define REPAIR_ENABLED 0\n#define SECOND_KNOB 0\nint main(){return REPAIR_ENABLED;}\n")
        for rel in ("bots/variants/tests/run.py", "tools/arena/wasi.py", "tools/arena/wasi-run.mjs",
                    "tools/arena/gate.py", "tools/arena/population.py", "tools/arena/games.py"):
            target = self.root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("fixture " + rel, encoding="utf-8")
        bundle = self.root / "docs/reference/current-version.json"
        bundle.parent.mkdir(parents=True)
        bundle.write_text(json.dumps({"engine": {"version": "1"}}), encoding="utf-8")
        self.map = self.root / "maps/map.map26a"
        self.map.parent.mkdir(parents=True)
        self.map.write_bytes(b"map fixture")
        for target, value in (("pool_maps", ["map"]), ("toolchain", "tool")):
            patcher = patch.object(p.core, target, return_value=value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = patch.object(p.core, "map_path", return_value=self.map)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.contract = {k: "declared" for k in p.CONTRACT_FIELDS}
        self.contract.update(id="variants/repair", bot="variants/repair", overrides={"REPAIR_ENABLED": "1"})
        self.bot = p.core.bot("variants/repair")
        self.prepare_calls = []

    def fake_prepare(self, bot, overrides=None, contents=None):
        self.prepare_calls.append((bot, dict(overrides or {}), dict(contents)))
        applied = p.core.apply_overrides(bot, contents, overrides) if overrides else dict(contents)
        version = p.core.version_of(applied, overrides)
        snap = p.CACHE / "snap" / version
        for source, content in applied.items():
            target = snap / source.relative_to(self.root)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        (snap / "meta.json").write_text(json.dumps({"bot": bot.id, "version": version,
                                                    "overrides": dict(overrides or {}), "files": len(contents)}), encoding="utf-8")
        (snap / ".ready").write_text("fake compiler succeeded", encoding="utf-8")
        (snap / ".toolchain").write_text("tool\n", encoding="utf-8")
        return Entrant(bot, version, snap / bot.path.relative_to(self.root), True,
                       overrides=tuple(sorted((overrides or {}).items())))

    def panel(self, contract=None, recorded=None):
        contract = self.contract if contract is None else contract
        recorded = p.identity(contract) if recorded is None else recorded
        row = {"bot": contract["bot"], "contract": contract, "assessment": {"ready": False},
               "identity": recorded, "gaps": ["native evidence is still required"]}
        with patch.object(p, "inventory", return_value=[row]), patch.object(p.core, "prepare", side_effect=self.fake_prepare):
            return p.make_panel(mode="quick", experimental=True)

    def test_default_off_variant_pins_correct_applied_snapshot_without_building(self):
        raw = p.core.read_closure(self.bot)
        recorded = p.identity(self.contract)
        applied = p.core.apply_overrides(self.bot, raw, self.contract["overrides"])
        expected = self.bot.id + "@" + p.core.version_of(applied, self.contract["overrides"])
        self.assertEqual(recorded["snapshot"], expected)
        self.assertEqual(recorded["working_source"], p._source_hashes(raw))
        self.assertEqual(recorded["source"], p._source_hashes(applied))
        self.assertNotEqual(recorded["working_source"], recorded["source"])
        panel = self.panel(recorded=recorded)
        self.assertEqual(self.prepare_calls[0][1], {"REPAIR_ENABLED": "1"})
        self.assertEqual(self.prepare_calls[0][2], raw)  # prepare receives raw bytes, not pre-applied bytes
        self.assertIn(b"#define REPAIR_ENABLED 0", self.source.read_bytes())
        self.assertEqual(panel["entries"][0]["snapshot"], expected)
        self.assertEqual(panel["entries"][0]["overrides"], self.contract["overrides"])
        self.assertEqual(p.validate_panel(panel)[0].key, expected)

    def test_changed_raw_default_stales_live_evidence_but_historical_applied_panel_replays(self):
        before = p.identity(self.contract)
        panel = self.panel(recorded=before)
        self.source.write_bytes(self.source.read_bytes().replace(b"REPAIR_ENABLED 0", b"REPAIR_ENABLED 2"))
        after = p.identity(self.contract)
        self.assertEqual(before["snapshot"], after["snapshot"])
        self.assertEqual(before["source"], after["source"])
        self.assertNotEqual(before["working_source"], after["working_source"])
        evidence = {"schema": 1, "behavior": self.contract["id"], "identity": before}
        self.assertEqual(p.assess(self.contract, evidence)["state"], "stale")
        with patch.object(p.core, "read_closure") as read, patch.object(p.core, "prepare") as prepare:
            self.assertEqual(p.validate_panel(panel)[0].key, before["snapshot"])
            read.assert_not_called()
            prepare.assert_not_called()

    def test_changed_override_values_stale_live_evidence(self):
        before = p.identity(self.contract)
        changed = copy.deepcopy(self.contract)
        changed["overrides"]["REPAIR_ENABLED"] = "2"
        after = p.identity(changed)
        self.assertNotEqual(before["snapshot"], after["snapshot"])
        self.assertNotEqual(before["source"], after["source"])
        self.assertEqual(before["working_source"], after["working_source"])
        evidence = {"schema": 1, "behavior": self.contract["id"], "identity": before}
        self.assertEqual(p.assess(changed, evidence)["state"], "stale")

    def test_omitted_and_empty_overrides_keep_the_default_snapshot_key(self):
        omitted = dict(self.contract)
        del omitted["overrides"]
        explicit = dict(omitted, overrides={})
        expected = self.bot.id + "@" + p.core.version(self.bot)
        for contract in (omitted, explicit):
            recorded = p.identity(contract)
            self.assertEqual(recorded["snapshot"], expected)
            self.assertEqual(recorded["overrides"], {})
            self.assertEqual(recorded["source"], recorded["working_source"])

    def test_malformed_override_shapes_fail_before_reading_source(self):
        for value in (None, [], "REPAIR_ENABLED=1", {"REPAIR_ENABLED": 1}, {"REPAIR_ENABLED": True}, {1: "1"}):
            with self.subTest(value=value), patch.object(p.core, "read_closure") as read:
                contract = dict(self.contract, overrides=value)
                with self.assertRaisesRegex(ArenaError, "overrides must be a dict"):
                    p.identity(contract)
                read.assert_not_called()

    def test_missing_knob_fails_instead_of_falling_back_to_default(self):
        contract = dict(self.contract, overrides={"UNKNOWN_KNOB": "1"})
        with self.assertRaisesRegex(ArenaError, "UNKNOWN_KNOB"):
            p.identity(contract)

    def test_override_order_does_not_change_snapshot_identity(self):
        left = dict(self.contract, overrides={"REPAIR_ENABLED": "1", "SECOND_KNOB": "2"})
        right = dict(self.contract, overrides={"SECOND_KNOB": "2", "REPAIR_ENABLED": "1"})
        self.assertEqual(p.identity(left), p.identity(right))

    def test_variant_metadata_and_manifest_override_mismatches_fail(self):
        panel = self.panel()
        changed = copy.deepcopy(panel)
        changed["entries"][0]["overrides"]["REPAIR_ENABLED"] = "0"
        with self.assertRaisesRegex(ArenaError, "override values disagree"):
            p.validate_panel(changed)
        changed = copy.deepcopy(panel)
        changed["entries"][0]["identity"]["overrides"] = None
        with self.assertRaisesRegex(ArenaError, "identity overrides must be a dict"):
            p.validate_panel(changed)
        version = panel["entries"][0]["snapshot"].rsplit("@", 1)[1]
        meta_path = p.CACHE / "snap" / version / "meta.json"
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        meta["overrides"] = None
        meta_path.write_text(json.dumps(meta), encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "snapshot metadata overrides must be a dict"):
            p.validate_panel(panel)

    def test_changed_raw_source_rejects_panel_creation_before_prepare(self):
        recorded = p.identity(self.contract)
        self.source.write_bytes(self.source.read_bytes().replace(b"REPAIR_ENABLED 0", b"REPAIR_ENABLED 2"))
        with self.assertRaisesRegex(ArenaError, "working source changed before pinning"):
            self.panel(recorded=recorded)
        self.assertEqual(self.prepare_calls, [])

    def test_excluded_identity_rejected_before_source_lookup(self):
        for bot in ("main/main", "MAIN/main", "surprise/new", "SURPRISE/new",
                    "contenders/economy", "CONTENDERS/economy", "anchors/ref", "ANCHORS/ref"):
            with patch.object(p.core, "bot") as lookup, patch.object(p.core, "read_closure") as read:
                with self.assertRaisesRegex(ArenaError, "outside the population mandate"):
                    p.identity(dict(self.contract, bot=bot))
                lookup.assert_not_called()
                read.assert_not_called()


if __name__ == "__main__":
    unittest.main()
