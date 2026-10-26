"""Offline prospective panel-byte audits. No builds, games, native queries or assessments."""
from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import panel_executables as audit
from core import ArenaError, Bot, Entrant


class PanelExecutables(unittest.TestCase):
    def setUp(self):
        self.home = Path(tempfile.mkdtemp(prefix="panel-executable-test-", dir=core.ROOT / ".cache"))
        self.panel_path = self.home / "panel.json"
        self.manifest_path = self.home / "executables.json"
        self.snapshots = []
        self.entrants = {}
        for ident in ("odd/one", "anchors/frozen"):
            version = hashlib.sha256((str(self.home)+ident).encode()).hexdigest()[:12]
            snapshot = core.CACHE / "snap" / version
            self.assertFalse(snapshot.exists())
            suite, name = ident.split("/")
            directory = snapshot / "bots" / suite / name
            build = directory / ".cambc" / "build-release"
            build.mkdir(parents=True)
            self.snapshots.append(snapshot)
            (build / "bot.wasm").write_bytes(b"raw fixture " + ident.encode())
            (build / "bot-injected.wasm").write_bytes(b"injected fixture " + ident.encode())
            entrant = Entrant(Bot(suite, name), version, directory, True)
            self.entrants[entrant.key] = entrant
        self.panel = {"schema": 1, "kind": "population-panel",
                      "entries": [{"snapshot": key} for key in self.entrants]}
        self.write(self.panel_path, self.panel)
        self.validator_patch = patch.object(audit.population, "validate_panel", side_effect=self.validated)
        self.validator = self.validator_patch.start()
        self.runner_patch = patch.object(audit.population, "regress", side_effect=AssertionError("never dispatch games"))
        self.runner = self.runner_patch.start()
        self.addCleanup(self.runner_patch.stop)
        self.addCleanup(self.validator_patch.stop)
        self.addCleanup(self.remove_fixtures)

    def remove_fixtures(self):
        for path in self.snapshots:
            resolved = path.resolve()
            self.assertTrue(resolved.is_relative_to((core.CACHE / "snap").resolve()))
            self.assertEqual(resolved, path)
            shutil.rmtree(resolved)
        self.assertTrue(self.home.resolve().is_relative_to((core.ROOT / ".cache").resolve()))
        shutil.rmtree(self.home)

    def validated(self, panel):
        return [self.entrants[entry["snapshot"]] for entry in panel["entries"]]

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8", newline="\n")

    def pinned(self):
        return audit.pin(self.panel_path, self.manifest_path)

    def mutate(self, manifest, name="mutation.json"):
        path = self.home / name
        self.write(path, manifest)
        return path

    def test_pin_validate_exact_unique_snapshots_and_canonical_paths(self):
        self.panel["entries"].append(copy.deepcopy(self.panel["entries"][0]))
        self.write(self.panel_path, self.panel)
        original_panel = self.panel_path.read_bytes()
        manifest = self.pinned()
        self.assertEqual(audit.validate(self.manifest_path), manifest)
        self.assertEqual(len(manifest["snapshots"]), 2)
        self.assertEqual(manifest["panel"]["sha256"], hashlib.sha256(original_panel).hexdigest())
        self.assertEqual(manifest["producer"]["sha256"], hashlib.sha256(audit.PRODUCER.read_bytes()).hexdigest())
        for binding in manifest["snapshots"]:
            entrant = self.entrants[binding["snapshot"]]
            for label, filename in audit.FILES.items():
                path = entrant.path / ".cambc/build-release" / filename
                self.assertEqual(binding[label], {"path": path.relative_to(core.ROOT).as_posix(),
                                                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        self.assertEqual(self.panel_path.read_bytes(), original_panel)
        self.assertEqual(self.validator.call_count, 2)
        self.runner.assert_not_called()

    def test_pin_refuses_overwrite_before_reading_a_changed_panel(self):
        self.pinned()
        original = self.manifest_path.read_bytes()
        calls = self.validator.call_count
        self.panel_path.write_bytes(b"now invalid")
        with self.assertRaisesRegex(ArenaError, "refuse overwrite"):
            self.pinned()
        self.assertEqual(self.manifest_path.read_bytes(), original)
        self.assertEqual(self.validator.call_count, calls)

    def test_exclusive_creation_preserves_concurrent_output(self):
        def appeared(panel):
            self.manifest_path.write_bytes(b"another writer's output")
            return self.validated(panel)
        self.validator.side_effect = appeared
        with self.assertRaises(FileExistsError):
            self.pinned()
        self.assertEqual(self.manifest_path.read_bytes(), b"another writer's output")

    def test_each_binary_drift_is_rejected_without_any_dispatch(self):
        manifest = self.pinned()
        for label in audit.FILES:
            path = core.ROOT / manifest["snapshots"][0][label]["path"]
            original = path.read_bytes()
            path.write_bytes(bytes([original[0] ^ 1])+original[1:])  # same-size executable mutation
            with self.subTest(label=label), self.assertRaisesRegex(ArenaError, "bytes changed"):
                audit.validate(self.manifest_path)
            path.write_bytes(original)
        self.runner.assert_not_called()

    def test_missing_binary_never_rebuilds_or_substitutes(self):
        manifest = self.pinned()
        path = core.ROOT / manifest["snapshots"][0]["injected"]["path"]
        path.unlink()
        with self.assertRaisesRegex(ArenaError, "missing.*executable"):
            audit.validate(self.manifest_path)
        with self.assertRaisesRegex(ArenaError, "missing.*executable"):
            audit.pin(self.panel_path, self.home / "another.json")
        self.assertFalse((self.home / "another.json").exists())
        self.runner.assert_not_called()

    def test_panel_formatting_drift_is_not_silently_rebound(self):
        self.pinned()
        self.panel_path.write_bytes(self.panel_path.read_bytes()+b" ")
        calls = self.validator.call_count
        with self.assertRaisesRegex(ArenaError, "panel bytes changed"):
            audit.validate(self.manifest_path)
        self.assertEqual(self.validator.call_count, calls)

    def test_missing_duplicate_extra_and_malformed_bindings(self):
        manifest = self.pinned()
        changes = []
        a = copy.deepcopy(manifest); a["snapshots"].pop(); changes.append(a)
        a = copy.deepcopy(manifest); a["snapshots"].append(copy.deepcopy(a["snapshots"][0])); changes.append(a)
        a = copy.deepcopy(manifest); a["snapshots"][0]["snapshot"] = "odd/extra@000000000000"; changes.append(a)
        a = copy.deepcopy(manifest); a["snapshots"] = {}; changes.append(a)
        a = copy.deepcopy(manifest); a["snapshots"][0]["unexpected"] = True; changes.append(a)
        for index, changed in enumerate(changes):
            with self.subTest(index=index), self.assertRaises(ArenaError):
                audit.validate(self.mutate(changed, f"binding-{index}.json"))

    def test_noncanonical_binary_alias_rejected_even_with_matching_bytes(self):
        manifest = self.pinned()
        binding = manifest["snapshots"][0]
        alternate = self.home / "bot.wasm"
        shutil.copyfile(core.ROOT / binding["raw"]["path"], alternate)
        binding["raw"]["path"] = alternate.relative_to(core.ROOT).as_posix()
        with self.assertRaisesRegex(ArenaError, "noncanonical raw"):
            audit.validate(self.mutate(manifest))

    def test_noncanonical_descriptor_spellings_and_hashes_rejected(self):
        manifest = self.pinned()
        for value in ("./"+manifest["panel"]["path"], "../outside.json", "/outside.json",
                      manifest["panel"]["path"].replace("/", "\\")):
            changed = copy.deepcopy(manifest); changed["panel"]["path"] = value
            with self.subTest(path=value), self.assertRaises(ArenaError):
                audit.validate(self.mutate(changed))
        for value in (None, "hash", "A"*64):
            changed = copy.deepcopy(manifest); changed["snapshots"][0]["raw"]["sha256"] = value
            with self.subTest(hash=value), self.assertRaises(ArenaError):
                audit.validate(self.mutate(changed))

    def test_changed_or_relocated_producer_rejected(self):
        manifest = self.pinned()
        changed = copy.deepcopy(manifest); changed["producer"]["sha256"] = "0"*64
        with self.assertRaisesRegex(ArenaError, "producer changed"):
            audit.validate(self.mutate(changed))
        fake = self.home / "producer.py"
        fake.write_bytes(audit.PRODUCER.read_bytes())
        changed = copy.deepcopy(manifest); changed["producer"]["path"] = fake.relative_to(core.ROOT).as_posix()
        with self.assertRaisesRegex(ArenaError, "producer changed"):
            audit.validate(self.mutate(changed))

    def test_upstream_validation_failure_blocks_pin_and_validate(self):
        self.pinned()
        self.validator.side_effect = ArenaError("source/maps/overrides gate failed")
        with self.assertRaisesRegex(ArenaError, "source/maps/overrides"):
            audit.validate(self.manifest_path)
        other = self.home / "other.json"
        with self.assertRaisesRegex(ArenaError, "source/maps/overrides"):
            audit.pin(self.panel_path, other)
        self.assertFalse(other.exists())

    def test_upstream_entrant_cannot_redirect_canonical_cache_path(self):
        entrant = next(iter(self.entrants.values()))
        entrant.path = self.home / "alias"
        with self.assertRaisesRegex(ArenaError, "canonical snapshot"):
            self.pinned()

    def test_malformed_json_schema_duplicate_fields_and_nonfinite(self):
        manifest = self.pinned()
        cases = [b'[]', b'{"schema":1,"schema":2}', b'{"x":NaN}', b'{']
        for index, raw in enumerate(cases):
            path = self.home / f"malformed-{index}.json"
            path.write_bytes(raw)
            with self.subTest(index=index), self.assertRaises(ArenaError):
                audit.validate(path)
        for key, value in (("schema", "old"), ("scope", "guaranteed consumed bytes"), ("extra", 1)):
            changed = copy.deepcopy(manifest); changed[key] = value
            with self.subTest(key=key), self.assertRaises(ArenaError):
                audit.validate(self.mutate(changed))

    def test_cli_pin_validate_and_no_regress_command(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(audit.main(["pin", str(self.panel_path), "--out", str(self.manifest_path)]), 0)
        self.assertFalse(json.loads(output.getvalue())["consumption_attested"])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(audit.main(["validate", str(self.manifest_path)]), 0)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(audit.main(["validate", str(self.home / "missing.json")]), 1)
            with self.assertRaises(SystemExit) as caught:
                audit.main(["regress", "anchors/frozen", "--manifest", str(self.manifest_path)])
        self.assertEqual(caught.exception.code, 2)
        self.assertFalse(hasattr(audit, "regress"))
        self.runner.assert_not_called()


if __name__ == "__main__":
    unittest.main()
