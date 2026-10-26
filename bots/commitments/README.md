# commitments: strategic interdependence and physical commitments

Sixteen bots on one shared runtime. Each makes one opponent-facing physical commitment (what it
carries, banks, buys, locks, occupies or releases, and when) and keeps a matched control arm
without it. All 16 build. Recent measured games were runtime-clean and mostly scored gems, but
none has beaten `anchors/fundamentals`: 0 wins in 332 local games (about 0-380 gems vs ~1000).
Older source won 2 of 36 against `anchors/starter-cpp`.

## Doctrine

**Question.** Does choosing which physical state we expose to the opponent, and when, change the
contest it faces enough to beat a better plain economy?

**Shared hypothesis.** Our state is part of the opponent's payoff. A carried bag is loot; banking
removes the loot but ties up an actor; gear, heals and locks persist; an actor committed here is
missing there. A team that sets these states deliberately leaves the rival worse responses
(contest, wait, substitute or unknown) than a team that only maximises its own yield. Value is
still only our own submitted gems. No credit goes to denial, to an imagined enemy delay or to a
threat that is never physically carried out, and every rival response must leave us a productive
continuation.

**Held fixed (`common/`): one `Runtime<Policy>`.** It observes self, up to 5 allies, the 2
closest rivals (every visible rival still steers movement), 24 camp groups and 2 altars. Its
ordinary economy uses an 8-source opening catalog (4 or 6 gem slots, the rest gold bootstrap,
later up to 24), the two altars nearest spawn, bank batching, Monster Strength, Jungle Health and
then Power Strength purchases, exposure-aware movement and recovery. Campaigns are capped at 2
per 64-round epoch and 16 owner turns, and lapse after 8 turns without fresh evidence. Only
personal evidence of our own bank, kill, heal or purchase counts. Rival responses are labelled
from up to 3 fresh observations. Options are ranked lexicographically: feasible, then the worst
own gems over 4 responses x 3 deadlines, then nominal gems, turns, exposed bag, arrival and id.
Errors are caught per turn, logged as `p9 error [slug]`, and the bot yields.

**Varied per bot.** `live-policy.h` (50-115 lines, `Policy::step(World, Arm)`) names the
commitment, its trigger, the roles of picks 0-2 and an `Arm::control` branch: the same
organization without the commitment. `live-profile.h` holds the design id, the slug, a distinct
five-pick draft (maps use its 3/4/5 prefix) and the economy knobs: bank batch, recall threshold,
shard goals, partial clears, gold-bootstrap pick and pre-move casting. Picks a policy does not name
run the ordinary economy.

**Neighbours.** `doctrines` judges bank/hold/reinvest choices by our own production; here they
are judged by what they leave the rival. `doctrines/endowment` keeps Prism's bag for Focus,
while banked-vanguard liquidates it before fighting. `doctrines/dividend` reinvests in
Marcella's route, while dividend-redeployment makes her support for Varg's payout.
`distributed/recovery-relay`, `distributed/contingencies` and `adaptive/circuitbreaker` react to
our own crew state; successive-waves and capped-contest-investment react to the rival's observed
weakness and to marginal value. `territory` prices geography; forbearance-frontier waits for an
observed rival intrusion. `frontier/conversion-race` and `events/terminal` organise play around
the finish; critical-mass-conversion acts only when one collection can end the race. Never
replace the 16 policies with one shared solver.

## Bots

