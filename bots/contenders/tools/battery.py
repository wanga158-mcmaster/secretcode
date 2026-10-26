"""Standard evaluation of one bot for the contenders suite (a contender, or a main anchor as the
reference): health, the economy speedrun, a pinned strategy set reported worst case first,
fundamentals per opponent, O1 coverage of the set, and a paired comparison with an earlier battery.

  python -B -I -X utf8 bots/contenders/tools/battery.py BOT [--quick] [--set k=v ...]
         [--seeds N --seed-knob NAME] [--out DIR] [--against PREVIOUS_DIR] [--jobs N]
         [--max-minutes M] [--no-fundamentals] [--keep-stats] [--maps SET] [--only LABELS]
  python -B -I -X utf8 bots/contenders/tools/battery.py --render-only --out DIR [--against PREV]
  python -B -I -X utf8 bots/contenders/tools/battery.py --show                (the pinned set)
  python -B -I -X utf8 bots/contenders/tools/battery.py --pin [--refresh]     (pin new entries)

The report follows the suite's doctrine (README "Domination, not adjustment"): opponent-
independent measures first, then the worst case over a broad set of strategies, averages last.
  1. Health: faults, fuel exhaustion, error lines, stalls and suspended turns of the subject in
     every game (full: all 40 pool maps x both sides against each opponent, so the health games
     against anchors/starter-cpp and anchors/fundamentals are a subset). Any problem blocks a
     release; opponents' problems are listed apart because they change what a result means.
  2. Speedrun: rounds to 1,000 banked gems against the passive starter, by map and mean (a loss or
     draw counts 2,000), exactly as bots/main/tools/speedrun.py (main measured 994).
  3. Strategy set, worst first: per opponent W-L-D, score (W + D/2)/N with a 95% Wilson interval,
     mean gem margin +- se, and the subject's fundamentals in those games.
  4. Fundamentals (fundamentals.py; online.md's definitions) pooled and by opponent group, beside
     online.md's reference rows (our v35 online, all our versions, T1, T2).
  5. O1 coverage: the foundation's style flags (o1flags.py, a copy of style.py) measured on the
     opponents' sides; an O1 kind counts as covered when some opponent shows it in >= 50% of its
     games, weakly at 20-50%. Kinds without a foundation flag are listed as unmeasured.
  6. --against: arena's paired comparison (sweep.compare: flips, exact McNemar, paired gem margin)
     on identical (opponent, map, side, seed) keys of both batteries, opponents without flips
     folded away, then both runs' fundamentals on the keys whose opponent snapshot is the same.
The printed report (~60-80 lines) is also saved as report.txt; battery.json in the run directory
holds everything (tallies, speedrun by map, fundamentals per opponent, flags, pins, notes). After
decoding, each game's statistics JSON (~1 MB) is gzipped beside its replay unless --keep-stats.
Rerunning the same command with the same --out reuses every finished game (games against a
contender whose version changed since are new games and are played); --render-only rebuilds the
report from the games already in --out without playing any.

The strategy set (battery_set.json; --show prints it). Every entry is a bot, optionally with a knob
set (a persona, applied like gauntlet.py's through arena's apply_overrides/prepare_many), pinned
either "frozen" (an anchor: its source never changes) or by "snapshot" (the entrant's own arena
snapshot .cache/arena/snap/<version>, overrides included, built at --pin; every battery run touches
it, so arena prune, 14 days unused, keeps it; a missing snapshot is played at the current version
and flagged UNPINNED). --pin adds entries that are not pinned yet and keeps the others, so the
yardstick only moves with --pin --refresh. The entries:
  main family  anchors/main-v36 (live since 22 Oct 04:05Z, `arena release`), main-v35 (named live
               when this battery was specified; the reference subject), main-v28 (live 21 Oct, long
               the lineage anchor) and main-hunter7 (the hunting line, ~16 kills a ladder game).
  personas     bots/main/tools/gauntlet.py's eight archetypes (hunter, rush, squads, burst, meta,
               brawl, willow, hoarder) as knob sets applied to the frozen live anchor main-v36,
               not to main/main's working source: the same archetypes, pinned forever. At pin time
               main/main was v36 plus D100 (target choice charges camp-fight health, on) and
               off-by-default experiments (D98, D99, D101), so the personas lack only D100. The knob
               values are copied into the set file; a later change of gauntlet.py is reported, not
               followed.
  archetypes   hook for contenders/_archetypes (scratch, main-v35's chassis, a `persona` knob whose
               values embody O1 behaviours no local bot covers: Prism battery, jackpot control,
               spawn siege, lock harassment, invasion, Willow sustain, crystal routing, hoarding,
               turtling). Not fielded yet: fill ARCHETYPES (name -> knob set) and ARCHETYPE_WHY
               from its NOTES.md, then --pin; each becomes "archetype:<name>", snapshot-pinned.
               Any other bot's personas are added the same way through PERSONA_SOURCES.
  ladder       the strongest non-main bots of distinct suites in `arena ladder table --top 40`
               (22 Oct 05:10Z), chosen for distinct styles (`arena ladder styles`: no two nearer
               than 0.31) and for online.md O1 behaviours a contender must answer:
               specialists/porcupine (#8, 1371: defensive Ren, fights only from superiority,
               Health/Focus), specialists/greenhouse (#9, 1362: income first, Treasure Strength;
               the economic race), specialists/gremlins (1102: altar locks on enemy tributes, lock
               harassment), commitments/bounty-syndicate (#13, 1191: intercepts loaded carriers),
               heuristics/reset-pursuit (#14, 1188: kill/reset pursuit, the most aggressive clean
               bot) and territory/frontier (#29, 1011: forward territorial pressure). Not taken:
               conveyor (style 0.44 from greenhouse, same B0 economy), distributed/contingencies
               (#40, 962, below the starter: a passive turtle the starter already covers).
  contenders   the other contenders at their current versions (in development: not pinned; keys
               recorded, and --against reports pairs that met other versions).
  health       anchors/starter-cpp (passive; the speedrun opponent) and anchors/fundamentals (B0).
  A subject never plays its own exact snapshot (mirror games are identical from both sides).
`arena population panel/regress` cannot pin this set: it refuses main, anchors, heuristics and
contenders as subjects and as opponents.

--quick: 10 maps (touch 00gem-fields, 00reverse-spiral, 00swirl; gem-rich 00lost-world,
  01the-farm; far 00ambush, 01starvation; mid 00collision, 00contest, 01islands) x both sides
  against starter, fundamentals, main-v36, main-v35, main-hunter7, personas hunter/rush/squads,
  porcupine, reset-pursuit, the archetypes once fielded, and the contenders. The header says
  QUICK: a subset for fast iteration; only identical keys pair with a full run. --maps/--only
  make a CUSTOM run.
--seeds N adds --seed-knob=s (s = 1..N) to the subject as separate snapshots (opponents unchanged);
  the seed is part of the game key. Runs land in .cache/contenders/battery/<time>-<bot>/ and
  resume: rerun the same command with the same --out to finish an interrupted battery.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import statistics
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402
from core import ArenaError, Bot  # noqa: E402
from games import GameSpec, Incomplete, deadline_from, health, perspective, play  # noqa: E402
from sweep import compare, finished, tally  # noqa: E402

import fundamentals as fu  # noqa: E402
import o1flags as o1  # noqa: E402
import onlinedefs as od  # noqa: E402

SET_FILE = HERE / "battery_set.json"
OUT_ROOT = ROOT / ".cache" / "contenders" / "battery"
MAX_ROUNDS = 2000
SPEEDRUN_OPP = "anchors/starter-cpp"
HEALTH = [("anchors/starter-cpp", "official starter: passive, the speedrun opponent"),
          ("anchors/fundamentals", "B0: the economy most suites build on")]
MAIN_FAMILY = [("anchors/main-v36", "live since 22 Oct 04:05Z (arena release)"),
               ("anchors/main-v35", "named live when the battery was specified; the reference subject"),
               ("anchors/main-v28", "live 21 Oct; long the lineage anchor"),
               ("anchors/main-hunter7", "hunting line: ~16 kills a ladder game")]
PERSONA_BASE = "anchors/main-v36"
PERSONA_WHY = {"hunter": "hunts enemies within 20 (early aggression, chases)",
               "rush": "gems valued 8x, small bags (gem rush)", "squads": "squads of 3 (grouped play)",
               "burst": "Focus builds, burst draft (scaling burst)", "meta": "online meta draft and Health-first shop",
               "brawl": "grouped Focus brawlers that hunt (IF-like deathball)",
               "willow": "Willow sustain draft in pairs", "hoarder": "120-gem bags (big bags, bounty carriers)"}
# Hook: the personas of contenders/_archetypes (see the module docstring). Fill from its NOTES.md,
# e.g. ARCHETYPES = {"prism": {"persona": "1"}, ...} and ARCHETYPE_WHY = {"prism": "Prism battery (O17)"},
# then run --pin (existing pins stay). QUICK_ARCHETYPES names those also fielded in --quick.
ARCHETYPE_BOT = "contenders/_archetypes"
ARCHETYPES: dict[str, dict[str, str]] = {}
ARCHETYPE_WHY: dict[str, str] = {}
QUICK_ARCHETYPES: list[str] = []
LADDER = [("specialists/porcupine", "#8 1371: defensive Ren, fights only from superiority, Health/Focus build"),
          ("specialists/greenhouse", "#9 1362: income first (Treasure Strength), the economic race"),
          ("specialists/gremlins", "1102: altar locks on visible enemy tributes (lock harassment)"),
          ("commitments/bounty-syndicate", "#13 1191: Vesper and Varg intercept loaded carriers"),
          ("heuristics/reset-pursuit", "#14 1188: kill/reset pursuit, the most aggressive clean bot"),
          ("territory/frontier", "#29 1011: forward territorial pressure, many deaths")]
CONTENDERS = ["contenders/timetable", "contenders/warband", "contenders/compound"]
QUICK_MAPS = ["00gem-fields", "00reverse-spiral", "00swirl", "00lost-world", "01the-farm", "00ambush",
              "01starvation", "00collision", "00contest", "01islands"]
QUICK_OPPS = ["anchors/starter-cpp", "anchors/fundamentals", "anchors/main-v36", "anchors/main-v35",
              "anchors/main-hunter7", "persona:hunter", "persona:rush", "persona:squads",
              "specialists/porcupine", "heuristics/reset-pursuit"]
KIND_ORDER = ("health", "main", "persona", "archetype", "ladder", "contender")
GROUP_NAME = {"health": "starter+B0", "main": "main family", "persona": "personas", "archetype": "archetypes",
              "ladder": "ladder", "contender": "contenders"}


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _rel(p: Path) -> str:
    p = Path(p).resolve()
    return p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p)


# ----------------------------------------------------------------------------- pinning
def _gauntlet_personas() -> dict:
    sys.path.insert(0, str(ROOT / "bots" / "main" / "tools"))
    from gauntlet import PERSONAS  # read-only: main's archetype knob sets
    return {k: dict(v) for k, v in PERSONAS.items()}


def persona_sources() -> list[tuple[str, str, dict, dict, str]]:
    """(label prefix, base bot, {name: knob set}, {name: why}, kind) of every persona family."""
    out = [("persona", PERSONA_BASE, _gauntlet_personas(), PERSONA_WHY, "persona")]
    if ARCHETYPES:
        out.append(("archetype", ARCHETYPE_BOT, ARCHETYPES, ARCHETYPE_WHY, "archetype"))
    return out


def snapshot_contents(version: str, b: Bot, overrides: dict | None = None) -> dict | None:
    """The build closure held in arena's snapshot .cache/arena/snap/<version> of `b` with exactly
    `overrides` applied (None when gone or another entrant), keyed like core.read_closure so
    core.prepare(b, overrides, contents=...) rebuilds the same version (reapplying a knob value
    that is already there changes nothing)."""
    snap = core.CACHE / "snap" / version
    try:
        meta = json.loads((snap / "meta.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    want = {k: str(v) for k, v in (overrides or {}).items()}
    if meta.get("bot") != b.id or (meta.get("overrides") or {}) != want:
        return None
    src = snap / b.path.relative_to(ROOT) / "src"
    files = {p.resolve() for p in src.rglob("*") if p.is_file() and ".cambc" not in p.parts}
    stack = [p for p in files if p.suffix in core.SOURCE_SUFFIXES]
    base = snap.resolve()
    while stack:
        f = stack.pop()
        for inc in core.INCLUDE.findall(f.read_text(encoding="utf-8", errors="surrogateescape")):
            cand = (f.parent / inc).resolve()
            if cand.is_file() and cand not in files and cand.is_relative_to(base) and "sdk" not in cand.relative_to(base).parts[:1]:
                files.add(cand)
                if cand.suffix in core.SOURCE_SUFFIXES:
                    stack.append(cand)
    contents = {ROOT / p.relative_to(base): p.read_bytes() for p in sorted(files)}
    return contents if contents and core.version_of(contents, want or None) == version else None


def desired_entries() -> list[dict]:
    """The set's entries without versions: label, bot, kind, overrides, why."""
    out = [{"label": bid, "bot": bid, "kind": kind, "why": why}
           for kind, items in (("health", HEALTH), ("main", MAIN_FAMILY)) for bid, why in items]
    for prefix, base, personas, why, kind in persona_sources():
        out += [{"label": f"{prefix}:{n}", "bot": base, "kind": kind, "overrides": dict(ov), "why": why.get(n, "")}
                for n, ov in personas.items()]
    out += [{"label": bid, "bot": bid, "kind": "ladder", "why": why} for bid, why in LADDER]
    return out


