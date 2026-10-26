#pragma once
// Dated SELF continuation producer. A quote is a conditional nomination, never
// current Life, inventory, movement, capture or foreign-actor authority.
#include "service_execution.h"
#include "production_investment.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace tt::production_job {
using Point=mb::service::Point;
inline constexpr std::size_t max_members=mb::service::max_members;
inline constexpr std::size_t slots=mb::service::spell_slots;
enum class Status { NeedCompute,Ready,Incomplete,Unsupported };
enum class Reason { None,MissingSelf,Dates,Shape,Life,Hp,Geometry,Traffic,Calendar,Word,Survival,Shop,Wallet,NoProgress,Capacity,BankHazard,RecoveryHazard };
enum class Life { Unknown,Alive,Reward,Respawning };
enum class Enabler { None,ObserveAlive,ObserveReward,ObserveRenewal,RefreshWallet,Readback };
enum class BankPolicy { QuietOnly,ChargedHome };
enum Assumption : std::uint32_t {
    FutureLife=1u,FutureHp=2u,FutureOccupancy=4u,FutureReadback=8u,FutureRenewal=16u,BodyPersists=32u
};
struct Key {
    std::uint64_t actor{},group{},generation{},body_revision{},life_revision{},geometry_revision{},word_revision{};
    friend bool operator==(Key,Key)=default;
};
struct Step { Point point;double raw_upper{}; };
struct Leg {
    std::vector<Step> points;
    std::uint32_t handoff_inputs{};
    bool complete{},disjoint_supported{};
};
struct Snapshot {
    std::uint64_t actor{},body_revision{};
    std::uint32_t epoch{},gold{},carried_gems{};
    mb::service::Body body;
    std::array<mb::service::Spell,slots> spells{};
    std::array<std::uint32_t,slots> cooldown{},period{};
    // Actual camp-effective current values above; factors below reproduce
    // them and project only a declared stable post-purchase kit.
    float basic_scale{1};
    float raw_strength{},raw_focus{},missing_health_bonus{},incoming_scale{1};
    std::array<float,slots> focus_modifier{};
    production_investment::ShopInput shop;
    bool own{},complete{},stable_outgoing{},shop_supported{};
};
struct Job {
    std::uint64_t group{},generation{},life_revision{},geometry_revision{},word_revision{};
    Life life{Life::Unknown};Enabler enabler{Enabler::ObserveAlive};
    std::uint32_t observed_round{},available_from{};
    Point perch;
    std::array<mb::service::Member,max_members> members{};
    std::array<float,max_members> full_hp{};
    std::size_t member_count{};
    int width{},height{};
    std::uint32_t gems{},gold{},capturers{1},capture_rank{};
    // Initial family admits integral owned gold credit; fractional wallet
    // arithmetic needs an interval/readback family, never silent truncation.
    float gold_multiplier{1},bank_gold_per_gem{};
    std::uint32_t capture_payout_inputs{},respawn_inputs{};
    std::uint32_t submit_minimum{1},submit_base{},submit_divisor{10};float submit_multiplier{};
    std::uint32_t heal_inputs{1},purchase_readback_inputs{1};float spawn_healing{};
    double external_work_raw{},external_capture_raw{};
    Leg initial,capture,release,bank,renewal,home,redeploy;
    bool full_group_known{},conditional_hp{},life_supported{},continuation_supported{};
    bool repeat{},purchase_when_home{};
    bool external_sources_complete{},common_full_generation{};
    BankPolicy bank_policy{BankPolicy::QuietOnly};
};
struct Input {
    Key key;Snapshot self;Job job;
    std::uint32_t start{},horizon{};
};
enum class EventKind { Payout,Bank };
struct Event { EventKind kind{};std::uint32_t offset{},period{},count{1};std::uint64_t gems{}; };
struct Quote {
    Key key;Status status{Status::NeedCompute};Reason reason{Reason::None};Enabler enabler{Enabler::None},renewal_enabler{Enabler::None};
    std::uint32_t start{},horizon{},assumptions{};
    std::uint64_t banked_gems_by_H{},carried_gems{},owned_gold{},cycles{},commands{},purchase_calls{};
    std::uint32_t last_clear{},last_payout{},last_release{},last_bank{},last_renewal{};
    production_investment::RawBody terminal_body;
    // complete means the conditional BANKED forecast covers H, not that every
    // partial operation at H reached its destination/readback endpoint.
    bool complete{},supported{},conditional{},disjoint_supported{};
    bool gold_exact{true}; // false: owned_gold is a conservative lower bound; future Shop is then incomplete
    bool shift_invariant{};std::uint64_t initial_carried{};std::vector<Event> events;
    bool terminal_resources_available{true};
    std::uint32_t last_purchase_readback{};float last_projected_basic{};
    std::size_t work_used{};
};
// Conditional FIRST cash/recovery calendar only; never actual accepted actions,
// current Life, healing or route authority. Full-H valuation may end midway
// through this programme. Dates belong to the ORIGINAL Input.start, not a
// later rebased quote. first_cash_complete includes paid redeployment when
// repeat=true; a finite repeat=false job ends recovered at quiet home.
struct CashPlan {
    std::uint32_t bank_arrival{},channel_ticks{},channel_paid{},bank_complete{},home_arrival{},
        heal_complete{},departure{},redeploy_complete{},renewal_not_before{};
    double bank_raw_upper{};
    float hp_at_bank{},hp_after_bank{},hp_at_home{},hp_recovered{};
    bool channel_started{},banked{},home_reached{},healed{},first_cash_complete{},departed{},redeployed{};
    Enabler renewal_enabler{Enabler::ObserveRenewal};
};

