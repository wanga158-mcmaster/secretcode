# events: event-time economics of the PvE pipeline

## Doctrine
**Question.** For one champion working the PvE scoring pipeline (clear -> capture -> transport ->
tribute -> recovery), which event-time decision model turns its rounds into banked gems fastest?

**Shared hypothesis.** The game is a partially observed, deterministic semi-Markov decision
process. A choice is worth what it pays before the next decision event, timed in the engine's
exact phase order and counted with exact arithmetic: sequential float32 hit counts, integer hit
work and SDK-queried constants. Modelling event transitions correctly beats tuning coefficients.

**Evidence rules.** Use visible facts, observation timestamps and conditional bounds only: no
calibrated probabilities for unseen units, no opponent model, no opponent-specific tuning. A
change states a game-mechanism hypothesis that a fixture or a game can falsify on its own.
Structural models, action packing, information limits and event transitions come before score
coefficients. Comparator games evaluate a bot; they never tune it.

**Held fixed** for every bot:
- [common/src/runtime.h](common/src/runtime.h), the bounded runtime: B0 draft (Tavi, Varg,
  Vendetta, Vesper Vale, Marcella), Monster Strength x5 -> Jungle Health x5 -> Power Strength
  shop (Capital excepted), a legality recheck after every action, channel preservation, fuel
  reserves, owner-round epoch guards and an exception catch at the turn boundary. About 40
  compile-time Policy hooks (`requires` checks) switch on each bot's mechanism.
- [common/src/events.h](common/src/events.h): State and Candidate, the float32 hit counter, the
  Smith-ordered clear schedule, tribute ticks, the shortlist and the selector.
- [tempo/src/policy.h](tempo/src/policy.h): Tempo's score, bank and retreat rules are the base
  Policy of 12 bots. Only Harvest, Strata and Surveyor own their financial model.

**Varied:** exactly one decision model per bot (table below), kept in that bot's own `src/`.

**Neighbours.** `decision` varies the governing rule over whole jobs (value, uncertainty,
cooperation) on an analytic forecast; `models` varies the forecaster of how the game evolves.
`frontier` builds a separate organisation per formalism; `distributed`, `adaptive` and
`territory` design cooperation, responsibility and geography; `doctrines` designs bag and banking
economics around particular champions; `commitments` prices rival responses; `specialists` forks
B0 once per commitment. Events is a controlled comparison of single-actor scheduling with exact
arithmetic on one runtime; only Matching coordinates, through published commitments. A new
events bot is a new event, clock, deadline or work quantum for one champion, expressible as one
Policy over this runtime. Team roles, opponent responses, map regions, drafts and bag doctrines
belong to those suites.

**Engine facts the model relies on** (CLI 3.0.4, engine 1.0.2; [rules-notes](../../docs/rules-notes.md)):
- A round runs respawns, spell updates, captures, camp attacks, then champion turns. Each actor
  completes owner start, actions and owner end before the next starts; first-team priority
  alternates by round.
- Owner start resolves zero timers, so a tribute completes with the bag held at that start;
  owner end only decrements timers.
- Basic-hit HP follows repeated float32 subtraction (bit-exact over a native 9-hit chain at
  156.2 damage). A product formula can undercount: 649 HP at 129.8 damage needs 6 hits, not 5.
- An attack reset uses up the shared one-cast allowance. A sleeping injured camp keeps its exact HP.
- Under fixed damage, Smith's weighted-completion kill order matched an exhaustive oracle
  (15,015 cases); ranking by continuous work can be strictly worse.

## Bots
State is the bot's record in the last local 12-game matrix: both sides of 00collision and
00monolith against starter-cpp and anchors/fundamentals, and of 00collision against
territory/concentric and commitments/banked-vanguard (pre-restructure versions). The current
sources rebuild the measured module for 14 bots; Payday and Contingency changed since. It is
diagnostic only (two maps, no events-vs-events games). All listed losses were to fundamentals;
"older" lists loss cells of earlier modules of the same bot.

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| tempo | Two rates: gold pays at capture, gems only after transport and tribute; banks at bag 20, altar distance 3 or the deadline | 12/12; older: 00monolith A, B, 00collision A. Base of 12 bots |
| harvest | Renewal value: harvests left in the horizon, cycle = clear + capture + max(respawn, altar round trip); no gold-only camps once mature | 12/12; older: 00monolith A, B |
| strata | Partial camp damage is reusable progress: multi-visit clears pay extra travel and recovery, never regenerated HP | 12/12; older: 00monolith A, B |
| surveyor | Information budget: public screen of every camp, then a 3-slot detailed frontier with a rotating information cursor | 10/12: lost 00monolith A 460-1004 (85 of 139 captures gold-only), B 720-1012; older: 00collision A, B |
| geodesic | Cached terrain distance fields price camp entry, capture and altar transport; unreachable camps are excluded | 12/12; older: 00monolith A. Fields ignore units |
| quiescence | Depth-3 search over wait, basics, plain resets and direct damage under one shared cast; executes a completed lethal plan | 12/12; older: 00collision B. No enhanced resets or splash |
| matching | Exact 3x3 job assignment (bitmask DP, private waits) over published, expiring 8-byte commitments | 10/12: lost 00collision A 840-1010, 00monolith A 836-1020; older: both B sides too |
| overlap | Action packing: parallel allowances and the current phase give elapsed-round clocks; tribute starts on arrival and can overlap capture | Experimental `1136f12c86d3`: focused checks 7/7; arrival packing logged on both Hooks sides, capture-tribute unwitnessed; Lost World round-1 fuel exhaustion blocks admission (population evidence below) |
| refuge | Withdraws by next-event loss and distance to a recall-safe region (up to 32 threats, 9 tiles) | 12/12; older: 00monolith B |
| capital | Buys equipment bundles only when they cross integer kill-work or survival thresholds on 3 representative jobs; waiting wins ties | 12/12; an older module exhausted fuel after a purchase |
| itinerary | One- and two-camp routes: gold pays at each capture, all gems at one terminal bank | 12/12; older: 00monolith B |
| reserve | Integer expedition capacity: clears that fit current health, plus an amortized recovery cost per clear | 12/12; closest: 00monolith A 1024-990 |
| terminal | Plays to the team's remaining winning-gem target: earliest winning bank event, surplus reward clipped | 12/12; older: 00collision A |
| payday | Capture-gold and bank-gem deadlines checked separately: only rewards paid before the round limit count | Unmeasured; older: 00collision A 933-1002 |
| contingency | Minimax regret over the baseline plus one extra camp attack or capture event per job; explicit private wait | Unmeasured; older 12/12 |
| triage | Three-phase basic-target schedules minimising imminent camp exposure; tributes a winning bag before a full-health recall | 12/12; older: 00monolith B. No logged target has differed from the baseline |

## Distinctness rules
- A bot counts only through a reliable physical interaction not adequately covered by another
  retained bot. A production fixture differing from Tempo (or its nearest sibling) identifies
  a candidate; native action, continuation/refusal and matched replacement evidence establish
  the contribution. Outcomes and `arena ladder styles` are triage, not admission or retirement
  criteria. A healthy losing, unusual interaction remains useful.
- One mechanism per bot, in its own `src/`, switched on by Policy hooks. Never merge one bot's
  mechanism into another: a combination is a new bot with its own hypothesis. Investigate an
  apparently redundant line before redesigning or parking it with a reopening condition.
- Shared modules are `common/` (all 16 bots) and `tempo/src/policy.h` (13 bots). They have one
  owner per suite at a time. Shared edits are limited to legality, channel preservation, bounded
  execution, exception handling and faithful engine arithmetic. A shared edit changes every bot
  that includes it: re-test the whole suite (`arena test events`, `arena build events`) and treat
  every affected bot's games as stale.
