# Contenders: long-running compute portfolio for three extra systems

**Status:** twelve C++ optimization options are present in the repository, revised 23 October
2026 for three extra APU systems with 32 GB RAM each. Start with
[`tools/optimization/`](tools/optimization/README.md) for the executable, launchers,
checkpoints and each program's actual scope. The original research deliverables are not all
complete: a runnable model optimizer does not by itself satisfy native execution or deployment
requirements. The descriptions below remain a research draft. Bounded readiness campaigns do
not establish competitive improvement, and no hours-long worker campaign starts automatically.

Implementation choices deliberately revise the draft where useful: C3 uses event-based anytime
scheduling rather than an external CP-SAT dependency; C5 uses a small CPU episodic policy-gradient
actor over safe native controller preferences rather than claiming a recurrent MAPPO system.
C6 can preserve an already compact teacher exactly instead of spending days relearning its
known weights, and fits values only to observed outcomes. C4/C8/C9 explicitly describe their
restricted models. C11 uses actual native replay branches and retains counterexamples. C12 adds
one native ordered-action search engine and a campaign wrapper over actual retained replay
states, rather than deriving champion kits from a reduced combat model.

Current readiness is qualified by domain:

- C3 has 82 focused checks and eight economic model cases covering routes, investment,
  cooperation, recovery and banking. A guarded native macro executor, qualified service costs
  and rolling replanning under disturbance/opposition remain essential gaps. See its
  [evidence index](../../results/contenders/optimization/c3/index.json).
- C4 has 32 model capture-and-departure cases with continuation frontiers, plus six strict
  native traces from three actual Varg replay windows. Broader party, equipment and geometry
  coverage and transfer over the frozen current controller remain unqualified. The shared
  `restricted-local-v4` simulator matches 90 native Varg/camp18 traces from 78 actual prefixes
  plus 12 suffix variations, covering 5,141 commands;
  that is coverage of those domains, not a universal model proof. See the
  [C4 index](../../results/contenders/optimization/c4/index.json).
- C8 provides broad reactive curriculum search and resumable model campaigns. The frozen
  actual-controller bridge and paired native qualification of its generated witnesses remain
  missing; model counterexamples are not demonstrated failures of the deployed bot. See its
  [evidence index](../../results/contenders/optimization/c8/index.json).
- C9 has 47 checks and a fair, bounded eight-stratum campaign through 410,000 model nodes.
  Native endgame reconstruction, natural-state frequency and information-safe deployment remain
  gaps. Its model proofs and partial policies do not solve the whole game. See its
  [evidence index](../../results/contenders/optimization/c9/index.json).
- C12 searches actual equipped native contexts, including adverse selected-owner replies and
  farm clear/capture/departure continuations. Its finite-horizon objectives, feasible endpoints
  and explored counterplay are not certified interaction or full-game wins; deployment and
  broader transfer still require qualification.

## 1. Actual objective and compute envelope

Develop a measurable understanding of strong play, then turn that understanding into three
competitive, distinct contenders. The first competitive bar is the current live main bot;
there is no ceiling. Read [LIVE.json](../../results/releases/LIVE.json) when freezing each
comparison. At drafting time it names main-v42, source `main/main@0353a987e04f`, anchor
`anchors/main-v42@9fc5d7469074`; the pinned bundle is CLI/SDK 3.0.5, engine 1.0.3.

The user has three extra systems, each with a decent APU and 32 GB RAM, available for arbitrary
programs. Their purpose is sustained unattended computation over genuinely hard search or
learning problems: useful campaigns lasting hours or days. Interactive agents cannot enumerate
these spaces. Agents can build the programs, define valid evaluations and integrate outputs;
they are not the inner search loop.

The payoff is **expected transferable playing capability**. A stronger controller, draft policy,
equipment policy, reusable solved frontier or computational headroom can be worthwhile without
producing a new human insight or changing a high-level strategic decision. Understanding and
proof are additional benefits, not prerequisites for value.

Treat the machines as three independent 32 GB workers, not one shared 96 GB computer. Exact APU
models, usable memory, operating systems, accelerator support and engine throughput are not yet
measured. CPU-first workloads are the initial hardware assumption. Small-network RL remains a
candidate; neither GPU acceleration nor a particular RL throughput is assumed.

Keep short mechanism checks, ordinary debugging and small comparisons local. A remote campaign
qualifies because its useful search/training space takes sustained work, not because we add
repetition or an unnecessarily long horizon. Local preparation may be short. A large atlas of
many meaningful independent cases can qualify even when an individual case is easy.

### 1.1 How expected value is assessed

Compare expected strength/coverage gain, chance of a valid deployable result, reuse across bots,
engineering and integration effort, hardware fit, and productive improvement over time. Do not
invent numerical probabilities, Elo gains or simulation rates before measuring them. A long
runtime and a large search space are eligibility conditions, not evidence of high value.

The ranking below is a prior based on our current evidence and missing interfaces. It must be
revised after small readiness checks and measured search/learning curves. No user example,
including attack order or group movement, is automatically a priority or a required benchmark.

### 1.2 Independently generated options

