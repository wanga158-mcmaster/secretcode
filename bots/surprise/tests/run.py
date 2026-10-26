"""Focused private-suite infrastructure tests; no games, builds or machine slots.

  arena test surprise/_greed
  python -B bots/surprise/tests/run.py [_greed]

Gameplay fixtures belong here when the first new behavior is implemented. These tests exercise
real arena selection, source overrides, version keys and reports with fake native execution.
"""
from __future__ import annotations

import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SUITE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE / "tools"))
import lab  # noqa: E402


def recipe():
    return dict(schema_version=1, question="Can the next mechanism improve this position?",
                maps="00collision,01big_world", seeds=[0],
                variants=[dict(name="same", set={})],
                opponents=[dict(name="reference", bot="anchors/main-v39", set={})])


def sources(bot):
    return {bot.path / "src/main.cpp":
            b"inline constexpr int knob = 0;\ninline constexpr int noise_burn = 0;\n"}


def entrants(items):
    out = []
    for bot, settings, label, contents in items:
        changed = lab.core.apply_overrides(bot, contents, settings) if settings else contents
        out.append(lab.core.Entrant(bot, lab.core.version_of(changed, settings), bot.path,
                                    True, overrides=tuple(sorted(settings.items())), label=label))
    return out


def native_rows(specs):
    rows = []
    for spec in specs:
        row = lab.games._base_row(spec, spec.map)
        row.update(winner=spec.tags["subject"], rounds=500)
        for side in "AB":
            row[side].update(gems=1000 if side == spec.tags["subject"] else 200,
                             gold=2000, clears=20, fuel_max=65000)
        rows.append(row)
    return rows