- A bot-only change must leave the other bots' versions unchanged: compare
  `arena bots events --versions` before and after.

## Layout and checks
```
common/src/           runtime.h (turn loop, Policy hooks), events.h, event_kernel.h (channel,
                      capture and allowance transitions), routes.h, focus_caller.h,
                      planning_epoch.h, turn_completion.h, initialization.h, public_work.h
tempo/src/policy.h    base Policy (score, bank, retreat) of 12 bots
<bot>/src/            main.cpp = p11_runtime::run<p11_<bot>::Policy>(), policy.h, mechanism headers
tests/plan11-*.cpp    37 WASI fixtures: model logic, independent oracles, shared caller paths
tests/run.py          the single test entry
```
- `./arena.cmd test events` runs 85 cases: the 37 fixtures, `syntax-<bot>`,
  `syntax-bounded-init-<bot>` with the staged initializer enabled, and
  `syntax-public-work-<bot>` with initialization and exact public-work caching enabled. These are
  `-Wall -Wextra -Werror` SDK syntax checks of each main.cpp, the only checks besides the build
  that compiles runtime.h. `./arena.cmd test events/<bot>` runs the cases whose includes reach
  that bot's `src/` plus the runtime-only fixtures; naming tempo also re-runs its 12 heirs'
  cases. Directly: `./arena.cmd test events -- [bot ...] [--case NAME] [--no-syntax]
  [--list]`; `--list` shows what each case covers.
- capital-work-reuse and payday-work-reuse are independent oracles of the current Capital and
  Payday models: update them only when changing those models on purpose.
- `./arena.cmd build events` builds release snapshots of all 16 bots. There are no suite tools.
- Builds and fixtures do not prove fuel safety. Games print bounded telemetry (`p11scan`,
  `p11clear`, `p11channel`, `p11match`, `p11micro`, `p11triage`, ...) and `p11 error:` for an
  exception caught at the turn boundary. Identifiers keep the `p11` prefix (`p11_<bot>`).

## Leads
Open problems and next steps:
- Production gap to B0: all listed losses were to fundamentals, mostly on 00monolith, and the
  diagnosed ones (Surveyor, Matching) fell short on collected gems, not on banking; Surveyor's
  gold-only captures went on after equipment maturity. Next: a falsifiable gold-stop hypothesis.
- Coverage: assess all 16 over the current 40-map pool, confirm on fresh conditions, and read
  `arena ladder styles` for converged pairs; 16 bots on one runtime are correlated entries.
- Payday and Contingency have no games for their current modules. Payday's older loss: round-1
  shortlist work left too little fuel for a detailed job; rounds 2-3 had jobs but no actions.
- Triage plans nonempty multi-target frontiers in games, but no logged choice has differed from
  the baseline target and the fuel reserve gate cut most logged opportunities: show a native
  deviation or retire the scheduler.
- Overlap: distinguish static altar adjacency from a live old-bag capture opportunity; its
  ordinary banking policy may consume the bag before the actor reaches the adjacent spawn.
  First fix or isolate the shared round-1 initialization fuel gap on Lost World.
- Matching: native board publication and expiry are unproven, the board has no compare-and-swap,
  and planning can leave too little fuel for the move after assignment.
- Tempo coupling: a change to tempo/src/policy.h moves 13 bots. Moving the base Policy into
  `common/` with byte-identical builds would let Tempo evolve alone.
- The hit recurrence is verified for basic hits only, not for spells, shields, splash or variable damage.

Idea bank (unimplemented designs worth keeping):
- Subset-DP itinerary over up to 5 camps with timers, bag and upgrades in the state (O(k^2 2^k)).
- Min-cost assignment with dummy waits beyond 3x3 (a fixed-cardinality cost shift keeps the optimum).
- Beam search to event boundaries with width/depth caps and deduplication, keeping a completed incumbent.
- Rollout improvement of Tempo truncated at the next bank event, rechecking the observation round.
- Duration-aware SMDP value: rewards during an activity plus gamma^tau V(next), replacing rate surrogates.
- Transposition cache: immutable terrain apart from timestamped observations; cold/warm and forced-collision fixtures.
- Explicit null move: every selector also scores waiting, since acting need not beat yielding.
- Planning state that includes pending zero channels, consumed casts and capture-history gaps.
- Fuel-aware anytime planning that reserves the move after the plan.
- Observation-equivalence fixtures: worlds differing only in hidden state must get identical decisions.
- Gold as threshold capital: gold is worth nothing once no further kill or survival threshold fits the horizon.

## Population evidence and next decision

The executable intent inventory is [tests/population.py](tests/population.py). It describes all
16 current bots conservatively; missing native evidence and full-pool health remain gaps rather
than inferred admissions. Its Overlap observer extracts production-log candidates and does not
certify their downstream physical sequence.

**Observed, 2026-10-21:** Overlap `1136f12c86d3` passed all seven checks selected by
`arena test events/overlap` (including the production policy selector, conditional clock and
refusal cases, shared event kernel, and runtime syntax). Static geometry matches all 40 current
map hashes: 43 camp-spawn/altar pairs at range one occur on nine maps, among 836 groups and
2,741 members. Adjacency is an opportunity prerequisite, not a live trigger count. The native
panel in [population-events-overlap-opportunity](../../.cache/arena/runs/population-events-overlap-opportunity/games.jsonl)
is four full-length games: Lost World and Hooks, both sides, against starter-cpp
`3785135fc7c7`. No `p11overlap` candidate appeared. Hooks was healthy on both sides and emitted
57/53 `p11arrival` events; these support investigating arrival packing separately, not claiming
capture overlap. Lost World exhausted each of five champions once in round 1 on both sides,
and side B also had zero clears. No faults or logged errors occurred. The 3-1 outcome does not
resolve the behavior or health gaps.

**Interpretation:** the unbounded shared initialization loop (`common/src/runtime.h`) is the
first fuel suspect: Lost World has 34 groups/78 members, Hooks 12/28; all exhaustion precedes
ordinary turns, whose observed fuel maxima stayed below 81,000. This narrows a shared-runtime
repair experiment, but does not identify the exact instruction that exhausted. Separately,
Tempo's bank rule starts returning with any old bag within adjusted altar distance three;
`move_to` takes one ordinary step. An ordinary approach therefore tends to bank before reaching
a spawn at tribute range. That may suppress the capture-overlap gate despite static adjacency.
Forced movement, gem theft on a held spawn, or another reachable sequence could change that
interpretation; absence of four witnesses does not prove the mechanism unreachable.

**Next decisive checks:** a shared-module owner should bound initialization and recheck all
dependent events bots without changing their policies. Then construct an actual old-bag,
altar-adjacent capture opportunity through the production executor and inspect native channel,
capture and bank timing against Tempo on identical conditions. A valid native sequence would
reopen capture-overlap admission; a proved banking obstruction calls for a narrowly scoped
activation design. Do not replace it with generic farming or count helper-only success as a
native witness. Full-pool health, a healthy cross-suite panel, physical distinction, refusal
and fresh applicability are still required.

**Bounded runtime experiment:** `P11_BOUNDED_INITIALIZATION=1` stages immutable public metadata
with explicit fuel reserves and yields the turn that completes setup; the default remains zero.
The original initialization and caller remain compiled unchanged when disabled. All 16 bots
built, and all 67 suite checks passed, including enabled-path syntax for every bot. Overlap's
default-off `4e4eb7022374` WASM is byte-identical to original `1136f12c86d3`; the native replay
histories are also bit-identical on all four matched original cases.

