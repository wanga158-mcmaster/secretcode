# contenders: rivals for the submission

One coordinator agent owns this suite and its three bots, and spawns subagents for the heavy
lifting. Coordinator: read [AGENTS.md](../../AGENTS.md),
[workflow](../../docs/workflow.md) and [the live reference](../../results/releases/LIVE.json);
then use your own judgement. Main and main-anchor source are outside this work. This charter
sets the direction, not the method.

## Why the contenders exist
`main` carries our online rank alone today. The contenders are its rivals for that place: three
bots that compete for the submission on any patch, and should come to outplay it. That asks for
two things at once, and neither is optional:
- **Strength at least comparable to our online versions, then beyond them.** The first bar is the
  live anchor (`results/releases/LIVE.json` names it; currently `anchors/main-v53`) and its predecessors;
  there is no ceiling.
- **Independence.** Each contender takes a different route through the game, so one failure mode
  (a patch, a counter-strategy, a blind spot) cannot take out main and the contenders together.

Which idea each contender holds, what it is called and where it starts is the coordinator's call.
Choose directions that are genuinely different and grounded in what the foundation shows; create
the bots with `arena new bot contenders/<name>`, named after their ideas, change course when
understanding says so, and record why.

## Current coordination: mastery of the game

**PAUSED by the user on 25 October 2026 at 20:54Z.** The active goal remains
to beat the actual main bot, but it is paused, not complete. No new contender
builds, games, probes or implementation work should start until the user resumes.
All three owners have saved their source, partial drafts and exact restart state
in their existing `NOTES.md`; those PAUSED checkpoints supersede the older running
statements below. Both complete retired implementations and protected modules
remain on disk. Main is unbeaten; no contender is promoted or qualified.
All owner children have stopped and released their leases. Confirmed owned
live-handle count is zero for each bot; every answered experiment is closed and
raw-clean. Warband's already-active R3 build finished safely, with no R3 game.
Mosaic's unapplied draft is protected under its narrowly scoped cache `.keep`;
the incomplete Runtime is explicitly unverified. No feature was completed merely
to make this save checkpoint.

Restart from [Timetable](timetable/NOTES.md), [Mosaic](mosaic/NOTES.md) and
[Warband](warband/NOTES.md), verifying each recorded handle rather than repeating
a completed deterministic key. Timetable's latest diagnostic isolates Step
crossing an owner input, and its next proposed work is a bounded static NPC
potential field. Mosaic's R21 service integration is partial. Warband R2 is
closed/raw-clean; R3 build outcome must be read from its saved checkpoint and
no R3 game is authorized during the pause.

The renewed 24 October mandate owns this suite and all three contenders. The shared foundation
is local to [bots/contenders/foundation](foundation/README.md). Earlier directions below that
point new work at `bots/main/foundation/` are historical and superseded. Do not follow them or
inspect main/main-anchor source; compare opaque executables with their exact identities.

One owner develops each bot, one curates the foundation, and an independent evidence reader
checks reference identity, completed execution, health and claim scope. The coordinator owns
this charter and integration. Each owner continues from the newest actual assessment, rather
than repeating a deterministic key because an older note still calls its job running. Existing
bot `NOTES.md` files retain decisions, source identities and restart commands; existing durable
indexes retain evidence. There are no online permissions.
The current table below and each bot's early checkpoint take precedence over the retained
research narrative. Older statements that a job is running are not evidence of a live process.

The user's renewed mandate is to understand the game and derive three independent contenders
from that understanding. The canonical shared foundation is
[bots/contenders/foundation](foundation/README.md): knowledge about mechanics, information,
action interactions and emergent behaviour, not a common bot implementation. Champion micro
must account for actual movement, the single cast, basic attacks, resets, actor order and
survival. An isolated policy change or a promising parameter sweep is not a bot direction.
This mandate supersedes the package-first method recorded below.

Models and proofs support complete playable controllers. Definitions, lemmas and applications
belong with their relevant local foundation entry; a separate main-owned mathematical core is
not a dependency. Proofs state their model and premises;
native contracts/observations, finite implementation refinement and open obligations remain
distinct. Safety does not imply progress, and a feasible plan for each hidden state does not
give one deployable observation-consistent policy. The maximum 17 available agent slots were
used for independent code owners, mathematical domains, evidence and proof criticism.

The active goal is to keep going until a contender beats the main bot. A completed research
cycle, a proof review, an isolated task or clean technical health is progress, not completion.
The user clarified the readiness horizon as two weeks from 23 October: 6 November 2026;
no exact hour was supplied. This does not authorize online actions or promotion.
The competitive claim needs the current authoritative main executable, all pool maps/both
sides with same-run paired controls, clean measured health and fresh map/opponent confirmation.
Tune on pool/train; generated `fresh` maps are reserved for claims and never used as probes,
alongside unseen heldout or legacy confirmation under the updated working agreement.
Check useful production throughout the game: one early clear can hide a later liveness collapse.
No online action or main-source inspection is authorized by this goal.

The coordinator owns this suite and the foundation; each active bot has one delegated owner.
A separate foundation owner curates reusable findings, and an independent reader challenges
shared assumptions. Owners carry complete implementation/evidence cycles, communicate findings
directly, and record state in their existing NOTES.md. No online actions are authorized.
Live is currently v53, `main/main@e39f62accd4a`, since 25 October 00:06:14Z;
`results/releases/LIVE.json` remains authoritative. Its protected RAW and INJECTED opaque modules
both hash to `bd6b051394b36df5849805130976acfab1a0b4361d412dc0f26c886d72419b61`
and are 1,145,235 bytes. Bindings and verified bundle metadata live in
`.cache/contenders-audit/opaque-reference-v53/binding.json`. Historical v52 roles are the
different `1e701d49...` module; historical v51 roles are the
different `61a5cef9...` module; v44/v42 roles are `5aa47517...`. Their gameplay results do not
transfer to v53. The audit preserves both historical bodies and flags release timestamp drift
without inferring a binary change from dates. Main and main-anchor source remain unread;
all new current-main claims must bind the currently verified LIVE executable.
The fresh v53 audit verifies original-to-protected byte equality and unchanged
pinned bundle metadata. The complete prior v52 audit, including its timestamp
observations and v51/v44 history, is preserved under `historical_v52`. Existing
v52 games remain historical exact-byte evidence; their gameplay does not transfer
to v53. No contender has yet qualified against this new current reference.

Fresh v53 RAW passive calibration now covers all 40 current pool maps and both
sides: 80 unique keys, all reaching 1,000 gems. Time to the native score event
has median 929 rounds, mean 950.4375 and range 349--1,725; every terminal win is
in a partial final round, one beyond the completed-round counter. Both teams
have zero faults, errors, exhaustions and fuel-budget crossings in all four
phases. Main has zero stalls; passive's 80 stalls are intentional. Independent
receipt, production, cleanup and LIVE audits passed; the experiment is closed
and all raw per-game outputs were removed. The [summary](../../results/contenders/throughput-bounds/main-v53-passive/summary.json)
and [per-key pace](../../results/contenders/throughput-bounds/main-v53-passive/pace.csv)
are absolute economic calibration, not contender strength or a paired version
improvement claim.

