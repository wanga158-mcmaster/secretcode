"""Offline tests of tools/arena/ladder.py on synthetic game rows: log reading, ratings, the scheduler
(placement, information, maps, caps, claims, retries, determinism), standings and the leaderboard,
styles, head-to-head, neighbours, a run with fake builds and games, and the command parsers.
No games, builds or slots.

Run: python tools/arena/tests/test_ladder.py   (exit 0 = all passed)
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import games  # noqa: E402
import ladder  # noqa: E402
from core import Bot, Entrant, append_jsonl, read_jsonl  # noqa: E402
from ladder import bot_of  # noqa: E402

MAPS = [f"m{i}" for i in range(12)]


def team(**kw) -> dict:
    return {**dict.fromkeys(games.TEAM_KEYS, 0), "gems": 100, "clears": 5, **kw}


def row(a, b, m, winner, ts="2026-10-21T00:00:00Z", A=None, B=None, rounds=1000, engine=None, **extra) -> dict:
    r = {"ts": ts, "map": m, "a": a, "b": b, "a_name": bot_of(a), "b_name": bot_of(b), "winner": winner,
         "rounds": rounds, "A": team(**(A or {})), "B": team(**(B or {})), "replay": None,
         "tags": {"ladder": True, "engine": engine or ladder.ENGINE}}
    r.update(extra)
    return r


def league(keys, strength, maps, ts="2026-10-20T00:00:00Z") -> list[dict]:
    """Round robin of `keys` on `maps`, both sides; the stronger key wins (deterministic)."""
    rows = []
    for a in keys:
        for b in keys:
            if a != b:
                for m in maps:
                    rows.append(row(a, b, m, "A" if strength[a] > strength[b] else "B", ts=ts))
    return rows


def padding(keys, n=20, style=None) -> list[dict]:
    """n games of each key against an old version on maps outside MAPS: no placement, no meetings.
    style = {key: team values} gives a key its own style."""
    style = style or {}
    return [row(k, "zz/old@0", f"x{i}", "draw", A=style.get(k)) for k in keys for i in range(n)]


def no_style_step():
    return mock.patch.object(ladder, "style_pairs", return_value=[])


def keys_of(plan) -> list[tuple]:
    return [(a, b, p.map) for p in plan for a, b in p.games]


class Paths(unittest.TestCase):
    """Points the ladder files at a temporary directory."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ladder-test-", dir=str(ladder.CACHE.parent)))
        res, cache = self.tmp / "results", self.tmp / "cache"
        patches = {"LOG": res / "games.jsonl", "BUILDS": res / "builds.jsonl", "BOARD": res / "leaderboard.md",
                   "RUNS": cache, "CLAIMS": cache / "claims"}
        self.patchers = [mock.patch.object(ladder, k, v) for k, v in patches.items()]
        for p in self.patchers:
            p.start()

    def tearDown(self):
        for p in self.patchers:
            p.stop()
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)


# ----------------------------------------------------------------------------- log and ratings
class LogAndRatings(Paths):
    def test_load_filters_engine_and_malformed_rows(self):
        X, Y = "s1/x@1", "s2/y@1"
        append_jsonl(ladder.LOG, [row(X, Y, "m0", "A"), row(X, Y, "m1", "A", engine="0.0.1"),
                                  dict(row(X, Y, "m2", "B"), tags={}), {"a": X, "winner": "A"}])
        self.assertEqual([r["map"] for r in ladder.load()], ["m0", "m2"])
        self.assertEqual([r["map"] for r in ladder.load(engine="*")], ["m0", "m1", "m2"])
        self.assertEqual(ladder.load(self.tmp / "missing.jsonl"), [])

    def test_played_dedupes_and_ignores_failures(self):
        X, Y = "s1/x@1", "s2/y@1"
        rows = [row(X, Y, "m0", "A"), row(X, Y, "m0", "B"), row(X, Y, "m1", "error"), row(X, Y, "m2", "skipped"),
                row(X, Y, "m3", "draw", truncated=True), row(X, X, "m4", "A"), row(Y, X, "m0", "draw")]
        got = [(r["a"], r["b"], r["map"], r["winner"]) for r in ladder.played(rows)]
        self.assertEqual(got, [(X, Y, "m0", "A"), (Y, X, "m0", "draw")])
        self.assertEqual(ladder.conflicts(rows), 1)

    def test_fit_anchors_newest_starter_at_1000(self):
        old, new, X = "anchors/starter-cpp@old", "anchors/starter-cpp@new", "s1/x@1"
        rows = [row(old, X, f"m{i}", "B", ts="2026-10-01T00:00:00Z") for i in range(4)]
        rows += [row(new, X, f"m{i}", "B", ts="2026-10-05T00:00:00Z") for i in range(4)]
        rows += [row(X, new, "m9", "error")]
        r = ladder.fit(rows)
        self.assertEqual(ladder.anchor_key(ladder.played(rows)), new)
        self.assertAlmostEqual(r[new]["elo"], 1000.0, places=6)
        self.assertGreater(r[X]["elo"], 1100)
        self.assertEqual(r[X]["games"], 8)  # the error row is ignored

    def test_fit_draws_and_mean_without_starter(self):
        X, Y = "s1/x@1", "s2/y@1"
        r = ladder.fit([row(X, Y, "m0", "draw"), row(Y, X, "m0", "draw")])
        self.assertAlmostEqual(r[X]["elo"], 1500.0, places=6)
        self.assertAlmostEqual(r[Y]["elo"], 1500.0, places=6)
        self.assertEqual(r[X]["score"], 0.5)