| ID | Long-running program and product | Why sustained compute helps | Engineering/readiness and main risk | Initial EV assessment |
| --- | --- | --- | --- | --- |
| C1 | Automated controller/program search: evolve bounded decision programs, policy graphs or compact policy weights, evaluated in the real engine | Search across many interacting behaviors, state-dependent choices and counter-strategies; retain diverse successful controllers | Medium-high setup. Requires an expressive legal policy representation and efficient candidate evaluation, but not a predictive world model. A narrow grammar can reproduce the ceiling of existing heuristics. | High capability upside; a leading first-wave candidate if the representation/evaluator can be built economically |
| C2 | Joint draft, item/build, role and opening configuration search: produce conditional policies for a frozen executable controller | Coupled categorical/sequential choices create a much larger space than a few hand-picked packages | Relatively low-medium setup if arena exposes the needed choices. Native outcomes avoid model bias; weak champion execution and an overused opponent set can cap or distort the result. | Strongest provisional near-term return per engineering effort; a serious search campaign, not an assertion that more tuning must work |
| C3 | Whole-economy planning: optimize complete routes, cooperation, purchases, recovery, capture and banking | Long-horizon resource-dependent scheduling and branching investment choices defeat manual enumeration | High setup and dependence on credible clear/travel/state costs. A solver can optimize an incorrect abstraction very effectively. | High structural upside; launch after the relevant costs and native execution interface are credible |
| C4 | Solved clearing/matchup atlas: search many champion/party/equipment/terrain/resource states and retain feasible continuation frontiers | Joint action sequences and the combinatorial state family require a large offline campaign | Medium-high setup; needs native branching or a qualified local model. Exact universal tables are intractable, so partition useful domains and report remaining gaps. | Strong reusable fundamentals candidate; high value if search can expose exploitable executor headroom |
| C5 | Population self-play RL: train compact recurrent or hierarchical policies against diverse opponents | Repeated experience can learn contingent behavior and long-term credit assignment without manual rules | High setup; success depends on observation/action design, environment throughput and learning efficiency. Three APUs may support a small system, but benchmark before committing to long training. | Uncertain, potentially high upside; eligible for an early bounded feasibility comparison, not automatically first or excluded |
| C6 | Expert imitation and counterfactual value learning from valid local top-team/strong-policy data | Large datasets and fitted value/policy models can transfer skilled behavior or improve action ranking | Conditional on enough action-level, version-compatible, information-safe data. Recorded opponents cannot react to changed actions; distribution shift can defeat cloning. | Could move into the top tier if the dataset is usable; currently a major readiness unknown |
| C7 | Automated best-response league: discover strong exploiters and robust policy mixtures | Repeated optimization against different opponents explores strategic cycles and weaknesses | Reuses C1/C2/C5 search machinery; requires opponent diversity and validation outside selection. More self-derived opponents alone do not establish strength. | Useful second objective for an existing search system; lower EV as an evaluation-only standalone build |
| C8 | Adversarial scenario/curriculum search: evolve legal states/maps/opponent responses that expose recurring failures | Search over arrangements, timers, resources and behavior finds interactions sparse natural play may miss | Moderate setup once controlled cases exist. Pathological cases may have little deployment relevance; finding failures has value only if a controller/model can use them. | Conditional support workload; prioritize demonstrated recurring losses and transfer |
| C9 | Restricted endgame solving: produce exact or bounded policies for banking, denial and terminal contests | Small spatial regions can still have huge action/timer/game trees | Medium-high setup; requires a tractable state domain and evidence that the resulting situations matter often enough. | Potentially valuable specialist; frequency and reusable coverage must justify it |
| C10 | Fuel-directed superoptimization/autotuning: search equivalent implementations, representations or compiler choices for hot paths | Large implementation spaces can yield deployment headroom that ordinary code review misses | High verification burden and tooling constraints. SDK costs or poor decisions may dominate rather than generated code. Fuel-dependent decisions complicate equivalence. | Defer unless measurement shows compute/fuel is limiting useful play |
| C11 | Automated model identification/falsification: search for missing action effects, timing rules or predictive errors | Large interaction combinations and counterexample-guided refinement can improve a model used by many planners | Useful only with a concrete dependent optimizer. Re-discovering documented mechanics or producing isolated edge cases can consume days without improving play. | Enabler for C3/C4/C9; not the default destination for spare machines |
| C12 | Native ordered-action microsearch: search actual replay states and equipped kits, retaining counterplay trees, action sequences and farm continuation frontiers | Primitive action order, positions, abilities, attacks and adverse replies create large trees across meaningful native contexts | Uses pinned native replay-prefix reconstruction rather than arbitrary state restoration. Bounded actions/horizons and declared background policies restrict coverage; local objective estimates do not certify wins. | A shared native route for micro-control work; value depends on measured executor headroom, relevant context frequency and deployment transfer |

These options overlap in reusable infrastructure but have different products and objectives.
Do not count an evaluator, a population algorithm and the policy it trains as three independent
benefits. C1 can target any deficient subsystem or a broader controller; its selection is not
a commitment to the tactical examples in the discussion.

### 1.3 Provisional shortlist and what could change it

The first shortlist is **C2 for near-term engineering-adjusted return, C1 for broader capability
search, and C4 for reusable fundamentals**. This is not a claim that they have the highest true
EV: we lack measurements needed to establish that.

- C2 rises if candidates are cheap to express/evaluate and a broad validation population exists.
  It falls if the exposed choices are already saturated or outcomes are dominated by execution.
- C1 rises if compact programs express meaningfully different, deployable behaviors and improve
  across held-out cases. It falls if grammar/interpreter work dominates or selection overfits.
