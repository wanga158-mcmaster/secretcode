#pragma once
#include "matching.h"
#include "../common/model.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace plan3_matchmaker {

enum class EdgeReason { unfinished, admitted, late, unreachable, unsafe, unsupported };
struct PublicActor {
    int ordinal = -1;
    double health = 0, strength = 0, focus = 0, campMultiplier = 1, moveSpeed = 0;
};
struct PublicGroup {
    int key = -1;
    std::uint32_t gems = 0;
    unsigned members = 0;
    double health = 0, incoming = 0;
    // SDK physical camp groups are cardinally connected by definition. A
    // non-SDK model producer must establish this precondition explicitly.
    bool connected = false;
};
struct PublicTiming {
    int capture = 0, recall = 0, tributeBase = 0, tributeMinimum = 0;
    double tributeMultiplier = 0, spawnHealing = 0;
};
struct Excursion {
    Cell cell;
    EdgeReason reason = EdgeReason::unfinished;
    int duration = 0;
    double healthLoss = 0;
};

// -2 is unfinished; -1 is a completed static non-route. Only a complete,
// supported safe excursion receives numeric utility. This is an assumed bare
// public model, not a calibrated forecast or proof of safe native combat.
inline Excursion estimate_excursion(const PublicActor& a,const PublicGroup& g,const PublicTiming& t,
                                   int groupDistance,int altarDistance,int spawnRelocation) {
    Excursion out;
    if (groupDistance == -2 || altarDistance == -2 || spawnRelocation == -2) return out;
    out.cell = {CellState::unavailable,0};
    out.reason = EdgeReason::unsupported;
    if (groupDistance < -1 || altarDistance < -1 || spawnRelocation < -1 || !g.connected || !g.members ||
        g.key < 0 || g.gems > unsigned(std::numeric_limits<int>::max()) ||
        !std::isfinite(a.health) || !std::isfinite(a.strength) || !std::isfinite(a.focus) ||
        !std::isfinite(a.campMultiplier) || !std::isfinite(a.moveSpeed) || !std::isfinite(g.health) ||
        !std::isfinite(g.incoming) || !std::isfinite(t.tributeMultiplier) || !std::isfinite(t.spawnHealing) ||
        a.health <= 0 || a.strength < 0 || a.focus < 0 || a.campMultiplier <= 0 || a.moveSpeed <= 0 ||
        g.health < 0 || g.incoming < 0 || t.capture < 0 || t.recall < 0 || t.tributeMinimum < 0 ||
        t.tributeMultiplier < 0 || t.spawnHealing <= 0) return out;
    const double healing=a.health*t.spawnHealing;
    if (!std::isfinite(healing) || healing<=0 ||
        !std::isfinite(g.health+g.incoming+a.strength+a.focus+a.campMultiplier+1.0+a.health+a.health) ||
        !std::isfinite((1.125*a.strength+0.12*a.focus)*a.campMultiplier)) return out;
    // The pinned helper adds two integer exposure turns. Exclude extreme
    // synthetic inputs before that addition could overflow its int domain.
    const double clearTurns=std::ceil(g.health/std::max(1.0,(1.125*a.strength+0.12*a.focus)*a.campMultiplier));
    if (!std::isfinite(clearTurns) || clearTurns>std::numeric_limits<int>::max()-2) return out;
    if (groupDistance < 0 || altarDistance < 0 || spawnRelocation < 0) {
        out.reason = EdgeReason::unreachable;return out;
    }
    const auto clear = plan3::estimate_clear(g.health,g.incoming,a.strength,a.focus,a.campMultiplier,1.0,a.health,a.health);
    out.healthLoss = clear.healthLoss;
    if (!clear.admitted) { out.reason=EdgeReason::unsafe;return out; }
    // Gold-only groups are excluded before the candidate cap. This explicit
    // zero case is useful for model callers, without inventing an empty tribute.
    if (!g.gems) { out.cell={CellState::known,0};out.reason=EdgeReason::admitted;return out; }
    const auto tribute=plan3::tribute_completion_turns(int(g.gems),t.tributeMultiplier,t.tributeBase,t.tributeMinimum);
    if (!tribute) return out;
    const double walk=std::ceil((2.0*groupDistance+altarDistance+2.0*spawnRelocation+2.0*(g.members-1))/a.moveSpeed);
    const double heal=std::max(1.0,std::ceil(clear.healthLoss/healing));
    const double duration=walk+clear.turns+t.capture+1.0+*tribute+t.recall+heal;
    if (!std::isfinite(duration) || duration < 1 || duration > std::numeric_limits<int>::max()) return out;
    out.duration=int(duration);
    out.cell={CellState::known,out.duration <= 128 ? g.gems : 0u};
    out.reason=out.duration <= 128 ? EdgeReason::admitted : EdgeReason::late;
    return out;
}

} // namespace plan3_matchmaker
