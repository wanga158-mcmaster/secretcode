# warband: notes

Retired intact on 23 October 2026 during the user-requested workflow reset. This is the old
implementation and historical evidence. The active restart is `bots/contenders/warband`.

## Retired-runtime health discovery (24–25 October 2026)
- The complete closed `strength-baseline/floor52` panel binds this Misc default to
  `miscellaneous/warband@2dad72d88ee6`, WASM SHA256
  `3f278a720f42c1fad2db0ebbe89bd11516219d92035ae3e2324847378a16ffa1`:
  1 strict win/80, 27 games with 43 error lines, no recorded faults/exhaustions/truncation,
  and no opponent flags. Its only win is unhealthy; this is historical Main v52 evidence.
  LIVE advanced to Main v53 during diagnosis; no current-strength claim transfers to it.
- Exact cached winning recipe `2d47a956a90e801315316615a774e058`, Concentrated Wealth B,
  logs recall E44 at rounds 888 and 1309. Native state ties these to stunned Tavi7
  (510/1050 HP, restrictions 63) and Ryth5 (1410.27/3300, restrictions 63 after a stun
  cancelled recall). Both recall successfully on the following turn. Failed SDK calls have
  no action receipt: actor attribution combines the exact log, state and production path.
- The winning game does exercise the retained party: Marcella6 attacks/resets central Elk0
  at r875, Ryth5 double-attacks at r877 and Cassi8 finishes it at r878; group0 capture at
  r884 pays Ryth +36 and Marcella +35 gems. Marcella banks 39 at r896 and Ryth banks 52
  at r899. Cassi reclears the elk at r997 and the next group capture pays Ryth/Tavi at r1019.
  This is a native clear/capture/bank/renewed-work word, not causal attribution of the win.
  Source dispatch is informative: the elk makes group0 a static `pack_job`, `task_turn`
  rejects living pack jobs, and only it or `converge_turn` can call `clear_group`; the
  observed attacks therefore imply convergence on the pack objective in clear mode.
  Private shared-focus and capture-assignment choices were not directly read or isolated.
- Reuse the existing `action_guard` for the health comparison, with `camp_retarget=false`.
  Its settled historical 160-key v40/starter arm removes 39 errors to zero, with no outcome
  flips and peak fuel 81968 -> 82061, while two Lost World zero-clear stalls remain.
  Do not change fighting ratios, leader election or pack strategy to repair these E44s.
  The guard remains OFF pending a clean complete current-reference panel and a strict win.
  The compact [native discovery proof](../../../results/population/warband-current-strength/index.json)
  binds the original recipe/payload, selected event receipts, source paths and trace hashes;
  raw replays are regenerated when needed rather than retained as evidence.
- The existing guard builds as `miscellaneous/warband@341a6d9f04c4`; RAW and injected modules
  both SHA-match the settled historical guard `a2057c76d2b74d96bf13adce685604c77a1b5cc232b9112700da07f8329874ae`.
  Registered original model/protocol and production camp-scheduler fixtures pass 2/2.
  `warband-audit/guard52-rebind` preserves the 874–576 win with zero health flags and an
  entirely byte-identical public replay payload (`aba3fe27574907c4aa10df7377e349bc750167f5cd4a09475332b462d6f0518c`).
  The two E44s disappear; next-turn legal recall and the witnessed productive party word remain.
- `warband-audit/guard53-pilot` covers Concentrated Wealth both roles against released
  `anchors/main-v53@fa5b8ae8b5ae`, RAW `bd6b051394b36df5849805130976acfab1a0b4361d412dc0f26c886d72419b61`.
  OFF and guard both lose 0/2, with every non-health row metric identical. B's OFF recall E44
  at r1317 becomes zero; B's full public replay is byte-identical (`3c7a4e69397febea19f398c0adcc4c966adc3f6fa6b58a82542d64a567271a79`).
  Stunned Ryth dies before the next owner turn, then shops while dead: this B pilot proves
  physical noninterference, not living continuation after refusal. No current win floor yet.