def make_pins(existing: dict | None = None, refresh: bool = False) -> tuple[dict, list[str]]:
    """Pin every desired entry: kept as pinned unless `refresh` or its definition changed; new
    ones at today's version (non-frozen ones built now, so their snapshot exists)."""
    old = {e["label"]: e for e in (existing or {}).get("entries", [])}
    notes, entries, build = [], [], []
    for d in desired_entries():
        b = core.bot(d["bot"])
        o = old.get(d["label"])
        if o and not refresh and o["bot"] == d["bot"] and (o.get("overrides") or {}) == (d.get("overrides") or {}):
            entries.append(dict(o, why=d["why"], pin=o.get("pin") or ("frozen" if b.frozen else "snapshot")))
            continue
        e = dict(d, pin="frozen" if b.frozen else "snapshot")
        if b.frozen:
            raw = core.read_closure(b)
            ov = d.get("overrides") or None
            e["version"] = core.version_of(core.apply_overrides(b, raw, ov) if ov else raw, ov)
        else:
            build.append((len(entries), b, d.get("overrides") or None, d["label"]))
        entries.append(e)
        notes.append(f"{'re-pinned' if o else 'pinned'} {d['label']}")
    if build:
        ents = core.prepare_many([(b, ov, lab) for _, b, ov, lab in build])
        for (i, b, ov, lab), ent in zip(build, ents):
            if not ent.ok:
                raise ArenaError(f"cannot pin {lab}: build of {ent.key} failed")
            entries[i]["version"] = ent.version
    dropped = sorted(set(old) - {e["label"] for e in entries})
    notes += [f"dropped {x} (no longer in the set's definition)" for x in dropped]
    engine = json.loads((ROOT / "docs/reference/current-version.json").read_text(encoding="utf-8"))["engine"]["version"]
    base = core.bot(PERSONA_BASE)
    quick = list(QUICK_OPPS) + [f"archetype:{n}" for n in QUICK_ARCHETYPES if n in ARCHETYPES]
    pins = {"pinned": (existing or {}).get("pinned") if existing and not refresh and not notes else _now(),
            "engine": engine, "persona_base": f"{PERSONA_BASE}@{core.version(base)}", "entries": entries,
            "contenders": CONTENDERS, "quick": {"maps": QUICK_MAPS, "opponents": quick}}
    return pins, notes


