# adaptive: evidence-gated organizations

**Question.** When an actor's model of the game can be wrong, who may change a commitment
(invest, join, specialise, rebuild, retire, roll back, take over a duty), and on what evidence?

**Shared hypothesis.** A team that changes work only on an actor's *own* completed, comparable
episodes, never on forecasts, public score or a teammate's knowledge, and keeps ordinary
independent production running meanwhile, loses less to unsupported stories than a team that
commits on forecasts. Each bot answers *which own evidence* may change *which commitment*. Its
control arm (`P8_CONTROL=1`) keeps the same observations, legality and safety and swaps only the
evidence rule for a fixed one (commit at once, never switch, never restore); that difference is
the bot's idea.

## Doctrine
Each bot varies only the trigger (which own evidence) and the object that changes (exposure,
membership, gear, geography, duty, a retired subsystem, the order of commitments). Held fixed
for every bot (`common/`, `distributed-common/`):
- **Organizations.** U: independent circuit (own payout, purchases and bag). G: generalist
  fallback. H: supported collector with visible, individually safe helpers. J: joint expedition.
- **Private ledger per actor.** At most 8 predeclared context slots, 4 named interval hypotheses
  per slot, one active episode, saturating counts. A hypothesis is eliminated only by a disjoint
  outcome whose premises the actor personally observed; an empty family is `MODEL_SET_FAILED`.
  Failures and spent probes survive death, recall and epochs. Nobody reads another actor's ledger.
- **Qualification.** Two distinct, non-overlapping completed episodes in one context, expiring
  256 rounds after the latest one or on a material premise change. A qualified *component* (for
  example an own return route) licenses one funded, visible, survival-checked commissioning
  campaign of at most two episodes; the new organization starts unqualified.
- **Failure.** One safety failure suspends the dependent proposal; two contradictions among the
  last three eligible episodes retire the context for the 64-round epoch; three unresolved
  attempts in a row suspend trials.
- **Probes.** At most one per actor per 64-round epoch and two per match, at most 8 owner turns
  of diagnostic detour, empty bag, inside the 120-round forecast window.
- **Coordination.** Visible proposals at deterministic rally pads (assembly fails after 8 turns),
  handoffs at visible safe meetings; no messages or shared memory.
- **Draft.** One conservative complete-cycle comparator over an 8-champion pool (Tavi, Marcella,
  Varg, Vendetta, Vesper Vale, Willow, Prism, Ren), no duplicates, no denial. Per-bot preference
  lists only break ties; roles follow the pick ordinal.

Where this suite ends (borrow ideas, not code):
- `territory` makes geography the mechanism; here geography is an input.
- `commitments` shapes the opponent's contest; here rivals are only observed (bots 15-16 use
  two finite response labels, APPROACH and STAY).
- `distributed` studies cooperation without shared memory; here those adoption rules are fixed.
- `doctrines` fixes one economic organization per bot; here organizations change on evidence.
- `models` and `decision` improve forecasts and decision rules; here a forecast never qualifies
  anything. A hypothesis bank, a visibility reward, min/CVaR scoring, residual timing
  correction or stored history on its own is their idea, not a new adaptive bot.

## Bots
Cards 01-03 are one program (`common/src/runtime.h`, `Runtime<Earned,Cells>`, which includes all
three policies); 04-16 each own `src/native/runtime.h`. Logs are tagged `p8` or `p8-NN`
(`DECLARED`, `ADMIT`, `EPISODE`, `OPPORTUNITY_UNAVAILABLE`, `ERROR`, ...).

Measured (stale: pre-restructure source, 00collision against fundamentals, both sides): every bot
scored 0 and banked nothing. 01-03 barely exhausted fuel (Canary dealt 560 and 675 monster damage;
Earned Center and Banded Cells stalled before admission); 04-16 exhausted it 700-2,200 times per
game, later cut to 3 for Workshop, still without admission. Current sources are unmeasured.
"Attribution" means the bot logs `PROOF_UNAVAILABLE`: with no event reader in the SDK a helper's
attack contribution is unobservable, so its H/J never becomes fully qualified.

