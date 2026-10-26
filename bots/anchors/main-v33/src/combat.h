#pragma once
// Turn-level action execution for fights.
//
// Mechanics assumed (GameRules "A champion turn", "Combat & abilities"): one basic attack
// per turn (Chebyshev range), one successful cast per turn across both slots, 8-turn-end
// cooldowns; ranged champions' walk and basic attack share one allowance; abilities flagged
// exclusive_with_movement cannot be combined with a walk; attack resets restore the attack
// allowance. Every action re-reads the champion state flags rather than assuming them.
#include "ctx.h"

namespace mb {

struct TurnActions
{
    bool moved{};
    bool attacked{};
    bool cast{};
};

// Fight the visible living members of `group`. With `allow_move` false we only act from
// the current tile. Returns true when any action was taken.
bool fight_camps(Ctx& ctx, TurnActions& acts, int group, bool allow_move);

// Fight visible enemy champions; `aggressive` allows stepping toward them.
bool fight_enemies(Ctx& ctx, TurnActions& acts, bool aggressive, const ChampView* focus = nullptr);

// Dash along a route when it beats walking by at least two tiles (no enemies in view;
// damaging dashes are kept for fights unless `spend_strike` is set). Returns true if dashed.
bool try_travel_dash(Ctx& ctx, TurnActions& acts, const cambc::distance_field& field, bool spend_strike);

// Damaging dash (landing damage/stun in a radius) onto the given living camp targets.
bool try_strike_dash_camps(Ctx& ctx, TurnActions& acts, std::vector<CampView*>& targets);

// Damaging dash onto enemy champions (focus target weighted).
bool try_strike_dash_enemies(Ctx& ctx, TurnActions& acts, const ChampView* focus);

// Defensive casts that are useful in any mode (shield when threatened, heal allies).
bool use_support(Ctx& ctx, TurnActions& acts, bool threatened);

// Estimated damage per round the visible enemies near `at` can deal to one target.
float enemy_threat_at(const Ctx& ctx, vec2 at);

// Sum over awake camps whose range covers `at` of their strength (worst case: all hit us).
float camp_threat_at(const Ctx& ctx, vec2 at);

// Damage per round from awake monsters for which a champion at `at` would be the nearest
// target (monsters hit the nearest champion in range), after our damage reductions.
float incoming_camp_rate(const Ctx& ctx, vec2 at);

// Rounds of single steps needed to leave every awake dangerous monster's range.
int escape_rounds(const Ctx& ctx);

// Sustained damage per round against champions for a champion type with live stats
// (non-positive stats fall back to the design's base values).
float pvp_dps_of(cambc::champion_type type, float strength, float focus);
// Skirmish profile from the design, with live strength/focus when known (> 0).
model::PvpProfile pvp_profile_of(cambc::champion_type type, float strength, float focus);
// A champion type's shield ability: slot (-1: none), focus multiplier and design focus.
struct ShieldAbility
{
    int slot{ -1 };
    float modifier{};
    float design_focus{};
};
ShieldAbility shield_ability_of(cambc::champion_type type);
// Remaining value of a visible champion's active shield (0 without one).
float shield_now(const cambc::champion& c);
// Tiles an enemy can strike this turn: walk/attack reach plus a ready dash (live cooldown).
int enemy_reach(const ChampView& e);

// Relative fighting power of visible allies (incl. self) vs visible enemies within radius.
float power_ratio(const Ctx& ctx, int radius);

} // namespace mb
