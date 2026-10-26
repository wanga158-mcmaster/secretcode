# Doctrines

Economic organizations: each bot is one doctrine for turning production into permanent score
in the finite race, anchored on the champion whose mechanic makes that doctrine work.

## Doctrine

**Question.** Which organization of production, personal capital and conversion wins the score
race? When to bank versus keep a bag, whose rewards to concentrate, whether to reinvest in the
field or at spawn, and whether to harvest finite stock or develop a renewable economy.

**Shared hypothesis.** Value exists only through complete cycles: capture, whole-bag tribute and
return to productive work. Carried gems, gold and items score nothing until converted, so a
doctrine helps only if its mechanism changes a complete cycle compared with the same actors'
ordinary alternative. Every stock is self-observed: gold is self-only, a bag needs vision, there
are no transfers, no live shared memory and no replay inputs.

**Held fixed** (shared modules, built by every bot):
- `common/src/observe.h`: draft request and private observation schema; unknowns are tagged, never zero.
- `common/src/accounting.h`: event schedules (capture shares, whole-bag tributes, purchases) under
  three finish scenarios; score is credited only at completed tributes.
- `common/src/execute.h`: the stage machine (bootstrap, travel, assemble, clear, hold, allocate,
  bank, recover, shop, renew), legality, safe channel and hold priority, and `turn_boundary`
  error catching.
- `common/src/card-life.h`: telemetry for one purchased project's milestones (purchase,
  production, payout, bank intent, renewal), printed as `P5 card-life` lines.
- `reference/src/runtime.h`: the native SDK runtime: draft from the full twelve-champion pool
  with a required anchor, cached routes, fresh snapshots, typed legal dispatch, evidence of own
  captures, banks and purchases from actual stock changes, fuel guards, and the reference
  independent production that every companion runs.

**Varied per bot.** Only the anchor champion runs the doctrine: `<bot>/src/policy.h` (SDK-free
core) bound to the SDK by `<bot>/src/native.h`, with `profile.h` naming the required anchor and
the companion role weights. Each `native.h` also keeps a matched control arm (the `Arm` enum)
that removes exactly that mechanism while keeping roster, menu and executor.

**Neighbours.** `decision` fixes the job menu and varies the decision rule; `models` varies the
predictive model; `distributed` fixes the economy and varies who must witness what to cooperate;
`territory` varies where cycles run; `events` values choices in event time; `commitments` varies
how our physical state changes the opponent's contest; `main/main` develops the v28
line. Doctrines varies the economic organization itself. A new bot here must change who owns
output, when capital is held or converted, or where it is reinvested; a better router, model or
threshold belongs to another suite.

## Bots

| Bot | Decision rule that must make it play differently | State |
| --- | --- | --- |
| reference | No doctrine. Each actor produces, banks the whole bag and returns to certified productive work; buys nothing. Every doctrine's companions run this. | Charges visible threats conservatively and can reject useful work; every bot inherits this for its companions. |
| dividend | Marcella banks early when her own tribute income makes an affordable shard (Monster Strength, Jungle Health or Monster Focus) buyable and that shard makes the next cycle faster or feasible; she buys at spawn, then returns to the better route. Control `route_first_control` picks the route before any tribute-funded purchase. | Last measured before the restructure: lost 77-1009 to Fundamentals at 200k fuel per turn (default 100k is unmeasured). In that game Marcella captured 11 gems at R594 and still carried them unbanked at R979. |
| endowment | Prism keeps carried gems when their Focus makes an otherwise unsafe group safe and a full liquidation and rebuild beats banking now; the whole bag is liquidated for a win, a deadline or danger. Control `bank_first_control` banks after each payout. | Builds in Release; no native game recorded, so first income and the retention choice are unverified. Rejects unnormalized equipment passives. |
| workshop | Tavi picks route and field purchase together: she may take a poorer first group whose own payout funds an in-field shard that unlocks a better successor without a spawn detour. Control `route_first_control` picks the route, then the purchase. | Builds in Release; no native game recorded. |
| collateral | A naturally injured Heart of Ruin keeps farming under his own shield, using missing-health damage, only when health plus shield certify the whole next cycle and its escape; otherwise he recovers. He never takes damage on purpose. Control `recovery_control` recovers under the common rule. | Historical exact-source evidence covers 38 games/19 maps; the shield commitment and complete native cycles remain unverified. The supplied-caller partial-production/refusal result is recorded below. |
| runoff | Finite harvest: Marcella sweeps two finite supplies, liquidates the whole bag before a development economy matures, then keeps a short productive loop. Control `bank_each_control` banks after each capture. | Last measured before the restructure: preparation of the 24-candidate catalogue (four exact units per owner turn) delays the first move to about R845 at default fuel; lost 88-1020 to Fundamentals with zero gems. |

