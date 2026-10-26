# main/zero - clean-slate design (24 Oct 2026)

25 Oct compact student (supersedes the full diagnostic command network in gameplay when config[5] and
config[6] are both on): a shared 48->16->16 encoder, direct command logits, then state-conditioned linear
scores over the chosen family's complete raw pointer rows. Every Farm ID and legal spatial payload remains
addressable. The student appends 5,408 parameters after the old 24,842-float prefix, ending at 30,250 within
the 32,768 block. Default-off behavior and the old numerical interfaces remain available for parity only.

Student feature schema2 uses public geometry consistently and supplies no obstacle-path or allied-claim
facts. It does not turn missing path fields into complete native BFS. This avoids hidden computation state
at the cost of obstacle-aware guidance; it is a first deployment test, not a strength claim. State/candidate
construction calls no SDK, path preparation or clear forecasts. The same production selector exposes its
actual inputs, candidate order, legality/admission masks and source labels to the replay record sink. Fuel,
command history, current priority, channel state and capture status are explicit inputs. Slot44 is exact
self shield/maxHP; runtime fuel is exclusively an engineering admission input, unavailable in expert
replay and omitted from learned features. Untransported respawn time is unavailable; slot47 is current
conditioning-goal age. Unknown measured caller bounds admit no
new command until calibrated. Acceptance remains complete runtime parity, actual100K execution with 8K
reserve, then a small trained activity/health smoke; the failed 140K Session graph is stopped.

Source data belongs to an oracle ReplayNext cursor. Each supervised example separates its decision origin
from the later label evidence and states its event role and history profile. Verified draft dispatch is
input1/kind7; input2/kind19 resolves that choice after END. Capture features before dispatch and emit one
record, without reclassifying the resolution as a fresh choice. Expert source eligibility is independent
of our initialization and fuel scheduling. Actual production lifecycle/execution remains a separate gate.

World facts come only from the decision prefix. Goal/control history can be actual learner history or an
explicit goal-conditioned hindsight augmentation, with label/evidence origins and declared initialization.
It is never recovered expert-private intent. Existing future-interaction annotations with fixed refresh,
lookahead and Hold heuristics are historical and unqualified for the new source profile. Do not silently
insert their targets into unconditional expert features or count synthetic REPLANs as observed commands.

Owner: the long-term track. One week to a running bot, one pass through training: this file fixes every
decision before code is written. Changes to it are recorded here with the reason.

25 Oct 05:34 UTC architecture correction (supersedes the fixed head passes in section 2): the 32-game BC
deployment smoke remained effectively inactive and had five unhealthy games. A learned action-type head now
chooses an engine command, then only that command's conditional argument head runs. The bot refreshes exact
state before another command; END preserves a channel, CANCEL explicitly abandons it. Internal REPLAN selects
the goal head without an automatic priority cascade. All engine commands are supervised in chronological
order from exact personal pre-input observations, not inferred simultaneous actor-turn labels. New interface
is behind config[5] (off by default while tested). Action/aux weights append after the old draft trunk; old
20,138-float checkpoint prefix remains compatible. Current layout 24,842 / 32,768 floats. Existing per-head
no-op labels and unrestricted-BFS replay features are not valid training data for the new interface.

Engineering gate: command-family masks are rule legality intersected with fuel admission; no unselected target
head executes. Force-END from fuel or microstep cap is recorded separately from learned END. Conservative
first-use bounds are not a proof of multi-action progress; teacher-forced SDK/fuel checks and runtime activity
must pass before corpus scaling. Exact data transport is `tools/exact_export.py`; shared C++ builds all facts.

25 Oct implementation contract: REPLAN factors the full intent space into kind, balanced stable group-ID
ranges and exact leaves; spatial commands factor complete legal tiles into X then Y. No nearest-target
cutoff removes an intent. PRIORITY and bulk BUY quantity have learned scalar heads. Command checkpoints
explicitly enable config[5] and carry model, feature-source and dataset provenance. Exogenous fuel boundaries
and inverse-ambiguous failed-command receipts remain in causal observation/history coverage; neither becomes
a fabricated policy label. Raw retention and eligible-policy representation are reported separately.

