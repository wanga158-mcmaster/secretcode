"""Model side of the siege verification (economy.md E8, E9, E28): econ.simulate_clear for the lab-map
siege configurations, with the walk back measured on data/economy/maps/econ-siege.map26a.

  python -I siege_compare.py
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402

MAIN5 = ["Heart", "Marcella", "Ryth", "Vendetta", "Varg"]
VESP5 = ["Heart", "Marcella", "Ryth", "Vendetta", "Vesper"]
TRIO = ["Ryth", "Vendetta", "Marcella"]
PAIR = ["Marcella", "Ryth"]
CASES = [
    ("mono", "monolith", [("monolith", 0, 0)], MAIN5, 11),
    ("mono-vesper", "monolith", [("monolith", 0, 0)], VESP5, 11),
    ("golem-five", "golem", [("golem", 0, 0)], MAIN5, 17),
    ("golem-trio", "golem", [("golem", 0, 0)], TRIO, 17),
    ("elk2-trio", "elk2 gold2", [("elk", 0, 0), ("elk", 0, 1), ("gold_cluster", 1, 0), ("gold_cluster", 1, 1)], TRIO, 21),
    ("elk1-pair", "elk1 gem1 gold1", [("elk", 0, 0), ("gem_cluster", 1, 0), ("gold_cluster", 0, 1)], PAIR, 25),
]


def run(cat, names, members, walk, order):
    if order == "most":
        key = lambda n: (not cat.kits[n].melee, -cat.kits[n].hp)
    else:
        key = lambda n: (not cat.kits[n].melee, cat.kits[n].hp)
    fs = [econ.make_fighter(cat, n, i) for i, n in enumerate(sorted(names, key=key))]
    r = econ.simulate_clear(cat, fs, econ.monsters_of(cat, members), walk_back=walk)
    return r


def main() -> int:
    cat = econ.load_catalog()
    for name, comp, members, roster, walk in CASES:
        for order in ("most", "least"):
            r = run(cat, roster, members, walk, order)
            print(f"{name:12s} {comp:16s} tank order {order:5s} walk {walk:2d}: rounds {r.rounds if r.killed else 'x'} "
                  f"damage {round(r.damage)} trips {r.trips} deaths {r.deaths} out {r.out_rounds}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
