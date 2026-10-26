# Foundation: the shared bank of insight

This is the contenders' canonical foundation under the user's renewed 24 October 2026 mandate.
New findings and corrections belong here. Each bot derives its own controller from this knowledge;
the main bot and main-anchor source are outside our reading boundary. The previous redirect to a
different suite's foundation is superseded. No main-suite source or foundation was read for this
renewal.

The original entry bodies, tools and tables remain available so old evidence and commands resolve.
Their engine 1.0.2 labels remain historical: copying a claim, a matching catalog or a newer document
date does not revalidate it on engine 1.0.3. Historical analyses that describe main internals are not
independent premises for these contenders. Current entries below cite the pinned SDK/rules, native
contender observations, or an explicitly stated deduction; they distinguish those evidence types.

What we understand about the game, built by the contenders' coordinator and its subagents (see
the [contenders charter](../README.md)) and readable by everyone. It holds knowledge, never code that bots
include: each contender derives its own bot from this bank, in its own direction.

## Layers and pages
| Layer | Page | Holds | State |
| --- | --- | --- | --- |
| Mechanics | [mechanics.md](mechanics.md) | What the engine does, measured by probes (rule prose has lagged the engine) | M1-M51 verified (engine 1.0.2, 22 Oct); lab maps and probe runner in `tools/mechanics/` |
| Building blocks | [sdk.md](sdk.md) | How to observe and act cheaply: API facts, fuel costs, pitfalls | S1-S24 verified (engine 1.0.2, 22 Oct) |
| Building blocks | [maps.md](maps.md) | The map pool as data: classes, camps, distances, contested ground | P1-P12 done (22 Oct); `data/maps.json` covers 40 pool + 17 legacy maps |
| Models | [economy.md](economy.md) | Clear times, camp values, map capacity, item economics, the race | E1-E27 done (22 Oct); simulator calibrated on solo probes; schedules in `tools/economy/speedrun.py` |
| Models | [combat.md](combat.md) | Duels, focus fire, stuns and burst, escape, the value of a kill | C1-C21 done (22 Oct; ~4,700 engine games); turn-exact model `tools/combat/duelsim.py` matches 288/288 single-body duels |
| Evidence | [online.md](online.md) | What strong online teams do, quantified, and what that data cannot show | O1-O20 done (22 Oct; 1,126 games, engine 1.0.2); regenerate with `tools/online/all.py` |
| Strategy | [strategy.md](strategy.md) | The game's structural facts, the answers built on them, and how each contender answers the same structure differently | G1-G10 on all layers (22 Oct); G10's safeguards are hypotheses until tested in games |

## Current independent corrections and applications

