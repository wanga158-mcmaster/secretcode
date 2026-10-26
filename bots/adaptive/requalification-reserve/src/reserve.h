#pragma once
#include "profile.h"
#include "../../reversible-workshop/src/transactions.h"
namespace p8::requalification_reserve {
using Inventory=reversible_workshop::Inventory;
using common::Arm; using common::Offer; using common::Decision; using common::Admission; using common::Rejection;
enum class ChangeKind { own_gear,observed_route,visible_membership };
struct System { Plan plan{}; ContextDeclaration context{}; };
struct Package {
    std::array<System,2> systems{};
    ChangeKind change=ChangeKind::visible_membership;
    int purchase_item=-1;
    Inventory old_inventory{},new_inventory{};
    double strength_boundary=0;
    p6::Fact<double> solo_work{},incoming_per_phase{},reserve{};
    p6::Fact<int> maximum_solo_rounds{};
};
inline bool declared_fact(p6::Source s) { return s==p6::Source::public_map||s==p6::Source::queried_setting||s==p6::Source::conservative||s==p6::Source::synthetic_fixture; }
inline bool own_boundary_feasible(const Package& p,const NumericState& state) {
    if (!std::isfinite(p.solo_work.value)||p.solo_work.value<=0||!declared_fact(p.solo_work.source)||
        !declared_fact(p.incoming_per_phase.source)||!std::isfinite(p.incoming_per_phase.value)||p.incoming_per_phase.value<0||
        !declared_fact(p.reserve.source)||!std::isfinite(p.reserve.value)||p.reserve.value<0||
        !declared_fact(p.maximum_solo_rounds.source)||p.maximum_solo_rounds.value<=0||p.maximum_solo_rounds.value>120||
        !valid_numeric_state(state)||state.strength<=0) return false;
    const double rounds=std::ceil(p.solo_work.value/state.strength);
    return rounds<=p.maximum_solo_rounds.value&&state.health-rounds*p.incoming_per_phase.value>=p.reserve.value;
}
struct Inputs:reversible_workshop::Inputs {};
inline bool allowed_members(const Plan& p,const DraftRoster& d) {
    if (!d.valid()||p.organization!=Organization::H||p.collector!=d.actors[0]||p.crew_count<2||p.crew_count>3) return false;
    if (common::participant(p,d.actors[1])||common::participant(p,d.actors[2])) return false;
    for (int i=0;i<p.crew_count;++i) { bool present=false; for (int j=0;j<d.picks;++j) present|=p.crew[i]==d.actors[j]; if (!present) return false; }
    return true;
}
inline bool package_valid(const Package& p,const DraftRoster& d) {
    if (p.change!=ChangeKind::own_gear&&p.change!=ChangeKind::observed_route&&p.change!=ChangeKind::visible_membership) return false;
    const auto& a=p.systems[0]; const auto& b=p.systems[1];
    for (const auto& s:p.systems) {
        if (!plan_valid(s.plan,d.actors[0])||!allowed_members(s.plan,d)||s.context.kind!=EpisodeKind::full_organization||
            !s.context.require_supported_work||!valid(s.context.key)||s.context.key.actor_id!=d.actors[0]||
            s.context.key.champion!=int(d.champions[0])||s.context.key.source_group!=s.plan.group||
            s.context.key.route_hash!=plan_route_hash(s.plan)||s.context.key.participant_count!=s.plan.crew_count) return false;
        for (int i=0;i<s.context.key.participant_count;++i) if (!common::participant(s.plan,s.context.key.participants[i])) return false;
    }
    if (a.context.key==b.context.key||a.plan.group!=b.plan.group||a.context.key.settings_id!=b.context.key.settings_id||
        a.context.key.actor_id!=b.context.key.actor_id||a.context.key.champion!=b.context.key.champion||a.context.key.role!=b.context.key.role) return false;
    ContextKey same=b.context.key;
    if (p.change==ChangeKind::own_gear) {
        if (p.purchase_item<0||p.purchase_item>=reversible_workshop::item_types||!p.old_inventory.valid()||!p.new_inventory.valid()||
            p.old_inventory.hash()!=a.context.key.build.equipment_hash||p.new_inventory.hash()!=b.context.key.build.equipment_hash||
            !std::isfinite(p.strength_boundary)||p.strength_boundary<=a.context.key.build.strength||p.strength_boundary>b.context.key.build.strength) return false;
        auto expected=p.old_inventory; ++expected.count[p.purchase_item]; if (!(expected==p.new_inventory)) return false;
        const auto& before=a.context.key.build; const auto& after=b.context.key.build;
        if (own_boundary_feasible(p,{before.max_health,before.max_health,before.strength,before.focus,before.growth,0,before.equipment_hash})||
            !own_boundary_feasible(p,{after.max_health,after.max_health,after.strength,after.focus,after.growth,0,after.equipment_hash})) return false;
        same.build=a.context.key.build;
    } else if (p.change==ChangeKind::observed_route) {
        if (a.context.key.route_hash==b.context.key.route_hash) return false;
        same.route_hash=a.context.key.route_hash; same.return_route_hash=a.context.key.return_route_hash;
        same.route_template=a.context.key.route_template; same.endpoint=a.context.key.endpoint;
    } else {
        if (a.context.key.participants==b.context.key.participants) return false;
        same.participants=a.context.key.participants; same.participant_count=a.context.key.participant_count;
    }
    return same==a.context.key;
}
struct GearCommand {
    bool valid=false;
    p6::ViewClock clock{};
    std::uint64_t token=0;
    int item=-1;
    p6::Gems cost=0,gold_before=0;
};
struct GearReceipt {
    p6::ViewClock command_clock{},observed_clock{};
    std::uint64_t token=0;
    int item=-1;
    bool attempted=false,success=false;
};
struct GearPlan { p6::Schedule with_purchase{},without_purchase{}; };
class Reserve {
    Package package_{};
    std::array<int,2> slots_{-1,-1};
    bool declared_=false,retired_=false,requalification_used_=false,change_witnessed_=false;
    int selected_=0,retired_round_=-1,witness_round_=-1;
    GearCommand issued_{};
    bool purchase_pending_=false;
    std::uint64_t next_token_=1,route_generation_=0;
    p6::ViewClock route_start_{};
    int route_last_=-1;
    bool route_pending_=false,route_failed_=false;
public:
    bool declared() const { return declared_; }
    bool retired() const { return retired_; }
    bool used() const { return requalification_used_; }
    bool changed() const { return change_witnessed_; }
    int selected() const { return selected_; }
    int retired_round() const { return retired_round_; }
    int slot(int i) const { return i>=0&&i<2?slots_[i]:-1; }
    const Package& package() const { return package_; }
    bool declare(const Package& p,const DraftRoster& draft,EvidenceLedger& ledger) {
        if (declared_||ledger.active()||ledger.size()!=0||!package_valid(p,draft)) return false;
        const int a=ledger.predeclare(p.systems[0].context); if (a<0) return false;
        const int b=ledger.predeclare(p.systems[1].context); if (b<0) return false;
        package_=p; slots_={a,b}; declared_=true; return true;
    }
    void synchronize(const EvidenceLedger& ledger,int round) {
        if (!declared_||retired_) return;
        const auto* old=ledger.state(slots_[0]);
        // Elapsed time/qualification expiry is deliberately absent here.
        if (old&&old->latest_terminal>=0&&(old->safety_suspended||old->campaign_failed||old->model_set_failed||
            old->material_premise_changed||old->unresolved_suspended||old->retired_epoch>=0)) {
            retired_=true; retired_round_=round;
        }
    }
    GearCommand propose_gear(const Inputs& in,const LocalFrame& local,const Work& work,const EvidenceLedger& ledger,const GearPlan& plan) {
        if (!declared_||!retired_||requalification_used_||package_.change!=ChangeKind::own_gear||purchase_pending_||!next_token_||
            !reversible_workshop::safe_shop(in,local,work)||ledger.active()||
            !(in.market.inventory.value==package_.old_inventory)||in.own_state.value.strength>=package_.strength_boundary||
            !common::guarded_key_current(in,package_.systems[0].context.key)) return {};
        const int item=package_.purchase_item; const auto& q=in.market.items[item];
        if (q.price.source!=p6::Source::queried_setting||q.price.value<=0||q.price.value>in.frame.own.gold||
            q.purchasable.source!=p6::Source::queried_setting||!q.purchasable.value||
            !q.buy_legal.current(in.frame.clock,in.frame.clock.self)||!q.buy_legal.value) return {};
        if (!p6::same_actors(plan.with_purchase,plan.without_purchase)) return {};
        const auto yes=common::evaluate(in,plan.with_purchase),no=common::evaluate(in,plan.without_purchase);
        if (!yes.eligible()||!no.eligible()||p6::compare(yes,no)<=0) return {};
        int purchases=0;
        for (int a=0;a<plan.with_purchase.actor_count;++a) for (int e=0;e<plan.with_purchase.actors[a].event_count;++e) {
            const auto& event=plan.with_purchase.actors[a].events[e];
            if (event.kind==p6::EventKind::purchase) {
                ++purchases;
                if (plan.with_purchase.actors[a].actor_id!=in.frame.clock.self||event.tick!=0||!event.gold_cost.known()||event.gold_cost.value!=q.price.value) return {};
            }
        }
        for (int a=0;a<plan.without_purchase.actor_count;++a) for (int e=0;e<plan.without_purchase.actors[a].event_count;++e)
            if (plan.without_purchase.actors[a].events[e].kind==p6::EventKind::purchase) return {};
        if (purchases!=1) return {};
        issued_={true,in.frame.clock,next_token_++,item,q.price.value,in.frame.own.gold}; purchase_pending_=true; return issued_;
    }
    bool confirm_gear(const GearCommand& c,const GearReceipt& r,const Inputs& after) {
        if (!purchase_pending_||!c.valid||c.token!=issued_.token||c.item!=issued_.item||c.cost!=issued_.cost||c.gold_before!=issued_.gold_before||!(c.clock==issued_.clock)||
            r.token!=issued_.token||r.item!=issued_.item||!(r.command_clock==issued_.clock)||!r.attempted||!r.success||
            !reversible_workshop::own_market(after)||!(r.observed_clock==after.frame.clock)||after.frame.clock.self!=issued_.clock.self||
            after.frame.clock.round!=issued_.clock.round||after.frame.clock.revision<=issued_.clock.revision||
            !after.frame.own.at_spawn||after.frame.own.actor.bag!=0||after.frame.own.actor.channel!=p6::Channel::none||
            after.frame.own.gold!=issued_.gold_before-issued_.cost||!(after.market.inventory.value==package_.new_inventory)||
            after.own_state.value.strength<package_.strength_boundary||!own_boundary_feasible(package_,after.own_state.value)||
            !common::guarded_key_current(after,package_.systems[1].context.key)||
            applicability(package_.systems[1].context.key,package_.systems[1].context.envelope,
                {package_.systems[1].context.key,after.own_state.value,all_premises},true)!=Applicability::applicable) return false;
        purchase_pending_=false; change_witnessed_=true; witness_round_=after.frame.clock.round; return true;
    }
    void failed_purchase() { purchase_pending_=false; }
    bool begin_route_observation(const Work& work) {
        if (!declared_||!retired_||requalification_used_||package_.change!=ChangeKind::observed_route||route_pending_||
            !work.active||!work.generation||work.plan.organization!=Organization::U||
            work.plan.collector!=package_.systems[1].context.key.actor_id||work.started_clock.self!=package_.systems[1].context.key.actor_id||
            work.started_clock.round<=retired_round_||work.last_round>=work.started_clock.round||
            work.productive_action_seen||work.payout_seen||work.bank_seen||work.cycle_completed||
            plan_route_hash(work.plan)!=package_.systems[1].context.key.route_hash) return false;
        route_generation_=work.generation; route_start_=work.started_clock; route_last_=-1;
        route_pending_=true; route_failed_=false; return true;
    }
    void observe_route(const LocalFrame& local,const Work& work) {
        if (!route_pending_||!local.fresh()||local.frame.clock.self!=package_.systems[1].context.key.actor_id||
            work.last_round!=local.frame.clock.round||local.frame.clock.round<route_last_) return;
        if (work.generation!=route_generation_||!(work.started_clock==route_start_)||local.fuel_resumed||
            !local.frame.own.actor.alive||!work.route_witness_active||
            plan_route_hash(work.plan)!=package_.systems[1].context.key.route_hash) route_failed_=true;
        route_last_=local.frame.clock.round;
        if (!work.active) {
            if (!route_failed_&&work.cycle_completed&&work.bank_seen&&work.renewal_seen) {
                change_witnessed_=true; witness_round_=local.frame.clock.round;
            }
            route_pending_=false;
        }
    }
    bool witness_membership(const Inputs& in) {
        if (!declared_||!retired_||requalification_used_||package_.change!=ChangeKind::visible_membership||
            !reversible_workshop::own_market(in)||in.frame.clock.round<=retired_round_) return false;
        const auto& p=package_.systems[1].plan; bool new_helper=false;
        for (int i=0;i<p.crew_count;++i) if (p.crew[i]!=p.collector) {
            const auto* a=in.frame.visible(p.crew[i]);
            if (!a||!common::roster_actor(*a,in.draft)||!a->alive||a->channel!=p6::Channel::none||
                !(a->position==p.pads[i])||!a->can_attack||a->ready_attack_group!=p.group) return false;
            new_helper|=!common::participant(package_.systems[0].plan,a->id);
        }
        if (!new_helper) return false;
        change_witnessed_=true; witness_round_=in.frame.clock.round; return true;
    }
    bool authorize(const Inputs& in,const LocalFrame& local,const Work& work,EvidenceLedger& ledger,Arm arm) {
        synchronize(ledger,in.frame.clock.round);
        if (arm!=Arm::active||!declared_||!retired_||requalification_used_||!change_witnessed_||witness_round_<retired_round_||
            !reversible_workshop::own_market(in)||!local.fresh()||!(local.frame.clock==in.frame.clock)||!local.legal.value.safe_now||
            local.fuel_resumed||work.active||work.pending||ledger.active()||in.frame.own.actor.bag!=0||
            in.frame.own.actor.channel!=p6::Channel::none||!collector_actor(in.frame.own.actor,in.draft)) return false;
        const auto& s=package_.systems[1]; const auto* fresh=ledger.state(slots_[1]);
        if (!fresh||fresh->campaign_started||fresh->positive_completions||ledger.suspended(slots_[1],in.frame.clock.round/common::project_rounds)||
            !common::guarded_key_current(in,s.context.key)||
            (package_.change==ChangeKind::own_gear&&!own_boundary_feasible(package_,in.own_state.value))||
            applicability(s.context.key,s.context.envelope,{s.context.key,in.own_state.value,all_premises},true)!=Applicability::applicable) return false;
        bool feasible=false;
        for (int i=0;i<in.offer_count;++i) if (in.offers[i].context_slot==slots_[1]&&in.offers[i].entry==s.context.key&&
            plan_route_hash(in.offers[i].plan)==s.context.key.route_hash&&common::legal_offer(in,in.offers[i])==Rejection::none&&
            common::crew_legal(in,in.offers[i],false)&&
            p6::compare(common::evaluate(in,in.offers[i].schedule),common::evaluate(in,in.offers[in.independent].schedule))>0) feasible=true;
        if (!feasible) return false;
        selected_=1; requalification_used_=true; return true;
    }
};
} // namespace p8::requalification_reserve
