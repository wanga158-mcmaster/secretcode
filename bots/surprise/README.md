# Surprise: keep the snowball rolling

One private tournament bot, developed locally until the team explicitly decides to reveal it.
Its objective is relentless economic compounding: turn resources into useful power, power into
access and productive time, and those advantages into more resources and a secured win.

## Doctrine

Maximize the rate and reliability of that compounding process in the actual position. Greed
means taking the most valuable available opportunity, including creating better opportunities.
It does not mean always farming, holding the largest bag, or always fighting. Pressure that
profitably denies the opponent's recovery is part of the economy. Pressure that gives up more
than it earns breaks the snowball. Recovery, information and banking can preserve or accelerate
it. Gold and carried gems are intermediate state; the game's winning condition still governs.

Aggressive and defensive play are not neutral styles to choose between. Actions should follow
from the position, including our lead, reachable resources, purchases, timings, opponent
responses, uncertainty and the consequences of being wrong. A lead should compound rather
than buy inactivity; an apparent opportunity should not justify a losing commitment.

This is a submission-strength research track, outside the diversity/admission objectives of
the development population. It may change its draft, model, organization and execution as the
evidence demands. There is no fixed menu of strategies or permanent evaluation taxonomy.

Derive the strategy independently from the current game mechanics and maps. Main and the
existing foundation are useful references and benchmarks; their policy, draft preferences,
purchase orders, greedy dispatcher and strategic architecture are not the starting design.
The initial main-derived source copy below is a retained infrastructure reference fixture,
not the implementation from which to develop the surprise strategy. Replace it with an
independent implementation when strategy work begins.

## Bot and ownership

| Bot | State |
| --- | --- |
| `surprise/_greed` | Reference fixture only: self-contained main-v39 copy with LF line endings, retained for the infrastructure checks. The independent surprise strategy is not implemented yet. |

One owner holds this suite, including `_greed/`, `tools/`, `tests/`, `.cache/surprise/` and
`results/surprise/`. Read other suites' evidence freely; do not edit their code. Research ideas
can be reimplemented here. There are no source includes into another suite. The bot's living
state and decisions are in [_greed/NOTES.md](_greed/NOTES.md).

## Keeping the surprise

The leading underscore is deliberate: arena bulk selectors, the routine ladder and population
orchestration skip this bot. Use its exact ID. Keep helper opponents private in the same way.
The underscore prevents accidental selection; it is not access control for local files.

Develop and test locally. Do not submit, upload, run online games, copy improvements into the
live bot, or release the surprise without a fresh explicit instruction from the team. A
qualifier is the intended occasion, not automatic authority to reveal it on a date. Main's
online grant does not transfer here. Local results never append to the shared ladder.

## Working loop

Questions may come from the mechanics, solved subproblems, computed economic plans, or actual
games. State the mechanism, what could disprove it, and the evidence needed. Ask better questions
as understanding improves; neither these notes nor the experiment runner prescribe what the
next question must be. Use the extended suite's existing strong opponents. Main already runs
controlled experiments; duplicating that work or building another adversarial policy search is
not this track's current compute priority.

The initial offline-compute direction is search over physically feasible economic trajectories:
composition, purchases, movement, cooperation, reward ownership, recovery and conversion, with
resource access and enemy arrival constraints varied explicitly. Derive candidate actions from
mechanics rather than a fixed grammar of favored strategies. Make every abstraction and search
restriction visible; widen it when it excludes a useful possibility. This direction remains a
research question, not a claim that an optimizer or an independent game model already exists.

Initial questions include map-dependent composition and production routes, joint clear and
capture work, reinvestment, and when a champion creates more value by interrupting the enemy
than by producing directly. These are seeds for investigation, not separate evaluation tracks.
Opponent response must be part of the reasoning: a route can stop being good when contested,
and a profitable opening need not be a profitable commitment. Use only information available
to the bot at the decision; replay knowledge can diagnose a mistake but cannot become an oracle.

Use the existing [arena workflow](../../docs/workflow.md). New behavior starts behind a knob
that defaults off, with a real unchanged control. Use both sides and matched game keys; all
pool maps and fresh maps/opponents are required before a broad strength claim. Keep per-map,
per-opponent failures visible. Distinguish a useful model, a measured improvement and any claim
of optimality. A finite search or best observed route does not prove an optimum.

## Long-term development

Main covers short-term competitive work. Optimize this project for durable game knowledge,
reusable computation and an independent bot whose economic decisions improve as our
understanding grows. Early milestones do not require ladder gains, an imminent submission or
a bot as strong as main. Record poor results honestly without letting a short-term strength
target determine the architecture. The long-term competitive bar remains high.

Plan around uncertainties and capabilities, with evidence determining when to proceed. Choose
work that can change the design, invalidate a consequential assumption, or make many later
questions cheaper to answer. Prefer results that survive a different roster, map, opponent or
balance bundle over a narrow gain with no explanation. Main's code remains a reference fixture.