Collateral's unchanged `a871ca0c0609` C++ caller was exercised with supplied SDK 3.0.5/runtime 1.0.3 catalogue data ([result and independent review](../../results/population/current-panels/index.json)). Active dispatched a self-shield, consumed its next-owner receipt, attacked the gem and moved to `(4,3)`; at R104 the escape certificate refused before a wolf basic despite shield `110` with duration `2`. Control recalled. Bag, gold and capture counts stayed zero, so no productive cycle was demonstrated. This finishes the conditional partial-production/refusal inquiry while preserving the missing-health/shield commitment. The 1M host allowance establishes no native 100k fuel/health result. The next question is native reachability of this conjunction and a complete cycle under matched control.

The current native bank-exit repair is experimental: `PLAN5_BANK_EXIT=1` in
`reference/src/runtime.h` sends an already owned output-bank payout directly toward its
committed altar and routes to the full queried tribute-range mask. Occupied bootstrap or
altar entry tiles no longer erase the only navigation target. Producer, bag ownership,
bank timing, purchases, complete-cycle safety and phase deadlines stay with each existing
controller. The default is `0`; all six consumers preserve both incumbent module roles.
The SDK-host regression blocks both old entry tiles and checks the actual typed vacate,
unchanged stock/plan and channel priority.

[Native evidence](../../results/population/doctrines-bank-exit/index.json) covers matched
default/repair full games against frozen Fundamentals on Road of Riches and Gem Fields,
both sides, with injected modules. On Gem Fields the repaired Runoff companions complete
repeated personal attack/capture/whole-bank cycles: Vendetta/A attacks at R176–177,
captures seven at R184, banks at R326, then attacks another source at R828–829, captures
at R836 and banks at R882. The side-B counterpart also completes two such cycles.
The four Gem Fields games have no technical flags. Runoff's Marcella remains inactive
there, and Road of Riches still has allied chokepoint stalls; this repairs ordinary
productive capability, not the finite-harvest mechanism's admission. No default promotion.

The same investigation rejects the old preparation-batching lead for Hooks: the current
default finishes all 24 quotes by R26 and rejects every complete plan. Native positions
also show existing bootstrap discovery already moves four of five actors. An extra
discovery-only arm produced no camp damage or income and increased exposure; it was
withdrawn. A directly observed harmless-group renewal veto at R877 had 1,200 HP and
110 damage per route tick, so that safety bound was retained. Next: resolve native
anchor reachability and remaining occupied corridors without weakening complete-cycle
safety; broad pool health and doctrine/control differentiation remain open.

Workshop's experimental `PLAN5_WORKSHOP_BOOTSTRAP=1` skips the expensive card
snapshot only when a fresh personal-visibility upper bound contains fewer than
three gem-bearing sources. Its existing three-source project cannot pass in
that case, so the same ordinary Reference fallback runs directly with enough
fuel to quote work. With three potential sources, the complete original packet,
financing menu and safety certification remain mandatory. Both new knobs default
off; `PLAN5_WORKSHOP_TRACE=1` records bounded post-decision veto facts.

