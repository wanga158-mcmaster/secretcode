#pragma once
#include "capital_service_forecast_native.h"
#include <array>
#include <cmath>
#include <span>

// Fresh named-body facts only. Colocation is not a commitment, modeled peer
// inputs are not command authority, and visible carried gems reveal no wallet.
namespace wb::capital::service_joint_native {
namespace f=service_forecast;namespace n=service_forecast_native;namespace en=encounter_native;namespace e=encounter;
enum class Entity {Champion,Npc};
struct Actor {int id{-1};Entity entity{Entity::Champion};};
enum class Status {Ready,NeedObservation,NeedFuel,Stale,Unsupported};
enum class Reason {None,Source,Base,Roster,Namespace,Duplicate,Team,Kit,Life,Effects,Numbers,Equipment,Rules,Funding,UnknownPeer};
struct Request {n::Epoch source{};std::array<Actor,3> actors{};};
struct PublicRoster {n::Epoch source{};std::array<int,3> allies{{-1,-1,-1}};bool actual_complete{};};
struct Design {
    bool actual{},has_reset{},has_shield{},has_heal{};f::Kit kit{};
    unsigned reset_slot{},reset_cooldown{},shield_slot{},shield_cooldown{},shield_duration{},heal_slot{},heal_cooldown{},heal_range{},recall_base{};
    float shield_modifier{},heal_modifier{},camp_multiplier{1},varg_splash{};
};
struct Modifiers {
    bool actual{},durability{},jungle{},treasure{},monster{},home{};float monster_magnitude{},home_magnitude{};
};
struct PeerFacts {
    n::Epoch source{};Actor actor{};e::Body body{};en::Coverage coverage{};Design design{};Modifiers modifiers{};
    std::array<unsigned,f::item_limit> items{};float focus{},npc_taken_multiplier{1};unsigned bag{};
    bool info_actual{},equipment_actual{},bag_actual{},priority_known{};int priority{};
};
struct JointCapture {
    Status status{Status::NeedObservation};Reason reason{Reason::Base};n::Epoch source{};f::World world{};
    unsigned owner_index{};std::array<int,3> actors{{-1,-1,-1}};std::array<bool,3> completed{},priority_known{},requires_start{},prestart_provenance_known{};
    std::array<int,3> priority{};std::array<int,6> unknown_actor{};unsigned unknown_count{};std::array<en::Coverage,6> external_coverage{};
    bool world_ready{},external_facts_complete{},requires_schedule_rebind{true};
    bool command_authority{},scenario_closed{},utility_complete{},commitment{};
};
inline Status fail(JointCapture& out,Status status,Reason reason){out.status=status;out.reason=reason;out.world_ready=false;return status;}
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline bool point(f::Point p){return p.x>=0&&p.y>=0&&p.x<=10000&&p.y<=10000;}
inline bool expected_kit(f::Kit kit){return unsigned(kit)<=unsigned(f::Kit::Vendetta);}
inline bool same_request(const Request& request,int id){for(auto actor:request.actors)if(actor.id==id)return true;return false;}
inline Status convert_peer(const PeerFacts& facts,n::Epoch source,int team,f::Rules& rules,f::Body& out,Reason& reason) {
    auto reject=[&](Status status,Reason why){reason=why;return status;};const auto& raw=facts.body;const auto& d=facts.design;const auto& m=facts.modifiers;
    if(!n::same(facts.source,source))return reject(Status::Stale,Reason::Source);
    if(facts.actor.entity!=Entity::Champion)return reject(Status::Unsupported,Reason::Namespace);
    if(facts.actor.id<0||raw.id!=facts.actor.id)return reject(Status::Unsupported,Reason::Roster);
    if(!raw.modeled)return reject(Status::NeedObservation,Reason::UnknownPeer);
    if(!facts.info_actual||!facts.equipment_actual||!facts.bag_actual||!d.actual||!m.actual)return reject(Status::NeedObservation,Reason::Equipment);
    if(raw.team!=team)return reject(Status::Unsupported,Reason::Team);
    if(!raw.alive)return reject(Status::NeedObservation,Reason::Life);
    if(!expected_kit(d.kit)||raw.ranged!=(d.kit==f::Kit::Willow))return reject(Status::Unsupported,Reason::Kit);
    if(facts.coverage.faults||!raw.effects_closed||raw.enhanced_present||raw.channel_present||raw.non_timed_restrictions)return reject(Status::Unsupported,Reason::Effects);
    if(!point({raw.position.x,raw.position.y})||!finite(raw.hp)||!finite(raw.max_hp)||raw.hp<=0||raw.max_hp<raw.hp||!finite(raw.basic)||!finite(facts.focus)||
       !finite(raw.shield)||!finite(raw.mitigation)||raw.mitigation>1||!finite(facts.npc_taken_multiplier)||facts.npc_taken_multiplier>raw.mitigation||
       raw.move_range<1||raw.move_range>10000||raw.attack_range<1||raw.attack_range>10000||raw.shield_turns<0||raw.shield_turns>512||raw.stun_turns<0||raw.stun_turns>512||
       raw.cooldown[0]<0||raw.cooldown[0]>512||raw.cooldown[1]<0||raw.cooldown[1]>512)return reject(Status::Unsupported,Reason::Numbers);
    if(m.treasure||(m.durability&&m.jungle))return reject(Status::Unsupported,Reason::Equipment);
    if(!d.has_reset||d.reset_slot>1||!d.reset_cooldown||d.reset_cooldown>512||!rules.reset_cooldown||d.reset_cooldown!=rules.reset_cooldown||
       !d.recall_base||d.recall_base>512||d.recall_base!=rules.recall_ticks)return reject(Status::Unsupported,Reason::Rules);
    if(!finite(d.camp_multiplier)||!finite(d.varg_splash)||!finite(d.shield_modifier)||!finite(d.heal_modifier)||
       d.has_shield!=(d.kit==f::Kit::Varg)||d.has_heal!=(d.kit==f::Kit::Willow))return reject(Status::Unsupported,Reason::Kit);
    if(d.has_shield&&(d.shield_slot>1||!d.shield_cooldown||d.shield_cooldown>512||!d.shield_duration||d.shield_duration>512||
       (rules.shield_cooldown&&rules.shield_cooldown!=d.shield_cooldown)))return reject(Status::Unsupported,Reason::Rules);
    if(d.has_heal&&(d.heal_slot>1||!d.heal_cooldown||d.heal_cooldown>512||!d.heal_range||d.heal_range>10000||
       (rules.heal_cooldown&&rules.heal_cooldown!=d.heal_cooldown)))return reject(Status::Unsupported,Reason::Rules);
    float camp=d.camp_multiplier;if(m.monster){if(camp!=1.f||!finite(m.monster_magnitude))return reject(Status::Unsupported,Reason::Equipment);camp=f::fp_add(1.f,m.monster_magnitude);}
    unsigned recall=d.recall_base;if(m.home){if(!finite(m.home_magnitude)||m.home_magnitude>=1)return reject(Status::Unsupported,Reason::Equipment);
        const auto duration=std::ceil(d.recall_base*(1.f-m.home_magnitude));if(duration<1||duration>512)return reject(Status::Unsupported,Reason::Equipment);recall=static_cast<unsigned>(duration);}
    f::Body next{};next.id=raw.id;next.kit=d.kit;next.pose={raw.position.x,raw.position.y};next.hp=raw.hp;next.max_hp=raw.max_hp;next.strength=raw.basic;next.focus=facts.focus;
    next.shield=raw.shield;next.shield_present=raw.shield_present;next.shield_ticks=unsigned(raw.shield_turns);next.stun_present=raw.stun_present;next.stun_ticks=unsigned(raw.stun_turns);
    next.alive=raw.alive;next.m=raw.m;next.a=raw.a;next.c=raw.c;next.completed=raw.completed_turn;next.actual=true;
    // For a nonowner, walked/cast provenance can be unobservable after its
    // input. These placeholders have no pre-START authority. The caller must
    // use the dated START recorded by requires_start before any peer word.
    next.walked=raw.provenance_known?raw.walked:false;next.cast_used=raw.provenance_known?raw.cast_used:false;
    next.move_range=unsigned(raw.move_range);next.attack_range=unsigned(raw.attack_range);next.cooldown={unsigned(raw.cooldown[0]),unsigned(raw.cooldown[1])};
    next.items=facts.items;next.bag=facts.bag;next.bag_known=true;next.gold=next.capital_floor=0;next.gold_known=false;
    next.camp_multiplier=camp;next.taken_multiplier=raw.mitigation;next.npc_taken_multiplier=facts.npc_taken_multiplier;next.gold_multiplier=1;
    next.reset_slot=d.reset_slot;next.shield_slot=d.shield_slot;next.shield_modifier=d.shield_modifier;next.shield_duration=d.shield_duration;
    next.heal_slot=d.heal_slot;next.heal_modifier=d.heal_modifier;next.heal_range=d.heal_range;next.effective_recall_ticks=recall;
    next.splash_radius=d.kit==f::Kit::Varg?d.varg_splash:0;next.splash_radius_known=true;
    if(d.has_shield)rules.shield_cooldown=d.shield_cooldown;if(d.has_heal)rules.heal_cooldown=d.heal_cooldown;
    out=next;reason=Reason::None;return Status::Ready;
}
inline Status convert(const n::Capture& base,const Request& request,const PublicRoster& roster,std::span<const PeerFacts> peers,JointCapture& out) {
    out.source=request.source;out.world_ready=out.external_facts_complete=false;out.requires_schedule_rebind=true;
    out.command_authority=out.scenario_closed=out.utility_complete=out.commitment=false;out.unknown_count=0;out.priority_known={};out.requires_start={};out.prestart_provenance_known={};
    if(!n::same(request.source,base.source)||!n::same(roster.source,request.source))return fail(out,Status::Stale,Reason::Source);
    if(base.status==n::Status::Stale)return fail(out,Status::Stale,Reason::Source);
    if(base.status==n::Status::NeedFuel)return fail(out,Status::NeedFuel,Reason::Funding);
    if(base.status!=n::Status::FactsReady&&!(base.status==n::Status::NeedModel&&base.reason==n::Reason::ExternalUnknown))return fail(out,Status::NeedObservation,Reason::Base);
    if(!base.local_facts_complete||base.world.actors!=1||base.world.bodies[0].id!=request.source.owner||!base.world.bodies[0].actual||!base.world.bodies[0].alive||
       base.world.bodies[0].completed||base.world.bodies[0].stun_present||base.world.bodies[0].walked||base.world.bodies[0].cast_used||!base.world.bodies[0].bag_known||!base.world.bodies[0].gold_known||
       !base.world.bodies[0].m||!base.world.bodies[0].a||!base.world.bodies[0].c||base.world.bodies[0].channel!=f::Channel::None||
       base.world.round!=request.source.round||base.world.phase!=f::Phase::ActorAction||!base.world.rules.queried||base.world.group.life!=f::Life::Alive||
       !base.world.group.roster_complete||!base.world.group.generation||!base.world.npcs||base.world.npcs>f::npc_limit||base.world.externals>f::external_limit||base.unknown_count>6||base.world.externals+base.unknown_count+1>6)
        return fail(out,Status::NeedObservation,Reason::Base);
    if(!roster.actual_complete||peers.size()!=2)return fail(out,Status::NeedObservation,Reason::Roster);
    unsigned owner=3;for(unsigned a=0;a<3;++a){const auto actor=request.actors[a];if(actor.entity!=Entity::Champion)return fail(out,Status::Unsupported,Reason::Namespace);
        if(actor.id<0)return fail(out,Status::Unsupported,Reason::Roster);for(unsigned b=0;b<a;++b)if(request.actors[b].id==actor.id)return fail(out,Status::Unsupported,Reason::Duplicate);
        if(actor.id==request.source.owner)owner=a;unsigned count=0;for(auto id:roster.allies)count+=id==actor.id;if(count!=1)return fail(out,Status::Unsupported,Reason::Roster);}
    if(owner==3)return fail(out,Status::Unsupported,Reason::Roster);
    for(unsigned a=0;a<peers.size();++a){if(peers[a].actor.id==request.source.owner||!same_request(request,peers[a].actor.id))return fail(out,Status::Unsupported,Reason::Roster);
        for(unsigned b=0;b<a;++b)if(peers[a].actor.id==peers[b].actor.id)return fail(out,Status::Unsupported,Reason::Duplicate);}
    f::World next=base.world;std::array<bool,3> kits{};const auto self=base.world.bodies[0];
    if(!expected_kit(self.kit))return fail(out,Status::Unsupported,Reason::Kit);kits[unsigned(self.kit)]=true;
    for(unsigned a=0;a<3;++a){out.actors[a]=request.actors[a].id;out.requires_start[a]=a!=owner;
        if(a==owner){next.bodies[a]=self;out.completed[a]=self.completed;out.prestart_provenance_known[a]=true;continue;}
        const PeerFacts* facts=nullptr;for(const auto& peer:peers)if(peer.actor.id==request.actors[a].id)facts=&peer;if(!facts)return fail(out,Status::NeedObservation,Reason::Roster);
        Reason why;const auto status=convert_peer(*facts,request.source,next.team,next.rules,next.bodies[a],why);if(status!=Status::Ready)return fail(out,status,why);
        if(kits[unsigned(next.bodies[a].kit)])return fail(out,Status::Unsupported,Reason::Kit);kits[unsigned(next.bodies[a].kit)]=true;
        out.completed[a]=facts->body.completed_turn;out.priority[a]=facts->priority;out.priority_known[a]=facts->priority_known;
        out.prestart_provenance_known[a]=facts->body.provenance_known&&facts->body.c;
    }
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<a;++b)if(next.bodies[a].pose==next.bodies[b].pose)return fail(out,Status::Unsupported,Reason::Numbers);
    unsigned externals=0;for(unsigned a=0;a<base.world.externals;++a){const auto& external=base.world.others[a];
        if(same_request(request,external.id)){if(external.team!=next.team)return fail(out,Status::Unsupported,Reason::Team);continue;}
        if(external.team==next.team)return fail(out,Status::Unsupported,Reason::Roster);
        next.others[externals]=external;out.external_coverage[externals++]=base.external_coverage[a];}
    for(unsigned a=0;a<base.unknown_count;++a)if(!same_request(request,base.unknown_actor[a]))out.unknown_actor[out.unknown_count++]=base.unknown_actor[a];
    next.actors=3;next.externals=externals;out.world=next;out.owner_index=owner;out.external_facts_complete=out.unknown_count==0;
    out.status=Status::Ready;out.reason=Reason::None;out.world_ready=true;return out.status;
}
#ifndef WB_SERVICE_JOINT_NATIVE_HOST_FREE
struct Options {unsigned capture_reserve{35000},query_reserve{18000};};
inline bool guard(n::Epoch source,JointCapture& out,unsigned reserve){if(!n::current(source)){fail(out,Status::Stale,Reason::Source);return false;}
    if(cambc::fuel_remaining()<reserve){fail(out,Status::NeedFuel,Reason::Funding);return false;}return true;}
