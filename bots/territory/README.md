# territory: geographic organization of complete production cycles

Sixteen bots (`territory/<bot>`) answering one question: **with the economy held fixed, which
geographic organization of complete cycles wins?** A cycle is source -> clear -> capture -> loaded
exit -> altar -> recovery/shop -> next work. Each bot changes who works where, how a loaded bag
leaves, which altar converts it, where actors recover and when a geographic commitment ends.

## Doctrine

Hypothesis: a winning territorial organization creates, preserves or removes complete bankable
opportunities. Tiles, influence, path length, kills and captured camps are worth nothing by
themselves: a capture pays once, groups renew together, and holding a spawn tile delays our own
renewal. Value comes only from our own completed banks along routes the actor actually keeps; a rich
source without an executable loaded exit is no opportunity. Only `encirclement` may credit denial of
an enemy conversion.

Held fixed (`common/src`, shared by all 16 bots):
- `runtime.h`: the SDK-facing executor: warm-up fields, observation frame, survival and recall,
  shopping, banking, movement, combat, fuel guards and the turn loop (exceptions caught per turn).
- `competitive.h`: the eight-source shortlist (four nearest reachable groups, then gem-bearing
  continuations) and reward-per-complete-cycle arbitration. A proposal survives if it carries a
  supported executable intent (dash circuit, alternate exit, own ward, withdrawal under pressure, a
  connector opened by a payout) or, with no journey committed, scores within 20% of the best source;
  otherwise the best-rate source wins. Eight rounds without approach retire a journey for 32 rounds.
- `native_nav.h` (bounded routes, disjoint exits, dash circuits), `native_types.h` (types).
- Information rules: each champion acts on its own observations; draft memory holds only the team
  size; nobody commands an ally; roles are draft ordinals and a helper counts only when visible.

Varied per bot: its controller `choose_NN` (`doctrines_a.h` 01-08, `doctrines_b.h` 09-16), which
proposes each turn's task; its `Design` cases in `competitive.h` (design score, bank target 12-32
gems, role affinity, contest posture); and its five-pick draft in `<bot>/src/native_profile.h`
(ordinals 0-2 are the core roles, 3-4 independent extensions). Every bot carries its comparison arm:
knob `P7_CONTROL=1` (the `#define` in `main.cpp`) swaps in the card's control policy with the same
roster and executor; the control arm also skips the `competitive.h` arbitration and the
bank-target early conversion, so active-vs-control gaps include those layers. Shared edits move
the control too: it is a paired comparison, not a baseline.

Neighbouring suites: `doctrines` (whose rewards to concentrate, harvest versus renew as a doctrine),
`decision`, `models` and `events` (decision rule, forecast, event-time value) vary the economy.
Here only the per-design bank target (12-32 gems) varies with the geography; the rest is fixed.
`distributed` (cooperation protocols) and `adaptive` (who may commit, from private evidence) vary
coordination; here roles come from the draft and the map. `commitments` prices rival responses to
our exposed state; here only encirclement's certified denial involves the rival. `frontier` builds
formal OR representations (unrelated to the `territory/frontier` bot). `specialists` add one
mechanism to the B0 chassis; here the whole team's work map changes. `main` is the tuned generic
farmer: a change that makes a controller fall through to the best-rate source erases its bot.

