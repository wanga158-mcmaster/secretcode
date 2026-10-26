#include "../common/actor-evidence.h"
#include <cassert>
#include <iostream>
#include <memory>
namespace cm = cambc::mock;
using namespace plan4;
using namespace plan4::runtime;
int main() {
    int cases = 0;
    auto setup = [] {
        cm::reset(); cm::query_cost = 25;
        cm::actors.push_back({7,{3,4},true,true,cambc::team::ally,23,80,120,17,3,9,4,true});
        cm::actors.push_back({9,{4,4},true,false,cambc::team::enemy,777});
    };
    setup(); auto out = std::make_unique<ActorEvidence>(); out->start();
    const auto initial = out->frame();
    assert(cm::queries == 15 && cm::raw_queries == 3);
    assert(out->collection_start_fuel() == 100000 && out->minimum_fuel() == 95200);
    assert(out->stop_reason() == 0 && out->stop_fuel() == -1);
    assert(initial.roster_complete && initial.own_observed && !initial.truncated && initial.count == 1);
    assert(!initial.draft_order_known && initial.own.draft_ordinal==-1 && initial.samples[0].draft_ordinal==-1);
    assert(initial.allied_count == 2 && initial.enemy_count == 1 && initial.allied_roster[1] == 7);
    assert(initial.samples[0].id == 7 && initial.samples[0].bag == 23 && initial.samples[0].position == (Point{3,4}));
    assert(initial.samples[0].strength == 17 && initial.samples[0].health == 80 && initial.samples[0].attack_range == 3);
    assert(initial.samples[0].completed_turn && initial.samples[0].current_priority == 9 && initial.samples[0].next_priority == 4);
    assert(initial.samples[0].provenance.status == Status::exact_now && initial.samples[0].provenance.source == Source::personal_vision);
    assert(!cm::unauthorized); ++cases;
    // Diagnostics are cached collector state, even if the referee has moved
    // on. Reading them cannot query, alter, or refresh the published frame.
    const int observed_queries = cm::queries, observed_raw = cm::raw_queries;
    cm::fuel = 45678; ++cm::round;
    assert(out->collection_start_fuel() == 100000 && out->minimum_fuel() == 95200);
    assert(out->stop_reason() == 0 && out->stop_fuel() == -1);
    assert(cm::queries == observed_queries && cm::raw_queries == observed_raw && cm::fuel == 45678);
    assert(out->frame().clock.round == initial.clock.round && out->frame().own_observed);
    assert(out->frame().count == initial.count && out->frame().samples[0].bag == initial.samples[0].bag);
    ++cases;
    cm::round++; cm::actors[1].shown = false; cm::actors[2].shown = true; cm::fuel = 100000;
    out->start(); assert(out->frame().count == 1 && out->frame().samples[0].id == 9 && out->frame().samples[0].bag == 777);
    assert(!out->frame().samples[0].ally && !cm::unauthorized); ++cases;
    cm::round++; cm::fuel=100000; cm::actors[2].shown=false;
    out->start(); assert(out->frame().count == 0 && !out->frame().truncated && !cm::unauthorized); ++cases;
    setup(); cm::actors[1].alive=false; out=std::make_unique<ActorEvidence>();out->start();
    assert(out->frame().count==0 && out->frame().roster_complete);++cases;
    setup(); cm::actors[0].alive=false;out=std::make_unique<ActorEvidence>();out->start();
    assert(!out->frame().own_observed && out->frame().count==0 && out->frame().roster_complete);
    assert(!out->frame().truncated && out->stop_reason()==0 && out->stop_fuel()==-1 && cm::queries==3);++cases;
    // A dead-own early return has no later budget boundary. The diagnostic
    // reports observed stops only; it must not infer one or change the frame.
    setup();cm::actors[0].alive=false;cm::shift_after=3;
    out=std::make_unique<ActorEvidence>();out->start();
    assert(cm::round==11 && cm::queries==3 && out->frame().clock.round==10);
    assert(out->frame().roster_complete && !out->frame().own_observed && !out->frame().truncated);
    assert(out->stop_reason()==0 && out->stop_fuel()==-1);++cases;
    setup(); cm::actors[1].id=0;out=std::make_unique<ActorEvidence>();out->start();
    assert(!out->frame().roster_complete && out->frame().truncated);
    assert(out->stop_reason()==4 && out->stop_fuel()==-1 && cm::queries==2);++cases;
    setup(); cm::actors[2].id=7;out=std::make_unique<ActorEvidence>();out->start();
    assert(!out->frame().roster_complete && out->frame().truncated);
    assert(out->stop_reason()==4 && out->stop_fuel()==-1);++cases;
    cm::reset();cm::query_cost=25;out=std::make_unique<ActorEvidence>();out->start();
    assert(out->frame().truncated && !out->frame().roster_complete && cm::queries==2);
    assert(out->stop_reason()==3 && out->stop_fuel()==-1);++cases;
    // Unsupported rosters remain configured; each new collection still
    // resets the diagnostic and computes its own cached fuel threshold.
    cm::fuel=90000;const int unsupported_queries=cm::queries;out->start();
    assert(cm::queries==unsupported_queries && out->collection_start_fuel()==90000 && out->minimum_fuel()==85200);
    assert(out->stop_reason()==3 && out->stop_fuel()==-1);++cases;
    cm::reset();cm::query_cost=25;
    for(int n=1;n<5;++n)cm::actors.push_back({n,{n,2},true,true,cambc::team::ally});
    for(int n=5;n<10;++n)cm::actors.push_back({n,{n,3},true,true,cambc::team::enemy});
    out=std::make_unique<ActorEvidence>();out->start();assert(out->frame().count==9 && !out->frame().truncated);++cases;
    cm::actors.push_back({11,{11,3},true,true,cambc::team::enemy});cm::fuel=100000;
    out=std::make_unique<ActorEvidence>();out->start();assert(!out->frame().roster_complete && out->frame().truncated);++cases;
    assert(out->stop_reason()==3 && out->stop_fuel()==-1);
    // Every live SDK read boundary can lose its budget or round. No partially
    // filled actor row may escape, and a resumed stale frame is unavailable.
    setup();out=std::make_unique<ActorEvidence>();out->start();const int calls=cm::queries;
    for(int cut=1;cut<=calls;++cut) {
        setup();cm::stop_after=cut;out=std::make_unique<ActorEvidence>();out->start();
        assert(out->frame().truncated && !cm::unauthorized);
        assert(out->stop_reason()==1 && out->stop_fuel()==60000 && cm::queries==cut);
        for(int n=0;n<out->frame().count;++n)assert(out->frame().samples[n].id==7 && out->frame().samples[n].bag==23);
        ++cases;
        setup();cm::shift_after=cut;out=std::make_unique<ActorEvidence>();out->start();
        assert(out->frame().truncated && out->frame().count==0 && !out->frame().own_observed && !cm::unauthorized);
        assert(out->stop_reason()==2 && out->stop_fuel()==100000-cut*25 && cm::queries==cut);++cases;
    }
    // A visible peer query changes both fuel and round. Fuel wins the existing
    // short circuit; the final stale-frame cleanup must retain that first stop.
    setup();cm::stop_after=cm::shift_after=9;out=std::make_unique<ActorEvidence>();out->start();
    assert(out->frame().truncated && out->frame().count==0 && !out->frame().own_observed && !out->frame().roster_complete);
    assert(out->stop_reason()==1 && out->stop_fuel()==60000 && cm::queries==9);++cases;
    // Reuse the same configured collector after a failed collection. Warm
    // reads omit the two roster queries and reset cause/fuel without a query.
    setup();out->start();
    assert(out->frame().roster_complete && out->frame().own_observed && out->frame().count==1 && !out->frame().truncated);
    assert(out->stop_reason()==0 && out->stop_fuel()==-1 && cm::queries==13);
    assert(out->collection_start_fuel()==100000 && out->minimum_fuel()==95200);++cases;
    setup();cm::fuel=70000;out=std::make_unique<ActorEvidence>();out->start();
    assert(out->frame().truncated && !cm::queries && out->frame().count==0);
    assert(out->collection_start_fuel()==70000 && out->minimum_fuel()==71200 && out->stop_reason()==1 && out->stop_fuel()==70000);++cases;
    setup();cm::fuel=6000;out=std::make_unique<ActorEvidence>();out->start();
    assert(out->collection_start_fuel()==6000 && out->minimum_fuel()==71200 && out->stop_reason()==1 && out->stop_fuel()==6000);
    assert(!cm::queries && out->frame().truncated && !out->frame().own_observed);++cases;
    // A returned roster is immutable setup evidence, even when its following
    // budget check fails. Resume only the missing side on the next start.
    for(int interruption=0;interruption<3;++interruption)for(int cut=1;cut<=2;++cut) {
        setup();
        if(interruption!=1)cm::stop_after=cut;
        if(interruption!=0)cm::shift_after=cut;
        out=std::make_unique<ActorEvidence>();out->start();
        assert(cm::queries==cut && cm::roster_calls[0]==1 && cm::roster_calls[1]==cut-1);
        assert(cm::roster_successes==cm::roster_calls && !cm::raw_queries);
        assert(out->frame().truncated && !out->frame().own_observed && !out->frame().count);
        assert(out->stop_reason()==(interruption==1?2:1));
        assert(out->stop_fuel()==(interruption==1?100000-cut*25:60000));
        cm::stop_after=cm::shift_after=-1;cm::fuel=100000;++cm::round;
        cm::actors[0].position={6,6};cm::actors[0].bag=84;cm::actors[0].hp=73;
        cm::actors[1].shown=false;cm::actors[2].shown=true;
        cm::actors[2].position={8,3};cm::actors[2].bag=991;cm::actors[2].hp=63;
        out->start();const auto& resumed=out->frame();
        assert(cm::queries==15 && cm::roster_calls[0]==1 && cm::roster_calls[1]==1);
        assert(cm::roster_successes[0]==1 && cm::roster_successes[1]==1);
        assert(resumed.roster_complete && resumed.own_observed && !resumed.truncated && resumed.count==1);
        assert(resumed.own.position==(Point{6,6}) && resumed.own.bag==84 && resumed.own.health==73);
        assert(resumed.samples[0].id==9 && resumed.samples[0].bag==991 && resumed.samples[0].health==63);
        assert(resumed.samples[0].position==(Point{8,3}) && !resumed.samples[0].ally);
        assert(resumed.own.provenance.observed_round==cm::round && resumed.samples[0].provenance.observed_round==cm::round);
        assert(out->stop_reason()==0 && out->stop_fuel()==-1 && !cm::unauthorized);++cases;
        // Cached identities never turn into cached body observations. A later
        // visibility swap must discard the previously visible enemy body.
        ++cm::round;cm::fuel=100000;cm::actors[0].bag=86;
        cm::actors[1].shown=true;cm::actors[1].position={9,6};cm::actors[1].bag=93;
        cm::actors[2].shown=false;const int previous_queries=cm::queries;
        out->start();
        assert(cm::queries==previous_queries+13 && cm::roster_calls[0]==1 && cm::roster_calls[1]==1);
        assert(out->frame().count==1 && out->frame().samples[0].id==7 && out->frame().samples[0].bag==93);
        assert(out->frame().samples[0].position==(Point{9,6}) && out->frame().own.bag==86);
        assert(out->frame().samples[0].provenance.observed_round==cm::round && !cm::unauthorized);++cases;
    }
    // A thrown query has no returned roster to commit. A successful other
    // side remains cached, while only the failed side is retried.
    for(int side=0;side<2;++side) {
        setup();cm::roster_throw_side=side;out=std::make_unique<ActorEvidence>();bool threw=false;
        try { out->start(); } catch(const std::runtime_error&) { threw=true; }
        assert(threw && cm::queries==side+1 && cm::roster_calls[side]==1 && !cm::roster_successes[side]);
        assert(cm::roster_successes[1-side]==side && !out->frame().own_observed && !cm::raw_queries);
        cm::roster_throw_side=-1;cm::fuel=100000;++cm::round;
        cm::actors[0].bag=78;cm::actors[1].hp=66;out->start();
        assert(cm::queries==16 && cm::roster_calls[side]==2 && cm::roster_calls[1-side]==1);
        assert(cm::roster_successes[0]==1 && cm::roster_successes[1]==1);
        assert(out->frame().roster_complete && out->frame().own_observed && !out->frame().truncated);
        assert(out->frame().count==1 && out->frame().own.bag==78 && out->frame().samples[0].health==66);
        assert(out->stop_reason()==0 && out->stop_fuel()==-1 && !cm::unauthorized);++cases;
        cm::fuel=100000;++cm::round;out->start();
        assert(cm::queries==29 && cm::roster_calls[side]==2 && cm::roster_calls[1-side]==1);
        assert(cm::roster_successes[0]==1 && cm::roster_successes[1]==1 && !cm::unauthorized);++cases;
    }
    // Empty/oversized sides and invalid identities are terminal once both
    // vectors returned. Even a post-roster cut must not make setup repeat.
    for(int invalid=0;invalid<8;++invalid) {
        setup();const auto supported_actors=cm::actors;
        if(invalid<2) {
            const auto side=invalid==0?cambc::team::ally:cambc::team::enemy;
            cm::actors.erase(std::remove_if(cm::actors.begin(),cm::actors.end(),
                [side](const auto& actor){return actor.side==side;}),cm::actors.end());
        } else if(invalid<4) {
            const auto side=invalid==2?cambc::team::ally:cambc::team::enemy;
            const int extra=side==cambc::team::ally?4:5;
            for(int n=0;n<extra;++n)cm::actors.push_back({20+n,{n,8},true,true,side});
        } else if(invalid==4)cm::actors[0].id=5;
        else if(invalid==5)cm::actors[1].id=0;
        else if(invalid==6)cm::actors.push_back({9,{5,5},true,true,cambc::team::enemy});
        else cm::actors[2].id=7;
        cm::stop_after=2;out=std::make_unique<ActorEvidence>();out->start();
        assert(cm::queries==2 && cm::roster_successes[0]==1 && cm::roster_successes[1]==1);
        assert(out->frame().truncated && out->stop_reason()==1 && out->stop_fuel()==60000);
        cm::stop_after=-1;cm::fuel=100000;++cm::round;out->start();
        assert(cm::queries==2 && out->stop_reason()==(invalid<4?3:4) && out->stop_fuel()==-1);
        assert(out->frame().truncated && !out->frame().roster_complete && !out->frame().own_observed && !out->frame().count);
        cm::actors=supported_actors;cm::fuel=100000;++cm::round;out->start();
        assert(cm::queries==2 && cm::roster_calls[0]==1 && cm::roster_calls[1]==1 && !cm::raw_queries);
        assert(out->stop_reason()==(invalid<4?3:4) && out->stop_fuel()==-1 && !cm::unauthorized);++cases;
    }
    std::cout << "plan4 actor evidence " << cases << " cases passed\n";
}
