# Reservoir Wardens

Varg may occupy an enemy-side camp spawn shortly before its personally observed
respawn becomes due, while ahead by at least 48 submitted gems and carrying none.
The episode lasts at most 32 eligible phases. The ordinary B0 economy funds it.
The score floor, empty-bag condition, personal visibility, safe arrival/exit,
enemy-distance guards, due window and release cap remain unchanged.

## Current activation experiment

Claim `claim-0060-5b6cdb35`, 23 October 2026, CLI/SDK 3.0.5, engine 1.0.3,
toolchain `3837f00416ea`. Both new knobs default to zero:

- `RESERVOIR_SPAWN_VISIBILITY=1` checks the same any/all visible spawn membership
  by iterating each physical group's immutable members. It avoids allocating and
  scanning whole-map bitboards for every group in observation and ranking.
- `RESERVOIR_CHOICE_SNAPSHOT=1` reads own position, health, maximum health and
  already supplied allies' positions once per action-free ranking invocation.
  Nothing survives into another decision or owner turn. Ranking, tie order and
  safety thresholds are unchanged.

Default source `86747507c056` retains the incumbent expressions verbatim in OFF
branches. Its raw and injected modules separately match incumbent
`60ebe8f12fc0`, SHA256
`ca5e08cd2c710dad77f312097d3ebac0886dfe3fcc3049510a6cbb810cdd7036`.
The explicit joint experiment is `58efadd056e0`; consumed role is raw.

Current Big World baselines versus frozen Fundamentals reproduced zero clears,
income and banking, with 65/479 exhaustions on A/B. Varg owns 63/479; Vendetta
and Marcella own one each on A. Initialization completed at round one; the
failure was not established as a startup problem. Full CAMSTAT has all events,
but the runner correctly rejects baseline execution qualification because its
bounded actor-context report retains only 32 events per team. Those failures
remain failures. The joint experiment completes both games cleanly, clearing
5/4 camps, earning 133/203 gold and banking eight gems on each side.

The Farm supplies sustained confirmation against productive frozen main-v28:
27/30 own clears and 319/249 banked gems on A/B, versus incumbent zero own clears
and 37/0 banked. All three drafted actors personally damage camps, collect,
earn and bank in both enabled games. Their bank totals are Tavi 81/133, Varg
100/26 and Vendetta 138/90. The opponent remains technically clean and banks
1011/1022. Vendetta's A game includes own group-5 clear at R47, capture at R53,
and a 24-gem bank at R185, with later banks at R336 and R545. These are native
event identities; no solo-damage or strength benefit is inferred.

Two rejected approaches are retained with the evidence: extracting common OFF
helpers initially changed executable bytes, so OFF reverted to verbatim
preprocessor branches; a 50k field-entry reserve removed fuel flags by stalling
all four Big World/Farm cells, so it was removed. Visibility alone restored Farm
production but left two Varg Big World exhaustions. Invocation-local query reuse
removes those failures in the four targeted joint-variant games.

Evidence and full-pool disposition are recorded in
`results/population/reservoir-activation/measurement.json`. Scratch, exact source
checkpoints, separate OFF gates, contained build receipts, failures and native
outputs live under `.cache/arena/orchestration/mass-activation-next/breakpoint_activation/`.
No default promotion or Reservoir denial-mechanism admission follows from the
production repair; the previous historical hold witness is not relabelled as
current evidence.

The complete paired pool comparison has 80 games per arm, all 40 maps and both
sides against frozen main-v28. The repair has zero exhaustion, error or fault
events, 77 productive cells, 2,431 clears and 17,628 banked gems. The incumbent
has 269 exhaustions, seven stalls, 73 productive cells, 1,976 clears and 13,260
banked gems. Paired mean changes are +5.6875 clears and +54.6 banked gems per
cell; these are descriptive coverage results. Three repaired cells still stall:
Road of Riches A/B and Boss Battle A. Full health and release remain blocked.

Normal repaired Broken Curve B play also reaches the preserved commitment.
Native Varg id 4 occupies (16,22) with empty bag, 760 HP and 190 gold, personally
observing group `g_896` in respawning state at timer zero on every R193–224 owner
start. Telemetry credits 32 eligible phases and releases at the unchanged cap;
the public lead remains 93 then 49. At R225 he is outside and the group is alive.
He then clears another group at R229, receives its 14 gems at R236 and banks
14 at R239. This is current physical holding and productive continuation.

