# Suites

Each suite explores one axis of the game, stated as a question in its charter
(`<suite>/README.md`). The axes keep suites different from each other, so the repo holds a diverse
local population: opponents that test our submission from many directions and stop every bot from
converging on one style. How to work and run experiments: [workflow](../docs/workflow.md).

## Roles
- **main** - the submission, one agent ([charter](main/README.md)). Three parallel tracks: short
  term, optimise the current bot's parameters; medium term, new decision structure on the same
  chassis; long term, new strategies and models from engine simulation (RL, genetic algorithms,
  SPSA and other optimisers).
- **contenders** - rival full bots for the submission ([charter](contenders/README.md)). A
  contender replaces main when it beats the live version on held-out maps and the gauntlet with
  clean health and passes the CLI gate.
- **The extended suites** (all others below except anchors and surprise) - the test population.
  Each bot follows its suite's doctrine; it earns its place by being healthy and playing in a way
  no other bot covers. The 24 October user revision adds a minimum: at least one actual win
  against current Main on all 40 pool maps, both sides (80 games per bot). Draws do not count.
  More wins and Elo do not justify converging on one strategy.
- **anchors** - frozen references (new ones only through `arena release`). **surprise** - a
  separate research track with private (underscored) bots.

## Current population accounting

The vendor's 25 October engine 2.0.0/70-map publication is a new pending epoch
([reconciliation](../docs/reference/updates/2026-10-25-engine-2.0.0/reconciliation.json)).
All measured panels and named qualified sets below retain their explicit 1.0.3/40-map scope;
none qualifies the new vendor epoch. Development launches stopped for bundle reconciliation.
An installed 70-map pool will require 140 unique complete keys per bot, with the same strict
win, health and contribution requirements. Production is still pinned to the older coherent bundle.

The 25 October filesystem inventory contains **194 extended-suite bot entry points**: adaptive 16,
commitments 16, decision 30, distributed 17, doctrines 6, events 16, frontier 16, heuristics 16,
miscellaneous 12, models 17, specialists 16 and territory 16. This counts non-underscored bot
directories with `src/main.cpp`, excluding main, anchors, surprise and contenders. Every one of
the historical census's 192 bot paths remains present. Two later retired contenders add paths:
[Mosaic ordered words R20](miscellaneous/mosaic-ordered-words-r20-20261025/NOTES.md) and
[Timetable calendar frontier](miscellaneous/timetable-calendar-frontier-20261025/NOTES.md).
Their [suite accounts](miscellaneous/README.md) retain their failed continuation or reachability
evidence; neither has a complete current-Main panel or qualified contribution here.

The [192-bot census](../results/regression/current.json) remains historical Main52 evidence.
The [current strength record](../results/regression/strength-current.json) contains eight complete
Main53 default panels, with Warband alone qualified; seven have zero wins. The other 186 current
filesystem bots have no current-default panel in that record. Configured variants do not fill
this gap. Each historical account still names its contribution or unresolved witness requirement;
its health and source epoch must be checked before reuse. The two archive additions need their
own accounts in a future census rather than inheriting their successors' results. The separate
Contenders pause and ownership remain governed by its charter; this inventory does not resume it.

## The gauntlet
Named opponent sets in [gauntlet.json](gauntlet.json), used as `arena exp run ... --vs NAME`:
- `gauntlet` (strength): `anchors/main-v53`, `anchors/main-hunter7`, `miscellaneous/compound`,
  `anchors/fundamentals`.
- `gauntlet-wide` (robustness): the above plus healthy bots of distinct style -
  `heuristics/satisficer`, `heuristics/reset-pursuit`, `heuristics/learned-circuit`,
  `heuristics/twin-interlock` (dies often) - and the starter.
- `coverage-candidates`: `distributed/firebrigade`. Its healthy paired control games show an
  observable contrast, but do not establish native alarm/intervention causality. Preserve the
  commitment while resolving that gap ([evidence](../results/population/firebrigade-current/index.json)).