## Bots

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| concentric | Ordinal 1 never leaves the three nearest home groups; 0 and 2 move out one ring at a time, only after their own capture and bank with kit, 2 only beside 0; only 0 makes verification trips | Weakest measured; cheap opening clears lost to enemy occupancy |
| interior-lines | Two wings and a reserve chosen jointly so the reserve (0) is within 10 steps of both; the reserve guards a personally witnessed pressure tile for at most 8 rounds per epoch | Best measured; productive sources stand in for rally pads; its right-wing bootstrap scored below the control on 00reverse |
| frontier | After a capture and bank the crew opens the best seen source outside the rear basin; after a completed cycle ordinal 1 stays as resident, the others return; forward pressure retires it | No bot-specific finding |
| islander | With its own dash or funded teleport, takes a mobility-only circuit (separate ingress and loaded exit) when it outranks connected work; banks, then redeploys | Native branch unreachable: shortlist rejects every home-unreachable source before mobility geometry runs. Pool disconnected sources exist; see population evidence below |
| forks | Collectors work sources with a second exit to another altar whose interior tiles are disjoint; fresh pressure switches the loaded exit; ordinal 2 supports within 10 steps | Deaths doubled on 00eight B; its opening serves ordinal 3, absent in three-actor games |
| gatehouse | Producers work an enclave behind a gate or proven cut only while the guard (0) was seen near it within 8 rounds; the guard holds the incident tile; enclave pressure evacuates home | Needs enclave geometry; bypasses not modelled |
| causeway | Clears a camp whose spawn set is a proven cut opening downstream work; ordinal 2 holds for the payout, then vacates; the crew works downstream cycles that finish before respawn | No qualifying camp-spawn-set cut among current retained groups on 40 pool maps, both sides; broader cut representations remain unassessed |
| sanctuary | Works the basin with the shortest spawn restoration rather than the one around the best source; 0 repeats heavy work there; Willow (1) heals visible injured allies | Independent extension routes tried and reverted |
| ports | Producers start in two altar catchments; on fresh pressure an actor banks and moves its future production to the other catchment once per epoch; diversified shortlist | Inactive with one altar |
| migration | The core crew starts in the cheapest basin; after a paid capture it banks, inspects a better seen basin in the other catchment and moves the whole routine; a failed inspection ends it | Extensions use the generic fallback |
| elastic-border | Ordinals 0-1 work the outermost ring, 2 the inner one; fresh pressure contracts inward when the inner cycle is better; one re-entry after re-verification | No bot-specific finding |
| counterfront | Two fronts in different catchments; on fresh pressure the reserve (2) pivots once per epoch to a better source on the opposite front and may join a visible producer there | No bot-specific finding |
| encirclement | Ordinals 0-1 pressure a visible carrier's tribute approach only under a same-round denial certificate (at most 2 altars, witnessed bag, each enemy response beats own production) | Certificate narrow, rarely fires |
| harvest-and-exit | Raiders (0-1) take up to three captures in the forward basin, bank, then withdraw to the inner source instead of reworking renewal; pressure ends the raid | The home producer (2) uses the generic fallback |
| reclamation | Moves an actor's future work to a seen source in another region with residual camp damage and a known exit when that improves its cycle; inspects within 4 rounds | No bot-specific finding |
| veil | Prefers a seen bush-occluded source with a known exit over the best open one; banks along its own retained exit with its own ward; a missing reveal reverts to the open route | Needs occluded sources; camps see through bushes |

Measured (local, bundle 3.0.4 / engine 1.0.2, `00eight` both sides vs `anchors/fundamentals`, two
games per bot, own total vs ~2000-2056): interior-lines 1690 (1 win), causeway 1494, islander 937,
encirclement 888, veil 815, elastic-border 755, frontier 670, counterfront 428, gatehouse 414,
sanctuary 341, harvest-and-exit 314, migration 302, reclamation 294, forks 273, ports 244,
concentric 215; no errors or fuel exhaustion, peak 92k of 100k fuel in a turn. The later concentric
and forks openings are not in these historical numbers. Default-off startup and live-runtime evidence
is below; those experiments do not establish a bot's distinct geographic contribution.

## Distinctness rules

- A bot is its geographic decision rule (its table row). Two bots that make the same choices count
  as one, whatever their names, drafts or thresholds. A new bot needs a new rule about where work,
  exits, conversion or recovery happen, or when a commitment ends, plus its `P7_CONTROL` arm.
- A threshold, gear or draft change that leaves the geography unchanged is a variant of an existing
  bot: test it with arena knob overrides, not a new directory.
- A mechanism counts only if it survives arbitration in games: compare the `p7 decision` lines'
  proposal and task. A bot whose proposal is always replaced duplicates the generic farmer; fix its
  geography or move it out. No bot may value occupied tiles, kills or captured camps as territory.
- `common/src` has one owner at a time. Any edit there, including a design-keyed branch, changes
  all 16 bots and their control arms: rerun `arena test territory` and `arena build territory`, and
  re-measure across the suite before claiming a gain. The per-bot controllers (`choose_NN`) and
  `competitive.h` cases also live in `common/src`, so per-bot work there needs the module owner
  and re-versions all 16 bots; only `native_profile.h` is truly per bot; behaviour other bots must not get is forked into
  `<bot>/src/` instead of a new global switch.

## Layout and checks

```
common/src/runtime.h       executor, warm-up, banking/shopping, recovery, fuel guards, turn loop
common/src/competitive.h   shortlist, design scores, bank targets, reward-per-cycle arbitration
common/src/doctrines_a.h   choose_01..08 (concentric .. sanctuary)
common/src/doctrines_b.h   choose_09..16 (ports .. veil)
common/src/native_nav.h    bounded routes, independent exits, mobility circuits, fuel cutoffs
common/src/native_types.h  Design, Goal, Source, Frame, State, Task
<bot>/src/main.cpp         p7::live::run(design, P7_CONTROL != 0, draft_order)
<bot>/src/native_profile.h design, name, five-pick draft, role summary
tests/                     run.py (the single entry); plan7-*.cpp WASI fixtures
```

