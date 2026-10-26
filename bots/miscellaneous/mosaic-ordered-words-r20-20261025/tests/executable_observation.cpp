#include "../src/executable_policy.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace executable_policy=mosaic::executable_policy;
namespace ep=executable_policy;
struct Pure {
#define MOSAIC_EXECUTABLE_OBSERVATION_PURE_ONLY
#include "../src/executable_observation.h"
#undef MOSAIC_EXECUTABLE_OBSERVATION_PURE_ONLY
};
using Facts=Pure::ExecutableObservationFacts;
using Owner=Pure::ExecutableOwnerDraft;
using Target=Pure::ExecutableTargetDraft;
using Move=Pure::ExecutableMoveDraft;
using Cone=Pure::ExecutableObservationCone;
using Preparation=Pure::ExecutableObservationPreparation;
static unsigned checks;
static void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"executable-observation failed line%d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

static Owner owner(){
    Owner d{};d.self=91;d.pose={5,5};d.authority.dead=false;
    d.authority.raw_m=d.authority.raw_a=d.authority.raw_c=true;
    d.health=649.0f;d.shield=12.25f;d.attack_range=1;
    d.body_read=d.restrictions_read=d.buffs_read=true;return d;
}
static ep::Stamp stamp(){return {4,20,3,31,8};}
static Target camp(unsigned id=501,ep::Point at={6,5}){
    Target d{};d.fact.kind=ep::TargetKind::camp;d.fact.id=id;d.fact.point=at;
    d.fact.visible=d.fact.alive=true;d.fact.health=35.125f;
    d.visibility_read=d.life_read=d.health_read=true;return d;
}
static Move step(ep::Point at={5,6}){
    Move d{};d.fact.point=at;d.fact.reachable=true;d.geometry_read=d.visibility_read=d.visible=true;
    d.champion_read=d.camp_read=d.teleport_read=true;return d;
}
static ep::Action basic(unsigned id=501){ep::Action a{};a.kind=ep::ActionKind::basic;a.target_kind=ep::TargetKind::camp;a.target_id=id;return a;}
static ep::Action moving(ep::Point at={5,6}){ep::Action a{};a.kind=ep::ActionKind::move;a.target_kind=ep::TargetKind::ground;a.point=at;return a;}
static ep::Action recall(){ep::Action a{};a.kind=ep::ActionKind::recall;a.target_kind=ep::TargetKind::self;a.target_id=91;return a;}
static ep::Snapshot ready(){
    ep::Snapshot s{};CHECK(Facts::publish_owner(s,owner(),stamp()));
    CHECK(Facts::publish_target(s,camp()));CHECK(Facts::publish_move(s,step()));return s;
}
static ep::Policy policy(const ep::Snapshot& s,ep::Action action){
    ep::Policy p{};p.owner={7,3,9};p.token={s.stamp,s.authority_lease,true};p.success.count=1;p.success.actions[0]=action;
    p.on_changed.declared=true;p.on_changed.guard=ep::Guard::always;p.on_changed.word.count=1;p.on_changed.word.actions[0]=recall();
    if(action.kind==ep::ActionKind::move){p.on_refusal.declared=true;p.on_refusal.guard=ep::Guard::always;
        p.on_refusal.word.count=1;p.on_refusal.word.actions[0]=recall();}
    return p;
}

static void owner_transaction(){
    for(unsigned mask=0;mask<8;++mask){
        ep::Snapshot s{};s.stamp={1,2,3,4,5};s.self=12;s.pose={40,50};s.health=-77;s.shield=-88;
        auto d=owner();d.body_read=(mask&1)!=0;d.restrictions_read=(mask&2)!=0;d.buffs_read=(mask&4)!=0;
        const bool complete=Facts::publish_owner(s,d,stamp());CHECK(complete==(mask==7));
        if(mask!=7){CHECK((s.stamp==ep::Stamp{1,2,3,4,5}));CHECK(s.self==12);CHECK((s.pose==ep::Point{40,50}));
            CHECK(!s.body_actual&&!s.restrictions_actual&&!s.channel_actual);CHECK(s.health==-77&&s.shield==-88);
        }else{CHECK(s.stamp==stamp());CHECK(s.self==91);CHECK((s.pose==ep::Point{5,5}));
            CHECK(s.body_actual&&s.restrictions_actual&&s.channel_actual&&s.health_actual&&s.shield_actual);
            CHECK(s.health==double(d.health)&&s.shield==double(d.shield));CHECK(s.authority==d.authority);}
    }
    auto d=owner();float residual=649.0f;for(unsigned n=0;n<5;++n)residual-=129.8f;
    CHECK(residual>0);d.health=residual;d.shield=0.125f;d.authority.raw_m=false;
    d.authority.raw_c=false;d.authority.walked=true;d.authority.restrictions=2;d.authority.stunned=true;
    ep::Snapshot s{};CHECK(Facts::publish_owner(s,d,stamp()));
    CHECK(s.health==double(residual)&&s.health>0&&s.shield==0.125);
    CHECK(!s.authority.raw_m&&s.authority.raw_a&&!s.authority.raw_c&&s.authority.walked);
    CHECK(s.authority.restrictions==2&&s.authority.stunned);CHECK(!ep::guard_allows(ep::Guard::usable_attack,s));
    CHECK(!ep::authorize(s,recall()));
    // Body action flags and restraint/stun classification remain independent.
    d.authority.stunned=false;d.authority.restrictions=0;CHECK(Facts::publish_owner(s,d,stamp()));
    CHECK(ep::authorize(s,recall()));
}

