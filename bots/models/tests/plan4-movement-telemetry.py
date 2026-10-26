"""Compile actual movement and deferred telemetry against an offline fuel double."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PRELUDE = r'''
#include "MOVEMENT_HEADER"
#include "MODEL_RUNTIME_HEADER"
#ifndef OLD_ORDER
#include "PHASE_HEADER"
#endif
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
struct LoggedRow {
    int version=2;
    std::string name;
    int round=-1,before=-1,after=-1;
};
struct Simulation {
    unsigned fuel=50000,after_step=31000,round=1;
    int actions=0,rows=0,notifications=0; unsigned action_fuel=0,used=0,allowance=0;
    int fields=0,last_status=-1,next_x=-1,next_y=-1,remaining=-2,moved=-1,prepared=-1,visits=-1,generation=-1;
    int steps=0,initializations=0,fresh_mask=0;
    bool blocked=false,can_move=true,jump=false,move_success=true,production_denies=false,jump_after_row=false;
    plan4::MovementStatus status=plan4::MovementStatus::ready;
    std::string order;
    std::vector<LoggedRow> emitted;
} sim;
namespace cambc {
using handle_champion_t=int;
struct vec2 {int x,y;};
struct Actor {
    explicit operator handle_champion_t() const {return 0;}
    bool can_move() const {return sim.can_move;}
    vec2 position() const {return {0,0};}
    bool move_absolute(vec2) {++sim.actions;sim.action_fuel=sim.fuel;sim.order+='A';return sim.move_success;}
};
Actor& self() {static Actor actor;return actor;}
unsigned round_number() {return sim.round;}
unsigned fuel_remaining() {return sim.fuel;}
namespace map {int width(){return 3;} int height(){return 1;}}
}
using cambc::vec2;
struct bitboard {
    std::array<bool,3> bits{};
    bool is_set(vec2 p) const {return p.x>=0&&p.x<3&&p.y==0&&bits[p.x];}
    void set(vec2 p,bool value=true){bits[p.x]=value;}
    bool is_empty() const {return !bits[0]&&!bits[1]&&!bits[2];}
    std::vector<vec2> positions() const {std::vector<vec2> out;for(int x=0;x<3;++x)if(bits[x])out.push_back({x,0});return out;}
    bitboard& operator&=(const bitboard& b){for(int x=0;x<3;++x)bits[x]=bits[x]&&b.bits[x];return *this;}
    bitboard operator&(const bitboard& b)const{auto out=*this;out&=b;return out;}
    bitboard& operator|=(const bitboard& b){for(int x=0;x<3;++x)bits[x]=bits[x]||b.bits[x];return *this;}
    bitboard operator~() const {auto out=*this;for(auto& b:out.bits)b=!b;return out;}
};
namespace plan4 {
struct LogRow {
    LoggedRow row;
    LogRow(const char*,const char*,int version=2){row.version=version;}
    template<class T> LogRow& field(const char* name,T value){
        const std::string_view key{name};
        if constexpr(std::is_integral_v<T>){
            if(key=="round")row.round=int(value);
            if(key=="before")row.before=int(value);
            if(key=="after")row.after=int(value);
            if(key=="status"){sim.last_status=int(value);sim.fields|=1;}
            if(key=="nextX"){sim.next_x=int(value);sim.fields|=2;}
            if(key=="nextY"){sim.next_y=int(value);sim.fields|=4;}
            if(key=="remaining"){sim.remaining=int(value);sim.fields|=8;}
            if(key=="moved"){sim.moved=int(value);sim.fields|=16;}
            if(key=="preparedFuel"){sim.prepared=int(value);sim.fields|=32;}
            if(key=="visits"){sim.visits=int(value);sim.fields|=64;}
            if(key=="generation"){sim.generation=int(value);sim.fields|=128;}
        } else if constexpr(std::is_convertible_v<T,std::string_view>){if(key=="name")row.name=value;}
        return *this;
    }
    void emit(){++sim.rows;sim.fuel-=2500;sim.order+='L';sim.emitted.push_back(row);if(sim.jump_after_row)++sim.round;}
};
}
struct Navigation {void seal(){} unsigned vertex_visits()const{return 4;}};
struct Clock {int round;struct {int id;} own;};
struct Runtime {bool wait_for_movement(Clock,plan4::Point,const plan4::Point*,int){return false;}};
struct Movement {
    void initialize(int,int,std::vector<unsigned char>){++sim.initializations;}
    void begin_turn(unsigned used,unsigned allowance){sim.used=used;sim.allowance=allowance;}
    unsigned cache_generation()const{return 5;}
    plan4::MovementStep next_step(plan4::Point,std::span<const plan4::Point>,
        bool(*blocked)(plan4::Point,const void*),const void* context,unsigned,plan4::WorkBudget) {
        ++sim.steps;for(int x=0;x<3;++x)if(blocked({x,0},context))sim.fresh_mask|=1<<x;
        sim.fuel=sim.after_step;if(sim.jump)++sim.round;
        return {sim.status,{1,0},7,2};
    }
    void notify_move_result(plan4::Point,plan4::Point,bool){++sim.notifications;}
};
#define PLAN4_REFERENCE_VERSION "offline-source-counterexample"
#define PLAN4_ONLINE_EVIDENCE 1
struct Bot {
    unsigned decision_sequence_=0,phase_seen_=0,movement_floor_=35000;
#ifndef OLD_ORDER
    plan4::DeferredPhases phase_samples_;
#endif
    bool movement_pending_=false,movement_initialized_=true,macro_active_=false;
    int physical_vertices_=2,model_vertices_=3,production_requested_farm_group_=-1;
    bitboard terrain_;Navigation navigation_;Movement movement_;Runtime runtime_model_;
    plan4::detour::Cursor alternate_route_;
    static bool movement_budget(const void*){return true;}
    bitboard blocked() const {bitboard out=terrain_;if(sim.blocked)out.set({1,0});return out;}
    void production_begin_travel_mask(const bitboard&,int,int){if(sim.production_denies)sim.fuel=29999;}
    void production_move_result(vec2,bool,unsigned){}
    void production_cancel(plan4::runtime::PhaseCensor){}
    // Detour admission is outside this telemetry fixture; ordinary moves use
    // the actual executor body and retain all of its action/fuel assertions.
    bool macro_try_detour(const plan4::MovementStep&,const std::vector<plan4::Point>&,vec2,int,const bitboard*,
        plan4::detour::Route&,plan4::runtime::ExecutionRepairFrame&){return false;}
    // actor-telemetry compiles the actual observer emitter/writer. This narrow
    // movement fixture models only observer capture, without data queries.
    void macro_observer_phase(unsigned before) {
#ifdef OLD_ORDER
        constexpr unsigned bit=1u;
        if((phase_seen_&bit)&&cambc::round_number()%32!=0)return;
        const unsigned after=cambc::fuel_remaining();
        if(after<16000||after>before)return;
        phase_seen_|=bit;
        plan4::LogRow("phase",PLAN4_REFERENCE_VERSION).emit();
#else
        const auto now=cambc::round_number();
        if(!phase_samples_.wants(0,now,phase_seen_))return;
        const unsigned after=cambc::fuel_remaining();
        if(after>before)return;
        plan4::PhaseSample sample{};
        sample.actor=0;sample.round=int(now);sample.before=before;sample.after=after;
        phase_samples_.save(0,sample);
#endif
    }
'''

TEST = r'''
};
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool move_and_flush(Bot& bot,bitboard goal){
    const bool moved=bot.macro_move(goal,nullptr);
#ifndef OLD_ORDER
    bot.macro_flush_phases();
#endif
    return moved;
}
int main(){
    bitboard goal;goal.set({2,0});
    sim={};Bot ready;
    const bool moved=ready.macro_move(goal,nullptr);
#ifdef OLD_ORDER
    check(!moved&&sim.actions==0&&sim.rows==1&&sim.fuel==28500,"retained v3 log consumes ready-step action reserve");
    std::puts("retained v3 counterexample: ready at31000; telemetry lowers to28500; action refused");
#else
    check(moved&&sim.actions==1&&sim.action_fuel==31000&&sim.rows==0&&sim.order=="A"
        &&sim.fuel==31000&&ready.phase_seen_==0,"movement capture leaves action fuel and emitted sampling untouched");
    ready.macro_flush_phases();
    check(moved&&sim.actions==1&&sim.action_fuel==31000&&sim.order=="AL","ready step acts before optional telemetry");
    check(sim.fields==255&&sim.last_status==0&&sim.next_x==1&&sim.next_y==0&&sim.remaining==2&&sim.moved==1&&sim.prepared==50000&&sim.visits==7&&sim.generation==5,"post-action row reports actual step, prepared fuel, visits and goal generation");
    check(sim.rows==1&&sim.notifications==1&&sim.used==9&&sim.allowance==256&&ready.physical_vertices_==9,"telemetry change preserves aggregate movement charge and actual result receipt");
    for(bool initialized:{false,true})for(unsigned fuel:{0u,44999u}){
        sim={};sim.fuel=fuel;Bot deferred;deferred.movement_initialized_=initialized;
        check(!move_and_flush(deferred,goal)&&deferred.movement_pending_
            &&deferred.movement_initialized_==initialized&&sim.initializations==0
            &&sim.steps==0&&sim.actions==0&&sim.rows==0,"below45k defers initialized and uninitialized movement without route, action or telemetry");
    }
    sim={};sim.fuel=45000;Bot initialized_boundary;initialized_boundary.movement_pending_=true;
    check(move_and_flush(initialized_boundary,goal)&&!initialized_boundary.movement_pending_
        &&sim.initializations==0&&sim.steps==1&&sim.actions==1&&sim.rows==1,"initialized movement is admitted at45k and clears stale pending");
    for(unsigned fuel:{45000u,50000u,64999u}){
        sim={};sim.fuel=fuel;Bot deferred_initialization;deferred_initialization.movement_initialized_=false;
        check(!move_and_flush(deferred_initialization,goal)&&deferred_initialization.movement_pending_
            &&!deferred_initialization.movement_initialized_&&sim.initializations==0
            &&sim.steps==0&&sim.actions==0&&sim.rows==0,"45k through below65k defers first initialization without route, action or telemetry");
    }
    sim={};sim.fuel=65000;Bot initialization_boundary;
    initialization_boundary.movement_initialized_=false;initialization_boundary.movement_pending_=true;
    check(move_and_flush(initialization_boundary,goal)&&initialization_boundary.movement_initialized_
        &&!initialization_boundary.movement_pending_&&sim.initializations==1
        &&sim.steps==1&&sim.actions==1&&sim.rows==1,"first movement initializes and is admitted at65k");
    bitboard current_goal=goal;current_goal.set({0,0});
    for(bool initialized:{false,true})for(unsigned fuel:{0u,44999u,64999u}){
        sim={};sim.fuel=fuel;sim.can_move=false;Bot immobile;
        immobile.movement_initialized_=initialized;immobile.movement_pending_=true;
        check(!move_and_flush(immobile,goal)&&!immobile.movement_pending_
            &&immobile.movement_initialized_==initialized&&sim.initializations==0
            &&sim.steps==0&&sim.actions==0&&sim.rows==0,"cannot-move clears stale pending before fuel deferral");
        sim={};sim.fuel=fuel;Bot at_goal;
        at_goal.movement_initialized_=initialized;at_goal.movement_pending_=true;
        check(!move_and_flush(at_goal,current_goal)&&!at_goal.movement_pending_
            &&at_goal.movement_initialized_==initialized&&sim.initializations==0
            &&sim.steps==0&&sim.actions==0&&sim.rows==0,"already-at-goal clears stale pending before fuel deferral");
    }
    sim={};Bot phases;
    for(const char* name:{"observer","navigation","menu","evaluation","movement"})phases.macro_phase(name,50000);
    check(sim.rows==0&&sim.fuel==50000&&phases.phase_seen_==0&&phases.phase_samples_.count==5,
        "known phases capture fixed snapshots without formatting or changing fuel and emitted cadence");
    for(int repeat=0;repeat<20;++repeat)
        for(const char* name:{"observer","navigation","menu","evaluation","movement"})phases.macro_phase(name,50000);
    check(phases.phase_samples_.count==5,"duplicate calls stay bounded at one snapshot per known phase and round");
    phases.macro_flush_phases();
    check(sim.rows==5&&phases.phase_seen_==31,"each known phase marks its first occurrence only after emission");
    for(unsigned n=0;n<sim.emitted.size();++n)
        check(sim.emitted[n].name==std::array<std::string_view,5>{"observer","navigation","menu","evaluation","movement"}[n]
            &&sim.emitted[n].round==1&&sim.emitted[n].before==50000&&sim.emitted[n].after==50000
            &&sim.emitted[n].version==(n==0?4:2),"deferred rows preserve capture order, clocks, fuel interval and schema");
    phases.macro_phase("movement",50000);phases.macro_flush_phases();
    check(sim.rows==5,"same-round duplicate after flush cannot emit a second row");
    sim.round=2;
    for(const char* name:{"observer","navigation","menu","evaluation","movement"})phases.macro_phase(name,50000);
    phases.macro_flush_phases();
    check(sim.rows==5,"decision zero does not cause permanent every-turn telemetry");
    sim.round=32;
    for(const char* name:{"observer","navigation","menu","evaluation","movement"})phases.macro_phase(name,50000);
    phases.macro_flush_phases();
    check(sim.rows==10,"known phase sampling retains every32-round diagnostics");
    phases.macro_phase("unknown",50000);phases.macro_flush_phases();
    check(sim.rows==10,"unknown phase names cannot emit malformed schema rows");
    sim={};Bot reordered;
    for(const char* name:{"evaluation","navigation","menu"})reordered.macro_phase(name,50000);
    reordered.macro_flush_phases();
    check(sim.rows==3&&sim.emitted[0].name=="evaluation"&&sim.emitted[1].name=="navigation"
        &&sim.emitted[2].name=="menu","flush retains execution order when phase kinds arrive out of numeric order");
    sim={};Bot copied;
    plan4::MovementStep ephemeral{plan4::MovementStatus::in_progress,{-2,-3},11,-4};
    copied.macro_phase("movement",53000,&ephemeral,true,44000,7);
    ephemeral={plan4::MovementStatus::ready,{99,98},97,96};
    copied.macro_phase("movement",54000,&ephemeral,false,43000,8);
    copied.macro_flush_phases();
    check(sim.rows==1&&sim.last_status==int(plan4::MovementStatus::in_progress)&&sim.next_x==-2
        &&sim.next_y==-3&&sim.remaining==-4&&sim.moved==1&&sim.prepared==44000&&sim.visits==11
        &&sim.generation==7&&sim.emitted[0].before==53000,
        "movement snapshot owns caller values and first capture survives caller mutation and duplicate capture");
    sim={};Bot fuel_skip;fuel_skip.macro_phase("navigation",50000);sim.fuel=15999;fuel_skip.macro_flush_phases();
    check(sim.rows==0&&fuel_skip.phase_seen_==0,"below16k flush omits telemetry without consuming first-emission eligibility");
    sim.fuel=50000;fuel_skip.macro_phase("navigation",50000);fuel_skip.macro_flush_phases();
    check(sim.rows==0,"omitted flush still enforces one capture of each phase per round");
    ++sim.round;fuel_skip.macro_phase("navigation",50000);fuel_skip.macro_flush_phases();
    check(sim.rows==1&&fuel_skip.phase_seen_==2,"first omitted phase retries its first emission on a later unsampled round");
    sim={};Bot fuel_boundary;fuel_boundary.macro_phase("menu",50000);sim.fuel=16000;fuel_boundary.macro_flush_phases();
    check(sim.rows==1&&fuel_boundary.phase_seen_==4,"16k flush boundary admits one optional row");
    sim={};Bot partial;
    for(const char* name:{"navigation","menu","evaluation"})partial.macro_phase(name,50000);
    sim.fuel=17000;partial.macro_flush_phases();
    check(sim.rows==1&&sim.emitted[0].name=="navigation"&&partial.phase_seen_==2,
        "flush rechecks fuel for each row and marks only the row that emitted");
    sim.fuel=50000;partial.macro_flush_phases();
    check(sim.rows==1,"omitted tail cannot leak through a later same-round flush");
    sim={};Bot stale_capture;stale_capture.macro_phase("menu",50000);++sim.round;stale_capture.macro_flush_phases();
    check(sim.rows==0&&stale_capture.phase_seen_==0,"round advance before flush discards stale optional snapshots");
    stale_capture.macro_phase("menu",50000);stale_capture.macro_flush_phases();
    check(sim.rows==1&&sim.emitted[0].round==2,"fresh capture after stale discard uses the current round");
    sim={};Bot advancing;
    for(const char* name:{"navigation","evaluation"})advancing.macro_phase(name,50000);
    sim.jump_after_row=true;advancing.macro_flush_phases();
    check(sim.rows==1&&advancing.phase_seen_==2,"round advance during flush prevents stale tail rows");
    sim={};Bot reversed_fuel;reversed_fuel.macro_phase("menu",49999);reversed_fuel.macro_flush_phases();
    check(sim.rows==0&&reversed_fuel.phase_seen_==0,"fuel increase invalidates captured interval without consuming sampling");
    sim={};sim.after_step=29999;Bot low;
    check(!move_and_flush(low,goal)&&sim.actions==0,"post-step30k action reserve is preserved");
    sim={};sim.after_step=15999;Bot exhausted;
    check(!move_and_flush(exhausted,goal)&&sim.actions==0&&sim.rows==0,"low-fuel telemetry is voluntarily skipped");
    sim={};sim.blocked=true;Bot blocked;
    check(!move_and_flush(blocked,goal)&&sim.actions==0&&sim.fresh_mask==2,"fresh destination blocker remains packed and refuses action");
    sim={};sim.status=plan4::MovementStatus::in_progress;Bot terrain;terrain.terrain_.set({1,0});
    check(!move_and_flush(terrain,goal)&&sim.steps==1&&sim.fresh_mask==0,"actual source packing omits immutable terrain from fresh callback mask");
    sim={};Bot filtered;filtered.terrain_.set({2,0});
    check(!move_and_flush(filtered,goal)&&sim.steps==0,"public full walls still reject immutable blocked goal before search");
    bool equivalent=true;
    for(unsigned terrain_bits=0;terrain_bits<8;++terrain_bits)for(unsigned dynamic_bits=0;dynamic_bits<8;++dynamic_bits)
        for(unsigned p=0;p<3;++p)if(!(terrain_bits&(1u<<p)))
            equivalent&=bool((terrain_bits|dynamic_bits)&(1u<<p))==bool(((terrain_bits|dynamic_bits)&~terrain_bits)&(1u<<p));
    check(equivalent,"all3-tile terrain/dynamic masks agree at every walkable callback point");
    sim={};sim.status=plan4::MovementStatus::in_progress;Bot pending;
    check(!move_and_flush(pending,goal)&&pending.movement_pending_&&sim.actions==0,"unfinished search remains pending without action");
    sim={};sim.jump=true;Bot stale;
    check(!move_and_flush(stale,goal)&&stale.movement_pending_&&sim.actions==0&&sim.rows==0,"owner-round change refuses action and stale phase row");
    sim={};sim.production_denies=true;Bot receipt;receipt.production_requested_farm_group_=4;
    check(!move_and_flush(receipt,goal)&&sim.actions==0,"production preparation retains its separate fresh fuel guard");
    sim={};sim.move_success=false;Bot collision;
    check(!move_and_flush(collision,goal)&&sim.actions==1&&sim.notifications==1&&sim.order=="AL","failed actual move is receipted before optional log");
    std::printf("movement telemetry: %d source-bound checks passed; synthetic fuel only\n",checks);
#endif
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--node", required=True)
    args = parser.parse_args()
    cache = (ROOT / ".cache/models-tests").resolve()
    cache.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="plan4-movement-telemetry-", dir=cache)).resolve()
    if not scratch.is_relative_to(cache):
        raise RuntimeError("scratch escaped repository cache")
    print(f"Scratch: {scratch}", flush=True)
    try:
        current = ROOT / "bots/models/common/macro-methods.h"
        # The OLD_ORDER branches below served a retired v3 counterexample contrast; only "current" runs.
        targets = [("current", current, ROOT / "bots/models/common/movement-methods.h")]
        movement_header = ROOT / "bots/models/common/movement.h"
        runtime_header = ROOT / "bots/models/common/model-runtime.h"
        phase_header = ROOT / "bots/models/common/phase-diagnostics.h"
        inputs = {path: path.read_bytes() for path in {Path(__file__).resolve(), movement_header, runtime_header, phase_header,
            *(path for _, phase_path, move_path in targets for path in (phase_path, move_path))}}
        expected_files = set()
        for label, phase_path, move_path in targets:
            cpp = scratch / f"{label}.cpp"
            wasm = scratch / f"{label}.wasm"
            compile_log = scratch / f"{label}.compile.log"
            stdout_log = scratch / f"{label}.stdout.log"
            stderr_log = scratch / f"{label}.stderr.log"
            expected_files.update((cpp, wasm, compile_log, stdout_log, stderr_log))
            cpp.write_text(PRELUDE.replace("MOVEMENT_HEADER", movement_header.as_posix())
                .replace("MODEL_RUNTIME_HEADER", runtime_header.as_posix())
                .replace("PHASE_HEADER", phase_header.as_posix())
                + method(inputs[phase_path].decode(), "void macro_phase(") + "\n"
                + (method(inputs[phase_path].decode(), "void macro_flush_phases(") + "\n" if label == "current" else "")
                + method(inputs[move_path].decode(), "bool macro_move(") + TEST)
            eh = Path(args.compiler).resolve().parents[1] / "share/wasi-sysroot/lib/wasm32-wasip1/eh"
            command = [args.compiler, "-std=c++20", "-O3", "-Wall", "-Wextra", "-Werror",
                "-fwasm-exceptions", "-mllvm", "-wasm-use-legacy-eh=false", "--no-wasm-opt", f"-L{eh}", "-lunwind"]
            compiled = subprocess.run(command + [str(cpp), "-o", str(wasm)], capture_output=True, cwd=ROOT)
            compile_log.write_bytes(compiled.stdout + compiled.stderr)
            if compiled.returncode:
                print((compiled.stdout + compiled.stderr).decode(errors="replace"), file=sys.stderr, end="")
            compiled.check_returncode()
            host = 'import fs from "node:fs";import {WASI} from "node:wasi";const w=new WASI({version:"preview1",args:[],env:{},preopens:{},returnOnExit:true});const m=await WebAssembly.compile(fs.readFileSync(process.argv[1]));const i=await WebAssembly.instantiate(m,w.getImportObject());process.exitCode=w.start(i);'
            run = subprocess.run([args.node, "--no-warnings", "--input-type=module", "-e", host, str(wasm)], capture_output=True, cwd=ROOT)
            stdout_log.write_bytes(run.stdout)
            stderr_log.write_bytes(run.stderr)
            print(run.stdout.decode(errors="replace"), end="")
            print(run.stderr.decode(errors="replace"), file=sys.stderr, end="")
            run.check_returncode()
        assert all(path.read_bytes()==raw for path, raw in inputs.items()), "Fixture input changed during check"
        assert scratch.parent==cache and scratch.name.startswith("plan4-movement-telemetry-")
        assert set(scratch.iterdir())==expected_files and all(path.is_file() and path.parent==scratch for path in expected_files)
        shutil.rmtree(scratch)
        if scratch.exists():
            raise RuntimeError("scratch cleanup failed")
        print("Verified source-counterexample scratch cleanup", flush=True)
    except BaseException:
        print(f"FAILED: retained scratch and logs at {scratch}", flush=True)
        raise


if __name__ == "__main__":
    main()