The [paired native evidence](../../results/population/events-runtime/initialization.json)
compares that default-off executable against enabled `41e014f32593` on the same two maps, sides
and starter snapshot (eight full-length games). Exhaustion fell from ten to zero, with no faults
or error lines in either arm. Lost World side B still stalled, and neither arm activated capture
overlap. This is a narrowed runtime result, not bot admission. On Lost World the enabled setup
completed in round 1 using 169,332–169,344 fuel and planning began in round 2; the old path
exhausted its 200,000 first-turn fuel and first logged planning in round 3. The evidence therefore
implicates **combined setup and same-turn gameplay work**, rather than proving setup alone
exceeds the budget. The successful initializer has not yet exercised a multi-round resume;
a forced low-fuel production-driver fixture is the next discriminating check. Isolating setup
with a single voluntary yield may also be sufficient on current maps. The staged variant remains
off, with its startup scheduling cost explicit and the unchanged Overlap policy preserved.

**Resumption follow-up, 2026-10-22:** [initialization.h](common/src/initialization.h) now owns
the production reserve/order driver; runtime callbacks still own the immutable metadata and
existing cursors. [plan11-initialization.cpp](tests/plan11-initialization.cpp) forces voluntary
yields before setup, group opening, first and middle members, the next group, group closing,
and final readiness. It compares staged and uninterrupted metadata, stable group indexes and
cache rows, checks empty input, and rejects repeated writes after completion. Review found that
the final member could leave too little fuel for closing: a separate 5,000-fuel close guard now
preserves the open group at member-end. A 14,500-fuel member fixture leaves 500 fuel and proves
closing resumes later without duplicating the member. Other reserves are 60,000 for setup,
30,000 for opening a group, 15,000 for a member, and 25,000 for finalization.

The [final driver evidence](../../results/population/events-runtime/resume-driver.json) records
68/68 suite checks and 16/16 builds. Default-off `4ebb86c57028` still compiles to the original
WASM bytes. The enabled `d4d0c73aad09` was validated in four full-length games on the same two
maps, both sides, against the same starter snapshot: zero exhaustion, faults or error lines;
Lost World side B still stalls and capture-overlap still has no log candidate. An intermediate
extraction's four games remain historical evidence and do not substitute for this final version.
The default remains off. Driver fixtures establish safe voluntary cursor resumption only under
their atomic callback bounds; map enumeration and each group's spawn-mask construction remain
monolithic SDK work requiring native coverage. In particular, this is not full-pool admission,
and startup success does not repair the later detailed-planning fuel gate or establish Overlap's
claimed capture/tribute interaction.

**Full-pool health and public-work cache, 2026-10-22:** the
[paired startup assessment](../../results/population/events-health/pool-initialization.json)
covers all 40 current pool maps, both sides, against starter-cpp `3785135fc7c7`: 80 games per
arm, snapshots `4ebb86c57028` and initialization-enabled `d4d0c73aad09`. Subject exhaustion
falls from 88 to zero; neither arm has a subject fault or error line. The same 14 zero-clear
stalls remain, so initialization alone does not clear health. Separately, starter has six
exhaustions, one error line and four stalls per arm. This is a declared frozen comparison,
not a healthy cross-suite panel. Neither arm emits a capture-overlap candidate.

The [native stage diagnosis](../../results/population/events-health/stage-diagnostic.json)
localizes Lost World B's remaining obstruction: 163/164 sampled scans produce no detailed
candidate, with every sample reporting a cutoff. Mean public preview work costs 28,711 fuel
and ranking 15,298; median deficit before the unchanged 50,000-fuel detail guard is 2,867.
These cadence samples cover five identities and rounds 2–1986, not every turn or live trigger.
An empty candidate list here reflects execution starvation, not evidence that the doctrine
finds every job infeasible.

`P11_CACHE_PUBLIC_WORK=1` tests an exact computation saving: each group caches only
`attacks(public_health, effective_damage)`, keyed by both float bit patterns. Position, live
camp facts, economic scores, candidate ordering, the detail reserve and executor are unchanged.
The [production cache fixture](tests/plan11-public-work.cpp) checks warm hits, bitwise
invalidation, nonfinite/signed-zero inputs, failed-computation retention, and equal production
shortlists and choices for Tempo and Overlap across 128 frames of 34 candidates. All 85 suite
checks and 16 builds pass. The all-off executable `1df01225d2d3` remains byte-identical to the
original, and cache-off initialization control `265c9babff2e` matches the previously measured
enabled initializer's bytes.

The [same-run targeted comparison](../../results/population/events-health/public-work-cache.json)
plays that initialization control against cache-enabled `9c6a3439c25e` on Lost World and Hooks,
both sides, with the same starter snapshot (eight full-length games). Both arms have no faults,
exhaustion or error lines. Lost World B's clears rise from zero to 25 and its stall disappears;
zero-modeled cadence samples fall from 163/164 to 6/158, with mean detail-entry fuel rising
from 47,562 to 54,299 across the existing guard. Lost World A clears rise from two to ten.
Hooks' outcomes and contribution totals match on both sides. Changed trajectories make sampled
mean fuel descriptive rather than a comparison of identical turn states; production value
equivalence and native gate crossing are the discriminating evidence. Capture-overlap remains
unwitnessed. Both knobs stay default-off.

The [full-pool cache assessment](../../results/population/events-health/pool-public-work-cache.json)
then finds 14 stalls without the cache and eight with it, with clears 5,991 to 7,315 across 80
games per arm. The cache arm has one exhaustion on Big World B (Vendetta, round 514), despite
no fault or error line. Its successful move is physically observed at the exhaustion input;
the continuation then queries fresh camps before the existing combat guard. This localizes a
shared execution boundary; it does not identify the exact exhausted instruction or show that
memoized arithmetic caused the fault. The remaining stalls are both sides of Lanes, Road of
Riches, Islands and Yellow Brick Road. Frozen starter health remains separate and unhealthy.
No stall flag has been waived and no variant is admitted.

**Movement continuation and independent arrival packing, 2026-10-22:**
`P11_MOVE_FUEL_GUARD` remains default-off. It reserves fuel before movement setup and every
retry, checks the owner-round epoch after expensive queries, and yields before the successful
farm move's vision refresh or per-target queries when fuel is insufficient. A tagged yield
unwinds to the existing turn boundary, preserving the completed move and starting fresh next
owner turn; it neither logs an engine error nor marks a camp unavailable. The reserves assume
bounded atomic SDK work and are not a proof that every native operation fits. Actual policies,
economic rules, the 50,000-fuel detail gate and ordinary blocked-camp handling are unchanged.

The [actual movement caller fixture](tests/plan11-move-fuel.cpp) forces low fuel immediately
after a successful move and proves no fresh-vision query or attack follows. It also exercises
setup, retry and target boundary equality/refusal, the three-attempt cap, stale epochs, next-turn
reentry and absence of unavailable-camp mutation on a yield. All 103 suite checks and all 16
default builds pass. All-off bytes equal the original executable; initialization-only bytes
equal the earlier driver, and initialization-plus-cache with the new guard off equals the
previous cache executable. The matched Big World B reproduction has one exhaustion and 75
clears in cache control `77e555aa48b4`, versus zero exhaustion and 66 clears with guard
`c9c617971319`, with neither arm showing faults, errors or a zero-clear stall. Earlier yields
change trajectories; full-pool health must judge their consequences before any enablement.

The [final paired full-pool guard assessment](../../results/population/events-health/pool-movement-continuation.json)
covers all 40 maps and both sides with 80 games per arm. Guarded execution has zero subject
faults, exhaustion and error lines, versus one exhaustion in cache control. The same eight
stalls remain and total clears decline from 7,315 to 7,013. This verifies removal of the measured
fuel failure without adding a zero-clear stall; it does not clear health, justify default
enablement, or certify another bot sharing the module. All seven native archive entries of
the unchanged control's Big World B game match its historical cache witness byte for byte.

