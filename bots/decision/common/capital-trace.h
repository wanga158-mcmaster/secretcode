#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace plan3_runtime {
// Diagnostics only. These describe a returned branch, never a unit's cost or
// the cause of a nested cutoff. They add no SDK query or telemetry record.
enum class CapitalSite : std::uint8_t {
    none,catalogue,entry,bankPrefix,returnRoute,deferBefore,deferAfter,funding,
    target,gear,loopsBefore,loopsAfter,proposalBefore,proposalAfter,
    finalBefore,finalContainer,finalActive,finalControl
};
enum class CapitalCause : std::uint8_t { none,metadata,cutoff,fuel,route };
enum class CapitalUnknown : std::uint8_t {
    none,catalogueMetadata,entryCutoff,entryFuel,bankPrefixCutoff,returnRouteUnknown,
    deferBeforeCutoff,deferBeforeFuel,deferAfterCutoff,deferAfterFuel,
    fundingCutoff,fundingFuel,targetCutoff,targetFuel,gearCutoff,gearFuel,
    loopsBeforeCutoff,loopsBeforeFuel,loopsAfterCutoff,loopsAfterFuel,
    proposalBeforeCutoff,proposalBeforeFuel,proposalAfterCutoff,proposalAfterFuel,
    finalBeforeFuel,finalContainerFuel,finalActiveFuel,finalControlFuel,count
};
struct CapitalTraceDescriptor {
    CapitalSite site;
    CapitalCause cause;
    std::string_view label;
    const char* fragment;
};
#define P3_CAPITAL_TRACE(site,cause,label) CapitalTraceDescriptor{CapitalSite::site,CapitalCause::cause,label,",\"provenance\":\"" label "\""}
inline constexpr std::array capital_trace_descriptors{
    P3_CAPITAL_TRACE(none,none,"analytical-capital-unknown"),
    P3_CAPITAL_TRACE(catalogue,metadata,"analytical-cap-Mm"),
    P3_CAPITAL_TRACE(entry,cutoff,"analytical-cap-Ec"),
    P3_CAPITAL_TRACE(entry,fuel,"analytical-cap-Ef"),
    P3_CAPITAL_TRACE(bankPrefix,cutoff,"analytical-cap-Bc"),
    P3_CAPITAL_TRACE(returnRoute,route,"analytical-cap-Ru"),
    P3_CAPITAL_TRACE(deferBefore,cutoff,"analytical-cap-D0c"),
    P3_CAPITAL_TRACE(deferBefore,fuel,"analytical-cap-D0f"),
    P3_CAPITAL_TRACE(deferAfter,cutoff,"analytical-cap-D1c"),
    P3_CAPITAL_TRACE(deferAfter,fuel,"analytical-cap-D1f"),
    P3_CAPITAL_TRACE(funding,cutoff,"analytical-cap-Fc"),
    P3_CAPITAL_TRACE(funding,fuel,"analytical-cap-Ff"),
    P3_CAPITAL_TRACE(target,cutoff,"analytical-cap-G0c"),
    P3_CAPITAL_TRACE(target,fuel,"analytical-cap-G0f"),
    P3_CAPITAL_TRACE(gear,cutoff,"analytical-cap-G1c"),
    P3_CAPITAL_TRACE(gear,fuel,"analytical-cap-G1f"),
    P3_CAPITAL_TRACE(loopsBefore,cutoff,"analytical-cap-L0c"),
    P3_CAPITAL_TRACE(loopsBefore,fuel,"analytical-cap-L0f"),
    P3_CAPITAL_TRACE(loopsAfter,cutoff,"analytical-cap-L1c"),
    P3_CAPITAL_TRACE(loopsAfter,fuel,"analytical-cap-L1f"),
    P3_CAPITAL_TRACE(proposalBefore,cutoff,"analytical-cap-T0c"),
    P3_CAPITAL_TRACE(proposalBefore,fuel,"analytical-cap-T0f"),
    P3_CAPITAL_TRACE(proposalAfter,cutoff,"analytical-cap-T1c"),
    P3_CAPITAL_TRACE(proposalAfter,fuel,"analytical-cap-T1f"),
    P3_CAPITAL_TRACE(finalBefore,fuel,"analytical-cap-P0f"),
    P3_CAPITAL_TRACE(finalContainer,fuel,"analytical-cap-P1f"),
    P3_CAPITAL_TRACE(finalActive,fuel,"analytical-cap-P2f"),
    P3_CAPITAL_TRACE(finalControl,fuel,"analytical-cap-P3f")
};
#undef P3_CAPITAL_TRACE
static_assert(capital_trace_descriptors.size()==static_cast<unsigned>(CapitalUnknown::count));
static_assert([] { for(const auto& d:capital_trace_descriptors)if(d.label.size()>26)return false;return true; }());
constexpr CapitalUnknown capital_guard_unknown(CapitalSite site,bool cutoff) {
    switch(site) {
    case CapitalSite::entry:return cutoff?CapitalUnknown::entryCutoff:CapitalUnknown::entryFuel;
    case CapitalSite::deferBefore:return cutoff?CapitalUnknown::deferBeforeCutoff:CapitalUnknown::deferBeforeFuel;
    case CapitalSite::deferAfter:return cutoff?CapitalUnknown::deferAfterCutoff:CapitalUnknown::deferAfterFuel;
    case CapitalSite::funding:return cutoff?CapitalUnknown::fundingCutoff:CapitalUnknown::fundingFuel;
    case CapitalSite::target:return cutoff?CapitalUnknown::targetCutoff:CapitalUnknown::targetFuel;
    case CapitalSite::gear:return cutoff?CapitalUnknown::gearCutoff:CapitalUnknown::gearFuel;
    case CapitalSite::loopsBefore:return cutoff?CapitalUnknown::loopsBeforeCutoff:CapitalUnknown::loopsBeforeFuel;
    case CapitalSite::loopsAfter:return cutoff?CapitalUnknown::loopsAfterCutoff:CapitalUnknown::loopsAfterFuel;
    case CapitalSite::proposalBefore:return cutoff?CapitalUnknown::proposalBeforeCutoff:CapitalUnknown::proposalBeforeFuel;
    case CapitalSite::proposalAfter:return cutoff?CapitalUnknown::proposalAfterCutoff:CapitalUnknown::proposalAfterFuel;
    default:return CapitalUnknown::none;
    }
}
struct CapitalTrace {
    CapitalUnknown code=CapitalUnknown::none;
    constexpr void latch(CapitalUnknown value) { if(code==CapitalUnknown::none)code=value; }
    constexpr void final_failure(CapitalUnknown value) { code=value; }
    constexpr const char* fragment() const { return capital_trace_descriptors[static_cast<unsigned>(code)].fragment; }
};
static_assert(sizeof(CapitalTrace)==1);
} // namespace plan3_runtime