- The complete `warband-audit/guard53-full` panel covers 40 pool maps, both roles, 80 keys/arm.
  OFF is 1 win/79 losses with 36 error lines in 23 games; guard is 1/79 with zero recorded
  errors, faults or exhaustions, and Main has zero flags. Both win Starvation A, 382–372 gems.
  There are no outcome flips; four keys change other metrics (Fragment B, Lanes A, Stampede B,
  Elk Arena A). These are finite pool/reference measurements, with no heldout or improvement claim.
- Lost World has no own camp captures in either guard role; B's 21 gems arrive with a kill.
  The old zero-clear classifier alone is not a technical-stall diagnosis for this fighting bot.
  Actual A r350–413 casts and ranged basics hit personally visible enemies inside the retained
  seven-tile spawn chase guard; B r180–240 actively moves, casts and kills. Preserve these costly
  fighting/refusal decisions. Neither window has decoder truncation or query failures.
  There is also an unresolved individual liveness limit: Marcella A makes 64 legal moves among
  four tiles, 58 owner ends on the (10,23)/(11,24) pair, with no personal enemies at 61 owner ends
  and no attacks, casts or resource gain. The `follow_anchor` near/far routing cutoff is a plausible
  cause, but private anchor/objective/mode and actual branch were not read. This does not prove
  an unreachable route or technical fault; whole-team PvP also does not prove her cycle healthy.
  Do not describe the complete panel as proving global absence of stalls.
- Promotion is held for a bounded read-only check of whether replay state exposes actual shared
  `PackMsg` bytes for that follower window. Current source/default is restored to
  `2dad72d88ee6` (`action_guard=false`, `camp_retarget=false`). The already-built, unpromoted
  default binding `6ade335a9cf3` has RAW/injected bytes identical to measured guard
  variant `341a6d9f04c4` (`a2057c...` above); it needs no rebuild if the later decision accepts it.
  Do not treat that artifact as the current default or grant qualification while this reliability
  gap remains unresolved. No draft, fighting, leader, party or navigation policy was changed.
- The bounded official shared-memory ABI probe succeeds (capacity 8, status 0) but returns the
  same eight zero bytes at initial state and exact Marcella owner-ready stops 350, 351, 352,
  360, 400 and 413. Repeat, canary and public-state neutrality checks pass. Expected current
  leader stamps 94, 95, 96, 104, 144 and 157 and nonzero availability are absent. Historical
  `PackMsg` restoration is therefore not established; never call the decoded sentinel shape
  an actual empty objective/anchor or travel mode. The suspected routing handoff remains
  unproven. Stop this bounded line with the legality guard measured but unpromoted, the
  follower-cycle reliability limit explicit, and current default/qualification unchanged.
- A separately authorized default-OFF `nav_journal` now observes only ally index1 at r350/351.
  It copies already-read pack fields and actual navigation calls/steps, adds no world/vision
  queries, and flushes once after action/shared-write/ward handling only above 40000 fuel.
  The recorder's production fixture plus the original two fixtures pass 3/3. OFF source
  `0538f97d70de` remains byte-identical to old RAW/injected `3f278a...`; guard source
  `7f793ee1abcc` remains byte-identical to measured `a2057c...`. Both diagnostic knobs default OFF.
- `warband-audit/nav53-journal` has exactly two diagnostic keys, matched silent/emitting arms
  on Lost World A/Main53. Their WASM files differ by one data-section byte, 17 -> 18 at offset
  157531; code-section SHA256 is identical. Both complete native replay payloads are exactly
  the preserved guard payload `e6e6f87c30b6fcf53bb8df4a2dee7a3b9fad3825063dfe1b6238f0295698acac`.
  Silent stdout is empty; emission records exactly two untruncated calls with 88990/86087 fuel.
  Actual read/dispatch/follow pack fields are objective6, anchor(2,19), FIGHT, stamps94/95.
  At r350, (11,24), distance9: actual action branch7 calls far follow -> group6 within4 ->
  field step to (10,23). At r351, distance8: the same pack state selects near follow -> anchor
  (2,19) within1 -> field step back to (11,24). This establishes the contradictory routing
  handoff; it does not establish that either destination is unreachable or prove a repair.