- C4 rises if native reconstruction/search is fast enough and feasible alternatives outperform
  current execution. It falls if useful domains cannot be searched or improvements do not transfer.
- C6 can replace one of these if the expert data is rich and observable enough to train from.
- C5 can replace one if a small CPU/APU learning system has a better measured improvement curve.
- C3 can become the strongest option once its cost model and execution are qualified.
- C12 rises if native ordered-action search finds reusable continuation gains over frozen
  executable controls at an affordable branch cost; selected-context gains alone do not qualify
  a deployed micro controller.

Do not lock each machine permanently to one research identity. Stage viable work packets and
allocate the three workers to productive campaigns as their prerequisites become available.

### 1.4 What the leading search programs actually do

**C1: simulation-driven policy search.** Define a bounded policy language or compact policy
parameterization with legal candidate actions and allowed observations. Search conditional
control structure as well as numerical parameters where feasible. Genetic programming,
evolution strategies and quality-diversity search are candidates; their fit depends on the
representation. Start from executable reference controllers and retain them as incumbents.
Evaluation uses native tasks/complete games and held-out validation. Archive policies and
their failure families; human-readable explanations are optional. A policy that simply plays
better is a valid output. Avoid an LLM call or manual edit for every candidate.

**C2: coupled configuration optimization.** Represent legal draft/order choices, item milestones,
role allocation and opening alternatives in one conditional search space. Use mixed-variable
model-based optimization, evolutionary search or another measured search baseline rather than
assuming continuous CMA-ES fits categorical trees. Evaluate candidates against matched native
controls; allocate fuller coverage to competitive candidates while testing that cheap screening
does not systematically discard eventual winners. The controller version is part of the result.

**C4: computational atlas.** Partition meaningful scenario families by champion/party, geometry,
equipment and resource/phase conditions. Use exact or anytime search to produce replayable
solutions, bounds and continuation frontiers, then validate compact deployment policies. Search
both near-natural states and deliberately chosen difficult states with declared provenance.
The campaign is the hard state family, not one tiny camp fixture stretched over several days.

**C12: native ordered-action search.** The shared
[`micro.cpp`](tools/optimization/src/micro.cpp) engine keeps a progressively widened MAX/MIN
tree over ordered primitive actions, with permissions and transitions supplied by the actual
native engine. States come from unchanged retained replay prefixes with their real kits,
equipment and timers. Selected enemy owners supply adverse MIN replies; the other owners'
declared background programs are assumptions, not adversarial guarantees.
[`micro_campaign.cpp`](tools/optimization/src/micro_campaign.cpp) mines and schedules varied
contexts through this same engine. The supplied
[`micro-worlds.json`](tools/optimization/examples/micro-worlds.json) targets four contexts;
[`micro-worlds-wide.json`](tools/optimization/examples/micro-worlds-wide.json) expands that
family to at most 32. One work iteration counts an evaluated native action prefix, not a complete
game.

The products are per-case action alternatives, a partial policy, an explored conditional
counterplay tree, native trace acceptance where available, and feasible farm endpoints through
clear, capture and departure. These frontiers are not complete Pareto sets. Feasible completion
times provide task bounds under the declared background; cutoff utilities and unexplored replies
provide no certified interaction or full-game win bound. This is a substantive native search
program for micro-control research, with measured transfer and information-safe deployment still
separate obligations.

Exact planning plus policy distillation can follow **Expert Iteration**. This remains one route
for C3/C4, not a compulsory pipeline for native black-box search or model-free RL. The detailed
work packages below describe shared capabilities; each selected program uses only its necessary
dependencies.

## 2. Questions exposed by our evidence

The following are recorded observations and open questions, not established policy remedies.

| Gap | Existing evidence | Question to answer with compute |
| --- | --- | --- |
| Gem production versus gold farming | Exact baseline reproductions collect 160 versus 1,013 gems on Stampede A, and 77 versus 1,097 on GoldRush B. Perfect banking would still lose. | Which complete clearing/investment plan produces the winning gem supply earliest? |
| Action sequencing | Ryth episodes trade camp damage for a later enemy cast. In one turn both alternatives deal 180 damage, but one stuns and the other preserves a step. | Which action bundle has the best continuation, including position, health and cooldowns? |
| Investment value | Tavi field spending accelerates passive completion by about 27 rounds, but its full-pool v42 result falls from 40/80 to 29/80. | When does growth create useful contested power, and when does it merely improve an uncontested proxy? |
| Clear-time and survival estimates | Some existing models are aggregate forecasts; controlled mechanical proofs cover narrow conditions. | How accurately can we predict the next state, a complete clear and the required recovery? |
| Cooperation | Arrival, tanking, capture and shared cast/movement constraints can invalidate additive DPS estimates. | When is another body worth the travel and lost parallel production? |
| Draft quality | A roster change also changes which champion controllers are exercised. | Is a roster weak, or is our execution of its kits weak? |
| Generalization | Training leads repeatedly disappear on pool/legacy/opponent transfer. | Which improvements survive new geometry, states and opponent strategies? |

Start from the [collection accounting](../../results/contenders/timetable/race-loss-reproductions/selected.json),
the [Ryth action assessment](../../results/contenders/micro/ryth-reservation/assessment.json),
and each bot's current `NOTES.md`. The Ryth sample is outcome-conditioned, four paired game
keys; it does not establish enemy intent or explain terminal wins. Do not promote these
examples into estimates of ordinary-game failure frequency.

## 3. Formal problem and objective