class InfrastructureTests(unittest.TestCase):
    def plan(self, data=None):
        with mock.patch.object(lab.core, "read_closure", side_effect=sources):
            return lab.make_plan(data or recipe())

    def test_private_bot_requires_exact_id(self):
        self.assertEqual(lab.core.bot(lab.SUBJECT).id, lab.SUBJECT)
        self.assertNotIn(lab.SUBJECT, {b.id for b in lab.core.select("all")})
        for selector in ("surprise", "surprise/*", "*/_greed"):
            with self.subTest(selector=selector), self.assertRaises(lab.core.ArenaError):
                lab.core.select(selector)

    def test_owned_source_closure_and_lf(self):
        closure = lab.core.read_closure(lab.core.bot(lab.SUBJECT))
        self.assertTrue(closure)
        for path, data in closure.items():
            self.assertTrue(path.is_relative_to(SUITE), str(path))
            self.assertNotIn(b"\r", data, str(path))

    def test_real_working_recipe_resolves(self):
        data = json.loads(lab.DEFAULT_SPEC.read_text(encoding="utf-8"))
        plan = lab.make_plan(data)
        self.assertEqual([m["name"] for m in plan.record["maps"]],
                         lab.core.map_set(data.get("maps", "pool")))
        self.assertEqual(plan.record["sides"], ["A", "B"])

    def test_one_source_capture_for_all_variants_and_opponent_overrides(self):
        data = recipe()
        data["variants"].append(dict(name="changed", set={"knob": 1}))
        data["opponents"].append(dict(name="different-response", bot="anchors/main-v39", set={"knob": 2}))
        with mock.patch.object(lab.core, "read_closure", side_effect=sources) as read:
            plan = lab.make_plan(data)
        self.assertEqual(read.call_count, 2)
        self.assertIs(plan.items[0][3], plan.items[2][3])
        keys = [b["key"] for b in plan.record["bindings"]]
        self.assertEqual(keys[0], keys[1])
        self.assertNotEqual(keys[0], keys[2])
        self.assertNotEqual(keys[3], keys[4])

    def test_map_side_seed_and_opponent_keys_pair(self):
        data = recipe()
        data["seeds"] = [0, 7]
        data["opponents"].append(dict(name="other", bot="anchors/main-v38", set={}))
        plan = self.plan(data)
        specs = lab.game_specs(plan, entrants(plan.items))
        self.assertEqual(len(specs), 32)
        for base, same in zip(specs[::2], specs[1::2], strict=True):
            self.assertEqual((base.a.key, base.b.key, base.map), (same.a.key, same.b.key, same.map))
            self.assertEqual(base.tags | {"variant": "same"}, same.tags)
        self.assertEqual({g.tags["subject"] for g in specs}, {"A", "B"})

    def test_invalid_knob_refused_before_build(self):
        data = recipe()
        data["variants"] = [dict(name="typo", set={"unknown_knob": 1})]
        with mock.patch.object(lab.core, "prepare_many") as build:
            with self.assertRaises(lab.core.ArenaError):
                self.plan(data)
        build.assert_not_called()

    def test_invalid_recipe_values(self):
        edits = [dict(schema_version=True), dict(question=""), dict(seeds=[0, 0]),
                 dict(seeds=[True]), dict(seeds=[-1]), dict(unrecognized=True),
                 dict(variants=[dict(name="base", set={})]),
                 dict(opponents=[dict(name="wildcard", bot="anchors/main-v3*", set={})])]
        for edit in edits:
            with self.subTest(edit=edit), self.assertRaises(lab.core.ArenaError):
                self.plan(recipe() | edit)

    def test_duplicate_exact_opponents_are_not_extra_samples(self):
        data = recipe()
        data["opponents"].append(dict(name="same-again", bot="anchors/main-v39", set={}))
        with self.assertRaisesRegex(lab.core.ArenaError, "duplicate exact opponent"):
            self.plan(data)

    def test_seed_conflict_refused(self):
        data = recipe()
        data["seeds"] = [3]
        data["variants"] = [dict(name="conflict", set={"noise_burn": 1})]
        with self.assertRaisesRegex(lab.core.ArenaError, "seed knob"):
            self.plan(data)

    def test_health_including_opponent_blocks_success(self):
        plan = self.plan()
        rows = native_rows(lab.game_specs(plan, entrants(plan.items)))
        self.assertTrue(lab.check_rows(rows, plan.record)["ok"])
        rows[0]["B"]["exhausted"] = 1
        self.assertFalse(lab.check_rows(rows, plan.record)["ok"])

    def test_unchanged_build_mismatch_and_truncation_are_failures(self):
        plan = self.plan()
        original = native_rows(lab.game_specs(plan, entrants(plan.items)))
        rows = copy.deepcopy(original)
        rows[1]["A"]["gold"] += 1
        self.assertTrue(any("mismatch" in p for p in lab.check_rows(rows, plan.record)["problems"]))
        rows = copy.deepcopy(original)
        rows[0]["truncated"] = True
        self.assertFalse(lab.check_rows(rows, plan.record)["ok"])
        self.assertFalse(lab.check_rows(original[:-1], plan.record)["ok"])

    def test_local_run_records_identity_and_refuses_changed_resume(self):
        scratch = lab.ROOT / ".cache/surprise/tests"
        scratch.mkdir(parents=True, exist_ok=True)
        plan = self.plan()
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            out = Path(directory)
            def play(specs, output, **kw):
                self.assertEqual(kw, dict(keep_replays="all", resume=True))
                return native_rows(specs)
            with (mock.patch.object(lab.core, "prepare_many", side_effect=entrants) as build,
                  mock.patch.object(lab.games, "play", side_effect=play) as native,
                  contextlib.redirect_stdout(io.StringIO())):
                self.assertEqual(lab.run_plan(plan, out), 0)
                self.assertTrue((out / "report.md").is_file())
                self.assertEqual(json.loads((out / "check.json").read_text())["identical_control_pairs"], 4)
                self.assertEqual(lab.run_plan(plan, out), 0)
                changed = self.plan(recipe() | {"question": "A different question"})
                with self.assertRaisesRegex(lab.core.ArenaError, "inputs changed"):
                    lab.run_plan(changed, out)
                self.assertEqual(build.call_count, 2)
                self.assertEqual(native.call_count, 2)

    def test_failed_build_never_launches_games(self):
        scratch = lab.ROOT / ".cache/surprise/tests"
        scratch.mkdir(parents=True, exist_ok=True)
        plan = self.plan()
        built = entrants(plan.items)
        built[0].ok, built[0].log = False, "test build failure"
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            with (mock.patch.object(lab.core, "prepare_many", return_value=built),
                  mock.patch.object(lab.games, "play") as native,
                  contextlib.redirect_stderr(io.StringIO())):
                self.assertEqual(lab.run_plan(plan, Path(directory)), 1)
                native.assert_not_called()

    def test_output_cannot_write_another_owners_run(self):
        with self.assertRaisesRegex(lab.core.ArenaError, "private output"):
            lab.run_plan(self.plan(), lab.ROOT / ".cache/arena/do-not-write")


if __name__ == "__main__":
    # arena passes selected bot names; fixtures here currently cover the one private bot.
    unexpected = [a for a in sys.argv[1:] if a != "_greed"]
    if unexpected:
        raise SystemExit("unsupported test arguments: " + " ".join(unexpected))
    unittest.main(argv=[sys.argv[0]], verbosity=2)