Overlap's declared `packed_clock` hook supplies a separate contribution:
[arrival-bank evidence](../../results/population/events-health/arrival-bank.json) physically
shows Hooks A, Varg actor 1, walking from (15,13) to (14,14) and starting tribute in owner round
219. Native channel timers are two and one at owner starts 220 and 221; completion in 222
removes the 22-gem bag, raises team submitted gems from 33 to 55, and the actor moves again in
that completion round. The [production bank member](common/src/bank_caller.inc) is extracted
verbatim so the [caller fixture](tests/plan11-bank-caller.cpp) executes actual Overlap and Tempo
gates from that same observed arrival state: Overlap completes/resumes in 222, Tempo in 223.
The fixture also refuses an ineligible small bag, blocked/immobile movement, stale round, low
fuel, empty bag, missing altar, out-of-range arrival and enemy locks. Its deterministic SDK
movement and event-kernel progression are not an exact-state native Tempo counterfactual.

The [native fixed-input timing comparison](../../results/population/events-branch/comparison.json)
now supplies the missing native timing experiment. Identical Hooks A forks at actor 1's owner
turn in round 219 execute the same legal move into altar range. Submit-now banks 22 gems at
owner start 222; deferring submission until owner start 220 banks them at 223. All 74 checks
pass, including illegal pre-arrival submission refusal and 67 exact retained reference inputs.
Reference equality excludes only the timeline handle's bytes [48,56) in the native snapshot;
the deferred arm is aligned by round, owner and phase, with every native action checked.
The [reproduction](../../results/population/events-branch/run.py) pins its
[branch case](../../results/population/events-branch/case.mjs), decoder and source dependencies.

This result establishes one owner turn of native timing benefit under the retained peer-input
schedule. It does not execute either guest policy or re-execute peer policies after the changed
state. After the now-arm completes, a separate reference fork at input 3673 validates later
peer inputs; selected historical attack/cast/move inputs 3670 through 3673 are not executed in
either intervention arm. The original replay separately shows resumed work, but the branch
comparison does not establish guest continuation or its fuel behavior. Re-executed policies,
interruptions or survival could change the practical benefit.

The separate contract `events/overlap-arrival-bank` therefore records observed native
activation and continuation, a production policy distinction, and the bounded native timing
comparison. Full guest/policy comparison, fresh applicability and health remain gaps: the
guarded executable still has eight stalls, and the historical arrival witness does not certify
those changed bytes. The original capture/tribute-overlap promise remains separate, with zero
candidates in both full-pool arms. A failed native status or reference equality check would
invalidate this timing experiment; failure to complete or resume under actual guest execution
would refute the claimed reliable contribution. The next useful work follows these gaps.

**Retained Lanes A audit, 2026-10-22:** the
[pinned native audit](../../results/population/events-stall/classification.json) examines
`c9c617971319` without new games. Across all 2,000 rounds and 359,433 native substeps, all five
allies retain their initial positions, HP and empty purses/bags; statistics record no damage,
income, collection or submission. This is no measured physical pipeline progress, rather than
useful combat hidden by a zero-clear count. Eight cadence- and fuel-censored late scan records
show zero detailed candidates at the unchanged 50,000-fuel gate, with three planned groups and
about 41,400–42,000 fuel. They do not establish that an admissible early job was lost.

Marcella sees none of the 42 camps in owner starts 2–3; the pending group in her round-1927
cutoff has never been personally visible. Absence of visibility does not justify refusing an
expedition: this caller intentionally models unseen camps from public metadata. Conversely,
the late horizon may legitimately reject a pipeline. Classification therefore stops at a
source-observation gap: capture one early post-initialization farm boundary with its actual
fuel, frontier, model/state and production feasibility/finite-score result. A source-permitted
expedition lost before detail would support execution loss; a passed gate followed by an
infeasible or terminally late job would redirect the investigation. No shared repair or reserve
reduction follows from the present evidence, and the action-packing commitment remains intact.

**Early expedition boundary, 2026-10-22:** the default-off
`P11_EXPEDITION_RECEIPT` retains one bounded packet at the first captured Marcella detail
boundary through round 8, then emits it next owner turn. It reuses the existing fuel predicate,
preview and shortlist evaluations; it adds no SDK or policy query. Attempt reset and partial
output refusal are covered by the fixture. All 120 suite checks pass, all 16 default raw and
injected binaries remain identical, and the three-knob control matches `c9c617971319` exactly.

The [early assessment](../../results/population/events-expedition/assessment-final.json)
records actor 4 in Lanes A, round 3. Group 14 is personally invisible; its actual preview is
feasible and first in the finite shortlist. Reconstructing the unchanged public-model detailed
source, including action packing and ally count, gives six clear turns, zero modeled loss and
finite value 2.35294117647. The actual diagnostic detail gate refuses with 9,721 fuel against
50,000. This fuel value belongs to the diagnostic executable. A from-start comparison matches
730 full spectator-state and timeline snapshots through the preflush boundary, masking only
the timeline handle; it proves neither guest private-state nor fuel equality.

A static terrain route exists, and the [corrected native branch probe](../../results/population/events-expedition/route-probe-full.json)
matches both full exposed layouts at round 3, input 76, substep 7 before an injected engine move
from (3,13) to (4,13) succeeds. The initial timeline-only probe is retained separately. These
are two same-endpoint in-memory forks with one injected move each, not guest execution or a
completed dynamic route. Both games in the observation pair have zero technical failures and
remain zero-clear stalls; no health or qualification gain follows.

This narrows the terminal-only ambiguity to an early source-model opportunity, with execution
cost location and complete route behavior still unresolved. The packet's bounded pre-gate
work is inventoried, not assigned a formal fuel bound. Repeated immutable altar-distance work
(42 groups by 18 altar tiles here) is a faithful cache experiment candidate, not an attributed
fuel cause. A next bounded experiment can precompute exactly those same minima inside the
existing resumable catalog initialization, preserving scores, order and the 50,000 gate, or
retain the caller's already computed stage stamps to distinguish earlier loss. Either requires
a separate reviewed boundary and preservation evidence. No repair or default enablement is
part of this result; the original capture/tribute gap and eight full-pool stalls remain.

**Stage discriminator not observed, 2026-10-22:** the
[bounded stage proposal](../../results/population/events-stages/assessment.json) adds no native
evidence. Its first boundary and one permitted structural restoration both preserve all 16
default binaries and the repaired `c9c617971319` executable, but change the legacy receipt
binary with the stage knob off. The failed equality blocks the planned same-run stage-only
pair. No games, prefix comparison or stage measurements were run; no cost interval or cache
sufficiency follows from the 137 suite, 29 extracted-caller and 17 parser checks.

Both rejected revisions and their fixtures are retained as historical UTF-8 bodies. The
accepted Claim 21 source/test boundary is restored exactly, and the three new proposal files
are moved to the repository's removal directory. The current default, repaired control and
legacy receipt resolve to `81044afa0e1e`, `3b155e0c15ed` and `92d8cbf00982`, with their accepted
cached raw/injected hashes. This record changes only the suite prose. Reopen with an observer
design that first preserves those binary controls, then obtains one complete stage tuple and
the full exposed-world prefix through its exact preflush boundary. Early cost location and
the faithful geometric-cache hypothesis remain unresolved; no repair or health waiver follows.