The full game is a sequential, partially observed adversarial game with cooperating champions
inside each team. With an opposing policy fixed, our decision problem can be viewed as a
partially observed control problem. Champions must still act from their permitted observations
and communication; a centralized training process does not grant shared vision at deployment.

The competitive objective is:

```text
maximize E[engine-defined terminal win payoff]
over map, side and opposing strategy
subject to legal actions, permitted information and measured turn fuel/liveness
```

The engine's scoring and tiebreak rules define the payoff. Re-query runtime settings rather
than embedding today's winning score, cooldowns or prices into the optimization definition.
Report per-family and worst-family outcomes as well as the aggregate: the opponent distribution
we choose can conceal a serious weakness.

Subproblems have explicit conditional objectives:

- Local clear: minimize time to completed capture, subject to a specified health/continuation
  requirement; retain other nondominated outcomes.
- Uncontested economy: minimize time to the winning banked score, or maximize banked gems at
  an explicit horizon. Include the investment that changes future production.
- Draft: maximize the value of the resulting legal draft and executable policy against the
  relevant opponent replies.
- Tactical adaptation: choose actions or missions that improve estimated continuation value
  under uncertainty about hidden state and opponent behavior.

Gold, damage, clear count and purchases are diagnostic quantities. They are not interchangeable
with winning. Carried gems and banked gems are different resources. A truncated run's missing
statistics are not zero production.

## 4. Architecture and method choices

| Layer | State/action abstraction | Initial method | What the result establishes |
| --- | --- | --- | --- |
| Native policy search | Legal conditional programs/compact policies over permitted observations | Evolutionary/program search; quality-diversity archive where useful | Empirically stronger executable policies in the covered distribution |
| Coupled configuration search | Legal draft, build, role and opening choices under a pinned controller | Mixed-variable model-based/evolutionary optimization | A conditional improvement within the exposed configuration space |
| Mechanics | Exact state plus one legal action/event | Native execution and differential tests | Transition fidelity on covered cases |
| Local clearing | Positions, resources, cooldowns and ordered actions through capture | Dynamic programming, A* or branch and bound | An optimum/bound for the specified finite problem |
| Cooperative clearing | Actual participant arrivals, aggro, action order and capture | Joint search on small groups | Feasible cooperation options and their costs |
| Team economy | Travel/clear/capture/shop/recovery/bank jobs with conditional costs | CP-SAT and rolling-horizon replanning | A schedule feasible/optimal within its abstraction |
| Draft | Legal picks/order, map, side, opponent and controller versions | Enumeration and minimax with evaluated leaves | A conditional draft policy, with evaluator uncertainty |
| Contested play | Observation history, beliefs and variable-duration missions | Hierarchical RL; test MAPPO as a baseline | Empirical adaptation against the training/evaluation population |
| Deployment | Allowed features and compact actions | Imitation learning, distillation, bounded lookup/search | A measured, executable approximation to the teacher |

For hidden information, belief-based search such as POMCP is an alternative to evaluate once a
usable generative model and opponent assumptions exist. Plain full-state MCTS is not an
information-safe policy for this game. Beam search or MCTS can provide approximate teachers
when exact search is too expensive; their best result is not an optimality certificate.

The intended learning loop is:

```text
native cases -> search/solver -> feasible plans and values -> compact policy
                   ^                                      |
                   |------ difficult states from games <--|
```

## 5. Work packages and acceptance criteria

These packages are an implementation reference for whichever options are selected. They are
not a mandatory linear research program. C1/C2 can use native complete-game evaluation without
a cloned engine state or predictive transition model. C3/C4/C9 require the relevant planning
interfaces and model qualification. C5 needs an efficient learning environment, not necessarily
a learned world model. C6 starts with dataset/observation qualification. Existing bot work continues
independently of these dependencies.

C12 supplies native ordered-prefix search from actual replay boundaries for local action and
continuation questions. It does not supply arbitrary state restoration or automatically qualify
the broader C3/C4/C8/C9 models, a rolling-horizon controller or a playable information-safe policy.

### W0. Reuse inventory and establish the experimental substrate

**Work:** inventory existing engine access, controlled cases, replay tooling, model assumptions
and frozen baselines. Consult the current [main research checkpoint](../main/README.md): it
already has a frozen league, engine-backed model laboratory and failure-attribution work.
Reuse pinned available outputs through read-only interfaces rather than duplicate those jobs.
Their existence does not establish broad calibration or a branch/restore API.

Determine experimentally:

- Can the exact gameplay engine be stepped from a controlled initial state?
- Can a state be cloned/restored, including timers, actor order and all relevant hidden state?
- If not, can deterministic action-prefix replay reconstruct it at an acceptable cost?
- What are setup latency, transitions/second, branching throughput and memory per worker?
- What information is available to each champion, and what communication is actually permitted?

The existing foundation `tools/mechanics/engine.mjs` is a map-editor bridge. Its presence alone
does not answer the gameplay stepping or restore questions. Byte-copying WASM memory alone
must not be assumed to restore host-managed state.

**Deliverable:** a minimal reproducible controlled gameplay case, a measured capability table
and a versioned data interface in the existing experiment index. Include unavailable operations.

**Acceptance:** replaying the same initial state/action prefix reproduces relevant native state;
independent branches agree on their common prefix and do not contaminate one another. No claim
of search readiness until the actual branching/reconstruction mechanism is demonstrated.

