"""Independent current-toolchain reproduction of a released source package.

The producer performs two real cold builds through core's contained API. The
consumer validates their inputs, execution evidence and retained outputs again;
supplied module hashes or a pass document never replace those executions.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import uuid
import zipfile

import core
from core import ArenaError

SCHEMA = "arena.released-executable-reproduction.v1"
CLAIM = "Current-toolchain reproduction only; historical server executable bytes are not established."


def _hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _direct(path):
    path = Path(path)
    if (not path.is_absolute() or path.resolve() != path or path.is_symlink() or
            path.exists() and core._is_reparse(path)):
        raise ArenaError("released reproduction input/output is redirected or not absolute")
    return path


def _file(path):
    path = _direct(path)
    if not path.is_file():
        raise ArenaError(f"released reproduction file is missing: {path}")
    data = path.read_bytes()
    return {"path": path.relative_to(core.ROOT).as_posix(), "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def _json(path):
    try:
        value = json.loads(_direct(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ArenaError("released reproduction document is unreadable") from exc
    if not isinstance(value, dict):
        raise ArenaError("released reproduction document must be an object")
    return value


def _owner_path(value, owner):
    root = core.resolve_snapshot_root(core.ROOT / ".cache" / owner / "snap", owner)
    path = Path(value)
    if not path.is_absolute():
        path = core.ROOT / path
    base = root.parent
    if (not path.is_relative_to(base) or path == base or any(part in (".", "..") for part in path.parts)
            or path.is_relative_to(root)):
        raise ArenaError("released reproduction evidence is outside its owner's evidence namespace")
    return _direct(path)


def _source_files(path):
    result, pending = {}, [_direct(path)]
    while pending:
        directory = pending.pop()
        for child in sorted(directory.iterdir()):
            _direct(child)
            if child.is_dir():
                pending.append(child)
            elif child.is_file():
                result[child.relative_to(path).as_posix()] = _file(child)
            else:
                raise ArenaError("released reproduction source is not a regular file")
    return dict(sorted(result.items()))


def _released(expected_record=None):
    live_file = core.ROOT / "results/releases/LIVE.json"
    live = _json(live_file)
    name = live.get("name")
    if (type(name) is not str or not re.fullmatch(r"[a-z0-9][a-z0-9.-]*", name) or name.endswith(".")):
        raise ArenaError("released reproduction has no exact LIVE release name")
    directory = core.ROOT / "results/releases" / name
    record_file, bundle = directory / "release.json", directory / "main.zip"
    record = _json(record_file)
    if (expected_record is not None and record != expected_record or
            any(record.get(field) != live.get(field) for field in ("name", "anchor", "key"))):
        raise ArenaError("released reproduction independent LIVE/record identity changed")
    bot_id = record.get("bot")
    if type(bot_id) is not str or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*", bot_id):
        raise ArenaError("released reproduction original logical bot identity is missing")
    suite, bot_name = bot_id.split("/")
    bot = core.Bot(suite, bot_name)
    contents, digests = {}, {}
    try:
        with zipfile.ZipFile(_direct(bundle)) as archive:
            entries = [entry for entry in archive.infolist() if not entry.is_dir()]
            names = [entry.filename for entry in entries]
            if not names or len(set(name.casefold() for name in names)) != len(names):
                raise ArenaError("released reproduction bundle has empty or colliding source members")
            for entry in entries:
                path = PurePosixPath(entry.filename)
                if (entry.filename != path.as_posix() or "\\" in entry.filename or path.is_absolute() or len(path.parts) < 2 or path.parts[0] != "src" or
                        (entry.external_attr >> 16) & 0o170000 == 0o120000 or
                        any(part in (".", "..") or ":" in part or part.endswith((".", " ")) or
                            re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
                            for part in path.parts)):
                    raise ArenaError("released reproduction bundle has an unsafe source member")
                data = archive.read(entry)
                relative = "/".join(path.parts[1:])
                logical = core.ROOT / "bots" / suite / bot_name / "src" / relative
                contents[logical] = data
                digests[relative] = {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
    except (OSError, zipfile.BadZipFile, RuntimeError) as exc:
        raise ArenaError("released reproduction independent source bundle is unreadable") from exc
    key = f"{bot_id}@{core.version_of(contents)}"
    if record.get("key") != key:
        raise ArenaError("released reproduction bundle differs from its original logical source key")
    bindings = {"name": name, "anchor": record["anchor"], "bot": bot_id, "key": key,
                "live": _file(live_file), "record": _file(record_file), "bundle": _file(bundle), "sources": digests}
    return bot, contents, bindings


def _reference(owner, key):
    root = core.resolve_snapshot_root(core.ROOT / ".cache" / owner / "snap", owner)
    ent = core.cached_snapshot(key, snapshot_root=root, snapshot_owner=owner)
    if ent.overrides:
        raise ArenaError("released reproduction reference cannot have configuration overrides")
    snap = root / ent.version
    sources = _source_files(ent.path / "src")
    bindings = {"key": ent.key, "root": root.relative_to(core.ROOT).as_posix(), "sources": sources,
                "meta": _file(snap / "meta.json"), "toolchain": _file(snap / ".toolchain"),
                "raw": _file(ent.path / ".cambc/build-release/bot.wasm"),
                "sdk": _file(ent.path / ".cambc/build-release/bot-injected.wasm")}
    return ent, bindings


def _comparison(pair, reference, released):
    if (not isinstance(pair, dict) or not isinstance(pair.get("builds"), list) or len(pair["builds"]) != 2 or
            len(set(pair.get("roots", []))) != 2):
        raise ArenaError("released reproduction requires two distinct actual cold builds")
    actual_sources = {name: {"sha256": value["sha256"], "size": value["size"]}
                      for name, value in reference["sources"].items()}
    if actual_sources != released["sources"]:
        raise ArenaError("released reproduction prepared reference source differs from released bundle")
    for build in pair["builds"]:
        for role in ("raw", "sdk"):
            value = build.get(role)
            if (not isinstance(value, dict) or value.get("sha256") != reference[role]["sha256"] or
                    value.get("size") != reference[role]["size"]):
                raise ArenaError("released reproduction cold RAW/SDK output differs from prepared reference")
    return {"builds": 2, "raw": reference["raw"]["sha256"], "sdk": reference["sdk"]["sha256"]}


def produce(owner, reference_key, *, out, roots, timeout=1800):
    """Run both cold builds of independently released bytes; never reuse ready snapshots."""
    output = _owner_path(out, owner)
    if output.exists():
        raise ArenaError("released reproduction output already exists")
    _, reference = _reference(owner, reference_key)
    bot, contents, released = _released()
    pair = core.cold_build_pair(bot, contents, owner=owner, roots=roots, expected_key=released["key"], timeout=timeout)
    pair = core.validate_cold_build_pair(pair, bot, contents, owner=owner, expected_key=released["key"])
    comparison = _comparison(pair, reference, released)
    if _released()[2] != released or _reference(owner, reference_key)[1] != reference:
        raise ArenaError("released reproduction independent inputs changed during cold execution")
    value = {"schema": SCHEMA, "owner": owner, "reference": reference, "released": released,
             "pair": pair, "comparison": comparison, "claim": CLAIM}
    value["fingerprint"] = _hash(value)
    _owner_path(output, owner).parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name("." + output.name + "-" + uuid.uuid4().hex + ".tmp")
    try:
        with _owner_path(temporary, owner).open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=1, allow_nan=False)
            stream.write("\n")
        _owner_path(output, owner)
        if output.exists():
            raise ArenaError("released reproduction output appeared during publication")
        temporary.rename(output)
    finally:
        temporary.unlink(missing_ok=True)
    return _file(output)


def verify(proof, *, reference, release_record):
    """Read-only independent revalidation of retained two-build production evidence."""
    try:
        owner = reference.path.relative_to(core.ROOT / ".cache").parts[0]
        if not isinstance(proof, dict) or set(proof) != {"path", "size", "sha256"}:
            raise ArenaError("released reproduction proof must be an exact file descriptor")
        path = _owner_path(proof["path"], owner)
        if _file(path) != proof:
            raise ArenaError("released reproduction proof bytes changed")
        value = _json(path)
        if (set(value) != {"schema", "owner", "reference", "released", "pair", "comparison", "claim", "fingerprint"} or
                value.get("schema") != SCHEMA or value.get("owner") != owner or value.get("claim") != CLAIM or
                value.get("fingerprint") != _hash({k: v for k, v in value.items() if k != "fingerprint"})):
            raise ArenaError("released reproduction proof schema or binding differs")
        bot, contents, released = _released(release_record)
        ent, prepared = _reference(owner, reference.key)
        if ent.path != reference.path or value["reference"] != prepared or value["released"] != released:
            raise ArenaError("released reproduction proof has another prepared or independent released input")
        pair = core.validate_cold_build_pair(value["pair"], bot, contents, owner=owner, expected_key=released["key"])
        if pair != value["pair"] or _comparison(pair, prepared, released) != value["comparison"]:
            raise ArenaError("released reproduction cold execution or payload comparison differs")
        if _file(path) != proof or _released(release_record)[2] != released or _reference(owner, reference.key)[1] != prepared:
            raise ArenaError("released reproduction evidence changed during verification")
    except (OSError, KeyError, TypeError, ValueError) as exc:
        raise ArenaError("released reproduction evidence is missing or invalid") from exc
    return {"proof": proof, "fingerprint": value["fingerprint"], "claim": CLAIM}
