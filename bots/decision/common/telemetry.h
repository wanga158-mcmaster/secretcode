#pragma once
#ifndef PLAN3_TELEMETRY_FORMAT_ONLY
#include <cambc.h>
#endif
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace plan3_runtime {
// Formatting is bounded independently of the caller's field count. No locale,
// iostream initialization, or stream allocation is needed for numeric output.
class TelemetryFields {
    std::array<char,512> bytes_{};
    std::size_t size_=0;
    bool overflow_=false;
    TelemetryFields& decimal(double value,unsigned digits) {
        if(overflow_)return *this;
        if(!std::isfinite(value) || std::abs(value)>1e9)return *this<<"null";
        const std::uint64_t scale=digits==3?1000:1000000;
        const auto scaled=static_cast<std::uint64_t>(std::abs(value)*double(scale)+0.5);
        if(value<0 && scaled)*this<<'-';
        *this<<(scaled/scale);
        auto fraction=scaled%scale;
        if(!fraction)return *this;
        char buffer[6];
        for(unsigned i=digits;i>0;--i) {buffer[i-1]=char('0'+fraction%10);fraction/=10;}
        unsigned count=digits;
        while(count && buffer[count-1]=='0')--count;
        return *this<<'.'<<std::string_view(buffer,count);
    }
public:
    bool valid() const { return !overflow_; }
    bool empty() const { return size_==0; }
    std::size_t size() const { return size_; }
    std::string_view view() const { return {bytes_.data(),size_}; }
    // Keep stream-style call sites source compatible. An overflow remains
    // oversized so emit rejects it instead of publishing truncated JSON.
    std::string str() const { return overflow_ ? std::string(513,' ') : std::string(view()); }
    TelemetryFields& operator<<(std::string_view text) {
        if(overflow_)return *this;
        if(text.size()>bytes_.size()-size_) { overflow_=true; return *this; }
        for(char c:text)bytes_[size_++]=c;
        return *this;
    }
    TelemetryFields& operator<<(const std::string& text) { return *this<<std::string_view(text); }
    TelemetryFields& operator<<(const char* text) { return *this<<std::string_view(text?text:"null"); }
    TelemetryFields& operator<<(char value) { return *this<<std::string_view(&value,1); }
    TelemetryFields& operator<<(bool value) { return *this<<(value?"true":"false"); }
    template<class T> requires(std::is_integral_v<T> && !std::is_same_v<T,bool> && !std::is_same_v<T,char>)
    TelemetryFields& operator<<(T value) {
        if(overflow_)return *this;
        char buffer[32];
        const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
        if(result.ec!=std::errc{}) { overflow_=true; return *this; }
        return *this<<std::string_view(buffer,result.ptr-buffer);
    }
    template<class T> requires(std::is_floating_point_v<T>)
    TelemetryFields& operator<<(T value) {
        // Native diagnostics use millionths, never general shortest-float
        // conversion. Exact model calculations and offline exports are separate.
        return decimal(static_cast<double>(value),6);
    }
    // Summary errors are diagnostics, not policy inputs. Round them to .001
    // using bounded integer formatting; shortest binary-float formatting can
    // consume a substantial fraction of a normal 100k-fuel callback in WASI.
    // Unknown/out-of-range values stay null. Trim zeroes to preserve line space.
    TelemetryFields& summary_error(double value) {
        return decimal(value,3);
    }
    TelemetryFields& quoted(std::string_view text) {
        *this<<'"';
        constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:text) {
            if(c=='"' || c=='\\') { *this<<'\\'<<static_cast<char>(c); }
            else if(c<0x20) { *this<<"\\u00"<<hex[c>>4]<<hex[c&15]; }
            else *this<<static_cast<char>(c);
            if(overflow_)break;
        }
        return *this<<'"';
    }
};
inline std::string telemetry_number(double value) { TelemetryFields f; f<<value; return f.str(); }

