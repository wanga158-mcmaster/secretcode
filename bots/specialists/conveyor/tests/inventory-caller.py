"""Actual whole-Bot/real-SDK inventory caller fixtures with an explicit SDK host double.

Run directly with arena's Python. No shared suite runner, native engine or game is used.
Generated access-only copies and reactors live in the caller cache; main.cpp stays intact.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
ATTEMPT = int(sys.argv[1]) if len(sys.argv) > 1 else time.time_ns()
assert ATTEMPT > 1
BUILD = ROOT / f'.cache/arena/orchestration/conveyor-inventory/caller-{ATTEMPT}'
OUT = ROOT / f'results/population/conveyor-inventory/caller-attempt-{ATTEMPT}'
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import wasi

WRAPPER = r'''
#include <optional>
#include <cstring>
#define main fixture_original_main
#include "repo/bots/specialists/conveyor/src/main.cpp"
#undef main
namespace { std::optional<Bot> fixture_bot; std::string fixture_error; }
#define EX(name) extern "C" __attribute__((export_name(#name)))
EX(fixture_call) int fixture_call(int mode) {
    try {
        fixture_error.clear();
        if (!fixture_bot) fixture_bot.emplace();
        if (mode == 0) fixture_bot->refresh_equipment();
        else if (mode == 1) fixture_bot->shop();
        else if (mode == 2) fixture_bot->turn();
        else return -2;
        return 0;
    } catch (const cambc::error& e) { fixture_error=e.what(); return static_cast<int>(e.code()); }
      catch (const std::exception& e) { fixture_error=e.what(); return -1; }
}
EX(fixture_error_text) const char* fixture_error_text() { return fixture_error.c_str(); }
EX(fixture_value) double fixture_value(unsigned index) {
    if (!fixture_bot) return -999;
    switch(index) {
      case 0:return fixture_bot->monster_shards_; case 1:return fixture_bot->jungle_shards_;
      case 2:return fixture_bot->camp_multiplier_; case 3:return fixture_bot->incoming_multiplier_;
      case 4:return fixture_bot->purchases_; case 5:return fixture_bot->initialized_;
      case 6:return static_cast<int>(fixture_bot->state_); case 7:return fixture_bot->recovering_;
      case 8:return fixture_bot->target_; default:return -998;
    }
}
EX(fixture_id) unsigned fixture_id(unsigned index) {
    switch(index) {
      case 0:return static_cast<unsigned>(cambc::item_type::monster_shard_of_strength);
      case 1:return static_cast<unsigned>(cambc::item_type::jungle_shard_of_health);
      case 2:return static_cast<unsigned>(cambc::item_type::power_shard_of_strength);
      case 3:return static_cast<unsigned>(cambc::item_type::monster_emblem_of_strength);
      case 4:return static_cast<unsigned>(cambc::item_type::jungle_emblem_of_health);
      case 5:return static_cast<unsigned>(cambc::item_type::durability_emblem_of_health);
      default:return static_cast<unsigned>(cambc::item_type::count);
    }
}
// Typed host records share the actual SDK declarations. These are declared
// synthetic metadata, not engine metadata or replacements for checked wrappers.
EX(fixture_size) unsigned fixture_size(unsigned kind) {
    switch(kind) {
      case 0:return sizeof(cambc::detail::metadata); case 1:return sizeof(cambc::detail::game_settings);
      case 2:return sizeof(cambc::detail::champion_design); case 3:return sizeof(cambc::detail::item_info);
      case 4:return sizeof(cambc::detail::champion_info); default:return 0;
    }
}
EX(fixture_fill) void fixture_fill(unsigned kind,unsigned id,void* out) {
    if(kind==0) { *static_cast<cambc::detail::metadata*>(out)={8,8,1,1,1,8,128}; return; }
    if(kind==1) {
        auto& v=*static_cast<cambc::detail::game_settings*>(out); v={};
        v.capture_time=5;v.camp_sleep_time=8;v.respawn_time=10;v.recall_time=3;
        v.max_rounds=2000;v.winning_gems=1000;v.submit_gems_range=1;return;
    }
    if(kind==2) {
        auto& v=*static_cast<cambc::detail::champion_design*>(out);v={};
        v.definition.type=static_cast<int>(id);v.base_stats={5,1,1,400,0,40,0,0,0,2};return;
    }
    if(kind==3) {
        auto& v=*static_cast<cambc::detail::item_info*>(out);v={};v.cost=10;v.purchasable=1;
        if(id==fixture_id(0)) { v.combine_count=4;v.result=fixture_id(3); }
        if(id==fixture_id(1)) { v.cost=20;v.combine_count=4;v.result=fixture_id(4); }
        if(id==fixture_id(2)) v.cost=30;
        if(id==fixture_id(3)) {v.passive_magnitude=.5f;v.purchasable=0;}
        if(id==fixture_id(4)) {v.passive_magnitude=.25f;v.purchasable=0;}
        if(id==fixture_id(5)) {v.passive_magnitude=.1f;v.purchasable=0;}
        return;
    }
    if(kind==4) { *static_cast<cambc::detail::champion_info*>(out)={0,11,0,0,360,400,40,0,1,1,1|8|16|2048};return; }
}
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(arm, sources):
    base = BUILD / arm
    applied = core.apply_overrides(core.Bot('specialists', 'conveyor'), sources,
                                   {'CONVEYOR_REFRESH_INVENTORY_ONCE': '1'}) if arm == 'on' else dict(sources)
    key = ROOT / 'bots/specialists/conveyor/src/main.cpp'
    original = applied[key]
    assert original.count(b'class Bot {') == 1
    public = original.replace(b'class Bot {', b'class Bot {\npublic:', 1)
    assert public.replace(b'class Bot {\npublic:', b'class Bot {', 1) == original
    for path, content in applied.items():
        target = base / 'repo' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(public if path == key else content)
    fixture = base / 'fixture.cpp'
    fixture.write_text(WRAPPER, encoding='utf-8', newline='\n')
    wasm = base / 'caller.wasm'
    with core.Slots(1, quiet=True):
        ok, log = wasi.compile_fixture(fixture, wasm, flags=['-mexec-model=reactor', '-Wno-error=sign-compare'])
    (base / 'compile.log').write_text(log, encoding='utf-8', newline='\n')
    if not ok:
        raise RuntimeError(arm + ': ' + log)
    return {'arm': arm, 'source_key': core.version_of(applied, {'CONVEYOR_REFRESH_INVENTORY_ONCE': '1'} if arm == 'on' else None),
            'applied_source': {p.relative_to(ROOT).as_posix():sha(v) for p,v in applied.items()},
            'access_patch': 'one class Bot public visibility insertion only; exact inverse verified',
            'wrapper_sha256':sha(fixture.read_bytes()), 'wasm':wasm.relative_to(ROOT).as_posix(),
            'wasm_sha256':sha(wasm.read_bytes()), 'compile_log':(base/'compile.log').relative_to(ROOT).as_posix()}


def main():
    gate = json.loads((ROOT/'results/population/conveyor-inventory/off-byte-gate.json').read_text(encoding='utf-8'))
    assert gate['pass']
    sources = core.read_closure(core.Bot('specialists','conveyor'))
    assert {p.relative_to(ROOT).as_posix():sha(v) for p,v in sources.items()} == gate['off_source']
    assert not BUILD.exists(), 'Frozen caller build output already exists; do not silently reuse.'
    OUT.mkdir(parents=True, exist_ok=False)
    for source in [Path(__file__), Path(__file__).with_suffix('.mjs')]:
        (OUT/source.name).write_bytes(source.read_bytes())
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(build, arm, sources) for arm in ['off','on']]
        arms = [future.result() for future in futures]
    inputs = {'schemaVersion':1,'kind':'actual whole-Bot real-SDK caller with synthetic import host',
              'arms':arms,'off_byte_gate':gate,'wrapper_sha256':sha(WRAPPER.encode()),
              'fixture_sources':{Path(__file__).relative_to(ROOT).as_posix():sha(Path(__file__).read_bytes()),
                 Path(__file__).with_suffix('.mjs').relative_to(ROOT).as_posix():sha(Path(__file__).with_suffix('.mjs').read_bytes()),
                 'sdk/cpp/cambc.h':sha((ROOT/'sdk/cpp/cambc.h').read_bytes()),
                 'sdk/cpp/imports.h':sha((ROOT/'sdk/cpp/imports.h').read_bytes())},
              'limits':['Synthetic item metadata, purchase effects and world; checked SDK wrappers are real.',
                        'Fuel responses are identical declared host inputs, not native fuel measurements or suspension.',
                        'No removed-call-only error equivalence or native mechanism/health claim.']}
    (OUT/'caller-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n',encoding='utf-8',newline='\n')
    with core.Slots(1,quiet=True):
        proc=subprocess.run([str(core.NODE),'--disable-warning=ExperimentalWarning',str(Path(__file__).with_suffix('.mjs')),
                             str(ROOT/arms[0]['wasm']),str(ROOT/arms[1]['wasm']),str(OUT/'caller-tests.json')],
                             cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
    (OUT/'caller.stdout').write_text(proc.stdout,encoding='utf-8',newline='\n')
    (OUT/'caller.stderr').write_text(proc.stderr,encoding='utf-8',newline='\n')
    assert core.read_closure(core.Bot('specialists','conveyor')) == sources, 'Source changed during caller tests.'
    print(proc.stdout,end='');print(proc.stderr,end='',file=sys.stderr)
    return proc.returncode


if __name__ == '__main__':
    raise SystemExit(main())
