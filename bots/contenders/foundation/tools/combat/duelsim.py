"""Turn-exact fight arithmetic for foundation/combat.md (the model that the engine duels calibrate).

Every number comes from docs/reference/runtime-catalog.json at run time (stats, growth, ability
payloads, passives), so the model follows balance patches without edits. Rules encoded here and
checked against engine duels (_duel-lab): rounds alternate the starting team (A first in odd
rounds); turns interleave by pick order (A1 B1 A2 B2 ... or B1 A1 ...); a stun removes the
victim's next turn and stuns landing before that turn do not add up; cooldowns are 8 owner turn
ends (cast at turn t, ready again at t+8); shields last 5 owner turns and absorb first; ranged
champions cannot walk and basic-attack in one turn unless an attack reset restores the attack;
melee champions walk and attack; a dash (2 tiles) and a walk combine with an attack; hp is float32.

  python duelsim.py duel X Y [--build-a N] [--build-b N] [--parity P] [--trace]
  python duelsim.py matrix [--build N] [--out FILE]        all 12x12 picks, both parities
  python duelsim.py compare RUN_DIR                        model vs engine per game (duel.py run)
  python duelsim.py groups RUN_DIR                         model vs engine for the group table rows
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
CAT = json.loads((ROOT / "docs/reference/runtime-catalog.json").read_text(encoding="utf-8"))["result"]
F32 = np.float32

PICK_BODIES = [["ryth"], ["vesper_vale"], ["vendetta"], ["cassi", "issac"], ["stellar"], ["prism"], ["marcella"],
               ["willow"], ["ren"], ["varg"], ["tavi"], ["heart_of_ruin"]]
PICK_NAMES = ["Ryth", "Vesper", "Vendetta", "Cassi&Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
              "Tavi", "Heart"]
# Build table of _duel-lab (shard counts H, S, F; durability emblem).
BUILDS = {0: (0, 0, 0, False), 1: (5, 0, 0, False), 2: (10, 0, 0, False), 3: (15, 0, 0, False),
          4: (0, 5, 0, False), 5: (0, 10, 0, False), 6: (0, 15, 0, False), 7: (0, 0, 5, False),
          8: (0, 0, 10, False), 9: (0, 0, 15, False), 10: (5, 5, 0, False), 11: (10, 6, 1, False),
          12: (5, 0, 11, True), 13: (5, 0, 0, True), 14: (10, 0, 0, True), 15: (5, 10, 0, True)}
BUILD_GOLD = {0: 0, 1: 150, 2: 300, 3: 450, 4: 150, 5: 300, 6: 450, 7: 150, 8: 300, 9: 450, 10: 300, 11: 450,
              12: 450, 13: 150, 14: 300, 15: 450}

CHAMP = {c["name"]: c for c in CAT["champions"]}
PASSIVE = {p["name"]: p for p in CAT["passives"]}


def roles_of(name: str) -> list[dict]:
    out = []
    for ab in CHAMP[name]["abilities"]:
        k, pl = ab["kind"], ab.get("payload", {})
        if k == 1:
            role = "ENHRESET" if pl.get("enhanced_attack") else "RESET"
        elif k == 2:
            role = "DMG"
        elif k == 3:
            role = "AOESTUN" if pl.get("duration", 0) > 0 else "AOE"
        elif k == 4:
            role = "STUN"
        elif k == 5:
            role = "HEAL"
        elif k == 6:
            role = "HDASH" if pl.get("radius", 0) > 0 else "DASH"
        elif k == 7:
            role = "SHIELD"
        else:
            role = "NONE"
        out.append({"role": role, "mod": float(pl.get("modifier", 0.0)), "range": int(pl.get("range", 0)),
                    "dur": int(pl.get("duration", 0)), "cooldown": int(ab["cooldown"]), "excl": bool(ab["attributes"] & 1)})
    return out


@dataclass
class Unit:
    name: str
    side: str
    order: int
    hp: F32
    maxhp: float
    strength: float
    focus: float
    ranged: bool
    rng: int
    abil: list
    durable: bool = False
    pol: int = 0
    slot: int = 0
    cd: list = field(default_factory=lambda: [0, 0])
    shield: float = 0.0
    shield_turns: int = 0
    stun_pending: bool = False
    stun_immune: bool = False
    alive: bool = True
    x: int = 0
    dealt: float = 0.0
    turns_lost: int = 0


CHAMP_ID = {c["name"]: c["id"] for c in CAT["champions"]}


def make_unit(name: str, side: str, order: int, build: int = 0, pol: int = 0, slot: int = 0) -> Unit:
    c = CHAMP[name]
    s = c["stats"]
    nh, ns, nf, dur = BUILDS[build]
    maxhp = s["base_health"] + nh * s["health_growth"]
    strength = s["base_strength"] + ns * s["strength_growth"]
    focus = s["base_focus"] + nf * s["focus_growth"]
    p = PASSIVE[name]
    if p["type"] == 5:  # Ryth: strength from bonus health
        strength += p["magnitude"] * (maxhp - s["base_health"])
    u = Unit(name, side, order, F32(maxhp), float(maxhp), float(F32(strength)), float(F32(focus)),
             c["attack_type"] == 1, int(s["base_attack_range"]), roles_of(name), durable=dur, pol=pol, slot=slot)
    u.stun_immune = p["type"] == 8  # Marcella
    return u


def heart_mult(u: Unit) -> float:
    p = PASSIVE[u.name]
    if p["type"] != 10:
        return 1.0
    return 1.0 + p["magnitude"] * (1.0 - float(u.hp) / u.maxhp)


FIGHT, KITE, FLEE, PASSIVE_POL, BASIC, FLEESTUN, HOLD, FLEEHIT, SPREAD, BASICSPREAD, STAGGER = range(11)


class Fight:
    """Champions on a line (A on the left moving right); Chebyshev distance = |dx| in the arena."""

    def __init__(self, a: list, b: list, start_round: int = 30, xa: int = 17, xb: int = 23, trace: bool = False):
        self.units = a + b
        self.a, self.b = a, b
        self.round = start_round
        self.trace = trace
        self.log = []
        for u in a:
            u.x = xa
        for u in b:
            u.x = xb

    def foes(self, u: Unit) -> list:
        return [v for v in (self.b if u.side == "A" else self.a) if v.alive]

    def allies(self, u: Unit) -> list:
        return [v for v in (self.a if u.side == "A" else self.b) if v.alive and v is not u]

    @staticmethod
    def dist(u: Unit, v: Unit) -> int:
        return abs(u.x - v.x)

    # damage ----------------------------------------------------------------
    def hit(self, src: Unit, dst: Unit, amount: float, retaliate: bool = True) -> None:
        if not dst.alive or (retaliate and not src.alive):
            return
        amt = amount * heart_mult(src)
        if dst.durable:
            amt *= 0.9
        if dst.shield > 0:
            absorbed = min(dst.shield, amt)
            dst.shield -= absorbed
            amt -= absorbed
        if amt > 0:
            dst.hp = F32(dst.hp - F32(amt))
            src.dealt += amt
        if dst.hp <= 0:
            self.kill(dst, src)
        p = PASSIVE[dst.name]
        if retaliate and p["type"] == 7 and src.alive:  # Ren retaliates on every damage instance
            self.hit(dst, src, p["magnitude"] * dst.focus, retaliate=False)

    def kill(self, u: Unit, by=None) -> None:
        if not u.alive:
            return
        u.alive = False
        u.hp = F32(0)
        self.log.append((self.round, "death", u.side, u.name))
        if by is not None and by.alive and PASSIVE[by.name]["type"] == 6:  # Vendetta: kill resets cooldowns
            by.cd = [0, 0]
        if u.name in ("cassi", "issac"):
            for v in self.units:
                if v.side == u.side and v.name in ("cassi", "issac") and v.alive:
                    self.kill(v)

    def stun(self, src: Unit, dst: Unit) -> None:
        if dst.alive and not dst.stun_immune:
            dst.stun_pending = True

    # one champion turn (mirrors _duel-lab's policies) ----------------------
    def slot(self, u: Unit, role: str) -> int:
        for i, ab in enumerate(u.abil):
            if ab["role"] == role:
                return i
        return -1

    def ready(self, u: Unit, role: str) -> bool:
        i = self.slot(u, role)
        return i >= 0 and u.cd[i] == 0

    def cast(self, u: Unit, role: str) -> dict:
        i = self.slot(u, role)
        u.cd[i] = u.abil[i]["cooldown"]
        return u.abil[i]

    def choose(self, u: Unit, fs: list, spread: bool) -> Unit:
        if spread:
            order = sorted(fs, key=lambda v: CHAMP_ID[v.name])
            return order[u.slot % len(order)]
        near = [v for v in fs if self.dist(u, v) <= 4]
        if near:
            return min(near, key=lambda v: (float(v.hp), v.order))
        return min(fs, key=lambda v: (self.dist(u, v), v.order))

    def toward(self, u: Unit, t: Unit) -> int:
        return 1 if t.x > u.x else (-1 if t.x < u.x else (1 if u.side == "A" else -1))

    def turn(self, u: Unit) -> None:
        if not u.alive:
            return
        if u.shield_turns == 0:
            u.shield = 0.0
        if u.stun_pending:
            u.stun_pending = False
            u.turns_lost += 1
            self.end_turn(u)
            return
        fs = self.foes(u)
        pol = u.pol
        if not fs or pol == PASSIVE_POL:
            self.end_turn(u)
            return
        spread = pol in (SPREAD, BASICSPREAD)
        stagger = pol == STAGGER
        abilities = pol not in (BASIC, BASICSPREAD)
        t = self.choose(u, fs, spread)
        d = self.dist(u, t)
        cast_used = walked = attacked = excl_cast = False
        if abilities and self.ready(u, "HEAL"):
            ab = u.abil[self.slot(u, "HEAL")]
            amount = ab["mod"] * u.focus
            cands = [v for v in self.allies(u) if self.dist(u, v) <= ab["range"] and v.maxhp - float(v.hp) >= 0.6 * amount]
            if cands:
                v = max(cands, key=lambda w: (w.maxhp - float(w.hp), -w.order))
                self.cast(u, "HEAL")
                v.hp = F32(min(v.maxhp, float(v.hp) + amount))
                cast_used = True
        if abilities and not cast_used and self.ready(u, "HDASH") and d <= 3:
            ab = self.cast(u, "HDASH")
            cast_used = True
            u.x = t.x - self.toward(u, t)
            d = 1
            for v in list(fs):
                if abs(v.x - u.x) <= 1:
                    self.hit(u, v, ab["mod"] * u.focus)
                    self.stun(u, v)
        if abilities and not cast_used and d <= 2 and self.ready(u, "SHIELD"):
            ab = self.cast(u, "SHIELD")
            u.shield = ab["mod"] * u.focus
            u.shield_turns = ab["dur"]
            cast_used = True
        if abilities and not cast_used and d <= 4 and t.alive:
            can_excl = u.ranged or d <= 1
            hold = stagger and t.stun_pending
            for role in ("STUN", "AOESTUN", "DMG", "AOE"):
                if not can_excl or not self.ready(u, role):
                    continue
                if hold and role in ("STUN", "AOESTUN"):
                    continue
                ab = self.cast(u, role)
                cast_used = True
                excl_cast = ab["excl"]
                targets = [t] if role in ("STUN", "DMG") else [v for v in fs if abs(v.x - t.x) <= 1]
                for v in targets:
                    if ab["mod"] > 0:
                        self.hit(u, v, ab["mod"] * u.focus)
                    if role in ("STUN", "AOESTUN"):
                        self.stun(u, v)
                break
        if abilities and not cast_used and not u.ranged and self.ready(u, "DASH") and 2 <= d <= 4 and pol != HOLD:
            self.cast(u, "DASH")
            cast_used = True
            step = min(2, d - 1)
            u.x += step * self.toward(u, t)
            d = self.dist(u, t)
        if not t.alive:
            fs = self.foes(u)
            if not fs:
                self.end_turn(u)
                return
            t = self.choose(u, fs, spread)
            d = self.dist(u, t)
        reach = u.rng if u.ranged else 1
        if d > reach and not excl_cast and pol != HOLD:
            u.x += self.toward(u, t)
            d = self.dist(u, t)
            walked = True
        if t.alive and d <= u.rng and not (u.ranged and walked):
            self.basic(u, t, fs)
            attacked = True
        if not t.alive:
            fs = self.foes(u)
            near = [v for v in fs if self.dist(u, v) <= u.rng]
            t = min(near, key=lambda v: float(v.hp)) if near else t
            d = self.dist(u, t)
        reset_role = "RESET" if self.ready(u, "RESET") else ("ENHRESET" if self.ready(u, "ENHRESET") else None)
        if (abilities and u.alive and t.alive and reset_role and not cast_used and d <= u.rng
                and (attacked or (u.ranged and walked))):
            self.cast(u, reset_role)
            self.basic(u, t, self.foes(u))
            if reset_role == "ENHRESET":
                self.stun(u, t)
        self.end_turn(u)

    def basic(self, u: Unit, t: Unit, fs: list) -> None:
        self.hit(u, t, u.strength)
        p = PASSIVE[u.name]
        if p["type"] == 4:  # Varg: splash Strength on other foes within 1 of the target
            for v in fs:
                if v is not t and v.alive and abs(v.x - t.x) <= 1:
                    self.hit(u, v, p["magnitude"] * u.strength)

    def end_turn(self, u: Unit) -> None:
        u.cd = [max(0, c - 1) for c in u.cd]
        if u.shield_turns > 0:
            u.shield_turns -= 1

    def sides_alive(self) -> tuple:
        return any(u.alive for u in self.a), any(u.alive for u in self.b)

    def run(self, max_rounds: int = 80) -> dict:
        end = self.round + max_rounds
        while self.round < end:
            first, second = (self.a, self.b) if self.round % 2 == 1 else (self.b, self.a)
            n = max(len(first), len(second))
            done = False
            for i in range(n):
                for side in (first, second):
                    if i < len(side):
                        self.turn(side[i])
                        if not all(self.sides_alive()):
                            done = True
                            break
                if done:
                    break
            if done:
                break
            self.round += 1
        a_alive, b_alive = self.sides_alive()
        winner = "A" if a_alive and not b_alive else ("B" if b_alive and not a_alive else None)
        hp = {s: sum(float(u.hp) for u in grp if u.alive) for s, grp in (("A", self.a), ("B", self.b))}
        mh = {s: sum(u.maxhp for u in grp) for s, grp in (("A", self.a), ("B", self.b))}
        return {"winner": winner, "round": self.round, "hp": hp, "hp_frac": {s: hp[s] / mh[s] for s in hp},
                "deaths": self.log, "lost_turns": {f"{u.side}:{u.name}": u.turns_lost for u in self.units}}


def team(picks: list, side: str, builds=None, pols=None) -> list:
    out, k = [], 0
    for j, p in enumerate(picks):
        for nm in PICK_BODIES[p]:
            out.append(make_unit(nm, side, k, (builds or [0] * len(picks))[j], (pols or [0] * len(picks))[j],
                                 5 if nm == "issac" else j))
            k += 1
    return out


def duel(x: int, y: int, build_a: int = 0, build_b: int = 0, parity: int = 0, xa: int = 17, xb: int = 23) -> dict:
    return Fight(team([x], "A", [build_a]), team([y], "B", [build_b]), start_round=30 + parity, xa=xa, xb=xb).run()


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[0] == "duel":
        x, y = int(argv[1]), int(argv[2])
        ba = int(argv[argv.index("--build-a") + 1]) if "--build-a" in argv else 0
        bb = int(argv[argv.index("--build-b") + 1]) if "--build-b" in argv else 0
        par = int(argv[argv.index("--parity") + 1]) if "--parity" in argv else 0
        print(json.dumps(duel(x, y, ba, bb, par)))
        return 0
    if argv and argv[0] == "matrix":
        bld = int(argv[argv.index("--build") + 1]) if "--build" in argv else 0
        res = {}
        for x in range(12):
            for y in range(12):
                res[f"{x},{y}"] = [duel(x, y, bld, bld, p) for p in (0, 1)]
        if "--out" in argv:
            Path(argv[argv.index("--out") + 1]).write_text(json.dumps(res), encoding="utf-8")
        for x in range(12):
            row = []
            for y in range(12):
                v = res[f"{x},{y}"]
                m = sum((r["hp_frac"]["A"] if r["winner"] == "A" else -r["hp_frac"]["B"] if r["winner"] == "B" else 0)
                        for r in v) / len(v)
                row.append(f"{m:+6.2f}")
            print(f"{PICK_NAMES[x][:10]:<11}" + " ".join(row))
        return 0
    print(__doc__)
    return 2




def compare(run: Path) -> dict:
    """Model vs engine for every duel of a duel.py run (picks, builds, FIGHT policies, placement 4)."""
    sys.path.insert(0, str(HERE))
    import duel as dl  # noqa: E402
    rows, agree, n, hp_err = [], 0, 0, []
    for r in dl.parse(run):
        s, o = r["scenario"], r.get("outcome")
        if not o or "pick" not in s or s.get("pol", [0, 0]) != [0, 0] or s.get("place", 4) != 4:
            continue
        x, y = s["pick"]
        ba, bb = s.get("build", [0, 0])
        start = dl.start_round(s)
        m = Fight(team([x], "A", [ba]), team([y], "B", [bb]), start_round=start).run()
        eng_w = o.get("winner")
        n += 1
        ok = m["winner"] == eng_w
        agree += ok
        ehp = sum(v for k, v in o["survivors"].items() if eng_w and k.startswith(eng_w + ":")) if eng_w else 0.0
        mhp = m["hp"].get(m["winner"], 0.0) if m["winner"] else 0.0
        if ok and eng_w:
            hp_err.append(abs(ehp - mhp))
        rows.append({"i": r["i"], "pick": [x, y], "build": [ba, bb], "parity": s.get("parity", 0), "engine": eng_w,
                     "model": m["winner"], "engine_hp": round(ehp, 1), "model_hp": round(mhp, 1)})
    exact = sum(1 for e in hp_err if e < 0.5)
    return {"games": n, "winner_agree": agree, "hp_exact": exact, "hp_err_mean": (sum(hp_err) / len(hp_err)) if hp_err else None,
            "rows": rows}


def compare_main(argv: list[str]) -> int:
    res = compare(Path(argv[1]).resolve())
    for row in res["rows"]:
        if row["engine"] != row["model"] or abs(row["engine_hp"] - row["model_hp"]) > 0.5:
            print("DIFF", json.dumps(row))
    print(f"games {res['games']}  winner agree {res['winner_agree']}  survivor hp exact {res['hp_exact']}  "
          f"mean |hp err| {res['hp_err_mean']}")
    return 0


# Table rows of _duel-lab/src/table.h used by the group check: (picks, policies, builds) per side.
TABLE = {
    0: ([[0, 4, 0]], [[6, 4, 0]]), 1: ([[0, 4, 0], [6, 4, 0]], [[0, 4, 0]]),
    2: ([[0, 4, 0], [6, 4, 0]], [[0, 4, 0], [6, 4, 0]]),
    3: ([[0, 4, 0], [6, 4, 0], [9, 4, 0]], [[0, 4, 0], [6, 4, 0]]),
    4: ([[0, 4, 0], [6, 4, 0], [9, 4, 0]], [[0, 4, 0], [6, 4, 0], [9, 4, 0]]),
    5: ([[0, 4, 0], [6, 4, 0], [9, 4, 0]], [[0, 4, 0]]),
    6: ([[0, 9, 0], [6, 9, 0], [9, 9, 0]], [[0, 4, 0], [6, 4, 0]]),
    7: ([[0, 9, 0], [6, 9, 0]], [[0, 4, 0], [6, 4, 0]]),
    8: ([[0, 9, 0], [6, 9, 0], [9, 9, 0]], [[0, 4, 0], [6, 4, 0], [9, 4, 0]]),
    9: ([[11, 0, 0], [1, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]], [[11, 0, 0], [1, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]]),
    10: ([[11, 0, 0], [1, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]], [[11, 0, 0], [1, 0, 0], [6, 0, 0], [0, 0, 0]]),
    11: ([[11, 0, 0], [1, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]], [[11, 0, 0], [1, 0, 0], [6, 0, 0]]),
    12: ([[6, 0, 0], [0, 0, 0], [2, 0, 0]], [[6, 0, 0], [0, 0, 0], [2, 0, 0]]),
    13: ([[6, 0, 0], [0, 0, 0], [2, 0, 0]], [[6, 0, 0], [0, 0, 0]]),
    14: ([[11, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]], [[11, 0, 0], [6, 0, 0], [0, 0, 0], [2, 0, 0]]),
    15: ([[6, 0, 0], [2, 0, 0]], [[11, 0, 0]]),
    24: ([[2, 0, 0], [7, 0, 0]], [[2, 0, 0], [1, 0, 0]]),
    25: ([[6, 0, 0], [7, 0, 0]], [[6, 0, 0], [10, 0, 0]]),
    26: ([[9, 0, 0], [7, 0, 0]], [[9, 0, 0], [1, 0, 0]]),
}


def groups_main(argv: list[str]) -> int:
    """Model vs engine for the group table rows of a duel.py run of s_g1.json (line model, columns at
    x 16 and 24 as in table.h's formation)."""
    sys.path.insert(0, str(HERE))
    import duel as dl  # noqa: E402
    agree = n = 0
    for r in dl.parse(Path(argv[1]).resolve()):
        s, o = r["scenario"], r.get("outcome")
        if not o or s.get("table") not in TABLE:
            continue
        a, b = TABLE[s["table"]]
        ta = team([p[0] for p in a], "A", [p[2] for p in a], [p[1] for p in a])
        tb = team([p[0] for p in b], "B", [p[2] for p in b], [p[1] for p in b])
        m = Fight(ta, tb, start_round=30 + int(s.get("parity", 0)), xa=16, xb=24).run()
        n += 1
        agree += m["winner"] == o["winner"]
        print(f"T{s['table']:<2} p{s.get('parity', 0)} engine {o['winner']} {o['hp_frac']} | model {m['winner']} "
              f"{ {k: round(v, 3) for k, v in m['hp_frac'].items()} }")
    print(f"rows {n}  winner agree {agree}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "compare":
        sys.exit(compare_main(sys.argv[1:]))
    if len(sys.argv) > 2 and sys.argv[1] == "groups":
        sys.exit(groups_main(sys.argv[1:]))
    sys.exit(main(sys.argv[1:]))
