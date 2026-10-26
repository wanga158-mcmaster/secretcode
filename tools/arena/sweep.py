"""Knob sweeps against a same-sweep baseline with paired statistics, and paired comparison of two runs.

  arena sweep BOT --var "NAME: k=v [k=v ...]" [--var ...] [--vs OPPS] [--maps pool] [--sides both]
              [--seeds N [--seed-knob noise_burn]] [--confirm] [--jobs N] [--rounds N] [--label L]
              [--out DIR] [--keep-replays flagged|all|none]
  arena compare RUN_A RUN_B [--variant-a NAME] [--variant-b NAME] [--subject-side-from-tags | --subject SEL]

Sweep. BOT's build closure is read once (core.read_closure) and the baseline ("base": no overrides)
and every variant are prepared from that same content, so edits made during the sweep cannot leak
in. Every entrant plays every opponent on every map from each side (and with each seed) in one
games.play call; rows are tagged {subject, variant, opponent, seed, phase} and land in
.cache/arena/sweeps/<time>-<label>/games.jsonl beside report.md and sweep.json.
  OPPS     comma-separated bot selectors (default anchors/starter-cpp,anchors/fundamentals) and
           ladder:nearN, the N current ladder entrants rated nearest to BOT (Bradley-Terry over
           results/ladder/games.jsonl); the default opponents stand in when BOT is unrated there.
  seeds    --seeds N adds KNOB=s (s = 1..N, KNOB = --seed-knob) to the baseline and to every
           variant as separate snapshots; the seed is part of the game key (0 = unseeded).
  report   per variant: pooled W-L-D and score (draws half) with a 95% Wilson interval; paired vs
           base on identical (opponent, map, side, seed) keys: flips +/-, exact McNemar p, gem-margin
           difference +- se; breakdowns per opponent and map size (small/large pool halves);
           subject health (faults, fuel exhaustion, error lines, stalls, max fuel); a verdict.
  verdict  "better": McNemar p < 0.05 with more games gained than lost, or a paired gem-margin
           difference above 2 se whose sign the net flips do not contradict; "worse" mirrors it;
           otherwise "no detectable difference". Only finished games pair: error, skipped and
           truncated (--rounds) games are counted apart.
  --confirm  replays base and the best variant on the legacy maps (never used for tuning) with the
           same opponents, sides and seeds and reports their pooled paired result: the best of many
           variants is biased upwards on the maps that chose it (winner's curse).
Compare. RUN_B relative to RUN_A (run directories or .jsonl logs of game rows), paired on
(opponent, map, side, seed) from the subject's side: tags.subject by default
(--subject-side-from-tags says so explicitly), or --subject A|B|key|bot id|name (globs allowed).
A sweep directory holds several variants: pick one per side with --variant-a/--variant-b.
"""
from __future__ import annotations

import fnmatch
import json
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import (CACHE, RESULTS, ROOT, ArenaError, Bot, all_bots, apply_overrides, map_set,  # noqa: E402
                  prepare_many, read_closure, read_jsonl, select, version_of)
from core import bot as find_bot  # noqa: E402
from games import KEEP, GameSpec, add_resume_args, deadline_from, health, perspective, play  # noqa: E402
from stats import format_paired, paired, wilson  # noqa: E402

BASE = "base"
DEFAULT_VS = "anchors/starter-cpp,anchors/fundamentals"
LADDER_LOG = RESULTS / "ladder" / "games.jsonl"
LADDER_NEAR = re.compile(r"^ladder:near(\d+)$", re.I)
VAR_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.+-]*$")
PLAYED = ("win", "loss", "draw")
SIZES = ("small", "large", "other")
VERDICTS = ("better", "no detectable difference", "worse")
LIMIT = 12  # terminal lines per long section (report.md keeps everything)


# ----------------------------------------------------------------------------- helpers
def _rel(p) -> str:
    p = Path(p).resolve()
    return p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p)


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _slug(s: str, n: int = 48) -> str:
    return re.sub(r"[^A-Za-z0-9.-]+", "-", str(s)).strip("-")[:n] or "x"


def tail_lines(text: str | None, n: int = 8) -> list[str]:
    return [l.rstrip() for l in str(text or "").strip().splitlines() if l.strip()][-n:] or ["(no output)"]


def brief(text: str | None, limit: int = 160) -> str:
    """First and last non-empty line of an error text, on one line."""
    lines = [l.strip() for l in str(text or "").splitlines() if l.strip()] or ["?"]
    s = lines[0] if len(lines) == 1 else f"{lines[0]} ... {lines[-1]}"
    return s if len(s) <= limit else s[:limit - 3] + "..."


