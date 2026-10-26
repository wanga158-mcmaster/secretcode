"""Offline native-health extraction/publication tests. No engines, games, builds or slots."""
from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import games
import health_context as hc
from core import ROOT, append_jsonl, map_path, read_jsonl


def document(events=(), map_name="00collision"):
    # Cassi and Issac are two actors with ONE draftPick. A non-contiguous B id
    # prevents deriving actor identity from team length or chronological position.
    roster = [{"id": 0, "name": "Tavi", "team": "teamA", "draftPick": 0},
              {"id": 1, "name": "Cassi", "team": "teamA", "draftPick": 2},
              {"id": 2, "name": "Issac", "team": "teamA", "draftPick": 2},
              {"id": 10, "name": "Vesper Vale", "team": "teamB", "draftPick": 1}]
    event_groups = [{"kind": "campClear", "owner": {**r, "kind": "champion"}, "entries": [{"value": "1"}]}
                    for r in (roster[0], roster[-1])]
    for actor, stamps in events:
        owner = next(r for r in roster if r["id"] == actor)
        event_groups.append({"kind": "fuelExhausted", "owner": {k: owner[k] for k in ("id", "team", "name")} | {"kind": "champion"},
                             "entries": [{"timestamp": stamp, "x": 0, "y": 0, "target": 0, "context": "0"} for stamp in stamps]})
    return {"formatVersion": "1.0", "jsonFormatVersion": "1.0",
            "match": {"outcome": "teamA", "roundsCompleted": 100, "roundsStarted": 100,
                      "engineVersion": "1.0.2", "engineBuildHash": "a" * 64,
                      "mapHash": hashlib.sha256(map_path(map_name).read_bytes()).hexdigest(),
                      "seed": "123", "metricSemanticsVersion": 1, "finalRoundPartial": False,
                      "finalInput": 500, "finalSubstep": 7, "width": 40, "height": 30},
            "teams": [{"team": "teamA", "submittedGems": "0"}, {"team": "teamB", "submittedGems": "0"}],
            "champions": [{"id": r["id"], "name": r["name"], "team": r["team"],
                           "metrics": {"earnedGold": "0", "kills": "0", "deaths": "0"}} for r in roster],
            "roster": {"champions": roster}, "events": event_groups,
            "diagnostics": {"detailedCaptureComplete": True, "botFaults": {"teamA": 0, "teamB": 0}}}


def stamp(round=19, input=289, ordinal=3268, substep=0, phase="champions"):
    return dict(round=round, input=input, ordinal=ordinal, substep=substep, phase=phase)


def write_game(run, stats, name="g", **extra):
    directory = run / "games"
    directory.mkdir(parents=True, exist_ok=True)
    replay, path = directory / (name + ".repl26a"), directory / (name + ".json")
    replay.write_bytes(b"retained replay fixture; never executed")
    path.write_text(json.dumps(stats), encoding="utf-8", newline="\n")
    parsed = games.parse_statistics(stats)
    row = dict(a="s/a@1", b="s/b@2", map="00collision", engine="1.0.2", winner=parsed["winner"], rounds=parsed["rounds"],
               A=parsed["A"], B=parsed["B"], tags={"ladder": True}, replay=hc._relative(replay))
    row.update(extra)
    append_jsonl(run / "games.jsonl", [row])
    return row, replay, path