[Native evidence](../../results/population/workshop-bootstrap/index.json) identifies
the old GreatWall A veto at R37/985/1933: one usable source, no Reference quote
executed, then a low-fuel yield. All sixteen raw/injected comparisons preserve
the six current defaults and existing Dividend/Runoff bank-exit recipes. In
paired full games on GreatWall and Gem Fields, both sides, the bootstrap repair
lets Tavi personally produce and bank: GreatWall A attacks at R222–227, captures
14 gems at R234, banks all 14 at R302 and attacks again at R1276–1281. All eight
paired games have clean technical health; the full fixture suite passes 18/18.
This does not activate Workshop financing: all tested Tavi actors still earn
zero gold and buy nothing, GreatWall B only moves, and Gem Fields A loses its
captured bag on death. The source catalogue drops gold-only groups before the
card's menu; that funding-menu gap is next, with complete project safety and
personal payout ownership retained. No full-pool qualification or promotion.

The [capital observation check](../../results/population/workshop-capital-observation/index.json)
narrows that next step. Gem Fields has no gold-only group; GreatWall's two
40-gold groups are never personally visible at any of Tavi's 7,507 owner-start
samples in the four saved default/bootstrap games. The fixed closest healthy
sample, GreatWall A R332–347, still observes neither source. Removing the
catalogue filter alone therefore does not supply a certified financing input
on these keys. No gameplay change or new game was made for this check. Reach a
personally observed capital source, with its route and complete investment tail
certified, before testing the gold-source menu repair. Mixed-reward capital is
a separate safety/model question; unseen gold is not affordable own stock.

## Distinctness rules

- A bot is distinct only if its rule produces a different legal decision (reward ownership,
  coalition, development route, stage schedule or liquidation phase) in some reachable state.
  Two bots that make the same choices count as one: merge them or retire the weaker. A
  coefficient-only change is a tuning of an existing bot, not a new bot.
- Only anchors run doctrine logic, so bots sharing an anchor (dividend and runoff both use
  Marcella) must differ in the anchor's decisions, not in their companions or draft weights.
- An active arm that never disagrees with its control in games is a failed mechanism, not two
  bots. Log the first disagreement before claiming the mechanism ran.
- Doctrine logic stays in `<bot>/src/`. Generic observation, accounting, execution and safety
  fixes go in `common/` or `reference/`. Shared modules have one owner per suite at a time; a
  shared change moves every bot, so rebuild and re-test the whole suite
  (`./arena.cmd build doctrines`, `./arena.cmd test doctrines`) before handing it on.
- Drafting may use the full pool, but each mechanism's anchor champion stays required.
- Fundamentals (`anchors/fundamentals`, Release) is the benchmark, not the target: do not tune
  toward it at the cost of the doctrine; test generality on other opponents, maps and against
  the matched control.
- Borrow ideas from other suites, never code (suites never include each other).

## Layout and checks

```
common/src/      observe.h accounting.h execute.h card-life.h    shared, SDK-free
reference/src/   runtime.h (native runtime), main.cpp (baseline bot)
<bot>/src/       profile.h policy.h native.h main.cpp            dividend adds basic-quotes.h;
                                                                 runoff adds preparation.h receipts.h
tests/           plan5-<case>.cpp WASI fixtures, run.py
```

- Build: `./arena.cmd build doctrines` (content-hash snapshots; `--in-place` builds the working dir).
- Test: `./arena.cmd test doctrines`, or `./arena.cmd test doctrines -- [bot|module|case ...]`
  (`--list` shows the cases). A bot name runs its own fixtures plus the shared `common/` ones;
  `reference` runs the two SDK-host fixtures that exercise `runtime.h` (`inspection-route`
  through dividend's `native.h`, `runoff-native` through runoff's); `common` runs everything.
  Pure fixtures use the `tools/arena/wasi.py` defaults; SDK-host fixtures need
  `-fno-access-control` and no wasm exception runtime (they stub `__cxa_throw` to abort).
