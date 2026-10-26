"""Reports over game rows and single games, map regions, and cache pruning.

  arena report RUN [RUN ...] [--by opponent|map|side|size|tag:NAME] [--subject SIDE_OR_KEY]
      RUN is a run directory (its games.jsonl files) or a .jsonl log of game rows. Prints W-L-D,
      score with a 95% Wilson interval, mean gem margin and health per group, from the subject's
      side (tags.subject, or --subject A|B|key|bot id|name) or else once per entrant, then the
      games with health incidents (faults, fuel exhaustion, logged errors, stalls, suspended turns).
  arena report game PATH [--team A|B] [--positions] [--events KIND,...] [--rounds A:B]
      PATH is a statistics JSON (cambc ... --statistics PATH --json) or a .repl26a replay; the
      other file is taken from the same directory when that is unambiguous (else --replay/--stats).
      Team totals and health, per-champion contributions and deaths with context. --positions
      decodes the replay (tools/inspect-strategy-replay.mjs) for team cohesion and the enemies
      and allies near each death.
  arena map NAME [x0 y0 x1 y1] [--mark x,y ...]
      ASCII view of a map region from maps/geometry.json (north, +y, up) and its camp groups.
  arena prune [--days 7] [--snap-days 14] [--dry-run]
      Remove replays older than --days under .cache/arena/{runs,sweeps,ladder} (game logs,
      reports and summaries stay) and build snapshots unused for --snap-days.
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import stat as statmod
import struct
import subprocess
import sys
import time
import uuid
import zipfile
from collections import Counter, defaultdict
from pathlib import Path, PureWindowsPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402
from core import CACHE, MAPS, NODE, ROOT, ArenaError, FileLock, Slots, map_set, read_jsonl  # noqa: E402
from stats import wilson  # noqa: E402

INSPECT = ROOT / "tools" / "inspect-strategy-replay.mjs"
REPLAY_WRAPPERS = ROOT / ".cache" / "replay-wrappers"
SIDES = ("A", "B")
TEAM = {"A": "teamA", "B": "teamB"}
PLAYED = ("win", "loss", "draw")
ERROR_LINE = re.compile(r"\berror\b|engine_error\b", re.I)  # same rule as games.ERROR_LINE
# sdk/cpp/cambc.h champion_type order (physical champions; Cassi and Issac are separate)
CHAMPION_TYPES = ["Ryth", "Vesper Vale", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow",
                  "Ren", "Varg", "Tavi", "Heart of Ruin"]
CAMP_CHAR = {"wolf": "w", "elk": "e", "gem_cluster": "g", "gold_cluster": "$", "monolith": "M", "golem": "G"}


def _rel(p) -> str:
    p = Path(p)
    try:
        return p.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(p)


def _path(p: str | Path) -> Path:
    """A path as given, else relative to the repository root."""
    p = Path(p)
    return ROOT / p if not p.exists() and not p.is_absolute() and (ROOT / p).exists() else p


def _int(x) -> int:
    try:
        return int(float(x))
    except (TypeError, ValueError):
        return 0


# ----------------------------------------------------------------------------- game rows
def load_rows(targets) -> tuple[list[dict], list[Path]]:
    """Rows of every games.jsonl in run directories, or of the given .jsonl files."""
    files: list[Path] = []
    for t in targets:
        p = _path(t)
        if p.is_file():
            files.append(p)
        elif p.is_dir():
            found = [p / "games.jsonl"] if (p / "games.jsonl").is_file() else sorted(p.rglob("games.jsonl"))
            if not found:
                raise ArenaError(f"no games.jsonl under {t}")
            files += found
        else:
            raise ArenaError(f"no such run directory or log: {t}")
    rows = []
    for f in files:
        rows += [r for r in read_jsonl(f) if isinstance(r, dict) and "winner" in r]
    return rows, files


def _stall(row: dict, side: str) -> bool:
    return (row.get("winner") in ("A", "B", "draw") and not row.get("truncated")
            and _int((row.get(side) or {}).get("clears")) == 0)


def _perspective(row: dict, side: str | None = None) -> dict | None:
    """Fallback mirror of games.perspective (used only when games.py cannot be imported)."""
    side = str(side or (row.get("tags") or {}).get("subject") or "").upper()
    if side not in SIDES:
        return None
    other = "B" if side == "A" else "A"
    me, op = row.get(side) or {}, row.get(other) or {}
    w = row.get("winner")
    result = "win" if w == side else "loss" if w == other else (w if w in ("draw", "error", "skipped") else "error")
    return {"result": result, "side": side, "map": row.get("map"), "me": row.get(side.lower()),
            "opponent": row.get(other.lower()),
            "opponent_name": row.get(f"{other.lower()}_name") or row.get(other.lower()),
            "gems": _int(me.get("gems")), "opp_gems": _int(op.get("gems")),
            "gold": _int(me.get("gold")), "opp_gold": _int(op.get("gold")),
            "kills": _int(me.get("kills")), "deaths": _int(me.get("deaths")),
            "clears": _int(me.get("clears")), "opp_clears": _int(op.get("clears")),
            "faults": _int(me.get("faults")), "exhausted": _int(me.get("exhausted")),
            "errors": _int(me.get("errors")), "fuel_max": _int(me.get("fuel_max")),
            "fuel_at_budget": _int(me.get("fuel_at_budget")), "stall": _stall(row, side),
            "truncated": bool(row.get("truncated")), "rounds": _int(row.get("rounds")),
            "tags": row.get("tags") or {}}


def _health(row: dict, side: str) -> list[str]:
    """Fallback mirror of games.health."""
    t = row.get(side) or {}
    out = [f"{k} {_int(t.get(k))}" for k in ("faults", "exhausted", "errors") if _int(t.get(k))]
    if not _int(t.get("exhausted")) and _int(t.get("fuel_at_budget")):
        out.append(f"suspended {_int(t.get('fuel_at_budget'))}")
    if _stall(row, side):
        out.append("stall")
    return out


def side_view(row: dict, side: str) -> dict:
    """games.perspective(row, side) plus display names, margin, replay, error and health flags."""
    v = dict(perspective(row, side))
    s = side.lower()
    v.update(key=v.get("me") or "?", name=row.get(f"{s}_name") or v.get("me") or "?",
             opp_key=v.get("opponent") or "?", opp_name=v.get("opponent_name") or v.get("opponent") or "?",
             map=v.get("map") or "?", replay=row.get("replay"), error=row.get("error"), ts=row.get("ts"),
             margin=v["gems"] - v["opp_gems"])
    v["flags"] = row_health(row, side) if v["result"] in PLAYED else [v["result"]]
    return v


def _matches(row: dict, side: str, pattern: str) -> bool:
    key = str(row.get(side.lower()) or "")
    cands = {key, key.split("@")[0], str(row.get(f"{side.lower()}_name") or "")}
    return any(c and (c == pattern or fnmatch.fnmatchcase(c, pattern)) for c in cands)


def subject_sides(row: dict, subject: str | None) -> list[str]:
    """Sides to report for a row: --subject A|B or an entrant key/bot id/name (globs allowed);
    by default tags.subject, else both entrants."""
    if subject and subject.upper() in SIDES:
        return [subject.upper()]
    if subject:
        return [s for s in SIDES if _matches(row, s, subject)]
    tag = (row.get("tags") or {}).get("subject")
    return [tag] if tag in SIDES else list(SIDES)


def views_of(rows: list[dict], subject: str | None) -> list[dict]:
    return [side_view(r, s) for r in rows for s in subject_sides(r, subject)]


def _labeller(views: list[dict]):
    """Display names; a name shared by several versions gets its version appended."""
    keys = defaultdict(set)
    for v in views:
        keys[v["name"]].add(v["key"])
        keys[v["opp_name"]].add(v["opp_key"])

    def label(name: str, key: str) -> str:
        return name if len(keys[name]) <= 1 else f"{name}@{key.rsplit('@', 1)[-1][:8]}"
    return label


_SIZES: dict[str, str] | None = None


def size_of(map_name: str) -> str:
    """small/large as in `--maps small|large` (pool split at the median area); other otherwise."""
    global _SIZES
    if _SIZES is None:
        _SIZES = {}
        try:
            _SIZES.update({m: "small" for m in map_set("small")})
            _SIZES.update({m: "large" for m in map_set("large")})
        except Exception:
            pass
    return _SIZES.get(map_name) or _SIZES.get(str(map_name).replace("_", "-")) or "other"


SIZE_ORDER = {"small": 0, "large": 1, "other": 2}


def group_key(v: dict, by: str, label) -> str:
    if by == "opponent":
        return label(v["opp_name"], v["opp_key"])
    if by == "map":
        return v["map"]
    if by == "side":
        return v["side"]
    if by == "size":
        return size_of(v["map"])
    if by.startswith("tag:") and len(by) > 4:
        return str(v["tags"].get(by[4:], "-"))
    raise ArenaError(f"unknown grouping {by!r} (opponent, map, side, size, tag:NAME)")


def aggregate(views: list[dict]) -> dict:
    """W-L-D and score (draws half) with a Wilson interval over played games; mean gem margin over
    finished ones (games cut by --rounds are draws without statistics); health sums."""
    played = [v for v in views if v["result"] in PLAYED]
    full = [v for v in played if not v.get("truncated")]
    n = len(played)
    w = sum(v["result"] == "win" for v in played)
    l = sum(v["result"] == "loss" for v in played)
    d = n - w - l
    lo, hi = wilson(w + d / 2, n)
    return {"games": len(views), "played": n, "wins": w, "losses": l, "draws": d, "failed": len(views) - n,
            "truncated": n - len(full), "score": (w + d / 2) / n if n else 0.0, "ci": [lo, hi],
            "margin": sum(v["margin"] for v in full) / len(full) if full else 0.0,
            "gems": sum(v["gems"] for v in full) / len(full) if full else 0.0,
            "faults": sum(v["faults"] for v in played), "exhausted": sum(v["exhausted"] for v in played),
            "errors": sum(v["errors"] for v in played), "stalls": sum(bool(v["stall"]) for v in played),
            "fuel_max": max((v["fuel_max"] for v in played), default=0),
            "fuel_at_budget": sum(v["fuel_at_budget"] for v in played)}


def incident(v: dict) -> list[str]:
    """Health flags of one perspective (games.health; an error/skipped game is its own flag)."""
    return v["flags"]


def _head(first: str, width: int) -> str:
    return (f"{first[:width]:{width}} {'games':>5} {'W-L-D':>11} {'score':>6} {'95% CI':>8} {'margin':>7}"
            f" {'flt':>4} {'exh':>4} {'err':>4} {'stl':>4} {'fuelmax':>7} {'atb':>4}")


def _line(name: str, a: dict, width: int) -> str:
    wld = f"{a['wins']}-{a['losses']}-{a['draws']}"
    ci = f"{100 * a['ci'][0]:.0f}-{100 * a['ci'][1]:.0f}%" if a["played"] else "-"
    notes = [f"{a[k]} {k}" for k in ("failed", "truncated") if a.get(k)]
    fail = f"  ({', '.join(notes)})" if notes else ""
    margin = f"{a['margin']:+7.1f}" if a["played"] > a.get("truncated", 0) else f"{'-':>7}"
    return (f"{name[:width]:{width}} {a['games']:5} {wld:>11} {100 * a['score']:5.1f}% {ci:>8} {margin}"
            f" {a['faults']:4} {a['exhausted']:4} {a['errors']:4} {a['stalls']:4} {a['fuel_max']:7}"
            f" {a['fuel_at_budget']:4}{fail}")


def run_report(rows: list[dict], by: str | None = None, subject: str | None = None, limit: int = 40,
               incidents: int = 8) -> tuple[list[str], dict]:
    """Text lines and a JSON-able summary for a list of game rows."""
    views = views_of(rows, subject)
    label = _labeller(views)
    tagged = sum(1 for r in rows if (r.get("tags") or {}).get("subject") in SIDES)
    how = (f"--subject {subject}" if subject else
           "subject from tags" if rows and tagged == len(rows) else
           f"subject from tags in {tagged}/{len(rows)} games, both entrants otherwise" if tagged else "per entrant")
    out = [f"{len(rows)} games, {len(views)} perspectives ({how})"]
    summary: dict = {"games": len(rows), "perspectives": len(views), "subjects": [], "incidents": []}
    if not views:
        out.append("no games match" + (f" --subject {subject}" if subject else ""))
        return out, summary
    by_subject: dict[str, list[dict]] = defaultdict(list)
    for v in views:
        by_subject[label(v["name"], v["key"])].append(v)
    order = sorted(by_subject, key=lambda s: (-len(by_subject[s]), s))
    if not by:
        out.append(_head("entrant", 24))
        for s in order[:limit]:
            agg = aggregate(by_subject[s])
            summary["subjects"].append({"subject": s, "total": agg})
            out.append(_line(s, agg, 24))
    else:
        for s in order[:limit]:
            vs = by_subject[s]
            groups: dict[str, list[dict]] = defaultdict(list)
            for v in vs:
                groups[group_key(v, by, label)].append(v)
            names = sorted(groups, key=lambda g: (SIZE_ORDER.get(g, 3) if by == "size" else 0, g))
            total = aggregate(vs)
            entry = {"subject": s, "total": total, "groups": []}
            out += ["", f"{s}: {total['games']} games by {by}", "  " + _head(by, 22)]
            for g in names[:limit]:
                agg = aggregate(groups[g])
                entry["groups"].append({"group": g, **agg})
                out.append("  " + _line(g, agg, 22))
            if len(names) > limit:
                out.append(f"  ... {len(names) - limit} more groups (--limit N)")
            out.append("  " + _line("total", total, 22))
            summary["subjects"].append(entry)
    if len(order) > limit:
        out.append(f"... {len(order) - limit} more entrants (--limit N)")
    flagged = [v for v in views if incident(v)]
    for v in flagged:
        summary["incidents"].append({"subject": label(v["name"], v["key"]), "map": v["map"], "side": v["side"],
                                     "opponent": label(v["opp_name"], v["opp_key"]), "result": v["result"],
                                     "flags": incident(v), "replay": v["replay"], "error": v["error"]})
    if flagged:
        more = f" (first {incidents}; --incidents N)" if len(flagged) > incidents else ""
        out += ["", f"health incidents in {len(flagged)} of {len(views)} games{more}:"]
        for v in flagged[:incidents]:
            what = ", ".join(incident(v))
            if v["result"] not in PLAYED:
                detail = str(v["error"] or "").strip().splitlines()
                what += f": {detail[0][:80]}" if detail else ""
            gems = f" {v['gems']}-{v['opp_gems']}" if v["result"] in PLAYED else ""
            out.append(f"  {label(v['name'], v['key'])[:22]} {v['side']} {v['map']} vs "
                       f"{label(v['opp_name'], v['opp_key'])[:22]} {v['result']}{gems}: {what}")
            if v["replay"]:
                out.append(f"      {v['replay']}")
    return out, summary


# ----------------------------------------------------------------------------- one game
def is_stats(data) -> bool:
    return isinstance(data, dict) and all(k in data for k in ("champions", "match", "events", "roster"))


def _load_json(p: Path):
    try:
        return json.loads(Path(p).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def find_stats(replay: Path) -> Path | None:
    """Statistics of a replay: <stem>.json / .stats.json / .statistics.json beside it, or
    stats.json / statistics.json when the directory holds only this replay."""
    d, stem = replay.parent, replay.stem
    cands = [d / f"{stem}.json", d / f"{stem}.stats.json", d / f"{stem}.statistics.json"]
    if len(list(d.glob("*.repl26a"))) == 1:
        cands += [d / "stats.json", d / "statistics.json"]
    return next((c for c in cands if c.is_file() and is_stats(_load_json(c))), None)


def find_replay(stats_path: Path) -> Path | None:
    """Replay of a statistics file: the same stem, or the only replay in its directory."""
    d, name = stats_path.parent, stats_path.name
    for suffix in (".stats.json", ".statistics.json", ".json"):
        if name.endswith(suffix) and (d / (name[: -len(suffix)] + ".repl26a")).is_file():
            return d / (name[: -len(suffix)] + ".repl26a")
    alone = list(d.glob("*.repl26a"))
    return alone[0] if len(alone) == 1 else None


def row_for_replay(replay: Path) -> dict | None:
    """The game row that references this replay (run logs beside it), for entrant names."""
    rel, absolute = _rel(replay), replay.resolve().as_posix()
    for log in (replay.parent / "games.jsonl", replay.parent.parent / "games.jsonl"):
        for r in read_jsonl(log) if log.is_file() else []:
            if r.get("replay") and Path(str(r["replay"])).as_posix() in (rel, absolute):
                return r
    return None


STAT_KEYS = ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted")


def _fuel(raw: bytes, picks: int, bodies: int) -> tuple[int, int]:
    """Fallback mirror of games.fuel_profile: (fuel_max, fuel_at_budget) over ordinary turns."""
    if len(raw) < 13 or raw[:8] != b"a62leuf\x00":
        raise ValueError("not a fuel stream")
    values = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
    budgets, turns = values[:8], values[8:]
    budget = budgets[3] if len(budgets) > 3 and budgets[3] else 100000
    ordinary = turns[picks + bodies:]
    return max(ordinary, default=0), sum(1 for v in ordinary if v >= budget)


def _count_errors(stdout: bytes) -> int:
    """Fallback mirror of games.count_errors."""
    text = stdout.replace(b"\x00", b"").decode("utf-8", "replace")
    return sum(1 for line in text.splitlines() if ERROR_LINE.search(line))


def _parse_statistics(stats: dict) -> dict:
    """Fallback mirror of the team blocks and roster shape of games.parse_statistics."""
    teams = {}
    diag = (stats.get("diagnostics") or {}).get("botFaults") or {}
    roster = {}
    for side in SIDES:
        team = TEAM[side]
        champs = [c for c in stats["champions"] if c.get("team") == team]
        groups = [g for g in stats["events"] if (g.get("owner") or {}).get("team") == team]
        teams[side] = {
            "gems": _int(next((t["submittedGems"] for t in stats["teams"] if t["team"] == team), 0)),
            "gold": sum(_int(c["metrics"].get("earnedGold")) for c in champs),
            "kills": sum(_int(c["metrics"].get("kills")) for c in champs),
            "deaths": sum(_int(c["metrics"].get("deaths")) for c in champs),
            "clears": sum(len(g["entries"]) for g in groups if g["kind"] == "campClear"),
            "faults": _int(diag.get(team)),
            "exhausted": sum(len(g["entries"]) for g in groups if g["kind"] == "fuelExhausted"),
        }
        mine = [c for c in stats["roster"]["champions"] if c["team"] == team]
        roster[side] = (len({c["draftPick"] for c in mine}), len(mine))  # Cassi & Issac: one pick, two bodies
    return {**teams, "roster": roster}


try:  # games.py owns the row schema: its perspective, health and parsers define every number
    from games import count_errors, fuel_profile, parse_statistics, perspective  # noqa: E402
    from games import health as row_health  # noqa: E402
except Exception:  # pragma: no cover - only when games.py is missing or broken
    count_errors, fuel_profile, parse_statistics = _count_errors, _fuel, _parse_statistics
    perspective, row_health = _perspective, _health


def stats_blocks(stats: dict) -> dict[str, dict]:
    """The parts of the game row's team blocks that the statistics determine."""
    parsed = parse_statistics(stats)
    return {s: {k: parsed[s][k] for k in STAT_KEYS} for s in SIDES}


