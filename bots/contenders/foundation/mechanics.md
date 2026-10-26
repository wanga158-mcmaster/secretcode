# Mechanics: what the engine does

M1-M51 originate in engine 1.0.2 probes. Their version labels and limits remain in force. The
24 October independent renewal adds targeted corrections, M30's scoped engine 1.0.3 expiry witness
and new observations at M52-M54; it does not revalidate the historical ledger wholesale. See the
[foundation index](README.md).

The verified ledger of engine behaviour that changes decisions. Every entry was established in native
engine 1.0.2 games of scratch probe bots (`bots/contenders/_probe-*`) on purpose-built lab maps, read
from the bots' own observations and from the replays decoded omnisciently with the official visualiser
engine; or it was read from the engine's own catalog (`docs/reference/runtime-catalog.json`). Numbers
are named by the runtime value that supplies them (`constants::*`, `camp_stats`, `ability.info()`,
catalog fields), so an entry survives a balance patch when the bot reads that value at runtime; the
number shown is today's value. Status "documented, unverified" marks rule prose nobody has probed;
"unresolved" says why.

How to recheck: lab maps are JSON specs in `data/mechanics/maps/` built by
`tools/mechanics/mapgen.mjs` (the official editor MapBuilder, so they are valid engine maps);
`tools/mechanics/run_probe.py BOT MAP` plays a probe through arena's release builds and extracts its
log lines; `tools/mechanics/replay-events.mjs REPLAY` prints every step (attacks with source and
target, casts, channels, captures, respawns) and the health, position, gold and gem changes it caused.
Compact evidence extracts sit in `data/mechanics/*-events.txt`, `info-evidence.txt` and
`fuel-*.{csv,json}`; full runs and replays in `.cache/contenders/mechanics/runs/`. Probe ids: team A
draws champion ids 0..n-1 in draft order, team B n..2n-1.

| Id | Claim | Status |
| --- | --- | --- |
| M1 | Each round runs respawns, captures and camp attacks before any champion acts | verified (engine 1.0.2) |
| M2 | Turns interleave the teams one champion at a time; priority, then draft order, inside a team | verified (engine 1.0.2) |
| M3 | Timers tick at the owner's turn end and resolve at the owner's next turn start | verified (engine 1.0.2) |
| M4 | Dead champions keep their turns: shop, priority and shared memory only | verified (engine 1.0.2) |
| M5 | Monsters hit the nearest champion of either team by Chebyshev distance, re-chosen every round | verified (engine 1.0.2) |
| M6 | At equal distance the champion earliest in the round's turn order is hit, across teams too | verified (engine 1.0.2) |
| M7 | Monster attack range is Chebyshev `camp.attack_range()`; monsters never move | verified (engine 1.0.2) for wolf and elk; golem and monolith ranges are catalog values |
| M8 | Each awake monster in range deals its Strength once per round | verified (engine 1.0.2) |
| M9 | Damage wakes the whole cardinally connected group and nothing else | verified (engine 1.0.2) |
| M10 | Camps sleep after two camp phases with no champion in range, whatever the damage | verified (engine 1.0.2) (corrects earlier prose) |
| M11 | Gem and gold clusters never attack | verified (engine 1.0.2) |
| M12 | Monsters hit champions in bushes and champions channelling; only a recall is cancelled | verified (engine 1.0.2) |
| M13 | A cleared group's reward waits until someone captures it, with no expiry | verified (engine 1.0.2) |
| M14 | A capture needs `capture_time` + 1 consecutive capture phases of sole presence | verified (engine 1.0.2) (corrects earlier prose) |
| M15 | Any enemy body on any member tile resets the capture; adjacency does not | verified (engine 1.0.2) |
| M16 | Groups respawn `max_respawn_timer()` rounds after the payout, only with every member tile empty | verified (engine 1.0.2) |
| M17 | Recall: five owner turn ends, cancelled by any damage, blocks every other action | verified (engine 1.0.2) |
| M18 | On a spawn tile a champion heals `spawn_healing` x max health at each turn end | verified (engine 1.0.2) |
| M19 | Tribute: duration from the bag at start, banks the whole bag at completion, damage cannot stop it | verified (engine 1.0.2) |
| M20 | Altar locks: both teams can lock one altar; a lock blocks only the other team and cancels its channels | verified (engine 1.0.2) |
| M21 | Teleport crystal: Chebyshev 3 over any terrain onto free ground, three telegraphed ticks | verified (engine 1.0.2) (corrects earlier prose) |
| M22 | A champion kill pays the killer only; a camp kill destroys the bag; respawn after 60 own turn ends | verified (engine 1.0.2) |
| M23 | Basic attacks: Chebyshev range; ranged champions trade the step and the attack, melee get both | verified (engine 1.0.2) |
| M24 | An attack reset restores only the attack, needs the attack spent first, and is the turn's one cast | verified (engine 1.0.2) |
| M25 | One cast per turn; "excludes walking" blocks only the step; damage is modifier x Focus in Chebyshev areas that spare allies | verified (engine 1.0.2) (corrects earlier prose) |
| M26 | A one-round stun costs the target exactly its next turn; stuns refresh, never stack | verified (engine 1.0.2) |
| M27 | A stun cancels recall, tribute and teleport-crystal channels; the bag is kept | verified (engine 1.0.2) (one detail open) |
| M28 | Dashes: Chebyshev `ability_dash.range` (2), over holes, not through walls, onto free ground | verified (engine 1.0.2) |
| M29 | Heart of Ruin: missing health scales all its damage; the dash hits and stuns everything around the landing | verified (engine 1.0.2) |
| M30 | Shields: expiry follows the shield owner's start; timer0 can still absorb before it | historical values verified (engine 1.0.2); one current-native zero-boundary witness |
| M31 | Willow's heal: min(3 x Focus, missing health) on another ally; wasted at full health | verified (engine 1.0.2) |
| M32 | Vendetta: every kill she is credited with resets both cooldowns | verified (engine 1.0.2) |
| M33 | Varg's splash hits every other enemy and camp within Chebyshev 1 of the target, waking their groups | verified (engine 1.0.2) |
| M34 | Ren retaliates 0.2 x Focus against the source of every damage instance, monsters included | verified (engine 1.0.2) |
| M35 | Marcella: stun-immune; her enhanced reset turns the next hit into Strength plus a one-turn stun | verified (engine 1.0.2) |
| M36 | Cassi and Issac die together: one kill reward, both bags to a champion killer, both lost to a camp | verified (engine 1.0.2) |
| M37 | Gold is fractional; gold bonuses add and scale every gold gain after the split | verified (engine 1.0.2), except the inferred Treasure-alone capture case |
| M38 | Marcella earns 2 gold per tributed gem, scaled by her gold bonuses | verified (engine 1.0.2) |
| M39 | Shards add the catalog growth; five combine into an emblem; Power counts six, Bruiser 5+1+1 | verified (engine 1.0.2) |
| M40 | Jungle x0.7 against camps, Durability x0.9 against everything; they multiply | verified (engine 1.0.2) |
| M41 | Home emblem: recall in three owner turn ends | verified (engine 1.0.2) |
| M42 | Selling: Tavi only, on spawn, full value, several per turn, not earned gold | verified (engine 1.0.2) |
| M43 | Ryth gains Strength from bonus health; Prism gains Focus per carried gem | verified (engine 1.0.2) |
| M44 | Vision is personal: a Chebyshev-square shadowcast from the champion plus its own ward | verified (engine 1.0.2) (corrects earlier prose) |
| M45 | A visible enemy is readable almost completely, except its gold | verified (engine 1.0.2) |
| M46 | Bushes hide from outside only; a blocked step reveals the hidden occupant for one round | verified (engine 1.0.2) (one detail open) |
| M47 | Wards: Chebyshev 2 placement almost anywhere, 10-turn recharge, permanent, visible to enemies | verified (engine 1.0.2) |
| M48 | Observations are live within a round | verified (engine 1.0.2) |
| M49 | Camp and group state are readable only in vision; respawn timers are exact when seen | verified (engine 1.0.2) |
| M50 | Shared memory: 8 bytes per team, zero at start, visible to later actors at once | verified (engine 1.0.2) |
| M51 | Draft memory: 1024 bytes written from offset 0, frozen in play | verified (engine 1.0.2) |
| M52 | A zero respawn timer does not establish a living group | observed (engine 1.0.3, one saved Warband game) |
| M53 | Ranged walk then reset is a real allowance transition | observed (engine 1.0.3, one truncated Vesper guard witness) |
| M54 | A failed recall is not made safe by empty personal vision | observed (engine 1.0.3, one saved Willow death window) |

## Round structure and turn order

