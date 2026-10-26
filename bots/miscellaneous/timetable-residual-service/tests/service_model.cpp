#include "service_model.h"
#include <cmath>
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
    state.body.focus = 100;
    state.body.splash = true;
    state.member_count = 4;
    const Point positions[] = {{1,0},{2,0},{1,1},{2,1}};
    for (int i = 0; i < 4; ++i)
        state.members[static_cast<std::size_t>(i)] = {i,0,positions[i],1350,20,3,false,0,true};
    state.spells[0] = {SpellKind::reset,true,0,0,0,0,false};
    state.spells[1] = {SpellKind::shield,true,0,250,0,5,false};
    return state;
}
}

int main()
{
    using namespace mb::service;
    auto state = wolves();
    check(apply_basic(state,0),"first Varg basic is admitted by live target and range");
    float damage = 0;
    bool every = true;
    for (std::size_t i = 0; i < state.member_count; ++i)
    {
        damage += 1350-state.members[i].hp;
        every = every && state.members[i].hp == 1310 && state.members[i].awake;
    }
    check(every && damage == 160,"one basic changes all four residuals to1310 without double hitting primary");
    check(apply_cast(state,0,state.body.pos) && apply_basic(state,0),"spent basic permits reset and refreshed second basic");
    damage = 0;
    for (std::size_t i = 0; i < state.member_count; ++i) damage += 1350-state.members[i].hp;
    check(damage == 320 && !state.body.can_cast && !state.body.can_attack,"basic reset basic applies320 and spends the one cast");
    check(next_camp_volley(state).gross_damage == 80,"next-volley bound retains all four still-alive damage sources");

    state = wolves();
    check(apply_cast(state,1,state.body.pos) && state.body.shield == 250 && !state.body.can_cast,
          "shield establishes conditional next-prefix cover and consumes sole cast");
    check(apply_basic(state,0) && !apply_cast(state,0,state.body.pos),"shield then basic cannot also use reset");
    const auto shielded = next_camp_volley(state);
    check(shielded.gross_damage == 80 && shielded.shield_absorbed == 80 && shielded.hp_damage == 0,
          "shield and HP damage are separate next-volley quantities");

    state = wolves();
    state.body.pos = {9,9};
    check(!apply_basic(state,0) && state.body.can_attack && !apply_cast(state,0,state.body.pos) && state.body.can_cast,
          "refused out-of-range basic cannot fabricate a spent attack for reset");
    state = wolves();
    state.body.ranged = true;
    state.body.can_move = false;
    state.body.can_attack = false;
    state.body.moved_this_turn = true;
    check(apply_cast(state,0,state.body.pos) && apply_basic(state,0) && !state.body.can_move,
          "ranged move-spent attack may reset without restoring ordinary movement");

    state = wolves();
    state.member_count = 2;
    state.body.splash = false;
    state.body.attack_range = 3;
    state.body.ranged = true;
    state.members[0] = {0,0,{1,0},50,0,0,false,0,true};
    state.members[1] = {1,0,{3,0},70,0,0,false,0,true};
    state.spells[0] = {SpellKind::aoe,true,4,60,0,0,true};
    check(apply_cast(state,0,{1,0}) && state.members[0].hp == 0 && state.body.can_attack && !state.body.can_move,
          "lethal exclusive AoE preserves basic while excluding movement");
    check(!apply_basic(state,0) && state.body.can_attack && apply_basic(state,1) && state.members[1].hp == 30,
          "post-lethal basic rejects dead target and applies to an actual survivor");

    state = wolves();
    state.body.moved_this_turn = true;
    state.body.can_move = false;
    state.spells[0] = {SpellKind::cc,true,4,100,0,1,true};
    check(!apply_cast(state,0,{1,0},0) && state.body.can_cast,"exclusive spell cannot follow an actual ordinary move");
    state.body.moved_this_turn = false;
    check(apply_cast(state,0,{1,0},0) && next_camp_volley(state).gross_damage == 60,
          "one positive stun suppresses only its next conditional camp opportunity");

    state = wolves();
    state.member_count = 2;
    state.members[0] = {0,7,{1,0},120,0,0,false,0,true};
    state.members[1] = {1,7,{4,0},2400,60,4,false,0,true};
    check(apply_basic(state,0) && state.members[1].awake && next_camp_volley(state).gross_damage == 60,
          "a harmless hit wakes a distant harmful sibling by group identity");

    auto before = state.members[0], after = before;
    before.hp = 75;
    after.hp = 120;
    after.hp_exact = false;
    check(compare_hp_observations(before,after) == HpRelation::unknown,
          "projected maximum HP rise cannot prove changed observed residual or generation");
    after.hp_exact = true;
    check(compare_hp_observations(before,after) == HpRelation::increased,
          "an exact HP rise is represented as contradiction evidence without inferring respawn");

    state = wolves();
    const auto productive = choose_fragment(state,0);
    check(productive && productive->effective_damage > 0 && productive->primitive_count > 0,
          "productive safe service beats an empty lower-volley idle fragment");
    state.body.hp = 60;
    const auto rescue = choose_fragment(state,0);
    check(rescue && rescue->primitives[0].kind == PrimitiveKind::cast && rescue->primitives[0].slot == 1 &&
          rescue->minimum_prefix_hp_after_volley > 0 && rescue->effective_damage > 0,
          "first-command execution requires shield-before-wake rather than unsafe basic-then-shield");
    state.spells[1].ready = false;
    check(!choose_fragment(state,0),"unsafe productive fragments are refused when no shield prefix exists");

    state = wolves();
    state.member_count = max_members;
    state.body.attack_range = 4;
    for (std::size_t i = 0; i < max_members; ++i)
        state.members[i] = {static_cast<int>(i),0,{static_cast<int>(i%8),static_cast<int>(i/8)},120,0,0,false,0,true};
    const auto fragments = enumerate_fragments(state);
    bool bounded = fragments.size() <= max_fragments;
    for (const auto& fragment : fragments) bounded = bounded && fragment.primitive_count <= max_primitives;
    check(bounded && !fragments.empty(),"32-member frame retains explicit15-fragment and3-primitive bounds");
    state.member_count = max_members+1;
    check(state_status(state) == ModelStatus::incomplete && enumerate_fragments(state).empty(),
          "capacity deficit is incomplete rather than a claim of infeasible work");
    state = wolves();
    state.members[0].hp_exact = false;
    check(state_status(state) == ModelStatus::incomplete && !choose_fragment(state,0),
          "inexact residuals cannot certify a safe service fragment");
    state = wolves();
    state.body.hp = std::numeric_limits<float>::quiet_NaN();
    check(state_status(state) == ModelStatus::invalid && !choose_fragment(state,0),"invalid numeric frame cannot enter scoring");
    state = wolves();
    state.width = state.height = 0;
    state.spells[0] = {SpellKind::aoe,true,4,100,1,1,true};
    check(state_status(state) == ModelStatus::incomplete && !choose_fragment(state,0),
          "missing map bounds leaves AoE legality incomplete rather than fabricating legal centers");
    std::printf("service model: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
