"""Where our champions' rounds go in real games (bots/main, D334): per-champion time classes up to the
round our team banks 1000 gems, from a local run's statistics (*.json next to each replay) and
per-round tracks (tools/inspect-strategy-replay.mjs --actor all, cached).

  python -B -X utf8 bots/main/tools/econ/execgap.py RUN_DIR [--side A] [--out FILE.tsv]

Classes per champion-round, first match wins: dead; farm (dealt monster damage); walk (moved one
step); recall (jumped home); then a stationary round takes the class of what ends its stillness:
capture (gold or gems gained), tribute (gems banked), recall (teleport home), wait (fighting starts),
idle (anything else, e.g. it walks off again).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NODE = Path(r"C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe")
INSPECT = ROOT / "tools" / "inspect-strategy-replay.mjs"
CLASSES = ["farm", "walk", "capture", "tribute", "recall", "wait", "idle", "dead"]


def expand(runs: list) -> list:
    out = []
    for r in runs:
        out += [float(r["value"])] * int(r["duration"])
    return out


def tracks_for(replay: Path, cache: Path) -> dict:
    if not cache.exists():
        cache.parent.mkdir(parents=True, exist_ok=True)
        res = subprocess.run([str(NODE), str(INSPECT), "--replay", str(replay), "--actor", "all"], capture_output=True)
        if res.returncode != 0:
            raise RuntimeError(f"inspect failed on {replay.name}: {res.stderr[-300:]!r}")
        cache.write_bytes(res.stdout)
    return json.loads(cache.read_text(encoding="utf-8"))


def analyse(stats: dict, tr: dict, team: int) -> dict:
    tname = "teamA" if team == 0 else "teamB"
    sub = next(expand(t["runs"]) for t in stats["teamTimelines"] if t["metric"] == "submittedGems" and t["owner"] == tname)
    end = next((i for i, v in enumerate(sub) if v >= 1000), len(sub) - 1)
    dmg = {t["owner"]["id"]: expand(t["runs"]) for t in stats["championTimelines"]
           if t["metric"] == "monsterDamageDone" and t["owner"].get("team") == tname}
    out = {"end": end, "champs": {}}
    for track in tr["tracks"]:
        if track["team"] != team:
            continue
        cid = track["id"]
        d = dmg.get(cid, [])
        rows = {s[0]: s for s in track["samples"]}
        cls = []
        for r in range(1, end + 1):
            s, p = rows.get(r), rows.get(r - 1)
            if s is None or s[3] <= 0:
                cls.append("dead")
                continue
            # statistics sample i holds the value after round i (sample 0 is the start)
            farm = r < len(d) and d[r] > d[r - 1]
            if farm:
                cls.append("farm")
            elif p is not None and p[3] > 0 and max(abs(s[1] - p[1]), abs(s[2] - p[2])) > 2:
                cls.append("recall")
            elif p is not None and (s[1], s[2]) != (p[1], p[2]):
                cls.append("walk")
            else:
                cls.append(None)
        # stationary rounds take the class of what ends them
        r = len(cls) - 1
        nxt = "idle"
        while r >= 0:
            c = cls[r]
            if c is not None:
                s, p = rows.get(r + 1), rows.get(r)
                nxt = {"farm": "wait", "recall": "recall", "walk": "idle", "dead": "idle"}[c]
            else:
                s, p = rows.get(r + 1), rows.get(r)
                if s and p and (s[4] > p[4] or s[5] > p[5]):
                    nxt = "capture"
                elif s and p and s[5] < p[5]:
                    nxt = "tribute"
                cls[r] = nxt
            r -= 1
        out["champs"][cid] = {"type": track["type"], "classes": Counter(cls)}
    caps = [c for c in tr.get("captures", []) if c.get("capturingTeam") == team and c["round"] <= end]
    out["captures"] = len(caps)
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("run")
    p.add_argument("--side", default="A")
    p.add_argument("--out", default="")
    a = p.parse_args()
    run = Path(a.run)
    rows = [json.loads(line) for line in (run / "games.jsonl").read_text(encoding="utf-8").splitlines() if line.strip()]
    team = 0 if a.side == "A" else 1
    lines = ["map\tend\tcaptures\t" + "\t".join(CLASSES)]
    tot = defaultdict(float)
    n = 0
    for row in sorted(rows, key=lambda x: x["map"]):
        replay = ROOT / row["replay"]
        stats = json.loads(replay.with_suffix(".json").read_text(encoding="utf-8"))
        tr = tracks_for(replay, run / "tracks" / (replay.stem + ".json"))
        res = analyse(stats, tr, team)
        k = max(1, len(res["champs"]))
        per = {c: sum(ch["classes"][c] for ch in res["champs"].values()) / k for c in CLASSES}
        lines.append(f"{row['map']}\t{res['end']}\t{res['captures']}\t" + "\t".join(f"{per[c]:.0f}" for c in CLASSES))
        for c in CLASSES:
            tot[c] += per[c] / max(1, res["end"])
        n += 1
    lines.append("share\t\t\t" + "\t".join(f"{100 * tot[c] / max(1, n):.1f}%" for c in CLASSES))
    text = "\n".join(lines)
    if a.out:
        Path(a.out).write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
