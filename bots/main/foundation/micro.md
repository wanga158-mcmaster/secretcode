# Micro control: preserve commitments, refresh continuations

Small action sequences matter because a champion turn contains several distinct allowances.
Choosing a spell target does not choose every later target, and finishing one target does not
necessarily finish the turn. This page extends [M23-M25](mechanics.md#combat-and-abilities) with
current-engine mixed-camp evidence and tested ground-center selection. The conservative
free-center research arm applies an extra hit in normal play but fails fresh strength
confirmation; it stays off. This page does not establish a contender strength improvement.

## Current evidence and reproduction

The native probe is `bots/contenders/_micro-pack/src/main.cpp`; the default-zero
`micro_followup` knob changes only continuation after a lethal action. Each side drafts Tavi,
Ryth and Vesper. Each champion reaches a separate cardinally connected Gold/Wolf group on open
ground. The Gold is killed at round 30 or 31; Wolf remains alive and in basic range.

Eight 35-round native games cover OFF/ON, both subject sides and both round parities. Each game
exercises all three sequences, so the saved replay verifier checks 24 sequences, including 12 ON
continuations and their 12 OFF controls. The other side runs OFF. Both maps are laboratory open
arenas, not pool maps. CLI/SDK 3.0.5, engine 1.0.3; exact source hashes, bundle identities, replays,
native fuel streams and query logs are retained in
[`results/contenders/micro/packing-3.0.5-r2/packing.json`](../../../results/contenders/micro/packing-3.0.5-r2/packing.json).

The independent current pinned official visualiser observer decodes all eight saved windows.
[`native-verification.json`](../../../results/contenders/micro/packing-3.0.5-r2/native-verification.json)
checks the champion actor identity, recorded action kind and target coordinate, Gold death,
survivor health delta, and reset allowance. OFF/ON queried actor states match exactly immediately
before the lethal action for all 12 matched champion cases. This equality covers represented
public queries, not private fuel or every native world byte. The observer's camp identity is
inferred from the unique living occupant at the recorded coordinate; raw history does not name
a target unit ID. Its complete traces and decoder provenance remain alongside the verification.

Use the Python printed by `arena env`; outputs must be fresh:

```
python bots/main/foundation/tools/micro/packing.py results/contenders/micro/NEW-RUN
python bots/main/foundation/tools/micro/verify.py results/contenders/micro/NEW-RUN
```

`packing.py` uses arena builds and its shared machine slot pool; `verify.py` reads saved replays
through the current contained observer and starts no games. Native maps/specifications live in
`.cache/contenders/micro/maps/`. The original `packing-3.0.5` run is retained as failed: Team B
had one setup-only known-occupied-destination error per game. The repaired probe refuses
`move_towards` while another visible champion occupies its goal. No failed observation was erased.

## U1. A lethal cast can leave a useful basic attack on a different group member

Claim: Killing the spell target does not consume an otherwise available basic. A fresh survivor
can receive that basic in the same owner turn. The cast's movement restriction still applies.

| Sequence | Applied lethal action | Available immediately after it | Applied ON continuation | OFF continuation |
| --- | --- | --- | --- | --- |
| Tavi slot 1 CC, then Wolf basic | Gold 120 -> dead; Wolf 1,350 unchanged | Move false, attack true, cast false | Wolf 1,350 -> 1,305 | No basic; Wolf remains 1,350 |
| Ryth slot 0 ground AoE, then Wolf basic | Prechipped Gold 75 -> dead; Wolf 1,350 -> 1,250 | Move false, attack true, cast false | Wolf 1,250 -> 1,205 | No basic; Wolf remains 1,250 |

Evidence: Eight native games and independent saved-byte replay observations above, with these
two sequences in all four keys per arm. Current stat values are queried by the engine: Ryth
Strength 45/Focus 100, Tavi Strength 45/Focus 110. These numbers calibrate this bundle; their
mechanical consequence is the remaining attack allowance, not a hardcoded damage threshold.

Rests on: M23, M25. Status: verified native engine 1.0.3 on the stated two mixed-camp geometries.
Recheck: action allowances, cast movement attributes, camp lifetime/API changes, or a changed kit.

Consequence: An executor must refresh its continuation after the cast. A stale pre-cast basic
target can be dead even while the turn has productive work. Retarget only within the original
decision's permitted scope, after live availability, visibility, life and range checks; the
mechanism alone does not authorize a new engagement or movement. Whether a production bot reaches
this conjunction, chooses it, and benefits over a complete clear/capture/bank cycle is separate.

## U2. A lethal ranged basic can precede reset and a basic on another member; reset does not restore movement

Claim: Vesper may kill one member with her ordinary basic, reset the now-spent attack, then
attack a different living member in range in the same turn. The reset restores attack alone.

Evidence: Vesper prechips Gold twice (120 -> 65 -> 10). On the selected owner turn her basic
kills Gold and leaves movement false, attack false, cast true. ON casts slot 0; the independent
native query then reads movement false, attack true, cast false. Her survivor basic takes Wolf
1,350 -> 1,295. OFF takes no reset or survivor basic and Wolf remains 1,350. All four side/parity
keys in each arm reproduce this sequence; the 55 damage is current runtime Strength.

A separate [ordinary Timetable application](../../../results/contenders/timetable/independent/4a9db07ca948/local-prerequisite/micro-audit.json)
now executes Basic -> Reset -> Basic without an intervening owner end. Frozen source
`4a9db07ca948`/ON `25a8f73df008`, YBR A/passive: at R41 Vesper at `(8,21)` hits the
same living wolf at `(11,24)` for HP `1350 -> 1295 -> 1240`. Reset changes attack false
to true and cast true to false, cooldown 0 to 8; movement remains false. Her R42 ordinary exit
is a later turn, not restored reset movement. Native inputs 494/495/496/497 locate the compound,
while actor 3/phase 10 remain continuous. The complete R40-42 query reports zero failures;
the retained selected-field audit pins the archive, module and decoder dependencies.

This is one actual ordinary-controller compound, not a universal word, fresh strength result
or an identity between owner turns and native input counters. Its full passive game banks 242
with 13 clears on one key; later source and current-main qualification remain separate.

Rests on: M23, M24. Status: verified native engine 1.0.3 for Vesper in this geometry.
Recheck: reset payload, ranged movement coupling, action allowances, or target lifecycle.

Consequence: Schedule resets after a successful spent basic and choose the next basic target
from current living candidates. `ability.is_ready()` alone does not express reset-specific
legality; its SDK implementation checks cooldown and `can_cast()`. A reset cannot repair a
ranged step/shot plan by restoring movement. This probe does not test dash/reset combinations,
enhanced resets, champion kills, or additional casts after a reset.

## U3. A dead camp handle's state and its live information have different query contracts

Claim: The SDK permits `camp.state()`/`is_alive()` for the last occupied tile of a dead camp when
that tile is personally visible. `camp.health()` and `position()` use live camp information.
`camp.is_visible()` calls that live-information API and returns false for either not-in-vision
or not-found; its false result does not establish that the spawn tile is hidden.

Evidence: Current `sdk/cpp/cambc.h` camp implementation, especially `state()` and `is_visible()`.
The native probe queries the cached killed Gold's `is_alive()` successfully after all 24 lethal
actions and reads no dead Gold health or live position. The broader contracts are source facts;
this study deliberately does not dispatch an invalid dead-target attack or test hidden dead
handles. No claim is made about every engine error's cause.

Rests on: S1-S24's SDK observation discipline, U1-U2. Status: SDK contract plus the stated native
dead-state queries on engine 1.0.3. Recheck: SDK or camp entity lifetime changes.

Consequence: If a caller needs to clear its known-dead member mask, first establish visibility
of the cached spawn tile and query camp state, then access live health only when alive. A guard
that starts by requiring the dead handle's live `is_visible()` can skip the very dead-state
update it intended to perform. Keep hidden state unknown rather than inventing a death.

The later [corrected Warband r4 reductions](../../../results/contenders/warband/capital-work/capital-ybr-r4/source-audit/corrected-reductions.json)
retain the actual [R40-75 native body](../../../results/contenders/warband/capital-work/capital-ybr-r4/source-audit/native-40-75.json)
and final footprint/ward interpretation. The decoder uses its supplied padded chunk stride six
and integer chunk keys. Earlier guessed-stride-five, hidden-neighbor and live-member assertions
are retracted. Actual member kills followed by personally visible dead spawn tiles support the
lifecycle query; a source observer gated on live `is_visible()` can miss the DEAD cache update.

This is source/native-consistent evidence for the adapter boundary, not a recording of its
private cache or selected group. False live-info availability means neither DEAD nor hidden.
Query the visible lifecycle separately, record zero HP only from confirmed death, and retain
unknown when authority is missing. A historical positive HP value is not fresh residual work,
and arriving within a static attack radius does not itself establish personal sight.

Tool maintenance: `packing.py` and `move_reset.py` now select the native fuel header's ordinary
budget for the queried side (A index 3, B index 7). The historical source hashes/results remain
unchanged. Both budget headers in all twelve retained repaired packing and move/reset games
are 100,000, so this correction does not change their reported peaks or budget-hit conclusions;
it prevents future asymmetric-budget runs from silently using side A's allowance for side B.
The [maintenance check](../../../results/contenders/micro/fuel-reader-maintenance.json) retains
all 24 header observations, current reader hashes and four asymmetric-budget expression checks;
it starts no games.

## U4. Warband's actual camp executor applies the fresh basic in a reachable native fixture

Claim: The current `Bot::attack_camp()` implementation with `camp_retarget=true` can preserve its
chosen camp-group commitment after a lethal cast and dispatch one basic at a living member. Its
default-OFF implementation leaves that basic available in the two observed kits.

Evidence: The owner retained four 56-round native games of
`contenders/_warband-production-probe`, OFF/ON on both sides of the same open mixed-camp map.
The fixture includes the actual production `act.cpp` and its dependencies. Ordinary movement and
28 basic attacks put the Wolf at 90 HP before the round-54 caller; Gold starts at 120 HP.
The fixture refreshes bot caches from live queries and calls the executor directly. It does not
run the normal strategic planner or inject game health/effects. OFF snapshot
`01adfe3ea067`, ON `db515f4f1c31`; the original owner evidence is
[`production-packing-r2/packing.json`](../../../results/contenders/warband/production-packing-r2/packing.json).

An independent current-engine observer reads the saved round-54 windows of all four games,
checking eight Tavi/Ryth caller sequences and four matched OFF/ON queried actor states:

| Actual executor | Shared native cast | OFF action after cast | ON action after cast |
| --- | --- | --- | --- |
| Tavi | Wolf 90 -> dead; Gold remains 120 | No basic; attack available | Basic Gold 120 -> 75 |
| Ryth | Wolf 90 -> dead; AoE also takes Gold 120 -> 20 | No basic; attack available | Basic Gold 20 -> dead |

Both effects occur in the same owner turn, with the native champion actor identity and recorded
target coordinate checked. Complete observer outputs, code/decoder identities and the original
failed owner-verifier hash are in
[`warband-production-r54/production-verification.json`](../../../results/contenders/micro/warband-production-r54/production-verification.json).
No new games were played for this independent check. Reproduce only the read-only verification
with `tools/micro/production.py OWNER-RUN FRESH-OUTPUT` from this foundation directory, using the
pinned Python runtime.

The owner's original broad verification failed and remains failed. It mistook Ryth's AoE splash
for a control retarget and expected an extra Vesper basic after her first basic/reset/second basic
had already spent her allowances. The independent consumer deliberately checks the two observed
kit-specific contributions above; it does not repair history or infer a Vesper third attack.

Rests on: U1-U3, M23-M25. Status: conditional contribution of the actual production executor
verified in native engine 1.0.3, one lab geometry, both sides, round 54 only.
Recheck: executor/SDK changes, changed action packing, different readiness or target lifecycle.

Consequence: U1 is now witnessed through a real contender execution path, not only a standalone
SDK sequence. The remaining question is how often the normal planner reaches this conjunction
and whether the continuation helps complete clear/capture/bank cycles under full observation,
navigation and fuel load. This fixture does not establish those answers, pool applicability,
full-game health or strength. In particular, Ryth's Gold death is not evidence of capture or bank.

## U5. Vesper can step, reset and basic without an earlier basic attack

Claim: A ranged step spends Vesper's ordinary attack allowance sufficiently for her reset to be
legal. A prior actual basic is not necessary. This narrows M24's original wording: the tested
illegal case was resetting while an attack remained available.

Evidence: `contenders/_micro-move-reset` drafts Vesper and two inactive fillers. It makes no
basic attack during setup. At round 30 Vesper starts at Chebyshev distance 4 from Wolf, steps
to distance 3, then ON resets and attacks. Four 33-round native games cover OFF/ON on both sides.
The independent current official observer reads rounds 1-30, verifies no earlier Vesper basic,
and checks these action-boundary flags and effects:

| Native boundary | Move | Attack | Cast | Wolf HP |
| --- | --- | --- | --- | --- |
| Before step | true | true | true | 1,350 |
| After step | false | false | true | 1,350 |
| After successful slot-0 reset (ON) | false | true | false | 1,350 |
| After basic (ON) | false | false | false | 1,295 |
| End of observed OFF turn | false | false | true | 1,350 |

The OFF/ON queried before-step actor states match on both sides. OFF source snapshot is
`29c4cb359fcd`, ON `28a05c3bf327`; the new `move_reset` knob defaults to zero. Both successful
casts start their eight-tick slot-0 cooldown. Ordinary subject fuel peaks at 51,492/100,000,
with no ordinary budget hits or logged errors in these bounded games.
[`move-reset-3.0.5/move-reset.json`](../../../results/contenders/micro/move-reset-3.0.5/move-reset.json)
and its [independent verification](../../../results/contenders/micro/move-reset-3.0.5/native-verification.json)
retain the games, live queries, fuel and exact source identities.

Current guard/control supplement: a separate
[engine 1.0.3 witness](../../../results/contenders/mechanics/vesper-move-reset-current/witness.json),
source `a752d57c134d`, uses ordinary Elkring A against a frozen passive for three truncated rounds.
At round 1 premature reset throws code 26 while preserving `(M,A,C)=111`, both cooldowns zero and
position `(1,1)`. An actual ordinary move to `(2,1)` yields `001`; reset then succeeds with `010`
and slot-0 cooldown 8. These are source-logged SDK state snapshots. The source contains no basic
command, so this independently binds the permission guard/restoration only, not a whole
move/reset/basic hit. It has no independent replay-state observer or full-game health/strength
qualification; the earlier complete-hit witness above retains its own source and coverage.

Reproduce with the pinned Python and a fresh output:

```
python bots/main/foundation/tools/micro/move_reset.py results/contenders/micro/NEW-RUN
python bots/main/foundation/tools/micro/verify.py --move-reset results/contenders/micro/NEW-RUN
```

Rests on: M23-M24, U2. Status: native engine 1.0.3 verified for Vesper, one open lab geometry,
both sides, one round parity. Recheck: ranged allowance coupling or reset validation changes.

Consequence: With a ready reset and a committed target, Vesper can combine an ordinary approach
step and one basic on arrival. That spends the turn's single cast and its reset cooldown; it
does not also restore movement or permit a second cast. Planning her arrival as always one turn
later than melee is wrong under this readiness conjunction. This is not proof for other reset
kits, enhanced resets, a normal contender planner, complete game health or strength.

Normal-planner follow-up: Compound's first reset-only action pilot independently reaches the
same sequence on `Concentrated Wealth`, side A against starter, round 105. Vesper 0 steps
(38,34) -> (37,33), resets slot 0 and basics camp 12 at (34,30), 1,350 -> 1,295. The matched
control steps and ends its turn. Selected actor, personal vision, ability and nearby public
projections match before and after the step. Both arms have applied two basics and leave the
camp at 1,240 HP by the end of round 106: this proves earlier damage, not extra two-round
damage or an improved complete cycle. Exact sources `d8a9c953080b` and `a1cba3100d21`, engine
1.0.3, traces and limits are in
[`action-reset-observation.json`](../../../results/contenders/compound/action-reset-observation.json).
This is one historical reset-only source pair, not the separate dash pilot or a fuel/strength
qualification of Compound's subsequent source.

## U6. A normal v40 Ryth cast exposes an omitted free-ground AoE center

Claim: Main-v40's enemy AoE selector enumerates enemy-occupied centers. A retained normal match
reaches a ready Ryth cast where a free, personally visible floor center covers an additional
enemy while preserving the two enemies actually hit. The alternative was not dispatched.

Evidence: Saved v40 match `e4c1e4a2-13eb-4c88-8120-d9d6cd3489a5`, `00eight`, own side A,
round 30/input 814. Ryth 2 stands at (15,12), has Focus 100, slot-0 cooldown zero and can cast.
Its actual ground cast at (17,9) applies 100 HP loss and a stun to Vendetta 5 at (17,9) and
Varg 6 at (16,9). Both are personally visible. Ren 9 at (16,11) is also personally visible,
alive with 528 HP, no shield and no stun immunity. Its existing stun has timer zero and it has
already completed its turn.

The alternative center (16,10) lies at cast distance 2, is personally visible, in map, free of
champions/camps, and ordinary non-wall/non-hole/non-altar floor. Its radius-one square contains
all three enemy positions. No enemy-occupied center covers more than two of these visible foes.
The exact saved native summary's map hash matches the current geometry export's map bytes; this
does not rely on a map-name/dimensions guess. The complete round, including all champion state
transitions, is decoded by the current pinned 1.0.3 observer with zero query failures.

[`aoe-midpoint-v40/opportunity.json`](../../../results/contenders/micro/aoe-midpoint-v40/opportunity.json)
binds the replay, full round, v40 registry entry, native summary, exact map, SDK and frozen
`anchors/main-v40/src/combat.cpp`. Its pure consumer
`tools/micro/aoe_midpoint.py TRACE SUMMARY REGISTRY MATCH ACTOR FRESH-OUTPUT.json` reconstructs
action-boundary states and checks the strict enemy-coverage difference. It also checks the
literal frozen selector score: best enemy-occupied center 320, free center 480 under that
selector's `min(HP, Focus * modifier) + 60` per-victim formula. These are heuristic units, not
measured damage, denied turns or utility.

The search was bounded and selected: three existing v40 combat windows (`00eight` rounds 20-65,
`01elk_arena` 25-70, `00contest` 50-90), eight own Ryth slot-0 casts, six affecting personally
visible enemies, one strict free-center coverage candidate. The windows were selected for
early Ryth PvP activity after a read-only screen of retained local summaries, not sampled
randomly. They and the selection records are retained under the evidence's `scout/` directory.
This is one actual opportunity, not an estimate of population frequency. No new game was played.

Limit: The actual cast also damages an awake camp at (18,10) by 100. The free center drops that
camp collateral. Hitting Ren can also trigger its retaliation. Thus the alternative is not
proved to dominate total value or improve the fight. Native alternative legality/effect,
incremental stun denial, future state, game outcome and fuel cost of a new selector remain
unmeasured. Published ground targeting, readiness, range and observed floor/vision prerequisites
support this candidate; this study does not certify arbitrary wall/hole centers or treat the
SDK's range-only mask as a complete legal-action oracle.

Rests on: corrected C14, M25-M26, M34. Status: measured normal-match opportunity and pure
source-consistent selector omission, engine 1.0.3; alternative effect remains unexecuted.
Recheck: selector candidate generation, kit radius/range, visibility or ground-target rules.

Consequence: A contender can enumerate the small legal ground-center region, rather than only
unit positions, then evaluate every affected champion and camp, retaliation and pending control.
Warband can implement this inside its own already-authorized strike selection; it need not
change movement or copy main's code. A pure selector test should include these exact points,
prove occupied-center coverage at most two versus three at (16,10), and preserve the separate
camp/retaliation cost. A native contender caller must next demonstrate actual selection/effect
and bounded fuel before a paired full-game experiment can support strength.

### Frozen-v40 research follow-up: applied extra hit, failed strength confirmation

The private `contenders/_micro-freecenter` fork tests a conservative refinement of the already
selected enemy AoE. It is research code, not a fourth submission. `free_aoe_centers` defaults
to false. OFF source `a7ef560b1bde` and ON `dbda67307612` are bound by
[`preservation.json`](../../../results/contenders/micro/freecenter/preservation.json).
Both OFF raw and injected modules exactly match the frozen-v40 baseline, SHA-256
`7fe8d557b5539d8b2caa40a842d266352f545badc3a02c22f6a131a4ad17c18b`.
The baseline source closure and toolchain are retained in
[`baseline.json`](../../../results/contenders/micro/freecenter/baseline.json).
These are historical v40 controls. The live bar subsequently became `main-v41`,
`main/main@db6ca9bc4e39`; no v41 strength comparison is implied.

The bounded search adds personally visible, unoccupied floor centers from the SDK's queried
range mask, then rejects walls, holes, altars and teleport reservations. This is a deliberately
restricted subset of ground targeting, not proof that ground AoE requires free floor or that
the range-only mask certifies legality. It retains every original enemy victim, requires an
additional enemy and positive old-heuristic gain after lost camp contribution and added Ren
retaliation, and refuses newly touched sleeping dangerous camps or unknown camp footprints.
It changes neither engagement admission nor the selected slot. Entry and iteration fuel
guards bound the work. Ten pure geometry/cost cases pass `arena test contenders/_micro-freecenter`.
The score still uses v40's `min(HP, Focus * modifier) + 60` duration bonus, including its
overcredit for immune or otherwise unproductive control. Camp and retaliation charges are
heuristics, not a calibrated complete fight value.

First, four bounded native actual-`fight_enemies` caller games cover OFF/ON on both sides.
At round 40 OFF casts at (17,9); ON chooses (17,10). Both hit the original two enemies and
damage the Wolf at (18,10), 1,350 -> 1,250. ON also applies 100 damage and stun to Ren and
receives 20 retaliation damage. Thus a different center can retain the camp collateral that
the original opportunity's (16,10) candidate dropped. Before-cast queried actor states match
in both pairs. Ordinary peak fuel is 55,402/100,000, with no logged errors or exhaustion in
these four 43-round mechanism games.
[`caller-r2/verification.json`](../../../results/contenders/micro/freecenter/caller-r2/verification.json)
retains independent native effects and decoder identities. The failed first setup and the
original r2 verifier's wrong expectation of center (16,10) remain preserved; the pure corrected
consumer verifies the actual better center without rerunning games. Reproduce the actual caller
with `tools/micro/freecenter_caller.py FRESH-OUTPUT`, or check saved r2 replays with
`tools/micro/freecenter_verify.py OWNER-RUN` from this foundation directory.

Second, a saved full normal-planner pool pair on `00eight`, side A against `anchors/main-v40`,
shows an applied contribution at round 147/input 3228. Ryth 2 at (27,12) casts at (27,10) in
OFF and (28,11) in ON. Both casts reduce Heart 5's shield 222 -> 122 with stun and Marcella 6's
HP 900 -> 800; Marcella is immune to stun. ON additionally reduces enemy Ryth 7 at (29,12)
from 900 -> 800, applies a one-tick stun and makes its move/basic/cast allowances unavailable.
Camp effects match. All ten initial queried champion states, represented camp/group/score
state and the preceding 218 observed events in the complete rounds 146-147 window match.
The center is the first represented action difference in that window, and the before-cast
queried actor states match. This is evidence of normal-planner selection and applied effect,
not private fuel/memory equivalence, a whole-game identical prefix or an explanation of all
outcome flips. Unqueried camp wake/stun state remains outside the equality claim.
[`normal-pilot/normal-verification.json`](../../../results/contenders/micro/freecenter/normal-pilot/normal-verification.json)
binds both saved replays and the complete current-engine traces. The pure consumer
`tools/micro/freecenter_normal.py NORMAL-PILOT-DIRECTORY` checks them without games.

Third, paired full-game evidence does not establish a strength gain. Both panels use the same
unchanged OFF/ON source pair, all maps in each set and both sides, CLI/SDK 3.0.5, engine 1.0.3:

| Panel | Opponents | Matched keys / games | OFF wins | ON wins | Win gains / losses |
| --- | --- | ---: | ---: | ---: | ---: |
| 40 pool maps | v40 and starter | 160 / 320 | 120 | 126 | 8 / 2 |
| 17 fresh legacy maps | v40 and main-hunter7 | 68 / 136 | 46 | 45 | 3 / 4 |
| Combined | stated historical anchors | 228 / 456 | 166 | 171 | 11 / 6 |

Pool v40 results are 40 -> 46 wins out of 80 keys; starter stays 80/80. Fresh legacy v40 is
17 -> 16 out of 34; hunter7 stays 29/34. The pool lead fails fresh confirmation. Combined exact
paired win-test p = 0.3323; paired gem-margin change is +29.34 with standard error 24.01.
All own aggregate faults, exhaustion, logged errors, stalls and budget-hit counts are zero in
both panels. Peak ordinary fuel is OFF 95,087, ON 94,956 across them. These health observations
cover the stated source/game sets, not universal liveness or a release qualification.
[`assessment.json`](../../../results/contenders/micro/freecenter/assessment.json),
[pool report](../../../results/contenders/micro/freecenter/pool/report.md) and
[fresh report](../../../results/contenders/micro/freecenter/legacy/report.md) retain exact run
identities, paired ledgers and health. The switch remains OFF; no release or online action was made.

Finally, a bounded coverage audit tests whether preserving original victims is hiding positive
trades under the same score. It reconstructs all actors and event kinds in the three original
selected windows: eight own Ryth slot-0 casts, six enemy-centered casts. Only `00eight` round 30
has any free-floor centers exchanging original victims for new ones: six centers, all with
enemy-score delta at most zero even before camp/retaliation costs. No positive same-score trade
blocked by preservation is found. All eight cast actions and before-states match the earlier
filtered traces; 16,038 reconstruction checks pass, complete windows have zero filtered events
and zero query failures. The audit starts no games.
[`victim-audit/audit.json`](../../../results/contenders/micro/freecenter/victim-audit/audit.json)
retains exact candidate states, centers, f32 score calculations, source text/hashes and three
compressed full traces. It refuses newly touched static camp footprints and bounds lost-camp
value because awake/stun state is absent. Dynamic teleport reservations are not decoded; this
uncertainty is explicit. These preselected windows neither estimate general veto frequency nor
prove the existing score expresses useful target value.

### First divergence in two failed-confirmation keys

A separate saved-replay audit checks two of the four fresh legacy confirmation keys that
change from an OFF win to an ON loss. Both are side B against the same frozen v40 opponent and
retain the original OFF/ON source pair. Selection is conditioned on those losses, not random.
Cheap complete owner-turn tracks first localize a state difference; complete all-actor/all-kind
native prefixes then establish the first represented action difference, rather than treating
rounded ready-turn tracks as an action trace.

| Legacy key | First changed action | OFF center | ON center | Equal preceding represented events |
| --- | --- | --- | --- | ---: |
| Fragment / B | Round 322/input 4170, Ryth 5 slot 0 | (3,12) | (3,13) | 25,068 |
| Eight / B | Round 84/input 1868, Ryth 7 slot 0 | (19,12) | (19,11) | 10,318 |

In Fragment both casts damage Marcella 1, 990 -> 890. ON additionally damages enemy Ryth 2,
640 -> 540, and applies a one-tick stun with move/basic/cast unavailable. In Eight both damage
Marcella 1, 1,200 -> 1,100, and Vendetta 3, 920 -> 820 with stun; ON also damages Ryth 2,
1,140 -> 1,040, with the same extra stun restrictions. Initial queried state, reconstructed
public state before the first different action, and contiguous chunk boundaries match in each
pair. All selected prefix windows are complete with zero filtered events/query failures.
No earlier represented movement or action omission is found in either key.

This answers the first-divergence question for these keys: the first change is an adopted cast
center with applied extra effects. It does not establish private fuel/memory equivalence,
attribute later losses to those extra hits, explain the other two loss flips or rule out later
work costs. No matched-work/no-adoption control is warranted by this finding, and none is added.
The later reason for each lost outcome remains unknown; there is no weight/radius tuning or
automatic rerun of the null arm.

One cheap interpretation is retained as a hypothesis. Fragment's extra victim has max HP
1,200; the 640 -> 540 hit crosses the frozen opponent's nominal `recall_health_frac=0.50`
boundary of 600. After the stun, later retreat/heal timing could differ. The actual low-health
branch, its danger/capture/camp exceptions, higher-priority threat handling and any causal
effect on the eventual loss are unqueried. No downstream window is added to promote that
threshold crossing into a loss explanation.

[`loss-audit/assessment.json`](../../../results/contenders/micro/freecenter/loss-audit/assessment.json)
binds the two complete prefix comparisons, native source/decoder identities, boundary checks,
negative earlier-omission result, exact key selection and
[threshold interpretation](../../../results/contenders/micro/freecenter/loss-audit/fragment-B/policy-threshold.json).
The legacy native map hashes differ from the pool geometry, so this audit makes no static
terrain/camp mapping claim. Wrapper/consumer failures remain retained; completed observer outputs
were reused for their pure corrections. No game or online request was made, and all audit jobs
are closed. This is bounded diagnosis of the historical failed confirmation, not a strength gain.

Consequence: Expanding centers is a real execution mechanism, but this particular conservative
refinement has not beaten its historical bar and should not be integrated on strength grounds.
Removing victim preservation alone has no positive same-score witness in the audited windows.
A separate substantive direction is to select casts using shield absorption, lethal damage,
stun immunity, pending enemy turns and the value of dropped victims, then make the engagement
forecast and executor share that choice. That is a new model/selector hypothesis, not radius or
weight tuning justified by this null. It needs a normal reachable decision witness and actual
caller effects before further broad games, and eventually comparison with v41/current main.

## U7. A ready shield before Varg's basic can preserve the same Ren kill and Varg

Claim: In the supplied native state below, shielding before the already-admitted basic absorbs
Ren's killing-blow retaliation and preserves both the original kill and Varg. Basic-first loses
Varg before its ready shield can help. The complete frozen-v41 `Brain::turn()` reaches both
sequences after ordinary scripted setup; no private admission flag is forced. This establishes
a conditional production-entry contribution, not a naturally occurring missed action or strength.

Source: Immutable `anchors/main-v41` credits a ready shield as immediate health in the local
fight model (`brain.cpp`, ready-shield block), while `fight_enemies` tries its basic before
`try_spell_enemy`. The model has no Ren-retaliation payload. Varg has no strike dash to intervene.
The initially considered `doomed_kill` shortcut is disabled by default and was rejected as
admission evidence. A supplied pure model projection with zero-bag fighter values 1 admits
the ready-shield state (predicted losses 0:1, one round); without that shield credit it predicts
1:1. The first pure scout accidentally used values 2, and that supplied-input result remains
preserved separately from the [corrected projection](../../../results/contenders/micro/shield/probe-r1/corrected-model.json).
Neither projection queries the actual native private forecast or establishes its exact branch.

The private frozen-v41 fork `contenders/_micro-shield` adds only default-false `prebasic_shield`.
OFF `779cc75a1fba`, ON `3a142ae4a86e`; both OFF raw/injected modules exactly match v41 SHA-256
`97202ece0b7114a4fad49ec0831e89c90dbf92a0e85f3b6b2aa86ab2e65f43d0`.
[Baseline](../../../results/contenders/micro/shield/baseline.json) and
[preservation](../../../results/contenders/micro/shield/preservation.json) bind the source closure.
The guard is deliberately narrow: Varg's primary next-basic target must be a freshly visible
Ren chosen by the original basic scoring; attack and cast must remain available; a ready queried
shield must cross the raw-retaliation survival threshold. Incoming mitigation can make that raw
threshold conservative. Splash-triggered retaliation and other shield kits are outside this arm.
It spends the single cast, so it can forgo an offensive reset; it is not a general shield-first rule.

Evidence: Four 93-round native lab games, OFF/ON on both sides of one open 30-by-30 map,
CLI/SDK 3.0.5, engine 1.0.3. The probe supplies the draft and invokes actual `Brain::turn()`
at rounds 1 and 90; between them all movement and damage use ordinary SDK actions. Twenty-two
Varg basics leave Ren at 20 HP and Varg at 560; eight Ren basics and six Tavi basics leave Varg
at 10. Tavi withdraws out of the local fight. The scene is scripted, not a normal full planner
run or normal v41 draft: v41's ordinary draft fields Varg as its fifth pick on five-pick maps.
At round 90 both actors have no items or shield; Varg has Strength 40, Focus 100, cooldowns
0/0 and move/basic/cast available. Ren has HP 20 and Focus 100. The unchanged complete Brain
then acts; the fixture does not call `fight_enemies` directly or override health, ability or model state.

| Native sequence | Ren | Varg health / shield | Varg allowances after the action |
| --- | --- | --- | --- |
| Shared before state | 20 HP | 10 / 0 | Move, basic, cast available |
| OFF basic | Dead | Dead / 0 | None |
| ON slot-1 shield | 20 HP | 10 / 250 | Move and basic available; cast spent |
| ON same basic | Dead | 10 / 230 | Move available; basic and cast spent |

Both arms award Varg the same kill and 100 gold. OFF also awards Ren a kill and 100 gold in the
same basic event; ON absorbs the observed 20 return damage in shield. The current SDK shield
queries return 250 both without a target and against self; its payload specifies duration 5,
and successful casting starts cooldown 8. Expiry over five future turns is not tested here.
One other queried value is narrow: Ren slot-0 `damage_amount(Varg)` returns 10 in this HP-10
scene, consistent with that targeted CC prediction's health cap. It is not a basic or retaliation
prediction, and this assay does not generalize the result to all damage-query forms.

The current official observer retains every actor/event kind in round 90, with complete windows
and zero query failures. Six initial queried champion states, camp/group/score projections and
the preceding represented events match (16 on A, 8 on B). Reconstructing the complete represented
state through that prefix also matches immediately before the first action difference. Per-event
`championStates` are relevant/changed subsets: the ON self-shield event need not repeat unchanged
Ren. The independent consumer's initial failure to account for that representation is preserved
in [its failure record](../../../results/contenders/micro/shield/independent-initial-failure.json);
the corrected consumer validates carried state rather than dropping field checks.
Private memory/fuel, unqueried camp state and the private favourable/doomed verdict remain outside
the equality claim. Actual production entry/admission is witnessed; its precise branch is unqueried.

[Owner native verification](../../../results/contenders/micro/shield/probe-r1/native-verification.json)
binds the four probe snapshots (`a37a52369b33`, `62152535a114`, `d25a64712c76`, `db42df070517`),
setup source, full Brain closure and all saved bytes. The
[independent verification](../../../results/contenders/micro/shield/independent-verification.json)
checks native actor/target identities, identical basic actions, shield/HP/counter transitions,
paired represented state, exact query logs and fuel headers without games or observer reruns.
Reproduce that pure check with the pinned Python and a fresh output:

```
python bots/main/foundation/tools/micro/shield_verify.py results/contenders/micro/shield/probe-r1 FRESH-OUTPUT.json
```

Peak ordinary fuel is 51,550/100,000 across the four games. Both streams' draft, first-gameplay
and ordinary samples stay below their respective native header budgets; no logged error or setup
refusal occurs. OFF dies earlier, so its lower fuel is not an isolated cost measurement of the
new guard. These truncated games provide bounded mechanism health, not full-game fault/stall,
natural-cycle or release qualification. There is no broad shield strength panel.

Applicability remains unproved. A read-only screen of 115 retained v41 summaries finds eleven
games with enemy Ren and our Varg/Heart, and four Ren kills of those shielders. All four selected
two-round native windows show direct enemy Ren attacks/casts, not self-triggered retaliation.
The [negative screen](../../../results/contenders/micro/control-scout/normal-screen-verification.json)
retains the summaries, four complete current-engine traces, exact actor/actions and source bindings.
It finds no naturally occurring missed pre-basic shield; it does not inspect every nonlethal hit
or establish general absence. No online access or new normal match was used to manufacture a witness.

Rests on: M23, M30, M34, S1-S24. Status: current native conditional full-Brain contribution for
item-free Varg versus Ren in the stated supplied geometry, both sides; normal frequency and
strength unknown. Recheck: kit/retaliation/shield rules, incoming mitigation, target scoring,
single-cast allocation or combat entry changes.

Consequence: Before an already-authorized damaging action, a contender should account for
immediate return damage and any required protective cast. Forecasting a ready shield as present
while executing the unshielded first hit can disagree with the engine. Timetable can make that
dependency explicit in its action schedule; Compound can compare the protective cast with the
reset it displaces; Warband can preserve a committed target while guarding the striker's immediate
survival. These are implementable directions, not permission to enable this narrow research arm
without a useful normal-play witness and measured complete consequences. The arm remains OFF.

## U8. Reserving Ryth's farm AoE trades current work and turn packing for possible later control

Four selected actual pool pairs show what Warband's `control_hold_ryth_aoe` changes. OFF
`76b6129855e8` matches frozen v42; ON is `a609f651a2ea`. Only the eligible ready farm slot-0
candidate is withheld while a fresh personally visible live enemy is within six tiles. Basics,
resets and the enemy executor remain unchanged. The sample contains two gained and two lost
keys against frozen v42 and hunter7, CLI/SDK 3.0.5, engine 1.0.3. It is conditioned on outcomes,
not representative sampling or proof that these episodes caused the final wins/losses.

| Actual key; OFF -> ON replay suffix | Observed farm decision | Later observed comparison |
| --- | --- | --- |
| Fragment A / v42, gain; `f3e7d6da` -> `be85cc01` | At r95 OFF AoEs two Wolves for 100 each; ON resets and adds a 45 basic instead | OFF spends farm AoE again at r104. At r106 ON actually casts on Heart/Marcella; OFF slot 0 has CD 6 |
| Fragment A / hunter7, gain; `4f9515bd` -> `5f95cc0b` | At r36 OFF damages two Wolves by 100 each and kills a 20-HP camp; ON ends with slot 0 ready and cast unused | At r40 ON deals 100 HP damage plus stun to enemy Ryth and kills another 20-HP camp; OFF slot 0 has CD 4 and basics a camp |
| Fragment B / v42, loss; `dc3cc26c` -> `df070aff` | At r72 OFF finishes a 12.17-HP Wolf and damages another by 100; ON ends with slot 0 ready and cast unused | No ON slot-0 cast or Ryth champion hit in r68-99; OFF engages Marcella at r77 and casts slot 0 at r81/r93 |
| Split A / hunter7, loss; `696dfa36` -> `dd890f1c` | At r597 OFF AoEs four camps for 100 each; ON ends with slot 0 ready and cast unused | At r604 both deal 180 HP damage to Ryth. ON adds stun; OFF uses reset and then steps away |

The farm cost is visible even in the gained examples. In Fragment A/v42, the first Wolf dies
at r96 OFF versus r99 ON, and the local group's reward becomes available at r104 versus r105.
By r96 ON has taken 60 more camp damage. Its r95 reset spends the single cast: retaining slot-0
readiness does not retain that turn's `can_cast`. At r106 the delivered ON AoE takes Heart's
shield 186 -> 86 with stun and Marcella's HP 1,200 -> 1,100; Marcella is not stunned. In the
hunter7 gain, own Ryth has 800 HP ON versus 880 OFF by the r40 enemy cast.

The lost examples show two different limits. Fragment B retains the option without using it
in the inspected encounter; the only enemy within six at the first hold is stun-immune Marcella
at distance five. Ally allocation and engagement paths also change: the local group's last camp
dies at r79 ON versus r88 OFF despite the first Wolf dying one turn later. This is not a simple
farm-delay explanation of that loss. In Split r604 both first basic the enemy Ryth for 90.
OFF resets and basics for another 90, then moves (19,30) -> (18,29). ON uses AoE for the same
second 90 with stun, consuming movement and remaining at (19,30). The HP gap already predates
this turn; no cause of the terminal loss is inferred from the stun-versus-step exchange.

Source boundaries matter: threat handling precedes farming, the farm DPS estimate still credits
the AoE that ON may withhold, and enemy AoE delivery still searches enemy-occupied centers within
four. Six is the hold condition's proximity radius, not cast legality. A visible cooldown can
change a later forecast, but the replays do not expose opponent intent or the private decision
that caused a changed path. These observations establish later spell delivery, unused reservation
and alternative action packing; they do not establish a general enemy-deterrence effect.

The [existing sample assessment](../../../results/contenders/micro/ryth-reservation/assessment.json)
binds exact game keys, frozen source hashes, saved replay bytes and complete native windows.
The v42 episodes cover r90-121 and r68-99, hunter7 Fragment r1-64, and Split r570-633 after
cheap track localization. Split's untraced middle is not claimed to have an identical prefix.
No games, fixtures, models, bot instrumentation or policy edits were added for this review;
observation work stopped after these four pairs. Strength and downstream score causation remain
outside the sample. Practical consequence: compare the actual camps/enemies affected, the
alternative reset and the movement that an AoE spends when allocating the turn's single cast.

## U9. A tactical option is an ordered state transition, not a ready slot

An executable option starts from a fresh personal observation, current actor permissions and a
legal target/path. Its result changes life, position, vision, buffs, channels and remaining
allowances before the next option is considered. Slot cooldown zero alone does not establish
`can_cast`, and a cast's damage does not describe which step/basic/reset alternative it consumes.
The current SDK exposes these independently through `state()`, `restrictions()`, ability metadata
and target/position guards. U1-U8 already supply applied examples; this entry changes no bot code.

| Transition boundary | What must remain explicit |
| --- | --- |
| Ordinary step | Melee may retain basic; ranged normally spends it; unseen collision may fail without spending the allowance and reveals new information |
| Basic or damaging cast | A target may die, a passive may trigger, a shield may absorb HP damage, and other group members may remain alive |
| Attack reset | Restores the applicable attack allowance, consumes the one cast, and does not restore ordinary movement |
| Exclusive cast | Cast and walking compete even if another slot is ready; later continuation needs fresh permissions |
| Other actor/round boundary | Earlier death or CC can deny a later action; camps act before champion decisions; timers expire at owner boundaries |
| Channel | A started action is a future commitment with distinct cancellation rules, not an immediate conversion |

Two already completed current-engine main-model-lab cases are preserved for reuse in
[derivation-3.0.5.json](../../../results/contenders/foundation/derivation-3.0.5.json). These are imported
observations, not new games or independently rerun native experiments. They branch one retained
replay at round 160/input 2666, using explicit constructed Ryth-versus-Ryth states on one map and
one actor-order orientation. Their original executed sources, probe bodies, process closure,
calibration and hashes are retained under `results/contenders/foundation/model-lab-import/`.

The lethal-basic case compares one basic then yield, with spells suppressed. The earlier native
basic kills the later actor; its attempted basic is refused with code 40. The exact aggregate
production projection credits both casualties in one round. This demonstrates one representation
counterexample, not a natural error rate, full Brain reconstruction or evidence that every pooled
estimate fails. Target access, policy, shields and CC remain separate dimensions.

The CC case applies a real 100-damage Ryth area cast to one nonimmune Ryth. Stun duration is one;
the next owner attack is refused, the target's owner end decrements the timer from one to zero,
and the following owner input has no stun and applies a 50-damage basic. The existing historical
M26 before/after-owner rule is consistent with this single current orientation; both orientations,
every kit and natural applicability were not revalidated by the import.

Consequently, an applied one-round stun denies the target's next owner opportunity; it does not
make that target harmless for an arbitrary number of world rounds. To forecast safety through
our next input, order each intervening owner opportunity and expiry. A target still pending this
round may spend its lost opportunity now and act ahead of us next round after expiry. A completed
target's next opportunity instead consumes the stun. Fresh restrictions and actual effect matter:
Marcella immunity, an already stunned target, an unsuccessful cast or another enemy can invalidate
the expected benefit. A stun can also cancel a bank/recall without making a subsequent chase safe.

This source/rule consequence does not choose an action scorer, rollout policy or search algorithm.
Each contender must connect the chosen sequence to its useful work, immediate survival and release
conditions (G11), then establish normal contribution and matched full-game strength separately.
No new native work, fuel qualification, natural frequency or strength claim is made by this synthesis.

Normal Tavi application now has a bounded witness in Compound's preselected `00ambush` A pilot
game, `06cd15d99824` versus v42, engine 1.0.3. The
[assessment](../../../results/contenders/compound/tavi-growth/ambush-A-mechanism/assessment.json)
binds the replay, statistics and two unique native windows, rounds 75-90 and 140-190. At 81,
Tavi's range-four exclusive cast applies 154 damage to Heart (150 shield and 4 HP), retaining a
basic allowance but no in-range basic target. At 188, another exclusive cast applies 154 HP
damage to Ryth and then a legal basic applies 55. Ryth had completed its turn; its actual next
input at 189 has all action allowances false and only ends turn, ticking stun1 to0. The stun
expires at its 190 input, then Ryth casts for 100 against Tavi. This is a normal executed
cast/basic and one denied-input contribution, not a lasting safe disengagement.

The same second window contains a stationary camp basic/reset/basic at 153 (HP120 to75 to30),
an actual final-member clear at 160, sole Tavi capture payout of 80 gold and zero gems at 168,
then two field Strength shards taking Strength45 to55. That is observed gold renewal; Tavi
collects and submits zero gems over the whole game. The first withdrawal's loss of sight and
release occur in the unobserved 91-139 gap. No continuous retreat/recovery chain, applied
step/reset/basic against a champion, survival counterfactual or score causation is established.
The field-control trajectory diverges before these events, so fewer whole-game deaths cannot
be attributed to this local packing sequence. All three original/corrected observer Jobs are
recorded empty; rereading their retained evidence adds no native runs or independent win samples.

Current full-game warning: Compound's independent Tavi field-growth role reaches 7/12 against
v42 in its training pilot, then only 25/80 on all 40 pool maps/both sides, versus 40/80 for the
same-run baseline (paired flips +10/-25); the same-Tavi field chassis wins 29/80. Against frozen
hunter7 baseline and growth both finish 66/80, versus field's 69/80. All 480 games
in that three-arm/two-opponent pool have clean subject and opponent health. The
[owner's pool record](../../../results/contenders/compound/tavi-growth/pool.json) retains exact
sources and evidence. The package is rejected. Baseline-versus-growth combines the Tavi chassis
and controller; field-versus-growth compares the controller package on that chassis. Neither
comparison identifies the value of each individual transition. The normal applied contributions
above establish mechanisms, while these full-game results reject their tested composition into
this race architecture; they do not identify the cause of its broad loss.

### Finite productive melee words

The [action-word knowledge artifact](data/combat/action-words-3.0.5.json) preserves the declared
grammar, fresh fields, kit differences and existing native references for Varg,
Marcella and Ryth. Starting full-ready in an uninterrupted owner turn, the restricted
Move/Basic/one-cast class has these maximal successful words:

- Reset branch: `M A R A`, `A M R A`, `A R M A`, `A R A M`. The first basic spends attack before
  reset restores it; include shorter supported words and actual-start suffixes separately.
- Varg's compatible shield: all supported permutations/subwords of `M A S`. Shield competes
  with reset for the single cast; neither order dominates every shield/timer/target state.
- Marcella/Ryth movement-exclusive offensive cast: `X A` or `A X`, and their supported
  subwords, with no successful ordinary move in the input.

This is an algebraic grammar under stated native allowance contracts, not exhaustive native
validation of every ordering, target or center. At most four successful primitives occur
in this class; refusals need a separate finite attempt budget. Use actual residual allowances,
cooldowns, enhanced state, restrictions and input history for other starts. A range-only
`ability.valid_tiles()` proposal is not complete ground legality; the supported free-floor
center subset must remain explicit. Ren's immediate hit-triggered retaliation can change HP
inside a word even without a scheduled opponent turn. Refresh effects or apply a validated
transition after each actual result; do not freeze the world for the whole word. This retained
knowledge adds no new games, whole-turn fuel claim, complete service or strength qualification.

Movement exclusion also needs actual owner-turn-local walk history. The current SDK's `is_ready()`
checks current ability information, cooldown and `can_cast`; it does not add the historical
M25 successful-step precondition. A false `can_move` is not an equivalent walked flag: a
standing ranged basic spends movement too. Retain a typed successful-ordinary-step fact with
the actual owner-turn epoch across recompilation and dispatch; the native timestamp input can
advance within that turn. A Boolean-refused move does not set it. An accepted exclusive cast
prevents a later ordinary step in the same owner turn.
The artifact distinguishes this current SDK derivation and historical M25 contract from the
owner-reported R3 adapter failure. They do not form a new universal native validation.

## U10. A refused basic does not make Ryth's reset legal

Claim: An out-of-range basic leaves Ryth's attack available. His reset then refuses the
input while that attack remains available; a ready cooldown and cast allowance are insufficient.
This extends the reset-availability distinction in U2/U5 to the stated Ryth branch, rather
than establishing a universal reset-kit rule.

Evidence: C11's native falsifier branches the preserved replay at input2666/round160 using
gameplay engine1.0.3, SHA256 `029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48`.
In the constructed mirror case, Ryth actor7 is at(35,18) and target2 at(31,18), outside his
range1. The requested basic refuses with code43, attack remains true, and slot1 reset refuses
with code26, `An available attack cannot be reset.` The original replay remains unchanged.
The [durable witness](../../../results/contenders/optimization/c11/reset-availability-witness.json)
contains setup/actions, queried before/after actor states, response and native asset hashes.
Coverage is one constructed replay branch, one kit and orientation, not normal-play frequency,
an independently sampled full game, broad model calibration or a strength result.

The optimizer's earlier restricted model admitted this reset. C11's counterexample caused
the model to require a spent attack and bump its model identity; legal-action and transition
regressions now reject the premature reset. A native optimizer can expose defects in its
own dependent model without a successful policy improvement. Executors should requery
allowances after refused inputs rather than advancing an assumed action sequence.

## U11. Dense geometry changes actual work, while the single cast still prices shield against reset

Claim: In one constructed dense 2x2 four-wolf task, current Varg's first ordinary basic applies
40 damage separately to each 1,350-HP wolf, including the primary target once: 160 actual damage
from one basic. His round-10 shield cast grants 250 shield and uses that turn's cast; the next
round's four 20-damage wolf attacks consume 80 shield before his basic/reset/basic continuation.
This is a current native transition, not a rate estimate. E2's historical 45/180 sustained rates
include a 40-damage reset per eight turns; they do not assert 45 Strength or 180 per basic.

Evidence: Mosaic's [corrected native verification](../../../results/contenders/mosaic/geometry/r2/native-verification.json)
binds Varg `3889212f4fbf`, Ryth `85a84b9ee382` and passive opponent `83e3f45b13da`, raw/injected
module equality per source, four 180-round lab games on both sides, and twelve complete saved
native windows with empty owned Jobs. Both specified executors reach solo partial-work/recovery,
clear, sole capture, tile release and bank. Varg clears at round 57, receives 16 gems/160 gold at
63 and banks at 69; Ryth clears at 104, receives the same reward at 110 and banks at 116. Native
bag 16 to zero and team score zero to 16 establish each bank. Varg recalls once (28 to spawn/heal
33), Ryth twice (25/58 to 30/63). Actual basics are 34 versus 68 and own moves 26 versus 36.
First basics occur at rounds 10 and 9 respectively, so pre-contact timing includes fixed draft
slot and approach differences; post-contact timing still compares these particular executors.

Status: bounded native mechanism and completed stipulated task, CLI/SDK3.0.5, engine1.0.3.
The corrected source removes optional logging after the prior traced Ryth arm reached its fuel
budget at round 81 on both sides; that health failure is preserved and is not relabelled healthy.
At-budget fuel alone proves neither suspension nor logging as its cause; the guest program
counter is not represented by this measurement. Corrected
180-round raw fuel streams have no at-budget values and no stdout error lines; deliberately
truncated outcome statistics do not establish full-game fault/stall/exhaustion health. This is
no ordinary-work selection, displaced-labor, optimal-kit, profitable-architecture, strength or
renewal proof. There are no items or productive alternative tasks, and the controller stops
after banking. The conservative incoming-damage admission rule is an executed choice, not a law.

Consequence: Candidate work and investment must use actual target geometry, per-action damage,
cast/movement allowances and individually survivable recovery, then prove capture/conversion.
The same geometry can give different bodies different feasible completion paths; this does not
prescribe a roster or justify transferring a lab executor before ordinary-game health passes.

Later finite model comparison: the [kernel comparison](../../../results/contenders/mosaic/kernel-refinement/comparison.json)
passes 27 checks over 23 native occurrences: six self-cast/reset transitions, the actual C11
reset refusal, sixteen external-damage arithmetic steps and four ordered chains reusing those
damage events. This is tested refinement of the declared dependent projections, not a whole
controller. The exact frozen pair is frame `64764181...` and transition `08ea0a48...`, retained in
sources `0e2ddc1e2ebc`/`88b5a88b9141`; the live frame changed afterward, so no transfer is automatic.
Positive atomic cases set `require_cover=false` and `camp_volleys=0`, retaining unsupported-forecast
flag 64. The fixture reports zero SDK calls during evaluation; collector correctness and charged
whole-phase cost remain separate. Sixteen camp-basic/AoE/move cases lack required status/masks and
were deferred. The 250-point shield input is derived from Focus 100 under no items, not an API-amount
producer test. Wake, damage generation, forecasts, candidate generation and complete-model
correctness are not established by these checks.

Executable micro also needs a charged whole-turn cost. The frozen
[kernel-cost measurements](../../../results/contenders/mosaic/kernel-cost/index.json) use the same
frame/transition pair but constructed typed inputs: at 30 task members/160 world members, a single
selected basic/AoE/reset/wait evaluation costs 31,217/42,385/31,817/22,863 fuel before SDK collection,
navigation or actuation. A Ryth-shaped nine-member menu already costs 102,544-106,855; the
30/160 menu costs 1.754-1.777 million, and even eight candidates cost 347,938. Both sides reproduce
these workloads. Typed-frame construction is separate (up to 61,583 here), not an SDK collector
measurement. These are measured failures of the specified whole-phase budget premise, not
universal cost bounds, native-world reachability, kit calibration or qualification of a later
optimized source. Purity and bounded iteration alone do not make a micro sequence executable.

Actual observation has its own cost. The [collector evidence](../../../results/contenders/mosaic/frame-observation-cost/index.json)
preserves three full 2,000-round BigWorld A games with both teams waiting: 24 SourceWorld groups,
92 members, and only 0-3 personally seen camps near spawn. Baseline `34a7b132222b` costs about
68-72k in complete steady samples and has three exhaustions; guard-only `005f7cc19d36` has five.
Persistent-buffer `842525e9d6ab` (frame `41c0a138...`) reduces observed steady collection to about
49-52k with zero faults/errors/exhaustions in that cell. Its first two attempts remain incomplete;
startup uses the distinct 200k allowance. Profiling is part of each measured module. This is
isolated collector health, not combined observation/model/navigation/action health, a universal
cost bound or strength. These native-world costs cannot simply be added to the differently
constructed 160-member kernel measurements as one measured pipeline.

Later [stage4 spatial measurements](../../../results/contenders/mosaic/kernel-cost/stage4/index.json)
bind a different frozen source and the same two constructed frames. With an explicitly accepted
canonical camp-layout token, 30-task/160-world nomination costs 26,323 and selected certification
22,949/23,006; two planning cycles still total 101,030 fuel before collection/navigation/actuation.
One-time index construction is separately 71,350 (4,987 for nine members), not free or inside
that aggregate. This demonstrates a finite cost reduction, not native layout validity, a universal
bound or ordinary-game health. M7's conditional position/range premise and fresh contradiction
handling remain obligations; prior source measurements retain their original hashes and scope.

The later [R8 panel](../../../results/contenders/mosaic/ordinary/index.json) tests a distinct
untraced module `9536deac930d`: four maps/both sides/v44, eight losses. Six ON keys are technically
clean, while both Yellow Brick Road keys have five exhaustions and zero work. Their five actors
each exhaust at round 5; the charged call site is untraced. OFF also fails health on the two new
Yellow Brick Road keys, so default gameplay preservation cannot supply health qualification.
The separate [sparse trace](../../../results/contenders/mosaic/ordinary/kernel-r8-trace/assessment.json),
`464962383847`, observes all five snapshots complete at round 100 with only 14,066-15,628 fuel
left, below the 25,000 macro-loop admission. Round 20 includes incomplete and complete captures.
This exposes one sampled observation-to-command boundary, not every private no-op or the round-5
overrun cause. Logging perturbs cost; the trace cannot relabel the untraced module healthy.
The separately preserved R9 packet source `c20242e6aa7a` also has five exhaustions and zero work
on each Yellow Brick Road side. Its dedicated layout phase actually occurs at round 7, after
the round-5 overrun; this does not identify the charged earlier call site or retroactively prove
R8's cause. Cold memory-copy attribution remains source inference. The successor working
packet adapter has no health qualification from these failed modules.

The later R12 untraced module `b794428c9ccb` removes faults/errors/exhaustion on one repeated
Yellow Brick Road A/v44 key (peak 88,178), but still has zero output, a stall and a loss. This
is a narrow technical repair, not productive or broad health qualification. Its separate sparse
diagnostic `9e9a914a66e5`, preserved in
[R12's assessment](../../../results/contenders/mosaic/ordinary/kernel-r12-enumeration/assessment.json),
has all five round-100 snapshots complete with only 16,220-17,817 fuel left, still below the
25,000 work-loop admission, at task -1/revision 0. That sampled boundary is not a private-branch
trace of the uninstrumented module. Default payload equality excludes private fuel; the default
arm itself has health failures. The proposed limited observation domain is a new, not yet
native-qualified architecture and must preserve all current effect/threat support obligations.

Frozen R13 `344b7dd29d35` likewise has no faults/errors/exhaustion on one Yellow Brick Road A/v44
game (peak 85,681), yet no work and a stall. Its separate
[final stage-profile index](../../../results/contenders/mosaic/ordinary/kernel-r13-stage-trace/index.json)
binds diagnostic `c0137dc61882`: at sampled actor-0 round 100, the limited domain reduces
eligible visible rows from 22 to 5, but collection remains incomplete. Measured boards cost
27,230 and groups/camps 36,142; stage sum is 79,596, total 97,640, with 18,044 unattributed.
The board subphase combines vision/export/spawn iteration and guards; SDK width/height are
cached metadata, not imports. Read-attempt counters are not all host calls, and the exact failed
group guard remains unresolved. Instrumentation adds five round-100 exhaustions absent from
ordinary R13; this is no uninstrumented cost, universal bound or healthy-pipeline certificate.
The scheduling obligations in [L8/T5](mathematics.md#l8-guarded-computation-and-retained-progress-are-different)
therefore concern completion and fresh effect authority as well as finite component costs.

## U12. Damaging Heart can strengthen its next response, and one disabled input is not safe renewal

The [retained R93 native assessment](../../../results/contenders/micro-word-compiler/r93-native/assessment.json)
starts from one actual Packs/Warband-versus-v44 Marcella owner input, not a constructed restore:
engine 1.0.3, replay `660c3d459ccc...`, input 937/substep 7. Marcella is at
`(19,17)`, HP 1200, with all allowances. Visible Heart is at `(17,15)`, HP 1000,
shield 204/timer 1, Strength 30/Focus 136, cooldowns 4/0. The 675 evaluated prefixes include
384 search prefixes and 291 explicitly declared response prefixes; 1,926 retained-evidence
checks pass. There is one actual root, with the other actors yielding and channels disabled.

The actual selected-owner order is Marcella R93, Marcella R94, Heart R94, Heart R95, Marcella
R95. Stopping after two Marcella ends therefore reaches the first Heart decision only at
cutoff; it supplies no evaluated counterplay. The bounded extension observes these words:

- Marcella's slot-1 cast at distance two consumes movement/cast, leaves attack, and reduces
  Heart's shield from 204 to 84. After End, Move -> Basic -> Reset -> Basic removes the shield,
  leaves Heart at 994 HP, and applies one-input stun/restrictions 63.
- Heart must End at R94 within the generated primitive domain. At R95 it refreshes and acts
  before Marcella. Dash -> Basic leaves Marcella at 965.2979736328125 HP; her later ready input
  has cooldowns 7/6. The stun did not cover both enemy opportunities.
- Under the two specified passive controls, Heart's Dash -> Basic -> End, then
  Move -> Basic -> End leaves Marcella at 936 HP and cooldowns 0/0. These are actual endpoints
  under declared reply words, not a common exhaustive adversarial tree or minimax bounds.

Heart's public missing-health passive changes its factor after the six HP of damage to
`1 + 0.5*(1-994/1000) = 1.003`. The subsequent dash and basic therefore have raw damage
204.612 and 30.09 before native float32 subtraction. The same two hits against a full-HP Heart
cost 234; the wounded Heart costs about 234.702. More damage dealt is not monotone response
weakening. The shield/timer, missing-health factor, actual allowance changes and next actor
must remain in the residual state; a scalar damage or stun score cannot replace them.

**Application boundary.** The consumer must refresh after every actual primitive/result and
invalidate a suffix on changed target visibility/position, refused movement/cast, unexpected
enhanced effect, changed owner/epoch or flags. Preserve the persistent attempt bound while
replanning. A reset is kit-specific: this Marcella witness restores attack, consumes cast and
creates an enhanced basic that later applies stun. It does not qualify a generic ordinary-reset
model, an unmodeled harmful dash, a shield-expiry assumption or a deployable pressure policy.

**Scope.** This is one equipped state and a finite declared word domain. It omits shopping,
consumables, wards, channels, hidden ingress, the other actors' active responses and later
turns. No accepted endpoint earns payout or changes score. Full-tree response coverage,
observation-safe deployment, fuel health, productive conversion and competitive strength
remain open. The native record is reused here; this synthesis ran no new game or branch.

## How the three contenders can derive different play

- Timetable can price available same-turn damage in clear-time scheduling, and must invalidate
  a scheduled basic target after an earlier lethal action. This changes service time, not its
  independent route/claim model.
- Compound can account for a productive reset or cast/basic pair when estimating an item's
  payback and a camp's safe completion time. The proof does not justify extra shopping trips
  or awakening a group outside its economic commitment.
- Warband can preserve a group/focus commitment while redistributing an otherwise wasted basic
  to a living member. U4 establishes conditional production-caller contribution; a paired
  full-game evaluation must establish strength and normal-planner applicability separately.
  U6's private center-search fork supplies a tested mechanism and failed fresh strength check,
  rather than an arm to copy into the coalition without an independent decision.

All ordinary fuel streams in the repaired eight-game probe stay below the queried 100,000
budget; peak is 55,943 and no ordinary turn reaches its budget. Both sides have zero logged
error lines. These are intentionally truncated mechanism games with no completed group cycle;
their zero native statistics fields do not qualify full-game fault/stall/health or release
safety. Complete production cycles, fuel under real observation/navigation load, pool-map
applicability and strength remain untested by the U1-U4 packing studies. U6 separately reports
full-game health and inconclusive strength for its exact historical frozen-v40 source pair.
