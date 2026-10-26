"""Offline fixed-route contract tests; synthetic game files never qualify a gate."""
from pathlib import Path
from types import SimpleNamespace
import copy
import importlib.util
import io
import json
import shutil
import sys
import unittest
from unittest.mock import patch
import uuid
import zipfile

ROOT = next(path for path in Path(__file__).resolve().parents if (path / "arena.cmd").is_file())
sys.path.insert(0, str(ROOT / "tools/arena"))
HELPER = Path(__file__).resolve().parents[1] / "owner_main_tests.py"
SPEC = importlib.util.spec_from_file_location("owner_main_tests", HELPER)
route = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(route)


class MainRoute(unittest.TestCase):
    def setUp(self):
        self.home = ROOT / ".cache/review-release-route" / ("tests-" + uuid.uuid4().hex)
        self.home.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.home)

    def inputs(self):
        return {"owner": "main", "subject": "main/main@000000000000", "fixtures": route.fixture_contract(),
                "tests": {"unit_runtime": {"compiler": "fixed-clang", "node": "fixed-node", "driver": "fixed-driver",
                                           "flags": ["fixed-flag"], "eh": "fixed-eh", "environment": {"SystemRoot": "fixed"}}}}

    def test_exact_inventory_and_configured_roles(self):
        self.assertEqual(len(route.expected_cases()), 46)
        self.assertEqual(len(set(route.expected_cases())), 46)
        contract = route.fixture_contract()
        self.assertEqual(len(contract["roles"]), 9)
        self.assertEqual(len(contract["games"]), 15)
        self.assertEqual(sum(len(names) for names in route.PYTHON_CASES.values()), 37)
        self.assertEqual(contract["roles"]["subject-heal"]["overrides"], {"draft_mode": "14"})
        self.assertEqual(sum("expected_key" in row and bool(row["sources"]) for row in contract["roles"].values()), 7)
        contract["roles"]["subject-heal"]["overrides"]["draft_mode"] = "0"
        self.assertEqual(route.fixture_contract()["roles"]["subject-heal"]["overrides"], {"draft_mode": "14"})

    def fake_unit_root(self):
        root = self.home / "fake-root"
        for _, relative in route.CPP_CASES:
            source = root / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            header = "retaliation-reserve.h" if "retaliation" in source.name else "model.h"
            source.write_text('#include "' + header + '"\nint main() { return 0; }\n', encoding="utf-8")
        return root

    def test_unit_guard_allows_prepared_headers_without_working_policy_pins(self):
        root = self.fake_unit_root()
        self.assertNotIn("bots/main/main/src/model.h", route.SOURCE_PATHS)
        self.assertNotIn("bots/main/main/src/retaliation-reserve.h", route.SOURCE_PATHS)
        with patch.object(route, "ROOT", root):
            self.assertEqual(set(route._unit_source_guard()), {root / relative for _, relative in route.CPP_CASES})

    def test_new_fourth_cpp_fails_binding_before_any_runtime_hashing(self):
        root = self.fake_unit_root()
        (root / "bots/main/main/tests/new-fourth.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        with patch.object(route, "ROOT", root), \
             patch.object(route, "_file", side_effect=AssertionError("runtime file touched")) as runtime_file:
            with self.assertRaisesRegex(route.core.ArenaError, "C\\+\\+ unit inventory changed"):
                route.runtime_inputs()
            runtime_file.assert_not_called()

    def test_sibling_model_header_shadow_fails_binding_without_canonical_writes(self):
        root = self.fake_unit_root()
        shadow = root / "bots/main/main/tests/model.h"
        shadow.write_text("// This sibling would precede the prepared include directory.\n", encoding="utf-8")
        with patch.object(route, "ROOT", root), \
             patch.object(route, "_file", side_effect=AssertionError("runtime file touched")) as runtime_file:
            with self.assertRaisesRegex(route.core.ArenaError, "unreviewed quoted unit dependency"):
                route.runtime_inputs()
            runtime_file.assert_not_called()

    def test_missing_or_changed_game_rejected_before_snapshot_access(self):
        for change in (lambda data: data["fixtures"]["games"].pop(),
                       lambda data: data["fixtures"]["games"][0].update(side="B")):
            inputs = self.inputs()
            change(inputs)
            with patch.object(route.core, "cached_snapshot", side_effect=AssertionError("source touched")) as read:
                with self.assertRaisesRegex(route.core.ArenaError, "matrix changed"):
                    route._roles(inputs)
                read.assert_not_called()

    def test_self_asserted_json_admission_rejected_before_verification(self):
        with self.assertRaises((AttributeError, route.core.ArenaError)):
            route.run_route(self.inputs(), self.home, {"slots": {"count": 1}})

    def test_missing_python_case_cannot_be_replaced_by_a_smaller_passing_suite(self):
        with patch.object(route, "PYTHON_CASES", {"main-online-test.py": ("KeeperTests.test_pause_drains_existing_groups_without_creating",)}):
            with self.assertRaisesRegex(route.core.ArenaError, "inventory changed"):
                route._python_tests(self.home)

    def test_process_receipt_requires_actual_environment_and_empty_job(self):
        directory = self.home / "native-receipt"
        directory.mkdir()
        leaf = {"kind": "fixture-game", "case_id": "fixture:heal:0", "argv": ["fixed-cli", "run-batch"],
                "cwd": str(ROOT), "directory": str(directory), "timeout": 1920, "env": {"TEMP": str(self.home)}}
        record = {"returncode": 0, "timed_out": False, "tree_exit_confirmed": True, "forced_termination": False,
                  "containment": {"kind": "windows-private-job", "assigned_before_resume": True, "resumed": True,
                                  "empty_confirmed": True, "kill_on_close": True, "forced_termination": False}}
        dispatch = {"argv": list(leaf["argv"]), "cwd": leaf["cwd"], "timeout": leaf["timeout"], "environment": dict(leaf["env"])}
        for name in ("stdout.bin", "stderr.bin"):
            (directory / name).write_bytes(b"")
        (directory / "dispatch.json").write_text(json.dumps(dispatch))
        (directory / "process-end.json").write_text(json.dumps(record))
        receipt = route._validate_process(leaf, record, self.home)
        self.assertEqual(receipt["env"], leaf["env"])
        for mutation in (lambda current: current.update(tree_exit_confirmed=False),
                         lambda current: current["containment"].update(empty_confirmed=False)):
            changed = copy.deepcopy(record)
            mutation(changed)
            (directory / "process-end.json").write_text(json.dumps(changed))
            with self.assertRaises(route.core.ArenaError):
                route._validate_process(leaf, changed, self.home)
        (directory / "process-end.json").write_text(json.dumps(record))
        dispatch["environment"]["TEMP"] = "other-owner"
        (directory / "dispatch.json").write_text(json.dumps(dispatch))
        with self.assertRaisesRegex(route.core.ArenaError, "dispatch evidence differs"):
            route._validate_process(leaf, record, self.home)

    def test_native_plan_has_exact_21_leaves_and_no_online_or_preparation_commands(self):
        out = self.home
        roles, fixture_output = self.fixture_data()
        with patch.object(route, "_owner_path", side_effect=lambda path, owner: path), \
             patch.object(route, "_roles", return_value=roles):
            plan = route.native_plan(self.inputs(), out)
        self.assertEqual(len(plan), 21)
        self.assertEqual([row["kind"] for row in plan].count("unit-compile"), 3)
        self.assertEqual([row["kind"] for row in plan].count("unit-run"), 3)
        self.assertEqual([row["kind"] for row in plan].count("fixture-game"), 15)
        self.assertTrue(all(row["argv"][1] == "run-batch" and row["argv"][-1] == "1800"
                            for row in plan if row["kind"] == "fixture-game"))
        self.assertTrue(all(row["timeout"] in (600, 1920) for row in plan))
        # Orchestration only: synthetic leaf outputs are not native evidence and
        # never enter the production verifier. Real admission/Job checks have
        # separate tests; this ensures every leaf uses the parent's exact verb.
        parent = SimpleNamespace(slots=route.core.Slots(1), checks=0, commands=[])
        parent.slots.count, parent.slots.fds = 1, ["synthetic entered slot"]
        def verify():
            parent.checks += 1
        def command(kind, case_id, argv, cwd, directory, timeout, *, env):
            verify()
            request = {"kind": kind, "case_id": case_id, "argv": argv, "cwd": str(cwd),
                       "directory": str(directory), "timeout": timeout, "env": env}
            self.assertEqual(request, plan[len(parent.commands)])
            parent.commands.append(request)
            if kind == "fixture-game":
                fixture_output(kind, case_id)
            verify()
            return {"synthetic": True}
        parent.verify, parent.command = verify, command
        inputs = self.inputs()
        with patch.object(route, "_owner_path", side_effect=lambda path, owner: path), \
             patch.object(route, "_roles", return_value=roles), \
             patch.object(route, "runtime_inputs", return_value=inputs["tests"]["unit_runtime"]), \
             patch.object(route, "_validate_process", side_effect=lambda leaf, record, output: dict(leaf)), \
             patch.object(route, "verify_result", side_effect=lambda value, output: json.loads((output / "tests.json").read_bytes())):
            result = route.run_route(inputs, out, parent)
        self.assertEqual(parent.commands, plan)
        self.assertEqual(parent.checks, 2 + 2 * 21)  # Route boundaries plus parent's before/after checks.
        self.assertEqual(len(result["fixture_games"]), 15)
        self.assertEqual(len(result["tests"]), 46)

    def test_all37_original_python_cases_execute_offline_in_owned_scratch(self):
        with patch.object(route.core, "prepare", side_effect=AssertionError("unexpected prepare")):
            rows = route._python_tests(self.home)
        self.assertEqual(len(rows), 37)
        self.assertTrue(all(row["passed"] for row in rows))
        self.assertEqual({row["name"] for row in rows}, {name for name in route.expected_cases() if name.startswith("python:")})

    def test_python_liveness_root_is_scoped_and_restored_despite_unrelated_canonical_statistics(self):
        expected_root = self.home / "python"
        canonical_statistics = ROOT / "games/present.json"
        original_is_file, original_load = Path.is_file, route._load
        loaded, probed_roots, canonical_probes = [], [], []

        def is_file(path):
            if path == canonical_statistics:
                canonical_probes.append(path)
                return True  # Simulate unrelated workspace data without creating or reading it.
            return original_is_file(path)

        def load(name, path):
            module = original_load(name, path)
            if Path(path).name == "main-liveness-test.py":
                dependency = module.liveness
                original_statistics_path = dependency.statistics_path
                loaded.append((module, dependency, module.ROOT, dependency.ROOT, original_statistics_path))
                def statistics_path(row, run):
                    probed_roots.append(dependency.ROOT)
                    return original_statistics_path(row, run)
                dependency.statistics_path = statistics_path
            return module

        try:
            with patch.object(Path, "is_file", new=is_file), patch.object(route, "_load", side_effect=load):
                rows = route._python_tests(self.home)
            self.assertEqual(len(rows), 37)
            self.assertEqual(len(loaded), 1)
            self.assertTrue(probed_roots)
            self.assertEqual(set(probed_roots), {expected_root})
            self.assertEqual(canonical_probes, [])
            module, dependency, test_root, dependency_root, _ = loaded[0]
            self.assertEqual((module.ROOT, dependency.ROOT), (test_root, dependency_root))
        finally:
            for _, dependency, _, _, original in loaded:
                dependency.statistics_path = original

    @staticmethod
    def statistics(side):
        team = "teamA" if side == "A" else "teamB"
        champion = {"kind": "champion", "id": 0, "team": team}
        def timeline(owner, metric, value):
            return {"owner": owner, "metric": metric, "sampleCount": 601,
                    "runs": [{"duration": 599, "value": "0"}, {"duration": 2, "value": str(value)}]}
        return {"match": {"roundsCompleted": 600, "roundsStarted": 600, "finalRoundPartial": False, "outcome": team},
                "diagnostics": {"botFaults": {"teamA": 0, "teamB": 0}}, "events": [],
                "roster": {"champions": [{"id": 0, "team": team}]},
                "championTimelines": [timeline(champion, name, 1 if name == "earnedGold" else 0) for name in
                    ("earnedGold", "spentGold", "saleRefunds", "collectedGems", "submittedGems", "kills", "deaths", "assists",
                     "monsterDamageDone", "monsterDamageTaken", "championDamageDone", "championDamageTaken", "healingDone", "healingReceived", "shieldingDone")],
                "teamTimelines": [timeline(team, "submittedGems", 1000)]}

    def fixture_data(self):
        roles = {}
        for name in route.ROLE_CONTRACT:
            path = self.home / "projects" / name
            build = path / ".cambc/build-release"
            build.mkdir(parents=True)
            for filename in ("bot.wasm", "bot-injected.wasm"):
                (build / filename).write_bytes(b"\0asm\x01\0\0\0")
            roles[name] = SimpleNamespace(path=path)
        def execute(kind, case):
            self.assertEqual(kind, "fixture-game")
            game = next(row for row in route.GAME_CONTRACT if row["id"] == case)
            _, replay, stats = route._game_paths(self.home, game)
            with zipfile.ZipFile(replay, "w") as archive:
                archive.writestr("team-" + game["side"].lower() + ".stdout", "t r500 k0 data idle1 await0\n")
            stats.write_text(json.dumps(self.statistics(game["side"])), encoding="utf-8")
        return roles, execute

    def test_six_original_fixture_mains_keep15_games_and_readonly_verifier(self):
        roles, execute = self.fixture_data()
        with route.redirect_stdout(io.StringIO()):
            rows, games = route._fixture_mains(self.inputs(), self.home, roles, execute)
            before = {str(path): path.read_bytes() for path in self.home.rglob("*") if path.is_file()}
            verified, retained_games = route._fixture_mains(self.inputs(), self.home, roles)
        after = {str(path): path.read_bytes() for path in self.home.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        self.assertEqual(rows, verified)
        self.assertTrue(all(row["passed"] for row in rows))
        self.assertEqual(len(rows), 6)
        self.assertEqual(len(games), 15)
        self.assertEqual(games, retained_games)
        liveness = json.loads((self.home / "fixtures/liveness/liveness-fixture.json").read_bytes())
        self.assertEqual(len(liveness["binaries"]), 5)
        self.assertTrue(all(set(row["binaries"]) == {"A", "B"} and all(set(value["wasm_sha256"]) == {"bot.wasm", "bot-injected.wasm"}
                            for value in row["binaries"].values()) for row in liveness["binaries"]))

    def test_original_heal_predicate_still_fails_on_logged_heal_error(self):
        roles, execute = self.fixture_data()
        with route.redirect_stdout(io.StringIO()):
            route._fixture_mains(self.inputs(), self.home, roles, execute)
        game = route.GAME_CONTRACT[0]
        _, replay, _ = route._game_paths(self.home, game)
        with zipfile.ZipFile(replay, "w") as archive:
            archive.writestr("team-a.stdout", "heal failed: stale visible target\n")
        with route.redirect_stdout(io.StringIO()):
            rows, _ = route._fixture_mains(self.inputs(), self.home, roles)
        self.assertFalse(next(row["passed"] for row in rows if row["name"] == "fixture:heal"))


if __name__ == "__main__":
    unittest.main()