### M1. Each round runs respawns, captures and camp attacks before any champion acts
Claim: A round's phases, in order: round start, camp respawns, spell entities (wards), camp captures,
camp turns (every awake camp acts, in camp id order), then champion turns. Each champion turn is
wrapped by a turn start (timers that reached zero resolve: respawn, recall arrival, tribute banking,
teleport arrival) and a turn end (timers decrement; spawn healing). Camps never act during champion
turns: a monster damaged in round r first strikes in round r+1's camp phase.
Evidence: replay timelines of every probe game (`replay-events.mjs --all-steps`; phase names from the
official visualiser bundle); first strike after a wake in mon-target r20 -> r21, mon-metric r20 -> r21.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: a patch to the turn order (phase list in the visualiser's PanelChrome bundle).
Consequence: Whatever a champion does in its turn, it cannot pre-empt the camp phase: ending a turn
inside an awake camp's reach costs that camp's hit before the next decision, and a payout in round r
lands before anyone acts in round r. Structure: the camp phase is a fixed tax on standing ground, paid
at round start; a fight next to awake camps is priced in camp hits per round, independent of turn order.

### M2. Turns interleave the teams one champion at a time; priority, then draft order, inside a team
Claim: In round r the starting team's k-th champion acts, then the other team's k-th, for k = 1, 2, ...;
team A starts odd rounds (round 1 is the first play round), team B even rounds. Inside a team the order
is higher current-round scheduling priority first, ties by draft order (champion ids). In the pinned
SDK `prev_priority()` reads the priority used for the current round and `priority()` reads the next
round's scheduled priority; the latter is not a current-rank query. `set_priority(n)` takes effect from the
next round and persists; a dead champion still has a scheduled input and can set priority. A team with more
bodies (Cassi & Issac makes 4 against 3) plays its extra bodies after the last interleaved pair, in both
parities (r5: A1 B1 A2 B2 A3 B3 A4; r6: B1 A1 B2 A2 B3 A3 A4). Cassi and Issac are consecutive,
Cassi first, in the observed equal-priority draft ordering; different later priorities can separate
their ranks.
Evidence: mon-target r1/r2 (A0 B3 A1 B4 A2 B5 / B3 A0 B4 A1 B5 A2); mon-metric: B stellar
`set_priority(5)` in r3 acts first in team B from r4 on; unequal bodies and dead slots: info-order
(`data/mechanics/info-evidence.txt`).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: a patch to scheduling; a draft with both teams taking Cassi & Issac.
Consequence: Team inputs interleave within a round until an unmatched tail, not across every round
boundary. The recorded equal-body sequence already ends `...A2 B5 | B3 A0...`: B5 and B3 are
consecutive team-B inputs across the boundary. No extra Cassi/Issac body is needed for that adjacency.
For two particular champions, projecting out all other actors gives C2's same-rank double turns or
different-rank strict alternation only while their relative ranks remain fixed across the rounds in
question; other actors and camp phases can still intervene between those projected pair inputs.
Next-round priority changes can alter ranks and therefore the next pair ordering. Use the actual
current schedule and a committed or explicitly uncertain next schedule rather than a universal
one-opposing-input reaction bound. Priority changes ordering, not the fact that each body has one
scheduled owner input per round; a scheduled input need not contain a usable action (M45).

Current phase clarification: under pinned `GameRules.md:122`, from a healthy current owner input's
final action to that body's next scheduled input in R+1, a continuing game crosses exactly one
round-start camp batch. Zero, one or two opposing champion opportunities do not change that count.
One batch may contain attacks from several sources: M30's current-native hits by camps 64 and 66 are two
sources in one batch, not two camp batches. A shield with timer1 in the current ready owner input
can absorb that next batch before expiring at the owner's start, subject to depletion/removal (M30).
This is a pinned phase-contract deduction, not a new native phase probe. Multi-input exits,
stun-delayed next usable actions or suspended execution need their actual intervening batch count;
existing spell/world-effect updates are separate hazards, not extra camp batches.

### M3. Timers tick at the owner's turn end and resolve at the owner's next turn start
Claim: Channels (recall, tribute, teleport crystal) and the respawn timer decrement at the owner's turn
end, the starting turn included, and complete at the owner's first turn start after reaching zero; a
channel of length L started in round r completes at the owner's turn start in round r+L (recall
`constants::recall_time` 5: r23 -> r28; tribute 2: r30 -> r32; crystal `constants::tp_crystal_time` 3:
r45 -> r48). At a turn start, `remaining_channel_time()` = completion round - current round (recall
started r23 read 4 at r24, 2 at r26, 1 at r27). A visible enemy's channel time can be read the same way
(M45).
Evidence: ch-basic logs and replay (`data/mechanics/channels-events.txt`).
Rests on: M1.
Status: verified (engine 1.0.2).
Recheck: the timer semantics in the rules ("Channels count down at owner turn ends").
Consequence: A completed channel resolves before its owner acts, so a recall arrival turn is a full turn
(shop, move, heal at its end). Every channel is exposed to L rounds of enemy turns and camp phases; an
observer can predict the completion round exactly. Structure: channels are telegraphed commitments
whose interrupt windows are fixed in length and readable by the enemy.

### M4. Dead champions keep their turns: shop, priority and shared memory only
Claim: A dead champion has position (-1,-1), health 0 and no vision, but still takes its turn. It can
buy items and consumables (limited only by gold: E52/E59), `set_priority` and write shared memory.
Move (E21), ward (E63), cast (E26), attack (E40) and recall (E44) throw.
Evidence: ch-lock B ryth dead r46-r106 (shop 20 -> 10 gold buying a crystal, set_priority, TRYALL);
info-order (`info-evidence.txt`).
Rests on: M2.
Status: verified (engine 1.0.2).
Recheck: shop rules in a patch.
Consequence: Death is the free shopping trip: buy with the whole purse while dead, so respawn turns can
move at once. A dead body can still coordinate (shared memory, priority). Structure: gold never sits idle
through a death.

## Monsters

### M5. Monsters hit the nearest champion of either team by Chebyshev distance, re-chosen every round
Claim: In each camp phase every awake monster picks, among champions within its attack range, the one at
the smallest Chebyshev distance and deals its Strength to it; the choice is remade every round (it
switches the moment someone is nearer, and back). The champion that damaged it gets no preference.
Evidence: mon-target: stellar (id 2) woke the wolf from 3 tiles, the wolf hit vesper (id 0, also 3
tiles) r21-r23, switched to ryth when it stood 2 tiles away (r24-r27), back to vesper when ryth left
(r28-r31), to stellar when vesper left (r32-r33); mon-metric side A: with ryth leaving, the elk hit
stellar at (dx,dy) = (-3,-3) (Chebyshev 3, Euclid 4.24) rather than vesper at (-4,0) (Chebyshev 4,
Euclid 4), so "nearest" is Chebyshev (`data/mechanics/monsters-events.txt`).
Rests on: M1.
Status: verified (engine 1.0.2).
Recheck: a patch to camp AI.
Consequence: The nearest body tanks, not the damage dealer: a team picks its tank by position, and a
caster can hit from behind a nearer ally. Monsters are neutral: an enemy standing nearer to an awake camp
takes its hits, so fighting next to awake camps punishes whoever stands closer. Structure: aggro is a
pure function of geometry and turn order, so it is predictable and can be planned and exploited.

### M6. At equal distance the champion earliest in the round's turn order is hit, across teams too
Claim: Ties at the minimum Chebyshev distance go to the champion that acts earliest in the current
round (M2): inside a team the higher priority, then the lower draft id; between teams the target
alternates with the starting team, so a cross-team tie flips every round.
Evidence: mon-target and mon-target-v (vesper id 0 hit over stellar id 2, whichever of them attacked);
mon-metric side B (stellar at priority 5 hit over vesper, though vesper has the lower id); info-order
cross-team tie (Cassi A1 hit in odd rounds, Vesper B1 at priority 5 in even rounds, r21-r27).
Rests on: M5, M2.
Status: verified (engine 1.0.2).
Recheck: as M5.
Consequence: To take aggro for certain, stand strictly nearer; at equal distance raise the tank's
priority. Against an enemy at equal distance a camp splits its hits by round parity. Structure:
priority is a free aggro lever inside a team.

### M7. Monster attack range is Chebyshev `camp.attack_range()`; monsters never move
Claim: A monster hits only champions within Chebyshev distance `camp_stats.base_attack_range` (wolf 3,
elk 4, golem 4, monolith 5; the clusters' 3 is moot, M11). Monsters never leave their spawn
tile.
Evidence: mon-metric: the elk (range 4) hit vesper at (dx,dy) = (-4,+4) (Euclid 5.66) and stopped when
she stood at Chebyshev 5; mon-target group wolves (range 3) hit ryth at Chebyshev 3 and not at 4
(r61-r63); mon-cast: Stellar's range-4 spell from Chebyshev 4 took a wolf 1350 -> 1210 -> 1070 at r15 and
r23 with no wolf attack at all, while Ryth's range-4 area cast on an elk from 4 was answered by elk hits
from r17; no camp position ever changed in any replay.
Rests on: M5.
Status: verified (engine 1.0.2) for wolf and elk; golem and monolith ranges are catalog values.
Recheck: catalog `camps[].stats.base_attack_range`.
Consequence: Spells with catalog range 4 outrange wolves: a caster at Chebyshev 4 chips a wolf group
without being hit (the group wakes, finds nobody in range and sleeps again, keeping the damage,
M10); elk and golem (4) match spell range, and the monolith (5) outranges every ability. Structure:
risk-free chip damage exists exactly where an ability's range exceeds a monster's, limited only by the
8-round cooldown.

Current rules/SDK corollary: fixed spawn tiles and queried Chebyshev design ranges permit an
immutable footprint index `I(p) = {camp: distance_inf(spawn,p) <= range}`. Union its sets over the
priced own endpoints and include the entire current physical work group, even nonharmful clusters.
This reproduces that geometric incoming-source/work nomination without re-observing the full
catalogue. Preserve stable physical IDs, group membership and required source order; deduplicate
each source. Pinned `GameRules.md:207`, SDK `camp::spawn_position()` and `base_stats()`/`attack_range()`
support this corollary; it is not a new native performance measurement.

The index supplies no fresh life, HP, shield/control, sleep or respawn fact. No path, wall or leash
pruning follows from range geometry alone. There is no camp batch between uninterrupted same-input
primitives, one batch before the next scheduled own input (M1-M2), and at most one hit per source in
that batch (M8). Effects still wake whole physical groups (M9), including Varg's collateral groups
(M33); unknown champions can wake local sources too. A local incoming-source set is not an exact
joint-world closure: remote hits can change Heart's HP and later damage. Include that separate causal
cone or retain an explicit envelope rather than treating remote interactions as absent.

### M8. Each awake monster in range deals its Strength once per round
Claim: Per camp phase each awake monster with a champion in range deals `camp_stats.base_strength`
(wolf 20, elk 60; catalog: golem 120, monolith 200, clusters 0) to its chosen target, independently of
the other members; three awake wolves on one champion deal 60 a round. Defensive modifiers: M40.
Evidence: replay damage steps (wolf 20, elk 60, three wolves 3 x 20 on ryth, mon-target r56-r61).
Rests on: M5.
Status: verified (engine 1.0.2).
Recheck: catalog `camps[].stats.base_strength`.
Consequence: A group's incoming damage per round is the sum of the Strength of its awake members whose
range covers the target's tile, so standing where only part of a group reaches cuts the damage (one wolf
of three at r62). Structure: camp damage is a deterministic, geometric function the bot can compute
exactly before engaging.

