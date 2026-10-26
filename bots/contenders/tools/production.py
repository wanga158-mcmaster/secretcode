"""Compare saved native production, survival and equipment at fixed horizons.

  python -B -I -X utf8 bots/contenders/tools/production.py RUN ... --variant base
         --opponent anchors/starter-cpp --opponent anchors/main-v40 --json OUT

  python -B -I -X utf8 bots/contenders/tools/production.py native STATS.json --team teamB
         [--json OUT]

Uses statistics beside the recorded replay path, not replay decoding or new games.
Separates exact subject, engine, variant, opponent and the two sides of each match.
Only matches reaching a horizon contribute to it; shorter wins are not extrapolated.
Clear/capture cycles join native input/ordinal order before the next clear of a group.
Travel and private decisions remain unknown from these native statistics.
"""
from __future__ import annotations

import argparse
import collections
import gzip
import hashlib
import itertools
import json
import statistics
import sys
from decimal import Decimal, InvalidOperation
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cohort
import fundamentals as fu
import onlinedefs as od

ROOT = Path(__file__).resolve().parents[3]


# Native terminal totals are authoritative here; missing timelines must not become zero.
NATIVE_METRICS = ("monsterDamageDone", "championDamageDone", "monsterDamageTaken",
                  "championDamageTaken", "kills", "deaths", "earnedGold", "spentGold",
                  "gold", "saleRefunds", "collectedGems", "submittedGems", "gems")
NATIVE_EVENTS = ("campClear", "gemCollection", "gemSubmission")


class NativeStatisticsError(ValueError):
    """Required native evidence is unavailable, malformed or ambiguously attributed."""


def _native_number(value, label: str):
    if isinstance(value, bool) or not isinstance(value, (int, float, str)):
        raise NativeStatisticsError(f"{label}: expected a native number")
    try:
        number = Decimal(str(value))
    except InvalidOperation as exc:
        raise NativeStatisticsError(f"{label}: invalid native number {value!r}") from exc
    if not number.is_finite() or number < 0:
        raise NativeStatisticsError(f"{label}: expected a finite nonnegative number")
    return int(number) if number == number.to_integral_value() else float(number)


def _native_count(value, label: str) -> int:
    if type(value) is int and value >= 0:
        return value
    if isinstance(value, str) and value.isascii() and value.isdecimal():
        return int(value)
    raise NativeStatisticsError(f"{label}: expected a nonnegative decimal integer")


def _native_group(target: dict, label: str) -> None:
    if (target["kind"] != "campGroup" or type(target["id"]) is not int
            or target["id"] < 0):
        raise NativeStatisticsError(f"{label}: invalid native campGroup target")


def _native_identity(row: dict) -> tuple:
    cid, name, team = row["id"], row["name"], row["team"]
    if (type(cid) is not int or cid < 0 or not isinstance(name, str) or not name
            or team not in ("teamA", "teamB")):
        raise NativeStatisticsError("invalid native champion identity")
    return cid, name, team


