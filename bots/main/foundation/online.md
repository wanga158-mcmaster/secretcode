# Online evidence: what strong teams do, and what the data cannot show

The evidence layer of the [foundation](README.md), entries O1-O20. Everything here is measured from
online games held locally (no server access). Per the charter's "Domination, not adjustment", these
games are evidence of mechanisms, not templates to copy or targets to tune to: each entry says
which mechanism a pattern reveals, how general it is, and what the data cannot show (O19).
O1 is the catalog of opponent behaviours each contender must answer.

Regenerate everything: `python -B -I -X utf8 bots/main/foundation/tools/online/all.py`, with
the runtime python (`arena env`). It is read-only, takes about 3 minutes, writes tables to
`data/online/*.json` and logs to `.cache/contenders/online/logs/`. One game's timeline:
`tools/online/timeline.py MATCH_PREFIX TEAM`.

## Coverage and method (applies to every entry)
- **Summaries:** 1,126 games, created 21 Oct 09:01 to 22 Oct 04:11 UTC, all on engine 1.0.2, all 40
  pool maps (16-57 games each). Sources: `results/online/` (our groups, ranked) and main's
  `.cache/main/online/summaries/`.
  - 68 identical replays of repeated groups were dropped (games are deterministic).
  - 901 games involve us (ha²t, versions v1-v36); 398 opponent sides face our v33-v36, 247 of them
    from main's survey.
  - 225 games involve two other teams: 150 from main's stratified ecosystem sample, the other 75
    mostly between T1 and T2 teams (54 T1-T1).
- **Tiers** come from the ladder snapshot of 22 Oct 03:2x UTC (742 teams): T1 = ranks 1-10,
  T2 = 11-50, T3 = 51-150, T4 = 151-400, T5 = 401+. We were #36. Other-team sides per tier
  (distinct teams):

  | tier | sides (teams) | sides from games not involving us |
  | --- | --- | --- |
  | T1 | 353 (10) | 147 |
  | T2 | 509 (33) | 80 |
  | T3 | 266 (40) | 72 |
  | T4 | 142 (31) | 75 |
  | T5 | 77 (25) | 72 |
- **Tracks:** 1,521 replays decoded with `tools/inspect-strategy-replay.mjs`. 1,000 games have both
  tracks and a summary, giving 997 other-team sides (T1 308, T2 412, T3 189, T4 75, T5 13).
  Positions are sampled at each champion's turn start.
- **Global window:** every ranked game created 22 Oct 03:10-03:20 UTC: 1,922 games, 3,844
  team-games, 716 teams. It holds drafts, results and end reasons only. Each team played one group
  of 5 maps against a near-rank opponent.
- **Rating model:** the platform's ratings are compressed. With a median gap of 9.6 points, the
  higher-rated side won 67%.
  - Fitted curve: p = 0.16 + 0.68 / (1 + exp(-0.097 × gap)), checked to ±4 points per decile in
    the window.
  - A **residual** is (win − p) in points: a rating-adjusted association, not an effect.
- **Map classes** are this page's own, deliberately coarse. maps.md owns the real ones, and the
  00/01 name prefix is not a size class (00ambush is 100x100, 01gold_rush 31x31).
  - **touch** (spawns ≤ 15 apart): 00gem_fields, 00quadrants, 00reverse_spiral, 00road_of_riches,
    00rushbait_v2, 00swirl, 01shortcut.
  - **gemrich** (harmless gem rate ≥ 4 a round): 00great_wall, 00lost_world, 01boss_battle,
    01the_farm.
  - **far** (spawns ≥ 60 apart): 00ambush, 00expanse, 00lanes, 01big_world, 01fracture,
    01starvation.
  - **mid**: the other 23 maps.
- **Selection:** 80% of these games involve us, a rank-36 team. O19 lists what that and the other
  limits do to each number.

## Five findings for bot authors
1. **Fundamentals separate the tiers (O2, O13).** At T1, a champion earns 1.05 gold per alive round
   by r400 against our 0.84. It dies in 13% of its fights; ours die in 30%. No style choice carries
   a robust rating-adjusted edge.
2. **Gold compounds and beats kills (O8, O9).** When one side leads in gold and the other in kills
   at r400, the gold leader wins 74% (31 games). A kill counts mainly through the gold, bag and
   time it moves.
3. **The map decides the currency (O10).** On gem-rich maps the banked-gem leader beats the gold
   leader in two thirds of conflicts. On mid maps the gold leader wins three quarters.
4. **Contact is escapable at the top (O13, O14).** 75% of T1 fight episodes end in separation.
   Deaths follow about 18 rounds of contact that began at full health: attrition, not ambush.
5. **Games last about 1,100 rounds at the top (O7), and top teams win in different ways (O1,
   O16-O18).** Survival-heavy solo farming (procless), a gem-carrying Prism (prism best girl) and
   grouped Focus burst (Intercontinental Fish) all reach the top three. A contender needs an
   answer to each.

---

