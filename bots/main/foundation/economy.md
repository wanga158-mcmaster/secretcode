# Economy: from clear arithmetic to the race (engine 1.0.2)

What a gem costs and how fast a team can bank 1,000 of them: the laws that price a camp (damage,
time, health, trips), the capacity of each map, what gold is worth, how to bank, and how the race is
decided. Owner: economy research (contenders). Entries are laws with the runtime values that drive
them named, so they survive a balance patch when a bot reads those values (catalog fields,
`constants::*`, `camp_stats`, `ability.info()`); the numbers shown are today's.

Engine facts come from mechanics.md (M-entries, probed on lab maps). The economy probes
(`bots/contenders/_econ-probe/`, knobs in `src/knobs.h`, passive opponent = the same bot at default
knobs) reproduce the ones the arithmetic rests on, on pool maps: runs and replays in
`.cache/contenders/economy/probes/<run>/`, read with `tools/economy/probe_log.py RUN` and
`probe_damage.py RUN`; `tools/economy/calibrate.py` compares them with the simulator.

E1-E28 retain their historical engine/calibration boundaries. E29-E30 are current-source model
algebra and pure geometric counterexamples; they launch no native games and establish no bot gain.
E31 separately verifies one complete native priority-rotation lab cycle on engine 1.0.3.

Model and data (all in `tools/economy/` and `data/economy/`, regenerate in this order):
- `econ.py`: catalog loader, float32 hit counter, the round-by-round clear simulator.
- `clear_table.py` -> `clear_table.json` (every kit and canonical teams x common compositions, base
  and +1 shard); `items.py` -> `items.json` (marginal value of shards and emblems).
- `speedrun.py` -> `speedrun_{full,half}_{none,spawn,instant}.json`; `capacity.py` ->
  `capacity.json` and the `capacity` field of `data/maps.json`; `items_order.py` -> `items_order.json`.
- `race_online.py` -> `race_online.json` (read-only pass over `results/online/` summaries).
- `run_siege.py` plays the coordinated siege probe `bots/contenders/_econ-siege` on the lab map
  `data/economy/maps/econ-siege.{json,map26a}` (built with `tools/mechanics/mapgen.mjs`);
  `siege_compare.py` gives the model side (E8, E9, E28).

| Id | Claim | Status |
| --- | --- | --- |
| E1 | Incoming camp damage is geometric and exact; a clear costs D = W/d, Smith-weighted health over team damage rate | verified (per-round law), derived (D) |
| E2 | Camp damage rate of every kit, and what shards add | verified (per hit), derived (rates) |
| E3 | The capture cycle: clear, six rounds on a tile, then the slowest member's respawn | verified (engine 1.0.2) |
| E4 | A heal trip costs the retreat, recall_time + 1 and the walk back; cover makes the retreat free | verified (pieces), derived (cost) |
| E5 | The clear simulator reproduces solo probe clears within 2 rounds | measured (calibration) |
| E6 | Solo at base: every kit clears wolf content alone; nobody clears an elk without two or more trips | model (clear table) |
| E7 | One Jungle emblem turns a solo elk from 2-3 trips into one for most melee kits | model; verified for one kit |
| E8 | Party-size law: the best party grows with the square root of a group's damage per trip cost | model; verified in the engine on elk and golem content |
| E9 | Monolith sieges need a coordinated team and cover; at base they cost ~220 rounds and a death, with Jungle ~80 | verified in the engine (lab map) |
| E10 | Kit levers on camps: Varg's splash, Heart's dash and shield, Vendetta's chains, range-4 chip | verified (mechanisms), model (rates) |
| E11 | Gems per champion-round by content and team: clusters > single elks in pairs > wolves solo | model |
| E12 | Supply floor: the respawn-limited minimum race is 284 rounds on the pool mean | computed |
| E13 | Feasible speedrun schedules: 1,054 rounds at base stats, 674 with items, 784 on the own half | model (upper bound) |
| E14 | What binds: health on damage maps, walking on long maps, respawn on three, capture waits on two | model |
| E15 | Main's speedrun is 320 rounds slower than the schedule; the solo-farming rule is the structural cause | model + measured |
| E16 | Where main's economy model and this one disagree | analysis |
| E17 | Gold buys nothing but items; its price is the trip that spends it | derived |
| E18 | The first emblem is worth 25-45% of a solo farmer's cycle; Jungle of the main stat is the best first buy | model |
| E19 | A shop trip pays back within one elk capture or two wolf captures | model |
| E20 | Throughput order: Jungle, then Monster, then Power of the main stat; Treasure first costs ~13 rounds | model (paired schedules) |
| E21 | Tribute costs ceil(bag/10) turns: 0.1 turn per gem in tens | verified (engine 1.0.2) |
| E22 | Bank on contact, not at a bag size: b* = sqrt(2 W rho / (k h)) with h set by contact (C21) | model (online hazards) |
| E23 | Marcella and Prism turn banking into gold and Focus; the capture tile decides who earns | verified (mechanism), derived |
| E24 | Race clock: t(1000) = t0 + 1000 / r; contested games end near r1,100-1,200, 1.6-1.7x the speedrun | measured + derived |
| E25 | One kill moves ~40 gems plus twice the victim's bag | derived |
| E26 | Income leads decide: a 10% rate edge is worth 2-3 bag kills; leaders at r400 win 78-90% | derived + measured |
| E27 | The round-2000 tiebreak is a gem count; lifetime gold has never decided a game | measured |
| E28 | Rallying costs the walk from the rally, the arrival spread and the slowest member's return | measured (engine) |
| E29 | Candidate bounds must dominate the actual state-conditioned score | conditional source/model algebra; no caller/native claim |
| E30 | A bank's travel cost is the extra route through one legal tribute tile | derived graph geometry; no native route claim |
| E31 | Next-round priority rotates a single elk's tank without losing either melee basic | verified bounded native full cycle, engine 1.0.3 |

## Engine laws the arithmetic rests on

