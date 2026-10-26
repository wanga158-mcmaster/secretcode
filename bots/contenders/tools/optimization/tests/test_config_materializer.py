"""Contextual policy grammar and virtual native-source dispatch checks."""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "bots/contenders/tools/optimization/adapters"))
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import config_materializer as materializer
from native_support import EvaluationError


def leaf(value=8):
    return {"knobs": {"draft_mode": value}}


def branch(left=None, right=None, feature="width", threshold=50):
    return {"feature": feature, "threshold": threshold,
            "left": left or leaf(8), "right": right or leaf(99)}


def policy(tree=None):
    return {"schema": materializer.POLICY_SCHEMA, "tree": tree or branch()}


class ContextGrammar(unittest.TestCase):
    def test_finite_bounded_tree_and_safe_knobs(self):
        normalized = materializer.validate_policy(policy())
        self.assertEqual(normalized["feature_schema"], materializer.FEATURE_SCHEMA)
        invalid = [dict(policy(), schema="wrong"), dict(policy(), feature_schema="wrong"),
                   policy(branch(feature="enemy_hidden_health")), policy(branch(threshold=True)),
                   policy(branch(threshold=float("nan"))), policy(branch(threshold=float("inf"))),
                   policy({"knobs": {"draft_mode": "8; system(0)"}}), policy({"knobs": []}),
                   {"tree": branch(), "unrecognized": 1}]
        too_deep = leaf()
        for _ in range(4):
            too_deep = branch(too_deep)
        invalid.extend([policy(too_deep), policy(branch(branch(), branch(branch())))])
        for value in invalid:
            with self.subTest(value=str(value)[:100]), self.assertRaises(EvaluationError):
                materializer.validate_policy(value)

    def test_threshold_and_public_roster_selection(self):
        value = policy(branch(feature="enemy_has_ryth", threshold=.5))
        self.assertEqual(materializer.select_configuration(value, {"enemy_has_ryth": 0}), leaf(8)["knobs"])
        self.assertEqual(materializer.select_configuration(value, {"enemy_has_ryth": .5}), leaf(8)["knobs"])
        self.assertEqual(materializer.select_configuration(value, {"enemy_has_ryth": 1}), leaf(99)["knobs"])
        with self.assertRaises(EvaluationError):
            materializer.select_configuration(value, {"enemy_has_ryth": float("nan")})
        with self.assertRaises(EvaluationError):
            materializer.select_configuration(value, {})

    def test_draft_enum_order_and_paired_roster_identity(self):
        sdk = (ROOT / "sdk/cpp/cambc.h").read_text(encoding="utf-8")
        enum = sdk.split("enum class champion_draft : std::int32_t", 1)[1].split("};", 1)[0]
        names = tuple(line.strip().split("=")[0].strip().rstrip(",") for line in enum.splitlines()
                      if line.strip() and not line.strip().startswith(("{", "///")))
        self.assertEqual(materializer.PICKS, names)
        dispatcher = materializer._dispatcher(branch(), [leaf(8)["knobs"], leaf(99)["knobs"]])
        self.assertIn("case cambc::champion_type::cassi: return 3;", dispatcher)
        self.assertIn("case cambc::champion_type::issac: return 3;", dispatcher)
        self.assertIn("if (gameplay_leaf < 0) gameplay_leaf = optimizer_context::select();", dispatcher)
        self.assertNotIn(".position()", dispatcher)
        self.assertNotIn(".health()", dispatcher)


