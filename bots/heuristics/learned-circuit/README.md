# Learned Circuit (07)

Each actor runs a private two-stop program: visit A, clear and hold A, visit B,
clear and hold B, bank, recover if needed, then repeat in the same order. The
program changes at a lap boundary or a concrete failed visit, rather than being
replaced by the best source score every turn. A personal payout resets that
stop's failure count. One failed visit retains it; two permit replacing exactly
one stop at the next boundary. Productive stops and their order survive a blocked
step, a short recovery and an attractive distraction. The experiment stays at two
stops; it does not use its profitable-lap counter to expand the route.

Initial and replacement candidates use a cheap return-per-journey screen,
personal equipment maturity, visible sharing, recency and the remaining game
horizon. Keeping the learned order is a hard switching cost. At most one
productive probe fills a temporarily missing/sleeping stop per lap. A witnessed
respawn defers a visit without calling the stop unproductive; an elapsed public
respawn interval only justifies looking again. No unseen group state is queried.
Own physical progress renews a 12-round lease, but a visit always expires after
120 rounds of active work. Deliberate bank/recovery pauses preserve that clock
and refresh its distance/HP reference on return. A third unpaid recovery ends
the visit, bounding interrupted excursions. Injury alone is never income.

The production `Circuit` owns visit/hold/bank/recovery transitions. Its executor
directly pursues the current stop between boundaries, and explicitly puts that
stop ahead of the nearest-12 feature budget. Missing expensive features do not
count as failure. The copied bounded setup, static capture-component filter,
altar approach mask, remembered occupancy, unseen-member frontier and expiring
witnessed-death memory preserve the prior legal navigation mechanics. All
postwalk work waits for a fresh owner turn so new vision, attack resets and
planning cannot stack behind a path computation.

`Receipts` samples actual own bag and wallet before shopping and records the
postaction baseline. A learned payout requires continuous physical hold, a fresh
reward-available to respawning transition and positive personal bag or gold
gain. Gold-only and shared rewards are valid; nominal group values, camp kills,
ally damage and predicted renewal do not teach paid stops. An outstanding tribute
conservatively blocks capture attribution, avoiding simultaneous Marcella bank
gold ambiguity. Actual submit initiation is recorded immediately, including its
starting bag and queried duration multiplier, so one-tick tribute completion is
visible on the next owner start. Recall and death do not count as banks.

The route banks at its end even with a small bag. Winning cargo, a finite-horizon
deadline, low health or a 24-gem risk cap can interrupt early; successful banking
then resumes the same stage and lap. Recovery has a separate saved stage so a
bank interrupted by recovery still resumes deliberately. Death preserves learned
stop history but clears the unbanked attempt and exhausted cursor, without
crediting a completed profitable lap.

A capture watch independent of the policy target counts the actual reward hold
through empty personal enemy vision. Queried capture time plus two rounds without
payout quarantines the stop and causes bounded physical departure. A failed exit
can still bank immediately beside an altar, attack one harmless whole-group route
blocker, or recall after bounded visible-threat clearance. The expired watch
persists until actual departure, death or a changed lifecycle. Existing channels
are checked first. This locally copies repaired 02's failed-exit dispatch rather
than 06's unconditional return after a blocked exit.

Draft order is Vesper, Marcella, Tavi, Ren, Stellar (suite indexes 3/4/0/10/8),
including an economic three-pick prefix. Vesper buys Monster Strength, Jungle
Strength, then Power Health; Tavi buys Monster Strength, Jungle Health, then Power
Strength. Marcella, Ren and Stellar buy Monster Focus, Jungle Health, then Power
Focus. Actual prices and legality come from the SDK. Vesper's ordinary same-target
work uses basic, ready own reset, fresh basic; it does not inherit reset-pursuit's
kill/retarget program. Other camp casts remain single-target damage or CC, one
successful cast across both slots. Stellar's ground area spell is unsupported in
this experiment. Unexpected engine errors log `error` at the turn boundary.

## Current bank-boundary repair (23 October, engine 1.0.3)

