"""Source-pinned mixed-HP splash clocks and one saved actual-caller witness; no games.

Run with a fresh evidence directory. The clocks are a fixed-rate model approximation,
not a finite-horizon native admissibility proof or an optimal route/clear solver.
"""
from __future__ import annotations
from collections import Counter
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[5]
SOURCE=ROOT/'.cache/arena/snap/415fc4762cb0/bots/contenders/_timetable-capacity/src'
WITNESS=ROOT/'.cache/contenders/timetable/capacity-v6-panel/native-party3-corrected'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def ceil_fraction(x):return -(-x.numerator//x.denominator)
def two_clock(health,aggregate,per_member):
    if sum(health)<=0:return F(0)
    if aggregate<=0 or any(h>0 and d<=0 for h,d in zip(health,per_member)):return None
    return max([sum(health)/aggregate]+[h/d for h,d in zip(health,per_member)if h>0])
def encode(x):
    if isinstance(x,F):return {'exact':str(x),'value':float(x)}
    raise TypeError(type(x).__name__)

def main(out):
    out.mkdir(parents=True,exist_ok=False)
    for name,source in(('native.json',WITNESS/'stdout.bin'),('selected.json',WITNESS/'selected.json')):
        (out/name).write_bytes(source.read_bytes())
    native=json.loads((out/'native.json').read_bytes());selected=json.loads((out/'selected.json').read_text(encoding='utf-8'))
    trace=native['interactions'];coverage=trace['coverage']
    assert coverage['complete'] and not coverage['queryFailures'] and not coverage['truncated']
    assert native['engineVersion']=='1.0.3' and native['decoderProfile']=='cli-3.0.5'
    members=selected['group']['members'];ids={m['nativeId']for m in members}
    hp={m['nativeId']:1350.0 if m['type']=='wolf' else 120.0 for m in members}
    initial=dict(hp);deaths={};actions=[];basics=Counter();casts=Counter();moves=[];actor_first={};checks=0
    for event in trace['events']:
        if not 353<=event['round']<=364:continue
        actor=event['actor'];state=trace['states'][event['actorState'][0]]if event['actorState'][0]is not None else None
        if actor in(5,9)and state:actor_first.setdefault(actor,state)
        if actor in(5,9)and event['kind']=='move':
            moves.append({'round':event['round'],'actor':actor,'from':event['action']['recordedFrom'],
                'to':event['action']['recordedTo'],'event':event['id']})
        damage=[]
        for delta in event['observed']:
            if not delta['entity'].startswith('camp:'):continue
            target=int(delta['entity'].split(':')[1])
            if target not in ids or 'health'not in delta.get('after',{}):continue
            before=delta['before']['health'];after=delta['after']['health']
            assert before==hp[target],(target,event['round'],before,hp[target])
            assert 0<=after<before;hp[target]=after;checks+=1
            damage.append({'member':target,'before':before,'after':after,'applied':before-after})
            if after==0:deaths[target]={'round':event['round'],'actor':actor,'event':event['id']}
        if actor in(5,9)and event['kind']=='attack':basics[actor]+=1
        if actor in(5,9)and event['kind']=='cast':casts[actor,event['action']['abilitySlot']]+=1
        if damage:
            assert actor in(5,9)
            actions.append({'round':event['round'],'actor':actor,'kind':event['kind'],
                'event':event['id'],'from':event['action']['recordedFrom'],'to':event['action']['recordedTo'],
                'slot':event['action']['abilitySlot'],'state':state,'damage':damage})
    assert all(h==0 for h in hp.values())and basics==Counter({5:11,9:12})
    assert all(deaths[i]['round']==353 for i in(5,7))
    assert deaths[28]['round']==360 and deaths[26]['round']==361
    assert deaths[17]['round']==363 and deaths[3]['round']==364
    varg=[a for a in actions if a['actor']==9 and a['kind']=='attack']
    assert len(varg[0]['damage'])==len(varg[1]['damage'])==4
    assert len([a for a in actions if a['actor']==5 and a['kind']=='cast'])==1
    assert not any(a['round']==362 for a in actions)
    assert any(m['round']==362 for m in moves)
    assert actor_first[5]['strength']==30 and actor_first[5]['focus']==220
    assert actor_first[9]['strength']==95 and actor_first[9]['focus']==100
    H=F(3180);M=F(1350);G=F(7,2)
    vb=F(95)*F(9,8);hb=F(30);hd=F(3,2)*220/8
    agg=vb*G+hb+hd*G;single=vb+hb+hd
    actual_clock=two_clock([F(h)for h in initial.values()],agg,[single]*6)
    assert ceil_fraction(H/agg)==6 and ceil_fraction(actual_clock)==8
    cases=[]
    def case(name,health,aggregate,member,expected,notes):
        t=two_clock(list(map(F,health)),F(aggregate),list(map(F,member)))
        assert t==expected,(name,t,expected)
        cases.append({'name':name,'health':health,'aggregateRate':aggregate,'perMemberRates':member,
            'clock':t,'rounded':ceil_fraction(t)if t is not None else None,'notes':notes})
    case('actual frozen scalar',list(initial.values()),agg,[single]*6,actual_clock,'6 ->8 fixed-rate quote; observed full cycle clear12 includes burst, target selection and movement.')
    mixed=[F(1350),F(1350)]+[F(120)]*4;equal=[F(530)]*6
    case('pure splash mixedHP',mixed,vb*G,[vb]*6,F(1350)/vb,'Largest member dominates, despite same total/count as next case; fluid only.')
    case('pure splash equalHP',equal,vb*G,[vb]*6,F(3180)/(vb*G),'Aggregate-work clock dominates; G assumed valid here, not proved by native metadata.')
    feasible=F(100,11);bad=F(101)/(10+F(101,100))
    case('mixed focused plus splash',[100,1],12,[11,11],feasible,'Focused10 to largest plus splash1 to both; small dies at1, largest at100/11.')
    assert bad>feasible
    cases.append({'name':'rejected area-only cap','formulaClock':bad,'feasibleClock':feasible,
        'areaOnlyCapIsLowerBound':False,'reason':'Other sources accelerate the largest target, so cap applied only to area is not an aggregate bound.'})
    case('same residual total, concentrated',[1000,0],20,[10,10],F(100),'Total-only hp_scale cannot distinguish this from next case.')
    case('same residual total, balanced',[500,500],20,[10,10],F(50),'Requires actual member residuals or explicit uniform-scale assumption.')
    case('two Varg targets beyond mutual splash',[100,100],10,[10,10],F(20),'Only legal primary centers are targets at(0,0),(2,0); no midpoint target, Gmax1. Density/count2 would invent shared splash.')
    ready={'health':100,'basic':10,'spell':100,'cooldown':8,'sustainedQuote':math.ceil(100/(10+100/8)),
        'readyNativeStyleOwnerTurns':1,'cooldown8NativeStyleOwnerTurns':9}
    assert ready['sustainedQuote']==5
    pins=[Path(__file__),WITNESS/'stdout.bin',WITNESS/'selected.json']+[SOURCE/x for x in('model.h','world.cpp','capacity.h','capacity.cpp','combat.cpp','config.h')]
    result={'passed':True,'purpose':'HP-aware fixed-rate two-clock approximation and actual mixed-camp service/geometry audit',
        'engine':'1.0.3','cli_sdk':'3.0.5','source_key':'contenders/_timetable-capacity@415fc4762cb0',
        'native_games':0,'observer_runs':0,'native_input':{'path':str((out/'native.json').relative_to(ROOT)),
            'sha256':sha(out/'native.json'),'replay':native['replayPath'],'replay_sha256':native['replaySha256'],
            'coverage':coverage,'filters':trace['filters'],'source_helper_hashes':native['helperSha256']},
        'group':selected['group'],'initialHP':initial,'actualActions':actions,'moves':moves,
        'memberDeaths':deaths,'basicCounts':dict(basics),'casts':{str(k):v for k,v in casts.items()},
        'HP_before_state_checks':checks,'appliedDamage':sum(sum(x['applied']for x in a['damage'])for a in actions),
        'model':{'staticMultiplier':G,'vargSingleMemberBasicRate':vb,'heartBasic':hb,'heartDashPerMemberRate':hd,
            'aggregateRate':agg,'singleMemberRate':single,'oldRoundedQuote':6,'twoClockRoundedQuote':8,
            'actualFirstDamage':353,'actualWolvesDead':361,'actualGroupDead':364,'actualElapsedOwnerRounds':12,
            'fixedPositionsAfterFirstDash':{'Heart':[42,35],'Varg':[42,36]},
            'farResidualsUnreachableByStationaryBasic':[{'id':3,'at':[45,35]},{'id':17,'at':[45,36]}]},
        'cases':cases,'readinessContrast':ready,'source_pins':{p.relative_to(ROOT).as_posix():sha(p)for p in pins},
        'limits':['Source formula uses fixed sustained rates and count-based G3.5, not certified finite-horizon upper bounds.',
        'No new games or observers; retained native trace filters actors5/9 and selected kinds. Every member HP delta reconciles but unretained private decisions are unknown.',
        'Two-clock8 is not observed12 or optimal clear. Do not fit overlap to routing, burst clipping, action guards or current readiness.',
        'Heart damaging dash353 only; future cooldown readiness does not establish legal/value-approved dispatch.',
        'Exact partial-member HP is needed; total hp_scale cannot recover largest residual or coverage.',
        'Native HP overkill is clipped; movement retains endpoints, not hypothetical best routes. No safety, release or strength claim.']}
    (out/'splash.json').write_text(json.dumps(result,indent=1,default=encode)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'old_model':6,'two_clock_model':8,'native_owner_rounds':12,
        'native_HP_checks':checks,'pure_cases':len(cases),'new_games':0,'observers':0}))

if __name__=='__main__':main(Path(sys.argv[1]).resolve())
