"""Owner snapshot input/locking tests. Compiler and native workers never run."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import threading
from types import SimpleNamespace
import unittest
import uuid
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import exp
import population
import compiled_runner as native
import report
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import checkup


def wasm_fixture(payload: bytes) -> bytes:
    """A WASM1 header with an inert custom section, never instantiated."""
    assert len(payload) < 126
    return b"\0asm\x01\0\0\0\0" + bytes([len(payload) + 1]) + b"\0" + payload


class PrivateSnapshots(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/private-snapshot-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / uuid.uuid4().hex
        self.root.mkdir()
        def cleanup():
            self.assertEqual(self.root.resolve(), self.root)
            self.assertTrue(self.root.is_relative_to(base))
            shutil.rmtree(self.root)
        self.addCleanup(cleanup)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.arena = self.root / ".cache/arena"
        self.private = self.root / ".cache/owner/snap"
        for module, name, value in (
            (core, "ROOT", self.root), (core, "BOTS", self.root / "bots"), (core, "MAPS", self.root / "maps"), (core, "CACHE", self.arena),
            (exp, "ROOT", self.root), (population, "ROOT", self.root), (population, "CACHE", self.arena),
            (native, "ROOT", self.root), (report, "ROOT", self.root), (report, "CACHE", self.arena),
        ):
            self.stack.enter_context(patch.object(module, name, value))
        self.stack.enter_context(patch.object(core, "toolchain", return_value="123456789abc"))
        self.stack.enter_context(patch.object(core, "slot_count", return_value=1))
        self.stack.enter_context(patch.object(core, "_link_sdk"))
        self.bot = self.make_bot("odd", "one", b"int main(){return MAGIC;}\n")
        self.ref = self.make_bot("anchors", "ref", b"int main(){return 2;}\n")
        self.raw = wasm_fixture(b"RAW control")
        self.injected = wasm_fixture(b"INJECTED control")
        self.build = self.stack.enter_context(patch.object(core, "build", side_effect=self.fake_build))
        self.map = self.root / "maps/map-one.map26a"
        self.map.parent.mkdir()
        self.map.write_bytes(b"fixed map")
        bundle = self.root / "docs/reference/current-version.json"
        bundle.parent.mkdir(parents=True)
        bundle.write_bytes(b"fixed bundle")
        self.engine = self.root / "engine.wasm"; self.engine.write_bytes(b"fixed engine")
        self.runner = self.root / "fastrun.exe"; self.runner.write_bytes(b"fixed runner")
        source = self.root / "tools/fastrun/fastrun.cpp"
        source.parent.mkdir(parents=True); source.write_bytes(b"fixed runner source")
        for name, value in (("engine_wasm", self.engine), ("fastrun", self.runner)):
            self.stack.enter_context(patch.object(exp, name, return_value=value))
        self.stack.enter_context(patch.object(exp, "runner_libraries", return_value={}))

    def make_bot(self, suite, name, body):
        bot = core.Bot(suite, name)
        (bot.path / "src").mkdir(parents=True)
        (bot.path / "src/main.cpp").write_bytes(b'#include "knob.h"\n' + body)
        (bot.path / "src/knob.h").write_bytes(b"#define MAGIC 0\n")
        return bot

    def fake_build(self, target, timeout=1800):
        # This also checks that preparing in an owner namespace did not replace
        # the global cache used by Slots.
        self.assertEqual(core.CACHE, self.arena)
        self.assertTrue((self.arena / "slots/0.lock").is_file())
        build = target / ".cambc/build-release"
        build.mkdir(parents=True)
        (build / "bot.wasm").write_bytes(self.raw)
        (build / "bot-injected.wasm").write_bytes(self.injected)
        return True, "built fixture"

    def global_entry(self, bot=None):
        return core.prepare(bot or self.bot)

    def private_entry(self, bot=None):
        return core.prepare(bot or self.bot, snapshot_root=self.private, snapshot_owner="owner")

    def snapshot(self, ent):
        return ent.path.parents[2]

    def files(self, directory):
        return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in directory.rglob("*") if p.is_file()}

    def spec(self, ent, ref):
        subjects = [{"label": "candidate", "bot": ent.bot.id, "set": {}, "weights": None}]
        roots = {"candidate": self.private.relative_to(self.root).as_posix(),
                 ref.bot.id: (self.arena / "snap").relative_to(self.root).as_posix()}
        spec = {"name": "owner/example", "private_snapshots": True, "reuse_shared_snapshots": True, "subjects": subjects,
                "entrants": {"candidate": ent.key, ref.bot.id: ref.key},
                "opponent_ids": [ref.bot.id], "opponents": [ref.bot.id], "maps": "map-one",
                "sides": "both", "rounds": 0, "games": 2}
        spec["provenance"] = exp.provenance(subjects, [self.map], {"candidate": ent, ref.bot.id: ref})
        spec["provenance"]["snapshot_roots"] = roots
        return spec

    def test_only_global_and_exact_declared_owner_roots_are_accepted(self):
        self.assertEqual(core.resolve_snapshot_root(), self.arena / "snap")
        self.assertEqual(core.resolve_snapshot_root(self.arena / "snap"), self.arena / "snap")
        self.assertEqual(core.resolve_snapshot_root(self.private, "owner"), self.private)
        self.assertEqual(core.resolve_snapshot_root(".cache/owner/snap", "owner"), self.private)
        self.assertEqual(core.resolve_snapshot_root(".cache/.owner/snap", ".owner"), self.root / ".cache/.owner/snap")
        for root, owner in ((self.private, None), (self.private, "other"),
                            (self.root / ".cache/other/snap", "owner"),
                            (self.root / "unrelated", "owner")):
            with self.subTest(root=root, owner=owner), self.assertRaises(core.ArenaError):
                core.resolve_snapshot_root(root, owner)

    def test_owner_aliases_and_traversal_are_refused_before_creation(self):
        for owner in (".", "..", "owner.", "CON", "con.txt", "NUL", "Lpt1", "a/b"):
            with self.subTest(owner=owner), self.assertRaises(core.ArenaError):
                core.resolve_snapshot_root(self.root / ".cache" / owner / "snap", owner)
        with self.assertRaises(core.ArenaError):
            core.resolve_snapshot_root(self.root / ".cache/owner/../other/snap", "owner")
        self.assertFalse(self.private.exists())

    def test_redirected_owner_namespace_is_refused(self):
        target = self.root / "redirect-target"; target.mkdir()
        self.private.parent.mkdir(parents=True)
        try:
            if os.name == "nt":
                import _winapi
                _winapi.CreateJunction(str(target), str(self.private))
            else:
                self.private.symlink_to(target, target_is_directory=True)
        except OSError as error:
            self.skipTest("Host cannot create a test redirect: " + str(error))
        with self.assertRaises(core.ArenaError):
            core.resolve_snapshot_root(self.private, "owner")

    def test_direct_population_lookup_refuses_a_redirected_version_before_any_read(self):
        ent = self.global_entry()
        redirected = self.private / ent.version
        actual = self.snapshot(ent)
        real_resolve = Path.resolve
        def resolve(path, *args, **kwargs):
            return actual if path == redirected else real_resolve(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), \
                patch.object(population, "_load", side_effect=AssertionError("redirected metadata must not be read")), \
                patch.object(native, "_preflight_snapshot", side_effect=AssertionError("direct population API must guard itself")), \
                self.assertRaisesRegex(core.ArenaError, "directory is redirected"):
            population._snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")

    def test_direct_population_lookup_refuses_a_reparse_source_leaf(self):
        ent = self.private_entry()
        leaf = ent.path / "src/main.cpp"
        real_reparse = core._is_reparse
        with patch.object(core, "_is_reparse", side_effect=lambda path: Path(path) == leaf or real_reparse(path)), \
                patch.object(native, "_preflight_snapshot", side_effect=AssertionError("direct population API must guard itself")), \
                self.assertRaisesRegex(core.ArenaError, "source file is redirected"):
            population._snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")

    def test_direct_population_lookup_refuses_a_redirected_internal_include(self):
        ent = self.private_entry()
        common = self.snapshot(ent) / "bots/odd/common"
        common.mkdir()
        actual, alias = common / "real.h", common / "alias.h"
        actual.write_text("// shared fixture\n", encoding="utf-8")
        main = ent.path / "src/main.cpp"
        main.write_bytes(b'#include "../../common/alias.h"\n' + main.read_bytes())
        real_resolve = Path.resolve
        def resolve(path, *args, **kwargs):
            return actual if path == alias else real_resolve(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), \
                patch.object(native, "_preflight_snapshot", side_effect=AssertionError("direct population API must guard itself")), \
                self.assertRaisesRegex(core.ArenaError, "source include is redirected"):
            population._snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")

    def test_private_prepare_keeps_global_version_lock_slots_and_input_marker(self):
        locks = []
        real_lock = core.FileLock
        class CaptureLock:
            def __init__(inner, path, *args, **kwargs):
                locks.append(Path(path)); inner.lock = real_lock(path, *args, **kwargs)
            def __enter__(inner): return inner.lock.__enter__()
            def __exit__(inner, *args): return inner.lock.__exit__(*args)
        with patch.object(core, "FileLock", CaptureLock):
            ent = self.private_entry()
        self.assertTrue(ent.ok)
        self.assertEqual(ent.path, self.private / ent.version / "bots/odd/one")
        self.assertEqual(locks, [self.arena / "snap" / (ent.version + ".lock")])
        self.assertTrue((self.private / ".keep").is_file())
        self.assertFalse((self.arena / "snap" / ent.version).exists())
        self.assertFalse((self.private.parent / "slots").exists())

    def test_owner_prepare_waits_on_the_same_global_version_lock(self):
        version = core.version(self.bot)
        attempted, built = threading.Event(), threading.Event()
        real_lock = core.FileLock
        class CaptureLock:
            def __init__(inner, path, *args, **kwargs):
                inner.lock = real_lock(path, *args, **kwargs)
            def __enter__(inner):
                attempted.set()
                return inner.lock.__enter__()
            def __exit__(inner, *args): return inner.lock.__exit__(*args)
        def build(*args, **kwargs):
            built.set(); return self.fake_build(*args, **kwargs)
        with patch.object(core, "FileLock", CaptureLock), patch.object(core, "build", side_effect=build), \
                ThreadPoolExecutor(1) as executor:
            with real_lock(self.arena / "snap" / (version + ".lock")):
                pending = executor.submit(self.private_entry)
                self.assertTrue(attempted.wait(5))
                self.assertFalse(built.is_set(), "private root must not bypass an already-held global version lock")
            self.assertTrue(pending.result(timeout=5).ok)
        self.assertTrue(built.is_set())

    def test_prepare_rechecks_namespace_after_admission_to_the_global_version_lock(self):
        real_lock, real_resolver = core.FileLock, core.resolve_snapshot_root
        locked, calls = [False], []
        class CaptureLock:
            def __init__(inner, path, *args, **kwargs):
                inner.lock = real_lock(path, *args, **kwargs)
            def __enter__(inner):
                result = inner.lock.__enter__(); locked[0] = True
                return result
            def __exit__(inner, *args):
                locked[0] = False
                return inner.lock.__exit__(*args)
        def resolve(value=None, owner=None):
            calls.append(locked[0])
            if locked[0]:
                return self.root / ".cache/other/snap"
            return real_resolver(value, owner)
        with patch.object(core, "FileLock", CaptureLock), patch.object(core, "resolve_snapshot_root", side_effect=resolve), \
                self.assertRaisesRegex(core.ArenaError, "changed while waiting"):
            self.private_entry()
        self.assertEqual(calls, [False, True])
        self.build.assert_not_called()
        self.assertFalse(self.private.exists())

    def test_explicit_owner_without_a_root_is_not_a_global_fallback(self):
        with self.assertRaisesRegex(core.ArenaError, "explicit root"):
            core.resolve_snapshot_root(None, "owner")
        with self.assertRaisesRegex(core.ArenaError, "explicit root"):
            core.prepare(self.bot, snapshot_owner="owner")
        self.build.assert_not_called()
        ent = self.global_entry()
        with self.assertRaisesRegex(core.ArenaError, "explicit root"):
            population._snapshot(ent.key, snapshot_owner="owner")
        with self.assertRaisesRegex(core.ArenaError, "explicit root"):
            population._snapshot_files(ent, snapshot_owner="owner")
        with patch.object(native, "_preflight_snapshot", side_effect=AssertionError("owner refusal must precede preflight")), \
                patch.object(native, "_capture", side_effect=AssertionError("owner refusal must precede capture")), \
                self.assertRaisesRegex(core.ArenaError, "explicit roots"):
            native.play(ent.key, self.ref.id + "@bbbbbbbbbbbb", self.map, self.root / ".cache/output",
                        snapshot_owner="owner")

    def test_normal_prepare_default_path_and_no_private_marker_are_unchanged(self):
        ent = self.global_entry()
        self.assertEqual(ent.path, self.arena / "snap" / ent.version / "bots/odd/one")
        self.assertFalse(self.private.exists())
        self.assertEqual(core.CACHE, self.arena)

    def test_complete_valid_global_input_is_reused_without_an_owner_build(self):
        original = self.global_entry()
        count = self.build.call_count
        result = exp._private_prepare([(self.bot, {}, "candidate")], "owner", reuse_shared=True)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].path, original.path)
        self.assertEqual(self.build.call_count, count)

    def test_ordinary_private_preparation_is_scope_neutral_but_native_execution_is_not(self):
        for suite in ("main", "contenders", "surprise"):
            bot = self.make_bot(suite, "private", b"int main(){return 0;}\n")
            with self.subTest(suite=suite):
                entry = exp._private_prepare([(bot, {}, "candidate")], "owner")[0]
                self.assertTrue(entry.path.is_relative_to(self.private))
                self.assertEqual(core.cached_snapshot(entry.key, snapshot_root=self.private,
                                                       snapshot_owner="owner").path, entry.path)
                with self.assertRaises(core.ArenaError):
                    native._capture(entry.key, "raw", snapshot_root=self.private, snapshot_owner="owner")
                with patch.object(native, "_run", side_effect=AssertionError("restricted native execution must never run")), \
                        self.assertRaises(core.ArenaError):
                    native.play(entry.key, self.ref.id + "@bbbbbbbbbbbb", self.map, self.root / ".cache/native-output",
                                snapshot_roots={entry.key: self.private, self.ref.id + "@bbbbbbbbbbbb": self.private},
                                snapshot_owner="owner")

    def test_strict_owner_preparation_never_reads_stats_or_touches_shared_inputs(self):
        original = self.global_entry()
        shared = self.snapshot(original)
        before, stamp = self.files(shared), (shared / ".ready").stat().st_mtime_ns
        guards = ExitStack()
        real = {name: getattr(Path, name) for name in ("read_bytes", "read_text", "open", "stat", "touch")}
        def guarded(name):
            def check(path, *args, **kwargs):
                self.assertFalse(path.is_relative_to(shared), "strict preparation accessed a shared input: " + str(path))
                return real[name](path, *args, **kwargs)
            return check
        with guards, \
                patch.object(native, "_key", side_effect=AssertionError("ordinary preparation must not use native mandate checks")), \
                patch.object(native, "_capture", side_effect=AssertionError("ordinary preparation must not use native capture")), \
                patch.object(native, "_preflight_snapshot", side_effect=AssertionError("ordinary preparation must not use native preflight")), \
                patch.object(population, "_snapshot", side_effect=AssertionError("ordinary preparation must not use population scope checks")):
            for name in real:
                guards.enter_context(patch.object(Path, name, autospec=True, side_effect=guarded(name)))
            result = exp._private_prepare([(self.bot, {}, "candidate")], "owner")
        self.assertEqual(result[0].path, self.private / original.version / "bots/odd/one")
        self.assertEqual(self.files(shared), before)
        self.assertEqual((shared / ".ready").stat().st_mtime_ns, stamp)
        self.assertTrue((self.arena / "snap" / (original.version + ".lock")).is_file(),
                        "using the real shared version lock remains allowed")

    def test_scope_neutral_main_raw_replay_does_not_invoke_native_execution_validation(self):
        bot = self.make_bot("main", "private-replay", b"int main(){return 0;}\n")
        ent = self.private_entry(bot)
        spec = self.spec(ent, self.global_entry(self.ref))
        with patch.object(native, "_key", side_effect=AssertionError("RAW replay must not use native mandate checks")), \
                patch.object(native, "_capture", side_effect=AssertionError("RAW replay must not use native capture")), \
                patch.object(native, "_preflight_snapshot", side_effect=AssertionError("RAW replay must not use native preflight")), \
                patch.object(core, "prepare", side_effect=AssertionError("cached RAW replay must not build")):
            actual = exp.replay_entrant(spec, "candidate", spec["subjects"][0])
            self.assertEqual(actual.path, ent.path)
            (ent.path / ".cambc/build-release/bot-injected.wasm").unlink()
            with self.assertRaises(core.ArenaError):
                exp.replay_entrant(spec, "candidate", spec["subjects"][0])

    def test_native_main_replay_refuses_before_ordinary_cached_input_reads(self):
        bot = self.make_bot("main", "native-refusal", b"int main(){return 0;}\n")
        spec = self.spec(self.private_entry(bot), self.global_entry(self.ref))
        with patch.object(core, "cached_snapshot", side_effect=AssertionError("native mandate refusal must precede ordinary cache reads")) as cached, \
                patch.object(exp, "replay_entrant", side_effect=AssertionError("native mandate refusal must precede entrant reads")) as entrant, \
                patch.object(native, "_capture", side_effect=AssertionError("native mandate refusal must precede capture")) as capture, \
                patch.object(native, "_dependencies", side_effect=AssertionError("native mandate refusal must precede runtime reads")) as dependencies:
            for role in ("raw", "injected"):
                with self.subTest(role=role), self.assertRaises(core.ArenaError):
                    exp.native_replay_recipe(spec, "candidate", self.ref.id, self.map, "A", role)
        cached.assert_not_called()
        entrant.assert_not_called()
        capture.assert_not_called()
        dependencies.assert_not_called()

    def test_scope_neutral_validator_rejects_payload_source_metadata_and_toolchain_drift(self):
        ent = self.private_entry()
        snap = self.snapshot(ent)
        meta = json.loads((snap / "meta.json").read_text())
        cases = [
            (ent.path / ".cambc/build-release/bot-injected.wasm", None),
            (ent.path / ".cambc/build-release/bot.wasm", b"not a WASM module"),
            (snap / ".ready", None),
            (snap / ".toolchain", b"stale\n"),
            (ent.path / "src/knob.h", b"#define MAGIC 9\n"),
            (snap / "meta.json", json.dumps({"bot": "odd/other", "version": ent.version,
                                           "overrides": {}, "files": 2}).encode()),
            (snap / "meta.json", json.dumps({**meta, "files": meta["files"] + 1}).encode()),
            (snap / "meta.json", json.dumps({**meta, "overrides": {"MAGIC": "9"}}).encode()),
        ]
        for path, bad in cases:
            with self.subTest(path=path):
                original = path.read_bytes()
                if bad is None: path.unlink()
                else: path.write_bytes(bad)
                try:
                    with self.assertRaises(core.ArenaError):
                        core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")
                finally:
                    path.write_bytes(original)
        for path in (snap / ".failed", ent.path / "src/unrecorded-data.bin"):
            path.write_bytes(b"unexpected")
            try:
                with self.subTest(extra=path), self.assertRaises(core.ArenaError):
                    core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")
            finally:
                path.unlink()

    def test_scope_neutral_validator_refuses_redirected_version_and_source(self):
        ent = self.private_entry()
        snap, leaf = self.snapshot(ent), ent.path / "src/main.cpp"
        real_resolve, real_reparse = Path.resolve, core._is_reparse
        def resolve(path, *args, **kwargs):
            return self.root / "elsewhere" if path == snap else real_resolve(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), self.assertRaises(core.ArenaError):
            core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")
        with patch.object(core, "_is_reparse", side_effect=lambda path: Path(path) == leaf or real_reparse(path)), \
                self.assertRaises(core.ArenaError):
            core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")

    def test_shared_reuse_permission_is_explicit_and_bound_in_progress(self):
        spec = self.spec(self.private_entry(), self.global_entry(self.ref))
        fingerprint = exp.progress_fingerprint(spec)
        spec["reuse_shared_snapshots"] = False
        self.assertNotEqual(exp.progress_fingerprint(spec), fingerprint)
        with self.assertRaises(core.ArenaError):
            exp.recorded_snapshot_root(spec, self.ref.id)
        del spec["reuse_shared_snapshots"]
        with self.assertRaises(core.ArenaError):
            exp.recorded_snapshot_root(spec, self.ref.id)

    def test_strict_cached_shared_root_refuses_before_resolving_shared_metadata(self):
        spec = self.spec(self.private_entry(), self.global_entry(self.ref))
        spec["reuse_shared_snapshots"] = False
        with patch.object(core, "resolve_snapshot_root", side_effect=AssertionError("strict replay must not resolve shared inputs")) as resolver, \
                self.assertRaisesRegex(core.ArenaError, "opt-in"):
            exp.recorded_snapshot_root(spec, self.ref.id)
        resolver.assert_not_called()

    def test_shared_reuse_opt_in_without_private_mode_refuses_before_build(self):
        args = SimpleNamespace(name="owner/invalid-reuse", question="invalid flag combination",
                               subject=["candidate=" + self.bot.id], subjects=None, vs=[self.ref.id],
                               maps="map-one", sides="both", rounds=0, keep="none", force=False,
                               private_snapshots=False, reuse_shared_snapshots=True)
        with patch.object(exp, "build", side_effect=AssertionError("invalid flags must not build")) as build, \
                self.assertRaises(core.ArenaError):
            exp.cmd_run(args)
        build.assert_not_called()

    def test_cli_exposes_strict_private_and_explicit_reuse_as_separate_flags(self):
        import argparse
        parser = argparse.ArgumentParser()
        exp.register(parser.add_subparsers())
        command = ["exp", "run", "owner/cli", "-q", "permission", "-s",
                   "candidate=" + self.bot.id, "--vs", self.ref.id, "--private-snapshots"]
        strict = parser.parse_args(command)
        self.assertIs(strict.private_snapshots, True)
        self.assertIs(strict.reuse_shared_snapshots, False)
        reused = parser.parse_args(command + ["--reuse-shared-snapshots"])
        self.assertIs(reused.reuse_shared_snapshots, True)

    def test_private_run_pins_strict_default_and_explicit_shared_reuse_choice(self):
        ent, ref = self.private_entry(), self.private_entry(self.ref)
        for reuse in (False, True):
            args = SimpleNamespace(name="owner/pin-" + str(reuse), question="pin permission",
                                   subject=["candidate=" + self.bot.id], subjects=None, vs=[self.ref.id],
                                   maps="map-one", sides="both", rounds=0, keep="none", force=False,
                                   private_snapshots=True, reuse_shared_snapshots=reuse)
            with patch.object(exp, "build", return_value=([ent], [ref])), \
                    patch.object(core, "map_set", return_value=["map-one"]), \
                    patch.object(exp, "run_games", side_effect=RuntimeError("stop at mocked runner")), \
                    self.assertRaises(RuntimeError):
                exp.cmd_run(args)
            _, stored = exp.load(args.name)
            self.assertIs(stored["reuse_shared_snapshots"], reuse)

    def test_missing_global_sdk_is_not_substituted_or_rebuilt_in_global_cache(self):
        original = self.global_entry()
        injected = original.path / ".cambc/build-release/bot-injected.wasm"
        injected.unlink()
        snapshot = self.snapshot(original)
        before = self.files(snapshot)
        stamp = (snapshot / ".ready").stat().st_mtime_ns
        result = exp._private_prepare([(self.bot, {}, "candidate")], "owner", reuse_shared=True)
        self.assertEqual(result[0].path, self.private / original.version / "bots/odd/one")
        self.assertEqual(self.files(snapshot), before)
        self.assertEqual((snapshot / ".ready").stat().st_mtime_ns, stamp)
        self.assertFalse(injected.exists())
        self.assertTrue((result[0].path / ".cambc/build-release/bot-injected.wasm").is_file())

    def test_stale_global_toolchain_or_source_uses_owner_input_without_global_mutation(self):
        for damage in ("toolchain", "source"):
            with self.subTest(damage=damage):
                original = self.global_entry()
                snapshot = self.snapshot(original)
                target = snapshot / ".toolchain" if damage == "toolchain" else original.path / "src/knob.h"
                target.write_text("stale" if damage == "toolchain" else "#define MAGIC 99\n", encoding="utf-8")
                before = self.files(snapshot)
                result = exp._private_prepare([(self.bot, {}, "candidate")], "owner", reuse_shared=True)
                self.assertTrue(result[0].path.is_relative_to(self.private))
                self.assertEqual(self.files(snapshot), before)
                # Restore the intentionally damaged global fixture between subcases.
                target.write_text("123456789abc\n" if damage == "toolchain" else "#define MAGIC 0\n", encoding="utf-8")

    def test_recorded_roots_are_complete_and_owner_bound(self):
        spec = self.spec(self.private_entry(), self.global_entry(self.ref))
        self.assertEqual(exp.recorded_snapshot_root(spec, "candidate"), self.private)
        self.assertEqual(exp.recorded_snapshot_root(spec, self.ref.id), self.arena / "snap")
        for damage in ("missing", "extra", "foreign", "type"):
            value = copy.deepcopy(spec)
            roots = value["provenance"]["snapshot_roots"]
            if damage == "missing": del roots["candidate"]
            if damage == "extra": roots["extra"] = ".cache/owner/snap"
            if damage == "foreign": roots["candidate"] = ".cache/other/snap"
            if damage == "type": value["provenance"]["snapshot_roots"] = []
            with self.subTest(damage=damage), self.assertRaises(core.ArenaError):
                exp.recorded_snapshot_root(value, "candidate")

    def test_changing_only_a_recorded_root_changes_progress_and_raw_recipe(self):
        ent = self.private_entry(); self.global_entry()
        spec = self.spec(ent, self.global_entry(self.ref))
        before_progress = exp.progress_fingerprint(spec)
        before_recipe = exp.replay_recipe(spec, "candidate", self.ref.id, self.map, "A")[0]
        spec["provenance"]["snapshot_roots"]["candidate"] = ".cache/arena/snap"
        self.assertNotEqual(exp.progress_fingerprint(spec), before_progress)
        self.assertNotEqual(exp.replay_recipe(spec, "candidate", self.ref.id, self.map, "A")[0], before_recipe)

    def test_private_cached_replay_never_uses_an_available_global_fallback(self):
        ent = self.private_entry(); self.global_entry()
        spec = self.spec(ent, self.global_entry(self.ref))
        with patch.object(core, "prepare", side_effect=AssertionError("unexpected rebuild")):
            actual = exp.replay_entrant(spec, "candidate", spec["subjects"][0])
            self.assertEqual(actual.path, ent.path)
            (ent.path / ".cambc/build-release/bot.wasm").unlink()
            with self.assertRaises(core.ArenaError):
                exp.replay_entrant(spec, "candidate", spec["subjects"][0])

    def test_selected_replay_projects_roots_without_requiring_unselected_inputs(self):
        spec = self.spec(self.private_entry(), self.global_entry(self.ref))
        bot = self.make_bot("odd", "other", b"int main(){return 3;}\n")
        other = self.private_entry(bot)
        spec["subjects"].append({"label": "other", "bot": bot.id, "set": {}, "weights": None})
        spec["entrants"]["other"] = other.key
        spec["provenance"]["snapshot_roots"]["other"] = ".cache/owner/snap"
        spec["provenance"]["wasm"]["other"] = exp.sha256(Path(exp.wasm(other)))
        self.assertEqual(exp.provenance_gaps(spec), [])
        Path(exp.wasm(other)).unlink()
        self.assertTrue(exp.provenance_gaps(spec))
        self.assertEqual(exp.replay_provenance_gaps(spec, "candidate", self.ref.id, ["map-one"]), [])

    def test_private_capture_binds_actual_source_and_both_roles(self):
        ent = self.private_entry()
        binding = native._capture(ent.key, "injected", snapshot_root=self.private, snapshot_owner="owner")
        self.assertEqual(binding.payload, self.injected)
        self.assertEqual(binding.raw["sha256"], hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(binding.injected["sha256"], hashlib.sha256(self.injected).hexdigest())
        self.assertTrue(all(self.private.relative_to(self.root).as_posix() in pin["path"] for pin in binding.source_files))
        (ent.path / ".cambc/build-release/bot-injected.wasm").unlink()
        with self.assertRaises(core.ArenaError):
            native._capture(ent.key, "raw", snapshot_root=self.private, snapshot_owner="owner")

    def test_native_outputs_cannot_create_inside_any_reserved_snapshot_namespace(self):
        for owner in ("owner", "other"):
            target = self.root / ".cache" / owner / "snap/0123456789ab/output"
            with self.subTest(owner=owner), self.assertRaises(core.ArenaError):
                native._fresh_out(target)
            self.assertFalse(target.exists())

    def test_native_output_cannot_be_an_ancestor_of_its_captured_inputs(self):
        ent, ref = self.private_entry(), self.global_entry(self.ref)
        bindings = {ent.key: native._capture(ent.key, "raw", snapshot_root=self.private, snapshot_owner="owner"),
                    ref.key: native._capture(ref.key, "raw")}
        plan = [{"id": "game-000000", "a": ent.key, "b": ref.key, "map": "map-one"}]
        with patch.object(native, "_dependencies", return_value=(None, {}, [])), \
                patch.object(native, "_fresh_out", side_effect=AssertionError("output creation must not be reached")), \
                self.assertRaisesRegex(core.ArenaError, "source inputs"):
            native._run(plan, bindings, self.private.parent, "raw", 1, 120, {})

    def test_same_native_key_cannot_silently_merge_two_recorded_roots(self):
        ent, global_ent = self.private_entry(), self.global_entry()
        spec = self.spec(ent, global_ent)
        with patch.object(native, "_dependencies", side_effect=AssertionError("conflict must precede runtime capture")), \
                self.assertRaisesRegex(core.ArenaError, "conflicting roots"):
            exp.native_replay_recipe(spec, "candidate", self.bot.id, self.map, "A", "injected")

    def test_none_is_not_an_explicit_native_root_and_fails_before_capture(self):
        ent, ref = self.private_entry(), self.global_entry(self.ref)
        with patch.object(native, "_preflight_snapshot", side_effect=AssertionError("root validation must precede preflight")), \
                patch.object(native, "_capture", side_effect=AssertionError("root validation must precede capture")), \
                self.assertRaisesRegex(core.ArenaError, "explicit root"):
            native.play(ent.key, ref.key, self.map, self.root / ".cache/output", snapshot_owner="owner",
                        snapshot_roots={ent.key: None, ref.key: self.arena / "snap"})

    def test_root_conflicts_are_rejected_by_general_provenance_not_only_native_replay(self):
        ent, global_ent = self.private_entry(), self.global_entry()
        spec = self.spec(ent, global_ent)
        with self.assertRaisesRegex(core.ArenaError, "conflicting roots"):
            exp.recorded_snapshot_root(spec, "candidate")
        self.assertTrue(any("conflicting roots" in gap for gap in exp.provenance_gaps(spec)))
        args = SimpleNamespace(name="owner/conflict", question="conflicting roots must never play",
                               subject=["candidate=" + self.bot.id], subjects=None, vs=[self.bot.id],
                               maps="map-one", sides="both", rounds=0, keep="none", force=False,
                               private_snapshots=True)
        with patch.object(exp, "build", return_value=([ent], [global_ent])), \
                patch.object(core, "map_set", return_value=["map-one"]), \
                patch.object(exp, "run_games", side_effect=AssertionError("RAW games must not be reached")) as run, \
                self.assertRaisesRegex(core.ArenaError, "conflicting roots"):
            exp.cmd_run(args)
        run.assert_not_called()

    def test_only_private_mode_labels_normalized_rows_as_diagnostic(self):
        spec = {"subjects": [{"label": "candidate"}], "opponent_ids": [self.ref.id]}
        part = [f"{self.map} A s0 - o0 - s0.o0"]
        raw = [{"map": "map-one", "a_side": "A", "tag": "s0.o0", "winner": "a", "rounds": 10,
                "gems": [5, 1], "exhausted": [0, 0], "faults": [0, 0], "errors": [0, 0]}]
        ordinary = exp.normalized_chunk(spec, part, raw)
        self.assertNotIn("diagnostic_only", ordinary[0])
        private = exp.normalized_chunk({**spec, "private_snapshots": True}, part, raw)
        self.assertIs(private[0].pop("diagnostic_only"), True)
        self.assertEqual(private, ordinary)

    def test_slimming_protects_private_inputs_but_not_the_owners_experiment_output(self):
        ent = self.private_entry()
        data = ent.path / "src/private-input.bin"; data.write_bytes(b"input")
        raw = self.private.parent / "exp/example/raw.bin"
        raw.parent.mkdir(parents=True); raw.write_bytes(b"regenerable experiment output")
        with patch.object(checkup, "CACHE", self.root / ".cache"), \
                patch.object(checkup, "SKIP_DIRS", {self.arena / "snap"}):
            scanned = {Path(item.path) for item in checkup.scan(self.root / ".cache")}
        self.assertNotIn(data, scanned)
        self.assertIn(raw, scanned)

    def retention_fixture(self, latest):
        ent, ref = self.global_entry(), self.global_entry(self.ref)
        spec = self.spec(ent, ref)
        spec.pop("private_snapshots"); spec.pop("reuse_shared_snapshots")
        del spec["provenance"]["snapshot_roots"]
        spec["name"] = "owner/case"
        directory = self.root / ".cache/owner/exp/case"; directory.mkdir(parents=True)
        spec_file, rows_file = directory / "spec.json", directory / "rows.jsonl"
        spec_file.write_text(json.dumps(spec), encoding="utf-8")
        rows_file.write_text('{"fixture":"unchanged rows"}\n', encoding="utf-8")
        evidence = {name: {"path": p.relative_to(self.root).as_posix(), "sha256": exp.sha256(p)}
                    for name, p in (("spec", spec_file), ("rows", rows_file))}
        if latest:
            record = {"schema": "development-main-strength-floor/v3", "reference_epoch": "ref",
                      "reference": {"snapshot": ref.key, "wasm_sha256": exp.sha256(Path(exp.wasm(ref)))},
                      "current_targeted_qualification": {ent.bot.id: {
                          "bot": ent.bot.id, "snapshot": ent.key,
                          "wasm_sha256": exp.sha256(Path(exp.wasm(ent))), "games": 2}},
                      "retention": {"schema": "arena-current-strength-retention/v1",
                                    "experiments": [{"name": spec["name"], "entrants": spec["entrants"], **evidence}]}}
            file = self.root / "results/regression/strength-current.json"
        else:
            record = {"schema": "experiment-coverage-audit/v1", "experiment": spec["name"],
                      "entrants": spec["entrants"], "evidence": evidence}
            file = self.root / "results/regression/current.json"
        file.parent.mkdir(parents=True, exist_ok=True); file.write_text(json.dumps(record), encoding="utf-8")
        return spec, spec_file, record, file

    def retention_refusal(self, latest):
        spec, spec_file, record, file = self.retention_fixture(latest)
        reader = report.strength_protected_paths if latest else report._canonical_regression_protected_paths
        self.assertTrue(reader(self.arena), "the otherwise-valid global fixture must pass first")
        original = copy.deepcopy(spec)
        for marker in ("mode", "roots"):
            spec = copy.deepcopy(original)
            if marker == "mode": spec["private_snapshots"] = True
            else: spec["provenance"]["snapshot_roots"] = {}
            spec_file.write_text(json.dumps(spec), encoding="utf-8")
            evidence = record["retention"]["experiments"][0] if latest else record["evidence"]
            evidence["spec"]["sha256"] = exp.sha256(spec_file)
            file.write_text(json.dumps(record), encoding="utf-8")
            with self.subTest(marker=marker), self.assertRaisesRegex(core.ArenaError, "private snapshot diagnostics"):
                reader(self.arena)

    def test_canonical_retention_rejects_even_hash_valid_private_diagnostic_specs(self):
        self.retention_refusal(False)

    def test_latest_retention_rejects_even_hash_valid_private_diagnostic_specs(self):
        self.retention_refusal(True)

    def test_an_otherwise_passing_floor_gets_no_private_diagnostic_credit(self):
        opponent = self.ref.id
        subject_key = "odd/one@aaaaaaaaaaaa"
        reference_key = opponent + "@bbbbbbbbbbbb"
        pool = ["m" + str(i) for i in range(40)]
        spec = {"name": "owner/floor", "entrants": {opponent: reference_key}, "sides": "both", "rounds": 0,
                "provenance": {"maps": {name: "x" for name in pool}}}
        report = {"coverage": {"opponents": [opponent]}, "integrity_pass": True, "provenance_gaps": [],
                  "bots": [{"source_error": None, "current_snapshot": subject_key,
                            "observations": [{"source_current": True, "default_configuration": True,
                                              "snapshot": subject_key, "matrix_complete": True,
                                              "by_opponent": {opponent: {"games": 80, "wins": 1, "draws": 0, "losses": 79,
                                                                        "own_flagged_games": 0, "truncated_games": 0,
                                                                        "healthy_wins": 1}}}]}]}
        with patch.object(core, "bot", return_value=self.ref), patch.object(core, "version", return_value="bbbbbbbbbbbb"), \
                patch.object(core, "map_set", return_value=pool):
            plain = copy.deepcopy(report)
            self.assertTrue(exp.win_floor(plain, spec, opponent)["all_passed"])
            for marker in ("mode", "roots"):
                changed = copy.deepcopy(spec)
                if marker == "mode": changed["private_snapshots"] = True
                else: changed["provenance"]["snapshot_roots"] = {}
                value = copy.deepcopy(report)
                self.assertFalse(exp.win_floor(value, changed, opponent)["all_passed"])
                self.assertTrue(any("private" in reason.lower() or "diagnostic" in reason.lower()
                                    for reason in value["bots"][0]["win_floor"]["reasons"]))


if __name__ == "__main__":
    unittest.main()
