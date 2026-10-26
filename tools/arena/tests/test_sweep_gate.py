"""Offline tests of tools/arena/sweep.py and gate.py: variant parsing, verdicts, sweep reports, ladder
neighbours, run comparison and the gate checks on synthetic rows; the sweep and gate commands end to
end with fake builds and games. No games, builds or slots are used.

Run: python tools/arena/tests/test_sweep_gate.py   (exit 0 = all passed)
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gate  # noqa: E402
import games  # noqa: E402
import sweep  # noqa: E402
from core import CACHE, ArenaError, Bot, Entrant, append_jsonl, map_set  # noqa: E402
from stats import paired  # noqa: E402

SMALL, LARGE = map_set("small")[0], map_set("large")[0]


def team(**kw) -> dict:
    t = dict.fromkeys(games.TEAM_KEYS, 0)
    t.update(clears=2, fuel_max=40_000)
    t.update(kw)
    return t


def row(variant="base", opp="t/opp", map_name=SMALL, side="A", seed=0, result="win", gems=None, opp_gems=None,
        phase="sweep", me="t/subj@v0", opp_key=None, tags=True, truncated=False, replay=None, **mine) -> dict:
    """A games.py row with the subject (team `side`) given `result`; `mine` sets subject team numbers."""
    other = "B" if side == "A" else "A"
    winner = {"win": side, "loss": other, "draw": "draw", "error": "error", "skipped": "skipped"}[result]
    if gems is None:
        gems = {"win": 120, "loss": 40, "draw": 80}.get(result, 0)
    if opp_gems is None:
        opp_gems = {"win": 60, "loss": 100, "draw": 80}.get(result, 0)
    r = {"ts": "2026-10-21T00:00:00Z", "map": map_name, side.lower(): me, other.lower(): opp_key or f"{opp}@v1",
         f"{side.lower()}_name": f"t/subj:{variant}", f"{other.lower()}_name": opp, "winner": winner,
         "rounds": 300 if truncated else 2000, side: team(gems=gems, **mine), other: team(gems=opp_gems),
         "replay": replay, "tags": {"subject": side, "variant": variant, "opponent": opp, "seed": seed, "phase": phase}}
    if not tags:
        r["tags"] = {}
    if truncated:
        r.update(truncated=True, winner="draw")
        r[side]["gems"] = r[other]["gems"] = 0
    if result in ("error", "skipped"):
        r["error"] = "runner exited\nerror: engine error 7"
    return r


def grid(variant, results, opps=("t/x", "t/y"), maps=(SMALL, LARGE), seeds=(0,), **kw) -> list[dict]:
    """One row per (opponent, map, side, seed); `results` maps a key to its result (default win)."""
    out = []
    for o in opps:
        for m in maps:
            for side in "AB":
                for s in seeds:
                    res = results.get((o, m, side, s), "win")
                    out.append(row(variant, o, m, side, s, res, **kw))
    return out


def run(fn, args) -> tuple[int, str]:
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(io.StringIO()):
        code = fn(args)
    return code, buf.getvalue()


class Parsing(unittest.TestCase):
    def test_parse_var(self):
        self.assertEqual(sweep.parse_var("a: k=1 j=2.5f"), ("a", {"k": "1", "j": "2.5f"}))
        self.assertEqual(sweep.parse_var("hi-1:k=x=y"), ("hi-1", {"k": "x=y"}))
        self.assertEqual(sweep.parse_var("same:"), ("same", {}))
        for bad in ("k=1", "base: k=1", "a: k", "a: k=1 k=2", " : k=1", "a b: k=1", "a: =1"):
            with self.assertRaises(ArenaError, msg=bad):
                sweep.parse_var(bad)

    def test_seeds_and_keys(self):
        self.assertEqual(sweep.seed_list(0), [0])
        self.assertEqual(sweep.seed_list(3), [1, 2, 3])
        p = games.perspective(row(seed=2, side="B"))
        self.assertEqual(sweep.game_key(p), ("t/opp", SMALL, "B", 2))
        self.assertEqual(sweep.key_text(sweep.game_key(p)), f"{SMALL} B vs t/opp s2")
        untagged = games.perspective(row(tags=False), "A")
        self.assertEqual(sweep.game_key(untagged), ("t/opp", SMALL, "A", 0))
        self.assertEqual((sweep.size_of(SMALL), sweep.size_of(LARGE), sweep.size_of("collision")),
                         ("small", "large", "other"))


class Verdicts(unittest.TestCase):
    def p(self, gained=0, lost=0, n=20, d=0.0, se=1.0, pv=None):
        from stats import binom_two_sided
        return {"n": n, "gained": list(range(gained)), "lost": list(range(lost)), "diff_mean": d, "diff_se": se,
                "mcnemar_p": binom_two_sided(min(gained, lost), gained + lost) if pv is None else pv}

    def test_rules(self):
        self.assertEqual(sweep.verdict(self.p(gained=8))[0], "better")       # p = 0.008
        self.assertEqual(sweep.verdict(self.p(lost=8))[0], "worse")
        self.assertEqual(sweep.verdict(self.p(gained=4, lost=1))[0], "no detectable difference")  # p = 0.375
        self.assertEqual(sweep.verdict(self.p(d=10, se=2))[0], "better")      # margin alone, no net flips
        self.assertEqual(sweep.verdict(self.p(gained=2, lost=1, d=10, se=2))[0], "better")
        self.assertEqual(sweep.verdict(self.p(gained=1, lost=3, d=10, se=2))[0], "no detectable difference")
        self.assertEqual(sweep.verdict(self.p(gained=1, lost=3, d=-10, se=2))[0], "worse")
        self.assertEqual(sweep.verdict(self.p(d=3, se=2))[0], "no detectable difference")
        self.assertEqual(sweep.verdict(self.p(n=1, d=5, se=0))[0], "no detectable difference")
        word, reason = sweep.verdict({"n": 0})
        self.assertEqual((word, reason), ("no detectable difference", "no common finished games"))
        self.assertIn("McNemar p=0.008", sweep.verdict(self.p(gained=8))[1])

    def test_with_stats_paired(self):
        base = sweep.keyed([games.perspective(r) for r in grid("base", {})])
        lose = {k: "loss" for k in [("t/x", SMALL, "A", 0), ("t/x", SMALL, "B", 0), ("t/y", SMALL, "A", 0),
                                    ("t/y", SMALL, "B", 0), ("t/x", LARGE, "A", 0), ("t/x", LARGE, "B", 0),
                                    ("t/y", LARGE, "A", 0)]}
        var = sweep.keyed([games.perspective(r) for r in grid("v", lose)])
        pr = paired(base, var)
        self.assertEqual((pr["n"], len(pr["gained"]), len(pr["lost"])), (8, 0, 7))
        self.assertEqual(sweep.verdict(pr)[0], "worse")  # p = 2/128 = 0.016


class SweepReport(unittest.TestCase):
    def rows(self):
        base_res = {("t/x", SMALL, "B", 0): "loss", ("t/y", LARGE, "B", 0): "loss"}
        rows = grid("base", base_res)
        rows += grid("a", {})  # gains both
        a_err = row("a", "t/x", LARGE, "A", 0, "error")
        rows = [r for r in rows if not (r["tags"]["variant"] == "a" and r["map"] == LARGE and r["tags"]["opponent"] == "t/x"
                                        and r["tags"]["subject"] == "A")] + [a_err]
        b = grid("b", {("t/x", SMALL, "A", 0): "loss", ("t/y", SMALL, "A", 0): "draw"})
        b[0]["A"].update(clears=0, faults=1, fuel_max=99_000)  # t/x small A: a loss with a stall and a fault
        b[0]["replay"] = ".cache/arena/sweeps/x/games/g.repl26a"
        rows += b
        rows.append(row("base", "t/x", "collision", "A", phase="confirm"))  # other phases stay out
        return rows

    def test_results_and_sections(self):
        secs, res = sweep.sweep_report(self.rows(), ["base", "a", "b"])
        tb, ta, tbb = res["base"]["tally"], res["a"]["tally"], res["b"]["tally"]
        self.assertEqual((tb["n"], tb["w"], tb["l"], tb["d"]), (8, 6, 2, 0))
        self.assertEqual((ta["games"], ta["n"], ta["w"], ta["failed"]), (8, 7, 7, 1))
        self.assertEqual((tbb["w"], tbb["l"], tbb["d"], tbb["stalls"], tbb["faults"], tbb["fuel_max"]), (6, 1, 1, 1, 1, 99_000))
        self.assertAlmostEqual(tbb["score"], 6.5 / 8)
        self.assertEqual(res["a"]["paired"]["n"], 7)  # the error game does not pair
        self.assertEqual(sorted(res["a"]["gained"]), sorted([f"{SMALL} B vs t/x", f"{LARGE} B vs t/y"]))
        self.assertEqual(res["a"]["lost"], [])
        self.assertEqual(res["b"]["lost"], [f"{SMALL} A vs t/x", f"{SMALL} A vs t/y"])  # win -> draw is lost
        self.assertEqual(res["b"]["gained"], [f"{SMALL} B vs t/x", f"{LARGE} B vs t/y"])
        for v in ("a", "b"):
            self.assertIn(res[v]["verdict"], sweep.VERDICTS)
        json.dumps(res)  # sweep.json must be able to hold it
        titles = [s["title"] for s in secs]
        for t in ("Pooled", "Paired vs base", "Verdicts", "By opponent", "By map size", "Subject health", "Flipped"):
            self.assertTrue(any(x.startswith(t) for x in titles), t)
        text = "\n".join(sweep.render(secs, terminal=True))
        self.assertIn("a: games 7  wins 5 -> 7 (+2/-0", text)
        self.assertRegex(text, r"\nb: [a-z ]+ \(.*\) \[health: faults 1, stalls 1\]\n")
        self.assertRegex(text, r"\na: [a-z ]+ \(.*\) \[health: failed 1\]\n")
        self.assertIn(f"b: {SMALL} A vs t/x: faults 1, stall  .cache/arena/sweeps/x/games/g.repl26a", text)
        self.assertIn(f"a: {LARGE} A vs t/x: error: runner exited ... error: engine error 7", text)
        by_opp = next(s for s in secs if s["title"].startswith("By opponent"))["lines"]
        self.assertTrue(by_opp[0].split() == ["base", "a", "b"])
        self.assertEqual(by_opp[1].split()[:4], ["t/x", "3-1-0", "3-0-0", "+1/-0"])
        by_size = next(s for s in secs if s["title"].startswith("By map size"))["lines"]
        self.assertEqual([l.split()[0] for l in by_size[1:]], ["small", "large"])
        md = "\n".join(sweep.render(secs, terminal=False))
        self.assertIn("## Verdicts", md)
        self.assertIn("```", md)

    def test_seeds_pair_separately_and_truncated_games_do_not_pair(self):
        rows = grid("base", {}, opps=("t/x",), maps=(SMALL,), seeds=(1, 2))
        rows += grid("a", {("t/x", SMALL, "A", 2): "loss"}, opps=("t/x",), maps=(SMALL,), seeds=(1, 2))
        rows += [row("base", "t/x", LARGE, "A", 1, "draw", truncated=True),
                 row("a", "t/x", LARGE, "A", 1, "draw", truncated=True, fuel_at_budget=2)]
        secs, res = sweep.sweep_report(rows, ["base", "a"])
        self.assertEqual(res["a"]["paired"]["n"], 4)
        self.assertEqual(res["a"]["lost"], [f"{SMALL} A vs t/x s2"])
        self.assertEqual((res["a"]["tally"]["truncated"], res["a"]["tally"]["suspended"]), (1, 2))
        health = "\n".join(next(s for s in secs if s["title"] == "Subject health")["lines"])
        self.assertIn(f"a: {LARGE} A vs t/x s1: suspended 2", health)

    def test_best_variant_and_terminal_limits(self):
        _, res = sweep.sweep_report(self.rows(), ["base", "a", "b"])
        res["a"]["verdict"], res["b"]["verdict"] = "no detectable difference", "worse"
        self.assertEqual(sweep.best_variant(res, ["base", "a", "b"]), "a")
        res["b"]["verdict"] = "better"
        self.assertEqual(sweep.best_variant(res, ["base", "a", "b"]), "b")
        self.assertIsNone(sweep.best_variant(res, ["base"]))
        s = sweep.section("T", [str(i) for i in range(30)], limit=5)
        lines = sweep.render([s], terminal=True)
        self.assertEqual(lines[-1], "... 25 more in report.md")
        self.assertEqual(len(lines), 7)
        self.assertEqual(len(sweep.render([s], terminal=False)), 35)


class Ladder(unittest.TestCase):
    ORDER = ["anchors/starter-cpp", "anchors/fundamentals", "anchors/main-v18", "anchors/main-v24", "anchors/main-v28"]

    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-sweep-", dir=CACHE)
        self.log = Path(self.tmp.name) / "ladder.jsonl"
        rows = [{"a": "anchors/fundamentals@old", "b": "anchors/main-v28@v", "winner": "A"}] * 6  # an old version
        for i, a in enumerate(self.ORDER):
            for j, b in enumerate(self.ORDER):
                if i < j:  # the stronger (later) bot wins 3 of 4 against a neighbour, 4 of 4 otherwise
                    for k in range(4):
                        upset = j == i + 1 and k == 0
                        rows.append({"a": f"{a}@v", "b": f"{b}@v", "winner": "A" if upset else "B", "tags": {}})
        rows.append({"a": "anchors/starter-cpp@v", "b": "anchors/main-v28@v", "winner": "error"})
        rows.append({"a": "gone/bot@v", "b": "anchors/main-v24@v", "winner": "B"})  # a removed bot
        append_jsonl(self.log, rows)

    def tearDown(self):
        self.tmp.cleanup()

    def test_neighbours(self):
        v24 = Bot("anchors", "main-v24")
        near, note = sweep.ladder_neighbours(v24, "unrated", 2, self.log)
        self.assertEqual(sorted(b.id for b in near), ["anchors/main-v18", "anchors/main-v28"])
        self.assertIn("anchors/main-v24", note)
        near, _ = sweep.ladder_neighbours(Bot("anchors", "starter-cpp"), "v", 1, self.log)
        self.assertEqual([b.id for b in near], ["anchors/fundamentals"])
        self.assertEqual(sweep.ladder_neighbours(Bot("main", "economy"), "x", 2, self.log)[0], [])
        self.assertEqual(sweep.ladder_neighbours(v24, "v", 2, Path(self.tmp.name) / "none.jsonl")[0], [])

    def test_resolve_opponents(self):
        v24 = Bot("anchors", "main-v24")
        opps, notes = sweep.resolve_opponents("ladder:near2,-anchors/main-v28,anchors/starter-cpp", v24, "v", self.log)
        self.assertEqual([b.id for b in opps], ["anchors/main-v18", "anchors/starter-cpp"])
        self.assertTrue(notes and notes[0].startswith("ladder:near2: anchors/main-v24"))
        opps, notes = sweep.resolve_opponents("ladder:near3", v24, "v", Path(self.tmp.name) / "none.jsonl")
        self.assertEqual([b.id for b in opps], sweep.DEFAULT_VS.split(","))
        self.assertIn("using", notes[0])
        with self.assertRaises(ArenaError):
            sweep.resolve_opponents("ladder:near0", v24, "v", self.log)
        with self.assertRaises(ArenaError):
            sweep.resolve_opponents("anchors,-anchors", v24, "v", self.log)


class Compare(unittest.TestCase):
    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-sweep-", dir=CACHE)
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, rows) -> Path:
        d = self.dir / name
        append_jsonl(d / "games.jsonl", rows)
        return d

    def test_compare_runs(self):
        a = grid("x", {("t/x", SMALL, "B", 0): "loss", ("t/y", LARGE, "A", 0): "loss"}, me="t/subj@v1")
        b = grid("x", {("t/y", SMALL, "A", 0): "loss"}, me="t/subj@v2")
        b[1]["B"]["errors"] = 3  # t/x small B: subject errors in B's run
        b.append(row("x", "t/z", SMALL, "A"))  # a key only in B
        lines, pr = sweep.compare(a, b)
        text = "\n".join(lines)
        self.assertEqual((pr["n"], len(pr["gained"]), len(pr["lost"])), (8, 2, 1))
        self.assertIn("A: t/subj@v1  8 games, 6-2-0", text)
        self.assertIn("paired B vs A: games 8  wins 6 -> 7 (+2/-1", text)
        self.assertIn("verdict: B no detectable difference", text)
        self.assertIn("1 only in B", text)
        self.assertIn("health B: faults 0 exhausted 0 errors 3", text)
        self.assertIn(f"lost 1: {SMALL} A vs t/y", text)
        self.assertIn("by map size:", text)

    def test_variants_subjects_and_command(self):
        sweep_rows = grid("base", {}) + grid("a", {("t/x", SMALL, "A", 0): "loss"})
        with self.assertRaises(ArenaError) as cm:
            sweep.compare(sweep_rows, sweep_rows)
        self.assertIn("--variant-a", str(cm.exception))
        _, pr = sweep.compare(sweep_rows, sweep_rows, variant_a="base", variant_b="a")
        self.assertEqual((pr["n"], len(pr["lost"])), (8, 1))
        with self.assertRaises(ArenaError) as cm:
            sweep.compare(sweep_rows, sweep_rows, variant_a="base", variant_b="nope")
        self.assertIn("it holds a, base", str(cm.exception))
        untagged = [dict(r, tags={}) for r in grid("x", {})]
        _, pr = sweep.compare(untagged, untagged)
        self.assertEqual(pr, {"n": 0})
        _, pr = sweep.compare(untagged, untagged, subject="t/subj")
        self.assertEqual(pr["n"], 8)
        _, pr = sweep.compare(untagged, untagged, subject="t/subj@*")
        self.assertEqual(pr["n"], 8)
        p = argparse.ArgumentParser()
        sweep.register(p.add_subparsers(dest="command"))
        run_a = self.write("a", grid("base", {}))
        run_b = self.write("b", grid("base", {("t/x", SMALL, "A", 0): "loss"}))
        code, out = run(sweep._cmd_compare, p.parse_args(["compare", str(run_a), str(run_b / "games.jsonl"),
                                                          "--subject-side-from-tags"]))
        self.assertEqual(code, 0)
        self.assertIn("wins 8 -> 7 (+0/-1", out)
        code, _ = run(sweep._cmd_compare, p.parse_args(["compare", str(run_a), str(self.write("c", untagged))]))
        self.assertEqual(code, 1)  # nothing pairs
        with self.assertRaises(ArenaError):
            sweep.load_rows(str(self.dir / "missing"))
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            p.parse_args(["compare", "x", "y", "--subject", "A", "--subject-side-from-tags"])


def fake_entrant(b, overrides=None, label="", ok=True):
    ov = tuple(sorted((overrides or {}).items()))
    ver = "v" + ("".join(f"-{k}{v}" for k, v in ov) or "0")
    return Entrant(b, ver, Path(tempfile.gettempdir()) / "arena-fake" / ver / b.name, ok, "" if ok else "error: no", ov, label)


def fake_rows(specs) -> list[dict]:
    """Deterministic outcomes: the subject wins as A and loses as B, except variant "a" (wins all)."""
    out = []
    for g in specs:
        t = g.tags
        side = t["subject"]
        res = "win" if side == "A" or t.get("variant") == "a" else "loss"
        me = (g.a if side == "A" else g.b).key
        r = row(t.get("variant", "base"), t["opponent"], g.map, side, t.get("seed", 0), res, phase=t.get("phase", "sweep"),
                me=me, opp_key=(g.b if side == "A" else g.a).key)
        r["tags"] = dict(t)
        out.append(r)
    return out


class SweepCommand(unittest.TestCase):
    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-sweep-", dir=CACHE)
        self.items, self.calls = [], []

        def prepare_many(items, workers=None):
            self.items = items
            return [fake_entrant(it[0], it[1], it[2]) for it in items]

        def play(specs, out, jobs=0, rounds=0, keep_replays="flagged", log=None, quiet=False, resume=True,
                 deadline=None):
            self.calls.append((specs, Path(out), rounds, keep_replays))
            return fake_rows(specs)
        self.saved = (sweep.prepare_many, sweep.play)
        sweep.prepare_many, sweep.play = prepare_many, play
        self.p = argparse.ArgumentParser()
        sweep.register(self.p.add_subparsers(dest="command"))

    def tearDown(self):
        sweep.prepare_many, sweep.play = self.saved
        self.tmp.cleanup()

    def test_matrix_snapshot_and_confirm(self):
        out = Path(self.tmp.name) / "sw"
        args = self.p.parse_args(["sweep", "anchors/main-v28", "--var", "a: tribute_gems=30", "--var",
                                  "b: tribute_gems=20 siege_max_trips=5", "--vs", "anchors/starter-cpp",
                                  "--maps", "00collision,00eight", "--seeds", "2", "--seed-knob", "max_respawn_wait",
                                  "--confirm", "--rounds", "0", "--out", str(out)])
        code, text = run(sweep._cmd_sweep, args)
        self.assertEqual(code, 0, text)
        subject_items = [it for it in self.items if it[0].id == "anchors/main-v28"]
        self.assertEqual(len(subject_items), 3 * 2)
        self.assertTrue(all(it[3] is subject_items[0][3] for it in subject_items))  # one closure read
        self.assertEqual({it[2] for it in subject_items}, {"anchors/main-v28:base", "anchors/main-v28:a",
                                                           "anchors/main-v28:b"})
        self.assertIn(({"max_respawn_wait": "2", "tribute_gems": "20", "siege_max_trips": "5"}),
                      [it[1] for it in subject_items])
        self.assertEqual([it[0].id for it in self.items if it[0].id != "anchors/main-v28"], ["anchors/starter-cpp"])
        (main_specs, main_out, _, keep), (conf_specs, conf_out, _, _) = self.calls
        self.assertEqual((main_out, conf_out, keep), (out, out, "flagged"))
        self.assertEqual(len(main_specs), 3 * 2 * 1 * 2 * 2)
        tags = {(g.tags["variant"], g.tags["seed"], g.map, g.tags["subject"]) for g in main_specs}
        self.assertEqual(len(tags), len(main_specs))
        self.assertEqual(set(main_specs[0].tags), {"subject", "variant", "opponent", "seed", "phase"})
        self.assertEqual([g.tags["variant"] for g in main_specs[:3]], ["base", "a", "b"])  # key-major order
        for g in main_specs:
            me = g.a if g.tags["subject"] == "A" else g.b
            self.assertEqual(dict(me.overrides).get("max_respawn_wait"), str(g.tags["seed"]))
        legacy = map_set("legacy")
        self.assertEqual(len(conf_specs), 2 * 2 * len(legacy) * 2)
        self.assertEqual({g.tags["variant"] for g in conf_specs}, {"base", "a"})  # a gains every B game
        self.assertEqual({g.map for g in conf_specs}, set(legacy))
        self.assertTrue(all(g.tags["phase"] == "confirm" for g in conf_specs))
        self.assertIn("a: better", text)
        self.assertIn("Confirm a vs base on the legacy maps (17)", text)
        self.assertIn("verdict on legacy: better", text)
        meta = json.loads((out / "sweep.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["variants"]["b"]["overrides"], {"tribute_gems": "20", "siege_max_trips": "5"})
        self.assertEqual(meta["results"]["a"]["verdict"], "better")
        self.assertEqual(meta["confirm"]["variant"], "a")
        report = (out / "report.md").read_text(encoding="utf-8")
        self.assertTrue(report.startswith("# Sweep anchors/main-v28@"))
        self.assertIn("## Confirm: Pooled", report)
        self.assertLess(len(text.splitlines()), 100)

    def test_confirm_skipped_on_legacy_and_notes_in_report(self):
        out = Path(self.tmp.name) / "leg"
        args = self.p.parse_args(["sweep", "anchors/main-v28", "--var", "same:", "--vs", "anchors/starter-cpp",
                                  "--maps", "legacy", "--sides", "a", "--confirm", "--out", str(out)])
        code, text = run(sweep._cmd_sweep, args)
        self.assertEqual(code, 0, text)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(len(self.calls[0][0]), 2 * len(map_set("legacy")))
        self.assertIn("skipped: the sweep already played every legacy map", text)
        self.assertIn("same: no detectable difference", text)
        report = (out / "report.md").read_text(encoding="utf-8")
        self.assertIn("- note: same builds identically to base", report)

    def test_bad_knobs_fail_before_building(self):
        for var in ("a: no_such_knob=1", "a: max_respawn_wait=1"):
            extra = ["--seeds", "1", "--seed-knob", "max_respawn_wait"] if "max_respawn" in var else []
            args = self.p.parse_args(["sweep", "anchors/main-v28", "--var", var, "--maps", "00collision", *extra,
                                      "--out", str(Path(self.tmp.name) / "bad")])
            with self.assertRaises(ArenaError):
                run(sweep._cmd_sweep, args)
        with self.assertRaises(ArenaError):
            run(sweep._cmd_sweep, self.p.parse_args(["sweep", "anchors/main-v28", "--maps", "00collision"]))
        with self.assertRaises(ArenaError):
            run(sweep._cmd_sweep, self.p.parse_args(["sweep", "anchors/main-v28", "--var", "a: tribute_gems=1",
                                                     "--var", "a: tribute_gems=2"]))
        self.assertEqual(self.items, [])
        self.assertEqual(self.calls, [])

    def test_failed_baseline_stops(self):
        sweep.prepare_many = lambda items, workers=None: [fake_entrant(it[0], it[1], it[2], ok=bool(it[1]))
                                                          for it in items]
        args = self.p.parse_args(["sweep", "anchors/main-v28", "--var", "a: tribute_gems=30", "--vs",
                                  "anchors/starter-cpp", "--maps", "00collision", "--out", str(Path(self.tmp.name) / "f")])
        code, text = run(sweep._cmd_sweep, args)
        self.assertEqual(code, 1)
        self.assertIn("FAILED build anchors/main-v28@v0", text)
        self.assertIn("baseline did not build", text)
        self.assertEqual(self.calls, [])


class GateChecks(unittest.TestCase):
    def test_health_requires_measurement_but_preserves_complete_legacy_rows(self):
        legacy = row()
        self.assertNotIn("replay_health_complete", legacy)
        self.assertIsNone(legacy["replay"])  # healthy captures may already have been pruned
        self.assertEqual(gate.health_check([legacy])["status"], "pass")
        for flag in ("replay_health_complete", "measurement_complete", "execution_complete", "detailedCaptureComplete"):
            for value in (False, None, 1, "true"):
                with self.subTest(flag=flag, value=value):
                    candidate = dict(legacy, **{flag: value})
                    result = gate.health_check([candidate])
                    self.assertEqual(result["status"], "fail")
                    self.assertIn("1 unmeasured games", result["note"])
                    self.assertIn("replay health incomplete", result["details"][0])
            self.assertEqual(gate.health_check([dict(legacy, **{flag: True})])["status"], "pass")
        for note in ("replay missing: errors and fuel not measured", "no team-a.stdout", "no team-b.fuel",
                     "team-a.fuel: not a fuel stream", "replay unreadable: bad ZIP"):
            with self.subTest(note=note):
                self.assertEqual(gate.health_check([dict(legacy, note=note)])["status"], "fail")
        for side in "AB":
            for key in ("errors", "fuel_max", "fuel_at_budget"):
                with self.subTest(side=side, missing=key):
                    candidate = dict(legacy, **{side: dict(legacy[side])})
                    del candidate[side][key]
                    self.assertEqual(gate.health_check([candidate])["status"], "fail")
                    candidate["replay_health_complete"] = True
                    self.assertEqual(gate.health_check([candidate])["status"], "fail")

    def test_health(self):
        clean = grid("base", {})
        c = gate.health_check(clean)
        self.assertEqual(c["status"], "pass")
        self.assertIn("8 games: 0 faults, 0 exhausted, 0 error lines, 0 stalls; fuel max 40000", c["note"])
        bad = grid("base", {("t/x", SMALL, "A", 0): "error"})
        bad[1]["B"].update(clears=0)
        bad[2]["A"].update(exhausted=2, fuel_at_budget=2, errors=1)
        bad[2]["replay"] = "r.repl26a"
        c = gate.health_check(bad)
        self.assertEqual(c["status"], "fail")
        self.assertIn("3 of 8 games: 0 faults, 2 exhausted, 1 error lines, 1 stalls, 1 failed games", c["note"])
        self.assertEqual(c["details"][1], f"{SMALL} as B vs t/x: stall")
        self.assertEqual(c["details"][2], f"{LARGE} as A vs t/x: exhausted 2, errors 1  r.repl26a")
        trunc = [row("base", "t/x", SMALL, "A", truncated=True), row("base", "t/x", SMALL, "B", truncated=True)]
        self.assertEqual(gate.health_check(trunc)["status"], "part")
        trunc[0]["A"]["fuel_at_budget"] = 3
        c = gate.health_check(trunc)
        self.assertEqual((c["status"], c["details"]), ("fail", [f"{SMALL} as A vs t/x: suspended 3"]))
        self.assertEqual(gate.health_check([])["status"], "fail")

    def test_reference(self):
        ref = Bot("t", "ref")
        rows = grid("base", {("t/ref", SMALL, "A", 0): "loss", ("t/ref", SMALL, "B", 0): "draw"}, opps=("t/ref",))
        rows += grid("base", {}, opps=("t/x",))
        c = gate.ref_check(rows, ref, 0.5)
        self.assertEqual(c["status"], "pass")
        self.assertIn("vs t/ref: 2-1-1 score 0.625 (95% CI", c["note"])
        self.assertEqual(gate.ref_check(rows, ref, 0.7)["status"], "fail")
        rows[0] = row("base", "t/ref", SMALL, "A", result="error")
        self.assertEqual(gate.ref_check(rows, ref, 0.1)["status"], "fail")
        self.assertEqual(gate.ref_check([row("base", "t/ref", truncated=True)], ref, 0.5)["status"], "part")
        self.assertEqual(gate.ref_check(grid("base", {}, opps=("t/x",)), ref, 0.5)["status"], "fail")

    def test_run_tests_and_checklist(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="test-gate-", dir=CACHE) as tmp:
            runner = Path(tmp) / "suite" / "tests" / "run.py"
            runner.parent.mkdir(parents=True)
            runner.write_text("import sys\nprint('cases', sys.argv[1:])\nsys.exit(0 if sys.argv[1:] == ['good'] else 3)\n",
                              encoding="utf-8")
            saved = gate.BOTS
            gate.BOTS = Path(tmp)
            try:
                ok = gate.run_tests(Bot("suite", "good"))
                bad = gate.run_tests(Bot("suite", "bad"))
                skip = gate.run_tests(Bot("other", "x"))
            finally:
                gate.BOTS = saved
        self.assertEqual(ok["status"], "pass")
        self.assertIn("exit 0 (cases ['good'])", ok["note"])
        self.assertEqual((bad["status"], bad["details"]), ("fail", ["cases ['bad']"]))
        self.assertIn("exit 3", bad["note"])
        self.assertEqual(skip["status"], "skip")
        checks = [gate.check("build", "pass", "k"), skip, gate.check("health", "pass", "n")]
        lines, good = gate.checklist("head", checks, None)
        self.assertTrue(good)
        self.assertEqual(lines[-1], "gate PASS: 2 passed, 1 skipped, 0 not passed")
        for status, word in (("part", "INCOMPLETE"), ("fail", "FAIL"), ("not run", "FAIL")):
            lines, good = gate.checklist("head", checks + [gate.check("reference", status, "x", ["d"] * 20)], None)
            self.assertFalse(good)
            self.assertTrue(lines[-1].startswith(f"gate {word}:"), lines[-1])
        self.assertIn("... 8 more in gate.json", [l.strip() for l in lines])


class GateCommand(unittest.TestCase):
    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-gate-", dir=CACHE)
        self.calls, self.prepared = [], []
        self.result = {}

        def prepare(b, overrides=None, label="", rebuild_failed=False, contents=None):
            self.prepared.append((b.id, contents is not None))
            return fake_entrant(b, overrides, label, ok=self.result.get("build", True))

        def prepare_many(items, workers=None):
            self.prepared += [(it[0].id, it[3] is not None) for it in items]
            return [fake_entrant(it[0], it[1], it[2]) for it in items]

        def play(specs, out, jobs=0, rounds=0, keep_replays="flagged", log=None, quiet=False, resume=True,
                 deadline=None):
            self.calls.append(specs)
            rows = fake_rows(specs)
            if self.result.get("stall"):
                rows[0][rows[0]["tags"]["subject"]]["clears"] = 0
            return rows
        self.saved = (gate.prepare, gate.prepare_many, gate.play, gate.run_tests)
        gate.prepare, gate.prepare_many, gate.play = prepare, prepare_many, play
        gate.run_tests = lambda b, **kw: gate.check("tests", self.result.get("tests", "pass"), f"fake {b.id}")
        self.p = argparse.ArgumentParser()
        gate.register(self.p.add_subparsers(dest="command"))

    def tearDown(self):
        gate.prepare, gate.prepare_many, gate.play, gate.run_tests = self.saved
        self.tmp.cleanup()

    def gate(self, *extra):
        out = Path(self.tmp.name) / f"g{len(self.calls)}-{len(self.prepared)}"
        args = self.p.parse_args(["gate", "anchors/fundamentals", "--maps", "00collision,00eight", "--out", str(out),
                                  *extra])
        code, text = run(gate._cmd_gate, args)
        return code, text, json.loads((out / "gate.json").read_text(encoding="utf-8"))

    def test_pass_and_ref(self):
        code, text, rec = self.gate()
        self.assertEqual(code, 0, text)
        self.assertEqual([c["step"] for c in rec["checks"]], ["build", "tests", "health", "reference"])
        self.assertEqual([c["status"] for c in rec["checks"]], ["pass", "pass", "pass", "skip"])
        specs = self.calls[-1]
        self.assertEqual(len(specs), 2 * 2 * 2)  # starter + fundamentals (itself) x 2 maps x 2 sides
        self.assertIn(("anchors/fundamentals", True), self.prepared)  # the self-opponent uses the one read
        self.assertIn("gate PASS", text)
        # the subject wins as A and loses as B against the reference: score 0.5 passes 0.5, fails 0.6
        code, text, rec = self.gate("--ref", "anchors/main-v18")
        self.assertEqual(code, 0, text)
        self.assertEqual(len(self.calls[-1]), 3 * 2 * 2)
        self.assertIn("vs anchors/main-v18: 2-2-0 score 0.500", rec["checks"][3]["note"])
        code, text, rec = self.gate("--ref", "anchors/starter-cpp", "--min-ref-score", "0.6")
        self.assertEqual(code, 1)
        self.assertEqual(len(self.calls[-1]), 2 * 2 * 2)  # the reference is already a health opponent
        self.assertEqual(rec["checks"][3]["status"], "fail")
        self.assertIn("gate FAIL", text)

    def test_health_vs_ladder_falls_back(self):
        saved = sweep.LADDER_LOG
        sweep.LADDER_LOG = Path(self.tmp.name) / "no-ladder.jsonl"
        try:
            code, text, rec = self.gate("--health-vs", "ladder:near2")
        finally:
            sweep.LADDER_LOG = saved
        self.assertEqual(code, 0, text)
        self.assertEqual(rec["health_vs"], sweep.DEFAULT_VS.split(","))
        self.assertIn("note: ladder:near2: no ladder games in", text)

    def test_failures(self):
        self.result = {"stall": True}
        code, text, rec = self.gate()
        self.assertEqual((code, rec["checks"][2]["status"]), (1, "fail"))
        self.assertIn("stall", text)
        self.result = {"tests": "fail"}
        code, _, rec = self.gate("--fail-fast", "--ref", "anchors/main-v18")
        self.assertEqual([c["status"] for c in rec["checks"]], ["pass", "fail", "not run", "not run"])
        n = len(self.calls)
        self.result = {"build": False}
        code, text, rec = self.gate("--ref", "anchors/main-v18")
        self.assertEqual(code, 1)
        self.assertEqual([c["status"] for c in rec["checks"]], ["fail", "not run", "not run", "not run"])
        self.assertEqual(len(self.calls), n)  # no games after a failed build
        self.assertIn("error: no", text)


if __name__ == "__main__":
    unittest.main(verbosity=1)