- Tests: `./arena.cmd test territory` or `./arena.cmd test territory -- [bot ...]` (~5 s, no
  games): self-containment; per-bot wiring (entrypoint, profile, five distinct legal picks, Willow
  second for sanctuary, Marcella third for causeway, mobile islander core, `choose_NN` dispatch,
  suite-local closure); the production fixtures. Named bots narrow the wiring and doctrine fixtures.
  `plan7-population.cpp` adds Islander's real controller/arbitration, refusal, loaded-bank and
  redeployment checks, plus the native shortlist obstruction. Its injected positive certificate
  is not a reachable native frame under the current initialization. `startup_driver.py` extracts
  the saved production initializer and turn-entry prefix, runs them with the real navigation and
  shortlist and an immutable SDK double, and checks output equality, suspended loop continuation
  and the enabled completion handoff. `live_driver.py` extracts the actual warm/current-field caller,
  trace dispatch and cache helper: 102 caller, 71 trace and 57 cache checks cover checkpoint handoff,
  exact-root reuse, fresh dynamic observations, partial-write exceptions and suspended-call refusal.
  Its distances, frame/geometry and costs are synthetic, not native cost or path proofs.
  `phase_driver.py` extracts the saved journal, macros and outer dispatch to check value reuse,
  ordered/frozen prefixes, overflow, bounded output and one attempted flush; its synthetic clock
  changes do not identify a native suspended instruction. The full suite currently passes 29/29;
  the partial-admission pair also passes its stronger fallback/adoption assertions on repeat.
- Build: `./arena.cmd build territory` (release snapshots of all 16). No suite tools. Logs:
  `p7 decision r=...` at rounds 128/256/512 (fuel by phase, task versus controller proposal) and
  `p7 error: ...` for exceptions caught at the turn boundary.
- Fuel guards in `runtime.h`: 65k before each warm-up field, 50k before ordinary planning, 30k
  before farming, 25k before movement routing, 20k before banking and recovery, 12k between combat
  actions. A cutoff is not proof of infeasibility, and a build does not prove fuel safety.

## Leads

Open problems:
- No bot beats `anchors/fundamentals` (1 win in 32 on 00eight). Throughput loses before geography
  matters: warm-up builds one immutable field per turn, so on 00collision actors first moved in
  round 12 versus round 1 for fundamentals; later route and recall losses remain.
- Convergence risk: arbitration can replace a proposal with the best-rate source; measure proposal
  survival per bot from the decision logs before tuning geography.
- islander, causeway, gatehouse and veil need geometry the pool may lack: survey the maps offline
  first and mark a bot blocked rather than inventing the geometry.
- Cycle estimates credit at most one currently legal visible ally hit and no future healing.

Population investigation (2026-10-22, no gameplay changes):
- **Observed:** the hash-validated [static survey](../../results/population/territory-audit/geometry-survey.json)
  covers 40 pool maps and 80 map/sides. Disconnected camp groups occur on 12 map/sides; one-hole
  component opportunities occur on 10. Yet `competitive_shortlist` admits zero home-disconnected
  groups. `runtime.h::geometry` tries Islander mobility only for `!home_connected`, which cannot
  occur among those admitted groups. This is a source-admission obstruction, not absent pool
  geometry. One-hole adjacency alone does not prove a safe ingress/capture/loaded-exit circuit.
- **Observed:** removing a retained camp's spawn set disconnects no other retained group from
  home on any of those 80 map/sides. This is Causeway's current static predicate, not a result
  about every possible cut or map. Canonical route candidates nominate Veil on `00swirl` and
  Gatehouse on `00great-wall`; SDK path ties, own wards and live guard/safety premises are unproved.
- **Native limit:** `p7 decision` samples only rounds 128/256/512; cached commitments omit
  proposal/flags, and the logged tuple omits fields used by full Task equality. An unchanged
  tuple is not proof that the projected controller state was committed. The retained Frontier
  `00swirl` B case has a real inspection move at R512 but no completed source-bound cycle before
  the actor's later death. It is a negative continuation case, not admission.
- **Decision:** assign one shared-module owner to the reachable mobility menu, identical in the
  active/control arms, before tuning Islander. Keep Causeway experimental until a suitable
  scenario or a different supported cut representation exists. What changes this decision:
  a production initialization that retains a valid mobility-only source, followed by its actual
  ingress, capture, loaded exit and redeployment; or a hash-validated qualifying Causeway cut.
  Evidence and exact identities are in [the audit](../../results/population/territory-audit/native-audit.json).
- Reproduce with `arena test territory` (22/22), the arena Python running
  `bots/territory/tests/population.py --self-test`, and
  `bots/territory/tests/geometry_survey.py --output .cache/arena/orchestration/territory-audit/geometry-check.json`.
  `tests/population.py` exposes 16 intent contracts and conservative native diagnostics; no bot
  is declared ready by these checks.

Current Islander admission triage (2026-10-23, no gameplay change):
- The exact current `islander@6da60a6896f8` still has the production obstruction above: retained
  groups inherit immutable nonnegative home distance, so the disconnected mobility preparation
  branch cannot run. Its declared controller additionally requires its own prior capture and bank,
  personal source visibility, an owned ready kit, a complete bankable circuit and preference over
  connected work. Those requirements and the shared arbitration remain intact.
