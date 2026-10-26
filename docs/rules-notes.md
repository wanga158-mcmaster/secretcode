# Rules and mechanics notes

## Pending SDK 4 / engine 2.0.0 epoch

The earlier website/SDK mismatch is superseded by the staged CLI/SDK 4.0.0 bundle. Publication
and validation remain the integrator's responsibility; [current-version.json](reference/current-version.json)
identifies the installed epoch. Authoritative staged inputs are the
[official handbook](../.cache/vendor-update-20261025/project4/docs/reference/GameRules.md),
[C++ SDK](../.cache/vendor-update-20261025/project4/sdk/cpp/cambc.h) and
[queried catalogue](../.cache/vendor-update-20261025/catalog4-verified/runtime.json).
The [semantic comparison](../.cache/vendor-update-20261025/rules4-audit/catalog-delta.json)
pins these exact bytes and retains the old catalogue. This section reports source/catalogue
observations; it does not establish equipped native gameplay or fuel costs.

- **Equipment expands from 42 to 84 item kinds (7 to 14 families).** Monster emblems change
  from 10% to 15% camp damage; Treasure changes from 20% to 10% bonus gold. Existing item IDs
  remain in the first 42 entries; inventory and item-enumeration bounds must include all 84.
- **New families (handbook descriptions):** Tribute shortens tribute by 33%, rounded up with
  a one-turn minimum; Capture is described as capping the remaining allied capture timer at
  three, including mid-capture entry (the patch limits this to the start; see below);
  Prospector adds 25% to the actor's cluster-reward share, rounded down; Hunter adds 50 kill
  gold before gold bonuses (one linked-duo reward); Skirmish adds 5% champion damage;
  Greed combines 25 shards/750 gold into 35 stat points; Collectors adds 1% damage per unique
  completed family, including itself. These are handbook rules plus queried magnitudes,
  not measured income or damage improvements.
- **Capture progress is now directly observable.** `camp_group::capture_state()` returns a
  timer and team-relative `no_capture_available`, `ally_capturing`, `contested`,
  `enemy_capturing` or `capture_available`, requiring group visibility. This is separate from
  the unchanged alive/reward-available/respawning lifecycle. The handbook says Capture's
  shorter timer persists after its holder leaves if another ally maintains capture; empty
  or contested occupancy still resets progress. The generic five-turn rule below therefore
  needs an equipment qualification in this new epoch.
- **Capture timing has conflicting vendor prose.** The newly retrieved
  [2.0.0 patch](reference/updates/2026-10-25-engine-2.0.0/patch-2.0.0.md) says the three-turn
  Capture benefit applies when a holder is present as capture begins. The exported handbook
  also describes applying the cap on mid-capture entry. Neither statement becomes a measured
  equipped phase rule here. Use runtime observations and an actual equipped native entry/exit
  witness; the generic queried `capture_time=5` does not resolve this disagreement.
- **First-draft fuel changed.** The same patch announces a tenfold increase. Exact retained
  native `.fuel` headers give CLI4 **10,000,000** fuel on the first draft turn, versus
  CLI3's **1,000,000**. Both fixtures report 100,000 on later draft turns, 200,000 on each
  champion's first gameplay turn and 100,000 ordinarily. The
  [header comparison](reference/updates/2026-10-25-engine-2.0.0/draft-fuel-headers.json)
  binds both replays and the decoded values. The initial staged runner granted one
  million: its 140-key outcome/round/gem/recorded-health match did not exercise this budget
  boundary. The corrected runner's
  [expensive-draft discriminator](../.cache/vendor-update-20261025/runner4-audit/fuel4/result.json)
  now matches native fuel use `[2013483,3886,3900,9988]` on both sides; its first callback
  completes the two-million-fuel workload while the old one-million control cannot. The
  advertised vector, execution budgets and emitted headers share one implementation.
  Runner eligibility remains suspended until fresh full-pool parity. These fixtures do not
  prove universal fuel equality.
