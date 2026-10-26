#pragma once
#include "capital_tribute.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#ifndef WB_CONVERSION_NATIVE_HOST_FREE
#include <cambc.h>
#endif

// SDK observation/command/receipt layer only. No value weights, priority
// mutation, zero-enemy assumption or automatic authority to wait lives here.
// Call observe_input BEFORE channel/stun/contact early returns. Runtime owns
// the Monitor/token lifetime, including unresolved receipts after bag==0.
namespace wb::capital::conversion_native {
namespace t=tribute;
inline constexpr unsigned max_altars=16,max_enemies=8;
struct Epoch {int owner{-1},round{-1};std::uint64_t input{};};
struct Enemy {
    int id{-1},kind{-1};t::Point position{};float hp{},max_hp{},strength{},focus{};
    int move_speed{},attack_range{};bool alive_known{},alive{},observed_now{},can_move{},can_attack{},can_cast{};
};
struct Packet {
    t::Observation own{};float hp{},max_hp{};int kind{-1},move_speed{},attack_range{},submit_range{};
    unsigned queried_ticks{},restrictions{};bool completed_turn{};
    std::array<Enemy,max_enemies> enemies{};unsigned enemy_count{};
    // Complete IDENTITIES is not complete vision/reply/service coverage.
    bool enemies_complete{};
};
struct Geometry {
    Epoch source{};std::span<const t::Point> altars{};bool altars_queried{};
    void* context{};bool (*static_walkable)(void*,t::Point){};
    std::span<const int> enemy_ids{};bool enemy_ids_complete{};
    bool root_history_known{},exclusive_cast_used{};
};
struct Premises {
    unsigned minimum_alive_cycle_gap{};bool minimum_gap_queried{},bag_loss_model_closed{};
};
struct BankIntent {
    bool valid{};Epoch source{};std::uint64_t sample{},token{},original_amount{},actual_bag{};
    t::Point position{},altar{},landing{};bool move_required{};
    unsigned queried_ticks{};int min_horizon{-1};
};
// True accepts the defined physical policy/branches at one coherent horizon.
// Unknown reply/service costs REMAIN UNKNOWN: a complete economic incumbent
// may be retained conditionally. This is neither numeric coverage of hidden
// replies nor a safety certificate. Never synthesize separate max damage/stun
// into one enemy policy or treat unknown service displacement as cost0.
using Authorize=bool(*)(void*,t::Action,const BankIntent&,const Packet&,const t::Context&,t::Guard&);
using FundFallback=bool(*)(void*,const Packet&,const t::Context&);
struct Policy {void* context{};Authorize authorize{};FundFallback fallback{};};
enum class Status {Ready,NoOffer,NeedModel,NeedObservation,NeedFuel,Stale,Approached,MoveRefused,StartRejected,Started,NeedFallback};
struct Result {
    Status status{Status::NoOffer};bool claimed{},move_attempted{},moved{},submit_attempted{},started{};
    bool fresh{},physical_released{},fallback_required{},fallback_funded{},unknown_effect{};
    Packet actual{};t::Context context{};t::Result receipt{};
};
// Separate empirical command and post-observation obligations. No allowance
// here is a universal SDK fuel proof; enabled native paths still need health.
struct Options {unsigned observation_reserve{9000},approach_reserve{3000},start_reserve{2000},fallback_reserve{5000};};
inline bool same(Epoch a,Epoch b){return a.owner==b.owner&&a.round==b.round&&a.input==b.input;}
inline int distance(t::Point a,t::Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}

template<class Host> class Adapter {
    Host& host_;Options options_;std::uint64_t sample_{};Packet latest_{};t::Context context_{};
    bool latest_known_{},claimed_{},pending_{},callback_pending_{};Epoch epoch_{};t::Plan pending_plan_{};
    std::uint64_t attempted_input_{};t::Result receipt_{};
    bool current(Epoch e){return e.input&&host_.owner()==e.owner&&host_.round()==e.round;}
    bool budget(Epoch e,unsigned reserve){return current(e)&&host_.fuel()>=reserve&&current(e);}
    static unsigned add(unsigned a,unsigned b){return a>std::numeric_limits<unsigned>::max()-b?std::numeric_limits<unsigned>::max():a+b;}
    unsigned fallback_tail() const{return add(options_.fallback_reserve,options_.observation_reserve);}
    unsigned start_tail() const{return add(add(options_.start_reserve,options_.observation_reserve),fallback_tail());}
    unsigned approach_tail() const{return add(add(options_.approach_reserve,options_.observation_reserve),start_tail());}
    bool known(const Geometry& g,t::Point altar) const {
        if(!g.altars_queried||g.altars.size()>max_altars)return false;
        for(auto p:g.altars)if(p==altar)return true;return false;
    }
    bool snapshot(Epoch e,const Geometry& geometry,t::Point altar,Premises premises,Packet& packet,t::Context& context,bool enemies=true) {
        if(!same(e,geometry.source)||!budget(e,options_.observation_reserve))return false;
        packet={};if(!host_.observe(e,geometry,packet,enemies)||!current(e))return false;
        packet.own.owner=e.owner;packet.own.round=e.round;packet.own.input=e.input;packet.own.sample=++sample_;
        packet.own.observed=packet.own.fresh=true;
        if(!t::detail::valid(packet.own)||!std::isfinite(packet.hp)||!std::isfinite(packet.max_hp)
            ||packet.hp<0||packet.max_hp<=0||(packet.own.alive&&packet.hp<=0))return false;
        context={};context.owner=e.owner;context.round=e.round;context.input=e.input;context.sample=packet.own.sample;context.altar=altar;
        context.queried_ticks=packet.queried_ticks;
        if(packet.queried_ticks<=t::max_ticks&&e.round<=t::max_round-static_cast<int>(packet.queried_ticks))context.planned_horizon=e.round+static_cast<int>(packet.queried_ticks);
        context.minimum_alive_cycle_gap=premises.minimum_alive_cycle_gap;context.minimum_gap_queried=premises.minimum_gap_queried;
        context.bag_loss_model_closed=premises.bag_loss_model_closed;
        if(known(geometry,altar)&&!host_.altar(e,packet,altar,context))return false;
        if(!current(e))return false;context.fresh=true;return true;
    }
    void remember(const Packet& p,const t::Context& c){latest_=p;context_=c;latest_known_=true;}
    Result result(Status status,bool observation_event=false) const {
        Result r;r.status=claimed_&&status==Status::NoOffer?Status::NeedModel:status;
        r.claimed=claimed_;r.fresh=latest_known_&&status!=Status::Stale&&!pending_&&!callback_pending_;r.actual=latest_;r.context=context_;r.receipt=receipt_;
        r.unknown_effect=pending_||callback_pending_;if(!r.fresh){r.actual.own.fresh=false;r.context.fresh=false;}
        if(!observation_event||!r.fresh){r.receipt.new_evidence=false;r.receipt.credited_original=0;}
        r.physical_released=r.fresh&&latest_.own.alive&&latest_.own.bag==0&&latest_.own.channel==t::Channel::None;
        return r;
    }
    bool quote(const Policy& policy,t::Action action,const BankIntent& intent,const Packet& packet,t::Context& context,t::Guard& guard) {
        if(!policy.authorize||!policy.authorize(policy.context,action,intent,packet,context,guard)||!current(intent.source))return false;
        if(guard.planned_horizon<intent.min_horizon||guard.planned_horizon>t::max_round)return false;
        context.planned_horizon=guard.planned_horizon;return true;
    }
    Result fallback(Result out,const Policy& policy,const Geometry& geometry,Premises premises,t::Monitor& monitor) {
        out.fallback_required=true;
        bool invoked=false;
        if(out.fresh&&budget(epoch_,fallback_tail())&&policy.fallback) {
            invoked=true;callback_pending_=true;claimed_=out.claimed=true;
            out.fallback_funded=policy.fallback(policy.context,out.actual,out.context);
        }
        if(!current(epoch_)){out.fresh=false;out.actual.own.fresh=false;out.context.fresh=false;out.physical_released=false;out.status=Status::Stale;out.unknown_effect=invoked;return out;}
        // A successful fallback callback performs a fresh physical refinement
        // or justifies a closed prefix from ACTUAL allowances/channel/stun. It
        // cannot merely promise a future macro. Refresh after any such work.
        if(invoked) {
            Packet after;t::Context context;
            if(snapshot(epoch_,geometry,out.context.altar,premises,after,context,false)) {
                callback_pending_=false;
                const bool changed=after.own.position!=out.actual.own.position;remember(after,context);receipt_=monitor.observe(after.own,context);
                if(changed&&monitor.rearm(t::Change::ActualStep,after.own,context))receipt_=monitor.observe(after.own,context);
                auto fresh=result(out.status);fresh.move_attempted=out.move_attempted;fresh.moved=out.moved;
                fresh.submit_attempted=out.submit_attempted;fresh.fallback_required=true;fresh.fallback_funded=out.fallback_funded;return fresh;
            }
            out.fresh=false;out.actual.own.fresh=false;out.context.fresh=false;out.physical_released=false;out.unknown_effect=true;
        }
        return out;
    }
public:
    explicit Adapter(Host& host,Options options={}):host_(host),options_(options){}
    const Packet& latest() const{return latest_;}
    const t::Context& context() const{return context_;}
    const t::Result& receipt() const{return receipt_;}
    const t::Plan& unresolved_plan() const{return pending_plan_;}
    bool claimed() const{return claimed_;}
    // No monitor replacement/remint occurs here. A caller may release physical
    // control on actual bag0/ChannelNone while retaining an unresolved monitor.
    Result observe_input(Epoch actual,const Geometry& geometry,Premises premises,t::Monitor* monitor=nullptr,
                         t::Point tracked_altar={},t::Change change=t::Change::None) {
        if(!same(actual,epoch_)){claimed_=pending_||callback_pending_;latest_known_=false;epoch_=actual;}
        Packet packet;t::Context context;
        if(!snapshot(actual,geometry,tracked_altar,premises,packet,context,false))return result(current(actual)?Status::NeedObservation:Status::Stale);
        remember(packet,context);
        if(monitor&&monitor->state()!=t::State::Empty) {
            receipt_=monitor->observe(packet.own,context);
            if(change!=t::Change::None&&monitor->rearm(change,packet.own,context))receipt_=monitor->observe(packet.own,context);
        } else receipt_={};
        // Any old command has now returned, but no old Plan is replayed or
        // confirmed with a new input. Monitor::observe closes its pending word.
        if(pending_||callback_pending_){pending_=callback_pending_=false;auto out=result(Status::NeedFallback,true);out.claimed=claimed_=true;out.unknown_effect=out.fallback_required=true;return out;}
        auto out=result(Status::Ready,true);
        if(monitor&&monitor->state()==t::State::Blocked&&!out.physical_released)out.fallback_required=true;
        return out;
    }
    Result fund_fallback(t::Monitor& monitor,Result actual,const Geometry& geometry,Premises premises,Policy policy) {
        if(!same(geometry.source,epoch_)||!current(epoch_)||!latest_known_||!actual.fresh
            ||!t::detail::same(actual.actual.own,latest_.own)||!t::detail::same(actual.context,context_))return result(Status::Stale);
        auto bound=result(actual.status);bound.move_attempted=actual.move_attempted;bound.moved=actual.moved;bound.submit_attempted=actual.submit_attempted;
        return fallback(bound,policy,geometry,premises,monitor);
    }
    BankIntent nominate(std::uint64_t token,std::uint64_t original_amount,const Geometry& geometry) const {
        BankIntent intent;if(!latest_known_||!same(epoch_,geometry.source)||!token||!geometry.altars_queried
            ||geometry.altars.size()>max_altars||latest_.queried_ticks<2||latest_.queried_ticks>t::max_ticks
            ||!latest_.own.alive||latest_.own.stunned||!latest_.own.bag||latest_.own.channel!=t::Channel::None
            ||!latest_.own.submit_permitted||latest_.completed_turn||latest_.submit_range<1)return intent;
        t::Point altar{},landing{};int best=1000000;bool move=false;
        for(auto candidate:geometry.altars) {
            const int d=distance(latest_.own.position,candidate);bool ingress=d>latest_.submit_range;
            if(ingress&&(d!=latest_.submit_range+1||!latest_.own.can_move||latest_.move_speed<1
                ||!geometry.root_history_known||geometry.exclusive_cast_used||!geometry.static_walkable))continue;
            auto q=latest_.own.position;
            if(ingress){q.x+=(candidate.x>q.x)-(candidate.x<q.x);q.y+=(candidate.y>q.y)-(candidate.y<q.y);}
            if(d<best){best=d;altar=candidate;landing=q;move=ingress;}
        }
        if(best==1000000||epoch_.round>t::max_round-static_cast<int>(latest_.queried_ticks))return intent;
        intent={true,epoch_,latest_.own.sample,token,original_amount,latest_.own.bag,latest_.own.position,altar,landing,move,
            latest_.queried_ticks,epoch_.round+static_cast<int>(latest_.queried_ticks)};return intent;
    }
    Result execute(t::Monitor& monitor,const BankIntent& intent,const Geometry& geometry,Premises premises,Policy policy) {
        if(!intent.valid||!same(intent.source,epoch_)||!same(epoch_,geometry.source)||!current(epoch_)||!latest_known_
            ||intent.sample!=latest_.own.sample||intent.position!=latest_.own.position||intent.actual_bag!=latest_.own.bag
            ||intent.queried_ticks!=latest_.queried_ticks||intent.queried_ticks<2||attempted_input_==epoch_.input)
            return result(current(epoch_)?Status::NoOffer:Status::Stale);
        Packet packet;t::Context context;
        if(!snapshot(epoch_,geometry,intent.altar,premises,packet,context))return result(current(epoch_)?Status::NeedObservation:Status::Stale);
        remember(packet,context);
        if(packet.own.position!=intent.position||packet.own.bag!=intent.actual_bag||packet.queried_ticks!=intent.queried_ticks)
            return result(Status::NoOffer);
        if(context.altar_known&&!context.unlocked)return result(Status::NoOffer);
        if(monitor.state()==t::State::Empty)receipt_=monitor.begin(intent.token,packet.own,context);
        else receipt_=monitor.observe(packet.own,context);
        if(receipt_.token!=intent.token||receipt_.original_amount!=intent.original_amount||monitor.state()!=t::State::Tracking)return result(Status::NoOffer);
        bool moved=false;int funded_horizon=context.planned_horizon;
        if(intent.move_required) {
            if(!budget(epoch_,approach_tail())||!geometry.static_walkable
                ||!geometry.static_walkable(geometry.context,intent.landing)||!current(epoch_))return result(Status::NeedModel);
            t::Guard guard;if(!quote(policy,t::Action::Approach,intent,packet,context,guard))return result(current(epoch_)?Status::NeedModel:Status::Stale);
            funded_horizon=context.planned_horizon;
            if(!host_.move_legal(epoch_,packet,intent.landing,geometry)||!current(epoch_))return result(current(epoch_)?Status::NoOffer:Status::Stale);
            if(!budget(epoch_,approach_tail()))return result(current(epoch_)?Status::NeedFuel:Status::Stale);
            receipt_=monitor.observe(packet.own,context);const auto plan=monitor.prepare(t::Action::Approach,intent.landing,packet.own,context,guard);
            if(!plan.prepared)return result(Status::NeedModel);
            pending_=true;pending_plan_=plan;attempted_input_=epoch_.input;claimed_=true;
            const bool accepted=host_.move(intent.landing);
            if(!current(epoch_)){auto out=result(Status::Stale);out.move_attempted=out.unknown_effect=true;out.fresh=false;return out;}
            if(!snapshot(epoch_,geometry,intent.altar,premises,packet,context)){auto out=result(current(epoch_)?Status::NeedObservation:Status::Stale);out.move_attempted=out.unknown_effect=true;out.fresh=false;return out;}
            remember(packet,context);pending_=false;receipt_=monitor.confirm_move(plan,accepted,packet.own,context);
            moved=receipt_.actual_movement;
            if(!accepted||packet.own.position!=intent.landing||monitor.state()!=t::State::Tracking) {
                auto out=result(accepted?Status::NeedFallback:Status::MoveRefused);out.move_attempted=true;out.moved=moved;return fallback(out,policy,geometry,premises,monitor);
            }
        }
        context.planned_horizon=std::max(context.planned_horizon,funded_horizon);
        BankIntent start=intent;start.sample=packet.own.sample;start.position=packet.own.position;start.actual_bag=packet.own.bag;
        start.queried_ticks=packet.queried_ticks;start.min_horizon=context.planned_horizon;start.move_required=false;start.landing=packet.own.position;
        if(packet.queried_ticks<2||packet.queried_ticks>t::max_ticks||!context.altar_known||!context.adjacent||!context.unlocked||!packet.own.submit_permitted) {
            auto out=result(moved?Status::NeedFallback:Status::NoOffer);out.move_attempted=intent.move_required;out.moved=moved;
            return moved?fallback(out,policy,geometry,premises,monitor):out;
        }
        if(!budget(epoch_,start_tail())){auto out=result(Status::NeedFuel);out.move_attempted=intent.move_required;out.moved=moved;return moved?fallback(out,policy,geometry,premises,monitor):out;}
        t::Guard guard;if(!quote(policy,t::Action::Start,start,packet,context,guard)) {
            auto out=result(current(epoch_)?Status::NeedModel:Status::Stale);out.move_attempted=intent.move_required;out.moved=moved;return moved?fallback(out,policy,geometry,premises,monitor):out;
        }
        if(!budget(epoch_,start_tail())){auto out=result(current(epoch_)?Status::NeedFuel:Status::Stale);out.move_attempted=intent.move_required;out.moved=moved;return moved?fallback(out,policy,geometry,premises,monitor):out;}
        receipt_=monitor.observe(packet.own,context);const auto plan=monitor.prepare(t::Action::Start,intent.altar,packet.own,context,guard);
        if(!plan.prepared){auto out=result(Status::NeedModel);out.move_attempted=intent.move_required;out.moved=moved;return moved?fallback(out,policy,geometry,premises,monitor):out;}
        pending_=true;pending_plan_=plan;attempted_input_=epoch_.input;claimed_=true;host_.submit(intent.altar);
        if(!current(epoch_)){auto out=result(Status::Stale);out.submit_attempted=out.unknown_effect=true;out.moved=moved;out.fresh=false;return out;}
        if(!snapshot(epoch_,geometry,intent.altar,premises,packet,context)){auto out=result(current(epoch_)?Status::NeedObservation:Status::Stale);out.submit_attempted=out.unknown_effect=true;out.moved=moved;out.fresh=false;return out;}
        remember(packet,context);pending_=false;
        const bool accepted=packet.own.channel==t::Channel::Submit&&packet.own.submit_started&&packet.own.remaining_known&&packet.own.remaining_ticks==plan.context.queried_ticks;
        receipt_=monitor.confirm_start(plan,accepted,packet.own,context);
        auto out=result(accepted?Status::Started:Status::StartRejected);out.move_attempted=intent.move_required;out.moved=moved;out.submit_attempted=true;
        out.started=accepted&&monitor.state()==t::State::Active;if(!out.started)out.status=Status::StartRejected;
        return out.started?out:fallback(out,policy,geometry,premises,monitor);
    }
};
#ifndef WB_CONVERSION_NATIVE_HOST_FREE
class SdkHost {
    int team_{};
    static bool unavailable(int code){return code==static_cast<int>(cambc::error_code::api_not_in_vision)||code==static_cast<int>(cambc::error_code::api_not_found);}
    bool current(Epoch e){return owner()==e.owner&&round()==e.round;}
    bool checked(int code,Epoch e){cambc::detail::check(code);return current(e);}
public:
    int owner(){return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));}
    int round(){return static_cast<int>(cambc::round_number());}
    unsigned fuel(){return cambc::fuel_remaining();}
    bool observe(Epoch e,const Geometry& g,Packet& p,bool sample_enemies=true) {
        cambc::detail::champion_info raw{};if(!checked(cambc::detail::sdk_champion(e.owner,&raw),e))return false;
        team_=raw.team;p.hp=raw.health;p.max_hp=raw.max_health;p.kind=raw.type;p.move_speed=raw.move_speed;p.attack_range=raw.attack_range;
        p.submit_range=cambc::constants::submit_gems_range;auto& o=p.own;o.position={raw.x,raw.y};
        const auto state=cambc::champion_state::from_bits_retain(raw.state);
        o.alive=state.contains(cambc::champion_state::alive);o.can_move=state.contains(cambc::champion_state::can_move);
        o.can_attack=state.contains(cambc::champion_state::can_attack);o.can_cast=state.contains(cambc::champion_state::can_cast);
        o.submit_started=state.contains(cambc::champion_state::submit_gems_started);o.submit_cancelled=state.contains(cambc::champion_state::submit_gems_cancelled);
        p.completed_turn=state.contains(cambc::champion_state::completed_turn);
        o.channel=state.contains(cambc::champion_state::submitting_gems)?t::Channel::Submit:
            state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::teleporting)?t::Channel::Other:t::Channel::None;
        std::uint32_t bag=0,score=0;if(!checked(cambc::detail::sdk_champion_gems(e.owner,&bag),e))return false;o.bag=bag;
        if(!checked(cambc::detail::sdk_bc_submitted_gems(team_,&score),e))return false;o.team_score=score;
        if(o.alive) {
            std::uint8_t restrictions=0;if(!checked(cambc::detail::sdk_champion_restrictions(e.owner,&restrictions),e))return false;p.restrictions=restrictions;
            std::array<cambc::detail::buff_data,static_cast<unsigned>(cambc::buff_types::count)> buffs{};std::uint32_t count=0;
            if(!checked(cambc::detail::sdk_champion_buff_data(e.owner,buffs.data(),buffs.size(),&count),e)||count>buffs.size())return false;
            for(unsigned i=0;i<count;++i)if(buffs[i].type==static_cast<unsigned>(cambc::buff_types::stun))o.stunned=true;
        }
        if(o.channel==t::Channel::Submit) {
            std::int32_t remaining=0;if(!checked(cambc::detail::sdk_champion_channel_time(e.owner,&remaining),e))return false;
            o.remaining_known=remaining>=0&&remaining<=static_cast<int>(t::max_ticks);if(o.remaining_known)o.remaining_ticks=static_cast<unsigned>(remaining);
        }
        // Independent of A/C: a ranged ordinary move may have spent attack.
        o.submit_permitted=o.alive&&!o.stunned&&o.channel==t::Channel::None&&!o.submit_started&&!p.completed_turn;
        const double duration=cambc::constants::submit_gem_base_duration+
            std::ceil(static_cast<double>(bag)*cambc::constants::submit_gem_duration_multiplier);
        const double ticks=std::max<double>(cambc::constants::submit_gem_minimum_duration,duration);
        if(std::isfinite(ticks)&&ticks>=1&&ticks<=t::max_ticks)p.queried_ticks=static_cast<unsigned>(ticks);
        if(!sample_enemies)return current(e);
        p.enemies_complete=g.enemy_ids_complete&&g.enemy_ids.size()<=max_enemies;
        p.enemy_count=std::min<unsigned>(g.enemy_ids.size(),max_enemies);
        for(unsigned i=0;i<p.enemy_count;++i) {
            auto& enemy=p.enemies[i];enemy.id=g.enemy_ids[i];
            if(enemy.id<0){p.enemies_complete=false;continue;}
            for(unsigned j=0;j<i;++j)if(enemy.id==p.enemies[j].id)p.enemies_complete=false;
            std::int32_t alive=0;const int living=cambc::detail::sdk_champion_is_alive(enemy.id,&alive);
            if(!current(e))return false;if(unavailable(living)){p.enemies_complete=false;continue;}cambc::detail::check(living);
            enemy.alive_known=true;enemy.alive=alive!=0;
            cambc::detail::champion_info foe{};const int code=cambc::detail::sdk_champion(enemy.id,&foe);if(!current(e))return false;
            if(unavailable(code))continue;cambc::detail::check(code);if(foe.team==team_){p.enemies_complete=false;continue;}
            enemy.kind=foe.type;if(!enemy.alive||foe.x<0||foe.y<0)continue;
            enemy.observed_now=true;enemy.position={foe.x,foe.y};enemy.hp=foe.health;enemy.max_hp=foe.max_health;
            enemy.strength=foe.strength;enemy.focus=foe.focus;enemy.move_speed=foe.move_speed;enemy.attack_range=foe.attack_range;
            const auto flags=cambc::champion_state::from_bits_retain(foe.state);
            enemy.can_move=flags.contains(cambc::champion_state::can_move);enemy.can_attack=flags.contains(cambc::champion_state::can_attack);enemy.can_cast=flags.contains(cambc::champion_state::can_cast);
        }
        return current(e);
    }
    bool altar(Epoch e,const Packet& p,t::Point altar,t::Context& c) {
        if(!cambc::map::is_in_bounds({altar.x,altar.y}))return true;
        std::int32_t visible=0;if(!checked(cambc::detail::sdk_is_visible(altar.x,altar.y,&visible),e))return false;
        if(!visible)return true;
        c.altar_known=true;c.adjacent=distance(p.own.position,altar)<=p.submit_range;
        cambc::detail::altar_lock_info lock{};const int code=cambc::detail::sdk_altar_lock(altar.x,altar.y,1-team_,&lock);
        if(!current(e))return false;
        if(code==static_cast<int>(cambc::error_code::api_not_in_vision)){c.altar_known=false;return true;}
        if(code==static_cast<int>(cambc::error_code::api_not_found)){c.unlocked=true;return true;}
        cambc::detail::check(code);c.unlocked=false;return true;
    }
    bool move_legal(Epoch e,const Packet& p,t::Point landing,const Geometry& g) {
        if(!current(e)||!g.root_history_known||g.exclusive_cast_used||!p.own.alive||p.own.stunned||p.completed_turn
            ||!p.own.can_move||p.move_speed<1||p.own.channel!=t::Channel::None
            ||(p.restrictions&cambc::buff_restrictions::move.bits())||distance(p.own.position,landing)!=1)return false;
        if(!cambc::map::is_in_bounds({landing.x,landing.y}))return false;
        std::int32_t visible=0;if(!checked(cambc::detail::sdk_is_visible(landing.x,landing.y,&visible),e)||!visible)return false;
        std::int32_t occupied=0;if(!checked(cambc::detail::sdk_has_champion(landing.x,landing.y,&occupied),e)||occupied)return false;
        if(!checked(cambc::detail::sdk_has_camp(landing.x,landing.y,&occupied),e)||occupied)return false;
        if(!checked(cambc::detail::sdk_has_teleport(landing.x,landing.y,&occupied),e)||occupied)return false;
        return current(e);
    }
    bool move(t::Point p){std::int32_t accepted=0;cambc::detail::check(cambc::detail::sdk_move_to(p.x,p.y,&accepted));return accepted!=0;}
    void submit(t::Point p){cambc::detail::check(cambc::detail::sdk_submit_gems(p.x,p.y));}
};
#endif
}
