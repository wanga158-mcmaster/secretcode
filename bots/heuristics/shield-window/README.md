# Shield Window (06)

This controller treats temporary absorption as a short duty cycle. Heart of Ruin
and Varg prepare a fresh self-shield, make at most two owner turns of physical
camp progress, withdraw from threat, and renew before a freshly admitted return.
Injured camp HP persists while asleep. A pulse therefore has value without being
labelled a complete clear of a large source. Willow, Ren and Tavi provide ordinary
productive fallback; Willow healing and Heart's dash are not part of this flow.

The selector ranks ready captures first, then short-horizon fractional progress
per approach and switching time, with coarse exposure downside and cashability.
Full affordable jobs are considered before partial work. Shield-role partial
jobs require a ready predicted shield and independent actual-HP exit reserve;
unshielded roles retain bounded normal partial work when no full job is affordable.
This is neither working-inventory's retained-gem spell threshold nor the prior
splash/payback source screen. Progress buys a short execution lease; it cannot
extend the two-turn pulse. Renewal may use harmless work or bank rather than wait
forever at the injured camp.

The production `execute` helper owns prepare/work/withdraw/renew transitions.
Its SDK adapter rechecks target visibility, source freshness, action allowance,
local clearance progress, actual HP, and current shield value and duration.
A successful cast is followed by a fresh owning buff snapshot. Forecast absorption alone never
admits an attack. Remaining duration one, insufficient value, stale observations,
no progress, forced cash conversion or elapsed pulse forces exit. A pulse permits
one shield cast and one basic per owner turn, and no speculative dash or reset
chain. Shield value is never added to health. Heart's missing-health damage
forecast uses actual HP; live equipment stats and the target-specific Monster
bonus are accounted for once.

Fresh complete vision with zero incoming permits normal physical continuation
without spending or waiting for a shield, including when the source originally
contained damaging monsters. Stale or hidden zero-incoming estimates cannot
take that branch. Varg refuses primaries whose splash could wake another public
damaging source. All already-awake visible camps enter actual window reserves.

The five draft priorities are Heart, Varg, Willow, Ren, Tavi (suite indexes
6/1/7/10/0). Shield roles establish five Monster Strength shards, then Jungle
Focus and Power Focus for absorption throughput. Willow starts Monster Focus;
the other fallback roles start Monster Strength. Non-shield roles use Jungle
Health, and all actual purchases query SDK prices and legality. Carried cargo
increases the actual-HP reserve. Eight gems, a nearby altar, low health, renewal
with at least four gems, a winning bag or the tribute deadline trigger banking.

The local executor/frontier copies completed05's bounded public metadata,
separate topology turn, static altar approach mask, routing, capture and
witnessed-death mechanics. A shared capture watch times the actual hold even
without visible enemies; missing payout after queried capture time plus two
rounds excludes that source temporarily and makes the worker leave. Departure,
death and a changed source lifecycle reset the watch, not empty enemy vision.
Its inventory/loaded-spell brain was removed. The off arm directly includes
frozen shared control. All turn work is bounded and
unexpected engine errors log `error` at the turn boundary before yielding.

## Evidence and limits

Production fixtures cover the actual phase helper, including forecast/actual
shield mismatch, failed or spent casts, expired buffs, visibility/freshness,
escape and actual-HP reserves, no progress and hard pulse expiry. The positive
flow casts, absorbs damage without healing, attacks twice, withdraws, renews,
re-enters the same injured source, clears, and reaches simulated capture and
production banking. Separate cases exercise the production selector, role
purchases, roster, banking, risk, one-cast limit and same-turn attack cap.

Strict macro profile v3 `dce101c6246c` differs from working-inventory on 54.2%
of source choices, 7.0% of banking, 1.1% of risk, 100% of roster entries and
16.9% of actual gear plans. The coarse three-stage build profile matches.
The profile omits combat and movement phases; native continuations are required.

The first eight-game smoke was clean but its local escape test used expanded
threat range for every adjacent candidate and rejected valid melee exits. It now
checked actual next-tile attack exposure while the admission estimate retained
the conservative one-tile threat margin. That eight-game smoke against
Fundamentals, both sides on broken-curve, lanes, islands and boss-battle, is clean
(maximum ordinary-turn fuel 90,434). Smoke outcomes are not strength evidence.

The first fresh legacy run exposed six elkring zero-clear stalls. Its ranged
Elks could not be escaped in one step, so an immediate exposure-decrease test
refused every shield pulse. Local weighted clearance now permits a multi-step
exit; independent HP pays for its observed attack-range gap and a boundary turn.
Three unshielded roles also lacked any partial fallback on this all-large-source
map. Restoring normal bounded partial work lets them contribute without waiting
for an ability they do not have. Eight elkring regression games are clean.

