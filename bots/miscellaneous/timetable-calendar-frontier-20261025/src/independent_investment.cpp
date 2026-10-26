// One conditional named Ryth capital cycle. Forecasts nominate; only fresh SDK
// observations authorize recall, purchase and each ordinary return step.
#include "runtime.h"
#include "capital_execution.h"
#include "production_potential.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace tt {
namespace {
namespace pi=production_investment;
namespace ce=capital_execution;
using ce::Phase;
using ce::Reason;
constexpr vec2 compass[]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
constexpr std::uint16_t unreachable=cambc::distance_field::unreachable;
struct Capital {
    Phase phase{Phase::Idle};
    ce::Key key;
    ce::Negative negative;
    pi::CalendarInput input;
    pi::Result nomination;
    std::optional<pi::Valuation> value;
    std::uint64_t generation{};
    int slot{-1};
    vec2 perch{},capture{};
    ce::ReturnClass return_class{ce::ReturnClass::Quiet};
    ce::ReturnWord return_word;
    ce::ReturnProgress return_progress;
    bool return_planned{},integer_sources{true};
    const mb::production::potential::Field* immutable_field{};
    Reason retreat_reason{Reason::None};
    std::uint32_t retreat_steps{},outbound_steps{};
    double last_required_hp{};
    bool geometry_ready{},shop_metadata{},readback_pending{},projection_matches{true};
    int geometry_group{-1};
    vec2 geometry_perch{-1,-1};
    std::size_t draw_group{},draw_member{};
    std::optional<cambc::bitboard> all_danger,external_danger,return_walls,service_walls;
    std::shared_ptr<cambc::distance_field> return_native,capture_native,bank_native;
    std::vector<std::uint16_t> return_dist,capture_dist,bank_dist;
    std::uint32_t spawn_return{},capture_walk{},bank_walk{};
    std::uint32_t actual_calls{},gold_spent{},last_gold{},decision_round{},recall_round{},shop_waits{};
    float focus_before{},focus_after{},last_shop_hp{};
    std::array<std::uint32_t,pi::family_count> expected_shards{},expected_results{};
};
Capital& capital() { static Capital state;return state; }
bool equal(vec2 a,ce::Point b) { return a.x==b.x && a.y==b.y; }
bool quiet(const mb::production::potential::Field& field,vec2 point) {
    const auto charge=field.at({point.x,point.y});
    return charge.status==mb::production::potential::Status::Ready && charge.upper==0;
}
std::uint16_t distance_at(const std::vector<std::uint16_t>& field,vec2 point) {
    const auto& world=World::get();
    if(point.x<0 || point.y<0 || point.x>=world.width || point.y>=world.height)return unreachable;
    const auto index=static_cast<std::size_t>(point.y)*world.width+point.x;
    return index<field.size()?field[index]:unreachable;
}
ce::Outcome outcome(const Capital& state,bool handled=true) {
    ce::Outcome result;result.handled=handled;result.group=state.key.group;
    result.perch={state.perch.x,state.perch.y};result.generation=state.generation;
    result.decision_round=state.decision_round;
    result.quoted_calls=static_cast<std::uint32_t>(state.nomination.shop.calls);
    result.actual_calls=state.actual_calls;result.gold_spent=state.gold_spent;
    result.focus_before=state.focus_before;result.focus_after=state.focus_after;
    result.projected_focus=state.nomination.shop.projected.focus;
    result.same_work_time_saved=state.nomination.same_work_time_saved;
    result.projection_matches=state.projection_matches;
    result.return_class=state.return_class;
    result.return_steps=state.return_word.valid?static_cast<std::uint32_t>(state.return_word.count-1):0;
    result.return_exposure=state.return_word.valid?state.return_word.suffix[0]:0;
    result.return_reserve=state.return_word.valid?state.return_word.reserve[0]:0;
    result.return_steps_completed=state.outbound_steps;
    result.return_required_hp=state.last_required_hp;
    result.retreat_steps=state.retreat_steps;
    if(state.retreat_reason!=Reason::None)result.reason=state.retreat_reason;
    return result;
}
ce::Outcome stop(Capital& state,Reason reason,vec2 actual) {
    // Preserve ownership of an actual, still-matching exposed prefix. Withdraw
    // immediately through its cheap reverse word before any full-world observer.
    // No planning claim supplies live permission: reentry refreshes every guard.
    if(state.phase==Phase::Return && state.return_planned && state.immutable_field &&
       state.return_progress.index>0 && state.return_progress.index<state.return_word.count &&
       equal(actual,state.return_word.points[state.return_progress.index])) {
        const auto charge=state.immutable_field->at({actual.x,actual.y});
        if(charge.status==mb::production::potential::Status::Ready && charge.upper>0) {
            state.retreat_reason=reason;state.phase=Phase::Retreat;
            return step_capital(*state.immutable_field);
        }
    }
    auto result=outcome(state,false);
    if(state.retreat_reason!=Reason::None) {
        result.reason=state.retreat_reason;result.recovery_reason=reason;
        result.recovered=reason==Reason::None && state.immutable_field &&
            quiet(*state.immutable_field,actual);
    } else result.reason=reason;
    const bool economic_rejection=static_cast<int>(state.phase)<=static_cast<int>(Phase::Recall) &&
        (reason==Reason::NoProfit || reason==Reason::Unsupported || reason==Reason::Computation);
    if(economic_rejection)state.negative.remember(state.key,reason);
    else state.negative.clear(); // enemy/occupancy/channel withdrawals are transient facts
    // Rejecting this capital family leaves the ordinary unfinished job intact.
    // Physical guard failures still request an ordinary fresh resolving handoff.
    result.aborted=!economic_rejection;
    result.actual={actual.x,actual.y};state.phase=Phase::Idle;state.value.reset();
    if(budget(10000))std::printf("capital abort g%d reason%d recovery%d reverse%u calls%u spent%u nominal_calls%zu saved%lld keep%u invest%u\n",
        state.key.group,static_cast<int>(result.reason),static_cast<int>(result.recovery_reason),state.retreat_steps,
        state.actual_calls,state.gold_spent,
        state.nomination.shop.calls,static_cast<long long>(state.nomination.same_work_time_saved),
        state.nomination.keep.bank_round[0],state.nomination.invest.bank_round[0]);
    return result;
}
pi::Passive passive(std::optional<cambc::item_passive> value) {
    if(!value)return pi::Passive::None;
    if(*value==cambc::item_passive::monster)return pi::Passive::Monster;
    if(*value==cambc::item_passive::jungle)return pi::Passive::Jungle;
    if(*value==cambc::item_passive::durability)return pi::Passive::Durability;
    if(*value==cambc::item_passive::treasure)return pi::Passive::Treasure;
    if(*value==cambc::item_passive::home)return pi::Passive::Home;
    return pi::Passive::Unsupported;
}
pi::Item item(cambc::item_type kind,const cambc::item_info& info) {
    return {static_cast<std::uint32_t>(kind),info.cost,info.combine_count,
        {info.health_points,info.strength_points,info.focus_points},
        passive(info.passive),info.passive_magnitude,info.purchasable};
}
bool collect_shop(Capital& state,int epoch) {
    if(!budget(26000))return false;
    auto& actor=cambc::self();
    const auto counts=actor.item_count();
    const auto design=actor.base_stats();
    if(design.camp_damage_multiplier!=1)return false;
    auto& shop=state.input.shop;shop={};
    shop.gold=actor.gold_count();
    shop.body={actor.health(),actor.max_health(),actor.strength(),actor.focus()};
    shop.growth={design.health_growth,design.strength_growth,design.focus_growth};
    shop.strength_per_bonus_health=cambc::constants::passive_abilities::ryth_health_strength_ratio;
    using I=cambc::item_type;
    const std::array<I,pi::family_count> shards={I::monster_shard_of_focus,I::jungle_shard_of_focus,
        I::durability_shard_of_focus,I::treasure_shard_of_focus,I::power_shard_of_focus};
    for(std::size_t i=0;i<shards.size();++i) {
        if(!budget(6000))return false;
        const auto info=shards[i].info();auto& family=shop.families[i];
        family.shard=item(shards[i],info);family.shards_owned=counts[shards[i]];
        if(info.result) {
            family.has_result=true;family.result=item(*info.result,info.result->info());
            family.result_owned=counts[*info.result];
        }
    }
    std::uint32_t index=0;
    for(const auto count:counts) {
        if(count) {
            if(!budget(5000))return false;
            const auto info=cambc::item_type{index}.info();
            if(!pi::detail::activate(shop.effects,passive(info.passive),info.passive_magnitude))return false;
        }
        ++index;
    }
    // Declared FUTURE operator and conditional projection premises, not a claim
    // that the actor is currently at spawn or future work is reserved.
    shop.metadata_complete=true;shop.shop_at_declared_spawn=true;
    shop.stable_raw_stat_projection=true;
    state.shop_metadata=true;
    return current(epoch);
}
struct Frame {
    vec2 pos{-1,-1};
    cambc::champion_state flags;
    float hp{},maximum{},focus{},strength{};
    std::uint32_t gold{},gems{};
    bool spawn{},recalling{},channel{};
};
Reason frame(const mb::production::potential::Field& field,Frame& value,int epoch,bool charged_return=false) {
    auto& actor=cambc::self();value.flags=actor.state();
    if(!value.flags.contains(cambc::champion_state::alive))return Reason::ActorUnavailable;
    value.pos=actor.position();value.hp=actor.health();value.maximum=actor.max_health();
    value.focus=actor.focus();value.strength=actor.strength();value.gold=actor.gold_count();value.gems=actor.gem_count();
    value.recalling=value.flags.contains(cambc::champion_state::recalling);
    value.channel=value.recalling || value.flags.contains(cambc::champion_state::submitting_gems) ||
        value.flags.contains(cambc::champion_state::teleporting);
    value.spawn=World::get().own_spawn.is_set(value.pos);
    if(actor.position()!=value.pos || !current(epoch))return Reason::Stale;
    if(!std::isfinite(value.hp) || value.hp<=0 || value.gems)return Reason::ActorUnavailable;
    if(!cambc::vision::champions(cambc::team::enemy).empty())return Reason::Enemy;
    const auto buffs=actor.buffs();
    if(buffs.has_stun() && buffs.get<cambc::buff_stun>().duration>0)return Reason::ActorUnavailable;
    if(actor.position()!=value.pos || !current(epoch))return Reason::Stale;
    const auto charge=field.at({value.pos.x,value.pos.y});
    if(charge.status!=mb::production::potential::Status::Ready ||
       !std::isfinite(charge.upper) || charge.upper<0 ||
       (!charged_return && charge.upper!=0))return Reason::Changed;
    return Reason::None;
}
// Refresh actual owned passives. Metadata is immutable; inventory and the raw
// body are actual per-input observations. Conservative return charges deliberately
// do not credit mitigation; [0,1] establishes that raw integer NPC Strength bounds
// each hit. Unexpected amplification or unsupported metadata withdraws this class.
std::optional<double> actual_incoming_multiplier() {
    auto& actor=cambc::self();
    const auto counts=actor.item_count();
    static std::vector<std::optional<cambc::item_info>> metadata(counts.size());
    if(counts.size()!=metadata.size() || actor.base_stats().camp_damage_multiplier!=1)return {};
    float durability=0,jungle=0;std::size_t index=0;
    for(const auto count:counts) {
        const auto slot=index++;
        if(!count)continue;
        if(!metadata[slot]) {
            if(!budget(8000))return {};
            metadata[slot]=cambc::item_type{static_cast<cambc::item_type::enum_type>(slot)}.info();
        }
        const auto& item=*metadata[slot];
        if(item.passive!=cambc::item_passive::durability && item.passive!=cambc::item_passive::jungle)continue;
        if(!std::isfinite(item.passive_magnitude) || item.passive_magnitude<0 || item.passive_magnitude>1)return {};
        if(item.passive==cambc::item_passive::durability)durability=std::max(durability,item.passive_magnitude);
        else jungle=std::max(jungle,item.passive_magnitude);
    }
    const float actual=(1.0f-durability)*(1.0f-jungle);
    if(!std::isfinite(actual) || actual<0 || actual>1)return {};
    return static_cast<double>(actual);
}

// Named ordinary route from the ACTUAL recall landing. Each destination is one
// future End/next-round camp interval. Static sources are all assumed awake and
// available. There is no attack/dash/spell in this word; ordinary Move itself
// cannot trigger damage retaliation (GameRules122,159,163,739). Visible foes and
// occupancy remain freshly guarded, conditional trip withdrawals.
bool plan_return(Capital& state,const mb::production::potential::Field& field,
                 const Frame& now,int epoch) {
    if(!now.spawn || !quiet(field,now.pos) || !budget(34000))return false;
    const auto length=distance_at(state.return_dist,now.pos);
    // Finite native class: at most127 paid steps (128 recorded trail nodes). A larger static route is
    // Unsupported, not a claim that the destination is impossible.
    if(length==unreachable || length>127 || length>ce::max_return_steps)return false;
    std::array<ce::Point,ce::max_return_steps+1> points{};
    std::array<double,ce::max_return_steps+1> raw{};
    vec2 point=now.pos;points[0]={point.x,point.y};
    auto charge=field.at({point.x,point.y});
    if(charge.status!=mb::production::potential::Status::Ready)return false;
    raw[0]=charge.upper;
    for(std::uint32_t step=1;step<=length;++step) {
        bool found=false;
        for(const auto delta:compass) {
            const vec2 next{point.x+delta.x,point.y+delta.y};
            if(distance_at(state.return_dist,next)!=length-step)continue;
            point=next;found=true;break;
        }
        if(!found)return false;
        charge=field.at({point.x,point.y});
        if(charge.status!=mb::production::potential::Status::Ready)return false;
        points[step]={point.x,point.y};raw[step]=charge.upper;
    }
    if(point!=state.perch || !current(epoch) ||
       !state.return_word.build(points,raw,static_cast<std::size_t>(length)+1,
                                World::get().width,World::get().height))return false;
    if(state.return_class==ce::ReturnClass::Quiet && state.return_word.reserve[0]!=0)return false;
    state.return_progress={};state.return_planned=true;
    return true;
}

bool same_inventory(const ce::Key& key) {
    const auto counts=cambc::self().item_count();
    if(counts.size()!=key.inventory_count)return false;
    std::size_t i=0;for(const auto count:counts)if(key.inventory[i++]!=count)return false;
    return true;
}
bool refresh_job(Capital& state,int epoch) {
    auto& job=state.input.jobs[0];const auto& world=World::get();
    if(state.key.group<0 || static_cast<std::size_t>(state.key.group)>=world.groups.size())return false;
    const auto& group=world.groups[static_cast<std::size_t>(state.key.group)];
    if(group.members.size()!=job.member_count)return false;
    try {
        const auto life=world.handles[static_cast<std::size_t>(state.key.group)].state();
        if(!std::holds_alternative<cambc::map::camp_group_alive>(life))return false;
        bool living=false;
        for(std::size_t i=0;i<group.members.size();++i) {
            if(!budget(8000))return false;
            const auto& camp=world.camp(state.key.group,static_cast<int>(i));
            const auto life_now=camp.state();
            const float hp=life_now.is_dead()?0.0f:camp.health();
            if(!std::isfinite(hp) || hp<0)return false;
            // Current-state admission is refreshed; future persistence remains a
            // separately recorded conditional assumption during the capital trip.
            job.members[i]={{group.members[i].pos.x,group.members[i].pos.y},hp,true};
            state.key.member_hp[i]=hp; // negative result belongs to these actual refreshed inputs
            living|=hp>0;
            if(!current(epoch))return false;
        }
        if(!living)return false;
    } catch(const cambc::error& error) {
        if(error.code()!=cambc::error_code::api_not_in_vision)throw;
        return false; // stop after one withdrawn visibility witness
    }
    job.observed_round=static_cast<std::uint32_t>(epoch);
    job.available_from=job.observed_round;
    return current(epoch);
}
bool export_field(const std::shared_ptr<cambc::distance_field>& native,std::vector<std::uint16_t>& out,int epoch) {
    if(!native || !budget(26000))return false;
    auto values=native->export_linear_buffer();
    const auto& world=World::get();
    if(!current(epoch) || values.size()!=static_cast<std::size_t>(world.width)*world.height)return false;
    out=std::move(values);return true;
}
bool select_capture(Capital& state) {
    vec2 point=state.perch;auto remaining=distance_at(state.capture_dist,point);
    // Declared local ordinary-capture family. Larger geodesic detours remain
    // Unsupported, never "unprofitable" or physically impossible.
    if(remaining==unreachable || remaining>128)return false;
    state.capture_walk=remaining;
    for(std::uint32_t n=remaining;n>0;--n) {
        if(!budget(8000))return false;
        bool found=false;
        for(const auto delta:compass) {
            const vec2 next{point.x+delta.x,point.y+delta.y};
            if(distance_at(state.capture_dist,next)==n-1) {point=next;found=true;break;}
        }
        if(!found)return false;
    }
    state.capture=point;return true;
}
void expected_purchase(Capital& state,const pi::Buy& buy) {
    if(buy.family>=pi::family_count) {state.projection_matches=false;return;}
    auto& shard=state.expected_shards[buy.family];auto& result=state.expected_results[buy.family];
    if(buy.combined) {shard=0;++result;}else shard+=buy.count;
}
void readback(Capital& state,const Frame& now) {
    const auto counts=cambc::self().item_count();
    for(std::size_t i=0;i<pi::family_count;++i) {
        const auto& family=state.input.shop.families[i];
        if(counts[cambc::item_type{family.shard.id}]!=state.expected_shards[i] ||
           (family.has_result && counts[cambc::item_type{family.result.id}]!=state.expected_results[i]))
            state.projection_matches=false;
    }
    // Actual spend was sampled immediately after the accepted buy, within its
    // owner input. A later wallet change is a projection discrepancy, not a
    // second purchase debit or proof that no intervening income occurred.
    if(now.gold!=state.last_gold)state.projection_matches=false;
    state.focus_after=now.focus;state.readback_pending=false;
}
ce::Outcome value_and_recall(Capital& state,const mb::production::potential::Field& field,const Frame& now,int epoch) {
    if(!budget(30000))return outcome(state);
    if(!collect_shop(state,epoch) || !refresh_job(state,epoch))
        return stop(state,Reason::Changed,now.pos);
    auto& input=state.input;auto& job=input.jobs[0];
    auto& actor=cambc::self();const auto ability=actor.abilities()[static_cast<std::size_t>(state.slot)];
    input.now=static_cast<std::uint32_t>(epoch);input.deadline=cambc::constants::max_rounds;
    input.query_generation=state.generation;input.remaining_cooldown=static_cast<std::uint32_t>(std::max(0,ability.cooldown()));
    input.carried_gems=now.gems;input.quiet_decision=true;input.no_pending_capture_or_channel=!now.channel;
    input.stable_outgoing_multiplier=1;input.stable_outgoing_supported=true;input.job_count=1;
    input.recall_to_first_shop_ready=static_cast<std::uint32_t>(std::max(1.0,
        std::ceil(cambc::constants::recall_time*(1.0-input.shop.effects.home))));
    job.observed_round=input.now;job.available_from=input.now;job.valid_through=input.deadline;
    job.ingress_keep=now.flags.contains(cambc::champion_state::can_cast)?0u:1u;
    // Recall destination is not predicted. The maximum declared spawn distance
    // conservatively prices the actual retained word, plus one route-planning
    // input and one fresh unmoved input after the final accepted ordinary move.
    job.ingress_shop=state.spawn_return+2;
    // Paid slots of the retained ordinary controller: three capture-planning
    // inputs, and a four-input bank handoff/submit allowance. These are protocol
    // costs in this conditional calendar, not a claim of optimal movement.
    job.damage_to_capture=state.capture_walk+3;
    job.capture_payout=static_cast<std::uint32_t>(cambc::constants::capture_time+1);
    const auto submit=std::max(cambc::constants::submit_gem_minimum_duration,
        static_cast<int>(std::ceil(job.gems*cambc::constants::submit_gem_duration_multiplier))+
        cambc::constants::submit_gem_base_duration);
    job.payout_to_bank=state.bank_walk+4+static_cast<std::uint32_t>(submit);
    job.queried_current_damage=ability.damage_amount();
    if(pi::detail::mul(input.shop.body.focus,job.cast_modifier)!=job.queried_current_damage)
        return stop(state,Reason::Unsupported,now.pos);
    state.value.emplace(input);
    while(budget(12000) && state.value->status()==pi::Status::NeedCompute) {
        state.value->advance(pi::max_advance_units);
        if(!current(epoch))return stop(state,Reason::Stale,now.pos);
    }
    if(state.value->status()==pi::Status::NeedCompute)
        return stop(state,Reason::Computation,now.pos); // this declared finite fuel class, not no-profit
    state.nomination=state.value->result();state.value.reset();
    if(state.nomination.status!=pi::Status::Ready)
        return stop(state,Reason::Unsupported,now.pos);
    if(state.nomination.decision!=pi::Decision::EarlierSameWork &&
       state.nomination.decision!=pi::Decision::InvestByDeadline)
        return stop(state,Reason::NoProfit,now.pos);
    // Revalidate current recall authority. The dated economic output never
    // grants movement/channel permission, nor does it reserve the named job.
    if(!budget(14000))return stop(state,Reason::Computation,now.pos);
    Frame fresh;const auto guard=frame(field,fresh,epoch);
    if(guard!=Reason::None)return stop(state,guard,fresh.pos);
    if(fresh.channel || fresh.pos!=state.perch || fresh.gold!=input.shop.gold ||
       fresh.focus!=input.shop.body.focus || fresh.strength!=input.shop.body.strength ||
       fresh.maximum!=input.shop.body.max_hp || !same_inventory(state.key))
        return stop(state,Reason::Changed,fresh.pos);
    if(!budget(5000))return stop(state,Reason::Computation,fresh.pos);
    if(cambc::self().position()!=state.perch || !current(epoch))
        return stop(state,Reason::Stale,fresh.pos);
    state.decision_round=static_cast<std::uint32_t>(epoch);
    state.focus_before=fresh.focus;state.focus_after=fresh.focus;
    state.actual_calls=state.gold_spent=0;state.projection_matches=true;
    for(std::size_t i=0;i<pi::family_count;++i) {
        state.expected_shards[i]=input.shop.families[i].shards_owned;
        state.expected_results[i]=input.shop.families[i].result_owned;
    }
    // These two future claims are ASSUMPTIONS of this conditional nomination.
    // Actual enemy/collision/lifecycle guards can abort the later native trip.
    std::printf("capital conditional g%d r%d saved%lld calls%zu focus%.3f->%.3f persist_assumed1 displacement_assumed0 foe_displacement_assumed0 ally_changes_assumed0 returnclass%d travel%u\n",
        state.key.group,epoch,static_cast<long long>(state.nomination.same_work_time_saved),
        state.nomination.shop.calls,static_cast<double>(fresh.focus),
        static_cast<double>(state.nomination.shop.projected.focus),
        static_cast<int>(state.return_class),state.spawn_return);
    if(!budget(3000))return stop(state,Reason::Computation,fresh.pos);
    if(cambc::self().position()!=state.perch || !current(epoch))
        return stop(state,Reason::Stale,fresh.pos);
    cambc::self().recall();
    if(!current(epoch))return stop(state,Reason::Stale,fresh.pos);
    state.recall_round=static_cast<std::uint32_t>(epoch);state.phase=Phase::AwaitRecall;
    return outcome(state);
}
ce::Outcome shop_step(Capital& state,const Frame& now,int epoch) {
    if(!now.spawn || now.channel)return stop(state,Reason::ActorUnavailable,now.pos);
    if(state.readback_pending)readback(state,now);
    state.focus_after=now.focus;
    if(state.actual_calls>=state.nomination.shop.calls) {
        if(now.hp<now.maximum) {
            if(now.hp<=state.last_shop_hp && ++state.shop_waits>2)
                return stop(state,Reason::Readback,now.pos);
            state.last_shop_hp=now.hp;return outcome(state); // actual spawn recovery, never projected healing
        }
        if(now.focus!=state.nomination.shop.projected.focus ||
           now.maximum!=state.nomination.shop.projected.max_hp ||
           now.strength!=state.nomination.shop.projected.strength ||
           now.gold!=state.nomination.shop.gold_left)state.projection_matches=false;
        state.phase=Phase::Return;
        std::printf("capital shop g%d calls%u spent%u focus%.3f expected%.3f matched%d\n",
            state.key.group,state.actual_calls,state.gold_spent,static_cast<double>(now.focus),
            static_cast<double>(state.nomination.shop.projected.focus),state.projection_matches);
        return outcome(state);
    }
    if(!budget(15000))return outcome(state);
    Observation current_frame;current_frame.status=Status::Ready;
    current_frame.epoch=epoch;current_frame.pos=now.pos;
    state.last_gold=now.gold;const auto status=purchase(current_frame);
    if(!current(epoch))return stop(state,Reason::Stale,now.pos);
    if(status==Status::NeedObservation) {
        const auto after_gold=cambc::self().gold_count();
        if(!current(epoch))return stop(state,Reason::Stale,now.pos);
        if(after_gold<=now.gold)state.gold_spent+=now.gold-after_gold;
        else state.projection_matches=false;
        state.last_gold=after_gold;
        expected_purchase(state,state.nomination.shop.buys[state.actual_calls]);
        ++state.actual_calls;state.readback_pending=true;return outcome(state);
    }
    if(status==Status::NeedCompute)return outcome(state);
    if(status==Status::Stale)return stop(state,Reason::Stale,now.pos);
    // Fresh actual operator had no buy, although the dated projection expected
    // one. Do not retry forever or invent the missing item; return actual state.
    state.projection_matches=false;
    state.phase=Phase::Return; // preserve the ORIGINAL quoted call count in the receipt
    return outcome(state);
}
} // namespace

