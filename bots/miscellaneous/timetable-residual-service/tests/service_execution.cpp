#include "service_execution.h"
#include <cstdio>
#include <limits>

namespace {
using namespace mb::service;
int failures{};
void check(bool condition, const char* label)
{
    if (!condition) { std::printf("FAIL %s\n",label); ++failures; }
}
State wolves()
{
    State state;
    state.width = state.height = 10;
    state.body.pos = {0,0};
    state.body.hp = state.body.max_hp = 1000;
    state.body.strength = 40;
    state.body.splash = true;
    state.member_count = 4;
    const Point positions[] = {{1,0},{2,0},{1,1},{2,1}};
    for (int i = 0; i < 4; ++i)
        state.members[static_cast<std::size_t>(i)] = {10+i*3,7,positions[i],1350,20,3,false,0,true};
    state.spells[0] = {SpellKind::reset,true,0,0,0,0,false};
    state.spells[1] = {SpellKind::shield,true,0,250,0,5,false};
    return state;
}
bool prefix_safe(State state, const ExecutionChoice& choice, float reserve)
{
    if (choice.status != ExecutionStatus::Ready) return false;
    const auto& p = choice.primitive;
    const bool applied = p.kind == PrimitiveKind::basic ? apply_basic(state,p.member_id) :
        apply_cast(state,p.slot,p.aim,p.member_id);
    return applied && next_camp_volley(state).complete && state.body.hp-next_camp_volley(state).hp_damage > reserve;
}
}

int main()
{
    auto state = wolves();
    auto choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::basic &&
          choice.effective_damage == 160,"healthy Varg chooses productive160 basic while preserving sole cast");
    check(choice.primitive.member_id == 10 && state.members[0].hp == 1350 && state.body.can_attack,
          "sparse stable native member IDs survive ranking and pure input remains unchanged");
    check(prefix_safe(state,choice,20) && choice.next_volley.gross_damage == 80,
          "returned first command carries its immediate conditional volley");
    check(choice.work.state_copies <= max_first_command_options && choice.work.basic_candidates <= 4 &&
          choice.work.cast_candidates <= 2,"representative first-command options obey structural work bounds");

    check(apply_basic(state,choice.primitive.member_id),"fixture spends actual basic before renewed selection");
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::cast &&
          choice.primitive.slot == 0 && choice.effective_damage == 160,
          "spent attack admits reset only with a productive subsequent basic");
    check(prefix_safe(state,choice,20) && apply_cast(state,0,state.body.pos),"returned reset prefix is safe and legal");
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::basic && !state.body.can_cast,
          "fresh post-reset selection returns basic without inventing a second cast");

    state = wolves();
    state.body.hp = 90;
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::cast &&
          choice.primitive.slot == 1,"unsafe basic-before-shield is rejected in favor of shield-first");
    check(prefix_safe(state,choice,20) && choice.next_volley.gross_damage == 0 && choice.effective_damage == 160,
          "shield first and its productive basic continuation both satisfy prefix cover");
    check(apply_cast(state,1,state.body.pos) && !apply_cast(state,0,state.body.pos),
          "selected shield consumes sole cast and cannot also reset");

    state = wolves();
    state.body.pos = {9,9};
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::NoProductive && state.body.can_attack,
          "out-of-range basic neither fabricates reset allowance nor justifies idle shield");
    state = wolves();
    state.body.can_attack = false;
    state.spells[0].ready = false;
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::NoProductive,"shield without any productive available basic is omitted");

    state = wolves();
    state.members[0].hp_exact = false;
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Incomplete && choice.work.state_copies == 0,
          "unknown residual HP withdraws authority before any full-state option copy");
    state = wolves();
    state.incoming_complete = false;
    check(choose_first_command(state,20).status == ExecutionStatus::Incomplete,
          "caller must cover external potential incoming sources");
    state = wolves();
    state.member_count = max_members+1;
    check(choose_first_command(state,20).status == ExecutionStatus::Incomplete,"capacity deficit is incomplete, not infeasible");
    state = wolves();
    check(choose_first_command(state,std::numeric_limits<float>::infinity()).status == ExecutionStatus::Incomplete,
          "nonfinite reserve is not a valid safety premise");

    state = wolves();
    state.member_count = 2;
    state.body.splash = false;
    state.body.attack_range = 3;
    state.members[0] = {71,7,{1,0},50,80,3,false,0,true};
    state.members[1] = {92,7,{3,0},70,0,0,false,0,true};
    state.spells[0] = {SpellKind::aoe,true,4,60,0,0,true};
    state.spells[1].ready = false;
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::cast &&
          choice.primitive.aim == Point{1,0},"lethal AoE targets the actual damaging source using absolute aim");
    check(apply_cast(state,choice.primitive.slot,choice.primitive.aim,choice.primitive.member_id),
          "selected AoE is applicable to the pure frame");
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.primitive.kind == PrimitiveKind::basic &&
          choice.primitive.member_id == 92,"post-lethal fresh selection attacks surviving stable ID, never dead primary");

    state.body.can_attack = false;
    state.body.can_cast = true;
    state.body.moved_this_turn = true;
    state.spells[0].ready = true;
    check(choose_first_command(state,20).status == ExecutionStatus::NoProductive,
          "actual movement excludes the exclusive cast even if can_cast remains true");

    state = wolves();
    state.member_count = 0;
    check(choose_first_command(state,20).status == ExecutionStatus::NoProductive,
          "empty damage frame returns no productive command; capture is a separate physical job");
    state = wolves();
    state.body.attack_range = 9;
    state.member_count = max_members;
    for (std::size_t i = 0; i < max_members; ++i)
        state.members[i] = {static_cast<int>(100+i*2),7,{1+static_cast<int>(i%8),1+static_cast<int>(i/8)},1350,1,9,false,0,true};
    state.spells[0] = {SpellKind::damage,true,9,45,0,0,false};
    state.spells[1] = {SpellKind::aoe,true,9,20,1,0,false};
    choice = choose_first_command(state,20);
    check(choice.status == ExecutionStatus::Ready && choice.work.basic_candidates == 4 &&
          choice.work.cast_candidates == 2 && choice.work.state_copies == 6 &&
          choice.work.transition_checks <= 8 && choice.work.volley_checks <= 8,
          "32-member complete frame evaluates six options with no Fragment vector or unbounded State copies");
    check(prefix_safe(state,choice,20),"maximum-capacity representative choice retains first-command prefix safety");
    std::printf("service execution: %d failures\n",failures);
    return failures ? 1 : 0;
}