- **Champion numbers also change.** Varg Strength 40→45; Vesper 55→45; Cassi Focus 70→100;
  Issac Strength 40→45; Willow Focus 120→140. Growth changes: Ryth Strength 5→4.5;
  Stellar Focus 7→8.5; Prism Health 60→65; Marcella Strength 5→4.5 and Focus 15→14;
  Heart Strength 3.5→3 and Focus 12→10. Ren's stun multiplier changes .75→1.1 and
  Tavi's 1.4→1.6. The 13 champion-passive records are unchanged.
- **Stable queried values are narrower evidence than stable gameplay.** All 29 numeric game
  settings and all six camp records match the old catalogue, including `capture_time=5`.
  New equipment can still change capture, tribute, damage and rewards. The official pool
  expands from 40 to 70 maps; old 80-key floor panels are historical coverage. The handbook's
  cluster table still says range zero, while the new queried records retain range three
  and zero Strength for both clusters; use queried fields rather than that table.

Before changing forecasts or granting coverage, discriminate equipped Capture entry/exit
timing and tribute completion with actual phase receipts; Prospector split/rounding,
Hunter linked-duo rewards and Collectors damage composition also need native witnesses.
Re-measure fuel and health under the new bundle. None of the 1.0.3 probe costs, survival
certificates, native interaction witnesses or Main wins below transfers automatically.

## Historical engine 1.0.3 notes

The following measurements describe CLI/SDK **3.0.5**, engine **1.0.3**. For the current
installed bundle, consult [current-version.json](reference/current-version.json). The source references are the
[rules](reference/GameRules.md), the [C++ API](reference/BotAPICpp.md), the pinned
`sdk/cpp/cambc.h`, the engine's runtime catalog ([runtime-catalog.json](reference/runtime-catalog.json))
and the [patch notes](reference/updates/). Rule prose can be incomplete or lag the engine, so
queried runtime settings win; read every balance number at runtime. The 22 October 1.0.3
capture matches the preceding catalog's settings, champions, camps, items and passives.
Fresh official handbook exports clarify that sleeping camps still process timers: zero-timer
buffs expire before their action; cooldowns and buffs, including newly applied ones, decrement
at turn end. Camps sleep after two turns without targets in range.

## Winning
Clear every monster in a camp group, hold one of its spawn tiles uncontested until the capture
finishes, then bank the gems at an altar. Carried gems do not score. The first team to 1,000
tributed gems wins. At 2,000 rounds the tiebreaks are tributed gems, then lifetime earned gold,
then champion kills; an exact tie favours Team B, so always evaluate both sides.

## Rules that change decisions
| Mechanic | Consequence |
| --- | --- |
| A group is captured after five uncontested capture rounds; extra allies do not speed it up. | Hold a cleared camp tile until the reward arrives; do not send everyone to one group. |
| Leaving or enemy presence resets capture; an occupied spawn tile delays the group's respawn. | Finish captures before departing. Presence alone denies an enemy capture. |
| Camps act at round start, before champion turns; damage wakes the whole group. | Estimate the whole group's damage before engaging: one hit can kill before your turn. |
| Monsters do not chase; they see through bushes and have different attack ranges. | Retreat outside the queried range before recalling; bushes do not make a safe recall. |
| Melee champions can walk and attack; for ranged ones, walking and basic attacking share an allowance. | Re-query action availability after every successful action; attack resets restore it. |
| One successful ability cast per turn across both slots; some abilities exclude ordinary movement. | Pick the single most useful cast; cast before moving when needed; use SDK ready/target masks. |
| Recall takes five owner turn-end ticks, and damage cancels it. Arrival is at turn start; spawn heals at turn end. | Flee first, then recall; stay on spawn for the arrival turn. Dead champions can shop. Damaging a recalling enemy cancels its recall. |
| Tribute takes `max(1, ceil(gems / 10))` ticks and completes at a later owner turn start. Damage does not cancel it; stun, death or an altar lock does. | Protect the channel. Stunning or killing a channelling enemy denies its bank. |
| Capture resolves before channels; tribute completion takes the whole current bag. | Do not cancel a tribute because a simultaneous capture changes the bag. |
| An altar lock lets only its owner's team tribute; enemy locks take five basic attacks to remove (`altar_lock_hits`). | A locked tribute attempt becomes an attack on the lock. Top online teams buy about four locks a game. |
| Each champion sees with personal vision plus its own ward; allies do not share vision. | Unseen camp state is unknown; read live group state only when a member spawn is visible. |
| An unseen occupant can make a legal-looking move return `false` without consuming movement. | Refresh vision and obstacles and try another step; known invalid destinations throw. |
| Every matching shard adds stats, but duplicate emblem family passives do not stack. | Buy one emblem per family, then raw stats or another family. |
| Tavi shops in the field; field Health purchases do not heal. | Shopping is not recovery; consumables still need spawn or death. |