| Bot (card) | Decision rule that must make it play differently | State |
| --- | --- | --- |
| canary-and-cashflow (01) | Pick 0 runs an empty-bag frontier campaign alone; only two own completed episodes on its return route let it propose H there and recruit producers, who stay U until then. Control: the crew commits to frontier J at once. | Only bot using the private-admission path; attacks camps but never banked. |
| earned-center (02) | The collector must complete two own capture, tribute, spawn-shop and return episodes before a supported center (H) may finance her later work; a changed route or expiry sends her back to U. Control: the same H from forecasts at once. | Stalls before admission. Its purchase gate (`purchases.h`) is fixture-only, not in the native build. |
| banded-cells (03) | Picks 0 and 1 lead separate, non-identical source/route cells under the same U-to-H gate; a failure retires only that cell's proposal and never summons the other cell. Control: one shared J route. | Blocked without two distinct feasible districts; stalls before admission. |
| reversible-workshop (04) | Exactly two declared builds (D-first clearer U, S-first collector H): two own route/return episodes license one funded spawn rebuild into H; a failed supported cycle or changed premise licenses one rollback; at most two rebuilds per match. Control: retargets but keeps its first gear. | Needs Tavi, the only champion with a pinned full refund. Most fuel-tuned driver, still 3 suspensions per game. |
| staged-venture (05) | Two ordinary-gear cycles over the venture's actual bank/spawn/return leg must precede its S-first purchase and helper commitment; a preliminary that misses the decisive premise leaves the venture unresolved. Control: buys and assembles first. | No card-specific gap recorded. |
| underwritten-frontier (06) | One producer is ring-fenced for the whole match: never a venture input (labor, purchases or rewards) even after qualification; the venture pair expands, retreats and picks its collector (only if its own loaded return survives) on its own evidence. Control: may recruit the insurer. | No card-specific gap recorded. |
| common-cause-firewall (07) | Two dependency compartments, Tavi's solo field-shopping U circuit and a two-actor H circuit with spawn conversion and purchases, adapt only on their own evidence; a failure in one never recruits the other. Control: one uniform J. | Blocked without a disjoint comparable field/spawn pair; the control's shared-J geometry is unsupported. |
| circuitbreaker (08) | A J subsystem is retired after one safety failure, two contradictions among its last three cycles or an empty model; members finish safe obligations and go U; one requalification only after a real change (visible hostile lock plus a completed replacement U route). Control: keeps repairing the same J. | Attribution. |
| rollback-crew (09) | Each producer's qualified incumbent U circuit (exact source, route, role) is stored; one replacement J trial is allowed, and its failure or expiry restores every actor to its own incumbent at its own safe boundary. Control: repairs inside the replacement, never restores. | Needs two qualified incumbents before any trial. |
| assay-guild (10) | One ordinary-production assay measures clearance C and logistics L: C >= L+4 gives joint clearance, L >= C+4 serial specialization (one clears, the other collects and banks), otherwise U; one assay and one role change per match. Control: chooses from nominal C/L. | Attribution. |
| requalification-reserve (11) | A retired coupled context may be retried once, only after a named real change (own equipment crossing a declared boundary, a personally seen replacement route, a different visible helper set), never on a timer; old failures stay and no qualification transfers. Control: retirement is permanent. | Attribution; the changed-membership path needs five picks. |
| custodial-authority (12) | When the custodian loses its observation or return premise, the next collector/region duty passes at a visible safe meeting to an actor that personally qualified for it; bags stay with their owners and the former custodian takes an independent circuit. Control: the custodian keeps the duty. | No card-specific gap recorded. |
| disagreement-partnership (13) | Two explicit route-duration models disagree on U versus supported H; one bounded productive distinguishing stage decides, and an overlapping or unknown result keeps U. Control: commits to the nominally better model at once. | Attribution. |
| abstaining-specialists (14) | Prism starts generalist; loaded-carry specialization (purchases and helper dependence on its bag) is adopted only after a qualified carry trial and dropped wholesale on an empty model, a changed bag/Focus regime or unresolved returns. Control: keeps the specialization whenever feasible. | Needs Prism at pick 0. |
| finite-counterprotocol (15) | Passive encounters only: two matching APPROACH responses (the same visible rival closes static distance over the next three own observations) at an altar approach license a visible convoy H; STAY keeps U; contradiction, unknown or 64-round expiry stops convoys. Control: logs the same responses, stays U. | Attribution; no protection or deterrence benefit shown. |
| probe-reveal-commit (16) | One empty-bag productive approach to a named low-load frontier elicits a response: APPROACH leads to a conservative H conversion, observed non-approach to U, unknown to G. Control: deploys directly without the probe. | Attribution; the paid (non-productive) probe is disabled because its minimum branch loses. |

