// Real cached producer and actual production append fragment. No native cost.
#ifndef PLAN4_ACTOR_BIND_DIAGNOSTICS
#define PLAN4_ACTOR_BIND_DIAGNOSTICS 1
#endif
#include "../common/actor-evidence.h"
#include "../common/online-evidence.h"
#include "../common/telemetry.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
namespace cm=cambc::mock;
namespace r=plan4::runtime;

auto key(const plan4::Provenance& p){return std::tie(p.observed_round,p.source,p.status);}
auto key(const r::VisibleActor& a){return std::tie(a.id,a.draft_ordinal,a.champion,a.position,a.health,
    a.maximum_health,a.strength,a.attack_range,a.bag,a.current_priority,a.next_priority,a.completed_turn,a.alive,a.ally);}
void equal(const r::ActorFrame& a,const r::ActorFrame& b){
    assert(std::tie(a.clock.round,a.clock.own.id,a.count,a.allied_count,a.enemy_count,a.own_observed,a.roster_complete,a.truncated)==
        std::tie(b.clock.round,b.clock.own.id,b.count,b.allied_count,b.enemy_count,b.own_observed,b.roster_complete,b.truncated));
    assert(key(a.own)==key(b.own)&&key(a.own.provenance)==key(b.own.provenance));
    for(int n=0;n<a.count;++n)assert(key(a.samples[n])==key(b.samples[n])&&key(a.samples[n].provenance)==key(b.samples[n].provenance));
    for(int n=0;n<a.allied_count;++n)assert(a.allied_roster[n]==b.allied_roster[n]);
    for(int n=0;n<a.enemy_count;++n)assert(a.enemy_roster[n]==b.enemy_roster[n]);
}
long long field(const std::string& row,const char* name){
    const std::string needle=std::string(" ")+name+"=";
    const auto start=row.find(needle);assert(start!=std::string::npos);
    assert(row.find(needle,start+1)==std::string::npos);
    return std::stoll(row.substr(start+needle.size()));
}
struct Rig{
    r::ActorEvidence actor_evidence_;
    r::OnlineEvidence online_evidence_;
    bool kit_seen_=false;
    int kit_observed_round_=-1;
    unsigned kit_epoch_=0;
    std::string row(const plan4::Evaluation& evaluation,bool headroom=false){
        (void)evaluation; // The disabled append intentionally uses no inputs.
        plan4::LogRow model_row("model","actor-bind-fixture");
        // Deliberately larger prefix than the measured Foundry model row.
        const std::string prefix(headroom?1000:0,'x');
        if(headroom)model_row.field("prefix",prefix);
#if PLAN4_ACTOR_BIND_DIAGNOSTICS
#include "../common/actor-bind-diagnostics.h"
#endif
        assert(model_row.valid());
        return std::string(model_row.view());
    }
};
void setup(int fuel=200000){
    cm::reset();cm::fuel=fuel;cm::query_cost=1;
    cm::actors={{0,{4,5},true,true,cambc::team::ally},{1,{5,5},true,true,cambc::team::ally},
        {5,{30,30},true,false,cambc::team::enemy}};
    cm::actor(0).bag=12;cm::actor(0).hp=83.25f;cm::actor(0).strength=10.125f;
}
void check(const char* label,int fuel,int cut=-1,int shift=-1){
    setup(fuel);cm::stop_after=cut;cm::shift_after=shift;
    auto rig=std::make_unique<Rig>();rig->actor_evidence_.start();
    const auto frame=rig->actor_evidence_.frame();
    const auto before=rig->actor_evidence_.collection_start_fuel();
    const auto minimum=rig->actor_evidence_.minimum_fuel();
    const auto stop=rig->actor_evidence_.stop_reason();
    const auto stop_fuel=rig->actor_evidence_.stop_fuel();
    if(shift>=0)assert(stop==2&&frame.truncated&&cm::round!=frame.clock.round);
    else if(cut>=0)assert(stop==1&&frame.truncated&&before>=minimum&&stop_fuel<minimum);
    else if(fuel==70000)assert(stop==1&&frame.truncated&&before<minimum&&stop_fuel==fuel);
    else assert(stop==0&&!frame.truncated&&frame.own_observed&&frame.roster_complete&&stop_fuel==-1);
    auto evaluation=std::make_unique<plan4::Evaluation>();
    // Menu shape is supplied to test serialization only, not bind feasibility.
    evaluation->menu.count=5;evaluation->menu.single_count=1;
    const int queries=cm::queries;const auto residual=cm::fuel;const auto round=cm::round;
    const auto row=rig->row(*evaluation);const auto long_row=rig->row(*evaluation,true);
    assert(cm::queries==queries&&cm::fuel==residual&&cm::round==round);
    equal(frame,rig->actor_evidence_.frame());
#if PLAN4_ACTOR_BIND_DIAGNOSTICS
    assert(field(row,"abV")==1&&field(row,"abBefore")==before&&field(row,"abMinimum")==minimum);
    assert(field(row,"abStop")==stop&&field(row,"abStopFuel")==stop_fuel);
    assert(field(row,"abRound")==frame.clock.round&&field(row,"abActor")==frame.clock.own.id);
    assert(field(row,"abCount")==frame.count&&field(row,"abOwn")==frame.own_observed);
    assert(field(row,"abRoster")==frame.roster_complete&&field(row,"abTruncated")==frame.truncated);
    assert(field(row,"abAllies")==frame.allied_count&&field(row,"abEnemies")==frame.enemy_count);
    assert(field(row,"abSingles")==1&&field(row,"abMenuCount")==5&&field(row,"abProducerCount")==0);
    assert(field(row,"abKitSeen")==0&&field(row,"abKitRound")==-1&&field(row,"abKitEpoch")==0);
    if(!stop)assert(field(row,"abStopFuel")==-1&&!frame.truncated&&frame.own_observed&&frame.roster_complete);
    evaluation->menu.count=2;evaluation->menu.single_count=0;
    const auto empty=rig->row(*evaluation);
    assert((field(empty,"abProducerBad")&64)==64&&field(empty,"abSingles")==0);
    assert(cm::queries==queries&&cm::fuel==residual&&cm::round==round);
    rig->kit_seen_=true;rig->kit_observed_round_=10;rig->kit_epoch_=4;
    const auto kit=rig->row(*evaluation);
    assert(field(kit,"abKitSeen")==1&&field(kit,"abKitRound")==10&&field(kit,"abKitEpoch")==4);
#else
    (void)before;(void)minimum;(void)stop;(void)stop_fuel;
    assert(row.find(" abV=")==std::string::npos&&row=="p4 model v=2 build=actor-bind-fixture");
#endif
    assert(long_row.size()<2048);
    std::printf("actor-bind-hook case=%s enabled=%d firstStop=%d before=%u minimum=%u stopFuel=%lld length=%zu headroomLength=%zu queries=%d\n",
        label,PLAN4_ACTOR_BIND_DIAGNOSTICS,stop,before,minimum,(long long)stop_fuel,row.size(),long_row.size(),queries);
}
int main(){
    check("complete",200000);check("entry-floor",70000);check("collection-debit",200000,12);
    check("clock",200000,-1,1);
    std::puts("actor-bind-hook PASS: actual append, cached-field equality, no queries/mutation, signed unset, disabled absence; no native cost or binder claim");
}
