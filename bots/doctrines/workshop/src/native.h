#pragma once
#include "policy.h"
#include "../../reference/src/runtime.h"

// Native Workshop binding: actual Tavi alone owns both production payouts,
// one field shard, whole-bag tribute and a distinct productive renewal.
// Other drafted bodies use the common independent reference controller.
namespace p5::workshop::native_binding {
namespace n=p5::native;
namespace e=p5::execution;
#if PLAN5_WORKSHOP_TRACE
#define P5_WORKSHOP_GATE(gate, useful) runtime_.trace_workshop(gate, useful)
#else
#define P5_WORKSHOP_GATE(gate, useful)
#endif
struct Pair { int first=-1,second=-1,distance=0; };
// Each physical champion owns its WASM memory. Constant initialization avoids
// constructing these unused array tails inside the first fuel-limited turn.
struct EconomicScratch {
    Project project{};n::Snapshot snapshot{};Observation planning{};Decision selection{};
    std::array<Pair,56> pairs{};std::array<RouteLeg,8> access{},bank{};
};
inline constinit EconomicScratch economic_scratch{};
constexpr bool dispatch_keeps_project(n::ApplyResult result,e::ActionKind kind,bool safe,bool safe_channel){
    return result==n::ApplyResult::acted || (result==n::ApplyResult::stopped &&
        (((kind==e::ActionKind::hold || kind==e::ActionKind::yield) && safe) ||
         (kind==e::ActionKind::preserve_channel && safe_channel)));
}
static_assert(dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::hold,true,false));
static_assert(dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::preserve_channel,false,true));
static_assert(!dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::buy,true,false));
static_assert(!dispatch_keeps_project(n::ApplyResult::stale,e::ActionKind::hold,true,false));
class Bot {
    n::Runtime runtime_{};
    Profile profile_{};
    Project& project_=economic_scratch.project;
    n::Snapshot& snapshot_=economic_scratch.snapshot;
    Observation& planning_=economic_scratch.planning;
    Decision& selection_=economic_scratch.selection;
    std::array<Pair,56>& pairs_=economic_scratch.pairs;
    std::array<RouteLeg,8>& access_=economic_scratch.access;
    std::array<RouteLeg,8>& bank_=economic_scratch.bank;
    int altar_=-1,renewal_=-1,handled_round_=-1;
    double initial_strength_=0,initial_max_health_=0,initial_basic_modifier_=0;
    bool running_=false;