- Smoke game: `./arena.cmd play doctrines/<bot> anchors/starter-cpp --maps 00collision --sides A --rounds 300`.
- Control arm game: a scratch bot dir (e.g. `bots/doctrines/_dividend-control/src/main.cpp`; the
  leading `_` keeps it out of bulk selectors and the ladder)
  with `#include "../../dividend/src/native.h"` and
  `int main(){return p5::dividend::native_binding::run(p5::dividend::Arm::route_first_control);}`.
  Move it out after the comparison; it is not a distinct bot.
- Logs: a caught engine error prints `P5 UNEXPECTED_ENGINE_ERROR ...` and the turn yields; every
  telemetry line starts with `P5 `. Include that pattern in any error scan of these bots.
- The native wasm stack is 64 KiB: keep large planning payloads in `constinit` static scratch
  (as dividend and workshop do), never on the stack.

## Leads

Open problems, most urgent first:
1. Reach first income at default fuel. Runoff: batch exact quotes per owner (validate the fresh
   immutable packet once, keep horizon and current-owner checks between ordered quotes) and
   replace the hard four-unit ceiling with a catalogue-sized one. Dividend: measure the complete
   menu at 100k fuel.
2. Persistent bank ownership: after any capture some controller must own the bag's liquidation
   (dividend's 11-gem bag stayed unbanked for about 400 rounds).
3. First native games for endowment and workshop: confirm the anchor reaches the state where
   active and control disagree, then the downstream bank and renewal. For Collateral, establish
   native mechanism reachability and a matched complete cycle through bank and renewal.
4. The reference companions buy nothing and avoid visible threats conservatively; they set the
   floor for every bot. Improving them is a shared change (see the rules above).
5. Expose each bot's `Arm` as an arena knob (a `constexpr` in `main.cpp`) so control arms build
   through overrides instead of temporary bot dirs; behaviour of the active arm must not change.
6. A demonstrated mechanism is not a benefit: compare active against control from the first
   disagreement, with complete outcomes on both sides.

Engine facts worth keeping: queried champion stats include flat item growth but not completed
emblem-family passives; camp monsters struck nearer melee actors before a farther ranged actor
that was in range.

Idea bank: unimplemented doctrines (their offline cores were retired with the restructure).
Reviving one needs a `profile.h`/`policy.h` core, a `native.h` binding on
`reference/src/runtime.h` (workshop's is about 300 lines) and a two-line `main.cpp`.
- compound: funnel a jointly cleared camp's rewards to Vesper until his amplified income crosses a capacity threshold, then release the helper on a visible role change (control splits rewards).
- consortium: the smallest complementary coalition of visible actors unlocks Golem, Monolith or mixed camps no member should attempt alone; helpers leave after a safe handoff (control: independent cycles).
- patronage: pipelined specialization; workers clear the next group while Marcella holds, banks and shops (control waits for her return).
- federation: several minimum-viable producers each own a draft-partitioned sector (bipartite startup matching) and take over idle sectors on evidence (control concentrates).
- ladder: invest a payout only if it advances the next ordered capability threshold (safe mixed, then Golem, then a major group) within the finish window; otherwise convert (control buys greedily).
- tollhouse: Ren defends Marcella's altar corridor and bank windows, optionally buying a lock, when interruptions would block valuable tributes (control: Ren produces).
- privateer: Vendetta chases a nearby visible enemy (at most 4 path steps and 4 turns) only when a provably lethal attack's bounty or theft plus enemy downtime beats the farming cycle (control keeps farming).
- preemption: Varg takes a contested public group before a witnessed enemy arrives when his own cycle plus a capped, evidenced denial term beats a home cycle (control: own yield only).
- countercycle: rotating conversion duty on a 48-round clock, one converter and one escort per 16-round slot, so returns are staggered and protected (control: nearest visible escort).
- twin-engines: two incompatible capital engines on separate circuits, Marcella's short bank-and-reinvest loop and Prism's gem-retention loop (control: Prism banks first).
- labor-surge: the Cassi/Issac linked pair buys extra early labor in one compact area, then shifts to short protected captures as linked-death risk grows (control: independent producers).
