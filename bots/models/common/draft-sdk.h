#pragma once
#include "cambc.h"
#include "draft.h"
#include <stdexcept>

namespace plan4::drafting::sdk {

// Detect the actual compiled SDK pool. Old frozen SDKs have eleven picks;
// 3.0.2 adds Heart of Ruin without changing the earlier draft enum values.
template<class Draft = cambc::champion_draft>
constexpr std::uint16_t legal_mask() {
    if constexpr (requires { Draft::heart_of_ruin; }) return all_mask;
    else return std::uint16_t(all_mask & ~(1u << unsigned(Pick::heart_of_ruin)));
}

template<class Draft = cambc::champion_draft>
constexpr Pick from_sdk(Draft value) {
    switch (value) {
        case Draft::ryth: return Pick::ryth;
        case Draft::vesper_vale: return Pick::vesper_vale;
        case Draft::vendetta: return Pick::vendetta;
        case Draft::cassi_and_issac: return Pick::cassi_and_issac;
        case Draft::stellar: return Pick::stellar;
        case Draft::prism: return Pick::prism;
        case Draft::marcella: return Pick::marcella;
        case Draft::willow: return Pick::willow;
        case Draft::ren: return Pick::ren;
        case Draft::varg: return Pick::varg;
        case Draft::tavi: return Pick::tavi;
        default: break;
    }
    if constexpr (requires { Draft::heart_of_ruin; })
        if (value == Draft::heart_of_ruin) return Pick::heart_of_ruin;
    return Pick::count;
}

template<class Draft = cambc::champion_draft>
Draft to_sdk(Pick value) {
    switch (value) {
        case Pick::ryth: return Draft::ryth;
        case Pick::vesper_vale: return Draft::vesper_vale;
        case Pick::vendetta: return Draft::vendetta;
        case Pick::cassi_and_issac: return Draft::cassi_and_issac;
        case Pick::stellar: return Draft::stellar;
        case Pick::prism: return Draft::prism;
        case Pick::marcella: return Draft::marcella;
        case Pick::willow: return Draft::willow;
        case Pick::ren: return Draft::ren;
        case Pick::varg: return Draft::varg;
        case Pick::tavi: return Draft::tavi;
        case Pick::heart_of_ruin:
            if constexpr (requires { Draft::heart_of_ruin; }) return Draft::heart_of_ruin;
            break;
        case Pick::count: break;
    }
    throw std::runtime_error("plan4 draft pick is outside the compiled SDK pool");
}

// The draft number is a PICK count, not an actor count. Enemy selections do
// not remove our choices. The callback observes a validated choice before the
// one engine action; it cannot change the selector's inputs or chosen pick.
template<class Diagnostic>
bool run(Profile profile, Diagnostic diagnostic) {
    Input input;
    input.team_size = int(cambc::draft::team_size());
    input.available_mask = legal_mask();
    if (input.team_size < 1 || input.team_size > pick_count)
        throw std::runtime_error("plan4 draft selection count is outside the supported pool");
    const auto selected = cambc::draft::current_draft(cambc::team::ally);
    input.selected_count = int(selected.size());
    if (selected.size() <= input.selected.size())
        for (std::size_t n = 0; n < selected.size(); ++n) input.selected[n] = from_sdk(selected[n]);
    const auto decision = choose(profile, input);
    if (!decision.valid) throw std::runtime_error("plan4 draft observation is malformed");
    if (!decision.has_pick) return false;
    const auto requested = to_sdk(decision.pick);
    diagnostic(profile, requested, input.selected_count, input.team_size);
    cambc::draft::pick(requested);
    return true;
}
} // namespace plan4::drafting::sdk