def native_terminal(stats: dict, team: str) -> dict:
    """Reduce ONE original native statistics object for teamA or teamB.

    Required missing data raises NativeStatisticsError, never a synthetic zero.
    Metrics come from champions[].metrics, not event-kind counts or timelines.
    Empty native streams are genuine zero events; receipts are per recipient,
    including zero-gem receipts, and are not deduplicated into capture episodes.
    Final-clear credit cannot measure every actor's clearing labor. No private
    idleness, health or strength conclusion follows from this terminal report.
    """
    if team not in ("teamA", "teamB"):
        raise NativeStatisticsError("team must be teamA or teamB")
    try:
        if (stats["formatVersion"] != "1.0" or stats["jsonFormatVersion"] != "1.0"
                or type(stats["match"]["metricSemanticsVersion"]) is not int
                or stats["match"]["metricSemanticsVersion"] != 1):
            raise NativeStatisticsError("unsupported native statistics schema/metric semantics")
        for field in ("roundsCompleted", "roundsStarted"):
            _native_count(stats["match"][field], f"match.{field}")
        if stats["diagnostics"]["detailedCaptureComplete"] is not True:
            raise NativeStatisticsError("native events unavailable: detailed capture is incomplete")
        roster, champions = {}, {}
        for row in stats["roster"]["champions"]:
            identity = _native_identity(row)
            if identity[0] in roster:
                raise NativeStatisticsError("duplicate native roster champion")
            roster[identity[0]] = identity
        for row in stats["champions"]:
            identity = _native_identity(row)
            if identity[0] in champions or roster[identity[0]] != identity:
                raise NativeStatisticsError("native champion/roster owner mismatch")
            champions[identity[0]] = row
        if set(champions) != set(roster):
            raise NativeStatisticsError("native champion terminal metrics unavailable")
        selected = [cid for cid in sorted(champions) if champions[cid]["team"] == team]
        if not selected:
            raise NativeStatisticsError(f"no native champions for {team}")
        streams = {}
        for event in stats["events"]:
            kind = event["kind"]
            if kind not in NATIVE_EVENTS:
                continue
            owner = event["owner"]
            identity = _native_identity(owner)
            if owner["kind"] != "champion" or roster[identity[0]] != identity:
                raise NativeStatisticsError(f"{kind}: native event owner mismatch")
            key = identity[0], kind
            if key in streams or not isinstance(event["entries"], list):
                raise NativeStatisticsError(f"{kind}: duplicate or invalid native event stream")
            streams[key] = event["entries"]
        output = []
        for cid in selected:
            row = champions[cid]
            metrics = {name: (_native_number if "Damage" in name else _native_count)(
                           row["metrics"][name], f"champion {cid}.{name}")
                       for name in NATIVE_METRICS}
            events = {kind: streams[cid, kind] for kind in NATIVE_EVENTS}
            for kind, entries in events.items():
                for entry in entries:
                    value = _native_count(entry["value"], f"champion {cid}.{kind}.value")
                    if not isinstance(entry["timestamp"], dict):
                        raise NativeStatisticsError(f"{kind}: missing native timestamp")
                    _native_count(entry["timestamp"]["round"], f"{kind}.round")
                    if kind == "campClear":
                        if value != 1:
                            raise NativeStatisticsError("invalid native final-clear credit")
                        _native_group(entry["target"], kind)
                    elif kind == "gemCollection":
                        if not isinstance(entry["context"], str):
                            raise NativeStatisticsError("missing native receipt context")
                        if entry["context"] == "campGroup":
                            _native_group(entry["target"], kind)
                        else:
                            entry["target"]
            counts = {"final_clear_credits": len(events["campClear"]),
                      "gem_receipt_events": len(events["gemCollection"]),
                      "received_gems": sum(_native_count(e["value"], "gemCollection.value")
                                           for e in events["gemCollection"]),
                      "bank_events": len(events["gemSubmission"]),
                      "banked_gems": sum(_native_count(e["value"], "gemSubmission.value")
                                         for e in events["gemSubmission"])}
            output.append({"id": cid, "name": row["name"], "team": row["team"],
                           "metrics": metrics, "event_totals": counts, "events": events})
        return {"schema": 1, "team": team, "match": stats["match"],
                "metrics": {name: sum(c["metrics"][name] for c in output)
                            for name in NATIVE_METRICS},
                "event_totals": {name: sum(c["event_totals"][name] for c in output)
                                 for name in output[0]["event_totals"]},
                "champions": output,
                "definitions": {
                    "metrics": "Terminal champions[].metrics summed by native team ownership.",
                    "final_clear_credits": "campClear entries; not all clearing labor.",
                    "gem_receipt_events": "gemCollection entries per recipient, including zero gems; not unique captures.",
                    "bank_events": "gemSubmission entries; banked_gems is their actual value sum."},
                "limits": ["Terminal counters do not establish recurring work, private idleness or strength.",
                           "Damage totals are not attributed to individual camp groups.",
                           "This reducer does not assess runner, fuel, faults or logged errors."]}
    except (KeyError, TypeError, IndexError) as exc:
        raise NativeStatisticsError(f"required native metric/schema unavailable: {exc}") from exc


def read_native_terminal(path: Path, team: str) -> dict:
    """Read original .json/.json.gz statistics without games or replay decoding."""
    path = Path(path)
    payload = path.read_bytes()
    result = native_terminal(json.loads(gzip.decompress(payload) if path.suffix == ".gz" else payload), team)
    result["statistics"] = {"path": str(path.resolve()),
                            "sha256": hashlib.sha256(payload).hexdigest()}
    return result


