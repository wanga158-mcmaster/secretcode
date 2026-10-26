#pragma once
#include "native_service_navigation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace mosaic::native_service {

struct RawCampPotential {std::uint64_t amount{};bool valid{};};
struct ExitWitnessTag {
    Epoch epoch{};Point origin{};std::uint64_t field_generation{},semantic_intent{};
    friend constexpr bool operator==(ExitWitnessTag,ExitWitnessTag)=default;
};
enum class ExitWitnessStatus : std::uint8_t {unknown,quiet,path,unsupported,invalid};
struct ExitWitness {
    static constexpr unsigned max_steps=32;
    ExitWitnessTag tag{};ExitWitnessStatus status{ExitWitnessStatus::unknown};
    std::array<Point,max_steps> path{};unsigned steps{},source_samples{};
    std::uint64_t melee_phases{},ranged_phases{},max_origin_phase{},melee_with_refusal{},ranged_with_refusal{};
    bool saturated{};
    constexpr bool complete()const noexcept {return status==ExitWitnessStatus::quiet||status==ExitWitnessStatus::path;}
    constexpr bool prices_complete()const noexcept {return complete()&&!saturated;}
    constexpr bool matches(ExitWitnessTag actual)const noexcept {return tag==actual;}
    constexpr std::optional<Point> next_point()const noexcept {
        return steps?std::optional<Point>{path[0]}:std::nullopt;
    }
};

// Narrow dependency lemma for a SMALL stored route after its Navigation
// borrower has been retired: opening a camp-overlay cell cannot invalidate a
// prior landing, and a newly excluded cell outside the landing footprint is
// irrelevant to this route. Static terrain/source values must remain unchanged;
// no future occupancy or currently edited Navigation distance is asserted.
constexpr bool retained_exit_survives_camp_edit(const ExitWitness& witness,Point cell,bool excluded)noexcept {
    if(!witness.prices_complete()||witness.steps>ExitWitness::max_steps)return false;
    if(excluded)for(unsigned i=0;i<witness.steps;++i)if(witness.path[i]==cell)return false;
    return true;
}

// A complete row-major static source. It is separate from Navigation's borrowed
// immutable passability board; no missing/out-of-bounds sample means zero.
template<class Amount=std::uint64_t> struct RawPotentialSpan {
    static_assert(std::is_integral_v<Amount>&&std::is_unsigned_v<Amount>&&sizeof(Amount)<=sizeof(std::uint64_t));
    std::span<const Amount> values{};int width{},height{};
    constexpr RawCampPotential operator()(Point point)const noexcept {
        if(width<=0||height<=0||point.x<0||point.y<0||point.x>=width||point.y>=height ||
           std::size_t(width)>std::numeric_limits<std::size_t>::max()/std::size_t(height))return {};
        const auto cells=std::size_t(width)*std::size_t(height);
        if(values.size()<cells)return {};
        return {std::uint64_t(values[std::size_t(point.y)*std::size_t(width)+std::size_t(point.x)]),true};
    }
};

namespace exit_witness_detail {
inline std::uint64_t add(std::uint64_t a,std::uint64_t b,bool& saturated)noexcept {
    if(b>std::numeric_limits<std::uint64_t>::max()-a){saturated=true;return std::numeric_limits<std::uint64_t>::max();}
    return a+b;
}
inline bool finished(NavigationStatus status)noexcept {
    return status==NavigationStatus::ready||status==NavigationStatus::unreachable||status==NavigationStatus::failed;
}
}

// This is geometry and raw camp-phase arithmetic, never SDK permission, HP
// safety, an occupancy assertion or Recall availability. The caller owns the
// immutable field/source and supplies their real semantic generation/intent;
// Navigation does not expose those tags for this helper to verify. Reuse only
// when matches() holds for fresh actual dependencies, and SDK-check every edge.
//
// Each already available origin has final BFS distance even while the global
// field is Preparing. At most32 strict descents and eight source samples per
// descent; ties follow Navigation's stable compass order. Stop on first zero
// potential, including a quiet perch reached before the field's home root.
//
// Melee can move in this input: sum phases at destination poses (quiet adds0).
// A ranged work input must additionally survive one origin phase before its
// next-input move. One explicitly bounded refusal contingency adds the maximum
// phase at any before-move pose. This prices at most ONE extra phase; it does
// not assert that a FALSE SDK move necessarily costs a phase or grants retry.
// Incomplete/invalid outputs retain only a nomination prefix, not a full price.
template<std::size_t Cells,class Source>
ExitWitness build_exit_witness(const Navigation<Cells>& navigation,ExitWitnessTag tag,Source source,
                              unsigned step_cap=ExitWitness::max_steps,bool source_complete=true) {
    ExitWitness out;out.tag=tag;
    if(tag.epoch.round<0||tag.origin.x<0||tag.origin.y<0||!tag.field_generation||!tag.semantic_intent){
        out.status=ExitWitnessStatus::invalid;return out;
    }
    if(step_cap>ExitWitness::max_steps){out.status=ExitWitnessStatus::unsupported;return out;}
    if(!source_complete)return out;
    auto unavailable=[&]{return exit_witness_detail::finished(navigation.status())?
        ExitWitnessStatus::unsupported:ExitWitnessStatus::unknown;};
    if(!navigation.available(tag.origin)){out.status=unavailable();return out;}
    ++out.source_samples;auto current_potential=source(tag.origin);
    if(!current_potential.valid){out.status=ExitWitnessStatus::invalid;return out;}
    const auto origin_potential=current_potential.amount;
    auto finish=[&](ExitWitnessStatus status){
        out.status=status;
        out.ranged_phases=exit_witness_detail::add(origin_potential,out.melee_phases,out.saturated);
        out.melee_with_refusal=exit_witness_detail::add(out.melee_phases,out.max_origin_phase,out.saturated);
        out.ranged_with_refusal=exit_witness_detail::add(out.ranged_phases,out.max_origin_phase,out.saturated);
        return out;
    };
    if(!origin_potential)return finish(ExitWitnessStatus::quiet);
    Point current=tag.origin;
    while(out.steps<step_cap){
        const auto before=navigation.distance(current);
        if(!before)return finish(unavailable());
        std::array<Point,8> nominees{};
        const auto count=navigation.nominees(current,{},nominees);
        if(!count)return finish(unavailable());
        unsigned best=0;std::uint64_t best_potential{};
        for(unsigned n=0;n<count;++n){
            const auto after=navigation.distance(nominees[n]);
            if(!after||*after>=*before)return finish(ExitWitnessStatus::invalid);
            ++out.source_samples;const auto potential=source(nominees[n]);
            if(!potential.valid)return finish(ExitWitnessStatus::invalid);
            if(n==0||potential.amount<best_potential){best=n;best_potential=potential.amount;}
        }
        out.max_origin_phase=std::max(out.max_origin_phase,current_potential.amount);
        current=nominees[best];out.path[out.steps++]=current;
        out.melee_phases=exit_witness_detail::add(out.melee_phases,best_potential,out.saturated);
        current_potential={best_potential,true};
        if(!best_potential)return finish(ExitWitnessStatus::path);
    }
    return finish(ExitWitnessStatus::unsupported);
}

} // namespace mosaic::native_service