def roster_counts(stats: dict) -> dict[str, tuple[int, int]]:
    """(draft picks, champion bodies) per side; Cassi and Issac are one pick, two bodies."""
    return {s: tuple(v) for s, v in parse_statistics(stats)["roster"].items()}


def _wrap_bundle(replay: Path) -> Path:
    """Derive a CLI-layout zip without changing the completed replay's immutable files."""
    def describe():
        paths = [("replay.repl26a", replay)]
        paths += [(p.name, p) for t in "ab" for x in ("stdout", "fuel")
                  if (p := replay.parent / f"team-{t}.{x}").is_file()]
        members = []
        for name, path in paths:
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
                size = stream.tell()
            members.append({"name": name, "sha256": digest, "size": size})
        return paths, {"format": 1, "members": members}

    paths, recipe = describe()
    key = hashlib.sha256(json.dumps(recipe, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    REPLAY_WRAPPERS.mkdir(parents=True, exist_ok=True)
    out, receipt = REPLAY_WRAPPERS / f"{key}.zip", REPLAY_WRAPPERS / f"{key}.json"

    def valid():
        saved = _load_json(receipt)
        if not isinstance(saved, dict) or saved.get("recipe") != recipe:
            return False
        try:
            with out.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != saved.get("sha256"):
                    return False
            with zipfile.ZipFile(out) as bundle:
                if bundle.namelist() != [m["name"] for m in recipe["members"]]:
                    return False
                for member in recipe["members"]:
                    with bundle.open(member["name"]) as stream:
                        if hashlib.file_digest(stream, "sha256").hexdigest() != member["sha256"]:
                            return False
                    if bundle.getinfo(member["name"]).file_size != member["size"]:
                        return False
            return True
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError):
            return False

    with FileLock(REPLAY_WRAPPERS / f"{key}.lock", poll=.05):
        if describe()[1] != recipe:
            raise ArenaError("replay inputs changed while preparing report wrapper; retry the report")
        if valid():
            out.touch()  # keep a just-reused derived bundle out of age-based slimming
            return out
        token = uuid.uuid4().hex
        staging = out.with_name(f".{key}-{token}.tmp")
        metadata = receipt.with_name(f".{key}-{token}.json.tmp")
        try:
            with zipfile.ZipFile(staging, "w", zipfile.ZIP_STORED) as bundle:
                for name, path in paths:
                    # Fixed metadata keeps repair/recreation byte-identical for the same inputs.
                    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    with path.open("rb") as source, bundle.open(info, "w") as target:
                        while data := source.read(1024 * 1024):
                            target.write(data)
            if describe()[1] != recipe:
                raise ArenaError("replay inputs changed while writing report wrapper; retry the report")
            with staging.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            metadata.write_text(json.dumps({"recipe": recipe, "sha256": digest}, indent=1) + "\n",
                                encoding="utf-8")
            staging.replace(out)
            metadata.replace(receipt)
            if not valid():
                raise ArenaError("report wrapper does not match its replay inputs")
            return out
        finally:
            staging.unlink(missing_ok=True)
            metadata.unlink(missing_ok=True)


def replay_health(replay: Path, counts: dict[str, tuple[int, int]]) -> dict[str, dict]:
    """errors, fuel_max and fuel_at_budget per side as in the game rows, plus the error lines.
    counts: side -> (picks, bodies)."""
    out = {}
    with zipfile.ZipFile(replay) as z:
        names = set(z.namelist())
        for side in SIDES:
            t = side.lower()
            raw = z.read(f"team-{t}.stdout") if f"team-{t}.stdout" in names else b""
            text = raw.replace(b"\x00", b"").decode("utf-8", "replace")
            out[side] = {"errors": count_errors(raw),
                         "error_lines": [l.strip() for l in text.splitlines() if ERROR_LINE.search(l)]}
            if f"team-{t}.fuel" in names:
                try:
                    out[side]["fuel_max"], out[side]["fuel_at_budget"] = fuel_profile(z.read(f"team-{t}.fuel"),
                                                                                      *counts.get(side, (0, 0)))
                except (ValueError, struct.error) as exc:
                    out[side]["fuel_note"] = str(exc)
    return out


_GEOMETRY: list | None = None


def geometry_maps() -> list[dict]:
    global _GEOMETRY
    if _GEOMETRY is None:
        path = MAPS / "geometry.json"
        _GEOMETRY = json.loads(path.read_text(encoding="utf-8"))["maps"] if path.is_file() else []
    return _GEOMETRY


def map_stem(geo: dict) -> str:
    """Canonical map name (file stem) of a geometry record; its id is the pool name."""
    return PureWindowsPath(str(geo.get("mapPath") or "")).stem or geo["id"]


def geometry_map(stats: dict | None = None, name: str | None = None) -> dict | None:
    """Geometry record of a pool map by statistics mapHash, or by file stem / pool name."""
    for m in geometry_maps():
        if stats and m.get("mapSha256") == stats["match"].get("mapHash"):
            return m
        if name and name in (m["id"], map_stem(m), m["id"].replace("_", "-")):
            return m
    return None


def map_by_hash(sha: str | None) -> str | None:
    """Name of the map file (pool, legacy or labs) whose bytes hash to `sha` (statistics mapHash)."""
    if not sha:
        return None
    for f in sorted(MAPS.glob("*.map26a")) + sorted(MAPS.glob("legacy/*.map26a")) + sorted(MAPS.glob("labs/*.map26a")):
        if hashlib.sha256(f.read_bytes()).hexdigest() == sha:
            return f.stem  # canonical name, as games.py records it (legacy stems resolve too)
    return None


def decode_tracks(replay: Path, map_id: str | None = None, rounds: tuple[int, int] | None = None) -> dict:
    """All champions' owner-turn positions from one inspector pass (official visualiser decoder).
    Returns the inspector output plus by_id: {id: {team, type, pos: {round: (x, y, health)}}}."""
    if not NODE.is_file():
        raise ArenaError(f"node not found ({NODE})")
    cmd = [str(NODE), "--disable-warning=ExperimentalWarning", str(INSPECT), "--replay", str(replay),
           "--actor", "all", "--captures", "0", "--max-steps", "10000000"]
    if map_id:
        cmd += ["--map-id", map_id]
    if rounds:
        cmd += ["--from-round", str(rounds[0]), "--to-round", str(rounds[1])]
    with Slots(1, quiet=True):
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=900)
    if proc.returncode:
        err = [l.strip() for l in proc.stderr.splitlines() if l.strip() and not l.strip().startswith("at ")]
        msg = next((l for l in err if "Error" in l), err[-1] if err else "no output")
        raise ArenaError(f"replay decode failed: {msg[:300]}")
    data = json.loads(proc.stdout)
    data["by_id"] = {t["id"]: {"team": "teamA" if t["team"] == 0 else "teamB", "type": t["type"],
                               "pos": {s[0]: (s[1], s[2], s[3]) for s in t["samples"]}} for t in data["tracks"]}
    return data