**Decision:** choose the substrate from measured search throughput and fidelity. An engine-backed
oracle can remain authoritative while a faster partial model is validated for a narrower domain.

### W1. Validate action transitions and observation boundaries

**Work:** cover movement/basic sharing, one successful cast across slots, resets, movement-exclusive
spells, legal target masks, vision after movement, damage/shields, death, CC, owner-end ticks,
camp action/sleep/aggro, capture, rewards, banking and recovery. Add passives and items as the
covered planning domain needs them. Preserve actual floating-point and event-order behavior.

Each reusable case contains pinned engine/SDK/model/source identities, initial-state provenance,
ordered actions, predicted state, observed native state and an explicit comparison tolerance.
Use exact comparisons where the engine semantics permit them; justify any approximation.

Separate four outcomes: invalid action generation, transition prediction error, different policy
choices, and insufficient observation. A scripted policy choosing another action is not by
itself evidence that a transition model is wrong.

**Deliverable:** a machine-readable transition corpus with held-out cases and tests that reject
deliberately bad predictions. Reuse existing verified cases and add coverage where needed.

**Acceptance:** the declared W2 domain has no unexplained transition mismatch; a held-out report
states coverage and blind spots. This is domain qualification, not a universal engine proof.

### W2. Solve local clearing and retain continuation frontiers

**Work:** begin with one champion and a small camp group. Search legal ordered actions through
the completed capture, including entry position, camp wake/aggro, cast/basic/reset order and
the departure needed to permit future respawn. Expand to partial-health camps, terrain,
different items/health/cooldowns, then small cooperative groups.

State keys must distinguish action allowances and phase/order, not just position and round.
Include a wait/end action where legal. Only use pruning or symmetry reductions justified by
the modeled rules. Seed search with the current executor's feasible plan when available.

Retain nondominated outcomes in elapsed time, remaining health/shield, cooldowns, location and
resources. Extend the endpoint to recovery or the next job when that changes the ranking.
Avoid a guessed universal conversion between health and time.

**Deliverable:** executable action traces, feasible outcome frontiers, search status and bounds,
plus comparisons with the current executor on identical initial states. For a declared scalar
cost, report `baseline cost - optimal cost`; for incomplete search, report the corresponding
bound interval. Do not report scalar regret for an unspecified multi-objective preference.

**Acceptance:** best plans replay successfully in the native engine; exact claims have an
exhaustion argument or matching bounds; approximate results are labeled. Held-out state
families test transfer beyond the cases used to build the search/model.

**Questions answered:** actual clear-time headroom, safe solo versus group work, item breakpoints,
ability conservation, tank/priority choices, and the cost of finishing in a bad state.

### W3. Optimize complete team clearing and investment cycles

**Work:** represent camp visits and service trips as jobs with alternative modes from W2.
Use CP-SAT initially for assignment, precedence and resource conflicts. Apply the first part of
a plan, observe the result and replan; compare with simple greedy and persistent-job controls.

The formulation must represent:

- A champion's travel and actual availability before it contributes damage or capture.
- Joint modes with specific participants, arrival times and tanking assumptions.
- Clear-to-capture-to-reward-to-bank precedence; extra capturers do not automatically accelerate
  capture, and continued spawn occupancy can delay respawn.
- Known availability versus predicted eligibility; unavailable/hidden state remains uncertain.
- Gold availability before purchase, actual shop permissions and recovery/travel costs.
- Equipment and passives changing future job costs and income.
- The difference between carried rewards, realized banked score and recoverable/lost resources.

Fixed-duration jobs are an approximation: define the health, items and entry-state region for
each mode. Reject or replan when execution leaves that region. Include uncertainty ranges or
scenario-based costs where justified; do not let the solver exploit an optimistic cost table.

**Deliverable:** an executable uncontested team plan with score/income/state curves, followed
by a rolling-horizon controller tested against disturbance and an opponent. Retain the gap
between model-predicted performance and native execution.

**Acceptance:** schedules respect physical constraints and replay as predicted within declared
bounds; they improve the stated economic objective on held-out cases. Competitive strength is
a separate complete-game result. An omniscient uncontested plan is a reference bound under its
assumptions, not evidence that a playable policy can achieve it against opposition.

### W4. Optimize drafting jointly with controller capability

**Work:** enumerate legal rosters and draft sequences using current runtime rules. Preserve pick
order when it affects the resulting game. Evaluate leaves as a function of map, side, opposing
roster/strategy and exact controller versions, not an intrinsic champion score.

Use W2/W3 outputs to screen capabilities and diagnose weaknesses, then complete native games
for competitive value. Allocate extra evaluations to uncertain competitive candidates and
interactions; independently repeated deterministic keys do not add samples.

Start with fixed opponent responses, then minimax over legal replies using evaluated leaves.
If the candidate population exhibits cycles, analyze its empirical payoff matrix and mixtures.
A restricted-population equilibrium is not an equilibrium of the entire game.

**Deliverable:** a conditional draft policy, matchup/map breakdown, uncertainty estimates and
explanations of role/synergy requirements. Retain controller version in every roster result.

**Acceptance:** the selected draft policy transfers to held-out opponents/maps and does not
rely on unavailable information. Revisit rankings after material controller improvements.
If execution is demonstrably limiting a champion, improve or bound that execution before
declaring the champion dominated.

### W5. Learn adaptation under partial observation and opposition

