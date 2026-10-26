# heuristics: useful approximations under partial information and finite fuel

## Doctrine
Which combinations of cheap heuristics make useful decisions when exact search, complete
information and perfectly calibrated forecasts are unavailable? Sixteen controllers are built
and measured **sequentially**. Each owns its decision procedure, financial rule and risk rule;
rosters and purchase priorities may differ. Carry measured lessons forward, never replace the
suite with copies of its strongest member.

`common/` supplies copied fundamentals mechanics: legality, routing, capture/channel lifecycle,
shopping, coarse feature extraction and the turn-boundary error catch. Later bots own local
executor forks when a different combat or production state machine is the experiment. Each
bot owns its policy and state. The selector invokes that policy over budgeted candidates and
imposes no shared score. This deliberately approximate suite differs from events' exact event arithmetic
and frontier's complete formal programs. Multiple heuristics interact in every controller.

New strategy starts disabled during construction. Explicit `HEURISTICS_ENABLED=0` replays
frozen fundamentals; completed experimental defaults select their measured active arms.
Measure active arms in the same sweep before promotion. The baseline is a developmental control, not a seventeenth
bot. No release or online action is part of this work. This is the twelfth development suite
(anchors are references). The integrator owns the suite index and needs to add it there.

## Bots
| Bot | Decision rule that makes it play differently | State |
| --- | --- | --- |
| 01 satisficer | Lexicographic useful-income/survival aspirations, progress-based commitments, congestion avoidance and bag-sensitive retreat | Active: 162/320 pool, 102/102 legacy; all 422 active games clean; no detected strength improvement |
| 02 [renewal-regret](renewal-regret/README.md) | Three-scenario opportunity regret, renewal capacity, travel batches, health-first finance and witnessed-death exploration memory | Capture repair: 114/320 pool, 99/102 legacy, 53/80 inventory matchup; all 502 active games clean |
| 03 [reset-pursuit](reset-pursuit/README.md) | Kill/reset/retarget combat flow, quick completion and cashable rewards, strength-first finance and smaller liquidations | Active: 158/320 pool, 102/102 legacy; all 422 active games clean; no detected strength difference |
| 04 [splash-harvester](splash-harvester/README.md) | Clipped joint-hit geometry, avoided attack windows, crowding and role-dependent equipment | Active: 126/320 pool, 101/102 legacy; all 422 active games clean; distinct losing bundle |
| 05 [working-inventory](working-inventory/README.md) | Prism combat stock, discrete spell thresholds, replenish/work/liquidate phases and cashability stopping rules | Active: 110/320 pool, 99/102 legacy; all 422 active games clean; distinct losing bundle |
| 06 [shield-window](shield-window/README.md) | Temporary absorption, bounded work/withdraw/renew duty cycles and persistent partial-source damage | Active: 109/320 pool, 83/102 legacy; all 422 active games clean; distinct losing bundle |
| 07 [learned-circuit](learned-circuit/README.md) | Repeated private production circuits, personal payout learning, bounded stop replacement and lap-end liquidation | Active: 111/320 pool, 99/102 legacy; all 422 active games clean; distinct losing bundle |
| 08 [witnessed-handoff](witnessed-handoff/README.md) | Comparative advantage, one bounded clearer/holder contract, witnessed overlap and a personally funded outside income option | Active: 118/320 pool, 99/102 legacy; all 422 active games clean; distinct losing bundle |
| 09 [altar-options](altar-options/README.md) | Private remove-or-divert conversion programs, executable lock-hit service costs, switching costs and bounded progress | Active: 124/320 pool, 102/102 legacy, 102/160 locking panel; all 582 active games clean; distinct losing bundle |
| 10 [active-surveyor](active-surveyor/README.md) | Pivotal information value, one bounded own-ward probe, replacement opportunity cost and observation-driven executable work | Active: 118/320 pool, 102/102 legacy, 75/80 pool screening; all 502 final subject games clean; distinct losing bundle |
| 11 [act-first](act-first/README.md) | Safe incumbent action before a scalar challenger audit, next-turn proposals with fresh vetoes, switching friction and a minimal low-fuel tail | Active: 102/320 pool, 100/102 legacy; 422 games without technical failures; two zero-clear combat losses; distinct losing bundle |
| 12 [twin-interlock](twin-interlock/README.md) | Linked-life loss reserves, dated cargo/safety reports, complementary Twin actions, separate conversion and bounded independent fallback | Active: 87/320 pool, 80/102 legacy; all 422 final subject games clean; distinct losing bundle |
| 13 [capability-portfolio](capability-portfolio/README.md) | Geometry admits one access or supply specialist and an executable role program, preserving independent ordinary producers | Active: 93/320 pool, 86/102 legacy; 422 games without technical failures or team zero-clear flags; distinct losing bundle |
| 14 [counterpunch](counterpunch/README.md) | One bounded retaliation opportunity, fresh cleanup, real-HP loss reserves, wounded-source preference and personal conversion | Active: 92/320 pool, 78/102 legacy, 54/80 Starter; 502 games without technical failures or team zero-clear flags; distinct losing bundle |
| 15 [liquidation-clock](liquidation-clock/README.md) | Irreversible terminal work, conversion reserves, retained channels, bounded held rewards and gold-only continuation | Active: 104/320 pool, 87/102 legacy, 60/80 Starter; 502 games without technical failures or team zero-clear flags; distinct losing bundle |
| 16 [bounded-interceptor](bounded-interceptor/README.md) | One executable channel interruption, fresh effect receipts, collateral and loss reserves, then compulsory useful own production | Active: 92/320 pool, 87/102 legacy, 57/80 Starter; 502 games without technical failures or team zero-clear flags; four individual zero-production actors audited |

All sixteen controllers are complete. Each was fixed, tested and released before the next
began; the preceding result informed the next design. The design space includes robust
scenarios, repeated-game memory, cashflow, tribute batches, information value, specialization,
stopping rules and bounded opponent interaction.

## Distinctness rules
- Code is shared only inside this suite (`bots/heuristics/<module>/`); never include another suite.
- Check `arena ladder styles` after the first ladder games: a bot whose style sits next to a
  neighbour with a near-even head-to-head is not distinct.
- A different coefficient vector is insufficient. Keep a different algorithm or state and
  demonstrate reachable choices that differ from earlier controllers.
- Fixtures reach the production selector. Sparse full-game diagnostics record selections and
  deviations from the old ranker, with fuel reserved for actions.
- Main is at most one quarter of the development panel. Do not inspect its code or tune to
  individual maps. A healthy, productive losing idea is useful.
- Full-pool paired comparisons and fresh-map/opponent confirmation precede strength claims.
  Faults, exhaustion, error lines and actual stalls block promotion. Always report raw
  zero-clear flags and audit native progress: a productive capture/bank without last-hit
  clear credit is different evidence from a controller that cannot make progress.
- By the user's explicit diversity goal, a healthy, demonstrably different controller may have
  its active default retained after a weaker paired result. Record that as an experimental
  losing idea, never as a strength improvement or release candidate.

## Layout and checks
- `bots/heuristics/<bot>/src/main.cpp` is a bot (id `heuristics/<bot>`); shared modules live in
  `bots/heuristics/<module>/` and are included by relative paths.
- New executor forks may use `common/src/draft.h`: append-only suite draft indexes preserve
  the first five choices and add other champions. These indexes differ from SDK draft IDs
  and physical champion identities; profile inputs still cover all physical IDs 0..12.
- `common/src/capture.h` supplies a pure elapsed-hold watchdog. A reward still unpaid after
  the expected capture interval may have an unseen enemy holder elsewhere in the group.
  Empty personal enemy vision must not reset that clock; reset on actual departure, death
  or a changed reward lifecycle. A timeout must lead to bounded useful departure work.
- `common/src/equipment.h` names pure purchase plans. A later policy may expose
  `purchase(identity, monster, jungle)` for actual role-dependent equipment; its executor
  must call that same hook and query SDK prices/legality. Profiles v3 add 325 such choices;
  original policies fall back to their existing three-stage build without behavior changes.
- `bots/heuristics/tests/run.py` holds the focused fixtures (C++ compiled to WASI by
  `tools/arena/wasi.py`): `arena test heuristics`.
