#pragma once
// A bounded camp-only staging quote, not an SDK legality/survival certificate.
// Caller supplies ALL static harmful supports, including sleeping/dated-dead
// members, and <=8 freshly runtime-legal adjacent candidates. Movement allowance,
// full personal occupancy/teleports, current epoch, bounded actuator/yield tail
// (including the existing empirical reserve) and PvP remain caller obligations.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace wb::staging {

inline constexpr std::size_t max_supports=256;
inline constexpr std::size_t max_landings=8;
inline constexpr double max_exact_float_integer=16777216.0;

struct Support {
    int x{},y{},range{};
    double raw_damage{}; // Canonical 20/60/120/200 raw hit, or zero harmless.
};
struct Landing { int x{},y{}; };

enum class Kind {
    AlreadyQuiet, Chosen, NoRepresentedQuietLanding, UnsafeOriginFailure, Unsupported
};

struct Result {
    Kind kind{Kind::Unsupported};
    Landing landing{};
    int candidate_index{-1};
    // Valid only for a non-Unsupported result. Never treat default zeros as a
    // successful quote. Failed movement remains at origin for one camp phase.
    double origin_raw{},failure_remaining{};
};

inline bool covers(const Support& support,Landing p) {
    const auto dx=static_cast<std::int64_t>(p.x)-support.x;
    const auto dy=static_cast<std::int64_t>(p.y)-support.y;
    const auto r=static_cast<std::int64_t>(support.range);
    return dx>=-r&&dx<=r&&dy>=-r&&dy<=r;
}

inline Result quote(int origin_x,int origin_y,double fresh_hp,
                    std::span<const Support> supports,
                    std::span<const Landing> legal_adjacent) {
    if(origin_x<0||origin_y<0||!std::isfinite(fresh_hp)||fresh_hp<=0
        ||fresh_hp>max_exact_float_integer||static_cast<double>(static_cast<float>(fresh_hp))!=fresh_hp
        ||supports.size()>max_supports||legal_adjacent.size()>max_landings) return {};
    const Landing origin{origin_x,origin_y};
    for(std::size_t i=0;i<legal_adjacent.size();++i) {
        const auto p=legal_adjacent[i];
        if(p.x<0||p.y<0) return {};
        const auto dx=static_cast<std::int64_t>(p.x)-origin_x;
        const auto dy=static_cast<std::int64_t>(p.y)-origin_y;
        if(dx<-1||dx>1||dy<-1||dy>1||(dx==0&&dy==0)) return {};
        for(std::size_t j=0;j<i;++j)
            if(p.x==legal_adjacent[j].x&&p.y==legal_adjacent[j].y) return {};
    }
    std::array<bool,max_landings> quiet{};quiet.fill(true);
    double origin_raw=0;
    // One <=256-member pass: origin reserve plus <=8*members endpoint tests.
    // No SDK imports, map allocation, route search, sleep/life filtering or HP pool.
    for(const auto& support:supports) {
        if(support.x<0||support.y<0||support.range<0
            ||(support.raw_damage!=0&&support.raw_damage!=20&&support.raw_damage!=60
               &&support.raw_damage!=120&&support.raw_damage!=200)) return {};
        if(support.raw_damage==0) continue;
        if(covers(support,origin)) {
            if(origin_raw>max_exact_float_integer-support.raw_damage) return {};
            origin_raw+=support.raw_damage;
            if(!std::isfinite(origin_raw)) return {};
        }
        for(std::size_t i=0;i<legal_adjacent.size();++i)
            if(quiet[i]&&covers(support,legal_adjacent[i])) quiet[i]=false;
    }
    Result out;out.origin_raw=origin_raw;out.failure_remaining=fresh_hp-origin_raw;
    // Supported HP is exactly float32 and <=2^24; supported hits/sum are exact
    // integers in that range. When HP>sum, every positive subtraction stays on
    // the original float32 lattice and is exact. Arbitrary/buffed raw values,
    // non-native HP or larger domains return Unsupported, not infeasible.
    if(origin_raw==0) {out.kind=Kind::AlreadyQuiet;out.landing=origin;return out;}
    if(!(fresh_hp>origin_raw)) {out.kind=Kind::UnsafeOriginFailure;return out;}
    for(std::size_t i=0;i<legal_adjacent.size();++i) if(quiet[i]) {
        const auto p=legal_adjacent[i];
        if(out.candidate_index<0||p.y<out.landing.y||(p.y==out.landing.y&&p.x<out.landing.x)) {
            out.landing=p;out.candidate_index=static_cast<int>(i);
        }
    }
    out.kind=out.candidate_index<0 ? Kind::NoRepresentedQuietLanding : Kind::Chosen;
    // NoRepresentedQuietLanding describes only this <=8-element input menu.
    // Chosen prices one failure volley, not enemy/spell/displacement responses.
    return out;
}

} // namespace wb::staging