### M9. Damage wakes the whole cardinally connected group and nothing else
Claim: Damaging one monster wakes every member of its physical group (`camp.camp_group()`, cardinally
connected camps); camps in other groups stay asleep, even diagonally adjacent ones and even with a
champion in their range. Proximity alone never wakes a camp.
Evidence: mon-target: one hit on (9,24) woke (9,24), (9,25), (10,25); the wolf at (10,23), diagonal to
the group and 2 tiles from ryth, never attacked; ch-damage: an asleep elk ignored a champion standing in
its range.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: map group construction (`map::camp_groups()`).
Consequence: The engagement unit is the group: forecast the whole group's damage before the first hit.
Champions can walk past sleeping camps freely. Structure: a group is a single switch; splash or area
damage that touches a second group wakes it too (M33).

### M10. Camps sleep after two camp phases with no champion in range, whatever the damage
Claim: An awake camp falls asleep at the `constants::camp_sleep_time`-th (2) consecutive camp phase with
no champion of either team within its range (wolf hit stellar at r33's phase, nobody in range at r34 and
r35, asleep from r35); damage counts like a champion in range at the next camp phase (woken by a hit in
round t with nobody in range: awake in t+1 and t+2, asleep from t+3). While any champion stays in range
it stays awake indefinitely without further damage (13 rounds in mon-target). Asleep camps keep their
damage indefinitely (`docs/rules-notes.md`).
Evidence: mon-target camp state logs r21-r41; mon-cast (spell hits at r15 and r23 from out of range:
asleep from r18 and r26); rules-notes probes.
Rests on: M5.
Status: verified (engine 1.0.2). The rule prose ("does not take damage for 2 turns") is wrong.
Recheck: `constants::camp_sleep_time`.
Consequence: Standing in range keeps a camp hitting you; stepping out for two rounds resets it, and
partial damage is permanent progress, so big groups can be sieged over several trips. Structure: camp
aggression is a function of presence, not of damage history.

### M11. Gem and gold clusters never attack
Claim: Gem and gold clusters (`base_strength` 0) wake when hit but produce no attack step at all, so
they never damage or cancel a recall beside them.
Evidence: mon-metric side B: ryth hit a gem cluster and recalled beside it, arriving 5 ticks later
(r21 -> r26); no cluster Attack step in any probe replay.
Rests on: M8.
Status: verified (engine 1.0.2).
Recheck: catalog strength of clusters.
Consequence: Clusters are free gold and gems guarded only by time (120 health); they are the safe
targets for a lone or weakened champion. Structure: clusters are pure tempo, never risk.

### M12. Monsters hit champions in bushes and champions channelling; only a recall is cancelled
Claim: Monsters see through bushes and target a champion standing in one; they also target champions
that are recalling, tributing or teleporting. A monster hit cancels a recall (M17); it does not
cancel a tribute (M19) or a teleport crystal (M21).
Evidence: mon-metric side A (wolf hit vesper inside a bush r41-r60); ch-basic (wolf cancelled ryth's
recall three times, r16-r19); ch-damage3 (elk hit stellar r30-r31 during her tribute, banked r31);
ch-damage (elk hit vendetta r36-r38 during his crystal channel, arrival r38).
Rests on: M5.
Status: verified (engine 1.0.2).
Recheck: as M5.
Consequence: Never start a recall within reach of an awake camp unless another body is nearer; tribute
and crystal channels survive camp damage, so an altar beside an awake camp is still usable at a known
cost. Structure: bushes hide from champions, never from monsters.

## Capture and respawn

### M13. A cleared group's reward waits until someone captures it, with no expiry
Claim: When a group's last monster dies (during a champion turn) its reward becomes available at once
and stays available until captured; the respawn timer does not start before the capture.
Evidence: cap-contest Z: cleared r8, left alone, captured r48-r53 (7 gems); camp-group state
`reward_available` throughout.
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: a capture-timeout setting in a patch.
Consequence: Clearing and capturing are separable: the clearer has no claim, so a group cleared and left
can be taken by anyone, and a cleared group is a standing invitation. Structure: the value of a clear is
only realised by holding a tile for the capture time.

### M14. A capture needs `capture_time` + 1 consecutive capture phases of sole presence
Claim: In each round's capture phase, if champions of exactly one team stand on member spawn tiles of a
cleared group, the first such phase arms the capture and the reward pays at the `constants::capture_time`
(5)-th later phase: stepping on during round k pays at the start of round k+6, before anyone acts.
Rewards (`camp_group.gold_value()` / `gem_value()`, the members' sums) split equally among that team's
champions on member tiles at the payout phase; the integer remainder goes to the lowest champion id.
Extra capturers do not shorten it.
Evidence: cap-basic (G2: on r7, armed r8, paid r13: 20 gold and 7 gems to stellar id 1 and vendetta id
2 as 10 g + 4 gems and 10 g + 3 gems; G1: on r8, armed r9, paid r14), cap-contest (Y: on r18, paid r24)
(`data/mechanics/capture-events.txt`).
Rests on: M1.
Status: verified (engine 1.0.2). The rule prose ("five rounds") undercounts by the arming phase.
Recheck: `constants::capture_time`.
Consequence: A capture is six rounds of a stationary body on a known tile; one capturer per group (more
bodies only split the reward). The payout cannot be stopped in its own round. Structure: captures are the
game's main exposure window; whoever can interrupt six rounds of stillness denies income.

### M15. Any enemy body on any member tile resets the capture; adjacency does not
Claim: Capture progress resets the moment an enemy champion steps onto any member spawn tile and restarts
(armed again) at the first capture phase after it leaves; while both teams are on tiles nobody
progresses. An enemy standing next to the tiles does not contest. Progress also resets when the team's
last champion leaves the tiles (immediately, at the step); if one ally stays, the capture continues and
pays only those present.
Evidence: cap-contest X (B stepped on (16,4) in r34: reset at the step, re-armed r37, paid r42), Y (B
adjacent for the whole capture: paid on time), cap-basic G1' (ryth stepped off r10, back r11: re-armed
r12, paid r17) and G2' (vendetta left, stellar stayed: paid r13 to stellar alone).
Rests on: M14.
Status: verified (engine 1.0.2).
Recheck: as M14.
Consequence: One enemy body on one tile denies a capture for as long as it stays, so a capturer must
remove or outlast contesters; standing adjacent achieves nothing. Leaving for one round costs the whole
progress. Structure: capture is a presence game on tiles, decided by who can hold a tile, not who is near.

### M16. Groups respawn `max_respawn_timer()` rounds after the payout, only with every member tile empty
Claim: The respawn timer starts at the payout and lasts `camp_group.max_respawn_timer()`, the longest
member respawn (catalog: clusters 50, wolf 40, elk 60, golem 80, monolith 150; a wolf + gold cluster
group reports 50). The group respawns at a round-start respawn phase once the timer has elapsed and no
champion of either team stands on any member tile; otherwise at the first respawn phase after all its
tiles are vacated. Its timer is readable while a member tile is in vision (M49).
Evidence: cap-basic G2 paid r13 -> respawned r63; G1 paid r14 but ryth stayed on it until r70 ->
respawned r71; ch-extra: A's ryth stood on the centre group B captured (paid r29, due r79) until r90 ->
respawned r91; groups metadata in `capture-events.txt`.
Rests on: M14.
Status: verified (engine 1.0.2).
Recheck: catalog respawn values (GameRules monster table), `max_respawn_timer()`.
Consequence: Standing on any tile of a captured group freezes its respawn for both teams: a denial tool
against enemy camps, and a trap for one's own capturer who idles on the tile. Structure: map income is
throttled by occupancy; a body parked on an enemy camp is worth that camp's whole income stream.

## Channels