def cheb(a, b) -> int:
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def _alive(track: dict, r: int | None):
    p = track["pos"].get(r) if r is not None else None
    return p if p and p[2] > 0 and p[0] >= 0 else None


def cohesion(tracks: dict, team: str) -> dict:
    """Share of alive champion-rounds with a teammate within 2/4/6 tiles (Chebyshev)."""
    mates = [t for t in tracks.values() if t["team"] == team]
    near, n = [0, 0, 0], 0
    for r in sorted({r for t in mates for r in t["pos"]}):
        alive = [p for p in (_alive(t, r) for t in mates) if p]
        for i, p in enumerate(alive):
            d = min((cheb(p, q) for j, q in enumerate(alive) if j != i), default=99)
            n += 1
            for k, lim in enumerate((2, 4, 6)):
                near[k] += d <= lim
    return {"samples": n, "within": {lim: near[k] / n if n else 0.0 for k, lim in enumerate((2, 4, 6))}}


def death_context(tracks: dict, victim: int, rnd: int, where=None) -> dict:
    """Enemies within 4 and allies within 6 of the victim at its last alive owner-turn start
    before the death round (the death tile and round - 1 if no such sample exists)."""
    me = tracks.get(victim)
    if not me:
        return {}
    r = next((q for q in range(rnd - 1, max(0, rnd - 6), -1) if _alive(me, q)), None)
    at = _alive(me, r)
    if not at:
        r, at = rnd - 1, where
    if not at:
        return {}
    foes = sum(1 for t in tracks.values() if t["team"] != me["team"] and (p := _alive(t, r)) and cheb(p, at) <= 4)
    mates = sum(1 for i, t in tracks.items()
                if i != victim and t["team"] == me["team"] and (p := _alive(t, r)) and cheb(p, at) <= 6)
    return {"context_round": r, "foes4": foes, "mates6": mates}


