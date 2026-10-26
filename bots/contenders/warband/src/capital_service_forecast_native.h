#pragma once
#include "capital_service_forecast.h"
#include "capital_service_bitmaps.h"
#include "capital_encounter_native.h"
#include "capital_service_native.h"
#include <algorithm>
#include <array>
#include <span>

// Fresh SDK facts for the first ONE-controlled-body Alive-group forecast.
// This grants no physical lease, invented schedule, route, enemy policy or
// utility rank. The caller must close the complete scenario and construct its
// dated programme. A missing external actor is retained as UNKNOWN coverage.
namespace wb::capital::service_forecast_native {
namespace f=service_forecast;namespace en=encounter_native;
enum class Status {FactsReady,NeedObservation,NeedFuel,Stale,Unsupported,NeedModel};
enum class Reason {None,Epoch,OwnerInput,Kit,Effects,GroupLife,MemberLife,MemberVision,
    MemberEffects,AwakeHistory,Roster,ExternalEffects,ExternalUnknown,Funding,GameBoundary};
struct Epoch {int owner{-1},round{-1};std::uint64_t input{},effect{};};
struct Capture {
    Status status{Status::NeedObservation};Reason reason{Reason::Epoch};Epoch source{};
    f::World world{};std::array<int,6> unknown_actor{};unsigned unknown_count{};
    std::array<en::Coverage,6> external_coverage{};
    bool local_facts_complete{},external_facts_complete{},schedule_supplied{};
    // These remain separate from actual facts and default FALSE. Native
    // capture never manufactures a closed conditional reply/route domain.
    bool command_authority{},scenario_closed{},utility_complete{};
    unsigned reserve_floor{18000};
};
inline bool same(Epoch a,Epoch b){return a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.effect==b.effect;}
enum class Scope {Unknown,ClosedConditionalModel};
struct VisionField {int actor{-1};f::Point pose{};f::Kit kit{};std::vector<std::uint8_t> body_vision;bool sdk_design_projection{};};
struct AltarFact {f::Point point{};bool observed{},unlocked{};bool conditional_initial{};std::uint64_t assumption{};};
struct PurchaseTransition {
    int actor{-1};f::Purchase intent{};std::array<unsigned,f::item_limit> before{};f::PurchaseEffect effect{};
};
// This is owned MODEL input, not observed coverage or command authority. Never
// point a retained Simulator callback at mutable Runtime boards/SDK scratch.
// The caller must validate a coherent observation-consistent conditional world
// before selecting ClosedConditionalModel; capture() never sets these claims.
struct ModelContext {
    Scope scope{Scope::Unknown};std::uint64_t initial_world{},static_map{},reply{},assumptions{};
    int width{},height{},first_round{-1},horizon{-1};unsigned submit_range{};
    std::vector<std::uint8_t> terrain,spawn,altar_tiles,foreign_npc_exposure;
    std::vector<VisionField> views;std::vector<AltarFact> altars;std::vector<PurchaseTransition> purchases;
    bool foreign_source_mask_complete{},all_relevant_occupants_represented{},fixed_external_hold{},no_pending_external_effects{},no_new_altar_locks{};
    void* work_context{};bool(*work_permit)(void*,unsigned){};
};
inline bool model_bound(const ModelContext& c,const f::World& w) {
    const auto valid=[&](const auto& bytes){return service_bitmaps::validate(service_bitmaps::PackedView{c.width,c.height,bytes}).valid;};
    return c.scope==Scope::ClosedConditionalModel&&c.initial_world&&w.revision==c.initial_world&&c.static_map&&c.reply&&c.assumptions
        &&valid(c.terrain)&&valid(c.spawn)&&valid(c.altar_tiles)&&valid(c.foreign_npc_exposure)
        &&w.round>=c.first_round&&w.round<=c.horizon&&w.horizon==c.horizon;
}
inline bool inside(const ModelContext& c,f::Point p){return p.x>=0&&p.y>=0&&p.x<c.width&&p.y<c.height;}
inline bool bit(const ModelContext& c,const std::vector<std::uint8_t>& mask,f::Point p) {
    if(!inside(c,p))return false;const auto n=static_cast<unsigned>(p.y*c.width+p.x);
    return n/8<mask.size()&&(mask[n/8]&(1u<<(n%8)));
}
inline f::Verdict vision_at(const ModelContext& c,const f::World& w,unsigned actor,f::Point point) {
    if(!model_bound(c,w)||actor>=w.actors)return f::Verdict::Unknown;
    for(const auto& view:c.views)if(view.actor==w.bodies[actor].id&&view.pose==w.bodies[actor].pose&&view.kit==w.bodies[actor].kit&&view.sdk_design_projection)
        return !service_bitmaps::validate(service_bitmaps::PackedView{c.width,c.height,view.body_vision}).valid?f::Verdict::Unknown:
            bit(c,view.body_vision,point)?f::Verdict::Allowed:f::Verdict::Blocked;
    return f::Verdict::Unknown;
}
inline f::Verdict model_vision(void* pointer,const f::World& w,unsigned actor,int target) {
    const auto& c=*static_cast<const ModelContext*>(pointer);
    // The simulator's visible callback names NPC basics. Champion heal targets
    // use their distinct entity namespace below, even when numeric IDs collide.
    for(unsigned n=0;n<w.npcs;++n)if(w.camps[n].id==target)return vision_at(c,w,actor,w.camps[n].pose);
    return f::Verdict::Unknown;
}
inline f::Verdict model_champion_vision(void* pointer,const f::World& w,unsigned actor,int target) {
    const auto& c=*static_cast<const ModelContext*>(pointer);
    for(unsigned n=0;n<w.actors;++n)if(w.bodies[n].id==target)return vision_at(c,w,actor,w.bodies[n].pose);
    return f::Verdict::Unknown;
}
inline f::Verdict model_legal(void* pointer,const f::World& w,unsigned actor,const f::Primitive& p) {
    const auto& c=*static_cast<const ModelContext*>(pointer);if(!model_bound(c,w)||actor>=w.actors)return f::Verdict::Unknown;
    if(p.action==f::Action::Move) {
        if(!c.all_relevant_occupants_represented||!c.foreign_source_mask_complete)return f::Verdict::Unknown;
        if(!inside(c,p.destination)||bit(c,c.terrain,p.destination)||bit(c,c.altar_tiles,p.destination))return f::Verdict::Blocked;
        if(bit(c,c.foreign_npc_exposure,p.destination))return f::Verdict::Unknown; // Unpriced exposure is not a wall.
        return f::Verdict::Allowed;
    }
    if(p.action==f::Action::Basic)return model_vision(pointer,w,actor,p.target);
    if(p.action==f::Action::Heal)return model_champion_vision(pointer,w,actor,p.target);
    if(p.action==f::Action::Recall)return f::Verdict::Unknown; // Native start/landing/cancellation family is separately qualified.
    return f::Verdict::Allowed; // Engine enforces the supported self/reset/shield/MAC/purchase conditions.
}
inline f::Verdict model_spawn(void* pointer,const f::World& w,unsigned actor,f::Point p) {
    const auto& c=*static_cast<const ModelContext*>(pointer);if(!model_bound(c,w)||actor>=w.actors)return f::Verdict::Unknown;
    return bit(c,c.spawn,p)?f::Verdict::Allowed:f::Verdict::Blocked;
}
inline f::Verdict model_altar(void* pointer,const f::World& w,unsigned actor,f::Point p) {
    const auto& c=*static_cast<const ModelContext*>(pointer);if(!model_bound(c,w)||actor>=w.actors||!c.no_new_altar_locks)return f::Verdict::Unknown;
    if(!bit(c,c.altar_tiles,p)||f::distance(w.bodies[actor].pose,p)>static_cast<int>(c.submit_range))return f::Verdict::Blocked;
    for(const auto& known:c.altars)if(known.point==p&&(known.observed||(known.conditional_initial&&known.assumption==c.assumptions)))return known.unlocked?f::Verdict::Allowed:f::Verdict::Blocked;
    return f::Verdict::Unknown;
}
inline f::Verdict model_uncontested(void* pointer,const f::World& w) {
    const auto& c=*static_cast<const ModelContext*>(pointer);if(!model_bound(c,w)||!c.all_relevant_occupants_represented)return f::Verdict::Unknown;
    for(unsigned n=0;n<w.externals;++n)if(w.others[n].alive&&w.others[n].team!=w.team)
        for(unsigned k=0;k<w.npcs;++k)if(w.others[n].pose==w.camps[k].pose)return f::Verdict::Blocked;
    return f::Verdict::Allowed;
}
inline f::Reply model_reply(void* pointer,const f::World& w,int id) {
    const auto& c=*static_cast<const ModelContext*>(pointer);f::Reply out;out.domain=c.reply;
    if(!model_bound(c,w)||!c.fixed_external_hold||!c.no_pending_external_effects)return out;
    for(unsigned n=0;n<w.externals;++n)if(w.others[n].id==id){out.complete=true;return out;}
    return out;
}
inline bool model_arrival(void*,const f::World&,unsigned,f::Point&){return false;}
inline f::PurchaseEffect model_purchase(void* pointer,const f::World& w,unsigned actor,const f::Purchase& p) {
    const auto& c=*static_cast<const ModelContext*>(pointer);if(!model_bound(c,w)||actor>=w.actors)return {};
    for(const auto& row:c.purchases)if(row.actor==w.bodies[actor].id&&row.intent.item==p.item&&row.intent.count==p.count
        &&row.intent.quote_revision==p.quote_revision&&row.intent.quoted_cost==p.quoted_cost&&row.before==w.bodies[actor].items)return row.effect;
    return {};
}
inline bool model_work(void* pointer,unsigned units) {
    auto& c=*static_cast<ModelContext*>(pointer);return c.work_permit&&c.work_permit(c.work_context,units);
}
inline f::Callbacks callbacks(ModelContext& c) {
    return {&c,&model_legal,&model_vision,&model_spawn,&model_altar,&model_uncontested,&model_reply,&model_arrival,&model_purchase,&model_work,&model_champion_vision};
}
#ifndef WB_SERVICE_FORECAST_NATIVE_HOST_FREE
inline bool current(Epoch e) {
    const auto& m=service_native::memory();
    return e.owner==static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))
        &&e.round==static_cast<int>(cambc::round_number())&&m.owner==e.owner&&m.round==e.round
        &&m.input==e.input&&m.effect_generation==e.effect&&!m.pending;
}
inline Status stop(Capture& c,Status status,Reason reason){c.status=status;c.reason=reason;return status;}
inline bool epoch(Capture& c) {return current(c.source);}
inline bool funded(Capture& c,unsigned required=18000) {
    if(cambc::fuel_remaining()<std::max(required,c.reserve_floor)){stop(c,Status::NeedFuel,Reason::Funding);return false;}
    if(!epoch(c)){stop(c,Status::Stale,Reason::Epoch);return false;}return true;
}
inline bool kit(int type,f::Kit& out) {
    if(type==static_cast<int>(cambc::champion_type::varg))out=f::Kit::Varg;
    else if(type==static_cast<int>(cambc::champion_type::willow))out=f::Kit::Willow;
    else if(type==static_cast<int>(cambc::champion_type::vendetta))out=f::Kit::Vendetta;
    else return false;return true;
}
inline bool own_body(Capture& c,bool ready_input=true) {
    en::Input input;input.owner=c.source.owner;input.round=c.source.round;
    encounter::Body raw;int type=0,priority=0;bool complete=false,priority_known=false;float npc=1;en::Coverage coverage;
    if(!en::capture_body(input,c.source.owner,raw,type,complete,priority,priority_known,npc,&coverage))
        {stop(c,Status::NeedObservation,Reason::OwnerInput);return false;}
    auto& b=c.world.bodies[0];b=f::Body{};
    if(!raw.modeled||(ready_input&&(!raw.alive||complete||!raw.m||!raw.a||!raw.c)))
        {stop(c,Status::NeedModel,Reason::OwnerInput);return false;}
    if(!kit(type,b.kit)){stop(c,Status::Unsupported,Reason::Kit);return false;}
    const auto faults=ready_input?coverage.faults:coverage.faults&~unsigned(en::Channel);
    if(faults||raw.enhanced_present||(ready_input&&!raw.effects_closed))
        {stop(c,Status::Unsupported,Reason::Effects);return false;}
    b.id=raw.id;b.pose={raw.position.x,raw.position.y};b.hp=raw.hp;b.max_hp=raw.max_hp;
    b.strength=raw.basic;b.shield=raw.shield;b.shield_present=raw.shield_present;
    b.splash_radius=b.kit==f::Kit::Varg?cambc::constants::passive_abilities::varg_splash_radius:0.0f;
    b.splash_radius_known=true;
    b.shield_ticks=static_cast<unsigned>(raw.shield_turns);b.stun_present=raw.stun_present;b.stun_ticks=static_cast<unsigned>(raw.stun_turns);
    b.actual=true;b.alive=raw.alive;b.m=raw.m;b.a=raw.a;b.c=raw.c;b.completed=complete;b.walked=false;b.cast_used=!raw.c;
    b.move_range=static_cast<unsigned>(raw.move_range);b.attack_range=static_cast<unsigned>(raw.attack_range);
    b.taken_multiplier=raw.mitigation;b.npc_taken_multiplier=npc;b.camp_multiplier=1;b.gold_multiplier=1;
    b.bag=cambc::self().gem_count();b.gold=cambc::self().gold_count();b.bag_known=b.gold_known=true;b.capital_floor=b.gold;
    cambc::detail::champion_info info{};cambc::detail::check(cambc::detail::sdk_champion(raw.id,&info));b.focus=info.focus;c.world.team=info.team;
    const auto flags=cambc::champion_state::from_bits_retain(info.state);
    if(flags.contains(cambc::champion_state::teleporting)){stop(c,Status::Unsupported,Reason::Effects);return false;}
    b.channel=flags.contains(cambc::champion_state::submitting_gems)?f::Channel::Bank:
        flags.contains(cambc::champion_state::recalling)?f::Channel::Recall:f::Channel::None;
    if(b.channel!=f::Channel::None)b.channel_ticks=static_cast<unsigned>(cambc::self().remaining_channel_time());
    const auto* d=en::design(type);if(!d||!epoch(c)){stop(c,Status::Stale,Reason::Epoch);return false;}
    b.camp_multiplier=d->base_stats.camp_damage_multiplier;
    for(unsigned slot=0;slot<2;++slot) {
        b.cooldown[slot]=static_cast<unsigned>(raw.cooldown[slot]);const auto& a=d->definition.abilities[slot];
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::attack_reset)){b.reset_slot=slot;c.world.rules.reset_cooldown=a.cooldown;}
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::shield)){b.shield_slot=slot;b.shield_modifier=a.payload.shield.modifier;
            b.shield_duration=a.payload.shield.duration;c.world.rules.shield_cooldown=a.cooldown;}
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::heal)){b.heal_slot=slot;b.heal_modifier=a.payload.heal.modifier;
            b.heal_range=static_cast<unsigned>(a.payload.heal.range);c.world.rules.heal_cooldown=a.cooldown;}
    }
    std::array<std::uint32_t,static_cast<unsigned>(cambc::item_type::count)> items{};
    static_assert(items.size()==f::item_limit);
    cambc::detail::check(cambc::detail::sdk_champion_items(raw.id,items.data()));
    for(unsigned n=0;n<items.size();++n)b.items[n]=items[n];
    bool durability=false,jungle=false;
    for(const auto item:{cambc::item_type::durability_emblem_of_strength,cambc::item_type::durability_emblem_of_focus,cambc::item_type::durability_emblem_of_health})
        durability=durability||items[static_cast<unsigned>(item)]!=0;
    for(const auto item:{cambc::item_type::jungle_emblem_of_strength,cambc::item_type::jungle_emblem_of_focus,cambc::item_type::jungle_emblem_of_health})
        jungle=jungle||items[static_cast<unsigned>(item)]!=0;
    // The first class does not equate two sequential native float32 scaling
    // operations with one pre-multiplied factor. Their operation order needs
    // its own source/native contract before this combination is represented.
    if(durability&&jungle){stop(c,Status::Unsupported,Reason::Effects);return false;}
    // gold_count() exposes floor(cash), not its hidden fractional carry. With
    // Treasure that remainder can change a later purchase boundary. The first
    // exact-cash family excludes it rather than inventing zero fractional gold.
    for(const auto item:{cambc::item_type::treasure_emblem_of_strength,cambc::item_type::treasure_emblem_of_focus,cambc::item_type::treasure_emblem_of_health})
        if(items[static_cast<unsigned>(item)]){stop(c,Status::Unsupported,Reason::Effects);return false;}
    for(const auto item:{cambc::item_type::monster_emblem_of_strength,cambc::item_type::monster_emblem_of_focus,cambc::item_type::monster_emblem_of_health})
        if(items[static_cast<unsigned>(item)]){
            if(b.camp_multiplier!=1.0f){stop(c,Status::Unsupported,Reason::Effects);return false;}
            b.camp_multiplier=f::fp_add(1.0f,item.passive_magnitude());break;
        }
    c.world.rules.recall_ticks=cambc::constants::recall_time;
    b.effective_recall_ticks=c.world.rules.recall_ticks;
    for(const auto item:{cambc::item_type::home_emblem_of_strength,cambc::item_type::home_emblem_of_focus,cambc::item_type::home_emblem_of_health})
        if(items[static_cast<unsigned>(item)]){b.effective_recall_ticks=static_cast<unsigned>(std::ceil(c.world.rules.recall_ticks*(1.f-item.passive_magnitude())));break;}
    c.world.team=info.team;return epoch(c);
}
// Actual source generation is a caller-owned, reconciled lifetime token. It
// cannot be reconstructed from an unobserved respawn or a retained HP vector.
inline Status capture(Capture& c,Epoch source,const cambc::map::camp_group& group,
                      unsigned source_generation,std::span<const int> roster,int horizon) {
    c.status=Status::NeedObservation;c.reason=Reason::Epoch;c.source=source;
    c.local_facts_complete=c.external_facts_complete=c.schedule_supplied=false;
    c.command_authority=c.scenario_closed=c.utility_complete=false;c.unknown_count=0;
    c.world.rules={};c.world.group={};
    c.world.actors=1;c.world.npcs=c.world.externals=c.world.slots=0;c.world.slot=c.world.npc_cursor=0;
    c.world.round=source.round;c.world.horizon=horizon;c.world.phase=f::Phase::ActorAction;
    c.world.revision=service_native::mix(service_native::mix(1,source.input),source.effect);
    if(!current(source))return stop(c,Status::Stale,Reason::Epoch);
    if(!source_generation||horizon<source.round||horizon-source.round>512||roster.empty()||roster.size()>6)
        return stop(c,Status::NeedModel,Reason::Roster);
    if(static_cast<unsigned>(horizon)>=cambc::constants::max_rounds||cambc::constants::passive_gold!=0)
        return stop(c,Status::NeedModel,Reason::GameBoundary);
    for(unsigned n=0;n<roster.size();++n)for(unsigned k=0;k<n;++k)if(roster[n]==roster[k])return stop(c,Status::Unsupported,Reason::Roster);
    if(std::find(roster.begin(),roster.end(),source.owner)==roster.end())return stop(c,Status::Unsupported,Reason::Roster);
    if(!funded(c,35000)||!own_body(c))return c.status;
    if(!funded(c))return c.status;
    const auto visible=cambc::vision::visible_tiles();bool sees=false;
    for(const auto& member:group.camps())if(visible.is_set(member.spawn_position())){sees=true;break;}
    if(!sees)return stop(c,Status::NeedObservation,Reason::GroupLife);
    if(!std::holds_alternative<cambc::map::camp_group_alive>(group.state()))return stop(c,Status::NeedModel,Reason::GroupLife);
    if(group.camps().empty()||group.camps().size()>f::npc_limit)return stop(c,Status::Unsupported,Reason::Roster);
    auto& g=c.world.group;g.id=static_cast<int>(static_cast<cambc::handle_camp_group_t>(group));g.generation=source_generation;
    g.life=f::Life::Alive;g.roster_complete=true;g.capture_history_known=true;g.capture_armed=false;g.capture_remaining=0;
    g.gold=group.gold_value();g.gems=group.gem_value();bool any=false;
    for(const auto& member:group.camps()) {
        if(!funded(c))return c.status;auto& n=c.world.camps[c.world.npcs++];
        n.id=static_cast<int>(static_cast<cambc::handle_camp_t>(member));const auto p=member.spawn_position();n.pose={p.x,p.y};
        if(!visible.is_set(p))return stop(c,Status::NeedObservation,Reason::MemberVision);
        std::uint32_t life=0;cambc::detail::check(cambc::detail::sdk_camp_state(n.id,&life));
        const auto base=member.base_stats();n.max_hp=base.base_health;n.hp=0;n.strength=base.base_strength;n.range=base.base_attack_range;
        n.shield=0;n.shield_present=false;n.shield_ticks=0;n.empty_batches=0;n.empty_batches_known=true;n.unsupported_effects=false;n.damaged_since_batch=false;
        if(life==0){n.awake=false;n.actual=true;continue;}
        if(life!=1&&life!=2)return stop(c,Status::NeedObservation,Reason::MemberLife);
        n.hp=member.health();n.strength=member.strength();n.range=member.attack_range();n.awake=life==2;n.empty_batches_known=!n.awake;
        // Unknown empty-batch history is retained, not filled with zero. The
        // simulator rejects the first actual read unless a modeled accepted
        // hit/known target has already reset this counter before that phase.
        const auto buffs=member.buffs();if(!buffs.is_empty())return stop(c,Status::Unsupported,Reason::MemberEffects);
        n.actual=true;any=any||n.hp>0;
    }
    if(!any)return stop(c,Status::NeedObservation,Reason::GroupLife);
    // SDK group's physical enumeration is not NPC phase order (Packs group0
    // enumerates27,0,31,2). The simulator's dated source order is numeric ID.
    std::sort(c.world.camps.begin(),c.world.camps.begin()+c.world.npcs,[](const auto& a,const auto& b){return a.id<b.id;});
    auto& rules=c.world.rules;rules.queried=true;rules.capture_ticks=cambc::constants::capture_time;
    rules.sleep_batches=cambc::constants::camp_sleep_time;rules.respawn_delay=group.max_respawn_timer();
    if(cambc::constants::submit_gem_base_duration!=0||cambc::constants::submit_gem_minimum_duration!=1)
        return stop(c,Status::Unsupported,Reason::Effects); // This model's current queried channel form is max(1,ceil(bag*multiplier)).
    rules.bank_multiplier=cambc::constants::submit_gem_duration_multiplier;
    rules.spawn_healing=cambc::constants::spawn_healing;rules.recall_damage_interrupt_known=false;rules.recall_damage_interrupt=false;
    rules.max_rounds=cambc::constants::max_rounds;rules.winning_gems=cambc::constants::winning_gems;rules.passive_gold=cambc::constants::passive_gold;
    c.world.team_score=cambc::global_submitted_gems(cambc::team::ally);
    if(c.world.team_score+c.world.bodies[0].bag+g.gems>=cambc::constants::winning_gems)
        return stop(c,Status::NeedModel,Reason::GameBoundary); // Race completion needs the separate goal/bank outcome, not a fictitious post-game renewal.
    c.local_facts_complete=true;
    for(const auto id:roster)if(id!=source.owner) {
        if(!funded(c))return c.status;en::Input input;input.owner=source.owner;input.round=source.round;
        encounter::Body b;int type=0,priority=0;bool done=false,priority_known=false;float mitigation=1;en::Coverage coverage;
        if(!en::capture_body(input,id,b,type,done,priority,priority_known,mitigation,&coverage))return stop(c,Status::NeedObservation,Reason::ExternalEffects);
        if(!b.modeled){c.unknown_actor[c.unknown_count++]=id;continue;}
        auto& x=c.world.others[c.world.externals];x=f::External{};c.external_coverage[c.world.externals++]=coverage;
        x.id=id;x.team=b.team;x.pose={b.position.x,b.position.y};x.hp=b.hp;x.shield=b.shield;x.alive=b.alive;x.actual=true;
        x.taken_multiplier=mitigation;x.npc_effects_closed=false; // Caller must qualify the actual reply/retaliation/channel model.
    }
    if(!epoch(c))return stop(c,Status::Stale,Reason::Epoch);
    c.external_facts_complete=c.unknown_count==0;
    return stop(c,c.external_facts_complete?Status::FactsReady:Status::NeedModel,
        c.external_facts_complete?Reason::None:Reason::ExternalUnknown);
}
#endif
}
