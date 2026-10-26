"""Decode top-vs-top replays into compact per-game timelines for the imitation tool (tools/replay_obs.cpp).

  python -B -X utf8 bots/main/zero/tools/decode.py [--jobs 16] [--limit N]

Reads results/online/topvtop/index.json and <matchId>.zip; writes .cache/main/zero/decoded/<matchId>.json.gz:
  {match, map, winner (0 = team A, 1 = B, -1 draw), ranks, tracks: {actor: {team, type, samples}},
   events: [[round, actor, kind, ...fields]]}
Event fields by kind (from the replay trace; one window of <= 64 rounds per call):
  move [r, a, 'move', x, y]            attack [r, a, 'attack', target_entity, hp_before, hp_after]
  cast [r, a, 'cast', slot, x, y]      shop [r, a, 'shop', [[item, count], ...]]
  recall / tribute [r, a, kind]        end-turn [r, a, 'end']
  capture [r, -1, 'capture', group, [[champion, gold_after, gems_after], ...]]
  gems-submitted [r, a, 'banked', gems]
"""
from __future__ import annotations

import argparse
import gzip
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / 'results/online/topvtop'
OUT = ROOT / '.cache/main/zero/decoded'
NODE = r'C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
INSPECT = str(ROOT / 'tools/inspect-strategy-replay.mjs')
KINDS = 'move,attack,cast,shop,recall,tribute,capture,gems-submitted,end-turn'


def run(args: list[str], timeout: int = 900) -> str:
    p = subprocess.run([NODE, '--disable-warning=ExperimentalWarning', INSPECT, *args], capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(p.stderr[-400:])
    return p.stdout


def entity_id(s: str | None) -> list:
    if not s:
        return ['none', -1]
    kind, _, i = s.partition(':')
    return [kind, int(i) if i.lstrip('-').isdigit() else -1]


def compact(e: dict) -> list | None:
    r, a, k = e.get('round'), e.get('actor'), e.get('kind')
    act = e.get('action') or {}
    if k == 'move':
        to = act.get('recordedTo') or [0, 0]
        return [r, a, 'move', to[0], to[1]]
    if k == 'attack':
        cands = act.get('targetCandidates') or []
        target = cands[0]['entity'] if cands else None
        before = after = None
        for ob in e.get('observed') or []:
            if ob.get('entity') == target:
                before = (ob.get('before') or {}).get('health')
                after = (ob.get('after') or {}).get('health')
        to = act.get('recordedTo') or [0, 0]
        return [r, a, 'attack', entity_id(target), before, after, to[0], to[1]]
    if k == 'cast':
        to = act.get('recordedTo') or [0, 0]
        hits = []
        for ob in e.get('observed') or []:
            ent = ob.get('entity', '')
            bh, ah = (ob.get('before') or {}).get('health'), (ob.get('after') or {}).get('health')
            if bh is not None and ah is not None and ent.startswith(('camp:', 'champion:')):
                hits.append([entity_id(ent), bh, ah])
        return [r, a, 'cast', act.get('abilitySlot'), to[0], to[1], hits]
    if k == 'shop':
        for ob in e.get('observed') or []:
            if ob.get('entity') == f'champion:{a}':
                aft = ob.get('after') or {}
                return [r, a, 'shop', aft.get('items') or [], aft.get('maxHealth')]
        return None
    if k in ('recall', 'tribute'):
        return [r, a, k]
    if k == 'end-turn':
        return [r, a, 'end']
    if k == 'capture':
        group, champs = -1, []
        for ob in e.get('observed') or []:
            ent = ob.get('entity', '')
            if ent.startswith('group:'):
                group = int(ent.split(':')[1])
            elif ent.startswith('champion:'):
                aft = ob.get('after') or {}
                champs.append([int(ent.split(':')[1]), aft.get('gold'), aft.get('gems')])
        return [r, -1, 'capture', group, champs]
    if k == 'gems-submitted':
        gems = None
        for ob in e.get('observed') or []:
            if ob.get('entity') == f'champion:{a}':
                gems = (ob.get('before') or {}).get('gems')
        return [r, a, 'banked', gems]
    return None


def decode(item: dict) -> str:
    mid = item['matchId']
    out = OUT / f'{mid}.json.gz'
    if out.is_file():
        return f'{mid} cached'
    replay = SRC / f'{mid}.zip'
    if not replay.is_file():
        return f'{mid} missing'
    try:
        tracks = json.loads(run(['--replay', str(replay), '--actor', 'all']))
        last = max(s[0] for t in tracks['tracks'] for s in t['samples'])
        events = []
        for lo in range(1, last + 1, 64):
            hi = min(last, lo + 63)
            text = run(['--replay', str(replay), '--trace', '1', '--from-round', str(lo), '--to-round', str(hi), '--teams', '0,1',
                        '--kinds', KINDS, '--format', 'jsonl', '--max-events', '100000'])
            for line in text.splitlines()[1:]:
                c = compact(json.loads(line))
                if c is not None:
                    events.append(c)
    except Exception as e:  # noqa: BLE001
        return f'{mid} failed: {e}'
    teams = item.get('teams') or []
    winner = -1
    for t in teams:
        if t.get('result') == 'win':
            winner = 0 if t.get('side') == 'a' else 1
    doc = {'match': mid, 'map': item.get('map'), 'winner': winner, 'ranks': [t.get('rank') for t in teams],
           'tracks': {str(t['id']): {'team': t['team'], 'type': t['type'], 'samples': t['samples']} for t in tracks['tracks']},
           'events': events}
    OUT.mkdir(parents=True, exist_ok=True)
    with gzip.open(out, 'wt', encoding='utf-8') as fh:
        json.dump(doc, fh, separators=(',', ':'))
    return f'{mid} ok {len(events)} events'


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    index = json.loads((SRC / 'index.json').read_text(encoding='utf-8'))
    if a.limit:
        index = index[:a.limit]
    # Failures under slot contention (waits that time out surface as bridge errors) are retried in later passes.
    todo = index
    for attempt in range(4):
        failed, done = [], 0
        with ThreadPoolExecutor(a.jobs) as ex:
            for item, msg in zip(todo, ex.map(decode, todo)):
                done += 1
                if 'failed' in msg:
                    failed.append(item)
                if ('ok' not in msg and 'cached' not in msg) or done % 50 == 0:
                    print(f'pass {attempt + 1}: {done}/{len(todo)} {msg[:160]}', flush=True)
        print(f'pass {attempt + 1}: {len(todo) - len(failed)} decoded or skipped, {len(failed)} failed', flush=True)
        if not failed:
            break
        todo = failed

if __name__ == '__main__':
    main()
