"""Pure check of the retained normal-planner r147 center/effect contrast.
  python freecenter_normal.py NORMAL-PILOT-DIR
No game, native call or observer rerun. Equality is represented public state only.
"""
import hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    folder=Path(sys.argv[1]).resolve();out=folder/'normal-verification.json'
    if out.exists():raise SystemExit('refusing existing verification')
    paths=[folder/'base-146-147.json',folder/'free-146-147.json'];docs=[json.loads(p.read_text()) for p in paths]
    for d in docs:
        assert d['engineVersion']=='1.0.3' and d['stopped']=='round_range_end'
        assert d['interactions']['coverage']['complete'] and not d['interactions']['native']['errors']
    ta,tb=[d['interactions'] for d in docs]
    expand=lambda t,ref:None if ref is None else t['states'][ref]
    ia={c['id']:expand(ta,c['state']) for c in ta['initial']['champions']}
    ib={c['id']:expand(tb,c['state']) for c in tb['initial']['champions']}
    assert ia==ib and ta['initial']['camps']==tb['initial']['camps']
    assert ta['initial']['groups']==tb['initial']['groups'] and ta['initial']['scores']==tb['initial']['scores']
    first=None
    for i,(a,b) in enumerate(zip(ta['events'],tb['events'])):
        sig=lambda e:(e['round'],e['actor'],e['kind'],e['action'])
        if sig(a)!=sig(b):first=(i,a,b);break
        assert a['native']==b['native'] and a['observed']==b['observed']
        aa=[(c['id'],expand(ta,c['before']),expand(ta,c['after'])) for c in a['championStates']]
        bb=[(c['id'],expand(tb,c['before']),expand(tb,c['after'])) for c in b['championStates']]
        assert aa==bb
        for c in a['championStates']:
            if c['after'] is not None:ia[c['id']]=expand(ta,c['after'])
        for c in b['championStates']:
            if c['after'] is not None:ib[c['id']]=expand(tb,c['after'])
    assert first
    i,base,free=first
    assert i==218 and ia==ib
    assert base['native']==free['native']
    assert base['actor']==free['actor']==2 and base['round']==free['round']==147
    assert base['action']['recordedTo']==[27,10] and free['action']['recordedTo']==[28,11]
    assert expand(ta,base['actorState'][0])==expand(tb,free['actorState'][0])
    old={d['entity']:d for d in base['observed']};new={d['entity']:d for d in free['observed']}
    assert old['champion:5']==new['champion:5'] and old['champion:6']==new['champion:6']
    assert 'champion:7' not in old
    extra=new['champion:7'];assert extra['before']['health']==900 and extra['after']['health']==800
    assert extra['after']['stunned'] and extra['after']['buffs']==[{'type':1,'name':'stun','timer':1}]
    assert all(not extra['after']['availability'][k] for k in('move','attack','cast'))
    assert not any(d['entity'].startswith('camp:') for d in base['observed']+free['observed'])
    result={'passed':True,'purpose':'normal-planner actual free-center contribution on a matched represented public prefix; not universal strength attribution',
            'map':'00eight','subject':'A','opponent':'anchors/main-v40','round':147,'input':3228,
            'matchedInitialAllChampionStates':len(ia),'matchedPrecedingObservedEvents':i,
            'equalityScope':'all represented initial champion/camp/group/score state and preceding observed events in rounds146-147; private fuel/memory and unqueried camp state excluded',
            'baselineCast':base['id'],'activeCast':free['id'],'baselineCenter':[27,10],'activeCenter':[28,11],
            'preservedEffects':[old['champion:5'],old['champion:6']],'extraEffect':extra,
            'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in paths+[Path(__file__)]}}
    out.write_text(json.dumps(result,indent=1))
    print(json.dumps({'passed':True,'matchedPrecedingObservedEvents':i,'extraEnemyHealth':[900,800],'appliedExtraStun':True}))
    return 0
if __name__=='__main__':raise SystemExit(main())