def out_dir(arg: str, area: str, name: str) -> Path:
    """--out DIR, else a fresh .cache/arena/<area>/<YYYYmmdd-HHMMSS>-<name>/."""
    if arg:
        out = Path(arg).resolve()
        out.mkdir(parents=True, exist_ok=True)
        return out
    base = CACHE / area / f"{time.strftime('%Y%m%d-%H%M%S')}-{_slug(name)}"
    out, k = base, 1
    while True:
        try:
            out.mkdir(parents=True, exist_ok=False)
            return out
        except FileExistsError:
            k += 1
            out = base.with_name(f"{base.name}-{k}")


def table(header: list[str], rows: list[list[str]]) -> list[str]:
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    return ["  ".join(str(c).ljust(w) for c, w in zip(r, widths)).rstrip() for r in [header] + rows]


def parse_var(spec: str) -> tuple[str, dict[str, str]]:
    """'NAME: k=v [k=v ...]' -> (NAME, {k: v}). Values are written verbatim (e.g. 1.1f, true);
    'NAME:' alone is a no-change variant (the baseline build, played again)."""
    name, sep, rest = str(spec).partition(":")
    name = name.strip()
    if not sep or not VAR_NAME.match(name):
        raise ArenaError(f"--var expects 'NAME: k=v [k=v ...]' (NAME: letters, digits, _.+-), got {spec!r}")
    if name == BASE:
        raise ArenaError(f"variant name {BASE!r} is reserved for the baseline")
    overrides: dict[str, str] = {}
    for item in rest.split():
        k, eq, v = item.partition("=")
        if not eq or not k or not v:
            raise ArenaError(f"--var {name}: expected k=v, got {item!r}")
        if k in overrides:
            raise ArenaError(f"--var {name}: knob {k} given twice")
        overrides[k] = v
    return name, overrides


def seed_list(n: int) -> list[int]:
    return list(range(1, n + 1)) if n and n > 0 else [0]


def _seed(v):
    try:
        return int(v or 0)
    except (TypeError, ValueError):
        return str(v)


_SIZE: dict[str, str] | None = None


def size_of(map_name: str) -> str:
    """small/large as in `--maps small|large` (pool halves at the median area); other otherwise."""
    global _SIZE
    if _SIZE is None:
        sizes: dict[str, str] = {}
        try:
            for s in ("small", "large"):
                sizes.update(dict.fromkeys(map_set(s), s))
        except Exception:  # a missing geometry export only loses the breakdown
            pass
        _SIZE = sizes
    return _SIZE.get(str(map_name)) or "other"


# ----------------------------------------------------------------------------- opponents
def ladder_neighbours(subject: Bot, version: str, n: int, log: Path | None = None) -> tuple[list[Bot], str]:
    """The n current ladder entrants rated nearest to `subject`, or ([], reason).
    Ratings: stats.bradley_terry over the finished games of the ladder log. A bot's current entrant is
    the version it played last in the log; the subject is rated at `version` when the ladder has
    played it, else at its last version. Only existing bots count, never the subject's own bot."""
    log = Path(log or LADDER_LOG)
    rows = read_jsonl(log)
    try:  # only the pinned engine's games count, as on the ladder itself
        from ladder import ENGINE
        rows = [r for r in rows if ((r.get("tags") or {}).get("engine") or r.get("engine")) in (None, ENGINE)]
    except ImportError:
        pass
    if not rows:
        return [], f"no ladder games in {_rel(log)}"
    from stats import bradley_terry
    games, last = [], {}
    for r in rows:
        a, b, w = r.get("a"), r.get("b"), r.get("winner")
        if not a or not b or w not in ("A", "B", "draw") or r.get("truncated"):
            continue
        games.append((a, b, 1.0 if w == "A" else 0.0 if w == "B" else 0.5))
        for key in (a, b):
            last[key.split("@")[0]] = key
    ratings = bradley_terry(games)
    me = ratings.get(f"{subject.id}@{version}") or ratings.get(last.get(subject.id, ""))
    if not me:
        return [], f"{subject.id} has no ladder rating"
    exists = {b.id: b for b in all_bots()}
    near = sorted((abs(ratings[k]["elo"] - me["elo"]), bid, ratings[k]["elo"]) for bid, k in last.items()
                  if bid != subject.id and bid in exists and k in ratings)[:n]
    if not near:
        return [], "no other current ladder entrants"
    names = ", ".join(f"{bid} {elo:.0f}" for _, bid, elo in near)
    return [exists[bid] for _, bid, _ in near], f"{subject.id} {me['elo']:.0f} -> {names}"


