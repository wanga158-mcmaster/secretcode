# models: sixteen models of the game

## Doctrine

**Question.** Which model of the game's dynamics gives better forecasts and, through one
fixed ranking, better decisions? A controller here is observer -> model -> selector ->
executor. The suite holds the observer, selector and executor fixed and varies only the
model between observation and decision. The shared hypothesis: a better model ranks a
different, better job where its coupling applies and plays like the reference elsewhere.

**Held fixed in `common/` (every bot, one owner):**
- Legal observer: personal vision and own ward only. Each fact carries value, observed
  round, source and status (exact-now / derived / predicted / unknown). Unknown is never
  zero, never assured stock and never permission for an SDK call. Teammates' sight,
  private gold, hidden bags, capture progress and enemy intent are unavailable.
- Phase clock: round start resolves respawns, captures and camp actions, then owner turns
  alternate; a channel resolves at a later owner start; spawn healing at turn end.
- M0 forecaster (`pure.h`): serial travel -> clear -> capture -> bank/restore per actor,
  conserving bag, health, captures and respawns, with combat stats frozen. No free reset,
  duplicated reward or fractional tribute.
- One menu (at most 16 jobs, built before any model runs): 8 single-camp FARM, 4 two-camp
  FARM pairs, 2 BANK, RENEW, HOLD; in-progress jobs keep their slot. One continuation (the
  8 single-camp jobs in M0 order, cyclic, at most 8 macro jobs, H = min(128, rounds left)).
- One ranking: lexicographic `(E[Z], E[our - enemy secured gems], -E[first conversion],
  -travel, job key)`, where Z = +1/-1/0 for the predicted first team to the winning total.
- B0-derived executor (`legacy.h`, `*-methods.h`): movement, combat, emergency safety
  (outranks any forecast), channel/capture preservation, the inherited purchases Monster
  Strength x5 -> Jungle Health x5 -> Power Strength, and the bootstrap fallback (bank,
  renew, else B0 harvest) when no productive forecast exists.
- Bounded work: 16 candidates x 8 macro jobs, 5 actors, 256 graph visits per owner turn,
  complete caches only; actions stop below 30k fuel, optional work below 40-45k.

**Each bot varies only** its predictive coupling (private `src/runtime.h` adapter plus
pure `src/<bot>.h` model, selected by `PLAN4_RUNTIME_TYPE`) and its own draft profile
in `common/draft.h`. The knob override `PLAN4_MODEL_ENABLED=0` is its
mechanism-disabled control: same menu, ranking, executor, draft and evidence collection,
coupling off. A model must say which it changes: state variables, a coupling, a duration
law or observation reconciliation. A coefficient or threshold alone is not a new model.

**Not the neighbours.** `decision` varies the objective over a shared forecast; here the
objective is fixed and the forecast varies. `events` fixes one exact event-time arithmetic
and varies the policy; here the policy is fixed and the world model varies. `frontier` and
`specialists` change the whole decision procedure or strategy, and `doctrines`,
`territory`, `distributed`, `adaptive` and `commitments` change economic, geographic or
organizational policy. Never import those policies here: a new bot is a new dynamics law
under the same menu, ranking and executor.

## Bots

