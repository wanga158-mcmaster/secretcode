# Champion interaction traces

`tools/inspect-strategy-replay.mjs` reads retained `.repl26a` files through the
matching, SHA256-pinned official visualiser WASM. It loads and walks replay
history; it never runs bots or calls action, runner or server APIs. Its Python
bridge uses the shared compiled-runner containment path: one machine-wide arena
slot and a private Windows Job assigned before Node resumes. The decoder has a
120-second wall timeout after slot acquisition; waiting for a slot is outside
that timeout. Use the Node
and Python installations printed by `arena.cmd env` (Node is not on PATH on the
standard Windows installation).

Each invocation retains dispatch, raw stdout/stderr and process/cleanup receipts
under a fresh `.cache/arena/replay-inspector/replay-*` directory. Output is buffered
there and forwarded without text conversion after the owned process tree is
confirmed empty; stdout and stderr byte contents are preserved separately.
Normal child exit codes are preserved, and a timed-out decoder returns status
124. Caught interruptions retain the exact process/Job handles and slot until
shared cleanup confirms the Job is empty; recovery does not relaunch the decoder.
An interrupted cleanup can therefore keep the bridge alive beyond the normal
decoder timeout. The shared launcher's documented abrupt-parent-death limitation
before Job assignment still applies.

The internal bridge accepts the configured arena Node executable, the canonical
inspector, and supported diagnostic Node flags (including the existing
`--disable-warning=ExperimentalWarning`). Evaluator/preload flags and unrelated
scripts are refused. The generated native-vision oracle used by the test suite
has a separate, narrowly scoped test helper using the same containment path.
Callers using automatic bridge entry must not hold another arena slot around
Node. `ARENA_REPLAY_SLOT=1` is the child handshake for a parent that already owns
both the shared slot and reviewed process containment, rather than a slot bypass.
The bridge supplies that handshake through one fixed child-only Node `--import`
module, leaving the Python parent's environment unchanged. Original application
argv and launched argv with this internal marker are retained separately.
Python adapters may import the bridge and call `run(argv, stdout, stderr)` with
binary streams directly, without an outer Slot, subprocess or timeout. Concurrent
calls keep slot acquisition and decoder execution parallel; only shared retained
cleanup is serialized so the same handles and Slot cannot be closed twice.

## Commands

```text
node tools/inspect-strategy-replay.mjs --replay R.repl26a --trace 1 --from-round 45 --to-round 60 --actors 4,5,9 --map-id 00eight
node tools/inspect-strategy-replay.mjs --replay R.repl26a --trace 1 --from-round 45 --to-round 60 --teams 0,1 --format jsonl
node tools/inspect-strategy-replay.mjs --replay R.repl26a --trace 1 --from-round 45 --to-round 60 --targets champion:9 --kinds attack,cast,movement,death
node tools/inspect-strategy-replay.mjs --replay R.repl26a --trace 1 --from-round 426 --to-round 426 --actors 4 --max-events 2000 --max-bytes 4194304
```

Teams are absolute: `0` is A and `1` is B. Champion IDs are global native roster
IDs, as in match statistics. `--actors` and `--teams` intersect; opponent actions
affecting a selected champion are included. Without these filters, trace mode
selects every champion on both teams. `--actor ID --team N` also works, as do the
existing `--actor all` position reports. `--targets` and `--kinds` narrow retained
events; summaries describe the retained events only. Ground targets use
`--targets tile:X:Y`, and kind filters accept native kind numbers `0`–`27`, action
names such as `attack,cast,movement-blocked`, or tags such as
`movement,damage,healing,combat-state,channel,economy,capture,visibility,death,turn`.
Detailed windows are limited
to 64 inclusive rounds. Default bounds are 20,000 events, 16 MiB of trace payload,
one million native steps and a nearby radius of four tiles; the corresponding
options are `--max-events`, `--max-bytes`, `--max-steps`, `--nearby-radius`.
The byte cap applies to `interactions`; identity metadata and optional legacy
reports are outside that cap. JSONL adds its small record-label overhead.