class VirtualController(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".cache/contenders/optimization/native/config-materializer-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=scratch)
        self.repo = Path(self.directory.name)
        self.source = self.repo / ".cache/arena/snap/123456789abc/bots/anchors/main-v42/src"
        self.source.mkdir(parents=True)
        (self.source / "main.cpp").write_text('#include "config.h"\nnamespace mb { inline int marker = 1; }\n' + materializer.FROZEN_MAIN,
                                              encoding="utf-8", newline="\n")
        (self.source / "config.h").write_text('#pragma once\n#include <array>\nnamespace mb::cfg {\n'
            'inline constexpr int draft_mode = 8;\ninline constexpr std::array<int,3> draft_pref = {0,1,2};\n}\n',
            encoding="utf-8", newline="\n")
        (self.source / "brain.cpp").write_text('#include "config.h"\nnamespace mb { int unchanged(){ return cfg::draft_mode; } }\n',
                                               encoding="utf-8", newline="\n")
        self.original = {p: p.read_bytes() for p in self.source.iterdir()}
        self.base = SimpleNamespace(path=self.source.parent, key="anchors/main-v42@123456789abc")
        self.root_patch = mock.patch.multiple(core, ROOT=self.repo, BOTS=self.repo / "bots")
        self.root_patch.start()

    def tearDown(self):
        self.root_patch.stop()
        self.directory.cleanup()

    def test_independent_leaf_configs_exact_source_and_virtual_dispatch(self):
        # A different working source must never replace the selected snapshot.
        working = self.repo / "bots/anchors/main-v42/src"
        working.mkdir(parents=True)
        (working / "main.cpp").write_text("working source must not be used")
        right = {"knobs": {"draft_mode": 99, "draft_pref": [2, 1, 0]}}
        contents, metadata = materializer.configuration_sources(core, self.base, policy(branch(right=right)))
        generated = self.repo / "bots/contenders/_optimizer-config/src"
        self.assertIn(b"draft_mode = 8;", contents[generated / "leaf0/config.h"])
        self.assertIn(b"draft_mode = 99;", contents[generated / "leaf1/config.h"])
        self.assertIn(b"draft_pref = {2, 1, 0};", contents[generated / "leaf1/config.h"])
        self.assertIn(b"namespace optimizer_leaf0::cfg", contents[generated / "leaf0/config.h"])
        self.assertIn(b"optimizer_leaf1::optimizer_game_turn()", contents[generated / "main.cpp"])
        self.assertNotIn(b"int main()", contents[generated / "leaf0/main.cpp"])
        self.assertIn(b"void optimizer_draft_turn()", contents[generated / "leaf0/main.cpp"])
        self.assertIn(b"turn error", contents[generated / "main.cpp"])
        self.assertIn(b"OPT_CONFIG", contents[generated / "main.cpp"])
        self.assertIn(b"contenders-config-choice/v1", contents[generated / "main.cpp"])
        self.assertIn(b"cambc::end_turn();", contents[generated / "main.cpp"])
        self.assertEqual({p: p.read_bytes() for p in self.source.iterdir()}, self.original)
        self.assertFalse(generated.exists())
        self.assertEqual(metadata["controller_key"], self.base.key)
        self.assertEqual(metadata["compiled_leaves"], 2)
        self.assertTrue(metadata["selection_trace"]["constant_policy_untraced"])
        h = hashlib.sha256()
        for p in sorted(self.original):
            h.update(p.relative_to(self.base.path).as_posix().encode() + b"\0" + self.original[p] + b"\0")
        self.assertEqual(metadata["controller_source_sha256"], h.hexdigest())

    def test_single_leaf_keeps_entrypoint_and_equal_leaves_share_code(self):
        contents, metadata = materializer.configuration_sources(core, self.base, policy(leaf(8)))
        generated = self.repo / "bots/contenders/_optimizer-config/src"
        self.assertEqual(contents[generated / "main.cpp"], self.original[self.source / "main.cpp"])
        self.assertEqual(metadata["compiled_leaves"], 1)
        duplicate = policy(branch(left=leaf(8), right=leaf(8)))
        contents, metadata = materializer.configuration_sources(core, self.base, duplicate)
        self.assertEqual(metadata["leaves"], 1)
        self.assertEqual(metadata["compiled_leaves"], 1)
        self.assertTrue(metadata["collapsed_constant"])
        self.assertEqual(contents[generated / "main.cpp"], self.original[self.source / "main.cpp"])
        self.assertNotIn(generated / "leaf0/main.cpp", contents)

    def test_constant_materialization_preserves_ordinary_arena_version(self):
        contents, metadata = materializer.configuration_sources(core, self.base, policy(leaf(8)))
        identity = {"source_sha256": metadata["controller_source_sha256"]}
        built = SimpleNamespace(ok=True, key="prepared")
        with mock.patch.object(materializer, "exact_entrant", return_value=self.base), \
                mock.patch.object(materializer, "entrant_identity", return_value=identity), \
                mock.patch.object(core, "prepare", return_value=built) as prepare:
            actual, info = materializer.materialize_configuration(core, self.base.key, policy(branch(leaf(8), leaf(8))))
        self.assertIs(actual, built)
        self.assertTrue(info["collapsed_constant"])
        arguments = prepare.call_args.kwargs
        self.assertEqual(arguments["overrides"], {"draft_mode": "8"})
        self.assertEqual(core.version_of(arguments["contents"], arguments["overrides"]),
                         core.version_of(contents, {"draft_mode": "8"}))

    def test_reject_changed_entrypoint_and_external_source_dependency(self):
        main = self.source / "main.cpp"
        main.write_bytes(self.original[main].replace(b"brain->turn();", b"brain->turn(1);"))
        with self.assertRaises(EvaluationError):
            materializer.configuration_sources(core, self.base, policy())
        main.write_bytes(self.original[main])
        extra = self.source.parent / "outside.h"
        extra.write_text("namespace mb {}\n")
        (self.source / "brain.cpp").write_text('#include "../outside.h"\n')
        with self.assertRaises(EvaluationError):
            materializer.configuration_sources(core, self.base, policy())

    def test_namespace_tokens_preserve_strings_and_comments(self):
        text = 'namespace mb { const char* x="mb"; } // mb\n#define LOG ::mb::log()\n'
        self.assertEqual(materializer._namespace(text, "optimizer_leaf0"),
            'namespace optimizer_leaf0 { const char* x="mb"; } // mb\n#define LOG ::optimizer_leaf0::log()\n')


if __name__ == "__main__":
    unittest.main()
