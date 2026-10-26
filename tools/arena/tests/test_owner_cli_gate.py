"""Owner CLI qualification fixtures. All inputs, tools and contained workers are synthetic."""
from __future__ import annotations

from contextlib import ExitStack
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import unittest
import uuid
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import games
import gate
import release
import compiled_runner as transport

RELEASE_EXECUTABLE_PROOF_RESOLVER = gate._require_released_executable_proof


class OwnerCliGate(unittest.TestCase):
    def setUp(self):
        base = core.ROOT / ".cache/owner-cli-gate/tests"
        base.mkdir(parents=True, exist_ok=True)
        self.base = base
        self.root = base / uuid.uuid4().hex
        self.root.mkdir()
        self.stack = ExitStack()
        self.addCleanup(self.cleanup)
        self.addCleanup(self.stack.close)
        arena = self.root / ".cache/arena"
        for module, name, value in (
            (core, "ROOT", self.root), (core, "BOTS", self.root / "bots"),
            (core, "MAPS", self.root / "maps"), (core, "CACHE", arena),
            (gate, "ROOT", self.root), (gate, "BOTS", self.root / "bots"),
            (games, "ROOT", self.root), (games, "CACHE", arena),
            (transport, "ROOT", self.root), (release, "ROOT", self.root),
        ):
            self.stack.enter_context(patch.object(module, name, value))
        self.stack.enter_context(patch.object(core, "toolchain", return_value="fixture-toolchain"))
        # Synthetic valid-flow fixtures isolate the separately unsupported proof prerequisite.
        # The explicit refusal case below restores the real resolver with a registered test route.
        self.stack.enter_context(patch.object(gate, "_require_released_executable_proof", return_value=None))
        self.stack.enter_context(patch.object(core, "pool_maps", side_effect=AssertionError("shared CLI pool lookup")))
        self.stack.enter_context(patch.object(release, "pool_maps", return_value=["fixture-map"]))
        self.stack.enter_context(patch.object(core, "_pool_index", side_effect=AssertionError("shared CLI map index read")))
        self.stack.enter_context(patch.object(core, "map_info", side_effect=AssertionError("shared CLI map metadata read")))
        self.stack.enter_context(patch.object(games, "ENGINE", "fixture-native"))
        for module, names in ((core, ("prepare", "prepare_many", "read_closure", "build")),
                              (gate, ("find_bot", "prepare", "prepare_many", "read_closure")),
                              (transport, ("_WinJobProcess", "_capture", "play"))):
            for name in names:
                self.stack.enter_context(patch.object(module, name, side_effect=AssertionError("real preparation/native/policy access")))
        self.owner = "owner"
        self.snap_root = self.root / ".cache/owner/snap"
        self.subject = self.snapshot("main", "main", b"int main(){return 7;}\n")
        self.reference = self.snapshot("anchors", "live-v1", b"int main(){return 2;}\n")
        self.health = self.snapshot("contenders", "fighter", b"int main(){return 3;}\n")
        self.map = self.root / "maps/fixture-map.map26a"
        self.write(self.map, b"public fixture map")
        self.pool_file = self.root / "maps/pool.json"
        self.write_json(self.pool_file, {"maps": [{"name": "fixture-map", "sha256": self.sha(self.map),
                                                   "size": self.map.stat().st_size}]})
        cli = self.root / ".tools/cambc.exe"
        self.write(cli, b"inert fixture CLI; never executed")
        self.stack.enter_context(patch.object(core, "CAMBC", cli))
        self.stack.enter_context(patch.object(games, "CAMBC", cli))
        cli_sha = self.sha(cli)
        self.write_json(self.root / "docs/reference/current-version.json",
                        {"cli": {"windowsX64": {"sha256": cli_sha}}, "engine": {"version": "fixture-native"}})
        self.write_json(self.root / "docs/reference/engine-identity.json",
                        {"nativeCli": {"sha256": cli_sha}, "engine": {"version": "fixture-native", "buildHash": "f" * 64}})
        for name in ("gate.py", "games.py", "core.py", "compiled_runner.py", "release.py", "health_context.py"):
            self.write(self.root / "tools/arena" / name, ("# synthetic " + name + "\n").encode())
        self.script = self.root / "tools/owner_test_route.py"
        self.write(self.script, b"# inert reviewed test route; launcher is mocked\n")
        self.route = {"script": self.script, "files": {self.script: self.sha(self.script)}, "suites": ("main", "contenders")}
        self.stack.enter_context(patch.object(gate, "OWNER_TEST_ROUTES", {"fixture": self.route}))
        released_contents = {self.root / "bots/main/main/src/main.cpp": (self.reference.path / "src/main.cpp").read_bytes()}
        self.live = {"name": "live-v1", "anchor": self.reference.bot.id, "bot": "main/main",
                     "key": "main/main@" + core.version_of(released_contents)}
        self.write_json(self.root / "results/releases/LIVE.json", self.live)
        self.release_dir = self.root / "results/releases/live-v1"
        self.write_json(self.release_dir / "release.json", self.live)
        with zipfile.ZipFile(self.release_dir / "main.zip", "w") as archive:
            archive.writestr("src/main.cpp", (self.reference.path / "src/main.cpp").read_bytes())
        self.out = self.root / ".cache/owner/gates/fixture"
        self.inputs = gate.bind_owner_inputs("owner", self.subject.key, self.reference.key,
                                             [self.health.key], ["fixture-map"], "fixture")

    def cleanup(self):
        self.assertEqual(self.root.resolve(), self.root)
        self.assertTrue(self.root.is_relative_to(self.base))
        shutil.rmtree(self.root)

    @staticmethod
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def write_json(self, path, value):
        self.write(path, (json.dumps(value, sort_keys=True) + "\n").encode())

    def snapshot(self, suite, name, source):
        bot = core.Bot(suite, name)
        contents = {self.root / "bots" / suite / name / "src/main.cpp": source}
        version = core.version_of(contents)
        snap = self.snap_root / version
        target = snap / "bots" / suite / name
        self.write(target / "src/main.cpp", source)
        self.write_json(snap / "meta.json", {"bot": bot.id, "version": version, "overrides": {}, "files": 1})
        self.write(snap / ".ready", b"fixture ready\n")
        self.write(snap / ".toolchain", b"fixture-toolchain\n")
        for filename, payload in (("bot.wasm", b"raw"), ("bot-injected.wasm", b"sdk")):
            self.write(target / ".cambc/build-release" / filename, b"\0asm\x01\0\0\0" + payload)
        return core.Entrant(bot, version, target, True)

    def process(self, directory, argv):
        self.write_json(directory / "dispatch.json", {"argv": argv, "cwd": str(self.root), "timeout": 30})
        result = {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True, "forced_termination": False,
                  "containment": {"kind": "windows-private-job", "assigned_before_resume": True, "resumed": True,
                                  "empty_confirmed": True, "kill_on_close": True, "forced_termination": False}}
        self.write_json(directory / "process-end.json", result)
        return result

    def fake_test_command(self, argv_factory, cwd, directory, timeout, jobs=1, **kwargs):
        self.assertEqual(cwd, self.root)
        self.assertEqual(jobs, 1)
        argv = argv_factory(1)
        self.assertEqual(argv[:3], [str(Path(sys.executable)), "-B", str(self.script)])
        self.assertEqual(argv[3:7], ["--owner", "owner", "--inputs", str(self.out / "inputs.json")])
        output = Path(argv[-1])
        self.write_json(output / "tests.json", {"schema": gate.OWNER_TEST_SCHEMA, "owner": self.owner,
                        "input_fingerprint": gate._hash(self.inputs), "passed": True,
                        "tests": [{"name": "bounded fixture", "passed": True}]})
        return self.process(directory, argv)

    @staticmethod
    def team(gems):
        return dict.fromkeys(games.TEAM_KEYS, 0) | {"gems": gems, "clears": 2, "fuel_max": 40000}

    def fake_rows(self, specs, out, **kwargs):
        self.assertEqual(kwargs["owner_run"].fingerprint, gate._hash(self.inputs))
        rows, lines, keys = [], [], []
        for i, spec in enumerate(specs):
            side = spec.tags["subject"]
            row = games._base_row(spec, spec.map)
            row.update(winner=side, rounds=2000, replay_health_complete=True,
                       measurement_complete=True, owner_input_fingerprint=gate._hash(self.inputs))
            row[side], row["B" if side == "A" else "A"] = self.team(120), self.team(60)
            rows.append(row)
            keys.append(games._resume_key(spec.a.key, spec.b.key, spec.map, spec.tags))
            lines.append(f"{spec.a.path} {spec.b.path} {self.map} {out / 'games' / f'{i}.repl26a'} --statistics {out / 'games' / f'{i}.json'} --json")
        manifest = out / "games/batch-fixture.txt"
        self.write(manifest, ("\n".join(lines) + "\n").encode())
        process_dir = out / "games/batch-fixture.transport"
        argv = [str(core.CAMBC), "run-batch", str(manifest), "--release", "--jobs", "1", "--color", "never"]
        if games.GAME_TIMEOUT:
            argv += ["--timeout", str(games.GAME_TIMEOUT)]
        self.process(process_dir, argv)
        receipt = {"schema": "arena-owner-cli-transport-v1", "owner": self.owner,
                   "owner_input_fingerprint": gate._hash(self.inputs), "game_keys": keys,
                   "rows_sha256": {key: gate._hash(row) for key, row in zip(keys, rows)},
                   "manifest": gate._file(manifest), "dispatch": gate._file(process_dir / "dispatch.json"),
                   "process": gate._file(process_dir / "process-end.json")}
        receipt_path = process_dir / "owner-run.json"
        self.write_json(receipt_path, receipt)
        descriptor = gate._file(receipt_path)
        for row in rows:
            row["owner_transport"] = descriptor
        self.write(out / "games.jsonl", b"".join((json.dumps(row) + "\n").encode() for row in rows))
        return rows

    def args(self):
        return SimpleNamespace(owner_inputs=str(self.root / ".cache/owner/inputs.json"), bot=self.subject.bot.id,
                               ref=self.reference.bot.id, test_args="", rounds=0, health_vs=gate.DEFAULT_HEALTH_VS,
                               maps="pool", min_ref_score=0.5, out=str(self.out), jobs=1,
                               keep_replays="none", max_minutes=0, fail_fast=False)

    def complete_gate(self):
        args = self.args()
        self.write_json(Path(args.owner_inputs), self.inputs)
        with patch.object(transport, "_slotted_command", side_effect=self.fake_test_command) as commands, \
                patch.object(gate, "play", side_effect=self.fake_rows) as play:
            self.assertEqual(gate._cmd_gate(args), 0)
        self.assertEqual(commands.call_count, 1)
        self.assertEqual(play.call_count, 1)
        return json.loads((self.out / "gate.json").read_text())

    def verify(self):
        return release.verify_gate_for_release(self.out, self.subject.key, self.live)

    def test_complete_bound_gate_is_consumed_by_release(self):
        record = self.complete_gate()
        self.assertEqual(self.verify(), record)
        self.assertTrue(record["pass"])
        self.assertEqual(record["games"], 4)

    def test_main_and_contenders_cached_inputs_need_no_policy_or_native_capture(self):
        for entry in (self.subject, self.health):
            self.assertEqual(core.cached_snapshot(entry.key, snapshot_root=self.snap_root,
                                                 snapshot_owner=self.owner).key, entry.key)
        self.assertEqual(gate.validate_owner_inputs(self.inputs), self.inputs)
        self.assertFalse((self.root / "bots").exists())

    def test_complete_gate_resume_reuses_tests_and_games(self):
        self.complete_gate()
        with patch.object(gate, "play", side_effect=AssertionError("completed games rerun")), \
                patch.object(transport, "_slotted_command", side_effect=AssertionError("completed tests rerun")):
            self.assertEqual(gate._cmd_gate(self.args()), 0)

    def test_unsupported_test_route_stops_before_output_or_dispatch(self):
        self.write_json(Path(self.args().owner_inputs), self.inputs)
        with patch.object(gate, "OWNER_TEST_ROUTES", {}), \
                patch.object(transport, "_slotted_command", side_effect=AssertionError("unexpected dispatch")), \
                self.assertRaisesRegex(core.ArenaError, "unsupported"):
            gate._cmd_gate(self.args())
        self.assertFalse(self.out.exists())

    def test_released_executable_proof_is_required_even_with_registered_tests(self):
        self.assertIn("fixture", gate.OWNER_TEST_ROUTES)
        self.write_json(Path(self.args().owner_inputs), self.inputs)
        with patch.object(gate, "_require_released_executable_proof", RELEASE_EXECUTABLE_PROOF_RESOLVER), \
                patch.object(transport, "_slotted_command", side_effect=AssertionError("unexpected dispatch")), \
                self.assertRaisesRegex(core.ArenaError, "independent released RAW/SDK executable proof"):
            gate._cmd_gate(self.args())
        self.assertFalse(self.out.exists())

    def test_input_mutations_fail_closed(self):
        files = {
            "source": self.subject.path / "src/main.cpp",
            "meta": self.subject.path.parents[2] / "meta.json",
            "toolchain": self.subject.path.parents[2] / ".toolchain",
            "RAW": self.subject.path / ".cambc/build-release/bot.wasm",
            "SDK": self.subject.path / ".cambc/build-release/bot-injected.wasm",
            "map": self.map,
            "pool registry": self.pool_file,
            "runtime": core.CAMBC,
            "test source": self.script,
        }
        for name, path in files.items():
            original = path.read_bytes()
            with self.subTest(name=name):
                path.write_bytes(original + b"changed")
                with self.assertRaises((core.ArenaError, ValueError)):
                    gate.validate_owner_inputs(self.inputs)
                path.write_bytes(original)

    def test_missing_sdk_is_not_built_or_substituted(self):
        (self.subject.path / ".cambc/build-release/bot-injected.wasm").unlink()
        with self.assertRaises(core.ArenaError):
            gate.validate_owner_inputs(self.inputs)

    def test_shared_root_refused_before_shared_resolution(self):
        value = copy.deepcopy(self.inputs)
        value["entrants"][self.subject.key]["root"] = ".cache/arena/snap"
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            if path == self.root / ".cache/arena/snap":
                self.fail("shared snapshot namespace was probed")
            return original(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), self.assertRaises(core.ArenaError):
            gate.validate_owner_inputs(value)

    def test_redirected_source_is_refused(self):
        alias = self.subject.path / "src/main.cpp"
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            return self.reference.path / "src/main.cpp" if path == alias else original(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), self.assertRaisesRegex(core.ArenaError, "redirected"):
            gate.validate_owner_inputs(self.inputs)

    def test_independent_live_bundle_must_match_reference(self):
        original = (self.release_dir / "main.zip").read_bytes()
        with zipfile.ZipFile(self.release_dir / "main.zip", "w") as archive:
            archive.writestr("src/main.cpp", b"candidate copied as reference")
        with self.assertRaisesRegex(core.ArenaError, "reference source"):
            gate.validate_owner_inputs(self.inputs)
        (self.release_dir / "main.zip").write_bytes(original)
        wrong = self.live | {"key": "main/main@123456789abc"}
        self.write_json(self.root / "results/releases/LIVE.json", wrong)
        self.write_json(self.release_dir / "release.json", wrong)
        with self.assertRaisesRegex(core.ArenaError, "recorded released key"):
            gate.validate_owner_inputs(self.inputs)

    def test_changed_live_record_is_refused(self):
        self.complete_gate()
        with self.assertRaisesRegex(core.ArenaError, "LIVE reference changed"):
            release.verify_gate_for_release(self.out, self.subject.key, self.live | {"key": "other@123456789abc"})

    def test_record_key_check_and_mode_tampering_refused(self):
        record = self.complete_gate()
        path = self.out / "gate.json"
        for name, mutate in (
            ("key", lambda v: v.update(key=self.reference.key)),
            ("check", lambda v: v["checks"][2].update(status="skip")),
            ("mode", lambda v: v.update(diagnostic_only=True)),
            ("root", lambda v: v["inputs"]["entrants"][self.subject.key].update(root=".cache/foreign/snap")),
            ("map list", lambda v: v.update(maps=[])),
        ):
            value = copy.deepcopy(record)
            mutate(value)
            value["evidence_fingerprint"] = gate._hash({k: v for k, v in value.items() if k != "evidence_fingerprint"})
            self.write_json(path, value)
            with self.subTest(name=name), self.assertRaises(core.ArenaError):
                self.verify()
        self.write_json(path, record)

    def test_row_tampering_cannot_be_rebound_as_passing_evidence(self):
        record = self.complete_gate()
        rows_path = self.out / "games.jsonl"
        rows = [json.loads(line) for line in rows_path.read_text().splitlines()]
        rows[0]["A"]["gems"] += 1
        self.write(rows_path, b"".join((json.dumps(row) + "\n").encode() for row in rows))
        record["rows"] = gate._file(rows_path)
        record["evidence_fingerprint"] = gate._hash({k: v for k, v in record.items() if k != "evidence_fingerprint"})
        self.write_json(self.out / "gate.json", record)
        with self.assertRaises(core.ArenaError):
            self.verify()

    def test_uncontained_test_execution_is_not_a_passing_test(self):
        self.complete_gate()
        record = json.loads((self.out / "tests.json").read_text())
        process_path = self.root / record["process"]["path"]
        process = json.loads(process_path.read_text())
        process["tree_exit_confirmed"] = False
        self.write_json(process_path, process)
        record["process"] = gate._file(process_path)
        with self.assertRaisesRegex(core.ArenaError, "contained execution"):
            gate._test_evidence(record, self.inputs, self.out)

    def test_supplied_pass_file_is_not_a_supported_route(self):
        value = copy.deepcopy(self.inputs)
        value["tests"] = {"route": "arbitrary-pass-file", "passed": True}
        with self.assertRaisesRegex(core.ArenaError, "unsupported"):
            gate.validate_owner_inputs(value)

    def test_external_map_refused_before_file_probe(self):
        external = self.root.parent / "not-owner-external.map26a"
        original = Path.is_file
        def is_file(path):
            if path == external:
                self.fail("external map was probed")
            return original(path)
        with patch.object(Path, "is_file", autospec=True, side_effect=is_file), self.assertRaises(core.ArenaError):
            gate.bind_owner_inputs("owner", self.subject.key, self.reference.key, [self.health.key],
                                   [str(external)], "fixture")

    def test_nonowner_input_manifest_refused_before_read(self):
        args = self.args()
        args.owner_inputs = str(self.root / ".cache/arena/shared-manifest.json")
        original = Path.read_text
        def read_text(path, *a, **kw):
            if path == Path(args.owner_inputs):
                self.fail("shared manifest was read")
            return original(path, *a, **kw)
        with patch.object(Path, "read_text", autospec=True, side_effect=read_text), self.assertRaises(core.ArenaError):
            gate._cmd_gate(args)

    def test_weight_policy_cannot_load_runtime_weights(self):
        value = copy.deepcopy(self.inputs)
        value["weights_policy"] = "external-runtime-weights"
        with self.assertRaises(core.ArenaError):
            gate.validate_owner_inputs(value)

    def test_missing_or_duplicate_side_rows_refused(self):
        record = self.complete_gate()
        rows_path = self.out / "games.jsonl"
        original_rows = [json.loads(line) for line in rows_path.read_text().splitlines()]
        for name, rows in (("missing", original_rows[:-1]), ("duplicate", original_rows[:-1] + [original_rows[0]])):
            self.write(rows_path, b"".join((json.dumps(row) + "\n").encode() for row in rows))
            value = copy.deepcopy(record)
            value["rows"] = gate._file(rows_path)
            value["evidence_fingerprint"] = gate._hash({k: v for k, v in value.items() if k != "evidence_fingerprint"})
            self.write_json(self.out / "gate.json", value)
            with self.subTest(name=name), self.assertRaises(core.ArenaError):
                self.verify()

    def rebind_transport(self, record, rows, receipt_path, receipt):
        self.write_json(receipt_path, receipt)
        descriptor = gate._file(receipt_path)
        for row in rows:
            row["owner_transport"] = descriptor
        rows_path = self.out / "games.jsonl"
        self.write(rows_path, b"".join((json.dumps(row) + "\n").encode() for row in rows))
        record["rows"] = gate._file(rows_path)
        record["evidence_fingerprint"] = gate._hash({k: v for k, v in record.items() if k != "evidence_fingerprint"})
        self.write_json(self.out / "gate.json", record)

    def test_cli_command_tamper_refused_after_rebinding_descriptors(self):
        record = self.complete_gate()
        rows = [json.loads(line) for line in (self.out / "games.jsonl").read_text().splitlines()]
        receipt_path = self.root / rows[0]["owner_transport"]["path"]
        receipt = json.loads(receipt_path.read_text())
        dispatch_path = self.root / receipt["dispatch"]["path"]
        dispatch = json.loads(dispatch_path.read_text())
        dispatch["argv"] += ["--rounds", "1"]
        self.write_json(dispatch_path, dispatch)
        receipt["dispatch"] = gate._file(dispatch_path)
        self.rebind_transport(record, rows, receipt_path, receipt)
        with self.assertRaisesRegex(core.ArenaError, "unrestricted CLI"):
            self.verify()

    def test_cli_manifest_tamper_refused_after_rebinding_descriptors(self):
        record = self.complete_gate()
        rows = [json.loads(line) for line in (self.out / "games.jsonl").read_text().splitlines()]
        receipt_path = self.root / rows[0]["owner_transport"]["path"]
        receipt = json.loads(receipt_path.read_text())
        manifest = self.root / receipt["manifest"]["path"]
        lines = manifest.read_text().splitlines()
        fields = lines[0].split()
        fields[0] = str(self.health.path)
        lines[0] = " ".join(fields)
        self.write(manifest, ("\n".join(lines) + "\n").encode())
        receipt["manifest"] = gate._file(manifest)
        self.rebind_transport(record, rows, receipt_path, receipt)
        with self.assertRaisesRegex(core.ArenaError, "CLI manifest"):
            self.verify()

    def test_test_result_requires_typed_passing_cases(self):
        self.complete_gate()
        receipt = json.loads((self.out / "tests.json").read_text())
        result_path = self.root / receipt["result"]["path"]
        result = json.loads(result_path.read_text())
        result["tests"][0]["passed"] = 1
        self.write_json(result_path, result)
        receipt["result"] = gate._file(result_path)
        with self.assertRaisesRegex(core.ArenaError, "passing case result"):
            gate._test_evidence(receipt, self.inputs, self.out)

    def test_game_process_proof_cannot_be_borrowed_from_other_directory(self):
        record = self.complete_gate()
        rows = [json.loads(line) for line in (self.out / "games.jsonl").read_text().splitlines()]
        receipt_path = self.root / rows[0]["owner_transport"]["path"]
        receipt = json.loads(receipt_path.read_text())
        old_process = self.root / receipt["process"]["path"]
        borrowed = self.root / ".cache/owner/other-command/process-end.json"
        self.write(borrowed, old_process.read_bytes())
        receipt["process"] = gate._file(borrowed)
        self.rebind_transport(record, rows, receipt_path, receipt)
        with self.assertRaises(core.ArenaError):
            self.verify()

    def test_reserved_arena_owner_refused_before_shared_probe(self):
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            if path.is_relative_to(self.root / ".cache/arena/snap"):
                self.fail("reserved shared namespace was resolved")
            return original(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve), \
                patch.object(core, "cached_snapshot", side_effect=AssertionError("reserved owner input read")), \
                self.assertRaises(core.ArenaError):
            gate.bind_owner_inputs("arena", self.subject.key, self.reference.key, [self.health.key],
                                   ["fixture-map"], "fixture")

    def test_test_source_change_during_slot_wait_refused_before_dispatch(self):
        self.write_json(Path(self.args().owner_inputs), self.inputs)
        def waited(factory, cwd, directory, timeout, **kwargs):
            self.write(self.script, b"# source changed while awaiting real Slot admission\n")
            return factory(1)
        with patch.object(transport, "_slotted_command", side_effect=waited), \
                patch.object(gate, "play", side_effect=AssertionError("games after changed test source")), \
                self.assertRaisesRegex(core.ArenaError, "test route source changed"):
            gate._cmd_gate(self.args())
        self.assertFalse((self.out / "tests.json").exists())

    def test_terminal_failed_checkpoint_refused_without_mutation_or_rerun(self):
        self.write_json(Path(self.args().owner_inputs), self.inputs)
        specs, _ = gate._owner_specs(self.inputs)
        row = games._base_row(specs[0], specs[0].map)
        row["owner_input_fingerprint"] = gate._hash(self.inputs)
        checkpoint = self.out / "games.jsonl"
        self.write(checkpoint, (json.dumps(row) + "\n").encode())
        original = checkpoint.read_bytes()
        with patch.object(transport, "_slotted_command", side_effect=AssertionError("tests before terminal refusal")), \
                patch.object(gate, "play", side_effect=AssertionError("terminal game rerun")), \
                self.assertRaisesRegex(core.ArenaError, "terminal failures"):
            gate._cmd_gate(self.args())
        self.assertEqual(checkpoint.read_bytes(), original)
        self.assertEqual(list(self.out.iterdir()), [checkpoint])


if __name__ == "__main__":
    unittest.main(verbosity=2)
