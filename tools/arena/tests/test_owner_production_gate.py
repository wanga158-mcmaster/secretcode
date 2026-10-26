"""Offline owner production gate contracts; all command execution is synthetic."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_owner_cli_gate as owner_fixture
import core
import gate
import compiled_runner as transport
import exp
import games


class ProductionGate(unittest.TestCase):
    def setUp(self):
        self.f = owner_fixture.OwnerCliGate()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        self.subject = self.snapshot("main/main", b"#define draft_mode 0\nint main(){return 9;}\n")
        self.reference = self.snapshot("main/main", (f.reference.path / "src/main.cpp").read_bytes())
        self.heal = self.snapshot("main/main", b"#define draft_mode 0\nint main(){return 9;}\n", {"draft_mode": "14"})
        self.keys = {"subject": self.subject.key, "subject-heal": self.heal.key}
        roles = {"subject": {"bot": "main/main", "overrides": {}},
                 "subject-heal": {"bot": "main/main", "overrides": {"draft_mode": "14"}}}
        for index, role in enumerate(("heal-opponent", "watchdog-opponent", "starter-opponent", "capture-opponent",
                                      "retaliation-opponent", "brawl-opponent", "hunter-opponent")):
            # The last two are independently configured versions of the same Bot.
            bot = "anchors/v40" if index >= 5 else f"anchors/v{index}"
            settings = {"mode": str(index)} if index >= 4 else {}
            entry = self.snapshot(bot, b"#define mode 0\nint main(){return 3;}\n", settings)
            self.keys[role] = entry.key
            descriptor = gate._entry(entry.key, f.owner, allow_overrides=True)[1]
            roles[role] = {"bot": bot, "overrides": settings, "expected_key": entry.key, "sources": descriptor["sources"]}
        self.plan = {"roles": roles, "games": [
            {"id": f"fixture:heal:{i}", "fixture": "heal", "index": i, "subject_role": "subject-heal",
             "opponent_role": "heal-opponent", "map": "fixture-map", "declared_map": "fixture-map", "side": "AB"[i % 2]}
            for i in range(15)]}
        self.inventory = [f"cpp:case-{i}" for i in range(3)] + [f"python:case-{i}" for i in range(37)] + [f"fixture:case-{i}" for i in range(6)]
        f.route.update(version=2, bots=("main/main",), inventory=self.inventory, fixtures=self.plan, timeout_seconds=33600)
        self.module = SimpleNamespace(runtime_inputs=lambda: {"environment": {"TEMP": "synthetic"}},
                                      run_route=self.run_route,
                                      verify_result=lambda inputs, out: json.loads((out / "tests.json").read_bytes()))
        f.stack.enter_context(patch.object(gate, "_route_module", return_value=self.module))
        for name in ("released_proof.py", "owner_main_tests.py", "wasi.py", "wasi-run.mjs", "exp.py", "stats.py", "sweep.py",
                     "panel_executables.py", "population.py"):
            f.write(f.root / "tools/arena" / name, ("# synthetic " + name).encode())
        self.proof = {"path": ".cache/owner/proof.json", "size": 2, "sha256": "pinned-synthetic-proof"}
        self.trace = []
        self.inputs = self.bind()
        f.write_json(f.out / "inputs.json", self.inputs)

    def snapshot(self, bot_id, source, settings=None):
        f = self.f
        suite, name = bot_id.split("/")
        bot, settings = core.Bot(suite, name), settings or {}
        contents = {f.root / "bots" / suite / name / "src/main.cpp": source}
        if settings:
            contents = core.apply_overrides(bot, contents, settings)
        version = core.version_of(contents, settings)
        snap = f.snap_root / version
        target = snap / "bots" / suite / name
        f.write(target / "src/main.cpp", next(iter(contents.values())))
        f.write_json(snap / "meta.json", {"bot": bot_id, "version": version, "overrides": settings, "files": 1})
        f.write(snap / ".ready", b"ready")
        f.write(snap / ".toolchain", b"fixture-toolchain")
        for filename, payload in (("bot.wasm", b"raw"), ("bot-injected.wasm", b"sdk")):
            f.write(target / ".cambc/build-release" / filename, b"\0asm\x01\0\0\0" + payload)
        return core.Entrant(bot, version, target, True, overrides=tuple(sorted(settings.items())))

    def bind(self, **options):
        values = {"fixture_roles": self.keys, "released_proof": self.proof}
        values.update(options)
        return gate.bind_owner_inputs(self.f.owner, self.subject.key, self.reference.key, [self.f.health.key],
                                      ["fixture-map"], "fixture", **values)

    def run_route(self, inputs, out, admission):
        self.assertEqual(admission.slots.count, 1)
        self.assertEqual(len(admission.slots.fds), 1)
        admission.verify()
        self.trace.append("route")
        result = {"schema": gate.OWNER_TEST_V2, "owner": self.f.owner, "input_fingerprint": gate._hash(inputs),
                  "passed": True, "tests": [{"name": name, "passed": True} for name in self.inventory]}
        self.f.write_json(out / "tests.json", result)
        return result

    def test_same_logical_subject_reference_and_fixture_configurations_remain_separate(self):
        self.assertEqual(self.inputs["schema"], gate.OWNER_INPUT_V2)
        self.assertNotEqual(self.subject.key, self.reference.key)
        self.assertEqual(self.subject.bot, self.reference.bot)
        self.assertEqual(gate.validate_owner_inputs(self.inputs), self.inputs)
        specs, entrants = gate._owner_specs(self.inputs)
        self.assertEqual(len(entrants), 3)
        self.assertEqual(len(specs), 4)
        self.assertNotIn(self.heal.key, entrants)
        self.assertNotEqual(self.keys["brawl-opponent"], self.keys["hunter-opponent"])

    def test_partial_fields_and_schema_downgrades_fail_closed(self):
        cases = []
        for name in ("fixtures", "released_proof"):
            value = copy.deepcopy(self.inputs)
            value.pop(name)
            cases.append(value)
        value = copy.deepcopy(self.inputs)
        value["fixtures"]["roles"].pop("subject-heal")
        cases.append(value)
        value = copy.deepcopy(self.inputs)
        value["schema"] = gate.OWNER_INPUT_SCHEMA
        cases.append(value)
        value = copy.deepcopy(self.inputs)
        value.pop("fixtures"); value.pop("released_proof"); value["schema"] = gate.OWNER_INPUT_SCHEMA
        cases.append(value)
        for index, value in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(core.ArenaError):
                gate.validate_owner_inputs(value)

    def test_reference_score_uses_exact_role_key_when_health_has_the_same_logical_bot(self):
        other = self.snapshot("main/main", b"int main(){return 77;}\n")
        inputs = gate.bind_owner_inputs(self.f.owner, self.subject.key, self.reference.key, [other.key],
                                       ["fixture-map"], "fixture", fixture_roles=self.keys, released_proof=self.proof)
        specs, _ = gate._owner_specs(inputs)
        rows = []
        for spec in specs:
            subject_side = spec.tags["subject"]
            winner = subject_side if spec.tags["opponent_key"] == self.reference.key else ("B" if subject_side == "A" else "A")
            rows.append({"map": spec.map, "a": spec.a.key, "b": spec.b.key, "tags": spec.tags, "winner": winner,
                         "rounds": 100, "A": games._zero() | {"clears": 1, "fuel_max": 100},
                         "B": games._zero() | {"clears": 1, "fuel_max": 100}, "replay_health_complete": True})
        with patch.object(gate, "_test_evidence", return_value=gate.check("tests", "pass", "synthetic")):
            checks = gate._owner_checks(inputs, rows, {}, self.f.out, .75)
        self.assertEqual(checks[-1]["status"], "pass")
        self.assertIn("2-0-0 score 1.000", checks[-1]["note"])

    def test_fixture_identity_alias_or_configuration_cannot_replace_an_ordinary_entrant(self):
        for role, key in (("subject", self.heal.key), ("subject-heal", self.subject.key),
                          ("hunter-opponent", self.keys["brawl-opponent"])):
            with self.subTest(role=role), self.assertRaises(core.ArenaError):
                self.bind(fixture_roles=self.keys | {role: key})
        with self.assertRaisesRegex(core.ArenaError, "overrides"):
            gate.bind_owner_inputs(self.f.owner, self.heal.key, self.reference.key, [self.f.health.key],
                                   ["fixture-map"], "fixture", fixture_roles=self.keys, released_proof=self.proof)

    def test_nonexact_inventory_or_source_drift_cannot_activate_registration(self):
        for inventory in (self.inventory[:-1], self.inventory[:-1] + [self.inventory[0]], self.inventory + ["extra"]):
            with self.subTest(count=len(inventory)), patch.dict(self.f.route, inventory=inventory), self.assertRaises(core.ArenaError):
                self.bind()
        for path in (self.f.script, self.f.root / "tools/arena/released_proof.py"):
            original = path.read_bytes()
            path.write_bytes(original + b"changed")
            with self.assertRaises(core.ArenaError):
                gate.validate_owner_inputs(self.inputs)
            path.write_bytes(original)

    def test_actual_parent_admission_and_locks_wrap_route_then_cleanup(self):
        trace = self.trace
        class Slots:
            def __init__(self, *args, **kwargs): self.count, self.fds = 0, []
            def __enter__(self): trace.append("slot+"); self.count, self.fds = 1, [1]; return self
            def __exit__(self, *args): trace.append("slot-"); self.count, self.fds = 0, []
        class Lock:
            def __init__(self, path): self.path = path
            def __enter__(self): trace.append("lock+"); return self
            def __exit__(self, *args): trace.append("lock-")
        with patch.object(core, "Slots", Slots), patch.object(core, "FileLock", Lock):
            receipt = gate._run_production_tests(self.inputs, self.f.out)
        self.assertEqual(trace[0], "slot+")
        self.assertLess(trace.index("lock+"), trace.index("route"))
        self.assertGreater(trace.index("lock-"), trace.index("route"))
        self.assertEqual(trace[-1], "slot-")
        self.assertEqual(gate._test_evidence(receipt, self.inputs, self.f.out)["status"], "pass")
        result_path = self.f.root / receipt["result"]["path"]
        result = json.loads(result_path.read_bytes())
        for tests in (result["tests"][:-1], result["tests"][:-1] + [result["tests"][0]],
                      result["tests"] + [{"name": "extra", "passed": True}]):
            self.f.write_json(result_path, result | {"tests": tests})
            changed = receipt | {"result": gate._file(result_path)}
            with self.assertRaisesRegex(core.ArenaError, "inventory"):
                gate._test_evidence(changed, self.inputs, self.f.out)

    def test_serialized_admission_or_unsupported_command_never_dispatches(self):
        admission = gate.OwnerAdmission(self.inputs, self.f.out, SimpleNamespace(count=1, fds=[1]))
        with self.assertRaisesRegex(core.ArenaError, "actual"):
            admission.verify()
        with patch("compiled_runner._command", side_effect=AssertionError("must not dispatch")):
            with self.assertRaises(core.ArenaError):
                admission.command("online", "cpp:case-0", [str(core.CAMBC), "submit"], self.f.root,
                                  self.f.out / "bad", 10, env={"TEMP": "synthetic"})

    def test_unconfirmed_child_cancellation_retains_actual_slot_and_version_locks_until_empty(self):
        trace = self.trace
        class Slots:
            def __init__(self, *args, **kwargs): self.count, self.fds = 0, []
            def __enter__(self): trace.append("slot+"); self.count, self.fds = 1, [1]; return self
            def __exit__(self, *args): trace.append("slot-"); self.count, self.fds = 0, []
        class Lock:
            def __init__(self, path): pass
            def __enter__(self): trace.append("lock+"); return self
            def __exit__(self, *args): trace.append("lock-")
        class Process:
            empty_confirmed, assigned, attempts = False, True, 0
            def terminate_tree(process):
                self.assertNotIn("slot-", trace)
                self.assertNotIn("lock-", trace)
                process.attempts += 1
                trace.append("terminate")
                if process.attempts == 1:
                    raise KeyboardInterrupt("second cancellation during owned cleanup")
                process.empty_confirmed = True
            def close(process): trace.append("process-close")
            def record(process): return {"empty_confirmed": process.empty_confirmed}
        process = Process()
        def leaf(inputs, out):
            return [{"kind": "unit-compile", "case_id": self.inventory[0], "argv": [str(core.CLANG), "-o", str(out / "unit.wasm")],
                     "cwd": str(self.f.root), "directory": str(out / "commands/00"), "timeout": 30, "env": {"TEMP": "synthetic"}}]
        self.module.native_plan = leaf
        def run(inputs, out, admission):
            row = leaf(inputs, out)[0]
            directory = Path(row["directory"]); directory.mkdir(parents=True)
            return admission.command(row["kind"], row["case_id"], row["argv"], Path(row["cwd"]), directory,
                                     row["timeout"], env=row["env"])
        self.module.run_route = run
        error = KeyboardInterrupt("first cancellation with live owned child")
        error.compiled_process = process
        with patch.object(core, "Slots", Slots), patch.object(core, "FileLock", Lock), \
                patch.object(transport, "_PENDING_CLEANUP", []), \
                patch.object(transport, "_command", side_effect=error) as command, \
                patch.object(exp.time, "sleep"), self.assertRaises(KeyboardInterrupt):
            gate._run_production_tests(self.inputs, self.f.out)
        command.assert_called_once()
        self.assertTrue(process.empty_confirmed)
        self.assertEqual(trace.count("slot-"), 1)
        self.assertLess(trace.index("process-close"), trace.index("lock-"))
        self.assertEqual(trace[-1], "slot-")

    def test_typed_leaf_checks_exact_arguments_environment_and_duplicate_identity_before_dispatch(self):
        class Slots:
            count, fds = 1, [1]
        plan = {"kind": "unit-compile", "case_id": self.inventory[0], "argv": [str(core.CLANG), "-o", str(self.f.out / "unit.wasm")],
                "cwd": str(self.f.root), "directory": str(self.f.out / "commands/00"), "timeout": 30, "env": {"TEMP": "synthetic"}}
        self.module.native_plan = lambda inputs, out: [plan]
        with patch.object(core, "Slots", Slots):
            admission = gate.OwnerAdmission(self.inputs, self.f.out, Slots())
            with patch.object(transport, "_command", side_effect=AssertionError("must not dispatch")) as command:
                for changed in (plan | {"argv": plan["argv"] + ["--injected"]}, plan | {"env": {"TEMP": "foreign"}},
                                plan | {"timeout": 31}, plan | {"directory": str(self.f.out / "other")}):
                    with self.subTest(changed=changed), self.assertRaises(core.ArenaError):
                        admission.command(changed["kind"], changed["case_id"], changed["argv"], Path(changed["cwd"]),
                                          Path(changed["directory"]), changed["timeout"], env=changed["env"])
                admission.dispatched.add((plan["kind"], plan["case_id"]))
                with self.assertRaises(core.ArenaError):
                    admission.command(plan["kind"], plan["case_id"], plan["argv"], Path(plan["cwd"]), Path(plan["directory"]),
                                      plan["timeout"], env=plan["env"])
                command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