static void target_transactions(){
    auto s=ready();const auto initial=s.targets[0];const auto count=s.target_count;
    auto d=camp(502);d.visibility_read=false;CHECK(!Facts::publish_target(s,d));CHECK(s.target_count==count);
    d.visibility_read=true;d.life_read=false;CHECK(!Facts::publish_target(s,d));CHECK(s.target_count==count);
    d.life_read=true;d.health_read=false;CHECK(!Facts::publish_target(s,d));CHECK(s.target_count==count);
    CHECK(s.targets[0].id==initial.id&&s.targets[0].health==initial.health&&ep::authorize(s,basic()));
    d.health_read=true;d.fact.health=0.03125f;d.fact.shield=999;CHECK(Facts::publish_target(s,d));
    const auto* live=ep::target(s,basic(502));CHECK(live&&live->actual&&live->visible&&live->alive);
    CHECK(live->health_actual&&live->health==0.03125&&!live->shield_actual);CHECK(ep::authorize(s,basic(502)));
    d.shield_read=true;d.fact.shield=3.75f;CHECK(Facts::publish_target(s,d));
    live=ep::target(s,basic(502));CHECK(live&&live->shield_actual&&live->shield==3.75);
    // An actual native dead flag is complete without a live HP query.
    d.fact.alive=false;d.fact.health=999;d.health_read=false;d.shield_read=false;
    CHECK(Facts::publish_target(s,d));live=ep::target(s,basic(502));
    CHECK(live&&live->actual&&live->visible&&!live->alive&&live->health_actual&&live->health==0);
    CHECK(!ep::authorize(s,basic(502)));
    // Hidden is not observed death, and cannot authorize an attack.
    d.fact.visible=false;d.fact.alive=true;d.fact.health_actual=d.fact.shield_actual=true;
    d.fact.health=99;d.life_read=false;CHECK(Facts::publish_target(s,d));live=ep::target(s,basic(502));
    CHECK(live&&!live->actual&&!live->visible&&!live->health_actual&&!live->shield_actual);
    CHECK(!ep::authorize(s,basic(502)));CHECK(ep::authorize(s,basic()));
    // Inactive stale payloads are never promoted by increasing observations.
    s.targets[5]=camp(999).fact;s.targets[5].actual=true;++s.stamp.observation;
    CHECK(!ep::authorize(s,basic(999)));CHECK(s.target_count==2);
    for(unsigned n=2;n<ep::target_capacity;++n){auto extra=camp(600+n);CHECK(Facts::publish_target(s,extra));}
    CHECK(s.target_count==ep::target_capacity);CHECK(!Facts::publish_target(s,camp(700)));
}

static void move_transactions(){
    ep::Snapshot s{};CHECK(Facts::publish_owner(s,owner(),stamp()));
    auto d=step();d.geometry_read=false;CHECK(!Facts::publish_move(s,d));CHECK(s.move_count==0);
    d.geometry_read=true;d.visibility_read=false;CHECK(!Facts::publish_move(s,d));CHECK(s.move_count==0);
    d.visibility_read=true;d.visible=false;CHECK(Facts::publish_move(s,d));
    CHECK(!s.moves[0].actual&&!s.moves[0].open&&!ep::authorize(s,moving()));
    d.visible=true;d.champion_read=false;CHECK(!Facts::publish_move(s,d));CHECK(!s.moves[0].actual);
    d.champion_read=true;d.camp_read=false;CHECK(!Facts::publish_move(s,d));
    d.camp_read=true;d.teleport_read=false;CHECK(!Facts::publish_move(s,d));
    d.teleport_read=true;CHECK(Facts::publish_move(s,d));CHECK(ep::authorize(s,moving()));
    for(unsigned blocker=0;blocker<3;++blocker){auto blocked=d;
        blocked.champion=blocker==0;blocked.camp=blocker==1;blocked.teleport=blocker==2;
        CHECK(Facts::publish_move(s,blocked));CHECK(s.moves[0].actual&&!s.moves[0].open);CHECK(!ep::authorize(s,moving()));}
    auto unreachable=step({4,5});unreachable.fact.reachable=false;unreachable.fact.open=true;
    unreachable.visibility_read=unreachable.champion_read=unreachable.camp_read=unreachable.teleport_read=false;
    CHECK(Facts::publish_move(s,unreachable));CHECK(s.moves[1].actual&&!s.moves[1].reachable&&!s.moves[1].open);
    CHECK(!ep::authorize(s,moving({4,5})));
}