def native_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=native_terminal.__doc__)
    parser.add_argument("statistics", type=Path)
    parser.add_argument("--team", required=True, choices=("teamA", "teamB"))
    parser.add_argument("--json", type=Path, help="write JSON here instead of stdout")
    args = parser.parse_args(argv)
    try:
        result = read_native_terminal(args.statistics, args.team)
    except (NativeStatisticsError, OSError, ValueError) as exc:
        parser.exit(2, f"native statistics unavailable: {exc}\n")
    rendered = json.dumps(result, indent=1) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        print(rendered, end="")
    return 0


def timestamp(entry: dict) -> tuple:
    value = entry["timestamp"]
    return (value["round"], value.get("input", 0), value.get("ordinal", 0), value.get("substep", 0))


def cycles(stats: dict) -> list[dict]:
    """A capture is a group cycle, even when split across recipients or worth zero gems."""
    clears, captures = collections.defaultdict(list), {}
    for event in stats["events"]:
        owner = event.get("owner") or {}
        for entry in event["entries"]:
            group = (entry.get("target") or {}).get("id")
            if event["kind"] == "campClear":
                clears[group].append({"time": timestamp(entry), "team": owner.get("team")})
            elif event["kind"] == "gemCollection" and entry.get("context") == "campGroup":
                key = (group, entry["timestamp"]["round"])
                capture = captures.setdefault(key, {"time": timestamp(entry), "teams": set()})
                capture["time"] = min(capture["time"], timestamp(entry))
                capture["teams"].add(owner.get("team"))
    out = []
    for group, group_clears in clears.items():
        group_clears.sort(key=lambda value: value["time"])
        group_captures = sorted((value for (gid, _), value in captures.items() if gid == group),
                                key=lambda value: value["time"])
        for index, clear in enumerate(group_clears):
            next_clear = group_clears[index + 1]["time"] if index + 1 < len(group_clears) else (float("inf"),)
            capture = next((value for value in group_captures
                            if clear["time"] <= value["time"] < next_clear), None)
            out.append({"group": group, "clear": clear, "capture": capture})
    return out


def side_measure(rec: dict, raw: dict, joined: list[dict], horizon: int) -> dict:
    team = rec["team"]
    count = lambda entries: sum(entry[0] <= horizon for entry in entries)
    deaths = [entry for entry in rec["deaths_list"] if entry["r"] <= horizon]
    own_cycles = [value for value in joined if value["clear"]["team"] == team and
                  value["clear"]["time"][0] <= horizon]
    conversion = collections.Counter()
    delays = []
    for value in own_cycles:
        capture = value["capture"]
        if capture is None:
            conversion["no_capture_before_next_clear_or_end"] += 1
        elif capture["time"][0] > horizon:
            conversion["capture_after_horizon"] += 1
        elif capture["teams"] == {team}:
            conversion["own_capture_by_horizon"] += 1
            delays.append(capture["time"][0] - value["clear"]["time"][0])
        elif team not in capture["teams"]:
            conversion["opponent_capture_by_horizon"] += 1
        else:
            conversion["mixed_team_receipt"] += 1
    losses = collections.Counter()
    for event in raw["events"]:
        if event["kind"] == "gemLoss" and (event.get("owner") or {}).get("team") == team:
            for entry in event["entries"]:
                if entry["timestamp"]["round"] <= horizon:
                    losses[entry["context"]] += int(entry["value"] or 0)
    shards, emblems = collections.Counter(), collections.Counter()
    purchases = []
    for champion in rec["champs"]:
        if champion["buys"] and champion["buys"][0][0] <= horizon:
            purchases.append(champion["buys"][0][0])
        for round_number, name, number, *_ in champion["buys"]:
            if round_number <= horizon and " Shard of " in name:
                shards[name.split(" Shard of ")[1]] += number
        for round_number, name in champion["emblems"]:
            if round_number <= horizon:
                emblems[name.split(" Emblem of ")[0]] += 1
    cats = collections.Counter(entry[1] for entry in rec["caps"] if entry[0] <= horizon)
    banked = sum(entry[1] for entry in rec["tributes"] if entry[0] <= horizon)
    dead_proxy = sum(min(60, horizon - entry["r"]) for entry in deaths)
    bodies = len(rec["champs"])
    alive_proxy = bodies * horizon - dead_proxy
    return {"horizon": horizon, "bodies": bodies, "draft": rec["draft"],
            "gold": rec["earnedGold"][str(horizon)], "spent": rec["spentGold"][str(horizon)],
            "collected": rec["collectedGems"][str(horizon)], "banked": banked,
            "bag": rec["gems"][str(horizon)], "clears": count(rec["clears"]),
            "captures": count(rec["caps"]), "capture_types": dict(cats),
            "first_clear": rec["clears"][0][0] if rec["clears"] else None,
            "first_capture": rec["caps"][0][0] if rec["caps"] else None,
            "first_champion_damage_dealt": rec["first_cdmg"], "deaths": len(deaths),
            "death_context": dict(collections.Counter(entry["ctx"] for entry in deaths)),
            "dead_rounds_proxy": dead_proxy,
            "gold_per_alive_body_round_proxy": rec["earnedGold"][str(horizon)] / alive_proxy if alive_proxy else None,
            "monster_damage": rec["monsterDamageDone"][str(horizon)],
            "champion_damage": rec["championDamageDone"][str(horizon)],
            "champion_damage_taken": rec["championDamageTaken"][str(horizon)],
            "gem_losses": dict(losses), "clear_conversion": dict(conversion),
            "own_clear_capture_delays": delays, "shards": dict(shards), "emblems": dict(emblems),
            "first_buys": purchases,
            "bought_by_100": sum(value <= 100 for value in purchases)}


