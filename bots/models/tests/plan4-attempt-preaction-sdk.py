"""Actual common pre-action collector with a narrow SDK double; zero engines.

Compile at -O3 with the default 64 KiB WASI stack. Check the same source against
the pinned SDK as well. This verifies source behavior, not native fuel safety.
Failed scratch/logs remain; successful scratch is verified then removed.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[3]
INPUTS = tuple(ROOT / path for path in (
    'bots/models/common/attempt-preaction-sdk.h',
    'bots/models/common/attempt-preaction.h', 'bots/models/common/attempts.h',
    'bots/models/common/pure.h', 'bots/models/common/model-runtime.h',
    'bots/models/common/alternate-route.h',
    'bots/models/common/safe-segment-execution.h', 'bots/models/common/safe-segment.h',
    'bots/models/kernel/src/exact-kit.h', 'bots/models/kernel/src/kernel.h',
    'sdk/cpp/cambc.h', 'sdk/cpp/imports.h',
))


SDK = r'''
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>
namespace cambc {
using handle_champion_t = int;
using handle_camp_group_t = int;
enum class team { ally, enemy };
enum class ability_kind { shield = 7 };
enum class item_type { count = 64 };
enum class error_code { api_not_in_vision = 97, unexpected = 999 };
struct vec2 {int x=0,y=0;};
struct error : std::runtime_error {
 error_code value;
 explicit error(error_code code):std::runtime_error("sdk-double"),value(code){}
 error_code code()const{return value;}
};
template<class T> struct enum_map {
 std::array<std::uint32_t,64> values{};
 auto ptr()const{return values.data();}
};
namespace fixture {
inline int current_actor=7,current_round=10,fuel_calls=0,queries=0,bad_reads=0;
inline int deny_at=0,shift_at=0,owner_at=0,throw_at=0;
inline int own_queries=0,ability_queries=0,inventory_queries=0,ally_masks=0,enemy_masks=0,board_sets=0;
inline int metadata_reads=0,actions=0,hidden_reads=0;
inline bool denied=false,unexpected=false;
inline std::uint64_t entry_fuel=200000;
inline std::array<unsigned,64> counts{};
inline std::array<int,2> kinds{2,7};
inline int width=8,height=8;
inline std::size_t raw_bytes=8;
inline std::array<std::uint64_t,1024> ally_words{},enemy_words{};
// Deliberately permute each raw word's tile bits. Production must use SDK set()
// to filter self, rather than assuming the engine-chunked layout is row-major.
inline unsigned tile_key(vec2 p){return unsigned(p.y*width+p.x)^63u;}
inline void put(team side,vec2 p){auto& words=side==team::ally?ally_words:enemy_words;const auto key=tile_key(p);words[key/64]|=1ULL<<(key%64);}
inline float strength=51.25f,focus=9.625f;
inline void query(){
 ++queries;if(denied)++bad_reads;
 if(queries==shift_at)++current_round;
 if(queries==owner_at)++current_actor;
 if(queries==throw_at)throw error(unexpected?error_code::unexpected:error_code::api_not_in_vision);
}
inline void reset(){
 current_actor=7;current_round=10;fuel_calls=queries=bad_reads=0;
 deny_at=shift_at=owner_at=throw_at=0;denied=unexpected=false;entry_fuel=200000;
 own_queries=ability_queries=inventory_queries=ally_masks=enemy_masks=board_sets=0;
 metadata_reads=actions=hidden_reads=0;counts={};kinds={2,7};
 width=height=8;raw_bytes=8;ally_words={};enemy_words={};put(team::ally,{2,3});
 strength=51.25f;focus=9.625f;
}
}
inline std::uint64_t fuel_remaining(){
 ++fixture::fuel_calls;
 if(fixture::deny_at&&fixture::fuel_calls>=fixture::deny_at){fixture::denied=true;return 54000;}
 return fixture::entry_fuel;
}
inline unsigned round_number(){return fixture::current_round;}
struct AbilityData {int kind=0;};
struct ability {
 int slot=0;
 std::optional<std::variant<AbilityData>> info()const{
  ++fixture::ability_queries;fixture::query();
  if(fixture::kinds[slot]==0)return std::nullopt;
  return std::variant<AbilityData>{AbilityData{fixture::kinds[slot]}};
 }
};
struct champion {
 int id=7;
 operator handle_champion_t()const{return id;}
 int champion_name()const{++fixture::own_queries;fixture::query();return 3;}
 int attack_range()const{++fixture::own_queries;fixture::query();return 1;}
 float strength()const{++fixture::own_queries;fixture::query();return fixture::strength;}
 float focus()const{++fixture::own_queries;fixture::query();return fixture::focus;}
 std::array<ability,2> abilities()const{return {ability{0},ability{1}};}
 enum_map<item_type> item_count()const{
  if(id!=fixture::current_actor){++fixture::hidden_reads;throw std::runtime_error("forbidden other inventory");}
  ++fixture::inventory_queries;fixture::query();
  enum_map<item_type> out;out.values=fixture::counts;return out;
 }
 float health()const{++fixture::hidden_reads;throw std::runtime_error("forbidden health");}
 void attack()const{++fixture::actions;}
};
inline champion& self(){static champion current;current.id=fixture::current_actor;return current;}
class bitboard {
 std::vector<std::uint64_t> words_;
public:
 bitboard():words_(fixture::raw_bytes/sizeof(std::uint64_t)){}
 static std::size_t raw_buffer_size(){return fixture::raw_bytes;}
 const std::uint64_t* ptr()const{return words_.data();}
 std::uint64_t* ptr(){return words_.data();}
 void set(vec2 p,bool value=true){
  ++fixture::board_sets;fixture::query();
  if(p.x<0||p.y<0||p.x>=fixture::width||p.y>=fixture::height)throw std::invalid_argument("outside map");
  const auto key=fixture::tile_key(p);auto& word=words_.at(key/64);
  if(value)word|=1ULL<<(key%64);else word&=~(1ULL<<(key%64));
 }
};
namespace map {
inline int width(){return fixture::width;}
inline int height(){return fixture::height;}
struct camp {
 int value=0;
 int kind()const{++fixture::metadata_reads;return value;}
 float health()const{++fixture::hidden_reads;throw std::runtime_error("forbidden camp health");}
};
struct camp_group {
 int id=19;std::vector<camp> members{{4},{1},{4}};
 operator handle_camp_group_t()const{return id;}
 std::span<const camp> camps()const{return members;}
 int state()const{++fixture::hidden_reads;throw std::runtime_error("forbidden camp state");}
};
}
namespace vision {
inline bitboard champion_tiles(team side){
 if(side==team::ally)++fixture::ally_masks;else ++fixture::enemy_masks;
 fixture::query();bitboard out;
 const auto& words=side==team::ally?fixture::ally_words:fixture::enemy_words;
 for(std::size_t n=0;n<bitboard::raw_buffer_size()/sizeof(std::uint64_t);++n)out.ptr()[n]=words[n];
 return out;
}
}
}
'''


CASES = r'''
#include "bots/models/common/attempt-preaction-sdk.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
namespace a=plan4::attempts;
namespace f=cambc::fixture;
int checks=0,scenarios=0;
void check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
void clean(){check(f::actions==0&&f::hidden_reads==0&&f::bad_reads==0,"no actions, hidden reads, or data reads after budget denial");++scenarios;}
a::Observation own(){a::Observation o;o.actor=7;o.round=10;o.own=o.fresh=true;o.position_known=true;o.x=2;o.y=3;return o;}
a::Identity key(a::Kind kind=a::Kind::one_camp,a::Origin origin=a::Origin::planned){return {kind,origin,11,19,2};}
plan4::Job job(){plan4::Job j;j.kind=plan4::JobKind::farm;j.group=0;j.nominal_travel=9;j.nominal_completion=17;return j;}
bool all_unknown(const a::PreactionEvidence& p){return !p.kit_known&&!p.composition_known&&!p.context_known&&!p.travel_known&&!p.nominal_duration_known;}
int main(){
 int normal_queries=0,normal_fuel=0;
 {f::reset();a::preaction_detail::Budget b(7,10);f::entry_fuel=196500;
  check(b.available(),"first-turn 200k fuel still grants only fixed 4000 allowance with 500 atomic margin");
  f::entry_fuel=196499;check(!b.available(),"first-turn allowance ends at exact atomic boundary");
  plan4::runtime::kernel_binding::KitPhaseBudget shared;shared.begin(56000);
  check(shared.floor==55000,"common sampler does not change shared Kernel exact-kit floor");clean();}
 {f::reset();auto j=job();cambc::map::camp_group g;
  f::counts[17]=9;f::counts[2]=3;auto p=a::collect_preaction(key(),own(),&j,&g);
  check(p.actor==7&&p.round==10&&p.kit_known&&p.composition_known&&p.context_known&&p.travel_known&&p.nominal_duration_known,"planned complete current collection");
  check(p.champion==3&&p.attack_range==1&&p.ability0==2&&p.ability1==7,"ordered own body and slots");
  check(p.strength_bits==std::bit_cast<unsigned>(51.25f)&&p.focus_bits==std::bit_cast<unsigned>(9.625f),"fractional stat IEEE bits survive");
  check(p.item_count==2&&p.items[0].kind==2&&p.items[0].count==3&&p.items[1].kind==17&&p.items[1].count==9,"entire inventory is canonical exact sparse catalog");
  check(p.composition_count==3&&p.composition[0]==1&&p.composition[1]==4&&p.composition[2]==4,"cached public composition is sorted with duplicate kinds");
  check(p.travel==9&&p.nominal_duration==17&&p.context==0,"frozen elapsed job duration, travel, personal solo context");
  check(f::inventory_queries==1&&f::own_queries==8&&f::ability_queries==2&&f::ally_masks==1&&f::enemy_masks==1&&f::board_sets==1,"two actual body passes, one inventory, two ordered slots, complete personal masks and one SDK self-tile clear");
  normal_queries=f::queries;normal_fuel=f::fuel_calls;f::counts[2]=99;g.members[0].value=8;
  check(p.items[0].count==3&&p.composition[2]==4,"published arrays own their frozen values");clean();}
 {f::reset();f::kinds[0]=0;auto p=a::collect_preaction(key(a::Kind::bank,a::Origin::bootstrap),own());
  check(p.kit_known&&p.item_count==0&&p.ability0==0&&p.composition_known&&p.composition_count==0,"empty inventory and bank composition are known, unused slot is zero");
  check(!p.travel_known&&!p.nominal_duration_known,"bootstrap never guesses travel/duration");clean();}
 {f::reset();auto j=job();auto p=a::collect_preaction(key(a::Kind::one_camp,a::Origin::bootstrap),own(),&j);
  check(!p.travel_known&&!p.nominal_duration_known&&!p.composition_known,"bootstrap ignores job hints and missing group remains unknown");clean();}
 {f::reset();auto p=a::collect_preaction(key(),own());
  check(p.kit_known&&p.context_known&&!p.composition_known&&!p.travel_known&&!p.nominal_duration_known,"no Job remains unknown independently");clean();}
 {f::reset();auto j=job();j.kind=plan4::JobKind::bank;auto p=a::collect_preaction(key(a::Kind::bank),own(),&j);
  check(p.travel_known&&p.nominal_duration_known&&p.composition_known&&p.composition_count==0,"planned bank uses frozen bank job and empty composition");clean();}
 for(int defect=0;defect<3;++defect){f::reset();auto j=job();
  if(defect==0)j.kind=plan4::JobKind::renew;
  if(defect==1)j.second_group=1;
  if(defect==2){j.nominal_travel=-1;j.nominal_completion=0;}
  auto p=a::collect_preaction(key(),own(),&j);
  check(!p.travel_known&&!p.nominal_duration_known,"unusable frozen job fields never fabricated");clean();}
 {f::reset();for(int n=0;n<32;++n)f::counts[n]=n+1;
  auto p=a::collect_preaction(key(),own());check(p.kit_known&&p.item_count==32,"full 32-item capacity accepted");clean();
  f::reset();for(int n=0;n<33;++n)f::counts[n]=n+1;
  p=a::collect_preaction(key(),own());check(!p.kit_known&&p.item_count==0&&p.champion==-1,"33rd owned item invalidates complete kit rather than truncating");clean();}
 for(int defect=0;defect<3;++defect){f::reset();cambc::map::camp_group g;
  if(defect==0)g.members.clear();if(defect==1)g.members.resize(33);if(defect==2)g.id=20;
  auto p=a::collect_preaction(key(),own(),nullptr,&g);
  check(!p.composition_known&&p.composition_count==0,"empty/overflow/foreign group composition unknown");clean();}
 {f::reset();cambc::map::camp_group g;g.members.resize(32,{2});auto p=a::collect_preaction(key(),own(),nullptr,&g);
  check(p.composition_known&&p.composition_count==32,"32 public members fit");clean();}
 for(int defect=0;defect<4;++defect){f::reset();auto o=own();
  if(defect==0)o.own=false;if(defect==1)o.fresh=false;if(defect==2)o.actor=9;if(defect==3)o.round=9;
  auto p=a::collect_preaction(key(),o);check(all_unknown(p)&&f::queries==0,"invalid/foreign/stale entry does no semantic SDK query");clean();}
 for(int boundary=1;boundary<=normal_queries;++boundary){
  for(bool owner:{false,true}){f::reset();if(owner)f::owner_at=boundary;else f::shift_at=boundary;
   auto j=job();cambc::map::camp_group g;auto p=a::collect_preaction(key(),own(),&j,&g);
   check(all_unknown(p),"owner/round change at each semantic query discards all current annotations");clean();}}
 for(int boundary=1;boundary<=normal_fuel;++boundary){f::reset();f::deny_at=boundary;auto j=job();cambc::map::camp_group g;
  auto p=a::collect_preaction(key(),own(),&j,&g);
  check(!p.context_known,"budget denial cannot certify incomplete personal context");
  check(!p.kit_known||boundary>20,"budget denial before full kit leaves it unknown");clean();}
 for(auto fuel:{54000ULL,55000ULL,55499ULL,60000ULL,65000ULL,65499ULL}){f::reset();f::entry_fuel=fuel;
  auto j=job();auto p=a::collect_preaction(key(),own(),&j);check(all_unknown(p)&&f::queries==0,"insufficient entry budget yields explicit unknown without any semantic query");clean();}
 {f::reset();f::entry_fuel=65500;auto p=a::collect_preaction(key(a::Kind::bank),own());
  check(p.kit_known&&p.composition_known&&p.context_known,"exact atomic margin accepted by bounded reader");clean();}
 for(int target=0;target<3;++target){f::reset();if(target==0)f::put(cambc::team::ally,{5,4});if(target==1)f::put(cambc::team::enemy,{5,4});if(target==2)f::put(cambc::team::ally,{2,3});
  auto p=a::collect_preaction(key(),own());check(p.context_known&&p.context==(target==2?0:1),"filter only own SDK tile from complete personal occupancy masks");
  check(f::ally_masks==1&&f::enemy_masks==1,"ally found early still reads both personal masks completely");clean();}
 for(int defect=0;defect<9;++defect){f::reset();auto o=own();
  if(defect==0)o.position_known=false;if(defect==1)o.x=-1;if(defect==2)o.y=8;
  if(defect==3)f::width=0;if(defect==4)f::height=-1;
  if(defect==5)f::raw_bytes=0;if(defect==6)f::raw_bytes=9;if(defect==7)f::raw_bytes=8200;
  if(defect==8)f::width=f::height=16;
  auto p=a::collect_preaction(key(),o);check(p.kit_known&&!p.context_known&&p.context==-1&&f::ally_masks==0&&f::enemy_masks==0,"unknown/out-of-map position or invalid/oversized raw geometry never certifies context or allocates masks");clean();}
 {f::reset();f::raw_bytes=8192;f::width=f::height=256;f::ally_words={};f::put(cambc::team::ally,{2,3});f::put(cambc::team::enemy,{255,255});
  auto p=a::collect_preaction(key(),own());check(p.context_known&&p.context==1,"full bounded 1024-word masks include last raw word");clean();}
 {f::reset();f::raw_bytes=8192;f::width=f::height=256;f::ally_words={};f::put(cambc::team::ally,{2,3});
  auto p=a::collect_preaction(key(),own());check(p.context_known&&p.context==0,"all words of large empty-of-others masks must complete");const int large_boundaries=f::fuel_calls;clean();
  for(int boundary=normal_fuel;boundary<=large_boundaries;++boundary){f::reset();f::raw_bytes=8192;f::width=f::height=256;f::ally_words={};f::put(cambc::team::ally,{2,3});f::deny_at=boundary;
   p=a::collect_preaction(key(),own());check(!p.context_known&&p.context==-1,"budget stop at each large-mask scan prefix leaves context unknown");clean();}}
 {f::reset();f::put(cambc::team::enemy,{2,3});auto p=a::collect_preaction(key(),own());
  check(p.context_known&&p.context==1,"self-tile filtering applies only to ally mask");clean();}
 for(int at:{1,9,11,12,13,14}){f::reset();f::throw_at=at;auto j=job();cambc::map::camp_group g;
  auto p=a::collect_preaction(key(),own(),&j,&g);
  check((at<=11?!p.kit_known:!p.context_known),"expected visibility loss leaves affected component unknown");
  check(p.composition_known&&p.travel_known&&p.nominal_duration_known&&(at<=11?p.context_known:p.kit_known),"expected failure preserves independently complete categories");clean();}
 {f::reset();f::throw_at=14;f::put(cambc::team::ally,{5,4});auto p=a::collect_preaction(key(),own());
  check(!p.context_known&&p.context==-1,"failed context is never relabelled solo");clean();}
 for(int at:{1,9,11,12,13,14}){f::reset();f::throw_at=at;f::unexpected=true;bool threw=false;
  try{a::collect_preaction(key(),own());}catch(const cambc::error& e){threw=e.code()==cambc::error_code::unexpected;}
  check(threw,"unexpected SDK exception propagates to existing owner-turn boundary");clean();}
 {f::reset();auto p=a::collect_preaction(key(),own());auto j=job();
  j.nominal_travel=44;j.nominal_completion=99;
  check(!p.travel_known&&!p.nominal_duration_known,"later planned data cannot backfill a bootstrap/no-job start");clean();}
 std::printf("scenarios=%d checks=%d normalQueries=%d budgetBoundaries=%d actions=0 hiddenReads=0\n",scenarios,checks,normal_queries,normal_fuel);
}
'''


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--compiler', required=True)
    cli.add_argument('--node', required=True)
    args = cli.parse_args()
    started = time.perf_counter()
    original = {path: path.read_bytes() for path in INPUTS}
    hashes = {path: hashlib.sha256(raw).hexdigest() for path, raw in original.items()}
    cache = ROOT / '.cache/models-tests'
    cache.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix='attempt-preaction-sdk-', dir=cache)).resolve()
    print(f'Scratch: {scratch}', flush=True)
    try:
        for path, raw in original.items():
            target = scratch / path.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        source = scratch / 'collector.cpp'
        source.write_text(CASES)
        (scratch / 'cambc.h').write_text(SDK)
        pinned = scratch / 'pinned.cpp'
        pinned.write_text('#include "bots/models/common/attempt-preaction-sdk.h"\n')
        prepared = time.perf_counter()
        base = [args.compiler, '-std=c++20', '-O3', '-Wall', '-Wextra', '-Werror']
        # Include resolution for quoted cambc.h starts alongside the production
        # header, then -I. The SDK double remains at scratch root only for run.
        command = base + [str(pinned), '-fsyntax-only', f'-I{scratch / "sdk/cpp"}']
        pinned_result = subprocess.run(command, capture_output=True)
        (scratch / 'pinned.compile.log').write_bytes(pinned_result.stdout + pinned_result.stderr)
        assert pinned_result.returncode == 0, pinned_result.stderr.decode(errors='replace')[:3500]
        eh = Path(args.compiler).parent.parent / 'share/wasi-sysroot/lib/wasm32-wasip1/eh'
        module = scratch / 'collector.wasm'
        command = base + [str(source), f'-I{scratch}', '-fwasm-exceptions',
                          '-mllvm', '-wasm-use-legacy-eh=false', '--no-wasm-opt',
                          f'-L{eh}', '-lunwind', '-o', str(module)]
        compiled = subprocess.run(command, capture_output=True)
        (scratch / 'collector.compile.log').write_bytes(compiled.stdout + compiled.stderr)
        assert compiled.returncode == 0, compiled.stderr.decode(errors='replace')[:3500]
        driver = scratch / 'run.mjs'
        driver.write_text("import fs from 'node:fs';import {WASI} from 'node:wasi';"
                          "const w=new WASI({version:'preview1',args:[],env:{}});"
                          "const m=await WebAssembly.compile(fs.readFileSync(new URL('./collector.wasm',import.meta.url)));"
                          "const i=await WebAssembly.instantiate(m,{wasi_snapshot_preview1:w.wasiImport,env:{}});w.start(i);")
        run = subprocess.run([args.node, str(driver)], capture_output=True)
        (scratch / 'collector.stdout.log').write_bytes(run.stdout)
        (scratch / 'collector.stderr.log').write_bytes(run.stderr)
        assert run.returncode == 0, (run.stdout + run.stderr).decode(errors='replace')[-3500:]
        tested = time.perf_counter()
        print(run.stdout.decode().strip())
        assert all(path.read_bytes() == raw for path, raw in original.items()), 'Input changed during check'
        assert hashes == {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in INPUTS}
        assert scratch.parent == cache.resolve() and scratch.name.startswith('attempt-preaction-sdk-')
        allowed = {'.h', '.cpp', '.wasm', '.mjs', '.log'}
        assert all(path.suffix in allowed and path.resolve().is_relative_to(scratch)
                   for path in scratch.rglob('*') if path.is_file())
        shutil.rmtree(scratch)
        assert not scratch.exists()
        finished = time.perf_counter()
        print('Pinned SDK syntax and actual collector SDK-double checks passed; O3/default 64 KiB WASI stack; zero engines.')
        print(f'Preparation {prepared-started:.3f}s; compile/check {tested-prepared:.3f}s; postcheck/cleanup {finished-tested:.3f}s')
        print(f'Verified {len(INPUTS)} input hashes ({sum(map(len, original.values()))} bytes); successful scratch removed: {scratch}')
    except BaseException:
        print(f'FAILED: retained scratch and logs at {scratch}', flush=True)
        raise


if __name__ == '__main__':
    main()
