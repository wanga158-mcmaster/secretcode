# Long-running optimization engines

This is the executable implementation of [the compute portfolio](../../lt_plan.md).
One executable provides C1–C12. The search loops run in C++ without an agent or LLM.
Native game execution and state queries use small Python/Node adapters around the pinned local engine. Nothing
submits bots or contacts the game server.

## Start a campaign

From the repository root on Windows:

```powershell
.\bots\contenders\tools\optimization\run.cmd c2 -Hours 24 -Iterations 100000 -Out results/contenders/optimization/my-c2
```

The launcher uses the compiled `bin/optimizer.exe`, or builds it if it is missing.
It chooses the engine's example input from `engines.json`. Use `-Input PATH` to supply
another input. Without `-Out`, campaigns use `results/contenders/optimization/campaigns/cN`,
separate from the shipped verification evidence. `-Hours` is a time limit for this invocation; `-Iterations` is the total
search-work limit, including work saved in an earlier checkpoint. An engine that exhausts
its useful finite search space finishes early. Time stops are checked at safe work boundaries,
and owned external evaluations are bounded by the invocation's remaining time. An unfinished
evaluation is not scored. Process cleanup and atomic checkpoint/report writes can add shutdown
latency after the time limit.

Resume the same campaign with the same input and seed:

```powershell
.\bots\contenders\tools\optimization\run.cmd c2 -Hours 24 -Iterations 100000 -Out results/contenders/optimization/my-c2 -Resume
```

Ctrl+C requests a stop. Checkpoints also protect progress after an interrupted process;
unfinished native evaluations reuse completed, verified game-cache entries on restart.
Use a short Windows installation path such as `C:\battlecode` or
`F:\battlecode-optimizers`, and short campaign output paths. Deep directory trees can exceed
Windows path limits during verification or execution. Extract a fresh packet to its final
location before starting it.
Keep each worker's output directory separate. Do not put a live campaign database on a
network share.

The direct executable interface is also available:

```powershell
.\bots\contenders\tools\optimization\bin\optimizer.exe help
.\bots\contenders\tools\optimization\bin\optimizer.exe c4 --input bots/contenders/tools/optimization/examples/c4-continuation-family.json --out results/contenders/optimization/my-atlas --iterations 1000000 --seconds 86400
```

`--resume` requires matching program, inputs, seed and engine/evaluator identities.
Changing a search problem or replacing the executable requires a new output directory.
Increasing the work/time limits is supported. Native evaluation caches are keyed by actual
executable, toolchain, map and opponent contents; repeated deterministic games are reused.

## Choose an engine

| Mode | What runs | Useful output | Authority of the result |
| --- | --- | --- | --- |
| `c1` | Evolution of conditional decision-program structure and numeric leaves, using native full games | Best program JSON/C++ header, archive, reproduction request and validation results | Covered job-prioritization controller, not arbitrary whole-bot synthesis |
| `c2` | Coupled categorical/conditional draft, build, role and opening search with native racing | Configuration policy, overrides/header, native artifacts and matched results | Conditional on the frozen executable controller and evaluation population |
| `c3` | Event-based economy planning with joint modes, purchases, recovery, capture and banking | Feasible schedules, greedy comparison, score curves, service coverage and execution contract | Supplied cost/state domains; guarded native execution and rolling replanning remain missing |
| `c4` | Bounded clearing search with feasible prefix rollouts through capture and departure | Replayable plans, continuation frontiers and safe economy modes | Broad model atlas; selected actual Varg traces have entire-trace native acceptance |
| `c5` | CPU episodic policy-gradient training of a compact actor against native opponents | Actor JSON/C++ header, optimizer/population checkpoint and learning curves | Native high-level controller preferences; not a recurrent MAPPO implementation |
| `c6` | Native teacher-data collection, actor imitation and observed-return value learning | Dataset/split manifest, actor/value weights, deployment header and held-out error | Teacher/source and permitted-feature coverage; no invented counterfactual labels |
| `c7` | Automatic best-response search and empirical league/mixture optimization | Policy archive, payoff matrix, robust mixtures and failure families | Restricted empirical population, not an equilibrium of the whole game |
| `c8` | Balanced scenario and reactive-response search with resource/timer mutations | Replayable curricula, failure witnesses, unresolved retries and full native-validation requests | Restricted-model references; arbitrary native reconstruction and a frozen real-controller bridge remain missing |
| `c9` | Fair, resumable adversarial search across banking/denial families | Bounded JSON/C++ policies, feasible traces and explicit outcome bounds | Final 32 rounds, two actors, integer damage and full information; native endgame/deployment qualification remains missing |
| `c10` | Equivalent-kernel search, differential checking and actual WASM fuel measurement | Generated C++ kernel, correctness evidence and fuel measurements | Kernel scope; CPU timing is not reported as WASM fuel |
| `c11` | Native model falsification and parameter identification over controlled branches | Counterexamples, regression corpus, parameter fits and coverage report | Supported native reconstruction/action domain; unsupported cases fail closed |
| `c12` / `micro` | Native MAX/MIN search over ordered move, attack, cast, channel and end primitives | Conditional counterplay tree, action sequences and native farming endpoints | Actual retained equipped states, selected adversarial owners and bounded horizons; cutoff values are estimates |