# ----------------------------------------------------------------------------- scheduler
class Scheduler(unittest.TestCase):
    def test_never_replays_and_swaps_sides(self):
        keys = ["s1/a@1", "s1/b@1", "s2/c@1", "s2/d@1", "s3/e@1", "s3/f@1"]
        strength = {k: i for i, k in enumerate(keys)}
        rows = league(keys, strength, MAPS[:3])
        plan = ladder.schedule(keys, rows, MAPS, 12)
        self.assertEqual(len(plan), 12)
        done = {(r["a"], r["b"], r["map"]) for r in rows}
        planned = keys_of(plan)
        self.assertFalse(set(planned) & done)
        self.assertEqual(len(planned), len(set(planned)))
        for p in plan:
            self.assertEqual(p.games, [(p.x, p.y), (p.y, p.x)])
            self.assertNotIn(p.map, MAPS[:3])

    def test_fresh_map_first_then_missing_half_then_exhausted(self):
        X, Y = "s1/x@1", "s2/y@1"
        rows = [row(X, Y, "m0", "A"), row(Y, X, "m0", "B"), row(X, Y, "m1", "A")]
        plan = ladder.schedule([X, Y], rows, ["m0", "m1", "m2"], 5)
        self.assertEqual([(p.map, sorted(p.games)) for p in plan], [("m2", sorted([(X, Y), (Y, X)])), ("m1", [(Y, X)])])

    def test_deterministic(self):
        keys = [f"s{i % 3}/b{i}@v" for i in range(9)]
        strength = {k: (i * 7) % 9 for i, k in enumerate(keys)}
        rows = league(keys, strength, MAPS[:2]) + [row(keys[0], keys[1], "m5", "error")]
        one = ladder.schedule(keys, rows, MAPS, 15)
        two = ladder.schedule(list(reversed(keys)), list(reversed(rows)), MAPS, 15)
        self.assertEqual([(p.x, p.y, p.map, p.games, p.phase) for p in one],
                         [(p.x, p.y, p.map, p.games, p.phase) for p in two])

    def test_placement_first_against_a_spread_of_ratings(self):
        old = [f"s{i % 4}/b{i}@v" for i in range(10)]
        new = "s9/new@v"
        strength = {k: i for i, k in enumerate(old)}
        rows = league(old, strength, MAPS[:2])  # 36 attempts each: established
        plan = ladder.schedule(old + [new], rows, MAPS, 20)
        self.assertEqual(ladder.cap_for(20, 11), 11)
        place = [p for p in plan if p.phase == "place"]
        self.assertEqual(len(place), 10)
        self.assertTrue(all(new in (p.x, p.y) for p in place))
        self.assertEqual(plan[:10], place)
        opps = [p.y if p.x == new else p.x for p in place]
        self.assertEqual(len(set(opps)), 10)
        r = ladder.fit(rows)
        first, low, high = (r[o]["elo"] for o in opps[:3])  # targets: median, 15%, 85% quantiles
        self.assertLess(low, first)
        self.assertLess(first, high)
        self.assertTrue(all(p.phase == "info" for p in plan[10:]))

    def test_cap_limits_any_entrant(self):
        old = [f"s{i % 3}/b{i}@v" for i in range(9)]
        new = "s9/new@v"
        rows = padding(old)
        plan = ladder.schedule(old + [new], rows, MAPS, 3)
        cap = ladder.cap_for(3, 10)
        self.assertEqual(cap, 2)
        self.assertEqual(len(plan), 3)
        count = {}
        for p in plan:
            for k in (p.x, p.y):
                count[k] = count.get(k, 0) + 1
        self.assertLessEqual(max(count.values()), cap)
        self.assertEqual(count[new], 2)

    def test_information_prefers_close_ratings_and_other_suites(self):
        A, B, C, D = "s1/a@v", "s1/b@v", "s2/c@v", "s3/d@v"
        ratings = {A: {"elo": 1500, "se": 50}, B: {"elo": 1500, "se": 50}, C: {"elo": 1500, "se": 50},
                   D: {"elo": 1900, "se": 50}}
        rows = padding([A, B, C, D])
        with no_style_step():
            first = ladder.schedule([A, B, C, D], rows, MAPS, 1, ratings)[0]
            self.assertEqual(first.phase, "info")
            self.assertIn((first.x, first.y), [(A, C), (B, C)])
            # A and C already met on two maps: the repeat decay moves the first pick to B-C
            met = rows + [row(A, C, "m0", "A"), row(C, A, "m0", "A"), row(A, C, "m1", "A"), row(C, A, "m1", "A")]
            first = ladder.schedule([A, B, C, D], met, MAPS, 1, ratings)[0]
            self.assertEqual((first.x, first.y), (B, C))

    def test_information_spreads_within_a_run(self):
        ks = ["s1/a@v", "s2/b@v", "s3/c@v", "s4/d@v"]
        ratings = {k: {"elo": 1500, "se": 100} for k in ks}
        with no_style_step():
            plan = ladder.schedule(ks, padding(ks), MAPS, 2, ratings)
        self.assertEqual(len(plan), 2)
        self.assertFalse({plan[0].x, plan[0].y} & {plan[1].x, plan[1].y})

    def test_information_repeats_a_pair_on_new_maps_when_needed(self):
        X, Y = "s1/x@v", "s2/y@v"
        with no_style_step():
            plan = ladder.schedule([X, Y], padding([X, Y]), ["m0", "m1", "m2"], 5)
        self.assertEqual(sorted(p.map for p in plan), ["m0", "m1", "m2"])
        self.assertTrue(all(p.phase == "info" and len(p.games) == 2 for p in plan))

    def test_style_neighbours_that_never_met_get_a_pairing(self):
        A, B, C, D = "s1/a@v", "s1/b@v", "s2/c@v", "s3/d@v"
        style = {A: dict(gems=500, gold=4000), B: dict(gems=505, gold=4010), C: dict(gems=100, kills=30),
                 D: dict(gems=900, deaths=40)}
        rows = padding([A, B, C, D], style=style)
        self.assertEqual(ladder.style_pairs(rows, [A, B, C, D]), [(A, B)])
        plan = ladder.schedule([A, B, C, D], rows, MAPS, 2)
        self.assertEqual((plan[0].x, plan[0].y, plan[0].phase), (A, B, "style"))
        self.assertEqual(plan[1].phase, "info")
        self.assertNotEqual({plan[1].x, plan[1].y}, {A, B})
        met = rows + [row(A, B, "m0", "A"), row(B, A, "m0", "A")]
        self.assertEqual(ladder.style_pairs(met, [A, B, C, D]), [])

    def test_claims_and_error_retries(self):
        X, Y = "s1/x@1", "s2/y@1"
        rows = [row(X, Y, "m0", "error"), row(X, Y, "m1", "error"), row(X, Y, "m1", "error")]
        plan = ladder.schedule([X, Y], rows, ["m0", "m1", "m2"], 5, taken={(X, Y, "m2")})
        self.assertEqual(sorted(keys_of(plan)), sorted([(X, Y, "m0"), (Y, X, "m0"), (Y, X, "m1"), (Y, X, "m2")]))
        self.assertEqual(plan[0].map, "m0")

    def test_trivial_inputs(self):
        self.assertEqual(ladder.schedule(["s1/x@1"], [], MAPS, 5), [])
        self.assertEqual(ladder.schedule(["s1/x@1", "s2/y@1"], [], [], 5), [])
        self.assertEqual(ladder.schedule(["s1/x@1", "s2/y@1"], [], MAPS, 0), [])

    def test_new_league_pairs_everyone(self):
        keys = [f"s{i % 4}/b{i}@v" for i in range(8)]
        plan = ladder.schedule(keys, [], MAPS, 16)
        self.assertEqual(len(plan), 16)
        count = {k: 0 for k in keys}
        for p in plan:
            count[p.x] += 1
            count[p.y] += 1
            self.assertEqual(p.phase, "place")
        self.assertEqual(set(count.values()), {4})
        self.assertGreater(sum(1 for p in plan if p.x.split("/")[0] != p.y.split("/")[0]), 12)