A fresh four-game Broken Curve comparison changes only existing
`core_enabled=false` on the repaired source (`798ace8efb29`); both arms and sides
are technically clean and productive. The enabled B game's seven replay
payloads exactly reproduce the pool game. The control leaves the region, but
the trajectories already differ at R187 and its camp becomes personally hidden
after R189. Therefore this is a whole-trajectory ablation, not an identical-frame
32-round respawn counterfactual. A bounded filtered respawn trace retained no
events and supplies no substitute for hidden state. Net denial utility and
interaction admission remain open; no new observer or weaker visibility rule
was introduced.

## Remaining executor repair

Claim `claim-0074-57643785` keeps all knobs default OFF. New
`RESERVOIR_WORK_PREP=1` constructs the ordinary obstacle mask before optional
action-free ranking, consumes it once only while round and position still match,
and retains a 50k ranking tail for source/goal setup before the unchanged 30k
field guard. Actions invalidate the preparation. The phase policy, queried
durations, safety, channels, personal visibility and unusual mobility are unchanged.
`RESERVOIR_WORK_DIAGNOSTICS` also defaults OFF. Default source `abdffc249547`
reproduces incumbent raw and injected SHA256 `ca5e08cd2c710dad77f312097d3ebac0886dfe3fcc3049510a6cbb810cdd7036`;
the existing two repairs with new knobs OFF (`f9d30db2b150`) separately reproduce
tested `58efadd056e0` bytes `da5533aa592a89a5640de0b5b1c078635f54ea5ed61d9e5a08f0591d0c11fc44`.
The enabled experiment is `fcab28a9c45b`, raw/injected
`be8f4943bdbb2b085d35278a0ff5beef63b5b470b7154337fdbe9ca5cdfebdd1`.

Current Road A/B and Boss A failures were executor dead ends: ready actors
finished optional ranking with about 34k fuel, then obstacle/setup work prevented
the first field from entering. The final repair clears all three in the targeted
both-side comparison. Road A banks 420 gems; Road B banks 56; Boss A earns 968
gold. Native Road A Marcella captures twice and banks 35 at R314. Boss Tavi
clears and receives paid group-25 captures at R277 and R465, with later shopping.
Farm still gives every actor paid work and banking: 296/347 team gems on A/B;
Varg personally clears/captures group 5 at R49/55 and again R184/191.

The paired 40-map, both-side pool versus frozen main-v28 completes all 160 games
on engine 1.0.3. Before/ON productive cells are 77/79 of 80, clears 2,431/2,656,
banked gems 17,628/19,148, and gold 152,206/158,222. Neither arm has faults,
errors or exhaustion. The ON arm has one new stall, Islands B; full health,
release and promotion remain blocked. Eight fresh paired Ambush/Collision games
versus Fundamentals are healthy. These descriptive gains establish neither
strength, mechanism admission nor universal actor production.

Normal enabled Packs B preserves the phase interaction: Varg personally observes
group `g_506` respawning at timer zero while occupying (10,16), R416-431, empty
bag and full HP. The unchanged enemy-distance guard releases him at R432; he is
outside at R434 and receives 33 gold/9 gems from the next capture at R448.
The core-disabled control is a whole-trajectory ablation, not an identical-frame
denial counterfactual. The unchanged 32-phase cap has fixture coverage; this
native episode is a 16-phase enemy-guard release. Net denial utility remains open.

Islands B Tavi stays at (31,56), full HP, move/attack/cast ready and without a
channel, stun or visible enemy at R1-25. A separate bounded diagnostic enters
one route field, so this is a different obstruction from the repaired zero-field
tail. Neither group `g_2555` capture pad, (35,42)/(36,42), belongs to the actor's
relaxed eight-neighbor walking component. That is evidence against ordinary
walking/capture access only: legal dash, teleport, item or other ability-assisted
access is unresolved. No public-component gameplay filter was added. The next
experiment is static-versus-ability capture admission with the actor's full
queried guards, rather than more fuel-floor tuning.

