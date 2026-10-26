"""Exercise the exact guarded production frontier against its generic source oracle."""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core,compiled_runner as runner,wasi
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def pin(path):return {'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path),'bytes':path.stat().st_size}
def write(path,value):
    with path.open('x',encoding='utf-8',newline='\n') as out:json.dump(value,out,indent=2);out.write('\n')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True,type=Path);ap.add_argument('--gate',required=True,type=Path);args=ap.parse_args()
    out=args.out.resolve();gate=args.gate.resolve()
    assert out.is_relative_to(ROOT/'.cache') and not out.exists() and json.loads(gate.read_text())['pass']
    assert sha(ROOT/'tools/arena/compiled_runner.py')=='feaa0715e0cf5160715772fbc369dbf4ef9005250ece75d8c3a55ed9ef4ababd'
    out.mkdir(parents=True)
    runtime=ROOT/'bots/events/common/src/runtime.h';body=runtime.read_text(encoding='utf-8')
    start=body.index('namespace p11_overlap {');end=body.index('#endif',start)
    actual=body[start:end];assert actual.count('unique_three_frontier')==1
    specimen=Path(__file__).with_name('frontier-scenarios.cpp.inc')
    test=specimen.read_text(encoding='utf-8')
    test=test.replace('#include "../overlap/src/policy.h"',f'#include "{(ROOT / "bots/events/overlap/src/policy.h").as_posix()}"')
    test=test.replace('#include "../overlap/src/frontier.h"',actual)
    source=out/'case.cpp';source.write_text(test,encoding='utf-8',newline='\n');binary=out/'case.wasm'
    commands=[('compile',[str(core.CLANG),*wasi.FLAGS,'-Werror',f'-I{ROOT / "sdk/cpp"}',str(source),f'-L{wasi.EH}','-lunwind','-o',str(binary)]),
              ('run',[str(core.NODE),'--disable-warning=ExperimentalWarning',str(wasi.DRIVER),str(binary)])]
    processes=[]
    for name,argv in commands:
        step=out/name;step.mkdir();result=runner._slotted_command(lambda count:argv,ROOT,step,600,jobs=1);processes.append({'step':name,'process':result})
        assert result['tree_exit_confirmed'] and result['returncode']==0 and not result['timed_out'] and not result['forced_termination'],result
    write(out/'result.json',{'runtime':pin(runtime),'supplied_assertions':pin(specimen),'source':pin(source),'gate':pin(gate),
        'driver':pin(Path(__file__).resolve()),'processes':processes,'pass':True,'mirror_implementation':False,'jobs_running':False})
    print(json.dumps({'pass':True,'production_helper_extracted':True,'random_frames':2048,'stable_tie_pairs':2916,'jobs_running':False}))
if __name__=='__main__':main()
