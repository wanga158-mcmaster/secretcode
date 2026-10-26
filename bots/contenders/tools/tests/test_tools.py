"""Tests of the contenders' measuring tools (fundamentals, battery, distinct).

  python -B -I -X utf8 bots/contenders/tools/tests/test_tools.py [-v]

1. Synthetic game: a 3v3 game built by hand on 00elkring's geometry, every expected number derived
   on paper in the comments (episodes, death class, cohesion, recalls, rates, shop, bags).
2. Local fixtures (fixtures/, statistics and decoded tracks of two real local games on 00elkring):
   specialists/fireflies vs anchors/fundamentals (a duel death, its episode and a separation) and
   anchors/main-v35 vs persona:hunter (a home death and a picked death), each checked by hand
   against the decoded positions.
3. Copies: onlinedefs.py reproduces the foundation's cached per-game outputs line for line on 25
   random online games (skipped when .cache/contenders/online or the summaries are absent).
4. Reference: the aggregates reproduce online.md's published numbers (O2, O4, O5, O11, O13, O14)
   from the foundation's cached tables (skipped likewise).
5. Battery and distinct helpers: speedrun, gem-margin se, pinned snapshot contents, distances.
"""
import gzip
import json
import random
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
import battery  # noqa: E402
import cohort  # noqa: E402
import distinct  # noqa: E402
import fundamentals as fu  # noqa: E402
import onlinedefs as od  # noqa: E402
import production  # noqa: E402

FIX = Path(__file__).resolve().parent / "fixtures"
FOUNDATION = ROOT / ".cache" / "contenders" / "online"
LAST = 400


def _timeline(cid, metric, steps):
    """Run-length timeline: value 0 from round 0, then each (round, value) step."""
    runs, prev_r, prev_v = [], 0, 0
    for r, v in steps:
        runs.append({"value": str(prev_v), "duration": r - prev_r})
        prev_r, prev_v = r, v
    runs.append({"value": str(prev_v), "duration": LAST + 1 - prev_r})
    return {"metric": metric, "owner": {"id": cid}, "runs": runs}


