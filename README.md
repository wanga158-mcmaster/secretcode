# Battlecode 2026 (cambc2026a) bots

Our C++ bots for Cambridge Battlecode Autumn 2026, grouped into suites with distinct strategic
doctrines, plus `arena`, a local toolkit that builds, plays, experiments, gates and ranks every bot.
Rules for agents: [AGENTS.md](AGENTS.md). How to improve a bot and run experiments:
[workflow](docs/workflow.md).

The vendor now publishes CLI/SDK 4.0.0, engine 2.0.0 and 70 maps. The
[update reconciliation](docs/reference/updates/2026-10-25-engine-2.0.0/reconciliation.json)
records the staged SDK4 bundle and completed checks. Production remains on the coherent
1.0.3/40-map bundle while full native runner parity and migration checks run. The audit
now derives its required panel from the installed pool; a 70-map pool requires 140 games.

## Layout
```
bots/<suite>/README.md         suite charter: doctrine, bots, distinctness rules, leads
bots/<suite>/<bot>/src/        a bot (id "<suite>/<bot>"); src/main.cpp is the entry point
bots/<suite>/<module>/         code shared inside one suite (relative includes, never across suites)
bots/<suite>/tests/run.py      the suite's focused tests
bots/anchors/                  frozen references: starter, fundamentals, live and past main versions
bots/README.md                 index of suites, their roles and the gauntlet
tools/arena/                   the toolkit (python tools/arena <command>, or arena.cmd)
tools/fastrun/                 the native game runner experiments use (built into .tools/fastrun/)
tools/checkup.py               repo checkup: sizes, stale scratch, slimming of regenerable output
tools/*.mjs, view-replay-local.py   replay inspection and the local viewer (official assets)
docs/workflow.md               the loop, the experiment protocol, evidence, releases, checkups
docs/rules-notes.md            mechanics that change decisions, measured costs
docs/reference/                pinned rules, C++ API, runtime catalog, patch notes, version bundle
maps/                          40 official pool maps, legacy/, labs/, generated/{train,heldout}, geometry.json
results/                       durable evidence only: ladder/, online/, releases/, summaries
.cache/<owner>/                scratch, temporary by default (experiments in .cache/<owner>/exp/)
.tools/                        pinned CLI, the fastrun build, local toolchains
```

## Setup (Windows)
1. `powershell -File tools/bootstrap.ps1` installs the pinned CLI into `.tools/` (cambc 3.0.5,
   engine 1.0.3) and verifies the bundle; `cambc build` fetches its own WASI SDK on first use.
2. `arena.cmd env` checks the CLI, SDK and maps against `docs/reference/current-version.json` and
   shows the Python, Node and clang it uses (Python on PATH is a Store stub; the launcher uses the
   pinned runtime under `%USERPROFILE%\.cache\codex-runtimes\`).
3. `bash tools/fastrun/build.sh` builds the native runner (toolchain in `.tools/toolchain/`), and
   `python tools/fastrun/validate.py` checks it against the CLI.

## Everyday commands
```
arena.cmd exp run OWNER/NAME -q Q -s base=BOT -s on=BOT+knob=1 --vs OPPS --maps pool,heldout
arena.cmd exp show|replay|close|list ...     the experiment lifecycle (docs/workflow.md section 2)
arena.cmd exp run OWNER/floor53 -q "Current Main win floor" -s candidate=SUITE/BOT --vs anchors/main-v53 --maps pool --sides both --keep none
arena.cmd exp close OWNER/floor53 --note "Complete 80-game floor answered; see its summary"
arena.cmd exp audit OWNER/floor53 --subjects SUITE/BOT --win-floor-vs anchors/main-v53 --out .cache/OWNER/floor53-audit.json
arena.cmd exp run OWNER/regression -q "Current qualified pack opponent" -s candidate=SUITE/BOT --vs regression-qualified-v53 --maps pool --sides both --keep none
arena.cmd bots [sel]                         list bots (sel: all, a suite, suite/bot, globs, -exclusions)
arena.cmd build [sel] [--in-place]           build cached snapshots (or the working directories)
arena.cmd test [sel]                         run the suites' tests/run.py
arena.cmd gate BOT --ref anchors/main-v53    release gate on the CLI: build, tests, health, head to head
arena.cmd release [BOT --name N --gate DIR]  show the live version, or prepare a gated release
arena.cmd ladder run|table|styles|vs         the simulated ladder shared by all agents
arena.cmd report RUN | report game PATH       tables and per-champion reports
arena.cmd map NAME [x0 y0 x1 y1]             a map region (terrain, spawns, camps)
arena.cmd new suite NAME | new bot SUITE/NAME   scaffold a suite or a bot
arena.cmd prune --days 2 --snap-days 2       old replays and unused build snapshots
python tools/checkup.py [--slim]             repo checkup (and slimming of regenerable output)
python tools/tests/run.py [--arena]          tests of the replay tools (and of arena itself)
```
From Bash use `./arena.cmd`, from PowerShell `.\arena.cmd`; `arena` in the docs means this launcher.
The complete population audit in `results/regression/current.json` is the historical Main v52
census. `results/regression/strength-current.json` distinguishes it from current v53 qualification;
`regression-qualified-v53` currently contains Warband. The epoch-labelled v52 set is historical.
`arena test SEL -- ARGS` passes flags to the suites' `tests/run.py` (e.g. `-- --fixtures`). For the
Python and Node tools, use the interpreters `arena env` prints. Bots whose directory name starts
with `_` are scratch: bulk selectors and the ladder skip them. Every command takes CPU slots from
one machine-wide pool (`ARENA_SLOTS`, default: all cores), so any number of agents can run at once.
`arena play`, `sweep` and `compare` still play on the CLI; prefer `arena exp` (the same paired
statistics, about five times faster per core and sharded over processes). The population tools
(`arena population`, `arena orchestrate`, `tools/arena/compiled_runner.py`) remain available to
the extended suites' owners; their evidence indexes stay in `results/population/`.

## Online
`arena release` shows the current live submission; frozen `bots/anchors/` holds past references.
Online actions need the user's explicit grant, which the user gives the main bot's release agent
in its kickoff. See [AGENTS.md](AGENTS.md#hard-limits).