The number after each name is its profile design id. Fundamentals scores are local games of this
source on engine 1.0.2, given as team A / team B; all were losses. "Unplayed" means no native game
with the current source.

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| twin-deadlines (01) | Marcella and Ren, both loaded and within 3 tiles in staging rounds 16k..16k+3, each take a different altar and start tribute at 16k+12 (at most 8 turns of waiting), so one interceptor cannot stop both. Both altars must be personally visible and unlocked, with represented routes for both actors. Tavi farms near the first altar and, while team score is 0, fights blockers on his capture for 7 turns. Control: bank when loaded. | Conditional production-policy scheduling, control, renewal and refusal checks pass. Retained Collision B R384-387 has a loaded visible meeting but hides the second altar; banking at R392 does not witness the R396 commitment. Native activation remains unverified; [bounded evidence](../../results/population/twin-exploration/result.json). |
| capture-fork (02) | Vendetta, Varg and Marcella meet in staging. Vendetta clears the lower-id of two safe uncontested groups for Marcella to hold; Varg clears and holds the other. Both holds are timed to 16k+12. Control: serial capture of the first group. | Unplayed. Needs a 3-actor meeting and two safe groups. |
| cross-type-fork (03) | Loaded Marcella meets the crew in staging and tributes at the lowest-id altar while Varg (Tavi helping) takes the nearest safe group more than 2 tiles away, in the same 16-round slot, so the rival must choose which to interrupt. Control: convert first; acquire after seeing Marcella unloaded. | Unplayed. Older builds kept bags through bank failures and capture loops. A challenge to one branch ends both. |
| counterclaim (04) | Ren and Varg near loaded Marcella, with a favorable loaded rival nearby. If the rival closes on or threatens her, they escort; if it moves away, they intercept; otherwise they farm the nearby source. Control: choose escort or intercept once, at the proposal. | Unplayed. Needs a favorable loaded rival within 4 tiles. |
| banked-vanguard (05) | Prism banks his whole bag and confirms the bank and his lower Focus before joining Varg on a contested safe group, where Marcella owns the capture. Opt-ins: ordinary work avoids contested captures; Prism buys Durability after his first Jungle emblem. Control: loaded Prism keeps Focus and prize. | Fundamentals 00collision 191/168. Engine 1.0.2 raised Prism's Focus per carried gem to 1.5, so banking costs more Focus. |
| dividend-redeployment (06) | After her own bank, Marcella spends that gold on a Durability shard at spawn, then supports Varg, who owns the next payout. She returns to her original source once he has banked. Control: ordinary runtime. | Unplayed. |
| decoupled-prizes (07) | Tavi owns the source with the most gold per gem and Marcella the one with the least, so combat capital and stealable loot sit on different actors. Tavi's own payout funds a Durability shard; then he guards Marcella's source. Varg helps her. Control: one principal takes the best cycle. | Default-OFF integer ratio trial removes 643 current Starfish exhaustions across both sides; paid gear and own banking witnessed on Collision. Full-pool health and intended support-to-recipient linkage remain open (see Leads). |
| bounty-syndicate (08) | Vesper and Varg intercept favorable loaded rivals. Only a kill credited to Vesper funds her Durability shard and a move to a new source. Control: same interceptions, no funded expansion. | Unplayed. Cassi/Issac is drafted but the link is unused. |
| spent-response-exchange (09) | Ren escorts loaded Marcella to tribute only after seeing the one visible rival (not Vendetta) go from ready to cooldown. The runtime must also confirm every other rival is credited dead, that the rival carries no teleport crystal and that it cannot reach her within the window. Control: the same escort whenever ready. | Unplayed. The window is so narrow that active may rarely differ from control. |
| prepaid-column (10) | Willow heals injured Varg at staging only when the runtime proves the Varg/Marcella column can clear, capture and bank with no later heal. She returns to her own source after seeing both leave. Control: Willow stays attached. | Fundamentals 00collision 145/199, 00swirl 15/127, 00lanes 301/378 (best covered). Willow dies to camps on 00swirl. |
| committed-capacity (11) | Varg funds up to five Durability shards from his own gold and escorts loaded Marcella at a contested safe group. She joins only after seeing his shards. Control: ordinary production gear. | Fundamentals 00swirl 15/23. |
| successive-waves (12) | Varg contests a group and exits after 8 turns. Ren starts a second wave only after watching Varg leave while a nearby rival is weakened (HP below 65% or an ability cooling down). Control: Varg stays 16 turns; Ren works alone. | Unplayed. |
| lock-work-exchange (13) | While Marcella is loaded, Tavi buys one altar lock with his own gold, places it on her service altar and leaves to farm: capital replaces labor. Varg escorts Marcella in both arms. Control: Tavi stuns enemy channels at that altar. | Lost World A active/control discovery is subject-clean, with equal exposed state through Tavi's R65 owner-start. No lock contribution is witnessed; this case is parked (see Leads). |
| forbearance-frontier (14) | Varg and Tavi farm their nearest own-side source. Seeing a rival on our cleared productive group allows one bounded counterclaim on an enemy-side source. Control: that outer pressure whenever available. | Collision current baseline is productive; a real 47-turn home-cooldown wait does not block respawn. Default-OFF renewal is rejected after longer loaded bank stalls (see Leads). |
| capped-contest-investment (15) | Vendetta and Varg contest one group. After 8 turns each compares the remaining cycle with the nearest alternative and switches if that ranks better, ignoring sunk effort. Control: finish or reach the cap. | Unplayed. The switch is permanent, with idle-stall risk (see Leads). |
| critical-mass-conversion (16) | When one group's gems would bring Marcella to the winning total, she holds it while Varg clears and Tavi locks the winning altar if he is already funded at spawn. Control: ordinary runtime. | Unplayed. Fires only near a close finish. |

