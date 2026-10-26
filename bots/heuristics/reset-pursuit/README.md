# 03 reset-pursuit

This controller combines short completion thresholds with deadline-aware source
admission, small useful liquidations, cargo-sensitive retreat and strength-first
equipment. Vendetta, Tavi and Marcella are the three-pick prefix. There is no
compulsory equipment-funding phase: a source's gold receives at most one shard's
marginal value while the first strength emblem remains unfinished.

Known, nearby jobs requiring at most two coarse work turns and already-cleared
jobs form an executable completion tier. Within the admitted tier, the selector
compares useful payoff per completion cost, adds one capture delay for a visible
contest threat and reserves health loss. Rewards unable to reach an altar by the
round limit lose their gem value. If every reward misses the horizon, the bot
still does productive work rather than refusing all sources. A commitment lasts
one queried capture-time window without new best distance or HP progress; a
new quick completion can interrupt travel. Only twelve candidates are
modelled, with action fuel reserved before detailed scanning.

The local executor calls `flow.h` for both production and focused fixtures.
It attacks a low-HP legal target A, observes actual death and reset cooldown,
chooses a freshly valid B before spending reset, and attacks B. It performs at
most two basics, one successful cast and three action attempts over six targets.
After A dies, B must differ from A. The second kill cannot buy a second cast.
Initially ready reset is counted as a spend, never as a kill refresh. The adapter
rechecks actual visibility, life, HP, action allowance and attack geometry;
range masks alone do not establish legality. Attack masks are cached only until
the next successful action. This flow does not attempt a speculative dash or a
reset-first attack after ranged movement.

The bot owns its runtime and frontier memory, copied from completed 02 and
renamed locally. It preserves witnessed-death expiry, legal unseen approaches,
persistent witnessed camp obstacles and safe route rescue. The disabled arm
includes the frozen shared control directly. No other bot's production code is
included.

## Evidence and limitations

Focused tests reach the actual selector and executor, including the mutable
40-damage A(20HP)/B(30HP)/C(30HP) transition: A/reset/B, two observed cooldown
refreshes, one cast, three attempts, and unchanged C. Refusals cover unavailable
casting, surviving A without refresh, hidden/dead/out-of-range B, failed reset,
post-reset invalidation, low fuel and a forbidden third hit.

The fixed `heuristics-profiles-v2` corpus fingerprints this policy as
`e92d1aa5e7b9`: versus 02, 64.5% selector, 22.0% banking, 12.6% risk, 36.0%
equipment and 40.0% roster disagreement. Profiles omit this combat flow.

Initial smoke exposed live-position queries on killed targets used only for
telemetry. Positions now come from pre-action observations or public camp spawn
metadata. A large-map post-move suspension also exposed redundant attack masks
and target-menu construction after Tavi had spent her ranged basic allowance on
walking. Masks now refresh by successful-action epoch, and action admission
precedes the unavailable continuation. These are execution repairs, not
coefficient tuning.

A first corrected full-pool run still had one zero-clear stall. All five actors
kept full health and repeatedly reversed successful steps as their chosen travel
sources alternated each round. Stage traces showed ample action fuel and fresh
goals, isolating selector thrash rather than a pathfinder fault. The completion
lease had protected only the quick tier. It now protects useful travel progress
for one capture-time window too, retaining historical minima and allowing quick
work to interrupt. The production fixture proves both bounded expiry and that
revisiting an old distance does not renew the lease. All eight failing-map keys
across four references and both sides are clean after this repair.

The final active default is enabled for diversity and productive behavior.
`HEURISTICS_ENABLED=0` preserves the frozen executable byte for byte (WASM
SHA256 `ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`).
Measured active source `8cd49049b57c` has WASM SHA256
`6c11d1d9afe12b81532ce5cb22725dc3bbdf3e9b53f707cb8e97ee1dbd4ea685`.
Promoted default source `2b415b498c4c` builds to exactly those measured bytes.

- `.cache/arena/sweeps/h12-03-complete`: all 40 pool maps, both sides, four
  opponents, 320 paired keys per arm. Active 158-162 versus frozen 162-158;
  flips +29/-33, McNemar p=.704, paired gem-margin difference +12.8 +/-21.4 SE.
  No detectable strength difference. Active wins: Fundamentals 35/80, Tempo
  46/80, main 0/80 (`8a7337f4849f`), Concentric 77/80.