static void optional_reads_keep_incumbent(){
    auto s=ready();ep::Lease lease;CHECK(lease.issue(s,100));const auto token=lease.token();
    const auto incumbent=policy(s,basic());CHECK(ep::valid_policy(s,incumbent));
    auto optional=step({4,5});optional.teleport_read=false;
    ++s.stamp.observation;CHECK(!Facts::publish_move(s,optional));CHECK(lease.observe(s));
    CHECK(ep::token_matches(token,s)&&lease.token().lease==100);CHECK(ep::authorize(s,basic()));
    auto unknown=camp(602);unknown.fact.visible=false;unknown.life_read=unknown.health_read=false;
    ++s.stamp.observation;CHECK(Facts::publish_target(s,unknown));CHECK(lease.observe(s));CHECK(ep::valid_policy(s,incumbent));
    CHECK(s.targets[0].id==501&&s.targets[0].actual&&s.targets[0].alive);
    // Read-only owner packet refresh preserves both authority and target scopes.
    auto d=owner();++s.stamp.observation;CHECK(Facts::publish_owner(s,d,s.stamp));CHECK(lease.observe(s));
    CHECK(ep::valid_policy(s,incumbent));CHECK(s.target_count==2&&s.move_count==1);
    auto alternative=policy(s,moving());ep::Comparison comparison{};comparison.optional_funded=comparison.joint_priced=true;
    comparison.alternative.state=ep::ValueState::conditional;comparison.alternative.value=10000;
    comparison.alternative.key={s.stamp,11,22,6};
    CHECK(ep::choose(s,incumbent,alternative,comparison)==ep::Choice::incumbent);
    ep::Valuation value{};CHECK(value.state==ep::ValueState::unknown&&value.assurance==ep::Assurance::none);
    // A fresh owner contradiction is relevant, not an optional menu failure.
    auto changed=s;changed.pose={7,5};++changed.stamp.observation;CHECK(!lease.observe(changed));CHECK(!lease.valid());
    CHECK(!lease.issue(changed,101)); // a new nonce alone cannot reissue authority.
    ++changed.stamp.effect;CHECK(lease.issue(changed,101));
    changed.authority.restrictions=1;++changed.stamp.observation;CHECK(!lease.observe(changed));CHECK(!lease.valid());
}

static void every_attempt_revokes(){
    for(bool accepted:{false,true}){
        auto s=ready();ep::Lease lease;CHECK(lease.issue(s,10));const auto old=lease.token();const auto before=s.stamp;
        s.ground_count=1;s.grounds[0]={{6,6},true,true,true,true};s.abilities[0].actual=s.abilities[0].cooldown_actual=true;
        s.gems_actual=s.bank_target_actual=s.observation_actual=s.capture_actual=s.channel_timer_actual=true;
        CHECK(lease.before_attempt(s));CHECK(!lease.valid());CHECK(s.stamp.host_attempt==before.host_attempt+1);
        CHECK(s.stamp.epoch==before.epoch&&s.stamp.frame==before.frame&&s.stamp.effect==before.effect);
        Facts::after_attempt(s);CHECK(!s.authority_lease&&!s.body_actual&&!s.restrictions_actual&&!s.channel_actual);
        CHECK(!s.health_actual&&!s.shield_actual&&s.target_count==0&&s.move_count==0&&s.ground_count==0);
        CHECK(!s.abilities[0].actual&&!s.abilities[0].cooldown_actual);
        CHECK(!s.gems_actual&&!s.bank_target_actual&&!s.observation_actual&&!s.capture_actual&&!s.channel_timer_actual);
        CHECK(!ep::token_matches(old,s)&&!ep::authorize(s,basic())&&!ep::authorize(s,moving())&&!ep::authorize(s,recall()));
        CHECK(!lease.issue(s,11));
        auto actual=owner();if(accepted){++s.stamp.effect;actual.authority.raw_a=false;}
        s.stamp.observation+=3;CHECK(Facts::publish_owner(s,actual,s.stamp));CHECK(lease.issue(s,11));
        CHECK(s.stamp.frame==before.frame&&lease.token().stamp.host_attempt==before.host_attempt+1);
        CHECK(!ep::authorize(s,basic())&&!ep::authorize(s,moving()));
        CHECK(Facts::publish_target(s,camp()));CHECK(Facts::publish_move(s,step()));
        CHECK(ep::authorize(s,basic())==!accepted);CHECK(ep::authorize(s,moving()));
        const auto refreshed=lease.token();++s.stamp.observation;CHECK(lease.observe(s));CHECK(ep::token_matches(refreshed,s));
        lease.revoke();++s.stamp.observation;CHECK(!lease.issue(s,12));
        ++s.stamp.epoch;++s.stamp.frame;CHECK(lease.issue(s,12));
    }
}

