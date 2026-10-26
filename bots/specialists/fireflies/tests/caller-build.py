"""Build actual-caller reactors from three exact current arena source closures.

No gameplay sources are written. fixture_boot advances one real setup step per
call; its host must declare the fuel offered on each call and repeat until the
readiness mask is 3. This builder supplies no engine host and executes no bot.

Run with the arena Python runtime; --check verifies existing artifacts without
compiling or modifying files. All generated outputs stay in the caller cache;
--build-dir permits isolated reruns there. Changed live sources require a new reviewed experiment, never silent
use of an old snapshot. A missing snapshot is rebuilt only from an exact live
source/override match through ordinary core.prepare().
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BUILD = ROOT / ".cache/arena/orchestration/fireflies-caller/build"
CALLER_CACHE = ROOT / ".cache/arena/orchestration/fireflies-caller"
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import wasi

ARMS = {
    "fireflies-on": ("specialists/fireflies", "f6c57ee622a9", {
        "FIREFLIES_BOUNDED_STARTUP": "1", "FIREFLIES_CONNECTED_CATALOG": "1", "FIREFLIES_CAPTURE_DEFENSE": "1"}),
    "fireflies-control": ("specialists/fireflies", "a194be9502fd", {
        "FIREFLIES_BOUNDED_STARTUP": "1", "FIREFLIES_CONNECTED_CATALOG": "1", "FIREFLIES_CAPTURE_DEFENSE": "1", "core_enabled": "false"}),
    "breakpoint": ("specialists/breakpoint-brigade", "babb2b9cb23d", {}),
}
EXPECTED_BUNDLE = "ebe7e3b973c1d659fa906811209f7d2108390dcd05f5ca5480482928d41f3333"
EXPECTED_WASM = {
    "fireflies-on": ("f02a7d444e5521b52be3afc69fce063b507d8c3b5d2ca5f142e346d2aa1aad00", 3300677),
    "fireflies-control": ("bf9c407e8f210194ba19d4f4a378c8dfb4d1cbadb8010424a4b2c045cd550750", 3300699),
    "breakpoint": ("a23d997754e7ce8823351ff558331d45905dda9a67631068eeb1fe619b88f083", 3311809),
}
FLAGS = ["-mexec-model=reactor", "-Wno-error=sign-compare"]
CLASS_ANCHOR = "class Bot {"
BANK_ANCHOR = "        if (bank()) return;"
RECEIPT_DECLARATIONS = '''\
extern "C" __attribute__((import_module("fixture"), import_name("bank_entry"))) void fixture_bank_entry();
extern "C" __attribute__((import_module("fixture"), import_name("bank_result"))) void fixture_bank_result(int);
'''
BANK_REPLACEMENT = '''\
        fixture_bank_entry();
        const bool fixture_bank_return = bank();
        fixture_bank_result(fixture_bank_return ? 1 : 0);
        if (fixture_bank_return) return;'''


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value) -> str:
    return sha(json.dumps(value, sort_keys=True, separators=(",", ":")).encode())


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def checked_output(path: Path) -> Path:
    if not path.resolve().is_relative_to(BUILD.resolve()):
        raise RuntimeError(f"output escapes owned build directory: {path}")
    if path.is_symlink() or getattr(path, "is_junction", lambda: False)():
        raise RuntimeError(f"generated output may not be a link: {path}")
    return path


def put(path: Path, data: bytes) -> None:
    checked_output(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def json_put(path: Path, value) -> None:
    put(path, (json.dumps(value, indent=2, sort_keys=True) + "\n").encode())


def file_pin(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "sha256": sha(data), "bytes": len(data)}


def bundle_pins() -> dict:
    manifest = ROOT / "docs/reference/current-version.json"
    data = manifest.read_bytes()
    if sha(data) != EXPECTED_BUNDLE:
        raise RuntimeError("official bundle manifest changed; this pinned experiment requires review")

    def walk(value):
        if isinstance(value, dict):
            if isinstance(value.get("path"), str) and isinstance(value.get("sha256"), str):
                yield value["path"], value["sha256"]
            for child in value.values():
                yield from walk(child)
        elif isinstance(value, list):
            for child in value:
                yield from walk(child)

    pins = dict(walk(json.loads(data)))
    for name, expected in pins.items():
        path = (ROOT / name).resolve()
        if not path.is_relative_to(ROOT) or not path.is_file() or sha(path.read_bytes()) != expected:
            raise RuntimeError(f"official bundle input missing or changed: {name}")
    return {"path": rel(manifest), "sha256": sha(data), "pinned_files": len(pins), "files_sha256": canonical(pins)}


def provenance() -> dict:
    sdk_root = core.CLANG.resolve().parent.parent
    binaries = [core.CLANG, core.CLANG.parent / "clang++.cfg", core.CLANG.parent / "wasm-ld.exe"]
    libraries = [wasi.EH / name for name in ("libunwind.a", "libc++.a", "libc++abi.a")]
    libraries.append(wasi.EH.parent / "libc.a")
    builtins = sorted(sdk_root.glob("lib/clang/*/lib/wasm32-*/libclang_rt.builtins.a"))
    if not builtins:
        raise RuntimeError("compiler WASI builtins archives are missing")
    libraries += builtins
    return {"builder": {"path": rel(Path(__file__)), "sha256": sha(Path(__file__).read_bytes())},
            "toolchain": core.toolchain(), "bundle_sha256": EXPECTED_BUNDLE, "bundle": bundle_pins(),
            "sdk": {key: sha((ROOT / key).read_bytes()) for key in ("sdk/cpp/cambc.h", "sdk/cpp/imports.h")},
            "compiler_helper": {"path": "tools/arena/wasi.py", "sha256": sha((ROOT / "tools/arena/wasi.py").read_bytes())},
            "snapshot_helper": {"path": "tools/arena/core.py", "sha256": sha((ROOT / "tools/arena/core.py").read_bytes())},
            "compiler_inputs": {"files": [file_pin(path) for path in binaries + libraries],
                                "base_flags": list(wasi.FLAGS), "fixture_flags": FLAGS, "werror": True,
                                "eh_path": str(wasi.EH.resolve())}}


def fixture_flags(name: str) -> list[str]:
    return [*FLAGS, "-MD", "-MF", str(BUILD / name / "dependencies.d"), "-MT", "fixture_dependencies"]


def dependency_pins(name: str) -> dict:
    """Read only compiler-reported headers; no scan of unused WASI targets.

    The explicit simple -MT target removes Windows drive-colon ambiguity. This
    Make lexer preserves ordinary Windows path backslashes and recognizes only
    documented filename escapes; unexpected syntax fails closed.
    """
    path = BUILD / name / "dependencies.d"
    data = path.read_bytes()
    text = data.decode("utf-8").replace("\\\r\n", "").replace("\\\n", "")
    prefix = "fixture_dependencies:"
    if not text.startswith(prefix):
        raise RuntimeError(f"{name}: compiler dependency target changed")
    rhs = text[len(prefix):]
    tokens, token, i = [], [], 0
    while i < len(rhs):
        char = rhs[i]
        if char.isspace():
            if token:
                tokens.append("".join(token))
                token = []
        elif char == "\\" and i + 1 < len(rhs) and rhs[i + 1] in " \\#:$":
            i += 1
            token.append(rhs[i])
        elif char == "$":
            if i + 1 >= len(rhs) or rhs[i + 1] != "$":
                raise RuntimeError(f"{name}: unexpected dependency variable syntax")
            token.append("$")
            i += 1
        elif char == "#":
            raise RuntimeError(f"{name}: unexpected dependency comment syntax")
        else:
            token.append(char)
        i += 1
    if token:
        tokens.append("".join(token))
    paths = sorted({Path(token).resolve() for token in tokens})
    sdk_root = core.CLANG.resolve().parent.parent
    for source in paths:
        if not source.is_file() or not (source.is_relative_to(ROOT) or source.is_relative_to(sdk_root)):
            raise RuntimeError(f"{name}: unexpected or missing compiler dependency: {source}")
    mandatory = {BUILD / name / "fixture.cpp", ROOT / "sdk/cpp/cambc.h", ROOT / "sdk/cpp/imports.h"}
    if not mandatory.issubset(set(paths)):
        raise RuntimeError(f"{name}: compiler dependency list omitted a required fixture/SDK input")
    return {"file": {"path": rel(path), "sha256": sha(data)}, "inputs": [file_pin(source) for source in paths]}


def live_sources(ident: str, version: str, overrides: dict) -> tuple[core.Bot, dict, dict]:
    bot = core.bot(ident)
    raw = core.read_closure(bot)
    applied = core.apply_overrides(bot, raw, overrides) if overrides else raw
    actual = core.version_of(applied, overrides)
    if actual != version:
        raise RuntimeError(f"current applied source closure differs: {ident}@{actual}; expected {version}")
    return bot, raw, {rel(path): data for path, data in applied.items()}


def pinned_sources(ident: str, version: str, overrides: dict, *, prepare_missing=False) -> tuple[dict, dict]:
    bot, raw, live = live_sources(ident, version, overrides)
    snap = ROOT / ".cache/arena/snap" / version
    if not snap.is_dir():
        if not prepare_missing:
            raise RuntimeError(f"missing pinned snapshot: {ident}@{version}; run this builder without --check")
        entrant = core.prepare(bot, overrides=overrides, contents=raw)
        if not entrant.ok or entrant.version != version:
            raise RuntimeError(f"ordinary arena snapshot build failed: {ident}@{version}: {entrant.log[-1000:]}")
    meta = json.loads((snap / "meta.json").read_text(encoding="utf-8"))
    if (meta.get("bot"), meta.get("version"), meta.get("overrides")) != (ident, version, overrides):
        raise RuntimeError(f"immutable snapshot metadata mismatch: {ident}@{version}")
    if not (snap / ".ready").is_file() or (snap / ".toolchain").read_text(encoding="utf-8").strip() != core.toolchain():
        raise RuntimeError(f"unbuilt or toolchain-stale snapshot: {ident}@{version}")
    # Reconstruct core.closure inside the immutable tree. The CLI can create
    # compile_commands.json beside src/, which is not part of the bot closure.
    sdk = (ROOT / "sdk").resolve()
    found = {p.resolve() for p in (snap / "bots" / ident / "src").rglob("*")
             if p.is_file() and ".cambc" not in p.parts}
    stack = [p for p in found if p.suffix in core.SOURCE_SUFFIXES]
    while stack:
        source_path = stack.pop()
        for include in core.INCLUDE.findall(source_path.read_text(encoding="utf-8", errors="surrogateescape")):
            candidate = (source_path.parent / include).resolve()
            if candidate.is_file() and candidate not in found and not candidate.is_relative_to(sdk) and candidate.is_relative_to(snap.resolve()):
                found.add(candidate)
                if candidate.suffix in core.SOURCE_SUFFIXES:
                    stack.append(candidate)
    sources = {p.relative_to(snap).as_posix(): p.read_bytes() for p in sorted(found)}
    if len(sources) != meta.get("files"):
        raise RuntimeError(f"source file count mismatch: {ident}@{version}")
    calculated = core.version_of({ROOT / key: data for key, data in sources.items()}, overrides)
    if calculated != version:
        raise RuntimeError(f"immutable source version mismatch: expected {version}, found {calculated}")
    if sources != live:
        raise RuntimeError(f"immutable and current applied source bytes differ: {ident}@{version}")
    return sources, meta


def wrapper(ident: str, is_fireflies: bool) -> str:
    connected = "fixture_bot->connected_ready_" if is_fireflies else "true"
    state = "static_cast<int>(fixture_bot->state_)" if is_fireflies else "-1"
    capture_group = "fixture_bot->capture_hold_.group()" if is_fireflies else "-1"
    setup = '''\
        if (!fixture_bot->initialized_) fixture_bot->initialize_bounded();
        if (fixture_bot->initialized_ && !fixture_bot->connected_ready_)
            fixture_bot->initialize_connected_catalog();'''
    if not is_fireflies:
        setup = "        if (!fixture_bot->initialized_) fixture_bot->initialize();"
    return f'''\
// Generated fixture wrapper; main.cpp below is the pinned production source
// with public visibility and two receipt callbacks only.
#include <optional>
#define main fixture_original_main
#include "repo/bots/{ident}/src/main.cpp"
#undef main

namespace {{ std::optional<Bot> fixture_bot; }}
#define FIXTURE_EXPORT(name) extern "C" __attribute__((export_name(#name)))

// Pure constants: the host can query ABI sizes before WASI _initialize.
FIXTURE_EXPORT(fixture_size_settings) unsigned fixture_size_settings() {{ return sizeof(cambc::detail::game_settings); }}
FIXTURE_EXPORT(fixture_size_camp_design) unsigned fixture_size_camp_design() {{ return sizeof(cambc::detail::camp_design); }}
FIXTURE_EXPORT(fixture_size_champion_design) unsigned fixture_size_champion_design() {{ return sizeof(cambc::detail::champion_design); }}
FIXTURE_EXPORT(fixture_size_item_info) unsigned fixture_size_item_info() {{ return sizeof(cambc::detail::item_info); }}
FIXTURE_EXPORT(fixture_size_consumable_info) unsigned fixture_size_consumable_info() {{ return sizeof(cambc::detail::consumable_info); }}
FIXTURE_EXPORT(fixture_get_initialized) int fixture_get_initialized() {{ return fixture_bot ? int(fixture_bot->initialized_) : 0; }}
FIXTURE_EXPORT(fixture_get_connected) int fixture_get_connected() {{ return fixture_bot ? int({connected}) : 0; }}
FIXTURE_EXPORT(fixture_boot) int fixture_boot() {{
    if (!fixture_bot) fixture_bot.emplace();
{setup}
    return fixture_get_initialized() | (fixture_get_connected() << 1);
}}
FIXTURE_EXPORT(fixture_turn) int fixture_turn() {{
    if (!fixture_bot || !fixture_bot->initialized_ || !({connected})) return -1;
    fixture_bot->turn();
    return fixture_get_initialized() | (fixture_get_connected() << 1);
}}
FIXTURE_EXPORT(fixture_get_recovering) int fixture_get_recovering() {{ return fixture_bot ? int(fixture_bot->recovering_) : -1; }}
FIXTURE_EXPORT(fixture_get_partial) int fixture_get_partial() {{ return fixture_bot ? int(fixture_bot->partial_clear_) : -1; }}
FIXTURE_EXPORT(fixture_get_target) int fixture_get_target() {{ return fixture_bot ? fixture_bot->target_ : -1; }}
FIXTURE_EXPORT(fixture_get_group_count) int fixture_get_group_count() {{ return fixture_bot ? int(fixture_bot->groups_.size()) : 0; }}
FIXTURE_EXPORT(fixture_get_state) int fixture_get_state() {{ return fixture_bot ? {state} : -1; }}
FIXTURE_EXPORT(fixture_get_capture_group) int fixture_get_capture_group() {{ return fixture_bot ? {capture_group} : -1; }}
FIXTURE_EXPORT(fixture_get_suppression) unsigned fixture_get_suppression(int index) {{
    if (!fixture_bot || index < 0 || unsigned(index) >= fixture_bot->groups_.size()) return ~0u;
    return fixture_bot->groups_[unsigned(index)].unavailable_until;
}}
FIXTURE_EXPORT(fixture_set_partial) int fixture_set_partial(int value) {{
    if (!fixture_bot || (value != 0 && value != 1)) return -1;
    fixture_bot->partial_clear_ = value != 0;
    return int(fixture_bot->partial_clear_);
}}
FIXTURE_EXPORT(fixture_force_recovering) int fixture_force_recovering() {{
    if (!fixture_bot) return -1;
    fixture_bot->recovering_ = true;
    return 1;
}}
'''


def arm_inputs(name: str, ident: str, version: str, overrides: dict, *, prepare_missing=False) -> tuple[dict, dict, bytes]:
    sources, meta = pinned_sources(ident, version, overrides, prepare_missing=prepare_missing)
    original_hashes = {key: sha(value) for key, value in sources.items()}
    key = f"bots/{ident}/src/main.cpp"
    source = sources[key].decode("utf-8")
    counts = {CLASS_ANCHOR: source.count(CLASS_ANCHOR), BANK_ANCHOR: source.count(BANK_ANCHOR)}
    if any(n != 1 for n in counts.values()):
        raise RuntimeError(f"production patch anchors must occur exactly once: {name}: {counts}")
    changed = source.replace(CLASS_ANCHOR, CLASS_ANCHOR + "\npublic:", 1)
    changed = RECEIPT_DECLARATIONS + changed.replace(BANK_ANCHOR, BANK_REPLACEMENT, 1)
    applied = dict(sources, **{key: changed.encode()})
    arm_root = BUILD / name
    fixture = arm_root / "fixture.cpp"
    fixture_bytes = wrapper(ident, ident == "specialists/fireflies").encode()
    wasm = BUILD / (name + ".wasm")
    # Whole production main.cpp contains dormant probe signed comparisons that
    # the native release build already accepts. Keep their warnings in the log
    # while retaining -Werror for other diagnostics; do not patch production.
    command = [str(core.CLANG), *wasi.FLAGS, "-Werror", *fixture_flags(name),
               f"-I{ROOT / 'sdk/cpp'}", str(fixture), f"-L{wasi.EH}", "-lunwind", "-o", str(wasm)]
    record = {"name": name, "snapshot": f"{ident}@{version}", "snapshot_meta": meta,
              "original_closure": original_hashes, "original_closure_sha256": canonical(original_hashes),
              "applied_closure": {key_name: sha(value) for key_name, value in applied.items()},
              "applied_closure_sha256": canonical({key_name: sha(value) for key_name, value in applied.items()}),
              "patch_anchor_counts": counts, "patch": {"class": [CLASS_ANCHOR, CLASS_ANCHOR + "\npublic:"],
              "bank": [BANK_ANCHOR, BANK_REPLACEMENT], "declarations": RECEIPT_DECLARATIONS},
              "wrapper": {"path": rel(fixture), "sha256": sha(fixture_bytes)},
              "compiler_command": command}
    record["source_patch_sha256"] = canonical(record["patch"])
    return record, applied, fixture_bytes


def compile_arm(name: str, ident: str, version: str, overrides: dict) -> dict:
    record, applied, fixture_bytes = arm_inputs(name, ident, version, overrides, prepare_missing=True)
    arm_root = BUILD / name
    for key, data in applied.items():
        put(arm_root / "repo" / key, data)
    fixture = arm_root / "fixture.cpp"
    put(fixture, fixture_bytes)
    wasm = checked_output(BUILD / (name + ".wasm"))
    checked_output(BUILD / name / "dependencies.d")
    with core.Slots(2, quiet=True):
        ok, log = wasi.compile_fixture(fixture, wasm, flags=fixture_flags(name))
    put(arm_root / "compile.log", log.encode())
    record.update({"compile_status": "pass" if ok else "fail",
                   "compile_log": {"path": rel(arm_root / "compile.log"), "sha256": sha(log.encode())}})
    if ok:
        record["dependencies"] = dependency_pins(name)
        record["wasm"] = {"path": rel(wasm), "sha256": sha(wasm.read_bytes()), "bytes": wasm.stat().st_size}
        if (record["wasm"]["sha256"], record["wasm"]["bytes"]) != EXPECTED_WASM[name]:
            record["compile_status"] = "artifact-mismatch"
            json_put(arm_root / "manifest.json", record)
            raise RuntimeError(f"{name}: compiled reactor differs from the reviewed exact fixture")
    json_put(arm_root / "manifest.json", record)
    if not ok:
        raise RuntimeError(f"{name}: compile failed; see {rel(arm_root / 'compile.log')}: {log[-1000:]}")
    print(f"built {name} from {ident}@{version}: {rel(wasm)}", flush=True)
    return record


def shared_record() -> dict:
    return {"schema": 1, "purpose": "Conditional actual-caller fixture, not a native bot executable or historical private-state reconstruction",
              **provenance(),
              "api": {"imports": {"fixture.bank_entry": "() -> void", "fixture.bank_result": "(i32 actual bank() return) -> void"},
                      "boot": "Call WASI _initialize once, then fixture_boot for real setup steps until mask 3; record offered host fuel per call, never silently refill inside a call",
                      "turn": "fixture_turn runs the entire production turn; receipt hooks do not bypass a bank decline",
                      "setters": ["fixture_set_partial(0|1): sensitivity intervention only", "fixture_force_recovering(): true-only negative; no false setter exists"]},
              "arms": []}


def check_manifest() -> dict:
    path = BUILD / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    expected = shared_record()
    actual_shared = {key: value for key, value in manifest.items() if key != "arms"}
    if actual_shared != {key: value for key, value in expected.items() if key != "arms"}:
        raise RuntimeError("compiled manifest no longer matches the current builder, SDK, bundle, compiler or helper pins; rebuild explicitly")
    records = manifest.get("arms")
    if not isinstance(records, list) or [record.get("name") for record in records] != list(ARMS):
        raise RuntimeError("compiled manifest arm names/order differ from the three reviewed arms")
    for record in records:
        name = record["name"]
        expected_arm, applied, fixture = arm_inputs(name, *ARMS[name])
        base = {key: value for key, value in record.items() if key not in ("wasm", "compile_log", "compile_status", "dependencies")}
        if base != expected_arm or record.get("compile_status") != "pass":
            raise RuntimeError(f"{name}: compiled manifest source, patch, flags or wrapper pins differ")
        repo = BUILD / name / "repo"
        found = {p.relative_to(repo).as_posix(): p.read_bytes() for p in repo.rglob("*") if p.is_file()}
        if found != applied or (BUILD / name / "fixture.cpp").read_bytes() != fixture:
            raise RuntimeError(f"{name}: generated source closure or wrapper changed")
        if record.get("dependencies") != dependency_pins(name):
            raise RuntimeError(f"{name}: compiler-reported dependency pins changed")
        log = record.get("compile_log", {})
        log_path = BUILD / name / "compile.log"
        if log != {"path": rel(log_path), "sha256": sha(log_path.read_bytes())}:
            raise RuntimeError(f"{name}: compile log pin changed")
        wasm_path = BUILD / (name + ".wasm")
        data = wasm_path.read_bytes()
        digest, size = EXPECTED_WASM[name]
        if data[:8] != b"\x00asm\x01\x00\x00\x00" or len(data) != size or sha(data) != digest or record.get("wasm") != {"path": rel(wasm_path), "sha256": digest, "bytes": size}:
            raise RuntimeError(f"{name}: reviewed reactor artifact changed")
        if json.loads((BUILD / name / "manifest.json").read_text(encoding="utf-8")) != record:
            raise RuntimeError(f"{name}: individual and combined manifests disagree")
    return manifest


def main() -> int:
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify existing compiled fixtures without recompiling or writing files")
    parser.add_argument("--build-dir", type=Path, default=BUILD, help="isolated build directory strictly inside .cache/arena/orchestration/fireflies-caller/")
    args = parser.parse_args()
    BUILD = (args.build_dir if args.build_dir.is_absolute() else ROOT / args.build_dir).resolve()
    if not CALLER_CACHE.resolve().is_relative_to(ROOT.resolve()) or BUILD == CALLER_CACHE.resolve() or not BUILD.is_relative_to(CALLER_CACHE.resolve()):
        raise RuntimeError("build output escaped the allowed caller cache scope")
    if args.check:
        check_manifest()
        print("verified 3/3 exact caller fixtures (no compilation)")
        return 0
    BUILD.mkdir(parents=True, exist_ok=True)
    with core.FileLock(checked_output(BUILD / "builder.lock")):
        shared = shared_record()
        # Check every live arm before preparing or compiling any output.
        for ident, version, overrides in ARMS.values():
            live_sources(ident, version, overrides)
        with ThreadPoolExecutor(max_workers=len(ARMS)) as executor:
            futures = [executor.submit(compile_arm, name, ident, version, overrides)
                       for name, (ident, version, overrides) in ARMS.items()]
            for future in futures:
                shared["arms"].append(future.result())
        if {key: value for key, value in shared.items() if key != "arms"} != {key: value for key, value in shared_record().items() if key != "arms"}:
            raise RuntimeError("compiler inputs changed while building caller fixtures")
        json_put(BUILD / "manifest.json", shared)
        check_manifest()
    print(f"manifest {rel(BUILD / 'manifest.json')}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"caller fixture build failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
