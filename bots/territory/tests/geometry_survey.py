"""Read-only territory opportunity survey; no build, game, or admission decision.

The shortlist and cut predicates mirror competitive.h/runtime.h on immutable
geometry. Canonical routes only nominate Gatehouse/Veil cases: SDK path ties,
live vision, guard contact, wards, feasibility and arbitration still need a game.
"""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
GEOMETRY = ROOT / "maps" / "geometry.json"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rectangle(bounds: dict, width: int) -> set[int]:
    # Geometry bounds are half-open; some maxX/maxY equal map width/height.
    return {y * width + x for y in range(bounds["minY"], bounds["maxY"])
            for x in range(bounds["minX"], bounds["maxX"])}


class Graph:
    def __init__(self, record: dict, side: str):
        self.record = record
        self.width, self.height = record["width"], record["height"]
        width, height = self.width, self.height
        self.adj = [[yy * width + xx
                     for yy in range(max(0, t // width - 1), min(height, t // width + 2))
                     for xx in range(max(0, t % width - 1), min(width, t % width + 2))
                     if yy * width + xx != t] for t in range(width * height)]
        enemy = "teamB" if side == "teamA" else "teamA"
        self.home = rectangle(record["spawns"][side], width)
        terrain = record["terrain"]
        self.holes, self.bushes = set(terrain["hole"]), set(terrain["bush"])
        self.blocked = (set(terrain["wall"] + terrain["hole"] + terrain["altar"])
                        | rectangle(record["spawns"][enemy], width))

    def flood(self, roots, extra=frozenset(), cap: int | None = None):
        distance = {t: 0 for t in sorted(roots) if t not in self.blocked and t not in extra}
        parent = {t: t for t in distance}
        queue = deque(distance)
        while queue:
            tile = queue.popleft()
            if cap is not None and distance[tile] >= cap:
                continue
            for other in self.adj[tile]:
                if other not in distance and other not in self.blocked and other not in extra:
                    distance[other] = distance[tile] + 1
                    parent[other] = tile
                    queue.append(other)
        return distance, parent


def minimum(field: dict[int, int], cells) -> int | None:
    return min((field[t] for t in cells if t in field), default=None)


def shortlist(groups: list[dict], cells: list[set[int]], home: dict[int, int]) -> list[int]:
    distance = [minimum(home, c) for c in cells]
    ranked = sorted((i for i, d in enumerate(distance) if d is not None),
                    key=lambda i: (distance[i], -groups[i]["gemValue"], groups[i]["nativeId"]))
    selected = ranked[:4]
    selected.extend(i for i in ranked if i not in selected and groups[i]["gemValue"] > 0)
    selected.extend(i for i in ranked if i not in selected)
    return selected[:8]


def source_row(group: dict) -> dict:
    return {"group": group["nativeId"], "gems": group["gemValue"],
            "member_tiles": [m["tile"] for m in group["members"]]}


def survey_side(record: dict, side: str) -> dict:
    graph = Graph(record, side)
    # runtime.h compute() caps immutable navigation fields at 256 steps.
    home, _ = graph.flood(graph.home, cap=256)
    full_home, _ = graph.flood(graph.home)
    groups = record["groups"]
    cells = [{m["tile"] for m in g["members"]} for g in groups]
    chosen = shortlist(groups, cells, home)
    disconnected = [i for i, c in enumerate(cells) if minimum(full_home, c) is None]
    range_limited = [i for i, c in enumerate(cells)
                     if minimum(full_home, c) is not None and minimum(home, c) is None]

    cuts = []
    for index in chosen:
        # runtime.h geometry(): remove this group's spawn set, then a full
        # field from ally spawn; another selected home-connected group is lost
        # only when every one of its spawn tiles is outside the reached set.
        remaining, _ = graph.flood(graph.home, cells[index])
        lost = [j for j in chosen if j != index and not cells[j].intersection(remaining)]
        if lost:
            cuts.append({"gate": groups[index]["nativeId"],
                         "downstream": [groups[j]["nativeId"] for j in lost]})

    one_hole = []
    for index in disconnected:
        component, _ = graph.flood(cells[index])
        bridges = sorted(h for h in graph.holes
                         if any(t in full_home for t in graph.adj[h])
                         and any(t in component for t in graph.adj[h]))
        if bridges:
            one_hole.append({**source_row(groups[index]), "bridge_holes": bridges})

    # Gatehouse/Veil candidate paths use a documented canonical BFS. Map export
    # sorts group members by tile, so it no longer preserves the native camps()
    # ordering used for Group.anchor. Lowest native camp ID is an explicit
    # approximation, not claimed as an SDK route/anchor witness.
    altars = sorted(record["terrain"]["altar"],
                    key=lambda t: (minimum(home, graph.adj[t])
                                   if minimum(home, graph.adj[t]) is not None else 100000, t))[:2]
    ports = [graph.flood(graph.adj[t], cap=256) for t in altars]
    gates, veils = [], []
    for index in chosen:
        group = groups[index]
        anchor = min(group["members"], key=lambda m: m["nativeId"])["tile"]
        options = [(minimum(field, cells[index]), p) for p, (field, _) in enumerate(ports)]
        options = [(d, p) for d, p in options if d is not None]
        if not options:
            continue
        _, port = min(options)
        field, parent = ports[port]
        if anchor not in field:
            continue
        route = [anchor]
        while parent[route[-1]] != route[-1]:
            route.append(parent[route[-1]])
        local_gate = next((t for t in route[1:-1]
                           if sum(n not in graph.blocked for n in graph.adj[t]) <= 3), None)
        if local_gate is not None:
            source_field, _ = graph.flood(cells[index], cap=256)
            near = [groups[j]["nativeId"] for j in chosen if j != index
                    and (d := minimum(source_field, cells[j])) is not None and d <= 10]
            gates.append({**source_row(group), "connector": local_gate,
                          "guard_jobs_within_10": near, "canonical_route": route})
        exits = [(route[k], route[k + 1]) for k in range(len(route) - 1)
                 if route[k] in graph.bushes and route[k + 1] not in graph.bushes]
        if exits:
            veils.append({**source_row(group), "bush_to_open_edges": exits,
                          "canonical_route": route})
    return {"map": Path(record["mapPath"]).stem, "side": side,
            "map_sha256": record["mapSha256"],
            "shortlist": [groups[i]["nativeId"] for i in chosen],
            "shortlist_sources": [{"source_index": n, "group_key": groups[i]["key"],
                                    **source_row(groups[i])} for n, i in enumerate(chosen)],
            "selected_home_disconnected": [groups[i]["nativeId"] for i in chosen
                                           if minimum(home, cells[i]) is None],
            "home_disconnected": [source_row(groups[i]) for i in disconnected],
            "range_limited": [groups[i]["nativeId"] for i in range_limited],
            "one_hole_component_opportunities": one_hole, "causeway_cuts": cuts,
            "gatehouse_route_candidates": gates, "veil_route_candidates": veils}


def self_test() -> int:
    """A real cut and a disconnected one-hole source must remain detectable."""
    record = {"width": 7, "height": 1, "mapPath": "synthetic-corridor.map26a",
              "mapSha256": "synthetic-not-a-pool-map",
              "spawns": {"teamA": {"minX": 0, "maxX": 1, "minY": 0, "maxY": 1},
                         "teamB": {"minX": 6, "maxX": 7, "minY": 0, "maxY": 1}},
              "terrain": {"wall": [], "hole": [], "altar": [], "bush": []},
              "groups": [{"nativeId": i, "key": f"g_{t}", "gemValue": 10,
                          "members": [{"nativeId": i, "tile": t, "x": t, "y": 0}]}
                         for i, t in enumerate((1, 4))]}
    connected = survey_side(record, "teamA")
    assert connected["shortlist"] == [0, 1]
    assert connected["causeway_cuts"] == [{"gate": 0, "downstream": [1]}]
    record["terrain"]["hole"] = [2]
    disconnected = survey_side(record, "teamA")
    assert disconnected["shortlist"] == [0] and disconnected["home_disconnected"][0]["group"] == 1
    assert disconnected["one_hole_component_opportunities"][0]["group"] == 1
    return 4


def survey() -> dict:
    checks = self_test()
    data = json.loads(GEOMETRY.read_text(encoding="utf-8"))
    for record in data["maps"]:
        path = Path(record["mapPath"])
        if not path.is_file() or digest(path) != record["mapSha256"]:
            raise ValueError(f"Missing or changed map geometry: {path}")
    sides = [survey_side(record, side) for record in data["maps"]
             for side in ("teamA", "teamB")]
    sources = [GEOMETRY, Path(__file__),
               ROOT / "bots/territory/common/src/competitive.h",
               ROOT / "bots/territory/common/src/runtime.h",
               ROOT / "bots/territory/common/src/native_nav.h"]
    return {"schema": "territory-static-opportunities-v1", "synthetic_positive_checks": checks,
            "scope": "Current 40-map pool, both sides; static opportunity only, no admission",
            "assumptions": [
                "Eight-neighbor movement without diagonal corner restriction; half-open spawn bounds",
                "Initial fields capped at 256; causeway cut field is uncapped",
                "Non-Ports production shortlist: four nearest reachable, then gem-bearing, then remaining",
                "One-hole opportunities connect components; not exact mobility circuits or viable cycles",
                "Canonical BFS and lowest-native-camp-ID anchors nominate gate/veil cases only; SDK ties unverified",
                "No claim of visibility, safety, guard contact, owned ward, activation or continuation"],
            "input_hashes": {p.relative_to(ROOT).as_posix(): digest(p) for p in sources},
            "summary": {"maps": len(data["maps"]), "map_sides": len(sides),
                        "causeway_cut_map_sides": sum(bool(s["causeway_cuts"]) for s in sides),
                        "selected_home_disconnected": sum(len(s["selected_home_disconnected"]) for s in sides),
                        "disconnected_map_sides": sum(bool(s["home_disconnected"]) for s in sides),
                        "one_hole_opportunity_map_sides": sum(bool(s["one_hole_component_opportunities"]) for s in sides),
                        "gatehouse_canonical_candidate_map_sides": sum(bool(s["gatehouse_route_candidates"]) for s in sides),
                        "veil_canonical_candidate_map_sides": sum(bool(s["veil_route_candidates"]) for s in sides)},
            "map_sides": sides}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = survey()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(result["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
