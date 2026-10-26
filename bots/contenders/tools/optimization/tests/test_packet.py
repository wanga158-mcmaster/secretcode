"""Packet integrity/closure tests. Fixtures never touch installed runtimes or accounts."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("optimizer_packet", HERE.parent / "packet.py")
packet = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(packet)


class PacketTests(unittest.TestCase):
    def setUp(self):
        repo = HERE.parents[4]
        scratch = repo / ".cache/contenders/optimization/packet-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = Path(tempfile.mkdtemp(prefix="case-", dir=scratch))
        self.repo = self.temp / "repo"
        self.engine = self.repo / "bots/contenders/tools/optimization"
        self.write("arena.cmd", "@echo off\n")
        self.write("bots/contenders/tools/optimization/bin/optimizer.exe", b"test-executable-not-run")
        self.write("bots/contenders/tools/optimization/src/main.cpp", "int main() { return 0; }\n")
        self.write("bots/contenders/tools/optimization/examples/c4-example.json", '{"cases":[]}\n')
        self.write("bots/contenders/tools/optimization/run.cmd", "@echo off\n")
        self.write("bots/contenders/tools/optimization/run.ps1", "param($Engine)\n")
        self.write("bots/contenders/tools/optimization/packet.py", "# fixture\n")
        self.write("bots/contenders/tools/optimization/assets/model/data.json", "{}\n")
        self.write("bots/contenders/tools/optimization/assets/oracle/gameplay.wasm", b"fixture-gameplay")
        self.write("docs/reference/current-version.json", '{"cli":{"version":"test"}}\n')
        self.write("docs/reference/runtime-catalog.json", "{}\n")
        self.write("docs/reference/GameRules.md", "# Fixture\n")
        self.write("sdk/cpp/cambc.h", "// exact SDK identity fixture\n")
        self.write("maps/one.map26a", b"map")
        self.write("maps/legacy/two.map26a", b"legacy-map")
        self.write("maps/geometry.json", "{}\n")

    def tearDown(self):
        # Every deletion is confined to our verified regenerable test directory.
        resolved = self.temp.resolve()
        scratch = (HERE.parents[4] / ".cache/contenders/optimization/packet-tests").resolve()
        self.assertTrue(resolved.is_relative_to(scratch))
        shutil.rmtree(resolved)

    def write(self, relative, content):
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode("utf-8"))
        return path

    def test_model_packet_round_trip(self):
        out = self.temp / "packet with spaces"
        manifest = packet.export_packet(self.repo, out, "model")
        verified = packet.verify_packet(out, strict=True)
        self.assertTrue(verified["verified"])
        self.assertEqual(verified["files"], len(manifest["files"]))
        self.assertTrue((out / "run.cmd").is_file())
        self.assertTrue((out / "bots/contenders/tools/optimization/assets/model/data.json").is_file())
        self.assertFalse((out / "bots/contenders/tools/optimization/assets/oracle/gameplay.wasm").exists())
        self.assertFalse((out / "runtime/python/python.exe").exists())
        self.assertFalse((out / "results/contenders/optimization/c11/camp-domain").exists())
        self.assertEqual((out / "sdk/cpp/cambc.h").read_bytes(), (self.repo / "sdk/cpp/cambc.h").read_bytes())
        self.assertFalse((out / ".packet-incomplete").exists())
        self.assertNotIn(str(self.repo), (out / packet.MANIFEST).read_text())

    def test_destination_must_be_empty(self):
        out = self.temp / "out"
        out.mkdir()
        protected = out / "user.txt"
        protected.write_text("keep")
        with self.assertRaisesRegex(ValueError, "empty"):
            packet.export_packet(self.repo, out, "model")
        self.assertEqual(protected.read_text(), "keep")

    def test_insufficient_destination_space_leaves_no_partial_packet(self):
        out = self.temp / "out"
        with patch.object(packet.shutil, "disk_usage", return_value=SimpleNamespace(free=1)):
            with self.assertRaisesRegex(ValueError, "free bytes.*no files copied"):
                packet.export_packet(self.repo, out, "model")
        self.assertFalse(out.exists())

    def test_node_license_must_be_present_in_distribution(self):
        node = self.temp / "node-license"
        (node / "bin").mkdir(parents=True)
        self.assertIsNone(packet.node_license(node))
        notice = node / "bin/LICENSE"
        notice.write_text("Node distribution notice")
        self.assertEqual(packet.node_license(node), notice)

    def test_reexport_prefers_packet_bundled_runtimes(self):
        python = self.repo / 'runtime/python'
        python.mkdir(parents=True)
        (python / 'python.exe').write_bytes(b'fixture-python')
        node = self.repo / 'runtime/node'
        (node / 'bin').mkdir(parents=True)
        (node / 'bin/node.exe').write_bytes(b'fixture-node')
        (node / 'LICENSE').write_text('Node notice')
        localappdata = self.repo / 'runtime/localappdata'
        (localappdata / 'cambc').mkdir(parents=True)
        with patch.object(packet, 'discover_repo', return_value=self.repo):
            self.assertEqual(packet.runtime_path(None, 'python'), python)
            self.assertEqual(packet.runtime_path(None, 'node'), node)
            self.assertEqual(packet.runtime_path(None, 'localappdata'), localappdata)

    def test_destination_cannot_nest_in_sources(self):
        with self.assertRaisesRegex(ValueError, "source tree"):
            packet.export_packet(self.repo, self.engine / "packet", "model")

    def test_modified_payload_rejected(self):
        out = self.temp / "out"
        packet.export_packet(self.repo, out, "model")
        (out / "maps/one.map26a").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "verification failed"):
            packet.verify_packet(out)

    def test_unlisted_payload_rejected_in_strict_mode(self):
        out = self.temp / "out"
        packet.export_packet(self.repo, out, "model")
        (out / "surprise.cmd").write_text("echo surprise")
        with self.assertRaisesRegex(ValueError, "unlisted"):
            packet.verify_packet(out, strict=True)

    def test_traversal_duplicate_and_absolute_manifest_entries_rejected(self):
        for replacement in ("../escape", "C:/escape", "/escape", "maps\\one.map26a", "maps/./one.map26a"):
            out = self.temp / ("out-" + str(len(list(self.temp.iterdir()))))
            packet.export_packet(self.repo, out, "model")
            manifest_path = out / packet.MANIFEST
            manifest = json.loads(manifest_path.read_text())
            manifest["files"][0]["path"] = replacement
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                packet.verify_packet(out)
        out = self.temp / "duplicates"
        packet.export_packet(self.repo, out, "model")
        manifest_path = out / packet.MANIFEST
        manifest = json.loads(manifest_path.read_text())
        manifest["files"].append(manifest["files"][0])
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            packet.verify_packet(out)

    def test_public_configuration_absolute_paths_rejected(self):
        for value in ("C:/Users/private/dataset.jsonl", "/home/private/dataset.jsonl"):
            config = self.engine / "examples/c4-example.json"
            config.write_text(json.dumps({"dataset": value}))
            with self.assertRaisesRegex(ValueError, "absolute path"):
                packet.export_packet(self.repo, self.temp / "out", "model")

    def test_incomplete_export_rejected(self):
        out = self.temp / "out"
        packet.export_packet(self.repo, out, "model")
        (out / ".packet-incomplete").write_text("incomplete")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            packet.verify_packet(out)

    def test_cache_and_bytecode_omitted(self):
        self.write("bots/contenders/tools/optimization/.cache/run/private.bin", b"cache")
        self.write("bots/contenders/tools/optimization/__pycache__/packet.pyc", b"bytecode")
        out = self.temp / "out"
        packet.export_packet(self.repo, out, "model")
        self.assertFalse((out / "bots/contenders/tools/optimization/.cache").exists())
        self.assertFalse((out / "bots/contenders/tools/optimization/__pycache__").exists())

    def test_source_symlink_rejected(self):
        target = self.write("outside.txt", "outside")
        link = self.engine / "src/link.txt"
        try:
            link.symlink_to(target)
        except OSError:
            self.skipTest("host does not permit creating symbolic links")
        with self.assertRaisesRegex(ValueError, "symlink"):
            packet.export_packet(self.repo, self.temp / "out", "model")

    @unittest.skipUnless(os.name == "nt", "Windows junction semantics")
    def test_source_junction_rejected(self):
        import _winapi
        target = self.repo / "outside-directory"
        target.mkdir()
        (target / "data.txt").write_text("outside")
        junction = self.engine / "src/junction"
        _winapi.CreateJunction(str(target), str(junction))
        with self.assertRaisesRegex(ValueError, "symlink/junction"):
            packet.export_packet(self.repo, self.temp / "out", "model")

    @unittest.skipUnless(os.name == "nt", "Windows junction semantics")
    def test_destination_junction_rejected(self):
        import _winapi
        target = self.temp / "other-output"
        target.mkdir()
        junction = self.temp / "redirected-output"
        _winapi.CreateJunction(str(target), str(junction))
        with self.assertRaisesRegex(ValueError, "symlink/junction"):
            packet.export_packet(self.repo, junction, "model")

    def test_native_closure_uses_only_whitelisted_runtime_dirs(self):
        self.write("cambc.toml", 'sdk_version="test"\n')
        self.write(".cambc/map-pool.json", '{"maps":["one"]}\n')
        self.write(".tools/cambc.exe", b"CLI")
        self.write("tools/arena/core.py", "# fixture\n")
        self.write("tools/replay-interaction-native.mjs", "// fixture\n")
        self.write("sdk/cpp/cambc.h", "// fixture\n")
        self.write("cmake/CMakeLists.txt", "# fixture\n")
        self.write("results/releases/LIVE.json", '{"name":"test"}\n')
        self.write("results/contenders/foundation/model-lab-import/seed.repl26a", b"seed")
        camp_domain = "results/contenders/optimization/c11/camp-domain"
        self.write(f"{camp_domain}/seed.repl26a", b"native-camp-seed")
        self.write(f"{camp_domain}/domain.json", '{"map":{"mapPath":"maps/one.map26a"}}\n')
        self.write(f"{camp_domain}/inventory.json", '{"sources":[{"source_input":1}]}\n')
        self.write(f"{camp_domain}/scenario-extra.json", '{"not":"runtime"}\n')
        self.write(f"{camp_domain}/witnesses.zip", b"not-runtime")
        for name in ("native_camp_oracle.py", "native_camp_oracle.mjs", "native_oracle.py", "native_micro.py", "native_micro.mjs"):
            self.write(f"bots/contenders/tools/optimization/adapters/{name}", "# fixture\n")
        replay_sources = [{"replay": f"{camp_domain}/seed.repl26a", "replay_sha256": packet.digest(self.repo / camp_domain / "seed.repl26a")},
                          {"replay": "results/contenders/foundation/model-lab-import/seed.repl26a", "replay_sha256": packet.digest(self.repo / "results/contenders/foundation/model-lab-import/seed.repl26a")}]
        for name in ("micro-worlds.json", "micro-worlds-wide.json"):
            self.write(f"bots/contenders/tools/optimization/examples/{name}", json.dumps({"schema": "contenders-micro-campaign/v1", "inventory": {"sources": replay_sources}}))
        self.write("bots/anchors/main-v42/src/main.cpp", "// anchor\n")
        python = self.temp / "python"
        (python / "Lib/site-packages").mkdir(parents=True)
        (python / "DLLs").mkdir()
        (python / "python.exe").write_bytes(b"python")
        (python / "LICENSE.txt").write_text("license")
        (python / "Lib/os.py").write_text("# stdlib")
        (python / "Lib/site-packages/secret.py").write_text("not stdlib")
        (python / "DLLs/_ssl.pyd").write_bytes(b"ssl")
        node = self.temp / "node"
        (node / "bin").mkdir(parents=True)
        (node / "bin/node.exe").write_bytes(b"node")
        (node / "LICENSE.txt").write_text("license")
        appdata = self.temp / "localappdata"
        cambc = appdata / "cambc"
        release = "a" * 64
        for directory in ("wasi-sdk-33", "cmake-3.31.8-windows-x86_64", "ninja-1.12.1-windows-x86_64"):
            (cambc / directory).mkdir(parents=True)
            (cambc / directory / "runtime.txt").write_text("runtime")
        for directory in ("cmake", "mapeditor", "sdk", "toolchains", "visualiser"):
            (cambc / "assets" / release / directory).mkdir(parents=True)
            (cambc / "assets" / release / directory / "asset.txt").write_text("asset")
        gameplay = cambc / "assets" / release / "visualiser/assets/game.wasm"
        gameplay.parent.mkdir()
        gameplay.write_bytes(b"fixture-gameplay")
        profile = {"profiles": [{"cliVersion": "test", "cacheRelease": release, "engineFile": "game.wasm", "decoderSha256": packet.digest(gameplay), "engineVersion": "test-engine"}]}
        self.write("tools/replay-profiles.json", json.dumps(profile))
        (cambc / "updates").mkdir()
        (cambc / "updates/account-settings.json").write_text("must not copy")
        (cambc / "credentials.json").write_text("must not copy")
        out = self.temp / "native"
        manifest = packet.export_packet(self.repo, out, "native", python_runtime=str(python), node_runtime=str(node), localappdata_runtime=str(appdata))
        payload = {item["path"] for item in manifest["files"]}
        self.assertIn("runtime/python/python.exe", payload)
        self.assertIn("runtime/node/bin/node.exe", payload)
        self.assertIn("bots/contenders/tools/optimization/assets/oracle/gameplay.wasm", payload)
        self.assertEqual({path for path in payload if path.startswith(camp_domain + "/")},
                         {f"{camp_domain}/{name}" for name in ("seed.repl26a", "domain.json", "inventory.json")})
        for name in ("native_camp_oracle.py", "native_camp_oracle.mjs", "native_oracle.py", "native_micro.py", "native_micro.mjs"):
            self.assertIn(f"bots/contenders/tools/optimization/adapters/{name}", payload)
        for name in ("micro-worlds.json", "micro-worlds-wide.json"):
            config = packet.strict_json(out / "bots/contenders/tools/optimization/examples" / name)
            for source in config["inventory"]["sources"]:
                self.assertIn(source["replay"], payload)
                self.assertEqual(packet.digest(out / source["replay"]), source["replay_sha256"])
        self.assertIn("run.cmd c12", (out / "START-HERE.txt").read_text())
        self.assertIn("install at a short path", (out / "START-HERE.txt").read_text())
        self.assertIn("original path when resuming", (out / "START-HERE.txt").read_text())
        self.assertFalse(any("site-packages" in path or "/updates/" in path or "credentials.json" in path for path in payload))
        self.assertTrue(packet.verify_packet(out, strict=True)["verified"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
