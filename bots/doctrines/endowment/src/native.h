#pragma once
#include "policy.h"
#include "../../reference/src/runtime.h"
#include <new>
#include <type_traits>

// Actual Prism caller. No helper/readiness inference and no SDK action bypass.
// Native v1 keeps a distinct currently observed basic-safe renewal and rejects
// unnormalized equipment passives/emblem activation. Runtime owns the typed,
// fresh current-camp primary authorization and exact native action receipts.
namespace p5::endowment::native_binding {
namespace n=p5::native;
namespace e=p5::execution;
// Private to each physical champion's WASM instance; no shared actor capital.
// Keep plain array payload initialization outside the first owner callback.
struct EconomicScratch {
    Project project{};n::Snapshot snapshot{};Observation planning{};Decision selection{};Group current_group{};
    execution::Observation observed{};ProjectFacts facts{};
};
inline constinit EconomicScratch economic_scratch{};
static_assert(std::is_trivially_destructible_v<Decision> && std::is_trivially_destructible_v<Project> &&
    std::is_trivially_destructible_v<e::Observation> && std::is_trivially_destructible_v<ProjectFacts>);
constexpr bool primary_spent(bool confirmed,std::uint64_t pending,int emitted_round,int observed_round){
    return confirmed || (pending!=0 && emitted_round<observed_round);
}
constexpr e::Phase primary_entry_phase(bool named_entry,bool legal_primary,e::Phase previous){
    return named_entry && legal_primary && (previous==e::Phase::bootstrap || previous==e::Phase::travel)?e::Phase::clear:previous;
}
static_assert(!primary_spent(false,1,4,4));
static_assert(primary_spent(false,1,4,5));
static_assert(primary_spent(true,0,-1,4));
static_assert(primary_entry_phase(true,true,e::Phase::bootstrap)==e::Phase::clear);
static_assert(primary_entry_phase(true,true,e::Phase::travel)==e::Phase::clear);
static_assert(primary_entry_phase(false,true,e::Phase::travel)==e::Phase::travel);
constexpr bool dispatch_keeps_project(n::ApplyResult result,e::ActionKind kind,bool safe,bool safe_channel){
    return result==n::ApplyResult::acted || (result==n::ApplyResult::stopped &&
        (((kind==e::ActionKind::hold || kind==e::ActionKind::yield) && safe) ||
         (kind==e::ActionKind::preserve_channel && safe_channel)));
}
static_assert(dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::hold,true,false));
static_assert(dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::preserve_channel,false,true));
static_assert(!dispatch_keeps_project(n::ApplyResult::stopped,e::ActionKind::cast,true,false));
static_assert(!dispatch_keeps_project(n::ApplyResult::stale,e::ActionKind::hold,true,false));
class Bot {
    n::Runtime runtime_{};Profile profile_{};Project& project_=economic_scratch.project;
    n::Snapshot& snapshot_=economic_scratch.snapshot;Observation& planning_=economic_scratch.planning;
    Decision& selection_=economic_scratch.selection;Group& current_group_=economic_scratch.current_group;Focus focus_{};
    e::Observation& observed_=economic_scratch.observed;ProjectFacts& facts_=economic_scratch.facts;
    int altar_=-1,renewal_=-1,handled_round_=-1;
    bool running_=false,metadata_ready_=false;
    double focus_growth_=0;
    e::Spell primary_{};

