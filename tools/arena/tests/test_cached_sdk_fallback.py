"""Cached source closure matches ordinary missing-relative SDK include lookup; no compiler runs."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_private_snapshots as fixtures
import core


class CachedSdkFallback(unittest.TestCase):
    setUp = fixtures.PrivateSnapshots.setUp
    make_bot = fixtures.PrivateSnapshots.make_bot
    fake_build = fixtures.PrivateSnapshots.fake_build
    private_entry = fixtures.PrivateSnapshots.private_entry
    def test_missing_relative_quoted_sdk_include_is_accepted(self):
        main = self.bot.path / "src/main.cpp"
        main.write_bytes(b'#include "cambc.h"\n' + main.read_bytes())
        ent = self.private_entry()
        self.assertFalse((ent.path / "src/cambc.h").exists())
        self.assertEqual(core.cached_snapshot(ent.key, snapshot_root=self.private,
                                             snapshot_owner="owner").key, ent.key)

    def test_missing_redirected_relative_include_is_not_mistaken_for_sdk_lookup(self):
        main = self.bot.path / "src/main.cpp"
        main.write_bytes(b'#include "alias.h"\n' + main.read_bytes())
        ent = self.private_entry()
        alias, target = ent.path / "src/alias.h", ent.path / "src/knob.h"
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            return target if path == alias else original(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve):
            with self.assertRaisesRegex(core.ArenaError, "redirected"):
                core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")

    def test_existing_redirected_relative_include_is_refused(self):
        alias = self.bot.path / "src/alias.h"
        alias.write_bytes(b"// fixture alias\n")
        main = self.bot.path / "src/main.cpp"
        main.write_bytes(b'#include "alias.h"\n' + main.read_bytes())
        ent = self.private_entry()
        alias, target = ent.path / "src/alias.h", ent.path / "src/knob.h"
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            return target if path == alias else original(path, *args, **kwargs)
        with patch.object(Path, "resolve", autospec=True, side_effect=resolve):
            with self.assertRaisesRegex(core.ArenaError, "redirected"):
                core.cached_snapshot(ent.key, snapshot_root=self.private, snapshot_owner="owner")


if __name__ == "__main__":
    suite = unittest.TestSuite(CachedSdkFallback(name) for name in (
        "test_missing_relative_quoted_sdk_include_is_accepted",
        "test_missing_redirected_relative_include_is_not_mistaken_for_sdk_lookup",
        "test_existing_redirected_relative_include_is_refused"))
    raise SystemExit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
