"""Fast contract checks. Native smoke is separate and uses complete local games."""
from __future__ import annotations

import copy
import contextlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
from types import SimpleNamespace
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
ADAPTERS = HERE.parent / "adapters"
sys.path.insert(0, str(ADAPTERS))
import native_support as support
import native_evaluator as evaluator


class NativeContract(unittest.TestCase):
    def test_isolated_cli_entrypoint(self):
        proc = subprocess.run([sys.executable, "-B", "-I", str(ADAPTERS / "native_evaluator.py"), "--help"],
                              capture_output=True, text=True, timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--request", proc.stdout)

    def test_data_only_knobs_include_joint_arrays(self):
        self.assertEqual(support.knobs({"draft_pref": [0, 2, 1], "enabled": True, "weight": 0.25}),
                         {"draft_pref": "{0, 2, 1}", "enabled": "true", "weight": "0.25"})
        self.assertEqual(support.cpp_value("{1,2,3}"), "{1, 2, 3}")
        for val in ("1; system(\"bad\")", "foo()", "#include <x>", "{1, x}", [[1]], float("nan")):
            with self.subTest(val=val), self.assertRaises(support.EvaluationError):
                support.cpp_value(val)

    def test_invalid_knob_names(self):
        for name in ("x.y", "a;b", "../../x", "", "1abc"):
            with self.subTest(name=name), self.assertRaises(support.EvaluationError):
                support.knobs({name: 1})

    def test_canonical_identity_is_order_independent(self):
        self.assertEqual(support.digest({"b": 2, "a": [1, 3]}), support.digest({"a": [1, 3], "b": 2}))
        self.assertNotEqual(support.digest({"a": 1}), support.digest({"a": 2}))

    def test_json_rejects_duplicate_and_nonfinite(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "input.json"
            for raw in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}'):
                p.write_text(raw, encoding="utf-8")
                with self.subTest(raw=raw), self.assertRaises((support.EvaluationError, ValueError)):
                    support.read_json(p)

    def test_atomic_round_trip_and_no_stale_tail(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "result.json"
            support.write_json(p, {"long": "a" * 100})
            support.write_json(p, {"short": 1})
            self.assertEqual(support.read_json(p), {"short": 1})
            self.assertEqual(list(Path(td).glob("*.tmp")), [])

    def test_native_map_and_cli_changes_reject_old_binding(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mp, cli = root / "map", root / "cli"
            mp.write_bytes(b"map-a")
            cli.write_bytes(b"engine-a")
            class Core:
                CAMBC = cli
            case = {"map_path": mp, "map_sha256": support.file_digest(mp)}
            environment = {"cli": support.file_digest(cli)}
            evaluator.verify_game_inputs(Core, case, environment)
            mp.write_bytes(b"map-b")
            with self.assertRaisesRegex(support.EvaluationError, "map bytes changed"):
                evaluator.verify_game_inputs(Core, case, environment)
            mp.write_bytes(b"map-a")
            cli.write_bytes(b"engine-b")
            with self.assertRaisesRegex(support.EvaluationError, "CLI bytes changed"):
                evaluator.verify_game_inputs(Core, case, environment)

    def test_duplicate_cases_rejected_and_context_union_deduplicates(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mp = root / "x.map26a"
            mp.write_bytes(b"fixture-map")
            class Core:
                ROOT = root
                @staticmethod
                def map_path(value):
                    return mp
            req = {"cases": [{"map": "x", "side": "A", "opponent": "anchors/main-v42"}] * 2}
            with self.assertRaises(support.EvaluationError):
                evaluator.normalize_cases(req, Core, "anchors/main-v42")
            self.assertEqual(len(evaluator.normalize_cases(req, Core, "anchors/main-v42", True)), 1)
            req["cases"] = [{"map": "x", "side": "B", "opponent": "anchors/main-v42", "seed": 3}]
            with self.assertRaises(support.EvaluationError):
                evaluator.normalize_cases(req, Core, "anchors/main-v42")

    def test_map_path_spellings_share_physical_identity_and_logical_name(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            mp = root / "maps/legacy/eight.map26a"
            mp.parent.mkdir(parents=True)
            mp.write_bytes(b"fixture-map")
            class Core:
                ROOT = root
                @staticmethod
                def map_path(value):
                    if value != "eight":
                        raise AssertionError("path spellings must resolve before stem lookup")
                    return mp
            normalized = []
            for name in ("eight", "legacy/eight.map26a", "maps/legacy/eight.map26a", str(mp)):
                case = {"map": name, "side": "A", "map_sha256": support.file_digest(mp)}
                row = evaluator.normalize_cases({"cases": [case]}, Core, "anchors/main-v42")[0]
                self.assertEqual(row["map"], name)
                self.assertEqual(row["map_name"], "eight")
                self.assertEqual(row["map_path"], mp)
                normalized.append((row["map_relative"], row["map_sha256"]))
            self.assertEqual(len(set(normalized)), 1)
            aliases = [{"map": name, "side": "A"}
                       for name in ("eight", "legacy/eight.map26a")]
            with self.assertRaisesRegex(support.EvaluationError, "duplicate deterministic case"):
                evaluator.normalize_cases({"cases": aliases}, Core, "anchors/main-v42")

    def test_payoff_uses_engine_outcome_not_gold_proxy(self):
        self.assertEqual(evaluator.payoff({"winner": "A", "A": {"gems": 0}, "B": {"gems": 1000}}, "A"), 1)
        self.assertEqual(evaluator.payoff({"winner": "A"}, "B"), -1)
        self.assertEqual(evaluator.payoff({"winner": "draw"}, "A"), 0)

    def test_declared_context_pin_checked_before_candidate_or_game(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            core = SimpleNamespace(ROOT=root)
            games = SimpleNamespace(ENGINE="fixture", play=unittest.mock.Mock())
            entrant = SimpleNamespace(key="anchors/main-v42@123456789abc")
            case = {"map": "map", "map_name": "map", "map_relative": "maps/map.map26a",
                    "map_sha256": "1" * 64, "side": "A", "opponent": "anchors/main-v42"}
            identity = {"key": entrant.key, "source_sha256": "2" * 64, "modules": {}}
            with patch.object(evaluator, "load_arena", return_value=(core, games)), \
                 patch.object(evaluator, "frozen_default", return_value="anchors/main-v42"), \
                 patch.object(evaluator, "normalize_cases", return_value=[case]), \
                 patch.object(evaluator, "restore_population", return_value=[]), \
                 patch.object(evaluator, "exact_entrant", return_value=entrant), \
                 patch.object(evaluator, "entrant_identity", return_value=identity), \
                 patch.object(evaluator, "environment_files", return_value={}), \
                 patch.object(evaluator, "configuration") as candidate:
                request = {"schema": evaluator.SCHEMA, "kind": "identity", "cases": [{}]}
                current = evaluator.evaluate_request(request, root)["context_identity"]
                matched = evaluator.evaluate_request(dict(request, expected_context_identity=current), root)
                self.assertEqual(matched["context_identity"], current)
                with self.assertRaisesRegex(support.EvaluationError, "context identity mismatch"):
                    evaluator.evaluate_request(dict(request, kind="config", expected_context_identity="0" * 64), root)
                candidate.assert_not_called()
                games.play.assert_not_called()
        with patch.object(evaluator, "load_arena") as load:
            with self.assertRaisesRegex(support.EvaluationError, "expected_context_identity must"):
                evaluator.evaluate_request({"kind": "identity", "expected_context_identity": False}, Path("unused"))
            load.assert_not_called()

    def test_reproduction_map_pin_rejects_changed_bytes_before_launch(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mp = root / "x.map26a"
            mp.write_bytes(b"frozen-map")
            class Core:
                ROOT = root
                @staticmethod
                def map_path(value):
                    return mp
            value = {"map": "x", "side": "A", "map_sha256": support.file_digest(mp)}
            request = {"cases": [value]}
            normalized = evaluator.normalize_cases(request, Core, "anchors/main-v42")
            self.assertEqual(normalized[0]["map_sha256"], value["map_sha256"])
            mp.write_bytes(b"changed-map")
            with self.assertRaisesRegex(support.EvaluationError, "map bytes differ"):
                evaluator.normalize_cases(request, Core, "anchors/main-v42")
            for invalid in (None, True, "", "0" * 63, "G" * 64):
                with self.subTest(invalid=invalid), self.assertRaisesRegex(support.EvaluationError, "lowercase SHA256"):
                    evaluator.normalize_cases({"cases": [dict(value, map_sha256=invalid)]}, Core, "anchors/main-v42")

    def test_population_restoration_requires_full_exact_identity(self):
        key = "contenders/_optimizer-config@123456789abc"
        request = {"population_configs": [{"key": key, "controller": "anchors/main-v42", "knobs": {}}]}
        with self.assertRaisesRegex(support.EvaluationError, "candidate_identity"):
            evaluator.restore_population(None, request, {key}, Path("unused"))
        request["population_configs"] *= 2
        request["population_configs"][1] = dict(request["population_configs"][0], knobs={"gem_value": 1})
        with self.assertRaisesRegex(support.EvaluationError, "conflicting"):
            evaluator.restore_population(None, request, {key}, Path("unused"))

    def test_existing_population_snapshot_still_checks_module_bytes(self):
        key = "contenders/_optimizer-config@123456789abc"
        identity = {"key": key, "source_sha256": "1" * 64,
                    "modules": {role: {"sha256": "2" * 64, "size": 10} for role in ("raw", "injected")}}
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            ready = root / "snap/123456789abc/.ready"
            ready.parent.mkdir(parents=True)
            ready.write_text("ready")
            class Core:
                CACHE = root
                @staticmethod
                def FileLock(path):
                    return contextlib.nullcontext()
            request = {"population_configs": [{"key": key, "identity": identity}]}
            with patch.object(evaluator, "exact_entrant", return_value=object()), \
                 patch.object(evaluator, "entrant_identity", return_value=identity):
                receipts = evaluator.restore_population(Core, request, {key}, root)
            self.assertFalse(receipts[0]["restored"])
            changed = copy.deepcopy(identity)
            changed["modules"]["raw"]["sha256"] = "3" * 64
            with patch.object(evaluator, "exact_entrant", return_value=object()), \
                 patch.object(evaluator, "entrant_identity", return_value=changed), \
                 self.assertRaisesRegex(support.EvaluationError, "WASM identity mismatch"):
                evaluator.restore_population(Core, request, {key}, root)

    def test_materializer_exact_base_cannot_alias_changed_source(self):
        class Entrant:
            key = "anchors/main-v42@111111111111"
        with patch.object(evaluator, "exact_entrant", return_value=Entrant()):
            self.assertEqual(evaluator.materializer_base(None, Entrant.key), "anchors/main-v42")
            with self.assertRaisesRegex(support.EvaluationError, "base source changed"):
                evaluator.materializer_base(None, "anchors/main-v42@222222222222")

    def test_cold_exact_anchor_rebuild_requires_same_source_key(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            class Bot:
                def __init__(self, suite, name):
                    self.suite, self.name = suite, name
                    self.path = root / "bots" / suite / name
            class Entrant:
                ok = True
                key = "anchors/main-v42@123456789abc"
            class Core:
                ROOT = root
                CACHE = root / "cache"
                @staticmethod
                def prepare(bot, label=""):
                    return Entrant()
            Core.Bot = Bot
            main = root / "bots/anchors/main-v42/src/main.cpp"
            main.parent.mkdir(parents=True)
            main.write_text("int main() {}")
            self.assertEqual(support.exact_entrant(Core, Entrant.key).key, Entrant.key)
            with self.assertRaisesRegex(support.EvaluationError, "anchor source changed"):
                support.exact_entrant(Core, "anchors/main-v42@222222222222")
            with self.assertRaisesRegex(support.EvaluationError, "no working-source fallback"):
                support.exact_entrant(Core, "contenders/_optimizer-config@123456789abc")

    def test_episode_requires_logs_for_gradient_but_not_validation(self):
        class Module:
            @staticmethod
            def parse_rollouts(stdout):
                return [{"actor": 2, "decision": 1}]
        cell = {"key": "gamehash", "row": {"winner": "A"},
                "identity": {"opponent": {"key": "anchors/main-v42@123456789abc"}}}
        case = {"map_name": "x", "side": "A"}
        with patch.object(evaluator, "subject_stdout", return_value="OPT_TRAJ {}\n"):
            item = evaluator.episodes_for(Module, cell, case, "contenders/a@123456789abc", "1.0.3", True, True)
        self.assertTrue(item["complete"])
        self.assertTrue(item["logging_complete"])
        item = evaluator.episodes_for(Module, cell, case, "contenders/a@123456789abc", "1.0.3", True, True, False)
        self.assertTrue(item["complete"])
        self.assertFalse(item["logging_complete"])
        self.assertEqual(item["observations"], [])
        with patch.object(evaluator, "subject_stdout", return_value="OPT_TRAJ {}\n"):
            item = evaluator.episodes_for(Module, cell, case, "contenders/a@123456789abc", "1.0.3", True, False)
        self.assertFalse(item["complete"])
        self.assertFalse(item["logging_complete"])


if __name__ == "__main__":
    unittest.main()
