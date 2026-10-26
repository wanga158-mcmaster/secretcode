# Timetable: executable production episodes

Latest checkpoint: corrected residual-service source `8ab060b6f62e` completes16/16 healthy
full games but fails production. The first source `f3c43eb5ad02` failed health and production;
the corrected source builds and passes all six fixture families. `production_service` defaults off, as do episodes,
routes and trace. The physical-trip candidate `9984ca34f21e` remains rejected. No competitive
replacement is qualified; the earlier eight OFF payload proofs remain bound to `195a3e912c4b`.

Current `8ab060b6f62e` OFF preservation is separately verified on eight full Ambush/Eight
keys, both sides against frozen v42/starter: whole uncompressed gameplay payload and all
terminal fields identical to the exact-live executable controls, zero masking, both teams
healthy. Default raw/injected SHA639ab2f63f1556f8e7a4724a413eaeb20f7d2b19332e00490b573138ebf099ca
equals the same-run explicit-OFF profile's bytes. Proof:
`results/contenders/timetable/service-renewal/8ab060b6f62e/off-preservation.json`.

The previous gem-sweep implementation and its complete notes are preserved at
`bots/miscellaneous/timetable-gem-sweep`; the earlier implementation remains separately
at `bots/miscellaneous/timetable`. The failed admission correction's native48-game evidence
is `.cache/contenders/timetable/race-loop-admission-pilot-v1` and the pure native audit is
`results/contenders/timetable/lifecycle-audit/admission-summary.json`. They are historical
evidence for the retired source, not this successor.

The active selector was recreated with `arena new bot contenders/timetable --from
anchors/main-v42` on23October2026. Its initial20-file chassis is frozen live v42
`anchors/main-v42@9fc5d7469074`, production `main/main@0353a987e04f`. No independent
strength is claimed by recreating that chassis. No online actions are authorized.

## Current implementation boundary

The new default-off controller owns complete useful production episodes: acquire an
achievable job, observe and do work, leave with covered individual survival, recover and
return to persistent actual damage, finish capture, release spawn tiles and convert the
reward. Solo work is the first supported executor; it is not a claim that every job ought
to be solo. Real-party execution belongs to Warband this cycle. Unsupported labor must be
priced or rejected rather than credited hypothetical ally HP.

The basic damage/camp-state model is a cost estimate. Current individual exit coverage
is a separate execution guard. Unseen damaged state is a planning hypothesis until freshly
observed; no shared capture intent or expired timer proves reward/respawn availability.
Recovery and conversion must preserve or explicitly release the episode, so the generic
health/tribute stack cannot silently retarget it. Threat handling may preempt any episode.

The first implementation is `src/production.h` plus a separate autonomous `Brain` path. Pure
transition/cost and actual whole-group wake cases belong in `tests/production.cpp`. Scratch and native experiments stay
under `.cache/contenders/timetable/`; durable measurements under `results/contenders/timetable/`.
Every new behavior remains off by default. The measured stage below is complete; no broad
qualification or adoption is justified.

## Executed architecture and rejected first cohort

`production_episodes` owns acquisition, individually bounded stationary work, legal-path exit,
recovery/return, observed reward, sole physical receiver, tile release and conversion. Enemy
threat and committed homeward handling retain precedence. Health and banking no longer silently
retarget an active episode; recovery retains its group, while death, observed respawn, a freshly
observed HP increase, a deadline or failed capture releases it. Current direct observation is
required for work. Remembered partial damage can price a return but cannot authorize a cast.

The quote uses the existing sustained-kit/Smith camp estimate, charges actual own health pools
and exit/recall/heal/return for every estimated extra trip, then capture and banking. It credits
no hypothetical ally damage or HP. A full retained gold purse also pays spending service in the
quote and calls a quiet recall after delivery; spawn shopping uses the inherited purchase executor.
The retained300gold service trigger is scaffolding, not a measured optimum. The current egress
controller requires a bounded legal walking path, sums possible camp damage without crediting
allies as tanks, checks the next ordinary approach destination against already awake camps,
and excludes damaging dashes from stationary work. It recomputes this finite current-state path
each turn; it cannot guarantee future occupancy or unseen enemy behavior.