### M17. Recall: five owner turn ends, cancelled by any damage, blocks every other action
Claim: `recall()` channels `constants::recall_time` (5) owner turn ends and lands at the owner's turn
start on the first free own spawn tile in the order of the initial placement (M3; the bot cannot
choose it); recall on a spawn tile is allowed. Any damage during the
channel cancels it, from monsters or champions. While recalling only `set_priority` and shared-memory
writes work; move, attack, cast, ward, shop and a second recall throw E34 ("Cancel recall before
acting"). One recall start per turn: start, cancel, start again in the same turn throws E113; cancelling
when not channelling throws E115. Arrival heals at that turn's end (M18). Home emblem: M41.
Evidence: ch-basic (r23 -> r28; wolf hits cancelled recalls r15-r19; TRYALL at r24; cancel rules r35-r37;
recall on spawn r1), ch-damage (B ryth's hit at r41 cancelled A ryth's recall), mon-metric (B ryth
recalled beside a gem cluster r21, arrived r26 on (31,13), the team's first placement tile, vacated
earlier by vesper), `channels-events.txt`.
Rests on: M3.
Status: verified (engine 1.0.2).
Recheck: `constants::recall_time`, Home emblem.
Current timing contract: pinned `GameRules.md:122,271,273,304,374` and queried editor-engine 1.0.3
`runtime-catalog.json:282,292` retain base Recall 5 owner-END ticks, Home 3, and spawn healing 1.
For an accepted uninterrupted base Recall at R50, with ordinary owner ends and an available landing,
R50-54 END leaves timers 4,3,2,1,0; the R55 camp batch still precedes Recall arrival at R55 owner
START. That input may act/shop if actually usable, but arrival itself does not heal. Remaining on
spawn through R55 END heals HP; R56 is only the first possible input starting with that HP, subject
to intervening damage/control and a fresh actual read. Zero-buff expiry belongs to owner START,
not to the Recall command midway through an input; existing cooldowns keep ticking, not resetting.
The current SDK `recall()` (`cambc.h:3780`) delegates to the host and encodes no unused-M/A/C gate.
Historical ch-basic R16 accepts Move->Recall; this does not newly qualify every spent-C or nonzero
restriction combination on current native. Require actual start acceptance/channel readback.
Rules name damage, stun and death as interruptions; whether a positive hit absorbed wholly by a
shield cancels Recall, and arrival behavior when every own spawn tile is occupied, remain unresolved
by the retained evidence here. Do not infer either shield-protected completion or an invented free
landing. Current passive Recall R191 -> purchase R196 in
`results/contenders/warband/word-service/service-program-native-witness.json` has HP700 already full;
it attests transport/purchase timing, not HP restoration or a safe opposed Recall alternative.
Consequence: Recall is safe only where no enemy and no awake camp can reach the champion for five
rounds; an enemy needs a single damage instance inside the window to deny it. Fleeing first and
recalling out of reach beats recalling in place. Structure: escape-by-recall is gated by distance, not
health; a chaser in reach turns every recall into a wasted turn.

### M18. On a spawn tile a champion heals `spawn_healing` x max health at each turn end
Claim: At the owner's turn end on an own spawn tile health rises by `constants::spawn_healing` (1.0) x
max health, i.e. to full in one turn end; the recall arrival turn counts.
Evidence: ch-basic: ryth arrived r28 at 1060/1200 and ended that turn at 1200.
Rests on: M3.
Status: verified (engine 1.0.2).
Recheck: `constants::spawn_healing`.
Consequence: With queried `spawn_healing` 1, an owner END on own spawn restores HP up to current max
health. Preserve actual cooldowns, spent allowances, trinket/consumable capacity, buffs and outstanding
custody/service debt; health recovery does not reset them. Recall arrival at owner START is not this
healing event. Staying through END and then observing the next actual state separates restored HP
from a usable renewed service, since intervening damage/control may change both. Current pinned
`GameRules.md:122,374` and editor-engine 1.0.3 `runtime-catalog.json:292` support this health/timing
contract; the retained native healing witness above remains historical engine 1.0.2.
Structure: renewable health has a dated return/healing cost and still needs a complete continuation.

### M19. Tribute: duration from the bag at start, banks the whole bag at completion, damage cannot stop it
Claim: `submit_gems(altar)` needs an altar within `constants::submit_gems_range` (1; Chebyshev per the
SDK, orthogonal adjacency exercised) and at least one gem; it channels max(`submit_gem_minimum_duration` 1, ceil(`submit_gem_duration_multiplier`
0.1 x gems at start)) owner turn ends and at the owner's turn start after zero banks every gem then
carried, including gems captured during the channel (captures resolve first, M1). Damage does not
cancel it; stun, death and an enemy lock placed on that altar do (M20; stun: M26). While
tributing only `set_priority` and shared memory work (E35 otherwise).
Evidence: ch-basic: 14 gems at start (2 ticks, r30 -> r32), +7 captured at the r32 capture phase, 21
banked at r32's turn start; ch-damage3: elk hits at r30-r31 during the channel, banked r31.
Rests on: M3, M14.
Status: original laws verified (engine 1.0.2); the following current ordinary movement/channel case
is observed on engine 1.0.3 without revalidating every historical interruption case.
Recheck: the submit settings in `constants`.
Consequence: Tribute in chunks of up to 10 gems for one-tick channels; a capture paying out during the
channel adds to the bank for free. Defending a tribute means preventing stuns, kills and enemy locks, not
damage. Structure: banking time is ceil(gems / 10) exposed rounds, so the risk of carrying grows with
the bag while the cost of banking grows only stepwise.

Current native witness: RAW Warband `8b536afce6ea`, module
`32dbd4936df8c1823e8fa632fbf1259ff46b832b7f54dab5882622ee62254fbe`, passive Packs B. Willow (actor 4) at
R46 actually Moves (4,8)->(3,7) and starts Submit in the same owner input, with queried duration 2.
R47 has timer 1; R48 owner-start payout changes bag 14->0 and team score 14->28, with HP700 unchanged.
The retained proof is `results/contenders/warband/word-service/conversion-native-witness.json`, SHA256
`52d60e9806489c788d75be2090538791618fb0115088aaefc2bea01a944642ad`. This establishes that actual
Move->Submit/N2 clock and payout, not native permission with both A/C unavailable or recovery of an
Unknown receipt. SDK conservation-supported inference and the actual native payout remain distinct.

Completion of this bank does not establish renewed service: R48-55 are all EndTurn. Willow's last
monster work is R29, and after the R48 payout there is no further work, receipt or bank through R2000.
The full paired panel in `conversion-passive-packs-r1.json` reports terminal bank/round rows
1028/1236 and 56/2000 versus fresh economic controls 1010/989 and 1004/1026; own technical counters
and deaths are zero. B gains 14 over retained
R7 without extra measured work/clear/spend. `conversion-service-witness.json` retains that separate
whole-game limit. No current-main, injected-role, safety or complete-production claim follows.

### M20. Altar locks: both teams can lock one altar; a lock blocks only the other team and cancels its channels
Claim: `consumables()[altar_lock].use(altar)` places a lock on an altar within Chebyshev
`constants::ward_placement_radius` (2; at 3 it throws E119 "outside ward placement range"). Each team can
hold one lock per altar (a second own lock is refused per the header, E120; not exercised), so both
teams can lock the same altar. A lock blocks the other team's tributes
there and its placement cancels the other team's tribute channels at that altar, even when the
channelling team has its own lock on it. The owner team keeps tributing through its own lock. An enemy
removes it with `constants::altar_lock_hits` (5) hits: basic attacks on the lock (melee, or ranged from
attack range 3) or tribute attempts at the locked altar, each of which is converted into one hit. A
team cannot attack its own lock (E42). A lock persisted 39+ rounds at one hit remaining; nothing expires
it. Pitfall: `vision::altar_lock_at(p)` returns the own lock when both exist; pick the enemy lock from
`vision::altar_locks(team::enemy)`.
Evidence: ch-lock (B lock at r35 cancelled A's channel; B tributed through it r37-r39; A's four tribute
attempts took 5 -> 1, the fifth at r80 removed it, A banked r83), ch-extra (diagonal (2,2) placement
fine, Chebyshev 3 E119; A and B locks on one altar), ch-lock2 (A's lock placed during B's channel through
B's own lock cancelled it; direct ranged and melee hits 5 -> 2; B's tribute attempt hit A's lock).
Rests on: M19.
Status: verified (engine 1.0.2).
Recheck: lock settings; a rule change to one lock per altar.
Consequence: Pre-locking one's own tribute altar does not protect the channel: the enemy can still lock
the same altar and cancel it, so a lock protects nothing on its own (contradicts the premise of main's
D76 lock mode 2). Locks are offence: 10 gold to cancel a channel and to tax five enemy attack turns.
Structure: altar denial is cheap and repeatable; tribute security comes from vision and bodies, not items.

### M21. Teleport crystal: Chebyshev 3 over any terrain onto free ground, three telegraphed ticks
Claim: `consumables()[tp_crystal].use(dest)` starts a `constants::tp_crystal_time` (3) channel to a
destination within Chebyshev 3, ignoring terrain in between; the destination must be free walkable ground
(walls, holes, altars and enemy spawn tiles throw E22 "Forbidden landing tile", a known occupied tile E22
"Known occupied destination", Chebyshev 4 E37); bushes are allowed. A failed attempt does not consume the
crystal. Arrival at the owner's turn start (M3); damage does not cancel it; actions throw E123
while teleporting; the reservation is visible to others (`vision::teleports()`). Stun: M26.
Evidence: ch-basic (wall, hole, range-4 refusals then a diagonal (3,3) jump across a wall block, r45 ->
r48), ch-extra (enemy spawn, altar, occupied refusals; bush landing), ch-damage (elk hits r36-r38 during
the channel, arrival r38).
Rests on: M3.
Status: verified (engine 1.0.2). The feature table's "TP Crystal can reach walls" is wrong (it can cross
them, not land on them).
Recheck: crystal range and landing rules (not exposed as settings; reprobe after a patch).
Consequence: Crystals open wall- and hole-gated pockets and shortcuts but are announced three rounds
ahead with a fixed landing tile, so an enemy that sees the reservation can meet the arrival. Structure:
mobility that ignores terrain is bought with predictability.

## Death

### M22. A champion kill pays the killer only; a camp kill destroys the bag; respawn after 60 own turn ends
Claim: The champion landing the killing blow receives `constants::kill_gold` (100; gold multipliers:
M37) and every gem the victim carried; assisting champions receive `assist_gold` (0). A champion
killed by a camp loses its carried gems to nobody. The dead champion leaves the map and respawns at its
own turn start after `constants::respawn_time` (60) of its own turn ends, at full health on the first
free own spawn tile in placement order (spawn regions have at least 9 tiles, the map reader rejects
fewer, so one is always free): killed after its own turn in round r it respawns in round r+61, killed
before it in r+60.
Evidence: ch-lock (A vendetta's last hit at r46: 0 -> 100 gold, 0 -> 7 gems, kills 1; A ryth, which dealt
half the damage, got nothing; B ryth respawned r107 at (30,10)), ch-damage2 (ryth died to the elk at r48
with 7 gems; nobody gained them); info-order (stellar killed in r18's camp phase had dead turns r18-r77,
respawned r78 on a spawn tile other than its first).
Rests on: M3.
Status: verified (engine 1.0.2).
Recheck: kill and respawn settings.
Consequence: Focus fire should let the champion that benefits most (bag size, gold multiplier, Vendetta's
reset) take the last hit; a full bag is worth more to the enemy than to a camp. Sixty rounds is about 3%
of a 2,000-round game per death, plus the lost bag. Structure: kills transfer value between teams, camp
deaths only destroy it, so risking a bag near enemies is worse than near camps.

## Combat and abilities

### M23. Basic attacks: Chebyshev range; ranged champions trade the step and the attack, melee get both
Claim: A basic attack deals `strength()` to a visible target within Chebyshev `attack_range()` (melee 1,
ranged 3; diagonal (3,3) is in range). For ranged champions an attack clears can_move (a step then
throws E36 "Movement already used") and a step clears can_attack (E41); melee champions step and attack
in either order in one turn.
Evidence: abil-basic r20-r22, state flags after each action for five champions; mon-metric (Vesper's
(3,3) hit) (`data/mechanics/abil-evidence.txt`).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: champion attack types in the catalog.
Consequence: An ordinary ranged basic consumes movement, and ordinary movement consumes attack.
This is the default interaction, not a prohibition on every move-plus-hit word: a ready attack reset
can restore attack after movement (M53). The reset still consumes the input's one cast.

### M24. An attack reset restores only the attack, needs the attack spent first, and is the turn's one cast
Claim: After attack, reset, the flags read "A": the attack is back, a ranged champion's movement is not.
A reset while the attack allowance is still available throws E26 ("An available attack cannot be
reset"); this is not a requirement for a preceding basic attack, because ranged ordinary movement
also spends attack (M53). A second cast in a turn throws E27. Cooldowns are 8 owner turn ends
(catalog `cooldown` 8 for every ability): a cast in
round r reads cooldown 8 and is ready again in round r+8.
Evidence: abil-basic (Tavi, Willow, Vesper, Ryth; Tavi cast r20, ready r28) (`abil-evidence.txt`).
Rests on: M23.
Status: verified (engine 1.0.2).
Recheck: catalog ability cooldowns.
Consequence: The ordinary burst word is "attack, reset, attack"; a ranged "move, reset" is also
accepted in M53 and restores attack without movement. A suffix basic still needs its actual target,
range, visibility and live allowance checked. No reset word may borrow another cast in the same input.

### M25. One cast per turn; "excludes walking" blocks only the step; damage is modifier x Focus in Chebyshev areas that spare allies
Claim: One successful cast per turn across both slots. A cast whose payload has
`exclusive_with_movement` (catalog attribute bit 1) cannot follow a step (E126) and forbids a later step
(E127), but combines with a basic attack in either order: a ranged caster's standing turn is spell plus
hit. Dashes, resets, heals and Cassi's stun are compatible with the step; a dash uses only the cast, so
step plus dash travels three tiles. Ability damage is `ability.info()` modifier x Focus
(`ability.damage_amount()`: Stellar 140 and 98, Tavi 154, Ryth 100, Prism 120, Ren 75); area payloads hit
enemies and camps within Chebyshev `radius` (1) of the target tile, never allies and never the caster.
Defensive modifiers apply (Durability, M40; Monster on camps).
Evidence: abil-basic r20-r24, r33; abil-varg r51 (`abil-evidence.txt`).
Rests on: M23.
Status: verified (engine 1.0.2). The rules' "this same rule applies for damaging abilities that are
ranged" is wrong: a damaging spell blocks the step, not the basic attack.
Recheck: catalog ability attributes and payloads.
Consequence: Casters stand and deliver spell plus basic hit; there is no friendly fire, so area spells
can land on a melee scrum. Structure: a standing ranged champion out-damages a moving one by a full
spell per turn, so ranged kits reward holding ground and melee kits reward closing.

### M26. A one-round stun costs the target exactly its next turn; stuns refresh, never stack
Claim: A stun (catalog payload `duration` 1) cast before the target's turn in round t costs its turn t;
cast after its turn, its turn t+1. `buff_stun.duration` reads 1 at application and during the lost turn.
A stunned champion's `restrictions()` hold move, attack, cast, shop, consumable and ward (move E21, ward
E63); `set_priority` and shared memory still work and the bot is still called. Two stuns landing after
the target's turn cost one turn; one before and one after its turn cost two consecutive turns. A stunned
camp skips exactly its next camp-phase attack. Marcella is immune.
Evidence: abil-stun B logs r20-r73 and replay; abil-heart r30-r32; mon-cast (Ryth's area stun on an elk
at r15: no elk attack at r16, attacks from r17) (`abil-evidence.txt`, `monsters-events.txt`).
Rests on: M2, M1.
Status: verified (engine 1.0.2).
Recheck: stun payload durations.
Consequence: Count actual effect applications, cooldown-ready dates, retained enhancement and owner
boundaries rather than assign permanent stun to a Basic. Without stored effects or cooldown refresh,
an eight-end cooldown limits that slot's casts; it does not replace a timed control-state model.
Pinned rules expire champion zero buffs at that champion's start and camp zero buffs before its camp
action; both tick at their owner's end (M2/M30). A next scheduled input can be unusable, adding camp
batches before the next usable action. M35's current native chain witnesses renewal; it does not
newly test camp zero-stun expiry or every invalid/repeated-stun case.

### M27. A stun cancels recall, tribute and teleport-crystal channels; the bag is kept
Claim: A stun on a channelling champion ends the channel: a recall (started r36, stunned r38: no
arrival), a 2-tick tribute (started r45, stunned r46: no banking, gems kept), a 1-tick tribute stunned
after its starting turn (nothing banked at the next turn start), a crystal channel (started r53, stunned
r54: no arrival).
Evidence: abil-stun, abil-misc, abil-candi (`abil-evidence.txt`).
Rests on: M26, M17, M19, M21.
Status: verified (engine 1.0.2). Whether a cancelled crystal is consumed was not logged.
Recheck: as M26.
Consequence: Any enemy stunner within cast range (catalog 4) that acts before the channel resolves
denies a bank or an escape outright; even a 1-tick tribute is exposed to every enemy acting after the
starter in round t or before it in round t+1. Clear or out-range stunners before channelling. Structure:
stuns are the universal channel breaker; damage only breaks recall.

### M28. Dashes: Chebyshev `ability_dash.range` (2), over holes, not through walls, onto free ground
Claim: A dash (`use(tile)`) reaches Chebyshev range 2 including (2,2) and crosses holes; a path through a
wall throws E32 ("Destination is unreachable"), a hole or occupied landing throws E32 ("Forbidden
landing tile" / "Known occupied destination"), range 3 throws E31. Paths may not cross units, altars or
enemy spawn tiles (`docs/rules-notes.md`).
Evidence: abil-heart r30-r38 (Ren, Vesper, Heart) (`abil-evidence.txt`); rules-notes probes.
Rests on: M25.
Status: verified (engine 1.0.2).
Recheck: catalog dash payloads.
Consequence: Holes are dash-only shortcuts; threat and escape reach is step plus dash range when the
dash is ready (readable on a visible enemy, M45). Structure: a ready dash extends contact range
by two tiles once per 8 rounds, so cooldown tracking is reach tracking.

### M29. Heart of Ruin: missing health scales all its damage; the dash hits and stuns everything around the landing
Claim: Heart's damage is multiplied by 1 + `heart_of_ruin_missing_health_damage_max_bonus` (0.5) x missing
health fraction, for basic attacks and the dash alike (at 580/1000 a 30-Strength hit dealt 36.3 =
30 x 1.21; at 495/1000 the dash dealt 187.875 = 150 x 1.2525). The dash deals 1.5 x Focus (catalog
modifier) to every enemy and camp within Chebyshev 1 of the landing and stuns them for one turn; allies
in the radius are spared. `ability.damage_amount()` and `damage_amount(target)` return the unscaled
value (150) despite the SDK comment. Shield: M30.
Evidence: abil-heart replay r29-r38 (`abil-evidence.txt`).
Rests on: M25, M26.
Status: verified (engine 1.0.2).
Recheck: Heart's catalog payloads and passive magnitude.
Consequence: Heart's burst is 25% higher at half health and its dash is an area stun; fight models must
apply the passive themselves. Structure: Heart's threat grows as it is damaged, so focus-firing it is
best done in one burst rather than by attrition.

<a id="m30-shields-shield_amount-absorbs-all-damage-first-for-five-owner-rounds"></a>
### M30. Shields: expiry follows the shield owner's start; timer0 can still absorb before it
Claim: Varg's shield is 2.5 x Focus (250), Heart's 1.5 x Focus (150), equal to `shield_amount()`. Cast in
round t it reads duration 5 to 1 and is gone at the caster's turn start in round t+5. It absorbs champion
and monster hits before health; a breaking hit's overflow reaches health in the same hit (55 against 15
left: 40 to health). Durability applies first (the shield fell 49.5 per 55-damage hit). Unspent shield is
lost at expiry; a recast while active is impossible (cooldown 8 > duration 5).
Evidence: abil-heart r32-r37, abil-varg r40-r45 (`abil-evidence.txt`).
Rests on: M25, M40.
Status: original magnitudes/absorption verified (engine 1.0.2); the following timer0 camp-absorption
boundary is directly observed on engine 1.0.3. It does not revalidate every historical shield case.
Recheck: catalog shield payloads.

Current native boundary: retained Mosaic R8 Yellow Brick Road A/frozen passive, exact subject
`3a4b8b68fd16`, RAW module `df05dcb52aa0b48cc660bf5b28b689d2fa22c37506475ad91178a7fde5d0ef42`.
The source pin is `results/contenders/mosaic/melee-exit-r8/death-witness.json`; its retained native
window `.cache/contenders-mosaic/r8/death-review/varg-770-798-native.json` has SHA256
`824a44c00bedb4a2da169e40a57d72f1931557275dcd2798cd34e80f791f68a4`. The following three actual
R793/input10392 transitions for Varg/body0 are retained here as a compact semantic witness:

| Native boundary | Shield before -> after | HP |
| --- | --- | --- |
| Camp64 attack, substep75; state refs267->272 | timer0/value124 -> timer0/value82 | 354 -> 354 |
| Camp66 attack, substep78; refs272->273 | timer0/value82 -> timer0/value40 | 354 -> 354 |
| Varg owner-start tick, substep174; refs273->274 | timer0/value40 -> no shield buff/value0 | 354 -> 354 |

Exact event ids are `3bdeaf504d25f229:793:10392:75:9:5:2:64:9:0:793`,
`3bdeaf504d25f229:793:10392:78:9:5:2:66:9:0:793` and
`3bdeaf504d25f229:793:10392:174:14:3:1:0:14:0:793`. These establish current native timer0 absorption
from two camp attacks followed by owner-start removal, not an inference from the cast's age. No new
game or native query was needed. The source archive SHA256 is
`3bdeaf504d25f2297bc2fa758b824d43dcd8ce6720c2dfc6f8cdfc295def912d`.

The pinned `GameRules.md` exact-turn-order contract separates the two owners' phases. Champion
zero-timer buffs expire at that champion's start, after the round-start camp phase and before channel
completion. A shield that ticks1->0 at its preceding owner end can therefore absorb that next camp
phase before expiry. A fresh post-owner-start self frame with shield duration d>=1 can credit its
next d camp phases under that contract, subject to actual shield depletion and other effects; zero
sampled after owner end is a different frame. This is phase provenance, not a universal TTL convention
or a number of enemy inputs. The same rule predicts protection during intervening champion inputs
before the shield owner's start, but the concrete current-native witness above tests camp attacks.

Camps expire their own zero-timer buffs at camp start before their action. Thus a camp already at
stun timer0 at that boundary gets no stun-based action-denial credit. This is the explicit pinned
rule contract; the searched current retained camp records omit buff timers, so a direct current-native
camp-zero-stun boundary remains unwitnessed here. Historical 1.0.2 probes are not promoted to it.

Consequence: Price shield depletion and expiry in actual phase order. A d-tick shield can protect the
last camp phase before an L=d tribute completes, then expire before that owner's channel completion
and subsequent action; carry shield0 into the post-bank continuation. Do not refill its value per
phase, expire every shield at global round start, or reuse one owner's timer as another's. A failed
or interrupted continuation needs its actual remaining shield and new owner/phase provenance.

### M31. Willow's heal: min(3 x Focus, missing health) on another ally; wasted at full health
Claim: The heal restores min(catalog modifier 3 x Focus, the target's missing health) (360 uncapped, 40
capped), which `heal_amount(target)` returns; Willow cannot target herself (E30); a heal on a full-health
ally returns true, heals nothing and spends the cooldown.
Evidence: abil-basic r21/r29, abil-misc r22/r30 (`abil-evidence.txt`).
Rests on: M25.
Status: verified (engine 1.0.2).
Recheck: Willow's catalog payload.
Consequence: Price the health actually restored, the cast spent and the target's timely continuation.
A partial heal can be useful; missing less than the nominal amount is not a universal rejection.
Willow cannot self-target this heal. Its queried payload supplies health restoration, not a stun
cleanse or an allowance refresh; a partner's presence is not a guaranteed timely heal.

### M32. Vendetta: every kill she is credited with resets both cooldowns
Claim: Killing one monster of a group or a champion resets both her ability cooldowns to 0 at once
(one wolf of three: (7,6) -> (0,0); Ren: (5,3) -> (0,0)).
Evidence: abil-misc r31, abil-varg r57 (`abil-evidence.txt`).
Current contract: pinned `GameRules.md:613` explicitly includes camp kills, and the SDK's
`reset_cooldowns_on_kill` documents the same passive. This resets cooldowns, not the input's one
successful-cast allowance (`GameRules.md:260`, M25). SDK `ability::is_ready()` requires both zero
cooldown and actual `can_cast()`.
Rests on: M22, M24-M25.
Status: historical native cooldown transitions verified (engine 1.0.2); current pinned rules/SDK
contract supports the stated distinction. No new native witness is claimed.
Recheck: Vendetta's passive.
Consequence: Kill credit can make reset/dash ready for a later input, or unlock this input's unused
cast. With C still unused, ordinary Move -> lethal Basic -> freshly ready Dash can be legal even
when Dash was cooling at the root (C23). A legally used Reset -> lethal Basic refreshes cooldowns
but cannot authorize another cast in that input. Structure: kill credit changes future ability
availability; it does not replenish C, M or A by itself.

### M33. Varg's splash hits every other enemy and camp within Chebyshev 1 of the target, waking their groups
Claim: A Varg basic attack also deals Strength (catalog magnitude 1 x Strength) to every other enemy
champion and every camp within Chebyshev 1 of the target, never allies; a splashed camp of another group
wakes and attacks next round.
Evidence: abil-varg r47, r5-r14 (`abil-evidence.txt`).
Rests on: M9.
Status: verified (engine 1.0.2).
Recheck: Varg's passive.
Consequence: Varg is strongest in dense camp blocks and melee scrums, and attacking beside unrelated
camps wakes them: include splash wake-ups in camp-threat accounting. Structure: splash multiplies damage
by local density.

### M34. Ren retaliates 0.2 x Focus against the source of every damage instance, monsters included
Claim: Each damage instance Ren takes (basic hits, area spells, monster hits, the killing blow) deals
`ren_retaliation_focus_ratio` (0.2) x Ren's Focus to its source as ordinary mitigable damage (20; 18
against Durability); camps lose health from hitting Ren.
Evidence: abil-varg r49-r57, abil-misc r21+ (`abil-evidence.txt`); main D90 (deaths to retaliation).
Rests on: M8.
Status: verified (engine 1.0.2). Retaliation cannot wake camps or start fights by itself: it only
answers an awake camp or an acting champion.
Recheck: Ren's passive.
Consequence: Every hit on Ren costs its attacker 0.2 x Ren's Focus, so a low-health attacker can die in
its own turn; monsters attacking Ren chip themselves. Structure: Ren taxes many small hits more than a
few big ones.

