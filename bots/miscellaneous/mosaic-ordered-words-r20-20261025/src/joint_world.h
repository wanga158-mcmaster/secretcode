#pragma once
// A bounded supplied-frontier CAMP + champion transition model. It does not
// query the SDK, assert unseen absence, or issue actual execution authority.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::joint_world {
inline constexpr std::size_t actor_capacity=10,camp_capacity=16,reply_capacity=16,
    frontier_capacity=8,event_capacity=64,word_capacity=6,attempt_capacity=6,
    successful_capacity=4,candidate_capacity=8,blocked_capacity=24,region_capacity=4;
inline constexpr unsigned no_word=255;
inline constexpr double unknown_damage=281474976710656.0;
using Revision=std::uint64_t;
struct Point {int x{},y{};friend bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}};
inline int distance(Point a,Point b){
    const auto dx=std::llabs(std::int64_t(a.x)-std::int64_t(b.x));
    const auto dy=std::llabs(std::int64_t(a.y)-std::int64_t(b.y));
    return int(std::min<std::int64_t>(std::max(dx,dy),std::numeric_limits<int>::max()));
}
enum class Knowledge {owner,fresh_champion,retained_hidden};
enum class CampKnowledge {fresh_awake,fresh_asleep,unknown};
enum class AbilityKind {damage,control,shield,reset,dash};
enum class ActionKind {move,basic,cast,observe,recall,bank,wait};
enum class Target {actor,camp,ground,self};
inline constexpr unsigned target_self=1,target_ally=2,target_enemy=4,target_camp=8,target_ground=16;
enum class ChannelKind {none,recall,bank};
enum class Purpose {work,flee,counteraction,bank,capture,recall};
enum class EventKind {camp_batch,champion_input};
enum class Assurance {none,approximate_frontier,declared_frontier,conditional_prefix_bound};
enum class Reason {ok,capacity,invalid_amount,invalid_timeline,invalid_word,illegal_own_action,
    missing_feedback,refusal_bound,operation_budget,incomplete_horizon,missing_coverage};
