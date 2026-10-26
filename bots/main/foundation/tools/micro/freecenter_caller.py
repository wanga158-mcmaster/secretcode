"""Reach actual free-center enemy combat caller, then observe saved OFF/ON effects.
  python freecenter_caller.py FRESH-OUTPUT
Four bounded native games, both subject sides, no strategic-planner admission claim.
"""
import concurrent.futures,hashlib,json,subprocess,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core,games
NODE=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
BOT='contenders/_micro-freecenter-probe'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=Path(sys.argv[1]).resolve()
    if out.exists():raise SystemExit('refusing existing output')
    scratch=ROOT/'.cache/contenders/micro/freecenter-caller';scratch.mkdir(exist_ok=True)
    spec={'width':30,'height':30,'symmetry':'ROTATIONAL180','champs':5,'spawnA':[0,0,3,3],
          'altars':[[0,29]],'camps':[['wolf',18,10]]}
    sp=scratch/'map.json';mp=scratch/'map.map26a';sp.write_text(json.dumps(spec))
    proc=subprocess.run([str(NODE),'--no-warnings',str(ROOT/'bots/main/foundation/tools/combat/mkmap.mjs'),str(sp),str(mp)],capture_output=True,text=True)
    if proc.returncode:raise SystemExit(proc.stderr)
    bot=core.bot(BOT);contents=core.read_closure(bot)
    entries=core.prepare_many([(bot,{'probe_active_side':str(side),'free_aoe_centers':str(bool(arm)).lower()},f'{arm}-{side}',contents)
                              for side in(0,1) for arm in(0,1)])
    for ent in entries:
        if not ent.ok:raise SystemExit(ent.log[-4000:])
    specs=[]
    for side in(0,1):
        off,on=entries[side*2:side*2+2]
        for arm,ent in(('off',off),('on',on)):
            a,b=(ent,off) if side==0 else(off,ent)
            specs.append(games.GameSpec(a,b,str(mp),{'arm':arm,'subject':'B' if side else 'A'}))
    rows=games.play(specs,out,rounds=43,keep_replays='all',quiet=True,resume=False)
    observations=[]
    def observe(pair):
        gs,row=pair;replay=ROOT/row['replay']
        argv=[str(NODE),'--no-warnings',str(ROOT/'tools/inspect-strategy-replay.mjs'),'--replay',str(replay),
              '--actor','all','--trace','1','--from-round','40','--to-round','40','--kinds','4,5,6','--nearby-radius','5']
        p=subprocess.run(argv,capture_output=True)
        if p.returncode:raise RuntimeError(p.stderr.decode('utf-8','replace')[-3000:])
        doc=json.loads(p.stdout);tag=gs.tags['arm']+'-'+gs.tags['subject']
        (out/(tag+'.json')).write_bytes(p.stdout)
        with zipfile.ZipFile(replay) as z:stdout=z.read('team-'+gs.tags['subject'].lower()+'.stdout').replace(b'\0',b'').decode('utf-8','replace')
        return gs,row,doc,stdout
    with concurrent.futures.ThreadPoolExecutor() as pool:observed=list(pool.map(observe,zip(specs,rows)))
    checked=[];before={};failures=[]
    for gs,row,doc,stdout in observed:
        t=doc['interactions'];team=0 if gs.tags['subject']=='A' else 1
        actor=next(c['id'] for c in t['entities'] if c['team']==team and c['type']==0)
        events=[e for e in t['events'] if e['actor']==actor]
        cast=next((e for e in events if e['kind']=='cast' and e['action']['abilitySlot']==0),None)
        if cast is None:failures.append({**gs.tags,'reason':'no actual AoE cast','stdout':stdout});continue
        base=t['states'][cast['actorState'][0]];before[(gs.tags['arm'],gs.tags['subject'])]=base
        expected=[17,9] if gs.tags['arm']=='off' else[17,10]
        if team:expected=[29-expected[0],29-expected[1]]
        if cast['action']['recordedTo']!=expected:failures.append({**gs.tags,'reason':'wrong center','cast':cast})
        enemy_effects=[d for d in cast['observed'] if d['entity'].startswith('champion:') and 'health' in (d.get('before')or{}) and d['entity']!='champion:'+str(actor)]
        if len(enemy_effects)!=(2 if gs.tags['arm']=='off' else 3):failures.append({**gs.tags,'reason':'wrong affected enemy count','effects':enemy_effects})
        if any(d['before']['health']-d['after']['health']!=100 for d in enemy_effects):failures.append({**gs.tags,'reason':'wrong observed HP difference'})
        if any(e['kind']=='move' for e in events):failures.append({**gs.tags,'reason':'unexpected movement'})
        if 'error' in stdout.lower() or 'refusal' in stdout.lower():failures.append({**gs.tags,'reason':'native log failure','stdout':stdout})
        if t['native']['errors'] or doc['stopped']!='round_range_end':failures.append({**gs.tags,'reason':'observer incomplete'})
        checked.append({**gs.tags,'row':row,'castStep':cast['id'],'center':cast['action']['recordedTo'],
                        'actualEffects':cast['observed'],'stdout':stdout})
    for side in('A','B'):
        if before.get(('off',side))!=before.get(('on',side)):failures.append({'subject':side,'reason':'queried pre-cast state mismatch'})
    result={'purpose':'conditional actual enemy combat caller center/effect; not normal strategic admission or strength',
            'passed':not failures,'failures':failures,'entries':[e.key for e in entries],'map_spec':spec,'map_sha256':sha(mp),
            'source_closure_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(raw).hexdigest() for p,raw in contents.items()},
            'driver_sha256':sha(Path(__file__)),'checked':checked}
    (out/'caller.json').write_text(json.dumps(result,indent=1))
    print(json.dumps({'passed':not failures,'games':len(rows),'failures':failures}))
    return int(bool(failures))
if __name__=='__main__':raise SystemExit(main())
