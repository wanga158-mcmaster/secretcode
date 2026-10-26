# contenders/compound: notes

Retired intact on 23 October 2026 during the user-requested workflow reset. This is the old
implementation and historical evidence. The active restart is `bots/contenders/compound`.

Line owner: compound agent (coordinator: contenders suite). Owned paths: `bots/contenders/compound/`,
scratch bots `bots/contenders/_compound-*/`, scratch output `.cache/contenders/compound/`.

## Thesis (revised by the coordinator, 22 Oct, from foundation E20 and C5/C8)
"Gold is capital: income reinvested at every spawn visit into the item that pays back fastest compounds
into income and survival, and dead time is the leak to plug." The earlier sub-claim "Health binds farming
and fights" was refuted: for throughput Jungle of the main stat is the best first buy (E18-E20, Health-first
costs ~59 rounds of speedrun); in duels Health and Strength tie at equal gold (C5); Health's distinct value
is the escape margin (C8). Organisation: dispersed solo farmers, early and frequent shopping, contact
discipline, convergence (3+ on isolated enemies; target-sized parties on camps, E8/G8). Accepted weakness:
early aggression before items; gem rushes (answers pending, see "Answers").

## Active research (22 Oct, goal remains beating main)
- USER REDIRECTION: stop the finite-episode/scout/simulator project; no more related jobs.
  Preserve its code and all results, including failed assays and unfinished edits. The new direction
  is a practical economic contender on reliable execution: measure large coherent draft/shop/bank
  packages in full games first, then explain promising results or conspicuous failures. No path
  moves or retirement yet; await the coordinator's fresh tournament brief. Hard limits, bounded
  fuel and default-OFF preservation remain. The detailed research below is checkpoint history,
  not authorization to resume that stopped project.
- Health-qualified execution remains the default; new `authoritative_groups`, `productive_dash`,
  `dash_follow_step`, `move_reset` and `admissible_targets` experiments default OFF. Current source `59a3814493a3`
  has raw and injected module bytes identical to the qualified health-only executable.
  Experimental source keys differ from the health-only
  source below; OFF preservation is checked against its raw/injected module bytes.
- Authoritative lifecycle experiment is complete at `.cache/contenders/compound/lifecycle-pool`:
  OFF `778d510aeccf`, ON `7cbbb65f4578`, all 40 pool maps/both sides vs v40/starter/fundamentals,
  480 games. Both healthy; ON peak 97,521. Wins unchanged 146/240, flips +7/-7 (p=1); v40 0-80
  in both. Speedrun worsens 1,402.8 -> 1,427.2 rounds (+24.4 +/- 13.9 SE). Keep OFF; correcting
  beliefs does not automatically allocate freed time to worthwhile production.
- Mechanism is physical: durable `results/contenders/compound/lifecycle-opportunity.json` shows
  an original current actor holding a member spawn for 168 owner starts, with fresh respawning{0}
  across 27 query rounds and three fully traced ready/unstunned/no-enemy turns. The enabled
  `lifecycle-renewal.json` proves a preventive r633 departure during positive cooldown, real
  respawn r693, applied renewed attack r718, payout r767 (+411 gold/+71 gems), and a 23-gem bank
  r775. These divergent trajectories are not a matched-prefix causal contrast or proof of the
  timer-zero refusal branch. The renewal still waits while freshly alive, so party/task dispatch
  is the next lifecycle-side allocation question.
- Actual-action mismatch: Heart's dash is priced by farming/shop
  models but was never dispatched; Vesper's post-walk caller blocks its existing reset helper
  when movement consumes attack. The first action pilot (`action-pilot`, 80 games on five maps,
  both sides vs v40/starter) yielded base/dash/reset/packed 9/9/8/9 wins per 20 keys. Old eager-zero
  dash/packed have a LostWorld A vs v40 zero-clear stall; these arms are unqualified. Native
  `action-dash-observation.json` proves actual transport casts followed by ENDTURN with movement
  still available. `action-reset-observation.json` proves Vesper MOVE -> RESET -> BASIC at r105
  under matched public prerequisites, but OFF catches up by r106; this is earlier action packing,
  not an additional two-round damage gain.
