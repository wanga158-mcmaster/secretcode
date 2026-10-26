# contenders/tools: the suite's measuring standard

Owner: the evaluation engineer (22 Oct). Usage and definitions are in each script's docstring.
Runtime: `PY=C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`,
always `PY -B -I -X utf8 bots/contenders/tools/<script>.py`.

| File | Role |
| --- | --- |
| `onlinedefs.py` | verbatim copies of the foundation's per-game code (sides, trackfeat, deathctx); its hash keys the decode cache |
| `fundamentals.py` | online.md's fundamentals for any run with kept replays; `--online-reference` rebuilds `online_reference.json` |
| `o1flags.py` | copy of the foundation's O1 style flags (style.py), used for the set's O1 coverage |
| `battery.py` | the standard evaluation; strategy set pinned in `battery_set.json` |
| `distinct.py` | fingerprints on matched conditions, distances, head-to-heads, converged pairs |
| `cohort.py` | read-only current-run summary: exact source/engine cohorts, per-opponent health and coverage, strict descriptive pairing; no replay decode |
| `production.py` | native-statistics production, equipment, survival and clear/capture cycle joins; horizon- and map-byte-matched contrasts; strict `native` mode for one original statistics file |
| `scoreboard.py` | actual six-map v42 package tournament:24 keys/package, exact-opponent wins/margins, paired starter completion and health |
| `pool_scoreboard.py` | separate full40-pool package follow-up:80 keys/opponent, both side-health panels and paired health sensitivity |
| `tests/test_tools.py` | 31 tests: synthetic/local games, foundation cache/reference equality, cohort guards, native cycle order/theft/censoring and horizon/map-byte matching |
| `tests/test_scoreboard.py` | 10 synthetic guards: training coverage, exact pairing, side health, completion censoring, duplicates/conflicts and ambiguous baselines |

## Commands
- `battery.py contenders/<bot> --quick [--against DIR]`: ~240 games; full (no `--quick`) ~1,760.
- `battery.py --render-only --out DIR`: re-report finished games, plays nothing. `--show`, `--pin [--refresh]`.
- `fundamentals.py DIR --by-opponent --online`; `distinct.py DIR1 DIR2 ... [--play A,B]`.
- Interrupted battery: rerun the same command with the same `--out` (finished games are reused).
- `production.py native STATS.json --team teamA [--json OUT.json]`: read original native
  champion terminal metrics and owner-attributed events without a game or decoder. Damage,
  final-clear credit, actor receipts and banking remain distinct. Required missing data and
  contradictory owners fail explicitly; genuine measured zeros remain zero. The Python APIs
  are `native_terminal(stats, team)` and `read_native_terminal(path, team)`.
  This reducer does not establish execution provenance, technical health or private idleness.
  `tests/test_production_native.py` covers real clean/failed/passive cases and missing-data/
  owner guards (15 checks); the existing 31 tool tests still pass.
- `cohort.py RUN ... --json OUT`: summarize ordinary arena, sweep or battery rows; optional
  `--subject BOT_GLOB` selects subjects when tags are absent. Never launches games or decoders.

## Full-game policy tournament (23 Oct reset, live v42)

The fresh contenders derive from frozen `anchors/main-v42@9fc5d7469074`. Live-at-reset is
`main/main@0353a987e04f`, reported module SHA256
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
The new scorekeeper reads actual owner games only; it launches no games or native observers.
Prior v40/v41 evidence and old contender sources stay historical.

`results/contenders/evidence/tournament-v42-training.json` contains the complete288-game
training tournament: each owner has baseline plus three whole-policy packages; each package
has **24 keys**, six maps (`00ambush`, `00eight`, `00gem-fields`, `00lost-world`,
`01big-world`, `01corridors`), both sides, exact v42 and starter opponents. Each opponent
therefore has12 keys/package. This is distinct from later40-pool and fresh17-legacy panels.
Sources, engine, variant and phase remain separate; changed opponents never pair.