| Result | Where | Evidence and limit |
| --- | --- | --- |
| Current absolute passive pace is release- and coverage-bound | [E15](economy.md#e15-historical-schedule-gap-and-current-opaque-passive-benchmark) | Opaque RAW v53 reaches 1,000 on all 40 pool maps/both sides: 80 unique keys, event-round median 929/mean 950.4375, independent audit PASS. Passive-only scope; no paired historical improvement, contender strength or private micro inference. |
| A zero-timer shield can absorb camp damage before its owner's start | [M30](mechanics.md#m30-shields-expiry-follows-the-shield-owners-start-timer0-can-still-absorb-before-it) | Exact current-native Varg events retain timer0 absorption twice and later owner-start expiry. Camp zero-stun expiry before its own action is the pinned rule contract, with no direct current-native timer witness here. |
| An exact observed countdown is not an observed physical transition | [M49, M52](mechanics.md#m52-a-zero-respawn-timer-does-not-establish-a-living-group) | Current-native Warband observes `respawning{0}` while captured source tiles remain occupied; departure and a later live observation are separate operations. |
| Ranged walking can enable reset without a preceding basic; reset restores attack only | [M24, M53](mechanics.md#m53-ranged-walk-then-reset-is-a-real-allowance-transition) | Current-native Vesper flags go `111 -> 001 -> 010` for M/A/C. The guard witness contains no basic command and establishes no successful move-reset-hit word. |
| Lost personal sight does not restore a failed recall premise | [M48, M54](mechanics.md#m54-a-failed-recall-is-not-made-safe-by-empty-personal-vision) | Current-native Willow moves out of sight of adjacent Ryth, recalls, takes damage, retries and dies. The trace proves the failure, not a safe alternative. |
| A chase average is not a sufficient escape bound | [C8, C20, C22](combat.md#c22-ordered-burst-invalidates-average-rate-escape-guarantees) | A two-round Varg reset/double-turn counterexample follows from the pinned mechanics; it is a deduction awaiting a seeded native witness. |
| Planning and observation need reachable computation and an executable action tail | [S26](sdk.md#s26-a-bounded-query-loop-still-needs-an-executable-action-tail) | Mosaic's ready no-op windows and Timetable's distinct traced 72,077-fuel gate stall separate actual action availability from liveness. The diagnostic does not reveal the untraced private cause. |
| Work and immediate egress are one ordered operator when movement survives the work | [G11](strategy.md#g11-the-complete-action-word-includes-its-continuation) | R9's exact ordinary/diagnostic replay join establishes Basic -> same-input move -> complete quiet egress -> recovery. The whole B game still has zero economic completion; the native word does not qualify full controller productivity. |
| Actual investment includes the shop, return and renewed service | [E19, E29](economy.md#e29-an-investment-is-an-actual-shop-charged-return-and-renewed-service) | Timetable's audited Ryth chain buys, physically returns, clears/captures/banks and works again; paired passive improvement covers one map/both sides. Nominal handoff timing is distinct from the later useful action. |
| A contact family needs actual economic operators and relevant-horizon coverage | [G12](strategy.md#g12-contact-must-compose-with-the-actual-economic-operator) | Warband's later exact native Move -> priority promotion -> one-tick bank -> restored order and renewed service refines the missing operator. The full two-side v51 comparison remains a loss and economic regression. |
| Work effects change the hazard and the legal continuation | [C23](combat.md#c23-admit-the-complete-word-after-its-effects-change-hazards-and-legal-continuation) | A restricted one-body M/A/C word has at most four successful primitives. A lethal basic can remove an otherwise unaffordable next hit and free a capture tile; this model distinction is not a new native outcome. |
| Future availability needs physical enablement before becoming executable work | [M52, G13](strategy.md#g13-future-availability-is-a-nomination-until-its-physical-enablement-is-resolved) | A repeated absent winner can discard living alternatives across otherwise finite queries. This source/model counterexample is not a proved ordinary private cause. |

The shared lesson is executable continuation. An attack, a legal path, an expected capture time,
unused allowances or a surviving prefix does not establish a complete service. Record the actual
clear, attributed payout, source release, bank, recovery and renewed useful work separately. Current
bot owners keep source-bound implementation/evidence state in their existing `NOTES.md`; these
entries carry only what transfers between them.

Tables and the scripts that regenerate them live in `data/` and `tools/` (never included by a
bot). Probe and duel bots are scratch bots, `bots/contenders/_<name>/` (bulk selectors and the
ladder skip a leading `_`).

## How an entry is written
Each entry has a stable id (M = mechanics, S = sdk, P = maps, E = economy, C = combat, O = online,
G = strategy), so bots' notes can cite it:
```
### E7. One-line claim
Claim: what is true, with numbers.
Evidence: how it was established (probe, engine games, replays, derivation) and where the run,
  script or replay is; coverage (maps, sides, opponents, sample size).
Rests on: entries it depends on (M3, P2).
Status: verified (engine 1.0.2) | measured | model (prediction, with its error) | hypothesis.
Recheck: what would invalidate it (a patch to X, a new map class).
Consequence: the decisions it changes.
```
Wrong entries are corrected or removed (moved under the repository's removal rules), not left to
mislead. A hypothesis is labelled as one. An entry worth most says what it claims, how it was
established, what it rests on and when to recheck it.

Independent primary references: [rules notes](../../../docs/rules-notes.md), the pinned
[rules](../../../docs/reference/GameRules.md), [C++ API](../../../docs/reference/BotAPICpp.md),
[SDK](../../../sdk/cpp/cambc.h), [runtime catalog](../../../docs/reference/runtime-catalog.json) and
[patch notes](../../../docs/reference/updates/). Native retained evidence lives under
[`results/contenders/`](../../../results/contenders/). The main bot is an opaque executable opponent;
its source is unnecessary for measuring our games.
