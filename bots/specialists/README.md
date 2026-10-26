# specialists: one first-principles commitment per bot

Sixteen self-contained forks of the B0 chassis (`anchors/fundamentals`), each adding exactly one
commitment derived from a structural property of the game: a population of distinct, attributable
behaviours, used as sparring partners by every suite and as one-mechanism testbeds.

## Doctrine

**Question.** Which single game mechanism, exploited by one bounded rule on an unchanged
economy, changes what a bot does and what it wins?

**Shared hypothesis.** The game's structure leaves local advantages that a simple rule can take
without a planner or a model: quantized damage, renewal timing, capture occupancy, partial
vision, linked bodies, inventory feedback and interruptible channels. Each rule makes a
falsifiable local prediction (fewer hits, earlier conversion, less damage taken, delayed enemy
income) that its own control arm on the same chassis can test.

**Held fixed, by convention rather than shared code.** The B0 chassis: draft Tavi, Varg,
Vendetta, Vesper Vale, Marcella; clear, capture, then tribute. A camp is affordable when the
predicted loss leaves 25% health; a champion retreats at 35% health or at 2x incoming + 60. The
build is Monster Strength, Jungle Health, Power Strength. Banking happens at 24 gems, within 4
tiles of an altar, or in the last 100 rounds. The chassis also bounds fuel and native fields,
catches errors at the turn boundary and writes bounded BC26T telemetry. Every bot owns a private
copy of all of this: the suite has no shared module.

**Varied.** Each bot changes exactly one decision, plus the one roster swap its commitment may
need: Ren for Varg (porcupine), Cassi & Issac for Varg (extra-credit), Prism for Tavi
(dragons-hoard). The bots fall into four groups: time and flow, combat thresholds, space and
information, and coupling between actors.

**How it differs from neighbouring suites.** Specialists have no planner, model or coordination
layer; a change that needs one belongs elsewhere. *decision*, *models*, *frontier* and *events*
choose among complete jobs through a shared rule, model, representation or event-time value.
*doctrines*, *territory* and *commitments* reorganise the whole economy, its geography or its
strategic interaction. *distributed* and *adaptive* study coordination without shared memory and
uncertainty-gated adaptation; Choir and Extra Credit coordinate only through a static draft-time
plan or a fixed partition. *main* tunes for ladder strength; specialists never import its policy.
Develop a bot only by deepening or narrowing its own commitment, and never adopt another bot's
commitment as a fallback, from this suite or another.

## Bots