The 25 October 00:19Z checkup found the repo at 20.9 GB, shared arena cache
at 9.65 GB and global results at 3.18 GB. Contender scratch remains below its owner
limit. Slim removed nine expired regenerable files; prune subsequently completed,
removed/trimmed none and retained 1,217 pins. Global results retention needs the
integrator's attention; current contender modules, bindings and source closures
are protected, and answered contender raw archives have been removed.
The post-calibration 01:03Z checkup reports 21.2 GB total, arena 9.81 GB and
results 3.21 GB; slim finds no expired files. Do not repeat the long prune just
23 minutes after its zero-removal completion: the new experiment outputs were
already removed and current files are ineligible. Resume the ordinary cadence;
this avoids another full scan during active native work without changing
retention thresholds or protecting raw outputs.
The next periodic checkup at 03:03Z reports 21.9 GB total, arena 9.91 GB and
results 3.30 GB. Slim again removes nothing; the scheduled prune was confirmed complete at
03:20Z, removed/trimmed nothing and retained 1,250 pins. Current contender inputs
remain protected and each owner stays below its scratch limit. The only open
contender experiment at that check was Warband's two-game full-service reduction;
it subsequently closed and removed 12.6 MB before the new pressure panel.
The next periodic check began at 05:49Z: repo 22.5 GB, shared arena 10.28 GB,
global results 3.35 GB; every contender scratch owner is below the 0.5 GB
report threshold. No contender experiment is open. Slim removes one expired
regenerable file. Standard prune was confirmed complete at 05:55Z: removed or
trimmed nothing, retained 1,260 pins. All root maintenance handles are terminal.
Current source/module bindings remain protected; no native slot was reserved.
The user-authorized maintenance review now coordinates shared arena cleanup with
the integrator; contenders will reuse its custody/checkup evidence rather than
start another global prune. A read-only inventory of historical Warband
`production-health/objects` and `service-rebuild/objects` finds 881 files,
242,081,593 bytes and 844 unique hashes; duplicate copies save only 976,219 bytes.
Eight old WASMs are absent from the current owned module cache, and immutable
reproduction references still depend on these stores. They are not certified
regenerable garbage. Nothing was moved: the removed production destination
already exists, and any future relocation needs collision checks, unchanged
manifests, reversible path mapping and verified destination hashes.
The maintenance coordinator reports a complete Arena-only scan at 06:45:55Z:
10,198,968,470 bytes, with no access errors or vanished entries. Its three cleanup
passes removed 341,204,062 bytes; all jobs are terminal and no further deletion
was scheduled. This is not a newer full-repository checkup. The next shared
periodic check has been requested through that coordinator.
The coordinated checkup is now terminal at
`.cache/integrator/checkup-20261025T091551Z/`: report, open-experiment review,
slim, prune and complete Arena scan all return zero, with all five Job trees
empty. Slim removes 601 expired files and two empty directories (about 0.03 GB);
prune removes none and retains 1,260 pins. The strict 09:17:34Z scan measures
10,411,064,409 Arena bytes with zero access/vanished errors; the report gives
repo 23.8 GB, results 3.45 GB, main cache 5.43 GB and fastrun 1.38 GB. Shared
Arena exceeds its 10 GB owner limit, so the integrator has stopped new canonical
build admission. Existing protected-module games and independently owned private
diagnostic preparation may continue within storage limits. Such private builds
do not establish current-panel or win-floor qualification. Owners reuse this
central check and do not run overlapping cleanup.
The next coordinated sequence is also terminal at
`.cache/integrator/checkup-20261025T122514Z/`: report/open/slim/prune/complete
Arena scan all return zero and all five process trees are empty. The strict
12:26:41Z scan finds 10,423,580,662 Arena bytes, with zero access/vanished errors.
Slim recovers about 0.07 GB from 410 files and removes 31 empty directories;
prune removes none and retains 1,260 pins. Report totals are repo 25.1 GB,
results 3.57 GB, main cache 6.11 GB, fastrun 1.40 GB and Mosaic cache 0.63 GB.
Shared Arena remains over its owner limit, so canonical body/build admission
stays closed. The contenders continue owner-private diagnostics and ordinary
lifecycle cleanup, reusing this one central result.
The latest central check is
`.cache/integrator/checkup-20261025T183853Z/summary.json`: report/open/slim and
the independent complete Arena scan return zero; prune returns two, refusing an
external official visualiser asset descriptor. All five private Job trees are
empty. Arena is 10,423,768,945 bytes, with zero access/vanished errors; canonical
body admission remains closed. Slim recovers about 0.17 GB from 1,870 files and
264 empty directories. Do not retry prune or weaken its path guards. The sole
open experiment at that check was Mosaic's settled one-game group run. Its owner
has since closed it, retained the compact cash window and removed 10.6 MB of
experiment raw plus the 283,800-byte query output, without replaying the key.

The goal continuation remains active until a contender beats the actual main bot. The previous
turn was progress: it changed source, corrected mechanics and supplied full-game evidence, but
did not achieve that goal. Current work resumes Timetable's actual capital/renewal application,
Mosaic's complete occupied-endpoint continuation, and Warband's ordered contact-to-service
arbitration. Its native contact adapter omitted economic continuation candidates; reducing reply
counts alone cannot fix that action-family gap. A complete local operator or healthy prerequisite
is evidence toward the goal, not a replacement for the competitive completion audit.

The resumed owner lanes remain distinct: Timetable derives dated residual-work schedules;
Mosaic derives executable ordered action words and geometry; Warband compares named joint
work and owned capital with the same actors' displaced independent services. Complete native
controller applications now take priority over further isolated proof components. Timetable
already admits harmful work and recovery; its measured next gap is acquisition progress and
competitive throughput. Mosaic must close repeated production and leave its harmless-only
class; Warband must expand that class and handle actual interference. The harmless-only
deadline bound is a restricted passive-race counterexample, not a universal head-to-head
impossibility or a reason to treat Timetable as harmless-only.

An earlier historical opaque-reference check, after temporary live-version changes, found
LIVE naming v44 and all retained/cached v44 and v42 raw/injected roles hashing to
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
Fresh native comparisons use explicit opaque bindings and the existing contained compiled
runner; missing bounded-prefix statistics never count as full-game health. The evidence
auditor maintains the exact reference independently of the three code owners. Revalidate
LIVE before a strength claim; do not relabel older deterministic keys as new evidence.

The user's long-running compute request is implemented in
[`tools/optimization/`](tools/optimization/README.md): one C++ executable provides C1–C12 from
[the long-term plan](lt_plan.md), with bounded campaigns, checkpoints, reports and reusable
policies, plans, frontiers and fixtures. C3 now covers investment/cooperation/recovery model
cycles; C4 retains capture-and-departure frontiers across 32 model cases and selected complete
native Varg traces; C8 searches scenario/response families; C9 searches bounded endgame families.
Their native execution, frequency and transfer gaps remain explicit in the guide and evidence.

C1/C2/C5/C6/C7 use complete native games, C10 measures native WASM fuel, and C11 branches the
pinned native engine. C12 (`micro`) uses that engine directly for ordered primitive MAX/MIN
combat and farming search over actual equipped replay states, exporting conditional counterplay
and feasible continuation endpoints. Its current flat prefixes stop at the first selected owner
READY after each End; they cannot require a named owner's absolute-round checkpoint or enforce
observation-consistent adaptive replies. The Willow R50 comparison failed its promised R51
boundary and supplies only local primitive-composition facts. Its cutoff estimates are not
certified wins. The `qualify`
utility evaluates a frozen full-game candidate against matched controls; it does not integrate
local plans into a controller or promote a contender automatically. The registry supplies
campaign defaults and offline packets remain frozen snapshots. Preserve native counterexamples
in the canonical foundation and let measured transfer determine which products enter the next
bot architecture.

C10 currently meters its generated nearest-target kernels through a mechanism self-game;
it has no arbitrary-header/compiler-state entrypoint. The standalone WASI fixture runner
does not meter SDK fuel. Pure fake-clock checks therefore do not qualify a controller's
preparation/compiler allowance or its production code-generation cost.

Coordinator checkpoint below: 25 October 19:54Z. Each owner's early NOTES checkpoint
records newer source, handles and outcomes; read it before resuming that lane.
Timetable's previous implementation has been retired reversibly to
[`miscellaneous/timetable-calendar-frontier-20261025`](../miscellaneous/timetable-calendar-frontier-20261025/NOTES.md).
Its active directory is being rebuilt; until the new notes exist, that retired
checkpoint holds the actual audit and restart state. New Timetable notes and
observation, word and route modules now exist in the active tree; foreground
integration is in progress. After the first tool window resumed normal work,
maintenance requested another short boundary hold for cold-runtime pinning.
The corrective publication has now explicitly resumed normal owned native work:
core is `c8bdf9a72bd2d20e032258d6b1b330cfde9719f43edd0e32b82b9a78adc3cd5e`.
Timetable's 16-file replacement passed SDK/focused checks and actually ran one
full A game, which failed with 4,980 raw exhaustions and zero physical work;
formal measurement was rejected for incomplete exhaustion context. Its owner
is replacing the nonpersistent whole-world observation barrier with a local
mandatory action slice. Owners' current handles and source identities remain
in their notes. Canonical
body admission stays closed. The new private release route has passed
offline checks, but actual cold-build/fixture validation remains pending and
does not yet qualify a contender. Do not rerun or rebind completed games after
a tool publication; use the explicit stable epoch for a new launch.
Mosaic's complete R20 implementation and notes are also retired reversibly at
[`miscellaneous/mosaic-ordered-words-r20-20261025`](../miscellaneous/mosaic-ordered-words-r20-20261025/NOTES.md):
all 98 files are preserved, with the frozen controller/programme/arrival hashes
independently rechecked. Its replacement is a live compatible-word controller,
with only a documented own-archive include for OFF compatibility; preparation
must capture those transitive dependencies, and ON excludes the old controller.