Fuel structure: command mode uses bit-exact fixed-row SIMD inference and caches a head projection only while
the trunk is unchanged. One noncopyable GoalPlanSession borrows an immutable Context for a single traversal,
prepares compact facts lazily and is destroyed before Memory changes, SDK refresh, primitive execution or
yield. The earlier persistent Memory cache was slower than recomputation and is rejected. Dash observation
stores ordinary moves; a cheap exact existence query offers the family and full destinations generate only
after selection. These changes still require complete-caller measurement and useful runtime activity; local
parity, a build or a fuel-masked END does not pass that gate. See tracks/long.md for current evidence.

## 0. Constraints (measured, not assumed)
- Fuel: 100K per ordinary turn, 200K on a champion's first turn, 1M on the first draft turn. Arithmetic in
  this runtime (smoke `main/zero-fuel0`, 32x32 dense layer): scalar 4.6 fuel per multiply-add, 4x unrolled
  4.7, WASM SIMD (f32x4, `__attribute__((target("simd128")))`, accepted by the engine) 2.6, falling with
  wider rows. Observation of a turn in v44 costs ~16K; a formatted log line ~1K; a distance field ~2.5 per
  tile. Budget: <= 30K fuel per turn for inference, <= 30K for observation and candidate features, the
  rest reserve.
- Information: one champion sees its own vision only; allies only when visible; 8 bytes of shared memory;
  1,024 bytes of draft memory (written in the draft, read-only after).
- Data: top-10 vs top-10 replays only (requested from the release agent; replays with our bot are
  excluded). Self-play on fastrun (exact engine, ~40 games/s with v44-sized bots; 32 cores).
- Training stack: numpy only (no torch/scipy): small networks, hand-written Adam, vectorised batches.

## 1. Principles
1. Code holds facts, never choices: rule constants from the engine, exact rule arithmetic (damage, ranges,
   channels, captures, rewards, items), pathing, observation and memory (last seen, with ages). Every choice
   - draft pick, item, goal, step, target, cast, channel - is the network's (charter in
   bots/main/tracks/long.md: no thresholds, exchange rates, margins, timers, radii-as-policy, fixed orders,
   behaviour modes, hand scores, priority cascades).
2. One network, shared trunk: every head reads the same state embedding, so what the bot learns about the
   game (tempo, purchasing power, danger, opportunity) is shared by all decisions.
3. Candidates are described by facts the rules compute (what the action would do next round), so the
   network ranks consequences, not raw coordinates - the first link of the abstraction.
4. Same feature code everywhere: the C++ that builds inputs in the bot also builds them, natively, from
   replays for imitation and from logs for training. No Python re-implementation of features.
5. Determinism: sampling uses a per-game seed, so every game replays exactly.

## 2. Runtime (per champion turn)
2.1 Observation (facts): self (hp, max, strength, focus, gold, carried gems, items, cooldowns, channel
    state, position), visible champions (both teams: type, hp, max, position, carried gems, channel, items
    seen), visible camps (hp, strength, awake), group states (static groups + visible state), altars and
    locks, round, banked gems both teams, shared-memory messages. Memory: last-seen enemies (position, hp,
    round), group respawn estimates from observed captures (rules), banked-gem history (tempo), our deaths.