`--trace 1` defaults to skipping the legacy full-history capture pass, enabling
binary seeking to just before the requested round. Pass `--seek 0` to walk from
the start, or `--captures 1` for the original reward-transition report as well.
Personal camp-observation histories requested with `--group-keys` still walk from
the start. Existing position tracks, capture receipts, `--state-changes 1` and
`--channels 1` retain their formats when trace mode is off. The latter two modes
still require a single actor and at most 64 rounds. No viewer change is required:
`tools/view-replay-local.py` continues serving the matching pinned official UI.

For one personal camp/occupancy checkpoint, use
`python tools/replay/personal-camp-oracle.py --spec .cache/OWNER/scene.json --spec-sha256 SHA256`.
The bounded scene has schema `arena-personal-camp-oracle/v1`, hash-pinned `replay` and `map`
descriptors, one native `actor`, absolute `team`, `round` and boolean `sample`. Run the same
checkpoint with sampling disabled and enabled; require identical history-prefix and checkpoint
hashes, intact allocation canaries and matching perspective before using the input card.
The sampler resolves only personally visible living camps and current occupancy; public designs
are separate. Ability cooldowns do not establish the basic attack interval, and current legal
inputs do not prove a safe future route or the original bot's private caller state. The fixed
launcher uses the existing private Job and shared slot with receipts under
`.cache/altar-camp-oracle/transport/`. Its offline checks are included in
`python tools/tests/run.py personal-camp-oracle`.

For the current `cli-3.0.5` profile, add `--locks 1` to a bounded trace to query
altar locks through each selected champion's personal perspective. Champion states
include `personalAltarLocks`: `[]` is a successful empty view, records contain the
lock's team, coordinates and remaining hits, and `null` means unselected or failed.
`interactions.native.personalAltarLocks` records query failures, bounds and the
queried runtime `settings.altarLockHits`. This is a read-only replay query and
does not establish when the original bot chose to inspect a visible lock.

Shared memory has a separate limitation. The official `cli-3.0.5` API can read a team's bytes,
but this does not prove that replay loading restores historical writes. A bounded Warband
probe returned successful all-zero reads that failed the source's known leader-stamp and
availability invariants, while public state remained unchanged. Treat that historical memory
as unavailable; do not decode reset-looking bytes into actual bot intentions. The
[case record](../results/population/warband-current-strength/index.json) preserves the scope.

Durable retention bindings named `officialAssets` use
`filename: {"path": "absolute official cache path", "sha256": "full digest"}`. The path must
identify the configured Cambc asset cache; ordinary repository evidence paths stay relative.
Each asset descriptor has exactly those two fields; keep byte sizes in a separate map.
Validate this schema as well as the hashes before publication. Label plain filename-to-hash maps separately: malformed authoritative bindings
block cache cleanup rather than silently dropping asset protection.

## Reading the output

JSON is compact. JSONL begins with one `record: "metadata"` object containing
identity, state/vision/terrain/ability dictionaries, coverage and summaries, followed by ordered
`record: "event"` objects. A single pass includes all selected champions and
their interactions. Event and state references make repeated stat blocks cheap.

Each event identifies its round, acting champion and absolute team when those
exist, exact native input/substep, raw actor kind/index, processing phase and
recorded action. Camp and global processing can have a null acting champion.
The action's **step round/input** drive identity and filtering: the snapshot's
current round/input can already point at the next turn. Input indices are never
interpreted as rounds. Phase labels distinguish `round-start`, `owner-start`,
`action` and `owner-end`, while preserving raw phase numbers.

Action identity, `observed` before/after field changes and coordinate-derived
target candidates are separate. An attack receipt plus a health drop does not
prove all the health loss came from that attack. Shields, expiry, retaliation,
linked champions, area effects and camp processing can change several entities.
Derived health/shield-loss totals and separation changes retain exact event IDs;
they are measurements over observations, not causal damage attribution.