| Bot owner | Structural question | Bounded discriminator at this checkpoint |
| --- | --- | --- |
| timetable | Can workers complete useful harvest, capture and delivery cycles with finite health and recovery time? | Retained YBR/passive CAPITAL banks 319/319; Capture 242/154. The global calendar frontier is retired after its actual diagnostic reached SDK collection but no usable programme. New residual source `49100d7fb43e` passes SDK/focused checks, but its first full A run actually fails: 4,980 raw exhaustions, zero damage/income on all five and a rejected formal measurement (32 contexts retained, 4,948 omitted). Source review identifies a nonpersistent complete-world observation transaction before every command; an oversized transaction can repeatedly stale/restart without any physical progress. Next: current self/selected-target/contact/channel facts authorize the foreground primitive; dated broad observations serve nomination and cannot block it. No guest-callsite attribution, adoption or current-main claim. [NOTES](timetable/NOTES.md), [retired audit](../../results/contenders/timetable/independent/875518c7e180/actual-reachability-audit-01.json). |
| mosaic | Which champion/action sequences can complete the actual work exposed by a camp's geometry? | Whole-group `5544ac1e54b6` produces one clear/26 received/zero bank on full YBR/passive A. Root repair `45dd4a75a556` leaves that result unchanged; guarded continuation `d2087ce32dce` regresses to zero clears/receipts, 4,375 damage and one death, technically clean with a stall. Its actual round 265–280 window shows Varg recover, while Ryth moves once then stands ready under repeated 80-damage batches. The private blocked point remains unknown. Independent source review establishes that live Work emits only Basic and bypasses every selected kit's richer word builder; transient route exclusions also lack fresh input lifetime. Next: replace the Basic-only whole programme with adaptive compatible words and an executable egress/cash/recovery tail. All answered experiments are closed/raw-clean. No adoption or pressure claim. [NOTES](mosaic/NOTES.md), `results/contenders/mosaic/native-r20/`. |
| warband | When does assembling a party unlock more useful work than its members give up? | Full-pool v53 pressure rejects retained economic/service controls: 0/80 and 0/79 valid wins, one invalid service health row. Guarded `d61516cd06d9` reaches 1,022/1,012 against passive at event rounds 1,734/1,969, technically clean but with zero forecast uptake. New typed diagnostic full A/B remains clean and unapplied; all 20 observed geometry refutations concern moving allies' historical constant-Hold origins, not hidden enemy statistics. That representation needs a fresh dated physical boundary. Parent is now wiring the concrete party runtime, shared intent exclusion and handoff; exact SDK word costs still require honest calibration. The represented domain remains three bodies, with no party advantage, larger-roster comparison, adoption or current-main claim. [NOTES](warband/NOTES.md), [pool diagnostic](../../results/contenders/throughput-bounds/warband-service-pool/diagnostic-summary.json). |

These are research questions, not permanent identities or claims that the implementations solve
them. Rebuild when the model outgrows the code. A genuinely new direction retires the old bot
with its notes into `bots/miscellaneous/`; preserve prior retirements and evidence. Coordinate
the move with the suite owner before reusing an active path.

The preceding turn's `contenders-timetable/capital-return-01` was closed as deferred before any
new native execution. The prepared-binding `capital-return-02` subsequently completed two new
RAW games with two validated imported EAGER controls; its positive Ryth application is now
separate from that unplayed deferral. Current active investigations and exact source/role
bindings live in the bot checkpoints and experiment indexes; verify actual handles before
treating any older running-job statement as current.

The independent source review identifies three open structural operators. Timetable needs
ordered contact continuations beyond a single move, basic or cast, including reset/dash
composition. Mosaic needs adversarial contact and failed-recall continuation after personal
vision empties; its visible scalar threat estimate is incomplete. Warband needs actual named
party assembly, individual arrival/exposure and useful support, compared with those same
actors' displaced independent services at one horizon. Fixing observation, egress or compiler
cost is necessary execution work; it does not by itself close these operators or establish
competitive mastery. Source pointers and current experiments belong in each owner's notes.

Current implementation work also addresses computational ownership and action-family coverage.
Mosaic's healthy diagnostic has ready movement rejected at a 36K entry floor after optional work
leaves 33--35K; that is diagnostic-profile evidence. The first R11 kernel regresses passive YBR
A/B to 0/26 against 218/30, and its final repair banks zero on both sides, with clean technical
counters. R11 stays OFF; R12 derives a complete return application from retained R10.
Replay-joined R11 diagnostics and
source review expose double-charged residual dispatch, unfunded at-goal observation, conflicting
field owners, and a ground-only AoE incorrectly filtered as a unit spell. These contracts are
being replaced before another pressure claim.

Warband's own movement-plus-damage compiler and named-party module have focused pure checks,
but the first integrated word profile `cc3698ab6b36` fails native fuel health on both v52 Packs
keys; context retention is incomplete after many actual exhaustions. Its exact economic controls
are clean and bank 264/43 versus v52's 1000/1003. The failed profile is not admitted strength
evidence. Source review also found an A-ready reset producer/executor mismatch, a whole-map
preparation gate exceeding ordinary fuel on large maps, and sibling-word refusal pricing that
can suppress an otherwise useful candidate. Stage attribution and equivalent bounded/local
representations precede the next native qualification. The modeled enemy words already include
combinations absent from the earlier own family. Complete
modeled alternatives, conditional certificates and actual receipts remain separate; a safe idle
input, a pure fixture or an isolated operator witness does not satisfy the active goal.

The historical opaque v44 executable has a separate, source-free passive calibration on all 40
pool maps and both sides: 80 wins, with no native faults, errors or fuel exhaustion. The median
time to bank 1000 is 990.5 rounds, ranging from 376 to 1597. On Packs the A/B times are 844/879;
on BigWorld, 1138/1090; on Yellow Brick Road, 906/871. These are economic reference measurements,
not contender head-to-head results. The [per-key pace table](../../results/contenders/throughput-bounds/main-v44-passive/pace.csv)
is retained; the old assessment is currently missing, and historical citations do not establish
current raw-archive availability. The
[current audit](../../results/contenders/baseline-current/audit.json) distinguishes accessible
bindings and historical records from missing artifacts; no fresh rehash of absent files is claimed.

Historical v51 has a separate four-key passive calibration: Packs A/B reaches 1000 at bank-event
rounds 828/854; YBR A/B at 909/905. All four have clean own technical counters, with one YBR A
death; the deliberate passive stalls. The [compact calibration](../../results/contenders/baseline-current/v51-passive-four.json)
records exact modules, maps, engine, per-body and relative final-quarter production. Ward placement,
purchases, combinations and complete economic cycles are observed. Ordered micro words and the
causal value of those operations are not inferred from totals. This is four current-reference
keys, not a contender comparison, v52 result or whole-pool v51 health claim.

A controlled native application now completes Vesper's ranged clear -> compatible dash onto
an otherwise inaccessible capture tile -> delayed recall overlapping payout -> bank -> renewed
spawn endpoint. On BigWorld A against the frozen passive, the final attack/dash is round 59,
21 gems arrive at spawn in round 65, banking completes in round 108 and full readiness is
observed at spawn in round 114. All potentially harmful camp ranges are excluded from its
routes and holds. [The retained witness](../../results/contenders/mechanics/vesper-dash-service/witness.json)
is one scripted 125-round slice; truncated row totals are not full-game statistics, and this
does not qualify a general controller or strength against main. The complete attainable
operator class, rather than its fixed route, is the implementation lesson.

Timetable's separate saved failure makes the coupled computation obligation concrete: Vesper
wakes two wolves while Ryth remains stationary with unused allowances; their 60 attacks kill
Ryth. The saved trace does not reveal Ryth's private reason for yielding. A deferred input is
a physical waiting step, so a finite safe return plan also needs executable continuation or
an explicit bound on computation delay. Sleeping camps are not a quiet-state guarantee when
another actor can wake them. The new staged services and potential-field witnesses address
this obligation; source fixes and pure fixtures alone do not qualify it.

The current implementations also leave structural operator gaps. Warband has no deliberate
party assembly stage: its optional pair acts after co-location. Mosaic's current executor has
no enemy attack operator. Neither is resolved by an activation threshold. Parallel read-only
reviews are deriving complete admissible operators while code owners repair measured execution
failures. A passive-opponent ordinary-map check is being prepared to isolate continuous
work/capture/conversion from enemy pressure. Timetable's first such check fails on its own
liveness, confirming that enemy pressure is not needed for that failure. These are prerequisite
experiments, not evidence of strength against main.

