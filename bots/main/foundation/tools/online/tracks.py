"""Per-round champion tracks for every local online replay (read-only replay inspection).

usage: python -I -X utf8 tracks.py [--workers 4] [--only-summarised]
For each match with a local replay bundle (our group.zip files, ranked match.zip, main's cached
replay zips) runs tools/inspect-strategy-replay.mjs --actor all and stores the compact result as
.cache/contenders/online/tracks/<matchId>.json.gz: {map, w, h, tracks: [{id, team, type, s: [[round,
x, y, hp, gold, gems], ...]}], captures: [...]}. Existing outputs are skipped.
"""
import concurrent.futures as cf
import gzip
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import OUT, ROOT, extract_replay, replay_sources, summary_paths

NODE = r"C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe"
INSPECT = ROOT / "tools" / "inspect-strategy-replay.mjs"
TR = OUT / "tracks"


def one(mid, src):
    dest = TR / f"{mid}.json.gz"
    if dest.exists():
        return mid, "have"
    with tempfile.TemporaryDirectory(dir=OUT / "tmp") as td:
        rp = os.path.join(td, "m.zip")
        try:
            extract_replay(src, rp)
        except Exception as e:
            return mid, f"extract {type(e).__name__}"
        p = subprocess.run([NODE, str(INSPECT), "--replay", rp, "--actor", "all"], capture_output=True,
                           timeout=600)
        if p.returncode != 0:
            return mid, "inspect failed: " + p.stderr.decode("utf-8", "replace").strip().splitlines()[-1][:120]
        d = json.loads(p.stdout.decode("utf-8"))
    out = {"map": d.get("mapId"), "w": d.get("width"), "h": d.get("height"),
           "tracks": [{"id": t["id"], "team": t["team"], "type": t["type"], "s": t["samples"]} for t in d["tracks"]],
           "captures": d.get("captures", [])}
    with gzip.open(dest, "wt", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))
    return mid, "ok"


def main():
    workers = 4
    if "--workers" in sys.argv:
        workers = int(sys.argv[sys.argv.index("--workers") + 1])
    TR.mkdir(parents=True, exist_ok=True)
    (OUT / "tmp").mkdir(parents=True, exist_ok=True)
    src = replay_sources()
    if "--only-summarised" in sys.argv:
        sp = summary_paths()
        src = {k: v for k, v in src.items() if k in sp}
    todo = {k: v for k, v in src.items() if not (TR / f"{k}.json.gz").exists()}
    print(f"replays {len(src)}, to do {len(todo)}", flush=True)
    stat = {}
    with cf.ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(one, k, v) for k, v in todo.items()]
        for i, f in enumerate(cf.as_completed(futs)):
            mid, st = f.result()
            key = st.split(":")[0]
            stat[key] = stat.get(key, 0) + 1
            if st not in ("ok", "have"):
                print(mid, st, flush=True)
            if (i + 1) % 100 == 0:
                print(f"{i + 1}/{len(todo)} {stat}", flush=True)
    print("done", stat, flush=True)


if __name__ == "__main__":
    main()
