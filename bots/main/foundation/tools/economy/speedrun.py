"""Speedrun bound per map (economy.md E13-E16): how fast can a team bank the winning gems when
the opponent does nothing?

  python -I speedrun.py [--maps all|pool|legacy|name,...] [--territory full|half] [--items]
                        [--roster main|meta|NAME,...] [--out FILE] [--jobs N]

Three numbers per map (side A; the maps are symmetric):
- supply floor: every reachable gem group captured the moment it can be (first visit after the
  walk from spawn, then every cooldown + capture) by an unlimited team with zero clear time, each
  capture banked at its nearest altar; the round the banked total reaches the target. Nothing can
  beat it (respawn and walking bind).
- labour bound: the fractional-knapsack maximum gem rate of N champions when each capture costs
  its clear champion-rounds (simulated, base stats, the best of solo/pair/trio/team) plus the
  capture wait plus the shortest walk from another group, and each group yields at most once per
  cooldown + capture; rounds = first walk + target / rate. A lower bound of the labour-limited time.
- schedule: an event-driven greedy dispatcher (feasible, so an upper bound on the optimum) with the
  verified mechanics: real walking distances (atlas travel.json), clears simulated round by round
  (econ.simulate_clear: tank rule, retreats and recall trips, kits), capture_time + 1 rounds on the
  tile, respawn timers, banking walks and ceil(bag/10) channels; each idle champion takes the option
  with the best gems per champion-round (solo, or a party of the nearest idle allies); the best
  schedule over a small policy grid (bank threshold, respawn wait, parties) is reported, with its
  time split (travel, wait, fight, capture, bank, out) so the binding constraint is visible.
Gold buys nothing at base stats; --items spawn buys shards at every spawn visit (heals, the
recall-first option, which also fires to shop with >= 150 gold), --items instant the moment gold
allows (optimistic); the order is Jungle, then Monster, then Power emblems of the main stat.
"""
from __future__ import annotations

import argparse
import heapq
import json
import math
import sys
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402

DATA = econ.ROOT / "bots" / "main" / "foundation" / "data"
INF = 1 << 20
ROSTERS = {"main": ["Heart", "Marcella", "Ryth", "Vendetta", "Varg"],          # main's speedrun draft (actual.json)
           "meta": ["Ryth", "Marcella", "Heart", "Vendetta", "Vesper"]}        # online top-team picks
TARGET = 1000


def load_maps():
    atlas = json.loads((DATA / "maps.json").read_text(encoding="utf-8"))
    travel = json.loads((DATA / "maps" / "travel.json").read_text(encoding="utf-8"))
    geo = {m["id"]: m for m in json.loads((econ.ROOT / "maps" / "geometry.json").read_text(encoding="utf-8"))["maps"]}
    leg = {}
    lp = DATA / "maps" / "legacy-geometry.json"
    if lp.exists():
        leg = {"legacy/" + m["id"]: m for m in json.loads(lp.read_text(encoding="utf-8"))["maps"]}
    out = {}
    for rec in atlas["maps"]:
        g = geo.get(rec["key"]) or leg.get(rec["key"])
        out[rec["key"]] = (rec, travel[rec["key"]], g)
    return out


class MapModel:
    def __init__(self, cat, rec, trav, geom, territory="full"):
        self.cat, self.rec, self.trav = cat, rec, trav
        self.groups = []
        for gi, (g, gr) in enumerate(zip(geom["groups"], rec["groups"])):
            mem = [(x["type"], x["x"], x["y"]) for x in g["members"]]
            ok = gr["dA"] is not None and (territory == "full" or (gr["dB"] is None or gr["dA"] <= gr["dB"]))
            self.groups.append({"i": gi, "key": g["key"], "members": mem, "gems": g["gemValue"], "gold": g["goldValue"],
                                "cd": g["cooldown"], "ok": ok and g["gemValue"] + g["goldValue"] > 0,
                                "harmless": gr["harmless"], "spawn": gr["dA"] if gr["dA"] is not None else INF,
                                "altar": min((x for x in (row[gi] for row in trav["group_to_alt"]) if x is not None), default=INF)})
        self.n_alt = len(trav["group_to_alt"])
        # altars reachable only through a group's member tiles (01boss_battle): open after its capture
        keys = {g["key"]: g["i"] for g in self.groups}
        self.gates = {a: keys[alt["gated_by"][0]] for a, alt in enumerate(rec["altars"]) if alt.get("gated_by")}
        self.opened = set()
        for a, gi in self.gates.items():
            if gi in keys.values():
                self.groups[gi]["altar"] = 0

    def walk(self, loc, j):
        kind, x = loc
        if kind == "spawn":
            return self.groups[j]["spawn"]
        if kind == "group":
            if x == j:
                return 0
            d = self.trav["travel"][x][j]
            return INF if d is None else d
        d = self.trav["alt_to_group"][x][j]
        return INF if d is None else d

    def nearest_altar(self, loc):
        kind, x = loc
        best = (INF, None)
        for a in range(self.n_alt):
            if kind == "spawn":
                d = self.trav["altar_spawnA_each"][a]
            elif kind == "group":
                d = self.trav["group_to_alt"][a][x]
            else:
                d = 0 if a == x else INF
            if (d is None or d >= INF) and a in self.gates and self.gates[a] in self.opened:
                d = self.walk(loc, self.gates[a]) + 1
            if d is not None and d < best[0]:
                best = (d, a)
        return best