State: unless qualified below, *witnessed* means the predicted local effect was seen in a paired
diagnostic game under engine 0.0.3, on development or lab maps. That evidence is stale; Fireflies'
current engine 1.0.2 assessment follows the table. Nothing here is a strength claim. All sixteen build on CLI 3.0.4 / engine
1.0.2 and pass the suite tests; all lose almost every game to main/main and the main anchors
(`bots/main/DECISIONS.md` D79).

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| fireflies | Bank at 12 carried gems instead of 24, keeping the nearby-altar and late clauses: smaller exposed bags, more frequent conversion | Explicit repaired variant has 80/80 clean frozen-reference games and physical banking in 4/4 preservation games; defaults still reproduce 20 exhaustions and one stall; healthy cross-suite and neighbor attribution remain open |
| clockwork | Farm a circuit of up to 3 camps, timed by personally observed respawn clocks that go stale after 16 rounds. Defer a camp that will not be due on arrival; wait at most 2 turns when a fresh timer is 2 or less | Paired timing witness blocked; its only normal win was a gold tiebreak |
| conveyor | While holding a capture, attack a different reachable, fully observed group from inside the held region if the holder survives that group's retaliation | Native single-flag comparison witnesses nearby camp preparation with the held capture preserved; measured HP cost, later conversion and health remain open |
| greenhouse | Buy Treasure Strength first (then Jungle Health, then Power Strength): income before damage | Treasure reward witnessed; payback before the deadline unproved |
| breakpoint-brigade | Tavi buys the cheapest 1-5 shard Monster or Power Strength bundle that removes a basic hit on the target camp, instead of B0's bulk buy | Current real-SDK caller buys 3 shards for 90 gold versus B0's 5 for 150, reserving 60 for this owner turn; native purchase and clear continuation remain unverified |
| integral-knights | Order targets inside a camp by incoming damage per required basic hit (weighted completion time) | Lower damage taken witnessed in a lab; area damage and stuns break the model |
| domino | Vendetta orders basics and resets so that kills land while a slot is cooling, chaining kill resets into extra attacks | Lethal reset with next-turn reuse witnessed |
| porcupine | Ren replaces Varg, built Jungle Focus then Power Health. He fights from tiles with at least 2 attackers and retaliation/incoming of at least 0.5, keeping a 25% health reserve | Retaliation and item contrast witnessed; superiority over B0 unproved; draft-only control never played |
| catapult-club | Once Tavi owns Monster Strength, he sieges with range-4 spells from tiles outside every monster's reach, then captures and banks | Repeated safe casts witnessed; the full siege route was censored |
| rabbit-holes | Vendetta takes one 2-step dash edge over a hole, chosen at draft time, when it saves at least 4 steps on the route | Crossing witnessed on a lab map. The drafted edge (the same tile pair for both sides, taken from the first 32 candidates in tile order) passes its walking-separation check on 19 of 40 pool maps (`tools/dash-edges.py`); elsewhere Vendetta walks like B0 |
| lighthouse | Place the ward only at a fork, where the runner-up eligible camp scores at least 75% of the best and a spawn is hidden, so the observation can change the choice | Ward placement never exercised; fuel-heavy (v3/v4 exhausted fuel under 0.0.3) |
| reservoir-wardens | When ahead by 48 or more tributed gems with an empty bag, Varg occupies a nearly due, enemy-side camp spawn to hold its respawn for up to 32 rounds | 32-round respawn delay witnessed, at the holder's opportunity cost; can fire only while 48 gems ahead |
| choir | In the opening, Varg clears with Tavi and leaves for another safe job once he sees Tavi holding the capture, so the whole reward goes to Tavi | Lab release witnessed; never released in ordinary screening games |
| extra-credit | Cassi & Issac replace Varg and work independent even/odd camp partitions. Each enters at 70% health or more with a predicted loss of at most 20%, and retreats at 65% | Independent jobs witnessed in a lab; linked-death cost and roster attribution open |
| dragons-hoard | Prism replaces Tavi and carries gems to 144 before tributing (Focus grows with every carried gem). Overrides: a winning bag, and deadlines 128 rounds out or projected | Hoard damage gain witnessed at 0.2 Focus/gem (the rules now give 1.5); conversion exposure open |
| gremlins | Varg carries an altar lock and places it on a visible enemy tribute of 24 or more gems, pursuing for at most 8 turns, to cancel the channel | Interruption delaying enemy conversion 4-5 rounds witnessed; net denial unmeasured |

Dragon's Hoard fresh-reference check (2026-10-24): retain the original 144-gem/296-Focus
conversion and renewal, and the separate capture-defense cases in the
[dual-generation panel](../../results/population/dragons-dual-generation-regression/native-run-index.json).
The [fresh v44 checkpoint](../../results/population/hoard-v44-exploration/index.json)
adds four healthy injected games against frozen `anchors/main-v44@b3ff333b6bcf`, both sides.
Original `4ddafd962131` reaches only 14/21 gems on Gem Fields B/A. Separate capture-defense
`58f571d5bd40` on Corridors A retains a 29-gem payout through another 29-gem payout,
then loses all 58 to a camp at R821; Corridors B has no Prism payout. None repeats 144.

The source-qualified 21-gem cast cannot distinguish the 144 commitment from ordinary
banking. The 58-gem R810 cast overkills its 120-HP target, so extra raw damage alone
does not establish useful gain. Current strong-reference applicability remains unknown;
losing does not retire these targeted cases or justify lowering the threshold. If reopened,
inspect the existing Corridors A Prism-0 R810–821 safety window before any bot-local change.
Preserve the threshold, draft and earlier evidence scopes. No gameplay, default or admission
change follows from this check.

Gremlins health triage (2026-10-23, claim `claim-0107-bb68d7da`): the current
[`RAW80 qualification`](../../results/population/gremlins-current-qualification/index.json)
first exhausts on Quadrants B, native Varg 6 at R279/input4651. Saved fuel records 100,000
then 3,278 on his next owner turn; the engine event and telemetry turn gap confirm suspension.
That Varg does no camp damage, earns no Gold or gems and banks nothing in the full match,
while all five frozen-reference actors do paid camp work. His exhausting guest operation,
target and recovery latch remain unknown. Empty saved debug/CLI logs and public replay state
cannot identify that callsite, so this triage adds no query, gameplay change or default knob.
The Tribute24 lock commitment remains unchanged; the published health failures remain open.

