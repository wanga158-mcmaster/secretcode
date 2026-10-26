"""The online catalog's style flags (foundation/online.md O1), copied so a local opponent's
behaviour is classified exactly as the online sides were.

Copied from bots/contenders/foundation/tools/online/style.py (22 Oct): family, feats and FLAGS,
unchanged except that feats takes the side record and track features directly. A flag is
measured on one team-side in one game, mostly before r400; "OUTCOME" flags depend on how the game
went. `contact_seeker` and `contact_avoider` compare a side's first champion damage with the
median first contact of its map over the games held (online: all summaries; locally: the
battery's own games, a different population). Kept apart from onlinedefs.py so that adding
flags does not invalidate the decoded-game cache.

O1 kinds without a foundation flag (jackpot control, chases, kiting as a strategy, bush
ambushes) are not measured here; NARRATIVE maps O1's narrative kinds to the flags that witness them.
"""
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from onlinedefs import geometry, median  # noqa: E402

RANGED = {"Vesper", "Stellar", "Cassi", "Willow", "Tavi", "Prism"}


def family(draft):
    s = set(draft)
    if len(s & {"Ryth", "Vesper", "Vendetta", "Marcella", "Heart"}) >= 3 and ("Heart" in s or "Marcella" in s):
        return "meta"
    if len(s & {"C&I", "Stellar", "Ren", "Varg"}) >= 3:
        return "template"
    if "Prism" in s:
        return "prism"
    if "Willow" in s:
        return "willow"
    return "other"


def rect_dist(x, y, rc):
    dx = max(rc["minX"] - x, 0, x - rc["maxX"])
    dy = max(rc["minY"] - y, 0, y - rc["maxY"])
    return max(dx, dy)


def feats(r, tf, map_contact):
    """Numeric style features of one side; tf = its track features or None; map_contact = median
    first-contact round of the game's map over all games held."""
    f = {}
    f["family"] = family(r["draft"])
    sh400 = collections.Counter()
    first_emb = []
    for c in r["champs"]:
        for rr, name, n, amt, x, y in c["buys"]:
            if " Shard of " in name and rr <= 400:
                sh400[name.split(" Shard of ")[1]] += n
        if c["emblems"]:
            first_emb.append(c["emblems"][0][1].split(" Emblem of ")[0])
    tot = sum(sh400.values())
    f["H"] = sh400["Health"] / tot if tot >= 5 else None
    f["S"] = sh400["Strength"] / tot if tot >= 5 else None
    f["F"] = sh400["Focus"] / tot if tot >= 5 else None
    f["treasure_first"] = (sum(1 for e in first_emb if e == "Treasure") / len(first_emb)) if first_emb else None
    f["first_buy"] = median([c["buys"][0][0] if c["buys"] else None for c in r["champs"]])
    caps200 = [c for c in r["caps"] if c[0] <= 200]
    cat200 = collections.Counter(c[1] for c in caps200)
    f["gem_caps200"] = cat200["gem"] + cat200["mix"]
    f["gem_caps_share200"] = f["gem_caps200"] / len(caps200) if caps200 else None
    caps400 = [c for c in r["caps"] if c[0] <= 400]
    cat = collections.Counter(c[1] for c in caps400)
    f["caps400"] = len(caps400)
    f["monster_caps_share"] = (cat["wolf"] + cat["elk"] + cat["golem"] + cat["monolith"]) / len(caps400) if caps400 else None
    big = [c[0] for c in r["clears"] if c[1] in ("golem", "monolith")]
    f["first_big_clear"] = min(big) if big else None
    bags400 = [t[1] for t in r["tributes"] if t[0] <= 400]
    f["bag_med400"] = median(bags400)
    f["prism_carry400"] = max([c["carry400"] for c in r["champs"] if c["name"] == "Prism"], default=None)
    f["maxcarry"] = max([c["maxcarry"] for c in r["champs"]], default=0)
    f["locks400"] = sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Altar Lock" and rr <= 400)
    f["locks"] = sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Altar Lock")
    f["crystals400"] = sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Teleport Crystal" and rr <= 400)
    f["kills400"] = sum(1 for k in r["kills_list"] if k["r"] <= 400)
    f["deaths400"] = sum(1 for d in r["deaths_list"] if d["r"] <= 400 and d["ctx"] == "champion")
    f["camp_deaths400"] = sum(1 for d in r["deaths_list"] if d["r"] <= 400 and d["ctx"] == "camp")
    f["first_dmg"] = r["first_cdmg"]
    f["map_contact"] = map_contact
    f["contact_rel"] = (r["first_cdmg"] / map_contact) if (r["first_cdmg"] is not None and map_contact) else None
    f["heal400"] = None  # per-round heal not kept; whole-game heal below
    f["heal"] = sum(c["heal"] for c in r["champs"])
    f["wards400"] = r["wards400"]
    f["fuel_ex"] = r["fuel_ex"]
    geo = geometry()["byid"].get(r["map"])
    if geo:
        other = "teamB" if r["team"] == "teamA" else "teamA"
        f["spawn_kills"] = sum(1 for k in r["kills_list"] if k["r"] <= 1000 and rect_dist(k["x"], k["y"], geo["spawns"][other]) <= 6)
    if tf:
        f["grouped"] = tf["coh300"][2] if tf.get("coh300") else None
        f["solo"] = tf["coh300"][0] if tf.get("coh300") else None
        f["spread"] = tf.get("spread300")
        f["near400"] = tf.get("near400")
        f["enemyhalf"] = tf.get("enemyhalf")
        f["enemyhalf300"] = tf.get("enemyhalf300")
        f["spawn"] = tf.get("spawn")
        f["recalls400"] = tf.get("recalls400")
        f["party"] = tf.get("party")
        f["ranged_hold"] = tf.get("ranged_hold") if (tf.get("ranged_dmg_rounds") or 0) >= 15 else None
        f["contact_frac"] = tf.get("contact_frac")
    return f