- Minimal health-only proposal, not implemented: behind an OFF knob, FIGHT/RETREAT followers
  should consistently route to the requested anchor through existing bounded `goto_tile`,
  without switching/falling back to the retained economic objective. Keep the current travel,
  gather/clear routing, fight selection/ratios, leader, draft and pack strategy intact. Any such
  change needs its own bounded native continuation and complete current-reference test.
  Existing action guard remains OFF; the measured Main53 1/80 is separate from this diagnosis.
- After separate authorization, `anchor_follow_fix=false` guards the minimal production dispatcher
  in `follow_route.h`: only follower FIGHT/RETREAT requests use one existing bounded anchor route
  with no economic fallback. Other modes and leaders retain their old routing sequence. The actual
  production-dispatch fixture covers both cutoff distances, blocked-anchor refusal/retry, other
  modes and leader behavior; all four registered Warband fixtures pass. New OFF `9ae11e2d2ea5`
  preserves RAW/injected `3f278a...`; new guard `1747f30bf5d6` preserves `a2057c...` exactly.
  Combined candidate `748c5ec54179` has RAW/injected SHA256
  `ed51344052537304f50a71a22ffcc06ac56516ca8c8d407b6de8433ccc962365`.
- Closed `warband-audit/anchor53-pilot` compares guard/fixed on Lost World both roles, four games,
  zero recorded flags on either team. Both arms still lose both roles. Fixed A deaths rise 8 -> 15
  and collects 21 rather than 0; fixed B deaths fall 6 -> 0 and collects 0 rather than 21. These costs
  are real; no strength gain follows. B's no-income game remains active legal defense: native casts
  at 180/184/185 damage and stun personally visible enemies while movement continues.
  In A350–413 the former follower now has 23 end-turn positions, 48 moves and a legal recall406,
  replacing the old four-tile handoff cycle. Its native kill receipts are r156/r164 (track reward
  snapshots r157/r165), with +100 gold each and 21 carried gems on the second. This witnesses
  continuation in the repaired replay, not isolated causal benefit or global absence of stalls.
- Local LIVE and frozen bytes were rechecked as Main v53 (`main/main@e39f62accd4a`, anchor
  `fa5b8ae8b5ae`, RAW `bd6b0513...`). Unchanged combined candidate now runs the complete 80 keys;
  the settled guard80 is reused on identical keys. No threshold tuning or additional repair follows
  a null. Both health knobs remain OFF until that floor, recorded health and the known gap pass.
- Closed `warband-audit/anchor53-full` completes that unchanged combined candidate on all 40 pool
  maps and both roles: 1 strict win/79 losses, zero recorded flags on either team. Its only win is
  Starvation A, 471-277 gems. The identical-key guard80 has no outcome flips; paired gem-margin
  difference is +8.8125 (SE 10.7366). This is a finite current-reference floor, not a general strength
  gain. The Lost World pilot costs above remain part of this decision. The original guarded-only
  module is not certified; promotion still awaits actual current-default RAW equality to `ed513440...`.
- The NEW `ed513440...` winner has its own native party word. At r1851-1853 all six champions
  attack or cast on live Elks in four-Elk group2: Ryth0 reduces Elk4 200 -> 122 -> 44 and Marcella1
  finishes it, while the remaining members help finish Elks3/2. Group2 becomes reward-available.
  Capture r1859 pays Vesper4 440 gold and 88 gems. Every one of 133 consecutive track samples
  through r1991 retains exactly 88 gems with positive health (minimum 350). Native banking r1992
  changes her bag 88 -> 0 and score 383 -> 471; the same round she moves (36,14) -> (37,15).
  Direct basics on living static `pack_job` Elks imply `converge_turn` clear/objective2 dispatch:
  only it and `task_turn` call `clear_group`, and `task_turn` rejects living pack jobs after observation.
  This is source-dispatch inference, not a direct read of this game's private PackMsg. The selected
  final three-Elk work does not show all four cleared from full health; actual reward/banking does
  not isolate private assignment choice or counterfactual benefit. Later movement proves resumed
  legal action, not a second Vesper payout. This word belongs to the new module; the earlier v52
  word retains its separate old-module scope. Exact event/source/map/module hashes and bounded
  decoder coverage live in the compact proof linked above; the one replay recipe is regenerable.
