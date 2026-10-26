#pragma once
// One requested primitive over a sparse current overlay. Catalog and Index share
// SourceWorld's stable canonical order; the producer owns that association and
// every actual clock/privacy fence. No function in this header queries the SDK.
#include "transition.h"
#include <span>

namespace mosaic::selected {
using Stamp=domain::Stamp;
using Command=transition::Command;
struct Request {Command command;std::uint64_t serial{};};
inline bool same_command(const Command& a,const Command& b) {
    return a.kind==b.kind&&a.target_kind==b.target_kind&&a.target_id==b.target_id&&a.slot==b.slot&&
        a.destination==b.destination&&a.expected_revision==b.expected_revision&&a.expected_epoch==b.expected_epoch&&
        a.expected_actor==b.expected_actor&&a.target_index==b.target_index;
}
inline bool same_request(const Request& a,const Request& b){return a.serial==b.serial&&same_command(a.command,b.command);}
struct Row {
    std::size_t index{};frame::Camp camp;Stamp stamp;
    bool effect{},retaliation_origin{},retaliation_after{};
    bool state_fresh{},record_fresh{},buffs_fresh{};
};
struct Packet {
    Stamp stamp;Request request;std::uint64_t nonce{};
    Stamp origin_stamp;bool origin_complete{};
    spatial::Token layout;
    std::span<const frame::SourceCamp> catalog;
    frame::Self self;
    std::vector<Row> rows;
    std::vector<frame::Group> groups;
    std::vector<frame::Enemy> enemies;
    bool complete{},self_complete{},enemies_complete{},move_test_complete{},move_legal{};
    bool effect_champions_clear{};
    const Row* row(std::size_t index) const {
        for(const auto& value:rows)if(value.index==index)return &value;
        return nullptr;
    }
    bool matches(Stamp expected,const Request& input) const {
        return complete&&stamp.matches(expected)&&same_request(request,input)&&layout.valid(catalog.size())&&
            input.command.expected_epoch==stamp.epoch&&input.command.expected_actor==stamp.actor&&
            input.command.expected_revision==stamp.revision;
    }
};
enum class Status {ready,incomplete,stale,unsupported,rejected};
struct OriginWait {
    bool complete{},safe{};float next_volley{},hp_after_prefix{};std::uint64_t flags{};
};
struct Result {
    Status status{Status::incomplete};transition::Evaluation action;
    OriginWait origin_wait;
};
inline bool same_token(spatial::Token a,spatial::Token b){return domain::same_layout(a,b);}
inline bool supported_kind(transition::Kind kind) {
    return kind==transition::Kind::move||kind==transition::Kind::basic||kind==transition::Kind::wait;
}
inline const frame::Group* group(const Packet& packet,int id) {
    for(const auto& value:packet.groups)if(value.id==id)return &value;
    return nullptr;
}
inline bool resolved(const Packet& packet,int id) {
    const auto* value=group(packet,id);
    return value&&value->observed_round==packet.stamp.epoch&&
        value->state==frame::GroupState::reward;
}
inline bool future_respawn(const Packet& packet,int id) {
    const auto* value=group(packet,id);
    return value&&value->observed_round==packet.stamp.epoch&&value->state==frame::GroupState::respawning;
}
inline bool canonical(const Packet& packet,const Row& row) {
    if(row.index>=packet.catalog.size()||!row.stamp.matches(packet.stamp))return false;
    const auto& design=packet.catalog[row.index].design;
    return row.camp.handle==design.handle&&row.camp.group==design.group&&row.camp.range==design.range&&
        row.camp.spawn==design.spawn&&(!row.camp.known||!row.camp.alive||row.camp.pos==design.spawn);
}
struct RowLookup {
    const Packet& packet;
    std::array<std::size_t,spatial::capacity> slots; // Only present entries are read.
    std::array<std::uint64_t,3> present{};
    bool valid{true};
    explicit RowLookup(const Packet& source):packet(source) {
        if(source.catalog.size()>spatial::capacity){valid=false;return;}
        for(std::size_t slot=0;slot<source.rows.size();++slot) {
            const auto& row=source.rows[slot];
            if(!canonical(source,row)){valid=false;return;}
            const auto bit=std::uint64_t{1}<<(row.index%64);
            if(present[row.index/64]&bit){valid=false;return;}
            present[row.index/64]|=bit;slots[row.index]=slot;
        }
    }
    const Row* row(std::size_t index) const {
        if(!valid||index>=spatial::capacity||!(present[index/64]&(std::uint64_t{1}<<(index%64))))return nullptr;
        return &packet.rows[slots[index]];
    }
};
// Completeness is field-specific: unhit retaliation does not consume camp shield
// or stun observations. Unknown life/strength has no implicit design upper bound.
inline bool current_body(const Row* row,bool hit) {
    return row&&row->state_fresh&&row->camp.known&&(!row->camp.alive||
        (row->record_fresh&&std::isfinite(row->camp.hp)&&row->camp.hp>0&&std::isfinite(row->camp.strength)&&
         row->camp.strength>=0&&(!hit||(row->buffs_fresh&&row->camp.buffs_known&&
         std::isfinite(row->camp.shield)&&row->camp.shield>=0))));
}
inline bool dead_through_next_camp_phase(const Packet& packet,const Row* row) {
    if(!current_body(row,false)||row->camp.alive||!row->stamp.matches(packet.stamp))return false;
    const auto* lifecycle=group(packet,row->camp.group);
    // Conditional pinned phase contract: no within-input respawn and at most
    // one timer decrement per round. With one tick of publication slack, c>=3
    // leaves the next phase strictly before the earliest possible new body.
    // This is sufficient for ONE camp phase, not a multi-phase absence proof.
    return lifecycle&&lifecycle->observed_round==packet.stamp.epoch&&
        lifecycle->state==frame::GroupState::respawning&&lifecycle->cooldown>=3;
}
inline bool current_retaliation_body(const Packet& packet,const Row* row) {
    if(!current_body(row,false))return false;
    if(row->camp.alive)return true;
    const auto* lifecycle=group(packet,row->camp.group);
    // A current corpse with an unknown respawn deadline is not a next-phase
    // absence proof. Fresh alive/reward groups cannot traverse the pinned
    // positive capture/respawn interval before the very next camp phase.
    return lifecycle&&lifecycle->observed_round==packet.stamp.epoch&&
        (lifecycle->state==frame::GroupState::alive||lifecycle->state==frame::GroupState::reward||
         dead_through_next_camp_phase(packet,row));
}
// Pinned non-attacking cluster contract: a gem/gold cluster with queried
// design Strength zero has no damage operator. Its hidden life, wake and respawn
// do not create retaliation. Effect/life rows are STILL mandatory when hit.
inline bool zero_retaliation_bound(const frame::SourceCamp& source) {
    return source.design.design_strength==0&&
        (source.handle.is_gem_cluster()||source.handle.is_gold_cluster());
}
inline OriginWait origin_wait_impl(const Packet& packet,const Request& request,const transition::Constraints& limits,const RowLookup& lookup) {
    using namespace transition;
    OriginWait result;
    if(!packet.stamp.matches({request.command.expected_epoch,request.command.expected_actor,request.command.expected_revision})||
        !same_request(packet.request,request)||!packet.origin_complete||!packet.origin_stamp.matches(packet.stamp)||
        !packet.self_complete||!packet.enemies_complete||!lookup.valid||
        !packet.layout.valid(packet.catalog.size())||packet.catalog.size()>spatial::capacity)return result;
    const auto support=packet.layout.index->threats(packet.self.pos.x,packet.self.pos.y);
    if(!support.complete)return result;
    for(auto index:support)if(!zero_retaliation_bound(packet.catalog[index])&&!resolved(packet,packet.catalog[index].design.group)) {
        const auto* row=lookup.row(index);
        if(!row||!row->retaliation_origin||!current_retaliation_body(packet,row))return result;
    }
    const auto& self=packet.self;float shield=self.shield;
    if(shield>0&&self.shield_duration<=1&&!limits.credit_short_shield){shield=0;result.flags|=short_shield_ignored;}
    auto prefix=prefixes(self.hp,self.max_hp,shield,{});result.complete=limits.camp_volleys==1;
    for(auto index:support) {
        const auto* row=lookup.row(index);const auto& design=packet.catalog[index].design;
        if(zero_retaliation_bound(packet.catalog[index]))continue;
        if(future_respawn(packet,design.group)&&!dead_through_next_camp_phase(packet,row)) {
            result.complete=false;result.flags|=unknown_wake;continue;
        }
        if(resolved(packet,design.group)||!row||!row->camp.alive)continue;
        const auto& camp=row->camp;
        // Another actor may wake this body before the next camp phase. Current
        // sleep is not a quiet-wait certificate without a separate no-wake proof.
        const float loss=rounded(camp.strength*camp_taken(self));
        if(!std::isfinite(loss)||loss<0||(loss>0&&!modifiers_known(self))) {
            result.complete=false;result.flags|=unknown_damage;continue;
        }
        result.next_volley=rounded(result.next_volley+loss);if(loss>0)prefix_event(prefix,self.hp,{EventKind::damage,loss});
    }
    for(const auto& foe:packet.enemies)if(foe.visible&&foe.alive) {
        result.flags|=unknown_foe_actions;if(!limits.allow_unmodelled_foes)result.complete=false;
        if(domain::distance(self.pos,foe.pos)<=std::int64_t(foe.range)+foe.move_speed) {
            const float loss=rounded(std::max(0.0f,foe.strength)*champion_taken(self));
            result.next_volley=rounded(result.next_volley+loss);if(loss>0)prefix_event(prefix,self.hp,{EventKind::damage,loss});
        }
    }
    result.hp_after_prefix=prefix.hp;result.safe=result.complete&&prefix.alive&&prefix.finite&&prefix.hp>limits.hp_reserve;
    return result;
}
inline OriginWait assess_origin_wait(const Packet& packet,const Request& request,const transition::Constraints& limits={}) {
    const RowLookup lookup(packet);return origin_wait_impl(packet,request,limits,lookup);
}
inline Result evaluate(const Packet& packet,const Request& request,const transition::Constraints& limits={}) {
    using namespace transition;
    Result result;auto& out=result.action;out.command=request.command;out.after_self=packet.self;
    const RowLookup lookup(packet);result.origin_wait=origin_wait_impl(packet,request,limits,lookup);
    auto fail=[&](Status status,Flag flag){result.status=status;out.flags|=flag;out.admissible=false;
        out.eligible=false;out.supported=status!=Status::unsupported;out.forecast_complete=false;return result;};
    if(!supported_kind(request.command.kind))return fail(Status::unsupported,unsupported_kind);
    if(!packet.stamp.matches({request.command.expected_epoch,request.command.expected_actor,request.command.expected_revision})||
        !same_request(packet.request,request))return fail(Status::stale,stale);
    if(!packet.complete||!packet.self_complete||!packet.enemies_complete)return fail(Status::incomplete,unavailable);
    if(!packet.layout.valid(packet.catalog.size())||packet.catalog.size()>spatial::capacity)return fail(Status::stale,outside_domain);
    if(!lookup.valid)return fail(Status::stale,outside_domain);
    const auto& self=packet.self;const auto& cmd=request.command;
    if(!self.alive||!std::isfinite(self.hp)||!std::isfinite(self.max_hp)||self.hp<=0||self.max_hp<self.hp||
        !std::isfinite(self.shield)||self.shield<0||self.move_speed<0||self.range<0)
        return fail(Status::rejected,unavailable);
    Pos after=self.pos;
    const Row* primary=nullptr;int radius=0;float hit=0;
    if(cmd.kind==Kind::move) {
        if(!packet.move_test_complete)return fail(Status::incomplete,wrong_target);
        if(!packet.move_legal||!self.can_move||cmd.destination==self.pos||
            domain::distance(self.pos,cmd.destination)>self.move_speed)return fail(Status::rejected,wrong_target);
        after=cmd.destination;
    } else if(cmd.kind==Kind::basic) {
        if(cmd.target_kind!=TargetKind::camp)return fail(Status::unsupported,unsupported_kind);
        if(!self.can_attack)return fail(Status::rejected,unavailable);
        for(const auto& row:packet.rows)if(row.camp.handle==cmd.target_id)primary=&row;
        if(!primary||!primary->effect||!current_body(primary,true))return fail(Status::incomplete,wrong_target);
        if(!primary->camp.visible||!primary->camp.alive||domain::distance(self.pos,primary->camp.pos)>self.range||
            (limits.group>=0&&primary->camp.group!=limits.group))return fail(Status::rejected,wrong_target);
        radius=splash_radius(self);hit=basic_damage(self);
        if(radius<0||!std::isfinite(hit)||hit<0)return fail(Status::unsupported,unknown_damage);
        if(!packet.effect_champions_clear)return fail(Status::unsupported,unsupported_kind);
        if(cmd.target_index>=0&&std::size_t(cmd.target_index)!=primary->index)return fail(Status::stale,wrong_target);
    }
    const auto origin_support=packet.layout.index->threats(self.pos.x,self.pos.y);
    const auto after_support=packet.layout.index->threats(after.x,after.y);
    if(!origin_support.complete||!after_support.complete)return fail(Status::incomplete,outside_domain);
    const auto effect_support=primary?packet.layout.index->hits(primary->camp.pos.x,primary->camp.pos.y,radius):spatial::Selection{};
    if(primary&&!effect_support.complete)return fail(Status::incomplete,outside_domain);
    for(auto index:origin_support)if(!zero_retaliation_bound(packet.catalog[index])&&!resolved(packet,packet.catalog[index].design.group)) {
        const auto* row=lookup.row(index);
        if(!row||!row->retaliation_origin||!current_retaliation_body(packet,row))return fail(Status::incomplete,unknown_wake);
    }
    for(auto index:after_support)if(!zero_retaliation_bound(packet.catalog[index])&&!resolved(packet,packet.catalog[index].design.group)) {
        const auto* row=lookup.row(index);
        if(!row||!row->retaliation_after||!current_retaliation_body(packet,row))return fail(Status::incomplete,unknown_wake);
    }
    for(auto index:effect_support)if(!resolved(packet,packet.catalog[index].design.group)) {
        const auto* row=lookup.row(index);
        if(!row||!row->effect||!current_body(row,true))return fail(Status::incomplete,unknown_damage);
    }
    out.admissible=true;
    if(!(limits.allowed_kinds&mask(cmd.kind))||(cmd.kind!=Kind::wait&&
       (self.stunned||self.recalling||self.submitting||self.teleporting)))return fail(Status::rejected,unavailable);
    if(cmd.kind==Kind::move) {
        out.after_self.pos=after;out.after_self.moved=true;out.after_self.can_move=false;
        if(self.range>1)out.after_self.can_attack=false;
    } else if(primary) {
        out.after_self.can_attack=false;out.after_self.attacked=true;
        if(self.range>1)out.after_self.can_move=false;
        if(!damage_known(self)){out.effect_exact=false;out.flags|=unknown_damage;}
        for(const auto& foe:packet.enemies)if(foe.visible&&foe.alive&&domain::distance(foe.pos,primary->camp.pos)<=radius)
            return fail(Status::unsupported,unsupported_kind);
        for(auto index:effect_support) {
            const auto* row=lookup.row(index);if(!row||!row->camp.alive||resolved(packet,row->camp.group))continue;
            const auto& camp=row->camp;wake(out,camp.group);
            const float blocked=std::min(camp.shield,hit);
            const float hp=std::max(0.0f,rounded(camp.hp-rounded(hit-blocked)));
            out.camps.push_back({camp.handle,camp.hp,hp,0});
            if(limits.group<0||camp.group==limits.group)out.useful_hp_removed=rounded(out.useful_hp_removed+rounded(camp.hp-hp));
        }
    }
    struct Forecast {Prefix prefix;float volley{};bool complete{true};std::uint64_t flags{};};
    auto forecast=[&](const spatial::Selection& support,Pos position,bool success) {
        float shield=self.shield;Forecast f;
        if(shield>0&&self.shield_duration<=1&&!limits.credit_short_shield){shield=0;f.flags|=short_shield_ignored;}
        f.prefix=prefixes(self.hp,self.max_hp,shield,{});
        if(limits.camp_volleys!=1){f.complete=false;f.flags|=unsupported_kind;}
        for(auto index:support) {
            const auto* row=lookup.row(index);const auto& design=packet.catalog[index].design;
        if(zero_retaliation_bound(packet.catalog[index]))continue;
            if(future_respawn(packet,design.group)&&!dead_through_next_camp_phase(packet,row)) {
                f.complete=false;f.flags|=unknown_wake;continue;
            }
            if(resolved(packet,design.group)||!row||!row->camp.alive)continue;
            const auto& camp=row->camp;
            if(success&&out.effect_exact)if(const auto* change=effect(out,camp.handle);change&&change->after_hp<=0)continue;
            // Saturate possible awake state across later actors; no implicit
            // coordination/no-wake premise is supplied by a sleeping observation.
            const float loss=rounded(camp.strength*camp_taken(self));
            if(!std::isfinite(loss)||loss<0||(loss>0&&!modifiers_known(self))){f.complete=false;f.flags|=unknown_damage;continue;}
            f.volley=rounded(f.volley+loss);if(loss>0)prefix_event(f.prefix,self.hp,{EventKind::damage,loss});
        }
        for(const auto& foe:packet.enemies)if(foe.visible&&foe.alive) {
            f.flags|=unknown_foe_actions;if(!limits.allow_unmodelled_foes)f.complete=false;
            if(domain::distance(position,foe.pos)<=std::int64_t(foe.range)+foe.move_speed) {
                const float loss=rounded(std::max(0.0f,foe.strength)*champion_taken(self));
                f.volley=rounded(f.volley+loss);if(loss>0)prefix_event(f.prefix,self.hp,{EventKind::damage,loss});
            }
        }
        return f;
    };
    const auto applied=forecast(after_support,after,true);
    out.forecast_complete=applied.complete;out.next_volley=applied.volley;out.max_prefix_loss=applied.prefix.max_prefix_loss;
    out.hp_after_prefix=applied.prefix.hp;out.survives_prefix=applied.prefix.alive&&applied.prefix.finite&&applied.prefix.hp>limits.hp_reserve;
    out.flags|=result.origin_wait.flags|applied.flags;
    if(!out.survives_prefix||!result.origin_wait.safe)out.flags|=unsafe_prefix;
    // No v1 certificate can omit a harmful unchanged-position/refusal outcome.
    out.eligible=out.admissible&&out.supported&&out.forecast_complete&&out.survives_prefix&&
        result.origin_wait.complete&&result.origin_wait.safe&&(!limits.require_exact_effect||out.effect_exact);
    result.status=Status::ready;return result;
}
struct Assessment;
class Certificate {
    Stamp stamp_;Request request_;spatial::Token token_;cambc::vec2 origin_{};std::uint64_t nonce_{};bool consumed_{};
    Certificate(const Packet& packet,const Request& request):stamp_(packet.stamp),request_(request),token_(packet.layout),
        origin_(packet.self.pos),nonce_(packet.nonce){}
    friend Assessment assess(const Packet&,const Request&,const transition::Constraints&);
public:
    Certificate(const Certificate&)=delete;Certificate& operator=(const Certificate&)=delete;
    Certificate(Certificate&& other) noexcept:stamp_(other.stamp_),request_(other.request_),token_(other.token_),
        origin_(other.origin_),nonce_(other.nonce_),consumed_(other.consumed_){other.consumed_=true;}
    Certificate& operator=(Certificate&&)=delete;
    std::optional<Command> consume(Stamp stamp,const Request& request,spatial::Token token,cambc::vec2 origin,std::uint64_t nonce) {
        if(consumed_)return std::nullopt;
        consumed_=true;
        if(!stamp_.matches(stamp)||!same_request(request_,request)||!same_token(token_,token)||
            !token.valid(token.count)||origin_!=origin||nonce_!=nonce)return std::nullopt;
        return request_.command;
    }
};
struct Assessment {Result result;std::optional<Certificate> certificate;};
inline Assessment assess(const Packet& packet,const Request& request,const transition::Constraints& limits={}) {
    auto result=evaluate(packet,request,limits);
    if(result.action.eligible)return {std::move(result),std::optional<Certificate>{Certificate{packet,request}}};
    return {std::move(result),std::nullopt};
}
inline std::optional<Certificate> certify(const Packet& packet,const Request& request,const transition::Constraints& limits={}) {
    return assess(packet,request,limits).certificate;
}
} // namespace mosaic::selected