def synthetic():
    """3v3 on 00elkring (spawn A x,y 0..3). Rounds 1-40 have positions; the game lasts 400.
    A: 0 Ryth (15,10) all game; 1 Vesper (15,14), recalls to (1,1) at r38; 2 Vendetta (5,25),
       hp 1000 -> 600 (r25) -> 200 (r28), killed at r30 by B's 4.
    B: 3 Ryth (25,10) r1-9, (21,10) r10, (18,10) r11-14, (25,10) r15+; 4 Vesper (28,28), comes to
       (8,25) r20-29, back r30+; 5 Vendetta (28,26), comes to (5,22) r20-29, back r30+."""
    geo = od.geometry()["byid"]["00elkring"]
    path = {0: lambda r: (15, 10, 1000), 1: lambda r: (15, 14, 1000) if r < 38 else (1, 1, 1000),
            2: lambda r: (5, 25, 1000 if r < 25 else 600 if r < 28 else 200) if r < 30 else (-1, -1, 0),
            3: lambda r: (25, 10, 1000) if r < 10 else (21, 10, 1000) if r == 10 else (18, 10, 1000) if r < 15 else (25, 10, 1000),
            4: lambda r: (8, 25, 1000) if 20 <= r < 30 else (28, 28, 1000),
            5: lambda r: (5, 22, 1000) if 20 <= r < 30 else (28, 26, 1000)}
    tracks = {"tracks": [{"id": i, "team": 0 if i < 3 else 1, "type": i % 3,
                          "s": [[r, *path[i](r), 0, 0] for r in range(1, 41)]} for i in range(6)]}
    names = ["Ryth", "Vesper Vale", "Vendetta"]
    roster = [{"id": i, "team": "teamA" if i < 3 else "teamB", "name": names[i % 3], "draftPick": i} for i in range(6)]
    tl = [_timeline(0, "championDamageDone", [(12, 50)]), _timeline(3, "championDamageTaken", [(12, 50)]),
          _timeline(2, "championDamageTaken", [(25, 400), (28, 800)]),
          _timeline(0, "earnedGold", [(100, 300)]), _timeline(1, "earnedGold", [(100, 300)]),
          _timeline(2, "earnedGold", [(20, 540)])]
    owner = lambda i: {"id": i, "kind": "champion", "name": roster[i]["name"], "team": roster[i]["team"]}  # noqa: E731
    ts = lambda r: {"round": r}  # noqa: E731
    events = [
        {"kind": "death", "owner": owner(2), "entries": [{"context": "champion", "target": owner(4), "timestamp": ts(30),
                                                          "value": "1", "x": 5, "y": 25}]},
        {"kind": "kill", "owner": owner(4), "entries": [{"context": "0", "target": owner(2), "timestamp": ts(30),
                                                         "value": "1", "x": 5, "y": 25}]},
        {"kind": "buyItem", "owner": owner(0), "entries": [{"context": "90", "target": {"name": "Treasure Shard of Health"},
                                                            "timestamp": ts(50), "value": "3", "amount": 90.0, "x": 1, "y": 1}]},
        {"kind": "buyItem", "owner": owner(1), "entries": [{"context": "60", "target": {"name": "Jungle Shard of Strength"},
                                                            "timestamp": ts(60), "value": "2", "amount": 60.0, "x": 1, "y": 1}]},
        {"kind": "gemSubmission", "owner": owner(0), "entries": [{"context": "0", "timestamp": ts(100), "value": "30", "x": 2, "y": 2}]},
        {"kind": "gemSubmission", "owner": owner(1), "entries": [{"context": "0", "timestamp": ts(200), "value": "60", "x": 2, "y": 2}]},
    ]
    stats = {"match": {"mapHash": geo["mapSha256"], "roundsCompleted": LAST, "outcome": "teamA", "engineVersion": "1.0.2"},
             "roster": {"champions": roster, "draft": []}, "championTimelines": tl, "events": events,
             "teams": [{"team": "teamA", "submittedGems": "90"}, {"team": "teamB", "submittedGems": "0"}],
             "diagnostics": {"botFaults": {}}}
    return stats, tracks


def game_of(stats, tracks, mid="g"):
    deaths, eps = od.deaths_and_episodes(mid, tracks, stats)
    g = {"sides": od.side_records(mid, stats), "tf": od.track_features(mid, tracks, stats), "deaths": deaths, "episodes": eps}
    return json.loads(json.dumps(g))


class Synthetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.g = game_of(*synthetic())
        cls.ep = {e["cid"]: e for e in cls.g["episodes"]}

    def test_episode_separation(self):
        # champion 0: B's 3 at distance 6 at r10 (start), 3 at r11-14, gone from r15: three turn
        # starts beyond 6 (r15-17) end it by separation, r1 = 17 - 2 = 15; it dealt 50 at r12.
        e = self.ep[0]
        self.assertEqual((e["r0"], e["r1"], e["end"], e["min"], e["close"]), (10, 15, "separation", 3, 0))
        self.assertEqual((e["dmg_done"], e["dmg_taken"]), (50, 0))
        self.assertEqual((self.ep[3]["r0"], self.ep[3]["r1"], self.ep[3]["dmg_taken"]), (10, 15, 50))

    def test_episode_needs_closing_to_3(self):
        self.assertNotIn(1, self.ep)  # champion 1 never had an enemy nearer than 4

    def test_episode_death(self):
        # champion 2: enemies at 3 from r20; absent (dead) at r30 -> end death, r1 30; taken 400+400.
        e = self.ep[2]
        self.assertEqual((e["r0"], e["r1"], e["end"], e["dmg_taken"]), (20, 30, "death", 800))
        # B's 4 is back at r30 on (28,28), inside B's spawn (27..30), from 20 tiles away: a recall.
        self.assertEqual((self.ep[4]["r0"], self.ep[4]["r1"], self.ep[4]["end"]), (20, 30, "recall"))
        # B's 5 returns to (28,26), outside the spawn: three turn starts beyond 6 (r30-32) -> r1 30.
        self.assertEqual((self.ep[5]["r0"], self.ep[5]["r1"], self.ep[5]["end"]), (20, 30, "separation"))

    def test_death_class_picked(self):
        # contact r20-29 (10 rounds), full health at r20, no ally within 6 at r27 (allies at 15 and
        # 11), two enemies within 6 (both at 3) and within 3 in the last rounds, no walk home.
        d, = self.g["deaths"]
        self.assertEqual((d["round"], d["victim"], d["cls"], d["contact"], d["hp0"]), (30, "Vendetta", "picked", 10, 1.0))
        self.assertEqual((d["attackers"], d["allies6"], d["enemies6"], d["walked_home"], d["close_share"]), (2, 0, 2, 0, 0.0))

    def test_cohesion_and_recalls(self):
        # r1-29: 0 and 1 are 4 apart (one ally each), 2 alone; r30-37 0 and 1; r38-40 1 recalled to
        # (1,1), both alone: [0, 1, 2+] = [29 + 6, 58 + 16, 0] of 109. Spread (29*10 + 8*4 + 3*14)/40.
        tf = {t["team"]: t for t in self.g["tf"]}["teamA"]
        self.assertEqual(tf["coh300"], [35 / 109, 74 / 109, 0.0])
        self.assertAlmostEqual(tf["spread300"], 9.1)
        self.assertAlmostEqual(tf["recalls400"], 1 / 3)

    def test_aggregate(self):
        a = fu.aggregate([fu.bundle(self.g, "teamA")])
        # earned 300 + 300 + 540 = 1140 over 3 x 400 - 60 dead rounds (death at r30) = 1140
        self.assertEqual((a["rate"]["n"], a["rate"]["gold_per_alive_round"], a["rate"]["dead_rounds"]), (1, 1.0, 60))
        self.assertEqual(a["rate1000"]["n"], 0)  # the game ended at r400
        ep = a["episodes"]  # fights: 0 (separation) and 2 (death); 2 episodes over 3 champions
        self.assertEqual((ep["fights"], ep["p_death_fight"], ep["ends"]["separation"], ep["ends"]["death"]), (2, 0.5, 0.5, 0.5))
        self.assertAlmostEqual(ep["per_champ_game"], 2 / 3)
        self.assertEqual(ep["fight_len"], 8.5)  # durations 6 and 11
        self.assertEqual(a["deaths"]["cls"]["picked"], 1.0)
        sh, b = a["shop"], a["build"]
        self.assertEqual((sh["first_buy"], sh["gold_per_session400"], sh["no_emblem_by_end"]), (55, 75, 1.0))
        self.assertEqual((b["Health"], b["Strength"], b["Focus"]), (0.6, 0.4, 0.0))  # 3 Health, 2 Strength shards
        bg = a["bags"]
        self.assertEqual((bg["median"], bg["p90"], bg["median_r400"]), (45, 60, 45))
        self.assertAlmostEqual(bg["share_ge50"], 60 / 90)
        self.assertEqual((a["kd400"]["kills"], a["kd400"]["deaths"]), (0, 1))
        self.assertEqual((a["outcome"]["w"], a["outcome"]["wins_by_gems"]), (1, 0))  # won at r400 with 90 banked