def load_set() -> dict:
    if not SET_FILE.is_file():
        raise ArenaError(f"{_rel(SET_FILE)} is missing: run battery.py --pin")
    return json.loads(SET_FILE.read_text(encoding="utf-8"))


def resolve(pins: dict, labels: set | None) -> tuple[list[tuple[dict, core.Entrant]], list[str]]:
    """Built entrants of the pinned entries (optionally only `labels`), plus notes."""
    notes, items, chosen = [], [], []
    for e in pins["entries"]:
        if labels is not None and e["label"] not in labels:
            continue
        b = core.bot(e["bot"])
        ov = e.get("overrides") or None
        contents = None
        if (e.get("pin") or ("frozen" if b.frozen else "snapshot")) == "snapshot":
            contents = snapshot_contents(e["version"], b, ov)
            if contents is None:
                notes.append(f"UNPINNED {e['label']}: snapshot {e['version']} is gone; played the current version")
        items.append((b, ov, e["label"], contents) if contents is not None else (b, ov, e["label"]))
        chosen.append(e)
    ents = core.prepare_many(items)
    for e, ent in zip(chosen, ents):
        if ent.version != e["version"] and not any(n.startswith(f"UNPINNED {e['label']}") for n in notes):
            notes.append(f"UNPINNED {e['label']}: built {ent.version}, pinned {e['version']}")
        if not ent.ok:
            notes.append(f"FAILED build of {e['label']} ({ent.key}): its games are skipped")
    try:
        held = {e["label"][8:]: e.get("overrides") for e in pins["entries"] if e["kind"] == "persona"}
        if _gauntlet_personas() != held:
            notes.append("gauntlet.py's personas changed since pinning (the pinned knob sets are played)")
    except Exception:  # noqa: BLE001  (main's tool missing: the pinned values stand)
        pass
    if not ARCHETYPES:
        notes.append(f"archetypes not fielded: {ARCHETYPE_BOT}'s personas are not in battery.py yet (hook ARCHETYPES)")
    return list(zip(chosen, ents)), notes


