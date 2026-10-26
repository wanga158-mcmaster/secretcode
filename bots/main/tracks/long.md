# Long track: exact-engine learning and search

Updated 25 Oct 2026 21:20 UTC. Goal: first place; international qualifier 4 Nov.
Code paths are relative to `bots/main/`; scratch `.cache/main/pbt/` (current), `.cache/main/zero/` (frozen).

## Authority and constraints

- Own `long/` (bot `main/long`), `zero/` and this note; write only `.cache/main` and `results/online`.
- Live `main/` belongs to release. No online commands, root files or other bots' source. Use
  `../../arena.cmd`, private snapshots (`.cache/main/snap`) without shared reuse, existing slots/Jobs.
- Behaviour defaults OFF until validated. Faults, exhaustion, error lines and stalls block release.
- Candidates go here: patch against `main/`, heldout evidence vs the live release, any online request.

## User direction 25 Oct ~23:00 UTC (binding): from-scratch adaptive bot is the long track
- "Policy tuning won't help us beat the best teams": the long track builds `zero/`, a bot whose
  hidden weights expose different policies under uncertain scenarios, behind a layer of abstraction,
  so it adapts to strategy changes. The 21:10 pivot to v53 module/weight search was wrong as the
  long track's main line; pbt1/killer1 only continue in parallel (killer1 may serve as a sparring
  partner). Work runs as parallel subagents with disjoint file ownership.
- Diagnosis of zero's zero-gold failure: (1) the 25 Oct switch to raw engine commands dropped the
  abstraction; (2) in the original option interface every primitive (step/attack/tail) was learned
  from poor imitation data, so nothing farmed and RL had no gradient; (3) fuel: obs 17-27K + state
  ~15K + goal ~40K + casts + steps exceed 100K, and Gate's "dearest run ever" lockout freezes heads.
- Approach (option layer, config[5]=0 + config[10]=1): the network chooses options (Farm g, Bank,
  Recall, Engage e, Avoid, Hold, Assist), shop, casts, steps, attacks; goal kind is now a state fact
  (cand.cpp `goal_state_facts`). An executor PRIOR is a weight initialisation, not code
  (`zero/tools/prior.py`): trunk unit 0 constant, units 1-7 pass the goal kind, head V/P/U terms start
  as goal-following linear priors; all trainable. Verified: logits respond as designed (trace).
