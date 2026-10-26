#pragma once
#include "../common/runtime.h"
#include "draft.h"

namespace plan3_assignment {
// One shared Market executor. Only the published draft allocation objective
// differs; no per-actor live information or private ally resources are shared.
inline int run(Policy policy,const char* profile) {
    plan3_runtime::Campaign bot(plan3::Controller::market,false,profile);
    auto allocation=std::make_unique<DraftAllocator>(policy);
    bool supplied=false;
    while(true) {
        try {
            if(cambc::is_draft()) {
                const auto status=allocation->step();
                std::fprintf(stdout,"P3_ASSIGNMENT {\"phase\":\"draft\",\"policy\":\"%s\",\"status\":%u,\"groups\":%u,\"truncated\":%u,\"work\":%u,\"matrix\":%u,\"assumed\":true}\n",
                    policy_name(policy),unsigned(status),allocation->considered_groups(),allocation->truncated_groups(),
                    allocation->last_work_units(),allocation->matrix_fingerprint());
                plan3_runtime::fixed_draft();
            } else {
                if(!supplied) {
                    const auto plan=read_plan(policy,unsigned(cambc::champions(cambc::team::ally).size()));
                    bot.supply_market_plan(ownership_plan(plan));supplied=true;
                    bot.report_policy(adoption_fields(plan,policy));
                    // Do not retain draft buffers/kernels throughout gameplay.
                    allocation.reset();
                }
                bot.turn();
            }
        } catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
          catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_assignment
