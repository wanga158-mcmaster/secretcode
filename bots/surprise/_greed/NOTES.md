# Greed: state and decisions

## Current state

The private surprise bot is scaffolded for local development. `src/` starts as a self-contained
copy of frozen `anchors/main-v39/src/`, with CRLF normalized to LF as required for new sources
(CLI/SDK 3.0.5, engine 1.0.3). Following the user's clarification, this copy is retained only as
an infrastructure reference fixture. Do not develop the surprise strategy from it. The
independent gameplay implementation has not begun; no gameplay logic has changed.

The doctrine is economic compounding: keep the snowball rolling by taking the action that best
turns the current advantage into the next one and ultimately a win. Aggression/defence are
consequences of that calculation, not symmetric style choices. See the suite README and kickoff.

## Decisions

- 2026-10-22: Use a healthy established frozen chassis to make initial experiments about the
  economic mechanism rather than rebuilding basic legality and movement. This deliberately
  replaces the scaffold's starter default; there is no cross-suite source dependency. Superseded
  as a development direction by the user's clarification below; retain the historical fixture
  and its setup evidence.
- 2026-10-22: Keep the bot's leading underscore permanently during secret development. This
  reuses arena's existing exact-selection behavior and avoids changing shared tooling or other
  owners' files. Local source visibility is not secrecy from teammates.
- 2026-10-22: Keep the research process open-ended. Team composition, routes and profitable
  interference are starting questions, not evaluation categories. Use the existing arena
  machinery with an editable generic recipe rather than inventing a separate optimizer or
  premature model of opponent styles.
- 2026-10-22, user clarification: Main and the foundation are useful references, but the surprise
  strategy must never be based on building from main. Build the gameplay design and code
  independently from mechanics. Prioritize economic trajectory search using the available
  extra machines; existing extended-suite opponents already beat the reference bot and main
  already runs controlled experiments. New adversarial program search is not the requested
  compute direction. No multi-machine search software or remote jobs have been launched.
- 2026-10-22, long-term remit: Main covers short-term competition. Judge this project first by
  faithful mechanics, an independent search-to-execution loop, reusable distributed computation
  and increasingly general economic knowledge. Competitive strength is a later milestone,
  not a requirement imposed on each foundation step. The suite README holds the capability
  roadmap and evidence needed to proceed. No new planner, executor or remote worker has been
  implemented by recording that roadmap.

## Next uncertainty

Can a small mechanics-based search produce an economic action sequence that an independently
written C++ executor reproduces in the native engine within the legal information and fuel
limits? Establish the branchable transition representation, native checks and this complete
loop before launching multi-day trajectory searches. Benchmark the actual transition and
checkpoint costs rather than committing to a solver or promising throughput in advance.

The larger question remains:
Which economic trajectories are physically attainable on each map, with which compositions
and purchases, and how do the best continuations change when resource access or arrival windows
change? Build an independent search representation from current mechanics and make its
approximations explicit. Existing foundation research supplies reference facts and comparison
cases, not the strategy or a fixed action menu. The retained main fixture is not a development
starting point.

## Infrastructure checks

- 2026-10-22, current 3.0.5 / engine 1.0.3 bundle: all 20 source files equal main-v39 after
  CRLF-to-LF normalization. No gameplay logic changed. Source key `surprise/_greed@ab86f385a16c`.
- All 14 focused infrastructure tests pass via `arena test surprise/_greed`. They cover actual
  selector isolation, same-source variants, opponent overrides, pairing, failed-build refusal,
  health/mismatch failures and changed-input resume refusal. Native execution is faked in those
  tests; they make no claim about game strategy.
- The native setup check builds the bot and completes 16 full games: Collision (3 picks) and
  Big World (5 picks), both sides, against frozen main-v39 and fundamentals, baseline plus a
  no-change variant. All subject and opponent health checks pass. All 8 unchanged-control pairs
  match final native aggregates and fuel; this is not a world-prefix equivalence check.
  Subject peak fuel is 91,549 on these keys. No full-pool fuel safety is inferred.
- Durable setup evidence: `results/surprise/infrastructure/` (experiment identities, native
  rows, paired report, checks). Replays remain in `.cache/surprise/setup-check/`.
- The default recipe resolves 40 current pool maps, both sides, two opponents and baseline plus
  no-change variant (320 planned games). That full recipe has not been run. The 16 setup games
  establish working infrastructure, not farming optimality, a stronger bot or tournament readiness.