// One unit validates one path point, scores ONE bounded command candidate, or
// advances an endpoint. A candidate copies/scans <=32 members; no full AoE box
// or whole owner-input sequence is hidden in one unit. Area centers are a
// declared representative member-centered/clamped family, not an optimum.
// Structural units are not a measured native WASM fuel bound.
class Producer {
public:
    explicit Producer(Input input);
    Status advance(std::size_t units);
    Status status() const {return out_.status;}
    const Quote& result() const {return out_;}
    // Immutable original programme, including all seven owned paths and the
    // declared word/scenario. This view is valid only while this Producer is
    // alive; it is neither a current observation nor command authority.
    const Input& programme() const {return in_;}
    const CashPlan* cash_plan() const {return cash_.get();}
    std::size_t last_work() const {return last_work_;}
private:
    enum class Phase { Validate,Initial,Start,Work,Capture,Payout,Release,Bank,AfterBank,Home,Shop,Redeploy,Renewal,Done,BankChannel,CashWait };
    Input in_;Quote out_;Phase phase_{Phase::Validate};
    mb::service::State work_;
    production_investment::ShopInput shop_;
    std::array<std::uint32_t,slots> ready_{};
    std::uint32_t clock_{},release_at_{},shield_timer_{};
    std::uint32_t recovery_inputs_{};
    std::uint64_t wallet_{};std::size_t validate_leg_{},validate_point_{},last_work_{};
    std::array<double,7> leg_charge_{},leg_reserve_{};std::array<std::uint32_t,7> leg_ticks_{};
    bool first_{true};
    bool turn_open_{},turn_changed_{};unsigned primitives_{};
    float raw_strength_{},raw_focus_{};
    float gold_multiplier_{1},bank_gold_rate_{};
    std::size_t candidate_stage_{},candidate_member_{},candidate_slot_{};
    mb::service::ExecutionChoice choice_;
    mb::service::execution_detail::Score score_;
    struct Cycle {
        bool present{};mb::service::Body body;
        std::array<std::uint32_t,slots> cooldown{};
        std::uint32_t clock{};std::uint64_t banked{},gold{},cycles{},commands{};
    } cycle_;
    std::unique_ptr<CashPlan> cash_;
    std::uint32_t channel_left_{};
    void fail(Status,Reason);
    bool tick(std::uint32_t);
    bool travel(std::size_t,const Leg&);
    void unit();void validate();void begin_work();void work_input();void after_bank();void shop();
    void candidate(mb::service::Primitive,bool);void end_turn();
    void refresh_damage();bool wait(std::uint32_t,double);
    void bank_channel();void cash_recover();void cash_wait();void cash_bank_complete();
    bool repeat_cycle();
    bool record(EventKind,std::uint32_t,std::uint32_t,std::uint32_t,std::uint64_t);
    const Leg& leg(std::size_t) const;
    void finish();
};