2.2 Decision heads (each a softmax over legal candidates; no fixed order of priority between them):
    - Goal head - at decision points (goal ended, new threat seen, a hit, or every K rounds): Farm(g) for
      the <= 6 best-reachable groups (nearest by path among available or soon-respawning), Bank(altar) for
      the <= 2 nearest unlocked altars, Recall, Engage(e) for <= 3 visible enemies, Avoid (away from
      threats), Hold (stay / spawn), Assist(ally) for <= 2 visible allies in contact. Candidate facts: path
      distance, rewards (gold x own multiplier, gems), group hp and strength, rounds to clear and damage
      taken by rule arithmetic for us alone and with claimants, respawn time known, ally claims, enemies
      near the target, our carried gems, channel rounds, distance to spawn.
    - Step head - every turn: legal moves (<= 8 + stay) and, when legal, channel actions (recall, submit
      gems, capture hold). Tile facts: path distance to the goal, monster strikes this tile would draw next
      round (monsters hit the nearest champion in range), enemy damage reachable next round, in range of
      the goal's targets, ally adjacency, bush, spawn.
    - Attack head: targets in range (camps, enemies) + none. Facts: target hp after the hit, kill now,
      carried gems, its damage to us, is goal target, linked (Cassi/Issac).
    - Cast heads (slot 0 and 1): legal targets / landings (bounded, by rule arithmetic) + none. Facts: the
      cast's effect by the rules (damage, kills, stun, heal, shield, dash landing facts as for steps).
    - Shop head - on spawn: affordable items + stop, repeated. Facts: stat deltas, emblem progress, passives.
    - Draft head: available champions; facts: design stats and kit, map statistics, our roster so far.
    - Tail head - end of every turn: none, or start a standing recall. Cancelling a recall is free, so a
      recall the step head picks next turn has one turn end done, and any other action cancels it first
      (a standing recall not kept is cancelled at that turn's end). The engine is fact, not rule: no
      handwritten "always pre-recall"; the head learns it from hints - the goal head's recall probability
      at its last decision and that decision's age, the goal being Recall, the turn ends a kept recall
      saves (1/recall turns), hp lost, enemy damage here, distance to spawn, gold, gems, enemies in view.
      Imitation labels: experts pre-start a recall at most turn ends (a recall after the turn's other
      actions; ~30% of expert turns), so replay_obs labels it per turn; it then refines in self-play. In
      the replay labels the teacher goal (Recall or not) stands in for the goal head's probability. Decisions see a standing recall as a free turn (allowances by the rules) with the step
      head's recall candidate showing the turn ends left (user, 25 Oct).
    Ranged champions: move and attack share an allowance, so step and attack candidates form one softmax.
2.3 Network (weights in the learned-parameters block, injected per game by fastrun; distilled as a header
    for submission): trunk s (48 facts) -> 48 -> 48 (tanh, SIMD); head h: logit(c) = (U_h t) . psi_h(phi(c)),
    psi_h: phi (<= 16 facts) -> 16 (tanh), U_h: 48 -> 16. Value heads: t -> P(win), t -> tanh(margin/400).
    Size ~13K floats. Per turn: trunk ~4.6K MACs + heads ~5K MACs ~= 25K fuel.
2.4 Team protocol (facts, shared memory): each champion publishes its goal (kind, target id) and status;
    claims are candidate facts for the others. Draft memory: map facts precomputed with the draft's 1M fuel
    (e.g. group-to-group path distances) if the first turn's budget needs it.

## 3. Training (one pass)
3.1 Imitation (top-vs-top replays): decode each replay (tools/inspect-strategy-replay.mjs: tracks + trace
    events) -> per champion and round, the observation restricted to its vision -> candidates and facts by
    the shared C++ code (native build) -> labels: goal = what the champion did next (next capture group,
    channel, damage to an enemy, retreat), step = its move, attack/cast = its actions, shop = its purchases,
    draft = its picks. Loss: cross-entropy per head + value heads on the game result.
3.2 Reinforcement (self-play on fastrun): stochastic policy with a per-game seed; logged segments (inputs,
    candidates, chosen, log-prob); reward only at the end (win, final gem margin - the game's own score);
    PPO with clipping, advantages by GAE on the value head, a KL term to the imitation policy so the policy
    cannot forget what it imitated. League: current, past snapshots, v44 and hunter7 as fixed sparring
    partners (yardsticks, never sources).
3.3 Gates before every release candidate: parity (C++ = Python inference within 1e-4, native features =
    in-bot features), fuel on every map incl. first turns, health (faults, exhaustion, errors, stalls),
    then strength on fresh generated maps (paired, map-clustered), claims on unused heldout maps.

## 4. Schedule (Day 1 = 25 Oct)
- Day 1-2: rules layer, observation/memory, candidates and facts, network inference (SIMD), sampling,
  logging, native feature tool, Python trainer; smoke tests: legal-action validity on all pool maps, fuel.
- Day 3: imitation data build and training; gate (imitation bot plays sanely: captures, banks, shops).
- Day 4-6: self-play RL iterations; league; gates.
- Day 7: release candidate: distilled weights, full gate, request online test.

## 5. Open items
- RESOLVED (tools/tracekinds.py on a scout top-team replay): the trace records every action with its
  actor and round - move (from/to), attack (target entity inferred from coordinates, e.g. camp:57, and
  the target's hp before/after), cast (slot, ability type, target tile, cooldowns before/after), shop,
  recall, tribute, ward, set-priority, capture, end-turn - plus each actor's availability flags and nearby
  champions/camps with a per-actor visibility flag. Trace windows are <= 64 rounds (32 calls per game;
  parallel decode). Labels for every head come straight from the trace; nothing has to be inferred.
- RESOLVED: a native clang++ exists (.tools/toolchain/llvm-mingw, the one fastrun is built with), so the
  core compiles to WASM (bot) and natively (replay tool) from one source.
- Fuel of observation and candidate facts on the heaviest pool maps (measure on Day 1 with the core).
- Throughput of fastrun with zero's network (games/s) - sets the RL batch sizes (measure on Day 2).
- Data: release agent downloading top-10 vs top-10 replays (throttled, ~300 target) into
  results/online/topvtop/ with index.json.

## 5b. Changes forced by measurement (24-25 Oct smoke tests, main/zero-smoke1/2)
- Activation: softsign x / (1 + |x|) instead of tanh (exact, cheap, the same in training).
- Every weight matrix is stored transposed (column-major); dense layers accumulate four outputs per SIMD op
  with no lane reductions and skip zero inputs: trunk 11.7K -> 3.7K fuel. Projection width kProj 16 -> 8.
- SDK costs (round 70): simple queries 50-80 fuel; vision::champions 4.7K; champions() 3.7K (cached once);
  bitboard OR ~550; positions() ~1.8K; visible_tiles ~540; vision::camps ~1K. So: the cached roster with
  per-champion visibility, legal moves by the rules from known units (core/moves.cpp, no bitboards), items
  read only on spawn or while dead, claims read only at goal decisions, the observation reused.
- Path fields are never built inside a decision: a missing field is queued and the Chebyshev distance stands
  in; the end of each turn builds queued fields while the fuel left covers the dearest measured build.
- Shop purchases update gold and items by the rules (no re-observation per purchase): the unbounded shop loop
  with full refreshes was the main source of fuel exhaustion.
- Dead champions read only gold and items (position, ability and state queries throw while dead).
- Engineering fuel guards (gates): each decision (shop purchase, goal, cast, ranged act, melee attack,
  step, re-attack, tail) runs only when the fuel left covers the dearest run of it seen so far + 15% + 8K;
  a run cut by suspension counts as what it had plus what it used after resuming. Shop purchases also
  leave room for the stat re-read and a step decision. Diagnostic reports print only with 10K to spare.
- Init in six stages, each within an ordinary 100K turn (an exhausted turn resumes next round, so a stage
  over 100K suspends): terrain (on the 200K first turn), groups, Map::finish, path cache, first
  observation, spawn field.
- Observation from bitboards: visible champions from champion_tiles per team (+ champion_at per visible
  one), camps from camp_tiles with wakefulness from awake_camp_tiles; camp health read for <= 12 camps a
  turn (near, never read or stale), kept per group member (a per-tile table cost ~110K to fill). Mid-turn
  re-reads replaced by rule updates (after an attack, a step). Pool + train, 480 games: no exhaustion.
- Measured per turn after these changes: observation ~13K, state + trunk ~9K, goal decision ~35K (every K
  rounds), casts ~15K, step + attack ~35K.

## 6. Code layout and interfaces (fixed before implementation)
src/core/ - pure C++20, no cambc include, compiled into the bot and into the native tools:
- catalog.h: rule data (champion designs and abilities, camps, items, game settings) as POD structs,
  filled from the SDK (bot) or docs/reference/runtime-catalog.json (native).
- map.h: static map (size, walls, holes, bushes, altars, spawns, camp groups and members) and distance
  fields (BFS over the 8-neighbourhood as the engine moves, cached per goal).
- obs.h: one champion's observation for one turn (self, visible champions/camps, group states, altars
  and locks, round, banked gems both teams, shared-memory claims, availability flags) + Memory (last
  seen enemies, group respawn estimates, banked history, deaths), updated by `observe(Obs&, Memory&)`.
- rules.h: exact rule arithmetic (damage with items and passives, ranges, who monsters hit, capture and
  reward split, channel times, item effects, vision).
- cand.h: candidate generation and facts per head (goal, step, attack, cast x2, shop, draft): fixed-size
  arrays `Cand{ kind, target, phi[16] }`, max counts per head; `state_vector(Obs, Memory) -> float[48]`.
- net.h: inference (SIMD on WASM, scalar natively; identical maths), weights from a flat float array
  (learn block / header), sampling with a per-game seed, `decide(head, state, cands) -> index, logp`.
- act.h: the chosen candidate as an engine action description (move to tile, attack id, cast slot+target,
  recall, submit, buy item, pick champion).
src/ - bot glue: sdk_obs.cpp (SDK -> Obs, Catalog, Map), sdk_act.cpp (act -> SDK calls with error
  capture), turn.cpp (observe -> heads in turn order -> act; logging), draft.cpp, main.cpp.
tools/ - replay_obs.cpp (native: decoded replay -> Obs per champion-turn -> cand facts + label ->
  binary records), decode.py (replay zip -> tracks + 64-round trace windows, parallel), train.py
  (numpy: imitation + PPO, Adam), weights.py (vector <-> learn block / header), parity.py (C++ vs Python
  inference, native vs in-bot facts), selfplay.py (fastrun batches, logs -> records).
## 7. Inputs (facts only; every number below is computed by the rules from what the champion knows)
State vector (trunk, 48): round; banked gems both teams; banked rates both teams over <= 50 rounds;
champions alive both teams; our deaths; own hp fraction, max hp, strength, focus, carried gems, gold,
gold multiplier, items owned per family (7), emblems owned; champion type (13 one-hot); visible enemies,
visible allies, nearest enemy distance, health share within 8 (ours / all), enemy damage reachable next
round / own hp, monster strikes drawn here next round / own hp, enemy carried gems seen; distance to spawn
and to the nearest altar; on spawn; channel state (recall, tribute, capture hold); current goal kind
(8 one-hot); rounds since the goal started.
Goal candidates (phi 24 = kind one-hot 8 + 16 kind slots), kinds and slots:
- Farm(g): path rounds to reach; gold x own multiplier; gems; group hp now (known or full); group
  strength; clear rounds alone and damage taken alone (rule arithmetic, Smith order); the same with the
  claimants; respawn rounds left; ally claimants; enemies seen near g and the age; g's distance to the
  enemy spawn; capture in progress; is the current goal.
- Bank(altar): path rounds; carried gems; channel rounds; locked; enemies near the altar; current goal.
- Recall: missing hp; gold and the stat gain it buys now (best affordable step by item rules); walk back
  from spawn to the current goal; enemies in reach (a hit cancels); carried gems; current goal.
- Engage(e): rounds for us to kill it, rounds for it to kill us (both from rule damage rates incl. ready
  casts); its carried gems; distance; allies / enemies near it; ranged; channelling; linked; current goal.
- Avoid: reachable enemy damage / own hp; nearest enemy; path rounds to spawn; current goal.
- Hold: on spawn; hp fraction; enemies near the spawn exits; current goal.
- Assist(a): ally hp fraction; enemies near it; distance; current goal.
Step candidates (phi 16): stay; channel (recall / submit / hold); change of path distance to the goal
target (-1/0/+1); monster strikes drawn on that tile next round / own hp; enemy damage reachable there /
own hp; in attack range of the goal target; can attack any enemy from there; bush; spawn; adjacent allies;
nearest enemy after the step; on a capture tile of the goal group.
Attack candidates (phi 16): none; camp / champion; hits to kill; kills now; carried gems; target's damage
to us per round / own hp; goal target; awake; channelling; linked; kill reward (100 gold + bag).
Cast candidates per slot (phi 24): none; ability kind (7 one-hot); damage dealt; kills; enemies hit; camps
hit; stun rounds applied; heal / ally missing hp; shield / incoming damage; landing facts (as for steps)
for dashes; channel-cancel effect on an enemy.
Shop candidates (phi 16): stop; item family (7 one-hot); hp / strength / focus gained relative to now;
completes an emblem; passive magnitude; cost / gold; consumable.
Draft (own small net; state = map statistics + our picks so far + their picks so far): candidates =
available champions with design stats and kit summary (damage rate on camps, burst, area, control,
heal, shield, dash, passive type).
Imitation labels: step / attack / cast / shop / draft are the recorded actions; the goal label is the
first decisive event in the next 20 rounds - capture or camp damage on g -> Farm(g); recall -> Recall;
tribute -> Bank; damage on enemy e -> Engage(e); movement away from enemies in contact -> Avoid; staying
on spawn -> Hold; moving to an ally in contact -> Assist; none -> the previous goal.

Records (binary, little-endian float32): per decision {head, state[48], n, phi[n][16], label|chosen,
  logp, game, champion, round}; per state sample {state[48], game, round} for value targets.
