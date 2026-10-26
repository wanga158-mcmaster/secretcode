"""Unit tests for tools/arena/report.py (report, map, prune) and scaffold.py (new suite/bot).

    python tools/arena/tests/test_report.py [-v]

Synthetic game rows, statistics, replay bundles and caches live in temporary directories under
.cache/arena-tests/; real repository files (maps/geometry.json, anchor sources) are only read.
"""
from __future__ import annotations

import contextlib
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import shutil
import time
import threading
import unittest
import uuid
import zipfile
from pathlib import Path
from unittest.mock import patch

ARENA = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARENA))
import core  # noqa: E402
import report  # noqa: E402
import scaffold  # noqa: E402
from stats import wilson  # noqa: E402

SCRATCH = core.ROOT / ".cache" / "arena-tests"
KEYS = ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted", "errors", "fuel_max", "fuel_at_budget")
HAVE_GEOMETRY = (core.MAPS / "geometry.json").is_file()


def block(**values) -> dict:
    out = dict.fromkeys(KEYS, 0)
    out.update(clears=5, gems=100)
    out.update(values)
    return out


def row(winner="A", a="main/economy@v1", b="anchors/starter-cpp@s1", map_name="00collision", subject="A",
        A=None, B=None, **extra) -> dict:
    r = {"ts": "2026-10-21T00:00:00Z", "map": map_name, "a": a, "b": b, "a_name": a.split("@")[0],
         "b_name": b.split("@")[0], "winner": winner, "rounds": 1000, "A": block(**(A or {})),
         "B": block(**(B or {})), "replay": None, "tags": {"subject": subject} if subject else {}}
    r.update(extra)
    return r


@contextlib.contextmanager
def scratch():
    """Use inherited Windows ACLs; tempfile's private directory ACL blocks this sandbox."""
    SCRATCH.mkdir(parents=True, exist_ok=True)
    directory = SCRATCH / ("report-" + uuid.uuid4().hex)
    directory.mkdir()
    try:
        yield str(directory)
    finally:
        shutil.rmtree(directory)


def fuel_stream(budgets, turns) -> bytes:
    return b"a62leuf\x00" + b"\x00\x00\x00\x01\x00" + struct.pack(f"<{len(budgets) + len(turns)}Q", *budgets, *turns)


BUDGETS = (1000000, 100000, 200000, 100000, 1000000, 100000, 200000, 100000)


def synthetic_stats() -> dict:
    """Team A: Tavi#0 and Varg#1; team B: Cassi#2 and Issac#3 (one pick, two bodies)."""
    def champ(cid, team, name, **m):
        metrics = {"earnedGold": "100", "kills": "1", "deaths": "0", "monsterDamageDone": 50.5,
                   "championDamageDone": 20.0, "championDamageTaken": 5.0, "monsterDamageTaken": 7.0,
                   "assists": "2", "submittedGems": "30"}
        metrics.update(m)
        return {"id": cid, "team": team, "name": name, "metrics": metrics}

    def owner(cid, team, name):
        return {"id": cid, "kind": "champion", "name": name, "team": team}

    def ts(r, o=0):
        return {"round": r, "ordinal": o, "input": 0, "substep": 0, "phase": "champions"}
    return {
        "formatVersion": "1.0", "jsonFormatVersion": "1.0",
        "match": {"outcome": "teamA", "roundsCompleted": 300, "roundsStarted": 301, "finalRoundPartial": True,
                  "width": 41, "height": 41, "engineVersion": "1.0.2", "mapHash": "no-such-map"},
        "teams": [{"team": "teamA", "submittedGems": "1000"}, {"team": "teamB", "submittedGems": "40"}],
        "roster": {"champions": [{"id": 0, "team": "teamA", "name": "Tavi", "draftPick": 0},
                                 {"id": 1, "team": "teamA", "name": "Varg", "draftPick": 2},
                                 {"id": 2, "team": "teamB", "name": "Cassi", "draftPick": 1},
                                 {"id": 3, "team": "teamB", "name": "Issac", "draftPick": 1}]},
        "champions": [champ(0, "teamA", "Tavi", earnedGold="700", kills="3"), champ(1, "teamA", "Varg"),
                      champ(2, "teamB", "Cassi", deaths="2"), champ(3, "teamB", "Issac", deaths="1")],
        "diagnostics": {"botFaults": {"teamA": 0, "teamB": 2}, "codes": [], "detailedCaptureComplete": True},
        "events": [
            {"kind": "campClear", "owner": owner(0, "teamA", "Tavi"),
             "entries": [{"timestamp": ts(10), "value": "1", "target": {"id": 4, "kind": "campGroup"}}] * 3},
            {"kind": "campClear", "owner": owner(2, "teamB", "Cassi"), "entries": []},
            {"kind": "fuelExhausted", "owner": owner(3, "teamB", "Issac"), "entries": [{"timestamp": ts(50)}] * 4},
            {"kind": "death", "owner": owner(2, "teamB", "Cassi"),
             "entries": [{"timestamp": ts(100, 5), "context": "champion", "x": 5, "y": 6, "value": "1",
                          "target": {"id": 0, "kind": "champion", "name": "Tavi", "team": "teamA"}},
                         {"timestamp": ts(200, 9), "context": "camp", "x": 7, "y": 8, "value": "1",
                          "target": {"id": 4, "kind": "camp"}}]},
            {"kind": "death", "owner": owner(3, "teamB", "Issac"),
             "entries": [{"timestamp": ts(100, 6), "context": "champion", "x": 9, "y": 9, "value": "1",
                          "target": {"id": 1, "kind": "champion", "name": "Varg", "team": "teamA"}}]},
            {"kind": "gemLoss", "owner": owner(2, "teamB", "Cassi"),
             "entries": [{"timestamp": ts(100, 7), "value": "12"}]},
        ],
    }