All three baselines match:6/12 versus v42 and12/12 starter score completions, mean856.833
rounds. The pilot lead is warband carrier_pincer `2487b2439693`:8/12 versus v42, paired
flips+3/-1 and gem-margin delta+119.58±169.80SE, while its starter race is51.42±26.03SE
rounds slower. Compound factory ties baseline6/12; timetable's three packages reach2/12.
All subject health is clean. These are package-selection results, not strength qualification.
Compound factory's requested safe-recall0.35 is inert under inherited recall0.50; its actual
measured source is preserved, with no threshold-test claim.

Starter wins and score completion are separate: timetable sprint wins12/12 but reaches1000
in11/12; the remaining tiebreak is not counted as a completion. The report includes the
completed-only round mean and paired capped-round delta (non-completion=2000) so censoring
cannot make a slower package look faster. Margins are terminal own-minus-opponent banked
gems; paired deltas are candidate-minus-baseline on identical keys.

```
PY -B -I -X utf8 bots/contenders/tools/scoreboard.py .cache/contenders/compound/packages-v42-pilot .cache/contenders/timetable/race-pilot-v1 .cache/contenders/warband/sweeps/map-control-pilot-r1 --json results/contenders/evidence/tournament-v42-training.json
PY -B -I -X utf8 bots/contenders/tools/tests/test_scoreboard.py
```

`results/contenders/evidence/tournament-v42-pool.json` preserves the three320-game
follow-ups separately from those288 training games. None transferred its pilot selection:
compound factory27/80 versus v42 (base40), timetable workers8/80 (base40), warband carrier36/80
(base40). Compound/warband also face frozen `main-hunter7@417c0489eb1f`; timetable faces
v40`6f62c56b73f7`. These distinct opponent sets are never pooled into a shared win rate.
No fresh17-legacy confirmation follows from these rejected packages.

All subject health is clean. Opponent health is preserved separately: factory/Hunter7
GemFieldsA and carrier/Hunter7 GemFieldsB have a zero-clear stall; workers/v42 Rushbait-v2B
has an exhaustion. Raw results stay primary. Removing each affected key from **both** paired
arms leaves159 pairs and the same rejection. Per-opponent margins, paired deltas and
terminal banked/gold/kill/death/clear totals are in the report; different game lengths mean
these terminal totals cannot establish production rates. Carried gems are absent from rows.

```
PY -B -I -X utf8 bots/contenders/tools/pool_scoreboard.py .cache/contenders/compound/packages-v42-pool .cache/contenders/timetable/race-workers-pool-v1 .cache/contenders/warband/sweeps/map-control-pool-r1
```

Subsequent training runs receive separate report files, preserving the original288-game
tournament and avoiding ambiguous comparisons to later baseline sources.

Later complete cohorts remain separate: `item-tempo-v42-training.json` (72),
`race-burst-v42-training.json` (48), `site-control-v42-training.json` (72),
`race-event-v42-training.json` (48), `race-collector-v42-training.json` (48), and
`ryth-mixed-v42-training.json` (48). Their exact opponent zero-clear keys are explicit;
starter zero clears are an opposition limitation, not clean subject health.

`tournament-v42-second-pool.json` retains another960 rows: adaptive shop36/80 versus v42
and two own exhaustions (adoption blocked); timetable burst39/80 versus v42; warband
deterrence46/80 versus v42. Deterrence's separate136-row17-legacy attempt in
`warband-ryth-v42-legacy.json` yields16/34 versus v42 baseline17/34 and30/34 versus
hunter7 baseline30/34, both sides clean. The preliminary pool lead has no positive win
confirmation. `compound-tempo-v42-pool.json` and `field-capital-v42-pool.json` preserve
the rejected320-row economic packages; no pilot result is relabelled after LF-only source
bindings or later full-pool play.