First source `38ce7b262e04`, ON `0b51d8d20f01`, completed48 full games in
`.cache/contenders/timetable/production-episodes-pilot-v1`: six training maps, both sides,
frozen v42 and starter, same-run baseline. V42 wins6/12→0/12; starter12/12 each. Total
flips+0/-6, p=.03125, paired margin−463.375±122.596SE. All subject health flags are zero,
maximum fuel93,079. Opponent health exceptions remain in the native audit, not filtered.
Matched at most400-round native receipts over24pairs show collected8113→3369,
banked6964→3112, gem-bearing jobs378→208, deaths33→44 and camp deaths12→13.
This is a severe production deficit, not a safety gain. Reject this cohort; no full-pool,
legacy confirmation, promotion or score/threshold tuning follows.

The saved audit is `results/contenders/timetable/lifecycle-audit/episodes-summary.json`.
Its exact measured source is never relabeled as the later corrected source.

## Executed recovery cycle, with its evidence boundary

Essential omitted execution boundaries were corrected separately: approach destination exposure,
possible collateral wake, and actual spending after gold-only work. This source's trace arm
`da6b9f6bf323` completed8 full health games on Elkring and Collision, both sides against v42
and starter, subject health clear and peak87,868. These are execution observations with logging,
not another strength comparison; the ordinary trace-off ON arm is unmeasured.

Two bounded current-native reads of one ordinary ElkringB/v42 game verify actor6 Marcella's
acquired production episode: actual camp5 damage at60, exit70, recall74 (timer5), arrival79
with HP120→1200, own resume move80, same-camp attack91 and clear102. Native group5/g_506
becomes reward; capture108 awards her22gems+110gold and her own move vacates the tile108.
Tribute starts129 (timer3) and completes132: bag22→0, teamB score22→44. Exact actor and
native statistics joins agree. Allied damage may contribute during absence; this is not
exclusive solo clearing. Native travel111–128, profitable renewal and a strength benefit
are unproved. Both saved windows are complete without query failures/truncation; both owned
bridge process trees exit confirmed empty. Evidence and exact command/stream/hash bindings:
`results/contenders/timetable/lifecycle-audit/trace-witness/assessment.json`.

The final mechanical correction handles **whole-group** collateral wake: an AoE or splash hit
on a harmless member can wake a distant harmful sibling. The production caller now consumes
the pure `collateral_wake_groups` helper before selecting threatening bodies; unknown members
of a potentially woken group are conservative possible sources. The focused test exercises
this exact helper, including harmless-hit/distant-sibling, dead-member and no-splash cases.

Current source `c2086eeeea81`, final trace ON `708f26eb3ea8`, completed4 full Elkring games
(both sides/v42+starter) with all subject flags zero and peak87,870. The24focused checks in
one WASI test pass, build passes, all source files are LF. These four games test final execution
health; they do not transfer the earlier bounded native witness automatically to a new source,
and do not establish competitiveness or full-pool health. No more games or corrections this cycle.

The current ordinary trace-off ON candidate is frozen and build-verified as `d0b30a5d993b`,
raw/injected SHA256 `73f76c470631e3bded5920b38af97039a496b0f4d5113846dfd97ea3e41adac5`.
It has zero full games. This concrete snapshot is the next experiment's starting candidate;
the traced observations above retain their own identities and are not relabeled as its health.

## Preservation and next structural question

Current defaults are `production_episodes=false`, `production_trace=false`. Raw/injected
SHA256 `747e867b031f050a5e7a1ab6d484e2250719f8e917889add435f9f8e8823c8d4`
is identical to replay-proved source `ee46468bf384`, and differs from live's module bytes.
The failed exact-live byte check is preserved; no compiler-order investigation followed.
Eight full same-key OFF games (Ambush/Eight, both sides, v42+starter) have SHA-identical whole
uncompressed `replay.repl26a` members to existing exact-live `compound@fab92a00127a` controls,
whose raw/injected5aa47517... hashes were independently rechecked. No bytes are masked.
Terminal gameplay also matches8/8. This is strict preservation over those keys, not a global
module identity claim. Proof: `results/contenders/timetable/production-episodes/off-replay-control.json`.

