# Strategy: the structure of the game, and the answers built on it

The top layer of the foundation (see [README](README.md)). Entries here state structural facts of
the game, the properties every strategy must answer whatever the opponent does, and connect them
to the layers below. They start as hypotheses derived from the rules and the evidence we hold; the
mechanics, economy, combat and online pages confirm, quantify or refute them. Each contender
derives its own answer from the same structure (table at the end); that is what keeps them
grounded and different at once.

Current interpretation, 24 October 2026: rules, conditional models and observed associations
have different authority. Round order, action allowances, capture and channel contracts constrain
every bot. Historical party-size optima, kill prices and contact statistics retain their stated
models and samples. An announced ally is not delivered damage, pooled health is not a tank
controller, and low sampled PvP hazard is not safety from camps or failed exits. G11 and
[micro U9](micro.md#u9-a-tactical-option-is-an-ordered-state-transition-not-a-ready-slot) state the
current derivation boundary. This synthesis establishes no strength improvement.

### G1. The camps are one shared, regenerating pool taken first come: every capture is also a denial
Claim: both teams harvest the same camps; a group we capture is unavailable to the enemy until it
  respawns (timer from the claim, delayed while any member spawn tile is occupied). So yield at
  contested camps can combine our gain with denied enemy production, weighted by whether the
  enemy would otherwise take them and successfully convert the reward. It is not automatic
  doubling of scored yield. The race is not two independent production lines.
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

### G2. A kill transfers resources and removes productive time; its score value is conditional
Claim: a death costs the victim 60 rounds dead plus the walk back to the field, hands its whole
  bag and 100 gold to a champion killer. The historical estimate of about 40 gems plus twice the
  bag assumes a particular productive rate, denied work and eventual delivery by the killer; it
  is not a fixed score transfer. A fight without a kill spends time and health but can still
  secure a capture, delay a bank or change access. Its value includes that objective, recovery
  and the alternative work.
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

### G3. Enemy contact can be costly to leave; safety also depends on camps and the actual exit
Claim: at equal speed walking never opens distance: a melee chaser in a different turn slot hits a
  fleeing champion every round, one in the same slot every second round (G9). Contact breaks for
  free in the tested open-ground chase through a dash against a chaser without one, or because
  the chaser chooses to stop. A walk-blocking stun was insufficient in the tested chase order;
  this does not cover every terrain, kit or actor order. A melee retreat can retain compatible
  damage, subject to its actual exit allowances. In the sampled champion-caused deaths:
  online, a champion with no enemy within 6 dies at 0.007 per 1,000 rounds, with one enemy within 3
  at 5.7, with two or more at 18.2 (91% of kills have an enemy within 3). Between top teams 75% of
  fight episodes still end in separation, because neither side insists; deaths come from staying:
  victims spent a median 18 rounds in contact that began at full health, and death risk rises with
  the exchange ratio (damage taken / dealt < 0.5: 2%; 0.5-1: 13%; 1-2: 32%; 2-4: 48%).
Evidence: combat C2, C7-C10 (engine-verified chases and escapes on open ground), C21 (hazard over
  9.8 million online champion-rounds); online O13-O14 (70,605 contact episodes, 12,740 deaths);
  main's fight calibration (retreats starting adjacent die 27% within 15 rounds) and collapse
  analysis (47% of main's trap-map deaths are retreats that never end) are the late-exit side of
  the same mechanism. These statistics select champion-caused deaths, not all hazards. Current
  [retired Timetable notes](../../miscellaneous/timetable-gem-sweep/NOTES.md) retain one Corridors
  loss with 17 own camp deaths before round 400, all to two
  monoliths, including blocked/adjacent low-health exits. This establishes a missing hazard in
  that case, not its frequency across maps.
Rests on: M2, M23, M26-M28, C2, C7-C10, C21, O13, O14.
Status: verified (engine 1.0.2, open ground; terrain in chases unmeasured) and measured (online).
  Revised 22 Oct twice: "contact is sticky" (too strong, O13) and the chase mechanism (C2).
Recheck: any change to movement, dash or stun rules, or a speed-changing item.
Consequence: evaluate contact with an actual escape or a conditional favorable fight forecast,
  accounting for legal paths, arrival order, readiness and each source of damage. Summed enemy
  rates and a ready dash are estimates, not proof of an exit; no globally exact fight oracle is
  established here. Read the exchange from the first round and leave while it is even;
  never stand in a losing exchange hoping it turns. Check the legal exit, next round-start camp
  volley and channel interruption separately; enemy distance alone certifies none of these.

### G4. Timely usable force can decide fights; gathering and keeping it has an opportunity cost
Claim: In the tested homogeneous full-contact fights, numbers approximately follow a square law:
  N against M leaves about sqrt(N^2 - M^2) survivors, so one extra
  champion turns a coin flip into a win that keeps most of the team, and an even fight is a coin
  flip (decided by first strike, G9) that wrecks both sides. Focus fire is worth about one champion
  (3 against 3, the focusing side loses nobody). Online, a second enemy within 3 triples the death
  hazard and allies within 6 cut it 2.4-fold; 19-21% of deaths are a lone champion against two or
  more. But captures do not speed up with allies and rewards split, so a team that stays grouped
  farms fewer camps: between top teams the more-grouped side won only 14 of 38 games. The law that
  joins the two: numbers are needed only where contact can happen, inside the enemies' two-turn
  reach plus their arrival time; everywhere else grouping costs throughput, except on big content
  (G8) and where proximity converts into burst kills (the #3 team, grouped Focus burst, has the top
  K/D). These tests do not make the square law a general engagement model: reach, terrain, arrival,
  health, kits, turn order and targets determine which bodies can apply damage. Area damage scales
  with shared legal effect centers; pair distance 2 does not exclude a radius-1 ground area (C14).
Evidence: combat C11-C14 (engine-verified up to 3 against 3 with full kits; model beyond), C16,
  C21; online O14, O15, O18; rules (capture, reward split); economy E8. Victims die with 1.5-1.7
  enemies within 3 in their last rounds (O14), fewer than the "3-3.5 participants per kill" of
  main's topteams.md, which counted assists over a longer window.
Rests on: C11-C14, C16, C21, O14, O15, E8.
Status: verified (engine, up to 3v3) and measured (online association; losing teams may group
  defensively).
Recheck: balance changes to burst or area abilities, capture or reward-sharing rules.
Consequence: never be the lone champion within reach of two enemies; never take an even fight
  (hold outside the enemy's two-turn reach and let it choose); focus one target; avoid shared legal
  area-effect centers (distance at least 3 is sufficient for radius-1 free centers); group only
  where contact can happen, for big content, or for burst. Verify actual pairwise access and action
  order before treating a numerical advantage as a forecasted win.

### G5. Information is local, the team's shared picture is 8 bytes, and the static map is fully known
Claim: each champion sees 8 tiles plus its own ward; allies' views are not shared; 8 shared bytes
  are the only dynamic team channel. The draft (10x fuel, the whole map, 1,024 bytes of draft
  memory) and each champion's first turn (2x fuel) permit bounded static precomputation. Fully
  knowable metadata does not make arbitrary analysis fit in those finite budgets.
  A visible enemy, though, is readable almost completely (cooldowns, shield, bag, whether it has
  acted this round), all but its gold; camp state, respawn timers included, only in vision.
Evidence: mechanics M44-M51 (vision is personal; shared writes are visible to later actors in the
  same round; draft memory frozen in play); fuel budgets 1,000,000 / 200,000 / 100,000 (S20).
Rests on: M44-M51, S20-S22.
Status: verified (engine 1.0.2).
Recheck: changes to vision, memory or fuel.
Consequence: Cache reusable static work when its measured initialization and worst-map costs fit.
  Bound initialization too, with fuel reserves; use lazy or incremental caches when necessary and
  never assume a successful build proves health. The 8 bytes go to facts that change decisions
  most (enemy sightings, claims, calls to converge or retreat). The current contenders' exhaustion
  call sites need their owners' diagnosis; these budget facts alone do not establish a cause.

### G6. Supply and remaining time determine currency value; historical races often ended near round 1,100
Claim: gold compounds (gold -> items -> faster clears and fewer deaths -> more gold), so on most
  maps the gold leader wins: at r400 the earned-gold leader wins 78% of games, and when gold and
  kills disagree the gold leader wins 74%. But compounding needs time: on gem-rich maps the race
  ends earlier (median r942) and the banked-gem leader beats the gold leader in two thirds of
  conflicts; where one group holds most of a map's gems (a jackpot, such as a 200-gem monolith),
  the side that captures it more often won 31 of 36 sampled games. Top games are won at a median
  r1085, so r400 is a third of a game and a plan must cover the whole race.
  Gold does not add to tributed gems; it buys items (E17) whose value is health and speed, and
  lifetime earned gold directly decides terminal ties when tributed gems are equal. Items cut
  the modeled speedrun 41% on damage maps but only 6-16% on capture and respawn maps (E13).
  Historical E27 found gems usually decided round-2000 games; that observation does not remove
  the earned-gold tiebreak.
Evidence: online O7-O10 (225 games between other teams, 69 top-vs-top; 1,124 games reaching
  r400 for the currency conflicts); economy E13, E17-E20, E24, E27; maps P4 (binding class).
Rests on: E13, E17-E20, E24, E27, P4, O7-O10.
Status: measured (online, engine 1.0.2, 21-22 Oct) and model (economy).
Recheck: map pool changes, camp yields, item growth, winning_gems, max_rounds.
Consequence: no fixed gem/gold exchange rate (any opponent can exploit one); price each currency
  by the map's supply and the expected length of the race; treat a jackpot as income plus denial.

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

### G8. Much gem supply costs health; a useful party must realize the modeled target capacity
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
Consequence: evaluate target-specific parties including rendezvous, individual survival, legal
  contribution, capture duty, recovery and foregone independent work. The historical modeled
  schedule is over 300 rounds faster on some maps; it does not establish that an autonomous
  controller can realize the gap or that target sizing caused main's observed gap. C22 excludes
  support arriving after the useful damage window. Current E31 validates one equal-distance
  two-melee rotation and complete lab cycle, with configured next-round priority and each exposed
  body surviving the determined volley. Neither entry licenses absent allies' health or DPS.

### G9. Tempo belongs to pairs: turn slots decide first strikes, catches and escapes
Claim: champions act one at a time, alternating teams, and the starting team swaps each round.
  So a champion in slot i and an enemy in slot j either strictly alternate (i != j: the lower slot
  acts first in every round) or trade double turns (i = j: each gets two turns in a row every
  second round, and the round's parity decides who acts first). A melee chaser in a different slot
  hits a fleeing champion every round, in the same slot every second round. Even fights are decided
  by first strike: mirror duels between same-slot first picks flip with the parity of the contact
  round. Priority, set a round ahead, reorders a team's slots and so chooses which pairs strictly
  alternate; stun chains need casts that straddle the target's turn.
Evidence: combat C2 (engine-verified slot mirrors and chases), C16; mechanics M2, M26.
Rests on: M2, M26, C2.
Status: verified (engine 1.0.2).
Recheck: any change to turn scheduling or priority.
Consequence: a fight model must resolve turns per pair, not as simultaneous volleys: main's
  model cannot see first strikes, catches or escapes. Historical C19 finds fights resolving about
  3x slower than predicted and deaths predicted 3.6x too often; it does not isolate turn pairing
  from target access, readiness, ability choice or continuation policy. Current micro U9 imports
  one controlled native lethal-basic case where the earlier kill denies the later actor's action.
  Approach to strike first (end a turn just outside reach when acting again next); use priority
  as a lever for chases, escapes and stun timing.

### G10. Historical candidate safeguards require executable conditions and full-game evidence
Claim: eight candidate rules were proposed from historical combat work (C20): (1) evaluate contact
  with an escape or favorable fight estimate, subject to G3's executable conditions; (2) never
  the lone champion within reach of two enemies; (3) melee retreats hit back, ranged champions
  stand, and kite only a chaser with neither dash nor stun, with a dash; (4) focus one target and
  stagger stuns; (5) avoid shared legal area-effect centers (C14); (6) never nearer than the enemy to
  an awake camp in a fight; (7) contact, not carrying, is the risk: carry freely out of contact,
  bank or break off when an enemy enters its two-turn reach; (8) no even fights: hold outside the
  enemy's two-turn reach and let it choose. Online, 91% of kills happen with an enemy within 3 and
  1% with none within 6 in that champion-death sample. This does not establish that rule 1 bounds
  overall deaths or that an estimated way out is physically realizable.
Evidence: combat C7-C18, C20, C21.
Rests on: G3, G4, G9.
Status: hypothesis (each rule rests on verified laws; their combined effect is untested in games).
Recheck: any change to the entries they rest on.
Consequence: treat these as strategic hypotheses, not shared invariants. Specify executable
  prerequisites, displaced useful work and actual action/escape evidence before adopting one.
  Conservatism can remove the only feasible work supply. Technical legality alone does not
  establish productive safety. Each bot may reject or replace these historical heuristics.

### G11. Useful production is a complete observed conversion chain
Claim: damage, a clear, a claimed job, a carried bag and gold are distinct intermediate states.
  Score requires actual reward ownership and tribute completion; growth requires a legal purchase
  and useful subsequent work. Each stage consumes actor opportunities and can fail independently.
  Camp damage persists, so a safe profitable siege can span recovery trips; requiring every solo
  admission to finish the whole group can also destroy access to useful work.
Evidence: current rules on capture, channels and movement; M52 on prediction versus observation;
  C22 on timely legal arrival; U1-U8 on actual action packing; current E31's four lab OFF/ON games
  on both sides witness clear/capture/bank with individually covered tanking. Current contender
  full-game nulls and failures refute equating a better local metric with a better race. The
  [synthesis record](../../../results/contenders/foundation/derivation-3.0.5.json) pins reused evidence
  and separates source interpretation from imported native cases.
Rests on: M1-M3/M13-M24/M48-M52, E29-E31, C22, U1-U9.
Status: rule/source synthesis and conditional controller contract, current bundle 3.0.5/1.0.3.
  This is a design obligation, not a new simulated optimum or strength result.
Recheck: mechanics changes, observed lifecycle failures, or a new useful form of work/conversion.
Consequence: a bot's work episode should answer these questions in its own architecture:

| Boundary | Required distinction |
| --- | --- |
| Select/reach | Observed available work versus predicted availability; physical arrival versus a published intention |
| Assemble | Named live contributors with timely legal actions versus radius, roster or promised DPS |
| Execute | Individual survival through each unavoidable camp/enemy action versus summed health |
| Continue/exit | Fresh covered next action and legal recovery path versus a latched target or blanket recall floor |
| Hold/reward | Actual uncontested member occupancy and observed payout versus disappearance of a claim |
| Convert | Legal altar access/channel completion or actual purchase versus bag/gold accumulation |
| Release/renew | Helpers leave capture/respawn tiles and restore temporary priority; next useful work is feasible |

  Deliberate occupancy can instead serve an explicit denial duty (G1), priced against the lost
  production of both teams and the occupier's alternative work. Release means ending the old
  commitment correctly, not forbidding a new justified hold.

  The episode must define which safety, banking and recovery phases may preempt it, and whether
  that interruption releases, pauses or invalidates its commitment. A generic phase running first
  can otherwise abandon a modeled continuation; suppressing global safety creates another defect.
  A constrained controller needs fresh prerequisites for every permitted continuation, with an
  explicit abort and restoration path. Elapsed time or retained task identity is insufficient.

  A null action also needs a reason: waiting for an allowance, waiting for an observed event,
  needing observation, deferring computation, finding no candidate in the represented menu,
  and proving a precisely scoped infeasibility are different outcomes. Spent attack/cast
  allowances do not alone justify recovery; an unfinished search does not prove no job exists.
  A pending result should name the fact, event or advancing search frontier that can resolve it.
  Approach needs actual route progress, work needs confirmed damage or a bounded enabling
  prefix, capture needs countdown/payout, and recovery needs egress/channel/heal progress.
  Rank decrease is meaningful only within that phase's unchanged problem and assumptions;
  this supplies no globally decreasing rank or progress guarantee under arbitrary opponents.

  Coordination and champion micro meet at these boundaries. A helper's first legal hit after the
  clear is not support; a reset spending the only cast removes a shield/stun option; an exclusive
  cast can trade an exit for equal damage. Preserve alternatives and observed effects before
  reducing them to a single DPS, health sum or gem/gold exchange rate.

  The conversion order is a partial order, not a compulsory bank-then-shop pipeline. Capture
  credits gold and gems; recall retains the bag, and legal spawn shopping may spend the gold
  before tribute converts the gems. Spawn healing occurs at owner turn end. A direct loaded
  altar route and recall/shop/heal followed by a loaded route are alternative suffixes with
  different exposure, elapsed time and return state. Each needs its own observed completion
  and safety premises; a bag increase or fixed threshold does not mechanically require either.

  Design inference: productive capacity belongs to a named body, task, time and observed state,
  with a feasible completion and renewal path. Safety, damage rate, gold spent or earlier local
  capture alone cannot represent that capacity. This does not identify an optimal assignment or
  a universal solo/party preference. Independent derivations can ask different questions of the
  same contract: Timetable about episode timing and repricing; Mosaic about kit and camp topology;
  Warband about actual cooperative labor and the complete work displaced by assembling it.

  The Timetable examples below belong to the retired
  [residual-service implementation](../../miscellaneous/timetable-residual-service/NOTES.md).
  Its inherited main-derived chassis makes this non-independent controller evidence. Exact
  native game events retain their stated scope; no architecture or performance qualification
  transfers to the new official-starter Timetable scaffold.

  A concrete productive-safety counterexample is the retired Timetable admission pilot: 48 full
  games, six training maps, both sides, v42 and starter. The conservative solo-clear gate reduces
  same-horizon own camp deaths from 81 to 5 across 24 paired keys, while wins fall from 11 to 5
  and four subject games have no-clear stalls. Both arms lose all 12 v42 keys. The
  [saved summary](../../../results/contenders/timetable/lifecycle-audit/admission-summary.json)
  retains native-statistics scope, exact sources and opponent health exceptions. This rejects
  the executed gate-plus-loop, not all conservative admission or safe partial-siege architectures.

  A current ordinary-game witness now reaches the full recovery/conversion chain. Corrected
  trace-enabled Timetable `da6b9f6bf323`, `00elkring` B against v42, has Marcella actor6 damage
  camp5 at round60, exit at70, recall at74, arrive/heal at79, make its own return move at80,
  attack the same camp at91 and clear native group5 (`g_506`) at102. Actual capture at108 gives
  that actor22 gems and110 gold, followed by its own move off the member tile. Tribute starts
  at129 with timer3 and completes at132: bag22 to0, teamB score22 to44. The
  [assessment](../../../results/contenders/timetable/lifecycle-audit/trace-witness/assessment.json)
  binds native commands, actual effects and exact actor/group native-statistics joins in complete
  windows60-110 and129-133, with both contained observer Jobs closed empty.

  This establishes one autonomous partial-work/recovery/capture/bank execution, not exclusive
  solo production: allies may damage the camp during its absence. Intervening travel111-128
  is not fully observed, and an immediate recall after banking proves no next productive cycle.
  This source belongs to eight trace-enabled health games on two maps, with no broad strength
  qualification. The earlier episode source `0b51d8d20f01` lost all12 training v42 keys against
  its same-run baseline's6/12; the corrected witness does not relabel that rejection or qualify
  subsequent source corrections. A realized lifecycle is necessary evidence for this architecture,
  while profitability, repeatability and strength remain separate questions.

  The failed episode cohort also contains a death-free early throughput counterexample:
  `01big-world` A/v42 through round400, BASE `38ce7b262e04` versus episodes `0b51d8d20f01`.
  Both subject rosters have zero deaths over this horizon, yet own collected/banked gems
  are 240/133 versus 33/33, with 12 versus 3 positive-reward camp jobs. The
  [same-horizon summary](../../../results/contenders/timetable/lifecycle-audit/episodes-summary.json)
  binds the statistics; the [opportunity audit](../../../results/contenders/timetable/lifecycle-audit/opportunity/assessment.json)
  gives all five bodies 400 ready owner starts in each arm. Episode Varg changes position 374 times
  versus BASE 250, while the episode arm obtains no Varg reward in that window. Its separately
  observed round 198 camp work, round 210 recall and round 215 recovery prove actual work and survival, rather than
  useful completed production. This rules out early deaths as the explanation in this selected key;
  movement counts do not identify its private chosen target, initial quote or one causal defect.
  Death avoidance and a realizable recovery chain cannot by themselves establish adequate supply.

  Warband supplies a different counterexample: earlier local completion and a concentrated reward
  can execute without adding measured team supply. Its Elkring/starter party receives 22 gems
  three rounds earlier on A and seven on B, and the exact recipients later submit those bags.
  Yet both matched arms collect the same team gems through round 200, and the party finishes
  those starter wins 41 and 23 rounds later. [E31](economy.md#e31-next-round-priority-rotates-a-single-elks-tank-without-losing-either-melee-basic)
  binds the native contribution, statistics bank suffix, final untraced gameplay transfer and
  the eight-key null cohort. This is not proof that cooperation lacks value, or that one helper
  caused the slower finish. The next discriminating question is what otherwise unavailable
  complete work the party unlocks and what feasible independent work its assembly displaces.

  Mosaic's later healthy Packs A/v44 loss, source `5b55bda2f7bd`, isolates another boundary:
  [the saved R6 audit](../../../results/contenders/mosaic/ordinary/kernel-r6-analysis/assessment.json)
  attributes zero eventual own banked revenue to all five ON final-clear episodes. Three rewards
  go to enemies; two own 28-gem bags are lost on death. This records final-clear/payout/bank
  outcomes, not exclusive labor or one causal defect. Varg holds a contested cleared-member tile
  through 26 ready inputs without an own action; two other inputs are actually stunned. Holding
  is therefore a continuing exposed job, not successful arrival. Ryth later works again after
  banking, yet loses its next bag during observed pursuit: a certified route step does not prove
  complete conversion. The successful round-22 reset without a follow-up basic is not itself a
  legality bug: remaining 22,224 fuel is below that source's 25,000 continuation reserve. Private
  branch choice and frame completeness remain unobserved. Both matched games are technically
  clean, while their different rosters and these selected events establish no strength gain.

  The later [Warband R12 keeper](../../../results/contenders/warband/production-health/saved-r11-r12-pool.json)
  preserves a separate full-pool failure: three arms, 40 maps/both sides/v44, each zero wins in
  80 games. Production source `a675e39e5e0c` has 2,998 exhaustions (2,985 on Ambush), one E5
  move-preparation error without an actor/time receipt, and 23 wholly zero-work keys. The
  baseline itself has health failures. These are failed execution/production obligations, not
  evidence about optimal party value, and healthy selected successor cells cannot qualify this
  campaign or the whole pool. Fresh current-awake versus possible-future-wake state (M9) and
  bounded route/planning execution must be established independently.

  A distinct Warband successor, `afe6ba3f3b78`, now has a
  [saved native-statistics ledger](../../../results/contenders/warband/production-health/awake-cycle-audit.json)
  for two healthy Ambush/v44 games. A Marcella clears at 534, collects 33 at 540 and banks at 575;
  buys five Jungle Strength shards with a recorded combination at 580; then damages camps again
  at 635 and clears/collects/banks at 805/811/846. B Marcella's corresponding sequence is
  524/530/565, four shards at 570, then damage at 625 and clear/collection/bank at 729/735/770.
  B's four purchases are not evidence of a completed emblem. These are two observed converted
  cycles with later spending/work, not an isolated purchase effect or useful-party proof.
  Both subject teams still have zero clears and bank through round 300. Cumulative damage samples do
  not identify each hit's target, continuous siege, recall, full HP or the source of healing.
  The ledger's native receipt/wallet order and gem conservation do not reveal unique gem tokens
  or displaced independent alternatives. This narrow healthy source/coverage must remain separate
  from R12 and from its own wider health qualification.
  The [same-source known-key panel](../../../results/contenders/warband/production-health/awake-known-health.json)
  confirms that limit: five other maps/both sides, zero wins in ten games, 288 native exhaustions
  and three literal E98 error lines. Yellow Brick Road supplies 271 of those exhaustions. The
  E98 lines name stages but lack actor/round/input; native fuel stamps do not identify a guest
  call site. The two clean
  Ambush games therefore establish local converted production, not controller-wide health.

  The later [frozen Warband frame-health keeper](../../../results/contenders/warband/production-health/objects/960f70d6714516d8ec5302168c1f717af8fdb2c53bbae3bf82a98adbd8be57f6.json)
  retains twelve full Yellow Brick Road games and five separate truncated probes. Every represented
  full subject arm fails health and wins none; repeated controls and different horizons are not
  independent cost samples. Fused first-100 native exhaustion counts fall from 34 to 24, while
  full-game counts rise from 208 to 241 on changed trajectories: neither proves an isolated cost
  repair. Packed B has zero own clears but 515 camp damage and 600 earned gold; sparse A/B have
  zero income/conversion but 600/960 camp damage. Zero clears, zero income and zero executed work
  are different claims. Probe labels print after the turn function returns, and added reads/logging
  perturb fuel; the last mutable stage is not an exhausting program counter. Source-specific field
  provenance and logical error-input joins do not reveal the exact exhausted instruction. New
  adapter fixtures or later working revisions inherit no native health or strength qualification.

  The next [immutable keeper](../../../results/contenders/warband/production-health/objects/061e3cb2160b1b4d96c9b32df8e13ef1d3e5a5ff036e65d5b1494ae803bf1f73.json)
  retains fourteen full games and seven separate probes. Its added proposal generation has no
  native exhaustion in the first 100 rounds on either side, but one/eleven across its full games;
  both lose with no clear, collection, submission or income. B still deals late camp damage
  660 and champion damage 121.5, so lack of converted production is not zero outgoing action.
  The clean short prefixes do not establish full-horizon fuel safety or service completion.
  Later mutable keeper additions and working adapters are outside this frozen cutoff.

  Receipt-field correction: the [fixed v8 keeper](../../../results/contenders/warband/production-health/objects/13372725610a85ab1f93dfd189510766143e267d50425e6e2efb05394600d2ca.json),
  SHA `13372725610a85ab1f93dfd189510766143e267d50425e6e2efb05394600d2ca`, deprecates legacy
  `zeroGemCampCollectionReceipts` and `zeroGemCampRecipientReceipts` in preserved v3-v7 objects.
  They classified generic `entry.amount == 0`; actual gem quantity is `entry.value`. Use v8's
  value-based receipt fields instead. Packed A has one true zero receipt, not the legacy three;
  its other two are positive 4/8. Packed B's two actual zero receipts, collected/banked totals
  and native health conclusions above remain unchanged. Old immutable artifacts stay intact.

  V8 also binds frozen `91b07905627d`: full Yellow Brick Road A/B versus v44 lose with one/eight
  native exhaustions. The separate full 2,000-round passive game has no faults/errors/exhaustion,
  but only one early group cycle, eight total banked gems and no represented productive metric
  change or production event after round 85. This proves neither no movement nor fully ready
  renewal, sustained supply, broad health or strength. The passive cell cannot qualify the two
  failed live-comparator cells or later working code.

  The fresh independent Timetable rebuild has its own narrower witness, separate from that
  retired lineage: generation `b2e6d9f0e45e`, ON module `1c78d5fd2ba1`, one full BigWorld A/starter
  game. Its [bound assessment](../../../results/contenders/timetable/independent/b2e6d9f0e45e/ordinary-startup/assessment.json)
  reports 360 Vesper camp damage and one clear, with zero incoming damage, deaths, faults,
  exhaustion and error lines, but zero collection and bank. This crosses actual service entry,
  not the complete production chain. Separate instrumented generation `67d4c0348c67`, ON
  `c97c674009be`, has [typed stage receipts](../../../results/contenders/timetable/independent/67d4c0348c67/entry-probe/assessment.json)
  for 44 approach-cover `NeedCompute` withdrawals through round 64, repeatedly leaving 26,403
  fuel at selected group 6 before the unchanged 56,000 entry guard. Its printed global packets
  remain Ready. These are observations of the instrumented module, not private state from an
  earlier frozen game; no reserve reduction, full renewal, strength or later-rebuild qualification
  follows. Resolving group presence/lifecycle and exact member work are different information jobs.

  The later frozen generation `b9ada67eaa0f`, ON `90bfbd571fb6`, still records one clear/360 damage
  and zero receipts on its ordinary BigWorld A/starter key. Its distinct instrumented module
  `dddcdf0394f8` reports 44 approach-cover withdrawals, including 31 with 52,206 fuel left
  before the unchanged 56,000 guard; this does not alter the earlier 67d4 receipts.
  More decisively, the [full Elkring A/passive prerequisite](../../../results/contenders/timetable/independent/b9ada67eaa0f/passive-prerequisite/assessment.json)
  lasts 2,000 rounds with every body doing zero camp damage and zero collection/bank: a
  production stall despite no faults, errors or exhaustion. Technical health and one selected
  clear therefore leave the independent controller unqualified; its next working adapter is
  a different, unverified source.

  Later frozen Timetable generation `df08b9ee5d75`, ON `68e3213a893f`, likewise has no clears,
  damage or receipts and stalls despite zero faults/errors/exhaustion. Its separate
  [instrumented source](../../../results/contenders/timetable/independent/6940bce7eb87/entry-probe/assessment.json),
  `6940bce7eb87`/ON `a55072f282e8`, records 40 `cover-sources` compute withdrawals before any
  field/path work, with branch indexes 0 once, 64 twelve times and 80 twenty-seven times.
  The label precedes its source loop: it cannot distinguish prior mask-collection cost from
  processed-source work or identify a charged instruction. The witnessed-domain successor is
  still under construction; fixtures and a build cannot turn it into native production evidence.

  Frozen `991300eec8d0`, ON `206820e6a0ed`, supplies a different
  [actual physical chain](../../../results/contenders/timetable/service-audit/fresh-991300eec8d0/assessment.json)
  on ordinary BigWorld A/starter. In rounds 100-139 Ryth stays at `(67,45)`, explicitly ending
  turns with all allowances and cooldowns ready. Nearby Vesper starts damaging wolf 11 at round
  104; wolves 9/11 hit Ryth at round 105, and wolf 9 continues after 11 dies at 128, killing Ryth
  at 140. Ryth takes 1,200 camp damage and deals none. Vesper removes 1,470 actual HP, killing
  wolf 11 and gold member 13, but leaves wolf 9 at 1,350 and gem member 4 at 120: no group clear,
  reward or conversion follows. These are native chronology and residual-state observations,
  not a private no-op/certificate/budget cause or exclusive-labor proof. Whole-group wake uses
  the existing rule; the audit adds no counterfactual experiment. Ryth's later group view is
  Unknown, and omniscient residual HP must not be promoted to personal information. A yielded
  ready input remains physical waiting under [T5](mathematics.md#t5-computation-must-refine-the-physical-continuation).

#### Settled service applications and their different terminal predicates

The [complete harmful-service policy](../../../results/contenders/coverage-atlas/harmful/index.json),
index SHA `01d94e2724693aaf739eaf4285e9f676fc9d59361a3e20e786006bece579b420`,
is conditional **model** evidence, with `nativeVerified=false`. On one exact YBR group/base
Vendetta state, it removes the harmful member, finishes satellites, captures four gems/80 gold,
performs a loaded quiet recall, observes its actual landing and owner-end full heal, follows
that landing's finitely priced bank leg and returns full-ready to spawn. All sixteen modeled
landings finish in 75-78 nominal physical inputs, at most 80 with one refused move and one
compute wait globally. HP reserves include both delays; changing automatic outcomes advance
the policy from actual observations rather than replaying a fictional countdown/arrival.
This instantiates the [conditional full-word contract](mathematics.md#conditional-complete-harmful-service-application),
not a native experiment, whole-pool feasibility, repeated-cycle rate or strength result. Its
raw composition census finds 296 candidate groups on 32 of 40 maps, including none on Packs;
composition alone does not establish a complete feasible service.

A separate [two-harmful application](../../../results/contenders/coverage-atlas/two-harmful/native-continuation/assessment.json)
validates a recovery edge inside unfinished work on YBR group 8/base Vendetta. After one declared
quiet ally parking move, the probe kills wolf 76 at round 23, walks onto own spawn at 26 with
200 HP and observes full 1000 at 27. It stops at 34 because a guard incorrectly demands a live
state query for the first, unavailable member; that original error remains recorded. One
official Seek/Branch continuation starts from the actual retained round 34 input, without a
fabricated restore, and executes the unchanged model suffix. It kills wolf 108 at 53, finishes
the group at 59, receives 15 gems/100 gold at 65, recalls while loaded and arrives at 71; owner-end
healing is observed at 72. Tribute begins at 88, the actual bank transition is 90, and the final
spawn endpoint at 105 has 1000 HP, 100 gold, bag 0 and ready allowances. All 128 host-dispatched
primitives pass independent saved-response checking. This is one controlled native mechanism
and one actual recall landing, not uninterrupted bot execution, WASM fuel qualification,
repeated throughput or competitive strength. The other 15 landings remain conditional model
evidence. Partial destroyed work plus actual recovery is therefore a real alternative to
requiring an entire fresh group to fit one health bar; it is not automatically the best
alternative to investment, ability micro or cooperation.

The [source-free main calibration](../../../results/contenders/throughput-bounds/main-v44-passive/assessment.json)
and [Q0 deadline join](../../../results/contenders/throughput-bounds/deadline-join/assessment.json)
give a separate finite capacity test. Main reaches 1000 banked gems on all 40 pool maps/both
sides against a frozen passive, in 376-1597 rounds (median 990.5). Q0 permits only harmless-source
Move/Basic/Wait service and no initial or champion-acquired gems. Even granting every such
group, free initial payout, free work and instant delivery, its supply ceiling misses that
per-key deadline on 67/80 keys; 46 have zero source. Packs A is 952 at 844, whereas Packs B is 1008
at 879. The latter permits an upper bound, not a feasible program. Independent capture/damage
bounds are combined by minimum, never by adding overlapping channel durations. This rejects
Q0 as a sufficient isolated economic class on those keys; it does not establish head-to-head
loss, because contesting main may change its pace, or limit broader ranged/spell/dash operators
that harvest harmful sources without exposure.

Mosaic's [frozen R18 keeper](../../../results/contenders/mosaic/ordinary/kernel-r18-keeper/index.json)
binds `00d66a796cd8`, one full 2,000-round Packs A/passive game. The group clear at round 41
leads to shared receipts at 47 of 10/9/9; teammates bank 18 at 56. Varg0's own ten-gem tribute
completes at 64, raising team score from 18 to 28, then he recalls at 64, actually arrives at
spawn at 69 and moves at 69/70.
This is an observed bank/recovery/departure suffix, with no second clear, receipt or bank and
no represented monster damage after 41 through 2000. The later native100-102 window records
ready idle. The selected group is harmless, and Varg arrives already at full HP: this is not a
native harmful-service or healing-from-deficit witness. It proves neither continuous idle
after departure nor a private cause. The separate
instrumented `1a034147407b` diagnostic and its short projected comparison are not the source
of this full-game claim. One realized recovery endpoint does not establish renewed productive work.

Timetable now supplies a later [selected-refresh application](../../../results/contenders/timetable/independent/4a9db07ca948/local-prerequisite/assessment.json),
source `4a9db07ca948`/ON `25a8f73df008`: one full YBR A/passive game, 242 banked,
13 clears/receipts/banks and four purchase events, with late production and clean measured
technical health (ordinary peak 91,670). The
[actual Vesper word](../../../results/contenders/timetable/independent/4a9db07ca948/local-prerequisite/micro-audit.json)
is a continuous R41 Basic -> Reset -> Basic owner turn; reset restores attack, not movement.
Its native input counter advances within that turn. Selected actor audit establishes later
Ryth renewal after bank/shop, not renewal for all five actors.

The competitive result remains a different frozen source:
[314 DIRECT versus PREFIX](../../../results/contenders/timetable/independent/314b38674e89/fresh-controls/assessment.json),
DIRECT `31acc1ca3c03`, YBR/v44 both sides, banks 55/17 against main's 1000/1017, versus
PREFIX zero on each side; all four games have clean measured technical health. DIRECT loses
both keys. The [finite gameplay comparison](../../../results/contenders/timetable/independent/314b38674e89/fresh-controls/behavioral-equivalence.json)
finds all four specified world payloads equal to the earlier b7 wave, so this adds no independent
strategic coverage and transfers no unrepresented fuel/private state. Source 4a9 has no
current-main result from these games. Earlier
[e035 late harmful conversion](../../../results/contenders/timetable/independent/e035a183bbe6/prefix-prerequisite/assessment.json)
remains a separate one-key application; its 154 banked result does not qualify a successor.
These source-specific applications replace neither broad paired evidence nor useful continuation
for every body.

The staged Mosaic predecessor is [retired with its notes](../../miscellaneous/mosaic-guarded-perch-service/NOTES.md);
[verification](../../../results/contenders/mosaic/retirement/guarded-perch-service/verification.json)
preserves all 42 files. Its [sampled R300 negative-family wait](../../../results/contenders/mosaic/interpreter-r1/diagnostic-r300/index.json),
[R2 late quiet production](../../../results/contenders/mosaic/support-bound-r2/index.json)
and [R3 Ryth harmful conversion/purchase](../../../results/contenders/mosaic/siege-r3/index.json)
retain their distinct scopes. The R3 A-role exclusive-walk errors fail that source's health.
These are historical applications of observation provenance, actual walk history and complete
conversion, not qualification inherited by the fresh lean controller.

The fresh lean [R2 keeper](../../../results/contenders/mosaic/lean-r2/index.json) binds
`a353f1b5558d` on Packs B/passive. It wins with 1012 banked and 37 clears. Native statistics
report 980 completed rounds, 981 started and a partial final winning round; the completed-round
ledger is not the last event timestamp. The
[independent technical audit](../../../results/contenders/native-evidence/mosaic-lean-r2-technical.json)
checks seven keeper descriptors, ten frozen source inputs and equal raw/injected
`a2b17dad...` bytes. Both teams have zero faults, error lines and exhaustion; own ordinary
peak is 87,553/100,000, first-body peak 43,777/200,000, with no budget-reaching records.

The [retained native statistics](../../../results/contenders/mosaic/lean-r2/native/00packs_mosaic-navigation-passiv_mosaic_52956b31.json)
supply a stronger production boundary than a harmless-only class: 22 harmless clears yield
616 gems, 13 elk-group clears yield 390 and two wolf-group clears yield 16. Collected
1022 = banked 1012 + Varg's final bag 10. All three actors have late clear/receipt/bank events.
Ryth has no damage or income in either middle 245-round bin, then resumes in the final bin;
this is observed resumption, not a per-input liveness guarantee. Banked gems in the explicit
bins 1-245/246-490/491-735/736-981 are 142/258/288/324.

Sixteen purchase events acquire 113 shards for 3390 gold; earned 6106 = spent 3390 + held 2716.
Actual bank -> purchase -> later elk-clear chains occur, but shared receipts and merged banks
do not establish exclusive solo work or marginal item value. The compact eight-node planner's
[separate 25-round component costs](../../../results/contenders/mosaic/lean-word/fuel-r1/assessment.json)
explain a bounded implementation change; those probes are not this full game's health evidence.

Execution provenance remains narrower than strictly opaque/contained play: the recorded driver
passes captured Entrant directories into CLI `--release`, which may recheck/build. The keeper
has module/hash and native-row audits, not an independent consumed-byte selector or WindowsJob
receipt. No model/Python main-source inspection was reported; these facts do not turn the run
into a strict source-free/contained execution claim. Preserve the useful native result without
rerunning an identical key for paperwork. No other map/side, opponent or current-main strength
qualification, and no qualification of later deduplication/source changes, follows.

The separate [20-game cohort audit](../../../results/contenders/native-evidence/mosaic-lean-r2-cohort.json)
covers ten ON/old-default paired keys: main on Packs, BigWorld and YBR, both sides; passive on
BigWorld and YBR, both sides. ON loses all six main games. The two Packs B E44 lines and one
YBR B E44 line belong to Mosaic; main has zero faults, exhaustion and error lines in all twelve
main games. The frozen OFF `6c5faaceb258` is an old-default control, not independently proved
current-source OFF until an explicit equivalence is supplied. Its YBR exhaustion/E22 failures
do not excuse ON health failures. The cohort's passive 3/4 wins include an empty YBR B tiebreak;
all four finish below 1000 and both YBR ON sides have zero economic production.

[BigWorld A's retained clarification](../../../results/contenders/native-evidence/mosaic-lean-r2-bigworld-a-production.json)
also prevents a metric mistake: zero own final-clear credit accompanies 7,340 own monster
damage and a real 228-gem receipt/bank. Main Varg receives group-16 clear credit at R1062;
own Marcella receives the reward at R1084 and banks at R1129. Own aggregate damage is not
group-attributed, so exclusive enemy labor is unproved. The scanner's zero-clear flag follows
its definition; it does not establish zero labor, zero economy or recurring autonomous work.

The [historical driver provenance audit](../../../results/contenders/native-evidence/mosaic-lean-r2-provenance.json)
retains the source-directory/recheck and missing process-containment limits separately from
native integrity. Future Binding/opaque-WASM execution does not retrospectively add those
receipts to these games, and identical keys need not be rerun just for that paperwork.

The independent Warband SERVICE [fixed audit](../../../results/contenders/warband/service-health/objects/a86e67e3efe2fdde06b152a46e2134f0e74e3bfe2f60f46f8e761cc04da0e132.json)
binds `d01adcbc055d`, Packs A/passive, engine1.0.3 for 2,000 rounds: 28 clears, 784 collected
and banked gems, 3,644 earned gold, no deaths/faults/errors/exhaustion and zero spending.
Actual events strictly after 1500 include eight clear credits and 224 collected/banked gems;
the final bank is at 1954. This is repeated and late conversion, not merely one early cycle.
Ryth1's own group3 clear at 102 and 28-gem receipt at 108 join his native tribute at 118 and
completion at 121: bag28 becomes0 and scoreA28 becomes56 in that exact owner-start event.
He immediately moves, holds full 1,200 HP, no channel/stun/restriction, zero cooldowns and all
allowances ready at `(8,7)` in rounds 140-167, departs at 168 and applies his next actual basic
at 176. Every observed Ryth state in the complete native118-181 window is outside own spawn.
Therefore full readiness and
later work are observed, while actual spawn renewal is not; zero spending supplies no
investment witness or proof that a required-shop contract was fulfilled.

On the same Packs/v44 environment keys, the [paired full-game binding](../../../.cache/contenders/warband/service-packs-main-r1/binding.json)
compares that ON source with fresh-starter OFF `9e332fab79fe`, both sides. ON loses 0-1006 on A
after 944 rounds and 28-1012 on B after 915; OFF loses 76-1020 on A after 821 and 88-1020 on B
after 848. All four games
have zero native faults/exhaustions/error lines. These are terminal outcomes at their own
horizons, not common-horizon production rates or four independent environment keys. Passive
production does not establish pressure survival or strength, and these outcomes do not isolate
one component or a unique private defect. Raw/injected ON module roles differ (`61ade898...`
versus `d99a2db3...`); both are pinned, but the consumed role lacks an independent selector
receipt. In the passive audit, ordinary-profile fuel peaks at 32,744 versus the all-role raw
stream's 52,637. Neither measured peak proves a universal whole-turn cost bound.

The [first deployed Warband capital panel](../../../results/contenders/warband/capital-work/native-r1/assessment.json)
binds candidate `9e1676555757`, control `12f2d3b4779a` and exact RAW executable roles:
16 full games on Packs/YBR, both sides, opaque v44/passive, candidate/control. Actual capital,
harmful work, support and conversion now occur in a full runtime; this is not just a pure
fixture. Candidate passive Packs banks 1004/1026, versus control 784/588 at 2000. The completed-
round ledger reports 1834/1642 while last actual bank event timestamps are 1835/1643; preserve
those different conventions rather than silently shifting an event.

All three candidate Packs actors actually purchase equipment. A saved A-side R80-118 window
records Varg/Willow damage 560/790 to the same group, Willow's actual heals, Varg's shield,
clear R107, both four-gem receipts R113 and actual banks R118. This is joint labor, support and
conversion, not a demonstrated advantage over complete independent alternatives. Late Packs
conversion is Vendetta's: Varg/Willow's last monster-damage rounds are A 552/361 and B 348/239.
Their earlier useful work does not establish all-actor repeated service.

The complete panel fails health and the competitive bar. Candidate YBR/passive has five
exhaustions on each side and zero economic production, while still dealing 720 monster damage
on each side (Willow 40, Marcella 480, Ryth 200). Zero income or clear events is not zero action.
All four candidate/v44 keys lose: banked Packs 24/24 and YBR 0/11. The health failure, bounded
coverage, control role differences and later unqualified source changes stay explicit; no
whole-pool, fresh-opponent or strength qualification follows.

The [retained late-work audit](../../../results/contenders/warband/capital-work/native-r1/support-audit/late-work-audit.json)
sharpens that renewal gap. After an actual eight-gem bank at R571, Varg reaches `(12,0)`
at R586 and alternates `(12,0)`/`(12,1)` through R610 with HP 748 and ready owner starts.
Both cells are distance four from the nearby range-three wolves: they are outside the actual
camp footprint but inside the source's extra one-tile margin. The frozen escape scoring
allows nondecreasing distance, consistent with the observed cycle; private mode is not saved.
Movement count and conservative hazard exclusion do not themselves establish return progress.

[C24](combat.md#c24-damage-erased-by-recovery-need-not-advance-the-objective) records a separate
Willow loop: her legal attacks repeatedly lose their target-HP progress to actual spawn recovery
while she retains a bag. This is a game-state/recovery question, not an opponent-specific
exception. The same frozen source's 48-byte report exceeds the queried eight-byte team register,
so its guards suppress reports. Earlier joint labor/support is therefore opportunistic
co-location, not evidence of named runtime coordination. The new nomination protocol and
later source repairs have their own scope; neither actual co-location nor an eight-byte packet
proves timely acknowledged helper contribution.

A later [rolling-work RAW preservation](../../../results/contenders/warband/capital-work/capital-ybr-r5-raw/assessment.json)
binds source `d8f11bb85210` on one full YBR A/passive native match: 55 banked, 3240 earned
gold and actual work across all five bodies, but 59 native exhaustions. Its standard result is
rejected as incomplete exhaustion context: only 32 events/team are retained in that diagnostic
context while the full native events exceed the cap. The retained full native archive and
statistics establish the exhaustion failure; context truncation does not erase it or certify
a complete runner measurement. RAW `e8713e10...` and injected `4b429af4...` differ, so no
role transfer is made. The cached-field/fuel successors retain separate evidence. Useful
expanded work, charged whole-pipeline cost and health remain different results.

### Complete harmful service is an integration boundary, not a new score weight

The supply result rejects the specific Q0 source restriction, not every current contender.
Timetable and Mosaic now have actual harmful-source conversion on their stated finite keys;
Warband's rolling class has expanded actual work alongside a failed-health result. A represented
operator, actually selected actions, one complete native instance, repeat production and
competitive strength remain different claims.

The shared structural operator is **resumable harmful service**. Existing mechanisms support
concrete instances; they do not establish a mathematical minimum or all-map feasibility.
Its residual state and continuation connect:

1. Actual available capital/gear and a reachable ingress to a useful harmful group. If a
   funding or purchase prelude is required, observe the gold and actual purchased equipment;
   a forecast or an attractive gold weight supplies neither.
2. A finite damage prefix with individual survival through every ordered hostile opportunity,
   including camp wake closure and priced movement refusal/computation waits. Use compatible
   movement, basic, cast and reset words rather than pooled HP or independent ready abilities.
3. When one health bar cannot finish, actual egress and recovery followed by fresh observation
   and renewed work on the same residual group. Persistent camp damage permits this edge; it
   does not grant knowledge of an unseen group's current state or future access.
4. Actual clear, contested-aware capture, receipt, bank and an observed terminal from which the
   specified next operator can begin. Readiness outside spawn may satisfy a declared basic-work
   continuation; it cannot silently satisfy a required spawn/shop contract.

The [named party-service contract](data/combat/party-service-contract.json) extends this word
with participant roles, legal stations, arrival deadlines, personal capture recipients and
complete displaced solo continuations. Its first rolling scope uses Varg's own exit, actual
Willow support and fresh quotes; promised support requires a named ordered policy and its
interruption premises. Wrapped intents nominate rather than commit. The retained contact/death
witnesses keep opportunity, actual action and useful contribution distinct. Unknown alternatives
limit a benefit claim, not a conditional experiment or permission to begin one.

Pressure belongs throughout this word, but universal safety against every hidden foe is not
a prerequisite for a productive controller. An owner may deploy an explicitly limited risk
model with bounded retreat/counteraction, fresh actual receipts and measured native outcomes.
`Unsupported`/`Unknown` describes a model boundary; it must not silently become a permanent
controller terminal. Formal guarantees cover only their stated response class. A bounded
interruption preserves actual work, resources and outstanding receipt/channel state while
the controller chooses its next executable continuation; it does not itself prove a safe
hold, retreat or future progress.

For an advertised universal pressure guarantee, the represented class must cover every allowed
ordered continuation and actual primitive-result branch through the next decision. Empty
personal enemy vision does not certify no foes. [C23](combat.md#c23-protection-through-our-next-input-must-cover-ordered-enemy-opportunities)
and [U12](micro.md#u12-damaging-heart-can-strengthen-its-next-response-and-one-disabled-input-is-not-safe-renewal)
show why allowance legality, one stun and decreased enemy HP do not establish that guarantee.
These are scope rules for claims, not another offline gate before a runtime discriminator.

The [Vendetta/YBR continuation](../../../results/contenders/coverage-atlas/two-harmful/native-continuation/assessment.json)
already witnesses one controlled complete partial-work/recovery/residual-clear/capture/bank
word. The [Vesper service](../../../results/contenders/mechanics/vesper-dash-service/witness.json)
witnesses a different reachable operator: ranged clear, compatible dash onto the capture tile,
recall overlapping payout, bank and ready return. Neither script supplies general controller
integration, repeated throughput or coverage of every map. Investment and useful parties are
possible additional ways to make a harmful prefix feasible, with their own capital and
displaced-labor obligations; the existing evidence does not establish their necessity everywhere.

**Stopping model-only expansion.** The next useful discriminator is an unchanged full runtime
that repeatedly executes its declared complete class, then a paired source-bound comparison
against the current opaque main on both sides. More component fixtures or native local words
cannot close an integration stall. After a repaired narrow prerequisite, extend evidence over
all pool keys and fresh maps/opponents; do not substitute a broad panel for a known failed
continuation. Record every actor's early and late work so one early bank cannot hide collapse.

Funding from harmless groups alone is also not an all-pool initial operator. The existing
[atlas](../../../results/contenders/coverage-atlas/atlas.json), SHA256 `8c5c91952894e6a7...`,
has 80 side records for 40 maps. Wholly harmless gold-only groups exist on 28 maps. Its
restrictive quiet/progressive melee graph supplies funding witnesses on 12 maps; a missing
witness remains Unknown, not unreachable. Seven maps have no wholly harmless group yielding
either currency: `00eight`, `00elkring`, `00fragment`, `00rushbait_v2`,
`01concentrated_wealth`, `01corridors`, `01starfish`. Under the same restriction to initially
wholly harmless camp sources, these seven offer no first camp income to fund equipment.
Direct harmful work or another initial resource class is needed there. These are exact
reductions of the retained static grouping, whose geometry/pool hashes still match the current
files; they prove no route, pressure safety, complete service or useful rate. No game or new
reachability search was performed for this reduction.

No current result proves that this class can produce 1000 banked gems on every pool map,
beat main's isolated pace or remain feasible under unrestricted pressure. All-map coverage
still needs actual kit/terrain/bank access, supported harmful compositions, fresh information,
every per-body survival prefix, conversion/recovery suffixes and useful elapsed time. Adding
a harmful operator removes the syntactic Q0 exclusion; it does not prove those premises.

## One structure, three answers
This table records historical doctrines and hypotheses. It does not certify that current bots
realize them or require future implementations to keep them. Current directions and experiment
state live in the contenders charter and each bot's notes; G11/U9 are shared derivation questions
with independent strategic answers.

| Structure | timetable | warband | compound |
| --- | --- | --- | --- |
| G1 shared pool | schedule the pool to its respawns; own ground first under threat | take the contested pool and the jackpots by force | spread out to cover the whole pool |
| G2 kills and idle fights | avoid fights that do not protect the schedule | fight for kills it can count on | fight only when the kill is near certain |
| G3 leaving early | never be found: route around contact, leave at first sight | contact only on its terms; the whole pack leaves together while the exchange is even | durability buys time to read the exchange; leave before it turns |
| G4 numbers | not being caught is the answer to being outnumbered | always the larger group, and pay for it with burst kills and jackpots | converge on an isolated enemy; never be the isolated one |
| G5 local information | the plan carries static knowledge; bytes for deviations | bytes as the pack's eyes and leader calls | bytes as convergence beacons |
| G6 currency and length | the plan prices gems and gold per map over the whole race | jackpots (camps only a pack can take) are its currency | gold first where the race is long; gems where it is short |
| G7 fundamentals | farm rate per alive round, by construction | deaths per fight, by numbers | dead time, by durability |
| G8 health-locked supply | the plan sizes a party for every target and schedules the sieges | the pack is the siege party; the big content is its income | durability lets one champion take a single elk; converge for packs and sieges |
| G9 tempo per pair | slots and priority planned so that escapes work when contact is unavoidable | priority set so the pack strikes first and chains its stuns | priority set so converging allies strike first |
| G10 safeguards | met by routing: stay out of the enemies' two-turn reach | met by numbers: be the larger side wherever contact can happen | met by durability and convergence: never the lone one within reach of two |