Conveyor decision (2026-10-22, claim `claim-0033-26f737f1`, engine 1.0.2): retain its unusual
capture-overlap commitment with a scoped causal witness. Two new full-length Gemfight A games
compare `specialists/conveyor@71b191fe203d` with the single-flag `core_enabled=false` control
`fa81c1ba6715`, both versus `decision/belief@9154051873bf`. The baseline reproduces the saved
canonical row and all seven replay payloads; the original full statistics JSON is unavailable.
From initialization, 217,901 full represented-world frames match before the first difference
at R1939. During native Tavi's hold on `g_603`, enabled Conveyor changes `g_697` member 33
from asleep with 120 HP to dead and wakes its peers. In the control, all three remain alive,
asleep and readable throughout R1939–1942. This establishes feature-caused nearby camp
preparation, without identifying the exact attack/cast, target or damage amount. Dead member
33's later HP read returns status 97: its HP is unknown, not zero.

Both arms preserve Tavi alive on the original held tile through the only capture payout at
R1941, with Tavi the sole eligible recipient and his bag changing from zero to seven. The
measured tradeoff follows payout: enabled Tavi has 300 HP at R1942, versus the control's 360;
this is no net-strength claim. The next camp's capture, own banking, fresh applicability and
health remain unproved. Both subject cells have no faults, errors or exhaustion, but Belief's
zero-clear stall makes it an unqualified opponent, and Conveyor's eight exhaustions across 28
historical exact-source games still block qualification. No gameplay/defaults changed.

Pinned pair, first-difference comparison, bounded control camp read and accepted synthesis
live in [`conveyor-control/measurement.json`](../../results/population/conveyor-control/measurement.json).
The earlier saved context and tooling-location mapping remain frozen in
[`conveyor-context/relocation.json`](../../results/population/conveyor-context/relocation.json);
its reader entrypoint is `tools/replay/conveyor/native-camp.mjs`. Further work must resolve a
specific health or later-conversion question; this witness requires neither a new action getter
nor a generic runtime repair.

Conveyor health follow-up (claims `claim-0034-650fa153` and `claim-0035-2caf4cd2`): unchanged
reproductions recover all eight retained exhaustion owners across four known conditions. Each
canonical row and all seven replay payloads match. Gold Rush B versus frozen Dividend identifies
Vendetta at R94; Quadrants B versus frozen Payday identifies Vendetta at R109. Concentrated
Wealth versus frozen Cross-type Fork identifies Vesper Vale at R312 and Tavi at R338/R1383 on
A, and Vesper Vale at R118 and Tavi at R941/R943 on B. Thus Vendetta-only is refuted in this
sample: two Vendetta, four Tavi and two Vesper Vale events. IDs are local to each game's explicit
event-owner/roster join; chronological slots never substitute for identity. Guest call sites
remain unknown, and these late failures do not justify copying a startup repair or assuming one
shared cause. The original full statistics JSONs were unavailable, so no identity with those
absent bytes is claimed. See [`conveyor-health-actors/measurement.json`](../../results/population/conveyor-health-actors/measurement.json)
and the reused [`Gold Rush receipt`](../../results/population/conveyor-health-reproduction/measurement.json).
Those reproductions changed no source/defaults; exhaustion still blocks qualification.

Conveyor inventory experiment (claim `claim-0037-e768e4fd`): keep
`CONVEYOR_REFRESH_INVENTORY_ONCE` off by default. Enabled, the two progress calculations borrow
the owning inventory already read by that invocation of `refresh_equipment()`; nothing is
cached across refreshes, purchases or turns. Current default source `a91b8946cf30` has the exact
incumbent raw and injected WASM hash `faa6f93d…`; it is a new source identity with a binary
bridge to `71b191fe203d`. Enabled variant `908a46d52e8f` is an experiment, not an admitted bot.
Fifteen whole-Bot/real-SDK caller fixtures witness 3→1 inventory reads per refresh and 6→2 in
the tested shop/turn paths, with equal fields, purchases and checked error outcomes under
identical declared fuel. The fixture's metadata/purchase effects are synthetic; it proves
neither complete import-trace equality, the main-loop catch, native fuel nor suspension.