- Corrected action-cycle pilot is complete (`.cache/contenders/compound/action-cycle-pilot`,
  60 full games on Ambush/LostWorld/Islands/Concentrated Wealth/Yellow Brick Road, both sides
  vs v40/starter). Base `70a01ac5bac8`, POD dash `27baa1fcf97d`, dash plus compatible ordinary
  movement `787dad823e36` each score 9/20, with v40 0/10 and starter 9/10. No flips; dash margin
  +58.5 +/- 50.0 SE and cycle +48.2 +/- 41.6 SE. All healthy, peaks 83,204/82,016/83,238.
  Keep OFF; the actual dash -> ordinary MOVE observation below establishes execution but no
  measured strength benefit. Those four tests passed; a fifth target-bound test now
  passes as well. No original-bot games are running.
  SDK range masks are not legality certificates; dash paths
  permit holes, landing tiles do not. Ordinary walking reachability still restricts portfolio;
  this first arm does not establish general dash-connected map planning or PvP dash competence.
- Actual cycle observation is now complete: `results/contenders/compound/action-cycle-observation.json`.
  Exact actor/ability/vision and nearby states match before/after a common r3 Heart dash from
  (3,31) to (5,29). Dash-only ends; cycle moves to (6,29) in the same owner turn and spends only
  movement. Later capture r70, JungleFocus buy r105 and bank r137 are observed continuation, not
  attributed to that extra step. All observers exited; no broad action sweep is warranted.
- Independent target allocation audit found a concrete post-item model/caller defect: the old
  alleged upper bound fixes base-stat party_n=2 while the scorer also offers equipped solo. A
  2,400-HP elk example has old bound1.41655 < incumbent1.50 < actual solo1.54709. That camp is
  skipped before evaluation. The bound also omits Marcella tribute-gold and continuation bonuses,
  and its Smith feasibility estimate cannot certify all offered options are impossible.
  `admissible_targets=0` is the new OFF default. Enabled `target_bound.h` uses the same real caller
  and a proven floor after multiplying score time by party size: capture + min(travel,heal-trip)
  + max(0,represented-health/max(1,dps)-.001), with exact positive value/continuation bonuses.
  All five tests pass, including 960 actual forecast combinations and the prune counterexample.
  Fixed top10 and fuel gates remain incomplete search, so this is no global optimization claim.
  constexpr refactoring initially changed OFF module bytes; preprocessing inactive paths restored
  exact raw/injected equality. Completed32game target-bound pilot on Corners,Ambush,LostWorld,
  BigWorld/both sides/v41+starter at `.cache/contenders/compound/target-bound-pilot`: OFF
  `b2a8c41f6c20`, ON `8851a1480765`, both8/16 (v41both0/8; starterboth8/8), no flips,
  margin-54.6 +/-25.5SE, all healthy, max83,809. Keep OFF; no broad sweep. Starter speedrun
  1187 ->1203.875 (+16.875 +/-45.53SE). By r400 v41 deaths2.75 ->3.875; starter .25 ->.375;
  small selected-map samples do not establish a causal death effect. Correcting the envelope
  alone does not cure finite-shortlist allocation. Exec84501 exited; do not restart it.
  After that measured source, proof hygiene adds a 1e-5 relative allowance for the two float HP
  sum orders/divisions plus the ceil epsilon, and 120 multi-member Smith-order/fractional-HP
  cases (1,080 total). Current OFF `59a3814493a3` still has both qualified module bytes exactly;
  the slightly more conservative ON envelope has not been measured. Old pilot rows remain exact.
  Read-only `contact-observation.json` traces the earliest own death (Swirlr17) across80qualified
  v40gate keys. Nearby Ryth/Marcella attackcamp26, but the existing r13 forecast ALREADY predicts
  retreat for all3,120 input-order/unseenbeacon cases. Removing camp-committed allies does not
  change the retreat branch; no support filter is warranted. R11retreat ->r12lostpersonalvision
  ->reentry and ready r13ENDTURN are observed; r15/16 are truly stunned. No saved-death claim.
  The bounded saved-frame audit is complete: all three neighbors improving the conditional
  geometric score are walls (23,13/22,13) or Marcella (21,13). Missing private spawnfield/threat/
  teleport/reveal/fuel state still prevents exact caller/flee discrimination. R12holding21,13
  avoids all basics but lies inside Ryth's geometric CC envelope; no legal-cast or survival
  counterfactual is proved. No filter/veto patch, games or native walks; all scout processes exited.
