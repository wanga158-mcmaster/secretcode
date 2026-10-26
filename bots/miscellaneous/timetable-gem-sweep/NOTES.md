# Timetable: pragmatic race contender

Root retired the previous timetable to `bots/miscellaneous/timetable` and recreated this bot
from frozen `anchors/main-v42@9fc5d7469074`. The `_timetable-capacity` research is parked.
Current method: full-game tournaments of materially different executed race policies, then
take a healthy promising package to broad evaluation. No draft solver or capacity model is a
prerequisite. No online actions, no automatic promotion or retirement.

## First training tournament

Same-source baseline plus three packages, trace off. Six maps: 00ambush, 00eight,
00gem-fields, 00lost-world, 01big-world, 01corridors. Both sides against frozen v42 and starter:
96 full games. This is training, not qualification. All strategy overrides remain off/default
in the working source until measured.

- **workers:** Cassi & Issac first, clustered Varg/Vendetta, then Tavi/Marcella. Extra body,
  strong home-territory preference, solo job forecasts, health-backed Treasure build and early
  cash reinvestment. Larger bags reduce home harvesting interruptions.
- **jackpot:** standard AoE draft, one live leader for the first 160 rounds, concentrated
  jackpot valuation, Monster-first gear, one physical reward recipient and larger bank trips.
  Helpers leave fresh rewards and stop following a capturing leader.
- **sprint:** Vesper second, Marcella third, high gem value, low gold value, small bags and
  wider tribute detours, solo forecasts, Monster/Power gear and fewer shopping detours.

New code is only the default-off opening-pack duration and single-recipient guard in
`src/brain.cpp`. Existing strong v42 movement/combat/legality/fuel controls remain the runtime.
Exact full override strings, source keys, outcome/health and live handles are saved in the
durable result index as the tournament runs. Do not tune these six maps through small variants.

Next decision after the pilot: take the strongest materially different healthy package to all
40 pool maps, both sides versus v42 plus a different strong opponent. Fresh 17-map legacy
confirmation is reserved for a promising pool result.

## Pilot result and next decision

`race-pilot-v1` completed all 96 full games, source `68870933c4d1`. Baseline versus v42 won
6/12; workers, jackpot and sprint each won 2/12. Workers flipped +1/-5, the others +2/-6.
Every arm won 12/12 against starter, but sprint had one round-2000 tiebreak rather than a
score completion. All arms had zero faults, exhaustions, errors, stalls and suspensions;
maximum fuel was 91,695. No package is a measured improvement or a promotion candidate.

Starter score completion: baseline 856.83 rounds; workers 938.17 (+81.33 +/-25.39 SE),
jackpot 971.08 (+114.25 +/-77.89 SE), sprint capped difference +332.42 +/-87.02 SE.
The evidence owner's exact scoreboard is `results/contenders/evidence/tournament-v42-training.json`.
The source/override/coverage identities and reports are retained in the timetable result index.

**One next decision:** workers is the least damaging materially different package and advances
unchanged to all 40 pool maps, both sides against v42 and frozen v40. This is broad evaluation
of a losing training policy, not a gain claim. Do not tweak its six-map settings. Fresh legacy
confirmation follows only if that pool is promising.

Fast review found jackpot helpers could immediately rejoin the just-released leader group.
The later source respects the existing temporary ban in the opening-follow path. That fix is
default off and does not affect the workers package. The pilot retains its unfixed source.

## Worker transfer rejected; ownership-only test

The coordinator clarified the gate: being least bad does not earn broad evaluation. The
already-launched worker run had finished all 320 games before that correction arrived, so it
is retained as a one-time transfer check. No confirmation or further worker tuning follows.
On all 40 maps/both sides, workers won 8/80 versus v42 (control40/80, flips+5/-37) and 27/80
versus v40 (control48/80, flips+9/-30). Pooled margin delta -466.5 +/-54.1 SE. Both arms were
healthy; worker maximum fuel98,365. This package is rejected.

The next concrete race change is **capture ownership alone**: `race_single_recipient=1`,
with baseline v42 composition, shop, banking and target weights. `race-owner-pilot-v1` is
48 full games on the six training maps, both sides against v42 and starter, trace off.
Source42823c9974b3, live session8691 at launch. Do not broaden it unless training is promising.

Ownership-only completed all 48 games: baseline `42823c9974b3`, sole-owner
`104a6e9ce1f9`. Against v42 it won 5/12 versus 6/12 (+3/-4), margin delta
-75.33 +/-273.14 SE. Against starter both finished 12/12; sole-owner averaged 832.75
versus 856.83 rounds (-24.08 +/-14.80 SE). All own health checks were clean. This is a
passive speed observation, not established competitive improvement; no broad run follows.
Exact scoreboard: `results/contenders/evidence/race-owner-v42-training.json`.