static void native_false_is_a_receipt(){
    auto after=ready();ep::Lease lease;CHECK(lease.issue(after,30));
    const auto before=after.stamp;const auto pose=after.pose;const auto authority=after.authority;
    CHECK(lease.before_attempt(after));Facts::after_attempt(after);
    after.stamp.observation+=3;CHECK(Facts::publish_owner(after,owner(),after.stamp));
    CHECK(lease.issue(after,31));CHECK(after.move_count==0&&after.target_count==0);
    auto incomplete=step();incomplete.champion_read=incomplete.camp_read=incomplete.teleport_read=false;
    CHECK(!Facts::publish_move(after,incomplete));CHECK(after.move_count==0);
    // The native FALSE plus fresh unchanged body is its own blocker receipt;
    // it does not pretend that four scalar tile reads or an identity were read.
    CHECK(Facts::publish_refused_move(after,{5,6},before,pose,authority,true,true,0));
    CHECK(after.move_count==1&&after.moves[0].actual&&after.moves[0].reachable&&!after.moves[0].open);
    CHECK(after.target_count==0&&!ep::authorize(after,moving()));CHECK(lease.valid());
    CHECK(Facts::publish_refused_move(after,{5,6},before,pose,authority,true,true,0));CHECK(after.move_count==1);
    for(unsigned failure=0;failure<12;++failure){
        auto wrong=after;wrong.move_count=0;auto prior=before;bool attempted=true,refused=true;int code=0;
        switch(failure){
        case 0:attempted=false;break;case 1:refused=false;break;case 2:code=97;break;
        case 3:wrong.body_actual=false;break;case 4:wrong.restrictions_actual=false;break;case 5:wrong.channel_actual=false;break;
        case 6:wrong.pose.x+=1;break;case 7:wrong.authority.raw_m=false;break;
        case 8:++wrong.stamp.epoch;break;case 9:++wrong.stamp.frame;break;case 10:++wrong.stamp.effect;break;
        case 11:wrong.stamp.host_attempt=prior.host_attempt;break;
        }
        CHECK(!Facts::publish_refused_move(wrong,{5,6},prior,pose,authority,attempted,refused,code));CHECK(wrong.move_count==0);
    }
    auto jumped=after;jumped.move_count=0;++jumped.stamp.host_attempt;
    CHECK(!Facts::publish_refused_move(jumped,{5,6},before,pose,authority,true,true,0));
    auto rolled_back=after;rolled_back.move_count=0;rolled_back.stamp.observation=before.observation-1;
    CHECK(!Facts::publish_refused_move(rolled_back,{5,6},before,pose,authority,true,true,0));CHECK(rolled_back.move_count==0);
    auto exhausted=before;exhausted.host_attempt=ep::invalid_cost;
    CHECK(!Facts::publish_refused_move(jumped,{5,6},exhausted,pose,authority,true,true,0));
}

static void exact_query_classes(){
    using Request=Pure::ExecutableObservationRequest;using Phase=Pure::ExecutableMacroPhase;
    Request request{};
    auto bounds=Pure::executable_observe_query_bounds(basic(),request);
    CHECK(bounds.validation==2&&bounds.feedback==5&&bounds.refusal_close==0); // owner3 + native camp state/HP2
    request.member=0;request.have_move=true;request.move={5,6};
    CHECK(Pure::executable_observe_prepare_query_bound(request)==9); // owner3 + camp2 + local tile4
    CHECK(Pure::executable_observe_prepare_query_bound(request,false)==6);
    bounds=Pure::executable_observe_query_bounds(basic(),request);CHECK(bounds.validation==0&&bounds.feedback==5);
    request.need_group=request.need_bag=request.need_home=request.need_bank_receipt=true;
    bounds=Pure::executable_observe_query_bounds(basic(),request);
    CHECK(bounds.feedback==5&&bounds.refusal_close==0); // unrelated request fields do not inflate post-Basic feedback
    request.need_target_shield=true;bounds=Pure::executable_observe_query_bounds(basic(),request);CHECK(bounds.feedback==6);
    bounds=Pure::executable_observe_query_bounds(moving(),request);CHECK(bounds.validation==4&&bounds.feedback==3&&bounds.refusal_close==0);
    auto bank=recall();bank.kind=ep::ActionKind::bank;bounds=Pure::executable_observe_query_bounds(bank,request);
    CHECK(bounds.validation==2&&bounds.feedback==4&&bounds.refusal_close==0);
    bounds=Pure::executable_observe_query_bounds(recall(),request);CHECK(bounds.validation==0&&bounds.feedback==4);
    Request enemy{};enemy.have_enemy=true;enemy.enemy=701;
    auto attack=basic(701);attack.target_kind=ep::TargetKind::champion;
    bounds=Pure::executable_observe_query_bounds(attack,enemy);CHECK(bounds.validation==0&&bounds.feedback==4);
    attack.target_id=702;bounds=Pure::executable_observe_query_bounds(attack,enemy);CHECK(bounds.validation==1&&bounds.feedback==4);
    CHECK(Pure::executable_observe_prepare_query_bound(enemy)==4);
    ep::Action wait{};wait.kind=ep::ActionKind::wait;wait.wait_reason=ep::WaitReason::channel;
    Request channel{};bounds=Pure::executable_observe_query_bounds(wait,channel);CHECK(bounds.validation==1&&bounds.feedback==0);
    channel.phase=Phase::bank_channel;bounds=Pure::executable_observe_query_bounds(wait,channel);CHECK(bounds.validation==0&&bounds.feedback==0);
    CHECK(Pure::executable_observe_prepare_query_bound(channel)==4);
    channel.phase=Phase::recall_channel;bounds=Pure::executable_observe_query_bounds(wait,channel);CHECK(bounds.validation==0);
    wait.wait_reason=ep::WaitReason::capture;channel.need_group=false;
    bounds=Pure::executable_observe_query_bounds(wait,channel);CHECK(bounds.validation==3&&bounds.feedback==0);
    channel.need_group=true;bounds=Pure::executable_observe_query_bounds(wait,channel);CHECK(bounds.validation==0);
}

