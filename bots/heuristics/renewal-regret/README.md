# 02 renewal-regret

Three cheap adverse completion scenarios price opportunity regret over a renewed production
cycle: nominal work, unseen-visit delay, then one contest reset and extra attack. The outside
production rate is endogenous to the current candidate set. Gold funds the first two emblems;
after funding, gems are the bottleneck currency. Nearby allies discount personal capture share.

This controller uses a friction-priced progress commitment, travel-dependent tribute batches,
cargo-sensitive retreat, health-first equipment, and Varg/Tavi/Marcella as the three-pick prefix.
It does not inherit Satisficer's lexicographic aspirations or unconditional progress lease.

Enabled by default after the final health checks; `HEURISTICS_ENABLED=0` includes the exact
frozen fundamentals executor. Active-versus-frozen performance compares the complete bundle,
including the suite's mechanical repairs;
the focused fixture separately demonstrates a different real choice from Satisficer.

Known limitation: source valuation does not clip new rewards to the final round horizon;
the bank deadline protects an existing bag only. This remains a separate next-controller lead.
The financing aspiration uses the pinned 30-gold shard cost as a coarse proxy; actual purchases
always use the SDK's queried item cost. This proxy must be reviewed with any balance bundle.

## Evidence and execution repairs

Initial paired pool: active 127/320 versus frozen 164/320; flips +16/-53, McNemar p<.001,
paired gem-margin difference -183.6 +/- 21.8 SE. No faults, exhaustion or errors, but three
zero-clear stalls on a long-row map. Legacy: 100/102, all clean. Those were not promotion grounds.

Across 80 initial Fundamentals keys, active earned 5.4% more gold, cleared 14.2% more camps and
had 32.3% fewer deaths, yet banked 19.0% fewer gems. On road-of-riches B, first clear moved from
round 134 to 16, but first bank from 163 to 1079. The compulsory personal equipment-funding
goal and travel batches defer permanent score. This is a measured losing tradeoff, not evidence
that minimax regret alone is worse: the complete bundle differs in finance, draft and execution.

The bot owns its executor copy, with the shared safety guards preserved. Dense-row replays
showed same-source movement cycles through previously dead pads and across repeatedly forgotten
living barricades. The local repair remembers observed living camp occupancy conservatively until a
visible empty tile clears it, selects unseen member frontiers while retaining witnessed-death
facts, and builds legal neighboring approach pads. Death facts expire after a public respawn
interval without a fresh live certificate, or immediately on a witnessed new generation.
An optional physical rescue opens an in-range harmless other group after four rounds without
best travel progress, only when no chosen-source attack is available; mixed groups and unsafe
Varg splash are refused. One ordinary attack is attempted, then the chosen-source plan resumes.

An initial all-public-spawn occupancy variant was rejected after large-map stalls. Witnessed
occupancy alone retained the dense-row fix, but legacy expanse still stalled. After-action
traces established a different cause: move entry had about 40K fuel, then obstacle preparation
left 31-32K, below the unchanged 35K native-field reserve. Exact scenario caching (one model
per eligible candidate, at most 12) removed repeated arithmetic. The same actors then traveled
across the map and completed productive work. Proven-unreachable cooldowns were not changed.

Focused final regressions: `h12-02-cached-rows` has all eight dense-row keys clean, 4/8 wins,
max fuel 82,871; `h12-02-cached-expanse` has all six keys clean, 6/6 wins, max 82,655.
In the current-source Tempo A replay `c1915a33`, Varg's blocker attacks at rounds 178-182
while pursuing source 15 are followed by crossing the row, clearing that source (`g_937`)
at 211, capturing 60 gold and four gems at 218, and banking those gems at 220.

Original exact-source evidence, before the capture watchdog, measured active `41831d617823`:

- `.cache/arena/sweeps/h12-02-final-cached`: 40 pool maps, four opponents, both sides,
  320 paired keys. Active 122-198 versus frozen 162-158; flips +16/-56, McNemar p<.001;
  paired gem-margin difference -175.7 +/- 22.7 SE. Active wins by opponent: Fundamentals
  15/80, Tempo 34/80, main 1/80 (`d5bc9e7b7a7b`), Concentric 72/80.
- `.cache/arena/runs/h12-02-final-cached-confirm`: 17 different legacy maps, Starter,
  Banked Vanguard and Bargainer, both sides; 100-2 (32/34, 34/34, 34/34 respectively).
- All 422 final active games have zero faults, exhaustion, errors, stalls or suspension.
  Pool max fuel 99,708 (late big-world A versus Tempo); legacy max 85,526. The observed
  pool maximum is close to the budget, so this is coverage evidence, not a universal bound.
  The frozen control has eight stalls; those games remain in the paired comparison.
- `arena test heuristics/renewal-regret`: 2/2. Fixtures exercise the actual selector,
  frontier memory/epoch expiry, perimeter transform and route-rescue admission.
  `heuristics-profiles-v2` fingerprint stays `666b4a5ff03e` through the cache and execution
  repairs, including all 920 choices. Versus Satisficer, disagreements are 46.3% choice,
  7.0% bank, 17.5% risk, 36.0% build and 100% roster on that fixed corpus.