# ----------------------------------------------------------------------------- measures
def speedrun(rows: list[dict]) -> dict:
    """bots/main/tools/speedrun.py: {(map, side): rounds if won else MAX_ROUNDS} against the starter."""
    games = {}
    for r in rows:
        t = r.get("tags") or {}
        side = t.get("subject")
        if t.get("opponent") != SPEEDRUN_OPP or side not in ("A", "B") or r.get("winner") in ("error", "skipped"):
            continue
        games[(r["map"], side, t.get("seed") or 0)] = r["rounds"] if r.get("winner") == side else MAX_ROUNDS
    if not games:
        return {"n": 0}
    mi = od.map_info()  # keyed by pool name (underscores); arena map names are file stems (hyphens)
    by_map, by_cls = {}, {}
    for (m, side, s), v in games.items():
        by_map.setdefault(m, []).append(v)
    for m, v in by_map.items():
        cls = (mi.get(m) or mi.get(m.replace("-", "_")) or {}).get("cls", "?")
        by_cls.setdefault(cls, []).extend(v)
    vals = list(games.values())
    return {"n": len(vals), "mean": statistics.mean(vals), "median": statistics.median(vals),
            "unfinished": sum(v >= MAX_ROUNDS for v in vals),
            "by_map": {m: statistics.mean(v) for m, v in sorted(by_map.items())},
            "by_class": {c: statistics.mean(v) for c, v in sorted(by_cls.items())}}


def margin_se(ps: list[dict]) -> float | None:
    m = [p["gems"] - p["opp_gems"] for p in ps if finished(p)]
    return statistics.stdev(m) / math.sqrt(len(m)) if len(m) > 1 else None


def opponent_health(rows: list[dict]) -> dict:
    """label -> {problem: count} over the opponents' sides."""
    out: dict = {}
    for r in rows:
        t = r.get("tags") or {}
        side = t.get("subject")
        if side not in ("A", "B"):
            continue
        other = "B" if side == "A" else "A"
        for h in health(r, other):
            k, _, n = h.partition(" ")
            d = out.setdefault(t.get("opponent"), {})
            d[k] = d.get(k, 0) + (int(n) if n.isdigit() else 1)
    return out


def subject_health_lines(rows: list[dict], limit: int = 5) -> list[str]:
    out = []
    for r in rows:
        p = perspective(r)
        if p is None:
            continue
        where = f"{r.get('map')} {p['side']} vs {(r.get('tags') or {}).get('opponent')}"
        if p["result"] in ("error", "skipped"):
            out.append(f"  {where}: {p['result']}: {str(r.get('error') or '').strip().splitlines()[-1:]}")
        elif health(r, p["side"]):
            out.append(f"  {where}: {', '.join(health(r, p['side']))}  {r.get('replay') or ''}")
    return out[:limit] + ([f"  ... {len(out) - limit} more in battery.json"] if len(out) > limit else [])


def flag_prevalence(subject: list[dict], opponents: list[dict]) -> tuple[dict, dict]:
    """O1 flags of every side: ({opponent label: {flag: (shown, measured)}}, {flag: (shown, measured)}
    of the subject). The map's median first contact comes from both sides of the games held."""
    games = collections.defaultdict(list)
    for b in subject + opponents:
        games[(b["rec"]["map"], b["replay"])].append(b["rec"])
    mc = o1.map_contacts(games)

    def count(bs):
        c = collections.defaultdict(lambda: [0, 0])
        for b in bs:
            for k, v in o1.flags_of(b["rec"], b.get("tf"), mc.get(b["rec"]["map"])).items():
                if v is not None:
                    c[k][0] += bool(v)
                    c[k][1] += 1
        return {k: tuple(v) for k, v in c.items()}
    by = collections.defaultdict(list)
    for b in opponents:
        by[b["opponent"]].append(b)
    return {lab: count(v) for lab, v in by.items()}, count(subject)