**Exact altar-minimum sufficiency, 2026-10-22:** default-off
`P11_CACHE_ALTAR_MINIMUM` stores only the original minimum of Chebyshev distance from the
native first-member spawn anchor to immutable altar tiles, minus the queried submission range,
clamped at zero and capped by the original 10,000 sentinel. Bounded initialization processes
one altar per existing member-work guard after the real members; it publishes no partial
group. These steps add intentional startup fuel checks. Legacy or unfinished construction uses
the old formula. Travel, live observations, geodesic overrides, financial rules and action
packing are unchanged. The 15,000-fuel step guard is not a universal atomic-fuel proof.

All 156 checks pass. Fixtures cover all 40 maps and every possible member anchor, exact saved
production candidates, score bits, ties and choices at equal supplied allowance, plus actual
initializer callbacks and resumption. All 16 default raw/injected binaries, the repaired control
and the legacy receipt retain their accepted bytes.

The [matched Lanes A result](../../results/population/events-altar-cache/assessment.json)
compares the three-repair control with the same executable recipe plus this cache against
frozen starter, without a receipt or stage probe. Control has zero production and remains a
stall; cache has 94 clear statistics, 4,514 gold and 567 submitted gems in the team statistic, with zero subject
faults, exhaustion, errors or deaths in both arms. The control's seven archive entries match
the historical witness. Native inspection separately records 91 completed allied payouts in
all four quarters, from round 42 through 1978, and a position transition in round 5. Clear and
payout counts measure different events. Individual channel starts and completions remain unverified; the round-42
gold paid to two actors is not an agreement test for Marcella's earlier single-actor model.

Exact initialization completion, foregone startup opportunities and fuel-stage location were
not measured. This one map/side/opponent supports testing the remaining known failure
conditions; it establishes neither wider health nor admission. No further maps are run in this
claim, the knob stays off, and the original capture/tribute-overlap gap remains separate.

**Known-stall follow-up, 2026-10-22:** the unchanged cache removes five of the seven
remaining stalls in the [fixed paired panel](../../results/population/events-altar-known/assessment.json):
Lanes B has 102 clear statistics, Road A/B 4/3, and Islands A/B 33/3. Both Yellow Brick Road
sides still have zero clears, earned gold and submitted gems. Big World B retains zero subject
faults, exhaustion and errors, with 68 clears versus 66 control; its three cache deaths versus
zero control are a separate descriptive cost. Every subject game in both arms has zero
technical failures. Starter retains six exhaustions and one error per arm; these remain blocking.

All 16 new games complete the normal 2,000 rounds. Eight controls reproduce all historical
statistics files and 56 archive payloads exactly. Lanes A was retained, not rerun: the combined
scope is nine distinct conditions and 18 games against one frozen opponent. Unchanged source,
test and binary pins reuse the 156 checks and disabled-feature proofs; no builds or tests repeat.
An offline validator initially compared statistics placeholders with replay-derived fuel/error
fields. Its report remains preserved; separate validation checks each field against its actual
source without rerunning games.

The cache is insufficient for the two Yellow stalls. Existing sparse guest logs already show
some detailed Candidate work there, so a universal first-detail-gate diagnosis is unsupported.
The next useful question is one early source-permitted opportunity and its actual selection,
refusal or execution conditions. No new logger, native query or repair is part of this result.
Broader health, fresh applicability and the original action-packing witness remain unresolved;
the cache stays off and no qualification follows.

**Fireflies cross-suite health, 2026-10-22:** the
[exact cached-Overlap/repaired-Fireflies pair](../../results/population/cross-suite/fireflies-overlap-cache-final.json)
completes all 40 pool maps on both sides. Both teams have zero faults, errors, exhaustion and
suspension throughout the 80-game panel. Fireflies has no zero-clear stalls; Overlap still stalls on
both Road of Riches sides. The full pair therefore fails health qualification. Productive
Yellow games against this opponent do not erase the retained Starter failures or establish
Overlap's capture/tribute mechanism.

The two Yellow games were reused exactly, adding 78 games in a new output folder while all
original statistics, replay payloads and log descriptors remain unchanged. The original
completion checker incorrectly required 2,000 completed rounds; the corrected offline gate
accepts the rules' immediate gem-target wins while preserving every technical and positive-clear
requirement. Seventeen focused completion/failure cases cover that gate. Default settings,
source recipes, applied snapshots, binaries, maps and engine remain pinned; the native outcome
is authoritative for a complete round-limit game, with no win or strength claim.

The remaining question is why Road produces no completed clear against Fireflies, or which
other independent pairing can meet the full health bar. Reopen with a source-bound opportunity,
refusal or execution witness, or a fresh complete healthy pairing. No repair, decoder query,
default enablement or admission is part of this result.

**Road owner-start motion, 2026-10-22:** four
[fixed native observation windows](../../results/population/overlap-road-motion/aggregate.json)
cover rounds 1–64 on both sides and the final 64 complete rounds: A 1017–1080, B 1023–1086.
All 1,280 samples have positive health and valid coordinates. Each of the ten actor/replay
identities has one unchanged observed coordinate across both windows, with unchanged health
and zero observed gold/gems. These are live stationary owner-start samples; the tool has no
explicit alive bit. Within-round returns and movement in the unobserved middle remain possible.

This favors a source-bound caller/guard and admissible-opportunity check before assuming ongoing
route execution. Position alone identifies no budget, model, policy or execution cause. A
verified transition outside these samples, or an actual caller refusal/admissibility witness,
could change that work selection. The existing zero-clear flags and qualification failure remain.

The first call stopped on my map-label validation mistake. The tool's documented exact
`00road-of-riches` → `00road_of_riches` geometry alias was corrected offline; the retained output
was revalidated before the remaining three fixed calls. All original bytes remain preserved.
There were four observer calls, no reruns, games, source/helper changes or further native work.
No packing attribution, repair or admission follows.

**Road saved caller evidence, 2026-10-22:** the existing guest stdout already supplies
[166 source-bound farm witnesses](../../results/population/overlap-road-log/assessment.json),
83 per side, joined by SDK champion name to all five native owners on each side. Every sample
has a three-entry finite-screen frontier, then refuses the unchanged first 50k detail gate with
zero scanned or modeled candidates. Initialization is complete by each owner's first logged
farm, not necessarily on that round. No additional guest logger is needed for this boundary.

The preview-to-sort interval consumes 21,523–21,641 fuel in these samples, including shortlist
setup, ranking and promotions; it is not a score-helper tariff. The subsequent interval to scan
start consumes 1,147. `screened=51` counts group-loop visits before skips, not score calls.
The actual shortlist is already linear with one screen evaluation per supplied Candidate, so
repeated-score memoization and replacing a supposed full sort have no demonstrated redundancy.

These censored logs do not cover every turn or establish a safe detailed job, chooser result,
navigation failure, sufficient repair saving or action packing. No detailed Candidate is
constructed in these sampled scan loops; visible-model preparation precedes scan start.
A separately verified compiler-work saving could justify a bounded unchanged-
policy experiment; absent that evidence, preserve the source menu and reserves. Both Road
zero-clear health failures remain. The standalone saved-byte producer retains every tuple,
source/payload pin and owner join, accepts a new output path and refuses existing outputs.
No source, test, build, game, evaluator or native-query work accompanies this record.

**Shortlist compiler-hint boundary, 2026-10-22:** the
[single bot-local proposal](../../results/population/overlap-inline-shortlist/assessment.json)
stops at disabled-byte preservation. With `P11_OVERLAP_INLINE_SHORTLIST=0`, the default
raw/injected build matches its incumbent, but the four-flag build produces `349405f6…`
instead of accepted `14c3ca02…`. This fails the required bar; no source-layout retry or
semantic-equivalence exception follows. The proposal's knob defaults off.