def aggregate(items: list[dict]) -> dict:
    means = ("bodies", "gold", "spent", "collected", "banked", "bag", "clears", "captures",
             "deaths", "dead_rounds_proxy", "gold_per_alive_body_round_proxy", "monster_damage", "champion_damage",
             "champion_damage_taken")
    out = {"n": len(items), "means": {name: statistics.mean(item[name] for item in items
            if item[name] is not None) if any(item[name] is not None for item in items) else None for name in means}}
    for name in ("capture_types", "death_context", "gem_losses", "clear_conversion", "shards", "emblems"):
        counts = collections.Counter()
        for item in items:
            counts.update(item[name])
        out[name] = dict(counts)
    for name in ("first_clear", "first_capture", "first_champion_damage_dealt"):
        values = [item[name] for item in items if item[name] is not None]
        out[name] = {"n": len(values), "median": statistics.median(values) if values else None}
    purchases = [value for item in items for value in item["first_buys"]]
    bodies = sum(item["bodies"] for item in items)
    delays = [value for item in items for value in item["own_clear_capture_delays"]]
    out["first_buy"] = {"n": len(purchases), "median": statistics.median(purchases) if purchases else None,
                         "fraction_bodies_bought_by_100": sum(item["bought_by_100"] for item in items) / bodies if bodies else None}
    out["own_clear_capture_delay"] = {"n": len(delays), "median": statistics.median(delays) if delays else None}
    out["draft_bodies"] = dict(collections.Counter(name for item in items for name in item["draft"]))
    return out


def comparisons(groups: list[dict], horizons) -> list[dict]:
    out = []
    for a, b in itertools.combinations(groups, 2):
        if a["engine"] != b["engine"] or a["engine"] == "unknown":
            continue
        for horizon in horizons:
            def keyed(group):
                return {(game["opponent"], game["map_sha256"], game["map"], game["side"], game["seed"]): game
                        for game in group["games"] if str(horizon) in game["measures"]}
            ka, kb = keyed(a), keyed(b)
            common = set(ka) & set(kb)
            for opponent in sorted({key[0] for key in common}):
                keys = sorted((key for key in common if key[0] == opponent), key=str)
                ma = [ka[key]["measures"][str(horizon)]["subject"] for key in keys]
                mb = [kb[key]["measures"][str(horizon)]["subject"] for key in keys]
                am, bm = aggregate(ma), aggregate(mb)
                out.append({"a": a["subject"], "b": b["subject"], "engine": a["engine"],
                            "opponent": opponent, "horizon": horizon, "n": len(keys),
                            "a_only_observed_keys": len(set(ka) - set(kb)),
                            "b_only_observed_keys": len(set(kb) - set(ka)),
                            "a_measures": am, "b_measures": bm,
                            "mean_deltas_b_minus_a": {name: bm["means"][name] - am["means"][name]
                                for name in am["means"] if am["means"][name] is not None and bm["means"][name] is not None},
                            "keys": keys})
    return out