- `tests/profiles.py --strict`, using the Python from `arena env`, compares production selectors,
  banking, risk, purchases and rosters on a fixed deterministic corpus. Identical profiles fail;
  disagreement alone does not prove strategic diversity. Pair it with real activation traces
  and matched games. Reports and generated fixtures remain in `.cache/arena/heuristics-profiles/`.
- `arena build heuristics`, then measure with `arena play`, `arena sweep` and `arena gate`
  ([workflow](../../docs/workflow.md)).

## Leads
- Several later bots should own different execution state machines, not just source selectors:
  kill/reset pursuit, splash geometry, Prism working inventory, shield duty cycles, learned
  circuits, and witnessed clearer/holder handoffs. Keep such flows local to their bots rather
  than merging every success into one universal brain. Each needs a positive flow fixture,
  a refusal/expiry fixture and an observed capture/bank continuation.
- Frontier's recorded first-income failures motivate a small controller and immediate useful
  fallback. Planning must leave fuel for actions.
- Each bot records coverage, health, mechanism result and next-bot lesson here. Regenerable
  experiment output is under `.cache/arena/`; durable ladder games are under `results/ladder/`.

## Development evidence

01 first experiment: `h12-01-pool`, 40 pool maps, both sides, four opponents (320 keys per arm).
The first active controller was worse: 121/320 versus control 166/320, flips +12/-57, paired
gem margin -218.1 +/- 23.2 SE. It also exhausted twice and stalled 13 times. No promotion.
The 16-key smoke's identical control arm matched exactly; that small clean sample missed the
large-pool health failures. Legacy evidence of that earlier source (97/102 against a different
three-opponent panel) did not override the pool result.

Replay-derived repairs, all in the active executor or Satisficer's own policy:
- Reject sources whose capture tiles are outside the walking component. Being able to shoot a
  source across a hole does not make its reward reachable.
- Detail only a cheap nearest-12 shortlist after static admission, with incumbent allowance.
  A general-purpose stable sort consumed enough fuel to leave zero detailed jobs on islands.
- Hold only uncontested captures; fight briefly or leave a persistent contest. Both teams
  previously stood in one cleared region forever before reaching their defense logic.
- Reserve fuel after movement and cache one attack mask. Suspension otherwise resumed an
  old reveal/attack chain in the next round.
- Satisficer keeps progress-based commitments using best distance/HP reached, so a two-tile
  oscillation cannot keep extending its commitment. Unsuccessful partial previews change no
  commitment state.

`h12-01-healthfix5`: all 32 discovered failure keys clean (4 maps x 4 opponents x both sides),
20 wins, maximum ordinary-turn fuel 86,096. A subsequent static approach-mask cache regressed
initialization fuel and was removed; superseded full-pool jobs were stopped. Final-source
full-pool and legacy rechecks are in `h12-01-validated` and `h12-01-validated-confirm`.
Do not attribute future bundle-vs-frozen-control improvements solely to the aspiration rule:
the active arm also contains these mechanical repairs. Sparse selector deviations and focused
fixtures establish the controller's separate effect on decisions.

The validated pool arm eliminated exhaustion but retained four stalls, all on boss-battle B.
It scored 159/320 versus 164/320, flips +33/-38 (p=.635), margin -47.8 +/- 22.3 SE. Its legacy
arm was 102/102, clean; that is evidence of transfer against those weaker opponents only.
Replay fuel showed every champion repeatedly spending ~69k before a move that required 35k
remaining. The detailed scan now stops at 55k, leaving room for goal construction and routing;
a distant harmless-income candidate is always available even behind nearer unaffordable jobs.
`h12-01-action-reserve` passed all 40 regression keys (five maps, four opponents, both sides),
23/40, maximum fuel 79,757. Final exact-source reruns: `h12-01-complete` and
`h12-01-complete-confirm`. No claim that this controller improves strength.

**01 accepted as a distinct experimental controller.** `h12-01-complete`: active 162/320,
frozen control 164/320; flips +37/-39, McNemar p=.909, margin -29.1 +/- 22.4 SE: no detectable
difference. All 320 active games clean, maximum fuel 82,153; main reference 1/80. All 102 fresh
legacy games clean and won against starter, banked-vanguard and bargainer; maximum fuel 75,113.
The frozen control retains nine known stalls; it is preserved only to keep old behavior exact.
Enabled the active default for diversity and robustness, not because it beat the main bot.
Next-bot lesson: reserve action fuel before modelling, retain a useful fallback, and price
uncertainty plus renewal explicitly rather than assuming a short nominal cycle is reliable.

The off-arm executable is byte-for-byte equal to frozen fundamentals (SHA256 starts
`ed121f9767fc`), stronger than a statistical determinism check. First durable ladder placement:
24 Satisficer games, 5-19, clean; coarse nearest style banked-vanguard at distance 1.04, with no
convergence flag. This small placement is descriptive, not an independent strength estimate.

**02 accepted as a distinct experimental losing controller.** Final same-run paired pool
`h12-02-final-cached`: active 122/320 versus control 162/320, flips +16/-56, p<.001,
paired gem-margin difference -175.7 +/- 22.7 SE. Main was one quarter of the panel (1/80).
Independent `h12-02-final-cached-confirm`: 100/102 against three other opponents on 17 legacy
maps, both sides. All 422 final active games clean; observed maximum fuel 99,708 leaves a
narrow margin. The enabled executable matches the measured active bytes (SHA256 starts
`1b2c3c858d2e`). Exact scenario caching preserved its decision fingerprint while restoring
movement fuel. Local perimeter/death-memory and harmless-blocker rescue repair long-row
execution; observed rescue attacks led to source clear, capture and bank. Detailed failures,
coverage and limitations live in the bot's README.

Next-bot lesson: better survival, gold and clear counts do not ensure permanent score.
Personal capital targets and travel-dependent batches delayed banking. Bot 03 will combine
quick executable kills, refreshed attack continuation and smaller useful liquidations.

02's first durable ladder placement: 18 games, 6-12, subject health clean. Six flagged games
in the 32-game placement were opponent Tempo failures. Its coarse nearest style was
territory/reclamation at distance 1.08, with no convergence flag; the placement is too small
for a strength conclusion. Current main changed externally between experiments, so only
same-run paired controls support comparisons.

**03 accepted as a distinct experimental controller.** `h12-03-complete`: active 158/320
versus frozen 162/320, flips +29/-33, p=.704, paired margin +12.8 +/- 21.4 SE; no detected
strength difference. Main 0/80. Independent `h12-03-complete-confirm`: 102/102. All 422
active games clean, maximum fuel 99,532 pool / 88,452 legacy. Enabled source `2b415b498c4c`
matches measured active bytes (SHA256 starts `6c11d1d9afe1`); the off arm remains byte-identical
frozen Fundamentals. Production flow on the final source: kill/refresh/reset/different hit
round 567, shared capture 576, personal four-gem bank 592. Focused flow refusals, source
selection and bounded progress memory pass. Corpus v2 selector disagreement with 02 is 64.5%;
v3 retains all original profile segments and adds actual purchase-plan coverage.

Observed failures drove two local repairs: guard expensive action menus when movement has
spent a ranged basic, and protect useful travel progress as well as the quick-completion tier.
A kill disappearing from live-info visibility is not proof of life: query camp death state
on its visible public spawn tile and use cached positions for telemetry. The bot README
records the failed runs. Next-bot lesson: one fast kill leaves joint-hit geometry unpriced;
test clipped multi-target damage and awakening costs, while accounting for shared payouts.

03's first durable placement: 20 subject games, 14-6, clean. The two flagged games were
opponent Tempo exhaustion. Nearest coarse style Tempo at distance 0.66, head-to-head 2-0;
no convergence flag. This small, unbalanced placement is descriptive only.

**04 accepted as a distinct experimental losing controller.** `h12-04-final`: active 126/320
versus frozen 162/320, flips +11/-47, p<.001, paired margin -165.7 +/- 19.8 SE. Main 0/80;
final reference `dce540466d11`. Independent `h12-04-final-confirm`: 101/102. All 422 active
games clean, maximum ordinary fuel 92,328 pool / 87,534 legacy. The production fixture uses
the same clipped hit-set planner as the SDK adapter; risks include whole-group awakening,
direct unsafe primaries and stale/dead observations. Final-source Stellar area damage at
round 6 led to clear 23, shared capture 32 (11 gems) and personal bank 53.
Enabled source `af3d1a8787de` matches measured active bytes (SHA256 starts `a64ad1073363`);
explicit off remains byte-identical to frozen Fundamentals. Corpus v3 fingerprint
`cb2fe24735f8`; source choice differs from 03 on 47.4% of its fixed choice corpus.