- The retained current Ambush games earn no capture or bank with either the default or the
  startup-only recipe. Great Wall defaults do bank (160 gems A, 80 B), but that map has no
  home-disconnected groups. These are different contexts; receipts do not transfer between them.
- A bounded offline review of named Ambush, Islands and Fracture candidates does not supply an
  eligible native opportunity. Reconstructed unrestricted routes use multiple hole crossings;
  a separate one-crossing Ambush ingress/exit still prices a 171--174 turn complete cycle against
  windows capped at 120. Anchor/order assumptions and hypothetical combat prices are explicit;
  this is not a native visibility, safety or route execution proof, nor a full-pool impossibility.
- **Decision:** stop without an admission patch or new build/game. All 16 default raw/injected
  bodies and 20 retained conditional recipes remain unchanged (72 role comparisons). Reopen on
  one actual personally observed, own-matured source with supported ingress and loaded exit,
  positive complete-cycle score and surviving arbitration. Exact current inputs, the source proof
  and the independent review are retained in the [bounded result](../../results/population/islander-mobility-admission/index.json).

Bounded startup investigation (2026-10-22, default off):
- `P7_BOUNDED_INITIALIZATION=1` adds immutable setup checkpoints at the existing 65k reserve,
  including terrain/hole batches and retained groups, and returns from the first `turn()` after
  initialization completes. A native yield resumes the same stack and loop cursor. This completion
  fence ends the dynamic `turn()` pipeline; `run()` still calls `remember()` with fresh receipt
  queries. Ranking, shortlist admission, controllers and fuel reserves are unchanged.
  `P7_STARTUP_TRACE=1` separately enables setup phase markers; their fuel and rounds are diagnostic.
- **Observed:** against pinned `specialists/fireflies@f6c57ee622a9`, 40 current pool maps and both
  sides, the same-run untraced default-off baseline `harvest-and-exit@1d3036281eca` has ten
  exhaustions, all first-turn Ambush setup. Checkpoints alone (`025100caa108`) remove all ten;
  separate cost-bearing native markers locate the legacy crossing between terrain completion
  (phase 3) and holes completion (phase 4). The checkpoint stage retains 12 stalls. The completion
  fence (`1ae4a3457744`) also has zero faults, exhaustions, error lines and ordinary budget hits,
  with nine stalls. Only Harvest-and-Exit's enabled gameplay was exercised, 80 games per stage.
- **Preservation:** all 16 disabled raw and injected WASM binaries remain byte-identical to the
  pre-repair versions. All 80 original qualification/default-off pairs preserve the inner replay,
  both stdout/fuel streams and native summaries exactly. The production-driver fixture preserves
  immutable fingerprints through 16 synthetic yields and tests completion fuel 50k/64,999/65k
  with a round advance. Its synthetic costs and two members per group do not prove a native bound
  for a large group or an atomic map API. The native panel supplies the separate health evidence.
- **Changed decision:** retain the startup repair as finished verified work, still default off.
  The completion stage removes Contest A, Swirl B, Shortcut B and Yellow Brick Road B from the
  checkpoint stage's stall set, but adds Broken Curve A. These outcomes do not prove a specific
  prior stale-action defect. The current Yellow B window moves and receives its own capture
  reward at R511; it does not reproduce the old available-reward stationary hold.
- **Next obstruction:** Ambush A/B and Big World A/B have five stationary actors over observed
  R1-80, zero whole-game income/clears/deaths and connected shortlist sources; locate their first
  live planning/execution loss before proposing a source or combat repair. Contest B, Broken
  Curve A/B and Starvation A/B instead show camp combat and deaths without income; inspect their
  retained approach, first damage, personal visibility and retreat/action windows. A current
  unpaid capture hold would reopen a separate capture-liveness experiment. No capture expiry,
  generic farming substitution, default enable or contribution admission is included here.
  Exact source identities, stage-specific rows, hashes, limitations and reopening conditions are
  in [the retained result](../../results/population/territory-health/final.json). The independently
  sourced opponent is not an admitted regression policy; all nine raw stalls still block health.

Current shared startup qualification (2026-10-23, default off):
- The existing `P7_BOUNDED_INITIALIZATION=1` recipe was qualified on all sixteen current
  controllers without changing runtime code, reserves or geographic policy. All sixteen default
  raw/injected bodies and five retained Harvest recipes preserve exact bytes (42 comparisons);
  the current suite passes 27/27. The enabled recipes remain separate from current defaults.
- 128 fresh full raw games form 64 same-run pairs: all sixteen bots, Ambush and Great Wall,
  both sides, against frozen productive Fundamentals `85ce679dc7b1`, current engine 1.0.3 and
  toolchain `3837f00416ea`. Every CLI-consumed capsule matches its captured raw module. Startup
  removes all 160 default R1 exhaustions; all 64 enabled games have zero faults, error lines,
  exhaustions and ordinary budget hits. The precise guest suspension callsite remains unobserved;
  the older 1.0.2 phase markers are not reused as a current callsite proof.