struct Ability {
    bool enabled{},ground{},exclusive_with_walk{};
    AbilityKind kind{AbilityKind::damage};
    int range{},radius{},cooldown{},cooldown_on_cast{};
    unsigned target_mask{target_self|target_ally|target_enemy|target_camp|target_ground};
    double damage{},shield{};
    int stun_ticks{},shield_ticks{},enhance_next_stun{};
};
struct Shield {double amount{};int ticks{-1};};
struct Channel {ChannelKind kind{ChannelKind::none};int ticks{};Point destination{};};
struct Envelope {
    // Remaining unenumerated equipped prefix, applied in this exact hit order
    // at each supplied hostile opportunity. Reach includes that word's motion.
    bool valid{},full_missing_health{true};int reach{},travel{};
    unsigned hits{};std::array<double,successful_capacity> damage{};
    int stun_ticks{};
    double defensive_shield{};
};
struct Actor {
    Knowledge knowledge{Knowledge::fresh_champion};unsigned id{},team{};
    Point position{},belief_center{};int belief_radius{};
    bool alive{true},ranged{},stun_immune{},can_move{},can_attack{},can_cast{},walked{},walk_locked{};
    double health{},max_health{},basic_damage{},damage_taken{1},camp_damage_taken{1},
        camp_damage_multiplier{1},missing_health_bonus{},retaliation{};
    int attack_range{1},move_speed{1},basic_splash_radius{},enhanced_stun_ticks{},stun_ticks{-1};
    bool enhanced_attack_ready{},refresh_cooldowns_on_kill{};
    Shield shield{};Channel channel{};
    std::array<Ability,2> abilities{};
    unsigned gems{},banked{};
    bool frontier_complete{true};Envelope remaining_reply{};
};
struct Camp {
    Point position{};unsigned group{},id{};CampKnowledge knowledge{CampKnowledge::unknown};
    bool alive{true},health_known{};double health{},damage{};int range{},stun_ticks{-1};Shield shield{};
};
struct AggregateCamp {
    // ALWAYS-potential incoming bound for omitted NPCs; never killed, slept or
    // suppressed by a proposed effect on one represented source.
    bool present{},covers_omitted{};Point center{};int reach{};double damage{};
};
struct Action {
    ActionKind kind{ActionKind::wait};Target target{Target::self};
    unsigned index{},slot{};Point point{};
    bool target_observed{},path_legal{},landing_observed_free{},optional_if_unavailable{},only_after_refusal{},follow_target{};
    int duration{};
    Revision observation_intent{};unsigned cursor_before{},cursor_after{},cursor_total{};
    bool relevant_observation{};
};
struct Word {unsigned count{};std::array<Action,attempt_capacity> actions;};
struct Event {EventKind kind{EventKind::camp_batch};unsigned actor{},word{no_word};};
struct Frontier {
    unsigned event_count{};std::array<Event,event_capacity> events;
    // Branch overrides select retained-hidden starting poses in the declared
    // reachable belief. Missing positions never delete a retained identity.
    std::uint16_t position_overrides{};std::array<Point,actor_capacity> positions{};
    std::uint16_t envelope_actor_mask{};
    bool complete{true};
    // At most one hidden-obstruction false in an OWN word. No time passes;
    // earlier effects remain, false effects do not, and observe must follow.
    int refused_word{-1},refused_action{-1};
};
struct Capture {
    bool enabled{},group_clear{},group_complete{};unsigned region_count{};std::array<Point,region_capacity> region{};
    int ticks{-1},duration{};unsigned gems{};std::uint16_t camp_mask{};
};
struct World {
    Revision frame{},observation{},effect{};unsigned owner{},actor_count{},camp_count{},reply_count{},frontier_count{};
    std::array<Actor,actor_capacity> actors;
    std::array<Camp,camp_capacity> camps;
    std::array<Word,reply_capacity> replies;
    std::array<Frontier,frontier_capacity> frontiers;
    // CAMP ties use current-round champion order, not entity ID. Row0 is the
    // next batch; bitN supplies that row's complete actor targeting ranks.
    std::array<std::array<unsigned,actor_capacity>,8> camp_target_ranks;
    std::uint16_t camp_orders_known{};
    unsigned blocked_count{};std::array<Point,blocked_capacity> blocked;
    Capture capture{};AggregateCamp aggregate{};
    Point own_spawn{};
    bool terrain_complete{},sources_complete{},effects_complete{},schedule_complete{},no_exogenous_wake{},no_respawn{},spawn_valid{};
    unsigned horizon_intervals{8},operation_quota{30000};
    Revision observation_intent{};unsigned observation_cursor{};
};
struct Candidate {
    Revision frame{},observation{},effect{};Purpose purpose{Purpose::flee};
    unsigned word_count{};std::array<Word,word_capacity> words;
    unsigned useful_service{};
};
struct Quote {
    const Candidate* subject{};Revision frame{},observation{},effect{};
    bool valid{},bounded{},alive{},complete{},next_usable_seen{},requires_actual_confirmation{},approximate{};
    // worst_health follows sequential float32 HP state. worst_loss sums hit
    // amounts for diagnostics/utility; comparing that sum with initial HP is
    // NOT a lethal test because each actual HP subtraction rounds separately.
    double worst_health{},worst_loss{};unsigned banked{},captured{},team_captured{},successful{},refusals{},observations{};
    bool owner_share_unknown{};
    unsigned camp_batches{},hostile_inputs{},hostile_usable{},owner_inputs{},owner_usable{},lost_owner_inputs{};
    unsigned operations{},fatal_event{std::numeric_limits<unsigned>::max()},fatal_actor{no_word};
    Assurance assurance{Assurance::none};Reason reason{Reason::invalid_word};
};
struct Selection {std::size_t index{std::numeric_limits<std::size_t>::max()};Quote quote{};};
struct Receipt {
    Revision frame{},observation{},effect{};unsigned successful_prefix{};
    bool actual_body{},actual_targets{},actual_occupancy{},refusal_closed{};
    bool allow_approximate{},allow_risk{};
};
// Scratch arrays are deliberately not eagerly value-initialized by default.
// Use World{}/Candidate{} for a fully initialized automatic object, or write
// every used entry before evaluation. Static SDK storage and counts stay small.
namespace detail {
inline bool amount(double value){return std::isfinite(value)&&value>=0&&value<=unknown_damage;}
inline bool point(Point p){return p.x>=-10000&&p.x<=10000&&p.y>=-10000&&p.y<=10000;}
struct State {
    const World* world{};const Candidate* candidate{};const Frontier* frontier{};
    std::array<Actor,actor_capacity> actors;std::array<Camp,camp_capacity> camps;
    std::array<bool,actor_capacity> revealed{};
    std::array<bool,actor_capacity> position_uncertain{};
    Capture capture{};Quote quote{};unsigned* operations{};
    unsigned camps_since_owner{},intervals{},cursor{},successes_this_input{};
    unsigned next_own_word{1};double loss{},minimum_health{};
    bool refresh_pending{},refused_input{},work{},recalled{},captured{};Point refusal_point{};
    unsigned event{};
    bool step(unsigned count=1){
        if(*operations>world->operation_quota||count>world->operation_quota-*operations){
            quote.valid=false;quote.bounded=false;quote.reason=Reason::operation_budget;return false;
        }
        *operations+=count;return true;
    }
    bool fail(Reason reason){quote.valid=false;quote.reason=reason;return false;}
};
inline double scaled(const Actor& source,double base){
    const double missing=source.max_health>0?std::clamp(1-source.health/source.max_health,0.0,1.0):1;
    return base*(1+source.missing_health_bonus*missing);
}
inline void cancel(Channel& channel){channel={};}
inline void stun(Actor& actor,int ticks){
    if(ticks>0&&!actor.stun_immune){actor.stun_ticks=std::max(actor.stun_ticks,ticks);cancel(actor.channel);}
}
inline void refresh_kill(Actor& killer){
    if(killer.refresh_cooldowns_on_kill)for(auto& ability:killer.abilities)ability.cooldown=0;
}
inline float engine_float(State& state,double value){
    const float rounded=static_cast<float>(value);
    if(double(rounded)!=value)state.quote.approximate=true;
    return rounded;
}
struct Depletion {float health{},shield{},penetrating{};};
inline Depletion deplete(float health,float shield,float hit){
    // Pinned rules-notes67-69: each hit subtracts from float32 state. A
    // double running sum/product can invent a lethal fifth129.8f hit at649HP.
    const float absorbed=std::min(shield,hit);
    const float remaining_shield=std::max(0.0f,shield-absorbed);
    const float penetrating=hit-absorbed;
    const float remaining_health=std::max(0.0f,health-penetrating);
    return {remaining_health,remaining_shield,penetrating};
}
inline bool damage_actor(State& state,unsigned target,double raw,unsigned attacker,bool retaliation=true){
    if(!state.step())return false;
    auto& victim=state.actors[target];if(!victim.alive||raw<=0)return true;
    // Queried float operands plus unit modifiers qualify the narrow storage
    // transition. Nontrivial damage/passive products remain an explicit model
    // approximation until their actual engine evaluation order is qualified.
    if(victim.damage_taken!=1||(attacker<state.world->actor_count&&state.actors[attacker].missing_health_bonus!=0))
        state.quote.approximate=true;
    const float raw_hit=engine_float(state,raw),multiplier=engine_float(state,victim.damage_taken);
    const float hit=raw_hit*multiplier;
    if(!std::isfinite(hit))return state.fail(Reason::invalid_amount);
    // Conservative recall interruption uses any positive incoming damage,
    // including a shielded hit; no uncertain shield/channel immunity is used.
    if(hit>0&&victim.channel.kind==ChannelKind::recall)cancel(victim.channel);
    const auto after=deplete(engine_float(state,victim.health),engine_float(state,victim.shield.amount),hit);
    victim.shield.amount=double(after.shield);victim.health=double(after.health);
    if(target==state.world->owner)state.loss+=double(after.penetrating);
    if(target==state.world->owner)state.minimum_health=std::min(state.minimum_health,victim.health);
    const double reply=retaliation&&attacker<state.world->actor_count?victim.retaliation:0;
    if(victim.health<=0){
        victim.alive=false;cancel(victim.channel);
        if(attacker<state.world->actor_count){state.actors[attacker].gems+=victim.gems;refresh_kill(state.actors[attacker]);}
        victim.gems=0;
        if(target==state.world->owner&&state.quote.fatal_event==std::numeric_limits<unsigned>::max()){
            state.quote.fatal_event=state.event;state.quote.fatal_actor=attacker;
        }
    }
    if(reply>0&&state.actors[attacker].alive)
        return damage_actor(state,attacker,reply,target,false);
    return true;
}
inline bool damage_camp(State& state,unsigned target,double raw,unsigned attacker){
    if(!state.step())return false;
    auto& camp=state.camps[target];if(!camp.alive||raw<=0)return true;
    const auto group=camp.group;
    for(unsigned i=0;i<state.world->camp_count;++i)
        if(state.camps[i].alive&&state.camps[i].group==group)state.camps[i].knowledge=CampKnowledge::fresh_awake;
    if(state.actors[attacker].missing_health_bonus!=0||state.actors[attacker].camp_damage_multiplier!=1)
        state.quote.approximate=true;
    const float hit=engine_float(state,raw);
    if(!std::isfinite(hit))return state.fail(Reason::invalid_amount);
    const auto after=deplete(engine_float(state,camp.health),engine_float(state,camp.shield.amount),hit);
    camp.shield.amount=double(after.shield);
    if(camp.health_known){camp.health=double(after.health);
        if(camp.health==0){camp.alive=false;refresh_kill(state.actors[attacker]);}}
    else state.quote.approximate=true;
    return true;
}
inline bool occupied(const State& state,Point p,unsigned mover,bool& hidden){
    hidden=false;
    for(unsigned i=0;i<state.world->actor_count;++i)if(i!=mover&&state.actors[i].alive&&state.actors[i].position==p){
        hidden=state.actors[i].knowledge==Knowledge::retained_hidden&&!state.revealed[i];return true;
    }
    for(unsigned i=0;i<state.world->camp_count;++i)if(state.camps[i].alive&&state.camps[i].position==p)return true;
    return false;
}
inline bool terrain_blocked(const State& state,Point p){
    for(unsigned i=0;i<state.world->blocked_count;++i)if(state.world->blocked[i]==p)return true;
    return false;
}
inline bool freed(const State& state,Point p){
    for(unsigned i=0;i<state.world->camp_count;++i)
        if(state.world->camps[i].alive&&!state.camps[i].alive&&state.camps[i].position==p)return true;
    for(unsigned i=0;i<state.world->actor_count;++i)
        if(state.world->actors[i].alive&&!state.actors[i].alive&&state.actors[i].position==p)return true;
    return false;
}
inline bool covered_hidden(const Actor& actor,Point center,int radius){
    return actor.knowledge!=Knowledge::retained_hidden||actor.frontier_complete||
        distance(actor.belief_center,center)+actor.belief_radius<=radius;
}
inline bool area(State& state,unsigned source,Point center,int radius,double base,int control){
    auto& attacker=state.actors[source];bool touched=false;
    for(unsigned i=0;i<state.world->actor_count;++i){
        if(!state.step())return false;
        auto& target=state.actors[i];
        if(i==source||!target.alive||target.team==attacker.team||distance(target.position,center)>radius)continue;
        if(!covered_hidden(target,center,radius)||(state.position_uncertain[i]&&
           distance(target.belief_center,center)+target.belief_radius>radius)){state.quote.approximate=true;continue;}
        touched=touched||base>0||(control>0&&!target.stun_immune);
        if(!damage_actor(state,i,scaled(attacker,base),source))return false;
        if(target.alive)stun(target,control);
    }
    for(unsigned i=0;i<state.world->camp_count;++i){
        if(!state.step())return false;auto& target=state.camps[i];
        if(!target.alive||distance(target.position,center)>radius)continue;
        touched=touched||base>0||control>0;
        if(!damage_camp(state,i,scaled(attacker,base)*attacker.camp_damage_multiplier,source))return false;
        if(control>0)target.stun_ticks=std::max(target.stun_ticks,control);
    }
    if(source==state.world->owner&&touched)state.work=true;
    return true;
}
inline bool unavailable(State& state,bool own,const Action& action){
    return !own&&action.optional_if_unavailable?true:state.fail(Reason::illegal_own_action);
}
// Returns1 success,0 unavailable,-1 invalid. A refused movement is a completed
// ATTEMPT with no successful primitive and no timer/camp advancement.
inline int act(State& state,unsigned source,const Action& requested,unsigned own_word,unsigned action_index){
    if(!state.step())return -1;
    Action action=requested;
    const bool own=source==state.world->owner;auto& actor=state.actors[source];
    const auto reject=[&](){return unavailable(state,own,action)?0:-1;};
    if(action.follow_target){
        // Enemy reply templates may pursue the current branch target instead
        // of replaying yesterday's endpoint. This nominates geometry only;
        // path_legal/terrain/frontier premises still determine its assurance.
        if(own||action.index>=state.world->actor_count||!state.actors[action.index].alive)return reject();
        const auto target=state.actors[action.index].position;
        const bool dash=action.kind==ActionKind::cast&&action.slot<2&&actor.abilities[action.slot].kind==AbilityKind::dash;
        if(action.kind==ActionKind::move||dash){
            const int budget=dash?actor.abilities[action.slot].range:actor.move_speed;
            const int d=distance(actor.position,target);
            if(!dash&&d<=actor.attack_range)return 0;
            const int step=std::min(budget,std::max(0,d-1));
            action.point={actor.position.x+std::clamp(target.x-actor.position.x,-step,step),
                          actor.position.y+std::clamp(target.y-actor.position.y,-step,step)};
            if(dash&&action.point==actor.position){
                bool found=false;
                for(int y=-1;y<=1&&!found;++y)for(int x=-1;x<=1&&!found;++x){
                    const Point p{target.x+x,target.y+y};bool hidden=false;
                    if((x||y)&&!(p==actor.position)&&distance(actor.position,p)<=budget&&
                       !terrain_blocked(state,p)&&!occupied(state,p,source,hidden)){action.point=p;found=true;}
                }
                if(!found)return reject();
            }
        }else if(action.kind==ActionKind::cast&&action.target==Target::ground&&action.slot<2){
            const auto& ability=actor.abilities[action.slot];
            action.point={actor.position.x+std::clamp(target.x-actor.position.x,-ability.range,ability.range),
                          actor.position.y+std::clamp(target.y-actor.position.y,-ability.range,ability.range)};
            if(distance(action.point,target)>ability.radius)return reject();
        }
    }
    if(action.only_after_refusal&&!state.refused_input)return 0;
    if(action.kind==ActionKind::wait)return 0;
    if(state.refresh_pending&&action.kind!=ActionKind::observe&&own){state.fail(Reason::missing_feedback);return -1;}
    if(action.kind==ActionKind::observe){
        if(!own||!action.relevant_observation||action.observation_intent!=state.world->observation_intent||
           action.cursor_before!=state.cursor||action.cursor_after!=state.cursor+1||
           action.cursor_after>action.cursor_total||action.cursor_total>32){state.fail(Reason::missing_feedback);return -1;}
        ++state.cursor;++state.quote.observations;state.refresh_pending=false;
        for(unsigned i=0;i<state.world->actor_count;++i)
            if(state.actors[i].position==state.refusal_point)state.revealed[i]=true;
        return 0;
    }
    if(!actor.alive||actor.stun_ticks>0||actor.channel.kind!=ChannelKind::none)return reject();
    if(action.kind==ActionKind::move||(action.kind==ActionKind::cast&&action.slot<2&&
       actor.abilities[action.slot].kind==AbilityKind::dash)){
        const bool dash=action.kind==ActionKind::cast;
        const Ability* ability=dash?&actor.abilities[action.slot]:nullptr;
        if(!action.path_legal||!point(action.point)||terrain_blocked(state,action.point)||
           distance(actor.position,action.point)>(dash?ability->range:actor.move_speed)||
           distance(actor.position,action.point)==0||(!dash&&(!actor.can_move||actor.walk_locked))||
           (dash&&(!actor.can_cast||!ability->enabled||ability->cooldown>0||
             (ability->exclusive_with_walk&&actor.walked))))return reject();
        bool hidden=false;const bool collision=occupied(state,action.point,source,hidden);
        if(collision&&!hidden)return reject();
        const bool forced=own&&state.frontier->refused_word==int(own_word)&&state.frontier->refused_action==int(action_index);
        if(own&&(forced||(collision&&hidden))){
            if(state.quote.refusals){state.fail(Reason::refusal_bound);return -1;}
            if(!action.landing_observed_free&&!freed(state,action.point)){state.fail(Reason::illegal_own_action);return -1;}
            ++state.quote.refusals;state.refused_input=true;state.refresh_pending=true;state.refusal_point=action.point;return 0;
        }
        if(collision)return reject();
        if(own&&!action.landing_observed_free&&!freed(state,action.point))return reject();
        actor.position=action.point;
        if(dash){actor.can_cast=false;actor.abilities[action.slot].cooldown=ability->cooldown_on_cast;
            if(ability->exclusive_with_walk){actor.can_move=false;actor.walk_locked=true;}
            if(!area(state,source,actor.position,ability->radius,ability->damage,ability->stun_ticks))return -1;
        }else{actor.can_move=false;actor.walked=true;if(actor.ranged)actor.can_attack=false;}
        return 1;
    }
    if(action.kind==ActionKind::basic){
        if(!actor.can_attack)return reject();Point target{};
        if(action.target==Target::actor){
            if(action.index>=state.world->actor_count||!state.actors[action.index].alive||
               state.actors[action.index].team==actor.team)return reject();
            if(own&&(state.position_uncertain[action.index]||
               (state.actors[action.index].knowledge==Knowledge::retained_hidden&&!state.revealed[action.index])))return reject();
            target=state.actors[action.index].position;
        }else if(action.target==Target::camp){
            if(action.index>=state.world->camp_count||!state.camps[action.index].alive)return reject();
            target=state.camps[action.index].position;
        }else return reject();
        if(distance(actor.position,target)>actor.attack_range||
           (own&&!action.target_observed&&(action.target!=Target::actor||!state.revealed[action.index])))return reject();
        actor.can_attack=false;if(actor.ranged)actor.can_move=false;
        const int control=actor.enhanced_attack_ready?actor.enhanced_stun_ticks:0;actor.enhanced_attack_ready=false;
        if(actor.basic_splash_radius>0){if(!area(state,source,target,actor.basic_splash_radius,actor.basic_damage,control))return -1;}
        else if(action.target==Target::actor){
            if(!damage_actor(state,action.index,scaled(actor,actor.basic_damage),source))return -1;
            if(state.actors[action.index].alive)stun(state.actors[action.index],control);
        }else{if(!damage_camp(state,action.index,scaled(actor,actor.basic_damage)*actor.camp_damage_multiplier,source))return -1;
            if(control>0)state.camps[action.index].stun_ticks=std::max(state.camps[action.index].stun_ticks,control);}
        if(own&&(actor.basic_damage>0||control>0))state.work=true;return 1;
    }
    if(action.kind==ActionKind::cast){
        if(action.slot>=2)return reject();auto& ability=actor.abilities[action.slot];
        if(!actor.can_cast||!ability.enabled||ability.cooldown>0||
           (ability.exclusive_with_walk&&actor.walked))return reject();
        if(ability.kind==AbilityKind::reset&&actor.can_attack)return reject();
        Point center=actor.position;unsigned mask=target_self;
        if(action.target==Target::actor){
            if(action.index>=state.world->actor_count||!state.actors[action.index].alive)return reject();
            if(own&&(state.position_uncertain[action.index]||
               (state.actors[action.index].knowledge==Knowledge::retained_hidden&&!state.revealed[action.index])))return reject();
            center=state.actors[action.index].position;mask=state.actors[action.index].team==actor.team?target_ally:target_enemy;
        }else if(action.target==Target::camp){if(action.index>=state.world->camp_count||!state.camps[action.index].alive)return reject();
            center=state.camps[action.index].position;mask=target_camp;
        }else if(action.target==Target::ground){center=action.point;mask=target_ground;}
        if(!point(center)||!(ability.target_mask&mask)||distance(actor.position,center)>ability.range||
           (own&&(mask==target_enemy||mask==target_camp)&&!action.target_observed))return reject();
        actor.can_cast=false;ability.cooldown=ability.cooldown_on_cast;
        if(ability.exclusive_with_walk){actor.can_move=false;actor.walk_locked=true;}
        if(ability.kind==AbilityKind::reset){actor.can_attack=true;
            if(ability.enhance_next_stun>0){actor.enhanced_attack_ready=true;actor.enhanced_stun_ticks=ability.enhance_next_stun;}}
        else if(ability.kind==AbilityKind::shield){
            const unsigned recipient=action.target==Target::actor?action.index:source;
            state.actors[recipient].shield={ability.shield,ability.shield_ticks};if(own&&ability.shield>0)state.work=true;
        }else if(!area(state,source,center,ability.ground?ability.radius:0,ability.damage,ability.stun_ticks))return -1;
        return 1;
    }
    if(action.kind==ActionKind::recall||action.kind==ActionKind::bank){
        if(action.duration<=0||action.duration>8)return reject();
        if(action.kind==ActionKind::recall){if(!state.world->spawn_valid)return reject();
            actor.channel={ChannelKind::recall,action.duration,state.world->own_spawn};}
        else{if(actor.gems==0||!point(action.point)||distance(actor.position,action.point)>1)return reject();
            actor.channel={ChannelKind::bank,action.duration,action.point};}
        return 1;
    }
    return reject();
}
inline bool word(State& state,unsigned source,const Word& word,unsigned own_index){
    if(word.count>attempt_capacity)return state.fail(Reason::capacity);
    for(unsigned i=0;i<word.count;++i){
        const int result=act(state,source,word.actions[i],own_index,i);
        if(result<0)return false;
        if(result>0){
            if(++state.successes_this_input>successful_capacity)return state.fail(Reason::invalid_word);
            if(source==state.world->owner)++state.quote.successful;
        }
        if(!state.actors[source].alive)break;
    }
    if(source==state.world->owner&&state.refresh_pending)return state.fail(Reason::missing_feedback);
    return true;
}
inline void end(Actor& actor){
    if(actor.stun_ticks>0)--actor.stun_ticks;
    if(actor.shield.ticks>0)--actor.shield.ticks;
    for(auto& ability:actor.abilities)if(ability.cooldown>0)--ability.cooldown;
    if(actor.channel.kind!=ChannelKind::none&&actor.channel.ticks>0)--actor.channel.ticks;
}
inline bool start(State& state,unsigned index){
    auto& actor=state.actors[index];
    if(actor.stun_ticks==0)actor.stun_ticks=-1;
    if(actor.shield.ticks==0)actor.shield={};
    if(actor.alive&&actor.channel.kind!=ChannelKind::none&&actor.channel.ticks==0){
        if(actor.channel.kind==ChannelKind::bank){actor.banked+=actor.gems;actor.gems=0;}
        else{actor.position=actor.channel.destination;if(index==state.world->owner)state.recalled=true;}
        cancel(actor.channel);
    }
    actor.walked=actor.walk_locked=false;
    const bool usable=actor.alive&&actor.stun_ticks<1&&actor.channel.kind==ChannelKind::none;
    actor.can_move=actor.can_attack=actor.can_cast=usable;return usable;
}
inline bool envelope(State& state,unsigned index){
    auto& enemy=state.actors[index];const auto& bound=enemy.remaining_reply;
    state.quote.approximate=true;
    if(!bound.valid||bound.travel>0)state.position_uncertain[index]=true;
    if(bound.defensive_shield>enemy.shield.amount)enemy.shield={bound.defensive_shield,8};
    const bool contact=!bound.valid||distance(state.actors[state.world->owner].position,enemy.belief_center)<=
        enemy.belief_radius+bound.reach;
    if(contact){
        if(!bound.valid){if(!damage_actor(state,state.world->owner,unknown_damage,index))return false;}
        else for(unsigned hit=0;hit<bound.hits&&enemy.alive;++hit){
            // A remaining-family bound cannot credit a hidden body's health
            // or omitted NPC targeting as suppressing missing-health output.
            const bool full=bound.full_missing_health||enemy.knowledge==Knowledge::retained_hidden||!state.world->effects_complete;
            const double upper=full?bound.damage[hit]*(1+enemy.missing_health_bonus):scaled(enemy,bound.damage[hit]);
            if(!damage_actor(state,state.world->owner,upper,index))return false;
            if(state.actors[state.world->owner].alive)stun(state.actors[state.world->owner],bound.stun_ticks);
        }
    }
    enemy.belief_radius=std::min(10000,enemy.belief_radius+std::max(0,bound.travel));return true;
}
inline bool on_region(const Capture& capture,Point position){
    for(unsigned i=0;i<capture.region_count;++i)if(capture.region[i]==position)return true;
    return false;
}
inline bool camp_batch(State& state){
    if(!state.step())return false;
    const unsigned batch_index=state.quote.camp_batches;
    if(batch_index>=state.world->horizon_intervals)return state.fail(Reason::invalid_timeline);
    ++state.quote.camp_batches;++state.camps_since_owner;
    // Respawn uncertainty precedes capture; it cannot award a group while a
    // possibly returning member has merely been left dead in the old snapshot.
    if(!state.world->no_respawn)for(unsigned c=0;c<state.world->camp_count;++c)
        if(!state.camps[c].alive){state.camps[c].alive=true;state.camps[c].health_known=false;
            state.camps[c].knowledge=CampKnowledge::unknown;state.quote.approximate=true;}
    auto& capture=state.capture;auto& owner=state.actors[state.world->owner];
    if(capture.enabled&&!state.captured){
        bool clear=capture.group_clear||capture.group_complete,contested=false;
        for(unsigned i=0;i<state.world->camp_count;++i)
            if((capture.camp_mask&(1u<<i))&&state.camps[i].alive)clear=false;
        for(unsigned i=0;i<state.world->actor_count;++i){
            const auto& actor=state.actors[i];if(!actor.alive||actor.team==owner.team)continue;
            if(on_region(capture,actor.position))contested=true;
            if(state.position_uncertain[i]||(actor.knowledge==Knowledge::retained_hidden&&!actor.frontier_complete))
                for(unsigned region=0;region<capture.region_count;++region)
                    if(distance(actor.belief_center,capture.region[region])<=actor.belief_radius){
                        contested=true;state.quote.approximate=true;
                    }
        }
        if(!clear||!owner.alive||!on_region(capture,owner.position)||contested)capture.ticks=-1;
        else if(capture.ticks<0)capture.ticks=capture.duration;
        else{
            if(capture.ticks>0)--capture.ticks;
            if(capture.ticks==0){
                unsigned present=0,lowest=state.world->owner;bool unknown=false;
                for(unsigned i=0;i<state.world->actor_count;++i){const auto& ally=state.actors[i];
                    if(!ally.alive||ally.team!=owner.team)continue;
                    if(ally.knowledge==Knowledge::retained_hidden||state.position_uncertain[i]){
                        for(unsigned region=0;region<capture.region_count;++region)
                            if(distance(ally.belief_center,capture.region[region])<=ally.belief_radius+
                               int(state.world->horizon_intervals)*std::max(1,ally.move_speed))unknown=true;
                    }else if(on_region(capture,ally.position)){
                        ++present;if(ally.id<state.actors[lowest].id)lowest=i;
                    }
                }
                state.quote.team_captured=capture.gems;state.captured=true;
                if(unknown){
                    // Team capture is distinct from an unobserved personal
                    // allocation. Do not mint the whole reward in this bag.
                    state.quote.owner_share_unknown=state.quote.approximate=true;
                }else if(present){
                    const unsigned each=capture.gems/present,remainder=capture.gems%present;
                    for(unsigned i=0;i<state.world->actor_count;++i){auto& ally=state.actors[i];
                        if(ally.alive&&ally.team==owner.team&&on_region(capture,ally.position)){
                            const auto share=each+(i==lowest?remainder:0);ally.gems+=share;
                            if(i==state.world->owner)state.quote.captured=share;
                        }
                    }
                }
            }
        }
    }
    for(unsigned c=0;c<state.world->camp_count;++c){
        if(!state.step())return false;auto& camp=state.camps[c];
        if(!camp.alive){
            if(state.world->no_respawn)continue;
            camp.alive=true;camp.health_known=false;camp.knowledge=CampKnowledge::unknown;state.quote.approximate=true;
        }
        if(camp.stun_ticks==0)camp.stun_ticks=-1;
        if(camp.shield.ticks==0)camp.shield={};
        const bool asleep=camp.knowledge==CampKnowledge::fresh_asleep&&state.world->no_exogenous_wake;
        state.quote.approximate|=camp.knowledge==CampKnowledge::unknown;
        if(!asleep&&camp.stun_ticks<1){
            unsigned target=no_word;int nearest=std::numeric_limits<int>::max();
            for(unsigned a=0;a<state.world->actor_count;++a){
                if(!state.step())return false;const auto& actor=state.actors[a];
                if(!actor.alive)continue;
                if(state.position_uncertain[a]||(actor.knowledge==Knowledge::retained_hidden&&!actor.frontier_complete)){
                    state.quote.approximate=true;continue;}
                const int d=distance(actor.position,camp.position);
                bool earlier=target==no_word;
                if(d==nearest&&target!=no_word){
                    if(state.world->camp_orders_known&(1u<<batch_index))
                        earlier=state.world->camp_target_ranks[batch_index][a]<state.world->camp_target_ranks[batch_index][target];
                    else{state.quote.approximate=true;earlier=a==state.world->owner||
                        (target!=state.world->owner&&actor.id<state.actors[target].id);}
                }
                if(d<=camp.range&&(d<nearest||(d==nearest&&earlier))){
                    nearest=d;target=a;
                }
            }
            if(target!=no_word){
                const double retaliation=camp.damage>0?state.actors[target].retaliation:0;
                if(state.actors[target].camp_damage_taken!=1)state.quote.approximate=true;
                if(!damage_actor(state,target,camp.damage*state.actors[target].camp_damage_taken,no_word))return false;
                if(retaliation>0&&!damage_camp(state,c,retaliation*state.actors[target].camp_damage_multiplier,target))return false;
            }
        }
        if(camp.stun_ticks>0)--camp.stun_ticks;if(camp.shield.ticks>0)--camp.shield.ticks;
    }
    const auto& aggregate=state.world->aggregate;
    if(aggregate.present&&owner.alive&&distance(owner.position,aggregate.center)<=aggregate.reach){
        state.quote.approximate=true;
        if(!damage_actor(state,state.world->owner,aggregate.covers_omitted?aggregate.damage:unknown_damage,no_word))return false;
    }
    return true;
}
inline bool input(State& state,const Event& event){
    if(!state.step())return false;
    if(event.actor>=state.world->actor_count)return state.fail(Reason::invalid_timeline);
    const bool own=event.actor==state.world->owner;auto& actor=state.actors[event.actor];
    if(own){
        if(state.camps_since_owner!=1||++state.intervals>state.world->horizon_intervals)
            return state.fail(Reason::invalid_timeline);
        state.camps_since_owner=0;++state.quote.owner_inputs;
    }else if(actor.team!=state.actors[state.world->owner].team)++state.quote.hostile_inputs;
    const bool usable=start(state,event.actor);
    state.successes_this_input=0;state.refused_input=false;
    if(own){
        if(usable){++state.quote.owner_usable;state.quote.next_usable_seen=true;}
        else ++state.quote.lost_owner_inputs;
    }else if(actor.team!=state.actors[state.world->owner].team&&usable)++state.quote.hostile_usable;
    if(usable){
        if(own){
            const unsigned index=event.word==no_word?state.next_own_word:event.word;
            if(index<state.candidate->word_count){
                if(!word(state,event.actor,state.candidate->words[index],index))return false;
                state.next_own_word=std::max(state.next_own_word,index+1);
            }else if(event.word!=no_word)return state.fail(Reason::invalid_word);
        }else if(actor.team!=state.actors[state.world->owner].team&&(!actor.frontier_complete||
                  (state.frontier->envelope_actor_mask&(1u<<event.actor)))){
            if(!envelope(state,event.actor))return false;
        }else if(event.word!=no_word){
            if(event.word>=state.world->reply_count)return state.fail(Reason::invalid_timeline);
            if(!word(state,event.actor,state.world->replies[event.word],no_word))return false;
        }
    }
    end(actor);
    if(own&&actor.alive&&state.world->spawn_valid&&actor.position==state.world->own_spawn)
        actor.health=actor.max_health;
    return true;
}
inline bool amounts(const World& world){
    for(unsigned i=0;i<world.actor_count;++i){const auto& actor=world.actors[i];
        if(!amount(actor.health)||!amount(actor.max_health)||!amount(actor.basic_damage)||
           (actor.alive&&actor.knowledge!=Knowledge::retained_hidden&&(actor.health<=0||actor.max_health<=0))||
           !amount(actor.shield.amount)||!amount(actor.damage_taken)||!amount(actor.camp_damage_taken)||
           !amount(actor.camp_damage_multiplier)||!amount(actor.missing_health_bonus)||!amount(actor.retaliation)||
           !point(actor.position)||!point(actor.belief_center)||actor.belief_radius<0||actor.belief_radius>10000||
           actor.move_speed<0||actor.move_speed>10000||actor.attack_range<0||actor.attack_range>10000||
           actor.remaining_reply.hits>successful_capacity)return false;
        for(const auto& ability:actor.abilities)if(!amount(ability.damage)||!amount(ability.shield)||ability.range<0||
            ability.range>10000||ability.radius<0||ability.radius>10000||ability.cooldown<0||
            ability.stun_ticks<0||ability.shield_ticks<0)return false;
        for(unsigned h=0;h<actor.remaining_reply.hits;++h)if(!amount(actor.remaining_reply.damage[h]))return false;
        if(actor.remaining_reply.reach<0||actor.remaining_reply.reach>10000||actor.remaining_reply.travel<0||
           actor.remaining_reply.travel>10000||!amount(actor.remaining_reply.defensive_shield))return false;
    }
    for(unsigned i=0;i<world.camp_count;++i)if(!amount(world.camps[i].health)||!amount(world.camps[i].damage)||
        !amount(world.camps[i].shield.amount)||!point(world.camps[i].position)||world.camps[i].range<0||
        world.camps[i].range>10000)return false;
    return amount(world.aggregate.damage)&&world.aggregate.reach>=0&&world.aggregate.reach<=10000&&
        (!world.spawn_valid||point(world.own_spawn));
}
inline Quote invalid(Reason reason,unsigned operations=0){Quote q;q.reason=reason;q.operations=operations;return q;}
}

