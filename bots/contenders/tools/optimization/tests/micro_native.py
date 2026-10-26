"""Bounded real-native branch tests, with retained actual kits/items and no sandbox."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[5])
    p.add_argument('--prefix-reuse', action='store_true', help='Exercise opt-in official timeline prefix reuse')
    a = p.parse_args()
    repo = a.repo.resolve()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    launcher = repo / 'bots/contenders/tools/optimization/adapters/native_micro.py'
    source_hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (launcher,launcher.with_suffix('.mjs'))}
    checks = []
    def call(name, body, success=True):
        folder = out / name
        folder.mkdir()
        request, response = folder/'request.json', folder/'response.json'
        request.write_text(json.dumps(body, indent=2)+'\n', encoding='utf-8')
        run = subprocess.run([sys.executable, str(launcher), '--repo', str(repo), '--request', str(request), '--response', str(response)], capture_output=True, text=True)
        if success:
            assert run.returncode == 0, (name, run.stderr)
        else:
            assert run.returncode != 0, name
        result = json.loads(response.read_text())
        assert result['request_sha256'] == hashlib.sha256(request.read_bytes()).hexdigest()
        if success:
            assert result['identity']['oracle_sha256']==source_hashes['native_micro.mjs']
            assert result['identity']['launcher_sha256']==source_hashes['native_micro.py']
        return result
    base = json.loads((repo/'bots/contenders/tools/optimization/examples/micro-native.json').read_text())
    if a.prefix_reuse:
        base['generation']['prefix_reuse']=True
    farm = call('farm', base)
    assert farm['native'] and all(x['supported'] and x['prefix_valid'] for x in farm['results'])
    assert farm['results'][0]['state']['fingerprint'] == farm['results'][2]['state']['fingerprint'] == farm['identity']['root_fingerprint']
    assert farm['results'][2]['cache_hit']
    if a.prefix_reuse:
        assert farm['stats']['games']<=3 and farm['stats']['prefix_reuse']['restores']>0
    else:
        assert farm['stats']['games']==3
    root = farm['root_state']
    camp = next(c for c in root['camps'] if c['id'] == 48)
    assert camp['group_id'] == 18
    group = next(g for g in root['groups'] if g['id'] == 18)
    assert set(group['members']) == {48, 84} and group['gold'] == 60 and group['gems'] == 4
    assert farm['results'][1]['state']['round'] == 65
    checks += ['fresh independent branch equality', 'ordered-prefix cache', 'native camp membership/rewards', 'native camp kill→capture continuation']
    farm_response=out/'farm/response.json'
    old_response_hash=hashlib.sha256(farm_response.read_bytes()).hexdigest()
    stale=subprocess.run([sys.executable,str(launcher),'--repo',str(repo),'--request',str(out/'farm/request.json'),'--response',str(farm_response)],capture_output=True,text=True)
    assert stale.returncode!=0 and 'already exists' in stale.stderr
    assert hashlib.sha256(farm_response.read_bytes()).hexdigest()==old_response_hash
    combat = dict(base)
    combat['scenario'] = {'replay':'results/contenders/foundation/model-lab-import/seed.repl26a', 'replay_sha256':'a4c77d43abd5979a5276826894d8346111c618544f4d45e1f36cd52e6d24b21b', 'input':2666, 'actor':7, 'participants':[7,2], 'background_policy':'yield'}
    attack = {'type':'attack','target':{'kind':1,'id':2}}
    reset = {'type':'cast','slot':1,'target':None,'x':-1,'y':-1}
    combat['jobs'] = [
        {'id':'move-attack-reset-attack','prefix':[{'type':'move','x':34,'y':17}, attack, reset, attack, {'type':'end'}]},
        {'id':'stun-before-move','prefix':[{'type':'cast','slot':0,'target':None,'x':33,'y':17}, {'type':'move','x':34,'y':17}]},
        {'id':'refused-attack','prefix':[attack]},
        {'id':'false-move','prefix':[{'type':'move','x':35,'y':18}]},
        {'id':'two-casts','prefix':[{'type':'move','x':34,'y':17},attack,reset,{'type':'cast','slot':0,'target':None,'x':33,'y':17}]},
    ]
    r = call('combat', combat)
    results = {x['id']:x for x in r['results']}
    good = results['move-attack-reset-attack']
    assert good['supported'] and good['prefix_valid'] and good['state']['current_owner'] == 2
    a7 = next(c for c in good['state']['champions'] if c['id']==7)
    a2 = next(c for c in good['state']['champions'] if c['id']==2)
    assert a7['items'] == [[22,2]] and a2['items'] == [[18,1]] and a2['health'] < 873
    assert len(good['trace']) == 5 and all(t['applied'] for t in good['trace'])
    assert not results['refused-attack']['prefix_valid'] and results['refused-attack']['trace'][0]['code'] != 0
    exclusive=results['stun-before-move']
    assert not exclusive['prefix_valid'] and exclusive['trace'][0]['applied'] and exclusive['trace'][1]['code']==127
    assert not results['two-casts']['prefix_valid'] and results['two-casts']['trace'][-1]['code']!=0
    assert results['false-move']['trace'][0]['code']==22 and not results['false-move']['prefix_valid']
    checks += ['equipped natural Ryth combat', 'movement→attack→reset→attack interaction', 'next selected enemy owner', 'invalid native action refusal']
    # Query actual owners from a retained prefix interval, rather than constructing kits.
    inv = dict(base, kind='inventory', first_input=1542, last_input=1575)
    inventory = call('inventory', inv)
    owners = {}
    for state in inventory['states']:
        actor = next(c for c in state['champions'] if c['id']==state['owner'])
        prior=owners.get(actor['type'])
        score=lambda c: (sum(cd==0 for cd in c['cooldowns']),c['availability']['cast'],not c['availability']['attack'])
        if prior is None or score(actor)>score(prior[1]):
            owners[actor['type']]=(state['input'],actor)
    expected = {12,7,0,2,10,1,3,4}
    assert expected <= set(owners)
    kit_results = []
    for kit in sorted(expected):
        index, actor = owners[kit]
        body = dict(base, kind='identity')
        body['scenario'] = dict(base['scenario'], input=index, actor=actor['id'], participants=[actor['id']])
        info = call('kit-'+str(kit), body)
        wait=[{'type':'end'}]*max(actor['cooldowns'])
        warm=call('kit-warm-'+str(kit),dict(body,kind='evaluate',jobs=[{'id':'ready','prefix':wait}]))['results'][0]
        assert warm['prefix_valid'] and next(c for c in warm['state']['champions'] if c['id']==actor['id'])['alive']
        proposals = warm['actions']
        jobs = [{'id':'end','prefix':wait+[{'type':'end'}]}]
        for action in proposals:
            if action['type'] in ('attack','cast'):
                jobs.append({'id':len(jobs),'prefix':wait+[action]})
        attacks=[x for x in proposals if x['type']=='attack']
        ready_actor=next(c for c in warm['state']['champions'] if c['id']==actor['id'])
        resets=[a['slot'] for a in ready_actor['abilities'] if a['kind']==1]
        for attack_action in attacks[:1]:
            for slot in resets:
                jobs.append({'id':'reset-after-attack','prefix':wait+[attack_action,{'type':'cast','slot':slot,'target':None,'x':-1,'y':-1}]})
        # All ground positions are actually attempted; refusals remain evidence.
        if len(jobs)>200:
            jobs=jobs[:200]
        body.update(kind='evaluate',jobs=jobs)
        evaluated = call('kit-actions-'+str(kit), body)
        assert all(x['supported'] for x in evaluated['results'])
        applied = [x['trace'][-1]['action'] for x in evaluated['results'] if x['prefix_valid'] and x['trace'] and x['trace'][-1]['applied']]
        assert any(x['type']=='cast' for x in applied), (kit,applied)
        kit_results.append({'kit':kit,'input':index,'actor':actor['id'],'items':actor['items'],'proposals':len(proposals),'attempted':len(jobs),'applied':applied})
    checks += ['eight naturally retained champion kits', 'actual equipment retained', 'both-slot and ground-dash proposal execution']
    baseline = dict(base,kind='baseline',horizon_turns=8,objective={'kind':'farm','group':18,'camp_ids':[48,84],'require_capture':True,'require_depart':True})
    baseline.pop('jobs',None)
    control = call('baseline', baseline)
    assert control['native'] and control['horizon_turns'] == 8
    assert all(t['code'] == 0 for t in control['trace']), control['trace']
    final_group = next(g for g in control['state']['groups'] if g['id']==18)
    assert final_group['state'] == 3 and not final_group['rewardAvailable']
    payout = [event for t in control['trace'] for event in t['events'] if event.get('resource_changes') and any(g['id']==18 for g in event.get('group_changes',[]))]
    assert payout, 'Payout must have exact native group and resource changes on the same step'
    history=control['state']['objective_history']['payouts']
    assert any(p['group']==18 and p['team']==0 and p['attribution_unique'] and p['credited_gold']==60 and p['credited_gems']==4 for p in history)
    assert final_group['captureTeam']==2, 'Native payout clears capture owner; history retains actual paid team'
    checks += ['feasible native greedy control', 'native capture→payout→depart', 'single-step payout attribution']
    bad = dict(base)
    bad['scenario'] = dict(base['scenario'],replay_sha256='0'*64)
    assert not call('wrong-replay-hash',bad,False)['native']
    bad['scenario'] = dict(base['scenario'],verifiedsetup={'hp':1})
    assert not call('unsupported-restore',bad,False)['supported']
    bad['scenario'] = dict(base['scenario'],hp=1)
    assert not call('ignored-semantic-restore',bad,False)['supported']
    # Actual retained ready-dead owner still receives an end input.
    edge=dict(combat,kind='evaluate')
    edge['scenario']=dict(combat['scenario'],input=1580,actor=1,participants=[1,2])
    edge['jobs']=[{'id':'dead-owner','prefix':[]},{'id':'continue','prefix':[{'type':'end'}]}]
    dead=call('dead-owner',edge)
    assert not next(c for c in dead['results'][0]['state']['champions'] if c['id']==1)['alive']
    assert dead['results'][0]['actions']==[{'type':'end'}]
    assert dead['results'][1]['prefix_valid'] and dead['results'][1]['state']['current_owner']==2
    # Actual four-gem owner adjacent to altar19,16: native channel completion.
    edge['scenario']=dict(combat['scenario'],input=1081,actor=1,participants=[1])
    edge['jobs']=[{'id':'altar-proposals','prefix':[]},{'id':'tribute','prefix':[{'type':'submit_gems','x':19,'y':16}]+[{'type':'end'}]*8}]
    tribute=call('tribute',edge)
    assert {'type':'submit_gems','x':19,'y':16} in tribute['results'][0]['actions']
    complete=tribute['results'][1]
    assert complete['prefix_valid'] and complete['state']['scores'][0]>tribute['root_state']['scores'][0]
    assert next(c for c in complete['state']['champions'] if c['id']==1)['gems']==0
    # Native finish is reached by newly ending a retained final owner input.
    edge['scenario']=dict(combat['scenario'],input=26138,actor=1,participants=[1])
    edge['jobs']=[{'id':'finish','prefix':[{'type':'end'}]}]
    terminal=call('terminal',edge)['results'][0]
    assert terminal['prefix_valid'] and terminal['terminal']['reason']=='native-finished'
    assert terminal['state']['moment']['phase']==11 and terminal['actions']==[]
    checks += ['asset tamper fail closed', 'arbitrary restore fail closed','exclusive ability forbids later movement','one-cast allowance across slots','eight typed applied native abilities','native dead-owner continuation','native valid-altar tribute completion','native terminal boundary','native neutral capture owner with retained paid-team witness','stale response fails without overwrite','stable JS and Python identities on every native call']
    summary={'passed':True,'checks':checks,'kits':kit_results,'source_hashes':source_hashes,'prefix_reuse':a.prefix_reuse,'scope':'Bounded real retained replay branches. No broad search optimum/hidden-state serialization claim.'}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'passed':True,'checks':len(checks),'kits':len(kit_results),'output':str(out)}))


if __name__ == '__main__':
    main()
