"""Regenerate every online.md table from local data, in dependency order (read-only, no server).

usage: python -B -I -X utf8 all.py [--skip-tracks]
Logs each step's output to .cache/contenders/online/logs/<step>.txt.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
from common import OUT

STEPS = ["sides.py", "tracks.py", "trackfeat.py", "deathctx.py", "drafts.py", "econ.py", "items.py", "fights.py",
         "catalog.py", "deathtab.py", "profiles.py", "extra.py"]


def main():
    logs = OUT / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    for step in STEPS:
        if step == "tracks.py" and "--skip-tracks" in sys.argv:
            continue
        p = subprocess.run([sys.executable, "-B", "-I", "-X", "utf8", str(HERE / step)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        (logs / f"{step[:-3]}.txt").write_text(p.stdout + p.stderr, encoding="utf-8")
        print(f"{step:14s} exit {p.returncode}: {(p.stdout.strip().splitlines() or [''])[-1][:100]}", flush=True)
        if p.returncode:
            print(p.stderr[-2000:])
            sys.exit(1)


if __name__ == "__main__":
    main()