The current activity census pins incumbent `7b64dffed801` and its injected
Great Wall A/Fundamentals replay. Tavi native actor2 has1515 unchanged owner-start
position/HP/wallet samples from249 through1763: full-health on spawn, holding five
gems. Three canonical native windows prove zero actions in forty-eight ready,
nonchanneling, unthreatened owner turns; they do not audit readiness or every
action across the entire1515-start interval.
The exact source/log chain is a state failure: stop15 pays five gems at169;
failed stop5 exhausts cursor2 and requests end banking at181; the permitted
gold-only detour11 pays at242, but its return calls `visit()` with cursor still2.
Subsequent policy selections have no current source and no bank stage.

`CIRCUIT_BANK_BOUNDARY=1` is now the default. Explicit zero retains the old
branch. One returns an
exhausted pending end-bank detour to bank stage and preserves that obligation
if another refused bank walk re-enters planning. The original one-detour limit,
learned stop history/order, private payout receipts, failure replacement,
bank/recovery pause handling and safety reserves remain unchanged. The first
small bank still requires an actual native tribute; no lap or income is invented.
Fresh OFF `dc93327c6f8e` raw and injected bodies independently equal incumbent
SHA256 `9067720cab11...`; ON is `66bda76f1efd`, both roles `51cc49607f51...`.

The fixed current A actor first differs at243, immediately after the detour
return. Native receipts show bank5 at287 (team10 to15), retained stop15 paying
personal80 gold and15 gems at414 with actual camp work, then bank15 at470
(team44 to59). He records ten personal gold increases,360 earned gold,
18195.5 camp damage,102 collected gems and88 submitted, without death; incumbent
values are66/1137/5/0. The mirrored B entire raw replay member is byte-identical,
including its ordinary bank at221 and renewed work/payout at290. Shared rewards
and damage are not solo-clear claims.

Same-run qualification covers forty pool maps, both sides, against frozen
Fundamentals:160 paired-arm games, all complete and technically healthy on both
teams. OFF and ON each win10/80 with no flips; paired gem-margin difference is
+16.15. Maximum subject fuel is98259/98260. Eight paired-arm fresh games on
legacy Ambush/Collision, both sides, against current frozen Starter also pass;
each arm wins2/4. No pool actor has zero whole-game source work in either arm;
zero-income actor counts are9/10, which do not classify every long quiet interval.
This is one pool opponent and two fresh maps, not a universal liveness, strength
or population-admission claim.

Production-flow fixtures reproduce the old failure and exercise repaired
detour-to-bank, refused-walk persistence, nested recovery and paid-stop renewal;
the existing common/learned fixtures also pass2/2. The initial host fixture needed
a full historical-state correction: the already-used replacement latch/counter
from native148 and Stop5's two prior failures, becoming three at181. The failure
count is inferred from native failed visits at71,147,181 and the frozen source,
not read from private guest state. The failed input/output remains preserved;
correcting the fixture did not change production source.
Exact source/module/native/health evidence lives under
`.cache/arena/orchestration/mass-activation-next/learned_circuit_activation/`.
The integrator approved only this mechanical default after an independent
70-check review. Final raw and injected default/OFF continuity and exact retained
evidence are recorded in
`results/population/learned-circuit-bank-boundary-20261023/index.json`.
The review does not establish strength, universal actor production or population
admission. Eight B Tavi's income falls from20 gold/five gems to zero while camp
damage rises from10142.3 to11144.7 with no deaths; that remains an explicit limit.

## Evidence and limits

Development began default off. The off arm directly includes frozen shared
control; all active bookkeeping is outside that arm. Production fixtures cover
two personally paid ordered laps and small route-end banks despite distraction,
one-failure retention, two-failure single replacement and replacement payout,
omitted details, recovery and absolute lease expiry, death during end banking,
nested early-bank/recovery, gold-only/shared/no-income receipts, tribute timing,
failed exits, purchases and roster. The fixtures call the production helpers.

The first eight-game smoke was clean, 2/8, maximum ordinary-turn fuel 79,276.
Review found two state defects before full panels: death at the end cursor could
leave the program unable to execute, and a shared resume field could trap an early
bank after recovery. Both now have production-flow regressions. Capture timeout
fallback also explicitly requests adjacent banking, independent of the ordinary
bag rule. A superseded diagnostic labelled unrelated personal capture as circuit
payout; final diagnostics distinguish `income` from learned `payout`.