This is a healthy, distinct losing bundle; no strength improvement is claimed. The next bot
should investigate deadline-aware payoff and equipment funding that does not postpone scoring
until every personal emblem is financed. Health-first startup can reduce deaths while losing
score. Keep the 30-gold financing proxy separate from queried purchase legality, preserve
movement fuel after every planning stage, and diagnose failed movement separately from a
wrong source choice. These are lessons for a different controller, not coefficient retuning here.

## Hidden capture contest repair

The original 422-game panel did not cover every opponent. The later 80-game Stock versus
Regret panel (`h12-05-style-confirm`) exposed one Regret zero-clear stall: yellow-brick-road B,
old source `9f238725fbdd`. Both teams held opposite ends of cleared 30-tile rows. Regret saw
no local enemy holder, reset its visible-contest timer and waited forever, although the native
group-wide contest prevented payout. The selector and movement field were not running.

`CAPTURE_WATCHDOG` is now enabled by default. It was developed default-off, with all new
runtime members, calls and bookkeeping excluded by the off arm. Both real reward-hold paths
use the shared `CaptureWatch`, queried capture duration plus two phase-slack rounds. Missing
enemy vision does not reset the clock. Departure, death or a changed reward lifecycle does.
An unpaid expired hold quarantines that source for 12 rounds and attempts a physical exit;
the clock remains expired if movement fails. A failed exit can immediately bank at an adjacent
altar, perform one harmless other-group blocker attack, or recall after the existing visible-
threat clearance. Whole-group incoming and Varg splash safeguards remain in force. Every
fallback scan has a fuel gate; incomplete clearance refuses recall. Existing channel checks
precede the watchdog. This changes capture execution, not the regret arithmetic, equipment,
draft, bank rule or risk rule.

The first patch's unconditional return after a failed exit could suppress all useful fallback
work. It was superseded before final coverage by the actual `leave_or_work` dispatch, tested
with failed and successful walks and refusal of a mixed blocker group. The clock fixture
also verifies that blocked movement and quarantine expiry do not restart the hold.

Final hotfix evidence uses old Regret as the same-run control, not frozen Fundamentals:

- `.cache/arena/sweeps/h12-02-watchdog-accepted`: all 40 pool maps, four opponents, both
  sides, 320 identical paired keys. Watchdog 114-206 versus old Regret 121-199; flips +5/-12,
  McNemar p=.143, paired gem-margin difference -13.6 +/-8.5 SE. No detectable difference.
  Watchdog wins: Fundamentals 11/80, Tempo 32/80, main 0/80, Concentric 71/80. Same-run
  references include main `69b8b017328c` and Tempo `51efb2bcec70`; both changed externally.
- `.cache/arena/runs/h12-02-watchdog-accepted-legacy`: all 17 legacy maps, both sides,
  Starter 32/34, Banked Vanguard 33/34, Bargainer 34/34: 99/102.
- `.cache/arena/runs/h12-02-watchdog-accepted-stock`: Stock `43ceb52fc5ee`, all 40 pool
  maps, both sides: 53/80. This includes the formerly stalled matchup.
- All 502 required watchdog games have zero faults, exhaustion, error lines, zero-clear
  stalls or suspension. Maximum ordinary-turn fuel: pool 99,607, legacy 85,546, Stock 97,705.
  The pool maximum remains close to budget. Coverage is evidence, not a universal bound.
- `arena test heuristics/renewal-regret` passes 2/2; strict v3 profile stays `a4758540cdec`,
  including all 920 source choices and 325 gear choices. No policy coefficients changed.

Exact known-match receipt, measured active `a3aa2f39ab7d`:
`.cache/arena/runs/h12-02-watchdog-bounded-repro/games/01yellow-brick-road_working-inventory_renewal-regret_530bc6d3.repl26a`.
Physical Tavi / native actor 6 holds `g_485` at (32,12), reward available and no payout, at
rounds 78-85. At round 85 the seven-round threshold expires; Tavi walks to (31,11), seen at
ready turn 86. A later shared capture of `g_377` at 174 pays Tavi 55 gold and seven gems;
two further gems arrive at 217, and Tavi's own nine-gem tribute completes at 220. Team result:
66 clears, 1,035 banked gems, a win at round 999 and no health flags. This proves productive
continuation after the watchdog, not solo clearing or superior economics.

Off source `7ba83c8d6fb8` remains byte-identical to old active Regret, WASM SHA256
`1b2c3c858d2e2ff4910fc789eadab8d3846676ab57803856e51ab3bb140382ab`.
Measured watchdog active `a3aa2f39ab7d` and promoted default `98f0e74e76b4` have identical WASM SHA256
`e94afe542a522147796adc9afbd9acb54cf651ba4a4d086801d63cbaae6e778b`.

