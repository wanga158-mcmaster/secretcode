"""Launcher refusal/transport tests. No decoder, game, build or native process."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr
import uuid

ROOT = Path(__file__).resolve().parents[2]
module_spec = importlib.util.spec_from_file_location("personal_camp_oracle_test", ROOT / "tools/replay/personal-camp-oracle.py")
oracle = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(oracle)


class Launcher(unittest.TestCase):
    def setUp(self):
        self.directory = ROOT / ".cache/altar-camp-oracle" / ("test-" + uuid.uuid4().hex)
        self.directory.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.directory)
        self.replay = self.directory / "fixture.zip"
        self.replay.write_bytes(b"validation fixture; no decoder will read this")
        self.map = ROOT / "maps/00lanes.map26a"
        self.scene = {"schema": "arena-personal-camp-oracle/v1",
                      "replay": {"path": self.replay.relative_to(ROOT).as_posix(),
                                 "sha256": hashlib.sha256(self.replay.read_bytes()).hexdigest()},
                      "map": {"id": "00lanes", "path": self.map.relative_to(ROOT).as_posix(),
                              "sha256": hashlib.sha256(self.map.read_bytes()).hexdigest()},
                      "actor": 5, "team": 1, "round": 159, "sample": True}
        self.file = self.directory / "scene.json"

    def write(self, scene=None):
        self.file.write_text(json.dumps(self.scene if scene is None else scene), encoding="utf-8", newline="\n")
        return hashlib.sha256(self.file.read_bytes()).hexdigest()

    def test_fixed_node_script_and_bounded_scene_are_the_only_invocation(self):
        command = oracle.invocation(self.file, self.write())
        self.assertEqual(command, [str(oracle.bridge.core.NODE), "--disable-warning=ExperimentalWarning",
                                  str(ROOT / "tools/replay/personal-camp-oracle.mjs"),
                                  "--spec", str(self.file), "--spec-sha256", self.write()])

    def test_typed_bounds_and_unknown_fields_fail(self):
        for key, value in (("actor", 16), ("actor", True), ("team", 2), ("team", False),
                           ("round", 2001), ("round", -1), ("sample", 1), ("action", "move")):
            with self.subTest(key=key, value=value):
                scene = copy.deepcopy(self.scene); scene[key] = value
                with self.assertRaises(ValueError):
                    oracle.invocation(self.file, self.write(scene))

    def test_input_and_spec_hash_changes_fail(self):
        digest = self.write()
        with self.assertRaises(ValueError):
            oracle.invocation(self.file, "0" * 64)
        self.replay.write_bytes(b"changed")
        with self.assertRaises(ValueError):
            oracle.invocation(self.file, digest)

    def test_input_paths_cannot_select_a_script_or_non_map(self):
        for name, file in (("map", ROOT / "AGENTS.md"), ("replay", ROOT / "tools/replay/personal-camp-oracle.mjs")):
            scene = copy.deepcopy(self.scene)
            scene[name]["path"] = file.relative_to(ROOT).as_posix()
            scene[name]["sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
            with self.subTest(name=name), self.assertRaises(ValueError):
                oracle.invocation(self.file, self.write(scene))

    def test_existing_bridge_owns_transport_and_oracle_owns_receipts(self):
        digest = self.write()
        with patch.object(oracle.bridge, "_run_validated", return_value=0) as run:
            self.assertEqual(oracle.main(["--spec", str(self.file), "--spec-sha256", digest]), 0)
        self.assertEqual(run.call_args.args[0], oracle.invocation(self.file, digest))
        self.assertEqual(oracle.bridge.RAW_ROOT, ROOT / ".cache/altar-camp-oracle/transport")
        self.assertFalse(oracle.bridge.RAW_ROOT.is_relative_to(ROOT / ".cache/arena"))

    def test_arbitrary_cli_or_node_flags_are_not_accepted(self):
        digest = self.write()
        with patch.object(oracle.bridge, "_run_validated") as run, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            oracle.main(["--spec", str(self.file), "--spec-sha256", digest, "--eval", "anything"])
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