Exact-source freeze smoke `e2ce2735682c`, `.cache/arena/runs/h12-07-freeze-smoke`,
covers Fundamentals, both sides on broken-curve, lanes, islands and boss-battle:
2/8, all clean, maximum ordinary-turn fuel 78,417. Smoke outcomes are not strength
evidence. Explicit-off `263124262b4c` has WASM SHA256
`ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`,
byte-identical to frozen Fundamentals, including its native injected build.

That source's 102-game fresh confirmation was clean (96 wins), but a late review
found that deliberate banking/recovery time aged the visit lease. A long pause
could therefore teach a false stop failure immediately upon return. The partial
pool run was stopped, and neither run certifies the repaired source. Production
fixtures now require long bank/recovery pauses to preserve the stop/cursor and
fresh progress reference, including recovery nested inside banking; the third
unpaid recovery and 120 active-round bounds remain explicit. Repaired source
`2638b7dd8873`, `.cache/arena/runs/h12-07-lease-smoke`, passes all eight smoke keys,
2/8, with zero health flags and maximum fuel 78,452. Explicit-off `ba9cdc455315`
still has the frozen `ed121f...` WASM. Its required full panels are rechecked
separately.

That lease-repaired source's full pool panel had three raw zero-clear flags
(ambush both sides and interlock B against the pinned main). They were mobile,
with no faults, exhaustion or errors. Interlock's 29 deaths repeatedly restarted
the same two Elk stops because death discarded the failed work outcome. Death
now teaches one failed visit only for visit/hold or recovery from that work;
early banking, recovery from banking and end banking do not blame a retained
unattempted stop. Existing personal payout history survives. The old ambush A
game actually banked 88 gems and earned 477 gold despite zero camp-clear credit;
its raw screening flag was retained and was not a stationary deadlock.

Final source `152476a1ded1` passed 32 pinned regression keys against the exact
four opponent snapshots from the failed sweep: ambush, interlock, broken-curve
and lanes, both sides, main one quarter. All 32 are clean, including all three
previous raw flags; maximum fuel 91,663. No candidate coefficient, admission
screen, roster or equipment change was used to fix those keys. Fixtures cover
work death, its second-failure replacement and payout, and refusal to blame a
stop for death during banking. Earlier source `2638...` and its native receipts
are superseded by this source.

Strict macro profiles v3 pass 7/7 policies. Fingerprint `d2994d7f1984` differs
from 06 on 62.9% of choices, 37.2% of banking, 0.7% of risk, 80% of roster
entries and 32.6% of actual purchase plans; coarse build stages match. Report:
`.cache/arena/heuristics-profiles/20261022-005801-79168-993908100/report.json`.
This corpus omits the route program's physical execution; native receipts and
matched games are separate requirements.

Exact-source native activation (`152476a1ded1`) is in
`.cache/circuit-review/death-final-receipts-summary.json`, with full receipts in
`death-*.json`. Broken-curve B versus Fundamentals, replay
`.cache/arena/runs/h12-07-death-regression/games/01broken-curve_fundamentals_learned-circuit_cf5f7404.repl26a`:
Tavi physical identity 11 / native actor 5 fails source 7 at rounds 26 and 192.
At the round-268 boundary, only stop 7 is replaced by 9 (`g_1377`), which pays
at 277. Stops 9 to 11 (`g_1363`) produce two completed ordered laps, 2 and 4:

- Round 277: source 9 pays 40 own gold, solo capture; 312: source 11 pays 85
  own gold and 13 gems shared with Vesper; own bank completes 13 to zero at 341,
  alive at 900 HP.
- Round 430: source 9 pays 20 own gold, shared capture; 461: source 11 pays 56
  own gold and eight gems shared three ways; own bank completes eight to zero at
  502, alive at 795 HP.

Own submitted totals independently rise 0 to 13 to 21. These are not consecutive
completed laps: intervening lap 3 fails source 9 at 353, earns unrelated source-16
gold at 361 and fails source 11 at 420. Physical owner holds are sampled at
272–277/307–312 and 425–430/457–461. Official raw full-state reward transitions,
geometry and personal deltas establish capture and conversion. The observer's
default A-view visibility fields are excluded from B-side knowledge claims.
This proves learned replacement, retained order and a cashable continuation;
shared damage and rewards do not establish solo clears. A separate exact-source
Vesper A receipt verifies source-6 failures at 63/157, replacement by source 8 at
240 and a 53-gold solo capture at 249, including his actual gold bonus.