inline void Producer::fail(Status status,Reason reason)
{out_.status=status;out_.reason=reason;out_.supported=false;phase_=Phase::Done;}
inline const Leg& Producer::leg(std::size_t index) const
{
    switch(index){case 0:return in_.job.initial;case 1:return in_.job.capture;case 2:return in_.job.release;
        case 3:return in_.job.bank;case 4:return in_.job.renewal;case 5:return in_.job.home;default:return in_.job.redeploy;}
}
inline Producer::Producer(Input value):in_{std::move(value)},shop_{in_.self.shop},clock_{in_.start},wallet_{in_.self.gold}
{
    const auto& input=in_;
    out_.key=input.key;out_.start=input.start;out_.horizon=input.horizon;out_.carried_gems=input.self.carried_gems;
    out_.owned_gold=input.self.gold;out_.conditional=true;
    out_.initial_carried=input.self.carried_gems;
    out_.assumptions=FutureLife|FutureOccupancy|BodyPersists;
    const auto& self=input.self;const auto& job=input.job;
    if(job.bank_policy!=BankPolicy::QuietOnly&&job.bank_policy!=BankPolicy::ChargedHome)
    {fail(Status::Unsupported,Reason::Shape);return;}
    if(job.bank_policy==BankPolicy::ChargedHome) {
        if(job.purchase_when_home){fail(Status::Unsupported,Reason::Shop);return;}
        if(job.home.points.empty()||job.home.points.back().raw_upper!=0)
        {fail(Status::Unsupported,Reason::RecoveryHazard);return;}
        try {cash_=std::make_unique<CashPlan>();cash_->renewal_enabler=job.repeat?Enabler::ObserveRenewal:Enabler::None;}
        catch(const std::bad_alloc&){fail(Status::Unsupported,Reason::Capacity);return;}
    }
    out_.renewal_enabler=job.repeat?Enabler::ObserveRenewal:Enabler::None;
    if(!self.own || !self.complete || !self.stable_outgoing || self.actor!=input.key.actor || self.body_revision!=input.key.body_revision)
    {fail(Status::Incomplete,Reason::MissingSelf);return;}
    if(input.start<self.epoch || input.horizon<input.start || input.horizon-input.start>1000000u ||
        (!job.common_full_generation && job.observed_round>input.start))
    {fail(Status::Unsupported,Reason::Dates);return;}
    if(job.group!=input.key.group || job.generation!=input.key.generation || job.life_revision!=input.key.life_revision ||
       job.geometry_revision!=input.key.geometry_revision || job.word_revision!=input.key.word_revision ||
       job.width<=0 || job.height<=0 || job.width>256 || job.height>256 || job.member_count==0 || job.member_count>max_members ||
       !job.capturers || job.capture_rank>=job.capturers || (job.submit_multiplier<=0 && !job.submit_divisor) || !job.capture_payout_inputs ||
       !std::isfinite(job.submit_multiplier) || job.submit_multiplier<0 ||
       !std::isfinite(job.gold_multiplier) || job.gold_multiplier<0 || !std::isfinite(job.bank_gold_per_gem) || job.bank_gold_per_gem<0)
    {fail(Status::Unsupported,Reason::Shape);return;}
    if(!job.full_group_known || !job.continuation_supported || !job.external_sources_complete)
    {fail(Status::Incomplete,Reason::Geometry);return;}
    if(!std::isfinite(job.external_work_raw) || job.external_work_raw<0 || job.external_capture_raw!=0)
    {fail(Status::Unsupported,Reason::Geometry);return;}
    if(!job.common_full_generation && !job.life_supported && (job.life==Life::Alive || job.life==Life::Reward))
    {fail(Status::Incomplete,Reason::Life);return;}
    if(job.common_full_generation || job.life==Life::Unknown || job.life==Life::Respawning)
    {
        if(!job.common_full_generation && job.enabler==Enabler::None){fail(Status::Incomplete,Reason::Life);return;}
        out_.enabler=job.common_full_generation?Enabler::ObserveAlive:job.enabler;out_.assumptions|=FutureHp;
    }
    if(self.body.shield!=0 || self.body.hp<=0 || !std::isfinite(self.body.hp) || self.body.hp>self.body.max_hp)
    {fail(Status::Unsupported,Reason::Word);return;}
    work_.body=self.body;work_.spells=self.spells;work_.width=job.width;work_.height=job.height;work_.incoming_complete=true;
    for(std::size_t slot=0;slot<slots;++slot)
    {
        const auto& spell=self.spells[slot];
        if(spell.range<0 || spell.range>8 || spell.radius<0 || spell.radius>8 ||
            (spell.kind!=mb::service::SpellKind::none && self.period[slot]==0) ||
            self.cooldown[slot]>std::numeric_limits<std::uint32_t>::max()-self.epoch)
        {fail(Status::Unsupported,Reason::Word);return;}
        if(spell.kind!=mb::service::SpellKind::none && spell.kind!=mb::service::SpellKind::reset && spell.damage>0 &&
           (!std::isfinite(self.focus_modifier[slot]) || self.focus_modifier[slot]<=0))
        {fail(Status::Incomplete,Reason::Word);return;}
        ready_[slot]=self.epoch+self.cooldown[slot];
    }
    if(job.purchase_when_home && !self.shop_supported)
    {fail(Status::Incomplete,Reason::Shop);return;}
    shop_.gold=self.gold;
    raw_strength_=self.raw_strength;raw_focus_=self.raw_focus;
    gold_multiplier_=job.gold_multiplier;bank_gold_rate_=job.bank_gold_per_gem;
    if(!std::isfinite(raw_strength_) || raw_strength_<0 || !std::isfinite(raw_focus_) || raw_focus_<0 ||
        !std::isfinite(self.missing_health_bonus) || self.missing_health_bonus<0 ||
        !std::isfinite(self.incoming_scale) || self.incoming_scale<0)
    {fail(Status::Unsupported,Reason::Word);return;}
    out_.shift_invariant=job.common_full_generation;
    for(const auto ready:ready_)if(ready>input.start)out_.shift_invariant=false;
    const float observed_basic=work_.body.strength;std::array<float,slots> observed_spell{};
    for(std::size_t slot=0;slot<slots;++slot)observed_spell[slot]=work_.spells[slot].damage;
    refresh_damage();
    if(work_.body.strength!=observed_basic || work_.body.focus!=self.body.focus)
    {fail(Status::Incomplete,Reason::Word);return;}
    for(std::size_t slot=0;slot<slots;++slot)
        if(work_.spells[slot].kind!=mb::service::SpellKind::none && work_.spells[slot].kind!=mb::service::SpellKind::reset &&
            work_.spells[slot].damage!=observed_spell[slot])
        {fail(Status::Incomplete,Reason::Word);return;}
}
inline bool Producer::record(EventKind kind,std::uint32_t offset,std::uint32_t period,std::uint32_t count,std::uint64_t gems)
{
    if(!gems || !count)return true;
    if(out_.events.size()>=512){fail(Status::Unsupported,Reason::Capacity);return false;}
    out_.events.push_back({kind,offset,period,count,gems});return true;
}
inline void Producer::refresh_damage()
{
    namespace d=production_investment::detail;
    const float missing=d::f32(1.0f-d::f32(work_.body.hp/work_.body.max_hp));
    const float health=d::add(1.0f,d::mul(in_.self.missing_health_bonus,missing));
    const float monster=d::add(1.0f,shop_.effects.monster);
    work_.body.strength=d::mul(d::mul(d::mul(raw_strength_,in_.self.basic_scale),health),monster);
    work_.body.focus=raw_focus_;
    for(std::size_t slot=0;slot<slots;++slot)
    {
        auto& spell=work_.spells[slot];
        if(spell.kind==mb::service::SpellKind::none || spell.kind==mb::service::SpellKind::reset)continue;
        spell.damage=d::mul(raw_focus_,in_.self.focus_modifier[slot]);
        if(spell.kind!=mb::service::SpellKind::shield)spell.damage=d::mul(d::mul(spell.damage,health),monster);
    }
}
inline bool Producer::wait(std::uint32_t ticks,double raw)
{
    const double charge=static_cast<double>(ticks)*raw*work_.body.camp_damage_multiplier;
    if(!std::isfinite(charge) || charge<0 || work_.body.hp<=charge)
    {fail(Status::Unsupported,Reason::Survival);return false;}
    if(!tick(ticks))return false;
    work_.body.hp=static_cast<float>(work_.body.hp-charge);refresh_damage();return true;
}
inline bool Producer::tick(std::uint32_t ticks)
{
    if(ticks>std::numeric_limits<std::uint32_t>::max()-clock_){fail(Status::Unsupported,Reason::Calendar);return false;}
    if(clock_+ticks>in_.horizon){clock_=in_.horizon;out_.terminal_resources_available=false;finish();return false;}
    clock_+=ticks;return true;
}
inline void Producer::validate()
{
    if(validate_leg_==7)
    {
        const auto& capture=in_.job.capture;const auto& release=in_.job.release;
        if(capture.points.empty() || release.points.empty()){fail(Status::Incomplete,Reason::Geometry);return;}
        bool source=false,released=true;
        for(std::size_t i=0;i<in_.job.member_count;++i)
        {source|=capture.points.back().point==in_.job.members[i].pos;released&=release.points.back().point!=in_.job.members[i].pos;}
        if(!source || !released){fail(Status::Unsupported,Reason::Geometry);return;}
        phase_=Phase::Initial;return;
    }
    const auto& path=leg(validate_leg_);
    if(path.points.empty() || !path.complete || !path.disjoint_supported)
    {++validate_leg_;validate_point_=0;return;}
    const auto& step=path.points[validate_point_];
    const auto& job=in_.job;
    if(step.point.x<0 || step.point.y<0 || step.point.x>=job.width || step.point.y>=job.height ||
        !std::isfinite(step.raw_upper) || step.raw_upper<0 ||
        (validate_point_ && mb::service::distance(path.points[validate_point_-1].point,step.point)!=1))
    {fail(Status::Unsupported,Reason::Geometry);return;}
    double selected_upper=0;
    for(std::size_t i=0;i<job.member_count;++i)
        if(mb::service::distance(step.point,job.members[i].pos)<=job.members[i].range)selected_upper+=job.members[i].strength;
    if(!std::isfinite(selected_upper) || selected_upper<0 || step.raw_upper<selected_upper)
    {fail(Status::Unsupported,Reason::Geometry);return;}
    if(path.points.size()>static_cast<std::size_t>(job.width)*job.height+1)
    {fail(Status::Unsupported,Reason::Capacity);return;}
    if(validate_point_)leg_charge_[validate_leg_]+=step.raw_upper;
    leg_reserve_[validate_leg_]=std::max(leg_reserve_[validate_leg_],step.raw_upper);
    ++validate_point_;
    if(validate_point_==path.points.size())
    {
        leg_charge_[validate_leg_]+=path.handoff_inputs*step.raw_upper;
        const auto moves=path.points.size()-1;
        if(moves>std::numeric_limits<std::uint32_t>::max()-path.handoff_inputs ||
           !std::isfinite(leg_charge_[validate_leg_])){fail(Status::Unsupported,Reason::Calendar);return;}
        leg_ticks_[validate_leg_]=static_cast<std::uint32_t>(moves)+path.handoff_inputs;
        ++validate_leg_;validate_point_=0;
    }
}
inline bool Producer::travel(std::size_t index,const Leg& path)
{
    if(!path.complete || !path.disjoint_supported || path.points.empty() || path.points.front().point!=work_.body.pos)
    {fail(Status::Incomplete,Reason::Geometry);return false;}
    const double loss=leg_charge_[index]*work_.body.camp_damage_multiplier;
    const double reserve=leg_reserve_[index]*work_.body.camp_damage_multiplier;
    if(!std::isfinite(loss) || !std::isfinite(reserve) || static_cast<double>(work_.body.hp)<=loss+reserve)
    {fail(Status::Unsupported,Reason::Survival);return false;}
    if(!tick(leg_ticks_[index]))return false;
    work_.body.hp=static_cast<float>(static_cast<double>(work_.body.hp)-loss);
    work_.body.pos=path.points.back().point;refresh_damage();return true;
}
inline void Producer::begin_work()
{
    const auto& job=in_.job;
    if(work_.body.pos!=job.perch){fail(Status::Unsupported,Reason::Geometry);return;}
    double raw=job.external_work_raw;
    for(std::size_t i=0;i<job.member_count;++i)
        if(mb::service::distance(job.perch,job.members[i].pos)<=job.members[i].range)raw+=job.members[i].strength;
    if(first_ && !job.common_full_generation && clock_<job.available_from && !wait(job.available_from-clock_,raw))return;
    if(!first_ && clock_<release_at_ && !wait(release_at_-clock_,raw))return;
    if(!first_ && !job.purchase_when_home && repeat_cycle() && out_.status!=Status::NeedCompute)return;
    work_.member_count=job.member_count;
    for(std::size_t i=0;i<job.member_count;++i)
    {
        auto member=job.members[i];
        if(!std::isfinite(job.full_hp[i]) || job.full_hp[i]<=0 || !std::isfinite(member.hp) || member.hp<0)
        {fail(Status::Incomplete,Reason::Hp);return;}
        if(member.pos.x<0 || member.pos.y<0 || member.pos.x>=job.width || member.pos.y>=job.height)
        {fail(Status::Unsupported,Reason::Geometry);return;}
        if(first_ && job.life==Life::Reward && !job.common_full_generation)
        {member.hp=0;member.hp_exact=true;member.awake=false;member.stun_remaining=0;}
        else if(!first_ || !member.hp_exact || job.common_full_generation)
        {
            if(first_ && !job.conditional_hp && !job.common_full_generation){fail(Status::Incomplete,Reason::Hp);return;}
            member.hp=job.full_hp[i];member.hp_exact=true;member.stun_remaining=0;out_.assumptions|=FutureHp;
            member.awake=true; // common conditional full generation grants no free sleeping volley
        }
        else if(clock_>=job.observed_round)
            member.stun_remaining=static_cast<int>(std::max<std::int64_t>(0,
                static_cast<std::int64_t>(member.stun_remaining)-(clock_-job.observed_round)));
        work_.members[i]=member;
    }
    if(mb::service::state_status(work_)!=mb::service::ModelStatus::ready)
    {fail(Status::Incomplete,Reason::Word);return;}
    work_.body.shield=0;shield_timer_=0;turn_open_=false;
    out_.last_renewal=clock_;phase_=Phase::Work;
}
inline bool Producer::repeat_cycle()
{
    const auto& body=work_.body;std::array<std::uint32_t,slots> cooldown{};
    for(std::size_t slot=0;slot<slots;++slot)cooldown[slot]=ready_[slot]>clock_?ready_[slot]-clock_:0;
    const auto& old=cycle_.body;
    const bool same=cycle_.present && cooldown==cycle_.cooldown && body.pos==old.pos && body.hp==old.hp &&
        body.max_hp==old.max_hp && body.strength==old.strength && body.focus==old.focus &&
        body.camp_damage_multiplier==old.camp_damage_multiplier && out_.carried_gems==0;
    if(same && clock_>cycle_.clock)
    {
        const std::uint32_t span=clock_-cycle_.clock;const auto repetitions=(in_.horizon-clock_)/span;
        if(repetitions)
        {
            const std::uint64_t shift=static_cast<std::uint64_t>(repetitions)*span;
            const auto cycle_gems=out_.banked_gems_by_H-cycle_.banked;
            if(!record(EventKind::Payout,out_.last_payout-in_.start+span,span,repetitions,cycle_gems) ||
                !record(EventKind::Bank,out_.last_bank-in_.start+span,span,repetitions,cycle_gems))return true;
            const auto add=[&](std::uint64_t& value,std::uint64_t previous) {
                if(value<previous || ((value-previous)>0 && repetitions>(std::numeric_limits<std::uint64_t>::max()-value)/(value-previous)))return false;
                value+=(value-previous)*repetitions;return true;
            };
            if(!add(out_.banked_gems_by_H,cycle_.banked) || !add(wallet_,cycle_.gold) ||
               !add(out_.cycles,cycle_.cycles) || !add(out_.commands,cycle_.commands))
            {fail(Status::Unsupported,Reason::Capacity);return true;}
            clock_+=static_cast<std::uint32_t>(shift);
            for(auto& ready:ready_)
            {if(static_cast<std::uint64_t>(ready)+shift>std::numeric_limits<std::uint32_t>::max()){fail(Status::Unsupported,Reason::Calendar);return true;}ready+=static_cast<std::uint32_t>(shift);}
            if(static_cast<std::uint64_t>(release_at_)+shift>std::numeric_limits<std::uint32_t>::max())
            {fail(Status::Unsupported,Reason::Calendar);return true;}
            release_at_+=static_cast<std::uint32_t>(shift);
            out_.last_clear+=static_cast<std::uint32_t>(shift);out_.last_payout+=static_cast<std::uint32_t>(shift);
            out_.last_release+=static_cast<std::uint32_t>(shift);out_.last_bank+=static_cast<std::uint32_t>(shift);
        }
    }
    cycle_.present=true;cycle_.body=body;cycle_.cooldown=cooldown;cycle_.clock=clock_;
    cycle_.banked=out_.banked_gems_by_H;cycle_.gold=wallet_;cycle_.cycles=out_.cycles;cycle_.commands=out_.commands;
    return false;
}
inline void Producer::candidate(mb::service::Primitive primitive,bool utility)
{
    namespace service=mb::service;
    auto option=work_;
    const bool applied=primitive.kind==service::PrimitiveKind::basic?service::apply_basic(option,primitive.member_id):
        service::apply_cast(option,primitive.slot,primitive.aim,primitive.member_id);
    if(!applied)return;
    const auto initial=service::next_camp_volley(option);
    const auto immediate=service::execution_detail::residual_effect(work_,option);
    if(utility || immediate.damage<=0)
    {
        const auto basics=service::detail::basic_targets(option,1);
        if(basics.empty() || !service::apply_basic(option,basics.front()))return;
    }
    const auto final=service::next_camp_volley(option);
    const float external=static_cast<float>(in_.job.external_work_raw*work_.body.camp_damage_multiplier);
    const auto effect=service::execution_detail::residual_effect(work_,option);
    if(!initial.complete || !final.complete || !(effect.damage>0) ||
       work_.body.hp<=initial.hp_damage+external || work_.body.hp<=final.hp_damage+external)return;
    const service::execution_detail::Score score{effect,final.hp_damage+external,immediate.damage>0};
    if(choice_.status!=service::ExecutionStatus::Ready || service::execution_detail::better(score,score_))
    {choice_.status=service::ExecutionStatus::Ready;choice_.primitive=primitive;score_=score;}
}
inline void Producer::end_turn()
{
    namespace service=mb::service;
    bool future_basic=false;for(std::size_t i=0;i<work_.member_count;++i)
        future_basic|=work_.members[i].hp>0 && work_.body.strength>0 && service::distance(work_.body.pos,work_.members[i].pos)<=work_.body.attack_range;
    if(!turn_changed_ && !future_basic && !service::future_damage_reaches(work_))
    {fail(Status::Unsupported,Reason::NoProgress);return;}
    if(shield_timer_)--shield_timer_;
    if(!tick(1))return;
    const auto volley=service::next_camp_volley(work_);
    const float external=static_cast<float>(in_.job.external_work_raw*work_.body.camp_damage_multiplier);
    // External sources receive no shield/CC credit in this declared family.
    const float loss=volley.hp_damage+external;
    if(!volley.complete || !std::isfinite(loss) || work_.body.hp<=loss)
    {fail(Status::Unsupported,Reason::Survival);return;}
    work_.body.hp-=loss;work_.body.shield-=volley.shield_absorbed;
    refresh_damage();
    for(std::size_t i=0;i<work_.member_count;++i)if(work_.members[i].stun_remaining>0)--work_.members[i].stun_remaining;
    if(!shield_timer_)work_.body.shield=0;turn_open_=false;
}
inline void Producer::work_input()
{
    namespace service=mb::service;
    bool alive=false;for(std::size_t i=0;i<work_.member_count;++i)alive|=work_.members[i].hp>0;
    if(!alive){out_.last_clear=clock_;phase_=Phase::Capture;return;}
    if(!turn_open_)
    {
        if(!(first_ && clock_==in_.self.epoch))
        {work_.body.can_move=work_.body.can_attack=work_.body.can_cast=true;work_.body.moved_this_turn=false;}
        for(std::size_t slot=0;slot<slots;++slot)work_.spells[slot].ready=work_.spells[slot].kind!=service::SpellKind::none && clock_>=ready_[slot];
        turn_open_=true;turn_changed_=false;primitives_=0;candidate_stage_=candidate_member_=candidate_slot_=0;choice_={};return;
    }
    // ONE basic target, ONE unit-cast target or ONE legal representative AoE
    // center per unit. There is no monolithic all-centers selector here.
    if(candidate_stage_==0)
    {
        if(candidate_member_<work_.member_count)
        {const auto& member=work_.members[candidate_member_++];candidate({service::PrimitiveKind::basic,member.id,-1,member.pos},false);return;}
        candidate_stage_=1;candidate_member_=0;return;
    }
    if(candidate_stage_==1 && candidate_slot_<slots)
    {
        const auto slot=candidate_slot_;const auto& spell=work_.spells[slot];
        if(spell.kind==service::SpellKind::none || !spell.ready){++candidate_slot_;candidate_member_=0;return;}
        if(spell.kind==service::SpellKind::reset || spell.kind==service::SpellKind::shield)
        {candidate({service::PrimitiveKind::cast,-1,static_cast<int>(slot),work_.body.pos},true);++candidate_slot_;candidate_member_=0;return;}
        if(candidate_member_<work_.member_count)
        {
            const auto& member=work_.members[candidate_member_++];auto aim=member.pos;
            if(spell.kind==service::SpellKind::aoe)
            {
                aim.x=std::clamp(aim.x,std::max(0,work_.body.pos.x-spell.range),std::min(work_.width-1,work_.body.pos.x+spell.range));
                aim.y=std::clamp(aim.y,std::max(0,work_.body.pos.y-spell.range),std::min(work_.height-1,work_.body.pos.y+spell.range));
            }
            candidate({service::PrimitiveKind::cast,member.id,static_cast<int>(slot),aim},false);return;
        }
        ++candidate_slot_;candidate_member_=0;return;
    }
    if(choice_.status!=service::ExecutionStatus::Ready){end_turn();return;}
    const auto command=choice_.primitive;
    bool applied=false;
    if(command.kind==service::PrimitiveKind::basic)applied=service::apply_basic(work_,command.member_id);
    else
    {
        const auto slot=static_cast<std::size_t>(command.slot);
        if(in_.self.period[slot]>std::numeric_limits<std::uint32_t>::max()-clock_){fail(Status::Unsupported,Reason::Calendar);return;}
        applied=service::apply_cast(work_,command.slot,command.aim,command.member_id);ready_[slot]=clock_+in_.self.period[slot];
        if(work_.spells[slot].kind==service::SpellKind::shield)shield_timer_=static_cast<std::uint32_t>(work_.spells[slot].duration);
    }
    if(!applied){fail(Status::Unsupported,Reason::Word);return;}
    ++out_.commands;turn_changed_=true;++primitives_;
    alive=false;for(std::size_t i=0;i<work_.member_count;++i)alive|=work_.members[i].hp>0;
    if(!alive){out_.last_clear=clock_;phase_=Phase::Capture;return;}
    if(primitives_>=service::max_primitives){end_turn();return;}
    candidate_stage_=candidate_member_=candidate_slot_=0;choice_={};
}
inline void Producer::after_bank()
{
    if(in_.job.bank_policy==BankPolicy::ChargedHome) {
        // Leave the charged stand immediately. Recovery/waiting belongs to
        // the declared quiet home, including a finite non-repeating cash job.
        first_=false;if(in_.job.repeat)out_.assumptions|=FutureRenewal;
        phase_=Phase::Home;return;
    }
    if(!in_.job.repeat){finish();return;}
    first_=false;out_.assumptions|=FutureRenewal;
    if(clock_<release_at_)
    {
        if(in_.job.bank.points.empty()){fail(Status::Incomplete,Reason::Geometry);return;}
        if(!wait(release_at_-clock_,in_.job.bank.points.back().raw_upper))return;
    }
    bool wants_shop=false;
    if(in_.job.purchase_when_home)
    {
        if(!out_.gold_exact || wallet_>std::numeric_limits<std::uint32_t>::max())
        {fail(Status::Incomplete,Reason::Wallet);return;}
        shop_.gold=static_cast<std::uint32_t>(wallet_);
        const auto quote=production_investment::project_shop(shop_);
        if(quote.status!=production_investment::Status::Ready){fail(Status::Unsupported,Reason::Shop);return;}
        wants_shop=quote.calls>0;
    }
    const double next_cost=(leg_charge_[4]+leg_reserve_[4])*work_.body.camp_damage_multiplier;
    const bool needs_heal=static_cast<double>(work_.body.hp)<=next_cost || work_.body.hp<work_.body.max_hp;
    phase_=(wants_shop || needs_heal)?Phase::Home:Phase::Renewal;
}
inline void Producer::shop()
{
    if(in_.job.bank_policy==BankPolicy::ChargedHome){cash_recover();return;}
    const auto& job=in_.job;
    if(!std::isfinite(job.spawn_healing) || job.spawn_healing<0 || job.spawn_healing>1)
    {fail(Status::Unsupported,Reason::Shop);return;}
    if(recovery_inputs_<job.heal_inputs)
    {
        if(!tick(1))return;
        const float healed=production_investment::detail::add(work_.body.hp,
            production_investment::detail::mul(job.spawn_healing,work_.body.max_hp));
        work_.body.hp=std::min(work_.body.max_hp,healed);++recovery_inputs_;
        work_.body.shield=0;shield_timer_=0;refresh_damage();return;
    }
    if(work_.body.hp<work_.body.max_hp){fail(Status::Incomplete,Reason::Survival);return;}
    if(job.purchase_when_home)
    {
        shop_.gold=static_cast<std::uint32_t>(wallet_);
        const auto quote=production_investment::project_shop(shop_);
        if(quote.status!=production_investment::Status::Ready){fail(Status::Unsupported,Reason::Shop);return;}
        if(quote.calls>std::numeric_limits<std::uint32_t>::max()-job.purchase_readback_inputs)
        {fail(Status::Unsupported,Reason::Calendar);return;}
        if(!tick(static_cast<std::uint32_t>(quote.calls)+job.purchase_readback_inputs))return;
        wallet_=quote.gold_left;out_.purchase_calls+=quote.calls;
        for(std::size_t i=0;i<quote.calls;++i)
        {
            const auto& buy=quote.buys[i];auto& family=shop_.families[buy.family];
            if(buy.combined){family.shards_owned=0;++family.result_owned;}else family.shards_owned+=buy.count;
        }
        const float treasure_delta=quote.effects.treasure-shop_.effects.treasure;
        const float prior_gold=gold_multiplier_;gold_multiplier_=production_investment::detail::add(gold_multiplier_,treasure_delta);
        if(prior_gold<=0 && bank_gold_rate_>0){fail(Status::Unsupported,Reason::Shop);return;}
        if(prior_gold>0)bank_gold_rate_=production_investment::detail::mul(bank_gold_rate_/prior_gold,gold_multiplier_);
        shop_.body=quote.projected;shop_.body.hp=shop_.body.max_hp;shop_.effects=quote.effects;
        raw_strength_=quote.projected.strength;raw_focus_=quote.projected.focus;
        work_.body.hp=work_.body.max_hp=quote.projected.max_hp;work_.body.focus=quote.projected.focus;
        const float monster=production_investment::detail::add(1,quote.effects.monster);
        work_.body.strength=production_investment::detail::mul(production_investment::detail::mul(quote.projected.strength,in_.self.basic_scale),monster);
        work_.body.camp_damage_multiplier=production_investment::detail::mul(in_.self.incoming_scale,
            production_investment::detail::mul(1-quote.effects.jungle,1-quote.effects.durability));
        for(std::size_t slot=0;slot<slots;++slot)
        {
            auto& spell=work_.spells[slot];
            if(spell.kind==mb::service::SpellKind::none || spell.kind==mb::service::SpellKind::reset)continue;
            if(!std::isfinite(in_.self.focus_modifier[slot]) || in_.self.focus_modifier[slot]<=0)
            {fail(Status::Unsupported,Reason::Shop);return;}
            spell.damage=production_investment::detail::mul(quote.projected.focus,in_.self.focus_modifier[slot]);
            if(spell.kind!=mb::service::SpellKind::shield)spell.damage=production_investment::detail::mul(spell.damage,monster);
        }
        out_.assumptions|=FutureReadback;
        refresh_damage();
        out_.last_purchase_readback=clock_;out_.last_projected_basic=work_.body.strength;
    }
    phase_=Phase::Redeploy;
}
inline void Producer::bank_channel()
{
    if(!channel_left_){cash_bank_complete();return;}
    // No future bank credit, future damage or completed endpoint state is
    // invented when the valuation window ends inside the paid channel.
    if(clock_>=in_.horizon){out_.terminal_resources_available=false;finish();return;}
    // One complete all-NPC batch per represented channel input. Shield, CC,
    // sleep and selected-group absence earn no credit in this family.
    if(!wait(1,in_.job.bank.points.back().raw_upper))return;
    --channel_left_;if(!cash_->banked)++cash_->channel_paid;
}
inline void Producer::cash_bank_complete()
{
    const auto bag=out_.carried_gems;
    if(bag>std::numeric_limits<std::uint64_t>::max()-out_.banked_gems_by_H)
    {fail(Status::Unsupported,Reason::Capacity);return;}
    const double bonus=production_investment::detail::mul(static_cast<float>(bag),bank_gold_rate_);
    if(!std::isfinite(bonus)||bonus<0||bonus>std::numeric_limits<std::uint32_t>::max()||
       static_cast<std::uint64_t>(std::floor(bonus))>std::numeric_limits<std::uint64_t>::max()-wallet_)
    {fail(Status::Unsupported,Reason::Wallet);return;}
    out_.banked_gems_by_H+=bag;
    if(!record(EventKind::Bank,clock_-in_.start,0,1,bag))return;
    if(std::floor(bonus)!=bonus)out_.gold_exact=false;
    wallet_+=static_cast<std::uint64_t>(std::floor(bonus));out_.carried_gems=0;out_.last_bank=clock_;
    if(!cash_->banked) {
        cash_->banked=true;cash_->bank_complete=clock_;cash_->hp_after_bank=work_.body.hp;
        cash_->renewal_not_before=release_at_;
    }
    phase_=Phase::AfterBank;
}
inline void Producer::cash_recover()
{
    const auto& job=in_.job;
    if(!std::isfinite(job.spawn_healing)||job.spawn_healing<0||job.spawn_healing>1)
    {fail(Status::Unsupported,Reason::Shop);return;}
    if(work_.body.hp<work_.body.max_hp) {
        if(recovery_inputs_>=job.heal_inputs||job.spawn_healing<=0)
        {fail(Status::Incomplete,Reason::Survival);return;}
        if(!tick(1))return;
        const auto prior=work_.body.hp;
        const auto healed=production_investment::detail::add(prior,
            production_investment::detail::mul(job.spawn_healing,work_.body.max_hp));
        work_.body.hp=std::min(work_.body.max_hp,healed);++recovery_inputs_;
        work_.body.shield=0;shield_timer_=0;refresh_damage();
        if(work_.body.hp<=prior){fail(Status::Incomplete,Reason::NoProgress);return;}return;
    }
    if(!cash_->healed) {
        cash_->healed=true;cash_->heal_complete=clock_;cash_->hp_recovered=work_.body.hp;
    }
    out_.assumptions|=FutureReadback; // actual owner must read its recovered HP
    if(!job.repeat){cash_->first_cash_complete=true;finish();return;}
    phase_=Phase::CashWait;
}
inline void Producer::cash_wait()
{
    // Travel may overlap respawn time, but no waiting is placed at a charged
    // perch. Departure from quiet home is delayed until paid redeployment can
    // arrive at/after the nominal renewal date. Actual Life remains an enabler.
    const auto duration=leg_ticks_[6];
    const auto earliest=release_at_>duration?release_at_-duration:0;
    if(clock_<earliest&&!tick(earliest-clock_))return;
    if(!cash_->departed){cash_->departed=true;cash_->departure=clock_;}
    phase_=Phase::Redeploy;
}
inline void Producer::finish()
{
    out_.status=Status::Ready;out_.reason=Reason::None;out_.complete=out_.supported=true;
    out_.disjoint_supported=in_.job.capturers==1;
    out_.owned_gold=wallet_;out_.terminal_body={work_.body.hp,work_.body.max_hp,raw_strength_,raw_focus_};phase_=Phase::Done;
}
inline void Producer::unit()
{
    const auto& job=in_.job;
    switch(phase_)
    {
    case Phase::Validate:validate();break;
    case Phase::Initial:if(travel(0,job.initial))phase_=Phase::Start;break;
    case Phase::Start:begin_work();break;
    case Phase::Work:work_input();break;
    case Phase::Capture:
        if(travel(1,job.capture) && tick(job.capture_payout_inputs))phase_=Phase::Payout;break;
    case Phase::Payout:
    {
        const std::uint64_t gems=job.gems/job.capturers+(job.capture_rank==0?job.gems%job.capturers:0);
        if(gems>std::numeric_limits<std::uint64_t>::max()-out_.carried_gems){fail(Status::Unsupported,Reason::Capacity);break;}
        out_.carried_gems+=gems;out_.last_payout=clock_;++out_.cycles;
        if(!record(EventKind::Payout,clock_-in_.start,0,1,gems))break;
        const auto base_gold=job.gold/job.capturers+(job.capture_rank==0?job.gold%job.capturers:0);
        const double gold=production_investment::detail::mul(static_cast<float>(base_gold),gold_multiplier_);
        if(!std::isfinite(gold) || gold>std::numeric_limits<std::uint32_t>::max())
        {fail(Status::Unsupported,Reason::Wallet);break;}
        if(std::floor(gold)!=gold)out_.gold_exact=false;
        wallet_+=static_cast<std::uint64_t>(std::floor(gold));phase_=Phase::Release;break;
    }
    case Phase::Release:
        if(travel(2,job.release))
        {
            out_.last_release=clock_;
            if(job.respawn_inputs>std::numeric_limits<std::uint32_t>::max()-out_.last_payout || clock_==std::numeric_limits<std::uint32_t>::max())
            {fail(Status::Unsupported,Reason::Calendar);break;}
            release_at_=std::max(clock_+1,out_.last_payout+job.respawn_inputs);phase_=Phase::Bank;
        }break;
    case Phase::Bank:
    {
        if(!travel(3,job.bank))break;
        // Movement/refusal payment does not fund the following N channel inputs.
        // This initial bank family admits only immutable ALL-NPC P0 endpoints;
        // no current sleep, selected-group Reward or temporary shield can turn
        // a positive upper into a harmless complete channel. A future charged
        // channel family must price every camp batch and shield expiry itself.
        if(job.bank_policy==BankPolicy::QuietOnly&&out_.carried_gems && job.bank.points.back().raw_upper>0)
        {fail(Status::Unsupported,Reason::BankHazard);break;}
        const double duration=job.submit_multiplier>0?
            std::ceil(production_investment::detail::mul(static_cast<float>(out_.carried_gems),job.submit_multiplier)):
            static_cast<double>((out_.carried_gems+job.submit_divisor-1)/job.submit_divisor);
        if(!std::isfinite(duration) || duration<0 || duration>std::numeric_limits<std::uint32_t>::max()-job.submit_base)
        {fail(Status::Unsupported,Reason::Calendar);break;}
        const auto ticks=out_.carried_gems?std::max<std::uint64_t>(job.submit_minimum,
            static_cast<std::uint64_t>(duration)+job.submit_base):0;
        if(ticks>std::numeric_limits<std::uint32_t>::max()){fail(Status::Unsupported,Reason::Calendar);break;}
        if(job.bank_policy==BankPolicy::ChargedHome) {
            channel_left_=static_cast<std::uint32_t>(ticks);work_.body.shield=0;shield_timer_=0;
            if(!cash_->channel_started) {
                cash_->channel_started=true;cash_->bank_arrival=clock_;cash_->channel_ticks=channel_left_;
                cash_->hp_at_bank=work_.body.hp;cash_->bank_raw_upper=job.bank.points.back().raw_upper;
            }
            phase_=Phase::BankChannel;break;
        }
        if(!tick(static_cast<std::uint32_t>(ticks)))break;
        out_.banked_gems_by_H+=out_.carried_gems;
        if(!record(EventKind::Bank,clock_-in_.start,0,1,out_.carried_gems))break;
        const double bonus=production_investment::detail::mul(static_cast<float>(out_.carried_gems),bank_gold_rate_);
        if(!std::isfinite(bonus) || bonus>std::numeric_limits<std::uint32_t>::max()){fail(Status::Unsupported,Reason::Wallet);break;}
        if(std::floor(bonus)!=bonus)out_.gold_exact=false;
        wallet_+=static_cast<std::uint64_t>(std::floor(bonus));out_.carried_gems=0;out_.last_bank=clock_;phase_=Phase::AfterBank;break;
    }
    case Phase::AfterBank:after_bank();break;
    case Phase::Home:if(travel(5,job.home)){
        recovery_inputs_=0;
        if(cash_&&!cash_->home_reached){cash_->home_reached=true;cash_->home_arrival=clock_;cash_->hp_at_home=work_.body.hp;}
        phase_=Phase::Shop;
    }break;
    case Phase::Shop:shop();break;
    case Phase::Redeploy:if(travel(6,job.redeploy)){
        if(cash_&&!cash_->redeployed){cash_->redeployed=true;cash_->redeploy_complete=clock_;cash_->first_cash_complete=true;}
        phase_=Phase::Start;
    }break;
    case Phase::Renewal:if(travel(4,job.renewal))phase_=Phase::Start;break;
    case Phase::Done:break;
    case Phase::BankChannel:bank_channel();break;
    case Phase::CashWait:cash_wait();break;
    }
}
inline Status Producer::advance(std::size_t units)
{
    last_work_=0;
    while(last_work_<units && out_.status==Status::NeedCompute)
    {++last_work_;++out_.work_used;unit();}
    return out_.status;
}
struct Rebased {
    Status status{Status::Incomplete};Reason reason{Reason::Dates};Key key;
    std::uint32_t start{},horizon{};std::uint64_t banked_gems_by_H{},carried_gems{};
};
inline Rebased rebase(const Quote& quote,std::uint32_t start,std::uint32_t horizon,Key current)
{
    Rebased result;result.key=current;result.start=start;result.horizon=horizon;
    if(quote.status!=Status::Ready || !quote.shift_invariant || start<quote.start || start>horizon ||
       horizon-start>quote.horizon-quote.start || current.actor!=quote.key.actor || current.group!=quote.key.group ||
       current.body_revision!=quote.key.body_revision || current.geometry_revision!=quote.key.geometry_revision ||
       current.word_revision!=quote.key.word_revision)return result;
    std::uint64_t payouts=quote.initial_carried,banks=0;
    for(const auto& event:quote.events)
    {
        const auto span=horizon-start;if(event.offset>span)continue;
        const std::uint64_t count=event.period?std::min<std::uint64_t>(event.count,1u+(span-event.offset)/event.period):event.count;
        auto& value=event.kind==EventKind::Bank?banks:payouts;
        if(event.gems && count>(std::numeric_limits<std::uint64_t>::max()-value)/event.gems)
        {result.status=Status::Unsupported;result.reason=Reason::Capacity;return result;}
        value+=count*event.gems;
    }
    if(banks>payouts){result.status=Status::Unsupported;result.reason=Reason::Calendar;return result;}
    result.banked_gems_by_H=banks;result.carried_gems=payouts-banks;result.status=Status::Ready;result.reason=Reason::None;return result;
}
} // namespace tt::production_job