- `passive`: `anchors/starter-cpp` (economy speedruns).
- `regression-qualified-v53`: `miscellaneous/warband`. Its current default wins1/80 against
  released Main53 with clean recorded health on both teams and a native six-member heavy-clear,
  capture, carry, bank and continuation witness. The default executable exactly matches the
  tested repair. [Evidence and limits](../results/population/warband-current-strength/index.json).
- `regression-qualified-v52` (historical): `heuristics/satisficer`, `heuristics/renewal-regret`.
  Each default had one strict win in its complete 80-game Main v52 test,
  clean recorded health on both teams and native evidence for distinct controller bundles.
  Satisficer's executable is unchanged; Renewal Regret now funds its first health restoration
  stage earlier and returns to paid work. Their
  [Satisficer](../results/population/satisficer-current-strength/index.json) and
  [Renewal Regret](../results/population/renewal-current-strength/index.json) records bind the
  exact modules, scope and costs. Main has advanced to v53, so these results do not certify
  current qualification. Exact complete Satisficer and Renewal Regret panels against v53 are
  healthy but each has 0/80 wins. They remain investigation candidates.

The gauntlet panels preserve existing comparisons while qualification is repaired; inclusion
there does not certify the new floor. Qualified members need clean measured health, distinct witnessed
coverage and the required current-Main win. Wins alone do not qualify them. The
census screens health and aggregate style, while named interactions need causal witnesses.
Refresh locally with
`arena exp run census/NAME -q ... --subjects all,-main,-anchors,-surprise,-contenders --vs anchors/main-v53,anchors/fundamentals --keep none`, close it, then
`arena exp audit census/NAME --win-floor-vs anchors/main-v53 --out results/regression/current.json`. Contenders has a separate owner.
Its `contenders/mosaic`, `contenders/timetable` and `contenders/warband` remain with that active
owner; their current contribution and strength are outside this panel's evidence. Historical
census results do not certify their changing sources.

**Earlier health census, 24 Oct 08:10 UTC**: 192 extended bots, 30,720 games, all 40 pool maps,
both sides against frozen v44 and Fundamentals (`census/c2`, closed). Its spec, rows and paired
classifier-impact summary preserve this historical measurement. The
[current accounts](../results/regression/current.json) now use the later complete v52 floor test.
Working-source freshness is recalculated by `exp audit`; later strength prototypes introduce
new source epochs, so their OFF continuity and new variant results live in the corresponding
bot records rather than being silently inherited from an older reference panel.

- **Measured health:** 31 bots have no recorded flags: heuristics 16/16, miscellaneous 6/10,
  distributed 6/17, doctrines 2/6 and specialists 1/16. The other 161 need health work. Own-team
  flags occur in 5,052 games: 5,008 include fuel exhaustion and 376 include error lines. Four
  additional games flag only the opponent. Stalls are not measured by these rows.
- **Measurement correction:** the old runner counted normal Decision telemetry such as
  `abs_duration_error` as exceptions. The corrected classifier matches the CLI. Across the same
  30,720 keys, 4,414 health rows change and every non-error metric is identical; error-flagged
  subject games fall from 4,790 to 376. The old c1 error counts are superseded, not bot repairs.
  [Paired evidence](../results/regression/classifier-impact.json) and
  [82-game CLI parity](../results/regression/runner-validation.json) preserve the scope.
- **Contribution:** every account names its charter, adapter availability and unresolved native
  witness requirement. The census itself admits no mechanism. It provides current health/style
  screening; existing bot records retain their more specific evidence. There is no heldout or
  strength-improvement claim here.
- **Observed row twins:** four Miscellaneous field-growth/gem-sweep/residual-service/local-party
  bots match on every recorded key; three guarded-perch/reactive-service/reactive-production bots
  form another group. Inspect the account's exact labels before assigning redundant coverage.
  The old Decision changepoint/commitment and citadel/hindsight duplicate claims are withdrawn:
  matching outcomes hid differences in health and resource metrics. Rows alone do not prove
  identical choices.

