# 05 working-inventory

Prism treats carried gems as a finite working balance. Her first source pays for
stock; she retains that stock only while a fresh, legal spell can cross a useful
discrete threshold. A loaded spell either kills where the empty-stock forecast
cannot, or reduces the predicted number of subsequent basic attacks. The local
`flow.h` supplies the same phase machine, threshold predicates, cargo receipts
and bounded combat executor to production and mutable fixtures.

The state machine is replenish -> work -> liquidate. Work requires a currently
executable spell, an affordable source and coarse time to cash the existing bag.
A safe incumbent capture can finish after its last monster dies, even though
the combat threshold has disappeared. Winning cargo, deadline pressure, low HP
or unsafe incoming damage override retention and capture protection. A 24-gem
cap, a no-progress expiry of capture_time+1 and an absolute work expiry of
2*capture_time+2 prevent repeated opportunities from extending work forever.
Once liquidation begins, later spell opportunities do not renew it. Every bag
decrease invalidates the old source/route assumptions. Actual tribute starts are
recorded immediately, including a one-tick channel that completes before another
entry observes it; recall retains gems and never counts as banking.

The five-pick order is Prism, Marcella, Vesper, Tavi, Willow, so three-pick maps
exercise the economic caster rather than treating the priority three as the
whole roster. Prism buys monster Focus, jungle Health and power Focus; Willow
buys both monster and jungle Focus; Marcella buys monster and jungle Strength
then power Health. Other roles buy monster Strength and jungle Health. The live
shop queries SDK cost and legality and invokes the same role purchase hook as
the fixed profiles. Other roles convert their gems promptly in six-gem batches,
with nearby altars, injury and deadline overrides.

The source selector uses finance queues rather than splash geometry or a
quick-completion coefficient. It prioritizes cashable jobs, capture-ready work,
gem income, meeting a personal batch, coarse downside bands and limited excess
production, then arrival time. Visible sharing reduces the personal batch.
An active stock option directs execution to its freshly observed source and
primary; ordinary commitments use a bounded best-progress lease. A source that
cannot be cashed before the horizon can still supply productive fallback work.

Combat inspects at most six contact targets and two typed spell slots. Ordinary
last hits precede spell expenditure; otherwise stock-enabled kills precede saved
basic thresholds and ordinary spell progress. This is a primary-target menu,
not a joint-hit geometry search. At most one basic and one successful cast run
in two transitions. Actual visibility, death, HP, readiness and action allowances
are checked again. Spell and basic camp forecasts both include the queried
Monster-emblem bonus once. Live Focus already contains Prism's gem bonus; the
empty forecast subtracts queried gem Focus and scales unclipped raw spell damage.
The empty state is a forecast, never an engine action or observed counterfactual.

The local runtime/frontier copies completed04's bounded metadata, routing,
capture and witnessed-death mechanics. Its splash combat and density prior were
removed. The disabled arm includes frozen shared control directly. Static altar
approach geometry is cached once on the separate topology turn, and area-risk
admission caches one fresh owner snapshot and one result per group per unchanged
observation epoch. These are execution cost repairs, not a different stock rule.

## Evidence and limitations

Focused fixtures exercise the production phases and executor: loaded/empty kill
and saved-basic boundaries, Monster-emblem basic thresholds, pending capture,
forced conversion, stale/uncashable/no-benefit refusals, expiry despite repeated
progress, post-bank reset, one-tick tribute, cancellation/death classification,
fresh actual HP and allowances, failed legality/actions and the two-action cap.
They also reach the real selector, roster, role purchases, banking and risk rule.

The v3 macro profile is `e0d02b974fcc`. Versus04, source choices differ on 70.2%,
banking on 8.5%, risk on 1.5%, roster entries on 80% and actual purchase plans on
28.9%. Coarse three-stage build profiles match. Profiles omit inventory
observations and combat, so the native continuation remains necessary.