class LocalFixture(unittest.TestCase):
    """fireflies (A) vs fundamentals (B) on 00elkring, checked by hand on the decoded positions."""

    @classmethod
    def setUpClass(cls):
        with gzip.open(FIX / "elkring-fireflies-fundamentals.stats.json.gz", "rt", encoding="utf-8") as f:
            stats = json.load(f)
        with gzip.open(FIX / "elkring-fireflies-fundamentals.tracks.json.gz", "rt", encoding="utf-8") as f:
            tracks = json.load(f)
        cls.g = game_of(stats, tracks)

    def test_the_kill(self):
        # Vesper (A) at (0,12) r977 with B's Vendetta at 6; it closes to 4, 2, 2, 2, 1 while she walks
        # north along x=0 (12 -> 19, 7 tiles away from home); hp 912 of 1500 at r977, dead at r985.
        d, = self.g["deaths"]
        self.assertEqual((d["round"], d["victim"], d["killer"], d["cls"], d["contact"]), (985, "Vesper", "Vendetta", "duel", 8))
        self.assertAlmostEqual(d["hp0"], 912 / 1500)
        self.assertEqual((d["attackers"], d["allies6"], d["enemies6"], d["walked_home"], d["close_share"]), (1, 0, 1, -7, 0.5))
        e = next(e for e in self.g["episodes"] if e["end"] == "death")
        self.assertEqual((e["r0"], e["r1"], e["name"], e["min"], e["close"], e["dmg_taken"]), (977, 985, "Vesper", 1, 4, 912))

    def test_a_separation(self):
        # Varg (A, id 1): enemy at 6 at r294, 3 at r298-299, 6 at r303, then 8, 9, ... -> r1 304.
        e = next(e for e in self.g["episodes"] if e["cid"] == 1 and e["r0"] == 294)
        self.assertEqual((e["r1"], e["end"], e["min"], e["dmg_done"] + e["dmg_taken"]), (304, "separation", 3, 0))


class LocalFixtureBrawl(unittest.TestCase):
    """anchors/main-v35 (A) vs persona:hunter (main-v36 with hunting knobs) on 00elkring: 23 deaths,
    two checked by hand on the decoded positions."""

    @classmethod
    def setUpClass(cls):
        with gzip.open(FIX / "elkring-v35-hunter.stats.json.gz", "rt", encoding="utf-8") as f:
            stats = json.load(f)
        with gzip.open(FIX / "elkring-v35-hunter.tracks.json.gz", "rt", encoding="utf-8") as f:
            tracks = json.load(f)
        cls.g = game_of(stats, tracks)

    def test_home_death(self):
        # Varg (A) leaves spawn at r386; enemy at 6 from r390 (r389: 8, r388: 10, r387: 12 end the
        # walk-back), full health; three different enemies within 3 in r395-397; four allies within 6
        # at r395; walked from Chebyshev 2.5 to 6.5 of the spawn centre; enemy within 2 at r394, 395,
        # 397 (3 of 8); died at (8,6), 5 from spawn A: "home" comes before every fight class.
        d = next(d for d in self.g["deaths"] if d["round"] == 398)
        self.assertEqual((d["victim"], d["cls"], d["contact"], d["hp0"], d["attackers"], d["allies6"], d["enemies6"],
                          d["walked_home"], d["close_share"]), ("Varg", "home", 8, 1.0, 3, 4, 3, -4, 0.375))

    def test_picked_death(self):
        # Heart (A): enemy within 6 from r234 (r236's 7 is a one-round gap), hp 722 of 1100; at r249 no
        # ally within 6 (18, 17, 9, 9) and two enemies (6, 4); walked 5 toward home but an enemy was
        # within 2 in only 4 of 18 rounds (no chase); 84 monster damage < 25% of 1100 (no camp).
        d = next(d for d in self.g["deaths"] if d["round"] == 252)
        self.assertEqual((d["victim"], d["cls"], d["contact"], d["attackers"], d["allies6"], d["enemies6"],
                          d["walked_home"], d["monster_dmg10"]), ("Heart", "picked", 18, 1, 0, 2, 5, 84))
        self.assertAlmostEqual(d["hp0"], 722 / 1100)
        self.assertAlmostEqual(d["close_share"], 4 / 18)
        self.assertEqual(len(self.g["deaths"]), 23)


def _foundation_ready():
    return all((FOUNDATION / f).is_file() for f in ("sides.jsonl", "trackfeat.jsonl", "deaths.jsonl", "episodes.jsonl"))