Execution failures exposed expensive work before and after selection. Local repairs include
24-member incremental public metadata, a six-by-six density sample, a separate topology turn,
cheap executable geometry admission, cached masks, observed-only compact logging and a source
execution phase before global rescoring. A candidate is admitted before the detail guard can
starve all jobs. These scheduling changes remain in this bot's local executor. The bot README
records exact-source regressions and evidence; no coefficient or opponent-specific tuning.

Next-bot lesson: a demonstrated tactical multi-hit gain is not a demonstrated income gain.
Test the economic reason to keep combat capital separately: carried gems may increase Prism's
Focus enough to change a useful spell threshold, but must eventually become permanent score.

04's first durable placement: 32 subject games, 16-16, clean. The placement's one flagged
game was opponent Tempo's zero-clear stall. Nearest coarse style was renewal-regret at 0.36,
head-to-head 3-1; no convergence flag. This is descriptive placement evidence, not a strength
comparison with the paired panel.

**05 accepted as a distinct experimental losing controller.** `h12-05-complete`: active
110/320 versus frozen 162/320, flips +8/-60, p<.001, paired margin -228.2 +/- 21.7 SE.
Final main reference `52f589af9629`. `h12-05-complete-confirm`: 99/102. All 422 active games
clean, maximum fuel 91,211 pool / 84,024 legacy. Promoted `43ceb52fc5ee` matches measured
`83e65cbfce23` bytes (SHA256 starts `1c19d8153bc5`); explicit off remains frozen. Focused tests
and strict v3 profiles pass; fingerprint `e0d02b974fcc`, source choices differ from 04 on 70.2%.

Logged work entry in at least 48/320 games, useful stock casts in at least 31/320, and one
stock-enabled kill game. Sparse logs are lower-bound coverage. Native exact-source evidence:
capture seven gems, Focus 112 to 122.5, retain for area kills on 120-HP clusters, capture gold,
bank, Focus back to 112. This happened in a lost game. Stock-dependent assumptions reset on
any cargo drop; one-turn tribute starts are recorded explicitly, and pending safe captures
are protected before ordinary liquidation. The bot README records the phase and risk limits.

Resource repairs cache static altar goals, skip empty-stock modelling, and cache owner stats
and group affordability within an unchanged observation epoch. Monster's bonus applies to
both spell and basic forecasts. These fixes preserve the decision rule; a threshold improvement
does not establish an income improvement. Next-bot lesson: test temporary capital whose
value expires, using honest partial work windows and deliberate withdrawal rather than hoarding.

05's first durable placement: 22 subject games, 5-17, clean. The four flagged games were
opponent exhaustion and zero-clear stalls. Nearest coarse style renewal-regret at 0.20,
head-to-head 0-2, no convergence flag. Because the aggregate distance was close, the separate
`h12-05-style-confirm` used all 40 pool maps on both sides: stock lost 24-56 and was healthy.
Regret had one zero-clear game (yellow-brick-road B), with no faults, exhaustion or errors;
it was repaired after 06. Excluding that stock win leaves 23-56,
still not a near-even head-to-head. Their production selectors differ
on 673/920 choices (73.2%), banking on 190/632 and purchase plans on 153/325; their native
inventory versus opportunity-regret mechanisms are separate. Retain both losing ideas;
aggregate style proximity alone does not establish a shared controller.

The new 02 stall is a reproduced hidden contest, not a failed regret calculation. On
yellow-brick-road B, opposing holders stood at distant ends of a long cleared group, outside
one another's vision. Empty enemy vision repeatedly renewed the wait despite no payout.
The elapsed-hold watchdog was first exercised in 06, then 02 was repaired behind a new
default-off knob against its existing active bytes, with its policy arithmetic unchanged.

**06 accepted as a distinct experimental losing controller.** `h12-06-final`: active 109/320
versus frozen 162/320, flips +9/-62, p<.001, paired margin -294.8 +/-22.5 SE. Main 0/80,
snapshot `41fae4ff57a6`; the reference changed externally, so only same-run comparisons
support the bundle result. `h12-06-final-confirm`: 83/102. All 422 active games clean,
maximum ordinary fuel 96,980 pool / 87,294 legacy. Promoted `4a232ca3516c` equals measured
active `788cbaaf6512` bytes (SHA256 starts `2bd4416371e5`); explicit off remains frozen.
Focused tests pass 2/2 and strict profiles 6/6; v3 fingerprint `dce101c6246c`, with source
choices different from 05 on 54.2% of the fixed corpus.

Actual shield absorption was logged in at least 263/320 pool games on 36/40 maps. The exact
native witness follows Varg: round 10 shield 250, round 11 absorption to 190 at unchanged
HP 820, two work turns, withdrawal 12, renewal 18, return to the same injured Elk and kill
23, shared capture of eight gems 30, own bank 50. Allies did substantial damage during the
absence; this is useful partial work and continuation, not a solo clear or income advantage.

Replay and audit repairs stay in 06: bounded escape progress for ranged sources, unchanged-
epoch threat caching, deferred post-walk shield preparation, fresh harmless continuation
without another shield, and conservative neighboring-source splash admission. The elapsed
capture watchdog survives empty enemy vision. No native blocked-exit failure was observed
in this coverage; local escape is not a proof of a complete safe route. Next-bot lesson:
repeated partial work needs honest lifecycle and injury observations plus a cashable renewal
cycle. Test route memory as its own controller rather than extending the shield model.

06's first durable placement: 12 subject games, 8-4, clean. The placement's six flagged
games were opponent exhaustion or zero-clear stalls. Nearest coarse style Fundamentals at
distance 0.84, head-to-head 0-2; no convergence flag. This sparse, unbalanced placement is
descriptive and does not reverse the weaker paired result.

**02 hidden-contest repair after 06.** `h12-02-watchdog-accepted` compares old active Regret
against `CAPTURE_WATCHDOG=1` on all 320 identical pool keys: 121/320 to 114/320, flips +5/-12,
p=.143, paired margin -13.6 +/-8.5 SE; no detectable difference. Main snapshot `69b8b017328c`,
0/80, remains one quarter of the panel. Final `accepted-legacy` is 99/102 and `accepted-stock`
is 53/80 against 05 on all pool maps, both sides. All 502 active games clean; maximum fuel
99,607 pool / 85,546 legacy / 97,705 Stock. The unchanged strict v3 fingerprint is
`a4758540cdec`. Off source `7ba83c8d6fb8` exactly matches old active Regret's WASM.
Promoted `98f0e74e76b4` matches measured watchdog `a3aa2f39ab7d` bytes (SHA256 starts
`e94afe542a52`). Both final fixtures pass; all owned jobs closed before 07 began.

The exact formerly stalled matchup now shows Tavi waiting unpaid at (32,12), rounds 78-85,
leaving at elapsed capture time plus two, receiving a later shared payout at 174 and banking
nine gems at 220. A failed walking exit can instead bank adjacent cargo, clear one harmless
blocker or recall after bounded local threat checks. The expired watch survives a blocked
exit and quarantine expiry. These are execution repairs; source selection, financing, risk,
draft and equipment stay unchanged. Local departure is still not a complete escape planner,
and the pool fuel maximum is narrow. The next controller must learn from actual payouts,
keep lifecycle clocks through missing vision, and distinguish useful work from income.

Repaired 02's first durable placement: 14 subject games, 8-6, clean. Both flagged games
in the placement were opponent Bargainer exhaustion and zero-clear stalls. Nearest coarse
style 06 at distance 0.58, with no head-to-head yet; no convergence flag. As with the other
small placements, opponent composition and sparse coverage prevent a strength inference.

07 development exposed an important health-metric limitation. Source `2638b7dd8873` had
three raw zero-clear flags against main. Native inspection found movement rather than a
stationary deadlock; Ambush A had banked 88 gems despite no clear credit. Separately, repeated
lethal unpaid visits were not teaching the circuit a loss. Counting work-episode death as
one failure, while excluding bank journeys and their nested recovery, repaired all three
exact keys against the pinned opponent. Keep the raw flags and causal diagnosis; do not
change a controller merely to manufacture clear credit. That source is superseded.