Collector's320-row report is `race-collector-v42-pool.json`:42/80 versus v42 control40,
34/80 versus v41 control37; no clear overall gain. Existing native statistics at the same
round400,80 exact map-byte/side keys per opponent, show collected-gem deltas of
−1.625±5.733SE versus v42 and+2.463±4.125SE versus v41. The source-bound per-role public
receipt review and native hashes are attached to its training report; they do not prove
private branch execution. See `race-collector-v42-r400.json` for equal-time coverage.

The two exact timetable baseline-loss reproductions are diagnostic repeats, contributing
zero independent strength keys. Their original terminal fields match exactly; selected
native bytes are compressed under `witnesses/` and indexed in
`tournament-v42-second-pool.json`'s `selected_native_diagnostics`. StampedeA collects160,
banks107 and loses53 at death versus main's1013 collected/banked. GoldRushB collects77,
banks66 and ends with11 carried versus main's1097 collected/banked; its16 deaths shed no
gems. Even perfect banking leaves a large collection deficit in these two cases.

The `actual_game_scoreboard_index` inside `tournament-v42-second-pool.json` links the
completed source-bound reports by hash, without summing overlapping keys as independent
confirmations. Mixed Ryth's pool result is46/80 versus v42; its separate legacy result is
15/34 versus control17/34. The requested held-versus-mixed contrast is attached to
`ryth-mixed-v42-pool.json`: both BASE binaries and160 normalized baseline result payloads
match, while mixed-minus-held flips+2/-2 and margin−5.96±13.64SE. These are separate sweeps,
not a same-run three-arm tournament. The completed frozen-v41 confirmation is separate in
`warband-ryth-mixed-v41-confirmation.json`:51/114 versus control50/114,+13/-12, margin
+14.32±51.97SE, both sides clean. Its57 maps were already used; the alternate main-lineage
opponent supplies neither fresh geometry nor an independent bot family. No robust broad
improvement is established, and the mixed package remains off.

## Historical qualification target (23 Oct, v41 checkpoint)

At this checkpoint `results/releases/LIVE.json` named `main-v41`, released
`main/main@db6ca9bc4e39`; frozen anchor identity is
`anchors/main-v41@6e7ef55c2795`. Working main `7d548544203c` with
`refresh_dead_camp=false` builds to the same raw and injected module SHA256,
`97202ece0b7114a4fad49ec0831e89c90dbf92a0e85f3b6b2aa86ab2e65f43d0`.
The bounded local audit and exact paths/hashes are in
`results/contenders/evidence/qualification-target.json`. That metadata file remains a v41
checkpoint. Earlier v40 cohorts, production horizons and measured source identities below
are preserved. Settled null experiments do not need replaying solely because the target advanced.

Retained v41 default evidence is `.cache/main/v41-default110-pool`: 880 finished games,
11 opponents, all40 pool maps/both sides, exact subject `db6ca9bc4e39`, engine1.0.3.
Its627/880 result is not paired with the old v40 panel: eight persona sources changed.
The final gate is `.cache/main/gate-v41-110-final`, including47/80 against v40.
These are main's retained qualifications, not contender wins against v41.

`fuel-header-audit.json` bounds the shared arena budget-index defect: games/report use
A's ordinary budget at index3 for a B stream, whose budget is index7. All960 retained
current contender gate/panel archives (1920 streams) have100K/100K headers, so budget-hit
counts and health conclusions do not change; peak fuel is an independent maximum and was
unaffected. Faults/exhaustions, stdout errors and zero-clear stalls remain side-attributed.
An additional47 labor-probe archives have93 present100K streams; one missing B stream is
explicitly unknown. Duplicate probe readers were reported to their owners. No integrator
reader was edited here; future asymmetric-budget fixtures require the corrected side index.

## Current-engine checkpoint (22 Oct, resumed contender work)

The live-at-start reference is `anchors/main-v40@6f62c56b73f7`, engine **1.0.3**.
The historical battery pins below remain engine **1.0.2** and are not refreshed or relabelled.
Current baseline evidence is retained in
`results/contenders/evidence/cohort-v40-baseline.json`; all three v40 matchups cover the same
40 pool maps, both sides, 80 games each, with these exact subject sources:

| Subject | Source | W-L vs v40 | faults / exhausted / errors / stalls |
| --- | --- | --- | --- |
| compound | `9da7bb86f3ab` | 0-80 | 0 / 21 / 2 / 0 |
| timetable | `3cfc5d884af9` | 0-80 | 0 / 0 / 10 / 0 |
| warband | `3290f993fd84` | 0-80 | 0 / 0 / 39 / 2 |

These are pre-repair baselines, not performance qualifications. Starter games are reported
separately: timetable 73-7 and warband 65-15, each 80 games with clean recorded health.
The retained v40 reference `.cache/main/g-v40-final-103` is the exact subject
`main/main@45108c43eb8a`, engine1.0.3: 638-242 across 880 games (11 opponents x40 maps x2sides),
clean recorded health, peak95,189. Its current-source personas are separate from this suite's
old frozen v36 personas; exact opponent identities stay in the summary. Its saved rows have
`replay:null`, so this retained run supports outcomes, terminal metrics and speedrun only,
not r400 fundamentals or decision distinctness. V40's starter speedrun is989.19 rounds with
1/80 unfinished; timetable1635.59 with33/80 and warband1764.67 with37/80 are separate current
engine1.0.3, all40-map/both-side measurements. Compound has no starter rows in this checkpoint.

`cohort.py` splits subject source, engine, variant and phase; phase labels are retained but do
not alter game pairing. It pairs only the same recorded engine, exact opponent source, map,
subject side and seed. Unknown engines refuse pairing; identical retries deduplicate;
conflicting duplicate cells are excluded. Missing health fields remain unknown, and truncated
games do not count as finished pool coverage. The summary reports source identities, not raw
executable/map-byte bindings or planned-run completion. Matched outcome differences are
descriptive; failed health still blocks qualification. Input row files and the summary tool
are retained by SHA256 and size so a later changed run cannot silently inherit this checkpoint.

Audit caveat: historical `battery.py --against` delegates to arena's looser outcome comparison,
which omits engine from keys and warns about changed opponents after producing a verdict.
Its fundamentals filter exact opponents but also omit engine. `distinct.py` likewise lacks an
engine key and pools subject snapshots by bot id. Until those legacy comparison paths are
revised, use `cohort.py` for identity/health/coverage checks and compare only independently
verified same-engine, same-opponent, single-source inputs. Do not infer distinctness from a
cohort outcome table.

Regenerate the checkpoint without playing games:
```
PY -B -I -X utf8 bots/contenders/tools/cohort.py .cache/contenders/timetable/current-v40 .cache/contenders/timetable/current-starter .cache/contenders/compound/resume-v40-base .cache/contenders/warband/runs/resume-v40-baseline .cache/main/g-v40-final-103 --json results/contenders/evidence/cohort-v40-baseline.json
```

## Final current-source evidence (22 Oct local)

`results/contenders/evidence/cohort-current.json` preserves ten exact source/variant cohorts
from the final three experiments and two promoted-default health gates, engine1.0.3 throughout.
Its review annotations distinguish the current defaults from historical controls and enabled
experiments. The live-at-start reference remains `anchors/main-v40@6f62c56b73f7`.

| Current default | Source | vs v40 | vs starter | vs fundamentals | Own health |
| --- | --- | --- | --- | --- | --- |
| timetable | `80d9c5ef961b` | 0-80 | 73-7 | 16-64 | clean, 240-game gate, peak99,432 |
| compound | `904d86692b59` (tested `6edcbd448d9c`) | 0-80 | 79-1 | 67-13 | clean, 240-game gate, peak86,883 |
| warband | `d90267e0fb82` (tested `048fcda6fdde`) | 0-80 | 65-15 | unmeasured | 39 error lines and2 stalls vs v40; no faults/exhaustion |

