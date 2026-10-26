"""Private row normalization/checkpoint/resume contract; no engine, build or real bot input."""
from contextlib import ExitStack, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import exp

RECOVERY_HELPER = Path(__file__).resolve().parents[1] / "recover_completed_chunk.py"


class DiagnosticCheckpoint(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/diagnostic-checkpoint-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = base / ("case-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for module, name, value in ((core, "ROOT", self.root), (exp, "ROOT", self.root),
                                    (core, "CACHE", self.root / ".cache/arena")):
            self.stack.enter_context(patch.object(module, name, value))
        self.spec = {
            "name": "owner/diagnostic", "question": "complete each recorded key once",
            "status": "open", "created": "synthetic", "preparation": {"state": "complete"},
            "private_snapshots": True, "reuse_shared_snapshots": False,
            "subjects": [{"label": "candidate", "bot": "odd/one", "set": {}, "weights": None}],
            "opponents": ["anchors/ref"], "opponent_ids": ["anchors/ref"],
            "entrants": {"candidate": "odd/one@aaaaaaaaaaaa", "anchors/ref": "anchors/ref@bbbbbbbbbbbb"},
            "maps": "two-recorded", "sides": "both", "rounds": 0, "games": 4, "keep": "none",
            "provenance": {
                "maps": {"one": "1" * 64, "two": "2" * 64}, "bundle": "bundle", "engine": "engine",
                "runner": "runner", "runner_source": "source", "toolchain": "toolchain",
                "runner_libraries": {}, "weights": {},
                "wasm": {"candidate": "a" * 64, "anchors/ref": "b" * 64},
                "snapshot_roots": {"candidate": ".cache/owner/snap", "anchors/ref": ".cache/owner/snap"},
            },
        }
        self.directory = exp.exp_dir(self.spec["name"])
        self.directory.mkdir(parents=True)
        exp.write_spec(self.directory, self.spec)
        self.args = SimpleNamespace(name=self.spec["name"], progress_from=None)
        self.stack.enter_context(patch.object(core, "map_path", side_effect=lambda name: self.root / (name + ".map26a")))
        self.stack.enter_context(patch.object(exp, "provenance_gaps", return_value=[]))
        self.stack.enter_context(patch.object(exp, "replay_entrant", side_effect=lambda spec, label, subject, **kw:
                                              SimpleNamespace(path=self.root / "cached" / label)))
        for name in ("prepare", "prepare_many", "select", "version", "map_set"):
            self.stack.enter_context(patch.object(core, name, side_effect=AssertionError("resume must only use pinned inputs")))
        self.stack.enter_context(patch.object(exp, "show"))
        self.games, self.keys = exp.game_plan(self.spec)
        self.expected = set(self.keys)

    def raw(self, lines):
        return [{"map": Path(fields[0]).stem, "a_side": fields[1], "tag": fields[6],
                 "winner": "a", "rounds": 2000, "gems": [1000, 800],
                 "exhausted": [0, 0], "faults": [0, 0], "errors": [0, 0]}
                for fields in (line.split() for line in lines)]

    def test_private_normalizer_checkpoint_resume_pipeline_never_replays_completed_keys(self):
        initial_calls = []
        def interrupted(lines, bots, out, rounds=0, *, on_chunk):
            initial_calls.append(list(lines))
            on_chunk(lines[:2], self.raw(lines[:2]))
            raise KeyboardInterrupt("test interruption after a durable chunk")
        with patch.object(exp, "run_games", side_effect=interrupted), redirect_stdout(io.StringIO()), \
                self.assertRaises(KeyboardInterrupt):
            exp.execute_prepared(self.directory, self.spec, self.games, {})
        self.assertEqual(initial_calls, [self.games])
        checkpoint = self.directory / "progress.json"
        saved = exp.read_progress(checkpoint, self.spec, self.expected)
        self.assertEqual(set(saved), set(self.keys[:2]))
        self.assertTrue(all(row["diagnostic_only"] is True for row in saved.values()))
        self.assertFalse((self.directory / "rows.jsonl").exists())

        resumed_calls = []
        def remaining(lines, bots, out, rounds=0, *, on_chunk):
            resumed_calls.append(list(lines))
            self.assertEqual(lines, self.games[2:])
            rows = self.raw(lines)
            on_chunk(lines, rows)
            return rows
        with patch.object(exp, "run_games", side_effect=remaining), redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        self.assertEqual(resumed_calls, [self.games[2:]])
        canonical = exp.read_rows(self.directory / "rows.jsonl")
        validated = exp.validate_progress_rows(canonical, self.expected, spec=self.spec)
        self.assertEqual(list(validated), self.keys)
        self.assertTrue(all(row["diagnostic_only"] is True for row in canonical))
        self.assertEqual({key: validated[key] for key in saved}, saved)
        self.assertEqual(len(exp.read_progress(checkpoint, self.spec, self.expected)), 4)
        with patch.object(exp, "run_games", side_effect=AssertionError("complete resume must run zero games")) as run, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        run.assert_not_called()
        # Every physical key reached the native seam once: first chunk then missing chunk.
        self.assertEqual(self.games[:2] + resumed_calls[0], self.games)

    def test_normal_rows_and_private_rows_have_disjoint_typed_marker_contracts(self):
        private = exp.normalized_chunk(self.spec, self.games, self.raw(self.games))
        self.assertEqual(len(exp.validate_progress_rows(private, self.expected, spec=self.spec)), 4)
        normal_spec = copy.deepcopy(self.spec)
        normal_spec.pop("private_snapshots")
        normal_spec.pop("reuse_shared_snapshots")
        normal_spec["provenance"].pop("snapshot_roots")
        normal = exp.normalized_chunk(normal_spec, self.games, self.raw(self.games))
        self.assertTrue(all("diagnostic_only" not in row for row in normal))
        self.assertEqual(len(exp.validate_progress_rows(normal, self.expected, spec=normal_spec)), 4)
        self.assertEqual(len(exp.validate_progress_rows(normal, self.expected)), 4)
        for marker in (False, 1, "true", None, {}, []):
            with self.subTest(marker=marker):
                altered = copy.deepcopy(private)
                altered[0]["diagnostic_only"] = marker
                with self.assertRaises(core.ArenaError):
                    exp.validate_progress_rows(altered, self.expected, spec=self.spec)
        missing = copy.deepcopy(private)
        missing[0].pop("diagnostic_only")
        with self.assertRaises(core.ArenaError):
            exp.validate_progress_rows(missing, self.expected, spec=self.spec)
        with self.assertRaises(core.ArenaError):
            exp.validate_progress_rows(private, self.expected, spec=normal_spec)
        with self.assertRaises(core.ArenaError):
            exp.validate_progress_rows(private, self.expected)

    def test_completed_raw_chunk_import_finalizes_without_replaying_any_game(self):
        listing, raw_file = self.directory / "run.completed.games0.txt", self.directory / "run.completed.rows0.jsonl"
        listing.write_text("\n".join(self.games) + "\n", encoding="utf-8", newline="\n")
        raw_file.write_text("".join(json.dumps(row) + "\n" for row in self.raw(self.games)),
                            encoding="utf-8", newline="\n")
        untouched = {path: path.read_bytes() for path in (listing, raw_file, self.directory / "spec.json")}
        part = listing.read_text().splitlines()
        self.assertEqual(set(part), set(self.games))
        rows = exp.normalized_chunk(self.spec, part, exp.read_rows(raw_file))
        validated = exp.validate_progress_rows(rows, self.expected, spec=self.spec)
        observer = self.directory.parent.parent / "recovered-progress.json"
        exp.write_document(observer, {"schema": "arena-experiment-progress/v1",
                                     "fingerprint": exp.progress_fingerprint(self.spec),
                                     "rows": [validated[key] for key in self.keys]})
        self.args.progress_from = observer
        with patch.object(exp, "run_games", side_effect=AssertionError("completed native rows must not be rerun")) as run, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_resume(self.args), 0)
        run.assert_not_called()
        self.assertEqual(exp.read_rows(self.directory / "rows.jsonl"), rows)
        self.assertEqual({path: path.read_bytes() for path in untouched}, untouched)

    def test_runner_cannot_spoof_the_adapter_marker(self):
        for marker in (True, False, 1, "true"):
            raw = self.raw(self.games)
            raw[0]["diagnostic_only"] = marker
            with self.subTest(marker=marker), self.assertRaises(core.ArenaError):
                exp.normalized_chunk(self.spec, self.games, raw)

    def test_supported_recovery_adapter_preserves_inputs_and_resumes_zero_games(self):
        listing, raw = self.directory / "completed.games.txt", self.directory / "completed.rows.jsonl"
        listing.write_text("\n".join(self.games) + "\n", encoding="utf-8", newline="\n")
        raw.write_text("".join(json.dumps(row) + "\n" for row in self.raw(self.games)),
                       encoding="utf-8", newline="\n")
        untouched = {path: path.read_bytes() for path in (listing, raw, self.directory / "spec.json")}
        observer = self.directory.parent.parent / "owner-observer-progress.json"
        module_spec = importlib.util.spec_from_file_location("supported_chunk_recovery", RECOVERY_HELPER)
        helper = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(helper)
        argv = [str(RECOVERY_HELPER), self.spec["name"], "--rows", str(raw), "--games", str(listing),
                "--out", str(observer)]
        with patch.object(sys, "argv", argv), redirect_stdout(io.StringIO()), \
                patch.object(exp, "run_games", side_effect=AssertionError("recovery must not replay completed games")) as run:
            helper.main()
            document = exp.read_spec(observer)
            self.assertTrue(all(row["diagnostic_only"] is True for row in document["rows"]))
            self.assertEqual(document["fingerprint"], exp.progress_fingerprint(self.spec))
            self.args.progress_from = observer
            self.assertEqual(exp.cmd_resume(self.args), 0)
        run.assert_not_called()
        self.assertEqual(untouched, {path: path.read_bytes() for path in untouched})

    def test_progress_is_bound_to_private_mode_and_every_recorded_root(self):
        rows = exp.normalized_chunk(self.spec, self.games, self.raw(self.games))
        progress = self.directory / "import.json"
        exp.write_document(progress, {"schema": "arena-experiment-progress/v1",
                                     "fingerprint": exp.progress_fingerprint(self.spec), "rows": rows})
        self.assertEqual(len(exp.read_progress(progress, self.spec, self.expected)), 4)
        variants = []
        changed = copy.deepcopy(self.spec); changed["private_snapshots"] = False; variants.append(changed)
        changed = copy.deepcopy(self.spec); changed.pop("private_snapshots"); variants.append(changed)
        changed = copy.deepcopy(self.spec); changed["provenance"]["snapshot_roots"]["candidate"] = ".cache/other/snap"; variants.append(changed)
        changed = copy.deepcopy(self.spec); changed["reuse_shared_snapshots"] = True; variants.append(changed)
        for changed in variants:
            with self.subTest(changed=changed), self.assertRaisesRegex(core.ArenaError, "fingerprint"):
                exp.read_progress(progress, changed, self.expected)

    def test_saved_rows_cannot_bypass_private_marker_validation(self):
        rows = exp.normalized_chunk(self.spec, self.games[:1], self.raw(self.games[:1]))
        rows[0]["diagnostic_only"] = False
        with patch.object(exp, "run_games") as run, self.assertRaises(core.ArenaError):
            exp.execute_prepared(self.directory, self.spec, self.games, {}, {self.keys[0]: rows[0]})
        run.assert_not_called()

    def test_normal_absent_and_false_modes_preserve_the_previous_fingerprint_and_checkpoint(self):
        normal = copy.deepcopy(self.spec)
        normal.pop("private_snapshots")
        normal["provenance"].pop("snapshot_roots")
        # The precise algorithm before the diagnostic-marker fix, including optional reuse/descriptors.
        fields = ("subjects", "entrants", "opponent_ids", "opponents", "maps", "sides", "rounds", "games", "provenance")
        old_inputs = {key: normal.get(key) for key in fields}
        for optional in ("opponent_descriptors", "reuse_shared_snapshots"):
            if optional in normal:
                old_inputs[optional] = normal[optional]
        old = hashlib.sha256(json.dumps(old_inputs, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
        rows = exp.normalized_chunk(normal, self.games, self.raw(self.games))
        progress = self.directory / "old-normal-progress.json"
        exp.write_document(progress, {"schema": "arena-experiment-progress/v1", "fingerprint": old, "rows": rows})
        for explicit_false in (False, True):
            candidate = copy.deepcopy(normal)
            if explicit_false:
                candidate["private_snapshots"] = False
            self.assertEqual(exp.progress_fingerprint(candidate), old)
            self.assertEqual(len(exp.read_progress(progress, candidate, self.expected)), 4)
        for invalid in (None, 0, 1, "false", {}, []):
            candidate = copy.deepcopy(normal); candidate["private_snapshots"] = invalid
            with self.subTest(invalid=invalid), self.assertRaises(core.ArenaError):
                exp.progress_fingerprint(candidate)


if __name__ == "__main__":
    unittest.main()