## Bounded opening burst

The next concrete policy keeps baseline composition, shop, banking and target weights, but
uses one shared leader for up to 80 opening rounds. A spare shared byte records the leader's
goal and ends concentration after a fresh reward observation. New `race_one_burst` remains
default off. Fast review is checking the early-return coverage of that observation; this
evaluated arm is at least a bounded 80-round concentration policy, not a guaranteed event
schedule. No detailed model or native reconstruction is a prerequisite for playing it.

`race-burst-pilot-v1` completed 48 full games on the same six training maps/both sides,
baseline `71992e48d4e6`, burst `93bf3a8ba870` (`race_opening_pack_rounds=80
race_one_burst=1`). V42 wins were 7/12 versus 6/12 (+4/-3), paired margin -30.50
+/-244.25 SE. Starter score completions were 12/12 each, 843.67 versus 856.83 rounds
(-13.17 +/-22.20 SE). Own faults, exhaustions, errors, stalls and suspensions were zero;
maximum fuel 92,044. These small-sample results establish competitiveness, not a gain.

**One next decision:** evaluate this unchanged arm on all 40 pool maps/both sides against
v42 and frozen v40. `race-burst-pool-v1` launched 320 full games, trace off, session 98013.
The durable index retains exact source/override identities and live state. Do not launch
legacy confirmation unless this transfer result is promising, and do not tune the six maps.

The unchanged burst transfer completed all 320 full games. Against v42, burst won 39/80
versus control 40/80 (+19/-20). Against v40, it won 52/80 versus 48/80 (+18/-14). Pooled
wins 91/160 versus 88/160 (+37/-34), margin -1.4 +/-50.8 SE. Subject health was clean,
maximum fuel 95,825. The live-target result does not establish improvement; no legacy
confirmation or promotion follows. The 80-round shared-leader policy is not the answer.

The next decision is whether a true first-event helper release is worth testing: the current
burst observation occurs below early capture/reward returns, so it can keep followers after
the first clear. Fix that executed release seam rather than tune duration or scoring weights.
Keep any later corrected source distinct from the completed `93bf3a8ba870` evaluation.

The final opening test moved the fresh-reward latch immediately after group observation,
before capture, health and tribute returns, and enabled the existing one-recipient release.
`race-event-pilot-v1`: baseline `75537bc3ba30`, event-release `86e4e3dedb16`, 48 full
training games, trace off, unchanged 80-round cap and baseline composition/shop/weights.
It won 5/12 versus v42 against control 6/12 (+3/-4), pooled margin -12.67 +/-122.67 SE;
both won 12/12 versus starter. All own health checks were clean, maximum fuel 92,097.
Session 83222 exited zero. This arm failed the competitive gate; no broad followup.

**Opening coordination is retired as a direction.** No more duration/weight tuning, latch
variants or coupled opening packages. New knobs remain default off; historical evaluated
sources remain intact. The next race/collection decision must come from actual lost-game
income, carried-gem, banking and death evidence in the existing full-game corpus.

## One designated collector

The previous pool had no retained normal-game native statistics. Two exact baseline losses
were reproduced once with ordinary arena retention, source `71992e48d4e6` versus frozen
v42 `9fc5d7469074`: Stampede A at round1763 and Gold Rush B at round1483. Winner, rounds
and both full terminal row summaries match the original keys exactly; these are diagnostics,
not independent samples. Native receipts are saved in
`results/contenders/timetable/race-loss-reproductions/selected.json` with file hashes.

Stampede A collected160, banked107, lost53 on one death and ended with zero carry; v42
collected/banked1013. Gold Rush B collected77, banked66, ended with11 carry and lost zero
gems across16 deaths; v42 collected/banked1097. Collection includes champion receipts.
Perfect banking still leaves both supply deficits. These two cases support a portfolio
experiment, not a general claim that banking or survival is solved.

New default-off `race_gem_collector`: Marcella alone switches to gem-bearing jobs after
buying her first Jungle emblem. Both the chooser bound and actual score then exclude
zero-gem jobs and value remaining jobs by gem output; inherited travel, survival, commitment,
capture and banking remain active. Other bodies retain the baseline economy. No opening
control is enabled. Baseline shop7 buys Treasure/Jungle/Durability/Monster: in the actual
losses Marcella reached Jungle at585/238 versus Monster838/677. Jungle is the one fixed
activation milestone, giving camp mitigation without waiting for the fourth emblem. No
milestone tuning or new forecast is involved.