The independence audit found that Timetable's independently authored service model/executor
still called inherited main-derived draft, observation, navigation, shop, threat and protocol
code. Preserving the frozen OFF executable as a comparison does not make that runtime independent.
The complete checkpoint is intact at `miscellaneous/timetable-residual-service`; the replacement derives its own adapters and
controller from the SDK and foundation. Main and main-anchor source inspection remains prohibited.

The completed owner cycles cover Timetable's saved opportunity audit and reachable-trip rebuild,
Warband's displaced independent labor and final untraced execution, and Mosaic's clean
`arena new bot` scaffold and independent prototype without a main/anchor copy. A foundation
curator integrates reusable mechanics and independent readers challenge the inference from
physical execution to useful completed work. The retired complete Compound
directory is preserved at `miscellaneous/compound-field-growth`; its prior evidence stays bound
to its original identities. Existing miscellaneous retirements are unchanged.

Continuation toward the active goal rebuilds Mosaic's bounded observation/action kernel,
Timetable's service and residual-work controller after observing late liveness collapse, and
Warband's complete named cooperative/independent schedules. Warband's preceding local-party
implementation is retired intact at `miscellaneous/warband-local-party`. Fresh source identities
must earn their own health and competitive evidence; no historical native witness is relabeled.

The current finite-fuel evidence is in
[`results/contenders/mosaic/kernel-cost/index.json`](../../results/contenders/mosaic/kernel-cost/index.json):
56 constructed native samples distinguish observation setup, one primitive and full menu cost.
This does not qualify native-world collection or forthcoming optimized headers. The latest
cross-controller progress audit also separates temporary allowances, missing observation,
deferred computation and infeasibility: a damage-only WAIT tie, a scan that restarts at the same
ten jobs, and forced recovery after spent allowances each need their own implementation witness.

Evidence must distinguish an observed engine transition, a source inference, a model prediction
and a full-game outcome. Reuse settled mechanics and saved games. Investigate uncertainties
that affect a real execution decision; avoid expanding instrumentation after its selection
question is answered. Keep new behaviour off until it earns adoption. Compare identical keys
with a same-run baseline, both sides, and measured subject/opponent health; pool and fresh
confirmation are necessary before claiming a competitive improvement. Preserve failed results
and do not count deterministic reruns as independent evidence. There is no game budget.

The pending collection-risk correction is closed: the old gem loop wins 0/12 v42 and 11/12
starter; restricting it to independently admitted work wins 0/12 and 5/12 with four zero-clear
stalls. Removing suicidal work did not create usable supply. This justifies retiring that
implementation, not a universal solo/party conclusion. The active timetable selector was
recreated from frozen v42; its new production architecture is unqualified. Exact arm identities,
health and preserved source are in the retired bot's NOTES.md and existing timetable results.

