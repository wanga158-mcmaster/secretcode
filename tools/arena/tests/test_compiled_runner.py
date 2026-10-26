"""Offline mutations and Windows dummy-process containment; no engine execution."""
from __future__ import annotations

import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compiled_runner as runner
import core
import games
import panel_executables
import population

REAL_COMMAND = runner._command
REAL_SLOTS = core.Slots
REAL_DEPENDENCIES = runner._dependencies

FIXTURE = Path(os.environ.get("ARENA_COMPILED_RUNNER_FIXTURE") or
               core.ROOT / "results/population/compiled-input-contract/portable-route/retained/compiled-portable-full-next")


def fixture_json(name):
    path = FIXTURE / name
    return path if path.is_file() else path.with_suffix(path.suffix + ".txt")


class Slots:
    count = 1

    def __init__(self, *args, **kwargs):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass


class FakeCLI:
    """Supplies saved output bytes; does not simulate or certify gameplay."""
    def __init__(self, case):
        self.case = case
        self.native = []
        self.summary = []
        self.archive_mutator = None
        self.statistics_mutator = None
        self.events_mutator = None
        self.code = 0
        self.raise_after_first = None

    def __call__(self, argv, cwd, directory, timeout):
        directory.mkdir(exist_ok=True)
        if argv[1] == "summary":
            self.summary.append(argv)
            stats = copy.deepcopy(self.case.stats)
            if self.statistics_mutator:
                self.statistics_mutator(stats)
            Path(argv[argv.index("-o") + 1]).write_text(json.dumps(stats), encoding="utf-8")
            raw = b""
            code = 0
        else:
            self.native.append(argv)
            self.case.assertNotIn("--rounds", argv)
            self.case.assertEqual(argv[1], "run-batch")
            jobs = [json.loads(line) for line in Path(argv[argv.index("--jsonl") + 1]).read_text().splitlines()]
            events = []
            for index, job in enumerate(jobs):
                for side in ("teamA", "teamB"):
                    with zipfile.ZipFile(job[side]["path"]) as z:
                        self.case.assertEqual(z.namelist(), ["manifest.json", "bot.wasm"])
                        self.case.assertEqual(json.loads(z.read("manifest.json")), runner.PORTABLE)
                target = Path(job["replay"])
                target.write_bytes(self.case.archive)
                if self.archive_mutator:
                    self.archive_mutator(target)
                events += [{"apiVersion": 1, "event": "started", "id": job["id"], "pid": 100 + index},
                           {"apiVersion": 1, "event": "progress", "id": job["id"], "round": 0,
                            "maxRounds": 2000, "status": "drafting"},
                           {"apiVersion": 1, "event": "progress", "id": job["id"], "round": 1522,
                            "maxRounds": 2000, "status": "finished"},
                           {"apiVersion": 1, "event": "finished", "id": job["id"], "replay": str(target)}]
                if self.raise_after_first:
                    break
            if not self.raise_after_first:
                events.append({"apiVersion": 1, "event": "batchFinished", "failed": 0})
            if self.events_mutator:
                self.events_mutator(events)
            raw = b"".join((json.dumps(value) + "\n").encode() for value in events)
            code = self.code
        (directory / "stdout.bin").write_bytes(raw)
        (directory / "stderr.bin").write_bytes(b"")
        if argv[1] == "run-batch" and self.raise_after_first:
            raise self.raise_after_first
        return {"pid": 10, "returncode": code, "timed_out": False,
                "tree_exit_confirmed": True, "forced_termination": False,
                "stdout": runner._desc(directory / "stdout.bin"), "stderr": runner._desc(directory / "stderr.bin")}