- The authorized final default binding stopped before compilation: its strict private-context scan
  after shared-slot admission rejected an incomplete classification (access/vanished/unknown-reparse
  gate). The helper did not persist individual counters, so the category remains unknown. Its Job
  exited empty; both health defaults were immediately restored OFF and source `9ae11e2d2ea5`
  reverified. No actual default RAW was produced and no retry launched. The qualified combined
  candidate remains a variant; default promotion is held pending a successful actual binding.
- A separately authorized metadata-only diagnostic persists every scan counter before refusal.
  It has zero access/vanished/readlink errors and identifies eight links outside the overly narrow
  snapshot-SDK shape: two declared orchestration SDK mirrors and six declared synthetic path-test
  aliases. Readlink targets match their fixture declarations; no target was traversed. Propose only
  those exact path/target exceptions, keeping unknown links blocked. The later cache epoch is also
  10,000,856,591 bytes, so the unchanged 10 GB/40 MB gate independently refuses compilation.
  This is tooling/capacity evidence, not a bot health fault; it cannot recover the omitted counters
  of the original attempt. Defaults and candidate status remain unchanged.
- After separate measured capacity, the corrected in-slot scan passes at 9,833,285,296 bytes,
  with all eight declared links matched and no unknown/readlink/access/vanished entries. The final
  default build is `miscellaneous/warband@a397f2074dca`: only `action_guard=true` and
  `anchor_follow_fix=true`; `camp_retarget=false`, `nav_journal=false`, emission 0. All 20 source
  files exactly match the tested combined variant's contents. Actual RAW and injected modules are
  both 941,509 bytes, SHA256 `ed51344052537304f50a71a22ffcc06ac56516ca8c8d407b6de8433ccc962365`.
  Toolchain `3837f00416ea` matches the measured spec and snapshot. The private Job exits empty;
  immediate trim frees 1,907,181 bytes and a complete post-trim scan is 9,841,769,723 bytes.
  Local LIVE and frozen RAW are rechecked as Main v53 before final qualification. This promotes
  the combined health repair as the local default using the existing 1-win/80-key panel, without
  repeating identical game modules. The old guard-only artifact remains unpromoted. Draft,
  fighting thresholds/ratios, leader election, pack strategy and economic-mode routing are unchanged.
  Retain the finite pool floor and the bounded target-contract/contribution evidence; neither
  general strength gain nor global absence of stalls is established.

Line owner's record (direction from the contenders coordinator, `../README.md`). Thesis: fights decide
games; one cohesive pack wins them by numbers and owns the rich ground. Grounded since 22 Oct in the
foundation ([canonical bank](../../main/foundation/README.md)): G4/G8 (the pack is the siege party for the elk/golem/monolith content
that holds 76% of the gems, P3, E8, E9), C11-C13 (square law, focus fire), C20/G10 (safeguards).

## State (22 Oct, resumed on CLI/SDK 3.0.5, engine 1.0.3)
- Active reconstruction now lives in `../_warband-coalition/NOTES.md`, with coordinator approval
  to borrow frozen v40's productive substrate while developing distinct useful coalitions. The old
  bot below remains intact and unpromoted. Its three proximity vetoes, serialized production and
  unsafe monolith transit motivate rebuilding; historical absolute refusal/spacing rules below
  are not current invariants. Canonical C11/G4 restrict the square-law approximation, and C14/G10
  correct radius-one free-center AoE spacing to pair distance at least three for universal pair
  exclusion. Productive conditional safety must replace universal refusal.
- The live target advanced during this work through v41 to `anchors/main-v42`
  (`main/main@0353a987e04f`, live03:17:42UTC).
  Retained v40 causal panels keep their exact opponent identity; eventual qualification includes
  live v42 and the then-current main. No online actions are authorized.
