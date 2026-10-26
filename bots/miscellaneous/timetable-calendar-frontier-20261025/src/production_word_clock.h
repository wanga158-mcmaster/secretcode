#pragma once
// Readiness dependencies of the Producer's represented stationary word.
// This is a projection for a PRIVATE programme copy, never an alteration of
// the actual SDK snapshot or of all-slot self/cooldown freshness validation.
#include "service_model.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tt::production_word_clock {
namespace service=mb::service;
inline constexpr std::size_t slots=service::spell_slots;
static_assert(slots<=8,"represented dependency mask must cover every Producer slot");
// Ready certifies only this clock projection, not Producer/body/word validity.
enum class Status { Ready,InvalidKind,Overflow };
struct Dependencies {
    Status status{Status::Ready};std::uint8_t mask{};
    bool uses(std::size_t slot) const {return slot<slots&&(mask&(std::uint8_t{1}<<slot))!=0;}
};
inline Dependencies dependency_mask(const std::array<service::Spell,slots>& spells) {
    Dependencies result;
    for(std::size_t slot=0;slot<slots;++slot) {
        switch(spells[slot].kind) {
        case service::SpellKind::none:break;
        case service::SpellKind::damage:case service::SpellKind::aoe:case service::SpellKind::cc:
        case service::SpellKind::reset:case service::SpellKind::shield:
            result.mask|=static_cast<std::uint8_t>(std::uint8_t{1}<<slot);break;
        default:result.status=Status::InvalidKind;return result;
        }
    }
    return result;
}
struct Clock {
    Status status{Status::Ready};std::uint8_t dependencies{};
    std::array<std::uint32_t,slots> cooldown{},ready_at{};
    std::uint32_t earliest_ready{};
};
// Basic has no spell cooldown: its actual M/A/C and ordinary-walk history are
// separate Producer/body inputs and remain unchanged. Every non-none current
// Producer kind is included, even if its ready flag, damage or utility is zero:
// readiness may enable a later legal cast/reset/shield in the emitted word.
inline Clock project(std::uint32_t epoch,const std::array<service::Spell,slots>& represented,
    const std::array<std::uint32_t,slots>& actual_cooldown) {
    Clock result;result.earliest_ready=epoch;result.ready_at.fill(epoch);
    const auto dependencies=dependency_mask(represented);
    result.status=dependencies.status;result.dependencies=dependencies.mask;
    if(result.status!=Status::Ready)return result;
    for(std::size_t slot=0;slot<slots;++slot)if(dependencies.uses(slot)) {
        const auto cooldown=actual_cooldown[slot];
        if(cooldown>std::numeric_limits<std::uint32_t>::max()-epoch)
            {result.status=Status::Overflow;return result;}
        result.cooldown[slot]=cooldown;result.ready_at[slot]=epoch+cooldown;
        if(result.ready_at[slot]>result.earliest_ready)result.earliest_ready=result.ready_at[slot];
    }
    return result;
}
// earliest_ready is only the earliest date when all REPRESENTED dependencies
// are warm. It does not certify a cold-start forecast can be shifted there,
// renew M/A/C on the current input, grant a cast, or discharge fresh Life,
// geometry, inventory, self-state, programme or physical-return obligations.
} // namespace tt::production_word_clock
