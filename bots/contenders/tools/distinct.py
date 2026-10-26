"""Are the contenders (and main) still different bots? Fingerprints, distances, head-to-heads.

  python -B -I -X utf8 bots/contenders/tools/distinct.py RUN_DIR [RUN_DIR ...] [--json OUT]
  python -B -I -X utf8 bots/contenders/tools/distinct.py RUN_DIR ... --play BOTS [--maps quick|pool|SET]

RUN_DIRs are battery directories (battery.py), one per bot, or any arena runs with kept replays
whose rows tag a subject. Each run's subject is one bot; its fingerprint is measured from its
own side (fundamentals.py, online.md's definitions) on MATCHED conditions only: the (opponent,
map, side, seed) keys that every compared bot's runs contain, the opponent by exact snapshot (a
contender opponent that changed between batteries drops out), excluding games among the
compared bots. So two bots are compared on the same opponents, maps and sides.
Features per side (the "decisions" block; draft is compared apart):
  build     shares of Health / Strength / Focus shards bought by r400; Treasure-first share
  shop      first purchase (side median), shop sessions by r400 per champion, gold per session,
            emblems completed by r400, altar locks and wards by r400
  cohesion  grouped / alone shares and mean pairwise distance r1-300, party size when dealing
            damage, recalls by r400 per champion, share of r1-300 nearer the enemy spawn
  contact   first champion damage round, place of first contact (0 own spawn, 1 theirs),
            champion-rounds with an enemy within 6 by r400
  captures  captures by r400 and their shares by camp type (wolf, elk, golem+monolith, gem+mix, gold)
  banking   median tribute bag, tributes by r400
The "outcomes" block holds the fundamentals (gold per alive round r400, dead rounds r400,
collected gems per 100 alive rounds, kills and deaths by r400, deaths per fight).
Distance of two bots over a block: the root mean square over its features of d = (mean_X -
mean_Y) / s, where s is the feature's pooled within-bot standard deviation over sides (a Cohen's
d: differences measured against game-to-game variation, not against the spread of the few bots
compared); each |d| counts at most 3, so one feature cannot carry a distance alone. Draft overlap: the mean Jaccard similarity of the two bots' drafts on the same map and
side. Head-to-head: every game between two compared bots in any RUN_DIR (deduplicated by
entrants, map and side); --play first plays the missing pairs of BOTS (round robin, both sides,
default the battery's quick maps) into .cache/contenders/battery/distinct-<time>/.
A pair "looks converged" when its decision distance is below 0.30 and its head-to-head score is
35-65% or absent (the ladder's rule for styles: similar play and no clear winner); "close" below
0.50. Calibration (22 Oct, the opponents' sides of contenders/timetable's quick battery, 20
matched games): main-v36 / main-v35 0.00 (identical play), main-v35 / persona:rush 0.23, /squads
0.32, /hunter 0.53, / main-hunter7 0.61, specialists/porcupine / anchors/fundamentals (a B0 fork
with one commitment) 0.36, main-v35 / anchors/fundamentals 1.1 (different codebases). So
"converged" means as alike as two knob settings of one bot. The JSON holds every feature mean,
standard deviation and pairwise difference.
"""
from __future__ import annotations

import argparse
import collections
import itertools
import json
import math
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import fundamentals as fu  # noqa: E402
from onlinedefs import median  # noqa: E402

