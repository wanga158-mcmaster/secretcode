#pragma once
#include "policy.h"
#include "../../reference/src/runtime.h"

// Actual Ruin only. Local reads add Focus/shield/escape evidence to the common
// packet; every action still passes through Runtime's typed, fresh dispatcher.
// This first binding rejects nonzero upkeep, unknown gold modifiers, visible
// enemy pursuit and an unproved two-step escape. It never creates injury.
namespace p5::collateral::native_binding {
namespace n=p5::native;
namespace e=p5::execution;
constexpr e::Phase native_production_phase(bool shield,bool at_actual_entry){return shield && at_actual_entry?e::Phase::clear:e::Phase::travel;}
constexpr bool prospective_shield(bool selected,bool done,std::uint64_t pending,int emitted_round,int observed_round){
    return selected && !done && (pending==0 || emitted_round==observed_round);
}
static_assert(prospective_shield(true,false,0,-1,4));
static_assert(prospective_shield(true,false,1,4,4));
static_assert(!prospective_shield(true,false,1,4,5));
static_assert(!prospective_shield(true,true,0,-1,4));
constexpr double escape_health_after(double health,double shield,int expiry,int steps,double incoming){
    const int protected_steps=steps<expiry?steps:expiry;
    const double protected_damage=protected_steps*incoming;
    const double absorbed=shield<protected_damage?shield:protected_damage;
    return health-steps*incoming+absorbed;
}
constexpr bool escape_budget_safe(double health,double maximum,double shield,int expiry,int steps,double incoming){
    if(!(health>0 && health<=maximum && maximum<=std::numeric_limits<double>::max() && shield>=0 &&
        shield<=std::numeric_limits<double>::max() && incoming>=0 && incoming<=std::numeric_limits<double>::max()) || expiry<0 || steps<0 || steps>2)return false;
    const double after=escape_health_after(health,shield,expiry,steps,incoming);
    return after>0 && after>=.2*maximum && after>=2*incoming;
}
// Runtime admits native self casts only in the actual clear phase. Existing
// travel is retained until this actor is personally at its named work entry.
static_assert(native_production_phase(true,true)==e::Phase::clear);
static_assert(native_production_phase(true,false)==e::Phase::travel);
static_assert(native_production_phase(false,true)==e::Phase::travel);
// Fresh recovery must price the entire newly observed escape, including exact
// shield expiry. H300/1000 and two80 phases leave140, despite a safe first step.
static_assert(escape_health_after(300,0,0,2,80)==140);
static_assert(!escape_budget_safe(300,1000,0,0,2,80));
static_assert(escape_budget_safe(300,1000,160,2,2,80));
static_assert(escape_health_after(300,160,1,2,80)==220);
static_assert(!escape_budget_safe(300,1000,160,0,2,80));
static_assert(!escape_budget_safe(300,1000,160,5,3,80));
struct EconomicScratch {Project project{};n::Snapshot snapshot{};Observation planning{};Decision selection{};Facts facts{};};
inline constinit EconomicScratch economic_scratch{}; // private to this actor's WASM instance
class Bot {
    n::Runtime runtime_{};
    Profile profile_{};
    Project& project_=economic_scratch.project;
    n::Snapshot& snapshot_=economic_scratch.snapshot;
    Observation& planning_=economic_scratch.planning;
    Decision& selection_=economic_scratch.selection;
    Facts& facts_=economic_scratch.facts;
    CombatActor actor_{};
    Shield shield_{};
    struct Exit {int tile=-1,steps=-1;std::array<int,4> first{{-1,-1,-1,-1}};int count=0;};
    Exit exit_{};
    int altar_=-1,renewal_=-1,handled_round_=-1,previous_round_=-1,recovery_round_=-1;
    double previous_health_=0,previous_max_health_=0,focus_=0;
    bool running_=false,natural_injury_=false,own_frame_valid_=false;
    std::uint64_t recovery_receipt_=0,shield_receipt_=0,shield_sequence_=0;