No model is accepted; no coupling has been shown to change a native decision (see Leads).

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| reference | M0 only, over the common menu and ranking; draft Tavi, Varg, Vendetta, Prism, Marcella | Current bundle, snapshot `3800e230ed2e`: 1/327 emitted decisions forecast-ranked in two full 00rows games against fundamentals; clean on that scope, no broader current health claim |
| foundry | Witnessed ally attacks/holds use up the same monster health and split capture rewards, so a group an ally already works ranks differently (control: independent copies) | Default `1fd8b5c86e14` keeps historical execution. Opt-in composition ordering removes most invalid layout frames; opt-in startup yield removes witnessed round-one exhaustion on 00collision. Neither produces a forecast-ranked model job; that map still stalls |
| governor | Per-phase residual `b <- clip(.75b + .25(actual - nominal), +-d/2)` corrects travel/clear/capture/bank/restore durations after 3 uncensored completions | Needs 3 positive uncensored current-kit completions per phase; own capture attribution unavailable |
| habitat | Nearest-altar patches: capacity `C = sum gems/(clear+capture+respawn)` and witnessed pressure n add supply waits `max(0, G(1+n)/C - service)` once seen ready stock is used, which can make relocation rank first | No productive relocation witness; v1 lost 170-737 to its own control on 00rows (teamA) |
| repairman | Camp damage persists across a safe escape, recall and restore, so a two-excursion FARM (at most 4 attacks per checked safe segment) can beat a fresh full clear | No live attack-frame/cycle producer: recall arrival is not the canonical spawn tile, so the cycle endpoint is undefined |
| archipelago | A freshly seen blocker on an articulation tile reroutes every dependent job at once (biconnected components, at most 256 visits per turn) via a proved alternate route | Native v8 emitted no decision rows (0 fuel exhaustion, 0 errors): observation plus navigation startup must fit the turn; no legal blocker witness |
| timetable | Two-frame extrapolation of up to 4 visible actors predicts vertex conflicts in our next two route rounds and adds at most 2 wait phases | Unknown turn order keeps current-only occupancy (M0); no traffic witness |
| stewards | Owner = group ordinal mod actual roster size, backup = next slot; other owners' groups are forecast consumed; backup takes over after 8 full-sight no-service rounds | Living service reach unknown; takeover never witnessed |
| bulkheads | One witnessed connector-block cause shares its estimated release time across every dependent phase (fit from at least 8 uncensored episodes) | No fitted episodes or release producer: forecasts exact M0 in ordinary play |
| chronicle | A legal history of witnessed captures plus queried respawn time bounds readiness after sight is lost and defers premature revisits | No qualifying history found yet; equals M0 without a witnessed capture |
| quotient | Single-camp jobs with equal composition, lifecycle, health quarter and `ceil(distance/4)` bins share one conservative representative forecast | Differs only near a deadline/alias boundary; alias witnesses uncollected |
| counterpart | A known reactive rival rule (chase the nearest visible positive-bag enemy, else a fixed camp order) is simulated for up to 6 owner turns against each candidate's first step | No rival-profile provider installed: both arms use M0, so it plays as reference with its own draft |
| kernel | Eight joint (duration, health, economic outcome) paths from logged attempts replace mean job times at deadlines; its `exact-kit.h` and `kernel.h` are shared | No fit table (needs first-eight-started bins): falls back to M0 with reference's draft order, so it plays as reference |
| capital | The fixed purchase priority propagates through cash -> equipment -> combat stats -> clear time/damage -> restore need (control: same purchases, frozen stats) | Purchase/combination parity across the three item families unverified |
| commons | One lifecycle stock per group, consumed in time order by our team and at most one visible enemy producer with a frozen priority (spawn distance, ID) | No sole-producer isolation or enemy route quotes; unknown opponents need not follow the rule |
| metronome | Refinement order by ranking closeness (optimistic vs leader pessimistic) inside the fixed 256-visit quota and fuel reserve decides which complete forecasts exist (control: round-robin) | No measured native cost envelope bound: both arms report unsupported and use M0 |
| covenant | Each job is a contract automaton (approach -> clear -> capture -> convert -> restore, plus repair) with premises and deadlines `start + ceil(1.5 nominal) + 2`; a witnessed premise failure predicts failure and the repaired plan | Two source-bound ranking witnesses pass; FARM milestones, interrupted-phase checkpoints and alternate-path proof incomplete |

## Distinctness rules

- A bot is distinct only when a reproducible legal situation makes its coupling choose a
  different job than reference. Show it as a decision difference against its own
  `PLAN4_MODEL_ENABLED=0` control (same map, seed and side) plus an offline case in `tests/`.
  A different forecast number that never changes the choice does not count.
