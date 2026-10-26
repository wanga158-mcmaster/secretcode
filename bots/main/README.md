# main: our submission and its sparring partners

## Roster (six bots when complete)
| Bot | Role | State |
| --- | --- | --- |
| `main` | The main bot: what we submit. One agent works on it alone and owns the whole online quota. Free to evolve in any direction its evidence supports. | Live v44 (online #52); D336-D341: exact native runner, learned decision layer, self-play training. |
| 5 targeting bots (planned) | Built to beat `main`: each studies `main`'s code and games (white-box) and exploits one weakness. They give `main` the strong, specific local opposition it lacks (every other local bot loses to it), and they never ship. | not started |

Rules that hold the roster together:
- Every bot owns its `src/`; there is no shared module and no include between bots.
- The main agent owns `main/`, `DECISIONS.md` and the online quota. It uses the targeting bots
  and contenders as opponents, but never edits them; their owners keep their notes in their own
  directories.
- Targeting bots are judged by their win rate against the current `main`. When `main` beats one
  reliably, its owner moves to the next weakness: an arms race that keeps local evidence honest.
- A contender replaces `main` only by the user's decision, or through a release by the main agent
  when the contender beats the live version.
- New bots start with `arena new bot main/<name> --from <bot>`; targeting bots are named
  `counter-<weakness>`. The contenders, rivals for the submission, have their own suite:
  [contenders](../contenders/README.md).

## The main bot
State as of 25 Oct 2026 (D321-D359).

**Live: v53** (platform version 53, 25 Oct 00:06 UTC) = v52 + the medium track's group news
(`group_news`) with the learned weights re-trained alongside it (exploit3) + a quiet mid-turn death
(D345: vs v52 71.8% head to head, +344/-175 of 6,974 broadly, every archetype clean). v52 (platform
52, 20:29 UTC, into the Sprint 2 lock; submissions never carry a name or note, D333) =
v44 + the medium track's value-based capture recall (`capture_recall 2`) + a first-choice fuel
hold (`choose_hold_fuel`) (= v50, D343) + the long track's hazard disengage (`hazard 1`, = v51)
+ the learned layer on, its weights trained by ES as an exploiter of v51 (D344: vs v51 64.8% head
to head, +309/-219 of 6,974 broadly, every archetype weaker against it; gate clean). Ladder #62
before v50 (1871); latest read-only ladder snapshot: **#36, 1988.88**, #1 2365.91 (25 Oct 20:08 UTC).
The17:02 quota response was50/50; the exclusive continuous keeper remains active.
SDK/CLI 3.0.5, engine 1.0.3. Working source = v53 defaults plus
default-off knobs (D323-D331). Track working memory: `tracks/{short,medium,long}.md`.

**The plan: three tracks in parallel (user, 24 Oct).** Short term: optimise the current bot's
parameters - its knobs and the learned layer (`src/learn.h`, `cfg::learned`, D340: 94 weights over
group target scores, the fight verdict, banking and recall thresholds, draft order, shop order,
holding on spawn and contesting sieges; zero weights = v44's rules, `learned=0` replays v44 bit for
bit). Medium term: new decision structure on the chassis (task options the census of losses calls
for: regroup, contest, carry-or-bank with path risk). Long term: new strategies and models from
engine simulation (RL, GA, SPSA; fight search on forked states).
Tools (repo-wide since D342): experiments through `arena exp` on fastrun (`tools/fastrun/`, the
native runner, identical to the CLI on whole games, ~40 games/s with `--procs`); map sets `pool`,
`train`, `heldout` (generated, `maps/generated/`); opponents `--vs gauntlet` (`bots/gauntlet.json`).
`tools/train/es.py` is a driver over the same runner (antithetic ES with Adam). Results (D341):
economy-only training cuts held-out speedrun 25 rounds but is not stronger head to head (it learns
risk); league training (against v44, hunter7, v42, v38) resumes from `.cache/main/train/runs/league1/`.
Release gate for a trained version: paired gains against the current live reference and credible
archetypes on unseen maps, clean full health, and the complete CLI gate. Online games diagnose
transfer and weaknesses; they do not select a release. Owner-local qualification currently awaits
the reviewed complete test route and independent released executable proof.

**Online.** A/B arms on the same 100 top-10 keys (`results/online/panels/top10-ab-v45.json`): v44 14,
v45 (meta draft) 10, v47 (pairs) 12, v48 (pack sightings) 9, v49 (Jungle-first) pending. Knob
arms on v44 do not move the top-10 rate. `bots/main/tools/armseq.py` is the single registry writer:
it submits arms when the queue is idle and keeps mixed-rank survey groups flowing for the live
version (35% ranks 1-20, 30% 21-60, 35% 61-250; kind "survey"). `standing.py` prints win rate by
version x rank band (v44: 9% vs 1-10, 30% vs 11-30, 43% vs 31-60, 66% vs 61-150, 96% beyond).
Replay check-ins (higher ranks: strategy; lower ranks: pointed flaws) append to
`results/online/analysis/findings.md`. Pause: `.cache/main/keeper.pause`.

Standing user instruction (25 Oct): the short-term release agent owns online submissions and the
entire team quota, keeps it fully used, and keeps a dedicated match-review agent active. Review
our games and top teams' games for decision differences, worse positioning, pivotal fights and
other important mechanisms. Data collection and review are continuous priorities.
The audit explicitly includes exact movement/ward tiles and other small actions; proposed general
changes and their local validation plans accumulate in `results/online/analysis/action-backlog.md`.
Review of recent **current-bot results is mandatory in every cycle**. Triage new finished games,
track coverage/backlog, and examine losses, near-wins and successful controls before selecting the
next deep review. Connect results to foundational weaknesses and required fixes, with a local
falsifying test and regression controls. Top-team and older games complement current evidence;
keep version qualifications explicit. The reviewer continues after each batch rather than treating
a completed report as the end of the assignment.

**Where v44 loses.** Against the top teams: deaths and kill gold, elk parties, the Vesper gold
carrier (`.cache/main/study/{economy,fights}/report.md`, D326-D327). In a pure farming race
(D334): 977 rounds to 1000 gems vs 745 for the solver's plan with the same roster, mostly from
~10 recalls per champion (shop recalls at 83% health) and the walks back out, plus cheap gold and
wolf targets.

**Solver program** (D329-D335, library now): `bots/main/tools/econ/` economy surrogate, party
scheduler and rollout search (698-703 rounds vs v44's ~985 speedrun: the economy headroom); the
JS engine harness and planner were retired for fastrun (D342; in battlecode-removed). A workbench agent
(`.cache/main/wb/opt`) works on pointed health/bug fixes.

**Current short-track work (D347-D354).** v53 stays live. Earlier hazard retreat failed the fixed
reference gate; shorter commitment, a learned allocation grid and reserving retreat movement did
not qualify improvements. The movement rule reduced deaths but also kills without competitive
gain: an available walk does not establish a better action. Both continuous review workers now
start with recent current-bot results. They distinguish early objective denial, acquisition-rich
conversion losses and successful large returns, then reconstruct the actual decision interfaces
before selecting another local test. ES remains paused while local opponents trade kills rather
than reproduce top-team low-cost kills. Medium/long candidates require independent verification.
Post-action camp-priority positioning activated, but its broad5,760-game diagnostic did not establish
a meaningfulgain (+39/2,880,p.145,margin+12 SE9); it stays OFF with no unseen/gate/release test.
The160-game pilot effect (50/80vs40/80) did not persist at that size. An unvalidated transport prototype was removed;
recent evidence favors acquisition/contact/protection work. Healing and zero-damage control are
missing from the current fight forecast, but their useful decision interface is unproven. A fixed
source-qualified Road canary produced an existing retreat, matching the observed step away; it
provides a negative control, not a support-change case (D352). Next select an outcome-independent
current-game cohort and establish addressable alternatives before adding features or fitting weights.
A 144-game fixed-controller roster enrollment also found no supported change: native heals and an
extra body activated, but five-pick replacement costs rose sharply. All negative controls passed;
the test is closed, raw outputs cleaned, and no broader roster sweep follows. Recent current-bot
review now distinguishes actual camp engagement/ally contribution from geometric presence (D353).
A fixed recent replay also shows withdrawal followed by renewed camp exposure, with shield damage
separated from HP loss. A sparse default-off consumer probe preserves the exact v53 payload. Its
small instrumented result does not qualify a change. A separate replay pair has equal exposed
history before attack versus recall, but the cleared camp's reward remains uncollected within
the fixed window. That check ended without a candidate; clear/capture/recall ownership is the
next source question (D354). The keeper now checks the rolling quota sooner.

The ownership audit and fixed nearby-reward test are now closed (D355). A legal reward-tile
approach occurred, but the actor left next round, received no reward within the fixed horizon,
and delayed healing. The knob stays OFF; no wider capture sweep follows. The16:21 audit found
the old quota keeper absent and restarted its exclusive writer; read-only quota16:23 is50/50,
rank46 without a rating refresh. Current-game triage and detailed review continue. A narrow
default-OFF retaliation reserve passed scalar checks and exact OFF payload parity, but its fixed
16-game consumer test was censored through300 (D356); no broader run follows. A separate post-PvP
camp-basic probe proved added work, then missed its performance forecasts over3,840 pool/train
games (D358):1423→1446wins,p=.102,52.7%against v53. It stays OFF and parked. Current shopping
timing does not support a uniform purchase-delay repair (D357). A known-dead commitment-count filter
also stays OFF after its fixed 96-game screen: 25→25/48 wins (D359). The integrator published the v2
qualification route: two cold builds reproduce v53, and the full fixture/240-game gate plus an
independent release consumer pass for exact OFF source 0558. Newer C5 needs its own bound gate.
No new release shipped. This short track is paused at the user's request.

Event dates (last verified 21 Oct; reverify): Sprint 2 lock 24 Oct 23:59 UK (22:59 UTC),
Sprint 3 1 Nov, international 4 Nov, UK 11 Nov, final 18 Nov.

History: the D180-D320 research framework is closed as a critical path (library only); the old
section is preserved in `D:\files\battlecode-removed\bots\main\README.md.before-D321`.

## Evaluation and release
Doctrine (D184 revises D89): combine independent mechanics and model work with reconstruction
of complete successful strategies from public play. Test the reconstructed plan across maps and
opponents; current ladder behavior is evidence, with hidden versions and future changes explicit.
1. Mechanism first. Every change states the mechanism it acts on (rules, probes, measured
   quantities, a calibrated model) and the evidence that the mechanism exists. A result without a
   mechanism is a lead, not a decision.
2. Opponent-independent measures decide what they can: economy speedruns (rounds to 1000 gems
   against a passive opponent on all maps, `tools/speedrun.py`), fight-model calibration against
   engine outcomes (`tools/pvpcal.py`), health (faults, fuel, errors, stalls).
3. Robustness across strategies: a local gauntlet of archetypes (anchors, knob personas that play
   distinct strategies such as early aggression, gem rush, grouped play; the counter and contender
   bots once they exist), paired over all maps and both sides (`tools/gauntlet.py`). Prefer the
   change that improves the worst archetype over one that only improves the average.
4. Online games are evidence, used for discovery: our ranked games and groups, and just as much
   the public games between other teams at every rank (`cambc matches list` without --team).
   Lower-ranked teams try unusual strategies and ways of thinking the top teams do not show; a
   strategy catalog drawn from the whole ecosystem feeds gauntlet personas and mechanism
   hypotheses. The games are abundant, so analyse them in parallel (subagents with focused
   questions), and turn each finding into a mechanism hypothesis plus a local reproduction before
   it changes the bot. Online panels
   (`tools/online.py`, D81) confirm a release; they never decide alone, and no knob is tuned to
   the panel's opponents or maps.

Defaults that worked:
- Sweeps against `anchors/main-v28,anchors/main-v28p1,anchors/main-v28p2,anchors/main-hunter7` on
  all pool maps, both sides (320 games, about a minute when the machine is free), with the
  same-run baseline and paired statistics; `--confirm` on the legacy maps and a second opponent
  set before trusting a win; `--seeds N` (knob `noise_burn`) when the effect is small (D80).
  Add the targeting bots and contenders as opponents as soon as they exist.
- Flag-off code should replay the old games bit for bit. Fuel: no one-time tables in scoring
  paths, a formatted log line costs ~1K, `fuel_remaining()` ~16 (docs/rules-notes.md).
- Release: `arena gate main/main --ref <live anchor> --test-args=--fixtures` (all pool maps
  and the five game fixtures), then a clean full gauntlet (`python -B bots/main/tools/gauntlet.py
  --out DIR`: no faults, exhaustion, errors or stalls against any archetype; D90 found an error
  the gate's opponents never trigger), then `arena release
  main/main --name main-vN --gate <gate dir>`. That bundles the sources into
  `results/releases/main-vN/` and freezes them as `bots/anchors/main-vN`. Then submit that
  anchor (`.tools/cambc.exe submit anchors/main-vN`, never with `-n/--note/--name`: the bot's
  name on the website is never changed and submissions carry no notes, D333) and record it with
  `arena release --live main-vN`.

Integrator's engine-upgrade request is resolved (22 Oct): `arena env` verifies CLI/SDK 3.0.5 /
engine 1.0.3 and the pinned catalog. The catalog's settings match 1.0.2; four native smoke games
establish identity and scoped health, not general strategy equivalence. New main evaluations use
1.0.3; old evidence keeps its engine qualification. D118's online drop alone cannot identify a cause.

Request to the integrator (docs/workflow.md is theirs): add the doctrine above to section 2,
"Reading evidence": online results against top teams are evidence of mechanisms, not a target;
teams can hide stronger bots before sprints; adopt changes on mechanism plus local,
opponent-independent and cross-archetype evidence.

## Layout and checks
- `main/src/` (all knobs in `config.h`), `main/tests/` (unit tests of the main bot; each bot owns
  its tests), `DECISIONS.md` (D1-D80, then new entries), `tests/run.py` (all bots' unit tests;
  `--fixtures` adds the game fixtures heal, watchdog, spawn, capture and retaliation, which replay past bugs
  of the main chassis against anchors), `tools/`: `deaths.py` (death classes), `timeuse.py` (time
  use, idle causes, stalled games), `oscillation.py`, `pvpcal.py` (fight-model calibration; trace
  knobs `debug_*`), `replays.py`, `gauntlet.py` (archetype personas + anchors, arena's paired
  report; `--bot-dir` builds a workbench copy, `--personas none`), `speedrun.py` (rounds to win vs
  the passive starter from any run dir), `deathrate.py`, `opening.py` (first clears, gold earned
  and spent, emblems by round; online by tier or a local run's summaries), `optimize.py` (joint
  knob search), `search.py` (draft order + shop plan search), and the online tools `online.py`
  (registry, panels, paired compare, quota keeper), `survey.py` (per version; `--compare A B` with
  standard errors), `budget.py` (time budget of online
  games), `scout.py` (any team's public games) and `vsteam.py`.
- Build: `./arena.cmd build main`. Tests: `./arena.cmd test main`; fixtures:
  `./arena.cmd test main -- --fixtures`.