The extracted actual frontier-and-promotion caller passes 34,440 supplied cases per arm,
including 1,300 score ties, with identical order/score/choice digests. That fixture does not
waive binary preservation or establish Road feasibility or fuel savings. No hint-enabled
whole-bot build, further focused checks, static specialization claim or native experiment
follows this failure. Reopen only with a separately authorized preservation approach; retain
the existing source models, reserves, action-packing doctrine and Road health failures.

**Triage catalog caller, 2026-10-22:** the
[source-extracted production caller](../../results/population/triage-catalog-caller/assessment.json)
carries its choice through target arbitration and `fight()` to supplied-host actions. At
damage 451, Triage attacks Wolf35 three times while Tempo attacks Elk33 three times;
the conditional three-phase exposure sums are 220 versus 240. At damage 55, Triage attacks
Gold26 once, then fresh observations select Wolf22 twice. Gold remains alive, exposure is
60 in both arms, and the scheduler has not committed a three-hit sequence.

Twenty supplied scenarios cover downstream fuel, epoch, mask, allowance, reset and offensive
spell precedence/refusal. The selected group and observations are supplied, SDK objects are
doubles, and navigation/clear completion are stubs. These are caller witnesses, not native
opportunities, actor damage, capture or banking evidence. Two failed expectations that required
Gold on all three turns remain retained; only the fixture expectation changed.

The [native prerequisite plan](../../results/population/triage-catalog-caller/native-plan.json)
now has a supported lab witness on the historical CLI/SDK 3.0.4, engine 1.0.2 bundle: Tavi0's
first slot-1 CC targets Gold2, whose HP changes 120 → 0 and lifecycle asleep → dead; Wolf0
remains at 1,350 HP. This is an actual spell interaction, not the forecasted three-basic sequence.
Matched Tempo full games have completed, but control002 observation is held during the
bundle transition. The [comparison evidence index](../../results/population/triage-tempo-native/index.json)
retains a partial package with historical dependency gaps; an unexecuted planner baseline does
not establish control distinctness.

The current bundle is CLI/SDK 3.0.5, engine 1.0.3. The altar-map fixture now checks geometry
schema, engine and content hash against pinned current-version metadata while retaining all
map/coordinate assertions. Its targeted metadata check is separate from WASI/native tests;
the new bundle needs fresh qualification. Historical native evidence does not qualify it.
The gameplay source remains `a74ba55348de`; no capture/bank or admission claim follows.

**Triage current-bundle lab contrast, 2026-10-22:** the
[current paired evidence](../../results/population/current-panels/index.json) records two clean
2,000-round games on one lab map, side A, under CLI/SDK 3.0.5 and engine 1.0.3. All 40,152 bytes
of the first-owner public timeline snapshots match without masking, and the named public
prerequisites match. Triage's first CC kills Gold (120 → 0), with no observed basic in the
bounded Tavi owner prefix. Tempo casts on Wolf (1,350 → 1,196), then basics it (1,196 → 1,151).
This establishes an executed target contrast against Tempo. The earlier 1.0.2/control002 gap
remains part of its historical package.

The different use of the basic allowance is the next uncertainty. Preserve the target
commitment while investigating that interaction; a spell kill is not a completed three-basic
schedule or a modeled exposure improvement. Public-frame equality does not prove private-fuel
equality. Ordinary-map reachability, capture/bank, strength and admission remain unproved.
No gameplay source changes follow from this record.

**Triage optional post-cast continuation, 2026-10-22:**
`P11_TRIAGE_POSTCAST_BASIC` remains off by default. The optional Tavi-only path preserves
the original Gold-first fight and dead-target guard, then permits at most one fresh same-group
basic after the existing clear/move attempt, subject to fresh epoch and allowance checks and
the existing 35,000-fuel reserve. It adds no
cast, reset or move. This supports the unusual target commitment rather than replacing it
with Tempo's first choice.

The [post-cast trace package](../../results/population/postcast-trace-integration/index.json)
records both sides of the same lab map against frozen Fireflies. OFF/ON match every byte of
the 40,152-byte public timeline frames from initial state through Gold's post-cast state
(28 frames on A, 36 on B), without masking or realignment. Both preserve Gold 120 → 0,
asleep → dead. ON then makes one ordinary Wolf basic, 1,350 → 1,305, in the same owner round;
Wolf remains awake. OFF has no corresponding Wolf basic in the measured owner prefix.
This is physical action packing, not a completed three-basic schedule or exposure improvement.

All 16 affected OFF builds match both retained WASM roles (32 byte comparisons); the integrator
audit found 150 other closures unchanged. Fresh baseline/OFF games preserve all 11 archive
payloads and decoded statistics byte for byte on both sides. All six baseline/OFF/ON lab games
complete 2,000 rounds with clean existing health checks for both teams. Coverage is one lab map,
one opponent and both sides. Private fuel, guest-branch attribution, intent, full-cycle benefit,
ordinary-map applicability, strength and admission remain unproved.

**Road productive work-order repair, 2026-10-23:** default-off
`P11_EARLY_FALLBACK_DETAIL` is defined only by Overlap's entry point. It prepares the
original finite conservative fallback before ranking can spend the unchanged 50,000-fuel
detail allowance, then completes the full public frontier before choice or action. The
three-slot increasing-ID public caller uses one stable top-three list rather than both a
raw prefix and a duplicate-aware list. It still screens every candidate, preserves both
finite promotions and uses the original generic helper for unsupported menus. Financial
models, hit arithmetic, phase clocks and the 40,000-fuel movement reserve are unchanged.

The [exact paired assessment](../../results/population/events-early-detail/index.json)
retains all 34 OFF raw/injected comparisons: every default bot and repaired Overlap match
their same-toolchain incumbent. The extracted production frontier matches its original
source oracle over 2,048 supplied frames and 2,916 stable-tie promotion pairs, including
generic fallback inputs. This establishes ordering, not native fuel safety.
All 157 existing suite cases pass through the unchanged driver, with a real shared Slot
and private Windows Job retained for each compiler/runner case.

Against frozen Fundamentals on Road, both same-run incumbent and OFF control have no
clears, earned gold or banked gems. ON records 100 clears, 1,449 gold and 490 banked gems
on A; 72 clears, 1,101 gold and 252 banked gems on B. All ten allied actor identities have
positive camp damage, earned gold, collection and banking. Native credited Marcella
episodes are clear/capture/bank at A 86/98/236, then 350/356/508; B 110/120/233, then
347/354/522. Deaths between complete episodes are allowed recovery, not continuous
survival. Neither selected collection-to-bank segment contains a death.

Complete bounded native windows show Marcella moving into altar range and starting tribute
in the same owner round, A 233 and B 230. Each 21-gem bag completes at owner start three
rounds later, A 236 and B 233; B moves again in 235. These are physical executions of
Overlap's existing arrival packing. The earlier frozen-input timing result and unchanged
bank caller remain separate support; this new Road context has no exact-state native Tempo
replacement experiment. The original capture/tribute-overlap promise remains unwitnessed.

Both arms cover all 40 current pool maps on both sides, plus both sides of two fresh legacy
maps against frozen main v28: 84 paired keys per arm, 168 paired games, plus two incumbent
preservation games. Subject faults, exhaustion, error lines and fuel-at-budget are zero.
Pool zero-clear stalls fall from four to two: Road A/B activate, while Yellow Brick Road
A/B still stall. Frozen Fundamentals separately stalls on both Islands sides in both arms.
Fresh confirmation games are healthy and productive in both arms. Across the 80 pool keys,
banked gems are 40,788 control versus 42,293 ON; this is paired descriptive coverage, not a
strength claim or a health waiver. These stalls block promotion and admission; the knob
remains off. Earlier work-order-only arms and all failed preservation boundaries are retained.