- A separate owned `_compound-capital` prototype explores coupled local-work/item/shop-route
  capital policy on frozen v40 execution. It cannot replace this bot without coordinator review,
  real distinct contribution and coordinated retirement. Main already adopted value-based shop
  recall (D115); D91/D102's failed simplistic gold pricing/large bags are negative controls.
  Its refined pilot was rejected (12/16 -> 4/16); diagnostic BigWorld A then established expensive
  pre-decision route appraisal consuming 21,164 fuel and leaving 33,322, below the farm caller's
  39K reserve, while post-action model publication was also starved. Staged geometry/candidate
  appraisal now freeze inputs, publish complete generations and reserve productive fuel;
  exact details and identities live in `_compound-capital/NOTES.md` and durable `capital-prototype.json`.
  Fuel repair restored actual BigWorld production (47 clears, winr1133, zero deaths), but repaired
  32game pilot still loses 12/16 -> 7/16 (+0/-5,p.0625); keep rejected. Actual gold-only captures
  appear12/16keys although portfolio excludes all zero-gem tasks and bundle_value omits non-Treasure
  extra gold. This proves missing work/value coverage, not that a chosen item is wrong. Native
  model-prediction/executed-cycle calibration comes before weights. Optional bank route pricing also
  lacks a committed altar landing; geometric estimate is not guaranteed executed detour. No adoption.
  Native forecast `e888fe36d61e` now pins an affordable caller discontinuity at r284: cache r283,
  gold160, itemversion1 matches cache1, remainingJungle3shards90gold, HP1000, before0gemflow and
  affordable-after.149044, advance137.667, heal0. Existing helper discards throughput gain when
  before0, yields only.339411survival benefit versus extra61 and shoppingfalse. Actual first2
  shards have model0flow and SDKstats82/100/1000; full5 forecast is explicitly separate. A focused
  default-off return-episode implementation now compares immediate return versus completing the
  actual current goldcamp first, then both purchase concrete affordable lists and clear the same
  named gemcamp. Extra gold contributes through an attainable added item's effect on that cycle;
  endpoints/retainedinventory and execution binding are under independent audit. Zero gemflow is
  not zero opportunitycost. Executing the predicted gemportfolio remains unproven.
  The activation gate then FAILED usefully: task-contract probe `2354a9633835` at r284 finds J0
  neverseen, J7lastseen122, and the sole positive-flow J10lastseen132 with two teammates committed.
  That stale/reserved forecast does not establish free marginal team capacity; claims are intent,
  not proven arrival or production. Corrected J10 forecast is33fight
  turns/1198.4damage (probe record labels were swapped; preserve its source and corrected receipt).
  No economic-term fix or free zero-baseline branch was added. The next action is a bounded paid
  joint-vision scout toward an unclaimed J while retaining fresh current G, then reobserve/appraise/
  abort or execute; all scout/reobservation/abort time counts in whole-policy results. Unknown or
  teammate-covered work cannot justify an optional shop trip. This remains private prototype work.
  First physical scout assay `cd4106979ade` (`episode-scout-contract`) is a negative mechanism
  result: 81starts,0jointobservations,76explicitaborts; healthy1005-218/r1646, peak91,378.
  Center distance<=2vision plus following Jfield did not ensure a joint-visible vantage, and
  releasing G's intent invited competing claims. Next version validates a named walkable joint-
  vision waypoint, stages/caches its search, reserves G only for a bounded scouting lease with
  explicit release, and avoids identical failed retries. No optionalreturn was justified by this assay.
- Live target has advanced to `anchors/main-v41` (`results/releases/LIVE.json`); existing v40
  controls remain exact historical evidence. Future qualification must include v41/current main.

