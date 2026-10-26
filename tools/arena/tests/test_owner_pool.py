"""Pool catalog aliases retain direct-file provenance; no native commands."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_owner_cli_gate as owner_fixture
import core
import gate


class OwnerPool(unittest.TestCase):
    def setUp(self):
        self.f = owner_fixture.OwnerCliGate()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)

    def entry(self, name, path, ident=None):
        value = {"name": name, "sha256": self.f.sha(path), "size": path.stat().st_size}
        if ident is not None:
            value["id"] = ident
        return value

    def catalog(self, entries):
        self.f.write_json(self.f.pool_file, {"maps": entries})

    def test_underscore_alias_binds_actual_canonical_hyphen_filename(self):
        path = self.f.root / "maps/00gem-fields.map26a"
        self.f.write(path, b"canonical public map bytes")
        self.catalog([self.entry("00gem_fields", path, "map-id")])
        maps = gate._owner_pool()
        self.assertEqual(maps, {"00gem-fields": gate._file(path)})
        self.assertNotIn("00gem_fields", maps)

    def test_duplicate_aliases_and_duplicate_ids_are_refused(self):
        path = self.f.root / "maps/00gem-fields.map26a"
        self.f.write(path, b"canonical map")
        self.catalog([self.entry("00gem_fields", path, "one"), self.entry("00gem-fields", path, "two")])
        with self.assertRaisesRegex(core.ArenaError, "duplicate canonical"):
            gate._owner_pool()
        self.catalog([self.entry("00gem_fields", path, "same-id"), self.entry("fixture-map", self.f.map, "SAME-ID")])
        with patch.object(core, "map_path", side_effect=AssertionError("IDs must be validated before lookup")), \
                self.assertRaisesRegex(core.ArenaError, "duplicate catalog map IDs"):
            gate._owner_pool()

    def test_tampered_hash_and_size_or_malformed_typed_pins_are_refused(self):
        original = self.entry("fixture-map", self.f.map)
        for field, value in (("sha256", "0" * 64), ("sha256", 12), ("size", original["size"] + 1),
                             ("size", True), ("size", float(original["size"])), ("size", 0)):
            with self.subTest(field=field, value=value):
                self.catalog([original | {field: value}])
                with self.assertRaises(core.ArenaError):
                    gate._owner_pool()

    def test_legacy_labs_generated_and_external_fallbacks_are_refused(self):
        for relative in ("maps/legacy/fallback.map26a", "maps/labs/fallback.map26a",
                         "maps/generated/train/fallback.map26a", "outside/fallback.map26a"):
            path = self.f.root / relative
            self.f.write(path, b"same valid map bytes in a forbidden location")
            self.catalog([self.entry("fallback", path)])
            with self.subTest(relative=relative), patch.object(core, "map_path", return_value=path), \
                    self.assertRaisesRegex(core.ArenaError, "direct canonical public map"):
                gate._owner_pool()

    def test_other_canonical_name_is_not_an_allowed_catalog_alias(self):
        other = self.f.root / "maps/other-map.map26a"
        self.f.write(other, b"matching hash does not license a different map name")
        self.catalog([self.entry("requested-map", other)])
        with patch.object(core, "map_path", return_value=other), self.assertRaisesRegex(core.ArenaError, "canonical"):
            gate._owner_pool()

    def test_malicious_or_duplicate_catalog_labels_are_rejected_before_any_lookup(self):
        original = self.entry("fixture-map", self.f.map)
        for name in ("../outside", "legacy/map", "C:/outside", "..", "name.", "NUL", "COM1.txt", "x\\y", None):
            self.catalog([original, original | {"name": name}])
            with self.subTest(name=name), patch.object(core, "map_path", side_effect=AssertionError("unsafe lookup")), \
                    self.assertRaises(core.ArenaError):
                gate._owner_pool()
        self.catalog([original, original | {"name": "FIXTURE-MAP"}])
        with patch.object(core, "map_path", side_effect=AssertionError("duplicate lookup")), self.assertRaises(core.ArenaError):
            gate._owner_pool()

    def test_resolved_lookup_does_not_erase_direct_path_redirection_guards(self):
        path = self.f.map
        self.catalog([self.entry("fixture-map", path)])
        actual_resolve = Path.resolve
        def redirected(candidate, *args, **kwargs):
            return self.f.root / "maps/legacy/fixture-map.map26a" if candidate == path else actual_resolve(candidate, *args, **kwargs)
        with patch.object(core, "map_path", return_value=path), patch.object(Path, "resolve", redirected), \
                self.assertRaisesRegex(core.ArenaError, "redirected"):
            gate._owner_pool()


class RealCurrentPool(unittest.TestCase):
    def test_all_current_public_maps_bind_read_only_without_index_cache_or_cli(self):
        # This is deliberately the actual pinned repository catalog and map bytes.
        with patch.object(core, "_pool_index", side_effect=AssertionError("pool index/cache is not needed")), \
                patch.object(core, "pool_maps", side_effect=AssertionError("no cached pool lookup")):
            maps = gate._owner_pool()
        catalog = json.loads((core.ROOT / "maps/pool.json").read_text(encoding="utf-8"))["maps"]
        self.assertTrue(catalog)
        self.assertEqual(len(maps), len(catalog))
        self.assertEqual(set(maps), {core.map_path(entry["name"]).stem for entry in catalog})
        self.assertTrue(all((core.ROOT / value["path"]).parent == core.ROOT / "maps" for value in maps.values()))


if __name__ == "__main__":
    unittest.main()
