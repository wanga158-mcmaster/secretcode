// Pure runtime storage regressions. No SDK imports or native games.
#include "../common/src/runtime_joint.h"
#include "../reference/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
int checks=0;
void check(bool condition,const char* message) {
    ++checks;
    if(!condition) {std::fprintf(stderr,"FAIL runtime storage: %s\n",message);std::exit(1);}
}
struct Payload {
    inline static int constructions=0;
    int value=0;
    Payload() {++constructions;}
};
template<class T> Observation<T> storage_seen(T value,ViewClock clock,int subject) {
    return {value,clock,subject,Knowledge::exact};
}
void prepared_quote_needs_current_caller_proof() {
    struct Fixture { ReferenceInput actual{}; forecast::Request request{}; };
    auto fixture=std::make_unique<Fixture>();auto& actual=fixture->actual;auto& request=fixture->request;
    auto& frame=actual.frame;frame.clock={5,11,1};
    auto& own=frame.own.actor;own.id=11;own.team=Team::ally;own.type=Champion::vendetta;
    own.alive=own.can_move=own.can_attack=true;own.health=own.max_health=1000;own.strength=70;
    own.position={0,1};own.ready_attack_group=7;frame.own.at_spawn=true;
    frame.group=storage_seen(Group{7,{0,0},{1,0},{0,1},Lifecycle::living,true,true,true},frame.clock,7);
    actual.assigned_group=7;actual.safe_now=actual.fresh_target_attack_legal=true;
    actual.remaining_rounds=95;actual.capture_window=3;
    auto quote=std::make_unique<forecast::EvaluatedQuote>();const auto* allocation=quote.get();
    ActorState owner;
    check(!quote->ready() && !quote->matches_frame(frame) && quote->result().status!=Status::feasible &&
          !reference::choose_forecast_replacing_into(owner,actual,quote.get()) && !owner.active,
          "prepared cold quote memory has no current evidence or production authority at spawn");
    const Frame preparation_frame=frame;
    frame.clock={6,11,2};own.position={0,0};frame.own.at_spawn=false;actual.at_approach=true;
    frame.group.clock=frame.clock;
    check(quote.get()==allocation && !quote->ready() && !quote->matches_frame(frame) &&
          !quote->matches_frame(preparation_frame) &&
          !reference::choose_forecast_replacing_into(owner,actual,quote.get()) && !owner.active,
          "clock and physical frame change cannot publish an allocated but unevaluated quote");
    request.group_count=1;request.timing.settings={{100,1000,2},Source::queried_setting};
    request.timing.tribute_multiplier={.1,Source::queried_setting};
    request.timing.tribute_base={0,Source::queried_setting};request.timing.tribute_minimum={1,Source::queried_setting};
    request.kit=storage_seen(Productivity{70,0,false,false,false,false},frame.clock,own.id);
    request.ambient_incoming=storage_seen(0.,frame.clock,own.id);
    request.bank_waypoint=90;request.bank_position={3,0};
    request.renewal_waypoint=91;request.renewal_position={0,0};
    request.bank_available=storage_seen(true,frame.clock,90);
    auto& source=request.groups[0];source.observed=frame.group;
    source.membership_complete={true,Source::public_map};source.member_count=1;
    source.members[0].public_full={{101,1350,20},Source::public_map};
    source.members[0].current=storage_seen(MonsterWork{101,1350,20},frame.clock,101);
    source.gems={4,Source::public_map};source.gold={40,Source::public_map};
    const auto leg=[&](Point from,Point to,int distance) {
        request.legs[request.leg_count++]={from,to,{distance,Source::public_map}};
    };
    leg({0,0},{0,0},0);leg({0,0},{1,0},1);leg({1,0},{3,0},2);leg({3,0},{0,0},3);
    quote->evaluate(frame,request);
    auto attack=reference::choose_forecast_replacing_into(owner,actual,quote.get());
    check(quote.get()==allocation && quote->ready() && quote->matches_frame(frame) &&
          !quote->matches_frame(preparation_frame) && quote->result().status==Status::feasible &&
          attack && attack->action==Action::attack && owner.active && owner.project.group==7,
          "same prepared allocation becomes authoritative only after a fresh evaluated quote reaches real reference attack");
    own.strength=76;
    check(!quote->matches_frame(frame) &&
          !reference::choose_forecast_replacing_into(owner,actual,quote.get()) &&
          owner.active && owner.project.group==7,
          "same-clock changed observed stats reject the old quote without erasing owned production");
    frame.clock={7,11,3};frame.group.clock=frame.clock;
    check(!quote->matches_frame(frame) &&
          !reference::choose_forecast_replacing_into(owner,actual,quote.get()) && owner.active,
          "later owner clock requires fresh proof despite retained allocated quote storage");
    source.observed=frame.group;source.members[0].current.clock=frame.clock;
    request.kit.clock=request.ambient_incoming.clock=request.bank_available.clock=frame.clock;
    request.kit.value.strength=own.strength;
    quote->evaluate(frame,request);attack=reference::choose_forecast_replacing_into(owner,actual,quote.get());
    check(quote.get()==allocation && quote->matches_frame(frame) &&
          quote->result().status==Status::feasible && attack && attack->action==Action::attack,
          "fresh observed-frame evaluation reuses prepared storage and restores real caller authorization");
    request.timing.settings.source=Source::unknown;quote->evaluate(frame,request);
    check(quote->ready() && quote->matches_frame(frame) && quote->result().status!=Status::feasible &&
          !reference::choose_forecast_replacing_into(owner,actual,quote.get()) &&
          owner.active && owner.project.group==7,
          "ready storage with a newly rejected evaluation cannot expose its previous feasible proof");
}
}
int main() {
    runtime_joint::LazyGrid<Payload> payloads;
    check(Payload::constructions==0,"empty grid does not construct 48 payloads");
    const auto& empty_payloads=payloads;
    const auto* sentinel=&empty_payloads[0][0];
    for(int site=0;site<8;++site)for(int actor=0;actor<max_actors;++actor)
        check(&empty_payloads[site][actor]==sentinel,"missing const slots share one immutable sentinel");
    check(Payload::constructions==1,"const reads construct only one sentinel");
    auto* allocated=&payloads[3][2];allocated->value=73;
    check(Payload::constructions==2 && empty_payloads[3][2].value==73,"only the written slot is allocated");
    payloads.clear_current();
    check(&empty_payloads[3][2]==sentinel,"clear hides the retained previous-turn payload");
    check(&payloads[3][2]==allocated && Payload::constructions==2,"later writes reuse one allocation");
    Payload borrowed;borrowed.value=91;
    payloads.borrow(1,1,borrowed);
    check(empty_payloads[1][1].value==91 && !payloads.storage_ready(1,1),"borrowed immutable view avoids a duplicate payload allocation");
    payloads.clear_current();
    check(&empty_payloads[1][1]==sentinel,"turn clear hides borrowed views too");
    payloads.prepare(2,2);
    check(payloads.storage_ready(2,2) && !payloads.current(2,2),"cold storage can be prepared without publishing current evidence");

    runtime_joint::Models models;
    runtime_joint::Requests requests;
    const auto& const_models=models;const auto& const_requests=requests;
    check(sizeof(models)<2048 && sizeof(requests)<2048,"runtime grids contain metadata, not 48 eager payloads");
    check(const_models[7][5].status==Status::invalid,"missing model is explicitly invalid");
    check(!models.current(7,5) && !requests.current(7,5),"missing slots have cheap current metadata");
    check(const_requests[7][5].group_count==0 && const_requests[7][5].kit.knowledge==Knowledge::unknown,
          "missing request has no current group or actor evidence");
    models[2][1].status=Status::feasible;
    auto& request=requests[2][1];request.kit.clock={5,11,1};request.kit.subject=11;
    request.kit.knowledge=Knowledge::exact;request.group_count=1;
    check(models.current(2,1) && requests.current(2,1),"written slots are current without payload traversal");
    models.clear_current();requests.clear_current();
    check(const_models[2][1].status==Status::invalid && const_requests[2][1].group_count==0,
          "cleared caches do not expose earlier feasible model or request");
    check(!models.current(2,1) && !requests.current(2,1),"turn clear invalidates metadata before joint scans");
    check(!const_requests[2][1].kit.current({6,11,2},11),"missing cache cannot supply a fresh actor witness");

    organization::Input in;
    in.frame.clock={5,11,1};in.frame.own.actor.id=11;in.frame.own.actor.team=Team::ally;
    Actor old;old.id=12;old.team=Team::ally;
    in.frame.actors[0]={old,in.frame.clock,12,Knowledge::exact};in.frame.actor_count=1;
    in.frame.group={Group{},in.frame.clock,7,Knowledge::exact};
    in.site_count=1;auto& site=in.sites[0];site.group.id=7;site.actor_count=1;site.actors[0]=12;
    site.cycles[0].actor=12;site.cycles[0].solo=true;site.cycles[0].clock=in.frame.clock;
    site.known=site.blocked=site.joint_better=site.damaged_now=true;site.holder=12;
    site.joint_rank.status=site.independent[0].status=Status::feasible;
    in.enemy_stress_count=1;in.enemy_stress[0]={100.,in.frame.clock,99,Knowledge::exact};
    in.delivery_rank.status=in.contest_rank.status=Status::feasible;in.role_ranks_current=true;
    in.legal_enemy_target=in.intercept_target=99;in.actual_payout=in.actual_bank=in.safe_now=true;
    in.reset_current();
    check(in.site_count==0 && in.frame.actor_count==0 && in.enemy_stress_count==0,"current array counts reset");
    check(in.frame.actors[0].subject==12 && site.cycles[0].actor==12,"unreachable large array payloads are retained");
    check(!site.cycle(12) && !site.known && !site.blocked && !site.joint_better && !site.damaged_now && site.holder==-1,
          "site reset hides old certificates and role facts before fuel-gated population");
    check(!site.joint_rank.eligible() && !in.delivery_rank.eligible() && !in.contest_rank.eligible() && !in.role_ranks_current,
          "current role ranks reset invalid");
    check(in.legal_enemy_target==-1 && in.intercept_target==-1 && !in.actual_payout && !in.actual_bank && !in.safe_now,
          "targets, receipts and safety cannot persist into the next observation");
    in.frame.clock={6,11,2};in.frame.own.actor.id=11;in.frame.own.actor.team=Team::ally;
    check(in.frame.visible(12)==nullptr && !in.frame.group.current(in.frame.clock,7),"new clock cannot reveal retained prior-frame payload");
    CycleCertificate joint;joint.actor=13;runtime_joint::install(site,joint);
    check(site.actor_count==1 && site.actors[0]==13 && !site.independent[0].eligible(),
          "joint-only append clears a retained earlier actor's independent rank");
    prepared_quote_needs_current_caller_proof();
    std::printf("runtime storage: %d checks passed\n",checks);
}