    static int return_id(const Decision& d) {
        for(int i=0;i<d.schedule.event_count;++i)if(d.schedule.events[i].kind==AccountingEventKind::return_to_work)return d.schedule.events[i].waypoint;
        return -1;
    }
    static const n::GroupFacts* group(const n::Snapshot& s,int id) {
        for(int i=0;i<s.group_count;++i)if(s.groups[i].id==id)return &s.groups[i];return nullptr;
    }
    static const n::PurchaseQuote* quote(const n::Snapshot& s,int family) {
        for(const auto& q:s.purchases)if(q.family==family)return &q;return nullptr;
    }
    static CombatActor conservative(CombatActor a) {
        a.shield=0;a.shield_expiry_tick=0;a.direct_or_reset_damage=0;
        a.spell_known_ready=a.legal_first_cast=a.reserved_cast=a.heal_legal_ready=false;
        a.heal_recipient=-1;a.heal_amount=0;a.visible_enemy_next_phase=0;return a;
    }
    static bool usable(const n::GroupFacts& g) {
        return g.known && !g.respawning && !g.other_holder && g.gems>0 &&
            g.combat.actor_count==1 && g.combat.monster_count>0 && g.combat.monster_count<=8;
    }
    static bool current(const n::Snapshot& s,unsigned floor=45000) {
        return s.complete && s.round==int(cambc::round_number()) && cambc::fuel_remaining()>=floor;
    }
    RouteLeg leg(n::Node from,n::Node to,bool locked_budget=false) const {
        auto out=runtime_.route(from,to);
        if(out.distance>=0 && locked_budget)out.distance+=int(cambc::constants::altar_lock_hits);
        out.safety_checked=out.turns()>=0;return out;
    }
    int nearest_altar(const n::Snapshot& s) const {
        int selected=-1,best=1000000;
        for(int i=0;i<s.altar_count;++i){const auto& a=s.altars[i];const auto r=runtime_.route({}, {n::NodeKind::altar_entry,a.tile_id});
            if(r.turns()<0)continue;const int cost=r.distance+((a.visible && !a.hostile_lock)?0:int(cambc::constants::altar_lock_hits));
            if(cost<best || (cost==best && a.tile_id<selected)){selected=a.tile_id;best=cost;}}
        return selected;
    }
    RouteLeg renewal_route(const n::Snapshot& s,int first,int second) const {
        RouteLeg best;
        for(int i=0;i<s.group_count;++i){const auto& g=s.groups[i];if(g.id==first || g.id==second || !usable(g))continue;
            const auto r=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,g.id});
            if(r.turns()>=0 && (best.turns()<0 || r.distance<best.distance || (r.distance==best.distance && r.destination<best.destination)))best=r;}
        return best;
    }
    static void copy_group(Group& out,const n::GroupFacts& g,const CombatActor& actor) {
        out.id=g.id;out.gems=g.gems;out.gold=g.gold;out.live_observed=g.known;out.reward_or_alive=g.known && !g.respawning;
        out.combat.actor_count=1;out.combat.actors[0]=actor;out.combat.monster_count=g.combat.monster_count;out.combat.hold_ticks=0;
        for(std::size_t i=0;i<g.combat.monster_count;++i)out.combat.monsters[i]=g.combat.monsters[i];
    }
    bool observation_into(const n::Snapshot& s,Observation& o) {
        o.count=o.purchase_count=0;o.round=s.round;o.team_score=s.team_score;o.self=s.self;o.stats=conservative(s.stats);
        o.settings=s.settings;o.windows=s.windows;o.fuel_remaining=int(cambc::fuel_remaining());
        o.safe_channel=s.channel!=e::Channel::none && s.channel_safe;o.safe_active_capture=s.safe_hold;o.needs_recovery=s.needs_recovery;
        bool unlocked=false;
        for(int i=0;i<s.altar_count;++i)if(s.altars[i].tile_id==altar_)unlocked=s.altars[i].adjacent && s.altars[i].visible && !s.altars[i].hostile_lock;
        o.current_to_altar=leg({}, {n::NodeKind::altar_entry,altar_},!unlocked);if(unlocked)o.current_to_altar.distance=0;
        o.bank_return=renewal_route(s,-1,-1);o.bank_legal=altar_>=0;o.bank_legal&=o.current_to_altar.turns()>=0;
        for(const auto& q:s.purchases){
            if(q.family<0 || q.family>=3 || q.item_id<0 || q.cost<=0 || !q.purchasable)continue;
            auto& p=o.purchases[o.purchase_count++];p.family=q.family;p.item=q.item_id;p.shards=1;p.cost=q.cost;
            p.strength_gain=q.strength_gain;p.health_gain=q.health_gain;p.spell_damage_gain=0;
            p.legal_in_field=p.equipment=true;
        }
        int count=0;
        for(int i=0;i<s.group_count;++i){access_[i]=leg({}, {n::NodeKind::group_entry,s.groups[i].id});
            bank_[i]=leg({n::NodeKind::group_entry,s.groups[i].id},{n::NodeKind::altar_entry,altar_},true);}
        // Screen all cheap geometric ordered pairs, then keep one identical
        // deterministic menu of at most eight for both Workshop arms.
        for(int i=0;i<s.group_count;++i)if(usable(s.groups[i]) && access_[i].turns()>=0)
            for(int j=0;j<s.group_count;++j)if(i!=j && usable(s.groups[j]) && bank_[j].turns()>=0){
                const auto between=leg({n::NodeKind::group_entry,s.groups[i].id},{n::NodeKind::group_entry,s.groups[j].id});
                if(between.turns()<0)continue;
                pairs_[count++]={i,j,access_[i].distance+between.distance+bank_[j].distance};}
        std::sort(pairs_.begin(),pairs_.begin()+count,[&](const Pair& a,const Pair& b){
            if(a.distance!=b.distance)return a.distance<b.distance;
            if(s.groups[a.first].id!=s.groups[b.first].id)return s.groups[a.first].id<s.groups[b.first].id;
            return s.groups[a.second].id<s.groups[b.second].id;});
        for(int i=0;i<count && o.count<o.routes.size();++i){
            if(!current(s)){o.count=0;return false;}
            const auto& p=pairs_[i];const auto& first=s.groups[p.first];const auto& second=s.groups[p.second];
            const auto renewal=renewal_route(s,first.id,second.id);if(renewal.turns()<0)continue;
            auto& r=o.routes[o.count++];copy_group(r.first,first,o.stats);copy_group(r.successor,second,o.stats);
            r.to_first=access_[p.first];r.between=leg({n::NodeKind::group_entry,first.id},{n::NodeKind::group_entry,second.id});
            r.to_altar=bank_[p.second];r.return_to_work=renewal;
        }
        return current(s);
    }
    static bool work(const n::GroupFacts& g,CombatActor& actor,const EconomicSettings& settings,bool full=false,int* ticks=nullptr) {
        auto c=g.combat;c.actor_count=1;c.actors[0]=actor;c.hold_ticks=settings.capture_rounds+1;
        if(full)for(std::size_t m=0;m<c.monster_count;++m){c.monsters[m].observed_health=c.monsters[m].public_health;c.monsters[m].visible=false;}
        if(!c.monster_count){if(ticks)*ticks=c.hold_ticks;return !full && g.known && g.reward && !g.respawning && travel(actor,c.hold_ticks);}
        const auto f=forecast_combat(c);if(f.state!=EstimateState::feasible)return false;
        actor.health=f.remaining_health[0];if(ticks)*ticks=f.clear_ticks+c.hold_ticks;return reserve_safe(actor);
    }
    bool renewal_work(const n::Snapshot& s,const n::GroupFacts& g,CombatActor actor,int remaining) const {
        if(!usable(g))return false;int work_ticks=0;if(!work(g,actor,s.settings,true,&work_ticks))return false;
        const auto to_altar=leg({n::NodeKind::group_entry,g.id},{n::NodeKind::altar_entry,altar_},true);
        const auto back=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,g.id});
        if(to_altar.turns()<0 || back.turns()<0)return false;
        const int tail=to_altar.turns()+s.settings.channel_ticks(g.gems)+back.turns();
        return work_ticks+tail<=remaining && travel(actor,tail);
    }
    static bool apply_gain(CombatActor& a,const n::Snapshot& s,const Decision& d) {
        const auto* q=quote(s,d.purchase_family);
        if(!q || q->item_id!=d.purchase_item || q->cost!=d.purchase_cost || !q->purchasable)return false;
        a.strength+=q->strength_gain;a.max_health+=q->health_gain;return reserve_safe(a); // Never field healing.
    }
    bool certify(const n::Snapshot& s,const Decision& d) const {
        if(!d.feasible || d.rank.status!=AccountingStatus::feasible || d.rank.occupied_turns<0 || d.rank.occupied_turns>s.windows.official_remaining)return false;
        auto actor=conservative(s.stats);
        if(d.job==Job::two_groups){
            const auto* first=group(s,d.first_group);const auto* second=group(s,d.successor_group);
            if(!first || !second || first->id==second->id || !usable(*first) || !usable(*second))return false;
            if(d.timing==Timing::now && !apply_gain(actor,s,d))return false;
            if(!work(*first,actor,s.settings))return false;
            if(d.timing==Timing::after_first_capture && !apply_gain(actor,s,d))return false;
            if(!work(*second,actor,s.settings,true))return false;
        }
        actor.health-=s.route_stress*d.rank.occupied_turns;actor.visible_enemy_next_phase=s.route_stress;
        if(!reserve_safe(actor))return false;if(d.schedule.terminal_win)return true;
        const auto* next=group(s,return_id(d));
        return next && next->id!=d.first_group && next->id!=d.successor_group &&
            renewal_work(s,*next,actor,s.windows.official_remaining-d.rank.occupied_turns);
    }
    bool postbuy_proven(const n::Snapshot& s) const {
        const auto& d=project_.selected;
        const bool bought=(d.timing==Timing::now && project_.stage==ProjectStage::first_production) ||
            (d.timing==Timing::after_first_capture && project_.stage==ProjectStage::second_production);
        if(!bought)return true;const auto* q=quote(s,d.purchase_family);
        return q && q->item_id==d.purchase_item && s.stats.strength>=initial_strength_+q->strength_gain &&
            s.stats.max_health>=initial_max_health_+q->health_gain && s.stats.basic_modifier>=initial_basic_modifier_;
    }
    bool remaining_safe(const n::Snapshot& s) const {
        const auto& d=project_.selected;auto actor=conservative(s.stats);int duration=0;
        if(!postbuy_proven(s))return false;
        const bool first=project_.stage==ProjectStage::purchase_now || project_.stage==ProjectStage::first_production;
        const bool second=first || project_.stage==ProjectStage::deferred_purchase || project_.stage==ProjectStage::second_production;
        if(project_.stage==ProjectStage::purchase_now && !apply_gain(actor,s,d))return false;
        if(first){const auto* g=group(s,d.first_group);if(!g || !work(*g,actor,s.settings))return false;
            duration+=d.forecast.travel+d.forecast.first_clear+d.forecast.first_capture;
            if(d.timing==Timing::after_first_capture && !apply_gain(actor,s,d))return false;}
        else if(project_.stage==ProjectStage::deferred_purchase && !apply_gain(actor,s,d))return false;
        if(second){const auto* g=group(s,d.successor_group);if(!g || !work(*g,actor,s.settings,first))return false;
            duration+=d.forecast.between+d.forecast.second_clear+d.forecast.second_capture;}
        if(project_.stage!=ProjectStage::renewal)duration+=d.forecast.conversion_travel+d.forecast.channel;
        duration+=d.forecast.return_to_work;
        if(d.job==Job::bank_now)duration=d.rank.occupied_turns;
        // Full configured remaining stages are a conservative bound. Actual
        // current HP is used each round; no planned recovery or kit is invented.
        actor.health-=s.route_stress*duration;actor.visible_enemy_next_phase=s.route_stress;
        if(!reserve_safe(actor))return false;if(d.schedule.terminal_win)return true;
        const auto* next=group(s,renewal_);return next && renewal_work(s,*next,actor,s.windows.official_remaining-duration);
    }
    ProjectFacts facts(const n::Snapshot& s) const {
        ProjectFacts f;f.observation_round=s.round;f.own_actor=s.self.id;f.own_gold=s.self.gold.value;f.own_bag=s.self.bag.value;
        f.own_capture_receipt=s.receipts.capture;f.own_bank_receipt=s.receipts.bank;f.own_purchase_receipt=s.receipts.purchase;
        f.own_purchase_confirmed=s.receipts.purchase_count==1;f.purchased_family=s.receipts.purchase_family;
        f.purchased_item=s.receipts.purchase_item;f.purchased_cost=s.receipts.purchase_cost;f.purchased_shards=s.receipts.purchase_count;
        f.productive_waypoint=renewal_;f.at_named_productive_waypoint=project_.stage==ProjectStage::renewal && renewal_>=0 && runtime_.productive_ready(renewal_);
        return f;
    }
    n::Goal goal() const {
        n::Goal g;g.controller_stage=int(project_.stage);g.phase=project_.execution_state.phase;g.altar_tile_id=altar_;
        g.forecast_turns=std::max(1,project_.selected.rank.occupied_turns);g.purchase_family=project_.selected.purchase_family;
        g.group_id=project_.stage==ProjectStage::first_production?project_.selected.first_group:project_.selected.successor_group;
        g.production=project_.stage==ProjectStage::first_production || project_.stage==ProjectStage::second_production;
        g.bank_requested=project_.stage==ProjectStage::output_bank;
        if(g.phase==e::Phase::recover)g.kind=n::GoalKind::recover;
        else if(project_.stage==ProjectStage::renewal){g.kind=n::GoalKind::productive_entry;g.group_id=renewal_;}
        else if(project_.stage==ProjectStage::purchase_now || project_.stage==ProjectStage::deferred_purchase)g.kind=n::GoalKind::spawn;
        else if(project_.stage==ProjectStage::output_bank)g.kind=g.phase==e::Phase::allocate?n::GoalKind::camp_entry:n::GoalKind::altar;
        else if(g.phase==e::Phase::hold)g.kind=n::GoalKind::camp_spawn;
        else if(g.phase==e::Phase::clear)g.kind=n::GoalKind::camp_attack;
        else g.kind=n::GoalKind::camp_entry;return g;
    }
    static bool movement(e::ActionKind k) {return k==e::ActionKind::move || k==e::ActionKind::vacate || k==e::ActionKind::withdraw;}
    void stale_turn() {running_=false;project_.stage=ProjectStage::aborted;runtime_.finish_turn();}