`race-collector-pilot-v1` completed48 full training games, ordinary `--keep-replays all`,
baseline `ffeeba68ae7a`, collector `09bb407794ee`. V42 wins7/12 versus6/12 (+3/-2),
paired margin +130.58 +/-166.62 SE. Starter12/12 score completions each, 846.08 versus856.83
rounds (-10.75 +/-12.32 SE). Own health clean, maximum fuel91,620; session61673 exited0.
This is competitive training evidence, not established improvement. The unchanged collector
advances to all40 pool maps/both sides against v42 plus v41, retaining every replay/statistics.
No legacy confirmation unless that transfer result is promising.

`race-collector-pool-v1` completed all320 full games with ordinary full retention, same
baseline/collector sources. V42 collector42/80 versus control40/80 (+12/-10); v41
collector34/80 versus37/80 (+10/-13). Pooled76/160 versus77/160, margin -0.3 +/-40.0 SE.
Own health clean, maximum fuel94,494; session20853 exited0. The small live-target edge is
offset by the regression against the different frozen v41 opponent. Coordinator explicitly rejects legacy
confirmation from this near-null result. No promotion and no tiny gem-weight or readiness
milestone tuning. Next decision awaits the compact actual role collection effect from the
retained native statistics, then chooses one substantive collection mechanism.

Do not recycle Prism battery as the next mechanism. Main D129 already rejected active
funneling (190->107 wins/266, plus63 fuel failures), and D136's healthy3520-game retention
test lost650->565/880 with lower banking and early income despite larger bags. A different
pick order does not answer those results. Those settled notes were read before any new
Prism edit or game; none was launched. Current owned source remains `ffeeba68ae7a`, all
custom race controls default off. Every local run above is complete, with no live handles.

The collector's retained native check found no clear supply gain at400: across80 map-hash
matched keys per opponent, collected-gem delta versus v42 was -1.63 +/-5.73 SE and versus
v41 +2.46 +/-4.13 SE. Pilot Marcella reached Jungle in22/24 games in each arm, but positive
collection receipts fell219->197 and gems4620->4126 (post-emblem3257->2763); terminal
duration differs. This is receipt/eligibility evidence, not private-branch execution proof.
Exact delegated reports: `results/contenders/evidence/race-collector-v42-r400.json` and
`race-collector-v42-training.json`. V41 is a different frozen strong opponent with shared
main lineage, not an independent design.

## Persistent gem territory: rejected pilot

`race_gem_territory` is a separate default-off policy. One Marcella uses the same first
Jungle readiness, selects a gem-producing patch, prefers it when not respawning, uses ordinary
nearby jobs within12 tiles (expanded for large group extent), and returns toward the patch
when no local job is available. Existing movement, safety, capture and bank phases remain
in charge. A death or180 rounds without gaining gems abandons the patch permanently to the
baseline; an ongoing capture keeps its existing completion/patience guard. This is a simple
region/return rule, with no calendar or capacity model. No Prism work was resumed.

`race-territory-pilot-v1`: baseline `aa9de9b04af8`, territory `04d28e796a2d`, only
`race_gem_territory=1`. All48 full training games completed with native retention. V42 wins
3/12 versus6/12 (+1/-4); starter12/12 each; pooled margin -51.1 +/-97.5 SE. Subject health
clean, maximum fuel91,668. Session79183 exited0. Reject this policy with no broad transfer,
radius/timeout/readiness tuning or promotion. The source and native files are retained.

Current source is `aa9de9b04af8`, with every custom race behavior default off. No games or
background jobs remain running. Opening coordination and single-worker strict collection /
static territory have not established improvement. Next experiment must change collection
execution, rather than reuse their worker/readiness scaffold or resubmit old Prism packages.

## Primary collection-controller rebuild

Coordinator directed a larger change after the failed worker scaffold: a new primary
collection controller from the first gameplay turn. Existing v42 SDK, navigation, combat,
threat, health and bank phases remain the executor. New default-off `race_collection_loop`
will replace the global weighted target quote/rechoose/hysteresis lifecycle on its ON path.
Initial policy: gem-led spatial sweep, fresh reward priority, persist through engagement and
capture, skip known respawning jobs, then choose the next gem job. Nearest unclaimed harmless
gold is bootstrap/filler when no useful available gem job exists. Existing capture, staging,
fight and travel watchdogs bound commitments; spawn tiles remain clear during waiting.

