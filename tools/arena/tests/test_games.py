"""Offline tests of tools/arena/games.py: statistics and replay parsing, rows, perspective/health/flagged,
chunked play() with a fake run-batch, cleanup and the `arena play` command. No games, builds or slots.

Run: python tools/arena/tests/test_games.py   (exit 0 = all passed)
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import games  # noqa: E402
import gate  # noqa: E402
from core import CACHE, ROOT, Bot, Entrant, map_info, read_jsonl  # noqa: E402
from games import GameSpec, flagged, health, perspective  # noqa: E402

BUDGETS = (1_000_000, 100_000, 200_000, 100_000) * 2
MAP = "00collision"


def fuel_stream(turns, budgets=BUDGETS) -> bytes:
    values = list(budgets) + list(turns)
    return games.FUEL_MAGIC + b"\x00\x00\x01\x00\x00" + struct.pack(f"<{len(values)}Q", *values)


def write_replay(path: Path, a_out=b"", b_out=b"", a_turns=(), b_turns=()) -> None:
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("replay.repl26a", b"a62per\0\0")
        z.writestr("team-a.stdout", a_out)
        z.writestr("team-b.stdout", b_out)
        z.writestr("team-a.debug", b"")
        z.writestr("team-b.debug", b"")
        z.writestr("team-a.fuel", fuel_stream(a_turns))
        z.writestr("team-b.fuel", fuel_stream(b_turns))


def stats_doc(outcome="teamA", gems=(120, 80), faults=(0, 0), exhausted=(0, 0), clears=(3, 2), rounds=1500,
              picks=((0, 2, 2), (1, 3))) -> dict:
    """Team A drafts Cassi & Issac (pick 2 twice): 2 picks, 3 bodies. Team B: 2 picks, 2 bodies."""
    roster, champions, events, cid = [], [], [], 0
    for t, team in enumerate(("teamA", "teamB")):
        first = cid
        for k, pick in enumerate(picks[t]):
            roster.append({"id": cid, "team": team, "draftPick": pick, "name": f"c{cid}"})
            champions.append({"id": cid, "team": team, "name": f"c{cid}",
                              "metrics": {"earnedGold": str(100 + cid), "kills": str(k), "deaths": "1"}})
            owner = {"id": cid, "kind": "champion", "name": f"c{cid}", "team": team}
            events.append({"kind": "fuelExhausted", "owner": owner, "entries": [{}] * (exhausted[t] if cid == first else 0)})
            events.append({"kind": "campClear", "owner": owner, "entries": [{"value": "1"}] * (clears[t] if cid == first else 0)})
            events.append({"kind": "death", "owner": owner, "entries": [{"context": "camp"}]})
            cid += 1
    return {"match": {"outcome": outcome, "roundsCompleted": rounds, "roundsStarted": rounds + 1},
            "teams": [{"team": "teamA", "submittedGems": str(gems[0])}, {"team": "teamB", "submittedGems": str(gems[1])}],
            "roster": {"champions": roster, "draft": []}, "champions": champions, "events": events,
            "diagnostics": {"botFaults": {"teamA": faults[0], "teamB": faults[1]}, "codes": []}}


def entrant(name: str, ok: bool = True, log: str = "", label: str = "") -> Entrant:
    return Entrant(Bot("t", name), f"v{name}", Path(tempfile.gettempdir()) / "arena-fake" / name, ok, log, (), label)


class Parsing(unittest.TestCase):
    def test_fuel_profile_skips_draft_and_first_turns(self):
        # 2 picks (first draft turn 10x), 3 bodies (first gameplay turns 2x), then ordinary turns
        raw = fuel_stream([900_000, 60_000, 190_000, 150_000, 120_000, 5_000, 100_000, 99_999, 100_000, 42])
        self.assertEqual(games.fuel_profile(raw, 2, 3), (100_000, 2))
        self.assertEqual(games.fuel_profile(raw, 2, 2), (120_000, 3))  # a miscounted body leaks a 2x turn
        self.assertEqual(games.fuel_profile(fuel_stream([]), 3, 3), (0, 0))
        with self.assertRaises(ValueError):
            games.fuel_profile(b"nonsense-stream-bytes", 1, 1)

    def test_count_errors(self):
        out = (b"\0\0\0turn error r3: bad target\n\0\0fundamentals error: unknown exception\n"
               b"ERROR: shouty\nerrors: 0\nterror and errorless\nfuel r1 total 107599\n")
        self.assertEqual(games.count_errors(out), 3)
        self.assertEqual(games.count_errors(b""), 0)

    def test_parse_statistics(self):
        p = games.parse_statistics(stats_doc(gems=(120, 80), faults=(1, 0), exhausted=(4, 0), clears=(3, 0)))
        self.assertEqual((p["winner"], p["rounds"]), ("A", 1500))
        self.assertEqual(p["roster"], {"A": (2, 3), "B": (2, 2)})
        a, b = p["A"], p["B"]
        self.assertEqual((a["gems"], a["gold"], a["kills"], a["deaths"]), (120, 100 + 101 + 102, 0 + 1 + 2, 3))
        self.assertEqual((a["faults"], a["exhausted"], a["clears"]), (1, 4, 3))
        self.assertEqual((b["gems"], b["gold"], b["kills"], b["faults"], b["exhausted"], b["clears"]),
                         (80, 103 + 104, 1, 0, 0, 0))
        self.assertEqual(games.parse_statistics(stats_doc(outcome="teamB"))["winner"], "B")
        self.assertEqual(games.parse_statistics(stats_doc(outcome="draw"))["winner"], "draw")
        with self.assertRaises(ValueError):
            games.parse_statistics(stats_doc(outcome="aliens"))

    def test_batch_status(self):
        log = "\n".join([
            r"Building \\?\D:\x\bots\t\alpha", "alpha is up to date",
            "[|] STARTING  alpha vs beta  00collision", r"    Replay: \\?\D:\x\games\g1.repl26a",
            "[|] STARTING  alpha vs beta  bad", r"    Replay: \\?\D:\x\games\G2.repl26a",
            "[|] STARTING  beta vs alpha  00corners", r"    Replay: \\?\D:\x\games\g3.repl26a",
            "[+] TEAM B WIN (beta)  alpha vs beta  00collision  Round 1124/2000", r"    Replay: \\?\D:\x\games\g1.repl26a",
            r"[x] FAILED: runner exited with exit code: 1 (log: \\?\D:\x\games\G2.repl26a.error.log)  alpha vs beta  bad",
            r"    Replay: \\?\D:\x\games\G2.repl26a",
            "[+] TRUNCATED  beta vs alpha  00corners  Round 0301/2000", "    Replay: D:/x/games/g3.repl26a",
            "Finished: 2 replay bundles saved, 1 failed"])
        st = games.batch_status(log)
        self.assertTrue(st["g1.repl26a"].startswith("TEAM B WIN"))
        self.assertTrue(st["g2.repl26a"].startswith("FAILED: runner exited"))
        self.assertTrue(st["g3.repl26a"].startswith("TRUNCATED"))
        self.assertEqual(len(st), 3)


class Rows(unittest.TestCase):
    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-games-", dir=CACHE)
        self.dir = Path(self.tmp.name)
        self.spec = GameSpec(entrant("alpha", label="alpha-x"), entrant("beta"), MAP, {"subject": "A", "opponent": "t/beta"})

    def tearDown(self):
        self.tmp.cleanup()

    def test_finished_game_row_and_views(self):
        replay, stats = self.dir / "g.repl26a", self.dir / "g.json"
        stats.write_text(json.dumps(stats_doc(outcome="teamB", gems=(64, 1010), exhausted=(2, 0), clears=(0, 5))),
                         encoding="utf-8")
        write_replay(replay, a_out=b"\0turn error r9: x\n", b_out=b"ok\n",
                     a_turns=[900_000, 1, 190_000, 190_000, 150_000, 160_000, 7_000, 100_000],  # 2 picks, 3 bodies
                     b_turns=[900_000, 1, 190_000, 190_000, 50_000, 61_000])                    # 2 picks, 2 bodies
        row = games.game_row(self.spec, MAP, replay, stats, "TEAM B WIN (beta) ...", 0)
        self.assertEqual(set(row) - {"note"}, {"ts", "map", "a", "b", "a_name", "b_name", "winner", "rounds", "A",
                                                 "B", "replay", "tags", "engine", "replay_health_complete"})
        self.assertTrue(row["replay_health_complete"])
        self.assertEqual((row["winner"], row["rounds"], row["map"]), ("B", 1500, MAP))
        self.assertEqual((row["a"], row["a_name"], row["b_name"]), ("t/alpha@valpha", "alpha-x", "t/beta"))
        self.assertEqual(row["replay"], replay.relative_to(ROOT).as_posix())
        self.assertEqual(set(row["A"]), set(games.TEAM_KEYS))
        # A: 2 picks + 3 bodies skipped -> ordinary [160000, 7000, 100000]
        self.assertEqual((row["A"]["fuel_max"], row["A"]["fuel_at_budget"], row["A"]["errors"]), (160_000, 2, 1))
        self.assertEqual((row["B"]["fuel_max"], row["B"]["fuel_at_budget"], row["B"]["errors"]), (61_000, 0, 0))
        self.assertTrue(all(isinstance(v, int) for t in "AB" for v in row[t].values()))
        p = perspective(row)
        self.assertEqual((p["result"], p["side"], p["me"], p["opponent"], p["opponent_name"]),
                         ("loss", "A", "t/alpha@valpha", "t/beta@vbeta", "t/beta"))
        self.assertEqual((p["gems"], p["opp_gems"], p["clears"], p["opp_clears"], p["stall"]), (64, 1010, 0, 5, True))
        self.assertEqual((p["exhausted"], p["errors"], p["fuel_max"], p["rounds"]), (2, 1, 160_000, 1500))
        q = perspective(row, "b")
        self.assertEqual((q["result"], q["side"], q["gems"], q["stall"]), ("win", "B", 1010, False))
        self.assertIsNone(perspective(dict(row, tags={})))
        self.assertEqual(health(row, "A"), ["exhausted 2", "errors 1", "stall"])
        self.assertEqual(health(row, "B"), [])
        self.assertTrue(flagged(row))
        clean = dict(row, A=dict(row["A"], exhausted=0, errors=0, clears=1, fuel_at_budget=0))
        self.assertFalse(flagged(clean))
        self.assertTrue(flagged(dict(clean, B=dict(row["B"], faults=1))))
        self.assertTrue(flagged(dict(clean, winner="error")))

    def test_truncated_game_row(self):
        replay = self.dir / "t.repl26a"
        picks = map_info(MAP)["draft"]  # unknown roster: skip picks draft turns and picks + 1 first turns
        lead = [900_000] + [1] * (picks - 1) + [190_000] * (picks + 1)
        write_replay(replay, a_out=b"x error: y\n", a_turns=lead + [100_000, 30_000], b_turns=lead + [20_000])
        row = games.game_row(self.spec, MAP, replay, self.dir / "t.json", "TRUNCATED  a vs b  00collision  Round 0301/2000",
                             300)
        self.assertEqual((row["winner"], row["truncated"], row["rounds"]), ("draw", True, 300))
        self.assertEqual((row["A"]["fuel_max"], row["A"]["fuel_at_budget"], row["A"]["errors"]), (100_000, 1, 1))
        self.assertEqual(row["B"]["fuel_max"], 20_000)
        self.assertEqual(perspective(row)["result"], "draw")
        self.assertFalse(perspective(row)["stall"])  # clears are unknown without statistics
        self.assertEqual(health(row, "A"), ["errors 1", "suspended 1"])
        self.assertEqual(health(row, "B"), [])

    def test_missing_or_corrupt_replay_channels_cannot_pass_the_health_gate(self):
        stats = self.dir / "health.json"
        stats.write_text(json.dumps(stats_doc()), encoding="utf-8")
        baseline = games.parse_statistics(stats_doc())
        members = {f"team-{side}.{kind}": (b"healthy\n" if kind == "stdout" else fuel_stream([1] * 12))
                   for side in "ab" for kind in ("stdout", "fuel")}
        cases = [("missing-replay", None), ("bad-zip", None)]
        cases += [(kind, f"team-{side}.{stream}") for side in "ab" for stream in ("stdout", "fuel")
                  for kind in ("missing", "bad-crc")]
        cases += [(kind, f"team-{side}.fuel") for side in "ab" for kind in ("bad-magic", "short", "unaligned")]
        for index, (kind, member) in enumerate(cases):
            with self.subTest(kind=kind, member=member):
                replay = self.dir / f"health-{index}.repl26a"
                if kind == "bad-zip":
                    replay.write_bytes(b"not a ZIP archive")
                elif kind != "missing-replay":
                    current = dict(members)
                    if kind == "missing":
                        current.pop(member)
                    elif kind == "bad-magic":
                        current[member] = b"broken fuel stream"
                    elif kind == "short":
                        current[member] = games.FUEL_MAGIC + b"\0" * 5
                    elif kind == "unaligned":
                        current[member] += b"\0"
                    with zipfile.ZipFile(replay, "w", compression=zipfile.ZIP_STORED) as bundle:
                        for name, contents in current.items():
                            bundle.writestr(name, contents)
                    if kind == "bad-crc":
                        with zipfile.ZipFile(replay) as bundle:
                            info = bundle.getinfo(member)
                        contents = bytearray(replay.read_bytes())
                        start = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
                        contents[start] ^= 1
                        replay.write_bytes(contents)
                row = games.game_row(self.spec, MAP, replay, stats)
                self.assertFalse(row["replay_health_complete"])
                result = gate.health_check([row])
                self.assertEqual(result["status"], "fail")
                self.assertIn("replay health incomplete", result["details"][0])
                for side in "AB":
                    for key in ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted"):
                        self.assertEqual(row[side][key], baseline[side][key])
                    self.assertEqual(row[side]["errors"], 0)  # unmeasured is never a fabricated exception

    def test_detailed_capture_flag_and_complete_valid_channels_are_preserved(self):
        replay, stats = self.dir / "capture.repl26a", self.dir / "capture.json"
        write_replay(replay, a_turns=[1] * 12, b_turns=[1] * 12)
        for complete in (True, False, None, 1, "true"):
            with self.subTest(complete=complete):
                document = stats_doc()
                document["diagnostics"]["detailedCaptureComplete"] = complete
                stats.write_text(json.dumps(document), encoding="utf-8")
                row = games.game_row(self.spec, MAP, replay, stats)
                self.assertEqual(row["replay_health_complete"], complete is True)
                self.assertEqual(gate.health_check([row])["status"], "pass" if complete is True else "fail")
                self.assertEqual(row["A"], games.parse_statistics(document)["A"] | {
                    "errors": 0, "fuel_max": 1, "fuel_at_budget": 0})

    def test_error_rows(self):
        replay = self.dir / "e.repl26a"
        err = self.dir / "e.repl26a.error.log"
        err.write_text("runner exited with exit code: 1\n\nrunner.log:\nerror: engine error 7:\n", encoding="utf-8")
        row = games.game_row(self.spec, MAP, replay, self.dir / "e.json", "FAILED: runner exited", 0, "log", 1)
        self.assertEqual((row["winner"], row["replay"]), ("error", None))
        self.assertIn("engine error 7", row["error"])
        self.assertEqual(perspective(row)["result"], "error")
        self.assertTrue(flagged(row))
        self.assertEqual(health(row, "A"), [])
        err.unlink()
        row = games.game_row(self.spec, MAP, replay, self.dir / "e.json", "", 0, "Building x\nerror: 1 matches failed", 1)
        self.assertIn("run-batch exit 1", row["error"])
        self.assertIn("matches failed", row["error"])
        (self.dir / "e.json").write_text("{not json", encoding="utf-8")
        row = games.game_row(self.spec, MAP, replay, self.dir / "e.json", "", 0)
        self.assertEqual(row["winner"], "error")
        self.assertIn("unreadable statistics", row["error"])


class FakeSlots:
    def __init__(self, want, minimum=1, poll=2.0, quiet=False):
        self.count = min(want, 2)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class FakeBatch:
    """Stands in for `cambc run-batch`: reads the manifest and writes what the CLI would."""

    def __init__(self):
        self.calls = []

    def __call__(self, manifest: Path, jobs: int, rounds: int, log_path: Path, entrants=()) -> int:
        lines = Path(manifest).read_text(encoding="utf-8").split("\n")
        self.calls.append((len([l for l in lines if l]), jobs, rounds))
        log, failed = ["Building fake"], 0
        for line in filter(None, lines):
            a, b, mp, replay, flag, stats, js = line.split(" ")
            self_check = (flag, js) == ("--statistics", "--json") and all(Path(x).is_absolute() for x in (a, b, mp, replay, stats))
            replay, stats = Path(replay), Path(stats)
            log += [f"[|] STARTING  {Path(a).name} vs {Path(b).name}  {Path(mp).stem}", f"    Replay: \\\\?\\{replay}"]
            if replay.exists() or stats.exists() or not self_check:
                failed += 1
                log += [f"[x] FAILED: output exists  x", f"    Replay: \\\\?\\{replay}"]
                continue
            if Path(b).name == "broken":
                failed += 1
                replay.with_name(replay.name + ".error.log").write_text("runner exited\nerror: engine error 7:\n")
                log += [f"[x] FAILED: runner exited with exit code: 1 (log: x)  y", f"    Replay: \\\\?\\{replay}"]
                continue
            faults = (1, 0) if Path(a).name == "faulty" else (0, 0)
            write_replay(replay, a_turns=[1] * 10 + [5_000], b_turns=[1] * 10 + [6_000])
            if rounds:
                log += ["[+] TRUNCATED  x vs y  m  Round 0301/2000", f"    Replay: \\\\?\\{replay}"]
                continue
            stats.write_text(json.dumps(stats_doc(faults=faults, clears=(2, 2))), encoding="utf-8")
            log += ["[+] TEAM A WIN (x)  x vs y  m  Round 1501/2000", f"    Replay: \\\\?\\{replay}"]
        log.append(f"Finished: {len(lines) - failed} replay bundles saved, {failed} failed")
        Path(log_path).write_text("\n".join(log) + "\n", encoding="utf-8")
        return 1 if failed else 0


class Play(unittest.TestCase):
    def setUp(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="test-games-", dir=CACHE)
        self.out = Path(self.tmp.name) / "run"
        self.fake = FakeBatch()
        self.saved = (games._run_batch, games.Slots, games._warm)
        games._run_batch, games.Slots, games._warm = self.fake, FakeSlots, lambda *a, **k: None

    def tearDown(self):
        games._run_batch, games.Slots, games._warm = self.saved
        self.tmp.cleanup()

    def test_chunks_cleanup_and_records(self):
        alpha, beta, faulty, broken = entrant("alpha"), entrant("beta"), entrant("faulty"), entrant("broken")
        nobuild = entrant("nobuild", ok=False, log="compiling...\nerror: expected ';'\n")
        maps = ["00collision", "00corners", "00contest", "00eight", "00expanse"]
        specs = [GameSpec(alpha, beta, m, {"subject": "A", "opponent": "t/beta"}) for m in maps]
        specs += [GameSpec(beta, alpha, m, {"subject": "B", "opponent": "t/beta"}) for m in maps[:3]]
        specs += [GameSpec(faulty, beta, MAP, {"subject": "A"}), GameSpec(alpha, broken, MAP, {"subject": "A"}),
                  GameSpec(alpha, nobuild, MAP, {"subject": "A"}), GameSpec(alpha, beta, MAP, {"subject": "A"})]
        gdir = self.out / "games"
        log = Path(self.tmp.name) / "shared.jsonl"
        # keep everything from a first call: the second call replays game 0 under the same file names, and
        # the fake run-batch fails any game whose outputs already exist (as the CLI refuses to overwrite)
        rows0 = games.play(specs[:1], self.out, jobs=1, keep_replays="all", quiet=True)
        self.assertEqual(rows0[0]["winner"], "A")
        self.assertTrue((ROOT / rows0[0]["replay"]).is_file())
        self.assertTrue((ROOT / rows0[0]["replay"]).with_suffix(".json").is_file())
        self.assertEqual(self.fake.calls, [(1, 1, 0)])
        rows = games.play(specs, self.out, jobs=4, log=log, quiet=True)
        self.assertEqual(len(rows), len(specs))
        self.assertEqual([r["tags"] for r in rows], [s.tags for s in specs])
        # 11 runnable games, FakeSlots grants 2 slots -> chunks of max(8, 4 * 2) = 8: 8 + 3 games
        self.assertEqual(self.fake.calls[1:], [(8, 2, 0), (3, 2, 0)])
        winners = [r["winner"] for r in rows]
        self.assertEqual(winners, ["A"] * 8 + ["A", "error", "skipped", "A"])
        self.assertIn("engine error 7", rows[9]["error"])
        self.assertIn("expected ';'", rows[10]["error"])
        self.assertIn("build failed: t/nobuild@vnobuild", rows[10]["error"])
        self.assertEqual(rows[11]["map"], MAP)  # a duplicate of game 0 gets its own files
        # keep flagged: clean replays deleted, the faulty game's replay kept, statistics always deleted
        self.assertIsNone(rows[0]["replay"])
        self.assertTrue(flagged(rows[8]) and (ROOT / rows[8]["replay"]).is_file())
        self.assertEqual(health(rows[8], "A"), ["faults 1"])
        self.assertEqual(sorted(p.suffix for p in gdir.glob("*.json")), [])
        self.assertEqual(len(list(gdir.glob("*.error.log"))), 1)
        # the chunk with the failure keeps its manifest and log; a chunk that kept a replay keeps its
        # manifest (it names the sides of that replay); the clean chunk's are removed
        self.assertEqual(len(list(gdir.glob("batch-*.txt"))), 2)
        self.assertEqual(len(list(gdir.glob("batch-*.log"))), 1)
        on_disk = read_jsonl(self.out / "games.jsonl")
        self.assertEqual(len(on_disk), 1 + len(specs))  # appended: the first call's row plus these
        self.assertEqual(len(read_jsonl(log)), len(specs))
        self.assertEqual(sorted(json.dumps(r, sort_keys=True) for r in on_disk[1:]),
                         sorted(json.dumps(r, sort_keys=True) for r in rows))

    def test_keep_modes_and_truncation(self):
        alpha, beta = entrant("alpha"), entrant("beta")
        rows = games.play([GameSpec(alpha, beta, MAP, {"subject": "A"})], self.out, keep_replays="none", quiet=True)
        self.assertIsNone(rows[0]["replay"])
        self.assertEqual(list((self.out / "games").glob("*.repl26a")), [])
        rows = games.play([GameSpec(alpha, beta, MAP, {"subject": "B"})], self.out, rounds=300, quiet=True)
        self.assertEqual((rows[0]["winner"], rows[0].get("truncated"), rows[0]["rounds"]), ("draw", True, 300))
        self.assertEqual(self.fake.calls[-1], (1, 1, 300))  # one game asks for one slot
        rows = games.play([GameSpec(alpha, beta, MAP, {"subject": "A"})], self.out, keep_replays="all", quiet=True)
        self.assertTrue((ROOT / rows[0]["replay"]).with_suffix(".json").is_file())
        with self.assertRaises(games.ArenaError):
            games.play([], self.out, keep_replays="some")

    @unittest.skipUnless(sys.platform == "win32", "an open file blocks deletion only on Windows")
    def test_stale_output_held_open_becomes_an_error_row(self):
        spec = GameSpec(entrant("alpha"), entrant("beta"), MAP, {"subject": "A"})
        first = games.play([spec], self.out, keep_replays="all", quiet=True)[0]
        with open(ROOT / first["replay"], "rb"):  # e.g. a replay open in the visualiser
            again = games.play([spec, GameSpec(entrant("beta"), entrant("alpha"), MAP, {"subject": "B"})],
                               self.out, quiet=True)
        self.assertEqual([r["winner"] for r in again], ["error", "A"])
        self.assertIn("cannot remove stale output", again[0]["error"])
        self.assertEqual(self.fake.calls[-1], (1, 1, 0))  # only the other game reached the manifest

    def test_only_skipped_games_never_run_a_batch(self):
        bad = entrant("nobuild", ok=False, log="error: x")
        rows = games.play([GameSpec(bad, bad, MAP), GameSpec(entrant("alpha"), entrant("beta"), "no-such-map")],
                          self.out, quiet=True)
        self.assertEqual([r["winner"] for r in rows], ["skipped", "error"])
        self.assertEqual(rows[0]["error"].count("build failed"), 1)
        self.assertIn("unknown map", rows[1]["error"])
        self.assertEqual(self.fake.calls, [])
        self.assertEqual(len(read_jsonl(self.out / "games.jsonl")), 2)


class Command(unittest.TestCase):
    def parser(self):
        p = argparse.ArgumentParser()
        games.register(p.add_subparsers(dest="command"))
        return p

    def test_arguments(self):
        a = self.parser().parse_args(["play", "main/economy", "anchors,-anchors/starter-cpp", "--sides", "b",
                                      "--set", "K=1", "--set", "J=x=y", "--rounds", "300"])
        self.assertEqual((a.bot, a.opponents, a.sides, a.overrides, a.rounds, a.keep_replays, a.maps, a.jobs),
                         ("main/economy", "anchors,-anchors/starter-cpp", "B", ["K=1", "J=x=y"], 300, "flagged",
                          "pool", 0))
        self.assertEqual(self.parser().parse_args(["play", "x", "y", "--sides", "BOTH"]).sides, "both")
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.parser().parse_args(["play", "x", "y", "--keep-replays", "some"])

    def test_play_command_end_to_end_with_fakes(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="test-games-", dir=CACHE) as tmp:
            fake = FakeBatch()
            seen = {}

            def fake_prepare_many(items, workers=None):
                seen["items"] = items
                return [Entrant(b, f"v{b.name}", Path(tempfile.gettempdir()) / "arena-fake" / b.name,
                                b.name != "fundamentals", "error: no", (), label) for b, _, label in items]
            saved = (games._run_batch, games.Slots, games._warm, games.prepare_many)
            games._run_batch, games.Slots, games._warm, games.prepare_many = fake, FakeSlots, lambda *a: None, fake_prepare_many
            try:
                args = self.parser().parse_args(["play", "anchors/starter-cpp", "anchors/fundamentals,anchors/main-v18",
                                                 "--maps", "00collision,00corners", "--set", "K=2", "--label", "probe",
                                                 "--out", str(Path(tmp) / "cli")])
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(io.StringIO()):
                    code = args.func(args)
            finally:
                games._run_batch, games.Slots, games._warm, games.prepare_many = saved
            text = buf.getvalue()
            self.assertEqual(code, 1)  # the fundamentals games are skipped (failed build)
            self.assertEqual(seen["items"][0][1:], ({"K": "2"}, "probe"))
            rows = read_jsonl(Path(tmp) / "cli" / "games.jsonl")
            self.assertEqual(len(rows), 2 * 2 * 2)
            self.assertEqual({(r["tags"]["subject"], r["tags"]["opponent"]) for r in rows},
                             {(s, o) for s in "AB" for o in ("anchors/fundamentals", "anchors/main-v18")})
            for r in rows:
                self.assertEqual(r[f"{r['tags']['subject'].lower()}_name"], "probe")
            self.assertIn("vs anchors/main-v18", text)
            self.assertIn("vs anchors/fundamentals", text)
            self.assertIn("4 skipped", text)
            self.assertIn("total", text)
            self.assertIn("out ", text)
            self.assertLess(len(text.splitlines()), 40)


if __name__ == "__main__":
    unittest.main(verbosity=1)