    static bool current(const n::Snapshot& s,unsigned reserve=16000){return s.complete && s.round==int(cambc::round_number()) && cambc::fuel_remaining()>=reserve;}
    static bool movement(e::ActionKind k){return k==e::ActionKind::move || k==e::ActionKind::vacate || k==e::ActionKind::withdraw;}
    static const n::GroupFacts* group(const n::Snapshot& s,int id){for(int i=0;i<s.group_count;++i)if(s.groups[i].id==id)return &s.groups[i];return nullptr;}
    static bool usable(const n::GroupFacts& g){return g.known && !g.respawning && !g.other_holder && g.gems>0 && g.combat.monster_count>0 && g.combat.monster_count<=8;}
    static int return_id(const Decision& d){for(int i=0;i<d.selected.schedule.event_count;++i)if(d.selected.schedule.events[i].kind==AccountingEventKind::return_to_work)return d.selected.schedule.events[i].waypoint;return -1;}
    static bool same_goal(n::Goal a,n::Goal b){a.target_combat_stamp=b.target_combat_stamp=0;return a==b;}
    RouteLeg leg(n::Node from,n::Node to,bool lock_budget=false)const{
        auto r=runtime_.route(from,to);if(r.distance>=0 && lock_budget)r.distance+=int(cambc::constants::altar_lock_hits);
        r.safety_checked=r.turns()>=0;return r;
    }
    int nearest_altar(const n::Snapshot& s)const{
        int result=-1,best=1000000;for(int i=0;i<s.altar_count;++i){const auto& a=s.altars[i];const auto r=runtime_.route({}, {n::NodeKind::altar_entry,a.tile_id});
            if(r.turns()<0)continue;const int cost=r.distance+((a.visible && !a.hostile_lock)?0:int(cambc::constants::altar_lock_hits));
            if(cost<best || (cost==best && a.tile_id<result)){result=a.tile_id;best=cost;}}
        return result;
    }
    static void copy_group(Group& out,const n::GroupFacts* in,const Group* previous=nullptr){
        out={};if(!in)return;out.id=in->id;out.gems=in->gems;out.gold=in->gold;out.current=in->known;
        out.available=in->known && !in->respawning;out.no_other_holder=!in->other_holder;
        out.count=in->combat.monster_count;for(std::size_t i=0;i<out.count;++i)out.monsters[i]=in->combat.monsters[i];
        if(!out.count && in->known && in->reward && previous && previous->id==in->id){
            out.count=previous->count;for(std::size_t i=0;i<out.count;++i){out.monsters[i]=previous->monsters[i];out.monsters[i].visible=true;out.monsters[i].observed_health=0;}}
    }
    bool own_frame(const n::Snapshot& s){
        own_frame_valid_=false;actor_=s.stats;actor_.direct_or_reset_damage=0;actor_.spell_known_ready=actor_.legal_first_cast=actor_.reserved_cast=actor_.heal_legal_ready=false;
        actor_.heal_recipient=-1;actor_.heal_amount=0;actor_.shield=0;actor_.shield_expiry_tick=0;
        auto& me=cambc::self();focus_=me.focus();const auto buffs=me.buffs();
        if(buffs.has_shield()){const auto b=buffs.get<cambc::buff_shield>();actor_.shield=b.value;actor_.shield_expiry_tick=b.duration;}
        shield_={};const auto ability=me.abilities()[0];
        if(ability.is_shield() && ability.can_target_self()){
            const auto meta=ability.get<cambc::ability_shield>();shield_.slot=0;shield_.duration=meta.duration;shield_.modifier=meta.modifier;
            auto& p=shield_.legal;p.target=s.self.id;p.ready=ability.is_ready();p.in_range_mask=p.visible=p.category_legal=true;
            p.useful=s.next_incoming>0;p.shield_needed=p.useful;
            // Metadata cannot promise more protection than the live effect.
            const double amount=ability.shield_amount();if(!finite(amount) || amount+.001<focus_*meta.modifier)p.ready=false;
        }
        own_frame_valid_=current(s) && finite(focus_) && focus_>=0 && stats_valid(actor_);return own_frame_valid_;
    }
    bool build_exit(const n::Snapshot& s,int named=-1,int from_tile=-1){
        exit_={};if(!current(s,30000) || !cambc::vision::champions(cambc::team::enemy).empty())return false;
        const auto visible=cambc::vision::visible_tiles();const auto camps=cambc::vision::camps();if(camps.size()>16)return false;
        cambc::bitboard danger;
        // The target remains in this mask before clearance, including asleep
        // members that our next basic would wake. No post-clear escape is used
        // as evidence that aborting a live range-three camp is safe.
        for(const auto& m:camps)if(m.is_alive() && m.strength()>0)danger.draw_chebyshev(m.position(),m.attack_range());
        auto walls=n::static_walls() | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        const auto actual=cambc::self().position();const auto origin=from_tile<0?actual:cambc::vec2{from_tile%cambc::map::width(),from_tile/cambc::map::width()};
        if(!visible.is_set(origin) || (origin!=actual && walls.is_set(origin)))return false;walls.set(origin,false);
        const auto first=walls.reachable_tiles(origin,1)&visible;
        auto candidates=(walls.reachable_tiles(origin,2)&visible&~danger).positions();
        std::sort(candidates.begin(),candidates.end(),[&](auto a,auto b){const int da=n::cheb(origin,a),db=n::cheb(origin,b);
            return da==db?(a.y==b.y?a.x<b.x:a.y<b.y):da<db;});
        for(const auto destination:candidates){const int id=destination.y*cambc::map::width()+destination.x;if(named>=0 && id!=named)continue;
            Exit candidate;candidate.tile=id;
            if(destination==origin){candidate.steps=0;exit_=candidate;return current(s);}
            candidate.steps=first.is_set(destination)?1:2;
            for(const auto step:first.positions()){
                if(step==origin || candidate.count==4)continue;
                const bool path=candidate.steps==1?step==destination:!danger.is_set(destination) && walls.reachable_tiles(step,1).is_set(destination);
                if(path)candidate.first[candidate.count++]=step.y*cambc::map::width()+step.x;
            }
            if(candidate.count){exit_=candidate;return current(s);}
        }return false;
    }
    e::Observation planning_execution(const n::Snapshot& s)const{
        e::Observation o;o.owner_turn=s.round;o.remaining_rounds=s.windows.official_remaining;o.fuel=int(cambc::fuel_remaining());o.capture_duration=s.settings.capture_rounds;
        o.alive=s.alive;o.safe=reserve(actor_) && actor_.health>=2*s.next_incoming;o.can_act=!cambc::self().has_completed_turn();
        o.can_cast=cambc::self().can_cast();o.can_move=cambc::self().can_move();o.can_attack=cambc::self().can_attack();
        o.at_spawn=s.at_spawn;o.channel=s.channel;o.channel_valid=s.channel!=e::Channel::none;o.channel_safe=s.channel_safe;
        o.spells[0]=shield_.legal;o.observation_epoch=s.epoch;
        for(int i=0;i<s.altar_count;++i)if(s.altars[i].tile_id==altar_){o.altar_id=altar_;o.adjacent_altar=s.altars[i].adjacent;
            o.legal_winning_bank=o.adjacent_altar && s.altars[i].visible && !s.altars[i].hostile_lock &&
                s.self.bag.value>=s.settings.winning_gems-s.team_score;}
        return o;
    }
    bool observation_into(const n::Snapshot& s,const n::GroupFacts& target,const n::GroupFacts& renewal,Observation& o){
        o={};o.round=s.round;o.team_score=s.team_score;o.resources=s.self;o.actor=actor_;o.actor.visible_enemy_next_phase=target.external_stress;
        o.raw_modifier_excludes_passive=true;o.naturally_injured=natural_injury_;o.must_recover=!reserve(o.actor);o.focus=focus_;o.shield=shield_;
        copy_group(o.target,&target);copy_group(o.renewal,&renewal);shield_.legal.useful=shield_.legal.shield_needed=s.next_incoming>0 || incoming(o.target)>0;
        o.shield=shield_;o.settings=s.settings;o.windows=s.windows;o.execution=planning_execution(s);
        const n::Node altar{n::NodeKind::altar_entry,altar_},spawn{n::NodeKind::spawn,0};
        bool unlocked=false;for(int i=0;i<s.altar_count;++i)if(s.altars[i].tile_id==altar_)unlocked=s.altars[i].adjacent && s.altars[i].visible && !s.altars[i].hostile_lock;
        o.current_to_group=leg({}, {n::NodeKind::group_entry,target.id});o.current_to_altar=leg({},altar,!unlocked);
        if(unlocked)o.current_to_altar.distance=0;
        o.safe_exit={exit_.tile,exit_.steps,true,true};o.altar_to_spawn=leg(altar,spawn);o.spawn_to_group=leg(spawn,{n::NodeKind::group_entry,target.id});
        o.group_to_altar=leg({n::NodeKind::group_entry,target.id},altar,true);o.spawn_to_renewal=leg(spawn,{n::NodeKind::group_entry,renewal.id});
        o.renewal_to_altar=leg({n::NodeKind::group_entry,renewal.id},altar,true);o.renewal_return=leg(altar,{n::NodeKind::group_entry,renewal.id});
        o.bank_legal=altar_>=0;o.recall_safe=exit_.tile>=0;o.recall_ticks=int(cambc::constants::recall_time);
        const double healing=cambc::constants::spawn_healing;
        if(!finite(healing) || healing<=0 || healing>1)return false;
        // Full-health worst-case wait is conservative even if the two-step
        // withdrawal incurs more damage than the initial recovery quote.
        o.spawn_rest=int(std::ceil(1/healing));o.spawn_heal_per_tick=actor_.max_health*healing;
        auto initial_bank=actor_;initial_bank.visible_enemy_next_phase=s.route_stress;int tick=0;
        const int channel=s.settings.channel_ticks(s.self.bag.value);
        o.safe_initial_bank=s.self.bag.value>0 && walk(initial_bank,o.current_to_altar,tick) && channel>0 && expose(initial_bank,channel,s.route_stress);
        return current(s,45000);
    }
    bool certify(const n::Snapshot& s,const Decision& d)const{
        if(d.job==Job::winning_bank)return d.input.execution.legal_winning_bank && d.input.execution.safe;
        if(!d.feasible || !d.selected.feasible || d.selected.rank.status!=AccountingStatus::feasible || d.selected.occupied>s.windows.official_remaining)return false;
        if(!d.selected.shielded && d.input.actor.health<.35*d.input.actor.max_health){auto escaping=d.input.actor;
            if(!expose(escaping,exit_.steps,s.route_stress))return false;}
        return true;
    }
    n::Goal goal()const{
        n::Goal g;g.phase=project_.executor.phase;g.altar_tile_id=altar_;
        const auto& d=project_.selected;
        if(d.job==Job::winning_bank){g.controller_stage=int(project_.stage)*step_cap+project_.step;g.kind=n::GoalKind::altar;g.bank_requested=true;g.forecast_turns=8;if(!project_.initialized)g.phase=e::Phase::bank;return g;}
        if(d.job==Job::recover){g.kind=n::GoalKind::recover;return g;}
        if(project_.step<0 || project_.step>=d.selected.count)return g;
        int index=project_.step;const auto& receipt=snapshot_.receipts;
        // Exact observed payout has finished production. Predict its next
        // typed goal before the policy consumes that receipt, so no bank
        // intent is emitted against an old camp goal and then silently lost.
        if(d.selected.steps[index].kind==StepKind::produce && receipt.capture_round==snapshot_.round && receipt.capture_group==d.input.target.id &&
            receipt.capture>project_.capture_base && index+1<d.selected.count){++index;g.phase=e::Phase::bank;}
        const auto& s=d.selected.steps[index];g.controller_stage=int(project_.stage)*step_cap+index;
        if(!project_.initialized)g.phase=s.kind==StepKind::produce?e::Phase::travel:s.kind==StepKind::bank?e::Phase::bank:s.kind==StepKind::recover?e::Phase::recover:e::Phase::renew;
        for(int i=index;i<d.selected.count;++i)g.forecast_turns+=d.selected.steps[i].duration;
        g.group_id=s.group;g.production=s.kind==StepKind::produce;g.bank_requested=s.kind==StepKind::bank;
        if(s.kind==StepKind::recover)g.kind=n::GoalKind::recover;
        else if(s.kind==StepKind::renew){g.kind=n::GoalKind::productive_entry;g.group_id=renewal_;}
        else if(s.kind==StepKind::bank)g.kind=n::GoalKind::altar;
        else if(g.phase==e::Phase::hold)g.kind=n::GoalKind::camp_spawn;
        else if(g.phase==e::Phase::clear)g.kind=n::GoalKind::camp_attack;
        else g.kind=n::GoalKind::camp_entry;
        const auto* live=group(snapshot_,d.input.target.id);
        const bool held_clear=snapshot_.safe_hold && live && live->reward;
        if(g.production && !held_clear){g.target_combat_mode=s.shield && !project_.shield_done?n::TargetCombatMode::ruin_one_self_shield:n::TargetCombatMode::ruin_passive_only;
            if(g.target_combat_mode==n::TargetCombatMode::ruin_one_self_shield){g.primary_cast_slot=0;g.primary_cast_target=d.input.actor.id;}}
        return g;
    }
    void facts_into(const n::Snapshot& s,const e::Observation* observed=nullptr){
        auto& f=facts_;f={};f.round=s.round;f.own_actor=s.self.id;f.own_bag=s.self.bag.value;f.own_gold=s.self.gold.value;f.own_stats=actor_;
        const auto* target=group(s,project_.selected.input.target.id);if(target)f.own_stats.visible_enemy_next_phase=target->external_stress;
        f.own_focus=focus_;f.raw_modifier_excludes_passive=true;f.group_round=f.route_safety_round=s.round;
        copy_group(f.current_target,target,&project_.selected.input.target);copy_group(f.current_renewal,group(s,renewal_),&project_.selected.input.renewal);
        f.capture_receipt=s.receipts.capture;f.bank_receipt=s.receipts.bank;
        if(observed && s.at_spawn && observed->spawn_recovery_observed && actor_.health==actor_.max_health && recovery_round_!=s.round){++recovery_receipt_;recovery_round_=s.round;}
        f.recovery_receipt=recovery_receipt_;f.recovery_round=recovery_round_;
        if(observed && project_.pending_cast && s.round==project_.cast_turn+1 && observed->feedback_sequence==project_.pending_cast &&
            observed->feedback_kind==e::ActionKind::cast && observed->feedback_success && observed->feedback_target==s.self.id &&
            actor_.shield>0 && actor_.shield_expiry_tick>0 && shield_sequence_!=project_.pending_cast){++shield_receipt_;shield_sequence_=project_.pending_cast;}
        f.shield_receipt=shield_receipt_;f.shield_cast_sequence=shield_sequence_;f.buff_round=s.round;
        f.navigation_round=s.round;f.navigation_goal=project_.step>=0 && project_.step<project_.selected.selected.count?project_.selected.selected.steps[project_.step].waypoint:-1;
        f.productive_group=renewal_;
        if(project_.step>=0 && project_.step<project_.selected.selected.count && project_.selected.selected.steps[project_.step].kind==StepKind::renew)
            f.at_named_productive=runtime_.productive_ready(renewal_);
    }
    bool initialize_shield_clear(const n::Snapshot& s){
        if(project_.initialized || project_.step<0 || project_.step>=project_.selected.selected.count)return true;
        const auto& step=project_.selected.selected.steps[project_.step];if(step.kind!=StepKind::produce || !step.shield)return true;
        const auto* target=group(s,step.group);
        if(!target || s.position_tile_id!=target->entry_tile_id || facts_.own_bag!=project_.selected.input.resources.bag.value ||
            facts_.own_gold!=project_.selected.input.resources.gold.value)return false;
        // The policy's generic initializer enters travel. This already-present
        // actor needs a clear-phase self cast. Initialize the same actual stock
        // and receipt baselines once, before execute_job can emit its one event.
        project_.initialized=true;project_.arrival=-1;project_.bank_intent=project_.pending_cast=0;project_.shield_done=false;
        project_.bag=facts_.own_bag;project_.gold=facts_.own_gold;project_.capture_base=facts_.capture_receipt;project_.bank_base=facts_.bank_receipt;
        project_.recovery_base=facts_.recovery_receipt;project_.shield_base=facts_.shield_receipt;
        e::Observation point;point.owner_turn=s.round;point.remaining_rounds=s.windows.official_remaining;point.forecast_turns=step.duration;
        e::enter(project_.executor,native_production_phase(step.shield,true),point);project_.executor.target=step.group;project_.executor.travel_next=e::Phase::clear;return true;
    }
    bool remaining_safe(const n::Snapshot& s)const{
        const auto& d=project_.selected;if(d.job==Job::winning_bank)return true;
        if(project_.step<0 || project_.step>=d.selected.count)return false;const auto& step=d.selected.steps[project_.step];
        if(step.kind==StepKind::produce){
            if(s.receipts.capture_round==s.round && s.receipts.capture_group==d.input.target.id && s.receipts.capture>project_.capture_base)return true;
            auto input=d.input;input.round=s.round;input.focus=focus_;input.target=facts_.current_target;input.renewal=facts_.current_renewal;
            auto a=facts_.own_stats;Quote proof;int tick=0,bag=facts_.own_bag;
            const auto* target=group(s,d.input.target.id);if(!target)return false;
            if(s.position_tile_id!=target->entry_tile_id && !walk(a,leg({}, {n::NodeKind::group_entry,d.input.target.id}),tick,d.input.target.id))return false;
            // Emission has no native effect yet. Keep this one event in the
            // same-packet pre-dispatch proof; a later owner packet must use
            // only its actual shield and exact successful self-cast feedback.
            return tail(input,proof,a,tick,bag,prospective_shield(step.shield,project_.shield_done,project_.pending_cast,project_.cast_turn,s.round),true) && tick<=s.windows.official_remaining;
        }
        if(step.kind==StepKind::recover)return exit_.tile>=0 && stats_valid(facts_.own_stats) &&
            escape_budget_safe(facts_.own_stats.health,facts_.own_stats.max_health,facts_.own_stats.shield,facts_.own_stats.shield_expiry_tick,exit_.steps,s.route_stress);
        auto input=d.input;input.round=s.round;input.actor=facts_.own_stats;input.renewal=facts_.current_renewal;auto a=input.actor;a.visible_enemy_next_phase=s.route_stress;
        int tick=0,bag=s.self.bag.value;Quote proof;
        if(step.kind==StepKind::bank){const auto r=leg({}, {n::NodeKind::altar_entry,altar_},true);
            if(!bank(input,proof,a,tick,bag,r) || !recover(input,a,tick,input.altar_to_spawn) || !walk(a,input.spawn_to_renewal,tick,renewal_))return false;
        }else if(!walk(a,leg({}, {n::NodeKind::group_entry,renewal_}),tick,renewal_))return false;
        return renewal_certificate(input,a,bag,s.windows.official_remaining-tick);
    }
    bool bind_combat(n::Goal& g,const n::Snapshot& s){
        if(!g.production)return true;if(!remaining_safe(s))return false;
        return runtime_.bind_target_combat(g,s,g.target_combat_mode,g.primary_cast_slot,g.primary_cast_target);
    }
    void reset(){running_=false;project_.stage=Stage::aborted;project_.pending_cast=project_.bank_intent=0;runtime_.reset_target_combat();shield_sequence_=0;}
    void stale_turn(){reset();natural_injury_=false;previous_round_=-1;runtime_.finish_turn();}
public:
    explicit Bot(Arm arm){profile_.arm=arm;}
    void unexpected_error(){reset();natural_injury_=false;previous_round_=-1;}
    void turn(){
        if(cambc::self().champion_name()!=cambc::champion_type::heart_of_ruin){runtime_.reference_turn();return;}
        if(!runtime_.begin_turn())return;const int round=int(cambc::round_number());
        if(handled_round_==round){runtime_.finish_turn();return;}handled_round_=round;
        const double health=cambc::self().health(),maximum=cambc::self().max_health();
        if(previous_round_==round-1 && maximum==previous_max_health_ && health<previous_health_)natural_injury_=true;
        if(!cambc::self().is_alive() || health==maximum)natural_injury_=false;
        previous_round_=round;previous_health_=health;previous_max_health_=maximum;
        if(round!=int(cambc::round_number())){stale_turn();return;}
        // Cheap opportunity admission leaves ordinary starts/companions on the
        // shared controller. Neither arm deliberately takes damage to enter it.
        if(!running_ && (cambc::constants::passive_gold!=0 || !natural_injury_ || health>=.35*maximum || health<.2*maximum ||
            cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus!=.5f)){
            runtime_.reference_turn();return;}
        auto& s=snapshot_;if(!runtime_.snapshot_into(s) || !current(s)){stale_turn();return;}
        if(!s.alive || !s.settings_valid || cambc::constants::passive_gold!=0){reset();runtime_.reference_turn(&s);return;}
        if(!own_frame(s)){stale_turn();return;}
        if(!running_){
            if(s.channel!=e::Channel::none || s.safe_hold){runtime_.reference_turn(&s);return;}
            if(!current(s,45000) || !runtime_.prepare_selection()){if(s.round!=int(cambc::round_number())){stale_turn();return;}runtime_.reference_turn(&s);return;}
            altar_=nearest_altar(s);if(altar_<0 || !build_exit(s)){runtime_.reference_turn(&s);return;}
            const auto started=cambc::fuel_remaining();unsigned built=started,chosen=started,certified=started;bool selected=false;
            // Freeze one public target/renewal pair before either arm chooses.
            // Prospective shielding is admitted only at the actual entry, so
            // the local escape proof refers to the place where work starts.
            for(int i=0;i<s.group_count && !selected;++i)if(usable(s.groups[i]) && s.position_tile_id==s.groups[i].entry_tile_id){
                int next=-1,best=1000000;for(int j=0;j<s.group_count;++j)if(j!=i && usable(s.groups[j])){
                    const auto route=leg({n::NodeKind::spawn,0},{n::NodeKind::group_entry,s.groups[j].id});
                    if(route.turns()>=0 && (route.distance<best || (route.distance==best && s.groups[j].id<next))){next=s.groups[j].id;best=route.distance;}}
                const auto* renewal=group(s,next);if(!renewal || !observation_into(s,s.groups[i],*renewal,planning_))continue;
                built=cambc::fuel_remaining();selection_=choose_job(planning_,profile_);chosen=cambc::fuel_remaining();selected=certify(s,selection_);certified=cambc::fuel_remaining();
            }
            const auto finished=cambc::fuel_remaining();const int end=int(cambc::round_number());
            if(!selected && chosen==started)built=chosen=certified=finished;
            runtime_.record_card_plan(started,built,chosen,certified,selected?1:0,selected,end);
            if(end!=s.round){stale_turn();return;}if(!selected){runtime_.reference_turn(&s);return;}
            runtime_.reset_target_combat();renewal_=return_id(selection_);project_=start_project(selection_,Facts{.round=s.round,.own_actor=s.self.id,.own_bag=s.self.bag.value,.own_gold=s.self.gold.value});
            running_=project_.stage==Stage::running;if(!running_){runtime_.reference_turn(&s);return;}
        }
        for(int transition=0;running_ && transition<4;++transition){
            if(!current(s) || !runtime_.prepare_selection()){if(s.round!=int(cambc::round_number())){stale_turn();return;}break;}
            facts_into(s);if(!initialize_shield_clear(s)){reset();break;}auto before=goal();
            const auto* current_target=group(s,project_.selected.input.target.id);
            const bool safe_capture=s.safe_hold && current_target && current_target->reward;
            if(before.production && !safe_capture){const auto* target=current_target;
                const int origin=project_.executor.phase==e::Phase::clear?s.position_tile_id:target?target->entry_tile_id:-1;
                if(!target || !build_exit(s,project_.selected.input.safe_exit.destination,origin)){reset();break;}}
            if(before.kind==n::GoalKind::recover && !build_exit(s)){reset();break;}
            if(!safe_capture && !bind_combat(before,s)){reset();break;}
            auto observed=runtime_.execution_observation(project_.executor,before);
            if(observed.owner_turn!=int(cambc::round_number())){stale_turn();return;}
            facts_into(s,&observed);
            if(before.production && observed.group_id==project_.selected.input.target.id && observed.target_visible && observed.target_category_legal && observed.target_in_range)
                observed.at_target=true; // actual named member is legally in work range
            if(observed.own_payout_observed && observed.payout_receipt==s.receipts.capture && s.receipts.capture_round==s.round && s.receipts.capture_gold_known)
                observed.own_gold_received=s.receipts.capture_gold;
            auto out=execute_job(observed,facts_,project_);auto after=goal();
            if(project_.stage!=Stage::running){reset();break;}if(!same_goal(before,after))continue;
            const bool priority=(observed.channel_valid && observed.channel_safe) || (before.production && observed.group_cleared && observed.on_group_spawn && observed.safe);
            if(!priority && !remaining_safe(s)){reset();break;}
            if(out.action.kind==e::ActionKind::yield || out.action.kind==e::ActionKind::refresh || movement(out.action.kind)){
                runtime_.add_moves(observed,project_.executor,before);if(s.round!=int(cambc::round_number())){stale_turn();return;}
                if(before.kind==n::GoalKind::recover && observed.in_threat_range){std::size_t keep=0;
                    for(std::size_t i=0;i<observed.move_count;++i)for(int j=0;j<exit_.count;++j)if(observed.moves[i].destination==exit_.first[j])observed.moves[keep++]=observed.moves[i];
                    observed.move_count=keep;}
                facts_into(s,&observed);out=execute_job(observed,facts_,project_);after=goal();if(project_.stage!=Stage::running){reset();break;}if(!same_goal(before,after))continue;
            }
            if(out.action.kind==e::ActionKind::cast && (out.action.slot!=0 || out.action.target!=s.self.id)){reset();break;}
            if(out.action.kind==e::ActionKind::buy || (out.action.kind==e::ActionKind::attack && before.kind!=n::GoalKind::altar && before.kind!=n::GoalKind::camp_attack)){reset();break;}
            const auto applied=runtime_.apply(out,observed,project_.executor,before);
            if(applied==n::ApplyResult::stale || (applied==n::ApplyResult::stopped && (out.action.kind==e::ActionKind::cast || out.action.kind==e::ActionKind::start_tribute || out.action.kind==e::ActionKind::start_recall)))reset();
            if(s.round!=int(cambc::round_number())){stale_turn();return;}break; // one action attempt, one packet per owner round
        }
        runtime_.finish_turn();
    }
};
inline int run(Arm arm){static Bot bot(arm);while(true){const auto result=e::turn_boundary([&]{
    if(cambc::is_draft())n::Runtime::draft(Profile::draft_request({}));else bot.turn();return e::Decision{};});
    if(result.log_error){bot.unexpected_error();std::printf("P5 UNEXPECTED_ENGINE_ERROR Collateral round-boundary; yielding\n");}cambc::end_turn();}}
} // namespace p5::collateral::native_binding
