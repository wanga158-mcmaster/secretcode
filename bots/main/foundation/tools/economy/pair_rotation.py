"""Native equal-distance two-melee elk HP pooling via SDK next-round priority.

Uses arena builds/games and the shared slot pool. Fresh output required.
No item/ability/host-effect changes and no complete-game health/strength qualification.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
import games
NODE=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
BOT='contenders/_foundation-pair-rotation'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    out=Path(sys.argv[1]).resolve()
    if out.exists():raise SystemExit('refusing existing output')
    scratch=ROOT/'.cache/contenders/foundation/pair-rotation'
    scratch.mkdir(parents=True,exist_ok=True)
    spec={'width':50,'height':30,'symmetry':'ROTATIONAL180','champs':3,'spawnA':[2,7,5,10],
          'altars':[[7,5]],'camps':[['elk',12,10]]}
    sp=scratch/'map.json';mp=scratch/'map.map26a'
    sp.write_text(json.dumps(spec)+'\n',encoding='utf-8')
    builder=ROOT/'bots/main/foundation/tools/combat/mkmap.mjs'
    build_map=subprocess.run([str(NODE),'--no-warnings',str(builder),str(sp),str(mp)],capture_output=True,text=True)
    if build_map.returncode:raise SystemExit(build_map.stderr)
    off,on,passive=core.prepare_many([(core.bot(BOT),{},'off'),(core.bot(BOT),{'rotate_pair':'1'},'on'),
                                      (core.bot(BOT),{'active_pair':'0'},'passive')])
    for entry in(off,on,passive):
        if not entry.ok:raise SystemExit(entry.log[-3000:])
    specs=[]
    for arm,entry in(('off',off),('on',on)):
        for side in('A','B'):
            a,b=(entry,passive) if side=='A' else(passive,entry)
            specs.append(games.GameSpec(a,b,str(mp),{'arm':arm,'subject':side}))
    rows=games.play(specs,out,rounds=90,keep_replays='all',quiet=True,resume=False)
    observations=[];failures=[]
    for game,row in zip(specs,rows):
        side=game.tags['subject'];replay=ROOT/row['replay']
        with zipfile.ZipFile(replay) as z:
            raw=z.read('team-'+side.lower()+'.stdout')
            stdout=raw.replace(b'\0',b'').decode('utf-8','replace')
            fuel=z.read('team-'+side.lower()+'.fuel')
        events=[]
        for line in stdout.splitlines():
            if line.startswith('PAIR '):
                event=dict(token.split('=',1) for token in line.split() if '=' in token)
                event['record_kind']=line.split()[1]
                events.append(event)
        values=struct.unpack_from(f'<{(len(fuel)-13)//8}Q',fuel,13)
        budget=values[3 if side=='A' else 7]
        regular=values[8+3+3:]
        basics=[e for e in events if e.get('phase')=='after_basic']
        killed=[e for e in basics if float(e['camp_hp'])==0]
        paid=[e for e in events if int(e.get('gems',0))>0]
        banked=[e for e in events if e.get('phase')=='banked']
        dead=[e for e in events if e['record_kind']=='dead']
        active_ids=(0,1) if side=='A' else(3,4)
        counts={str(actor):sum(int(e['id'])==actor for e in basics) for actor in active_ids}
        priorities=[e for e in events if e['record_kind']=='priority' and int(e['r'])>=30 and int(e['r'])<=56]
        problem=None
        try:
            assert 'error' not in stdout.lower()
            assert killed and paid and banked
            assert int(banked[0]['score'])==15
            assert sum(counts.values())==54
            assert all(e['before_M']==e['after_M'] and e['before_A']==e['after_A']
                       and e['before_C']==e['after_C'] for e in priorities)
            if game.tags['arm']=='on':
                assert not dead and list(counts.values())==[27,27]
                assert all(float(e['hp'])>0 for e in events if e.get('phase')=='helper_released')
            else:
                assert dead and any(int(e['id'])==active_ids[0] for e in dead)
            assert not any(v>=budget for v in regular)
        except(AssertionError,KeyError) as exc:
            problem=repr(exc)
        if problem:failures.append({**game.tags,'error':problem})
        observations.append({**game.tags,'replay':row['replay'],'row':row,'events':events,
                             'basic_counts':counts,'clear_round':int(killed[0]['r']) if killed else None,
                             'first_bag_round':int(paid[0]['r']) if paid else None,
                             'bank_round':int(banked[0]['r']) if banked else None,
                             'dead_actor_rounds':[{k:e[k] for k in('id','r')} for e in dead],
                             'ordinary_peak':max(regular,default=0),'ordinary_budget':budget})
    paths=[Path(__file__),ROOT/'bots/contenders/_foundation-pair-rotation/src/main.cpp',builder,mp,sp,
           ROOT/'docs/reference/current-version.json',ROOT/'sdk/cpp/cambc.h']
    result={'purpose':'actual next-round priority HP pooling, equal-distance Ryth/Marcella single-elk clear/capture/bank',
            'engine':'1.0.3','cli_sdk':'3.0.5','rounds':90,'off_key':off.key,'on_key':on.key,'passive_key':passive.key,
            'source_sha256':{p.relative_to(ROOT).as_posix():sha(p) for p in paths},
            'setup_notes':['arena new rejects underscored scratch name; authorized owned source directory created directly.',
                           'First build6a48aba6be9c failed before games because attack returns void; fixed at source.'],
            'games':observations,'failures':failures,'passed':not failures,
            'limits':['One open lab geometry and one fixed hit-start parity; both sides; no other kits/items/abilities.',
                      'Bounded90-round games are not complete-game health/release/strength qualification.',
                      'Guest logs are not independent camp-target/priority evidence; saved native verification is separate.']}
    (out/'pair-rotation.json').write_text(json.dumps(result,indent=1)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'passed':not failures,'games':len(rows),'failures':failures,
                      'outcomes':[{k:g[k] for k in('arm','subject','clear_round','first_bag_round','bank_round','basic_counts','ordinary_peak')} for g in observations]}))
    return int(bool(failures))


if __name__=='__main__':raise SystemExit(main())
