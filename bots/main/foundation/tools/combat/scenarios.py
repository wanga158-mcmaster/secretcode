"""Write the scenario sets behind foundation/combat.md (input to duel.py run).

  python scenarios.py [OUT_DIR]      writes s_<name>.json (default .cache/contenders/combat/)

Picks: 0 Ryth 1 Vesper 2 Vendetta 3 Cassi&Issac 4 Stellar 5 Prism 6 Marcella 7 Willow 8 Ren 9 Varg
10 Tavi 11 Heart. Policies: 0 FIGHT 1 KITE 2 FLEE 3 PASSIVE 4 BASIC 5 FLEESTUN 6 HOLD 7 FLEEHIT
8 SPREAD 9 BASICSPREAD 10 STAGGER 11 BAIT 12 STANDOFF (misc 1: parity-aware margin). Builds: see
duelsim.BUILDS. Placement 4 = 6 tiles apart; 7 = chase (misc m: fleer 3m+3 tiles from home).
Run order used for the page: stun base kite2 chase stat b11 fleehit online metab camp standoff
standoff3 slot g1 (the first kite run, towards the kiter's spawn, was discarded).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parents[4]
P = range(12)


def sets() -> dict[str, list[dict]]:
    out: dict[str, list[dict]] = {}
    out["base"] = [{"pick": [a, b], "pol": [0, 0], "place": 4, "parity": p} for a in P for b in P for p in (0, 1)]
    out["b11"] = [{"pick": [a, b], "build": [11, 11], "pol": [0, 0], "place": 4, "parity": p}
                  for a in P for b in P for p in (0, 1)]
    stat = []
    for x in P:
        for ba, bb in [(1, 4), (1, 7), (4, 7), (2, 5), (2, 8), (5, 8), (3, 6), (3, 9), (6, 9)]:
            for p in (0, 1):
                stat.append({"pick": [x, x], "build": [ba, bb], "pol": [0, 0], "place": 4, "parity": p})
                stat.append({"pick": [x, x], "build": [bb, ba], "pol": [0, 0], "place": 4, "parity": p})
    out["stat"] = stat
    ranged, melee = [1, 10, 7, 4, 5, 3], [2, 6, 0, 11, 8, 9]
    out["kite2"] = [s for r in ranged for m in melee for p in (0, 1)
                    for s in ({"pick": [r, m], "pol": [1, 0], "place": 4, "parity": p},
                              {"pick": [m, r], "pol": [0, 1], "place": 4, "parity": p})]
    chase = [{"pick": [f, c], "pol": [2, 0], "place": 7, "misc": m, "parity": p}
             for c in [2, 6, 0, 11, 8, 9, 1, 10] for f in [1, 2, 6, 0, 10, 11, 4] for m in (0, 1, 3, 7) for p in (0, 1)]
    chase += [{"pick": [f, c], "pol": [5, 0], "place": 7, "misc": m, "parity": p}
              for f in [10, 0, 5, 8, 3] for c in [2, 6, 11, 0] for m in (3, 7) for p in (0, 1)]
    out["chase"] = chase
    out["fleehit"] = [{"pick": [f, c], "pol": [7, 0], "place": 7, "misc": m, "parity": p}
                      for f in [6, 0, 2, 11, 9] for c in [2, 6, 0, 11] for m in (3, 7) for p in (0, 1)]
    slot_t = {"walls": [[40, 10]]}
    slot = []
    for c in [2, 6, 0, 11, 9]:
        for f in [6, 0, 1, 10]:
            for m in (3, 7):
                for p in (0, 1):
                    slot.append({"pick": [f, c], "pol": [2, 0], "place": 7, "misc": m, "parity": p,
                                 "terrain": slot_t, "terrain_tag": "slot"})
                    slot.append({"pick": [c, f], "pol": [0, 2], "place": 7, "misc": m, "parity": p,
                                 "terrain": slot_t, "terrain_tag": "slot"})
    slot += [{"pick": [a, a], "pol": [0, 0], "place": 4, "parity": p, "terrain": slot_t, "terrain_tag": "slot"}
             for a in P for p in (0, 1)]
    out["slot"] = slot
    online = []
    for x in [11, 1, 6, 0, 2, 10, 5, 8, 3, 9, 4, 7]:
        hb = 12 if x == 11 else 11
        for p in (0, 1):
            online.append({"pick": [x, x], "build": [hb, 15], "pol": [0, 0], "place": 4, "parity": p})
            online.append({"pick": [x, x], "build": [15, hb], "pol": [0, 0], "place": 4, "parity": p})
    out["online"] = online
    meta = [11, 1, 6, 0, 2, 10, 5, 8, 3]
    out["metab"] = [{"pick": [a, b], "build": [12 if a == 11 else 11, 12 if b == 11 else 11], "pol": [0, 0],
                     "place": 4, "parity": p} for a in meta for b in meta for p in (0, 1)]
    out["stun"] = [{"table": t, "champs": 5 if t in (18, 19) else 3, "gold": True, "parity": p}
                   for t in (16, 17, 18, 19) for p in (0, 1)]
    out["camp"] = [{"table": t, "champs": 3, "parity": p, "terrain": {"camps": [["elk", 20, 14]]}, "terrain_tag": "elk"}
                   for t in range(27, 33) for p in (0, 1)]
    so = [s for a in P for b in P for p in (0, 1)
          for s in ({"pick": [a, b], "pol": [12, 0], "place": 4, "parity": p},
                    {"pick": [a, b], "pol": [0, 12], "place": 4, "parity": p})]
    out["standoff"] = so + [{"pick": [a, a], "pol": [12, 12], "place": 4, "parity": p} for a in P for p in (0, 1)]
    so3 = []
    for a in P:
        for b in P:
            for p in (0, 1):
                so3.append({"pick": [a, b], "pol": [12, 0], "place": 4, "misc": 1, "parity": p})
                so3.append({"pick": [a, b], "pol": [0, 12], "place": 4, "misc": 1, "parity": p})
    out["standoff3"] = so3
    sizes = {0: (1, 1), 1: (2, 1), 2: (2, 2), 3: (3, 2), 4: (3, 3), 5: (3, 1), 6: (3, 2), 7: (2, 2), 8: (3, 3),
             9: (5, 5), 10: (5, 4), 11: (5, 3), 12: (3, 3), 13: (3, 2), 14: (4, 4), 15: (2, 1), 16: (3, 1),
             17: (3, 1), 18: (5, 1), 19: (5, 1), 20: (1, 3), 21: (1, 3), 22: (1, 3), 23: (1, 3), 24: (2, 2),
             25: (2, 2), 26: (2, 2)}
    out["g1"] = [{"table": t, "champs": max(3, a, b), "gold": t in (16, 17, 18, 19), "parity": p}
                 for t, (a, b) in sizes.items() for p in (0, 1)]
    return out


def main(argv: list[str]) -> int:
    out = Path(argv[0]) if argv else ROOT / ".cache/contenders/combat"
    out.mkdir(parents=True, exist_ok=True)
    for name, sc in sets().items():
        (out / f"s_{name}.json").write_text(json.dumps(sc), encoding="utf-8")
        print(f"s_{name}.json {len(sc)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