The current checkpoint and68raw native statistics are durable under
`results/contenders/timetable/production-episodes/`; the existing index preserves prior-generation
metadata and links current source/module closure, failed claims and all measured stages.
All owned build/game/read handles have finished; no viewer/server was started.

Next investigate the production opportunity set and actual time allocation on a matched loss:
how much useful gem supply did this controller make unavailable, and which exact commitments,
walking, recovery, capture or repeated conversion consumed the missing productive turns?
The sustained-rate cost forecast, full-group visibility requirement, static route fallback and
single-body first executor remain approximations/coverage limits. The verified partial-work
cycle proves executable persistence, not that repeated independent trips beat target-sized
joint labor or that this quote selects profitable work. Answer that question before adding
weights or broadening another losing cohort.

## Executable travel and displaced production: saved Big World discriminator

The next question above is now narrower. Reused the original rejected pilot's Big World A
against `anchors/main-v42@9fc5d7469074`: BASE `38ce7b262e04`, episodes `0b51d8d20f01`, engine
1.0.3. At R400 both own teams have zero deaths, yet BASE has240collected/133banked/12productive
gem jobs and episodes33/33/3. Both sides have clean full-game technical health on this key.
This case separates missing early production from early death; it does not isolate one cause.

Two current-native saved-history reads retain all five actors'400ready starts through R400.
Varg actor4 has250ready-start position changes in BASE and374 in episodes. These are position
changes, including recall/displacement, not counts of ordinary moves or wasted turns. BASE
Varg obtains74gems,4credited clears and13purchase events by R400; episodes Varg obtains none.
The latter still has no own death by R400. Statistics' cumulative timeline indices are not
native action rounds: the bounded action observation below places the first worked suffix at
R198, correcting the initially plausible R199 interpretation of the first damage sample.

One complete native R190-216 window directly verifies episodes Varg's ordinary approach to
group9 `g_6281`, applied basic/reset/basic at R198, further basics through R205, exit moves
R206-209, recall R210, arrival R215 and end-turn HP350->1000, followed by his own move R216.
The group contains two wolves, an elk, two gems and gold (37gems/190gold). This is real partial
work and recovery after extensive travel, with no observed Varg payout through R400. It does
not show his initial private target, quote, target changes or the reason for every earlier step.

A reproducible pure 8-neighbor BFS from his native R1 `(78,12)` to a legal tile within melee
range of that group requires89ordinary moves. The result is89 both with only static terrain
and enemy spawn blocked and with every camp spawn additionally blocked. The source's
uncomputed-route fallback to integer-average center `(81,63)` is `51*5/4=63`,26moves less.
This is a geometric model counterexample, not a minimum owner-turn claim with abilities or
dynamic occupancy, and it does not prove this group was his initial commitment. The old owned
source permits that fallback in its quote; once acquired, the episode does not reprice travel
when a more exact route is computed. Actual arrival at R198 is later still, and its whole delay
cannot be attributed to the26move discrepancy from these observations.

**Decision:** stop tuning or broadening the losing quote. The next complete architecture must
bind acquisition to reachable executable work and refresh a feasible continuation as route,
visibility, health and opportunity change. Its cost must account for displaced production,
including the value of nearer renewal and growth; changing a detour coefficient is insufficient.
The evidence selects this representation/executor boundary, not a final policy or stronger bot.
The current corrected trace-off candidate remains unqualified and off; its source is unchanged.

Durable inputs, all three native outputs/arguments/empty-Job confirmations, both exact selected
replay archives, source excerpts/hashes, matching raw statistics and both BFS routes are linked
from `results/contenders/timetable/lifecycle-audit/opportunity/assessment.json` and the existing
Timetable index. The pure assessor is `.cache/contenders/timetable/lifecycle-audit/opportunity_assess.py`.
All three reads end at the requested round with complete coverage, no query failures and empty
owned process trees. No new games, bot changes, builds or online actions were performed.