The active default is retained as a healthy, distinct experimental losing
controller. `.cache/arena/sweeps/h12-07-complete` covers all 40 pool maps, four
opponents and both sides, with 320 identical paired keys per arm. Active 111–209
versus frozen 162–158; flips +13/−64, exact McNemar p=3.02e−9, paired gem-margin
difference −262.7 +/-26.0 SE. Active wins: Fundamentals 10/80, main 0/80, Tempo
30/80, Concentric 71/80. Main is one quarter of the panel, snapshot
`001974b90521`. Final Tempo is `c54228dd4385`, changed externally from the earlier
`8cbde6af1a2e`; only same-run paired results support the bundle comparison. The
complete bundle includes draft, purchases and mechanical repairs, so the margin
does not isolate circuit learning.

`.cache/arena/runs/h12-07-complete-confirm` covers 17 fresh legacy maps, both
sides and Starter, Banked Vanguard and Bargainer: 99/102 (31/34, 34/34, 34/34).
All 320 final active pool games and all 102 fresh active games have zero faults,
exhaustion, error lines, zero-clear flags or suspension. Maximum ordinary-turn
fuel is 98,259 pool and 93,450 legacy. Thus all 422 required active games are
clean on that coverage; the pool maximum is close to the budget and is not a
proof for every state. Frozen control retains eight historical zero-clear flags.
Opponent health is separate: opponents in active pool games total 74 exhaustion
events and 19 zero-clear flags; fresh opponents total 13 exhaustion events,
one at-budget turn and 12 zero-clear flags. Neither set has opponent faults or
logged errors.

Capped final pool logs record program planning in all 320 games, stop payouts in
317 games on all 40 maps, personal bank completion in 291 games on 38 maps, and
probe payouts in 243 games on 39 maps. The 36-event-per-actor cap makes these
lower bounds, not opportunity rates or counts of complete productive circuits.
`.cache/arena/sweeps/h12-07-complete/circuit-scan.json` contains the scan. The
native receipt above separately establishes repeated ordered work and cashable
continuation. `arena test heuristics/learned-circuit` passes 2/2; final strict v3
profiles pass all seven policies. Default-active and explicit-off builds pass.

Binary identity is read from the actual final sweep manifest and built snapshots,
not inferred from a preflight build:

| Arm | Snapshot | WASM |
| --- | --- | --- |
| Same-run frozen control, original default 0 | `077221f18b30` | frozen hash below |
| Measured active override 1 | `152476a1ded1` | active hash below |
| Promoted default 1 | `7b64dffed801` | identical active hash |
| Explicit override 0 after promotion | `9a52eb9f5cbe` | identical frozen hash |

Active SHA256:
`9067720cab11fb25dcd394f8d30f102f92f567da0e4160d6208bb36d5d74b9e0`.
Frozen SHA256:
`ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`.
Both raw and native-injected WASM match in each pair. Source identities differ
because override metadata participates in snapshot identity. Machine-readable
proof: `.cache/circuit-review/promotion-proof.json`.

Repeated income can still be poor income: a personally productive stop may be
distant, shared, or unavailable next lap. Budget-omitted detail can conservatively
cause an unnecessary return before the next direct execution epoch. Local escape
is not a complete safe-route proof. Nothing here establishes solo clears,
optimal circuits or a strength improvement. This is a research default, not a
release candidate.

Next-bot lesson: private recurrence can physically learn and cash the same order
while still losing. Include lethal unpaid work in loss feedback, distinguish
deliberate financial/recovery pauses from failed visits, and separate personal
payout from team damage and shared clearing. Knowing that a stop paid does not
identify who will clear it or hold it next time; recurring disruption and sharing
are evidence for coordination work rather than another circuit coefficient.

## Main v51 capital-phase experiment, 24 October 2026