- `.cache/arena/runs/h12-03-complete-confirm`: all 17 different legacy maps,
  Starter, Banked Vanguard and Bargainer, both sides: 102-0, 34/34 per reference.
- All 422 final active games have zero faults, fuel exhaustion, logged errors,
  zero-clear stalls or suspension. Pool maximum ordinary-turn fuel is 99,532;
  legacy maximum is 88,452. The pool maximum is close to the budget: this is
  observed coverage, not a universal bound. The frozen control retains eight
  stalls in the paired comparison.
- Focused tests pass 2/2 (`arena test heuristics/reset-pursuit`). The final v2
  profile report is
  `.cache/arena/heuristics-profiles/20261021-222438-86916-418032500/report.json`.

Final-source real flow and continuation: active paired replay
`00packs_reset-pursuit_fundamentals_3b4c57a0.repl26a`, team A Vendetta physical
actor 0 (log actor 2 is champion type), round 567, source 4 / `g_77`:
BASIC A(16,2), observed cooldown refresh, reset, different BASIC B(16,3).
The bounded flow performs two basics, one cast and three attempts. Tavi supplies
the group's final clear at round 570. Both hold the source; capture at 576 gives
Vendetta 60 gold and four gems. She personally banks those four at round 592,
raising team score 298 to 302. Exact state receipts:
`.cache/reset-pursuit-final-packs-567-receipts.json`. This is a shared clear and
capture, not a claim that the reset alone supplied the whole reward.

Coarse work is based on the existing executor's approximate damage rate, not an
exact monster hit forecast. The selector's shard-value proxy is the pinned
30-gold price; purchases themselves use queried item costs. Horizon feasibility
does not predict enemy locks or unseen route delays.

Next-bot lesson: finish worthwhile travel as well as nearby combat. A bounded
progress commitment prevents dynamic estimates from reversing a route every
step; historical minima prevent the resulting two-tile cycle from renewing it.
Reserve fuel before observation menus and distinguish a visible corpse tile
from live-unit visibility when observing kill effects. The next controller
should add a different execution rule rather than tune these thresholds.


## Current Main strength follow-up (23–24 October 2026)

The [current engine-1.0.3 baseline](../../../results/population/strength-v44-baselines/index.json)
pins incumbent `2b415b498c4c` against frozen live Main v44
`anchors/main-v44@b3ff333b6bcf`: all 40 pool maps, both sides, 0 wins and
80 losses, with both teams healthy.

`RESET_RANGED_DEFENCE=0` keeps the conditional response disabled. OFF
`c2447f43ba34` exactly reproduces incumbent RAW/injected SHA256
`6c11d1d9afe12b81532ce5cb22725dc3bbdf3e9b53f707cb8e97ee1dbd4ea685`.
All 80 fresh OFF games also reproduce the prior complete native statistics
byte for byte. ON `daf7f5deecc6` admits personally observed opponents within
the queried basic range to the existing bounded combat/reset executor. It adds
no pursuit. The mutable production-path fixture passes with common and the
existing case (3/3). Production preparation succeeds under the verified host
launch mode; earlier sandboxed failures remain recorded.

The initial four matched Ambush/Split keys increased banking from 931 to
1,626 gems, with healthy execution. This gain survived round normalization
and justified a full-pool measurement; fewer deaths was a causal hypothesis,
not an independent release requirement. The complete 160-game comparison
does not establish a general strength gain:

| Scope | Matched keys | OFF / ON wins | OFF / ON banked gems |
| --- | ---: | ---: | ---: |
| Discovery maps | 4 | 0 / 0 | 931 / 1,626 |
| Other 38 pool maps | 76 | 0 / 0 | 16,688 / 16,335 |
| All pool maps | 80 | 0 / 0 | 17,619 / 17,961 |

Across the 76 new keys, mean paired banking-rate change is -0.00352 gems per
completed round (descriptive SE 0.01249). Overall deaths rise 1,335 to 1,372;
potential roster exposure gives 3.587 to 3.661 deaths per 1,000 actor-rounds.
Actual roster sizes are used and partial final rounds excluded; alive/ready
exposure is unknown. All faults, exhaustion, logged errors and fuel-at-budget
counts are zero. ON nevertheless introduces a blocking raw zero-clear stall
on Corridors A: zero credited clears, 58 banked gems, 209 gold and 21 deaths.
Those positive economic totals do not erase the flag or prove general inactivity.