### O1. Opponent behaviour comes in about 20 measurable kinds, and their mix changes sharply by tier
Claim: every flag below is measured on one team-side in one game, mostly before r400, so that it
  describes a choice rather than the result. The table gives the share of sides showing it.

  | kind | measured as | T1 | T2 | T3 | T4 | T5 | us |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | meta draft | 3+ of Ryth/Vesper/Vendetta/Marcella/Heart, incl. Heart or Marcella | 84 | 85 | 64 | 13 | 10 | 99 |
  | template draft | 3+ of C&I/Stellar/Ren/Varg | 1 | 2 | 8 | 40 | 18 | 0 |
  | grouped | ≥ 40% of champion-rounds r1-300 with 2+ allies within 4 | 21 | 18 | 24 | 28 | 46 | 9 |
  | dispersed | ≥ 55% of champion-rounds r1-300 with no ally within 4 | 19 | 22 | 31 | 28 | 15 | 36 |
  | Health build | ≥ 45% of shards by r400 Health | 66 | 36 | 30 | 41 | 18 | 7 |
  | Focus build | ≥ 30% Focus | 28 | 18 | 24 | 44 | 35 | 6 |
  | Strength build | ≥ 65% Strength | 2 | 23 | 26 | 9 | 52 | 73 |
  | Treasure first | ≥ 60% of champions' first emblem is Treasure | 54 | 51 | 11 | 13 | 14 | 78 |
  | early shop | median first purchase ≤ r100 | 42 | 30 | 31 | 24 | 8 | 28 |
  | contact seeker | own first champion damage ≤ half the map's median first contact | 5 | 6 | 10 | 6 | 4 | 6 |
  | contact avoider | own first damage ≥ 2.5x the map median, or none | 9 | 5 | 9 | 25 | 31 | 7 |
  | invader | ≥ 25% of champion-rounds r1-300 nearer the enemy spawn | 22 | 20 | 17 | 3 | 31 | 14 |
  | turtle | ≤ 3% in the enemy half (r1-1000) and ≥ 8% on/next to own spawn | 1 | 2 | 3 | 24 | 8 | 7 |
  | gem rush | 3+ gem-cluster captures by r200, ≥ 40% of captures | 14 | 15 | 11 | 6 | 9 | 8 |
  | monster farming | ≥ 55% of captures by r400 are wolf/elk/golem/monolith | 62 | 56 | 55 | 76 | 51 | 67 |
  | early siege | golem or monolith cleared by r300 | 3 | 1 | 3 | 1 | 6 | 2 |
  | lock user | 3+ altar locks by r400 | 59 | 61 | 66 | 54 | 30 | 66 |
  | no locks | no lock all game | 39 | 29 | 25 | 37 | 52 | 25 |
  | crystal routes | 2+ teleport crystals by r400 | 18 | 17 | 10 | 7 | 0 | 0 |
  | big bags | median tribute bag by r400 ≥ 40 | 19 | 14 | 8 | 26 | 7 | 5 |
  | battery* | a Prism carrying ≥ 150 at r400, or any champion ≥ 300 | 4 | 1 | 5 | 0 | 0 | 4 |
  | sustain* | ≥ 3,000 healing in the game | 7 | 9 | 10 | 8 | 9 | 0 |
  | spawn camping* | 2+ kills within 6 of the enemy spawn by r1000 | 24 | 18 | 17 | 7 | 13 | 3 |
  | camp suicide* | 3+ deaths to camps by r400 | 8 | 12 | 12 | 33 | 60 | 1 |

  \* depends on how the game went, so it is reported as prevalence only.

  Each kind, with the mechanism it exploits, who does it, its record, and what an answer has to
  handle:
  - **Early aggression** (contact seekers; subscribe to caterpillow, HouseOfCoders 3b2729d2
    00gem_fields, geese 614f95e3 00fragment per main's catalog).
    - Mechanism: first blood takes 100 gold, the victim's bag and about 60 rounds of its farming.
      The side with the only kill by r100 won 58% of T1-T1 games (19), 66% of other games (65)
      and 73% against us (211).
    - Record: seeking contact itself has no edge. Residual -1.7 (27 sides, games not involving
      us). T2 seekers beat our v33-v36 in 38% of games, other T2 sides 54% (13 vs 175 sides).
    - Answer must: survive or win the first contact, which happens about r29 on touch maps (O12).
  - **Grouped and dispersed play** (grouped: Intercontinental Fish 59% of games, Mite, dromedaries;
    dispersed: BashCode, procless near the line at 52% solo).
    - Mechanism: numbers decide local fights (G4), but captures do not speed up with allies, so
      grouping costs throughput.
    - Record: neither wins by itself (O15).
    - Answer must: refuse isolation against a group (21% of T1 deaths are "picked", O14), and
      out-farm a group by covering more camps.
  - **Gem rush** (Univardisty 8f055441 00lost_world: 784 banked by r400 with 354 gold; octo's
    odyssey and Mite per main's catalog).
    - Mechanism: on gem-rich maps the race ends before gold compounds (O10).
    - Answer must: price gems by map supply and race length.
  - **Jackpot control** (main's catalog: the jackpot leader won 31 of 36 sampled games; IF
    b4b7450c 00contest took the 200-gem monolith five times).
    - Mechanism: when one group is most of the map's gems, holding it is both income and denial
      (G1).
    - Answer must: contest or deny the jackpot, or out-race it elsewhere.
  - **Scaling builds**: Health-first (66% of T1 sides) and Focus burst (IF, Zarzorki).
    - Mechanism: items compound (O4, O8). Health converts gold into fight survival; Focus feeds
      Heart's dash and shield, Stellar and Prism.
    - Answer must: keep pace in item value. A bot that buys late loses the compounding (O5).
  - **Spawn camping and late sieges**.
    - Measured: 24% of T1 sides make 2+ kills by the enemy spawn. 16% of T1 victims and 21% of
      ours die within 6 of their own spawn, at a median r699-760.
    - In 22-30% of those deaths the same champion had died less than 90 rounds earlier.
    - Mechanism: the leading side squeezes the losing side's exits late in the game.
    - Answer must: hold safe exits or alternative farming routes when behind.
  - **Lock harassment**.
    - Measured: lock users are 59-66% of T1-T4 sides. The top 3 teams buy none, while #4-#8 buy
      locks in 87-100% of games.
    - Lock placement is not in the summaries (O19).
    - Mechanism: a lock blocks enemy tributes until 5 basic attacks remove it, and cancels a
      channel.
    - Answer must: break or avoid a locked altar, and lock a long enemy channel (battery).
  - **Chases**: 4-8% of deaths by our class (O14), more when "home" deaths are counted.
    - Mechanism: equal speed keeps a chaser in reach (G3), yet 75% of T1 fight episodes end in
      separation (O13).
    - Answer must: start disengaging early, and count the escape before contact.
  - **Kiting** is universal range discipline, not a distinct strategy.
    - Ranged champions deal champion damage with no enemy adjacent in a median 90-93% of their
      damage rounds (tiers T1-T4, sides with ≥ 15 such rounds; us 85%).
    - Answer must: assume every ranged enemy holds range 2-3.
  - **Bush ambushes** are rare killers at the top.
    - A killer-team champion stood in a bush within 6 of the victim when contact began in 9-15% of
      deaths.
    - Bursts (dead within 5 rounds from ≥ 70% health) are 2-5% of T1-T3 deaths, against 18% of T5
      deaths.
    - Answer must: consider bush vision, but this is a minor cause.
  - **Prism battery** (prism best girl, OneTap; O17).
    - Mechanism: Prism gains 1.5 Focus per carried gem, so a 700-gem carrier hits like a boss, and
      a tribute channel is broken only by stun, death or lock.
    - Answer must: kill, stun or lock-deny the carrier. Its bag is the largest bounty in the game.
  - **Big bags / hoarding** (policy wonk, SilverSamurai; Sloppy Joe and Nameless per main).
    - Mechanism: fewer altar trips, but the whole bag goes to the killer.
    - Record: T2 big-bag sides beat our v33-v36 in 32% of games, other T2 sides 59% (28 vs 142).
  - **Territory invasion**: farming the enemy half, 22% of T1 sides (dromedaries 83%, subscribe to
    caterpillow 50%).
    - Mechanism: a capture taken from the enemy's side is a double swing (G1).
    - Record: T2 invaders beat our v33-v36 in 72% of games, others 48% (36 vs 152).
  - **Passivity and turtling** (contact avoiders, turtles: 25-31% of T4-T5 sides).
    - Mechanism: no deaths, but the map is conceded.
    - Record: avoiders residual -5.1 (86 sides).
    - Answer must: keep farming next to a passive enemy rather than stand in quiet contact (main's
      topteams.md: we spent about 200 champion-rounds a game there).
  - **Sustain** (Willow; Sponge, policy wonk, Mite, Sic Mundus Creatus Est).
    - Mechanism: heals replace recall trips and refill a front line.
    - Answer must: count enemy heals in any fight estimate.
  - **Teleport-crystal routing** (EMPEROR Tung in 92% of games, E=mc^2 63%).
    - Mechanism: a 3-turn channel crosses walls and holes, shortening routes.
    - Answer must: expect enemies arriving through walls on wall maps.
  - **Early siege** of golems and monoliths by r300: 1-6% of sides, rare.
  - **Camp suicide** (60% of T5 sides): an opponent that throws its own champions away.
    - Answer must: finish the game cleanly, and remember the r2000 tiebreak (O7).
Evidence: `tools/online/style.py` (flags), `catalog.py` (tables) and `deathtab.py`; data in
  `data/online/catalog.json`.
  - Coverage: 1,347 other-team sides (tracks for 997). T5 has only 13 tracked sides, so its track
    flags are thin.
  - Examples per flag: `catalog.py`'s "example teams" table (teams with ≥ 3 sides showing the
    flag in ≥ 50%), in `data/online/catalog.json`.
  - Main's ecosystem catalog (`.cache/main/analysis/ecosystem/catalog.md`, 108 teams) adds jackpot
    control, deathballs and per-team detail.
Rests on: G1-G4 (structure tested here), O2, O10, O13-O15.
Status: measured (online, engine 1.0.2, 21-22 Oct); flag thresholds are judgment calls.
Recheck:
  - After each sprint (hidden bots, O19), on a new patch, and with a second global window.
  - New kinds may appear; the list is what was seen, not what is possible.
Consequence:
  - Each contender's notes should answer every row.
  - The answers that matter most at the top are to scaling builds, gem rushes and jackpots,
    batteries, isolation by groups, and late sieges.

### O2. Fundamentals, not style, separate the tiers
Claim: per alive champion-round over rounds 1-400:

  | | T1 | T2 | T3 | T4 | T5 | us |
  | --- | --- | --- | --- | --- | --- | --- |
  | earned gold | 1.05 | 0.92 | 0.82 | 0.61 | 0.39 | 0.84 |
  | dead champion-rounds by r400 | 102 | 109 | 125 | 227 | 440 | 132 |
  | collected gems per 100 alive champion-rounds | 12.6 | 11.4 | 10.0 | 9.4 | 5.1 | 9.3 |
  | P(death per fight episode) (O13) | 13% | 12% | 19% | 24% | 52% | 30% |

  - In T1-T1 games, winners earned 1.05 against losers' 0.84 and spent 108 against 173 champion-
    rounds dead.
  - No early-measured style flag has a robust residual in games not involving us. All lie within
    ±13 points; the largest, dispersed -13 (23 sides), is 1.3 standard errors.
  - Among flags with ≥ 100 sides the largest are lock user +8.5 (195 sides), no locks -6.1 (206),
    early shop +6.0 (105) and Treasure first -5.8 (154), at 1.2-2.4 standard errors.
  - Those four are confounded by who uses them: the top 3 teams buy no locks and go Treasure first,
    and the rating model caps a strong team's expectation at about 84%.
  - Draft families add ≤ ±6 points (O3), grouping -0.9 (36 sides, O15), and builds ≤ ±3 points
    (O4).
Evidence: `extra.py` (rates; earned gold includes kill gold, 13-16% of it by r400, 8% for us) and
  `catalog.py` (residuals).
  - Coverage: 1,344 other-team sides that reached r400, 446 rated sides in games not involving
    us, and 69 T1-T1 games.
Rests on: rating model (Coverage), O13.
Status: measured. The rates are partly outcome-laden: a team that is ahead is harassed less and
  farms faster.
Recheck: a patch that changes kill gold, respawn time or camp yields.
Consequence:
  - The quantities to beat are opponent-independent: farm rate per alive round, dead time and
    deaths per fight.
  - These are exactly the opponent-independent measures the charter puts first. Style is a means
    to them.

### O3. Drafts: the meta family dominates the top, but given team strength no champion is worth more than ±2.5 points; drafts follow the map and the bot version, not the opponent
Claim: pick rates in % of team-games, by rank band (global window):

  | | Ryth | Vesper | Vendetta | C&I | Stellar | Prism | Marcella | Willow | Ren | Varg | Tavi | Heart |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | 1-20 (100) | 68 | 78 | 47 | 16 | 13 | 15 | 74 | 10 | 20 | 10 | 17 | 81 |
  | 21-50 (145) | 76 | 72 | 51 | 13 | 16 | 5 | 67 | 14 | 23 | 32 | 5 | 83 |
  | 51-150 (500) | 65 | 58 | 47 | 19 | 32 | 6 | 53 | 13 | 34 | 39 | 25 | 55 |
  | 151-400 (1,195) | 60 | 30 | 28 | 49 | 64 | 7 | 25 | 10 | 50 | 63 | 37 | 29 |
  | 401+ (1,904) | 60 | 33 | 39 | 56 | 52 | 16 | 29 | 18 | 36 | 65 | 39 | 8 |

  - **First pick:** Heart in the top 20 (55%), Marcella 17%, Vesper 10%. C&I leads below rank 400
    (29%).
  - **Mean pick position in the top 20:** Heart 1.5, Prism 2.4, Vesper/Marcella/C&I 2.8, Ryth 3.5,
    Vendetta 3.6, Willow 4.0.
  - **Commonest comps:**
    - Top 20: Heart+Marcella+Ryth+Vendetta+Vesper, 16 of 62 five-pick team-games (5 teams).
    - Below rank 150: C&I+Ren+Ryth+Stellar+Varg, 255 team-games (93 teams).
  - **Draft size changes the comp.** On 3-pick maps the top 20 never pick Ryth (13 games: Heart
    85%, Vesper 69%, Marcella 69%, Vendetta 38%). On 5-pick maps Ryth appears in 75-100%.
  - **Value:** the residual when a champion is on only one side is between -2.3 (Willow) and +2.5
    (Marcella), from 359 to 878 team-games per champion (all ranks; ranks 21+ alone: -1.8 to
    +2.1). Meta against template: 55% [49-61] (285 games), residual +3.2.
  - **Stability:** 297 of 328 top-20 (team, map) pairs with ≥ 2 games drafted identically.
    - The variations follow resubmissions, not opponents.
    - prism best girl switched Vendetta → Vesper on 01gold_rush between 00:10 and 00:30 UTC,
      against every opponent.
    - Sponge added Willow from 00:30.
Evidence: `drafts.py`, `data/online/drafts.json`.
  - Global window: 1,922 games, 716 teams, engine 1.0.2.
  - Stability: all match lists held (our groups, main's team lists).
Rests on: draft rules (simultaneous picks revealed one by one; both teams may pick the same
  champion, so a pick denies nothing).
Status: measured.
Recheck: every patch (1.0.2 buffed Vesper, Vendetta and Tavi and nerfed Stellar, Heart and Ren),
  each sprint, and a second window.
Consequence:
  - Champion value is realised by the bot that plays it: main's meta draft lost online for its
    engine (D82: 18 → 12 of 50).
  - Draft for the bot's own plan, per map and per draft size.
  - Counter-drafting is unnecessary against today's teams, because they do not react.

### O4. Builds: the top turns gold into Health; the families differ by team
Claim: by r400, T1 sides put 52% of their shards into Health (median), T2 38%, T3 37%, us 15%.
  Health builds are 66% of T1 sides, Strength builds 2% (ours 73%).
  - **First emblem:**
    - T1: Treasure/Health 30%, Bruiser/Health 13%, Treasure/Strength 12%, Treasure/Focus 10%,
      Jungle/Strength 9%.
    - Us: Treasure/Strength 59%.
    - T3: Monster/Strength 29%. T5: Power/Strength 23%.
  - **Whole-game Health/Strength/Focus per champion at T1:**
    - Ryth 61/38/2, Marcella 45/37/17, Vendetta 45/55/0, Vesper 27/73/0.
    - Heart 44/1/54, Stellar 21/0/79, Prism 71/0/29, Ren 44/1/56, Cassi 35/0/65.
  - Top-10 orders (first three emblems, commonest per champion; all in `data/online/items.json`):

    | team | orders |
    | --- | --- |
    | procless | Treasure/Health > Jungle/Strength > Bruiser/Health on Marcella, Ryth and Vendetta; Vesper Treasure/Strength > Jungle/Strength > Bruiser/Health; Heart Treasure/Health > Jungle/Focus > Bruiser/Health |
    | prism best girl | Prism Durability/Health > Jungle/Health > Home/Health (100% Health); Vesper Power/Strength > Durability/Health |
    | Intercontinental Fish | Heart and Stellar Treasure/Focus > Power/Focus > Home/Health; Ren Bruiser/Health > Durability/Health > Power/Focus |
    | :3 | Jungle/Health first on Marcella, Ryth, Vendetta and Vesper; Heart Jungle/Focus |
    | Univardisty | Bruiser/Health > Durability/Health on Heart, Marcella and Ryth |
    | Zarzorki | Heart Power/Focus > Treasure/Focus; Marcella Treasure/Focus > Bruiser/Focus |
  - **Head to head:** Health builds against Strength builds 53% [36-70] (30 games); Focus against
    Health 32% [20-49] (37).
Evidence: `items.py`, `data/online/items.json`.
  - Coverage: 1,347 sides. The per-team orders come from 8 (prism best girl) to 85 (procless)
    games each.
Rests on: rules (shards 30 gold; 5 combine into an emblem; Health bought on spawn heals; Ryth
  gains Strength from bonus Health; Heart's dash and shield scale with Focus; Prism gains Focus
  from carried gems).
Status: measured.
Recheck: item or growth changes in a patch.
Consequence:
  - Survival per fight is the top's separator (O13), and Health is the stat that buys it directly.
  - Strength pays on basic-attack carries (Vesper, Vendetta); Focus pays only on casters whose
    abilities scale with it.
  - Main rejected a copied Health-first plan online for its own engine (D88): the build has to fit
    the fight logic.

### O5. The top converts gold into items early and continuously
Claim: champions' first purchase, median:

  | | T1 | T2 | T3 | T4 | T5 | us |
  | --- | --- | --- | --- | --- | --- | --- |
  | first purchase | r112 | r134 | r130 | r154 | r230 | r132 |
  | team gold spent by r100 / r200 / r400, % of earned | 66 / 81 / 90 | 65 / 82 / 90 | 72 / 85 / 92 | 64 / 85 / 92 | 23 / 52 / 80 | 40 / 67 / 82 |

  - T1 emblems complete at r226, r358 and r459 (us r293, r449, r564). Only 5% of T1 champions end a
    game without an emblem (T5 27%).
  - A T1 champion shops 2.8 times by r400 at a median 120 gold a visit, about one visit every 87
    rounds (us 130).
  - procless's Vesper first buys at r67 and its Ryth at r89; IF's Stellar at r81 and Heart at r93.
  - T2 sides that shop early (median first purchase ≤ r100) beat our v33-v36 in 82% of games,
    other T2 sides in 42% (51 vs 137 sides). The same comparison in T1 is 93% vs 85%.
Evidence: `items.py` (a shop session is a run of purchases with gaps ≤ 3 rounds) and
  `catalog.py`.
  - Coverage: 1,603 T1 champion-games; 398 sides against v33-v36.
Rests on: O4; rules (shopping only on spawn or while dead, except Tavi; recall takes 5 turn ends).
Status: measured. Early shopping is partly a symptom of fast farming.
Recheck: shop price changes, or recall or Home emblem changes.
Consequence:
  - Unspent gold earns nothing; buying the next useful shard as soon as the trip is cheap starts
    the compounding of O8 earlier.
  - A recall made to shop also heals.

### O6. Consumables: locks split the top in two, crystals are a niche routing tool, and wards are cheap noise
Claim:
  - **Altar locks:** T1 sides buy 4.8 a game (3.0 by r400), and 39% buy none.
    - The split is by team: procless, prism best girl, Intercontinental Fish and Trzy Kwaterki buy
      no locks in any game.
    - :3, EMPEROR Tung, Univardisty, Sponge and policy wonk buy them in 87-100% of games.
    - In games not involving us, lock users vs no-lock sides: 50% [41-60] (109 games), residual
      +13.7. The residual carries O2's top-team confound; the raw record is even.
  - **Teleport crystals:** 3.0 a T1 game (mean). EMPEROR Tung uses 2+ by r400 in 92% of games,
    E=mc^2+AI^3 63%. T2 crystal users beat our v33-v36 in 46% of games, other T2 sides 54% (26 vs
    162).
  - **Wards** by r400, median: T1 42, T2 40, T3 68, us 158.
Evidence: `items.py`, `catalog.py`.
  - Coverage: 1,347 sides. Lock and ward placements are not in the summaries.
Rests on: rules (lock: own team only, removed by 5 enemy basic attacks; crystal: 3-tile teleport
  after a 3-turn channel).
Status: measured (purchases only).
Recheck: consumable price or rule changes.
Consequence:
  - Locks are optional for a top strategy; the #1-#3 teams win without them.
  - Answers must still cover a locked altar (O1).
  - Crystals are worth testing only on wall maps (main's D74 and catalog).

### O7. Games are long: a top game is won at about round 1,100, so r400 is a third of it
Claim:
  - **T1-T1 summaries:** all 69 games reached 1,000 banked gems, at median r1085 [IQR 919-1293].
  - **Other games:** T2 r1206, T3 r1135, T4 r1298, T5 r1504. The share reaching 1,000 falls from
    78-97% (T2-T4) to 42% (T5).
  - **Global window:** 92-95% of games end by gems in rank bands 1-150, 75% in 151-400 and 38% at
    401+, where 51% go to the gem tiebreak at r2000.
  - **By map class** (games not involving us, median end): gemrich r942, touch r1051, far r1194,
    mid r1299.
  - **Kills per T1-T1 game** (both teams): 0.7 by r100, 1.7 by r200, 3.7 by r400, 10.4 by r1000,
    13.9 in all.
Evidence: `econ.py`, `fights.py`.
  - Coverage: 225 games not involving us (69 T1-T1), the 1,922-game window, and 901 games involving
    us (us vs T1 r1105 median).
Rests on: rules (1,000 banked gems wins; tiebreak at r2000 by banked gems, then earned gold, then
  kills).
Status: measured.
Recheck: winning_gems or max_rounds changes.
Consequence:
  - Judge a plan over the whole game, not the first 400 rounds; main's topteams.md covers r1-400
    only, about 35-40% of a top game.
  - Against low tiers, the r2000 tiebreak decides half the games.

### O8. Leads compound, and the r400 gold leader usually wins
Claim: median earned gold, winner / loser:

  | round | games not involving us (225) | T1-T1 (69) |
  | --- | --- | --- |
  | r100 | 280 / 233 | 360 / 323 |
  | r200 | 690 / 489 | 804 / 679 |
  | r400 | 1620 / 1065 | 1903 / 1505 |
  | r1000 | 5199 / 3041 | 6828 / 4036 |

  - The winner/loser ratio grows from 1.20 to 1.71 over r100-r1000.
  - **Who leads:** the eventual winner led in earned gold in 60% of games at r100, 72% at r200, 78%
    at r400 and 83% at r600. T1-T1: 60%, 74%, 80%, 89%.
  - **Comebacks:** the side behind in gold won from r200 in 28% of games (T1-T1 26%), from r400 in
    22% (20%), and from r600 in 18% (11%).
  - **Spending:** winners had spent 1,375 gold by r400, losers 870.
  - **Banked gems** lag gold as a predictor: the winner leads 66% at r300, 70% at r400 and 80% at
    r600.
Evidence: `econ.py`, `extra.py`.
  - Coverage: 225 games not involving us (69 T1-T1); 901 games involving us give the same picture.
    Against us, the gold leader at r400 won 80%.
Rests on: O5 (items bought early), O2.
Status: measured.
Recheck: patches to item growth or camp yields.
Consequence:
  - Early efficiency matters because it compounds. Gold → items → faster clears and fewer deaths
    → more gold.
  - About a fifth of games are still won from behind at r400, usually through the other currency
    (O10).

### O9. Gold beats kills when they disagree; a kill counts through what it moves
Claim:
  - **Conflicts:** when one side leads in earned gold and the other in kills, the gold leader won:

    | games | r200 | r400 | r600 |
    | --- | --- | --- | --- |
    | not involving us | 58% (19) | 74% [57-86] (31) | 78% (36) |
    | T1-T1 | 62% (8) | 75% (8) | 83% (12) |
    | against us | 62% (66) | 69% [60-77] (114) | 72% (134) |
  - **Kill differential at r400** (games not involving us): -3 or worse 16% (25 games); -1 34%
    (29); 0 49% (72); +1 68% (25); +3 or better 81% (36).
  - **Kill gold:** 13-16% of other teams' earned gold by r400; 8% of ours.
  - **Whole-game kills mostly reflect a lead, not its cause.** The winner had more kills in the
    game in 77% of T1-T1 games ("fight-decided"), and fewer in 14% ("economy-decided").
    - By map class (games not involving us), fight- / economy-decided: touch 73/18%, gemrich
      70/20%, mid 74/20%, far 47/27% (27% even).
    - Most kills come late: T1-T1 games have only 3.7 kills by r400, 13.9 in all (O7).
Evidence: `extra.py`, `fights.py`.
  - Coverage: 222 games not involving us that reached r400, 69 T1-T1 games, 900 games involving us.
Rests on: G2 (a kill moves 100 gold, the victim's bag and its time); O8.
Status: measured.
Recheck: kill_gold, respawn_time or bag-transfer changes.
Consequence:
  - Value a fight by its economic yield against the farming it costs.
  - A kill lead with a gold deficit is the losing side three times in four.
  - Supports G2. A count of kills is not a goal.

### O10. The map decides the currency: gems on gem-rich maps, gold elsewhere, the jackpot where gems are concentrated
Claim: at r400, in games where the earned-gold leader and the banked-gem leader differ, the gold
  leader won:

  | map class | conflicts | gold leader won |
  | --- | --- | --- |
  | mid | 159 | 75% |
  | far | 77 | 56% |
  | touch | 55 | 51% |
  | gemrich | 36 | 33% |

  - The gemrich losses by map: 00lost_world 3 of 14, 01the_farm 5 of 15, 00great_wall 4 of 7.
  - The plain r400 gold leader wins 85% on mid maps and 64% on gemrich maps. The gem leader wins
    72% on mid maps and 79% on gemrich maps.
  - **Examples:**
    - 8f055441, 00lost_world: Univardisty banked 462 by r200 and 784 by r400 with 354 gold, and
      beat procless (2,266 gold, 280 banked) at r536.
    - 937057a5: prism best girl's Prism carried 710 gems and won 1472-206 at r720 against
      procless.
    - b4b7450c, 00contest: IF banked exactly five monolith captures of 200 gems (r774, r1173,
      r1310, r1460, r1746) and beat procless 1000-200.
  - **Jackpot maps** (main's ecosystem catalog): the side with more captures of a group holding
    ≥ 40% of the map's gem rate won 31 of 36 sampled games.
Evidence: `extra.py` (section "currency"; per-map counts in `data/online/extra.json`).
  - Coverage: 1,124 games that reached r400 (all sources); the examples are from summaries.
Rests on: O7 (gem-rich races end at r942 median); maps.md for real classes; E (gem supply per
  map).
Status: measured.
Recheck: map pool changes (01concentrated_wealth was new on 22 Oct), and any change to camp yields.
Consequence:
  - Value gems and gold per map, from supply and expected race length.
  - A fixed gem/gold exchange rate is exploitable by any opponent, whatever its strategy.
  - A jackpot is income plus denial (G1).

### O11. Banking: small frequent bags at the top; a big bag is a bounty unless its carrier cannot die
Claim:
  - **Bag sizes** (tribute size; median, p75, p90; share of banked gems in bags ≥ 50):

    | tier | median | p75 | p90 | gems in bags ≥ 50 |
    | --- | --- | --- | --- | --- |
    | T1 | 24 | 44 | 66 | 45% |
    | T2 | 22 | 35 | 52 | 32% |
    | T3 | 22 | 33 | 52 | 32% |
    | T4 | 19 | 37 | 58 | 38% |
    | T5 | 12 | 22 | 39 | 26% |
    | us | 22 | 35 | 44 | 26% |
  - **Volume:** procless makes 29 tributes a game, IF 33.
  - **Carrying:** T1 teams carry a median 30 gems at r300 (us 17).
  - **Losses** to champion kills, mean per game: T1 65 gems, T2 37, T3 180 (big-bag hoarders), us
    89.
  - **Big bags against us:** T2 sides with big bags (median ≥ 40 by r400) beat our v33-v36 in 32%
    of games, other T2 sides 59% (28 vs 142).
  - **The exception (O17):** prism best girl's Prism carries a median maximum of 220 gems and banks
    single bags of 239-710, with 4 Prism deaths in 8 games.
Evidence: `econ.py`, `profiles.py`.
  - Coverage: 37,365 tributes across 1,347 other-team sides; 22,459 tributes on 901 of our sides.
Rests on: rules (a tribute takes ceil(gems/10) turns and is broken by stun, death or lock, not by
  damage; a death hands the bag to the killer; Prism gains 1.5 Focus per carried gem).
Status: measured.
Recheck: tribute-time or bag-transfer changes.
Consequence:
  - A bag's right size falls as death risk rises (O13).
  - Carriers are bounty targets for any opponent (G2).
  - Only a carrier that will not die should hold a big bag.

### O12. Contact starts mid-map at about r60-75, and almost never early at the top
Claim: first champion damage in the game, median [IQR]:

  | games | first damage | first kill |
  | --- | --- | --- |
  | T1-T1 | r73 [34-170] | r125 [79-295] |
  | against us | r56-87 | |

  - **By map class** (games not involving us): touch r29 [2-79], mid r61 [35-135], gemrich r160
    [25-270], far r255 [140-488].
  - **Place:** a median 0.47-0.50 of the way from one spawn to the other, per map class (931
    tracked games; `extra.py`, section "contact_place").
  - **Seekers are rare:** 5-10% of sides deal their first champion damage before half the map's
    median time (O1).
  - **T1-T1 kills:** 0.7 by r100 and 1.7 by r200.
Evidence: `fights.py`, `trackfeat.py`.
  - Coverage: 225 games not involving us, 901 games involving us, 1,000 tracked games.
Rests on: rules (equal movement speed; symmetric maps).
Status: measured.
Recheck: a speed-changing item or patch.
Consequence:
  - The opening (until about r60 on mid maps) is contact-free farming. On touch maps contact
    starts at about r29, and on far maps after r250.
  - Plans should be timed by map class.

### O13. Fight survival separates the tiers: top champions get out of most fights; ours die in 30%
Claim: a contact episode is a champion with an enemy within 6 that closes to ≤ 3, ending at death,
  a recall, or 3 rounds with no enemy within 6. 87-90% of episodes involve champion damage
  ("fights").
  - **The champion dies in:** T1 13%, T2 12%, T3 19%, T4 24%, T5 52%, us 30%.
  - **By matchup** (death % / separation %):

    | champion | vs T1 | vs us |
    | --- | --- | --- |
    | T1 | 21 / 68 (3,463 fights) | 9 / 80 (6,287) |
    | T2 | 21 / 46 (137) | 12 / 77 (13,155) |
    | T3 | | 18 / 70 (5,625) |
    | T4 | | 25 / 58 (1,851) |

    Our champions die in 32% of fights against T1, 31% against T2, 28% against T3 and 21% against
    T4.
  - **How T1 fights end:** 75% separation, 10% recall, 13% death. The median fight lasts 22 rounds.
  - **Risk by length:** ≤ 5 rounds 39%, 6-10 31%, 11-20 24%, more than 20 18-19%.
  - **Risk by damage ratio** (taken / dealt within the episode): < 0.5 2%, 0.5-1 13%, 1-2 32%, 2-4
    48%, ≥ 4 37% (this last group includes victims caught without dealing damage).
Evidence: `deathctx.py`, `deathtab.py`, `data/online/deaths.json`.
  - Coverage: 70,605 episodes in 1,000 tracked games.
Rests on: G3 (sticky contact), O14.
Status: measured. Episode boundaries are a proxy built from turn-start positions.
Recheck: movement, dash, stun or recall changes.
Consequence:
  - G3 holds for weak teams (us 53% separation), but between top teams most fights end in
    separation.
  - So disengagement is possible and is a skill: start it before the damage ratio passes about 1,
    keep allies close, keep a dash or stun ready.
  - Deaths per fight is an opponent-independent fight measure worth calibrating locally.

### O14. How champions die: long attrition from full health, isolation, and late spawn sieges, rarely ambushes
Claim: death classes, taking the first that applies:
  - **camp:** monster damage ≥ 25% of max HP in the last 10 rounds;
  - **home:** within 6 of own spawn;
  - **burst:** ≤ 5 rounds of contact from ≥ 70% HP;
  - **chase:** walked ≥ 3 toward own spawn with an enemy within 2 in half the contact rounds;
  - **picked:** no ally within 6, 2+ enemies;
  - **outnumbered:** enemies > allies + 1;
  - **duel:** alone against at most one enemy;
  - **brawl:** the rest.

  | victim tier | brawl | picked | home | duel | outnumbered | chase | camp | burst | deaths |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | T1 | 24 | 21 | 16 | 12 | 8 | 8 | 7 | 3 | 1,256 |
  | T2 | 30 | 18 | 10 | 17 | 8 | 7 | 7 | 2 | 1,500 |
  | T3 | 34 | 19 | 6 | 18 | 7 | 4 | 6 | 5 | 1,054 |
  | T4 | 32 | 13 | 5 | 21 | 2 | 8 | 11 | 9 | 523 |
  | T5 | 28 | 12 | 8 | 17 | 7 | 6 | 2 | 18 | 266 |
  | us | 28 | 19 | 21 | 12 | 8 | 4 | 5 | 3 | 8,089 |

  - Victims had an enemy within 6 for a median 18 rounds before dying (T1-T3; T5 9.5), from full
    health (median 0.99-1.00).
  - Enemies within 3 in the last 3 rounds: 1.5-1.7 on average.
  - A killer-team champion stood in a bush within 6 at contact start in 9-15% of deaths.
  - **Home deaths are late sieges.** For T1 victims of T1 killers (174 deaths): median r699,
    contact 18 rounds from full health, no walk home, and 22% within 90 rounds of the same
    champion's previous death. Ours (1,692): r760, 30% (`extra.py`, section "home_deaths").
  - By r400, camps add 12% of T1 deaths and 23% of T4 deaths.
Evidence: `deathctx.py`, `deathtab.py`.
  - Coverage: 12,740 champion deaths in 914 tracked games (deaths of all tiers).
  - Main's collapse analysis (`.cache/main/analysis/collapse/report.md`) classifies our deaths
    differently and finds retreats that never end on trap maps.
Rests on: O13, G4.
Status: measured. The classes are heuristics; tune them before relying on small differences.
Recheck: as O13.
Consequence:
  - The avoidable deaths are isolation against two or more and long losing contacts.
  - Late base sieges punish a team that is behind and has no safe exit.
  - Bursts and bush ambushes are a small share of deaths at T1-T3 (but 18% at T5).

### O15. Cohesion does not decide top games; grouping works only when the comp converts it into burst
Claim: "grouped" is the share of champion-rounds r1-300 with 2+ allies within 4. The side more
  grouped by ≥ 0.10 won:
  - 14 of 38 T1-T1 games (37%);
  - 22 of 54 other games not involving us;
  - 229 of 445 games against us.

  The side fighting in larger parties (mean allies within 4 of a damage dealer, by ≥ 0.25) won 16
  of 42 T1-T1 games and 192 of 439 against us.
  - **T1-T1 winners vs losers, medians:** grouped 0.23 vs 0.33, alone 0.45 vs 0.36, mean pairwise
    distance 10.9 vs 9.3 tiles, recalls 4.0 vs 3.0 per champion by r400.
  - **Top teams span the range:**
    - procless: grouped 0.17, alone 0.52, spread 13.5, party 1.36.
    - Intercontinental Fish: grouped 0.49, alone 0.23, spread 7.3, party 2.23; it has the highest
      K/D, 3.5/1.3 by r400 (O18).
Evidence: `trackfeat.py`.
  - Coverage: 49 tracked T1-T1 games, 67 other games not involving us, 863 games involving us.
  - Script: `extra.py` (section "cohesion").
Rests on: G4 (numbers decide local fights); rules (more allies do not speed a capture).
Status: measured. Possibly confounded: losing teams may group defensively.
Recheck: capture-time or reward-sharing changes.
Consequence:
  - G4 holds locally: 19-21% of deaths are isolated champions against two or more (O14).
  - "Always be the larger group" is not supported as a team policy. Grouping costs throughput.
  - Grouping pays when the comp turns proximity into burst kills (IF's Focus Heart, Stellar,
    Ren).

### O16. procless (#1, rating 2189): survival-first solo farming that out-earns everyone and never contests altars
Claim: 86 sides, 30 maps, 22 Oct 00:10-03:52 UTC; 63 wins: 26-17 against T1, 33-5 against us.
  - **Draft** (identical per map):
    - Heart Vesper Ryth Marcella Vendetta on 5-pick maps (62);
    - Heart Vesper Ryth Marcella on 4-pick maps (18);
    - Vesper Heart Vendetta on 3-pick maps (6).
  - **Build:** Treasure/Health > Jungle/Strength > Bruiser/Health on Marcella, Ryth and Vendetta.
    Vesper Treasure/Strength first; Heart Treasure/Health > Jungle/Focus (40% Health, 60% Focus
    over the game).
    - First purchases: Vesper r67, Ryth r89.5, Heart r94, Marcella r105, Vendetta r120.5.
  - **Opening:** first clear r30, first capture r36, 4.0 captures by r100 and 9.3 by r200. The
    first five captures are wolves 40%, gold 27%, elks 23%, gems 10%.
  - **Economy** (own / opponent median earned gold): r200 760/677, r400 1956/1410, r1000
    6143/4394. Banked at r400: 148/114.
  - **Wins** by gems at r979 median [874-1106].
  - **Banking:** 29 tributes a game, bag median 22, max carry about 55-60 per champion, 0 locks in
    86 games.
  - **Fights:** own first champion damage r72; K/D 1.7/1.4 by r400 and 6.8/5.2 per game; camp
    deaths 0.7. Killers: Heart 180, Vendetta 134, Vesper 107, Marcella 106, Ryth 59.
  - **Movement:** alone 52% of champion-rounds r1-300, spread 13.5 tiles, 4.4 recalls per champion
    by r400.
  - **Typical win:** 9bddce86, 01islands vs Zarzorki, r959.
    - Ryth earned 568 by r300 and completed Treasure/Health at r90.
    - Tributes of about 30 began at r95; only 3 kills by r683.
    - Gold 811/680 at r200 and 1848/1112 at r400; won 1005-600.
  - **Zero-kill win:** 606e89ab, 01fracture vs prism best girl. Gold 1988/1096 at r400 and no kill
    until r765; it banked late in bags of 71-86 and won 1033-477.
  - **Losses:** 12 of 17 against T1 were behind in gold at r400. They come in three structural
    ways:
    - **gem race:** 8f055441, Univardisty, 00lost_world, r536;
    - **jackpot:** b4b7450c, IF, 00contest monolith;
    - **battery:** 937057a5, prism best girl, 00lost_world.

    It was also out-farmed on 00ambush and 00swirl (gold at r400 699/2203 and 878/2301). Against us
    it lost three times on 01starvation, where its r400 gold was 242-522 against our 624-996.
Evidence: `profiles.py`, `timeline.py`, `data/online/profiles.json`. Coverage as stated, 74
  tracked.
Rests on: O2, O4, O5, O10, O13.
Status: measured.
Recheck: procless resubmits often; recheck after each sprint (O19).
Consequence:
  - **To imitate:** maximise farm rate per alive round and survival; Health shards; shop small and
    early; recall often; avoid fights without economic yield; skip locks.
  - **To counter:** win the currency procless undervalues. Race gems on gem-rich maps, own the
    jackpot, or carry gems safely; and starve it on sparse maps (01starvation).

### O17. prism best girl (#2, rating 2165, only 53 ladder matches): the gem-carrying Prism battery
Claim: 8 sides, 8 maps, 22 Oct 02:42-03:20 UTC; 6 wins: 2-1 against procless, 4-1 against us.
  - **Draft:** Prism first in every game, then Vesper/Marcella with Ryth, Heart or Vendetta.
  - **Build:** Prism 100% Health: Durability/Health > Jungle/Health > Home/Health, first buy at
    r322 median. Heart Power/Health > Durability/Health; Vesper Power/Strength > Durability/Health.
    Late shoppers overall (Marcella r151, Ryth r418).
  - **Economy:** behind on gold early (r100 214/279, r300 974/1084) but ahead on banked gems (r300
    109/42, r400 175/102).
  - **Battery:** the Prism carries a median maximum of 220 gems and banks bags of 239 (f30735b2,
    00elkring, r655), 336 (690d0f86, 00lanes, r1031), 331 (9ff7d05a, 00rushbait_v2, r1060) and 710
    (937057a5, 00lost_world, r720).
  - **Fights:** K/D 2.1/0.5 by r400 and 10.1/1.9 per game. Killers: Vesper 25, Prism 22, Marcella
    15. Prism deaths: 4 in 8 games.
  - **Battery example:** 937057a5 vs procless, 00lost_world.
    - From r37 the team took a gem cluster every 3-17 rounds (10 by r117), and the Prism kept
      its gems (maximum carried 710).
    - It made 14 kills with 0 deaths and won 1472-206 at r720 with one 710-gem tribute.
    - procless had 1292 vs 1086 gold at r400.
  - **Losses:**
    - 606e89ab, 01fracture vs procless: the battery never formed (Prism ≤ 59 carried) and procless
      out-farmed it 1988/1096 by r400.
    - 921a5f68, 01starvation vs us: r2000 tiebreak, 550-864.
Evidence: `profiles.py`, `timeline.py`.
  - Coverage: only 8 sides, 6 tracked; main's catalog adds 937057a5, 9ff7d05a and 606e89ab. This
    is the thinnest profile (O19).
Rests on: O11; rules (Prism +1.5 Focus per carried gem; damage does not break a tribute).
Status: measured on a small sample.
Recheck: as soon as more of its games are fetched (request in O19). The 53 matches suggest a new
  team or a fresh account.
Consequence:
  - The battery is a genuine strategy, not a fluke: its value grows with the bag.
  - Every contender needs an answer for a carrier it cannot kill: lock the target altar, stun the
    channel, or out-race the banked total before the single tribute lands.

### O18. Intercontinental Fish (#3, rating 2160): grouped Focus burst with the highest kill rate at the top
Claim: 31 sides, 21 maps, 22 Oct 00:20-03:53 UTC; 22 wins: 9-7 against T1, 13-2 against us.
  - **Draft:** Stellar Heart Ren C&I Ryth (5 picks, 18 games), Stellar Heart Ren (3 picks, 9),
    Stellar Heart Ren C&I (4).
  - **Build:** Heart and Stellar Treasure/Focus > Power/Focus > Home/Health (82% Focus); Ren
    Bruiser/Health > Durability/Health > Power/Focus; Ryth Bruiser/Health; Cassi Power/Focus;
    Issac Treasure/Strength.
    - Heart's first emblem at r101; first purchases Stellar r81, Heart r93.
  - **Opening:** first clear r20, first capture r27, 3.7 captures by r100. The first five are
    wolves and elks 61%.
  - **Economy:** r200 788/699, r400 1989/1542, r1000 7061/4699. Wins at r1066 median; 33 tributes
    a game, bag median 22, 0 locks.
  - **Fights:** K/D 3.5/1.3 by r400 and 12.7/4.0 per game. Killers: Heart 157, Stellar 116, Ren 57.
    Its dead are mostly C&I (46 each, as they die together) and Stellar 33.
  - **Movement:** grouped 49%, spread 7.3 tiles, party size 2.23 when dealing damage.
  - **Win example:** b4b7450c, 00contest vs procless.
    - Gold 3449/1474 and K-D 12-1 by r400.
    - The only gem source was the monolith, and IF banked it five times at 200 gems.
  - **Losses against T1** come in two ways:
    - **short gem races:** a04d9a24 00gem_fields vs procless lost at r675 despite K/D 7-3;
      ddd86e8b 00quadrants vs :3 at r493; 2468dd8b 01shortcut vs :3, 30-1010;
    - **out-fought or out-farmed:** 227a63d8 00corners vs Univardisty, K-D 1-11 by r400;
      4d868e33 01broken_curve vs procless, 1996/2621 gold, 0 kills.
Evidence: `profiles.py`, `timeline.py`. Coverage as stated, 29 tracked.
Rests on: O9, O10, O15; rules (Heart's dash and shield scale with Focus; Stellar's stun).
Status: measured.
Recheck: as O16.
Consequence:
  - Grouped burst works when kills convert into gold and denial. It fails when the race is shorter
    than its kill tempo.
  - **To counter it:** race gems on touch and gem-rich maps, and never feed it isolated champions.
  - **To imitate it:** a comp whose damage needs proximity, and Focus on the casters.

### O19. What this data cannot show
Claim: these limits apply to every entry; the numbers are only as general as this list allows.
  1. **Selection.** 80% of the summarised games involve us, a rank-36 team.
     - Opponents' behaviour against us is behaviour against a weaker team. T1 champions die in 9%
       of fights against us but 21% against T1 (O13); K/D and gold leads are inflated the same
       way.
     - The non-us sample is small (225 games): 147 T1 sides from top-vs-top games, and T3-T5 from
       main's stratified sample.
     - Half of those strata were chosen for the most unusual draft, which overstates rare drafts
       in T3-T5.
     - Low tiers have few tracked sides (T5: 13).
  2. **Hidden bots.** Teams can hold a stronger or different bot until a sprint (lock 24 Oct). The
     ladder is a sample of today's play.
     - prism best girl's 53 matches suggest a new account; its profile rests on 8 sides.
     - Every profile is at most 19 hours of one team's history.
  3. **Engine and time.**
     - All games are on engine 1.0.2, from 21 Oct 09:01 to 22 Oct 04:11 UTC.
     - Teams resubmitted during the period: drafts changed for prism best girl and Sponge (O3).
     - The rating snapshot (22 Oct 03:2x) postdates many games, so rating-adjusted residuals for
       earlier games are noisier (fit 0.65 vs actual 0.56 at gap 0-25 for other-team games).
  4. **Inference.**
     - Residuals and with/without comparisons are associations. Many features (gold, kills, bags,
       grouping) are partly consequences of being ahead; "fight-decided" in particular counts late
       snowball kills.
     - Most flag samples are 10-60 sides; intervals are given where a claim leans on a rate.
  5. **Measurement.**
     - Tracks are sampled at each champion's turn start; monsters and vision are not tracked.
     - Lock and ward placements, ability casts and damage targets are not in the summaries.
     - Death and episode classes are heuristics. Map classes are this page's own, not maps.md's.
  6. **Our own versions.** Our side spans v1-v36. Comparisons "against our v33-v36" pool four
     releases that differ in fight logic.
  7. **Duplicates.** Repeated groups replay identical games; 68 duplicates were removed. Main's
     paired comparisons keep them on purpose.
Evidence: coverage counts from `catalog.py`, `extra.py` and `sides.py`.
Rests on: Coverage section.
Status: standing caveat.
Recheck: when new data arrives. The most useful fetches, all for the main agent (we have no
  server access):
  - (a) prism best girl's (team 38e6d8c4-1608-44f5-956b-08390f564336) last 100 ranked games,
    summaries and replays;
  - (b) top-20 ranked games between top-20 teams after the Sprint 2 lock (from 25 Oct), to detect
    hidden bots;
  - (c) replays for the summarised games between other teams that have none locally (the match
    ids are in `data/online/fetch_requests.txt`), so cohesion and fights can be measured at T3-T5
    without us;
  - (d) a second global window and ladder snapshot at a different hour and after the sprint;
  - (e) the ladder rank at game time for survey groups (already in the registry: `created.rank`),
    plus a rating snapshot per fetch.
Consequence: prefer the opponent-independent rates (O2, O13) and the structural cuts (O8-O10) over
  style records (O1) and profiles (O16-O18) when they disagree.

### O20. Against main's analyses: what this confirms and what it contradicts
Claim: compared with `.cache/main/analysis/topteams.md` (23 procless and 46 top-8 sides, r1-400)
  and the ecosystem catalog (108 teams):
  - **Confirmed on larger samples:**
    - Health-first builds: 66% of T1 sides; procless Treasure/Health first on 4 champions in 86
      games.
    - Top Hearts buy no Strength: 1% of T1 Heart shards.
    - The top shops earlier: first purchase T1 r112 vs us r132.
    - Vesper over Varg in the top 20: 78% vs 10%.
    - procless buys no locks: 0 in 86 games.
    - First contact is mid-map: 0.47-0.50 of the spawn distance.
    - Deaths follow long contact rather than ambush: median 18 rounds; bursts 2-5% of T1-T3
      deaths.
  - **Contradicted or qualified:**
    - **The "S/L" split** in topteams.md is the 00/01 name prefix, not map size; 00ambush is
      100x100. Its small/large rows should be read as prefix groups.
    - **Grouping** is not a winning pattern at the top: the more-grouped side won 14 of 38 T1-T1
      games (O15). The "grouped burst brawl" works through IF's Focus comp, not grouping as such.
    - **Kills:** "fight-decided" outcomes are mostly snowball. When gold and kills disagree at
      r400, gold wins about 3 in 4 (O9).
    - **Time frame:** r400 covers about a third of a top game (O7). Leads at r400 decide about 78%
      of games, not all (O8).
    - **Focus fire:** about 1.5-1.7 enemies were within 3 of a victim in its last 3 rounds, below
      topteams.md's 3.0-3.5 kill participants. Both may hold (assists count from further away),
      but local numbers at the moment of death are smaller than the participant counts suggest.
  - **New here:**
    - fight survival by tier and matchup (O13);
    - farm rate per alive round (O2);
    - the map class decides the currency (O10);
    - drafts keyed to map and version, with champion residual ±2.5 (O3);
    - game length and endings (O7);
    - death classes for all tiers (O14);
    - profiles of prism best girl and Intercontinental Fish (O17-O18).
Evidence: as cited in each entry.
Rests on: O2-O18.
Status: measured.
Recheck: when main revises its analyses.
Consequence: when main's online analyses and this page disagree, prefer the larger sample and the
  structural cut, and say which one a decision rests on.
