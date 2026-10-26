# SDK cookbook: observe, act, stay inside the fuel (CLI/SDK 3.0.4, engine 1.0.2)

S1-S25 preserve the original measurement bundle and its limits. They are measurements, not atomic
cost bounds for every caller. The 24 October independent renewal adds the current-native continuation
obligation S26; it does not transfer the old numerical costs to CLI/SDK 3.0.5/engine 1.0.3.

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
| S26 | A bounded query loop still needs an executable action tail | observed failure (engine 1.0.3); derived dispatch obligation |

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
pick randomizes the remaining selections. Map data is available
in the draft: terrain masks, `map::camp_groups()`, spawn regions, symmetry and `cambc::champion_designs`
(the 13 designs' base stats, growth and abilities). Draft memory: S5.
Evidence: SDK header and test bot; probe drafts (picks in draft order became champion ids 0..n-1 and
n..2n-1); fuel budgets (`fuel-budgets.json`).
Rests on: M51.
Status: verified (engine 1.0.2) for pick order, budgets and map access; the pick error codes and the
random fallback are documented, unverified.
Recheck: draft API.
Current pinned rules/SDK 3.0.5 clarification (`GameRules.md:53-86`, SDK `draft` namespace): uniqueness
is within each team, not across the two teams. Opposing picks remain legal. `team_size()` queries
draft rounds, not physical play bodies; query the completed play roster for actual actor counts.
`current_draft()` skips unavailable slots and supplies completed picks, not a legal-choice list or
a guarantee of a same-round opponent commit peek. Rules specify simultaneous picks revealed as
drafting proceeds; exact same-round visibility/commit timing is not newly native-tested here.
Picking belongs to draft; an authenticated live `self()` and its actions belong to play. Static
design/map data remain available for bounded draft preparation; memory semantics are in S5/M51.
The current catalog is editor-engine 1.0.3 metadata, not new native draft validation.
Consequence: Budget static setup in the available draft fuel and pass dated plan data through draft
memory. Missing opponent-pick information is not a forbidden choice or proof of a missing enemy.
Rebind physical identities and scheduling from the actual roster/current priority before relying on
actor counts, control capacity or aggro order (M2, M6); no roster preference follows from legality.

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
| a group's state or respawn timer | held group `state()` once while a member tile is visible, then count down | 360-467 once |
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
Current SDK 3.0.5 identity contract: `handle_champion_t`, `handle_camp_t` and
`handle_camp_group_t` are strong `uint32_t` enums with no zero-invalid sentinel declared there
(`cambc.h:183-196`). The champion constructor forwards the identity unchanged (`3471-3473`);
champion 0 can be valid. Use explicit bound/presence state or the particular API's declared invalid
sentinel, never integer truthiness/nonzero as proof that an actor/group/member reference exists.
Do not assume different handle types or member/ordinal APIs share an invalid sentinel, or that every
integer is valid. Identity also does not establish life, visibility or fresh action authority.
This is a current source contract, not a new native test or attribution of earlier failures.
Consequence: Own state for a turn costs about 300 fuel the obvious way and under 100 lean.

### S8. Allies: keep the handles; outside your vision an ally is opaque
Claim: `cambc::champions(team::ally)` returns handles to every ally, visible or not (2.2K for three,
3.2-3.5K for five: each builds two ability views); keep them from the first turn. An ally outside your
own vision answers only `is_alive()`, `champion_name()` and `team()`; position, health, cooldowns, gems
and items throw E97 (M44). Inside vision every field reads as for self except gold (self-only).
Evidence: `fuel-costs.csv`; info-order (`info-evidence.txt`).
Rests on: M44.
Status: verified (engine 1.0.2).
Recheck: vision sharing in a patch.
Current SDK 3.0.5 preserves field-level privacy: `cambc.h:3732` queries a runtime champion's carried
gems through the host, while `gold_count()` at `3741` requires self. Retain an actually observed
personally visible peer bag (ally or enemy) even when its gold is private; a coarse `private_economy`
flag must not erase the bag or turn unknown gold into zero. Hidden/failed reads remain unknown,
and runtime visibility/engine permission checks still apply. A simulated actor's guaranteed earned-
reward lower bound can support later purchase affordability only under explicit reward attribution,
no-intervening-spend and controlled-policy premises, with all other purchase guards retained. It
does not reveal actual starting gold or establish an actual future wallet. This is a current source
contract, not a new native test.
Consequence: Allied positions and intentions travel only through the eight shared bytes (S19).

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

### S10. Camps: build a static index once, then read held camp handles and the awake/asleep masks
Claim: `map::camp_groups()` (also in the draft) returns every group with its members' handles, types,
spawn tiles, gold and gem values and `max_respawn_timer()`; it costs about 930 per group plus 105 per
camp (7.3K on a lab map, 16.1K on 00ambush, 32.0K on 01big_world) and returns fresh copies every call, so
call it once and keep the result. Per turn: `vision::awake_camp_tiles_into(board)` and
`asleep_camp_tiles_into(board)` (27-180 each) for state, a held camp's `health()` (34), `state()` (32),
`is_visible()` (41). `map::camp(p)` by spawn tile is 121; `vision::camp_at(p)` 103 (current tile, visible
camps only). Avoid per turn: `vision::camps()` (1.7-2.7K), `vision::camp_groups()` (9.6-36K),
`camp.camp_group()` (0.8-1.3K, it reloads the group). `camp_group.state()` (360-467) needs a member spawn
tile in vision (M49); read a respawn timer once and count it down. Monster range and strength
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
Current SDK 3.0.5 buffer contract is separate from those historical costs. A bitboard's raw buffer
uses engine chunks (`cambc.h:782-784`); its linear buffer is packed row-major bytes with size
`ceil(width*height/8)` (`784-790`). Import/export headings and the allocating export (`892-911`)
agree on packed storage. The parameter comments at 893/900 misleadingly say one byte per tile;
they must not turn an already packed export into a cell-byte array. Do not pack that export again.
A consumer whose own contract requires one byte per cell needs an explicit unpack/population step
under separately verified ordering. The size function alone proves neither bit order nor native
import/export parity. Keep engine chunks, packed linear bytes and explicit cell-byte arrays typed
separately. `distance_field` has a different ABI: linear size is `width*height*sizeof(uint16_t)`
and import/export spans contain one `uint16_t` distance per tile (`1224-1227,1287-1306`), not packed
bitboard bytes. This is a current source-contract correction, not a new native or fuel test.
Consequence: Keep scratch boards alive; enumerate with `positions_into`; never call `positions()` on a
large mask.

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
Evidence: measured by the timetable line while building `contenders/timetable` (its `NOTES.md`,
  "foundation-worthy findings", and `tools/fuel.py`); not yet reproduced by a dedicated probe.
Rests on: S20, S24.
Status: measured (one bot, release builds, engine 1.0.2).
Recheck: any CLI/SDK or toolchain upgrade.
Consequence: allocate static tables once and without zeroing in hot paths; never copy group handles per
turn; budget the first turn for setup and stage the rest over later turns.

### S26. A bounded query loop still needs an executable action tail
Claim: Finite query/search work and unused game allowances do not establish that an admitted action
or escape will execute in the current input. A deferred input is a physical wait through subsequent
actor and camp phases. Both the waiting continuation and the remaining computation matter.

Evidence: saved native history
`results/contenders/mosaic/lean-r5/ordinary-observer/ryth-1519-1531-native.json`, engine 1.0.3,
Yellow Brick Road A/frozen passive, exact replay SHA256
`f4b6dcb9e834e2b8d168de9baee79221feefaa4ec35267ac8a2f3e55d17fb5c8`.
Independent reduction of its complete bounded window finds twelve live Ryth owner inputs R1519-1530
with only end-turn, M/A/C=`111`, both cooldowns0 and restrictions0 at those end-turn boundaries.
The actor stays at (11,11); recorded camp attacks reduce HP660 before the first attack to0 at R1531.
The thirteenth end-turn is dead. The native trace does not expose the private reason for yielding.
Mosaic's source/fuel audit makes consumed movement-tail reserve a candidate explanation; it is not
established by ready flags or this public trace alone. Full-game health and other actors are outside
this bounded finding.

A second retained witness separates that computation obligation from the actual availability of a
return endpoint: `results/contenders/mosaic/melee-exit-r8/death-witness.json` (SHA256
`ff7e249582ecd21283d08505985e26baf7ddece34aff35be4a5b013cfa5c0286`), exact Mosaic
`3a4b8b68fd16`, Yellow Brick Road A/frozen passive. Varg ends R796/R797 at (9,13) with HP144/60,
shield0, M/A/C=`111`, cooldowns0 and no stun/channel/restrictions. Whole-owner-turn remaining fuel is
54,902/55,167 under the witness's explicit draft-slot-to-actor mapping; the fuel stream itself has
no timestamps. Personally visible ally3 occupies (8,13), the quiet tile Varg last reached at R786,
and is recalling there at R797. Two elk deal42 each per subsequent camp phase and kill Varg at R798.
No Basic-to-immediate-egress word or refused move occurs in R770-798. The prior moves then basics
at R778/779 had already spent M. Private anchor, admission and branch state were not logged:
an occupied singleton return goal is a source-consistent possibility, not an established cause.
This witness neither demonstrates failure of the new word nor proves another legal safe action.

A separate Timetable diagnostic exposes an unreachable resource gate without fuel exhaustion. Exact
traced `contenders/timetable@2c2a9747f455`, RAW
`7551208716b955f96cf4c33d12a18cffdea64662d460e7a3dece8c3e79b84f10`, Yellow Brick Road A/frozen
passive, records Marcella's `plan-head` status1 (`NeedCompute`), group19, with exactly72,077 sampled
fuel remaining in all70 inputs R131-200. The diagnostic's `actor7` is champion-type Marcella, not a
native body id. Frozen `independent_navigation.cpp` SHA256
`98f10c733c5005d597cfdf118bfaeea4b4269975b40dd664bca1eb8e5cf4d0ff` requires
`remaining > 14000 + 64000` before route work. The repeated path reaches that gate below78,000 and
cannot start it; bounded loops and a fresh next-turn budget do not make this dispatch productive.
The closed experiment's durable semantic record is
`results/contenders/timetable/independent/6eedf9797c63/acquisition-stage-audit.json`, SHA256
`b2597e87f68575a5dfc87c54d2eda982bb3da2c57e9aadcb321fc55b71a3b450`, with frozen source and native
archive/statistics identities. The independently checked team-a stdout SHA256 was
`b28e525f6dcc23fa291948a957e36172f9bfb42898552952fb2de04a40fdf221`.
That traced full game banks198 versus the original EAGER's253, so its repeated computation stall is
an actual traced-program finding, not the untraced controller's observed private cause or a new
strength sample. This window is pre-conversion: trace printing stops after R200, and Marcella later
banks at221 and shops at231. These70 bounded facts do not prove indefinite unchanged behavior or
the untraced post-bank cause.

R12 adds a distinct generated-work finding. Exact Mosaic `7fe496c43e9d`, RAW
`b9c87e16b903aced798153c4bb03110281f2ec3519db9487ffa1f29ea8e86feb`, emits the
`ModelReturnState` constructor at WASM 0x18957 before the inner 62K fuel guard. Its `memory.fill`
operations initialize 191,717 bytes, including eight candidate pairs of 2504+20990 bytes (187,952
candidate bytes) and Frame 2048. The durable proof is
`results/contenders/mosaic/return-policy-r12/generated-cold-init.json` (SHA256
`b5680bfcc33451ae450ecb75f1a9d99c5ce4ead83be89e4bcffff9de8c6b679d`). The matched ordinary
YBR/passive panel in that directory's `assessment.json` fails health with 10 exhaustions per side,
banking 189/30 versus 218/30 controls. `cold-fuel-witness.json` separately establishes two consecutive
exhaustions per actor and no later recurrence through R2000. Neither timing nor generated byte count
joins a native exhaustion to this constructor; fuel per bulk-fill byte is unmeasured.

The controlled repair in `return-policy-r12/cold-source-delta.json` changes only the accessor to
`static constinit ModelReturnState value{};`, accepted by the actual toolchain; the pure core is
unchanged. The selected repaired module emits no `ModelReturnState` constructor or pre-guard
`memory.fill`; the payload is represented in active DATA, with ordinary body/buff queries still
before the guard. Exact ON `ed5220a7cd22`, RAW
`bc37f80cdfef6a67560407a117696db9e39ca6fbff6ee12d54086ff1e9f639f5` (3,573,302 bytes), has no
exhaustions on the fresh matched ordinary A/B keys, removing all 20 observed in the original panel.
Its banks 295/83 versus R10's 218/30 and deaths 8/2 versus 1/2 are scoped application outcomes, not a
competitive result. All 12 rows across `cold-{ordinary,pressure,diagnostic}-assessment.json` complete
with zero own technical counters; passive-opponent stalls prevent overall ordinary/diagnostic
health qualification. The distinct diagnostic banks 182/30 and fails public replay equality on both
sides, so its private fields, including a funding-overrun report, do not establish the ordinary
program's private cause. This intervention does not measure per-byte cost or prove a universal
first-use/callsite/fuel bound; the current-main pressure losses are recorded separately in G12.

R13's distinct ordinary subject `e074e74b8950`, RAW
`73650366e4e111ee814896dc0a50675161e454eff36a07b2f8b83171e9fb2b48`, banks 218/15 against R12's
295/83 on the matched passive A/B keys, with zero own technical counters but no B productive work
in the last two quarters. Its ordinary repeat joins public replay. Separate trace4 `9985d9b64b11`
has 29 retained JPR1 samples with quote/selected/physical counts all zero. Instrumented preparation
high-water costs include bodies 12.0-13.4K, details 6.5-7.1K, camps 7.5-10.6K and cold roster 5.6K,
before the declared 73K first-quote tail; maxima need not co-occur. Both complete trace replays differ
from ordinary. These are sampled traced-program costs and a compositional/source counterexample,
not an ordinary private-cause join or a universal no-action claim. Exact profiles, scope and diagnostic
appendix are retained at `results/contenders/mosaic/joint-r13/assessment.json`, SHA256
`554fd7ee25b41b03f99b341aceafe255f91247bbdb6eabf0f95796cccca0e428`.

Representation refinement is not programme uptake. Timetable's compact-calendar source
`ec0120c31bd4`/snapshot `0f63daaa1970` retains the prior Capture public trajectories exactly on
both ordinary YBR/passive sides (242/154 bank), with zero valid guest publication rows. The actual
old clear/bank recurrence survives, but no new through-horizon programme execution or private gate
cause is established. See `results/contenders/timetable/independent/ec0120c31bd4/calendar-assessment-01.json`.

Mosaic R17 ordinary snapshot `637e8042513f`/RAW `d908922b...` separately witnesses a same-input Basic->Move
and a late B clear R1956 -> 15-gem capture R1988 -> bank R1994, while A has two exhaustions at R400/402
with full-M/A/C owner-start boundaries and no actual work/move before rollover. Its
`results/contenders/mosaic/executable-r17/word-witness.json` attributes neither public word to the
new executor. Separate diagnostic samples retain zero attempts/words and both complete joins fail;
their admission-overrun fields cannot explain the ordinary exhaustions or private fallthrough.
Actual late service does not waive A's health failure or establish whole-input fuel closure.

Rests on: M1-M3, M23, S20-S21; actual owner-input and damage boundaries in the retained observer.
Status: scoped native observations (engine 1.0.3), separately identified diagnostic findings and
exact-module generated work; the following contracts are not universal bounds.
Recheck: SDK/toolchain costs, query shape, dispatch stages or native action phase order change.
Consequence: Before optional observation/search, preserve a budget for the actual action suffix and
its legal fallback, including exception/refresh/end-turn costs. Check the current epoch before acting
on any retained frame. If planning defers, choose and price an explicit waiting or cheap egress
continuation under the current external-damage schedule. A loop cap or an entry fuel check alone does
not establish the suffix's cost. Qualify the exact ordinary executable with full-game fuel/error/fault
counters and retained proof of renewed useful work; diagnostic builds and pure fixtures have their
own identities and cannot substitute for that measurement.

Resource-gate liveness needs a separate reachability check. If each visit starts with budget B,
repeats a prefix costing at least P, then demands `remaining > G` for its only progress step, the
condition `B-P <= G` makes that step unreachable under those unchanged dispatch premises. Returning
`NeedCompute` to the same state and repeating the same prefix cannot advance it. This is a structural
conditional deduction; the measured72,077 signature establishes only its stated diagnostic window.
Every retained compute state therefore needs an eventually reachable admission point or a different
explicit continuation. Revalidate cheap physical/epoch guards, schedule retained work before optional
service when its waiting premise permits, or split the expensive stage into progress whose actual
entry budget can reach it. Merely lowering a safety reserve does not establish fuel safety. Confirm
the new ordinary executable both reaches/advances that stage and resumes actual useful service.
This is distinct from cross-generation selection churn: finite monotone Price cursors can finish
repeatedly while publication forgets a rejected winner and every viable alternative (G13). Observe
the actual cursor/publication lifecycle before calling a single `NeedCompute` record a stall.

Funding is dated to an execution phase as well as an owner input. Before nomination, reserve the
complete validation, quote and command/failure tail. After those stages have actually completed,
arbitration must charge the work still outstanding from the current fuel, rather than charge the
same preparation twice. A retained token must identify the completed stages and their observation,
effect and hazard dependencies; an intervening query can change knowledge even when no other actor
has moved. Reuse only the stages whose premises still hold. A smaller residual command bound needs
its own measured closure cost; subtracting a nominal preparation estimate or lowering the original
entry gate does not establish that bound.

Cold/default/static-capacity initialization belongs to the actual input prefix even when later
offers reset only sparse headers. A guard after first-use construction cannot fund that earlier
work. Initialize bounded used regions with valid object lifetimes, and use poisoned/reused-workspace
differential fixtures to expose stale or uninitialized reads. Moving the same cost into draft, or
using raw storage without a valid lifetime/initialization argument, supplies no resource proof.
Qualify cold first use separately from warmed calls; S25's historical byte-loop rate does not measure
the current bulk-fill cost. The rejected original R12 cannot qualify a different workspace repair;
the controlled constant-initialization result above applies only to its exact module and game keys.

A retained winner also retains its outstanding mandatory cost while later offers are searched.
If work admission requires only `fuel > A` but, after accepted work and refresh, its mandatory move
requires `fuel > B` with B>A, an entry in (A,B] cannot execute the quoted word even if refresh were
free. Fund work, actual-effect refresh, repricing, movement and refusal closure before the first
primitive, or admit a complete post-work alternative. Recheck that bound before dispatch after any
optional menu; keeping the old winner while spending its continuation tail does not preserve an
executable incumbent. This is a source/model counterexample, not a measured native incidence.

Observation itself needs dependency-directed funding. Acquire the actual enabling facts needed for
one first complete playable policy, including its guarded continuation and failure/refresh closure,
and publish that policy before optional whole-world detail, hazard discounts, gear or sibling search.
Keep every unqueried identity and potential hazard as unknown or conservatively covered; omission is
not evidence of absence. The first policy may have an explicit limited model/risk scope, but its
premises, remaining queries and dispatch cost must actually fit. A rich world snapshot with no first
policy is not progress toward that dispatch, and a cheap isolated primitive is not a complete policy.
Revalidate the funded incumbent's dependencies when enrichment changes knowledge; do not spend its
execution tail to obtain an optional more precise forecast.

Fresh observation also needs a reachable publication boundary. A source-only counterexample is
Timetable `49100d7fb43e`: `WorldCache.observe` clears its fresh frame/provenance on each call,
retains no broad collection cursor, and returns Ready only after the whole world/roster transaction.
If that finite transaction crosses an ordinary 100K input slice, stale continuation can be discarded
and collection restarted before any physical command. Mandatory fresh self/selected-target/contact/channel facts and
their feedback tail must form an executable local transaction; unrelated dated observations may
nominate without being retagged as fresh action authority. Finite inner loops alone do not establish
that boundary. This source liveness counterexample is separate from the following native outcome.
Build `40200196669e` physically ran full YBR A/passive through R2000: raw records count 4,980 native
exhaustions (996 per body, first all five at R10, then every two rounds), with zero earned resources
or camp damage for all five. Complete measurement was rejected: 32 contexts were retained and
4,948 omitted. This is neither a no-start result nor valid complete health/strength qualification,
and it does not identify the exhausted guest callsite. Closed compact evidence:
`results/contenders/timetable/independent/49100d7fb43e/residual-native-A-01.json`, SHA256
`5710d0b9ebec92afcc03545fe3c1a35b287343d869b28eab978bd92dbb640a41`; source scope is recorded in
`bots/contenders/timetable/NOTES.md` with the retained 49100 source manifest.

The remaining input also needs closure when preparation declines and a legacy fallback acts.
Charge observation, book/word compilation, validation, actual dispatch and mandatory feedback as one
composed input. A funded observer that attempts no command does not thereby fund later legacy work;
each handoff needs a fresh remaining-cost check and a complete waiting/failure alternative. Separate
component guards and unpriced fallthrough cannot establish that whole-input contract.

A component's `false`/overrun result does not erase an already observed accepted/refused command
result or causal commit acknowledgment. Close the prior attempted prefix exactly once before any
new or legacy owner commands: acquire fresh actual authority, retain the known outcome/receipt
identity, and reconcile only the missing feedback/hooks. A later pose or HP reading cannot be
retroactively attributed to the old attempt; leave that attribution Unknown while preserving the
outstanding bank/service obligation. An acknowledged hook must not run twice merely because its
outer resource check failed. Mosaic R18 makes this conditional source refinement in
`src/executable_controller.h` (SHA256 `84bcb70430edf6e59ef4ba566cfc332041b736b0d16dbf79d93f2c66fb4b7302`)
and `src/executable_execution.h` (`edfa3fa40c92a8102e6c4615d2e4b398b1bbf90587cbe70ee509637c186be457`),
under `bots/contenders/mosaic/`; `tests/executable_execution.cpp` (`0e1a3b24...`) covers actual FALSE
and acknowledged-commit/failed-hook cases. Its existing NOTES records 36 strict fixture groups.
The outer caller must honor pending ownership after `false`: `src/local.cpp:1619-1625` returns
before legacy dispatch when `input_started` remains set.
This is a source/fixture ownership contract, not new measured engine behavior, native uptake or a
fuel/whole-input health proof.

M7's fixed-tile footprint corollary can nominate the current work group and incoming sources at the
priced own endpoints without repeated whole-catalogue scans. It preserves geometric coverage,
not fresh lifecycle/control or remote champion dependencies; those remain separately funded facts
or explicit conservative/model limits. This is a rules/SDK resource corollary, not a cost attribution
to any ordinary controller trace.

A dated navigation field or geometric arrival nominates an action or observation; neither completes
its service owner. Under unchanged semantic intent, a finite preparation cursor may advance only
through distinct completed stages. New input numbers, repeated requests, an already ready field,
or the same unreachable result earn no new progress. Completion of an observation owner requires
the matching fresh information; otherwise it must make actual approach/information progress or
consume a finite blocked resolver and hand off. Bind diagnostics to intent, roots/field dates,
cursor stage, actual command and the matching observation receipt before interpreting a repeated
preparation marker as a particular private failure. These are execution obligations, not claims
that the current controllers meet them or that one pure cursor establishes useful native service.

Canonical Mosaic R20 supplies an actual counterexample to treating motion as service progress. Exact ordinary
`e7bb9ebf2120`/RAW `c34418478a6d742585cc72f0f5705b566f6c4f4ae05ce2158ead70946c6decdb`, YBR
A/passive, banks 385 with 15 clears and 28 deaths over 2000 rounds and no faults/errors/exhaustions; last
positive monster work is R1472. In the complete R1490-1505 window all five actors stay alive, four
remain full HP, Vesper stays at 874 HP carrying 30 gems, and no incoming HP loss occurs. They execute
8/8/16/8/16 accepted Moves, including cycles, but no Basic, cast, Recall, tribute or shop. Each has
personally visible camps with positive HP recorded in Basic range. This establishes physical motion
without observed service progress, not death/fuel paralysis; range alone proves neither a proposed
attack's acceptance nor its complete continuation. Evidence:
`results/contenders/mosaic/native-r20/death-ordinary-a.json`, SHA256
`07489c9cc700ecfe186ac0bf21343c8a74de16857d0a89f3ec38bdaf4839fa8c`.

The separate late-only `27626bf36ca4`/RAW `6b7053d5...` compares all 11 archive members: nine are
byte-equal to that ordinary game, with only own stdout/fuel different. Its one cached-state packet
per actor at R1490 has four work/acquisition owners with retained jobs, not recovery/custody owners;
Vesper is BANK with an actual 30-gem bag and selected altar (20,15). The public window separately puts
her within range 1 of another recorded altar (19,14) at eight owner ends. Those are distinct altars and
poses: R1490's cached selected altar is distance 2, and opposing lock state was not queried. No
alternative tribute acceptance or safety is established. The join narrows the retained-owner facts,
not all private instructions/callback costs or the first transition into the loop. See
`results/contenders/mosaic/native-r20/late-private-join.json`, SHA256
`33d19b9cfb0aea8acb7776cf2b75e1410bb3bf9e04a1a9c476bf4f4de735c6a8`; exact late module binding is in
`native-service-r20-late-a.json` beside it. Prior trace `67f900` has different gameplay and supplies no
ordinary causal explanation.

Separately, the frozen R20 SOURCE-ONLY continuation review found that a generic `ExecutionState.next`
label can be ignored/overwritten, and Move/Recall/Bank can also inherit a paid-exit label. Such a label
is not evidence of a work-induced egress obligation or its completion. Retain a typed purpose and
matching actual receipt; close the obligation only with fresh qualified closure or a funded
replacement, preserving custody and the outstanding service. Blindly dispatching whenever
`next==exit` does not supply those premises. Review pins under `bots/contenders/mosaic/src/` are
`native_service_execution.h` `cd7cdd26d795978e615bcf0e37df85164829266f3aef74dd991bfb597b108645`,
`native_service_controller.h` `c681a94dd6bd48cf175c00f7b2aa478cc0cb7f0d07e215eeb39bf2a45b445899`,
`native_service_runtime.h` `ca4c65fa88298e5e1077878ba3c42767a3a2401a3e81a7cea6e41e50c7eca197`, and
`native_service_words.h` `2a3a569afd893ab0f782a0d36a080252f67a03dd493ac0feb0965177a9f5d624`.
This source contract has no native causal join to the ordinary late loops. Life/command eligibility,
accepted motion, useful work, bank custody and model closure remain separately evidenced facts.

The closed Mosaic cycle2 canonical window adds a different distinction: exposed waiting and quiet
arrival are not recovery. In ordinary `4c805bf156ad`/RAW `b74e0047...`, YBR A/passive R936-999,
five nonterminal melee Basics are each followed by four idle owner inputs with M/A/C ready and
cooldowns zero, while the surviving elk continues dealing 60 per camp phase. Ryth works at R941
with 1200 HP, first moves out at R946 with 900, reaches quiet (2,9) at R950 still with 900, returns
and works at R957 with 660, then works at R975 with 120, idles at R976 with 60 and dies at R977.
The selected other melee show the same unhealed return/death pattern. This is a bounded public
trajectory, not an explanation of every death or the private reason for four idle inputs.
Elk 104 in group 26 persists from 1065 to 536 HP: 265 melee damage plus 264 Vendetta damage, with no
observed reset. Sleep state is unknown; M10 separately establishes that sleeping camps retain
damage. Later whole-game team clear R1096 and reward R1102 show why zero own finish/payout credit does not
mean zero contribution. Evidence: `results/contenders/mosaic/native-r20/cycle2-death-window.json`,
SHA256 `eb47beeab10ff0b93590cfacaa80b9642132e0d87d0ce1b8729491fa581c5726`; closure is
`cycle2-closure.json` beside it. Scope is one pool map/side/passive game and selected window, not native parity or strength.

Admission must fund and retain a usable exit before exposed ingress/work; later computation or a
yield still crosses real camp phases. A quiet waypoint does not heal HP or discharge restoration
and custody liabilities: retain those until an actual qualified recovery receipt and usable resume.
Separately, the source-only review of frozen `5367` in `bots/contenders/mosaic/NOTES.md` reports sole-field
retirement on Work/egress changes and recovery armed only by immediate static lethality. That source
contract motivates retained exit/recovery ownership, but the public four-idle count does not prove
that private stage caused it. Neither an exit-after-every-Basic rule nor an alternate survival word
is qualified by this observation.

A previously priced return endpoint also needs current occupancy handling. An ally is a real blocker,
and its presence or recall is not a promise to vacate before the next damage phase. An invalidated
endpoint needs a re-established continuation: an actual alternative, a timely release premise, or an
explicitly affordable bounded wait. Existing route memory alone supplies none of those facts.