Handbook corrections (engine 1.0.2): locks need five successful attacks; Heart of Ruin's shard
growth is 3.5 Strength / 12 Focus; Prism gains 1.5 Focus per carried gem; non-attacking clusters
deal zero damage (queried range 3, Strength 0) despite the overview's "all monsters range three";
teleport landing rules are stated more precisely in the detailed sections than in the feature
table. The 1.0.2 patch weakened Stellar, Heart of Ruin and Ren and strengthened Vesper Vale,
Vendetta and Tavi ([review](reference/updates/2026-10-21-engine-1.0.2/README.md)).

## Settings (engine 1.0.3 catalog)
capture_time 5, camp_sleep_time 2, respawn_time 60, recall_time 5, submit multiplier 0.1 (min 1),
submit range 1, max_rounds 2000, winning_gems 1000, kill_gold 100, assist 0, passive_gold 0,
spawn_healing 1.0 (fraction of max HP per round on spawn), ward cooldown 10, ward vision 6, ward
placement 2, altar_lock_hits 5, tp_crystal_time 3. Shards cost 30 gold, consumables 10. All
abilities have cooldown 8; ranged attack range 3, melee 1, spells range 4. Ability target bits:
1 camps, 2 enemies, 4 allies, 8 self, 16 ground.

## Verified by probes
- Camps keep damage while asleep, so partial clears are permanent progress and big groups can be
  sieged over several trips.
- Camps stay awake while any champion is within range, visible or not; they fall asleep two rounds
  after the last champion leaves range. A champion behind a bush row that cannot see the camps
  still in range keeps taking damage, and its recall keeps being cancelled.
- Melee basic attacks reach Chebyshev distance 1. At base, attack damage equals current Strength
  (no camp multiplier).
- Dashes take up to `range` 8-directional steps. Paths may cross holes but not walls, altars,
  units or enemy spawn tiles, and the landing tile must be free.
- Turn order: each actor completes owner start, its actions and owner end before the next actor
  starts, and which team moves first alternates by round. Owner start resolves timers that
  reached zero, so a tribute completes with the bag held at that owner start; owner end only
  decrements timers.
- Basic-hit damage is applied by repeated float32 subtraction (bit-exact over a 9-hit native chain
  at 156.2 damage). Count hits by simulating the subtraction: a product formula can undercount
  (649 HP at 129.8 damage needs 6 hits, not 5).
- An attack reset consumes the turn's single cast. A sleeping injured camp keeps its exact HP
  indefinitely (checked over 176 sub-steps).

## Fuel
The installed CLI3 epoch gives C++ bots 100,000 fuel per turn, ten times that on the first
draft turn and twice on each champion's first gameplay turn. Staged CLI4 increases the first
draft allocation to 10,000,000 as recorded above; do not apply the old draft multiplier to
that epoch. Exhaustion suspends the bot until its next owner turn; the
function resumes with locals from the old turn, so observations and intended actions go stale.
Unused fuel does not carry over. Exhaustion is not a crash: count `fuelExhausted` events (below).

