#pragma once
// Tunable policy settings for bots/main. Every value here is a strategy choice; balance
// numbers (health, damage, timers, costs) are always read from the SDK at runtime.
#include <array>
#include <cstdint>

namespace mb::cfg {

// --- Learned layer --------------------------------------------------------------------------
// Learned decision residuals (D340, learn.h): 1 applies the weights in learn.cpp to the target score,
// fight verdict, banking and recall thresholds and the draft order (zero weights = the rules below
// exactly, apart from fuel); 0 leaves the layer out.
inline constexpr int learned = 1;
// Learned death hazard (hazard_model.h, fitted on v44's online games; bots/main/tracks/long.md): 1 disengages
// (v44's retreat) at a contact decision off spawn when P(killed by an enemy within 10 rounds) >= hazard_threshold,
// for hazard_commit rounds. 0 replays v44.
inline constexpr int hazard = 1;
inline constexpr float hazard_threshold = 0.5f;
inline constexpr int hazard_commit = 3;

// T34 diagnostic: before an ordinary FARM walk, spend one otherwise spare melee
// basic on an awake, visible ally-engaged different camp, preserving our route.
// Off erases the new dispatch and leaves the original travel path unchanged.
inline constexpr bool skim_camp = false;
// Proposal-only control: used solely as a nonzero volatile data initializer.
inline constexpr bool skim_execute = true;

// --- Fuel ---------------------------------------------------------------------------------
// Optional work never starts below this many fuel units (turn-end bookkeeping needs margin). D340 used
// 20K with the learned layer; that reserve alone costs 41 wins in 1,638 (D344) and 14K was clean in
// 10K+ games with the layer on, so v52 keeps 14K.
inline constexpr std::uint64_t fuel_reserve = 14000;
// Evaluation seed: loop iterations of fuel burnt each turn (local experiments; 0 in submissions).
inline constexpr int noise_burn = 0;
// Minimum fuel (on top of the reserve) to start each turn phase; later phases need more.
inline constexpr std::uint64_t fuel_phase_threats = 45000;
inline constexpr std::uint64_t fuel_phase_basic = 12000;
inline constexpr std::uint64_t fuel_phase_farm = 25000;
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
// After a target choice this turn, open a camp fight only with this much left (plus reserve); otherwise
// hold it for the next turn's budget (g1-084-00split: a ~32K first choice left a camp fight ~11K over
// the reserve, the fight spent ~17K and the turn suspended). 0 = off.
inline constexpr std::uint64_t choose_hold_fuel = 21000;
// A no-target champion may spend a turn planning without paying for farm actions.
// Full scores only, fair continuation, normal tail reserve; D164/D171 liveness repair.
inline constexpr bool planning_turn = true;
inline constexpr std::uint64_t fuel_plan_score = 20000;
// Optional steps gated by their measured worst case (spike logs, 2026-10-21): strike-dash landing
// scan ~22K, fight tile scoring ~20K, escape-dash scan ~33K; target scoring stops early enough
// to leave a fight's budget.
inline constexpr std::uint64_t fuel_strike_dash = 26000;
inline constexpr std::uint64_t fuel_fight_move = 24000;
inline constexpr std::uint64_t fuel_escape_dash = 36000;
inline constexpr std::uint64_t fuel_score_group = 26000;
// Target scoring also reserves its dearest evaluation so far; partial route fields keep the full
// field tail margin (both: a heavy farm turn ran out of fuel, 2026-10-21).
inline constexpr bool score_reserve_dearest = false;  // also on with the opening ball (squad_size 9+, D328)
inline constexpr bool route_field_full_margin = false;
// Retreat safety field: bounded search (tiles) from tiles outside every threat's reach (~6K).
inline constexpr std::uint64_t fuel_safety_field = 16000;
inline constexpr int safety_field_radius = 10;
inline constexpr std::uint64_t fuel_phase_log = 25000;

// --- Survival -----------------------------------------------------------------------------
// Start recalling (when nothing can hit us) below this fraction of max health. Dev vs the v17
// set + hunter2: 0.30 94/216, 0.40 ~127, 0.50 148, 0.60 138, 0.70 131 (fewer deaths at 30-50%
// health; more shop visits).
inline constexpr float recall_health_frac = 0.50f;
// With no enemy within 8, recall below this fraction instead (0 = off; top-20 analysis rule 3).
inline constexpr float safe_recall_frac = 0.0f;
// While fighting camps, disengage below this fraction (camps keep their damage while asleep).
inline constexpr float disengage_health_frac = 0.30f;
// Fraction of a camp fight's predicted damage our engaged health must cover before waking it.
// At v23 (all maps /280, base 1.25 = 165): 1.1 150, 1.4 173, 1.6 181, 1.8 179; legacy (/136,
// base 75): 1.4 72, 1.6 87.
inline constexpr float camp_safety_margin = 1.6f;
// Target choice charges the health a camp fight burns as this share of a recall trip (recall, heal,
// walk back from spawn) per unit of max health spent (0 = off).
inline constexpr float hp_cost_weight = 1.0f;
// Siege planning counts only the health a trip can spend before the recall rule pulls us out
// (current health above recall_health_frac; allies 1 - recall_health_frac), not all of it.
inline constexpr bool siege_pool_from_recall = false;
// Sieges: at most this many trips (camps keep damage while asleep), and an engagement must
// remove at least this share of the group's remaining health when the planned team is absent.
inline constexpr int siege_max_trips = 4;
inline constexpr float engage_progress = 0.35f;
// Low health beside a cleared target whose reward is uncaptured (within this many tiles of a capture
// tile, no enemy able to strike us): capture before recalling (0 = recall at once).
inline constexpr int capture_before_recall = 0;
// Keep walking to a visible respawning target that will return by arrival (D120). Off preserves
// the old execution path; waiting beside a camp never occupies a member tile.
inline constexpr bool respawn_travel = true;  // isolated v39 candidate; engine-1.0.3 release gate
inline constexpr int respawn_travel_near = 2;
// Fresh timer-zero respawns are blocked now, not available now. Reject them until
// live observation changes, and wait only beside a group. D158/D171 liveness repair.
inline constexpr bool respawn_wait_liveness = true;
// A disappearing capture intent may mean retreat/death, not a received reward. Trust explicit
// cleared messages and host observations instead of inventing a respawn (D135 candidate).
inline constexpr bool shared_capture_confirmed_only = true;
// Target rewards as this champion converts them: gold x (1 + Vesper 33% + Treasure 20%), gems + 2 gold
// each for Marcella (tribute gold).
inline constexpr bool passive_rewards = false;
// Count AoE/dash stun credit only for victims whose runtime passive permits stun.
// Direct damage and targeted channel-interruption scoring keep their existing treatment.
inline constexpr bool control_immunity = false;
// Before spending a melee attack on a distractor, take a legal step only if it
// immediately delivers the simulated focus victim. D154: experimental, default off.
inline constexpr bool melee_focus_step = false;
// A confirmed camp death must stop contributing cached incoming damage this turn.
// Keep unknown refresh failures conservative. D173 experimental/off.
inline constexpr bool refresh_dead_camp = false;

// --- Economy ------------------------------------------------------------------------------
// Tribute once carrying at least this many gems, or fewer when an altar is nearby.
inline constexpr std::uint32_t tribute_gems = 25;
// Extra tiles we accept detouring to tribute small bags.
inline constexpr int tribute_detour_tiles = 8;
// With an enemy seen within danger_radius in the last danger_rounds rounds, bank bags from this many
// gems (0: tribute_gems always).
inline constexpr int tribute_gems_danger = 0;
// Prism battery (Prism gains 1.5 Focus per carried gem; the core of the #1 online team, ecosystem
// catalog section 2): our Prism keeps its bag until the tribute would win (banked + bag reaches
// winning_gems), the enemy has banked prism_cash_enemy, or the game nears its end; it buys the
// shop_seq_prism plan (shop_plan 99 codes).
inline constexpr bool prism_battery = false;
inline constexpr int prism_cash_enemy = 700;
inline constexpr float prism_gem_weight = 1.0f;  // the battery Prism's gem value multiplier in target choice
inline constexpr bool prism_probe = false;  // diagnostics: log the Prism's gems and live Focus every 50 rounds
inline constexpr std::array<int, 8> shop_seq_prism = { 325, 425, 225, 125, 120, -1, -1, -1 };
inline constexpr int danger_rounds = 15;
inline constexpr int danger_radius = 12;
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
// Opening shop recalls: until early_shop_until a purse of early_shop_gold is enough (0 = off).
inline constexpr int early_shop_gold = 0;
inline constexpr int early_shop_until = 250;
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
// Gem-rich maps: when harmless (cluster-only) groups produce at least gem_rich_rate gems per round
// map-wide, gems are worth at least gem_rich_value (0 = off). Those maps are short gem races
// (00lost-world: top online teams bank 1000 by round 370-490, we needed 735-934 vs the starter;
// gem value 3-8 there: 377-445).
inline constexpr float gem_rich_value = 4.0f;
inline constexpr float gem_rich_rate = 1.4f;
// Gem weight from the marginal value of items (0 = off): a shard speeds the damage-limited share
// (gold_dps_share) of income by growth / main stat for the gems still to bank by either team; the
// weight is the inverse of that rate, at least the values above and at most gem_value_max.
inline constexpr bool gem_value_model = false;  // D91: speedrun -18 (share 0.3) but robustness -15/640
// Race-aware gem weight: multiplied by 1 + race_gem_boost x (their banked - ours) / 100 while they
// lead (0 = off), capped at gem_value_max.
inline constexpr float race_gem_boost = 0.0f;
inline constexpr float gold_dps_share = 0.5f;
inline constexpr float gem_value_max = 20.0f;
inline constexpr float gold_value = 1.0f;
// Jackpots: a group yielding at least jackpot_share of the map's gem production (0 = off). Its gems
// are worth 1 + jackpot_weight times more, its siege may count jackpot_joiners hypothetical allies
// (others: max_hypothetical_joiners) and staging there waits jackpot_patience rounds.
inline constexpr float jackpot_share = 0.0f;  // D98: 0.4 rejected (head-to-head 85 -> 64-72 of 112)
inline constexpr float jackpot_weight = 2.0f;
inline constexpr int jackpot_joiners = 4;
inline constexpr int jackpot_patience = 30;
// Target rate divided by 1 + this x (how much nearer their spawn the group is, relative); 0 = off.
inline constexpr float territory_penalty = 0.0f;
// Mark groups with a visible enemy champion within this radius of their centre (score_group then
// halves their rate for 20 rounds).
inline constexpr bool track_group_enemies = false;
inline constexpr int enemy_group_radius = 6;

// --- Draft ----------------------------------------------------------------------------------
// 0: Cassi&Issac first, Varg/Vendetta by camp clustering, Tavi, Marcella, Vesper, Ryth...
// 1: stun/durability core seen in top ladder drafts: Heart, Ryth, Marcella, Ren, Stellar...
// 2: mix: Heart, Marcella, Ryth, Vendetta, Vesper...
// 3: mobile farmers: Vendetta, Heart, Vesper, Varg, Ryth...
// 7/8: mode 2 with Ren / Varg fifth (8: pooled 118/216 vs v11/v12).
inline constexpr int draft_mode = 8;
// Draft order on maps fielding at most four champions (0 = draft_mode).
inline constexpr int small_draft_mode = 0;
// Draft mode 99: preference orders as data (champion_draft values: 0 ryth, 1 vesper, 2 vendetta,
// 3 cassi&issac, 4 stellar, 5 prism, 6 marcella, 7 willow, 8 ren, 9 varg, 10 tavi, 11 heart; -1
// ends the list). draft_pref3 / draft_pref4 serve maps fielding 3 / 4 when their first entry is set.
inline constexpr std::array<int, 12> draft_pref = { 11, 6, 0, 2, 9, 1, 4, 10, 3, 5, 7, 8 };
inline constexpr std::array<int, 12> draft_pref3 = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
inline constexpr std::array<int, 12> draft_pref4 = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

// --- Coordination ---------------------------------------------------------------------------
// Squad size: 1 = everyone farms alone (commitments still apply), 2 = pairs, 3+ = two big squads.
inline constexpr int squad_size = 1;  // 9+: one squad of everyone (the opening ball, D326)
// The escorted ball (D329): before round escort_until (0 = off) the acting leader farms for the team
// and everyone else escorts it within escort_radius tiles (Brain::escort). Pair with squad_size 9 and
// squad_until_round = escort_until so the leader's targets count the whole team.
inline constexpr int escort_until = 0;
inline constexpr int escort_radius = 2;
// The opening elk party (D331): the first opening_party drafted champions (0 = off) take the
// elk-bearing group nearest our spawn (within opening_radius tiles, nearer ours than theirs) together,
// staging until all of them are there, before round opening_until; then everyone farms as usual.
// Top teams' first elk capture: median r46-65 with 3+ champions; ours r104 (economy study).
inline constexpr int opening_party = 0;
inline constexpr int opening_radius = 22;
inline constexpr int opening_until = 120;
// Squads (squad_size >= 2) only before this round (0 = all game).
inline constexpr int squad_until_round = 0;  // D99: opening pairs/squads rejected (484 -> 449-457 of 640)
// Keep the current target unless a new one scores this much better (hysteresis).
inline constexpr float retarget_ratio = 1.35f;
// Target choice scores at most this many groups (in descending upper-bound order) per decision;
// the bound ignores fight time and team size. 8 (pre-v23) / 12 / 16: all maps 161 / 169 / 167 of
// 280, legacy 76 / 79 / 83 of 136.
inline constexpr int max_target_evals = 16;
// Rounds between re-scoring the current target against the others (a turn uses a median 24K of
// its 100K fuel, so more frequent re-scoring is affordable).
inline constexpr int rechoose_every = 8;
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
// Simulated losses the enemy must exceed ours by to fight (no ally already engaged).
inline constexpr float fight_margin = 0.5f;
// Retreat: enemies repel this many tiles beyond their reach; with enemies in view, each tile
// towards the nearest healthy ally (within 10) is worth this many tiles of the way home.
inline constexpr int retreat_enemy_buffer = 0;
inline constexpr int retreat_ally_pull = 2;
// Retreat destination with enemies in view: 0 = nearest tile out of reach (safety field); 1 = home
// along the spawn field unless an enemy within 5 is nearer our spawn, then the nearest healthy ally.
// 2 = committed homeward retreat (top-20 analysis): with 2+ enemies within 6 or the nearest 3+ away
// and no enemy within 5 nearer our spawn, walk the spawn field every turn (farming suspended) until on
// spawn or two turns out of sight of every enemy, then recall below homeward_recall_frac or retarget
// away from the last sighting; way home held: towards a healthy ally within 8; a single adjacent
// enemy keeps the local scoring.
inline constexpr int retreat_mode = 0;
inline constexpr float homeward_recall_frac = 0.7f;
// 3 = mode 2's commitment only after chase_turns consecutive retreat turns.
inline constexpr int chase_turns = 3;  // D92: mode 1 rejected (+1.5 deaths by r400 on trap maps)
// PvP: enemies seen within this many rounds and close enough to have reached us stay in the
// fight simulation while hidden (0 disables).
inline constexpr int enemy_memory_rounds = 5;
// Share the nearest enemy seen in the spare shared-memory bytes; take reports up to sighting_age
// rounds old into enemy memory (0/false = off).
inline constexpr bool share_sightings = false;
inline constexpr int sighting_age = 4;
// Remembered (or ally-reported) enemies join the fight simulation within this many tiles + their age.
inline constexpr int memory_join_radius = 6;
// Phantoms (D323): live enemies out of sight whose last sighting (or their spawn at round 0) cannot
// rule out that they stand beside the nearest visible enemy (within phantom_radius tiles after
// walking one tile a round since) join the fight simulation at that enemy's tile, arriving
// phantom_delay rounds after it (at most phantom_max of them; 0 = off). Top teams move in groups:
// the first enemy in view is usually the edge of a pack that our vision cannot see yet.
inline constexpr int phantom_mode = 0;
inline constexpr int phantom_radius = 10;
inline constexpr int phantom_delay = 2;
inline constexpr int phantom_max = 4;
// Mode 2: phantoms only from a pack sighting (2+ enemies within 4 tiles of one of them) shared in
// the spare shared-memory bytes, kept pack_age rounds; it counts when its centre lies within
// vision + age + pack_reach tiles of us. Members of that size not in view join at its centre.
inline constexpr int pack_age = 15;
inline constexpr int pack_reach = 4;
// Local-power tactics (D327; fight study .cache/main/study/fights/report.md): S = (our health + the
// visible allies' within 8) / (that + the enemies' within 8, visible or remembered). With an enemy
// within 8: below s_flee disengage toward healthy allies or home for at least s_commit rounds
// (farming cannot override it); at s_fight or more, or at s_flee or more with a kill in reach, fight
// with the focus order (killable now, Cassi/Issac, the ranged carry, the lowest effective health, a
// shielded Heart last); between, answer in place and never approach. Recall only with the nearest
// enemy s_recall_dist+ tiles away (+1 when a ranged-burst kit is near). On our spawn below s_fight,
// hold the spawn and strike what comes adjacent. 1 replaces handle_threats (0 = off).
inline constexpr int tactics_s = 0;
inline constexpr float s_flee = 0.5f;
inline constexpr float s_fight = 0.6f;
inline constexpr int s_commit = 6;
inline constexpr int s_recall_dist = 6;
// Count the unseen members of a shared pack sighting (phantom_mode 2 sharing) on the enemy side.
inline constexpr bool s_pack = false;
// Fight simulation: fighters out of reach of every opponent join after walking the gap, divided
// by sim_arrival_divisor (2: both sides closing); sim_arrivals_allies off keeps allies in contact.
inline constexpr bool sim_arrivals = true;
inline constexpr int sim_arrival_divisor = 2;
inline constexpr bool sim_arrivals_allies = true;
// Populate visible allies' runtime attack range before estimating their arrival. Previously
// their ChampView range stayed zero, delaying ranged support at distances four and five.
inline constexpr bool sim_ally_range = false;
// Conservative local support model: channeling allies remain targetable, but contribute
// no committed attacks, casts or prospective shield while they continue their channel.
inline constexpr bool sim_ally_channels = false;
// Diagnostic control: perform the same allied channel observations with the old policy.
inline constexpr bool observe_ally_channels = false;
// Reuse the pure fight forecast only when every ordered input field is identical.
// Frees planning fuel in repeated unchanged contacts; D163, experimental/off.
inline constexpr bool skirmish_memo = false;
// Focus fire on the most damage per remaining health (simulation and our first target).
inline constexpr bool threat_focus = false;
// Fight model: dashes with a damaging landing count as damaging casts (Heart of Ruin); active
// shields and ready shield abilities count as health.
inline constexpr bool sim_dash_damage = true;
inline constexpr bool sim_shields = true;
// Fight model: awake monsters that will strike us (at most sim_camp_max) fight on the enemy side.
inline constexpr bool sim_camps = false;  // D92: no effect on trap-map deaths
// Fight model: an ally counts only within this distance of a visible enemy (0 = every ally within 6).
inline constexpr int sim_ally_contact = 0;  // D93: 3 cuts deaths but loses ground (wins 56 -> 29/80)
// PvP: with an enemy adjacent and simulated losses at most this much worse than theirs, strike back
// in place instead of retreating (0 = off).
// PvP: closing in on enemies needs this much more simulated advantage than fighting in place (0 = off).
inline constexpr float approach_margin = 0.0f;  // D94: 0.5 / 1.0 rejected (wins 56 -> 47 / 31 of 80)
inline constexpr float contact_hold = 0.0f;  // D93: 1.0 and 2.0 rejected (+1.9 to +2.3 deaths by r400)
inline constexpr int sim_camp_max = 4;
// Camp damage model: damaging dash landings count (Heart of Ruin's dash on cooldown).
inline constexpr bool camp_dash_damage = true;
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
// Enlarge legacy immediate threat for ready runtime damage/CC/AoE cast geometry.
// This is a spell augmentation, not a complete legal footprint. D176-D177.
inline constexpr bool ready_cast_reach = true;
// PvP: chase only targets within reach (ranged: attack range + 1, melee: 3) and away from their spawn.
inline constexpr bool limited_chase = true;
// PvP: when the fight simulation predicts no death on our side, do not retreat (answer in place
// if the enemy can hit us, else keep farming) - retreating starts farm/retreat standoffs.
inline constexpr bool no_loss_stance = true;
// While capturing, hold the tile when the fight simulation predicts no death for us.
inline constexpr bool capture_hold = false;
// Doomed (about to die) with an enemy in reach that one basic attack kills: strike before fleeing.
inline constexpr bool doomed_kill = false;
// Hunting: seek an enemy seen within hunt_age rounds and hunt_radius tiles when above hunt_health
// (1 = any, 2 = only one with less health than us, 3 = carriers; 0 = off).
inline constexpr int hunt_mode = 0;
// Mode 3 hunts carriers only: enemies seen carrying at least this many gems.
inline constexpr int hunt_carrier_gems = 40;
inline constexpr int hunt_age = 10;
inline constexpr int hunt_radius = 15;
inline constexpr float hunt_health = 0.6f;
// Comeback: from `comeback_round`, with the enemy this many banked gems ahead, fight the
// "win with at most one loss" trades too (0 disables).
inline constexpr int comeback_gap = 0;
inline constexpr int comeback_round = 800;
// Caution when behind: from comeback_round, with the enemy this many banked gems ahead, fights
// need behind_fight_margin instead of fight_margin (0 disables).
inline constexpr int behind_caution_gap = 0;
inline constexpr float behind_fight_margin = 1.0f;
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
// Calm farming: when a fight is not favourable, retreat only if an enemy can strike us next turn
// (walk or dash plus attack); otherwise keep farming, give up targets within reach of a visible
// enemy (contest_ban_rounds) and keep camp-fight tiles out of enemy reach. Online, v28 spent ~200
// champion-rounds a game beside passive enemies (procless ~90) and 26% of its deaths followed
// such standoffs. 1 = keep farming only; 2 = also give up contested targets.
inline constexpr int calm_farm = 1;  // D168/D171: avoid proximity-retreat/farm cycles.
inline constexpr int contest_ban_rounds = 30;
// Leaving camp tiles: when no neighbour is strictly farther from the group's centre (a boundary tile
// at a wall or map edge), walk the shortest path to the nearest free tile off every camp.
inline constexpr bool leave_camp_search = true;
// Turns go_capture may fail to step towards a free member tile before giving the target up.
inline constexpr int capture_stall_turns = 3;
// Turns a capture tile may be held before the capture counts as contested and is given up (0: never).
inline constexpr int capture_patience = 100;
// Carrier captures (D326): a cleared group goes to one designated capturer among us and the allies
// in view beside it - Vesper for gold-bearing groups (her passive adds a third), Marcella, then
// Prism for gem-only ones (tribute gold, carried Focus), then whoever stands on a member tile, then
// the lowest roster index. Everyone else steps off and moves on instead of splitting the reward.
inline constexpr bool carrier_capture = false;
// Group news (Brain::share_news, the three tail shared bytes): post a respawning group we see (one an
// ally walks to first) with its exact respawn end; everyone folds it into its beliefs. Excludes the
// other tail-byte users (share_sightings, sos_radius, phantom_mode 2); false = off.
inline constexpr bool group_news = true;
// Recall pre-start (Brain::turn_inner): with an enemy champion within 10, start a recall after the turn's actions;
// next turn keep it if health is under the recall threshold, else cancel it (free). Saves a tick on escapes.
inline constexpr bool recall_prestart = false;
// Recall on the capture (D326): holding a capture with nothing hostile in view, start the recall
// as soon as it lands after the reward (capture_time - recall turns + 1 holding turns), when we
// want to shop (at least capture_recall_gold gold after the reward) or heal (below
// capture_recall_hp). The capture wait and the recall channel then overlap (0 = off).
// 2 = value-based: recall when what the purse after the reward buys plus the healing (the shop's value
// model, shop_recall_benefit) is worth more rounds than capture_recall_walk x the walk from our spawn
// back to this group (the repositioning the free channel does not pay for) minus capture_recall_slack.
inline constexpr int capture_recall = 2;
inline constexpr float capture_recall_walk = 0.3f;
inline constexpr int capture_recall_slack = 0;
inline constexpr int capture_recall_gold = 90;
inline constexpr float capture_recall_hp = 0.6f;

// --- Shop -------------------------------------------------------------------------------------
// 0 = Jungle(main), [melee: Durability(health)], Monster(main), [melee: Power(health)], Power(main)
// forever; 1 = Monster(main), Jungle(health), Power(main); 2 = Monster(main), Power(main);
// 3 = Jungle(health), Durability(health), Monster(main), Power(main); 4 = plan 0 with the melee
// steps for ranged champions too. A/B vs plan 0 (dev): 1 lost 16-38, 2 lost 5-49.
// 5 = Treasure(main) (+20% gold) first, then plan 0; 6 = Treasure after Jungle.
// 7 = plan 5 with Bruiser (5 chosen + 1 of each other stat: 7 shards' worth) instead of Power
// (6) for the stacking steps: 97/162 vs the v13 set, 36-18 vs v12, 45-9 vs hunter.
// 14 = health first like the top online team: Treasure(health), Jungle(main), Bruiser(health),
// Durability(health), Bruiser(main) forever.
inline constexpr int shop_plan = 7;
// Shop plan 99: steps as data, family x 100 + stat x 10 + shards to own (families: 0 monster, 1 power,
// 2 durability, 3 treasure, 4 jungle, 5 home, 6 bruiser; stats: 0 strength, 1 focus, 2 health, 3 the
// champion's main stat; shards 0 = unlimited, the last step; -1 ends). The defaults are plan 7.
inline constexpr std::array<int, 8> shop_seq = { 335, 435, 225, 35, 625, 630, -1, -1 };
inline constexpr std::array<int, 8> shop_seq_ranged = { 335, 435, 35, 630, -1, -1, -1, -1 };
// Shop plan 100: state-driven. At each purchase the next five-shard block (a passive family not yet
// owned, Power or Bruiser; the main stat or Health) is the one that saves this champion the most
// rounds over the expected rest of the game, from its own measured time budget: rounds fighting
// camps, heal trips forced by camp and champion damage, deaths and recalls (shop.cpp, ShopSignals).
// A started block is always finished first (only the completed emblem carries the passive).
// shop_recall_mode 1 replaces the purse rule (shop_recall_gold / shop_recall_detour) with the same
// value model: recall on the way to a target when what the gold buys, owned until the next natural
// spawn visit, plus the healing, is worth more rounds than the detour.
// Evidence (workbench shop, 22 Oct; full gauntlet 880 games, base 666; speedrun vs the starter on
// 80 games, base 1006 rounds): recall mode 1 alone 696 (+103/-73, p 0.03, gem margin +49 +- 17),
// speedrun -33 +- 9, hoarder persona 35 -> 49; gold spent by r200 62% -> 75% of earned (all gauntlet
// opponents; top online teams ~80%), first shop r195 -> r163, shop visits by r400 2.2 -> 2.8 per
// champion. Plan 100 alone speedrun +20 +- 8 (weight 1) / +8 +- 7 (weight 2); recall mode 1 +
// plan 100 (weight 2) 694, speedrun -17 +- 9; + shop_plan_margin 0.3: 691, speedrun -19 +- 9.
inline constexpr int shop_recall_mode = 1;  // D115 (v38)
// Value-model priors, blended with the champion's own measurements over shop_prior_rounds rounds.
inline constexpr float shop_prior_rounds = 120.0f;
inline constexpr float shop_prior_fight = 0.36f;      // share of rounds fighting camps (economy analysis)
inline constexpr float shop_prior_heal = 0.15f;       // share of rounds on heal trips (camp damage)
inline constexpr float shop_prior_damage = 4.0f;      // health lost per round (camp and champion damage)
inline constexpr float shop_prior_gap = 130.0f;       // rounds between spawn visits
inline constexpr float shop_prior_deaths = 0.0004f;   // deaths per champion-round
inline constexpr float shop_prior_income = 1.4f;      // gold per round
inline constexpr float shop_prior_end = 1200.0f;      // expected last round before banking rates are known
// Deaths ~ effective health^-hp_exp x damage^-dps_exp. A duel is won by the larger health x damage
// product, so both count alike (with 1.5 / 0.75 plan 100 built Health first against the hunter
// persona and won 66 vs 70 of 80; D88: our fights need the damage).
inline constexpr float shop_pvp_hp_exp = 1.0f;
inline constexpr float shop_pvp_dps_exp = 1.0f;
// Camp damage also gates which groups a champion may solo (siege pool = health / damage multiplier):
// its measured heal-trip share is weighted by this (plan 100 speedrun: 1 +20, 2 +8, 3 +8 rounds).
// Used by the recall rule's pricing too: the measured recall mode 1 ran with 1.
inline constexpr float shop_camp_damage_weight = 1.0f;
// Plan 7 as the prior (0 = pure value model): the plan's next step is bought unless the best block
// is worth at least (1 + margin) times as much.
inline constexpr float shop_plan_margin = 0.0f;
// Treasure's extra gold is spent on later blocks worth this share of the best block now.
inline constexpr float shop_future_share = 0.6f;
// The recall rule's benefit is multiplied by this before it is compared with the detour (speedrun on
// 31 games: 1.0 -25, 1.5 -27, 2.0 -17 rounds; 0.5 with plan 100 lost the gain).
inline constexpr float shop_recall_scale = 1.0f;
// Every champion builds Focus (test proxies of the burst teams seen online).
inline constexpr bool focus_builds = false;
// Heart of Ruin builds Focus (shield 1.5 x Focus, stunning dash 1.5 x Focus; top online teams do).
inline constexpr bool heart_focus = true;
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
// Log every block the state-driven shop (shop_plan 100) starts, with its value and the runner-up.
inline constexpr bool debug_shop = false;
// Log remaining fuel at handle_farm checkpoints in rounds <= debug_farm_fuel (0 = off).
inline constexpr int debug_farm_fuel = 0;
// On an engine error escaping a turn, log the last fuel-trace phase and farm branch before the
// turn-boundary catch (diagnostics only; false = no extra code).
inline constexpr bool debug_error_phase = false;
// A turn that throws because our champion was killed during it (retaliation) yields with a plain
// "died mid-turn" line instead of an error line (v53 vs the brawl persona, g101-053-00reverse_spiral r105:
// alive 0 at the catch). Only turns that already threw change.
inline constexpr bool died_mid_turn_quiet = true;
// Engine probe (diagnostics, 0 = off): slot 0 starts a recall at the end of round R after its actions and
// cancels it at the start of R+1 (brain.cpp, Brain::turn), logging state bits.
inline constexpr int debug_recall_probe = 0;
// 1: a health recall deferred because we were threatened (handle_health -> retreat) starts at the end of
// the same turn when the retreat step left every threat's reach (probe: recall after a move or attack is
// accepted; the next turn's cancel is free). 0 = off (recall waits for a turn without actions).
inline constexpr int recall_tail = 0;
// k > 0: cancel a recall in progress when an enemy off its spawn is within its attack range + k tiles
// (the hit would interrupt it anyway; cancelling is free and lets the threat logic act). 0 = off (v44:
// a recalling champion does nothing until the channel ends or is broken).
inline constexpr int recall_cancel_reach = 0;

} // namespace mb::cfg
