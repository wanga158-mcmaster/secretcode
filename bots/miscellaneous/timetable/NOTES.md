# timetable: notes

Retired intact on 23 October 2026 during the user-requested workflow reset. This is the old
implementation and historical evidence. The active restart is `bots/contenders/timetable`.

Direction (contenders charter): the game is a race to 1,000 tributed gems; win it by scheduling the
economy near the map's capacity. Central plan computed in the draft (10x fuel, whole map), carried
in draft memory, executed and repaired by each champion. Fights only to protect the schedule.
Accepted weakness: strong aggression (must still survive it by not being caught).

## State (22 Oct, resumed on CLI/SDK 3.0.5, engine 1.0.3)
- Live bar is now `anchors/main-v40@6f62c56b73f7`; no online actions are authorized.
- Current pre-change baseline `3cfc5d884af9` includes D17. Full 40-pool-map/both-side runs:
  `current-v40` 0-80, 158 vs 1,025 mean gems, 10 recall errors while stunned, no faults,
  exhaustion or stalls; `current-starter` 73-7, clean, peak 99,426 fuel, speedrun 1,635.6
  rounds with 33/80 unfinished. These current-bundle results supersede historical health claims.
- Initially default-off corrections D18-D20 build and the focused availability/action model test passes.
  First same-run sweep `evidence-v40` has 800 games, including base/same/legal/observed/bounded
  arms, all pool maps/both sides against starter and v40. All enabled arms have clean health;
  no detectable strength or speedrun gain. The reviewed scorer-tail revision is evaluated
  separately in `evidence-reviewed`; do not merge source versions under one verdict.
- Current default is `contenders/timetable@80d9c5ef961b`: D18 legal-action repair enabled,
  D19 availability and D20 optional fuel scheduling still off. `gate-legality` passes build,
  the focused test and 240 full games (40 pool maps, both sides, starter/fundamentals/v40):
  0 faults/exhaustions/errors/stalls, peak 99,432. Outcomes are 73-7 / 16-64 / 0-80.
  Reference-score qualification was not requested or passed; this is a health repair only.
- Reviewed-source sweep (`3c230547d0f4`, 720 total games) confirms no detectable strength gain:
  base 89/240 with 17 error lines; legal `fe0873faac96` 89/240 with no win flips and clean
  health; bounded `d347c1f35781` 94/240 (+9/-4, p=.267), healthy, peak 89,643. All arms lose
  every v40 key. The independent fundamentals opponent does not change the conclusion.
- M2 scheduling and M3 model-level diagnosis remain open. The first structural finding is that
  correcting availability swaps waiting for travel rather than increasing production (D19).
- Canonical shared knowledge is now [main/foundation](../../main/foundation/README.md),
  especially [M52](../../main/foundation/mechanics.md) for capture intent and actual availability.
- Accumulated source/binary bindings, coverage, health and the ledger null are safely retained
  in [the durable index](../../../results/contenders/timetable/index.json), with selected native
  sweep/gate reports beside it. No owned jobs are running.

## Architecture (src/)
- `config.h` all knobs (inline constexpr; `noise_burn` is the perturbation seed; `trace_slot`
  -1 off, n = one body, -2 = all bodies: per-turn decision trace, diagnosis only).
- `availability.h`: pure observed lifecycle/action-permission helpers. Its focused test covers
  timer-zero, stale observations, fresh reward/alive and unavailable actions; it does not test
  the SDK reader/dispatch wiring or establish a universal fuel bound.
- `model.h` pure: GroupSpec (Smith-ordered members), `smith_clear` (exact) and `smith_fast`
  (O(1) closed form: rounds = H/d + n/2, damage = A/d + B, constants built in `sort_members`).
- `tactics.h` pure: enemy reach, threat at a tile, camp incoming damage (nearest-champion rule
  via `ally_dist`), camp escape steps, skirmish verdict. Carries safeguards S1-S3 (below).
- `plan.h/.cpp` pure: PlanInput, Plan, encode/decode (draft memory, checksum), Planner
  (units per structure solo/pairs/trios/team, contest-weighted territory, circuit model,
  greedy assignment + local search, fuel probe via function pointer).