# ----------------------------------------------------------------------------- claims
class Claims(Paths):
    def test_live_and_stale_claims(self):
        c = ladder.Claim([["s1/x@1", "s2/y@1", "m0"]])
        self.assertEqual(ladder.live_claims(), {("s1/x@1", "s2/y@1", "m0")})
        c.release()
        c.release()
        self.assertEqual(ladder.live_claims(), set())
        (ladder.CLAIMS / "dead.json").write_text(json.dumps({"games": [["a", "b", "m"]]}), encoding="utf-8")
        self.assertEqual(ladder.live_claims(), set())
        self.assertEqual(list(ladder.CLAIMS.iterdir()), [])


# ----------------------------------------------------------------------------- standings and board
class Standings(Paths):
    X, Y, Zold, Znew = "s1/x@1", "s2/y@1", "s3/z@old", "s3/z@new"

    def rows(self):
        X, Y, Z = self.X, self.Y, self.Zold
        return [row(X, Y, "m0", "A", A={"faults": 1, "gems": 300, "kills": 4}, B={"clears": 0, "gems": 50}),
                row(Y, X, "m0", "A", A={"gems": 200}, B={"gems": 150, "exhausted": 2}),
                row(X, Y, "m1", "draw", A={"errors": 3}),
                row(X, Z, "m0", "A", ts="2026-10-02T00:00:00Z"), row(Z, X, "m0", "B", ts="2026-10-02T00:00:00Z"),
                row(X, Y, "m2", "error")]

    def current(self):
        return {"s1/x": self.X, "s2/y": self.Y, "s3/z": self.Znew}

    def test_standings_records_health_style_and_previous(self):
        e = {x["key"]: x for x in ladder.standings(self.rows(), self.current())}
        x = e[self.X]
        self.assertEqual((x["games"], x["wins"], x["losses"], x["draws"]), (5, 3, 1, 1))
        self.assertEqual((x["faults"], x["exhausted"], x["errors"], x["stalls"]), (1, 2, 3, 0))
        self.assertAlmostEqual(x["gems"], (300 + 150 + 100 + 100 + 100) / 5)
        self.assertAlmostEqual(x["score"], 3.5 / 5)
        self.assertEqual(x["failed"], 1)
        self.assertEqual(x["rank"], 1)
        y = e[self.Y]
        self.assertEqual((y["wins"], y["losses"], y["draws"], y["stalls"]), (1, 1, 1, 1))
        z = e[self.Znew]
        self.assertIsNone(z["elo"])
        self.assertIsNone(z["rank"])
        self.assertEqual(z["previous"]["version"], "old")
        order = [x["key"] for x in ladder.standings(self.rows(), self.current())]
        self.assertEqual(order[-1], self.Znew)
        allv = {x["key"] for x in ladder.standings(self.rows())}
        self.assertEqual(allv, {self.X, self.Y, self.Zold})

    def test_render_and_board(self):
        rows = self.rows()
        ratings = ladder.fit(rows)
        entries = ladder.standings(rows, self.current(), ratings)
        lines = ladder.render(entries)
        self.assertEqual(len(lines), 4)
        self.assertTrue(lines[0].startswith("rank"))
        self.assertIn("3-1-1", lines[1])
        self.assertIn("1/2/3/0", lines[1])
        self.assertIn("no games yet; last rated old", lines[3])
        path = ladder.write_board(entries, rows, ratings)
        text = path.read_text(encoding="utf-8")
        self.assertIn("| Rank | Bot | Version | Elo |", text)
        self.assertEqual(sum(1 for l in text.splitlines() if l.startswith("| ") and "`s" in l), 3)
        self.assertIn("1 failed game rows ignored", text)

    def test_table_command(self):
        append_jsonl(ladder.LOG, self.rows())
        bots = [Bot("s1", "x"), Bot("s2", "y"), Bot("s3", "z")]
        out = io.StringIO()
        with mock.patch.object(ladder, "current_keys", return_value=self.current()), \
                mock.patch.object(ladder, "select", return_value=bots[:2]), contextlib.redirect_stdout(out):
            code = ladder._cmd_table(argparse.Namespace(select="s1,s2", all_versions=False, top=1))
        text = out.getvalue()
        self.assertEqual(code, 0)
        self.assertIn("ladder: 5 games", text)
        self.assertIn("s1/x", text)
        self.assertIn("... 1 more", text)
        self.assertTrue(ladder.BOARD.is_file())
        out = io.StringIO()
        with mock.patch.object(ladder, "current_keys", return_value=self.current()), \
                mock.patch.object(ladder, "select", return_value=bots), contextlib.redirect_stdout(out):
            ladder._cmd_table(argparse.Namespace(select="all", all_versions=True, top=0))
        self.assertIn("s3/z", out.getvalue())
        self.assertIn("old", out.getvalue())