All 720 subject actor instances validate. Camp-damage actors are 360/360 OFF
and 359/360 ON; actors earning gold or collecting gems are 357 and 356;
banking actors are 283 and 292. Components remain separate: a gold-only camp
holder can earn without own damage, and zero-gold gem farmers can still bank.

One native Ambush B comparison preserves 12,799 exposed state/timeline frame
pairs before R90 Tavi6's first different command. From the same (37,55), she
personally sees Heart0 at (34,56), distance three. OFF moves to (36,55).
ON casts slot1 at the observed tile, then applies a basic without moving
during those R90 actions. Action-associated effects remove Heart's 150 shield,
reduce HP 1,000 to 996 to 951, and record stun source Tavi6. Target identity is
coordinate-inferred. Both bounded windows are complete with zero query errors;
private memory/fuel are outside the prefix.

This actor supplies no paid continuation before dying to Ryth at R115.
Her next own camp damage is sampled at R288, own clear at R295 and gold-only
payout at R302; she never collects gems or banks in that ON game. Preserve
the actual response and this continuation gap. Keep the prototype OFF; root
reviews the Corridors flag and visible-threat recovery before further work.
Exact full-pool, per-actor, native witness and cleanup records are indexed in
`.cache/arena/reset-strength-next/full-pool-index.json`.

## Visible-threat returnability experiment (24 October 2026)

`RESET_VISIBLE_SAFETY=1` is a disabled experiment in personally observed enemy
basic/cast exposure. It leaves the reset flow, roster, completion selector and
purchase order intact. A bounded observation of visible ready/next-tick casts
adds a conservative raw-damage reserve, avoids unsafe ordinary/banking steps,
and chooses recovery steps by total exposure before clearance. It neither
queries hidden enemies nor adds pursuit. Optional observation falls back to the
complete old observation when its fuel admission cannot finish.

OFF `64f111bc156d` preserves incumbent RAW and injected SHA256 `6c11d1d9afe12b81532ce5cb22725dc3bbdf3e9b53f707cb8e97ee1dbd4ea685`.
Its sixteen Fundamentals discovery rows also match the preceding census exactly.
ON `f8c7c1e1787b` has RAW/injected SHA256 `7be6a5aa077739a55c86de94d8696f122f17ee67fbc1ec6156065d32245546ab`.
The existing three focused test cases pass; the actual production route-danger
and escape helpers are covered, including overlapping opponents and a safe
spell-only fringe.

`reset-strength/visible-safety-discovery` measured eight predeclared pool maps,
both sides, against Main v51 and Fundamentals: 64 matched-arm games, no recorded
health flags on either team. Main deaths fell 215 to 174, while banking fell
5,673 to 5,382 and neither arm won any of its sixteen Main games. Fundamentals
wins fell 6/16 to 3/16. These aggregate contrasts do not establish safety causality.

The unchanged ON arm then completed `reset-strength/visible-safety-main51`:
all forty pool maps, both sides, against frozen Main v51, **0/80 wins** and no
recorded health flags on either team. Thus it fails the requested one-win floor.
The experiment stays OFF. It is not promoted on the basis of fewer deaths, and
no threshold tuning or heldout-map fishing follows from this result. Selected
Split B attribution changes the interpretation: the first observed ready-track
difference is Vesper actor 9 at R74. In the complete native R70–76 window, both
R73 starts show (36,20), 760 HP, 55 Strength and all action allowances, with no
personally visible enemy. OFF moves to (35,21); ON ends without an action. Thus
observer/dispatch fuel already perturbs production before visible-threat behavior
is isolated. This is not a verified whole-world prefix or an explanation of all
aggregate differences.

A bounded coarse screen of the same complete OFF Split B timeline (last ready
sample R962) finds no living own actor within four tiles of a same-round enemy
ready sample with 1–300 HP. This does not establish any wounded-finisher
opportunity, nor prove none exists elsewhere: actor samples are different turn
starts and do not establish personal visibility or action legality. No blind
finisher policy is added from a low aggregate kill count.