**Integrator decisions.** The observation of ordinary telemetry behind thousands of health flags
changed work selection: verify measurement parity before changing 30 independent Decision rules.
That bounded change removed false flags without changing gameplay. Current evidence still points
to fuel safety as the main health obstruction; it does not identify a shared strategic defect.
Firebrigade's healthy active/control contrast also failed to isolate its claimed rescue mechanism,
so it moved to `coverage-candidates` without a policy rewrite. A personally visible alarm followed
by actual bounded intervention and productive continuation, distinguished from a suitable control
on a fresh applicable condition, would justify reconsidering that decision. Pool screening is an
explicit finite assumption, not a guarantee on unseen maps. The current manifest protects its
snapshots during pruning; cleanup reduced the shared arena cache from about 24 GB to under 9 GB
while preserving recorded inputs.

**Current strength floor.** Every retained bot must win at least once in the complete
40-pool-map, both-side test (80 games), while keeping its own stratagems and fighting decisions.
This supersedes the earlier strength-is-never-an-entry-test default. The
[current strength evidence](../results/regression/strength-current.json) pins the released
Main reference, sources, actual executable bytes, wins, draws and both teams' health.
Released Main is now v53 (`anchors/main-v53@fa5b8ae8b5ae`), byte-verified against the released
source snapshot. Satisficer, Renewal Regret, Compound, Twin Interlock, Altar Options, Splash
Harvester and Witnessed Handoff each fail its win floor at 0/80, with both teams' recorded health
clean. Together with Warband these are eight complete current default panels, not a new
population census. The changing working Main head is a
separate epoch and does not inherit these results.

The last complete population census is historical v52: **192 defaults, 15,360 games, seven floor passes and 185
blocked**. Satisficer and the promoted Renewal Regret each win 1/80. Compound wins 2/80,
but its contribution remains unverified here. Four historical Main-derived row twins win
31/80 each; they do not supply four independent contributions. Timetable and Warband each
have one real win but fail whole-panel health. Staged Venture's only engine win is an unhealthy
all-zero Team B default. Thus 182 bots have zero engine wins and 183 have zero nondefault wins.
Forty captured panels are health-clean; 2,477 games flag the subject and none flags Main.
The audit separately records later source epochs and verified byte-equivalent defaults.

Satisficer's unchanged executable retains its witnessed joint aspiration/progress-lease
contribution. Renewal Regret now funds its first health restoration stage when affordable,
then resumes paid work; its earlier return has costs as well as gains. These own contributions
and passing v52 floors justify the historical named set above. Pairwise native neighbor distinction
remains unresolved; the records make no claim that every heuristic is independently isolated.
Evidence of coincident productive choices would change the coverage credit even after a later
passing floor. Other unusual bots remain investigation candidates, with their gaps preserved.

Warband's promoted combined legality/anchor-follow repair wins 1/80 against v53 with no recorded
health flags on either team. Default `a397f2074dca` exactly matches tested `748c5ec54179`, RAW
`ed51344052537304f50a71a22ffcc06ac56516ca8c8d407b6de8433ccc962365`.
A licensed journal reproduces the old complete public replay and identifies a contradictory
handoff: unchanged FIGHT orders switch between an economic group and the leader anchor at
distances nine/eight. The repair preserves other modes and fighting choices. Stationary teammates
perform deliberate spawn-edge PvP, so low income or coordinates alone did not establish a stall.
Historical shared-memory reads failed source invariants; those reset-looking bytes were not used
as bot-state evidence. In the new fixed winner all six actors clear a heavy Elk group; Isaac
captures 88 gems/440 gold, carries them alive, banks the88 and resumes movement. The
[current-module record](../results/population/warband-current-strength/index.json) binds this
word and its limits. The journal remains OFF. This is the first qualified v53 member;
other development bots retain their explicitly unresolved contribution/strength gaps.

**Work selection.** The historical v51 census had 188 zero-win defaults and only the four
Main-derived twins passed. Satisficer's existing win against v52 changed the allocation:
refresh the reference panel before treating old zero-win labels as current defects. Those v51
facts remain explicitly historical in the strength index. A partial screen, draw-derived score
or unhealthy default victory never substitutes for the requested full test.