## Health-qualified state (22 Oct, resumed on CLI/SDK 3.0.5 and engine 1.0.3)
- Qualified LF-normalized default source: `contenders/compound@904d86692b59`, `bounded_turns=true`
  (decision 10). Inactive research code now gives current source `59a3814493a3` with identical modules.
  This is a health-only promotion, authorized by the coordinator after the paired panel; no
  novel economy or PvP policy was promoted. No online, anchor or release action was taken.
- Qualified executable health gate PASS: `.cache/contenders/compound/health-promotion-gate`,
  measured default source `6edcbd448d9c`, all 40 pool maps,
  both sides, against `anchors/main-v40@6f62c56b73f7`, `anchors/starter-cpp@3785135fc7c7` and
  `anchors/fundamentals@85ce679dc7b1`: 240 complete games, zero faults, exhaustion, error lines,
  stalls and suspensions, maximum 86,883 fuel. Both tests (`execution`, `models`) pass. No strength
  reference was requested. The raw and injected binaries equal evaluated ON `87be6d435db3` exactly.
  After that gate, six edited legacy C++ files were normalized from CRLF to required LF, creating
  current source `904d86692b59`; both module byte streams still match the gated source exactly.
  This explicit executable binding carries the qualification; the old gate and game rows retain
  their actual source identity. No games were rerun for a newline-only change.
- The live bar during this panel was v40 (the live file now names v41). Both arms of the 480-game paired
  panel lose **0-80** against v40. Starter: 79-1 in both; fundamentals: 64-16 OFF, 67-13 ON. Across
  240 identical keys, wins 143 -> 146, flips +6/-3, McNemar p=0.5078; gem margin +11.0 +/- 11.8 SE.
  This is no detectable strength gain. The gate reuses the same deterministic 240 keys; it is a
  current-default qualification, not 240 additional independent samples.
- Speedrun on the panel's 80 starter keys: 1,401.0 -> 1,402.8 rounds, paired +1.8 +/- 14.1 SE;
  unfinished 15 -> 13. Economic throughput remains well below the live bar.
- Exact same-run panel: `.cache/contenders/compound/health-repair-pool`; OFF `a0a971d7df5b`, ON
  `87be6d435db3`. OFF has 64 exhausted turns and four error lines in 17 cells, versus zero ON.
  Original resumed source `9da7bb86f3ab` and paired OFF have byte-identical raw/injected binaries.
- Durable record: `results/contenders/compound/index.json` retains source/opponent/engine identities,
  byte hashes, paired outcomes, speedrun, native failing timestamps/error lines and the gate.
  `bots/main/foundation/` is the canonical shared knowledge bank; old references below are historical.
- No original-bot native jobs are running. Authoritative camp lifecycle (M52) has since been tested
  and retained OFF (see active research); the default model still treats a respawn timer as availability and may
  fabricate a capture at timer zero; fix this independently of execution health. The bank's own-lock
  preference when both teams have altar locks is another source-only liveness gap: the enemy-occupancy
  branch returns without attack or tribute while the preferred handle is own. No current native
  overlapping-lock occurrence, duration or indefinite denial has been proved. Neither is qualified
  as solved by this health panel.

## Historical state (22 Oct, checkpoint before a reboot; older source and engine evidence)
- Sources build (`./arena.cmd build contenders/compound`) and unit tests pass
  (`./arena.cmd test contenders/compound`, tests/models.cpp: camp forecast, Smith estimate, E18 cycle
  calibration, feasibility, death risk, purchases, emblems, horizon, beacon codec, skirmish).
- Last measured version: `contenders/compound@c8ec5aa31d44` built with `--set debug_level=3` (the
  activity log; the release key differs only by that override).
- Health (M1, version a56e7294e6fb, before the latest economy changes): all 40 pool maps x both sides vs
  `anchors/starter-cpp` and `anchors/fundamentals`, 160 games: 0 faults, 0 fuel exhaustion, 0 error lines,
  0 stalls, fuel max 86.6K; 79-1 vs starter, 65-15 vs fundamentals. Run `.cache/contenders/compound/health2`.
  The later changes (parties, hidden-camp invariant, economy model fixes) were run on the 80 speedrun
  games with no health incident printed, but the full 160-game health panel has NOT been rerun since.