**07 accepted as a distinct experimental losing controller.** `h12-07-complete`: active
111/320 versus frozen 162/320, flips +13/-64, McNemar p=3.02e-9, paired margin
-262.7 +/-26.0 SE. Main `001974b90521` won all 80 reference games; Tempo changed externally
to `c54228dd4385`, so only the same-run comparison supports the result. Final
`h12-07-complete-confirm`: 99/102. All 422 active games clean, including all 80 main keys;
maximum ordinary fuel 98,259 pool / 93,450 legacy. Promoted `7b64dffed801` equals actual
measured active `152476a1ded1` raw and injected WASM (SHA256 starts `9067720cab11`);
explicit off remains frozen. Fixtures pass 2/2 and strict profiles 7/7, fingerprint
`d2994d7f1984`. Source choices differ from 06 on 62.9% of the fixed corpus.

Final native Tavi evidence includes failed source 7 at 26/192, replacement by source 9 at
268, then actual payment 277. Retained order 9 then 11 pays in completed laps 2 and 4,
with own banks of 13 gems at 341 and eight at 502. The intervening lap fails both visits
and earns unrelated gold, so these are not consecutive productive laps. Shared rewards and
damage are recorded explicitly. This demonstrates replacement, retention and conversion,
not solo clears or efficient renewal. Sparse logs record programmed-source payouts on at
least 317/320 games and banks on 291/320; caps make these lower-bound coverage.

Repairs preserve the learning idea: restart an unfinished lap after death; distinguish
nested bank/recovery resumes; pause work leases during deliberate interruptions and refresh
progress reference points; bound unpaid recoveries; teach one loss for an unpaid work-episode
death while excluding bank journeys. Exact pinned regressions cover four opponents, four
maps and both sides (32 clean games). Omitted boundary detail can still cause a conservative
return; source income may be distant, shared or unavailable on the next lap. The next bot
should test whether a witnessed division of clearing and holding can overlap useful work.
Private route learning alone does not coordinate capture ownership or prove better income.

07's first durable placement: 16 subject games, 12-4, clean. The health flags in all seven
affected placement games belonged to entrants outside this suite. Nearest coarse style 03
at distance 0.65, head-to-head 0-2; no convergence flag. Sparse placement describes behavior and does not overturn the
weaker same-run paired result.

08 development found source-switch churn on long partially observed rows: all five actors
could move and hit incidental harmless blockers without earning anything. A bounded solo work
ticket preserves a selected job while damage or travel makes progress; it is not a learned
circuit. Contract activation also exposed a provisional packet being treated as already
expired before its dates were assigned. Neither repair relaxes funding, physical arrival or
fresh-clear admission.

The superseded first full 08 panel (`h12-08-complete`, active `73790489acba`) was 118/320
versus frozen 162/320, flips +11/-55, paired margin -187.9 +/-19.9 SE. All aggregate subject
health metrics were clean, yet static review found a gold-only holder could retain an empty
bank demand forever after receiving income. No recorded natural contract in that panel was
gold-only: this is a fixture-proven latent defect, not a claimed observed replay stall. Actual
own cargo now controls the bank demand; gold-only PAID resumes solo production. Both actor
orders are tested. This source and its clean metrics do not establish the repaired build's
health; final panels are being repeated.

The repaired frozen source `7c88b323f812` reproduces the natural Collision A chain: personal
income and a useful purchase fund Varg; Marcella arrives on a source pad at 453; Varg sees her
and leaves the entire source at 454; outside work clears at 456 before Marcella's sole-holder
payout of 11 gems at 459; Varg alone receives outside gold at 463; Marcella banks at 469.
This demonstrates overlapping production and separate actual receipts, not solo clearing or
an income advantage. Intent reports, personally witnessed arrival and personal payout remain
different facts.

**08 accepted as a distinct experimental losing controller.** `h12-08-gold-final` covers
320 identical pool keys per arm: active 118/320 versus frozen 162/320, flips +11/-55,
exact McNemar p=3.60e-8, paired margin -189.3 +/-20.0 SE. Active wins are Fundamentals
10/80, main 0/80, Tempo 33/80 and Concentric 75/80. Main `b0e31bd7e555` remains one quarter
of the panel; Concentric changed externally to `d7e76b86acd3`. Only the same-run comparison
supports this result. `h12-08-gold-final-confirm` is 99/102 on fresh legacy maps and opponents.
All 422 final subject games have zero faults, exhaustion, errors, raw zero-clear flags or
suspension. Maximum ordinary fuel is 89,601 pool / 82,747 legacy. The final whole-bundle
comparison does not isolate the effect of handoffs.

Promoted `69817693ab16` matches the actual final manifest's measured active `7c88b323f812`
raw and injected WASM, SHA256 starts `1dbd383b98e9`. Explicit off `aee205af6af0` and same-run
base `52a3543404ea` remain frozen. Proof is `.cache/h08-promotion-proof.json`. Fixtures pass
2/2, including both actor orders and gold-only continuation. All eight strict profiles pass;
08's fingerprint `9d73c78e7b3e` remains valid across the flow-only repair because all six
profiled inputs are byte-identical. Versus 07: 32.8% source-choice, 26.7% bank and 31.1%
purchase-plan disagreement; the native production chain separately establishes the new flow.

Final sparse logs record 37 offers across 24 games/11 maps, 21 accept/release/PAID reports
each across 18 games/10 maps, and 18 outside-income events across 16 games/9 maps. There are
18 logged report-plus-outside-income chains and three incomplete chains. Caps make these
lower bounds, not opportunity rates or a proof that every report led to a completed bank.
The next bot should examine personal conversion service costs and alternatives: successfully
separating clearing from holding still does not establish efficient income or banking.
08's first durable placement (`20261022-015849`): 18 subject games, 8-10, all clean. The health
flags in all five affected placement games belonged to entrants outside this suite.
Nearest coarse style is
Commitments' Cross Type Fork at distance 0.70, without head-to-head; no convergence flag.
The 0-2 result against 05 on one map is descriptive only. This sparse placement does not
overturn the weaker paired result. 08's record and all owned jobs closed before 09 began.
The final pool also includes natural gold-only continuation evidence, separately recorded in
08's README; productive dispatch after that payment no longer depends on a nonempty gem bag.

**09 accepted as a distinct experimental losing controller.** `h12-09-final` covers all
320 identical pool keys per arm: active 124/320 versus frozen 162/320, flips +13/-51,
exact McNemar p=1.88e-6, paired margin -125.9 +/-19.0 SE. Active wins: Fundamentals 15/80,
main 0/80, Tempo 36/80, Concentric 73/80. Main `f64d2a97241b` remains one quarter of the
panel; Concentric is `e4b0dc7ef863`. The comparison is same-run only and measures the full
bundle, not an isolated service intervention. Fresh `h12-09-final-confirm` is 102/102;
`h12-09-locks` is 102/160 against Lock Work Exchange and Gremlins on the full pool, both sides.
All 582 final subject games are clean, including zero raw zero-clear flags. Maximum ordinary
fuel: 99,430 pool / 84,953 legacy / 99,525 extra. The highest extra maxima cluster on Big World;
these narrow reserves are measured limits, not a universal fuel proof.

09 combines a financing-lot selector with a private inspect/remove-or-divert/submit/confirm
service program. The next useful purchase's queried price replaces an early fixed-price
assumption. Already-funded actors seek gems; actual shop returns amortise six queried prices.
Public-terrain altar fields are built one per setup turn, then a commitment shortlists two
reachable options. Fresh enemy-lock hit counts, actual attack/reset opportunities, arrival
work, unknown inspection, exposure and switching friction determine service choice. Failed
options are quarantined within the attempt; explicit no-progress and absolute expiry restore
ordinary production. Empty cargo and death cannot retain mandatory banking. No locks are
bought, no wards are used, and there is no team service contract.

