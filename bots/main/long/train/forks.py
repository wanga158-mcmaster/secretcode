"""Forked counterfactual games for main/long (cfg::fork, src/fork.h; bots/main/tracks/long.md).

  python -B -X utf8 bots/main/long/train/forks.py collect --run NAME [--maps pool,train] [--sample K]
         [--vs OPP,...] [--per-game M] [--options 0,1,2,3,4,5,6] [--commit D] [--window W] [--full]
         [--weights W.txt] [--seed S]

collect: plays base games (directive off) with bundles, reads every contact decision ("F" lines) of our
team, samples M decisions per game and replays the game once per option with the directive (round,
champion, option, commitment) in the learned block: the prefix is identical, the option plays for D
rounds, then v44's policy resumes. Each fork is cut at the first multiple of 50 at or after R+W (team
totals of the truncated row) and, with --full, also played to the end. Output: one JSON line per
decision in .cache/main/long/runs/NAME/data.jsonl (features, v44's stance, per-option outcomes).
Bundles and candidate files are deleted as soon as they are read.
"""
from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

COUNT = 396          # learn.h kCount
FEATURES = 48        # fork.h kFeatures
DIRECT = 94          # learn.h kOffset[direct]
STEP_ROUNDS = 50     # truncation granularity


def build(bot: str, overrides: dict | None = None) -> str:
    e = core.prepare(core.bot(bot), overrides or {})
    if not e.ok:
        sys.exit(f'build of {bot} {overrides or ""} failed:\n{e.log[-1500:]}')
    return exp.wasm(e)


def weights(base: list[float], r: int = 0, champ: int = 0, option: int = 0, commit: int = 0, value: int = 0) -> list[float]:
    w = list(base) + [0.0] * (COUNT - len(base))
    w[DIRECT:DIRECT + 5] = [float(r), float(champ), float(option), float(commit), float(value)]
    return w


def values(stdout: str) -> dict | None:
    """The V lines of one team at the value round: ours [alive, hp, max_hp, gems, gold] per draft index,
    both teams' tributed gems and the enemies' alive flags."""
    ours, tributed, enemies = {}, None, None
    for line in stdout.splitlines():
        if not line.startswith('V '):
            continue
        head, *rest = line.split('|')
        v = [int(x) for x in head.split()[1:]]
        ours[v[1]] = v[2:7]
        if len(rest) >= 2:
            tributed = [int(x) for x in rest[0].split()]
            enemies = [int(x) for x in rest[1].split()]
    if tributed is None:
        return None
    return {'ours': [ours[k] for k in sorted(ours)], 'tributed': tributed, 'enemies': enemies}


def team_stdout(bundles: Path, r: dict) -> str:
    d = bundles / f'{Path(r["map"]).stem}_{r["a_side"]}_{r["tag"]}'
    f = d / ('team-a.stdout' if r['a_side'] == 'A' else 'team-b.stdout')
    return f.read_text(encoding='utf-8', errors='replace') if f.is_file() else ''


def write_weights(path: Path, w: list[float]) -> None:
    path.write_text(' '.join(f'{x:.6g}' for x in w) + '\n', encoding='utf-8')


def decisions(stdout: str) -> list[dict]:
    out = []
    for line in stdout.splitlines():
        if not line.startswith('F '):
            continue
        v = line.split()
        try:
            nums = [int(x) for x in v[1:]]
        except ValueError:
            continue
        if len(nums) != 3 + FEATURES:  # cut by the runner's log limit
            continue
        out.append({'round': nums[0], 'champ': nums[1], 'v44': nums[2], 'x': [n / 1000.0 for n in nums[3:]]})
    return out


def side_values(r: dict) -> dict:
    """Our side's totals minus theirs from a fastrun row (our bot is key 's', team a_side)."""
    a = 0 if r['a_side'] == 'A' else 1
    d = {k: r[k][a] - r[k][1 - a] for k in ('gems', 'kills', 'gold', 'collected') if k in r}
    d['deaths'] = r['deaths'][a] if 'deaths' in r else 0
    d['opp_deaths'] = r['deaths'][1 - a] if 'deaths' in r else 0
    d['win'] = 1 if r['winner'] == 'a' else (0 if r['winner'] == 'b' else 0.5)
    d['rounds'] = r['rounds']
    d['unhealthy'] = int(bool(r['exhausted'][a] or r['faults'][a] or r['errors'][a] or r.get('error')))
    return d