def analyze(paths: list[Path], variants: set[str] | None, opponents: set[str] | None,
            horizons=(200, 400)) -> dict:
    groups, inputs = cohort.collect(paths)
    output, coverage, stat_pins = [], collections.Counter(), []
    for group in groups:
        entrant, engine, variant, phase = group["identity"]
        if variants and variant not in variants:
            continue
        buckets = collections.defaultdict(list)
        details = []
        for key, item in group["cells"].items():
            opponent, map_name, side, seed = key
            labels = {opponent, opponent.split("@")[0], (item["row"].get("tags") or {}).get("opponent")}
            if opponents and not opponents.intersection(labels):
                continue
            row = item["row"]
            coverage["selected_rows"] += 1
            if not cohort.terminal(row):
                coverage["unfinished"] += 1
                continue
            replay = row.get("replay")
            path = fu.stats_file(ROOT / replay) if replay else None
            if path is None:
                coverage["missing_native_statistics"] += 1
                continue
            stats = fu.read_stats(path)
            if stats["match"].get("engineVersion") != engine:
                raise ValueError(f"native engine mismatch: {path}")
            records = od.side_records(path.stem, stats)
            joined = cycles(stats)
            pin = {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            stat_pins.append(pin)
            paired = {"map": map_name, "side": side, "seed": seed, "opponent": opponent,
                      "stats": str(path), "map_sha256": stats["match"]["mapHash"],
                      "engine_build_hash": stats["match"].get("engineBuildHash"),
                      "width": stats["match"]["width"], "height": stats["match"]["height"],
                      "rounds": stats["match"]["roundsCompleted"], "measures": {}}
            for horizon in horizons:
                if stats["match"]["roundsCompleted"] < horizon:
                    coverage[f"shorter_than_{horizon}"] += 1
                    continue
                both = {}
                for rec in records:
                    perspective = "subject" if rec["side"].upper() == side else "opponent"
                    measure = side_measure(rec, stats, joined, horizon)
                    both[perspective] = measure
                    for size in ("all", "small" if max(paired["width"], paired["height"]) <= 50 else "large"):
                        buckets[(opponent, horizon, perspective, size)].append(measure)
                paired["measures"][str(horizon)] = both
            details.append(paired)
            coverage["native_statistics_measured"] += 1
        output.append({"subject": entrant, "engine": engine, "variant": variant, "phase": phase,
                       "conflicting_cells_excluded": len(group["conflicts"]),
                       "summaries": [{"opponent": key[0], "horizon": key[1], "perspective": key[2],
                                      "size": key[3], **aggregate(items)} for key, items in sorted(buckets.items())],
                       "games": details})
    return {"schema": 1, "inputs": inputs, "coverage": dict(coverage), "native_statistics": stat_pins,
            "measurement_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "cohorts": output,
            "matched_comparisons": comparisons(output, horizons),
            "definitions": {"size": "small=max(width,height)<=50, large otherwise",
                            "dead_rounds_proxy": "sum min(60,horizon-death_round), a current-bundle respawn proxy",
                            "capture": "campGroup gemCollection deduplicated by group and round, including zero-gem captures",
                            "conversion": "earliest native-ordered capture before the next clear, not a count ratio",
                            "first_champion_damage_dealt": "first positive championDamageDone timeline value; not first proximity or damage received",
                            "first_buy": "first item purchase at or before the horizon, conditional on buying by that horizon"},
            "limits": ["No travel/idle/private-policy inference from native statistics alone.",
                       "Income and survival differences in a head-to-head are descriptive, not isolated causal effects.",
                       "A first purchase after the horizon is not a purchase by the horizon.",
                       "Short games are excluded at each horizon; survivor coverage is explicit.",
                       "Source cohorts and native engine/map identities are retained; no health or strength promotion."]}


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == 'native':
        return native_main(sys.argv[2:])
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", nargs="+", type=Path)
    parser.add_argument("--variant", action="append")
    parser.add_argument("--opponent", action="append")
    parser.add_argument("--json", required=True, type=Path)
    args = parser.parse_args()
    result = analyze(args.runs, set(args.variant or []), set(args.opponent or []))
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(result["coverage"]))
    for group in result["cohorts"]:
        for summary in group["summaries"]:
            if summary["horizon"] == 400 and summary["perspective"] == "subject" and summary["size"] == "all":
                mean = summary["means"]
                print(f"{group['subject']} vs {summary['opponent']} n={summary['n']}: "
                      f"gold={mean['gold']:.1f} collected={mean['collected']:.1f} banked={mean['banked']:.1f} "
                      f"clears={mean['clears']:.1f} captures={mean['captures']:.1f} deaths={mean['deaths']:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
