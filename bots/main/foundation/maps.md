# Maps: the pool as data (engine 1.0.2, pool sha c3728722)

The map pool as numbers a bot can read at draft time: walking geometry, camp groups, altars, terrain
that matters, and what binds the economy on each map. Owner: economy research (contenders). All
40 pool maps and the 17 legacy versions are covered.

Data (regenerate in this order; nothing here is included by a bot):
- `data/maps.json`: one record per map (`key` = pool id or `legacy/<name>`): size, draft count,
  symmetry, spawn rectangles (max exclusive), spawn-to-spawn walk (closed and with every camp
  cleared), altars with the walk from each spawn, every camp group (composition, health, Strength,
  gems, gold, cooldown, walk to engage from each spawn `dA`/`dB` (adjacent) and `dA_ranged` (within
  3), owner, walk to the nearest altar, bushes within 3), terrain metrics, structural `classes`, and
  `capacity` (the speedrun results of economy.md E12-E15 and the binding class of P4).
- `data/maps/travel.json`: walking distances group to group, spawn to group, group to altar and altar
  to group, per map (side A's blocking; the maps are symmetric).
- `data/maps/legacy-geometry.json`: the 17 legacy maps decoded like `maps/geometry.json`.
- Tools (`tools/economy/`): `decode_legacy.mjs` (node), `atlas.py`, then after the speedrun
  `capacity.py` (python -I). `tables_md.py atlas|speedrun` prints the tables at the end of this page.

| Id | Claim | Status |
| --- | --- | --- |
| P1 | The atlas measures walking with the engine's movement rules on engine-decoded geometry | verified (geometry), computed (distances) |
| P2 | Group rewards are member sums, respawn is the slowest member, on every map | verified (engine 1.0.2) |
| P3 | 76% of the pool's gems sit in elk and monolith groups; cluster gems are 6% | measured (census) |
| P4 | Four binding classes predict what wins the race on each map: damage, travel, respawn, capture | model (speedrun schedules, E14) |
| P5 | BossBattle's only altar has one camp-occupied approach tile | current static geometry; dynamic opening unresolved |
| P6 | Modeled walking, dashes and crystals have different static access | computed for the declared movement graph |
| P7 | Walking width: four maps have a mid-map cut of 1-2 tiles; Chebyshev spawn distance misleads | computed |
| P8 | Spawns touch on 00reverse_spiral and 00rushbait_v2; 00swirl, 01elk_arena and 00fragment are close | computed |
| P9 | Harmless clusters: two gem-rush maps, a few cluster fields, and 1,200 gold of rows at 01yellow_brick_road's spawn | computed |
| P10 | Own, contested and enemy ground by walking ratio | computed |
| P11 | Bush cover: on eight maps nearly every camp has a bush within 3; four maps line the spawn route with 120+ bush tiles | computed |
| P12 | The pool versions are the legacy maps with 13-63% more gems on the same terrain | verified (geometry) |
| P13 | The 57 hash-matched maps need up to 51 groups, 30 members/group and 160 total members | finite static capacity census; not a fuel bound |

### P1. The atlas measures walking with the engine's movement rules on engine-decoded geometry
Claim: Every distance in `data/maps.json` and `data/maps/travel.json` is a breadth-first count of king
moves (one tile a round, M-rules: walls, holes, altars, living camp members and the enemy's spawn
tiles block; own spawn tiles are passable), from the side's whole spawn rectangle. A group is
"engaged" from a tile adjacent to a member (`dX`, melee reach) or within 3 (`dX_ranged`); after a
clear the group's own tiles are free, so travel from a group starts on its member tiles. Spawn
rectangles are [min, max) (00packs: A on x0-2, y0-2, as `arena map` draws).
Evidence: geometry is the official editor engine's (pool: `maps/geometry.json`; legacy:
`tools/economy/decode_legacy.mjs`, the same MapBuilder/Game_API calls, 17 maps); BFS in
`tools/economy/atlas.py`. Coverage: 57 maps, both sides.
Rests on: M7 (monsters never move), M28 (dash rules, used only for the dash inventory).
Status: verified (geometry); distances computed (exact for a lone walker; other units, hidden
occupants and dashes change real paths).
Recheck: any map or engine bundle change (pool sha256 in `docs/reference/current-version.json`).
Consequence: A draft-time planner can read every walk it needs from these tables instead of
spending turn fuel on distance fields; runtime routing still needs vision for occupants.

### P2. Group rewards are member sums, respawn is the slowest member, on every map
Claim: For every group in all 57 maps, `gemValue` = sum of member gems (wolf 4, elk 15, golem 36,
monolith 200, gem cluster 7, gold cluster 0), `goldValue` = sum of member gold (40, 90, 160, 300, 0,
20) and `cooldown` = the largest member respawn (40, 60, 80, 150, 50, 50). No exception exists.
Evidence: `econ.check_rewards` over `maps/geometry.json` and `data/maps/legacy-geometry.json` (zero
mismatches in ~1,000 groups); M16 for the respawn timer.
Rests on: P1, M16.
Status: verified (engine 1.0.2).
Recheck: a patch to the camp table (GameRules "Pick your target"); rerun `check_rewards`.
Consequence: A group's value and cycle follow from its composition alone; a bot can price any
group from `map::camp_groups()` member types without hard-coding per-map values.

