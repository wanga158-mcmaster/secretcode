# Strategy: the structure of the game, and the answers built on it

The top layer of the foundation (see [README](README.md)). Entries here state structural facts of
the game, the properties every strategy must answer whatever the opponent does, and connect them
to the layers below. They start as hypotheses derived from the rules and the evidence we hold; the
mechanics, economy, combat and online pages confirm, quantify or refute them. Each contender
derives its own answer from the same structure (table at the end); that is what keeps them
grounded and different at once.

G1-G10 preserve the original engine 1.0.2 analyses and their coverage. Claims about main internals
are inherited historical context, not independent premises for the current contenders. The 24 October
renewal corrects average-rate escape sufficiency (C22) and adds the executable-continuation result G11.

### G1. The camps are one shared, regenerating pool taken first come: every capture is also a denial
Claim: both teams harvest the same camps; a group we capture is unavailable to the enemy until it
  respawns (timer from the claim, delayed while any member spawn tile is occupied). So yield at
  contested camps counts twice (our gain, their loss), and being first at a respawn is worth more
  than an uncontested camp of equal yield. The race is not two independent production lines.
  Occupancy throttles the pool: a group respawns only once every member tile is empty, so a body
  parked on a captured group's tile freezes that group's income stream for both teams, and one
  enemy body on a member tile resets a capture in progress.
Evidence: mechanics M13-M16 (verified by probe: reward waits, arm + 5 phases, any enemy body on a
  member tile resets, respawn after `max_respawn_timer()` only with the tiles empty). Online O10:
  where one group holds most of a map's gems, the side that captures it more won 31 of 36 sampled
  games. Magnitude still unmeasured: what share of each map's capacity is contested (maps.md).