## Distinctness rules
- A bot is distinct only if, in some reachable state, it makes a different physical choice (who
  works where, who joins, what is bought, who collects or holds) than every other bot. Two bots
  that make the same choices count as one: merge them or redesign one.
- A change that only alters a counter, delay, threshold or estimator merges cards. A change that
  makes active and control behave the same erases the card. Removing a gate to get income turns
  a card into a generic farmer, which is the convergence this suite exists to avoid.
- Watch the collision pairs: 01/06 (novel-frontier exposure vs a permanent exposure boundary),
  03/07 (same gate in different contexts vs different dependency structures in comparable ones),
  05/13 (staging without vs with model disagreement), 08/09 (retire without a proven
  alternative vs restore a demonstrated incumbent), 15/16 (passive vs bounded intervention).
- One owner per suite at a time for shared code: `common/`, `distributed-common/` and the shared
  headers in bot directories listed below. A shared change affects every bot that builds it and
  is re-tested suite-wide (`arena build adaptive`, `arena test adaptive`). Card work stays in the
  card's own files.

## Layout and checks
- Card files: `policy.h` (gate, organizations), `profile.h` (bounds, draft preferences), `draft.h`
  (roles by pick ordinal), `native/{runtime,configuration}.h` (driver, arm, SDK glue), extras.
- `common/src/`: `profile.h` (pool, caps), `context.h`/`episode.h` (ledger, qualification),
  `organization.h` (U/G/H/J physical work), `policy.h`, `draft.h`, `routes.h`/`candidates.h`/
  `coalition.h` (public routes, candidate cycles), `sdk.h`/`sdk_shop.h` (SDK boundary, typed shop),
  `forecast_probe.h`, `native_economy.h`, `*_storage.h`, `route_*.h` (fuel-bounded forecasts),
  `runtime.h` (cards 01-03).
- `distributed-common/src/{contract.h,observe.h}`: adaptive's own copy of the distributed suite's
  legal observer and survival/economic primitives (namespace `p6`). Suites never include each
  other; this copy is edited here as a shared module and is not synced with `bots/distributed`.
- Shared headers in bot directories: the 01-03 policies, profiles and drafts (one program);
  `earned-center/src/trace.h` and its `episode.h`/`organization.h` forwarders (15 bots, all but
  assay-guild); `reversible-workshop/src/{transactions,profile}.h` via `common/src/sdk_shop.h` (04,
  05, 07, 11, 14); `circuitbreaker/src/trace.h` (also 09); `finite-counterprotocol/src/` policy,
  profile, response, convoy and `native/{configuration,distance}.h` (also 16).
- Build: `./arena.cmd build adaptive` (16 release snapshots). The suite has no tools of its own.
- Tests: `./arena.cmd test adaptive` runs `tests/run.py`: `layout` (every include stays in this
  suite or the SDK), `control-knob` and 30 WASI fixtures (`tests/plan8-*.cpp`); about 25 s.
  `run.py <bot>` runs the fixtures that include headers that bot compiles; `--list` shows the map.
  `context-geometry` is compile-only: a namespace-scope initializer calls the SDK at startup.
- Control arm: the arena knob `P8_CONTROL=1` (`core.apply_overrides`, e.g.
  `prepare(bot, {"P8_CONTROL": "1"})`), equivalent to `#define P8_CONTROL 1` before `main.cpp`'s
  first include. `control-knob` checks that the knob edits the first definition reached (identical guarded defaults elsewhere are edited too).

