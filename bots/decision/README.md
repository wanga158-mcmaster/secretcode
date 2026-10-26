# decision: one governing decision rule per bot over complete jobs

## Doctrine

**Question.** Which representation of value, uncertainty and cooperation, used as the
*decision rule* over complete jobs, plays strongest across opponents? A job is a semi-Markov
excursion (travel, clear, capture, carry, bank or renew) valued at its endpoint. **Shared
hypothesis:** with the forecast, menu and executor held fixed, objectives that disagree on
which complete job to take (rate, position, tail risk, regret, bargaining, learning,
equilibrium) produce measurably different, and some stronger, play.

**Held fixed in `common/` (one owner):** the B0 draft (Tavi, Varg, Vendetta, Vesper Vale,
Marcella); B0's Monster Strength -> Jungle Health -> Power Strength purchases (Treasury
excepted); an actor-local `InformationState` (public design, own timestamped observations,
unknown is never zero, no ally hidden state, only the 8 shared live bytes); the analytical
`JobForecast` (`deltaS`, `tau`, health, terminal state; potential `Phi_H` with `H=min(128,T-t)`;
four response worlds production/race/interception/denial x three lifecycle states, with
*assumed*, uncalibrated weights); one job menu (<=16 single, 8 double, 4 non-harvesting
finalists); the executor built from B0 fragments (legal movement, combat, capture/channel,
recovery, 25% full-clear admission, emergency retreat); the Tavi sortie runtime; bounded
telemetry and the turn-boundary catch/log/yield (`p3 error: ...`).

**Each bot varies only** the rule that picks a job from that menu and forecast, plus that
rule's own state (beliefs, prices, tables, assignments). Its *control* is the same rule
switched to its declared contrast inside the same module (see Distinctness).

**Not the neighbours.** `models` fixes the objective and varies the forecast; here the
forecast is fixed and the objective varies. `events` fixes one exact event-time arithmetic for
one champion; `frontier` changes the whole representation of the team problem; `specialists`
add one tactical mechanism to B0; `doctrines`, `territory`, `distributed`, `adaptive` and
`commitments` change economic, geographic or organizational policy. Tactical tricks, new live
protocols and executor fixes never earn a bot here: they go into `common/` for every bot.

## Bots