- Current default `contenders/warband@d90267e0fb82`; `action_guard=false`, `camp_retarget=false`.
  Both raw and injected WASM remain byte-identical to original `3290f993fd84`, SHA256
  `3f278a720f42c1fad2db0ebbe89bd11516219d92035ae3e2324847378a16ffa1`. Build and two test files pass
  (including seven cases of the production basic/reset scheduler).
- Source hygiene: ten legacy files are now LF. Final default/guard/pack source keys are
  `d90267e0fb82` / `af8cd00d07d2` / `8e12a2e9a537`; each rebuild matches both raw and injected
  WASM of evaluated `048fcda6fdde` / `9e67c2c68be8` / `0bfc71c1162d`, respectively. Historical
  game identities below remain unchanged. Durable binding: `results/contenders/warband/source-bindings.json`.
  No native games were repeated for the newline-only change. All 21 audited source paths are LF.
- The retained causal bar is frozen `anchors/main-v40@6f62c56b73f7`. Final same-source sweep covers all
  40 pool maps, both sides, v40 and starter: 480 full games, 160 per arm. Default/guard/pack each
  lose 0-80 to v40; starter records are 65-15 / 65-15 / 67-13. Guard `9e67c2c68be8` removes all
  39 baseline errors (36 recall E44, two tribute E45, one locked-altar E125). Both enabled arms
  retain two zero-clear stalls (Lost World, both sides); all arms have zero faults/exhaustions.
  Guard-to-pack isolated result: +3/-1 flips, p=.625, gem margin difference -7.7 +/-13.4 SE;
  no detectable strength gain. Peak fuel: default 81,968; guard 82,061; pack 94,219.
- Actual `attack_camp` contribution is established on a reachable mixed-camp native fixture,
  four bounded 56-round games, both subject sides. Tavi's lethal Wolf cast preserves a basic:
  OFF leaves Gold 120, ON basics it to 75. Ryth's AoE leaves Gold 20, then ON basics it to zero.
  This is conditional action packing, not a productive economic cycle or strength result.
  The first verifier's overbroad assumptions failed and are preserved; a separate pure,
  kit-specific verification passes without rerunning games. Independent replay decoding passes
  all four saved windows: eight Tavi/Ryth caller sequences, four matched OFF/ON queried actor
  states, actual actor/target/effect checks. Evidence:
  `results/contenders/micro/warband-production-r54/production-verification.json`.
- Both owned caller drivers now select native ordinary budget from fuel header index3 for A,
  index7 for B; the previous duplicate A threshold could misreport asymmetric fixtures. The
  asymmetric 100/80 regression passes for both drivers. Independent saved-header audit of eight
  packing/focus caller games (16 streams) finds all A/B budgets 100,000, so prior health claims
  remain unchanged. `results/contenders/warband/caller-fuel-header-audit.json` binds immutable
  original reports; no games or report bodies were rewritten.
- Durable evidence and exact source/knob identities: [index](../../../results/contenders/warband/index.json).
  Full sweep `.cache/contenders/warband/sweeps/resume-action-packing-r2`; the earlier sweep without
  the reviewed dead-camp/dual-lock corrections remains historical. No promotion or release:
  M1 still fails health. No jobs remain running at this checkpoint.

## Historical state (22 Oct, checkpoint before a reboot)
- Build key `contenders/warband@3290f993fd84` (sources in `src/`, unit tests `tests/model.cpp`, both
  build and pass: `./arena.cmd build contenders/warband`, `./arena.cmd test contenders/warband`).
- Milestone: M1 nearly met. Health on all 40 pool maps, both sides
  (`.cache/contenders/warband/runs/m1j`): vs starter-cpp 65-15, vs fundamentals 63-17; 0 faults,
  0 fuel exhaustion, 0 stalls; 5 error lines (all "recall: Champion cannot act", a recall attempted
  while stunned; fix listed below). Economy is weak: speedrun vs starter 1,765 rounds mean, 37 of 80
  games unfinished (main ~994; foundation schedule 674, E13). Not yet measured against main.
- No run, sweep or background job is in flight.

## Architecture (src/)
- `config.h` all knobs (`inline constexpr`; `noise_burn` is the perturbation seed; `debug_every`,
  `debug_budget` are diagnostics, off in measured builds).