`reset-strength/visible-safety-calls` uses the existing native `--games` and
`--calls-log` path for the same selected OFF/ON Split B keys, one slot each. Its
logged runs exactly reproduce each unlogged outcome row, replay payload, both
fuel streams and both stdout streams. At R70 and R71, when no enemy is visible,
ON adds one `bc_Bitboard_Zero` call (30 charged fuel) and 111 instruction fuel,
141 total. At R73 OFF computes a distance field and moves; ON does neither.
Per-turn aggregate imports cannot identify the private selection or precise
earlier gate responsible. This supports testing an empty-observation fast path
before attributing the difference to PvP. The selected counts and neutrality
hashes are retained in [the import discriminator](../../../results/population/reset-visible-safety/import-discriminator.json);
bulk logs are discarded when the experiment closes.

### Inactive observation fast path

The corrected experiment returns before clearing an already inactive, empty
observation. On the first turn after an enemy disappears, the production helper
clears both the threat list and derived route mask once. Focused fixtures cover
normal/recovery steps after disappearance and repeated inactive turns. The real
threat rule, reset flow and all thresholds are unchanged.

Corrected OFF `4845c70bb3f9` still produces incumbent `6c11d1d9afe1...` in both
module roles. Corrected ON `4bfa6f79e8e5` produces
`137aef63f26b576ca4be10f84e43cc2eb1d903994bc07ad9428a244d057701b2`.
`reset-strength/visible-safety-fast-split` and `visible-safety-fast-calls`
repeat the exact Split B pair. Counted and uncounted outcomes, replay and both
teams' fuel/stdout streams match byte for byte. At R70/R71 the additional clear
import is gone, but ON still spends 102 extra instruction fuel with identical
import counts/charges. Its entire public replay payload equals the earlier ON
payload: the R73 no-contact movement does not return. This does not identify a
particular private selection or admission gate.

The unchanged corrected ON source completed
`reset-strength/visible-safety-fast-main51`: **0/80 wins**, all forty pool maps
and both sides against frozen Main v51; neither team has recorded health flags.
It also fails the one-win floor and remains disabled. The earlier prototype's
0/80 remains a separate closed result. The next step needs a new observed
productive/finishing opportunity, not repeated threshold changes or a claim that
lower mean mortality establishes useful Main coverage.

[Corrected import evidence](../../../results/population/reset-visible-safety/fast-import-discriminator.json)
preserves the bounded counts and neutrality checks. All three corrected
experiments are closed after their answers are recorded.

[Compact current evidence](../../../results/population/reset-visible-safety/index.json)
records identities, complete experiment coverage, native observations and their
limits. Fuel maxima, per-actor liveness and absence of stalls are not established
by the fast rows. Both experiments are closed; diagnostic replays can be
regenerated from their exact keys.

### Current v52 PvP opportunity screen

`strength-reset/pvp-opportunity-v52` selected 01elk-arena A before replaying it:
it had the highest champion damage among all 80 incumbent rows against Main v51
(all forty pool maps, both sides; zero wins and zero kills). Against current frozen
Main v52 `4aaf587c22cc`, this one diagnostic key loses with 8,129 champion damage,
14 deaths, zero kills and zero collected or banked gems. Neither team has recorded
fast-row health flags. This is a selected mechanism case, not a current full-floor
or strength result.

Complete native windows R40–56 and R472–482 reject two tempting finishers. The
early actor is Varg, not Tavi. At R43 its sole one-step destination adjacent to
300-HP Ryth is the unwalkable altar (27,27); even two raw basics total only 80.
Varg's ready shield is an unused defensive capability, but westward clearance from
Ryth alone does not certify safe escape from nearby camps or a paid continuation.
At Marcella's R476 action, enemy Vendetta has 350.3 HP, not the later 278.3 sample;
both spells are cooling. Vendetta attacks and moves before Varg's action. At
Marcella R477, the ready raw-120 spell cannot kill the 278.3-HP enemy and excludes
movement; the reset is still cooling. Same-round actor samples must not be treated
as a shared decision state.

