"""Slimming respects fresh directory activity and retained experiment metadata."""
from __future__ import annotations

from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import shutil
import sys
import time
import unittest
from unittest.mock import patch
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import checkup


class SlimConcurrency(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".cache/arena-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.root = scratch / ("checkup-" + uuid.uuid4().hex)
        self.cache = self.root / ".cache"
        self.cache.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.root)
        self.now = time.time()
        self.old = self.now - 24 * 3600
        for name, value in (("ROOT", self.root), ("CACHE", self.cache),
                            ("SKIP_DIRS", {self.cache / "arena/snap"})):
            mock = patch.object(checkup, name, value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(checkup.time, "time", return_value=self.now)
        mock.start(); self.addCleanup(mock.stop)

    def directory(self, name, old=False):
        path = self.cache / name
        path.mkdir(parents=True)
        if old:
            os.utime(path, (self.old, self.old))
        return path

    def slim(self, dry_run=False):
        with redirect_stdout(io.StringIO()):
            checkup.slim(12, dry_run)

    def test_fresh_empty_preparation_directory_survives_and_old_empty_can_go(self):
        fresh = self.directory("owner/exp/preparing")
        old = self.directory("owner/old-empty", old=True)
        self.slim()
        self.assertTrue(fresh.is_dir())
        self.assertFalse(old.exists())

    def test_keep_ancestor_protects_old_empty_descendants_without_traversal(self):
        protected = self.directory("owner/protected")
        (protected / ".keep").touch()
        child = self.directory("owner/protected/deep/empty", old=True)
        calls = []
        actual = checkup.os.scandir
        def scandir(path):
            calls.append(Path(path))
            return actual(path)
        with patch.object(checkup.os, "scandir", side_effect=scandir):
            self.slim()
        self.assertTrue(child.is_dir())
        self.assertNotIn(protected, calls)
        self.assertNotIn(child, calls)

    def test_keep_added_after_discovery_still_protects_empty_descendant(self):
        protected = self.directory("owner/protected")
        child = self.directory("owner/protected/deep-empty", old=True)
        actual = checkup.os.listdir
        def listdir(path):
            if Path(path) == child:
                (protected / ".keep").touch()
            return actual(path)
        with patch.object(checkup.os, "listdir", side_effect=listdir):
            self.slim()
        self.assertTrue(child.is_dir())

    def test_keep_added_after_file_discovery_still_protects_bulk_file(self):
        protected = self.directory("owner/protected")
        bulk = protected / "game.repl26a"
        bulk.write_bytes(b"retained witness")
        os.utime(bulk, (self.old, self.old))
        actual = checkup.scan
        def scan(path):
            for entry in actual(path):
                if Path(entry.path) == bulk:
                    (protected / ".keep").touch()
                yield entry
        with patch.object(checkup, "scan", side_effect=scan):
            self.slim()
        self.assertTrue(bulk.is_file())

    def test_file_refreshed_after_discovery_is_not_removed(self):
        directory = self.directory("owner/changing")
        bulk = directory / "game.stdout"
        bulk.write_bytes(b"active stream")
        os.utime(bulk, (self.old, self.old))
        actual = checkup.scan
        def scan(path):
            for entry in actual(path):
                if Path(entry.path) == bulk:
                    entry.stat(follow_symlinks=False)  # cache the discovery metadata
                    os.utime(bulk, (self.now, self.now))
                yield entry
        with patch.object(checkup, "scan", side_effect=scan):
            self.slim()
        self.assertTrue(bulk.is_file())

    def test_unreadable_keep_ancestor_is_conservatively_protected(self):
        protected = self.directory("owner/unreadable")
        child = self.directory("owner/unreadable/empty", old=True)
        actual = Path.exists
        def exists(path):
            if path == protected / ".keep":
                raise PermissionError("cannot inspect protection")
            return actual(path)
        with patch.object(Path, "exists", exists):
            self.slim()
        self.assertTrue(child.is_dir())

    def test_keep_at_cache_root_and_fixture_snapshot_exclusions_survive(self):
        old = self.directory("owner/old", old=True)
        (self.cache / ".keep").touch()
        self.slim()
        self.assertTrue(old.is_dir())
        (self.cache / ".keep").unlink()
        fixture = self.directory("owner/fixtures/old-empty", old=True)
        snapshot = self.directory("arena/snap/version/old-empty", old=True)
        self.slim()
        self.assertTrue(fixture.is_dir())
        self.assertTrue(snapshot.is_dir())
        self.assertFalse(old.exists())

    def test_directory_that_becomes_fresh_during_empty_check_is_not_removed(self):
        changing = self.directory("owner/changed-empty", old=True)
        actual = checkup.os.listdir
        def listdir(path):
            if Path(path) == changing:
                os.utime(changing, (self.now, self.now))
            return actual(path)
        with patch.object(checkup.os, "listdir", side_effect=listdir):
            self.slim()
        self.assertTrue(changing.is_dir())

    def test_large_lifecycle_metadata_stays_but_old_raw_statistics_are_slimmed(self):
        experiment = self.directory("owner/exp/closed")
        for name in ("spec.json", "summary.json", "progress.json", "game-statistics.json"):
            path = experiment / name
            path.write_bytes(b"x" * (checkup.BIG_JSON + 1))
            os.utime(path, (self.old, self.old))
        self.slim()
        self.assertTrue((experiment / "spec.json").is_file())
        self.assertTrue((experiment / "summary.json").is_file())
        self.assertTrue((experiment / "progress.json").is_file())
        self.assertFalse((experiment / "game-statistics.json").exists())

    def test_dry_run_never_removes_old_empty_or_bulk_files(self):
        old = self.directory("owner/old-empty", old=True)
        bulk = self.cache / "owner/game.stdout"
        bulk.write_bytes(b"log")
        os.utime(bulk, (self.old, self.old))
        self.slim(dry_run=True)
        self.assertTrue(old.is_dir())
        self.assertTrue(bulk.is_file())


if __name__ == "__main__":
    unittest.main()
