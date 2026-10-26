import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import activity_metrics as a

def fixture():
    owner = {"id": 2, "kind": "champion", "name": "Tavi", "team": "teamA"}
    metrics = {m: 0 if m in a.INTEGER_METRICS else 0.0 for m in a.METRICS}
    metrics.update(monsterDamageDone=120.0, earnedGold=20, collectedGems=3, submittedGems=3)
    def entry(value, context="0"):
        return {"value": str(value), "context": context,
                "timestamp": {"round": 3, "input": 8, "substep": 0, "phase": "campCaptures", "ordinal": 20}}
    events = {"campClear": [entry(1)], "gemCollection": [entry(0, "campGroup"), entry(3, "campGroup")],
              "gemSubmission": [entry(3)], "death": [], "fuelExhausted": []}
    return {"match": {"roundsStarted": 5},
            "roster": {"champions": [{k: v for k, v in owner.items() if k != "kind"}]},
            "champions": [{"id": 2, "team": "teamA", "name": "Tavi", "metrics": metrics}],
            "championTimelines": [{"metric": metric, "owner": owner.copy(), "sampleCount": 6,
                                     "runs": [{"duration": 3, "value": 0}, {"duration": 3, "value": value}]}
                                    for metric, value in metrics.items()],
            "events": [{"kind": kind, "owner": owner.copy(), "entries": entries} for kind, entries in events.items()]}

def s(round_, hp=100, x=4, gold=0, gems=0):
    return [round_, x, 5 if hp else -1, hp, gold, gems] if hp else [round_, -1, -1, 0, gold, gems]

class IntervalTests(unittest.TestCase):
    def test_death_breaks_equal_living_states(self):
        result = a.sample_activity([s(1), s(2), s(3, 0), s(4), s(5)], 5, 3)
        self.assertEqual((result["living_samples"], result["dead_samples"]), (4, 1))
        self.assertEqual(result["longest_unchanged_living_interval"]["sample_count"], 2)
        self.assertEqual(result["longest_interval_ties"], 2)
        self.assertFalse(result["meets_sample_threshold"])

    def test_tie_keeps_earliest_actual_interval(self):
        result = a.sample_activity([s(10), s(11), s(12), s(13, x=8), s(14, x=8), s(15, x=8)], 15, 3)
        interval = result["longest_unchanged_living_interval"]
        self.assertEqual((interval["first_round"], interval["last_round"], interval["sample_count"], interval["round_label_span"]), (10, 12, 3, 2))
        self.assertEqual(result["longest_interval_ties"], 2)
        self.assertTrue(result["meets_sample_threshold"])

    def test_threshold_is_samples_not_elapsed_round_labels(self):
        samples = [s(50), s(51)]
        self.assertTrue(a.sample_activity(samples, 51, 2)["meets_sample_threshold"])
        self.assertFalse(a.sample_activity(samples, 51, 3)["meets_sample_threshold"])

    def test_missing_round_does_not_bridge_quiet_interval(self):
        result = a.sample_activity([s(1), s(2), s(4), s(5)], 5, 3)
        self.assertEqual(result["missing_round_gaps"], 1)
        self.assertEqual(result["longest_unchanged_living_interval"]["sample_count"], 2)
        self.assertFalse(result["meets_sample_threshold"])

    def test_nonmonotonic_or_duplicate_samples_reject(self):
        for samples in ([s(2), s(1)], [s(1), s(1)]):
            with self.assertRaisesRegex(a.ValidationError, "nonmonotonic"):
                a.sample_activity(samples, 5, 2)

    def test_health_bag_and_gold_changes_each_end_interval(self):
        result = a.sample_activity([s(1), s(2, hp=90), s(3, hp=90, gems=1), s(4, hp=90, gems=1, gold=20)], 4, 2)
        self.assertEqual(result["longest_unchanged_living_interval"]["sample_count"], 1)
        self.assertFalse(result["meets_sample_threshold"])

    def test_dead_only_does_not_become_living_quiet(self):
        result = a.sample_activity([s(i, 0) for i in range(1, 61)], 60, 20)
        self.assertIsNone(result["longest_unchanged_living_interval"])
        self.assertEqual(result["dead_samples"], 60)
        self.assertFalse(result["meets_sample_threshold"])

class CompletenessTests(unittest.TestCase):
    def test_excluded_subject_rejects_before_any_evidence_read(self):
        from pathlib import Path
        with patch.object(a, "pinned", side_effect=AssertionError("unexpected read")):
            with self.assertRaisesRegex(a.ValidationError, "excluded"):
                a.analyze_saved({"subject_snapshot":"main/main@0123456789ab"}, Path.cwd())

    def test_old_engine_rejects_before_statistics_or_archive_read(self):
        from pathlib import Path
        run = {"planned_jobs":[{"id":"game-000000","a":"heuristics/example@0123456789ab","b":"anchors/example@abcdef012345","map":"map"}],"engine":{"version":"1.0.2"}}
        row = {"id":"game-000000","a":"heuristics/example@0123456789ab","b":"anchors/example@abcdef012345","map":"map","engine":"1.0.3"}
        spec = {"case":"game-000000","subject_side":"A","subject_snapshot":row["a"]}
        import json
        evidence = [(Path("run.json"),json.dumps(run).encode()),(Path("rows.jsonl"),json.dumps(row).encode())]
        with patch.object(a, "pinned", side_effect=evidence) as mocked:
            with self.assertRaisesRegex(a.ValidationError, "engine"):
                a.analyze_saved(spec, Path.cwd())
        self.assertEqual(mocked.call_count, 2)

    def test_missing_metric_is_unknown_not_synthetic_zero(self):
        stats = fixture()
        del stats["champions"][0]["metrics"]["monsterDamageDone"]
        rows, issues = a.activity_tables(stats)
        self.assertIsNone(rows[2]["totals"]["monsterDamageDone"])
        self.assertIsNone(rows[2]["whole_game_deltas"]["monsterDamageDone"])
        self.assertTrue(any("final metric missing" in issue for issue in issues))

    def test_missing_dense_group_is_unknown_not_zero_events(self):
        stats = fixture()
        stats["events"] = [e for e in stats["events"] if e["kind"] != "gemCollection"]
        rows, issues = a.activity_tables(stats)
        self.assertIsNone(rows[2]["event_counts"]["gemCollection"])
        self.assertTrue(issues)

    def test_timeline_hole_does_not_get_complete_delta(self):
        stats = fixture()
        stats["championTimelines"][0]["runs"][0]["duration"] = 2
        rows, issues = a.activity_tables(stats)
        self.assertIsNone(rows[2]["whole_game_deltas"]["monsterDamageDone"])
        self.assertTrue(any("dense timeline unknown" in x for x in issues))

    def test_owner_identity_mismatch_rejects(self):
        stats = fixture()
        stats["championTimelines"][0]["owner"]["team"] = "teamB"
        with self.assertRaisesRegex(a.ValidationError, "owner mismatch"):
            a.activity_tables(stats)

    def test_gold_only_capture_counts_as_actual_capture(self):
        rows, issues = a.activity_tables(fixture())
        self.assertFalse(issues)
        self.assertEqual(rows[2]["event_counts"]["gemCollection"], 2)
        self.assertEqual(rows[2]["event_value_sums"]["gemCollection"], 3)

    def test_missing_descriptor_cannot_validate_input_identity(self):
        from pathlib import Path
        with self.assertRaisesRegex(a.ValidationError, "descriptor"):
            a.pinned(Path.cwd(), {"path": "not-read-without-hash"})

if __name__ == "__main__":
    unittest.main(verbosity=2)