### P3. 76% of the pool's gems sit in elk and monolith groups; cluster gems are 6%
Claim: The 836 pool groups split by their strongest member as follows:

| content | groups | share of groups | gems | share of gems | gold | share of gold |
| --- | --- | --- | --- | --- | --- | --- |
| gold clusters only | 200 | 24% | 0 | 0% | 9,600 | 11% |
| gem clusters (any clusters with gems) | 98 | 12% | 1,141 | 6% | 320 | 0% |
| wolf (no elk or bigger) | 262 | 31% | 2,672 | 13% | 20,160 | 24% |
| elk (no golem or monolith) | 239 | 29% | 8,311 | 42% | 42,950 | 50% |
| golem | 10 | 1% | 946 | 5% | 2,400 | 3% |
| monolith | 27 | 3% | 6,946 | 35% | 9,760 | 12% |

The most common compositions are gold2 (128 groups), gold1 wolf1 (84), gem1 (56), gem1 wolf1 (32),
gold1 (32), elk1 gem1 gold1 (28) and wolf1 (28). Golems are rare (10 groups on 6 maps).
Evidence: census of `data/maps.json` (pool set); composition counts in `data/economy/clear_table.json`.
Rests on: P2.
Status: measured (exact count of the current pool).
Recheck: pool change.
Consequence: Gems are earned against health: three quarters of the race is elk and monolith
content, where incoming damage, not travel, prices a gem (economy.md E8-E10). A bot that can only
take wolf and cluster content safely competes for 19% of the pool's gems; one that farms gold-only
groups early buys items with 11% of the gold.

### P4. Four binding classes predict what wins the race on each map: damage, travel, respawn, capture
Claim: Classified by the time split of the best feasible speedrun schedule found (economy.md E13-E14,
items at spawn visits, passive opponent), the 40 pool maps fall into:
- **damage** (fighting plus recall trips >= 45% of champion time; health per gem binds): 00collision,
  00contest, 00corners, 00eight, 00elkring, 00interlock, 00lanes, 00packs, 00quadrants, 00swirl,
  01boss_battle, 01broken_curve, 01starfish, 01the_farm, 01yellow_brick_road (15). Winners here
  convert gold into camp survivability early (Jungle) and take elk/monolith content with parties.
- **travel** (walking to camps and altars >= 55%): 00ambush, 00expanse, 00gem_fields, 00pilgrimage,
  00reverse_spiral, 00road_of_riches, 00stampede, 01concentrated_wealth, 01shortcut, 01starvation
  (10). Winners route: bank where they farm, recall as transport, and keep bags large.
- **respawn** (schedule within 35% of the supply floor; camps are scarce, labour is not): 00fragment,
  01elk_arena, 01hooks (3). Winners are first to each respawn and deny the enemy's.
- **capture** (capture waits >= 15%; many tiny groups): 00gemfight, 00lost_world (2; 01gold_rush is
  at the threshold). Winners spread capturers over the clusters; the race ends early (00lost_world
  schedule 310).
- **mixed** (none dominant): 00great_wall, 00monolith, 00rows, 00rushbait_v2, 00split, 01big_world,
  01corridors, 01fracture, 01gold_rush, 01islands (10).
The structural `classes` field in `data/maps.json` (gem-rush, monster-locked, gold-rich, corridor,
open, close-spawns, far-altars; thresholds in `atlas.py classify`) describes geometry and content;
`capacity.binding` is the economic class above. online.md's coarse classes (touch/gemrich/far/mid)
are Chebyshev- and harmless-rate based; by walking, its "touch" maps 00road_of_riches (spawn walk
242) and 01shortcut (100) are long-walk maps (P7).
Evidence: `data/economy/capacity.json` (`binding`, `time_split`), from `speedrun.py --items spawn`
(all maps, policy grid, `capacity.py` thresholds stated in its docstring). Coverage: 40 pool + 17
legacy maps, side A (symmetric maps), main's speedrun draft.
Rests on: P1, economy.md E6-E14.
Status: model (the class follows from a heuristic schedule; thresholds are judgement; maps near a
threshold, e.g. 00eight 40/39, can flip).
Recheck: a schedule policy change in `speedrun.py`, a camp-table or item patch, a pool change.
Consequence: The class tells a draft-time planner which lever matters: survivability items and
parties (damage), routing and banking geometry (travel), respawn timing and denial (respawn),
capturer spread (capture). A bot that applies one doctrine to every map loses the classes it
does not fit; main's largest gaps are on travel and party-content maps (economy.md E15).

