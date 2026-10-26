"""Compact timeline of one team-side of one game (for profiles and examples in online.md).

usage: python -I -X utf8 timeline.py MATCH_PREFIX TEAM_NAME [--caps 10]
Prints draft, captures (round, group type, gems), first purchases and emblems per champion,
tributes (first five and every bag >= 100), kills and deaths, gold/gems at checkpoints.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import OUT, load_jsonl


def main():
    pref, name = sys.argv[1], sys.argv[2]
    ncaps = int(sys.argv[sys.argv.index("--caps") + 1]) if "--caps" in sys.argv else 10
    rows = [r for r in load_jsonl(OUT / "sides.jsonl") if r["mid"].startswith(pref)]
    me = [r for r in rows if r.get("name") == name]
    if not me:
        print("not found", [(r["mid"][:8], r.get("name")) for r in rows])
        return
    r = me[0]
    o = [x for x in rows if x is not r][0]
    print(f"{r['mid'][:8]} {r['map']} {r['name']} ({r['side']}) vs {o.get('name')}: {r['res']} at r{r['rounds']}, gems {r['gems_final']}-{o['gems_final']}")
    print(f"  draft {' '.join(r['draft'])} | opponent {' '.join(o['draft'])}")
    print("  captures: " + ", ".join(f"r{c[0]} {c[1]}{'+' + str(c[3]) + 'g' if c[3] else ''}" for c in r["caps"][:ncaps]))
    for c in r["champs"]:
        b = c["buys"][:1]
        em = ", ".join(f"r{e[0]} {e[1].replace(' Emblem of ', '/')}" for e in c["emblems"][:4])
        print(f"  {c['name']:9s} first buy {('r' + str(b[0][0]) + ' ' + b[0][1]) if b else '-'}; emblems {em}; "
              f"K/D {c['kills']:.0f}/{c['deaths']:.0f}; max carry {c['maxcarry']:.0f}; earned r300 {c['earned']['300']:.0f}")
    tr = r["tributes"]
    big = [t for t in tr if t[1] >= 100]
    print("  tributes: first " + ", ".join(f"r{t[0]} {t[1]}" for t in tr[:5]) + f"; {len(tr)} in all; bags >= 100: "
          + ", ".join(f"r{t[0]} {t[1]}" for t in big))
    print("  kills: " + ", ".join(f"r{k['r']} {k['killer']}>{k['victim']}" for k in r["kills_list"][:12]))
    print("  deaths: " + ", ".join(f"r{d['r']} {d['victim']}({d['ctx']}{'' if d['ctx'] == 'camp' else ' by ' + d['killer']})" for d in r["deaths_list"][:12]))
    print("  gold/opp, banked/opp: " + "; ".join(f"r{k} {r['earnedGold'][k]:.0f}/{o['earnedGold'][k]:.0f}, {r['submittedGems'][k]:.0f}/{o['submittedGems'][k]:.0f}"
                                               for k in ("100", "200", "300", "400", "600", "800", "1000") if int(k) <= r["rounds"]))


if __name__ == "__main__":
    main()