- **Activation/promotion negative:** all 32 enabled Ambush cells still earn and bank zero.
  Great Wall clears fall 170→135, earned gold 20,461→17,245 and banks 3,885→3,147; individual
  organizations have gains and losses. All four old Great Wall B stalls remain, with new stalls
  at Interior Lines B and Veil A. Veil loses its only paid cell. Across matched actor-cases,
  fifteen lose paid receipts and one loses measured combat/economy activity; movement and action
  readiness are not inferred from those totals. Overall stalled cells rise 36→38.
- Native payout→bank→later monster-damage increases survive in 52 enabled actor-cases across
  thirteen bots; fifteen bots retain some banking. These are actual paid continuation receipts,
  not proof of private geography, loaded-route identity, recovery or raid retirement. No broad
  or fresh qualification follows the new stalls and unpaid Ambush cases. Keep the knob off;
  next separate Veil/Interior Lines' first lost productive boundary from the common post-warm
  planning/action fuel obstruction. No new deferred logger was needed for this qualification.
  Exact roles, every per-actor outcome, regressions and all 277 empty Job closures are retained in
  [the result](../../results/population/territory-startup-current/index.json).

Live producer investigation (2026-10-22, default off):
- **Observed:** bounded native receipts on the startup-enabled `1ae4a3457744` stationary cases
  show completed warm fields, followed by current-field, frame and geometry work leaving less
  than the existing 30k planning guard. On Ambush and Big World, both sides, all 60 sampled late
  packets (R32/64/128, five actors per side) stop at that guard before choosing a task. Current-field
  marker intervals cost 28,468-34,253 fuel; frame intervals cost 38,343-41,206; post-geometry fuel
  is 17,091-27,815. These include diagnostic marker costs, not exact untraced phase costs.
- **Measurement correction:** an initial 20k logging-floor assumption caused trace exhaustions.
  `P7_LIVE_TRACE=1` now retains one bounded packet and emits on a fresh owner turn with at least
  90k fuel, skipping both `turn()` and `remember()` on that diagnostic turn. Clean trace
  `36b5fb5e38f8` has zero technical flags in four games. Its four skipped diagnostic turns per
  actor are explicit; R1-80 physical histories and whole-game native economy/combat timelines
  equal the pinned baseline in all four cells. The unsafe trace remains separate retained evidence.
- **Separate repair:** `P7_REUSE_CURRENT_FIELD=1` caches the static field only at its last
  successfully completed, captured root tile. A miss invalidates before writing; exceptions leave
  it invalid. A round change after compute returns before consuming the old frame observations.
  Movement, dash or displacement to another tile requires recomputation; current observations,
  geometry and the movement field stay fresh. Terrain and the 256-distance cap are immutable
  cache inputs. The actual helper/caller and exception seams passed the fixture and independent
  source review. All 16 disabled raw and injected WASM binaries, and the startup-only enabled
  Harvest binary, preserve the accepted bytes. No reserve, controller, ranking or refusal changed.
- **Observed target result:** untraced cache candidate `harvest-and-exit@fc217c382f6b` versus
  startup-only `1ae4a3457744`, against pinned `fireflies@f6c57ee622a9`, uses Ambush and Big World,
  both sides. The exact-purpose comparison contains four fresh same-run controls and four
  candidates. Four older baseline rows were copied into the log, but changed purpose tags prevented
  arena resume reuse; those historical rows do not supply this comparison. All 20 candidate actor
  tracks leave spawn. Ambush A/B produce 3/1 clears, 153/51 gems and 681/282 gold with zero technical
  flags. Big World A/B have zero income, 26/15 deaths, all camp-attributed; both show active camp
  damage rather than the old stationary opening. Target stalls fall from four to two.
- **New health obstruction:** Big World A has 100k exhaustion at Marcella/Tavi R19 and Tavi R54;
  Big World B at Tavi R19. No subject debug receipt precedes them; ordinary samples start at R128.
  Native movement receipts and whole-turn fuel constrain ordering but do not locate the suspended
  C++ stage. Four exhaustions block full-pool qualification, which was not launched. The two
  remaining zero-income stalls are active engagement/continuation gaps; the private farm/inspect,
  commitment, recovery and entry-fuel branches remain unobserved, so no capture-expiry or combat
  threshold change follows. A separate bounded last-started/last-completed production-phase receipt
  at the earliest R19 crossing would discriminate the next fuel assignment while preserving that
  failure. A healthy same-key repair and physical continuation would change the health decision;
  geographic contribution still requires its own source-bound witness. No default enable or
  admission is claimed. Exact hashes, replay identities, limits and reproduction helpers are in
  [the retained result](../../results/population/territory-live/final.json).