@unittest.skipUnless(_foundation_ready(), "foundation caches absent")
class FoundationCopies(unittest.TestCase):
    def test_copies_reproduce_cached_lines(self):
        sys.path.insert(0, str(ROOT / "bots" / "contenders" / "foundation" / "tools" / "online"))
        import common as fc  # read-only use of the foundation's loaders
        sp = fc.summary_paths()
        mids = sorted(p.name[:-8] for p in (FOUNDATION / "tracks").glob("*.json.gz") if p.name[:-8] in sp)
        if len(mids) < 25:
            self.skipTest("too few online games with tracks")
        want = set(random.Random(1).sample(mids, 25))
        meta = ("name", "tid", "rank", "rating", "tier", "opp", "otid", "orank", "orating", "otier", "res", "end", "kind",
                "created", "version", "group", "survey", "rank_created")
        ref = {k: {} for k in ("sides", "tf", "deaths", "eps")}
        for name, key in (("sides.jsonl", "sides"), ("trackfeat.jsonl", "tf"), ("deaths.jsonl", "deaths"),
                          ("episodes.jsonl", "eps")):
            with open(FOUNDATION / name, encoding="utf-8") as f:
                for line in f:
                    r = json.loads(line)
                    if r["mid"] in want:
                        ref[key].setdefault(r["mid"], []).append(r)
        for mid in sorted(want):
            with gzip.open(FOUNDATION / "tracks" / f"{mid}.json.gz", "rt", encoding="utf-8") as f:
                tr = json.load(f)
            g = game_of(fc.jload(sp[mid]), tr, mid)
            for r in g["sides"]:
                cached = [x for x in ref["sides"].get(mid, []) if x["team"] == r["team"]]
                if cached:  # the foundation drops duplicate games
                    self.assertEqual({k: v for k, v in r.items() if k not in meta},
                                     {k: v for k, v in cached[0].items() if k not in meta}, mid)
            if mid in ref["tf"]:
                self.assertEqual(g["tf"], ref["tf"][mid], mid)
            self.assertEqual(g["deaths"], ref["deaths"].get(mid, []), mid)
            self.assertEqual(g["episodes"], ref["eps"].get(mid, []), mid)


@unittest.skipUnless(_foundation_ready(), "foundation caches absent")
class Reference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        B = list(fu.online_bundles().values())
        cls.agg = {t: fu.aggregate([b for b in B if b["rec"]["tier"] == t]) for t in ("T1", "US")}

    def test_o2_rates(self):  # online.md O2 and extra.txt: T1 n 353 1.05 102 12.6 14%; US n 900 0.84 132 9.3 8%
        for t, (n, g, d, gm, kg) in {"T1": (353, "1.05", "102", "12.6", "14"), "US": (900, "0.84", "132", "9.3", "8")}.items():
            r = self.agg[t]["rate"]
            self.assertEqual((r["n"], f"{r['gold_per_alive_round']:.2f}", f"{r['dead_rounds']:.0f}",
                              f"{r['gems_per_100']:.1f}", f"{r['kill_gold_share']:.0f}"), (n, g, d, gm, kg), t)

    def test_o13_fights(self):  # deathtab.txt: T1 7.9 90% 13.3% 0.4% 75/10/13/1 r22; US 7.8 87% 30.1% 0.3% 53/16/30/1 r23
        for t, want in {"T1": ("7.9", "90", "13.3", "0.4", "75/10/13/1", 22.0),
                        "US": ("7.8", "87", "30.1", "0.3", "53/16/30/1", 23.0)}.items():
            e = self.agg[t]["episodes"]
            got = (f"{e['per_champ_game']:.1f}", f"{100 * e['fight_share']:.0f}", f"{100 * e['p_death_fight']:.1f}",
                   f"{100 * e['p_death_quiet']:.1f}", "/".join(f"{100 * e['ends'][k]:.0f}" for k in fu.ENDS), e["fight_len"])
            self.assertEqual(got, want, t)

    def test_o14_classes(self):  # T1 n 1256: camp 7 home 16 burst 3 chase 8 picked 21 outnumbered 8 duel 12 brawl 24
        d = self.agg["T1"]["deaths"]
        self.assertEqual(d["n"], 1256)
        self.assertEqual([round(100 * d["cls"][k]) for k in fu.CLS], [7, 16, 3, 8, 21, 8, 12, 24])
        self.assertEqual(self.agg["US"]["deaths"]["n"], 8089)

    def test_o5_o4_o11(self):
        s, b, bg = self.agg["T1"]["shop"], self.agg["T1"]["build"], self.agg["T1"]["bags"]
        self.assertEqual((s["champs"], s["first_buy"], s["emblem_rounds"]), (1603, 112, [226.0, 358, 459.0]))
        self.assertEqual((f"{s['sessions400']:.1f}", s["gold_per_session400"], s["gap"]), ("2.8", 120.0, 87.0))
        self.assertEqual(f"{b['Health']:.2f}", "0.52")  # O4: T1 sides put 52% of shards into Health by r400
        self.assertEqual((bg["n"], bg["median"], bg["p75"], bg["p90"], bg["median_r400"], f"{bg['share_ge50']:.2f}"),
                         (9258, 24.0, 44, 66, 21.0, "0.45"))
        self.assertEqual(self.agg["US"]["shop"]["first_buy"], 132)


