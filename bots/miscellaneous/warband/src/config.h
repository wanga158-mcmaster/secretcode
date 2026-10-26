#pragma once
// Tunables of the warband. Every number the decisions use lives here as an inline constexpr, so
// `arena sweep --var` and `--set` can reach it. New behaviour goes behind a knob that defaults off.
#include <cstdint>

namespace wb::cfg {

// Perturbation seed: a per-turn fuel burn that changes trajectories without changing intent.
inline constexpr int noise_burn = 0;

// Current-engine action-legality and camp-packing experiments. Both stay off until measured.
inline constexpr bool action_guard = true;
inline constexpr bool camp_retarget = false;
// Two-turn, data-only route diagnostic; no policy action and no new observations.
inline constexpr bool nav_journal = false;
inline constexpr int nav_journal_emit = 0;
inline constexpr bool anchor_follow_fix = true;

// ---- draft ----
// 0: pack order (Ryth, Marcella, Tavi, C&I, Vesper); other modes are alternatives for A/B tests.
inline constexpr int draft_mode = 0;

// ---- pack and formation ----
// The flock's zone: members take camp groups whose centre lies within this Chebyshev distance of
// the anchor, and walk back to it otherwise. 0 makes a tight ball.
inline constexpr int zone_radius = 6;
// Members within zone_radius + this of the anchor join a gather or clear of the focus group.
inline constexpr int converge_margin = 4;
// The anchor holds while more than this many members are busy, at most anchor_patience rounds.
inline constexpr int busy_hold = 1;
inline constexpr int anchor_patience = 8;
// Harmless groups as the pack's focus count at this fraction (a member takes them as a task).
inline constexpr float harmless_focus = 0.5f;
// A group is a pack job when a base champion (this damage per round) would take more than this
// much camp damage (times camp_safety) clearing it alone.
inline constexpr float solo_dps = 60.0f;
inline constexpr float solo_spare = 600.0f;
// Before waking a dangerous group the pack waits at its rally point for members to gather, at most
// this many rounds.
inline constexpr int gather_patience = 6;
// Members counted as gathered when within this distance of the rally point.
inline constexpr int gather_radius = 3;

// ---- objective choice (leader) ----
inline constexpr float gold_value = 1.0f;     // value of one gold
inline constexpr float gem_value = 3.0f;      // value of one gem (gold equivalents)
inline constexpr float capture_cost = 3.0f;   // pack rounds charged for a capture
inline constexpr float retarget_ratio = 1.3f; // a new objective must beat the current one by this
inline constexpr float camp_safety = 1.5f;    // forecast camp damage x this <= usable pack health
inline constexpr float detour = 1.25f;        // Chebyshev-to-walk factor when no field is cached
inline constexpr int monolith_min_round = 400;// never plan a monolith before this round
inline constexpr int max_respawn_wait = 3;    // a respawning group is a candidate only if back on arrival

// ---- health and recall ----
inline constexpr float recall_frac = 0.5f;    // recall when below this health fraction
inline constexpr float tank_swap_frac = 0.40f;// a tank steps out of camp range below this
inline constexpr int hold_patience = 5;       // a ranged member waits at most this long for a tank
// Watchdog: a focus group in gather or clear mode that loses no health (and is not captured) for
// this many rounds is banned for focus_ban rounds.
inline constexpr int focus_watchdog = 40;
inline constexpr int focus_ban = 80;
inline constexpr int shop_recall_gold = 260;  // recall to shop with this much gold when idle
inline constexpr int bank_gems = 30;          // bank when carrying this many gems near an altar
inline constexpr int bank_gems_far = 80;      // bank regardless of distance at this bag
inline constexpr int bank_near = 12;          // "near an altar" in walking steps

// ---- fights against champions ----
inline constexpr float engage_ratio = 1.3f;   // our power / their power to engage
inline constexpr float flee_ratio = 0.75f;    // below this the pack disengages
inline constexpr int enemy_memory = 6;        // rounds a hidden enemy stays in the force count
inline constexpr int ally_memory = 25;        // rounds an unseen available ally still counts in the pack
inline constexpr int chase_spawn_guard = 7;   // never chase into this distance of the enemy spawn

// ---- vision ----
// 0: no wards; 1: drop the ward whenever ready, on a bush in reach when there is one.
inline constexpr int ward_mode = 0;

// ---- diagnostics (perturb fuel; off in every measured build) ----
inline constexpr int debug_every = 0;         // leader logs its state every N rounds (0: never)
inline constexpr int debug_until = 400;
inline constexpr int debug_budget = 0;        // print a time budget per champion at r400 and r1000

// ---- fuel ----
inline constexpr int fuel_reserve = 12000;    // always keep this much for the end of the turn
inline constexpr int fuel_field = 32000;      // a new distance field needs this much left

} // namespace wb::cfg