# ----------------------------------------------------------------------------- styles, vs, near
class StylesAndVs(Paths):
    STYLE = {"s1/p@1": dict(gems=500, gold=4000, kills=5, deaths=2, clears=40),
             "s2/q@1": dict(gems=505, gold=4010, kills=5, deaths=2, clears=40),
             "s3/r@1": dict(gems=100, gold=900, kills=20, deaths=15, clears=5),
             "s4/s@1": dict(gems=900, gold=8000, kills=0, deaths=0, clears=80)}

    def rows(self):
        rows = []
        ks = sorted(self.STYLE)
        for i, a in enumerate(ks):
            for b in ks[i + 1:]:
                for m, (x, y) in enumerate(((a, b), (b, a))):  # team A always wins: every pair splits 1-1
                    rows.append(row(x, y, f"m{m}", "A", A=self.STYLE[x], B=self.STYLE[y]))
        return rows

    def test_styles_flags_convergence(self):
        current = {bot_of(k): k for k in self.STYLE}
        res = {s["bot"]: s for s in ladder.styles(self.rows(), current, min_games=4)}
        self.assertEqual(res["s1/p"]["nearest"], "s2/q@1")
        self.assertEqual(res["s1/p"]["h2h"], (1, 1, 0))
        self.assertEqual(res["s1/p"]["flag"], "possible convergence")
        self.assertEqual(res["s3/r"]["flag"], "")
        self.assertGreater(res["s3/r"]["dist"], 0.5)
        self.assertEqual(ladder.styles(self.rows(), current, min_games=7), [])

    def test_head_to_head_and_vs_command(self):
        rows = self.rows()
        g = ladder.head_to_head(rows, {"s1/p@1"}, {"s3/r@1"})
        self.assertEqual(sorted(s for _, s in g), ["A", "B"])
        self.assertEqual(ladder._record(g), (1, 1, 0))
        append_jsonl(ladder.LOG, rows)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = ladder._cmd_vs(argparse.Namespace(a="s1/p@1", b="s2/q@1", all_versions=False))
        self.assertEqual(code, 0)
        self.assertIn("1-1-0 in 2 games", out.getvalue())
        self.assertIn("expected", out.getvalue())

    def test_styles_command(self):
        append_jsonl(ladder.LOG, self.rows())
        current = {bot_of(k): k for k in self.STYLE}
        out = io.StringIO()
        with mock.patch.object(ladder, "current_keys", return_value=current), \
                mock.patch.object(ladder, "select", return_value=[Bot(*b.split("/")) for b in current]), \
                contextlib.redirect_stdout(out):
            code = ladder._cmd_styles(argparse.Namespace(select="all", threshold=0.25, min_games=4, top=0))
        self.assertEqual(code, 0)
        self.assertIn("2 possible convergence, 0 close without head-to-head", out.getvalue())

    def test_vs_all_versions_groups_by_version(self):
        append_jsonl(ladder.LOG, self.rows() + [row("s1/p@2", "s2/q@1", "m5", "A")])
        out = io.StringIO()
        with mock.patch.object(ladder, "find_bot", side_effect=lambda s: Bot(*s.split("/"))), \
                mock.patch.object(ladder, "version", return_value="2"), contextlib.redirect_stdout(out):
            ladder._cmd_vs(argparse.Namespace(a="s1/p", b="s2/q", all_versions=True))
        text = out.getvalue()
        self.assertIn("s1/p vs s2/q: 2-1-0 in 3 games", text)
        self.assertIn("1 vs 1: 1-1-0", text)
        self.assertIn("2 vs 1: 1-0-0", text)

    def test_near(self):
        bots = [Bot(*bot_of(k).split("/")) for k in self.STYLE]
        current = {bot_of(k): k for k in self.STYLE}
        with mock.patch.object(ladder, "all_bots", return_value=bots):
            got = ladder.near("s1/p", 2, rows=self.rows(), current=current)
        self.assertEqual(len(got), 2)
        self.assertNotIn("s1/p", [b.id for b in got])