class Helpers(unittest.TestCase):
    def test_speedrun_counts_losses_as_2000(self):
        rows = [{"map": "00elkring", "rounds": 900, "winner": "A", "tags": {"subject": "A", "opponent": battery.SPEEDRUN_OPP}},
                {"map": "00elkring", "rounds": 1100, "winner": "A", "tags": {"subject": "B", "opponent": battery.SPEEDRUN_OPP}},
                {"map": "00elkring", "rounds": 500, "winner": "B", "tags": {"subject": "B", "opponent": "x"}}]
        sr = battery.speedrun(rows)
        self.assertEqual((sr["n"], sr["mean"], sr["unfinished"], sr["by_map"]["00elkring"]), (2, 1450, 1, 1450))
        self.assertEqual(list(sr["by_class"]), ["mid"])

    def test_opponent_health_reads_the_other_side(self):
        z = {"faults": 0, "exhausted": 0, "errors": 0, "clears": 5, "fuel_at_budget": 0}
        rows = [{"winner": "A", "rounds": 900, "A": dict(z), "B": dict(z, exhausted=3, clears=0),
                 "tags": {"subject": "A", "opponent": "x"}},
                {"winner": "B", "rounds": 900, "A": dict(z, errors=2), "B": dict(z), "tags": {"subject": "A", "opponent": "x"}}]
        self.assertEqual(battery.opponent_health(rows), {"x": {"exhausted": 3, "stall": 1}})
        lines = battery.subject_health_lines(rows)
        self.assertEqual(len(lines), 1)
        self.assertIn("errors 2", lines[0])

    def test_margin_se(self):
        ps = [{"result": "win", "truncated": False, "gems": g, "opp_gems": 0} for g in (10, 20, 30)]
        self.assertAlmostEqual(battery.margin_se(ps), 10 / 3 ** 0.5)

    def test_pinned_snapshot_reproduces_its_version(self):
        pins = battery.load_set()
        lad = [e for e in pins["entries"] if e["kind"] == "ladder"]
        import core
        for e in lad[:2]:
            c = battery.snapshot_contents(e["version"], core.bot(e["bot"]))
            if c is None:
                self.skipTest(f"snapshot {e['version']} pruned")
            self.assertEqual(core.version_of(c), e["version"])

    def test_distance_is_standardized(self):
        st = {"f": {"mean": {"x": 1.0, "y": 3.0}, "sd": 2.0}, "g": {"mean": {"x": 5.0, "y": 5.0}, "sd": 1.0}}
        d, top = distinct.distance(st, "x", "y")
        self.assertAlmostEqual(d, (0.5 * (1.0 ** 2 + 0)) ** 0.5)
        self.assertEqual(top[0], ("f", -1.0))
        per = {"x": [{"decisions": {"a": v}, "outcomes": {}} for v in (1, 3)],
               "y": [{"decisions": {"a": v}, "outcomes": {}} for v in (3, 5)]}
        s = distinct.stats_by_bot(per)["decisions"]["a"]
        self.assertEqual((s["mean"], s["sd"]), ({"x": 2.0, "y": 4.0}, 2 ** 0.5))

    def test_head_to_head_dedupes(self):
        r = {"a": "c/x@1", "b": "c/y@2", "map": "m", "winner": "A"}
        h = distinct.head_to_head([r, dict(r), dict(r, a="c/y@2", b="c/x@1", winner="A")], ["c/x", "c/y"])
        self.assertEqual((h[("c/x", "c/y")]["w"], h[("c/x", "c/y")]["l"], h[("c/x", "c/y")]["n"]), (1, 1, 2))


