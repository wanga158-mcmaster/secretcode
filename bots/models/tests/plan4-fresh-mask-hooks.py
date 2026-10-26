"""The actual macro_move body under original and reusable-grid guards/hooks."""
import argparse,importlib.util,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.dont_write_bytecode=True;sys.path.insert(0,str(ROOT/'tools/arena'))
from wasi import compile_fixture,run_wasm
sp=importlib.util.spec_from_file_location('fresh_grid_existing_movement_fixture',Path(__file__).with_name('plan4-movement-telemetry.py'));fixture=importlib.util.module_from_spec(sp);sp.loader.exec_module(fixture)
EXTRA=r'''
    sim={};sim.fuel=70000;Bot precedence;bitboard mixed;mixed.set({1,0});mixed.set({2,0});bitboard danger;danger.set({0,0});danger.set({2,0});
    mask_history.clear();destination_history.clear();
    check(precedence.macro_move(mixed,&danger)&&destination_history.back().size()==1&&destination_history.back()[0]==plan4::Point{1,0},"full danger/origin/goal precedence and ordered surviving destinations");
    sim={};sim.fuel=70000;Bot same_round;mask_history.clear();
    check(same_round.macro_move(goal,nullptr),"first same-round fresh call");sim.fuel=70000;sim.blocked=true;
    check(!same_round.macro_move(goal,nullptr),"same-round changed blocker refuses");sim.fuel=70000;sim.blocked=false;
    check(same_round.macro_move(goal,nullptr)&&mask_history==std::vector<int>({0,2,0}),"same-round removal/insertion always refills complete masks");
    sim={};sim.fuel=70000;Bot nested;hook_bot=&nested;hook_nested=true;mask_history.clear();
    check(nested.macro_move(goal,nullptr)&&mask_history==std::vector<int>({2,0})&&sim.actions==1,"nested runtime hook uses independent dense fallback and preserves outer view");
    for(int fail:{1,2}) {
        sim={};sim.fuel=70000;Bot recovery;sim.blocked=true;recovery.macro_move(goal,nullptr);
        sim={};sim.fuel=70000;sim.throw_positions_at=fail;bool caught=false;
        try { recovery.macro_move(goal,nullptr); }catch(const std::runtime_error&){caught=true;}
        check(caught&&sim.actions==0,"unchanged dynamic/goals export exception yields no action");
        sim={};sim.fuel=70000;check(recovery.macro_move(goal,nullptr),"next fresh invocation recovers after export exception");
    }
    sim={};sim.fuel=70000;sim.throw_blocked_at=2;Bot final_error;bool caught=false;
    try { final_error.macro_move(goal,nullptr); }catch(const std::runtime_error&){caught=true;}
    check(caught&&sim.actions==0,"terminal fresh blocked exception precedes action");sim={};sim.fuel=70000;
    check(final_error.macro_move(goal,nullptr),"terminal error releases prepared lease");
    for(int pause:{1,2}) {
        sim={};sim.fuel=70000;sim.jump_positions_at=pause;Bot clock;
        check(!clock.macro_move(goal,nullptr)&&sim.actions==0&&sim.steps==0,"existing preparation round fence rejects semantic suspension across SDK export/goals");
        sim.jump_positions_at=0;sim.fuel=70000;check(clock.macro_move(goal,nullptr),"later current invocation refills after clock crossing");
    }
    sim={};sim.fuel=70000;sim.wait=true;Bot waiting;
    check(!waiting.macro_move(goal,nullptr)&&waiting.movement_pending_&&sim.steps==0,"model wait retains full hooks and releases lease on early return");
    sim.wait=false;sim.fuel=70000;check(waiting.macro_move(goal,nullptr),"later invocation after model wait is fresh");
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--node');ap.parse_args()
 movement=ROOT/'bots/models/common/movement.h';runtime=ROOT/'bots/models/common/model-runtime.h';phase=ROOT/'bots/models/common/phase-diagnostics.h';helper=ROOT/'bots/models/common/fresh-dynamic-grid.h'
 methods=(ROOT/'bots/models/common/macro-methods.h').read_text();body=(ROOT/'bots/models/common/movement-methods.h').read_text()
 pre=fixture.PRELUDE.replace('MOVEMENT_HEADER',movement.as_posix()).replace('MODEL_RUNTIME_HEADER',runtime.as_posix()).replace('PHASE_HEADER',phase.as_posix())
 pre='#include "'+helper.as_posix()+'"\n'+pre
 pre=pre.replace('int steps=0,initializations=0,fresh_mask=0;', 'int steps=0,initializations=0,fresh_mask=0,position_calls=0,throw_positions_at=0,jump_positions_at=0,blocked_calls=0,throw_blocked_at=0;bool wait=false;')
 pre=pre.replace('} sim;', '} sim;\nstd::vector<int> mask_history;std::vector<std::vector<plan4::Point>> destination_history;bool hook_nested=false;void nested_hook();')
 pre=pre.replace('std::vector<vec2> positions() const {', 'std::vector<vec2> positions() const {++sim.position_calls;if(sim.position_calls==sim.throw_positions_at)throw std::runtime_error("export");if(sim.position_calls==sim.jump_positions_at)++sim.round;')
 pre=pre.replace('return false;}};','if(hook_nested)nested_hook();return sim.wait;}};')
 pre=pre.replace('std::span<const plan4::Point>,','std::span<const plan4::Point> requested,')
 pre=pre.replace('++sim.steps;for(int x=0;x<3;++x)if(blocked({x,0},context))sim.fresh_mask|=1<<x;', '++sim.steps;int mask=0;for(int x=0;x<3;++x)if(blocked({x,0},context)){sim.fresh_mask|=1<<x;mask|=1<<x;}mask_history.push_back(mask);destination_history.emplace_back(requested.begin(),requested.end());')
 pre=pre.replace('bitboard blocked() const {bitboard out=', 'bitboard blocked() const {++sim.blocked_calls;if(sim.blocked_calls==sim.throw_blocked_at)throw std::runtime_error("blocked");bitboard out=')
 pre=pre.replace('bitboard terrain_;Navigation', '#if PLAN4_FRESH_DYNAMIC_GRID\nplan4::FreshDynamicGrid<vec2> fresh_dynamic_grid_;std::uint64_t fresh_grid_map_epoch_=1;\n#endif\n    bitboard terrain_;Navigation')
 test=fixture.TEST.replace('};\nint checks=0;', '};\nBot* hook_bot=nullptr;void nested_hook(){hook_nested=false;sim.blocked=true;bitboard goal;goal.set({2,0});hook_bot->macro_move(goal,nullptr);sim.blocked=false;}\nint checks=0;')
 test=test.replace('    std::printf("movement telemetry:',EXTRA+'\n    std::printf("movement telemetry:')
 source=pre+fixture.method(methods,'void macro_phase(')+'\n'+fixture.method(methods,'void macro_flush_phases(')+'\n'+fixture.method(body,'bool macro_move(')+test
 scratch=Path(tempfile.mkdtemp(prefix='fresh-mask-hooks-',dir=ROOT/'.cache/models-tests'))
 for enabled in [0,1]:
  cpp=scratch/('arm'+str(enabled)+'.cpp');wasm=cpp.with_suffix('.wasm');cpp.write_text(source,encoding='utf-8',newline='\n')
  ok,log=compile_fixture(cpp,wasm,defines=[f'PLAN4_FRESH_DYNAMIC_GRID={enabled}'],flags=['-O3','-Wl,-z,stack-size=65536']);print(log,end='');assert ok
  ok,log=run_wasm(wasm);print('fresh-grid='+str(enabled)+' '+log,end='');assert ok
 print('Actual macro_move guard/action/production/clock/telemetry cases pass in both representation arms; synthetic work cuts only.')
if __name__=='__main__':main()