### M35. Marcella: stun-immune; her enhanced reset turns the next hit into Strength plus a one-turn stun
Claim: Marcella ignores stuns (Heart's dash hit her twice without a stun). Her slot-0 reset (catalog
`enhanced_attack` 1) makes her next basic attack deal plain Strength (45) and stun for one turn.
Evidence: abil-misc r40-r41, abil-heart (`abil-evidence.txt`).
Rests on: M26, M24.
Status: original cases verified (engine 1.0.2); current pinned kit contracts and the separate narrow
ordinary champion chain below retain their own scopes.
Recheck: Marcella's passive and payloads.
Consequence: Her own immunity removes stun interruption, not damage, death or enemy altar locks.
It supplies no immunity aura, automatic ally protection or universal carrier/roster preference.

Current control-capacity map, pinned CLI/SDK 3.0.5 and rules/catalog engine 1.0.3:

| Kit/effect | Queried contract | Actual prerequisites and limits |
| --- | --- | --- |
| Ryth slot 0 area control | Ground aiming in rules; range 4/radius 1, 1xFocus damage + stun 1; cooldown 8, excludes walking. | Requires C, cooldown 0 and legal center; an ordinary walk excludes this cast. It is AoE kind 3, so SDK `is_cc()` is false despite positive stun duration. |
| Marcella slot 0 enhancement | Self reset, walking compatible, cooldown 8; creates enhanced next Basic, which deals Strength + stun 1. | M24's spent-A reset guard and fresh C/cooldown apply. Later enhanced Basic needs actual A, the buff and a legal visible target; it consumes the buff and need not retain C. Ordinary Basics do not inherit permanent stun. Her passive immunity is personal. |
| Varg | Melee Basic range 1 with radius 1 splash; self shield 2.5xFocus for 5 owner ticks; reset/shield compatible with walking, cooldown 8. | Shield and reset compete for one C. Shield absorbs damage (M30), with no stun-immunity effect in this payload; splash wakes every affected physical group (M33). |
| Willow | Ranged Basic range 3; heal another ally within 4 for up to 3xFocus/missing health; compatible self reset, cooldown 8. | Ordinary Move spends M+A; reset restores A only. Heal and reset compete for C; the heal supplies no self-heal or control cleanse (M31). |
| Vendetta | Melee Basic range 1; compatible self reset and dash 2, cooldown 8. Credited kills refresh cooldowns. | Legal dash landing/path and unused C still required. Kill refresh restores neither C nor ordinary M/A; reset before a kill cannot grant a second cast (M32). |