All 30 build. None has a winning record against `anchors/fundamentals` or `anchors/starter-cpp`;
the only wins are single Rows games (Ledger 96-43 in R16; gold tiebreaks at 0-0 gems in R24, R29).

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| ledger | Max converted gems / max(1, tau + renewal-and-return turns) over single/double/bank jobs; renew if none | Lead and library campaign; lost every Rows game to fundamentals in R25-R31 (60-115 vs 229-849 gems in R28-R31); Tavi banks 0 gems |
| atlas | Max `Y(o,production) = deltaS + Phi` of the terminal state: future productive capacity, not yield | Library campaign; Lanes 0-1001 (R16), 66/44 (R18), 33/11 (R19), all lost |
| tempo | Rate plus urgency `0.5*l/(1+max(0,d-tau))` for rewards an observed opponent can take first | Default full forecasts; experimental `PLAN3_TEMPO_NOMINAL=1` removes unconsumed forecast work and restores sustained production in current paired games; health still blocks promotion |
| horizon | After a Ledger warm-up of four 32-round score blocks, max predicted probability of winning the score race | Plays Ledger until 4 blocks exist; next in line for the nominal path |
| options | Atlas's value but the chosen job is held to its end; no milestone reselection (control: per-turn reselection) | Library campaign, lost every fixed anchor setup; control replanning has no nominal path |
| treasury | Chooses among defer / next Monster or Jungle emblem / one Power Strength or Focus shard by production repaid; the only bot off B0's purchase order | Funding damage and recovery have no nominal path |
| market | Draft-time allocation of groups by marginal team gems after duplication (control ignores ownership) | Mechanism not yet exercised natively |
| council | Four proposers (rate, shop payback, best bank, expiring opportunity) judged by Atlas's critic, blind to proposer | Mechanism not yet exercised natively |
| belief | Bayesian intention weights from timestamped personal sightings drive scenario-weighted value | Needs full counterfactual responses (no nominal path); weights uncalibrated |
| scout | Belief plus one-step observe jobs when lifecycle worlds would change the best job | As Belief |
| robust | Max over jobs of min over represented worlds | Mechanism not yet exercised natively |
| insurance | Mean of the worst 50% probability mass of value (lower tail) | Library campaign, lost every fixed anchor setup |
| citadel | Pure maximin over common response-class means (control: uniform mean) | Active and control played identical native replays (R29: won A on gold at 0 gems, lost B 0-473) |
| hindsight | Min over jobs of max preventable loss vs each world's best job (control: security) | R17: 0 gems both sides; Varg/Vendetta found no jobs |
| underwriter | Max mean subject to security >= best security - 10 (control: unconstrained mean) | R24: both arms won Rows on gold at 0-0 with identical outcomes |
| ambiguity | Max over jobs of min over a total-variation polytope around Belief's weights (controls: fixed weights, minimax) | Radius assumed (1/8), not calibrated |
| ruin | Max expected conversion subject to a cap on bag-loss/death probability before the next bank (control: Belief) | Loss budget assumed; no calibrated loss probability |
| lifeline | Only jobs that keep a bank or safe recovery reachable in a finite viability set, then max production | Quiet/safe-recall continuations assumed; nominal endpoint accounting only |
| shadow-prices | Reward minus LP dual prices on health, travel, capital and time (control: fixed prices) | R1 restored liveness (gold, 0 gems); continuation and health budget have no nominal path |
| indexer | Per-camp renewal index `G*N/H` (controls: `run_indexer(1)` rate, `(2)` fixed circuit) | Runs on a forked Campaign copy (`indexer-common/runtime.h`) |
| backpressure | Relieve the largest clear/capture/carry/convert imbalance: pressure `sum(Q*s)/D` (control: cycle rate) | Forked Campaign copy; parity with the shared Campaign broken (known failure) |
| matchmaker | Exact max-weight assignment of champions to jobs on the public draft matrix (control: greedy) | Historical six-map equality; exact current Great Wall B matrix has no positive edge and both arms stay idle. Native differentiation unverified |
| bargainer | Nash bargaining: max sum of log gains above disagreement baselines (control: total) | Static draft allocation; draft valuation and sealed ownership have no nominal path |
| correlator | Joint role/territory bundle drawn from a shared draft signal (control: independent draws, same marginals) | Marginal frequencies proved; no joint gameplay model |
| adapter | Exp-weights bandit over Ledger/Atlas/Tempo/Insurance, updated only from the chosen expert's completed jobs | Parameters assumed; no regret claim |
| changepoint | Bayesian online changepoint (run length <=16, hazard 1/16) restarts opponent inference (control: fixed forgetting) | Detection delay and false alarms unmeasured |
| signaller | Values visible regional presence by the assumed opponent reaction plus production (control: production only) | Opponent responsiveness assumed |
| commitment | Stackelberg leader: persistent regional/economic commitment against pessimistic follower responses (control: minimax) | Follower model assumed |
| sequence | Offline CFR policy table over a two-job/observation abstraction (control: normal-form LP table) | Table from `tools/export_sequence.py`; payoffs assumed |
| belief-search | Depth-two Bayesian lookahead over job outcomes (control: depth one) | No validated generative model |

## Distinctness rules

- A bot is distinct only when a reproducible legal situation makes its rule pick a different
  complete job than Ledger and than its own control. Show the fork in an SDK-host fixture
  (`tests/`) and then natively against the control on the same map, seed and side. Two bots
  that make the same choices count as one; record collapses (Citadel/control R29, Underwriter/
  control R24, Matchmaker exact = greedy) and retire or redesign rather than rename.
- Controls live in the module, not in leaves: `run(..., true, ...)` for most modules;
  `Policy::matchmaker_greedy` / `bargainer_total`; `run_indexer(1|2)`; risk-common
  `run(family, Control::belief|minimax, true, ...)`. Measure one through a scratch bot
  `bots/decision/_control-<bot>/` (ignored by the ladder; move it out afterwards).