## Physical-trip rebuild: definitions, proofs, results and application

**Definitions.** A production episode retains a real group and observed partial work across
recovery. A trip is a replaceable origin, intended member, legal attack frontier and adjacent
walking path. Its path length comes from one origin-rooted distance field. Static graph
validity is distinct from the fresh authority to execute its next step. Runtime state is
private to this champion's VM; this change adds no team-memory capacity or message protocol.

The `production_routes` arm implements that representation in `production_trip.h` and Brain's
`production_prepare_route`, `production_route_plan` and `production_route_step`. Quiet static
search owns a planning turn before the expensive observer, then fresh observation and position
must authorize subsequent selection/execution. Unknown camp spawns are conservatively blocked.
The cached graph is not current dynamic occupancy. An exposed continuation instead searches a
bounded fresh local envelope; candidates outside it are unknown, not proved unreachable.

Planning binds an actual next member and attack stance. Arrival, unexpected displacement,
refused movement, member death, changed observed work, changed kit and recovery require fresh
continuation/opportunity assessment. The executor retains actual approach-exposure, whole-group
collateral wake and legal egress checks. `production_work_front` starts with the intended live
member, refreshes splash effects, then checks actual survivors and allowances for spell/reset
continuation. Existing capture, tile release, tribute and recovery phases remain explicit.

**Lemmas and implementation checks.** In the supplied frozen graph, a path descending exact
integer distance by one reaches its root in its initial number of successful steps. This does
not bound elapsed rounds, dynamic blockage or ability movement. The pure trip kernel checks
adjacency, source distance, reached versus unreached, actor-round observation, refusal and
displacement. Its 14 focused WASI checks include the actual caller adapter: absolute coordinates
must call `move_absolute`, demonstrated at a non-origin position with an SDK-shaped double.
The existing 24 production checks also pass; `arena test contenders/timetable` reports 2/2.

The [mathematical foundation](../../main/foundation/mathematics.md) L5/T1/T2/T4 supplies the
conditional path, guarded-transition, complete-cycle and fuel obligations. These are not proofs
of this C++ controller. Search/export/query/allocation/action charge bounds remain open; round
fences alone cannot prove no suspension between a guard and effect. A complete residual-state
production operator with guaranteed finite contribution has not been implemented or proved.

**Native results.** The first same-source 24-game generation exposed two implementation
boundaries. Absolute destinations were mistakenly passed to the relative `move` API, causing
Elkring errors; Big World also stalled with no useful work. The full-origin search's placement
after the observer prevented admission under the retained reserves on that path. The correction
uses the tested absolute adapter and a separately staged quiet search; it does not lower reserves.
The failed generation remains preserved as failed; the correction changes two mechanisms and
does not isolate their individual outcome effects.

The corrected source `195a3e912c4b` builds default, old-episode and route arms. Routes compile to
`9984ca34f21e`, raw/injected SHA256
`a3313f148237f6d75771380093784d211086f025c9936748c42174f9957fcced`.
All 24 corrected full games finish with no subject or opponent faults, exhaustion, errors or
stalls. Coverage is Big World and Elkring, both sides, v44 and starter; each of three arms has
eight keys. Peak subject fuel is 99,899, an empirical maximum rather than a safety theorem.

This implementation still fails production. Routes win 0/4 against v44 and 2/4 against starter;
BASE wins 2/4 and 4/4, old episodes 0/4 and 2/4. At the common R400 horizon across eight keys:

| Arm | Collected | Banked | Productive gem jobs |
| --- | ---: | ---: | ---: |
| BASE | 2,623 | 2,135 | 118 |
| Old episodes | 1,132 | 904 | 53 |
| Physical trips | 670 | 494 | 34 |

One ordinary corrected-source Big World A/v44 saved window, R80-140, directly verifies Varg4's
applied approach and work at R85, credited clear of group5 `g_5634` at R114, sole physical
15-gem/100-gold payout at R121, own tile release that round, tribute start R137 with timer2,
and completion R139: bag15->0, score4->19. Native command, capture/channel state and exact
owner/group native-statistics joins agree. Credited clearing is not exclusive damage proof.
The private initial quote/certificate is source inference, not directly observed. This window
does not verify a current-source recovery-return chain; Elkring's statistics offer candidates
only, and the older source's recovery witness is not relabeled.