def coverage(prev: dict) -> dict:
    """O1 narrative kind -> {"status": covered|weak|absent|unmeasured, "by": [(label, share)]}."""
    out = {}
    for kind, flags in o1.NARRATIVE.items():
        if not flags:
            out[kind] = {"status": "unmeasured", "by": []}
            continue
        by = []
        for lab, c in prev.items():
            share = max((c[f][0] / c[f][1] for f in flags if f in c and c[f][1]), default=0.0)
            if share >= 0.2:
                by.append((lab, share))
        by.sort(key=lambda x: -x[1])
        best = by[0][1] if by else 0.0
        out[kind] = {"status": "covered" if best >= 0.5 else "weak" if best >= 0.2 else "absent", "by": by}
    return out


def _short_label(label: str) -> str:
    return label.replace("anchors/", "").replace("contenders/", "").replace("specialists/", "spec/")


class _Key:
    """Stand-in for an Entrant when rendering finished games (--render-only)."""

    def __init__(self, key: str):
        self.key = key


# ----------------------------------------------------------------------------- command
def main() -> int:
    p = argparse.ArgumentParser(description="Standard evaluation of one bot: health, speedrun, the pinned strategy set "
                                            "worst first, fundamentals, O1 coverage, --against (details: the module docstring).")
    p.add_argument("bot", nargs="?", help="the subject (a selector matching one bot)")
    p.add_argument("--quick", action="store_true", help="the fast subset (10 maps, 10 opponents + contenders)")
    p.add_argument("--set", dest="sets", action="append", default=[], metavar="K=V", help="knob override of the subject")
    p.add_argument("--seeds", type=int, default=0, help="perturbation seeds 1..N of --seed-knob for the subject")
    p.add_argument("--seed-knob", default="noise_burn")
    p.add_argument("--out", default="", help="run directory (default .cache/contenders/battery/<time>-<bot>/)")
    p.add_argument("--against", default="", help="an earlier battery directory: paired comparison on identical keys")
    p.add_argument("--jobs", type=int, default=0, help="machine slots for games (default: all free)")
    p.add_argument("--max-minutes", type=float, default=0, help="stop starting games after M minutes (exit 3; rerun to resume)")
    p.add_argument("--no-fundamentals", action="store_true", help="skip replay decoding (health, speedrun, scores only)")
    p.add_argument("--keep-stats", action="store_true",
                   help="keep each game's statistics JSON plain (default: gzip it after decoding, ~10x smaller)")
    p.add_argument("--maps", default="", help="CUSTOM: this map set instead of the mode's")
    p.add_argument("--only", default="", help="CUSTOM: only these opponent labels (comma-separated)")
    p.add_argument("--workers", type=int, default=6, help="parallel replay decodes for fundamentals")
    p.add_argument("--render-only", action="store_true", help="report the games already in --out; play nothing")
    p.add_argument("--show", action="store_true", help="print the pinned strategy set and exit")
    p.add_argument("--pin", action="store_true", help="pin entries not pinned yet (keep the others) and exit")
    p.add_argument("--refresh", action="store_true", help="with --pin: re-pin every entry to today's versions")
    a = p.parse_args()
    if a.pin:
        existing = load_set() if SET_FILE.is_file() else None
        pins, notes = make_pins(existing, a.refresh)
        SET_FILE.write_text(json.dumps(pins, indent=1) + "\n", encoding="utf-8", newline="\n")
        print(f"{len(pins['entries'])} entries in {_rel(SET_FILE)}" + (": " + "; ".join(notes) if notes else ": unchanged"))
        return 0
    pins = load_set()
    set_sha = hashlib.sha256(SET_FILE.read_bytes()).hexdigest()[:8]
    if a.show:
        print(f"strategy set pinned {pins['pinned']} (engine {pins['engine']}; personas on {pins['persona_base']}; sha {set_sha})")
        for e in pins["entries"]:
            ov = " ".join(f"{k}={v}" for k, v in (e.get("overrides") or {}).items())
            print(f"  {e['kind']:9s} {e['label']:30s} {e['version']} {e.get('pin', '?'):8s} {e['why']}" + (f"  [{ov}]" if ov else ""))
        print(f"  contender {', '.join(pins['contenders'])} (current versions)")
        print(f"  quick: {len(pins['quick']['maps'])} maps, opponents {', '.join(pins['quick']['opponents'])} + contenders")
        if not ARCHETYPES:
            print(f"  archetypes: not fielded ({ARCHETYPE_BOT}: fill ARCHETYPES in battery.py from its NOTES.md, then --pin)")
        return 0
    if a.render_only:
        return render_only(a, pins, set_sha)
    if not a.bot:
        p.error("give the subject BOT (or --show / --pin / --render-only --out DIR)")
    subject_bot = core.bot(a.bot)
    overrides = core.parse_sets(a.sets)
    mode = "QUICK" if a.quick else "FULL"
    if a.maps or a.only:
        mode = "CUSTOM"
    maps = core.map_set(a.maps) if a.maps else (list(pins["quick"]["maps"]) if a.quick else core.pool_maps())
    labels = None
    if a.quick:
        labels = set(pins["quick"]["opponents"])
    if a.only:
        labels = {x.strip() for x in a.only.split(",") if x.strip()}
    # entrants: the subject (one per seed), the pinned set, the other contenders
    seeds = list(range(1, a.seeds + 1)) if a.seeds > 0 else [0]
    contents = core.read_closure(subject_bot)
    label = subject_bot.id + (" " + " ".join(f"{k}={v}" for k, v in overrides.items()) if overrides else "")
    subj = core.prepare_many([(subject_bot, dict(overrides, **({a.seed_knob: str(s)} if s else {})) or None,
                               label, contents) for s in seeds])
    if not all(e.ok for e in subj):
        bad = next(e for e in subj if not e.ok)
        print(f"FAILED build of the subject {bad.key}\n" + "\n".join(bad.log.strip().splitlines()[-12:]))
        return 1
    chosen, notes = resolve(pins, labels)
    opps = [(e["label"], e["kind"], ent) for e, ent in chosen]
    others = [c for c in pins["contenders"] if c != subject_bot.id and (labels is None or a.quick or c in labels)]
    if others:
        cents = core.prepare_many([(core.bot(c), None, c) for c in others])
        opps += [(c, "contender", e) for c, e in zip(others, cents)]
        notes.append("contenders at current versions: " + ", ".join(e.key.split("/", 1)[1] for e in cents))
        notes += [f"FAILED build of {e.key}: its games are skipped" for e in cents if not e.ok]
    keys = {e.key for e in subj}
    for lab, kind, ent in list(opps):
        if ent.key in keys:
            opps.remove((lab, kind, ent))
            notes.append(f"skipped {lab}: it is the subject's own snapshot (mirror)")
    sides = ["A", "B"]
    specs = []
    for lab, kind, o in opps:
        for m in maps:
            for side in sides:
                for s, e in zip(seeds, subj):
                    x, y = (e, o) if side == "A" else (o, e)
                    specs.append(GameSpec(x, y, m, {"subject": side, "opponent": lab, "seed": s, "phase": "battery",
                                                    "group": kind}))
    if a.out:
        out = Path(a.out).resolve()
    else:
        out = OUT_ROOT / f"{time.strftime('%Y%m%d-%H%M%S')}-{subject_bot.id.replace('/', '-')}"
    out.mkdir(parents=True, exist_ok=True)
    print(f"battery {subj[0].key}: {mode}, {len(opps)} opponents x {len(maps)} maps x 2 sides x {len(seeds)} seed(s) = "
          f"{len(specs)} games -> {_rel(out)}", flush=True)
    try:
        rows = play(specs, out, jobs=a.jobs, keep_replays="all", resume=True, deadline=deadline_from(a.max_minutes))
    except Incomplete as exc:
        print(str(exc))
        return 3
    return finish(a, rows, subj, opps, maps, seeds, mode, pins, set_sha, notes, out)


