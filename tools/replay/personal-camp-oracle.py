"""Contained entry for the generic, bounded read-only personal camp oracle.

Only the configured Node and fixed getter utility are admitted. The unchanged
replay bridge owns the private Windows Job/Slot and confirms cleanup before
forwarding. Receipt storage belongs to this oracle, not the shared Arena owner.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("personal_camp_replay_bridge", ROOT / "tools/replay-native-slot.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


def direct(value, *, directory=False):
    path = Path(os.path.abspath(value))
    exists = path.is_dir() if directory else path.is_file()
    if not exists or path.is_symlink() or os.path.normcase(str(path)) != os.path.normcase(str(path.resolve(strict=True))):
        raise ValueError("Oracle inputs must be direct regular paths")
    return path


def inside(value, parent):
    path = direct(value)
    if not path.is_relative_to(parent):
        raise ValueError("Oracle input is outside its declared workspace area")
    return path


def invocation(scene_file, scene_sha256):
    if not re.fullmatch("[a-f0-9]{64}", scene_sha256):
        raise ValueError("Expected a full scene SHA256")
    scene_file = inside(scene_file, ROOT / ".cache")
    raw = scene_file.read_bytes()
    if len(raw) > 65536 or hashlib.sha256(raw).hexdigest() != scene_sha256:
        raise ValueError("Bounded scene pin differs")
    scene = json.loads(raw)
    if (not isinstance(scene, dict) or set(scene) != {"schema", "replay", "map", "actor", "team", "round", "sample"}
            or scene["schema"] != "arena-personal-camp-oracle/v1"):
        raise ValueError("Unknown oracle scene")
    if (type(scene["actor"]) is not int or not 0 <= scene["actor"] < 16
            or type(scene["team"]) is not int or scene["team"] not in (0, 1)
            or type(scene["round"]) is not int or not 0 <= scene["round"] <= 2000
            or type(scene["sample"]) is not bool):
        raise ValueError("Oracle scene requires one bounded personal checkpoint")
    for key, parent, suffixes, fields in (
        ("replay", ROOT, {".zip", ".repl26a"}, {"path", "sha256"}),
        ("map", ROOT / "maps", {".map26a"}, {"path", "sha256", "id"}),
    ):
        value = scene[key]
        if (not isinstance(value, dict) or set(value) != fields or type(value["path"]) is not str
                or type(value["sha256"]) is not str or not re.fullmatch("[a-f0-9]{64}", value["sha256"])):
            raise ValueError("Invalid oracle input pin")
        file = inside(ROOT / value["path"], parent)
        if file.suffix not in suffixes or hashlib.sha256(file.read_bytes()).hexdigest() != value["sha256"]:
            raise ValueError("Oracle input bytes/type differ")
    if type(scene["map"]["id"]) is not str or not re.fullmatch("[A-Za-z0-9_-]+", scene["map"]["id"]):
        raise ValueError("Invalid map identifier")
    node = direct(bridge.core.NODE)
    script = direct(HERE / "personal-camp-oracle.mjs")
    return [str(node), "--disable-warning=ExperimentalWarning", str(script),
            "--spec", str(scene_file), "--spec-sha256", scene_sha256]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", required=True)
    parser.add_argument("--spec-sha256", required=True)
    args = parser.parse_args(argv)
    command = invocation(args.spec, args.spec_sha256)
    receipts = ROOT / ".cache/altar-camp-oracle/transport"
    # Explicitly approved receipt-location adaptation; transport/argv/Job semantics
    # remain those of the unchanged bridge. Reject ancestor redirects before mkdir.
    if receipts.resolve() != receipts or not receipts.is_relative_to(ROOT / ".cache/altar-camp-oracle"):
        raise ValueError("Oracle receipt directory is redirected")
    receipts.mkdir(parents=True, exist_ok=True)
    direct(receipts, directory=True)
    bridge.RAW_ROOT = receipts
    return bridge._run_validated(command)


if __name__ == "__main__":
    raise SystemExit(main())
