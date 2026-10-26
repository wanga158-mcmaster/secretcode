"""Economy model library (contenders foundation): parameters from the engine's runtime catalog,
exact float32 hit arithmetic and a round-by-round camp-clear simulator.

Everything a balance patch can change is read from docs/reference/runtime-catalog.json (camp
health/strength/range, champion stats, growth, abilities, passives, item passives, settings) or
from the map geometry (group rewards and respawn). The mechanics encoded here are the ones the
economy probes verified on engine 1.0.2 (economy.md E1-E7):
- each awake monster strikes once per round, at round start, the nearest champion (Chebyshev)
  within its range, ties to the lower champion id; damage = Strength x (1 - Jungle) x (1 -
  Durability) for camps; a stunned monster skips its next strike;
- a hit wakes the whole group; the first strikes land at the next round start;
- basic damage to camps = Strength x (1 + Monster); hit counts by float32 subtraction;
- attack resets add one basic attack in the turn they are cast (cooldown 8); Vendetta's kills
  reset her cooldowns; damage spells and the basic attack share a turn (casting excludes walking);
- capture: reward at the start of the 6th round after the capturer stands on a member tile of
  the dead group (capture_time + 1); respawn = group cooldown rounds after the reward;
- recall: 5 channel turns then arrival at turn start; the spawn heals fully at that turn's end.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[5]
CATALOG = ROOT / "docs" / "reference" / "runtime-catalog.json"

# Handbook camp rewards per member (the catalog has no reward fields; every pool and legacy
# group's gemValue/goldValue equals the sum of these and its cooldown the maximum of RESPAWN,
# checked by check_rewards()).
REWARD = {"wolf": (4, 40), "elk": (15, 90), "golem": (36, 160), "monolith": (200, 300),
          "gem_cluster": (7, 0), "gold_cluster": (0, 20)}
RESPAWN = {"wolf": 40, "elk": 60, "golem": 80, "monolith": 150, "gem_cluster": 50, "gold_cluster": 50}
NAMES = {"ryth": "Ryth", "vesper_vale": "Vesper", "vendetta": "Vendetta", "cassi": "Cassi", "issac": "Issac",
         "stellar": "Stellar", "prism": "Prism", "marcella": "Marcella", "willow": "Willow", "ren": "Ren",
         "varg": "Varg", "tavi": "Tavi", "heart_of_ruin": "Heart"}
KEY = {v: k for k, v in NAMES.items()}


def f32(x) -> np.float32:
    return np.float32(x)


def hits_to_kill(hp: float, dmg: float) -> int:
    """Hits by exact float32 repeated subtraction (rules-notes: product formulas can undercount)."""
    h, d = f32(hp), f32(dmg)
    if d <= 0:
        return 1 << 30
    n = 0
    while h > f32(0):
        h = f32(h - d)
        n += 1
    return n


@dataclass
class Ability:
    kind: int          # 1 reset, 2 damage, 3 aoe, 4 cc, 5 heal, 6 dash, 7 shield
    cooldown: int
    targets: int
    exclusive: bool
    rng: float = 0.0
    modifier: float = 0.0
    radius: float = 0.0
    duration: int = 0
    enhanced: bool = False


@dataclass
class Kit:
    name: str
    melee: bool
    hp: float
    strength: float
    focus: float
    g_hp: float
    g_str: float
    g_focus: float
    attack_range: int
    abilities: list
    passive_type: int
    passive_mag: float

    def stats(self, shards_hp=0, shards_str=0, shards_focus=0):
        return (self.hp + shards_hp * self.g_hp, self.strength + shards_str * self.g_str,
                self.focus + shards_focus * self.g_focus)


@dataclass
class Catalog:
    settings: dict
    camps: dict
    kits: dict
    item_passive: dict   # family -> magnitude
    raw: dict = field(repr=False, default_factory=dict)


def load_catalog(path: Path = CATALOG) -> Catalog:
    d = json.loads(path.read_text(encoding="utf-8"))["result"]
    camps = {c["name"]: {"hp": c["stats"]["base_health"], "str": c["stats"]["base_strength"],
                         "range": c["stats"]["base_attack_range"]} for c in d["camps"]}
    passives = {p["name"]: (p["type"], p["magnitude"]) for p in d["passives"]}
    kits = {}
    for c in d["champions"]:
        st = c["stats"]
        abil = []
        for a in c["abilities"]:
            pl = a.get("payload", {})
            abil.append(Ability(kind=a["kind"], cooldown=a["cooldown"], targets=a["targets"], exclusive=bool(a["attributes"] & 1),
                                rng=float(pl.get("range", 0)), modifier=float(pl.get("modifier", 0)), radius=float(pl.get("radius", 0)),
                                duration=int(pl.get("duration", 0)), enhanced=bool(pl.get("enhanced_attack", 0))))
        pt, pm = passives.get(c["name"], (0, 0.0))
        kits[NAMES.get(c["name"], c["name"])] = Kit(
            name=NAMES.get(c["name"], c["name"]), melee=c["attack_type"] == 0, hp=st["base_health"],
            strength=st["base_strength"], focus=st["base_focus"], g_hp=st["health_growth"], g_str=st["strength_growth"],
            g_focus=st["focus_growth"], attack_range=st["base_attack_range"], abilities=abil, passive_type=pt, passive_mag=pm)
    fam = {1: "monster", 2: "durability", 3: "treasure", 4: "jungle", 5: "home"}
    item_passive = {}
    for it in d["items"]:
        if it["passive"] in fam:
            item_passive[fam[it["passive"]]] = it["passive_magnitude"]
    return Catalog(settings=d["settings"], camps=camps, kits=kits, item_passive=item_passive, raw=d)


# --------------------------------------------------------------------------------------------
@dataclass
class Fighter:
    kit: Kit
    cid: int                       # tank priority: the lowest engaged melee cid is struck
    hp: float
    max_hp: float
    strength: float
    focus: float
    monster: bool = False
    jungle: bool = False
    durability: bool = False
    gold_mult: float = 1.0
    cd: list = field(default_factory=lambda: [0, 0])
    shield: float = 0.0
    shield_left: int = 0
    out: int = 0
    staged: bool = False
    dealt: float = 0.0
    taken: float = 0.0
    trips: int = 1

    @property
    def name(self):
        return self.kit.name

    def camp_mult_in(self, cat: Catalog) -> float:
        m = 1.0
        if self.jungle:
            m *= 1.0 - cat.item_passive["jungle"]
        if self.durability:
            m *= 1.0 - cat.item_passive["durability"]
        return m

    def out_mult(self, cat: Catalog) -> float:
        m = 1.0 + (cat.item_passive["monster"] if self.monster else 0.0)
        if self.kit.passive_type == 10:  # Heart of Ruin: up to +50% at zero health
            m *= 1.0 + self.kit.passive_mag * max(0.0, 1.0 - self.hp / self.max_hp)
        return m


def make_fighter(cat: Catalog, name: str, cid: int, shards=(0, 0, 0), emblems=()) -> Fighter:
    """shards = (health, strength, focus) shard counts (an emblem counts as its five shards, a
    Power emblem as six); emblems = passive families held (monster, jungle, durability, treasure)."""
    k = cat.kits[name]
    hp, st, fo = k.stats(*shards)
    if k.passive_type == 5:  # Ryth: Strength + 1% of bonus health
        st += k.passive_mag * (hp - k.hp)
    gm = 1.0 + (k.passive_mag if k.passive_type == 1 else 0.0) + (cat.item_passive["treasure"] if "treasure" in emblems else 0.0)
    return Fighter(kit=k, cid=cid, hp=hp, max_hp=hp, strength=st, focus=fo, monster="monster" in emblems,
                   jungle="jungle" in emblems, durability="durability" in emblems, gold_mult=gm)


@dataclass
class Monster:
    kind: str
    hp: float
    strength: float
    rng: int
    pos: tuple
    alive: bool = True
    stunned: int = 0


def monsters_of(cat: Catalog, members) -> list:
    """members: iterable of (type, x, y)."""
    out = []
    for t, x, y in members:
        c = cat.camps[t]
        out.append(Monster(kind=t, hp=float(c["hp"]), strength=float(c["str"]), rng=int(c["range"]), pos=(x, y)))
    return out


def compact_layout(types):
    """A compact placement for a composition without coordinates: a square block filled row by
    row, strongest members first (pool groups are cardinally connected blobs)."""
    order = sorted(types, key=lambda t: -REWARD[t][0] - RESPAWN[t])
    w = max(1, math.ceil(math.sqrt(len(order))))
    return [(t, i % w, i // w) for i, t in enumerate(order)]


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


@dataclass
class ClearResult:
    rounds: int            # rounds from the first hit to the last kill, both counted (1<<20 if never)
    damage: float          # damage absorbed by the team (after reductions and shields)
    trips: int             # engagements of the busiest fighter (1 = no retreat)
    out_rounds: int        # champion-rounds spent retreating, recalling, healing and walking back
    per_fighter: dict
    killed: bool
    strikes: int
    deaths: int
    killer_melee: bool


def _best_center(alive, target, radius):
    """Centre within 1 of the target (a ground cast or a dash landing) covering the most members."""
    best, best_n = target.pos, -1
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            c = (target.pos[0] + dx, target.pos[1] + dy)
            n = sum(1 for m in alive if cheb(m.pos, c) <= radius)
            if n > best_n:
                best, best_n = c, n
    return best


def simulate_clear(cat: Catalog, fighters: list, mons: list, *, walk_back: int = 10, use_spells: bool = True,
                   reserve: float = 1.0, retreat: bool = True, wave: bool = True, max_rounds: int = 4000) -> ClearResult:
    """Round-by-round clear of one group by `fighters` (in place at round 1, abilities ready).

    Model (economy.md E2-E7): every living monster with Strength > 0 is in range of the engaged
    team once the group is awake (the first hit wakes it; strikes begin the next round start) and
    strikes the tank: the engaged melee fighter with the lowest cid, else the engaged ranged one
    with the lowest cid (order fighters by tank priority). Stunned monsters skip a strike.
    Fighters act in cid order: one cast (damage/cc/AoE/dash/shield, or an attack reset for a second
    basic attack), then the basic attack; Vendetta's kills reset her cooldowns. AoE, splash and
    dash use the members' positions (radius 1). The tank retreats when it could not survive one
    more round plus its retreat (monster range - 1 strikes, none when another melee fighter stays
    engaged); it is out for range + recall + 1 + walk_back rounds and returns at full health
    (camps keep damage). Without `retreat` fighters fight to death. Kill order: Smith's rule.
    Siege policy (`wave`, default): a tank covered by another melee fighter steps back (no strikes),
    recalls at once and waits for the team; when the last tank leaves uncovered, every fighter
    leaves (the hurt ones recall) and the team re-engages together once everyone is back. Without
    `wave`, each fighter re-engages on its own as soon as it is back.
    """
    recall = int(cat.settings["recall_time"])
    respawn = int(cat.settings["respawn_time"])
    rnd = 0
    strikes = deaths = 0
    first_hit = None
    last_kill = None
    out_rounds = 0
    killer_melee = False

    def alive():
        return [m for m in mons if m.alive]

    def order():
        return sorted(alive(), key=lambda m: (-(m.strength / m.hp if m.hp > 0 else 0.0), m.hp))

    def engaged():
        return [f for f in fighters if f.out == 0 and f.hp > 0 and not f.staged]

    def tank_of(eng):
        melee = [f for f in eng if f.kit.melee]
        return min(melee or eng, key=lambda f: f.cid) if eng else None

    def hit(f, m, dmg):
        nonlocal last_kill, killer_melee
        m.hp = float(f32(m.hp) - f32(dmg))
        f.dealt += dmg
        if m.hp <= 0 and m.alive:
            m.alive = False
            last_kill = rnd
            killer_melee = f.kit.melee
            return 1
        return 0

    while rnd < max_rounds and alive():
        rnd += 1
        for f in fighters:
            if f.out > 0:
                f.out -= 1
                out_rounds += 1
                if f.out == 0:
                    if f.hp < f.max_hp:
                        f.hp, f.trips = f.max_hp, f.trips + 1
                    f.shield, f.shield_left = 0.0, 0
                    f.staged = True
        if not engaged() and all(f.out == 0 for f in fighters):
            for f in fighters:
                f.staged = False
        elif not wave:
            for f in fighters:
                if f.out == 0:
                    f.staged = False
        eng = engaged()
        if first_hit is not None and eng:
            tank = tank_of(eng)
            for m in alive():
                if m.strength <= 0:
                    continue
                if m.stunned > 0:
                    m.stunned -= 1
                    continue
                dmg = float(f32(m.strength) * f32(tank.camp_mult_in(cat)))
                strikes += 1
                if tank.shield_left > 0:
                    a = min(tank.shield, dmg)
                    tank.shield -= a
                    dmg -= a
                tank.hp -= dmg
                tank.taken += dmg
                if tank.kit.passive_type == 7:  # Ren retaliates against each striking monster
                    hit(tank, m, float(f32(tank.kit.passive_mag * tank.focus)))
            for f in eng:
                if f.shield_left > 0:
                    f.shield_left -= 1
                    if f.shield_left == 0:
                        f.shield = 0.0
            if tank.hp <= 0:
                deaths += 1
                tank.out = respawn + walk_back
        if not alive():
            break
        for f in sorted(engaged(), key=lambda x: x.cid):
            if not alive():
                break
            eng = engaged()
            incoming = sum(m.strength for m in alive() if m.strength > 0 and m.stunned == 0) * f.camp_mult_in(cat)
            if retreat and first_hit is not None and incoming > 0 and f is tank_of(eng):
                rng = max(m.rng for m in alive() if m.strength > 0)
                dist = 1 if f.kit.melee else 3
                n_ret = max(0, rng - dist)          # strikes while walking out of range
                others = [g for g in eng if g is not f]
                if any(g.kit.melee for g in others) or (not f.kit.melee and others):
                    n_lost = 0                      # a step back hands the strikes over
                elif others:
                    n_lost = min(n_ret, 2)          # melee tank walking behind ranged cover
                else:
                    n_lost = n_ret
                if f.hp <= incoming * (1 + n_lost) * reserve:
                    lost = n_lost * incoming
                    f.hp -= lost
                    f.taken += lost
                    if f.hp <= 0:
                        deaths += 1
                        f.out = respawn + walk_back
                    elif n_lost == 0:
                        # nothing strikes it any more: recall at once, no walk out of range
                        f.out = 1 + recall + 1 + walk_back
                    else:
                        f.out = max(1, rng - dist + 1) + recall + 1 + walk_back
                    if not [g for g in engaged() if g is not f and g.out == 0] or (n_lost and wave):
                        # the wave ends: the others leave too (the hurt ones recall)
                        for g in engaged():
                            if g is f:
                                continue
                            step = max(1, rng - (1 if g.kit.melee else 3) + 1)
                            g.out = step + ((recall + 1 + walk_back) if g.hp < 0.9 * g.max_hp else 0)
                    continue
            tgt = order()[0]
            cast = False
            n_att = 1
            if use_spells:
                for slot, ab in enumerate(f.kit.abilities):
                    if f.cd[slot] > 0 or cast:
                        continue
                    mult = f.out_mult(cat)
                    if ab.kind in (2, 4) and ab.targets & 1:
                        if ab.kind == 4 and ab.duration:
                            tgt.stunned = max(tgt.stunned, ab.duration)
                        if ab.modifier > 0:
                            hit(f, tgt, float(f32(ab.modifier * f.focus) * f32(mult)))
                        cast = True
                    elif ab.kind == 3 and ab.targets & 1:
                        c = _best_center(alive(), tgt, ab.radius)
                        d = float(f32(ab.modifier * f.focus) * f32(mult))
                        for m in [m for m in alive() if cheb(m.pos, c) <= ab.radius]:
                            if ab.duration:
                                m.stunned = max(m.stunned, ab.duration)
                            hit(f, m, d)
                        cast = True
                    elif ab.kind == 6 and ab.modifier > 0:
                        c = _best_center(alive(), tgt, ab.radius)
                        d = float(f32(ab.modifier * f.focus) * f32(mult))
                        for m in [m for m in alive() if cheb(m.pos, c) <= ab.radius]:
                            if ab.duration:
                                m.stunned = max(m.stunned, ab.duration)
                            hit(f, m, d)
                        cast = True
                    elif ab.kind == 7 and first_hit is not None and incoming > 0 and f is tank_of(eng):
                        f.shield, f.shield_left = ab.modifier * f.focus, ab.duration
                        cast = True
                    elif ab.kind == 1 and first_hit is not None:
                        n_att, cast = 2, True
                    if cast:
                        f.cd[slot] = ab.cooldown
                        if first_hit is None:
                            first_hit = rnd
                if cast and f.kit.passive_type == 6 and last_kill == rnd:
                    f.cd = [0, 0]
            k = 0
            while k < n_att and alive():
                tgt = order()[0]
                kills = hit(f, tgt, float(f32(f.strength) * f32(f.out_mult(cat))))
                if f.kit.passive_type == 4:  # Varg splash: Strength onto every other member within 1
                    for m in [m for m in alive() if m is not tgt and cheb(m.pos, tgt.pos) <= 1]:
                        kills += hit(f, m, float(f32(f.kit.passive_mag * f.strength) * f32(f.out_mult(cat))))
                if k == 1 and tgt.alive and any(ab.kind == 1 and ab.enhanced for ab in f.kit.abilities):
                    tgt.stunned = max(tgt.stunned, 1)  # Marcella's enhanced attack
                if first_hit is None:
                    first_hit = rnd
                if kills and f.kit.passive_type == 6:  # Vendetta: kills reset every cooldown
                    f.cd = [0, 0]
                    if not cast and use_spells:
                        for slot, ab in enumerate(f.kit.abilities):
                            if ab.kind == 1:
                                n_att, cast = n_att + 1, True
                                f.cd[slot] = ab.cooldown
                                break
                k += 1
            f.cd = [max(0, c - 1) for c in f.cd]
    killed = not alive()
    per = {f.name: {"dealt": round(f.dealt, 1), "taken": round(f.taken, 1), "trips": f.trips} for f in fighters}
    span = (last_kill - first_hit + 1) if killed and first_hit is not None and last_kill is not None else 1 << 20
    return ClearResult(rounds=span, damage=sum(f.taken for f in fighters), trips=max(f.trips for f in fighters),
                       out_rounds=out_rounds, per_fighter=per, killed=killed, strikes=strikes, deaths=deaths,
                       killer_melee=killer_melee)


def check_rewards(groups) -> list:
    """Groups whose gemValue/goldValue/cooldown differ from the per-member tables."""
    bad = []
    for g in groups:
        gems = sum(REWARD[m["type"]][0] for m in g["members"])
        gold = sum(REWARD[m["type"]][1] for m in g["members"])
        cd = max(RESPAWN[m["type"]] for m in g["members"])
        if (gems, gold, cd) != (g["gemValue"], g["goldValue"], g["cooldown"]):
            bad.append((g["key"], (gems, gold, cd), (g["gemValue"], g["goldValue"], g["cooldown"])))
    return bad