class Cohort(unittest.TestCase):
    def row(self, **changes):
        team = {"faults": 0, "exhausted": 0, "errors": 0, "fuel_at_budget": 0,
                "clears": 4, "fuel_max": 80000, "gems": 1000, "gold": 2000}
        row = {"a": "contenders/x@111", "b": "anchors/main-v40@222", "engine": "1.0.3",
               "map": "m", "winner": "A", "rounds": 900, "A": dict(team), "B": dict(team, gems=200),
               "tags": {"subject": "A", "phase": "play", "seed": 0}}
        return dict(row, **changes)

    def group(self, row):
        side = cohort.subject_side(row)
        return {"identity": (row[side.lower()], row.get("engine", "unknown"), "base", "play"),
                "cells": {cohort._cell(row, side): {"row": row, "side": side}}}

    def test_matching_requires_recorded_engine_and_exact_opponent(self):
        row = self.row()
        a = self.group(row)
        same = self.group(dict(row, a="contenders/y@333", winner="B"))
        result = cohort.matched(a, same)
        self.assertEqual((result["n"], result["gained"], result["lost"]), (1, 0, 1))
        same["identity"] = (*same["identity"][:3], "sweep")
        self.assertEqual(cohort.matched(a, same)["n"], 1)  # labels do not change game inputs
        missing_gems = self.group(dict(row, a="contenders/y@333", A={"clears": 3}))
        self.assertIsNone(cohort.matched(a, missing_gems)["gem_margin_delta"])
        for b in (self.group(dict(row, engine="1.0.2")), self.group(dict(row, engine="unknown")),
                  self.group(dict(row, b="anchors/main-v40@changed"))):
            self.assertEqual(cohort.matched(a, b)["n"], 0)
            self.assertIn("refusal", cohort.matched(a, b))

    def test_terminal_early_win_counts_and_truncation_does_not(self):
        self.assertTrue(cohort.terminal(self.row(rounds=900)))
        self.assertFalse(cohort.terminal(self.row(rounds=400, truncated=True)))
        result = cohort.describe([{"row": self.row(rounds=400, truncated=True), "side": "A"}], {"m"})
        self.assertEqual((result["finished"], result["unfinished"]), (0, 1))
        self.assertFalse(result["full_pool_both_sides"])

    def test_health_missing_is_unknown_and_opponent_is_separate(self):
        row = self.row(B={"errors": 2, "clears": 0})
        items = [{"row": row, "side": "A"}]
        self.assertEqual(cohort.health(items)["status"], "clean")
        other = cohort.health(items, opponent=True)
        self.assertEqual((other["status"], other["errors"], other["stalls"], other["unknown_games"]),
                         ("failed", 2, 1, 1))
        self.assertEqual(cohort.health([{"row": self.row(A={}), "side": "A"}])["status"], "unknown")

    def test_conflicting_duplicate_excluded_and_versions_split(self):
        import tempfile
        rows = [self.row(), self.row(), self.row(winner="B"), self.row(a="contenders/x@new")]
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "games.jsonl"
            file.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
            groups, coverage = cohort.collect([file])
        self.assertEqual(coverage["rows"], 4)
        self.assertEqual(len(coverage["files"][0]["sha256"]), 64)
        self.assertEqual(len(groups), 2)
        old, new = groups
        self.assertEqual((len(old["cells"]), len(old["conflicts"]), old["duplicates"]), (0, 1, 1))
        self.assertEqual(len(new["cells"]), 1)

    def test_full_pool_requires_each_side_for_every_seed(self):
        a = {"row": self.row(), "side": "A"}
        b = {"row": self.row(tags={"subject": "B"}), "side": "B"}
        self.assertFalse(cohort.describe([a], {"m"})["full_pool_both_sides"])
        self.assertTrue(cohort.describe([a, b], {"m"})["full_pool_both_sides"])
        extra_seed = {"row": self.row(tags={"subject": "A", "seed": 1}), "side": "A"}
        self.assertFalse(cohort.describe([a, b, extra_seed], {"m"})["full_pool_both_sides"])

    def test_subject_selector_refuses_ambiguous_self_play(self):
        self.assertEqual(cohort.subject_side(self.row(), "contenders/x"), "A")
        self.assertIsNone(cohort.subject_side(self.row(b="contenders/x@other"), "contenders/x"))