class Extraction(unittest.TestCase):
    def test_actual_shape_actor_roster_and_zero_stamp(self):
        stats = document([(10, [stamp(0, 0, 0, 0, "draft")])])
        original = copy.deepcopy(stats)
        result = hc.extract(stats, "c" * 64)
        self.assertEqual(result["status"], "complete")
        event = result["teams"]["B"]["events"][0]
        self.assertEqual(event["owner"]["id"], 10)
        self.assertEqual(event["timestamp"], stamp(0, 0, 0, 0, "draft"))
        self.assertEqual(event["roster_join"]["draftPick"], 1)
        self.assertNotIn("x", event)
        self.assertNotIn("context", event)
        self.assertEqual(stats, original)

    def test_cassi_issac_are_distinct_actors_one_pick(self):
        result = hc.extract(document([(1, [stamp(ordinal=2)]), (2, [stamp(ordinal=1)])]), "c" * 64)
        events = result["teams"]["A"]["events"]
        self.assertEqual([r["owner"]["id"] for r in events], [2, 1])
        self.assertEqual([r["roster_join"]["draftPick"] for r in events], [2, 2])

    def test_cap_uses_native_chronology_across_owner_groups(self):
        stats = document([(0, [stamp(ordinal=9), stamp(ordinal=1)]), (1, [stamp(ordinal=2), stamp(ordinal=3)])])
        result = hc.extract(stats, "c" * 64, cap=2)
        self.assertEqual(result["status"], "partial")
        team = result["teams"]["A"]
        self.assertEqual((team["total"], team["valid"], team["kept"], team["omitted"], team["invalid"]), (4, 4, 2, 2, 0))
        self.assertEqual([x["timestamp"]["ordinal"] for x in team["events"]], [1, 2])

    def test_missing_or_malformed_identity_time_never_becomes_zero(self):
        for field, value in (("id", None), ("id", True), ("team", "teamC"), ("name", "wrong")):
            stats = document([(0, [stamp()])])
            stats["events"][-1]["owner"][field] = value
            result = hc.extract(stats, "c" * 64)
            self.assertEqual(result["status"], "partial")
            self.assertEqual(sum(x["kept"] for x in result["teams"].values()), 0)
        for field in hc.STAMP:
            stats = document([(0, [stamp()])])
            del stats["events"][-1]["entries"][0]["timestamp"][field]
            result = hc.extract(stats, "c" * 64)
            self.assertEqual(result["teams"]["A"]["invalid"], 1)
            self.assertEqual(result["teams"]["A"]["events"], [])

    def test_invalid_examples_and_match_fields_are_bounded(self):
        stats = document([(0, [{} for _ in range(100)])])
        stats["match"]["unexpected"] = "z" * 100000
        stats["formatVersion"] = "z" * 100000
        result = hc.extract(stats, "c" * 64)
        self.assertEqual(len(result["invalid_examples"]), 8)
        self.assertEqual(result["teams"]["A"]["invalid"], 100)
        self.assertNotIn("unexpected", result["native_match"])
        self.assertIsNone(result["formats"]["formatVersion"])
        self.assertLess(len(json.dumps(result)), 8000)

    def test_duplicate_roster_unknown_events_and_limits(self):
        stats = document([(2, [stamp()])])
        stats["roster"]["champions"].append(copy.deepcopy(stats["roster"]["champions"][2]))
        self.assertEqual(hc.extract(stats, "c" * 64)["teams"]["A"]["invalid"], 1)
        for cap in (0, -1, 33, True):
            with self.assertRaises(ValueError):
                hc.extract(document(), "c" * 64, cap)
        stats = document()
        del stats["events"]
        self.assertEqual(hc.extract(stats, "c" * 64)["status"], "partial")

    def test_present_invalid_match_identity_never_allows_complete_capture(self):
        for key in ("engineVersion", "engineBuildHash", "mapHash", "seed", "metricSemanticsVersion"):
            for value in (None, {}, False):
                stats = document([(0, [stamp()])])
                stats["match"][key] = value
                captured = hc.extract(stats, "c" * 64)
                self.assertEqual(captured["status"], "partial", (key, value))
                self.assertNotIn(key, captured["native_match"])
        for seed in (0, "0"):
            stats = document()
            stats["match"]["seed"] = seed
            stats["match"]["metricSemanticsVersion"] = 0
            result = hc.extract(stats, "c" * 64)
            self.assertEqual(result["status"], "complete")
            self.assertEqual(result["native_match"]["seed"], seed)
            self.assertEqual(result["native_match"]["metricSemanticsVersion"], 0)

    def test_detailed_capture_requires_explicit_true_even_without_events(self):
        for events in ((), [(0, [stamp()])]):
            for value in (False, None, 1, "true"):
                stats = document(events)
                stats["diagnostics"]["detailedCaptureComplete"] = value
                result = hc.extract(stats, "c" * 64)
                self.assertEqual(result["status"], "partial")
                self.assertIs(result["detailed_capture_complete"], False if value is False else None)
                self.assertEqual(result["teams"]["A"]["kept"], len(events))
            stats = document(events)
            del stats["diagnostics"]["detailedCaptureComplete"]
            result = hc.extract(stats, "c" * 64)
            self.assertEqual(result["status"], "partial")
            self.assertIsNone(result["detailed_capture_complete"])


