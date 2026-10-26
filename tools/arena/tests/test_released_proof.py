"""Offline producer/consumer contracts; cold builds and transport are synthetic."""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_owner_cli_gate as owner_fixture
import core
import release
import released_proof as proof


class ReleasedProof(unittest.TestCase):
    def setUp(self):
        self.f = owner_fixture.OwnerCliGate()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        self.output = f.root / ".cache/owner/qualification-tooling/proof/proof.json"
        self.roots = tuple(f.root / ".cache/owner/qualification-tooling" / name for name in ("cold-a", "cold-b"))
        self.runtime = f.root / ".tools/cold-runtime"
        f.write(self.runtime, b"independent fake runtime")
        self.build = f.stack.enter_context(patch.object(core, "cold_build_pair", create=True, side_effect=self.build_pair))
        self.validate = f.stack.enter_context(patch.object(core, "validate_cold_build_pair", create=True, side_effect=self.validate_pair))

    def descriptor(self, path):
        value = proof._file(path)
        return value | {"path": str(path)}

    def build_pair(self, bot, contents, *, owner, roots, expected_key, timeout):
        f = self.f
        self.assertEqual(bot.id, "main/main")
        self.assertEqual(expected_key, f.live["key"])
        self.assertEqual(owner, f.owner)
        self.assertEqual(tuple(roots), self.roots)
        self.assertEqual(contents, {f.root / "bots/main/main/src/main.cpp": (f.reference.path / "src/main.cpp").read_bytes()})
        builds = []
        for root in roots:
            self.assertFalse(root.exists())
            target = root / expected_key.rsplit("@", 1)[1] / "bots/main/main"
            f.write(target / "src/main.cpp", next(iter(contents.values())))
            for role, filename in (("raw", "bot.wasm"), ("sdk", "bot-injected.wasm")):
                f.write(target / ".cambc/build-release" / filename,
                        (f.reference.path / ".cambc/build-release" / filename).read_bytes())
            f.process(root / "process", ["fake-compiler", "--released-source"])
            builds.append({"root": str(root), "raw": self.descriptor(target / ".cambc/build-release/bot.wasm"),
                           "sdk": self.descriptor(target / ".cambc/build-release/bot-injected.wasm"),
                           "dispatch": self.descriptor(root / "process/dispatch.json"),
                           "process": self.descriptor(root / "process/process-end.json")})
        pair = {"schema": "arena.owner-cold-build-pair.v1", "owner": owner, "bot": bot.id, "key": expected_key,
                "roots": list(map(str, roots)), "runtime": self.descriptor(self.runtime), "builds": builds}
        return pair

    def validate_pair(self, pair, bot, contents, *, owner, expected_key):
        self.assertEqual(bot.id, "main/main")
        self.assertEqual(owner, self.f.owner)
        self.assertEqual(expected_key, self.f.live["key"])
        for descriptor in [pair["runtime"], *(value for build in pair["builds"] for key, value in build.items() if key != "root")]:
            if self.descriptor(Path(descriptor["path"])) != descriptor:
                raise core.ArenaError("cold retained artifact changed")
        for build in pair["builds"]:
            process = json.loads(Path(build["process"]["path"]).read_bytes())
            if process.get("tree_exit_confirmed") is not True:
                raise core.ArenaError("cold owned Job cleanup unconfirmed")
        return pair

    def produce(self):
        return proof.produce(self.f.owner, self.f.reference.key, out=self.output, roots=self.roots)

    def verify(self, descriptor):
        return proof.verify(descriptor, reference=self.f.reference, release_record=self.f.live)

    def rewrite(self, descriptor, mutate):
        value = json.loads(self.output.read_bytes())
        mutate(value)
        value["fingerprint"] = proof._hash({k: v for k, v in value.items() if k != "fingerprint"})
        self.f.write_json(self.output, value)
        return proof._file(self.output)

    def test_real_producer_interface_maps_independent_bundle_to_original_logical_bot(self):
        descriptor = self.produce()
        self.assertEqual(self.build.call_count, 1)
        value = json.loads(self.output.read_bytes())
        self.assertEqual(value["pair"]["bot"], "main/main")
        self.assertEqual(value["reference"]["key"], self.f.reference.key)
        self.assertEqual(value["comparison"]["builds"], 2)
        self.assertNotEqual(value["pair"]["key"], self.f.reference.key)
        self.assertEqual(self.verify(descriptor)["claim"], proof.CLAIM)
        self.assertEqual(self.build.call_count, 1)  # verification never dispatches or rebuilds
        self.assertEqual(self.validate.call_count, 2)

    def test_missing_or_rebound_proof_and_stale_runtime_or_outputs_are_refused(self):
        descriptor = self.produce()
        original = self.output.read_bytes()
        self.output.write_bytes(original + b" ")
        with self.assertRaisesRegex(core.ArenaError, "bytes changed"):
            self.verify(descriptor)
        self.output.write_bytes(original)
        value = json.loads(original)
        paths = [self.runtime, Path(value["pair"]["builds"][0]["raw"]["path"]),
                 self.f.reference.path / ".cambc/build-release/bot-injected.wasm"]
        for path in paths:
            contents = path.read_bytes()
            path.write_bytes(contents + b"changed")
            with self.subTest(path=path), self.assertRaises(core.ArenaError):
                self.verify(descriptor)
            path.write_bytes(contents)

    def test_forged_comparisons_duplicate_roots_and_partial_roles_cannot_be_rebound(self):
        descriptor = self.produce()
        original = self.output.read_bytes()
        for name, mutate in (
            ("role", lambda value: value["pair"]["builds"][0]["sdk"].update(sha256="0" * 64)),
            ("roots", lambda value: value["pair"].update(roots=[value["pair"]["roots"][0]] * 2)),
            ("missing-role", lambda value: value["pair"]["builds"][0].pop("sdk")),
            ("partial-proof", lambda value: value.pop("comparison")),
        ):
            self.output.write_bytes(original)
            changed = self.rewrite(descriptor, mutate)
            with self.subTest(name=name), patch.object(core, "validate_cold_build_pair", side_effect=lambda pair, *a, **kw: pair), self.assertRaises(core.ArenaError):
                self.verify(changed)
        self.output.write_bytes(original)

    def test_consumer_requires_actual_core_execution_revalidation(self):
        descriptor = self.produce()
        with patch.object(core, "validate_cold_build_pair", side_effect=core.ArenaError("unconfirmed actual build")):
            with self.assertRaisesRegex(core.ArenaError, "unconfirmed actual build"):
                self.verify(descriptor)
        with zipfile.ZipFile(self.f.release_dir / "main.zip", "w") as archive:
            archive.writestr("src/main.cpp", b"changed independent released source")
        with self.assertRaisesRegex(core.ArenaError, "source key"):
            self.verify(descriptor)

    def test_failed_second_role_never_publishes_a_proof(self):
        original_build = self.build_pair
        def mismatched(*args, **kwargs):
            pair = original_build(*args, **kwargs)
            pair["builds"][1]["sdk"]["sha256"] = "0" * 64
            return pair
        self.build.side_effect = mismatched
        self.validate.side_effect = lambda pair, *a, **kwargs: pair
        with self.assertRaisesRegex(core.ArenaError, "RAW/SDK"):
            self.produce()
        self.assertFalse(self.output.exists())

    def test_unsafe_or_case_colliding_source_members_are_refused_before_build(self):
        for members in (("src/./main.cpp",), ("src/main.cpp", "src/MAIN.cpp"), ("src/../outside.cpp",),
                        ("src/NUL.cpp",), ("src/main.cpp ",), ("src/main.cpp:stream",)):
            with zipfile.ZipFile(self.f.release_dir / "main.zip", "w") as archive:
                for name in members:
                    archive.writestr(name, b"source")
            with self.subTest(members=members), self.assertRaises(core.ArenaError):
                self.produce()
        self.build.assert_not_called()

    def test_production_release_cli_has_explicit_two_cold_roots_and_no_online_dispatch(self):
        parser = argparse.ArgumentParser()
        release.register(parser.add_subparsers(dest="command"))
        argv = ["release", "--prove-reference", self.f.reference.key, "--owner", self.f.owner,
                "--proof-out", str(self.output), "--cold-root", str(self.roots[0]), "--cold-root", str(self.roots[1])]
        with patch.object(release, "prove_reference", return_value={"path": "synthetic"}) as producer, \
                patch.object(release, "_mark_live", side_effect=AssertionError("online path")), \
                patch.object(release, "_prepare", side_effect=AssertionError("new anchor path")):
            self.assertEqual(release._cmd(parser.parse_args(argv)), 0)
            producer.assert_called_once_with(self.f.owner, self.f.reference.key, out=str(self.output), roots=self.roots, timeout=1800)
            with self.assertRaises(core.ArenaError):
                release._cmd(parser.parse_args(argv + ["--live", "other"]))
            with self.assertRaises(core.ArenaError):
                release._cmd(parser.parse_args(argv[:-2]))


if __name__ == "__main__":
    unittest.main()