- Speedrun vs the passive starter (all pool maps, both sides; `bots/main/tools/speedrun.py RUN`), mean
  rounds to 1,000 banked: compound 1,401 (15 of 80 unfinished, run sr5) against main-v35 995 (2
  unfinished, run `.cache/contenders/compound/ref_main35`). History: 1,453 (health2) -> 1,434 (sr2,
  lambda restated) -> 1,424 (sr3, hidden-camp invariant: 00lost_world 1,024 -> 410) -> 1,418 (sr4,
  parties) -> 1,401 (sr5, health charged once).
- Economy by r400 vs starter (sr5): collected 179 gems, banked 151, earned gold 1,913 (main-v35: 275 /
  237 / 2,411). Clears by r400 per game 22.2: gold clusters 10.4, wolves 5.8, elks 2.9, gem clusters 2.8
  (main-v35 25.6: 10.1 / 7.6 / 5.0 / 2.5). First emblem median r217-366 by champion; first emblem is
  Jungle of the main stat in most games now (sr5 firstbuy), Treasure in about a third.
- Time use r1-400 (sr5, activity log): walking to targets 37%, camp fights 27%, captures 8%, bank walks
  6%, staging/waiting 4%, beacons 4%, recall 3%, dead 2%.
- Not yet measured: head-to-head vs main-v28/v35/hunter7 (M3), gauntlet personas, ladder.

## Architecture (src/)
- `main.cpp`: turn loop; every exception is caught at the turn boundary and logged as
  "compound error ..." (at most 20 lines), then `end_turn()`.
- `config.h`: all knobs (`inline constexpr`). `noise_burn` is the perturbation seed for `--seeds`.
  `debug_level`: 1 decision trace (printf, ~5K fuel a line), 2 fuel profile per phase (printed every 200
  rounds by slot 0, which then yields), 3 activity log (one char per turn, flushed every 100 rounds;
  read with tools/timeuse.py). Diagnostics change fuel use and so trajectories.
- `model.h` (SDK-free): Smith-order camp forecast (fixed arrays, insertion sort), Smith constants
  W/S/H with the O(1) damage estimate W/d - S/2 (E1), party-size law n* (E8), kit damage rates (E2),
  shop plans (legacy knob), skirmish volley model (to be replaced, C19), shared-memory codec.