## Distinctness rules

1. A bot is distinct only through its card. A card means a trigger that is observable now, a
   physical commitment (which state changes and who owns the payout), and a control arm that
   differs in exactly that commitment. Two bots that make the same choices count as one; redesign
   or retire one of them. A new roster or new profile numbers alone never make a new bot.
2. A card that never fires plays as the shared runtime. Show strategic directives (replays, or
   the per-task strategic counts in the `p9 decisions` log line) before claiming an effect.
3. One owner changes `common/` at a time. A shared change moves all 16 bots, so put
   card-specific behaviour behind an opt-in (a profile field, the runtime's design-9/10
   certificates, or a `Policy::` hook such as `avoid_contested_captures()`), keep other cards'
   defaults, and re-test, rebuild and re-measure the whole suite on both sides.
4. Tune profile knobs per bot, never all bots toward the same values. Keep the rosters distinct.
5. A new bot needs a new design id, a slug equal to its directory name, the namespace
   `p9::<slug_with_underscores>`, a two-arm case in a design fixture and an entry in `DESIGNS`
   in `tests/run.py`. The `layout` test enforces these.

## Layout and checks

- **Bots:** `<bot>/src/main.cpp` is one line, `p9::live::run<Policy>(profile, Arm::active)`,
  next to `live-profile.h` and `live-policy.h`.
- **Shared modules** (`common/src/`, about 2.25k lines): `contract.h` (world, profile, campaign,
  responses, `cycle_value`/`better`), `competition.h` (economy rules), `receipts.h` (own-tribute
  attribution), `navigation.h` (staged 120-step fields, catalog, altars, movement), `sdk.h` (SDK
  adapter, `../../../../sdk/cpp/cambc.h`) and `runtime.h` (observe, decide, execute).
- **Tests:** `./arena.cmd test commitments` runs `bots/commitments/tests/run.py [bot|case ...]`
  in about 10 s. Cases: `layout` (closures stay in the suite, unique ids/slugs, every bot
  covered), `competition`, `receipts`, and `designs-01-05`, `designs-06-10`, `designs-11-16`
  (both arms of every policy). Fixtures build at `-O3 -DNDEBUG -Werror` and use explicit checks.
- **Build:** `./arena.cmd build commitments`. A build proves neither fuel safety nor strength;
  the largest measured regular turn used 71,217 of 100,000 fuel (older source).
- **Control arms** have no entrypoint. Measure one with a scratch bot
  `bots/commitments/_control-<bot>/` whose `main.cpp` passes `Arm::control` (names starting with `_`
  are skipped by bulk selectors and the ladder; move it out afterwards). There are no suite tools.