class CompiledRunner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.archive = (FIXTURE / "private-run/output.zip").read_bytes()
        cls.stats = json.loads(fixture_json("summary.json").read_bytes())
        assert hashlib.sha256(cls.archive).hexdigest() == "2a0946a306176b102691d8d1e5c26c760d33cedd79cbca50d5939cab28fbf4e0"
        assert hashlib.sha256(fixture_json("summary.json").read_bytes()).hexdigest() == "243eb60395bdc2274b2afe5bcadea1ef4a429db779e8292766284f3131699f2b"
        cls.fixture_engine = {"version": cls.stats["match"]["engineVersion"],
                              "buildHash": cls.stats["match"]["engineBuildHash"]}

    def setUp(self):
        self.home = Path(tempfile.mkdtemp(prefix="compiled-runner-test-", dir=core.ROOT / ".cache"))
        self.snapshots = []
        self.keys = []
        self.entrants = []
        self.raw = b"\0asm\x01\0\0\0"
        self.injected = self.raw + b"\0\x01\0"  # different inert custom section, never executed
        for name in ("a", "b"):
            rel = Path(f"bots/compiledfixture/{name}/src/main.cpp")
            source = ("// " + str(self.home) + name + "\nint main() { return 0; }\n").encode()
            version = core.version_of({core.ROOT / rel: source})
            snap = core.CACHE / "snap" / version
            self.assertFalse(snap.exists())
            directory = snap / rel.parent.parent
            (directory / "src").mkdir(parents=True)
            (directory / "src/main.cpp").write_bytes(source)
            build = directory / ".cambc/build-release"
            build.mkdir(parents=True)
            (build / "bot.wasm").write_bytes(self.raw)
            (build / "bot-injected.wasm").write_bytes(self.injected)
            ident = f"compiledfixture/{name}"
            (snap / "meta.json").write_text(json.dumps({"bot": ident, "version": version, "overrides": {}, "files": 1}))
            (snap / ".ready").write_text("offline fixture\n")
            (snap / ".toolchain").write_text(core.toolchain() + "\n")
            self.snapshots.append(snap)
            self.keys.append(ident + "@" + version)
            self.entrants.append(core.Entrant(core.Bot("compiledfixture", name), version, directory, True))
        self.map = self.home / "map.map26a"
        self.map.write_bytes((core.ROOT / "maps/00collision.map26a").read_bytes())
        self.number = 0
        self.host = FakeCLI(self)
        self.patchers = [patch.object(runner, "_command", side_effect=self.host), patch.object(core, "Slots", Slots),
                         patch.object(runner, "_dependencies", side_effect=self.fixture_dependencies)]
        for module, name in ((core, "prepare"), (core, "build"), (games, "play"), (games, "_warm"),
                             (games, "_run_batch"), (population, "regress")):
            self.patchers.append(patch.object(module, name, side_effect=AssertionError("forbidden source runner")))
        for p in self.patchers:
            p.start()
        self.addCleanup(self.cleanup)

    def cleanup(self):
        for p in reversed(self.patchers):
            p.stop()
        for target in self.snapshots + [self.home]:
            resolved = target.resolve()
            self.assertEqual(resolved, target)
            self.assertTrue(resolved.is_relative_to((core.ROOT / ".cache").resolve()))
            self.assertNotEqual(resolved, (core.ROOT / ".cache").resolve())
            shutil.rmtree(resolved)

    def out(self):
        self.number += 1
        return self.home / f"run-{self.number}"

    def fixture_dependencies(self):
        # FakeCLI returns the byte-pinned historical archive, not current engine output.
        # Keep the real installed-bundle validation and dependency drift pins, then bind
        # expected output identity to the retained fixture and pin that source as well.
        cli, _, pins = REAL_DEPENDENCIES()
        return cli, dict(self.fixture_engine), pins + [runner._desc(fixture_json("summary.json"))]

    def play(self, **kwargs):
        return runner.play(*self.keys, self.map, self.out(), **kwargs)

    @staticmethod
    def rewrite(path, change):
        with zipfile.ZipFile(path) as z:
            entries = [(i.filename, z.read(i)) for i in z.infolist()]
        entries = change(entries)
        with zipfile.ZipFile(path, "w") as z:
            for name, raw in entries:
                z.writestr(name, raw)

    def test_saved_output_exact_projection_and_scoped_coverage(self):
        result = self.play()
        self.assertTrue(result["execution_pass"])
        self.assertEqual(result["engine"], self.fixture_engine)
        self.assertTrue(result["health_pass"])
        self.assertEqual(result["coverage"]["status"], "unknown")
        self.assertFalse(result["admitted"])
        row = result["rows"][0]
        expected = games.parse_statistics(self.stats)
        channels, notes = games.parse_replay(FIXTURE / "private-run/output.zip", expected["roster"])
        self.assertFalse(notes)
        for side in "AB":
            expected[side].update(channels[side])
            self.assertEqual(row[side], expected[side])
        self.assertEqual(row["rounds"], 1521)
        self.assertEqual(row["native_match"]["roundsStarted"], 1522)
        self.assertTrue(row["native_match"]["finalRoundPartial"])
        self.assertFalse(row["truncated"])
        self.assertFalse(row["brief_result"]["result"]["detailedCaptureComplete"])
        self.assertEqual(len(self.host.native), 1)
        self.assertEqual(len(self.host.summary), 1)

    def test_selected_role_is_exact_captured_payload_and_both_roles_pinned(self):
        result = self.play(role="injected")
        binding = result["bindings"][0]
        self.assertNotEqual(binding["raw"]["sha256"], binding["injected"]["sha256"])
        self.assertEqual(binding["inner_bot_sha256"], hashlib.sha256(self.injected).hexdigest())
        manifest = Path(self.host.native[0][self.host.native[0].index("--jsonl") + 1])
        job = json.loads(manifest.read_text())
        with zipfile.ZipFile(job["teamA"]["path"]) as z:
            self.assertEqual(z.read("bot.wasm"), self.injected)
        self.assertIn("unequal", " ".join(result["limits"]))

    def test_early_excluded_and_dot_keys_never_reach_source_validator(self):
        with patch.object(population, "_snapshot", side_effect=AssertionError("source touched")):
            for key in ("main/x@0123456789ab", "MAIN/x@0123456789ab",
                        "surprise/x@0123456789ab", "SURPRISE/x@0123456789ab",
                        "contenders/x@0123456789ab", "CONTENDERS/x@0123456789ab",
                        "../x@0123456789ab", "odd/..@0123456789ab", "odd/x", "odd/x@ABCDEF123456"):
                with self.subTest(key=key), self.assertRaises(core.ArenaError):
                    runner.play(self.keys[0], key, self.map, self.out())
        self.assertFalse(self.host.native)

    def test_heuristics_and_anchor_keys_are_permitted_before_snapshot_validation(self):
        for suite in ("heuristics", "anchors"):
            self.assertEqual(runner._key(suite + "/x@0123456789ab"), (suite, "x", "0123456789ab"))

    def test_output_existing_or_outside_cache_refused_without_dispatch(self):
        existing = self.out()
        existing.mkdir()
        (existing / "marker").write_text("keep")
        with self.assertRaises(FileExistsError):
            runner.play(*self.keys, self.map, existing)
        self.assertEqual((existing / "marker").read_text(), "keep")
        with self.assertRaises(core.ArenaError):
            runner.play(*self.keys, self.map, core.ROOT / "results/not-authorized")
        self.assertFalse(self.host.native)

    def test_source_drift_after_capture_prevents_dispatch(self):
        original = runner._pack
        changed = False
        def mutate(path, binding):
            nonlocal changed
            value = original(path, binding)
            if not changed:
                changed = True
                (self.entrants[0].path / ".cambc/build-release/bot.wasm").write_bytes(self.raw + b"x")
            return value
        with patch.object(runner, "_pack", side_effect=mutate), self.assertRaises(core.ArenaError):
            self.play()
        self.assertFalse(self.host.native)

    def test_owned_input_drift_during_slot_wait_prevents_dispatch(self):
        case = self
        class MutatingSlots(Slots):
            def __enter__(self):
                for path in case.home.glob("run-*/inputs/*.zip"):
                    path.write_bytes(path.read_bytes() + b"drift")
                return self
        with patch.object(core, "Slots", MutatingSlots), self.assertRaises(core.ArenaError):
            self.play()
        self.assertFalse(self.host.native)

    def test_nonzero_even_with_complete_plausible_stats_never_passes(self):
        self.host.code = 1
        result = self.play()
        self.assertFalse(result["execution_pass"])
        self.assertFalse(result["health_pass"])
        self.assertTrue(result["rows"][0]["measurement_complete"])
        self.assertFalse(result["rows"][0]["execution_complete"])

    def test_required_output_members_and_crc_format_refuse_unknown_health(self):
        for change in (
            lambda rows: [(n, b) for n, b in rows if n != "team-a.fuel"],
            lambda rows: [(n, b) for n, b in rows if n != "team-b.stdout"],
            lambda rows: [(n, b) for n, b in rows if n != "summary.camstat"],
            lambda rows: rows + [rows[0]],
            lambda rows: [(n, b"wrong" if n == "summary.camstat" else b) for n, b in rows],
            lambda rows: [(n, b + b"x" if n == "team-a.fuel" else b) for n, b in rows],
        ):
            with self.subTest(change=change):
                self.host.archive_mutator = lambda p: self.rewrite(p, change)
                result = self.play()
                self.assertFalse(result["execution_pass"])
                self.assertIsNone(result["rows"][0]["health"])
                self.assertIsNone(result["rows"][0]["A"])

    def test_native_completeness_required_metrics_identity_roster_and_outcome(self):
        changes = [lambda s: s["diagnostics"].update(detailedCaptureComplete=False),
                   lambda s: s["diagnostics"].pop("botFaults"),
                   lambda s: s["champions"][0]["metrics"].pop("earnedGold"),
                   lambda s: s.pop("events"),
                   lambda s: s["roster"]["champions"].append(s["roster"]["champions"][0]),
                   lambda s: s["match"].update(engineVersion="9.9.9"),
                   lambda s: s["match"].update(engineBuildHash="0" * 64),
                   lambda s: s["match"].update(mapHash="0" * 64),
                   lambda s: s["match"].update(roundsStarted=0),
                   lambda s: s["match"].update(outcome="draw")]
        for change in changes:
            with self.subTest(change=change):
                self.host.statistics_mutator = change
                result = self.play()
                self.assertFalse(result["execution_pass"])
                self.assertFalse(result["rows"][0]["measurement_complete"])
                self.assertIsNone(result["rows"][0]["health"])

    def test_bad_machine_events_partial_unknown_duplicate_and_truncated(self):
        changes = [lambda e: e.insert(-1, copy.deepcopy(e[-2])),
                   lambda e: e[0].update(id="unplanned"),
                   lambda e: e[1].update(extra="unknown"),
                   lambda e: e[2].update(status="truncated"),
                   lambda e: e.pop(),
                   lambda e: e[-1].update(failed=1)]
        for change in changes:
            with self.subTest(change=change):
                self.host.events_mutator = change
                result = self.play()
                self.assertFalse(result["execution_pass"])

    def panel(self):
        panel = {"entries": [{"snapshot": self.keys[1], "bot": "compiledfixture/b", "behavior": "fixture/old",
                              "contract": {"bot": "compiledfixture/b"}, "maps": {"saved-one": runner._digest(self.map),
                              "saved-two": runner._digest(self.map)}, "sides": ["B", "A"]}]}
        path = self.home / "panel.json"
        path.write_text(json.dumps(panel))
        manifest = self.home / "executables.json"
        validator = patch.object(population, "validate_panel", return_value=[self.entrants[1]])
        validator.start()
        self.addCleanup(validator.stop)
        panel_executables.pin(path, manifest)
        return panel, path, manifest

    def full_panel(self):
        """Own all source/module inputs while retaining the full pool schedule."""
        bundle = json.loads((core.ROOT / "docs/reference/current-version.json").read_bytes())
        maps = {name: runner._digest(core.map_path(name)) for name in core.pool_maps()}
        catalog = json.loads((core.ROOT / "maps/pool.json").read_text(encoding="utf-8"))["maps"]
        self.assertTrue(catalog)
        self.assertEqual(len(maps), len(catalog))
        self.assertEqual(set(maps), {core.map_path(entry["name"]).stem for entry in catalog})
        panel = {"schema": population.SCHEMA, "kind": "population-panel", "mode": "full",
                 "experimental": True, "engine": bundle["engine"], "toolchain": core.toolchain(),
                 "bundle": runner._digest(core.ROOT / "docs/reference/current-version.json"), "entries": []}
        for entrant in self.entrants:
            contract = {"bot": entrant.bot.id, "id": entrant.bot.id + "/fixture"}
            source = population._snapshot_files(entrant)
            identity = {"snapshot": entrant.key, "source": source, "working_source": source,
                        "overrides": {}, "contract": population.canonical(contract),
                        **population._measurement_files(contract)}
            panel["entries"].append({"snapshot": entrant.key, "bot": entrant.bot.id,
                                     "behavior": contract["id"], "contract": contract, "identity": identity,
                                     "source": source, "overrides": {}, "maps": dict(maps), "sides": ["A", "B"]})
        path = self.home / "full-panel.json"
        path.write_text(json.dumps(panel))
        manifest = self.home / "full-executables.json"
        panel_executables.pin(path, manifest)
        return panel, path, manifest

    def test_regress_exact_old_maps_sides_and_byte_validator_without_observers(self):
        panel, path, manifest = self.panel()
        with patch.object(core, "map_path", return_value=self.map):
            result = runner.regress(self.keys[0], manifest, self.out())
        self.assertEqual(result["expected_games"], 4)
        self.assertEqual([(r["map"], r["tags"]["subject"]) for r in result["rows"]],
                         [(name, side) for name in panel["entries"][0]["maps"] for side in ["B", "A"]])
        self.assertTrue(result["execution_pass"])
        self.assertEqual(result["coverage"]["status"], "unknown")
        self.assertEqual(len(self.host.native), 1)
        original = path.read_bytes()
        path.write_bytes(original + b" ")
        with self.assertRaises(core.ArenaError):
            runner.regress(self.keys[0], manifest, self.out())
        self.assertEqual(len(self.host.native), 1)

    def test_late_excluded_panel_entry_is_rejected_before_first_source_access(self):
        panel, path, manifest = self.panel()
        original_entries = copy.deepcopy(panel["entries"])
        executable = json.loads(manifest.read_bytes())
        for bot in ("main/x", "MAIN/x", "surprise/x", "SURPRISE/x", "contenders/x", "CONTENDERS/x"):
            panel["entries"] = original_entries + [{"snapshot": bot + "@0123456789ab", "bot": bot,
                                                    "contract": {"bot": bot}}]
            path.write_text(json.dumps(panel))
            executable["panel"]["sha256"] = runner._digest(path)
            manifest.write_text(json.dumps(executable))
            with self.subTest(bot=bot), \
                    patch.object(runner, "_preflight_snapshot", side_effect=AssertionError("source preflight touched")) as preflight, \
                    patch.object(population, "_snapshot", side_effect=AssertionError("source touched")) as snapshot:
                with self.assertRaisesRegex(core.ArenaError, "outside.*mandate"):
                    runner.regress(self.keys[0], manifest, self.out())
                preflight.assert_not_called()
                snapshot.assert_not_called()
        self.assertFalse(self.host.native)

    def test_partial_exception_salvages_completed_prefix_once_and_reraises_original(self):
        panel, path, manifest = self.panel()
        original = OSError("controlled process transport failure")
        self.host.raise_after_first = original
        out = self.out()
        with patch.object(core, "map_path", return_value=self.map), self.assertRaises(OSError) as raised:
            runner.regress(self.keys[0], manifest, out)
        self.assertIs(raised.exception, original)
        result = json.loads((out / "summary.json").read_bytes())
        lines = [json.loads(line) for line in (out / "games.jsonl").read_text().splitlines()]
        self.assertEqual(len(lines), 4)
        self.assertEqual(len({r["id"] for r in lines}), 4)
        self.assertTrue(lines[0]["measurement_complete"])
        self.assertFalse(result["execution_pass"])
        self.assertIn("controlled process", result["exception"])

    def test_duplicate_or_escaping_job_id_rejected_before_output(self):
        bindings = {key: runner._capture(key, "raw") for key in self.keys}
        spec = {"id": "../escape", "a": self.keys[0], "b": self.keys[1], "map": "x"}
        out = self.out()
        with self.assertRaises(core.ArenaError):
            runner._run([spec], bindings, out, "raw", 1, 120, {})
        self.assertFalse(out.exists())
        spec["id"] = "game-000000"
        with self.assertRaises(core.ArenaError):
            runner._run([spec, spec], bindings, out, "raw", 1, 120, {})

    def test_saved_actual_machine_stream_validates_and_partial_line_does_not(self):
        path = FIXTURE / "stdout.bin"
        events = [json.loads(line) for line in path.read_bytes().splitlines()]
        planned = {"portable-full-selfplay": runner._path(next(e["replay"] for e in events if e["event"] == "finished").removeprefix("\\\\?\\"))}
        valid = runner._events(path, planned)
        self.assertFalse(valid["errors"])
        self.assertEqual(valid["jobs"]["portable-full-selfplay"]["terminal"], "finished")
        broken = self.home / "machine.bin"
        broken.write_bytes(path.read_bytes().rstrip(b"\n"))
        self.assertTrue(runner._events(broken, planned)["errors"])

    def test_full_frozen_panel_derives_complete_current_pool_cases_without_dispatch(self):
        panel, _, manifest = self.full_panel()
        saved_bindings = json.loads(manifest.read_bytes())
        with patch.object(runner, "_capture", wraps=runner._capture) as capture, \
             patch.object(runner, "_run", side_effect=lambda specs, *args: specs) as dispatch:
            specs = runner.regress(self.keys[0], manifest, self.out())
        expected_games = sum(len(entry["maps"]) * len(entry["sides"]) for entry in panel["entries"])
        self.assertGreater(expected_games, 0)
        self.assertEqual(len(specs), expected_games)
        self.assertEqual(len({s["id"] for s in specs}), expected_games)
        self.assertEqual([(s["map"], s["map_sha256"], s["tags"]["subject"], s["tags"]["population_behavior"]) for s in specs],
                         [(name, digest, side, entry["behavior"]) for entry in panel["entries"]
                          for name, digest in entry["maps"].items() for side in entry["sides"]])
        self.assertEqual({s["map"] for s in specs}, {name for entry in panel["entries"] for name in entry["maps"]})
        self.assertEqual(len({(s["map"], s["tags"]["subject"], s["tags"]["population_behavior"])
                              for s in specs}), expected_games)
        self.assertEqual([call.args[0] for call in capture.call_args_list],
                         [self.keys[0]] + [row["snapshot"] for row in saved_bindings["snapshots"]
                                         if row["snapshot"] != self.keys[0]])
        dispatch.assert_called_once()
        self.assertFalse(self.host.native)

    def test_stale_snapshot_refuses_current_toolchain(self):
        (self.snapshots[0] / ".toolchain").write_text("historical-fixture-toolchain\n")
        with self.assertRaisesRegex(core.ArenaError, "snapshot toolchain is stale"):
            runner._capture(self.keys[0], "raw")
        self.assertFalse(self.host.native)

    def test_historical_full_panel_refuses_current_bundle_before_dispatch(self):
        panel, panel_path, manifest = self.full_panel()
        panel["bundle"] = "0" * 64
        panel_path.write_text(json.dumps(panel))
        saved_bindings = json.loads(manifest.read_bytes())
        saved_bindings["panel"]["sha256"] = runner._digest(panel_path)
        manifest.write_text(json.dumps(saved_bindings))
        self.assertNotEqual(panel["bundle"], runner._digest(core.ROOT / "docs/reference/current-version.json"))
        with patch.object(runner, "_run", side_effect=AssertionError("stale panel dispatched")) as dispatch, \
             patch.object(runner, "_capture", side_effect=AssertionError("stale panel captured")) as capture, \
             self.assertRaisesRegex(core.ArenaError, "panel engine/toolchain bundle is stale"):
            runner.regress(self.keys[0], manifest, self.out())
        capture.assert_not_called()
        dispatch.assert_not_called()
        self.assertFalse(self.host.native)

    def test_subprocess_chunking_schedules_every_case_without_resume(self):
        self.panel()
        manifest = self.home / "executables.json"
        with patch.object(core, "map_path", return_value=self.map), patch.object(runner, "MANIFEST_CHUNK", 2):
            result = runner.regress(self.keys[0], manifest, self.out())
        self.assertTrue(result["execution_pass"])
        self.assertEqual(result["recorded_games"], 4)
        self.assertEqual(len(self.host.native), 2)
        self.assertEqual(len({r["id"] for r in result["rows"]}), 4)

    def test_secondary_finalization_failure_does_not_mask_original_exception(self):
        self.panel()
        original = OSError("original transport exception")
        self.host.raise_after_first = original
        out = self.out()
        write = runner._write
        def fail_final(path, value):
            if path == out / "summary.json":
                raise OSError("secondary publication exception")
            return write(path, value)
        with patch.object(core, "map_path", return_value=self.map), patch.object(runner, "_write", side_effect=fail_final):
            with self.assertRaises(OSError) as raised:
                runner.regress(self.keys[0], self.home / "executables.json", out)
        self.assertIs(raised.exception, original)
        self.assertIn("secondary publication", " ".join(original.__notes__))
        rows = [json.loads(line) for line in (out / "games.jsonl").read_text().splitlines()]
        self.assertEqual(len(rows), len({r["id"] for r in rows}))
        self.assertTrue((out / "jobs/game-000000/output.zip").is_file())

    def test_row_publication_failure_retains_completed_native_archive(self):
        out = self.out()
        opening = Path.open
        failure = OSError("controlled row publication failure")
        def fail_row(path, mode="r", *args, **kwargs):
            if path == out / "games.jsonl" and mode == "a":
                raise failure
            return opening(path, mode, *args, **kwargs)
        with patch.object(Path, "open", new=fail_row), self.assertRaises(OSError) as raised:
            runner.play(*self.keys, self.map, out)
        self.assertIs(raised.exception, failure)
        result = json.loads((out / "summary.json").read_bytes())
        self.assertFalse(result["execution_pass"])
        self.assertTrue(result["rows"][0]["measurement_complete"])
        self.assertTrue((out / "jobs/game-000000/output.zip").is_file())

    def test_crc_error_is_not_projected_as_zero_health(self):
        def corrupt(path):
            data = bytearray(path.read_bytes())
            with zipfile.ZipFile(path) as z:
                offset = z.start_dir
            while data[offset:offset + 4] == b"PK\x01\x02":
                names, extra, comment = struct.unpack_from("<HHH", data, offset + 28)
                name = bytes(data[offset + 46:offset + 46 + names]).decode()
                if name == "team-a.fuel":
                    data[offset + 16] ^= 1
                    path.write_bytes(data)
                    return
                offset += 46 + names + extra + comment
            self.fail("fixture central member absent")
        self.host.archive_mutator = corrupt
        result = self.play()
        self.assertFalse(result["execution_pass"])
        self.assertIsNone(result["rows"][0]["health"])
        self.assertIn("CRC", result["rows"][0]["error"])

    def test_existing_health_flags_are_not_waived_for_valid_output(self):
        self.host.archive_mutator = lambda path: self.rewrite(path, lambda rows:
            [(name, raw + b"\nerror controlled test\n" if name == "team-a.stdout" else raw) for name, raw in rows])
        result = self.play()
        self.assertTrue(result["execution_pass"])
        self.assertFalse(result["health_pass"])
        self.assertEqual(result["rows"][0]["A"]["errors"], 1)
        self.assertEqual(result["rows"][0]["health"]["A"], ["errors 1"])

    def test_cassi_two_body_one_pick_uses_existing_roster_projection(self):
        stats = copy.deepcopy(self.stats)
        brief = json.loads(fixture_json("native-result.json").read_bytes())
        roster = stats["roster"]["champions"]
        extra = copy.deepcopy(roster[0])
        extra.update(id=6, name="Issac")
        roster[0]["name"] = "Cassi"
        roster.append(extra)
        champion = copy.deepcopy(stats["champions"][0])
        champion.update(id=6, name="Issac")
        champion["metrics"] = {key: 0 for key in champion["metrics"]}
        stats["champions"][0]["name"] = "Cassi"
        stats["champions"].append(champion)
        for event in stats["events"]:
            if event.get("owner", {}).get("id") == 0:
                event["owner"]["name"] = "Cassi"
        for kind in ("campClear", "fuelExhausted"):
            stats["events"].append({"kind": kind, "owner": {"id": 6, "kind": "champion", "team": extra["team"], "name": "Issac"}, "entries": []})
        runner._statistics(stats, brief, runner._digest(self.map), self.fixture_engine)
        self.assertEqual(games.parse_statistics(stats)["roster"], {"A": (3, 4), "B": (3, 3)})

    def test_manifest_or_panel_drift_after_validator_prevents_dispatch(self):
        panel, panel_path, manifest = self.panel()
        validate = panel_executables.validate
        for target in (manifest, panel_path):
            before = target.read_bytes()
            def changed(path):
                result = validate(path)
                target.write_bytes(before + b" ")
                return result
            with self.subTest(target=target), patch.object(panel_executables, "validate", side_effect=changed):
                with self.assertRaises(core.ArenaError):
                    runner.regress(self.keys[0], manifest, self.out())
            target.write_bytes(before)
        self.assertFalse(self.host.native)

    def test_missing_duplicate_health_owners_timestamps_and_codes_are_unknown(self):
        def drop_one(s):
            index = next(i for i, group in enumerate(s["events"]) if group["kind"] == "fuelExhausted")
            s["events"].pop(index)
        def drop_all(s):
            s["events"] = [group for group in s["events"] if group["kind"] != "fuelExhausted"]
        def duplicate(s):
            s["events"].append(copy.deepcopy(next(g for g in s["events"] if g["kind"] == "campClear")))
        def stamp(s, value):
            next(g for g in s["events"] if g["kind"] == "campClear" and g["entries"])["entries"][0]["timestamp"] = value
        valid_stamp = copy.deepcopy(next(g for g in self.stats["events"] if g["kind"] == "campClear" and g["entries"])["entries"][0]["timestamp"])
        for change in (drop_one, drop_all, duplicate,
                       lambda s: stamp(s, {}),
                       lambda s: stamp(s, {k: v for k, v in valid_stamp.items() if k != "ordinal"}),
                       lambda s: stamp(s, dict(valid_stamp, round=True)),
                       lambda s: stamp(s, dict(valid_stamp, phase="")),
                       lambda s: s["diagnostics"].pop("codes"),
                       lambda s: s["diagnostics"].update(codes=None),
                       lambda s: s["diagnostics"].update(codes=["unknown-native-condition"])):
            with self.subTest(change=change):
                self.host.statistics_mutator = change
                result = self.play()
                self.assertFalse(result["health_pass"])
                self.assertFalse(result["rows"][0]["measurement_complete"])
                self.assertIsNone(result["rows"][0]["A"])

    def test_explicit_zero_health_groups_are_valid_and_worker_identity_is_retained(self):
        out = self.out()
        result = runner.play(*self.keys, self.map, out)
        self.assertTrue(result["rows"][0]["measurement_complete"])
        self.assertTrue(result["health_pass"])
        batch = json.loads((out / "batches/0000/machine-validation.json").read_bytes())
        self.assertEqual(batch["jobs"]["game-000000"]["worker_pid"], 100)

    def test_pre_read_metadata_traversal_exclusions_in_private_root(self):
        fake = self.home / "private-root"
        denied = [fake / "bots" / suite for suite in ("MAIN", "SURPRISE", "CONTENDERS")]
        for directory in denied:
            directory.mkdir(parents=True)
            (directory / "panel.json").write_text("never read")
        opening = Path.open
        reads = []
        def guarded(path, mode="r", *args, **kwargs):
            if "r" in mode and any(path.resolve().is_relative_to(directory.resolve()) for directory in denied):
                reads.append(path)
                raise AssertionError("excluded data read")
            return opening(path, mode, *args, **kwargs)
        with patch.object(runner, "ROOT", fake), patch.object(Path, "open", new=guarded):
            for suite in ("MAIN", "SURPRISE", "CONTENDERS"):
                for value in (f"bots/specialists/../{suite}/panel.json", f"bots/{suite.lower()}/panel.json"):
                    with self.subTest(value=value), self.assertRaises(core.ArenaError):
                        runner._frozen_json(runner._path(value))
                with self.assertRaises(core.ArenaError):
                    runner._excluded_paths({"path": f"bots/specialists/../{suite}/panel.json", "sha256": "0" * 64})
                with self.assertRaises(core.ArenaError):
                    runner._preflight_measurements({"bot": "safe/x", "observer_paths": [f"bots/safe/../{suite}"]})
        self.assertEqual(reads, [])

    @unittest.skipUnless(os.name == "nt", "Windows directory junction fixture")
    def test_redirected_source_and_metadata_are_blocked_before_legacy_reader(self):
        fake = self.home / "alias-root"
        denied = fake / "bots/MAIN"
        denied.mkdir(parents=True)
        (denied / "main.cpp").write_text("never read")
        cache = fake / ".cache/arena"
        version = "0123456789ab"
        snap = cache / "snap" / version
        directory = snap / "bots/safe/x"
        directory.mkdir(parents=True)
        for name, raw in (("meta.json", json.dumps({"bot": "safe/x", "version": version})), (".ready", "ready"), (".toolchain", "fixture")):
            (snap / name).write_text(raw)
        link = directory / "src"
        created = runner.subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(denied)], capture_output=True)
        self.assertEqual(created.returncode, 0, created.stderr.decode(errors="replace"))
        opening = Path.open
        reads = []
        def guarded(path, mode="r", *args, **kwargs):
            if "r" in mode and path.resolve().is_relative_to(denied.resolve()):
                reads.append(path)
                raise AssertionError("alias reached excluded source")
            return opening(path, mode, *args, **kwargs)
        try:
            with patch.object(runner, "ROOT", fake), patch.object(core, "CACHE", cache), patch.object(Path, "open", new=guarded), patch.object(population, "_snapshot", side_effect=AssertionError("legacy reader reached")):
                with self.assertRaises(core.ArenaError):
                    runner._capture("safe/x@" + version, "raw")
                with self.assertRaises(core.ArenaError):
                    runner._frozen_json(link / "main.cpp")
            self.assertEqual(reads, [])
        finally:
            link.rmdir()  # remove only the private fixture junction, never its target

    def test_process_start_publication_failure_still_waits_for_launched_process(self):
        class Process:
            pid = 1234
            returncode = None
            waits = 0
            empty_confirmed = True
            def wait(self, timeout):
                self.waits += 1
                self.returncode = 0
                return 0
            def poll(self):
                return self.returncode
            def close(self):
                pass
            def record(self):
                return {"empty_confirmed": True, "forced_termination": False}
        process = Process()
        failure = OSError("controlled start-record publication failure")
        directory = self.home / "command"
        directory.mkdir()
        write = runner._write
        def publish(path, value):
            if path.name == "process-start.json":
                raise failure
            return write(path, value)
        with patch.object(runner, "_WinJobProcess", return_value=process), patch.object(runner, "_write", side_effect=publish):
            with self.assertRaises(OSError) as raised:
                REAL_COMMAND(["fake-only"], self.home, directory, 1)
        self.assertIs(raised.exception, failure)
        self.assertEqual(process.waits, 1)
        self.assertEqual(process.returncode, 0)
        self.assertTrue((directory / "process-end.json").is_file())

    def test_fatal_unknown_tree_cannot_claim_stopped_health_or_release_slot(self):
        class Process:
            pid, assigned, empty_confirmed = 9876, True, False
            closed = False
            def terminate_tree(self):
                self.empty_confirmed = True
            def close(self):
                self.closed = True
            def record(self):
                return {"empty_confirmed": self.empty_confirmed}
        class HeldSlots(Slots):
            exits = 0
            def __exit__(self, *args):
                self.exits += 1
        process, slots = Process(), HeldSlots()
        error = RuntimeError("synthetic fatal cleanup unknown")
        error.compiled_process, error.compiled_live_pid = process, process.pid
        out = self.out()
        try:
            with patch.object(core, "Slots", return_value=slots), patch.object(runner, "_command", side_effect=error), self.assertRaises(RuntimeError) as raised:
                runner.play(*self.keys, self.map, out)
            self.assertIs(raised.exception, error)
            result = json.loads((out / "summary.json").read_bytes())
            self.assertFalse(result["stopped"])
            self.assertTrue(result["jobs"])
            self.assertFalse(result["execution_pass"])
            self.assertFalse(result["health_pass"])
            self.assertEqual(result["rows"], [])
            self.assertEqual(slots.exits, 0)
            self.assertFalse(self.host.summary)
        finally:
            self.assertTrue(runner.confirm_pending_cleanup())
        self.assertEqual(slots.exits, 1)
        self.assertTrue(process.closed)