- `econ_model.h` (SDK-free): the value model. Camp income (gems and gold separately) with health
  charged once as heal trips (usable 80%, trip = recall + 1 + walk back; E4/E18), a loop of the best k
  camps capped by the slowest respawn (M16/E3), death risk (logistic in effective health over the
  exposure of a losing exchange; to be replaced by C21's contact hazard), net income, item bundles
  (stat shards + emblem passives, linear decomposition: 9 farming-rate evaluations), race horizon,
  shop-trip rule.
- `world.h/.cpp`: static map (groups as spans into one flat member vector, per-group Smith W and party
  size, camp tile index, altar tiles incl. gated altars), gem supply rate, lazily cached distance
  fields (group, spawn, altars), at most `field_limit` field computations per turn (4 on the first turn).
- `brain.h/.cpp`: the per-champion controller: snapshot, observation (enemies, camps with beliefs and
  awake evidence, allies), shared memory, derived stats, threat estimate, lambda, camp specs, profile,
  turn flow with committed tasks.
- `farm.cpp`: target choice (bound with each group's party size, then the full per-champion score for
  the best `target_evals`), camp fights (Smith target, casts, watchdog), capture (one capturer),
  party staging, farm loop.
- `econ.cpp`: value shop (spend the purse on the best bundle), recall rule (heal below 30%, shop trip
  by the derived rule), banking (bag 24, detour 6, enemies near), heal trips, walking home.
- `pvp.cpp`: enemy handling (volley forecast; fight if favourable, keep farming if no loss, flee
  otherwise), beacons for isolated enemies, answering beacons.
- `act.cpp`: movement primitives (no allocation, revealed-tile memory, no step into a hidden awake big
  monster's reach), attacks and casts with live visibility checks.
- `draft.cpp`: Vesper, Ryth, Marcella, Vendetta, Heart (3 picks: Ryth, Marcella, Vesper). Not yet
  derived from the economy/combat tables (open, see next steps).

Shared memory (8 bytes, M50): bytes 0-4 one per champion slot (mode 2 bits: farm / converge / fight /
home; value 6 bits: target group + 1, or ETA when converging); bytes 5-7 the enemy beacon (x+1, y,
round mod 64, asks; zero = none). A claim on a group whose party size n* > 1 is also the party request.

## Decisions
1. **Write from the starter, in layers; pure models SDK-free and unit-tested.** Why: tests run under
   WASI only (bots/contenders/tests/run.py). Evidence: tests/models.cpp passes.
2. **Value model of gold (one currency).** Items are bought by marginal gems over the race per gold
   (best bundle of a family/stat incl. its emblem); lambda = that value per gold prices gold in target
   choice; a dedicated shop trip is made when the items' rate x the rounds they arrive early beats the
   trip's farming cost net of the health it refills. Replaces the copied procless plan (`shop_plan` 1 is
   main's order, kept for comparison only). Corrections on the way, each a model error found by
   measurement: (a) the fixed plan ran out (Ryth 8,700 HP) and "walk home to shop" looped forever beside
   spawn (00collision r1000-2000, run tu2); (b) gold earned back by items was valued at the current
   lambda, so gold looked self-reproducing and lambda sat at its cap 1.5 with a horizon of ~1,900 rounds
   (lam run); fixed by a horizon from map gem supply and team labour (E12/E24: supply x 0.6 vs team x 0.25
   gems a round, floor 0.4) and reinvestment at 0.25 x lambda, cap 0.6; lambda is now 0.4-0.6 early,
   matching E18's first-emblem payback; (c) health was charged twice for multi-trip camps (extra trips
   plus a trip share): a base Vendetta elk cycle came out ~125 rounds instead of E18's 85; fixed (tests:
   77 rounds, Jungle saves ~33 with the loop-level respawn cap). Evidence: sr2-sr5.
3. **Committed tasks** (bank, home, heal trip, target) instead of re-deciding every turn: a bank/target
   flip on alternate turns ran 1,000 rounds (00collision, run tu1, "WBWB"). Evidence: tu2.
4. **Progress watchdog and unreachability**: an engagement that removes no camp health for 20 rounds
   bans the group 80 rounds; a target whose cached field cannot reach us is dropped (islands).
5. **Fuel** (foundation S21-S24): no `std::sort`/allocation in hot paths, a camp-tile index instead of
   10K-entry arrays (they cost ~25K to fill), one field per turn (four on the first), the first turn only
   initialises unless 110K fuel remain, valuation cut from 21 to 9 farming-rate evaluations, printf only
   behind debug knobs. Result: fuel max 78-87K, zero exhaustion on 160 games (health2).
6. **Safety invariant: no awake camp left out of the accounting, including unseen ones.** Bushes hide
   camps from champions but not champions from camps (M12, M46); proximity never wakes a camp (M9). An
   unseen monster counts as a threat for 12 rounds after evidence it is awake (seen awake, or health lost
   in its reach that nothing in view explains); we never step into the reach of such a big monster.
   00lost_world: 8 deaths to the bush-hidden monolith -> 0, win r1,024 -> r410 (main-v35 r400). A first
   version blocked every hidden big monster and oscillated at the zone edge (lw2), hence the evidence rule.
7. **Threat from observed exchanges, not rosters**: the survival value of Health uses the envelope of
   champion damage a contact turn actually puts on us (prior: half the two strongest enemy kits, weight
   20 turns). Against the passive starter Health then buys little; against hitters it buys more.
