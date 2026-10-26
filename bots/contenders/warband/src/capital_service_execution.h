#pragma once
#include "capital_service_forecast.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>

// Actual, single-body interpreter for a selected one-group forecast Program.
// A Directive is an intention, never an SDK permission, risk quote or lease.
// The native caller must freshly admit its complete suffix before attempting
// it, and invalidate this interpreter on a changed reply/model premise.
namespace wb::capital::service_execution {
namespace f=service_forecast;
struct Stamp {int owner{-1},round{-1};std::uint64_t input{},sample{},effect{};
    friend bool operator==(const Stamp&,const Stamp&)=default;};
enum class Evidence {Unknown,ActualDirect,SupportedConservation};
enum class ReceiptKind {GroupPayout,BankPayout,Purchase,Healing,CustodyLost,Interrupted,Death};
struct Receipt {
    std::uint64_t event{},program{},source_directive{};Stamp observed{};ReceiptKind kind{};Evidence evidence{Evidence::Unknown};
    int group{-1};unsigned generation{};bool source_known{},amount_known{};
    unsigned gems{},source_bag{},item{},count{};double gold{},cost{};float healing_amount{};
};
struct Frame {
    Stamp stamp{};f::Body body{};f::Group group{};unsigned npcs{};
    std::array<f::Npc,f::npc_limit> camps{};std::span<const Receipt> receipts{};
    bool observed{},fresh{},permissions_known{},roster_complete{};
    bool in_group_known{},in_group{},on_spawn_known{},on_spawn{};
};
enum class State {Empty,Active,Complete,Handoff};
enum class Kind {Physical,Preparation,Wait,Complete,Handoff};
enum class Reason {None,Invalid,Binding,Capacity,JointUnsupported,RecallUnsupported,HealUnsupported,
    Date,Stale,ChangedPremise,Death,UnknownObservation,UnknownRoster,UnknownPresence,UnknownSpawn,
    AwaitAttempt,AwaitFresh,ActionLimit,Stunned,Channel,Permissions,Route,Range,Target,Refused,
    UnknownAttempt,Readback,Receipt,MissingPayout,CustodyLost,Interrupted,Purchase,UnknownDamage,
    Generation,Unfinished,TransitionLimit};
enum class Attempt {Accepted,Refused,Unknown};
struct Binding {
    Stamp source{};int group{-1};unsigned generation{};
    std::uint64_t program{},initial_world{},domain{};int horizon{-1};
};
struct Directive {
    Kind kind{Kind::Preparation};Reason reason{Reason::UnknownObservation};f::Stage stage{f::Stage::Work};
    Stamp source{};std::uint64_t id{};f::Primitive primitive{};f::Purchase purchase{};
};
struct Facts {
    bool clear_observed{},capture_attributed{},source_released{},bank_attributed{},recovered_actual_state{},healing_observed{},renewed{};
    unsigned captured_gems{},banked_gems{},purchases{};double captured_gold{},spent_gold{},work_damage{},renewal_damage{};
    std::uint64_t capture_event{},bank_event{},healing_event{};
};
namespace detail {
inline bool finite(double x){return std::isfinite(x)&&x>=0;}
inline void mix(std::uint64_t& h,std::uint64_t x){h=(h^x)*1099511628211ull;}
inline std::uint64_t scalar(float x){return std::bit_cast<std::uint32_t>(x);}
inline bool same_body(const f::Body& a,const f::Body& b){
    return a.id==b.id&&a.kit==b.kit&&a.pose==b.pose&&a.hp==b.hp&&a.max_hp==b.max_hp&&a.strength==b.strength&&a.focus==b.focus&&a.shield==b.shield&&
        a.gold==b.gold&&a.gold_known==b.gold_known&&a.bag==b.bag&&a.bag_known==b.bag_known&&a.capital_floor==b.capital_floor&&a.gold_multiplier==b.gold_multiplier&&
        a.camp_multiplier==b.camp_multiplier&&a.taken_multiplier==b.taken_multiplier&&a.npc_taken_multiplier==b.npc_taken_multiplier&&
        a.shield_present==b.shield_present&&a.shield_ticks==b.shield_ticks&&a.stun_present==b.stun_present&&a.stun_ticks==b.stun_ticks&&a.alive==b.alive&&
        a.m==b.m&&a.a==b.a&&a.c==b.c&&a.walked==b.walked&&a.cast_used==b.cast_used&&a.completed==b.completed&&a.actual==b.actual&&
        a.move_range==b.move_range&&a.attack_range==b.attack_range&&a.cooldown==b.cooldown&&a.reset_slot==b.reset_slot&&a.shield_slot==b.shield_slot&&a.heal_slot==b.heal_slot&&
        a.shield_modifier==b.shield_modifier&&a.heal_modifier==b.heal_modifier&&a.splash_radius==b.splash_radius&&a.splash_radius_known==b.splash_radius_known&&
        a.shield_duration==b.shield_duration&&a.heal_range==b.heal_range&&a.items==b.items&&a.channel==b.channel&&a.channel_ticks==b.channel_ticks&&a.effective_recall_ticks==b.effective_recall_ticks;
}
inline bool same_npc(const f::Npc& a,const f::Npc& b){return a.id==b.id&&a.pose==b.pose&&a.hp==b.hp&&a.max_hp==b.max_hp&&a.strength==b.strength&&a.shield==b.shield&&a.range==b.range&&
    a.awake==b.awake&&a.actual==b.actual&&a.shield_present==b.shield_present&&a.shield_ticks==b.shield_ticks&&a.unsupported_effects==b.unsupported_effects;}
inline std::uint64_t receipt_hash(const Receipt& r){
    std::uint64_t h=1469598103934665603ull;for(auto x:{r.event,r.program,r.source_directive,std::uint64_t(r.kind),std::uint64_t(r.evidence),
        std::uint64_t(r.observed.owner+1),std::uint64_t(r.observed.round+1),r.observed.input,r.observed.sample,r.observed.effect,
        std::uint64_t(r.group+1),std::uint64_t(r.generation),std::uint64_t(r.source_known),std::uint64_t(r.amount_known),
        std::uint64_t(r.gems),std::uint64_t(r.source_bag),std::uint64_t(r.item),std::uint64_t(r.count),std::bit_cast<std::uint64_t>(r.gold),
        std::bit_cast<std::uint64_t>(r.cost),scalar(r.healing_amount)}){mix(h,x);}return h;
}
inline std::uint64_t frame_hash(const Frame& x){
    std::uint64_t h=1469598103934665603ull;const auto& b=x.body;
    for(auto v:{std::uint64_t(b.id+1),std::uint64_t(b.kit),std::uint64_t(b.pose.x+1),std::uint64_t(b.pose.y+1),scalar(b.hp),scalar(b.max_hp),scalar(b.strength),scalar(b.focus),scalar(b.shield),
        std::uint64_t(b.bag),std::bit_cast<std::uint64_t>(b.gold),std::uint64_t(b.bag_known),std::uint64_t(b.gold_known),
        std::uint64_t(b.alive),std::uint64_t(b.m),std::uint64_t(b.a),std::uint64_t(b.c),std::uint64_t(b.walked),std::uint64_t(b.cast_used),
        std::uint64_t(b.completed),std::uint64_t(b.stun_present),std::uint64_t(b.stun_ticks),std::uint64_t(b.shield_present),std::uint64_t(b.shield_ticks),
        std::uint64_t(b.channel),std::uint64_t(b.channel_ticks),std::uint64_t(b.move_range),std::uint64_t(b.attack_range),
        std::uint64_t(b.cooldown[0]),std::uint64_t(b.cooldown[1]),std::uint64_t(x.group.id+1),std::uint64_t(x.group.life),std::uint64_t(x.group.generation),
        std::uint64_t(x.npcs),std::uint64_t(x.roster_complete),std::uint64_t(x.in_group_known),std::uint64_t(x.in_group),std::uint64_t(x.on_spawn_known),std::uint64_t(x.on_spawn)})mix(h,v);
    for(const auto count:b.items)mix(h,count);
    for(unsigned n=0;n<x.npcs&&n<f::npc_limit;++n){const auto& c=x.camps[n];
        for(auto v:{std::uint64_t(c.id+1),std::uint64_t(c.pose.x+1),std::uint64_t(c.pose.y+1),scalar(c.hp),scalar(c.max_hp),scalar(c.shield),std::uint64_t(c.actual)})mix(h,v);}
    return h;
}
}
class Interpreter {
    f::Program program_;Binding binding_{};State state_{State::Empty};Reason reason_{Reason::None};f::Stage stage_{f::Stage::Work};Facts facts_{};
    std::array<unsigned,6> cursor_{};unsigned bought_{},ops_{};std::uint64_t serial_{},last_event_{},last_event_hash_{};
    Stamp observed_{};std::uint64_t observation_hash_{};bool observed_any_{},outstanding_{},bank_started_{},capture_seen_{};
    Directive pending_{};int pending_leg_{-1};float target_hp_{};unsigned bank_bag_{},pending_bag_{},initial_bag_{};double initial_gold_{};
    std::array<int,f::npc_limit> before_id_{};std::array<float,f::npc_limit> before_hp_{};unsigned before_npcs_{};
    std::array<int,f::npc_limit> member_id_{};std::array<f::Point,f::npc_limit> member_pose_{};unsigned members_{};
    std::array<unsigned,f::item_limit> purchase_before_{};double purchase_gold_{};std::uint64_t bank_directive_{};
    Stamp bank_source_{};int bank_due_round_{-1};f::Kit kit_{};
    bool stop(Reason r){state_=State::Handoff;reason_=r;outstanding_=false;return false;}
    Directive reply(Kind k,Reason r=Reason::None)const{return {k,r,stage_,observed_,0,{},{}};}
    bool frame_valid(const Frame& x){
        if(!x.observed||!x.fresh||!x.permissions_known||!x.body.actual||x.stamp.owner!=binding_.source.owner||x.body.id!=x.stamp.owner||
            x.stamp.round<0||!x.stamp.input||!x.stamp.sample||!detail::finite(x.body.hp)||!detail::finite(x.body.max_hp)||x.body.hp>x.body.max_hp||
            !detail::finite(x.body.strength)||!detail::finite(x.body.focus)||!detail::finite(x.body.shield)||x.body.move_range>100||x.body.attack_range>100||
            x.body.reset_slot>1||x.body.shield_slot>1||x.body.heal_slot>1||unsigned(x.body.kit)>unsigned(f::Kit::Vendetta)||
            !x.body.bag_known||!x.body.gold_known||!detail::finite(x.body.gold)||x.npcs>f::npc_limit||x.receipts.size()>32)return stop(Reason::UnknownObservation);
        if(x.stamp.round>binding_.horizon)return stop(Reason::Date);
        if(x.group.id!=binding_.group)return stop(Reason::Binding);
        if(observed_any_&&x.body.kit!=kit_)return stop(Reason::ChangedPremise);
        if(x.group.life!=f::Life::Unknown&&x.group.generation!=binding_.generation&&!(capture_seen_&&stage_>=f::Stage::Release&&x.group.generation==binding_.generation+1))return stop(Reason::Generation);
        if(!x.body.alive||x.body.hp<=0)return stop(Reason::Death);
        if(x.group.life==f::Life::Unknown&&(stage_==f::Stage::Work||stage_==f::Stage::Capture))return stop(Reason::UnknownObservation);
        if(observed_any_){
            if(x.stamp.round<observed_.round||x.stamp.input<observed_.input||x.stamp.sample<observed_.sample||x.stamp.effect<observed_.effect)return stop(Reason::Stale);
            if(x.stamp.input==observed_.input&&x.stamp.round!=observed_.round)return stop(Reason::Stale);
            if(x.stamp==observed_&&detail::frame_hash(x)!=observation_hash_)return stop(Reason::Readback);
            if(x.stamp!=observed_&&x.stamp.sample==observed_.sample)return stop(Reason::Stale);
        }
        const bool new_input=!observed_any_||x.stamp.input!=observed_.input;
        observed_=x.stamp;observation_hash_=detail::frame_hash(x);observed_any_=true;if(new_input)ops_=0;
        return true;
    }
    bool receipt_frame(const Frame& x){
        // Validate this bounded batch before applying any credit.
        for(unsigned n=0;n<x.receipts.size();++n){const auto& r=x.receipts[n];
            if(!r.event||r.observed.owner!=x.stamp.owner||r.observed.round>x.stamp.round||r.observed.input>x.stamp.input||r.observed.sample>x.stamp.sample||r.observed.effect>x.stamp.effect||
                !r.observed.input||!r.observed.sample||!detail::finite(r.gold)||!detail::finite(r.cost)||!detail::finite(r.healing_amount))return stop(Reason::Receipt);
            for(unsigned k=0;k<n;++k)if(x.receipts[k].event==r.event&&detail::receipt_hash(x.receipts[k])!=detail::receipt_hash(r))return stop(Reason::Receipt);
            if(n&&r.event<x.receipts[n-1].event)return stop(Reason::Receipt);
            if(r.event==last_event_&&detail::receipt_hash(r)!=last_event_hash_)return stop(Reason::Receipt);
        }
        for(const auto& r:x.receipts){if(r.event<=last_event_)continue;
            last_event_=r.event;last_event_hash_=detail::receipt_hash(r);
            if(!r.source_known||r.evidence==Evidence::Unknown||r.program!=binding_.program)continue;
            if(r.observed.round<binding_.source.round||r.observed.input<binding_.source.input||
                r.observed.sample<binding_.source.sample||r.observed.effect<binding_.source.effect)return stop(Reason::Receipt);
            if(r.kind==ReceiptKind::Death)return stop(Reason::Death);
            if(r.kind==ReceiptKind::CustodyLost)return stop(Reason::CustodyLost);
            if(r.kind==ReceiptKind::Interrupted&&bank_started_&&r.source_directive==bank_directive_)return stop(Reason::Interrupted);
            if(r.group!=binding_.group||r.generation!=binding_.generation)continue;
            if(r.kind==ReceiptKind::GroupPayout){
                if(!r.amount_known||capture_seen_||x.group.life!=f::Life::Respawning||x.body.bag!=initial_bag_+r.gems||x.body.gold<initial_gold_+r.gold)return stop(Reason::Receipt);
                capture_seen_=true;facts_.capture_attributed=true;facts_.captured_gems=r.gems;facts_.captured_gold=r.gold;facts_.capture_event=r.event;
            }
            if(r.kind==ReceiptKind::BankPayout){
                if(!bank_started_||r.source_directive!=bank_directive_)continue;
                if(!r.amount_known||r.observed.round<bank_due_round_||r.observed.input<=bank_source_.input||r.observed.sample<=bank_source_.sample||r.observed.effect<=bank_source_.effect||
                    r.source_bag!=bank_bag_||r.gems!=bank_bag_||x.body.bag||x.body.channel!=f::Channel::None)return stop(Reason::Receipt);
                facts_.bank_attributed=true;facts_.banked_gems=r.gems;facts_.bank_event=r.event;bank_started_=false;
            }
            if(r.kind==ReceiptKind::Healing&&r.amount_known&&r.healing_amount>0){facts_.healing_observed=true;facts_.healing_event=r.event;}
        }
        return true;
    }
    int npc(const Frame& x,int id)const{for(unsigned n=0;n<x.npcs;++n)if(x.camps[n].id==id)return int(n);return -1;}
    bool roster(const Frame& x){
        if(!x.roster_complete||!x.group.roster_complete||!x.npcs)return stop(Reason::UnknownRoster);
        for(unsigned n=0;n<x.npcs;++n){const auto& c=x.camps[n];if(!c.actual||c.id<0||!detail::finite(c.hp)||!detail::finite(c.max_hp)||c.hp>c.max_hp)return stop(Reason::UnknownRoster);
            if(x.group.life!=f::Life::Alive&&c.hp>0)return stop(Reason::UnknownRoster);
            for(unsigned k=0;k<n;++k)if(c.id==x.camps[k].id)return stop(Reason::UnknownRoster);}
        if(members_){if(x.npcs!=members_)return stop(Reason::UnknownRoster);for(unsigned n=0;n<members_;++n){const int at=npc(x,member_id_[n]);
            if(at<0||!(x.camps[unsigned(at)].pose==member_pose_[n]))return stop(Reason::UnknownRoster);}}
        return true;
    }
    Directive physical(const Frame& x,f::Primitive command,int leg=-1,f::Purchase purchase={}){
        pending_={Kind::Physical,Reason::None,stage_,x.stamp,++serial_,command,purchase};pending_leg_=leg;outstanding_=true;
        pending_bag_=x.body.bag;
        if(command.action==f::Action::Basic){const int n=npc(x,command.target);target_hp_=n>=0?x.camps[unsigned(n)].hp:0;
            before_npcs_=x.npcs;for(unsigned k=0;k<x.npcs;++k){before_id_[k]=x.camps[k].id;before_hp_[k]=x.camps[k].hp;}}
        if(command.action==f::Action::Buy){purchase_before_=x.body.items;purchase_gold_=x.body.gold;}
        return pending_;
    }
    Directive route(const Frame& x,f::LegKind kind,bool& done){
        done=false;const auto k=unsigned(kind);const int id=program_.bodies[0].legs[k];
        if(id<0||unsigned(id)>=program_.leg_count){stop(Reason::Route);return reply(Kind::Handoff,reason_);}const auto& leg=program_.legs[unsigned(id)];
        const auto at=cursor_[k];if(at>leg.count||!leg.complete||leg.begin>program_.route_count||leg.count>program_.route_count-leg.begin){stop(Reason::Route);return reply(Kind::Handoff,reason_);}
        const auto expected=at?program_.route[leg.begin+at-1].destination:leg.origin;
        if(!(x.body.pose==expected)){stop(Reason::Route);return reply(Kind::Handoff,reason_);}
        if(at==leg.count){done=true;return reply(Kind::Preparation);}
        const auto& step=program_.route[leg.begin+at];if(x.stamp.round>step.latest_round){stop(Reason::Date);return reply(Kind::Handoff,reason_);}
        if(x.stamp.round<step.earliest_round)return reply(Kind::Wait,Reason::Date);
        if(!x.body.m)return reply(Kind::Wait,Reason::Permissions);
        if(step.destination==x.body.pose||f::distance(step.destination,x.body.pose)>int(x.body.move_range)){stop(Reason::Range);return reply(Kind::Handoff,reason_);}
        return physical(x,{f::Action::Move,step.destination},int(k));
    }
public:
    Interpreter()=default;Interpreter(const Interpreter&)=delete;Interpreter& operator=(const Interpreter&)=delete;
    State state()const{return state_;}Reason reason()const{return reason_;}f::Stage stage()const{return stage_;}const Facts& facts()const{return facts_;}
    const Binding& binding()const{return binding_;}
    void invalidate(Reason reason=Reason::ChangedPremise){stop(reason);}
    // observed_source is the immutable captured World supplied to the selected
    // Simulator. Local actual facts are joined exactly; its Result/source
    // ownership and full-world fingerprint remain the native caller's duty.
    // Construction and these bounded used-prefix copies need paid Prepare.
    bool begin(const Frame& initial,const f::Program& p,const f::Result& selected,Binding bind,const f::World& observed_source){
        binding_=bind;state_=State::Active;reason_=Reason::None;facts_={};cursor_={};bought_=ops_=0;serial_=last_event_=last_event_hash_=0;
        observed_any_=outstanding_=bank_started_=capture_seen_=false;pending_leg_=-1;bank_bag_=members_=0;bank_directive_=0;
        if(bind.source!=initial.stamp||bind.program!=p.id||!bind.program||!bind.initial_world||!bind.domain||bind.group<0||!bind.generation||bind.generation==~0u||
            bind.horizon<initial.stamp.round||bind.horizon-initial.stamp.round>512||selected.status!=f::Status::Complete||!selected.physical_complete||selected.service_tail!=f::Tail::Complete||
            selected.program!=p.id||selected.domain.initial_world!=bind.initial_world||selected.domain.assumptions!=bind.domain||selected.initial_round!=initial.stamp.round||selected.horizon!=bind.horizon)return stop(Reason::Binding);
        if(selected.terminal.actors!=1)return stop(Reason::JointUnsupported);
        if(initial.npcs>f::npc_limit||observed_source.npcs>f::npc_limit||observed_source.actors!=1||observed_source.npcs!=initial.npcs||observed_source.revision!=bind.initial_world||observed_source.round!=initial.stamp.round||observed_source.horizon!=bind.horizon||
            observed_source.group.id!=initial.group.id||observed_source.group.life!=initial.group.life||observed_source.group.generation!=initial.group.generation||
            !detail::same_body(observed_source.bodies[0],initial.body))return stop(Reason::Binding);
        for(unsigned n=0;n<initial.npcs;++n){bool found=false;for(unsigned k=0;k<observed_source.npcs;++k)if(detail::same_npc(initial.camps[n],observed_source.camps[k]))found=true;if(!found)return stop(Reason::Binding);}
        if(p.route_count>f::route_limit||p.leg_count>f::leg_limit||p.purchase_count>f::purchase_limit)return stop(Reason::Capacity);
        if(!p.bodies[0].required||p.bodies[0].initial!=f::Stage::Work)return stop(Reason::JointUnsupported);
        if(p.bodies[0].use_recall)return stop(Reason::RecallUnsupported);
        if(p.bodies[0].heal_for_work||p.bodies[0].require_useful_support)return stop(Reason::HealUnsupported);
        if(initial.group.life!=f::Life::Alive)return stop(Reason::Binding);
        program_.id=p.id;program_.route_count=p.route_count;program_.leg_count=p.leg_count;program_.purchase_count=p.purchase_count;program_.bodies=p.bodies;
        for(unsigned n=0;n<p.route_count;++n)program_.route[n]=p.route[n];for(unsigned n=0;n<p.leg_count;++n)program_.legs[n]=p.legs[n];
        for(unsigned n=0;n<p.purchase_count;++n)program_.purchases[n]=p.purchases[n];
        const auto& plan=p.bodies[0];if(plan.purchase_begin>p.purchase_count||plan.purchase_count>p.purchase_count-plan.purchase_begin)return stop(Reason::Purchase);
        for(unsigned k=0;k<6;++k){const int id=plan.legs[k];if(id<0||unsigned(id)>=p.leg_count)return stop(Reason::Route);const auto& leg=p.legs[unsigned(id)];
            if(!leg.complete||leg.begin>p.route_count||leg.count>p.route_count-leg.begin)return stop(Reason::Route);}
        for(unsigned n=0;n<p.route_count;++n)if(p.route[n].earliest_round<0||p.route[n].latest_round<p.route[n].earliest_round)return stop(Reason::Date);
        stage_=f::Stage::Work;kit_=initial.body.kit;if(!frame_valid(initial)||!roster(initial))return false;initial_bag_=initial.body.bag;initial_gold_=initial.body.gold;
        members_=initial.npcs;for(unsigned n=0;n<members_;++n){member_id_[n]=initial.camps[n].id;member_pose_[n]=initial.camps[n].pose;}
        return receipt_frame(initial);
    }
    // Cancel an unattempted intention after a native funding/guard denial.
    // This neither changes actual facts nor authorizes any physical suffix.
    bool cancel(const Directive& d){if(!outstanding_||d.id!=pending_.id||d.source!=pending_.source)return false;outstanding_=false;return true;}
    Directive choose(const Frame& x){
        if(state_==State::Empty)return reply(Kind::Handoff,Reason::Binding);
        if(state_==State::Handoff)return reply(Kind::Handoff,reason_);
        if(state_==State::Complete)return reply(Kind::Complete);
        if(outstanding_)return reply(Kind::Preparation,Reason::AwaitAttempt);
        if(!frame_valid(x)||!receipt_frame(x))return reply(Kind::Handoff,reason_);
        // Resolve an actual stopped channel before stun/completed-input waits.
        // Custody retained is interruption; empty custody needs dated payout.
        if(bank_started_&&x.body.channel==f::Channel::None){stop(x.body.bag?Reason::Interrupted:Reason::MissingPayout);return reply(Kind::Handoff,reason_);}
        if(x.body.completed)return reply(Kind::Wait,Reason::Permissions);
        if(x.body.stun_present)return reply(Kind::Wait,Reason::Stunned);
        if(x.body.channel!=f::Channel::None){if(x.body.channel!=f::Channel::Bank||!bank_started_){stop(Reason::Channel);return reply(Kind::Handoff,reason_);}return reply(Kind::Wait,Reason::Channel);}
        if(ops_>=8)return reply(Kind::Wait,Reason::ActionLimit);
        const auto& bp=program_.bodies[0];
        for(unsigned transition=0;transition<8;++transition){if(ops_>=8)return reply(Kind::Wait,Reason::ActionLimit);++ops_;bool done=false;Directive d;
            switch(stage_){
            case f::Stage::Work:case f::Stage::Renew:{
                if(stage_==f::Stage::Work&&!roster(x))return reply(Kind::Handoff,reason_);
                if(stage_==f::Stage::Work&&x.group.life!=f::Life::Alive){facts_.clear_observed=true;stage_=f::Stage::Capture;continue;}
                if(stage_==f::Stage::Renew){
                    if(x.body.bag||!facts_.source_released||!facts_.recovered_actual_state||bought_!=bp.purchase_count){stop(Reason::Unfinished);return reply(Kind::Handoff,reason_);}
                }
                d=route(x,stage_==f::Stage::Work?f::LegKind::Work:f::LegKind::Renew,done);if(!done)return d;
                if(stage_==f::Stage::Renew){if(x.group.life==f::Life::Unknown)return reply(Kind::Preparation,Reason::UnknownObservation);
                    if(x.group.life!=f::Life::Alive)return reply(Kind::Wait,Reason::Generation);
                    if(x.group.generation!=binding_.generation+1){stop(Reason::Generation);return reply(Kind::Handoff,reason_);}}
                if(!roster(x))return reply(Kind::Handoff,reason_);
                const auto& b=x.body;
                if(bp.shield_for_work&&b.kit==f::Kit::Varg&&b.c&&!b.cast_used&&!b.shield_present&&b.shield_slot<2&&!b.cooldown[b.shield_slot])return physical(x,{f::Action::Shield,{},-1,b.shield_slot});
                int target=-1;for(unsigned n=0;n<x.npcs;++n)if(x.camps[n].hp>0&&f::distance(b.pose,x.camps[n].pose)<=int(b.attack_range)&&(target<0||x.camps[n].id<target))target=x.camps[n].id;
                // This single-body standing family has no omitted helper that
                // can finish an out-of-range member. It needs a NEW approach.
                if(target<0){stop(Reason::Target);return reply(Kind::Handoff,reason_);}
                if(b.a)return physical(x,{f::Action::Basic,{},target});
                if(bp.reset_for_work&&b.c&&!b.cast_used&&b.reset_slot<2&&!b.cooldown[b.reset_slot])return physical(x,{f::Action::Reset,{},-1,b.reset_slot});
                return reply(Kind::Wait,Reason::Permissions);}
            case f::Stage::Capture:
                if(x.group.life==f::Life::Respawning){if(!capture_seen_){stop(Reason::MissingPayout);return reply(Kind::Handoff,reason_);}stage_=f::Stage::Release;continue;}
                if(x.group.life!=f::Life::Reward){stop(Reason::ChangedPremise);return reply(Kind::Handoff,reason_);}
                d=route(x,f::LegKind::Capture,done);if(!done)return d;
                if(!x.in_group_known){stop(Reason::UnknownPresence);return reply(Kind::Handoff,reason_);}if(!x.in_group){stop(Reason::Route);return reply(Kind::Handoff,reason_);}
                return reply(Kind::Wait);
            case f::Stage::Release:
                d=route(x,f::LegKind::Release,done);if(!done)return d;
                if(!x.in_group_known){stop(Reason::UnknownPresence);return reply(Kind::Handoff,reason_);}if(x.in_group){stop(Reason::Route);return reply(Kind::Handoff,reason_);}
                facts_.source_released=true;stage_=f::Stage::Altar;continue;
            case f::Stage::Altar:
                if(bank_started_&&!x.body.bag){if(!facts_.bank_attributed){stop(Reason::MissingPayout);return reply(Kind::Handoff,reason_);}}
                if(bank_started_&&x.body.bag){stop(Reason::Interrupted);return reply(Kind::Handoff,reason_);}
                if(!x.body.bag){if((initial_bag_||facts_.captured_gems)&&!facts_.bank_attributed){stop(Reason::MissingPayout);return reply(Kind::Handoff,reason_);}stage_=f::Stage::Return;continue;}
                d=route(x,f::LegKind::Altar,done);if(!done)return d;
                if(!bp.altar_known||f::distance(x.body.pose,bp.altar)>1){stop(Reason::Range);return reply(Kind::Handoff,reason_);}
                return physical(x,{f::Action::Bank,bp.altar});
            case f::Stage::Return:
                if(!x.on_spawn_known)return reply(Kind::Preparation,Reason::UnknownSpawn);
                if(x.on_spawn){if(x.body.hp<x.body.max_hp)return reply(Kind::Wait);facts_.recovered_actual_state=true;stage_=f::Stage::Invest;continue;}
                d=route(x,f::LegKind::Spawn,done);if(!done)return d;
                if(!x.on_spawn){stop(Reason::Route);return reply(Kind::Handoff,reason_);}continue;
            case f::Stage::Invest:
                if(!x.on_spawn_known||!x.on_spawn){stop(Reason::UnknownSpawn);return reply(Kind::Handoff,reason_);}
                if(bought_==bp.purchase_count){stage_=f::Stage::Renew;continue;}
                {const auto& p=program_.purchases[bp.purchase_begin+bought_];if(!p.count||!p.quote_revision||p.item>=f::item_limit||!detail::finite(p.quoted_cost)||p.quoted_cost<=0||p.quoted_cost>std::floor(x.body.gold)){stop(Reason::Purchase);return reply(Kind::Handoff,reason_);}
                    return physical(x,{f::Action::Buy,{},int(p.item)},-1,p);}
            case f::Stage::Done:state_=State::Complete;return reply(Kind::Complete);
            }
        }
        return reply(Kind::Preparation,Reason::TransitionLimit);
    }
    bool attempt(const Directive& d,Attempt result,const Frame& post){
        if(state_!=State::Active||!outstanding_||d.id!=pending_.id||d.source!=pending_.source)return false;
        const auto command=pending_;const int leg=pending_leg_;outstanding_=false;
        if(result==Attempt::Unknown)return stop(Reason::UnknownAttempt);
        if(post.stamp.owner!=command.source.owner||post.stamp.round!=command.source.round||post.stamp.input!=command.source.input||
            post.stamp.sample<=command.source.sample||post.stamp.effect<=command.source.effect)return stop(Reason::AwaitFresh);
        if(!frame_valid(post)||!receipt_frame(post))return false;
        if(result==Attempt::Refused)return stop(Reason::Refused);
        const auto& b=post.body;
        switch(command.primitive.action){
        case f::Action::Move:if(leg<0||!(b.pose==command.primitive.destination)||b.m||(b.kit==f::Kit::Willow&&b.a))return stop(Reason::Readback);++cursor_[unsigned(leg)];break;
        case f::Action::Basic:{if(!roster(post))return false;const int n=npc(post,command.primitive.target);if(n<0)return stop(Reason::UnknownDamage);
            const auto hp=post.camps[unsigned(n)].hp;if(hp>target_hp_||b.a)return stop(Reason::Readback);
            if(post.npcs!=before_npcs_)return stop(Reason::UnknownDamage);double delta=0;
            for(unsigned k=0;k<before_npcs_;++k){const int at=npc(post,before_id_[k]);if(at<0||post.camps[unsigned(at)].hp>before_hp_[k])return stop(Reason::UnknownDamage);
                delta+=double(before_hp_[k])-double(post.camps[unsigned(at)].hp);}
            facts_.work_damage+=delta;
            if(stage_==f::Stage::Renew&&delta>0){if(post.group.life!=f::Life::Alive||b.hp!=b.max_hp||b.stun_present)return stop(Reason::Unfinished);
                facts_.renewal_damage+=delta;facts_.renewed=true;stage_=f::Stage::Done;state_=State::Complete;}break;}
        case f::Action::Reset:if(!b.a||b.c||command.primitive.slot>1||!b.cooldown[command.primitive.slot])return stop(Reason::Readback);break;
        case f::Action::Shield:if(b.c||!b.shield_present||b.shield<=0||command.primitive.slot>1||!b.cooldown[command.primitive.slot])return stop(Reason::Readback);break;
        case f::Action::Bank:if(b.channel!=f::Channel::Bank||!b.bag||b.bag!=pending_bag_||!b.channel_ticks||b.channel_ticks>512)return stop(Reason::Readback);
            if(std::int64_t(command.source.round)+b.channel_ticks>binding_.horizon)return stop(Reason::Date);
            bank_started_=true;bank_directive_=command.id;bank_source_=command.source;bank_due_round_=command.source.round+int(b.channel_ticks);bank_bag_=b.bag;break;
        case f::Action::Buy:{
            const auto& wanted=command.purchase;bool exact=true;for(unsigned n=0;n<f::item_limit;++n){const auto addition=n==wanted.item?wanted.count:0u;exact=exact&&purchase_before_[n]<=~0u-addition&&b.items[n]==purchase_before_[n]+addition;}
            bool receipt=false;for(const auto& r:post.receipts)if(r.kind==ReceiptKind::Purchase&&r.program==binding_.program&&r.source_directive==command.id&&r.observed.owner==b.id&&r.evidence!=Evidence::Unknown&&r.source_known&&r.amount_known&&
                r.observed.round==command.source.round&&r.observed.input==command.source.input&&r.observed.sample>command.source.sample&&r.observed.effect>command.source.effect&&
                r.item==wanted.item&&r.count==wanted.count&&r.cost==wanted.quoted_cost&&r.group==binding_.group&&r.generation==binding_.generation)receipt=true;
            if(!exact||!receipt||purchase_gold_-b.gold!=wanted.quoted_cost)return stop(Reason::Purchase);++bought_;++facts_.purchases;facts_.spent_gold+=wanted.quoted_cost;break;}
        case f::Action::Heal:return stop(Reason::HealUnsupported);
        case f::Action::Recall:return stop(Reason::RecallUnsupported);
        case f::Action::Hold:return stop(Reason::Invalid);
        }
        return true;
    }
};
} // namespace wb::capital::service_execution