class ProgressProtocol(unittest.TestCase):
    """Saved actual8-job stdout and mutations only; no process/game dispatch."""
    fixture = core.ROOT / ".cache/arena/orchestration/compiled-runner-progress-next/fixture"

    @classmethod
    def setUpClass(cls):
        if not (cls.fixture / "stdout.bin").is_file() or not (cls.fixture / "input.jsonl").is_file():
            # The saved stream lived in .cache (scratch) and was removed by the 24 Oct cleanup; its pinned
            # hash cannot be regenerated. A new capture belongs in tools/arena/tests/fixtures/.
            raise unittest.SkipTest(f"saved compiled-runner stream missing: {cls.fixture}")
        cls.stdout = (cls.fixture / "stdout.bin").read_bytes()
        jobs = (cls.fixture / "input.jsonl").read_bytes()
        assert hashlib.sha256(cls.stdout).hexdigest() == "6dc0e81f4c53d60f0bb1d193d3ca56a165c74a1f756575fa25569395bdff124d"
        assert hashlib.sha256(jobs).hexdigest() == "2fcdd9fc98fc2eb382ca6e1fca993d5b6cf3ca21c19bc16ad9c222965fbc607e"
        cls.planned = {row["id"]: runner._path(row["replay"]) for row in map(json.loads, jobs.splitlines())}
        cls.events = list(map(json.loads, cls.stdout.splitlines()))
        assert len(cls.planned) == 8

    def setUp(self):
        self.home = Path(tempfile.mkdtemp(prefix="parser-", dir=self.fixture.parent))
        self.number = 0
        def cleanup():
            self.assertEqual(self.home.resolve(), self.home)
            self.assertTrue(self.home.is_relative_to(self.fixture.parent.resolve()))
            shutil.rmtree(self.home)
        self.addCleanup(cleanup)

    def parse(self, events):
        self.number += 1
        path = self.home / f"events-{self.number}.bin"
        path.write_bytes(b"".join((json.dumps(event) + "\n").encode() for event in events))
        return runner._events(path, self.planned)

    def test_saved_actual_eight_job_stream_accepts_sampled_running_progress(self):
        result = runner._events(self.fixture / "stdout.bin", self.planned)
        self.assertEqual(result["counts"]["started"], 8)
        self.assertEqual(result["counts"]["finished"], 8)
        self.assertEqual(result["batch"]["failed"], 0)
        self.assertFalse(result["errors"])
        self.assertEqual(sum(state["status"] == "running" for state in result["jobs"].values()), 6)
        self.assertEqual(sum(state["status"] == "finished" for state in result["jobs"].values()), 2)
        for key, state in result["jobs"].items():
            with self.subTest(key=key):
                self.assertEqual(state["terminal"], "finished")
                self.assertFalse(state["errors"])
                self.assertIs(type(state["worker_pid"]), int)

    def test_each_missing_or_duplicate_finished_still_refuses(self):
        for key in self.planned:
            events = copy.deepcopy(self.events)
            index = next(i for i, event in enumerate(events) if event["event"] == "finished" and event["id"] == key)
            with self.subTest(key=key, mutation="missing"):
                missing = self.parse(events[:index] + events[index + 1:])
                self.assertIsNone(missing["jobs"][key]["terminal"])
                self.assertIn("job lifecycle incomplete", missing["jobs"][key]["errors"])
            with self.subTest(key=key, mutation="duplicate"):
                events.insert(index + 1, copy.deepcopy(events[index]))
                self.assertTrue(self.parse(events)["errors"])

    def test_authoritative_error_is_retained_and_batch_must_match(self):
        events = copy.deepcopy(self.events)
        key = next(iter(self.planned))
        index = next(i for i, event in enumerate(events) if event["event"] == "finished" and event["id"] == key)
        failure = {"apiVersion": 1, "event": "error", "id": key, "errorCode": "retained-test-error", "diagnostic": "synthetic mutation"}
        events[index] = failure
        events[-1]["failed"] = 1
        result = self.parse(events)
        self.assertFalse(result["errors"])
        self.assertEqual(result["jobs"][key]["terminal"], "error")
        self.assertEqual(result["jobs"][key]["errors"], [failure])
        self.assertEqual(result["batch"]["failed"], 1)
        events[-1]["failed"] = 0
        self.assertIn("batch failed count disagrees with terminal records", self.parse(events)["errors"])

    def test_missing_duplicate_failed_and_malformed_batch_still_refuse(self):
        mutations = [lambda e: e.pop(), lambda e: e.append(copy.deepcopy(e[-1])),
                     lambda e: e[-1].update(failed=1), lambda e: e[-1].update(failed=-1),
                     lambda e: e[-1].update(failed=False), lambda e: e[-1].update(extra="unexpected")]
        for change in mutations:
            with self.subTest(change=change):
                events = copy.deepcopy(self.events)
                change(events)
                self.assertTrue(self.parse(events)["errors"])

    def test_identity_path_order_and_truncation_gates_are_preserved(self):
        key = next(iter(self.planned))
        finish = next(i for i, event in enumerate(self.events) if event["event"] == "finished" and event["id"] == key)
        progress = next(i for i, event in enumerate(self.events) if event["event"] == "progress" and event["id"] == key)
        mutations = [lambda e: e[finish].update(id="unknown-job"),
                     lambda e: e[finish].update(replay=str(self.home / "different.zip")),
                     lambda e: e.insert(0, e.pop(finish)),
                     lambda e: e[finish].update(extra="unexpected"),
                     lambda e: e[progress].update(status="truncated")]
        for change in mutations:
            with self.subTest(change=change):
                events = copy.deepcopy(self.events)
                change(events)
                result = self.parse(events)
                self.assertTrue(result["errors"] or any(state["errors"] for state in result["jobs"].values()))

    def test_progress_absence_is_not_fabricated_as_terminal_progress(self):
        result = self.parse([event for event in self.events if event["event"] != "progress"])
        self.assertFalse(result["errors"])
        for state in result["jobs"].values():
            self.assertEqual(state["terminal"], "finished")
            self.assertNotIn("status", state)
            self.assertFalse(state["errors"])
        # This is only event acceptance; native output and health are not read here.