class RowViews(unittest.TestCase):
    def test_results_margin_and_flags(self):
        v = report.side_view(row("A", A={"gems": 300}, B={"gems": 120}), "A")
        self.assertEqual((v["result"], v["margin"], v["key"], v["name"], v["opp_name"]),
                         ("win", 180, "main/economy@v1", "main/economy", "anchors/starter-cpp"))
        self.assertEqual(report.side_view(row("A"), "B")["result"], "loss")
        self.assertEqual(report.side_view(row("draw"), "B")["result"], "draw")
        failed = report.side_view(row("error", error="no statistics"), "A")
        self.assertEqual((failed["result"], failed["flags"]), ("error", ["error"]))
        sick = report.side_view(row("B", A={"faults": 1, "exhausted": 2, "errors": 3, "clears": 0}), "A")
        self.assertEqual(sick["flags"], ["faults 1", "exhausted 2", "errors 3", "stall"])
        self.assertEqual(report.side_view(row("A", A={"fuel_at_budget": 4}), "A")["flags"], ["suspended 4"])
        cut = report.side_view(row("draw", truncated=True, A={"clears": 0}), "A")
        self.assertFalse(cut["stall"])

    def test_fallback_mirrors_games_perspective(self):
        rows = [row("A"), row("B", subject="B"), row("draw", truncated=True), row("error"),
                row("skipped", subject=None), row("A", A={"clears": 0, "fuel_at_budget": 3})]
        for r in rows:
            for side in "AB":
                self.assertEqual(report.perspective(r, side), report._perspective(r, side))
                self.assertEqual(report.row_health(r, side), report._health(r, side))

    def test_subject_resolution(self):
        r = row("A", subject="B")
        self.assertEqual(report.subject_sides(r, None), ["B"])
        self.assertEqual(report.subject_sides(row(subject=None), None), ["A", "B"])
        self.assertEqual(report.subject_sides(r, "a"), ["A"])
        self.assertEqual(report.subject_sides(r, "main/economy"), ["A"])
        self.assertEqual(report.subject_sides(r, "anchors/*"), ["B"])
        self.assertEqual(report.subject_sides(r, "anchors/starter-cpp@s1"), ["B"])
        self.assertEqual(report.subject_sides(r, "nobody"), [])

    def test_aggregate_counts_score_and_health(self):
        views = [report.side_view(r, "A") for r in (
            row("A", A={"gems": 200}, B={"gems": 100}), row("A", A={"gems": 300}, B={"gems": 0}),
            row("A", A={"faults": 1}), row("B", A={"gems": 0}, B={"gems": 100}),
            row("draw", A={"exhausted": 2, "fuel_max": 99000}), row("error"),
            row("draw", truncated=True, A={"gems": 0, "fuel_at_budget": 5}))]
        a = report.aggregate(views)
        self.assertEqual((a["games"], a["played"], a["wins"], a["losses"], a["draws"], a["failed"], a["truncated"]),
                         (7, 6, 3, 1, 2, 1, 1))
        self.assertAlmostEqual(a["score"], 4 / 6)
        self.assertEqual(a["ci"], list(wilson(4, 6)))
        self.assertAlmostEqual(a["margin"], (100 + 300 + 0 - 100 + 0) / 5)  # truncated game excluded
        self.assertEqual((a["faults"], a["exhausted"], a["fuel_max"], a["fuel_at_budget"]), (1, 2, 99000, 5))