inline Quote evaluate(const World& world,const Candidate& candidate){
    if(world.actor_count==0||world.actor_count>actor_capacity||world.owner>=world.actor_count||
       world.camp_count>camp_capacity||world.reply_count>reply_capacity||world.frontier_count==0||
       world.frontier_count>frontier_capacity||world.blocked_count>blocked_capacity||
       world.capture.region_count>region_capacity||candidate.word_count==0||candidate.word_count>word_capacity||
       world.horizon_intervals==0||world.horizon_intervals>8)return detail::invalid(Reason::capacity);
    if(world.frame!=candidate.frame||world.observation!=candidate.observation||world.effect!=candidate.effect)
        return detail::invalid(Reason::missing_feedback);
    if(!detail::amounts(world))return detail::invalid(Reason::invalid_amount);
    unsigned operations=0;Quote result;result.subject=&candidate;result.frame=world.frame;
    result.observation=world.observation;result.effect=world.effect;
    result.valid=result.bounded=result.alive=result.complete=result.next_usable_seen=true;
    result.worst_health=world.actors[world.owner].health;result.banked=result.captured=result.team_captured=std::numeric_limits<unsigned>::max();
    result.reason=Reason::ok;bool assumptions=false;
    for(unsigned branch=0;branch<world.frontier_count;++branch){
        const auto& frontier=world.frontiers[branch];
        if(frontier.event_count>event_capacity)return detail::invalid(Reason::capacity,operations);
        detail::State state;state.world=&world;state.candidate=&candidate;state.frontier=&frontier;
        state.actors=world.actors;state.camps=world.camps;state.capture=world.capture;state.operations=&operations;
        state.cursor=world.observation_cursor;state.minimum_health=world.actors[world.owner].health;
        state.quote.valid=state.quote.bounded=true;state.quote.reason=Reason::ok;
        state.quote.approximate=!world.terrain_complete||!world.sources_complete||!world.effects_complete||
            !world.schedule_complete||!frontier.complete;
        for(unsigned i=0;i<world.actor_count;++i){
            auto& actor=state.actors[i];
            if(actor.knowledge==Knowledge::retained_hidden){assumptions=true;
                if(!actor.alive){actor.alive=true;actor.health=std::max(actor.health,actor.max_health);state.quote.approximate=true;}}
            if(!actor.frontier_complete)state.quote.approximate=true;
            if(frontier.position_overrides&(1u<<i)){
                if(actor.knowledge!=Knowledge::retained_hidden||!detail::point(frontier.positions[i])||
                   distance(frontier.positions[i],actor.belief_center)>actor.belief_radius)
                    return detail::invalid(Reason::invalid_timeline,operations);
                actor.position=frontier.positions[i];
            }
            if(actor.knowledge!=Knowledge::retained_hidden)actor.belief_center=actor.position;
        }
        for(unsigned i=0;i<world.camp_count;++i)assumptions|=state.camps[i].knowledge==CampKnowledge::fresh_asleep;
        state.successes_this_input=0;
        if(!detail::word(state,world.owner,candidate.words[0],0))return detail::invalid(state.quote.reason,operations);
        detail::end(state.actors[world.owner]);
        if(world.spawn_valid&&state.actors[world.owner].alive&&state.actors[world.owner].position==world.own_spawn)
            state.actors[world.owner].health=state.actors[world.owner].max_health;
        for(unsigned e=0;e<frontier.event_count;++e){
            state.event=e+1;const auto& event=frontier.events[e];
            if(event.kind==EventKind::camp_batch){if(!detail::camp_batch(state))return detail::invalid(state.quote.reason,operations);}
            else if(!detail::input(state,event))return detail::invalid(state.quote.reason,operations);
        }
        const auto& owner=state.actors[world.owner];bool complete=state.quote.next_usable_seen;
        if(candidate.purpose==Purpose::work||candidate.purpose==Purpose::counteraction)complete=complete&&state.work;
        else if(candidate.purpose==Purpose::bank)complete=owner.banked>world.actors[world.owner].banked;
        else if(candidate.purpose==Purpose::capture)complete=state.captured;
        else if(candidate.purpose==Purpose::recall)complete=state.recalled;
        result.alive=result.alive&&owner.alive;result.complete=result.complete&&complete;
        result.next_usable_seen=result.next_usable_seen&&state.quote.next_usable_seen;
        result.worst_health=std::min(result.worst_health,state.minimum_health);result.worst_loss=std::max(result.worst_loss,state.loss);
        result.banked=std::min(result.banked,owner.banked-world.actors[world.owner].banked);
        result.captured=std::min(result.captured,state.quote.captured);
        result.team_captured=std::min(result.team_captured,state.quote.team_captured);
        result.owner_share_unknown=result.owner_share_unknown||state.quote.owner_share_unknown;
        result.successful=std::max(result.successful,state.quote.successful);result.refusals=std::max(result.refusals,state.quote.refusals);
        result.observations=std::max(result.observations,state.quote.observations);
        result.camp_batches=std::max(result.camp_batches,state.quote.camp_batches);
        result.hostile_inputs=std::max(result.hostile_inputs,state.quote.hostile_inputs);
        result.hostile_usable=std::max(result.hostile_usable,state.quote.hostile_usable);
        result.owner_inputs=std::max(result.owner_inputs,state.quote.owner_inputs);
        result.owner_usable=std::max(result.owner_usable,state.quote.owner_usable);
        result.lost_owner_inputs=std::max(result.lost_owner_inputs,state.quote.lost_owner_inputs);
        result.approximate=result.approximate||state.quote.approximate;
        if(state.quote.fatal_event<result.fatal_event){result.fatal_event=state.quote.fatal_event;result.fatal_actor=state.quote.fatal_actor;}
    }
    result.operations=operations;result.requires_actual_confirmation=result.successful>0||result.observations>0;
    if(result.alive&&result.complete)result.assurance=result.approximate?Assurance::approximate_frontier:
        assumptions?Assurance::declared_frontier:Assurance::conditional_prefix_bound;
    if(!result.next_usable_seen&&result.alive&&candidate.purpose!=Purpose::bank&&candidate.purpose!=Purpose::capture&&
       candidate.purpose!=Purpose::recall)result.reason=Reason::incomplete_horizon;
    return result;
}
// Utility-only comparison of already evaluated quotes. Admission/coverage and
// remaining computation/dispatch funding remain the incremental caller's job.
inline bool prefer(const Quote& quote,const Candidate& candidate,const Quote& old,const Candidate& incumbent){
    if(quote.alive!=old.alive)return quote.alive;
    if(quote.complete!=old.complete)return quote.complete;
    if(candidate.useful_service!=incumbent.useful_service)return candidate.useful_service>incumbent.useful_service;
    if(quote.banked!=old.banked)return quote.banked>old.banked;
    if(quote.captured!=old.captured)return quote.captured>old.captured;
    if(quote.worst_health!=old.worst_health)return quote.worst_health>old.worst_health;
    if(quote.worst_loss!=old.worst_loss)return quote.worst_loss<old.worst_loss;
    if(quote.lost_owner_inputs!=old.lost_owner_inputs)return quote.lost_owner_inputs<old.lost_owner_inputs;
    return unsigned(quote.assurance)>unsigned(old.assurance);
}
inline Selection choose(const World& world,const Candidate* candidates,std::size_t count,
                        bool allow_approximate=false,bool allow_fatal=false){
    Selection selected;if(!candidates||count>candidate_capacity)return selected;
    for(std::size_t i=0;i<count;++i){
        const auto quote=evaluate(world,candidates[i]);
        if(!quote.valid||!quote.bounded||(!allow_approximate&&quote.approximate)||
           (!allow_fatal&&(!quote.alive||!quote.complete)))continue;
        const bool better=selected.index==std::numeric_limits<std::size_t>::max()||
            prefer(quote,candidates[i],selected.quote,candidates[selected.index]);
        if(better){selected.index=i;selected.quote=quote;}
    }
    return selected;
}
inline bool actual_continuation(const World& world,const Candidate& candidate,const Quote& quote,const Receipt& receipt){
    return quote.valid&&quote.bounded&&((quote.alive&&quote.complete)||receipt.allow_risk)&&
        (!quote.approximate||receipt.allow_approximate)&&quote.subject==&candidate&&quote.frame==world.frame&&quote.observation==world.observation&&
        quote.effect==world.effect&&candidate.frame==world.frame&&candidate.observation==world.observation&&
        candidate.effect==world.effect&&receipt.frame==world.frame&&receipt.observation>world.observation&&
        receipt.effect>world.effect&&receipt.successful_prefix>0&&receipt.successful_prefix<=quote.successful&&
        receipt.actual_body&&receipt.actual_targets&&receipt.actual_occupancy&&receipt.refusal_closed;
}
}