### P5. BossBattle's only altar has one camp-occupied approach tile
Claim: In the pinned initial `01boss_battle` geometry, the only altar is (25,20). Of its eight
  Chebyshev-1 neighbors, only (24,20) is terrain-legal ground: four are holes and three are walls.
  That tile initially holds monolith native camp20. The other member, camp27 at (23,20), is two
  tiles from the altar. Their group `g_1023`, native group16, contains only two monoliths and pays
  400 gems/600 gold after complete clear/capture; the old gem2/monolith2/414-gem description was wrong.
Evidence: current `maps/geometry.json`, the atlas `data/maps.json` group/altar records and map
  `maps/01boss-battle.map26a`, SHA256 `53086e249294aed6f427fe647d578303589a081e953c62a0ff8f20cade9993fa`.
  The retained decoder identity is engine WASM `029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48`.
  The [foundation index](../../../results/contenders/foundation/index.json) binds the eight exact
  neighbors, member identities and source hashes. The [retired Warband selection notes](../../miscellaneous/warband-local-party/NOTES.md)
  record the independent static audit. No new game or native action query is implied by these data checks.
Rests on: P1, M16/M19 (respawn occupancy/submit range), M21/M28 (ordinary landing constraints),
  M49 (historical dead-member occupancy observation).
Status: current pinned static geometry, plus explicitly scoped historical occupancy evidence.
  The atlas treats an entire group's member tiles as released together when calculating its open
  paths: closed initially, spawn-to-spawn walk37 with both removed, altar approach distance19.
  Those model states do not prove that both monoliths must die or that capture must precede every
  tribute. M49's engine1.0.2 observation says dead individual members disappear from `camp_tiles`;
  current death of camp20 alone, a legal landing while camp27 lives, and successful tribute remain
  an unresolved concrete interaction. Special-kit movement also needs its actual API contract.
Recheck: map/landing/occupancy/channel changes, current dynamic single-blocker evidence.
Consequence: Separate reward ownership from bank access and price the actual required blockers.
  Test camp20-only death, legal approach and channel survival under the living companion before
  treating a whole-group clear/capture as necessary for an existing bag to score. Ordinary damage
  does not cancel tribute (M19), while death, stun and enemy locks still matter. After full capture,
  occupancy may prevent group respawn (M16), but it does not by itself confer exclusive bank access.
  The old 103-round gate-adjusted supply floor remains a conditional model output (E12), not a
  proved engine lower bound or a mandate to draft a two-monolith clearing party.

### P6. Modeled walking, dashes and crystals have different static access
Claim: The atlas compares its ordinary walking graph, modeled radius-2 dashes over holes (M28)
  and radius-3 teleport-crystal landings (M21), not every kit or live action state:
- 01islands: 34 of 46 groups and 6 of 10 altars are classified dash-only relative to modeled
  walking; 4 groups are outside the modeled walk/dash reach and classified crystal-only among
  these options. Walking alone reaches 3 own, 2 contested, 3 enemy groups in that static graph.
- 00ambush (2 groups, 62 tiles), 01big_world (4 groups), 01fracture (2), 01shortcut (2 groups, 38
  tiles): small dash-only pockets.
- 00collision: altar (39,20) sits inside a wall chevron with no modeled walk/dash approach;
  the modeled crystal edge reaches it. This does not establish exclusive access or team control.
- 00quadrants: group g_967 has no access in the modeled walking/dash graph.
Evidence: `atlas.py` dash inventory (`terrain.dash_only_*`, `unreachable_groups`), dash
reachability of every altar approach checked separately.
Rests on: P1, M21, M28.
Status: computed static reachability for the stated movement model; dash paths allow an
  intermediate hole per M28. The graph's terrain/camp occupancy assumptions do not resolve live
  champions, reservations, consumable availability, cooldowns, actual kit casting flags or other
  movement abilities. A graph-disconnected point is not proved unreachable by every legal kit.
Recheck: map or movement changes; actual departure/path/landing occupancy and kit legality.
Consequence: These classifications select mobility opportunities to inspect. A bot still needs
  its actual legal arrival, survivable work and conversion path, and the enemy may obtain access
  too. E13's walking-only schedules omit modeled shortcuts; this alone proves neither a best
  mobility draft nor private ownership of an isolated altar.