inline bool sample_peer(const Request& request,int id,PeerFacts& out,JointCapture& result,Options options){
    if(!guard(request.source,result,options.capture_reserve))return false;out=PeerFacts{};out.source=request.source;out.actor.id=id;
    en::Input input;input.owner=request.source.owner;input.round=request.source.round;input.input=request.source.input;input.effect=request.source.effect;
    int type=0;bool complete=false;float npc=1;
    if(!en::capture_body(input,id,out.body,type,complete,out.priority,out.priority_known,npc,&out.coverage)){
        fail(result,n::current(request.source)?Status::NeedObservation:Status::Stale,n::current(request.source)?Reason::UnknownPeer:Reason::Source);return false;}
    if(!guard(request.source,result,options.query_reserve))return false;if(!out.body.modeled){fail(result,Status::NeedObservation,Reason::UnknownPeer);return false;}
    cambc::detail::champion_info info{};cambc::detail::check(cambc::detail::sdk_champion(id,&info));
    std::array<std::uint32_t,f::item_limit> items{};cambc::detail::check(cambc::detail::sdk_champion_items(id,items.data()));
    std::uint32_t bag=0;cambc::detail::check(cambc::detail::sdk_champion_gems(id,&bag));
    if(!guard(request.source,result,options.query_reserve))return false;
    if(info.type!=type||info.team!=out.body.team||info.x!=out.body.position.x||info.y!=out.body.position.y||info.health!=out.body.hp||info.max_health!=out.body.max_hp||info.strength!=out.body.basic){fail(result,Status::Stale,Reason::Source);return false;}
    if(!n::kit(type,out.design.kit)){fail(result,Status::Unsupported,Reason::Kit);return false;}
    const auto* design=en::design(type);if(!design){fail(result,Status::Unsupported,Reason::Kit);return false;}if(!guard(request.source,result,options.query_reserve))return false;
    auto& d=out.design;d.actual=true;d.camp_multiplier=design->base_stats.camp_damage_multiplier;d.recall_base=cambc::constants::recall_time;
    d.varg_splash=d.kit==f::Kit::Varg?cambc::constants::passive_abilities::varg_splash_radius:0.f;
    for(unsigned slot=0;slot<2;++slot){const auto& a=design->definition.abilities[slot];
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::attack_reset)){d.has_reset=true;d.reset_slot=slot;d.reset_cooldown=a.cooldown;}
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::shield)){d.has_shield=true;d.shield_slot=slot;d.shield_cooldown=a.cooldown;d.shield_modifier=a.payload.shield.modifier;d.shield_duration=a.payload.shield.duration;}
        if(a.kind==static_cast<unsigned>(cambc::ability_kind::heal)){d.has_heal=true;d.heal_slot=slot;d.heal_cooldown=a.cooldown;d.heal_modifier=a.payload.heal.modifier;d.heal_range=unsigned(a.payload.heal.range);}}
    out.items=items;out.focus=info.focus;out.npc_taken_multiplier=npc;out.bag=bag;out.info_actual=out.equipment_actual=out.bag_actual=true;
    auto& m=out.modifiers;m.actual=true;
    for(const auto item:{cambc::item_type::durability_emblem_of_strength,cambc::item_type::durability_emblem_of_focus,cambc::item_type::durability_emblem_of_health})m.durability=m.durability||items[static_cast<unsigned>(item)]!=0;
    for(const auto item:{cambc::item_type::jungle_emblem_of_strength,cambc::item_type::jungle_emblem_of_focus,cambc::item_type::jungle_emblem_of_health})m.jungle=m.jungle||items[static_cast<unsigned>(item)]!=0;
    for(const auto item:{cambc::item_type::treasure_emblem_of_strength,cambc::item_type::treasure_emblem_of_focus,cambc::item_type::treasure_emblem_of_health})m.treasure=m.treasure||items[static_cast<unsigned>(item)]!=0;
    for(const auto item:{cambc::item_type::monster_emblem_of_strength,cambc::item_type::monster_emblem_of_focus,cambc::item_type::monster_emblem_of_health})if(items[static_cast<unsigned>(item)]){m.monster=true;m.monster_magnitude=item.passive_magnitude();break;}
    for(const auto item:{cambc::item_type::home_emblem_of_strength,cambc::item_type::home_emblem_of_focus,cambc::item_type::home_emblem_of_health})if(items[static_cast<unsigned>(item)]){m.home=true;m.home_magnitude=item.passive_magnitude();break;}
    return guard(request.source,result,options.query_reserve);
}
inline Status capture_joint(const n::Capture& base,const Request& request,JointCapture& out,Options options={}){
    out.source=request.source;out.world_ready=out.command_authority=out.scenario_closed=out.utility_complete=out.commitment=false;out.requires_schedule_rebind=true;
    if(!guard(request.source,out,options.query_reserve))return out.status;
    if(!n::same(base.source,request.source))return fail(out,Status::Stale,Reason::Source);
    std::uint32_t count=0;cambc::detail::check(cambc::detail::sdk_num_allies(&count));
    if(!guard(request.source,out,options.query_reserve))return out.status;if(count!=3)return fail(out,Status::Unsupported,Reason::Roster);
    std::array<std::uint32_t,3> handles{};cambc::detail::check(cambc::detail::sdk_allies(handles.data(),handles.size(),&count));
    if(!guard(request.source,out,options.query_reserve))return out.status;if(count!=3)return fail(out,Status::Stale,Reason::Roster);
    PublicRoster roster;roster.source=request.source;roster.actual_complete=true;for(unsigned a=0;a<3;++a)roster.allies[a]=int(handles[a]);
    for(auto actor:request.actors)if(actor.entity!=Entity::Champion||actor.id<0||!std::count(roster.allies.begin(),roster.allies.end(),actor.id))return fail(out,Status::Unsupported,actor.entity==Entity::Champion?Reason::Roster:Reason::Namespace);
    std::array<PeerFacts,2> peers;unsigned at=0;for(auto actor:request.actors)if(actor.id!=request.source.owner){if(at==peers.size())return fail(out,Status::Unsupported,Reason::Roster);
        if(!sample_peer(request,actor.id,peers[at++],out,options))return out.status;}
    if(at!=2)return fail(out,Status::Unsupported,Reason::Roster);if(!guard(request.source,out,options.query_reserve))return out.status;
    const auto status=convert(base,request,roster,peers,out);
    // The owned copy/validation tail is paid work too. A suspended C++ tail
    // cannot publish the earlier observation as a fresh joint preparation.
    if(status==Status::Ready&&!guard(request.source,out,options.query_reserve))return out.status;
    return status;
}
#endif
} // namespace wb::capital::service_joint_native