    static bool current(const n::Snapshot& s,unsigned floor=45000){
        return s.complete && s.round==int(cambc::round_number()) && cambc::fuel_remaining()>=floor;
    }
    static const n::GroupFacts* group(const n::Snapshot& s,int id){for(int i=0;i<s.group_count;++i)if(s.groups[i].id==id)return &s.groups[i];return nullptr;}
    static bool usable(const n::GroupFacts& g){return g.known && !g.respawning && !g.other_holder && g.gems>0 &&
        g.combat.actor_count==1 && g.combat.monster_count>0 && g.combat.monster_count<=8;}
    static CombatActor conservative(CombatActor a,double stress){
        a.shield=0;a.shield_expiry_tick=0;a.direct_or_reset_damage=0;a.spell_known_ready=a.legal_first_cast=a.reserved_cast=a.heal_legal_ready=false;
        a.heal_amount=0;a.heal_recipient=-1;a.visible_enemy_next_phase=stress;return a;
    }
    RouteLeg leg(n::Node from,n::Node to,bool lock_budget=false) const {
        auto r=runtime_.route(from,to);if(r.distance>=0 && lock_budget)r.distance+=int(cambc::constants::altar_lock_hits);
        r.safety_checked=r.turns()>=0;return r;
    }
    int nearest_altar(const n::Snapshot& s) const {
        int best=-1,cost=1000000;
        for(int i=0;i<s.altar_count;++i){const auto& a=s.altars[i];const auto r=runtime_.route({}, {n::NodeKind::altar_entry,a.tile_id});
            if(r.turns()<0)continue;const int n=r.distance+((a.visible && !a.hostile_lock)?0:int(cambc::constants::altar_lock_hits));
            if(n<cost || (n==cost && a.tile_id<best)){best=a.tile_id;cost=n;}}
        return best;
    }
    bool normalized_inventory() const {
        const auto items=cambc::self().item_count();
        for(std::uint32_t raw=0;raw<items.size();++raw){const cambc::item_type item{raw};
            if(items[item] && item.info().passive)return false;}
        return true;
    }
    bool focus_into(const n::Snapshot& s){
        auto& me=cambc::self();if(!normalized_inventory())return false;
        if(!metadata_ready_){focus_growth_=me.base_stats().focus_growth;metadata_ready_=true;}
        focus_.observation_round=s.round;focus_.valid_until_round=s.settings.round_limit;
        focus_.per_gem=cambc::constants::passive_abilities::prism_focus_per_gem;
        focus_.current=me.focus();focus_.permanent=focus_.current-focus_.per_gem*s.self.bag.value;
        // Current SDK buff types are shield/stun/enhanced-basic, none changes
        // Focus. Owned equipment is checked above and actual Focus is reread
        // after every bank/buy/loss; this is not a copied design-base constant.
        focus_.base_is_permanent=std::isfinite(focus_.permanent) && focus_.permanent>=0;
        focus_.modifiers={0,0};const auto ability=me.abilities()[0];
        if(ability.info() && ability.is_cc() && ability.can_target_camps() && focus_.current>0)
            focus_.modifiers[0]=ability.damage_amount()/focus_.current;
        return current(s,16000) && focus_.base_is_permanent && std::isfinite(focus_growth_) && focus_growth_>=0 &&
            std::isfinite(focus_.current) && std::isfinite(focus_.per_gem) && focus_.per_gem>=0 &&
            std::isfinite(focus_.modifiers[0]) && focus_.modifiers[0]>=0;
    }
    e::Spell primary_for(const n::Snapshot& s,const n::GroupFacts& g,int target=-1) const {
        e::Spell out;auto& me=cambc::self();const auto a=me.abilities()[0];
        if(!current(s,16000) || !a.info() || !a.is_cc() || !a.can_target_camps() || !a.is_ready() || !me.can_cast() || me.has_completed_turn() ||
            me.buffs().has_stun() || me.restrictions().contains(cambc::buff_restrictions::cast))return out;
        const auto mask=a.valid_tiles();
        for(const auto& m:cambc::vision::camps()){
            const int id=int(static_cast<cambc::handle_camp_t>(m));bool member=false;
            for(std::size_t i=0;i<g.combat.monster_count;++i)member|=g.combat.monsters[i].id==id;
            if(!member || (target>=0 && id!=target) || !m.is_visible() || !m.is_alive() || !mask.is_set(m.position()) || m.buffs().has_shield())continue;
            if(out.target<0 || id<out.target)out.target=id;
        }
        if(out.target>=0){out.ready=out.in_range_mask=out.visible=out.category_legal=out.useful=true;
            out.movement_exclusive=std::visit([](const auto& p){return p.exclusive_with_movement;},*a.info());}
        return out;
    }
    void copy_group(const n::Snapshot& s,const n::GroupFacts& g,Group& out,bool casts) const {
        out.id=g.id;out.gems=g.gems;out.gold=g.gold;out.live_observed=g.known;out.reward_or_alive=g.known && !g.respawning;
        out.monster_count=g.combat.monster_count;for(std::size_t m=0;m<g.combat.monster_count;++m)out.monsters[m]=g.combat.monsters[m];
        if(!out.monster_count && g.known && g.reward && !g.respawning){
            // Observed reward-available means every original monster is dead.
            // Preserve known public identity/HP metadata, not invented work.
            if(const auto* original=find_group(planning_,g.id)){
                out.monster_count=original->monster_count;
                for(std::size_t m=0;m<out.monster_count;++m){out.monsters[m]=original->monsters[m];out.monsters[m].visible=true;out.monsters[m].observed_health=0;}
            }
        }
        out.from_current=leg({}, {n::NodeKind::group_entry,g.id});out.from_altar=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,g.id});
        out.from_spawn=leg({n::NodeKind::spawn,0},{n::NodeKind::group_entry,g.id});
        out.to_altar=leg({n::NodeKind::group_entry,g.id},{n::NodeKind::altar_entry,altar_},true);
        out.return_to_work=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,renewal_});
        out.prism_casts={};if(casts)out.prism_casts[0]=primary_for(s,g);
    }
    bool basic_work(const n::GroupFacts& g,CombatActor a,const n::Snapshot& s,bool full=true) const {
        auto c=g.combat;c.actor_count=1;c.actors[0]=a;c.hold_ticks=s.settings.capture_rounds+1;
        if(full)for(std::size_t m=0;m<c.monster_count;++m){c.monsters[m].visible=false;c.monsters[m].observed_health=c.monsters[m].public_health;}
        return usable(g) && forecast_combat(c).state==EstimateState::feasible;
    }
    int nearest_basic(const n::Snapshot& s,int excluded) const {
        int best=-1,cost=1000000;
        for(int i=0;i<s.group_count;++i){if(!current(s))return -1;const auto& g=s.groups[i];if(g.id==excluded || !basic_work(g,conservative(s.stats,s.route_stress),s))continue;
            const auto r=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,g.id});if(r.turns()<0)continue;
            if(r.distance<cost || (r.distance==cost && g.id<best)){best=g.id;cost=r.distance;}}
        return best;
    }
    bool observation_into(const n::Snapshot& s){
        auto& o=planning_;o.actor_count=1;o.prism_index=0;o.round=s.round;o.team_score=s.team_score;o.focus=focus_;
        o.actors[0].resources=s.self;o.actors[0].stats=conservative(s.stats,s.route_stress);o.actors[0].tribute_income_eligible=false;
        o.settings=s.settings;o.windows=s.windows;o.group_count=o.purchase_count=0;
        o.bootstrap_partner_visible_usable=false;o.concentrate_bootstrap=true;o.needs_recovery=s.needs_recovery;o.safe_active_capture=s.safe_hold;
        bool unlocked=false;for(int i=0;i<s.altar_count;++i)if(s.altars[i].tile_id==altar_)unlocked=s.altars[i].adjacent && s.altars[i].visible && !s.altars[i].hostile_lock;
        o.current_to_altar=leg({}, {n::NodeKind::altar_entry,altar_},!unlocked);
        o.altar_to_spawn=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::spawn,0});
        o.final_return=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,renewal_});
        o.final_return_productive=renewal_>=0 && o.final_return.turns()>=0;
        o.rebuild_group=nearest_basic(s,renewal_);o.bootstrap_group=o.rebuild_group;
        o.bank_legal=o.safe_bank_reachable=altar_>=0 && o.current_to_altar.turns()>=0;
        o.spawn_rest_ticks=1;const double fraction=cambc::constants::spawn_healing;
        if(!std::isfinite(fraction) || fraction<0 || fraction>1)return false;
        o.queried_spawn_heal_per_tick=fraction*s.stats.max_health;
        const auto items=cambc::self().item_count();
        for(const auto& q:s.purchases){if(q.family<0 || q.family>=3 || q.item_id<0 || !q.purchasable || q.cost<=0)continue;
            const cambc::item_type item{std::uint32_t(q.item_id)};const auto info=item.info();
            if(info.passive || (info.result && info.combine_count>0 && items[item]+1>=info.combine_count))continue;
            auto& p=o.purchases[o.purchase_count++];p.family=q.family;p.item=q.item_id;p.shards=1;p.cost=q.cost;
            p.strength_gain=q.strength_gain;p.health_gain=q.health_gain;p.focus_gain=info.focus_points*focus_growth_;
            p.actual_spawn_purchase_heal=q.health_gain;p.legal_on_spawn=true;
        }
        for(int i=0;i<s.group_count && o.group_count<o.groups.size();++i){if(!current(s))return false;
            const auto& g=s.groups[i];if(g.id!=renewal_ && usable(g))copy_group(s,g,o.groups[o.group_count++],true);}
        o.execution={};o.execution.owner_turn=s.round;o.execution.remaining_rounds=s.windows.official_remaining;o.execution.fuel=int(cambc::fuel_remaining());
        o.execution.safe=s.stats.health>=std::max(.2*s.stats.max_health,2*s.next_incoming);o.execution.alive=s.alive;
        o.execution.can_act=!cambc::self().has_completed_turn() && !cambc::self().buffs().has_stun();o.execution.can_cast=cambc::self().can_cast();
        o.execution.channel=s.channel;o.execution.channel_valid=s.channel!=e::Channel::none;o.execution.channel_safe=s.channel_safe;
        o.execution.needs_recovery=s.needs_recovery;o.execution.spells={};o.execution.spells[0].ready=cambc::self().abilities()[0].is_ready();
        return current(s) && o.group_count>=2 && o.rebuild_group>=0 && o.final_return_productive;
    }
    bool renewal_proof(const n::Snapshot& s,CombatActor a,int remaining) {
        const auto* next=group(s,renewal_);if(!next || !usable(*next))return false;copy_group(s,*next,current_group_,false);
        for(std::size_t m=0;m<current_group_.monster_count;++m){current_group_.monsters[m].visible=false;current_group_.monsters[m].observed_health=current_group_.monsters[m].public_health;}
        int clear=0;if(!fight(planning_,current_group_,a,0,focus_.permanent,-1,clear))return false;
        const int tail=turns(current_group_.to_altar)+s.settings.channel_ticks(next->gems)+turns(current_group_.return_to_work);
        return turns(current_group_.to_altar)>=0 && turns(current_group_.return_to_work)>=0 && clear+s.settings.capture_rounds+1+tail<=remaining && travel(a,tail);
    }
    bool tail_proof(const n::Snapshot& s){
        const auto& plan=project_.selected.actors[0];if(project_.step_index<0 || project_.step_index>=plan.count)return false;
        auto a=conservative(s.stats,s.route_stress);int bag=s.self.bag.value,t=0;double permanent=focus_.permanent;
        // A same-owner emitted intent has not reached Runtime.apply yet.
        // Only a previous dispatched packet or confirmed feedback is spent;
        // its continuation uses actual remaining HP, never forecast damage.
        bool spent=primary_spent(project_.cast_done,project_.pending_cast,project_.pending_cast_turn,s.round);int location=-1;
        for(int i=project_.step_index;i<plan.count;++i){const auto& step=plan.steps[i];const bool first=i==project_.step_index;
            if(step.kind==StepKind::produce){const auto* g=group(s,step.group);if(!g || (!g->known && first) || g->respawning || g->other_holder)return false;
                copy_group(s,*g,current_group_,false);int slot=-1;
                if(step.cast_slot>=0 && !(first && spent)){
                    // No future ready/geometry speculation. Only the current
                    // first primary event receives a fresh SDK certificate.
                    if(!first || step.cast_slot!=0)return false;primary_=primary_for(s,*g,step.cast_target);
                    if(primary_.target!=step.cast_target)return false;current_group_.prism_casts[0]=primary_;slot=0;spent=true;
                }
                const auto route=first?current_group_.from_current:location==-3?current_group_.from_spawn:current_group_.from_altar;
                const int distance=turns(route);int clear=0;
                if(distance<0 || !travel(a,distance) || !fight(planning_,current_group_,a,bag,permanent,slot,clear))return false;
                if(bag>1000000-g->gems)return false;bag+=g->gems;t+=distance+clear+s.settings.capture_rounds+1;location=g->id;
            }else if(step.kind==StepKind::bank){
                int distance=0;if(!(first && [&]{for(int j=0;j<s.altar_count;++j)if(s.altars[j].tile_id==step.waypoint)return s.altars[j].adjacent;return false;}())){
                    if(location>=0){const auto* g=group(s,location);if(!g)return false;copy_group(s,*g,current_group_,false);distance=turns(current_group_.to_altar);}
                    else distance=turns(planning_.current_to_altar);}
                const int n=s.settings.channel_ticks(bag);if(distance<0 || n<1 || !travel(a,distance+n))return false;t+=distance+n;bag=0;location=-2;
            }else if(step.kind==StepKind::purchase){
                const Purchase* p=nullptr;for(std::size_t j=0;j<planning_.purchase_count;++j)if(planning_.purchases[j].item==step.purchase_item)p=&planning_.purchases[j];
                if(!p)return false;const int distance=first && s.at_spawn?0:turns(planning_.altar_to_spawn);
                if(distance<0 || !travel(a,distance+1))return false;t+=distance+1;
                // This admission proof uses actual HP, with no hypothetical
                // spawn healing. The policy also requires actual restored HP
                // and gear on its later exact purchase receipt.
                a.strength+=p->strength_gain;a.max_health+=p->health_gain;permanent+=p->focus_gain;if(!safe(a))return false;location=-3;
            }else if(step.kind==StepKind::renew){
                const int distance=turns(planning_.final_return);if(distance<0 || !travel(a,distance))return false;t+=distance;
                return bag==0 && t<=s.windows.official_remaining && renewal_proof(s,a,s.windows.official_remaining-t);
            }else return false;
        }
        return project_.selected.schedule.terminal_win && bag==0 && t<=s.windows.official_remaining;
    }
    n::Goal goal() const {
        n::Goal g;g.phase=project_.executor.phase;g.controller_stage=project_.step_index;
        const auto& p=project_.selected.actors[0];if(project_.step_index<0 || project_.step_index>=p.count)return g;
        const auto& s=p.steps[project_.step_index];g.group_id=s.group;g.altar_tile_id=altar_;g.forecast_turns=std::max(1,p.occupied);
        g.production=s.kind==StepKind::produce;g.bank_requested=s.kind==StepKind::bank;g.purchase_family=s.purchase_family;
        if(g.phase==e::Phase::recover)g.kind=n::GoalKind::recover;
        else if(s.kind==StepKind::purchase)g.kind=n::GoalKind::spawn;
        else if(s.kind==StepKind::bank){g.kind=n::GoalKind::altar;g.altar_tile_id=s.waypoint;}
        else if(s.kind==StepKind::renew){g.kind=n::GoalKind::productive_entry;g.group_id=s.waypoint;}
        else if(g.phase==e::Phase::hold)g.kind=n::GoalKind::camp_spawn;
        else if(g.phase==e::Phase::clear)g.kind=n::GoalKind::camp_attack;else g.kind=n::GoalKind::camp_entry;
        return g;
    }
    bool bind(n::Goal& g,const n::Snapshot& s){
        const auto& step=project_.selected.actors[0].steps[project_.step_index];
        if(step.kind!=StepKind::produce || step.cast_slot<0)return runtime_.bind_target_combat(g,s,n::TargetCombatMode::basic_only);
        return runtime_.bind_target_combat(g,s,n::TargetCombatMode::prism_one_primary,step.cast_slot,step.cast_target);
    }
    const ProjectFacts& facts(const n::Snapshot& s) {
        auto& f=facts_;f={};f.observation_round=s.round;f.own_actor=s.self.id;f.own_bag=s.self.bag.value;f.own_gold=s.self.gold.value;
        f.own_focus=focus_.current;f.own_permanent_focus=focus_.permanent;f.own_stats=s.stats;
        f.capture_receipt=s.receipts.capture;f.bank_receipt=s.receipts.bank;f.purchase_receipt=s.receipts.purchase;
        f.purchase_confirmed=s.receipts.purchase_count==1;f.purchased_item=s.receipts.purchase_item;f.purchased_family=s.receipts.purchase_family;
        f.purchased_cost=s.receipts.purchase_cost;f.purchased_shards=s.receipts.purchase_count;f.productive_waypoint=renewal_;
        if(running_ && project_.step_index<project_.selected.actors[0].count && project_.selected.actors[0].steps[project_.step_index].kind==StepKind::renew)
            f.at_named_productive_waypoint=runtime_.productive_ready(renewal_) && renewal_proof(s,conservative(s.stats,s.route_stress),s.windows.official_remaining);
        return f;
    }
    e::Observation& observe(const n::Goal& goal){
        // Construct returned plain payloads directly in actor-private storage.
        // Assignment would require a second full return object on turn's stack.
        ::new (static_cast<void*>(&observed_)) e::Observation(runtime_.execution_observation(project_.executor,goal));
        return observed_;
    }
    bool initialize_primary_clear(const n::Snapshot& s){
        const auto& plan=project_.selected.actors[0];if(project_.step_index<0 || project_.step_index>=plan.count)return false;
        const auto& step=plan.steps[project_.step_index];if(step.kind!=StepKind::produce || step.cast_slot<0 ||
            project_.cast_done || project_.pending_cast)return true;
        const auto* target=group(s,step.group);if(!target || !target->known || target->respawning)return false;
        bool at_entry=s.position_tile_id==target->entry_tile_id;
        if(!at_entry){const auto own=cambc::self().position();
            // Public group geometry retains dead-member spawn tiles. Current
            // live combat prefixes deliberately omit those dead monsters.
            for(const auto& public_group:cambc::map::camp_groups())if(n::gid(public_group)==step.group){
                at_entry=public_group.spawn_tiles().is_set(own);break;}}
        const auto legal=primary_for(s,*target,step.cast_target);
        const auto phase=primary_entry_phase(at_entry,legal.target==step.cast_target,project_.executor.phase);
        if(phase!=e::Phase::clear || project_.executor.phase==e::Phase::clear)return true;
        if(!project_.initialized){
            if(s.self.bag.value!=plan.initial_bag || s.self.gold.value!=plan.initial_gold)return false;
            project_.initialized=true;project_.shop_arrival=-1;project_.intent=0;project_.cast_done=false;project_.pending_cast=0;
            project_.bag_baseline=s.self.bag.value;project_.gold_baseline=s.self.gold.value;
            project_.capture_baseline=s.receipts.capture;project_.bank_baseline=s.receipts.bank;project_.purchase_baseline=s.receipts.purchase;
        }else if(s.self.bag.value!=project_.bag_baseline || s.self.gold.value!=project_.gold_baseline)return false;
        e::Observation point;point.owner_turn=s.round;point.remaining_rounds=s.windows.official_remaining;point.forecast_turns=std::max(1,plan.occupied);
        e::enter(project_.executor,phase,point);project_.executor.target=step.group;project_.executor.travel_next=e::Phase::clear;return current(s,16000);
    }
    static bool movement(e::ActionKind k){return k==e::ActionKind::move || k==e::ActionKind::vacate || k==e::ActionKind::withdraw;}
    void finish_project(){running_=false;runtime_.reset_target_combat();}
    void stop(){project_.stage=Stage::aborted;finish_project();}
    void stale(){stop();runtime_.finish_turn();}