static bool sequential(std::uint64_t fuel,unsigned queries,std::uint64_t query,std::uint64_t compute,std::uint64_t tail){
    if(!query||!compute||!tail)return false;
    for(unsigned n=0;n<queries;++n){if(fuel<=query)return false;fuel-=query;}
    if(fuel<=compute)return false;fuel-=compute;
    if(fuel<=tail)return false;return true;
}
static void whole_query_cones(){
    constexpr auto max=std::numeric_limits<std::uint64_t>::max();
    for(unsigned q=0;q<=12;++q)for(std::uint64_t tail:{2000u,20000u,32000u})
        for(std::uint64_t fuel=1000;fuel<=100000;fuel+=137){
            CHECK(Cone::admitted(fuel,q,2000,6000,tail)==sequential(fuel,q,2000,6000,tail));
            CHECK(Cone::cost(q,2000,6000,tail)==q*2000+6000+tail);
        }
    CHECK(Cone::cost(3,2000,6000,20000)==32000);CHECK(!Cone::admitted(32000,3,2000,6000,20000));
    CHECK(Cone::admitted(32001,3,2000,6000,20000));
    std::uint64_t fuel=32001;Owner draft=owner();draft.body_read=draft.restrictions_read=draft.buffs_read=false;
    ep::Snapshot s{};ep::Lease lease;
    fuel-=2000;draft.body_read=true;CHECK(!Facts::publish_owner(s,draft,stamp()));CHECK(!lease.issue(s,1));
    fuel-=2000;draft.restrictions_read=true;CHECK(!Facts::publish_owner(s,draft,stamp()));CHECK(!lease.issue(s,1));
    // A failed final read is not a complete actor packet or a completed action.
    fuel-=2000;CHECK(!Facts::publish_owner(s,draft,stamp()));CHECK(!lease.issue(s,1));CHECK(fuel>6000+20000);
    draft.buffs_read=true;CHECK(Facts::publish_owner(s,draft,stamp()));CHECK(lease.issue(s,1));
    fuel-=6000;CHECK(fuel==20001);
    CHECK(Cone::cost(1,0,6000,2000)==max&&Cone::cost(1,2000,0,2000)==max&&Cone::cost(1,2000,6000,0)==max);
    CHECK(Cone::cost(2,max/2+1,1,1)==max);CHECK(Cone::cost(1,max,1,1)==max);
    CHECK(Cone::cost(0,1,max,1)==max);CHECK(!Cone::admitted(max,1,max,1,1));
}