## Leads
Open problems, most blocking first:
1. **First income.** No bot banks. Find the cheapest productive ordinary U cycle for each card
   that still passes through its own gate; verify bank and renewed work before tuning gates.
   A current-engine negative for Earned Center is retained in
   [adaptive-earned-admission](../../results/population/adaptive-earned-admission/index.json).
   On Gem Fields, both sides against frozen Fundamentals, incumbent `f2d1882b43f3` and the final
   opt-in queue/packet trial all dealt zero monster damage and collected/banked zero gems.
   The A incumbent's feasible U model stops after queue validation below the unchanged 25k
   admission reserve. Exact current-allied-observation reuse saves about 2.8k but does not admit;
   a zero-delay solo queue specialization reaches attachment, then current-packet reuse reaches
   the admission decision and exhausts fuel across rounds (A: one, B: four suspensions). The
   incumbent B draft also exhausts once; its native statistics are retained even though the
   compiled runner cannot classify a draft-owned health event. All gameplay changes were
   restored; no completed episode or earned adaptation was observed. Next isolate the complete
   `earned_center::admit_current` decision/commit cost on a fresh owner turn, with exact current
   observations and the same safety/evidence requirements, before another arithmetic-cache trial.
   Canary's separate current-engine inspection negative is retained in
   [adaptive-canary-inspection](../../results/population/adaptive-canary-inspection/index.json).
   Great Wall B Varg7 repeatedly inspects the nearest wolf source and recalls: its warm current
   models at rounds 131, 147 and 163 reject that source as unsafe with about 49k fuel remaining.
   This is a genuine source refusal, before admission, rather than an idle admitted executor.
   A bounded distinct-source inspection trial, with the same eight-turn limit, still performs
   no resource work. Its Great Wall actors visit more positions, but Varg sees neither tested
   alternative source in rounds 1-400. The trial adds 19/2 fuel suspensions on A/B; using the
   existing equivalent owned forecast reduces these to 16/1 without admitting work. Ambush
   A/B remains unproductive with or without that combined trial. All 15 full games (three
   diagnostic, twelve paired) have productive frozen Fundamentals opposition, zero Canary
   monster damage, income, captures, banked gems, admissions and episodes. All gameplay edits
   were restored; each trial's OFF raw and injected bodies matched all sixteen incumbents.
   Next establish a safe, personally observable alternative within the inspection limit and
   bound its inspection/forecast fuel before changing admission or evidence gates.
   [Personal ward evidence](../../results/population/adaptive-personal-ward/index.json) shows
   the opt-in `P8_CANARY_WARD_INSPECTION=1` establishes that observation on Great Wall:
   designated independent Varg places one private ward on inspection turn 7, after paying its
   own probe slot. B places at (30,31) from (28,33); fresh personal vision reveals source14,
   native ward-placement receipts agree, charge goes 1 to 0 and cooldown 0 to 10. A succeeds
   at the mirrored source13. A six-tile staging route and a currently legal decreasing-distance
   alternative step avoid the wall detour and occupied first step; the unchanged eight-turn
   limit remains binding. The ward's hypothetical mask never supplies live camp state.
   This is observation only: the unchanged first fresh model returns `unknown_input` before
   completing its request (zero members/legs, about 18k fuel remaining), and both full games
   still have zero admission, resource work, payout, bank or renewal. All twelve same-run
   paired full games are free of faults, error lines and fuel exhaustion; Fundamentals banks
   in every game. The knob remains OFF, with all sixteen raw/injected defaults byte-identical.
   Stop this branch at the now-observed model-preparation boundary; do not count a ward as an
   earned episode, relax the model/admission gates, or extend probe limits to obtain income.
   [First-input evidence](../../results/population/adaptive-first-missing-input/index.json)
   binds the default-OFF `P8_SOLO_INPUT_STATUS=1` diagnostic's first missing input on
   that same B ward case. A deferred round36 packet identifies the round35 post-route fuel
   guard: 18,565 remains against the unchanged 20k floor. The personally observed source is
   visible, living and uncontested; all fields and routes validate. Groups/members/legs remain
   zero because request assembly has not begun. All five actors' 175 owner-start state samples
   through round35 exactly match the retained ward-only case; no hidden-member refusal is shown.
   Its actual route counts 36/2/46/46 imply a 134-turn walking lower bound including the
   declared collision slack, above `plan_valid`'s unchanged 120-turn maximum. Completing input
   alone cannot admit that exact circuit. One public fixed-leg warm-up trial, charged to the
   same probe, completes A's next request (two members, four legs; the forecast builder returns
   feasible) but still admits and produces nothing. B prepares the public legs but does not
   reach another ward; the exact exit cause remains unresolved. Both paired sides are free of
   faults, error lines and fuel exhaustion. The failed warm-up branch was restored; no horizon,
   reserve, visibility or earning gate was relaxed. Preserve the distinction between complete
   forecast input, a valid bounded plan, admission and actual paid work.
   [Independent-bootstrap evidence](../../results/population/adaptive-independent-bootstrap/index.json)
   resolves the later A interpretation. Saved native personal inputs reproduce a feasible
   156-turn complete model: capture payout at tick54, bank at109, renewal at156, valid survival
   certificate and route joins. The old general 120-turn Plan cap and bank queue reject the
   return, while the unchanged 120-turn scorer credits only the banked14 gems. This is not
   missing qualification: ordinary U needs no prior episodes, and the ward Varg is not the
   leader. Public geometry and a complete forecast still supply no private evidence or SDK
   action permission.
   The conditional `P8_ORDINARY_COMPLETE_CYCLE=1` experiment permits a sole, unmarked,
   capital-free U/G cycle within queried remaining match time; long work explicitly issues
   no evidence episode, trial or commissioning attempt. Scoring, probes, H/J and qualification
   keep their existing windows. Its OFF bodies match all32 default raw/injected roles and
   both retained ward and warm recipes. Production-function fixtures pass, with synthetic
   local admission permission clearly distinguished from actual SDK legality.
   One full same-run Great Wall A/B pair in the frozen warm/ward context remains unproductive
   with the knob both OFF and ON. A now validates the long solo Plan, but round36 leaves
   about18.4k fuel below the unchanged30k post-model reserve. A later round52 queue succeeds
   and leaves9,610 below the unchanged25k admission reserve; no executor is admitted. B's
   later preparation exit remains unknown. All four games have zero resource work, income,
   capture, bank, admission and episodes; no faults, error lines or exhaustion, but stalls
   still block release. The knob stays OFF as a named conditional experiment, with no default
   recommendation. Stop this branch at full-validation/queue cost rather than treating the
   old Plan cap, an organization forecast or another fuel-gate relaxation as activation.
