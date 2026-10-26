"""Tables from duel.py runs (foundation/combat.md).

  python analyze.py matrix RUN_DIR [--out data.json]   duel matrix by pick pair (both sides, both parities)
  python analyze.py games RUN_DIR                       one line per game

Per game: winner pick, rounds from first damage to the first death, the survivor's health share,
who dealt the first damage, damage dealt by each side, error lines.
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import duel  # noqa: E402

NAMES = ["Ryth", "Vesper", "Vendetta", "Cassi&Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
         "Tavi", "Heart"]
SHORT = {"ryth": 0, "vesp": 1, "vend": 2, "cass": 3, "issa": 3, "stel": 4, "pris": 5, "marc": 6, "will": 7, "ren": 8,
         "varg": 9, "tavi": 10, "hear": 11}


def game_facts(r: dict) -> dict | None:
    if not r.get("outcome"):
        return None
    s, summ = r["scenario"], r["summary"]
    picks = s.get("pick", [None, None])
    maxhp = defaultdict(float)
    for st in summ["stats"]:
        maxhp[(st["t"], st["c"])] += float(st["mh"])
    side_max = {"A": sum(v for (t, _), v in maxhp.items() if t == "A"),
                "B": sum(v for (t, _), v in maxhp.items() if t == "B")}
    # first damage: the first logged turn where some duelist is below max health
    first = None
    hp_last = {}
    for t in summ["turns"]:
        key = (t["t"], t["c"])
        hp = float(t["hp"])
        if first is None and hp < maxhp.get(key, hp) - 1e-3:
            first = (int(t["r"]), "B" if t["t"] == "A" else "A")
        hp_last[key] = hp
    deaths = sorted(summ["deaths"], key=lambda d: int(d["r"]))
    out = {"picks": picks, "parity": s.get("parity", 0), "errors": summ["errors"], "winner": None,
           "ttk": None, "first_side": first[1] if first else None, "first_round": first[0] if first else None,
           "surv_frac": None, "surv_hp": None}
    if deaths:
        loser = deaths[0]["t"]
        win = "B" if loser == "A" else "A"
        out["winner"] = win
        out["ttk"] = int(deaths[0]["r"]) - (first[0] if first else int(deaths[0]["r"]))
        hp = sum(v for (t, _), v in hp_last.items() if t == win)
        out["surv_hp"] = hp
        out["surv_frac"] = hp / side_max[win] if side_max[win] else None
    return out


def matrix(run: Path) -> dict:
    res = duel.parse(run)
    cells = defaultdict(list)  # (x, y) -> list of (x_won, margin, ttk, x_first)
    errors = 0
    for r in res:
        f = game_facts(r)
        if f is None:
            continue
        errors += f["errors"]
        a, b = f["picks"]
        for x, y, side in ((a, b, "A"), (b, a, "B")):
            if f["winner"] is None:
                cells[(x, y)].append((0.5, 0.0, None, f["first_side"] == side))
                continue
            won = f["winner"] == side
            margin = f["surv_frac"] if won else -f["surv_frac"]
            cells[(x, y)].append((1.0 if won else 0.0, margin, f["ttk"], f["first_side"] == side))
    table = {}
    for (x, y), v in cells.items():
        wins = sum(w for w, *_ in v)
        table[f"{x},{y}"] = {"games": len(v), "wins": wins, "margin": sum(m for _, m, *_ in v) / len(v),
                             "ttk": [t for *_, t, _ in v if t is not None],
                             "first_wins": sum(1 for w, _, _, fs in v if fs and w == 1.0),
                             "first_games": sum(1 for *_, fs in v if fs)}
    return {"table": table, "errors": errors, "games": len(res)}


def print_matrix(m: dict, picks=range(12)) -> None:
    t = m["table"]
    hdr = "X \\ Y      " + " ".join(f"{NAMES[y][:5]:>6}" for y in picks)
    print(hdr)
    for x in picks:
        row = []
        for y in picks:
            c = t.get(f"{x},{y}")
            row.append("   .  " if not c else f"{c['margin']:+6.2f}")
        print(f"{NAMES[x][:10]:<11}" + " ".join(row))
    print(f"errors {m['errors']} over {m['games']} games")


def main(argv):
    if len(argv) >= 2 and argv[0] == "matrix":
        m = matrix(Path(argv[1]).resolve())
        print_matrix(m)
        if "--out" in argv:
            Path(argv[argv.index("--out") + 1]).write_text(json.dumps(m, indent=1), encoding="utf-8")
        return 0
    if len(argv) >= 2 and argv[0] == "chase":
        rows = chase_table(Path(argv[1]).resolve())
        if "--out" in argv:
            Path(argv[argv.index("--out") + 1]).write_text(json.dumps(rows, indent=1), encoding="utf-8")
        for row in rows:
            print(json.dumps(row))
        return 0
    if len(argv) >= 2 and argv[0] == "games":
        for r in duel.parse(Path(argv[1]).resolve()):
            print(r["i"], json.dumps(r["scenario"]), json.dumps(game_facts(r)))
        return 0
    print(__doc__)
    return 2




def chase_facts(r: dict) -> dict | None:
    """Fleer on side A runs home (spawn x<3, y<3); returns arrival, death, damage taken per round."""
    if not r.get("outcome"):
        return None
    s, summ = r["scenario"], r["summary"]
    fside = "A" if s["pol"][0] in (2, 5, 7) else "B"
    f, c = s["pick"] if fside == "A" else s["pick"][::-1]
    start = duel.start_round(s)
    L = 3 * int(s.get("misc", 0)) + 3
    rows = [t for t in summ["turns"] if t["t"] == fside and t.get("foes") != "died-in-turn"]
    maxhp = max((float(st["mh"]) for st in summ["stats"] if st["t"] == fside), default=0.0)
    arrive, hp_at, dead = None, None, None
    hp0 = None
    for t in rows:
        rr = int(t["r"])
        if rr < start:
            continue
        hp = float(t["hp"])
        if hp0 is None:
            hp0 = hp
        x, y = (int(v) for v in t["p"].split(","))
        if fside == "B":
            x, y = duel.W - 1 - x, duel.H - 1 - y
        if 0 <= x < 3 and 0 <= y < 3 and arrive is None:
            arrive, hp_at = rr - start, hp
            break
    for d in summ["deaths"]:
        if d["t"] == fside:
            dead = int(d["r"]) - start
            break
    if arrive is not None and dead is not None and dead < arrive:
        arrive = None
    lost_turns = sum(1 for t in rows if int(t["r"]) >= start and t["stunned"] and (arrive is None or int(t["r"]) - start <= arrive))
    taken = (maxhp - hp_at) if arrive is not None else maxhp
    rounds = arrive if arrive is not None else dead
    return {"fleer": f, "chaser": c, "L": L, "pol": s["pol"][0 if fside == "A" else 1], "fleer_side": fside,
            "slot_shift": s.get("terrain_tag") == "slot", "parity": s.get("parity", 0), "escaped": arrive is not None,
            "rounds": rounds, "hp_left": hp_at, "max": maxhp, "taken": taken,
            "rate": (taken / rounds) if rounds else None, "stunned": lost_turns}


def chase_table(run: Path) -> list[dict]:
    out = []
    for r in duel.parse(run):
        f = chase_facts(r)
        if f:
            out.append(f)
    return out


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