### P7. Walking width: four maps have a mid-map cut of 1-2 tiles; Chebyshev spawn distance misleads
Claim: The minimum number of vertex-disjoint walking paths between the rings 6 steps out of each spawn
("mid width", capped at 12) is 1 on 01shortcut (cut at (3,36)), 2 on 00road_of_riches ((17,5),
(17,7)), 00rows ((25,22),(25,27)) and 01fracture ((30,34),(30,36)), 3 on 00split and 01starvation,
4 on 00great_wall, 00lost_world, 00quadrants and 01corridors (00swirl: 4 spawn to spawn, its spawns
are too close for the ring measure); 01boss_battle has none until its gate dies (P5); 20 maps are 8
or wider.
Spawn walks range from 0 (adjacent spawns) to 242 (00road_of_riches, whose spawns are 11 apart by
Chebyshev); tortuosity (walk / Chebyshev) is 17.3 there, 7.7 on 01shortcut, 6.1 on 00quadrants and
4.4 on 00gem_fields.
Evidence: `terrain.mid_paths`, `mid_cut_tiles`, `tortuosity` in `data/maps.json` (Edmonds-Karp on the
node-split walking grid in `atlas.py`). `terrain.disjoint_paths` (spawn to spawn) is bounded by the
spawn exits on most maps and is less useful.
Rests on: P1.
Status: computed (exact for the walking grid with every camp alive).
Recheck: map change.
Consequence: Chokepoint maps let one body hold a lane (ambushes, contested captures behind it);
distance classes must use walking, not Chebyshev, distance.

### P8. Spawns touch on 00reverse_spiral and 00rushbait_v2; 00swirl, 01elk_arena and 00fragment are close
Claim: Spawn-to-spawn walking distance is 0 on 00reverse_spiral and 00rushbait_v2 (the rectangles are
adjacent), 12 on 00swirl, 18 on 01elk_arena and 22 on 00fragment; the median pool map is 45.
Evidence: `spawn_to_spawn` in `data/maps.json`.
Rests on: P1.
Status: computed.
Recheck: map change.
Consequence: On these maps every camp is contested from round 1, and the speedrun numbers (passive
opponent) are least informative; the economy there is decided by the combat model.

### P9. Harmless clusters: two gem-rush maps, a few cluster fields, and 1,200 gold of rows at 01yellow_brick_road's spawn
Claim: Cluster-only groups yield gems at a respawn-limited rate (gems / (cooldown + capture_time + 1))
of 4.25 a round on 00lost_world (18 gem groups, 238 gems) and 4.00 on 00road_of_riches (21 groups,
224 gems), 1.25 on 00gem_fields, 00gemfight and 01shortcut, 1.0 on 00great_wall and 00packs, under
0.8 elsewhere and 0 on 27 maps. Gold-only fields: 01yellow_brick_road has two 30-cluster rows of 600
gold each adjacent to each spawn (y12 and y17), 01elk_arena 1,080 gold in 28 small groups, 00pilgrimage
720, 00contest 640, 00stampede 560. Clusters never attack (M11) and die to two or three hits each.
Evidence: `metrics.harmless_gem_rate`, group records in `data/maps.json`; the yellow-brick rows were
farmed by the economy probes (a solo Vendetta clears one row in ~58 rounds).
Rests on: P1, P2, M11.
Status: computed (rates are ceilings: no travel, no competition).
Recheck: map change.
Consequence: Gem-rush maps reward spreading capturers over clusters (the capture wait, not the
fight, is the cost: P4 capture class); the yellow-brick rows are an item rush (four emblems for one
champion inside ~60 rounds) that turns that map into a damage-class map won by the better-equipped
team.

