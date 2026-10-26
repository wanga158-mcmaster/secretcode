#pragma once
#include "profile.h"
#include "../../common/src/organization.h"
#include <span>

namespace p8::banded_cells {
// Draft/adapter certificates, not another actor's private ledger. A native
// builder must establish each complete first-income cycle from public inputs.
struct CircuitCandidate {
    Plan plan{};
    p6::Fact<int> spawn_distance{};
    p6::Fact<bool> solo_first_income{};
};
inline bool public_fixture(p6::Source source) {
    return source==p6::Source::public_map || source==p6::Source::synthetic_fixture;
}
inline std::uint64_t geometry_hash(const Plan& plan) {
    std::uint64_t hash=1469598103934665603ULL;
    for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal}) {
        const auto part=route_hash(route_for(plan,leg));
        if (!part) return 0;
        hash=hash_word(hash,part);
    }
    return hash;
}
// The district retains its source, capture and return circuit. Reaching it
// from spawn or a new safe waypoint is an entry leg, not a different district.
// H has a separate assembly/capture prefix, bound by its exact evidence slot.
inline bool assigned_circuit(const Plan& assigned,const Plan& offered) {
    if (assigned.group!=offered.group || assigned.altar!=offered.altar ||
        route_hash(assigned.bank)!=route_hash(offered.bank) ||
        route_hash(assigned.renewal)!=route_hash(offered.renewal)) return false;
    return offered.organization==Organization::H ||
        route_hash(assigned.capture)==route_hash(offered.capture);
}
class Assignments {
    std::array<Plan,2> plans_{};
    std::array<int,2> actors_{-1,-1};
    std::array<p6::Champion,2> champions_{p6::Champion::other,p6::Champion::other};
    bool available_=false;
public:
    static bool select_into(Assignments& out,const DraftRoster& draft,std::span<const CircuitCandidate> candidates) {
        out.available_=false;
        if (!draft.valid() || candidates.empty() || candidates.size()>8) return false;
        auto eligible=[&](int index,int ordinal) {
            const auto& candidate=candidates[index]; const auto& plan=candidate.plan;
            return plan.organization==Organization::U && plan.collector==draft.actors[ordinal] &&
                plan_valid(plan,draft.actors[ordinal]) && candidate.solo_first_income.value &&
                public_fixture(candidate.solo_first_income.source) && public_fixture(candidate.spawn_distance.source) &&
                candidate.spawn_distance.value>=0;
        };
        auto key=[&](int a,int b) {
            return std::array<std::uint64_t,6>{std::uint64_t(candidates[a].spawn_distance.value),
                std::uint64_t(candidates[b].spawn_distance.value),std::uint64_t(candidates[a].plan.group),
                std::uint64_t(candidates[b].plan.group),plan_route_hash(candidates[a].plan),plan_route_hash(candidates[b].plan)};
        };
        int first=-1,second=-1;
        // At most64 pairs. A greedy first pick could consume the only source
        // available to the other actor despite a valid distinct pair existing.
        for (int i=0;i<int(candidates.size());++i) if (eligible(i,0))
            for (int j=0;j<int(candidates.size());++j) if (eligible(j,1)) {
                const auto& a=candidates[i]; const auto& b=candidates[j];
                if (a.plan.group==b.plan.group || geometry_hash(a.plan)==geometry_hash(b.plan) ||
                    a.plan.capture.tiles[a.plan.capture.count-1]==b.plan.capture.tiles[b.plan.capture.count-1]) continue;
                if (first<0 || key(i,j)<key(first,second)) { first=i; second=j; }
            }
        if (first<0) return false;
        out.plans_[0]=candidates[first].plan; out.plans_[1]=candidates[second].plan;
        out.actors_[0]=draft.actors[0]; out.actors_[1]=draft.actors[1];
        out.champions_[0]=draft.champions[0]; out.champions_[1]=draft.champions[1];
        out.available_=true; return true;
    }
    static Assignments select(const DraftRoster& draft,std::span<const CircuitCandidate> candidates) {
        Assignments out; select_into(out,draft,candidates); return out;
    }
    bool valid(const DraftRoster& draft) const {
        return available_ && draft.valid() && actors_[0]==draft.actors[0] && actors_[1]==draft.actors[1] &&
            champions_[0]==draft.champions[0] && champions_[1]==draft.champions[1];
    }
    const Plan* for_actor(int actor) const {
        for (int i=0;i<2;++i) if (available_ && actors_[i]==actor) return &plans_[i];
        return nullptr;
    }
    const Plan* for_ordinal(int ordinal) const { return available_ && ordinal>=0 && ordinal<2 ? &plans_[ordinal] : nullptr; }
};
// J uses one named source/altar and a retained bank/return corridor. Distinct
// assembly pads and capture tiles need actor-specific ingress/capture prefixes;
// the first bank tile is that actor's capture tile, before the shared corridor.
inline std::uint64_t joint_route_hash(const Plan& plan) {
    std::uint64_t hash=hash_word(1469598103934665603ULL,plan.group);
    hash=hash_word(hash,plan.altar);
    if (!route_valid(plan.bank) || plan.bank.count<3 || !route_valid(plan.renewal)) return 0;
    hash=hash_word(hash,plan.bank.blocker_assumptions); hash=hash_word(hash,plan.bank.count-1);
    for (int i=1;i<plan.bank.count;++i) {
        hash=hash_word(hash,std::uint32_t(plan.bank.tiles[i].x)); hash=hash_word(hash,std::uint32_t(plan.bank.tiles[i].y));
    }
    hash=hash_word(hash,route_hash(plan.renewal));
    return hash;
}
} // namespace p8::banded_cells
