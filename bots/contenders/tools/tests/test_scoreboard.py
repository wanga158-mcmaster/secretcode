"""Synthetic guards for the read-only six-map policy tournament scoreboard.

Run with the arena Python: python -B -I -X utf8 <this file> [-v].
No game, build, replay, decoder or live repository result is required.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import cohort  # noqa: E402
import scoreboard as sb  # noqa: E402


MAPS = ("00ambush", "00eight", "00gem-fields", "00lost-world", "01big-world", "01corridors")
ANCHOR = "anchors/main-v42@9fc5d7469074"
STARTER = "anchors/starter-cpp@3785135fc7c7"


def game(*, map_name=MAPS[0], side="A", opponent=ANCHOR, subject="contenders/example@base",
         variant="base", phase="sweep", engine="1.0.3", seed=0, result="win",
         rounds=900, gems=1000, other_gems=200, truncated=False):
    """An explicit subject-side native arena row, with independently clean teams."""
    other = "B" if side == "A" else "A"
    clean = {"faults": 0, "exhausted": 0, "errors": 0, "fuel_at_budget": 0,
             "clears": 4, "fuel_max": 80000, "gems": other_gems, "gold": 2000}
    winner = side if result == "win" else other if result == "loss" else result
    return {side.lower(): subject, other.lower(): opponent, "engine": engine,
            "map": map_name, "winner": winner, "rounds": rounds, "truncated": truncated,
            side: dict(clean, gems=gems), other: dict(clean),
            "tags": {"subject": side, "variant": variant, "phase": phase, "seed": seed}}


def training(**changes):
    return [game(map_name=map_name, side=side, opponent=opponent, **changes)
            for opponent in (ANCHOR, STARTER) for map_name in MAPS for side in "AB"]


class Scoreboard(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.serial = 0

    def saved(self, rows):
        self.serial += 1
        path = self.root / f"run-{self.serial}.jsonl"
        path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8", newline="\n")
        return path

    def group(self, rows):
        groups, _ = cohort.collect([self.saved(rows)])
        self.assertEqual(len(groups), 1)
        return groups[0]

    def package(self, rows):
        data = sb.report([self.saved(rows)])
        self.assertEqual(len(data["packages"]), 1)
        return data["packages"][0]

    def test_protocol_is_24_training_keys_and_12_per_exact_opponent(self):
        data = sb.report([self.saved(training())])
        self.assertEqual(data["scope"]["maps"], list(MAPS))
        self.assertEqual(data["scope"]["opponents"], [ANCHOR, STARTER])
        self.assertEqual(data["scope"]["keys_per_package"], 24)
        self.assertEqual(data["scope"]["observed_finished_training_keys"], 24)
        self.assertEqual(data["scope"]["initial_tournament_protocol"]["all_owner_games"], 288)
        package = data["packages"][0]
        self.assertTrue(package["training_complete"])
        self.assertEqual(package["finished_training_keys"], 24)
        self.assertEqual(package["missing_keys"], [])
        self.assertNotIn("full_pool_both_sides", package)
        for opponent in (ANCHOR, STARTER):
            summary = package["opponents"][opponent]
            self.assertEqual((summary["expected_keys"], summary["finished"], summary["wins"]), (12, 12, 12))
            self.assertEqual(summary["missing_keys"], [])

    def test_partial_or_extra_rows_cannot_be_called_complete_training(self):
        partial = self.package(training()[:-1])
        self.assertFalse(partial["training_complete"])
        self.assertEqual(partial["finished_training_keys"], 23)
        self.assertEqual(partial["missing_keys"], [(STARTER, MAPS[-1], "B", 0)])
        for extra in (game(map_name="00collision"), game(seed=1), game(opponent="anchors/main-v42@other")):
            with self.subTest(extra=extra["map"], opponent=extra["b"], seed=extra["tags"]["seed"]):
                package = self.package(training() + [extra])
                self.assertEqual(package["finished_training_keys"], 24)
                self.assertFalse(package["training_complete"])
                self.assertEqual(len(package["outside_training_cells"]), 1)

    def test_exact_subject_engine_variant_and_phase_remain_separate(self):
        rows = [game(), game(subject="contenders/example@other"), game(engine="1.0.2"),
                game(variant="schedule"), game(phase="confirm")]
        data = sb.report([self.saved(rows)])
        self.assertEqual(len(data["packages"]), 5)
        self.assertEqual({(p["subject"], p["engine"], p["variant"], p["phase"]) for p in data["packages"]},
                         {("contenders/example@base", "1.0.3", "base", "sweep"),
                          ("contenders/example@other", "1.0.3", "base", "sweep"),
                          ("contenders/example@base", "1.0.2", "base", "sweep"),
                          ("contenders/example@base", "1.0.3", "schedule", "sweep"),
                          ("contenders/example@base", "1.0.3", "base", "confirm")})
        self.assertTrue(all(package["finished_training_keys"] == 1 for package in data["packages"]))

    def test_pairing_requires_exact_opponent_engine_side_map_and_seed(self):
        baseline = self.group([game()])
        for changes in ({"opponent": "anchors/main-v42@changed"}, {"engine": "1.0.2"},
                        {"engine": None}, {"side": "B"}, {"map_name": MAPS[1]}, {"seed": 1}):
            with self.subTest(changes=changes):
                arm = self.group([game(subject="contenders/example@arm", variant="arm", **changes)])
                paired = sb.paired_training(baseline, arm)
                self.assertEqual(paired["n"], 0)
                self.assertIn("refusal", paired)
        unknown = self.group([game(engine=None)])
        self.assertEqual(sb.paired_training(unknown, unknown)["n"], 0)

    def test_paired_win_and_margin_changes_are_subject_side_correct(self):
        baseline = self.group([game(side="B", result="loss", gems=100, other_gems=500)])
        arm = self.group([game(side="B", subject="contenders/example@arm", variant="arm",
                               result="win", gems=1100, other_gems=200)])
        paired = sb.paired_training(baseline, arm)
        self.assertEqual(paired["n"], 1)
        result = paired["opponents"][ANCHOR]
        self.assertEqual((result["gained"], result["lost"]), (1, 0))
        self.assertEqual(result["score_delta"]["mean"], 1)
        self.assertEqual(result["gem_margin_delta"]["mean"], 1300)

    def test_only_terminal_1000_gem_starter_wins_are_completed(self):
        rows = [game(opponent=STARTER, side="B", rounds=450, gems=1000),
                game(opponent=STARTER, map_name=MAPS[1], rounds=400, gems=1000, truncated=True),
                game(opponent=STARTER, map_name=MAPS[2], rounds=500, gems=999),
                game(opponent=STARTER, map_name=MAPS[3], rounds=600, gems=1100, result="loss"),
                game(opponent=STARTER, map_name=MAPS[4], rounds=700, gems=1100, result="error")]
        summary = self.package(rows)["opponents"][STARTER]
        completion = summary["completion"]
        self.assertEqual((summary["observed"], summary["finished"]), (5, 3))
        self.assertEqual((completion["completed"], completion["not_completed"]), (1, 4))
        self.assertEqual(completion["completed_rounds"], {"n": 1, "mean": 450, "se": None})
        self.assertEqual(completion["capped_rounds"]["n"], 3)
        self.assertEqual(completion["capped_rounds"]["mean"], (450 + 2000 + 2000) / 3)

    def test_completed_only_mean_is_distinct_from_paired_capped_speedrun(self):
        base_rows = [game(opponent=STARTER, map_name=MAPS[0], rounds=1000),
                     game(opponent=STARTER, map_name=MAPS[1], rounds=1000)]
        arm_rows = [game(opponent=STARTER, map_name=MAPS[0], rounds=500,
                         subject="contenders/example@arm", variant="arm"),
                    game(opponent=STARTER, map_name=MAPS[1], rounds=2000, gems=900, result="loss",
                         subject="contenders/example@arm", variant="arm")]
        summary = self.package(arm_rows)["opponents"][STARTER]["completion"]
        self.assertEqual(summary["completed_rounds"]["mean"], 500)
        self.assertEqual(summary["capped_rounds"]["mean"], 1250)
        paired = sb.paired_training(self.group(base_rows), self.group(arm_rows))
        completed = paired["opponents"][STARTER]["starter_completion"]
        self.assertEqual((completed["baseline_completed"], completed["candidate_completed"]), (2, 1))
        self.assertEqual(completed["capped_round_delta"]["mean"], 250)

    def test_health_reads_subject_b_even_when_opponent_a_is_healthy(self):
        row = game(side="B")
        row["B"].update(errors=2, clears=0)
        package = self.package([row])
        health = package["training_health"]
        self.assertEqual((health["errors"], health["stalls"], health["status"]), (2, 1, "failed"))
        self.assertEqual(package["opponents"][ANCHOR]["health"]["errors"], 2)
        clean = game(side="B")
        clean["A"].update(exhausted=3, clears=0)
        self.assertEqual(self.package([clean])["training_health"]["status"], "clean")
        unknown = game()
        del unknown["A"]["faults"]
        self.assertEqual(self.package([unknown])["training_health"]["status"], "unknown")

    def test_identical_repeated_rows_dedupe_but_conflicting_cell_is_excluded(self):
        rows = training()
        duplicate = self.package(rows + [copy.deepcopy(rows[0])])
        self.assertEqual((duplicate["finished_training_keys"], duplicate["duplicate_rows"]), (24, 1))
        self.assertTrue(duplicate["training_complete"])
        changed = copy.deepcopy(rows[0])
        changed["winner"] = "B"
        conflicted = self.package(rows + [changed, copy.deepcopy(rows[0])])
        self.assertEqual(conflicted["finished_training_keys"], 23)
        self.assertEqual(conflicted["conflicting_cells"], [(ANCHOR, MAPS[0], "A", 0)])
        self.assertFalse(conflicted["training_complete"])

    def test_ambiguous_baseline_sources_do_not_pick_one(self):
        rows = [game(), game(subject="contenders/example@second-base"),
                game(subject="contenders/example@arm", variant="arm")]
        data = sb.report([self.saved(rows)])
        self.assertEqual(len(data["paired"]), 1)
        self.assertEqual(data["paired"][0]["n"], 0)
        self.assertEqual(data["paired"][0]["refusal"], "baseline missing or ambiguous")


if __name__ == "__main__":
    unittest.main()
