#pragma once
// Tunables of the timetable bot. Every knob is an `inline constexpr` so arena sweeps reach it
// (`--var "name: k=v"`, `--set k=v`). New behaviour enters behind a knob that defaults off.

namespace cfg {

// --- Health and diagnostics ---------------------------------------------------------------
// Perturbation seed: iterations of a per-turn fuel burn (0 = off). Changes trajectories, not intent.
inline constexpr int noise_burn = 0;
// 0 silent, 1 plan and economy events (captures, banks, shops, deaths), 2 verbose.
inline constexpr int log_level = 1;
// A formatted line costs ~1K fuel: log only with this much spare.
inline constexpr int fuel_log_spare = 30000;
// Per-turn decision trace for one body slot (-1 = off). Diagnosis only: costs ~1K fuel a turn.
inline constexpr int trace_slot = -1;
// Stunned bodies have no action permissions: do not attempt recall/tribute without any.
inline constexpr bool idle_action_guard = true;
// Camp reservations are intentions; only a visible group state establishes its availability.
inline constexpr bool availability_evidence = false;
// Defer decisions after expensive cold navigation; bound target-scoring by remaining fuel.
inline constexpr bool scheduler_fuel_guard = false;
// Complete, admissibly ordered candidate search instead of the fixed static-HP shortlist.
inline constexpr bool safe_dispatch = false;
// Native opportunity diagnosis only; evaluates omitted known rewards without taking them.
inline constexpr bool dispatch_audit = false;
// Diagnostic delayed activation; normal experiments use round0/all bodies (-1).
inline constexpr int safe_dispatch_after = 0;
inline constexpr int safe_dispatch_slot = -1;
// Feed owned emblem passives back into farm damage, safety and gold forecasts.
inline constexpr bool equipment_model = false;
inline constexpr bool equipment_trace = false;
// Execute committed draft itineraries; release on observed completion, denial or bounded expiry.
inline constexpr bool itinerary_jobs = false;
// Walking can reveal occupied dash destinations; refresh before a same-turn travel dash.
inline constexpr bool dash_refresh = false;
// Bound optional strict/relaxed target scoring after observation and PvP work.
inline constexpr bool health_score_budget = false;

// --- Fuel discipline -----------------------------------------------------------------------
// Keep this much fuel for the rest of a turn after optional work.
inline constexpr int fuel_reserve = 14000;
// Full distance fields per turn at most (first turn and later turns).
inline constexpr int fields_first_turn = 1;
inline constexpr int fields_per_turn = 1;
// After the first turn's static setup, act only with this much fuel left (else wait a round).
inline constexpr long first_turn_floor = 80000;

// --- Draft ---------------------------------------------------------------------------------
// 0 = fixed priority roster; 1 = roster chosen by the planner's throughput estimate.
inline constexpr int draft_mode = 1;

// --- Plan (draft-time model) ------------------------------------------------------------------
// Fuel per planner work unit, for the draft's planning budget (measured; see NOTES).
inline constexpr long plan_fuel_per_op = 40;
// 0 = no plan (every champion greedy over the territory), 1 = central plan from draft memory.
inline constexpr int plan_mode = 1;
// Territory: groups with d(our spawn) <= d(enemy spawn) + margin tiles are ours.
inline constexpr int territory_margin = 0;
// Groups up to this many tiles nearer the enemy are plannable, at a contest-weighted value.
inline constexpr int contest_margin = 6;
// The draft measures pairwise distances for groups up to this many tiles past the territory line.
inline constexpr int measure_margin = 8;
// Gold valued as this many gems per gold in target choice. Gold scores only through items (E17),
// and gem groups pay gold on the side; the foundation's schedules price it at 0-0.05.
inline constexpr float gold_weight = 0.05f;
// A cleared group's waiting reward (M13) is trusted this long after it was last seen; then it is
// presumed captured by someone and priced as a respawn.
inline constexpr int reward_memory = 12;
// Wait at most this many rounds for a respawn (the schedules wait up to 25).
inline constexpr int wait_max = 20;
// Rounds charged per full health bar spent (recall + heal + walk back is added per map).
inline constexpr float heal_trip_base = 7.0f;

// --- Farming -------------------------------------------------------------------------------
// Forecast camp damage x margin must fit our health to engage a group alone.
inline constexpr float camp_margin = 1.35f;
// Retreat from a camp fight when health < incoming per round x (steps out of reach + this).
inline constexpr float camp_escape_rounds = 1.5f;
// A new target must beat the current one by this factor (3x within 5 tiles of it): every switch
// throws away the walk already made.
inline constexpr float switch_ratio = 1.5f;
// Rounds between target re-evaluations (fuel: every body scores every group).
inline constexpr int replan_rounds = 5;
// Recall to heal below this health fraction (when safe), unless the next fight fits.
inline constexpr float recall_health_frac = 0.40f;
// Sieges: camps keep their damage while asleep (M10), so a group can be cleared over trips, each
// paid with escape, recall, heal and the walk back. Groups needing more trips are left alone.
inline constexpr int siege_max_trips = 4;
// Rounds a capture takes from stepping on a member tile (5 capture phases + the step).
inline constexpr int capture_rounds = 6;
// Waiting for a respawn counts this many times its rounds in the rate.
inline constexpr float wait_weight = 1.0f;
// Give up a target after this many turns without a successful step or attack.
inline constexpr int stuck_limit = 6;
// Fight watchdog: engaged rounds without landing an attack before banning the group.
inline constexpr int fight_watchdog = 20;
// Ban length (rounds) for a group we gave up on.
inline constexpr int ban_rounds = 40;

// --- Banking -------------------------------------------------------------------------------
// Bank when the bag reaches this many gems.
inline constexpr int bank_gems = 40;
// Bank a smaller bag when an altar is this close (tiles of walking).
inline constexpr int bank_near_tiles = 6;
inline constexpr int bank_near_gems = 10;

// --- Shopping ------------------------------------------------------------------------------
// Recall to shop with at least this much gold (gold buys nothing until spent, M39); within this
// many tiles of spawn walk home instead.
inline constexpr int shop_recall_gold = 150;
inline constexpr int shop_walk_tiles = 6;
// Use recall as transport when walking takes this many rounds more than recall + walk.
inline constexpr int travel_recall_margin = 6;

// --- PvP (survival first) --------------------------------------------------------------------
// Enemies within this Chebyshev distance are a threat worth reacting to.
inline constexpr int threat_radius = 6;
// Fight back only when our side's kill time beats theirs by this factor.
inline constexpr float fight_edge = 1.6f;
// Finish an enemy in reach whose health is at most this many of our hits.
inline constexpr float finish_hits = 2.0f;
// Rounds an enemy sighting stays remembered.
inline constexpr int enemy_memory = 4;

} // namespace cfg