8. **Camp convergence: target-sized parties via claims (E8, G8).** Every group has n* at base; a claim
   on it is the request; allies join while the party is short (per-champion rate with shared reward,
   pooled health, capture share); a founder stages outside reach up to 14 rounds, then fights alone if
   still feasible or bans the group. Aggro: priority = effective health / 10 (M6: ties go to the earliest
   actor, so the toughest takes them). Evidence: sr4 elk groups by r400 1.8 -> 2.5, gems collected 149 ->
   164 (small; monolith/golem sieges with waves and cover, E9, are not implemented).
9. **One capturer per group** (E3): an ally on a member tile takes it; others move on.
10. **Execution cannot borrow a future turn.** The current-bundle v40 baseline exposed 21 exhausted
    turns and two tribute errors across 80 keys. Native timestamps put 20 exhaustions at round 1,
    once per champion on Islands and Yellow Brick Road (both sides). Startup markers on side A
    leave only 42,815 / 44,116 fuel after custom groups, before altar and identity work. Yellow
    Brick Road is only 40x30: this is layout complexity, not simply map area. Quadrants A also
    exhausts in Ryth's round 144: optional repricing (~30K) and farming (up to 48K) share the turn
    with observation/PvP. Implemented behind `bounded_turns=false`: static construction resumes
    only at explicit group/altar boundaries (55K reserve); Brain publishes completion only after
    identity is ready and queries a fresh gameplay snapshot afterwards. Optional repricing moves
    after actions and requires 85K before the initial camp-spec build; target search checks 35K/30K
    reserves. A shop-heavy spawn turn yields below 45K. Tribute checks a fresh stun buff through
    the actual `execution.h` helper; unit cases include a zero-timer buff still present and renewed
    stun after a formerly ready query. OFF binary preservation passed, then the full paired panel
    and current-default health gate above passed; promoted only this execution arm. Healthy
    completion does not establish arbitrary-layout fuel bounds, fight quality or strength.

## What works / what does not
- Works: health on all maps (pending a rerun after decisions 6-9); value shop; parties form for elks;
  lost-world fixed; commitment.
- Does not yet: speedrun ~400 rounds behind main-v35 (walking 37%, too few elk/monolith captures,
  gold-cluster heavy); 01islands (dash-only ground, P6: walking reaches 8 groups) and 01boss_battle
  (altar gated by the monolith pair, P5) never finish; no monolith sieges; PvP is a volley model with
  no turn-slot tempo, reach discipline or escape condition (C2, C8, C16, C19); bag fixed at 24 (E22 says
  carry freely out of contact); draft not derived.

## How to resume
Read Active research and `results/contenders/compound/index.json` first, then inspect live agent/job
handles. The original action-cycle pilot has finished; do not restart it. Private capital work and
native observation are child-owned, so coordinate before launching overlapping jobs. Existing result directories
are immutable source-bound evidence; do not resume the old OFF-first sweep with today's promoted
default. A fresh comparison can use a new output directory and `--var "legacy: bounded_turns=false"`
against today's healthy baseline. The current health qualification can be inspected with
`./arena.cmd report .cache/contenders/compound/health-promotion-gate`.

For a future change, run `./arena.cmd test contenders/compound`, then a same-run paired panel over
all pool maps/both sides, with explicit legacy/current arm labels. Preserve this health gate as
historical evidence if the source or engine changes. The next promising economic mechanism is
that `bag_detour=6` is actually distance to the altar, not extra route distance: it can spend a
12-step round trip for one gem. Sr5 spends 6% of early turns and 10% of whole games walking to bank,
versus 1% channeling. A future route-aware trigger must retain contact/deadline/finish safeguards,
Marcella's banking income and teammates' carried bags. Pure unbounded carrying is not yet justified.

