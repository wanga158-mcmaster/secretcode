#pragma once
#include "profile.h"
#include "../../common/src/organization.h"
namespace p8::common_cause_firewall {
inline bool public_data(p6::Source s) { return s==p6::Source::public_map||s==p6::Source::synthetic_fixture; }
inline bool visits(const Plan& p,p6::Point tile) {
    for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal}) {
        const auto& r=route_for(p,leg); for (int i=0;i<r.count;++i) if (r.tiles[i]==tile) return true;
    }
    return false;
}
inline std::uint64_t uniform_dependency_hash(const Plan& p) {
    if (!route_valid(p.bank)||p.bank.count<2||!route_valid(p.renewal)) return 0;
    auto h=hash_word(hash_word(1469598103934665603ULL,p.group),p.altar);
    h=hash_word(h,p.bank.blocker_assumptions); h=hash_word(h,p.bank.count-1);
    // Different actors assemble/capture on distinct legal tiles. Their bank
    // corridor after the first step and the whole return dependency are shared.
    for (int i=1;i<p.bank.count;++i) { h=hash_word(h,p.bank.tiles[i].x); h=hash_word(h,p.bank.tiles[i].y); }
    return hash_word(h,route_hash(p.renewal));
}
struct Layout {
    Plan field{},spawn_component{},spawn_supported{},uniform{};
    p6::Point spawn_shop{};
    p6::Fact<double> field_work{},spawn_work{};
    p6::Fact<p6::Gems> field_reward{},spawn_reward{};
    bool valid(const DraftRoster& d) const {
        if (!d.valid() || d.champions[0]!=p6::Champion::tavi ||
            !plan_valid(field,d.actors[0]) || field.organization!=Organization::U || field.collector!=d.actors[0] ||
            !plan_valid(spawn_component,d.actors[1]) || spawn_component.organization!=Organization::U ||
            spawn_component.collector!=d.actors[1] || !plan_valid(spawn_supported,d.actors[1]) ||
            spawn_supported.organization!=Organization::H || spawn_supported.collector!=d.actors[1] ||
            !common_participant(spawn_supported,d.actors[2]) || common_participant(spawn_supported,d.actors[0]) ||
            field.group==spawn_component.group || spawn_component.group!=spawn_supported.group ||
            plan_return_hash(spawn_component)!=plan_return_hash(spawn_supported) ||
            !visits(spawn_component,spawn_shop) || !visits(spawn_supported,spawn_shop) || visits(field,spawn_shop) ||
            !plan_valid(uniform,d.actors[0]) || uniform.organization!=Organization::J || uniform.crew_count!=3 ||
            !visits(uniform,spawn_shop) || (uniform.group!=field.group&&uniform.group!=spawn_component.group) ||
            !public_data(field_work.source)||!public_data(spawn_work.source)||!public_data(field_reward.source)||!public_data(spawn_reward.source)||
            !std::isfinite(field_work.value)||field_work.value<=0||field_work.value!=spawn_work.value||
            field_reward.value<=0||field_reward.value!=spawn_reward.value) return false;
        for (int i=0;i<3;++i) if (!common_participant(uniform,d.actors[i])) return false;
        return true;
    }
private:
    static bool common_participant(const Plan& p,int actor) { return crew_index(p,actor)>=0; }
};
} // namespace p8::common_cause_firewall
