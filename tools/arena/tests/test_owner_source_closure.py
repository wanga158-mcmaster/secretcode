"""Offline exact owner build-closure/fixture configuration regression cases."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_owner_cli_gate as owner_fixture
import core
import gate


class OwnerSourceClosure(unittest.TestCase):
    def setUp(self):
        self.f = owner_fixture.OwnerCliGate()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)

    def materialize(self, bot, contents, settings=None):
        f, settings = self.f, settings or {}
        version = core.version_of(contents, settings)
        snap = f.snap_root / version
        target = snap / "bots" / bot.suite / bot.name
        for logical, data in contents.items():
            f.write(snap / logical.relative_to(f.root), data)
        f.write_json(snap / "meta.json", {"bot": bot.id, "version": version, "overrides": settings, "files": len(contents)})
        f.write(snap / ".ready", b"ready")
        f.write(snap / ".toolchain", b"fixture-toolchain")
        for name in ("bot.wasm", "bot-injected.wasm"):
            f.write(target / ".cambc/build-release" / name, b"\0asm\x01\0\0\0fake-output")
        return core.Entrant(bot, version, target, True, overrides=tuple(sorted(settings.items())))

    def test_entry_excludes_generated_botroot_outputs_and_unrelated_sources(self):
        f = self.f
        snap = f.subject.path.parents[2]
        f.write(f.subject.path / "compile_commands.json", b"native CLI generated output")
        f.write(f.subject.path / "build-metadata.json", b"unrelated native metadata")
        f.write(snap / "bots/main/other/src/main.cpp", b"unrelated bot in the same tree")
        ent, entry = gate._entry(f.subject.key, f.owner)
        self.assertEqual(ent.key, f.subject.key)
        self.assertEqual(entry["sources"], {"main/main/src/main.cpp": f.sha(f.subject.path / "src/main.cpp")})
        self.assertEqual(json.loads((snap / "meta.json").read_bytes())["files"], len(entry["sources"]))

    def test_normal_entry_to_configured_fixture_preserves_true_quoted_suite_closure(self):
        f = self.f
        bot = core.Bot("main", "main")
        contents = {f.root / "bots/main/main/src/main.cpp": b'#include "../../common/shared.h"\n#define draft_mode 0\nint main(){return value;}\n',
                    f.root / "bots/main/common/shared.h": b"constexpr int value = 14;\n"}
        subject = self.materialize(bot, contents)
        f.write(subject.path / "compile_commands.json", b"generated compile database outside src")
        subject, entry = gate._entry(subject.key, f.owner)
        self.assertEqual(set(entry["sources"]), {"main/main/src/main.cpp", "main/common/shared.h"})
        self.assertEqual(len(entry["sources"]), 2)
        # Follow the real fixture preparation contract: derive inputs from _entry's
        # source descriptor, apply the exact knob, and validate the cached result.
        snapshot = subject.path.parents[2]
        measured_contents = {f.root / "bots" / relative: (snapshot / "bots" / relative).read_bytes()
                             for relative in entry["sources"]}
        self.assertEqual(measured_contents, contents)
        settings = {"draft_mode": "14"}
        configured = core.apply_overrides(bot, measured_contents, settings)
        heal = self.materialize(bot, configured, settings)
        f.write(heal.path / "compile_commands.json", b"another generated compile database")
        validated = core.cached_snapshot(heal.key, snapshot_root=f.snap_root, snapshot_owner=f.owner)
        self.assertEqual(validated.key, f"main/main@{core.version_of(configured, settings)}")
        _, heal_entry = gate._entry(heal.key, f.owner, allow_overrides=True)
        self.assertEqual(len(heal_entry["sources"]), 2)
        self.assertEqual(heal_entry["sources"]["main/common/shared.h"], entry["sources"]["main/common/shared.h"])
        plan = {"roles": {"subject": {"bot": bot.id, "overrides": {}},
                           "subject-heal": {"bot": bot.id, "overrides": settings}},
                "games": [{"id": f"fixture:heal:{index}", "subject_role": "subject-heal", "opponent_role": "subject",
                           "map": "fixture-map", "side": "AB"[index % 2]} for index in range(15)]}
        fixture = gate._fixture_inputs(f.owner, subject, entry, plan, {"subject": subject.key, "subject-heal": heal.key})
        self.assertEqual(fixture["roles"]["subject-heal"]["entry"], heal_entry)
        self.assertEqual(fixture["roles"]["subject"]["entry"], entry)


if __name__ == "__main__":
    unittest.main()