- Parallel tracks: (A) fuel fit of the option layer (agent; owns bot.cpp, sdk.cpp, core/*.cpp);
  (B) RL trainer `zero/tools/rl.py` — PPO/REINFORCE on decision logs + ES fallback, league from
  starter -> v53 -> past zero (agent); (C) opponent-context facts `src/core/context.*` — DONE: 16
  facts (contact, pack, press, cc, fed, trade, gank, camp, bank share, kit ranged/dash/stun, quiet,
  evidence), native test passes, est. <1.6K fuel; integration (kState 48->64, zeros after w1) after A.
- TEACHER/STUDENT (user, 25 Oct ~23:40): train the teacher without fuel limits, then distill into a
  small quantised fuel-bounded student. Teacher runner: private `.cache/main/teacher/fastrun-teacher.exe`
  (tools/fastrun copy; budgets x env FASTRUN_FUEL_SCALE, 20 used; never installed, no claims from it).
  Prior with unlimited fuel: vs starter 13/16 wins, ~10K gold/game (vs ~0 under 100K fuel); vs v53 0/16,
  mean ~50 gems banked to ~1,000, gold ~2K vs ~10K. Error lines 1-53/game = stale attack targets (fix
  with fuel agent). So the option layer + prior is a functional prototype; RL closes the v53 gap.
- RL driver DONE (`zero/tools/rl.py`, rl_data.py, rl_run.py; state `.cache/main/zero/rl/runs/`): PPO on
  decision logs + ES + league; logp parity 4e-5..2e-3 after snapping codec zeros; PPO grad-checked.
  First run ppo-t03 (6x64 games vs starter, T 0.3): NULL vs prior (paired -0.04 +- 0.05). Causes:
  sources changed under the run (5 builds), goal facts 13-16 exceed the codec's ±4 (fix: rescale in
  cand.cpp, with the fuel agent), starter too weak (prior wins 92-100%). Next: frozen build, vs v53,
  128 games/iteration, parity gate on.
- Fuel fit DONE (partial): gate = dearest of last 8 runs + deferral; cheaper goal/obs/paths/net
  kernels; stale attack targets checked. Real 100K: prior wins 6-8/32 vs starter, ~1/32 exhaustion,
  p50 66-81K, 0.1-0.8% turns > 95K. All facts kept in [-4, 4] (goal Farm 13-16 /2.5; attack hits,
  engage 8-9, cast shield /1.25); prior.py rescaled to match: unlimited fuel 10/16 vs starter, 2 bad.
  New knob config[11] (goal period from the last decision), default off.
- Teacher run t1-v53 started 26 Oct ~02:30 UTC: rl.py PPO, 30 x 128 games vs v53 + starter, T 0.5,
  gold/gems shaping annealed over 20 iterations, eval vs v53 every 2 (`.cache/main/zero/rl/t1-v53.out`).
- t1 (lr 3e-4) stopped at iter 3: KL ~0.001, no movement. t2 (lr 3e-3, 4 epochs; KL ~0.015) stopped at
  iter 4: eval vs v53 flat (0/48, gems 55-60 vs 69 baseline, deaths ~30 vs 30.6). Diagnosis: one
  terminal return per game gives no credit for the ~30 deaths/game that decide it. Added `--dense`
  to rl.py: per-champion events from consecutive logged states (team bank, carried gems, gold gains,
  -0.5/death) as discounted returns-to-go (gamma 0.985/round). t3-dense started from t2 weights.
- REJECTED (user, 26 Oct): those dense coefficients and the --shape gold/gem bonus are handwritten
  exchange rates that push the policy towards an arbitrary priority list. t3 stopped after 1 iter;
  never use --dense or --shape for the teacher. Replacement: reward = the game's own outcome only
  (win + 0.25 tanh(gem margin/400)); credit by GAE/TD(lambda) bootstrapped from a learned critic
  (`--gae LAMBDA`, lambda per round; `zero/tools/rl_critic.py`: training-only numpy MLP 48-128-128-1,
  never deployed, refit each iteration on lambda-returns; Monte Carlo until fitted twice). A learned
  value is a potential, so the shaping cannot change which policy is optimal. t4-gae (lambda 0.99,
  from the prior, vs v53 + starter, lr 3e-3) started.
- t4 result (4 iters): critic explains ~37% of outcome variance out of sample on MC returns, ~88% on
  lambda-returns; ~0.5M decisions/iteration; parity < 0.0012. vs starter improving (train win 0.28 ->
  0.34); vs v53 nothing: lost by ~900 gems, tanh(margin/400) ~ -0.98 saturates, so v53 games carry no
  gradient. Zero dies 23-30x/game even vs the passive starter: mostly camp deaths. Ladder screen
  (prior, 16 games each): v18 0/16, v24 0/16, fundamentals 0/16 — no anchor gives signal yet.
- t5-league (from t4 weights + critic): vs starter + snap + self (self-play keeps outcomes informative),
  eval vs starter; add v18/v24 when the starter is beaten decisively.
- Gates for zero: activity vs passive starter (gold, gems, health) -> RL gains vs starter -> vs v53
  league -> heldout claim. Health blocks everything.

## Runs (scratch `.cache/main/pbt/runs/NAME/`: state.json, log.jsonl, best.*)
- STOPPED 26 Oct ~00:05 UTC, both NULL: pbt1 through round 9 and killer1 through round 5 — the
  unchanged v53 reference matched or beat every member (killer: every aggressive genome far below).
  Stopped for the user's direction and to free compute for the zero teacher. State kept for reuse.
- `pbt1` (started 21:35): genes `main` (29 modules), league above. Rounds 0-6: centre-vs-ref paired
  differences within noise (best ~+0.02, SE ~0.05/round); pure-weight member drifts slightly negative
  (v53 weights near a local optimum for this league). Interim: likely null.
- `killer1` (started ~22:40): genes `killer` (draft mode, squads/escort/opening party, hunting,
  trade/chase rules, focus, shop plan), objective lethal (win + margin + 0.25 tanh((kills-deaths)/6)),
  vs v53/v52/v51, 10 maps/round. Why: local opponents kill us ~4/game vs ~13 online (top 10), so every
  death-reducing change looks neutral locally. A searched top-team-like killer is either a candidate
  (if it beats v53 broadly) or the missing sparring partner. Realism check: it should beat v51-v53
  about equally (online top 10 ranks them flat: 30/25/32 of 250).

## Library from earlier long work (do not extend without a new question)
- zero/: DESIGN.md, encoder/model sources, command-audit evidence under `.cache/main/zero/`.
- v44-era fork/Q/hazard sources archived to `D:\files\battlecode-removed\bots\main\long\20261025T2109\`;
  hazard shipped as v51 (H1). Old scripts remain in `long/train/`.

## Operating rules
- After a surprise, two failed attempts or 2h without improvement: fresh reviewer given ONLY this
  note, then change approach. Audit every 3h (next 00:10 UTC). Keep scratch < 10 GB; close Jobs.