def resolve_opponents(spec: str, subject: Bot, version: str, log: Path | None = None) -> tuple[list[Bot], list[str]]:
    """Selectors and ladder:nearN items (in order, so '-sel' exclusions apply after them)."""
    items, notes = [], []
    for item in [s.strip() for s in str(spec or DEFAULT_VS).split(",") if s.strip()]:
        m = LADDER_NEAR.match(item)
        if not m:
            items.append(item)
            continue
        if int(m.group(1)) < 1:
            raise ArenaError(f"{item}: N must be at least 1")
        near, note = ladder_neighbours(subject, version, int(m.group(1)), log)
        if near:
            items += [b.id for b in near]
            notes.append(f"{item}: {note}")
        else:
            items += DEFAULT_VS.split(",")
            notes.append(f"{item}: {note}; using {DEFAULT_VS}")
    opponents = select(items) if items else []
    if not opponents:
        raise ArenaError(f"opponents {spec!r} leave no bot")
    return opponents, notes


# ----------------------------------------------------------------------------- statistics over rows
def finished(p: dict) -> bool:
    return p["result"] in PLAYED and not p["truncated"]


def game_key(p: dict) -> tuple:
    """(opponent bot id, map, subject side, seed) of a perspective row."""
    t = p.get("tags") or {}
    opp = t.get("opponent") or str(p.get("opponent") or "?").split("@")[0]
    return (str(opp), str(p.get("map")), str(p.get("side")), _seed(t.get("seed")))


def key_text(k: tuple) -> str:
    return f"{k[1]} {k[2]} vs {k[0]}" + (f" s{k[3]}" if k[3] else "")


def keyed(ps: list[dict]) -> dict:
    """Finished games by game key (a repeated key keeps its last row)."""
    return {game_key(p): p for p in ps if finished(p)}


def tally(ps: list[dict]) -> dict:
    """W-L-D, score (draws half) with a 95% Wilson interval and mean gem margin over finished games;
    health sums over played games (truncated ones included); failed = error or skipped games."""
    fin = [p for p in ps if finished(p)]
    played = [p for p in ps if p["result"] in PLAYED]
    n = len(fin)
    w = sum(p["result"] == "win" for p in fin)
    l = sum(p["result"] == "loss" for p in fin)
    d = n - w - l
    return {"games": len(ps), "n": n, "w": w, "l": l, "d": d,
            "score": (w + d / 2) / n if n else None, "ci": list(wilson(w + d / 2, n)) if n else None,
            "margin": sum(p["gems"] - p["opp_gems"] for p in fin) / n if n else None,
            "faults": sum(p["faults"] for p in played), "exhausted": sum(p["exhausted"] for p in played),
            "errors": sum(p["errors"] for p in played), "stalls": sum(bool(p["stall"]) for p in played),
            "suspended": sum(p["fuel_at_budget"] for p in played if not p["exhausted"]),
            "fuel_max": max((p["fuel_max"] for p in played), default=0),
            "failed": sum(p["result"] in ("error", "skipped") for p in ps),
            "truncated": sum(1 for p in played if p["truncated"])}


def wld(t: dict) -> str:
    return f"{t['w']}-{t['l']}-{t['d']}"


def score_text(t: dict) -> str:
    if not t["n"]:
        return "no finished games"
    lo, hi = t["ci"]
    return f"{wld(t)} score {t['score']:.3f} (95% CI {lo:.2f}-{hi:.2f})"


_T975 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 12: 2.18,
         15: 2.13, 20: 2.09, 25: 2.06, 30: 2.04, 40: 2.02, 60: 2.00}


def _t975(df: int) -> float:
    """Two-sided 5% critical value of Student's t (tabulated, conservative between entries)."""
    return _T975[max(k for k in _T975 if k <= df)] if df >= 1 else float("inf")


