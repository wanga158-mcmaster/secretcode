#pragma once
#include "residual_types.h"

namespace residual {
enum class WordAdmission : unsigned {Admit,Defer,Stale};
// A gate admits the ENTIRE following leaf, not an arbitrarily large search.
// Enter: fixed storage construction/initialization and scalar validation.
// Records: <=8 flat records, or one group with <=64 duplicate comparisons.
// External: <=8 catalogue sources at ONE reachable pose.
// Consider: <=32 members, evaluating a completed prefix.
// BasicTarget/CastTarget/AreaAim: State copy and bounded hit/score loops over
// <=32 members. MoveCheck: <=8 occupancy points (a fresh gate per chunk).
// Finish: no search work; fresh epoch check immediately before returning.
// The caller calibrates sufficient headroom per leaf using ACTUAL fuel and
// checks the supplied expected epoch. No SDK or fuel constants live here.
enum class WordWork : unsigned {Enter,Records,External,Consider,BasicTarget,CastTarget,AreaAim,MoveCheck,Finish};
struct WordBudget {
    void* context{};
    WordAdmission (*admit)(void*,int expected_epoch,WordWork) noexcept{};
};
struct WordResult {
    enum class Status : unsigned {Complete,Deferred,Stale,Unsupported};
    Word word;Status status{Status::Unsupported};
};
// A bounded nomination, never SDK command authority. The executor must refresh
// availability/targets after every accepted primitive and rebuild the entire
// Frame after a refusal (which can reveal an occupant).
//
// Represented grammar: one ordinary Move, one global Cast, and Basic with a
// possible Reset -> Basic suffix, in every compatible order up to four actions.
// Each fragment chooses its locally best visible primary/ground aim; this is
// not an exhaustive optimiser over all target combinations. The group may have
// up to 32 members and the immutable world up to 256 camps/64 groups. Observed
// groups and each visible champion list are capped at 64, altar/enemy-spawn
// lists at 4096. Oversized/malformed
// inputs return supported=false, not a verdict about the group.
//
// Supported casts: self Reset (including enhanced-next-basic), camp Targeted,
// ground Area, self Shield and explicitly self-targetable Heal. Range <=4,
// area radius <=2. Dash is omitted: Grid does not represent legal dash paths.
// incoming_upper is ONE next camp-phase batch, after camp mitigation but before
// own shields, including hidden/outside-group catalogue sources. It assumes all
// potentially present camps are awake and targeting this body. No enemy reply,
// capture, payout, escape, future item or later-input claim is made.
Word choose_word(const Frame&,const World&,int group) noexcept;
// The final pose must be the actual origin or one accepted ordinary edge from
// it. This overload never teleports the Body to a hypothetical pose.
Word choose_word(const Frame&,const World&,int group,Point required_final_pose) noexcept;
// A deferred result can contain a COMPLETELY evaluated feasible incumbent,
// including an empty word. Search optimality is not a publication prerequisite.
// No partial target/aim scan is published as a completed greedy fragment. An
// epoch-changing gate result discards every incumbent. A null gate admits all
// work and preserves the unlimited overloads' words, ordering and tie breaks.
WordResult choose_word_bounded(const Frame&,const World&,int group,WordBudget) noexcept;
WordResult choose_word_bounded(const Frame&,const World&,int group,Point required_final_pose,WordBudget) noexcept;
}