- **Logs:** `p9 decisions`, `p9 budget`, `p9 loaded`, `p9 bank-block`, `p9 capture-defense`.

## Leads

The 22 October Lock-work discovery freezes active `34e22814e05f` and a control `fc84d1e1162b`
whose only source change is the existing `Arm::control` entrypoint token. Against pinned
`finite-counterprotocol@4bbd29e36987` on Lost World A, both normal-horizon games end at R477
with 1,008 submitted gems and no subject fault, exhaustion, error or stall. The active game
matches the old eight canonical fields and seven archived payloads exactly. Both arms' whole
archives, stdout and fuel streams also match despite distinct verified input snapshots/binaries.
The unchanged from-initial comparator separately proves 11,002 equal exposed-state/timeline
frames through the fixed Tavi R65 owner-start, before that turn's action. It does not reveal
private campaign state, prove a trigger was absent, or attribute a lock-for-labor exchange.
The opponent's 145 exhaustions limit this one-map/one-side exploration. Park this case; reopen
only with independent evidence of a useful admission opportunity or changed applicability,
without extending this cutoff or replacing the doctrine. The [record](../../results/population/lock-work-discovery/assessment.json)
retains the input audit, baseline bridge, prelaunch measurement-gate corrections and exact scope.

The 22 October Banked Vanguard exploration stops at a physical role/payout linkage gap.
Exact source `9b2141517714`, retained Great Wall A, proves Prism's own R128-129 tribute:
bag `6 -> 0`, actual Focus `89 -> 80`, and absolute submitted team gems `0 -> 6`.
Prism remains at Focus 80 in the fixed R174-235 window. The nearby eastern `g_90` reward
goes to an opponent at R216; the later `g_530` payout at R230 gives Marcella and Stellar
seven gems each while Prism and Varg are away. These are physical observations, not proof
of either source being the private support target or of a causal recipient handoff.
Ordinary banking can also reduce Focus. The initialized production caller/control history
and executed support-to-payout link remain unresolved; no doctrine, threshold or runtime
repair follows from this episode, and no claim is made about other episodes. The
[report](../../results/population/vanguard-exploration/summary.json) preserves the original
survey, the explicit two-window measurement amendment, all five native invocations and
3,526 matching native personal-vision membership checks. Source is unchanged; the retained
game's five exhaustion events still block health and admission.

The 22 October Twin Deadlines exploration stops at a precise opportunity gap, without a source
repair. Exact source `02915ba8acbb` has both loaded collectors personally visible and adjacent
through all four R384-387 staging rounds in one retained Collision B episode, but each sees only
altar `(27,20)`; `(39,20)` remains hidden. Both start tribute at R392 beside the first altar,
complete their own banks, then collect and bank again. This preserves productive native
continuation but does not demonstrate Twin activation or a native matched control. The existing
design fixture, extended to 132 checks, separately proves both conditional policy roles retain
their deadline, release after their own bank and reject an invisible other altar. Caller
preemption, actual route certificates and private campaign state are not inferred from the replay.
Reopen when a reachable personally observable two-altar staging condition is evidenced; do not
change the commitment or start shared-runtime repair merely to make this episode activate.
All six suite cases pass. Historical health flags and missing full-pool qualification remain.
The [result](../../results/population/twin-exploration/result.json) records the rejected first
geometry interpretation, corrected native queries, source hashes and limits. Corrected personal
visibility agrees with 250 native managed-bitboard membership checks; canonical channel queries
also check the selected actor against native headers, and both observation windows complete.
An independent fixture review rejected the first renewal premise: it gave positive travel to a
collector still standing at its altar approach. The corrected case first refuses that zero-travel
nine-turn wait, then retains the same policy instance while restoring a fresh conditional staging
World, including actor positions, routes and its last own-bank fact. This is a policy-model check,
not a native collection or joint-reachability claim. Old reports and native files remain immutable;
the separate [fixture correction](../../results/population/twin-exploration/fixture-correction.json)
supersedes only the earlier renewal assertion. The native zero-of-eight opportunity gap is unchanged.