Natural exact-source evidence `5899ae18d470` is consolidated in
`.cache/altar-review/native/native-summary.json`. Ambush A against Gremlins: Vesper's 26-gem
tribute at 874 is interrupted by a new lock at 877; Vesper removes two hits, Varg two more,
then Vesper the last at 878. Fresh absence precedes channel 879 and her own 26-gem bank 882,
score 127 to 153. This is shared removal, not a solo five-hit claim. Monolith A: Varg holds
five gems and 525/1000 HP; a personally visible enemy moves from distance five to four of his
target altar at 259, making that service unsafe. He diverts to the alternate, reverses route,
and banks at 271, score 49 to 54. No lock exists in this second window; it demonstrates
exposure-driven outside-option choice. Production fixtures separately prove lock-work and
alternate-travel sensitivity. Actual role purchases are verified in the native summary.

Fixtures pass 2/2 and all nine strict v3 profiles pass; fingerprint `578034bc7958`. Macro
source choices differ from 03 on 33.5%, while the native service cases establish a different
execution flow. The artificial native lock probe validates engine mechanics only and is kept
separate from production-policy evidence. Promoted default `e5852e4d42fe` matches measured
`5899ae18d470` raw and injected WASM (SHA256 starts `eb55418f5c03`); explicit off `c17f31f599b8`
and same-run base `de0db388f7a4` remain frozen. The proof reads the completed 640-record
manifest: `.cache/altar-review/promotion-proof.json`. First durable placement
`20261022-024058`: 18 subject games, 15-3, all clean; the two flagged games in the 48-game
run are Bargainer's stalls outside this suite. Nearest coarse style is Reservoir Wardens at
distance 0.51, without head-to-head, and no convergence flag. Sparse, unbalanced placement
does not reverse the weaker paired result. All 09 jobs and records closed before 10 began.
The next idea should test whether a bounded information action can
change executable work: a public alternative's existence and a current live observation have
different value, and an inspection option is not a known safe conversion.

**10 accepted as a distinct experimental losing controller.** `h12-10-final-complete`
covers all 320 identical pool keys per arm: active 118/320 versus frozen 162/320,
flips +10/-54, exact McNemar p=1.9965e-8, paired margin -167.0 +/-21.0 SE.
Main `d243650467f7` is one quarter of the panel; Concentric is `49ab05f73f8a`.
These are same-run comparisons; changed opponents make earlier panels unsuitable for
pooled strength claims. Fresh `h12-10-final-confirm-complete` is 102/102; the additional
all-pool, both-side Starter liveness screen is 75/80. All 502 final subject keys are
clean, including zero raw zero-clear flags. Maximum ordinary fuel is 96,367 pool /
93,318 legacy / 96,075 screen. Fixed regressions and a deterministic duplicate retained
for native inspection do not supply additional independent strength samples.

10 combines cashflow-horizon source choice, a two-job detail bound, personal exposure
and cargo reserves, and one pivotal own-ward information option. Wards cost no gold,
but replacement can remove useful old vision, and refresh can consume the current
action and a recovery trip. Successful placement rebuilds fresh observations and the
entire bounded shortlist; a lost observation returns to conservative estimates.
False placement, incomplete refresh and observation-associated choice changes are
separate outcomes. Fifteen retained legacy replays contain lower bounds of 369
attempts: 100 incomplete, 134 failed placements, 116 same choices and 19 changes.
These capped observations are not opportunity rates or economic benefits.

Two earlier full panels failed health and were stopped. Ambush exposed an unguarded
post-probe execution tail that suspended across owner rounds. Yellow Brick Road
exposed successive forms of execution starvation: eager aggregates whose values were
discarded, costly per-member SDK bitboard operations, and a path-field admission
gate that could never admit ordinary turns. Factual frontier memory must record
visible empty members even while another live member is targeted. The final executor
queries only used detail, caches member indices and local facts, preserves execution
reserves, and provides a bounded one-step route fallback below the full-field gate.
This greedy fallback does not guarantee obstacle detours. Native replay of the old
stall key now shows all five actors producing and converting their own cargo.

The prototype's positive ward-to-work-to-bank chain did not persist in the bounded
final-source recheck at the same known Fragment window. It remains prototype evidence
only. Final fixtures establish observation-driven choice and refusal paths, and real
games establish probe activation and ordinary production; an exact final-source
economic benefit from a ward remains unproven. No alternate case search or tuning was
used to replace this negative result.

Fixtures pass 2/2 and all ten strict v3 profiles pass; fingerprint `1e46cb3eb69b`.
Promoted default `021cc2853a0f` matches measured `28046109b5b5` raw and injected WASM
(SHA256 starts `42df697ec43f`); explicit off `093505a47a53` and same-run base
`1bab941683d1` remain frozen. Root independently checked source and WASM hashes
against the completed manifest in `.cache/surveyor-review/promotion-proof.json`.
First durable placement `20261022-051852`: 16 subject games, 9-7, all clean. The health flags
in all five affected placement games belong to entrants outside this suite.
Nearest coarse style is 09
at distance 0.46, without head-to-head, and no convergence flag. Sparse placement
does not reverse the weaker paired result. The next bot should execute a small safe
prefix before optional thought, with a separately bounded low-fuel yield path; moving
the same expensive perception prefix ahead of an act-first scheduler would retain
this failure.
All 10 jobs and records closed and ownership was released before 11 began.

**11 accepted as a distinct experimental losing controller.** `h12-11-final-access`
covers all 320 identical pool keys per arm: active 102/320 versus frozen 162/320,
flips +11/-71, exact McNemar p=6.8138e-12, paired margin -335.1 +/-24.5 SE.
Main `d243650467f7` is one quarter of the panel; Concentric is `a2d4c9052f0f`.
Fresh `h12-11-final-access-confirm` is 100/102. All 422 final subject games have
zero faults, exhaustion, errors or at-budget events; maxima are 55,427 pool and
52,013 legacy. The two raw zero-clear flags, Swirl A and Hooks B against main,
are native-confirmed active combat losses: all five actors move, deal damage
and repeatedly die. They are not internal planning or routing stalls.

11 owns a smaller executor: a saved productive intent and a small fresh veto
precede an actual useful action; one rotating cached-scalar challenger comparison
may then publish a proposal for the immediately following owner turn. No proposal
redirects the current turn, and no broad dynamic feature pass precedes steady
work. Fresh life, finance, source, safety, channel and capture checks can veto
adoption. A low-fuel move has a separate minimal yield path. Cashable throughput,
queried-price gold discounting, cargo-dependent risk, twelve-gem liquidation and
role-dependent equipment interact with this scheduling rule.

Read-only review found two real freshness gaps: cash-only income did not advance
the finance epoch, and a teammate could finish the occupied source before its
watchdog was established. Both are repaired. Progress-based switching friction
protects an improving approach without resetting the persistent no-work clock.
The first full pool source failed on five Islands keys. A bounded diagnostic
showed unreachable saved goals with 84-87K fuel still available; a complete public
terrain check found 38 of 46 groups isolated from the relevant walking component.
The repair caches one complete static walking component, rejects uncapturable
sources, and refreshes after an actual component change, including recall.
It does not treat a distance cutoff as permanent unreachability. The initial
flood reserves a cheap continuation and cannot trigger a second routing field.
Every Islands key is productive in the final panel. Earlier failed-source scores
and native episodes are retained as failed evidence, not pooled with this result.

Final-source native evidence shows Tavi's actual source-seven move at 29,
publication of source eighteen, and next-owner-turn adoption and movement at 30.
Separately, Vesper receives fifteen gems at 106, starts tribute at 155 and banks
her own fifteen at 157. These prove scheduling activation and ordinary personal
production, not an audit-caused income improvement or a same-actor economic chain.
The bounded record is `.cache/act-first-review/native/access-native-summary.json`.
Across 46,959 normal receipt lines, cumulative completed audit cost is at most
714 fuel, complete static flood 31,712, and the pre-formatting-argument through
yield tail 13,132-16,701. These fit the measured 4K audit and 24K normal-tail
admissions. Cumulative maxima are not independent samples at every line; the
minimal low-fuel route emits no such receipt, so these are not universal bounds.

Fixtures pass 2/2 and all eleven strict v3 profiles pass; fingerprint
`ace558834d52`. The corpus omits execution order; production adapters and native
receipts establish that distinction. Promoted `9599b87be9c4` matches measured
`471b85617f41` raw and injected WASM (SHA256 starts `0d2d83b74003`); explicit
off `1250b9aa8fbe` and base `ae2b0d559614` remain frozen. Root independently
verified all source and WASM hashes against the completed manifest in
`.cache/act-first-review/final-proof.json`.

