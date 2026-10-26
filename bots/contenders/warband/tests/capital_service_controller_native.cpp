#define WB_SERVICE_CONTROLLER_NATIVE_HOST_FREE
#define WB_SERVICE_FORECAST_NATIVE_HOST_FREE
#define WB_ENCOUNTER_NATIVE_HOST_FREE
#define WB_SERVICE_NATIVE_HOST_FREE
#include "capital_service_controller_native.h"
#include <cstdio>
#include <memory>
namespace c=wb::capital::service_controller_native;
namespace f=wb::capital::service_forecast;
static unsigned checks{},failures{};
static void check(bool pass,const char* why){++checks;if(!pass){++failures;std::printf("FAIL %s\n",why);}}
static void scenario_diagnostics(){
    using R=c::ScenarioReason;namespace h=c::h;
    for(const int actor:{-1,0,65534})for(const int detail:{-1,0,65534})for(const int round:{-1,0,2000,65534}){
        const c::ScenarioStamp original{R::SelectedNpcRange,actor,detail,round,false};const auto word=c::scenario_word(original);const auto decoded=c::scenario_stamp(word);
        check(c::scenario_word_valid(word)&&decoded.reason==original.reason&&decoded.actor==actor&&decoded.detail==detail&&decoded.source_round==round&&!decoded.overflow,
              "scenario stamp preserves zero IDs, unknown sentinels and source round");
    }
    for(const int invalid:{-2,65535}){
        const auto decoded=c::scenario_stamp(c::scenario_word({R::ExternalPoint,invalid,invalid,invalid,false}));
        check(decoded.overflow&&decoded.actor==-1&&decoded.detail==-1&&decoded.source_round==-1,"out-of-range scenario fields stay explicitly unknown");
    }
    check(!c::scenario_word_valid(std::uint64_t(R::Count))&&!c::scenario_word_valid(std::uint64_t(1)<<56),"unknown reason and reserved bit are rejected");
    for(const bool assigned:{false,true})for(const auto geometry:{h::Geometry::Unknown,h::Geometry::Supported,h::Geometry::Inconsistent})
    for(const auto life:{h::Life::Unknown,h::Life::Alive,h::Life::Dead})for(const bool public_spawn:{false,true})for(const bool first:{false,true})
    for(const auto reason:{h::ClassReason::None,h::ClassReason::LifeUnknown,h::ClassReason::Dead,h::ClassReason::ChannelUnknown,h::ClassReason::ActiveChannel,h::ClassReason::EffectsUnknown,h::ClassReason::UnsupportedEffects}){
        h::Witness witness;witness.actor.id=0;witness.point_assigned=assigned;witness.geometry=geometry;witness.current.life=life;witness.origin_is_public_spawn=public_spawn;witness.class_reason=reason;
        const bool rejected=!assigned||geometry!=h::Geometry::Supported||life!=h::Life::Alive||(public_spawn&&!first)
            ||reason==h::ClassReason::Dead||reason==h::ClassReason::ActiveChannel||reason==h::ClassReason::UnsupportedEffects;
        const auto stamp=c::scenario_witness_fault(witness,first,17);
        check((stamp.reason!=R::NotSampled)==rejected,"diagnostic witness split exactly preserves the prior rejection predicate");
        if(rejected)check(stamp.actor==0&&stamp.source_round==17,"rejection keeps actual zero actor and source epoch");
    }
    for(unsigned mask=0;mask<8;++mask)for(const bool actual:{false,true}){
        const unsigned faults=(mask&1?unsigned(c::en::Channel):0u)|(mask&2?unsigned(c::en::Restrictions):0u)|(mask&4?unsigned(c::en::UnknownPassive):0u);
        const auto stamp=c::scenario_external_fault(0,actual,faults,18);
        check((stamp.reason!=R::NotSampled)==(actual&&faults!=0),"external diagnostic preserves actual-only known fault rejection");
        if(actual&&faults)check(stamp.reason==(mask&1?R::ExternalChannel:mask&2?R::ExternalRestrictions:R::ExternalPassive),"fault union reports its first actual rejecting branch");
    }
    h::Snapshot snapshot;snapshot.status=h::Status::Unknown;snapshot.assignment=h::Assignment::NeedFuel;snapshot.reason=h::Reason::Funding;
    auto rejection=c::scenario_snapshot_fault(snapshot,0);check(rejection.reason==R::SnapshotJoint&&rejection.detail==((int(h::Status::Unknown)<<8)|(int(h::Assignment::NeedFuel)<<4)|int(h::Reason::Funding)),"snapshot funding remains a typed status/assignment/reason");
    snapshot.count=1;snapshot.actors[0].actor.id=0;snapshot.actors[0].geometry=h::Geometry::Inconsistent;snapshot.actors[0].geometry_reason=h::GeometryReason::ObservedOriginRefuted;
    rejection=c::scenario_snapshot_fault(snapshot,0);check(rejection.reason==R::SnapshotActorGeometry&&rejection.actor==0&&rejection.detail==int(h::GeometryReason::ObservedOriginRefuted),"computed refuted-origin witness identifies actual zero actor without new observation");
    std::array<char,4096> line{};std::array<std::uint64_t,112> values{};values.back()=c::scenario_word({R::WitnessLife,0,0,0,false});
    const auto old=c::audit_line(line,values);check(old&&line[18]=='1',"old sampled packet codec remains v1");
    const auto next=c::scenario_line(line,values);check(next==old&&line[18]=='2',"scenario codec adds only the versioned 112th-field contract");
    values.fill(~std::uint64_t(0));check(c::scenario_line(line,values)>0,"112 maximum-width fields fit existing single buffer");
}
namespace receipt_regression {
namespace e=wb::capital::service_execution;
static f::Verdict legal(void*,const f::World&,unsigned,const f::Primitive&){return f::Verdict::Allowed;}
static f::Verdict visible(void*,const f::World&,unsigned,int){return f::Verdict::Allowed;}
static f::Verdict spawn(void*,const f::World&,unsigned,f::Point p){return p==f::Point{0,1}?f::Verdict::Allowed:f::Verdict::Blocked;}
static f::Verdict altar(void*,const f::World& w,unsigned a,f::Point p){return p==f::Point{5,1}&&f::distance(w.bodies[a].pose,p)==1?f::Verdict::Allowed:f::Verdict::Blocked;}
static f::Verdict uncontested(void*,const f::World&){return f::Verdict::Allowed;}
static bool permit(void*,unsigned){return true;}
static f::World world(){
    f::World w;w.round=0;w.horizon=13;w.phase=f::Phase::ActorAction;w.actors=w.npcs=w.slots=1;w.revision=901;
    auto& b=w.bodies[0];b.id=0;b.actual=true;b.pose={1,1};b.hp=b.max_hp=1000;b.strength=70;b.focus=100;
    b.bag_known=b.gold_known=b.splash_radius_known=true;b.splash_radius=1;b.effective_recall_ticks=5;
    auto& n=w.camps[0];n.id=5;n.pose={2,1};n.hp=n.max_hp=120;n.actual=n.empty_batches_known=true;
    w.group.id=8;w.group.life=f::Life::Alive;w.group.roster_complete=w.group.capture_history_known=true;w.group.generation=1;w.group.gold=60;w.group.gems=14;
    auto& r=w.rules;r.queried=true;r.capture_ticks=2;r.sleep_batches=2;r.respawn_delay=10;r.reset_cooldown=8;r.recall_ticks=5;
    r.bank_multiplier=.1;r.spawn_healing=1;r.max_rounds=2000;r.winning_gems=1000;w.even[0]=w.odd[0]={true,0,-1};return w;
}
static f::Program program(){
    f::Program p;p.id=7001;const std::array<std::array<f::Point,5>,6> paths{{
        {{{1,1}}},{{{1,1},{2,1}}},{{{2,1},{3,1}}},{{{3,1},{4,1}}},{{{4,1},{3,1},{2,2},{1,2},{0,1}}},{{{0,1},{1,1}}}}};
    const unsigned size[6]={1,2,2,2,5,2};
    for(unsigned k=0;k<6;++k){p.bodies[0].legs[k]=int(p.leg_count);auto& leg=p.legs[p.leg_count++];leg.origin=paths[k][0];leg.begin=p.route_count;leg.count=size[k]-1;leg.complete=true;
        for(unsigned n=1;n<size[k];++n)p.route[p.route_count++]={paths[k][n],0,13};}
    p.bodies[0].altar={5,1};p.bodies[0].altar_known=true;p.bodies[0].post_renewal=f::PostRenewal::ConditionalHold;return p;
}
static const f::Result& selected(){
    static auto simulator=std::make_unique<f::Simulator>();static bool done=false;
    if(!done){const f::Domain domain{901,902,903,904,905,true,true,true,true,true,false};
        const f::Callbacks cb{nullptr,&legal,&visible,&spawn,&altar,&uncontested,nullptr,nullptr,nullptr,&permit,&visible};
        check(simulator->begin(world(),program(),domain,cb),"receipt regression enters real selected Simulator");
        unsigned calls=0;while(simulator->result().status==f::Status::Building&&calls++<1000)simulator->advance(64);
        check(simulator->result().status==f::Status::Complete&&simulator->result().bodies[0].banked==14&&simulator->result().bodies[0].renewed,"real selected policy covers N2 payout and renewal");done=true;}
    return simulator->result();
}
struct Run {e::Interpreter interpreter;e::Frame frame;std::array<e::Receipt,2> receipts{};e::Directive bank{};};
static std::unique_ptr<Run> start(){
    auto r=std::make_unique<Run>();const auto w=world();auto& x=r->frame;x.stamp={0,0,1,1,0};x.body=w.bodies[0];x.group=w.group;x.npcs=1;x.camps=w.camps;
    x.observed=x.fresh=x.permissions_known=x.roster_complete=x.in_group_known=x.on_spawn_known=true;
    check(r->interpreter.begin(x,program(),selected(),{x.stamp,8,1,7001,901,905,13},w),"real Interpreter owns selected immutable programme");return r;
}
static void next(Run& r,int round){auto& x=r.frame;x.receipts={};x.stamp.round=round;++x.stamp.input;++x.stamp.sample;x.body.m=x.body.a=x.body.c=true;x.body.walked=x.body.cast_used=x.body.completed=false;}
static e::Directive intention(Run& r,f::Action action){const auto d=r.interpreter.choose(r.frame);check(d.kind==e::Kind::Physical&&d.primitive.action==action,"real Interpreter emits required physical prefix");return d;}
static void execute(Run& r,const e::Directive& d){
    auto& x=r.frame;x.receipts={};++x.stamp.sample;++x.stamp.effect;
    switch(d.primitive.action){
    case f::Action::Basic:x.body.a=false;x.camps[0].hp=std::max(0.0f,x.camps[0].hp-70);if(!x.camps[0].hp)x.group.life=f::Life::Reward;break;
    case f::Action::Reset:x.body.a=true;x.body.c=false;x.body.cast_used=true;x.body.cooldown[0]=8;break;
    case f::Action::Move:x.body.pose=d.primitive.destination;x.body.m=false;x.in_group=x.body.pose==f::Point{2,1};x.on_spawn=x.body.pose==f::Point{0,1};break;
    case f::Action::Bank:x.body.channel=f::Channel::Bank;x.body.channel_ticks=2;x.body.m=x.body.a=x.body.c=false;r.bank=d;break;
    default:check(false,"unsupported receipt-regression mock host command");break;
    }
    check(r.interpreter.attempt(d,e::Attempt::Accepted,x),"actual accepted command consumes fresh real Interpreter readback");
}
static void work_and_capture_wait(Run& r){
    execute(r,intention(r,f::Action::Basic));execute(r,intention(r,f::Action::Reset));execute(r,intention(r,f::Action::Basic));execute(r,intention(r,f::Action::Move));
    check(r.interpreter.choose(r.frame).kind==e::Kind::Wait,"actual reward capture wait is retained");
    next(r,1);check(r.interpreter.choose(r.frame).kind==e::Kind::Wait,"first reward owner boundary");
    next(r,2);check(r.interpreter.choose(r.frame).kind==e::Kind::Wait,"second reward owner boundary");
}
static void capture_receipt(Run& r){
    next(r,3);auto& x=r.frame;x.group.life=f::Life::Respawning;x.body.bag=14;x.body.gold=60;
    auto& v=r.receipts[0];v={};v.event=1;v.program=7001;v.observed=x.stamp;v.kind=e::ReceiptKind::GroupPayout;v.evidence=e::Evidence::SupportedConservation;
    v.group=8;v.generation=1;v.source_known=v.amount_known=true;v.gems=14;v.gold=60;x.receipts={r.receipts.data(),1};
}
static void bank_wait(Run& r){
    work_and_capture_wait(r);capture_receipt(r);execute(r,intention(r,f::Action::Move));next(r,4);execute(r,intention(r,f::Action::Move));execute(r,intention(r,f::Action::Bank));
    check(!r.interpreter.facts().bank_attributed,"accepted void Submit has no payout credit");
    next(r,5);r.frame.body.channel_ticks=1;r.frame.body.m=r.frame.body.a=r.frame.body.c=false;check(r.interpreter.choose(r.frame).kind==e::Kind::Wait,"actual N2 channel continues before due boundary");
}
static void bank_receipt(Run& r){
    next(r,6);auto& x=r.frame;x.body.channel=f::Channel::None;x.body.channel_ticks=0;x.body.bag=0;
    auto& v=r.receipts[1];v={};v.event=2;v.program=7001;v.source_directive=r.bank.id;v.observed=x.stamp;v.kind=e::ReceiptKind::BankPayout;
    v.evidence=e::Evidence::SupportedConservation;v.source_known=v.amount_known=true;v.group=8;v.generation=1;v.gems=v.source_bag=14;x.receipts={&v,1};
}
static void regressions(){
    {auto run=start();bank_wait(*run);bank_receipt(*run);run->frame.body.stun_present=true;run->frame.body.stun_ticks=1;
        const auto head=c::reconcile_head(run->interpreter,run->frame,false);const auto& facts=run->interpreter.facts();
        check(head.refuted&&head.directive.kind==e::Kind::Handoff&&head.directive.id==0,"changed control refutes remaining programme without emitting physical word");
        check(facts.bank_attributed&&facts.banked_gems==14&&facts.bank_event==2,"supported payout is retained before newly observed stun refutation");
        check(run->interpreter.state()==e::State::Handoff,"new stun leaves actual receipt with no continuation authority");
        auto stale=run->frame;++stale.stamp.sample;++stale.stamp.effect;check(!run->interpreter.attempt(run->bank,e::Attempt::Accepted,stale),"old consumed Submit cannot be reused after actual payout and refutation");}
    {auto run=start();work_and_capture_wait(*run);capture_receipt(*run);
        const auto head=c::reconcile_head(run->interpreter,run->frame,false);const auto& facts=run->interpreter.facts();
        check(head.refuted&&head.directive.kind==e::Kind::Handoff&&head.directive.primitive.action==f::Action::Hold,"refuted capture head cancels proposed release movement");
        check(facts.capture_attributed&&facts.captured_gems==14&&facts.captured_gold==60&&facts.capture_event==1,"attributed source payout survives changed model");
        check(!facts.source_released&&run->frame.body.pose==f::Point{2,1},"canceled release has no invented position or source-release effect");
        check(run->interpreter.choose(run->frame).kind==e::Kind::Handoff,"canceled outstanding intention cannot reappear on stale same-frame choose");}
    {auto wrapped=start(),direct=start();work_and_capture_wait(*wrapped);work_and_capture_wait(*direct);capture_receipt(*wrapped);capture_receipt(*direct);
        const auto head=c::reconcile_head(wrapped->interpreter,wrapped->frame,true);const auto expected=direct->interpreter.choose(direct->frame);
        check(!head.refuted&&head.directive.kind==e::Kind::Physical&&head.directive.id==expected.id&&head.directive.source==expected.source&&head.directive.primitive.action==expected.primitive.action&&head.directive.primitive.destination==expected.primitive.destination,"model-consistent wrapper returns exactly the real Interpreter intention once");
        check(wrapped->interpreter.choose(wrapped->frame).reason==e::Reason::AwaitAttempt,"returned head retains one outstanding command rather than choosing again");
        execute(*wrapped,head.directive);check(!wrapped->interpreter.attempt(head.directive,e::Attempt::Accepted,wrapped->frame),"same physical head cannot be accepted twice");
        check(direct->interpreter.cancel(expected),"comparison's unattempted intention explicitly canceled");}
    {auto run=start();bank_wait(*run);bank_receipt(*run);auto head=c::reconcile_head(run->interpreter,run->frame,true);execute(*run,head.directive);
        next(*run,7);execute(*run,intention(*run,f::Action::Move));next(*run,8);execute(*run,intention(*run,f::Action::Move));next(*run,9);execute(*run,intention(*run,f::Action::Move));
        check(run->interpreter.choose(run->frame).kind==e::Kind::Wait,"real full-health spawn readiness preserves input movement limit");
        next(*run,10);execute(*run,intention(*run,f::Action::Move));next(*run,11);check(run->interpreter.choose(run->frame).kind==e::Kind::Wait,"renewal waits actual next generation");
        next(*run,12);check(run->interpreter.choose(run->frame).kind==e::Kind::Wait,"second next-generation wait");
        next(*run,13);run->frame.group.life=f::Life::Alive;run->frame.group.generation=2;run->frame.camps[0].hp=120;execute(*run,intention(*run,f::Action::Basic));
        head=c::reconcile_head(run->interpreter,run->frame,false);
        check(!head.refuted&&head.directive.kind==e::Kind::Complete&&run->interpreter.facts().renewed,"already actual complete renewal is preserved even when no future model is admitted");}
    {auto run=start();run->frame.body.hp=999;const auto head=c::reconcile_head(run->interpreter,run->frame,false);
        check(!head.refuted&&head.directive.kind==e::Kind::Handoff&&head.directive.reason==e::Reason::Readback,"actual stale/readback handoff is not relabeled as a model outcome");}
}
}
int main(){
    std::array<char,4096> audit_buffer;std::array<std::uint64_t,112> extreme;extreme.fill(~std::uint64_t(0));
    const auto audit_size=c::audit_line(audit_buffer,extreme);unsigned spaces=0;for(unsigned i=0;i<audit_size;++i)spaces+=audit_buffer[i]==' ';
    check(audit_size&&audit_size<audit_buffer.size()&&spaces==112&&audit_buffer[audit_size-1]=='\n',"maximum-width complete audit packet fits actual emitter without silent truncation");
    check(c::audit_line(std::span<char>{audit_buffer.data(),64},extreme)==0,"partial audit encoding is not a publishable line");
    std::array<std::uint64_t,1> capture_zero{{0}};const auto zero_size=c::audit_line(audit_buffer,capture_zero);
    check(zero_size>2&&audit_buffer[zero_size-2]=='0',"valid stage zero remains serialized instead of becoming missing");
    const std::array<std::uint8_t,9> cells{{1,0,0,0,0,0,0,1,1}};std::vector<std::uint8_t> packed;
    check(c::pack_cells(3,3,cells,packed)&&packed.size()==2&&packed[0]==129&&packed[1]==1,"owned cell bytes explicitly convert to packed row-major bits");
    const std::vector<std::uint8_t> sdk_export(121,0);
    check(wb::capital::service_bitmaps::validate(wb::capital::service_bitmaps::PackedView{31,31,sdk_export}).valid,"actual SDK31x31 export has121 packed bytes");
    check(!c::pack_cells(31,31,sdk_export,packed),"SDK packed export cannot be mistaken for961 cell bytes and packed a second time");
    static c::ss::Storage<c::History> store;static c::Counters counters;
    const c::ss::Admission admitted{nullptr,[](void*,c::ss::Component,std::size_t){return c::ss::Decision::Allow;},nullptr};
    while(!store.ready())store.advance(admitted);static c::Memory m(store,counters);
    m.prior.observed=true;m.prior.stamp={3,10,20,30,40};m.actual.stamp={3,11,21,31,40};
    m.prior.body.pose={1,1};m.prior.body.hp=m.prior.body.max_hp=100;m.prior.body.npc_taken_multiplier=1;
    m.prior.body.shield=40;m.prior.body.shield_ticks=1;m.captured.world.npcs=1;
    auto& npc=m.captured.world.camps[0];npc.pose={2,1};npc.strength=50;npc.range=3;m.actual.body.hp=90;
    check(c::npc_only_prefix_not_refuted(m),"timer1 own shield covers one pre-owner-start camp batch");
    m.actual.body.hp=89;check(!c::npc_only_prefix_not_refuted(m),"extra observed damage refutes declared NPC-only prefix");
    m.prior.body.shield_ticks=0;m.actual.body.hp=50;check(c::npc_only_prefix_not_refuted(m),"zero-timer uncertainty receives no beneficial shield credit");
    m.prior.body.shield_ticks=1;m.captured.world.npcs=2;m.captured.world.camps[1]=npc;m.captured.world.camps[1].strength=30;
    m.actual.body.hp=60;check(c::npc_only_prefix_not_refuted(m),"two sources consume one shield in source order within one batch");
    m.actual.body.hp=55;check(!c::npc_only_prefix_not_refuted(m),"two enemy opportunities cannot manufacture a second camp batch allowance");
    m.actual.body.hp=100;m.actual.body.stun_present=true;check(!c::npc_only_prefix_not_refuted(m),"supported NPC domain cannot explain a newly observed stun");
    m.actual.body.stun_present=false;m.prior.body.hp=25;m.prior.body.shield=0;m.prior.on_spawn_known=m.prior.on_spawn=true;m.captured.world.rules.spawn_healing=1;
    m.captured.world.npcs=1;m.actual.body.hp=50;check(c::npc_only_prefix_not_refuted(m),"actual on-spawn END healing precedes next camp damage");
    m.actual.body.hp=49;check(!c::npc_only_prefix_not_refuted(m),"spawn restoration does not erase subsequently observed unexplained damage");
    m.actual.stamp.round=12;check(!c::npc_only_prefix_not_refuted(m),"missing owner observation cannot claim a one-batch horizon");
    m.actual.stamp=m.prior.stamp;m.actual.body.hp=1;check(!c::npc_only_prefix_not_refuted(m),"supported own word cannot explain same-input self damage");
    m.actual.receipts={m.receipts.data(),1};c::remember_actual(m);check(m.prior.receipts.empty()&&m.prior.body.hp==1,"retained actual prefix owns scalars but not the recycled receipt span");
    f::World initial;initial.actors=1;initial.bodies[0].id=3;initial.bodies[0].bag_known=initial.bodies[0].gold_known=true;initial.group.id=4;initial.group.generation=1;
    wb::capital::service_execution::Frame prior,now;prior.observed=now.observed=now.fresh=true;prior.stamp={3,10,20,30,40};now.stamp={3,11,21,31,40};
    prior.group=now.group=initial.group;prior.group.life=f::Life::Reward;now.group.life=f::Life::Respawning;now.body.alive=now.body.bag_known=now.body.gold_known=true;
    now.body.gold=120;const auto gold=c::capture_delta(initial,prior,now,true);check(gold.known&&gold.gems==0&&gold.gold==120,"gold-only source payout conserves actual wallet without inventing gems");
    check(!c::capture_delta(initial,prior,now,false).known,"unknown other-income closure cannot attribute source payout");
    now.body.gold=0;now.body.bag=8;const auto gems=c::capture_delta(initial,prior,now,true);check(gems.known&&gems.gems==8&&gems.gold==0,"gem-only actual capture is distinct from channel start and score");
    now.group.id=5;check(!c::capture_delta(initial,prior,now,true).known,"another group's respawn cannot attribute the selected source");
    now.group.id=4;now.stamp.round=12;check(!c::capture_delta(initial,prior,now,true).known,"missing owner observation leaves capture conservation unknown");
    receipt_regression::regressions();scenario_diagnostics();
    std::printf("service controller native: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
