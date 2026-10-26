"""arena: build, play, sweep, gate and rank every bot in bots/<suite>/<bot>/.

Run from anywhere:  python tools/arena <command> ...   (or arena.cmd from the repo root)
Each module below contributes subcommands through register(subparsers).
"""
import argparse
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
MODULES = ["core", "games", "ladder", "sweep", "gate", "release", "report", "scaffold",
           "population", "orchestrate", "exp"]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="arena", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True, metavar="command")
    for name in MODULES:
        try:
            module = importlib.import_module(name)
            module.register(sub)
        except ModuleNotFoundError as exc:
            if exc.name != name:
                print(f"arena: module {name} unavailable: {exc}", file=sys.stderr)
        except Exception as exc:  # a broken optional module must not take the other commands down
            if name == "core":
                raise
            print(f"arena: module {name} failed to load: {exc!r}", file=sys.stderr)
    args = parser.parse_args(argv)
    try:
        return int(args.func(args) or 0)
    except KeyboardInterrupt:
        return 130
    except Exception as exc:  # report tool errors briefly; tracebacks with ARENA_DEBUG=1
        import os
        from core import ArenaError
        if isinstance(exc, ArenaError) and not os.environ.get("ARENA_DEBUG"):
            print(f"arena: {exc}", file=sys.stderr)
            return getattr(exc, "exit_code", 2)  # 3 = stopped at --max-minutes, rerun to continue
        raise


if __name__ == "__main__":
    sys.exit(main())