First durable placement `20261022-055613`: 16 subject games, 7-9, zero technical
failures. Its one raw zero-clear flag is the exact previously audited Hooks B
main key: same executable bytes, opponent, engine, rounds and both teams' metrics.
The other four flags in the 48-game run are outside this suite. Nearest coarse
style is Concentric at 0.48, head-to-head 1-1, with no convergence flag. Sparse
placement does not overturn the weaker paired result. All 11 jobs and records
closed and ownership was released before 12 began. The next bot must treat a
fresh report as a dated observation, and revalidate the state that permits action;
cheap coordination cannot replace current safety, capture continuity or access.

**12 accepted as a distinct experimental losing controller.** `h12-12-final-health`
covers all 320 identical pool keys per arm: active 87/320 versus frozen 162/320,
flips +6/-81, exact McNemar p=7.0345e-18, paired margin -369.9 +/-25.2 SE.
Main `d243650467f7` remains one quarter of the panel; Tempo is `10fbf47f6741`
and Concentric `a2d4c9052f0f`. Fresh `h12-12-final-health-confirm` is 80/102.
All 422 exact final subject games are clean, including zero raw zero-clear flags.
Maximum ordinary fuel is 68,789 pool and 69,201 legacy. The full bundle is weaker;
this comparison does not isolate a benefit or cost of the interlock itself.

12 replaces the act-first scheduler with linked-life risk modes. Cassi and Issac
have separate actions and bags but can lose both through either body's death.
Each writes only its own four-byte dated self-report; other roster members never
write the envelope and work independently. Live partner queries are restricted
to legal identity/team/alive facts. Reports convey no additional vision rights.
Positive cargo or unsafe reports constrain new hostile work, while current own
danger takes priority. Each Twin banks any positive bag, preventing a double-loaded
wait below an ordinary threshold. Actual tribute start preserves reported cargo;
only a subsequent self-observed zero clears it. Cassi's movement-exclusive damage
and compatible stun, Issac's reset and safe dash, role equipment and private
conversion interact with the linked-loss rule. Zero-damage hostile actions and
exposed landings cannot bypass that rule.

An age-one zero report can precede a new capture payment, so it cannot grant
current zero-cargo permission. It becomes Unknown; the Unknown clock still
progresses toward bounded conservative fallback rather than permanent waiting.
Current-round zero permission remains subject to fresh own risk. This is a
partial-information heuristic, not a guarantee that a reported state persists.
Separate owner-start respawns, life epochs and zero-initialized unknown packets
are tested. Low-HP recall cancellation and repeated unaffordable approaches caused
actual Shortcut stalls in an earlier screen. Recall now persists absent actual
exposure; queried public design workload and coarse affordability replace zero
incoming for unknown huge groups, with fresh in-range risk authoritative and a
bounded partial-work fallback. A stronger-opponent panel then exposed a missing
early stun/action guard. Both that panel and the prior stale-zero logic are
superseded. Final exact-source validation covers all of these repairs.

Final normal Collision A versus Fundamentals supplies a bounded mechanism chain.
Cassi is at full 600 HP at 377, accepts Issac's loaded report from 376 and moves
away through 379. Issac receives nine gems and twenty gold from a capture shared
with Tavi at 376, starts tribute at 378 with cargo still positive, and actually
banks nine at owner start 379, score zero to nine. Cassi treats the previous-round
zero as Unknown at 380, performs harmless gold-source work, and attacks another
camp at 381. This establishes protection activation, actual conversion and
continuation, not a solo clear or causal strength improvement. The record is
`.cache/twin-interlock-review/native/health-native-summary.json`.

Across 22,773 normal receipt lines, observed packet read/publish intervals reach
22,714 fuel, the entire yield tail 10,197-12,975, and the paired report-plus-tail
envelope 33,262 below the 42K publication guard. Accounting uses actual draft
pick counts three/four/five and physical bodies four/five/six, with no ordinal
failures. These bounded observations are not universal fuel guarantees.
Fixtures pass 2/2 and all twelve strict v3 profiles pass; fingerprint
`9e6033040eba`. Promoted `54a45fe46bee` matches measured `8202c6378a93` raw
and injected WASM (SHA256 starts `36be7fda90e1`); explicit off `31a7a85b7982`
and base `be501c1355b5` remain frozen. Root independently checked all source and
WASM hashes against the completed manifest in
`.cache/twin-interlock-review/final-proof.json`.

First durable placement `20261022-062634`: 16 subject games, 8-8, all clean.
The health flags in all eight affected placement games belong to entrants outside this suite.
Nearest coarse style is 09 at 0.37, without head-to-head, and no convergence flag. This sparse
placement does not reverse the weaker paired result. All 12 jobs and records
closed and ownership was released before 13 began. The next bot should admit a
special capability only for an executable economic program, while preserving
ordinary producers whose first income does not depend on that specialist.

## 13: admit a capability, then execute its whole income path

Capability Portfolio admits at most one specialist from public geometry. Access
uses Vesper for a disconnected source with a reversible hole crossing, all-member
work access and a home-component conversion suffix. Supply uses Tavi for one
capture-funded field Focus purchase and subsequent work. Other actors produce
independently. A complete versioned draft envelope binds actual champion and SDK
source identities; invalid or partial plans reconstruct ordinary roles. Fresh
occupancy, cast masks, capacity and exposure can refuse a static admission.
Independent review found no plan, identity-binding or Access execution blocker.

Special programs initially all refused at the draft fuel floor. Bounded native
fields and cached public camp designs replace expensive repeated guest topology
and design queries. The first technically clean full result was still insufficient:
an individual actor earned nothing for the entire Reverse Spiral game. Native
inspection exposed a route-progress gap: straight-line distance understated wall
detours, and source changes could postpone recovery. RouteWatch now measures
native field progress toward a stable goal. Source changes, movement and recall
preserve the global no-work history; Renewal adds bounded safe empty-bag recovery.
Its two-attempt cap applies to that new recovery process, not every existing recall.
The independent patch review found no new blocker.

The exact previously unproductive Vendetta against the same Concentric snapshot
now clears 20 members and collects/submits 140 gems. Bounded native inspection
shows walking distance 50 to zero over 50 steps while straight-line distance grows
from three to nineteen, then an actual source kill, own seven-gem capture and own
seven-gem bank. No Renewal attempt preceded this work. This establishes the route
repair, not universal individual productivity. Final scouts still find fifteen
zero-income actors among 1,440 and ten actors with long quiet intervals; one actor
has no whole-game production amid four deaths and champion damage. These are
remaining productivity limits, not evidence of profitable specialization.

Final measured `f8d54de63e1c` covers forty pool maps, both sides, against
Fundamentals, Main, Tempo and Concentric: 93/320 versus same-run frozen 162/320,
flips +6/-75, McNemar p=2.911e-16, paired gem-margin difference
-330.4375 +/- 24.4011 SE. Fresh legacy maps and three opponents yield 86/102.
All 422 subject games have no faults, exhaustion, error lines, at-budget turns or
team zero-clear flags; observed maximum ordinary fuel is 63,525 pool and 61,247
fresh. Main occupies one quarter of the pool panel. The fixed same-roster program
ablation is 8/16 in both arms, no outcome flips, p=1, margin +8.25 +/- 16.20 SE.
It detects no capability benefit. This is retained as a distinct losing experiment.

The final bounded special-program audit covers 86 owner starts in four windows
across three fixed cases, with zero query errors. Access actually crosses, captures
gold and returns; its zero-cargo Bank phase does not prove gem conversion. Supply
captures gold, buys a real field Focus shard and deals useful damage, but that hit
was already affordable before the purchase; a much later ordinary bank is not a
causal purchase benefit. The route regression adds 75 samples with zero query
errors. Records are in the bot README and `.cache/portfolio-review/`.

Focused fixtures pass 2/2 and all thirteen strict v3 profiles pass; fingerprint
`d1d1a9aae991`. Current default `26afbbadaa50` matches measured active raw and
injected WASM (SHA256 starts `309b4e708308`); explicit off `f82d2ba491f3` and
base `d670ab196f7d` remain frozen. Root independently checked all source and WASM
hashes against the actual completed manifest in
`.cache/portfolio-review/final-health-proof.json`. The earlier `1328e002207d`
full result and proof are superseded by the route repair and exact rerun.