2. **Fuel.** Measured hotspots: the forecast builder (about 20-22k fuel inside, 30-32k from forecast
   entry to own return), menu baseline (about 7.3k), offer assembly (about 3k), queue checks (about
   12k), idle population and shop queries, and offer legality crossing rounds. Drivers guard with
   literal reserves duplicated per driver (55k model entry, 30k admission, 25k attach, 8k SDK
   physical). Spread modeling over turns with stale-proposal invalidation, or give more callers
   the cheaper adaptive-owned forecast Earned Center already uses (`owned_forecast` in
   `candidates.h`) after an equivalence fixture.
3. **Attribution.** Six cards cannot fully qualify H/J. Decide suite-wide whether a conservative
   own-observation proxy is admissible or whether those cards qualify components only.
4. **Evaluation.** Once income exists: per card, active vs control on both sides, one map where
   the gate should pay and one where it should cost, plus fundamentals. A mechanism counts only
   with its physical witness (the changed duty, then a bank and renewed work).
5. **Draft dependence.** 04 and 07 need Tavi and 14 needs Prism, each at pick 0; otherwise they
   are opportunity-blocked. Linked picks (Cassi/Issac) and Heart of Ruin are not in the pool.

Idea bank (unimplemented):
- Diagnostic Production: after an ambiguous mismatch, one bounded productive diagnostic separates
  two narrow alternatives while the others keep scoring; control adapts to all choices at once.
- Calibrated retirement: a CUSUM-style statistic over declared residuals replaces 2-of-3
  contradictions; counts only if it changes who works, not just when.
- Income-budgeted exposure: experimental actor-turns are capped by the incumbent's own banked
  income so far (realized, never forecast).
- Partial-refund rebuild: extend 04's reversible gear to other champions if a queried refund
  capability exists; never assume one.
- Context-geometry run: move the namespace-scope SDK initializer behind a function-local static so
  the fixture can run (byte-compare the release build to prove it gameplay-neutral).
