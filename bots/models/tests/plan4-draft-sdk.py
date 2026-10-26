"""Exercise the production draft adapter (common/draft-sdk.h) through actual SDK Wasm imports.

Only engine metadata and drafting are doubled. No GameRunner or native game is
used. The pinned SDK (sdk/cpp, 12-champion pool) and adapter inputs are copied
once, verified, and compiled at -O3 with the default 64 KiB WASI stack for both
PLAN4_MODEL_ENABLED arms. --sdk2 DIR optionally adds an archived 11-champion SDK2
header folder. Success removes all scratch; failure preserves it with its logs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
from core import CLANG as COMPILER, NODE  # noqa: E402

GUEST = r'''
#include "draft-sdk.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace d = plan4::drafting;
static int callbacks=0, last_profile=-1, last_pick=-1, last_count=-1, last_size=-1;
static int mappings=0, finished=0;
#define EXPORT(name) extern "C" __attribute__((export_name(#name))) int name()
EXPORT(test_callbacks){return callbacks;}
EXPORT(test_profile){return last_profile;}
EXPORT(test_pick){return last_pick;}
EXPORT(test_count){return last_count;}
EXPORT(test_size){return last_size;}
EXPORT(test_mappings){return mappings;}
EXPORT(test_finished){return finished;}
static void require(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
static void mapping_checks() {
    require(d::sdk::legal_mask()==((1u<<EXPECT_POOL)-1), "legal SDK mask");
    for(int n=0;n<EXPECT_POOL;++n) {
        const auto raw=static_cast<cambc::champion_draft>(n);
        const auto pick=d::sdk::from_sdk(raw);
        require(pick!=d::Pick::count, "legal SDK value rejected");
        require(static_cast<int>(d::sdk::to_sdk(pick))==n, "draft ABI round trip");
        ++mappings;
    }
    for(int raw:{-1,EXPECT_POOL,99})
        require(d::sdk::from_sdk(static_cast<cambc::champion_draft>(raw))==d::Pick::count,
                "unknown SDK enum accepted");
    for(auto invalid:{d::Pick::count,static_cast<d::Pick>(99)}) {
        bool threw=false;
        try{(void)d::sdk::to_sdk(invalid);}catch(const std::exception&){threw=true;}
        require(threw,"unknown internal pick accepted");
    }
    if constexpr(EXPECT_POOL==11) {
        bool threw=false;
        try{(void)d::sdk::to_sdk(d::Pick::heart_of_ruin);}catch(const std::exception&){threw=true;}
        require(threw,"Heart accepted by SDK2");
    } else {
        require(d::sdk::from_sdk(d::sdk::to_sdk(d::Pick::heart_of_ruin))==d::Pick::heart_of_ruin,
                "Heart missing from SDK3");
    }
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try{mapping_checks();}catch(const std::exception& e){std::fprintf(stderr,"mapping: %s\n",e.what());return 3;}
    const int profile=std::atoi(argv[1]);
    const bool expect_error=std::atoi(argv[2])!=0;
    bool threw=false;
    try {
        bool complete=false;
        for(int n=0;n<=d::pick_count;++n) {
            const bool picked=d::sdk::run(static_cast<d::Profile>(profile),
                [](d::Profile p,cambc::champion_draft pick,int count,int size){
                    ++callbacks;last_profile=int(p);last_pick=int(pick);last_count=count;last_size=size;
                });
            if(!picked){complete=true;break;}
        }
        require(complete,"draft failed to complete within bounded calls");
    }catch(const std::exception&){threw=true;}
    if(threw!=expect_error){std::fprintf(stderr,"unexpected error state: %d\n",int(threw));return 4;}
    finished=1;
    return 0;
}
'''

HOST = r'''
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {WASI} from 'node:wasi';
const configuration=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const results=[];
let scenarioCount=0, actionCount=0, duplicateEnemyPicks=0, duoCases=0;
const totals={};
async function run(module,spec,pool,mode) {
  const state={selected:[...(spec.selected??[])],actions:[],calls:{},designs:new Set(),consumables:new Set()};
  let instance;
  const wasi=new WASI({version:'preview1',args:['draft-test',String(spec.profile),String(+!!spec.invalid)],
    env:{},preopens:{},returnOnExit:true});
  const view=()=>new DataView(instance.exports.memory.buffer);
  const set=(p,n)=>view().setUint32(p,n,true);
  const zero=(p,n)=>new Uint8Array(instance.exports.memory.buffer,p,n).fill(0);
  const allowed={
    game_settings:()=>0,
    shared_memory_size:p=>{set(p,0);return 0;},
    bc_metadata:p=>{[45,30,6,4,8,192,3072].forEach((n,k)=>set(p+4*k,n));return 0;},
    consumable_info:(id,p)=>{assert([0,2].includes(id));state.consumables.add(id);zero(p,8);return 0;},
    champion_design:(id,p)=>{assert(id>=0&&id<pool+1);state.designs.add(id);zero(p,128);return 0;},
    draft_memory_size:p=>{set(p,0);return 0;},
    read_draft_memory:()=>0,
    my_champion:()=>-1,
    bc_map_symmetry:p=>{set(p,0);return 0;},
    bc_map_mask:(id,p)=>{zero(p,192);return 0;},
    bc_Bitboard_Zero:p=>{zero(p,192);return 0;},
    my_team:()=>spec.side,
    spawn_region:(team,p)=>{zero(p,16);return 0;},
    bc_get_last_error:()=>0,
    draft_num_rounds:p=>{view().setUint8(p,spec.size);return spec.roundError??0;},
    draft_get:(team,round,p)=>{
      assert.equal(team,spec.side,'enemy draft queried');
      assert(round>=0&&round<spec.size,'draft round outside queried selection count');
      if(spec.getError)return spec.getError;
      if(round>=state.selected.length)return 16;
      view().setInt32(p,state.selected[round],true);return 0;
    },
    draft_champion:pick=>{
      assert(!spec.invalid,'action on invalid observation');
      assert(pick>=0&&pick<pool,'pick outside actual SDK pool');
      assert(!state.selected.includes(pick),'duplicate own pick');
      const e=instance.exports;
      assert.equal(e.test_callbacks(),state.actions.length+1,'callback must precede exactly one action');
      assert.equal(e.test_profile(),spec.profile);
      assert.equal(e.test_pick(),pick,'diagnostic pick differs from submitted pick');
      assert.equal(e.test_count(),state.selected.length,'diagnostic completed PICK count');
      assert.equal(e.test_size(),spec.size,'diagnostic queried selection count');
      if((spec.enemy??[]).includes(pick))++duplicateEnemyPicks;
      state.selected.push(pick);state.actions.push(pick);++actionCount;return 0;
    },
  };
  const env={};
  for(const imp of WebAssembly.Module.imports(module)) {
    if(imp.module==='wasi_snapshot_preview1')continue;
    assert.equal(imp.module,'env');assert.equal(imp.kind,'function');
    env[imp.name]=(...args)=>{
      assert(allowed[imp.name],'unexpected SDK query/action '+imp.name);
      state.calls[imp.name]=(state.calls[imp.name]??0)+1;
      return allowed[imp.name](...args);
    };
  }
  instance=await WebAssembly.instantiate(module,{...wasi.getImportObject(),env});
  assert.equal(wasi.start(instance),0,'guest failed '+JSON.stringify(spec));
  const e=instance.exports;
  assert.equal(e.test_finished(),1);assert.equal(e.test_mappings(),pool);
  assert.equal(state.designs.size,pool+1,'physical designs include two duo actors');
  assert.equal(state.consumables.size,2);
  assert.equal(e.test_callbacks(),state.actions.length);
  if(spec.invalid){assert.equal(state.actions.length,0);assert.equal(e.test_callbacks(),0);}
  else {
    assert.equal(state.selected.length,spec.size,'one engine action per requested selection');
    assert.equal(new Set(state.selected).size,spec.size);
    const actors=spec.size+Number(state.selected.includes(3));
    assert(actors<=5,'profile exceeded current five-actor storage capacity');
    if(state.selected.includes(3)){assert(spec.size<5);++duoCases;}
    assert.equal(state.calls.draft_num_rounds,2*(state.actions.length+1));
    assert.equal(state.calls.draft_get,spec.size*(state.actions.length+1));
  }
  for(const [name,count]of Object.entries(state.calls))totals[name]=(totals[name]??0)+count;
  const result={sdk:pool===11?'2.2.5':'3.0.2',mode,...spec,picks:state.actions,calls:state.calls};
  console.log(JSON.stringify(result));results.push(result);++scenarioCount;
  return state.selected;
}
for(const bundle of configuration.modules) {
  bundle.module=await WebAssembly.compile(fs.readFileSync(bundle.path));
}
for(const pool of [11,12]) {
  const modes=configuration.modules.filter(m=>m.pool===pool);
  for(let profile=0;profile<17;++profile)for(const size of [3,4,5]) {
    let expected;
    for(const side of [0,1])for(const bundle of modes) {
      const selected=await run(bundle.module,{profile,size,side,enemy:expected??[]},pool,bundle.mode);
      if(expected)assert.deepEqual(selected,expected,'active/control or team-side draft policy changed');
      else expected=selected;
    }
  }
  for(const bundle of modes)for(const side of [0,1]) {
    const invalid=[
      {size:0},{size:13},{size:255},{size:pool+1},
      {size:3,selected:[0,0]},{size:3,selected:[-1]},
      {size:3,selected:[pool]},{size:3,selected:[99]},
      {size:3,profile:99},{size:3,roundError:1},{size:3,getError:1},
    ];
    for(const spec of invalid)await run(bundle.module,{profile:0,side,invalid:true,...spec},pool,bundle.mode);
    for(const size of [3,4,5]) {
      const selected=Array.from({length:size},(_,n)=>n===3?5:n);
      // A five-choice completed fixture must respect the same actor capacity.
      if(size===5)selected[3]=5;
      await run(bundle.module,{profile:0,side,size,selected},pool,bundle.mode);
    }
  }
}
assert(duplicateEnemyPicks>0,'enemy duplicate availability was never exercised');
assert(duoCases>0,'one-selection two-actor duo was never exercised');
const summary={scenarios:scenarioCount,actions:actionCount,duplicateEnemyPicks,duoCases,
  sdkPools:[11,12],profiles:17,teamSizes:[3,4,5],sides:[0,1],modelEnabled:[0,1],
  gameplayActions:0,enemyQueries:0,totals};
fs.writeFileSync(configuration.summary,JSON.stringify(summary,null,2)+'\n');
console.log('SUMMARY '+JSON.stringify(summary));
'''


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--compiler', default=COMPILER)
    cli.add_argument('--node', default=NODE)
    cli.add_argument('--sdk2', type=Path, default=None,
                     help='Optional archived SDK2 (11-champion) header folder; skipped when absent')
    cli.add_argument('--sdk3', type=Path, default=ROOT / 'sdk/cpp')
    args = cli.parse_args()
    started = time.perf_counter()
    cache = ROOT / '.cache/models-tests'
    cache.mkdir(parents=True, exist_ok=True)
    retained = Path(tempfile.mkdtemp(prefix='plan4-draft-sdk-', dir=cache)).resolve()
    scratch = retained / 'work'
    scratch.mkdir()
    print(f'Scratch: {retained}', flush=True)
    originals, frozen = {}, {}

    def source_bytes(path):
        return path.read_bytes()
    pools = ([('sdk2', 11)] if args.sdk2 else []) + [('sdk3', 12)]
    try:
        sources = {'adapter': ROOT / 'bots/models/common'}
        sources.update({label: getattr(args, label).resolve() for label, _ in pools})
        for label, folder in sources.items():
            names = ['draft-sdk.h', 'draft.h'] if label == 'adapter' else ['cambc.h', 'imports.h', 'wasm_simd128.h']
            for name in names:
                path = folder / name
                raw = source_bytes(path)
                originals[path] = raw
                target = scratch / label / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
                frozen[target] = raw
        assert all(source_bytes(path) == raw for path, raw in originals.items()), 'Source changed while snapshotting'
        assert all(path.read_bytes() == raw for path, raw in frozen.items()), 'Snapshot verification failed'
        verified_versions = {}
        for label, expected_pool in pools:
            header = (scratch / label / 'cambc.h').read_text(encoding='utf8')
            declaration = re.search(r'enum class champion_draft\s*:\s*std::int32_t\s*\{(.*?)\};', header, re.S)
            assert declaration, f'{label}: draft enum declaration not found'
            uncommented = re.sub(r'//[^\n]*', '', declaration[1])
            names = [entry.strip().split('=')[0].strip() for entry in uncommented.split(',') if entry.strip()]
            assert len(names) == expected_pool, (f'{label}: snapshot has {len(names)} draft choices; '
                f'expected {expected_pool}. Pass explicit --sdk2/--sdk3 paths to verified archived/current headers.')
            assert ('heart_of_ruin' in names) == (expected_pool == 12)
            verified_versions[label] = {'draftChoices': names, 'pool': expected_pool,
                                       'physicalDesigns': expected_pool+1}
        print(f"{'/'.join(label.upper() for label, _ in pools)} and production adapter snapshots frozen and verified.",
              flush=True)
        prepared = time.perf_counter()
        source = scratch / 'draft-test.cpp'
        source.write_text(GUEST, encoding='utf8')
        driver = scratch / 'run.mjs'
        driver.write_text(HOST, encoding='utf8')
        eh = Path(args.compiler).parent.parent / 'share/wasi-sysroot/lib/wasm32-wasip1/eh'
        modules = []
        for label, pool in pools:
            for mode in (0, 1):
                assert all(path.read_bytes() == raw for path, raw in frozen.items()), 'Frozen inputs changed before compile'
                module = scratch / f'{label}-enabled{mode}.wasm'
                command = [args.compiler, '-std=c++20', '-O3', '-Wall', '-Wextra', '-Werror',
                           '-fwasm-exceptions', '-mllvm', '-wasm-use-legacy-eh=false', '--no-wasm-opt',
                           f'-L{eh}', '-lunwind', f'-DEXPECT_POOL={pool}',
                           f'-DPLAN4_MODEL_ENABLED={mode}', f'-I{scratch / "adapter"}',
                           f'-I{scratch / label}', str(source), '-o', str(module)]
                result = subprocess.run(command, capture_output=True)
                (retained / f'{label}-enabled{mode}.compile.log').write_bytes(result.stdout + result.stderr)
                assert result.returncode == 0, result.stderr.decode(errors='replace')[:3500]
                modules.append({'path': str(module), 'pool': pool, 'mode': mode, 'sha256': digest(module.read_bytes())})
        compiled = time.perf_counter()
        configuration = scratch / 'configuration.json'
        summary_path = retained / 'summary.json'
        configuration.write_text(json.dumps({'modules': modules, 'summary': str(summary_path)}), encoding='utf8')
        assert all(path.read_bytes() == raw for path, raw in frozen.items()), 'Frozen inputs changed before execution'
        run = subprocess.run([args.node, str(driver), str(configuration)], capture_output=True)
        (retained / 'host.stdout.log').write_bytes(run.stdout)
        (retained / 'host.stderr.log').write_bytes(run.stderr)
        assert run.returncode == 0, (run.stdout + run.stderr).decode(errors='replace')[-3500:]
        summary = json.loads(summary_path.read_text())
        tested = time.perf_counter()
        assert all(path.read_bytes() == raw for path, raw in frozen.items()), 'Frozen inputs changed after execution'
        # Compilation/execution used only the frozen copies; the adapter must not have changed meanwhile.
        for path, raw in originals.items():
            if path.parent == sources['adapter']:
                assert digest(source_bytes(path)) == digest(raw), 'Production draft source changed; rerun against final adapter'
        assert scratch.parent == retained and retained.parent == cache.resolve()
        assert scratch.name == 'work' and retained.name.startswith('plan4-draft-sdk-')
        assert all(path.resolve().is_relative_to(retained) for path in retained.rglob('*'))
        shutil.rmtree(retained)
        assert not retained.exists()
        print(json.dumps({**summary, 'sdkPools': [pool for _, pool in pools],
                          'verifiedPools': {label: v['pool'] for label, v in verified_versions.items()}},
                         sort_keys=True))
        print(f'Passed in {time.perf_counter() - started:.1f}s (prepare {prepared - started:.1f}s, compile '
              f'{compiled - prepared:.1f}s, run {tested - compiled:.1f}s); scratch removed.')
    except BaseException:
        print(f'FAILED: retained all scratch/logs at {retained}', flush=True)
        raise


if __name__ == '__main__':
    main()
