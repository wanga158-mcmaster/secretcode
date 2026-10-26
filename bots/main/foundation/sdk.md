# SDK cookbook: observe, act, stay inside the fuel (CLI/SDK 3.0.4, engine 1.0.2)

How a new C++ bot reads the game and acts on it cheaply. The header (`sdk/cpp/cambc.h`) and the API
reference (`docs/reference/BotAPICpp.md`) list every call; this page says which calls to use, what they
cost, what throws, and the pitfalls the probes hit. Fuel numbers are release builds (`arena` and the
platform build release; `debug_block` is skipped there), measured as `fuel_remaining()` deltas minus a
15-fuel no-op baseline, medians of 5-31 samples, on a 30x30 lab map, a 100x100 lab map and the pool
maps 00packs (31x31), 00ambush and 01big_world (100x100); repeat runs agree within 5
(`data/mechanics/fuel-costs.csv`, `fuel-budgets.json`, probe `bots/contenders/_probe-fuel`). Mechanics
the calls expose are in [mechanics.md](mechanics.md) (M ids). Error codes are `cambc::error_code`
values (E97 = `api_not_in_vision`).

| Id | Claim | Status |
| --- | --- | --- |
| S1 | One loop: dispatch, catch everything at the turn boundary, always end the turn | verified (engine 1.0.2) |
| S2 | What throws and what returns false | verified (engine 1.0.2) for the codes listed; others per the header |
| S3 | A thrown exception costs 1.8-8K fuel; check first | verified (engine 1.0.2) |
| S4 | Draft: one pick per draft turn, enemy picks visible as they land, the whole map readable | verified (engine 1.0.2) for pick order, budgets and map access; the pick error codes and the random fallback are documented, unverified |
| S5 | Draft memory: write the plan in the draft, read `cambc::draft_memory` in play | verified (engine 1.0.2) |
| S6 | The cheapest known way to get each key piece of information | verified (engine 1.0.2) |
| S7 | Own state: hold `self()`, one `state()` read, held ability copies | verified (engine 1.0.2) |
| S8 | Allies: keep the handles; outside your vision an ally is opaque | verified (engine 1.0.2) |
| S9 | Visible enemies: held handles plus `is_visible()`, not `vision::champions()` | verified (engine 1.0.2) |
| S10 | Camps: build a static index once, then read held camp handles and the awake/asleep masks | verified (engine 1.0.2) |
| S11 | Terrain, spawns and symmetry are free cached masks | verified (engine 1.0.2) |
| S12 | Vision queries: held boards and point tests | verified (engine 1.0.2) |
| S13 | Paths: one distance field per destination, stepped with `get()`; avoid `move_towards` | verified (engine 1.0.2) |
| S14 | Bitboards: in-place operations are ten times cheaper than new boards | verified (engine 1.0.2) |
| S15 | Movement: one step per turn; ranged champions trade it against the attack | verified (engine 1.0.2) |
| S16 | Attacks: `attack(unit)` or `attack(altar_lock)` on a visible target in Chebyshev range | verified (engine 1.0.2) |
| S17 | Casting: one successful cast per turn; target form follows the payload kind | verified (engine 1.0.2) |
| S18 | Channels, shop, wards and priority | verified (engine 1.0.2) |
| S19 | Shared memory: 8 bytes, 25-29 fuel per call, visible to later actors at once | verified (engine 1.0.2) |
| S20 | Budgets: 100,000 a turn; 200,000 on a champion's first turn; 1,000,000 on the first draft turn | verified (engine 1.0.2) |
| S21 | Exhaustion suspends the bot mid-function; the next turn finishes the old one | verified (engine 1.0.2) |
| S22 | Logging and standard algorithms are expensive | verified (engine 1.0.2) (corrects earlier prose) |
| S23 | A full per-turn observation costs 2.3-5.7K the obvious way, 0.6-1.5K lean | verified (engine 1.0.2) |
| S24 | Fuel cost of each call (release, net of the 15-fuel baseline) | verified (engine 1.0.2) |
| S25 | Zero-initialisation is a byte loop; copied group handles cost ~600 each; static setup on a big map takes most of the first turn | measured (timetable's builder) |

## The turn boundary

### S1. One loop: dispatch, catch everything at the turn boundary, always end the turn
Claim: A bot is one `main()` that loops forever: `if (cambc::is_draft()) draft(); else play();` inside
`try { ... } catch (const std::exception& e) { log a line containing "error"; }` and then
`cambc::end_turn()` every iteration (the starter template does exactly this). Each champion and the draft
run in separate bot instances with their own globals; `cambc::self()` is the instance's champion
(throws `std::bad_optional_access` in the draft). Statics persist across turns of one instance, so
per-champion memory is plain globals. `debug_block(...)` is free in debug builds and skipped entirely in
release, so it may only hold logging.
Evidence: SDK header; template `sdk/templates/cpp/src/main.cpp`; every probe bot.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: SDK template changes.
Consequence: Never let an exception escape a turn: the champion would stop acting. Turn-start logic
must tolerate being skipped (S21). Structure: a bot is a resumable coroutine per body, not a
function per turn.

### S2. What throws and what returns false
Claim: Engine refusals throw `cambc::error` (`code()`, `message()`). Observed in the probes: E97 hidden
target, E98 nothing there, E42 bad attack target, E41 attack already used (a ranged attack after a move),
E36 movement already used (a ranged step after an attack), E27 a second cast in the turn, E26 cannot cast
(also "An available attack cannot be reset"), E30 bad cast target, E31 / E32 dash out of range or bad
path or landing, E126 / E127 an
excludes-walking cast after or before a move, E34 / E35 / E123 acting while recalling / tributing /
teleporting, E22 forbidden landing or known occupied tile, E37 crystal out of range, E119 lock out of
range, E64 / E65 / E117 / E62 ward refusals, E49 / E55 shopping off spawn, E52 / E59 not enough gold, E58
consumable carry limit, E113 second recall start in a turn, E115 not channelling, E18 draft memory
written in play, E21 a dead champion moving. The header lists the rest (E43 attack out of range, E28
on cooldown, E31 cast out of range, ...). SDK-side checks throw standard exceptions:
`std::invalid_argument` for actions on a handle other than self and for `cancel_recall()` /
`cancel_submit_gems()` when not channelling,
`std::out_of_range` for shared memory outside 8 bytes, a missing buff in `buffs().get<T>()`, or no camp
at `map::camp(p)`, `std::bad_variant_access` / `std::bad_optional_access` for a wrong `ability.get<T>()`.
Actions that can meet a hidden occupant (`move`, `move_absolute`, `move_towards`, `ability.use`,
`consumable.use`, `place_ward`) return false instead and consume nothing (M46); `move_towards` also
returns false when already there, unable to move or without a route. `true` from a crystal means the
channel started.
Evidence: probe logs (`data/mechanics/channels-events.txt`, `info-evidence.txt`, `econ-events.txt`);
SDK header.
Rests on: M46.
Status: verified (engine 1.0.2) for the codes listed; others per the header.
Recheck: SDK error enum.
Consequence: Treat false as "re-observe and choose again this turn"; treat a throw as a bug in the
bot's model of the state, log it, and keep the turn going if the remaining work is independent.

### S3. A thrown exception costs 1.8-8K fuel; check first
Claim: A caught SDK query error (hidden camp, hidden enemy, hidden group) costs 3.7-4.7K, a failed
action 7.5-8K, a plain `std::runtime_error` 1.85K; a `try` block that does not throw costs nothing. The
matching checks cost 33-57: `camp.is_visible()`, `enemy.is_visible()`, `vision::in_vision(p)`,
`vision::has_camp(p)`, `vision::has_champion(p)`.
Evidence: `fuel-costs.csv` (5 maps).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: SDK error class.
Consequence: Never probe visibility by catching; one catch at the turn boundary is the only one a hot
path should rely on.

## Draft

### S4. Draft: one pick per draft turn, enemy picks visible as they land, the whole map readable
Claim: The draft instance sees `is_draft()` true and gets 1,000,000 fuel on its first draft turn and
100,000 later (S20). `draft::team_size()` is the number of picks (3-5); `draft::current_draft(team)`
lists completed picks for either team (both teams pick simultaneously and see each other's earlier
picks); `draft::pick(c)` once per draft turn (per the header a second pick throws
`draft_already_picked` and an own duplicate `draft_duplicate_champion`; the opponent may pick the same
champion; Cassi & Issac is one pick that yields two bodies). Per the rules a draft turn ended without a
pick gets a random one. Map data is available
in the draft: terrain masks, `map::camp_groups()`, spawn regions, symmetry and `cambc::champion_designs`
(the 13 designs' base stats, growth and abilities). Draft memory: S5.
Evidence: SDK header and test bot; probe drafts (picks in draft order became champion ids 0..n-1 and
n..2n-1); fuel budgets (`fuel-budgets.json`).
Rests on: M51.
Status: verified (engine 1.0.2) for pick order, budgets and map access; the pick error codes and the
random fallback are documented, unverified.
Recheck: draft API.
Consequence: Do the expensive whole-map analysis in the first draft turn (10x fuel) and pass the result
in draft memory. Draft order becomes champion ids, which break monster-aggro and turn-order ties
(M2, M6).

### S5. Draft memory: write the plan in the draft, read `cambc::draft_memory` in play
Claim: In the draft, `cambc::draft_memory = bytes` (a span, initializer list or any contiguous range of
trivially copyable values) submits at offset 0 immediately, truncating to 1024 bytes; a shorter write
leaves the remaining bytes as they were. Champions read the final bytes through `cambc::draft_memory`
(captured at their startup; 5 fuel per byte from the snapshot); assigning in play throws E18.
Evidence: info-order (`info-evidence.txt`); `fuel-costs.csv`.
Rests on: M51.
Status: verified (engine 1.0.2).
Recheck: draft memory size.
Consequence: Encode one fixed-layout struct and write it whole on the last draft turn.

## Observing

### S6. The cheapest known way to get each key piece of information
| Need | Cheapest known call | Fuel |
| --- | --- | --- |
| own position, health, strength, focus, state | raw record `detail::sdk_champion(handle, &info)` (not public API), else `state()` + `position()` + `health()` | 34, else ~200 |
| own cooldowns; can cast slot i | held `abilities()` copy: `cooldown()`; ready = `cooldown() == 0` and `state()` has `can_cast` | 39 each |
| own gold, bag | `gold_count()`, `gem_count()` | 44, 45 |
| where visible enemies stand | `vision::champion_tiles_into(team::enemy, held_board)` | 28 (30x30) - 181 (100x100) |
| one enemy's health / position / bag / cooldown | held handle from `cambc::champions(team::enemy)`: `is_visible()` then the getter | 57 + 62 / 75 / 42 / 142 |
| whether a hidden enemy is alive | held handle `is_alive()` | 36 |
| which camps are awake / asleep | `vision::awake_camp_tiles_into` / `asleep_camp_tiles_into` | 27-180 each |
| a visible camp's health | held camp from the static `map::camp_groups()` index: `health()` | 34 |
| a group's state or respawn timer | held group `state()` while a member tile is visible; countdown predicts earliest eligibility only (M52) | 360-467 per read |
| what I see | `vision::visible_tiles_into(held_board)`; one tile `vision::in_vision(p)` | 27-180; 33 |
| terrain, spawns | `map::walls()` / `bushes()` / `holes()` / `altars()` / `spawn_region(team)` | free (cached) |
| steps to a destination | cached `distance_field`, `get(p)` per neighbour | 38 each |
| round, score | `round_number()`, `global_submitted_gems(team)` | 21, 24 |
| allies beyond my vision | shared memory, all 8 bytes in one `shared_memory.read(0, span)` | 29 |
Evidence: `data/mechanics/fuel-costs.csv` (5 maps); visibility rules from M44, M45.
Rests on: S7, S9, S10, S12, S19.
Status: verified (engine 1.0.2).
Recheck: any SDK header change.
Consequence: A full turn's observation fits in about 1-1.5K fuel (S23); handles, the camp
index and scratch boards are built once and kept.

### S7. Own state: hold `self()`, one `state()` read, held ability copies
Claim: `self()` is a cached reference (1 fuel). Getters cost: position 78, health / max_health /
strength 65, state 56, can_move 60, is_alive 39, gold_count 44, gem_count 45, restrictions 45,
ward_cooldown 44, priority 39, buffs 118, item_count 216, ward_positions 122. Read `state()` once and test
its flags (alive, recalling, submitting_gems, teleporting, can_move, can_attack, can_cast,
completed_turn). `abilities()` returns copies (101); on a held copy `cooldown()` is 39 against 146 on a
fresh copy, and `info()` (range, modifier, radius, duration, valid_targets, exclusive_with_movement) is 8.
`ability.is_ready()` costs 480 because it builds a champion: use `cooldown() == 0` plus
`state().contains(can_cast)` instead. The cheapest whole record is the raw import behind the getters
(`detail::sdk_champion`, 34 fuel for position, health, max health, strength, focus, range, team, state).
Evidence: `fuel-costs.csv`.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: SDK header changes (the raw import is not public API).
Consequence: Own state for a turn costs about 300 fuel the obvious way and under 100 lean.

### S8. Allies: keep the handles; outside your vision an ally is opaque
Claim: `cambc::champions(team::ally)` returns handles to every ally, visible or not (2.2K for three,
3.2-3.5K for five: each builds two ability views); keep them from the first turn. An ally outside your
own vision answers `is_alive()`, `champion_name()` and `team()`; position, health, cooldowns, gems
and items throw E97 (M44). Visible allies permit live combat reads, but self-only actions, gold and
ward resources remain self-only. Immutable design facts can be obtained from the champion type
without observing its current equipment or state.
Evidence: `fuel-costs.csv`; info-order (`info-evidence.txt`).
Rests on: M44.
Status: verified (engine 1.0.2).
Recheck: vision sharing in a patch.
Consequence: Allied positions and intentions travel only through the eight shared bytes (S19).

Current-source contract (CLI/SDK 3.0.5, engine 1.0.3 bundle):

| Data | Personally visible ally | Unseen ally |
| --- | --- | --- |
| Position, HP/maxHP, Strength, Focus, attack range, state flags | Fresh host query | Unknown; historical native E97 |
| Gems, item counts, cooldowns, buffs/restrictions, channel timer, current/next priority | Fresh host query; channel timer requires an active channel | Unknown; historical native E97 |
| Identity, team, alive | Available | Available |
| Base stats, passive tag, immutable ability design | Available by type | Available by type; not current gear or readiness |
| Public gold, ward positions/recharge, actions | Self only | Self only |

The current header routes live stats/state through `sdk_champion` (`cambc.h` 3501-3579, 3922),
items through `sdk_champion_items` (3723), cooldowns through their own live query (3269). Saved
`data/mechanics/info-evidence.txt` 94-96 shows visible allies reading these fields and a hidden ally
returning E97 for each. Those observations remain engine 1.0.2; this is current-source agreement,
not a new engine 1.0.3 hidden-query experiment. Identity explicitly works for hidden/dead champions
(3932). Other-dead-ally live-query permission was not established: the old dead-self record at
line 101 proves only self access. Reject dead new assignees without guessing their live stats.

Freshness matters separately from permission. `state()` is a fresh bitset copy: can_move,
can_attack, can_cast, completed_turn and channel flags describe that queried boundary. A completed
owner has no second turn this round even if unused allowance bits remain true. False allowance
alone does not diagnose stun; use fresh `restrictions()`/buff presence and channel flags. A
zero-timer stun can still be present until owner-start expiry. Predicting the next owner turn's
readiness is a model, not the currently observed permission. Buff collections are snapshots too;
call `buffs()` again after relevant actions.

Ability metadata has a different lifetime: `champion::abilities()` copies views cached when the
champion handle was constructed (3720). A hidden construction can cache E97 (3204-3212), and
`info()` rethrows it later (3338); `is(kind)==false` from an empty cache does not prove the kit lacks
that ability. Conversely, previously cached design data can remain readable while its owner is
hidden. Use `champion_designs[type]` for immutable kit metadata; read visible live cooldowns and
state separately. `is_ready()` also checks cached metadata, cooldown and can_cast, but does not
prove target, movement-exclusion or reset legality. The raw visible champion record includes
move_speed (`imports.h` 88); the public champion class has no corresponding getter, and its
movement helper uses design base_move_speed (5231).

Actual application: the audited main observation path constructs `ChampView` with zero-initialized
Strength/Focus/range, then omits those ally reads (`ctx.h` 27-42, `ctx.cpp` ally branch). Those zeros
are absent measurements, not exact zero-DPS allies. Timetable's capacity consumer must enrich
visible eligible bodies or carry explicit unknown/estimated validity; unseen claims may retain a
dated estimate, but fresh physical assembly still requires actual visibility, range and state.

### S9. Visible enemies: held handles plus `is_visible()`, not `vision::champions()`
Claim: Keep `cambc::champions(team::enemy)` handles (2.2-3.5K once). Per turn, `enemy.is_visible()` (57)
then health 62, position 75, state 53, gem_count 42, buffs 115, ability cooldown 142, item_count 213.
`vision::champions(enemy)` rebuilds views from the occupancy mask every call (about 300-440 plus 700 per
champion; 2.2-2.6K with two visible) and `vision::champion_at(p)` builds one view (448).
`vision::champion_tiles_into(team::enemy, held_board)` (28 on 30x30, 181 on 100x100) gives the occupied
tiles cheaply. Hidden enemies: `is_alive()` 36, `champion_name()` 31, `is_visible()` 41. Readable
fields: M45; gold never.
Evidence: `fuel-costs.csv`; info-observe.
Rests on: M45.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Enemy observation costs about 250 fuel per visible enemy done this way, against 1K or more
through the convenience lists.

Current-source response boundary: personally visible foes expose fresh state, cooldowns,
inventory, buffs and channel/order data, but neither gold/shop permission nor wards. Their
observed allowance bits describe now, not their next owner-start permissions. There is no
public legal-movement mask for another body; `attack_tiles()` and ability `valid_tiles()` are
geometric masks without full target/readiness/occupancy legality. Immutable kit metadata and
hidden ability-view caching follow S8. [C23](combat.md#c23-protection-through-our-next-input-must-cover-ordered-enemy-opportunities)
sets the conditional two-opportunity response obligation; these SDK facts alone prove no safe action.

### S10. Camps: build a static index once, then read held camp handles and the awake/asleep masks
Claim: `map::camp_groups()` (also in the draft) returns every group with its members' handles, types,
spawn tiles, gold and gem values and `max_respawn_timer()`; it costs about 930 per group plus 105 per
camp (7.3K on a lab map, 16.1K on 00ambush, 32.0K on 01big_world) and returns fresh copies every call, so
call it once and keep the result. Per turn: `vision::awake_camp_tiles_into(board)` and
`asleep_camp_tiles_into(board)` (27-180 each) for state, a held camp's `health()` (34), `state()` (32),
`is_visible()` (41). `map::camp(p)` by spawn tile is 121; `vision::camp_at(p)` 103 (current tile, visible
camps only). Avoid per turn: `vision::camps()` (1.7-2.7K), `vision::camp_groups()` (9.6-36K),
`camp.camp_group()` (0.8-1.3K, it reloads the group). `camp_group.state()` (360-467) needs a member spawn
tile in vision (M49); a remembered timer predicts earliest eligibility, not availability. Re-observe
when action depends on its actual state, and preserve fresh `respawning{0}` as unavailable (M52).
Monster range and strength
come from `camp.base_stats()` (150; cache per type).
Evidence: `fuel-costs.csv`; info-observe.
Rests on: M49.
Status: verified (engine 1.0.2).
Recheck: SDK camp API.
Consequence: Live camp knowledge per turn costs a few hundred fuel; everything not in vision must be
remembered by the bot.

### S11. Terrain, spawns and symmetry are free cached masks
Claim: `map::walls()`, `bushes()`, `holes()`, `altars()` and `map::spawn_region(team)` return references
to masks built before `main` (free per use); `map::width()`, `height()`, `symmetry()` and the
`reflect_*` helpers are cheap. Terrain never changes during a game.
Evidence: `fuel-costs.csv` (constants and terrain references read as free).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: map format changes.
Consequence: Build walkability masks once (walls | holes | altars | enemy spawn) and keep them.

### S12. Vision queries: held boards and point tests
Claim: `vision::visible_tiles_into(board)` 27 (30x30) to 180 (100x100) against 314-645 for a new board;
`vision::in_vision(p)` 33. Hypothetical vision: `self().visible_tiles_from(p)` 366-850 (excludes the
ward), `ward::visible_tiles_from(p)`, `bitboard::shadow_cast(p, r)` 553-1,155 with any opaque mask. The
same `_into` pattern applies to `champion_tiles_into`, `camp_tiles_into`, `ward_tiles_into`,
`teleport_tiles_into`, `altar_lock_tiles_into`. Vision is live: one's own move updates it within the
turn (M48).
Evidence: `fuel-costs.csv`; info lane test.
Rests on: M44, M48.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Allocate a few boards once and reuse them with the `_into` forms; a new board costs ten
times an in-place one.

### S13. Paths: one distance field per destination, stepped with `get()`; avoid `move_towards`
Claim: `distance_field::compute(walls, roots, visited, max)` costs about (16 + expansions) x
ceil(W/8) x ceil(H/8): 621 on an open 30x30 map, 12.0K on an open 100x100, 25.2K on 00ambush and 30.0K
on 01big_world for a whole-map field; bounded to 10 steps 461 / 4,439; `compute_to` a tile 6 away 402 /
3,768. `get(p)` is 38; `next_move_from(p)` 843 (reading the eight neighbours with `get` is cheaper);
`copy_from` 319-2,767. `champion::move_towards(target)` builds about eight boards and a fresh search every
call: 4.7-10.3K for a target 3 tiles away and 5.0-36.7K across a map; it treats the target as free and
avoids walls, holes, altars, the enemy spawn, visible champions and camps and teleport reservations.
`bitboard::reachable_tiles(p, 1)` (370-837) is the one-step movement mask.
Evidence: `fuel-costs.csv`; expansion counts in `fuel-budgets.json`.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: SDK pathing.
Consequence: Cache fields per destination (spawn, altars, targeted groups) and refresh them only when
blockers matter; on 100x100 maps a `move_towards` every turn alone takes 10-37% of the budget.

### S14. Bitboards: in-place operations are ten times cheaper than new boards
Claim: Constructing, copying or combining into a new board (`a | b`, `a & b`, `~a`, `shift_right`,
`reflect`) costs 272-305 on 30x30 and 437-775 on 100x100; in-place forms (`|=`, `&=`, `*_into`),
`count()`, `is_empty()` and `==` cost 27-54 and 180-207; `is_set` 38, `set` 14, `draw_chebyshev` /
`draw_circle` about 22. `positions()` costs about 9 per set bit (85K for a nearly full 100x100 board,
over a turn's budget) against about 1 per bit for `positions_into(span)` into a preallocated buffer.
Evidence: `fuel-costs.csv`.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Keep scratch boards alive; enumerate with `positions_into`; never call `positions()` on a
large mask.

Current ABI supplement: [the compact native witness](../../../results/contenders/mechanics/bitboard-import-current/compact/witness.json)
and its [qualified assessment](../../../results/contenders/mechanics/bitboard-import-current/assessment.json)
bind SDK 3.0.5/engine 1.0.3, source `02167957af3a`, BigWorld 100-by-100. For this board the
linear buffer is 1,250 bytes and matches flat row-major LSB indexing: `i=y*100+x`, byte `i>>3`,
bit `1<<(i&7)`, with no row padding. The exact selected flat indexes are
`0,1,7,8,99,100,101,5050,9900,9992,9998,9999`. All twelve queried memberships are true and
native count is twelve, excluding extra set bits in each tested board. Independently guest-built
bytes, SDK set/export and import/export/reimport buffers agree exactly in count, selected masks
and all 1,250 bytes. This is a finite ABI witness, not exhaustive proof for other board sizes,
every input buffer or a production observer's semantic completeness.

The compact four-round diagnostic records a round-3 ordinary fuel-budget hit; its printed fuel
argument was sampled before later formatting. The charged instruction is not isolated, so this
does not prove formatting caused suspension. Truncated zero fault/exhaustion fields are not
clean-health evidence. The preserved first two-round attempt never reached native count or
membership results and is inconclusive. ABI agreement does not qualify native fuel safety,
contender behavior, whole-game health or strength; S14's cost table retains engine 1.0.2 scope.

## Acting

### S15. Movement: one step per turn; ranged champions trade it against the attack
Claim: `move(delta)` / `move_absolute(p)` (155) step to one of the 8 neighbours; false when a hidden
occupant blocks (allowance kept, tile revealed for the round, M46); known-invalid destinations
throw E22. For ranged champions moving spends the attack allowance and attacking spends the move (E41
"Attack already used" after a move, E36 "Movement already used" after an attack); melee champions may do
both in either order. An
excludes-walking cast cannot follow a move (E126) and forbids a later move (E127). Dashes and crystals
are casts and consumables, not moves (M28, M21).
Evidence: probe logs (`channels-events.txt`, `econ-events.txt`); state flags after actions (st=AC after a
melee move, MC after a melee attack).
Rests on: M23.
Status: verified (engine 1.0.2).
Recheck: movement rules.
Consequence: Decide a ranged champion's move-or-strike before acting; re-read `state()` after each
action instead of assuming.

### S16. Attacks: `attack(unit)` or `attack(altar_lock)` on a visible target in Chebyshev range
Claim: `champion::attack(const unit&)` (camps and champions) and `attack(const altar_lock&)` cost 44;
the target must be visible and within Chebyshev `attack_range()` (melee 1, ranged 3). Own locks cannot
be attacked (E42). With both teams' locks on an altar `vision::altar_lock_at(p)` returns the own lock;
take the enemy lock from `vision::altar_locks(team::enemy)`. Attack resets: M23.
Evidence: probe logs; `fuel-costs.csv`; ch-extra / ch-lock2 (`channels-events.txt`).
Rests on: M20.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Choose targets from held handles and check `is_visible()` and distance before attacking;
a failed attack costs about 8K fuel (S3).

### S17. Casting: one successful cast per turn; target form follows the payload kind
Claim: `ability.use()` (self: attack reset, shield), `use(champion)` and `use(camp)` (unit spells),
`use(tile)` (ground: area spells, dashes) return true on success (98 fuel for a reset, 112 for a one-step
dash). `info()` gives the payload (`ability_damage`, `ability_aoe_damage`, `ability_cc`, `ability_heal`,
`ability_dash`, `ability_shield`, `ability_attack_reset`) with range, radius, modifier, duration,
`valid_targets` (bit flags camps 1, enemies 2, allies 4, self 8, ground 16) and
`exclusive_with_movement`; `damage_amount()` is the Focus-scaled damage (verified equal to modifier x
Focus), `damage_amount(target)` per the header the predicted health loss after the target's mitigation
and shields,
`heal_amount(target)` (capped at missing health) and `shield_amount()` likewise. Pitfall: for Heart of
Ruin both `damage_amount` forms return the unscaled value; apply the missing-health passive yourself
(M29). `valid_tiles()` is the Chebyshev range square, not a target filter. Per-kind semantics:
M25 and the entries after it.
Evidence: SDK header; `fuel-costs.csv`.
Rests on: M25.
Status: verified (engine 1.0.2).
Recheck: catalog ability payloads.
Consequence: Read every ability number from `info()` and the `damage_amount` family at runtime; never
hard-code a champion's kit.

### S18. Channels, shop, wards and priority
Claim: `recall()` / `cancel_recall()`; `submit_gems(altar_tile)` / `cancel_submit_gems()` (both cancels
throw `std::invalid_argument` when not channelling); `consumables()[consumable_type::tp_crystal]
.use(dest)` and `[altar_lock].use(altar)`; `buy(item_type, n)` on spawn or dead (Tavi anywhere for
items; `buy(shard, 5)` in one call yields the emblem directly), `buy(consumable_type)` on spawn or dead,
`sell(item)` Tavi on spawn; `can_buy(item)` 135;
`place_ward(p)` returns bool; `set_priority(n)` takes effect next round. While channelling, only
`set_priority` and shared memory work (M17, M19, M21); dead champions can shop
(M4).
Evidence: probe logs (`channels-events.txt`, `econ-events.txt`, `info-evidence.txt`).
Rests on: M17, M19, M20, M21, M47, M39.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Shop and set priority before starting a channel in the same turn; a channel ends every
other action until it completes or is cancelled.

### S19. Shared memory: 8 bytes, 25-29 fuel per call, visible to later actors at once
Claim: `cambc::shared_memory[i]` reads or writes one byte through the host (27 / 29);
`shared_memory.read(0, span)` and `.write(0, span)` move up to all 8 bytes in one call (29 / 25). Semantics:
M50.
Evidence: `fuel-costs.csv`; info-order.
Rests on: M50.
Status: verified (engine 1.0.2).
Recheck: shared memory size.
Consequence: Read all 8 bytes once per turn into a local struct and write back the own slot once.

## Fuel

### S20. Budgets: 100,000 a turn; 200,000 on a champion's first turn; 1,000,000 on the first draft turn
Claim: The replay's fuel stream header is [1,000,000, 100,000, 200,000, 100,000] (draft first, draft
later, play first, play later). Later turns start at 99,999; unused fuel never carries over. SDK start-up
before `main` uses 9.5K (draft, 30x30) to 11.4K (play, 100x100, five champions), about 7.2K of it
building the 13 `champion_designs`. `fuel_remaining()` costs 4.
Evidence: `fuel-budgets.json` (fuel streams on 5 maps, both teams).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: CLI/SDK upgrade; the fuel stream header.
Consequence: One-off precomputation belongs in the draft (10x) or the first play turn (2x); read the
budget from `fuel_remaining()`, never from constants; expensive analysis must be spread over turns.

### S21. Exhaustion suspends the bot mid-function; the next turn finishes the old one
Claim: Running out of fuel stops the bot at once: actions already taken that turn stand and the turn
ends. Next turn the bot resumes at the suspension point with a fresh budget and the old turn's locals
(its round variable said 67 while `round_number()` said 68); its actions apply to the new round and that
round's turn-start code never runs. Signs: one `fuelExhausted` statistics event per exhausted turn
(phase "champions", the champion as owner; `botFaults` stays 0), a TurnRollover step in the replay, and
a fuel stream entry equal to the budget. Team stdout lines from an exhausted round come out of order.
Evidence: fuel-lab2 exhaustion test (`fuel-budgets.json`); mon-target r45 stale mask (M48).
Rests on: S20.
Status: verified (engine 1.0.2).
Recheck: engine scheduling.
Consequence: Gate every optional phase by `fuel_remaining()` with a margin; re-read the round number
and observations after any heavy step; count `fuelExhausted` as a health failure.

### S22. Logging and standard algorithms are expensive
Claim: `printf` costs about 650 plus about 625 per integer field and about 1,900 per float field (a
typical line with floats 5.4-7.8K; integers only about 3.1K); `fputs` of a literal 115; `fflush` 95-430;
`std::to_string` + concatenation 561; 100 `push_back` 8.7K; `std::sort` of 100 ints 27.5K (about 275 per
element); 100 int64 Chebyshev distances 1.7K.
Evidence: `fuel-costs.csv`.
Rests on: none.
Status: verified (engine 1.0.2). The rules-notes figure of about 1K per formatted line is low.
Recheck: toolchain or libc changes.
Consequence: Gate diagnostics by spare fuel and print integers; use fixed arrays, partial selection and
small hand-written sorts in hot paths.

### S23. A full per-turn observation costs 2.3-5.7K the obvious way, 0.6-1.5K lean
Claim: Own position, health and state, both cooldowns, `vision::champions(enemy)` with health,
`vision::camps()` with state and health, and an 8-byte shared memory read cost 2.3K with nothing in view
and 4.8-5.7K with two enemies and seven camps in view. The same information through one raw own record,
held ability copies, held boards with `_into`, `positions_into` and held handles costs 0.57-0.88K and
0.93-1.54K.
Evidence: `observe_typical` and `observe_lean` rows of `fuel-costs.csv` (5 maps).
Rests on: S7, S9, S10, S12.
Status: verified (engine 1.0.2).
Recheck: SDK header.
Consequence: Observation is cheap either way next to pathing (S13) and logging (S22); the
budget goes to search and simulation, so those are where design effort pays.

### S24. Fuel cost of each call (release, net of the 15-fuel baseline)
| Operation | 30x30 | 100x100 | Notes |
| --- | --- | --- | --- |
| `fuel_remaining()` / `round_number()` / `global_submitted_gems` | 4 / 21 / 24 | same | |
| own position / health / state / is_alive | 78 / 65 / 56 / 39 | same | raw record 34 |
| `abilities()` copy / held `cooldown()` / fresh `cooldown()` / `is_ready()` | 101 / 39 / 146 / 480 | same | |
| gold / gems / restrictions / ward_cooldown / buffs / item_count | 44 / 45 / 45 / 44 / 118 / 216 | same | |
| `vision::visible_tiles_into` / new board | 27 / 314 | 180 / 574-645 | |
| occupancy masks `_into` / new board | 27-28 / 272 | 180-181 / 379-450 | champions, camps, awake, asleep, wards, teleports, locks |
| `in_vision` / `has_camp` / `has_champion` | 33 / 42 / 42 | same | |
| `vision::champion_at` / `vision::champions(enemy)` | 448 / 2,177 | 448 / 2,590 | 2 visible |
| held enemy health / position / state / is_visible / cooldown / items | 62 / 75 / 53 / 57 / 142 / 213 | same | |
| `vision::camp_at` / held camp health / state / is_visible | 103 / 34 / 32 / 41 | same | |
| `vision::camps()` (7 visible) | 2,330 | 2,743 | |
| `camp.camp_group()` / `camp_group.state()` | 824 / 360 | 1,294 / 467 | |
| `map::camp_groups()` | 7.3K (7 groups) | 16.1K (00ambush) / 32.0K (01big_world) | ~930/group + 105/camp |
| `vision::camp_groups()` | 9.6K | 18.4K / 36.0K | |
| `cambc::champions(team)` | 2.2K (3) | 3.2-3.5K (5) | |
| shared memory 1 byte / 8 bytes | 27-29 / 25-29 | same | |
| new bitboard / in-place op | 272-305 / 27-54 | 437-775 / 180-207 | |
| `positions()` / `positions_into` | ~9 / ~1 per bit | same | |
| `reachable_tiles(p,1)` / `shadow_cast(p,8)` | 370 / 553 | 474-837 / 786-1,155 | |
| distance field whole map / bounded 10 / `compute_to` 6 | 621 / 461 / 402 | 12.0K-30.0K / 4,439 / 3,768 | (16+E) x chunks |
| field `get` / `next_move_from` / `copy_from` | 38 / 843 / 319 | 38 / 843 / 2,767 | |
| `move(delta)` / `attack` / reset cast / dash cast | 155 / 44 / 98 / 112 | same | |
| `move_towards` 3 tiles / across the map | 4.7K / 5.0K | 10.2K / 18.9-36.7K | |
| caught query error / failed action / `std::runtime_error` | 3.7-4.7K / 7.5-8K / 1.85K | same | |
| `printf` (no args / int / float / string field) | 332 / +625 / +1,900 / +1,050 | same | |
| 100 `push_back` / `std::sort` 100 ints | 8.7K / 27.5K | same | |
| typical / lean observation set | 2.3-4.8K / 0.57-0.93K | 2.3-5.7K / 0.88-1.54K | S23 |
Evidence: `data/mechanics/fuel-costs.csv` (all rows, five maps), probe `bots/contenders/_probe-fuel`.
Rests on: S20.
Status: verified (engine 1.0.2).
Recheck: any CLI/SDK or toolchain upgrade (rerun the probe on the same maps).
Consequence: Supersedes the shorter table in `docs/rules-notes.md` (its 1K per log line and 2.5 per tile
for fields are low on large walled maps).

### S25. Zero-initialisation is a byte loop; copied group handles cost ~600 each; static setup on a big map takes most of the first turn
Claim: In this toolchain `memset` and zero-initialisation compile to a byte loop of about 3 fuel per
byte, and `std::vector::resize` value-initialises, so a 10 KB table costs about 30K fuel to clear; a
user-provided empty constructor avoids the zeroing. Copying a `camp_group` handle costs about 600 fuel
per group: keep the vector returned by `map::camp_groups()` alive and point into it. A champion's first
turn on a large map spends about 100-120K of its 200K budget (S20) on static setup alone.
Evidence: measured by the historical timetable line, now retired in
  [timetable-residual-service](../../miscellaneous/timetable-residual-service/NOTES.md)
  ("foundation-worthy findings", and `tools/fuel.py`); not yet reproduced by a dedicated probe.
Rests on: S20, S24.
Status: measured (one historical, non-independent bot, release builds, engine 1.0.2); no
  qualification of the fresh Timetable starter or current toolchain.
Recheck: any CLI/SDK or toolchain upgrade.
Consequence: allocate static tables once and without zeroing in hot paths; never copy group handles per
turn; budget the first turn for setup and stage the rest over later turns.