Development evidence is retained under `.cache/arena/`. The first broad source
included spell Monster bonuses but used raw Strength for follow-up basic counts;
the corrected camp-damage helper and boosted-boundary regression repair that
idea-correctness defect. A subsequent full panel found four Prism exhaustion
events on lanes. Trace showed a banking route resuming after suspension: empty
bags paid for irrelevant altar/stock observation, and conversion rebuilt one
approach bitboard per altar after the threshold menu. Empty bags now reset the
phase immediately; position/stat/affordability queries and static altar geometry
are cached. All eight affected lane regression keys are clean in
`h12-05-healthfix2`, maximum fuel 81,413. Superseded diagnostic runs do not count
toward final acceptance.

The active default is enabled as a healthy, distinct experimental losing
controller. It is not a strength improvement or a release candidate.

- `.cache/arena/sweeps/h12-05-complete`: all 40 pool maps, both sides, four
  opponents, 320 paired keys per arm. Active 110-210 versus frozen 162-158;
  flips +8/-60, McNemar p<.001, paired gem-margin difference -228.2 +/-21.7 SE.
  Active results: Fundamentals 8/80, Tempo 30/80, main 0/80, Concentric 72/80.
  Main is exactly one quarter of the panel, snapshot `52f589af9629`; it changed
  externally between development runs, so only same-run comparisons apply.
- `.cache/arena/runs/h12-05-complete-confirm`: all 17 different legacy maps,
  both sides, three different opponents: 99-3. Starter 32/34, Banked Vanguard
  34/34, Bargainer 33/34. This is transfer against those weaker opponents.
- All 422 required final active games have zero faults, exhaustion, errors,
  zero-clear stalls or suspension. Maximum ordinary-turn fuel is 91,211 in
  the pool and 84,024 on legacy maps. This covers the measured games, not every
  possible state. Frozen control retains eight historical stalls.
- Sparse logs identify work-phase entry in at least 48/320 pool games, useful
  stock casts in at least 31/320 and an actual stock-enabled kill in one game.
  These are logged coverage lower bounds, not exhaustive action counts.
- `arena test heuristics/working-inventory` passes 2/2. The strict v3 profile
  passes 5/5; its final policy report is
  `.cache/arena/heuristics-profiles/20261021-231647-80092-736026900/report.json`.

Measured active source `83e65cbfce23` has WASM SHA256
`1c19d8153bc5d4bfecb05c7c4cfafad198adc06906b3260f7c9848b9169bff82`.
Promoted default source `43ceb52fc5ee` builds to exactly those bytes. Explicit
off source `254c9febb97c` remains byte-for-byte frozen fundamentals, SHA256
`ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`.

Exact-source native continuation: final pool replay
`01broken-curve_tempo_working-inventory_a5559478.repl26a`, side B, Prism physical
actor 3 (champion identity 6). At round 365 a shared source 15 capture grants her
7 gems, raising observed Focus from 112 to 122.5. The same turn enters work and
casts slot 1 AOE at (19,22), killing two 120-HP gold clusters. Loaded raw damage
is 122.5; the empty-stock 112 forecast cannot kill them. At round 373 source 17's
shared capture grants Prism 20 gold while her 7 gems and 122.5 Focus remain intact.
At round 375 the native tribute transition reduces her bag 7->0 and Focus 122.5->112,
raising team B score 61->68. This verifies capture stock -> useful cast/clear ->
gold capture -> bank. The continuation is gold income, not gem replenishment.
The game was lost 176-1003, so this is a mechanism witness without a strength
claim. Native compact receipts:
`.cache/inventory-review/complete-native-witness.json`; full native state changes:
`.cache/inventory-review/complete-broken-curve-b-receipts.json`.

Cashability uses cheap distance/work estimates, not a guaranteed walkable bank
route or a complete forecast of the source's eventual payout. Blocked banking
can leave carried cargo in liquidation while productive fallback clears another
source; incidental loaded casts during that journey do not prove deliberate
retention and are excluded from the stock-work action log. Saved-basic counts
are forecasts, whereas observed HP/death establishes the chosen action's actual
effect. Retaining gems is opportunistic and short-lived, not a general claim
that carrying more cargo improves income or strength.

Next-bot lesson: a measured temporary threshold benefit does not establish
better permanent income. Keep resource windows short, observe their actual
expiry and cash conversion, and make their observation and static route setup
cheap enough that the chosen action still executes.