**Application decision.** Reject the complete implementation and keep it off. Physically
witnessed routing and one legal conversion are useful components but insufficient production.
No coefficient tuning, broader cohort, promotion or strength claim follows. The next derivation
must implement a residual-state operator with bounded observation/planning/action liveness and
actual useful allocation, rather than equating route reachability with timely service.

Default raw/injected SHA256 `32c17f4d3f9150d4d929e927a1b6f1bb82778165809ce5ca59c832d590da68af`
differs from the older default module. Eight complete Ambush/Eight keys, both sides against
v42/starter, nevertheless match the exact-live executable controls' whole uncompressed gameplay
payload and terminal records byte for byte, with zero masked bytes. This proves preservation
on those keys only. All defaults remain false: `production_episodes`, `production_routes`,
`production_trace`. All 22 source files are LF. No main or main-anchor source was inspected.

Durable `results/contenders/timetable/physical-trips/checkpoint.json` retains all 56 complete
game/statistics inputs (failed24, corrected24, OFF8), builds, source closure, health and native
receipt accounting. `off-preservation.json` binds the strict payload check;
`execution-witness/assessment.json` binds the one current native window and its limits.
Every owned game, build and observer process has finished; the observer Job is confirmed empty.
No viewer, server or background job was started. No online action was performed.

## Residual service rebuild (in progress)

The continuing question is complete productive service under finite health, travel, capture,
recovery and conversion. The failed route implementation stays in arena snapshots and the
source-bound records above. This rebuild replaces that representation in place; it does not
rename a coefficient change as a new bot. New `production_service=false` dispatches before the
inherited turn stack, retaining the old OFF path. Purchases and enemy combat remain fixed
support components, so an independent complete economic architecture is still being earned.

**Definitions.** A service job names an actual camp generation and individually observed
member residuals. Capture of an already cleared group is separate available work, including
another actor's clear. Banking operates on carried inventory, rather than being forced after
every payout. Recovery retains residual knowledge but may not declare a projected HP increase
a new generation. Planning-deferred, stale, and a completed static component's unreachable
result are distinct outcomes. A fresh command authority includes owner epoch, position,
allowances, legal target and the prospective whole wake, not only the cached static path.

**Conditional model lemmas.** `service_model.h` applies pointwise simultaneous basic/splash,
single cast/reset/shield/CC/AoE and group wake to a supplied complete state. Its fragments
require every nonempty prefix to survive the supplied next-volley/escape model. These are
finite model properties, not C++/native proof. The restricted basic-only `service_price.h`
applies simultaneous damage until the first affected death, reranks actual residual sources
and repeats for at most N death chunks. Its attacks and raw incoming are conditional on that
event clock; they are neither universal elapsed service nor a gold/gem exchange theorem.
The native 4-wolf Varg premise yields 34 ordinary splash basics, versus 136 without splash.

`service_observe` collects exact living HP even for sleeping harmless members, provenance
for hidden projections, actual lifecycle state when visible, own actual allowances/shield and
runtime ability values. `Ready` means same-epoch collection; it does not mean model-complete
or safe. Applied first primitives use incremental affected-member refresh; refused commands,
displacement and a stale epoch withdraw that authority. Capacity32 covers the verified current
maximum30 members/group. Unknown cannot become dead or infeasible through a failed query.

**First native result.** Source `f3c43eb5ad02`, OFF `01c73de779db`, ON `7ec8aebf5a6d`, completed
16 full games: Big World and Elkring, both sides against frozen v44 and starter, paired same-run
control. OFF8/8 healthy; ON7/8 health failures (six exhaustion games, one error), peak100000.
ON0/4 v44 and4/4 starter versus OFF2/4 and4/4. Common R400 totals: OFF2623 collected/2135
banked/118 gem jobs; ON1049/804/46. Reject execution and adoption.

