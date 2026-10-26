# contenders/_archetypes: archetype opponents for the contenders' battery

Scratch bot (leading `_`: bulk selectors and the ladder skip it; only the exact id
`contenders/_archetypes` selects it). Purpose: opponents that faithfully embody the O1 kinds
(foundation/online.md O1, O10, O16-O18) that no local bot represents, so that whether a contender
has an answer to each kind can be tested. Strength is descriptive; the behaviour is the admission test.
Owned paths: `bots/contenders/_archetypes/`, `.cache/contenders/archetypes/`.

## State (checkpoint 22 Oct, before a planned reboot)
- Done: foundation and chassis read (mechanics M1-M51, sdk S20-S24, economy E8-E9/E22-E23, online
  O1/O10/O16-O18, maps P3-P6, main-v35 source, arena core, contenders tools); `src/` = byte-identical
  copy of `anchors/main-v35/src` (diff -r clean; not yet built under this id; it builds as main-v35 does).
- Half done: nothing in code. The `persona` knob is NOT added yet.
- Not started: all nine personas' code, the persona-0 determinism check, smoke runs, witnesses,
  health runs, standings. No game has been played for this bot.
- Scratch: `.cache/contenders/archetypes/mapsurvey.py` (pool maps: jackpot share, monoliths, classes).

## Monoculture caveat
The chassis is main-v35 (strongest local chassis, so personas do not lose for unrelated reasons). Every
persona therefore farms, fights (skirmish simulation), recalls and shops like main: these are
main-flavoured versions of each kind, not the online teams. A contender that has learnt to beat
main's habits may beat a persona for reasons unrelated to the persona's kind; read results per kind
and confirm against other suites/online evidence.

## Implementation rules (keep persona 0 = main-v35 bit for bit)
- `config.h`: `inline constexpr int persona = 0;` at the top; persona parameters as their own knobs;
  existing knobs derived by ternaries (`tribute_gems = persona == 8 ? 100 : 25`), which arena can still
  override with literals.
- Persona code only under `if constexpr (cfg::persona == N)` (no code emitted for persona 0).
- Persona state in a new `persona.cpp/.h` as function-local statics (no new members in Brain/World/
  Knowledge: no layout or constructor change; no namespace-scope objects with constructors: start-up
  fuel must not change).
- Health: every action in try/catch with `MB_LOG("... failed: ...")` (contains "failed", never an
  `error` line unless a real error); each new scan bounded and gated by `fuel_ok`; at most a few log
  lines per game (a printf line costs 3-8K fuel, S22).
- Determinism check after adding the knob and after each persona: play `contenders/_archetypes`
  (persona 0) and `anchors/main-v35` against the same opponent, all pool maps, both sides; every row
  must match (winner, rounds, per-team gems/gold/kills/deaths/clears/fuel_max); spot-check that the
  replays' `.fuel` streams are equal.

## Persona design (value: kind, behaviour, hooks, witness, maps to show it)
Hooks refer to main-v35 functions: brain.cpp `handle_threats` (the `favourable` rule, the
own-spawn ignore rule, `limited_chase` in combat.cpp `fight_enemies`), `handle_low_health`
(`recall_health_frac`), `handle_tribute` (trigger: gems >= bag, or da <= `tribute_detour_tiles`,
end of game, or win now), `score_group` (rate modifiers; wait > `max_respawn_wait` rejects;
`free_allies` capped by `max_hypothetical_joiners`; `plan_siege(..., siege_max_trips)`), `handle_farm`
(staging patience, respawn-wait branch, hunt block, travel), `go_capture`, `lock_enemy_tribute` /
`place_lock`, `try_tp_shortcut` (only in `travel_to_group`); shop.cpp `plan_for`/`main_stat`/`shop`
(the lock is bought after items, the crystal before); draft.cpp `preference`; knowledge.cpp squads
(`squad_of`, the leader is the lowest living index = earliest pick).
1. battery (O17): draft Prism first, then Marcella, Ryth, Heart, Vesper. Prism banks only at >= 250
   gems (`battery_bag`), on win-now or at game end (never the detour rule); Prism values gems x3; Prism
   Health build (Durability/Health > Jungle/Health > Home/Health > Bruiser/Health); Prism recall at 0.6
   while carrying >= 50. Escort: custom squad Prism + strongest melee ally (others solo); the escort
   never captures (stays off member tiles) while the Prism is alive within 8, so the Prism takes whole
   captures. Witness: Prism carry >= 150 at r400 (o1flags `prism_carry400`) and tributes >= 100 in one
   bag. Maps: gem-rich 00lost_world, 00road_of_riches, 00gem_fields, 00gemfight, 01shortcut,
   00great_wall, 00packs.
2. jackpot (O10): jackpot = group with max gems/respawn (walk-reachable). score_group for it: all allies
   as joiners, siege_max_trips 12, wait allowed up to 50 rounds before its respawn, rate x4; staging
   patience 40 there; await beside it (never on member tiles) before respawn. Witness: captures of the
   jackpot group per game (side record `caps`, gid = jackpot). Maps (jackpot share >= 0.4): 00contest
   (monolith 200 gems), 00rows (470), 01gold_rush (200), 00pilgrimage, 00stampede, 01elk_arena,
   00gemfight; other monolith maps 00monolith, 00split, 01corridors, 01broken_curve, 01the_farm.
