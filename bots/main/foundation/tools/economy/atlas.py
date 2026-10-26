"""Map atlas: per-map walking geometry, camp groups, altars, terrain and economic metrics.

  python -I atlas.py [--out-dir bots/main/foundation/data]

Reads maps/geometry.json (40 pool maps) and data/maps/legacy-geometry.json (17 legacy maps,
written by decode_legacy.mjs). Writes data/maps.json (the atlas) and data/maps/travel.json
(group/altar/spawn walking-distance matrices used by the capacity model).

Walking model (rules: Maps & movement; rules-notes "Verified by probes"):
- one king move (8 directions) per round; walls, holes, altars, the enemy's spawn tiles and
  living camp members block; own spawn tiles are passable;
- a group is engaged from a tile adjacent (Chebyshev 1) to a member: melee reach. Ranged
  champions can stop up to 2 tiles earlier (their range is 3), recorded separately;
- after a clear the group's own member tiles are free (capture stands on one); travel from a
  group starts on its member tiles;
- dash edges (Vendetta, Vesper, Issac, Ren, Heart): up to 2 king steps whose intermediate tile
  may be a hole, landing on a free tile; used only for the dash-only inventory.
"""
from __future__ import annotations

import argparse
import json
import math
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
DATA = ROOT / "bots" / "main" / "foundation" / "data"
INF = 1 << 20
DIRS = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]
# type: (health, strength, attack range, respawn) - runtime catalog / handbook camp table
MON = {"wolf": (1350, 20, 3, 40), "elk": (2400, 60, 4, 60), "golem": (5000, 120, 4, 80),
       "monolith": (20000, 200, 5, 150), "gem_cluster": (120, 0, 3, 50), "gold_cluster": (120, 0, 3, 50)}
SHORT = {"wolf": "wolf", "elk": "elk", "golem": "golem", "monolith": "monolith", "gem_cluster": "gem", "gold_cluster": "gold"}
CAPTURE = 5  # capture_time


def rect_tiles(r, W):
    return [y * W + x for y in range(r["minY"], r["maxY"]) for x in range(r["minX"], r["maxX"])]


