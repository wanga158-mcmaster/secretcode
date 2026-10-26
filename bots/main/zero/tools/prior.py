"""Executor prior for main/zero's legacy option interface (config[5] = 0, config[10] = 1; tracks/long.md).

  python -B -X utf8 bots/main/zero/tools/prior.py [--out .cache/main/zero/prior/prior.txt] [--temperature 0] [--seed 1]

A weight INITIALISATION, not code: the network keeps every choice and training may overwrite all of it.
Trunk unit 0 is a constant (~0.95) and units 1-7 pass the current goal kind through (state facts 27-33 with
goal_state_facts on), so each head's V term (t @ vv) . phi starts as a linear prior over candidate facts plus
goal-conditioned terms: steps follow the chosen option (path distance to its target, its tiles, the recall
channel under Recall, tribute when beside an altar with gems), attacks hit the option's target, the tail
pre-starts a chosen recall, the shop buys, casts fire when they deal damage, and the goal head starts as a
rough rate score over the option facts. Remaining trunk units keep a random init so gradients flow; the
U/P bilinear paths start at zero.
Fact scales follow cand.cpp (26 Oct: goal Farm 13-16 /2.5, attack hits-to-kill and cast shield /1.25).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train as T  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
GOAL_STATE = 27  # first goal-kind fact in the legacy state with goal_state_facts (cand.cpp state_vector)
G_FARM, G_BANK, G_RECALL, G_ENGAGE, G_AVOID, G_HOLD, G_ASSIST = range(7)

# head -> {phi index: weight} for the constant unit; (head, goal kind) -> {phi index: weight} conditional.
CONST = {
    'goal': {0: 1.0, 8: -2.5, 9: 1.0, 10: 3.0, 13: -2.5, 14: -5.0, 17: -3.0, 18: -1.0, 19: -1.5, 21: 2.0, 22: -1.0,
             23: 0.7},
    'step': {1: -4.0, 2: 6.0, 3: -3.0, 5: -1.5, 12: 1.0, 16: 0.2},
    'attack': {1: -1.0, 2: 0.5, 3: -0.25, 4: 1.5, 7: 3.0},
    'cast0': {8: 2.0, 9: 2.0, 10: 1.0, 11: 0.5, 13: 2.0, 14: 1.25, 15: -1.0},
    'shop': {**{k: 1.0 for k in range(1, 8)}, 8: 3.0, 9: 3.0, 10: 3.0},
    'tail': {1: -3.0},
}
# Goal kinds as separate one-hot slots of the goal candidates: per-kind facts share slots 8-22, so the
# kind-specific slot weights ride on the kind's own one-hot via the goal-conditioned table below? No: goal
# facts are per candidate, so kind-specific weights need products. They are approximated here by giving each
# kind a bias and letting the shared slots carry the Farm reading; Bank/Recall/Hold get their main fact from
# a dedicated slot weight on top (their facts live in slots 8-12 like Farm's, at different meanings).
KIND_BIAS = {G_FARM: 1.0, G_BANK: -1.5, G_RECALL: -3.0, G_ENGAGE: -2.5, G_AVOID: -3.0, G_HOLD: -2.5, G_ASSIST: -3.5}
COND = {
    ('step', G_RECALL): {1: 10.0},
    ('attack', G_FARM): {1: 2.0},
    ('attack', G_ENGAGE): {2: 3.0},
    ('tail', G_RECALL): {1: 8.0},
}


def build(seed: int):
    rng = np.random.default_rng(seed)
    P = T.init(rng)
    for k in [n for n in P if n.startswith(('u_', 'vv_', 'c_'))]:
        P[k][:] = 0.0
    P['v'][:] = 0.0
    P['m'][:] = 0.0
    big = 20.0
    # Unit 0: constant. Units 1..7: goal kind pass-through.
    P['w1'][:, 0] = 0.0
    P['b1'][0] = big
    P['w2'][:, 0] = 0.0
    P['b2'][0] = big
    for k in range(7):
        u = 1 + k
        P['w1'][:, u] = 0.0
        P['w1'][GOAL_STATE + k, u] = big
        P['b1'][u] = 0.0
        P['w2'][:, u] = 0.0
        P['w2'][u, u] = big
        P['b2'][u] = 0.0
    one = T.ss(np.float32(big))  # first-layer output of a saturated unit
    t_on = float(T.ss(np.float32(big * one)))
    scale = 1.0 / t_on
    for head, ws in CONST.items():
        names = ['cast0', 'cast1'] if head == 'cast0' else [head]
        for name in names:
            for j, w in ws.items():
                P[f'vv_{name}'][0, j] += w * scale
    for (head, kind), ws in COND.items():
        for j, w in ws.items():
            P[f'vv_{head}'][1 + kind, j] += w * scale
    for kind, b in KIND_BIAS.items():
        P['vv_goal'][0, kind] += (b - CONST['goal'].get(kind, 0.0)) * scale
    # Kind-specific readings of shared goal slots, carried by the kind's own one-hot through the P/U path:
    # hidden unit k of the goal head's projection = a(sum_j P[j,k] phi_j + c_k); U row of the constant
    # trunk unit gives its weight. Bank: carried gems (slot 9) and lock (11); Recall: missing hp (8);
    # Hold: on spawn (8) minus hp fraction (9); Avoid: enemy damage (8).
    proj = [
        (G_BANK, {9: 6.0, 11: -5.0, 8: -1.0}),
        (G_RECALL, {8: 6.0, 9: 1.5, 12: -2.0}),
        (G_HOLD, {8: 4.0, 9: -5.0}),
        (G_AVOID, {8: 4.0}),
    ]
    for k, (kind, ws) in enumerate(proj):
        # a(x) with x = gate + facts, gate = -8 unless the candidate is this kind (+8 cancels): output ~ facts.
        P['p_goal'][kind, k] = 8.0
        P['c_goal'][k] = -8.0
        for j, w in ws.items():
            P['p_goal'][j, k] = w / 8.0  # small pre-activation, softsign ~ linear near 0 (scaled back by U)
        P['u_goal'][0, k] = 8.0 * scale
        # Every other kind reads a(-8) ~ -0.889 on this unit, so this kind gains 8 x 0.889 relative to them.
        P['vv_goal'][0, kind] -= 8.0 * float(T.ss(np.float32(8.0))) * scale
    # Ranged champions score steps and attacks in one set: steps follow the step prior; attacks carry
    # attack facts in the same slots plus a kind flag (22 camp, 23 champion), which offsets the step
    # weights those slots carry for steps.
    for j, w in CONST['step'].items():
        P['vv_ranged'][0, j] += w * scale
    for j, w in CONST['attack'].items():
        P['vv_ranged'][0, j] += w * scale
    P['vv_ranged'][0, 22] += (4.0 + 1.0) * scale  # cancels step slot 1 (-4) on camp attacks, +1
    P['vv_ranged'][0, 23] += (-6.0 + 0.5) * scale  # cancels step slot 2 (+6) on champion attacks
    P['vv_ranged'][1 + G_RECALL, 1] += 10.0 * scale
    P['vv_ranged'][1 + G_RECALL, 22] -= 10.0 * scale
    P['vv_ranged'][1 + G_FARM, 22] += 2.0 * scale
    P['vv_ranged'][1 + G_ENGAGE, 23] += 3.0 * scale
    return P


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=str(ROOT / '.cache/main/zero/prior/prior.txt'))
    ap.add_argument('--temperature', type=float, default=0.0)
    ap.add_argument('--log-rate', type=float, default=0.0)
    ap.add_argument('--period', type=float, default=4.0)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    P = build(a.seed)
    cfg = [0.0] * 11
    cfg[0], cfg[1], cfg[2], cfg[3], cfg[10] = a.temperature, a.log_rate, float(a.seed), a.period, 1.0
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    T.save(P, out, cfg)
    print(out)


if __name__ == '__main__':
    main()