The local foundation now states [G11](foundation/strategy.md#g11-the-complete-action-word-includes-its-continuation)
for complete ordered continuations and [C22](foundation/combat.md#c22-ordered-burst-invalidates-average-rate-escape-guarantees)
for the failure of average-rate escape guarantees. Earlier universal claims about kill value,
contact safety and party size remain qualified by their model/evidence scopes. Historical
main-model-lab references are retained history, not independent premises or new native results.

Compound's first field-growth role is rejected after its unchanged 480-game broad check:
v42 BASE40/80, generic Tavi chassis29/80, growth25/80; hunter7 BASE66/80, chassis69/80,
growth66/80. All subject health is clean. Its 7/12 v42 training result did not transfer.
This rejects the complete design; the chassis comparison is necessary before attributing the
loss to micro alone. No legacy confirmation, default promotion or retreat-threshold tuning
follows. Exact sources, opponent health and useful-work diagnosis live in Compound's NOTES.md
and its existing durable results. None of the new architectures is a qualified live replacement.
The preselected normal Tavi sample verifies applied poke/basic and reset/basic sequences,
actual stun expiry, renewed gold income and field purchases, but no Tavi gem capture or bank.
Fewer deaths without restored throughput closes this wrapper; it does not refute the individual
mechanics or identify one lost task as the cause. That complete checkpoint, including its exact
v42 default control, is now preserved at `miscellaneous/compound-field-growth`; Mosaic is a clean
successor, with no claim that a new scaffold itself is competitive.

Timetable's successor production-episode controller also fails its first 48-game comparison:
v42 BASE6/12 versus episodes0/12, starter12/12 each, with clean subject technical health.
Across the matched first 400 rounds, collected gems fall 8,113 to 3,369 and deaths rise 33 to 44.
This is a throughput collapse, not a safety gain. No broad check follows. Separately corrected
approach-exposure and gold-service omissions have a different source identity and only a small
execution-health check; those games cannot relabel the rejected cohort or establish strength.
The active bot's NOTES.md binds both stages and the actual recovery/return evidence.
The corrected traced Elkring B example verifies Marcella's actual camp work, exit, recall,
healing, return to the same camp, clear, 22-gem payout, tile release and completed tribute.
Allied damage may contribute; this is neither exclusive solo production nor a strength gain.

Warband's corrected local-party diagnostic spans two maps, both sides, v42 and starter:
16 full games, BASE2/4 and ON2/4 against v42, 4/4 each against starter, clean subject health.
Saved native windows establish actual contributions and sole 22-gem payouts on both sides.
One clear includes a third ally's final damage; below-half-health continuation and renewed
helper attacks remain unobserved. These results belong to the traced diagnostic source.
The final exception-cleanup correction is checked separately; no full-pool advantage is claimed.
The default module retains Warband's previously verified 24-key replay-preservation binding.
The final untraced candidate subsequently completes 16 full healthy games on the same two maps,
both sides, v44 and starter. All sixteen gameplay payloads match the corresponding traced
payloads without masking; both arms win 2/4 live and 4/4 starter. Exact bag/score statistics
close both selected 22-gem payout-to-bank suffixes, but controls already clear those groups
three/seven rounds later, with identical team gem collection at round 200. The two party starter
wins finish 41/23 rounds later. Keep OFF and close local activation: these cases establish
execution and conversion, with no added supply or general advantage. Warband's existing notes
and `results/contenders/warband/local-party/labor-audit.json` retain the exact scope.

All previously measured new behaviours remain off. Retired Compound's preserved default is exactly live v42's module;
Warband's is exactly its accepted replay-verified control. Timetable's earlier episode default
differs from live but matches eight complete same-key old gameplay payloads byte for byte,
without masking; that prior preservation proof remains bound to its own module. The prior untraced Timetable
episode candidate remains a separate unqualified identity. Its new physical-trip representation
has a corrected 24-game comparison: base/episodes/routes, Big World and Elkring, both sides,
v44 and starter. All measured subject/opponent technical health is clean, but the route package
loses useful throughput and is rejected. Its eight OFF gameplay payloads and terminal records
match exact-live controls without masking; differing module bytes do not prove private-state
identity. Focused production and party/protocol/error-cleanup checks pass. Existing
NOTES.md and durable indexes retain source identities, evidence limits and next discriminators;
all completed studies have closed their owned jobs and observers.

Mosaic's complete independent prototype is also rejected for technical health. Its final eight
experimental ordinary games have 142 exhaustions and eight errors; the healthy four constructed
geometry cells cannot qualify that controller. All failed generations, source closures and
exact evidence are preserved in its NOTES and `results/contenders/mosaic/ordinary/index.json`.
The next kernel must bound observation and action-transition work before further strategy
evaluation. No new controller is a qualified live replacement.

## Historical coordination: game-first reset (23 October 2026)

The user rejected the previous rate of progress and requested a change of experimentation,
direction and workflow. The old complete implementations are retired, with their notes intact,
at `bots/miscellaneous/timetable`, `compound` and `warband`. The active three paths were recreated
from frozen `anchors/main-v42@9fc5d7469074`; old private research remains parked and does not gate
new gameplay. This is a deliberate restart, not a claim that copying the baseline creates rivals.

| Active bot | Direction | First experiment |
| --- | --- | --- |
| timetable | Race: workers, territory, capture ownership and gem throughput | Three substantially different executed opening/economy packages |
| compound | Growth: composition, early purchases and useful carrying/returns | Three coherent draft/shop/bank packages |
| warband | Control: mobile pairs, concentrated squads and invasion | Three coordinated pressure packages |

Each owner does the heavy lifting. The coordinator keeps policies distinct and chooses the next
iteration from full-game evidence. Foundation work answers specific implementation questions;
it is no longer a prerequisite for trying a strategy. Preserve useful old findings without
continuing a failed project because its model or instrumentation is nearly finished.

The first tournament per bot is **96 full games**: baseline plus three packages, six maps
(`00ambush`, `00eight`, `00gem-fields`, `00lost-world`, `01big-world`, `01corridors`), both sides,
fixed v42 and starter opponents. Each arm has 24 keys. Trace is off. Report wins, paired margins,
starter completion and health immediately. These are training results, not final qualification.
Test actual packages early; investigate only promising results or conspicuous failures.

An improved or competitive head-to-head policy with a plausible strategic tradeoff can earn
all 40 pool maps/both sides against current main and a different strong opponent, then fresh
legacy maps/opponents if promising. Do not advance a severely worse policy merely because it
is the least bad member of a losing batch. Avoid
repeated tiny variants on the six training maps. A losing batch should produce one clear next
decision, not another infrastructure project. New behavior starts off and preserves old replay
behavior; exact module bytes are a convenient proof, not a separate research objective. Build,
fix obvious legality/fuel defects, play, compare, then explain. Health problems block adoption.

The first 288 training games are complete and technically clean. Control is 6/12 against v42.
Warband's carrier-pincer reaches 8/12, compound's factory ties 6/12, and all three timetable
packages finish 2/12. The subsequent 960-game transfer checks reject every selected package:
factory 27/80, workers 8/80 and carrier-pincer 36/80 against v42, each versus a 40/80 control.
A started workers pool run was allowed to finish once, not treated as a reason to continue it.
All packages win 12/12 against starter, but every package slows its completion; wins alone do
not establish better economic throughput. The
[training scoreboard](../../results/contenders/evidence/tournament-v42-training.json) retains
exact packages, paired results, completion and coverage. The separate
[pool scoreboard](../../results/contenders/evidence/tournament-v42-pool.json) retains opponent
health exceptions and paired sensitivity. Subject health is clean; no strength gain is claimed.
Next decisions: capture ownership alone for timetable, baseline-composition item tempo for
compound, and a direct capture/denial objective for warband. No legacy confirmation or repeated
small tuning of the rejected packages.

The next full-game iteration separates useful signals from another training mirage. Timetable's
bounded opening burst reaches only39/80 against v42 on the full pool; correcting its reward-release
lifecycle then reaches5/12 in training. Opening coordination is closed rather than tuned further.
Compound's adaptive shop reaches36/80 and exhausts fuel twice, so it is rejected; the unchanged
productive-item tempo arm then fails its broad check20/80 against v42 and44/80 against hunter7.
Earlier purchases alone are closed as a direction; this result does not identify whether item
order or extra returns caused the loss.
Warband's single change, retaining Ryth's farm area spell while an enemy is visible within6,
reaches46/80 against v42 (control40/80) and69/80 against hunter7 (control66/80), with clean subject
health. Unchanged fresh legacy confirmation is null:16/34 against v42 (control17/34),30/34
against hunter7 (control30/34). The combined62/114 against v42 remains uncertain and the policy
stays off. A small paired-game review checks what actually changed; it does not turn into a new
model project. Current working main still builds as0353a987e04f, identical to the frozen target.

The next decisions change actual duties or mechanics. Two exact timetable loss reproductions
showed severe gem collection deficits, rather than mostly lost bags:160 collected versus1013
on Stampede A,77 versus1097 on GoldRush B. A Marcella collector activates after its first Jungle
emblem, excludes pure-gold jobs and values gem supply; other bodies retain ordinary economy.
Its7/12 training result becomes42/80 against v42 and34/80 against v41 on the pool, versus40
and37 controls: another overall null, with no legacy followup. Compound's Tavi-third field
spending finishes starter games26.6 rounds faster, but transfers poorly against v42:29/80
versus40. It is rejected; the next executed conversion test services an own completed bank
before another camp, motivated by actual saved bank-to-purchase delays. Warband's bounded
allocator considers a ground cast's camp and enemy contribution together:10/12 training becomes
46/80 v42 and69/80 hunter7 on the pool, same win totals as the blanket hold with different flips.
Legacy gives15/34 v42 versus17, and31/34 hunter7 versus30. A separate v41 check is near neutral,
51/114 versus50. It remains unpromoted. Actual paired episodes show
both later enemy casts and lost farm damage; one equal-damage turn trades a reset/escape step
for a stun. This is an action-allocation tradeoff, not free added damage or proof of terminal wins.
Retain native replays/statistics in these new runs so
actual decisions can explain results. Earlier pilot keys are training data, never fresh samples.

The single-worker territory continuation also fails3/12 versus6; own-bank service ties6/12
and does not improve passive completion. Close those scaffolds instead of tuning their thresholds.
Timetable now replaces the primary collection/retarget lifecycle from turn0, using a small
gem-led sweep with persistent actual jobs and existing execution/safety. Compound builds a
Tavi-specific spell-poke/withdraw controller, testing real ranged cast/basic/reset movement
choices rather than another roster or item permutation. Warband's blanket retreat-walk rule
fails4/12 versus6; one final trial may reserve a concrete improving legal exit, then that branch
closes if it also fails. The first retreat refactor's24 matched OFF replay payloads are identical
despite different module bytes; no binary-order investigation is needed. No robust broad
strength improvement is established by the completed experiments.

The new primary gem-led loop also fails its first pilot:0/12 versus v42 and11/12 versus starter,
against6/12 and12/12 controls. At round400 it reduces collected gems8113 to5117 across24 keys
while removing zero-gem receipts. This disproves that executed package; several lifecycle
changes prevent attributing the loss to gold exclusion alone. No broad run follows. The final
concrete retreat-step trial fails5/12 versus6 and closes that branch. Compound's new controller
uses two actual Tavi pressure windows: walking out of vision followed by immediate farm return
re-exposes it to pursuers, and another retreat spends movement while leaving a compatible
reset/basic continuation unused. Its first new full-game trial is next; these observations
do not prove that an alternative action would have saved it. Warband now selects a primary
map-control/denial duty, rather than continuing spell-reservation or retreat wrappers.
Its chosen duty is a Heart altar warden: travel, hold/interfere with locks, then resupply.
The concrete Hooks A loss has all1012 enemy gems banked at one altar while purchased allied
locks remain unused; that establishes an opportunity, not that a warden can survive or win.
The failed collection loop's Corridors A trace instead shows17 camp deaths to two monoliths
by round400, with failed exits after contact. One conservative actual admission/re-entry
correction will compare against the old losing loop; improvement over0/12 is not the live bar.

The active goal remains **keep going until we beat the main bot**. Live is now v42,
`main/main@0353a987e04f`, raw/injected SHA256
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
Recheck `results/releases/LIVE.json` before qualification. No online actions are authorized.
Current code/run handles and decisions belong in each new bot's `NOTES.md`; immutable evidence
and old source identities remain in `results/contenders/`. The material below is historical.

## Archived research coordination (before the game-first reset)

The user renewed ownership of all three contenders and explicitly placed the shared foundation
at [bots/main/foundation](../main/foundation/README.md). That knowledge bank is the only owned
part of `bots/main/`; the main bot and its online tooling remain read-only. The older
`contenders/foundation/` material is retained with its original engine and evidence identity.
The current bundle is CLI/SDK 3.0.5, engine 1.0.3. Historical 1.0.2 results are leads, not
current health or strength evidence. This work began against the frozen live v40 anchor;
the live target has since advanced to v41. Preserve v40 experiment identities and include
v41/current main in eventual qualification.

One subagent owns each contender and its named scratch/cache paths. Separate owners curate the
foundation, investigate champion micro, and maintain the suite's evidence tools. The coordinator
owns this charter, integrates discoveries and guards distinctness. The first cycle established current
health and v40 baselines, then tested explained structural experiments per contender: scheduling
for timetable, reinvestment and productive time for compound, coordinated control for warband.
New strategic behavior stays default-off until measured; health defects are handled first.
Current state, evidence and restart commands live in each bot's existing `NOTES.md` and the
foundation/tool indexes. No online actions are authorized.

The active user goal is **keep going until we beat the main bot**. Health qualification is a
prerequisite, not completion. The next strength claim needs a current source with a verified
advantage over main across the pool, both sides, clean health and fresh confirmation; inspect
the current main and live anchor again at that point. Do not count repeated deterministic gates
as independent strength samples.

Second cycle: owners are addressing execution/model gaps rather than repeating the first nulls.
Timetable's draft tour/party/rate data was not consumed by gameplay, and its candidate filter's
full-health score was not an admissible bound for cleared rewards. Compound's current native
evidence includes a 168-owner-start camp hold with 27 fresh zero-timer respawn observations;
its modeled Heart dash also had no dispatched action. Warband's proximity response contains
three separate economy vetoes, and its selected-objective safety check does not protect transit.
These are model/architecture questions, not invitations to tune another radius.

Private `_warband-coalition`, `_compound-capital` and `_timetable-capacity` research may reuse the frozen v40 execution
closure; they do not replace the owned contenders by copying it. A cutover requires a distinct,
executed policy contribution, useful native evidence and broad strength/health assessment.
Preserve independent failure modes in their actual policies; source ancestry alone does not
establish independence. Retire the superseded complete bot into `miscellaneous/` when a better
direction is evidenced. Timetable's private rebuild now targets executed capacity and event jobs;
the original compound has tested its authoritative lifecycle repair. Canonical foundation research now supports actual arrival costs,
legal route costs and admissible dispatch bounds.

Second-cycle checkpoint: none of the tested mechanisms establishes a strength gain. Compound's
full 480-game lifecycle panel is paired +7/-7 with zero v40 wins; free-center casting on the
frozen-v40 micro fork improves the pool mirror 40/80 to 46/80 but fails fresh confirmation
(legacy v40 17/34 to 16/34). Warband's focus fallback returns to 40/80, paired +6/-6.
All remain experimental. Native evidence does establish earlier Vesper damage after a move/reset,
an additional Ryth victim in a normal planner cast, and independently verified productive priority
rotation by a melee pair. The latter uses four bounded 90-round games on both sides: equal-distance
melee basics and next-round priorities split 26 elk volleys evenly, both survive, and clear/capture/bank
finish at 56/62/68 versus 63/69/75 with one death under fixed priority. This is a conditional
mechanics result, not pool strength or full-game health qualification; see the
[native verification](../../results/contenders/foundation/pair-rotation-3.0.5/native-verification.json).
Mechanics witnesses and strength evidence are recorded separately.

Current implementation work follows those limits. Warband's conditional production-caller
party cycle is independently verified, but basic-only control loses time against ordinary
full-kit play; retain useful stationary casts before claiming a policy gain. Timetable is
binding shared assignments, actual visible-ally stats and sleeping-camp assembly to event jobs.
Shortened-game summary rows do not establish harvest counts; use native clear/capture events.
Its frozen v5 native trace now establishes an autonomous solo job through issue, attacks, payout
and freshly observed release. A later named two-body job verifies both actors' contributions,
helper release and a sole capturer. The first full-length panel is healthy but negative on two
maps/both sides: starter 4/4 in both arms with completion 232 rounds slower, v41 1/4 to 0/4.
Its actual Varg clear also exposes a static-density forecast that counts dead small clusters as
continuing splash targets. These are integration and model findings, not broad strength evidence.
Compound has a native
internal-appraisal counterexample: its shop values an affordable unlock from zero gem flow,
while its return helper prices that throughput advance at zero. A later task audit finds stale
or already-claimed portfolio capacity, so this does not establish an available investment trip.
A finite return, purchase and specified-work episode must charge gold work, scouting and teammate
displacement. Each owner's notes preserve failed arms and current source identities; no private
rebuild is adopted yet.

Micro U7 independently verifies shield-before-basic against Ren through the full v41 Brain in
four legally prepared native fixtures: the same kill and gold reward survive, and Varg lives
at 10 HP with 230 shield instead of dying to retaliation. The saved normal-game screen found
no matching occurrence; the guard remains off. See the
[verification](../../results/contenders/micro/shield/independent-verification.json) and canonical
[micro bank](../main/foundation/micro.md) for exact scope and failed controls.

The target refresh is read-only: `LIVE.json` names `main/main@db6ca9bc4e39` / `anchors/main-v41@6e7ef55c2795`,
raw/injected module SHA256 `97202ece0b7114a4fad49ec0831e89c90dbf92a0e85f3b6b2aa86ab2e65f43d0`.
V41 enables bounded planning, fresh unavailable-respawn handling and immediate-danger farming
(main D158-D172). No old v40 cohort is relabeled, and settled nulls are not rerun merely because
the target changed. The owners retain exact current source and restart state in their notes.

The resumed baseline against `anchors/main-v40@6f62c56b73f7` is complete: each row is all
40 pool maps, both sides, seed 0, engine 1.0.3. These are starting sources, not the experimental
arms being developed. All three are unqualified; health counts are own faults/exhaustions/errors/stalls.

| Starting source | Wins / games | Health f/x/e/s |
| --- | --- | --- |
| `timetable@3cfc5d884af9` | 0 / 80 | 0 / 0 / 10 / 0 |
| `compound@9da7bb86f3ab` | 0 / 80 | 0 / 21 / 2 / 0 |
| `warband@3290f993fd84` | 0 / 80 | 0 / 0 / 39 / 2 |

The [retained cohort](../../results/contenders/evidence/cohort-v40-baseline.json) separates
sources, engines and opponents; it does not turn historical v35 results into current evidence.
Current local work addresses action legality, bounded initialization, camp availability beliefs
and productive control. The foundation now distinguishes observed state from intended capture,
calibrates square-law claims to their tested contact assumptions, and corrects area-effect spacing.
Keep all three directions until a causal thesis fails; these losses alone do not justify retirement.

Historical checkpoint after the first cycle (these complete implementations are now retired):

| Bot | Enabled repairs and qualification | Next structural question |
| --- | --- | --- |
| [timetable](../miscellaneous/timetable/NOTES.md) | Legal-action guard enabled; 240 full health games clean against v40, starter and fundamentals, all 40 maps/both sides. Strategy flags off. | Does the dispatcher's supposed upper bound incorrectly prune cleared rewards? Prove the actual caller before changing allocation. |
| [compound](../miscellaneous/compound/NOTES.md) | Bounded initialization/turn work and tribute legality enabled; the same 240-key health coverage is clean. | Authoritative camp lifecycle, actual bank-route cost and dual-lock liveness; then productive reinvestment. |
| [warband](../miscellaneous/warband/NOTES.md) | Both new flags off. The reviewed guard removes 39 errors, but two v40 Lost World zero-clear stalls remain and block qualification. | Separate genuine contact from an indefinite economy veto, and make awake-camp route safety a constraint rather than a penalty. |

None has a detectable strength gain or a win against v40 in its 80 pool keys. Warband's guarded
packing arm adds only two starter wins (paired +3/-1, p=.625), a null result. Timetable's corrected
availability mostly exchanges waiting for travel; compound's repair removes fuel failures without
improving the race. These mechanisms and rejected strength claims remain in the bot notes.
The [current cohort](../../results/contenders/evidence/cohort-current.json) keeps evaluated sources
separate; newline-only final source changes are bound through exact raw/injected module identity.

Micro [U1-U4](../main/foundation/micro.md) now verifies lethal-action continuations and reset
allowances in native 1.0.3. Four bounded actual-warband-caller games also witness the extra basic;
normal planner applicability, complete production/capture/bank cycles and strength remain open.
Durable per-bot indexes under `results/contenders/` retain source, knob, bundle, module and
coverage identities; notes retain the next experiment. The historical checkpoint below is preserved
for context, not current qualification.

## The three contenders
Historical state (22 Oct, checkpoint before a planned system reboot: every contenders agent stopped at a safe
point with its work on disk and no jobs running):
- Foundation, first pass done: mechanics M1-M51, sdk S1-S24, maps P1-P12 (+ `data/maps.json`),
  economy E1-E28, combat C1-C21, online O1-O20, strategy G1-G10. The coordinated siege is now
  tested in the engine (E8/E9/E28, `_econ-siege`) and E22 corrected from C21. Not yet run: a
  monolith siege on a pool map (00lost_world g_491) and a last-tank-by-health siege policy
  (resume: `foundation/tools/economy/run_siege.py`, batches in `.cache/contenders/economy/`).
- Bots (milestones: M1 healthy complete bot, M2 the doctrine's core mechanism evidenced, M3 a
  diagnosis of where and why each loses, at model and structure level); each `<bot>/NOTES.md` holds
  its state, decisions and resume commands:
  - timetable (checkpointed): builds; vs the starter 75-5 on 40 maps x 2 sides, 0 faults, errors
    and stalls, 1 fuel-exhausted turn (01shortcut A, unexplained); economy weak: speedrun 1,689
    rounds with 34 of 80 games unfinished (main 994, schedule bound 674); M2 not started; decision
    D17 (stale-reward ageing) built but unmeasured.
  - compound (checkpointed): builds, unit tests pass; health clean on 160 games vs starter and
    fundamentals (79-1, 65-15; an older version: rerun first); speedrun 1,401 rounds with 15 of 80
    unfinished (main-v35 995); by r400 vs the starter 179 gems collected, 151 banked, 1,913 gold
    (main-v35 275 / 237 / 2,411). Built: a gold value model, committed tasks, awake bush-hidden
    camps on evidence (00lost_world 8 deaths -> 0), target-sized parties via the claim bytes (E8).
    Gap: too few elk and monolith groups, walking 37% of champion-turns; 01islands (dash pathing)
    and 01boss_battle (altar gate) never finish. M2 partial, M3 not started.
  - warband (checkpointed, `contenders/warband@3290f993fd84`): builds, unit tests pass; health on
    40 maps x 2 sides vs starter 65-15 and fundamentals 63-17, 0 faults, exhaustion and stalls, 5
    error lines from one bug (recall attempted while stunned: first fix). The pack is a flock around
    one shared anchor (members take clusters and single wolves within 6 tiles; the whole pack takes
    the leader's focus group: elk groups, golems, monoliths); economy weak: speedrun 1,765 rounds
    with 37 of 80 unfinished; the fight model is still a Lanchester ratio (no per-pair turn order,
    no C20 safeguards yet); no games against main, M2 and M3 not started.
- Suite tools (checkpointed; state, reference tables and resume commands in `tools/README.md`):
  `battery.py`, `fundamentals.py`, `distinct.py`, 21 tests passing. Reference main-v35 (1,760
  clean games): speedrun 995; worst opponents persona:hoarder 0.44, main-v36 0.48, persona:meta
  0.50; fundamentals locally 1.04 gold per alive round, 53 dead rounds by r400, 12% deaths per
  fight, against 0.89 / 121 / 26% online (v33-v36): local opposition kills far less than the online
  top. O1 kinds absent from the local set: early aggression, spawn camping, Prism battery, crystal
  routing, early siege (gem rush and invasion weak). The battery's `ARCHETYPES` hook waits for
  `_archetypes`.
- Archetype opponents `_archetypes/` (checkpointed): source is a byte copy of main-v35; nine
  personas designed in its NOTES.md, none implemented yet.
- Reading of the first builds: all three are far from the economic bar (speedruns 1,401-1,765
  against main's 995 and the schedule's 674), and none has met main yet. The foundation explains
  why at model level, and each bot's next steps already target it: party size by target (E8, G8),
  covered recalls (E4), the Jungle-first build (E20), banking on contact (C21), covered sieges (E9),
  then fight models with per-pair turn order (G9) and the safeguards (G10).
- To resume: read each NOTES.md; brief one agent per owned path again (the bot's directory, its
  `_<bot>-*` scratch bots and `.cache/contenders/<bot>/`; `tools/`; `_archetypes/`); rerun an
  interrupted arena run with the same command and `--out` (finished games are reused). Order of
  work: each bot's first fixes and economy (its NOTES "next steps"), then the archetype personas
  (battery's `ARCHETYPES` hook), then M3 diagnoses against main-v28, main-v35 and main-hunter7.

The routes are chosen from the evidence we already hold (main charter; `.cache/main/analysis/`
topteams, collapse, fights, economy): online, gold ahead at r400 wins 76% and 5+ deaths by r400
lose 95%; top teams collect ~70% more gems than main by r400 and win fights by focus fire; main's
speedrun (994 rounds to 1,000 gems against a passive opponent) sits ~25% above the estimated
bound; main farms solo and decides fights champion by champion. The three bots differ in how the
team is organised, what they convert first, and what they accept losing to:

| Bot | Idea | Organisation | Converts first | Accepted weakness |
| --- | --- | --- | --- | --- |
| `timetable` | The game is a race; win it by scheduling the economy near the map's capacity | Central plan computed in the draft (10x fuel, whole map), carried in draft memory, repaired locally | Gems (throughput) | Strong aggression: avoids fights rather than winning them |
| `warband` | Fights decide games; a team that is always together wins them and owns the rich ground | One cohesive pack (two on big maps if evidence says so), leader protocol, explicit group engagement model | Kills and map control | Out-raced on big maps when no fight happens |
| `compound` | Gold is capital: income reinvested at every spawn visit into the item that pays back fastest compounds into income and survival, and dead time is the leak to plug (the #1 online team shows the route; evidence, not a template). Revised 22 Oct: "Health binds farming and fights" was refuted (economy E20, combat C5); Health is one tool, the escape margin (C8) | Dispersed solo farmers, early and frequent shopping, contact discipline, convergence on isolated enemies | Gold into items | Early aggression before items; gem rushes |

All three originally started from the official starter and wrote their own code. The renewed
user instruction permits any starting point and rebuilding or retirement when understanding
outgrows a direction; each contender must still derive independent play. Each bot keeps its state, decisions and evidence in
`<bot>/NOTES.md`. Distinctness is checked in play once they run (styles, head-to-head, decisions
under matched conditions); a direction that converges on another is rebuilt.

## How the work is organised
- **You coordinate; subagents do the heavy lifting.** You set directions, keep the three bots
  apart, curate the foundation and judge the evidence. Subagents run the probes, experiments, game
  analyses, implementations, rebuilds, sweeps and gates. Run as many as are useful: the machine's
  slot pool is shared, and nobody reserves cores.
- **Brief them fresh, then let them work.** Each subagent gets the paths it owns (disjoint from
  every other running subagent), the question or goal, the evidence that would settle it and a stop
  condition (AGENTS.md, "Parallel work"). The method is theirs.
- **Everything learned comes back.** A subagent's result is a foundation entry or a bot change,
  with its evidence. You integrate it, resolve contradictions and keep the bank coherent.
- **Guard independence.** One mind tends to converge. Develop each bot from its own direction,
  check distinctness in play (`arena ladder styles`, head-to-head records, decisions under matched
  conditions), and don't let one bot's solution quietly become all three's.

## The real target: mastery of the game
The goal is understanding deep enough that strength follows from it, and survives the next patch.
Code is cheap in the agentic era; insight is not. So:
- **Explore.** Tear down and rebuild a bot whenever the suite understands the game better than
  its code does. Rewriting from scratch is a normal move, not a failure. Start from anything (the
  official starter, an anchor, `fundamentals`, a clean page), as long as each bot ends up as its
  own derivation.
- **Retire, don't discard.** When a bot's direction is superseded, retire it into
  [miscellaneous](../miscellaneous/README.md), where it stays as a semi-competitive opponent for
  everyone, then rebuild the slot along the new direction.
- **Build the foundation.** [bots/contenders/foundation/](foundation/README.md) is the shared bank of insight:
  what the game is, verified, from mechanics up to strategy. All three bots draw on the same bank
  and derive *different* play from it. That is how independent bots stay grounded without
  converging.
- **Build upward.** The useful picture is layers: what the engine does (mechanics); small facts and
  tools that rest on it; models that predict; decisions that choose by those models; and last, the
  few numbers that get tuned. Lower layers outlive patches; higher ones are rederived. How to
  state, check and connect what is learned is yours to work out, and the bank should improve as
  you learn how knowledge accumulates best here.
- **Prefer understanding to results.** A win nobody can explain is a lead. A mechanism you
  understand is an asset, even before it wins.

## Domination, not adjustment
Strength means doing the fundamental things better than any opponent, whatever it does: having
an answer, solutions and safeguards, not adjusting to the opponents we happen to meet. Fixes
keyed to the games we lost go only so far; when a new counter-strategy appears, a bot built from
them drops (user direction, 22 Oct).
- **Learn from losses at depth.** A lost game is a question about our understanding, not a task
  list. Go past the event chain ("this event caused that event") to the level where the answer is
  general: which model was wrong or missing, which structural property of the game the loss
  reveals, and why our design let that situation arise at all. Example: "Ryth died retreating
  into a pocket" (event) <- "the retreat picked the nearest tile out of reach" (proximate) <-
  "we model escape as possible, but at equal speed a retreat never opens distance" (model) <-
  "contact is sticky in this game, so the real decision is made before contact: where we farm
  relative to enemies and allies, what we can see, how many we are" (structure). Answer at that
  level, then check the answer against the whole family of situations it covers, not only the
  game that revealed it.
- **Answers, not reactions.** Each bot's notes state its answer to every kind of opponent
  behaviour it can meet (early aggression, grouped or dispersed play, gem rushes, scaling, spawn
  camping, lock harassment, chases, kiting, bush ambushes) and why the answer holds, derived from
  the foundation's models, so that it holds against opponents we have not seen.
- **Safeguards.** Invariants that bound the downside whatever the opponent does (for instance: no
  engagement the calibrated model says we lose, no contact without a known way out, no awake camp
  left out of a fight's accounting, clean health under the fuel limit), each unit-tested and
  witnessed in games.
- **Stable footing.** A doctrine changes when understanding changes (a foundation entry, a
  verified mechanism), never after a bad run. Tuned numbers are the last and smallest layer: few
  knobs at broad optima, never re-tuned on noise or to particular opponents or maps. Rejected ideas
  stay recorded, with why, so they are not retried on a whim.
- **Measure in that order:** opponent-independent measures first (the speedrun, survival and
  fight-model calibration, health), then the worst case over a broad set of strategies, then
  averages. A gain against today's opponents that costs the worst case is a loss.

## Evidence without an online quota
`main` uses the whole unranked quota, so the contenders have no online games. Local evidence must
carry the weight; it is plentiful (local games are unlimited) and deterministic per (bots, map,
side).
- **Swap-readiness, the ruler.** The main anchors have known online results (main charter, "The
  main bot"). Matching the live version locally, across the same opponents, is our best proxy for
  holding its rank: `arena gate contenders/<bot> --ref <live anchor>` (default bar 0.5), plus
  results against the older anchors, main's archetype personas (`bots/main/tools/gauntlet.py
  --bot ...`; the personas are `main/main` knob sets), the targeting bots when they exist, the
  other suites on the ladder (`arena ladder`), and the economy speedrun
  (`bots/main/tools/speedrun.py`). Clean health always. Decide which measures are trustworthy and
  say why.
- **Know the limits.** Local strength is not online strength, and opponents derived from one
  codebase mislead (workflow "Monoculture"). The online data `main` has already collected
  (`results/online/`, the main charter, `.cache/main/analysis/`) is there to read; ask the main
  agent through your notes if you need something it can fetch.
- **Paired, broad and confirmed**, as the workflow explains: identical game keys, both sides, all
  pool maps, confirmation on fresh maps or opponents, coverage stated with every claim.

## Boundaries
- Everything under `bots/contenders/` is yours, this charter included: keep its state current, but
  change its purpose or boundaries only with the user. You may add retired bots to
  `bots/miscellaneous/` (directory and charter row). Shared knowledge belongs in this suite's
  `foundation/`; do not inspect main or main-anchor source or edit `bots/main/`, the anchors,
  root `tools/` or `docs/`; write requests in your notes instead.
- No online actions of any kind, for you or your subagents. No shared code between any two bots,
  the three contenders included; ideas and knowledge travel freely, code doesn't.
- A contender replaces `main` only by the user's decision, or through a release by the main agent.
- Keep each bot's state, direction and decisions in a notes file beside it. No per-task documents
  and no versioned copies of a bot.

## Requests to the main agent
Read-only fetches the contenders cannot make (no online access); ids and details in
`foundation/data/online/fetch_requests.txt`, results welcome anywhere under `results/online/`:
1. Ranked games between top-20 teams after the Sprint 2 lock (from 25 Oct), summaries and
   replays: the first look at bots teams may be hiding (foundation O19).
2. The last 100 ranked games of prism best girl (team 38e6d8c4-1608-44f5-956b-08390f564336, #2 on
   22 Oct): the gem-carrying Prism is a distinct threat every bot must answer (O17).
3. A second global window (all ranked games of 10 minutes) with a ladder snapshot, at another hour
   and after the sprint; and a ladder snapshot with every survey fetch.
4. Replays for the 156 summarised games between other teams listed in that file.

Findings main may want (foundation, engine 1.0.2, verified by probes):
- `mechanics.md` M20: both teams can lock the same altar, and an enemy lock cancels our tribute
  channel even with our own lock on it; D76's lock mode 2 assumes our lock protects the tribute.
- M14: a capture pays at the start of round k+6 after stepping on (one arming phase plus five).
- M25: "excludes walking" blocks only the step; a standing ranged caster casts and basic-attacks in
  the same turn.
- `sdk.md` S21-S22: a formatted log line costs 3-8K fuel (rules-notes says ~1K), an exception 2-8K.
- `economy.md` E15 (model + main's measured speedrun): most of main's 320-round gap to the best
  feasible schedule (994 vs 674 rounds, passive opponent) is structural: its target rule rates every
  group for one champion plus at most one joiner, while 76% of the pool's gems sit in elk and
  monolith groups whose best party is 2-5 (E8, maps P3). Second order: recalls at 50% health, a
  late and camp-suboptimal first emblem (E20: Jungle first), small bags on travel maps (E22).
- `combat.md` C2, C19 (engine-verified): tempo is per pair of champions (same-slot pairs trade
  double turns, different slots strictly alternate, lower slot first); a model that resolves rounds
  as simultaneous volleys cannot see first strikes, catches or escapes, the structural reason the
  fight report measured fights ~3x slower than predicted and deaths predicted 3.6x too often. C8: at
  equal speed only a dash against a dash-less chaser breaks contact; a retreat survives when health
  exceeds the chasers' summed rate times the rounds to safety. C21: online, the death hazard is a
  function of contact, health and allies, not of the bag.

## Requests to the integrator

The 24 October contained cycles encountered freshly built snapshots whose injected artifact
was trimmed before capture, including during a roughly ten-minute shared-slot wait. Exact RAW
copies and explicit `Binding` payloads let the native questions proceed without role-equivalence
claims. Please expose supported snapshot-to-binding capture for this suite with the selected
payload protected at readiness; absence of an unused role should not block an explicitly RAW-only
experiment. No root tooling was changed. The bot notes and compact assessments record the
pre-native failures separately from games; the external slot holder was left intact.

The runner provenance audit found that `tools/arena/games.py` can create a result row from
native statistics without retaining CLI exit status, and uses plain process wait without an
independent descendant-cleanup receipt. Its cached entrant directories also let CLI
`--release` recheck source builds; optional local warmup is not an opaque-byte binding.
Please add explicit execution/cleanup status and a contained precompiled path to the shared
runner when convenient. The contenders use existing `compiled_runner.Binding/_run` for new
native execution and did not edit integrator tooling. Exact historical driver/tool evidence:
[provenance audit](../../results/contenders/native-evidence/mosaic-lean-r2-provenance.json).
Native game outcomes and execution/provenance qualifications remain distinct; old keys are
not rerun solely to manufacture new receipts.

The active contender selectors are now `contenders/timetable`, `contenders/mosaic` and
`contenders/warband` (`arena bots contenders` verifies exactly three). The complete rejected
Compound checkpoint is at `miscellaneous/compound-field-growth`. Update the integrator-owned
`bots/README.md` roster when convenient; no index or tooling outside this ownership was edited.

From the mechanics work (22 Oct):
- `arena play --maps FILE` fails for a map file outside `maps/`: the path becomes a name that is not
  found again (workaround: `foundation/tools/mechanics/run_probe.py`).
- `arena new bot` rejects names starting with `_`, although `_` marks scratch bots; probes were made
  by hand.
- `docs/rules-notes.md` corrections: logging cost (3-8K a line), capture is an arming phase plus
  five, two locks can share an altar, distance-field cost is (16 + expansions) x ceil(W/8) x
  ceil(H/8) for a W x H map.

## Calendar
Sprint 2: 25 Oct (lock 24 Oct, 23:59 UK). Sprint 3: 1 Nov. International qualifiers: 4 Nov.
UK qualifiers: 11 Nov. Finals: 18 Nov. The contenders' value peaks at the qualifiers and finals,
after the remaining patches, so the foundation comes first. Be swap-ready early enough that
switching is a real option.

## Layout and checks
- `bots/contenders/<bot>/src/main.cpp` is a bot (id `contenders/<bot>`); its notes, tools and unit
  tests (`tests/*.cpp`, compiled against its own `src/`, found by `tests/run.py`) live beside it.
- `foundation/` holds knowledge, never code a bot includes.
- `tools/` (in progress): the suite's common measuring standard. `battery.py BOT` gives health,
  speedrun, the worst case over a pinned strategy set, and fundamentals measured with the online
  definitions (`fundamentals.py`); `distinct.py` fingerprints the contenders and main.
- `arena build contenders`, `arena test contenders` (or `contenders/<bot>`); measure with
  `arena play`, `sweep`, `gate` and `ladder` ([workflow](../../docs/workflow.md)).