- No private executor, menu, safety rule or purchase policy (Treasury's purchases are its rule).
  A rule without support falls back to its runtime controller (Ledger; Market for the
  assignment family), never to a guess.
- Shared code is `common/` plus family modules (`hindsight-common/regret.h` for Citadel and
  Underwriter, `assignment-common` for Matchmaker, Bargainer and Correlator, `commitment-common`
  for Signaller, `risk-common` for Ambiguity and Ruin). One owner per suite at a time; a shared
  change affects every bot: rerun `./arena.cmd build decision` and `./arena.cmd test decision`.
- `backpressure-common/runtime.h` and `indexer-common/runtime.h` are forks of the shared
  Campaign that missed later `common/` work; until re-merged, their results mix rule and drift.

The [retained Matchmaker matrix check](../../results/population/matchmaker-matrix/summary.json)
reconstructs Great Wall B, row 3049, at `f18a69cc1c4c` from actual public native map/order,
catalogue, settings and distance inputs. The unchanged production allocator reproduces
fingerprint 1591821959: 0 positive, 20 known-zero (complete excursions take 142–255 rounds
against H128), and 55 model-unsafe edges. Existing exact and greedy both publish total zero
with all 15 groups unassigned, so this native comparison is parked. This scopes the older
six-map equality; it does not establish universal redundant physical coverage. Known-zero
ties can affect exact owners on other matrices without improving total. The host supplies
ample modeled fuel and fresh draft memory; it proves source reconstruction, not retained
guest scheduling, physical ownership or native fuel safety. The eight-review unowned fallback,
45k selection reserve, and historical 7 exhaustions/9 stalls in 24 games remain separate gaps.

## Layout and checks

- `<bot>/src/main.cpp` is one call, e.g. `plan3_runtime::run(plan3::Controller::ledger, false,
  "p3-ledger-working")` (rule, control flag, telemetry label); `plan3`/`p3` are this suite's names.
- `common/`: `model.h`/`model-capital.h` (pure objectives 01-12), `information.h`, `paths.h`,
  `forecasts.h`, `executor.h` + `reference/` (B0 fragments), `capital*.h`, `allocation*.h`,
  `conversion-witness.h`, `two-trip-*.h`, `sortie-*.h` (Tavi multi-sortie), `policy.h`,
  `runtime.h` (Campaign, bounded planning, catch/log/yield), `telemetry.h` (`P3 {json}`, <=512 B).
- `<family>-common/`: the rule's pure kernel plus a `runtime.h` adapter over the shared Campaign.
- `tests/run.py` is the single entry (`./arena.cmd test decision`, 39 cases;
  `run.py ledger` runs the cases the bot's closure reaches; `--list`, `-k GLOB`). Fixtures
  without an SDK-throw shim build through `tools/arena/wasi.py`; the rest use the plan3 flags
  (-O3, no wasm EH: the shim turns SDK throws into aborts). SDK-host fixtures compile `cambc.h`
  with `__wasm__` undefined against hosts generated by `tests/hosts.py` (count-checked seams
  over `tests/host/sdk-shim.h` and the CLI 2.2.5-era captured designs in
  `tests/host/public-config.h`; SDK 3.0.4's 13th design gets a start-up placeholder; current
  Varg/catalogue records come from `docs/reference/runtime-catalog.json`, Rows from `maps/geometry.json`).
- **Known failures** (`KNOWN` in `tests/run.py`; pre-existing, still run and reported):
  two-trip-ledger/tavi (the shared Ledger Campaign never starts Tavi's two-trip episode);
  sortie-runtime, all six callback modes, and sortie-ability rows-default/rows-static-kit
  (stale after the last plan3 sortie and runtime edits; R31's scoped SDK checks were never
  finished); backpressure shared-parity (fork drift). Fixing them needs gameplay decisions.
- `./arena.cmd build decision` builds the 30 bots. `tools/export_sequence.py` regenerates
  `sequence-common/policy-data.h`; the test fails if the committed table differs.

## Leads

The Indexer nominal alignment check (23 October, engine 1.0.3) is a bounded negative.
Default-OFF `PLAN3_INDEXER_NOMINAL_CAMPAIGN` aligns only its nominal acquisition
materialization with shared Campaign. Indexer's fixed physical family, fresh/current
renewal services, `G*N/H` index, rate/circuit controls, safety and paid executor remain
unchanged. Production fixtures compare exact menu order, consumed nominal/renewal
inputs, geometry, all three rankings and conservative conversions on both sides and
Varg/Tavi, including eligible fresh triples. Declared fixture cash is not earned
native income. All 30 default raw/injected modules and the retained Tempo experimental
recipe equal their incumbents: 62 byte comparisons precede ON preparation.

Same-run RAW `dda90df2c8ec`/`8f28c2983dc8` against frozen Fundamentals on Ambush and
Great Wall both sides completes all 8 executions. Every pair has byte-identical replay
and canonical statistics; all 40 subject actor-games have zero outgoing damage, earned
income and banking, and every subject team still stalls. Opponents are healthy and
productive. All four baselines also exactly reproduce the retained failure replays
and statistics. Great Wall maximum fuel falls 82491 to 80826(A)/82152(B), which does not
establish activation. The incumbent reaches `selection_deferred` at round 174 around
31k fuel; ON emits no compute rows and still reports zero decisions/activations.
Keep the knob OFF and stop this discriminator without pool/fresh qualification.
The all-group/item numeric preflight with its 65k internal floor and eager information
remain separate source-supported suspects; their exact native refusal is unproved.
This does not finish the Campaign fork merge or establish actor liveness/admission.
The full suite passes 41/41 cases with the same 10 declared KNOWN failing parts;
the enabled Indexer paid-cycle fixture and eight exact-equivalence runs pass.
Finite proof is in `.cache/arena/orchestration/mass-activation-next/indexer-campaign-alignment/`.

The [startup phase check](../../results/population/decision-startup-repair/index.json)
(23 October, engine 1.0.3) locates the remaining initial exhaustion inside public
field ordering: on Road of Riches the final camp-layout sort leaves about 62k fuel,
wall preparation leaves 55k, and all five B actors resume field ordering in the next
round. Default-OFF `PLAN3_STARTUP_LAYOUT_PHASE` publishes the immutable layout one
callback later, giving the unchanged route ordering fresh fuel. It changes no
ordering comparator, complete-job rule, observation, safety floor or paid executor.
All 30 rebuilt OFF raw/injected modules equal their incumbents; Tempo's nominal OFF
body also equals `2ba3736602b9` before ON games. With `PLAN3_TEMPO_NOMINAL=1`, paired
`6d5bac5f19df`/`f593a889305d` games on the four predeclared failure maps remove all 10
Road B/Islands B startup exhaustions. Their B actor production totals are unchanged.
All 16 ON actors across fresh Starter Lanes/legacy Expanse both sides personally
damage, earn, bank and repeat paid work; those eight paired games are fully healthy.
The four subject Corridors/Yellow Brick Road stalls persist. Fundamentals also
stalls on Islands in both arms/sides; delayed startup removes Varg/Vendetta's partial
Yellow Brick Road B work. Road B still has two zero-work actors. Keep both knobs OFF:
this is an experimental startup health repair, not full-pool health or universal
liveness. Corridors has repeated selection cutoffs with no activations; Yellow
Brick Road selects jobs but logs repeated no-progress/emergency aborts. Diagnose
those separately before attributing both stalls to empty safe menus.
The full suite passes 39/39 cases with the same 10 declared KNOWN failing parts;
the new fixture checks both arms/sides, exactly one added callback, immutable
layout membership, idempotent publication and unknown unfinished routes.

The bounded continuation check (25 October) separates the first missing movement
at R134 from the R141 no-progress abort and R143 reselection on Yellow Brick Road B.
The original native Marcella already casts at R131 and moves at R133, so whole-window
executor nonentry is ruled out; its later private return branch remains unknown.
Default-OFF `PLAN3_CONTINUATION_JOURNAL` records three callbacks (134/141/143) from
existing identity, job, observation, target, fuel and move-status values, without
additional game queries. Its actual-Campaign SDK fixture passes on both modeled
sides across OFF and matched emit0/emit1 frames (one focused case, six runs); this
alone does not establish native fuel safety or noninterference. Both emission
frames use nonzero data words with a final low-bit switch. Actual RAW and injected
code/non-data/custom sections match; one data byte2→3 controls output. Six injected
CLI reproductions preserve the entire public replay and complete native statistics
across legacy/control/emit on both sides, including exact reproduction of the
original B game. Their matched-frame fuel prefixes agree through the sample window.
The [licensed case](../../results/population/decision-job-continuation/index.json)
binds the inputs, output hashes, bounded values and limits.
All 30 current default RAW bodies match their protected incumbents: 28 owner-local
production builds and two unchanged source recipes. These proof copies are not
canonical game entrants. The test dispatcher now gives each same-case invocation
its own directory; a canary regression and the six-run Campaign fixture pass after
that isolation fix. The supported owner snapshot namespace then builds the exact
historical RAW0690…/injectedc221… control without Arena module growth; the diagnostic
experiment records all actual roots and closes after recovering its six completed
RAW rows with zero reruns.

Observed in the reproducing B frame: R134 enters execution at33,686 fuel with a
fresh alive observation, selects target5, then reports movement `fuel_cutoff`.
R141 has no active job and13,202 at the final boundary; R143 holds cycle485 again
but has19,585 and does not execute. This shifts the next allocation toward shared
pre-action/target-query cost and scheduling, rather than objectives or occupancy.
These are instrumented private values: identical public play/statistics do not
prove original private fuel/history equivalence or zero capture cost. No reserve,
rule or gameplay repair changes, and there is no qualification or strength claim.
The first proposed cost lead was repeated live `me.position()` production inside
the action-free target-ranking loop. Same-round reuse must survive an adversarial
round-change check. Its import count, debit and adequate savings needed measurement;
the host result below answers that allocation.
The subsequent explicit RAW-role check replays the same six cached keys without
new builds or arms. All three RAW modules preserve baseline public play and native
statistics on both sides; every RAW payload/statistics hash and bounded packet also
matches its SDK counterpart. The existing fastrun RAW outcomes, rounds, gems, gold,
kills and deaths agree. Thus the R134 execution/target/fuel-cutoff observation is
now measured in the floor's RAW role, rather than inferred from SDK. The original
private-fuel disclaimer still applies. Both role records remain separate; the
already-closed experiment spec and SDK record are unchanged, and regenerated RAW
replay bulk is removed after compact publication. No cost probe or repair follows
without a separate allocation.

The subsequent transparent host check rejects that small position-cache lead for
the measured uninstrumented RAW case. Marcella actor9 on Yellow Brick Road B/R134 reaches
the actual movement guard with 29,029 fuel against its 30,000 floor: shortfall 971.
Five eligible camps produce five self-state queries, so loop-local reuse would
remove four. Their observed adjacent guest-work envelopes total at most 436,
including retained camp-wrapper/score work; even all five envelopes total 545.
This is a conservative local work-selection bound, not an isolated marginal cost
for a newly compiled cache implementation. Zero direct host charge does not mean
zero guest cost. The full ranking loop consumes 2,383, and the retained subsequent
fight/argument/approach setup consumes 1,343; those costs are not assigned to the
repeated position reads. The earlier instrumented 33,686 value is not substituted
for any original fuel value.

The unchanged validated host observer reproduces complete public replay, recorded
fastrun statistics, stdout and both fuel streams on both keys. Only B9/R134 supplies
cost data; the impossible A9 filter selects no turns and serves paired whole-game
parity. No caching, policy, objective or fuel-fence change follows. The
[compact cost rejection](../../results/population/decision-work-cost/index.json)
binds the exact RAW payload, source intervals, assumptions, closed lifecycle and
the private-row diagnostic-marker adapter correction.

The broader owning-state feasibility check also ends negative. It combines each
eligible camp's three fresh CampGet records into one checked 32-byte owning HP/current
position observation, plus one ranking-local self position. The original group/alive
guards, float arithmetic, order, combat and reserves stay fixed; fresh round fences
discard partial rankings after suspension. The actual Campaign fixture passes target,
tie, current-versus-spawn, error and 22 epoch/reserve adversaries. Tempo OFF reproduces
RAW0690…/SDKc221… exactly before the two-arm, both-side Yellow Brick Road RAW test.
Its four aggregate rows match, but native B public play eventually differs: all
decoded public boundaries agree through round 144, and the first champion-track
difference is actor 6 at round 176. This does not isolate later behavior to the old loop.

The unchanged transparent observer licenses the actual ON B9/R134 trace against
its trace-OFF complete public/stdout/both-fuel files and the canonical ON row. At
the same 33,788-fuel execution entry and selected camp 5, the movement guard receives
28,487 instead of 29,029: 542 worse, still below its unchanged 30,000 floor. Five
CampGets replace fifteen and one self query replaces five, but the complete compiled
checks/copies/code generation erase the saving. The measured final fence-to-fight
tail is 70 fuel; this exact-key observation is not a universal bound. The completed
trace-OFF duplicate raw row was lost after a driver alias-label assertion and is
explicitly unknown; no row was reconstructed or game rerun. Both native B subjects
retain their stall with zero faults/exhaustions/errors. Reject the implementation,
restore exact shared sources/tests, and stop before the 30-bot matrix or Main panels.
The [compact result](../../results/population/decision-state-cache/index.json) records
the closed experiment, exact inputs, limits and this early-feasibility ordering.

The immutable identity-join component has a positive early result, with adoption
still pending. Default-OFF `PLAN3_OBSERVATION_IDENTITY_JOIN` indexes only the SDK
group's stored spawn/handle membership, validates its exact ordered metadata before
reuse, and resolves unique matches back into the current member span. Bounded
construction and unsupported/unprepared fallback preserve the original scan.
GroupDesign, forecasts, objectives, visible-list first-match search, fresh live
queries, errors, clocks, publication and reserves remain unchanged. The actual
30-member producer fixture passes 14 runs covering 30 scenarios, two boundary
checks, both arms/sides and five error modes, including changed live scalars after
warming the index. Tempo OFF exactly reproduces RAW0690…/SDKc221… before games.

The first native RAW difference is Tavi B5/R134/input2141: identical prior public
history, then OFF ends with movement unused while ON moves (36,15) to (35,14) and
uses its existing fighting code. The unchanged host observer measures that same
callback in both arms, with complete-file/control and canonical-row checks.
Priority observation costs 53,410 to 27,915 fuel (25,495 saved), with 1,411 to 541
fuel polls and identical fresh group/state/info query order, identities and status
codes. The unchanged 30,000 movement guard receives 27,067 to 52,534. These are
actual B5 measurements; the earlier Marcella costs are not transferred.
The four both-side RAW games have zero recorded faults/exhaustions/errors and no
wins. Native B still stalls with zero clears, income or banking, despite monster
damage rising 720 to 2,880. The 30-default preservation matrix below is now complete.
Keep the component OFF pending productive native continuation and a current-Main
panel; cold-construction utility and whole-suite ON health remain unproved. Exact inputs,
semantic limits and the closed lifecycle are in the
[component evidence](../../results/population/decision-identity-join/index.json).

Required shared-module preservation is complete with the component still OFF:
all 30 current default RAW and SDK bodies match their actual pre-component hash
bindings. Twenty-eight prior SDK bodies were rehashed; Backpressure and Indexer's
two absent SDK bodies were rebuilt against exact-key/source-matched historical
bindings, restoring replay availability without assuming SDK equals RAW. The
43 remaining suite drivers complete with no new failures; their ten declared
KNOWN failing parts remain unresolved and are not passed tests. The unchanged
producer's 14 runs are reused. Sources, real shared admission, owner-local output
and empty private Jobs are recorded in the existing component index. No default
promotion, new game or endpoint experiment follows from these checks.

The [bounded continuation classification](../../results/population/decision-continuation-next/index.json)
then reused the same ON RAW game, with two unchanged-observer regenerations and
exact native replay-body, statistics, both stdout and both fuel-file equality.
At Varg B6/R158, active-group observation consumes 40,218 fuel (69,943 to 29,725).
Threat inspection leaves 21,679 at the unchanged 30,000 continuation guard, so
execution is deferred despite personally visible surviving work and legal steps.
The optional review guard sees 21,820 and skips; the proposed cost of constructing
`information()` before its inner review check does not explain this callback.
The remaining original visible-list search has long guest-only fuel-poll runs;
the current list contains 46 camps. Those measured costs select a bounded current-list
join as the next candidate, without promising marginal savings, caching live state,
lowering reserves or changing objectives. No private recovery flag was sampled,
no fix was tested here, and other callbacks remain unclassified. The parent
experiment is closed, both private Jobs are empty and raw bulk is removed.

The subsequent [bank-proxy trial](../../results/population/decision-bank-proxy/index.json)
is negative. It kept the visible-index recipe and replaced repeated self-position
queries in the existing altar-distance loop with one fresh query; empty altars,
the banking predicate and reserves stayed unchanged. OFF exactly reproduced the
visible-ON control's RAW and SDK bodies. Four full Road/Fundamentals RAW keys and
four native replays show no health benefit: both A arms have zero production and
native stall flags; B changes from 135 banked/3235 gold with exhaustion at actor8/R815
to 83/1866 with exhaustion at actor7/R352. The changed histories do not measure ON
cost at the old B8/R815 input. No tuning or Main panel followed. The experiment is
closed, raw bulk removed and all private Jobs empty; runtime.h is restored exactly
to its pre-trial bytes, with no additional retained knob.

The later [default-OFF bank materialization fence](../../results/population/decision-bank-fence/index.json) uses the
existing45k reserve before bank entry and response/lifecycle copies; refusal
clears outcomes and marks the forecast incomplete. Under the **old CLI/SDK3.0.5,
engine1.0.3 bundle**, its specific Tempo OFF RAW/SDK bodies exactly match the
visible-join variant. Two modeled SDK fixture runs exercise every refusal and
fresh retry. Four paired RAW keys and four native cases on Yellow Brick Road
against Fundamentals show B's exhaustion at Vesper Vale8/R815 disappears, with
both teams' recorded B health clean and maxfuel88,971. Production stays exactly
135banked/3235gold/13clears; A still stalls at zero production. Neither arm has
a recorded actor8 productive event from815 onward. This is a bounded safety
component, not productive continuation, default promotion or a Main win.
The user reported a game update during the final read-only public-prefix check;
that check was cancelled with its private Job confirmed empty, so public-prefix
equality remains unmeasured. The experiment is closed; new builds/games/probes
stopped. Revalidate against the reconciled bundle before
using this old-epoch evidence. Keep the knob OFF; no30-default matrix or repeated
Main panel is justified by unchanged outcomes.

Preceding Tempo nominal activation check (23 October 2026):
[`decision-tempo-nominal-activation`](../../results/population/decision-tempo-nominal-activation/index.json)
retains CLI/SDK 3.0.5, engine 1.0.3, toolchain 3837f00416ea exact inputs. The shared
default-OFF `PLAN3_TEMPO_NOMINAL` branch applies only to direct Tempo: its acquisition
forecasts use the existing nominal path, and Bank/Renew omit terminal renewal loops
which neither Tempo arm consumes. Complete-job timing, safety, recovery, personally
supported deadlines/releases/denial, and the independent urgency objective are preserved.
Supplied policies and every other controller retain their old forecast preparation.
All 30 default-OFF bots build; both raw and injected modules equal their same-toolchain
incumbents byte for byte. Tempo's separate OFF proof preceded native ON execution.
The full suite reports 38/38 cases passed with the same 10 declared KNOWN failing parts;
the new both-side Tempo fixture checks consumed-feature equality and capture/bank execution.

All 40 pool maps and both sides were attempted against frozen Fundamentals. Three
baseline cells have incomplete native exhaustion context (Great Wall A/B, Starvation B),
so only 77 pairs support the comparison: +480 clears, +2402 banked gems and +24429 earned
gold; banked gems increased in 49 pairs, tied in 24 and decreased in 4. Lanes B changes
from 1 clear/11 banked to 15/165 with native per-actor capture, income and repeated banking.
Across 345 matched actor-game observations, actors that bank increase from 189 to 215;
actors with bank -> renewed damage -> payout -> bank increase from 129 to 177.
Fresh Starter Lanes A/B changes 22/11 to 253/253 banked; legacy Expanse 116/80 to 148/132, all 8 confirmation
games clean. These are activity results, not admission or a strength claim.
ON still has 10 exhaustions across Road of Riches B and Islands B and zero-clear stalls
on Corridors and Yellow Brick Road both sides. Keep the knob OFF. The next repair
should bind those actor-local startup/empty-menu refusals to their exact native inputs;
do not infer that cheaper forecasts solve every actor's inactivity.

The default-OFF `PLAN3_QUIET_BOUNDARY_PACKET` diagnostic is an unsuccessful
measurement experiment. Its private phase stamps for Marcella on startup-repaired
Tempo's Yellow Brick Road B recipe are formatted only after rounds131–146, but
the enabled build still changes the full replay/statistics and introduces two
exhaustions and an error. Only five of16 deferred rows are emitted. Those rows
cannot establish the incumbent's execution cutoff. All30 defaults retain exact
raw/injected incumbent bytes with this knob OFF; the retained experimental OFF
recipe also reproduces the original entire replay archive and statistics. Keep
the knob OFF and require a noninterfering caller witness before changing any
ranking, menu, observation or executor guard. Exact inputs and the failed identity
gate are retained in `.cache/arena/orchestration/mass-activation-next/decision-actual-quiet-boundary/`.

1. **The shared model does not run within fuel, so objectives rarely diverge in play.** The
   four library campaigns lost every fixed anchor setup to B0 and Starter; Ledger loses Rows
   to fundamentals; Tavi's private sortie evaluator never started in R30 (0 model starts).
   Profile `common/` turn work and make the forecast cheap before tuning any objective.
2. **Finish R31** (defer live inventory/kit and combat-prune queries until static geometry is
   complete): bring the sortie fixtures back to green, then replay Ledger on Rows
   (`./arena.cmd play decision/ledger anchors/fundamentals --maps 00rows --keep-replays all`) and
   analyze its losses before the next sortie change (R-numbered results are history: no artifacts).
3. **Re-merge the Backpressure and Indexer runtimes into `common/`** so their rules run on the
   shared executor; then rerun backpressure shared-parity.
4. **Spread the cheap path only where consumed features permit it.** Default direct Ledger
   takes slim nominal acquisitions (R25); direct Tempo now has the default-OFF native-tested
   path above. Finish its health obstructions before promotion. Horizon still consumes
   terminal potential after warm-up; spreading the shortcut is blocked until designed for
   Options-control replanning, Risk/Belief/Scout counterfactual
   responses, capital funding damage/recovery, Shadow continuation/health budget, Indexer
   fresh healthy full-group periods and assignment draft valuation/sealed ownership.
5. **Native forks first:** each rule needs one reproduced decision difference against its
   control before any strength claim; add a `constexpr` control knob per entrypoint so
   `arena sweep --var` can run matched arms.

Idea bank (unimplemented, one line each):
- Chimera (15): restricted-equilibrium mixture over campaigns (old leaf was a Ledger alias); needs shared feasible opponent columns.
- Oracle (32): double-oracle expansion of the campaign library against its largest measured weakness.
- Distributional values (33): predict the whole secured-output distribution, not mean plus one tail.
- Time-consistent risk (34) and risk budgets (35): nested risk; a failure allowance spent unevenly across actors/phases.
- Entropy-regularized policies (36): keep a controlled spread of good jobs for robustness.
- Bounded-rational opponents (37) and population uncertainty (38): noisy/finite-depth responders; robustness over a prior family.
- Cooperative attribution (39): Shapley-style marginal value of actors, resources and kits.
- Submodular observation (40): marginal-coverage scouting without shared vision.
- Average reward vs shortest path (41): when sustainable income should yield to the fastest win.
- Pareto archives (42) and quality diversity (43): keep trade-offs and behaviour niches (MAP-Elites idea) before one scalar.
- Counterexample-guided synthesis (44): repair repeated failures in a small rule grammar.
- State abstraction (45) and value of computation (46): sufficient features for job choice; fuel only for decision-changing work.
- Adversarial scenarios (47) and causal interventions (48): legal opponents/maps that expose weaknesses; which dependency explains a gain.
