# Mechanics: what the engine does (engine 1.0.2)

The ledger of engine behaviour that changes decisions. M1-M51 were established in native
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
| M24 | An attack reset restores only the attack and is the turn's one cast; Vesper can spend attack by moving first | historical reset facts plus current Vesper move/reset/basic witness |
| M25 | One cast per turn; "excludes walking" blocks only the step; damage is modifier x Focus in Chebyshev areas that spare allies | verified (engine 1.0.2) (corrects earlier prose) |
| M26 | A one-round stun costs the target exactly its next turn; stuns refresh, never stack | verified (engine 1.0.2) |
| M27 | A stun cancels recall, tribute and teleport-crystal channels; the bag is kept | verified (engine 1.0.2) (one detail open) |
| M28 | Dashes: Chebyshev `ability_dash.range` (2), over holes, not through walls, onto free ground | verified (engine 1.0.2) |
| M29 | Heart of Ruin: missing health scales all its damage; the dash hits and stuns everything around the landing | verified (engine 1.0.2) |
| M30 | Shields: `shield_amount()` absorbs all damage first for five owner rounds | verified (engine 1.0.2) (one detail open) |
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
| M52 | Capture intent and expired respawn deadlines do not prove availability | current public-source correction; independently retained native witnesses, no audit rerun |

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
inside an awake camp's reach risks that camp's hit before the next decision, and a payout in round r
lands before anyone acts in round r. Structure: camp damage is applied at round start before any
champion decision. The victim still depends on nearest distance and configured order at a tie (M5/M6),
and live sleep/stun state controls whether the camp attacks; phase timing alone is not a damage forecast.