public:
    explicit Bot(Arm arm){profile_.arm=arm;}
    void turn(){
        if(cambc::self().champion_name()!=cambc::champion_type::prism || cambc::constants::passive_gold!=0){runtime_.reference_turn();return;}
        if(!runtime_.begin_turn())return;const int now=int(cambc::round_number());if(handled_round_==now){runtime_.finish_turn();return;}handled_round_=now;
        if(!running_ && runtime_.reference_busy()){runtime_.reference_turn();return;}
        auto& s=snapshot_;if(!runtime_.snapshot_into(s) || !current(s,16000)){stale();return;}
        if(s.self.bag.value>0 && std::int64_t(s.team_score)+s.self.bag.value>=s.settings.winning_gems){
            stop();runtime_.reference_turn(&s);return;}
        if(!focus_into(s)){stop();runtime_.reference_turn(&s);return;}
        if(!running_){int count=0;for(int i=0;i<s.group_count;++i)count+=usable(s.groups[i]);
            if(count<3 || !current(s) || !runtime_.prepare_selection()){if(s.round!=int(cambc::round_number())){stale();return;}runtime_.reference_turn(&s);return;}
            const auto start=cambc::fuel_remaining();altar_=nearest_altar(s);renewal_=nearest_basic(s,-1);
            if(altar_<0 || renewal_<0 || !observation_into(s)){if(s.round!=int(cambc::round_number())){stale();return;}runtime_.reference_turn(&s);return;}
            const auto built=cambc::fuel_remaining();selection_.~Decision();
            ::new (static_cast<void*>(&selection_)) Decision(choose_job(planning_,profile_));
            const auto chosen=cambc::fuel_remaining();
            if(!current(s,30000)){stale();return;}
            runtime_.reset_target_combat();const auto& initial=facts(s);project_.~Project();
            ::new (static_cast<void*>(&project_)) Project(start_project(selection_,initial));running_=project_.stage==Stage::running;
            const bool certified=running_ && tail_proof(s);const auto endfuel=cambc::fuel_remaining();const int endround=int(cambc::round_number());
            runtime_.record_card_plan(start,built,chosen,endfuel,planning_.group_count,certified,endround);
            if(endround!=s.round){stale();return;}if(!certified){stop();runtime_.reference_turn(&s);return;}
        }
        for(int transition=0;running_ && transition<8;++transition){
            if(!current(s,16000)){stale();return;}
            const auto& step=project_.selected.actors[0].steps[project_.step_index];
            const bool capture=(step.kind==StepKind::produce && s.receipts.capture_round==s.round && s.receipts.capture_group==step.group && s.receipts.capture>project_.capture_baseline);
            const bool bank=(step.kind==StepKind::bank && project_.intent && s.receipts.bank>project_.bank_baseline && s.self.bag.value==0);
            const bool buy=(step.kind==StepKind::purchase && project_.intent && s.receipts.purchase>project_.purchase_baseline);
            if(capture || bank || buy){
                // Real payout/bank/buy evidence advances the pure controller
                // before its old target can become respawning or bag0. This
                // observation authorizes no action; a new step is rebound below.
                auto receipt_goal=goal();receipt_goal.production=false;
                if(!runtime_.bind_target_combat(receipt_goal,s,n::TargetCombatMode::basic_only)){stop();break;}
                auto& observed=observe(receipt_goal);
                if(observed.owner_turn!=int(cambc::round_number())){stale();return;}
                if(observed.own_payout_observed && observed.payout_receipt==s.receipts.capture && s.receipts.capture_gold_known)
                    observed.own_gold_received=s.receipts.capture_gold;
                // Process only the actual receipt. The next stage receives a
                // fresh goal/binding before it may emit any native intent.
                observed.fuel=0;observed.at_target=false;observed.can_act=observed.can_cast=observed.can_move=observed.can_attack=false;
                const int prior=project_.step_index;(void)execute_job(observed,facts(s),project_);
                if(project_.stage!=Stage::running){finish_project();break;}
                if(project_.step_index==prior){stop();break;}continue;
            }
            if(!initialize_primary_clear(s)){stop();break;}
            auto before=goal();if(before.production && before.phase==e::Phase::bootstrap && !runtime_.prepare_selection())break;
            if(!tail_proof(s) || !bind(before,s)){stop();break;}
            auto& observed=observe(before);if(observed.owner_turn!=int(cambc::round_number())){stale();return;}
            if(observed.own_payout_observed && observed.payout_receipt==s.receipts.capture && s.receipts.capture_round==s.round && s.receipts.capture_gold_known)
                observed.own_gold_received=s.receipts.capture_gold;
            auto selected=execute_job(observed,facts(s),project_);
            if(project_.stage!=Stage::running){finish_project();break;}
            auto after=goal();if(!tail_proof(s) || !bind(after,s)){stop();break;}if(!(before==after))continue;
            if(selected.action.kind==e::ActionKind::yield || selected.action.kind==e::ActionKind::refresh || movement(selected.action.kind)){
                runtime_.add_moves(observed,project_.executor,after);if(s.round!=int(cambc::round_number())){stale();return;}
                selected=execute_job(observed,facts(s),project_);if(project_.stage!=Stage::running){finish_project();break;}
                auto refreshed=goal();if(!tail_proof(s) || !bind(refreshed,s)){stop();break;}if(!(after==refreshed))continue;
            }
            if(selected.action.kind==e::ActionKind::buy){const auto& step=project_.selected.actors[0].steps[project_.step_index];
                if(selected.action.target!=step.purchase_item){stop();break;}selected.action.target=step.purchase_family;}
            const auto dispatched=runtime_.apply(selected,observed,project_.executor,after);
            if(!dispatch_keeps_project(dispatched,selected.action.kind,observed.safe,observed.channel_valid && observed.channel_safe))stop();
            if(s.round!=int(cambc::round_number())){stale();return;}break;
        }
        runtime_.finish_turn();
    }
};
inline int run(Arm arm){static Bot bot(arm);while(true){const auto result=e::turn_boundary([&]{
    if(cambc::is_draft())n::Runtime::draft(Profile::draft_request());else bot.turn();return e::Decision{};});
    if(result.log_error)std::printf("P5 UNEXPECTED_ENGINE_ERROR Endowment round-boundary; yielding\n");cambc::end_turn();}}
} // namespace p5::endowment::native_binding