Rests on: M13-M16, P (contested groups), E (capacity), O10.
Status: mechanism verified (engine 1.0.2); its share of the race pending economy C and maps.
Recheck: any change to capture, respawn or reward rules.
Consequence: a camp's value is its yield plus the yield it denies, weighted by the chance the
  enemy would take it; contested ground deserves more than its face value; respawn timing matters;
  occupying a tile is a denial move with a price (the occupier's time) and a trap for our own idle
  capturers.

### G2. A kill is worth a fixed slice of the race; a fight without a kill costs both sides time
Claim: a death costs the victim 60 rounds dead plus the walk back to the field, hands its whole
  bag and 100 gold to the killer: about 40 gems plus twice the victim's bag, 5-10% of the race per
  kill. An engagement that ends without a kill only turns farming time into HP, which a recall
  restores at a further time cost.
Evidence: mechanics M22 (the last hitter alone gets 100 gold and the whole bag; a camp kill
  destroys it). Economy E25-E26 and combat C17: one kill moves about 40 gems plus twice the victim's
  bag; a 10% income-rate edge is worth 2-3 bag kills. Online: 5+ deaths by r400 lose 95% (main
  charter, 150 games); when gold and kills disagree the gold leader wins 74% (O9); main's champions
  spend ~200 champion-rounds a game in quiet contact, the #1 team ~90 (topteams.md).
Rests on: M22, E25, E26, C17, O9.
Status: model (kill value) and measured (online).
Recheck: changes to respawn time, kill gold or bag transfer.
Consequence: fight where a kill is likely or a camp is at stake; leave even fights early; never
  stand in contact doing nothing.

### G3. Contact is the whole risk, and leaving is decided before or at contact, never after
Claim: at equal speed walking never opens distance: a melee chaser in a different turn slot hits a
  fleeing champion every round, one in the same slot every second round (G9). Contact breaks for
  free only through a dash against a chaser without one, or because the chaser chooses to stop;
  a walk-blocking stun on the chaser does not help an escape, and a retreating melee champion should
  keep hitting (same damage taken, 500-700 dealt). Contact is also where almost all deaths happen:
  online, a champion with no enemy within 6 dies at 0.007 per 1,000 rounds, with one enemy within 3
  at 5.7, with two or more at 18.2 (91% of kills have an enemy within 3). Between top teams 75% of
  fight episodes still end in separation, because neither side insists; deaths come from staying:
  victims spent a median 18 rounds in contact that began at full health, and death risk rises with
  the exchange ratio (damage taken / dealt < 0.5: 2%; 0.5-1: 13%; 1-2: 32%; 2-4: 48%).
Evidence: combat C2, C7-C10 (engine-verified chases and escapes on open ground), C21 (hazard over
  9.8 million online champion-rounds); online O13-O14 (70,605 contact episodes, 12,740 deaths);
  main's fight calibration (retreats starting adjacent die 27% within 15 rounds) and collapse
  analysis (47% of main's trap-map deaths are retreats that never end) are the late-exit side of
  the same law.
Rests on: M2, M23, M26-M28, C2, C7-C10, C21, O13, O14.
Status: verified (engine 1.0.2, open ground; terrain in chases unmeasured) and measured (online).
  Revised 22 Oct twice: "contact is sticky" (too strong, O13) and the chase mechanism (C2).
Recheck: any change to movement, dash or stun rules, or a speed-changing item.
Consequence: A proposed way out needs a physically executable continuation whose every ordered damage
  prefix is affordable under its explicit enemy, geometry and delay assumptions (C22). Chase-rate
  averages are useful estimates, not sufficient escape bounds. A ready dash or nearby support still
  requires actual route/landing or timed individual support effects. Read the exchange from the first
  round; waiting in a losing exchange is another physical damage exposure.

### G4. Numbers decide the fights that happen; grouping elsewhere is pure throughput cost
Claim: fights follow a square law: N against M leaves about sqrt(N^2 - M^2) survivors, so one extra
  champion turns a coin flip into a win that keeps most of the team, and an even fight is a coin
  flip (decided by first strike, G9) that wrecks both sides. Focus fire is worth about one champion
  (3 against 3, the focusing side loses nobody). Online, a second enemy within 3 triples the death
  hazard and allies within 6 cut it 2.4-fold; 19-21% of deaths are a lone champion against two or
  more. But captures do not speed up with allies and rewards split, so a team that stays grouped
  farms fewer camps: between top teams the more-grouped side won only 14 of 38 games. The law that
  joins the two: numbers are needed only where contact can happen, inside the enemies' two-turn
  reach plus their arrival time; everywhere else grouping costs throughput, except on big content
  (G8) and where proximity converts into burst kills (the #3 team, grouped Focus burst, has the top
  K/D). Area damage scales with clumping; two tiles of spacing defeats it.
Evidence: combat C11-C14 (engine-verified up to 3 against 3 with full kits; model beyond), C16,
  C21; online O14, O15, O18; rules (capture, reward split); economy E8. Victims die with 1.5-1.7
  enemies within 3 in their last rounds (O14), fewer than the "3-3.5 participants per kill" of
  main's topteams.md, which counted assists over a longer window.
Rests on: C11-C14, C16, C21, O14, O15, E8.
Status: verified (engine, up to 3v3) and measured (online association; losing teams may group
  defensively).
Recheck: balance changes to burst or area abilities, capture or reward-sharing rules.
Consequence: never be the lone champion within reach of two enemies; never take an even fight
  (hold outside the enemy's two-turn reach and let it choose); focus one target; space allies two
  tiles apart against area kits; group only where contact can happen, for big content, or for burst.

### G5. Information is local, the team's shared picture is 8 bytes, and the static map is fully known
Claim: each champion sees 8 tiles plus its own ward; allies' views are not shared; 8 shared bytes
  are the only dynamic team channel. The draft (10x fuel, the whole map, 1,024 bytes of draft
  memory) and each champion's first turn (2x fuel) can precompute everything static.
  A visible enemy, though, is readable almost completely (cooldowns, shield, bag, whether it has
  acted this round), all but its gold; camp state, respawn timers included, only in vision.
Evidence: mechanics M44-M51 (vision is personal; shared writes are visible to later actors in the
  same round; draft memory frozen in play); fuel budgets 1,000,000 / 200,000 / 100,000 (S21).
Rests on: M44-M51, S21-S22.
Status: verified (engine 1.0.2).
Recheck: changes to vision, memory or fuel.
Consequence: static analysis is free, done once and deterministically; the 8 bytes go to the
  facts that change decisions most (enemy sightings, claims, calls to converge or retreat).

### G6. The map decides the currency, and the race lasts about 1,100 rounds
Claim: gold compounds (gold -> items -> faster clears and fewer deaths -> more gold), so on most
  maps the gold leader wins: at r400 the earned-gold leader wins 78% of games, and when gold and
  kills disagree the gold leader wins 74%. But compounding needs time: on gem-rich maps the race
  ends earlier (median r942) and the banked-gem leader beats the gold leader in two thirds of
  conflicts; where one group holds most of a map's gems (a jackpot, such as a 200-gem monolith),
  the side that captures it more often won 31 of 36 sampled games. Top games are won at a median
  r1085, so r400 is a third of a game and a plan must cover the whole race.
  Gold itself scores nothing: it counts only through items (E17), whose value is the health and
  speed they buy; items cut the speedrun 41% on damage maps but only 6-16% on capture and respawn
  maps (E13), and the round-2000 tiebreak is in practice a gem count (E27).
Evidence: online O7-O10 (225 games between other teams, 69 top-vs-top; 1,124 games reaching
  r400 for the currency conflicts); economy E13, E17-E20, E24, E27; maps P4 (binding class).
Rests on: E13, E17-E20, E24, E27, P4, O7-O10.
Status: measured (online, engine 1.0.2, 21-22 Oct) and model (economy).
Recheck: map pool changes, camp yields, item growth, winning_gems, max_rounds.
Consequence: no fixed gem/gold exchange rate (any opponent can exploit one); price each currency
  by the map's supply and the expected length of the race; treat a jackpot as income plus denial.

Current opaque passive calibration is separately scoped in E15: RAW v53 reaches 1,000 on all 80
unique pool-map/side keys, native event-round median 929 and mean 950.4375. This is an absolute
pool/passive reference, with partial-final-round timing; it supplies no paired historical improvement,
contender-strength conclusion, current contested clock or explanation of private micro.

### G7. Fundamentals, not style, separate strong teams from weak ones
Claim: per alive champion-round over r1-400, top-10 teams earn 1.05 gold (main's online v33-v36
  0.89, all its versions 0.84; ranks 401+ 0.39), spend 102 champion-rounds dead (main v33-v36 121)
  and die in 13% of their fight episodes (main v33-v36 26%). Locally main-v35 measures 1.04 / 53 /
  12% against the battery's strategy set and 0.95 / 85 / 18% against its own lineage (contenders
  `tools/README.md`): the local opposition kills far less than the online top does, so local
  survival numbers flatter every bot. No style choice (grouping, drafts, locks, early aggression, builds) carries a robust
  rating-adjusted edge, and once team strength is accounted for no champion pick is worth more
  than about 2.5 points. The top three teams win three different ways (survival-first dispersed
  farming; a gem-carrying Prism; grouped Focus burst).
Evidence: online O2, O3, O15-O18.
Rests on: O2, O3.
Status: measured (association; a team that is ahead farms faster, so the rates are partly
  outcome-laden).
Recheck: each sprint (teams can hide bots), each patch.
Consequence: the targets are opponent-independent: farm rate per alive round, dead time, deaths
  per fight episode. A style is only a means to them, which is why three different styles can
  each be the right answer.

### G8. The gem supply is locked behind health, so party size is a function of the target
Claim: 76% of the pool's gems sit in elk (42%) and monolith (35%) groups; wolves hold 13%, gem
  clusters 6%. Monsters strike the nearest champion, so a clear costs W/(n delta) damage (W the
  group's Smith-weighted health, n champions of damage rate delta) while its labour H/delta does
  not depend on n; with covered rotation the champion-rounds per capture are minimised at
  n* = sqrt(W T_trip / (delta h walk)): solo for wolves and clusters, pairs for single elks, three
  to four for elk packs, the whole team for golems and monoliths (a monolith siege runs in waves,
  145-175 rounds at base, ~100 with Jungle). Solo-only play competes for 19% of the gems: the best
  solo-only schedule takes 896 rounds against 674 with target-sized parties (passive opponent), and
  never banks 1,000 on four maps. Online almost nobody besieges golems or monoliths early (1-6% of
  sides), so this content is the least contested part of the pool.
Evidence: maps P3 (census of the 836 pool groups); economy E1, E4, E8, E9, E13, E15 (law derived
  from the verified per-round damage rule, simulator calibrated on solo probes within 2 rounds;
  schedules on 40 pool + 17 legacy maps); online O1 (early siege 1-6% of sides), O10 (jackpots).
  Engine test (E8, E9, E28; scratch bot `_econ-siege` on a lab map, one game per configuration):
  the party law holds within 17% (elk pack by a trio 27 rounds against 25-29 modelled, single elk by
  a pair 18 against 18, golem by five 21 against 17, golem by a melee trio 61 against 57-60); a
  monolith with Jungle matches (119 rounds against 109-111; 81 when champions back from spawn rejoin
  the running fight); at base it ran 218 rounds with a death against 159-178, because execution
  breaks it: staggered arrival, recalling inside its reach, a ranged ally that cannot cover a melee
  walking out. Rallying costs 13-19 rounds of waiting per clean wave (E28).
Rests on: M5-M10, M14-M16, E1-E15, E28, P3.
Status: law derived from verified mechanics and confirmed in the engine for elk, elk packs, golems
  and the Jungle monolith siege; base-stat monolith sieges are fragile in execution.
Recheck: camp health, strength or range; recall time; the pool's composition.
Consequence: rate each target with its own best party and commit that many allies to it (target,
  size, rally point through the shared bytes); a doctrine of pure solo play or of fixed squads
  loses 300+ rounds on the maps whose content it does not fit. This is the structural cause of
  most of main's 320-round gap to the schedule (E15): its target rule rates every group for one
  champion plus at most one joiner.

### G9. Tempo belongs to pairs: turn slots decide first strikes, catches and escapes
Claim: Champions act one at a time with within-round team interleave; the starting team swaps each
  round. Equal-body teams can still have consecutive same-team inputs across a round boundary (M2).
  Projecting to two actors with fixed ranks across adjacent rounds gives strict pair alternation
  for unequal ranks and double pair turns for equal ranks (C2); other actors and camp phases can
  intervene. Priority changes can alter the next ranks, so this pattern is conditional, not a
  universal future cadence. The finite chase/duel measurements use their recorded fixed schedules:
  same-rank mirror results can flip with the parity of contact. Stun chains need actual casts on
  the appropriate sides of the target's owner-input boundary.
Evidence: combat C2 (engine-verified slot mirrors and chases), C16; mechanics M2, M26.
Rests on: M2, M26, C2.
Status: fixed-schedule consequences measured (engine 1.0.2); current/next scheduling contract from
  pinned SDK and M45's scoped current-native completion reset. Dynamic-rank opportunity counts are
  conditional schedule deductions in C2, not new native fight measurements.
Recheck: any change to turn scheduling or priority.
Consequence: a fight model must resolve turns per pair, not as simultaneous volleys: main's
  model cannot see first strikes, catches or escapes, which is the structural reason its fights
  resolve about 3x slower than predicted and its deaths are predicted 3.6x too often (C19).
  Approach to strike first (end a turn just outside reach when acting again next); use priority
  as a lever for chases, escapes and stun timing.
  Price the independent camp clock: the next scheduled owner input in R+1 crosses one camp batch,
  regardless of two possible opposing inputs (M2/C2). A ready-owner timer1 shield covers that batch
  before owner-start expiry if it remains present. Multi-input or next-usable-action horizons need
  their actual camp count and separate existing-effect updates; no new native phase test is claimed.

### G10. Contact safeguards are conditional hypotheses
Claim: eight historical candidate heuristics are collected in combat C20: (1) no contact without an
  executable way out whose ordered damage prefixes are affordable (C22), or a fight the admitted
  model calls won, counting every enemy that can arrive; (2) never
  the lone champion within reach of two enemies; (3) melee retreats hit back, ranged champions
  stand, and kite only a chaser with neither dash nor stun, with a dash; (4) focus one target and
  stagger stuns; (5) allies two tiles apart against area kits; (6) never nearer than the enemy to
  an awake camp in a fight; (7) contact, not carrying, is the risk: carry freely out of contact,
  bank or break off when an enemy enters its two-turn reach; (8) no even fights: hold outside the
  enemy's two-turn reach and let it choose. Online, 91% of kills happen with an enemy within 3 and
  1% with none within 6. This concentration is descriptive and does not certify the safety of a
  particular continuation.
Evidence: combat C7-C18, C20, C21.
Rests on: G3, G4, G9.
Status: conditional hypotheses; C22 refutes the previous average-rate sufficient escape test.
  Their combined effect remains untested in games.
Recheck: any change to the entries they rest on.
Consequence: Each contender states its admitted model and premises, checks the corresponding action
  contracts, then measures its actual continuations in games. The current three derivations below
  remain distinct; no heuristic is promoted to an invariant without establishing its premises.

### G11. The complete action word includes its continuation
Claim: Useful work is an ordered state transition with a feasible continuation. Pricing every attack
as followed by one compulsory exposed hold rejects some real melee operators; pricing an attack
without its required exit can admit work the actual executor cannot survive. Work admission and
execution must refer to the same ordered word and actual remaining M/A/C permissions.

Model discriminator: a melee champion at exposed tile P has HP100, no shield and fresh movement.
Its basic preserves M. Quiet tile Q is reachable by its actual ordinary egress. Assume one unresolved
refusal can force at most one additional exposed camp phase, costing60; there are no other hazards or
support. `basic -> same-input egress` has at most60 damage under those premises. A model that inserts
one mandatory exposed hold before the same egress adds another60, rejects at120, and has excluded a
feasible word. If the actual suffix is not available or its delay/refusal bound is unestablished, the
first price is also unestablished. The example does not grant arbitrary damaging work or general safety.

Evidence: M23's melee action contract; independent Mosaic source/model review of the previous HOLD1
admission class, with the new word and actual-M checks recorded in `bots/contenders/mosaic/NOTES.md`.
The 60/100/one-refusal case is a finite model discriminator, not a native-game outcome. S26 supplies
the distinct current-native counterexample to assuming a mathematically available action will run.
M54 supplies the distinct counterexample to discarding hidden danger after a move.
Rests on: actual move/basic permissions, actor/camp phase order, explicit delay and refusal bounds.
Status: derived operator distinction. General native implementation refinement remains open for each bot:
measure accepted work, actual same-input egress, later exposure, complete payout/bank and repeated
production from the exact executable. A passing pure word fixture does not establish those endpoints.
The R8 Mosaic application remains unqualified: its owner's matched passive games have mixed
production and more deaths (`results/contenders/mosaic/melee-exit-r8/assessment.json`). The model
distinction grants no implemented safety or overall improvement. The distinct bounded semantic witness
`results/contenders/mosaic/melee-exit-r8/death-witness.json` shows Varg yielding ready R796/R797 under
elk exposure while visible ally3 occupies its previously reached quiet tile and recalls there. It
contains no added Basic-to-immediate-egress word and no refused move; it does not identify the private
return anchor/branch or the cause of the baseline/ON trajectory difference (S26).

A subsequent exact native refinement establishes this word and its complete physical suffix on one
real trajectory: `results/contenders/mosaic/route-egress-r9/word-witness.json`, SHA256
`abcca09c0da270bf7323de11cc730381a0bc7fdffaaad5414e49b3329ec8b472`. On Yellow Brick Road B/frozen
passive, ordinary `contenders/mosaic@4fb30563e5af`, selected RAW module
`8f8196fa3a2686ad2ede728daa1396b1570ec623128f5c424d50e2d31051be3b`, actor6 Ryth actually basics
from(29,13) at R52 with HP600, retains M and walks to(30,13) in that same owner input. Ordinary moves
then reach(31,13) R53/HP580, (32,13) R54/HP560, (33,13) R55/HP560 and own-spawn(34,14) R56/HP560.
The endpoint has zero coverage from every positive-Strength initial camp footprint; actual HP1200
at R57 establishes spawn recovery. This is actual Basic -> same-input move -> dated egress -> quiet
endpoint -> recovery, not an inferred reverse path or only the first escape step.

The diagnostic `c05d518e4076`, RAW
`5e26d9fcf1e9cf654fab05fdc2be46fa6b4efd4ccc5e6cb872b01f24c62e1593`, reports HOLD1 worst loss600
versus immediate-egress440 at HP600: the strict gate rejects the former and admits the latter.
Those are conservative CAMP-model prices, not actual damage; actual loss along the shown suffix is40.
Its entire native replay member equals the ordinary game's SHA256
`1909e6b957a78921733bd66575058d7987c2c1fde5654bd166d7b76754fa5d5f`, binding the private diagnostic
admission to identical physical gameplay. Their stdout and fuel differ, so fuel equivalence is not
claimed. Only these original module roles/keys qualify this witness; later edits inherit no result.
The complete ordinary B game still has zero clears, banked gems and earned gold, with a stall. Thus
physical work/egress/recovery is now witnessed, but repeated useful conversion, full controller
liveness, arbitrary hidden blockers/enemy interference and competitive strength remain unqualified.
Recheck: movement/attack interactions, native refusal behavior or executor dispatch change.
Consequence: A complete controller keeps the following facts aligned: the observation epoch; actual
work and cast effects; remaining movement; the retained exit/return route; and the latest safe dispatch
point before an external damage phase. A successful displacement invalidates old route origins. A
refused dash that leaves the pose unchanged does not by itself erase the old physical return route,
although revealed occupancy must update its premises. A deferred query/planning phase needs an
affordable waiting continuation or enough reserved computation to execute the admitted suffix now.
If a previously priced egress endpoint is occupied, allies cannot be assumed to leave in time. The
continuation must establish a current alternative, an actual timely release, or an affordable bounded
wait. No safe alternative is established by the R8 witness merely because M/A/C remain available.

The singleton-endpoint class has a concrete source/model counterexample. Let actual history be quiet
A=(0,0) -> exposed P=(1,0); an ally occupies A for at least two camp phases. A source at (4,0), range3,
deals60 at P, while adjacent free floor B=(0,1) is quiet. HP100, ready M and sufficient computation
make P->B an immediate legal quiet return under these explicit premises. The old fallback retains
singleton A because its static exposure is zero, filters A as occupied, issues no move and returns
for the whole owner input; two waits kill. This proves that operator class can exclude a useful
continuation, not that B or that private branch exists in the actual R8 state. The replacement class
is blocked return -> observed legal quiet reanchor or separately priced multi-step suffix -> actual
recovery/conversion -> renewed useful service. Proposed future edges remain a plan; only accepted
motion becomes actual history. A refusal or replan must not reset its finite waiting allowance.

An admitted multi-input operator also needs dispatch ownership of its remaining suffix. After a
paid observation step, unchanged Unknown life does not by itself invalidate the already priced next
approach leg, and a generic quiet-staging guard cannot silently replace it with retreat. Fresh
Alive/Reward or a revealed blocker requires revalidation and may justify a different complete
continuation; it does not erase actual motion, the return geometry, or consumed refusal/wait
allowances. Transfer ownership only with an actual completion receipt, a relevant failed premise,
or a freshly admitted replacement including its waiting/exit cost. Retaining geometry alone is
insufficient when an earlier caller preempts its executor. These are composed execution obligations,
not a claim that an unchanged route stays safe or an attribution of a native stall.
Crucially, a priced single entry edge plus its reverse is not an already admitted multi-input
observation ingress. If that is the controller's supported class, retreat may fulfill its contract
while preventing further approach. Add the complete observation owner or a proved composition of
step/wait/return capabilities; relaxing a quiet-only staging guard does not create that capability.

Fresh Reward can establish a different staging class from Unknown. In exact Timetable source
`92f07f0fca76`, RAW subject `d64f0d716cec`, Ryth personally sees native group19/g_970 Reward at R239
from (9,23), HP900. The selected dead wolf at (11,24) still contributes 20 to immutable design support,
while complete matched geometry/current runtime camp stats give external potential 0 (nearest
external wolf distance6/range3, elk7/range4). Actual reverse steps at R240/241 reach (8,22)/(7,21)
with no HP change. The retained proof is
`results/contenders/timetable/independent/92f07f0fca76/capture-assessment.json`, a separate window
from source556's Heart evidence. This grounds fresh
Reward/external-zero staging followed by bounded geometry preparation, subject to current foe,
effect and computation guards; it does not authorize Unknown staging or certify a private branch
as the reason for retreat. Static selected-group support need not remain a current hazard after
fresh whole-group Reward, but every external source and later continuation still needs coverage.
Each later staging input refreshes Reward; one observation supplies no indefinite absence promise.

The work effect can also change which exit is necessary or legal. [C23](combat.md#c23-admit-the-complete-word-after-its-effects-change-hazards-and-legal-continuation)
derives the successful-primitives bound for the restricted M/A/C family and a lethal-basic
counterexample to unchanged-support admission. A valid no-action exit filter cannot preclude a
candidate whose actual kill/stun removes the next attacker or frees its landing. Such effects must
be propagated and freshly verified; an unexecuted or failed primitive supplies no hazard reduction.
Missing move/reset/dash compositions in a controller are supported-family omissions to repair and
qualify, not SDK infeasibility or evidence of the private cause in an earlier game.

### G12. Contact must compose with the actual economic operator
Claim: A contact kernel whose candidate set omits banking cannot perform a bank merely by improving
its survival ranking. The actual economic transition and its dated continuation must enter the same
action/dispatch contract. Actual completion also needs reconciliation before contact can claim the
next input again.

Source-bound counterexample: independent review of Warband `94ac8dc2f3c2` finds `Kind::Submit` in its
executor but no generated Submit plan. Native `base()` leaves `continuation_covered=false` and all
economic fields zero. `Runtime::play` can return from contact before observing bag/wallet completion
and choosing macro bank/work. Thus even a model state with an affordable one-tick bank cannot be
nominated by that contact family. Exact native source hash is
`36bd8ecc5edf9159a5c3dc1590ede0cd7e2dd97f935b7f435c1256be27a2ee0f`; runtime hash is
`4e62e5c9b540a494c9d17f00a00e3a01e2219e714828381edafa0cc253e23759` in that contender snapshot.
Compact native evidence remains `results/contenders/warband/capital-work/contact-continuation-r3.json`
and `contact-continuation-side-b-r3.json`. The source property does not prove that this gap causes all
of either game's production collapse, nor qualify a replacement against the current live reference.

Small complete application class: a stationary tribute with actual queried duration
`max(minimum, base + ceil(multiplier*bag)) == 1`, currently bags1..10. Fresh self must be alive,
unstunned, in an unfinished input, with no active channel or already-started tribute. Use the actual
positive bag, personally observed altar within queried range and fresh opposing-lock state. There is
no SDK `can_submit_gems()`; unused C does not authorize tribute because tribute is not an ability cast.
The host validates `submit_gems()`. An accepted call needs fresh active-channel confirmation; a locked
attempt can become an attack. Starting is `channel_started`, not `banked_gems`.
The next owner start must occur before the match horizon; a last-round start is not a completed bank.

One tick resolves at the next owner start after the start input's owner-end countdown. Exactly one
intervening round-start camp phase occurs, plus the applicable existing effects and owner-start
ordering. Price those actual hazards rather than calling two camp phases intrinsically necessary.
A complete interval must retain positive HP and exclude effective stun, death and a new opposing
lock before completion. Zero possible inputs for every relevant interruptor is one sufficient
subclass, not a universal requirement: nonzero foe inputs can still permit a bank when their actual
ordered damage is affordable and effective stun/lock is excluded. Damage alone does not stop tribute.
The documented current action model has seven ability payloads and shield/stun/enhanced buffs, plus
wards, locks and teleport channels; it declares no persistent damage-zone or damage-over-time action.
Generic "spell entities" phase wording alone does not establish another harmful operator. A supported
closed-action-model application may state that premise explicitly, while native controls refine it;
absence of a separate live-spell query is not a reason to make every service synthetic forever.
This is not an independent proof of all internal engine effects. Linked Cassi/Issac remote death,
pending teleport arrivals and still-possible shopping/equipment changes need their own relevant
handling or an explicit unsupported boundary.

There is a constructive scheduling subclass that avoids needing hidden enemy flags or kit models.
Let the enemy team start the current round, let this body be the actual last own rank, and require
own body count at least enemy body count, counting dead bodies and Cassi/Issac separately. Within-round
interleaving then places every enemy input, and every other allied input, before this one. Allies'
next priorities are now committed. If the body actually sets its next priority strictly above every
ally's committed next priority and reads back the accepted value, it is first in its team next round.
Team alternation makes its team start that next round. Thus `SetPriority -> accepted one-tick Submit
-> next owner-start payout` has no intervening champion input, including hidden or unsupported
foes. One camp phase and applicable existing/owner-start effects remain; this does not make the
channel universally safe. This is an ordered-word deduction, not a native validation.

The current-last rank and priority maximum need actual observations or a complete own-controller
schedule contract; draft ordinal alone is insufficient after priority mutation. The public SDK states
no priority range beyond the int32 interface: avoid `max+1` overflow, require host acceptance and fresh
readback, and decline the strict-first class if no valid higher value is established. A controlled
all-own-default-priority branch can provide a bounded first prototype, but that code-wide premise
must include every other possible priority writer. Pending allies, fewer own bodies, starting the
current round oneself, an unresolved tie at the maximum, or a longer-than-one-tick bag defeat this
specific zero-champion-input argument. Current round/parity conventions require native binding before
deployment. If priority changes but tribute fails, the future schedule still changed and must be
reconciled; failed channel admission does not roll back an accepted priority action.
Priority action, readback and accepted tribute must stay in the same fresh owner input. A suspension
or epoch change invalidates the last-input/parity premise; preserve the actual priority effect, but
rebuild the schedule/channel admission before issuing a later action.

Relevant information coverage is local to this endpoint and horizon. For each foe, intersect an
observation-consistent position support with the backward reach of damaging, stunning or altar-lock
words over its current/next scheduled-input interval (C2). An empty intersection establishes
irrelevance even if that foe is hidden; current negative visibility can legitimately shrink support.
A stale last-seen point is not its full current support. A dead body needs its possible respawn input
and spawn reach considered. Pending teleports, unknown consumables, Tavi's field shopping and other
unrepresented action classes must widen the domain or remain explicit limits. Unknown enemy gold
means a visible empty lock inventory alone does not exclude a purchase and lock. Blanket rejection
for every hidden body and pretending hidden absence are both unjustified. When strict coverage is
unavailable, retain a named observation/escape alternative or an explicit declared-risk class; do not
turn a limited proof domain into universal productive inactivity.

Assurance is not a utility rank. "Certified within these premises", "model reachable" and
"unsupported effect" describe the strength and limits of a claim; they do not establish which offer
best advances the game. A complete modeled bank can be preferable to a certified retreat without
being relabeled safe. In the explicit model score980/bag20, two-tick tribute, HP300, no interruptor or
earlier enemy win, and total ordered damage at most160, the bank wins at its completion with positive
HP. A retreat with more remaining HP but no bank by that time does not win. This finite comparison
refutes HP-only ranking in that model, not uncertainty about the actual opponent.

Compare complete work/bank/observe/recover offers at one declared horizon, with the same explicit
redeployment/service baseline after an early endpoint. Distinguish actual outcomes from modeled
bank/win curves; unfinished bodies, carried bags, equipment, positions and residual work determine
what continuation is possible. Partial camp damage is shared world capital that an enemy can finish
or capture, not owned score. Do not double-count the same reward among actors. A worst-case damage
envelope can certify a bound, but it is not a free enemy policy simultaneously maximizing damage,
stun, locks, farming and banking. Those effects need jointly feasible actor/resource sequences and
the economic service displaced by applying pressure. This supplies no fixed HP/gold-to-gem weights,
probabilities or universal optimality claim.

Completed payout and passive renewal are separate from recovery under opposition. Exact Warband
`58a9258f6d70`, RAW `d934643f28776b7d331bf37de7df54a000aa1f229f0405dc36d38e8a485e956d`, shows
repeated all-body work, purchases and banks on passive Packs A/B (1007/1004 terminal bank, zero own
technical counters). The ordinary B witness joins payout 9 at R50 -> Basic/Reset/Basic R66 -> recall
R191 -> actual purchase/combine R196 -> renewed Basic/Reset/Basic R202. It does not attest HP-healing
recovery or private lease continuity. Evidence: `service-program-passive-packs-r1.json` and
`service-program-native-witness.json` under `results/contenders/warband/word-service/`.

Fresh matched Packs A/B pressure against opaque v53 loses both sides, service 148:1000 and 52:1004 versus economic
controls 224:1004 and 64:1024, all technically clear. The pressure native history first pays Willow 9
at R48, then observes R50 ingress, Ryth/Marcella control, unusable R51/R52 and Heart's R53 kill (M35).
Earlier defeat is not faster useful production. Full scope is in
`results/contenders/warband/word-service/service-program-v53-packs-r1.json`, SHA256
`50a28738a9cabfce1ef63221d16c20af5c6fd2bc3011a8298c35dfad14fe21d9`. No hidden support, safe unchosen
alternative, private decision cause or broader strength claim follows.

A coherent service horizon must name whether it ends at the next scheduled input, next usable
action, or actual restoration/service readiness. Stun-denied scheduled inputs supply no action
capacity and may add camp batches before the usable endpoint. Spawn-restoration credit requires
actual owner-END healing (M18), not an arrival/owner-start label; purchases or heal spells need their
own actual HP/equipment readback. Preserve the accepted prefix, current custody and complete priced
continuation through these boundaries. A bank receipt or a healthy passive cycle discharges none of
the unobserved adversarial recovery premises.

The current Warband `capital_conversion_value.h` makes one finite application of that distinction:
policies share one input/observation/effect domain and horizon, with outcomes paired by the same
producer-supplied legal/coherent reply identity. The comparator checks the declared domain and
pairing; the producer still owes supported legal reply forecasts and complete continuation. Score events
describe scored payouts under their actual or explicit forecast evidence class; custody transfers,
channel starts and
carried bags remain separate capital facts. Own and opposing displaced service must be known and
included once in those same timelines. A maximum-damage `EnvelopeOnly` result is excluded as a
utility forecast. Missing replies, unresolved residual capital or incompatible forecasts produce
incomparability and retain the complete economic Bank offer, rather than silently replacing it with
a survival action or inventing zero opposing cost. Keeping that offer still needs fresh actual bank
admission. Assurance metadata supplies no utility preference, and borrowed comparison views cannot
be retained past their synchronous comparison. Header SHA256 `660ae27e...` and fixture `2929d2ba...`
are pinned in Warband's existing NOTES; the reported 94 strict fixture checks establish component
behavior only, not native performance, fuel safety, a tiebreak model or competitive strength.

Current application limit: Mosaic's constant-initialization repair `ed5220a7cd22` improves passive
YBR A/B banking to 295/83 versus fresh R10's 218/30, with zero own technical counters (S26). Fresh
same-run opaque v52 pressure still loses 30:1008 at R760 and 0:1014 at R829; the matched R10 controls
lose 0:1000 at R968 and 15:1015 at R899. Exact bindings and rows are in
`results/contenders/mosaic/return-policy-r12/cold-pressure-assessment.json`. More clear credits do
not establish owned conversion, useful opponent displacement or a stronger controller. The frozen
CAMP-only return model records but does not use `has_visible_enemy`, assumes no exogenous group
wake for its dormancy estimate, and claims the input before legacy contact handling. These are
known response-coverage limits, not established causes of those native losses. Its unmatched
diagnostic's private state cannot supply that causal attribution.

Concrete extension families are shield -> multi-tick tribute with actual intermediate Continue/Cancel;
capture payout -> short bank; basic/reset -> priced exit; relevant ward/observation -> refreshed
service; and interrupt/contest -> withdraw. Each requires actual compatibility, guarded continuation
and failure/observation closure. M30's shield clock is relative to its owner's start, not a count of
enemy inputs: a zero-timer shield can absorb the last camp phase before expiring before payout;
the post-bank continuation must use its actual remaining value. A multi-tick channel input must expose Continue/Cancel offers before
an unconditional channel return; a cancellation must actually occur before otherwise forbidden
movement or combat. Unsupported effects narrow that family rather than making all economic offers
permanently subordinate to a generic HP-only fallback. These are implementation applications to
qualify, not new native outcomes or an isolated parameter-policy direction.

Evidence: M1-M3, M19-M20, M45, pinned SDK and GameRules tribute/channel contracts; independent bounded
source review above. The general contract is not a safety guarantee. One later native implementation
refinement is now retained at `results/contenders/warband/ordered-service/first-bank-witness.json`,
SHA256 `56b54bd8609ff6a1fccf1bc5aced317e5a4019404b94813f0128d6e07717a4f7`. Exact RAW
`contenders/warband@6cae4a015f07`, module
`bdb3a3a085f0bd660f67173dff3b19864756f547aa2d2af2ce979d50ba99dced`, Packs A/opaque v51: Vendetta
actor2 at HP740 executes Move -> SetPriority -> one-tick Submit at R130, reaches timer0 at owner end,
then at R131 owner start banks8 (bag8->0, team score8->16) at the same HP740 with M/A/C ready. No
other champion owner start intervenes. Actual native owner orders are R130 `[3,0,4,1,5,2]`, R131
`[2,3,0,4,1,5]`, and restored R132 `[3,0,4,1,5,2]`. Numeric priority0->1->0 comes from actual SDK
readback logs; the selected native events independently establish the resulting order, not those
numeric request payloads.

The bounded query window has complete four-round coverage and zero query errors. This actor has
four actual class banks totaling16g, three later collection receipts and two later own final-clear
credits. Thus accepted channel, actual order-controlled payout and renewed economic service are
observed for this named application. The same-run two-side RAW Packs comparison in
`results/contenders/warband/ordered-service/v51-packs-r1.json` is technically clean but banks80/8
against the economic control's180/44; both programs lose both sides to v51. The local operator result
is not a stronger-controller or cooperation result. Priority changes can alter other bodies' timing
and camp aggro ties, so the complete team comparison still matters.

General refinement still requires the ordinary executable to nominate and accept the start,
observe the actual completion, then resume actual useful work. Reconcile pending channel/bag/score
state before channel/contact early returns. Team score delta alone cannot attribute one actor's bank
when allies can bank between inputs. A passing reply model or accepted channel is not that sequence.
Native controls should distinguish opposite order, a two-tick bag, an opposing lock and lethal next
camp damage; partial flags or diagnostics must not replace the actual bank and subsequent service.

### G13. Future availability is a nomination until its physical enablement is resolved
Claim: A predicted arrival after a respawn deadline does not establish executable living work (M52).
The offer must distinguish currently observed living work, future-availability nomination and any
actual release/observation/wait operator needed to enable it. A source can remain `Respawning{0}`
while occupied; no timer threshold or unconditional camp ban repairs that distinction.

Source/model counterexample: high-rate source A is known `Respawning{0}` and remains occupied by an
ally; living source B has feasible work. Both sources can be harmless and visible, with this body
quiet and unchanged. An arrival lower-bound filter admits A's future nomination,
ranking selects A and publication presents Work(A). Fresh observation still finds absence and drops
the entire query, including B, without retaining A's unchanged rejection. A new generation repeats
the same ranking. Under those unchanged premises this can cycle forever even though every individual
Price cursor is finite and monotone. This is a cross-generation counterexample, not a proved private
cause in an ordinary replay. A single R160 `NeedCompute` observation establishes no stall: an active
unchanged query with sufficient computation may still be making finite progress.

An actionable offer carries its named group/family, actual observation provenance, unresolved
enablement, executable first action and continuation. A future A may legitimately offer moving off
its occupied source when the actor controls that occupancy, obtaining a fresh observation, or a
finite priced wait with a fallback; another ally's release needs its own actual commitment. It may
not silently turn that unresolved premise into living damage work. Already prepared alternatives
remain available as nominations after one offer fails, subject to their own fresh admission. A
rejection/refutation survives query restarts while its relevant physical/model premises are unchanged;
incrementing an epoch or query id alone is not such a change. Reobserve at useful deadlines, and
re-enable absent Work on actual Alive or Capture on actual Reward. Other changed body, geometry or
operator premises may discharge their corresponding family refutations, but do not substitute for
the lifecycle required by the chosen operator. Actual release still needs a post-phase observation.
Do not refute observation/release operators or permanently blacklist A along with its absent Work.

Lifecycle authority and executable access are separate premises. Actual Reward can authorize capture
only after its current movement/channel guards pass; it does not erase an unchanged unreachable
ingress refutation. Likewise, a remote remembered Reward can nominate a paid observation approach
without authorizing capture itself. If inaccessible Reward(A) wins every new menu and its failed
route discards accessible B, the same cross-generation liveness failure remains even though A's
Reward is fresh. Preserve alternatives and scope that refutation to the actual geometry/operator;
changed access can reopen it. This is a source/model obligation, not an attributed native failure.

Separate three progress obligations: finite within-query cursors; publication/selection that preserves
alternatives or meaningful refutations; and actual execution that advances, changes a premise, or
hands off to a distinct continuation. Retaining a cursor solves only the first. Under unchanged
finite offer premises, each failed publication should consume a distinct remaining offer/refutation
or enter a named finite resolver/wait; it cannot merely restart the same selection rank. Diagnostics
must separately identify `query.group` (cursor), `query.best.group` (nominee), and `state.group`
(published). Bind the nominee's actual queried lifecycle to `query.best.group` and its generation;
`state.group=-1` before publication identifies no candidate. Record source/model
findings and native observations separately. Native refinement should observe A still absent while B
is actually serviced, then actual release/Alive re-enabling A, rather than infer either from timers.

## One structure, three current derivations

These are the active research decompositions, not claims that the controllers solve them. Compound's
historical direction is retired; current implementation and exact evidence remain in the bot notes.

| Structural obligation | timetable | mosaic | warband |
| --- | --- | --- | --- |
| Useful work over time | Dated services with residual work, acquisition, payout, conversion and recovery | Ordered local words tied to live geometry and service endpoints | Owned capital and named actors' joint work compared with displaced independent work |
| Observation consistency | Attach dates and provenance to each service premise; reacquire when stale | Reveal actual movement/work destinations and refresh changed word premises | Separate personal foe observations, recent hidden reach and each member's timely support |
| Movement, casts and attacks | Make the service's native action sequence executable at its promised date | Compile legal work and immediate egress using actual remaining allowances | Price ordered equipped contact words and individual action opportunities |
| Continuation and renewal | Deliver and recover, release sources, then acquire new useful work | Retain actual egress/return suffixes; distinguish cooldown wait from no-op collapse | Finish conversion/recovery, release owned source footprints, and resume useful labor |
| Computation | Reserve enough input for the dated primitive and its fallback | Bound observation/search without consuming the admitted egress | Bound contact enumeration before external damage can invalidate the frame |
| Cooperation evidence | Show dated arrivals and useful capacity beyond the solo alternatives | Show actual supporting effects within the executable local word | Assemble named actors, show actual support/work and price each displaced service |
| Current adversarial gap | Complete reset/dash/actor-order contact continuation | Persistent hidden contact and ordered opponent prefixes | Fuel-qualified contact, then actual named assembly and displaced-work comparison |
