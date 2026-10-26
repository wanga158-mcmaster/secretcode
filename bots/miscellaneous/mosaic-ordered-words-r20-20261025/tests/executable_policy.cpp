#include "../src/executable_policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace mosaic::executable_policy;
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(false)

static Snapshot facts(){
    Snapshot s{};s.stamp={4,20,1,30,10};s.authority_lease=111;s.self=99;s.pose={5,5};
    s.authority.dead=false;s.authority.raw_m=s.authority.raw_a=s.authority.raw_c=true;
    s.body_actual=s.restrictions_actual=s.channel_actual=true;
    s.target_count=1;s.targets[0]={TargetKind::camp,501,{6,5},true,true,true,false};
    s.move_count=3;s.moves[0]={{6,6},true,true,true};s.moves[1]={{4,5},true,true,true};s.moves[2]={{6,5},true,false,false};
    s.abilities[0]={true,true,false,true,target_self,0,true,0};
    s.abilities[1]={true,true,false,false,target_camp|target_enemy|target_ground,4,true,0};
    return s;
}
static Action move(Point p){return {ActionKind::move,TargetKind::ground,0,p};}
static Action basic(){return {ActionKind::basic,TargetKind::camp,501};}
static Action reset(){return {ActionKind::cast,TargetKind::self,99,{},0};}
static Action spell(){return {ActionKind::cast,TargetKind::camp,501,{},1};}
static Policy policy(const Snapshot& s,Action first){
    Policy p{};p.token={s.stamp,s.authority_lease,true};p.owner={1,12,1};p.actual_prefix=7;
    p.success.count=1;p.success.actions[0]=first;
    p.on_refusal.declared=p.on_changed.declared=true;p.on_refusal.guard=p.on_changed.guard=Guard::usable_move;
    p.on_refusal.word.count=p.on_changed.word.count=1;p.on_refusal.word.actions[0]=p.on_changed.word.actions[0]=move({4,5});
    return p;
}
static Receipt receipt(ServiceKey key,Revision serial,Stamp before,Point from,Point to,Outcome result=Outcome::accepted){
    Receipt r{};r.owner=key;r.serial=serial;r.before=before;r.after=before;++r.after.host_attempt;++r.after.observation;
    if(result==Outcome::accepted)++r.after.effect;
    r.before_pose=from;r.after_pose=to;r.outcome=result;r.actual_attempt=r.actual_body=true;
    r.before_authority.dead=r.after_authority.dead=false;
    r.before_authority.raw_m=r.before_authority.raw_a=r.before_authority.raw_c=true;r.after_authority=r.before_authority;
    r.action=move(to);if(result==Outcome::accepted)r.after_authority.raw_m=false;
    return r;
}
static void unknown_incumbent(){
    auto s=facts();const auto incumbent=policy(s,basic());auto alternative=policy(s,move({6,6}));
    CHECK(valid_policy(s,incumbent));CHECK(valid_policy(s,alternative));
    Comparison c{};c.joint_priced=c.optional_funded=true;
    c.incumbent.value=999;c.alternative.state=ValueState::conditional;c.alternative.key={s.stamp,11,22,6};c.alternative.value=10000;
    CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent); // UNKNOWN is not numericzero.
    c.incumbent.state=ValueState::conditional;c.incumbent.key=c.alternative.key;c.incumbent.value=-5;c.alternative.value=-4;
    c.incumbent.assurance=Assurance::certified;c.alternative.assurance=Assurance::none;
    CHECK(choose(s,incumbent,alternative,c)==Choice::alternative); // utility and assurance remain distinct.
    c.optional_funded=false;CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent);c.optional_funded=true;
    c.joint_priced=false;CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent);c.joint_priced=true;
    ++c.alternative.key.replies;CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent);--c.alternative.key.replies;
    ++s.stamp.observation;CHECK(token_matches(incumbent.token,s));CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent);
    c.incumbent.key.stamp=c.alternative.key.stamp=s.stamp;CHECK(choose(s,incumbent,alternative,c)==Choice::alternative);
    auto unsupported=policy(s,move({6,5}));CHECK(!authorize(s,unsupported.success.actions[0]));
    CHECK(choose(s,incumbent,unsupported,c)==Choice::incumbent);
    c.alternative.value=std::numeric_limits<double>::quiet_NaN();CHECK(choose(s,incumbent,alternative,c)==Choice::incumbent);
    CHECK(choose(incumbent,alternative,c)==Choice::incumbent); // no currentpacket, no replacement authority.
}
static void leased_native_authority(){
    Lease lease;auto s=facts();s.authority_lease=0;CHECK(lease.issue(s,1));const auto first=lease.token();
    ++s.stamp.observation;CHECK(lease.observe(s));CHECK(lease.valid());CHECK(token_matches(first,s));
    const auto before=s.stamp;CHECK(lease.before_attempt(s));CHECK(!lease.valid());CHECK(s.stamp.host_attempt==before.host_attempt+1);
    CHECK(!s.authority_lease&&!s.body_actual&&!s.restrictions_actual&&!s.channel_actual);CHECK(!authorize(s,move({6,6})));
    s.body_actual=s.restrictions_actual=s.channel_actual=true;++s.stamp.observation; // FALSE preserved physicalMAC/pose.
    CHECK(lease.issue(s,2));CHECK(!token_matches(first,s));const auto current=lease.token();
    auto bad=s;bad.authority_lease=999;const auto attempt=bad.stamp.host_attempt;
    CHECK(!lease.before_attempt(bad));CHECK(!lease.valid());CHECK(bad.stamp.host_attempt==attempt); // no phantomattempt.
    ++s.stamp.observation;CHECK(!lease.issue(s,3)); // newnonce cannot revive same spent physical generation.
    ++s.stamp.frame;CHECK(lease.issue(s,3));
    auto stale=facts();CHECK(!lease.issue(stale,4));CHECK(lease.valid()); // newer leaseID cannot rollback physicalstamp.
    auto changed=s;changed.authority.restrictions=1;++changed.stamp.observation;
    CHECK(!lease.observe(changed));CHECK(!lease.valid());
    Token missing=current;CHECK(!consume(missing,current.stamp));CHECK(!missing.valid);
    Token drift=current;auto after=current.stamp;++after.host_attempt;++after.epoch;
    CHECK(!consume(drift,after));CHECK(!drift.valid);
    Token overflow=current;overflow.stamp.host_attempt=invalid_cost;after=overflow.stamp;
    CHECK(!consume(overflow,after));CHECK(!overflow.valid);
    Token accepted=first;after=first.stamp;++after.host_attempt;++after.effect;
    CHECK(consume(accepted,after));CHECK(!accepted.valid);
    Token refused=first;after=first.stamp;++after.host_attempt;
    CHECK(consume(refused,after));CHECK(!refused.valid);
}
static void resource_and_guard_facts(){
    for(unsigned mac=0;mac<8;++mac)for(unsigned restrictions=0;restrictions<8;++restrictions)
    for(unsigned dead=0;dead<2;++dead)for(unsigned channel=0;channel<2;++channel){
        auto s=facts();s.authority.raw_m=mac&1;s.authority.raw_a=mac&2;s.authority.raw_c=mac&4;
        s.authority.restrictions=std::uint8_t(restrictions);s.authority.dead=dead;s.authority.channel=channel?Channel::recall:Channel::none;
        CHECK(authorize(s,move({6,6}))==(!dead&&!channel&&(mac&1)&&!(restrictions&1)));
        CHECK(authorize(s,basic())==(!dead&&!channel&&(mac&2)&&!(restrictions&2)));
        CHECK(authorize(s,spell())==(!dead&&!channel&&(mac&4)&&!(restrictions&4)));
    }
    auto s=facts();auto melee=resource_interval(s,basic());CHECK(melee.valid&&melee.success.raw_m&&!melee.success.raw_a);
    CHECK(melee.refusal==s.authority);
    s.attack_range=3;auto ranged=resource_interval(s,basic());CHECK(ranged.valid&&!ranged.success.raw_m&&!ranged.success.raw_a);
    auto post=s;post.authority=ranged.success;auto restored=resource_interval(post,reset());
    CHECK(restored.valid&&restored.success.raw_a&&!restored.success.raw_m&&!restored.success.raw_c);
    CHECK(restored.refusal==post.authority); // exclusivecastFALSE/ResetFALSE cannot spendorrestoreM.
    auto physical=policy(s,basic());physical.success.count=2;physical.success.actions[1]=move({6,6});
    CHECK(valid_shape(physical));CHECK(!valid_policy(s,physical));
    s.attack_range=1;physical.token={s.stamp,s.authority_lease,true};CHECK(valid_policy(s,physical));
    // A future landing can open after actualwork: initialblocked mask is not
    // consumed as futureauthority. The dispatcher must refresh after Basic.
    physical.success.actions[1]=move({6,5});CHECK(valid_policy(s,physical));CHECK(!authorize(s,move({6,5})));
    auto impossible_changed=physical;impossible_changed.on_changed.word.actions[0]=basic();
    CHECK(valid_shape(impossible_changed));CHECK(!valid_policy(s,impossible_changed)); // A already spent after firstBasic.
    auto moved_then_basic=physical;moved_then_basic.success.actions[0]=move({6,6});moved_then_basic.success.actions[1]=basic();
    CHECK(valid_shape(moved_then_basic));CHECK(!valid_policy(s,moved_then_basic)); // changed Move fallback has noMafterMove.
    auto too_many=physical;too_many.success.count=5;for(unsigned n=0;n<5;++n)too_many.success.actions[n]=basic();CHECK(!valid_shape(too_many));
    auto no_token=physical;no_token.token={};CHECK(valid_shape(no_token));CHECK(!valid_policy(no_token));
    s.abilities[1].exclusive_with_walk=true;s.authority.walked=true;CHECK(!authorize(s,spell()));
    s.authority.walked=false;s.authority.raw_m=false;CHECK(authorize(s,spell())); // spentM is not walked.
    Action ground{ActionKind::cast,TargetKind::ground,0,{7,5},1};CHECK(!authorize(s,ground));
    s.ground_count=1;s.grounds[0]={{7,5},true,true,true,false};CHECK(authorize(s,ground));
    s.abilities[1].moves_caster=true;CHECK(!authorize(s,ground));
    s.grounds[0].landing_open=true;CHECK(authorize(s,ground)); // no ordinaryreachable requirement for dash.
    s.authority.stunned=true;CHECK(!authorize(s,{ActionKind::recall}));s.authority.stunned=false;
    s.authority.channel=Channel::recall;s.channel_timer_actual=true;s.channel_ticks=3;
    Action wait{};wait.wait_reason=WaitReason::channel;CHECK(authorize(s,wait));CHECK(!authorize(s,basic()));
    s.authority.channel=Channel::none;s.capture_actual=s.group_clear=s.reward_available=s.on_region=true;
    s.contest_known=false;s.finite_capture_inputs=1;wait.wait_reason=WaitReason::capture;CHECK(authorize(s,wait));
    s.contest_known=true;s.uncontested=false;CHECK(!authorize(s,wait));
    s.contest_known=false;s.finite_capture_inputs=0;CHECK(!authorize(s,wait));
}
static Funding funding(){
    Funding f{};f.success.count=4;
    f.success.phases[0]={1,2,0,10};f.success.phases[1]={2,0,1,0};
    f.success.phases[2]={3,2,0,5};f.success.phases[3]={4,0,1,0};
    f.refusal_count=1;f.refusals[0]=f.success;f.refusals[0].count=6;
    f.refusals[0].phases[4]={5,2,0,10};f.refusals[0].phases[5]={6,0,1,0};
    return f;
}
static void explicit_funding(){
    const Price price{2000,300,100};const auto f=funding();const auto q=quote_funding(f,price);
    CHECK(q.valid&&q.success==8715&&q.refusal==13025&&q.required==13025);
    Book b;CHECK(!b.reserve(f,price,13024));CHECK(b.reserve(f,price,13025));CHECK(b.remaining()==13025);
    CHECK(b.phase_paid(1,3900));CHECK(!b.phase_paid(1,1));CHECK(b.remaining()==9015);
    CHECK(b.phase_paid(2,290));CHECK(b.remaining()==8715);
    Receipt success{};success.serial=1;success.actual_attempt=true;success.outcome=Outcome::accepted;
    CHECK(!b.follow(BranchKind::success,success));CHECK(b.remaining()==8715); // retainlatefalseescrow.
    CHECK(b.phase_paid(3,3000));CHECK(b.phase_paid(4,290));CHECK(b.remaining()==4410);
    Receipt refused{};refused.serial=2;refused.actual_attempt=true;refused.outcome=Outcome::refused;
    CHECK(b.follow(BranchKind::refusal,refused));CHECK(b.remaining()==4410);CHECK(b.funded(4410));CHECK(!b.funded(4409));
    CHECK(b.phase_paid(5,3000));CHECK(b.remaining()==400);CHECK(b.phase_paid(6,290));CHECK(b.remaining()==100);
    CHECK(!b.follow(BranchKind::success,success));success.serial=3;success.actual_body=true;CHECK(b.finish(success));
    CHECK(b.failure().failure==Failure::unfunded); // firstfailurelatchnever overwritten.
    auto duplicate=f;duplicate.success.phases[1].id=1;CHECK(!quote_funding(duplicate,price).valid);
    auto permuted=f;permuted.refusals[0].phases[0]=f.success.phases[1];permuted.refusals[0].phases[1]=f.success.phases[0];
    CHECK(!quote_funding(permuted,price).valid);
    auto changed_cost=f;++changed_cost.refusals[0].phases[0].sdk_queries;CHECK(!quote_funding(changed_cost,price).valid);
    auto overflow=f;overflow.success.phases[0].compute_bound=invalid_cost;CHECK(!quote_funding(overflow,price).valid);
    const Price enormous{invalid_cost,300,100};CHECK(!quote_funding(f,enormous).valid);
    Book overrun;CHECK(overrun.reserve(f,price,20000));CHECK(!overrun.phase_paid(1,4011));CHECK(overrun.failure().failure==Failure::bound_overrun);
    CHECK(overrun.remaining()==invalid_cost);
    FailureLatch latch;CHECK(latch.record(Failure::refused,{1,2,3,4,5},2));CHECK(!latch.record(Failure::unfunded,{},0));
    CHECK(latch.failure==Failure::refused&&latch.primitive==2);
}
static void retained_service_history(){
    ServiceOwner owner;const ServiceKey key{7,901,1};CHECK(owner.request(key)); // nativeGROUP901 != Runtimeordinal12
    auto r=receipt(key,1,{4,20,1,30,10},{5,5},{6,5});CHECK(owner.accept(r));CHECK(owner.trail_size()==1&&owner.actual_prefix()==1);
    CHECK(owner.word_boundary(1));CHECK(!owner.word_boundary(1));CHECK(owner.next_required()==NextRequired::work);
    owner.knowledge({100,100,100,100,100});CHECK(owner.trail_size()==1&&owner.actual_prefix()==1);
    auto false_r=receipt(key,2,r.after,{6,5},{6,5},Outcome::refused);false_r.actual_body=false;
    CHECK(!owner.accept(false_r));CHECK(owner.refusal_debt()==1&&owner.actual_prefix()==1&&owner.trail_size()==1);
    false_r.serial=3;CHECK(!owner.accept(false_r));CHECK(owner.refusal_debt()==1); // duplicatehostattemptnotnewdebt.
    owner.handoff_unknown();CHECK(!owner.pose_known()&&!owner.trail_complete());CHECK(owner.trail_size()==1&&owner.actual_prefix()==1);
    CHECK(!owner.request(key));CHECK(owner.refusal_debt()==1);
    const ServiceKey other{8,99,2};CHECK(owner.request(other));CHECK(owner.refusal_debt()==1&&owner.trail_size()==1&&owner.actual_prefix()==1);
    CHECK(owner.request(key));
    ServiceReceipt event{key,ServiceEvent::clear,false_r.after,100,true,12,0,{6,5}};
    CHECK(!owner.service(event));event.group=901;CHECK(owner.service(event));CHECK(owner.stage()==Stage::capture);
    CHECK(!owner.service(event));event.serial=101;event.event=ServiceEvent::reward;event.amount=15;
    CHECK(owner.service(event));CHECK(owner.stage()==Stage::bank);
    auto bank=receipt(key,4,false_r.after,{6,5},{6,5});bank.action.kind=ActionKind::bank;bank.after_authority.channel=Channel::bank;
    CHECK(owner.accept(bank));CHECK(owner.stage()==Stage::bank);CHECK(owner.word_boundary(4));
    owner.recover();CHECK(owner.next_required()==NextRequired::recover);
    event.event=ServiceEvent::recovered;event.serial=102;event.stamp=bank.after;event.amount=0;
    CHECK(owner.service(event));CHECK(owner.stage()==Stage::bank&&owner.refusal_debt()==0&&owner.trail_complete()&&owner.trail_size()==0);
    event.event=ServiceEvent::bank;event.serial=103;event.amount=0;CHECK(!owner.service(event));CHECK(owner.stage()==Stage::bank);
    event.serial=104;event.amount=15;CHECK(!owner.service(event));CHECK(owner.bank_pending());event.full_bank_actual=true;
    CHECK(owner.service(event));CHECK(owner.next_required()==NextRequired::none&&!owner.bank_pending());
    auto stale=receipt(key,999,{3,9,0,0,2},{0,0},{1,0});CHECK(!owner.accept(stale));CHECK(owner.actual_prefix()==4&&owner.trail_size()==0);
    event.serial=999;event.owner.episode=0;CHECK(!owner.service(event));
    ServiceOwner overflow;overflow.request(key);Stamp stamp{1,1,1,1,0};
    for(unsigned n=0;n<trail_capacity+1;++n){auto step=receipt(key,n+1,stamp,{int(n),0},{int(n+1),0});
        CHECK(overflow.accept(step));stamp=step.after;++stamp.epoch;++stamp.frame;}
    CHECK(overflow.trail_size()==trail_capacity&&!overflow.trail_complete());CHECK((overflow.actual_pose()==Point{int(trail_capacity+1),0}));
    auto debt=receipt(key,100,stamp,overflow.actual_pose(),overflow.actual_pose(),Outcome::refused);CHECK(overflow.accept(debt));
    overflow.knowledge({999,999,999,999,999});overflow.request(other);
    CHECK(overflow.refusal_debt()==1&&overflow.trail_size()==trail_capacity&&!overflow.trail_complete());
    ServiceOwner chronology;chronology.request(key);auto first=receipt(key,1,{4,20,1,30,10},{5,5},{6,5});CHECK(chronology.accept(first));
    ServiceReceipt ordered{key,ServiceEvent::clear,{4,30,2,80,11},100,true,901,0,{6,5}};
    CHECK(chronology.service(ordered));ordered.event=ServiceEvent::reward;ordered.serial=101;ordered.amount=15;ordered.stamp={4,25,2,50,11};
    CHECK(!chronology.service(ordered));CHECK(chronology.stage()==Stage::capture);
    ordered.stamp={4,31,2,81,11};CHECK(chronology.service(ordered));ordered.event=ServiceEvent::bank;ordered.serial=102;ordered.stamp={4,30,2,80,11};
    CHECK(!chronology.service(ordered));CHECK(chronology.stage()==Stage::bank);
    ordered.stamp={4,32,2,82,11};ordered.full_bank_actual=true;CHECK(chronology.service(ordered));CHECK(chronology.stage()==Stage::complete);
    ServiceOwner trip;const ServiceKey group_a{55,901,1},group_b{55,1903,1};trip.request(group_a);
    ServiceReceipt carried{group_a,ServiceEvent::clear,{7,1,0,1,0},1,true,901,0,{5,5}};CHECK(trip.service(carried));
    carried.serial=2;carried.event=ServiceEvent::reward;carried.amount=8;CHECK(trip.service(carried));CHECK(trip.bank_pending());
    CHECK(trip.request(group_b,Purpose::work));CHECK(trip.stage()==Stage::work&&trip.bank_pending());
    trip.knowledge({7,2,0,2,0});trip.handoff_unknown();CHECK(trip.bank_pending());
    carried.owner=group_b;carried.serial=3;carried.event=ServiceEvent::bank;carried.amount=8;
    CHECK(!trip.service(carried));CHECK(trip.bank_pending()); // only starting/predicting bank cannot pay debt.
    carried.full_bank_actual=true;carried.amount=0;CHECK(!trip.service(carried));CHECK(trip.bank_pending());
    carried.amount=8;CHECK(trip.service(carried));CHECK(!trip.bank_pending()&&trip.stage()==Stage::complete);
    trip.request({55,2001,2});carried.owner=trip.key();carried.group=2001;carried.event=ServiceEvent::clear;carried.serial=4;CHECK(trip.service(carried));
    carried.event=ServiceEvent::reward;carried.serial=5;carried.amount=9;CHECK(trip.service(carried));CHECK(trip.bank_pending());
    carried.event=ServiceEvent::loss;carried.serial=6;carried.actual=false;CHECK(!trip.service(carried));CHECK(trip.bank_pending());
    carried.actual=true;CHECK(trip.service(carried));CHECK(!trip.bank_pending()&&trip.stage()==Stage::recover);
}
int main(){unknown_incumbent();leased_native_authority();resource_and_guard_facts();explicit_funding();retained_service_history();
    std::printf("executable_policy: %u checks passed\n",checks);
}