The stopped temporal screen-reuse proposal was archived and its source restored before this
repair. Its [historical preservation failure](../../results/population/events-rate-reuse-preservation/index.json)
is unchanged; current-source paths there are historical identities backed by retained bytes.
The next useful obstruction is Yellow's real selected/executed job, rather than another Road
first-detail logger or a reduced reserve. Shared suite source changed, but all affected OFF
module roles preserve their incumbent bytes.

**Yellow executor inspection, 2026-10-23:** the
[bounded follow-up](../../results/population/events-ybr-goal-negative/index.json) reads the exact
retained current-bundle Yellow A/B runs. Marcella A moves back and forth each round in the
complete native round-48--58 window; Marcella B ends rounds 43--46 with movement, attack and
cast still available, then advances and fights. These observations establish physical
inactivity patterns, not their selected jobs or guest refusal conditions. Frozen Fundamentals'
four retained Islands failures remain separate opponent contexts.

Yellow has two 30-member groups. A proposed mechanical executor change drew each original
radius-one approach square directly into the same goal mask, avoiding a temporary mask and
union per member. A separate bounded receipt would have measured the actual selected large
group, goal-construction fuel and native mask equality. Neither enabled build nor diagnostic
game ran: strict preservation passed all 16 default raw/injected pairs and the active
five-repair pair, but the older four-repair raw/injected pair produced the already retained
17-byte exception-layout variant. Moving the new definitions after the old policy was one
bounded structural correction and failed the same guard. Both boundaries are retained.

The proposal was stopped and runtime, policy and entry point restored byte for byte to their
incumbents; all 16 default source keys and both older repair recipes match again. No reserve,
financial policy, frontier, arithmetic or phase order changed. The remaining task is to
identify the real selected Yellow job and refusal without bypassing the disabled-byte guard;
goal construction is still a hypothesis. No new full games, paid-work claim or promotion.

**Goal-cost follow-up, 24 October 2026:** the integrator narrowed the preservation requirement
to the retained current defaults and the active five-repair control. Historical experimental
recipes keep their immutable cached source/module identities; recompiling every old recipe
with new optional code is not an additional admission requirement. The prior 17-byte
exception-layout difference in the older four-repair recipe therefore does not block a new
diagnostic when all sixteen current defaults and the active control retain their bodies.
This changes work selection, not any event model, policy, reserve or historical evidence.

`P11_GOAL_RECEIPT=0` keeps the new diagnostic absent. Its enabled arm retains one Marcella
selected-job/goal/movement packet at a declared owner round (default 48), then prints it at a
later owner turn with at least 90k fuel. Unknown phases retain explicit negative sentinels;
the sampled goal round distinguishes a suspended old computation from same-round work.
Packet collection and output cost fuel, so native observation still requires a matched
control and an exposed-state prefix comparison. A selected group, a cheaper goal mask or
a printed packet is not itself useful work, a paid continuation, a win or a policy activation.

**Contest continuation, 24 October 2026:** the enabled owner-entry observer changes the full
public prefix at round 41, before its round-48 packet. That packet cannot identify incumbent
private state. An existing neutral host-call trace instead reproduces the entire Yellow A
replay, both fuel streams and both stdout streams exactly. At the original Marcella cycle,
the trace reaches movement with one distance field and five small goal draws, rather than
the 30-member farm goal builder. Personal group queries identify the cleared Gold line.
The supported interpretation is a lost capture-contest detour: an off-source step returns
to ordinary nearest-source re-entry on the next owner turn. The goal-union explanation is
rejected for this cycle.

`P11_CONTEST_CONTINUATION=0` leaves the bounded component disabled. ON retains only the
selected cleared group's prior successful off-source contest step, requiring fresh personal
enemy visibility, reward state, existing retreat checks and existing combat/movement rules.
In four paired full Yellow/Fundamentals games with the five existing repairs active, it
breaks the physical cycle: Marcella reaches (18,17), Tavi (16,17), and Vendetta (17,17).
Neither side earns gold, gems or a kill. Each arm retains five late Vesper exhaustions at
rounds 1373--1377, a separate health obstruction. These are component results, not admission.

The next stop is the old on-source capture hold after the continuation cleared. At Marcella's
round-74/75 turns she sees only an off-source enemy and ends with all three allowances.
The public timeline starts an allied capture at round 75, then a distant enemy enters the
far end of the line and resets it outside her vision. A hidden chase does not follow from
her observations. Neither capture dispatch nor the farm's on-source reward shortcut has a
blocked-capture input to return to its policy. Reusing the existing failed-movement three-turn
retry for a failed capture would be a new interpretation, not an established contract.
Matching's optional continuation publication now mirrors existing best-effort publication;
it cannot veto actions. The last compiled sixteen defaults retain their incumbent RAW bodies;
the new 17-byte active-control variant reproduces all Yellow A game bytes and both sides'
perspective rows, with that bounded coverage explicitly distinguished from module equality.

Further Yellow-specific work is paused: the line is gold-only and the component has no payout
witness. The higher-value next test is the unmeasured sixteen-policy cohort with exactly
`P11_BOUNDED_INITIALIZATION`, `P11_CACHE_PUBLIC_WORK`, `P11_MOVE_FUEL_GUARD`,
`P11_CACHE_ALTAR_MINIMUM`, and `P11_EARLY_FALLBACK_DETAIL` set to 1. Goal receipts and contest
continuation remain off. The early-detail implementation was already generic, but only
Overlap exposed its knob; the common runtime now also declares its default zero so Arena
can configure every Policy without per-bot edits. Each bot retains its own Policy; defaults remain off.
The completed cohort covers all 40 pool maps, both sides, against frozen
`anchors/main-v52@4aaf587c22cc`: every policy has **0/80 strict wins**. All 1,280 keys pass
input/coverage integrity. Capital has two technically flagged keys and Geodesic four;
the other fourteen have none on this panel, and Main has none. All sixteen defaults again
match their incumbent RAW bodies, all per-bot sources are unchanged, and the sixteen
enabled modules and full-panel row signatures differ. These are preservation/style facts,
not proof that each named contribution activates reliably. Overlap's active five-repair
module also matches the original `5de698...` control exactly after shared knob exposure.

Surveyor's enabled early-detail arm has a separate compatibility defect: its fallback can
call `observed()` before `frontier()` registers the pending information probe. A completed
probe's payment/cursor advancement can be lost. Keep that arm diagnostic until its protocol
order is preserved; other policies retain their existing selector, route, assignment,
financial and combat hooks. A strict win would not waive an observed health fault, including
one after the end of a shorter Main game.

This result rejects the expectation that these shared execution repairs alone will produce
the requested win floor. The next bounded lead is Tempo on Packs A: 917 banked versus Main's
1,005 at round 1,426, despite 1,000 personally collected. Personal ready-turn history resolves
the missing 83 into four death intervals (Varg 14+13+14, Vendetta 42), with zero final bags.
The largest loss is an attempted bank return, not a declined bank threshold. Native return,
lock-service and visible-intercept evidence must precede a correction to the two-rate model.

**Tempo return component, 24 October 2026:** those four losses have distinct causes. The
first 14 remains below Tempo's bank threshold and dies after returning to central work;
the 13 enters a recall which CC and attacks cancel. The last 14 enters a personally visible
Heart dash footprint before a live source could pay. Vendetta's 42 already attempts a bank:
both nearby right altars are personally visible with five enemy lock hits from round 1285;
the round-1291 attack reduces the target from five to four, then Ryth kills the carrier.
A simple dash/reset/free-altar explanation does not supply a feasible late conversion.