Measured costs (release, 100x100 map):
| Operation | Fuel |
| --- | --- |
| simple query (position, health, state, gold) | 50-80 |
| `fuel_remaining()` | ~16 |
| bitboard construct / binary op | ~410 |
| `vision::champion_tiles()` | ~970 |
| bitboard `positions()` (540 bits) | ~5,400 |
| `vision::camps()` with nothing visible | ~2,400 |
| `map::camp_groups()` (12 groups) | ~16,900 (cache once) |
| `vision::camp_groups()` | ~17,700 (avoid) |
| `cambc::champions(ally)` | ~3,700 |
| `distance_field.compute` on 100x100 | ~25,200 (~2.5 per tile) |
| `distance_field.get` | ~62 |
| `distance_field.export_linear_buffer` | ~22,900 (avoid) |
| `reachable_tiles(p, 1)` | ~500 |
| SDK static init on a champion's first turn | ~11,300 |
| one formatted log line | ~1,000 |

Treat these costs as measurements, not atomic safety bounds. Territory's later stage logger
exhausted despite a 20k emission floor. Moving retained-packet output to a dedicated fresh owner
turn at 90k eliminated technical flags in four matched Ambush/Big World games; the outer
dispatch also skipped ordinary receipt logging, which otherwise ran after `turn()` returned.
Those deliberately skipped diagnostic turns still need an explicit behavior comparison.
The same clean packets measured 28,468--34,253 fuel for that runtime's current-field operation;
this is a scoped caller cost, not a replacement universal SDK constant. See the source-bound
[measurement](../results/population/territory-live/native-clean.json).

SDK getter syntax does not imply the same cost or lifetime. Camp-group gold/gem getters read
stored fields, while `champion::item_count()` performs a checked import and returns an owning
equipment map. Conveyor's default-off invocation-local reuse removes two inventory imports per
refresh in fifteen actual-caller cases, without retaining data across refreshes or purchases.
The permanent-equipment ownership contract supports that lifetime; the fixture does not test
native suspension. Two identical recorded command histories show median ordinary-entry fuel
changes of -878, but nineteen entries cost more. This is a scoped native cost comparison, not
a per-query tariff or monotonic saving guarantee. The four-condition trial removes one of eight
exhaustions and leaves seven, so health remains blocked. See the
[measurement and limits](../results/population/conveyor-inventory/measurement-v2.json).

## Match outputs
- Statistics JSON (`--statistics PATH --json`): `match.outcome`, `match.roundsCompleted`,
  `teams[].submittedGems`, `champions[].metrics` (earnedGold, kills, deaths, ...),
  `roster.champions` (team, draftPick), `diagnostics.botFaults`, and `events` groups by `kind`
  (`fuelExhausted`, `campClear`, `death` with entry `context`, ...). Fuel exhaustion = the sum of
  `entries` lengths of `fuelExhausted` groups per `owner.team`; empty groups exist for champions
  that never exhausted. `match.mapHash` is the sha256 of the `.map26a` file.
- Games cut short with `--rounds` export no statistics (run-batch reports `TRUNCATED`); arena
  records them as truncated draws with only fuel and error lines measured, and the ladder
  ignores them. A runner failure leaves `<replay>.error.log` next to the intended replay.
- A replay (`*.repl26a`) is a zip with `team-{a,b}.stdout`, `.debug` and `.fuel`. The `.fuel`
  stream is an 8-byte magic (`a62leuf\0`), a 5-byte header, then little-endian u64 values: 8
  budget values (draft first/other, play first/other, twice), then the fuel used per own-team turn
  in order. A suspended turn shows the full budget.

## Behaviour checks worth having in every bot's tests
1. Clear a multi-monster group, hold the capture, observe the reward, leave the region.
2. Carry gems to an unlocked altar, start a tribute, keep the channel and see the score rise.
3. Recall while low and out of active monster range, wait on spawn until healed, shop, depart.
4. A ranged champion one step out of range must not make an illegal same-turn basic attack;
   exercise an attack reset separately.
5. Reveal a sleeping dangerous camp: prefer a safer reward over waking a group you cannot survive.
6. A blocked passage or hidden occupant: movement fails safely and replanning makes progress.
7. Maps with three and five draft choices, both sides; full release games on large maps for fuel.