// One reserved summary line survives detail saturation. Counters live in the
// actor's ordinary private state and keep increasing after details are full.
class Telemetry {
    unsigned lines_=0,details_=0;
    bool context_reserved_=false,context_emitted_=false;
public:
    unsigned turns=0,decisions=0,completed=0,aborted=0,censored=0;
    unsigned opportunities=0,activations=0,disagreements=0;
    unsigned robust_degeneracies=0;
    bool robust_logging=false;
    unsigned capital_unknown=0;
    bool capital_logging=false;
    unsigned details_omitted=0,oversize_omitted=0;
    unsigned captures=0,converted=0,collisions=0,cutoffs=0,preparations=0;
    unsigned two_trip_started=0,two_trip_completed=0,two_trip_aborted=0;
    double duration_error=0,health_error=0,conversion_error=0;
    // Pure formatting entry point also exercises caps without an engine turn.
    void reserve_context_line() { if(lines_==0)context_reserved_=true; }
    bool detail_available() const {
        return lines_<95 && details_<32-unsigned(context_reserved_ && !context_emitted_);
    }
    TelemetryFields format_line(unsigned round,std::string_view actor,std::string_view kind,
                                std::string_view fields,bool final=false,bool reserved_context=false) {
        TelemetryFields line;
        if(lines_>=(final?96u:95u))return line;
        const bool detail=kind!="summary" && kind!="start";
        const bool context=reserved_context && kind=="horizon-context" && context_reserved_ && !context_emitted_;
        if(reserved_context && !context) { ++details_omitted; return line; }
        // 32 details + start + 62 periodic summaries + reserved final = 96.
        // Horizon can reserve one of those same details for its first genuine
        // objective fork, so early candidate logs cannot erase that witness.
        const unsigned detail_limit=32-unsigned(context_reserved_ && !context_emitted_ && !context);
        if(detail && details_>=detail_limit) { ++details_omitted; return line; }
        line<<"P3 {\"schema\":\"p3-telemetry-v1\",\"round\":"<<round<<",\"actor\":";
        line.quoted(actor)<<",\"kind\":";
        line.quoted(kind)<<fields<<'}'<<'\n';
        if(!line.valid()) { ++oversize_omitted; return {}; }
        ++lines_; if(detail)++details_; if(context)context_emitted_=true;
        return line;
    }
    TelemetryFields summary_fields(bool final=false) const {
        TelemetryFields f;
        f<<",\"turns\":"<<turns<<",\"decisions\":"<<decisions
         <<",\"completed\":"<<completed<<",\"aborted\":"<<aborted
         <<",\"censored\":"<<censored<<",\"captures\":"<<captures
         <<",\"opportunities\":"<<opportunities<<",\"activations\":"<<activations
         <<",\"disagreements\":"<<disagreements
         <<",\"details_omitted\":"<<details_omitted<<",\"oversize_omitted\":"<<oversize_omitted
         <<",\"converted\":"<<converted<<",\"hidden_collisions\":"<<collisions
         <<",\"cutoffs\":"<<cutoffs<<",\"preparations\":"<<preparations
         <<",\"two_trip_started\":"<<two_trip_started<<",\"two_trip_completed\":"<<two_trip_completed
         <<",\"two_trip_aborted\":"<<two_trip_aborted<<",\"abs_duration_error\":";
        f.summary_error(duration_error)<<",\"abs_health_error\":";
        f.summary_error(health_error)<<",\"abs_conversion_error\":";
        f.summary_error(conversion_error)<<",\"final\":"<<final;
        if(robust_logging)f<<",\"robust_degeneracies\":"<<robust_degeneracies;
        if(capital_logging)f<<",\"capital_unknown\":"<<capital_unknown;
        return f;
    }
    // sx:1 is the direct Ledger/Atlas preparation diagnostic summary. The
    // caller supplies only PreparationMetrics::snapshot()'s bounded integers;
    // it replaces, rather than adds to, the existing periodic/final frame.
    TelemetryFields sortie_summary_fields(const std::array<std::int64_t,19>& sortie,bool final=false) const {
        TelemetryFields f;
        f<<",\"sx\":1,\"base\":["<<turns<<','<<decisions<<','<<completed<<','<<aborted<<','<<converted<<','<<cutoffs
         <<"],\"sortie\":[";
        for(unsigned i=0;i<sortie.size();++i) {if(i)f<<',';f<<sortie[i];}
        f<<"],\"final\":"<<final;return f;
    }
#ifndef PLAN3_TELEMETRY_FORMAT_ONLY
    void emit(const char* kind,std::string_view fields,bool final=false) {
        const auto line=format_line(cambc::round_number(),cambc::to_string(cambc::self().champion_name()),kind,fields,final);
        if(line.empty())return;
        std::fwrite(line.view().data(),1,line.size(),stdout);
        std::fflush(stdout);
    }
    void summary(bool final=false) {
        if(lines_>=(final?96u:95u) || cambc::fuel_remaining()<30000)return;
        const auto fields=summary_fields(final);
        emit("summary",fields.str(),final);
    }
    void summary_sortie(const std::array<std::int64_t,19>& sortie,bool final=false) {
        if(lines_>=(final?96u:95u) || cambc::fuel_remaining()<30000)return;
        const auto fields=sortie_summary_fields(sortie,final);
        emit("summary",fields.str(),final);
    }
    bool emit_context(std::string_view fields) {
        const auto line=format_line(cambc::round_number(),cambc::to_string(cambc::self().champion_name()),
                                    "horizon-context",fields,false,true);
        if(line.empty())return false;
        std::fwrite(line.view().data(),1,line.size(),stdout); std::fflush(stdout);
        return true;
    }
#endif
};
} // namespace plan3_runtime