static void shared_preparation_credit(){
    const auto physical=stamp();Preparation ledger{};
    constexpr std::uint64_t query_base=10000,entry=100000;
    ledger.begin(physical,entry,query_base,6000);CHECK(ledger.matches(physical));
    CHECK(ledger.spent(physical,entry,query_base)==0&&ledger.remaining(physical,entry,query_base)==6000);
    // The three owner imports actually cost 4500, not their 6000 bound.
    // Only that actual imported cost is excluded from the single pure budget.
    auto readonly=physical;readonly.observation+=3;
    std::uint64_t fuel=entry-4500-1200,queries=query_base+4500;
    CHECK(ledger.matches(readonly));CHECK(ledger.spent(readonly,fuel,queries)==1200);
    CHECK(ledger.remaining(readonly,fuel,queries)==4800);
    fuel-=1536; // parent nomination/selection is pure work between SDK phases
    CHECK(ledger.spent(readonly,fuel,queries)==2736);CHECK(ledger.remaining(readonly,fuel,queries)==3264);
    const auto suffix=ledger.remaining(readonly,fuel,queries);
    CHECK(Cone::cost(2,2000,suffix,85000)==92264);CHECK(Cone::admitted(fuel,2,2000,suffix,85000));
    CHECK(!Cone::admitted(fuel,2,2000,6000,85000)); // no second preparation allowance
    // A partial read and a failed admission leave the same unpaid suffix.
    ep::Snapshot packet{};auto incomplete=owner();incomplete.buffs_read=false;
    CHECK(!Facts::publish_owner(packet,incomplete,readonly));CHECK(!packet.body_actual);
    CHECK(!Cone::admitted(90000,2,2000,suffix,85000));
    CHECK(ledger.remaining(readonly,fuel,queries)==suffix);
    // A failed host query still has a real import cost; it refunds no pure work.
    fuel-=1500;queries+=1500;++readonly.observation;
    CHECK(ledger.remaining(readonly,fuel,queries)==3264);
    fuel-=50;CHECK(ledger.remaining(readonly,fuel,queries)==3214);
    fuel-=1500+3214;queries+=1500;
    CHECK(ledger.spent(readonly,fuel,queries)==6000);CHECK(ledger.remaining(readonly,fuel,queries)==0);
    CHECK(!Cone::admitted(fuel,0,2000,ledger.remaining(readonly,fuel,queries),1000));
    CHECK(ledger.remaining(readonly,fuel-1,queries)==ep::invalid_cost);
    // Success and FALSE both consume physical authority, regardless of effect.
    for(bool accepted:{false,true}){auto after=readonly;++after.host_attempt;if(accepted)++after.effect;
        CHECK(!ledger.matches(after));CHECK(ledger.remaining(after,fuel,queries)==ep::invalid_cost);}
    for(unsigned changed=0;changed<4;++changed){auto after=readonly;
        if(changed==0)++after.epoch;else if(changed==1)++after.frame;
        else if(changed==2)++after.effect;else ++after.host_attempt;
        CHECK(ledger.spent(after,fuel,queries)==ep::invalid_cost);}
    ledger.close();CHECK(!ledger.matches(physical));CHECK(ledger.remaining(readonly,fuel,queries)==ep::invalid_cost);

    // Subtractions reject rolled-back counters and impossible import totals;
    // high counters are valid only when all deltas remain representable.
    ledger.begin(physical,entry,query_base,6000);
    CHECK(ledger.spent(physical,entry+1,query_base)==ep::invalid_cost);
    CHECK(ledger.spent(physical,entry,query_base-1)==ep::invalid_cost);
    CHECK(ledger.spent(physical,entry-1,query_base+2)==ep::invalid_cost);
    CHECK(ledger.remaining(physical,entry-6001,query_base)==ep::invalid_cost);
    CHECK(ledger.remaining(physical,entry-6000,query_base)==0);
    constexpr auto bad=ep::invalid_cost;
    for(unsigned invalid=0;invalid<4;++invalid){
        ledger.begin(physical,invalid==0?bad:entry,invalid==1?bad:query_base,invalid==2?bad:invalid==3?0:6000);
        CHECK(!ledger.active);CHECK(ledger.spent(physical,entry,query_base)==bad);
    }
    ledger.begin(physical,0,0,6000);CHECK(ledger.spent(physical,0,0)==0);
    CHECK(!Cone::admitted(0,0,2000,ledger.remaining(physical,0,0),1));
    ledger.begin(physical,bad-1,bad-20,6000);
    CHECK(ledger.spent(physical,bad-10,bad-15)==4);CHECK(ledger.remaining(physical,bad-10,bad-15)==5996);
    CHECK(ledger.spent(physical,bad-10,bad-1)==bad);
    CHECK(ledger.spent(physical,bad-30,bad)==bad); // overflow sentinel cannot masquerade as delta20
    CHECK(ledger.remaining(physical,bad-30,bad)==bad);
    CHECK(ledger.spent(physical,bad,bad-20)==bad);
}

static void new_input_invalidates_outer_packet(){
    struct Packet {
        ep::Snapshot snapshot;ep::Token token;
        bool owner_actual{},group_actual{},submitted_actual{},home_actual{},retained_incumbent{};
    };
    Packet packet{};packet.snapshot=ready();auto& s=packet.snapshot;ep::Lease lease;CHECK(lease.issue(s,90));
    packet.token=lease.token();packet.owner_actual=packet.group_actual=packet.submitted_actual=
        packet.home_actual=packet.retained_incumbent=true;
    s.ground_count=1;s.grounds[0]={{6,6},true,true,true,true};
    for(auto& ability:s.abilities)ability.actual=ability.cooldown_actual=true;
    s.gems_actual=s.bank_target_actual=s.observation_actual=s.capture_actual=s.channel_timer_actual=true;
    s.gems=30;s.channel_ticks=2;s.authority.channel=ep::Channel::bank;
    const auto before=s.stamp;const auto old_token=packet.token;
    // A finite Wait reaches the next input without any native host attempt.
    ++s.stamp.epoch;++s.stamp.frame;++s.stamp.observation;
    Facts::invalidate_packet(packet);
    CHECK(s.stamp.host_attempt==before.host_attempt&&s.stamp.effect==before.effect);
    CHECK(s.stamp.epoch==before.epoch+1&&s.stamp.frame==before.frame+1);
    CHECK(!packet.token.valid&&!packet.owner_actual&&!packet.group_actual&&!packet.submitted_actual&&
        !packet.home_actual&&!packet.retained_incumbent);
    CHECK(!s.authority_lease&&!s.body_actual&&!s.restrictions_actual&&!s.channel_actual&&!s.health_actual&&!s.shield_actual);
    CHECK(!s.target_count&&!s.move_count&&!s.ground_count);
    for(const auto& ability:s.abilities)CHECK(!ability.actual&&!ability.cooldown_actual);
    CHECK(!s.gems_actual&&!s.bank_target_actual&&!s.observation_actual&&!s.capture_actual&&!s.channel_timer_actual);
    CHECK(!ep::token_matches(old_token,s)&&!ep::authorize(s,basic())&&!ep::authorize(s,moving())&&!ep::authorize(s,recall()));
    // Inactive payload may remain; a new owner receipt cannot restore its scopes.
    CHECK(s.gems==30&&s.channel_ticks==2);CHECK(Facts::publish_owner(s,owner(),s.stamp));CHECK(lease.issue(s,91));
    CHECK(!packet.group_actual&&!packet.submitted_actual&&!packet.home_actual&&!packet.retained_incumbent);
    CHECK(!s.target_count&&!s.move_count&&!s.gems_actual&&!s.capture_actual);
    CHECK(!ep::authorize(s,basic())&&!ep::authorize(s,moving()));
    Facts::invalidate_packet(packet);Facts::invalidate_packet(packet);
    CHECK(!packet.token.valid&&!packet.owner_actual&&!packet.group_actual&&!packet.submitted_actual&&!packet.home_actual&&!packet.retained_incumbent);
}

