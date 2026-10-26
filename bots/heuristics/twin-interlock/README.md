# Twin Interlock

Bot 12 studies linked death and private wealth. A Twins draft creates two
independent bodies with separate turns, bags, purchases and ability allowances.
Either death kills both and loses both bags. The controller therefore makes
one body's fresh risk and the other body's dated exposure report interact
with production and personal conversion. This is a heuristic experiment;
strength improvement is not a prerequisite for retaining a distinct healthy bot.

## Decision procedure

The roster order is Twins, Willow, Tavi, Ryth, Marcella, using append-only suite
draft indexes `11,7,0,9,4`. Only Cassi and Issac use the interlock. Every other
drafted actor independently selects, works, captures, shops and converts; it
neither clears the envelope nor participates in a job allocation protocol.

The two private role programs share this order of decisions:

1. Observe actual own life, bag, HP, allowances and immediate visible exposure.
   Own unsafe status dominates every partner report. A live stunned actor with
   no movement, attack or cast allowance performs no game action. Dead actors
   retain legal shopping. Safe ongoing channels remain intact.
2. A current or previous round report of an unsafe live partner causes
   withdrawal. Positive partner cargo prohibits provoking a dangerous camp,
   attacking enemies or entering visible exposure, including a zero damage
   stun or dash. A safe own bank or harmless production remains possible.
3. Every positive own Twin bag forces individual conversion, even one gem.
   Two loaded bodies therefore each convert instead of waiting for a larger
   batch. Starting tribute keeps the report positive; only a later actual
   self-observed zero bag clears it.
4. A current round safe zero report permits ordinary work. A previous round
   zero is `Unknown`: capture may have loaded the sender since that report.
   Unknown permits harmless production immediately, up to four turns of
   movement toward a recent reported position, and after six turns conservative
   own work at at least 82% HP, no visible enemy, and fully observed estimated
   source loss at most 12% maximum HP. Prior zero reports do not reset that age.
   There is no permanent wait for an absent or silent partner.

Reports select a mode; they do not share vision, allocate a holder/clearer
contract, promise healing, supply current partner HP, or authorize hidden
queries. A current zero remains a dated observation and cannot guarantee that
the engine has not changed the bag later in the round. Fresh own risk and
fresh source observation remain necessary.

Within permitted work, the selector balances reward per travel/work/capture/
conversion cycle, private cash needs, bag-sensitive exposure and commitment.
Cassi values her ranged spell opportunities and buys Monster Focus, Jungle
Health, then Power Focus. Issac uses melee walking plus ordinary reset and
buys Monster Strength, Jungle Health, then Power Health. Prices, readiness,
payloads, target categories and legality come from the SDK. Cassi's damage
spell excludes ordinary walking; her pure stun is compatible. Issac's reset
has no range mask; his zero damage dash uses a visible unoccupied conservative
path and a separate exposure gate. One successful cast consumes the shared
ability allowance across slots.

Unknown sources use public queried design HP and Strength as coarse workload
and affordability estimates. Immediate exposure uses actual visible awake
camps and enemies in range. These are separate quantities. A fresh refusal
quarantines that source for 48 rounds and replaces the incumbent; a later
attempt can use changed health, gear or source lifecycle. If no full source is
affordable, the selector retains a bounded partial work fallback. Harmless
sources provide a personally funded route to initial equipment.

## Packet and lifecycle

The eight byte envelope has one four byte owner record per body: Cassi owns
bytes 0–3, Issac 4–7. Each owner freshly reads eight bytes and writes only its
own four. Explicit unsigned little endian shifts encode round 11 bits, x/y
7 each, alive/cargo/self-safe one each, channel two, epoch one and valid one.
The queried map dimensions must be at most 100, maximum rounds at most 2047,
and capacity at least eight. An all-zero envelope is unknown, never an observed
empty bag. Future, stale, invalid and out-of-bounds records are rejected.

Each instance observes its own deaths and later resurrection, toggling its own
epoch. Following an observed linked death, acceptance requires a fresh report
from the new cohort and a changed peer epoch when the old epoch was observed.
The partner handle's public hidden-safe `is_alive()` must also be true. Same
round numbers do not establish simultaneous respawn or linked channels.
Partner handles are discovered through all allied champions and cached by
physical type (Cassi 3, Issac 4); hidden HP, positions, state, items and bags
are never queried. Packet coordinates remain dated reports, not visibility.

The protocol can become unknown after a skipped low fuel publication or a
missing lifecycle observation. Conservative productive fallback is deliberate;
the one-bit epoch and short report lifetime are not a complete distributed
state proof. They do not claim current shared knowledge.

## Execution and fuel

The local executor reuses the suite's routing, shop and capture mechanics,
including portions of Act First's mechanics fork, while replacing its entire
proposal/audit controller with the linked risk modes and role programs.
There is no post-action challenger, saved proposal or scalar audit here.