The existing `throughput_structure` subagent owns implementation in brain/config files;
`fuel_audit` owns a separate read-only poor-job receipt and measurement piece. Parent owns
coordination, pilot launches and this durable state. No calendar/capacity/auction model,
draft solver, retired executor or private activation-proof prerequisite. Freeze one coherent
controller and play48 full training games with ordinary full retention before refining it.

The retained poor-job receipt check supports the loop choice. Stampede A made118 zero-gem
camp collection receipts before its first positive gem receipt at1526. Its whole160 gems
came from one native group10 capture split54/53/53; it then returned to zero-gem gold camps
through1758 and gained no more gems. Gold Rush B had seven11-gem camp receipts at196,
495,618,684,978,1455,1461, from native groups14/10/12, against28 zero-gem camp receipts.
These are actual resource-selection sequences; native IDs are not assumed to equal static
World group indices. Measurement child will use matched native horizons and separate camp
receipts from champion pickups for the first controller pilot.

`race-loop-pilot-v1` built the new controller, baseline `82c9eebf9d8c`, loop
`d6e43d1d613b`. All48 full games/native statistics are retained. The loop won0/12 versus
v42 (historical same-run OFF control6/12) and11/12 versus starter (control12/12). Pooled
margin -486.8 +/-123.0 SE, flips0/-7. Subject health clean, peak91,608. Session59934
exited0. No broad transfer or promotion. Its matched-at-most400 native collection fell
8113->5117, banked6964->4794, gem-bearing camp jobs378->325, zero-gem receipts63->0 over
24 pairs. Horizon is min(400, shortest completed rounds) per key, not unequal terminal
totals. Exact measurement: `results/contenders/timetable/loop-measure/race-loop-pilot-v1-summary.json`.

Do not attribute this failure to removing gold alone: job persistence, planning/rechoose
and travel/shop detours changed too. Main D180/D181's failed harvest/invest and delivery
controllers were read; do not rebuild them under a new name.

The bounded actual Corridors A/starter loss points to admission/re-entry. Before400 there
are17 own camp deaths, all to two monoliths, versus3 control camp deaths (elk/wolf, none
monolith). Repeated job logs re-enter targets0/1 after respawn, with zero bags. Native
rounds20-33 (Heart0/Varg4,109 selected events, no decoder failure/truncation) show central
monolith contact, round-start HP losses, blocked/adjacent low-health exits, deaths31/33.
This is before any successful gem completion; the stale-respawn-revisit hypothesis does
not explain this case. It does not establish that those camps explain every lost game.

One approved correction keeps priority/lifecycle fixed and adds a default-off conservative
camp admission check using the existing forecast/survival machinery, separate from value
ranking and without hypothetical party health. The next48 games compare the old losing
loop against loop+admission from one source, versus fixed v42+starter on the six maps/both
sides, retaining all native files. **That baseline is the old losing loop, not v42.** Prior
OFF mirror controls remain historical. Improving over0/12 alone cannot earn promotion.

Worker health caveat: the rejected 320-game workers transfer had clean subject health, but
one v42 opponent exhaustion on Rushbait-v2 side B. Primary 160-pair result is preserved;
removing that key from both arms gives +14/-66 and margin -463.73 +/-54.38 SE over 159 pairs,
so rejection is unchanged. No rerun or selective primary filtering was used.

## Retirement: the gem sweep has no feasible production contract

The pending same-source admission correction completed all48 full native-retained games in
`.cache/contenders/timetable/race-loop-admission-pilot-v1`. Old loop `ce636b44b929` won0/12
against v42 and11/12 against starter. Solo-admitted loop `c335f47f55dd` won0/12 and5/12;
four admitted-loop games failed health through zero camp clears. No faults, fuel exhaustion
or logged errors occurred; maximum subject fuel91,635. The default-OFF source was
`81f51229637c`. Full rows, modules, statistics and replays remain preserved.

Admission prevents some unpayable camp work but also removes most available early work on
health-locked maps. A full-clear solo HP test cannot be bolted onto a nearest-gem sweep and
called an economic architecture. Nor does nearby ally HP prove a realizable shared tank.
The user renewed the mastery-first direction: models and lifecycle must cover achievable
production, recovery, capture and conversion. Root authorized retiring this implementation
to `bots/miscellaneous/timetable-gem-sweep`, preserving the earlier miscellaneous/timetable.

Its successor keeps the active timetable selector but rebuilds independent production episodes:
observed group state, achievable solo work, damaged-camp persistence across recovery trips,
actual reward completion, bounded abandonment and conversion. Party/priority work belongs
to Warband this cycle. The admission and all earlier sweeps stay rejected, not inherited as
new strength evidence. No online action or adoption follows.
