"""Offline release consumption tests; no policy reads, builds, games or release writes."""
from __future__ import annotations

import contextlib
import json
import sys
import unittest
import uuid
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gate
import release
from core import ArenaError


class OwnerGateRelease(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[3] / ".cache" / "owner-cli-gate" / "release-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.root = scratch / uuid.uuid4().hex
        self.root.mkdir()
        self.gate_dir = self.root / "gate"
        self.gate_dir.mkdir()
        self.key = "main/candidate@0123456789ab"
        self.live = {"name": "live-v1", "anchor": "anchors/live-v1", "key": "main/old@111111111111"}
        self.record = {"pass": True, "key": self.key, "ref": self.live["anchor"],
                       "maps": ["map-a", "map-b"], "rounds": 0, "checks": []}
        patcher = mock.patch.object(release, "pool_maps", return_value=["map-a", "map-b"])
        patcher.start()
        self.addCleanup(patcher.stop)

    def write(self, record=None):
        (self.gate_dir / "gate.json").write_text(json.dumps(self.record if record is None else record), encoding="utf-8")

    def verify(self):
        return release.verify_gate_for_release(self.gate_dir, self.key, self.live)

    def test_legacy_gate_keeps_original_checks_without_bound_verifier(self):
        self.write()
        with mock.patch.object(gate, "verify_owner_gate", create=True) as bound:
            self.assertEqual(self.verify(), self.record)
            bound.assert_not_called()

    def test_legacy_gate_mismatches_remain_refused(self):
        for update in ({"pass": False}, {"key": "main/candidate@ffffffffffff"},
                       {"ref": "anchors/other"}, {"maps": ["map-a"]}, {"rounds": 1}):
            with self.subTest(update=update):
                self.write(dict(self.record, **update))
                with self.assertRaises(ArenaError):
                    self.verify()

    def test_bound_gate_is_fully_delegated_with_current_identity(self):
        record = dict(self.record, schema=release.OWNER_GATE_SCHEMA, owner="main",
                      input_fingerprint="a" * 64, evidence_fingerprint="b" * 64)
        self.write(record)
        with mock.patch.object(gate, "verify_owner_gate", return_value=record, create=True) as bound:
            self.assertEqual(self.verify(), record)
            bound.assert_called_once_with(self.gate_dir, subject_key=self.key, live_record=self.live)

    def test_bound_verifier_rejection_is_not_laundered_by_legacy_pass(self):
        self.write(dict(self.record, schema=release.OWNER_GATE_SCHEMA))
        with mock.patch.object(gate, "verify_owner_gate", side_effect=ArenaError("input drift"), create=True):
            with self.assertRaisesRegex(ArenaError, "input drift"):
                self.verify()

    def test_unknown_or_partial_bound_markers_never_fall_back(self):
        for update in ({"schema": "other"}, {"owner": "main"}, {"input_fingerprint": "a" * 64},
                       {"evidence_fingerprint": "b" * 64}, {"schema": None},
                       {"inputs": {}}, {"rows": {}}, {"tests": {}},
                       {"inputs": None}, {"rows": None}, {"tests": None}):
            with self.subTest(update=update):
                self.write(dict(self.record, **update))
                with mock.patch.object(gate, "verify_owner_gate", create=True) as bound:
                    with self.assertRaisesRegex(ArenaError, "unsupported owner-bound gate schema"):
                        self.verify()
                    bound.assert_not_called()

    def test_stripped_owner_markers_cannot_launder_evidence_as_legacy(self):
        record = dict(self.record, schema=release.OWNER_GATE_SCHEMA, owner="main",
                      input_fingerprint="a" * 64, evidence_fingerprint="b" * 64,
                      inputs={"subject": self.key}, rows={"path": "owner/games.jsonl"},
                      tests={"path": "owner/tests.json"})
        for field in ("schema", "owner", "input_fingerprint", "evidence_fingerprint"):
            del record[field]
        self.write(record)
        with mock.patch.object(gate, "verify_owner_gate", create=True) as bound:
            with self.assertRaisesRegex(ArenaError, "unsupported owner-bound gate schema"):
                self.verify()
            bound.assert_not_called()

    def test_diagnostic_experiments_never_qualify(self):
        for field in ("diagnostic_only", "private_snapshots"):
            for schema in ({}, {"schema": release.OWNER_GATE_SCHEMA}):
                with self.subTest(field=field, schema=schema):
                    self.write(dict(self.record, **schema, **{field: True}))
                    with mock.patch.object(gate, "verify_owner_gate", create=True) as bound:
                        with self.assertRaisesRegex(ArenaError, "diagnostic experiments"):
                            self.verify()
                        bound.assert_not_called()

    def test_untyped_diagnostic_markers_are_not_legacy_gates(self):
        for field in ("diagnostic_only", "private_snapshots"):
            for value in (1, "true", None, []):
                with self.subTest(field=field, value=value):
                    self.write(dict(self.record, **{field: value}))
                    with self.assertRaisesRegex(ArenaError, "invalid diagnostic"):
                        self.verify()

    def test_missing_malformed_and_nonobject_records_fail(self):
        with self.assertRaises(ArenaError):
            self.verify()
        (self.gate_dir / "gate.json").write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "cannot read"):
            self.verify()
        self.write([])
        with self.assertRaisesRegex(ArenaError, "object"):
            self.verify()

    def test_prepare_refuses_bound_gate_before_any_release_output(self):
        self.write(dict(self.record, schema=release.OWNER_GATE_SCHEMA))
        working = self.root / "working"
        fake_bot = SimpleNamespace(frozen=False, path=working, id="main/candidate")
        fake_contents = {working / "src" / "main.cpp": b"synthetic source"}
        args = SimpleNamespace(bot="main/candidate", name="candidate-v1", gate=str(self.gate_dir))
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(release, "find_bot", return_value=fake_bot))
            stack.enter_context(mock.patch.object(release, "FileLock", return_value=contextlib.nullcontext()))
            stack.enter_context(mock.patch.object(release, "live", return_value=self.live))
            stack.enter_context(mock.patch.object(release, "read_closure", return_value=fake_contents))
            stack.enter_context(mock.patch.object(release, "version_of", return_value="0123456789ab"))
            stack.enter_context(mock.patch.object(release, "RELEASES", self.root / "releases"))
            stack.enter_context(mock.patch.object(release, "BOTS", self.root / "bots"))
            native = stack.enter_context(mock.patch.object(release.subprocess, "run"))
            stack.enter_context(mock.patch.object(gate, "verify_owner_gate", side_effect=ArenaError("input drift"), create=True))
            with self.assertRaisesRegex(ArenaError, "input drift"):
                release._prepare(args)
            native.assert_not_called()
        self.assertFalse((self.root / "releases").exists())
        self.assertFalse((self.root / "bots").exists())


if __name__ == "__main__":
    unittest.main()