This is already an early service deficit: all ON bodies have zero native fuel exhaustion
through R200, but R100 OFF376/198/20 versus ON176/132/8, and R200 OFF1105/687/51 versus
ON308/220/14. Big World has zero ON captures through200 on all four keys, all five bodies
already dealing some monster damage, and zero own deaths. OFF has8-11 per-body capture
receipts; deduplicating shared payouts gives4-6 physical capture cycles on those keys. This
does not attribute cumulative damage to private targets: native statistics do not expose a
per-target damage event stream. Durable binding, health, exact receipts and first exhaustion
timestamps are under `results/contenders/timetable/service-renewal/f3c43eb5ad02/`.

**Refinement counterexamples and current application.** A route root in range of one member
need not reveal every member. The initial all-visible work gate could repeatedly reach a
distance0 frontier, discard it and plan the same frontier without acting. A ground AoE aim
offset r from a target can hit another group 2r away; discovering collateral from visible
members within r therefore did not cover the actual command family. The exact native error
was ElkringB/v44R103: altar locked while the body could not attack it. None of these source
counterexamples alone is claimed to cause the whole early production deficit.

Corrected source `8ab060b6f62e` builds and passes six fixture families. Work uses a compact
exact-observed member subset with original identities; omitted members remain possible sources
in a separate static-membership whole-wake/egress bound. A 2r bounding-rectangle envelope
overapproximates possible offset AoE collateral, including hidden siblings. The bounded
escape-field miss is NeedCompute, not impossible. Cleared camp tiles open conditional routes;
fresh dynamic movement still authorizes each absolute step. Enemy altar locks stop submission
unless a legal attack can be applied. `service_execution.h` selects one first command with at
most six State option copies rather than replaying fifteen fragments three times. Shield/reset
first requires a productive legal basic continuation and safe first and second prefixes.
Structural work reduction is established; its native fuel safety remains to be checked.

The corrected same16-key cohort completes with both teams healthy in16/16 full games and
peak service fuel87476 (empirical only). OFF `20f607b71573` and ON `db7665268d91` bind raw
and injected modules respectively `639ab2f63f1556f8e7a4724a413eaeb20f7d2b19332e00490b573138ebf099ca`
and `f1d8ba63d1312cd49dad87b8fccd18c9e67e674561f0fed45eccf9d7bb4dafeb`.
R100 OFF376/198/20 versus ON176/176/8; R200 OFF1105/687/51 versus ON418/264/17;
R400 OFF2623/2135/118 versus ON1236/1032/51 (collected/banked/per-body gem-job receipts).
ON0/4 v44 and3/4 starter versus OFF2/4 and4/4. Healthy execution alone does not repair the
service deficit. Source-bound inputs, native accounting and health are under
`results/contenders/timetable/service-renewal/8ab060b6f62e/`.

Next is a bounded saved early ordinary work/recall/re-entry audit, before widening full-kit
pricing. Repeated tiny work with large healing may reflect a control-boundary bug, broad
collateral exposure or real expensive service. No single cause is inferred from cumulative
damage. Defaults remain OFF; no strength or release claim follows from these small cohorts.

## Complete retirement for independence

The active economic state machine and pure service kernels were independently authored,
but the complete active bot still executed the inherited draft, Ctx/Kit/World caches and
observation, Knowledge/shared protocol, navigation, enemy combat/retreat, purchase executor,
gem weighting and tile-release helpers. It therefore did not satisfy the renewed requirement
for an independent complete contender. Root explicitly authorized moving this entire directory
intact to `bots/miscellaneous/timetable-residual-service` and creating a fresh official-starter
Timetable. All performance and OFF preservation above belong to this retired implementation.

Only independently authored pure service model/selection/pricing headers and their fixtures
may carry over with an exact closure record. SDK adapters and executors are reauthored; none
of the inherited chassis helpers or draft may remain as the active new controller. The fresh
starter is its own OFF control, not a relabeling of historical OFF preservation. The already
running saved8ab native-chain queries finished with complete coverage, no failed query and
empty owned Jobs before the move. Their final assessment is stored separately under
`results/contenders/timetable/service-audit/renewal-8ab060b6f62e/`.