def cmd_collect(a) -> None:
    run = ROOT / '.cache/main/long/runs' / a.run
    run.mkdir(parents=True, exist_ok=True)
    rng = random.Random(a.seed)
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    if any('generated/heldout' in m.replace('\\', '/') for m in maps):
        sys.exit('heldout maps are for evaluation only')
    if 0 < a.sample < len(maps):
        maps = rng.sample(maps, a.sample)
    overrides = {'learned': '1', 'fork': '1', **dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)}
    bots = {'s': build(a.bot, overrides)}
    opps = {}
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        opps[f'o{j}'] = sel
        bots[f'o{j}'] = build(sel)
    base_w = [float(x) for x in Path(a.weights).read_text(encoding='utf-8').split()] if a.weights else []
    tmp = run / 'tmp'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    t0 = time.time()
    write_weights(tmp / 'base.txt', weights(base_w))
    keys = [(m, side, o) for m in maps for side in 'AB' for o in opps]
    rng.shuffle(keys)
    options = [x for x in a.options.split(',') if x]  # "K" or "KbN": option K plus N burn iterations
    for b0 in range(0, len(keys), a.batch):
        batch(a, rng, run, tmp, bots, opps, base_w, keys[b0:b0 + a.batch], options)
        print(f'batch {b0 // a.batch + 1}/{-(-len(keys) // a.batch)} done ({time.time() - t0:.0f}s)', flush=True)
    shutil.rmtree(tmp, ignore_errors=True)


def batch(a, rng, run, tmp, bots, opps, base_w, keys, options) -> None:
    # 1. base games with bundles: the decision lists, sampled as they are read
    lines = [f'{m} {side} s {tmp / "base.txt"} {o} - g{i}' for i, (m, side, o) in enumerate(keys)]
    rows = exp.run_games(lines, bots, tmp / 'base', bundles=tmp / 'bundles')
    base = {}
    picks = []
    for r in rows:
        i = int(r['tag'][1:])
        ds = [d for d in decisions(team_stdout(tmp / 'bundles', r)) if d['round'] + a.window <= 1990]
        base[i] = {'row': side_values(r)}
        for d in rng.sample(ds, min(a.per_game, len(ds))):
            picks.append((i, d))
    shutil.rmtree(tmp / 'bundles', ignore_errors=True)
    # 2. forks
    by_cut: dict[int, list[str]] = {}
    for p, (i, d) in enumerate(picks):
        m, side, o = keys[i]
        cut = -(-(d['round'] + a.window) // STEP_ROUNDS) * STEP_ROUNDS
        for k in options:
            f = tmp / f'w{p}_{k}.txt'
            opt, _, burn = k.partition('b')
            w = weights(base_w, d['round'], d['champ'], int(opt), a.commit, cut - 1)
            w[DIRECT + 5] = float(burn or 0)
            write_weights(f, w)
            by_cut.setdefault(cut, []).append(f'{m} {side} s {f} {o} - f{p}.{k}')
    window: dict[str, dict] = {}
    for cut, ls in sorted(by_cut.items()):
        for r in exp.run_games(ls, bots, tmp / f'cut{cut}', rounds=cut, bundles=tmp / 'fb'):
            v = side_values(r)
            v['state'] = values(team_stdout(tmp / 'fb', r))
            window[r['tag']] = v
        shutil.rmtree(tmp / 'fb', ignore_errors=True)
    full: dict[str, dict] = {}
    if a.full:
        ls = [x for v in by_cut.values() for x in v]
        for r in exp.run_games(ls, bots, tmp / 'full'):
            full[r['tag']] = side_values(r)
    with (run / 'data.jsonl').open('a', encoding='utf-8') as out:
        for p, (i, d) in enumerate(picks):
            m, side, o = keys[i]
            rec = {'map': Path(m).stem, 'side': side, 'opp': opps[o], 'round': d['round'], 'champ': d['champ'], 'v44': d['v44'],
                   'x': d['x'], 'commit': a.commit, 'window': a.window, 'base_final': base[i]['row'],
                   'win': {k: window.get(f'f{p}.{k}') for k in options}}
            if a.full:
                rec['full'] = {k: full.get(f'f{p}.{k}') for k in options}
            out.write(json.dumps(rec) + '\n')
    for f in tmp.glob('w*.txt'):
        f.unlink()


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('collect')
    c.add_argument('--run', required=True)
    c.add_argument('--bot', default='main/long')
    c.add_argument('--maps', default='pool')
    c.add_argument('--sample', type=int, default=0)
    c.add_argument('--vs', default='anchors/main-v44')
    c.add_argument('--per-game', type=int, default=4)
    c.add_argument('--options', default='0,1,2,3,4,5,6')
    c.add_argument('--commit', type=int, default=5)
    c.add_argument('--window', type=int, default=30)
    c.add_argument('--full', action='store_true')
    c.add_argument('--weights')
    c.add_argument('--knobs', default='', help='extra knob overrides, e.g. stance=1')
    c.add_argument('--batch', type=int, default=120, help='base games per batch (bounds memory and bundle disk)')
    c.add_argument('--seed', type=int, default=1)
    a = p.parse_args()
    if a.cmd == 'collect':
        cmd_collect(a)


if __name__ == '__main__':
    main()