@unittest.skipUnless(os.name == "nt", "Verified Windows Job implementation")
class WindowsContainment(unittest.TestCase):
    """Real OS dummy Python processes only; no CLI, engine, game or replay execution."""
    @classmethod
    def setUpClass(cls):
        parent = core.ROOT / ".cache/arena/orchestration/compiled-runner-next"
        cls.home = Path(tempfile.mkdtemp(prefix="dummy-os-", dir=parent))

    def setUp(self):
        self.directory = self.home / self._testMethodName
        self.directory.mkdir()
        self.assertFalse(runner._PENDING_CLEANUP)

    def command(self, code, timeout=5):
        argv = [sys.executable, "-B", "-c", code]
        with patch.object(core, "Slots", REAL_SLOTS), patch.object(runner, "_command", REAL_COMMAND):
            return runner._slotted_command(lambda _: argv, self.directory, self.directory, timeout)

    def spawning_code(self, parent_sleep=0):
        # Child inherits only this process's assigned Job. Its PID is observation,
        # never authority to terminate it; termination uses the private Job handle.
        return ("import json,os,subprocess,sys,time;from pathlib import Path;"
                "p=subprocess.Popen([sys.executable,'-B','-c','import time;time.sleep(60)']);"
                "Path('dummy-pids.json').write_text(json.dumps({'parent':os.getpid(),'child':p.pid}));"
                f"time.sleep({parent_sleep});sys.exit(7)")

    def assert_empty(self, record):
        self.assertTrue(record["tree_exit_confirmed"])
        containment = record["containment"]
        self.assertTrue(containment["empty_confirmed"])
        self.assertTrue(containment["assigned_before_resume"])
        self.assertTrue(containment["resumed"])
        self.assertTrue(containment["kill_on_close"])
        pids = json.loads((self.directory / "dummy-pids.json").read_bytes())
        self.assertEqual(pids["parent"], record["pid"])
        self.assertTrue(set(pids.values()).issubset(containment["observed_owned_pids"]))
        self.assertFalse(runner._PENDING_CLEANUP)

    def test_dead_parent_live_worker_is_terminated_and_confirmed(self):
        result = self.command(self.spawning_code())
        self.assertEqual(result["returncode"], 7)
        self.assertTrue(result["forced_termination"])
        self.assert_empty(result)

    def test_normal_process_exit_confirms_empty_without_forced_termination(self):
        result = self.command("print('dummy normal exit')")
        self.assertEqual(result["returncode"], 0)
        self.assertTrue(result["tree_exit_confirmed"])
        self.assertFalse(result["forced_termination"])
        self.assertIn(result["pid"], result["containment"]["observed_owned_pids"])

    def test_timeout_confirms_entire_job_before_slot_release(self):
        write = runner._write
        ready = []
        def publish_after_dummy_setup(path, value):
            result = write(path, value)
            if path.name == "process-start.json":
                # This test times out an established parent/worker tree. Wait for
                # its bounded setup handshake before the real 0.5 s process wait;
                # machine contention must not turn it into a startup-race test.
                deadline = time.monotonic() + 10
                while True:
                    try:
                        pids = json.loads((self.directory / "dummy-pids.json").read_bytes())
                        if pids["parent"] == value["pid"] and type(pids["child"]) is int:
                            ready.append(pids)
                            break
                    except (OSError, ValueError, KeyError):
                        pass
                    if time.monotonic() >= deadline:
                        raise AssertionError("dummy process tree did not finish setup within 10 s")
                    time.sleep(0.01)
            return result
        with patch.object(runner, "_write", side_effect=publish_after_dummy_setup):
            result = self.command(self.spawning_code(60), timeout=0.5)
        self.assertEqual(len(ready), 1)
        self.assertEqual(ready[0]["parent"], result["pid"])
        self.assertTrue(result["timed_out"])
        self.assertTrue(result["forced_termination"])
        self.assert_empty(result)

    def test_start_publication_failure_preserves_original_after_tree_cleanup(self):
        original = OSError("dummy publication failure")
        write = runner._write
        def fail(path, value):
            if path.name == "process-start.json":
                raise original
            return write(path, value)
        with patch.object(runner, "_write", side_effect=fail), self.assertRaises(OSError) as raised:
            self.command(self.spawning_code())
        self.assertIs(raised.exception, original)
        result = json.loads((self.directory / "process-end.json").read_bytes())
        self.assert_empty(result)

    def test_assignment_failure_never_resumes_suspended_initial_process(self):
        failure = OSError("synthetic nested-job assignment refusal")
        with patch.object(runner._WinJobProcess, "_assign", side_effect=failure), self.assertRaises(OSError) as raised:
            self.command("from pathlib import Path;Path('must-not-execute').write_text('bad')")
        self.assertIs(raised.exception, failure)
        evidence = json.loads((self.directory / "containment-failure.json").read_bytes())
        self.assertTrue(evidence["empty_confirmed"])
        self.assertFalse(evidence["assigned_before_resume"])
        self.assertFalse(evidence["resumed"])
        self.assertFalse((self.directory / "must-not-execute").exists())
        self.assertFalse(runner._PENDING_CLEANUP)

    def test_resume_failure_terminates_assigned_job_without_running_code(self):
        failure = OSError("synthetic ResumeThread failure")
        with patch.object(runner._WinJobProcess, "_resume", side_effect=failure), self.assertRaises(OSError) as raised:
            self.command("from pathlib import Path;Path('must-not-execute').write_text('bad')")
        self.assertIs(raised.exception, failure)
        evidence = json.loads((self.directory / "containment-failure.json").read_bytes())
        self.assertTrue(evidence["empty_confirmed"])
        self.assertTrue(evidence["assigned_before_resume"])
        self.assertFalse(evidence["resumed"])
        self.assertFalse((self.directory / "must-not-execute").exists())
        self.assertFalse(runner._PENDING_CLEANUP)

    def test_unconfirmed_cleanup_retains_exact_ownership_and_slot_until_recovered(self):
        original = OSError("synthetic process query failure")
        class Process:
            pid, returncode = 9876, None
            assigned, empty_confirmed, forced = True, False, False
            recover = False
            closed = False
            def wait(self, timeout):
                raise original
            def terminate_tree(self):
                if not self.recover:
                    raise OSError("synthetic cleanup query failure")
                self.empty_confirmed = True
            def close(self):
                self.closed = True
            def record(self):
                return {"initial_pid": self.pid, "empty_confirmed": self.empty_confirmed, "forced_termination": self.forced}
        class HeldSlots(Slots):
            exits = 0
            def __exit__(self, *args):
                self.exits += 1
        process = Process()
        slots = HeldSlots()
        with patch.object(core, "Slots", return_value=slots), patch.object(runner, "_command", REAL_COMMAND), patch.object(runner, "_WinJobProcess", return_value=process):
            with self.assertRaises(OSError) as raised:
                runner._slotted_command(lambda _: ["dummy-not-executed"], self.directory, self.directory, 1)
        self.assertIs(raised.exception, original)
        self.assertEqual(slots.exits, 0)
        self.assertFalse(process.closed)
        self.assertEqual(len(runner._PENDING_CLEANUP), 1)
        self.assertIs(runner._PENDING_CLEANUP[0]["process"], process)
        self.assertFalse(original.compiled_process_record["tree_exit_confirmed"])
        self.assertEqual(original.compiled_live_pid, 9876)
        try:
            self.assertFalse(runner.confirm_pending_cleanup())
            self.assertEqual(slots.exits, 0)
        finally:
            process.recover = True
            self.assertTrue(runner.confirm_pending_cleanup())
        self.assertTrue(process.closed)
        self.assertEqual(slots.exits, 1)
        self.assertFalse(runner._PENDING_CLEANUP)


if __name__ == "__main__":
    unittest.main()