def verdict(p: dict, alpha: float = 0.05, z: float = 2.0) -> tuple[str, str]:
    """(word, reason) for a stats.paired result of a variant against its baseline."""
    if not p.get("n"):
        return VERDICTS[1], "no common finished games"
    g, l = len(p["gained"]), len(p["lost"])
    net = (g > l) - (g < l)
    d, se = p["diff_mean"], p["diff_se"]
    sign = (d > 0) - (d < 0)
    flips = f"flips +{g}/-{l}, McNemar p={p['mcnemar_p']:.3f}"
    margin = f"margin diff {d:+.1f} +- {se:.1f} se"
    if net and p["mcnemar_p"] < alpha:
        return ("better" if net > 0 else "worse"), f"{flips}; {margin}"
    crit = max(z, _t975(p["n"] - 1))  # the paired margin is a t statistic with n-1 df
    if sign and p["n"] >= 2 and abs(d) > crit * se and net in (0, sign):
        return ("better" if sign > 0 else "worse"), f"{margin} (beyond {crit:.2g} se); {flips}"
    return VERDICTS[1], f"{flips}; {margin}"


def health_lines(rows: list[dict], label=None) -> list[str]:
    """One line per game where the subject failed or shows a health problem (games.health)."""
    out = []
    for r in rows:
        p = perspective(r)
        if p is None:
            continue
        who = f"{label(p)}: " if label else ""
        where = f"{who}{key_text(game_key(p))}"
        if p["result"] in ("error", "skipped"):
            out.append(f"{where}: {p['result']}: {brief(r.get('error'))}")
        elif health(r, p["side"]):
            out.append(f"{where}: {', '.join(health(r, p['side']))}" + (f"  {r['replay']}" if r.get("replay") else ""))
    return out


# ----------------------------------------------------------------------------- reports
def section(title: str, lines: list[str], limit: int | None = None, short: list[str] | None = None) -> dict:
    return {"title": title, "lines": lines, "limit": limit, "short": short}


def _views(rows: list[dict], phase: str | None) -> dict[str, list[dict]]:
    by: dict[str, list[dict]] = {}
    for r in rows:
        t = r.get("tags") or {}
        if phase is not None and t.get("phase", "sweep") != phase:
            continue
        p = perspective(r)
        if p is not None:
            by.setdefault(str(t.get("variant", BASE)), []).append(p)
    return by


def _breakdown(by: dict, names: list[str], group, order=None) -> list[str]:
    groups = sorted({group(p) for v in names for p in by.get(v, [])},
                    key=lambda g: (order.index(g) if order and g in order else len(order or ()), g))
    base = keyed(by.get(BASE, []))
    rows = []
    for g in groups:
        cells = [g]
        base_g = {k: p for k, p in base.items() if group(p) == g}
        for v in names:
            ps = [p for p in by.get(v, []) if group(p) == g]
            t = tally(ps)
            cell = wld(t) if t["n"] else "-"
            if v != BASE and t["n"]:
                pr = paired(base_g, keyed(ps))
                if pr.get("n"):
                    cell += f" +{len(pr['gained'])}/-{len(pr['lost'])}"
            cells.append(cell)
        rows.append(cells)
    return table(["", *names], rows) if rows else ["no games"]


