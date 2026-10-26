"""Pure consumer of a complete saved round, proving a ground-center candidate omission.

  python aoe_midpoint.py TRACE SUMMARY REGISTRY MATCH ACTOR FRESH-OUTPUT.json
No builds, games, native actions or observer calls. Input trace must retain all actors/kinds.
The alternative is geometric/model evidence, not an executed counterfactual effect.
"""
from __future__ import annotations
import gzip
import hashlib
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[5]
def sha(raw):return hashlib.sha256(raw).hexdigest()
def dist(a,b):return max(abs(a[0]-b[0]),abs(a[1]-b[1]))

def main():
    trace_path,summary_path,registry_path=map(lambda s:Path(s).resolve(),sys.argv[1:4])
    uid=sys.argv[4];actor=int(sys.argv[5]);out=Path(sys.argv[6]).resolve()
    if out.exists():raise SystemExit('refusing existing output')
    trace_raw=trace_path.read_bytes();summary_raw=summary_path.read_bytes();registry_raw=registry_path.read_bytes()
    doc=json.loads(trace_raw);t=doc['interactions'];stats=json.loads(gzip.decompress(summary_raw))
    registry=json.loads(registry_raw)
    assert doc['engineVersion']==stats['match']['engineVersion']=='1.0.3'
    assert t['filters']['actors'] is None and t['filters']['kinds'] is None
    group=next(g for g in registry if str(g.get('version'))=='40' and any(m['matchId']==uid for m in g.get('show',{}).get('matches',[])))
    match=next(m for m in group['show']['matches'] if m['matchId']==uid)
    assert match['status']=='finished'
    own=next(x for x in match['teams'] if x['teamId']=='34b36bab-66bb-4506-b032-2df6a081758d')
    side=0 if own['side']=='a' else 1
    geometry_path=ROOT/'maps/geometry.json';geometry_raw=geometry_path.read_bytes()
    geometry=json.loads(geometry_raw)
    gm=next(m for m in geometry['maps'] if m['mapSha256']==stats['match']['mapHash'])
    assert gm['width']==doc['width'] and gm['height']==doc['height']
    assert geometry['engineSha256']==doc['decoderSha256']
    current={c['id']:t['states'][c['state']] for c in t['initial']['champions']}
    found=None
    for e in t['events']:
        for c in e['championStates']:
            if c['before'] is not None:current[c['id']]=t['states'][c['before']]
        if e['actor']==actor and e['kind']=='cast' and e['action']['abilitySlot']==0 and e['action']['abilityType']==3:
            assert found is None,'more than one selected cast; narrow input window'
            me=current[actor]
            assert me['type']==0 and me['team']==side and me['alive'] and me['availability']['cast']
            assert me['cooldowns'][0]==0
            ability=next(a for a in t['abilityDefinitions'][me['abilityDefinition']]['abilities'] if a['slot']==0)
            radius=int(ability['payload']['radius']);ran=int(ability['payload']['range'])
            assert ability['targets']&16 and radius==1 and ran==4
            origin=[me['x'],me['y']]
            foes=[s for s in current.values() if s['team']!=side and s['alive'] and s['id'] in me['personalVisibleEnemies']]
            near={int(c['entity'].split(':')[1]):c for c in e['nearby']['before']['champions']}
            for foe in foes:
                if dist(origin,[foe['x'],foe['y']])<=5:
                    assert near[foe['id']]['at']==[foe['x'],foe['y']] and near[foe['id']]['personallyVisibleToActor']
            actual=e['action']['recordedTo']
            def victims(center):return {s['id'] for s in foes if dist(center,[s['x'],s['y']])<=radius}
            def score(center):return sum(min(s['health'],ability['payload']['modifier']*me['focus'])+60 for s in foes if s['id'] in victims(center))
            actual_hit=victims(actual)
            occupied=[{'at':[s['x'],s['y']],'hits':sorted(victims([s['x'],s['y']])),'score':score([s['x'],s['y']])}
                      for s in foes if dist(origin,[s['x'],s['y']])<=ran]
            vision=dict(t['visions'][me['personalVision']]);stride=t['visionEncoding']['chunkStride']
            def visible(p):
                chunk=(p[1]>>3)*stride+(p[0]>>3);bit=((p[1]&7)<<3)+(p[0]&7)
                return bool(int(vision.get(chunk,'0'),16)&(1<<bit))
            candidates=[]
            occupied_tiles={(s['x'],s['y']) for s in current.values() if s['alive']}
            camp_tiles={tuple(c['at']) for c in e['nearby']['before']['camps']}
            for y in range(max(0,origin[1]-ran),min(doc['height'],origin[1]+ran+1)):
                for x in range(max(0,origin[0]-ran),min(doc['width'],origin[0]+ran+1)):
                    p=[x,y];flat=y*doc['width']+x
                    if not visible(p) or (x,y) in occupied_tiles or (x,y) in camp_tiles:continue
                    if any(flat in gm['terrain'].get(k,[]) for k in ('wall','hole','altar')):continue
                    hit=victims(p)
                    if actual_hit<hit:candidates.append({'at':p,'hits':sorted(hit),'score':score(p)})
            assert candidates,'no strict enemy-coverage candidate'
            candidates.sort(key=lambda c:(-len(c['hits']),dist(origin,c['at']),sum((c['at'][i]-origin[i])**2 for i in (0,1))))
            best=candidates[0];extra=set(best['hits'])-actual_hit
            assert len(best['hits'])>max(len(c['hits']) for c in occupied)
            assert best['score']>max(c['score'] for c in occupied)
            camp_changes=[c for c in e['observed'] if c['entity'].startswith('camp:')]
            dropped=[]
            for c in camp_changes:
                previous=next((v for v in e['nearby']['before']['camps'] if v['entity']==c['entity']),None)
                if previous and dist(previous['at'],best['at'])>radius:dropped.append(c)
            found={'castStep':e['id'],'round':e['round'],'actor':actor,'actorBefore':me,'queriedAbility':ability,
                   'actualCenter':actual,'actualVisibleEnemyVictims':sorted(actual_hit),'nativeActualEffects':e['observed'],
                   'occupiedEnemyCenters':occupied,'bestFreeVisibleGroundCenter':best,
                   'extraVictimsBefore':[s for s in foes if s['id'] in extra],
                   'droppedObservedCampEffects':dropped,
                   'alternativeEffectStatus':'unexecuted geometric/model candidate; not a native counterfactual or utility dominance proof'}
        for c in e['championStates']:
            if c['after'] is not None:current[c['id']]=t['states'][c['after']]
    assert found
    inputs=[trace_path,summary_path,registry_path,geometry_path,Path(__file__),ROOT/'bots/anchors/main-v40/src/combat.cpp',ROOT/'sdk/cpp/cambc.h']
    result={'passed':True,'purpose':'normal-planner v40 ground-center candidate omission, not strength or executed alternative',
            'match':uid,'mapHash':stats['match']['mapHash'],'mapId':gm['id'],'engine':'1.0.3',
            'provenance':{'registryVersion':group['version'],'group':group['group'],'match':match,
                          'sourceAnchor':'anchors/main-v40','releaseSourceKey':'main/main@45108c43eb8a'},
            'source_sha256':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in inputs},'finding':found}
    out.write_text(json.dumps(result,indent=1))
    print(json.dumps({'passed':True,'round':found['round'],'actual':found['actualVisibleEnemyVictims'],
                      'best':found['bestFreeVisibleGroundCenter'],'droppedCampEffects':len(found['droppedObservedCampEffects'])}))
    return 0
if __name__=='__main__':raise SystemExit(main())
