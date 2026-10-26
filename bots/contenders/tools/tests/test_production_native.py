"""Terminal production regression tests against retained original native statistics.

Run with the arena Python runtime: python -B -I -X utf8 <this file> [-v].
No games, replay decoding, bot source reads, or inferred private behavior.
"""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
import fundamentals as fu
import production

CLEAN = ROOT / "results/contenders/mosaic/lean-r2/native/00packs_mosaic-navigation-passiv_mosaic_52956b31.json"
COHORT = ROOT / "results/contenders/mosaic/lean-r2-cohort/native"
BIGWORLD = COHORT / "01big-world_mosaic_main-v44_f826c1e6.json"
ZERO_YBR = COHORT / "01yellow-brick-road_mosaic_mosaic-navigation-passiv_642c176a.json"
OWN_ERROR = COHORT / "00packs_main-v44_mosaic_73b33147.json"


class NativeTerminal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clean = fu.read_stats(CLEAN)
        cls.bigworld = fu.read_stats(BIGWORLD)

    def test_real_clean_production_and_per_actor_native_identity(self):
        result = production.read_native_terminal(CLEAN, "teamB")
        self.assertEqual(result["statistics"]["sha256"],
                         "fffc9e65cc34b57e07046bcee6f8f05e207c06af5268937a7aae1261a9a72a9a")
        self.assertEqual([(c["id"], c["name"], c["team"]) for c in result["champions"]],
                         [(3, "Varg", "teamB"), (4, "Ryth", "teamB"), (5, "Marcella", "teamB")])
        metrics = result["metrics"]
        self.assertEqual([metrics[k] for k in ("monsterDamageDone", "earnedGold", "spentGold",
                                               "collectedGems", "submittedGems", "gems")],
                         [99740, 6106, 3390, 1022, 1012, 10])
        self.assertEqual(result["event_totals"],
                         {"final_clear_credits": 37, "gem_receipt_events": 61, "received_gems": 1022,
                          "bank_events": 34, "banked_gems": 1012})
        self.assertEqual([c["metrics"]["submittedGems"] for c in result["champions"]], [474, 135, 403])
        # Include the last started partial round instead of truncating to completed rounds.
        self.assertEqual(result["match"]["roundsCompleted"], 980)
        self.assertEqual(max(e["timestamp"]["round"] for c in result["champions"]
                             for e in c["events"]["gemSubmission"]), 981)

    def test_real_passive_zero_is_observed_not_missing(self):
        result = production.native_terminal(self.clean, "teamA")
        self.assertTrue(all(value == 0 for value in result["metrics"].values()))
        self.assertTrue(all(value == 0 for value in result["event_totals"].values()))
        self.assertEqual([c["id"] for c in result["champions"]], [0, 1, 2])
        self.assertTrue(all(entries == [] for c in result["champions"] for entries in c["events"].values()))

    def test_real_lost_game_zero_clear_has_damage_and_conversion(self):
        result = production.native_terminal(self.bigworld, "teamA")
        self.assertEqual(result["event_totals"]["final_clear_credits"], 0)
        self.assertEqual(result["metrics"]["monsterDamageDone"], 7340)
        self.assertEqual(result["metrics"]["submittedGems"], 228)
        self.assertEqual(result["metrics"]["deaths"], 7)
        marcella = next(c for c in result["champions"] if c["id"] == 2)
        self.assertEqual((marcella["event_totals"]["received_gems"],
                          marcella["event_totals"]["banked_gems"]), (228, 228))
        receipt = marcella["events"]["gemCollection"][0]
        self.assertEqual((receipt["target"]["id"], receipt["timestamp"]["round"]), (16, 1084))
        self.assertEqual(marcella["events"]["gemSubmission"][0]["timestamp"]["round"], 1129)
        enemy = production.native_terminal(self.bigworld, "teamB")
        varg = next(c for c in enemy["champions"] if c["id"] == 9)
        self.assertTrue(any(e["target"]["id"] == 16 and e["timestamp"]["round"] == 1062
                            for e in varg["events"]["campClear"]))

    def test_real_zero_value_receipt_is_still_a_receipt(self):
        result = production.native_terminal(self.bigworld, "teamB")
        varg = next(c for c in result["champions"] if c["id"] == 9)
        self.assertTrue(any(e["target"]["id"] == 10 and e["timestamp"]["round"] == 165
                            and e["value"] == "0" for e in varg["events"]["gemCollection"]))
        self.assertEqual(result["event_totals"]["gem_receipt_events"], 63)
        self.assertEqual(sum(e["context"] == "campGroup" for c in result["champions"]
                             for e in c["events"]["gemCollection"]), 61)

    def test_real_empty_ybr_and_own_error_match_keep_owner_attribution(self):
        # Genuine failed economy vs passive; a tiebreak/outcome cannot supply production.
        zero = production.read_native_terminal(ZERO_YBR, "teamA")
        self.assertEqual((zero["metrics"]["collectedGems"], zero["metrics"]["submittedGems"],
                          zero["event_totals"]["final_clear_credits"]), (0, 0, 0))
        # Retained match with Mosaic's own E44: selected team is explicit, independent
        # of filenames, list order, same champion names, outcome, or external health rows.
        self.assertEqual(production.read_native_terminal(OWN_ERROR, "teamB")["metrics"]["submittedGems"], 231)
        self.assertEqual(production.read_native_terminal(OWN_ERROR, "teamA")["metrics"]["submittedGems"], 1013)

    def test_metrics_do_not_come_from_event_counts_or_array_order(self):
        stats = copy.deepcopy(self.clean)
        stats["team_event_counts"] = {}
        stats["champions"].reverse()
        stats["roster"]["champions"].reverse()
        stats["events"].reverse()
        result = production.native_terminal(stats, "teamB")
        self.assertEqual(result["metrics"]["monsterDamageDone"], 99740)
        self.assertEqual(result["event_totals"]["final_clear_credits"], 37)

    def test_missing_metric_is_explicit_even_for_real_zero_actor(self):
        stats = copy.deepcopy(self.clean)
        del stats["champions"][0]["metrics"]["monsterDamageDone"]
        with self.assertRaisesRegex(production.NativeStatisticsError, "monsterDamageDone"):
            production.native_terminal(stats, "teamA")

    def test_invalid_numeric_metric_is_not_zero(self):
        for value in (None, False, "NaN", "-1", "unknown"):
            with self.subTest(value=value):
                stats = copy.deepcopy(self.clean)
                stats["champions"][0]["metrics"]["monsterDamageDone"] = value
                with self.assertRaises(production.NativeStatisticsError):
                    production.native_terminal(stats, "teamA")

    def test_required_empty_stream_missing_or_duplicated_is_not_zero(self):
        for duplicate in (False, True):
            with self.subTest(duplicate=duplicate):
                stats = copy.deepcopy(self.clean)
                index = next(i for i, event in enumerate(stats["events"])
                             if event["kind"] == "gemCollection" and event["owner"]["id"] == 0)
                event = stats["events"].pop(index)
                if duplicate:
                    stats["events"].extend([event, copy.deepcopy(event)])
                with self.assertRaises(production.NativeStatisticsError):
                    production.native_terminal(stats, "teamA")

    def test_empty_opponent_stream_owner_must_still_match_native_roster(self):
        stats = copy.deepcopy(self.clean)
        event = next(e for e in stats["events"] if e["kind"] == "campClear" and e["owner"]["id"] == 0)
        event["owner"]["team"] = "teamB"
        with self.assertRaisesRegex(production.NativeStatisticsError, "owner mismatch"):
            production.native_terminal(stats, "teamB")

    def test_champion_roster_mismatch_and_missing_schema_fail_explicitly(self):
        stats = copy.deepcopy(self.clean)
        stats["champions"][0]["team"] = "teamB"
        with self.assertRaisesRegex(production.NativeStatisticsError, "owner mismatch"):
            production.native_terminal(stats, "teamA")
        for key in ("champions", "roster", "events", "formatVersion"):
            with self.subTest(missing=key):
                stats = copy.deepcopy(self.clean)
                del stats[key]
                with self.assertRaises(production.NativeStatisticsError):
                    production.native_terminal(stats, "teamA")

    def test_incomplete_capture_unknown_semantics_and_team_fail_explicitly(self):
        stats = copy.deepcopy(self.clean)
        stats["diagnostics"]["detailedCaptureComplete"] = False
        with self.assertRaisesRegex(production.NativeStatisticsError, "incomplete"):
            production.native_terminal(stats, "teamB")
        stats = copy.deepcopy(self.clean)
        stats["match"]["metricSemanticsVersion"] = 99
        with self.assertRaisesRegex(production.NativeStatisticsError, "unsupported"):
            production.native_terminal(stats, "teamB")
        with self.assertRaisesRegex(production.NativeStatisticsError, "teamA or teamB"):
            production.native_terminal(self.clean, "B")


    def test_fractional_counters_bad_targets_ids_and_missing_rounds_are_errors(self):
        for value in ("0.5", "1e2", 0.5, False):
            with self.subTest(counter=value):
                stats = copy.deepcopy(self.clean)
                stats["champions"][0]["metrics"]["submittedGems"] = value
                with self.assertRaises(production.NativeStatisticsError):
                    production.native_terminal(stats, "teamA")
        for target in (None, 0, {"kind": "campGroup", "id": None},
                       {"kind": "campGroup", "id": -1}, {"kind": "champion", "id": 3}):
            with self.subTest(target=target):
                stats = copy.deepcopy(self.clean)
                event = next(e for e in stats["events"] if e["kind"] == "gemCollection"
                             and e["owner"]["id"] == 3)
                event["entries"][0]["target"] = target
                with self.assertRaises(production.NativeStatisticsError):
                    production.native_terminal(stats, "teamB")
        for field in ("roundsCompleted", "roundsStarted"):
            stats = copy.deepcopy(self.clean)
            del stats["match"][field]
            with self.assertRaises(production.NativeStatisticsError):
                production.native_terminal(stats, "teamB")
        stats = copy.deepcopy(self.clean)
        stats["roster"]["champions"][0]["id"] = -1
        with self.assertRaises(production.NativeStatisticsError):
            production.native_terminal(stats, "teamA")
        stats = copy.deepcopy(self.clean)
        event = next(e for e in stats["events"] if e["kind"] == "gemCollection"
                     and e["owner"]["id"] == 3)
        event["entries"][0]["timestamp"]["round"] = 1.5
        with self.assertRaises(production.NativeStatisticsError):
            production.native_terminal(stats, "teamB")

    def test_gzip_loader_pins_exact_original_bytes(self):
        import gzip
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "native.json.gz"
            payload = gzip.compress(CLEAN.read_bytes())
            path.write_bytes(payload)
            result = production.read_native_terminal(path, "teamB")
            self.assertEqual(result["metrics"]["submittedGems"], 1012)
            self.assertEqual(result["statistics"]["sha256"], hashlib.sha256(payload).hexdigest())


    def test_new_cli_outputs_json_and_existing_help_remains(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "report.json"
            cmd = [sys.executable, "-B", "-I", "-X", "utf8", str(TOOLS / "production.py")]
            completed = subprocess.run(cmd + ["native", str(CLEAN), "--team", "teamB", "--json", str(out)],
                                       capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(json.loads(out.read_text(encoding="utf-8"))["metrics"]["submittedGems"], 1012)
            invalid = Path(directory) / "missing.json"
            invalid.write_text("{}", encoding="utf-8")
            completed = subprocess.run(cmd + ["native", str(invalid), "--team", "teamB"],
                                       capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("unavailable", completed.stderr)
            legacy = subprocess.run(cmd + ["--help"], capture_output=True, text=True, check=False)
            self.assertEqual(legacy.returncode, 0)
            self.assertIn("--variant", legacy.stdout)
            self.assertIn("runs", legacy.stdout)


if __name__ == "__main__":
    unittest.main()