Rejected bounded attempts remain recorded: complete camp-HP snapshots did not
repair the stalls and reduced Farm income; hold-scan reuse gave unpaid Road B
clears; an earlier obstacle-preparation tail still left Boss A stalled. SDK
reward accessors already cache immutable values, so that proposal was stopped
before ON games. Existing phase-policy tests pass all 31 checks. Exact inputs,
raw/injected gates, native failures/continuations, whole-game actor health and
private-Job exit receipts are retained in
`results/population/reservoir-executor-tail/index.json`. Source remains default
OFF; claim 74 stops here with the Islands mobility question explicitly open.

## Tavi ordinary capture admission

Claim `claim-0084-a38f993c` follows independent Islands triage. New
`RESERVOIR_TAVI_CAPTURE_ACCESS` defaults OFF. Only Tavi, with no dash kit or
owned teleport crystal, checks ordinary access to an actual member capture pad
before pricing a camp job. A bounded native static field is reused while his
position remains in its walking component; an incomplete field supplies no
negative conclusion. Dynamic occupancy, visibility, affordability, retreat,
field/reserve limits and Varg's phase/prize/hold policy remain unchanged.
Other actors and mobility-consumable states keep their existing admission path.
This is an ordinary-job applicability check, not universal mobility impossibility.

Before enabled preparation, five retained recipes each passed raw and injected
OFF-byte comparisons: original default, old two-knob repair, claim-74 repair,
core-disabled control and diagnostics. Current default `fcf329004737` still
consumes `ca5e08cd2c710dad77f312097d3ebac0886dfe3fcc3049510a6cbb810cdd7036`.
Claim-74 recipe with the new knob OFF is `00a4f91d8389` and still consumes
`be8f4943bdbb2b085d35278a0ff5beef63b5b470b7154337fdbe9ca5cdfebdd1`.
The measured four-knob ON candidate is `d9b9c293b6e6`, both roles
`5244f3153ff868298f454bde64a76027a16e42a9b90da7b2aa93b4a73ac4277c`.
Private-cache prospective default `a726f3970b24` reproduces those exact role
bytes. All four canonical knobs remain OFF pending root's independent review.

Uninstrumented Islands B Tavi now clears personally visible `g_3149` and alone
receives 40 gold at R14. Its two Gold members start at 120 HP each with zero
incoming damage; the unchanged affordability rule accepts that current work.
Retained own states through the payout have 900 HP and no visible enemy. He
later fights and dies, so no Islands renewal is claimed. On Farm A, the same
candidate's Tavi captures `g_951` at R42, another group at R226, `g_951` again
at R291, then banks 35 at R314. Current Packs B Varg still holds personally
observed timer-zero `g_506` during R416-431, releases on the enemy guard at
R432, is outside at R434 and receives 33 gold/9 gems at R448. This preserves
the scoped 16-phase interaction; net denial utility remains unproved.

All 20 targeted preservation games and eight fresh paired Ambush/Collision
games versus Fundamentals are healthy. The complete same-run 160-game pool
comparison versus frozen main-v28 has candidate 80/80 team-health passes,
zero stalls/faults/errors/exhaustion and maximum fuel 98,033. The claim-74
baseline remains failed at 79/80 because of Islands B. Candidate aggregate
clears/bank/gold fall from 2,656/19,148/158,222 to 2,626/18,732/153,127.
These are health/activity results, not strength or admission claims.

All 360 matched actors are audited, including gains and losses. Income gains
13 cases and loses six; production gains six and loses three. Lost income is
Big World A Varg plus **all five Yellow Brick Road A actors**. Lost production
is Big World A Varg and Big World B Vendetta/Vesper. The latter two previously
had damage without income; Yellow Brick Road retains some outgoing damage but
loses all paid work. Thus formal team health does not establish preservation
of every productive actor or paid economy. These losses are material review
limitations, not hidden by the improved zero-income/zero-production totals
(29/12 to 22/9). No additional matrix or source repair was attempted here.

Exact inputs, ten pre-ON byte gates, native paid work/renewal/guarded release,
full pool/fresh outputs, every actor transition, prospective-default proof and
private-Job exits are pinned by
`results/population/reservoir-capture-access/index.json`. Source is frozen for
independent review. Root has placed default promotion on HOLD because the paid
work regressions fail the useful-activity purpose despite formal team health.
Read-only Yellow Brick Road geometry reaches every group's capture region from
all 25 own-spawn origins, with maximum walking distance 35-39. Non-Tavi actors
short-circuit before field setup. The next proposed discriminator is one bounded
R1-3 comparison of Tavi's added field/member-query cost, fully priced prefix and
selected job before interaction divergence; no new game is authorized by this note.