bool capital_active() { return capital().phase!=Phase::Idle; }
std::span<const capital_execution::Point> capital_return_prefix() {
    const auto& state=capital();
    if(!state.return_word.valid || !state.return_planned ||
       state.return_progress.index>=state.return_word.count)return {};
    // Historical ACCEPTED ordinary moves only; no proposed future suffix is
    // exported. Caller must compare the actual current point with prefix.back()
    // before importing these edges into its own Trail; displacement revokes it.
    return {state.return_word.points.data(),state.return_progress.index+1};
}

bool try_capital_offer(const Observation& anchor,const WorkObservation& work,
                      const mb::production::potential::Field& field)
{
    auto& state=capital();
    if(state.phase!=Phase::Idle)return true;
    if(anchor.status!=Status::Ready || work.status!=Status::Ready ||
       anchor.epoch!=work.epoch || !current(anchor.epoch) ||
       work.group<0 || work.availability!=Availability::Alive ||
       anchor.channeling || anchor.gems || anchor.gold==0 || anchor.on_spawn ||
       !anchor.enemies.empty() || !quiet(field,anchor.pos))return false;
    auto& actor=cambc::self();
    if(actor.champion_name()!=cambc::champion_type::ryth ||
       actor.position()!=anchor.pos || !current(anchor.epoch))return false;
    const auto& world=World::get();
    if(static_cast<std::size_t>(work.group)>=world.groups.size() ||
       world.camp_tiles.is_set(anchor.pos) || world.spawn_tiles.empty() ||
       world.spawn_tiles.size()>64)return false;
    const auto& group=world.groups[static_cast<std::size_t>(work.group)];
    if(group.members.empty() || group.members.size()>pi::max_members ||
       work.state.member_count!=group.members.size())return false;
    pi::Job job;job.family=pi::Family::StationaryAreaCaster;
    job.group=static_cast<std::uint64_t>(work.group);job.worker={anchor.pos.x,anchor.pos.y};
    job.width=world.width;job.height=world.height;job.member_count=group.members.size();
    ce::Key key;key.group=work.group;key.perch=job.worker;key.gold=anchor.gold;
    key.focus=anchor.focus;key.strength=anchor.raw_strength;
    key.max_hp=anchor.max_hp;key.hp=anchor.hp;key.member_count=job.member_count;
    bool alive=false;
    for(std::size_t i=0;i<job.member_count;++i) {
        const auto& member=work.state.members[i];
        if(!member.hp_exact || member.id!=static_cast<int>(i) || member.group!=work.group ||
           !std::isfinite(member.hp) || member.hp<0)return false;
        if(member.hp>0 && mb::service::distance(work.state.body.pos,member.pos)<=work.state.body.attack_range)
            return false; // this named comparison is fixed AoE service, not omitted free basics
        alive|=member.hp>0;job.members[i]={{member.pos.x,member.pos.y},member.hp,true};
        key.member_hp[i]=member.hp;
    }
    if(!alive || !budget(14000))return false;
    const auto counts=actor.item_count();
    if(counts.size()>key.inventory.size())return false;
    key.inventory_count=counts.size();std::size_t count_index=0;
    for(const auto count:counts)key.inventory[count_index++]=count;
    if(state.negative.matches(key))return false;
    int slot=-1;
    const auto abilities=actor.abilities();
    for(std::size_t i=0;i<abilities.size();++i) {
        if(!abilities[i].info())continue;
        const auto* area=std::get_if<cambc::ability_aoe_damage>(&*abilities[i].info());
        if(!area || !area->valid_targets.contains(cambc::ability_target_mask::camps) ||
           !area->valid_targets.contains(cambc::ability_target_mask::ground) ||
           area->max_cooldown<=0 || !std::isfinite(area->modifier) || area->modifier<=0)continue;
        ce::Point aim;
        if(!ce::fixed_aim(job.members,job.member_count,job.worker,world.width,world.height,
                         area->range,area->radius,aim))continue;
        slot=static_cast<int>(i);job.aim=aim;job.cast_range=area->range;
        job.area_radius=area->radius;job.cast_modifier=area->modifier;
        job.cooldown_period=static_cast<std::uint32_t>(area->max_cooldown);break;
    }
    if(slot<0 || actor.position()!=anchor.pos || !current(anchor.epoch))return false;
    if(state.generation==std::numeric_limits<std::uint64_t>::max())return false;
    ++state.generation;state.key=key;state.perch=anchor.pos;state.slot=slot;
    state.input={};state.input.jobs[0]=job;state.input.jobs[0].generation=state.generation;
    state.input.jobs[0].gems=group.gems;state.input.jobs[0].basic_range=anchor.attack_range;
    state.input.jobs[0].full_group_known=true;state.input.jobs[0].availability_supported=true;
    // Zero incoming names the represented POST-ingress service/capture/bank
    // chain. The return ingress is a separately charged conditional prefix.
    state.input.jobs[0].geometry_supported=true;state.input.jobs[0].zero_incoming_through_continuation=true;
    // Conditional hypotheses, NOT observations of future ownership/availability.
    state.input.jobs[0].team_displacement_accounted=true;
    state.input.jobs[0].residual_persists_while_away=true;
    state.shop_metadata=false;state.value.reset();state.nomination={};
    state.actual_calls=state.gold_spent=0;state.decision_round=0;
    state.readback_pending=false;state.projection_matches=false;state.shop_waits=0;state.last_shop_hp=0;
    state.return_planned=false;state.return_word={};state.return_progress={};
    state.immutable_field=&field;state.retreat_reason=Reason::None;
    state.retreat_steps=state.outbound_steps=0;state.last_required_hp=0;
    state.focus_before=state.focus_after=anchor.focus;
    if(state.geometry_ready && state.geometry_group==work.group &&
       state.geometry_perch==anchor.pos)state.phase=Phase::Value;
    else {
        state.geometry_ready=false;state.draw_group=state.draw_member=0;
        state.return_class=ce::ReturnClass::Quiet;state.integer_sources=true;
        state.all_danger.reset();state.external_danger.reset();
        state.return_native.reset();state.capture_native.reset();state.bank_native.reset();
        state.return_dist.clear();state.capture_dist.clear();state.bank_dist.clear();
        state.phase=Phase::Walls;
    }
    return true;
}