The 23 October activation trial keeps both new knobs **OFF**. Decoupled's
`P9_DECOUPLED_INTEGER_RATIOS=1` uses exact integer cross-products instead of repeated
software extended-precision divisions. Its reward ordering, zero-gem infinity and ID ties are
unchanged over SDK reward widths (625 boundary comparisons plus 75 existing design checks).
Both default raw and injected modules exactly match the retained incumbent. Current Starfish
against frozen Fundamentals changes retained subject exhaustion entries from 251/392 to zero,
and own submitted gems from 8/19 to 187/166. All five actors resume repeated movement and income.
Against frozen main-v28, the same repair changes own submitted gems 0/8 to 76/12 with clean ON
health. This covers Starfish, both sides, and those two frozen opponents; it is not a strength claim.

The full 40-map, both-side incumbent/ON comparison retains all 160 games. ON removes exactly
those 643 Starfish exhaustion entries, but retains 80 entries across sixteen other map-sides
and Yellow Brick Road's 20/751; the latter B context remains incomplete under the unchanged
strict runner. The frozen opponent stalls on Islands. Nine other canonical outcomes
also change as cheaper policy work reaches existing fuel gates differently. No default promotion
or full health qualification follows. Collision's paid capital is physical: Tavi buys item 16
(Durability Health) at R329 A / R101 B, and keeps it while working. Marcella's separate native
tributes at R535 A / R363 B bank 15/4 gems. The inspected A support episode instead clears a
source later paid to Varg; it does not establish the intended Tavi-to-Marcella source handoff.
Resolve that recipient linkage separately from the repaired arithmetic bottleneck. Inputs,
controls, exact OFF proofs, full-pool failures and native receipts are retained in the
[activation record](../../results/population/commitments-activation/index.json).

`P9_NAV_BOUNDED_PREPARE=1` is a separate default-OFF cost repair. Native post-return probes
bind Ambush's R1 suspension to the complete opening catalog and Yellow Brick Road's R21-26
suspensions to source pricing. The opt-in retains every original spawn and resumes immutable
catalog, boundary and Dijkstra work under the existing fuel floors. The full suite passes;
7,018 opt-in checks include 320 interrupted calculations compared with complete pricing.
All 16 defaults plus the prior ratio opt-in reproduce both incumbent module roles exactly.
In 24 full games (three maps, both sides, active/control, repair OFF/ON), Ambush active loses
its 5/5 startup exhaustions and banks 339/317 versus 269/317. Yellow Brick Road A loses its
20 startup exhaustions and banks 1,007, but the frozen opponent banks zero in that changed
context. B loses its opening suspensions yet retains 76 observation exhaustions; Tavi/Varg
earn nothing and remain physically unchanged for 1,801/1,719 alive owner turns. No broad health,
population activation or promotion follows. The next cost obstruction is mandatory own bank
and group valuation after complete personal camp/threat observation.

Collision active remains productive at 130/223 banked, with all three actors repeatedly
collecting and banking against a productive frozen opponent. Native inventory and attacks
confirm paid Tavi support, while the matching control lacks that purchase and those attacks.
The first reward after Tavi's R377 group-0 clear goes to **enemy** Varg at R428; Marcella's
separate 15/4-gem bank receipts cannot prove the intended handoff. Preserve the recipient
requirement. Exact inputs, per-actor failures, controls and native receipts are in the
[startup repair record](../../results/population/commitments-startup-repair/index.json).