Eight normal games pair the original with enabled on the four retained failure conditions;
all original rows and seven-payload histories reproduce. Quadrants B loses its R109 exhaustion
and maximum recorded fuel falls from 100,000 to 97,817. Gold Rush B still exhausts once, and
Concentrated Wealth A/B still exhaust three times each at the same actor-bound timestamps.
Their internal replay payloads also remain equal to their paired originals. Thus this measured
query reduction helps one condition but does not resolve the other seven events or diagnose
a failing guest leaf. On those two identical recorded histories, matching headers, budgets,
rosters and entry counts permit ordinary-entry fuel comparison using the existing parser's
draft/first-play exclusions. Recorded fuel totals fall by 7,676,056 on B (8,799 entries) and
7,936,093 on A (9,202 entries), with median delta −878 on both; 11 B entries and eight A entries
cost more. This is scoped aggregate native cost evidence, not a fixed per-query saving or
identical guest path. Health remains blocked; no broader/fresh coverage or strength benefit is
claimed. The incumbent overlap/capture artifacts remain frozen, while enabled overlap
applicability is unverified. See [`conveyor-inventory/measurement-v2.json`](../../results/population/conveyor-inventory/measurement-v2.json)
and its [`saved-fuel comparison`](../../results/population/conveyor-inventory/fuel-cost.json).
Run its explicit caller test with arena's Python and
`bots/specialists/conveyor/tests/inventory-caller.py`; shared suite tests remain unchanged.

Fireflies decision (2026-10-22, claim `claim-0003-eda2dc2f`, engine 1.0.2): use the explicit
experimental variant `specialists/fireflies@f6c57ee622a9` with
`FIREFLIES_BOUNDED_STARTUP=1 FIREFLIES_CONNECTED_CATALOG=1 FIREFLIES_CAPTURE_DEFENSE=1`.
All three defaults remain off. The default executable `ea6f135361a1` is byte-identical to the
original `cbc61de16ccc`: raw and injected WASM SHA256
`7482cbd253964b35acb9f02ab99d4b172e267ca38895d97283bcfbc25f02407e`, and all seven internal
entries in each of 80 original native bundles match exactly. The legacy contract remains in
repair, with 20 round-one exhaustions on Road of Riches/Islands A/B and one Islands B stall.
Original banking and neighbor evidence remains under `results/population/fireflies/`.

Observation: initialization itself exceeded first-turn fuel on those maps. A first eight-group
schedule removed exhaustion but took six to nine setup turns and introduced another stall;
that result changed the schedule, not the bank threshold. The retained startup repair falls
through setup, at most 64 groups, and finalization while a 60k reserve permits it, then starts
gameplay on a fresh turn. A remaining Islands B stall came from a fuel-truncated, sorted prefix
of unreachable camp groups. The second knob caches only static terrain connectivity, admits a
group if **any** spawn is reachable, and recomputes after displacement into another component.
Rank, index bias, tie order, live occupancy refresh and local ability rules remain unchanged.
The atomic setup/finalization and per-group reserve are measured over this pool (at most 30
members per group), not a fuel proof for arbitrary new maps.

The next counterexample was Stampede B: independent native inspection found five living,
motionless Fireflies actors from rounds 70 to 2000, zero capture transitions, and personally
visible enemies on their reward-available camps. Interpretation: the unconditional capture
hold preempted existing legal defense. The third knob tries that existing in-place combat
against a visible enemy on the same group. Without observed damage/removal, the hold expires
after `2 * queried capture_time + 1` ticks; both capture and farm guards suppress immediate
reselection until the same bounded interval expires. Leaving, group changes and death reset
the episode. Ordinary uncontested capture remains intact. This is an execution repair; the
threshold-12 commitment and its exception clauses are unchanged.

The final same-run panel is 160 games: default and repaired, each on all 40 pool maps and both
sides against `anchors/fundamentals@85ce679dc7b1`. Repaired subject **and opponent** have zero
faults, errors, exhaustion entries, zero-clear stalls, suspended or failed games; ordinary
subject fuel peaks at 92,551. The baseline reproduces its 20 exhaustions and one stall; its
opponent separately has three stalls. The 57/80 repaired wins versus 35/80 baseline wins are
descriptive and do not decide retention. Root's separate 80-game full-pool qualification
against `territory/harvest-and-exit@dca7da42b1d4` is also clean for Fireflies, but that opponent
has 10 exhaustions and 12 stalls. It does **not** supply healthy cross-suite coverage.

