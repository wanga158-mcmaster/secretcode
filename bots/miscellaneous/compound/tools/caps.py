# Clears by camp category up to r400 per run (needs statistics JSON: --keep-replays all).
# usage: python -B bots/contenders/compound/tools/caps.py RUN_DIR [RUN_DIR ...]
import json, sys, collections, statistics as S
geo = json.load(open('maps/geometry.json'))
gmap = {}
for m in geo['maps']:
    for g in m['groups']:
        types = collections.Counter(x['type'] for x in g['members'])
        cat = 'mono' if 'monolith' in types else 'golem' if 'golem' in types else 'elk' if 'elk' in types else 'wolf' if 'wolf' in types else 'goldC' if set(types)=={'gold_cluster'} else 'gemC' if set(types)=={'gem_cluster'} else 'mix'
        gmap[(m['id'].replace('_','-'), g['nativeId'])] = (cat, g['gemValue'], g['goldValue'])
for run in sys.argv[1:]:
    rows = [json.loads(l) for l in open(run + '/games.jsonl')]
    cats = collections.Counter(); first = []; clears = []; gems400 = []
    for row in rows:
        side = row['tags']['subject']; team = 'teamA' if side == 'A' else 'teamB'
        st = json.load(open(row['replay'].replace('.repl26a', '.json')))
        ids = {c['id'] for c in st['roster']['champions'] if c['team'] == team}
        rs = []
        for g in st['events']:
            o = g.get('owner') or {}
            if g['kind'] == 'campClear' and isinstance(o, dict) and o.get('id') in ids:
                for e in g['entries']:
                    r = e['timestamp']['round']; rs.append(r)
                    if r <= 400:
                        key = (row['map'], e['target']['id'])
                        cats[gmap.get(key, ('?', 0, 0))[0]] += 1
        rs.sort(); first.append(rs[0] if rs else 2000); clears.append(sum(1 for r in rs if r <= 400))
    n = len(rows)
    print(run.split('/')[-1], 'clears r400/game', round(S.mean(clears), 1), 'first clear median', S.median(first), {k: round(v / n, 1) for k, v in cats.most_common()})