A complete static walking component admits capture pads before selection.
Recall or resurrection outside the cached component rebuilds it. A partial
search is never a certificate of permanent disconnection. Source routes use
an admitted native field or at most eight cheap neighbors; the static setup
turn forces a cheap continuation to avoid two fields in one turn. Banking
filters altars to a reachable adjacent component tile. Visible exposure is
checked separately from a geometric dash mask.

All visible empty member pads are retained even while another member lives.
A fresh physical reward hold is established before redirection or banking.
Its elapsed capture watchdog survives empty enemy vision and expires to useful
departure; actual departure, death or a changed lifecycle resets it. Source
changes and ordinary movement do not reset persistent no-work age.

Publication needs 42K remaining before refreshing actual local safety and
writing four bytes. Static component setup needs 80K. Low fuel paths can skip
the report and all formatted diagnostics; that leaves the partner unknown
instead of spending the action reserve. Sparse normal mode receipts retain
the measured packet maximum and fuel at the formatting argument. Whole native
yield-tail accounting must include actual draft picks and actual physical
bodies, since Twins adds a body without adding a pick. Measurements are scoped
observations, not universal SDK cost bounds.

## Fixtures and development failures

`arena test heuristics/twin-interlock` passes the common case and the focused
production-helper fixture, 2/2. The fixture covers explicit packets and fresh
reads, owner-only writes, current/prior/future/stale/invalid records, hidden
alive and separate resurrection, cohort epochs, tribute-start positive cargo,
actual zero release, both loaded actors banking one gem, prior zero capture
timing, bounded unknown fallback, hostile zero damage actions, landing
exposure, cast/walk compatibility, component refresh, safe channel continuity,
source quarantine, action allowance and fuel admission boundaries. Packet,
lifecycle, mode, allowance and guard cases call the runtime's actual helpers.
Report-construction cases additionally check the actual-bag rule. The fixture
is not a complete SDK transition simulator or native policy benefit proof.

Strict `heuristics-profiles-v3` passes all twelve production policies, all
physical identities 0–12, and the 325 queried purchase-plan cases per policy.
Twin Interlock's fingerprint is `9e6033040eba`; macro differences alone do not
prove the linked gameplay mechanism. The final report is under
`.cache/arena/heuristics-profiles/20261022-062111-61628-699986200/`.

Initial 12 game smoke panels were technically clean but weak. The first two
80 game Starter screens exposed two real Shortcut stalls. Native inspection
showed low HP repeatedly cancelling and restarting safe recall at timer four,
and healthy actors repeatedly approaching an unaffordable incumbent with no
income. The repair preserves safe channels, quarantines refused work and uses
queried public design workload for initial admission. The targeted repair
won both sides and averaged 68 banked gems. The fresh broad screen
`h12-12-screen3` was 54/80, with zero faults, exhaustion, error lines or raw
zero-clear flags; maximum observed fuel 69,146.

The first complete pool sweep `h12-12-final` is superseded: it had 270 engine
44 error lines from acting without an allowance after stronger opponents'
stuns. No faults or exhaustion occurred, but this blocked promotion. A fresh
allowance guard and the prior-zero correction passed twelve affected regression
games in `h12-12-guardcheck`, all clean. Its earlier 80/102 legacy confirmation
cannot substitute for confirmation of the repaired source.

## Final evidence

The exact repaired source is measured in
`.cache/arena/sweeps/h12-12-final-health` (640 paired pool games) and
`.cache/arena/runs/h12-12-final-health-confirm` (102 fresh legacy games).

| Pool opponent, 40 maps and both sides | Active | Same-run frozen control |
| --- | ---: | ---: |
| Fundamentals | 7/80 | 40/80 |
| Main | 0/80 | 0/80 |
| Tempo | 26/80 | 48/80 |
| Concentric | 54/80 | 74/80 |
| Total | 87/320 | 162/320 |

The paired result is worse: flips +6/-81, McNemar
`p=7.0345e-18`, paired gem-margin difference `-369.9 +/- 25.2 SE`.
Main is exactly one quarter of the panel; no Main code was inspected or
strength-tuned. Fresh confirmation is 80/102: Starter 21/34, Banked Vanguard
27/34, Bargainer 32/34, over all 17 legacy maps and both sides. The opponent
snapshots are pinned by each run; Tempo changed externally between the
superseded and repaired panels, so cross-run differences are not attributed
to the repairs. Only the repaired same-run paired result is used above.

All **422 exact final active games are clean**: zero faults, fuel exhaustion,
error lines, suspension-at-budget or raw zero-clear flags. Maximum observed
fuel is 68,789 pool and 69,201 legacy. The unchanged frozen control retains
eight known zero-clear flags. These health repairs and the new controller form
one bundle; there is no isolated interlock-versus-repaired-executor comparison.
The enabled default is retained as a distinct experimental losing controller,
not a strength improvement or release candidate.

## Bounded normal native evidence

The official decoder observes the exact final source `8202c6378a93` in one
normal Collision A game against Fundamentals, rounds 365–405, separately for
global actors 0 (Cassi) and 1 (Issac). Both 41-round observation windows finish
with zero query errors. The bounded scout examined twelve final replays.