Remaining limits: the watchdog infers an unresolved contest from missing payout, rather than
querying hidden holders. Its departure rule is bounded local work, not a complete escape
planner; a visible unsafe threat can prevent recall and a dangerous blocker remains refused.
The healthy experimental bundle remains a losing idea relative to its original Fundamentals
panel. No strength improvement or release is claimed. The next controller should distinguish
absent local evidence from actual global progress, and make failed departure admit useful
bounded work while retaining the unresolved lifecycle clock.

## Funded health restoration test, 24 October 2026

The current `98f0e74e76b4` remains 0/80 against frozen Main v51 on all 40 pool
maps, both sides, with no truncated games or recorded health flags on either
team (`strength-baseline/floor51`). Closest Lanes A collects 844, banks 733 and
loses to 1,003 at round 1,188, with three deaths. The 111-gem conversion gap does
not by itself identify a bank-batch bug or explain the production deficit.

A selected official native replay gives a more specific finance constraint.
At round 214 Marcella has 130 gold, 980/1,300 HP, one jungle-health shard, no
bag/channel and no personally visible enemy. The remaining four health shards
cost 120. It still has the same one-shard health stage at round 355 with 300 gold.
It starts recall then, suffers an interruption, escapes and retries at 363.
At 368 it buys 120 gold of health, 150 of Monster Strength and 30 of Power
Strength. The health purchase changes 860/1,300 to 1,260/1,700 at spawn;
the separate spawn-restoration tick completes 1,700/1,700 before departure
at 369. Purchase, increased capacity and completed restoration are distinct.

`REGRET_HEALTH_FINANCE` was developed OFF and is now enabled by default after
the qualification below. It returns when this controller's first health
stage is fully funded. The rule uses queried shard cost and combination
count, excludes Tavi's field buying, and retains the existing 300-gold finance
rule after health completion. Capture, requested banking and health retreat
still precede finance, and the existing threat-clearance/recall and spawn-heal
execution remains in force. No regret scenario, renewal cost, travel batch,
health-first equipment order or exploration/death memory changes. This is a
bounded finance experiment; it does not assume an earlier purchase pays for an
extra restoration/return trip. Native comparison must account for actual recall,
healing and return latency, later private paid work, lost income and any delayed
Monster completion. A failed bank walk falling through to finance is a separate
static concern; the selected first return has only two gems and does not prove
that failure, so the existing 300-gold path is unchanged. A new early return
explicitly refuses a pending bank request, including a failed bank walk. The
funding test avoids fresh full-state queries on mature or unfunded turns.

The actual paired Main v52 first intervention is Varg, not the historical
Marcella example. Both arms have Varg at (36,23), 940/1,000 HP, 160 gold,
no items, bag, channel or personally visible enemy at 189. ON legally recalls
then, arrives at spawn at 194 and buys the health emblem for 150. Purchase
changes HP to 1,440/1,500; a separate owner-end heal completes restoration
before ready round 195. OFF continues working, recalls at 240 and completes
the same health restoration before 246. An independent comparison matches
35,407 official exposed world snapshots through the 189 owner-start before
the intervention. Private fuel, memory and logs are outside that comparison.

The early trip costs real income time: Varg's next payout is 40 gold at 242,
25 rounds after OFF's next payout at 217. By 246 each has earned 40 gold since
the intervention. ON then receives 40 at 250 and 258, obtains 11 gems at 361
and banks 33 at 416. Through 400, matched Varg capture income is 220 gold/22
gems ON versus 140/0 OFF. Actual item traces put the completed Monster emblem
at 305 ON versus 491 OFF, although OFF owns one Monster shard from 245.
These are whole-variant outcomes after the first intervention, not isolated
single-actor effects. Marcella's capture income over 214-400 falls from 180
gold/eight gems to 80/zero. Varg also later dies in ON, and Lanes A team deaths
rise four to six. Earlier health investment does not establish uniform income
or survival gains.

`renewal-strength/health-lanes52` has four complete games, both sides, with
no recorded health flags on either team. OFF wins 0/2 and ON 1/2; paired margin
changes +230 +/- 172 SE. ON loses Lanes A 859-1,022 but wins B 1,026-917 at
1,299. The separate full `renewal-strength/health-floor52` includes all 40 pool
maps, both sides: exactly one strict win in 80 complete games, zero truncations
and no recorded health flags on either team. It meets the user's one-win floor;
the already observed deterministic B key is included, not an independent
replication. No heldout or general superiority claim is made.

Qualified injected ON `d25147f19d7c` and promoted default `9666ca0c5723` have
identical WASM SHA256 `2a009a47a0e8065fabc13f3fc4424fefd4861972bef2bd96a409a1e52ff0ffe0`.
Forced OFF `0de2b2bbfca9` still exactly matches incumbent SHA256
`e94afe542a522147796adc9afbd9acb54cf651ba4a4d086801d63cbaae6e778b`.
Only this private finance default changes. Production fixtures pass 2/2,
including queried financing terms and refused early return during travel-batch
or adjacent-altar bank requests. Both answered experiments are closed. The
integrator captured this exact default in the complete 192-bot Main v52 census;
this bot did not overwrite the global regression account. Compact evidence
is in `results/population/renewal-current-strength/index.json`.