Every measured opponent cell is all40 pool maps, both sides,80 games. Timetable promotes only
the legality guard; compound promotes only the bounded-turn execution repair. Their strategic
experiments stay off. Warband's guard and micro retarget arms stay off: both clear the39 errors
but retain2 stalls against v40. Its pack arm wins67/80 starter games; the isolated guard->pack
contrast is null (+3/-1 over160 paired keys, p=.625). Compound's bounded arm is likewise null
for strength (+6/-3 over240 keys, p=.508), as is timetable's bounded scheduler/evidence arm
(+9/-4 over240, p=.267). All arms remain0/80 against v40. Health gates are not reference-score
qualifications. Compound's and warband's final source keys change only through CRLF-to-LF
normalization; both rebuilt modules exactly match their tested defaults. Warband's final
experimental guard `af8cd00d07d2` and pack `8e12a2e9a537` likewise match evaluated
`9e67c2c68be8` and `0bfc71c1162d`. These bindings are explicit in the report; measured rows
and cohort identities stay at their original source keys.

The saved2160 rows are not2160 independent samples: both promoted-default gates repeat the
same240 deterministic keys with modules matching their evaluated enabled arms. The original
baseline index is unchanged. Owner evidence and byte-preservation details live in
`results/contenders/timetable/index.json`, `results/contenders/compound/index.json` and
`results/contenders/warband/index.json`. No new games or replay decodes were needed for this integration.

Regenerate the measured cohort blocks (review annotations are a separate interpretation):
```
PY -B -I -X utf8 bots/contenders/tools/cohort.py .cache/contenders/timetable/evidence-reviewed .cache/contenders/timetable/gate-legality .cache/contenders/compound/health-repair-pool .cache/contenders/compound/health-promotion-gate .cache/contenders/warband/sweeps/resume-action-packing-r2 --json .cache/contenders/evidence/cohort-current-regenerated.json
```

## Structural production evidence (current-source work)

`production.py` reads existing native statistics without builds, games or replay decoding.
`results/contenders/evidence/production-current.json` covers720 saved games: current contender
defaults against starter/v40, and current-main BASE against starter/v40/hoarder. Exact measured
subjects are timetable`80d9c5ef961b`, compound`6edcbd448d9c`, warband`048fcda6fdde` and
main`80f9c96c7104`, engine1.0.3. Final LF-only contender source bindings remain in
`cohort-current.json`; measured identities are unchanged. The main reference is
`.cache/main/plan109-pool`, **base only**, avoiding its unpromoted planning arm.
This is a source-bound checkpoint; later working-main revisions need their own source/byte
binding and eventual direct strength checks.

At r400 the passive comparison uses the **same79 map/side keys** against
`anchors/starter-cpp@3785135fc7c7`, with matching native map hashes. LostWorldB is excluded from
every comparison because main and compound finish before400. No terminal carryforward:

| Subject | Gold | Collected gems | Banked | Captures | Deaths |
| --- | --- | --- | --- | --- | --- |
| current main BASE | 2549 | 270.0 | 232.5 | 25.3 | 0.27 |
| compound | 1848 | 165.0 | 135.0 | 20.9 | 0.59 |
| timetable | 1254 | 147.1 | 104.7 | 23.7 | 1.06 |
| warband | 1211 | 147.9 | 87.0 | 8.5 | 1.19 |

These gaps exist against a passive opponent. Timetable nearly matches capture count but earns
half the gold: portfolio/value and productive body allocation matter more than count alone.
It takes far fewer elk and many more harmless gem groups than main. Compound's competitive
first capture does not extend to later production; it has fewer wolf/elk cycles and later item
investment. Warband gets richer gems per capture but about one third of main's capture cycles,
shops later and buys mostly Health. These are distinct leads for the three doctrines, not
isolated causal effects or a reason to copy main's organisation. Equipment and size strata,
native death causes and the paired numerical deltas are in the report.