static void already_paid_compiler_intervals(){
    const auto physical=stamp();auto readonly=physical;readonly.observation+=7;
    Preparation ledger{};ledger.begin(physical,100000,20000,6000);
    // Three owner imports cost 4500 and owner preparation costs 1200.
    CHECK(ledger.spent(readonly,94300,24500)==1200);
    // The separately funded compiler consumed this latched 4000 interval.
    // Another 800 of parent work is deliberately outside that paid interval.
    CHECK(ledger.credit(readonly,94300,90300,89500,1,true));
    CHECK(ledger.compile_paid==4000&&ledger.last_serial==1);
    CHECK(ledger.known_credit(readonly,94300,90300,1));
    CHECK(!ledger.credit(readonly,94300,90300,89500,1,true));
    CHECK(ledger.known_credit(readonly,94300,90300,1)&&ledger.compile_paid==4000);
    CHECK(ledger.spent(readonly,89500,24500)==2000);CHECK(ledger.remaining(readonly,89500,24500)==4000);
    CHECK(ledger.credit(readonly,89500,86500,86000,2,true));
    CHECK(ledger.compile_paid==7000&&ledger.last_serial==2);
    CHECK(!ledger.known_credit(readonly,94300,90300,1));CHECK(ledger.known_credit(readonly,89500,86500,2));
    CHECK(!ledger.known_credit(readonly,89501,86500,2)&&!ledger.known_credit(readonly,89500,86501,2));
    CHECK(ledger.spent(readonly,86000,24500)==2500);CHECK(ledger.remaining(readonly,86000,24500)==3500);
    // Publishing a ready incumbent and rewriting its optional suffix do not
    // start another preparation transaction or restore its consumed credit.
    auto packet=ready();ep::Lease lease;CHECK(lease.issue(packet,101));
    auto incumbent=policy(packet,basic());CHECK(ep::valid_policy(packet,incumbent));
    auto rewritten=policy(packet,moving());CHECK(ep::valid_policy(packet,rewritten));
    CHECK(ledger.active&&ledger.matches(readonly));
    CHECK(ledger.spent(readonly,84000,24500)==4500);CHECK(ledger.remaining(readonly,84000,24500)==1500);
    CHECK(!Cone::admitted(84000,1,2000,6000,80000));
    CHECK(Cone::admitted(84000,1,2000,1500,80000));
    // Actual suffix import2000 + pure1500 reaches the original limit exactly.
    CHECK(ledger.spent(readonly,80500,26500)==6000);CHECK(ledger.remaining(readonly,80500,26500)==0);
    CHECK(ledger.finish(readonly,80500,26500));CHECK(!ledger.active&&!ledger.failed);
    CHECK(!ledger.credit(readonly,80000,79000,79000,3,true));

    // Rejected credits cannot mutate a previously accepted receipt or subtract
    // its cost again. Each failure starts with the same legitimate first span.
    for(unsigned failure=0;failure<16;++failure){
        Preparation trial{};trial.begin(physical,100000,0,6000);
        CHECK(trial.credit(physical,99000,97000,96000,10,true));
        auto tag=readonly;std::uint64_t before=95000,after=94000,now=93000,serial=11;bool paid=true;
        switch(failure){
        case 0:paid=false;break;case 1:serial=0;break;case 2:serial=10;break;case 3:serial=9;break;
        case 4:before=100001;break;case 5:after=before+1;break;case 6:now=after+1;break;
        case 7:before=98000;after=97500;now=97000;break; // overlaps previous [99000,97000]
        case 8:++tag.epoch;break;case 9:++tag.frame;break;case 10:++tag.effect;break;case 11:++tag.host_attempt;break;
        case 12:before=ep::invalid_cost;break;case 13:now=ep::invalid_cost;break;
        case 14:after=ep::invalid_cost;break;case 15:serial=ep::invalid_cost;break;
        }
        CHECK(!trial.credit(tag,before,after,now,serial,paid));
        CHECK(trial.compile_paid==2000&&trial.last_serial==10);
        CHECK(trial.spent(physical,93000,0)==5000);
        CHECK(trial.credit(readonly,97000,96000,93000,11,true)); // adjacent spans do not overlap
        CHECK(trial.compile_paid==3000&&trial.spent(readonly,93000,0)==4000);
        CHECK(!trial.credit(readonly,99000,97000,93000,12,true)); // new serial cannot double-subtract old interval
        CHECK(trial.compile_paid==3000);
    }
    Preparation inactive{};CHECK(!inactive.credit(physical,100,90,80,1,true));

    Preparation overrun{};overrun.begin(physical,100000,0,6000);
    CHECK(overrun.credit(readonly,99000,95000,90000,1,true));
    CHECK(overrun.spent(readonly,89999,0)==6001);
    CHECK(!overrun.finish(readonly,89999,0));CHECK(overrun.failed&&!overrun.active);
    overrun.begin(readonly,89999,0,6000);CHECK(!overrun.active);
    CHECK(overrun.remaining(readonly,89999,0)==ep::invalid_cost);
    CHECK(!overrun.credit(readonly,89000,88000,87000,2,true));
    // A new actual effect or new owner input can legitimately begin afresh.
    auto changed=readonly;++changed.effect;overrun.begin(changed,89999,0,6000);
    CHECK(overrun.active&&!overrun.failed&&overrun.compile_paid==0&&overrun.last_serial==0);
    CHECK(overrun.remaining(changed,89999,0)==6000);overrun.close();
    ++changed.epoch;++changed.frame;overrun.begin(changed,80000,1000,6000);
    CHECK(overrun.matches(changed)&&overrun.remaining(changed,80000,1000)==6000);
    // Paid imports and paid compiler spans cannot both deduct the same elapsed
    // time; a negative remainder is malformed, not extra preparation credit.
    Preparation double_paid{};double_paid.begin(physical,100000,0,6000);
    CHECK(double_paid.credit(physical,100000,95000,95000,1,true));
    CHECK(double_paid.spent(physical,95000,1)==ep::invalid_cost);
    CHECK(!double_paid.finish(physical,95000,1));CHECK(double_paid.failed);
    constexpr auto bad=ep::invalid_cost;Preparation large{};
    large.begin(physical,bad-1,0,bad-1);
    CHECK(large.credit(physical,bad-1,1,0,bad-2,true));CHECK(large.compile_paid==bad-2);
    CHECK(large.credit(physical,1,0,0,bad-1,true));CHECK(large.compile_paid==bad-1);
    CHECK(large.spent(physical,0,0)==0);CHECK(!large.credit(physical,1,0,0,bad,true));
    Preparation empty_interval{};empty_interval.begin(physical,100000,0,6000);
    CHECK(empty_interval.credit(physical,99000,99000,98000,1,true));CHECK(empty_interval.compile_paid==0);
    CHECK(empty_interval.spent(physical,98000,0)==2000);CHECK(empty_interval.known_credit(readonly,99000,99000,1));
}