- Two bots that make the same choices count as one. Today counterpart, kernel, metronome
  and bulkheads always forecast M0, and chronicle and timetable do until their evidence
  appears: they choose as reference does and differ only in draft and evidence cost
  (kernel even shares reference's draft). A draft profile alone never makes a model.
- No private menu, ranking, executor, safety threshold or purchase policy. A model without
  support falls back to M0 for that actor, never to a guess. Online models never read
  replays, native telemetry, files, future rounds or another actor's state.
- Shared code is `common/` plus `kernel/src/exact-kit.h` and `kernel/src/kernel.h` (every
  bot reaches them through `common/attempt-preaction-sdk.h`). One owner per suite at a
  time. A shared change affects every bot: rerun `./arena.cmd test models` and
  `./arena.cmd build models` for the whole suite. Code under `<bot>/src/` belongs to that
  bot; if another bot includes it, it is shared.

## Layout and checks

- `<bot>/src/main.cpp` defines `PLAN4_RUNTIME_TYPE`, `PLAN4_MODEL_ENABLED`,
  `PLAN4_DRAFT_PROFILE` and evidence flags (`PLAN4_ACTOR_EVIDENCE`, `_SPATIAL_EVIDENCE`,
  `_STATIC_SPATIAL`, `_PUBLIC_COMPOSITION`, `_OWN_ACQUISITION`), then includes
  `../../common/model-main.h`; `reference` includes `common/legacy.h` directly. A new bot
  also needs a `Profile` in `common/draft.h` and an entry in `tests/plan4-draft-profiles.py`.
- `common/`: `pure.h` (SDK-free M0, menu, ranking); `observer.h` and `*-evidence.h` (legal
  observation); `nav.h`, `movement*.h`, `alternate-*.h` (bounded navigation); `legacy.h`,
  `legacy-policy.h`, `macro-/attempt-/production-/acquisition-methods.h` (executor and
  phase records); `safe-segment*.h`; `phase-*.h`; `model-runtime.h` and `model-main.h` (runtime
  hook and entry); `draft.h` and `draft-sdk.h` (17 profiles); `telemetry.h` (bounded
  `p4 ...` rows); `fixture-methods.h` (staged-fixture path, used only by tests).
- `tests/run.py` is the single entry: `./arena.cmd test models` runs the suite;
  `./arena.cmd test models -- foundry` runs the shared cases plus foundry's;
  `--list`, `-k TEXT`, `-q`. Kinds: `cpp:` WASI fixtures at -O3 on the 64 KiB stack the bots
  link with; `sdk:` strict syntax checks of each bot and fixture main on the pinned SDK;
  `host:common-constructor`; `py:` harnesses compiling actual methods against SDK doubles.
  `tests/fixtures/<bot>-fixture/` are staged setups (test-only, not bots) and
  `tests/fixtures/plan4-*-sdk/` restricted `cambc.h` doubles. `plan4`/`PLAN4_`/`p4` are this suite's names.
- `./arena.cmd build models` builds the 17 bots. A control arm is the knob override, e.g.
  `arena sweep models/foundry --var "off: PLAN4_MODEL_ENABLED=0" --vs anchors/fundamentals`.
- `tools/kernel_attempt_log.py LOG` parses `p4 attempt` rows (Kernel's only route to fit data);
  `tools/telemetry.py` validates `p4 decision|summary|milestone|abort|draft|phase|input` rows.
- `tests/population.py` exposes 17 unadmitted behavior contracts and an offline planning-path
  observer. With the arena Python, run `tests/population.py RUN_DIR models/foundry` to count
  native decisions/phases, or `--self-test` for the observer's framing checks. Native stdout
  may NUL-pad owner-turn boundaries. These counts are diagnostics, never coupling admission;
  opportunities remain unknown until a legal trigger observer exists.
- Fuel pitfalls: eager setup of 2,048 forecast slots before the fuel-budget check exhausted fuel
  (reference-v1). Keep initialization lazy, cache public terrain, rotate navigation requests,
  avoid per-field stream formatting; builds and WASI tests do not prove native fuel safety.

## Leads

Open problems, in order:
1. **Productive native planning is rare and model evidence is truncated.** Current-bundle
   remeasurement on 00rows against frozen fundamentals, both sides: reference has 1/327
   forecast-ranked emitted decisions (126 budget, 200 bootstrap); Foundry active and off
   each have 0/222 (184 budget, 38 bootstrap). Foundry's explicit masks show invalid
   public-layout frames in 220/222 consultations, truncated actor frames in 157/222 and
   truncated menus in 32/222. These deficiencies overlap. The shared layout producer
   `OnlineObserver::layout_for_menu` prepares immutable composition after spatial evidence
   under the remaining planning reserve. The metadata-cost hypothesis was rejected:
   pinned `kind()` and `spawn_position()` are local getters. The opt-in producer-order
   experiment below targets the late startup admission guard. Shared collection/menu/evaluation costs need a bounded experiment
   preserving the fixed legal inputs, menu and ranking, followed by the same native panel.
2. **Test the cost explanation before tuning.** Earlier active/control losses suggested
   active-only evidence cost. In the current paired Foundry sweep the active/off native
   physical replay streams and subject fuel streams are byte-identical on both sides.
   That explanation is unsupported for these versions/conditions. All consultations have
   reason 2 (truncated), so the absence of productive forecasts is not proof that a legal
   Foundry opportunity occurred and failed. Current source is not a useful admitted model.
3. **First legal witness per model:** produce each State column prerequisite in ordinary
   play, then a decision difference against the control, before any strength claim.
4. **Kernel data:** log `p4 attempt` rows in ordinary games, parse them with
   `tools/kernel_attempt_log.py` and build the table from the first eight starts per bin.
5. **Expand reference's current coverage** beyond 00rows/fundamentals to fresh maps and
   starter. Its entrypoint omits the active wrapper's `after_turn()`, so captured phase
   samples are never flushed: no phase rows is a measurement limitation, not proof that
   planning was never reached.

### Integrator check, 2026-10-21

Observed: `arena test models` passed 101/101 existing cases; no gameplay source changed.
Six full-length native games cover one map, both sides, one frozen opponent and two
Foundry arms. Both teams in all six have zero faults, fuel exhaustion and logged errors,
and clear at least one camp. Reference's ordinary fuel maximum was 77,449; Foundry's was
91,501. This is targeted health evidence, not the full-pool/cross-suite admission panel.
Per Foundry arm, 89 observer, 57 menu and 33 evaluation phase samples were emitted;
their maximum sampled costs were 40,148, 15,359 and 10,219 respectively. Sampling does
not count all opportunities or planning attempts. The observer initially missed NUL-padded
rows; its framing fixture now covers that native format before these counts are recorded.

Interpretation: retain the models' distinct intended couplings as unresolved contributions,
and assign the common evidence/planning producer before individual model tuning. No model
is admitted on this result. A fresh active/off case with different physical execution or
supported productive model-ranked jobs would change this work selection. Pure ranking
fixtures alone would not. The current bounded check finishes with a precise shared
obstruction rather than a speculative gameplay rewrite.

Evidence, source/test/toolchain/map hashes, paired stream hashes and the six necessary
native replays live in [results/population/models-runtime/evidence.json](../../results/population/models-runtime/evidence.json).
Reproduce the native checks with `arena play models/reference anchors/fundamentals --maps
00rows --keep-replays all` and `arena sweep models/foundry --var "off:
PLAN4_MODEL_ENABLED=0" --vs anchors/fundamentals --maps 00rows --keep-replays all`.

### Shared producer and startup experiments, 2026-10-22

Two independent development knobs remain **off**: `PLAN4_COMPOSITION_FIRST=1` collects
immutable public composition immediately after the fixed menu, before current spatial
masks; `PLAN4_STARTUP_YIELD=1` yields after common initialization when less than the
existing 30k turn reserve remains or the owner clock changed, and suppresses that yielded
turn's after-turn work. The next turn resumes the normal executor. Neither changes the
menu, ranking, draft, purchases, safety rules or a model's coupling. All seventeen fully
disabled raw and injected WASM outputs are byte-identical to the pre-experiment builds.
Production-block fixtures cover exact public-frame parity, 6,599/5,999 fuel headroom,
stale/oversized-layout refusal, the 29,999/30,000 startup boundary and next-turn recovery.

Observed on 00rows/fundamentals, both sides, with same-run disabled, composition-first and
composition-first/model-off arms: invalid-layout masks fall from 220/222 consultations
to 14/211 that reach the truncation guard (11 stop earlier). All six games are clean,
but none produces a forecast-ranked decision. Forty-two repaired consultations reject
the later roster/kit/menu guard. Telemetry does not identify which predicate failed;
an empty FARM menu is a hypothesis, not a measured fact. Native spatial completeness
is also unmeasured after the reordering, so a valid layout alone is not a supported model.

The fresh 00collision/starter panel exposed four first-gameplay-round exhaustions per
side in every original arm, followed by no camp clears. Constructor, common initialization
and the first summary already completed; that summary reports only 4,201-4,207 fuel.
The map has 20 groups/56 members versus 00rows' 9 groups/54 members. This supported a
small handoff guard before more observation/action queries, rather than a metadata cache
or a model rewrite. Startup-only, startup+composition and startup+composition/model-off
arms each remove all eight paired exhaustions, with no faults or logged errors. They
still stall and emit no decisions on 00collision. A repeated 00rows panel stays clean;
startup+composition there has 0/221 productive decisions and 13 invalid-layout masks
among 210 consultations reaching the guard. Both enabled model arms still execute the
same physical game, so there is no coupling activation or distinct contribution claim.

Interpretation: these are useful shared-runtime fixes in development, not admitted bots.
Startup exhaustion is resolved on the measured case, while general initialization safety
and the zero-clear failure remain open. Next investigate native admission predicates:
`macro_decide`'s 75k gate precedes navigation warmup, and `macro_move` separately requires
65k for cold movement setup. No current trace establishes which guard causes the stall.
A production guard witness plus a physically executed productive forecast would change
that work selection. Preserve the model commitments; do not substitute another policy.
No default enable or full-pool health claim is justified by this two-map/two-opponent panel.

Exact source/test/toolchain/map identities, disabled build comparisons and native witnesses
are retained under [results/population/models-producer](../../results/population/models-producer/).
Reproduce with the controls in that directory's evidence record; use `arena sweep` with
`PLAN4_STARTUP_YIELD=1`, optionally `PLAN4_COMPOSITION_FIRST=1`, and a same-input
`PLAN4_MODEL_ENABLED=0` arm. Both sides and the known 00collision health failure remain
mandatory checks for any later producer repair.

### Observer query and clock discriminator, 2026-10-23

Observed: `cpp:observer-state-boundary` executes the actual `OnlineObserver::observe_into`
against an opt-in wrapper-seam spy. A visible damaging member next to the owner is read
twice, including when that member is dead in a still-living partial group; the farther
member is read once. A hidden nearby member is never read and cannot certify safe recall.
No-op callback twins agree on every represented Information field and provenance, observer
diagnostics, menu, evaluated M0 forecast/event and ranking choice. First-read, second-read
and raw-packet exceptions propagate; partial exception outputs are not usable. The synthetic
collection-floor +1,999/+2,000 boundary allows one/two nearby state reads respectively;
both completed observations truncate later and refuse safe recall. The new case and all six
existing SDK-double C++ cases passed at -O3 on a 64 KiB WASI stack, under one shared slot
and one confirmed-empty private Job. No gameplay source changed.

A forced R10-to-R11 transition between the member and recall reads returns nontruncated
Information stamped R10, with exact R10 health and recall safety from the later dead-state
read. This establishes the collector's conditional clock-stress behavior. It does not show
native suspension is reachable through the current fuel reserves, that a macro uses this
output, or that a stale SDK action occurs. The actual caller has a later clock fence before
its main macro dispatch. The actor-switch case is separately synthetic: a production guest
has a fixed SDK self identity. Same-round state changes are also robustness probes. These
wrapper counts do not measure native import multiplicity or fuel; the double does not model
the SDK's asleep/invalid-enum ABI, native visibility at a monster's current tile, or exact
SDK error types. Its default hooks remain absent in the six compatibility cases.

Interpretation: repeated reads are a concrete mechanical opportunity, not yet an affordable
cache or a current production defect. None of the eight saved Collision observer samples
has joinable own position/visibility, so their duplicate-read frequency remains unknown;
the four current cost games report no exhaustion. Park query reuse and a gameplay clock
repair. Reopening reuse first requires actual production-call multiplicity and cost under
the unchanged caller. Useful native savings plus demonstrated safe reuse would reopen the
candidate; zero/rare applicability or stale/error divergence would reject it. Preserve
legal inputs, completeness, all reserves, menu, ranking and each model's coupling. No model
is admitted by this fixture. Exact sources, commands, WASM, logs and scoped reviews are in
[the retained discriminator](../../results/population/models-observer-discriminator/index.json).

### Actor-frame correlation, 2026-10-23

`PLAN4_ACTOR_BIND_DIAGNOSTICS=1` appends `abV=1` cached producer fields to the existing emitted
model row. It adds no SDK queries or log rows and keeps collection, evaluation and sampling
order. `abBefore`, `abMinimum`, `abStop` and signed `abStopFuel` correlate the actual actor
collector with its frame clock, counts and completeness flags plus the evaluated menu's
single-farm and total counts. `abMinimum` is the admission threshold, not a measured low-water
mark; `abStopFuel=-1` means no first stop was recorded. An early admission failure can leave
the frame's own id at its default zero, so that value alone is not an identity handoff defect.

`abProducerBad` is an incomplete public producer projection of the compound guard; cached kit
seen/round/epoch fields do not certify the consumer's private `kit.exact` or every binder
predicate. Foundry copies the actor frame immediately after collection, and does not recollect
it before this row. A diagnostic game remains an instrumented observation, not a reconstruction
of the old R1287 invocation or proof that a later model guard or forecast succeeds.

`cpp:actor-bind-diagnostics` exercises the actual append fragment with the real actor collector.
ON and OFF controls preserve cached field values, represented actor state and query counts for
complete, entry-fuel, collection-debit and clock-stop schedules; the disabled row has no `ab`
fields. Signed unset values and a 1,000-byte existing prefix fit the bounded writer. The menu
and kit inputs in this serialization fixture are supplied; producer-to-binder feasibility is
already covered by `cpp:foundry-coverage`, rather than re-proved by this hook test.

Observation: all 17 affected default snapshots and the repaired Foundry control retain exact
current-toolchain raw and injected module bytes with the knob off (36 same-role comparisons).
The fixed Rows/fundamentals experiment used injected modules, both sides, one same-run baseline
and one diagnostic game per side; all four games passed execution and both-team health checks.
Of 224 emitted diagnostic model rows, 160 retained a fuel stop after the entry sample and 64
retained no stop. Six farm-present `reason=2, truncationMask=16` rows joined their decision and
current actor frame without inconsistencies. The first (A, actor 1, round 1410, job 40) sampled
99,617 fuel before collection, admitted against 94,817, and retained its first stop at 94,785;
roster and own flags were complete, with one single-farm option in a five-item menu.

Interpretation: these emitted instrumented rows identify collection debit below the existing
threshold as the first stop, rather than entry denial, a clock stop or a roster/handoff defect.
They do not identify the exact denied query or explain every later binder predicate. Logging
can perturb future execution, so this does not reconstruct the old R1287 row. Keep thresholds,
menu and coupling unchanged; any follow-up needs a separately chosen question and evidence.
The exact preserved inputs, module roles, commands, output and cleanup records are in
[the retained actor correlation](../../results/population/models-actor-bind/index.json).

### Native data-delivery repair, 2026-10-23

The shared producer can now use invocation-local SDK packets without constructing
champion wrappers: `PLAN4_OBSERVER_ACTOR_PACKETS=1` for visible observer bodies and
`PLAN4_PRODUCER_ACTOR_PACKETS=1` for visible cleared-spawn producers. The independent
`PLAN4_ACTOR_PACKET_BLOCK=1` decodes the already fetched actor packet as one guarded
local block. Foundry also has `PLAN4_FOUNDRY_ACTIVE_STORAGE=1` to initialize counted
input slots instead of unused capacity, and `PLAN4_FOUNDRY_ROUTE_DOMINANCE=1` to skip
route proofs for endpoints that cannot beat a fully proved endpoint. All five knobs
default off. `PLAN4_FOUNDRY_BIND_SITE=1` is a separate, perturbing first-stop diagnostic.
Visibility, stock and participation premises, clock fences, reserves, menu, ranking,
draft, purchases and model laws are unchanged. No body packet survives its invocation.

Observed: all 112 suite cases pass. All 17 defaults, repaired Foundry and three earlier
repair controls preserve raw and injected bytes separately (42 same-role comparisons).
Current injected engine-1.0.3 Rows/fundamentals panels use baseline, repair and mechanism
disabled controls on both sides. The observer-only increment changes Foundry A from two
paid actors/two credited clears/no bank to all five paid actors/24 credited clears/60
banked gems. Side B changes from 122 to 67 banked gems. Adding the producer increment is
mixed: A has three paid actors/20 clears/67 banked; B has four/11/zero. These are actual
native fallback activity changes, not forecasts or general strength improvements.

No tested predictive coupling contributed: Foundry, Habitat and Chronicle each report
zero supported model rows and zero forecast-ranked decisions in their six-game Rows
panels. Foundry and Habitat enabled/control replay and subject fuel streams match
exactly; Chronicle's differ despite zero support, so physical differences alone do not
establish a model contribution. Foundry's route-proof increment is an exact native null.
The packet block improves sampled complete actor frames from 122/630 to 276/630 without
activating a forecast. A concrete complete-frame, five-candidate consultation (A, actor1,
R721, job22) loses 58,934 sampled fuel before evaluating any candidate. The opt-in stop
witness locates the first denial at the initial group fence after binder preparation;
counted-slot initialization alone still does not deliver a usable evaluation reserve.

The final producer repair, its same-run baseline and its mechanism-disabled arm all
stall on both Collision and fresh Gem Fields: 12 full games, zero subject camp damage,
income, captures or banks, with no faults, exhaustion or logged errors. No default
enable, model admission or broader health claim is justified. The next bounded question
is the fuel delivered through Foundry's pre-group binder preparation and per-member
validation before the first candidate, using the indexed complete-frame witnesses;
another route optimization or coupling change cannot address the measured first stop.
Exact selected source/module/input/native/test/proof descriptors and the retained cache
locators are in [the bounded native result](../../results/population/models-native-activation/index.json).

### First-candidate discriminator, 2026-10-23

The R721 debit above spans common observation, navigation and frame delivery as well
as private evaluation: `decision.fuelBefore` is sampled before those stages. It is
not a measured 58,934-fuel binder cost. `PLAN4_FOUNDRY_PREP_TRACE=1` samples fuel and
actual round at private stages for actor1/R721 only, then emits one cached diagnostic
row. The exact information/menu hashes reproduce (`2301730268`/`2496269542`), but the
instrumented evaluator arrives with only 40,563 fuel against the unchanged 40k floor.
Its sampling overhead moves the first denial to the entry fence at 39,958, still R721.
Later private stamps remain unset; the trace does not measure successful stock binding.

`PLAN4_OBSERVER_LOCAL_MASKS=1` exports the existing observer masks once and reads their
packed bits locally. It keeps invocation-local lifetime, current member/body reads,
visibility and error semantics, with conservative export admission. This is a rejected
budget-delivery experiment, not a fuel saving: sampled truncated observers change from
16/75 to 140/140, including 105 group-admission stops and 29 new 8k altar-export stops.
Menu bit2 inherits observation truncation; it does not isolate menu-building cost.
The matched initial actor0/R10 prefix costs 1,575 more fuel before collection. Both
new knobs default off; existing reserves and model premises remain unchanged.

The shared M0 planner nevertheless supplies one actual paid BANK continuation in the
six-game Rows panel: A actor3 chooses job28 at R1886, moves R1887--1932 and submits 19
gems at R1935. The official native event agrees with bag19->0 and team score60->79.
Its Foundry row is unsupported (`reason=2, mask=18`), and the model-disabled arm has
the identical continuation, replay and subject fuel. Foundry has zero supported rows
on either side. This proves a productive common-planner path, not a coupling contribution.
Total A activity changes from two paid actors/two clears/zero bank to five/27/79;
B changes from four/26/122 to four/31/141. Cost-driven trajectory differences are not
a strength or efficiency claim.

All 114 suite tests and 44 separate OFF raw/injected comparisons pass (17 defaults
plus five repair controls). The four targeted trace games and six local-mask games
are full, current engine1.0.3, injected, both-side Rows/fundamentals games and pass
both-team health; no broader panel or promotion is claimed. Do not retune binder
initialization or re-export masks on this evidence. A next source-bound discriminator
could test cached immutable group-spawn masks for current group visibility certification,
while retaining every live member read and the complete fixed menu. Exact sources,
roles, native continuation, stop samples, proofs and cleanup locators are in
[the bounded first-candidate result](../../results/population/models-first-candidate/index.json).

### Archipelago actual model control, 2026-10-23

Four fresh current RAW Ambush games compare the default with only
`PLAN4_MODEL_ENABLED=0`, both sides against frozen Fundamentals. Actual rosters,
complete replay archives and native statistics are identical by side: zero damage,
income, gems, banks or exhaustion; the subject stalls and the opponent is healthy.
The control retains spatial collection, draft, fixed ranking and executor. The
`enabled=0` evidence field means no applied private job, not that the default knob
was off. These games show no model-toggle contribution or private refusal cause.

`PLAN4_CALLER_RECEIPT=1` records actor0/R14--17 and flushes four numeric rows after
R18. Its whole gameplay replay and native statistics equal the same-run baseline.
R14 has a complete observation (12 groups, neither truncation flag), but only two
menu entries and zero evaluated candidates. Stamps around evaluation storage span
882 fuel (48,500->47,618); frame producers leave 32,937 before the unchanged 40k entry fence.
The earlier apparent evaluation debit therefore does not measure cold allocation
or private model cost. R15--17 pass the actual alive/unchanneled/any-action caller
predicate; inherited farming returns pending without moving, after entering near
77.5k and returning near 51.6k. No feasible productive candidate is proved.

`PLAN4_MOVEMENT_STORAGE_PREP=1` was the one mechanical trial: prepare the identical
immutable movement storage at a fresh >=90k boundary and yield that turn. It changes
no dynamic observation or existing movement/safety floor. All six baseline/repair/
repair-M0 Ambush games, both sides, still have zero work and identical statistics;
this rejects storage preparation as a sufficient repair. The two knobs default
off. At each source boundary, 44 separate raw/injected OFF comparisons pass for
all 17 defaults, four retained first-candidate recipes and the matched M0 control.
The additional `PLAN4_MOVEMENT_RECEIPT=1` packet reuses existing guard reads and
local values, with no extra SDK queries, for actor0/R15--17 in the failed storage
context. Its complete replay and statistics equal that control. R15 never calls
movement; R16/R17 have initialized storage, stable generation1 and the same 16-goal
digest, zero visits, and all 256 vertices still available. Prepared fuel is
34,713/33,266, below the unchanged 35k movement work fence. The uninstrumented
control already records R6 prepared33,950/status in-progress/zero visits. These
measured calls stop before search work; this is not a vertex-quota or cold-storage
fix. Instrumented fuel does not prove identical internal progress at every
uninstrumented callback. The packet defaults off, after the standard 44 byte
checks plus two exact failed-storage control checks. No second repair, promotion,
model admission or broader health claim follows. Next work must deliver complete
fresh movement masks/goals within the existing reserve, with measured costs;
cached wrapper counts alone do not prove savings. Exact control and cleanup evidence
is in [the actual control result](../../results/population/models-actual-control/index.json).

### Fresh movement-mask representation, 2026-10-23

`PLAN4_FRESH_DYNAMIC_GRID=1` was one representation trial proposed and challenged
by the read-only movement probe. A per-Bot map/owner-bound grid clears its complete
old positions list, takes ownership of the whole unchanged fresh export before
marking, and publishes only after the existing clock fence. An exclusive lease
keeps nested calls on the original dense path. Goals, SDK queries, model hooks,
BFS state and all safety/work reserves remain unchanged. Seven parity groups and
the extracted actual movement guard/action/hook tests pass, including exception
recovery and semantic same-stack clock changes. Retained-old plus fresh-vector
peak memory remains a limitation; fixtures do not prove arbitrary heap or real
fuel parity. All 17 defaults and six retained controls pass 46 separate OFF
raw/injected byte comparisons.

Both native arms fix the earlier storage-prep knob at 1, so applicability is
conditional on that warmed context. Four fresh RAW Ambush games (both sides,
frozen Fundamentals) still have zero sampled movement, damage, income, gems and
banks for all 20 actor-cell records. Baseline archives reproduce the prior failed
storage control; treatment gameplay replay and native statistics match baseline
by side. Subjects stall, opponents stay healthy, and there are no faults or fuel
exhaustions. Stored whole-turn fuel changes do not isolate an import/allocation
cost or establish useful work. No broader games or second repair followed.
The knob defaults off; this is a rejected utility trial, not a standalone default
repair, model contribution or promotion. Future work needs an actual source-bound
mask/export/preparation debit rather than inferring savings from wrapper or
allocation counts. Exact probe, sources, parity, controls and native evidence are
in [the fresh-mask result](../../results/population/models-fresh-mask-repair/index.json).

The subsequent work-admission check keeps that negative and all defaults intact.
One saved-native Ambush A scene (Vendetta actor0, R15, exact RAW executable alias
of `archipelago@6c5dbf7d8219`) observes none of the twelve public camp groups.
The public group2 Gold cluster reconstructs the actual sixteen movement goals
and digest3458768485. This binds the inherited farmer's destination, not a model
decision or permission to treat hidden stock as ready. The prior R15-17 caller
already reaches farming above77k, so changing the bootstrap60k return alone is
insufficient. Whole-turn fuel changes in the fresh-grid trial remain compatible
with identical zero-work trajectories.

The unchanged full production-caller fixture cannot yet be assembled: its exact
private entry state is absent, beginning with the recorder queue consulted by
`macro_tick -> attempt_flush`, followed by model, navigation and movement state.
This is an `UNKNOWN_INPUT` fixture binding, not a native stop at that function.
No empty/false private defaults, per-import fuel tape, new timing diagnostic or
gameplay repair is substituted. A proposed pre-first-contact shortcut is declined
because skipping evidence could suppress a supported history-based coupling.
Next obtain an exact private entry checkpoint or complete SDK-input replay from
constructor defaults, then compare the actual caller and direct farmer with
logical budgets permitted. The retained scene/assembly result is under
`.cache/arena/orchestration/mass-activation-next/models-work-admission/`; this
adds no model contribution, health clearance or admission.

### Native work-cost measurement, 2026-10-25

The existing FreshDynamicGrid trial **does save preparation cost**. Cached RAW
storage-only `b15386dc16b3` and storage+fresh-grid `97d009706f82` were measured on
Ambush, both sides against frozen Fundamentals. Separate host observers add no
SDK/engine queries. Their 19/28 focused checks and separate 80-key official CLI
parity comparisons pass. Each bounded observer reproduces its own complete game
replay, recorded fastrun statistics, stdout and both fuel streams. These tool
checks do not broaden the model experiment's one-map scope; neither candidate
runner is installed.

The source-bound 10,500 guest-fuel preparation interval falls to 646/679. For A0
at R16, total preparation falls from 19,079 to 9,996 and prepared fuel rises from
35,149 to 39,379. Arrival fuel and private context also differ; B's goal capacity
changes from 12 to 16 between modules, so this is not an isolated whole-turn
benefit. Enabled calls pass positive movement-work checks before reaching the
unchanged 35k fence. Checks alone do not reveal visits or completed search work.

Typed, bounded SDK outputs establish equal incoming goal and dynamic-blocker
arrays across R16–17 within `97d`, for A0 and B5. Their cleared origins are absent
from those blockers. The exact public static mask connects both origins to the
goals at distance 64. An SDK-free fixture using the exact kernel and captured
inputs reaches ready at calls 359/360 with 20 assumed successful work checks per
call; four checks still leave 5,944 visits pending after 2,000 calls, and assumed
used=256 permits zero BFS visits. These are logical allowances, not measured
native fuel, allowance, topology or progress.

Native persistent progress and available visits under the known same incoming
goals/blockers remain unmeasured: stored goals/generation, reset cursors,
frontier and allowance. No unconditional reset was found in source. Another
dense representation or a lower fuel fence is not justified. Defaults remain
off, and no model coupling, broad health clearance or Main win floor is claimed.

A bounded default-off continuation capsule passed its exact97d RAW baseline,
matched RAW/SDK code gate, focused fixture, six RAW keys and six native cases
with complete public replay/NativeStats parity. Its measured A-side matched
fuel prefix also passed. But all 12 native stdout streams lacked the literal
`mcV` marker, and no eligible actor0 phase row after R17 was observed. Capture,
overflow, output eligibility and private progress remain unknown; absence is
not proof of nonentry. The four source files were restored exactly, the two new
files moved to the Removed archive, and no 17-default matrix or adoption followed.

Next work must establish output-channel feasibility or an original host/private
state binding. Repeating this piggyback approach or lowering reserves is not
justified by the negative result.
The [compact work-cost record](../../results/population/models-work-cost/index.json)
binds exact sources, tools, closed experiments, scope and reproducible inputs.

Idea bank (unimplemented, worth keeping):
- Factorial crosses only after single couplings show decision differences: Chronicle x Commons, Habitat x Capital.
- Bulkheads: action-conditioned hostile altar-lock episodes as a second, separately controlled cause class.
- Repairman: moving or target-switching safe segments (v1 is stationary against one attackable target).
- Counterpart: a small rival-rule library chosen by witnessed behaviour, then transfer tests against unfamiliar opponents.
- Kernel: team-context attempt records (v1 is solo only) and kit/composition-conditioned bins.
- Quotient: a pair-class cache keyed on inter-camp geometry and intermediate bag/health.
- Timetable: turn-order-aware edge swaps and a longer horizon, still without reservations.
- Foundry: predict an ally's next target from Stewards' deterministic responsibility, with no messages.
- Metronome: drive refinement order from measured per-operation native fuel costs.
- Endgame: refine the max-round tie (hidden lifetime gold) as a separate objective follow-up, not a model.