Early fuel measurement (2026-10-22, default off; measurement obstruction):
- `P7_PHASE_TRACE=1` arms only initialized actor 2's R19 pipeline. It records at most 64 packed
  memory events and reuses existing production clock/fuel results without adding live SDK queries.
  The first changed clock freezes the prefix; overflow invalidates attribution. One bounded packet
  is attempted on a fresh owner turn at the 90k eligibility floor, skipping both `turn()` and
  `remember()`. That floor is a policy, not a universal logging bound; absent output stays unknown.
- **Preservation:** an initial off-arm move-condition spelling change failed binary preservation;
  its rejected manifest is retained. Restoring the original off condition verbatim makes all 16
  disabled raw/injected binaries and the untraced cache variant identical to the accepted bytes.
  The coherent runtime is `27a18486a712`; journal candidate `ea282d87dc6a` passes the 25/25 suite
  and independent source review. Synthetic journal tests prove mechanics, not native noninterference.
- **Observed:** one fresh Big World A pair against pinned Fireflies reproduces untraced
  `fc217c382f6b`'s three exhaustions (actors 2/3 R19, actor 3 R54). The journal preserves the two
  R19 events and emits one 58-event packet at actor 2's R21 turn. Its last same-round clock is the
  R19 commitment stamp; the first changed clock is R20 `remember()` completion. Existing fuel
  results increase from 27,304 at the movement retry guard to 97,062 at the remember guard.
  These intervals describe the journal run only; completion markers are not timestamps.
- **Falsifying result:** the canonical read-only comparator finds the first 3,421 exposed-world
  snapshots equal, then diverges at frame 3,421, R19 actor 2, input 289/substep 0. The baseline is
  at (79,10), phase 10/kind 4, awaiting input; the journal is still at (80,11), phase 13/kind 26,
  not awaiting input. Memory-marker cost shifts exhaustion before the target move. The same
  actor/R19 failure therefore does not preserve the baseline crossing. Comparison uses the full
  184,760-byte spectator state and 40,152-byte timeline, masking only identity bytes 48-55; guest
  private memory, fuel, logs and unexposed engine internals are outside this equality claim.
- **Decision:** finish this as a verified measurement obstruction. The untraced failure remains;
  the traced interval cannot identify a baseline leaf or exclude baseline `remember()` work.
  The later R54 event's absence follows a skipped diagnostic turn and is not health progress.
  No reserve, strategy, capture, combat or navigation repair, lower-cost trace rerun, default enable
  or admission follows. Reopen only with an independently cheaper or nonintrusive witness capable
  of distinguishing the baseline source phases while preserving the target physical boundary;
  repeating this logger is not that witness. Exact source, rejected and accepted byte proofs,
  raw health, prefix specification/report and limitations are in
  [the retained result](../../results/population/territory-phase/final.json).

Reciprocal source-field activation (2026-10-23, default off):
- `P7_RECIPROCAL_SOURCE_FIELDS=1` removes the repeated current-root flood only after every
  immutable source-root field completes. The fixed eight-neighbour terrain graph is undirected,
  so querying a source field at the current tile preserves the old minimum distance over that
  source's spawn tiles, including the 256 cap. Startup, a missing field or a failed partial write
  retains the old current-field path. Islander keeps its separate current-field mobility-ingress
  witness. Controllers, geographic commitments, safety floors, ranking and movement stay intact.
- **Preservation and tests:** all sixteen defaults plus startup-only and stationary-cache
  Harvest preserve both raw and injected bytes: 36 exact comparisons on toolchain `3837f00416ea`.
  The two historical enabled snapshots had stale `5d1fa9d78574` stamps; their bodies were preserved
  and current-toolchain incumbents were built before comparison. The suite passes 26/26, including
  actual extracted readiness, distance, warm-publication and dynamic-caller checks on blocked,
  disconnected and multi-spawn graph doubles. Those doubles prove no native fuel bound.
- **Exact native result:** twenty fresh full raw-role games use Ambush and Big World, both sides,
  against pinned productive Fireflies `f6c57ee622a9`: default, startup-only, stationary cache,
  reciprocal ON `bc7b172cad2f`, and its existing `P7_CONTROL` arm `47fc4f33553e`. Default's twenty
  subject actor tracks stay stationary over R1-80 and earn nothing; its ten Ambush startup
  exhaustions disappear with startup-only, which still earns nothing. Cache reproduces four
  Big World exhaustions. Reciprocal ON removes all four; all twenty ON tracks leave spawn.
  Ambush banks 153/241 gems versus cache 153/51 and geographic control 37/0. Big World still
  earns nothing, with 49/28 camp deaths: two stalls continue to block full-pool qualification.
