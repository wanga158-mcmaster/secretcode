"""Small, content-addressed support layer for the suite-owned native evaluator.

The optimization algorithms live in the C++ executable. This adapter reuses arena's
builds, machine-wide slots, full games, and health parsers; it never contacts servers.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import zipfile


class EvaluationError(RuntimeError):
    pass


def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def digest(value) -> str:
    return hashlib.sha256(canonical(value)).hexdigest()


def file_digest(path: Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def descriptor(path: Path, role: str = "") -> dict:
    p = Path(path).resolve()
    return {"path": str(p), "sha256": file_digest(p), "size": p.stat().st_size, "role": role}


def read_json(path: Path):
    def reject(value):
        raise EvaluationError(f"non-finite JSON value {value}")
    def pairs(values):
        out = {}
        for key, value in values:
            if key in out:
                raise EvaluationError(f"duplicate JSON key {key!r}")
            out[key] = value
        return out
    with Path(path).open("r", encoding="utf-8-sig") as f:
        return json.load(f, parse_constant=reject, object_pairs_hook=pairs)


def write_json(path: Path, value) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False,
                      allow_nan=False).encode("utf-8") + b"\n"
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temporary, path)
    finally:
        if Path(temporary).exists():
            Path(temporary).unlink()


def load_arena(repo: Path):
    repo = Path(repo).resolve()
    if not (repo / "tools/arena/core.py").is_file():
        raise EvaluationError(f"not a Battlecode repository: {repo}")
    sys.path.insert(0, str(repo / "tools/arena"))
    import core
    import games
    if core.ROOT.resolve() != repo:
        raise EvaluationError("one evaluator process cannot mix repository roots")
    return core, games


_NUMBER = re.compile(r"[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?[fF]?")
_KNOB = re.compile(r"[A-Za-z_]\w*")
_SELECTOR = re.compile(r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)(?:@([0-9a-f]{12}))?")


def cpp_value(value) -> str:
    """Only data literals enter C++; configuration files cannot inject source code."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        if abs(value) > 2**53:
            raise EvaluationError("knob integer exceeds the exact JSON integer domain")
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise EvaluationError("non-finite knob")
        return repr(value)
    if isinstance(value, list):
        if not value or len(value) > 256 or any(isinstance(v, (dict, list)) for v in value):
            raise EvaluationError("knob arrays must contain 1..256 scalar literals")
        return "{" + ", ".join(cpp_value(v) for v in value) + "}"
    if isinstance(value, str):
        text = value.strip()
        if text in ("true", "false"):
            return text
        if _NUMBER.fullmatch(text) and math.isfinite(float(text.rstrip("fF"))):
            return text
        if text.startswith("{") and text.endswith("}"):
            fields = [v.strip() for v in text[1:-1].split(",")]
            if fields and len(fields) <= 256 and all(v in ("true", "false") or
                    (_NUMBER.fullmatch(v) and math.isfinite(float(v.rstrip("fF")))) for v in fields):
                return "{" + ", ".join(fields) + "}"
    raise EvaluationError("knob values must be finite numeric/bool data or flat literal arrays")


def knobs(value) -> dict[str, str]:
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise EvaluationError("knobs must be an object")
    out = {}
    for key, val in value.items():
        if not isinstance(key, str) or not _KNOB.fullmatch(key):
            raise EvaluationError(f"invalid knob name {key!r}")
        out[key] = cpp_value(val)
    return out


def exact_entrant(core, selector: str, label: str = ""):
    if not isinstance(selector, str) or not (m := _SELECTOR.fullmatch(selector)):
        raise EvaluationError(f"invalid bot selector {selector!r}; use suite/bot or suite/bot@hash")
    b = core.Bot(m[1], m[2])
    version = m[3]
    if not version:
        if not (b.path / "src/main.cpp").is_file():
            raise EvaluationError(f"bot source missing: {b.id}")
        ent = core.prepare(b, label=label)
    else:
        snap = core.CACHE / "snap" / version
        target = snap / b.path.relative_to(core.ROOT)
        if not (snap / ".ready").is_file() or not (target / "src/main.cpp").is_file():
            # Anchors are immutable repository inputs included in native packets.
            # A cold worker can rebuild that exact source pin without carrying an
            # arena cache. Generated/mutable bots still require an exact recipe.
            if b.suite != "anchors" or not (b.path / "src/main.cpp").is_file():
                raise EvaluationError(f"exact snapshot unavailable: {selector}; no working-source fallback")
            ent = core.prepare(b, label=label)
            if not ent.ok:
                raise EvaluationError(f"frozen anchor rebuild failed for {selector}: {ent.log[-3000:]}")
            if ent.key != selector:
                raise EvaluationError(f"frozen anchor source changed: expected {selector}, got {ent.key}")
            return ent
        stamp = snap / ".toolchain"
        if not stamp.is_file() or stamp.read_text(encoding="utf-8").strip() != core.toolchain():
            raise EvaluationError(f"exact snapshot has a different toolchain: {selector}")
        ent = core.Entrant(b, version, target, True, "", (), label)
    if not ent.ok:
        raise EvaluationError(f"build failed for {selector}: {ent.log[-3000:]}")
    return ent