class RunReports(unittest.TestCase):
    def rows(self):
        out = []
        for i, m in enumerate(("00collision", "01big-world", "00collision", "01big-world")):
            out.append(row("A" if i % 2 == 0 else "B", map_name=m, A={"gems": 500}, B={"gems": 300},
                           tags={"subject": "A", "opponent": "anchors/starter-cpp"}))
        out.append(row("B", a="anchors/main-v28@m1", b="main/economy@v1", subject="B", map_name="00collision",
                       A={"gems": 10}, B={"gems": 900, "errors": 2}, replay=".cache/x.repl26a"))
        return out

    def test_groupings(self):
        rows = self.rows()
        lines, summary = report.run_report(rows)
        self.assertIn("subject from tags", lines[0])
        self.assertEqual([s["subject"] for s in summary["subjects"]], ["main/economy"])
        self.assertEqual(summary["subjects"][0]["total"]["wins"], 3)
        for by, expect in (("opponent", {"anchors/starter-cpp": 4, "anchors/main-v28": 1}),
                           ("map", {"00collision": 3, "01big-world": 2}), ("side", {"A": 4, "B": 1}),
                           ("size", {"small": 3, "large": 2}), ("tag:opponent", {"anchors/starter-cpp": 4, "-": 1})):
            lines, summary = report.run_report(rows, by=by)
            got = {g["group"]: g["games"] for g in summary["subjects"][0]["groups"]}
            self.assertEqual(got, expect, by)
            self.assertTrue(any(l.strip().startswith("total") for l in lines))
        with self.assertRaises(core.ArenaError):
            report.run_report(rows, by="colour")

    def test_per_entrant_and_explicit_subject(self):
        rows = [row("A", subject=None), row("B", subject=None)]
        lines, summary = report.run_report(rows)
        self.assertIn("per entrant", lines[0])
        totals = {s["subject"]: (s["total"]["wins"], s["total"]["losses"]) for s in summary["subjects"]}
        self.assertEqual(totals, {"main/economy": (1, 1), "anchors/starter-cpp": (1, 1)})
        _, summary = report.run_report(rows, subject="B")
        self.assertEqual([s["subject"] for s in summary["subjects"]], ["anchors/starter-cpp"])
        lines, summary = report.run_report(rows, subject="nobody")
        self.assertIn("no games match", lines[-1])

    def test_versions_get_distinct_labels_and_limits(self):
        rows = [row("A", a="main/economy@aaaa1111"), row("B", a="main/economy@bbbb2222")]
        _, summary = report.run_report(rows)
        self.assertEqual(sorted(s["subject"] for s in summary["subjects"]),
                         ["main/economy@aaaa1111", "main/economy@bbbb2222"])
        lines, _ = report.run_report(rows, limit=1)
        self.assertIn("1 more entrants", lines[-1])

    def test_incidents(self):
        rows = self.rows() + [row("error", error="FAILED: runner exited\nmore"), row("A", A={"clears": 0})]
        lines, summary = report.run_report(rows, incidents=2)
        self.assertEqual(len(summary["incidents"]), 3)
        text = "\n".join(lines)
        self.assertIn("health incidents in 3 of 7 games (first 2; --incidents N)", text)
        self.assertIn("errors 2", text)
        self.assertIn(".cache/x.repl26a", text)
        lines, _ = report.run_report(rows, incidents=5)
        self.assertIn("error: FAILED: runner exited", "\n".join(lines))

    def test_load_rows_from_dirs_and_files(self):
        with scratch() as tmp:
            run = Path(tmp) / "run"
            (run / "nested").mkdir(parents=True)
            core.append_jsonl(run / "nested" / "games.jsonl", [row("A"), row("B")])
            with open(run / "nested" / "games.jsonl", "a", encoding="utf-8") as fh:
                fh.write('{"torn": ')  # an interrupted writer
            rows, files = report.load_rows([str(run)])
            self.assertEqual((len(rows), len(files)), (2, 1))
            core.append_jsonl(run / "games.jsonl", [row("draw"), {"not": "a game"}])
            rows, files = report.load_rows([str(run), str(run / "nested" / "games.jsonl")])
            self.assertEqual((len(rows), len(files)), (3, 2))  # run/games.jsonl wins over the nested log
            with self.assertRaises(core.ArenaError):
                report.load_rows([str(Path(tmp) / "missing")])
            (Path(tmp) / "empty").mkdir()
            with self.assertRaises(core.ArenaError):
                report.load_rows([str(Path(tmp) / "empty")])


