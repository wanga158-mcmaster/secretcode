"""Which features carry signal: per option, correlation of each feature with the clipped advantage, and
clipped mean advantage by quartile of the strongest features.

  python -B -X utf8 bots/main/long/train/probe.py RUN[,RUN...]
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fit  # noqa: E402

NAMES = ['as_is', 'fight_close', 'fight_here', 'retreat', 'ignore', 'recall']
FEAT = ['n_ours', 'n_theirs', 'sim_hp_share', 'hp_frac', 'gems', 'enemy_gems', 'nearest', 'sim_rounds', 'our_losses',
        'their_losses', 'gem_gap', 'round', 'allies_engaged', 'spawn_dist', 'favourable', 'doomed', 'threat/hp', 'reach_now',
        'capturing', 'allies4', 'enemies4', 'min_enemy_frac', 'min_enemy_hits', 'dash', 'camp_threat', 'ally_fighting',
        'max_hp', 'gold', 'ranged', 'type', 'hp', 'allies', 'reach_us', 'we_reach', 'S4', 'S8', 'enemy_stuns',
        'nearest_ally', 'cut_off', 'hp_lost', 'nuke', 'stun', 'weakest_hits', 'on_their_spawn', 'enemy_dps/hp',
        'our_dps/weakest', 'nearest_bag', 'retreat_streak']


def main() -> None:
    recs = fit.load(sys.argv[1])
    X = np.array([r['x'][:fit.NF] for r in recs])
    print(f'{len(recs)} decisions, {fit.NF} features')
    for k in fit.OPTIONS:
        idx = [i for i, r in enumerate(recs) if k in r['adv']]
        y = np.array([recs[i]['adv'][k] for i in idx])
        Xk = X[idx]
        c = []
        for j in range(Xk.shape[1]):
            s = Xk[:, j].std()
            c.append(0.0 if s == 0 else float(np.corrcoef(Xk[:, j], y)[0, 1]))
        top = sorted(range(len(c)), key=lambda j: -abs(c[j]))[:4]
        print(f'{NAMES[k]:12s} mean {y.mean():+6.2f} se {y.std() / len(y) ** .5:4.2f}  ' +
              '  '.join(f'{FEAT[j]} {c[j]:+.3f}' for j in top))
        j = top[0]
        q = np.quantile(Xk[:, j], [0.25, 0.5, 0.75])
        bins = np.digitize(Xk[:, j], q)
        print('              by quartile of ' + FEAT[j] + ': ' +
              '  '.join(f'{y[bins == b].mean():+.1f}({(bins == b).sum()})' for b in range(4)))


if __name__ == '__main__':
    main()