Movement uses actual before/after champion coordinates and the replay's recorded
endpoints. Dashes, ordinary steps, recall/teleport arrivals, respawn and death
removal are distinguished. Unrecorded intermediate paths are explicitly null.
Blocked receipts preserve their endpoints, allowance changes and personal reveal
changes. A generic blocked receipt does not establish a hidden occupant or the
attempted action type. Positioning context is omniscient, with personal visibility
reported separately. Static terrain comes only from matching-profile geometry;
its map identity retains the existing dimensions/selected-label qualification.

Champion states include native health, shield, Strength, Focus, range, cooldowns,
movement/attack/cast flags, restrictions, buffs, immunity, channel state/timer,
alive state, sparse equipment/consumable slots and carried gold/gems. Zero-duration buffs remain
present until owner-start expiry. A zero channel timer is pending completion;
an absent channel has a null timer. Query failures remain null and appear in
bounded diagnostics. Respawn arrivals are observed; the respawn countdown is
explicitly unavailable because its native export lacks a verified matching SDK
query contract. No guessed signature is called.

Turn-end receipts retain the preceding state's available actions, death, stun
and channel context. An unused action is an observation, never an automatic
mistake. Native allowance flags also expose attack resets, ranged move/attack
coupling and the consumed single successful cast.

Check `interactions.coverage` and `interactions.native` query diagnostics before drawing a
conclusion. Caps, incomplete windows, missing before-states and query failures
are explicit, including windows beyond the end of retained history. A finished
match and an unfinished replay with no remaining inputs are distinguished.
Replay bundle/payload, tool/helpers, profile registry/module,
decoder and selected geometry hashes bind the result. Event IDs depend on native
history, not how many host steps were needed to seek there.

## Validation and retained examples

```text
node --test tools/tests/test-inspect-strategy-replay.mjs tools/tests/test-replay-interactions.mjs
python -B tools/tests/test-replay-native-slot.py
python -B tools/tests/test-view-replay-local.py
```

The slot-bridge tests exercise the real shared command/slot wrappers with fake
Slot and process objects: they launch no decoder. The other tests read retained
fixtures and cached official assets; they never launch games. The native cases
cover chronological actions, movement, resets, dash,
shield and stun timing, immunity, blocked hidden occupancy and reveal, death,
channels, filters, truncation, JSONL and seek/walk agreement. Existing tracks,
captures, geometry/personal visibility and channel receipts remain regression
checked. Native tests explicitly skip if their pinned official asset is absent.

The small fixture `tools/tests/fixtures/skirmish-cli-3.0.5.repl26a` is an exact
copy of the retained `results/online/scout/micro-leaders104/33d91940.repl26a`
(SHA256 `0df351adfcacf0ea7c1d34fdd4463b4a6da886b0f92168fbaec4b30046bd3b8a`).
It supplies real interaction examples without rerunning or instrumenting bots.
Verified traces and their exact event references live in the
[evidence index](../results/replay-analysis/index.json):
[reset skirmish](../results/replay-analysis/reset-skirmish.jsonl) and
[hidden-enemy reveal](../results/replay-analysis/hidden-reveal-skirmish.jsonl).

Two real examples, both checked against native before/after states:

- **Reset secures a kill, rounds 45–60 on Eight.** At R53, Issac4 attacks Heart9
  (48.23984 → 8.23984 HP), casts slot0 to restore his attack allowance while
  consuming his cast, then attacks again. Heart dies and Issac gains 100 gold.
  Inputs `1452/0`, `1453/0`, `1454/0` preserve the whole sequence. The same window
  shows a shield with timer0 absorbing damage before owner-start expiry.
- **Movement failure reveals an enemy, rounds 113–124 on Contest.** At R113,
  Heart5's attempt from `(28,22)` to `(27,21)` produces a blocked receipt at
  `3957/0`. Tavi3 occupies the previously unseen tile. Heart's personal vision
  gains that tile and enemy; movement, attack and cast flags remain available.
  The trace preserves the observed reveal and occupancy evidence while labeling
  the occupant as an inferred explanation, and makes subsequent actions readable
  in the same chronology. It does not call the failed attempt a mistake.