class ReplayParts(unittest.TestCase):
    def test_fuel_profile_and_error_lines(self):
        # 2 draft turns, 3 first gameplay turns (double budget), then ordinary turns
        raw = fuel_stream(BUDGETS, [900000, 50, 150000, 180000, 120000, 40000, 100000, 99999, 100001])
        for fuel in (report.fuel_profile, report._fuel):
            self.assertEqual(fuel(raw, 2, 3), (100001, 2))
            self.assertEqual(fuel(fuel_stream(BUDGETS, []), 3, 3), (0, 0))
            with self.assertRaises(ValueError):
                fuel(b"not a fuel stream", 0, 0)
        out = b"\x00\x00ok\n\x00bot error: bad\x00\nERROR x\nerrors are fine\nmy_error\n"
        self.assertEqual((report.count_errors(out), report._count_errors(out)), (2, 2))

    def test_parsers_mirror_games(self):
        s = synthetic_stats()
        local, used = report._parse_statistics(s), report.parse_statistics(s)
        for side in "AB":
            self.assertEqual({k: local[side][k] for k in report.STAT_KEYS},
                             {k: used[side][k] for k in report.STAT_KEYS})
        self.assertEqual({k: tuple(v) for k, v in local["roster"].items()},
                         {k: tuple(v) for k, v in used["roster"].items()})

    def test_replay_health(self):
        with scratch() as tmp:
            replay = Path(tmp) / "g.repl26a"
            with zipfile.ZipFile(replay, "w") as z:
                z.writestr("replay.repl26a", b"opaque")
                z.writestr("team-a.stdout", b"\x00\x00ok\n\x00bot error: bad\x00\nERROR x\nerrors are fine\nmy_error\n")
                z.writestr("team-b.stdout", b"")
                z.writestr("team-a.fuel", fuel_stream(BUDGETS, [1, 2, 3, 70000, 100000]))
                z.writestr("team-b.fuel", fuel_stream(BUDGETS, [1, 2, 5]))
            h = report.replay_health(replay, {"A": (1, 2), "B": (1, 1)})
            self.assertEqual((h["A"]["errors"], h["A"]["fuel_max"], h["A"]["fuel_at_budget"]), (2, 100000, 1))
            self.assertEqual(h["A"]["error_lines"], ["bot error: bad", "ERROR x"])
            self.assertEqual((h["B"]["errors"], h["B"]["fuel_max"]), (0, 5))


