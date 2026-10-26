#pragma once
// Pure, response-free proposals over one immutable observation frame. The actuator
// must issue one primitive, inspect its real response, then capture a new revision.
#include "frame.h"
#include "spatial_support.h"
#include "observation_domain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace mosaic::transition {
using Pos = cambc::vec2;
enum class Kind { wait, move, basic, cast, reset, recall, tribute };
enum class TargetKind { none, camp, enemy, self, ground };
constexpr std::uint32_t mask(Kind kind) { return 1u << static_cast<unsigned>(kind); }
inline constexpr std::uint32_t all_kinds = (1u << 7) - 1;
struct Command {
    Kind kind{Kind::wait};
    TargetKind target_kind{TargetKind::none};
    int target_id{-1}, slot{-1};
    Pos destination{};
    std::uint64_t expected_revision{};
    int expected_epoch{-1}, expected_actor{-1};
    int target_index{-1}; // Optional checked acceleration; raw handle remains authoritative.
};
enum Flag : std::uint64_t {
    stale = 1ull << 0, unavailable = 1ull << 1, wrong_target = 1ull << 2,
    out_of_range = 1ull << 3, exclusive_move = 1ull << 4,
    reset_requires_spent_attack = 1ull << 5, unsupported_kind = 1ull << 6,
    unknown_damage = 1ull << 7, unknown_wake = 1ull << 8,
    unknown_foe_actions = 1ull << 9, short_shield_ignored = 1ull << 10,
    unsafe_prefix = 1ull << 11, truncated = 1ull << 12,
    conditional_service = 1ull << 13, unknown_cc_timing = 1ull << 14,
    outside_domain = 1ull << 15
};
struct Constraints {
    int group{-1};
    std::uint32_t allowed_kinds{all_kinds};
    bool require_cover{true}, require_exact_effect{false};
    bool allow_unmodelled_foes{false}, credit_short_shield{false}, credit_cc{false};
    bool service_authorized{false};
    float hp_reserve{};
    int camp_volleys{1};
    std::size_t max_candidates{32};
};
struct CampEffect { int handle{-1}; float before_hp{}, after_hp{}; int stun_ticks{}; };
enum class EventKind { damage, heal, set_shield, expire_shield, set_max_hp };
struct Event { EventKind kind{EventKind::damage}; float amount{}; };
struct Prefix {
    float hp{}, max_hp{}, shield{}, max_prefix_loss{};
    bool alive{}, finite{true};
};
// Each arithmetic operation is rounded to float, matching the declared restricted
// float32 event model. This is not a model of an unrepresented passive or enemy cast.
inline float rounded(float value) { volatile float out=value; return out; }
inline void prefix_event(Prefix& out,float initial,const Event& event) {
    if(!out.alive)return;
    if(!std::isfinite(event.amount)||event.amount<0){out.finite=false;out.alive=false;return;}
    switch(event.kind) {
    case EventKind::damage: {
        const float blocked=std::min(out.shield,event.amount);
        out.shield=rounded(out.shield-blocked);
        const float loss=rounded(event.amount-blocked);
        out.hp=std::max(0.0f,rounded(out.hp-loss));break;
    }
    case EventKind::heal:out.hp=std::min(out.max_hp,rounded(out.hp+event.amount));break;
    case EventKind::set_shield:out.shield=event.amount;break;
    case EventKind::expire_shield:out.shield=0;break;
    case EventKind::set_max_hp:out.max_hp=event.amount;out.hp=std::min(out.hp,out.max_hp);break;
    }
    out.max_prefix_loss=std::max(out.max_prefix_loss,rounded(initial-out.hp));out.alive=out.hp>0;
}
inline Prefix prefixes(float hp, float max_hp, float shield, std::span<const Event> events) {
    Prefix out{hp,max_hp,shield,0,hp>0,true};
    if(!std::isfinite(hp)||!std::isfinite(max_hp)||!std::isfinite(shield)||hp<0||max_hp<0||shield<0) {
        out.finite=false;out.alive=false;return out;
    }
    const float initial=hp;
    for(const auto& event:events)prefix_event(out,initial,event);
    return out;
}
struct Evaluation {
    Command command;
    bool admissible{}, supported{true}, effect_exact{true}, forecast_complete{true};
    bool survives_prefix{}, eligible{};
    std::uint64_t flags{};
    frame::Self after_self;
    std::vector<CampEffect> camps;
    std::vector<int> wake_groups;
    float useful_hp_removed{}, followup_hp_removed{}, next_volley{}, max_prefix_loss{};
    float hp_after_prefix{};
};
struct Candidates { std::vector<Evaluation> values; bool complete{true}; };
inline int distance(Pos a,Pos b) {return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
inline bool contains(std::span<const Pos> values,Pos value) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}
inline void wake(Evaluation& out,int group) {
    if(std::find(out.wake_groups.begin(),out.wake_groups.end(),group)==out.wake_groups.end())out.wake_groups.push_back(group);
}
template<class Self> int splash_radius(const Self& self) {
    if constexpr(requires{self.splash_radius;})return self.splash_radius;
    return self.passive==cambc::passive_type::splash_attack?-1:0;
}
template<class Self> bool damage_known(const Self& self) {
    if constexpr(requires{self.basic_damage_known;})return self.basic_damage_known;
    return false;
}
template<class Self> float basic_damage(const Self& self) {
    if constexpr(requires{self.basic_damage;self.basic_damage_known;})if(self.basic_damage_known)return self.basic_damage;
    return rounded(self.strength*self.camp_multiplier);
}
template<class Self> float camp_taken(const Self& self) {
    if constexpr(requires{self.camp_taken_multiplier;})return self.camp_taken_multiplier;
    return 1;
}
template<class Self> float champion_taken(const Self& self) {
    if constexpr(requires{self.champion_taken_multiplier;})return self.champion_taken_multiplier;
    return 1;
}
template<class Self> bool modifiers_known(const Self& self) {
    if constexpr(requires{self.damage_modifiers_known;})return self.damage_modifiers_known;
    return false;
}
template<class Board> Command command(const frame::Snapshot<Board>& frame,Kind kind) {
    Command out;out.kind=kind;out.expected_epoch=frame.epoch;out.expected_actor=frame.actor;out.expected_revision=frame.revision;return out;
}
inline const frame::Ability* ability(const std::array<frame::Ability,2>& abilities,int slot) {
    for(const auto& a:abilities)if(a.slot==slot)return &a;
    return nullptr;
}
inline const CampEffect* effect(const Evaluation& out,int handle) {
    for(const auto& e:out.camps)if(e.handle==handle)return &e;
    return nullptr;
}
template<class Board> const frame::Camp* find_camp(const frame::Snapshot<Board>& frame,int handle) {
    if(handle>=0&&static_cast<std::size_t>(handle)<frame.camps.size()&&frame.camps[handle].handle==handle)return &frame.camps[handle];
    return frame.camp(handle);
}
template<class Board> const frame::Group* find_group(const frame::Snapshot<Board>& frame,int id) {
    if(id>=0&&static_cast<std::size_t>(id)<frame.groups.size()&&frame.groups[id].id==id)return &frame.groups[id];
    return frame.group(id);
}
template<class Board> const frame::Camp* command_camp(const frame::Snapshot<Board>& frame,const Command& command) {
    if(command.target_index>=0&&static_cast<std::size_t>(command.target_index)<frame.camps.size()&&
        frame.camps[command.target_index].handle==command.target_id)return &frame.camps[command.target_index];
    return find_camp(frame,command.target_id);
}
template<class Board> bool current_domain(const frame::Snapshot<Board>& frame) {
    if constexpr(requires{frame.domain;}) {
        if(!frame.domain.limited)return frame.domain.complete;
        return frame.domain.matches({frame.epoch,frame.actor,frame.revision},frame.spatial_support,frame.camps.size(),
            domain::geometry(frame.self,frame.abilities));
    }
    return true;
}
template<class Board> bool domain_member(const frame::Snapshot<Board>& frame,std::size_t index) {
    if constexpr(requires{frame.domain;})return frame.domain.contains(index);
    return true;
}
template<class Board> bool domain_hits(const frame::Snapshot<Board>& frame,Pos center,int radius) {
    if constexpr(requires{frame.domain;})return frame.domain.covers_hits(center,radius);
    return true;
}
template<class Board> bool domain_threats(const frame::Snapshot<Board>& frame,Pos after_position) {
    if constexpr(requires{frame.domain;})return frame.domain.covers_threats(after_position);
    return true;
}
enum class ContextMode { indexed, direct, spatial };
// Cached geometry and immutable per-revision facts. The hit cone is a proven
// superset by the Chebyshev triangle inequality; a move can only select one of
// the captured destinations. No affected member or possible nearby threat is
// removed by an arbitrary count cap.
template<class Board> struct PlanningContext {
    struct Row {int group_slot{-1};float strength{},loss{};bool active{},fresh_dead{};};
    // The pinned 40-pool + 17-legacy map census bounds these at 160/51.
    // Larger frames remain explicitly unsupported rather than being truncated.
    static constexpr std::size_t camp_capacity=160,group_capacity=51;
    struct Indices {
        std::array<std::size_t,camp_capacity> values;
        std::size_t used{};
        bool consecutive{};
        struct Iterator {
            const std::size_t* values{};std::size_t offset{};
            std::size_t operator*() const{return values?values[offset]:offset;}
            Iterator& operator++(){++offset;return *this;}
            bool operator!=(const Iterator& other) const{return offset!=other.offset;}
        };
        void clear(){used=0;consecutive=false;}
        void all(std::size_t count){used=count;consecutive=true;}
        void select(const spatial::Selection& selection){clear();for(auto i:selection)push_back(i);}
        void push_back(std::size_t index){values[used++]=index;}
        std::size_t size() const{return used;}
        bool empty() const{return used==0;}
        Iterator begin() const{return {consecutive?nullptr:values.data(),0};}
        Iterator end() const{return {consecutive?nullptr:values.data(),used};}
        std::size_t operator[](std::size_t index) const{return consecutive?index:values[index];}
    };
    struct Rows {
        const frame::Snapshot<Board>* source{};
        std::size_t size() const{return source?source->camps.size():0;}
        Row operator[](std::size_t index) const {
            const auto& frame=*source;const auto& c=frame.camps[index];const auto* g=find_group(frame,c.group);
            Row row;row.group_slot=g?int(g-frame.groups.data()):-1;
            row.fresh_dead=g&&g->observed_round==frame.epoch&&
                (g->state==frame::GroupState::reward||g->state==frame::GroupState::respawning);
            row.active=c.awake||!c.known||(g&&g->possibly_woken);
            row.strength=c.known?c.strength:std::max(c.strength,c.design_strength);
            row.loss=rounded(std::max(0.0f,row.strength)*camp_taken(frame.self));return row;
        }
    };
    const frame::Snapshot<Board>* source{};
    int epoch{-1},actor{-1};std::uint64_t revision{};
    bool capacity_supported{true};
    ContextMode mode{ContextMode::indexed};
    Rows rows;
    Indices hit_indices,threat_indices,basic_targets;
    std::array<int,camp_capacity> effect_slots;
    std::array<std::uint32_t,camp_capacity> effect_epochs{};
    std::array<std::uint32_t,group_capacity> wake_epochs{};
    std::uint32_t serial{};
    explicit PlanningContext(const frame::Snapshot<Board>& frame,ContextMode selected=ContextMode::indexed):mode(selected) {reset(frame);}
    void reset(const frame::Snapshot<Board>& frame) {
        source=&frame;epoch=frame.epoch;actor=frame.actor;revision=frame.revision;
        rows.source=&frame;hit_indices.clear();threat_indices.clear();basic_targets.clear();
        capacity_supported=frame.camps.size()<=camp_capacity&&frame.groups.size()<=group_capacity;
        if(!capacity_supported)return;
        begin();
        // A selected primitive does not need an eagerly built geometry index.
        // Its evaluator filters these lazy source-order ranges exactly once.
        if(mode!=ContextMode::indexed) {
            hit_indices.all(frame.camps.size());threat_indices.all(frame.camps.size());basic_targets.all(frame.camps.size());return;
        }
        const auto& self=frame.self;
        float reach=float(self.range+std::max(0,splash_radius(self)));
        for(const auto& a:frame.abilities)if(a.ready&&(a.damage||a.aoe||a.cc)) {
            if(!std::isfinite(a.range)||!std::isfinite(a.radius))reach=std::numeric_limits<float>::infinity();
            else reach=std::max(reach,a.range+(a.aoe?std::max(0.0f,a.radius):0.0f));
        }
        int displacement=0;for(auto p:frame.move_destinations)displacement=std::max(displacement,distance(self.pos,p));
        for(std::size_t i=0;i<frame.camps.size();++i) {
            const auto& c=frame.camps[i];const int d=distance(self.pos,c.pos);
            const bool alive=c.known?c.alive:c.possible_alive;
            if((!c.known||c.alive)&&float(d)<=reach)hit_indices.push_back(i);
            if(alive&&d<=c.range+displacement)threat_indices.push_back(i);
            if(c.visible&&c.known&&c.alive&&c.hp>0&&d<=self.range)basic_targets.push_back(i);
        }
    }
    bool current() const {return source&&source->complete&&source->epoch==epoch&&source->actor==actor&&source->revision==revision;}
    const spatial::Index* spatial_index() const {
        if(mode!=ContextMode::spatial)return nullptr;
        if constexpr(requires{source->spatial_support;})
            if(source->spatial_support.valid(source->camps.size()))return source->spatial_support.index;
        return nullptr;
    }
    void select_hits(Pos center,int radius) {
        if(mode==ContextMode::indexed)return;
        hit_indices.all(source->camps.size());
        if(const auto* index=spatial_index()) {
            const auto selected=index->hits(center.x,center.y,radius);
            if(selected.complete)hit_indices.select(selected);
        }
    }
    void select_threats(Pos center) {
        if(mode==ContextMode::indexed)return;
        threat_indices.all(source->camps.size());
        if(const auto* index=spatial_index()) {
            const auto selected=index->threats(center.x,center.y);
            if(selected.complete)threat_indices.select(selected);
        }
    }
    void select_basics() {
        if(mode==ContextMode::indexed)return;
        basic_targets.all(source->camps.size());
        if(const auto* index=spatial_index()) {
            const auto selected=index->hits(source->self.pos.x,source->self.pos.y,source->self.range);
            if(selected.complete)basic_targets.select(selected);
        }
    }
    void begin() {
        if(++serial==0){std::fill(effect_epochs.begin(),effect_epochs.end(),0);std::fill(wake_epochs.begin(),wake_epochs.end(),0);serial=1;}
    }
    void mark_wake(Evaluation& out,std::size_t camp) {
        const auto* g=find_group(*source,source->camps[camp].group);
        const int slot=g?int(g-source->groups.data()):-1;
        if(slot<0)wake(out,source->camps[camp].group);
        else if(wake_epochs[slot]!=serial){wake_epochs[slot]=serial;out.wake_groups.push_back(source->camps[camp].group);}
    }
    bool woke(const Evaluation& out,std::size_t camp) const {
        const auto* g=find_group(*source,source->camps[camp].group);
        const int slot=g?int(g-source->groups.data()):-1;
        return slot>=0?wake_epochs[slot]==serial:
            std::find(out.wake_groups.begin(),out.wake_groups.end(),source->camps[camp].group)!=out.wake_groups.end();
    }
    const CampEffect* changed(const Evaluation& out,std::size_t camp) const {
        return effect_epochs[camp]==serial?&out.camps[effect_slots[camp]]:nullptr;
    }
};
template<class Board> Evaluation evaluate(PlanningContext<Board>& context,const Command& input,const Constraints& constraints={}) {
    const auto& frame=*context.source;
    Evaluation out;out.command=input;out.after_self=frame.self;
    auto reject=[&](Flag flag) {out.flags|=flag;out.admissible=false;return out;};
    if(!context.current()||!frame.matches(input.expected_epoch,input.expected_actor,input.expected_revision))return reject(stale);
    if(!context.capacity_supported){out.supported=false;out.flags=truncated;return out;}
    if(!current_domain(frame)){out.supported=false;out.forecast_complete=false;return reject(outside_domain);}
    context.begin();
    if(!(constraints.allowed_kinds&mask(input.kind)))return reject(unavailable);
    const auto& self=frame.self;
    if(!self.alive)return reject(unavailable);
    if(input.kind!=Kind::wait && (self.stunned||self.recalling||self.submitting||self.teleporting))return reject(unavailable);
    out.admissible=true;
    const frame::Camp* target=nullptr;
    if(input.target_kind==TargetKind::camp) {
        target=command_camp(frame,input);
        if(!target||!target->visible||!target->known||!target->alive||!std::isfinite(target->hp)||target->hp<=0)return reject(wrong_target);
        if(!domain_member(frame,std::size_t(target-frame.camps.data()))){out.supported=false;out.forecast_complete=false;return reject(outside_domain);}
        if(constraints.group>=0&&target->group!=constraints.group)return reject(wrong_target);
    }
    float hit=0;int radius=0,stun=0;bool damages=false;
    const frame::Ability* cast=nullptr;
    switch(input.kind) {
    case Kind::wait: break;
    case Kind::move:
        if(!self.can_move||input.destination==self.pos)return reject(unavailable);
        if(!contains(frame.move_destinations,input.destination))return reject(wrong_target);
        out.after_self.pos=input.destination;out.after_self.moved=true;out.after_self.can_move=false;
        if(self.range>1)out.after_self.can_attack=false;
        break;
    case Kind::basic:
        if(!self.can_attack)return reject(unavailable);
        if(input.target_kind!=TargetKind::camp||!target) {out.supported=false;return reject(unsupported_kind);}
        if(distance(self.pos,target->pos)>self.range)return reject(out_of_range);
        hit=basic_damage(self);radius=splash_radius(self);damages=true;
        if(!damage_known(self)){out.effect_exact=false;out.flags|=unknown_damage;}
        if(radius<0){out.supported=false;out.forecast_complete=false;out.flags|=unknown_wake;radius=0;}
        out.after_self.can_attack=false;out.after_self.attacked=true;
        if(self.range>1)out.after_self.can_move=false;
        break;
    case Kind::reset:
    case Kind::cast:
        cast=ability(frame.abilities,input.slot);
        if(!cast||!cast->ready||cast->cooldown!=0||!self.can_cast||self.cast_spent)return reject(unavailable);
        if(cast->dash){out.supported=false;return reject(unsupported_kind);}
        if(cast->exclusive&&(self.moved||!self.can_move))return reject(exclusive_move);
        if(cast->reset) {
            if(self.can_attack)return reject(reset_requires_spent_attack);
            out.after_self.can_attack=true;
            if(cast->enhanced){out.effect_exact=false;out.flags|=unknown_damage;}
            // This is a continuation opportunity, not an applied second basic.
            context.select_basics();
            for(const auto i:context.basic_targets) {
                const auto& c=frame.camps[i];
                if(!domain_member(frame,i))continue;
                if(!c.visible||!c.known||!c.alive||c.hp<=0||distance(self.pos,c.pos)>self.range)continue;
                if(constraints.group<0||c.group==constraints.group)
                    out.followup_hp_removed=std::max(out.followup_hp_removed,std::min(c.hp,basic_damage(self)));
            }
        } else if(input.kind==Kind::reset) return reject(wrong_target);
        else if(cast->shield&&input.target_kind==TargetKind::self&&cast->self) {
            if(self.shield>0){out.supported=false;out.flags|=unsupported_kind;}
            out.after_self.shield=cast->shield_amount;out.after_self.shield_duration=cast->duration;
        } else if(cast->damage||cast->aoe||cast->cc) {
            if(!std::isfinite(cast->range)||cast->range<0||!std::isfinite(cast->radius)||cast->radius<0||
                double(cast->radius)>double(std::numeric_limits<int>::max())) {
                out.supported=false;out.forecast_complete=false;return reject(unsupported_kind);
            }
            if(input.target_kind==TargetKind::ground) {
                if(!cast->ground)return reject(wrong_target);
                if(distance(self.pos,input.destination)>cast->range)return reject(out_of_range);
                const auto* anchor=command_camp(frame,input);
                const bool known_anchor=anchor&&anchor->known&&anchor->visible&&anchor->alive&&anchor->pos==input.destination;
                if(!contains(cast->legal_targets,input.destination)&&!known_anchor) {
                    out.supported=false;out.flags|=unsupported_kind;
                }
                if(constraints.group>=0&&!cast->camps){out.supported=false;out.flags|=wrong_target;}
            } else {
                if(input.target_kind!=TargetKind::camp||!target||!cast->camps)return reject(wrong_target);
                if(distance(self.pos,target->pos)>cast->range)return reject(out_of_range);
            }
            hit=cast->damage_amount;radius=cast->aoe?int(cast->radius):0;stun=cast->cc?cast->duration:0;damages=true;
        } else {out.supported=false;out.flags|=unsupported_kind;}
        out.after_self.can_cast=false;out.after_self.cast_spent=true;
        if(cast->exclusive)out.after_self.can_move=false;
        break;
    case Kind::recall:
    case Kind::tribute:
        out.flags|=conditional_service;
        if(!constraints.service_authorized){out.supported=false;out.flags|=unsupported_kind;}
        if(input.kind==Kind::tribute&&self.gems<=0)return reject(unavailable);
        break;
    }
    if(!domain_threats(frame,out.after_self.pos)){out.supported=false;out.forecast_complete=false;return reject(outside_domain);}
    if(damages) {
        if(!std::isfinite(hit)||hit<0){out.supported=false;out.effect_exact=false;out.flags|=unknown_damage;return out;}
        const Pos center=input.target_kind==TargetKind::ground?input.destination:target->pos;
        if(!domain_hits(frame,center,radius)){out.supported=false;out.forecast_complete=false;return reject(outside_domain);}
        context.select_hits(center,radius);
        const auto apply_hit=[&](std::size_t i) {
            const auto& c=frame.camps[i];
            const bool primary=target&&c.handle==target->handle;
            if(!primary&&(input.target_kind==TargetKind::ground?
                distance(center,c.pos)>radius:(radius<=0||distance(center,c.pos)>radius)))return;
            if(c.known&&!c.alive)return;
            context.mark_wake(out,i);
            if(!c.known){out.effect_exact=false;out.flags|=unknown_wake;return;}
            // A current shield absorbs damage before HP. Missing buff observations
            // cannot certify a lethal hit and removal of the next camp threat.
            float absorbed=0;
            if(!c.buffs_known||!std::isfinite(c.shield)||c.shield<0) {
                out.effect_exact=false;out.flags|=unknown_damage;
            } else absorbed=std::min(c.shield,hit);
            const float hp=std::max(0.0f,rounded(c.hp-rounded(hit-absorbed)));
            context.effect_slots[i]=int(out.camps.size());context.effect_epochs[i]=context.serial;
            out.camps.push_back({c.handle,c.hp,hp,stun});
            if(constraints.group<0||c.group==constraints.group)out.useful_hp_removed=rounded(out.useful_hp_removed+rounded(c.hp-hp));
        };
        if(target&&radius<=0&&input.target_kind!=TargetKind::ground)apply_hit(std::size_t(target-frame.camps.data()));
        else for(const auto i:context.hit_indices)apply_hit(i);
    }
    float shield=out.after_self.shield;
    if(shield>0&&out.after_self.shield_duration<=1&&!constraints.credit_short_shield) {
        shield=0;out.flags|=short_shield_ignored;
    }
    // The automatic-phase forecast covers one next camp phase only. Longer
    // horizons require an explicit ordered schedule (including expiry/respawn),
    // which callers can supply to prefixes; repeating this phase is not a proof.
    if(constraints.camp_volleys!=1){out.forecast_complete=false;out.flags|=unsupported_kind;}
    auto forecast=prefixes(out.after_self.hp,out.after_self.max_hp,shield,{});
    context.select_threats(out.after_self.pos);
    for(int volley=0;volley<std::clamp(constraints.camp_volleys,0,1);++volley)for(const auto i:context.threat_indices) {
        const auto& c=frame.camps[i];
        if(!(c.known?c.alive:c.possible_alive)||distance(out.after_self.pos,c.pos)>c.range)continue;
        const auto* changed=context.changed(out,i);
        // An uncertain fallback damage estimate cannot certify removal of a threat.
        const bool alive=changed&&out.effect_exact?changed->after_hp>0:(c.known?c.alive:c.possible_alive);
        if(!alive)continue;
        const auto row=context.rows[i];if(row.fresh_dead)continue;
        const bool active=row.active||context.woke(out,i);
        if(!active)continue;
        const float strength=row.strength;
        if(!std::isfinite(strength)||strength<0){out.forecast_complete=false;out.flags|=unknown_damage;continue;}
        if(!c.known){out.flags|=unknown_wake;if(strength>0)out.forecast_complete=false;}
        if(changed&&changed->stun_ticks>0&&volley==0) {
            if(constraints.credit_cc&&out.effect_exact)continue;
            out.flags|=unknown_cc_timing;
        }
        const float loss=row.loss;
        if(loss>0&&!modifiers_known(self)){out.forecast_complete=false;out.flags|=unknown_damage;}
        if(volley==0)out.next_volley=rounded(out.next_volley+loss);
        if(loss>0)prefix_event(forecast,out.after_self.hp,{EventKind::damage,loss});
    }
    for(const auto& foe:frame.enemies)if(foe.visible&&foe.alive) {
        out.flags|=unknown_foe_actions;
        if(!constraints.allow_unmodelled_foes)out.forecast_complete=false;
        // Diagnostic basic-reach term only, never an upper bound on unknown spells.
        if(distance(out.after_self.pos,foe.pos)<=foe.range+foe.move_speed) {
            const float loss=rounded(std::max(0.0f,foe.strength)*champion_taken(self));
            out.next_volley=rounded(out.next_volley+loss);if(loss>0)prefix_event(forecast,out.after_self.hp,{EventKind::damage,loss});
        }
    }
    out.max_prefix_loss=forecast.max_prefix_loss;out.hp_after_prefix=forecast.hp;
    out.survives_prefix=forecast.alive&&forecast.finite&&forecast.hp>constraints.hp_reserve;
    if(!out.survives_prefix)out.flags|=unsafe_prefix;
    out.eligible=out.admissible&&out.supported&&(!constraints.require_exact_effect||out.effect_exact)&&
        (!constraints.require_cover||(out.forecast_complete&&out.survives_prefix));
    return out;
}
template<class Board> Evaluation evaluate(const frame::Snapshot<Board>& frame,const Command& input,const Constraints& constraints={}) {
    if(!frame.matches(input.expected_epoch,input.expected_actor,input.expected_revision)) {
        Evaluation out;out.command=input;out.after_self=frame.self;out.flags=stale;return out;
    }
    PlanningContext context(frame);return evaluate(context,input,constraints);
}