Sources: pinned `GameRules.md:122,260,489-499,613-623,645-655,707-719,769-783`, SDK typed ability
metadata/buffs and `ability::is_ready()`, and `runtime-catalog.json` (version/hashes in
`docs/reference/current-version.json`; passed editor-engine metadata,
not blanket native CLI parity). Runtime equipped values override copied base magnitudes. Ryth's raw
target bits 19 include camps/enemies/ground; SDK copies them and generically forwards entity or ground
overloads. Those flags alone do not establish engine acceptance of a unit-aimed AoE overload. The
handbook specifies Ground and the native case below uses a ground center; other overloads remain open.

Current ordinary witness: `results/contenders/warband/word-service/service-program-pressure-native-witness.json`,
SHA256 `54eb18fdd0be26ff2a9559e7986889119b42f80628561fb111c37410bb04a61f`, exact
profile `58a9258f6d70`, Packs B/opaque v53. After an actual 9-gem payout at R48, Willow (actor 4) at R50 has
HP600, full M/A/C, cooldowns[6,0], no
effects and all three foes personally visible. Her ordinary Move (4,5)->(5,4) spends ranged M+A.
Ryth's Basic615 and ground cast616 deal 45 then 100 and apply stun 1. At R51 Marcella actually performs
Basic633 -> Reset634 -> Basic635; before635 A is available, C is spent and enhanced timer 1 exists.
That Basic consumes enhancement and renews Willow's prior stun timer 0 to 1. Willow's scheduled R51/R52
inputs are unusable; Heart kills her at R53 before the next usable input. All 600HP loss reconciles
observed champion actions. This is not a hidden-foe, fuel or missing-bank finding; ally Vendor5 is not
personally visible and supplies no credited free support. NPC nearest-target rules (M5-M6) do not
redirect a player's chosen champion attacks. Recorded target identity is coordinate-inferred where
the witness has no target id. No private decision cause or unchosen safe escape is established.

Open here: unused enhanced-buff lifetime, a current native immunity interaction, camp zero-stun
expiry-to-action, current invalid-reset cases and Ryth entity-overload acceptance. The queried kit and
phase contract are usable premises; this sequence is not a universal control cadence or roster claim.

### M36. Cassi and Issac die together: one kill reward, both bags to a champion killer, both lost to a camp
Claim: When either dies the other dies in the same step; a champion killer gains `kill_gold` once (100),
both bags (7 + 7 gems) and one kill; a camp kill destroys both bags.
Evidence: abil-candi r53, abil-misc r21; info-order (Issac died across the map when a wolf killed Cassi)
(`abil-evidence.txt`, `info-evidence.txt`).
Rests on: M22.
Status: verified (engine 1.0.2).
Recheck: the shared passive.
Consequence: The weaker body is a 2-for-1 target, so protecting Cassi protects both; splitting a bag
between them reduces no risk, since both bags fall together. Structure: one pick buys two bodies of
turns and vision, priced by a single shared life.

## Economy