### E1. Incoming camp damage is geometric and exact; a clear costs D = W/d, Smith-weighted health over team damage rate
Claim: Each round start, every awake monster m with a champion within `camp.attack_range()` R_m
deals `camp_stats.base_strength` S_m to the nearest champion (Chebyshev; ties to the earliest in the
round's turn order), times (1 - 0.3 Jungle) x (1 - 0.1 Durability) for that champion, after its
shield; a stunned monster skips that strike. Strikes start the round after the first hit (the hit
wakes the whole group). Killing members in Smith order (descending S/H) with a constant damage
rate d, the damage the team absorbs is D = W / d, with W = sum over members of S_m x (health of m
plus every member killed before it): wolf1 W = 27,000; gold2 wolf2 81,000; elk1 (with clusters)
144,000; elk2 gold2 432,000; golem 600,000; monolith 4,000,000. Discretisation (float32 hits,
whole rounds, the first round free) moves it by at most one round of incoming.
Evidence: M5-M8, M26, M30, M40. Economy probes (pool maps, side A): p1 two wolves 40 a round on
Vendetta, 20 after one died (00packs g_69); y1 Jungle 20 -> 14, y3 Jungle + Durability 20 -> 12.6;
y9 and k10 a stunned wolf skipped its strike (40 -> 20); p4 a ranged Vesper at 3 took nothing while
Vendetta stood adjacent; e2/p5 an elk at equal distance struck the earlier drafted champion; k5
Varg's 250 shield absorbed five rounds of 40. D = W/d is the sum over members of S_m x their
lifetime (cumulative health / d).
Rests on: M5-M8, M26, M30, M40.
Status: verified (per-round law); derived (D = W/d, exact up to discretisation).
Recheck: camp catalog (strength, range), item passive magnitudes.
Consequence: Damage per capture is inversely proportional to the damage rate brought, so a group's
health price falls with every attacker added, while its labour (champion-rounds of fighting,
n x H/(n x delta)) does not change (E8). A bot can forecast a clear's cost exactly before the first
hit: W is a constant per group, d is its own team's rate.

### E2. Camp damage rate of every kit, and what shards add
Claim: A stationary champion's approximate sustained camp damage rate is delta = (1 + Monster bonus)
x [S x (1 + 1/8 with an attack reset) + sum over damaging slots of (modifier x Focus x targets hit) / 8].
Monster's runtime passive magnitude (currently 0.1) applies to both basic and ability damage (M40).
Read cooldowns from `ability.info()` (currently 8). A standing caster can cast and basic in one turn;
a reset permits another basic after the first. Transient cooldowns, kill resets, variable-health
bonuses and movement or target-selection losses require separate accounting. At base:

| kit | S | F | delta, one target | delta, 2x2 group | +1 Strength shard | +1 Focus shard (one target) |
| --- | --- | --- | --- | --- | --- | --- |
| Vendetta | 70 | 100 | 78.8 (more with kill resets) | 78.8 | +6.8 | 0 |
| Tavi | 45 | 110 | 69.9 | 69.9 | +5.6 | +1.8 |
| Marcella | 45 | 60 | 65.6 | 65.6 | +5.6 | +3.8 |
| Ryth | 45 | 100 | 63.1 | 100.6 (area stun) | +5.6 | +1.3 (x targets) |
| Vesper | 55 | 100 | 61.9 | 61.9 | +5.6 | 0 |
| Willow | 55 | 120 | 61.9 | 61.9 | +3.4 | 0 |
| Prism | 35 | 80 | 60.0 | 90.0 | +2.5 | +2.5 (x targets on the area) |
| Stellar | 30 | 70 | 59.8 | 96.5 | +3.5 | +3.0 |
| Heart | 30 | 100 | 48.8 (x up to 1.5 when hurt) | 105.0 (dash hits all four) | +3.5 | +2.3 (x targets) |
| Cassi | 30 | 70 | 47.5 | 47.5 | +3.0 | +3.3 |
| Varg | 40 | 100 | 45.0 | 180.0 (splash) | +5.6 (x 1 + neighbours) | 0 (shield +25 per 8 rounds) |
| Issac | 40 | 100 | 45.0 | 45.0 | +3.4 | 0 |
| Ren | 35 | 100 | 44.4 (+0.2 F per strike taken) | 44.4 | +5.0 | +0.8 (+1.8 retaliation per strike) |

Hits are counted by float32 repeated subtraction (`econ.hits_to_kill`): a wolf (1,350) takes 20
Vendetta hits, 25 Vesper, 30 Ryth/Marcella/Tavi, 45 Heart; an elk (2,400) 35, 44, 54, 80; with the
Monster emblem on 100 Strength, 13 hits per wolf.
Evidence: M23-M25, M29, M32-M35, M39, M43; catalog payloads. Economy probes: per-hit values for all 13
kits (p1-p4, y-, k-series: Vendetta 70, Vesper 55, Ryth/Marcella/Tavi 45, Heart 30 x (1 + 0.5
missing), Stellar 140 + 98 area, Cassi 140, Marcella 120, Tavi 154, Prism 120 + 80 area, Ryth 100
area, Ren 75 and 20 per retaliation, Heart's dash 273.48 = 1.5 x 172 x 1.06 to both wolves, Varg's
splash 70 to every member around the target); Monster emblem 100 -> 110 (y4); Power emblem = 6
shards (k5); Vendetta's resets every turn while each turn kills a cluster (p2 r39-r41).
Rests on: M23-M26, M29, M32-M35, M39, M40, M43.
Correction: the original prose put Monster only around the basic term. M40's historical Stellar
140 -> 154 spell witness and `econ.py`'s existing `out_mult` application agree that it covers the
spell term too. This corrects the prose, not the retained base table or the historical item simulator.
Ren's retaliation path lacks that multiplier in the simulator and was not covered by M40; its
Monster interaction remains an uncertainty rather than a silently repaired calibration.
Status: verified (per hit and per cast); derived (rates: sustained, in place, ignoring the first
round's missing reset).
Recheck: any champion patch (base stats, growth, modifiers, cooldown); read `ability.info()`.
Consequence: Single-target farming speed is Strength x reset; clustered content multiplies splash,
area and dash kits by the number of members in radius 1 (Varg x4 on a 2x2 group). A Strength shard
adds 3-9% of delta for every kit; Focus is worth more than Strength only for area/dash kits on
clustered groups (Heart, Stellar, Prism, Ryth) and for Marcella's and Cassi's single spells.

### E3. The capture cycle: clear, six rounds on a tile, then the slowest member's respawn
Claim: Stepping on a member tile of a dead group in round k pays at the start of round k+6
(`capture_time` + 1); a melee killer steps on in the kill turn, a ranged killer one or more rounds
later. The payout is the members' summed reward, split equally among the team's champions on tiles,
scaled by each recipient's gold bonuses (fractional; the SDK reports the floor); the group respawns
`max_respawn_timer()` rounds after the payout once every member tile is empty. A dedicated holder's
cycle is clear + 6 + cooldown, so a group yields at most gems / (cooldown + 6 + clear) a round.
Evidence: M13-M16, M37. Economy probes: p2 last kill r57 on a tile -> 120 gold 8 gems at r63, timer 50
at r63, alive r113; p4 Vesper on the tile r40 -> 159 = floor(120 x 1.33) at r46; k14 600 -> 798
(x1.33), 120 -> 183 (x1.53 with Treasure); k15 two capturers -> 60 gold + 4 gems each.
Rests on: M13-M16, M37, P2.
Status: verified (engine 1.0.2).
Recheck: `capture_time`, respawn values, gold bonus magnitudes.
Consequence: Six rounds of stillness are a fixed tax per capture: two thirds or more of the time on
site for a cluster group (1-3 rounds to clear), 7-17% for a solo elk. Tiny groups are capture-bound,
not clear-bound (maps.md P4); one capturer per group, and the champion with the best gold bonus
(Vesper) or the banker (Marcella, E23) should stand on the tile.

### E4. A heal trip costs the retreat, recall_time + 1 and the walk back; cover makes the retreat free
Claim: A champion whose health runs out of budget leaves the fight, recalls and walks back: the trip
costs T_trip = r + `recall_time` (5; 3 with Home) + 1 (the arrival turn heals to full, M18) + the walk
from spawn back to the group. The retreat r is free when another champion stays nearer to every
striking monster: one step back hands the strikes over (E1) and the recall can start inside the
monsters' range. Uncovered, a melee tank leaving from distance 1 takes R - 1 more strikes on the way
out (wolf 2, elk and golem 3, monolith 4 = 800 health), so it must keep incoming x R in reserve; a
lone base champion cannot tank a monolith even once and leave alive.
Units and timing: `spawn_healing` is a fraction of maximum HP per owner turn end, not HP per round
(current SDK lines 124-126; current catalog 1.0 means 100% of maximum HP). Recall arrives at owner
start, but the champion must remain on own spawn through its end to heal. At a smaller positive
fraction f, ceil(1/f) owner ends is a conservative full-pool healing proxy; missing-HP pricing is a
separate model choice. The current rule/SDK agreement is not a new native timing revalidation.
Evidence: M17, M18, M41. Probes: p2 recall at r28 -> arrived r33 at 300/1000 -> full and moving r34;
k16 Home: r100 -> r103; e2 R23: Vendetta stepped back to distance 2, took no damage while Marcella at
distance 1 took the elk's 60; p2/p3 retreats took 2-4 rounds under fire; v2 (01yellow_brick_road):
a lone Ryth leaving an elk at 8% health (96) died on the way out, as the law predicts (3 x 60 = 180).
Rests on: M5, M6, M12, M17, M18, M41.
Status: verified (pieces); derived (the trip cost).
Recheck: `recall_time`, `spawn_healing`, Home magnitude.
Consequence: Health converts into time at T_trip per health pool. The usable pool per trip is
max health minus the exit reserve (wolf 60, elk 240 for a lone melee tank), so recalling at 50%
(main, D61) wastes about a third (elk) to a half (wolves) of each pool; pairs and parties that
rotate the tank with cover spend their whole pools. Two limits: a covered champion that recalls
inside the reach is safe only while the cover lasts the whole channel (E9: recalls cancelled and 4
deaths when waves ended early), so walk out under cover first; and the cover argument is exact only
against monsters, which strike the nearest body (M5). Enemy champions choose their own target, so
cover protects against them only if they prefer the nearer one; online, allies within 6 cut the
in-contact death hazard about 2.4-fold (C21), which is all the data shows.

### E5. The clear simulator reproduces solo probe clears within 2 rounds
Claim: `econ.simulate_clear` (E1-E4 as code: tank = nearest engaged melee, else the earliest ranged;
Smith kill order; casts and resets on cooldown; float32 hits; retreats just in time; siege waves
with cover) matches the probes where its assumptions hold: Vendetta gold2 wolf2 51 vs 51 rounds
(1,080 vs 1,060 damage), Vendetta elk group 63 vs 64 (2,280 vs 2,100), Ryth elk group 68 vs 70
(2,400 vs 2,580), Varg elk group 68 vs 83 (1,740 vs 1,800); Vendetta with Jungle of Health on an elk
group 32 vs 31 rounds, one trip both (v4). Team probes run longer in the engine (e2 33 vs 18, e3 79 vs
33, v3 Marcella+Ryth 24 vs 18 rounds with 1,020 vs 720 damage and no recall in either) because the
probe staggers arrivals and (e2, e3) retreats at 35%, which the simulator does not; the monolith
probes (m1, m2) wiped repeatedly without coordination (863 and 563 rounds), where the simulator's
covered waves need 277 and 151.
Evidence: `tools/economy/calibrate.py` over 16 probe runs plus v2-v4 (00packs, 00elkring,
01yellow_brick_road, 01broken_curve, 00lost_world; side A; one game each, deterministic).
Rests on: E1-E4.
Status: measured (solo agreement within 2 rounds and ~10% damage; team and siege numbers are model
outputs, not engine measurements).
Recheck: rerun `calibrate.py` after any change to econ.py or a patch.
Consequence: Solo clear arithmetic can be trusted for decisions; party and siege numbers assume
simultaneous arrival and covered rotation, which a bot must execute to get them.

## Clear arithmetic

### E6. Solo at base: every kit clears wolf content alone; nobody clears an elk without two or more trips
Claim: At base stats, a lone champion clears a wolf with gold or gem clusters (W 27,000) in 19-33
rounds taking 120-580 damage, without a recall; two wolves (gold2 wolf2, W 81,000) only Ryth (38
rounds, 980 damage), Marcella (45, 1,060), Heart (39, 430) and Varg (30, 440) manage in one trip;
the elk group (elk1 gem1 gold1, W 144,000) needs 2 trips for Marcella, Heart and Varg and 3-6 for the
others (61-158 rounds); elk2 gold2 needs 8-27 trips, a golem 13-46, and nobody survives a monolith.

| team (base) | gold1 wolf1 | gem1 wolf1 | gold2 wolf2 | elk1 gem1 gold1 | elk2 gold2 | golem1 | monolith1 |
|---|---|---|---|---|---|---|---|
| Ryth | 21r/340 (0.11) | 21r/340 (0.31) | 38r/980 (0.15) | 78r/2400 x3 (0.24) | 208r/8520 x8 (0.14) | 339r x14 (0.10) | - |
| Vesper | 25r/420 (0.10) | 25r/420 (0.27) | 67r/1300 x2 (0.10) | 80r/2520 x3 (0.23) | 245r x10 (0.12) | 315r x14 (0.11) | - |
| Vendetta | 19r/340 (0.12) | 19r/340 (0.32) | 56r/1080 x2 (0.11) | 73r/2280 x3 (0.25) | 244r x10 (0.12) | 364r x16 (0.10) | - |
| Stellar | 22r/420 (0.10) | 22r/420 (0.29) | 58r/1260 x2 (0.11) | 113r/2880 x5 (0.17) | 379r x18 (0.08) | 550r x27 (0.06) | - |
| Prism | 22r/360 (0.10) | 22r/360 (0.29) | 58r/1160 x2 (0.11) | 94r/2400 x4 (0.20) | 325r x15 (0.09) | 496r x24 (0.07) | - |
| Marcella | 22r/300 (0.11) | 22r/300 (0.30) | 45r/1060 (0.13) | 61r/1980 x2 (0.29) | 217r x8 (0.13) | 315r x13 (0.11) | - |
| Willow | 25r/420 (0.10) | 25r/420 (0.27) | 67r/1320 x2 (0.10) | 116r/2760 x5 (0.17) | 371r x17 (0.08) | 549r x27 (0.06) | - |
| Ren | 25r/360 (0.10) | 25r/360 (0.28) | 63r/1160 x2 (0.10) | 104r/2640 x4 (0.18) | 325r x14 (0.09) | 474r x21 (0.07) | - |
| Varg | 30r/220 (0.09) | 30r/220 (0.24) | 30r/440 (0.18) | 73r/1740 x2 (0.25) | 233r x10 (0.12) | 511r x21 (0.07) | - |
| Tavi | 20r/280 (0.11) | 20r/280 (0.31) | 59r/1020 x2 (0.11) | 74r/1920 x3 (0.24) | 217r x9 (0.13) | 288r x13 (0.12) | - |
| Heart | 25r/120 (0.10) | 25r/120 (0.28) | 39r/430 (0.15) | 61r/1590 x2 (0.29) | 226r x9 (0.12) | 409r x17 (0.09) | - |
| Cassi | 31r/440 (0.09) | 31r/440 (0.23) | 98r/1560 x3 (0.07) | 146r/3120 x6 (0.14) | 573r x27 (0.05) | 915r x46 (0.04) | - |
| Issac | 33r/580 (0.08) | 33r/580 (0.23) | 104r/1940 x3 (0.07) | 158r/4320 x6 (0.13) | 552r x23 (0.05) | 831r x37 (0.04) | - |

Cells: rounds from first hit to last kill including recall trips (walk back 10) / damage absorbed,
x trips when more than one, (gems per champion-round of the full cycle: clear + capture + 10 rounds
of travel). Cassi and Issac are listed alone for reference; they are one pick (E8 table).
Evidence: `data/economy/clear_table.json` (`clear_table.py`, compositions laid out as on the pool
map where they first occur), calibrated by E5.
Rests on: E1-E5, P3.
Status: model (solo rows validated within 2 rounds, E5).
Recheck: any champion or camp patch; regenerate the table.
Consequence: A base-stat solo farmer competes for wolf and cluster content only, 19% of the pool's
gems (P3). Every elk it takes costs one to five recall trips: on elk content solo base champions
are health-bound, not travel-bound.

### E7. One Jungle emblem turns a solo elk from 2-3 trips into one for most melee kits
Claim: With five shards and the Jungle emblem (150 gold), the solo elk group (elk1 gem1 gold1)
becomes a one-trip clear: Ryth 26 rounds (Jungle of Strength; 34 with Jungle of Health), Marcella
25-35, Vendetta 31 (Jungle of Health), Heart 32-42, Tavi 33, Varg 33-53; ranged kits stay at 2 trips
(Vesper 49-62, Stellar 64-79, Prism 56-63). The Monster emblem instead leaves 2 trips for most kits
(44-55 rounds). On two wolves (gold2 wolf2) most emblems make it a one-trip clear (17-50 rounds).
Evidence: `econ.simulate_clear`, solo, walk back 15, per kit and emblem (run in this research;
reproduce with `items.py` variants); E1 (Jungle x0.7 verified). Engine check (probe v4,
01yellow_brick_road): Vendetta with Jungle of Health (1,500 health) killed the elk1 gold1 group at
(18,9) in 32 rounds taking 30 strikes of 42 (1,260) without a recall; at base the model needs 3
trips, and a lone Ryth that retreated late died there (v2).
Rests on: E1, E2, E4, M39, M40.
Status: model, verified in the engine for Vendetta with Jungle of Health.
Recheck: Jungle magnitude, elk strength/health, champion health growth.
Consequence: The first 150 gold is a phase change for a solo farmer: elk content (42% of the pool's
gems) moves from health-bound to time-bound. Buying it at the first spawn visit after 150 gold is the
single largest economic decision of the opening.

### E8. Party-size law: the best party grows with the square root of a group's damage per trip cost
Claim: For n champions of rate delta and usable health h each on a group of total health H and
Smith-weighted health W, fighting labour is n x H/(n delta) = H/delta (independent of n), damage is
W/(n delta), and with covered rotation the pooled health n h is spent before a common trip of T_trip
for each member, so per capture
  L(n) = H/delta + capture + n x walk + W x T_trip / (n x delta x h)
champion-rounds, minimised at n* = sqrt(W x T_trip / (delta x h x walk)). With delta 70, h 900,
T_trip 21, walk 10: wolf1 n* 0.95 (solo), gold2 wolf2 1.6, elk1 2.2 (pairs), elk2 3.8, golem 4.5,
monolith 11.5 (the whole team). The simulator agrees: per champion-round, elk1 gem1 gold1 pays 0.13-0.29
solo, 0.33-0.36 for Marcella+Ryth, Heart+Stellar or Varg+Tavi, 0.34 for a melee trio and 0.22 for five;
elk2 gold2 0.27 for a trio; golem and monolith 0.26 for five at base.

| team (base) | gold1 wolf1 | gold2 wolf2 | elk1 gem1 gold1 | elk2 gold2 | golem1 | monolith1 |
|---|---|---|---|---|---|---|
| Cassi+Issac | 17r/240 (0.07) | 33r/760 (0.09) | 48r/1440 x2 (0.18) | 192r x8 (0.07) | 229r x10 (0.07) | 2633r (0.04) |
| Vendetta+Vesper | 10r/180 (0.09) | 20r/540 (0.12) | 38r/1140 x2 (0.22) | 114r x5 (0.12) | 153r x7 (0.11) | 2137r (0.05) |
| Marcella+Ryth | 10r/120 (0.09) | 18r/380 (0.13) | 18r/720 (0.36) | 61r/3540 x2 (0.20) | 86r x3 (0.18) | 694r x26 (0.14) |
| Heart+Stellar | 10r/40 (0.09) | 18r/170 (0.13) | 19r/540 (0.35) | 93r x4 (0.14) | 140r x6 (0.12) | 2059r (0.05) |
| Varg+Tavi | 11r/60 (0.09) | 18r/200 (0.13) | 20r/400 (0.33) | 72r x3 (0.18) | 121r x5 (0.14) | 1622r (0.06) |
| Ryth+Vendetta+Marcella | 7r/100 (0.07) | 12r/280 (0.11) | 10r/420 (0.34) | 25r/1680 (0.27) | 50r x2 (0.20) | 324r x11 (0.20) |
| Heart+Stellar+Tavi | 8r/20 (0.07) | 13r/90 (0.11) | 12r/210 (0.31) | 42r x2 (0.18) | 64r x3 (0.16) | 961r x43 (0.07) |
| main five (Ryth, Marcella, Vendetta, Heart, Vesper) | 4r/40 (0.05) | 8r/160 (0.08) | 8r/360 (0.23) | 15r/1080 (0.23) | 17r/1680 (0.26) | 145r x4 (0.26) |

Evidence: derivation from E1 and E4; `data/economy/clear_table.json` for the table (same columns as
E6, walk 10). Engine check with the coordinated probe (E9, lab map): elk2 gold2 by Ryth + Vendetta +
Marcella 27 rounds and 1,920 damage without a recall (model 25-29, 1,680-1,920); elk1 gem1 gold1 by
Marcella + Ryth 18 rounds, 840 damage (model 18, 720); golem by five 21 rounds (17), by the melee
trio 61 rounds in 2 waves (57-60).
Rests on: E1, E2, E4, E5.
Status: model, verified in the engine for pairs, trios and fives on elk and golem content (within 4
rounds and 17% damage); parties assume covered rotation, and arrival spreads (E28) add 2-6 rounds.
Recheck: as E1/E4; the law's inputs are all runtime values (camp health and strength, kit rates,
health, recall_time, walking distances).
Consequence: Party size is a function of the target, not a doctrine: solo for wolves and clusters,
pairs for single elks, trios for elk packs, the team for golems and monoliths. A pair with a melee
tank and a ranged damage dealer never exposes the ranged one (E1). Solo-only play gives up the
content where 76% of the gems are (P3).

Application boundary: the table starts its named party in place with the assumed kit damage and
cover. It does not prove those bodies will arrive usefully or that diverting them from other work
pays. A helper whose first possible hit is after the solo clear contributes zero to that clear,
even if the two-in-place arithmetic halves its duration. Count each body's travel, staging and
lost alternative work, then the actual attack/arrival order (C22); E28's measured rally spread is
one native instance of this obligation, not a universal fixed surcharge.
The capturer also needs a path from its final attack position to a member tile before the hold:
fresh movement permissions determine whether that step can share the lethal turn. Use one common
shadow currency for the camp value and each body's displaced alternative; a budget split is not
the actual reward split. Captured-resource value alone is not a complete banked-score cycle.

### E9. Monolith sieges need a coordinated team and cover; at base they cost ~220 rounds and a death, with Jungle ~80
Claim: In the engine, five champions (main's speedrun draft Heart, Marcella, Ryth, Vendetta, Varg)
cleared a lone monolith (20,000 health, 200 a round from range 5; walk 11 from spawn) with the wave
policy (rally out of reach, engage together, covered tanks step back and walk out before recalling,
the last uncovered tank ends the wave, everyone heals, rally, engage again) in 218 rounds from first
hit to kill: 4 waves, 16,000 damage, 1 death. Without the death each wave cycle took 47 rounds
(about 30 of contact, 13-19 out), as the model predicts (159-178 rounds, 4-5 waves, 15,400-18,200
damage, no death); the death came from staggered arrival (below) and cost the team a ~100-round
rally wait. With Jungle on five Health shards each (bought with 160 gold farmed per champion), the
same team took 119 rounds in 2 waves with no death (model 109-111), and 81 rounds in one continuous
covered rotation when back-from-spawn champions rejoin the running wave instead of waiting for its
end (model 78-80; 5,950 damage). A golem (5,000, 120, range 4) took the base five 21 rounds without
a recall (model 17) and a melee trio 61 rounds in 2 waves (model 57-60, 3,720 damage both). What the
model misses at base:
- Staggered arrival: from one rally 8 tiles out, the first champion reached the monolith 9 rounds
  after the wave start and the last of five 14-15 rounds after; the earlier arrivals tank first, so
  the champion meant to tank last (and walk out uncovered, R - 1 = 4 strikes) had already lost health
  and died on the way out.
- Recalling inside the reach: covered tanks that recalled where they stood were struck, and their
  recalls cancelled, when the wave ended within their 5-turn channel: 4 deaths, 319 rounds.
- Ranged cover: with Vesper replacing Varg, the last melee walking out was still the nearest body at
  distances 2-3 (ties go to the earlier actor, M6) and first arrivals without an adjacent melee ended
  waves early: 9 waves, 4 deaths, 436 rounds (model 169-172).
- Rejoining at base: back-from-spawn champions joining a running wave arrive as single exposed bodies:
  3 deaths, 320 rounds; rejoining pays only when each champion's health covers its exit (Jungle).
A pair needs 26 waves (694 rounds) and a solo champion dies every attempt at base (model).
Evidence: `bots/contenders/_econ-siege` (coordinated probe: rally, shared-memory wave state, priority
tank order, covered step-back) on the lab map `data/economy/maps/econ-siege.map26a` (monolith (14,9),
golem (8,20), five 160-gold rows), runs and logs in `.cache/contenders/economy/siege/`, measured by
`tools/economy/run_siege.py --measure`; model side `tools/economy/siege_compare.py`. One deterministic
game per configuration, side A, passive opponent.
Rests on: E1, E4, E8, M5, M6, M10, M12, M16, M17.
Status: verified in the engine (lab map, one game per policy); the model is within 10% with Jungle
and for golems, 25-35% optimistic for a base monolith because it assumes simultaneous arrival.
Recheck: monolith or golem stats, recall_time, Jungle magnitude; a pool-map monolith (00lost_world
g_491, edge pocket with clusters) would test crowded approaches.
Consequence: A base-stat team should not siege a monolith: it costs ~220 rounds of five champions
(~0.18 gems per champion-round) and deaths whenever arrival is staggered. After one Jungle emblem
each, a continuous covered rotation takes ~80 rounds without deaths (~0.45 gems per champion-round,
better than any solo content). Safeguards for a siege planner: tank first with the champions whose
health cannot cover an uncovered exit, keep the healthiest for last, walk out under cover before
recalling, and start the wave only when the last tank can survive R - 1 strikes.

### E10. Kit levers on camps: Varg's splash, Heart's dash and shield, Vendetta's chains, range-4 chip
Claim:
- Varg's splash hits every member within 1 of the target (M33), so a 2x2 group takes 4x his
  Strength per hit: gold2 wolf2 solo in 30 rounds (Vendetta 56); his shield (2.5 x Focus for five
  rounds) absorbs five rounds of two wolves. Splash also wakes adjacent groups (M33).
- Heart's dash hits everything around the landing for 1.5 x Focus x (1 + 0.5 x missing) and stuns;
  with her shield (1.5 x Focus) a single wolf costs her 120 damage solo (others 220-580).
- Vendetta's kill reset (M32) makes every turn that kills a cluster a double hit: one cluster per
  turn, a 30-cluster row in ~60 rounds alone.
- Spell range 4 > wolf range 3 (M7): casters chip wolves without being struck; with elk, golem (4) and
  monolith (5) there is no free range.
- Ren returns 0.2 x Focus to every striking monster (M34): tanking two wolves adds 40 a round.
- Stuns (Ryth's area, Prism, Tavi, Ren, Cassi, Heart's dash, Marcella's enhanced hit) remove one strike
  of the stunned member per 8 rounds: -12.5% of that member's damage.
Evidence: probes k5 (Varg), k6 (Heart dash 273.48 on two wolves), p2 (Vendetta chain), y9 (Tavi
from range 4 untouched), y13 (Ren), y9/k10 (stun skips); clear table (E6).
Rests on: M7, M26, M29, M32-M35.
Status: verified (mechanisms), model (rates).
Recheck: champion patches.
Consequence: Kit value on camps depends on the map's compositions: splash and area kits on clustered
groups (Varg x4), shields for solo wolf content, Vendetta for cluster fields, casters for wolf
chip. Draft for the map's content (maps.md P3, clear table).

### E11. Gems per champion-round by content and team: clusters > single elks in pairs > wolves solo
Claim: Over a full cycle (clear + capture + 10 rounds of travel), at base: a gem cluster pays
0.39-0.44 gems per champion-round to any kit, gem1 wolf1 0.23-0.32 solo, the elk group 0.33-0.36 in a
good pair, elk packs 0.27 in a trio, golem and monolith 0.26 with five, gold1 wolf1 0.08-0.12 gems
and 1.3-2.0 gold, gold2 1.9-2.5 gold per champion-round. The team's whole income is
these rates times the champion-rounds it can keep busy, minus walking.
Evidence: `data/economy/clear_table.json` (`gems_per_cr`, `gold_per_cr` per row).
Rests on: E6, E8, E9.
Status: model.
Recheck: as E6.
Consequence: A target's value is (gems + gold value) / champion-rounds including travel, waiting and
capture, with its party size chosen by E8; this is the score a farming planner should maximise, and
the gem-to-gold exchange rate is set by E17-E20.

## Capacity and the speedrun bound

### E12. Supply floor: the respawn-limited minimum race is 284 rounds on the pool mean
Claim: With unlimited champions, zero clear time, every reachable gem group first captured after its
walk from spawn and then every cooldown + 6 rounds, each capture banked at its nearest altar, the
banked total reaches 1,000 after 64 (01the_farm) to 937 (00fragment) rounds; pool mean 284, own half
only 433 (legacy 398 and 648). Gated altars count only after the gate's first capture (01boss_battle
103).
Evidence: `speedrun.supply_floor`; per map in `data/economy/capacity.json` and maps.md "Capacity".
Agrees with main's floor within 1-4 rounds on 39 maps (main's 63 on 01boss_battle ignores the gate).
Rests on: E3, P1, P2, P5.
Status: computed under the stated supply model. The historical BossBattle value103 adds a
  whole-group-first-capture access assumption; P5's corrected one-blocker geometry does not prove
  that assumption necessary. The claim that this is an unbeatable engine lower bound is withdrawn.
  These retained tables and aggregate values were not recomputed by the static correction.
Recheck: map, respawn, movement or gate-opening changes; a native single-blocker access witness.
Consequence: In this model, 00fragment, 01elk_arena and 01hooks (floors 751-937) are supply-poor
  while other maps have substantially more supply. Use those conditional outputs to select an
  investigation; actual labor and access feasibility still decide an executed race. BossBattle103
  cannot justify a required two-monolith clear/capture or a universal scoring floor.

### E13. Feasible speedrun schedules: 1,054 rounds at base stats, 674 with items, 784 on the own half
Claim: The best schedule the dispatcher found per map (`speedrun.py`, passive opponent, main's draft
Heart, Marcella, Ryth, Vendetta, Varg) banks 1,000 gems in a pool mean of 1,054 rounds at base
stats, 697 buying items at spawn visits, 674 taking the faster of spawn and instant buying, and 784
with items restricted to the own half (legacy: 1,562 / 938 / 915 / 1,072). Per map: maps.md
"Capacity". Items cut the race by 41% on damage maps (16-78%), 37% on mixed maps, 28% on travel
maps, 16% on respawn maps and 6% on capture maps (maps.md P4): their value is the health they save.
The own-half restriction costs 16%. The schedule is feasible under the model (E1-E5), so the optimum
is at most these numbers; it is not a lower bound (the labour bound at base, 184-1,267, is below the
base schedule on every map).
Evidence: event-driven dispatcher: squads from {solo, pairs, 3+rest, 3+2, team}, each idle squad takes
the target with the best gems per champion-round (walk from `travel.json`, clear from
`simulate_clear` with the squad's current health and items, a recall-first option that also shops,
respawn waits up to 25 rounds), banks at bag thresholds {25, 50, 100, 200} or when the bank plus
everything in flight wins; best of the grid per map. Coverage: 40 pool + 17 legacy maps, side A, one
draft.
Rests on: E3-E11, P1, P5.
Status: model (upper bound on the optimal speedrun under the model's mechanics; walking only, so
01islands is pessimistic; one draft).
Recheck: a schedule policy change, a patch, the bot's real draft.
Consequence: A good economy banks 1,000 against no resistance in ~550-700 rounds on most maps; this
is the time scale against which a race against a real opponent (E24, ~1,100-1,200) is played.

### E14. What binds: health on damage maps, walking on long maps, respawn on three, capture waits on two
Claim: In the items schedules, champion time splits into travel 17-67%, fighting 11-70% plus recall
trips 0-10%, capture waits 0-22%, banking 2-14%, respawn waits up to 39%. The binding constraint per map
(maps.md P4): damage on 15 maps (fighting + trips >= 45%: 00collision, 00contest, 00corners, ...,
01yellow_brick_road), travel on 10 (walking >= 55%: 00ambush, 00pilgrimage, 00road_of_riches,
01concentrated_wealth, 01shortcut, 01starvation, ...), respawn on 3 (00fragment, 01elk_arena, 01hooks:
schedule within 35% of the floor), capture waits on 2 (00gemfight, 00lost_world), mixed on 10.
Structural reasons: damage maps are those whose gems sit in elk and monolith groups near the spawns
(health per gem, E8-E9); travel maps have long spawn walks or tortuous corridors (P7) with altars far
from the camps; respawn maps have little total supply; capture maps have many tiny cluster groups
(E3's six-round tax).
Evidence: `data/economy/capacity.json` (`time_split`, `binding`).
Rests on: E12, E13, P4, P7.
Status: model.
Recheck: as E13.
Consequence: The lever differs by class: on damage maps, survivability items and parties; on travel
maps, banking where you farm, recall as transport (main D64), and large bags; on respawn maps,
timing and denial (M16); on capture maps, more capturers in parallel and no idle fighting.

### E15. Main's speedrun is 320 rounds slower than the schedule; the solo-farming rule is the structural cause
Claim: main/main@8a7337f4849f banks 1,000 against the starter in a mean 994 rounds (table.md), 320
more than the best schedule (674; median gap 294); main is slower on 39 of 40 maps (00contest: 1,054 vs
1,116). The gap by the schedule's best squad split: 273 rounds on the 14 maps where solo farming is
best, 333 on the 19 where pairs are, 350 on the 6 where the whole team is, 526 on 01yellow_brick_road
(3+rest). A solo-only schedule (same dispatcher, squads of one) averages 896 rounds (+221): 2,000 on
00contest, 00stampede, 01boss_battle and 01starvation and +189 to +518 on 01elk_arena, 00pilgrimage,
00ambush, 01islands, 01fracture, 00eight, 01corridors and 00interlock. The structural reason: main's
target rule rates every group for the deciding champion plus at most one hypothetical joiner, over
at most four trips at a safety margin of 1.6 (D11, D16, D69; its report's mechanism 2). By E1 a
group's damage scales as W/(n delta), so rated for one or two champions an elk pack costs ~3 recall
trips, a golem 5-15 and a monolith 25+ (rate near zero), while E8 puts the optimal party for that
content at 3-5; it is taken late (main's report: big groups untouched for 500+ rounds) or by
health-bound sieges with recall trips (14.4 heal recalls by r400). The rule is not wrong for wolf and
cluster content (19% of the pool's gems, P3); it is structurally blind to the rest. Second order:
recalls at 50% health waste a third to a half of each health pool (E4), the first emblem comes late
(first shop r133-146, O5) and is not the camp-optimal one (E20), and bags of ~20 cost extra altar
walks on travel maps (E22).
Evidence: `data/economy/capacity.json` vs `.cache/main/analysis/economy/table.md`; solo-only schedules
in `data/economy/solo_only.json` (`speedrun.run_schedule(split="solo")`, 40 maps, grid B x W); main's
report mechanisms 2-4.
Rests on: E1, E4, E8, E13, P3.
Status: model (schedules) + measured (main's rounds); the causal attribution is a model reading:
main's own time split (travel 31%, fight 36%) matches the schedule's shares, so the gap is in what
the rounds buy (gems per champion-round), not in idle time.
Recheck: a main release with on-demand parties (its report's mechanism 2) should close most of the
gap on pair/team maps first.
Consequence: For the contenders: the planner must choose party size per target from E8 and
commit allies to it (shared memory: target group, size, rally round); a doctrine of fixed squads or
pure solo play loses 300+ rounds on the maps whose content does not fit it.

### E16. Where main's economy model and this one disagree
Claim:
1. Retreat cost: main's bound.py keeps two rounds of incoming for an escape from any group; the
   engine charges an uncovered melee tank R - 1 strikes (wolf 2, elk and golem 3, monolith 4 x 200),
   so the bound overrates solo and pair sieges of elk packs, golems and monoliths (E4, E9).
2. Tank rotation: a healthiest-fighter model needs an actual aggro controller. Unequal distances
   require positioning/cover and may cost attacks (E1, E4). At equal nearest distance, M6's priority
   tie can rotate next-round targeting without moving; current native E31 now verifies this for a
   single elk and two adjacent melee bodies. Merely assuming pooled HP supplies no such controller.
3. Gates: the retained 01boss_battle values63 and103 assume open access and first-full-group-capture
   access respectively. Corrected P5 identifies one altar-neighbor blocker, not a proof that both
   monoliths must die or capture must complete before any tribute. Neither access assumption is
   thereby validated as an engine scoring lower bound; dynamic single-blocker access remains open.
4. Items: main's bound buys Strength shards instantly with Jungle, Monster, then Power emblems
   (optimistic in timing, close to E20's best order); its estimates are therefore close to this
   model's instant-buying schedules on wolf-and-elk maps and far from them on siege maps (00contest
   667 vs 1,116 here).
5. Agreements: main's camp_dps (model.h) is E2's formula (resets, kill resets, spells and area
   neighbours per 8 rounds, splash, Heart's dash since D85); capture 6 rounds from the step; supply
   floors within 4 rounds elsewhere; the travel-heavy split; the big-group lever; bag size as a lever;
   the respawn-bound maps (00fragment, 01elk_arena, 01hooks).
Evidence: `.cache/main/analysis/economy/scripts/bound.py` and `bots/main/main/src/model.h` read
against E1-E9; numbers as cited.
Rests on: E1-E15.
Status: analysis.
Recheck: when either model changes.
Consequence: Use main's per-map gaps as a pointer to where a solo rule loses, not its estimates as
bounds: they are optimistic on siege content and pessimistic on items.

### E28. Rallying costs the walk from the rally, the arrival spread and the slowest member's return
Claim: Coordinating a party costs time outside the fight: (1) the walk from a rally that must lie
outside the group's reach (R + 1 tiles out), 6-9 rounds to first contact from a rally 8 tiles out;
(2) the arrival spread, the rounds until the last member stands where it can act: 2 rounds for a pair
or trio, 5-6 for five melee around one target; (3) waiting for the slowest member before each wave:
13-19 rounds after an orderly wave, about 100 after a death (`respawn_time` 60 plus the walk); (4)
under a strict wave policy, champions back from spawn idle until the wave ends (40-70 idle rounds each
in the Jungle siege, where rejoining the running wave cut the siege from 119 to 81 rounds). In
champion-rounds, a five-champion wave start costs about 5 x 9 = 45 before the first hit lands.
Evidence: wave-by-wave arrival in the E9 runs (`.cache/contenders/economy/siege/`: first in reach
+4-6 rounds after the wave start, first adjacent +8-9, last adjacent +12-15 for five, +10 for pairs
and trios; wave cycles 47 rounds without a death, 105 with one).
Rests on: E9, M2, M6, M22, M50.
Status: measured (engine, one lab map, one probe policy).
Recheck: another rally policy or map; recall_time and respawn_time.
Consequence: Parties are worth forming where E8's gain exceeds roughly 10 champion-rounds per member
per engagement; for short clears (single elks, elk pairs) meet at the target rather than at a rally,
and for sieges let returning champions rejoin a running rotation once their health covers their
exit.

## Item economics

### E17. Gold buys nothing but items; its price is the trip that spends it
Claim: Gold scores only through items (and the round-2000 tiebreak counts lifetime earned gold,
which spending does not reduce, E27). Unspent gold provides no direct production bonus, but that
does not prove any affordable purchase is beneficial now. Choose an item against the productive
work and survival it changes, including the option to save for another purchase. Its travel price
is the trip that reaches the shop (E19), or zero additional travel when the champion is already on
spawn (start, heal recalls, deaths: M4 lets the dead shop).
Evidence: rules (Winning; tiebreak by earned gold); M4, M39, M42 (only Tavi can sell, refunds are not
earned gold).
Rests on: M4, M39.
Status: derived.
Recheck: a passive gold-interest mechanic or a tiebreak change.
Consequence: Valuation of gold in target choice should come from the items it buys within the race
that remains (E18-E20), never from a fixed exchange rate; main's flat 1.5 gold per gem (D58) is a
proxy for that value at mid-game.
Application boundary: a positive internal equipment forecast is not yet marginal team income.
Its future tasks must be accessible, timely and credibly available. Never-observed or stale tasks
need paid reobservation/scouting; teammate commitments require counting displaced work rather than
adding the same capacity again. Gold-only tasks can finance later productive items, but that
conditional value needs an executable work/shop path. Source/model inconsistencies and an actual
fresh investment opportunity are separate claims; neither one establishes the other.

### E18. The first emblem is worth 25-45% of a solo farmer's cycle; Jungle of the main stat is the best first buy
Claim: Charging health as recall trips (damage / 80% of max health x (recall_time + 1 + 15)), one
solo capture cycle costs (champion-rounds, base -> with the item):

| kit | content | base cycle | +1 Str | +1 Health | Jungle (Str) | Monster (Str) | Power (Str, 6) | Jungle (Health) | Durability (Health) |
|---|---|---|---|---|---|---|---|---|---|
| Vendetta | gold1 wolf1 | 32.9 | -3.1 | -0.8 | -10.9 | -10.7 | -10.7 | -4.8 | -3.6 |
| Vendetta | elk1 gem1 gold1 | 85.2 | -7.7 | -4.3 | -35.2 | -30.9 | -28.3 | -25.2 | -18.9 |
| Ryth | elk1 gem1 gold1 | 85.0 | -9.3 | -5.4 | -34.7 | -29.8 | -27.4 | -27.8 | -22.7 |
| Marcella | elk1 gem1 gold1 | 84.1 | -5.3 | -2.9 | -31.8 | -26.8 | -24.5 | -19.3 | -13.9 |
| Heart | elk1 gem1 gold1 | 96.5 | -10.3 | -3.9 | -41.4 | -30.2 | -30.2 | -27.4 | -18.6 |
| Varg | elk1 gem1 gold1 | 97.4 | -9.7 | -3.6 | -46.1 | -41.1 | -40.1 | -24.7 | -18.4 |
| Vesper | elk1 gem1 gold1 | 109.8 | -11.3 | -5.4 | -46.3 | -38.1 | -35.5 | -31.9 | -23.9 |
| Stellar | elk1 gem1 gold1 | 143.9 | -13.7 | -8.8 | -56.0 | -51.4 | -41.1 | -51.7 | -38.8 |
| Tavi | elk1 gem1 gold1 | 93.0 | -5.8 | -3.8 | -34.3 | -30.5 | -27.8 | -24.8 | -17.9 |

Heart's Jungle of Focus equals her Jungle of Strength on the elk (-41.6). A Strength shard is worth
about two Health shards on elk content and three to four on wolf content; the Jungle passive alone
(Jungle minus 5/6 of Power) is worth about half an emblem's stats on elk content (-11.6 of -35.2 for
Vendetta) and a fifth on wolf content.
Evidence: `data/economy/items.json` (`items.py`, all 13 kits x 10 compositions x 10 item variants).
Rests on: E1, E2, E4, M39, M40.
Status: model (throughput only; survival in fights is combat.md's question).
Recheck: item magnitudes, growth values.
Consequence: For throughput, the first 150 gold should be the Jungle emblem of the main stat (Focus
for Heart, Stellar, Prism, Cassi); Health and Durability are survival items whose economic value is
in fights, not camps. procless's Treasure/Health first (O4) buys PvP survival and compounding gold at
an economic cost of ~20-30% of its first emblem's camp value.

### E19. A shop trip pays back within one elk capture or two wolf captures
Claim: A dedicated shop trip (recall_time + 1 + 15 walk back = 21 rounds) to buy the first emblem
repays itself after 0.4-0.7 solo elk captures or 1.4-2.7 solo wolf captures (items.json
`breakeven_captures`). Combined with a heal recall the trip is free. With R gems left to bank by
one champion at g gems per capture, the trip pays when the cycle saving x R / g exceeds 21 rounds.
Evidence: `data/economy/items.json`.
Rests on: E4, E18.
Status: model.
Recheck: as E18; recall_time (Home cuts the trip by 2).
Application boundary: R means realizable marginal captures after the investment, not all static
camp supply or a copied stale portfolio. Price scouting, current access, competing capture times,
teammate displacement, the route back and the remaining race deadline. The historical simulator's
per-cycle saving is conditional on those actual cycles being available; an internal positive rate
does not certify a useful shop trip.
Consequence: Shop on every heal recall; make a dedicated trip when the purse reaches an emblem (150)
and at least two captures of the champion's content remain; stop dedicated trips only in the last
two or three captures of the race.

### E20. Throughput order: Jungle, then Monster, then Power of the main stat; Treasure first costs ~13 rounds
Claim: On the same schedules (each map's best policy, items bought at spawn), changing only the
emblem plan moves the pool mean (paired, 40 maps, from 759 rounds): Jungle -> Monster -> Power of the
main stat (reference), Monster first +11.3 +- 5.8 rounds (better on 14 maps), Treasure first +12.6 +-
6.5 (11 maps), Jungle of Health first +4.5 +- 34.6 (7 maps; noisy), Power only +31.9 +- 6.5 (7 maps),
Jungle then Durability of Health (tank) +58.7 +- 44.0 (3 maps), Treasure/Health -> Jungle -> Bruiser/
Health (procless's order, O4) +58.7 +- 42.7 (4 maps).
Diminishing returns are structural: clear time ~ 1/delta, so each emblem's saving is smaller than the
last, while Jungle's passive saves a fixed 30% of the health cost whenever it is bought.
Evidence: `tools/economy/items_order.py` -> `data/economy/items_order.json` (40 pool maps, main's draft,
side A; spawn-visit buying).
Rests on: E13, E17-E19.
Status: model (paired; one draft; passive opponent, so no value for survival).
Recheck: item magnitudes; a schedule-policy change.
Consequence: Against a passive or distant opponent, buy for camps: Jungle first, then damage. Each
Health-first or Treasure-first build should be justified by the fights it wins (combat.md), since it
costs 10-60 rounds of race.

## Banking

### E21. Tribute costs ceil(bag/10) turns: 0.1 turn per gem in tens
Claim: A tribute of b gems channels max(1, ceil(0.1 b)) owner turns and banks the whole bag
(including gems captured during the channel) at the following turn start; damage does not stop it.
The channel costs 0.1 turn per gem when b is a multiple of 10 and up to 0.19 per gem just above (b =
11 takes 2 turns), so bags just over a multiple of ten waste up to one turn.
Evidence: M19; probe p2 (16 gems at r192 -> banked r194).
Rests on: M19.
Status: verified (engine 1.0.2).
Recheck: submit settings.
Consequence: Channel time is never the reason to bank small: 1,000 gems need ~100 channel turns in
any bag sizes; bag size is decided by walking and risk (E22).

### E22. Bank on contact, not at a bag size: b* = sqrt(2 W rho / (k h)) with h set by contact (C21)
Claim: A farmer earning rho gems a round whose banking trip costs W extra rounds of walking, and who
dies with its bag at hazard h per round (the bag averages b/2 while it fills), pays per gem
W / b + k h b / (2 rho), with k = 2 when a champion takes the bag (the enemy gains it) and 1 when a
camp destroys it; the minimum is at b* = sqrt(2 W rho / (k h)). The hazard is not a constant and
not a function of the bag: online it depends on contact, health and allies and is flat in the bag
(C21: with one enemy within 3, 5.8 / 5.3 / 5.6 / 5.3 / 4.6 / 6.1 deaths per 1,000 rounds for bags of
0 / 1-9 / 10-19 / 20-39 / 40-79 / 80+). With rho = 0.18 (a top champion's banked rate, E24) and W =
25, by contact state at the turn start:

| contact (C21) | h per 1,000 rounds | b* stolen (k = 2) | b* camp (k = 1) |
| --- | --- | --- | --- |
| no enemy within 6 | 0.007 | ~800 | ~1,100 |
| enemies at 4-6 only | 0.32 | ~120 | ~170 |
| one enemy within 3 | 5.7 | ~28 | ~40 |
| two or more within 3 | 18.2 | ~16 | ~22 |

Out of contact b* exceeds anything a champion collects before passing an altar, so bag size is not
the risk there; in contact the optimal bag collapses to 16-40. Against a passive opponent the
schedules bank 100-200 at a time (E13); top teams bank a median 24 (T1, O11), main ~20.
Evidence: derivation (economic order quantity); hazards from C21 (1,000 online games, 9.8 million
champion-rounds, 5,960 champion kills, all tiers); rho from E24; schedule policies in
`capacity.json` (`policy.bank_at`).
Rests on: E21, E24, C21, M45.
Status: model with measured hazards. The flat-in-bag hazard is a property of today's online field: a
bot that reads visible bags (M45) and hunts carriers would make h rise with b and lower b*.
Recheck: opponents that target carriers; new online hazard data; tribute or bag-transfer rule changes.
Consequence: The banking trigger is contact, not a bag threshold: carry freely while no enemy is
within 6 (bank at altars passed on the way, W near 0), and bank or break contact as soon as an enemy
comes within its two-turn reach (C16), where the hazard is about 1,000 times higher. Small fixed bags
(main's ~20) pay the walking cost W / b on every trip without buying safety out of contact.

### E23. Marcella and Prism turn banking into gold and Focus; the capture tile decides who earns
Claim: Marcella earns 2 gold per gem she tributes, times her gold bonuses (M38): 1,000 gems banked
by her are 2,000 gold, as much as a team earns from camps by ~r400 (O8). Prism gains 1.5 Focus per
carried gem (M43): a 100-gem bag is +150 Focus, 2.9x her base 80. Gems belong to whoever stood on
the capture tile (E3), so a party chooses who earns by who captures.
Evidence: M38, M43; probe k15 (Marcella +8 gold for 4 gems); O17 (a Prism carrying 220-710 gems).
Rests on: E3, M38, M43.
Status: verified (mechanisms), derived (magnitudes).
Recheck: their passives.
Consequence: In parties, put Marcella on the capture tile and let her bank (a gold stream that does
not depend on camps), Vesper on gold groups (x1.33), and Prism on gem groups when it can carry
safely. A team with Marcella should route gems through her.

## The race

### E24. Race clock: t(1000) = t0 + 1000 / r; contested games end near r1,100-1,200, 1.6-1.7x the speedrun
Claim: A team banking at rate r (gems a round) after a start-up t0 finishes at t0 + 1000 / r. Against
a passive opponent the schedules bank at ~1.5 a round (674 rounds); in online games the winner's
whole-game rate is a median 0.88 a round and the loser's 0.41 (909 games on engine 1.0.2), games end
at a median r1,170 [IQR 993-1,366], and top games at r1,085 (O7). A real opponent therefore cuts a
team's banking rate to 30-60% of its speedrun rate (contested camps, denied captures, fights,
deaths).
Evidence: `tools/economy/race_online.py` over every summary in `results/online/` (909 unique games, 26
reaching r2000), O7.
Rests on: E13, O7.
Status: measured (online rates) + derived (the clock).
Recheck: winning_gems, max_rounds.
Consequence: Plans should be judged over ~1,100 rounds: an investment that pays back in 200 rounds
pays five times in a real game; one that pays back in 800 barely pays.

### E25. One kill moves ~40 gems plus twice the victim's bag
Claim: A kill removes the victim for `respawn_time` (60) plus its walk back (~15-30) rounds, worth
rho x 75-90 = 14-27 gems of its production (rho 0.18-0.30); hands its bag b to the killer (a 2b swing
in the race once banked; a camp kill only destroys b, a 1b swing); and pays the killer
`kill_gold` (100) x its gold bonuses, worth about two thirds of an emblem, i.e. ~20 gems over a
600-round remaining race (E18: an emblem lifts one farmer ~30%). Total ~35-45 gems + 2b; with the
median top carry of ~30 gems (O11) ~100 gems, about 110 rounds of a winner's income. The bag at a
death is smaller than the average carry: carriers are in contact less often (5-6% of their rounds
within 3 of an enemy, against 7.6% with an empty bag), so their death rate is 0.36-0.46 per 1,000
rounds against 0.71 (C21).
Evidence: derivation from M22, E18, E24 (rates), O11 (carry), C21 (hazard by bag and contact).
Rests on: E18, E24, M22, C21.
Status: model.
Recheck: kill_gold, respawn_time, bag-transfer rules.
Consequence: A fight is worth its gem swing against the farming it costs: two champions spending 20
rounds to kill a carrier with 30 gems trade ~8 gems of farming for ~100; chasing an empty-bagged
champion for 20 rounds trades the same for ~40. Carrying a big bag near enemies is the most
expensive risk in the game (O11).

### E26. Income leads decide: a 10% rate edge is worth 2-3 bag kills; leaders at r400 win 78-90%
Claim: If A banks at r and B at r(1 - x), B is 1000x gems short when A finishes; B must win that many
gems of net swing in fights (E25): x = 10% is 100 gems, 1-3 kills depending on bags. Online the
banked-gem leader at r400 wins 55% with a lead under 50, 78% at 50-149, 90% at 150-299 and 96% above
300; the earned-gold leader at r300 wins 52% under 250, 76% at 250-499, 92% at 500-999 and 98% above
1,000 (909 games). Kill leads at game end track wins (1 kill 63%, 3 81%, 6+ 96%) but mostly reflect
leads rather than cause them (O9).
Evidence: `data/economy/race_online.json`; O8-O10.
Rests on: E24, E25, O8, O9.
Status: derived (the arithmetic) + measured (online: mostly our games against ranks 22-40 and the
top-8; association, not effect).
Recheck: as E24.
Consequence: Economy is the default win condition; fights are worth taking when their expected swing
exceeds the farming they cost (E25). Early gold leads compound through items (E18), which is why the
r300 gold lead predicts better than the banked lead.

Measurement example (two reproduced historical baseline losses, engine 1.0.3): camp-clear counts and a
banked-gem deficit do not identify whether the missing score was never collected, lost on death
or still carried. Timetable's
[source-bound receipts](../../../results/contenders/timetable/race-loss-reproductions/selected.json)
reproduce `contenders/timetable@71992e48d4e6` versus `anchors/main-v42@9fc5d7469074`.
The retired inherited chassis is non-independent, with no qualification of fresh Timetable.

| Baseline side / end round | Collected | Banked | Explicit death loss | Terminal carry | Opponent collected / banked |
| --- | ---: | ---: | ---: | ---: | ---: |
| 00stampede A / 1763 | 160 | 107 | 53 | 0 | 1013 / 1013 |
| 01gold-rush B / 1483 | 77 | 66 | 0 | 11 | 1097 / 1097 |

Here `collectedGems` includes camp and champion receipts; it is not unique new camp supply.
`gemLoss` is an explicit death receipt, and terminal `gems` is carry. Stampede's Varg really loses
53 gems at round 1556, but the team's collected total is already far below the opponent's. Gold
Rush's remaining 11 gems are still carried by Marcella, rather than lost. These records show a
portfolio/collection deficit, not evidence that fixing bag loss alone explains the score deficit.
They do not identify which draft, route, combat or allocation decision caused the low collection.
Coverage is exactly these two deterministic reproductions, not independent new samples, a
population-frequency estimate or evidence that a new policy is stronger.

### E27. The round-2000 tiebreak is a gem count; lifetime gold has never decided a game
Claim: 26 of 909 online games (2.9%) reached r2000; all 26 were decided by banked gems, none by
earned gold or kills. Gold decides only an exact gem tie.
Evidence: `data/economy/race_online.json` (`tiebreak`).
Rests on: rules (tiebreak order).
Status: measured (909 games; rank bands differ: O7 reports 51% gem tiebreaks at rank 401+).
Recheck: tiebreak rules; lower-tier pools.
Consequence: Never bank gold "for the tiebreak"; on maps where a race to 1,000 is unlikely
(supply-poor: 00fragment, 01elk_arena, 01hooks, E12), bank every gem before r2000 and deny the
opponent's banks (locks, contested captures, M15, M20).

### E29. Candidate bounds must dominate the actual state-conditioned score
Claim: Original full camp health divided by sustained damage is not a universal lower bound on
  the work remaining. A cleared reward has zero clear work; partial health and action readiness
  also change the candidate. A pruning score must upper-bound the actual scored option, including
  its current value multipliers, rather than a nominal full-health task. An admissible individual
  bound alone does not make a fixed-size shortlist a globally exact selection.
Evidence: current-source audit of timetable `rate_ctx`, `rate_n` and `choose_target`, with exact
  source hashes in [decision-models-3.0.5.json](../../../results/contenders/foundation/decision-models-3.0.5.json).
  The pure [decision_models.py](tools/economy/decision_models.py) retains discriminating examples:
  value 120, travel 2, capture 6 and a cleared reward score 15, while an original 9,000 HP / 100 DPS floor
  bounds it at 120/98; missing the owner 1.15 multiplier underbounds a zero-travel reward; ten
  high-bound/low-actual options can hide an eleventh actual winner. These are synthetic model
  contexts, not native game states or an actual-caller test.
Rests on: M13-M16, M52; E8's labour model and the source-specific scalar conventions.
Status: conditional algebra/source audit (CLI/SDK3.0.5, engine1.0.3 bundle); 2,592 exact-rational
  domain samples accompany the symbolic proof. No native games, caller probes, health or strength
  claim, and no universal bit-level floating-point proof.
Recheck: any change to score multipliers, discounts, cost domains, distance conventions or search
  truncation. Re-derive a bound from its actual score, not from a different bot's formula.
Consequence: For the audited timetable formula, nonnegative travel t, wait, clear and damage,
  n>=1, home>=0, positive HP and capture cost C imply L>=C+t/2: without heal-first L>=n*t+C;
  with its discount L=n*(t/2+wait+clear+6+home)+C+damage*trip/(0.8*maxHP). Thus
  1.3*current_value/(C+t/2) is an upper bound for its options, provided current_value includes
  ownership 1.15 and every other value factor and 1.3 bounds the call bonus. The heal-first
  discount makes C+t an unsafe general floor. A sorted bound<=best break is justified; stopping
  after a fixed topK or for fuel is still an explicitly incomplete search. This proves only the
  model comparison, not realized productive throughput.

Application (historical retired non-independent source, one key): timetable's
[safe-dispatch record](../../../results/contenders/timetable/index.json) retains a fresh native
reward witness on 00gem-fields, side A versus starter. At actor 0's round-64 turn, native group 5
(`g_727`, 71 gems/90 gold) is personally visible and reward-available. Its omitted model score is
5.03333 versus chosen/hysteresis threshold 0.29135. Control `b6a62a955324` and delayed active
`f33cc436a843` hold availability/audit flags fixed; their
[represented prefix](../../../results/contenders/timetable/pruning-available-effect/prefix.json)
matches through the ready turn in 11,037 state/timeline frames, masking only timeline identity.
They move differently by round 67, but both lose this reward to the same enemy recipients at
round 67, before the logged round 71 arrival estimate. This is model-ranking/dispatch contribution
without a productive capture gain, not a strength improvement. The earlier round97 guest-reward
witness remains failed: native state was respawning with timer30 despite a fresh seen stamp.
Source-specific prefix equality excludes private fuel and unrepresented internals. Correct model
selection still needs reachable arrival, target ownership/deadline and complete costs; a bound on
a model with omitted item/passive or executor effects remains a bound on that incomplete model.

### E30. A bank's travel cost is the extra route through one legal tribute tile
Claim: On one consistent fixed travel graph, with position P, work goal set T and legal tribute
  roots A, minimum extra travel is min_a[d(P,a)+d(a,T)]-d(P,T). Both legs must use the same visit
  root a. Distance to the altar, or separately minimized legs through different roots, does not
  determine that surcharge. The altar center is impassable; tribute happens from a legal tile in
  queried submit range.
Evidence: the source-pinned compound root/field audit and pure counterexamples in
  [decision-models-3.0.5.json](../../../results/contenders/foundation/decision-models-3.0.5.json).
  In a walkable y=0 corridor, P=(0,0), altar=(6,1), submit range 1 gives roots x=5,6,7. Both work
  goals (12,0) and (-12,0) have the same five-step nearest tribute distance, but banking adds zero
  and ten travel steps respectively. A separate tree example makes independently minimized legs
  shorter than the direct route, exposing the invented transfer between roots.
Rests on: P5/P7 (altar and travel geometry), M19/M20 (tribute and locks), M52 (state confidence).
Status: derived graph geometry and current-source audit, not a native route/caller result.
Recheck: movement/tribute-range changes, gates, distance-field obstacle sets, target/range projection,
  transient occupancy or any estimate being promoted to an exact route cost.
Consequence: With exact fields DA to A and DT to T on the same graph and finite relevant roots,
  max(0,DA(P)+min_a DT(a)-DT(P)) is a lower bound and the same expression with max_a DT(a) is an
  upper bound for going via a nearest tribute root. A small lower bound cannot certify a cheap
  detour; use a witnessed consistent-root route or a suitable upper bound for that claim. Add
  channel time, lock removal, income-on-bank, recovery, danger and deadlines separately. Current
  compound altar/group fields open different camp roots, and cold altar-center Chebyshev distance
  differs from warm tribute-root distance: the same-graph proof cannot silently qualify their mix.
  Live gated camps, blocked starting camp tiles and cold/unreachable fields require an explicit
  access/unknown policy; static metadata or a timer estimate is not a reachable-bank certificate.

### E31. Next-round priority rotates a single elk's tank without losing either melee basic
Claim: Two adjacent equal-distance melee champions can share a single elk's actual damage through
  SDK priority, while both basic every fighting turn. In the measured case, Ryth and Marcella each
  have 1,200 HP and 45 Strength; the elk has 2,400 HP and one 60-damage round-start volley. Both
  start at Chebyshev distance 1 on opposite sides of the elk. Each owner sets next-round priority
  100 when currently healthier, else 0, with Ryth winning equal-HP ties. No movement is needed to
  change tank. Setting priority spends no movement, attack or cast allowance.
Evidence: [pair-rotation.json](../../../results/contenders/foundation/pair-rotation-3.0.5/pair-rotation.json)
  retains four 90-round native lab games, OFF/ON on both sides of one symmetric open custom map,
  current engine 1.0.3 and CLI/SDK 3.0.5. OFF is `f25ef940df55`, ON `e1b9dc399766`, passive opponent
  `ccc850e981ae`; source is `bots/contenders/_foundation-pair-rotation/src/main.cpp`, with the
  `rotate_pair` knob default off. These are dedicated mechanics bots, not a contender planner.
  [Independent verification](../../../results/contenders/foundation/pair-rotation-3.0.5/native-verification.json)
  reads all four saved games at native rounds 29-76, using the current official query logic and an
  owned hash-bound projection that retains its already-read native priority field. All requested
  windows are complete with no query failures. Four complete queried actor states before the
  first basic match between OFF and ON. Actual priority-setting event IDs, each following volley's
  target coordinate and HP delta, positions, basic order, capture recipients and bank score are saved.

| Both sides give the same outcome | Fixed Ryth priority OFF | Health-based next-round priority ON |
| --- | --- | --- |
| Basics Ryth / Marcella | 20 / 34 | 27 / 27 |
| Elk volleys Ryth / Marcella | 20 / 13 | 13 / 13 |
| Clear round; HP Ryth / Marcella | 63; 0 / 420 | 56; 420 / 420 |
| Capture round; named recipient | 69; surviving Marcella | 62; Ryth |
| Capture reward; bank round | 90 gold + 15 gems; 75 | 90 gold + 15 gems; 68 |

  Both first basics occur at round 30, before the first elk volley at round 31. That volley still
  hits Ryth under round-30 priority. Settings made after that damage in round 31 redirect the
  round-32 volley to Marcella and put her first in that round's own-team order; they cannot rescue
  the already-hit body in round 31. Ryth delivers the last hit and steps onto the member tile in
  round 56; Marcella has already acted and leaves on her next own turn, round 57. Only Ryth captures. Both
  cycles bank all 15 gems. OFF loses a champion and banks seven rounds later. All four games have
  zero faults, exhaustion and error lines; maximum ordinary fuel is 69,683/100,000. Saved native
  fuel headers are independently checked for the selected side; all owned observer jobs exited empty.
Rests on: M2/M5/M6 (round order and nearest-distance tie), E3/E4 (capture and recovery), E8/E28
  (party cost and arrival). This current study supplements the historical siege probes without
  relabelling their models or evidence.
Status: verified bounded native full clear/capture/bank mechanism, engine 1.0.3. Ninety-round lab
  health is not full-game release health, a production integration, an opportunity-profit test or
  a contender strength result. No items, abilities, shields, healing, other awake camps or enemy
  interference occur. Unequal distances, terrain changes, ranged bodies, different damage
  multipliers and multiple volleys before the next priority update are untested.
Recheck: any camp targeting, priority, owner-order or basic-damage change; actual actor readiness,
  peer presence and every other source of incoming damage at the production caller.
Consequence: A party forecast may use this actual controller only while the named live melee peer
  remains at the same nearest distance, both have a timely legal basic, and next-round priorities
  are configured. Simulate each unavoidable future volley against the body the current configured
  order will target; require individual survival plus escape reserve. A sum of HP is insufficient.
  The round-start volley precedes every owner action, so a body must survive it before changing
  priority. The successful clear ends at 35% HP on each body: a generic 50% recall would abandon it.
  A production exception therefore needs a fresh covered continuation forecast and actual peer,
  with explicit release and restored normal priority; this fixture supplies no global safety rule.

Actual conditional caller application: Warband's r3 supplied-candidate fixture now reaches the
production party executor with rotation/continuation enabled. The
[independent r3 verification](../../../results/contenders/foundation/party-r3-verification/verification.json)
reads four saved 80-round OFF/ON games on both sides, complete unfiltered native rounds 29-67.
Four complete queried actor prerequisite states match at their first round-30 native gameplay
actions, dereferencing only vision/ability table references. This does not compare private shared
memory; offer/accept and model-score values remain source/guest evidence. The fixture stipulates
the named task/alternative, rather than proving autonomous target selection.
ON supplies 27 basic attacks per body at rounds 32-58, with 26 actual elk volleys split 13 each,
and clears at 58 with both at 420 HP. It maintains real numeric next-round priority through the
actual SDK calls and continues below the generic recall floor. The leader enters the member tile
at 59; the helper leaves at 59 and legally starts recall at 60. At 65, only the leader receives
90 gold and 15 gems. OFF never starts the camp. All four saved windows are complete with no
query failures, owner health counters are clear, and private observer jobs exit empty. This adds
actual executor/continuation evidence, not a bank, normal scheduling, full-game release health or
strength result. The owner's failed r2 probe remains preserved beside its repaired r3 source.

Normal-play geometry application, with limits: one saved v41 `00elkring` A/starter window
(rounds 168-183, source `main/main@db6ca9bc4e39`) has actual equal-distance co-farming. The
[complete reconstruction](../../../results/contenders/foundation/cofarm-v41/analysis.json) covers
2,332 unfiltered native events and 1,968 before-state checks. Ryth first reaches the same adjacent
distance after moving at 172. At Marcella's round-173 owner entry, Elk0 is (3,6), Marcella1 is
(2,5) at 1,020/1,200 HP and Strength85, Ryth2 is (2,6) at 1,200/1,200 HP and Strength50; both are
visible melee with no channel/restriction, current and next priority0, and no completed incoming
mitigation emblem. Their extra Strength is actual gear, not the lab's base45 assumption.
Elk volleys at 171/172/173/175/176/177/179/180/181 all target Marcella for60, taking her HP through
1,140/1,080/1,020/960/900/840/780/720/660. Ryth takes no damage; no other camp/PvP HP damage hits
the pair in the measured cycle. Thus the targeting tie has a real normal-play geometry, but fixed
priority never reaches the 600-HP recall threshold before this actual clear.
This is not a pure two-body/basic-only cycle or a single-member group: group0 also has harmless
gem29 and gold6. The pair uses resets/spells; Vendetta3 kills gold at 178 and adds three 88-damage
elk basics at 179-180. Marcella delivers the elk's final 56 damage at 181, and Ryth then finishes
the gem member. No rotation alternative was dispatched, and no capture/bank beyond the window is
claimed. Numeric priorities were queried read-only, while camp wake/stun state was not; missing
volleys alone are not attributed to one particular cause. This is a scoped applicability seam,
not measured rotation gain or evidence of a recall failure.

Autonomous party application now has two bounded payout-to-bank suffixes. Warband R3 trace-ON
`e0e21bc6ae4d` on `00elkring`/starter, both sides, applies repeated named-pair basics and casts:
Ryth actor 2 receives the sole 22-gem reward at round 79 on A, and Marcella actor 6 receives it at
round 161 on B. A has a third body's final-clear contribution; B's represented target-camp damage
comes from the pair. Numeric current/next priorities were not queried, and neither case reaches
below-half-health continuation. The [native analysis](../../../results/contenders/warband/local-party/native-r3-analysis.json)
therefore proves actual cooperation, not every rotation prerequisite or exclusive pair causality.

The [labor audit](../../../results/contenders/warband/local-party/labor-audit.json) joins each native
payout to its exact recipient's statistics: bag 0 to 22, then 22 submitted with bag zero and own
team score increased by 22, at round 87 on A and round 196 on B. No intervening recipient gem
collection, submission, loss or death occurs. This is a native-payout/statistics-submission join,
not a fresh native channel observation. Final untraced BASE `e5c2eb026465` versus ON `896f4080a7aa`
has 16 healthy complete subject games on Elkring/Ambush, both sides, v44/starter, eight paired keys.
The [qualification](../../../results/contenders/warband/local-party/untraced-r1/qualification.json)
finds every complete uncompressed gameplay payload identical to its corresponding R3 payload
without masking. This transfers represented gameplay to the final source on those keys; fuel,
stdout and private state are outside that equality and health is checked separately.

Earlier capture does not establish extra team supply here. BASE pays the same target groups at
rounds 82 and 168, versus ON 79 and 161. At round 200, both arms have collected 231 team gems on A
and 176 on B; at round 300, banked gems are BASE 330 versus ON 341 on A and 308 each on B.
ON's starter victories finish 41 and 23 rounds later. These are two selected matched keys and
the eight-key cohort remains null, not a universal party result. Useful otherwise unavailable
work and the counterfactual independent jobs displaced by assembly remain open. This execution
study is closed; another local activation witness cannot answer that economic question.
These diagnostic executors are preserved at `bots/miscellaneous/warband-local-party`; the active
clean Warband rebuild does not inherit their native, gameplay-transfer or health qualification.