**Work:** compare practical action abstractions instead of assuming a whole-game primitive
policy or a mission policy is best. Variable-duration actions such as clear, capture, bank,
invest, defend, scout and disengage are one candidate, with explicit completion, interruption
and failure conditions. Their durations make that formulation hierarchical/semi-Markov.
Primitive or local action-bundle learning is another candidate where its measured gap justifies it.

Test a recurrent MAPPO implementation as a baseline, alongside the best nonlearned planner.
Centralized training may use privileged state in the critic. Actors must receive only each
champion's legal observation history and permitted shared-memory messages, including timing.
An omniscient teacher is an upper bound unless its labels can be conditioned on actor-visible
information; do not silently train an actor to imitate inaccessible knowledge.

Train against an archived population and diverse fixed opponents. PSRO-style best responses to
opponent mixtures are a candidate population update. Keep independent opponents and validation
outside policy selection. Opponent models and belief sampling are hypotheses to validate.

Use engine-defined terminal outcomes as the competitive target. If dense reward shaping is
needed, document what objective it preserves; arbitrary rewards for damage, kills, gold or
clears can create a different game. Compare with an unshaped control where practical.

**Deliverable:** a learned policy and population checkpoint, observation/action specification,
learning curves, failure families and held-out complete-game evaluation.

**Acceptance:** measured improvement over planner/imitation controls at comparable evaluation
coverage, with legal observations and no concealed opponent-family collapse. Simulator-only
improvement does not qualify an engine-deployed policy.

### W6. Distill and integrate useful discoveries

**Work:** train compact policies/value functions from search and solver output. Use interactive
dataset aggregation when the learner visits states missing from its teacher's demonstrations.
Feed high-regret or poorly predicted states back into search; let useful learned proposals
guide later search without removing legality checks.

Evaluate small networks, decision trees, tables and bounded candidate search by decision quality
and actual WASM fuel. Offline solver cost and deployed inference cost are different concerns.
Count SDK queries, observation construction, communication, navigation, logging and fallback work.

Possible distinct derivations, for the coordinator to choose rather than new assignments:

- Timetable: executable clearing schedules and route/mission continuation.
- Compound: productive investment, equipment breakpoints and economic/tactical continuation value.
- Warband: legal tactical action bundles, control, coordination and denial.

**Deliverable:** one useful behavior integrated behind a default-off switch, with focused tests
reaching the real implementation and a feasible fallback when inputs leave its coverage.

**Acceptance:** OFF preserves prior gameplay; ON is fuel-safe and improves the intended local
behavior in real execution. Complete-game qualification follows W7. Exact module equality is
a convenient OFF check, not an independent research objective.

### W7. Qualify strength and update the foundation

Use `arena` and the [workflow](../../docs/workflow.md). Freeze subject/opponent executables,
map contents, side, engine, parameters and any meaningful perturbation. Compare identical
keys with a same-run baseline, both sides and all pool maps. Report paired flips, margin
changes, uncertainty and strategy/map-family coverage.

Maintain three separate claims: transition correctness, subproblem planning quality and complete
competitive strength. A successful conditional probe does not establish ordinary applicability;
a lower model cost does not establish native gain; a local win does not establish broad strength.

The existing pool and legacy maps have been used repeatedly. Do not describe them as unseen
merely because a new experiment labels them confirmation. Reserve genuinely new scenario/map
families where supported, new opponents and untouched evaluation sets before tuning. Split
entire scenario families or game trajectories, not adjacent frames from one replay.

Preserve raw results and report opponent faults separately. Any sensitivity analysis excluding
a contaminated paired key removes it from both arms and remains secondary to the raw report.
Faults, exhaustion, error lines and stalls in our bot block release. Full execution and health
are required; builds and truncated cases do not prove them.

**Deliverable:** a reproducible qualification report and short code-local decision. Accepted
game knowledge goes into the existing [canonical foundation](../main/foundation/README.md)
with engine/source/coverage qualifications; bot-specific choices remain in the bot's notes.
Local qualification grants no online authority or claim of external superiority.

## 6. From local preparation to three long-running workers

### 6.1 Local readiness checks

Use existing measurements where possible. These are short engineering checks on the main
machine, not the desired multi-day remote workload:

| Check | Why it changes option selection |
| --- | --- |
| Native complete-game throughput, startup/build cost and memory | Determines practical C1/C2/C7 candidate-evaluation capacity |
| Controlled stepping/reconstruction, state fidelity and branch cost | Determines whether C3/C4/C9 teachers are feasible |
| Policy representation and candidate injection/build cost | Determines whether C1 explores useful new behavior economically |
| Expert replay versions, actions, visibility and label coverage | Can move C6 ahead of more expensive from-scratch learning |
| Small learner rollout/update throughput on the actual hardware | Determines whether C5 is competitive with CPU black-box search |
| Exact CPU/APU/OS, usable RAM, runtime support and available storage | Sets per-worker process count and identifies any usable accelerator |

Benchmark sustained throughput, not only a short boost-clock result. APU memory is shared with
the rest of the system; leave measured host/accelerator headroom. No task should depend on more
than one worker's available memory unless it explicitly implements a distributed algorithm.

The first deliverable is an honest shortlist with measured bottlenecks, a portable executable
worker packet and one candidate-to-result-to-restart round trip. Do not claim a campaign is
ready from a paper recommendation or an agent's ability to describe the algorithm.

### 6.2 Provisional first remote wave

After those checks, the following is a starting allocation, not a permanent reservation:

| Extra system | Initial candidate campaign | Result to bring back |
| --- | --- | --- |
| A | C2 coupled draft/build/role/opening optimization | Best conditional candidates, native results and controller-relative limits |
| B | C1 automated controller/program search | Deployable policy candidates, validation curves and failure families |
| C | C4 hard scenario-family atlas, once its engine/model interface is ready | Feasible plans/frontiers, bounds and compact policy candidates |

If C4 is not ready, system C takes another useful C1/C2 partition or a qualified C6/C5 pilot;
it does not spend days waiting for an interface or running a trivial fixture repeatedly. If
measurements favor imitation or RL, change the allocation. Independent search populations and
case partitions are easy to distribute across these systems without tightly coupled memory.

For C2, partition by legal configuration prefixes, context families or independent optimizer
runs while keeping held-out evaluation separate. For C1, run independent populations/initial
seeds with occasional validated elite exchange. For C4, partition complete scenario families
or explicit search prefixes. If the same candidate is evaluated on the same deterministic
game key, reuse the result with identity checks rather than counting it again.

The intended duration is hours to days of useful search, learning or state-space coverage.
Actual duration follows measured workload. A campaign that completes quickly should return its
answer; a plateau should prompt reallocation or a better search space, not padded runtime.
No prediction of a particular number of simulations or a strength gain is made before profiling.

### 6.3 Worker protocol and unattended operation

Prefer a small packet runner over a cluster platform built for its own sake. Each packet pins:

- Campaign/instance IDs; engine, SDK, executable, dataset and model hashes.
- Search representation, algorithm/version, objective and random-seed semantics.
- Initial state or game-key/context family; training/validation/holdout role.
- Resource requirements, compatible runtime/OS and output/checkpoint schema.

A worker evaluates packets without an LLM or human in the candidate loop. It emits improving
incumbents/archives, relevant curves, failures and exact reproduction inputs. Results merge by
identity and content; duplicate transport must not create duplicate evidence. Large search
state stays local to its worker where practical. Use independent result files or a proper
coordinator/backend; do not assume a shared SQLite file is a safe multi-machine job database.

Provide crash recovery, atomic result writes and resource-aware process/thread counts. Respect
each host's slot pool and avoid oversubscription from running many multi-threaded solvers at
once. More processes are useful only when measured throughput rises within RAM limits.
Transport can be a private-network queue or offline packet/result exchange; access, credentials
and connectivity are not assumed or configured by this planning request.

Checkpoint semantics are algorithm-specific:

- Custom search: save a valid frontier/transposition state or restartable prefix partitions.
- Evolution/program search: save populations, archive, fitness cache and random state.
- RL/imitation: save policy, optimizer, normalization, dataset/split identity and training state;
  describe what happens to unfinished episodes on restart.
- CP-SAT/other packaged solvers: preserve incumbents, model identity and bounds. Verify actual
  resume support; saving a solution hint generally provides a warm restart, not a serialized
  continuation of every internal search thread.

For minimization, keep feasible upper bounds separate from proven lower bounds. Global bounds
from partitioned search require complete coverage of the stated partition space and correct
bound aggregation. Random restarts, beam search and population fitness alone do not prove an
optimum or provide a certified global gap.

One owner writes each bot/shared module. Program implementation and integration remain with
their owners while the extra systems perform numerical search/training. Local games remain
unlimited; this protocol introduces no launch budgets, charges or approval records.

### 6.4 Integration boundary

Return artifacts that a local agent can use directly: candidate WASM/source or a bounded policy
description, configuration/draft policies, value tables, feasible plans or native counterexamples.
Preserve enough inputs to replay them. Search-generated code should be represented through a
bounded, tested evaluator/compiler path, with measured legality and fuel under native execution.

Re-evaluate frozen finalists locally through arena before adopting them. The training evaluator
and deployed bot must agree on observations, allowed actions and semantics. A stronger deployable
component is a successful result even without a new explanatory theory. W7 qualification still
separates component improvement from whole-game competitive gain.

## 7. Measure EV empirically and reallocate

For each candidate program, publish quality-versus-wall-time curves and total setup/integration
effort alongside throughput and memory. A short feasibility check can reject an implementation;
the substantive campaigns must explore the genuinely hard state/policy/configuration space.
Do not infer that all methods plateau from one implementation's poor learning curve.

Compare outputs on relevant held-out native tasks and complete games. Useful measures include
better task success, faster safe production, robust response to opponents, reduced deployed
regret, stronger conditional drafts and additional useful decisions within fuel. Human insight
is not a required metric, and no one illustrative subsystem is the required objective.

Track in existing experiment indexes:

| Dimension | Measurement |
| --- | --- |
| Capability | Held-out improvement over the executable reference; breadth of affected situations |
| Search | Candidate diversity, feasible-plan quality, certified bounds where available |
| Efficiency | Useful improvement versus machine-hours, sustained throughput, peak RAM and implementation effort |
| Fidelity | Native prediction/execution errors where the method relies on a model |
| Economy/draft | Equal-time production/banking curves, conditional value and controller dependence |
| Learning | Transfer to new states/opponents and performance under the learned policy's state distribution |
| Deployment | Full-turn fuel, faults/errors/exhaustion/stalls and fallback behavior |
| Competition | Paired complete-game outcomes and strategy-family weaknesses |

