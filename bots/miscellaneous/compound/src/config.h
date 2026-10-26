#pragma once
// Tunables of contenders/compound. Every value is a knob (`inline constexpr`, unique name), so
// `arena sweep --var` and `arena play --set` reach it. New behaviour enters behind a knob that
// defaults off; with the knob off the bot replays its old games bit for bit.

namespace cfg {

// ---- perturbation seed (arena --seeds): extra loop iterations per turn, compiled out at 0 ----
inline constexpr int noise_burn = 0;

// ---- draft ----
// 0: Vesper, Ryth, Marcella, Vendetta, Heart (3 picks: Ryth, Marcella, Vesper).
inline constexpr int draft_mode = 0;

// ---- economy: value of gold and gems (one currency, see model.h) ----
// Gems are the objective; gold is worth what the items it buys add to future gems. The gold
// value of a gem early in the race; the model lowers gold's worth as the race shortens.
inline constexpr float gem_gold_value = 3.0f;
// Rounds of race assumed left when nothing is known (team gem rate unmeasured yet).
inline constexpr int race_rounds_default = 900;
// Share of the team's summed gem-focused rates a plan achieves (shared and contested camps,
// travel between loops); sets the race horizon used to price gold.
inline constexpr float plan_efficiency = 0.6f;
inline constexpr float supply_share = 0.6f;     // share of the map's gem supply a team takes in a race
inline constexpr float labour_gem_rate = 0.25f; // gems per champion-round of a farming loop (E11)
inline constexpr float min_plan_rate = 0.4f;    // floor of the planned team pace (online loser 0.41, E24)
inline constexpr float lambda_cap = 0.6f;       // gold is never worth more than this many gems
inline constexpr float reinvest_weight = 0.25f; // gold that items earn back, at this share of lambda

// ---- shopping and recall ----
inline constexpr int shop_plan = 0;            // 0: Treasure, Jungle, Bruiser, Durability (Health first)
inline constexpr int shop_recall_gold = 90;     // recall to shop from this much gold ...
inline constexpr float shop_recall_hp = 0.70f;  // ... when health is below this fraction
inline constexpr int shop_recall_full = 150;    // or from this much gold at any health
inline constexpr int shop_recall_max_walk = 40; // ... unless the walk back costs more than this
inline constexpr float recall_hp_frac = 0.30f;  // recall below this health fraction when safe
inline constexpr float camp_margin = 1.25f;     // forecast camp damage x margin must fit in health
inline constexpr float usable_share = 0.8f;     // share of max health spent per trip (exit reserve, E4)
inline constexpr int walk_home_tiles = 6;       // walk instead of recalling this close to spawn

// ---- banking ----
inline constexpr int bag_gems = 24;             // tribute once carrying this many gems
inline constexpr int bag_detour = 6;            // ... or any bag when the altar costs this detour
inline constexpr int bag_late_round = 1850;     // bank everything after this round

// ---- farming ----
inline constexpr int retarget_period = 8;       // rounds between target re-evaluations
inline constexpr int target_evals = 10;         // groups fully scored per target decision
inline constexpr float claim_penalty = 0.15f;   // score factor for a group an ally claims
inline constexpr int max_wait_rounds = 6;       // accept a respawn this close to arrival
inline constexpr int siege_trips = 3;           // plan big groups over at most this many trips
inline constexpr int fight_watchdog = 20;       // rounds engaged without removing camp health
inline constexpr bool parties = true;           // camp convergence: target-sized parties (E8)
inline constexpr float party_found_discount = 0.6f; // chance-weighted value of founding a party
inline constexpr int party_assembly_estimate = 8;   // rounds a party member expects to wait
inline constexpr int party_stage_rounds = 14;   // a founder waits this long for its party
inline constexpr int ban_rounds = 80;           // a group that cannot be progressed is skipped this long
inline constexpr int stuck_turns = 6;           // turns without a step before a target is dropped
inline constexpr int hidden_awake_memory = 12;  // rounds an unseen monster stays "awake" after evidence
inline constexpr bool authoritative_groups = false; // fresh lifecycle facts govern capture and suppressed-respawn leases
inline constexpr bool productive_dash = false; // execute Heart's modeled ability with a bounded path/landing search
inline constexpr bool dash_follow_step = false; // requery and use compatible ordinary movement after transport dash
inline constexpr bool move_reset = false;       // Vesper may spend a ready reset after walking into basic range
#define admissible_targets 0 // preprocess inactive path to preserve exact legacy optimizer layout

// ---- survival model (econ_model.h death_risk) ----
inline constexpr float exposure_rounds = 8.0f;  // rounds a losing exchange lasts before we separate
inline constexpr float risk_slope = 4.0f;       // steepness of the risk in effective health / exposure
inline constexpr float fight_rate_prior = 0.005f; // contact episodes a round before any is seen

// ---- PvP ----
inline constexpr int threat_radius = 7;         // enemies this close are evaluated each turn
inline constexpr int fight_horizon = 12;        // rounds simulated in a skirmish forecast
inline constexpr float fight_edge = 0.5f;       // engage when they lose this much more than us
inline constexpr int beacon_radius = 14;        // allies this far (walk) answer a beacon
inline constexpr int beacon_ttl = 6;            // rounds a beacon stays valid
inline constexpr int chase_spawn_radius = 7;    // never chase into the enemy spawn's surroundings

// ---- fuel ----
inline constexpr bool bounded_turns = true;     // health promotion: staged startup, spare-fuel valuation, channel guard
inline constexpr int fuel_reserve = 20000;      // fuel left after a field computation
inline constexpr int fuel_field = 32000;        // a full distance field needs this much left

// ---- diagnostics (cost fuel: a formatted line is ~1K) ----
inline constexpr int debug_level = 0;

}  // namespace cfg