@lru_cache(maxsize=200000)
def _clear(cat_id, members, names, shards, emblems, hp_bucket, walk_back):
    cat = CAT[cat_id]
    fighters = []
    for i, n in enumerate(names):
        f = econ.make_fighter(cat, n, i, shards=shards[i], emblems=emblems[i])
        f.hp = min(f.max_hp, hp_bucket[i] * 50.0) if hp_bucket[i] >= 0 else f.max_hp
        fighters.append(f)
    r = econ.simulate_clear(cat, fighters, econ.monsters_of(cat, members), walk_back=walk_back)
    return r.killed, r.rounds, tuple(round(f.hp, 1) for f in fighters), r.killer_melee, r.out_rounds, r.trips


CAT = {}


class Champ:
    def __init__(self, idx, name, cat, items):
        self.idx, self.name, self.cat = idx, name, cat
        self.main = "focus" if name in ("Stellar", "Cassi", "Prism", "Heart") else "str"
        self.shards = [0, 0, 0]
        self.emblems = []
        self.gold = 0.0
        self.items = items
        self.order = "jungle"
        self.partial = 0
        self.done = 0
        f = econ.make_fighter(cat, name, idx)
        self.max_hp = f.max_hp
        self.hp = f.max_hp
        self.gold_mult = f.gold_mult

    def key(self):
        return tuple(self.shards), tuple(sorted(self.emblems))

    def preview(self):
        """Item key after spending the purse at spawn (the recall-first option shops)."""
        if not self.items or self.gold < 30:
            return self.key()
        saved = (list(self.shards), list(self.emblems), self.gold, self.max_hp, self.hp, self.gold_mult, self.partial, self.done)
        self.buy()
        k = self.key()
        (self.shards, self.emblems, self.gold, self.max_hp, self.hp, self.gold_mult, self.partial,
         self.done) = saved
        return k

    def buy(self):
        """Spend the purse on the next shards of the emblem plan ORDERS[self.order]."""
        if not self.items:
            return
        plan = ORDERS[self.order]
        while self.gold >= 30:
            fam, stat = plan[min(self.done, len(plan) - 1)]
            idx = {"hp": 0, "str": 1, "focus": 2}[self.main if stat == "main" else stat]
            self.gold -= 30
            self.shards[idx] += 1
            self.partial += 1
            if self.partial == 5:
                self.partial = 0
                self.done += 1
                if fam == "power":
                    self.shards[idx] += 1
                elif fam == "bruiser":
                    for k in range(3):
                        if k != idx:
                            self.shards[k] += 1
                elif fam not in self.emblems:
                    self.emblems.append(fam)
            f = econ.make_fighter(self.cat, self.name, self.idx, shards=tuple(self.shards), emblems=tuple(self.emblems))
            gain = f.max_hp - self.max_hp
            self.max_hp = f.max_hp
            self.hp += gain
            self.gold_mult = f.gold_mult


