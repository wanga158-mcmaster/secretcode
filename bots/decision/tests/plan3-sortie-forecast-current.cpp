// Actual forecast factory + SDK wrappers; synthetic metadata/8x8 geometry.
// This does not exercise Builder, action timing, native balance or fuel cost.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../common/sortie-forecast.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "backpressure-config-v1.h"
#include "backpressure-host-v1.h"
namespace {
int cost_zero_item=-1;
bool bp_hidden_collision=false;
std::uint64_t bp_camp_mask(){return 0;}
using namespace plan3_runtime;
cambc::bitboard mask(cambc::vec2 p){cambc::bitboard b;b.set(p);return b;}
struct World {
    PersonalInformation personal;
    StaticPaths paths;
    World() {
        personal.width=8;personal.team_size=1;personal.own_spawn=mask({0,0});personal.spawn_positions={{0,0}};
        paths.initialize({});personal.spawn_field=paths.add(personal.own_spawn);
        personal.altars={{6,6}};personal.altar_fields={paths.add(mask({5,5}))};
        auto group=cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(0),270,66,40,{});
        auto region=mask({4,2});personal.groups.push_back({group,region,{4,2},20,paths.add(region),4290,60,false,66,270,40,{{4,2}},{{{4,2},4290,60}}});
        for(int i=0;i<16;++i){paths.begin_turn();paths.advance();}verify(paths.ready());
    }
    ForecastContext context() {
        plan3::InformationState info;info.round=100;info.roundLimit=1000;info.winningGems=1000;
        info.health=info.maximumHealth=750;info.ownPermanentScore=0;
        ForecastContext context(personal,paths,info);context.strength=135;context.recall=5;return context;
    }
};
std::unique_ptr<sortie::Candidate> candidate() {
    auto c=std::make_unique<sortie::Candidate>();auto& p=c->plan;
    p.preparedRound=100;p.groupKey=20;p.decisionOrigin={2,2};p.origin={3,2};
    p.altarIndex=0;p.altarTile={6,6};p.altarApproach={5,5};
    auto& input=c->input;input.round=100;input.horizon=128;input.memberCount=3;
    for(unsigned i=0;i<3;++i)input.members[i]={int(20+i),1430,20,true};
    input.health=input.maximumHealth=750;input.strength=135;input.gems=66;
    input.outward={3,0,true,true};input.escape={2,1,true,true};input.returnToCamp={4,0,true,true};
    input.capture={2,0,true,true};input.bank={3,0,true,true};input.recallTicks=5;input.captureTicks=5;
    input.tributeTicks=4;input.spawnHealing=.1;
    input.actorKnown=input.memberEvidenceKnown=input.stationarySupported=input.safeRecall=input.quietNoReset=true;
    const auto result=plan3_sortie::evaluate(input);verify(result.schedule.has_value());
    c->accounting=*result.schedule;p.input=input;p.accounting=c->accounting;return c;
}
struct Ready {
    unsigned calls=0,allowed;
    explicit Ready(unsigned n):allowed(n){}
    Ready(const Ready&)=delete;
    bool operator()(){return calls++<allowed;}
};
}
int main() {
    reset();World world;auto context=world.context();auto c=candidate();
    Ready count(1000);auto job=sortie::forecast(*c,context,count);verify(job.has_value());
    const unsigned boundaries=count.calls;verify(boundaries>=3);
    verify(job->id==plan3::JobId{plan3::JobKind::cycle,20,-1,54});
    verify(job->complete && job->routeKnown && job->fullClearAdmitted && job->outcomes.size()==1);
    verify(job->campGems==66 && job->campGold==270 && job->travelPlusClear==89);
    verify(job->projectedRenewalAndReturnTime==13);
    for(unsigned i=0;i<1;++i) {
        const auto& o=job->outcomes[i];verify(unsigned(o.response)==i);
        verify(o.tau==89 && o.deltaS==66 && o.healthLoss==1420 && o.minimumHealth==210);
        verify(o.conversions.size()==1 && o.conversions[0].completedAt==89 && o.conversions[0].gems==66);
        verify(o.terminal.position.tile==45 && o.terminal.health==410 && o.terminal.maximumHealth==750 && o.terminal.bag==0);
        verify(o.terminal.preparationTurns==16 && o.terminal.loops.empty() && o.terminal.loopSupportUnavailable);
    }
    verify(plan3::select(plan3::Controller::ledger,false,context.info,{*job}).job==job->id);
    verify(plan3::select(plan3::Controller::atlas,false,context.info,{*job}).job==job->id);
    for(unsigned cut=0;cut<boundaries;++cut) {
        Ready ready(cut);verify(!sortie::forecast(*c,context,ready));verify(c->accounting.tau==89);
    }
    Ready exact(boundaries);verify(sortie::forecast(*c,context,exact).has_value());
    c->plan.preparedRound=99;verify(!sortie::forecast(*c,context,[]{return true;}));c->plan.preparedRound=100;
    for(double bad:{0.0,-1.0,751.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        c->accounting.minHealth=bad;verify(!sortie::forecast(*c,context,[]{return true;}));
    }
    c->accounting.minHealth=210;c->assumedQuiet=false;verify(!sortie::forecast(*c,context,[]{return true;}));c->assumedQuiet=true;
    c->publicMemberBound=false;verify(!sortie::forecast(*c,context,[]{return true;}));c->publicMemberBound=true;
    // Copied-record boundary projection ONLY: factory honors the global job
    // deadline, while Atlas separately enforces its128-turn scoring horizon.
    // This129tau mutation is not a claimed feasible model/native trajectory.
    c->accounting.tau=129;c->accounting.conversion.completedAt=129;
    job=sortie::forecast(*c,context,[]{return true;});verify(job.has_value());
    verify(plan3::admitted(context.info,*job,false) && !plan3::admitted(context.info,*job,true));
    verify(plan3::select(plan3::Controller::ledger,false,context.info,{*job}).job==job->id);
    verify(!plan3::select(plan3::Controller::atlas,false,context.info,{*job}).job);
    context.info.roundLimit=228;verify(!sortie::forecast(*c,context,[]{return true;}));
    std::printf("{\"checks\":%u,\"release\":true,\"nativeLaunches\":0}\n",checks);
}