def derived_deaths(tracks: dict) -> list[dict]:
    """Deaths seen in the position tracks (alive -> dead), for replays without statistics."""
    out = []
    for cid, t in tracks.items():
        prev = None
        for r in sorted(t["pos"]):
            p = t["pos"][r]
            alive = p[2] > 0 and p[0] >= 0
            if prev and prev[0] and not alive:
                name = CHAMPION_TYPES[t["type"]] if 0 <= t["type"] < len(CHAMPION_TYPES) else "?"
                out.append({"round": r, "id": cid, "name": name, "team": t["team"], "x": prev[1][0], "y": prev[1][1],
                            "context": None, "killer": None, "gems_lost": 0})
            prev = (alive, p)
    return sorted(out, key=lambda d: (d["round"], d["id"]))


def stats_deaths(stats: dict, geo: dict | None) -> list[dict]:
    """Champion deaths from statistics events: round, victim, killer (champion or camp type), tile
    and the gems lost at that death."""
    camp_type = {m["nativeId"]: m["type"] for g in (geo or {}).get("groups", []) for m in g["members"]}
    lost = Counter()
    for g in stats["events"]:
        if g["kind"] == "gemLoss":
            for e in g["entries"]:
                lost[((g.get("owner") or {}).get("id"), e["timestamp"]["round"])] += _int(e.get("value"))
    out = []
    for g in stats["events"]:
        o = g.get("owner") or {}
        if g["kind"] != "death" or o.get("kind") != "champion":
            continue
        for e in g["entries"]:
            tgt = e.get("target") or {}
            if tgt.get("kind") == "champion":
                killer = f"{tgt.get('name', '?')}#{tgt.get('id')}"
            elif tgt.get("kind") == "camp":
                killer = f"{camp_type.get(tgt.get('id'), 'camp')}#{tgt.get('id')}"
            else:
                killer = str(tgt.get("kind") or e.get("context") or "?")
            r = e["timestamp"]["round"]
            out.append({"round": r, "id": o.get("id"), "name": o.get("name", "?"), "team": o.get("team"),
                        "context": e.get("context"), "killer": killer, "x": e.get("x"), "y": e.get("y"),
                        "gems_lost": lost.get((o.get("id"), r), 0)})
    return sorted(out, key=lambda d: (d["round"], d["id"]))


def _rounds_arg(spec: str | None) -> tuple[int, int] | None:
    if not spec:
        return None
    m = re.fullmatch(r"(\d+):(\d+)", spec)
    if not m or int(m[1]) > int(m[2]):
        raise ArenaError("--rounds takes FROM:TO")
    return int(m[1]), int(m[2])


def _champion_lines(stats: dict, sides) -> tuple[list[str], list[dict]]:
    """Per-champion contributions (the old champs.py columns plus damage taken, assists, gems)."""
    clears, dctx = Counter(), defaultdict(Counter)
    for g in stats["events"]:
        o = g.get("owner") or {}
        if o.get("kind") != "champion":
            continue
        if g["kind"] == "campClear":
            clears[o.get("id")] += len(g["entries"])
        elif g["kind"] == "death":
            for e in g["entries"]:
                dctx[o.get("id")]["champion" if e.get("context") == "champion" else "camp"] += 1
    lines = [f"{'champion':20} {'clears':>6} {'gold':>6} {'mdmg':>7} {'cdmg':>6} {'taken':>6} {'kills':>5}"
             f" {'asst':>4} {'d-ch':>4} {'d-cp':>4} {'gems':>5}"]
    recs = []
    for s in sides:
        for c in sorted((c for c in stats["champions"] if c.get("team") == TEAM[s]), key=lambda c: c["id"]):
            mt = c["metrics"]
            rec = {"side": s, "id": c["id"], "name": c.get("name"), "clears": clears[c["id"]],
                   "gold": _int(mt.get("earnedGold")), "mdmg": _int(mt.get("monsterDamageDone")),
                   "cdmg": _int(mt.get("championDamageDone")),
                   "taken": _int(mt.get("championDamageTaken")) + _int(mt.get("monsterDamageTaken")),
                   "kills": _int(mt.get("kills")), "assists": _int(mt.get("assists")),
                   "deaths_champion": dctx[c["id"]]["champion"], "deaths_camp": dctx[c["id"]]["camp"],
                   "gems": _int(mt.get("submittedGems"))}
            recs.append(rec)
            who = f"{s} {c.get('name')}#{c['id']}"
            lines.append(f"{who[:20]:20} {rec['clears']:6} {rec['gold']:6} {rec['mdmg']:7} {rec['cdmg']:6}"
                         f" {rec['taken']:6} {rec['kills']:5} {rec['assists']:4} {rec['deaths_champion']:4}"
                         f" {rec['deaths_camp']:4} {rec['gems']:5}")
    return lines, recs


def _event_lines(stats: dict, events: str, sides, window, limit, geo: dict | None = None) -> tuple[list[str], list[dict]]:
    groups = {g["nativeId"]: g["key"] for g in (geo or {}).get("groups", [])}
    camps = {m["nativeId"]: m["type"] for g in (geo or {}).get("groups", []) for m in g["members"]}
    kinds = {k.strip() for k in events.split(",") if k.strip()}
    known = sorted({g["kind"] for g in stats["events"]})
    unknown = kinds - set(known) - {"all"}
    if unknown:
        raise ArenaError(f"unknown event kind(s) {', '.join(sorted(unknown))}; available: {', '.join(known)}")
    teams = {TEAM[s] for s in sides}
    rows = []
    for g in stats["events"]:
        o = g.get("owner") or {}
        if ("all" not in kinds and g["kind"] not in kinds) or o.get("team") not in teams:
            continue
        for e in g["entries"]:
            r = e["timestamp"]["round"]
            if not window or window[0] <= r <= window[1]:
                rows.append((r, e["timestamp"].get("ordinal", 0), g["kind"], o, e))
    rows.sort(key=lambda x: (x[0], x[1]))
    more = f" (first {limit}; --limit N)" if len(rows) > limit else ""
    lines = [f"events {','.join(sorted(kinds))}: {len(rows)}{more}"]
    for r, _, kind, o, e in rows[:limit]:
        tgt = e.get("target")
        if isinstance(tgt, dict) and tgt.get("kind") == "campGroup" and tgt.get("id") in groups:
            tgt = groups[tgt["id"]]
        elif isinstance(tgt, dict) and tgt.get("kind") == "camp" and tgt.get("id") in camps:
            tgt = f"{camps[tgt['id']]}#{tgt['id']}"
        elif isinstance(tgt, dict):
            tgt = f"{tgt.get('name') or tgt.get('kind')}" + (f"#{tgt['id']}" if "id" in tgt else "")
        lines.append(f"  r{r:<5} {kind:15} {str(o.get('team', '?'))[-1]} {o.get('name', '?')}#{o.get('id')}"
                     f" value {e.get('value')} target {tgt} at ({e.get('x')},{e.get('y')})")
    return lines, [{"round": r, "kind": kind, "owner": o, **e} for r, _, kind, o, e in rows]


