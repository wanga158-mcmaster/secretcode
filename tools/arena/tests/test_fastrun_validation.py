"""Offline checks of complete-key, physical-team fastrun/CLI validation."""
from __future__ import annotations

import contextlib
import copy
import importlib.util
import io
import sys
import shutil
import unittest
import uuid
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
spec = importlib.util.spec_from_file_location("fastrun_validate", ROOT / "tools" / "fastrun" / "validate.py")
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


def rows():
    cli, fast = [], []
    for side in ("A", "B"):
        key = f"sample.{side}"
        teams = {"A": {"gems": 100, "errors": 2, "faults": 1, "exhausted": 3},
                 "B": {"gems": 25, "errors": 4, "faults": 0, "exhausted": 6}}
        cli.append({"tags": {"case": key}, "winner": "A", "rounds": 2000, **teams})
        fast.append({"tag": key, "a_side": side, "winner": "a" if side == "A" else "b", "rounds": 2000,
                     **{field: [teams[t][field] for t in ("A", "B")] for field in ("gems",) + v.HEALTH}})
    return cli, fast, {"sample.A": "A", "sample.B": "B"}


class CompareTests(unittest.TestCase):
    def test_physical_teams_match_with_subject_on_either_side(self):
        result = v.compare_rows(*rows())
        self.assertTrue(result["ok"])
        self.assertEqual(result["identical"], 2)

    def test_each_team_health_metric_is_compared(self):
        for field in v.HEALTH:
            for team in (0, 1):
                with self.subTest(field=field, team=team):
                    cli, fast, expected = rows()
                    fast[1][field][team] += 1
                    self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_missing_row_on_either_runner_fails(self):
        for runner in (0, 1):
            values = list(rows())
            values[runner].pop()
            result = v.compare_rows(*values)
            self.assertFalse(result["ok"])
            self.assertTrue(any("missing row" in issue for issue in result["issues"]))

    def test_duplicate_row_cannot_substitute_for_complete_matrix(self):
        for runner in (0, 1):
            values = list(rows())
            values[runner].append(copy.deepcopy(values[runner][0]))
            result = v.compare_rows(*values)
            self.assertFalse(result["ok"])
            self.assertTrue(any("duplicate row" in issue for issue in result["issues"]))

    def test_unexpected_row_fails(self):
        cli, fast, expected = rows()
        extra = copy.deepcopy(fast[0])
        extra["tag"] = "unexpected.A"
        fast.append(extra)
        self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_matching_runner_failures_do_not_pass(self):
        cli, fast, expected = rows()
        cli[0]["winner"] = fast[0]["winner"] = "error"
        cli[0]["error"] = fast[0]["error"] = "engine failed"
        self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_truncation_or_invalid_subject_side_fails(self):
        for mutation in ({"truncated": True}, {"a_side": "B"}):
            cli, fast, expected = rows()
            fast[0].update(mutation)
            self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_missing_or_invalid_metrics_fail(self):
        for value in (None, [1], [1, "2"], [True, 2]):
            cli, fast, expected = rows()
            fast[0]["errors"] = value
            self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])
        cli, fast, expected = rows()
        del cli[0]["A"]["exhausted"]
        self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_invalid_key_fails(self):
        cli, fast, expected = rows()
        fast.append({"tag": []})
        self.assertFalse(v.compare_rows(cli, fast, expected)["ok"])

    def test_empty_matrix_does_not_pass(self):
        self.assertFalse(v.compare_rows([], [], {})["ok"])

    def test_candidate_runner_is_selected_without_installing(self):
        cli, fast, _ = rows()
        installed = v.exp.FASTRUN
        scratch = ROOT / ".cache" / "runner-validation"
        scratch.mkdir(parents=True, exist_ok=True)
        root = scratch / ("unit-" + uuid.uuid4().hex)
        root.mkdir()
        try:
            candidate = root / "candidate.exe"
            candidate.write_bytes(b"candidate")
            (root / ".cache").mkdir()
            def run(*args, **kwargs):
                self.assertEqual(v.exp.FASTRUN, candidate.resolve())
                return fast
            with patch.object(v.core, "ROOT", root), patch.object(v.core, "prepare_many", return_value=[SimpleNamespace(ok=True)] * 2), \
                    patch.object(v.core, "map_set", return_value=["sample"]), patch.object(v.core, "map_path", return_value=root / "sample.map26a"), \
                    patch.object(v.core, "bot", side_effect=lambda name: name), patch.object(v.exp, "wasm", return_value="bot.wasm"), \
                    patch.object(v.games, "GameSpec"), patch.object(v.games, "play", return_value=cli), \
                    patch.object(v.exp, "run_games", side_effect=run), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(v.main(["--runner", str(candidate)]), 0)
            self.assertEqual(candidate.read_bytes(), b"candidate")
            self.assertEqual(v.exp.FASTRUN, installed)
        finally:
            shutil.rmtree(root)


if __name__ == "__main__":
    unittest.main()