class ReplayWrappers(unittest.TestCase):
    def setUp(self):
        self.scratch = scratch()
        self.root = Path(self.scratch.__enter__())
        self.addCleanup(self.scratch.__exit__, None, None, None)
        self.source = self.root / "completed-recipe"
        self.source.mkdir()
        self.replay = self.source / "game.repl26a"
        self.replay.write_bytes(b"raw replay bytes")
        (self.source / "team-a.stdout").write_bytes(b"bot error: recorded\n")
        (self.source / "team-b.fuel").write_bytes(b"fuel")
        (self.source / "complete.json").write_text('{"complete": true}\n')
        self.cache = self.root / "replay-wrappers"
        mock = patch.object(report, "REPLAY_WRAPPERS", self.cache)
        mock.start(); self.addCleanup(mock.stop)

    def manifest(self):
        return {p.relative_to(self.source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.source.rglob("*") if p.is_file()}

    def test_completed_recipe_is_unchanged_and_wrapper_reuses_exact_bytes(self):
        before = self.manifest()
        out = report._wrap_bundle(self.replay)
        self.assertEqual(out.parent, self.cache)
        original = out.read_bytes()
        self.assertEqual(report._wrap_bundle(self.replay), out)
        self.assertEqual(out.read_bytes(), original)
        self.assertEqual(self.manifest(), before)
        with zipfile.ZipFile(out) as bundle:
            self.assertEqual(bundle.namelist(), ["replay.repl26a", "team-a.stdout", "team-b.fuel"])
            self.assertEqual(bundle.read("replay.repl26a"), self.replay.read_bytes())

    def test_changed_input_bytes_or_membership_never_reuse_old_wrapper(self):
        first = report._wrap_bundle(self.replay)
        before = first.read_bytes()
        stamp = self.replay.stat().st_mtime_ns
        self.replay.write_bytes(b"new replay bytes")  # same length, restored timestamp
        os.utime(self.replay, ns=(stamp, stamp))
        second = report._wrap_bundle(self.replay)
        self.assertNotEqual(first, second)
        self.assertEqual(first.read_bytes(), before)
        (self.source / "team-a.fuel").write_bytes(b"new stream")
        self.assertNotEqual(report._wrap_bundle(self.replay), second)

    def test_tampered_output_or_receipt_is_rebuilt_and_archive_bytes_are_checked(self):
        out = report._wrap_bundle(self.replay)
        expected = out.read_bytes()
        out.write_bytes(b"damaged")
        self.assertEqual(report._wrap_bundle(self.replay).read_bytes(), expected)
        # A matching output checksum alone cannot license an archive with changed members.
        with zipfile.ZipFile(out, "w") as bundle:
            bundle.writestr("replay.repl26a", b"wrong replay")
        receipt = out.with_suffix(".json")
        saved = json.loads(receipt.read_text())
        saved["sha256"] = hashlib.sha256(out.read_bytes()).hexdigest()
        receipt.write_text(json.dumps(saved))
        self.assertEqual(report._wrap_bundle(self.replay).read_bytes(), expected)
        receipt.write_text("{broken")
        self.assertEqual(report._wrap_bundle(self.replay).read_bytes(), expected)

    def test_parallel_wrapping_serializes_and_leaves_no_shared_temporary_file(self):
        barrier = threading.Barrier(4)
        def wrap(_):
            barrier.wait(timeout=5)
            return report._wrap_bundle(self.replay)
        with ThreadPoolExecutor(max_workers=4) as workers:
            outputs = list(workers.map(wrap, range(4)))
        self.assertEqual(len(set(outputs)), 1)
        self.assertTrue(zipfile.is_zipfile(outputs[0]))
        self.assertEqual(list(self.cache.glob("*.tmp")), [])
        self.assertEqual(self.manifest().keys(), {"game.repl26a", "team-a.stdout", "team-b.fuel", "complete.json"})


class GameReports(unittest.TestCase):
    def test_blocks_roster_and_deaths(self):
        s = synthetic_stats()
        b = report.stats_blocks(s)
        self.assertEqual(b["A"], {"gems": 1000, "gold": 800, "kills": 4, "deaths": 0, "clears": 3, "faults": 0,
                                  "exhausted": 0})
        self.assertEqual((b["B"]["clears"], b["B"]["faults"], b["B"]["exhausted"], b["B"]["deaths"]), (0, 2, 4, 3))
        self.assertEqual(report.roster_counts(s), {"A": (2, 2), "B": (1, 2)})
        geo = {"groups": [{"members": [{"nativeId": 4, "type": "wolf"}]}]}
        deaths = report.stats_deaths(s, geo)
        self.assertEqual([(d["round"], d["name"], d["killer"], d["gems_lost"]) for d in deaths],
                         [(100, "Cassi", "Tavi#0", 12), (100, "Issac", "Varg#1", 0), (200, "Cassi", "wolf#4", 0)])
        self.assertEqual(report.stats_deaths(s, None)[2]["killer"], "camp#4")

    def test_game_report_from_statistics(self):
        with scratch() as tmp:
            path = Path(tmp) / "stats.json"
            path.write_text(json.dumps(synthetic_stats()), encoding="utf-8")
            lines, summary = report.game_report(path)
            text = "\n".join(lines)
            self.assertIsNone(summary["replay"])
            self.assertIn("300 rounds (+partial), winner teamA", lines[0])
            self.assertEqual(summary["teams"]["B"]["faults"], 2)
            self.assertIn("B deaths 3: champion 2, camp 1", text)
            self.assertIn("STALL", text)  # team B cleared no camp
            self.assertEqual([c["clears"] for c in summary["champions"]], [3, 0, 0, 0])
            self.assertEqual(summary["champions"][2]["taken"], 12)
            lines, summary = report.game_report(path, team="b", events="death,gemLoss", rounds="100:150", limit=2)
            self.assertEqual({c["side"] for c in summary["champions"]}, {"B"})
            self.assertEqual(len(summary["events"]), 3)
            self.assertIn("events death,gemLoss: 3 (first 2; --limit N)", "\n".join(lines))
            self.assertEqual(len(summary["deaths"]), 2)
            with self.assertRaises(core.ArenaError):
                report.game_report(path, events="nonsense")
            with self.assertRaises(core.ArenaError):
                report.game_report(path, positions=True)  # no replay beside it
            with self.assertRaises(core.ArenaError):
                report.game_report(path, team="C")
            bad = Path(tmp) / "other.json"
            bad.write_text("{}", encoding="utf-8")
            with self.assertRaises(core.ArenaError):
                report.game_report(bad)

    def test_map_names_from_hashes(self):
        import hashlib
        legacy = core.MAPS / "legacy" / "collision.map26a"
        if legacy.is_file():
            self.assertEqual(report.map_by_hash(hashlib.sha256(legacy.read_bytes()).hexdigest()), "collision")
        self.assertIsNone(report.map_by_hash("no-such-map"))
        self.assertIsNone(report.map_by_hash(None))

    def test_companion_files(self):
        with scratch() as tmp:
            d = Path(tmp)
            (d / "x.json").write_text(json.dumps(synthetic_stats()), encoding="utf-8")
            with zipfile.ZipFile(d / "x.repl26a", "w") as z:
                z.writestr("replay.repl26a", b"opaque")
            self.assertEqual(report.find_stats(d / "x.repl26a"), d / "x.json")
            self.assertEqual(report.find_replay(d / "x.json"), d / "x.repl26a")
            with zipfile.ZipFile(d / "y.repl26a", "w") as z:
                z.writestr("replay.repl26a", b"opaque")
            self.assertIsNone(report.find_stats(d / "y.repl26a"))  # two replays: stats.json would be ambiguous

    def test_positions(self):
        def track(team, pos):
            return {"team": team, "type": 11, "pos": pos}
        tracks = {
            0: track("teamA", {1: (0, 0, 100), 2: (1, 0, 100), 3: (-1, -1, 0)}),
            1: track("teamA", {1: (2, 0, 100), 2: (8, 0, 100), 3: (8, 0, 100)}),
            2: track("teamB", {1: (3, 3, 100), 2: (4, 3, 100), 3: (4, 3, 100)}),
        }
        c = report.cohesion(tracks, "teamA")
        self.assertEqual(c["samples"], 5)
        self.assertEqual(c["within"], {2: 2 / 5, 4: 2 / 5, 6: 2 / 5})
        ctx = report.death_context(tracks, 0, 3)
        self.assertEqual((ctx["context_round"], ctx["foes4"], ctx["mates6"]), (2, 1, 0))
        self.assertEqual(report.death_context(tracks, 9, 3), {})
        deaths = report.derived_deaths(tracks)
        self.assertEqual([(d["round"], d["id"], d["name"], d["x"]) for d in deaths], [(3, 0, "Tavi", 1)])


FIXTURE = core.ROOT / "tools" / "tests" / "fixtures" / "00collision-fundamentals-vs-starter.repl26a"


def decoder_cached() -> bool:
    profiles = json.loads((core.ROOT / "tools" / "replay-profiles.json").read_text(encoding="utf-8"))["profiles"]
    p = next(p for p in profiles if p["id"] == "cli-3.0.4")
    assets = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData/Local") / "cambc/assets"
    return core.NODE.is_file() and (assets / p["cacheRelease"] / "visualiser/assets" / p["engineFile"]).is_file()


@unittest.skipUnless(FIXTURE.is_file() and HAVE_GEOMETRY and decoder_cached(), "fixture or official decoder missing")
class FixtureGame(unittest.TestCase):
    """fundamentals (A) beat starter-cpp (B) on 00collision; trimmed statistics sit beside the replay."""

    def test_report_with_positions(self):
        lines, summary = report.game_report(FIXTURE, positions=True)
        self.assertEqual(summary["statistics"], "tools/tests/fixtures/00collision-fundamentals-vs-starter.json")
        self.assertTrue(lines[0].startswith("map 00collision 41x41, 1128 rounds (+partial), winner teamA"))
        a, b = summary["teams"]["A"], summary["teams"]["B"]
        self.assertEqual((a["gems"], a["kills"], a["deaths"], a["clears"], a["errors"], a["fuel_at_budget"]),
                         (1009, 22, 0, 67, 0, 0))
        self.assertEqual((b["gems"], b["deaths"], b["fuel_max"]), (120, 34, 41208))
        self.assertEqual(a["fuel_max"], 76062)
        self.assertEqual(summary["cohesion"]["B"]["samples"], 2533)
        self.assertEqual(len(summary["deaths"]), 34)
        self.assertTrue(all("foes4" in d for d in summary["deaths"]))
        self.assertIn("by Varg 14, Vendetta 13, wolf 4, Tavi 3", "\n".join(lines))  # camp ids -> geometry types
        self.assertEqual(sum(d["killer"].startswith("wolf#") for d in summary["deaths"]), 4)
        first = summary["deaths"][0]
        self.assertEqual((first["round"], first["name"], first["killer"], first["context_round"]),
                         (41, "Cassi", "Vendetta#2", 40))

    def test_replay_only_matches_the_statistics_view(self):
        with scratch() as tmp:
            alone = Path(tmp) / "game.repl26a"
            alone.write_bytes(FIXTURE.read_bytes())
            lines, summary = report.game_report(alone)
            self.assertIn("replay only", lines[0])
            self.assertEqual(summary["teams"]["B"]["fuel_max"], 41208)  # picks/bodies from the decoded roster
            _, summary = report.game_report(alone, positions=True, team="B")
            self.assertEqual(len(summary["deaths"]), 33)  # first round seen dead (the last death ends the game)


@unittest.skipUnless(HAVE_GEOMETRY, "maps/geometry.json missing")
class Maps(unittest.TestCase):
    def test_full_map_and_region(self):
        lines = report.map_view("00collision")
        self.assertTrue(lines[0].startswith("00collision 41x41"))
        grid = lines[1:42]
        self.assertEqual(len(grid), 41)
        self.assertTrue(all(len(l) == 4 + 41 for l in grid))
        self.assertEqual(grid[-1][:4], "  0 ")
        self.assertIn("a", grid[-2])  # team A spawns at the bottom (y 1-3)
        lines = report.map_view("00collision", (2, 0, 6, 4), [(2, 0)])
        self.assertEqual(lines[0].split(": ")[1], "x2-6 y0-4 (north, +y, up)")
        self.assertEqual(lines[5][4], "*")  # (2, 0) is the bottom-left tile of the region
        self.assertEqual(report.map_view("00reverse_spiral")[0].split()[0], "00reverse-spiral")
        with self.assertRaises(core.ArenaError):
            report.map_view("no-such-map")


class Prune(unittest.TestCase):
    def test_population_pins_keep_replay_companions_and_snapshot(self):
        with scratch() as tmp:
            cache = Path(tmp)
            replay = cache / "runs" / "case" / "games" / "witness.repl26a"
            replay.parent.mkdir(parents=True)
            replay.write_bytes(b"witness")
            companion = replay.with_suffix(".json")
            companion.write_text("{}")
            snap = cache / "snap" / "pinned"
            snap.mkdir(parents=True)
            marker = snap / ".ready"
            marker.write_text("built")
            for path in (replay, companion, marker):
                os.utime(path, (1, 1))
            pins = {companion, snap / "bots" / "suite" / "bot"}
            report.prune(0, 0, cache=cache, protected=pins)
            self.assertTrue(replay.is_file())
            self.assertTrue(companion.is_file())
            self.assertTrue(marker.is_file())

    def test_prune_old_replays_and_idle_snapshots(self):
        with scratch() as tmp:
            cache, now = Path(tmp) / "cache", time.time()
            old = now - 10 * 86400

            def put(rel, age=None, data=b"x" * 10):
                p = cache / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
                if age:
                    os.utime(p, (age, age))
                return p
            gone = [put("runs/r1/games/a.repl26a", old), put("runs/r1/games/a.json", old),
                    put("sweeps/s1/v/b.repl26a", old), put("ladder/c.repl26a", old)]
            kept = [put("runs/r1/games.jsonl", old), put("runs/r1/summary.json", old),
                    put("runs/r1/games/new.repl26a"), put("runs/r1/games/a.repl26a.error.log", old),
                    put("other/d.repl26a", old)]
            idle = put("snap/aaaa/.ready", now - 20 * 86400)
            put("snap/aaaa/meta.json", old, b'{"bot": "x/y"}')
            put("snap/aaaa/bots/x/y/src/main.cpp", old)
            sentinel = put("../sentinel/keep.txt")
            link = cache / "snap" / "aaaa" / "sdk"
            if os.name == "nt":
                import _winapi
                _winapi.CreateJunction(str(sentinel.parent), str(link))
            else:
                os.symlink(sentinel.parent, link, target_is_directory=True)
            fresh = put("snap/bbbb/.ready", now - 86400)
            broken = put("snap/cccc/meta.json", now - 30 * 86400, b"{}")  # interrupted build, no marker

            lines = report.prune(7, 14, dry_run=True, cache=cache, now=now, protected=set())
            self.assertTrue(all(p.exists() for p in gone + kept + [idle, fresh, broken]))
            self.assertIn("would remove 4 replay files older than 7 days and 2 snapshots", lines[-1])

            lines = report.prune(7, 14, cache=cache, now=now, protected=set())
            self.assertFalse(any(p.exists() for p in gone), lines)
            self.assertTrue(all(p.exists() for p in kept), lines)
            self.assertFalse((cache / "snap" / "aaaa").exists())
            self.assertFalse((cache / "snap" / "cccc").exists())
            self.assertTrue(fresh.exists())
            self.assertTrue(sentinel.is_file(), "pruning followed the sdk junction")
            self.assertIn("removed 4 replay files", lines[-1])
            self.assertTrue(any("snapshot aaaa (x/y, ready" in l for l in lines))

    def test_busy_snapshot_is_skipped(self):
        with scratch() as tmp:
            cache = Path(tmp)
            (cache / "snap" / "dddd").mkdir(parents=True)
            marker = cache / "snap" / "dddd" / ".failed"
            marker.write_text("log")
            os.utime(marker, (1, 1))
            with core.FileLock(cache / "snap" / "dddd.lock"):
                lines = report.prune(7, 14, cache=cache, protected=set())
            self.assertTrue(marker.exists())
            self.assertIn("skipped snapshot dddd: in use", "\n".join(lines))

    def test_snapshot_refreshed_at_lock_admission_survives_prune(self):
        with scratch() as tmp:
            cache = Path(tmp)
            snapshot = cache / "snap" / "refreshed"
            snapshot.mkdir(parents=True)
            marker = snapshot / ".ready"
            marker.write_text("built")
            os.utime(marker, (1, 1))
            actual = report.FileLock
            class RefreshOnLock:
                def __init__(self, path, **kwargs):
                    self.lock = actual(path, **kwargs)
                def __enter__(self):
                    self.lock.__enter__()
                    marker.touch()
                    return self
                def __exit__(self, *args):
                    return self.lock.__exit__(*args)
            with patch.object(report, "FileLock", RefreshOnLock):
                lines = report.prune(2, 2, cache=cache, protected=set())
            self.assertTrue(marker.is_file())
            self.assertIn("skipped snapshot refreshed: recently used", "\n".join(lines))


class Scaffold(unittest.TestCase):
    def test_new_suite_files(self):
        with scratch() as tmp:
            bots = Path(tmp) / "bots"
            readme, runner = scaffold.new_suite("probe-suite", bots)
            text = readme.read_text(encoding="utf-8")
            for section in ("Doctrine", "Bots", "Distinctness rules", "Layout and checks", "Leads"):
                self.assertIn(f"\n## {section}\n", text)
            self.assertTrue(text.startswith("# probe-suite: "))
            env = dict(os.environ, PYTHONPATH=str(ARENA))  # the stub finds tools/arena from its real location
            proc = subprocess.run([sys.executable, "-B", str(runner)], capture_output=True, text=True, env=env)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("no test cases", proc.stdout)
            proc = subprocess.run([sys.executable, "-B", str(runner), "somebot"], capture_output=True, text=True,
                                  env=env)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with self.assertRaises(core.ArenaError):
                scaffold.new_suite("probe-suite", bots)
            for bad in ("Bad", "a_b", "-x", "tests", "x" * 41, ""):
                with self.assertRaises(core.ArenaError, msg=bad):
                    scaffold.new_suite(bad, bots)

    def test_new_bot_copies_src_and_refuses_overwrites(self):
        with scratch() as tmp:
            bots = Path(tmp) / "bots"
            scaffold.new_suite("probe-suite", bots)
            lines = scaffold.new_bot("probe-suite/probe", bots_dir=bots)
            src = core.bot("anchors/starter-cpp").path / "src"
            copied = sorted(p.relative_to(bots / "probe-suite" / "probe" / "src").as_posix()
                            for p in (bots / "probe-suite" / "probe" / "src").rglob("*") if p.is_file())
            self.assertEqual(copied, sorted(p.relative_to(src).as_posix()
                                            for p in core.closure(core.bot("anchors/starter-cpp"))))
            self.assertTrue(lines[0].startswith("created "))
            self.assertIn("arena.cmd build probe-suite/probe", "\n".join(lines))
            for spec, kw in (("probe-suite/probe", {}), ("anchors/x", {}), ("nosuite/x", {}), ("probe-suite", {}),
                             ("probe-suite/Bad", {}), ("probe-suite/y", {"source": "no/such-bot"})):
                with self.assertRaises(core.ArenaError, msg=spec):
                    scaffold.plan_bot(spec, bots_dir=bots, **kw)

    def test_cross_suite_source_with_shared_modules_is_refused(self):
        shared = [b for b in core.all_bots() if b.suite != "anchors" and any(
            not f.is_relative_to((b.path / "src").resolve()) for f in core.closure(b))]
        if not shared:
            self.skipTest("no bot with shared suite modules")
        with scratch() as tmp:
            bots = Path(tmp) / "bots"
            scaffold.new_suite("probe-suite", bots)
            with self.assertRaises(core.ArenaError):  # never copies another suite's modules
                scaffold.plan_bot("probe-suite/probe", shared[0].id, bots_dir=bots)
            _, target, copies, _ = scaffold.plan_bot("probe-suite/probe", "anchors/main-v28", bots_dir=bots)
            self.assertTrue(copies and all(d.is_relative_to(target) for _, d in copies))  # self-contained source


class CommandLine(unittest.TestCase):
    def run_arena(self, *argv) -> tuple[int, str]:
        import __main__ as _  # noqa: F401  (keep the test runner's module)
        import importlib.util
        spec = importlib.util.spec_from_file_location("arena_main", ARENA / "__main__.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(io.StringIO()):
            code = mod.main(list(argv))
        return code, buf.getvalue()

    def test_report_run_and_json(self):
        with scratch() as tmp:
            log = Path(tmp) / "games.jsonl"
            core.append_jsonl(log, [row("A"), row("B", map_name="01big-world")])
            code, text = self.run_arena("report", str(log), "--by", "map")
            self.assertEqual(code, 0)
            self.assertIn("main/economy: 2 games by map", text)
            code, text = self.run_arena("report", tmp, "--json")
            self.assertEqual(json.loads(text)["games"], 2)

    @unittest.skipUnless(HAVE_GEOMETRY, "maps/geometry.json missing")
    def test_map_command(self):
        code, text = self.run_arena("map", "00collision", "0", "0", "9", "4", "--mark", "1,1")
        self.assertEqual(code, 0)
        self.assertIn("x0-9 y0-4", text.splitlines()[0])


if __name__ == "__main__":
    unittest.main()