def entrant_identity(core, ent) -> dict:
    build = ent.path / ".cambc/build-release"
    modules = {}
    for role, name in (("raw", "bot.wasm"), ("injected", "bot-injected.wasm")):
        p = build / name
        if not p.is_file():
            raise EvaluationError(f"missing built {role} WASM for {ent.key}")
        modules[role] = {"sha256": file_digest(p), "size": p.stat().st_size}
    contents = core.read_closure(ent.bot) if ent.path == ent.bot.path else {
        p: p.read_bytes() for p in ent.path.joinpath("src").rglob("*") if p.is_file()}
    h = hashlib.sha256()
    for p, data in sorted(contents.items(), key=lambda item: str(item[0])):
        rel = p.relative_to(ent.path).as_posix()
        h.update(rel.encode() + b"\0" + data + b"\0")
    return {"key": ent.key, "source_sha256": h.hexdigest(), "modules": modules,
            "input_route": "arena project path; exact raw and injected bytes both pinned"}


def environment_files(repo: Path, core, include_program: bool = False) -> dict:
    files = {}
    paths = [repo / p for p in ("docs/reference/current-version.json", "docs/reference/runtime-catalog.json",
             "sdk/cpp/cambc.h", "sdk/cpp/imports.h", "cmake/CMakeLists.txt", "cambc.toml",
             "tools/arena/core.py", "tools/arena/games.py", "tools/arena/stats.py")]
    paths += [Path(__file__), Path(__file__).with_name("native_evaluator.py"),
              Path(__file__).with_name("config_materializer.py")]
    if include_program:
        paths += [Path(__file__).with_name("program_materializer.py"),
                  Path(__file__).with_name("policy_runtime.h"),
                  Path(__file__).with_name("program_runtime.h")]
    for p in paths:
        if p.is_file():
            name = p.relative_to(repo).as_posix() if p.is_relative_to(repo) else p.name
            files[name] = file_digest(p)
    for key, p in (("cli", core.CAMBC), ("wasi_compiler", core.CLANG)):
        if not p.is_file():
            raise EvaluationError(f"missing native dependency {key}: {p}")
        files[key] = file_digest(p)
    return files


def export_entrant(core, ent, store: Path) -> tuple[dict, list[dict]]:
    """Export immutable WASM and source inputs, not another versioned bot directory."""
    store.mkdir(parents=True, exist_ok=True)
    artifacts = []
    modules = {}
    for role, filename in (("raw", "bot.wasm"), ("injected", "bot-injected.wasm")):
        src = ent.path / ".cambc/build-release" / filename
        sha = file_digest(src)
        target = store / f"{sha}.wasm"
        with core.FileLock(store / "locks" / f"{sha}.lock"):
            if target.exists():
                if file_digest(target) != sha:
                    raise EvaluationError(f"corrupt content-addressed artifact {target}")
            else:
                fd, tmp = tempfile.mkstemp(prefix=sha + ".", dir=store)
                try:
                    with os.fdopen(fd, "wb") as f:
                        f.write(src.read_bytes())
                        f.flush()
                        os.fsync(f.fileno())
                    os.replace(tmp, target)
                finally:
                    if Path(tmp).exists():
                        Path(tmp).unlink()
        modules[role] = descriptor(target, f"{role}_wasm")
        artifacts.append(modules[role])
    sources = []
    for src in sorted(ent.path.joinpath("src").rglob("*")):
        if src.is_file():
            raw = src.read_bytes()
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError as exc:
                raise EvaluationError(f"non-UTF8 source input {src}") from exc
            sources.append({"path": src.relative_to(ent.path).as_posix(),
                            "sha256": hashlib.sha256(raw).hexdigest(), "text": text})
    bundle = {"schema": "contenders-source-inputs/v1", "key": ent.key,
              "overrides": dict(ent.overrides), "files": sources}
    body = canonical(bundle)
    sha = hashlib.sha256(body).hexdigest()
    target = store / f"{sha}.sources.zip"
    with core.FileLock(store / "locks" / f"{sha}.lock"):
        if not target.exists():
            fd, tmp = tempfile.mkstemp(prefix=sha + ".", suffix=".zip", dir=store)
            os.close(fd)
            try:
                with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
                    z.writestr("sources.json", body)
                os.replace(tmp, target)
            finally:
                if Path(tmp).exists():
                    Path(tmp).unlink()
        else:
            with zipfile.ZipFile(target) as z:
                if hashlib.sha256(z.read("sources.json")).hexdigest() != sha:
                    raise EvaluationError(f"corrupt source artifact {target}")
    source_artifact = descriptor(target, "source_inputs")
    artifacts.append(source_artifact)
    return {"key": ent.key, "wasm": modules, "sources": source_artifact}, artifacts


def load_materializer():
    path = Path(__file__).with_name("program_materializer.py")
    if not path.is_file():
        raise EvaluationError("program/policy materializer is not installed")
    spec = importlib.util.spec_from_file_location("optimizer_program_materializer", path)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module