def sweep_report(rows: list[dict], names: list[str] | None = None, phase: str | None = "sweep",
                 sizes: bool = True) -> tuple[list[dict], dict]:
    """Report sections and per-variant results of sweep rows (tags.variant, tags.phase)."""
    by = _views(rows, phase)
    names = list(names or [BASE])
    names += sorted(v for v in by if v not in names)
    base = keyed(by.get(BASE, []))
    results, pooled, paired_lines, verdict_lines, flips_full, flips_short = {}, [], [], [], [], []
    for v in names:
        t = tally(by.get(v, []))
        res = {"tally": t}
        extra = ", ".join(f"{t[k]} {k}" for k in ("failed", "truncated") if t[k])
        ci = f"{t['ci'][0]:.2f}-{t['ci'][1]:.2f}" if t["n"] else "-"
        pooled.append([v, str(t["games"]), str(t["n"]), wld(t), f"{t['score']:.3f}" if t["n"] else "-", ci,
                       f"{t['margin']:+.0f}" if t["n"] else "-", extra])
        if v != BASE:
            pr = paired(base, keyed(by.get(v, [])))
            word, reason = verdict(pr)
            res.update(paired={k: val for k, val in pr.items() if k not in ("gained", "lost")},
                       gained=[key_text(k) for k in pr.get("gained", [])],
                       lost=[key_text(k) for k in pr.get("lost", [])], verdict=word, reason=reason)
            paired_lines.append(f"{v}: {format_paired(pr)}")
            sick = [f"{k} {t[k]}" for k in ("faults", "exhausted", "errors", "stalls", "suspended", "failed") if t[k]]
            verdict_lines.append(f"{v}: {word} ({reason})" + (f" [health: {', '.join(sick)}]" if sick else ""))
            parts = [f"{sign}{len(ks)} [{'; '.join(ks)}]" for sign, ks in (("+", res["gained"]), ("-", res["lost"])) if ks]
            full = f"{v}: {'  '.join(parts) or 'no flips'}"
            flips_full.append(full)
            flips_short.append(full if len(full) <= 150 else full[:147] + "...")
        results[v] = res
    health_tab = table(["variant", "faults", "exhausted", "errors", "stalls", "suspended", "fuel max"],
                       [[v, *(str(results[v]["tally"][k]) for k in ("faults", "exhausted", "errors", "stalls",
                                                                     "suspended", "fuel_max"))] for v in names])
    offenders = health_lines([r for r in rows if (phase is None or (r.get("tags") or {}).get("phase", "sweep") == phase)],
                             label=lambda p: str(p["tags"].get("variant", BASE)))
    secs = [section("Pooled (subject; score = (W + D/2) / N over finished games, 95% Wilson interval)",
                    table(["variant", "games", "N", "W-L-D", "score", "95% CI", "gem margin", ""], pooled))]
    if len(names) > 1:
        secs.append(section("Paired vs base on identical (opponent, map, side, seed)", paired_lines))
        secs.append(section("Verdicts", verdict_lines))
    secs.append(section("By opponent (W-L-D, flips vs base)", _breakdown(by, names, lambda p: game_key(p)[0])))
    if sizes:
        secs.append(section("By map size (pool halves at the median area)",
                            _breakdown(by, names, lambda p: size_of(p["map"]), list(SIZES))))
    secs.append(section("Subject health", health_tab + ([f"{len(offenders)} game(s) with problems:"] + offenders
                                                        if offenders else ["no problems"]),
                        limit=len(health_tab) + 1 + LIMIT))
    if len(names) > 1:
        secs.append(section("Flipped games vs base (+gained, -lost)", flips_full, short=flips_short))
    return secs, results


def best_variant(results: dict, names: list[str]) -> str | None:
    """The variant with the best verdict, then net flips, then paired margin difference."""
    def rank(v):
        r = results.get(v) or {}
        p = r.get("paired") or {}
        if not p.get("n"):
            return (-1, 0, 0.0)
        return (2 - VERDICTS.index(r["verdict"]), len(r["gained"]) - len(r["lost"]), p["diff_mean"])
    cands = [v for v in names if v != BASE]
    return max(cands, key=rank) if cands else None


def render(sections: list[dict], terminal: bool) -> list[str]:
    out = []
    for s in sections:
        out.append(f"== {s['title']}" if terminal else f"## {s['title']}")
        lines = s["lines"]
        if terminal:
            lines = s["short"] if s["short"] is not None else lines
            limit = s["limit"] if s["limit"] is not None else len(lines)
            if len(lines) > limit:
                lines = lines[:limit] + [f"... {len(lines) - limit} more in report.md"]
            out += lines
        else:
            out += ["", "```", *lines, "```", ""]
    return out


# ----------------------------------------------------------------------------- sweep command
def _plan_games(mine: dict, names: list[str], seeds: list[int], opps, maps: list[str], sides: list[str],
                phase: str) -> list[GameSpec]:
    """Key-major order (every variant of a key together), so an interrupted sweep still pairs."""
    specs = []
    for o in opps:
        for m in maps:
            for side in sides:
                for s in seeds:
                    for v in names:
                        e = mine[(v, s)]
                        a, b = (e, o) if side == "A" else (o, e)
                        specs.append(GameSpec(a, b, m, {"subject": side, "variant": v, "opponent": o.bot.id,
                                                        "seed": s, "phase": phase}))
    return specs


