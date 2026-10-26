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
// After a blocked travel turn (no field, greedy step stalled) with no enemy in view, the next turn
// computes the target field keeping only this much free (0: normal margin).
inline constexpr std::uint64_t stuck_field_margin = 0;
// Distance-field cost model: fixed overhead per search, taken out of the per-step rate (0: the
// pre-v22 rule that charged it to the steps; 2-step pocket searches then inflated the rate ~15x
// and blocked 7-35% of turns on fracture/big-world/ambush/quadrants: there 21/32 -> 30/32 wins).
inline constexpr std::uint64_t field_overhead = 3000;
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
// At v23 (all maps /280, base 1.25 = 165): 1.1 150, 1.4 173, 1.6 181, 1.8 179; legacy (/136,
// base 75): 1.4 72, 1.6 87.
inline constexpr float camp_safety_margin = 1.6f;
// Siege planning counts only the health a trip can spend before the recall rule pulls us out
// (current health above recall_health_frac; allies 1 - recall_health_frac), not all of it.
inline constexpr bool siege_pool_from_recall = false;
// Sieges: at most this many trips (camps keep damage while asleep), and an engagement must
// remove at least this share of the group's remaining health when the planned team is absent.
inline constexpr int siege_max_trips = 4;
inline constexpr float engage_progress = 0.35f;

// --- Economy ------------------------------------------------------------------------------
// Tribute once carrying at least this many gems, or fewer when an altar is nearby.
inline constexpr std::uint32_t tribute_gems = 25;
// Extra tiles we accept detouring to tribute small bags.
inline constexpr int tribute_detour_tiles = 8;
// Recall as transport when walking to the target takes this many rounds more than recalling
// and walking from spawn (0 disables). Fires ~4x/game, mostly on corridor/big maps (01corridors
// 31, 01big-world 26): dev 132/216 vs the v18 set + hunter2 (base 126), holdout 44/64 (40).
inline constexpr int travel_recall_margin = 4;
// Shop recall: carrying at least this much gold (0 disables), recall when it costs at most
// `shop_recall_detour` rounds more than walking to the target (unspent gold buys nothing).
// All maps vs the v19 set + hunter3 (/280, base 163): 300/20 179, 500/20 176, 300/10 172,
// 200/6 171, 400/15 170; legacy maps (/136, base 74): 300/20 87, 500/20 72.
inline constexpr int shop_recall_gold = 300;
inline constexpr int shop_recall_detour = 20;
// Target choice counts the travel recall in travel time (at v20 all maps 156/280 vs base 168:
// draws champions to far targets; off); tribute trips recall as transport when the walk to the
// altar exceeds the recall plus the walk from spawn by this margin (0 off; fires ~0.5x/game;
// margin 4: all maps 174 vs 168, legacy 81 vs 75).
inline constexpr bool recall_in_target_travel = false;
inline constexpr int tribute_recall_margin = 4;
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
// Target choice scores at most this many groups (in descending upper-bound order) per decision;
// the bound ignores fight time and team size. 8 (pre-v23) / 12 / 16: all maps 161 / 169 / 167 of
// 280, legacy 76 / 79 / 83 of 136.
inline constexpr int max_target_evals = 16;
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
// Camp-fight tile scoring counts the walking distance (group field) out of reach, not only the
// straight line (approach-vs-travel oscillation across walls; tests/main-watchdog-fixture.py).
// Watchdog events 2.33 -> 1.39/game; all maps 157 vs 155 of 280, legacy 94 vs 85 of 136.
inline constexpr bool fight_route_scoring = true;
// Rounds a champion waits at a staging point for allies before giving up on a target.
inline constexpr int staging_patience = 5;
// ... or this long while other allies have published the same group (they are on their way;
// 0: same as staging_patience).
inline constexpr int staging_patience_committed = 0;
// Only groups worth at least this many gems (00stampede's 20-member group: 160) get the longer wait.
inline constexpr int siege_patience_min_gems = 0;

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
// Fight simulation: fighters out of reach of every opponent join after walking the gap, divided
// by sim_arrival_divisor (2: both sides closing); sim_arrivals_allies off keeps allies in contact.
inline constexpr bool sim_arrivals = true;
inline constexpr int sim_arrival_divisor = 2;
inline constexpr bool sim_arrivals_allies = true;
// Focus fire on the most damage per remaining health (simulation and our first target).
inline constexpr bool threat_focus = false;
// Fight model: dashes with a damaging landing count as damaging casts (Heart of Ruin); active
// shields and ready shield abilities count as health.
inline constexpr bool sim_dash_damage = false;
inline constexpr bool sim_shields = false;
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
// PvP: when the fight simulation predicts no death on our side, do not retreat (answer in place
// if the enemy can hit us, else keep farming) - retreating starts farm/retreat standoffs.
inline constexpr bool no_loss_stance = false;
// While capturing, hold the tile when the fight simulation predicts no death for us.
inline constexpr bool capture_hold = false;
// Comeback: from `comeback_round`, with the enemy this many banked gems ahead, fight the
// "win with at most one loss" trades too (0 disables).
inline constexpr int comeback_gap = 0;
inline constexpr int comeback_round = 800;
// Ban the target for 40 rounds after this many travel turns that each follow a retreat within 2
// rounds (farm/retreat standoffs; 0 disables).
inline constexpr int standoff_ban_after = 0;
// PvP: rounds the fight simulation looks ahead (max 32). Short horizons leave 1v1s between
// healthy champions unresolved (0-0), and both sides then hold off (standoffs).
inline constexpr int skirmish_horizon = 15;
// SOS: a champion retreating from a losing fight posts its position; healthy allies within this
// many tiles walk towards it for `sos_age` rounds (0 disables).
inline constexpr int sos_radius = 0;
inline constexpr int sos_age = 4;
// Teleport-crystal shortcuts: keep one crystal (10 gold) and use it while travelling when a free
// landing within 3 is this many rounds closer than walking, after its channel (0 disables).
inline constexpr int tp_shortcut_margin = 0;
// Altar locks (10 gold, one carried): 1 = lock the altar a visible enemy is tributing at (cancels
// the channel); 2 = also lock our tribute altar before channelling; 3 = also any altar in reach.
inline constexpr int lock_mode = 2;
// Standoff recall: after this many consecutive retreat turns without losing health (nobody
// adjacent, below standoff_recall_frac health), recall instead of walking (0 disables).
inline constexpr int standoff_recall_turns = 0;
inline constexpr float standoff_recall_frac = 0.75f;
// Turns go_capture may fail to step towards a free member tile before giving the target up.
inline constexpr int capture_stall_turns = 3;