The draft's W0/W1 substrate and transition work is provided by the shared native oracle,
restricted model, and `c11`. W2–W5 correspond to the clearing, economy, draft and learning
engines. Exports and imitation support W6. Native validation reports support W7, while
complete-game qualification and adoption remain explicit later decisions.

For the three additional systems, start with `c2`, `c1`, and `c4` in separate local copies.
This is a starting allocation, not a measured claim that those methods are best. `c5`,
`c7`, and `c11` provide alternative native workloads; `c3`, `c8`, and `c9` can run entirely
inside their declared models. Compare useful improvement and held-out results over time.
Do not make a completed small search run longer merely to consume a day.

The expanded model campaigns have useful but specific coverage:

- **C3:** eight economy cases exercise investment, joint work, recovery, travel and banking.
  Cost coverage is explicit, including maps with missing service modes. A model schedule is
  executable by its model replayer; native observation guards, macro execution and disturbance/
  opponent replanning remain essential gaps. See the [C3 index](../../../../results/contenders/optimization/c3/index.json).
- **C4:** the 32-case continuation family retains 216 outcomes and 208 safe all-participant economy
  modes. Six entire native traces across three actual Varg windows passed, including complete
  clear/capture/reward/departure. Broader parties, equipment and geometry remain model-only;
  the references are model greedy or explicitly scripted controls, not the current live bot.
  See the [C4 index](../../../../results/contenders/optimization/c4/index.json).
- **C8:** balanced eight-family campaigns and deeper retries produce feasible failure witnesses
  and explicit unresolved cases. These compare model references; they have no whole-trace native
  or frozen real-controller transfer qualification. See the [C8 index](../../../../results/contenders/optimization/c8/index.json).
- **C9:** an eight-stratum campaign explored 410,000 nodes, retaining proofs for three restricted
  cases and unresolved bounds for five. Case quanta prevent starvation; total transposition
  payload and input/frontier sizes are capped. Native occurrence frequency and an allowed-observation
  executor remain separate work. See the [C9 index](../../../../results/contenders/optimization/c9/index.json).

The shared local model matches 90 retained Varg/camp18 native traces containing 5,141 actions
in its declared observable projection. This covers solitary Varg, no items, one camp group and
early rounds; it does not qualify every model case above. Its observed corrections and blind
spots are in the [model index](../../../../results/contenders/optimization/sim/index.json).

The supplied worker profile makes that allocation explicit. On the first machine run:

```powershell
.\bots\contenders\tools\optimization\run.cmd -Portfolio examples/portfolio-three-workers.json -Worker A
```

Use `-Worker B` or `-Worker C` on the other machines, and add `-Resume` when restarting.
The profile supplies separate seeds and output paths; there is no shared coordinator process.
Each native build/game takes the existing arena slot only while that work runs.

## Native micro-control with C12

`c12` (alias `micro`) searches the actual pinned engine from retained replay input boundaries.
Natural positions, champion kits, equipment and cooldowns stay intact. It searches ordered
primitives rather than selecting a prewritten tactical macro. Selected friendly owners are
MAX nodes and selected opposing owners are MIN nodes; other owners follow the case's declared
background policy. The explored tree can therefore retain different actions after different
enemy replies.

Start the default four-context combat/farming campaign, or choose the wider 32-context input:

```powershell
.\bots\contenders\tools\optimization\run.cmd c12 -Input examples/micro-worlds.json -Hours 24 -Iterations 80000 -Out results/contenders/optimization/my-micro
.\bots\contenders\tools\optimization\run.cmd micro -Input examples/micro-worlds-wide.json -Hours 48 -Iterations 3200000 -Out results/contenders/optimization/my-wide-micro
```