## Bounded first-selection diagnostic

Claim `claim-0091-8374b932` adds only default-OFF
`RESERVOIR_SELECTION_DIAGNOSTICS`: Tavi R1-3 reuses existing fuel-fence reads,
records completed-prefix/field/selection counters and emits after turn actions.
All seven retained recipes pass 14 separate raw/injected disabled-byte gates.
No strategy repair or default enablement was made.

**Observation.** One fixed Yellow Brick Road A four-arm execution reproduces
both uninstrumented claim-84 replays exactly. All four native action/state
projections match through gameplay R3. Diagnostic R2 before/candidate prefixes
are 28/22; R3 they are 26/22. Both select `g_685` with zero access rejections.
The candidate enters its capture field at R2 and reuses it at R3. These are
instrumented internal counts, not proven uninstrumented counterfactual counts.
The first physical difference is R12 Vendetta: before moves (11,15) to (12,16),
while candidate ends at (11,15), full 1000 HP, empty, move/attack/cast ready,
without channel, stun, restriction or visible enemy. The before diagnostic also
changes that move to an end-turn, so whole-game diagnostic equivalence fails.

**Interpretation.** The first observable divergence is not Tavi's first target.
Extra context/false-branch cost on another actor near a fixed work fence is a
specific hypothesis; neither the exact blocking predicate nor causality is
established. No repair or further trace follows from this experiment.

**Limits.** Retained R12 whole-turn fuel is before 89,939 used/10,061 remaining,
candidate 73,118/26,882 from a 100,000 budget. These are stream values, not a
pre-field guard sample; private selected target and direct end-fuel query remain
unknown. The exact R1-3 reader first returned empty/incomplete coverage because
draft indices exceed three. A widened reader through R12, explicitly authorized
after an initial automatic-review denial, supplies the gameplay prefix and first
difference. Empty attempts, denial/correction and all controls remain retained.
Current HOLD and all default-OFF flags remain. The finite analysis is pinned by
`results/population/reservoir-first-selection-cost/index.json`.

## Current Main53 diagnostic

This is an engine1.0.3 historical measurement. Work stopped when the user reported
a game update; applicability to the incoming bundle requires revalidation. No new
native case or source change was started after that notice.

The existing four-repair variant completes all 40 pool maps on both sides against
released Main53: **0 wins, 0 draws, 80 losses**, with no recorded fault, error or
fuel-exhaustion flags on either team and no truncation. This is a private diagnostic,
not a default qualification. Current configured source `a886aeb582bf` consumes RAW
`5244f3153ff868298f454bde64a76027a16e42a9b90da7b2aa93b4a73ac4277c`, exactly the
earlier `d9b9c293b6e6` repair; source and all default-OFF flags remain unchanged.
The old 80/80 Main28 result was a health result, not 80 wins. The strength gap and
the earlier paid-work regressions independently keep promotion on HOLD.

The closest loss, Split A, banks603 against1013. Its native RAW regeneration
reproduces the result. Varg banks13 byR100, then approaches five personally visible
enemies. He shields and attacks atR112 and attacks atR113 with movement still
available, then ends at(24,20). AtR114/115 he has no move/attack/cast availability;
Vendetta kills him atR115/input2386. Thus this is neither an unconditional capture
hold nor an available late retreat. A native R113 personal card permits(24,19),
(24,21) and(25,21): the south step leaves Marcella adjacent and awake camp exposure,
even though it removes immediate Vendetta basic reach. This does not establish a
safe exit or renewed paid work, so no post-defense movement repair follows.

Next discriminate the earlier approach into the visible cluster while preserving
Varg's denial commitment. A personally applicable alternative followed by actual
paid continuation in a matched experiment would change this decision. No coefficient
search or universal stall-safety claim follows. The complete experiment
`specialists-next/reservoir-floor53` is closed; exact inputs, byte bridge, aggregate
results and bounded native limits are in
[`reservoir-current-strength`](../../../results/population/reservoir-current-strength/index.json).