def game_report(path, team: str | None = None, positions: bool = False, events: str | None = None,
                rounds: str | None = None, limit: int = 12, stats_path=None, replay_path=None) -> tuple[list[str], dict]:
    """Text lines and a JSON-able summary of one game (statistics JSON and/or replay)."""
    p = _path(path)
    if not p.is_file():
        raise ArenaError(f"no such file: {path}")
    if p.suffix == ".repl26a":
        replay, spath = p, (_path(stats_path) if stats_path else find_stats(p))
    else:
        spath, replay = p, (_path(replay_path) if replay_path else find_replay(p))
    stats = _load_json(spath) if spath else None
    if spath and not is_stats(stats):
        raise ArenaError(f"{_rel(spath)} is not a statistics JSON (cambc run ... --statistics PATH --json)")
    if replay and not zipfile.is_zipfile(replay) and (replay.parent / "team-a.stdout").is_file():
        replay = _wrap_bundle(replay)  # raw fastrun replay from `arena exp replay`
    if replay and not zipfile.is_zipfile(replay):
        raise ArenaError(f"{_rel(replay)} is not a replay bundle")
    sides = [team.upper()] if team else list(SIDES)
    if any(s not in SIDES for s in sides):
        raise ArenaError("--team takes A or B")
    if positions and not replay:
        raise ArenaError("--positions needs the replay (none found next to the statistics; pass --replay)")
    if events and not stats:
        raise ArenaError("--events needs the statistics JSON (--stats PATH)")
    if events:
        _event_lines(stats, events, sides, None, 0)  # reject unknown kinds before any decoding
    window = _rounds_arg(rounds)
    row = row_for_replay(replay) if replay else None
    geo = geometry_map(stats=stats) if stats else geometry_map(name=(row or {}).get("map"))
    names = {s: (row or {}).get(f"{s.lower()}_name") for s in SIDES}
    summary: dict = {"statistics": _rel(spath) if spath else None, "replay": _rel(replay) if replay else None}

    decoded = tracks = None
    if replay and (positions or not stats):
        # without statistics a one-round decode still yields the roster (picks/bodies for fuel)
        decoded = decode_tracks(replay, map_id=geo["id"] if geo else None, rounds=None if positions else (1, 1))
        tracks = decoded["by_id"]
        geo = geo or geometry_map(name=decoded.get("mapId"))
    mapname = ((map_stem(geo) if geo else None) or (row or {}).get("map")
               or (map_by_hash(stats["match"].get("mapHash")) if stats else None) or "?")
    if stats:
        m = stats["match"]
        out = [f"map {mapname} {m.get('width')}x{m.get('height')}, {m.get('roundsCompleted')} rounds"
               f"{' (+partial)' if m.get('finalRoundPartial') else ''}, winner {m.get('outcome')},"
               f" engine {m.get('engineVersion')}"]
    else:
        last = max((r for t in tracks.values() for r in t["pos"]), default=0) if positions else None
        out = [f"map {mapname} {decoded['width']}x{decoded['height']}, replay only (no statistics found; --stats PATH)"
               + (f", last round {last}" if last else "")]
    out.append(f"statistics: {summary['statistics'] or '-'}   replay: {summary['replay'] or '-'}")

    # team totals and health (the game row's team blocks)
    blocks = stats_blocks(stats) if stats else {s: {} for s in SIDES}
    if stats:
        counts = roster_counts(stats)
    else:
        counts = {}
        for s in SIDES:
            mine = [t for t in tracks.values() if t["team"] == TEAM[s]]
            counts[s] = (len(mine) - (1 if {3, 4} <= {t["type"] for t in mine} else 0), len(mine))
    health = replay_health(replay, counts) if replay else {}
    out.append(f"{'team':18} {'gems':>5} {'gold':>6} {'kills':>5} {'deaths':>6} {'clears':>6} {'faults':>6}"
               f" {'exh':>4} {'errors':>6} {'fuelmax':>7} {'atb':>4}")
    summary["teams"] = {}
    for s in SIDES:
        b = dict(blocks[s])
        b.update({k: health[s][k] for k in ("errors", "fuel_max", "fuel_at_budget") if k in health.get(s, {})})
        summary["teams"][s] = b
        who = f"{s} {names[s]}" if names[s] else s

        def cell(k, w):
            return f"{b[k]:>{w}}" if k in b else f"{'-':>{w}}"
        out.append(f"{who[:18]:18} {cell('gems', 5)} {cell('gold', 6)} {cell('kills', 5)} {cell('deaths', 6)}"
                   f" {cell('clears', 6)} {cell('faults', 6)} {cell('exhausted', 4)} {cell('errors', 6)}"
                   f" {cell('fuel_max', 7)} {cell('fuel_at_budget', 4)}" + ("  STALL" if b.get("clears") == 0 else ""))
    for s in sides:
        lines = health.get(s, {}).get("error_lines") or []
        if lines:
            kinds = Counter(l[:70] for l in lines)
            out.append(f"  {s} logged errors ({len(lines)}): "
                       + "; ".join(f"{k} x{v}" if v > 1 else k for k, v in kinds.most_common(3)))

    if stats:
        lines, summary["champions"] = _champion_lines(stats, sides)
        out += [""] + lines

    if tracks and positions:
        out.append("")
        summary["cohesion"] = {}
        for s in sides:
            c = cohesion(tracks, TEAM[s])
            summary["cohesion"][s] = c
            w = c["within"]
            out.append(f"{s} cohesion: a teammate within 2/4/6 tiles in {100 * w[2]:.0f}/{100 * w[4]:.0f}/"
                       f"{100 * w[6]:.0f}% of {c['samples']} alive champion-rounds")

    deaths = stats_deaths(stats, geo) if stats else derived_deaths(tracks) if tracks and positions else None
    summary["deaths"] = []
    if deaths is None:
        out += ["", "deaths need the statistics JSON (or --positions with the replay)"]
    for s in sides if deaths is not None else ():
        mine = [d for d in deaths if d["team"] == TEAM[s] and (not window or window[0] <= d["round"] <= window[1])]
        if tracks and positions:
            for d in mine:
                d.update(death_context(tracks, d["id"], d["round"], (d["x"], d["y"]) if d.get("x") is not None else None))
        summary["deaths"] += mine
        line = (f"{s} deaths {len(mine)}" + (f" in rounds {window[0]}-{window[1]}" if window else "")
                + ("" if stats else " (first round seen dead in the positions; no statistics)"))
        if mine and stats:
            by_ctx = Counter(d["context"] or "?" for d in mine)
            by_killer = Counter(str(d["killer"]).split("#")[0] for d in mine)
            line += (": " + ", ".join(f"{k} {v}" for k, v in by_ctx.most_common())
                     + " | by " + ", ".join(f"{k} {v}" for k, v in by_killer.most_common(5)))
        ctx = [d for d in mine if "foes4" in d]
        if ctx:
            lone = sum(1 for d in ctx if d["mates6"] == 0)
            line += (f" | mean foes<=4 {sum(d['foes4'] for d in ctx) / len(ctx):.1f},"
                     f" mates<=6 {sum(d['mates6'] for d in ctx) / len(ctx):.1f}, isolated {lone}/{len(ctx)}")
        out += ["", line]
        for d in mine[:limit]:
            at = f" at ({d['x']},{d['y']})" if d.get("x") is not None else ""
            by = f" by {d['killer']}" if d.get("killer") else ""
            lost = f", lost {d['gems_lost']} gems" if d.get("gems_lost") else ""
            near = f", foes<=4 {d['foes4']} mates<=6 {d['mates6']}" if "foes4" in d else ""
            out.append(f"  r{d['round']:<5} {d['name']}#{d['id']}{at}{by}{lost}{near}")
        if len(mine) > limit:
            out.append(f"  ... {len(mine) - limit} more (--limit N)")

    if events:
        lines, summary["events"] = _event_lines(stats, events, sides, window, limit, geo)
        out += [""] + lines
    return out, summary


