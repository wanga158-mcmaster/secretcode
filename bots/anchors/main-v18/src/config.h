#pragma once
// Tunable policy settings for bots/main. Every value here is a strategy choice; balance
// numbers (health, damage, timers, costs) are always read from the SDK at runtime.
#include <cstdint>

namespace mb::cfg {

// --- Fuel ---------------------------------------------------------------------------------
// Optional work never starts below this many fuel units (turn-end bookkeeping needs margin).
inline constexpr std::uint64_t fuel_reserve = 14000;
// Minimum fuel (on top of the reserve) to start each turn phase; later phases need more.
inline constexpr std::uint64_t fuel_phase_threats = 45000;
inline constexpr std::uint64_t fuel_phase_basic = 12000;
inline constexpr std::uint64_t fuel_phase_farm = 40000;
// Fuel kept free after any distance-field search for the remainder of the turn.
inline constexpr std::uint64_t field_tail_margin = 20000;
// Below this (plus reserve) the farm phase keeps its target and computes no new fields.
inline constexpr std::uint64_t fuel_light_farm = 50000;
// Re-scoring every group (target choice) needs this much (plus reserve).
inline constexpr std::uint64_t fuel_choose = 45000;
// Optional steps gated by their measured worst case (spike logs, 2026-10-21): strike-dash landing
// scan ~22K, fight tile scoring ~20K, escape-dash scan ~33K; target scoring stops early enough
// to leave a fight's budget.
inline constexpr std::uint64_t fuel_strike_dash = 26000;
inline constexpr std::uint64_t fuel_fight_move = 24000;
inline constexpr std::uint64_t fuel_escape_dash = 36000;
inline constexpr std::uint64_t fuel_score_group = 26000;
// Retreat safety field: bounded search (tiles) from tiles outside every threat's reach (~6K).
inline constexpr std::uint64_t fuel_safety_field = 16000;
inline constexpr int safety_field_radius = 10;
inline constexpr std::uint64_t fuel_phase_log = 25000;

// --- Survival -----------------------------------------------------------------------------
// Start recalling (when nothing can hit us) below this fraction of max health. Dev vs the v17
// set + hunter2: 0.30 94/216, 0.40 ~127, 0.50 148, 0.60 138, 0.70 131 (fewer deaths at 30-50%
// health; more shop visits).
inline constexpr float recall_health_frac = 0.50f;
// While fighting camps, disengage below this fraction (camps keep their damage while asleep).
inline constexpr float disengage_health_frac = 0.30f;
// Fraction of a camp fight's predicted damage our engaged health must cover before waking it.
inline constexpr float camp_safety_margin = 1.25f;
// Sieges: at most this many trips (camps keep damage while asleep), and an engagement must
// remove at least this share of the group's remaining health when the planned team is absent.
inline constexpr int siege_max_trips = 4;
inline constexpr float engage_progress = 0.35f;

// --- Economy ------------------------------------------------------------------------------
// Tribute once carrying at least this many gems, or fewer when an altar is nearby.
inline constexpr std::uint32_t tribute_gems = 25;
// Extra tiles we accept detouring to tribute small bags.
inline constexpr int tribute_detour_tiles = 8;
// Weight of one gem relative to one gold when valuing a camp group. With Treasure + Bruiser
// stacking, gold (items) wins more than gems: 1.0-2.0 all ~143/216 vs the v14 set + hunter2
// (6.0: ~119; 4.5: 133; 3.0: 135; 0.5: 138).
inline constexpr float gem_value = 1.5f;
// Gem weight at the end of the game: the weight moves from gem_value to this with progress (the
// later of round / max_rounds and the leading team's banked gems / winning_gems).
inline constexpr float gem_value_late = 1.5f;
inline constexpr float gold_value = 1.0f;

// --- Draft ----------------------------------------------------------------------------------
// 0: Cassi&Issac first, Varg/Vendetta by camp clustering, Tavi, Marcella, Vesper, Ryth...
// 1: stun/durability core seen in top ladder drafts: Heart, Ryth, Marcella, Ren, Stellar...
// 2: mix: Heart, Marcella, Ryth, Vendetta, Vesper...
// 3: mobile farmers: Vendetta, Heart, Vesper, Varg, Ryth...
// 7/8: mode 2 with Ren / Varg fifth (8: pooled 118/216 vs v11/v12).
inline constexpr int draft_mode = 8;

// --- Coordination ---------------------------------------------------------------------------
// Squad size: 1 = everyone farms alone (commitments still apply), 2 = pairs, 3+ = two big squads.
inline constexpr int squad_size = 1;
// Keep the current target unless a new one scores this much better (hysteresis).
inline constexpr float retarget_ratio = 1.35f;
// Allies a champion may assume will join a fight they have not committed to.
inline constexpr int max_hypothetical_joiners = 1;
// A round spent waiting for a respawn counts this many rounds in a target's rate.
inline constexpr float wait_weight = 1.0f;
// Longest wait (after arrival) for a respawning group that we still plan around. 0: only go to
// groups alive on arrival, never wait beside one (dev vs the v16 set + hunter2: 0 145/216,
// 10 141, 20 133, 40 130, 80 ~126; 16 of 27 maps at >= 4/6; holdout 40/64).
inline constexpr int max_respawn_wait = 0;
// Disengage (and ban the target) after this many engaged rounds without an attack or spell.
inline constexpr int fight_watchdog = 25;
// Rounds a champion waits at a staging point for allies before giving up on a target.
inline constexpr int staging_patience = 5;

// --- PvP and retreat ----------------------------------------------------------------------
// PvP: fight only gem-weighted trades better than 1-for-1 (false: also "win with one loss").
inline constexpr bool fight_better_trades_only = true;
// Retreat: enemies repel this many tiles beyond their reach; with enemies in view, each tile
// towards the nearest healthy ally (within 10) is worth this many tiles of the way home.
inline constexpr int retreat_enemy_buffer = 0;
inline constexpr int retreat_ally_pull = 2;
// PvP: enemies seen within this many rounds and close enough to have reached us stay in the
// fight simulation while hidden (0 disables).
inline constexpr int enemy_memory_rounds = 5;
// On our spawn with enemies this close and the fight unfavourable once we step off, hold.
inline constexpr int spawn_hold_radius = 8;
inline constexpr int spawn_hold_max = 40;  // rounds per spawn visit
// PvP: with an ally within 3 already in contact with enemies, fight even trades (do not leave).
inline constexpr bool stand_with_engaged_allies = true;
// PvP: in a favourable fight, close in when an ally within 6 is in contact with enemies.
inline constexpr bool help_engaged_allies = true;
// PvP: read visible champions' live ability cooldowns for the fight simulation (otherwise
// everyone else is assumed ready), and add a ready dash's range to an enemy's threat reach.
// Together: dev 133/216 vs the v15 set + hunter2 (each alone 125 / 117), holdout 40/64.
inline constexpr bool live_cooldowns = true;
inline constexpr bool dash_aware_reach = true;
// PvP: chase only targets within reach (ranged: attack range + 1, melee: 3) and away from their spawn.
inline constexpr bool limited_chase = true;

// --- Shop -------------------------------------------------------------------------------------
// 0 = Jungle(main), [melee: Durability(health)], Monster(main), [melee: Power(health)], Power(main)
// forever; 1 = Monster(main), Jungle(health), Power(main); 2 = Monster(main), Power(main);
// 3 = Jungle(health), Durability(health), Monster(main), Power(main); 4 = plan 0 with the melee
// steps for ranged champions too. A/B vs plan 0 (dev): 1 lost 16-38, 2 lost 5-49.
// 5 = Treasure(main) (+20% gold) first, then plan 0; 6 = Treasure after Jungle.
// 7 = plan 5 with Bruiser (5 chosen + 1 of each other stat: 7 shards' worth) instead of Power
// (6) for the stacking steps: 97/162 vs the v13 set, 36-18 vs v12, 45-9 vs hunter.
inline constexpr int shop_plan = 7;

// --- Diagnostics ------------------------------------------------------------------------------
// Per-turn trace of one champion (slot index) for rounds [lo, hi]; hi = 0 disables it.
inline constexpr int debug_trace_champion = 0;
inline constexpr int debug_trace_lo = 2;
inline constexpr int debug_trace_hi = 0;
// Log every target change with its score.
inline constexpr bool debug_targets = false;
// Score dump of every group for the traced champion every N rounds (0 disables; costly).
inline constexpr int debug_scores_every = 0;
// PvP trace: one line per turn with enemies in view, for rounds [lo, hi]; hi = 0 disables it.
inline constexpr int debug_pvp_lo = 0;
inline constexpr int debug_pvp_hi = 0;

} // namespace mb::cfg