Several verified components finished without qualifying their bots. Reset's bank-returnability
variant executes bank -> reset/retarget -> next payout but still wins 0/80. Circuit's renewal
and retained-permission variants pay and bank, yet the v52 retained-permission panel is 0/80;
joining later work does not increase third-source captures. Hoard's matched-code area-casting
control proves useful casting and paid continuation, but every tested v52 profile is 0/80 and
marginal capital repayment remains unresolved. Distributed repairs restore real gold work,
gem observations and a 79-round earlier personal payout; a safe complete gem cycle is still
unrepresented. These prototypes stay OFF; further phase, margin and threshold tuning stops.
Timetable's bounded scoring repair also preserves its historical v52 win and restores an owned
capture/bank cycle, but its complete v53 panel is a healthy 0/80. It stays OFF; the
[finished component record](../results/population/timetable-current-strength/index.json) retains
the production fixture, native receipts and attribution limits.

Tempo's v52 diagnostic loses 83 carried gems across four deaths. Both nearby altars in its
largest loss have five personally visible lock hits, ruling out the apparent free alternate.
A bounded exit preserves cargo, restores health and banks 22 after renewed work. The revised
selected-source and action-epoch guard reproduces that entire public replay, but both it and its
coherent control finish the full v53 panel at 0/80 with clean recorded health. The prototype
remains OFF. Surveyor's experimental early-detail path also changes information-clock ordering
and cannot serve as a preserved control. The suite's current default epochs are checked
separately from these experimental recipes.

The operating environment now checkpoints completed chunks, resumes only missing keys from
exact cached inputs, and contains native worker trees before releasing their slots. The v52
census recovered 6,109 rows after its transport watchdog and replayed the 9,251 missing keys.
A separate cache investigation found duplicated forwarded inspector streams; successful
transport now removes those staging copies. These process changes preserve the evidence bar
and the same shared machine pool.

The 25 October 18:38 UTC check found `results/` at 3.74 GB, above its 3 GB target, and the shared
Arena cache at 10,423,768,945 bytes, above its 10 GB limit. Slimming freed 0.17 GB; pruning refused
an external visualiser asset reference without removing it. After a scoped reader correction,
the canonical retry freed 49.1 MiB while preserving 1,260 pinned snapshots. Shared snapshot growth remains paused.
Owner-coordinated compaction must keep selected claims resolvable and reconcile older missing
catalog references; neither target changes. Separately bounded owner-local work may continue.

The [unchanged Splash and Handoff panels](../results/population/strategic-shortlist-current-strength/index.json)
test two productive, distinct bundles without new builds: clipped multi-target combat and funded,
physically witnessed division of work. Their best current losses score 511–1002 and 572–1028,
respectively. This weakens the expectation from their closer historical losses; neither a small
threshold adjustment nor current qualification follows. Inspect current production and conversion
before selecting a repair.
The [current Handoff native case](../results/population/handoff-current-native/index.json) records
50 last-hit clears against Main's 49, but A collects 583 gems against 1,028. Its paid-source mix
has lower rewards; last-hit counts alone do not establish capture ownership. One plausible
handoff instead pays both occupants, while affordable outside work and private admission remain
unknown. This selects source economics and opportunity prerequisites for investigation; it does
not establish failed delivery or a benefit over solo work. The diagnostic case is closed.

Territory's post-heal episodes deal real monster damage that survives across visits; neither
team captures those central groups. A bounded refusal of uncertified partial work then produces
no gold or gems on either Big World side, removes all monster damage and raises A's exhaustion
from one to six. The actors discover the cheap alternate but do not complete its approach and
payout. This rejects the refusal as a repair; it stays OFF. Quote completion, selection and
execution remain separate unknowns. Preserve geographic commitments and staged work while
resolving that contract. The [closed ablation](../results/population/territory-admission/ablation.json)
records all sixteen OFF executable checks and the native attribution limits.

