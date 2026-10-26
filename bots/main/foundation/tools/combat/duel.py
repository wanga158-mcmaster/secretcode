"""Engine duels and skirmishes for foundation/combat.md, played by the scratch bot contenders/_duel-lab.

Each scenario becomes its own small map (41x30, rotational symmetry) whose bottom-row wall bits
tell both teams' copies of _duel-lab what to pick, buy and do (see the bot's main.cpp header).

  python duel.py run SCENARIOS.json OUT_DIR      play every scenario (one game each), keep replays
  python duel.py parse OUT_DIR [--json OUT]      outcome per scenario from the bots' own logs

A scenario is a JSON object: pick [a, b] (champion_draft ids), build [a, b] (build table ids),
pol [a, b] (policy ids), place (0-7), misc (0-7), parity (0/1), table (row index, optional).
Run with the pinned runtime: arena env shows it (Python on PATH is a Store stub).
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402
import games  # noqa: E402

NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
MAPS = ROOT / ".cache/contenders/combat/maps"
BOT = "contenders/_duel-lab"
W, H, X0, NBITS = 41, 30, 6, 32
PICKS = ["ryth", "vesper", "vendetta", "cassi_issac", "stellar", "prism", "marcella", "willow", "ren", "varg",
         "tavi", "heart"]


def bits_of(s: dict) -> int:
    if "table" in s:
        return (int(s["table"]) & 0xFFF) | (1 << 28) | ((int(s.get("parity", 0)) & 1) << 29)
    pa, pb = s["pick"]
    ba, bb = s.get("build", [0, 0])
    qa, qb = s.get("pol", [0, 0])
    v = (pa & 15) | (pb & 15) << 4 | (ba & 15) << 8 | (bb & 15) << 12 | (qa & 7) << 16 | (qb & 7) << 19
    v |= (int(s.get("place", 4)) & 7) << 22 | (int(s.get("misc", 0)) & 7) << 25 | (int(s.get("parity", 0)) & 1) << 29
    v |= ((qa >> 3) & 1) << 30 | ((qb >> 3) & 1) << 31
    return v


def needs_gold(s: dict) -> bool:
    if "table" in s:
        return bool(s.get("gold", False))
    return any(s.get("build", [0, 0]))


def start_round(s: dict) -> int:
    if needs_gold(s):
        base = 110
    elif "table" not in s and int(s.get("place", 4)) == 7:
        base = 50
    else:
        base = 30
    return base + (int(s.get("parity", 0)) & 1)


def make_map(s: dict) -> Path:
    v = bits_of(s)
    extra = s.get("terrain", {})
    tag = f"{v:08x}" + (("-" + s["terrain_tag"]) if "terrain_tag" in s else "")
    champs = int(s.get("champs", 3))
    name = f"duel-{tag}-c{champs}" + ("-g" if needs_gold(s) else "")
    out = MAPS / f"{name}.map26a"
    if out.is_file():
        return out
    MAPS.mkdir(parents=True, exist_ok=True)
    spec = {"width": W, "height": H, "symmetry": "ROTATIONAL180", "champs": champs, "spawnA": [0, 0, 3, 3],
            "walls": [[X0 + i, H - 1] for i in range(NBITS) if v >> i & 1] + extra.get("walls", []),
            "holes": extra.get("holes", []), "bushes": extra.get("bushes", []),
            "altars": [[0, H - 1]], "camps": [list(c) for c in extra.get("camps", [])]}
    if needs_gold(s):
        spec["camps"] += [["gold_cluster", x, y] for x in range(4, 9) for y in range(4, 9)]
    sp = MAPS / f"{name}.json"
    sp.write_text(json.dumps(spec), encoding="utf-8")
    proc = subprocess.run([str(NODE), "--no-warnings", str(HERE / "mkmap.mjs"), str(sp), str(out)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"mkmap failed for {name}: {proc.stderr[-800:]}")
    return out


LINE = re.compile(r"^(D\*?|S|X) (.*)$")


def parse_kv(rest: str) -> dict:
    d = {}
    head, _, foes = rest.partition(" |")
    for tok in head.split():
        k, _, val = tok.partition("=")
        d[k] = val
    d["foes"] = foes.strip()
    return d


def read_logs(replay: Path) -> dict:
    out = {"A": [], "B": []}
    with zipfile.ZipFile(replay) as z:
        for side, member in (("A", "team-a.stdout"), ("B", "team-b.stdout")):
            try:
                text = z.read(member).decode("utf-8", "replace")
            except KeyError:
                continue
            out[side] = [ln.strip() for ln in text.replace(chr(0), chr(10)).splitlines() if ln.strip()]
    return out


def summarize(lines: dict) -> dict:
    """Turn logs into: stats per champion, per-turn D rows, deaths, errors."""
    res = {"stats": [], "turns": [], "deaths": [], "errors": 0}
    for side in ("A", "B"):
        for ln in lines[side]:
            if "error" in ln:
                res["errors"] += 1
            m = LINE.match(ln)
            if not m:
                continue
            kind, rest = m.group(1), m.group(2)
            kv = parse_kv(rest)
            if kind == "S":
                res["stats"].append(kv)
            elif kind == "X":
                res["deaths"].append(kv)
            else:
                kv["stunned"] = kind == "D*"
                res["turns"].append(kv)
    res["turns"].sort(key=lambda t: int(t.get("r", 0)))
    return res


def outcome(s: dict, summ: dict) -> dict:
    """First death, every death (side, champion, rounds after the start), survivors' health at the
    end of the window, and the winner when one side is wiped (Cassi and Issac die together)."""
    deaths = sorted(summ["deaths"], key=lambda d: int(d["r"]))
    start = start_round(s)
    last_hp, maxhp = {}, {}
    for st in summ["stats"]:
        maxhp[(st["t"], st["c"])] = float(st["mh"])
    for t in summ["turns"]:
        if t.get("foes") == "died-in-turn":
            continue
        last_hp[(t["t"], t["c"])] = float(t["hp"])
    dead = {(d["t"], d["c"]) for d in deaths}
    for t in summ["turns"]:
        if t.get("foes") == "died-in-turn":
            dead.add((t["t"], t["c"]))
    o = {"start": start, "first_death": None, "loser": None, "survivors": {}, "errors": summ["errors"],
         "deaths": [[int(d["r"]) - start, d["t"], d["c"]] for d in deaths]}
    if deaths:
        o["first_death"] = int(deaths[0]["r"]) - start
        o["loser"] = deaths[0]["t"]
        o["loser_champ"] = deaths[0]["c"]
    alive = {"A": 0, "B": 0}
    hp = {"A": 0.0, "B": 0.0}
    mh = {"A": 0.0, "B": 0.0}
    for key, m in maxhp.items():
        mh[key[0]] += m
    for key, v in last_hp.items():
        if key in dead:
            continue
        o["survivors"][f"{key[0]}:{key[1]}"] = v
        alive[key[0]] += 1
        hp[key[0]] += v
    o["alive"] = alive
    o["hp_frac"] = {k: (round(hp[k] / mh[k], 3) if mh[k] else None) for k in hp}
    o["winner"] = "A" if alive["A"] and not alive["B"] else ("B" if alive["B"] and not alive["A"] else None)
    return o


def run(scen_file: Path, out: Path, jobs: int = 0) -> None:
    scens = json.loads(scen_file.read_text(encoding="utf-8"))
    ent = core.prepare(core.bot(BOT))
    if not ent.ok:
        raise SystemExit("build failed:\n" + ent.log[-3000:])
    by_rounds: dict[int, list] = {}
    for i, s in enumerate(scens):
        m = make_map(s)
        by_rounds.setdefault(start_round(s) + int(s.get("window", 72)), []).append((i, s, m))
    index = []
    for rounds, items in sorted(by_rounds.items()):
        specs = [games.GameSpec(ent, ent, str(m), {"scenario": i}) for i, _, m in items]
        rows = games.play(specs, out, jobs=jobs, rounds=rounds, keep_replays="all", quiet=True, resume=True)
        for (i, s, m), row in zip(items, rows):
            index.append({"i": i, "scenario": s, "map": m.name, "replay": row.get("replay"), "rounds": rounds,
                          "status": row.get("winner"), "error": row.get("error")})
    index.sort(key=lambda r: r["i"])
    (out / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    print(f"{len(index)} scenarios -> {out / 'index.json'}")


def parse(out: Path) -> list[dict]:
    index = json.loads((out / "index.json").read_text(encoding="utf-8"))
    res = []
    for r in index:
        if not r.get("replay"):
            res.append({**r, "outcome": None})
            continue
        rp = Path(r["replay"])
        if not rp.is_absolute():
            rp = ROOT / rp
        summ = summarize(read_logs(rp))
        res.append({**r, "outcome": outcome(r["scenario"], summ), "summary": summ})
    return res


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[0] == "run":
        run(Path(argv[1]), Path(argv[2]).resolve(), int(argv[3]) if len(argv) > 3 else 0)
        return 0
    if len(argv) >= 3 and argv[0] == "show":
        res = parse(Path(argv[1]).resolve())
        for i in [int(x) for x in argv[2].split(",")]:
            r = res[i]
            print("==", i, json.dumps(r["scenario"]), "->", json.dumps(r["outcome"]))
            for st in r["summary"]["stats"]:
                print("  S", " ".join(f"{k}={v}" for k, v in st.items() if k != "foes"))
            for t in r["summary"]["turns"]:
                print(f"  {t['r']} {t['t']} {t['c']} hp={t['hp']} sh={t['sh']} p={t['p']} st={t['st']} cd={t['cd']} "
                      f"a={t['a']}{' STUNNED' if t['stunned'] else ''} |{t['foes']}")
        return 0
    if len(argv) >= 2 and argv[0] == "parse":
        res = parse(Path(argv[1]).resolve())
        if "--json" in argv:
            Path(argv[argv.index("--json") + 1]).write_text(json.dumps(res, indent=1), encoding="utf-8")
        for r in res:
            s, o = r["scenario"], r["outcome"]
            print(r["i"], json.dumps(s), "->", json.dumps(o) if o else r.get("error"))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