# Emblem plans: (family, stat) per emblem, the last entry repeats. "main" = Strength, or Focus for
# the spell kits (Stellar, Cassi, Prism, Heart).
ORDERS = {
    "jungle": [("jungle", "main"), ("monster", "main"), ("power", "main")],
    "monster": [("monster", "main"), ("jungle", "main"), ("power", "main")],
    "power": [("power", "main")],
    "treasure": [("treasure", "main"), ("jungle", "main"), ("monster", "main"), ("power", "main")],
    "health": [("jungle", "hp"), ("monster", "main"), ("power", "main")],
    "tank": [("jungle", "hp"), ("durability", "hp"), ("monster", "main"), ("power", "main")],
    "treasure-health": [("treasure", "hp"), ("jungle", "main"), ("bruiser", "hp"), ("power", "main")],
}


class Squad:
    """Champions that move and fight together (one agent); a solo farmer is a squad of one."""

    def __init__(self, sid, members):
        self.sid = sid
        self.members = sorted(members, key=lambda c: (not c.cat.kits[c.name].melee, -c.max_hp))  # tank first
        self.bag = 0
        self.loc = ("spawn", None)
        self.free = 1
        self.use = {"travel": 0, "wait": 0, "fight": 0, "capture": 0, "bank": 0, "out": 0, "idle": 0}

    @property
    def k(self):
        return len(self.members)

    def add(self, kind, rounds):
        self.use[kind] += rounds * self.k


def partitions(roster):
    n = len(roster)
    out = {"solo": [[x] for x in roster], "team": [list(roster)]}
    if n >= 2:
        pairs = [roster[i:i + 2] for i in range(0, n, 2)]
        out["pairs"] = pairs
    if n >= 4:
        out["3+rest"] = [roster[:3]] + [[x] for x in roster[3:]]
    if n == 5:
        out["3+2"] = [roster[:3], roster[3:]]
    return out


def order_roster(cat, roster):
    """Melee tanks spread over squads first (pairs get a melee each), then the others."""
    melee = sorted([r for r in roster if cat.kits[r].melee], key=lambda r: -cat.kits[r].hp)
    ranged = [r for r in roster if not cat.kits[r].melee]
    out = []
    while melee or ranged:
        if melee:
            out.append(melee.pop(0))
        if ranged:
            out.append(ranged.pop(0))
    return out