The 25 October old-engine late-observation discriminator is closed without a repair.
On Yellow Brick Road against frozen Fundamentals, the configured integer/startup repair
banks 1,007/936 gems, with 0/76 exhaustion events. Existing broad navigation tracing changes
those histories to 479/1,010 gems and 5/14 events. Its first B crossing brackets mandatory
own-bank/group valuation (actor8 R53), while the baseline's first retained native event is
Varg7 R45. The trace's complete native RAW replay matches its fastrun replay bytes, but that
does not make its private timing the baseline's. Baseline native classification correctly
refuses a capped context (32 kept, 44 omitted); this check was not weakened. No released-exit
crossing was recorded, so no BFS guard patch is justified. A deferred packet enabled only
for baseline Varg7 R45, with unchanged public chronology through that owner-start and
per-bank/per-group cuts, is the next discriminating question. No new bot source, defaults,
Main floor or contribution qualification follows. The game-update freeze supersedes new
execution of that proposed probe; revalidate the bundle first. Compact inputs, rows and
scope limits are in the [old-epoch record](../../results/population/commitments-startup-repair/late-observe.json).

`P9_NAV_PRICING_RESUME=1` separately enables only that existing complete immutable pricing
pipeline; it defaults OFF and leaves opening catalog construction unchanged. A fixed RAW
banked-vanguard Great Wall A default/diagnostic pair identifies the actual R27-to-R28 crossing
as `source-price`, native group 2 (20 members), after retained field construction. The new
switch resumes every goal, bank boundary and Dijkstra settlement with the existing 65k guard.
All 16 default raw/injected bodies and the retained bounded-prepare recipe remain byte exact
when it is OFF. The full suite passes, and each pricing entry point passes 7,018 checks.

Eight same-run RAW Great Wall games cover active/control, OFF/ON, both sides. Each OFF game
has five R27 exhaustions; all four ON games are healthy. Active banks change 322/217 to
280/231, so this is a mechanical health repair, not a strength gain. All ten active ON actors
receive real payouts, bank repeatedly, then receive another payout after their first bank
against productive frozen Fundamentals. Prism's whole-six-gem banks on both sides complete
at R132; native Focus changes 89 to 80 with unchanged inventory. The separate Ambush A
round-1 failure remains with either setting. No claim joins this with Yellow Brick Road's
late observation cause, admits the other 15 ON policies, or proves a contested collector
handoff. No full-pool or fresh qualification and no default promotion follows. Exact inputs,
native work/renewal, controls and physical receipts are in the
[warm pricing record](../../results/population/commitments-warm-phase-repair/index.json).

`P9_NAV_ENTRY_RESUME=1` is a default-OFF negative experiment for banked-vanguard's unpaid
Ambush opening. A single deferred `P9_NAV_ENTRY_TRACE` packet prints only after catalog work;
its first traced crossing is group-4 release recording, phase 7, R1 to R2 with 1,117 fuel
before and 99,571 after. Terrain, storage, layout, mask, union and candidate cuts precede it.
Sampling can move the precise untraced instruction; this identifies cumulative catalog work,
not an isolated terrain call. The entry switch reuses the complete bounded catalog/member
builder with the existing 75k guard. Every group/member and ordinary bootstrap rule remains;
pricing is held ON, without weakening safety or dropping catalog data.

The same-key RAW Ambush pairs on both sides remove all 5/5 R1 exhaustions, yet both still
fail native health with `stall`: no own clear, earned Gold, collected gem or bank occurs.
Deaths stay 11/2 against productive frozen Fundamentals banking 658/241. All ten actors'
20,000 paired owner-start samples (position, HP, Gold and gems) are identical OFF/ON; first
movement remains R25, despite 1,738-1,967 position changes each. Prepared-source logs remain
10:10 in both variants; this count is distinct from the map's 12 public groups. Entry fuel
removal is therefore not useful paid activation in this context. Keep it OFF and investigate
an actual ordinary-source admission/completion obstruction without weakening this card.
The 34 current-default/retained-pricing raw and injected comparisons remain exact, the full
suite passes, and no further repair, broader qualification or promotion follows. Source epochs,
the packet's limits and the decisive negative are in the
[Ambush entry record](../../results/population/commitments-ambush-entry/index.json).