class Grid:
    def __init__(self, m: dict):
        self.m = m
        W, H = self.W, self.H = m["width"], m["height"]
        t = m["terrain"]
        self.wall = set(t["wall"])
        self.hole = set(t["hole"])
        self.bush = set(t["bush"])
        self.altar = list(t["altar"])
        self.member = {}
        for gi, g in enumerate(m["groups"]):
            for mem in g["members"]:
                self.member[mem["y"] * W + mem["x"]] = gi
        self.spawn = {"A": rect_tiles(m["spawns"]["teamA"], W), "B": rect_tiles(m["spawns"]["teamB"], W)}
        base = bytearray(W * H)
        for i in self.wall | self.hole:
            base[i] = 1
        for i in self.altar:
            base[i] = 1
        self.terrain_block = base
        self.blocked = {}
        for side, other in (("A", "B"), ("B", "A")):
            b = bytearray(base)
            for i in self.spawn[other]:
                b[i] = 1
            for i in self.member:
                b[i] = 1
            self.blocked[side] = b

    def xy(self, i):
        return i % self.W, i // self.W

    def nbrs(self, i):
        W, H = self.W, self.H
        x, y = i % W, i // W
        for dx, dy in DIRS:
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H:
                yield ny * W + nx

    def bfs(self, side, sources, free=()):
        """Distances over walkable tiles; `free` tiles (e.g. a cleared group's members) are passable."""
        blocked = self.blocked[side]
        free = set(free)
        dist = [INF] * (self.W * self.H)
        dq = deque()
        for s in sources:
            dist[s] = 0
            dq.append(s)
        while dq:
            u = dq.popleft()
            du = dist[u] + 1
            for v in self.nbrs(u):
                if dist[v] > du and (not blocked[v] or v in free):
                    dist[v] = du
                    dq.append(v)
        return dist

    def adjacent_walkable(self, side, tiles, reach=1):
        blocked = self.blocked[side]
        out = set()
        W, H = self.W, self.H
        for i in tiles:
            x, y = i % W, i // W
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and not blocked[ny * W + nx]:
                        out.add(ny * W + nx)
        return out

    def dash_reach(self, side, sources):
        """Tiles reachable with walking plus dashes over holes (no cooldown limit)."""
        blocked = self.blocked[side]
        W, H = self.W, self.H
        seen = set(sources)
        dq = deque(sources)
        while dq:
            u = dq.popleft()
            x, y = u % W, u // W
            for v in self.nbrs(u):
                if not blocked[v] and v not in seen:
                    seen.add(v)
                    dq.append(v)
            for dx1, dy1 in DIRS:
                mx, my = x + dx1, y + dy1
                if not (0 <= mx < W and 0 <= my < H):
                    continue
                mid = my * W + mx
                # the intermediate may be a hole but not a wall, altar, unit or enemy spawn tile
                if blocked[mid] and mid not in self.hole:
                    continue
                for dx2, dy2 in DIRS:
                    nx, ny = mx + dx2, my + dy2
                    if 0 <= nx < W and 0 <= ny < H:
                        v = ny * W + nx
                        if not blocked[v] and v not in seen:
                            seen.add(v)
                            dq.append(v)
        return seen

    def min_cut(self, cap=12, src=None, dst=None):
        """Vertex-disjoint walking paths between two tile sets (default: the spawn regions;
        Edmonds-Karp on the node-split grid, unit tile capacity, capped) and the cut tiles when the
        flow is small."""
        W, H = self.W, self.H
        n = W * H
        ok = [not self.terrain_block[i] and i not in self.member for i in range(n)]
        src = set(self.spawn["A"]) if src is None else set(src)
        dst = set(self.spawn["B"]) if dst is None else set(dst)
        for i in src | dst:
            ok[i] = True
        node_flow = {}
        edge_flow = {}

        def ncap(i):
            return INF if (i in src or i in dst) else 1

        def search():
            parent = {}
            dq = deque()
            for s in src:
                parent[(s, 0)] = None
                dq.append((s, 0))
            while dq:
                node = dq.popleft()
                i, part = node
                if part == 0:
                    nxt = [(i, 1)] if node_flow.get(i, 0) < ncap(i) else []
                    nxt += [(j, 1) for j in self.nbrs(i) if ok[j] and edge_flow.get((j, i), 0) > 0]
                else:
                    if i in dst:
                        return node, parent
                    nxt = [(j, 0) for j in self.nbrs(i) if ok[j]]
                    if node_flow.get(i, 0) > 0:
                        nxt.append((i, 0))
                for v in nxt:
                    if v not in parent:
                        parent[v] = node
                        dq.append(v)
            return None, parent

        flow, reach = 0, None
        while flow < cap:
            end, parent = search()
            if end is None:
                reach = parent
                break
            node = end
            while parent[node] is not None:
                prev = parent[node]
                (i, pi), (j, pj) = prev, node
                if i == j:
                    node_flow[i] = node_flow.get(i, 0) + (1 if pi == 0 else -1)
                elif pi == 1:  # out(i) -> in(j)
                    if edge_flow.get((j, i), 0) > 0:
                        edge_flow[(j, i)] -= 1
                    else:
                        edge_flow[(i, j)] = edge_flow.get((i, j), 0) + 1
                else:  # in(i) -> out(j): reverse of out(j) -> in(i)
                    edge_flow[(j, i)] -= 1
                node = prev
            flow += 1
        cut = []
        if reach is not None and flow <= 4:
            cut = sorted(i for (i, part) in reach if part == 0 and (i, 1) not in reach)
        return flow, cut