def _f(f, k):
    return f.get(k)


# flag -> (predicate, definition, kind). kind: "choice" (measured early, used for records) or
# "OUTCOME" (depends on how the game went; prevalence only).
FLAGS = {
    "meta_draft": (lambda f: f["family"] == "meta", "3+ of Ryth/Vesper/Vendetta/Marcella/Heart, incl. Heart or Marcella", "choice"),
    "template_draft": (lambda f: f["family"] == "template", "3+ of C&I/Stellar/Ren/Varg", "choice"),
    "prism_draft": (lambda f: f["family"] == "prism", "Prism, not meta or template", "choice"),
    "willow_draft": (lambda f: f["family"] == "willow", "Willow, not meta/template/prism", "choice"),
    "grouped": (lambda f: None if _f(f, "grouped") is None else f["grouped"] >= 0.40,
                ">= 40% of champion-rounds r1-300 with 2+ allies within 4", "choice"),
    "dispersed": (lambda f: None if _f(f, "solo") is None else f["solo"] >= 0.55,
                  ">= 55% of champion-rounds r1-300 with no ally within 4", "choice"),
    "health_build": (lambda f: None if f["H"] is None else f["H"] >= 0.45, ">= 45% of shards by r400 Health", "choice"),
    "focus_build": (lambda f: None if f["F"] is None else f["F"] >= 0.30, ">= 30% of shards by r400 Focus", "choice"),
    "strength_build": (lambda f: None if f["S"] is None else f["S"] >= 0.65, ">= 65% of shards by r400 Strength", "choice"),
    "treasure_first": (lambda f: None if f["treasure_first"] is None else f["treasure_first"] >= 0.6,
                       ">= 60% of champions' first emblem is Treasure", "choice"),
    "early_shop": (lambda f: None if f["first_buy"] is None else f["first_buy"] <= 100, "median first purchase <= r100", "choice"),
    "contact_seeker": (lambda f: None if f["contact_rel"] is None and f["first_dmg"] is not None else
                       (f["first_dmg"] is not None and f["map_contact"] and f["first_dmg"] <= 0.5 * f["map_contact"]),
                       "own first champion damage at <= half the map's median first-contact round", "choice"),
    "contact_avoider": (lambda f: (f["first_dmg"] is None or (f["map_contact"] and f["first_dmg"] >= 2.5 * f["map_contact"])),
                        "own first champion damage at >= 2.5x the map's median first-contact round (or never)", "choice"),
    "invader": (lambda f: None if _f(f, "enemyhalf300") is None else f["enemyhalf300"] >= 0.25,
                ">= 25% of champion-rounds r1-300 nearer the enemy spawn", "choice"),
    "turtle": (lambda f: None if _f(f, "enemyhalf") is None else f["enemyhalf"] <= 0.03 and f["spawn"] >= 0.08,
               "<= 3% of champion-rounds r1-1000 in the enemy half and >= 8% on/next to own spawn", "choice"),
    "gem_rush": (lambda f: f["gem_caps200"] >= 3 and (f["gem_caps_share200"] or 0) >= 0.4,
                 "3+ gem-bearing cluster captures by r200, >= 40% of captures", "choice"),
    "monster_farm": (lambda f: None if f["monster_caps_share"] is None else f["monster_caps_share"] >= 0.55,
                     ">= 55% of captures by r400 are wolf/elk/golem/monolith groups", "choice"),
    "early_siege": (lambda f: f["first_big_clear"] is not None and f["first_big_clear"] <= 300, "a golem or monolith cleared by r300", "choice"),
    "lock_user": (lambda f: f["locks400"] >= 3, "3+ altar locks bought by r400", "choice"),
    "no_locks": (lambda f: f["locks"] == 0, "no altar lock all game", "choice"),
    "crystal_routes": (lambda f: f["crystals400"] >= 2, "2+ teleport crystals by r400", "choice"),
    "big_bags": (lambda f: None if f["bag_med400"] is None else f["bag_med400"] >= 40, "median tribute bag by r400 >= 40", "choice"),
    "battery": (lambda f: (f["prism_carry400"] or 0) >= 150 or f["maxcarry"] >= 300,
                "a Prism carrying >= 150 gems at r400, or any champion >= 300 at once", "OUTCOME"),
    "sustain": (lambda f: f["heal"] >= 3000, ">= 3000 healing done in the game", "OUTCOME"),
    "spawn_camper": (lambda f: (f.get("spawn_kills") or 0) >= 2, "2+ kills within 6 of the enemy spawn by r1000", "OUTCOME"),
    "camp_suicide": (lambda f: f["camp_deaths400"] >= 3, "3+ deaths to camps by r400", "OUTCOME"),
}

