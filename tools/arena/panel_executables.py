"""Prospective executable-byte audit for an existing population panel.

python tools/arena/panel_executables.py pin PANEL --out NEW_MANIFEST
python tools/arena/panel_executables.py validate MANIFEST

Both operations call the existing panel validator and read current cached binaries.
They never prepare, rebuild, assess, play, or substitute an executable. This is not
an attestation of bytes consumed by a later regression: the ordinary runner may
reconfigure/rebuild entrants and resume source-keyed games after this audit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402
import population  # noqa: E402
from core import ArenaError, ROOT  # noqa: E402

SCHEMA = "arena-panel-executables/v1"
SCOPE = "Prospective current opponent bytes only; no consumed-byte, fresh-game, health or admission attestation."
PRODUCER = Path(__file__).resolve()
HEX = re.compile(r"[0-9a-f]{64}")
SNAPSHOT = re.compile(r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)@([0-9a-f]{12})")
FILES = {"raw": "bot.wasm", "injected": "bot-injected.wasm"}


def _digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def _input_path(value: Path | str) -> Path:
    path = Path(value)
    return (path if path.is_absolute() else ROOT / path).resolve()


def _relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError as exc:
        raise ArenaError("panel audit paths must be inside the repository") from exc


def _descriptor(path: Path) -> dict:
    return {"path": _relative(path), "sha256": _digest(path)}


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ArenaError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _constant(value):
    raise ArenaError(f"non-finite JSON constant: {value}")


def _load(path: Path) -> tuple[dict, str]:
    raw = path.read_bytes()
    try:
        result = json.loads(raw.decode("utf-8"), object_pairs_hook=_object, parse_constant=_constant)
    except (ValueError, UnicodeError) as exc:
        raise ArenaError(f"invalid JSON: {_relative(path)}") from exc
    if not isinstance(result, dict):
        raise ArenaError("panel audit JSON must be an object")
    return result, hashlib.sha256(raw).hexdigest()


def _keys(value, expected: set[str], label: str) -> None:
    if not isinstance(value, dict) or set(value) != expected:
        raise ArenaError(f"malformed {label} fields")


def _record_path(record, label: str) -> Path:
    _keys(record, {"path", "sha256"}, label)
    value = record["path"]
    if (type(value) is not str or not value or "\\" in value or ":" in value or
            value.startswith("/") or any(part in ("", ".", "..") for part in value.split("/")) or
            type(record["sha256"]) is not str or HEX.fullmatch(record["sha256"]) is None):
        raise ArenaError(f"malformed {label} descriptor")
    path = ROOT / value
    if path.resolve() != path or _relative(path) != value:
        raise ArenaError(f"noncanonical {label} path")
    return path


def _snapshot_path(key: str) -> Path:
    match = SNAPSHOT.fullmatch(key) if isinstance(key, str) else None
    if not match or any(part in (".", "..") for part in match.groups()[:2]):
        raise ArenaError("invalid panel snapshot key")
    suite, name, version = match.groups()
    return core.CACHE / "snap" / version / "bots" / suite / name


def _expected(panel: dict) -> dict[str, Path]:
    entries = panel.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ArenaError("panel requires entries")
    result = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ArenaError("malformed panel entry")
        key = entry.get("snapshot")
        result[key] = _snapshot_path(key)
    return result


def _validated_panel(panel: dict) -> dict[str, Path]:
    expected = _expected(panel)
    entrants = population.validate_panel(panel)
    if not isinstance(entrants, list) or len(entrants) != len(panel["entries"]):
        raise ArenaError("existing panel validator returned inconsistent entrants")
    for entry, entrant in zip(panel["entries"], entrants):
        key = entry["snapshot"]
        if (entrant.key != key or not entrant.ok or Path(entrant.path).absolute() != expected[key]):
            raise ArenaError("validated entrant disagrees with canonical snapshot path")
    return expected


def _binary(path: Path) -> dict:
    # Reject redirects/junction escapes rather than hashing an alias somewhere else.
    if path.resolve() != path or not path.is_file():
        raise ArenaError(f"missing or noncanonical cached executable: {_relative(path)}")
    return _descriptor(path)


def _binding(key: str, directory: Path) -> dict:
    build = directory / ".cambc" / "build-release"
    return {"snapshot": key, **{label: _binary(build / filename) for label, filename in FILES.items()}}


def pin(panel_path: Path | str, output: Path | str) -> dict:
    """Pin current opponent bytes after existing panel validation; refuse overwrite.

    Pinning is prospective. It does not prove which executable any saved or future
    game consumed. Caller ownership must prevent concurrent cache/file mutation.
    """
    panel_path, output = _input_path(panel_path), _input_path(output)
    _relative(panel_path)
    _relative(output)
    if output.exists():
        raise ArenaError("executable manifest already exists; refuse overwrite")
    panel, panel_sha = _load(panel_path)
    expected = _validated_panel(panel)
    manifest = {"schema": SCHEMA, "scope": SCOPE,
                "panel": {"path": _relative(panel_path), "sha256": panel_sha},
                "producer": _descriptor(PRODUCER),
                "snapshots": [_binding(key, expected[key]) for key in sorted(expected)]}
    if _digest(panel_path) != panel_sha:
        raise ArenaError("panel changed while pinning")
    raw = (json.dumps(manifest, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation also protects against a writer arriving after exists().
    # A failed I/O leaves its partial new file; never replace/delete old evidence.
    with output.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    return manifest


def validate(manifest_path: Path | str) -> dict:
    """Validate only current bytes/metadata; never launch regression or rebuild."""
    manifest, _ = _load(_input_path(manifest_path))
    _keys(manifest, {"schema", "scope", "panel", "producer", "snapshots"}, "executable manifest")
    if manifest["schema"] != SCHEMA or manifest["scope"] != SCOPE:
        raise ArenaError("unsupported executable manifest schema/scope")
    producer_path = _record_path(manifest["producer"], "producer")
    if producer_path != PRODUCER or _descriptor(PRODUCER) != manifest["producer"]:
        raise ArenaError("executable manifest producer changed")
    panel_path = _record_path(manifest["panel"], "panel")
    panel, panel_sha = _load(panel_path)
    if panel_sha != manifest["panel"]["sha256"]:
        raise ArenaError("original panel bytes changed")
    expected = _expected(panel)
    rows = manifest["snapshots"]
    if not isinstance(rows, list) or not rows:
        raise ArenaError("executable manifest requires snapshot bindings")
    seen = set()
    for row in rows:
        _keys(row, {"snapshot", "raw", "injected"}, "snapshot binding")
        key = row["snapshot"]
        if not isinstance(key, str) or key not in expected or key in seen:
            raise ArenaError("extra, unknown or duplicate snapshot binding")
        seen.add(key)
        build = expected[key] / ".cambc" / "build-release"
        for label, filename in FILES.items():
            path = _record_path(row[label], label)
            if path != build / filename:
                raise ArenaError(f"noncanonical {label} executable path")
    if seen != set(expected):
        raise ArenaError("missing snapshot binding")
    _validated_panel(panel)  # preserve all existing source/override/map/toolchain/test gates
    for row in rows:
        if row != _binding(row["snapshot"], expected[row["snapshot"]]):
            raise ArenaError(f"cached executable bytes changed: {row['snapshot']}")
    if _digest(panel_path) != panel_sha:
        raise ArenaError("panel changed while validating")
    return manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("pin", help="pin current cached bytes; never overwrite a manifest")
    create.add_argument("panel", type=Path)
    create.add_argument("--out", type=Path, required=True)
    check = commands.add_parser("validate", help="audit current bytes; does not dispatch games")
    check.add_argument("manifest", type=Path)
    args = parser.parse_args(argv)
    try:
        manifest = pin(args.panel, args.out) if args.command == "pin" else validate(args.manifest)
    except (ArenaError, OSError) as exc:
        print(f"panel executable audit error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"command": args.command, "snapshot_bindings": len(manifest["snapshots"]),
                      "scope": SCOPE, "consumption_attested": False, "games": 0, "builds": 0}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