CONVERGED, CLOSE = 0.30, 0.50
CAP = 3.0  # largest |d| one feature contributes to a distance
CHAMPS = ["Ryth", "Vesper", "Vendetta", "C&I", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi", "Heart"]


def features(b: dict) -> dict:
    """Decision and outcome features of one side bundle (None when not measurable)."""
    r, tf = b["rec"], b.get("tf") or {}
    sh = fu.shard_share400(r)
    first_emb = [c["emblems"][0][1].split(" Emblem of ")[0] for c in r["champs"] if c["emblems"]]
    ss = [fu.sessions(c["buys"]) for c in r["champs"]]
    caps = [c for c in r["caps"] if c[0] <= 400]
    cat = collections.Counter(c[1] for c in caps)
    n = len(caps)
    bags = [t[1] for t in r["tributes"] if t[0] <= 400]
    alive = len(r["champs"]) * 400 - fu.dead_rounds(r, 400) if r["rounds"] >= 400 else None
    fights = [e for e in b["eps"] if (e["dmg_done"] + e["dmg_taken"]) > 0]
    coh = tf.get("coh300")
    dec = {
        "health400": sh["Health"], "strength400": sh["Strength"], "focus400": sh["Focus"],
        "treasure_first": (sum(1 for e in first_emb if e == "Treasure") / len(first_emb)) if first_emb else None,
        "first_buy": median([c["buys"][0][0] if c["buys"] else None for c in r["champs"]]),
        "sessions400": sum(sum(1 for s in x if s[0] <= 400) for x in ss) / max(1, len(ss)),
        "gold_per_session": median([s[1] for x in ss for s in x if s[0] <= 400]),
        "emblems400": sum(1 for c in r["champs"] for e in c["emblems"] if e[0] <= 400),
        "locks400": sum(nn for c in r["champs"] for (rr, nm, nn) in c["cons"] if nm == "Altar Lock" and rr <= 400),
        "wards400": r.get("wards400"),
        "grouped": coh[2] if coh else None, "alone": coh[0] if coh else None, "spread": tf.get("spread300"),
        "party": tf.get("party"), "recalls400": tf.get("recalls400"), "enemyhalf300": tf.get("enemyhalf300"),
        "first_contact": r.get("first_cdmg"), "contact_place": tf.get("contact_frac"), "near400": tf.get("near400"),
        "captures400": n,
        "cap_wolf": cat["wolf"] / n if n else None, "cap_elk": cat["elk"] / n if n else None,
        "cap_big": (cat["golem"] + cat["monolith"]) / n if n else None,
        "cap_gem": (cat["gem"] + cat["mix"]) / n if n else None, "cap_gold": cat["gold"] / n if n else None,
        "bag400": median(bags), "tributes400": len(bags),
    }
    out = {
        "gold400": (r["earnedGold"]["400"] / alive) if alive else None,
        "dead400": fu.dead_rounds(r, 400) if r["rounds"] >= 400 else None,
        "gems100": (100 * r["collectedGems"]["400"] / alive) if alive else None,
        "kills400": sum(1 for k in r["kills_list"] if k["r"] <= 400),
        "deaths400": sum(1 for d in r["deaths_list"] if d["r"] <= 400 and d["ctx"] == "champion"),
        "death_per_fight": (sum(1 for e in fights if e["end"] == "death") / len(fights)) if fights else None,
    }
    return {"decisions": dec, "outcomes": out}


def bot_of(key: str | None) -> str:
    return str(key or "?").split("@")[0]


def stats_by_bot(per_bot: dict) -> dict:
    """{block: {feature: {"mean": {bot: m}, "sd": pooled within-bot sd}}}."""
    out = {}
    for block in ("decisions", "outcomes"):
        names = sorted({k for fs in per_bot.values() for f in fs for k in f[block]})
        res = {}
        for k in names:
            means, ss, dof = {}, 0.0, 0
            for bot, fs in per_bot.items():
                xs = [f[block][k] for f in fs if f[block].get(k) is not None]
                if not xs:
                    continue
                m = sum(xs) / len(xs)
                means[bot] = m
                ss += sum((x - m) ** 2 for x in xs)
                dof += len(xs) - 1
            sd = math.sqrt(ss / dof) if dof > 0 else None
            res[k] = {"mean": means, "sd": sd}
        out[block] = res
    return out


def distance(st: dict, x: str, y: str) -> tuple[float | None, list[tuple[str, float]]]:
    """RMS standardized difference of bots x and y over one block's features, and the features
    sorted by |d| (largest first)."""
    ds = []
    for k, v in st.items():
        mx, my, sd = v["mean"].get(x), v["mean"].get(y), v["sd"]
        if mx is None or my is None or not sd:
            continue
        ds.append((k, (mx - my) / sd))
    if not ds:
        return None, []
    return (math.sqrt(sum(min(abs(d), CAP) ** 2 for _, d in ds) / len(ds)),
            sorted(ds, key=lambda kv: -abs(kv[1])))


def draft_overlap(bx: list[dict], by: list[dict]) -> float | None:
    dx = {(b["map"], b["side"]): set(b["rec"]["draft"]) for b in bx}
    dy = {(b["map"], b["side"]): set(b["rec"]["draft"]) for b in by}
    common = sorted(set(dx) & set(dy))
    if not common:
        return None
    return sum(len(dx[k] & dy[k]) / len(dx[k] | dy[k]) for k in common) / len(common)


def head_to_head(rows: list[dict], bots: list[str]) -> dict:
    """(x, y) -> {w, l, d, n, versions} from x's side, over deduplicated games between two compared bots."""
    seen, out = set(), {}
    for r in rows:
        if r.get("winner") not in ("A", "B", "draw") or r.get("truncated"):
            continue
        ba, bb = bot_of(r.get("a")), bot_of(r.get("b"))
        if ba not in bots or bb not in bots or ba == bb:
            continue
        key = (r.get("a"), r.get("b"), r.get("map"))
        if key in seen:
            continue
        seen.add(key)
        for me, opp, side in ((ba, bb, "A"), (bb, ba, "B")):
            if bots.index(me) > bots.index(opp):
                continue
            rec = out.setdefault((me, opp), {"w": 0, "l": 0, "d": 0, "n": 0, "versions": collections.Counter()})
            w = r["winner"]
            rec["w" if w == side else "d" if w == "draw" else "l"] += 1
            rec["n"] += 1
            mine, theirs = (r.get("a"), r.get("b")) if side == "A" else (r.get("b"), r.get("a"))
            rec["versions"][f"{str(mine).split('@')[-1]} vs {str(theirs).split('@')[-1]}"] += 1
    return out


def play_round_robin(bots: list[str], maps_spec: str) -> Path:
    import core
    from games import GameSpec, play
    import battery
    maps = battery.QUICK_MAPS if maps_spec == "quick" else core.map_set(maps_spec)
    ents = core.prepare_many([(core.bot(b), None, b) for b in bots])
    specs = []
    for (bx, ex), (by, ey) in itertools.combinations(list(zip(bots, ents)), 2):
        for m in maps:
            for side in ("A", "B"):
                a, b = (ex, ey) if side == "A" else (ey, ex)
                specs.append(GameSpec(a, b, m, {"subject": side, "opponent": by, "phase": "distinct"}))
    out = fu.CACHE / f"distinct-{time.strftime('%Y%m%d-%H%M%S')}"
    print(f"round robin of {len(bots)} bots on {len(maps)} maps x 2 sides: {len(specs)} games -> {out}", flush=True)
    play(specs, out, keep_replays="all", resume=True)
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("runs", nargs="*", help="battery or run directories, one subject bot each")
    p.add_argument("--play", default="", help="comma-separated bots: play their missing head-to-heads first")
    p.add_argument("--maps", default="quick", help="maps of the round robin (quick = the battery's quick maps)")
    p.add_argument("--json", default="")
    p.add_argument("--workers", type=int, default=6)
    a = p.parse_args()
    runs = [Path(x).resolve() for x in a.runs]
    extra_rows = []
    if a.play:
        rr = play_round_robin([x.strip() for x in a.play.split(",") if x.strip()], a.maps)
        extra_rows = fu._rows(rr)
    per_run, all_rows = {}, list(extra_rows)
    for run in runs:
        rows = fu._rows(run)
        all_rows += rows
        subj = collections.Counter(bot_of(r.get((r.get("tags") or {}).get("subject", "A").lower())) for r in rows
                                   if (r.get("tags") or {}).get("subject") in ("A", "B"))
        if not subj:
            print(f"skip {run}: no subject-tagged rows")
            continue
        bot = subj.most_common(1)[0][0]
        B, cov = fu.load_runs([run], workers=a.workers, quiet=True)
        B = [b for b in B if bot_of(b["entrant"]) == bot]
        per_run.setdefault(bot, []).extend(B)
    bots = list(per_run)
    if len(bots) < 2:
        print("need runs of at least two subject bots")
        return 1
    # matched keys: the same opponent snapshot (entrant key, so an unpinned opponent that changed
    # between two batteries does not match), map, side and seed for every bot; games among the
    # compared bots are head-to-heads, not matched conditions
    def mkey(b):
        return (b["opp_entrant"], b["map"], b["side"], b["seed"])
    keysets = [{mkey(b) for b in per_run[x] if b["opponent"] not in bots} for x in bots]
    common = set.intersection(*keysets)
    matched = {x: [b for b in per_run[x] if mkey(b) in common] for x in bots}
    label = {b["opp_entrant"]: b["opponent"] for x in bots for b in matched[x]}
    opps = sorted({label.get(k[0], k[0]) for k in common})
    print(f"bots: {', '.join(f'{x} ({len(matched[x])} matched sides)' for x in bots)}")
    print(f"matched conditions: {len(common)} keys = {len(opps)} opponents x {len({k[1] for k in common})} maps x sides"
          f" ({', '.join(o.replace('anchors/', '') for o in opps)})")
    if not common:
        print("no common keys: run the same battery mode for every bot")
        return 1
    per_bot = {x: [features(b) for b in matched[x]] for x in bots}
    st = stats_by_bot(per_bot)
    # fingerprint table
    cols = [("decisions", "health400", "{:.0%}"), ("decisions", "focus400", "{:.0%}"), ("decisions", "first_buy", "r{:.0f}"),
            ("decisions", "sessions400", "{:.1f}"), ("decisions", "grouped", "{:.2f}"), ("decisions", "alone", "{:.2f}"),
            ("decisions", "spread", "{:.1f}"), ("decisions", "first_contact", "r{:.0f}"), ("decisions", "captures400", "{:.1f}"),
            ("decisions", "cap_gem", "{:.0%}"), ("decisions", "bag400", "{:.0f}"), ("outcomes", "gold400", "{:.2f}"),
            ("outcomes", "dead400", "{:.0f}"), ("outcomes", "death_per_fight", "{:.0%}"), ("outcomes", "kills400", "{:.1f}")]
    head = ["bot"] + [c[1] for c in cols] + ["draft (top 5 picks)"]
    trows = []
    for x in bots:
        cells = [x]
        for block, k, fmt in cols:
            m = st[block].get(k, {}).get("mean", {}).get(x)
            cells.append("-" if m is None else fmt.format(m))
        picks = collections.Counter(ch for b in matched[x] for ch in set(b["rec"]["draft"]))
        cells.append(" ".join(f"{ch}{100 * n / len(matched[x]):.0f}" for ch, n in picks.most_common(5)))
        trows.append(cells)
    print("== Fingerprints on matched conditions (means over sides)")
    print("\n".join(fu.table(head, trows)))
    # pairs
    h2h = head_to_head(all_rows, bots)
    prow, pairs = [], []
    for x, y in itertools.combinations(bots, 2):
        dd, dtop = distance(st["decisions"], x, y)
        do, otop = distance(st["outcomes"], x, y)
        ov = draft_overlap(matched[x], matched[y])
        h = h2h.get((x, y))
        hs = None
        if h and h["n"]:
            hs = (h["w"] + h["d"] / 2) / h["n"]
        flag = ""
        if dd is not None and dd < CONVERGED and (hs is None or 0.35 <= hs <= 0.65):
            flag = "LOOKS CONVERGED"
        elif dd is not None and dd < CLOSE:
            flag = "close"
        pairs.append({"x": x, "y": y, "decision_distance": dd, "outcome_distance": do, "draft_overlap": ov,
                      "h2h": {k: (dict(v) if k == "versions" else v) for k, v in h.items()} if h else None,
                      "h2h_score": hs, "flag": flag, "top_decision_diffs": dtop[:6], "top_outcome_diffs": otop[:4]})
        prow.append([f"{x} / {y}", "-" if dd is None else f"{dd:.2f}", "-" if do is None else f"{do:.2f}",
                     "-" if ov is None else f"{ov:.2f}",
                     f"{h['w']}-{h['l']}-{h['d']} ({hs:.2f})" if h else "none", flag])
    print("== Pairs (distance = RMS standardized difference; draft = mean Jaccard on the same map and side)")
    print("\n".join(fu.table(["pair", "decisions", "outcomes", "draft", "head-to-head (x score)", "flag"], prow)))
    print("== Where each pair differs most (d = difference / pooled sd; positive: the first bot is higher)")
    for pr in pairs:
        print(f"  {pr['x'].split('/')[-1]} / {pr['y'].split('/')[-1]}: "
              + ", ".join(f"{k} {d:+.1f}" for k, d in pr["top_decision_diffs"][:5]))
    multi = [f"{x}/{y}: {len(v['versions'])} version pairs" for (x, y), v in h2h.items() if len(v["versions"]) > 1]
    if multi:
        print("  note: head-to-heads pool several versions: " + "; ".join(multi))
    conv = [f"{p['x']} / {p['y']}" for p in pairs if p["flag"] == "LOOKS CONVERGED"]
    print("converged-looking pairs: " + (", ".join(conv) if conv else "none"))
    if a.json:
        Path(a.json).write_text(json.dumps({"bots": bots, "runs": [str(r) for r in runs], "matched_keys": len(common),
                                            "opponents": opps, "features": st, "pairs": pairs}, indent=1, default=str) + "\n",
                                encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
