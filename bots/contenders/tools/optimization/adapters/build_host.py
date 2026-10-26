"""Compile a native host program while sharing arena's existing one-machine slot pool.

Usage: python -B -I build_host.py [--repo ROOT] -- COMPILER ARG ...
This owns one slot only while the compiler is alive. It creates no campaign/game ledger.
Outside an arena checkout, an explicit --standalone uses one compiler process directly.
"""
from __future__ import annotations

import argparse
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--standalone", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("compiler command is required after --")
    repo = args.repo.resolve()
    arena = repo / "tools" / "arena" / "core.py"
    if arena.is_file():
        sys.path.insert(0, str(arena.parent))
        from core import Slots
        slot = Slots(1)
    elif args.standalone:
        slot = contextlib.nullcontext()
    else:
        parser.error("arena slot pool is unavailable; use --standalone only outside an arena checkout")
    compiler = shutil.which(command[0])
    if compiler is None:
        parser.error("compiler executable was not found: " + command[0])
    command[0] = compiler
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    with slot:
        # Popen's context waits for this owned compiler on every exit path. Interrupts
        # stop it before releasing the CPU slot rather than abandoning a background build.
        with subprocess.Popen(command, cwd=repo if repo.is_dir() else None, env=env) as child:
            try:
                return child.wait()
            except KeyboardInterrupt:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                return 130


if __name__ == "__main__":
    raise SystemExit(main())