- `protocol.h` the 8 shared bytes (pure, unit-tested): focus group+1, anchor x+1/y+1, mode | focus
  enemy+1 | sighting age, leader stamp, availability bits, busy bits, sighting offset from the anchor.
  All-zero memory decodes as "nothing".
- `model.h` pure models (unit-tested): Smith-order camp clear forecast (D = W/d, E1), Lanchester force
  ratio, focus score.
- `world.*` static map (one `map::camp_groups()` call, plain data, lazy per-group bitboards and
  distance fields), camp-group beliefs (per-monster health persists while asleep, M10; respawn timers
  counted down, M49), static `pack_job` flag per group.
- `sense.*` kits decoded from `champion_designs` (stun/nuke/aoe/reset/dash/shield slots), one
  observation of visible champions per turn via held handles.
- `bot.*` turn loop, leader election (lowest-index available ally), the leader's focus-group choice
  (pack rate from the anchor, hysteresis), anchor steering (walks the focus group's field, paced by
  busy members), gather/clear transitions, focus-group progress watchdog.
- `act.cpp` member actions in order: fight, recover, capture, bank, converge on the focus group,
  zone task, follow the anchor. `fight.cpp` force ratio, focus choice, stun stagger (cast only when
  the target has no pending stun, M26/C10), damage casts, resets, retreat. `nav.cpp` field stepping
  with sideways steps past blocking allies, bad-tile memory, bounded then full searches.
  `shop.cpp` Treasure(main) -> [tanks: Jungle(Health)] -> Bruiser(main); wards (knob, off).
- `draft.cpp` mode 0: Ryth, Marcella, Tavi, Cassi&Issac, Vesper (first n picks).

## The design as it stands: a flock, not a ball
The pack has one anchor (shared memory). Members harvest the camps whose centre is within
`zone_radius` (6) of the anchor as solo tasks (clusters, single wolves: groups a base champion can
clear, `pack_job` false), and converge on the leader's focus group (`pack_job` true: elk groups,
wolf pairs, golems, monoliths) when the leader calls gather (edge of its range) and clear. The anchor
walks the focus group's distance field one tile per round, holding while more than `busy_hold`
members are busy (at most `anchor_patience` rounds). Fights: the leader calls fight/retreat from the
local force ratio and writes a focus target; every member engages with ratio >= 1.3 (or >= 0.75 when
the pack has called a fight), retreats below 0.75.

## Decisions (newest last)
- W1. Start from a clean page, layered as the foundation (model -> decisions -> knobs). Probe
  (`bots/contenders/_warband-probe`, run `.cache/contenders/warband/runs/probe2`) confirmed before the
  foundation landed: teams interleave turns per champion; a stun costs the victim exactly its next
  turn (duration reads 1 until consumed, 0 after); hidden allies throw E97; `is_alive()` works on
  hidden enemies (used to count enemy deaths).
- W2. Fuel: `map::camp_groups()` once, plain data per monster, no SDK objects copied per member,
  lazy per-group bitboards; allocation-free forecasts with branch-and-bound over groups. First-turn
  exhaustion on 42-group maps (runs m1b: 92 exhausted turns) -> 0 (m1c on). Held ability copies:
  `ability.is_ready()` costs ~480 (S7), `cooldown()` on a held copy ~40.
- W3. A tight ball (leash 3, everyone on one group) does not farm: speedrun 1,879, 63/80 unfinished
  (m1f). Time budget (`tools/budget.py`, 6 maps): follow 119 + idle 76 of 400 champion-rounds, tasks 23.
  The structural cause is G4/E8: captures do not speed up with allies, and wolves/clusters (19% of
  gems) are solo content. Answer adopted: the flock (detached singles on solo content inside the
  zone, the whole pack on pack jobs). Zone 6 vs 12 changed little by itself (budget runs, 12 games):
  the binding problem was positioning, not radius.
- W4. Pack strength for planning counts every available ally at its last seen state
  (`ally_memory` 25 rounds), not only visible ones: vision-only counts flipped the focus group every
  turn when a bush hid allies (01corridors oscillation, dbg runs).