def run_schedule(mm: MapModel, roster, *, B=40, wait_max=20, split="solo", items=False, lam=0.0, max_rounds=2000, cat_id=0,
                 order="jungle"):
    cat = mm.cat
    roster = order_roster(cat, roster)
    champs = {n: Champ(i, n, cat, bool(items)) for i, n in enumerate(roster)}
    for c in champs.values():
        c.order = order
    squads = [Squad(i, [champs[n] for n in part]) for i, part in enumerate(partitions(roster)[split])]
    banked = 0
    avail = {g["i"]: 1 for g in mm.groups if g["ok"]}
    busy = set()
    events = []
    seq = 0
    log = {"captures": 0, "tributes": 0, "gems": 0, "gold": 0.0, "bank_at": {}, "max_bag": 0}

    def push(t, kind, payload):
        nonlocal seq
        seq += 1
        heapq.heappush(events, (t, seq, kind, payload))

    def in_flight():
        return sum(s.bag for s in squads) + sum(p["gems"] for _, _, k, p in events if k == "reward")

    def plan(s, t):
        endgame = banked + in_flight() >= TARGET
        if s.bag > 0 and (s.bag >= B or endgame):
            d, a = mm.nearest_altar(s.loc)
            if a is not None and d < INF:
                ch = max(1, math.ceil(s.bag / 10))
                s.add("travel", d)
                s.add("bank", ch)
                push(t + d + ch, "bank", {"s": s.sid, "a": a})
                s.free = INF
                return
        if endgame:
            s.add("idle", 1)
            s.free = t + 1
            return
        need = TARGET - banked - in_flight()
        names = tuple(c.name for c in s.members)
        keys = [c.key() for c in s.members]
        hurt = (any(c.hp < c.max_hp for c in s.members) or any(c.items and c.gold >= 150 for c in s.members)) and s.loc[0] != "spawn"
        back = int(cat.settings["recall_time"]) + 1
        best = None
        for g in mm.groups:
            j = g["i"]
            if not g["ok"] or j in busy:
                continue
            value = min(g["gems"], max(1, need)) + lam * g["gold"]
            if value <= 0:
                continue
            # option 1: go now with current health; option 2 (when hurt): recall, heal, walk from spawn
            for recall_first in ((False, True) if hurt else (False,)):
                d = (back + mm.groups[j]["spawn"]) if recall_first else mm.walk(s.loc, j)
                if d >= INF:
                    continue
                wait = max(0, avail[j] - (t + d))
                if wait > wait_max:
                    continue
                hpb = tuple(-1 for c in s.members) if recall_first else tuple(int(c.hp // 50) if c.hp < c.max_hp else -1 for c in s.members)
                kk = [c.preview() for c in s.members] if recall_first else keys
                res = _clear(cat_id, tuple(g["members"]), names, tuple(k[0] for k in kk), tuple(k[1] for k in kk), hpb,
                             int(min(g["spawn"], 60)))
                killed, rounds, hps, melee_kill, out_rounds, trips = res
                if not killed:
                    continue
                cap = int(cat.settings["capture_time"]) + (0 if melee_kill else 1)
                labour = (d + wait + rounds + cap) * s.k
                rate = value / max(1, labour)
                if best is None or rate > best[0]:
                    best = (rate, j, d, wait, rounds, hps, cap, recall_first)
        if best is None:
            s.add("idle", 1)
            s.free = t + 1
            return
        rate, j, d, wait, rounds, hps, cap, recall_first = best
        busy.add(j)
        if recall_first:
            s.add("out", back)
            d -= back
            for c in s.members:
                c.hp = c.max_hp
                c.buy()
            for c in s.members:
                c.hp = c.max_hp
        s.add("travel", d)
        s.add("wait", wait)
        s.add("fight", rounds)
        s.use["capture"] += cap
        for c, hp in zip(s.members, hps):
            c.hp = hp if hp > 0 else c.max_hp
        s.loc = ("group", j)
        s.free = INF
        push(t + d + wait + rounds + cap, "reward", {"s": s.sid, "j": j, "gems": mm.groups[j]["gems"]})

    t = 1
    while t <= max_rounds and banked < TARGET:
        while events and events[0][0] <= t:
            _, _, kind, p = heapq.heappop(events)
            s = squads[p["s"]]
            if kind == "reward":
                g = mm.groups[p["j"]]
                s.bag += g["gems"]
                log["max_bag"] = max(log["max_bag"], s.bag)
                for c in s.members:  # gold split evenly over the squad (all stand on member tiles)
                    gold = g["gold"] / s.k * c.gold_mult
                    c.gold += gold
                    log["gold"] += gold
                    if items == "instant":
                        c.buy()
                log["captures"] += 1
                log["gems"] += g["gems"]
                avail[p["j"]] = t + g["cd"]
                busy.discard(p["j"])
                mm.opened.add(p["j"])
                s.free = t
            elif kind == "bank":
                banked += s.bag
                log["tributes"] += 1
                s.bag = 0
                s.loc = ("altar", p["a"])
                s.free = t
                if banked >= TARGET:
                    break
            elif kind == "healed":
                for c in s.members:
                    c.hp = c.max_hp
                    c.buy()
                s.loc = ("spawn", None)
                s.free = t
        if banked >= TARGET:
            break
        if t in (200, 400, 600):
            log["bank_at"][t] = banked
        for s in squads:
            if s.free <= t:
                plan(s, t)
        t += 1
    use = {}
    for s in squads:
        for k, v in s.use.items():
            use[k] = use.get(k, 0) + v
    for r in (200, 400, 600):
        log["bank_at"].setdefault(r, banked)
    return {"rounds": t if banked >= TARGET else max_rounds, "banked": banked, "use": use, **log}


def supply_floor(mm: MapModel, cap=6):
    """Infinite labour, zero clear time: each group first captured after the walk from spawn, then
    every cooldown + capture, banked at its nearest altar; the round banked gems reach the target."""
    events = []
    gate_open = None
    if mm.gates:  # gated altars: banking opens with the gate group's first capture
        gi = next(iter(mm.gates.values()))
        gate_open = mm.groups[gi]["spawn"] + cap
    for g in mm.groups:
        if not g["ok"] or g["gems"] <= 0:
            continue
        alt, opens = g["altar"], 0
        if alt >= INF and gate_open is not None:
            gi = next(iter(mm.gates.values()))
            alt = mm.walk(("group", g["i"]), gi) + 1
            opens = gate_open
        if alt >= INF:
            continue
        t = g["spawn"] + cap
        while t + alt <= 2000:
            events.append((max(t + alt, opens) + 1, g["gems"]))
            t += g["cd"] + cap
    events.sort()
    tot = 0
    for t, gm in events:
        tot += gm
        if tot >= TARGET:
            return t
    return 2000


def labour_bound(mm: MapModel, roster, cat_id=0):
    """Fractional knapsack over groups: max gems per round for len(roster) champions."""
    cat = mm.cat
    n = len(roster)
    items = []
    for g in mm.groups:
        if not g["ok"] or g["gems"] <= 0:
            continue
        j = g["i"]
        hop = min([x for x in (mm.trav["travel"][i][j] for i in range(len(mm.groups)) if i != j) if x is not None] + [g["spawn"]])
        best = None
        for k in range(1, n + 1):
            names = tuple(sorted(roster, key=lambda x: -cat.kits[x].hp)[:k])
            res = _clear(cat_id, tuple(g["members"]), names, tuple((0, 0, 0) for _ in names), tuple(() for _ in names),
                         tuple(-1 for _ in names), int(min(g["spawn"], 60)))
            if not res[0]:
                continue
            cost = res[1] * k + 6 + hop * k
            if best is None or g["gems"] / cost > best[0]:
                best = (g["gems"] / cost, cost)
        if best:
            items.append((best[0], best[1], g["gems"], g["cd"] + 6 + 1))
    items.sort(reverse=True)
    labour = float(n)
    rate = 0.0
    for r, cost, gems, cycle in items:
        use = min(labour, cost / cycle)
        rate += use * r
        labour -= use
        if labour <= 0:
            break
    first = min((g["spawn"] for g in mm.groups if g["ok"] and g["gems"] > 0), default=0)
    return (first + TARGET / rate) if rate > 0 else 2000, rate


def evaluate(job):
    key, rec, trav, geom, roster, territory, items = job
    cat = econ.load_catalog()
    CAT[0] = cat
    mm = MapModel(cat, rec, trav, geom, territory)
    roster = roster[: rec["draft"]]
    best = None
    splits = list(partitions(order_roster(cat, roster)).keys())
    grid = [(B, W, P, L) for B in (25, 50, 100, 200) for W in (0, 25) for P in splits for L in ((0.0, 0.05) if items else (0.0,))]
    for B, W, P, L in grid:
        r = run_schedule(mm, roster, B=B, wait_max=W, split=P, items=items, lam=L)
        if best is None or (r["rounds"], -r["banked"]) < (best[1]["rounds"], -best[1]["banked"]):
            best = ((B, W, P, L), r)
    lb, rate = labour_bound(mm, roster)
    return {"map": key, "draft": rec["draft"], "roster": roster, "territory": territory, "items": items,
            "supply_floor": supply_floor(mm), "labour_bound": round(lb), "labour_rate": round(rate, 3),
            "schedule": best[1]["rounds"], "policy": {"bank_at": best[0][0], "wait_max": best[0][1], "split": best[0][2],
                                                      "gold_weight": best[0][3]},
            "detail": best[1]}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--maps", default="pool")
    p.add_argument("--territory", default="full")
    p.add_argument("--items", default="none", choices=["none", "spawn", "instant"])
    p.add_argument("--roster", default="main")
    p.add_argument("--out", default="")
    p.add_argument("--jobs", type=int, default=8)
    a = p.parse_args()
    maps = load_maps()
    if a.maps == "pool":
        keys = [k for k in maps if not k.startswith("legacy/")]
    elif a.maps == "legacy":
        keys = [k for k in maps if k.startswith("legacy/")]
    elif a.maps == "all":
        keys = list(maps)
    else:
        keys = a.maps.split(",")
    roster = ROSTERS.get(a.roster) or a.roster.split(",")
    items = None if a.items == "none" else a.items
    jobs = [(k, *maps[k], roster, a.territory, items) for k in keys]
    import multiprocessing as mp
    with mp.Pool(a.jobs) as pool:
        res = pool.map(evaluate, jobs)
    for r in res:
        d = r["detail"]
        u = d["use"]
        tot = sum(u.values()) or 1
        split = " ".join(f"{k} {100 * v / tot:.0f}" for k, v in u.items() if v)
        print(f"{r['map']:24s} d{r['draft']} floor {r['supply_floor']:5d} labour {r['labour_bound']:5d} schedule {r['schedule']:5d} "
              f"b400 {d['bank_at'][400]:4d} caps {d['captures']:3d} trib {d['tributes']:3d} pol {r['policy']} | {split}", flush=True)
    if a.out:
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