def group_record(gr: Grid, g: dict, gi: int, dist: dict, altar_from_group):
    W = gr.W
    comp = {}
    hp = st = 0
    for mem in g["members"]:
        k = SHORT[mem["type"]]
        comp[k] = comp.get(k, 0) + 1
        h, s, _, _ = MON[mem["type"]]
        hp += h
        st += s
    tiles = [mem["y"] * W + mem["x"] for mem in g["members"]]
    xs = [mem["x"] for mem in g["members"]]
    ys = [mem["y"] for mem in g["members"]]
    rec = {"key": g["key"], "extent": [min(xs), min(ys), max(xs), max(ys)], "size": len(tiles),
           "comp": comp, "hp": hp, "strength": st, "gems": g["gemValue"], "gold": g["goldValue"],
           "cooldown": g["cooldown"], "harmless": st == 0,
           "max_range": max(MON[m["type"]][2] for m in g["members"]) if st else 0}
    for side in ("A", "B"):
        approach = gr.adjacent_walkable(side, tiles, 1)
        d = min((dist[side][t] for t in approach), default=INF)
        ranged = gr.adjacent_walkable(side, tiles, 3)
        dr = min((dist[side][t] for t in ranged), default=INF)
        rec[f"d{side}"] = d if d < INF else None
        rec[f"d{side}_ranged"] = dr if dr < INF else None
    da, db = rec["dA"], rec["dB"]
    if da is None and db is None:
        rec["owner"] = "unreachable"
    elif db is None:
        rec["owner"] = "A"
    elif da is None:
        rec["owner"] = "B"
    else:
        r = (da + 1) / (db + 1)
        rec["ratio"] = round(r, 3)
        rec["owner"] = "A" if r <= 0.8 else ("B" if r >= 1.25 else "contested")
    alt = altar_from_group[gi]
    rec["altar_walk"] = alt if alt < INF else None
    rec["bush_near"] = sum(1 for i in gr.bush if min(max(abs(i % W - x), abs(i // W - y)) for x, y in zip(xs, ys)) <= 3)
    return rec


def analyse(m: dict, label: str):
    gr = Grid(m)
    W, H = gr.W, gr.H
    dist = {s: gr.bfs(s, gr.spawn[s]) for s in ("A", "B")}
    # spawn to spawn: A walks to a tile adjacent to B's region (B's tiles are closed to A)
    near_b = gr.adjacent_walkable("A", gr.spawn["B"], 1)
    s2s = min((dist["A"][t] for t in near_b), default=INF)
    # altars: approach tiles within Chebyshev 1 (submit range)
    altars = []
    for a in gr.altar:
        rec = {"x": a % W, "y": a // W}
        for side in ("A", "B"):
            ap = gr.adjacent_walkable(side, [a], 1)
            d = min((dist[side][t] for t in ap), default=INF)
            rec[f"d{side}"] = d if d < INF else None
        altars.append(rec)
    altar_ap = {s: sorted(gr.adjacent_walkable(s, gr.altar, 1)) for s in ("A", "B")}
    # group-start fields (A's blocking rules; the maps are symmetric, so B mirrors A)
    groups = m["groups"]
    tiles = [[mem["y"] * W + mem["x"] for mem in g["members"]] for g in groups]
    gfield = [gr.bfs("A", tiles[gi], free=tiles[gi]) for gi in range(len(groups))]
    approach = [sorted(gr.adjacent_walkable("A", tiles[gi], 1)) for gi in range(len(groups))]
    altar_from_group = [min((gfield[gi][t] for t in altar_ap["A"]), default=INF) for gi in range(len(groups))]
    grecs = [group_record(gr, g, gi, dist, altar_from_group) for gi, g in enumerate(groups)]
    # per-altar travel: altar approach -> group approach, group members -> altar approach
    alt_to_group, group_to_alt = [], []
    for a in gr.altar:
        ap = sorted(gr.adjacent_walkable("A", [a], 1))
        if not ap:
            alt_to_group.append([None] * len(groups))
            group_to_alt.append([None] * len(groups))
            continue
        fa = gr.bfs("A", ap)
        alt_to_group.append([min((fa[t] for t in approach[gi]), default=INF) for gi in range(len(groups))])
        group_to_alt.append([min((gfield[gi][t] for t in ap), default=INF) for gi in range(len(groups))])
    alt_to_group = [[x if x is not None and x < INF else None for x in row] for row in alt_to_group]
    group_to_alt = [[x if x is not None and x < INF else None for x in row] for row in group_to_alt]
    # open regime: every camp cleared (member tiles passable); gates = groups whose clearing opens
    # an altar or shortens the spawn-to-spawn walk
    all_members = set(gr.member)
    dist_open = gr.bfs("A", gr.spawn["A"], free=all_members)
    near_b_open = set()
    for t in gr.spawn["B"]:
        x, y = t % W, t // W
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H:
                    v = ny * W + nx
                    if (not gr.blocked["A"][v]) or v in all_members:
                        near_b_open.add(v)
    s2s_open = min((dist_open[t] for t in near_b_open), default=INF)
    for k, a in enumerate(gr.altar):
        ap_open = []
        ax, ay = a % W, a // W
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = ax + dx, ay + dy
                if 0 <= nx < W and 0 <= ny < H:
                    v = ny * W + nx
                    if (not gr.blocked["A"][v]) or v in all_members:
                        ap_open.append(v)
        d = min((dist_open[t] for t in ap_open), default=INF)
        altars[k]["dA_open"] = d if d < INF else None
        if altars[k]["dA"] is None and d < INF:
            gates = []
            for gi in range(len(groups)):
                f = gr.bfs("A", gr.spawn["A"], free=set(tiles[gi]))
                ap_g = [t for t in ap_open if not gr.blocked["A"][t] or t in tiles[gi]]
                if min((f[t] for t in ap_g), default=INF) < INF:
                    gates.append(groups[gi]["key"])
            altars[k]["gated_by"] = gates
    # travel matrix: from group i's member tiles (cleared) to group j's approach tiles
    G = len(groups)
    travel = [[0] * G for _ in range(G)]
    for i in range(G):
        for j in range(G):
            if i != j:
                d = min((gfield[i][t] for t in approach[j]), default=INF)
                travel[i][j] = d if d < INF else None
    # terrain
    walkable_a = sum(1 for i in range(W * H) if not gr.blocked["A"][i])
    reach_walk = {i for i in range(W * H) if dist["A"][i] < INF}
    reach_dash = gr.dash_reach("A", gr.spawn["A"])
    dash_only = reach_dash - reach_walk
    dash_groups = []
    for gi, g in enumerate(groups):
        ap = set(approach[gi])
        if ap and not (ap & reach_walk) and (ap & reach_dash):
            dash_groups.append(g["key"])
    unreachable = [g["key"] for gi, g in enumerate(groups) if grecs[gi]["dA"] is None and g["key"] not in dash_groups]
    flow, cut = gr.min_cut()
    # mid-map width: disjoint paths between the rings at walking distance 6 from each spawn
    # (excludes the spawn exits, which bound the spawn-to-spawn flow on every map)
    mid_flow, mid_cut = None, []
    if s2s < INF and s2s >= 16:
        ring_a = [i for i in range(W * H) if dist["A"][i] == 6]
        ring_b = [i for i in range(W * H) if dist["B"][i] == 6]
        if ring_a and ring_b:
            mid_flow, mid_cut = gr.min_cut(cap=12, src=ring_a, dst=ring_b)
    # path tortuosity: walking distance / Chebyshev distance between spawn centres
    ca = [(r["minX"] + r["maxX"] - 1) / 2 for r in (m["spawns"]["teamA"],)] + [(m["spawns"]["teamA"]["minY"] + m["spawns"]["teamA"]["maxY"] - 1) / 2]
    cb = [(m["spawns"]["teamB"]["minX"] + m["spawns"]["teamB"]["maxX"] - 1) / 2, (m["spawns"]["teamB"]["minY"] + m["spawns"]["teamB"]["maxY"] - 1) / 2]
    cheb = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]))
    bush_on_path = 0
    if s2s < INF:
        dist_to_b = gr.bfs("A", sorted(near_b))
        for i in gr.bush:
            if dist["A"][i] < INF and dist["A"][i] + dist_to_b[i] <= s2s + 2:
                bush_on_path += 1
    terrain = {"walls": len(gr.wall), "holes": len(gr.hole), "bushes": len(gr.bush), "walkable": walkable_a,
               "bush_frac": round(len(gr.bush) / max(1, walkable_a + len(gr.bush)), 3), "bush_on_spawn_path": bush_on_path,
               "disjoint_paths": flow, "cut_tiles": [[i % W, i // W] for i in cut],
               "mid_paths": mid_flow, "mid_cut_tiles": [[i % W, i // W] for i in mid_cut],
               "dash_only_tiles": len(dash_only), "dash_only_groups": dash_groups, "unreachable_groups": unreachable,
               "tortuosity": round(s2s / cheb, 3) if s2s < INF and cheb else None}
    rec = {"id": m["id"], "set": label, "width": W, "height": H, "area": W * H, "draft": m["draftCount"],
           "symmetry": m["symmetry"], "spawns": {"A": m["spawns"]["teamA"], "B": m["spawns"]["teamB"]},
           "spawn_to_spawn": s2s if s2s < INF else None, "spawn_to_spawn_open": s2s_open if s2s_open < INF else None,
           "altars": altars, "groups": grecs, "terrain": terrain}
    rec["metrics"] = metrics(rec)
    rec["classes"] = classify(rec)
    trav = {"groups": [g["key"] for g in groups], "travel": travel, "alt_to_group": alt_to_group, "group_to_alt": group_to_alt,
            "altar_spawnA_each": [a["dA"] for a in altars],
            "altar": [x if x < INF else None for x in altar_from_group],
            "spawnA": [g["dA"] for g in grecs], "spawnB": [g["dB"] for g in grecs],
            "altar_spawnA": min((a["dA"] for a in altars if a["dA"] is not None), default=None),
            "altar_spawnB": min((a["dB"] for a in altars if a["dB"] is not None), default=None)}
    return rec, trav


def cycle(g):
    """Respawn-limited cycle of one group held by an idle champion: cooldown + capture + a
    nominal clear (harmless: 1 round per cluster at >= 120 damage; others counted separately)."""
    return g["cooldown"] + CAPTURE + 1


def metrics(rec):
    gs = [g for g in rec["groups"] if g["owner"] != "unreachable"]
    gems = sum(g["gems"] for g in gs)
    gold = sum(g["gold"] for g in gs)
    harmless = [g for g in gs if g["harmless"]]
    big = [g for g in gs if g["comp"].get("golem") or g["comp"].get("monolith")]
    beast = [g for g in gs if not g["harmless"]]
    out = {
        "groups": len(gs),
        "gems_total": gems, "gold_total": gold,
        "gem_rate_cap": round(sum(g["gems"] / cycle(g) for g in gs), 3),
        "gold_rate_cap": round(sum(g["gold"] / cycle(g) for g in gs), 3),
        "harmless_groups": len(harmless),
        "harmless_gem_rate": round(sum(g["gems"] / cycle(g) for g in harmless), 3),
        "harmless_gold_rate": round(sum(g["gold"] / cycle(g) for g in harmless), 3),
        "gem_share_big": round(sum(g["gems"] for g in big) / gems, 3) if gems else 0.0,
        "gem_share_harmless": round(sum(g["gems"] for g in harmless) / gems, 3) if gems else 0.0,
        "monster_hp_total": sum(g["hp"] for g in beast),
        "hp_per_gem": round(sum(g["hp"] for g in beast) / max(1, sum(g["gems"] for g in beast)), 1),
    }
    for side in ("A",):
        own = [g for g in gs if g["owner"] == side]
        con = [g for g in gs if g["owner"] == "contested"]
        out["own_gem_rate"] = round(sum(g["gems"] / cycle(g) for g in own), 3)
        out["contested_gem_rate"] = round(sum(g["gems"] / cycle(g) for g in con), 3)
        out["own_harmless_gem_rate"] = round(sum(g["gems"] / cycle(g) for g in own if g["harmless"]), 3)
        dists = [g["dA"] for g in own if g["dA"] is not None]
        out["own_mean_dist"] = round(sum(dists) / len(dists), 1) if dists else None
    alt = [a["dA"] for a in rec["altars"] if a["dA"] is not None]
    out["altar_from_spawn"] = min(alt) if alt else None
    walks = [g["altar_walk"] for g in gs if g["altar_walk"] is not None and g["owner"] in ("A", "contested")]
    out["altar_walk_mean"] = round(sum(walks) / len(walks), 1) if walks else None
    return out


def classify(rec):
    m, t = rec["metrics"], rec["terrain"]
    classes = []
    if m["harmless_gem_rate"] >= 1.4:
        classes.append("gem-rush")
    if m["gem_share_big"] >= 0.4:
        classes.append("monster-locked")
    if m["gold_rate_cap"] >= 2 * max(1.0, m["gem_rate_cap"]) * 6:
        classes.append("gold-rich")
    width = t["mid_paths"] if t["mid_paths"] is not None else t["disjoint_paths"]
    if width <= 2 or (t["tortuosity"] or 0) >= 1.6:
        classes.append("corridor")
    elif width >= 8:
        classes.append("open")
    if (rec["spawn_to_spawn"] or 0) <= 20:
        classes.append("close-spawns")
    if (m["altar_walk_mean"] or 0) >= 20:
        classes.append("far-altars")
    return classes


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out-dir", default=str(DATA))
    a = p.parse_args()
    out_dir = Path(a.out_dir)
    pool = json.loads((ROOT / "maps" / "geometry.json").read_text(encoding="utf-8"))["maps"]
    legacy_path = DATA / "maps" / "legacy-geometry.json"
    legacy = json.loads(legacy_path.read_text(encoding="utf-8"))["maps"] if legacy_path.exists() else []
    recs, travels = [], {}
    for label, maps in (("pool", pool), ("legacy", legacy)):
        for m in maps:
            rec, trav = analyse(m, label)
            key = rec["id"] if label == "pool" else "legacy/" + rec["id"]
            rec["key"] = key
            recs.append(rec)
            travels[key] = trav
            mm = rec["metrics"]
            print(f"{key:24s} {rec['width']:3d}x{rec['height']:<3d} d{rec['draft']} s2s {rec['spawn_to_spawn']} "
                  f"paths {rec['terrain']['disjoint_paths']:2d} gemcap {mm['gem_rate_cap']:5.2f} harmless {mm['harmless_gem_rate']:4.2f} "
                  f"big {mm['gem_share_big']:.2f} {','.join(rec['classes'])}")
    cap_path = out_dir / "economy" / "capacity.json"
    if cap_path.exists():  # speedrun capacity (capacity.py) survives an atlas regeneration
        cap = json.loads(cap_path.read_text(encoding="utf-8"))
        for rec in recs:
            if rec["key"] in cap:
                rec["capacity"] = cap[rec["key"]]
    (out_dir / "maps").mkdir(parents=True, exist_ok=True)
    doc = {"schema": "contenders-atlas/1", "sources": ["maps/geometry.json", "bots/main/foundation/data/maps/legacy-geometry.json"],
           "notes": "distances are walking rounds (king moves) from the side's spawn region to a tile adjacent to a member "
                    "(dX) or within 3 (dX_ranged); owner by (dA+1)/(dB+1): <=0.8 A, >=1.25 B, else contested",
           "maps": recs}
    (out_dir / "maps.json").write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    (out_dir / "maps" / "travel.json").write_text(json.dumps(travels) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