- **Paid continuation:** on Ambush B, core Vendetta receives 26 gems at R444, travels loaded from
  (44,64) to (38,58), completes its bank at R453 and resumes movement. Core Ren banks its R489
  17-gem payout at R575. Tavi completes four capture-to-bank episodes (R444/452, R489/576,
  R704/712, R1386/1413). These prove native work and banking, not private raid retirement or a
  completed withdrawal-to-recovery commitment. The control contrast also includes the charter's
  existing arbitration and bank-target differences; it does not isolate geography alone.
- **Fresh confirmation and limit:** eight further raw games use Fundamentals `85ce679dc7b1`
  on Ambush and Eight, both sides, ON/control. ON banks 255/255 on Ambush versus control 0/0;
  Eight is 196/0 versus 44/94 (ON B clears one gold-only group). All eight ON games across both
  experiments have zero faults, exhaustions, error lines and ordinary budget hits. Only Harvest's
  enabled behavior was played; no default enable, full-pool health or contribution admission.
  Next inspect Big World A Ren's first camp death at R101 and B Vendetta/Ren at R104/105:
  personally visible camp range, approach, retreat and recovery actions must distinguish the
  remaining unpaid-work obstruction. Do not repeat the failed phase logger or replace the
  territorial controllers with farming. Exact inputs, native per-actor records, failures and
  owned Job receipts are retained in [the result](../../results/population/territory-reciprocal-fields/final.json).

Range-aware recovery experiment (2026-10-23, rejected, default off):
- Big World A Ren loses personal vision of monolith 57 after R92 at (44,49), inside its
  queried range 5. Recall restarts under camp damage at R98–100 and Ren dies at R101.
  B Vendetta/Ren instead oscillate inside visible monolith 56's range and die at R104/105.
- `P7_RANGE_RECOVERY=1` retains only personally observed awake stationary camp envelopes;
  personally observed asleep/dead camps clear them. A bounded local escape uses fresh occupancy,
  keeps the existing camp range+1/enemy-8 recall clearance, and leaves source choice, all sixteen
  controllers, raid retirement, banking and geographic commitments intact. The first search
  revision stalled B's retreat; the second snapshots occupancy once and reserves an ordinary move.
- Both revisions pass 27/27 focused checks and all 36 OFF raw/injected byte comparisons against
  the sixteen defaults and reciprocal Harvest/control incumbents on toolchain `3837f00416ea`.
  Four full raw games per revision pair incumbent/ON on Big World both sides against frozen
  productive Fireflies `f6c57ee622a9`. Final ON `8a2dc7933f81` reduces deaths from 49/28 to 23/16.
  A Ren and B Vendetta/Ren now escape, recall, heal and resume travel. **Both sides still earn,
  clear, collect and bank zero; both remain stalls.** A Vesper Vale exhausts once at R1745;
  that suspended source phase is unproved. Fireflies banks 582/564 against final ON.
- This is a decisive negative for productive recovery/renewal, not a release candidate. No
  Ambush continuation, full-pool, fresh-opponent qualification, default enable or admission follows.
  Next discriminate the repeatedly selected group's actual approach and complete clear/capture
  feasibility after a successful return, while preserving each controller's territory commitment;
  another retreat-only search cannot explain away zero paid work. Exact failed source/modules,
  physical windows, native health and empty Job receipts are in
  [the result](../../results/population/territory-recovery/final.json).

Uncertified partial-admission discriminator (2026-10-25, rejected, default off):
- The exact Big World recovery cases execute real partial attacks and retain damage across
  healing/return. Neither central monolith group is captured by either team in either replay.
  The retained catalogue already contains a harmless 240-HP, 40-gold source; it is unseen at
  the selected contacts. This justifies testing exploration starvation, not banning staged work.
- `P7_NO_PARTIAL_ADMISSION=1` adds only the seen, non-solo-feasible, non-joint-feasible,
  non-reward-ready refusal inside `productive`. Existing vetoes, reserve, controllers, geography,
  unseen inspection, safety, recovery and banking remain. The lone-source controller fallback
  can still return partial work. Production planner fixtures change fresh/retained monolith
  farming into unseen-source inspection, retain the listed certificates and expose that limit.
- All sixteen default RAW bodies and the exact historical four-override RAW recipe remain
  byte-identical; keeper source, recipe and toolchain checks pass. The suite passes 29/29 plus
  both strengthened production fixtures. Those supplied cards prove shared selection behavior,
  not native fuel or every controller programme.
- Four full RAW games on Big World A/B against exact productive Fireflies `f6c57ee622a9` earn
  zero gold, spent gold, collected gems and banked gems in both arms. OFF reproduces monster
  damage 14,029/14,536 and deaths 23/16. ON has zero monster damage and deaths; A exhaustion
  rises from one to six, while B has no recorded health flags. This is no paid-work repair.