- W5. Movement robustness (each was a total stall, zero clears, in m1b-m1i): all-zero memory used to
  decode as an anchor at (0,0); melee attack tiles must include the target group's dead tiles
  (00quadrants golem block); ranged members waiting for a tank must stand behind it and wait at most
  `hold_patience` (01fracture corridor deadlock); a blocked best step allows a sideways step;
  `goto_tile` retries with a full-map bounded search before a greedy step; far members walk the focus
  group's static field (01concentrated_wealth walls); a refused step onto an unseen occupant (E22)
  marks the tile bad for 5 rounds. Plus the focus watchdog: no progress for 40 rounds in gather/clear
  bans the group 80 rounds (safeguard for deadlocks not foreseen). m1j: 0 stalls in 160 games.
- W6. Enemies resting on their own spawn are not a fight unless within 2 (they heal every turn):
  00rushbait_v2 (touching spawns) froze the pack in fight mode.
- W7. Explicit action legality (`action_guard`, default off) precedes shopping and the complete
  action chain; alive/current-turn/channel flags and current cast restrictions prevent stunned
  recall/tribute attempts. A locked altar requires an attack allowance. Query enemy lock occupancy
  explicitly: both teams can lock one altar, and the SDK's singular accessor prefers the allied
  lock. Final paired 160-key coverage removes 39 errors but does not resolve the two stalls.
- W8. A target is not a plan for the remainder of a turn. `camp_retarget` (default off) refreshes
  visible member state after a cast/basic, selects again, and allows at most one reset basic using
  the production `camp_basic_sequence` scheduler. Dead `is_visible()` can be false; license
  `is_alive()` with current personal visibility of the cached spawn, clear dead local state, and
  never read dead health. Each member scan checks the fuel reserve. Actual caller Tavi/Ryth native
  contributions are verified conditionally; full-pool strength remains a null. Vesper's 85-HP Wolf
  consumes two basics, so no third Gold attack is expected. The original fixture gate that assumed
  otherwise remains failed in `production-packing-r2/packing.json`; `verification.json` records the
  corrected pure interpretation. Foundation U1-U4 supplies the general action rules.
- W9. Saved current-v40 losses expose two different structures. Lost World A has nine deaths to
  monolith camp 36 and 5,945 camp damage taken. Lost World B deals only 400 camp damage in 413
  rounds, with its first death at round 298: nearby enemies can switch the leader to fight/retreat,
  skip objective/anchor updates, and veto zone tasks despite no actual engagement. Public tracks
  support prolonged proximity and inactivity; the private branch explanation is source-consistent,
  not directly traced. Across all 80 v40 games there are 1,260 champion-caused and 146 camp-caused
  deaths. Fix productive threat arbitration and safe approach before spending more effort tuning
  this packing arm. Navigation's soft awake-camp penalty is not a safety invariant.

## What does not work yet (the next steps, in order)
1. Finish health: `action_guard` removes the current 39 action errors, but remains default off
   while the two Lost World zero-clear failures block M1. Establish a bounded productive economic
   continuation under noncontact threats, then a safe monolith approach/escape policy; use W9's
   saved losses as family evidence. Recheck actual guards and both sides; zero errors alone is not
   clean health. Debug output stays compiled out at `debug_every=0`.
2. Economy to the foundation's structure (opponent-independent, measure with
   `python -B bots/main/tools/speedrun.py RUN --opponent starter-cpp` and `tools/budget.py`):
   - party size per target from E8's law n* = sqrt(W T_trip / (delta h walk)) (pure function in
     model.h, unit-tested): only the n* nearest free members converge, the rest keep zone tasks;
   - recall with cover (E4): a covered member spends its whole pool; recall at the exit reserve, not
     at 50% (`recall_frac` 0.5 now);
   - shop for throughput first: Jungle(main) -> Monster -> Power (E20), Focus on Heart/casters; spend
     the purse at every spawn visit and while dead (M4, E17);
   - banking: carry freely out of contact, bank at altars passed, bank or break off when an enemy
     enters its two-turn reach (C21, E22);
   - capturer choice by gold bonus (Vesper on gold groups, Marcella banks, E23, M37);
   - golem and monolith sieges in covered waves (E9; would be a foundation finding: report rounds and
     damage).