`-Iterations` / `--iterations` counts evaluated native search prefixes cumulatively across cases,
not complete games or gameplay rounds. Inventory and root/baseline setup also take time.
The default profile allows 20,000 prefixes per case; the wide profile allows 100,000. A campaign
can finish early at its declared case/tree/resource limits. Resume with the same command and
`-Resume`; changing replay bytes, inputs or executable requires another output directory.

The inventory mines varied unchanged states from the declared replay intervals. Append actual
replay intervals or supply explicit cases to grow coverage; arbitrary champion/item JSON
restoration is not assumed. The replay files named in the input and the native runtime must
be available. The four and 32 contexts are caps on selected cases, not promises of distinct
maps or unseen opponents.

Open `campaign.md` and `case-comparisons.json` for per-case progress and baselines. Each case
exports `best-sequence.txt`, `counterplay-tree.json`, `policy.json`, `candidate-actions.csv`, `root-state.json` and
`frontier.json`; a completed chosen-prefix replay is retained as `native-trace.json` when time
permits. Farming completion requires actual native reward attribution and departure, retaining
health, cooldowns and position for continuation. The frontier contains feasible endpoints,
not a complete Pareto proof. Endpoint records share the counterplay tree's parent/action links
instead of duplicating every full prefix.

The search interleaves action classes so a large ground-target domain does not bury an attack
reset or the other ability slot. It also follows the independently replayed native control one
edge per batch before general widening (`search.seed_baseline`, default true). This seeds a
feasible route; it does not establish that the route survives different enemy replies. All
seeded prefixes count toward the same work limit and obey the same search bounds. Control and
search values are not comparable when their horizons or opponent continuations differ.

The world profiles enable `generation.prefix_reuse` to avoid replaying the same long prefix
for every sibling. The backend uses official native timeline Seek and Branch operations at
successful selected-owner end boundaries, checks the exposed state and native snapshot, and
retains the branch's payout history and personal vision. It recycles each native game after
16 branches and keeps at most 128 prefix records; a restoration mismatch falls back to a fresh
independent game. Set the option to `false` for fresh reconstruction throughout. Finite paired
fixture batches measured 1.4–3.8 times faster total execution with reuse, including setup;
this is not a universal speedup. Native WASM memory plateaued at 9.56 MiB across seven recycled
epochs in the fixture matrix, separately from Node and C++ memory. Eight champion kits and
capture, channel, dead-owner and combat traces passed exact fresh/reuse comparisons.

`search.max_state_memory_mb` defaults to 512 MiB. At safe batch boundaries the engine stops
with a resource-limited result when its conservative search-state estimate approaches that
amount. The estimate covers checkpoint JSON amplification; it is not a hard total RSS quota
and is reported separately from measured native worker memory.
Native action execution includes movement, basic attacks, both ability slots, ending turns,
recall, cancellation and gem submission. Ward placement, consumable use and shopping are not
part of the current proposal domain.

Every search result is conditional on the selected participants, action proposals, background
behavior and horizon. Backed cutoff utilities and local task completion do not certify a win
against unexplored replies or a complete-game win. Replan after an opponent deviation; these
observed-state analysis trees have no information-safe deployment or full-turn fuel certificate.

## Read the result

Every campaign writes these common files in its output directory:

- `report.md` and `report.json`: result, scope and status of the latest committed algorithm work.
- `run-status.json` and `run-status.md`: latest invocation outcome, including interruption,
  deadline or failure; consult these when an earlier algorithm report still exists.
- `checkpoint.json`: committed engine state, random state, iteration and identity for restart.
- `progress.csv` and `events.jsonl`: quality/coverage over elapsed compute time.
- Engine-specific policies, plans, frontiers, kernels or counterexamples listed in the report.

A first interruption can occur before an engine has committed a checkpoint. The runtime writes
an invocation status/report without inventing resumable search state. Check the reported
checkpoint availability; a saved earlier checkpoint remains authoritative after a later failure.

The [implementation and evidence index](../../../../results/contenders/optimization/index.json)
records exact program identities, verification coverage and retained native witnesses.

An incumbent found before search completion can be useful. A feasible plan is not an optimal
plan; a model optimum is not native-engine validation; a native local improvement is not
complete-game strength. Read the reported bounds, health and coverage. A run may correctly
return that no better candidate was found or that its teacher/model is inadequate.

