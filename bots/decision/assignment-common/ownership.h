#pragma once
#include "../common/allocation-wire.h"
#include "wire.h"
#include <cstdio>
#include <string>

namespace plan3_assignment {
inline plan3_runtime::MarketPlan ownership_plan(const DraftPlan& plan) {
    if (!valid_plan(plan) || !plan.feasible) return {};
    plan3_runtime::MarketPlan result;
    result.complete=true;result.width=plan.width;result.height=plan.height;
    result.actorOrdinals=plan.actors;result.groupIds=plan.groups;result.ownerOrdinals=plan.owners;
    return result;
}
inline std::string adoption_fields(const DraftPlan& plan,Policy policy) {
    const bool available=plan.complete && plan.feasible;char fields[384];
    const int length=std::snprintf(fields,sizeof(fields),",\"phase\":\"adopt\",\"policy\":\"%s\",\"complete\":%s,\"feasible\":%s,\"governing_assignment\":%s,\"safe_fallback\":%s,\"groups\":%zu,\"matrix\":%u,\"horizon\":128,\"idle_disagreement\":%s,\"assumed\":true",
        policy_name(policy),plan.complete?"true":"false",plan.feasible?"true":"false",available?"true":"false",available?"false":"true",
        plan.groups.size(),plan.matrixFingerprint,bargaining(policy)?"0":"null");
    return length>=0 && unsigned(length)<sizeof(fields)?std::string(fields,unsigned(length)):std::string{};
}
} // namespace plan3_assignment