Cassi is at 600/600 HP when she accepts Issac's round-376 positive cargo report
at 377. The production mode is Protect; her actual action moves `(20,21)` to
`(20,20)` at input 4760–4761, followed by `(20,19)` and `(19,18)` on the next
two turns, with no harmful work. This is the reachable partner-triggered gate
on a healthy body, rather than a self-injury retreat.

Issac receives an actual camp payout from `g_885` at round 376, input 4748,
substep 18–19: twenty gold and nine gems. Tavi receives the other twenty gold
and nine gems; this is shared capture income, not a solo-clear claim. Issac
starts his own tribute at 378, input 4778–4779. His bag remains nine through
the timer reaching zero. Cassi acts before his next owner start at 379 and
continues withdrawing. Issac's owner start 379, input 4792, substep 6–7 clears
his bag nine to zero and raises team A score zero to nine, with HP unchanged.

Cassi's turn 380 accepts the actual zero report from 379. Because it is prior
round, the mode is conservatively Unknown. She resumes harmless production:
an actual spell at input 4804–4805 removes the 120 HP gold camp at `(22,16)`,
then her basic at 381 reduces another camp from 120 to 90. Harmless work was
also allowed during Protect: this sequence proves activation and productive
continuation after conversion, not that zero release caused the cast or that
the gate saved cargo. It supplies no causal strength benefit.

The exact receipts and source-bound hashes are in
`.cache/twin-interlock-review/native/health-native-summary.json`, SHA256
`b2c32a13d8855651ebe0274121e67e136df8a29922b65b50276bd08da4db9bf8`.
The earlier independent native lifecycle observation
`.cache/twin-research/lifecycle-receipt.json` verifies linked death, separate
owner-start resurrection and independent channels. Both bags were zero in
that mechanical window, so it establishes no positive cargo-loss example or
policy benefit.

## Fuel and executable preservation

Across all 320 final active pool games, 22,773 retained normal Twins receipts
use actual per-game draft pick counts 3/4/5 and physical body counts 4/5/6.
The maximum completed packet interval is 22,714 fuel. The entire
formatting-argument through native yield/flush tail is 10,197–12,975, with zero
ordinal or accounting failures. One measured packet interval plus its tail
has a conservative observed envelope 33,262, below the 42K publication guard.
Two packet intervals plus the tail have envelope 55,976; the early read is
already spent before late-publication admission, so 55,976 is not compared
against that 42K guard. Packet maxima cover either completed read or publish,
not separately identified samples. Repeated cumulative maxima are not new
independent measurements. Low-fuel paths emit no receipt, small wrapper
instructions are not all included, and these observations do not prove a
universal engine cost bound.

`.cache/twin-interlock-review/fuel/final-health-fuel-tail.json`, SHA256
`193e39ccc2e0639daec469ed48710e6de01243c82bd1bae456a09302bfacdb52`,
pins the completed manifest and all measured receipts.

After the exact healthy comparisons completed, `HEURISTICS_ENABLED=1` was
promoted. Default snapshot `54a45fe46bee`, raw and injected WASM, is exactly
equal to the actual completed active snapshot `8202c6378a93`:
`36be7fda90e1eb1658aef86ecd762a0c589a5553517ca4441e27cdcd9aa95cb0`.
Explicit-off snapshot `31a7a85b7982` and measured base `be501c1355b5`, raw and
injected, all equal frozen Fundamentals:
`ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`.

`.cache/twin-interlock-review/final-proof.json`, SHA256
`298f480219f336a072acb42765ea832d9cdd4f843974fe1b49040a8a132c3a79`,
reads the actual completed sweep, pins all 640 paired and 102 fresh rows,
records source-closure and fixture hashes, and checks both byte equalities.

The next-bot lesson is that linked risk reports can be legal, active and fuel
safe while a conservative two-body production program loses badly. Current
zero versus prior zero matters because capture precedes owner turns. Preserve
safe recall, replace freshly refused sources, distinguish public workload from
immediate exposure, and account for physical bodies separately from picks.
No online action, submission, release or account change occurred. All owned
builds, games and native jobs are closed; bot 13 was not constructed here.

## Durable ladder placement

Root's first placement, `20261022-062634`, contains sixteen subject games,
8–8, all clean. All eight flagged games in the forty-eight-game placement are
outside this suite: four Tempo, two Banked Vanguard and two Bargainer cases.
These opponent flags are not subject health failures.

The coarse nearest style is Altar Options at distance 0.37, with no head-to-head
record and no convergence or close flag. Sixteen placement games and this
style distance are descriptive; they supply neither an independent strength
estimate nor evidence that the two controllers are strategically equivalent.

Durable records are `results/ladder/games.jsonl` and
`results/ladder/leaderboard.md`; all forty-eight run rows are retained in
`.cache/arena/ladder/20261022-062634/games.jsonl`. Root independently verified
the eight source/WASM hashes and completed comparison manifest. This follow-up
changed documentation only and started no build, game or native job.