- `draft.cpp`: fixed roster (C&I, Vendetta, Marcella, Tavi, Vesper), measures spawn/altar fields
  then group fields for our side and the contested margin, runs the planner, writes the plan.
- `world.*`: static map (groups/members point into the engine's own group list; raw tile index),
  lazily computed distance fields (group, altar, spawn), fuel-gated.
- `kit.*`: ability decoding, camp dps and pvp dps per kit.
- `bot.*`: the champion. Turn order: channel -> shop -> enemies (S1/S2) -> health (S3, heal and
  shop recalls) -> bank -> farm (target choice by rate; on-demand party calls; staging; capture).
  Shared memory: byte b = body b's claim (group+1 in bits 0-5, status in bits 6-7: solo,
  capturing, party call, absent). Logs: `init`, `cap`, `bank`, `recall`, `death`, `ledger` every
  400 rounds (time per activity), level 2 adds targets and status lines.
- `tools/`: `table.py` (per-map table of a run), `ledger.py` (time split from ledger lines),
  `streaks.py` (capture-mode streaks from a trace run), `fuel.py` (heavy turns in a .fuel stream).

## What works
- Draft plan written and decoded on every pool map (fuel-safe: planner stops at 160K left).
- Fuel: first turn guarded (World init ~90-120K on big maps; the turn waits a round when less than
  80K is left); target choice bounded (cheap bound, at most 10 full rates, cached solo clears and
  remaining specs).
- Parties: on-demand calls inside a unit; a call is answered by mates up to the target's best
  party size (E8 labour law in `rate_n`); staging outside camp reach until the bodies present can
  take the trip; sieges up to 4 trips (camps keep damage, M10).