### P10. Own, contested and enemy ground by walking ratio
Claim: With owner = A if (dA+1)/(dB+1) <= 0.8, B if >= 1.25, else contested, the pool's 836 groups
are 265 own, 257 contested and 265 enemy for each side, 49 unreachable by walking (01islands,
01boss_battle's far half before the gate, 00quadrants g_967). The contested share is highest on
00swirl (18 of 22), 00lost_world (22 of 34), 00lanes (18 of 42), 01starfish (12 of 16) and 01corridors
(12 of 12).
Evidence: `groups[].owner`, `ratio` in `data/maps.json`.
Rests on: P1.
Status: computed (geometry only; who actually holds ground depends on play).
Recheck: map change; the 0.8/1.25 cut is a convention.
Consequence: "Own half" plans (economy.md E13 half schedules) give up contested groups; on
contested-heavy maps the half schedule is much slower (00packs, 01hooks: maps.md "Capacity"), so
contested groups decide those races.

### P11. Bush cover: on eight maps nearly every camp has a bush within 3; four maps line the spawn route with 120+ bush tiles
Claim: Bushes are 22% of open ground on 01yellow_brick_road, 16% on 00stampede, 13-14% on 00collision
and 01islands, 12% on 01starfish and under 9% elsewhere. Camps with a bush within 3 tiles: all of them
on 01yellow_brick_road (28), 01starfish (16), 01corridors (12) and 01hooks (12), most on 01fracture
(24/28), 01islands (36/46), 01broken_curve (18/22) and 01elk_arena (21/29); none on 00collision,
00elkring, 00monolith and 00rushbait_v2. Bush tiles on a shortest spawn-to-spawn path: 252 on
00stampede, 186 on 01big_world, 144 on 00lanes, 124 on 01concentrated_wealth; 0 on 00collision,
00quadrants, 00rushbait_v2 and 01yellow_brick_road.
Evidence: `terrain.bush_frac`, `bush_on_spawn_path`, `groups[].bush_near` in `data/maps.json`.
Rests on: P1, M44, M46.
Status: computed.
Recheck: map change.
Consequence: Where camps sit beside bushes, a capturer's six rounds of stillness (M14) can be
watched or ambushed unseen; warding bushes near target groups matters most on these maps.

### P12. The pool versions are the legacy maps with 13-63% more gems on the same terrain
Claim: Each of the 17 legacy maps has a pool twin with the same walls and holes; the pool versions
carry more gems (00collision 182 -> 280, 00rushbait_v2 160 -> 258, 00elkring 150 -> 220, 00interlock
144 -> 200, 00lanes 210 -> 294, 00pilgrimage 134 -> 184; 00contest unchanged in gems) and more gold;
00expanse went from 3 to 5 picks, 00monolith from 22 to 20 groups. Speedrun schedules are 26% faster
on the pool versions (mean 674 vs 915 with items).
Evidence: legacy/pool comparison over `data/maps.json`; schedules in `data/economy/capacity.json`.
Rests on: P1, P2.
Status: verified (geometry).
Recheck: the next pool revision.
Consequence: The legacy set is a fair generalisation check of geometry-driven logic, but a slower
economy: per-map tuning learned on one set does not transfer as numbers, only as structure.

## Tables

### Pool atlas (side A; `capacity` columns are in the next table)
Columns: spawn walk to a tile next to the enemy spawn ("gated (n)": only with camps cleared);
altars (count, nearest walk); groups own/contested/theirs (P10); gem cap = sum of gems / (cooldown +
6) over reachable groups (a respawn ceiling with zero clear time); harmless gems per round (P9);
share of gems in golem/monolith groups; mid width (P7, 12+ = capped).

| map | size | draft | spawn walk | altars (nearest) | groups own/cont/theirs | gem cap /rd | harmless gems /rd | big-gem share | mid width | classes |
|---|---|---|---|---|---|---|---|---|---|---|
| 00ambush | 100x100 | 5 | 122 | 4 (37) | 2/6/2 | 3.67 | 0.00 | 0.00 | 12+ | open |
| 00collision | 41x41 | 3 | 33 | 2 (21) | 8/4/8 | 4.44 | 0.00 | 0.00 | 11 | open |
| 00contest | 50x50 | 5 | 38 | 2 (19) | 3/8/3 | 2.56 | 0.00 | 1.00 | 12+ | monster-locked, open |
| 00corners | 71x71 | 5 | 39 | 5 (19) | 11/8/11 | 6.16 | 0.00 | 0.00 | 12+ | open |
| 00eight | 41x30 | 5 | 45 | 1 (22) | 4/0/4 | 3.59 | 0.00 | 0.00 | 12+ | open |
| 00elkring | 30x30 | 5 | 30 | 2 (23) | 5/0/5 | 3.33 | 0.00 | 0.00 | 12+ | open |
| 00expanse | 80x80 | 5 | 84 | 10 (7) | 8/8/8 | 5.70 | 0.00 | 0.00 | 12+ | open |
| 00fragment | 30x30 | 3 | 22 | 2 (12) | 3/0/3 | 1.07 | 0.00 | 0.00 | 7 | - |
| 00gem_fields | 50x40 | 5 | 40 | 4 (15) | 8/2/8 | 5.95 | 1.25 | 0.00 | 7 | corridor |
| 00gemfight | 30x30 | 4 | 45 | 2 (31) | 9/1/9 | 3.45 | 1.25 | 0.00 | 8 | corridor |
| 00great_wall | 50x40 | 5 | 84 | 4 (56) | 7/3/7 | 13.41 | 1.00 | 0.77 | 4 | monster-locked, corridor, far-altars |
| 00interlock | 40x40 | 4 | 40 | 4 (17) | 5/4/5 | 3.16 | 0.00 | 0.00 | 12+ | open |
| 00lanes | 90x30 | 5 | 80 | 18 (7) | 12/18/12 | 4.89 | 0.00 | 0.00 | 12+ | open |
| 00lost_world | 35x31 | 5 | 25 | 3 (26) | 6/22/6 | 7.68 | 4.25 | 0.45 | 4 | gem-rush, monster-locked |
| 00monolith | 50x50 | 5 | 48 | 6 (25) | 6/8/6 | 6.01 | 0.00 | 0.76 | 12+ | monster-locked, open |
| 00packs | 31x31 | 3 | 25 | 6 (3) | 2/4/2 | 2.48 | 1.00 | 0.00 | 12+ | open |
| 00pilgrimage | 50x65 | 5 | 122 | 4 (54) | 4/15/4 | 2.87 | 0.00 | 0.00 | 6 | corridor, far-altars |
| 00quadrants | 45x45 | 5 | 49 | 4 (15) | 5/10/5 | 11.06 | 0.00 | 0.33 | 4 | corridor |
| 00reverse_spiral | 30x30 | 4 | 0 | 2 (51) | 7/0/7 | 4.83 | 0.75 | 0.43 | 12+ | monster-locked, open, close-spawns, far-altars |
| 00road_of_riches | 40x40 | 5 | 242 | 2 (120) | 21/9/21 | 13.34 | 4.00 | 0.56 | 2 | gem-rush, monster-locked, corridor, far-altars |
| 00rows | 50x50 | 4 | 72 | 2 (10) | 4/1/4 | 5.05 | 0.00 | 0.81 | 2 | monster-locked, corridor, far-altars |
| 00rushbait_v2 | 30x80 | 4 | 0 | 2 (37) | 9/0/9 | 4.05 | 0.00 | 0.00 | 12+ | open, close-spawns, far-altars |
| 00split | 45x35 | 5 | 83 | 3 (23) | 15/2/15 | 9.10 | 0.75 | 0.65 | 3 | monster-locked, corridor |
| 00stampede | 60x50 | 5 | 54 | 2 (28) | 5/3/5 | 2.42 | 0.00 | 0.00 | 7 | far-altars |
| 00swirl | 45x30 | 4 | 12 | 2 (2) | 2/18/2 | 9.97 | 0.00 | 0.00 | 4 | close-spawns, far-altars |
| 01big_world | 100x100 | 5 | 138 | 6 (39) | 8/4/8 | 8.13 | 0.50 | 0.59 | 6 | monster-locked, corridor, far-altars |
| 01boss_battle | 50x41 | 5 | gated (37) | 1 (-) | 15/1/15 | 14.43 | 0.00 | 0.34 | 0 | corridor, close-spawns |
| 01broken_curve | 40x40 | 3 | 35 | 4 (18) | 6/10/6 | 8.01 | 0.50 | 0.54 | 8 | monster-locked, open |
| 01concentrated_wealth | 61x61 | 5 | 176 | 2 (85) | 4/3/4 | 2.88 | 0.00 | 0.00 | 5 | corridor, far-altars |
| 01corridors | 51x40 | 5 | 52 | 2 (25) | 0/12/0 | 7.87 | 0.00 | 0.57 | 4 | monster-locked, far-altars |
| 01elk_arena | 50x50 | 5 | 18 | 2 (6) | 12/5/12 | 1.09 | 0.00 | 0.00 | 12+ | gold-rich, open, close-spawns, far-altars |
| 01fracture | 61x71 | 5 | 159 | 3 (75) | 9/8/9 | 9.07 | 0.00 | 0.49 | 2 | monster-locked, corridor, far-altars |
| 01gold_rush | 31x31 | 3 | 56 | 4 (11) | 3/9/3 | 2.46 | 0.00 | 0.75 | 5 | monster-locked, corridor |
| 01hooks | 31x31 | 4 | 33 | 1 (17) | 3/6/3 | 1.33 | 0.00 | 0.00 | 10 | open |
| 01islands | 60x60 | 5 | 62 | 10 (2) | 3/2/3 | 3.58 | 0.00 | 0.00 | 12+ | open |
| 01shortcut | 40x40 | 4 | 100 | 2 (110) | 4/11/4 | 10.86 | 1.25 | 0.48 | 1 | monster-locked, corridor, far-altars |
| 01starfish | 50x50 | 5 | 40 | 2 (18) | 2/12/2 | 5.49 | 0.00 | 0.00 | 6 | - |
| 01starvation | 80x80 | 5 | 182 | 2 (99) | 2/10/2 | 4.71 | 0.50 | 0.75 | 3 | monster-locked, corridor, far-altars |
| 01the_farm | 50x40 | 3 | 40 | 6 (30) | 10/8/10 | 13.39 | 0.50 | 0.38 | 9 | open |
| 01yellow_brick_road | 40x30 | 5 | 28 | 10 (7) | 13/2/13 | 7.58 | 0.00 | 0.00 | 12+ | open |

### Legacy atlas

| map | size | draft | spawn walk | altars (nearest) | groups own/cont/theirs | gem cap /rd | harmless gems /rd | big-gem share | mid width | classes |
|---|---|---|---|---|---|---|---|---|---|---|
| legacy/ambush | 100x100 | 5 | 122 | 4 (37) | 2/6/2 | 3.24 | 0.00 | 0.00 | 12+ | open |
| legacy/collision | 41x41 | 3 | 33 | 2 (21) | 8/4/8 | 2.97 | 0.00 | 0.00 | 12+ | open |
| legacy/contest | 50x50 | 5 | 38 | 2 (19) | 3/8/3 | 2.56 | 0.00 | 1.00 | 12+ | monster-locked, open |
| legacy/corners | 71x71 | 5 | 39 | 5 (19) | 11/8/11 | 5.17 | 0.00 | 0.00 | 12+ | open |
| legacy/eight | 41x30 | 5 | 45 | 1 (22) | 3/2/3 | 3.01 | 0.00 | 0.00 | 12+ | open |
| legacy/elkring | 30x30 | 5 | 30 | 2 (23) | 5/0/5 | 2.27 | 0.00 | 0.00 | 12+ | open |
| legacy/expanse | 80x80 | 3 | 84 | 10 (7) | 8/8/8 | 5.32 | 0.00 | 0.00 | 12+ | open |
| legacy/fragment | 30x30 | 3 | 22 | 2 (12) | 3/0/3 | 0.86 | 0.00 | 0.00 | 9 | open |
| legacy/interlock | 40x40 | 4 | 40 | 4 (17) | 5/4/5 | 2.34 | 0.00 | 0.00 | 12+ | open |
| legacy/lanes | 90x30 | 5 | 80 | 18 (7) | 12/18/12 | 3.97 | 0.00 | 0.00 | 12+ | open |
| legacy/monolith | 50x50 | 5 | 48 | 6 (25) | 6/10/6 | 5.30 | 0.00 | 0.83 | 12+ | monster-locked, open |
| legacy/packs | 31x31 | 3 | 25 | 6 (3) | 2/4/2 | 2.10 | 0.50 | 0.00 | 12+ | open |
| legacy/pilgrimage | 50x65 | 5 | 122 | 4 (54) | 4/15/4 | 2.23 | 0.00 | 0.00 | 7 | corridor, far-altars |
| legacy/rows | 50x50 | 4 | 72 | 2 (10) | 4/1/4 | 4.22 | 0.00 | 0.85 | 4 | monster-locked, corridor, far-altars |
| legacy/rushbait_v2 | 30x80 | 4 | 0 | 2 (37) | 9/0/9 | 2.69 | 0.00 | 0.00 | 12+ | open, close-spawns, far-altars |
| legacy/stampede | 60x50 | 5 | 54 | 2 (28) | 5/3/5 | 2.00 | 0.00 | 0.00 | 7 | far-altars |
| legacy/swirl | 45x30 | 4 | 12 | 2 (2) | 2/18/2 | 8.71 | 0.00 | 0.00 | 4 | close-spawns, far-altars |

### Capacity per pool map (economy.md E12-E15)
Rounds to 1,000 banked gems against a passive opponent, main's speedrun draft (Heart, Marcella, Ryth,
Vendetta, Varg; first `draft` picks), side A. floor = supply floor (E13); base = best schedule at
base stats; items = best schedule buying at spawn visits (or instantly, whichever is faster); half =
items, own-half groups only; main est / main actual = `.cache/main/analysis/economy/table.md`
(main's model and main/main@8a7337f4849f vs the starter, mean of both sides); binds = the two
largest shares of champion time in the items schedule.

| map | floor | base | items | half | main est | main actual | binds (items run: largest shares) |
|---|---|---|---|---|---|---|---|
| 00ambush | 338 | 1427 | 780 | 905 | 948 | 1413 | travel 54%, fight 31% |
| 00collision | 251 | 1163 | 676 | 725 | 885 | 1026 | fight 41%, travel 38% |
| 00contest | 365 | 1515 | 1116 | 1116 | 667 | 1054 | fight 70%, travel 19% |
| 00corners | 183 | 905 | 495 | 587 | 598 | 740 | fight 46%, travel 31% |
| 00eight | 298 | 986 | 545 | 707 | 699 | 784 | fight 40%, travel 39% |
| 00elkring | 298 | 760 | 466 | 686 | 553 | 670 | fight 42%, travel 34% |
| 00expanse | 219 | 1199 | 694 | 743 | 831 | 922 | travel 53%, fight 28% |
| 00fragment | 937 | 1484 | 1158 | 1931 | 1205 | 1381 | travel 41%, fight 27% |
| 00gem_fields | 182 | 433 | 373 | 479 | 448 | 513 | travel 47%, fight 31% |
| 00gemfight | 317 | 596 | 529 | 583 | 618 | 762 | travel 39%, fight 27% |
| 00great_wall | 72 | 809 | 646 | 724 | 883 | 1048 | travel 49%, fight 30% |
| 00interlock | 316 | 1182 | 677 | 754 | 759 | 961 | fight 42%, travel 40% |
| 00lanes | 228 | 867 | 573 | 693 | 735 | 1039 | fight 42%, travel 37% |
| 00lost_world | 145 | 310 | 310 | 337 | 349 | 400 | travel 46%, capture 22% |
| 00monolith | 199 | 958 | 614 | 772 | 637 | 944 | travel 40%, fight 32% |
| 00packs | 419 | 826 | 596 | 917 | 719 | 904 | fight 39%, travel 36% |
| 00pilgrimage | 437 | 1323 | 925 | 929 | 969 | 1451 | travel 67%, fight 13% |
| 00quadrants | 107 | 411 | 344 | 359 | 435 | 482 | fight 42%, travel 33% |
| 00reverse_spiral | 254 | 1058 | 744 | 827 | 865 | 1275 | travel 50%, fight 33% |
| 00road_of_riches | 209 | 557 | 532 | 567 | 731 | 937 | travel 53%, fight 17% |
| 00rows | 221 | 1573 | 932 | 1234 | 908 | 1214 | travel 41%, fight 35% |
| 00rushbait_v2 | 277 | 924 | 639 | 743 | 790 | 1098 | travel 42%, fight 39% |
| 00split | 116 | 609 | 471 | 542 | 615 | 760 | travel 40%, fight 36% |
| 00stampede | 516 | 1486 | 1025 | 1025 | 1222 | 2000 | travel 60%, fight 25% |
| 00swirl | 133 | 1150 | 522 | 598 | 695 | 689 | fight 45%, travel 31% |
| 01big_world | 207 | 1360 | 751 | 746 | 753 | 1058 | travel 42%, fight 35% |
| 01boss_battle | 103 | 2000 | 440 | 440 | 561 | 850 | fight 44%, travel 30% |
| 01broken_curve | 132 | 1027 | 630 | 684 | 804 | 928 | fight 44%, travel 33% |
| 01concentrated_wealth | 449 | 1105 | 854 | 887 | 896 | 1022 | travel 63%, fight 19% |
| 01corridors | 151 | 1132 | 599 | 599 | 720 | 1064 | travel 43%, fight 37% |
| 01elk_arena | 873 | 985 | 885 | 885 | 972 | 1152 | wait 39%, travel 17% |
| 01fracture | 165 | 1449 | 663 | 735 | 736 | 972 | travel 50%, fight 30% |
| 01gold_rush | 353 | 1465 | 974 | 1169 | 965 | 1154 | fight 39%, travel 35% |
| 01hooks | 751 | 947 | 808 | 1507 | 848 | 1010 | wait 28%, travel 26% |
| 01islands | 306 | 1079 | 663 | 763 | 749 | 795 | travel 47%, fight 32% |
| 01shortcut | 181 | 1178 | 848 | 861 | 1036 | 1289 | travel 58%, fight 25% |
| 01starfish | 188 | 916 | 567 | 567 | 645 | 794 | fight 44%, travel 36% |
| 01starvation | 272 | 1366 | 968 | 1033 | 861 | 1462 | travel 50%, fight 36% |
| 01the_farm | 64 | 988 | 604 | 586 | 804 | 986 | fight 40%, travel 37% |
| 01yellow_brick_road | 134 | 659 | 339 | 405 | 461 | 754 | fight 39%, travel 27% |

Legacy maps (same columns, no main figures): see `data/economy/capacity.json`; the items schedules
average 915 rounds (pool 674).

### P13. Finite map capacities are observation bounds, not fuel or infeasibility certificates

Claim: For exactly the 40 pool and 17 legacy map files in the existing raw geometry and atlas,
let `G` be group count, `K` maximum members in one group and `M` total members. Their maxima are:

| Map set | Maps | Max G | Max K | Max M |
| --- | ---: | ---: | ---: | ---: |
| Pool | 40 | 51, Road of Riches | 30, Yellow Brick Road | 160, Yellow Brick Road |
| Legacy | 17 | 42, Lanes | 12, Stampede | 80, Swirl |
| Combined | 57 | 51 | 30 | 160 |

The 30-member Yellow Brick Road groups `g_485` and `g_685` are each entirely gold clusters.
Packs has two legitimate nine-member harmless groups, each five gold and four gem clusters.
Concentrated Wealth has a 21-member group and Stampede a 20-member group. A per-group cap of eight
therefore removes real funding and gem work; it is not merely a cap on unusual combat targets.

Evidence: [foundation index](../../../results/contenders/foundation/index.json),
`integrated_studies.finite_map_capacity_census`, retains all 57 per-map counts, compositions of
largest groups, exact current map hashes and source hashes. Every map file matches its raw
geometry's recorded SHA256, and every group size agrees with the atlas. The pool geometry records
current decoder `029ecd29...`; the legacy export retains historical decoder `742919b4...`.
No new engine query, game or native revalidation of that legacy decoder was performed.

Status: exact reduction of those hash-matched static records. This is not a universal engine
limit, a complete live-World observation or a native cost certificate. Runtime state availability,
clock/fuel truncation, future maps and any new camp-generating mechanic need separate handling.
Recheck: any map/source hash or camp-generation change; compare actual observed counts at runtime.
Consequence: Allocate or explicitly handle at least these dimensions when claiming completeness
over this map set. An overflow or bounded observation/search cutoff must remain unknown/incomplete,
not an empty group, permanently infeasible job or a fabricated complete World. These finite
cardinalities can bound loop iterations; T4 still requires charged per-iteration/host-call bounds.