def _cmd_sweep(args) -> int:
    deadline = deadline_from(args.max_minutes)
    subject = find_bot(args.bot)
    variants = [parse_var(v) for v in args.var]
    if not variants:
        raise ArenaError("give at least one --var 'NAME: k=v [k=v ...]'")
    names = [BASE] + [n for n, _ in variants]
    dup = sorted({n for n in names if names.count(n) > 1})
    if dup:
        raise ArenaError(f"variant names must be unique: {', '.join(dup)}")
    seeds = seed_list(args.seeds)
    if args.seeds > 0:
        clash = [n for n, ov in variants if args.seed_knob in ov]
        if clash:
            raise ArenaError(f"variant {clash[0]} sets the seed knob {args.seed_knob}")
    maps = map_set(args.maps)
    if not maps:
        raise ArenaError("no maps selected")
    sides = ["A", "B"] if args.sides == "both" else [args.sides]
    contents = read_closure(subject)  # the one read: baseline and variants all build from it
    version = version_of(contents)
    plan = []
    for v, ov in [(BASE, {})] + variants:
        for s in seeds:
            full = dict(ov, **({args.seed_knob: str(s)} if s else {}))
            apply_overrides(subject, contents, full)  # every knob must resolve before anything builds
            plan.append((v, s, full))
    opponents, notes = resolve_opponents(args.vs, subject, version)
    out = out_dir(args.out, "sweeps", args.label or subject.id)
    seeds_text = f"seeds 1..{args.seeds} ({args.seed_knob})" if args.seeds > 0 else "unseeded"
    n_games = len(names) * len(seeds) * len(opponents) * len(maps) * len(sides)
    head = [f"sweep {subject.id}@{version} -> {_rel(out)}",
            "variants: " + "; ".join(f"{v}: {' '.join(f'{k}={x}' for k, x in ov.items()) or '(no change)'}"
                                     for v, ov in variants),
            f"vs {', '.join(o.id for o in opponents)} | {len(maps)} map(s) ({args.maps}) | side {'/'.join(sides)}"
            f" | {seeds_text} | {n_games} games" + (f" | {args.rounds} rounds" if args.rounds else "")]
    print("\n".join(head + [f"note: {n}" for n in notes]), flush=True)
    items = [(subject, full, f"{subject.id}:{v}", contents) for v, s, full in plan]
    items += [(o, None, "", contents if o == subject else None) for o in opponents]
    print(f"preparing {len(items)} snapshot(s)", file=sys.stderr, flush=True)
    ents = prepare_many(items)
    mine = {(v, s): e for (v, s, _), e in zip(plan, ents)}
    opps = ents[len(plan):]
    for v, ov in variants:
        if any(mine[(v, s)].version == mine[(BASE, s)].version for s in seeds):
            notes.append(f"{v} builds identically to base (a determinism check)")
            print(f"note: {notes[-1]}")
    for key, e in {e.key: e for e in ents if not e.ok}.items():
        print(f"FAILED build {key}\n    " + "\n    ".join(tail_lines(e.log, 8)), flush=True)
    specs = _plan_games(mine, names, seeds, opps, maps, sides, "sweep")
    meta = {"bot": subject.id, "version": version, "started": _now(), "out": _rel(out),
            "variants": {v: {"overrides": dict(ov), "versions": {str(s): mine[(v, s)].version for s in seeds}}
                         for v, ov in [(BASE, {})] + variants},
            "opponents": [o.key for o in opps], "maps": maps, "sides": sides, "seeds": args.seeds,
            "seed_knob": args.seed_knob if args.seeds > 0 else None, "rounds": args.rounds, "notes": notes}
    (out / "sweep.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    if not any(mine[(BASE, s)].ok for s in seeds):
        print("the baseline did not build: nothing to compare")
        return 1
    rows = play(specs, out, jobs=args.jobs, rounds=args.rounds, keep_replays=args.keep_replays, resume=True,
                deadline=deadline)
    secs, results = sweep_report(rows, names)
    meta["results"] = results
    confirm_rows = []
    if args.confirm:
        best = best_variant(results, names)
        legacy = [m for m in map_set("legacy") if m not in maps]
        title = f"Confirm {best} vs base on the legacy maps ({len(legacy)})"
        if not legacy:
            secs.append(section(title, ["skipped: the sweep already played every legacy map"]))
        else:
            print(f"confirm: {best} vs base on {len(legacy)} legacy map(s)", flush=True)
            confirm_rows = play(_plan_games(mine, [BASE, best], seeds, opps, legacy, sides, "confirm"), out,
                                jobs=args.jobs, rounds=args.rounds, keep_replays=args.keep_replays,
                                resume=True, deadline=deadline)
            csecs, cres = sweep_report(confirm_rows, [BASE, best], phase="confirm", sizes=False)
            r = cres.get(best) or {}
            summary = [f"pooled paired: {format_paired(dict(r.get('paired') or {}, gained=r.get('gained', []), lost=r.get('lost', [])))}",
                       f"verdict on legacy: {r.get('verdict', '?')} ({r.get('reason', '')})",
                       f"in the sweep: {results[best]['verdict']} ({results[best]['reason']})"]
            secs.append(section(title, summary))
            secs += [dict(s, title=f"Confirm: {s['title']}") for s in csecs
                     if not s["title"].startswith(("Paired", "Verdicts"))]
            meta["confirm"] = {"variant": best, "maps": legacy, "results": cres}
    meta["finished"] = _now()
    (out / "sweep.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    md = [f"# Sweep {subject.id}@{version}", "", *[f"- {h}" for h in head[1:]], *[f"- note: {n}" for n in notes],
          f"- out `{_rel(out)}`", "",
          *render(secs, terminal=False)]
    (out / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(render(secs, terminal=True)))
    print(f"out {_rel(out)} (report.md, games.jsonl, sweep.json)")
    return 1 if any(r["winner"] in ("error", "skipped") for r in rows + confirm_rows) else 0


# ----------------------------------------------------------------------------- compare command
def load_rows(target: str) -> tuple[list[dict], Path]:
    """Game rows of a run directory (its games.jsonl) or of a .jsonl log."""
    p = Path(target)
    if not p.exists() and not p.is_absolute() and (ROOT / p).exists():
        p = ROOT / p
    if p.is_dir():
        p = p / "games.jsonl"
    rows = read_jsonl(p)
    if not rows:
        raise ArenaError(f"no game rows in {target}")
    return rows, p.resolve()


def _matches(row: dict, side: str, pattern: str) -> bool:
    key = str(row.get(side.lower()) or "")
    cands = {key, key.split("@")[0], str(row.get(f"{side.lower()}_name") or "")}
    return any(c and (c == pattern or fnmatch.fnmatchcase(c, pattern)) for c in cands)


def subject_side(row: dict, subject: str | None = None) -> str | None:
    """tags.subject by default; else A|B, or the one side whose entrant key, bot id or name matches."""
    if not subject:
        side = str((row.get("tags") or {}).get("subject") or "").upper()
        return side if side in ("A", "B") else None
    if subject.upper() in ("A", "B"):
        return subject.upper()
    hits = [s for s in ("A", "B") if _matches(row, s, subject)]
    return hits[0] if len(hits) == 1 else None


def subject_views(rows: list[dict], subject: str | None = None, variant: str | None = None) -> tuple[list[dict], int]:
    """(perspective rows of the subject, rows without a known subject), optionally of one tags.variant."""
    out, dropped = [], 0
    for r in rows:
        if variant is not None and str((r.get("tags") or {}).get("variant")) != variant:
            continue
        side = subject_side(r, subject)
        p = perspective(r, side) if side else None
        if p is None:
            dropped += 1
        else:
            out.append(p)
    return out, dropped


def compare(rows_a: list[dict], rows_b: list[dict], subject: str | None = None, variant_a: str | None = None,
            variant_b: str | None = None) -> tuple[list[str], dict]:
    """Lines and the stats.paired result of B relative to A on identical game keys."""
    lines, sides = [], {}
    for tag, rows, variant in (("A", rows_a, variant_a), ("B", rows_b, variant_b)):
        views, dropped = subject_views(rows, subject, variant)
        if variant is not None and not views and not dropped:
            have = sorted({str(r["tags"]["variant"]) for r in rows if "variant" in (r.get("tags") or {})})
            raise ArenaError(f"run {tag} has no rows of variant {variant!r}" + (f" (it holds {', '.join(have)})"
                                                                               if have else ""))
        if variant is None:
            found = sorted({str(p["tags"]["variant"]) for p in views if "variant" in p["tags"]})
            if len(found) > 1:
                raise ArenaError(f"run {tag} holds variants {', '.join(found)}: pick one with --variant-{tag.lower()}")
        fin = [p for p in views if finished(p)]
        k = keyed(views)
        t = tally(views)
        names = sorted({str(p["me"]) for p in views})
        what = names[0] if len(names) == 1 else f"{len(names)} subject entrants"
        extra = [f"{n} {w}" for n, w in ((t["failed"], "failed"), (t["truncated"], "truncated"),
                                         (dropped, "without subject"), (len(fin) - len(k), "repeated keys"))
                 if n]
        lines.append(f"{tag}: {what}  {t['games']} game{'s' if t['games'] != 1 else ''}, {score_text(t)}"
                     + (f"  ({', '.join(extra)})" if extra else ""))
        sides[tag] = (views, k, t)
    (va, ka, ta), (vb, kb, tb) = sides["A"], sides["B"]
    common = set(ka) & set(kb)
    pr = paired(ka, kb)
    word, reason = verdict(pr)
    lines.append(f"paired B vs A: {format_paired(pr)}")
    lines.append(f"verdict: B {word} ({reason})")
    only = [f"{len(set(ka) - common)} keys only in A"] if set(ka) - common else []
    only += [f"{len(set(kb) - common)} only in B"] if set(kb) - common else []
    differ = sum(1 for k in common if ka[k]["opponent"] != kb[k]["opponent"])
    if differ:
        only.append(f"{differ} pairs met different opponent versions")
    if only:
        lines.append("note: " + ", ".join(only))
    for title, group, order in (("opponent", lambda k: k[0], None), ("map size", lambda k: size_of(k[1]), SIZES)):
        groups = sorted({group(k) for k in common}, key=lambda g: (order.index(g) if order else 0, g))
        if len(groups) > 1 or title == "opponent":
            lines.append(f"by {title}:")
            for g in groups:
                sub = paired({k: p for k, p in ka.items() if group(k) == g}, {k: p for k, p in kb.items() if group(k) == g})
                lines.append(f"  {g}: {format_paired(sub)}")
    for tag, t in (("A", ta), ("B", tb)):
        lines.append(f"health {tag}: faults {t['faults']} exhausted {t['exhausted']} errors {t['errors']} "
                     f"stalls {t['stalls']} suspended {t['suspended']}  fuel max {t['fuel_max']}")
    if pr.get("n"):
        for sign, ks in (("+", pr["gained"]), ("-", pr["lost"])):
            if ks:
                shown = "; ".join(key_text(k) for k in ks[:8]) + (f"; ... {len(ks) - 8} more" if len(ks) > 8 else "")
                lines.append(f"{'gained' if sign == '+' else 'lost'} {len(ks)}: {shown}")
    return lines, pr


def _cmd_compare(args) -> int:
    rows_a, path_a = load_rows(args.run_a)
    rows_b, path_b = load_rows(args.run_b)
    lines, pr = compare(rows_a, rows_b, args.subject, args.variant_a, args.variant_b)
    print(f"A {_rel(path_a)}\nB {_rel(path_b)}")
    print("\n".join(lines))
    return 0 if pr.get("n") else 1


# ----------------------------------------------------------------------------- registration
def _sides(s: str) -> str:
    return "both" if s.lower() == "both" else s.upper()


def register(sub) -> None:
    p = sub.add_parser("sweep", help="knob variants vs a same-sweep baseline: paired flips, McNemar, margins")
    p.add_argument("bot", help="a selector matching one bot")
    p.add_argument("--var", action="append", default=[], metavar='"NAME: k=v ..."',
                   help="a variant: knob overrides of BOT (repeatable)")
    p.add_argument("--vs", default=DEFAULT_VS, help=f"opponent selectors and ladder:nearN (default {DEFAULT_VS})")
    p.add_argument("--maps", default="pool", help="map set: pool, legacy, labs, small, large or names (default pool)")
    p.add_argument("--sides", default="both", choices=["both", "A", "B"], type=_sides, help="sides BOT plays")
    p.add_argument("--seeds", type=int, default=0, help="perturbation seeds 1..N of --seed-knob (default none)")
    p.add_argument("--seed-knob", default="noise_burn", help="the seed knob (default noise_burn)")
    p.add_argument("--confirm", action="store_true", help="replay base and the best variant on the legacy maps")
    p.add_argument("--jobs", type=int, default=0, help="machine slots to use (default: all free)")
    p.add_argument("--rounds", type=int, default=0, help="stop games after N rounds (no statistics; smoke runs)")
    p.add_argument("--label", default="", help="names the output dir (default the bot id)")
    p.add_argument("--out", default="", help="output dir (default .cache/arena/sweeps/<time>-<label>/)")
    p.add_argument("--keep-replays", default="flagged", choices=KEEP, help="replays to keep (default flagged)")
    add_resume_args(p)
    p.set_defaults(func=_cmd_sweep)
    p = sub.add_parser("compare", help="paired comparison of two runs (B relative to A) on identical game keys")
    p.add_argument("run_a", help="run directory (games.jsonl) or .jsonl log")
    p.add_argument("run_b", help="run directory (games.jsonl) or .jsonl log")
    p.add_argument("--variant-a", default=None, help="only rows of this sweep variant in RUN_A")
    p.add_argument("--variant-b", default=None, help="only rows of this sweep variant in RUN_B")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--subject-side-from-tags", action="store_true",
                   help="the subject is the side in tags.subject (the default)")
    g.add_argument("--subject", default=None, help="A|B, or an entrant key, bot id or name (globs) for untagged rows")
    p.set_defaults(func=_cmd_compare)