# ----------------------------------------------------------------------------- maps
def map_view(name: str, region=None, marks=()) -> list[str]:
    """ASCII map region from the geometry export (north, +y, printed up)."""
    geo = geometry_map(name=name)
    if not geo:
        raise ArenaError(f"map {name!r} is not in maps/geometry.json (pool maps only; see maps/)")
    w, h = geo["width"], geo["height"]
    x0, y0, x1, y1 = region if region else (0, 0, w - 1, h - 1)
    x0, x1 = sorted((max(0, min(w - 1, x0)), max(0, min(w - 1, x1))))
    y0, y1 = sorted((max(0, min(h - 1, y0)), max(0, min(h - 1, y1))))
    grid = {}
    for kind, ch in (("wall", "#"), ("hole", "o"), ("bush", '"'), ("altar", "A")):
        for t in geo["terrain"].get(kind, []):
            grid[(t % w, t // w)] = ch
    for team, ch in (("teamA", "a"), ("teamB", "b")):
        s = geo["spawns"][team]
        for y in range(s["minY"], s["maxY"]):
            for x in range(s["minX"], s["maxX"]):
                grid[(x, y)] = ch
    for g in geo["groups"]:
        for mem in g["members"]:
            grid[(mem["x"], mem["y"])] = CAMP_CHAR.get(mem["type"], "?")
    for xy in marks:
        grid[tuple(xy)] = "*"
    out = [f"{map_stem(geo)} {w}x{h} {geo.get('symmetry', '')}, draft {geo.get('draftCount', '?')}:"
           f" x{x0}-{x1} y{y0}-{y1} (north, +y, up)"]
    for y in range(y1, y0 - 1, -1):
        out.append(f"{y:3} " + "".join(grid.get((x, y), ".") for x in range(x0, x1 + 1)))
    out.append("    " + "".join(str(x % 10) for x in range(x0, x1 + 1)))
    out.append('legend: # wall, o hole, " bush, A altar, a/b spawn, w wolf, e elk, g gem, $ gold,'
               ' M monolith, G golem, * mark')
    inside = [g for g in geo["groups"] if any(x0 <= m["x"] <= x1 and y0 <= m["y"] <= y1 for m in g["members"])]
    if inside:
        out.append(f"camp groups ({len(inside)}): key, members, gems/gold, cooldown, extent")
        for g in inside[:24]:
            kinds = Counter(m["type"].replace("_cluster", "") for m in g["members"])
            xs, ys = [m["x"] for m in g["members"]], [m["y"] for m in g["members"]]
            members = " ".join(f"{k}{v}" for k, v in sorted(kinds.items()))
            out.append(f"  {g['key']:7} {members:20} {g['gemValue']:>3}/{g['goldValue']:<4} cd {g['cooldown']:<3}"
                       f" x{min(xs)}-{max(xs)} y{min(ys)}-{max(ys)}")
        if len(inside) > 24:
            out.append(f"  ... {len(inside) - 24} more (narrow the region)")
    return out


# ----------------------------------------------------------------------------- prune
PRUNE_AREAS = ("runs", "sweeps", "ladder")


def regression_protected_paths(cache: Path | None = None) -> set[Path]:
    """Union canonical census pins with the explicit latest-only strength projection."""
    return _canonical_regression_protected_paths(cache) | strength_protected_paths(cache)


def _canonical_regression_protected_paths(cache: Path | None = None) -> set[Path]:
    """Preserve only the authoritative current experiment, never every exploratory variant."""
    manifest = ROOT / "results/regression/current.json"
    if not manifest.exists():
        return set()
    try:
        record = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(record, dict) or record.get("schema") != "experiment-coverage-audit/v1":
            raise ValueError("unknown current regression schema")
        experiment = record.get("experiment")
        if not isinstance(experiment, str) or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*", experiment):
            raise ValueError("invalid current regression experiment name")
        entrants = record.get("entrants")
        if not isinstance(entrants, dict) or not entrants:
            raise ValueError("current regression needs recorded entrants")
        snapshots = set()
        for label, key in entrants.items():
            if not isinstance(label, str) or not label or not isinstance(key, str) or not re.fullmatch(
                    r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*@[0-9a-f]{12}", key):
                raise ValueError("invalid current regression entrant identity")
            snapshots.add(key.rsplit("@", 1)[1])
        owner, name = experiment.split("/")
        evidence = record.get("evidence")
        if not isinstance(evidence, dict):
            raise ValueError("current regression needs experiment evidence descriptors")
        protected = {manifest.resolve()}
        spec = None
        for role, filename in (("spec", "spec.json"), ("rows", "rows.jsonl")):
            expected = Path(".cache") / owner / "exp" / name / filename
            descriptor = evidence.get(role)
            if (not isinstance(descriptor, dict) or descriptor.get("path") != expected.as_posix() or
                    not isinstance(descriptor.get("sha256"), str) or
                    not re.fullmatch(r"[0-9a-f]{64}", descriptor["sha256"])):
                raise ValueError(f"invalid current regression {role} descriptor")
            path = (ROOT / expected).resolve()
            if not path.is_relative_to(ROOT.resolve()):
                raise ValueError("current regression evidence escapes the repository")
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
                raise ValueError(f"current regression {role} changed; refresh the audit after closing the experiment")
            if role == "spec":
                spec = json.loads(data)
            protected.add(path)
        if not isinstance(spec, dict) or spec.get("name") != experiment or spec.get("entrants") != entrants:
            raise ValueError("current regression recorded entrants disagree with its experiment")
        if spec.get("private_snapshots") or "snapshot_roots" in (spec.get("provenance") or {}):
            raise ValueError("private snapshot diagnostics are not retention-qualified")
        protected.update(((Path(cache or CACHE) / "snap" / version).resolve()) for version in snapshots)
        return protected
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise ArenaError(f"cannot safely prune with {manifest}: {exc}") from exc


def strength_protected_paths(cache: Path | None = None) -> set[Path]:
    """Validate a finite selected-input projection; never traverse history or prose."""
    manifest = ROOT / "results/regression/strength-current.json"
    if not manifest.exists():
        return set()
    key_pattern = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*@[0-9a-f]{12}")
    name_pattern = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*")
    digest_pattern = re.compile(r"[0-9a-f]{64}")
    root = ROOT.resolve()
    snapshots = Path(cache or CACHE).resolve() / "snap"

    def key(value):
        if type(value) is not str or not key_pattern.fullmatch(value):
            raise ValueError("invalid selected snapshot identity")
        return value

    def digest(value):
        if type(value) is not str or not digest_pattern.fullmatch(value):
            raise ValueError("invalid full SHA256 identity")
        return value

    def exact_path(relative):
        expected = root / relative
        actual = expected.resolve()
        if actual != expected or not actual.is_relative_to(root):
            raise ValueError("selected input path escapes or redirects within the repository")
        return actual

    def evidence(descriptor, expected):
        if not isinstance(descriptor, dict) or descriptor.get("path") != expected.as_posix():
            raise ValueError("invalid selected experiment evidence path")
        expected_hash = digest(descriptor.get("sha256"))
        path = exact_path(expected)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected_hash:
            raise ValueError("selected experiment evidence changed; refresh the strength retention projection")
        return path, data

    def snapshot(identity, expected_hash, overrides, toolchain):
        ident, version = key(identity).rsplit("@", 1)
        expected_hash = digest(expected_hash)
        directory = snapshots / version
        if not directory.is_relative_to(root) or directory.resolve() != directory:
            raise ValueError("selected snapshot escapes or redirects within the repository")
        meta_path, ready, stamp = directory / "meta.json", directory / ".ready", directory / ".toolchain"
        binary = directory / "bots" / ident / ".cambc/build-release/bot.wasm"
        for path in (meta_path, ready, stamp, binary):
            if path.resolve() != path or not path.is_file():
                raise ValueError(f"selected snapshot replay input missing or redirected: {identity}")
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        if (not isinstance(meta, dict) or meta.get("bot") != ident or meta.get("version") != version or
                meta.get("overrides", {}) != overrides):
            raise ValueError(f"selected snapshot metadata disagrees with its spec: {identity}")
        if hashlib.sha256(binary.read_bytes()).hexdigest() != expected_hash:
            raise ValueError(f"selected snapshot WASM changed: {identity}")
        if stamp.read_text(encoding="utf-8").strip() != toolchain:
            raise ValueError(f"selected snapshot toolchain disagrees with its spec: {identity}")
        src = directory / "bots" / ident / "src"
        if not (src / "main.cpp").is_file():
            raise ValueError(f"selected snapshot source closure missing: {identity}")
        files = {p for p in src.rglob("*") if p.is_file() and ".cambc" not in p.parts}
        stack = [p for p in files if p.suffix in core.SOURCE_SUFFIXES]
        while stack:
            source = stack.pop()
            if source.resolve() != source:
                raise ValueError(f"selected snapshot source redirects: {identity}")
            for include in core.INCLUDE.findall(source.read_text(encoding="utf-8", errors="surrogateescape")):
                candidate = (source.parent / include).resolve()
                if candidate.is_file() and candidate not in files and candidate.is_relative_to(directory):
                    files.add(candidate)
                    if candidate.suffix in core.SOURCE_SUFFIXES:
                        stack.append(candidate)
        source_hash = hashlib.sha256()
        for path in sorted(files):
            if path.resolve() != path:
                raise ValueError(f"selected snapshot source redirects: {identity}")
            source_hash.update(path.relative_to(directory).as_posix().encode() + b"\0" + path.read_bytes() + b"\0")
        for name, value in sorted(overrides.items()):
            source_hash.update(f"override {name}={value}\0".encode())
        if source_hash.hexdigest()[:12] != version:
            raise ValueError(f"selected snapshot source closure changed: {identity}")
        return directory

    try:
        manifest = exact_path(Path("results/regression/strength-current.json"))
        record = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(record, dict):
            raise ValueError("strength report must be an object")
        projection = record.get("retention")
        if projection is None:
            return set()  # Older reports remain canonical-only; no guessed lineage pins.
        if (record.get("schema") != "development-main-strength-floor/v3" or not isinstance(projection, dict) or
                projection.get("schema") != "arena-current-strength-retention/v1" or
                set(projection) not in ({"schema", "experiments"}, {"schema", "experiments", "current_default_aliases"})):
            raise ValueError("unknown latest strength retention schema")
        reference = record.get("reference")
        if not isinstance(reference, dict):
            raise ValueError("latest strength reference missing")
        alias = key(reference.get("snapshot"))
        alias_bot = alias.rsplit("@", 1)[0]
        if alias_bot != "anchors/" + str(record.get("reference_epoch")):
            raise ValueError("latest reference alias disagrees with its epoch")
        reference_hash = digest(reference.get("wasm_sha256"))
        reference_keys = {alias}
        working = reference.get("released_working_snapshot")
        if working is not None:
            working = key(working)
            if not working.startswith("main/"):
                raise ValueError("released working reference is not a Main snapshot")
            reference_keys.add(working)
        targets = record.get("current_targeted_qualification")
        if not isinstance(targets, dict) or not targets:
            raise ValueError("latest strength selected targets missing")
        target_keys, target_hashes = {}, {}
        for bot, target in targets.items():
            if (type(bot) is not str or not name_pattern.fullmatch(bot) or not isinstance(target, dict) or
                    target.get("bot") != bot or type(target.get("games")) is not int or target["games"] <= 0):
                raise ValueError("invalid latest measured target")
            identity = key(target.get("snapshot"))
            if identity.rsplit("@", 1)[0] != bot or identity in reference_keys:
                raise ValueError("latest target snapshot disagrees with its bot")
            target_keys[identity] = bot
            target_hashes[identity] = digest(target.get("wasm_sha256"))
        aliases = projection.get("current_default_aliases", [])
        if not isinstance(aliases, list) or len(aliases) > len(targets):
            raise ValueError("invalid current-default alias projection")
        default_aliases = {}
        for binding in aliases:
            if not isinstance(binding, dict) or set(binding) != {"bot", "tested_snapshot", "current_default", "wasm_sha256", "toolchain"}:
                raise ValueError("invalid current-default alias binding")
            bot = binding["bot"]
            if type(bot) is not str or bot not in targets or bot in default_aliases:
                raise ValueError("current-default alias is arbitrary or repeated")
            tested, current = key(binding["tested_snapshot"]), key(binding["current_default"])
            if (tested == current or tested.rsplit("@", 1)[0] != bot or current.rsplit("@", 1)[0] != bot or
                    current != targets[bot]["snapshot"] or tested != targets[bot].get("measured_snapshot") or
                    digest(binding["wasm_sha256"]) != target_hashes[current] or
                    type(binding["toolchain"]) is not str or not binding["toolchain"]):
                raise ValueError("current-default alias disagrees with the declared measured/current target")
            default_aliases[bot] = binding
            # Only the immutable measured recipe may stand in for this target;
            # the separately verified bare-default closure is an additional pin.
            del target_keys[current]
            target_keys[tested] = bot
            target_hashes[tested] = binding["wasm_sha256"]
        experiments = projection.get("experiments")
        if not isinstance(experiments, list) or not 0 < len(experiments) <= len(targets):
            raise ValueError("latest retention must select a bounded experiment per target")
        protected, seen_experiments, seen_targets, reference_toolchains = {manifest.resolve()}, set(), set(), set()
        for entry in experiments:
            if not isinstance(entry, dict) or set(entry) != {"name", "spec", "rows", "entrants"}:
                raise ValueError("invalid selected experiment projection")
            name = entry["name"]
            if type(name) is not str or not name_pattern.fullmatch(name) or name in seen_experiments:
                raise ValueError("invalid or duplicate selected experiment")
            seen_experiments.add(name)
            owner, short = name.split("/")
            base = Path(".cache") / owner / "exp" / short
            spec_path, data = evidence(entry["spec"], base / "spec.json")
            rows_path, _ = evidence(entry["rows"], base / "rows.jsonl")
            spec = json.loads(data)
            if not isinstance(spec, dict) or spec.get("name") != name or not isinstance(spec.get("entrants"), dict):
                raise ValueError("selected experiment spec identity disagrees")
            if spec.get("private_snapshots") or "snapshot_roots" in (spec.get("provenance") or {}):
                raise ValueError("private snapshot diagnostics are not retention-qualified")
            selected = entry["entrants"]
            if not isinstance(selected, dict) or not 2 <= len(selected) <= len(targets) + 1:
                raise ValueError("invalid selected entrant subset")
            subject_list, opponents = spec.get("subjects"), spec.get("opponent_ids")
            if (not isinstance(subject_list, list) or not all(isinstance(s, dict) and type(s.get("label")) is str and
                    type(s.get("bot")) is str and isinstance(s.get("set", {}), dict) for s in subject_list) or
                    not isinstance(opponents, list) or not all(type(o) is str for o in opponents)):
                raise ValueError("selected experiment subject/opponent roles missing")
            subjects = {s["label"]: s for s in subject_list}
            if len(subjects) != len(subject_list) or len(set(opponents)) != len(opponents) or set(subjects) & set(opponents):
                raise ValueError("selected experiment roles are ambiguous")
            provenance = spec.get("provenance") or {}
            wasm_hashes, toolchain = provenance.get("wasm"), provenance.get("toolchain")
            if not isinstance(wasm_hashes, dict) or type(toolchain) is not str or not toolchain:
                raise ValueError("selected experiment WASM provenance missing")
            reference_label, target_labels = None, []
            for label, identity in selected.items():
                identity = key(identity)
                if type(label) is not str or not label or spec["entrants"].get(label) != identity:
                    raise ValueError("selected entrant disagrees with experiment spec")
                ident = identity.rsplit("@", 1)[0]
                if label in subjects:
                    if subjects[label]["bot"] != ident:
                        raise ValueError("selected subject identity disagrees with its role")
                    overrides = subjects[label].get("set", {})
                elif label in opponents and label == ident:
                    overrides = {}
                else:
                    raise ValueError("selected entrant is not a recorded subject/opponent")
                expected_hash = digest(wasm_hashes.get(label))
                if identity in reference_keys:
                    if reference_label is not None or expected_hash != reference_hash:
                        raise ValueError("selected reference is ambiguous or not the current reference bytes")
                    reference_label = label
                    reference_toolchains.add(toolchain)
                elif identity in target_keys:
                    bot = target_keys[identity]
                    if bot in seen_targets or expected_hash != target_hashes[identity]:
                        raise ValueError("selected target is repeated or its WASM disagrees with the report")
                    binding = default_aliases.get(bot)
                    if binding is not None:
                        if toolchain != binding["toolchain"]:
                            raise ValueError("current-default alias toolchain disagrees with its measured recipe")
                        protected.add(snapshot(binding["current_default"], expected_hash, {}, toolchain))
                    seen_targets.add(bot)
                    target_labels.append(label)
                else:
                    raise ValueError("selected snapshot is historical or exploratory, not a latest target/reference")
                protected.add(snapshot(identity, expected_hash, overrides, toolchain))
            if reference_label is None or not target_labels or any(
                    (label in subjects) == (reference_label in subjects) for label in target_labels):
                raise ValueError("selected targets were not paired against the selected reference")
            protected.update((spec_path, rows_path))
        if seen_targets != set(targets):
            raise ValueError("latest retention does not cover exactly its measured targets")
        if len(reference_toolchains) != 1:
            raise ValueError("selected reference toolchains disagree")
        protected.add(snapshot(alias, reference_hash, {}, next(iter(reference_toolchains))))
        return protected
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        raise ArenaError(f"cannot safely prune with {manifest}: {exc}") from exc


def _is_link(p: Path) -> bool:
    return p.is_symlink() or bool(getattr(os.path, "isjunction", lambda _p: False)(p))


def _unlink_link(p: Path) -> None:
    try:
        os.unlink(p)
    except OSError:
        os.rmdir(p)  # a junction or directory symlink on Windows: removes the link, not its target


def remove_tree(path: Path) -> int:
    """Delete a directory tree without following links or junctions (snapshots link sdk/ into the
    repository). Returns the bytes freed."""
    freed = 0
    with os.scandir(path) as it:
        entries = list(it)
    for entry in entries:
        p = Path(entry.path)
        if _is_link(p):
            _unlink_link(p)
        elif entry.is_dir(follow_symlinks=False):
            freed += remove_tree(p)
        else:
            freed += entry.stat(follow_symlinks=False).st_size
            try:
                os.unlink(p)
            except PermissionError:
                os.chmod(p, statmod.S_IWRITE)
                os.unlink(p)
    os.rmdir(path)
    return freed


def prune(days: float = 7, snap_days: float = 14, dry_run: bool = False, cache: Path | None = None,
          now: float | None = None, protected: set[Path] | None = None, trim_hours: float = 12) -> list[str]:
    """Remove replays (and same-stem statistics) older than `days` under runs/sweeps/ladder and
    snapshots whose .ready/.failed marker is older than `snap_days`. Returns report lines."""
    cache = Path(cache or CACHE)
    now = time.time() if now is None else now
    if protected is None:
        from population import protected_paths
        protected = protected_paths() | regression_protected_paths(cache)
    pins = {Path(p).resolve() for p in protected}
    # Indexed: a path is pinned when it or an ancestor is a pin; a snapshot also when a pin lies inside
    # it. (Testing every pin per file was quadratic: hours on ~5,000 snapshots and ~1,000 pins.)
    snap_root = (cache / "snap").resolve()
    pin_snaps = set()
    for p in pins:
        if p.is_relative_to(snap_root) and p != snap_root:
            pin_snaps.add(p.relative_to(snap_root).parts[0])

    def pinned(path: Path) -> bool:
        resolved = path.resolve()
        if resolved in pins or any(parent in pins for parent in resolved.parents):
            return True
        return resolved.parent == snap_root and resolved.name in pin_snaps

    verb = "would remove" if dry_run else "removed"
    lines: list[str] = []
    per_dir: dict[Path, list[Path]] = defaultdict(list)
    for area in PRUNE_AREAS:
        base = cache / area
        if not base.is_dir() or _is_link(base):
            continue
        for f in base.rglob("*.repl26a"):
            try:  # another process (a run, a checkup) may remove files while this walks
                old = f.is_file() and not _is_link(f) and now - f.stat().st_mtime > days * 86400
            except OSError:
                continue
            if old:
                companions = (f.with_suffix(".json"), f.parent / f"{f.stem}.stats.json")
                if pinned(f) or any(pinned(p) for p in companions):
                    continue
                per_dir[f.parent].append(f)
                for comp in companions:
                    if comp.is_file() and comp.name != "games.jsonl":
                        per_dir[f.parent].append(comp)
    files_n = files_bytes = 0
    for d, files in sorted(per_dir.items()):
        size = removed = 0
        for f in files:
            try:
                n = f.stat().st_size
                if not dry_run:
                    f.unlink()
                size, removed = size + n, removed + 1
            except OSError as exc:
                lines.append(f"  could not remove {_rel(f)}: {exc}")
        files_n, files_bytes = files_n + removed, files_bytes + size
        lines.append(f"{verb} {removed:4} replay files {size / 2**20:8.1f} MiB  {_rel(d)}")
    snaps = snap_bytes = 0
    snap_root = cache / "snap"
    kept_pinned = trimmed = trim_bytes = 0
    for d in sorted(snap_root.iterdir()) if snap_root.is_dir() else []:
        if not d.is_dir() or _is_link(d):
            continue
        if pinned(d):
            kept_pinned += 1
            continue
        marker = next((d / m for m in (".ready", ".failed") if (d / m).is_file()), None)
        ref = marker or (d / "meta.json" if (d / "meta.json").is_file() else d)
        try:
            idle = (now - ref.stat().st_mtime) / 86400
        except OSError:  # removed meanwhile
            continue
        if idle <= snap_days:
            # Idle for trim_hours: keep only sources and bot.wasm (the CLI path rebuilds on first use).
            if trim_hours > 0 and idle * 24 > trim_hours and marker is not None and marker.name == ".ready" \
                    and not (d / core.TRIMMED).is_file():
                trimmed += 1
                if not dry_run:
                    trim_bytes += core.trim_snapshot(d)
            continue
        meta = _load_json(d / "meta.json") or {}
        state = marker.name[1:] if marker else "incomplete"
        try:
            with FileLock(snap_root / f"{d.name}.lock", poll=0.05, timeout=0.2):
                # prepare() may have reused or rebuilt this snapshot since discovery.
                # Its lock protects the freshness check as well as the removal.
                marker = next((d / m for m in (".ready", ".failed") if (d / m).is_file()), None)
                ref = marker or (d / "meta.json" if (d / "meta.json").is_file() else d)
                idle = (now - ref.stat().st_mtime) / 86400
                if idle <= snap_days:
                    lines.append(f"  skipped snapshot {d.name}: recently used")
                    continue
                meta = _load_json(d / "meta.json") or {}
                state = marker.name[1:] if marker else "incomplete"
                if not dry_run:
                    if marker:
                        marker.unlink()  # first, so a partial removal can never look ready
                    snap_bytes += remove_tree(d)
            snaps += 1
            lines.append(f"{verb} snapshot {d.name} ({meta.get('bot', '?')}, {state}, unused {idle:.0f} days)")
        except ArenaError:
            lines.append(f"  skipped snapshot {d.name}: in use")
        except OSError as exc:
            lines.append(f"  could not remove snapshot {d.name}: {exc}")
    out = lines[:60] + ([f"... {len(lines) - 60} more lines"] if len(lines) > 60 else [])
    freed = "" if dry_run else f", {(files_bytes + snap_bytes + trim_bytes) / 2**20:.1f} MiB freed"
    out.append(f"{verb} {files_n} replay files older than {days:g} days and {snaps} snapshots unused for"
               f" {snap_days:g} days; {'would trim' if dry_run else 'trimmed'} {trimmed} snapshots idle over"
               f" {trim_hours:g} h to sources and bot.wasm; kept {kept_pinned} pinned snapshots{freed}")
    return out


# ----------------------------------------------------------------------------- commands
def _cmd_report(args) -> int:
    targets = list(args.target)
    if targets[0] == "game" and not _path(targets[0]).exists():
        if len(targets) != 2:
            raise ArenaError("usage: arena report game PATH (a statistics JSON or a .repl26a replay)")
        for flag in ("by", "subject"):
            if getattr(args, flag):
                raise ArenaError(f"--{flag} applies to `arena report RUN` (game reports take --team)")
        lines, summary = game_report(targets[1], team=args.team, positions=args.positions, events=args.events,
                                     rounds=args.rounds, limit=args.limit or 12, stats_path=args.stats,
                                     replay_path=args.replay)
    else:
        for flag in ("team", "positions", "events", "rounds", "stats", "replay"):
            if getattr(args, flag):
                raise ArenaError(f"--{flag} applies to `arena report game PATH`")
        rows, files = load_rows(targets)
        lines, summary = run_report(rows, by=args.by, subject=args.subject, limit=args.limit or 40,
                                    incidents=args.incidents)
        lines[0] = f"{_rel(files[0]) if len(files) == 1 else f'{len(files)} logs'}: {lines[0]}"
    if args.json:
        print(json.dumps(summary, indent=1, default=str))
    else:
        print("\n".join(lines))
    return 0


def _cmd_map(args) -> int:
    try:
        nums = [int(v) for v in args.region]
        marks = [tuple(int(c) for c in m.split(",")) for m in args.mark or []]
    except ValueError:
        raise ArenaError("region takes integers x0 y0 x1 y1 and --mark takes x,y")
    if len(nums) not in (0, 4) or any(len(m) != 2 for m in marks):
        raise ArenaError("region takes four numbers x0 y0 x1 y1 and --mark takes x,y")
    print("\n".join(map_view(args.name, tuple(nums) if nums else None, marks)))
    return 0


def _cmd_prune(args) -> int:
    print("\n".join(prune(args.days, args.snap_days, args.dry_run, trim_hours=args.trim_hours)))
    return 0


def register(sub) -> None:
    fmt = argparse.RawDescriptionHelpFormatter
    p = sub.add_parser("report", help="tables of game rows (RUN) or one game's report (game PATH)",
                       description=__doc__, formatter_class=fmt)
    p.add_argument("target", nargs="+", metavar="RUN | game PATH",
                   help="run directories or .jsonl logs, or `game` and a statistics JSON or replay")
    p.add_argument("--by", help="group by opponent, map, side, size or tag:NAME")
    p.add_argument("--subject", help="perspective: A, B, an entrant key, bot id or name (globs allowed)")
    p.add_argument("--limit", type=int, default=0, help="rows per table (runs, 40) or deaths/events listed (game, 12)")
    p.add_argument("--incidents", type=int, default=8, help="health incidents listed (runs)")
    p.add_argument("--team", help="game: only team A or B")
    p.add_argument("--positions", action="store_true", help="game: decode the replay for cohesion and death context")
    p.add_argument("--events", help="game: list native events of these kinds (comma-separated, or all)")
    p.add_argument("--rounds", help="game: restrict deaths and events to FROM:TO")
    p.add_argument("--stats", help="game: statistics JSON of the replay (default: found beside it)")
    p.add_argument("--replay", help="game: replay of the statistics JSON (default: found beside it)")
    p.add_argument("--json", action="store_true", help="print the summary as JSON")
    p.set_defaults(func=_cmd_report)
    p = sub.add_parser("map", help="ASCII view of a map region with terrain, spawns and camps",
                       description=__doc__, formatter_class=fmt)
    p.add_argument("name", help="map name (file stem such as 00reverse-spiral, or pool name)")
    p.add_argument("region", nargs="*", help="x0 y0 x1 y1 (default: the whole map)")
    p.add_argument("--mark", nargs="+", metavar="X,Y", help="tiles to mark with *")
    p.set_defaults(func=_cmd_map)
    p = sub.add_parser("prune", help="remove old replays and unused build snapshots from .cache/arena",
                       description=__doc__, formatter_class=fmt)
    p.add_argument("--days", type=float, default=7, help="replays older than this many days (default 7)")
    p.add_argument("--snap-days", type=float, default=14, help="snapshots unused this many days (default 14)")
    p.add_argument("--trim-hours", type=float, default=12,
                   help="reduce unpinned snapshots idle this long to sources and bot.wasm (0: off; default 12)")
    p.add_argument("--dry-run", action="store_true", help="only list what would be removed")
    p.set_defaults(func=_cmd_prune)
