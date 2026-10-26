#pragma once

namespace h12_active_surveyor {
struct SourceTotals { float health=0,incoming=0; };

// Partial views use the public full-group estimate. Do not query live members
// whose totals cannot enter the decision; large partial groups need that fuel
// to reach their unseen members.
template<class Camps,class Member,class Alive,class Health,class Strength>
SourceTotals source_totals(bool fully_visible,SourceTotals original,const Camps&camps,
    Member member,Alive alive,Health health,Strength strength) {
    if(!fully_visible)return original;
    SourceTotals result;
    for(const auto&c:camps)if(member(c)&&alive(c)) {
        result.health+=health(c);result.incoming+=strength(c);
    }
    return result;
}
} // namespace h12_active_surveyor