# ----------------------------------------------------------------------------- run with fakes
class FakeRun(Paths):
    def setUp(self):
        super().setUp()
        self.bots = [Bot("s1", "x"), Bot("s2", "y"), Bot("s3", "z"), Bot("s4", "broken")]
        self.ents = [Entrant(b, f"v{i}", self.tmp / b.id, b.name != "broken", "" if b.name != "broken" else
                             "error: nope\nbuild failed") for i, b in enumerate(self.bots)]
        self.calls = []

    def fake_play(self, specs, out, jobs=0, rounds=0, keep_replays="flagged", log=None, quiet=False):
        self.calls.append((Path(out), keep_replays, Path(log) if log is not None else None))
        rows = [row(g.a.key, g.b.key, g.map, "A" if g.a.key < g.b.key else "B", engine=g.tags["engine"])
                for g in specs]
        for r, g in zip(rows, specs):
            r["tags"] = dict(g.tags)
        append_jsonl(Path(out) / "games.jsonl", rows)
        if log is not None:
            append_jsonl(Path(log), rows)
        return rows

    def run_once(self, pairings):
        out = io.StringIO()
        with mock.patch.object(ladder, "select", return_value=self.bots), \
                mock.patch.object(ladder, "map_set", return_value=["m0", "m1"]), \
                mock.patch.object(ladder, "prepare_many", return_value=self.ents), \
                mock.patch.object(ladder, "play", side_effect=self.fake_play), \
                mock.patch.object(ladder, "current_keys", return_value={e.bot.id: e.key for e in self.ents}), \
                contextlib.redirect_stdout(out):
            res = ladder.run_once("all", pairings, "pool")
        return res, out.getvalue()

    def test_run_once_logs_tags_builds_and_claims(self):
        res, text = self.run_once(4)
        self.assertEqual(len(res["plan"]), 4)
        self.assertEqual(len(res["rows"]), 8)
        logged = read_jsonl(ladder.LOG)
        self.assertEqual(len(logged), 8)
        for r in logged:
            self.assertEqual(r["tags"]["ladder"], True)
            self.assertEqual(r["tags"]["engine"], ladder.ENGINE)
            self.assertIn(r["tags"]["phase"], ("place", "style", "info"))
            self.assertNotIn("broken", r["a"] + r["b"])
        out, keep, log = self.calls[0]
        self.assertEqual((keep, log), ("all", None))
        self.assertTrue((out / "health-context.json").is_file())
        self.assertTrue(all(r["health_context"]["status"] == "unavailable" for r in logged))
        self.assertEqual(out.parent, ladder.RUNS)
        self.assertEqual([b["key"] for b in read_jsonl(ladder.BUILDS)], ["s4/broken@v3"])
        self.assertIn("build failed: s4/broken@v3", text)
        self.assertIn("ladder: 8 games", text)
        self.assertTrue(ladder.BOARD.is_file())
        self.assertEqual(list(ladder.CLAIMS.glob("*")), [])
        # a second run never replays a game key and logs the build failure only once
        res2, _ = self.run_once(4)
        first = {(r["a"], r["b"], r["map"]) for r in res["rows"]}
        second = {(r["a"], r["b"], r["map"]) for r in res2["rows"]}
        self.assertFalse(first & second)
        self.assertEqual(len(read_jsonl(ladder.BUILDS)), 1)
        # three entrants x two maps x both sides = 12 game keys: all played after two runs
        res3, text3 = self.run_once(4)
        self.assertEqual(res3["rows"], [])
        self.assertIn("only 0 of 4 pairings", text3)

    def test_run_needs_two_entrants(self):
        self.ents = self.ents[:1]
        with self.assertRaises(ladder.ArenaError):
            self.run_once(2)

    def test_completed_local_prefix_is_published_after_game_exception(self):
        normal = self.fake_play
        failure = RuntimeError("game process failed after completed chunk")
        def broken(specs, out, **kwargs):
            normal(specs[:1], out, **kwargs)
            raise failure
        self.fake_play = broken
        with self.assertRaises(RuntimeError) as caught:
            self.run_once(2)
        self.assertIs(caught.exception, failure)
        self.assertEqual(len(read_jsonl(ladder.LOG)), 1)
        self.assertEqual(list(ladder.CLAIMS.glob("*")), [])
        out = self.calls[0][0]
        self.assertEqual(len(read_jsonl(out / "games.jsonl")), 1)
        self.assertTrue((out / "health-context.json").is_file())

    def test_publication_error_is_visible_and_claim_is_released(self):
        with mock.patch.object(ladder, "finalize_ladder_run", side_effect=OSError("disk full")):
            with self.assertRaisesRegex(OSError, "disk full"):
                self.run_once(2)
        self.assertFalse(ladder.LOG.exists())
        self.assertTrue((self.calls[0][0] / "games.jsonl").is_file())
        self.assertEqual(list(ladder.CLAIMS.glob("*")), [])

    def test_publication_error_never_masks_original_game_exception(self):
        normal = self.fake_play
        failure = RuntimeError("original game failure")
        def broken(specs, out, **kwargs):
            normal(specs[:1], out, **kwargs)
            raise failure
        self.fake_play = broken
        with mock.patch.object(ladder, "finalize_ladder_run", side_effect=OSError("secondary publication failure")), \
                contextlib.redirect_stderr(io.StringIO()) as error:
            with self.assertRaises(RuntimeError) as caught:
                self.run_once(2)
        self.assertIs(caught.exception, failure)
        self.assertIn("secondary publication failure", " ".join(failure.__notes__))
        self.assertIn("raw outputs retained", error.getvalue())
        self.assertEqual(list(ladder.CLAIMS.glob("*")), [])

    def test_loop_stops_after_max_loops(self):
        calls = []

        def fake_run_once(*a, **k):
            calls.append(a)
            return {"rows": [row("s1/x@1", "s2/y@1", "m0", "A")], "plan": [], "entrants": [], "failed": []}
        with mock.patch.object(ladder, "run_once", side_effect=fake_run_once), contextlib.redirect_stdout(io.StringIO()):
            code = ladder._cmd_run(argparse.Namespace(select="all", pairings=0, maps="pool", jobs=0, loop=True,
                                                      max_loops=3))
        self.assertEqual((code, len(calls)), (0, 3))