// Allocation-free task-progress nomination. This is a local damage ordering,
// not a legality, exposure or economic-completion certificate. The nominated
// primitive must still pass evaluate(context, ...), then real actuation.
template<class Board> float nominal_progress(const PlanningContext<Board>& context,const Command& input,int group=-1) {
    const auto& frame=*context.source;const auto& self=frame.self;
    if(!context.current()||!current_domain(frame)||!self.alive||self.stunned||self.recalling||self.submitting||self.teleporting)return 0;
    typename PlanningContext<Board>::Indices all;all.all(frame.camps.size());
    const auto& hits=context.mode==ContextMode::indexed?context.hit_indices:all;
    const auto& basics=context.mode==ContextMode::indexed?context.basic_targets:all;
    const frame::Ability* a=nullptr;const frame::Camp* target=nullptr;
    if(input.target_id>=0)target=command_camp(frame,input);
    float hit=0;int radius=0;
    if(input.kind==Kind::basic) {
        if(input.target_kind!=TargetKind::camp||!self.can_attack||!target||!target->known||!target->visible||!target->alive||target->hp<=0||distance(self.pos,target->pos)>self.range)return 0;
        hit=basic_damage(self);radius=std::max(0,splash_radius(self));
    } else if(input.kind==Kind::cast||input.kind==Kind::reset) {
        a=ability(frame.abilities,input.slot);
        if(!a||!a->ready||a->cooldown||!self.can_cast||self.cast_spent||(a->exclusive&&(self.moved||!self.can_move)))return 0;
        if(a->reset) {
            if(self.can_attack)return 0;
            float best=0;
            for(auto i:basics) {
                const auto& c=frame.camps[i];
                if(!domain_member(frame,i))continue;
                if(!c.visible||!c.known||!c.alive||c.hp<=0||distance(self.pos,c.pos)>self.range||(group>=0&&c.group!=group))continue;
                // A reset enables an attack; it does not itself apply that attack.
                // The current basic allowance is spent, so compute its geometry directly.
                float value=0;const int splash=std::max(0,splash_radius(self));const float amount=basic_damage(self);
                for(auto j:hits) {
                    const auto& other=frame.camps[j];
                    if(!domain_member(frame,j))continue;
                    if(!other.known||!other.alive||other.hp<=0||(group>=0&&other.group!=group))continue;
                    if(other.handle==c.handle||(splash>0&&distance(c.pos,other.pos)<=splash))value=rounded(value+std::min(other.hp,amount));
                }
                best=std::max(best,value);
            }
            return best;
        }
        if(!(a->damage||a->aoe||a->cc)||!a->camps)return 0;
        if(input.target_kind!=TargetKind::camp&&input.target_kind!=TargetKind::ground)return 0;
        const auto center=input.target_kind==TargetKind::ground?input.destination:(target?target->pos:Pos{});
        if(input.target_kind==TargetKind::ground?!a->ground:(!target||!target->known||!target->visible||!target->alive))return 0;
        if(distance(self.pos,center)>a->range)return 0;
        hit=a->damage_amount;radius=a->aoe?int(a->radius):0;
    } else return 0;
    if(!std::isfinite(hit)||hit<0)return 0;
    const auto center=input.target_kind==TargetKind::ground?input.destination:target->pos;
    float value=0;
    for(auto i:hits) {
        const auto& c=frame.camps[i];if(!c.known||!c.alive||c.hp<=0||(group>=0&&c.group!=group))continue;
        if(!domain_member(frame,i))continue;
        const bool primary=target&&target->handle==c.handle;
        if(!primary&&(input.target_kind==TargetKind::ground?distance(center,c.pos)>radius:(radius<=0||distance(center,c.pos)>radius)))continue;
        value=rounded(value+std::min(c.hp,hit));
    }
    return value;
}

