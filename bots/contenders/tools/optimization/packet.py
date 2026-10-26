"""Export and verify offline optimizer packets; no network or account access.

The optimization loop remains the native C++ executable. This standard-library
preparation utility copies a declared dependency closure and hashes every payload.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import sys
from typing import Any, Callable

HERE = Path(__file__).resolve().parent
SCHEMA = "optimizer-packet/v1"
MANIFEST = "packet-manifest.json"
SKIP_DIRS = {".cache", ".cambc", ".git", "__pycache__", "node_modules", "results"}
PYTHON_SKIP = {"site-packages", "__pycache__", "test", "tests", "idlelib", "turtledemo", "ensurepip"}
BOT_ID = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")


def strict_json(path: Path) -> Any:
    def object_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def bad_number(value):
        raise ValueError(f"non-finite JSON number: {value}")

    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=object_pairs, parse_constant=bad_number)


def is_link(path: Path) -> bool:
    if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
        return True
    try:
        return bool(getattr(path.lstat(), "st_file_attributes", 0) & 0x400)
    except FileNotFoundError:
        return False


def reject_link_components(path: Path) -> None:
    for component in (path, *path.parents):
        if is_link(component):
            raise ValueError(f"symlink/junction paths are unsupported: {component.name}")


def relative_payload(value: str) -> str:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError("payload paths must be nonempty relative POSIX paths")
    posix = PurePosixPath(value)
    if posix.is_absolute() or PureWindowsPath(value).is_absolute() or PureWindowsPath(value).drive:
        raise ValueError("absolute payload path refused")
    if any(part in ("", ".", "..") for part in value.split("/")):
        raise ValueError("payload traversal refused")
    return posix.as_posix()


def discover_repo(start: Path = HERE) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "arena.cmd").is_file() or (candidate / MANIFEST).is_file():
            return candidate
    raise ValueError("repository/packet root not found; pass --repo")


def assert_public_configs(path: Path) -> None:
    def visit(value, location):
        if isinstance(value, dict):
            for key, child in value.items():
                visit(child, f"{location}.{key}")
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, f"{location}[{index}]")
        elif isinstance(value, str):
            # Public examples must travel with their packet rather than refer to
            # one developer's drive/home. A slash operator alone is not a path.
            if PureWindowsPath(value).is_absolute() or (len(value) > 1 and value.startswith("/")):
                raise ValueError(f"absolute path in public configuration {path.name}:{location}; use packet-relative paths")

    visit(strict_json(path), "$")


class CopyPlan:
    def __init__(self):
        self.files: dict[str, Path | bytes] = {}

    def add(self, source: Path, target: str, required: bool = True) -> None:
        target = relative_payload(target)
        if not source.exists():
            if required:
                raise ValueError(f"required packet dependency missing: {target}")
            return
        reject_link_components(source)
        if not source.is_file():
            raise ValueError(f"file dependency is not a regular file: {target}")
        previous = self.files.get(target)
        if previous is not None and previous != source:
            previous_hash = hashlib.sha256(previous).hexdigest() if isinstance(previous, bytes) else digest(previous)
            if previous_hash != digest(source):
                raise ValueError(f"conflicting packet destination: {target}")
            return
        self.files[target] = source

    def generated(self, target: str, data: str | bytes) -> None:
        target = relative_payload(target)
        if target in self.files:
            raise ValueError(f"duplicate generated packet destination: {target}")
        self.files[target] = data.encode("utf-8") if isinstance(data, str) else data

    def tree(self, source: Path, target: str, *, skip: set[str] | None = None, required: bool = True,
             skip_suffixes: set[str] | None = None, skip_relative: set[str] | None = None) -> None:
        if not source.exists():
            if required:
                raise ValueError(f"required packet directory missing: {target}")
            return
        reject_link_components(source)
        if not source.is_dir():
            raise ValueError(f"packet directory is not a directory: {target}")
        ignored = SKIP_DIRS | (skip or set())
        for directory, subdirs, names in os.walk(source, followlinks=False):
            directory_path = Path(directory)
            allowed_dirs = []
            for name in sorted(subdirs):
                if name in ignored:
                    continue
                candidate = directory_path / name
                if skip_relative and candidate.relative_to(source).as_posix() in skip_relative:
                    continue
                if is_link(candidate):
                    raise ValueError(f"symlink/junction in packet source: {target}/{candidate.relative_to(source).as_posix()}")
                allowed_dirs.append(name)
            subdirs[:] = allowed_dirs
            for name in sorted(names):
                if name.endswith((".pyc", ".pyo")) or (skip_suffixes and Path(name).suffix.lower() in skip_suffixes):
                    continue
                relative = (directory_path / name).relative_to(source).as_posix()
                self.add(directory_path / name, f"{target}/{relative}")


def runtime_path(explicit: str | None, role: str) -> Path:
    if explicit:
        return Path(explicit).absolute()
    bundled = discover_repo() / "runtime" / role
    if role == "python" and (bundled / "python.exe").is_file():
        return bundled
    if role == "node" and (bundled / "bin/node.exe").is_file() and node_license(bundled):
        return bundled
    if role == "localappdata" and (bundled / "cambc").is_dir():
        return bundled
    home = Path.home()
    dependencies = home / ".cache/codex-runtimes/codex-primary-runtime/dependencies"
    if role == "python":
        candidate = dependencies / "python"
        if (candidate / "python.exe").is_file():
            return candidate
        if (Path(sys.executable).parent / "Lib").is_dir():
            return Path(sys.executable).parent
    if role == "node":
        candidate = dependencies / "node"
        if (candidate / "bin/node.exe").is_file() and node_license(candidate):
            return candidate
        # Some minimal runtime bundles omit redistribution notices. Prefer a
        # locally installed complete Node distribution instead of silently
        # exporting an executable without its license.
        appdata = os.environ.get("LOCALAPPDATA")
        if appdata:
            installed = Path(appdata) / "OpenAI/Codex/runtimes/cua_node"
            if installed.is_dir():
                for version in sorted(installed.iterdir(), reverse=True):
                    candidate = version / "bin"
                    if (candidate / "node.exe").is_file() and node_license(candidate):
                        return candidate
    if role == "localappdata":
        value = os.environ.get("LOCALAPPDATA")
        if value and (Path(value) / "cambc").is_dir():
            return Path(value)
    raise ValueError(f"{role} runtime not found; provide --{role}-runtime")


def node_license(runtime: Path) -> Path | None:
    for directory in (runtime, runtime / "bin"):
        for name in ("LICENSE", "LICENSE.txt", "LICENSE.md"):
            candidate = directory / name
            if candidate.is_file():
                return candidate
    return None


def referenced_bots(repo: Path, engine_root: Path, extra: list[str]) -> set[str]:
    bots = set(extra)
    suites = {path.name for path in (repo / "bots").iterdir() if path.is_dir()}

    def visit(value):
        if isinstance(value, dict):
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
        elif isinstance(value, str):
            candidate = value.split("@", 1)[0]
            if BOT_ID.fullmatch(candidate) and candidate.split("/", 1)[0] in suites:
                bots.add(candidate)

    for config in sorted((engine_root / "examples").glob("*.json")):
        visit(strict_json(config))
    for bot in bots:
        if not BOT_ID.fullmatch(bot) or any(part in (".", "..") for part in bot.split("/")):
            raise ValueError("invalid bot id supplied to packet exporter")
    return bots


def create_plan(repo: Path, kind: str, *, python_runtime: str | None = None, node_runtime: str | None = None,
                localappdata_runtime: str | None = None, bots: list[str] | None = None) -> tuple[CopyPlan, dict]:
    reject_link_components(repo)
    engine_root = repo / "bots/contenders/tools/optimization"
    binary = engine_root / "bin/optimizer.exe"
    if not binary.is_file():
        raise ValueError("build the native C++ optimizer first: bots/contenders/tools/optimization/build.cmd")
    for config in sorted((engine_root / "examples").glob("*.json")):
        assert_public_configs(config)
    if (engine_root / "engines.json").is_file():
        assert_public_configs(engine_root / "engines.json")
    plan = CopyPlan()
    plan.tree(engine_root, "bots/contenders/tools/optimization", skip_relative={"assets/oracle"} if kind == "model" else set())
    plan.generated("run.cmd", '@echo off\r\ncall "%~dp0bots\\contenders\\tools\\optimization\\run.cmd" %*\r\nexit /b %errorlevel%\r\n')
    plan.generated("packet.cmd", '@echo off\r\ncall "%~dp0bots\\contenders\\tools\\optimization\\packet.cmd" %*\r\nexit /b %errorlevel%\r\n')
    first_campaign = "Run a native micro campaign:\r\n  run.cmd c12 -Hours 24 -Out results\\c12 -Iterations 80000" if kind == "native" else "Run a model campaign:\r\n  run.cmd c4 -Hours 24 -Out results\\c4 -Iterations 100000"
    platform_note = "Windows: install at a short path such as C:\\opt; long packet or output paths can exceed Windows path limits.\r\nKeep an active native campaign at its original path when resuming.\r\n\r\n" if kind == "native" else ""
    plan.generated("START-HERE.txt", platform_note + first_campaign + "\r\nResume with the same arguments plus -Resume.\r\n\r\nRead bots\\contenders\\tools\\optimization\\README.md for each engine's inputs, outputs and validation limits.\r\nModel execution uses the C++ executable; native packets additionally bundle Python, Node, the CLI and offline build/runtime assets.\r\nNo online commands, credentials or account settings are included.\r\n")
    for name in ("current-version.json", "runtime-catalog.json", "GameRules.md"):
        plan.add(repo / "docs/reference" / name, f"docs/reference/{name}")
    # Run identities bind the exact SDK header whenever the source repository
    # has it, even for a model campaign. Preserve that small identity input so
    # an existing model checkpoint can move from the repository into a packet.
    plan.add(repo / "sdk/cpp/cambc.h", "sdk/cpp/cambc.h", required=False)
    for map_file in sorted((repo / "maps").glob("*.map26a")):
        plan.add(map_file, f"maps/{map_file.name}")
    plan.tree(repo / "maps/legacy", "maps/legacy", required=False)
    plan.add(repo / "maps/geometry.json", "maps/geometry.json", required=False)
    metadata = {"kind": kind, "platform": "windows-x86_64", "runtime": "C++ executable; model domains remain explicitly qualified by their reports"}
    if kind == "model":
        return plan, metadata
    if kind != "native":
        raise ValueError("packet kind must be model or native")

    for name in ("arena.cmd", "cambc.toml"):
        plan.add(repo / name, name)
    plan.add(repo / ".cambc/map-pool.json", ".cambc/map-pool.json")
    plan.add(repo / ".tools/cambc.exe", ".tools/cambc.exe")
    plan.tree(repo / "tools/arena", "tools/arena")
    for name in ("replay-profiles.json", "replay-interaction-native.mjs"):
        plan.add(repo / "tools" / name, f"tools/{name}")
    plan.tree(repo / "sdk", "sdk")
    plan.tree(repo / "cmake", "cmake")
    plan.tree(repo / "docs/reference", "docs/reference")
    plan.add(repo / "results/releases/LIVE.json", "results/releases/LIVE.json")
    plan.tree(repo / "bots/anchors", "bots/anchors")
    for bot in sorted(referenced_bots(repo, engine_root, bots or [])):
        if bot.startswith("anchors/"):
            continue
        plan.tree(repo / "bots" / bot, f"bots/{bot}")
    seed = repo / "results/contenders/foundation/model-lab-import/seed.repl26a"
    plan.add(seed, "results/contenders/foundation/model-lab-import/seed.repl26a")
    plan.add(seed, "bots/contenders/tools/optimization/assets/oracle/seed.repl26a")
    camp_domain = "results/contenders/optimization/c11/camp-domain"
    for name in ("seed.repl26a", "domain.json", "inventory.json"):
        source = repo / camp_domain / name
        if source.suffix == ".json":
            assert_public_configs(source)
        plan.add(source, f"{camp_domain}/{name}")

    python = runtime_path(python_runtime, "python")
    for source in sorted(python.iterdir()):
        if source.is_file() and (source.name.lower().startswith(("python", "vcruntime")) and source.suffix.lower() in {".exe", ".dll"} or source.name.upper().startswith("LICENSE")):
            plan.add(source, f"runtime/python/{source.name}")
    plan.tree(python / "Lib", "runtime/python/Lib", skip=PYTHON_SKIP)
    plan.tree(python / "DLLs", "runtime/python/DLLs")
    if "runtime/python/python.exe" not in plan.files:
        raise ValueError("Python runtime must contain python.exe")
    node = runtime_path(node_runtime, "node")
    node_executable = node / "bin/node.exe" if (node / "bin/node.exe").is_file() else node / "node.exe"
    plan.add(node_executable, "runtime/node/bin/node.exe")
    license_path = node_license(node)
    if not license_path:
        raise ValueError("Node distribution must include its LICENSE; provide --node-runtime with a complete offline distribution")
    plan.add(license_path, "runtime/node/LICENSE.txt")

    appdata = runtime_path(localappdata_runtime, "localappdata") / "cambc"
    for directory in ("wasi-sdk-33", "cmake-3.31.8-windows-x86_64", "ninja-1.12.1-windows-x86_64"):
        plan.tree(appdata / directory, f"runtime/localappdata/cambc/{directory}")
    pinned = strict_json(repo / "docs/reference/current-version.json")
    profiles = strict_json(repo / "tools/replay-profiles.json")["profiles"]
    selected = next((profile for profile in profiles if profile.get("cliVersion") == pinned["cli"]["version"]), None)
    if not selected:
        raise ValueError("no replay profile matches the pinned CLI")
    release = selected["cacheRelease"]
    if not re.fullmatch(r"[0-9a-f]{64}", release):
        raise ValueError("invalid pinned cache release")
    for directory in ("cmake", "mapeditor", "sdk", "toolchains", "visualiser"):
        plan.tree(appdata / "assets" / release / directory, f"runtime/localappdata/cambc/assets/{release}/{directory}")
    engine = appdata / "assets" / release / "visualiser/assets" / selected["engineFile"]
    if digest(engine) != selected["decoderSha256"]:
        raise ValueError("gameplay engine does not match the pinned replay profile")
    plan.add(engine, "bots/contenders/tools/optimization/assets/oracle/gameplay.wasm")
    metadata["pinned"] = {"cli": pinned["cli"]["version"], "engine": selected["engineVersion"], "cache_release": release}
    metadata["runtime"] = "Bundled Windows C++/Python/Node/CLI/WASI SDK; offline cache paths are scoped by run.ps1"
    return plan, metadata


def export_packet(repo: Path, out: Path, kind: str, *, progress: Callable[[str], None] | None = None, **kwargs) -> dict:
    repo, out = repo.absolute(), out.absolute()
    reject_link_components(out)
    try:
        out.relative_to(repo / "bots/contenders/tools/optimization")
    except ValueError:
        pass
    else:
        raise ValueError("packet destination must be outside the copied engine source tree")
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise ValueError("packet destination must be a new or empty directory")
    if progress:
        progress("Checking declared offline dependencies...")
    plan, metadata = create_plan(repo, kind, **kwargs)
    payload_bytes = sum(len(source) if isinstance(source, bytes) else source.stat().st_size for source in plan.files.values())
    manifest_bytes = 4096 + sum(len(json.dumps(relative, ensure_ascii=False).encode("utf-8")) + 256 for relative in plan.files)
    reserve_bytes = max(64 * 1048576, (payload_bytes + 19) // 20)
    existing = out
    while not existing.exists():
        existing = existing.parent
    available = shutil.disk_usage(existing).free
    required_bytes = payload_bytes + manifest_bytes + reserve_bytes
    if available < required_bytes:
        raise ValueError(f"packet needs at least {required_bytes} free bytes ({payload_bytes} payload + {reserve_bytes} reserve + {manifest_bytes} manifest); destination has {available}; no files copied")
    if progress:
        progress(f"Copying {len(plan.files)} files ({payload_bytes / 1048576:.1f} MiB payload; {reserve_bytes / 1048576:.1f} MiB disk reserve) into the new packet...")
    out.mkdir(parents=True, exist_ok=True)
    marker = out / ".packet-incomplete"
    with marker.open("xb") as stream:
        stream.write(b"Export incomplete. A valid packet has packet-manifest.json and no incomplete marker.\n")
    records = []
    total = 0
    for index, (relative, source) in enumerate(sorted(plan.files.items()), 1):
        destination = out / relative
        reject_link_components(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(source, bytes):
            with destination.open("xb") as stream:
                stream.write(source)
        else:
            shutil.copy2(source, destination, follow_symlinks=False)
        size = destination.stat().st_size
        records.append({"path": relative, "size": size, "sha256": digest(destination)})
        total += size
        if progress and (index % 2000 == 0 or index == len(plan.files)):
            progress(f"Copied {index}/{len(plan.files)} files ({total / 1048576:.1f} MiB).")
    manifest = {"schema": SCHEMA, **metadata, "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "entrypoint": "run.cmd", "optimizer": "bots/contenders/tools/optimization/bin/optimizer.exe",
                "files": records, "payload_bytes": total, "payload_identity": hashlib.sha256(canonical(records)).hexdigest()}
    # The incomplete marker becomes the final manifest by a same-directory
    # rename. No removal or cross-volume move of user content is required.
    with marker.open("wb") as stream:
        stream.write(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False).encode("utf-8") + b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    marker.replace(out / MANIFEST)
    if progress:
        progress("Verifying all copied payload hashes...")
    verify_packet(out)
    return manifest


def verify_packet(root: Path, strict: bool = False) -> dict:
    root = root.absolute()
    reject_link_components(root)
    if (root / ".packet-incomplete").exists():
        raise ValueError("packet export is incomplete")
    manifest = strict_json(root / MANIFEST)
    if manifest.get("schema") != SCHEMA or not isinstance(manifest.get("files"), list):
        raise ValueError("unsupported packet manifest")
    seen = set()
    total = 0
    for record in manifest["files"]:
        relative = relative_payload(record.get("path"))
        if relative in seen:
            raise ValueError(f"duplicate manifest payload: {relative}")
        seen.add(relative)
        expected_size = record.get("size")
        expected_hash = record.get("sha256")
        if isinstance(expected_size, bool) or not isinstance(expected_size, int) or expected_size < 0 or not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise ValueError(f"invalid payload descriptor: {relative}")
        path = root / relative
        reject_link_components(path)
        if not path.is_file() or path.stat().st_size != expected_size or digest(path) != expected_hash:
            raise ValueError(f"packet payload verification failed: {relative}")
        total += expected_size
    if total != manifest.get("payload_bytes") or hashlib.sha256(canonical(manifest["files"])).hexdigest() != manifest.get("payload_identity"):
        raise ValueError("packet aggregate identity mismatch")
    if strict:
        for directory, subdirs, names in os.walk(root, followlinks=False):
            subdirs[:] = [name for name in subdirs if name not in {".cache", "results"}]
            for name in names:
                relative = (Path(directory) / name).relative_to(root).as_posix()
                if relative != MANIFEST and relative not in seen:
                    raise ValueError(f"unlisted packet payload: {relative}")
    return {"schema": SCHEMA, "kind": manifest["kind"], "files": len(seen), "payload_bytes": total,
            "payload_identity": manifest["payload_identity"], "verified": True}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create", help="copy an offline worker packet into an empty directory")
    create.add_argument("--kind", choices=("model", "native"), required=True)
    create.add_argument("--out", type=Path, required=True)
    create.add_argument("--repo", type=Path)
    create.add_argument("--python-runtime")
    create.add_argument("--node-runtime")
    create.add_argument("--localappdata-runtime")
    create.add_argument("--bot", action="append", default=[])
    verify = subparsers.add_parser("verify", help="verify every manifest-listed byte")
    verify.add_argument("packet", type=Path)
    verify.add_argument("--strict", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "verify":
            result = verify_packet(args.packet, args.strict)
        else:
            manifest = export_packet(args.repo or discover_repo(), args.out, args.kind,
                progress=lambda message: print(message, file=sys.stderr, flush=True),
                python_runtime=args.python_runtime, node_runtime=args.node_runtime,
                localappdata_runtime=args.localappdata_runtime, bots=args.bot)
            result = {key: manifest[key] for key in ("schema", "kind", "payload_bytes", "payload_identity")}
            result["files"] = len(manifest["files"])
            result["packet"] = str(args.out.absolute())
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, KeyError, StopIteration, subprocess.SubprocessError) as error:
        print(f"packet error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