The stopped first pool run also caught Varg exhausting on rows at round 1809,
after movement, new vision, shield observation, cast and attack stacked on the
same turn. Threat position/range/Strength is now cached per unchanged action
epoch, including recovery's local neighbor search. Shield preparation after a
walk executes on the next fresh owner turn under its existing source lease.
Sixteen cache regression games are clean. These superseded sources do not count
toward final acceptance.

An independent code review found that a source's original damage classification
could keep rejecting its harmless remaining work. The same production phase
helper now admits a fresh zero-incoming continuation without another cast;
hidden/stale/forced cases and the same-turn cap have focused regressions. Varg's
neighbor-source awakening guard and all-visible-awake threat reserve keep that
continuation from silently borrowing absorption for an unbudgeted nearby group.

The active default is enabled as a healthy, distinct experimental losing
controller. It is not a strength improvement or release candidate.

`.cache/arena/sweeps/h12-06-final` covers all 40 pool maps, both sides and four
opponents, with 320 identical paired keys per arm. Active 109-211 versus frozen
162-158, flips +9/-62, McNemar p<.001, paired gem-margin difference
-294.8 +/-22.5 SE. Active results: Fundamentals 12/80, Tempo 27/80, main 0/80,
Concentric 70/80. Main is one quarter of this panel, snapshot `41fae4ff57a6`;
Tempo is `3823c3e77561`. Both changed externally between development runs, so
only the same-run paired comparison supports the bundle result.

Exact-source fresh confirmation `.cache/arena/runs/h12-06-final-confirm`
covers 17 legacy maps, both sides and three new opponents: 83/102 (Starter 26/34,
Banked Vanguard 27/34, Bargainer 30/34). All 102 games have zero faults, exhaustion,
errors, zero-clear stalls or suspension; maximum ordinary-turn fuel is 87,294.
All 320 final active pool games are also clean, maximum fuel 96,980. Thus all
422 required active games have zero faults, exhaustion, error lines, zero-clear
stalls or suspension. Frozen control retains eight historical stalls. This
certifies the measured coverage, not every possible state or a complete safe
escape route.

Sparse final pool logs record casts in 264/320 games, actual absorption in
263/320, work in 265/320, withdrawal in 270/320 and renewal in 271/320; each
appears on 36/40 maps. These are capped-log lower bounds, not opportunity rates
or counts of complete productive cycles. Work includes harmless zero-incoming
continuation. The separate native receipt below establishes one complete
productive sequence. `.cache/shield-review/final-coverage.json` holds the scan.

`arena build heuristics/shield-window` and its explicit-off build pass;
`arena test heuristics/shield-window` passes 2/2, including the shared capture
watch tests. Strict v3 profiles pass 6/6 policies; final report:
`.cache/arena/heuristics-profiles/20261022-000103-78956-490331500/report.json`.

Measured active source `788cbaaf6512` and promoted default source `4a232ca3516c`
have identical WASM SHA256
`2bd4416371e5fa6ed16ad30dcfddface7bbb63f5f27d54164acfb5b5cc093b32`.
Explicit-off source `d2d250cfe9d8` remains byte-identical to frozen Fundamentals,
SHA256 `ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`.

Exact-source native continuation (active `788cbaaf6512`):
`.cache/arena/runs/h12-06-final-witness/games/elkring_shield-window_starter-cpp_8c0efff8.repl26a`,
side A, physical Varg identity 10 / native actor 1, source 0 (`g_183`). Round 10
casts an actual 250 shield, duration 5, at actual HP 820 and damages the Elk at
(3,6) from 2200 to 2160 HP. Round 11 consumes shield 250 to 190 while actual HP
stays 820, and does a second work turn. Round 12 withdraws with two pulses used;
native positions walk out to spawn, with renewal at round 18. Round 23 casts a
fresh shield and kills the same now-injured Elk at 11 HP. All source members are
dead by round 24. Shared capture at round 30 gives Varg 55 gold and eight gems;
his own bank completes eight to zero at round 50. Allies contributed substantial
damage during withdrawal, so this establishes useful partial work and safe
continuation, not a solo clear or better income. The game won with 66 clears and
no health flags, maximum fuel 72,886. Read-only native receipts, including direct
shield buff snapshots: `.cache/shield-review/final-receipt-v2.json`.

Limits: source forecasts are coarse, local escape is not a complete safe route,
and a productive shield pulse can still have poor long-run economics. Shared
capture payout and an opponent's capture/respawn can erase the value of a return.
No claim that temporary absorption improves permanent income follows from a
single observed pulse. Main is only one quarter of the comparison panel.

Next-bot lesson: a real absorption window still does not establish efficient
permanent income. Repeated interrupted source work needs fresh injury/lifecycle
checks, an exit that actually progresses, and a cashable continuation; shared
ally damage must not be credited as solo productivity. Test route and renewal
memory as its own decision rule, rather than expanding this temporary-window
controller into a universal model.
