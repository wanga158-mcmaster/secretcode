# Clockwork

Clockwork keeps a circuit of up to three camps, uses its own visible respawn
observations (stale after 16 rounds), defers a camp not due at arrival, and waits
at most two rounds. The roster, safety checks, visibility, respawn rules and
circuit thresholds remain in place.

## Native activation repair, 2026-10-23

The current engine 1.0.3 baseline on LostWorld initialized all five actors but
produced no damage, clears, income or banking on either side against productive
frozen Fundamentals. Ordinary menu ranking spent down the fuel needed by the
circuit gate, so an affordable circuit was never constructed. Larger maps also
exhausted during initialization. Later native panels exposed expensive repeated
component checks, unreachable circuits stranded between the four-turn member
limit and eight-turn rebuild limit, contested capture holds, and eager telemetry
formatting after the detail cap.

Four independent switches in `src/profile.h` were initially **0 by default**
through the full native panel and independent review:

- `CLOCKWORK_PLANNING_REPAIR`: finish the complete ordinary menu over bounded,
  stationary owner turns; construct the circuit on a fresh turn. Preserve partial
  camp work and do not act on a partial menu.
- `CLOCKWORK_STARTUP_REPAIR`: stage metadata, terrain component and group setup
  over bounded turns; accept a group when any public spawn belongs to the home
  component, rather than requiring its anchor to be reachable.
- `CLOCKWORK_REACHABILITY_REPAIR`: refresh the component certificate after a new
  circuit or failed navigation, with a fresh action turn. Recertify a fully
  blocked circuit until the existing rebuild threshold can be reached; fresh
  reachable members reset their counters. Movement still refreshes blockers.
- `CLOCKWORK_CONTINUATION_REPAIR`: reuse inventory only within one refresh, yield
  before costly work, give periodic summaries a fresh turn, and gate detail
  formatting before its exhausted cap. After personally observing an enemy hold
  the same contested reward spawn for the queried capture interval, use the
  existing safe departure and replan; do not change uncontested capture priority.

`CLOCKWORK_FUEL_DIAGNOSTICS` remains 0. With all repair switches off, raw and
injected modules separately equal incumbent `0a7c174ec947` byte for byte
(`8f1f733604bc88aee71732f944373287741b66b331559f0ff031616c21a0f177`).
The final experimental ON snapshot is `8c453d641c32`, raw/injected
`49d71e57972a0cf1386281e9d30c972170a4e7500eb51d1ee0d610426424564b`.
Control `cd234e0283bd` has all four repairs and only `core_enabled=false`.

The same-run 40-map, both-side Fundamentals baseline had 59 own health failures,
51 fuel exhaustions and 58 zero-clear cells. The final ON 80 cells have no own
faults, errors, exhaustions, stalls or zero-clear cells; maximum fuel is 86,590.
Total clears/banked gems/earned gold changed from 495/1,977/24,952 to
3,163/19,309/167,537. These are descriptive paired activity measurements, not a
strength claim. The original full-match panel is **78/80 healthy**, with two
opponent stalls on Yellow Brick Road retained as failures. A separately qualified
technical-health matrix uses 78 Fundamentals cells and main-v28 for those two
map/sides; both teams are healthy in all 80. Fresh main-v28 LostWorld A/B are also
healthy (60/43 clears, 1,005/763 banked).

In final LostWorld A/B, all ten actor instances bank, later deal monster damage,
clear a camp, receive a paid capture from that same group, and bank again. Native
matched control traces show Tavi0 cast killing camp32 at round100 on A and Tavi5
killing camp28 at round95 on B, while the control explicitly ends the turn. The
queried initial world, actor before-state and preceding event projections are
equal within complete bounded windows (3,613/5,321 preceding events). This does
not claim private-memory, fuel or raw-world equality.

Limits remain explicit: 12/80 Fundamentals cells bank zero, one earns zero gold
(Corridors B, 46 deaths), and five of 400 actor instances deal zero monster damage
(Vendetta on Road of Riches A/B, Tavi on Boss Battle B, Marcella on Corridors A/B).
Their causes have not been classified as new living-ready loops. Fresh main-v28
Yellow Brick Road A/B bank zero; B earns zero gold. Technical health does not
establish every actor's production or every map's economy. No admission, online
or strength conclusion follows. Historical partial repairs and their failures
remain in the scratch evidence rather than being presented as current health.

Exact source plans, build/OFF proof, current modules, native cycle/control proof,
80-cell panels and retained evidence are indexed at
`results/population/clockwork-activation/index.json`.

The integrator approved mechanical default promotion after independent review
`bf543ece66cdc62346dd53b5f291eb7a0ba36c6abec65641cae058a7316f9bf7`.
Only the four repair defaults change from 0 to 1; diagnostics stays 0. Promotion
requires a fresh no-override default build matching the tested ON body in each
role, and a fresh explicit-OFF build matching the incumbent in each role. The
byte bridge and final result are recorded separately in
`results/population/clockwork-activation/promotion.json`; the original review
index and its failed whole-match panel remain unchanged. The economy and
per-actor limits above still apply, without a strength or admission claim.

## Boss Battle refusal classification, 2026-10-23

The stronger main-v28 census selected B-side Tavi5's zero-production match.
Its final native window contains a respawn at 696 and 25 full-health ready
end-turns on the same tile. This did not by itself establish a missing safe job.
The complete public terrain component contains 1,896 tiles and reaches all 31
groups; connected geometry is not a safe route or personal payout certificate.

A narrowly gated `CLOCKWORK_COMPONENT_TRACE` diagnostic defaults to 0. Its
corrected ON snapshot `e18abcc4d889` reproduces the **entire** original native
replay payload byte for byte. It shows a sole circuit member `g_628`, stage
eligible but unaffordable under the unchanged model (public HP 5,040, incoming
120). On each non-summary turn it refuses that member, records another nominal
lap, and returns without movement. The affordability refusal is active; the lap
counter is not evidence of paid production. No personally visible payable
alternative or safe route was certified.

A bounded whole-circuit replan prototype `2836fc5869b1` was tested on Boss Battle
and LostWorld, both sides against frozen main-v28. All four matches were healthy;
LostWorld's complete physical replay payloads stayed identical. Boss Battle B
team clears/gold rose from 12/792 to 17/1,272, but Tavi still dealt no monster
damage and earned/banked nothing, while deaths rose from one to four. Forcing a
new route did not establish useful personal work. The speculative replan was
withdrawn from working source; its exact snapshot and negative games remain.

The revised decision is to release this context from forced-activation repair.
Revisit only with a certified safe current opportunity or an actual counterexample
to the affordability model; do not assume every possible alternative is unsafe.
The default diagnostic-OFF source is `b7a9f2e859c2`, with both payload roles exactly
equal to promoted `b29893a0610d` (`49d71e...`). The existing four-switch OFF recipe
is `271e25ed294d`, both roles still exactly equal to original `8f1f733...`.
The four reviewed repair defaults remain 1; both diagnostics remain 0.
Evidence: `results/population/clockwork-refusal/index.json`.

The stronger census's 117/241/217 actor renewal counts require a strictly positive
personal **gem** payout (`campGroup` gemCollection value > 0) between two positive
bank events. Zero-value receipts and gold-only payouts do not count. This is
clarified without changing the previous census publication or rerunning games.
