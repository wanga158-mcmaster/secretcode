"""Focused tests for the retained-statistics liveness screen; no games or builds."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("main_liveness", ROOT / "bots/main/tools/liveness.py")
liveness = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(liveness)


def timeline(owner, metric, change=0, final=200):
    runs = [{"duration": final+1, "value": "0"}] if not change else [
        {"duration": change, "value": "0"}, {"duration": final+1-change, "value": "1"}]
    return {"owner": owner, "metric": metric, "sampleCount": final+1, "runs": runs}


def statistics(final=200, own_change=0, enemy_change=199):
    own = {"kind": "champion", "id": 7, "team": "teamB"}
    enemy = {"kind": "champion", "id": 0, "team": "teamA"}
    tls = [timeline(own, metric, own_change if metric == "earnedGold" else 0, final)
           for metric in liveness.CHAMPION_METRICS]
    tls += [timeline(enemy, "monsterDamageDone", enemy_change, final)]
    return {"match": {"roundsCompleted": final, "roundsStarted": final, "finalRoundPartial": False},
            "roster": {"champions": [{"id": 7, "team": "teamB"}, {"id": 0, "team": "teamA"}]},
            "championTimelines": tls,
            "teamTimelines": [timeline("teamB", "submittedGems", 0, final), timeline("teamA", "submittedGems", enemy_change, final)]}


class LivenessTests(unittest.TestCase):
    def test_rle_boundary_and_late_progress(self):
        report = liveness.screen_statistics(statistics(own_change=179), "B")
        self.assertEqual((report["status"], report["last_change_round"], report["tail_rounds"]), ("NO_SUSPECT", 179, 21))

    def test_other_team_activity_is_not_ours(self):
        report = liveness.screen_statistics(statistics(), "B")
        self.assertEqual((report["status"], report["tail_rounds"]), ("SUSPECT", 200))
        self.assertEqual(report["champion_ids"], [7])

    def test_missing_metric_is_unavailable(self):
        stats = statistics()
        stats["championTimelines"] = [t for t in stats["championTimelines"] if t["metric"] != "healingReceived"]
        report = liveness.screen_statistics(stats, "B")
        self.assertEqual(report["status"], "UNAVAILABLE")
        self.assertTrue(any("healingReceived" in message for message in report["missing"]))

    def test_missing_report_is_unavailable(self):
        self.assertEqual(liveness.screen_statistics({}, "B")["status"], "UNAVAILABLE")

    def test_channel_timer_changes_do_not_confirm_a_stall(self):
        stats = statistics()
        stats["championTimelines"].append(timeline({"kind": "champion", "id": 7, "team": "teamB"}, "channelTimer", 199))
        report = liveness.screen_statistics(stats, "B")
        self.assertEqual(report["status"], "SUSPECT")
        self.assertEqual(report["classification"], "unconfirmed_statistics_tail")
        self.assertTrue(any("channel" in text for text in report["confirmation_required"]))
        self.assertNotIn("confirmed_stall", report)

    def test_adjacent_equal_runs_are_not_progress(self):
        tl = {"sampleCount": 201, "runs": [{"duration": 180, "value": "0"}, {"duration": 21, "value": "0.0"}]}
        self.assertEqual(liveness.timeline_last_change(tl, 200)["last_change_round"], 0)

    def test_partial_final_round_is_included(self):
        stats = statistics(final=201, own_change=201)
        stats["match"]["roundsCompleted"] = 200
        stats["match"]["finalRoundPartial"] = True
        report = liveness.screen_statistics(stats, "B")
        self.assertEqual((report["status"], report["final_observed_round"], report["tail_rounds"]), ("NO_SUSPECT", 201, 0))

    def test_invalid_coverage_and_counters_are_unavailable(self):
        for mutation in ("duration", "decrease", "nonfinite", "owner"):
            with self.subTest(mutation=mutation):
                stats = statistics(own_change=100)
                own = stats["championTimelines"][0]
                if mutation == "duration": own["runs"][-1]["duration"] -= 1
                if mutation == "decrease": own["runs"][0]["value"] = "2"
                if mutation == "nonfinite": own["runs"][0]["value"] = "NaN"
                if mutation == "owner": own["owner"] = {**own["owner"], "team": "teamA"}
                self.assertEqual(liveness.screen_statistics(stats, "B")["status"], "UNAVAILABLE")

    def test_file_coverage_and_variant_filter(self):
        scratch = ROOT / ".cache/main/liveness107/tests"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="main-liveness-", dir=scratch) as directory:
            root = Path(directory)
            (root / "games").mkdir()
            (root / "games" / "present.json").write_text(json.dumps(statistics()), encoding="utf-8")
            rows = [{"tags": {"subject": "B", "variant": "base"}, "replay": "games/present.repl26a"},
                    {"tags": {"subject": "B", "variant": "wait"}, "replay": "games/missing.repl26a"}]
            (root / "games.jsonl").write_text("\n".join(json.dumps(r) for r in rows)+"\n", encoding="utf-8")
            all_rows = liveness.screen_run(root)
            self.assertFalse(all_rows["coverage_complete"])
            self.assertEqual(all_rows["counts"], {"SUSPECT": 1, "UNAVAILABLE": 1})
            base = liveness.screen_run(root, "base")
            self.assertTrue(base["coverage_complete"])
            self.assertEqual((base["selected_rows"], base["rows_skipped_by_variant"]), (1, 1))

    def test_missing_subject_is_unavailable(self):
        self.assertEqual(liveness.screen_statistics(statistics(), None)["status"], "UNAVAILABLE")


if __name__ == "__main__":
    unittest.main()
