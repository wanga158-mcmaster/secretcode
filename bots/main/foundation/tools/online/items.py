"""Items: build orders per champion, stat shares, shop timing and cadence, gold spent by round.

usage: python -I -X utf8 items.py [--teams N]
Input: .cache/contenders/online/sides.jsonl. Prints tables; writes data/online/items.json.
A shop session is a run of one champion's purchases with gaps of <= 3 rounds.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, OUT, load_jsonl, mean, median, quant

STATS = ("Health", "Strength", "Focus")


def sessions(buys):
    """[(start_round, gold, n_items)] from a champion's sorted buy list."""
    out = []
    for r, name, n, amt, x, y in buys:
        if out and r - out[-1][3] <= 3:
            s = out[-1]
            out[-1] = (s[0], s[1] + amt, s[2] + n, r)
        else:
            out.append((r, amt, n, r))
    return [(a, b, c) for a, b, c, d in out]


def emb_seq(c, k=4):
    return [e[1].replace(" Emblem of ", "/") for e in c["emblems"][:k]]


def stat_share(c, upto=None):
    cnt = collections.Counter()
    for r, name, n, amt, x, y in c["buys"]:
        if upto is not None and r > upto:
            continue
        if " Shard of " in name:
            cnt[name.split(" Shard of ")[1]] += n
    tot = sum(cnt.values())
    return {s: cnt[s] / tot for s in STATS} if tot else None, tot


def fam_share(c, upto=None):
    cnt = collections.Counter()
    for r, name, n, amt, x, y in c["buys"]:
        if upto is not None and r > upto:
            continue
        if " Shard of " in name:
            cnt[name.split(" Shard of ")[0]] += n
    return cnt