First durable placement `20261022-071656`: sixteen subject games, 3-13, without
technical or team zero-clear flags. The health flags in four placement games
belong to Tempo and Bargainer outside this suite. Nearest coarse style is 08 at
0.52, without head-to-head, and no convergence flag. The next bot should purchase
a small, bounded action-order opportunity and test actual cleanup, rather than
assuming a richer specialist program or an expected damage benefit pays off.
All 13 jobs and records closed and ownership was released before 14 began.

## 14: one expected attack phase, then actual cleanup

Counterpunch gives Ren a one-camp-lifecycle exposure/cleanup commitment. Admission
compares a nonlethal basic's remaining target HP with next-owner basic-plus-stun
capacity and one expected retaliation. Current HP must independently cover two
observed incoming phases plus a cargo-sensitive reserve; expected retaliation,
future healing and shields cannot pay that reserve. Fresh next-owner state drives
basic-first cleanup or withdrawal. Ren's Jungle Focus/Monster Health finance,
wounded manageable sources, independent Willow/Tavi/Vesper/Marcella producers,
ordinary twelve-gem batches and personally paid-source conversion complete the
bundle. An unavailable cleanup never renews the same camp exposure indefinitely.

Independent review found a fresh-enemy veto missing before special cleanup, then
a successful basic whose rejected exposure plan lost its action receipt. Both are
repaired through production helpers and fixtures. A separate bounded actor audit
found two actual spawn loops: enemy chip kept HP below maximum at every owner
entry despite end-turn healing, and the strict full-HP gate returned before any
action. SpawnRest now permits one wounded arrival rest and does not renew it for
continued missing HP. Death and genuine return establish new arrivals. On the
four regression keys all sixteen actors now earn personally; the previously
stuck Willow submits 140 gems. The component-fuel hypothesis did not explain
these cases and prompted no unrelated change.

The finance audit then found a real four-gem capture whose deferred observation
failed to request prompt conversion. PaidCapture now recognizes prior own-pad
residence plus a fresh personal currency delta and selected-group respawn in the
early snapshot. Actual residence is also recorded after all own actions, and an
observed completed reward lifecycle retires old expectations. Receipt, routing,
tribute start and completed banking remain separate evidence.

Final measured `d364cdf5cce4` covers the full forty-map, both-side panel against
Fundamentals, Main, Tempo and Concentric: 92/320 versus same-run frozen 162/320,
flips +7/-77, McNemar p=5.138e-16, paired gem-margin difference
-370.26875 +/- 24.19258 SE. Main remains one quarter of the panel. Fresh legacy
maps/three opponents give 78/102; the full-pool Starter screen gives 54/80.
All 502 final subject keys have no faults, exhaustion, error lines, at-budget
turns or team zero-clear flags. Observed maximum ordinary fuel is 71,769 pool,
57,868 fresh and 61,172 screening. The fixed same-roster order ablation is 7/16
in both arms, p=1, margin -9.5 +/- 7.5845 SE: no detected strength benefit.

The final native audit binds two fixed Collision cases, three windows, 77 owner
starts and 386 receipts with zero query errors. At Tempo B 408 the basic leaves
151.899719 camp HP; the camp phase takes twenty real Ren HP and 25.400002 camp
HP, matching 0.2 times 127 Focus. Fresh basic then ready stun finish the group.
Ren alone holds its pad at 411-415, receives eighty gold/four gems at 416,
recognizes that receipt and actually banks four at 513. That 97-round conversion
has an undecoded middle interval and already meets an ordinary survival-bank
override, so it isolates no paid-flag benefit. In the Starter case an ally clears
the target before the expected retaliation; Ren withdraws and recalls. Both final
arms begin with stun unavailable. They establish a forecast/commitment cleanup
flow, not the intended benefit from withholding a currently legal stun. No
threshold tuning or alternative-case hunt replaced this negative finding.

Final pool scouts find seventeen zero-income actors among 1,440, none without
whole-game production, and a longest quiet interval of 1,875 rounds. Universal
individual productivity is not established. The remaining zero-income Starter
Willow makes 115 moves over 146 alive starts, with seven deaths: she retreats
from a visible Monolith into a bush where it remains in range but becomes unseen,
then loses recall to damage and dies. This is a documented unsafe-retreat loss,
not the repaired no-action spawn gate. Visible-only recovery remains a limit.

Focused fixtures pass 2/2 and all fourteen strict v3 profiles pass; fingerprint
`e35592013e3e`. Current default `431bc04f582d`, measured active and native order
variant `16a591a86b6c` have identical raw/injected WASM (SHA256 starts
`2ba797d97063`); explicit off `49ef489682c0` and base `c3bd4900b9c4` stay frozen.
Root independently verified all source/fixture hashes, the completed manifest and
all ten relevant WASM files in `.cache/counterpunch-review/final-proof.json` and
the native variant. Earlier pre-receipt, pre-spawn-repair and pre-finance panels
are superseded. This remains a distinct losing experiment.

First durable placement `20261022-075140`: fourteen subject games, 8-6, without
technical or team zero-clear flags. The health flags in eight affected placement
games belong to Tempo, Bargainer and Banked Vanguard outside this suite. Nearest
coarse style is 13 at 0.31, without head-to-head, and no convergence flag. The
next bot should make an observable irreversible game-clock choice, distinguish
its conversion reserve from an impossibility proof, and carry the repaired action,
arrival and payout boundaries forward.
All 14 jobs and records closed and ownership was released before 15 began.

## 15: reserve the end of the game, then stay terminal

Liquidation Clock uses an irreversible per-actor terminal flag, surviving banks,
empty bags, death and respawn. Its fixed reserve is
`max(24, 2*capture_time + D(max(1, bag)))` with queried tribute timing; the flag
sets when remaining rounds reach that reserve. This is a stopping heuristic,
not proof that every future harvest is impossible. Actual channels are preserved
before new-start horizon checks. Loaded conversion precedes ordinary source
holding; an empty terminal actor can finish one selected physically held cleared
reward, then admits only safe zero-gem gold work with an estimated timely payout.
Optional shopping stops. Ordinary near-term cashable work, uncrowded captures,
current-HP risk, role gear and twelve-gem batches complete the Tavi/Willow/Vesper/
Marcella/Vendetta bundle. Spending does not erase lifetime earned-gold tiebreak
credit. Loaded capture/tribute overlap and the final start/finish boundary use
the queried duration; increasing cargo does not restart an existing tribute.

Independent review confirmed clock persistence, channel priority, shopping and
source restrictions. A timeout's handled wait originally reported an action
without issuing one; the actual receipt is now separate. Repeated ally SDK reads
inside source ranking were replaced by one fresh action-free-pass position list
and a strength snapshot. Neither change retunes the clock threshold.

A bounded worst-actor audit then found a genuine ordinary-work loop on Islands
B/Fundamentals. Willow stands for 31 observed starts at full HP with all actions
available, while 74.6K fuel remains. Full component connectivity and routing-fuel
starvation were both ruled out. Work repeatedly selects source 44, observes
respawning timer zero, marks it unavailable for one round, then returns before
route/no-work recovery. World-only inspection finds an unseen opponent blocking
the source's other pad; occupied pads delay the whole group. The guest receives
no targeting rights from that evidence. The stationary allies themselves are
off this group's pads.

Respawning work is now quarantined for at least twelve rounds, retaining longer
queried timers. Refusal permits actual safe pad departure, one bounded alternate
work attempt, then actual recovery. A productive accepted capture wait may issue
no SDK action; a real action cannot be discarded after intent rejection. Clock
and global no-work age persist. Narrow independent review found no blocker;
nested failed refusal can attempt recovery twice, with no unbounded retry or
duplicate successful recall. The exact repaired window has 31 moves/31 starts,
93 receipts and no query errors. Willow earns 160 versus sixty gold and deals
1,475 versus 435 monster damage, with no deaths; her personal gems remain zero.
The sixty submitted gems are a team result. This proves the identified loop's
repair, not universal productivity.

