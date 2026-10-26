#pragma once
#include "../common/telemetry.h"
namespace plan3_runtime {
// Executor retains the immutable base pointer solely to increment collisions.
// Campaign emissions all use this derived formatter; the base private formatter
// state has no emission path. All public counters, including collisions, are shared.
class IndexerTelemetry : public Telemetry {
    unsigned lines_=0,details_=0;
    bool context_reserved_=false,context_emitted_=false;
public:
    // Pure formatting entry point also exercises caps without an engine turn.
    void reserve_context_line() { if(lines_==0)context_reserved_=true; }
    TelemetryFields format_line(unsigned round,std::string_view actor,std::string_view kind,
                                std::string_view fields,bool final=false,bool reserved_context=false) {
        TelemetryFields line;
        if(lines_>=(final?96u:95u))return line;
        const bool detail=kind!="summary" && kind!="start";
        const bool context=reserved_context && kind=="indexer-context" && context_reserved_ && !context_emitted_;
        if(reserved_context && !context) { ++details_omitted; return line; }
        // 32 details + start + 62 periodic summaries + reserved final = 96.
        // Indexer reserves one existing detail for its first paired context.
        const unsigned detail_limit=32-unsigned(context_reserved_ && !context_emitted_ && !context);
        if(detail && details_>=detail_limit) { ++details_omitted; return line; }
        line<<"P3 {\"schema\":\"p3-telemetry-v1\",\"round\":"<<round<<",\"actor\":";
        line.quoted(actor)<<",\"kind\":";
        line.quoted(kind)<<fields<<'}'<<'\n';
        if(!line.valid()) { ++oversize_omitted; return {}; }
        ++lines_; if(detail)++details_; if(context)context_emitted_=true;
        return line;
    }
#ifndef PLAN3_TELEMETRY_FORMAT_ONLY
    void emit(const char* kind,std::string_view fields,bool final=false) {
        const auto line=format_line(cambc::round_number(),cambc::to_string(cambc::self().champion_name()),kind,fields,final);
        if(line.empty())return;
        std::fwrite(line.view().data(),1,line.size(),stdout);
        std::fflush(stdout);
    }
    void summary(bool final=false) {
        const auto fields=summary_fields(final);
        emit("summary",fields.str(),final);
    }
    bool emit_context(std::string_view fields) {
        const auto line=format_line(cambc::round_number(),cambc::to_string(cambc::self().champion_name()),
                                    "indexer-context",fields,false,true);
        if(line.empty())return false;
        std::fwrite(line.view().data(),1,line.size(),stdout); std::fflush(stdout);
        return true;
    }
#endif
};
} // namespace plan3_runtime