public:
    explicit Bot(Arm arm){profile_.arm=arm;}
    void turn() {
        if(cambc::self().champion_name()!=cambc::champion_type::tavi || cambc::constants::passive_gold!=0){runtime_.reference_turn();return;}
        if(!runtime_.begin_turn())return;const int round=int(cambc::round_number());
        if(handled_round_==round){runtime_.finish_turn();return;}handled_round_=round;
        if(!running_ && runtime_.reference_busy()){runtime_.reference_turn();P5_WORKSHOP_GATE(1,-1);return;}
#if PLAN5_WORKSHOP_BOOTSTRAP
        if(!running_){
            const int possible=runtime_.visible_paid_source_upper_bound();
            if(possible<0){stale_turn();return;}
            // The unchanged full project needs three usable observed sources.
            // Avoid constructing its expensive packet when visibility alone
            // already proves that admission impossible on this owner turn.
            if(possible<3){runtime_.reference_turn();P5_WORKSHOP_GATE(7,possible);return;}
        }
#endif
        auto& s=snapshot_;if(!runtime_.snapshot_into(s) || !s.complete || s.round!=int(cambc::round_number())){stale_turn();P5_WORKSHOP_GATE(2,-1);return;}
        if(!s.alive || !s.settings_valid)running_=false;
        if(!running_){
            int useful=0;for(int i=0;i<s.group_count;++i)useful+=usable(s.groups[i]);
            // No queried respawn budget is exported: a third observed source
            // certifies productive renewal. Sparse views use ordinary reference.
            if(useful<3 || !current(s) || !runtime_.prepare_selection()){
                if(s.round!=int(cambc::round_number())){stale_turn();return;}runtime_.reference_turn(&s);P5_WORKSHOP_GATE(3,useful);return;}
            altar_=nearest_altar(s);const auto start=cambc::fuel_remaining();
            const bool built=altar_>=0 && observation_into(s,planning_);const auto after_build=cambc::fuel_remaining();
            planning_.fuel_remaining=int(after_build);const int built_round=int(cambc::round_number());
            if(built_round!=s.round){stale_turn();return;}
            if(!built || planning_.count==0 || after_build<45000){
                runtime_.record_card_plan(start,after_build,after_build,after_build,planning_.count,false,built_round);
                runtime_.reference_turn(&s);P5_WORKSHOP_GATE(4,int(planning_.count));return;}
            selection_=choose_job(planning_,{},profile_);const auto chosen=cambc::fuel_remaining();
            const bool certified=current(s,30000) && certify(s,selection_);const auto after_cert=cambc::fuel_remaining();const int end=int(cambc::round_number());
            runtime_.record_card_plan(start,after_build,chosen,after_cert,planning_.count,certified,end);
            if(end!=s.round){stale_turn();return;}if(!certified){runtime_.reference_turn(&s);P5_WORKSHOP_GATE(5,int(planning_.count));return;}
            renewal_=return_id(selection_);initial_strength_=s.stats.strength;initial_max_health_=s.stats.max_health;initial_basic_modifier_=s.stats.basic_modifier;
            project_=start_project(selection_,facts(s));running_=project_.stage!=ProjectStage::aborted;
        }
        if(running_)for(int transition=0;transition<4;++transition){
            if(!current(s,16000)){stale_turn();return;}
            const bool producing=project_.stage==ProjectStage::first_production || project_.stage==ProjectStage::second_production;
            if(producing && project_.execution_state.phase==e::Phase::bootstrap && !runtime_.prepare_selection()){
                if(s.round!=int(cambc::round_number())){stale_turn();return;}break;}
            auto before=goal();const auto& receipts=s.receipts;
            const int capture_group=project_.stage==ProjectStage::first_production?project_.selected.first_group:project_.selected.successor_group;
            const bool capture=producing && receipts.capture_round==s.round && receipts.capture_group==capture_group && receipts.capture>project_.capture_baseline;
            const bool buy=(project_.stage==ProjectStage::purchase_now || project_.stage==ProjectStage::deferred_purchase) &&
                project_.purchase_intent && receipts.purchase>project_.purchase_baseline;
            const bool bank=project_.stage==ProjectStage::output_bank && project_.bank_intent && receipts.bank>project_.bank_baseline && s.self.bag.value==0;
            const bool transition_only=capture || buy || bank;
            if(transition_only)before.production=false; // Receipt is checked before the old respawning work veto.
            auto observed=runtime_.execution_observation(project_.execution_state,before);
            if(observed.owner_turn!=int(cambc::round_number())){stale_turn();return;}
            if(observed.own_payout_observed && observed.payout_receipt==s.receipts.capture && s.receipts.capture_round==s.round && s.receipts.capture_gold_known)
                observed.own_gold_received=s.receipts.capture_gold;
            // Neither arm spends an unmodeled cast/reset; actual equipment is
            // always queried again by the runtime after its one native action.
            observed.spells={};
            if(transition_only){observed.fuel=0;observed.at_target=false;observed.can_act=observed.can_cast=observed.can_move=observed.can_attack=false;}
            auto selected=execute_job(observed,facts(s),project_);const auto after=goal();
            if(project_.stage==ProjectStage::complete || project_.stage==ProjectStage::aborted){running_=false;break;}
            if(!(before==after))continue;
            const bool priority=(observed.channel_valid && observed.channel_safe) ||
                (producing && observed.group_cleared && observed.on_group_spawn && observed.safe);
            if(!priority && !remaining_safe(s)){running_=false;project_.stage=ProjectStage::aborted;break;}
            if(selected.action.kind==e::ActionKind::yield || selected.action.kind==e::ActionKind::refresh || movement(selected.action.kind)){
                runtime_.add_moves(observed,project_.execution_state,after);if(s.round!=int(cambc::round_number())){stale_turn();return;}
                observed.spells={};selected=execute_job(observed,facts(s),project_);if(!(goal()==after))continue;
                if(project_.stage==ProjectStage::complete || project_.stage==ProjectStage::aborted){running_=false;break;}
            }
            if(selected.action.kind==e::ActionKind::buy){
                // Pure Workshop identifies the actual item. Runtime action
                // dispatch identifies its menu family; exact receipt retains
                // item/cost/count for Workshop's confirmation guard.
                const auto* q=quote(s,project_.selected.purchase_family);
                if(!q || selected.action.target!=q->item_id || q->item_id!=project_.selected.purchase_item){running_=false;project_.stage=ProjectStage::aborted;break;}
                selected.action.target=q->family;
            }
            const auto dispatched=runtime_.apply(selected,observed,project_.execution_state,after);
            if(!dispatch_keeps_project(dispatched,selected.action.kind,observed.safe,observed.channel_valid && observed.channel_safe)){
                running_=false;project_.stage=ProjectStage::aborted;}
            if(s.round!=int(cambc::round_number())){stale_turn();return;}break;
        }
        runtime_.finish_turn();
        P5_WORKSHOP_GATE(6,int(project_.stage));
    }
};
inline int run(Arm arm) {
    static Bot bot(arm);
    while(true){const auto result=e::turn_boundary([&]{
        if(cambc::is_draft())n::Runtime::draft(Profile::draft_request());else bot.turn();return e::Decision{};});
        if(result.log_error)std::printf("P5 UNEXPECTED_ENGINE_ERROR Workshop round-boundary; yielding\n");cambc::end_turn();}
}
} // namespace p5::workshop::native_binding
#undef P5_WORKSHOP_GATE
