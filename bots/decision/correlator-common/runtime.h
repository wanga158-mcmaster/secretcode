#pragma once
#include "../common/runtime.h"
#include "draft.h"

namespace plan3_correlator {
inline int run(bool control,const char* profile) {
    plan3_runtime::Campaign bot(plan3::Controller::market,false,profile);
    auto draft=std::make_unique<Draft>(control);bool supplied=false;
    while(true) {
        try {
            if(cambc::is_draft()) {draft->step();plan3_runtime::fixed_draft();}
            else {
                if(!supplied) {
                    const auto seal=read_seal(control,unsigned(cambc::champions(cambc::team::ally).size()));
                    const int actor=int(cambc::self().champion_name());
                    const auto plan=actor_plan(seal,actor);const bool governing=plan.complete;
                    bot.supply_market_plan(plan);supplied=true;
                    plan3_runtime::TelemetryFields fields;
                    fields<<",\"family\":\"correlator\",\"arm\":"<<unsigned(control)
                        <<",\"model\":\"group-max-once128\",\"governing_roles\":"<<governing
                        <<",\"safe_fallback\":"<<!governing
                        <<",\"marginals\":\"intended-roles\",\"entropy\":\"WASI-assumed\",\"calibrated\":false";
                    if(governing)fields<<",\"ticket\":"<<seal.ticket<<",\"matrix\":"<<seal.matrixFingerprint
                        <<",\"table\":"<<table_fingerprint(seal.policy)<<",\"tuples\":"<<seal.policy.tupleCount
                        <<",\"joint\":["<<seal.policy.jointNumerator<<",2],\"independent\":["
                        <<seal.policy.independentNumerator<<','<<seal.policy.tupleCount<<']';
                    bot.report_policy(fields.str());draft.reset();
                }
                bot.turn();
            }
        } catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
          catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_correlator