Decision's historical panel also contains substantial camp damage, income and banking despite
zero wins. Its next discriminating case is an already-selected Yellow Brick Road job that fails
continuation; Corridors' preselection cutoffs are a separate obstruction. The earlier private
phase packet changed gameplay and remains unlicensed. A later bounded continuation journal
reproduces complete native public play and statistics on both sides, separately verified for RAW
and SDK-injected roles. Its R134 frame reaches a fresh target and then reports movement's fuel
cutoff; R141/143 stop before execution. These are measured frame values, not proof of the original
private fuel. All thirty default RAW executables remain unchanged. The
[closed role-bound case](../results/population/decision-job-continuation/index.json) selected a
bounded pre-action cost discriminator. Five qualifying targets cause five fresh self-position
queries, but the four removable queries' observed local envelope is only 436 fuel against an
actual 971-fuel movement shortfall. The [closed cost rejection](../results/population/decision-work-cost/index.json)
stops that narrow cache proposal. A broader owning-state cache then leaves 28,487 fuel at the
same 30,000 movement fence, versus the original 29,029; its safety checks erase the savings.
That [negative trial](../results/population/decision-state-cache/index.json) is restored and closed.

Profiling the larger producer changed the allocation: active-group observation spends over
53k fuel repeatedly joining immutable identities while also making necessary fresh reads.
The default-OFF [identity index](../results/population/decision-identity-join/index.json) saves
25,495 fuel in the measured Tavi turn, preserves live-query order and funds an actual move,
reset and attack under the unchanged movement reserve. That component's thirty-default proof
preserves both RAW and SDK bytes. Its required suite drivers complete without new failures; ten pre-existing KNOWN
parts remain unresolved. The measured game still has no clear or income and retains a stall,
so no default promotion or Main-floor claim follows. A later personally scoped Varg card shows
available actions, a visible living target and legal improving steps during another quiet job.
The [matching callback trace](../results/population/decision-continuation-next/index.json) then
measures 40,218 fuel spent in active observation and only 21,679 at the unchanged 30,000 execution
guard in the C++ observer. Public native input is bound; native private fuel is not. A later paired
check finds sparse native-versus-observer fuel differences, so cost and branch findings retain
that runtime scope. Optional review is skipped in the observed callback. Searches over its 46 visible camps
select a bounded lookup experiment; neither a review-clock change nor lower reserves follows.
The [closed visible-lookup component](../results/population/decision-visible-join/index.json)
then raises native B production from zero to 135 banked gems and 3,235 gold, including a witnessed
clear, capture, carry, bank and later clear. It remains OFF: one late fuel exhaustion and detailed
native stalls block adoption. The new thirty-default matrix is not run. The failing actor carries
eight gems, rejecting a zero-bag shortcut. The [fresh-position reuse trial](../results/population/decision-bank-proxy/index.json) still exhausts,
has lower measured production and is backed out; runtime sources return to their exact preimage.
Models likewise needs caller evidence: Covenant's law
changes ranking in its narrow scripted positive control, but native delivery is unproved. Kernel's
adapter is wired and deliberately lacks a production fit table; that unimplemented data component
does not establish a missing dispatcher in other models. Do not infer blanket inactivity or tune
their distinct decision rules/forecasts from these aggregate losses.

Altar Options' unchanged default completes the current v53 floor at 0/80 with both teams'
recorded health clean. Its prior lock-removal and diversion witnesses remain distinct
contributions. Its closest current loss is Lanes B, 627–1024 with six deaths; the historical
v52 margin did not carry forward. Exact personal camp and movement cards explain the selected
late refusal: even optimistic next-phase camp damage exceeds the actor's health. An earlier
safe two-step retreat does not establish a complete visible route to recovery. That negative
result stops the proposed escape repair and shopping-batch tuning; it does not establish that
every possible escape fails. The [closed case](../results/population/altar-current-strength/index.json)
records the source inputs and counterfactual limits.