### Historical speedrun reproduction commands
The following commands describe the earlier sr5 workflow (current defaults now differ):
```
./arena.cmd play contenders/compound anchors/starter-cpp --maps pool --set debug_level=3 --keep-replays all --out .cache/contenders/compound/sr6
python -B bots/main/tools/speedrun.py .cache/contenders/compound/sr6 --opponent starter-cpp [--by-map]
python -B bots/contenders/compound/tools/collect.py .cache/contenders/compound/sr6
python -B bots/contenders/compound/tools/caps.py .cache/contenders/compound/sr6
python -B bots/contenders/compound/tools/timeuse.py .cache/contenders/compound/sr6 --by none
python -B bots/contenders/compound/tools/firstbuy.py .cache/contenders/compound/sr6
python -B bots/contenders/compound/tools/measure.py .cache/contenders/compound/sr6 --by map
```
(python = `C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`.)
Reference per-map speedrun of main-v35: `.cache/contenders/compound/ref_main35` (995 mean).
Health panel (rerun before M2/M3 claims): `./arena.cmd play contenders/compound
anchors/starter-cpp,anchors/fundamentals --maps pool --keep-replays all --out .cache/contenders/compound/health3`.

## Next steps (in order)
1. Keep current health qualified and all null research arms OFF. Finish actual dash-follow-step
   observation; prioritize the private capital model's measured planning/farming fuel competition.
   M52's observation/reservation/deadline distinction is tested and correct but strength-null;
   the dual-lock bank liveness gap remains separate.
2. Economy, by mechanism (opponent-independent; speedrun and r400 curves):
   - walking 37%: inspect target travel vs E13 schedules (bank where we farm, recall as transport on
     travel maps, E14), retarget hysteresis, beacon walks vs a passive enemy (4%);
   - bag policy from E22/C21: carry freely with no enemy within 6, bank when an enemy enters its
     two-turn reach or at altars passed;
   - monolith/golem sieges in waves with cover (E9), as team convergence;
   - dash edges for 01islands (P6), the gate race on 01boss_battle (P5);
   - draft from the clear tables (E6/E10) and C3/C6 (Heart on Focus).
3. Survival model from C21 (hazard by contact state, allies, health) and C8 (escape margin) instead of
   the logistic; PvP rebuilt on C2/C16/C20: per-pair tempo, hold outside the two-turn reach, escape
   condition, melee retreats hit back, convergence (square law C11) on isolated enemies.
4. M3: head-to-head vs main-v28, main-v35, main-hunter7 (80 games each, both sides) and the gauntlet;
   diagnose losses by mechanism (replays: `./arena.cmd report game PATH --positions`).

## Answers to opponent behaviours (foundation O1) - draft, to be derived and tested
Early aggression: not yet answered (no reach discipline); planned: C16 holding + escape margin + allies
within 6 (C21). Packs: never the lone champion within reach of two (C20 rule 2, not yet enforced).
Dispersed teams: convergence on isolated enemies (beacon exists, model weak). Gem rush: lambda falls as
the horizon shortens (gem supply x share), gem clusters valued at full gem worth; not measured. Scaling
teams: the value shop keeps buying the best payback item every visit. Spawn camping, locks, chases,
kiting, bush ambushes: not yet answered beyond the hidden-camp invariant.

## Safeguards (G10/C20) - status
Enforced: no awake camp left out (incl. unseen, decision 6); recall only when no threat can reach us
(safe_to_channel); clean health on the current 240-key pool panel (decision 10; not a proof for other
engines/layouts/opponents). Planned: rules 1, 2, 3, 7, 8 with the
PvP rebuild; unit tests and game witnesses to be added with them.

## Foundation-worthy findings (for the coordinator)
- Fuel: `World`-style 10K-entry std::vector fills cost ~25K fuel on 100x100 maps (byte loops); an O(n^2)
  insertion sort of ~120 camp tiles cost ~57K; printf in probes distorts fuel profiles (S22 confirms).
- An unseen awake monster in a bush is a camp-death trap the threat lists of every bot miss (M12/M46):
  on 00lost_world it killed 8 of our champions by r450 until evidence-based awareness was added.
- A value model that reinvests gold at the current lambda diverges (gold looks self-reproducing) when
  the horizon is long; the horizon must come from map supply and team labour, not from one's own pace.

## Rejected / superseded
- Copying procless's plan and thresholds (shop from 90 gold, Treasure/Health first): superseded by the
  value model (decision 2) per the coordinator's doctrine; the plan remains as `shop_plan` 1 (main's
  order) for comparison.
- Hard-blocking every unseen big monster's reach: oscillation at the edge (lw2); replaced by evidence.