3. Fights (M2): replace the Lanchester ratio with a per-pair turn-order model (C1/C2/C19: slots,
   movement-dependent hit rates, stuns as turn denial, arrivals within the fight's length, monsters),
   and enforce the C20 safeguards as invariants with unit tests: (1) no contact without a way out,
   (2) never the lone member within reach of two enemies (zone tasks stop on a sighting already; make
   it an invariant), (4) focus one target and stagger stuns (done: pending-stun check), (5) two tiles of
   spacing against area kits, (6) never nearer than the enemy to an awake camp, (8) no even fights.
   Priority as a lever (G9): set the stunners/tank slots a round ahead.
4. Draft for group fights and sieges (C3-C6, C14, C15; E10): Heart (Focus) and Marcella grow with
   gold; C&I fades; measure alternatives with `draft_mode`.
5. M2 evidence: cohesion and group clears per game (`tools/track.py` cohesion line), engagements with
   focus fire (replays). M3: `./arena.cmd play contenders/warband anchors/main-v28,anchors/main-v35,
   anchors/main-hunter7 --maps pool`, then diagnose losses at the model/structure level.

## Answers to opponent behaviours (O1) - draft, to be derived and tested
| Behaviour | Warband's answer | Status |
| --- | --- | --- |
| Early aggression | the pack opens together on its own side; numbers win the first contact (C11) | partly: opening is grouped |
| Grouped play / other packs | count every arrival; refuse even fights (C16), fight where they must be | planned (model) |
| Dispersed solo farmers | converge on the lone one inside the zone, stun-chain, focus | partly: ratio + focus + stagger |
| Gem rush, big-map races | farm the zone in parallel; price gems by map (G6) | open: economy too slow |
| Scaling builds | Jungle-first throughput, then fight stats; out-gold them (O8) | planned |
| Spawn camping | fight on our spawn (heal each turn), ignore enemies resting on theirs | partly |
| Altar locks | bank where no enemy is in reach; break a lock with the pack | open |
| Kiting ranged | not reachable alone; melee close only with numbers (C7) | open |
| Chases | escape rule C8; never chase into their spawn (`chase_spawn_guard`) | partly |
| Bush ambushes | sighting contracts the zone; wards on bushes (knob) | partly |
| Jackpot control / batteries | pack sieges of golems/monoliths; stun/kill channels (M27) | planned |

## Tools (scratch, `.cache/contenders/warband/tools/`)
`track.py REPLAY --team 0|1` (positions and cohesion), `dbg.sh MAP SIDE EVERY OPP [--set ...]`
(leader/member traces with `debug_every`), `budget.sh MAPS OPP` + `budget.py` (time budget at r400
with `debug_budget`), `patch_*.py` (the source patches applied this session, for reference).
Runs in `.cache/contenders/warband/runs/` (m1a-m1j health/economy series; dbg-* traces).
Current commands (use Python printed by `arena env`, never the Store stub):
- `arena.cmd test contenders/warband`
- `arena.cmd sweep contenders/warband --var "guard: action_guard=true" --var "pack: action_guard=true camp_retarget=true" --vs anchors/main-v40,anchors/starter-cpp --maps pool --sides both --keep-replays all --out .cache/contenders/warband/sweeps/resume-action-packing-r2`
  This finished run is reusable; use a fresh output after changing source.
- `arena.cmd compare .cache/contenders/warband/sweeps/resume-action-packing-r2 .cache/contenders/warband/sweeps/resume-action-packing-r2 --variant-a guard --variant-b pack`
- `python -B bots/contenders/warband/tools/packing_probe.py FRESH_OUTPUT` builds its own scratch
  map and invokes arena's `GameSpec` with the full map path. The earlier CLI sweep custom-map
  attempt failed prelaunch because the registry lookup lost the path; do not repeat it.

## Rejected or superseded
- The tight ball (W3). Waiting beside respawning groups (`max_respawn_wait` 3, as main's D60).
- Ranged members holding fire indefinitely until a tank engages (W5: deadlocks in corridors).