### M2. Turns interleave the teams one champion at a time; priority, then draft order, inside a team
Claim: In round r the starting team's k-th champion acts, then the other team's k-th, for k = 1, 2, ...;
team A starts odd rounds (round 1 is the first play round), team B even rounds. Inside a team the order
is higher `priority()` first, ties by draft order (champion ids). `set_priority(n)` takes effect from the
next round and persists; a dead champion keeps its slot and can still set priority. A team with more
bodies (Cassi & Issac makes 4 against 3) plays its extra bodies after the last interleaved pair, in both
parities (r5: A1 B1 A2 B2 A3 B3 A4; r6: B1 A1 B2 A2 B3 A3 A4); Cassi and Issac take two consecutive
slots, Cassi first.
Evidence: mon-target r1/r2 (A0 B3 A1 B4 A2 B5 / B3 A0 B4 A1 B5 A2); mon-metric: B stellar
`set_priority(5)` in r3 acts first in team B from r4 on; unequal bodies and dead slots: info-order
(`data/mechanics/info-evidence.txt`).
Rests on: none.
Status: verified (engine 1.0.2).
Recheck: a patch to scheduling; a draft with both teams taking Cassi & Issac.
Consequence: As a team, neither side takes two turns in a row against the other's single response (except
the extra Cassi & Issac body, which always moves after every enemy). Between two particular champions it
is different (combat C2): a same-slot pair trades double turns (B1 acts at the end of round r and again at
the start of r+1 with no A1 turn between), while a pair in different slots strictly alternates with the
lower slot first in every round; so tempo is a property of each pair (strategy G9). Priority chooses which own champion
answers first in each slot (the stunner before the striker, the tank first for monster ties, M6),
not how many times a team acts. Structure: reaction latency is one opposing turn, and the first strike in
each slot pair alternates with the round's parity, so who hits first in a duel depends on the round in
which contact begins (a bot can choose that round by when it steps in).

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
Current scoped revalidation: [E31](economy.md#e31-next-round-priority-rotates-a-single-elks-tank-without-losing-either-melee-basic)
verifies actual next-round priority and every volley in four engine 1.0.3 Ryth/Marcella single-elk
games, both sides. It completes clear/capture/bank while both melee bodies keep attacking; the
historical cross-team and other-camp targeting cases above were not rerun.

### M7. Monster range is Chebyshev design range; spawn immobility has a scoped premise
Claim: A monster hits only champions within Chebyshev distance `camp_stats.base_attack_range` (wolf 3,
elk 4, golem 4, monolith 5; the clusters' 3 is moot, M11). No monster left its spawn tile in the
recorded historical cases.
Evidence: mon-metric: the elk (range 4) hit vesper at (dx,dy) = (-4,+4) (Euclid 5.66) and stopped when
she stood at Chebyshev 5; mon-target group wolves (range 3) hit ryth at Chebyshev 3 and not at 4
(r61-r63); mon-cast: Stellar's range-4 spell from Chebyshev 4 took a wolf 1350 -> 1210 -> 1070 at r15 and
r23 with no wolf attack at all, while Ryth's range-4 area cast on an elk from 4 was answered by elk hits
from r17; no camp position ever changed in any replay.
Rests on: M5.
Status: verified (engine 1.0.2) for wolf and elk; golem and monolith ranges are catalog values.
Recheck: catalog `camps[].stats.base_attack_range`.
Current-source supplement: `camp.attack_range()` returns the immutable design range, and the
pinned public catalog has no camp displacement or range-changing payload. A persistent spawn
index additionally assumes normal spawn initialization, complete public camp AI transitions
with no autonomous movement, and same-location respawn. The rules' "do not chase" alone does
not prove that AI-completeness premise; camp designs still have movement speed one. Historical
wolf/elk observations do not supply an all-kind engine 1.0.3 proof. Accept that premise explicitly
or check/revoke on an observed mismatch. Even under it, life, wake, HP, buffs and group capture
state remain dynamic; future-route threat support must cover the route, not only its first tile.
Consequence: Spells with catalog range 4 outrange wolves: a caster at Chebyshev 4 chips a wolf group
without being hit (the group wakes, finds nobody in range and sleeps again, keeping the damage,
M10); elk and golem (4) match spell range, and the monolith (5) outranges every ability. Structure:
risk-free chip damage exists exactly where an ability's range exceeds a monster's, limited only by the
8-round cooldown.

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

Current observation contract: alive, currently awake and possibly awake before the next camp
phase are different predicates. `camp.state()` distinguishes dead, awake (`alive`) and asleep;
`is_alive()` includes both awake and asleep. A missing personal observation is unknown, not
asleep. For a proposed damaging action, expand the affected footprint to every living member
of each touched group, including remote siblings; include groups an intervening foe can wake.
Charging every living camp as already awake can reject usable quiet work, while charging only
the present awake mask can miss prospective damage. Preserve the actual sleep/wake uncertainty
and the timing/visibility assumptions used by either bound. This is a source/rule distinction,
not a claim that a particular saved failure has an observed sleep state.

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
Consequence: At the stated timer, capture needs six phases of uninterrupted uncontested team presence
on member tiles. A fixed body is sufficient, not mandatory if presence is handed over without a gap
(M15); extra bodies do not shorten the countdown, although their combat value is separate. Compatible
channels may overlap holding (M17). Payout precedes champion actions that round. Capture remains an
exposure window: interrupting required team presence denies that pending income.

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
Consequence: Recall is safe only where no enemy and no awake camp can reach the champion for five
rounds; an enemy needs a single damage instance inside the window to deny it. Fleeing first and
recalling out of reach beats recalling in place. Structure: escape-by-recall is gated by distance, not
health; a chaser in reach turns every recall into a wasted turn.

Conditional capture/recall composition (current rules and two engine 1.0.3 witnesses): let the
last clear occur on own input k, after that round's capture phase, with live uncontested member
occupation maintained through payout. If capture time is C, phase k+1 arms and k+C+1 pays (M14).
An eligible uninterrupted recall of L owner-end ticks begun on input k+C+1-L reaches zero at
the preceding owner end and resolves at own start k+C+1, **after** that round's capture phase.
For C=5, normal L=5 starts at k+1; Home L=3 starts at k+3. A normal start at k arrives at k+5,
one phase too early to remain a payout recipient; if it removes the last capturer, progress resets.
This follows by counting the ordered phases, not by adding capture and recall times serially;
the Home case is derived here, not a newly observed Home overlap.

The [Yellow Brick Road witness](../../../results/contenders/warband/awake-audit/comparator-yellow/assessment.json)
has black-box Vendetta clear37, recall38, payout43 before arrival43, shop43, owner-end healing,
bank52 and next clear66. The [Ambush witness](../../../results/contenders/warband/awake-audit/comparator-ambush/assessment.json)
independently has clear268, recall269 and payout274 before arrival274, followed by shopping and
healing, later bank319 and applied renewed work342. Targets were already damaged or received
ally work; these are not fresh solo-service or general strategy proofs. Continuous uncontested
live occupation, actual channel eligibility, no damage/stun/death/cancellation/early departure,
legal spawn arrival and runtime C/L are premises. Overlap supplies no universal safety guarantee,
nor does it require banking before shopping or prescribe a bag threshold.

### M18. On a spawn tile a champion heals `spawn_healing` x max health at each turn end
Claim: At the owner's turn end on an own spawn tile health rises by `constants::spawn_healing` (1.0) x
max health, i.e. to full in one turn end; the recall arrival turn counts.
Evidence: ch-basic: ryth arrived r28 at 1060/1200 and ended that turn at 1200.
Rests on: M3.
Status: verified (engine 1.0.2).
Recheck: `constants::spawn_healing`.
Consequence: At healing fraction 1.0 a living owner-end on spawn restores full HP. Time retained
there can also support computation under an explicit survival/recovery contract; a general
leave-immediately rule does not follow from the healing mechanic.
Structure: renewable HP depends on arrival, surviving until recovery, the runtime healing amount
and the physical schedule, alongside travel, carried gems and computation.

Current conditional model supplement: the [fixed public-rule derivation](../../../results/contenders/foundation/spawn-healing/lemma.json)
and [T5's finite induction](mathematics.md#t5-computation-must-refine-the-physical-continuation)
permit positive per-batch camp exposure `0<D<H` during retained computation when initial HP
exceeds `D` and each batch is followed by full living own-spawn owner-end recovery. Full healing
is a queried runtime premise; the pinned catalog's 1.0 is only its copied default here. No foe,
displacement, max-HP change, unpriced batch/loss or invalidating action may intervene. Recovery
cannot revive a body killed first. Physical owner-end recovery is distinct from the suspended
logical VM invocation returning. This is public-contract/model composition, not a new native
suspension/healing experiment. Cumulative incoming damage and healing cannot establish the
location, per-prefix survival or actual healing cause of the recorded 8ff Vesper; no fuel,
controller-progress or strength qualification follows.

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
Status: verified (engine 1.0.2).
Recheck: the submit settings in `constants`.
Consequence: Tribute in chunks of up to 10 gems for one-tick channels; a capture paying out during the
channel adds to the bank for free. Defending a tribute means preventing stuns, kills and enemy locks, not
damage. Structure: banking time is ceil(gems / 10) exposed rounds, so the risk of carrying grows with
the bag while the cost of banking grows only stepwise.

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
Consequence: Without a further allowance-restoring action, a ranged champion cannot ordinary-step
and basic in one turn; positioning before contact matters. M24's current Vesper move/reset/basic
witness is an explicit exception to the old blanket positional advice. A melee ordinary step does
not spend its basic, so its baseline damage is mobile.

### M24. An attack reset restores only the attack and is the turn's one cast
Claim: After attack, reset, the flags read "A": the attack is back, a ranged champion's movement is not.
A reset while the attack allowance is still available throws E26 ("An available attack cannot be reset"); a second cast in a
turn throws E27. Cooldowns are 8 owner turn ends (catalog `cooldown` 8 for every ability): a cast in
round r reads cooldown 8 and is ready again in round r+8.
Evidence: abil-basic (Tavi, Willow, Vesper, Ryth; Tavi cast r20, ready r28) (`abil-evidence.txt`).
  Current correction: four 33-round OFF/ON native games, both sides, verify Vesper MOVE -> RESET -> BASIC
  with no earlier actual basic. At round 30, a step closes Wolf range 4 to 3 and changes M/A/C flags from
  1/1/1 to 0/0/1; slot 0 reset succeeds, flags 0/1/0; basic takes Wolf 1,350 to 1,295. The current
  independent observer reads rounds 1-30 and checks zero earlier basics, with two matched OFF/ON queried actor states.
  Source/replay identities: [move-reset.json](../../../results/contenders/micro/move-reset-3.0.5/move-reset.json)
  and [native-verification.json](../../../results/contenders/micro/move-reset-3.0.5/native-verification.json).
Rests on: M23; current Vesper action-path witness (micro U5).
Status: historical reset/cooldown facts verified engine1.0.2; Vesper MOVE/RESET/BASIC verified engine1.0.3
  in the stated one lab geometry. No other reset-kit, full-cycle, pool-health or strength claim.
Recheck: catalog ability cooldowns, attack allowances or reset legality.
Consequence: Plan from spent allowance, not a presumed prior basic. Attack/reset/attack can add a
  second basic; Vesper's step/reset/basic can instead make the first actual basic after closing range.
  Both consume the single cast and restore no movement. The old blanket "never reset, attack"
  advice was too strong. Credit only an available, legal executor sequence for the actual kit;
  cooldown readiness alone does not express every guard. One cast per turn and its cooldown still
  bound repeatable burst.

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
Consequence: One stunner denies at most one enemy turn per 8 rounds; a two-turn lock needs two stunners
whose casts straddle the target's turn (one acting before it in the round, one after), which depends
on round parity and on priority set a round ahead. A stun on a monster buys one free round of standing
in its range. Structure: under interleaving, crowd control is a turn-denial resource that chains only
across slot boundaries, so stun-chain kills need coordination planned one round ahead.

Current supplement: [U9](micro.md#u9-a-tactical-option-is-an-ordered-state-transition-not-a-ready-slot)
preserves an existing engine 1.0.3 controlled Ryth-on-Ryth case: actual stun1 denies the next owner
attack, decrements to0 at that owner end, then expires before the following owner input, where a
basic succeeds. This is one map/input/order orientation, not a revalidation of both timing cases,
all kits or normal-game frequency. Order the intervening owner opportunities before crediting
safety until our next input; a currently stunned target can spend the denied turn before then.

A distinct [saved current camp chain](../../../results/contenders/foundation/camp-stun-gap/chain.json)
now supplies a bounded engine 1.0.3 attack-gap witness on both sides of the dense Ryth lab:
four live wolves attack at round 17, Ryth casts zero-based slot 0 CC afterward, no camp attacks
occur in the complete round-18 phase, and the same four resume at round 19. Ryth stays fixed,
with HP 640 throughout round 18. Runtime metadata gives radius one/duration one; slot 1 is reset.
Conditional on the successful spell's stun contract, this supports one following camp-phase
volley denial in that task. The saved camp projection has no buff, awake or expiry fields:
this is not direct native stun-countdown/expiry refinement, a queried-existing-stun certificate,
or a timer-zero/other-CC/reapplication theorem. The reducer launches no games or native queries;
its exact preserved producer writes to the original scratch location when re-executed.

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

Current supplement: the [engine 1.0.3 Vesper witness](../../../results/contenders/mechanics/vesper-dash-current/witness.json)
binds source `aa2ed9dfa2bc`, ordinary Islands A/frozen passive, ten truncated rounds. At round 6,
ordinary movement from `(26,9)` into the queried hole `(27,10)` refuses with code 22 and preserves
`(M,A,C)=111` and both cooldowns zero. Zero-based slot-1 dash to `(28,11)` succeeds in that input,
changes the actual queried position and leaves `110`, slot-1 cooldown 8. Separately, the pinned
optimistic terrain-only walking graph disconnects source and destination even with all units
removed; that static result is distinct from the actual dash. This tests one dash's reach across
the hole, not attack/dash composition, capture, recall/return, service, other kits, full-game
health or strength. States are logged SDK queries, without an independent replay-state observer.

Constructive service application: source `3af97918ca82` runs one fixed BigWorld A task against
the same pinned passive for 125 truncated rounds. The [original witness](../../../results/contenders/mechanics/vesper-dash-service/witness.json)
is qualified by an [independent 52-object audit and saved-history observer](../../../results/contenders/mechanics/vesper-dash-service-audit/assessment.json).
Nine basics finish three harmless 120-HP gem members; the final basic and dash compose into an
actual capture-tile arrival, then recall, payout, bank and a ready spawn endpoint:

| Actual boundary | Observed effect |
| --- | --- |
| Round 59 final basic, then slot-1 dash in the same input | From `(55,30)`, allowances `001` become `000` at capture tile `(53,28)`, dash cooldown 8 |
| Round 60 | Five-tick recall starts while holding the cleared group |
| Round 65 phase 7/input 721 | Bag `0 -> 21` at the landing, still recalling with timer zero |
| Later round 65 phase 14/input 729 | Recall arrives at spawn `(78,12)` with all 21 gems |
| Round 108 | Actual tribute changes bag `21 -> 0` and own score `0 -> 21`; return recall starts afterward |
| Rounds 113/114 | Spawn arrival, then observed full HP 1,000, no channel, all allowances and cooldowns ready |

The fixed route/station guard excludes all 32 potential harmful camp ranges regardless of life,
sleep or stun; 114 logged quiet samples have no nearby unknown harmful source or visible foe.
These are this fixed passive scenario's guard premises, not contested safety or a general task
chooser. Gold stays zero: the witness establishes no investment effect. The endpoint permits a
next task but does not execute a second cycle or prove renewed admission, controller liveness,
repeatability, full-game health or strength. All aggregate values in the truncated game row,
including fault/exhaustion zeros, are unavailable placeholders. Measured logs have no error lines
or budget hits and a fuel peak of 72,626 over this prefix; native fault-event counts are unavailable.
Printed success `code=0` is hardcoded text, not a native code readback. The physical claims above
instead use queried state and the independent saved-history action/channel boundaries.

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

### M30. Shields: `shield_amount()` absorbs all damage first for five owner rounds
Claim: Varg's shield is 2.5 x Focus (250), Heart's 1.5 x Focus (150), equal to `shield_amount()`. Cast in
round t it reads duration 5 to 1 and is gone at the caster's turn start in round t+5. It absorbs champion
and monster hits before health; a breaking hit's overflow reaches health in the same hit (55 against 15
left: 40 to health). Durability applies first (the shield fell 49.5 per 55-damage hit). Unspent shield is
lost at expiry; a recast while active is impossible (cooldown 8 > duration 5).
Evidence: abil-heart r32-r37, abil-varg r40-r45 (`abil-evidence.txt`).
Rests on: M25, M40.
Status: verified (engine 1.0.2) for the original damage, duration and overflow observations.
  A current engine 1.0.3 supplement resolves camp-phase absorption at timer zero on both sides;
  earlier other-champion attacks at that boundary and other shield kits were not exercised.
Recheck: catalog shield payloads.
Consequence: A shield is value / (1 - Durability) effective health for a five-round window: cast it just
before burst lands, not early. Structure: shields reward reading the enemy's cooldowns (M45).

Current camp-expiry boundary: [shield-expiry verification](../../../results/contenders/foundation/shield-expiry/verification.json)
uses unchanged trace-OFF Varg `3889212f4fbf` against passive `83e3f45b13da`, two 25-round cells on
both sides of one mixed one-wolf/three-gold-cluster map. Complete native rounds 9–17 show:

| Event | Timer | Shield |
| --- | --- | --- |
| Round 14 owner end | 1 -> 0 | 170 retained |
| Round 15 camp attack | 0 -> 0 | 170 -> 150, HP 1,000 unchanged |
| Later round 15 owner start | expired | remaining 150 -> 0 |

Thus a shield freshly observed at timer 1 in the owner's opportunity can absorb the next camp
phase before that owner's zero-timer expiry. A duration guard requiring at least 2 omits this
witnessed available phase. Amount and intervening damage still need separate coverage; this is
not coverage after the owner's next start or permission to reuse a stale/suspended frame.
Both contained native observer Jobs exit empty. Raw fuel/error streams are clean in these short
cells (maximum ordinary fuel 7,109); truncated outcome statistics establish no full-game health,
normal frequency, profitable admission or strength. The original broader measurements keep their
historical engine 1.0.2 scope.

### M31. Willow's heal: min(3 x Focus, missing health) on another ally; wasted at full health
Claim: The heal restores min(catalog modifier 3 x Focus, the target's missing health) (360 uncapped, 40
capped), which `heal_amount(target)` returns; Willow cannot target herself (E30); a heal on a full-health
ally returns true, heals nothing and spends the cooldown.
Evidence: abil-basic r21/r29, abil-misc r22/r30 (`abil-evidence.txt`).
Rests on: M25.
Status: verified (engine 1.0.2).
Recheck: Willow's catalog payload.
Consequence: Gate heals on missing health at least the heal amount; Willow is never self-sustaining.
Structure: healing is a transfer that needs a damaged partner within range 4, so it rewards staying
grouped.

### M32. Vendetta: every kill she is credited with resets both cooldowns
Claim: Killing one monster of a group or a champion resets both her ability cooldowns to 0 at once
(one wolf of three: (7,6) -> (0,0); Ren: (5,3) -> (0,0)).
Evidence: abil-misc r31, abil-varg r57 (`abil-evidence.txt`).
Rests on: M22.
Status: verified (engine 1.0.2).
Recheck: Vendetta's passive.
Consequence: In multi-monster groups Vendetta re-resets and re-dashes per kill; hand her last hits.
Structure: her kit turns kill credit into tempo, so last-hit assignment is part of her damage.

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
Status: verified (engine 1.0.2).
Recheck: Marcella's passive and payloads.
Consequence: Channel-breaking stuns do not work on Marcella's channels, making her the safest banker
(she also earns gold per gem banked, M38). Structure: the one stun-proof body is the natural
carrier of the team's bags.

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
Current SDK path: fresh visible `champion.item_count()` gives equipment; immutable
`item_type::info()` exposes passive and magnitude. There is no public aggregate incoming-camp
damage getter. Apply each family passive once, even with duplicate emblems, then shield/HP in
event order. Current rules/catalog agree with the historical factors above; this is source
agreement, not a new engine 1.0.3 item-mitigation experiment. Other-body gold remains private.
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
attack range; state flags (recalling, tributing, teleporting, the can_move / can_attack / can_cast still
left this round and `completed_turn`, so whether it has acted this round); `remaining_channel_time()`;
both ability cooldowns; buffs with durations and the shield's remaining value; `restrictions()` (a stun
blocks move, attack, cast, shop, consumable and ward); `gem_count()` (its bag, the kill prize); exact
`item_count()` and consumable counts; current and next priority; its `visible_tiles()`. Gold is not
readable (`gold_count()` is self-only; the raw import returns E105 for enemies). A hidden enemy gives
only `is_alive`, `champion_name` and `team`.
Evidence: info-observe r6, r48-r84 (`info-evidence.txt`).
Rests on: M44.
Status: verified (engine 1.0.2).
Recheck: SDK or ABI changes.
Consequence: Inside vision, fight and chase models can use exact health, shields, cooldowns, bags and
whether the enemy has already moved this round; outside vision there is nothing, and gold must be
inferred from items. Structure: information is exact within sight and zero outside it, so vision is
worth as much as damage.

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
Claim: Occupancy masks, unit positions and vision are computed on demand: an enemy's move earlier in the
round already shows in one's turn, and one's own step updates one's vision at once (211 -> 214 tiles in
mid-turn). A turn suspended by fuel exhaustion resumes next round with the old turn's variables, which is
one source of stale copied observations (mon-target r45: a mask built in r44 met an enemy that had moved).
Guest memory or inferred fields retained across turns can also be stale (M52); this does not make the
live SDK query itself stale.
Evidence: info lane test r24-r30; fuel-lab2 exhaustion (`fuel-budgets.json`).
Rests on: M2.
Status: verified (engine 1.0.2).
Recheck: SDK caching changes.
Consequence: Move, then look, then act works inside one turn; never carry observations across a possible
suspension. Structure: later actors in a round know strictly more than earlier ones.

### M49. Camp and group state are readable only in vision; respawn timers are exact when seen
Claim: `camp.state()` works while the camp's tile (its last tile if dead) is in vision and reports dead
camps as dead; otherwise E97. Dead camps are absent from `camp_tiles` and the awake/asleep masks and
their health throws. `camp_group.state()` needs any member's spawn tile in vision (E97 otherwise) and
reports alive, `reward_available` or `respawning{timer}`; the timer counts down by one per round from
`max_respawn_timer()` at the payout. Capture progress and the capturing team are not exposed.
The current native replay oracle confirms a related attribution trap: in the retained group-18
branch, payout at round 70 changes the internal `rewardAvailable` flag from true to false and
`captureTeam` from team 0 to neutral (2), while actor 4 receives exactly 60 gold and 4 gems in
the same native step. A neutral internal capture marker therefore does not mean that no payout
occurred. This is an offline debug observation, not an additional SDK observation. Keep the
attributed payout event separate from the current group marker; simultaneous group payouts
need separate attribution rather than assigning an undifferentiated resource delta to one task.
Evidence for this engine-1.0.3 branch is retained in the
[native micro witness archive](../../../results/contenders/optimization/micro/native-backend/index.json).
Evidence: info-observe r21-r45 (`info-evidence.txt`); cap-basic group logs.
Rests on: M44, M16.
Status: verified (engine 1.0.2).
Recheck: SDK camp queries.
Consequence: A visible timer gives an exact countdown to earliest respawn eligibility, not an exact
future availability state: M16's occupancy condition can delay respawn indefinitely. Outside vision,
retain the observation and its age separately from the predicted deadline; re-observe before treating
an expired estimate as live state (M52). A body on a reward-available tile suggests capture intent,
but neither its later absence nor a shared reservation proves payout. Map memory must distinguish
what was observed, what is reserved, and what is merely predicted.

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
only the eight shared bytes change. Structure: bounded precomputation can move into the draft's larger
but finite budget; it still needs measured fuel limits (SDK S20).

## Observation and prediction safeguards

### M52. Capture intent and expired respawn deadlines do not prove availability
Claim: A reservation or a champion standing on a reward tile describes intent/presence, not completed
  payout. Leaving, dying or a shared claim disappearing can interrupt capture (M15), so none of those
  transitions alone proves the group has entered respawn. Likewise, a seen respawn timer gives the
  earliest eligible round, not the round the group must become alive: any occupied member spawn can
  keep the freshly observed state `respawning{0}` indefinitely (M16). The SDK's `alive`,
  `reward_available` and `respawning{timer}` are distinct alternatives even at zero.
Evidence: logical consequence of historical native M13-M16 and current public
  `sdk/cpp/cambc.h` camp-group state variants/reader (lines 4462-4503), plus
  `docs/reference/GameRules.md` "After defeating a camp, make sure to capture!". Separately retained
  current-engine corroboration, not rerun by this audit: main D158 records enemy-occupied g_734
  freshly respawning at timer zero for hundreds of rounds and a downstream wait/liveness failure
  (`.cache/main/economy107`, `.cache/main/wait107-repro`, one pool map, both sides, starter).
  Main D135's capture-inference witness finds 54 contradictions in 15 of 20 baseline probe games
  versus zero in 20 fixed games; that is bounded witness coverage, not an exhaustive event rate.
Rests on: M13-M16, M49; current SDK state contract. Corrects M49's original exact-memory
  consequence and the countdown-only advice in SDK S6/S10; it does not change those fuel measurements.
Status: verified logical correction against current public source (CLI/SDK 3.0.5, engine 1.0.3
  bundle); historical and independently retained native witnesses remain scoped to their own runs.
  No new native games or contender health/strength claim from this audit.
Recheck: changes to capture reset, spawn occupancy, visibility or the group-state API.
Consequence: Keep observations (including their round), reservations and predicted dates separate.
  Fresh state overrides an expired estimate. A stale expired estimate can justify travelling to
  inspect; it cannot justify declaring a live camp, confirmed payout or an unbounded wait. Leave
  one's captured member tiles so production can renew; make target eligibility and wait termination
  explicit in the real caller. Minimal fixtures cover interrupted capture, fresh respawning at
  timer zero, stale expired estimates, and reopening on a fresh alive/reward observation. Different
  doctrines can choose different reservations, inspection routes and denial policies under this
  same state contract. E12-E14/P4 schedules retain their declared unobstructed/model assumptions.
  A fresh aggregate seen timestamp does not prove every remembered field was actually queried.
  Retain field provenance: timetable's failed round-97 reward witness had seen_round97 while
  native group state was respawning with timer30; its later actual lifecycle query supplies a
  separate fresh-reward witness (E29). This concerns guest memory, not a failure of live SDK queries.