- Safety: escape from camps by actual steps out of reach (S3), arrival reserve (no step into a
  camp's reach without the health to walk out), one move per turn (try_move), ranged step clears
  the attack.

## What does not work yet (by measured cost, ledger r1-800, side A, run `ledger3`)
- Capture mode 28.7% of champion time: mostly walking to groups remembered as "reward waiting"
  that someone else had already captured (streaks of 20-67 rounds, `cap2`). D17 targets it.
- Staging 10.8%: calls wait long for mates far away (the call rate ignores mates' travel).
- Respawn waits 9%, travel 15.8%, fight 12.8%.
- Monolith/huge-group maps bank 0 gems (00contest, 00stampede, 01boss_battle): no covered siege.
- Planner structure choice is crude (circuit model, not a schedule simulation).
- 01corridors B: many deaths (43) and few gems; 00contest: 22-29 deaths (enemy Vesper and
  monoliths) - not diagnosed.

## Decisions (numbered; evidence = run directories under .cache/contenders/timetable/)
- D1 Central plan = units + per-group ownership + bank/gold policy, solved in the draft; runtime
  is a rate dispatcher over the unit's groups. Why: deviations (contested camps, deaths) need
  local repair; a fixed timeline breaks at the first deviation.
- D2 Parties are sized per target (G8/E8), not by doctrine: units call parties, mates join up to
  the best size. Evidence: smoke3-5 (00elkring: solo plan assigned nothing, every group has an
  elk); corr3/corr5 (fixed leader starved followers).
- D3 S3 escape uses the real steps out of the monsters' reach, plus 1.5 rounds. Evidence: trace1
  (Vendetta died leaving an elk at 2.5 rounds of margin; adjacent elk needs 4 steps).
- D4 Never stand on a respawning group's tiles (M16 freeze): leave_camp_tiles. Evidence: smoke4
  (Marcella oscillated on a cleared tile; capture never completed).
- D5 Fuel: memset is a byte loop (~3 fuel/byte): no zero-initialised large structs, user-provided
  constructors, raw tile index, in-place plan decode; held bitboards with *_into. Evidence:
  heavy5-heavy8 (World init 185K -> ~100K; first turns no longer exhaust).
- D6 Planner budget by real fuel (function pointer), not work units (counted units missed 90%
  of the cost). Evidence: heavy1 (draft 965K-1M on 01islands/yellow-brick, lost a pick).
- D7 Contested groups are plannable at a contest weight (G1). Evidence: corr1 (side B had an
  empty territory and idled).
- D8 Sieges (<= 4 trips) in runtime rate and planner feasibility. Evidence: econ1 (00ambush
  0 -> 789-890 gems, 01gold-rush 11 -> 550 gems).
- D9 Shop recall at 150 gold (E19), walk home within 6 tiles; Tavi buys in the field.
  Evidence: corr2 vs econ1 (bodies hoarded 8,700 gold on 00ambush).
- D10 Gold weight 0.05 (E17, schedules use 0-0.05). Evidence: ledger1 (bodies re-captured their
  own gold groups every ~57 rounds); speedrun unchanged within noise (ledger2 1,652).
- D11 Respawn waits capped at 20 rounds. Evidence: ledger1 wait 22% -> ledger2 9%.
- D12 Claims resolve by slot; a target is kept unless an option is 1.5x better (3x within 5
  tiles). Evidence: cap1 trace (thrash 3->14->21->14->21); speedrun unchanged (ledger3 1,651).
- D13 First-turn guard (80K) and fields deferred past the first turn. Evidence: heavy8, pool3.
- D14 One capturer: a mate's capturing claim releases the others (E3).
- D15 Present-pool assembly: a call engages when the bodies present can take the trip (E8).
  Evidence: corr4/corr5.
- D16 Log lines carry the body slot; formatted lines cost 3-8K (S22): spare fuel 30K.
- D17 (historically built but unmeasured; now included in the current baseline, not isolated)
  A waiting reward not seen for 12 rounds is priced as a respawn; a mate's capturing claim
  updates everyone's group knowledge.
- D18 A body with no move, attack or cast permission cannot start a channel: skip decisions
  after existing channel handling. Current baseline emits 10 `Champion cannot act` recall
  errors over 80 v40 keys. In `evidence-v40`, `idle_action_guard=1` removes all 10 with zero
  win flips over 160 keys (starter + v40); speedrun +1.8 +/- 1.5 se, no strength gain claimed.
  Reviewed full-pool confirmation removes all 17 errors across 240 keys with zero win flips.
  Promoted solely as a legal-action repair after current-default gate `80d9c5ef961b` passes
  all 240 keys. Its raw/injected modules exactly match evaluated `fe0873faac96`:
  `8a62f47a817f42de6dd2db9fa0eebae1d896ba213386e008a459ff62202b381b` /
  `e1d1b3f5e1db8c1fabb5fb8d10c16cad3f97efb8dda7f5c8ff983218d98833e3`.
- D19 (experimental, default off) Separate reservations from availability (M52). A capturing
  claim no longer invents payout or respawn; query actual lifecycle when a member tile is in
  view and no living member is seen. A fresh `respawning{timer:0}` stays unavailable; waits are
  bounded. `availability_evidence=1` with D18: 74/160 vs 73 baseline, flips +3/-2, p=1.000,
  speedrun -4.3 +/- 14.3 se. Recorded r<=400 starter ledgers (80 keys) explain the null: wait
  21.0% -> 11.4%, travel 13.7% -> 20.4%, staging 11.3% -> 12.5%; logged captures 1,904 -> 1,863.
  The ledger comparison uses legal control `f9c4d7aec8c1` and observed `8cc0d0666162`, holding
  D18 fixed; the legal starter ledger has the same displayed values as base. These are
  available guest ledger observations, not an exact uncensored activity trace.
  Correct state alone does not create a useful schedule: dispatch still neglects party arrival
  and future available work. Next work must solve that allocation rather than tune the wait.
- D20 (experimental, default off) Fuel checkpoints after optional field warming and during
  candidate scoring defer work while retaining an available target. The historical pool3
  exhaustion is pinned to r2 slot4 on 01shortcut A (engine 1.0.2), after deferred initialization;
  suspended callsite unknown, not reproduced current-bundle. Area-based field cost is an
  estimate, not a worst-case proof. First bounded arm `75e23d74dd20` is healthy on 160 keys,
  peak 89,879 vs 99,426 baseline, wins 75 vs 73 (+3/-1, p=.625), speedrun -3.9 +/- 14.2 se.
  Review then added a missing fuel guard around retained-target rescoring. That revised
  candidate is `d347c1f35781` in `evidence-reviewed`, never silently substituted into the first run.
  That reviewed arm completes 240 keys with clean health and peak 89,643; this measures the
  checkpoint policy on those keys, not an absolute bound over all possible game states.
- D21 Disabled preservation: baseline `3cfc5d884af9`, initial OFF `8572c3610f3b` and reviewed
  OFF `3c230547d0f4` have identical raw SHA256
  `ee41399a837e1357d6825228e33fa446625c77c3c81dea3c38226cf2649f5e07` and injected SHA256
  `08a25136f52484853ca873bbee440d005558fbc9870ffb9d058f6ce975d7a3bd`.
  The first sweep's same arm also exactly matches the baseline games.

## Safeguards (G10 / C20) - status
- S1 no engagement the model loses: skirmish verdict gates strikes (crude volley model; must
  move to the turn-exact law, C1/C16). Not unit-tested yet.
- S2 no contact without a way out: enemy reach model (melee walk+hit, +dash; ranged; spells);
  groups near seen enemies are not targeted for 12 rounds; flee to the safest step. To do (C16):
  hold at reach + 1 (+2 against double turns), ranged reach +1 with a ready reset.
- S3 no awake camp out of the accounting: every awake camp (visible or remembered 8 rounds)
  counts at the tile, with the nearest-champion rule; arrival reserve.
- S4 clean health under fuel: guarded first turn, bounded choice, real-fuel planner budget.
- Unit tests for S1-S3 and the plan codec are owed (tests/*.cpp, pure modules only).

## Answers to opponent behaviours (first draft; to be derived and tested, NOT yet verified)
- Early aggression: stay out of two-turn reach (C16); our side's groups first; spawn is a
  sanctuary (M18) to hold and fight from.
- Grouped play: a pack farms one group at a time; avoid its vicinity (danger ban), farm elsewhere.
- Dispersed play: no even fights (C11); finish weakened intruders only.
- Gem rushes: the plan prices gold low on gem-rich maps; bank more often under contact.
- Scaling: items by marginal throughput (Jungle first, E18/E20); the race clock (E24).
- Spawn camping: fight from the spawn tiles (full heal each turn), exit away from campers.
- Lock harassment: bank at another altar or break the lock (5 hits); locks protect nothing (M20).
- Chases: equal speed never opens distance (C8): decide before contact; dash breaks contact.
- Kiting: never chase a kiter; leave its range.
- Bush ambushes: remembered enemies count; wards on bushes near our routes (not implemented).

## Foundation-worthy findings (for the coordinator)
- memset / zero-initialisation is a byte loop in this toolchain: ~3 fuel per byte (a 400-byte
  struct ~1.2K; vector::resize value-initialises). User-provided empty constructors avoid it.
- Copying a camp_group handle (it holds a vector of camps) and moving large structs is ~600 fuel
  per group; keep `map::camp_groups()` alive and point into it.
- A champion's first turn on a big map costs ~100-120K for static setup alone (camp groups 16-32K
  plus guest-side indexing 50-80K); plan for it.

## Work in flight and how to resume
Nothing is running. Read the durable index and this file, then inspect retained games rather
than rerun the settled null. `evidence-v40` and `evidence-reviewed` bind distinct immutable
pre-promotion sources; do not resume them with today's default source. A new scheduling
experiment needs a fresh output, a same-run baseline and both-side/pool coverage. Current
verification command is `./arena.cmd test contenders -- timetable`; the completed current-source
health gate is `.cache/contenders/timetable/gate-legality` and need not be repeated unchanged.

## Next steps (in order)
1. Economy (M2): inspect the candidate filter before tuning rates. `choose_target` labels its
   static full-HP/centre-distance score an optimistic bound, but a known cleared reward costs
   only travel plus capture. Example: value 100, travel 10, capture 6, full HP 10,000 and DPS 100
   gives a filter score 0.86 but actual capture rate 6.25. A sorted early break cannot safely
   prune with that bound. This is a source/model counterexample, not a witnessed native choice;
   next prove it through a real dispatcher fixture and measure the whole family.
   Then call rates must include mates' travel (claims give their positions roughly);
   capture mode only on the tile; Jungle-first build check (E18); bag size by altar distance and
   contact (E22, C21); then a schedule-simulation planner in the draft (port the dispatcher idea
   of `foundation/tools/economy/speedrun.py`: squads, items at spawn, banking) to choose the unit
   structure per map, and plan adherence measurement (own captures share from `cap` lines).
2. Covered sieges for monoliths/golems (E9): waves, cover, tank priority (M6).
3. S2 per C16 (hold distances), unit tests for S1-S3 and the codec.
4. M3: diagnose the retained `current-v40` losses by wrong model and missing structural answer,
   then confirm explanations against fresh opponents; do not rerun unchanged v40 losses.

## Rejected ideas
- Fixed unit leader for party targets (followers starve when the leader goes solo): replaced by
  calls (D2).
- Strict half-map territory (empty on symmetric middle-row maps): replaced by contest weights (D7).
- Planner work-unit budget: does not track fuel (D6).


## D20 - Actual admissible dispatcher, equipment, and failed itinerary (3.0.5 / 1.0.3)
The safe dispatcher now calls the tested production ordered_dispatch loop, with E29's bound
and no fixed top-ten truncation. Default OFF modules were identical to 80d9c5ef961b through
e8a3ecb00334. Full pool/both sides/starter+v40: 73 -> 74 wins /160, +3/-2, no gain; all 0/80
v40. A personally queried reward at r64 on 00gem-fields A was genuinely omitted. Exact exposed
prefix: 11,037 frames equal before activation. It changed movement, but the enemy captured at
r67 before our r71 arrival. This proves ranking/control effect, not a productive opportunity.
The earlier r97 witness was actually respawning despite seen==round and remains a failed
availability witness (M52). Exact source keys and native/prefix links are in the durable index.

Equipment-pool@353a023a060f: 480 games, 160 keys/arm; base73, fresh74, equipped78 wins,
all0/80v40. Equipped margin +36.0 +/-13.4 SE and starter speedrun -45.8 +/-18.1 SE rounds;
one occupied-destination error (01broken-curve A, r406 actor2) blocks qualification. Native
trace proves an ordinary move20,18->19,19 newly revealing ally3 at17,21; rejected cast target
is absent from native history. A stale origin/occupancy explanation is plausible, not proven.
Current default-OFF dash_refresh reads actual origin/readiness/range and fresh occupants after
movement, permits hole intermediates, forbids hole landings. A later four-key origin smoke is
clean, but does not qualify this correction across the pool.

Itinerary-v1-pool@e3f9a1b40fff: 480 games, 160 keys/arm; base73, equipped78, itinerary69,
all0/80v40. Equipped control clean; itinerary has one zero-clear corridor stall. Tour commitments
were truly executed, but ready was not consulted: this arm is not a ready-time schedule. Review
also found reward-hold bypassing deadlines, untyped income inventing capture release, and
helpers near a ranged leader counted while out of camp range. Keep itinerary OFF. Do not
retune this failed arm before fixing the structural event/capacity model.

Review corrected two equipment formulas in current unmeasured OFF source: Vesper+Treasure
bonuses add (M37), and Monster multiplies all camp damage including abilities (M40). Historical
evaluated sources retain their identities and incorrect formulas; their measurements are not
silently attributed to the corrected source. tests/equipment.cpp now includes the Vesper case.

## D21 - Capacity/event-job reconstruction, private and unqualified
Parent authorized bots/contenders/_timetable-capacity using frozen anchors/main-v40 physical
execution. This is private research, no adoption or retirement yet. Preserve this complete bot
and evidence. The doctrine must allocate actual body capacity/arrival cost, bootstrap portfolio,
bounded job commitment/deadline/release, and one capturer. A copied runtime or mirror record is
insufficient; prove different productive choices and clean paired full-pool strength, then fresh
opponents/maps and current working main. New policy stays default-OFF with exact module
preservation. No jobs are running at this checkpoint. The durable index retains exact source,
knob, engine and completed coverage identities; new experiments use new output folders.

Private reconstruction now lives in _timetable-capacity/NOTES.md and durable index. Diagnostic v1-v3 harvest is unverified, not zero: truncated arena rows lack reliable final harvest summaries. Saved v5 native events prove a scheduled solo job through attack/payout/lifecycle release. No private adoption or public default change.

## D22 - Current v52 health repair investigation (24 October 2026)

The retired bot is being inspected only under Miscellaneous's health-fix exception. The active
Contenders restart is outside this work. Closed `strength-baseline/floor52` binds historical
`miscellaneous/timetable@f4876a111ee9`, executable SHA256
`e80c32363e22728b0958dcfb9222df000668824c878c98f8f4263a8a9684f3c2`, against
`anchors/main-v52@4aaf587c22cc`, executable
`1e701d49e9f39c4d5fdb758cd8ba2d1805d37bc544094090dff7c790a8d3728a`.
All 40 pool maps/both sides give one healthy strict win and one subject health flag; Main is clean.

The win is `00lanes B`, 1012-836 gems at round1117. Native draft logs show a completed solo
plan, six units, estimate1164, and nonzero owned groups for every body. Tavi/slot4/native actor9
has two owned groups, captures owned g35 at r60 (+11 gems,+40 gold), buys a shard, starts tribute
at r65 and actually submits11 at r67 (team score0->11), then captures owned g41 at r135 and
g35 at r277. This exercises retained central ownership/rates and subsequent productive work;
it is not an ownership-free fallback. It does not isolate ownership's causal benefit from a
matched controller, establish party use on that winning key, or prove pairwise neighbor distinction.
Do not credit the OFF itinerary interpreter or `draft_mode=1`: the latter currently still uses
the fixed roster. These limits preserve the difference between observed controller use and a
stronger mechanism claim.

The flagged key is `01islands A`, 88-1002 gems at r761, exactly one fuel exhaustion and no
fault/error. Fuel binds r86 slot3 (Marcella/native actor3) at100000, followed by a17365-fuel
stale continuation at r87. Native event kind26/input1919 is the r86 turn rollover at(53,24),
HP1200, no cargo/purse and unused move/attack/cast allowances; enemies7/8/10 are personally
visible. Thus this is not an initialization or stunned-action failure.

Neutral existing fastrun `--calls-log` reproduces the replay plus BOTH stdout and BOTH fuel
byte for byte. At r86 there are13 distance reads, no field compute, and no movement/claim write.
The stale r87 continuation has nine distance reads, one field compute, movement and claim write,
with no fresh observation calls. The default-OFF bounded `health_journal` diagnostic is now
answered and removed from working sources. Its OFF snapshot `297789229b27` has RAW executable
bytes exactly equal to historical `e80c3236...`; its separately captured SDK-injected module is
`42418910119cd9e2fbba2d8748379be77d8dd1f6bf6b6afd5a586c87fe05e633`.
The historical SDK-injected file was trimmed, so equality for that historical role is unknown.

Journal ON `436b2cfe2248` has RAW SHA256
`16da99ea53d7a36f1d43ec15834836945c277ba072af7acf75afd9119c6436c3`.
Official full spectator state/timeline bytes match the original through r86 actor3's included
owner-start (input1918/substep7), 19,530 frames; private fuel and memory are outside that check.
The journal records observation complete at86889 fuel, PvP exit at58048, strict search entry57025
and exit47029 with no positive result, then relaxed entry46965 and discovery completion10421.
The remaining unguarded option loop crosses the turn boundary before the later group field.
Budget samples perturb fuel and can shift the exact option where suspension occurs; the named
combined strict/relaxed work obstruction is supported by both neutral counters and this journal.
The journal prints at most eight lines per fresh turn and yields each diagnostic flush turn;
later diagnostic outcomes cannot qualify gameplay. `timetable-health/journal` is closed.

The health-only candidate `health_score_budget` is default OFF. It reuses existing35K entry,
30K discovery and25K option/tail scheduling floors, with unchanged top-ten ordering, rates,
party math and hysteresis. Incomplete discovery/comparison is explicit, not an inferred zero
rate or evidence that work is unreachable. A permitted incumbent whose comparison was deferred
keeps its party/hysteresis and receives a fresh retry next round rather than waiting five.
The production shortlist/option helpers have a focused fuel-consuming callback fixture at
`tests/health_dispatch.cpp`, registered in the suite runner; it passes under the pinned WASI
compiler/runtime in an owned private Job/slot. No draft, plan, finance, fighting threshold or
public default has changed.

Repair OFF `1c594a94244a` exactly preserves RAW `e80c3236...` and the newly captured OFF
SDK-injected role `42418910...`. Repair ON `1b0dc0d22f23` binds RAW
`44822840ab320f4af8c27d25b8200c3bca990193a2ab3e138623a78f001ef32e` and SDK-injected
`0a0fc7d79583e0e2971001aa35b649c3a5db4abdc5ebe2732be0b2a757e9e1e9`. One initial OFF build
was rejected before games for a single function-name metadata byte (`$_0`->`$_1`); placing the
unused health-only lambda entirely inside the discarded ON branch restores exact RAW identity.
This was continuity plumbing, not a different health policy.

Closed `timetable-health/repair-probe` compares fixed OFF/ON on Islands/Lanes, both sides,
against historical Main52 (eight games). All four OFF rows exactly match the census. ON has
clean recorded health on both teams and retains LanesB (1003-821,r1134); IslandsA remains a loss,
176-1004,r778 versus OFF88-1002,r761. These four paired keys do not establish a full strength floor.
Native repaired Marcella at(53,24) moves to(54,23) on r86 and to(55,22) on r87, explicitly ending
both turns; fuel is79602/80075, without rollover. The same actor then captures its OWN g2 at
r270 (+58 gems,+180 gold), starts tribute at277 and actually banks58 at283 (score0->58), with
another owned capture at430 and bank attempt439. This is productive continuation, not only a
health-clean idle suffix. Source inference: a completed turn re-enters `play_turn` and refreshes
`observe`, so r87 does not use the old suspended observation stack.

Production ON's first exposed-world difference is r61 actor5/Vesper (an extra move), so its
full instruction/scheduling costs are included; there is no identical-context-through86 claim
or isolated marginal-income attribution. The diagnostic journal's earlier equal prefix only
binds diagnosis. The production fixture, direct native fresh actions and own payout/bank support
the bounded health mechanism with this limit.

LIVE changed to Main53 during this work. Frozen `anchors/main-v53@fa5b8ae8b5ae` is warm and its
RAW `bd6b051394b36df5849805130976acfab1a0b4361d412dc0f26c886d72419b61` equals the released
Main53 module. Closed `timetable-health/repair-floor` tests unchanged ON on all40 pool maps/both
sides versus Main53: **0 wins,0 draws,80 losses**, all80 keys validated and **both teams clean**
in the recorded health counters. The v52 win is historical, not a current floor. This reverses
the initial allocation premise: the isolated health obstruction is repaired with productive
continuation, but current strength is still absent. Stop this allocation without tuning any key.

`health_score_budget` remains **OFF**; there is no default promotion. Current source is
`miscellaneous/timetable@1c594a94244a`, whose RAW remains exactly historical `e80c3236...`.
Only candidate ON `1b0dc0d22f23` was a Main53 subject; do not silently assign its0/80 or clean
health to the preserved OFF/default. No full80 ON-versus-Main52 qualification was run: its
four-key controlled probe and the later Main53 panel have separate scopes. The useful bounded
health prototype and focused fixture remain available, but the bot is not current-floor qualified.

Compact selected recipes, native events, logs and hashes live in
`.cache/timetable-health/diagnosis.json` and `calls-diagnosis.json`. Only the two needed cached
replays were regenerated; their historical replay leaves were removed under the replay lock,
and the closed census spec/rows/summary hashes remain unchanged. All full counter streams and
duplicate diagnostic bundles were removed after extraction. The answered journal's compact
stage/prefix/module bindings are in `journal-diagnosis.json`. Both fixed diagnostic snapshots
were immediately trimmed, with post-pair arena size9,698,707,899 bytes. Native build ownership
has returned to the other owners. The integrator published the compact closed Main53 descriptors,
final non-promotion decision and six byte-verified evidence files in
[the durable index](../../../results/population/timetable-current-strength/index.json).
All three owned experiments are closed; no native jobs remain.