Models' bounded host probe verifies a real saving in the existing FreshDynamicGrid trial:
one measured preparation interval falls from 10,500 guest fuel to 646/679. Incoming goals and
dynamic blockers are stable across the two selected rounds. The exact kernel progresses under
assumed logical allowances, so neither intrinsic kernel failure nor those public inputs explains
the native inactivity. Native private allowance, persistent frontier and reset state remain
unknown. The [finished measurement](../results/population/models-work-cost/index.json) rules out
repeating the dense-representation trial or lowering its work reserve. Further work must
distinguish those remaining causes. Fireflies' existing small-bag banking repair separately
finishes the exact current-Main panel at 0/80, with both teams' recorded health clean. Its
[closed variant screen](../results/population/fireflies-current-strength/index.json) stops this
branch without threshold tuning or default promotion; the default was not measured by that panel.

## Suites
| Suite | Doctrine (the question it answers) | Shared code |
| --- | --- | --- |
| [main](main/README.md) | The submission: the **main** bot (v44 chassis, live), one dedicated agent | none |
| [contenders](contenders/README.md) | Separately owned rivals for the submission, each derived independently from the suite's game foundation. Current assessments and directions live in its charter | none (self-contained bots) |
| [anchors](anchors/README.md) | Frozen references: official starter, fundamentals, live and past main versions, hunter proxies | none (never edited) |
| [specialists](specialists/README.md) | One first-principles commitment per bot (time and flow, combat thresholds, space and information, coupling between actors) added to the unchanged B0 economy | none (self-contained forks) |
| [decision](decision/README.md) | Which decision rule over complete jobs (clear, capture, carry, bank, renew) plays best when the model, draft, purchases and executor are held fixed | common + per-family modules |
| [models](models/README.md) | Which predictive model of the game's dynamics gives better forecasts and, through one fixed ranking and executor, better decisions | common, kernel |
| [doctrines](doctrines/README.md) | Economic organizations: one doctrine per bot for turning production into permanent score | common, reference runtime |
| [distributed](distributed/README.md) | Cooperation among champions that see only their own vision, share no memory and send no messages | common, reference |
| [territory](territory/README.md) | Which geographic organization of complete source -> capture -> exit -> altar -> recovery cycles wins while the economy is held fixed | common |
| [adaptive](adaptive/README.md) | Who may change a commitment when models can be wrong, gated on each actor's own completed episodes | common, distributed-common (own copy) |
| [commitments](commitments/README.md) | Commitments of our own physical state (carried or banked gems, gear, altar locks, channels) that leave the rival only worse responses | common |
| [frontier](frontier/README.md) | Complete organizations, each built on a different formal representation from operations research, scheduling, control or game theory | common |
| [heuristics](heuristics/README.md) | Which combinations of cheap heuristics make useful decisions without exact search, complete information or calibrated forecasts | common |
| [events](events/README.md) | Event-time economics of one champion's pipeline: each choice valued by what it pays before the next decision event | common, tempo |
| [miscellaneous](miscellaneous/README.md) | Retired contenders: complete bots from earlier directions, kept as fixed opponents | none (self-contained bots) |

## Rules that keep suites apart
1. **One axis per suite.** A change belongs to the suite whose question it serves. A new idea
   that fits no axis starts a new suite (`arena new suite`), with its question stated first.
2. **Borrow ideas, not code.** Suites never include each other. An idea seen elsewhere may be
   re-implemented inside your own doctrine; copied code becomes your suite's code, and you own it.
3. **Distinctness is shown in games.** The census style columns and `arena ladder styles` find
   pairs that play alike; if two bots make the same choices, redesign or retire one (removed
   content goes to `D:\files\battlecode-removed`). A useful unusual commitment with no win stays
   an investigation candidate while its obstruction is repaired; it is not certified regression
   coverage. Poor health needs fixing before qualification.
4. **One-win floor, distinct play.** Require at least one strict win in the complete current-Main
   pool test; preserve each bot's stratagem and fighting decisions instead of adopting another
   suite's winner wholesale. Health, mechanism evidence and the win floor are separate checks.
5. **Anchors stay frozen**, so ratings remain comparable over time.
