#pragma once
#include "capital_contact.h"
#include <cmath>
#include <cstdint>
#include <span>

// Same-horizon MODEL-RISK selection, separate from closed safety admission.
// No observation-completeness flag is assigned here. Useful progress is a
// caller-bound dated operator premise, never inferred from position, raw damage
// effects, a prospective clear/capture, or the mere start of a bank channel.
namespace wb::capital::word_arbitration {
enum class HorizonKind {NextScheduledOwnerInput,NextUsableOwnerInputBound};
struct Horizon {
    int owner=-1,round=-1,end_round=-1;
    std::uint64_t generation=0;
    bool fresh=false;
    HorizonKind kind=HorizonKind::NextScheduledOwnerInput;
};
enum class ServiceKind {None,ChannelProgress,EnemyKill,CampWork};
enum class TargetKind {None,Altar,Enemy,Camp};
struct Service {
    ServiceKind kind=ServiceKind::None;Horizon horizon{};
    TargetKind target_kind=TargetKind::None;int target_id=-1;
    unsigned effect_count=0;
    float quantity=0;
    // bound covers this exact target key, effect trace, endpoint and horizon.
    // quantity is proposed channel progress, whole modeled enemy kills, or a
    // LOWER persistent camp HP delta; none is an actual receipt or bank score.
    bool modeled_useful=false,dated=false,bound=false,persistent=false,exit_known=false,exit_priced=false;
    float exit_damage_upper=0;
};
// Immutable, lossless caller encoding of the actual word and its dependency /
// evaluation-domain identity. This is NOT a hash or an arbitrary nomination ID.
// The external proof producer owns storage for the whole comparison. A partial
// encoding cannot bind a certificate; large unsupported encodings stay model-
// conditional. Model selection itself needs no binding or proof.
struct WordBinding {std::span<const std::uint64_t> exact_words{};bool complete=false;};
// Supplied only by an INDEPENDENT closed-horizon producer. Evaluation booleans
// or effects alone cannot mint this proof. Each field is a caller obligation,
// scoped to the exact dated word/endpoint, including failed native prefixes.
struct HorizonProof {
    bool supplied=false;Horizon horizon{};contact::Point endpoint{};unsigned effect_count=0;
    WordBinding binding{};
    contact::Choice bound_choice{};contact::Evaluation bound_evaluation{};Service bound_service{};
    unsigned bound_lost_inputs_upper=0;bool bound_delay_priced=false;
    bool word_dependencies_closed=false,reply_domain_closed=false,npc_closed=false,
        active_effects_closed=false,refusal_prefixes_closed=false,fuel_closed=false;
};
// A non-owning borrow, not optional inline proof storage. A default model-only
// Offer carries one null pointer; copying an Offer never constructs or copies
// a HorizonProof. The proof AND its binding.exact_words storage must stay alive
// and immutable through every classify / compare using any copy of the Offer.
// This is a caller lifetime obligation, not a runtime dangling-pointer check.
// Temporary attachment is rejected; a later observation still needs its own
// exact horizon / word / evaluation bindings even while old storage is alive.
class BorrowedProof {
    const HorizonProof* proof_=nullptr;
public:
    constexpr BorrowedProof() noexcept=default;
    explicit constexpr BorrowedProof(const HorizonProof& proof) noexcept:proof_(&proof){}
    BorrowedProof(HorizonProof&&)=delete;
    BorrowedProof(const HorizonProof&&)=delete;
    [[nodiscard]] constexpr const HorizonProof* get() const noexcept{return proof_;}
};
struct Offer {
    contact::Choice choice{};contact::Evaluation evaluation{};Horizon horizon{};
    unsigned modeled_effect_count=0;Service service{};
    unsigned modeled_lost_inputs_upper=0;bool delay_priced=false;
    WordBinding binding{};
    BorrowedProof proof{};
};
struct Class {
    // Initial supported assurance class. Potentially stunned work remains a
    // conditional fallback; it is not declared universally illegal or useless.
    // A future interrupted class must price delay to one shared usable-input
    // horizon, rather than silently borrowing this next-input assessment.
    bool requires_uninterrupted_next_input=true;
};
enum class Classification {ConditionalModel,Certified};
enum class Preference {Left,Right,Tie,Incomparable};
enum class Basis {Validity,Horizon,PrefixAlive,Survival,InitialUninterruptedClass,Receipt,
    UsefulProgress,ProgressQuantity,ResidualHP,Equivalent};
struct Comparison {
    Preference preference=Preference::Incomparable;Basis basis=Basis::Validity;
    Classification left_class=Classification::ConditionalModel,right_class=Classification::ConditionalModel;
    bool left_progress=false,right_progress=false,service_fallback=false;
    bool left_modeled_lost_input=false,right_modeled_lost_input=false;
    bool uninterrupted_class=true;
};
namespace detail {
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline bool valid(const Horizon& h) {
    if(!h.fresh||h.owner<0||h.round<0||h.round>=1000000000||h.end_round<=h.round||h.generation==0)return false;
    if(h.kind==HorizonKind::NextScheduledOwnerInput)return h.end_round==h.round+1;
    return h.kind==HorizonKind::NextUsableOwnerInputBound;
}
inline bool same(const Horizon& a,const Horizon& b) {
    return valid(a)&&valid(b)&&a.owner==b.owner&&a.round==b.round&&a.end_round==b.end_round
        &&a.generation==b.generation&&a.kind==b.kind;
}
inline bool same_values(const Horizon& a,const Horizon& b) {
    return a.owner==b.owner&&a.round==b.round&&a.end_round==b.end_round&&a.generation==b.generation
        &&a.fresh==b.fresh&&a.kind==b.kind;
}
inline bool same(const WordBinding& a,const WordBinding& b) {
    if(!a.complete||!b.complete||a.exact_words.empty()||a.exact_words.size()>512||a.exact_words.size()!=b.exact_words.size())return false;
    for(unsigned i=0;i<a.exact_words.size();++i)if(a.exact_words[i]!=b.exact_words[i])return false;
    return true;
}
inline bool same(const contact::Choice& a,const contact::Choice& b) {
    if(a.primitive!=b.primitive||a.endpoint.x!=b.endpoint.x||a.endpoint.y!=b.endpoint.y||a.effect_count!=b.effect_count
       ||a.banked_gems!=b.banked_gems||a.channel_started!=b.channel_started||a.ally_rescues!=b.ally_rescues
       ||a.ally_hp_margin!=b.ally_hp_margin||a.ally_effect_covered!=b.ally_effect_covered||a.camp_work!=b.camp_work
       ||a.objective_distance!=b.objective_distance||a.displaced_inputs!=b.displaced_inputs||a.npc_damage_upper!=b.npc_damage_upper
       ||a.continuation_damage_upper!=b.continuation_damage_upper||a.npc_hit_count!=b.npc_hit_count||a.native_legal!=b.native_legal
       ||a.prefix_complete!=b.prefix_complete||a.continuation_covered!=b.continuation_covered||a.preserves_receipt!=b.preserves_receipt)return false;
    for(unsigned i=0;i<a.effect_count;++i) {
        const auto& x=a.effects[i];const auto& y=b.effects[i];
        if(x.kind!=y.kind||x.foe!=y.foe||x.amount!=y.amount||x.stun_inputs!=y.stun_inputs)return false;
    }
    return true;
}
inline bool same(const contact::Evaluation& a,const contact::Evaluation& b) {
    return a.valid==b.valid&&a.prefix_alive==b.prefix_alive&&a.survives==b.survives&&a.horizon_admissible==b.horizon_admissible
        &&a.may_stun_next==b.may_stun_next&&a.continuation_admissible==b.continuation_admissible&&a.hp_lower==b.hp_lower
        &&a.damage_upper==b.damage_upper&&a.reply_hits==b.reply_hits&&a.kills==b.kills;
}
inline bool same(const Service& a,const Service& b) {
    return a.kind==b.kind&&same_values(a.horizon,b.horizon)&&a.target_kind==b.target_kind&&a.target_id==b.target_id
        &&a.effect_count==b.effect_count&&a.quantity==b.quantity&&a.modeled_useful==b.modeled_useful&&a.dated==b.dated
        &&a.bound==b.bound&&a.persistent==b.persistent&&a.exit_known==b.exit_known&&a.exit_priced==b.exit_priced
        &&a.exit_damage_upper==b.exit_damage_upper;
}
inline bool valid(const Offer& o) {
    const auto& e=o.evaluation;
    return valid(o.horizon)&&e.valid&&finite(e.hp_lower)&&finite(e.damage_upper)&&e.kills<=contact::max_foes
        &&o.modeled_effect_count<=contact::max_effects&&o.choice.effect_count<=contact::max_effects
        &&o.choice.endpoint.x>=0&&o.choice.endpoint.y>=0&&o.choice.endpoint.x<=100&&o.choice.endpoint.y<=100
        &&(!e.survives||(e.prefix_alive&&e.hp_lower>0))
        &&(!e.horizon_admissible||(e.survives&&!e.may_stun_next))
        &&(!e.continuation_admissible||e.horizon_admissible);
}
enum class Progress {None,Eligible,Unknown};
inline Progress progress(const Offer& o,Class admitted) {
    const auto& s=o.service;const auto& c=o.choice;const auto& e=o.evaluation;
    if(s.kind==ServiceKind::None)return s.modeled_useful||s.quantity!=0?Progress::Unknown:Progress::None;
    if(!s.modeled_useful||!s.dated||!s.bound||!same(s.horizon,o.horizon)||s.target_id<0
       ||s.effect_count!=o.modeled_effect_count||!finite(s.quantity)||s.quantity<=0
       ||!c.native_legal||!c.prefix_complete||!e.prefix_alive||!e.survives)return Progress::Unknown;
    if(e.may_stun_next||o.modeled_lost_inputs_upper>0) {
        if(admitted.requires_uninterrupted_next_input||!o.delay_priced||o.modeled_lost_inputs_upper==0
           ||o.horizon.kind!=HorizonKind::NextUsableOwnerInputBound
           ||static_cast<std::int64_t>(o.horizon.end_round)<static_cast<std::int64_t>(o.horizon.round)+1+o.modeled_lost_inputs_upper)
            return Progress::Unknown;
    }
    if(s.kind==ServiceKind::ChannelProgress) {
        return s.target_kind==TargetKind::Altar&&c.primitive==contact::Primitive::Submit&&c.channel_started>0
            &&s.quantity<=c.channel_started?Progress::Eligible:Progress::Unknown;
    }
    if(s.kind==ServiceKind::EnemyKill) {
        return s.target_kind==TargetKind::Enemy&&s.effect_count>0&&e.kills>0&&s.quantity<=e.kills
            &&std::floor(s.quantity)==s.quantity?Progress::Eligible:Progress::Unknown;
    }
    if(s.kind==ServiceKind::CampWork) {
        return s.target_kind==TargetKind::Camp&&s.effect_count>0&&s.persistent&&s.exit_known&&s.exit_priced
            &&finite(c.camp_work)&&s.quantity<=c.camp_work&&finite(s.exit_damage_upper)&&e.hp_lower>s.exit_damage_upper
            ?Progress::Eligible:Progress::Unknown;
    }
    return Progress::Unknown;
}
}
[[nodiscard]] inline Classification classify(const Offer& o) {
    const auto* proof=o.proof.get();
    if(!proof)return Classification::ConditionalModel;
    const auto& p=*proof;const auto& e=o.evaluation;
    const bool closed=detail::valid(o)&&p.supplied&&detail::same(p.horizon,o.horizon)
        &&p.endpoint.x==o.choice.endpoint.x&&p.endpoint.y==o.choice.endpoint.y&&p.effect_count==o.modeled_effect_count
        &&detail::same(p.binding,o.binding)&&detail::same(p.bound_choice,o.choice)&&detail::same(p.bound_evaluation,e)
        &&detail::same(p.bound_service,o.service)&&p.bound_lost_inputs_upper==o.modeled_lost_inputs_upper
        &&p.bound_delay_priced==o.delay_priced&&o.modeled_lost_inputs_upper==0
        &&p.word_dependencies_closed&&p.reply_domain_closed&&p.npc_closed&&p.active_effects_closed
        &&p.refusal_prefixes_closed&&p.fuel_closed&&o.choice.native_legal&&o.choice.prefix_complete
        &&e.prefix_alive&&e.survives&&!e.may_stun_next&&e.horizon_admissible;
    return closed?Classification::Certified:Classification::ConditionalModel;
}
[[nodiscard]] inline Comparison compare(const Offer& a,const Offer& b,Class admitted={}) {
    Comparison out;out.left_class=classify(a);out.right_class=classify(b);
    out.left_modeled_lost_input=a.evaluation.may_stun_next||a.modeled_lost_inputs_upper>0;
    out.right_modeled_lost_input=b.evaluation.may_stun_next||b.modeled_lost_inputs_upper>0;
    out.uninterrupted_class=admitted.requires_uninterrupted_next_input;
    const auto prefer=[&](bool left,Basis basis){out.preference=left?Preference::Left:Preference::Right;out.basis=basis;return out;};
    const bool av=detail::valid(a),bv=detail::valid(b);
    if(av!=bv)return prefer(av,Basis::Validity);
    if(!av)return out;
    if(!detail::same(a.horizon,b.horizon)){out.basis=Basis::Horizon;return out;}
    const auto& x=a.evaluation;const auto& y=b.evaluation;
    if(x.prefix_alive!=y.prefix_alive)return prefer(x.prefix_alive,Basis::PrefixAlive);
    if(x.survives!=y.survives)return prefer(x.survives,Basis::Survival);
    if(admitted.requires_uninterrupted_next_input&&out.left_modeled_lost_input!=out.right_modeled_lost_input)
        return prefer(!out.left_modeled_lost_input,Basis::InitialUninterruptedClass);
    if(a.choice.preserves_receipt!=b.choice.preserves_receipt)return prefer(a.choice.preserves_receipt,Basis::Receipt);
    const auto ap=detail::progress(a,admitted),bp=detail::progress(b,admitted);
    out.left_progress=ap==detail::Progress::Eligible;out.right_progress=bp==detail::Progress::Eligible;
    // Unknown service is not silently treated as idle. Distinct service units
    // or target identities have no invented exchange rate; retain HP fallback.
    out.service_fallback=ap==detail::Progress::Unknown||bp==detail::Progress::Unknown;
    if(!out.service_fallback) {
        if(out.left_progress!=out.right_progress)return prefer(out.left_progress,Basis::UsefulProgress);
        if(out.left_progress&&out.right_progress) {
            if(a.service.kind==b.service.kind&&a.service.target_kind==b.service.target_kind&&a.service.target_id==b.service.target_id) {
                if(a.service.quantity!=b.service.quantity)return prefer(a.service.quantity>b.service.quantity,Basis::ProgressQuantity);
            } else out.service_fallback=true;
        }
    }
    // No credit for objective distance, prospective capture/score, or unbound
    // raw effects. These are model residuals on one identical dated horizon.
    if(x.hp_lower!=y.hp_lower)return prefer(x.hp_lower>y.hp_lower,Basis::ResidualHP);
    out.preference=Preference::Tie;out.basis=Basis::Equivalent;return out;
}
[[nodiscard]] inline bool better(const Offer& candidate,const Offer& incumbent,Class admitted={}) {
    // Fixed-nomination incumbent arbitration ONLY. Same-service quantity and
    // cross-service HP fallbacks need not form a transitive global order; do
    // not pass this to sorting or claim nomination-independent maximization.
    return compare(candidate,incumbent,admitted).preference==Preference::Left;
}
}
