# Collected and banked gems and earned gold at r400 per run, slowest games listed.
# usage: python -B bots/contenders/compound/tools/collect.py RUN_DIR
import json, glob, statistics as S, sys
def tv(runs, r):
    t = 0
    for run in runs:
        if t + run['duration'] > r: return float(run['value'])
        t += run['duration']
    return float(runs[-1]['value'])
run = sys.argv[1]
rows = [json.loads(l) for l in open(run + '/games.jsonl')]
out = []
for row in rows:
    side = row['tags']['subject']; team = 'teamA' if side == 'A' else 'teamB'
    st = json.load(open(row['replay'].replace('.repl26a', '.json')))
    ids = {c['id'] for c in st['roster']['champions'] if c['team'] == team}
    col = gold = 0.0
    for tl in st['championTimelines']:
        if tl['owner']['id'] in ids:
            if tl['metric'] == 'collectedGems': col += tv(tl['runs'], 400)
            if tl['metric'] == 'earnedGold': gold += tv(tl['runs'], 400)
    bank = 0.0
    for tl in st['teamTimelines']:
        if tl['owner'] == team: bank = tv(tl['runs'], 400)
    out.append((row['map'], side, col, bank, gold, row['rounds'] if row['winner'] == side else 2000))
print('collected r400 mean', S.mean(x[2] for x in out), 'banked r400', S.mean(x[3] for x in out), 'gold r400', S.mean(x[4] for x in out))
out.sort(key=lambda x: -x[5])
for x in out[:12]: print(x)