The first natural Prism approach on that retained Ambush A input is not a
certified safe work opportunity. At R108 `(41,45)`, Prism is fully ready and sees
all three live gold-cluster members of group4, but also four rivals. Visible Tavi
and Vesper alone contribute at least105 incoming at the group; with360 camp HP,
Prism's35 Strength and700 HP, the unchanged full-clear admission fails even before
crediting enemy spells. Only that group is personally visible through R108–115.
A source-bound host check compiles the actual policy, independent-selection and
dispatch bodies: under optimistic routes/resources and all13 exploration-target
alternatives it still dispatches inspection, never farming. It does not execute
`Runtime::observe`, reconstruct private native decisions or prove later refusals
correct. No shared repair follows from this context. Reopen work activation only
with a personally supported safe opportunity, preserving the policies and bank
objective. Native inputs, fixture seams and the narrow negative are retained in
`.cache/arena/orchestration/mass-activation-next/commitments-work-transition/`.

Forbearance's `P9_FORBEARANCE_RENEWAL=1` is a rejected experiment. Collision A Varg really waits
at `(13,12)` for R220-266 while personally seeing his home source respawn normally. Temporary
safe own-side work removes that wait and produces a capture, but shifts Tavi and Varg into
409/261-turn loaded bank stalls with four actual gems. The unchanged safety-aware bank executor
reports known routes and failed movement; no safe alternative exit was proved. Keep renewal OFF
and investigate a witnessed safe loaded exit before revisiting it. Active/control games on both
sides are technically clean; that does not clear this behavioral inactivity.

Open problems:
- Production under camp and champion pressure is the main measured weakness: deaths concentrate
  on 00swirl (Willow camp deaths) and loaded actors stall near altars.
- 11 of 16 bots are unplayed. Play both sides and confirm each trigger fires.
- Idle stall (found by reading code; not yet seen in a replay). forbearance-frontier (home
  group), lock-work-exchange (after its lock) and capped-contest-investment (after switching)
  keep sending `farm` to one fixed group without a respawning check. The runtime skips `farm` on
  a respawning group, so the actor idles while it is visible, and on a spawn tile it blocks the
  respawn. The fix changes gameplay: make it per card and re-measure.
- spent-response-exchange, critical-mass-conversion and counterclaim may almost never fire.
  Count their triggers before tuning them.
- The rosters predate engine 1.0.2, which gave Prism 1.5 Focus per gem and Ren a 0.75 stun
  multiplier, nerfed Stellar and Heart, and buffed Tavi, Vendetta and Vesper.

Next ideas:
- Prepaid-column loaded-capture exit: reviewed, 48 fixture checks pass, never applied. Patch:
  `leads/prepaid-column-loaded-exit.patch` (written against the old `bots/p9-*` paths).
- Burst-aware kill screen: the strength-only `favorable_rival` misses kills that a ready spell
  plus basic attack can make. A Prism variant made no kills, so pin the decision in fixtures first.
- Kits: a second Jungle Health versus a first Durability Health at equal cost for capture-fork,
  banked-vanguard, prepaid-column, committed-capacity, forbearance-frontier and
  critical-mass-conversion; later, Power/Focus for caster drafts.
- Bootstrap from unseen harmless gold groups (maps have 4-6 pure gold groups). Price recall and
  heal trips before the first payout in partial-clear profiles.
- Make the arm a knob (`constexpr auto ARM = p9::live::Arm::active;`) so arena overrides can run
  controls.

Measured and rejected (do not retry unchanged): a global contested-capture filter (kept only as
banked-vanguard's opt-in), a preparatory Willow heal (it regressed 00collision; no map gating),
Tavi gold capital from outbound service, a Prism ready-bounty gate, roster retests, model/probe
gates and fork-staging changes.

Idea bank (unimplemented):
- Kill-credit arrangement: helpers hold the last hit when Vesper's kill is safe.
- Spent-response capture windows, beyond loaded conversion.
- Branch-local exit for cross-type-fork: the unchallenged branch finishes.
- Cassi/Issac linked-death and loot accounting.
- Conversion windows timed by the alternating starting team (threats now assume CD<=1, range+1).