capital_execution::Outcome step_capital(const mb::production::potential::Field& field)
{
    auto& state=capital();
    if(state.phase==Phase::Idle)return outcome(state,false);
    if(!budget(10000)) {
        // Once a charged word has left spawn, insufficient headroom is a
        // resolving withdrawal, never another unguarded stationary interval.
        if((state.phase==Phase::Return || state.phase==Phase::Retreat) && state.return_planned &&
           state.return_progress.index>0 &&
           state.return_progress.index<state.return_word.count) {
            const auto expected=state.return_word.points[state.return_progress.index];
            return stop(state,Reason::Computation,{expected.x,expected.y});
        }
        return outcome(state);
    }
    const int epoch=static_cast<int>(cambc::round_number());Frame now;
    const bool charged_return=(state.phase==Phase::Return || state.phase==Phase::Retreat) &&
        state.return_class==ce::ReturnClass::Charged;
    const auto guard=frame(field,now,epoch,charged_return);
    if(guard!=Reason::None)return stop(state,guard,now.pos);
    if(static_cast<std::uint32_t>(epoch)>=cambc::constants::max_rounds)
        return stop(state,Reason::Changed,now.pos);
    const auto& world=World::get();
    if(static_cast<int>(state.phase)<=static_cast<int>(Phase::Recall)) {
        if(now.pos!=state.perch || now.channel || now.focus!=state.key.focus ||
           now.strength!=state.key.strength || now.maximum!=state.key.max_hp ||
           now.hp!=state.key.hp || now.gold!=state.key.gold)return stop(state,Reason::Changed,now.pos);
    }
    if(state.phase==Phase::Retreat) {
        if(quiet(field,now.pos))return stop(state,Reason::None,now.pos);
        const auto index=state.return_progress.index;
        if(!state.return_word.valid || index==0 || index>=state.return_word.count ||
           !equal(now.pos,state.return_word.points[index]) || now.channel ||
           !state.integer_sources || now.hp>16777216.0f)
            return stop(state,Reason::Changed,now.pos);
        const auto charge=field.at({now.pos.x,now.pos.y});
        const auto multiplier=actual_incoming_multiplier();
        double required{};
        if(!multiplier || charge.status!=mb::production::potential::Status::Ready ||
           charge.upper>state.return_word.raw_upper[index] ||
           !ce::return_detail::add_upper(state.return_word.reverse_prefix[index],
                                        state.return_word.global_reserve,required) ||
           !current(epoch) || !(static_cast<double>(now.hp)>required))
            return stop(state,Reason::Changed,now.pos);
        state.last_required_hp=required;
        if(!now.flags.contains(cambc::champion_state::can_move) || !budget(18000))
            return stop(state,Reason::Computation,now.pos);
        const auto next=state.return_word.points[index-1];
        const vec2 destination{next.x,next.y};
        const auto applied=transit_authority(required,now.pos,destination);
        if(applied.status==Status::Ready && applied.moved) {
            const auto actual_destination=cambc::self().position();
            if(actual_destination!=destination || !current(epoch))
                return stop(state,Reason::Stale,actual_destination);
            --state.return_progress.index;++state.retreat_steps;
            state.return_progress.last_move_input=static_cast<std::uint64_t>(epoch);
            state.return_progress.has_moved=true;
            if(quiet(field,actual_destination)) {
                auto result=stop(state,Reason::None,actual_destination);
                result.moved=true;return result;
            }
            auto result=outcome(state);result.moved=true;return result;
        }
        // This conditional counteraction cannot promise against a newly blocked
        // reverse edge, displacement or a visible foe. Concrete failure hands
        // the current body to the ordinary bounded-risk resolver, once.
        return stop(state,applied.status==Status::Stale?Reason::Stale:
            (applied.status==Status::NeedCompute?Reason::Computation:Reason::Blocked),now.pos);
    }
    if(state.phase==Phase::Walls) {
        if(!state.shop_metadata) {
            if(!budget(30000))return outcome(state);
            if(!collect_shop(state,epoch))return stop(state,Reason::Unsupported,now.pos);
            const auto quote=pi::project_shop(state.input.shop);
            if(quote.status!=pi::Status::Ready)return stop(state,Reason::Unsupported,now.pos);
            if(!quote.calls)return stop(state,Reason::NoProfit,now.pos);
        }
        if(!state.all_danger) {
            state.all_danger.emplace();state.external_danger.emplace();
        }
        while(state.draw_group<world.groups.size() && budget(12000)) {
            const auto& group=world.groups[state.draw_group];
            while(state.draw_member<group.members.size() && budget(12000)) {
                const auto& member=group.members[state.draw_member++];
                if(!std::isfinite(member.strength) || member.strength<0 ||
                   member.strength>16777216.0f || std::floor(member.strength)!=member.strength)
                    state.integer_sources=false;
                if(member.strength>0) {
                    state.all_danger->draw_chebyshev(member.pos,static_cast<float>(member.range));
                    if(static_cast<int>(state.draw_group)!=state.key.group)
                        state.external_danger->draw_chebyshev(member.pos,static_cast<float>(member.range));
                }
                if(!current(epoch))return stop(state,Reason::Stale,now.pos);
            }
            if(state.draw_member!=group.members.size())break;
            ++state.draw_group;state.draw_member=0;
        }
        if(state.draw_group==world.groups.size())state.phase=Phase::QuietField;
        return outcome(state);
    }
    if(state.phase==Phase::QuietField) {
        if(!budget(64000))return outcome(state);
        state.return_walls.emplace(world.terrain | world.camp_tiles);
        if(state.return_class==ce::ReturnClass::Quiet)*state.return_walls |= *state.all_danger;
        cambc::bitboard roots;roots.set(state.perch);
        roots &= ~*state.return_walls;
        if(roots.is_empty())return stop(state,Reason::Unsupported,now.pos);
        cambc::bitboard visited;state.return_native=std::make_shared<cambc::distance_field>();
        state.return_native->compute_to(*state.return_walls,roots,world.own_spawn,visited);
        if(!current(epoch))return stop(state,Reason::Stale,now.pos);
        state.phase=Phase::QuietExport;return outcome(state);
    }
    if(state.phase==Phase::QuietExport) {
        if(!export_field(state.return_native,state.return_dist,epoch))return outcome(state);
        std::uint32_t farthest=0;bool found=false,unreachable_spawn=false;
        for(const auto point:world.spawn_tiles) {
            if(!budget(5000))return stop(state,Reason::Computation,now.pos);
            const auto d=distance_at(state.return_dist,point);
            if(d==unreachable)unreachable_spawn=true;
            else farthest=std::max(farthest,static_cast<std::uint32_t>(d));
            if(!quiet(field,point))return stop(state,Reason::Unsupported,now.pos);
            found=true;
        }
        if(unreachable_spawn && state.return_class==ce::ReturnClass::Quiet) {
            // A zero-exposure route is only one sufficient operator family.
            // Rebuild ordinary known terrain/camp blocking; positive potential
            // will be explicitly charged on its exact named word after recall.
            if(!state.integer_sources)return stop(state,Reason::Unsupported,now.pos);
            state.return_class=ce::ReturnClass::Charged;
            state.return_native.reset();state.return_dist.clear();
            state.phase=Phase::QuietField;return outcome(state);
        }
        if(!found || unreachable_spawn || farthest>127)return stop(state,Reason::Unsupported,now.pos);
        state.spawn_return=farthest;state.phase=Phase::CaptureField;return outcome(state);
    }
    if(state.phase==Phase::CaptureField) {
        if(!budget(64000))return outcome(state);
        auto walls=world.terrain | world.camp_tiles | *state.external_danger;
        cambc::bitboard roots;
        for(const auto& member:world.groups[static_cast<std::size_t>(state.key.group)].members) {
            walls.set(member.pos,false);roots.set(member.pos);
        }
        walls |= world.terrain | *state.external_danger;roots &= ~walls;
        if(roots.is_empty())return stop(state,Reason::Unsupported,now.pos);
        state.service_walls.emplace(std::move(walls));
        cambc::bitboard destination;destination.set(state.perch);cambc::bitboard visited;
        state.capture_native=std::make_shared<cambc::distance_field>();
        state.capture_native->compute_to(*state.service_walls,roots,destination,visited);
        if(!current(epoch))return stop(state,Reason::Stale,now.pos);
        state.phase=Phase::CaptureExport;return outcome(state);
    }
    if(state.phase==Phase::CaptureExport) {
        if(!export_field(state.capture_native,state.capture_dist,epoch))return outcome(state);
        if(!select_capture(state))return stop(state,Reason::Unsupported,now.pos);
        state.phase=Phase::BankField;return outcome(state);
    }
    if(state.phase==Phase::BankField) {
        if(!budget(64000))return outcome(state);
        cambc::bitboard roots;
        for(const auto altar:world.altars)
            roots.draw_chebyshev(altar,static_cast<float>(cambc::constants::submit_gems_range));
        roots &= ~*state.service_walls;
        if(roots.is_empty())return stop(state,Reason::Unsupported,now.pos);
        cambc::bitboard destination;destination.set(state.capture);cambc::bitboard visited;
        state.bank_native=std::make_shared<cambc::distance_field>();
        state.bank_native->compute_to(*state.service_walls,roots,destination,visited);
        if(!current(epoch))return stop(state,Reason::Stale,now.pos);
        state.phase=Phase::BankExport;return outcome(state);
    }
    if(state.phase==Phase::BankExport) {
        if(!export_field(state.bank_native,state.bank_dist,epoch))return outcome(state);
        const auto d=distance_at(state.bank_dist,state.capture);
        if(d==unreachable)return stop(state,Reason::Unsupported,now.pos);
        state.bank_walk=d;state.geometry_ready=true;
        state.geometry_group=state.key.group;state.geometry_perch=state.perch;
        state.phase=Phase::Value;return outcome(state);
    }
    if(state.phase==Phase::Value || state.phase==Phase::Recall)
        return value_and_recall(state,field,now,epoch);
    if(state.phase==Phase::AwaitRecall) {
        if(now.recalling)return outcome(state);
        if(!now.spawn || now.channel)return stop(state,Reason::Cancelled,now.pos);
        state.phase=Phase::Shop;
        return shop_step(state,now,epoch); // first buy may share actual arrival input
    }
    if(state.phase==Phase::Shop)return shop_step(state,now,epoch);
    if(state.phase==Phase::Return) {
        if(now.channel)return stop(state,Reason::ActorUnavailable,now.pos);
        if(!state.return_planned) {
            if(!plan_return(state,field,now,epoch))
                return stop(state,Reason::Computation,now.pos);
            std::printf("capital route g%d class%d steps%zu raw%.3f reserve%.3f conditional1\n",
                state.key.group,static_cast<int>(state.return_class),state.return_word.count-1,
                state.return_word.suffix[0],state.return_word.reserve[0]);
            return outcome(state); // the explicitly priced planning input is on P0 spawn
        }
        if(state.return_class==ce::ReturnClass::Charged &&
           (!state.integer_sources || now.hp>16777216.0f))
            return stop(state,Reason::Unsupported,now.pos);
        const auto multiplier=actual_incoming_multiplier();
        if(!multiplier || !current(epoch))return stop(state,Reason::Changed,now.pos);
        const auto current_charge=field.at({now.pos.x,now.pos.y});
        ce::ReturnObservation actual;
        actual.actual={now.pos.x,now.pos.y};actual.hp=now.hp;
        actual.incoming_multiplier=*multiplier;actual.raw_upper=current_charge.upper;
        actual.input=static_cast<std::uint64_t>(epoch);
        actual.fenced_input=static_cast<std::uint64_t>(cambc::round_number());
        actual.exact=current_charge.status==mb::production::potential::Status::Ready &&
            cambc::self().position()==now.pos && current(epoch);
        const auto checked=ce::check_return(state.return_word,state.return_progress,actual);
        state.last_required_hp=checked.required_hp;
        if(checked.disposition==ce::ReturnDisposition::Arrived) {
            state.phase=Phase::Arrived;return outcome(state);
        }
        if(checked.disposition!=ce::ReturnDisposition::Ready)
            return stop(state,Reason::Changed,now.pos);
        // Never deliberately wait at a positive point. A refusal, unavailable
        // move or low-fuel seam withdraws to the ordinary fresh retreat/recovery
        // handler. One max-volley reserve covers this transition interval; it is
        // not permission for repeated idle inputs or an unseen-foe guarantee.
        if(!now.flags.contains(cambc::champion_state::can_move) || !budget(18000))
            return stop(state,Reason::ActorUnavailable,now.pos);
        const vec2 destination{checked.next.x,checked.next.y};
        const auto applied=transit_authority(checked.required_hp,now.pos,destination);
        if(applied.status==Status::Ready && applied.moved) {
            const auto actual_destination=cambc::self().position();
            if(ce::advance_return(state.return_word,state.return_progress,
                                  {actual_destination.x,actual_destination.y},true,
                                  static_cast<std::uint64_t>(epoch),
                                  static_cast<std::uint64_t>(cambc::round_number()))!=ce::ReturnDisposition::Ready)
                return stop(state,Reason::Stale,actual_destination);
            ++state.outbound_steps;
            if(destination==state.perch)state.phase=Phase::Arrived;
            auto result=outcome(state);result.moved=true;
            return result; // fresh input required before exclusive AoE
        }
        if(applied.status==Status::Stale)return stop(state,Reason::Stale,now.pos);
        return stop(state,applied.status==Status::NeedCompute?Reason::Computation:Reason::Blocked,now.pos);
    }
    if(state.phase==Phase::Arrived) {
        if(now.pos!=state.perch || now.channel)return stop(state,Reason::Changed,now.pos);
        if(state.return_progress.has_moved && state.return_progress.last_move_input==static_cast<std::uint64_t>(epoch))
            return stop(state,Reason::Stale,now.pos);
        // This is a NEW unmoved input. Resolve current named work rather than
        // pretending the old residual was guaranteed to survive our absence.
        if(!refresh_job(state,epoch))return stop(state,Reason::Changed,now.pos);
        state.focus_after=now.focus;
        auto result=outcome(state,false);result.returned=true;result.actual=result.perch;
        result.return_round=static_cast<std::uint32_t>(epoch);
        state.negative.remember(state.key,Reason::Readback);
        std::printf("capital return g%d r%d spent%u focus%.3f matched%d conditional1\n",
            state.key.group,epoch,state.gold_spent,static_cast<double>(state.focus_after),state.projection_matches);
        state.phase=Phase::Idle;return result;
    }
    return stop(state,Reason::Unsupported,now.pos);
}
} // namespace tt
