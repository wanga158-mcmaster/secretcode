# Improving bots

The same loop works for every bot in every suite. The suite charter (`bots/<suite>/README.md`)
says *what* to explore; this page says *how* to make and prove progress, alone or with many
agents in parallel. Rules that bind everyone are in [AGENTS.md](../AGENTS.md). Every experiment
runs through one protocol ([section 2](#2-experiments)); every agent keeps the repo lean
([section 11](#11-checkups)).

## 1. The loop
1. **Orient.** Read the suite charter and the bot's state (its README or decisions file). Health
   comes first: a bot whose games show fuel exhaustion, faults, error lines or stalls loses to its
   own bugs; fix those before any strategy work.
2. **Diagnose from games.** Run the matchups you lose as an experiment, read the summary
   (`arena exp show NAME --by map`), and regenerate the games you need to look at
   (`arena exp replay NAME MAP SIDE SUBJECT OPPONENT`; then `arena report game REPLAY --positions`
   or `tools/view-replay-local.py`). Name a mechanism you saw: the big gains so far all came from
   mechanisms seen in games.
3. **Implement behind a knob** that defaults off (`inline constexpr` in the bot's config, or
   `#define`). With the knob off the bot must replay the old games bit for bit (an experiment with
   a `same:` subject and the knob-off build shows identical rows). Unit-test decision logic in
   `tests/run.py`.
4. **Measure** with an experiment: the baseline and the variants as subjects, on identical games.
5. **Confirm** on maps the change was not tuned on (`heldout`, `legacy`) and a second opponent set.
   A winner among many variants regresses (winner's curse).
6. **Promote.** For a development regression bot, follow its charter, preserve OFF continuity,
   verify its own contribution and pass the complete current-Main 80-game floor with clean
   health before flipping the local default. Bind the promoted executable to the tested variant.
   Submission replacement additionally needs the held-out comparison and `arena gate BOT`
   described in section 9. A regression bot's one-win floor does not require a majority against
   Main. Record the decision where the bot's decisions live. **Close the experiments** that led there.
7. **Stop rule.** After about five consecutive nulls at the noise floor, switch to a structural
   change or a different lead. Never tune below the measurement resolution.

Order feasibility work before large preservation matrices: check the enabled path against the
real SDK and focused production-code cases, build its control/treatment pair, then run a bounded
cold or representative native pilot through `arena exp`. Label partial runs explicitly; they
cannot establish full-game health or strength. Pure fixtures do not meter native fuel or prove
SDK API conformance. Before adoption, complete every required suite check, OFF profile and
full-game health/qualification panel. Reuse exact settled controls rather than rebuilding them
solely to adapt a recorder. This changes work order, not the acceptance bar.

| Change | Check |
| --- | --- |
| Docs, charters, notes | Read back; links and commands resolve |
| Wiring, paths, includes | `arena build <sel>` |
| Decision logic or state | Focused cases in `tests/run.py` that reach the real decision code |
| Gameplay behaviour | Build, tests, then an experiment (paired; pool or train maps, both sides) |
| Shared suite module | `arena build <suite>`, `arena test <suite>`, experiments of the affected bots |
| Release candidate | experiment on `heldout`, then `arena gate BOT --ref <live anchor>` |

## 2. Experiments
An experiment answers one question with paired, reproducible game rows. It is the only way agents
play games for evidence; anything that needs games (a comparison, a sweep, an optimiser, a census)
goes through it or through its runner.

**Principles.**
- *A row is the result.* One line per game: the game key (subject, opponent, map, side) and the
  outcome (result, rounds, gems, kills, deaths, gold earned and spent, gems collected, damage, health
  of both sides). Games are deterministic per key, so a row is also a recipe: any game can be
  regenerated with its replay and logs in about a second. Nothing is stored "just in case".
- *Paired by construction.* Every subject plays identical games; compare flips (exact McNemar) and
  paired margins, never raw totals of different games.
- *Claims on unseen maps.* Tune on `pool` and `train`; claim on `heldout` (and `legacy`); state
  coverage (games, maps, opponents, sides) with every claim.
- *Health of both sides* is part of every summary; an unhealthy subject game blocks a release, an
  unhealthy opponent makes its games weaker evidence.
- *Attribute the mechanism with an appropriate control.* An enabled build can change private
  instruction costs before the intended intervention and thereby change fuel-gated actions.
  A matched code path with a final data-only intervention switch can isolate the physical
  action; verify the relevant code equality and actual intervention. Keep the legacy baseline
  as a separate arm and report the enabled build's full costs and outcomes against it. This
  complements OFF preservation and normal qualification. Leave fuel safety thresholds intact.

**Running.**
```
arena exp run OWNER/NAME -q "QUESTION" -s base=main/main -s on=main/main+knob=1 \
    [-s tuned=main/main+learned=1@path/to/weights.txt] --vs anchors/main-v44,anchors/main-hunter7 \
    [--maps pool,heldout] [--sides both]
arena exp show OWNER/NAME [--by map]
arena exp resume OWNER/NAME [--progress-from PATH]
arena exp audit OWNER/NAME [--subjects all,-main,-anchors,-surprise,-contenders] [--out results/regression/current.json]
arena exp replay OWNER/NAME MAP SIDE SUBJECT OPPONENT
arena exp close OWNER/NAME --note "the answer; where it is recorded"
arena exp list [OWNER] [--open]
```
Opponents take the subject syntax `BOT[+KNOB=V...][@WEIGHTS]` too (`--vs main/main+learned=1@w.txt`);
they are labelled by that spelling, and weights are pinned like subjects'. Stopping a backgrounded run's
parent (e.g. TaskStop) can leave python/fastrun children holding the experiment lock: kill your
own children before `resume`.

For diagnostics that need an independent snapshot namespace, add `--private-snapshots`.
Preparation then reads/builds inputs directly in `.cache/OWNER/snap`, without probing shared
snapshot inputs. The machine-wide slots and version locks remain shared. Explicit
`--reuse-shared-snapshots` additionally permits validated shared inputs and their ready-marker
refresh; it requires private mode and permission for that shared access. Actual roots are pinned
per entrant and included in resume/replay fingerprints. Both payload files, lexical source paths,
source identity, metadata and toolchain must validate; no alternate SDK payload is substituted.
Ordinary RAW preparation is scope-neutral; native replay retains its separate suite restrictions.
Private mode is diagnostic only: win-floor qualification and current-panel retention reject it
until their private-input verification is supported. Keep its owner and total storage within the
same limits; private snapshots are protected inputs and are not reclaimed by shared Arena pruning.

- *Subjects* are `LABEL=BOT[+KNOB=VALUE...][@WEIGHTS]`: any bot, any knob overrides (built as
  content-hash snapshots), optionally a weight file for a learned layer (the main bot's
  `src/learn.h`). The first subject is the baseline.
- *Opponents* are arena selectors (bots, suites, anchors). Use the gauntlet in
  [bots/README.md](../bots/README.md) unless the question names others.
- *Map sets*: `pool` (40 official maps), `train` (200 generated), `heldout` (60 generated, never
  tuned on), `legacy` (17 older pool versions), `labs` (7 special cases), or map names.
  Generated sets are fitted to the pool (`maps/generated/`, `fastrun --genmap`).
- *Scale.* Games run on fastrun ([tools/fastrun](../tools/fastrun/fastrun.cpp)), the native runner,
  sharded over single-job processes that take slots from the machine pool: about 40 games per
  second when the machine is free. 2,400 games take about a minute.
- *Output* lives in `.cache/<owner>/exp/<name>/`: `spec.json`, `rows.jsonl`, `summary.json`, and
  bundles only for games with health flags.

**Drivers.** Sweeps, optimisers and learners (grid, SPSA, CMA-ES, ES, GA, RL rollouts) are drivers:
they generate candidates (knob overrides or weight files) and score them with the same runner
(`exp.run_games` in `tools/arena/exp.py`, or `fastrun --games` lines). A driver keeps only its own
state (parameters, log, best candidate); candidate files and games are temporary. Its result is
claimed with an ordinary experiment on held-out maps. Example: `bots/main/tools/train/es.py`.

**Lifecycle (mandatory).**
1. `run` opens the experiment; `show` and `replay` serve diagnosis.
2. Record the answer where the decision lives (suite README, decisions file).
3. `close` it with a note as soon as the question is answered or dropped: it keeps spec, rows and
   summary (kilobytes), deletes everything else and logs the note in the owner's
   `.cache/<owner>/exp/index.jsonl`.
4. No experiment stays open idle for more than a day (`arena exp list --open` flags them).
- Never: replays, statistics or bundles for every game; raw outputs copied into `results/`;
  one-off runner scripts (extend `arena exp` or write a driver); per-experiment documents.

Prepared experiments checkpoint each completed runner chunk atomically in `progress.json`.
If a terminal runner saved a complete raw chunk before checkpointing failed, its owner can run
`python tools/arena/recover_completed_chunk.py OWNER/NAME --rows RAW_JSONL --games GAME_LIST --out NEW_OWNER_PROGRESS`
after confirming the original process tree is empty, then pass that new envelope to
`arena exp resume OWNER/NAME --progress-from NEW_OWNER_PROGRESS`. The adapter verifies the original
prepared plan and pinned inputs, preserves diagnostic markers and leaves source chunks unchanged.
It neither plays games nor resumes the experiment; resume runs only keys still missing.
After the original worker has terminated and its owned process tree is confirmed empty, use
`arena exp resume OWNER/NAME` to run only missing game keys. Resume validates the pinned cached
sources, WASM, maps, weights and runtime; it never rebuilds current sources or expands current
selectors. `--progress-from PATH` can import a separately observed progress envelope with the
same input fingerprint. Invalid, conflicting or duplicate rows fail closed. Run, resume, force
and close exclude one another; a controller launched before that lock existed still requires
explicit termination confirmation. Keep using the existing private Job helpers for native
Windows launches. Closing the experiment removes its checkpoint after retaining final rows.
Recorded opponent overrides are part of the recipe. Replay and resume validate those exact
descriptors instead of assuming an unmodified opponent. Older cached closures can recover the
descriptor from keyed metadata only with the pinned WASM and normal source/toolchain checks;
missing or inconsistent evidence fails rather than substituting the current opponent.

**Exactness.** fastrun reproduces the CLI on whole games (winner, rounds, gems; per-turn fuel on
probe bots). After any CLI, SDK, engine or fastrun change run `python tools/fastrun/validate.py`
(every pool game identical) before trusting experiments. Release gates still play the CLI.

For a candidate runner, use
`python tools/fastrun/validate.py --runner .tools/fastrun/fastrun-dev.exe --maps pool`
before installation. The validator checks complete game keys, outcomes and both teams'
error, fault and exhaustion counts. `python tools/fastrun/tests/test_error_lines.py` checks the
production log classifier against the CLI parser; ordinary telemetry fields ending in `_error`
are not caught engine exceptions.

**Coverage audit.** `exp audit` accounts for every current selected bot, including missing or stale
sources, incomplete or duplicate game keys, recorded health and explicit contribution gaps. It
does not admit bots or infer a mechanism from wins or style averages. A clean row measures only
the recorded health counters; it does not establish absence of stalls. Suite charters and bot
records hold the named contribution and its positive, refusal and continuation evidence. Keep an
unverified unusual bot as an investigation candidate until its interaction is witnessed; exclude
it from panels that claim that interaction is reliably covered.

For the current-Main win floor, run the complete pool on both sides, close the experiment,
then use `arena exp audit NAME --win-floor-vs anchors/main-v53 --out results/regression/current.json`
(replace the reference with the current frozen Main epoch). The report separates wins, draws,
losses and healthy wins for each opponent. It requires complete 80-key coverage, current default
identities, pinned inputs and clean subject health, with at least one win against a healthy
opponent. Exit 2 means at least one selected bot has not met that floor. A passing measured floor
does not establish an independent stratagem or qualify a duplicate. Recheck LIVE and working Main
before treating an old reference result as current.

New experiments pin source snapshots, executable WASM, engine, runner and its runtime library,
toolchain, maps and weights. Replay uses the recorded cached closure, even after working sources
change, and rejects missing or changed selected inputs. A replay validates its two entrants,
selected map and subject weights plus every shared runtime pin; unrelated pruned variants do not
block it. Full experiment audits and resume retain their full-input checks. Legacy experiments disclose their missing pins;
they cannot retroactively establish full provenance. Keep compact audit summaries in `results/`;
rows remain in the closed experiment and replays are regenerated only for diagnosis.
For an exact native CLI diagnostic, use
`arena exp replay OWNER/NAME MAP SIDE SUBJECT OPPONENT --native-role injected` (or `raw`).
This cached-only branch binds the selected payload role, both source closures, toolchain, map
and native runtime in its own recipe. It preserves the experiment's RAW entrants and rows;
native diagnostic results do not inherit their qualification. Both cached payload files must
exist, and weights or round caps are rejected. A completed, hash-checked `output.zip` and
`statistics.json` are retained under the replay case until `close`; failed scratch remains for
diagnosis. Role portability and equality to a historical game need an actual comparison.
Keep candidate runner source and executables in scratch until validated installation: changing
the canonical source while its installed executable stays unchanged breaks its provenance pair.

After closing the census, write `results/regression/current.json` with `exp audit`. Ordinary
`arena prune` validates that manifest's spec/row hashes and preserves its subject and opponent
snapshots. Refresh the audit after any intentional change to its experiment. Exploratory snapshots
are disposable; an old experiment whose inputs have gone missing fails replay explicitly. This
keeps the current regression panel reproducible without retaining every optimization variant.
When `results/regression/strength-current.json` records a newer targeted reference panel, its
current-only retention projection also protects the selected experiment inputs. Prune validates
spec/row hashes, selected roles and cached source/module identities before retaining those
snapshots and the frozen reference. Nested historical reports and exploratory catalogs add no
pins. Refresh the projection when a measured default or its evidence changes.
Name retained input paths or snapshot identities as well as full hashes. Verified keepers for
older hash-only claims live in `results/population/retained-cache-inputs.json`; keeper identity
does not establish the original source identity, default status or qualification.

## 3. Reading evidence
- **Determinism.** The same bots, map and side always give the same game, so one game per key is
  all there is. About 90% of outcomes are fixed by map, side and opponent; the rest flip
  chaotically on small changes. More samples need more keys: maps (`train`, `heldout`),
  opponents, sides.
- **Resolution.** 320 paired games have a standard error of about 9 wins; differences under ~10/320
  are noise unless the paired statistics say otherwise.
- **Map classes.** Small maps tend to be decided by fights, big maps by economy; read `--by map`.
- **Health.** Faults, fuel exhaustion, logged `error` lines and stalls block a release.
- **Monoculture.** Opponents derived from your own code mislead, and local proxies of strong online
  teams have been weak. Prefer the gauntlet's diverse opponents; online evidence (`results/online/`)
  is the only view of top teams.

## 4. Suites and their roles
- **main** - the submission. One agent; three tracks run in parallel: short term, optimise the
  current bot's parameters (knobs, learned weights) on the exact engine; medium term, new
  decision structure on the same chassis; long term, new strategies and models from engine
  simulation (RL, genetic algorithms, SPSA and other optimisers). Releases beat the live anchor.
- **contenders** - rival full bots for the submission ([charter](../bots/contenders/README.md)).
  Same protocol; a contender that beats main on held-out maps and the gauntlet, with clean health
  and the CLI gate, is a release candidate.
- **The extended suites** (adaptive, commitments, decision, distributed, doctrines, events,
  frontier, heuristics, models, specialists, territory, miscellaneous) - the test population: each
  explores one axis of the game (its charter's question) and supplies opponents that are healthy,
  distinct in style and cheap to run. The user's 24 October revision requires each retained bot
  to win at least once against current Main across all 40 pool maps, both sides (80 games).
  Count strict wins separately from draws and half-draw score; a partial screen cannot establish
  failure or passage of the full test. A healthy full panel and a witnessed distinctive mechanism
  are also required. Zero-win unusual commitments remain repair/investigation candidates, rather
  than being replaced by copies of the strongest bot. Higher win rate and Elo remain descriptive.
  Refresh the gauntlet at checkups and identify the exact Main/reference epoch with every claim.
- **anchors** - frozen references (`arena release` only). **surprise** - a separate research track.

## 5. The ladder
`results/ladder/games.jsonl` is one append-only league shared by all agents. `arena ladder run
[sel] [--pairings N] [--loop]` builds the current version of each bot, schedules pairings it has
never played and refits Bradley-Terry ratings (starter = 1000). Versions are content hashes, so an
edit simply creates a new player. `arena ladder table`, `arena ladder styles` (style vectors and
nearest neighbours: check that bots are not converging) and `arena ladder vs A B`. Ladder ratings
measure local strength only.

## 6. Running many agents
- **Roles.** Line owner: one per bot, owns `bots/<suite>/<bot>/`. Module owner: one per suite for
  a shared module. Integrator: tools, docs, anchors, maps, `bots/README.md`.
- **Brief.** Each delegated agent gets its owned paths, the doctrine it must keep, the question,
  the experiment that answers it (opponents, maps, metric), and a stop condition. It returns the
  decision line it recorded and the experiment names (closed).
- **Sharing the machine.** Builds and games take slots from one machine-wide pool (`arena env`
  shows how many are busy); long jobs hold slots per chunk and interleave. Nobody reserves cores.
  Small game batches request only as many slots as they have games: a one-game replay needs
  one slot and one worker, rather than waiting for a four-slot minimum or spawning idle workers.
  Small callers requesting up to two slots with a one-slot minimum retry at 50 ms to catch brief
  handoffs; bulk callers retain the two-second cadence. Experiment batches of at most four games
  also accept one slot and use the fast cadence, running all games through the granted worker
  count. Larger batches retain the four-slot minimum. This changes latency, not reserved shares. A contained
  Python wrapper around `core.prepare` must not hold an extra outer slot; direct native commands
  and fixture runners without internal slot ownership still take their own slot.
- **Safety under concurrency.** Games never run the working tree: `arena` copies each bot's closure
  into a content-hash snapshot first. Shared logs take a lock on every append.
  Source-pinned native batches also need stable shared tools. Coordinate a short publication
  freeze while those cases run; stage unrelated tool changes in scratch, validate their syntax
  before atomic publication, then release the freeze when the native batch ends. This avoided
  another provenance failure in the Models continuation experiment after an overlapping helper
  edit had correctly invalidated one case. Reuse valid immutable cases; never rebind failed
  output to a newer tool epoch.
- **No status files.** Suite READMEs hold the current state and leads; no plans, controls or
  receipts.

## 7. Fuel and robustness
- C++ bots get 100,000 fuel per turn (ten times on the first draft turn, twice on each champion's
  first gameplay turn). Exhaustion suspends the bot until its next turn with stale locals. Measured
  costs are in [rules notes](rules-notes.md).
- Cache static map data once; stage expensive searches over several turns; keep one-time tables out
  of scoring paths; gate optional work on measured fuel (`fuel_remaining()` costs ~16).
- A formatted log line costs ~1K fuel, so a traced build can play differently from the real one.
- `arena gate` checks every game it plays for faults, exhaustion, error lines and stalls; deadlocks
  after some clears need game fixtures in `tests/run.py` (`--test-args "--fixtures"`).

## 8. New suites and bots
- `arena new suite NAME` writes a charter template: a doctrine axis no suite covers, as a question.
- `arena new bot SUITE/NAME --from SEL` copies a starting point. A bot earns its place with a
  decision rule that makes it play differently; two bots that make the same choices count as one.
- Suites never include each other. To reuse code, copy it into your suite and own the copy.

## 9. Releases and online
- The live version is recorded in `results/releases/LIVE.json`; its anchor is the reference: a
  candidate beats it on held-out maps and the pool (experiments), then `arena gate BOT --ref <live
  anchor>` (CLI, every game clean).
- The owner-bound CLI gate accepts prepared-input manifests with
  `arena gate BOT --owner-inputs .cache/OWNER/inputs.json --maps pool --out .cache/OWNER/gates/NAME`.
  Its manifest producer is `gate.bind_owner_inputs(owner, subject_key, reference_key, health_keys,
  map_names, test_route, fixture_roles=roles, released_proof=proof)`. It verifies source,
  metadata, toolchain, both payload roles, maps,
  runtime and an independent LIVE release package; warming, tests and games stay in the owner's
  namespace with the shared slots and version locks. Resume and release consumption revalidate
  bound inputs and evidence. This path plays its own CLI gate; diagnostic experiment rows gain
  no qualification credit. Main's reviewed `main-production-v1` route runs 46 verdicts,
  including 15 fixed fixture games, with nine explicit prepared roles. It rejects unexpected
  test sources and shadow headers. Candidate source identity remains exact even when two
  different source packages produce identical executable bytes.
- Produce the independent LIVE executable proof locally before binding gate inputs:
  `arena release --prove-reference ANCHOR_KEY --owner OWNER --proof-out FILE
  --cold-root ROOT_A --cold-root ROOT_B`. Both roots must be fresh owner-local directories.
  The producer builds the released source bundle twice under its original logical bot ID,
  pins actual toolchain/runtime bytes and environment, and requires both builds' RAW and SDK
  outputs to match the prepared reference. The consumer independently checks that proof.
  This establishes current-toolchain reproduction; it does not establish historical server
  bytes. A source match or a supplied private module hash cannot replace executable proof.
  The trusted in-process fixture executor retains the actual parent Slot and ordered VERSION
  locks until its private Job is empty. Its native leaves use fixed prepared-fixture commands,
  without nested slot acquisition or caller-supplied admission claims. Selected fixture
  witnesses and both cold roots must remain available for gate and release revalidation;
  ordinary gate panels can use `--keep-replays none`.
- `arena release BOT --name NAME --gate DIR` bundles the sources into `results/releases/NAME/` and
  freezes them as `bots/anchors/NAME`. Only the agent the user authorizes in its kickoff may
  submit (the main bot's release agent).
- **Online information gathering.** The user authorizes the Main release agent to submit a
  performance-uncertain or potentially weaker Main as a planned online experiment, even without
  proven offline superiority. This permission covers experimental submission; ordinary
  strength-qualified production promotion still requires its usual strength evidence. Keep
  runtime-health, exact source binding and submission-identity checks, and never manufacture a
  passing strength gate for an experiment. Coordinate each arm's submission/version window,
  unranked match-group quota allocation, collection and rollback with the sole Main quota
  custodian before submission. Record the exact platform version and source identity in the
  online registry; do not overlap uncoordinated submitters or quota writers. Existing limits on
  account/team settings and credentials remain in force. This policy update alone triggers no
  submission or online match.

## 10. Game updates

Check the public editor/visualiser manifests and handbook as well as the distribution API:
the website can publish a newer engine before its downloadable native CLI/SDK. Stage and pin
that candidate separately, query its metadata without starting a match, and retain the installed
coherent bundle until native runner parity can be established. A larger pool changes the complete
floor to twice its map count; historical panels retain their old pool/runtime scope. The
[25 October reconciliation](reference/updates/2026-10-25-engine-2.0.0/reconciliation.json) records
one such publication gap. Never label old native tests as validation of the newer website engine.
Refresh the public patch index before publication as well as at initial discovery. Compare the
native exported fuel-budget vector with the runner's actual configuration, including draft turns;
cheap bots can match every outcome while a changed budget remains unexercised. When a budget changes,
use a bounded workload between its old and new limits before repeating the complete pool comparison.
Upgrade the CLI, SDK, rules, maps and `docs/reference/current-version.json` together (`arena env`
checks them). Refresh `maps/geometry.json` (`node tools/export-game-geometry.mjs`), then
`python tools/fastrun/validate.py` (fastrun must match the new CLI on every pool game; rebuild with
`bash tools/fastrun/build.sh` if the engine changed), `arena build all`, `arena test all` and
`python tools/tests/run.py`. Regenerate `maps/generated/` if the map format changed
(`fastrun --genmap`, templates from `bots/main/tools/mapgen/poolstats.py`). Rule prose can lag the
engine: trust queried settings and patch notes (`docs/reference/updates/`).

Keep a staged runtime's coordination paths fixed while its native controllers are active.
Apply cache/lock changes only after those controllers are terminal, then test the merged tools.
An isolated SDK workspace still uses the canonical machine slot pool; mocked tests must explicitly
replace its coordination path along with their temporary cache, rather than acquiring real slots.
Preserve the previous bundle and snapshots before changing SDK epochs: source identity alone does
not distinguish binaries built by different SDKs, and an upgrade must not rebuild over retained inputs.

## 11. Checkups
Every agent keeps the repo lean; scratch is temporary by default.
- **When:** at the start of a work session, every ~3 hours of work, and after any large run.
- **What:** `python tools/checkup.py` (sizes by area and owner, stale directories, heavy file
  types); `arena exp list --open` (close what is answered); `python tools/checkup.py --slim`
  (deletes regenerable game output older than 12 hours: replays, bundles, dumps, large statistics);
  `arena prune --days 2 --snap-days 2` (old replays and unused build snapshots).
- **Budgets:** `.cache` under 30 GB in total and under 10 GB per owner; `results/` holds registries,
  summaries and releases only (under 3 GB). Stop adding output to an over-budget owner and
  clean it; independently owned work may continue within its owner and total limits.
- **Where things live:** scratch in `.cache/<owner>/` (experiments in `.cache/<owner>/exp/`); tools
  and their binaries in `tools/` and `.tools/`; shared map sets in `maps/`; durable evidence in
  `results/`; decisions where the code lives; test fixtures with their tests
  (`tools/arena/tests/fixtures/`, `bots/<suite>/tests/`), never in `.cache` or `results/`. A `.keep`
  file protects a directory from `--slim` (toolchains, long-running state); use it sparingly.

Coordinate shared housekeeping through one owner while agents are working in parallel. Reuse that
owner's recent complete scan and terminal slim/prune results; report new growth or scoped owner
measurements instead of launching overlapping global cleanup. Repeat the measurement after large
runs or when its visibility or freshness is in doubt. This avoids duplicate scans and competing
deletions without reserving storage or changing the limits above.

Before removing unfamiliar output, ask its owner which inputs remain live or support a retained
claim. File extensions and age alone do not establish that training data or replay evidence is
disposable. Use `python tools/checkup.py --slim --dry-run` to inspect bulk candidates; keep mandatory
regression fixtures with their tests, so owner cleanup cannot silently remove test coverage.

Check visibility before granting capacity. On this host a sandboxed scan omitted roughly53,000
files; a complete reviewed private-Job scan exposed the overage. Report access/vanished entries
and reparses without following them, and use a fresh measurement after slot admission. A measured
margin is not reserved space while other owners work.

The 25 October capacity experiment expired two bounded cohorts of ready, unclaimed, nondefault
snapshots older than two hours, after lock/source/identity and batched ID/full-WASM-hash checks.
It released52 snapshots while retaining seven hash-claimed witness roots and every active input.
The normal two-day policy remains unchanged. Shorter retention requires the same input and
durable-claim protections; it changes no game limit or bot qualification rule.

A later bounded pass released 34 superseded default-source aliases (226,132,363 bytes), after
excluding all 192 current working defaults and verifying protected RAW/SDK keepers outside the
entire cohort. Generated-file cleanup retained both modules and every explicit build pin.
Concurrent growth still exceeded these releases, so those figures did not grant build capacity.
Owner-local OFF proofs subsequently used the same global slots and version locks, with actual
RAW equality checks, while canonical Arena growth remained paused. This separates independent
proof work from the over-budget namespace without changing either storage limit.
