#pragma once
#include "../../tempo/src/policy.h"
#include "escape.h"

namespace p11_refuge {
// Keep tempo's financial, bank and retreat decisions controlled. Only the
// bounded recovery adapter's local escape decision changes.
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-refuge";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool event_escape=true;
    using EscapeThreat=Threat;
    using EscapeTile=Tile;
    static int choose_escape(float hp,std::span<const Tile> tiles,std::span<const Threat> threats) {
        return p11_refuge::choose_escape(hp,tiles,threats);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
};
} // namespace p11_refuge