Use training cases for adaptive selection and retain a separate frozen qualification set. Many
candidate evaluations make winner's curse and leakage serious risks. Predeclare qualification
checkpoints and report uncertainty; repeated deterministic keys and related policies do not
become independent samples because they ran on different computers. Do not permanently blacklist
a different method because a narrow proxy, faulty implementation or weak policy representation
lost an earlier experiment.

Reallocate toward programs with credible transfer and productive progress, and away from broken
evaluation, memory thrashing, saturated representations or gains confined to a training set.
Unexpectedly valuable C6/C5/C3 results can replace the initial shortlist. Keep sufficient local
capacity for active bot development; these three additional systems extend it rather than
turn every current experiment into a prerequisite for this research program.

## 8. Locations, ownership and research record

This is the suite's long-lived optimization plan. Update it as the program learns; keep
decisions in existing suite/bot notes rather than creating per-task planning documents.

- `bots/contenders/<bot>/`: owned implementations and focused tests.
- `bots/contenders/tools/optimization/`: implemented suite-owned C++ engines, shared models,
  native adapters, launchers, focused tests and portable packet tooling. Start with its
  [user guide](tools/optimization/README.md) and [engine registry](tools/optimization/engines.json).
- `bots/main/foundation/`: canonical game knowledge and its owned analysis tools; bots do not
  include code from this cross-suite knowledge bank.
- `.cache/contenders/optimization/`: regenerable search trees, builds and training scratch.
- `results/contenders/optimization/`: durable case definitions, native witnesses,
  feasible plans, model/data identities, bounds and evaluation indexes.

The implementation is an unattended program, not an agent-driven loop. Each invocation has a
finite work/time limit, writes a report and exports its useful product. Resume retains the
search/RNG state and refuses incompatible inputs. Worker packets allow three independent local
campaigns without an agent session or online access. The runtime does not install a service or
start jobs on other computers. Preserve sufficient durable inputs and native witnesses to
reproduce a result after cache pruning, not only hashes pointing into cache.

Respect [AGENTS.md](../../AGENTS.md): no online/server commands or credentials, no edits to
frozen anchors/SDK/cmake, no integrator-owned tooling/map changes, and no irreversible deletion.
Request needed interfaces from their owners instead of changing those paths. Use arena snapshots
for versions; retire superseded bots according to the user's direction and repository rules.

## 9. Method references

These motivate the proposed methods; their published results do not establish suitability or
performance in this game.

- [Expert Iteration: Thinking Fast and Slow with Deep Learning and Tree Search](https://arxiv.org/abs/1705.08439)
  separates stronger search from a policy that generalizes its plans.
- [OR-Tools job-shop scheduling](https://developers.google.com/optimization/scheduling/job_shop)
  supplies precedence/resource-constraint modeling patterns for the scheduling abstraction.
- [DAgger: A Reduction of Imitation Learning and Structured Prediction to No-Regret Online Learning](https://proceedings.mlr.press/v15/ross11a.html)
  addresses the state-distribution shift induced by the learned policy's own actions.
- [MAPPO: The Surprising Effectiveness of PPO in Cooperative, Multi-Agent Games](https://arxiv.org/abs/2103.01955)
  is a candidate cooperative learning baseline.
- [PSRO: A Unified Game-Theoretic Approach to Multiagent Reinforcement Learning](https://arxiv.org/abs/1711.00832)
  motivates responses to opponent mixtures and empirical population analysis.
- [POMCP: Monte-Carlo Planning in Large POMDPs](https://papers.nips.cc/paper_files/paper/2010/file/edfbe1afcf9246bb0d40eb4d8027d90f-Paper.pdf)
  motivates belief-based planning with a generative model where its assumptions hold.
- [Policy Invariance Under Reward Transformations](https://people.eecs.berkeley.edu/~russell/papers/icml99-shaping.pdf)
  gives conditions for reward shaping that preserves an MDP's optimal policy; those conditions
  must be checked for our formulation rather than assumed for arbitrary dense rewards.
- [Sequential Model-Based Optimization for General Algorithm Configuration](https://www.cs.ubc.ca/~hoos/Publ/HutEtAl11.pdf)
  motivates C2's mixed/conditional configuration search; it does not establish that the exposed
  choices contain a strong policy.
- [The CMA Evolution Strategy](https://cma-es.github.io/index.html)
  describes a continuous black-box method; categorical program/draft spaces need an appropriate
  representation or a different search algorithm.
- [Evolution Strategies as a Scalable Alternative to Reinforcement Learning](https://arxiv.org/abs/1703.03864)
  supports considering parallel CPU black-box policy optimization. Its large-cluster results
  are not throughput or strength predictions for three APUs.
- [Illuminating Search Spaces by Mapping Elites](https://arxiv.org/abs/1504.04909)
  motivates retaining diverse strong behaviors; descriptor diversity alone is not playing strength.
- [Programmatically Interpretable Reinforcement Learning](https://proceedings.mlr.press/v80/verma18a.html)
  demonstrates a programmatic policy approach; C1's exact synthesis/evolution method remains open.
- [CP-SAT model and response schema](https://github.com/google/or-tools/blob/stable/ortools/sat/cp_model.proto)
  distinguishes feasible objective values from proven objective bounds for the modeled problem.
- [Optuna storage/parallelization FAQ](https://optuna.readthedocs.io/en/latest/faq.html)
  documents multi-process/multi-node storage considerations. A multi-machine study does not
  require adopting Optuna, and a local shared SQLite file is not a general worker coordinator.