// --- Shop -------------------------------------------------------------------------------------
// 0 = Jungle(main), [melee: Durability(health)], Monster(main), [melee: Power(health)], Power(main)
// forever; 1 = Monster(main), Jungle(health), Power(main); 2 = Monster(main), Power(main);
// 3 = Jungle(health), Durability(health), Monster(main), Power(main); 4 = plan 0 with the melee
// steps for ranged champions too. A/B vs plan 0 (dev): 1 lost 16-38, 2 lost 5-49.
// 5 = Treasure(main) (+20% gold) first, then plan 0; 6 = Treasure after Jungle.
// 7 = plan 5 with Bruiser (5 chosen + 1 of each other stat: 7 shards' worth) instead of Power
// (6) for the stacking steps: 97/162 vs the v13 set, 36-18 vs v12, 45-9 vs hunter.
inline constexpr int shop_plan = 7;
// Every champion builds Focus (test proxies of the burst teams seen online).
inline constexpr bool focus_builds = false;
// Heart of Ruin builds Focus (shield 1.5 x Focus, stunning dash 1.5 x Focus; top online teams do).
inline constexpr bool heart_focus = false;
// Wards (vision 6, one per champion, permanent): 0 none (pre-v26); 1 leave it at the camp we
// capture; 2 drop it wherever we stand whenever it is ready (top online teams place 65-558 a game);
// 3 as 2 but toward the target; 4 as 2 but on a bush within reach when there is one; 5 bushes only;
// 6 where it uncovers the most tiles out of our sight (bushes favoured).
// Mode 2 (~526/game): vs hunters +13 of 174 games (hunter5 59-11 vs 50-20 all maps, 26-8 vs 28-6
// legacy; hunter3 62-8 vs 56-14), neutral vs our own versions; mode 1 147 vs 160. Mode 4 vs mode 2:
// all maps 172 vs 162, legacy 89 vs 74, vs {v25, v24, hunter3} 144 vs 129; mode 3 160 vs 162.
inline constexpr int ward_mode = 4;
// Mode 6 scores up to 10 candidate tiles by unseen coverage (bitboard ops) only with this much fuel.
inline constexpr std::uint64_t fuel_ward_scan = 30000;

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
