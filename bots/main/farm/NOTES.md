# main/farm notes (medium track, farming throughput; 25-26 Oct)

Fresh copy of v53/v54 (hazard 0.3). All new behaviour is behind default-off knobs; with them off the
build replays the base rows bit for bit (80/80 pool keys checked after moving the ledger state out of
`Brain`: extra members alone changed 2/80 games through first-turn fuel).

## Where the rounds go (unopposed, vs anchors/starter-cpp)
Ledger (`debug_farm_ledger`, 74K champion-rounds, 16 pool maps) per 1000 champion-rounds: walking to
targets 430 (big/elk targets 224, wolf 82, harmless 124), camp fighting 300, capture holds 81.
12.2 recalls: capture recalls 8.9 at 84% health with ~80 gold before the reward, travel/shop 2.1,
low-health 1.2; each followed by ~17 rounds of walk-out. Track analysis of 24 replays: 13.5 recalls
per champion per game, ~20 rounds lost each vs walking on (~200-260 rounds per champion).
Camp damage does not drive the recalls (84% health): shopping does. Banking bag/detour are not levers.

## Mechanism
The value-based capture recall (capture_recall 2) charged only 0.3 x the walk from spawn back to the
group, so it shopped on almost every capture. Charging the full walk (capture_recall_walk 1.0) halves
the trips; gem_value_model (gold valued through its items' dps share) stops over-weighting gold.
Fewer top-ups lose to hunters (hx1 -26 wins), so with an enemy seen within 40 rounds/16 tiles the
old 0.3 weight applies (capture_recall_walk_danger 0.3).

## Knobs added
- `debug_farm_ledger` (diagnostic 'L' lines every 250 rounds).
- `arrival_step`: step inside the spawn box on the arrival turn (-9+-7 pool 80; not kept).
- `capture_recall = 3`: counterfactual detour to the next target (-6; not kept).
- `capture_recall_walk_danger`, `capture_recall_danger_rounds`, `capture_recall_danger_radius`.

## Results (experiment names under main/medfarm-*; index.jsonl has the notes)
See the final report in the conversation and the closed experiments' notes.
