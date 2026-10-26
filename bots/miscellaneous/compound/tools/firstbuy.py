# First emblem per champion type (median round and kinds). usage: firstbuy.py RUN_DIR
import json, sys, collections
rows = [json.loads(l) for l in open(sys.argv[1] + '/games.jsonl')]
first = collections.defaultdict(collections.Counter); firstr = collections.defaultdict(list)
for row in rows:
    side = row['tags']['subject']; team = 'teamA' if side == 'A' else 'teamB'
    st = json.load(open(row['replay'].replace('.repl26a', '.json')))
    ids = {c['id']: c['name'] for c in st['roster']['champions'] if c['team'] == team}
    for g in st['events']:
        o = g.get('owner') or {}
        if g['kind'] == 'itemCombination' and isinstance(o, dict) and o.get('id') in ids and g['entries']:
            e = sorted(g['entries'], key=lambda e: e['timestamp']['round'])[0]
            first[ids[o['id']]][e['target']['name']] += 1
            firstr[ids[o['id']]].append(e['timestamp']['round'])
import statistics as S
for ch, c in first.items():
    print(ch, 'first emblem median r', S.median(firstr[ch]), dict(c.most_common(4)))