static void classification_is_not_operator_authority(){
    using Tag=Pure::ExecutableObservationClassificationTag;
    const auto physical=stamp();Tag tag{};CHECK(!tag.current(physical));
    tag.stamp=physical;tag.actual=true;CHECK(tag.current(physical));
    auto readonly=physical;readonly.observation+=9;CHECK(tag.current(readonly));
    for(unsigned changed=0;changed<4;++changed){auto other=readonly;
        if(changed==0)++other.epoch;else if(changed==1)++other.frame;
        else if(changed==2)++other.effect;else ++other.host_attempt;
        CHECK(!tag.current(other));}
    tag.actual=false;CHECK(!tag.current(physical)&&!tag.current(readonly));tag.actual=true;
    // A raw body can classify an owner while restrictions and buffs remain
    // unread. Even a current classification plus an actual target is no lease.
    auto raw=owner();raw.restrictions_read=raw.buffs_read=false;
    ep::Snapshot packet{};ep::Lease lease;CHECK(Facts::publish_target(packet,camp()));
    CHECK(tag.current(readonly));CHECK(!Facts::publish_owner(packet,raw,readonly));
    CHECK(!packet.body_actual&&!packet.restrictions_actual&&!packet.channel_actual);
    CHECK(!lease.issue(packet,200)&&!lease.valid());CHECK(!ep::authorize(packet,basic()));
    raw.restrictions_read=true;CHECK(!Facts::publish_owner(packet,raw,readonly));
    CHECK(!lease.issue(packet,200)&&!ep::authorize(packet,basic()));
    raw.buffs_read=true;CHECK(Facts::publish_owner(packet,raw,readonly));
    CHECK(lease.issue(packet,200)&&ep::authorize(packet,basic()));
}

int main(){
    owner_transaction();target_transactions();move_transactions();optional_reads_keep_incumbent();every_attempt_revokes();
    native_false_is_a_receipt();exact_query_classes();whole_query_cones();
    shared_preparation_credit();new_input_invalidates_outer_packet();
    already_paid_compiler_intervals();
    classification_is_not_operator_authority();
    std::printf("executable-observation: %u checks passed\n",checks);return 0;
}