3. siege (O1 spawn camping): from r400 the team holds posts at walking distance ~5 from the enemy
   spawn on the side facing our spawn (field rooted at enemy spawn tiles; k-th nearest candidate per
   ally index), farms only groups within 12 of the enemy spawn, fights at margin 0 (even trades) and
   chases anywhere off their spawn tiles; carriers still bank, low health still recalls. Witness:
   kills within 6 of the enemy spawn rect by r1000 (o1flags `spawn_kills` >= 2). Maps: close or
   medium spawns (00swirl, 01elk_arena, 00fragment, 00rushbait_v2, 00reverse_spiral, 00interlock,
   01hooks, 00packs).
4. locker (O1 lock harassment, M20): everyone lock_mode 3 and buys the lock BEFORE items at every
   spawn visit; the last 1-2 picks are harassers that watch the altars nearest the enemy spawn from
   Chebyshev 2 (bush preferred), lock any enemy channel there (cancels it even through the owner's
   lock), pre-lock after 40 quiet rounds, recall to re-supply when empty. Witness: locks placed at an
   altar while an enemy was channelling there (replay steps PlaceAltarLock vs the enemy's open
   SubmitGems; baseline: main-v35 mode 1 fires ~0.13/game, DECISIONS D76).
5. invader (O1 territory invasion): score_group rate x3 for groups nearer the enemy spawn (walking
   from both spawn centres), x0.5 for own-half groups. Witness: `enemyhalf300` >= 0.25 (Chebyshev to
   spawn centres, as trackfeat). Any map; clearest on mid maps.
6. sustain (O1 Willow): draft Heart, Willow, Ryth, Marcella, Vendetta (Willow on 3-pick maps); custom
   pair Willow + strongest melee (Willow follows); Willow builds Focus (heal 3 x Focus); the partner
   recalls at 0.25 instead of 0.5 while Willow is visible within 4 with heal ready or nearly so.
   Witness: healingDone >= 3,000 per game (o1flags `heal`). Any map.
7. crystal (O1 teleport routing, M21, P6): tp_shortcut_margin 2 (any use then crosses walls/holes:
   in open ground a 3-tile jump never beats walking after the 4-round channel); also shortcut on
   tribute trips. Witness: TeleportChannel/TeleportComplete steps whose walking distance exceeds the
   Chebyshev jump (BFS on maps/geometry.json), and `crystals400` >= 2. Maps: 01islands, 00collision,
   00quadrants, 00ambush, 01big_world, 01fracture, 01shortcut (main rejected the knob for strength, D74).
8. hoarder (O1 big bags): tribute_gems 100, tribute_detour_tiles 0. Witness: `bag_med400` >= 40.
9. turtle (O1 passivity): only groups with Chebyshev(centre, enemy spawn centre) >= own + 2; any enemy
   off its spawn within 8: retreat home (melee strike while backing off), never engage; own-half
   altars only; idle beside own spawn. Witness: `enemyhalf` <= 0.03 and `spawn` >= 0.08.

## Measuring witnesses
- Keep replays: `--keep-replays all`. Side records, track features and the O1 flags exactly as online:
  import `bots/contenders/tools/fundamentals.py` (`load_runs(runs, side="subject")`) and
  `o1flags.py` (`feats`, `flags_of`) read-only from a script in `.cache/contenders/archetypes/`.
- Not in the statistics: lock placements and crystal uses. Use
  `node bots/contenders/foundation/tools/mechanics/replay-events.mjs REPLAY` (steps PlaceAltarLock,
  TeleportChannel, TeleportComplete, SubmitGems, GemsSubmitted, CancelChannel) and filter its lines.

## Fielding a persona
- Command line: `./arena.cmd play contenders/_archetypes anchors/main-v35 --set persona=N --maps pool`
  (the persona is the subject; `--set` repeats for its other knobs).
- As an opponent in a script (as `bots/main/tools/gauntlet.py` does with main's knob personas):
  `from core import bot, read_closure, apply_overrides, prepare_many`; `b = bot("contenders/_archetypes")`;
  `contents = read_closure(b)`; check each override set with `apply_overrides(b, contents, {"persona": "1"})`;
  then `prepare_many([(b, {"persona": "1"}, "persona:battery", contents), ...])` returns built entrants
  for `games.GameSpec(a, b, map, tags)` / `games.play(...)`. Values: 1 battery, 2 jackpot, 3 siege,
  4 locker, 5 invader, 6 sustain, 7 crystal, 8 hoarder, 9 turtle (0 = main-v35).

## Resume (exact commands, from Bash in D:\files\battlecode)
1. Implement the knob and personas (rules above), then `./arena.cmd build contenders/_archetypes`.
2. Determinism: `./arena.cmd play contenders/_archetypes anchors/starter-cpp --maps pool --out .cache/contenders/archetypes/runs/p0-starter`
   and `./arena.cmd play anchors/main-v35 anchors/starter-cpp --maps pool --out .cache/contenders/archetypes/runs/v35-starter`; compare rows per (map, side).
3. Per persona N (name P): smoke on its maps with `--maps a,b,c --keep-replays all --out .cache/contenders/archetypes/runs/P-smoke`,
   then health: `./arena.cmd play contenders/_archetypes anchors/starter-cpp,anchors/main-v35 --set persona=N --maps pool --keep-replays all --out .cache/contenders/archetypes/runs/P-pool`
   (an interrupted run resumes with the same command and `--out`; finished games are reused).
4. Witness and standing per persona from `.cache/contenders/archetypes/runs/P-pool` (score vs main-v35 with coverage).
