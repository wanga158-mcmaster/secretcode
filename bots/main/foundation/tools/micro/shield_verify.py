"""Pure saved-byte check of the supplied native v41 Brain shield/basic assay.

  python shield_verify.py PROBE-DIRECTORY FRESH-OUTPUT.json
No games or observer calls. Public equality excludes private memory/fuel and unqueried state.
"""
from __future__ import annotations
import hashlib,json,struct,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def states(t,e,which):return {r['id']:t['states'][r[which]] for r in e['championStates']}
def advance(t,e,current):
    # Events contain relevant/changed subsets, not a full snapshot of every unit.
    for key,value in states(t,e,'before').items():assert current[key]==value
    before=dict(current)
    current.update(states(t,e,'after'))
    return before,dict(current)
def main():
    folder=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
    assert not out.exists()
    observations=json.loads((folder/'observations.json').read_text())
    inputs=json.loads((folder/'input.json').read_text())
    checks=[];pairs=[];headers=[]
    for side,team in [('A',0),('B',1)]:
        docs=[json.loads((folder/(arm+'-'+side+'.json')).read_text()) for arm in ['off','on']]
        off,on=[d['interactions'] for d in docs]
        for d,t in zip(docs,[off,on]):
            assert d['engineVersion']=='1.0.3' and d['stopped']=='round_range_end'
            c=t['coverage'];assert c['complete'] and not c['truncated'] and c['omittedByFilter']==0
            assert c['queryObservationComplete'] and not c['queryFailures'] and not t['native']['errors']
            assert t['filters']['actors'] is None and t['filters']['kinds'] is None
        actor=next(e['id'] for e in off['entities'] if e['type']==10 and e['team']==team)
        ren=next(e['id'] for e in off['entities'] if e['type']==9 and e['team']!=team)
        initial=lambda t:{r['id']:t['states'][r['state']] for r in t['initial']['champions']}
        assert initial(off)==initial(on) and len(initial(off))==6
        current_off,current_on=initial(off),initial(on)
        for key in ['camps','groups','scores']:assert off['initial'][key]==on['initial'][key]
        prefix=0
        for eo,en in zip(off['events'],on['events']):
            signature=lambda e:(e['round'],e['actor'],e['kind'],e['action'])
            if signature(eo)!=signature(en):break
            assert eo['native']==en['native'] and eo['observed']==en['observed']
            assert states(off,eo,'before')==states(on,en,'before')
            assert states(off,eo,'after')==states(on,en,'after')
            advance(off,eo,current_off);advance(on,en,current_on)
            assert current_off==current_on
            prefix+=1
        else:raise AssertionError('No represented action divergence')
        assert eo['actor']==en['actor']==actor and eo['kind']=='attack' and en['kind']=='cast'
        assert en['action']['abilitySlot']==1 and en['action']['abilityType']==7
        pre,after_off=advance(off,eo,current_off)
        pre_on,after_shield=advance(on,en,current_on)
        assert pre==pre_on
        assert pre[actor]['health']==10 and pre[actor]['shield']==0
        assert pre[actor]['strength']==40 and pre[actor]['focus']==100 and pre[actor]['cooldowns']==[0,0]
        assert all(pre[actor]['availability'][k] for k in ['move','attack','cast'])
        assert pre[ren]['health']==20 and pre[ren]['focus']==100 and pre[ren]['shield']==0
        assert pre[actor]['items']==[] and pre[ren]['items']==[]
        assert not after_off[actor]['alive'] and after_off[actor]['health']==0
        assert not after_off[ren]['alive'] and after_off[ren]['health']==0
        shielded=after_shield[actor]
        assert shielded['health']==10 and shielded['shield']==250 and shielded['cooldowns']==[0,8]
        assert shielded['availability']['move'] and shielded['availability']['attack'] and not shielded['availability']['cast']
        definition=on['abilityDefinitions'][shielded['abilityDefinition']]['abilities'][1]
        assert definition['kind']==7 and definition['attributes']==0
        assert definition['payload']['modifier']==2.5 and definition['payload']['duration']==5
        attack=next(e for e in on['events'][prefix+1:] if e['actor']==actor and e['kind']=='attack')
        for event in on['events'][prefix+1:]:
            before_attack,after_attack=advance(on,event,current_on)
            if event is attack:break
        assert attack['action']==eo['action']
        assert before_attack[actor]['health']==10 and before_attack[actor]['shield']==250
        assert after_attack[actor]['alive'] and after_attack[actor]['health']==10 and after_attack[actor]['shield']==230
        assert after_attack[actor]['availability']['move']
        assert not after_attack[actor]['availability']['attack'] and not after_attack[actor]['availability']['cast']
        assert not after_attack[ren]['alive'] and after_attack[ren]['health']==0
        for end in [after_off,after_attack]:
            assert end[actor]['gold']==pre[actor]['gold']+100 and end[actor]['kills']==pre[actor]['kills']+1
        assert after_off[ren]['gold']==pre[ren]['gold']+100 and after_off[ren]['kills']==pre[ren]['kills']+1
        assert after_attack[ren]['gold']==pre[ren]['gold'] and after_attack[ren]['kills']==pre[ren]['kills']
        pairs.append({'subject':side,'queried_initial_champions':6,'equal_represented_prefix_events':prefix,
                      'before_first_difference_all_queried_champions_equal':True})
        checks.append({'subject':side,'actor':actor,'ren':ren,'off_attack':eo['id'],
                       'on_shield':en['id'],'on_attack':attack['id'],'same_basic_action':attack['action'],
                       'off_actor_after':after_off[actor],'on_actor_after':after_attack[actor],
                       'shield_payload':definition})
    for observation in observations:
        side=observation['tag'][-1];rp=Path(observation['row']['replay'])
        if not rp.is_absolute():rp=ROOT/rp
        with zipfile.ZipFile(rp) as z:
            for stream in ['a','b']:
                stdout=z.read('team-'+stream+'.stdout').replace(b'\0',b'').decode('utf8','replace')
                assert 'error' not in stdout.lower() and 'refusal' not in stdout.lower()
                raw=z.read('team-'+stream+'.fuel');assert raw[:8]==b'a62leuf\0' and (len(raw)-13)%8==0
                values=struct.unpack_from(f'<{(len(raw)-13)//8}Q',raw,13)
                base=0 if stream=='a' else 4
                for samples,budget in [(values[8:11],values[base+1]),(values[11:14],values[base+2]),(values[14:],values[base+3])]:
                    assert all(v<budget for v in samples)
                headers.append({'tag':observation['tag'],'stream':stream,'ordinary_budget':values[base+3],
                                'ordinary_peak':max(values[14:],default=0),'header':list(values[:8])})
            own=z.read('team-'+side.lower()+'.stdout').decode('utf8','replace')
            assert 'shield_query250 shield_target250 duration5 ren_hp20 ren_sh0 ren_foc100 ren_spell_against10' in own
    files=[folder/(a+'-'+s+'.json') for s in ['A','B'] for a in ['off','on']]
    files += [folder/'input.json',folder/'builds.json',folder/'observations.json',folder/'native-verification.json',
              folder/'corrected-model.json',folder.parent/'preservation.json',ROOT/'results/contenders/micro/control-scout/normal-screen-verification.json',Path(__file__)]
    files.append(folder.parent/'independent-initial-failure.json')
    result={'passed':True,'scope':'independent pure native trace consumer; conditional full-Brain contribution on supplied legal setup, no natural occurrence or strength claim',
            'paired_states':pairs,'checked':checks,'fuel_headers':headers,
            'peak_ordinary':max(h['ordinary_peak'] for h in headers),
            'query_scope':'Only observed Ren slot0 targeted ability.damage_amount(Varg) returns HP-capped10 here. Shield_amount/self250. No basic/retaliation prediction or universal damage-query claim.',
            'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},
            'input_source_closure_sha256':inputs['source_closure_sha256'],
            'limits':['Only queried public states/prefix are equal; no private memory/fuel equality.',
                      'Brain called at rounds1 and90 only; normal planner occurrence screen remains negative.',
                      'Full native admission occurred but favourable/doomed/private branch fields were not queried.',
                      'Four truncated93-round assays qualify bounded mechanics/fuel, not full-game liveness/release.',
                      'OFF dies earlier; peak fuel difference is not isolated guard cost.']}
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'passed':True,'pairs':pairs,'peak_ordinary':result['peak_ordinary'],'output':str(out)}))
if __name__=='__main__':main()
