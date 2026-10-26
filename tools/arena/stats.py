"""Statistics for arena results: Wilson intervals, paired A/B comparisons and Bradley-Terry ratings.

Games are deterministic for fixed (bots, map, side): replaying a game adds no information, and about
90% of outcomes are fixed by map, side and opponent. Compare variants on identical game keys
(paired), judge flips with an exact McNemar test and gem margins with a paired standard error.
"""
from __future__ import annotations

import math


def wilson(k: float, n: int, z: float = 1.96) -> tuple[float, float]:
    """Wilson score interval for k successes (draws count 0.5) out of n."""
    if n <= 0:
        return 0.0, 1.0
    p = k / n
    den = 1 + z * z / n
    mid = (p + z * z / (2 * n)) / den
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return max(0.0, mid - half), min(1.0, mid + half)


def binom_two_sided(k: int, n: int) -> float:
    """Exact two-sided p-value of k successes in n fair coin flips (McNemar on discordant pairs)."""
    if n <= 0:
        return 1.0
    probs = [math.comb(n, i) / 2 ** n for i in range(n + 1)]
    pk = probs[k]
    return min(1.0, sum(p for p in probs if p <= pk * (1 + 1e-9)))


def paired(base: dict, var: dict, win=lambda r: r["result"] == "win",
           margin=lambda r: r["gems"] - r["opp_gems"]) -> dict:
    """Compare two perspective-row dicts keyed by identical game keys (opponent, map, side, ...).
    Returns n, wins, flips each way, McNemar p, mean gem margins and the paired margin difference."""
    keys = sorted(set(base) & set(var), key=str)
    n = len(keys)
    if not n:
        return {"n": 0}
    gained = [k for k in keys if not win(base[k]) and win(var[k])]
    lost = [k for k in keys if win(base[k]) and not win(var[k])]
    diffs = [margin(var[k]) - margin(base[k]) for k in keys]
    mean = sum(diffs) / n
    sd = math.sqrt(sum((d - mean) ** 2 for d in diffs) / max(1, n - 1))
    return {
        "n": n,
        "wins_base": sum(1 for k in keys if win(base[k])),
        "wins_var": sum(1 for k in keys if win(var[k])),
        "gained": gained, "lost": lost,
        "mcnemar_p": binom_two_sided(min(len(gained), len(lost)), len(gained) + len(lost)),
        "margin_base": sum(margin(base[k]) for k in keys) / n,
        "margin_var": sum(margin(var[k]) for k in keys) / n,
        "diff_mean": mean, "diff_se": sd / math.sqrt(n),
    }


def format_paired(p: dict) -> str:
    if not p.get("n"):
        return "no common games"
    return (f"games {p['n']}  wins {p['wins_base']} -> {p['wins_var']} (+{len(p['gained'])}/-{len(p['lost'])},"
            f" McNemar p={p['mcnemar_p']:.3f})  gem margin {p['margin_base']:+.0f} -> {p['margin_var']:+.0f}"
            f"  paired diff {p['diff_mean']:+.1f} +- {p['diff_se']:.1f} se")


ELO = 400 / math.log(10)


def bradley_terry(games, prior: float = 2.0, iters: int = 2000, tol: float = 1e-10,
                  anchor: str | None = None, anchor_elo: float = 1000.0) -> dict:
    """Ratings from (player_a, player_b, score_a) games, score_a in {0, 0.5, 1}.

    Minorization-maximization fit of the Bradley-Terry model; every player also gets `prior`
    virtual games (half won) against a fixed average opponent, so unbeaten players stay finite.
    Elo-scaled; shifted so `anchor` sits at `anchor_elo` when present, else the mean is 1500.
    Returns {player: {"elo", "se", "games", "score"}} (se from the diagonal Fisher information)."""
    pairs: dict[tuple[str, str], list[float]] = {}
    score: dict[str, float] = {}
    count: dict[str, int] = {}
    for a, b, s in games:
        if a == b:
            continue
        key = (a, b) if a < b else (b, a)
        rec = pairs.setdefault(key, [0, 0.0])  # games, score of key[0]
        rec[0] += 1
        rec[1] += s if key[0] == a else 1 - s
        score[a] = score.get(a, 0.0) + s
        score[b] = score.get(b, 0.0) + 1 - s
        count[a] = count.get(a, 0) + 1
        count[b] = count.get(b, 0) + 1
    players = sorted(count)
    if not players:
        return {}
    gamma = {p: 1.0 for p in players}
    nbrs: dict[str, list[tuple[str, int]]] = {p: [] for p in players}
    for (a, b), (n, _) in pairs.items():
        nbrs[a].append((b, n))
        nbrs[b].append((a, n))
    for _ in range(iters):
        delta = 0.0
        new = {}
        for p in players:
            den = sum(n / (gamma[p] + gamma[q]) for q, n in nbrs[p]) + prior / (gamma[p] + 1.0)
            g = (score[p] + prior / 2) / den
            new[p] = g
        # the prior's virtual opponent (gamma = 1) fixes the scale, so no renormalization: this is
        # the MM iteration of the MAP of the stated model
        for p in players:
            g = new[p]
            delta = max(delta, abs(math.log(g) - math.log(gamma[p])))
            gamma[p] = g
        if delta < tol:
            break
    out = {}
    for p in players:
        info = sum(n * gamma[p] * gamma[q] / (gamma[p] + gamma[q]) ** 2 for q, n in nbrs[p])
        info += prior * gamma[p] / (gamma[p] + 1.0) ** 2
        out[p] = {"elo": ELO * math.log(gamma[p]), "se": ELO / math.sqrt(info), "games": count[p],
                  "score": score[p] / count[p]}
    shift = anchor_elo - out[anchor]["elo"] if anchor in out else 1500 - sum(v["elo"] for v in out.values()) / len(out)
    for v in out.values():
        v["elo"] += shift
    return out


def expected(elo_a: float, elo_b: float) -> float:
    return 1 / (1 + 10 ** ((elo_b - elo_a) / 400))