The final 12-game preservation panel compares default, repair, and repair plus
`core_enabled=false` on Lost World/Collision, both sides. All are technically clean. All four
repair games contain a decoder-confirmed loaded walk, successful tribute, empty living actor
and subsequent movement; 30 recorded threshold admissions are censored lower bounds. On
Collision B, Varg has 13 gems at (15,30) on round 469, starts tribute on 481, finishes on 483,
and moves on 484. The repaired control matches the entire recorded own state on 469 and still
carries 13 gems on 483. Collision A also changes conversion timing. Lost World's control
converts at the same time, so those cases establish preservation rather than a timing gain.
The earlier neighbor's matching own state occurs at a different round and does not isolate
full branch-preemption inputs; nearest-neighbor attribution remains incomplete.

`fireflies/tests/run-variant.py` runs the existing production runner against a disposable
applied source tree, with static assertions of the requested repair knobs. The four ordinary
fixtures cover banking boundary/refusal, ANY-spawn connectivity, capture defense/expiry, and
control resolution. The exact three-knob invocation additionally runs `run-caller.py`:
complete production callers compiled against the actual SDK, native replay queries/actions,
reached private checkpoints and preemption/missing-query controls. Each invocation keeps
separate output and build paths. Reproduce variant fixtures with the pinned arena
Python and `--set` once per repair knob, plus `--out` under the owned evidence directory.
Exact flags, source/fixture hashes, test identities, selected health rows, native witnesses
and conservative admission assessment live in `results/population/fireflies-startup/manifest.json`.
Remaining decisions require a healthy cross-suite opponent and a discriminating nearest-neighbor
comparison. Clean frozen-reference health alone does not admit or enable the repair.

Fireflies current-strength screen (25 October 2026): the exact existing three-switch
variant `f6c57ee622a9`, RAW `d2548286489b...`, loses all **80/80** games against
released `anchors/main-v53@fa5b8ae8b5ae` on the40 official pool maps, both sides.
Both teams have zero recorded faults, fuel exhaustion and error lines; no game is
truncated. The normal RAW experiment reused validated warm inputs without builds,
snapshot copies or SDK restoration, and is closed. This measures the experimental
variant, not the distinct recipe-{} default. The historical threshold12 bank/move
contribution remains, but the current win floor fails. All three defaults stay OFF;
no further threshold search or native attribution follows this strength screen.
[Exact inputs, rows, limits and decision](../../results/population/fireflies-current-strength/index.json).

Caller decision (2026-10-22, claim `claim-0015-d65a2c29`): retain a scoped conditional
replacement distinction on Collision B. Real initialization and a checked full-HP Varg move
from (5,38) to (6,37) establish an off-spawn recovery-false private checkpoint. The entire
guest memory is then retained while the observable world is explicitly replaced by native
B469 state; this is not a historical joint trajectory. Fireflies requests banking and its
repaired control and Breakpoint decline, yet all three first move to (16,29). On B470 the
active caller moves to (17,28), while both replacements move to (15,28) after naturally
changing farm target. Each replacement has 110 exact native reference frames before the
split, excluding only timeline identity bytes48-55. The provisional common-continuation
oracle failed on this new result; comparing each actual physical arm changed the decision.
The active-only continuation stops before an unresolved r472 peer Cast; no passive input
replaces it, and the earlier split requires none. Declared world/fuel interventions and fixed
retained peer inputs limit the inference. Existing autonomous banking witnesses separately
prove activation/completion. Twenty-one caller cases include both partial-clear values and
recovery, capture, channel, low-fuel and missing-query controls. The adapter rechecks all pins
and independently reruns the three actual native callers before passing this conditional
route. Complete receipts and source/SDK pins live in `results/population/fireflies-caller/comparison.json`.
Gameplay snapshot `f6c57ee622a9` and its native health are unchanged; healthy cross-suite and
fresh-applicability gaps remain.