Native clear-to-capture joins distinguish a stolen reward from a late capture: sort by native
round/input/ordinal, take the first group capture before its next clear, and deduplicate all
recipients (including zero-gem gold-group receipts). A count ratio cannot prove conversion loss.
Timetable AmbushB really loses goldgroup3: clear138, main capture157; first own capture288.
Yet full-pool conversion is generally high, so this witness does not establish a global
conversion bottleneck. Warband AmbushB has zero champion damage dealt **and received** and no
deaths through400, while earning967 gold against main's1712; first damage dealt is1049, which
does not establish absence of all earlier contact. Exact current-default native witness bytes
are compressed losslessly under `results/contenders/evidence/witnesses/` and indexed in
`structural-witnesses.json`. No private branch/path attribution is claimed. Empty travel and
idle-time shares remain unknown without positions or action logs; the tool does not infer them
from low production. The dead-round figure is explicitly a60-round respawn proxy.

Known banking counterstrategy stays scoped: the old v40 gauntlet's hoarder
`main/main@7d0da369515a` wins49/80 against`main/main@45108c43eb8a`; current-main retained BASE
faces distinct hoarder`main/main@384fb69892fa`, also49/80, all40 pool maps/both sides and clean
hoarder health. These reuse the same pool keys and are not160 independent confirmations.
`tribute_gems=120` also retains main's near-altar trigger, so this is not unbounded carrying.
Main's D102 already rejects a general larger-bag promotion: thresholds40/60/90 improve the
passive race while losing to hunters. D115's value-based shopping is a separate adopted
mechanism. A new route/exposure-dependent policy needs held-out hunter7/aggressive opposition,
not merely a main-only gain. This existing counterstrategy is a lead, not goal completion.

Reproduce the native analysis (the opponent filter accepts bot ids, exact entrants or saved
opponent labels; output always retains the exact entrant):
```
PY -B -I -X utf8 bots/contenders/tools/production.py .cache/contenders/timetable/gate-legality .cache/contenders/compound/health-promotion-gate .cache/contenders/warband/sweeps/resume-action-packing-r2 .cache/main/plan109-pool --variant base --opponent anchors/starter-cpp --opponent anchors/main-v40 --opponent persona:hoarder --json results/contenders/evidence/production-current.json
```

## Bounded coalition opportunities from retained native windows

`results/contenders/evidence/coalition/index.json` indexes four native windows from exact
`main/main@80f9c96c7104` versus starter `3785135fc7c7`, engine1.0.3: 113 unique replay-rounds,
complete observation/query coverage and zero query errors. Source rows, native-statistics,
replay, map, trace and contained-process hashes preserve the identity; all four private
native Job trees exited and were confirmed empty. These are v40-era main observations.
YBR's starter exhausted three times and Corridors' starter cleared zero camps, so these
selected passive games provide mechanism evidence only.

The concrete caller fixture `coalition/ybr-offer-r330.json` records every actor's current
state/order, personal vision, health, equipment, ability contract, all current camps/groups,
and a seven-step personally visible, currently free ordinary route for Ryth. Conditional
first melee is336; Varg's baseline first elk hit337, clear351 and capture358. The target
is native group4, **30gems/220gold**, not a headcount-derived estimate. Future blockers,
awake neighboring wolves and Ryth's lack of Varg's camp mitigation remain material.
Target awake state at the offer was not directly queried.

**Ryth is not certified as a cheap helper.** It stands on group0 while enemy9 occupies the
opposite end: leaving can release an uncontested five-round enemy capture worth600gold.
Zero observed personal income does not price this denied enemy income. The displaced
alternative remains explicitly unknown until a denial handoff/other contestor or a priced
team counterfactual is established. A dispatch policy must include that term before claiming
the elk beats independent work. No profitable joint arrival/clear/capture is yet established.

