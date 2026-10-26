#pragma once
// Small action-boundary helpers, independent of SDK storage and queried afresh on every call.
namespace ce {

template <class Actor, class Position>
bool try_start_tribute(Actor& actor, Position altar)
{
    // A zero-timer stun still exists until the next owner-start expiry. Inspect the live buff,
    // never infer its disappearance from a remembered duration or spent action allowances.
    if (actor.buffs().has_stun()) return false;
    actor.submit_gems(altar);
    return true;
}

}  // namespace ce