### M37. Gold is fractional; gold bonuses add and scale every gold gain after the split
Claim: Each gold gain is multiplied by 1 + the sum of the champion's gold bonuses (Vesper Vale passive,
catalog magnitude 0.33; Treasure emblem `item_type::info().passive_magnitude` 0.2; together x1.53) and
accumulates as a fraction; `gold_count()`, purchases and the `earnedGold` metric use the floor of the
running value. Bonuses apply to capture gold, kill gold and Marcella's tribute gold, never to gems. A
capture splits first (floor(total / n), remainder to the lowest id, M14) and each share is then
multiplied by its owner's bonus. Champions start with 0 gold and `constants::passive_gold` is 0.
Evidence: econ-pass, econ-cap, econ-kill (`data/mechanics/econ-events.txt`): Vesper capturing six
20-gold clusters read 26, 53, 79, 106, 133, 159 = floor(26.6 k); 3-way split of 20 gold + 14 gems: ryth
(id 0) 8 g 6 gems, Vesper 7 (6 x 1.33), stellar 6 g 4 gems; kill gold 100 / 133 (Vesper) / 120
(Marcella + Treasure) / 153 (Vesper + Treasure); Marcella + Treasure tribute gold 14 x 2 x 1.2 = 33.6.
Treasure alone on capture gold is inferred from the x1.53 capture and the x1.2 kill.
Rests on: M14, M22.
Status: verified (engine 1.0.2), except the inferred Treasure-alone capture case.
Recheck: catalog passives and Treasure magnitude.
Consequence: Who stands on the capture tile and who lands the last hit decide whose multiplier applies:
a 1.0 ally sharing a capture with Vesper lowers team gold, and a passer-by on a member tile takes a
share. Rounding never loses gold over a game. Structure: with unequal multipliers, assigning capturers
and last hits is an optimisation problem, not a team total.

### M38. Marcella earns 2 gold per tributed gem, scaled by her gold bonuses
Claim: When Marcella's tribute completes she gains catalog magnitude 2 gold per gem banked (14 gems ->
28; with Treasure 33.6); it counts as earned gold.
Evidence: econ-pass (`econ-events.txt`).
Rests on: M19, M37.
Status: verified (engine 1.0.2).
Recheck: catalog passive marcella.
Consequence: If Marcella banks the team's bags the win objective also pays shop power (1,000 gems is
about 2,000 gold); the bags must travel to her or she to them. Structure: one champion converts score
into strength, coupling the race and the economy.

### M39. Shards add the catalog growth; five combine into an emblem; Power counts six, Bruiser 5+1+1
Claim: A shard (`item_type::info().cost` 30) adds the champion's `base_stats().*_growth` to its stat
(checked: Ryth 100/5/10, Tavi 75/5/10, Prism 60/2.5/8, Stellar health 65). Five matching shards combine
automatically (`item_count` then shows the emblem; emblems are not sold, E50). Power emblems count six
shards (Tavi Power of Strength +30); Bruiser five plus one of each other stat (+25 Str, +75 HP, +10 Focus
on Tavi). A second emblem of a family adds its stats but not the passive (a second Jungle: +500 HP, no
further reduction). Health shards bought on spawn heal exactly the health gained (610/650 -> 935/975);
Tavi's field purchases raise max health without healing. Consumables (`consumable_type::cost()` 10,
`carry_limit()` 1) need spawn or death (E55); a second of a kind throws E58.
Evidence: econ-shop2, econ-sell, econ-pass (`econ-events.txt`).
Rests on: catalog items.
Status: verified (engine 1.0.2).
Recheck: catalog items and champion growth.
Consequence: Value a shard as growth / 30 gold; Power is one free shard per five, Bruiser a spread; a
duplicate family is worth stats only. Structure: item power is linear in gold within a family, with a
small bonus at each fifth shard.

### M40. Jungle x0.7 against camps, Durability x0.9 against everything; they multiply
Claim: Jungle (`passive_magnitude` 0.3) scales camp damage taken by 0.7; Durability (0.1) scales all
incoming damage, basic attacks and abilities from champions too, by 0.9; together a wolf hit of 20 is
12.6 (x0.63, multiplicative, not x0.6). Jungle does not reduce champion damage. Health is fractional
(2187.4). Monster emblem (0.1) multiplies damage against camps by 1.1, basic attacks and abilities alike
(Stellar 30 -> 33 and 140 -> 154 on a wolf), and nothing against champions.
Evidence: econ-pass r151-r297, econ-emblem r65-r92, econ-sell r75-r129 (`econ-events.txt`).
Rests on: M8.
Status: verified (engine 1.0.2).
Recheck: emblem magnitudes.
Consequence: Survival against camps is a product of factors; Durability is the only item mitigation in
PvP; Monster is a pure farming item with no fight value. Count hits by float subtraction
(`docs/rules-notes.md`). Structure: mitigation stacks multiplicatively, so each extra layer is worth less
in absolute damage than the first.

### M41. Home emblem: recall in three owner turn ends
Claim: With a Home emblem (`passive_magnitude` 0.5) recall takes ceil(`constants::recall_time` x 0.5)
= 3 owner turn ends (started r114, arrived at r117's turn start).
Evidence: econ-pass (`econ-events.txt`).
Rests on: M17.
Status: verified (engine 1.0.2).
Recheck: Home magnitude, recall_time.
Consequence: Each recall saves two rounds and its interruptible window shrinks from five rounds to
three, which matters most where recall is the travel mode. Structure: Home buys both tempo and recall
safety.

### M42. Selling: Tavi only, on spawn, full value, several per turn, not earned gold
Claim: Only Tavi sells (others E128), only on spawn (E133), for the full purchase value
(`item_type::cost()`), and several items in one turn (two shards and a Power emblem; error 131 never
fired); stats revert; refunds are tracked as `saleRefunds`, outside `earnedGold`. Purchases made while
dead persist after respawn.
Evidence: econ-sell, econ-shop2 (`econ-events.txt`).
Rests on: M39.
Status: verified (engine 1.0.2).
Recheck: Tavi's passive.
Consequence: Tavi's build is a free respec at every spawn visit; shopping and selling never move the
gold tiebreak. Structure: for Tavi item choices are reversible, so the build can follow the game state.

### M43. Ryth gains Strength from bonus health; Prism gains Focus per carried gem
Claim: Ryth gains catalog magnitude 0.01 x bonus health as Strength (+1 per 100-health shard, +6 from a
Power emblem of health). Prism gains `constants::passive_abilities::prism_focus_per_gem` (1.5) Focus per
carried gem (7 gems: 88 -> 98.5) and loses it when the gems are banked (loss on death follows from
losing the bag; not observed separately).
Evidence: econ-shop2, econ-pass (`econ-events.txt`).
Rests on: M39.
Status: verified (engine 1.0.2).
Recheck: catalog passives.
Consequence: Health is also damage for Ryth; Prism fights strongest just before banking, which pulls
against banking early. Structure: two champions convert a resource (health, the bag) into damage.

## Information and vision

### M44. Vision is personal: a Chebyshev-square shadowcast from the champion plus its own ward
Claim: A champion sees a shadowcast square of Chebyshev radius `base_stats().vision_radius` (8; 17x17 =
289 tiles on open ground: (8,8) in, (9,0) out) united with its own ward's square of
`constants::ward_vision_radius` (6; 13x13). Walls and bushes occlude (M46). Allies' vision and wards
are not shared: an ally outside one's own vision returns E97 for position, health, cooldowns, gems and
items; only `is_alive`, `champion_name` and `team` work for hidden champions of either team.
Evidence: info-wards and info-order (owner sees through its ward, allies do not; parity 300 = 207 own +
ward tiles), `info-evidence.txt`.
Rests on: none.
Status: verified (engine 1.0.2). The rules' "circle" wording is loose: the shape is a square.
Recheck: vision radius settings.
Consequence: A bot does not know where its allies are unless it sees them or reads them from the eight
shared bytes (M50); every rendezvous, help and focus-fire decision goes through that channel.
Structure: the team plays on per-body partial information; vision is the scarcest shared resource.

### M45. A visible enemy is readable almost completely, except its gold
Claim: For a visible enemy champion the SDK returns position, health, max health, strength, focus,
attack range; raw state flags (recalling, tributing, teleporting, can_move / can_attack / can_cast and
`completed_turn`); `remaining_channel_time()`;
both ability cooldowns; buffs with durations and the shield's remaining value; `restrictions()` (a stun
blocks move, attack, cast, shop, consumable and ward); `gem_count()` (its bag, the kill prize); exact
`item_count()` and consumable counts; current and next priority; its `visible_tiles()`. Gold is not
readable (`gold_count()` is self-only; the raw import returns E105 for enemies). A hidden enemy gives
only `is_alive`, `champion_name` and `team`.
Evidence: info-observe r6, r48-r84 (`info-evidence.txt`). The same retained file's A/R25 observations
of Issac/Vesper report raw2105, which includes `completed_turn` alongside M/A/C. Their observer Ryth
acts later, so this is consistent with those allies already having acted; it is not a counterexample
to current-round completion semantics. A retained current-native Packs A event independently resolves
the reset boundary: `16abb1c256e43e27:33:405:7:0:0:0:0:1:0:33`, round-start, resets `completedTurn`
from true to false for all six bodies. Ordinary bodies refresh M/A/C; the channelling Ryth remains
M/A/C=`000` despite `completedTurn=false`. The saved read is
`.cache/contenders/warband/contact-audit/native-28-55.json` (SHA256
`1e2a9201c0f38915f9ea15e3810e725e7b043152d1a1e9ae8eed4da7dba9c7ec`); the durable provenance is
`results/contenders/warband/capital-work/contact-audit/assessment.json`, candidate `2fe1083b0bf6`,
raw role, engine 1.0.3. These six flag transitions are retained here as the bounded semantic witness;
they are not a new game. Current `sdk/cpp/cambc.h` documents `prev_priority()` as current round
priority and `priority()` as next round priority.
Rests on: M44.
Status: original query coverage verified (engine 1.0.2); round-start completion reset observed on all
six bodies in one pinned engine 1.0.3 event, not whole-roster/special-state coverage.
Recheck: SDK or ABI changes.
Consequence: Inside vision, use the actual queried health, shields, cooldowns and bags, but attach
allowance/completion flags to an established owner-input boundary before using them to remove an
enemy's future opportunity. Under the observed round-start reset contract, a fresh `!completed_turn`
identifies a remaining scheduled current-round turn; it does not promise a usable attack, movement or
cast. Next-round priority is not the current ordering, and its value may change before that next round.
Where the actual order boundary is unestablished, retain the possible intervening enemy input
instead of silently deleting its burst.
Outside current vision, preserve only explicit historical observations/beliefs rather than claiming
fresh hidden health or allowances. Gold must be inferred from items.

