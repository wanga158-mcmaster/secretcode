# Working agreements

This repository holds our bots for Cambridge Battlecode Autumn 2026 (event `cambc2026a`) and the
local tooling to improve them in parallel. Start from the user's task, [README](README.md) and the
relevant suite charter (`bots/<suite>/README.md`); [workflow](docs/workflow.md) explains how to
improve a bot and how to read evidence.

Use your own judgement. Apart from the hard limits, everything here is a default that has worked:
deviate when the evidence or the task calls for it, and record why where the work lives.

## Hard limits
- **Online.** No submission, upload, login/logout, online or unranked matches, match-group
  creation or other server commands unless the user's instructions grant them (the user grants
  them to the main bot's release agent in its kickoff). Never change account or team settings,
  and never handle credentials.
- **Ownership.** Edit only what you own: your bot, or your suite's shared module when you hold
  it. The integrator owns `tools/`, `docs/`, `maps/`, `bots/README.md`, `AGENTS.md` and
  `README.md`. Report what you need from others instead of editing their files.
- **Frozen and vendored files.** Never edit `bots/anchors/` (new anchors come only from
  `arena release`), `sdk/` or `cmake/`. Upgrade CLI, SDK, rules and maps together as one bundle
  (`docs/reference/current-version.json`).
- **No irreversible deletion.** Move removed content to
  `D:\files\battlecode-removed\<same relative path>`; the user empties that folder. Regenerable
  output under `.cache/` may be deleted.
- **No game budgets.** Local games are unlimited. Do not recreate launch budgets, ledgers,
  charges, seals, receipts or approval documents.
- **Clean up after every experiment.** Experiments run through `arena exp`; close each one
  (`arena exp close NAME --note ...`) as soon as it is answered or dropped. Never keep replays,
  bundles or statistics for every game (games are deterministic: `arena exp replay` regenerates
  any of them), never copy raw outputs into `results/`, and keep `.cache` under 30 GB (10 GB per
  owner). Storage budgets are not game budgets: play as many games as the question needs.

## Defaults
- **Gameplay.** Read `docs/rules-notes.md`, the relevant parts of `docs/reference/GameRules.md`
  and `sdk/cpp/cambc.h` first; queried runtime settings win over copied constants. Catch
  unexpected engine errors at the turn boundary, log a line containing `error`, and yield. Bound
  every turn's work: fuel exhaustion suspends a bot across rounds, a build proves no fuel safety,
  and logging costs fuel too (a formatted line is about 1K). New behaviour goes behind a knob that
  defaults off, and with the knob off the bot replays the old games bit for bit.
- **Evidence.** Every experiment follows [the protocol](docs/workflow.md#2-experiments): one
  question, subjects compared on identical game keys (`arena exp run`, fastrun under the hood),
  paired statistics, both sides. Tune on `pool`/`train` maps, claim on `heldout` (generated,
  never tuned on), `fresh` (generated; claims only, no probes) or `legacy`, and state coverage with every claim. Optimisers and learners are
  drivers over the same runner, not new runners. Health problems (faults, fuel exhaustion, error
  lines, stalls) block releases.
- **Parallel work.** One owner per bot, and one per shared suite module. Builds and games share
  one machine-wide slot pool, so run as many agents as are useful and never reserve cores.
  Delegated agents get a fresh brief: owned paths, the goal, the evidence that proves it, a stop
  condition. Reuse settled results; never poll unchanged work.
- **Records.** Decisions live where the code lives (suite README, a bot's decisions or notes
  file). Prefer editing an existing file to adding one, keep no per-task documents and no
  versioned copies of bots (`arena` snapshots by content hash). Scratch goes to
  `.cache/<owner>/` (experiments in `.cache/<owner>/exp/`); `results/` holds durable evidence
  only: registries, summaries, the ladder log, online data, releases.
- **Checkups.** At the start of a session, every ~3 hours and after large runs:
  `python tools/checkup.py`, `arena exp list --open`, `python tools/checkup.py --slim` and
  `arena prune --days 2 --snap-days 2` ([workflow section 11](docs/workflow.md#11-checkups)).
- **Native Windows launches.** On this host, sandboxed Ninja stalls during console creation;
  the same contained probe and production build pass through reviewed unsandboxed execution.
  Use the existing private-Job helpers and shared arena slots for affected native operations.
  See [the retained launch evidence](results/population/strength-v44-baselines/build-host/sandbox-resolution.json).
- **Shell.** Use scoped `rg` and keep command output under about 100 lines. Judge commands by
  exit codes. Sources use LF; prefer the Edit tool, and write script patches as files rather
  than heredocs. Python on PATH is a Store stub, so use `arena.cmd` or the runtime `arena env`
  shows. Close servers, viewers and background jobs you started before finishing.