The Varg fixture binds round 1273: (16,10), 1,080 HP, 14 gems, and an optimistic ready
250 shield versus Heart at (20,12), Focus 1,036, zero-cooldown dash range two/radius one.
Heart's current shared cast is already spent, while its next owner turn restores it before
the five-round capture can pay. The actual step (17,11) is hit by the following native dash;
(15,9) is a legal first exit. This is one state, not a verdict on all contested work.

The first optional guard's eager frame adds roughly 191 fuel on empty-bag ordinary turns
and loses a move at round 17. Its 203-bank result cannot judge the risk policy. A bounded
lazy/noinline structural change preserves the complete 136,931-frame public prefix through
the original round-1273 Varg input. The subsequent west step is followed by recall, healing,
eight new captured gems and a 22-gem tribute completing at round 1326, then renewed work.
Neutral host counting reproduces replay, both fuel streams and stdout exactly and exposes
the new guard-query/direct-recovery tail; an absent optional printf is not activation proof.

That arm banks 931 versus 917 on Packs A, still losing to Main 1,021; Packs B matches the
control exactly. All four games are technically clean, and neither arm wins. The two-rate
scoring and bank threshold remain intact. The correction now scopes admission to an actual
planned step for a selected live source, a personally legal direct-dash word and a fresh
recovery action; it reuses the existing recovery obligation. The refined arm reproduces the
entire earlier public replay, binding the paid return and renewed work to the new module.
Its neutral host counter reproduces all five game streams and exposes the new planned-step
field and dash-license queries before the direct recovery move. Private guest-state equality
is not claimed. The lethal certificate
compares post-shield damage in wider precision: float32 addition can otherwise round a
barely surviving HP-plus-shield total down to the candidate damage. The focused fixture
passes and all 16 current default RAW modules match their pinned incumbents exactly.

Against frozen Main53, both coherent active5 OFF and active5-plus-conversion ON lose all
80 pool keys each (40 maps, both sides), with no technical health flags on either team.
These are diagnostic recipes; the actual recipe-{} default has a different RAW module and
is not measured against Main53 by this experiment. Default-byte continuity does not transfer
the configured-arm outcomes or health to it. The [durable component and floor record](../../results/population/events-tempo-conversion/index.json)
binds all inputs and limits. This is finished historical paid-execution evidence, not a
current winning bot. The guard remains default off, with no promotion, global risk threshold
or generic combat controller; further Tempo threshold/escape tuning is stopped.

**Geodesic fuel boundary, 2026-10-23:** the current default census identifies five
round-13 exhaustions on each Ambush side. The [bounded follow-up](../../results/population/events-geodesic-fuel-boundary/index.json)
replays those exact inputs in six full same-run incumbent, disabled-control and diagnostic
games. Incumbent/control statistics match exactly on both sides; all arms retain the ten
exhaustions. The default remains productive, so this is a health obstruction.

One deferred Geodesic-local diagnostic forwards the exact SDK distance-field compute and
linear-export calls. It emits no crossing packet. The saved incumbent Tavi A window has no
round-13 physical action and a round-14 move. Combined with the post-shop round check, that
narrows the ordinary-turn handoff, but does not identify a guest instruction or prove the
diagnostic's unobserved internal state. Preparation uses nearly the whole preceding turn's
budget. These are observed events plus a source-control-flow inference, not an atomic SDK
cost bound.

One proposed mechanical guard yielded the turn that completed immutable route preparation,
retaining every cached field and starting ordinary work with fresh fuel. It kept geometry,
economic policy, complete ranking and the 40k/50k reserves intact. Its strict disabled-byte
gate passed all 16 defaults but failed both retained Overlap repair pairs (32/36 comparisons).
No enabled repair build or game ran. An earlier shared diagnostic layout failed 34/36; the
local diagnostic then passed 36/36. A diagnostic-only narrowing compile error was corrected
at a new snapshot before its games. All failed boundaries and process receipts are retained.

Runtime and Geodesic entry point are restored byte for byte, the diagnostic header was moved
to removed storage, and all default/retained source keys match the incumbent again. No
repair, default or admission change remains. A source-preserving integration and a deferred
marker that positively records preparation completion and the ordinary-prefix budget would
change the next decision; absence of a crossing packet alone does not justify a repair.

**Source-location discriminator, 25 October 2026:** three owner-private builds compare the
current unmodified five-repair Overlap control with a disabled Geodesic completion-yield
insertion, then the same insertion with the original logical source location restored.
The unmodified RAW/SDK hash is `b74f1104a70c`; the plain insertion produces `5de698657162`,
with 17 changed bytes in the executable code section. Restoring the source location recovers
the current control's exact RAW and SDK bytes. This supports that integration boundary; it
does not identify the compiler's internal cause or permit treating executable differences
as metadata. The current baseline already differs from the older control, so historical
hashes cannot substitute for the same-run comparison.

The [bounded result](../../results/population/events-geodesic-fuel-boundary/source-location.json)
records all three source/module identities and empty process trees. At that boundary working
sources were unchanged; no enabled repair or game ran. All 16 default continuities and a native paired
completion/health/production pilot remain required. The older Geodesic `index.json` and its
body directory were absent on both ordinary and reviewed host reads; its surviving final
audit and worker result are historical records, not reconstructed provenance.

**Game-update stop, 25 October 2026:** the completion-yield guard is now staged behind
`P11_GEODESIC_FRESH_ROUTE_TURN=0`, with the logical runtime source location restored after
its conditional block. Five defaults pass exact RAW and SDK preservation. Matching also
matches its known distinct RAW and SDK hashes, but the harness incorrectly required its
injected module to equal RAW and stopped. Several historical SDK alias bodies are absent;
role-specific retained bodies or exact preimage builds are required before claiming the
complete matrix. Neither an enabled build nor an experiment/game started.

The user's game update stopped this work before completing the 16-default/configured-control
matrix. The suite emitted 62 passes and no failures before its active worker was cancelled;
this is an incomplete test run. Both private Job records confirm empty trees, with the test
Job recording forced termination. No owned process remains. The
[exact old-epoch state](../../results/population/events-geodesic-fuel-boundary/completion-yield.json)
binds frozen source closures, both measured module roles and the partial checks under
CLI/SDK 3.0.5, engine 1.0.3, toolchain `3837f00416ea`. No default is promoted. Resume only
after the integrator reconciles the updated bundle and runner parity, then bind fresh
role-specific controls, complete preservation/tests and run the paired Ambush pilot.

**Strata strength baseline, 2026-10-23:** exact RAW `events/strata@7684e8fba6f6`
against frozen RAW `anchors/main-v44@b3ff333b6bcf` loses all 80 games: 40 pool maps,
both sides. Execution and final input checks pass; 62/80 games pass both-team health.
Strata has exhaustion in 14 games and stalls in 14, overlapping in ten (18 unique
unhealthy games); Main has no health flags. Native death events attribute 1,152 of
Strata's 1,264 deaths to opposing champions, and 112 to camps. The
[durable baseline account](../../results/population/strength-v44-baselines/index.json)
retains this scope; the [original run](../../.cache/arena/runs/strata-main-v44-baseline-20261023/summary.json)
is the local fallback while that account is prepared.

Preserve the earlier exact-key Ambush/Great Wall paid-work and renewal evidence
against Fundamentals; it does not transfer to this stronger opposition. Allocate immediate
strength work elsewhere while retaining Strata's partial-progress contribution. Dominant
observed PvP losses do not justify economic-policy tuning. Shared fallback bypassing the
local admission rule and admission/retreat incoming-cost differences remain deferred
hypotheses, requiring a production-caller discriminator and a native opportunity witness.
No code, default, strategic admission or health waiver follows from this baseline.
