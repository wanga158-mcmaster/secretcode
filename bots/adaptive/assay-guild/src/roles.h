#pragma once
#include "assay.h"
namespace p8::assay_guild {
struct RoleSpec {
    Configuration configuration=Configuration::independent;
    Plan primary{},next{};
    ContextDeclaration context{};
    p6::CycleCertificate next_safety{};
    int nominal_duration=-1;
};
inline bool collecting(const RoleSpec& role,int owner) { return role.primary.collector==owner; }
inline std::uint64_t role_hash(const RoleSpec& role,int owner) {
    if (role.configuration==Configuration::independent || !plan_valid(role.primary,owner)) return 0;
    auto hash=hash_word(1469598103934665603ULL,int(role.configuration));
    hash=hash_word(hash,plan_route_hash(role.primary));hash=hash_word(hash,role.primary.collector);
    hash=hash_word(hash,role.primary.altar);
    for (int i=0;i<role.primary.crew_count;++i) {
        hash=hash_word(hash,role.primary.crew[i]);hash=hash_word(hash,role.primary.pads[i].x);hash=hash_word(hash,role.primary.pads[i].y);
    }
    if (!collecting(role,owner)) {
        if (!plan_valid(role.next,owner)) return 0;
        hash=hash_word(hash,plan_route_hash(role.next));hash=hash_word(hash,role.next.group);hash=hash_word(hash,role.next.altar);
    }
    return hash;
}
inline bool role_valid(const RoleSpec& role,const common::Inputs& in) {
    const int self=in.frame.clock.self;const auto& p=role.primary;const auto& key=role.context.key;
    const int collector=role.configuration==Configuration::joint_clear?in.draft.actors[0]:in.draft.actors[1];
    if ((role.configuration!=Configuration::joint_clear && role.configuration!=Configuration::serial) ||
        p.organization!=Organization::H || p.collector!=collector || p.crew_count!=2 ||
        !common::participant(p,in.draft.actors[0]) || !common::participant(p,in.draft.actors[1]) ||
        !role_hash(role,self) || role.nominal_duration<0 || role.nominal_duration>120 ||
        role.context.kind!=EpisodeKind::full_organization || !role.context.require_supported_work ||
        key.actor_id!=self || key.participant_count!=2 || key.participants[0]!=std::min(in.draft.actors[0],in.draft.actors[1]) ||
        key.participants[1]!=std::max(in.draft.actors[0],in.draft.actors[1]) ||
        key.route_hash!=role_hash(role,self) || !common::guarded_key_current(in,key) ||
        !role.context.envelope.contains(in.own_state.value,key.build)) return false;
    if (collecting(role,self)) return key.source_group==p.group;
    return role.next.organization==Organization::U && role.next.collector==self && role.next.group!=p.group &&
        key.source_group==role.next.group && p.release.count>0 &&
        p.release.tiles[p.release.count-1]==role.next.ingress.tiles[0] &&
        role.next_safety.group==role.next.group && role.next_safety.valid(in.frame,self,role.next.group) &&
        independent_safety(role.next_safety);
}
struct Proposal { Configuration configuration=Configuration::independent;int group=-1; };
struct RoleOffer { int offer=-1;RoleSpec role{}; };
struct Inputs:common::Inputs {
    AssaySpec assay{};
    std::array<RoleOffer,2> roles{};
    p6::Observation<Proposal> proposal{};
    // Own production remains executable when another affected actor is unseen.
    // This fallback never supplies the ordinary-comparator assay permission.
    p6::Observation<common::Offer> private_income{};
};
struct Guild {
    int owner=-1,locked_slot=-1;
    Configuration locked=Configuration::independent;
    Assay assay{};
    explicit Guild(int actor):owner(actor) {}
};
}
