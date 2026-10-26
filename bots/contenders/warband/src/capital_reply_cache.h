#pragma once
#include "capital_contact.h"

// Exact reply physics within ONE immutable captured input. This contains no
// action, coverage, horizon or continuation authority. A native caller must
// invalidate BEFORE every attempted primitive, including refused attempts.
namespace wb::capital::reply_cache {
inline constexpr unsigned capacity=8;
struct Binding {
    int owner=-1,round=-1;
    std::uint64_t generation=0;
};
struct RiskRequest {
    contact::Point endpoint{};
    std::array<contact::Effect,contact::max_effects> effects{};
    unsigned effect_count=0;
    float npc_damage_upper=0;
    unsigned npc_hit_count=0;
};
struct RiskCore {
    bool valid=false,prefix_alive=false,survives=false;
    float hp_lower=0,damage_upper=0;
    unsigned reply_hits=0,kills=0;
    bool may_stun_next=false;
};
enum class Status {Inactive,WrongBinding,InvalidRequest,InvalidEvaluation,Evaluated,Reused};
struct Result {
    RiskCore core{};
    Status status=Status::Inactive;
    bool reused=false,evaluated=false;
};
namespace detail {
inline bool same(Binding a,Binding b) {
    return a.owner==b.owner&&a.round==b.round&&a.generation==b.generation;
}
inline bool valid_frame(const contact::Frame& f) {
    // Exactly the contact model's frame domains, not a coverage certificate.
    if(!f.fresh||f.owner<0||f.round<0||f.position.x<0||f.position.y<0
        ||f.position.x>100||f.position.y>100||!contact::detail::finite(f.hp)||f.hp<=0
        ||!contact::detail::finite(f.max_hp)||f.max_hp<f.hp||!contact::detail::finite(f.shield)
        ||f.foe_count>contact::max_foes)return false;
    for(unsigned i=0;i<f.foe_count;++i) {
        if(!contact::detail::valid_foe(f.foes[i]))return false;
        for(unsigned j=0;j<i;++j)if(f.foes[i].id==f.foes[j].id)return false;
    }
    return true;
}
inline bool valid_request(const RiskRequest& r,unsigned foes) {
    if(r.endpoint.x<0||r.endpoint.y<0||r.endpoint.x>100||r.endpoint.y>100
        ||r.effect_count>contact::max_effects||!contact::detail::finite(r.npc_damage_upper)
        ||r.npc_hit_count>512)return false;
    for(unsigned i=0;i<r.effect_count;++i) {
        const auto& e=r.effects[i];
        if(static_cast<unsigned>(e.kind)>static_cast<unsigned>(contact::EffectKind::StunFoe)
            ||!contact::detail::finite(e.amount)||e.stun_inputs<0||e.stun_inputs>100)return false;
        if((e.kind==contact::EffectKind::DamageFoe||e.kind==contact::EffectKind::StunFoe)&&e.foe>=foes)return false;
    }
    return true;
}
inline bool same(const RiskRequest& a,const RiskRequest& b) {
    if(a.endpoint.x!=b.endpoint.x||a.endpoint.y!=b.endpoint.y||a.effect_count!=b.effect_count
        ||a.npc_damage_upper!=b.npc_damage_upper||a.npc_hit_count!=b.npc_hit_count)return false;
    for(unsigned i=0;i<a.effect_count;++i) {
        const auto& x=a.effects[i];const auto& y=b.effects[i];
        if(x.kind!=y.kind||x.foe!=y.foe||x.amount!=y.amount||x.stun_inputs!=y.stun_inputs)return false;
    }
    return true;
}
inline contact::Choice normalized(const RiskRequest& r) {
    contact::Choice c;
    c.primitive=contact::Primitive::Hold;c.endpoint=r.endpoint;
    c.effect_count=r.effect_count;
    for(unsigned i=0;i<r.effect_count;++i)c.effects[i]=r.effects[i];
    c.npc_damage_upper=r.npc_damage_upper;c.npc_hit_count=r.npc_hit_count;
    c.native_legal=c.prefix_complete=true;c.continuation_covered=false;
    return c;
}
inline RiskCore core(const contact::Evaluation& e,const contact::Frame& f) {
    // Aggregate damage can exceed the model's 1e9 PER-INPUT-value bound.
    if(!e.valid||!std::isfinite(e.hp_lower)||e.hp_lower<0||e.hp_lower>f.max_hp
        ||!std::isfinite(e.damage_upper)||e.damage_upper<0||e.kills>f.foe_count
        ||e.reply_hits>512u+static_cast<unsigned>(contact::max_foes)*6u
        ||e.survives!=(e.prefix_alive&&e.hp_lower>0)
        ||e.horizon_admissible||e.continuation_admissible)return {};
    return {e.valid,e.prefix_alive,e.survives,e.hp_lower,e.damage_upper,e.reply_hits,e.kills,e.may_stun_next};
}
}
class Session {
    struct Entry {RiskRequest request{};RiskCore core{};};
    contact::Frame frame_{};
    Binding binding_{};
    std::array<Entry,capacity> entries_{};
    unsigned count_=0,evaluations_=0,reuses_=0;
    std::uint64_t last_generation_=0;
    bool active_=false;
public:
    // Every reset revokes the previous capture, including a rejected reset.
    // Frame's value fields/arrays are copied, never borrowed from the caller.
    // Capture generations increase within a Session; revoked generations can
    // never rearm, even after an intervening capture or a rejected reset.
    bool reset(const contact::Frame& frame,Binding binding) {
        invalidate();evaluations_=reuses_=0;
        if(!binding.generation||binding.generation<=last_generation_||binding.owner!=frame.owner||binding.round!=frame.round
            ||!detail::valid_frame(frame))return false;
        frame_=frame;binding_=binding;last_generation_=binding.generation;active_=true;return true;
    }
    void invalidate(){active_=false;count_=0;}
    bool active() const{return active_;}
    unsigned size() const{return count_;}
    unsigned evaluations() const{return evaluations_;}
    unsigned reuses() const{return reuses_;}
    Result query(Binding binding,const RiskRequest& request) {
        Result out;
        if(!active_)return out;
        if(!detail::same(binding_,binding)){out.status=Status::WrongBinding;return out;}
        if(!detail::valid_request(request,frame_.foe_count)){out.status=Status::InvalidRequest;return out;}
        for(unsigned i=0;i<count_;++i)if(detail::same(entries_[i].request,request)) {
            out.core=entries_[i].core;out.status=Status::Reused;out.reused=true;++reuses_;return out;
        }
        const auto e=contact::detail::evaluate(frame_,detail::normalized(request),false);
        ++evaluations_;out.evaluated=true;out.core=detail::core(e,frame_);
        if(!out.core.valid){out.status=Status::InvalidEvaluation;return out;}
        out.status=Status::Evaluated;
        // A full cache still evaluates normally. It never grows or silently
        // drops a query; overflow results simply have no retained entry.
        if(count_<capacity)entries_[count_++]={request,out.core};
        return out;
    }
};
}