def main():
    rows = load_jsonl(OUT / "sides.jsonl")
    nteams = 10
    if "--teams" in sys.argv:
        nteams = int(sys.argv[sys.argv.index("--teams") + 1])
    out = {}
    # tier-level: stat share per champion by end of game and by r400; first emblem round; spend ratio
    print("shard stat share H/S/F % per champion type, by tier (whole game; n champion-games)")
    out["stat_share"] = {}
    tiers = ("T1", "T2", "T3", "T4", "T5", "US")
    champs = sorted({c["name"] for r in rows for c in r["champs"]})
    for ch in champs:
        line = []
        for t in tiers:
            v = [stat_share(c)[0] for r in rows if r["tier"] == t for c in r["champs"] if c["name"] == ch]
            v = [x for x in v if x]
            if len(v) < 5:
                line.append(f"{'-':>12s}")
                continue
            h, s, f = (100 * mean([x[k] for x in v]) for k in STATS)
            out["stat_share"].setdefault(ch, {})[t] = {"H": h, "S": s, "F": f, "n": len(v)}
            line.append(f"{h:3.0f}/{s:3.0f}/{f:3.0f}({len(v):3d})")
        print(f"  {ch:9s} " + " ".join(line))
    print("  tiers:   " + " ".join(f"{t:>12s}" for t in tiers))
    # first-emblem families by tier
    print("\nfirst emblem (family/stat) share % by tier, all champions")
    out["first_emblem"] = {}
    for t in tiers:
        fe = collections.Counter(emb_seq(c, 1)[0] for r in rows if r["tier"] == t for c in r["champs"] if c["emblems"])
        n = sum(fe.values())
        out["first_emblem"][t] = {k: v / n for k, v in fe.most_common(8)}
        print(f"  {t:3s} n {n:5d} " + ", ".join(f"{k} {100 * v / n:.0f}" for k, v in fe.most_common(6)))
    # timing: first purchase, emblems by round, sessions
    print("\nshop timing per champion (medians): first buy, emblem #1/#2/#3 round, sessions by r400, gold per session (r<=400), "
          "rounds between sessions (r<=1000)")
    out["timing"] = {}
    for t in tiers:
        cs = [c for r in rows if r["tier"] == t and r["rounds"] >= 400 for c in r["champs"]]
        fb = [c["buys"][0][0] if c["buys"] else None for c in cs]
        e = [[c["emblems"][i][0] if len(c["emblems"]) > i else None for c in cs] for i in range(3)]
        ss = [sessions(c["buys"]) for c in cs]
        s400 = [sum(1 for s in x if s[0] <= 400) for x in ss]
        g400 = [s[1] for x in ss for s in x if s[0] <= 400]
        gaps = [b[0] - a[0] for x in ss for a, b in zip(x, x[1:]) if b[0] <= 1000]
        out["timing"][t] = {"n": len(cs), "first_buy": median(fb), "emblem_rounds": [median(x) for x in e],
                            "no_emblem_by_end": sum(1 for c in cs if not c["emblems"]) / max(1, len(cs)),
                            "sessions400": mean(s400), "gold_per_session400": median(g400), "gap": median(gaps)}
        print(f"  {t:3s} champ-games {len(cs):4d} first buy r{median(fb)} emblems r{median(e[0])}/r{median(e[1])}/r{median(e[2])}"
              f" sessions<=400 {mean(s400):.1f} gold/session {median(g400)} gap {median(gaps)} rounds; "
              f"no emblem by end {100 * out['timing'][t]['no_emblem_by_end']:.0f}%")
    # gold spent and spend ratio by round, team level
    print("\nteam gold spent (median) and spent/earned by round, by tier")
    out["spent"] = {}
    for t in tiers:
        v = [r for r in rows if r["tier"] == t]
        line = []
        for ck in ("100", "200", "300", "400", "600", "1000"):
            w = [r for r in v if r["rounds"] >= int(ck)]
            sp = [r["spentGold"][ck] for r in w]
            ratio = [r["spentGold"][ck] / r["earnedGold"][ck] for r in w if r["earnedGold"][ck] > 0]
            out["spent"].setdefault(t, {})[ck] = {"spent": median(sp), "ratio": median(ratio), "n": len(w)}
            line.append(f"r{ck} {median(sp):.0f} ({100 * median(ratio):.0f}%)" if w else f"r{ck} -")
        print(f"  {t:3s} " + "  ".join(line))
    # consumables per game: locks, crystals; wards
    print("\nconsumables per side-game (mean): altar locks by r400 / whole game; teleport crystals; wards by r400")
    out["cons"] = {}
    for t in tiers:
        v = [r for r in rows if r["tier"] == t]
        lk400 = [sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Altar Lock" and rr <= 400) for r in v]
        lk = [sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Altar Lock") for r in v]
        tp = [sum(n for c in r["champs"] for (rr, nm, n) in c["cons"] if nm == "Teleport Crystal") for r in v]
        zero_lock = sum(1 for x in lk if x == 0) / max(1, len(lk))
        out["cons"][t] = {"locks400": mean(lk400), "locks": mean(lk), "zero_lock_share": zero_lock, "tp": mean(tp),
                          "wards400": median([r["wards400"] for r in v])}
        print(f"  {t:3s} n {len(v):4d} locks {mean(lk400):.1f}/{mean(lk):.1f} (no lock all game {100 * zero_lock:.0f}%) "
              f"crystals {mean(tp):.1f} wards<=400 median {median([r['wards400'] for r in v])}")
    # per top team: build orders per champion
    ranked = sorted({(r["rank"], r["name"], r["tid"]) for r in rows if r.get("rank") and r["tier"] != "US"})
    top = [x for x in ranked if x[0] <= nteams]
    out["teams"] = {}
    for rank, name, tid in top:
        v = [r for r in rows if r.get("tid") == tid]
        print(f"\n#{rank} {name}: {len(v)} sides")
        tt = {}
        for ch in sorted({c["name"] for r in v for c in r["champs"]}):
            cs = [c for r in v for c in r["champs"] if c["name"] == ch]
            seqs = collections.Counter(" > ".join(emb_seq(c, 3)) for c in cs if c["emblems"])
            share = [stat_share(c)[0] for c in cs]
            share = [x for x in share if x]
            e1 = median([c["emblems"][0][0] if c["emblems"] else None for c in cs])
            fb = median([c["buys"][0][0] if c["buys"] else None for c in cs])
            s400 = mean([sum(1 for s in sessions(c["buys"]) if s[0] <= 400) for c in cs])
            fams = collections.Counter()
            for c in cs:
                fams.update(fam_share(c))
            nf = sum(fams.values())
            top_seq = seqs.most_common(2)
            tt[ch] = {"n": len(cs), "seqs": top_seq, "first_buy": fb, "emblem1": e1, "sessions400": s400,
                      "HSF": [mean([x[k] for x in share]) for k in STATS] if share else None,
                      "families": {k: v2 / nf for k, v2 in fams.most_common(4)} if nf else {}}
            hsf = "/".join(f"{100 * x:.0f}" for x in tt[ch]["HSF"]) if share else "-"
            print(f"  {ch:9s} n {len(cs):3d} H/S/F {hsf:11s} first buy r{fb} emblem1 r{e1} sessions<=400 {s400:.1f} | "
                  + " ; ".join(f"{k} ({n})" for k, n in top_seq))
        out["teams"][f"{rank} {name}"] = tt
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "items.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