class Files(unittest.TestCase):
    def setUp(self):
        self.base = Path(tempfile.mkdtemp(prefix="health-context-test-", dir=ROOT / ".cache"))
        self.run = self.base / "run"
        self.log = self.base / "shared.jsonl"

    def tearDown(self):
        assert self.base.resolve().is_relative_to((ROOT / ".cache").resolve())
        shutil.rmtree(self.base)

    def test_capture_cli_is_pure_and_idempotent(self):
        row, replay, stats = write_game(self.run, document([(10, [stamp()])]))
        source = (self.run / "games.jsonl").read_bytes()
        output = self.base / "capture.json"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hc.main(["capture", str(self.run), "--out", str(output)]), 0)
        first = output.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hc.main(["capture", str(self.run), "--out", str(output)]), 0)
        self.assertEqual(output.read_bytes(), first)
        self.assertEqual((self.run / "games.jsonl").read_bytes(), source)
        self.assertTrue(replay.is_file() and stats.is_file())
        self.assertFalse(self.log.exists())
        artifact = json.loads(first)
        self.assertEqual(hc.canonical(row), hc.canonical(artifact["rows"][0]))
        for part in artifact["producer"].values():
            self.assertEqual(hashlib.sha256(part["utf8"].encode()).hexdigest(), part["sha256"])
        self.assertEqual(artifact["rows"][0]["health_context"]["producer_sha256"],
                         {name: part["sha256"] for name, part in artifact["producer"].items()})

    def test_flagged_cleanup_only_after_durable_copies(self):
        clean, clean_replay, clean_stats = write_game(self.run, document(), "clean")
        flagged, flagged_replay, flagged_stats = write_game(self.run, document([(0, [stamp()])]), "flagged")
        unrecorded = self.run / "games" / "unrecorded.repl26a"
        unrecorded.write_bytes(b"interrupted current chunk")
        manifest = self.run / "games" / "batch-clean.txt"
        manifest.write_text(f"a b m {clean_replay} --statistics {clean_stats} --json\n", encoding="utf-8")
        pending_manifest = self.run / "games" / "batch-unrecorded.txt"
        pending_manifest.write_text(f"a b m {unrecorded} --statistics {unrecorded.with_suffix('.json')} --json\n", encoding="utf-8")
        returned = hc.finalize_ladder_run(self.run, self.log)
        published = read_jsonl(self.log)
        self.assertEqual([hc.canonical(r) for r in published], [hc.canonical(clean), hc.canonical(flagged)])
        self.assertFalse(clean_replay.exists() or clean_stats.exists() or flagged_stats.exists() or manifest.exists())
        self.assertTrue(flagged_replay.exists() and unrecorded.exists() and pending_manifest.exists())
        self.assertIsNone(returned[0]["replay"])
        self.assertEqual(returned[1]["replay"], flagged["replay"])
        first = self.log.read_bytes()
        hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(self.log.read_bytes(), first)

    def test_native_context_does_not_repair_canonical_fuel_error_metrics(self):
        row, _, _ = write_game(self.run, document([(0, [stamp()])]))
        original = copy.deepcopy(row)
        artifact = hc.capture_run(self.run)
        captured = artifact["rows"][0]
        self.assertEqual(hc.canonical(original), hc.canonical(captured))
        self.assertEqual(games.health(original, "A"), games.health(captured, "A"))
        self.assertEqual(games.perspective(original, "A"), games.perspective(captured, "A"))
        self.assertEqual(games._resume_key(row["a"], row["b"], row["map"], row["tags"]),
                         games._resume_key(captured["a"], captured["b"], captured["map"], captured["tags"]))

    def test_incomplete_capture_is_published_as_unknown_and_retains_raw(self):
        _, replay, stats = write_game(self.run, document([(0, [{}])]))
        with contextlib.redirect_stderr(io.StringIO()):
            result = hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(result[0]["health_context"]["status"], "partial")
        self.assertTrue(stats.exists() and replay.exists())
        self.assertEqual(len(read_jsonl(self.log)), 1)

    def test_match_identity_or_extractor_failure_retains_raw_without_metric_changes(self):
        stats_doc = document()
        stats_doc["match"]["seed"] = None
        original, replay, stats = write_game(self.run, stats_doc)
        with contextlib.redirect_stderr(io.StringIO()):
            result = hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(result[0]["health_context"]["status"], "partial")
        self.assertEqual(hc.canonical(result[0]), hc.canonical(original))
        self.assertTrue(replay.exists() and stats.exists())
        other = self.base / "extractor-error"
        _, replay2, stats2 = write_game(other, document())
        with mock.patch.object(hc, "extract", side_effect=RuntimeError("unexpected extraction failure")):
            artifact = hc.capture_run(other)
        self.assertEqual(artifact["rows"][0]["health_context"]["status"], "unavailable")
        self.assertTrue(replay2.exists() and stats2.exists())

    def test_swapped_or_unknown_map_cannot_authorize_cleanup(self):
        for name, stats_doc, declared in (("swapped", document(map_name="00ambush"), "00collision"),
                                           ("unknown", document(), "not-a-real-map")):
            run = self.base / name
            original, replay, stats = write_game(run, stats_doc, map=declared)
            with contextlib.redirect_stderr(io.StringIO()):
                rows = hc.finalize_ladder_run(run, self.log)
            self.assertEqual(rows[0]["health_context"]["status"], "unavailable")
            self.assertEqual(hc.canonical(rows[0]), hc.canonical(original))
            self.assertTrue(replay.exists() and stats.exists())

    def test_incomplete_native_capture_retains_apparent_zero_raw_files(self):
        stats_doc = document()
        stats_doc["diagnostics"]["detailedCaptureComplete"] = False
        _, replay, stats = write_game(self.run, stats_doc)
        with contextlib.redirect_stderr(io.StringIO()):
            rows = hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(rows[0]["health_context"]["status"], "partial")
        self.assertTrue(replay.exists() and stats.exists())

    def test_missing_stats_or_outside_imported_path_is_unavailable(self):
        _, replay, stats = write_game(self.run, document())
        stats.unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            result = hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(result[0]["health_context"]["status"], "unavailable")
        self.assertIsNone(result[0]["health_context"]["teams"])
        self.assertTrue(replay.exists())
        outside = self.base / "outside.repl26a"
        outside.write_bytes(b"untouched")
        other = self.base / "other"
        append_jsonl(other / "games.jsonl", [{**result[0], "replay": str(outside)}])
        captured = hc.capture_run(other)
        self.assertEqual(captured["rows"][0]["health_context"]["status"], "unavailable")
        self.assertEqual(outside.read_bytes(), b"untouched")

    def test_run_local_publication_failure_has_no_cleanup_or_shared_append(self):
        _, replay, stats = write_game(self.run, document())
        with mock.patch.object(hc, "_write_artifact", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                hc.finalize_ladder_run(self.run, self.log)
        self.assertTrue(replay.exists() and stats.exists())
        self.assertFalse(self.log.exists())

    def test_partial_shared_append_retry_never_duplicates_or_drops_remainder(self):
        write_game(self.run, document(), "one")
        write_game(self.run, document(), "two")
        original_append = hc._append_rows_unlocked
        def partial(path, rows):
            original_append(path, rows[:1])
            raise OSError("interrupted append")
        with mock.patch.object(hc, "_append_rows_unlocked", side_effect=partial):
            with self.assertRaises(OSError):
                hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(len(read_jsonl(self.log)), 1)
        self.assertEqual(len(list((self.run / "games").glob("*.json"))), 2)
        hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(len(read_jsonl(self.log)), 2)
        first = self.log.read_bytes()
        hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(self.log.read_bytes(), first)

    def test_old_rows_and_legitimate_same_game_retry_survive(self):
        old = {"a": "s/a@1", "b": "s/b@2", "map": "m0", "winner": "error"}
        append_jsonl(self.log, [old])
        prefix = self.log.read_bytes()
        write_game(self.run, document([(0, [stamp()])]))
        hc.finalize_ladder_run(self.run, self.log)
        other = self.base / "another-run"
        write_game(other, document([(0, [stamp()])]))
        hc.finalize_ladder_run(other, self.log)
        self.assertTrue(self.log.read_bytes().startswith(prefix))
        self.assertEqual(len(read_jsonl(self.log)), 3)
        self.assertEqual(read_jsonl(self.log)[0], old)

    def test_torn_raw_tail_salvages_complete_prefix_without_cleanup(self):
        _, replay, stats = write_game(self.run, document())
        with (self.run / "games.jsonl").open("ab") as fh:
            fh.write(b'{"a":')
        with contextlib.redirect_stderr(io.StringIO()):
            rows = hc.finalize_ladder_run(self.run, self.log)
        self.assertEqual(len(rows), 1)
        self.assertTrue(replay.exists() and stats.exists())
        self.assertTrue(json.loads((self.run / hc.ARTIFACT).read_bytes())["source_log_issues"])

    def test_changed_source_after_capture_is_not_deleted(self):
        _, replay, stats = write_game(self.run, document())
        hc.capture_run(self.run)
        stats.write_bytes(b"changed after capture")
        with self.assertRaisesRegex(hc.ArenaError, "changed after capture"):
            hc.finalize_ladder_run(self.run, self.log)
        self.assertTrue(replay.exists() and stats.exists())
        self.assertEqual(len(read_jsonl(self.log)), 1)


if __name__ == "__main__":
    unittest.main()
