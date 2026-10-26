#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace plan3_runtime {

// Frozen draft-only protocol. This is not a live team message format. Owners
// are physical champion ordinals, never draft-slot or hidden-runtime handles.
struct MarketPlan {
    bool complete = false;
    unsigned width = 0, height = 0;
    std::vector<int> groupIds;
    std::vector<int> ownerOrdinals;
    std::vector<int> actorOrdinals;
    int owner(int groupId) const {
        if (!complete) return -1;
        const auto it = std::lower_bound(groupIds.begin(), groupIds.end(), groupId);
        return it == groupIds.end() || *it != groupId ? -1 : ownerOrdinals[it-groupIds.begin()];
    }
};
using MarketBytes = std::array<std::byte, 1024>;

inline std::uint32_t market_checksum(std::span<const std::byte> bytes) {
    std::uint32_t value = 2166136261u;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const unsigned b = n >= 20 && n < 24 ? 0u : std::to_integer<unsigned>(bytes[n]);
        value = (value ^ b) * 16777619u;
    }
    return value;
}
inline bool valid_market_plan(const MarketPlan& plan) {
    if (!plan.complete || !plan.width || !plan.height || plan.width > 100 || plan.height > 100 ||
        plan.groupIds.size() > 64 || plan.groupIds.size() != plan.ownerOrdinals.size() ||
        plan.actorOrdinals.size() < 3 || plan.actorOrdinals.size() > 5) return false;
    for (std::size_t n = 0; n < plan.actorOrdinals.size(); ++n)
        if (plan.actorOrdinals[n] < 0 || plan.actorOrdinals[n] >= 12 ||
            (n && plan.actorOrdinals[n-1] >= plan.actorOrdinals[n])) return false;
    for (std::size_t n = 0; n < plan.groupIds.size(); ++n) {
        if (plan.groupIds[n] < 0 || unsigned(plan.groupIds[n]) >= plan.width*plan.height ||
            (n && plan.groupIds[n-1] >= plan.groupIds[n])) return false;
        if (plan.ownerOrdinals[n] != -1 &&
            !std::binary_search(plan.actorOrdinals.begin(), plan.actorOrdinals.end(), plan.ownerOrdinals[n])) return false;
    }
    return true;
}
inline std::optional<MarketBytes> encode_market_plan(const MarketPlan& plan) {
    if (!valid_market_plan(plan)) return std::nullopt;
    MarketBytes bytes{};
    auto put = [&](unsigned at, unsigned value) { bytes[at] = std::byte(value & 255u); };
    auto put16 = [&](unsigned at, unsigned value) { put(at,value); put(at+1,value>>8); };
    put(0,'P'); put(1,'3'); put(2,'M'); put(3,'K');
    put(4,1); put(5,1); // Wire version, complete flag.
    put16(6,static_cast<unsigned>(plan.groupIds.size()));
    put(8,static_cast<unsigned>(plan.actorOrdinals.size()));
    put16(10,plan.width); put16(12,plan.height); put16(14,1024);
    put(16,1); // p3-analytical-v1 allocation model tag.
    for (unsigned n = 0; n < plan.actorOrdinals.size(); ++n) put(24+n,plan.actorOrdinals[n]);
    for (unsigned n = 0; n < plan.groupIds.size(); ++n) {
        put16(32+n*4,static_cast<unsigned>(plan.groupIds[n]));
        put(34+n*4,plan.ownerOrdinals[n] < 0 ? 255u : unsigned(plan.ownerOrdinals[n]));
    }
    const auto check = market_checksum(bytes);
    for (unsigned n=0;n<4;++n) put(20+n,check>>(n*8));
    return bytes;
}
inline MarketPlan decode_market_plan(std::span<const std::byte> bytes,
                                     unsigned expectedWidth, unsigned expectedHeight) {
    MarketPlan invalid;
    if (bytes.size() != 1024) return invalid;
    auto get = [&](unsigned at) { return std::to_integer<unsigned>(bytes[at]); };
    auto get16 = [&](unsigned at) { return get(at) | (get(at+1)<<8); };
    if (get(0)!='P' || get(1)!='3' || get(2)!='M' || get(3)!='K' || get(4)!=1 ||
        get(5)!=1 || get16(6)>64 || get(8)<3 || get(8)>5 || get16(10)!=expectedWidth ||
        get16(12)!=expectedHeight || get16(14)!=1024 || get(16)!=1) return invalid;
    std::uint32_t check = 0;
    for (unsigned n=0;n<4;++n) check |= std::uint32_t(get(20+n))<<(n*8);
    if (check != market_checksum(bytes)) return invalid;
    MarketPlan plan;
    plan.complete = true; plan.width = expectedWidth; plan.height = expectedHeight;
    for (unsigned n=0;n<get(8);++n) plan.actorOrdinals.push_back(int(get(24+n)));
    for (unsigned n=0;n<get16(6);++n) {
        plan.groupIds.push_back(int(get16(32+n*4)));
        plan.ownerOrdinals.push_back(get(34+n*4)==255 ? -1 : int(get(34+n*4)));
    }
    // Require the canonical zero padding as well as a valid checksum. This
    // prevents unversioned trailing data from silently becoming a new protocol.
    const auto canonical = encode_market_plan(plan);
    if (!canonical || !std::equal(bytes.begin(),bytes.end(),canonical->begin())) return invalid;
    return plan;
}

} // namespace plan3_runtime