`CIRCUIT_CAPITAL_PHASE=0` preserves the incumbent executable: new OFF snapshot
`8fbae1bb033e` and incumbent `7490c7bb39cf` have identical WASM SHA256
`51cc49607f5143731c3dfd5e20880bc154f45ecc3f3e2bc95d4cda40133f7a78`.
The experimental ON snapshot is `65568a598fc2`. It returns a non-Tavi actor
when the wallet can complete the next opening equipment phase, using the
engine's shard price and combination count. After both opening phases it keeps
the existing 300-gold rule. Capture and bank precedence, field shopping and the
same circuit's recovery/resume machinery remain in place. The production helper
and program-resumption fixtures pass with common and existing Circuit cases
(2/2 fixture cases).

The hypothesis was delayed equipment growth, not poor conversion: Rows A against
v44 collects 591 and banks 579 without deaths or PvP damage. Native inspection
narrows that hypothesis: Vesper first has 166 gold at round 88 but ordinary
recovery already buys 150 gold of equipment at round 122; Marcella's 154 gold at
217 is spent at 224. A fixed 300-gold finance trigger is a real constraint, but
these actors do not carry their first equipment investment for hundreds of turns.

`circuit-strength/capital-smoke` measures OFF/ON against frozen
`anchors/main-v51@d7ba402feec8` on Rows, Fragment, Ambush, Packs, Elkring, Great
Wall, Big World and Gold Rush, both sides: 32 games, all complete and both teams
without recorded health flags. Each arm wins 0/16. Paired gem-margin change is
-18.375 +/- 12.981 SE, with no win flips. Rows A banking is unchanged at 548;
Rows B falls 641 to 588. Elkring A improves 107 to 218, while the complete paired
panel does not support a general strength gain. Keep the new rule OFF. This
16-key arm provides no heldout, universal health or population-admission claim.
The complete `circuit-strength/capital-floor51` follow-up then loses all 80 games
on the 40 pool maps, both sides, with both teams free of recorded health flags.
It does not meet the requested one-win floor. Further finance-threshold tuning
is dropped in favour of the separate paid-stop coverage mechanism below.

The next discrimination should examine persistent low-yield or shared paid stops
at a lap boundary. Positive income currently forbids replacement indefinitely;
that is a separate production constraint, not a reason to retune the finance rule.

## Private paid-yield renewal component

The Rows A native production decomposition is concrete: the incumbent captures
the 12/19/26-gem sources 18/17/2 times, while frozen v44's mirrored sources pay
20/19/16 times. Of the 426-gem collection gap, 364 comes from missing repeated
production of the third source. All four actors initially learn stops 0,2.
Tavi repeatedly receives only 4 of 12 and 6 of 19 nominal gems, with three payout
recipients, while retaining both personally paid stops.

`CIRCUIT_YIELD_RENEWAL=0` keeps the incumbent. ON records actual own capture
deltas and active visit time, and two completed paid-lap durations. At a normal
lap boundary it may replace one repeatedly low-share paid stop when an actually
admitted, affordable outside quote offers a strictly better discounted rate.
Integer split rounding does not classify a fair two-holder share as a failure.
The other stop and its order survive; missing, unsafe, crowded or uncompetitive
quotes keep the current program. The actor's existing roster ordinal staggers
eligibility across its private completed paid laps, without an idle wait or team
memory. One outside quote follows the committed current stop in the existing
feature budget. Its public pre-screen is not proof of eligibility.

The tested ON `322463a49557` (also current-equivalent `6ba4e63383d5`) has WASM
SHA256 `7f3494edde59349dac03c4136fcd9fe907996aba7d47c43399e15d8b50b09ffa`.
All OFF builds retain incumbent SHA256 `51cc49607f5143731c3dfd5e20880bc154f45ecc3f3e2bc95d4cda40133f7a78`.
The common and Circuit production fixtures pass 2/2, including all roster phases,
continued ordinary work outside the phase, missing/unsafe/crowded quotes,
mid-lap refusal, one replacement, personal payout and later banking.

