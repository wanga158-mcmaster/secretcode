"""Current experiment retention through ordinary prune; no builds or games."""
from __future__ import annotations

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import shutil
import sys
import unittest
import uuid
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import exp
import population
import report


class ExperimentRetention(unittest.TestCase):
    def setUp(self):
        scratch = core.ROOT / ".cache/arena-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.root = scratch / ("exp-retention-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        self.cache = self.root / ".cache/arena"
        self.directory = self.root / ".cache/owner/exp/case"
        self.directory.mkdir(parents=True)
        self.manifest = self.root / "results/regression/current.json"
        self.spec = {"name": "owner/case", "subjects": [
            {"label": "one", "bot": "odd/one", "set": {}, "weights": None}],
            "entrants": {"one": "odd/one@aaaaaaaaaaaa", "anchors/ref": "anchors/ref@bbbbbbbbbbbb"},
            "opponent_ids": ["anchors/ref"], "maps": "map-one", "sides": "both", "games": 2, "rounds": 0}
        (self.directory / "spec.json").write_text(json.dumps(self.spec), encoding="utf-8")
        rows = [{"subject": "one", "opponent": "anchors/ref", "map": "map-one", "side": side,
                 "result": "loss", "rounds": 1000, "gems": 0, "opp_gems": 1000, "health": {}, "opp_health": {}}
                for side in "AB"]
        (self.directory / "rows.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n", encoding="utf-8")
        for module, name, value in ((exp, "ROOT", self.root), (report, "ROOT", self.root)):
            mock = patch.object(module, name, value)
            mock.start(); self.addCleanup(mock.stop)
        for name, value in (("select", []), ("map_set", ["map-one"])):
            mock = patch.object(core, name, return_value=value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(population, "protected_paths", return_value=set())
        mock.start(); self.addCleanup(mock.stop)
        self.publish()

    def publish(self):
        args = SimpleNamespace(name="owner/case", subjects=exp.DEVELOPMENT, out=str(self.manifest), json=False)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(exp.cmd_audit(args), 0)
        return json.loads(self.manifest.read_text(encoding="utf-8"))

    def snapshot(self, version, bot):
        directory = self.cache / "snap" / version
        files = [directory / "bots" / bot / "src/main.cpp",
                 directory / "bots" / bot.split("/")[0] / "common/runtime.h",
                 directory / "bots" / bot / ".cambc/build-release/bot.wasm",
                 directory / ".ready"]
        for path in files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"retained fixture")
            os.utime(path, (1, 1))
        (directory / "meta.json").write_text(json.dumps({"bot": bot}), encoding="utf-8")
        return directory, files

    def publish_strength(self):
        selected = {
            "main": ("main/main", b"exact released Main"),
            "alias": ("anchors/main-v53", b"exact released Main"),
            "first": ("odd/current", b"first current subject"),
            "second": ("odd/other", b"second current subject")}
        identities, hashes, directories = {}, {}, {}
        for label, (bot, binary) in selected.items():
            source = b'#include "../../common/runtime.h"\n// ' + label.encode() + b'\nint main(){}\n'
            shared = b'// exact shared closure\n'
            relative_main = Path("bots") / bot / "src/main.cpp"
            relative_shared = Path("bots") / bot.split("/")[0] / "common/runtime.h"
            version = core.version_of({core.ROOT / relative_main: source, core.ROOT / relative_shared: shared})
            identity = bot + "@" + version
            directory, _ = self.snapshot(version, bot)
            (directory / relative_main).write_bytes(source)
            (directory / relative_shared).write_bytes(shared)
            (directory / ".toolchain").write_text("fixture-toolchain")
            (directory / "meta.json").write_text(json.dumps({"bot": bot, "version": version, "overrides": {}}))
            wasm = directory / "bots" / bot / ".cambc/build-release/bot.wasm"
            wasm.write_bytes(binary)
            identities[label], hashes[label], directories[label] = identity, exp.sha256(wasm), directory
        reused = {"name": "owner/reused", "subjects": [
            {"label": "off", "bot": "main/main", "set": {"variant": "off"}, "weights": None},
            {"label": "on", "bot": "main/main", "set": {}, "weights": None}],
            "entrants": {"off": "main/main@111111111111", "on": identities["main"],
                         "odd/current": identities["first"], "odd/exploratory": "odd/exploratory@999999999999"},
            "opponent_ids": ["odd/current", "odd/exploratory"],
            "provenance": {"toolchain": "fixture-toolchain", "wasm": {"on": hashes["main"], "odd/current": hashes["first"],
                                    "off": "1" * 64, "odd/exploratory": "9" * 64}}}
        latest = {"name": "owner/latest", "subjects": [
            {"label": "base", "bot": "odd/other", "set": {}, "weights": None}],
            "entrants": {"base": identities["second"], "anchors/main-v53": identities["alias"]},
            "opponent_ids": ["anchors/main-v53"],
            "provenance": {"toolchain": "fixture-toolchain", "wasm": {"base": hashes["second"], "anchors/main-v53": hashes["alias"]}}}
        experiments = []
        for spec, entrants in ((reused, {"on": identities["main"], "odd/current": identities["first"]}),
                               (latest, latest["entrants"])):
            owner, name = spec["name"].split("/")
            directory = self.root / ".cache" / owner / "exp" / name
            directory.mkdir(parents=True)
            (directory / "spec.json").write_text(json.dumps(spec))
            (directory / "rows.jsonl").write_text('{"normalized":"saved exact outcomes"}\n')
            entry = {"name": spec["name"], "entrants": entrants}
            for role, filename in (("spec", "spec.json"), ("rows", "rows.jsonl")):
                path = directory / filename
                entry[role] = {"path": path.relative_to(self.root).as_posix(), "sha256": exp.sha256(path)}
            experiments.append(entry)
        record = {"schema": "development-main-strength-floor/v3", "reference_epoch": "main-v53",
                  "reference": {"snapshot": identities["alias"], "wasm_sha256": hashes["alias"],
                                "released_working_snapshot": identities["main"]},
                  "current_targeted_qualification": {
                      "odd/current": {"bot": "odd/current", "snapshot": identities["first"], "wasm_sha256": hashes["first"], "games": 2},
                      "odd/other": {"bot": "odd/other", "snapshot": identities["second"], "wasm_sha256": hashes["second"], "games": 2}},
                  "retention": {"schema": "arena-current-strength-retention/v1", "experiments": experiments},
                  "historical_main52": {"retention": {"snapshots": ["777777777777"], "path": ".cache/arena"}},
                  "exploratory_catalog": [{"snapshot": "odd/exploratory@999999999999"}]}
        path = self.root / "results/regression/strength-current.json"
        path.write_text(json.dumps(record))
        return path, record, directories

    def publish_strength_alias(self):
        path, record, selected = self.publish_strength()
        target = record["current_targeted_qualification"]["odd/current"]
        tested = target["snapshot"]
        relative_main = Path("bots/odd/current/src/main.cpp")
        relative_shared = Path("bots/odd/common/runtime.h")
        relative_data = Path("bots/odd/current/src/policy.dat")
        source = (selected["first"] / relative_main).read_bytes() + b"// promoted bare default\n"
        shared = (selected["first"] / relative_shared).read_bytes()
        data = b"exact non-source closure input\n"
        version = core.version_of({core.ROOT / relative_main: source, core.ROOT / relative_shared: shared,
                                   core.ROOT / relative_data: data})
        default, _ = self.snapshot(version, "odd/current")
        (default / relative_main).write_bytes(source)
        (default / relative_shared).write_bytes(shared)
        (default / relative_data).write_bytes(data)
        (default / ".toolchain").write_text("fixture-toolchain")
        (default / "meta.json").write_text(json.dumps({"bot": "odd/current", "version": version, "overrides": {}}))
        binary = Path("bots/odd/current/.cambc/build-release/bot.wasm")
        (default / binary).write_bytes((selected["first"] / binary).read_bytes())
        current = "odd/current@" + version
        target.update(snapshot=current, measured_snapshot=tested)
        record["retention"]["current_default_aliases"] = [{
            "bot": "odd/current", "tested_snapshot": tested, "current_default": current,
            "wasm_sha256": target["wasm_sha256"], "toolchain": "fixture-toolchain"}]
        path.write_text(json.dumps(record))
        selected["promoted"] = default
        return path, record, selected

    def test_command_report_binds_spec_rows_and_every_recorded_entrant(self):
        record = self.publish()
        self.assertEqual(record["entrants"], self.spec["entrants"])
        self.assertEqual(record["evidence"]["spec"]["sha256"], exp.sha256(self.directory / "spec.json"))
        self.assertEqual(record["evidence"]["rows"]["sha256"], exp.sha256(self.directory / "rows.jsonl"))
        self.assertEqual(record["evidence"]["rows"]["path"], ".cache/owner/exp/case/rows.jsonl")

    def test_default_prune_keeps_subject_opponent_and_modules_but_removes_obsolete_snapshot(self):
        subject, subject_files = self.snapshot("aaaaaaaaaaaa", "odd/one")
        opponent, opponent_files = self.snapshot("bbbbbbbbbbbb", "anchors/ref")
        obsolete, _ = self.snapshot("cccccccccccc", "odd/obsolete")
        lines = report.prune(days=0, snap_days=0, cache=self.cache, trim_hours=0)
        self.assertTrue(all(p.is_file() for p in subject_files + opponent_files), lines)
        self.assertTrue(subject.is_dir() and opponent.is_dir())
        self.assertFalse(obsolete.exists(), lines)
        self.assertIn("kept 2 pinned snapshots", lines[-1])

    def test_other_experiments_are_not_permanently_pinned(self):
        obsolete, _ = self.snapshot("cccccccccccc", "odd/obsolete")
        other = self.root / ".cache/owner/exp/exploratory/spec.json"
        other.parent.mkdir(parents=True)
        other.write_text(json.dumps({"entrants": {"old": "odd/obsolete@cccccccccccc"}}))
        report.prune(days=0, snap_days=0, cache=self.cache, trim_hours=0)
        self.assertFalse(obsolete.exists())
        self.assertTrue(other.is_file())

    def test_malformed_current_manifest_blocks_prune_before_any_deletion(self):
        original = json.loads(self.manifest.read_text(encoding="utf-8"))
        cases = []
        value = copy.deepcopy(original); value["schema"] = "unknown"; cases.append(value)
        value = copy.deepcopy(original); value["entrants"]["one"] = "../one@aaaaaaaaaaaa"; cases.append(value)
        value = copy.deepcopy(original); value["entrants"]["one"] = "odd/one@../aaaaaaaaaaaa"; cases.append(value)
        value = copy.deepcopy(original); value["entrants"] = {}; cases.append(value)
        value = copy.deepcopy(original); value["experiment"] = "../case"; cases.append(value)
        value = copy.deepcopy(original); del value["evidence"]["rows"]; cases.append(value)
        value = copy.deepcopy(original); value["evidence"]["spec"]["path"] = "elsewhere.json"; cases.append(value)
        value = copy.deepcopy(original); value["evidence"]["rows"]["sha256"] = "0" * 64; cases.append(value)
        value = copy.deepcopy(original); value["entrants"]["one"] = "odd/one@dddddddddddd"; cases.append(value)
        obsolete, _ = self.snapshot("cccccccccccc", "odd/obsolete")
        for value in cases:
            with self.subTest(value=value):
                self.manifest.write_text(json.dumps(value), encoding="utf-8")
                with self.assertRaisesRegex(core.ArenaError, "cannot safely prune"):
                    report.prune(days=0, snap_days=0, cache=self.cache, trim_hours=0)
                self.assertTrue(obsolete.is_dir())
        self.manifest.write_text("broken JSON", encoding="utf-8")
        with self.assertRaisesRegex(core.ArenaError, "cannot safely prune"):
            report.prune(days=0, snap_days=0, cache=self.cache, trim_hours=0)
        self.assertTrue(obsolete.is_dir())

    def test_closing_changes_spec_and_requires_refreshing_authoritative_audit(self):
        changed = dict(self.spec, status="closed", note="answered")
        (self.directory / "spec.json").write_text(json.dumps(changed), encoding="utf-8")
        with self.assertRaisesRegex(core.ArenaError, "refresh the audit"):
            report.regression_protected_paths(self.cache)
        self.publish()
        pins = report.regression_protected_paths(self.cache)
        self.assertIn((self.cache / "snap/aaaaaaaaaaaa").resolve(), pins)
        self.assertIn((self.cache / "snap/bbbbbbbbbbbb").resolve(), pins)

    def test_latest_strength_selects_only_current_pairs_and_reused_main_key(self):
        path, record, selected = self.publish_strength()
        pins = report.regression_protected_paths(self.cache)
        self.assertIn(path.resolve(), pins)
        self.assertTrue(all(p.resolve() in pins for p in selected.values()))
        for entry in record["retention"]["experiments"]:
            for role in ("spec", "rows"):
                self.assertIn((self.root / entry[role]["path"]).resolve(), pins)
        for version in ("111111111111", "777777777777", "999999999999"):
            self.assertNotIn((self.cache / "snap" / version).resolve(), pins)
        # Canonical52 pins remain exactly as before; latest strength adds to them.
        self.assertIn((self.cache / "snap/aaaaaaaaaaaa").resolve(), pins)
        self.assertIn(self.manifest.resolve(), pins)

    def test_latest_only_projection_works_without_canonical_manifest(self):
        path, record, selected = self.publish_strength()
        self.manifest.unlink()
        pins = report.regression_protected_paths(self.cache)
        self.assertEqual(len(pins), 9)  # report, four source files and four selected snapshots
        self.assertNotIn((self.cache / "snap/aaaaaaaaaaaa").resolve(), pins)

    def test_unselected_history_and_source_variants_are_prunable_in_fixture(self):
        self.snapshot("aaaaaaaaaaaa", "odd/one")
        self.snapshot("bbbbbbbbbbbb", "anchors/ref")
        _, _, selected = self.publish_strength()
        unused = [self.snapshot(version, bot)[0] for version, bot in (
            ("111111111111", "main/main"), ("777777777777", "odd/historical"), ("999999999999", "odd/exploratory"))]
        report.prune(days=0, snap_days=0, trim_hours=0, cache=self.cache)
        self.assertTrue(all(p.is_dir() for p in selected.values()))
        self.assertFalse(any(p.exists() for p in unused))

    def test_no_strength_projection_preserves_canonical_only_behavior(self):
        path, record, selected = self.publish_strength()
        del record["retention"]
        path.write_text(json.dumps(record))
        pins = report.regression_protected_paths(self.cache)
        self.assertEqual(len(pins), 5)
        self.assertNotIn(path.resolve(), pins)
        self.assertFalse(any(p.resolve() in pins for p in selected.values()))

    def test_malformed_latest_projection_blocks_all_fixture_deletion(self):
        path, original, _ = self.publish_strength()
        obsolete, _ = self.snapshot("777777777777", "odd/old")
        cases = []
        value = copy.deepcopy(original); value["retention"]["schema"] = "unknown"; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["snapshots"] = ["777777777777"]; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["spec"]["path"] = "../outside/spec.json"; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["rows"]["path"] = str(self.root / "elsewhere.json"); cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["rows"]["sha256"] = "0" * 64; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["rows"]["sha256"] = "abc"; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["entrants"]["off"] = "main/main@111111111111"; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][1] = copy.deepcopy(value["retention"]["experiments"][0]); cases.append(value)
        value = copy.deepcopy(original); value["retention"]["experiments"][0]["entrants"]["on"] = "main/main@111111111111"; cases.append(value)
        value = copy.deepcopy(original); value["reference"]["released_working_snapshot"] = "main/main@111111111111"; cases.append(value)
        value = copy.deepcopy(original); value["reference_epoch"] = "main-v52"; cases.append(value)
        value = copy.deepcopy(original); value["current_targeted_qualification"]["odd/current"]["wasm_sha256"] = "0" * 64; cases.append(value)
        for value in cases:
            with self.subTest(value=value):
                path.write_text(json.dumps(value))
                with self.assertRaisesRegex(core.ArenaError, "cannot safely prune"):
                    report.prune(days=0, snap_days=0, trim_hours=0, cache=self.cache)
                self.assertTrue(obsolete.is_dir())

    def test_stale_selected_spec_rows_and_module_fail_closed(self):
        path, record, selected = self.publish_strength()
        paths = [self.root / record["retention"]["experiments"][0][role]["path"] for role in ("spec", "rows")]
        paths.append(selected["main"] / "bots/main/main/.cambc/build-release/bot.wasm")
        for changed in paths:
            with self.subTest(path=changed):
                saved = changed.read_bytes()
                changed.write_bytes(saved + b"changed")
                with self.assertRaisesRegex(core.ArenaError, "cannot safely prune"):
                    report.regression_protected_paths(self.cache)
                changed.write_bytes(saved)
        meta = selected["first"] / "meta.json"
        meta.write_text(json.dumps({"bot": "odd/wrong", "version": selected["first"].name, "overrides": {}}))
        with self.assertRaisesRegex(core.ArenaError, "metadata disagrees"):
            report.regression_protected_paths(self.cache)

    def test_valid_hash_cannot_authorize_wrong_subject_opponent_role(self):
        path, record, _ = self.publish_strength()
        entry = record["retention"]["experiments"][0]
        spec_path = self.root / entry["spec"]["path"]
        spec = json.loads(spec_path.read_text())
        spec["opponent_ids"].remove("odd/current")
        spec_path.write_text(json.dumps(spec))
        entry["spec"]["sha256"] = exp.sha256(spec_path)
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(core.ArenaError, "recorded subject/opponent"):
            report.regression_protected_paths(self.cache)

    def test_selected_cached_sources_and_toolchain_are_required_for_replay(self):
        _, _, selected = self.publish_strength()
        paths = [selected["first"] / "bots/odd/current/src/main.cpp",
                 selected["first"] / "bots/odd/common/runtime.h", selected["first"] / ".toolchain"]
        for changed in paths:
            with self.subTest(path=changed):
                saved = changed.read_bytes()
                changed.write_bytes(saved + b"drift")
                with self.assertRaisesRegex(core.ArenaError, "source closure changed|toolchain disagrees"):
                    report.regression_protected_paths(self.cache)
                changed.unlink()
                with self.assertRaisesRegex(core.ArenaError, "source closure|missing"):
                    report.regression_protected_paths(self.cache)
                changed.write_bytes(saved)

    def test_in_repo_path_redirection_is_rejected(self):
        path, record, _ = self.publish_strength()
        descriptor_path = self.root / record["retention"]["experiments"][0]["spec"]["path"]
        resolve = Path.resolve
        def redirected(candidate, *args, **kwargs):
            if candidate == descriptor_path:
                return self.root / "unexpected-target.json"
            return resolve(candidate, *args, **kwargs)
        with patch.object(Path, "resolve", redirected), self.assertRaisesRegex(core.ArenaError, "redirect"):
            report.regression_protected_paths(self.cache)

    def test_explicit_default_alias_pins_measured_and_actual_default_without_sdk(self):
        path, record, selected = self.publish_strength_alias()
        source = self.root / record["retention"]["experiments"][0]["spec"]["path"]
        original = source.read_bytes()
        pins = report.strength_protected_paths(self.cache)
        self.assertEqual(len(pins), 10)  # only one extra current-default closure
        self.assertIn(selected["first"].resolve(), pins)
        self.assertIn(selected["promoted"].resolve(), pins)
        self.assertEqual(source.read_bytes(), original, "immutable measured entrants must remain unchanged")
        self.assertFalse((selected["promoted"] / "sdk").exists(), "SDK presence is not fabricated or required")
        for version in ("111111111111", "777777777777", "999999999999"):
            self.assertNotIn((self.cache / "snap" / version).resolve(), pins)

    def test_arbitrary_stale_duplicate_and_cross_bot_aliases_fail_closed(self):
        path, original, _ = self.publish_strength_alias()
        cases = []
        value = copy.deepcopy(original); value["retention"]["current_default_aliases"] = {}; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["current_default_aliases"] *= 2; cases.append(value)
        for field, bad in (("bot", "odd/unmeasured"), ("tested_snapshot", "odd/other@111111111111"),
                           ("current_default", "odd/other@111111111111"), ("wasm_sha256", "0" * 64),
                           ("toolchain", "wrong-toolchain")):
            value = copy.deepcopy(original); value["retention"]["current_default_aliases"][0][field] = bad; cases.append(value)
        value = copy.deepcopy(original); value["current_targeted_qualification"]["odd/current"]["measured_snapshot"] = "odd/current@111111111111"; cases.append(value)
        value = copy.deepcopy(original); value["current_targeted_qualification"]["odd/current"]["snapshot"] = "odd/current@111111111111"; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["current_default_aliases"][0]["current_default"] = value["retention"]["current_default_aliases"][0]["tested_snapshot"]; cases.append(value)
        value = copy.deepcopy(original); value["retention"]["current_default_aliases"][0]["extra_variants"] = ["111111111111"]; cases.append(value)
        for value in cases:
            with self.subTest(value=value):
                path.write_text(json.dumps(value))
                with self.assertRaisesRegex(core.ArenaError, "cannot safely prune"):
                    report.regression_protected_paths(self.cache)

    def test_alias_requires_actual_raw_toolchain_source_and_default_overrides(self):
        path, record, selected = self.publish_strength_alias()
        default = selected["promoted"]
        for changed in (default / "bots/odd/current/.cambc/build-release/bot.wasm",
                        default / ".toolchain", default / "bots/odd/current/src/main.cpp"):
            with self.subTest(path=changed):
                original = changed.read_bytes()
                changed.write_bytes(original + b"drift")
                with self.assertRaisesRegex(core.ArenaError, "WASM changed|toolchain disagrees|source closure changed"):
                    report.regression_protected_paths(self.cache)
                changed.unlink()
                with self.assertRaisesRegex(core.ArenaError, "missing"):
                    report.regression_protected_paths(self.cache)
                changed.write_bytes(original)
        meta = default / "meta.json"
        metadata = json.loads(meta.read_text()); metadata["overrides"] = {"variant": "on"}
        meta.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(core.ArenaError, "metadata disagrees"):
            report.regression_protected_paths(self.cache)

    def test_alias_never_replaces_the_immutable_tested_source_key(self):
        path, record, _ = self.publish_strength_alias()
        entry = record["retention"]["experiments"][0]
        current = record["current_targeted_qualification"]["odd/current"]["snapshot"]
        spec_path = self.root / entry["spec"]["path"]
        spec = json.loads(spec_path.read_text()); spec["entrants"]["odd/current"] = current
        spec_path.write_text(json.dumps(spec))
        entry["spec"]["sha256"] = exp.sha256(spec_path)
        entry["entrants"]["odd/current"] = current
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(core.ArenaError, "historical or exploratory"):
            report.regression_protected_paths(self.cache)

    def test_alias_non_source_closure_file_cannot_redirect_outside_snapshot(self):
        _, _, selected = self.publish_strength_alias()
        data = selected["promoted"] / "bots/odd/current/src/policy.dat"
        resolve = Path.resolve
        def redirected(candidate, *args, **kwargs):
            if candidate == data:
                return self.root / "outside-snapshot-policy.dat"
            return resolve(candidate, *args, **kwargs)
        with patch.object(Path, "resolve", redirected), self.assertRaisesRegex(core.ArenaError, "source redirects"):
            report.regression_protected_paths(self.cache)


if __name__ == "__main__":
    unittest.main()