For C6, an exported compact actor is copied exactly when its weights are available; the
engine then spends work on observed-return value fitting and independent comparisons.
Set `refit_actor` to request actor fitting too. An opaque teacher dataset uses imitation.
The supplied reference teacher is a baseline, not a claimed strong expert. C1 decision trees
and C5 macro actors have different action semantics; C1 outputs cannot become C6 labels by
renaming their leaves.

Native candidates retain reproduction inputs and selected executable/source artifacts.
Search-generated candidates are kept separate from the three active contenders. No engine
changes an active bot's default or promotes a candidate automatically.

Inspect a saved campaign without changing it, or replay a finalist's exported native
full-game evaluator request:

```powershell
.\bots\contenders\tools\optimization\run.cmd status -Out results/contenders/optimization/my-c2
.\bots\contenders\tools\optimization\run.cmd evaluate -Input results/contenders/optimization/my-c2/best/reproduce.json -Out results/contenders/optimization/replay-c2
```

Use the reproduction-request filename listed in that engine's report. The evaluator verifies
identities and health on replay; cached deterministic games do not add independent evidence.

## Qualify a frozen full-game candidate

The `qualify` utility runs a frozen exported `config`, `program` or `policy` request against its
baseline on identical map/side/opponent keys. Copy `examples/qualify-native.json` to your chosen
input location and replace its neutral inline `request` with a `request_file` pointing to the
candidate's exported full-game evaluator request. Declare stage maps, opponents and both sides:

```powershell
.\bots\contenders\tools\optimization\run.cmd qualify -Input path/to/qualification.json -Hours 24 -Iterations 100000 -Out results/contenders/optimization/my-qualification
```

Here iterations count cumulative paired game keys. Resume the same frozen request and stages
with `-Resume`. The shipped neutral example should demonstrate no gain. Known pool and legacy
maps are historically used; naming a stage "confirmation" does not create unseen holdouts.
Local atlas/micro traces need explicit controller integration before this full-game interface
can evaluate them.

Inspect `decision.json`, `paired-games.csv`, `case-plan.json`, `candidate-request.json` and the
retained evidence listed in `artifacts.json`. Raw health problems block qualification; removing
contaminated pairs is only a paired sensitivity analysis. The utility reports map-cluster
uncertainty conditional on the fixed opponents and can return incomplete, no demonstrated gain,
or positive but uncertain. A supported gain requires complete requested coverage, clean health
and sufficient uncertainty support; it does not promote a bot or authorize an online action.
The utility's bookkeeping/resume tests use controlled fixtures; they do not supply a native
candidate qualification result.

## Offline worker packets

The packet tool creates a new directory and records content hashes:

```powershell
.\bots\contenders\tools\optimization\packet.cmd create --kind model --out .cache/contenders/optimization/model-packet
```

Use `--kind native` to include the pinned native adapters, local CLI/engine assets,
WASI toolchain, Python/Node runtimes and required bot/map inputs. Use the packet's root
`run.cmd` on the other Windows computer. The exporter does not copy account settings,
credentials, online state or previous bulk experiment caches. Native packets are larger
because they include the local build/runtime tools. Run `packet.cmd verify DIRECTORY`
before using a transported packet.

Packets are frozen snapshots. Previously exported C1–C11 packets keep their original executable,
schemas and evidence scope; they do not acquire C12 or later fixes. Prepare a separate current
native packet with its required replay inputs for C12. Do not patch a running packet in place.

The model executable is statically linked and does not require MSYS2 or Python to run.
Portable C++20 sources and `build.sh` are supplied for other platforms; native-game
adapter compatibility must be verified on the destination platform. Current native
packets target the Windows tool bundle used by this repository.

## Build and extend

```powershell
.\bots\contenders\tools\optimization\build.cmd
```

`build.ps1 -Compiler PATH -Output PATH` supports another GCC-compatible C++20 compiler/output.
The pinned WASI compiler builds game bots, not this host executable. The local tested host
compiler is MSYS2 UCRT64 `g++`; the default Windows build links its runtimes statically.

Each engine owns its source file and focused tests. The common runtime owns strict JSON,
checkpoint identities, random-state persistence, locking, progress and contained child
processes. Adapters own engine I/O, not search decisions. Keep new game knowledge in the
canonical foundation and preserve the distinction between a prediction and an observation.
