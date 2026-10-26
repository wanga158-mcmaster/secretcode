"""Owner CLI transport tests: synthetic prepared paths, no engine or bot source reads."""
from __future__ import annotations

import hashlib
import json
import shutil
import sys
import threading
import unittest
import uuid
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import core
import games
import compiled_runner
from test_games import stats_doc, write_replay

ORDINARY_RUN_BATCH = games._run_batch


class OwnerTransport(unittest.TestCase):
    def setUp(self):
        self.root = core.ROOT / ".cache" / "owner-cli-gate" / ("transport-test-" + uuid.uuid4().hex)
        self.root.mkdir(parents=True)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.addCleanup(lambda: shutil.rmtree(self.root, ignore_errors=True))
        self.owner = self.root / ".cache" / "fixture"
        self.out = self.owner / "gates" / "case"
        self.snap = self.owner / "snap"
        self.events, self.calls, self.verified = [], [], 0
        self.fp = "f" * 64
        self.a = core.Entrant(core.Bot("main", "candidate"), "a" * 12,
            self.snap / ("a" * 12) / "bots" / "main" / "candidate", True)
        self.b = core.Entrant(core.Bot("contenders", "reference"), "b" * 12,
            self.snap / ("b" * 12) / "bots" / "contenders" / "reference", True)
        for e in (self.a, self.b):
            e.path.mkdir(parents=True)
        self.map = self.root / "maps" / "fixture.map26a"
        self.map.parent.mkdir()
        self.map.write_bytes(b"synthetic map")
        self.specs = [games.GameSpec(self.a, self.b, "fixture", {"subject": "A"}),
                      games.GameSpec(self.b, self.a, "fixture", {"subject": "B"})]
        for module, name, value in ((core, "ROOT", self.root), (core, "CACHE", self.root / ".cache" / "arena"),
                (games, "ROOT", self.root), (games, "CACHE", self.root / ".cache" / "arena"),
                (compiled_runner, "ROOT", self.root), (games, "slot_count", lambda: 4),
                (games, "map_path", lambda value: self.map), (games, "_warm", self.forbidden),
                (games, "_run_batch", self.forbidden), (core, "read_closure", self.forbidden),
                (compiled_runner, "_capture", self.forbidden), (compiled_runner, "_WinJobProcess", self.forbidden),
                (compiled_runner, "_PENDING_CLEANUP", [])):
            self.stack.enter_context(patch.object(module, name, value))
        outer = self
        class Slots:
            def __init__(self, jobs, **kwargs):
                self.count = min(jobs, 2)
            def __enter__(self):
                outer.events.append("slots-enter")
                return self
            def __exit__(self, *args):
                outer.events.append("slots-exit")
        class Lock:
            def __init__(self, path):
                self.path = path
            def __enter__(self):
                outer.events.append(("lock-enter", self.path))
                return self
            def __exit__(self, *args):
                outer.events.append(("lock-exit", self.path))
        self.stack.enter_context(patch.object(core, "Slots", Slots))
        self.stack.enter_context(patch.object(games, "FileLock", Lock))
        self.stack.enter_context(patch.object(compiled_runner, "_command", self.command))

    def forbidden(self, *args, **kwargs):
        raise AssertionError("ordinary/global preparation or native capture is forbidden")

    def verify(self):
        self.verified += 1

    def command(self, argv, cwd, directory, timeout):
        self.assertEqual(argv[:2], [str(games.CAMBC), "run-batch"])
        self.assertEqual(cwd, self.root)
        self.assertIn("--release", argv)
        manifest = Path(argv[2])
        lines = manifest.read_text(encoding="utf-8").splitlines()
        warm = "--rounds" in argv
        self.assertEqual(argv[argv.index("--jobs") + 1], "1" if warm else str(min(2, len(lines))))
        self.calls.append((warm, len(lines), argv))
        log = []
        for line in lines:
            parts = line.split(" ")
            replay = Path(parts[3])
            if warm:
                replay.write_bytes(b"warm")
            else:
                self.assertEqual(len(parts), 7)
                write_replay(replay, a_turns=[1] * 12, b_turns=[1] * 12)
                Path(parts[5]).write_text(json.dumps(stats_doc(clears=(2, 2))), encoding="utf-8")
                log += ["[+] TEAM A WIN (fixture)  a vs b", f"    Replay: {replay}"]
        (directory / "stdout.bin").write_bytes(("\n".join(log) + "\n").encode())
        (directory / "stderr.bin").write_bytes(b"")
        (directory / "dispatch.json").write_text(json.dumps({"argv": argv, "cwd": str(cwd), "timeout": timeout}))
        record = {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True,
                  "containment": {"kind": "windows-private-job", "assigned_before_resume": True,
                                  "resumed": True, "empty_confirmed": True}}
        (directory / "process-end.json").write_text(json.dumps(record))
        self.events.append("job-empty")
        return record

    def run_games(self, specs=None, **kwargs):
        selected = self.specs if specs is None else specs
        return games.play(selected, self.out, jobs=2, quiet=True, resume=True,
                          owner_run=games.OwnerRun("fixture", self.fp, self.verify), **kwargs)

    def test_valid_owner_main_contenders_route_has_bound_receipts_and_no_shared_probe(self):
        original = Path.resolve
        shared = games.CACHE / "snap"
        def resolve(path, *args, **kwargs):
            if path.is_relative_to(shared):
                raise AssertionError("shared input namespace was resolved")
            return original(path, *args, **kwargs)
        with patch.object(Path, "resolve", resolve):
            rows = self.run_games()
        self.assertEqual([warm for warm, _, _ in self.calls], [True, True, False])
        self.assertEqual([r["winner"] for r in rows], ["A", "A"])
        for row in rows:
            self.assertEqual(row["owner_input_fingerprint"], self.fp)
            descriptor = row["owner_transport"]
            receipt = self.root / descriptor["path"]
            self.assertEqual(hashlib.sha256(receipt.read_bytes()).hexdigest(), descriptor["sha256"])
            value = json.loads(receipt.read_text())
            self.assertEqual(value["schema"], "arena-owner-cli-transport-v1")
            self.assertIn(games._resume_key(row["a"], row["b"], row["map"], row["tags"]), value["game_keys"])
            self.assertTrue((self.root / value["manifest"]["path"]).is_file())
        locks = [v[1] for v in self.events if isinstance(v, tuple) and v[0] == "lock-enter"]
        self.assertEqual(set(locks), {shared / (("a" * 12) + ".lock"), shared / (("b" * 12) + ".lock")})
        self.assertLess(self.events.index("job-empty"), self.events.index("slots-exit"))

    def test_partial_resume_runs_only_missing_key_and_retains_original_receipt(self):
        first = self.run_games(self.specs[:1])[0]
        rows = self.run_games()
        self.assertEqual(rows[0], first)
        self.assertEqual([n for warm, n, _ in self.calls if not warm], [1, 1])
        calls = len(self.calls)
        self.assertEqual(self.run_games(), rows)
        self.assertEqual(len(self.calls), calls)

    def test_wrong_fingerprint_or_unbound_normal_row_refused_before_dispatch(self):
        self.run_games()
        calls = len(self.calls)
        self.fp = "e" * 64
        with self.assertRaisesRegex(core.ArenaError, "different inputs"):
            self.run_games()
        self.assertEqual(len(self.calls), calls)
        path = self.out / "games.jsonl"
        rows = core.read_jsonl(path)
        for row in rows:
            row.pop("owner_input_fingerprint")
        path.write_text("".join(json.dumps(r) + "\n" for r in rows))
        with self.assertRaisesRegex(core.ArenaError, "different inputs"):
            self.run_games()

    def test_conflicting_finished_row_and_receipt_tamper_refused(self):
        rows = self.run_games()
        different = {**rows[0], "winner": "B"}
        binding = json.loads((self.root / different["owner_transport"]["path"]).read_bytes())
        binding["rows_sha256"][games._resume_key(different["a"], different["b"], different["map"], different["tags"])] = games._owner_digest(different)
        other = self.out / "alternate-receipt.json"
        other.write_text(json.dumps(binding))
        different["owner_transport"] = games._owner_desc(other)
        core.append_jsonl(self.out / "games.jsonl", [different])
        with self.assertRaisesRegex(core.ArenaError, "conflicting"):
            self.run_games()
        (self.out / "games.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
        receipt = self.root / rows[0]["owner_transport"]["path"]
        receipt.write_text("{}")
        with self.assertRaisesRegex(core.ArenaError, "receipt changed"):
            self.run_games()

    def test_parsed_health_or_outcome_tamper_cannot_reuse_completed_key(self):
        rows = self.run_games()
        rows[0]["A"]["faults"] += 1
        (self.out / "games.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
        with self.assertRaisesRegex(core.ArenaError, "differs from its transport receipt"):
            self.run_games()

    def test_unexpected_row_and_partial_row_refused(self):
        rows = self.run_games()
        rows[0]["map"] = "other"
        path = self.out / "games.jsonl"
        path.write_text("".join(json.dumps(r) + "\n" for r in rows))
        with self.assertRaisesRegex(core.ArenaError, "different inputs"):
            self.run_games()
        path.write_text('{"torn":')
        with self.assertRaisesRegex(core.ArenaError, "partial final row"):
            self.run_games()

    def test_wrong_root_duplicate_request_and_redirect_refuse_before_output(self):
        other = core.Entrant(self.a.bot, self.a.version, self.root / "elsewhere", True)
        with self.assertRaisesRegex(core.ArenaError, "exact snapshot root"):
            self.run_games([games.GameSpec(other, self.b, "fixture")])
        with self.assertRaisesRegex(core.ArenaError, "duplicate game keys"):
            self.run_games([self.specs[0], self.specs[0]])
        original = Path.resolve
        def redirected(path, *args, **kwargs):
            return self.root / "redirect" if path == self.out else original(path, *args, **kwargs)
        with patch.object(Path, "resolve", redirected), self.assertRaisesRegex(core.ArenaError, "output"):
            self.run_games()
        self.assertFalse(self.out.exists())
        self.assertFalse(self.calls)

    def test_postwarm_and_postplay_drift_abort_without_checkpoint(self):
        for failure_at, completed in ((5, 1), (12, 3)):
            with self.subTest(failure_at=failure_at):
                self.verified = 0
                self.out = self.owner / "gates" / f"drift-{failure_at}"
                start = len(self.calls)
                def verify():
                    self.verified += 1
                    if self.verified == failure_at:
                        raise core.ArenaError("bound payload drift")
                with self.assertRaisesRegex(core.ArenaError, "payload drift"):
                    games.play(self.specs, self.out, jobs=2, quiet=True, resume=True,
                               owner_run=games.OwnerRun("fixture", self.fp, verify))
                self.assertEqual(len(self.calls) - start, completed)
                self.assertFalse((self.out / "games.jsonl").exists())

    def test_unconfirmed_cancellation_retains_slots_and_version_locks_until_cleanup(self):
        outer = self
        class Process:
            empty_confirmed, assigned = False, True
            def terminate_tree(self):
                outer.events.append("job-empty")
                self.empty_confirmed = True
            def close(self):
                outer.events.append("job-close")
            def record(self):
                return {"empty_confirmed": self.empty_confirmed}
        process = Process()
        error = RuntimeError("cancelled with unconfirmed tree")
        error.compiled_process = process
        directory = self.out / "cancel.transport"
        manifest, log = self.out / "cancel.txt", self.out / "cancel.log"
        self.out.mkdir(parents=True)
        def make_manifest(count):
            manifest.write_text("synthetic\n")
            return []
        with patch.object(compiled_runner, "_command", side_effect=error), self.assertRaises(RuntimeError):
            games._owner_batch([self.a], directory, manifest, log, 0, 1,
                games.OwnerRun("fixture", self.fp, self.verify), make_manifest)
        self.assertNotIn("slots-exit", self.events)
        self.assertFalse(any(isinstance(e, tuple) and e[0] == "lock-exit" for e in self.events))
        self.assertEqual(len(compiled_runner._PENDING_CLEANUP), 1)
        self.assertTrue(compiled_runner.confirm_pending_cleanup())
        self.assertLess(self.events.index("job-empty"), self.events.index("slots-exit"))
        self.assertTrue(any(isinstance(e, tuple) and e[0] == "lock-exit" for e in self.events))


    def test_reserved_shared_owner_refused_before_snapshot_probe(self):
        with patch.object(core, "resolve_snapshot_root", side_effect=AssertionError("shared namespace must not be probed")):
            with self.assertRaisesRegex(core.ArenaError, "independent owner"):
                games.play(self.specs, self.out, owner_run=games.OwnerRun("arena", self.fp, lambda: None))
        self.assertFalse(self.out.exists())

    def test_mixed_normal_owner_order_waits_for_slots_without_version_lock(self):
        pool = threading.Lock()
        versions, held = {}, {}
        normal_ready, owner_waiting = threading.Event(), threading.Event()
        failures = []
        outer = self
        class OrderedSlots:
            def __init__(self, count, **kwargs):
                self.count = count
            def __enter__(self):
                if threading.current_thread().name != "ordinary-cli-fixture":
                    owner_waiting.set()
                outer.assertFalse(held.get(threading.get_ident()), "VERSION held while awaiting shared Slots")
                outer.assertTrue(pool.acquire(timeout=2), "bounded fake Slot admission deadlocked")
                return self
            def __exit__(self, *args):
                pool.release()
        class OrderedVersion:
            def __init__(self, path):
                self.path = path
                self.lock = versions.setdefault(path, threading.Lock())
            def __enter__(self):
                outer.assertTrue(pool.locked(), "VERSION acquired before Slot admission")
                outer.assertTrue(self.lock.acquire(timeout=2), "bounded fake VERSION admission deadlocked")
                held.setdefault(threading.get_ident(), set()).add(self.path)
                return self
            def __exit__(self, *args):
                held[threading.get_ident()].remove(self.path)
                self.lock.release()
        class FinishedProcess:
            def __init__(self, *args, **kwargs):
                pass
            def poll(self):
                return 0
            def wait(self):
                return 0
        directory = self.out / "games/order.transport"
        manifest, log = directory.parent / "order.txt", directory.parent / "order.log"
        normal_manifest, normal_log = self.owner / "normal.txt", self.owner / "normal.log"
        self.owner.mkdir(parents=True, exist_ok=True)
        normal_manifest.write_text("synthetic ordinary manifest\n")
        normal_paths = [core.CACHE / "snap" / e.version / "bots" / e.bot.suite / e.bot.name for e in (self.a, self.b)]
        def ordinary():
            try:
                with games.Slots(1):
                    normal_ready.set()
                    outer.assertTrue(owner_waiting.wait(timeout=2))
                    outer.assertEqual(ORDINARY_RUN_BATCH(normal_manifest, 1, 0, normal_log, normal_paths), 0)
            except BaseException as error:
                failures.append(error)
        def owner_manifest(count):
            manifest.write_text(f"{self.a.path} {self.b.path} {self.map} {directory.parent / 'order.repl26a'} --statistics {directory.parent / 'order.json'} --json\n")
            return [games._resume_key(self.a.key, self.b.key, "fixture", {})]
        with patch.object(core, "Slots", OrderedSlots), patch.object(games, "Slots", OrderedSlots), \
                patch.object(games, "FileLock", OrderedVersion), patch.object(games.subprocess, "Popen", FinishedProcess):
            worker = threading.Thread(target=ordinary, name="ordinary-cli-fixture")
            worker.start()
            self.assertTrue(normal_ready.wait(timeout=2))
            try:
                games._owner_batch([self.a, self.b], directory, manifest, log, 0, 1,
                                   games.OwnerRun("fixture", self.fp, self.verify), owner_manifest)
            finally:
                owner_waiting.set()
                worker.join(timeout=3)
            self.assertFalse(worker.is_alive())
        self.assertEqual(failures, [])
        self.assertFalse(any(held.values()))

    def test_owner_truncated_fallback_does_not_query_shared_map_index(self):
        self.out.mkdir(parents=True)
        replay = self.out / "partial.repl26a"
        write_replay(replay)
        with patch.object(games, "_guess_roster", side_effect=AssertionError("owner fallback must not query map metadata")):
            row = games.game_row(self.specs[0], "fixture", replay, self.out / "absent.json", "TRUNCATED", 0,
                                 fallback_roster={s: (games.MAX_PICKS, games.MAX_PICKS + 1) for s in "AB"})
        self.assertTrue(row["truncated"])
        self.assertEqual(row["rounds"], 0)


if __name__ == "__main__":
    unittest.main()