Breakpoint caller decision (22 October, source `babb2b9cb23d`): the complete production Bot
and real SDK wrappers, given explicit current-catalogue facts, initialize all 34 Lost World
groups and select visible `g_113` without a target override. Active buys three Monster Strength
shards for 90 gold and reserves 60 for this shop/owner turn; `core_enabled=false` buys five for 150.
Independent repeated float32 subtraction gives nine isolated basics before and six after either
bundle. The positive supplied Tavi is injured on spawn and returns after shopping: no basic,
clear or capture is executed. Eleven caller cases verify purchases, combine/passive effects,
refusals and B0 fallthrough. The separate 7,425-row offline survey compares only bundle
`(item,count,cost)` signatures from two Python enumerations, using the old ceiling versus
repeated float32 subtraction; it is not 7,425 production calls or universal rounding evidence.
It found no bundle difference within its declared full-HP/equipment bounds. No gameplay repair
follows; the generic 649/129.8 example and native zero-purchase summaries do not establish one.
Gold may be spent by a later shop call. Native activation, damage/clear continuation, health
and admission remain open. The [record](../../results/population/breakpoint-caller/assessment.json)
preserves exact commands, conditional inputs, host failures and independent review.

Replay-observer integration (claim `claim-0046-07eadedf`, CLI/SDK 3.0.5, engine 1.0.3):
the two Fireflies contracts now explicitly bind 15 repository dependencies instead of five,
changing their measurement contract identity. The adapter calls the contained bridge directly;
the bridge owns the shared slot, private Job, timeout and confirmed cleanup. Ten transport stub
tests, 34 Node output/interaction tests and 25 pure adapter integration checks passed. Gameplay
C++ is unchanged. The new quick/full manifests and executable sidecars are unadmitted. Eight
fresh Collision/LostWorld quick cells, both sides, completed with clean health for both teams;
all 88 uncompressed archive payloads and eight decoded statistics objects equal the old same-key
run. Contribution coverage remains unknown; the full 160-cell panel is unexecuted.

The actual one-round adapter smoke decoded all seven expected actors through one matching
contained bridge dispatch with an empty Job. Its original result remains **FAIL** at the frozen
single-observed-PID gate: two owned PIDs were observed and the second has no executable identity.
Later parent-environment and pending-cleanup assertions were not executed. This is a bounded
transport result, with no single-OS-process, behavior or admission claim. The
[integration record](../../results/population/postcast-trace-integration/index.json) retains the
new generation, tests, failed gate and root's scoped disposition.

## Distinctness rules

- A bot is distinct only while its commitment changes choices: draft, purchase, target, tile,
  timing or bank moment. Two bots that make the same choices count as one; merge them or retire
  one. A specialist whose trigger never fires plays like B0, which is already an anchor. Measure the
  fire rate before claiming distinctness (BC26T summaries count opportunities, attempts, actions
  and witnesses per actor), and use `arena ladder styles` against other suites.
- Close pairs stay apart along their own axis. Conveyor overlaps work within one actor; Choir
  moves work between actors. Extra Credit adds an actuator; Choir decomposes a fixed roster.
  Clockwork harvests on the respawn clock; Reservoir Wardens withholds it. Gremlins interrupts
  channels; Reservoir Wardens blocks spawns. Fireflies banks early; Dragon's Hoard banks late.
- Every bot keeps a control arm. `core_enabled=false` switches the commitment off on the same
  chassis. `draft_only=true` keeps a swapped roster with B0 policy (porcupine, extra-credit,
  dragons-hoard); for extra-credit it is the only informative control. Build control arms with
  knob overrides on a snapshot (`arena play <bot> <opp> --set core_enabled=false`), never by
  editing `profile.h`. The tests check that each `core_enabled` knob resolves.
- Shared changes: there is no shared module by design. A module introduced under
  `bots/specialists/<module>/` has one owner at a time, and every change to it affects each bot
  that includes it, so it is rebuilt and re-tested suite-wide. Chassis fixes follow the same
  rule: one owner, applied to every copy with the defect, whole suite rebuilt and re-tested.

## Layout and checks

- `<bot>/src/` is the whole bot. `profile.h` holds identity and knobs: `core_enabled`,
  `probe_mode` (keep false) and thresholds. `policy.h` is the bot's copy of the B0 policy.
  fireflies, clockwork, conveyor, greenhouse, breakpoint-brigade, integral-knights, domino and
  porcupine keep B0's runtime in `main.cpp`. The other eight split it into `runtime.h` (B0
  runtime), `core.h` (the commitment's state machine), `draft-plan.h` (draft and any draft-time
  plan) and `telemetry.h`.
