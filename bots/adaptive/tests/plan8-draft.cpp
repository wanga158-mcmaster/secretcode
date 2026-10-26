#include "../canary-and-cashflow/src/draft.h"
#include <cstdio>
#include <cstdlib>
using namespace p8;
using namespace p8::canary;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class T> p6::Fact<T> fixture(T value) { return {value,p6::Source::synthetic_fixture}; }
void setup(DraftCandidate& c, p6::Champion champion, int gems) {
    c = {}; c.champion = champion;
    auto& p = c.first_income;
    p.group = 7; p.collector = 1; p.altar = 9; p.crew[0] = 1; p.crew_count = 1;
    p.forecast_rounds = 10; p.remaining_rounds = 2000; p.capture_window = 5;
    p.gem_bearing = p.complete_verified = true;
    for (auto* route : {&p.ingress,&p.capture,&p.bank,&p.renewal}) {
        route->verified = true; route->count = 1; route->tiles[0] = {2,2};
    }
    c.public_geometry_verified = c.ability_schedule_legal = true;
    c.starting_health = c.starting_max_health = {100,p6::Source::queried_setting};
    for (auto& e : c.cumulative_survival) e = {100,100,20,1,0,0,true};
    auto& s = c.complete_schedule;
    s.actor_count = s.production_count = 1; s.target_id = 7;
    auto& a = s.actors[0]; a.actor_id = 1; a.initial_bag = fixture<p6::Gems>(0);
    a.initial_gold = fixture<p6::Gems>(0); a.occupied_turns = fixture<p6::Gems>(10);
    a.event_count = 3;
    a.events[0] = {p6::EventKind::production,1,2,0,fixture<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
    a.events[1] = {p6::EventKind::tribute,2,5,-1,fixture<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
    a.events[2] = {p6::EventKind::renewal,3,10,-1,fixture<p6::Gems>(0),8,p6::Source::synthetic_fixture};
    auto& prod = s.productions[0]; prod.event_id = 1; prod.tick = 2;
    prod.gems = fixture<p6::Gems>(gems); prod.gold = fixture<p6::Gems>(0);
    prod.holders[0] = 1; prod.holder_count = 1; prod.source = p6::Source::synthetic_fixture;
}
}
int main() {
    DraftInputs in; in.prospective_actor = 1; in.settings = {2000,1000,5};
    in.settings_identity = {17,p6::Source::queried_setting};
    in.windows.deadlines = in.windows.windows = {120,120,120};
    in.windows.sampled_actor_id = 1; in.windows.sampled_round = 0;
    in.windows.source = p6::Source::public_map; in.windows.valid = true;
    static std::array<DraftCandidate,8> candidates;
    for (int i = 0; i < 8; ++i) setup(candidates[i],champion_pool[i],10+i);
    auto chosen = select_draft(in,candidates);
    check(chosen.champion == p6::Champion::ren,"wider pool can choose Ren when verified complete score wins");
    candidates[0].complete_schedule.productions[0].gems.value = 17;
    chosen = select_draft(in,candidates);
    check(chosen.champion == p6::Champion::tavi,"canary field-shopping tie is deterministic after equal economic rank");
    in.ordinal = 1; in.own_picks[0] = p6::Champion::tavi;
    candidates[1].complete_schedule.productions[0].gems.value = 17;
    chosen = select_draft(in,candidates);
    check(chosen.champion == p6::Champion::marcella && chosen.role == DraftRole::home_conversion,
          "next role can choose Marcella while excluding own duplicate pick");
    candidates[1].cumulative_survival[3].incoming_total = 1000;
    check(select_draft(in,candidates).champion == p6::Champion::ren,"unsafe high-value conversion candidate rejected");
    candidates[7].public_geometry_verified = false;
    check(select_draft(in,candidates).champion != p6::Champion::ren,"unverified route cannot determine draft");
    candidates[7].public_geometry_verified = true;
    candidates[7].complete_schedule.actors[0].events[1].tick = 121;
    candidates[7].complete_schedule.actors[0].events[2].tick = 122;
    check(select_draft(in,candidates).champion != p6::Champion::ren,"unbanked projected gems do not win drafting");
    candidates[6].champion = p6::Champion::cassi;
    check(!supported_champion(p6::Champion::cassi) && !supported_champion(p6::Champion::issac),
          "linked picks remain unsupported rather than counting two bodies as ordinary picks");
    in.settings_identity.source = p6::Source::unknown;
    check(select_draft(in,candidates).candidate < 0,"missing queried settings blocks draft certification");
    DraftRoster draft;
    draft.picks = 3; draft.champions = {p6::Champion::ren,p6::Champion::marcella,p6::Champion::vendetta,
        p6::Champion::other,p6::Champion::other}; draft.actors = {1,2,3,-1,-1};
    p6::Actor actor; actor.id = 1; actor.type = p6::Champion::ren; actor.pick_ordinal = 0;
    actor.team = p6::Team::ally;
    check(roster_actor(actor,draft) && canary_actor(actor,3),"non-Tavi selected actor retains canary role");
    actor.id = 99;
    check(!roster_actor(actor,draft),"opponent with same champion and ordinal is not an own helper");
    draft.champions[2] = draft.champions[1];
    check(!draft.valid(),"own champion picks must be distinct");
    std::printf("plan8-draft: %d checks passed; eight-champion bounded pool; no optimality or native claim\n",checks);
}