`circuit-strength/yield-rows51` compares both arms on Rows, both sides, against
frozen Main v51: four complete games, all recorded health clean, no wins in
either arm. Banking changes A 548 to 590 and B 641 to 688; paired margin change
is +44.5 +/- 2.5 SE. An independent official comparison finds 94,814 exposed world
snapshots equal from start through Tavi's R686 owner-start, before the renewal.
Private memory, fuel and logs are outside that comparison. The next ready state
first differs at 687; ON selects source 4, receives 26 gems/120 gold at 752 and
banks at 791, then receives the same source's reward again at 881. Tavi is the
sole payout recipient in those events; this is not a solo-clear claim.

Against the same Main v51, third-source captures rise 2 to 7 while the older
sources fall 17 to 15 and 16 to 14. This establishes a real contribution with
paid continuation, not sufficient strength: `circuit-strength/yield-floor51`
still loses all 80 pool/both-side games, with both teams free of recorded health
flags. Keep the prototype OFF. Main has since advanced to v52; these historical
v51 tests cannot qualify a new keeper against that target.

The first failed phase is now observed rather than assumed. A compact,
diagnostic-only packet at Tavi R222 has eligible=1, two paid laps, HP 820/1050,
five monster shards and two jungle shards. Source 4 is actually admitted but
not affordable: 4,290 HP, incoming Strength 60, estimated 46.6 work rounds,
26 travel and 26 altar distance. The committed sources and the inspected outside
source 3 also fail full-solo affordability; existing committed/high-health partial
work continues. No missing quote or visible crowding explains that refusal.
The compact observer is health-clean; an earlier formatted packet exhausted once
and is superseded. Independent exposed-state prefix comparison passes through
R222 before the compact packet. Diagnostic/truncated draws are not strength
evidence. The next question is when an affordable outside continuation first
exists, and whether a previously earned renewal opportunity should survive a
failed quote; no safety threshold is relaxed here.

`CIRCUIT_EARNED_RENEWAL=0` tests that narrower opportunity-expiry question.
With both renewal flags ON, a roster-phase opportunity survives a failed quote
until one replacement actually executes or a completed lap has neither current
stop with poor personal payout history. Cancellation is evaluated at the normal
lap boundary, including when the poor member changes during that lap.
Permission belongs to the actor's circuit, not
one source ID; this is a policy assumption, not a guarantee against herding.
Every intervening lap continues ordinary ordered work. A later replacement still
requires the same actual admitted, affordable, uncrowded and better-rate quote.
The old OFF and unlatched renewal executables remain byte-identical to their
recorded hashes. Production fixtures pass for all five roster identities,
missing/unsafe/crowded/uncompetitive quotes, later off-phase execution, actual
payout and bank continuation, and cancellation after fair personal receipts.
An initial synthetic continuation was too fast to preserve the native measured
rate comparison; correcting its travel/work time made the fixture discriminating
without changing the production rule.

The paired Main v52 Rows experiment has four complete, recorded-health-clean
games: both arms win 0/2, paired margin changes +37 +/- 24 SE. Tavi first renews
at 686 in both arms, refuting an earlier safe Tavi continuation in this witness.
The earned arm instead adds Vesper's off-phase renewal at 784 (paid lap 8,
ordinal 0). Its first different ready position is 785; an independent official
comparison matches 108,058 exposed world snapshots through its 784 owner-start.
It receives 13 gems/80 gold with Tavi at 861, banks at 900, and receives the
same shared reward again at 953. This verifies executed retained permission.
Source 4 still pays seven times in both arms, while source 2 falls 15 to 14;
own total collection falls 647 to 628. Saved permissions can execute together
despite zero visible allies at selection. This is evidence about private
execution bunching, not an increase in third-source coverage or earlier Tavi
production. No further credit or phase tuning is justified by this witness.
The separate `circuit-strength/earned-floor52` completes all 40 pool maps, both
sides, against `anchors/main-v52@4aaf587c22cc`: 0/80 strict wins, no truncated
games, and no recorded health flags on either team. It fails the requested
one-win floor. All prototypes remain OFF, preserving incumbent WASM bytes;
the paid-yield and retained-opportunity components are finished verified work,
but this bot is unqualified. The next useful allocation would discriminate
physical productive capacity or private execution bunching rather than another
credit, phase or share threshold. No heldout strength claim is made. All eight
answered experiments are closed; one queued diagnostic was also dropped and
closed. Compact evidence lives in `results/population/circuit-current-strength`.