class Production(unittest.TestCase):
    def event(self, kind, team, round_number, *, group=1, value=0, context="0", ordinal=0):
        return {"kind": kind, "owner": {"team": team, "id": team}, "entries": [{
            "target": {"id": group}, "context": context, "value": str(value),
            "timestamp": {"round": round_number, "input": round_number * 10, "ordinal": ordinal}}]}

    def test_cycle_join_uses_next_clear_and_keeps_zero_gem_capture(self):
        events = [self.event("campClear", "teamA", 10),
                  self.event("gemCollection", "teamB", 15, context="campGroup"),
                  self.event("gemCollection", "teamB", 15, context="campGroup", ordinal=1),
                  self.event("campClear", "teamA", 30),
                  self.event("gemCollection", "teamA", 36, context="campGroup")]
        got = production.cycles({"events": events})
        self.assertEqual(len(got), 2)
        self.assertEqual(got[0]["capture"]["teams"], {"teamB"})
        self.assertEqual(got[0]["capture"]["time"][0], 15)
        self.assertEqual(got[1]["capture"]["teams"], {"teamA"})

    def test_same_round_order_does_not_assign_prior_capture_to_new_clear(self):
        earlier = self.event("gemCollection", "teamA", 10, context="campGroup", ordinal=1)
        later = self.event("campClear", "teamA", 10, ordinal=2)
        got = production.cycles({"events": [later, earlier]})
        self.assertIsNone(got[0]["capture"])

    def test_native_side_measure_distinguishes_steal_and_late_capture(self):
        raw, _ = synthetic()
        rec = od.side_records("synthetic", raw)[0]
        joined = [
            {"clear": {"team": "teamA", "time": (10,)}, "capture": {"teams": {"teamB"}, "time": (15,)}},
            {"clear": {"team": "teamA", "time": (395,)}, "capture": {"teams": {"teamA"}, "time": (402,)}},
        ]
        got = production.side_measure(rec, raw, joined, 400)
        self.assertEqual(got["clear_conversion"], {"opponent_capture_by_horizon": 1, "capture_after_horizon": 1})
        self.assertEqual(got["deaths"], 1)
        self.assertEqual(got["banked"], 90)
        self.assertEqual(got["first_champion_damage_dealt"], 12)
        self.assertEqual(got["champion_damage_taken"], 800)

    def test_matched_horizons_exclude_short_game_and_changed_map_bytes(self):
        raw, _ = synthetic()
        measure = production.side_measure(od.side_records("synthetic", raw)[0], raw, [], 400)
        base = {"subject": "c/a@1", "engine": "1.0.3", "games": []}
        game = {"opponent": "o@1", "map_sha256": "old", "map": "m", "side": "A", "seed": 0,
                "measures": {"400": {"subject": measure}}}
        a = dict(base, games=[game])
        for b in (dict(base, subject="c/b@2", games=[dict(game, measures={})]),
                  dict(base, subject="c/b@2", games=[dict(game, map_sha256="changed")])):
            self.assertEqual(production.comparisons([a, b], [400]), [])
        got = production.comparisons([a, dict(base, subject="c/b@2", games=[game])], [400])
        self.assertEqual((got[0]["n"], got[0]["mean_deltas_b_minus_a"]["gold"]), (1, 0))


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[sys.argv[0]])