template<class Board,class Consumer> void visit_commands(const frame::Snapshot<Board>& frame,const Constraints& constraints,
                                                        const std::optional<Command>& move,Consumer&& add) {
    // Keep the finite defensive/continuation menu even when optional damage search
    // is truncated. There are at most two slots, one move proposal and one wait.
    for(const auto& a:frame.abilities)if(a.ready) {
        if(a.reset){auto reset=command(frame,Kind::reset);reset.slot=a.slot;add(reset,true);}
        if(a.shield&&a.self){auto shield=command(frame,Kind::cast);shield.slot=a.slot;shield.target_kind=TargetKind::self;add(shield,true);}
    }
    add(command(frame,Kind::wait),true);
    if(move)add(*move,true);
    for(std::size_t target_index=0;target_index<frame.camps.size();++target_index) {
        const auto& camp=frame.camps[target_index];
        if(!(camp.visible&&camp.known&&camp.alive&&camp.hp>0&&(constraints.group<0||camp.group==constraints.group)))continue;
        if(frame.self.can_attack) {
            auto basic=command(frame,Kind::basic);basic.target_kind=TargetKind::camp;basic.target_id=camp.handle;basic.target_index=int(target_index);add(basic);
        }
        for(const auto& a:frame.abilities)if(frame.self.can_cast&&!frame.self.cast_spent&&a.ready&&!a.reset&&!a.shield&&(a.damage||a.aoe||a.cc)) {
            auto cast=command(frame,Kind::cast);cast.slot=a.slot;cast.target_id=camp.handle;
            cast.target_kind=a.ground?TargetKind::ground:TargetKind::camp;cast.destination=camp.pos;cast.target_index=int(target_index);add(cast);
        }
    }
}
template<class Board> Candidates enumerate(PlanningContext<Board>& context,const Constraints& constraints={},
                                          const std::optional<Command>& move=std::nullopt) {
    Candidates out;out.values.reserve(std::min<std::size_t>(constraints.max_candidates+4,128));
    auto add=[&](Command cmd,bool essential=false) {
        if(!essential&&out.values.size()>=constraints.max_candidates){out.complete=false;return;}
        auto result=evaluate(context,cmd,constraints);
        if(result.admissible)out.values.push_back(std::move(result));
    };
    visit_commands(*context.source,constraints,move,add);return out;
}
template<class Board> Candidates enumerate(const frame::Snapshot<Board>& frame,const Constraints& constraints={},
                                           const std::optional<Command>& move=std::nullopt) {
    PlanningContext context(frame);return enumerate(context,constraints,move);
}
// This local ordering does not assign macro tasks or declare their economic value.
// It prefers actual current-task HP progress, then an enabled basic continuation,
// then less immediate exposure. The caller can inspect all candidates instead.
inline std::optional<Evaluation> choose(const Candidates& candidates) {
    const Evaluation* best=nullptr;
    for(const auto& candidate:candidates.values)if(candidate.eligible) {
        const float value=candidate.useful_hp_removed+candidate.followup_hp_removed;
        const float prior=best?best->useful_hp_removed+best->followup_hp_removed:-1;
        if(!best||value>prior||(value==prior&&candidate.next_volley<best->next_volley))best=&candidate;
    }
    return best?std::optional<Evaluation>{*best}:std::nullopt;
}
struct StreamChoice {std::optional<Evaluation> best;std::size_t candidates{};bool complete{true};};
template<class Board> StreamChoice choose_stream(PlanningContext<Board>& context,const Constraints& constraints={},
                                                const std::optional<Command>& move=std::nullopt) {
    StreamChoice out;
    auto add=[&](Command cmd,bool essential=false) {
        if(!essential&&out.candidates>=constraints.max_candidates){out.complete=false;return;}
        auto candidate=evaluate(context,cmd,constraints);if(!candidate.admissible)return;++out.candidates;
        if(!candidate.eligible)return;
        const float value=candidate.useful_hp_removed+candidate.followup_hp_removed;
        const float previous=out.best?out.best->useful_hp_removed+out.best->followup_hp_removed:-1;
        if(!out.best||value>previous||(value==previous&&candidate.next_volley<out.best->next_volley))out.best=std::move(candidate);
    };
    visit_commands(*context.source,constraints,move,add);return out;
}
template<class Board> StreamChoice choose_stream(const frame::Snapshot<Board>& frame,const Constraints& constraints={},
                                                const std::optional<Command>& move=std::nullopt) {
    PlanningContext context(frame);return choose_stream(context,constraints,move);
}
template<class Board> std::optional<Evaluation> choose(PlanningContext<Board>& context,const Constraints& constraints={},
                                                      const std::optional<Command>& move=std::nullopt) {
    return choose_stream(context,constraints,move).best;
}
template<class Board> std::optional<Evaluation> choose(const frame::Snapshot<Board>& frame,const Constraints& constraints={},
                                                      const std::optional<Command>& move=std::nullopt) {
    return choose_stream(frame,constraints,move).best;
}
} // namespace mosaic::transition