def finish(a, rows, subj, opps, maps, seeds, mode, pins, set_sha, notes, out) -> int:
    lines, result = render(a, rows, subj, opps, maps, seeds, mode, pins, set_sha, notes, out)
    text = "\n".join(lines)
    print(text)
    (out / "report.txt").write_text(text + "\n", encoding="utf-8", newline="\n")
    (out / "battery.json").write_text(json.dumps(result, indent=1, default=str) + "\n", encoding="utf-8", newline="\n")
    return 0


def render_only(a, pins, set_sha) -> int:
    """Report the battery games already in --out (as recorded: subject, opponents, versions)."""
    if not a.out:
        raise ArenaError("--render-only needs --out DIR (a battery directory)")
    out = Path(a.out).resolve()
    rows = [r for r in fu._rows(out) if (r.get("tags") or {}).get("phase") == "battery"]
    if not rows:
        raise ArenaError(f"no battery games in {_rel(out)}")
    prev = {}
    try:
        prev = json.loads((out / "battery.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        pass
    subj_keys = collections.Counter(r[r["tags"]["subject"].lower()] for r in rows)
    opp_keys: dict = {}
    for r in rows:
        t = r["tags"]
        other = "b" if t["subject"] == "A" else "a"
        opp_keys.setdefault(t["opponent"], (t.get("group") or "?", collections.Counter()))[1][r[other]] += 1
    opps = [(lab, kind, _Key(c.most_common(1)[0][0])) for lab, (kind, c) in opp_keys.items()]
    multi = [lab for lab, (_, c) in opp_keys.items() if len(c) > 1]
    notes = list(prev.get("notes") or []) + [f"rendered again {_now()} from {len(rows)} recorded games"]
    if multi:
        notes.append("opponents met at several versions (pooled): " + ", ".join(multi))
    seeds = sorted({r["tags"].get("seed") or 0 for r in rows})
    maps = sorted({r["map"] for r in rows})
    subj = [_Key(k) for k, _ in subj_keys.most_common()]
    return finish(a, rows, subj, opps, maps, seeds, prev.get("mode") or "RENDERED", pins, set_sha, notes, out)


def render(a, rows, subj, opps, maps, seeds, mode, pins, set_sha, notes, out) -> tuple[list[str], dict]:
    ps = [x for x in (perspective(r) for r in rows) if x is not None]
    kind_of = {lab: kind for lab, kind, _ in opps}
    by_opp: dict[str, list[dict]] = {}
    for x in ps:
        by_opp.setdefault(str(x["tags"].get("opponent")), []).append(x)
    L = []
    seeds_txt = "unseeded" if seeds == [0] else f"seeds 1..{len(seeds)} of {a.seed_knob}"
    L.append(f"battery {subj[0].key}  {mode}{' (subset for fast iteration; not a full battery)' if mode == 'QUICK' else ''}"
             f"  {len(maps)} maps x 2 sides, {seeds_txt}; set pinned {pins['pinned']} (sha {set_sha})")
    t_all = tally(ps)
    L.append(f"  {len(rows)} games ({t_all['n']} finished{', ' + str(t_all['failed']) + ' failed' if t_all['failed'] else ''}"
             f"{', ' + str(t_all['truncated']) + ' truncated' if t_all['truncated'] else ''}), out {_rel(out)}")
    for n in notes:
        L.append(f"  note: {n}")
    # 1. health
    L.append("== 1. Health of the subject (any problem blocks a release)")
    hrows = []
    for lab in ["all"] + [x for x, k, _ in opps if k == "health"]:
        t = t_all if lab == "all" else tally(by_opp.get(lab, []))
        hrows.append([lab, str(t["games"]), str(t["faults"]), str(t["exhausted"]), str(t["errors"]), str(t["stalls"]),
                      str(t["suspended"]), str(t["fuel_max"]), str(t["failed"])])
    L += ["  " + x for x in fu.table(["games vs", "games", "faults", "exhausted", "errors", "stalls", "suspended",
                                      "fuel max", "failed"], hrows)]
    probs = subject_health_lines(rows)
    L += probs if probs else ["  clean in every game"]
    oh = opponent_health(rows)
    if oh:
        L.append(("  opponents' problems (their side; results still count): " + "; ".join(
            f"{_short_label(k)} " + " ".join(f"{p} {n}" for p, n in v.items()) for k, v in sorted(oh.items())))[:400])
    # 2. speedrun
    sr = speedrun(rows)
    L.append(f"== 2. Speedrun vs {SPEEDRUN_OPP} (rounds to 1000 banked gems; a loss or draw counts {MAX_ROUNDS})")
    if sr["n"]:
        L.append(f"  mean {sr['mean']:.1f}  median {sr['median']:.0f}  ({sr['n']} games, {sr['unfinished']} unfinished)"
                 "  by class: " + "  ".join(f"{c} {v:.0f}" for c, v in sr["by_class"].items()))
        worst = sorted(sr["by_map"].items(), key=lambda kv: -kv[1])
        L.append("  slowest: " + ", ".join(f"{m} {v:.0f}" for m, v in worst[:6]) + "; fastest: "
                 + ", ".join(f"{m} {v:.0f}" for m, v in worst[-3:]))
    else:
        L.append("  no games against the starter")
    # fundamentals per opponent (decoded replays), both sides
    funds, fcov, gagg, B, OB = {}, {}, {}, [], []
    if not a.no_fundamentals:
        B, fcov = fu.load_runs([out], workers=a.workers, quiet=True)
        OB, _ = fu.load_runs([out], side="opponent", workers=a.workers, quiet=True)  # same decoded games
        if not a.keep_stats:  # decoded games keep their replay and analysis; statistics shrink tenfold
            fcov["compacted_bytes"] = sum(fu.compact_stats(Path(b["replay"])) for b in B)
        B = [b for b in B if b["opponent"] in kind_of]
        OB = [b for b in OB if b["opponent"] in kind_of]
        for lab in kind_of:
            sel = [b for b in B if b["opponent"] == lab]
            if sel:
                funds[lab] = fu.aggregate(sel)
        groups = {"all games": B, "strategy set": [b for b in B if kind_of.get(b["opponent"]) != "health"]}
        for k in KIND_ORDER:
            sel = [b for b in B if kind_of.get(b["opponent"]) == k]
            if sel:
                groups[GROUP_NAME[k]] = sel
        gagg = {k: fu.aggregate(v) for k, v in groups.items() if v}
    # 3. strategy set, worst first
    L.append("== 3. Strategy set, worst first (score = (W + D/2)/N, 95% Wilson CI; margin = mean banked-gem margin +- se)")
    srows, res_opp = [], {}
    for lab, kind, ent in opps:
        t = tally(by_opp.get(lab, []))
        se = margin_se(by_opp.get(lab, []))
        res_opp[lab] = {"kind": kind, "entrant": ent.key, "tally": t, "margin_se": se, "fundamentals": funds.get(lab)}
    order = sorted(res_opp, key=lambda k: (res_opp[k]["tally"]["score"] if res_opp[k]["tally"]["n"] else 9,
                                           res_opp[k]["tally"]["margin"] or 0))
    for lab in order:
        r = res_opp[lab]
        t, f = r["tally"], r["fundamentals"]
        if not t["n"]:
            srows.append([_short_label(lab), r["kind"], "-", "-", "-", "-", "", "", ""])
            continue
        fc = [fu._f(f["rate"]["gold_per_alive_round"]), fu._f(f["rate"]["dead_rounds"], "{:.0f}"),
              fu._pct(f["episodes"]["p_death_fight"])] if f else ["", "", ""]
        srows.append([_short_label(lab), r["kind"], f"{t['w']}-{t['l']}-{t['d']}", f"{t['score']:.3f}",
                      f"{t['ci'][0]:.2f}-{t['ci'][1]:.2f}",
                      f"{t['margin']:+.0f}" + (f" +- {r['margin_se']:.0f}" if r["margin_se"] is not None else "")] + fc)
    L += ["  " + x for x in fu.table(["opponent", "kind", "W-L-D", "score", "95% CI", "gem margin", "gold400", "dead400",
                                      "P(d|f)"], srows)]
    setps = [x for x in ps if kind_of.get(str(x["tags"].get("opponent"))) != "health"]
    ts = tally(setps)
    if ts["n"] and order:
        w = order[0]
        L.append(f"  worst: {_short_label(w)} {res_opp[w]['tally']['score']:.3f}; strategy set pooled (no health games) "
                 f"{ts['w']}-{ts['l']}-{ts['d']} score {ts['score']:.3f} ({ts['ci'][0]:.2f}-{ts['ci'][1]:.2f}), margin {ts['margin']:+.0f}")
    # 4. fundamentals
    if not a.no_fundamentals and gagg:
        ref = fu.load_reference()
        L.append("== 4. Fundamentals of the subject (online.md definitions; fundamentals.py), local groups then online rows")
        L += ["  " + x for x in fu.report(gagg, ref)]
        if "strategy set" in gagg:
            L.append("  death classes, strategy set: " + fu.class_line(gagg["strategy set"]))
        if fcov.get("measured", 0) < t_all["n"]:
            L.append(f"  coverage: {fcov.get('measured', 0)} of {t_all['n']} finished games decoded ({fcov})"[:300])
    # 5. O1 coverage of the set (flags on the opponents' sides)
    prev, own, cov = {}, {}, {}
    if not a.no_fundamentals and OB:
        prev, own = flag_prevalence(B, OB)
        cov = coverage(prev)
        L.append("== 5. O1 coverage: foundation style flags on the opponents' sides (covered: an opponent shows it in"
                 " >= 50% of its games; weak: 20-50%)")
        for status in ("covered", "weak"):
            items = [f"{k} ({', '.join(f'{_short_label(l)} {100 * s:.0f}%' for l, s in v['by'][:2])})"
                     for k, v in cov.items() if v["status"] == status]
            if items:
                L.append(f"  {status}: " + "; ".join(items))
        absent = [k for k, v in cov.items() if v["status"] == "absent"]
        unmeasured = [k for k, v in cov.items() if v["status"] == "unmeasured"]
        L.append(f"  absent: {', '.join(absent) or 'none'}; no foundation flag: {', '.join(unmeasured)}")
        shown = [f"{k} {100 * n / m:.0f}%" for k, (n, m) in sorted(own.items()) if m and n / m >= 0.5]
        L.append("  the subject itself shows (>= 50% of its games): " + (", ".join(shown) or "none"))
    # 6. paired comparison
    cmp_lines, fpair = [], None
    if a.against:
        prevdir = Path(a.against)
        prevdir = prevdir if prevdir.is_absolute() else (ROOT / prevdir if (ROOT / prevdir).exists() else prevdir.resolve())
        try:
            rows_prev = [json.loads(x) for x in (prevdir / "games.jsonl").read_text(encoding="utf-8").splitlines() if x.strip()]
            cmp_lines, _ = compare([r for r in rows_prev if (r.get("tags") or {}).get("phase") == "battery"],
                                   [r for r in rows if (r.get("tags") or {}).get("phase") == "battery"])
        except (OSError, ValueError, ArenaError) as exc:
            cmp_lines = [f"cannot compare with {a.against}: {exc}"]
        L.append(f"== 6. Paired vs {_rel(prevdir)} (A = previous, B = this run; identical opponent, map, side, seed)")
        # opponents without flips are left to battery.json; every other line of arena's comparison is shown
        shown = [x for x in cmp_lines if not (x.startswith("  ") and "(+0/-0," in x)]
        hidden = len(cmp_lines) - len(shown)
        L += ["  " + x for x in shown[:24]] + ([f"  ... {len(shown) - 24} more lines in battery.json"] if len(shown) > 24 else [])
        if hidden:
            L.append(f"  ({hidden} opponents without flips not shown)")
        if not a.no_fundamentals and gagg:
            PB, _ = fu.load_runs([prevdir], workers=a.workers, quiet=True)
            old = {b["key"]: b for b in PB}
            cur = {b["key"]: b for b in B}
            common = sorted((k for k in set(old) & set(cur) if old[k]["opp_entrant"] == cur[k]["opp_entrant"]), key=str)
            if common:
                fa, fb = fu.aggregate([old[k] for k in common]), fu.aggregate([cur[k] for k in common])

                def cell(agg):
                    return (fu._f(agg["rate"]["gold_per_alive_round"]), fu._f(agg["rate"]["dead_rounds"], "{:.0f}"),
                            fu._pct(agg["episodes"]["p_death_fight"]), fu._f(agg["rate"]["gems_per_100"], "{:.1f}"))
                ca, cb = cell(fa), cell(fb)
                L.append(f"  fundamentals on the {len(common)} common keys with the same opponent snapshot, previous -> now: "
                         f"gold400 {ca[0]} -> {cb[0]}, dead400 {ca[1]} -> {cb[1]}, P(d|f) {ca[2]} -> {cb[2]}, "
                         f"gems/100 {ca[3]} -> {cb[3]}")
                fpair = {"keys": len(common), "previous": fa, "now": fb}
    result = {"subject": [e.key for e in subj], "overrides": core.parse_sets(a.sets), "seeds": seeds, "mode": mode,
              "maps": maps, "set_file": _rel(SET_FILE), "set_sha": set_sha, "set_pinned": pins["pinned"], "notes": notes,
              "games": len(rows), "tally_all": t_all, "tally_set": ts, "worst": order[0] if order else None,
              "opponents": res_opp, "speedrun": sr, "opponent_health": oh,
              "health_problems": subject_health_lines(rows, limit=10 ** 6),
              "fundamentals_groups": gagg if not a.no_fundamentals else None, "fundamentals_coverage": fcov,
              "o1_flags_opponents": prev, "o1_flags_subject": own, "o1_coverage": cov,
              "compare": cmp_lines, "compare_fundamentals": fpair, "created": _now()}
    return L, result


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ArenaError as exc:
        print(f"error: {exc}")
        sys.exit(2)