- Pure mechanism kernels: `clockwork.h`, `breakpoint.h` (hook in `shop.h`), `siege-policy.h`,
  `lighthouse-policy.h`, `reservoir-policy.h`, `mission-policy.h` (choir),
  `extra-credit-policy.h`, `hoard-policy.h`, `gremlins-policy.h`, and the `policy.h` of
  fireflies, greenhouse, integral-knights, domino and porcupine. Conveyor's rule lives in
  `main.cpp`; Rabbit Holes' rule in `draft-plan.h` and `core.h`.
- Tests: `./arena.cmd test specialists` or `./arena.cmd test specialists -- [bot ...]`
  runs every `<bot>/tests/*.cpp` (self-contained, own `main()`, picked up automatically):
  policy-kernel fixtures and mocked-clock BC26T framing fixtures (gremlins, lighthouse), plus
  a check that each selected bot's `core_enabled` knob resolves exactly once. Clockwork, conveyor
  and rabbit-holes have no kernel fixture.
- Build: `./arena.cmd build specialists` (cached snapshots; `--in-place` builds the working
  directory). Suite tool: `tools/dash-edges.py` lists the pool maps where Rabbit Holes' drafted
  edge is accepted (static geometry from `maps/geometry.json`, mirroring `draft-plan.h`/`core.h`).
- Telemetry: each actor writes at most 1 start, 32 detail and 62 summary `BC26T{json}` lines
  (at most 512 bytes each, dropped below 8000 fuel). Keep the word "error" out of them: arena
  counts stdout lines matching it, and only the turn-boundary `bot error:` line should match.
- Dormant probe code is compiled in (`probe_mode` branches, `probe.h`, `probe.inc`, `probe_*`
  selectors keyed to engine-0.0.3 development and lab maps); production never enables it.
  Removing it is a source change: rebuild, then recheck fuel.

## Leads

Open problems:
- Re-measure every bot on engine 1.0.2 across the pool: enabled against its control arm, both
  sides, errors and fuel inspected. Record fire rates, and retire bots that never fire.
- Never exercised: Lighthouse ward placement, Choir's ordinary-start release, Clockwork's paired
  timing. The draft-only controls (porcupine, extra-credit, dragons-hoard) were never played, so
  roster and policy effects are still confounded.
- Premises changed. Prism now gains 1.5 Focus per carried gem (it was 0.2), so re-derive Dragon's
  Hoard's 144-gem target and deadline. Recheck other numeric premises against
  `docs/reference/GameRules.md` before refining a bot.
- Fuel: Lighthouse (two terrain fields plus a placement scan) exhausted fuel under 0.0.3 before
  v5. Fireflies' explicit repair is clean on the current pool above; its working defaults still
  reproduce the old round-one failures. The other fifteen lack a current full-pool health
  assessment. A build proves nothing.
- Hygiene: stale `version_id` labels (they feed telemetry); breakpoint-brigade's unused
  `bank_threshold = 12`.

Next ideas (growth path of each commitment):
- Temporal scheduling (clockwork, conveyor): release-time scheduling, pipelining neighbouring
  groups. Role assignment (choir, extra-credit): reward recipient by next upgrade or bank
  distance, separation limits for correlated death.
- Adversarial resource control (gremlins, reservoir-wardens): capture resets, channel-deadline
  interception, minimax cost of denial. Sensing (lighthouse): uncertainty age, compact beliefs.
- integral-knights: exposure by position, retreat by escape time. fireflies: altar choice by
  total conversion cost, deadline-aware final tribute. greenhouse: investment horizons tied to
  named thresholds. domino: event-triggered action sequences. porcupine: feedback control of
  exposure and reserve. catapult-club: Monster Focus against Strength while sieging.
  rabbit-holes: draft the edge nearest our own routes instead of the first 32 in tile order, then
  crystal edges priced by channel, purchase and single use. dragons-hoard: optimal stopping with
  loss-sensitive carry limits.

Idea bank (unimplemented designs worth keeping):
- Chameleon: Tavi sells at spawn for full value; re-spec Strength and Focus at a recovery visit when a named camp threshold changes.
- Loaded Dice: seeded, persistent choices among near-equal routes to frustrate route-predicting hunters, at a bounded efficiency cost.
- Turnstile: intercept a visible channel at its last useful moment with a legal stun; fixed against planned next-round priority.
- Second Breakfast: prefer camps seen damaged or cleared when an uncontested capture is cheaper than clearing them ourselves.
- Checkmate: near the end, secure the exact winning deficit or a feasible denial action; both scores are public.
