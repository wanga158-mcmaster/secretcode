"""Slot-aware launcher for the pinned native gameplay branch oracle."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--node", type=Path)
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--seed-replay", type=Path)
    parser.add_argument("--sampler", type=Path)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--driver", type=Path, help="Explicit trusted internal native-oracle module")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    repo = args.repo.resolve()
    sys.path.insert(0, str(repo / "tools" / "arena"))
    try:
        from core import NODE, Slots
    except ImportError as error:
        raise RuntimeError("Oracle packet needs tools/arena/core.py for the shared slot pool") from error
    candidates = [args.node, os.environ.get("OPT_NODE"), os.environ.get("ARENA_NODE"),
                  Path(__file__).resolve().parents[1] / "runtime" / "node" / "node.exe", NODE,
                  shutil.which("node")]
    node = next((Path(p) for p in candidates if p and Path(p).is_file()), None)
    if node is None:
        raise RuntimeError("Node is missing; pass --node or include the packet runtime")
    command = [str(node), str((args.driver or Path(__file__).with_suffix(".mjs")).resolve()), "--request", str(args.request.resolve()),
               "--response", str(args.response.resolve()), "--repo", str(repo)]
    for key in ("engine", "seed_replay", "sampler", "profiles"):
        value = getattr(args, key)
        if value:
            command.extend(["--" + key.replace("_", "-"), str(value.resolve())])
    args.response.parent.mkdir(parents=True, exist_ok=True)
    stdout = args.response.with_suffix(".stdout.log")
    stderr = args.response.with_suffix(".stderr.log")
    with Slots(1, quiet=True), stdout.open("wb") as out, stderr.open("wb") as err:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=out, stderr=err, timeout=args.timeout)
    if result.returncode:
        raise RuntimeError("Native oracle failed; see " + str(stderr))
    body = json.loads(args.response.read_text(encoding="utf-8"))
    if body.get("schema") != "contenders-native-oracle/v1":
        raise RuntimeError("Native oracle response schema mismatch")
    print(json.dumps({"response": str(args.response), "native": body.get("native"),
                      "results": len(body.get("results", []))}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
        print("native oracle error: " + str(error), file=sys.stderr)
        raise SystemExit(1)
