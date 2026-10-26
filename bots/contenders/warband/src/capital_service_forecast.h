#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

// A dated, SDK-free ONE-GROUP physical simulator. Nothing here is an SDK
// permission, actual receipt, fuel certificate, or value for omitted services.
// Start from personally observed Alive/member facts; native observation ingress
// and the operational incumbent remain the caller's responsibility. Callbacks
// describe one closed conditional world/reply, never an adaptive opaque enemy.
namespace wb::capital::service_forecast {
using Scalar=float;
static_assert(sizeof(Scalar)==4&&std::numeric_limits<Scalar>::is_iec559);
// Materialize every native physical operation at binary32 width. Separate
// volatile stores prevent an optimizer from fusing multiply/subtract or
// retaining wider intermediates. Money/event totals below are separate data.
inline Scalar fp_add(Scalar a,Scalar b){volatile Scalar value=a+b;return value;}
inline Scalar fp_sub(Scalar a,Scalar b){volatile Scalar value=a-b;return value;}
inline Scalar fp_mul(Scalar a,Scalar b){volatile Scalar value=a*b;return value;}
inline constexpr unsigned actor_limit=3,npc_limit=16,external_limit=6,route_limit=512;
inline constexpr unsigned leg_limit=18,purchase_limit=24,event_limit=2048,slot_limit=6;
inline constexpr unsigned item_limit=42,call_quanta_limit=64;
struct Point {int x{},y{};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
enum class Verdict {Unknown,Allowed,Blocked,Refused};
enum class Kit {Varg,Willow,Vendetta};
enum class Channel {None,Bank,Recall};
enum class Life {Unknown,Alive,Reward,Respawning};
enum class Stage {Work,Capture,Release,Altar,Return,Invest,Renew,Done};
enum class PostRenewal {Unknown,ConditionalHold};
enum class LegKind : unsigned {Work,Capture,Release,Altar,Spawn,Renew};
enum class Action {Hold,Move,Basic,Reset,Shield,Heal,Bank,Recall,Buy};
enum class Phase {RoundStart,Capture,Npc,ActorStart,ActorAction,ActorEnd,Reply,Finish};
enum class Status {Empty,Building,Complete,Unknown,Refuted};
enum class Tail {Unknown,Complete};
enum class Termination {None,ModeledWin,RoundBoundary};
enum class Reason {None,Invalid,MissingContract,MissingObservation,MissingRoute,Date,Legality,Vision,
    Reply,Purchase,Arrival,Capacity,Dead,CustodyLost,Interrupted,Unfinished,OutOfWork,WorkLimit,UnsupportedCustody,EngineTerminated,RecallDuration,UnsupportedEffect,Order,UnsupportedDeath};
enum class EventKind {Move,Refused,Basic,Reset,Shield,Heal,Damage,Stun,Clear,CaptureArmed,
    Captured,Released,BankStarted,Banked,RecallStarted,RecallInterrupted,Arrived,SpawnHeal,
    Purchased,Renewed,Respawn,Sleep,Death,BankInterrupted};
struct Body {
    int id{-1};Kit kit{Kit::Varg};Point pose{};Scalar hp{},max_hp{},strength{},focus{},shield{};
    double gold{},earned_gold{},gold_multiplier{1};Scalar camp_multiplier{1.0f},taken_multiplier{1.0f},npc_taken_multiplier{1.0f};
    bool bag_known{},gold_known{};double capital_floor{}; // lower bound, never a peer-gold observation
    unsigned bag{},shield_ticks{},stun_ticks{};bool shield_present{},stun_present{},alive{true};
    bool m{true},a{true},c{true},walked{},cast_used{},completed{},actual{};
    unsigned move_range{1},attack_range{1};std::array<unsigned,2> cooldown{};
    unsigned reset_slot{},shield_slot{1},heal_slot{};Scalar shield_modifier{},heal_modifier{};
    Scalar splash_radius{};bool splash_radius_known{};
    unsigned shield_duration{},heal_range{};std::array<unsigned,item_limit> items{};
    Channel channel{Channel::None};unsigned channel_ticks{},channel_start_bag{};Point channel_target{};
    // Effective duration for THIS body's current inventory. Zero is unknown;
    // neither the base Rule nor a guessed item ID supplies this contract.
    unsigned effective_recall_ticks{};
};
struct Npc {int id{-1};Point pose{};Scalar hp{},max_hp{},strength{},shield{};unsigned range{},empty_batches{};
    bool actual{},awake{},shield_present{},damaged_since_batch{},empty_batches_known{},unsupported_effects{};unsigned shield_ticks{};};
// External bodies are real possible NPC targets/occupants. Their omitted kit
// effects must be closed by the declared reply domain (e.g. explicit Hold).
struct External {int id{-1},team{};Point pose{};Scalar hp{},shield{},taken_multiplier{1.0f};
    bool alive{},actual{},npc_effects_closed{};
    // Explicit MODEL placement from a constructive, observation-consistent
    // history. It never makes hidden pose/HP an actual observation. Such a
    // placement is admitted only in a declared conditional domain; its stats
    // remain unknown and cannot absorb a modeled NPC attack.
    bool conditional_placement{};std::uint64_t placement_revision{};};
struct Group {int id{-1};Life life{Life::Unknown};bool roster_complete{},capture_history_known{};
    unsigned capture_remaining{},respawn_round{},gold{},gems{},generation{};bool capture_armed{};};
struct Rules {bool queried{};unsigned capture_ticks{},sleep_batches{},respawn_delay{},reset_cooldown{},
    shield_cooldown{},heal_cooldown{},recall_ticks{};double bank_multiplier{};Scalar spawn_healing{};
    // recall_ticks retains queried BASE metadata only, never an effective duration.
    bool recall_damage_interrupt_known{},recall_damage_interrupt{};unsigned max_rounds{},winning_gems{},passive_gold{};};
struct Slot {bool controlled{};unsigned actor{};int external_id{-1};};
struct World {int round{},horizon{},team{};Phase phase{Phase::RoundStart};unsigned slot{},npc_cursor{};
    unsigned actors{},npcs{},externals{},slots{};std::array<Body,actor_limit> bodies{};
    std::array<Npc,npc_limit> camps{};std::array<External,external_limit> others{};Group group{};Rules rules{};
    std::array<Slot,slot_limit> even{},odd{};std::uint64_t team_score{},revision{};
};
struct RouteStep {Point destination;int earliest_round,latest_round;};
struct Leg {Point origin{};unsigned begin{},count{};bool complete{};};
struct Purchase {unsigned item{},count{1};std::uint64_t quote_revision{};double quoted_cost{};};
struct BodyPlan {bool required{true},use_recall{},shield_for_work{},heal_for_work{},reset_for_work{true},require_useful_support{};
    std::array<int,6> legs{{-1,-1,-1,-1,-1,-1}};unsigned purchase_begin{},purchase_count{};
    Point altar{};bool altar_known{};Stage initial{Stage::Work};PostRenewal post_renewal{PostRenewal::Unknown};};
struct Program {unsigned route_count{},leg_count{},purchase_count{};
    std::array<RouteStep,route_limit> route;std::array<Leg,leg_limit> legs{};
    std::array<Purchase,purchase_limit> purchases{};std::array<BodyPlan,actor_limit> bodies{};
    std::uint64_t id{};};
struct Domain {std::uint64_t initial_world{},physics{},reply{},services{},assumptions{};
    bool physics_closed{},replies_closed{},local_sources_closed{},conditional{},
         service_tail_closed{},other_services_represented{};
};
struct Primitive {Action action{Action::Hold};Point destination{};int target{-1};unsigned slot{};};
enum class EffectKind {Damage,Stun,Heal,Shield,ExternalMove,ExternalDeath,CustodyLoss,BankLock};
struct Effect {EffectKind kind{EffectKind::Damage};int target{-1};Scalar amount{};unsigned ticks{};Point pose{};};
struct Reply {bool complete{};unsigned count{};std::array<Effect,8> effects{};std::uint64_t domain{};};
struct PurchaseEffect {bool complete{};std::uint64_t revision{};double cost{};Scalar health_growth{},strength_growth{},focus_growth{};
    Scalar camp_multiplier{1.0f},taken_multiplier{1.0f},npc_taken_multiplier{1.0f};double gold_multiplier{1};std::array<unsigned,item_limit> items{};
    unsigned effective_recall_ticks{}; // exact post-purchase duration, or explicitly unknown
};
struct Callbacks {
    void* context{};
    Verdict (*legal)(void*,const World&,unsigned,const Primitive&){};
    Verdict (*visible)(void*,const World&,unsigned,int){};
    Verdict (*spawn)(void*,const World&,unsigned,Point){};
    Verdict (*altar)(void*,const World&,unsigned,Point){}; // actual-model lock+range contract
    Verdict (*contested)(void*,const World&){}; // Allowed = known uncontested
    Reply (*reply)(void*,const World&,int){};
    bool (*arrival)(void*,const World&,unsigned,Point&){};
    PurchaseEffect (*purchase)(void*,const World&,unsigned,const Purchase&){};
    // Called BEFORE each bounded atomic transition. Caller may stop a native
    // invocation without abandoning the retained simulation. Callback execution
    // bounds and begin/copy costs require separate native qualification.
    bool (*work)(void*,unsigned){};
    // Champion and camp handles have separate namespaces. A Heal requires
    // this typed callback; a matching camp handle supplies no visibility.
    Verdict (*visible_champion)(void*,const World&,unsigned,int){};
};
struct Event {EventKind kind;int round,actor,target;double amount;unsigned aux;Point pose;};
struct Facts {double work_damage{},raw_damage{},shield_damage{},renewal_damage{},capture_gold{},spent_gold{};unsigned captured_gems{},banked{},
    purchases{},useful_heals{},useful_support_words{},refusals{};bool clear{},released{},recovered{},renewed{},custody_lost{};int renewal_hit_round{-1};};
struct Result {Status status{Status::Empty};Tail service_tail{Tail::Unknown};Reason reason{Reason::None};
    Termination termination{Termination::None};
    World terminal{};std::array<Facts,actor_limit> bodies{};std::array<Event,event_limit> events;
    unsigned event_count{},quanta{},last_call_quanta{};Domain domain{};int initial_round{},horizon{};
    std::uint64_t program{},initial_fingerprint{};bool forecast{true},physical_complete{},displaced_service_comparable{};
};
inline bool same_domain(const Domain& a,const Domain& b){return a.initial_world==b.initial_world&&a.physics==b.physics&&
    a.reply==b.reply&&a.services==b.services&&a.assumptions==b.assumptions&&a.physics_closed==b.physics_closed&&
    a.replies_closed==b.replies_closed&&a.local_sources_closed==b.local_sources_closed&&a.conditional==b.conditional&&
    a.service_tail_closed==b.service_tail_closed&&a.other_services_represented==b.other_services_represented;}
// One group's joint and independent programmes must each be simulated as ONE
// World. Never sum solo results which can claim the same source reward twice.
inline bool comparable(const Result& a,const Result& b){return a.status==Status::Complete&&b.status==Status::Complete&&
    a.service_tail==Tail::Complete&&b.service_tail==Tail::Complete&&a.horizon==b.horizon&&a.initial_round==b.initial_round&&
    a.initial_fingerprint&&a.initial_fingerprint==b.initial_fingerprint&&same_domain(a.domain,b.domain)&&
    a.displaced_service_comparable&&b.displaced_service_comparable;}

class Simulator {
    World w_{};Program p_;Callbacks cb_{};Result out_;std::array<Stage,actor_limit> stage_{};
    std::array<std::array<unsigned,6>,actor_limit> route_cursor_{};
    std::array<unsigned,actor_limit> bought_{};std::array<bool,actor_limit> healed_this_input_{};unsigned ops_{};
    bool finite(double v)const{return std::isfinite(v)&&v>=0&&v<=1e12;}
    bool point(Point p)const{return p.x>=0&&p.y>=0&&p.x<=10000&&p.y<=10000;}
    std::uint64_t fingerprint()const{
        std::uint64_t h=1469598103934665603ull;auto u=[&](std::uint64_t x){h=(h^x)*1099511628211ull;};
        auto d=[&](auto x){if constexpr(std::is_same_v<decltype(x),Scalar>)u(std::bit_cast<std::uint32_t>(x));
            else u(std::bit_cast<std::uint64_t>(x));};auto p=[&](Point x){u(unsigned(x.x));u(unsigned(x.y));};
        u(w_.round);u(w_.horizon);u(w_.team);u(unsigned(w_.phase));u(w_.slot);u(w_.npc_cursor);u(w_.actors);u(w_.npcs);u(w_.externals);u(w_.slots);u(w_.team_score);u(w_.revision);
        const auto& r=w_.rules;u(r.queried);u(r.capture_ticks);u(r.sleep_batches);u(r.respawn_delay);u(r.reset_cooldown);u(r.shield_cooldown);u(r.heal_cooldown);u(r.recall_ticks);
        d(r.bank_multiplier);d(r.spawn_healing);u(r.recall_damage_interrupt_known);u(r.recall_damage_interrupt);
        u(r.max_rounds);u(r.winning_gems);u(r.passive_gold);
        const auto& g=w_.group;u(g.id);u(unsigned(g.life));u(g.roster_complete);u(g.capture_history_known);u(g.capture_remaining);u(g.respawn_round);u(g.gold);u(g.gems);u(g.generation);u(g.capture_armed);
        for(unsigned a=0;a<w_.actors;++a){const auto& b=w_.bodies[a];u(b.id);u(unsigned(b.kit));p(b.pose);d(b.hp);d(b.max_hp);d(b.strength);d(b.focus);d(b.shield);
            u(b.gold_known);if(b.gold_known)d(b.gold);u(b.bag_known);u(b.bag);d(b.capital_floor);d(b.earned_gold);d(b.gold_multiplier);d(b.camp_multiplier);d(b.taken_multiplier);d(b.npc_taken_multiplier);
            u(b.shield_ticks);u(b.stun_ticks);u(b.shield_present);u(b.stun_present);u(b.alive);u(b.m);u(b.a);u(b.c);u(b.walked);u(b.cast_used);u(b.completed);u(b.actual);
            u(b.move_range);u(b.attack_range);for(auto x:b.cooldown)u(x);u(b.reset_slot);u(b.shield_slot);u(b.heal_slot);d(b.shield_modifier);d(b.heal_modifier);d(b.splash_radius);u(b.splash_radius_known);u(b.shield_duration);u(b.heal_range);
            for(auto x:b.items)u(x);u(unsigned(b.channel));u(b.channel_ticks);u(b.channel_start_bag);p(b.channel_target);u(b.effective_recall_ticks);}
        for(unsigned n=0;n<w_.npcs;++n){const auto& c=w_.camps[n];u(c.id);p(c.pose);d(c.hp);d(c.max_hp);d(c.strength);d(c.shield);u(c.range);u(c.empty_batches);
            u(c.actual);u(c.awake);u(c.shield_present);u(c.damaged_since_batch);u(c.empty_batches_known);u(c.unsupported_effects);u(c.shield_ticks);}
        for(unsigned n=0;n<w_.externals;++n){const auto& e=w_.others[n];u(e.id);u(e.team);p(e.pose);d(e.hp);d(e.shield);d(e.taken_multiplier);u(e.alive);u(e.actual);u(e.npc_effects_closed);u(e.conditional_placement);u(e.placement_revision);}
        for(const auto* order:{&w_.even,&w_.odd})for(unsigned n=0;n<w_.slots;++n){const auto& s=(*order)[n];u(s.controlled);u(s.actor);u(s.external_id);}
        return h?h:1;
    }
    bool stop(Reason r,Status s=Status::Unknown){out_.reason=r;out_.status=s;out_.terminal=w_;return false;}
    bool event(EventKind kind,int actor,int target=-1,double amount=0,unsigned aux=0,Point pose={}){
        if(out_.event_count==event_limit)return stop(Reason::Capacity);
        out_.events[out_.event_count++]={kind,w_.round,actor,target,amount,aux,pose};return true;
    }
    unsigned present(unsigned* indices=nullptr)const{unsigned count=0;
        for(unsigned a=0;a<w_.actors;++a){if(w_.bodies[a].alive&&region(w_.bodies[a].pose)){
            if(indices)indices[count]=a;++count;}}
        return count;}
    bool region(Point p)const{for(unsigned n=0;n<w_.npcs;++n)if(w_.camps[n].pose==p)return true;return false;}
    bool external_region()const{for(unsigned n=0;n<w_.externals;++n)if(w_.others[n].alive&&region(w_.others[n].pose))return true;return false;}
    bool external_ally_region()const{for(unsigned n=0;n<w_.externals;++n)if(w_.others[n].alive&&w_.others[n].team==w_.team&&region(w_.others[n].pose))return true;return false;}
    const Slot& slot()const{return (w_.round&1?w_.odd:w_.even)[w_.slot];}
    int order_rank(int id)const{int rank=-1;const auto& order=w_.round&1?w_.odd:w_.even;for(unsigned n=0;n<w_.slots;++n){const auto& s=order[n];
        if((s.controlled&&w_.bodies[s.actor].id==id)||(!s.controlled&&s.external_id==id)){if(rank>=0)return -1;rank=int(n);}}
        return rank;}
    Verdict legality(unsigned a,const Primitive& x){return cb_.legal?cb_.legal(cb_.context,w_,a,x):Verdict::Unknown;}
    bool required_alive(){for(unsigned a=0;a<w_.actors;++a)if(p_.bodies[a].required&&!w_.bodies[a].alive)return stop(Reason::Dead,Status::Refuted);return true;}
    bool capture_presence_changed(){if(w_.group.life==Life::Reward){
        if(external_ally_region())return stop(Reason::UnsupportedCustody);
        if(!present()||external_region()){w_.group.capture_armed=false;w_.group.capture_remaining=0;}}
        return true;}
    bool hurt(unsigned a,Scalar raw,bool npc_source=false){auto& b=w_.bodies[a];if(!finite(raw))return stop(Reason::Invalid);
        // A pre-folded two-item multiplier does not establish native f32
        // factor order. Current Native admits only single-factor classes until
        // that separate contract is qualified (Durability+Jungle is Unknown).
        if(npc_source&&b.taken_multiplier!=1.0f&&b.npc_taken_multiplier!=b.taken_multiplier)return stop(Reason::MissingContract);
        const Scalar amount=fp_mul(raw,npc_source?b.npc_taken_multiplier:b.taken_multiplier),absorbed=std::min(amount,b.shield);
        if(!finite(amount))return stop(Reason::Invalid);b.shield=fp_sub(b.shield,absorbed);
        if(b.shield_present&&b.shield<=0){b.shield=0;b.shield_present=false;b.shield_ticks=0;}
        const Scalar residual=fp_sub(amount,absorbed);b.hp=std::max(0.0f,fp_sub(b.hp,residual));
        if(b.hp==0){b.alive=b.m=b.a=b.c=false;b.shield=0;b.shield_present=false;b.channel=Channel::None;
            if(b.bag){b.bag=0;out_.bodies[a].custody_lost=true;}const bool custody=capture_presence_changed();
            // A known lethal physical prefix outranks an unrecordable trace or
            // an unknown service/channel tail. Refutation is this Programme only.
            if(p_.bodies[a].required){event(EventKind::Damage,b.id,-1,amount);event(EventKind::Death,b.id);return stop(Reason::Dead,Status::Refuted);}if(!custody)return false;}
        if(amount>0&&b.channel==Channel::Recall){if(!w_.rules.recall_damage_interrupt_known)return stop(Reason::MissingContract);
            if(w_.rules.recall_damage_interrupt){b.channel=Channel::None;b.channel_ticks=0;if(!event(EventKind::RecallInterrupted,b.id))return false;}}
        if(!event(EventKind::Damage,b.id,-1,amount))return false;
        if(b.hp==0)event(EventKind::Death,b.id);
        return required_alive();
    }
    bool camp_hit(unsigned a,unsigned n,Scalar amount){auto& c=w_.camps[n];if(c.hp<=0)return true;
        const Scalar hp_before=c.hp,absorbed=std::min(c.shield,amount);c.shield=fp_sub(c.shield,absorbed);
        const Scalar residual=fp_sub(amount,absorbed);c.hp=std::max(0.0f,fp_sub(c.hp,residual));
        if(c.shield_present&&c.shield<=0){c.shield_present=false;c.shield_ticks=0;}
        for(unsigned k=0;k<w_.npcs;++k)if(w_.camps[k].hp>0){w_.camps[k].awake=true;w_.camps[k].empty_batches=0;w_.camps[k].empty_batches_known=true;w_.camps[k].damaged_since_batch=true;}
        out_.bodies[a].raw_damage+=amount;out_.bodies[a].shield_damage+=absorbed;out_.bodies[a].work_damage+=fp_sub(hp_before,c.hp);
        if(c.hp==0&&w_.bodies[a].kit==Kit::Vendetta)w_.bodies[a].cooldown={0,0};
        bool live=false;for(unsigned k=0;k<w_.npcs;++k)live=live||w_.camps[k].hp>0;
        if(!live){w_.group.life=Life::Reward;w_.group.capture_history_known=true;w_.group.capture_armed=false;w_.group.capture_remaining=0;
            for(unsigned k=0;k<w_.actors;++k)out_.bodies[k].clear=true;if(!event(EventKind::Clear,w_.bodies[a].id))return false;}
        return true;
    }
    bool primitive(unsigned a,const Primitive& x){auto& b=w_.bodies[a];
        if(x.action==Action::Hold)return true;
        if(!b.alive||b.stun_present||b.channel!=Channel::None||b.completed)return stop(Reason::Legality);
        if(legality(a,x)!=Verdict::Allowed)return stop(Reason::Legality);
        switch(x.action){
        case Action::Move:{if(!b.m||!point(x.destination)||x.destination==b.pose||distance(b.pose,x.destination)>int(b.move_range))return stop(Reason::Legality);
            for(unsigned k=0;k<w_.actors;++k)if(k!=a&&w_.bodies[k].alive&&w_.bodies[k].pose==x.destination)return stop(Reason::Legality);
            for(unsigned k=0;k<w_.externals;++k)if(w_.others[k].alive&&w_.others[k].pose==x.destination)return stop(Reason::Legality);
            for(unsigned k=0;k<w_.npcs;++k)if(w_.camps[k].hp>0&&w_.camps[k].pose==x.destination)return stop(Reason::Legality);
            b.pose=x.destination;b.m=false;b.walked=true;if(b.kit==Kit::Willow)b.a=false;
            return event(EventKind::Move,b.id,-1,0,0,b.pose)&&capture_presence_changed();}
        case Action::Basic:{unsigned target=w_.npcs;for(unsigned n=0;n<w_.npcs;++n)if(w_.camps[n].id==x.target)target=n;
            if(!b.a||target==w_.npcs||w_.camps[target].hp<=0||distance(b.pose,w_.camps[target].pose)>int(b.attack_range))return stop(Reason::Legality);
            if(!cb_.visible||cb_.visible(cb_.context,w_,a,x.target)!=Verdict::Allowed)return stop(Reason::Vision);
            const auto center=w_.camps[target].pose;const Scalar amount=fp_mul(b.strength,b.camp_multiplier);
            if(!finite(amount))return stop(Reason::Invalid);
            // Hold replies do not disable Varg's passive. This first class
            // has no PvP damage/kill/custody interpreter, so do not apply even
            // the camp portion of a word whose enemy collateral is omitted.
            if(b.kit==Kit::Varg){if(!b.splash_radius_known||!finite(b.splash_radius))return stop(Reason::UnsupportedEffect);
                for(unsigned k=0;k<w_.externals;++k){const auto& e=w_.others[k];
                    if(e.alive&&e.team!=w_.team&&Scalar(distance(center,e.pose))<=b.splash_radius)return stop(Reason::UnsupportedEffect);}}
            if(amount<=0)return stop(Reason::OutOfWork);const auto prior_work=out_.bodies[a].work_damage;b.a=false;if(b.kit==Kit::Willow)b.m=false;
            for(unsigned n=0;n<w_.npcs;++n)if(n==target||(b.kit==Kit::Varg&&Scalar(distance(center,w_.camps[n].pose))<=b.splash_radius))if(!camp_hit(a,n,amount))return false;
            const auto persistent=out_.bodies[a].work_damage-prior_work;
            if(healed_this_input_[a]&&persistent>0){++out_.bodies[a].useful_support_words;healed_this_input_[a]=false;}
            if(stage_[a]==Stage::Renew&&persistent>0){out_.bodies[a].renewal_damage+=persistent;out_.bodies[a].renewed=true;stage_[a]=Stage::Done;
                out_.bodies[a].renewal_hit_round=w_.round;
                if(!event(EventKind::Renewed,b.id,x.target,persistent))return false;}
            return event(EventKind::Basic,b.id,x.target,persistent);}
        case Action::Reset:if(b.a||!b.c||b.cooldown[b.reset_slot]||b.cast_used)return stop(Reason::Legality);
            b.a=true;b.c=false;b.cast_used=true;b.cooldown[b.reset_slot]=w_.rules.reset_cooldown;return event(EventKind::Reset,b.id);
        case Action::Shield:if(b.kit!=Kit::Varg||!b.c||b.cast_used||b.shield_present||b.cooldown[b.shield_slot])return stop(Reason::Legality);
            b.c=false;b.cast_used=true;b.cooldown[b.shield_slot]=w_.rules.shield_cooldown;b.shield_present=true;b.shield=fp_mul(b.focus,b.shield_modifier);b.shield_ticks=b.shield_duration;
            return event(EventKind::Shield,b.id,-1,b.shield,b.shield_ticks);
        case Action::Heal:{unsigned k=w_.actors;for(unsigned n=0;n<w_.actors;++n)if(w_.bodies[n].id==x.target)k=n;
            if(b.kit!=Kit::Willow||!b.c||b.cast_used||b.cooldown[b.heal_slot]||k==w_.actors||k==a||!w_.bodies[k].alive||
               distance(b.pose,w_.bodies[k].pose)>int(b.heal_range)||w_.bodies[k].hp>=w_.bodies[k].max_hp)return stop(Reason::Legality);
            if(!cb_.visible_champion||cb_.visible_champion(cb_.context,w_,a,x.target)!=Verdict::Allowed)return stop(Reason::Vision);
            auto& target=w_.bodies[k];const Scalar before=target.hp,amount=fp_mul(b.focus,b.heal_modifier);
            target.hp=std::min(target.max_hp,fp_add(target.hp,amount));const Scalar delta=fp_sub(target.hp,before);
            b.c=false;b.cast_used=true;b.cooldown[b.heal_slot]=w_.rules.heal_cooldown;++out_.bodies[a].useful_heals;healed_this_input_[a]=delta>0;return event(EventKind::Heal,b.id,x.target,delta);}
        case Action::Bank:{if(!b.bag||!cb_.altar||cb_.altar(cb_.context,w_,a,x.destination)!=Verdict::Allowed||distance(b.pose,x.destination)>1)return stop(Reason::MissingContract);
            const auto ticks=std::ceil(double(b.bag)*w_.rules.bank_multiplier);if(!finite(ticks)||ticks<1||ticks>512)return stop(Reason::Invalid);
            b.channel=Channel::Bank;b.channel_ticks=unsigned(ticks);b.channel_start_bag=b.bag;b.channel_target=x.destination;b.m=b.a=b.c=false;
            return event(EventKind::BankStarted,b.id,-1,0,b.channel_ticks);}
        case Action::Recall:if(!cb_.arrival)return stop(Reason::Arrival);
            if(!b.effective_recall_ticks||b.effective_recall_ticks>512)return stop(Reason::RecallDuration);
            b.channel=Channel::Recall;b.channel_ticks=b.effective_recall_ticks;b.m=b.a=b.c=false;return event(EventKind::RecallStarted,b.id,-1,0,b.channel_ticks);
        case Action::Buy:case Action::Hold:return stop(Reason::Invalid);
        }
        return stop(Reason::Invalid);
    }
    bool route(unsigned a,LegKind kind,bool& done){done=false;const auto k=unsigned(kind);const auto id=p_.bodies[a].legs[k];
        if(id<0||unsigned(id)>=p_.leg_count)return stop(Reason::MissingRoute);const auto& leg=p_.legs[unsigned(id)];auto& cursor=route_cursor_[a][k];
        if(!leg.complete||leg.begin>p_.route_count||leg.count>p_.route_count-leg.begin)return stop(Reason::MissingRoute);
        auto& b=w_.bodies[a];if(cursor==0&&!(b.pose==leg.origin))return stop(Reason::Arrival);
        if(cursor==leg.count){done=true;return true;}const auto& step=p_.route[leg.begin+cursor];
        if(w_.round<step.earliest_round)return true;if(w_.round>step.latest_round)return stop(Reason::Date);
        if(!b.m)return true;const auto legal=legality(a,{Action::Move,step.destination});
        if(legal==Verdict::Refused){++out_.bodies[a].refusals;event(EventKind::Refused,b.id,-1,0,0,step.destination);return stop(Reason::Legality);}
        if(legal!=Verdict::Allowed)return stop(Reason::Legality);
        if(!primitive(a,{Action::Move,step.destination}))return false;++cursor;done=cursor==leg.count;return true;
    }
    bool input_action(unsigned a){auto& b=w_.bodies[a];
        // A lifecycle witness is not a suffix. No implicit idle programme may
        // turn a missing paid exit into an alleged whole-job death refutation.
        if(stage_[a]==Stage::Done&&p_.bodies[a].post_renewal!=PostRenewal::ConditionalHold)return stop(Reason::MissingContract);
        if(!b.alive||b.stun_present||b.channel!=Channel::None||stage_[a]==Stage::Done){w_.phase=Phase::ActorEnd;return true;}
        if(ops_>=8){w_.phase=Phase::ActorEnd;return true;}++ops_;auto& s=stage_[a];bool done=false;
        if(s==Stage::Work&&w_.group.life!=Life::Alive){s=Stage::Capture;return true;}
        if(s==Stage::Work||s==Stage::Renew){
            if(s==Stage::Renew&&(b.bag||b.channel!=Channel::None||!out_.bodies[a].released||!out_.bodies[a].recovered||bought_[a]!=p_.bodies[a].purchase_count))return stop(Reason::Unfinished);
            const auto before=b.pose;if(!route(a,s==Stage::Work?LegKind::Work:LegKind::Renew,done))return false;
            if(!(before==b.pose))return true;if(!done){w_.phase=Phase::ActorEnd;return true;}
            if(w_.group.life!=Life::Alive){w_.phase=Phase::ActorEnd;return true;}
            if(p_.bodies[a].heal_for_work&&b.kit==Kit::Willow&&b.c&&!b.cooldown[b.heal_slot])for(unsigned k=0;k<w_.actors;++k)
                if(k!=a&&w_.bodies[k].alive&&w_.bodies[k].hp<w_.bodies[k].max_hp&&distance(b.pose,w_.bodies[k].pose)<=int(b.heal_range))return primitive(a,{Action::Heal,{},w_.bodies[k].id});
            if(p_.bodies[a].shield_for_work&&b.kit==Kit::Varg&&b.c&&!b.cooldown[b.shield_slot]&&!b.shield_present)return primitive(a,{Action::Shield});
            int target=-1;for(unsigned n=0;n<w_.npcs;++n)if(w_.camps[n].hp>0&&distance(b.pose,w_.camps[n].pose)<=int(b.attack_range)){target=w_.camps[n].id;break;}
            if(target<0){w_.phase=Phase::ActorEnd;return true;} // another represented actor may finish the group
            if(b.a)return primitive(a,{Action::Basic,{},target});
            if(p_.bodies[a].reset_for_work&&b.c&&!b.cooldown[b.reset_slot])return primitive(a,{Action::Reset});
            w_.phase=Phase::ActorEnd;return true;
        }
        if(s==Stage::Capture){if(w_.group.life==Life::Respawning){s=Stage::Release;return true;}
            if(w_.group.life!=Life::Reward)return stop(Reason::MissingObservation);
            if(!route(a,LegKind::Capture,done))return false;if(done&&!region(b.pose))return stop(Reason::Arrival);
            w_.phase=Phase::ActorEnd;return true;}
        if(s==Stage::Release){if(!route(a,LegKind::Release,done))return false;if(done){if(region(b.pose))return stop(Reason::Arrival);
                out_.bodies[a].released=true;s=Stage::Altar;return event(EventKind::Released,b.id);}return true;}
        if(s==Stage::Altar){if(b.bag){if(!route(a,LegKind::Altar,done))return false;if(done){
                    if(!p_.bodies[a].altar_known)return stop(Reason::MissingObservation);
                    return primitive(a,{Action::Bank,p_.bodies[a].altar});}return true;}s=Stage::Return;return true;}
        if(s==Stage::Return){const auto on=cb_.spawn?cb_.spawn(cb_.context,w_,a,b.pose):Verdict::Unknown;if(on==Verdict::Unknown)return stop(Reason::MissingContract);
            if(on==Verdict::Allowed){if(b.hp<b.max_hp){w_.phase=Phase::ActorEnd;return true;}out_.bodies[a].recovered=true;s=Stage::Invest;return true;}
            if(p_.bodies[a].use_recall)return primitive(a,{Action::Recall});if(!route(a,LegKind::Spawn,done))return false;
            if(done&&(!cb_.spawn||cb_.spawn(cb_.context,w_,a,b.pose)!=Verdict::Allowed))return stop(Reason::Arrival);return true;}
        if(s==Stage::Invest){const auto& bp=p_.bodies[a];if(bought_[a]==bp.purchase_count){s=Stage::Renew;return true;}
            if(bp.purchase_begin+bought_[a]>=p_.purchase_count||!cb_.purchase)return stop(Reason::Purchase);
            const auto& intent=p_.purchases[bp.purchase_begin+bought_[a]];
            const auto q=cb_.purchase(cb_.context,w_,a,intent);
            const auto available=b.gold_known?b.gold:b.capital_floor;
            if(!intent.quote_revision||!intent.count||intent.item>=item_limit||!q.complete||q.revision!=intent.quote_revision||!finite(q.cost)||q.cost<=0||q.cost!=intent.quoted_cost||q.cost>std::floor(available)||!finite(q.health_growth)||
               !finite(q.strength_growth)||!finite(q.focus_growth)||!finite(q.camp_multiplier)||!finite(q.taken_multiplier)||!finite(q.npc_taken_multiplier)||!finite(q.gold_multiplier))return stop(Reason::Purchase);
            if(!cb_.spawn||cb_.spawn(cb_.context,w_,a,b.pose)!=Verdict::Allowed||legality(a,{Action::Buy,{},int(intent.item)})!=Verdict::Allowed)return stop(Reason::Purchase);
            if(b.gold_known)b.gold-=q.cost;b.capital_floor=std::max(0.0,b.capital_floor-q.cost);
            b.max_hp=fp_add(b.max_hp,q.health_growth);b.hp=std::min(b.max_hp,fp_add(b.hp,q.health_growth));
            b.strength=fp_add(b.strength,q.strength_growth);b.focus=fp_add(b.focus,q.focus_growth);b.items=q.items;
            b.camp_multiplier=q.camp_multiplier;b.taken_multiplier=q.taken_multiplier;b.npc_taken_multiplier=q.npc_taken_multiplier;b.gold_multiplier=q.gold_multiplier;
            b.effective_recall_ticks=q.effective_recall_ticks;
            ++bought_[a];++out_.bodies[a].purchases;out_.bodies[a].spent_gold+=q.cost;return event(EventKind::Purchased,b.id,int(intent.item),q.cost);}
        return stop(Reason::Invalid);
    }
    bool round_start(){if(w_.round>w_.horizon){w_.phase=Phase::Finish;return true;}
        if(w_.group.life==Life::Respawning&&unsigned(w_.round)>=w_.group.respawn_round&&!present()&&!external_region()){
            for(unsigned n=0;n<w_.npcs;++n){auto& c=w_.camps[n];c.hp=c.max_hp;c.awake=false;c.empty_batches=0;c.empty_batches_known=true;c.damaged_since_batch=false;c.shield=0;c.shield_present=false;}
            w_.group.life=Life::Alive;++w_.group.generation;if(!event(EventKind::Respawn,-1))return false;}
        w_.phase=Phase::Capture;return true;
    }
    bool capture(){if(w_.group.life==Life::Reward){if(!w_.group.capture_history_known)return stop(Reason::MissingObservation);
            if(!cb_.contested)return stop(Reason::MissingContract);const auto contest=cb_.contested(cb_.context,w_);
            if(external_ally_region())return stop(Reason::UnsupportedCustody);
            if(contest==Verdict::Unknown)return stop(Reason::MissingContract);unsigned ids[actor_limit]{};const auto count=present(ids);
            if(!count||external_region()||contest!=Verdict::Allowed){w_.group.capture_armed=false;w_.group.capture_remaining=0;}
            else if(!w_.group.capture_armed){w_.group.capture_armed=true;w_.group.capture_remaining=w_.rules.capture_ticks;if(!event(EventKind::CaptureArmed,-1,-1,0,w_.group.capture_remaining))return false;}
            else {if(w_.group.capture_remaining)--w_.group.capture_remaining;if(!w_.group.capture_remaining){
                unsigned lowest=ids[0];for(unsigned k=1;k<count;++k)if(w_.bodies[ids[k]].id<w_.bodies[lowest].id)lowest=ids[k];
                for(unsigned k=0;k<count;++k){auto a=ids[k];auto& b=w_.bodies[a];const auto gems=w_.group.gems/count+(a==lowest?w_.group.gems%count:0);
                    const auto raw=w_.group.gold/count+(a==lowest?w_.group.gold%count:0);
                    // Non-unit gold-bonus accumulation has its own native
                    // rounding contract, not the verified HP subtraction one.
                    if(raw&&b.gold_multiplier!=1.0)return stop(Reason::MissingContract);
                    const double gold=double(raw);
                    b.bag+=gems;if(b.gold_known)b.gold+=gold;b.capital_floor+=gold;b.earned_gold+=gold;out_.bodies[a].captured_gems+=gems;out_.bodies[a].capture_gold+=gold;
                    if(!event(EventKind::Captured,b.id,-1,gold,gems))return false;}
                w_.group.life=Life::Respawning;w_.group.respawn_round=unsigned(w_.round)+w_.rules.respawn_delay;w_.group.capture_armed=false;}}}
        w_.npc_cursor=0;w_.phase=Phase::Npc;return true;
    }
    bool npc(){if(w_.npc_cursor==w_.npcs){w_.slot=0;w_.phase=slot().controlled?Phase::ActorStart:Phase::Reply;return true;}
        auto& c=w_.camps[w_.npc_cursor++];if(c.hp<=0)return true;
        if(c.shield_present&&c.shield_ticks==0){c.shield_present=false;c.shield=0;}if(c.shield_ticks)--c.shield_ticks;
        if(!c.awake)return true;int id=-1,nearest=1000000;bool own=false;unsigned chosen=0;
        struct Candidate {int id,distance;unsigned at;bool own;};
        std::array<Candidate,actor_limit+external_limit> candidates;unsigned count=0;
        for(unsigned a=0;a<w_.actors;++a)if(w_.bodies[a].alive){const auto d=distance(c.pose,w_.bodies[a].pose);
            if(d<=int(c.range)){candidates[count++]={w_.bodies[a].id,d,a,true};nearest=std::min(nearest,d);}}
        for(unsigned a=0;a<w_.externals;++a)if(w_.others[a].alive){const auto d=distance(c.pose,w_.others[a].pose);
            if(d<=int(c.range)){candidates[count++]={w_.others[a].id,d,a,false};nearest=std::min(nearest,d);}}
        // Missing order is relevant only inside the FINAL nearest set. A
        // farther tied pair must not reject a later unique closer target.
        for(unsigned n=0;n<count;++n){const auto& candidate=candidates[n];if(candidate.distance!=nearest)continue;
            if(id>=0){const auto prior=order_rank(id),next=order_rank(candidate.id);if(prior<0||next<0)return stop(Reason::Order);
                if(next>=prior)continue;}
            id=candidate.id;own=candidate.own;chosen=candidate.at;}
        if(id<0){if(c.damaged_since_batch){c.damaged_since_batch=false;c.empty_batches=0;c.empty_batches_known=true;return true;}
            if(!c.empty_batches_known)return stop(Reason::MissingObservation);
            if(++c.empty_batches>=w_.rules.sleep_batches){c.awake=false;return event(EventKind::Sleep,c.id);}return true;}
        c.empty_batches=0;c.empty_batches_known=true;c.damaged_since_batch=false;if(own)return hurt(chosen,c.strength,true);auto& e=w_.others[chosen];if(!e.actual||!e.npc_effects_closed)return stop(Reason::MissingContract);
        const Scalar hit=fp_mul(c.strength,e.taken_multiplier),absorbed=std::min(e.shield,hit);e.shield=fp_sub(e.shield,absorbed);
        const Scalar residual=fp_sub(hit,absorbed);e.hp=std::max(0.0f,fp_sub(e.hp,residual));e.alive=e.hp>0;
        if(!e.alive)return stop(Reason::UnsupportedDeath); // external custody/respawn is not this class
        return capture_presence_changed();
    }
    bool actor_start(unsigned a){auto& b=w_.bodies[a];ops_=0;healed_this_input_[a]=false;b.completed=false;
        if(!b.alive)return stop(p_.bodies[a].required?Reason::Dead:Reason::UnsupportedDeath,
            p_.bodies[a].required?Status::Refuted:Status::Unknown);
        if(b.shield_present&&!b.shield_ticks){b.shield_present=false;b.shield=0;}if(b.stun_present&&!b.stun_ticks)b.stun_present=false;
        if(b.channel==Channel::Bank){if(!cb_.altar)return stop(Reason::MissingContract);const auto open=cb_.altar(cb_.context,w_,a,b.channel_target);
            if(open==Verdict::Unknown)return stop(Reason::MissingContract);if(open!=Verdict::Allowed){b.channel=Channel::None;b.channel_ticks=0;
                if(!event(EventKind::BankInterrupted,b.id))return false;}}
        if(b.channel!=Channel::None&&!b.channel_ticks){if(b.channel==Channel::Bank){
                if(!cb_.altar)return stop(Reason::MissingContract);const auto open=cb_.altar(cb_.context,w_,a,b.channel_target);
                if(open==Verdict::Unknown)return stop(Reason::MissingContract);if(open!=Verdict::Allowed){b.channel=Channel::None;}
                else{const auto amount=b.bag;b.bag=0;w_.team_score+=amount;out_.bodies[a].banked+=amount;b.channel=Channel::None;
                    if(!event(EventKind::Banked,b.id,-1,double(amount)))return false;
                    if(w_.team_score>=w_.rules.winning_gems){out_.termination=Termination::ModeledWin;return stop(Reason::EngineTerminated);}}}
            else {Point landing{};if(!cb_.arrival||!cb_.arrival(cb_.context,w_,a,landing)||!point(landing)||!cb_.spawn||cb_.spawn(cb_.context,w_,a,landing)!=Verdict::Allowed)return stop(Reason::Arrival);
                b.pose=landing;b.channel=Channel::None;if(!event(EventKind::Arrived,b.id,-1,0,0,landing)||!capture_presence_changed())return false;}}
        b.walked=b.cast_used=false;b.m=b.a=b.c=!b.stun_present&&b.channel==Channel::None;w_.phase=Phase::ActorAction;return true;
    }
    bool actor_end(unsigned a){auto& b=w_.bodies[a];for(auto& c:b.cooldown)if(c)--c;
        if(b.shield_ticks)--b.shield_ticks;if(b.stun_ticks)--b.stun_ticks;if(b.channel!=Channel::None&&b.channel_ticks)--b.channel_ticks;
        b.m=b.a=b.c=false;b.completed=true;if(b.alive){if(!cb_.spawn)return stop(Reason::MissingContract);
            const auto on=cb_.spawn(cb_.context,w_,a,b.pose);if(on==Verdict::Unknown)return stop(Reason::MissingContract);
            if(on==Verdict::Allowed){const Scalar before=b.hp,amount=fp_mul(b.max_hp,w_.rules.spawn_healing);
                b.hp=std::min(b.max_hp,fp_add(b.hp,amount));const Scalar delta=fp_sub(b.hp,before);
                if(delta>0&&!event(EventKind::SpawnHeal,b.id,-1,delta))return false;}}
        return next_slot();
    }
    bool next_slot(){if(++w_.slot==w_.slots){++w_.round;w_.phase=Phase::RoundStart;}else w_.phase=slot().controlled?Phase::ActorStart:Phase::Reply;return true;}
    bool reply(){if(!cb_.reply)return stop(Reason::Reply);const auto r=cb_.reply(cb_.context,w_,slot().external_id);
        if(!r.complete||r.domain!=out_.domain.reply||r.count>r.effects.size())return stop(Reason::Reply);
        for(unsigned n=0;n<r.count;++n){const auto& e=r.effects[n];unsigned a=w_.actors;for(unsigned k=0;k<w_.actors;++k)if(w_.bodies[k].id==e.target)a=k;
            if(e.kind==EffectKind::ExternalMove||e.kind==EffectKind::ExternalDeath){bool found=false;
                for(unsigned k=0;k<w_.externals;++k){if(w_.others[k].id==e.target){found=true;
                    if(e.kind==EffectKind::ExternalMove)w_.others[k].pose=e.pose;else w_.others[k].alive=false;}}
                if(!found)return stop(Reason::Reply);if(!capture_presence_changed())return false;continue;}
            if(a==w_.actors||!finite(e.amount))return stop(Reason::Reply);auto& b=w_.bodies[a];
            if(e.kind==EffectKind::Damage){if(!hurt(a,e.amount))return false;}
            else if(e.kind==EffectKind::Stun){if(e.ticks>1)return stop(Reason::MissingContract);if(b.alive&&e.ticks){b.stun_present=true;b.stun_ticks=std::max(b.stun_ticks,e.ticks);
                    b.m=b.a=b.c=false;b.channel=Channel::None;b.channel_ticks=0;if(!event(EventKind::Stun,b.id,-1,0,e.ticks))return false;}}
            else if(e.kind==EffectKind::Heal)b.hp=std::min(b.max_hp,fp_add(b.hp,e.amount));
            else if(e.kind==EffectKind::Shield){if(b.shield_present)return stop(Reason::MissingContract);b.shield_present=true;b.shield=e.amount;b.shield_ticks=e.ticks;}
            else if(e.kind==EffectKind::CustodyLoss){b.bag=0;out_.bodies[a].custody_lost=true;return stop(Reason::CustodyLost,Status::Refuted);}
            else if(e.kind==EffectKind::BankLock&&b.channel==Channel::Bank){b.channel=Channel::None;b.channel_ticks=0;if(!event(EventKind::BankInterrupted,b.id))return false;}
        }
        return next_slot();
    }
    bool finish(){for(unsigned a=0;a<w_.actors;++a)if(!p_.bodies[a].required&&!w_.bodies[a].alive)return stop(Reason::UnsupportedDeath);
        bool tails=true;for(unsigned a=0;a<w_.actors;++a)if(p_.bodies[a].required)
            tails=tails&&stage_[a]==Stage::Done&&!w_.bodies[a].bag&&w_.bodies[a].channel==Channel::None&&out_.bodies[a].renewed&&
                (!p_.bodies[a].require_useful_support||out_.bodies[a].useful_support_words>0);
        // A renewed hit is not a free recovery certificate for later exposure,
        // nor may a newly cleared second generation lose its capture tail.
        for(unsigned a=0;a<w_.actors;++a)if(p_.bodies[a].required)
            tails=tails&&w_.bodies[a].hp==w_.bodies[a].max_hp&&!w_.bodies[a].stun_present;
        tails=tails&&w_.group.life==Life::Alive;
        out_.terminal=w_;out_.physical_complete=true;out_.service_tail=tails&&out_.domain.service_tail_closed?Tail::Complete:Tail::Unknown;
        out_.displaced_service_comparable=out_.domain.other_services_represented;
        for(unsigned a=0;a<w_.actors;++a)if(p_.bodies[a].required&&!w_.bodies[a].gold_known)out_.displaced_service_comparable=false;
        out_.status=out_.service_tail==Tail::Complete?Status::Complete:Status::Unknown;out_.reason=out_.service_tail==Tail::Complete?Reason::None:Reason::Unfinished;return true;
    }
public:
    // Fixed-capacity copy, bounded by 512 route records, 18 legs, 24 purchases;
    // the caller must qualify/admit this separate initialization interval.
    bool begin(const World& initial,const Program& program,const Domain& domain,const Callbacks& callbacks){
        w_=initial;cb_=callbacks;out_.status=Status::Building;out_.service_tail=Tail::Unknown;out_.reason=Reason::None;out_.termination=Termination::None;
        out_.event_count=out_.quanta=out_.last_call_quanta=0;out_.physical_complete=out_.displaced_service_comparable=false;
        out_.domain=domain;out_.initial_round=w_.round;out_.horizon=w_.horizon;out_.program=program.id;out_.initial_fingerprint=0;out_.bodies={};ops_=0;
        bought_={};route_cursor_={};healed_this_input_={};
        if(w_.round<0||w_.horizon>1000000000||w_.horizon<w_.round||w_.horizon-w_.round>512||!w_.actors||w_.actors>actor_limit||!w_.npcs||w_.npcs>npc_limit||
           w_.externals>external_limit||!w_.slots||w_.slots>slot_limit||w_.slot>=w_.slots||program.route_count>route_limit||program.leg_count>leg_limit||program.purchase_count>purchase_limit)
            return stop(Reason::Capacity);
        if(w_.rules.winning_gems&&w_.team_score>=w_.rules.winning_gems){out_.termination=Termination::ModeledWin;return stop(Reason::EngineTerminated);}
        for(unsigned a=0;a<w_.actors;++a)if(program.bodies[a].required&&!w_.bodies[a].alive)return stop(Reason::Dead,Status::Refuted);
        if(!domain.initial_world||w_.revision!=domain.initial_world||!domain.physics||!domain.reply||!domain.services||!domain.physics_closed||!domain.replies_closed||!domain.local_sources_closed)return stop(Reason::MissingContract);
        if(!w_.rules.queried||!w_.rules.capture_ticks||w_.rules.capture_ticks>512||!w_.rules.sleep_batches||w_.rules.sleep_batches>512||
           !w_.rules.respawn_delay||w_.rules.respawn_delay>512||
           !finite(w_.rules.bank_multiplier)||w_.rules.bank_multiplier<=0||!finite(w_.rules.spawn_healing)||w_.rules.spawn_healing>1)return stop(Reason::MissingContract);
        if(!w_.rules.max_rounds||!w_.rules.winning_gems||w_.rules.passive_gold)return stop(Reason::MissingContract);
        if(unsigned(w_.horizon)>=w_.rules.max_rounds){out_.termination=Termination::RoundBoundary;return stop(Reason::EngineTerminated);}
        if(w_.group.life!=Life::Alive||!w_.group.roster_complete)return stop(Reason::MissingObservation);
        for(unsigned a=0;a<w_.actors;++a){const auto& b=w_.bodies[a];stage_[a]=program.bodies[a].initial;
            if(!b.alive&&!program.bodies[a].required)return stop(Reason::UnsupportedDeath);
            if(b.id<0)return stop(Reason::Order);
            for(unsigned k=0;k<a;++k)if(w_.bodies[k].id==b.id)return stop(Reason::Order);
            if(unsigned(b.kit)>unsigned(Kit::Vendetta)||!b.actual||!b.bag_known||!b.alive||!finite(b.hp)||!finite(b.max_hp)||b.hp<=0||b.hp>b.max_hp||!finite(b.strength)||!finite(b.focus)||!finite(b.shield)||
               (b.gold_known&&!finite(b.gold))||!finite(b.capital_floor)||(b.gold_known&&b.capital_floor>b.gold)||!finite(b.camp_multiplier)||!finite(b.taken_multiplier)||!finite(b.npc_taken_multiplier)||!finite(b.gold_multiplier)||!finite(b.shield_modifier)||!finite(b.heal_modifier)||!point(b.pose)||b.reset_slot>1||b.shield_slot>1||b.heal_slot>1||
               program.bodies[a].purchase_begin+program.bodies[a].purchase_count>program.purchase_count)return stop(Reason::Invalid);}
        bool living=false;for(unsigned n=0;n<w_.npcs;++n){const auto& c=w_.camps[n];living=living||c.hp>0;
            if(!c.actual||c.unsupported_effects||!finite(c.hp)||!finite(c.max_hp)||!finite(c.shield)||c.max_hp<=0||c.hp>c.max_hp||!finite(c.strength)||!point(c.pose)||(n&&w_.camps[n-1].id>=c.id))return stop(Reason::MissingObservation);}
        if(!living)return stop(Reason::MissingObservation);
        for(unsigned n=0;n<w_.externals;++n){const auto& e=w_.others[n];
            if(!e.actual&&!(domain.conditional&&domain.assumptions&&e.conditional_placement&&e.placement_revision))return stop(Reason::MissingObservation);
            if(e.id<0)return stop(Reason::Order);
            for(unsigned a=0;a<w_.actors;++a)if(w_.bodies[a].id==e.id)return stop(Reason::Order);
            for(unsigned k=0;k<n;++k)if(w_.others[k].id==e.id)return stop(Reason::Order);}
        for(const auto* order:{&w_.even,&w_.odd}){unsigned seen=0;std::array<int,slot_limit> ids;ids.fill(-1);
            for(unsigned n=0;n<w_.slots;++n){const auto& s=(*order)[n];int id=s.external_id;
                if(s.controlled){const auto a=s.actor;if(a>=w_.actors||(seen&(1u<<a)))return stop(Reason::Order);seen|=1u<<a;id=w_.bodies[a].id;}
                if(id>=0){for(unsigned k=0;k<n;++k)if(ids[k]==id)return stop(Reason::Order);}
                ids[n]=id;}
            if(seen!=(1u<<w_.actors)-1u)return stop(Reason::MissingContract);}
        if((w_.phase==Phase::ActorStart||w_.phase==Phase::ActorAction||w_.phase==Phase::ActorEnd)&&!slot().controlled)return stop(Reason::Date);
        p_.route_count=program.route_count;p_.leg_count=program.leg_count;p_.purchase_count=program.purchase_count;p_.id=program.id;p_.bodies=program.bodies;
        for(unsigned n=0;n<p_.route_count;++n){const auto& step=program.route[n];
            if(!point(step.destination)||step.earliest_round<0||step.latest_round<step.earliest_round||step.latest_round>1000000000)return stop(Reason::Date);
            p_.route[n]=step;}for(unsigned n=0;n<p_.leg_count;++n)p_.legs[n]=program.legs[n];
        for(unsigned n=0;n<p_.purchase_count;++n)p_.purchases[n]=program.purchases[n];out_.initial_fingerprint=fingerprint();return true;
    }
    Status advance(unsigned quanta){quanta=std::min(quanta,call_quanta_limit);out_.last_call_quanta=0;while(out_.status==Status::Building&&out_.last_call_quanta<quanta){
            if(cb_.work&&!cb_.work(cb_.context,1))break;++out_.last_call_quanta;++out_.quanta;
            if(!required_alive())break;bool okay=false;
            switch(w_.phase){case Phase::RoundStart:okay=round_start();break;case Phase::Capture:okay=capture();break;case Phase::Npc:okay=npc();break;
            case Phase::ActorStart:okay=actor_start(slot().actor);break;case Phase::ActorAction:okay=input_action(slot().actor);break;
            case Phase::ActorEnd:okay=actor_end(slot().actor);break;case Phase::Reply:okay=reply();break;case Phase::Finish:okay=finish();break;}
            if(!okay)break;}
        return out_.status;}
    const Result& result()const{return out_;}
    const World& world()const{return w_;}
};
} // namespace wb::capital::service_forecast