# O1's narrative kinds (the "Answer must" list) -> the flags that witness them (any one); None: no
# flag. Prism battery uses the Prism half of the battery flag (a starter that drafts Prism and never
# banks is not a battery), sustain the healing flag alone (drafting Willow is not healing).
NARRATIVE = {
    "early aggression": ["contact_seeker"], "grouped play": ["grouped"], "dispersed play": ["dispersed"],
    "gem rush": ["gem_rush"], "jackpot control": None, "Health-first scaling": ["health_build"],
    "Focus burst scaling": ["focus_build"], "spawn camping / late sieges": ["spawn_camper"],
    "lock harassment": ["lock_user"], "chases": None, "kiting": None, "bush ambushes": None,
    "Prism battery": ["prism_battery"], "big bags / hoarding": ["big_bags"], "territory invasion": ["invader"],
    "passivity / turtling": ["turtle", "contact_avoider"], "sustain": ["sustain"],
    "teleport-crystal routing": ["crystal_routes"], "early siege": ["early_siege"], "camp suicide": ["camp_suicide"],
}


def map_contacts(bundles_by_game: dict) -> dict:
    """map -> median first contact (min of both teams' first champion damage per game), as style.load."""
    per_map = collections.defaultdict(list)
    for (mapname, _), recs in bundles_by_game.items():
        fc = [r["first_cdmg"] for r in recs if r["first_cdmg"] is not None]
        if fc:
            per_map[mapname].append(min(fc))
    return {m: median(x) for m, x in per_map.items()}


def flags_of(rec: dict, tf: dict | None, map_contact) -> dict:
    """flag -> True/False/None for one side."""
    f = feats(rec, tf, map_contact)
    out = {}
    for k, (fn, _, _) in FLAGS.items():
        try:
            out[k] = fn(f)
        except (TypeError, KeyError):
            out[k] = None
    out["prism_battery"] = (f["prism_carry400"] or 0) >= 150  # the Prism half of "battery"
    return out


if __name__ == "__main__":  # python -B -I -X utf8 o1flags.py: list the flags and the O1 kinds they witness
    for k, (_, text, kind) in FLAGS.items():
        print(f"{k:16s} {kind:8s} {text}")
    for k, v in NARRATIVE.items():
        print(f"O1 kind {k}: {', '.join(v) if v else 'no foundation flag'}")