Final `fbedecacc62e` covers forty pool maps, both sides, against Fundamentals,
Main, Tempo and Concentric: 104/320 versus same-run frozen 162/320, flips +7/-65,
McNemar p=6.9644e-13, paired gem-margin difference -309.36875 +/- 22.61752 SE.
Main is one quarter of the panel. Fresh legacy maps/three opponents give 87/102;
full-pool Starter gives 60/80. All 502 subject keys have no faults, exhaustion,
error lines, at-budget turns or team zero-clear flags. Observed maximum ordinary
fuel is 92,866 pool, 85,065 fresh and 91,499 screening. The final identical-roster
phase ablation is 9/16 in both arms, no flips, p=1, margin +1.5625 +/- 1.1364 SE.
Its 66 cutovers, fifteen terminal personal payouts and two terminal banks show
activation, with no detected strength benefit or threshold tuning.

Final bounded terminal evidence covers two declared windows, 62 owner starts,
259 receipts and zero query errors. Ambush Starter A Vendetta cuts over with 33,
starts a four-tick tribute at 1979, preserves its timer-zero pending channel,
banks 33 at 1983 and stays Gold through H; no gold payout follows in that window.
Collision Starter A Willow cuts over empty, physically holds already-cleared gold
sources, receives forty gold at 1986 and another forty at 1996, and departs after
each. No fresh kill or solo-clear claim is made. A same-actor bank-to-gold-payout
chain is unobserved on the repaired source. Final H-1/H admission, loaded overlap,
large-bag early-reserve persistence and timeout exit remain rules plus production
fixtures. Earlier positive episodes are superseded rather than substituted.

Final scouts find twenty-one zero-income actors among 1,440, none with zero
whole-game production, and a longest quiet interval of 1,882 rounds. The remaining
The Farm B/Fundamentals Vesper earns 53 gold and collects eleven unsubmitted gems,
with 550 monster/220 champion damage and no deaths. That gap is not classified by
the selected Islands audit; no universal productivity claim or further case hunt
follows. Fixtures pass 2/2 and all fifteen strict v3 profiles pass; fingerprint
`121463ad33f0`.

Current default `64adc5fd9b15`, measured active and native phase `8f2cc08c63b6`
match raw/injected WASM (SHA256 starts `724fa6d7e096`); explicit off `09b37ca10603`
and base `c8b8a248c78e` stay frozen. Root independently checked source/fixture
hashes, completed manifest, all ten relevant WASM files and native-summary hash
in `.cache/liquidation-clock-review/final-recovery-proof.json`. All pre-recovery
full panels/proofs are superseded. This remains a distinct losing experiment.

First durable placement `20261022-082625`: fourteen subject games, 4-10, without
technical or team zero-clear flags. Both affected placement games flag Banked
Vanguard outside this suite. Nearest coarse style is 05 at 0.33, without
head-to-head, and no convergence flag. The last bot should test a bounded immediate
opponent interruption, verify actual channel cancellation and then resume useful
own production. Timer-zero channels remain pending; ordinary damage does not
cancel tribute. Neither rule can be replaced by a generic zero-timer shortcut.
All 15 jobs and records closed and ownership was released before 16 began.

## 16: interrupt once, then repay useful production

[Bounded Interceptor](bounded-interceptor/README.md) fixes one in-place candidate: a fresh,
visible, living channeler with cargo, ordered by pending timer then handle. Actual own
channels, urgent conversion and physically held ready captures take priority. Cargo is a
denial/delay proxy compared with foregone own production, never predicted income. Current
HP independently covers two observed incoming attacks and a half-maximum reserve; Ryth
refuses public camp-pad AoE collateral. Typed effects, shields, stun immunity and fresh
allowances decide whether a spell or one basic can cancel. No approach, chase, cooldown wait
or second candidate is admitted. Ordinary short, cashable production, role gear, crowding,
loss reserves and twelve-gem/near-altar/wounded conversion interact with that rule.

An owed-production latch is set before the attempt, including an SDK refusal. It survives
death, respawn and source changes. Only a later normal-production effect repays it: actual
camp HP loss, safe selected reward-pad residence, personal payout or bank. Movement, plan
acceptance, enemy damage and interception kill rewards do not repay; post-attempt currency
becomes the new baseline. The latest15 routing, capture, spawn-rest and respawn-zero repairs
are local mechanics, without its terminal Clock program.

Broader testing rejected the first final source: resetting an available attack caused
thirty-four pool and seven Starter error lines. The pure planner and fresh SDK adapter now
both require a spent attack allowance before reset; refusal still leaves production owed.
Available/spent-basic and post-reset fixtures cover the repair. The in-place caller normally
has no spent attack, so enhanced-reset activation remains unproven rather than adding a first
hit to manufacture it. Earlier error-producing panels are superseded.

Final `c1187218e632` covers forty pool maps, both sides, against Fundamentals, Main, Tempo and
Concentric: 92/320 versus same-run frozen 162/320, flips +7/-77, McNemar
p=5.137564699362503e-16, paired gem-margin difference -346.534375 +/- 24.874005 SE.
Main occupies eighty of 320 keys. Fresh seventeen-map/three-opponent confirmation is 87/102;
all-pool Starter is 57/80. All 502 exact final subject keys have no faults, exhaustion,
error lines, at-budget events or team zero-clear flags. Maximum ordinary fuel is 92,668 pool,
86,552 fresh and 92,668 Starter. These are observations, not universal fuel bounds.

The fixed sixteen-key interception-only ablation holds every actual pick, ordinary policy
and finance rule equal. Both arms win 9/16, with no flips, p=1, margin difference -33.6875
+/- 24.94422424 SE. Ten logged attempts observe fresh cancellations and nine log later
production repayment. No threshold or case was changed to find activity or wins. Final
native evidence repeats the originally declared Collision/Starter A window: 32 owner starts,
235 receipts and no query errors. Tavi cancels four recalls with basics, alternates them with
real camp damage, then physically captures a personal eighty gold and four gems at 1912.
No bank occurs by 1922, and no own kill, solo-clear, permanent denial or net benefit is claimed.

Among 1,440 pool actors, nineteen have no personal income and four have no whole-game
production. Five fixed liveness windows cover 155 starts and 1,189 receipts without accepted
query errors. Road of Riches Willow shows exposed routing and death; three Lost World/Main
actors show finite combat blocking, withdrawal, death and respawn travel. No persistent
healthy refusal or closed route cycle is confirmed in those windows. Big World/Tempo Willow
walks every audited turn at full HP, while her whole 1,761-round quiet interval remains
unclassified. Neither movement nor team clears establish individual productivity. These
limitations remain part of the losing experiment; no further case hunt or policy tuning follows.

Default `a13174310754`, measured active and native explicit-interception `555e24980644`
match raw/injected WASM SHA256 starting `13f900bb9022`; off `9fa2543c6007` and base
`6ca0cb1fc715` remain frozen. Root independently checked all ten relevant WASMs, twelve
source-closure files, fixture/profile, actual completed manifest, health logs and native,
ablation, regression, scout and bounded-liveness hashes in
`.cache/interceptor-review/final-proof.json` (SHA256 starts `5cde6e478503`). An independent
parse confirms all 502 health rows and the map/side/Main coverage. Profile `4777e0c1f5c6`
is distinct; focused fixtures pass 2/2 and the complete suite fixtures pass 17/17.

Durable placement `20261022-085924` gives fourteen subject games, 7-7, without health flags.
The three affected other games flag Starter or Bargainer outside this suite. Nearest coarse
style is 08 at distance 0.56, without head-to-head or convergence flags. All sixteen bots'
source, docs, fixtures, scratch, children and jobs are closed and ownership is released.

## Completion checks

All sixteen bots have entry points, policies and focused fixtures, and their experimental
defaults select measured active behavior. The final complete fixture run passes 17/17.
The final v3 report contains sixteen distinct profiles, no failures, pending cases or
identical groups; root checked all sixteen current policy hashes against that report.
All 122 source files use LF. The final all-suite ladder style scan has zero convergence or
close-without-head-to-head flags. Two close pairs have only two decisive head-to-head games,
so coarse style and sparse placement do not certify global separation or optimality.

The suite retains distinct tested ideas and explicit weaker/null results. No Main improvement
or universal individual productivity is claimed. Full paired panels, native flows, repaired
failures and remaining evidence limits are recorded above and in the bot READMEs. The global
suite index remains an integrator-owned follow-up; this suite's implementation and local
validation are complete. All work started for this suite is stopped and all owners released.