Corridors rejects four-intent headcount reasoning: Marcella is low-health and recalling,
Vendetta arrives first, and Heart/Ryth join later; the baseline already has follow-up.
Pilgrimage rejects position overlap as damage overlap: two bodies attack elk together only
at527-528, then retreat. A third body's optimistic arrival533-535 displaces an actual
wolf capture542 worth144gold/15gems; the farther Ryth cannot join that window. The next useful
measurement is one native offer-to-capture attempt with priced denial/alternative income and
prefix survival, not another aggregate baseline. Elkring was scouted but not decoded; this
was a bounded search, not evidence that all other coalition opportunities are absent.

## State (22 Oct ~06:10Z, checkpoint before a reboot)
Done: the tools, tests (21/21), the full main-v35 reference, quick runs of the three contenders, a quick
main-v35 run paired against the reference (180 pinned keys replay identically). Run directories under
`.cache/contenders/battery/`: `ref-main-v35` (full, rendered with O1 coverage, statistics compacted),
`quick-timetable` (timetable@0cc5058027e2), `quick-warband` (@7642ebd108c8), `quick-compound`
(@0a9aad7f787e), `quick-main-v35`, `distinct-4.json`; decode cache `analysis/`.
Not done: (1) the quick-* reports predate the O1 section and compaction: `battery.py --render-only
--out .cache/contenders/battery/quick-<bot>`; (2) `ARCHETYPES` in battery.py is an empty hook
(`contenders/_archetypes` did not exist yet): fill it from its NOTES.md, then `battery.py --pin`.

## main-v35 reference (full: 22 opponents x 40 maps x 2 sides, 1,760 games, all clean)
Speedrun 995 rounds (median 969; 2 of 80 unfinished, 00stampede). Worst first: persona:hoarder 0.44
[0.33-0.55], main-v36 0.48, persona:meta 0.50, persona:willow 0.61, main-v28 0.65, squads 0.66, rush
0.69, hunter 0.71, hunter7 0.85; ladder bots, starter, B0 and contenders >= 0.975; set pooled 0.825.

| subject side | sides | gold/alive r400 | r1000 | dead r400 | P(death/fight) | sep/recall |
| --- | --- | --- | --- | --- | --- | --- |
| local, all games | 1758 | 1.04 | 1.48 | 53 | 12% | 70/17% |
| local vs main family / personas | 240 / 639 | 0.95 / 0.94 | 1.30 / 1.36 | 85 / 86 | 18% / 16% | 59/22% |
| local vs ladder bots | 480 | 1.19 | 1.71 | 11 | 0% | 92/6% |
| online v35 (all / vs T1 / vs T2) | 208 / 25 / 98 | 0.89 / 0.76 / 0.84 | 1.29 / 0.94 / 1.30 | 98 / 152 / 90 | 23 / 30 / 24% | 58/17% |
| online T1 / all our versions | 353 / 901 | 1.05 / 0.84 | 1.36 / 1.13 | 102 / 132 | 13 / 30% | 75/10, 53/16% |

The opposition explains the gap: local main-lineage opponents play like online T2-T3 (main dies in
16-18% of fights against them, 23-24% online against T2-T3), the ladder bots barely fight (0%), and no
local opponent kills main as T1 does (30%). Pooled local numbers look T1-like through weak opposition.
The 0.84/132/30% often quoted for v33-v36 is the all-versions row; v33-v36 measure 0.89/121/26%.

O1 coverage of the set (o1flags on the opponents' sides): covered grouped, dispersed, Health-first,
Focus burst, lock use, big bags (warband only: persona:hoarder shows it in 4 of 62 games), passivity,
sustain, camp suicide; weak gem rush (persona:rush 19%), invasion (49%); absent early aggression,
spawn camping, Prism battery, crystal routing, early siege; no flag: jackpot, chases, kiting, bush.

## Requests for the integrator
- `arena prune` protects only snapshots named under `results/population/`; pins here stay alive
  only because each battery touches them (14-day window).
- `arena population panel/regress` refuses main, anchors, heuristics and contenders, so it cannot
  pin this set; a non-admission evaluation panel would replace `battery_set.json`'s own pins.
- Game rows do not name their statistics file; the tools assume `<replay>.json` (or `.json.gz`).