There is a narrower legal candidate: actual Tavi at R472 moves to its escape
endpoint (19,22), then ends with attack spent, cast available and reset ready.
The personally visible, stunned enemy Vendetta remains in basic range three.
Self-reset then one basic would preserve that endpoint. Enemy personal vision
currently excludes Tavi; firing can reveal the attacker, so later escape safety
is unproven. This audit does not execute the counterfactual or establish a useful
income consequence, so it does not justify adding general harassment. The production
reset flow deliberately starts with an available basic and excludes this
after-movement reset word, as stated above. Reconsidering that boundary needs a
productive native witness, rather than treating the choice as missing support.

The selection rule changes: maximum champion damage led to a zero-income brawl.
Stop this case and choose a productive Reset game next, looking for an actual
reset/retarget or escape interruption with a paid completion or bank consequence.
An executable continuation would change this decision. No source behavior changes,
threshold tuning or broad panel follow from this screen; existing experimental
knobs stay OFF. The selected experiment is closed after retaining
[the compact observations and exact recipe](../../../results/population/reset-visible-safety/pvp-opportunity-v52.json).

### Productive bank returnability, v52

Productive historical 00lanes A selected the next diagnostic; its v51 numbers
are not current-opponent evidence. `strength-reset/productive-lanes52` measures
the current incumbent against frozen Main v52: 900 banked from 950 collected,
two deaths, a 900–1005 loss. Native receipts bind both deaths to paid cargo:
Vesper collects ten gems at R892 and Vendetta twelve at R995, then bank routes
enter personally visible opponents' ready burst reach before recovery begins.

`RESET_BANK_RETURNABILITY=1` tests a bank-only decision: avoid directly targeted
basic/damage/CC words reachable by one ordinary move and one cast while converting
already-earned cargo. It uses fresh personal enemies and queried ability metadata
and cooldowns. Ranged walking, dash and reset allowances are kept separate;
movement-exclusive casts cannot add a walk, and reset never buys another move.
An actor already inside a footprint keeps its incumbent escape/fight behavior.
The optional scan runs only at an available bank movement with one to five visible
opponents and more than 50k fuel; the existing 35k movement floor is unchanged.
This does not model collateral basic splash, future purchases or unseen enemies.
It preserves the roster, finance, completion selector and deliberate dash/reset
boundaries. A missing avoidance path retains existing bank fallthrough work.

OFF `deacf2e65276` still has incumbent raw/injected SHA256 `6c11d1d9afe1...`.
ON `018e7bcbab87` has raw/injected SHA256
`3a1aa29272777a09622e0d538c52d36219c347e91e9bd10f1939b906028b2012`.
All three focused fixtures pass, including the production reach/mask rule,
movement allowances and already-exposed fallback.

The first ready-track difference is actor 0 at R521. Complete native R520
queried own and visible-opponent states agree, including decoded personal vision:
Vendetta has 590 HP and six gems at (46,6). OFF moves to (47,5), within the
visible ready Heart word; ON moves to (45,5). ON subsequently observes no lock
at the left altar, tributes R536, and native R537 receipts change its bag 6→0
and team score 283→289. At R541 it kills cluster 32, spends an initially ready
reset and attacks a different wolf 80. That is a reset spend, not a kill refresh.
The next completed group pays it eleven gems and forty gold at R557. Thus a real
bank → reset/retarget → next paid work sequence is executed. The optional full
world prefix scan times out after 120 seconds with its process tree confirmed
empty; whole-prefix equality remains unestablished and is not retried.

`strength-reset/bank-returnability-lanes52` reproduces the baseline exactly;
ON banks 965/975 collected with one death, still losing 965–1015. Both teams'
recorded health dictionaries are empty. This selected aggregate gain is not all
attributed to one route. The unchanged ON then completes
`strength-reset/bank-returnability-main52`: **0/80 strict wins**, all forty pool
maps and both sides against exact frozen Main v52; neither team has recorded
health flags. Full OFF pairing awaits the independent current-default census;
there is no broad improvement claim.

The component is verified finished work but fails the requested win floor.
It stays OFF. Do not tune its reach margins or fish for another map: a different
limiting uncertainty must justify further Reset work. All three answered
experiments are closed, and selected raw diagnostics are discarded after
[compact proof](../../../results/population/reset-visible-safety/bank-returnability-v52.json).