- Ready position/HP/gold/bag samples first differ at R84. The R83 ordinary move reroutes in
  both games when one central group is fully personally visible. At R92–97, ON Vendetta really
  sees both live 120-HP gold clusters of source 3 on both sides and continues moving. Discovery
  occurred; payout, purchase and next paid work did not. The targets remain 2–6 tiles away
  against basic range 1, so this does not establish a missed legal attack or a complete alternate
  approach programme.
- Fuel differs earlier at entry 90 (+188 ON). Public equality does not establish private fuel,
  quote or journey equality, nor a marginal cause solely from the eligibility branch. The next
  unresolved contract is discovered source -> qualified work programme/approach/execution;
  quote completion, ranking and geographic commitment remain separate explanations. Stop this
  ablation, keep it OFF and preserve staged/partial work. No new threshold, exhaustion repair,
  third arm, broad panel or promotion follows. The experiment is closed and selected raw
  diagnostics removed. [Diagnosis](../../results/population/territory-admission/final.json) and
  [bounded result](../../results/population/territory-admission/ablation.json) retain exact inputs and limits.

Pre-update Main53 generalization screen (2026-10-25, existing recipe, default off):
- **Question:** does the already productive reciprocal-field Harvest recipe generalize beyond
  the repeatedly investigated Big World failure? No policy or source was changed. Current-source
  alias `5ab1a71efc4d` uses only bounded initialization, current-field reuse and reciprocal source
  fields. Its RAW exactly matches the actual retained `bc7b172cad2f` RAW; its SDK module matches
  the historical recorded hash, although that old SDK body is missing. Both new bodies are retained
  owner-locally. Range recovery and partial-admission refusal remain OFF.
- **Observed:** `territory-next/harvest-floor53-diagnostic` completes all 40 pool maps, both
  sides, against frozen `anchors/main-v53@fa5b8ae8b5ae`: **0 wins, 0 draws, 80 losses**, with zero
  recorded health flags on either team. Harvest banks in 52 games and earns gold in 64; fourteen
  have neither gold nor banked gems. Lanes A banks 667 of 703 collected gems, earns 3,610 gold,
  spends 3,210 and loses one actor before Main banks 1,001 at round 1,060. Big World A/B still
  earn nothing and suffer 22/15 deaths; Main ends those games at rounds 1,131/1,055.
- **Decision:** Big World's unpaid work is not representative of the complete pool. Stop using
  that one obstruction as the sole basis for suite work selection. This screen supplies no win,
  default qualification, stall clearance or geographic-mechanism attribution; its owner-private
  inputs are diagnostic only. Shorter games against Main do not erase the historical late
  exhaustion against Fireflies. The next useful native condition is Lanes A: witness whether a
  paid raid and bank actually cause the prescribed withdrawal to an inner source and renewed work.
  A distinct physical choice and continuation would justify developing that controller; coincident
  generic farming would instead challenge its coverage credit. Do not infer a speed or coefficient
  repair from these aggregate rows. Exact identities and the closed experiment descriptors are
  appended to the [reciprocal-field record](../../results/population/territory-reciprocal-fields/final.json).
  This is CLI/SDK 3.0.5, engine 1.0.3 evidence. The user's subsequent game-update notice freezes
  further native work until the integrator reconciles the bundle; no new-bundle validity is inferred.

Staged runtime patches (leads, not applied): diffs of today's `common/src/runtime.h` under its old
path `bots/p7-common/src/runtime.h`; both apply cleanly and compose. Each changes all 16 bots:
- `leads/runtime-warm-batch.patch`:
  `warm()` prepares up to 10 immutable fields per turn while fuel stays at or above 65k and skips an
  absent second altar: actors could start up to nine turns earlier when fields are cheap. No game run.
- `leads/same-source-basic.patch`:
  after a spell kills the selected camp member, spend the still-available basic attack on another
  visible member of the same source in range (25k/20k fuel guards). Benefit unmeasured.

Tried and reverted (do not retry without a new reason): common opening crew, Strength for Tavi,
deeper partial clears, role partition, concentric blockers, sanctuary independent workers, Vendetta
or Vesper for Stellar in forks. Kept: contest posture, concentric/forks openings, interior bootstrap.

Idea bank (unimplemented):
- Route objects for all designs: legs and a cursor drive estimate and movement; a block invalidates.
- Complete static cut proofs that also check dash bypasses over holes; a cut-off search stays unknown.
- Islander: shortlist slots for groups reachable only by dash or teleport, identical in the control.
- Interior-lines: real rally pads on retained routes instead of productive sources.
- Encirclement: two enemy routes and substitute altars; more than two altars needs an exclusion proof.
- Reclamation: check offline from replays whether residual camp damage came from rival work.
- Veil: plan the single ward's relocation (ready again after 10 own turns) around the key exit.
- Sanctuary: price Willow's actual healing time into the basin choice.
- Self-play: forks and ports vs encirclement, the suite's in-house exit-versus-denial test.
- Counterfront and elastic-border vs `anchors/main-hunter7`, so their pressure triggers fire.
