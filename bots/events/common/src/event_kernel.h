#pragma once
#include <optional>

namespace p11::kernel {
enum class Status { unchanged, applied, unknown, unsupported };
enum class ChannelKind { none, recall, tribute, teleport };
enum class Interrupt { damage, stun, death, enemy_lock };
struct ChannelSnapshot {
    ChannelKind kind=ChannelKind::none;
    std::optional<int> remaining;
    int bag=0;
};
struct ChannelResult {
    Status status=Status::unchanged;
    int banked=0;
    bool recall_arrival=false,teleport_landing_required=false;
};
inline bool preserve_channel(const ChannelSnapshot&c) { return c.kind!=ChannelKind::none; }
inline ChannelResult owner_end(ChannelSnapshot&c) {
    if(!preserve_channel(c))return {};
    if(!c.remaining)return {Status::unknown};
    if(*c.remaining<0)return {Status::unsupported};
    if(*c.remaining==0)return {};
    --*c.remaining;return {Status::applied}; // zero stays pending; never completes here
}
inline ChannelResult owner_start(ChannelSnapshot&c,int current_bag) {
    if(!preserve_channel(c))return {};
    if(!c.remaining)return {Status::unknown};
    if(*c.remaining<0||current_bag<0)return {Status::unsupported};
    c.bag=current_bag;
    if(*c.remaining>0)return {};
    if(c.kind==ChannelKind::teleport)return {Status::unsupported,0,false,true};
    const ChannelResult result{Status::applied,c.kind==ChannelKind::tribute?current_bag:0,
                               c.kind==ChannelKind::recall,false};
    if(c.kind==ChannelKind::tribute)c.bag=0;
    c.kind=ChannelKind::none;c.remaining.reset();
    return result; // arrival is an event; spawn healing belongs to a later owner end
}
// enemy_lock means an enemy lock at THIS channel's altar. Teleport stun
// provenance is explicit: the rules specify enemy stuns, not an arbitrary buff.
inline ChannelResult interrupt(ChannelSnapshot&c,Interrupt cause,
                               std::optional<bool> enemy_stun=std::nullopt) {
    if(!preserve_channel(c))return {};
    if(c.kind==ChannelKind::teleport&&cause==Interrupt::stun) {
        if(!enemy_stun)return {Status::unknown};
        if(!*enemy_stun)return {};
    }
    const bool stops=cause==Interrupt::death||cause==Interrupt::stun
        ||(cause==Interrupt::damage&&c.kind==ChannelKind::recall)
        ||(cause==Interrupt::enemy_lock&&c.kind==ChannelKind::tribute);
    if(!stops)return {};
    c.kind=ChannelKind::none;c.remaining.reset();return {Status::applied};
}
// This emits a separate recovery boundary, not a guessed healing amount.
inline bool spawn_recovery_due(bool owner_end_boundary,bool on_spawn) {
    return owner_end_boundary&&on_spawn;
}

enum class Truth { no, yes, unknown };
struct Allowances {
    Truth move=Truth::yes,attack=Truth::yes,cast=Truth::yes;
    bool ranged=false;
};
inline Status permission(Truth value) {
    return value==Truth::yes?Status::applied:value==Truth::no?Status::unsupported:Status::unknown;
}
inline Status successful_walk(Allowances&a) {
    const auto result=permission(a.move);if(result!=Status::applied)return result;
    a.move=Truth::no;if(a.ranged)a.attack=Truth::no;return result;
}
inline Status successful_basic(Allowances&a) {
    const auto result=permission(a.attack);if(result!=Status::applied)return result;
    a.attack=Truth::no;if(a.ranged)a.move=Truth::no;return result;
}
inline Status successful_cast(Allowances&a,bool reset=false,bool exclusive_with_movement=false,
                              std::optional<Truth> observed_move=std::nullopt) {
    const auto result=permission(a.cast);if(result!=Status::applied)return result;
    a.cast=Truth::no;
    if(exclusive_with_movement)a.move=Truth::no;
    if(reset){a.attack=Truth::yes;a.move=observed_move.value_or(Truth::unknown);}
    else if(observed_move)a.move=*observed_move;
    return result;
}

struct CaptureProgress { std::optional<int> remaining;bool witnessed=false; };
struct CaptureResult { Status status=Status::unchanged;bool completed=false; };
inline void observation_gap(CaptureProgress&c) {c.remaining.reset();c.witnessed=false;}
inline CaptureResult capture_start(CaptureProgress&c,int configured_ticks,bool witnessed_start) {
    if(!witnessed_start){observation_gap(c);return {Status::unknown};}
    if(configured_ticks<=0)return {Status::unsupported};
    c.remaining=configured_ticks;c.witnessed=true;return {Status::applied};
}
inline CaptureResult capture_phase(CaptureProgress&c,bool uninterrupted_uncontested) {
    if(!uninterrupted_uncontested||!c.witnessed||!c.remaining) {
        observation_gap(c);return {Status::unknown};
    }
    if(*c.remaining<=0)return {Status::unsupported};
    if(--*c.remaining==0){observation_gap(c);return {Status::applied,true};}
    return {Status::applied};
}
// Witnessed means capture-phase occupancy/contest history, not merely successive
// owner-turn sightings. CaptureStart and later CapturePhase are separate events.
// A caller must process
// the round's capture award before owner_start, supplying the actual current bag.
// Fog, contested holds, unsupported effects and unobserved teleport landings do
// not become exact forecasts through these helpers. No engine bit parity claimed.
}