| Milestone | Durable capability | Evidence needed to proceed |
| --- | --- | --- |
| 1. Faithful, branchable mechanics | An independent representation of legal actions, state, turn phases, observations and resource ownership, with cheap branching for offline search | Native comparisons of the covered transitions, reproducible branch replay, measured throughput/memory, and explicit uncovered mechanics and abstractions |
| 2. Independent end-to-end proof | A bounded economic problem searched from mechanics, an action witness, and a small independently written C++ executor | The witness reaches its predicted physical outcome in the native engine using legal observations; execution fits measured fuel; no strategic dependency on main |
| 3. Reusable distributed computation | Search checkpoints, cached subproblems and workers that can run unattended on the available machines | Interrupted jobs recover; identical inputs reproduce; different inputs remain distinct; bundle/model/map identities accompany results; repeated exact work is reused |
| 4. Discovery of economic structure | Expanding local searches and joint team trajectories with composition, purchases, cooperation and conversion free to change | Executable plans, useful alternative endpoints, bounds where valid, and supported explanations of the limiting resources; search restrictions can be widened |
| 5. Deployment under uncertainty | Conditional continuations and an independent executor that adapt production to observable changes in access and timing | Plans remain useful with private vision, observation age, limited communication, failed actions and changed opportunities; adaptation preserves the economic objective and stays within measured runtime limits |
| 6. Competitive validation and continued improvement | A complete independent bot plus a research process that can diagnose and improve it | Full-pool, both-side evidence against existing strong, varied opposition; fresh held-out confirmation; clean health; model and execution failures distinguished from strategic limitations |

These are dependency milestones, not a waterfall or a fixed calendar. Prove a small complete
loop early, then expand the model, search and executor together. Do not spend months completing
a simulator or a library of tables before checking whether its output is executable and useful.
Correctness checks and small early prototypes serve the long-term design; they are not demands
for short-term competitive results.

The three extra machines should consume a common collection of reusable subproblems, rather
than receive permanent roles or independently repeat entire experiments. Expand coverage where
an answer can change the plan, deepen promising unresolved searches and reuse settled results.
Multi-day jobs need validated transitions, recoverable checkpoints and clearly stated questions
before scaling. No game budget or core reservation is introduced. Hardware/worker setup is
still unimplemented; this charter is not evidence of remote access or running jobs.

Keep the full winning objective in view. Early gold, a quick clear or a fast opening is a
diagnostic, not the project objective. Preserve endpoint position, health, personal inventory
and reward ownership, cooldowns and remaining access when comparing economic trajectories.
Information-perfect plans can provide research references; deployable decisions must use only
the legal observations and communication available to each actor.

Maintain the research questions and their current answers in this charter or the bot's notes.
Keep assumptions and evidence beside the implementation that uses them; do not create a
document for every task. Pin results to their mechanics/model/map dependencies, and revalidate
affected results when those inputs change. Reserve confirmation conditions before tuning on
them. Native verification of solver witnesses is necessary work, distinct from duplicating
main's existing strategy experiments. Reuse other owners' settled evidence with its limits.

Revise the approach when evidence exposes a structural problem:

- A simulated improvement that exploits a model error requires a model correction.
- A useful sequence excluded by the action abstraction requires reconsidering that abstraction.
- A plan that relies on unavailable information requires a different information contract.
- A plan that cannot fit the executor's fuel or communication requires redesigning the offline
  output and deployment together.
- Deeper search that repeats settled answers should yield to a question that can change the
  architecture or the selected plan.

Choose the solver, abstraction, compression method and any use of ML through those tests;
none is a permanent commitment. The qualifier remains the intended reveal occasion, subject
to the team's explicit decision. Development milestones do not authorize online actions.

## Local infrastructure

`tools/lab.py` is a question-agnostic runner over arena's snapshots, games and paired reports.
`experiment.json` is the editable working recipe, initially just a no-change plumbing check.
It accepts arbitrary knob variants and exact local opponents, including opponent overrides.
It captures every source closure once per experiment, records map/bundle/source identities,
and refuses to resume a changed experiment in an old output directory. It imposes no strategy
classes, fixed metrics, game budget or search algorithm. Retained native games are available
for new analyses as questions change.

Use the Python printed by `arena.cmd env` (PATH Python is a Store stub). From the repository
root in PowerShell:

```powershell
$surprisePython = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
.\arena.cmd build surprise/_greed
.\arena.cmd test surprise/_greed
& $surprisePython -B bots/surprise/tools/lab.py plan
& $surprisePython -B bots/surprise/tools/lab.py run --out .cache/surprise/first-question
.\arena.cmd report .cache/surprise/first-question --by map
```

For a small infrastructure check add `--maps 00collision,01big_world` to `plan` or `run`.
Those are full games on a deliberately limited map set, not evidence about the whole pool.
An unchanged rerun with the same output directory reuses completed keys. Edit the one recipe
for the next question and choose a new output directory. Scratch and replays live under
`.cache/surprise/`; preserve decisions here or in the bot's notes and important evidence under
`results/surprise/`. `arena sweep`, `compare`, `map` and `gate` also work directly with the
exact bot ID. A local gate does not authorize a reveal.

Start map research from current `maps/geometry.json`, runtime settings and the SDK. The
[contenders foundation](../contenders/foundation/README.md) contains useful economic, combat
and map hypotheses, including offline route/schedule tools; much of that evidence is tied to
engine 1.0.2. Read it as prior research and recheck what matters on the current bundle. Derive
the independent search model from mechanics; do not inherit the reference scheduler's policy
assumptions as constraints on what the surprise bot can discover.

## Verification state

Infrastructure validation is recorded in `_greed/NOTES.md`. No composition, route, economic
advantage or tournament readiness is established by this setup.