### M46. Bushes hide from outside only; a blocked step reveals the hidden occupant for one round
Claim: A champion outside a bush sees neither the bush tiles nor their occupants, even from an adjacent
tile; inside, it sees its whole connected bush cluster and out, but not into another cluster. An unseen
enemy cannot be targeted: attack E42, cast E30. Stepping onto a hidden occupant's tile returns false,
keeps the movement allowance and reveals that tile for the rest of the round
(`constants::tile_reveal_rounds` 1). Monsters ignore bushes (M12).
Evidence: info-wards r62-r67 (`info-evidence.txt`).
Rests on: M44.
Status: verified (engine 1.0.2). Unresolved: whether a revealed occupant can be attacked in the same turn
(`has_champion` turns true; untested).
Recheck: bush rules in a patch.
Consequence: Bushes make ambushes and escapes real: an enemy in a bush is untargetable until revealed,
and probing a bush by stepping costs nothing. Structure: bushes are the only terrain that denies
vision to champions, so their control is control of information.

### M47. Wards: Chebyshev 2 placement almost anywhere, 10-turn recharge, permanent, visible to enemies
Claim: `place_ward(p)` within Chebyshev `constants::ward_placement_radius` (2; at 3 E65) works on ground,
the own tile, ally- or enemy-occupied tiles, the own spawn and unseen bush tiles; walls, holes, altars
and the enemy spawn are refused (E64), as are a tile holding one's own ward (E117) and an attempt
without a charge (E62); failures keep the charge. `ward_cooldown()` reads `constants::ward_cooldown`
(10) after placing and drops by one per own turn end. A new ward replaces the old one; wards never
expire (`ward_lifetime` -1) and cannot be destroyed; enemies see a ward in their vision, owner included
(`vision::wards()`).
Evidence: info-wards r15-r47 (`info-evidence.txt`).
Rests on: M44.
Status: verified (engine 1.0.2).
Recheck: ward settings.
Consequence: A ward is a movable 13x13 vision patch every ten turns, placeable into an unseen bush or
under an enemy; a seen ward tells the enemy who placed it. Structure: persistent personal vision is
cheap and indestructible, so the information game is won by placement, not by protection.

### M48. Observations are live within a round
Claim: Fresh occupancy, position and vision queries reflect the current engine state: an enemy's move
earlier in the round already shows in one's turn, and one's own step updates one's vision at once
(211 -> 214 tiles mid-turn). This does not refresh a previously returned value or a bot's memory.
A turn suspended by fuel exhaustion resumes next round with the old turn's variables (mon-target r45:
a mask built in r44 met an enemy that had moved); it is one source of stale observations, not the only
one. The current SDK explicitly returns owning buff and vision collections; each object retains its
captured contents, and another query obtains a fresh object.
Evidence: info lane test r24-r30; fuel-lab2 exhaustion (`fuel-budgets.json`); current
`sdk/cpp/cambc.h` owning collection/query contracts.
Rests on: M2.
Status: original live-query observations verified (engine 1.0.2); owning-value correction from the
pinned SDK contract (engine 1.0.3 bundle), not a new native probe.
Recheck: SDK caching changes.
Consequence: Query again after a state-changing primitive when a later decision needs the changed
state. Never carry live premises across a possible suspension without checking their epoch. A later
actor sees a later physical state through its own personal visibility; it need not know more than an
earlier actor, and a move can lose sight of a still adjacent enemy (M54).

### M49. Camp and group state are readable only in vision; respawn timers are exact when seen
Claim: `camp.state()` works while the camp's tile (its last tile if dead) is in vision and reports dead
camps as dead; otherwise E97. Dead camps are absent from `camp_tiles` and the awake/asleep masks and
their health throws. `camp_group.state()` needs any member's spawn tile in vision (E97 otherwise) and
reports alive, `reward_available` or `respawning{timer}`; the timer counts down by one per round from
`max_respawn_timer()` at the payout. Capture progress and the capturing team are not exposed.
Evidence: info-observe r21-r45 (`info-evidence.txt`); cap-basic group logs.
Rests on: M44, M16.
Status: verified (engine 1.0.2).
Recheck: SDK camp queries.
Consequence: A countdown nominates the earliest possible renewal observation; it does not change a
remembered `respawning` group to `alive` (M52). Occupancy can postpone renewal after zero. Preserve
the last observed state and observation time separately from its predicted deadline. Capture intent
and elapsed hold time also do not establish an attributed payout. Whether an enemy is mid-capture
must be inferred from observed presence and history.

## Memory

### M50. Shared memory: 8 bytes per team, zero at start, visible to later actors at once
Claim: `constants::shared_memory_bytes` (8) per team, zero at the start. A write is visible to allies
acting later in the same round and to every ally from the next round on; it persists until overwritten;
the teams' memories are separate; dead and channelling champions can write. Out-of-range access throws
std::out_of_range in the SDK.
Evidence: info-order r1-r7 (`info-evidence.txt`); channelling and dead writes in ch-basic and ch-lock.
Rests on: M2.
Status: verified (engine 1.0.2).
Recheck: `constants::shared_memory_bytes`.
Consequence: Eight bytes are the only channel for vision and intent between bodies (vision is not
shared, M44): give each body its own byte or bits. An ally acting earlier in a round never sees a
later ally's write from that round. Structure: team coordination has one round of latency for half the
pairs and a hard 64-bit bandwidth.

### M51. Draft memory: 1024 bytes written from offset 0, frozen in play
Claim: `constants::draft_memory_bytes` (1024). Every draft write starts at offset 0 and overwrites only
its own length (a 4-byte write after a 16-byte write kept bytes 4-15); the draft instance's own snapshot
stays at its startup value; champions read the final bytes through `draft_memory`. Assigning in play
throws E18 (invalid_action).
Evidence: info-order (`info-evidence.txt`).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: draft memory size.
Consequence: The draft bot (10x fuel, whole map visible) can hand every body one precomputed plan; in play
only the eight shared bytes change. Structure: precomputation moves into the draft for free.

## Current independent continuation observations

### M52. A zero respawn timer does not establish a living group
Claim: `respawning{0}` is a real current-native observation. A predicted deadline alone cannot admit
a clear operation, and staying on a captured source can prevent its renewal.
Evidence: `results/contenders/warband/capital-work/capital-ybr-r6-raw/renewal-audit/assessment.json`,
candidate `contenders/warband@214a62ea507b`, Yellow Brick Road A/frozen passive. A fresh Varg SDK
query at R520 observes top gold group 1 in `respawning` state with raw timer 0; four actual actors
occupy captured gold-row source tiles. The bounded R520-560 history has only ready end-turns.
The evidence does not reveal every actor's private selected group or route cursor.
Rests on: M16, M49; current SDK observation in the retained witness.
Status: observed (engine 1.0.3, one saved game and bounded native-query window).
Recheck: group respawn, occupancy or SDK state semantics change.
Consequence: Record captured-source departure as a physical operation. After the earliest deadline,
reobserve living members or actual reward availability before choosing the corresponding work.
Payout, source release, renewed source observation and next useful work are separate events.
An arrival-time lower bound may nominate future work, but it does not resolve the physical enablement
of an occupied source. [G13](strategy.md#g13-future-availability-is-a-nomination-until-its-physical-enablement-is-resolved)
separates that nomination from executable living work and preserves alternatives/refutations until
an actual relevant premise changes.

### M53. Ranged walk then reset is a real allowance transition
Claim: Vesper's ordinary ranged move spends both M and A while keeping C; its ordinary reset then
restores only A. A preceding basic attack is unnecessary when movement already spent the allowance.
Evidence: `results/contenders/mechanics/vesper-move-reset-current/witness.json`, exact subject
`contenders/_cooperation-vesper-move-reset@a752d57c134d`, engine 1.0.3, Elk Ring A/frozen passive,
R1 in a three-round truncated guard witness. Queried flags M/A/C are `111` initially, unchanged after
premature reset refuses E26, `001` after successful ordinary move (1,1)->(2,1), then `010` with reset
cooldown 8 after accepted reset. The probe has zero basic commands. This proves the guard and flags,
not successful move-reset-basic damage, a different kit's guard, full-game health or strength.
Rests on: M23-M25; exact source/module/SDK identities in the witness.
Status: observed (engine 1.0.3, one map/side, one actual owner input).
Recheck: reset guard or ranged allowance semantics change.
Consequence: Compile action words from actual spent allowances, not a count of preceding attacks.
Preserve successful ordinary-walk provenance for the whole owner input: a reset does not restore M
or permit a movement-exclusive cast after that walk, and it consumes C.

### M54. A failed recall is not made safe by empty personal vision
Claim: Losing personal sight of a recent enemy does not establish that recall can complete. A failed
channel carries information about local danger even when the next visible-enemy list is empty.
Evidence: `results/contenders/warband/capital-work/contact-audit/death/assessment.json`, exact
`contenders/warband@2fe1083b0bf6`, raw role, Packs A/historical opaque main-v44. Complete bounded
native history R362-379: Willow moves (15,20)->(14,19) at R376 and loses personal sight while Ryth
remains adjacent at (14,20). Recall at R377 with HP135 is cancelled by 115 basic damage; R378's
repeated recall at HP20 ends in death. No NPC damage, healing or economic conversion occurs in
this window. Alternative action masks were not queried, so this does not establish a safe route.
Rests on: M17, M44, M48; actual observed actor-order and damage boundaries.
Status: observed (engine 1.0.3, one saved first-death window); the persistence requirement is a
consequence of that counterexample, not a proof of any replacement controller.
Recheck: channel cancellation or visibility mechanics change.
Consequence: A retry needs a new physical, information or timely support premise. Elapsed time or
an empty personal list alone does not discharge the preceding failed-channel premise. Nearby ally
HP is not transferable capacity: the support action needs the actual ally, range, readiness and slot.
