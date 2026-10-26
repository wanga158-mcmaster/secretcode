#include "../earned-center/src/draft.h"
#include <cstdio>
#include <cstdlib>

using namespace p8;
using namespace p8::earned_center;
namespace ec = p8::earned_center;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class T> p6::Fact<T> fixture(T value) { return {value,p6::Source::synthetic_fixture}; }
// Large fixed records have static storage in this test, including reset data.
void setup(DraftCandidate& c, p6::Champion champion, int gems) {
    static const DraftCandidate empty{};
    c = empty; c.champion = champion;
    auto& p = c.first_income;
    p.group = 7; p.collector = 1; p.altar = 9; p.crew[0] = 1; p.crew_count = 1;
    p.forecast_rounds = 24; p.remaining_rounds = 2000; p.capture_window = 5;
    p.gem_bearing = p.complete_verified = true;
    int x = 1;
    for (auto* route : {&p.ingress,&p.capture,&p.bank,&p.renewal}) {
        route->verified = true; route->count = 2;
        route->tiles[0] = {x,1}; route->tiles[1] = {x+1,1}; ++x;
    }
    c.public_geometry_verified = c.ability_schedule_legal = true;
    c.starting_health = c.starting_max_health = {100,p6::Source::queried_setting};
    double incoming = 10;
    for (auto& e : c.cumulative_survival) { e = {100,100,incoming,1,0,0,true}; incoming += 5; }
    auto& s = c.complete_schedule;
    s.actor_count = s.production_count = 1; s.target_id = 7;
    auto& a = s.actors[0]; a.actor_id = 1; a.initial_bag = fixture<p6::Gems>(0);
    a.initial_gold = fixture<p6::Gems>(0); a.occupied_turns = fixture<p6::Gems>(24);
    a.event_count = 3;
    a.events[0] = {p6::EventKind::production,1,12,0,fixture<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
    a.events[1] = {p6::EventKind::tribute,2,16,-1,fixture<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
    a.events[2] = {p6::EventKind::renewal,3,24,-1,fixture<p6::Gems>(0),8,p6::Source::synthetic_fixture};
    auto& prod = s.productions[0]; prod.event_id = 1; prod.tick = 12;
    prod.gems = fixture<p6::Gems>(gems); prod.gold = fixture<p6::Gems>(0);
    prod.holders[0] = 1; prod.holder_count = 1; prod.source = p6::Source::synthetic_fixture;
}
DraftInputs input() {
    DraftInputs in; in.prospective_actor = 1; in.settings = {2000,1000,5};
    in.settings_identity = {17,p6::Source::queried_setting};
    in.windows.deadlines = in.windows.windows = {120,120,120};
    in.windows.sampled_actor_id = 1; in.windows.sampled_round = 0;
    in.windows.source = p6::Source::public_map; in.windows.valid = true;
    return in;
}
}
int main() {
    auto in = input();
    static std::array<DraftCandidate,8> candidates;
    for (int i = 0; i < 8; ++i) setup(candidates[i],champion_pool[i],10+i);
    auto chosen = ec::select_draft(in,candidates);
    check(chosen.champion == p6::Champion::ren && chosen.role == DraftRole::collector,
          "wider-pool Ren wins collector role through a higher certified complete bank");
    check(chosen.rank.min_margin == 17 && chosen.rank.productive_arrival == 24,
          "draft ranks actual scheduled tribute and productive return");

    for (int i = 0; i < 8; ++i) setup(candidates[i],champion_pool[i],10);
    check(ec::select_draft(in,candidates).champion == p6::Champion::marcella,
          "collector prefers Marcella only after an identical complete-cycle economic rank");
    candidates[1].complete_schedule.productions[0].gold.value = 2000;
    candidates[7].complete_schedule.productions[0].gems.value = 11;
    check(ec::select_draft(in,candidates).champion == p6::Champion::ren,
          "collector gold without a represented score-producing continuation adds no score bonus");
    setup(candidates[1],p6::Champion::marcella,10); setup(candidates[7],p6::Champion::ren,10);
    candidates[0].complete_schedule.actors[0].occupied_turns.value = 23;
    check(ec::select_draft(in,candidates).champion == p6::Champion::tavi,
          "lower occupied work beats collector champion preference");
    candidates[0].complete_schedule.actors[0].occupied_turns.value = 24;
    in.ordinal = 1; in.own_picks[0] = p6::Champion::marcella;
    chosen = ec::select_draft(in,candidates);
    check(chosen.champion == p6::Champion::tavi && chosen.role == DraftRole::home_production,
          "second pick favors home production while excluding the own collector pick");
    in.ordinal = 2; in.own_picks[1] = p6::Champion::tavi;
    chosen = ec::select_draft(in,candidates);
    check(chosen.champion == p6::Champion::willow && chosen.role == DraftRole::safe_helper,
          "third pick can prefer a safe helper with its own certified independent income");
    in.ordinal = 3; in.picks = 5; in.own_picks[2] = p6::Champion::willow;
    chosen = ec::select_draft(in,candidates);
    check(chosen.role == DraftRole::independent && chosen.champion == p6::Champion::varg,
          "fourth actor is an independent producer rather than an assumed collector helper");
    in.own_picks[2] = p6::Champion::marcella;
    check(ec::select_draft(in,candidates).candidate < 0,"duplicate already selected own picks invalidate the draft input");

    in = input();
    static DraftCandidate candidate;
    const std::span<const DraftCandidate> one(&candidate,1);
    setup(candidate,p6::Champion::ren,20);
    check(ec::select_draft(in,one).candidate == 0,"alternative collector has a connected solo first-income cycle");
    candidate.first_income.complete_verified = false;
    check(ec::select_draft(in,one).candidate < 0,"unverified first-income cycle is rejected");
    setup(candidate,p6::Champion::ren,20); candidate.first_income.gem_bearing = false;
    check(ec::select_draft(in,one).candidate < 0,"a gold-only first source cannot certify first gem income");
    setup(candidate,p6::Champion::ren,20); candidate.first_income.bank.tiles[0] = {40,40};
    check(ec::select_draft(in,one).candidate < 0,"a disconnected bank route is rejected despite completion metadata");
    setup(candidate,p6::Champion::ren,20); candidate.first_income.renewal.verified = false;
    check(ec::select_draft(in,one).candidate < 0,"missing productive return route cannot certify first income");
    setup(candidate,p6::Champion::ren,20); candidate.first_income.organization = Organization::H;
    check(ec::select_draft(in,one).candidate < 0,"first income cannot rely on a supported collector coalition");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].initial_bag.value = 1;
    check(ec::select_draft(in,one).candidate < 0,"preexisting bag cannot masquerade as ordinary-start first income");
    setup(candidate,p6::Champion::ren,20); candidate.cumulative_survival[3].incoming_total = 100;
    check(ec::select_draft(in,one).candidate < 0,"individually unsafe return rejects an otherwise profitable pick");
    setup(candidate,p6::Champion::ren,20); candidate.cumulative_survival[2].assigned_timed_heal = 10;
    check(ec::select_draft(in,one).candidate < 0,"an assumed helper heal cannot certify solo first income");
    setup(candidate,p6::Champion::ren,20); candidate.cumulative_survival[2].incoming_total = 1;
    check(ec::select_draft(in,one).candidate < 0,"noncumulative survival damage cannot erase earlier exposure");
    setup(candidate,p6::Champion::ren,20); candidate.starting_health.source = p6::Source::unknown;
    check(ec::select_draft(in,one).candidate < 0,"starting health must come from queried settings");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].events[1].kind = p6::EventKind::recovery;
    check(ec::select_draft(in,one).candidate < 0,"no actual tribute event leaves projected gems unbanked");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].event_count = 2;
    check(ec::select_draft(in,one).candidate < 0,"banked score without scheduled productive return is ineligible");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].events[1].tick = 121;
    candidate.complete_schedule.actors[0].events[2].tick = 122;
    check(ec::select_draft(in,one).candidate < 0,"tribute outside the comparison window does not secure first-income score");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].events[2].tick = 121;
    check(ec::select_draft(in,one).candidate < 0,"productive return beyond the forecast cap is rejected");
    setup(candidate,p6::Champion::ren,20); candidate.complete_schedule.actors[0].actor_id = 2;
    check(ec::select_draft(in,one).candidate < 0,"another actor's cycle cannot certify the prospective pick");
    setup(candidate,p6::Champion::ren,20); in.ordinal = 1; in.own_picks[0] = p6::Champion::ren;
    check(ec::select_draft(in,one).candidate < 0,"already selected own champion is not available for a duplicate pick");
    in = input(); in.settings_identity.source = p6::Source::unknown;
    check(ec::select_draft(in,one).candidate < 0,"unknown settings identity blocks draft certification");

    DraftRoster roster; roster.picks = 3;
    roster.champions = {p6::Champion::ren,p6::Champion::tavi,p6::Champion::willow,
        p6::Champion::other,p6::Champion::other}; roster.actors = {31,42,53,-1,-1};
    p6::Actor actor; actor.id = 31; actor.type = p6::Champion::ren; actor.pick_ordinal = 0;
    actor.team = p6::Team::ally;
    check(collector_actor(actor,roster),"actual selected non-Marcella own actor owns collector role");
    actor.id = 99;
    check(!collector_actor(actor,roster),"opponent matching champion and ordinal is not the drafted collector");
    actor.id = 42; actor.type = p6::Champion::tavi; actor.pick_ordinal = 1;
    check(roster_actor(actor,roster) && !collector_actor(actor,roster),"home producer does not become collector by champion preference");
    actor.id = 31; actor.type = p6::Champion::ren; actor.pick_ordinal = 0; actor.team = p6::Team::enemy;
    check(!collector_actor(actor,3) && !collector_actor(actor,roster),"enemy actor cannot own the collector role");
    check(!supported_champion(p6::Champion::cassi) && !supported_champion(p6::Champion::issac) &&
          !supported_champion(p6::Champion::heart_of_ruin),"linked and unreconciled picks remain outside the supported pool");
    std::printf("plan8-earned-center-draft: %d checks passed; eight supported picks; no optimality or native claim\n",checks);
}