# ----------------------------------------------------------------------------- command line
class Commands(unittest.TestCase):
    def test_register_parses(self):
        parser = argparse.ArgumentParser()
        sub = parser.add_subparsers(dest="command", required=True)
        ladder.register(sub)
        a = parser.parse_args(["ladder", "run", "anchors", "--pairings", "6"])
        self.assertEqual((a.func, a.select, a.pairings, a.maps, a.loop), (ladder._cmd_run, "anchors", 6, "pool", False))
        a = parser.parse_args(["ladder", "run", "--loop", "--max-loops", "2", "--jobs", "4", "--maps", "legacy"])
        self.assertEqual((a.select, a.loop, a.max_loops, a.jobs, a.maps), ("all", True, 2, 4, "legacy"))
        a = parser.parse_args(["ladder", "table", "main", "--all-versions", "--top", "5"])
        self.assertEqual((a.func, a.select, a.all_versions, a.top), (ladder._cmd_table, "main", True, 5))
        a = parser.parse_args(["ladder", "styles", "--threshold", "0.3"])
        self.assertEqual((a.func, a.threshold, a.min_games), (ladder._cmd_styles, 0.3, 4))
        a = parser.parse_args(["ladder", "vs", "main/economy", "anchors/main-v28", "--all-versions"])
        self.assertEqual((a.func, a.a, a.b, a.all_versions), (ladder._cmd_vs, "main/economy", "anchors/main-v28", True))
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parser.parse_args(["ladder"])

    def test_cap_formula(self):
        self.assertEqual(ladder.cap_for(6, 9), 4)
        self.assertEqual(ladder.cap_for(356, 178), 12)
        self.assertEqual(ladder.cap_for(1, 200), 1)
        self.assertTrue(math.isclose(ladder.SE0, ladder.ELO / math.sqrt(0.5)))


if __name__ == "__main__":
    unittest.main(verbosity=1)
