"""Native Vesper MOVE -> RESET -> BASIC, without an earlier basic. Four bounded games.

  python bots/main/foundation/tools/micro/move_reset.py FRESH-OUTPUT
Outputs must be fresh. No full-game strength or health qualification.
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
BOT='contenders/_micro-move-reset'

def main():
    out=Path(sys.argv[1]).resolve()
    if out.exists():raise SystemExit('refusing existing output')
    scratch=ROOT/'.cache/contenders/micro/move-reset'
    scratch.mkdir(exist_ok=True)
    spec={'width':30,'height':30,'symmetry':'ROTATIONAL180','champs':3,'spawnA':[0,0,3,3],
          'altars':[[0,29]],'camps':[['gold_cluster',7,6],['wolf',8,6]]}
    sp=scratch/'map.json';mp=scratch/'map.map26a'
    sp.write_text(json.dumps(spec))
    builder=ROOT/'bots/main/foundation/tools/combat/mkmap.mjs'
    p=subprocess.run([str(NODE),'--no-warnings',str(builder),str(sp),str(mp)],capture_output=True,text=True)
    if p.returncode:raise SystemExit(p.stderr)
    off,on=core.prepare_many([(core.bot(BOT),{},'off'),(core.bot(BOT),{'move_reset':'1'},'on')])
    for e in(off,on):
        if not e.ok:raise SystemExit(e.log[-3000:])
    specs=[]
    for arm,ent in(('off',off),('on',on)):
        for side in('A','B'):
            a,b=(ent,off) if side=='A' else(off,ent)
            specs.append(games.GameSpec(a,b,str(mp),{'arm':arm,'subject':side}))
    rows=games.play(specs,out,rounds=33,keep_replays='all',quiet=True,resume=False)
    observed=[];failures=[]
    for spec,row in zip(specs,rows):
        replay=ROOT/row['replay'];side=spec.tags['subject']
        with zipfile.ZipFile(replay) as z:
            raw=z.read('team-'+side.lower()+'.stdout')
            stdout=raw.replace(b'\0',b'').decode('utf-8','replace')
            fuel=z.read('team-'+side.lower()+'.fuel')
            values=struct.unpack_from(f'<{(len(fuel)-13)//8}Q',fuel,13)
            budget=values[3 if side.upper()=='A' else 7]
        phases={}
        for line in stdout.splitlines():
            if line.startswith('V '):
                d=dict(x.split('=',1) for x in line.split()[1:] if '=' in x)
                phases[d['phase']]=d
        error=None
        try:
            assert phases['before_step']['A']=='1' and phases['before_step']['M']=='1'
            step=phases['after_step'];assert step['ok']=='1' and step['A']=='0' and step['M']=='0' and step['C']=='1'
            if spec.tags['arm']=='on':
                reset=phases['after_reset'];assert reset['ok']=='1' and reset['A']=='1' and reset['M']=='0' and reset['C']=='0'
                assert phases['after_basic']['hp']=='1295' and phases['done']['A']=='0'
            else:assert phases['done']['hp']=='1350' and 'after_reset' not in phases
            assert 'error' not in stdout.lower()
            assert not any(v>=budget for v in values[8+3+3:])
        except (AssertionError,KeyError) as e:error=repr(e)
        if error:failures.append({**spec.tags,'error':error,'stdout':stdout})
        observed.append({**spec.tags,'replay':row['replay'],'row':row,'phases':phases,
                         'ordinary_peak':max(values[8+3+3:],default=0),'ordinary_budget':budget})
    files=[Path(__file__),ROOT/'bots/contenders/_micro-move-reset/src/main.cpp',ROOT/'sdk/cpp/cambc.h',mp]
    result={'purpose':'Vesper MOVE->RESET->BASIC without prior basic; bounded native mechanism only',
            'off_key':off.key,'on_key':on.key,'engine':'1.0.3','rounds':33,'games':observed,
            'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
            'failures':failures,'passed':not failures}
    (out/'move-reset.json').write_text(json.dumps(result,indent=1))
    print(json.dumps({'passed':not failures,'games':len(rows),'failures':failures}))
    return int(bool(failures))
if __name__=='__main__':raise SystemExit(main())
