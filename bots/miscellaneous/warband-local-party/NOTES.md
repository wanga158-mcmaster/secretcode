# Warband: map control

The game-first reset starts from frozen `anchors/main-v42@9fc5d7469074`. The previous complete
warband is preserved at `bots/miscellaneous/warband/`; `_warband-*` party/focus research is parked.
Its fixtures and old panels do not qualify this implementation. Direction: useful local pressure
through pairs, concentrated rich-ground squads, or opportunistic carrier interception. Judge the
packages in normal full games before expanding models or protocols.

## 23 October: first full-game tournament

Evaluated default `7ddc68b46c8f` retains v42 behavior. Final LF source `7b99c6dfd13c` builds to
the exact same raw/injected v42 module; rejected carrier source `3bf033697f3d` similarly aliases
evaluated `2487b2439693`. Proof: `results/contenders/warband/map-control/newline-identities-r1.json`.
Only four inherited CRLF files were normalized; all current sources use LF. The only added action gate is default-off
`hunt_min_nearby_allies`: enabled hunting requires visible live allies within `hunt_support_radius`
and rejects allies currently recalling, submitting or teleporting. This is nearby presence, not a
promise of contribution: stunned allies can count. The bounded roster scan does not run in default
builds. Existing fight admission remains responsible for contact. No new contract or arrival model.

Same-run BASE plus three packages, six training maps (`00ambush`, `00eight`, `00gem-fields`,
`00lost-world`, `01big-world`, `01corridors`), both sides, frozen v42 and starter, 96 full games.
No trace or round limit. Run: `.cache/contenders/warband/sweeps/map-control-pilot-r1`.
Durable rows, exact overrides, sources and report: `results/contenders/warband/map-control/`.

| Package | Source | Direction | v42 wins / 12 | Starter wins / 12 | Mean pooled gem margin |
| --- | --- | --- | --- | --- | --- |
| BASE | 7ddc68b46c8f | frozen v42 execution | 6 | 12 | +471.25 |
| mobile_pairs | 7c68df612b93 | draft3; persistent pairs; rechoose4; no hypothetical joiners; bank60 | 5 | 12 | +382.17 |
| rich_ground | 21f38293451d | draft1; balanced squads; jackpot share .20; committed wait18; bank80 | 2 | 12 | +241.54 |
| carrier_pincer | 2487b2439693 | draft18; recent carrier20 pursuit; nearby ally; channel/range-aware fight model | 8 | 12 | +519.71 |

Mobile pairs overrides: `draft_mode=3 squad_size=2 squad_until_round=0 max_hypothetical_joiners=0
retarget_ratio=1.20 rechoose_every=4 staging_patience=3 staging_patience_committed=6 tribute_gems=60
tribute_detour_tiles=4 travel_recall_margin=0`. This removes target transport recalls; banking and
shopping recalls still work.

Rich ground overrides: `draft_mode=1 squad_size=3 squad_until_round=0 max_hypothetical_joiners=0
jackpot_joiners=0 jackpot_share=0.20 jackpot_weight=1.0 jackpot_patience=18 staging_patience=10
staging_patience_committed=18 siege_patience_min_gems=15 retarget_ratio=1.50 rechoose_every=12
tribute_gems=80 tribute_detour_tiles=4 capture_hold=true`. The existing >=3 partition balances two
squads, often 3+2 with five bodies; it does not guarantee triples.

Carrier pincer overrides: `draft_mode=18 hunt_mode=3 hunt_carrier_gems=20 hunt_age=2 hunt_radius=8
hunt_health=0.70 hunt_min_nearby_allies=1 hunt_support_radius=4 sim_ally_channels=true
sim_ally_range=true threat_focus=true share_sightings=false sos_radius=0 squad_size=1`.
It retains independent economy and diverts toward a remembered rich enemy when local support is
present. Greedy pursuit can cost farming time or cross camp exposure. SOS and shared sightings
are both off because the inherited implementations overlap their shared-memory bytes.

All 96 games finish with zero faults, exhaustion, errors, stalls or suspension. Peak fuel by arm:
BASE91640, pairs91643, rich91645, carrier91605 / 100000. Starter mean completion rounds:
BASE856.83, pairs872.58, rich996.00, carrier908.25. Thus grouping serializes the passive economy;
the carrier package also slows starter by51.42±26.03 SE. Carrier's v42 margin change is
+119.58±169.80 SE, flips +3/-1; this small training lead is not qualification.

Initial decision: discard the two grouping packages for this iteration; advance unchanged carrier pincer
to all40 pool maps, both sides, frozen v42 plus aggressive `main-hunter7`. No retuning on the six
training maps. Full-pool run `.cache/contenders/warband/sweeps/map-control-pool-r1`, BASE+carrier,
320 full games, trace off. Both exec sessions29664 and3657 completed normally.

The full pool rejected the training lead: BASE106/160 versus carrier98/160; v42 BASE40/80 to
carrier36/80 (+18/-22), hunter7 BASE66/80 to62/80 (+8/-12). Pooled margin244.58 to218.87,
paired change-25.71±55.58 SE, McNemar p=.366. Own faults/exhaustion/errors/stalls/suspension all0;
peak BASE93864, carrier95307. The opponent hunter7 has one zero-clear stall against carrier on
GemFields B. Removing that matched key from both arms gives159 pairs and still-26.35±55.93 SE,
same +26/-34 flips. Raw rows remain intact. No fresh legacy run or tuning is justified.

Terminal totals do not show a blanket productivity collapse: versus v42 carrier has +4.06 clears,
+0.76 kills,-0.31 deaths,+4.96 banked gems and+615.36 gold per game; versus hunter7 +2.21 clears,
+809.9 gold but-15.41 banked gems. These games have different durations, so totals are not rates.
Carried gems are absent from the rows. Do not attribute the aggregate failure to one mechanism.

Decision: reject the entire carrier package and retain the v42-equivalent default. Root's next
direction is one explicit rich-site capture/denial job on baseline composition, rather than
chasing a moving carrier. Inspect one or two saved loss summaries, choose one duty, then measure
a short full-game same-source pilot. The original six maps remain training data, not qualification.

Resume current assessment with the exact carrier overrides above and `arena sweep
contenders/warband --var "carrier_pincer: ..." --vs anchors/main-v42,anchors/main-hunter7 --maps
pool --sides both --out .cache/contenders/warband/sweeps/map-control-pool-r1 --keep-replays all`.
The existing output reuses completed keys. The durable index is
`results/contenders/warband/index.json`; no historical game/source identities are relabeled.

## Direct site control and retained farm threat

Root selected direct control after the carrier package failed. A cheap saved Ambush B baseline
loss has56vs57 clears but746vs1002 banked gems,7262vs8704 gold and4vs2 deaths. This supports
testing resource conversion/denial without asserting that capture selection caused the loss.
Source summary: `map-control-pool-r1/games/00ambush_main-v42_warband_029ec93d.json` under the cache run.
Rules194 requires uncontested occupancy for capture and says one body suffices; occupation also
blocks respawn, but the API does not expose captor ownership. We chose taking a fresh reward
over holding an already captured site, avoiding speculative respawn ownership/timing.

New all-OFF source `76b6129855e8` is raw/injected exact frozen v42 SHA256
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
Baseline draft/shop and all prior knobs remain unchanged. Two small, separate arms:

- `reward_raider` (`1ee6e3532f23`): only `control_reward_raider=true`. Roster-index0 seeks a
  currently observed reward with15+gems, a visible free member tile within6 and enemy-side
  geometry, rejecting existing allied commitments/on-tile workers. It publishes the ordinary
  slot, which other bodies respect even for cached targets. Normal go_capture/navigation and
  capture continuation execute it. While capturing it keeps its reservation and does not close
  in on a favourable combat verdict; actual danger, health and bank handling still preempt.
  Capturer0 uses a12-turn patience ceiling, including its ordinary captures. Fresh state does
  not prove the enemy made the clear. This is one worker's direct allocation experiment.
- `ryth_deterrence` (`a609f651a2ea`): only `control_hold_ryth_aoe=true`. During camp spell choice,
  Ryth preserves ready slot0 ground AoE when a fresh visible live enemy is within6. Camp basics,
  resets, other kits and all champion combat stay in the ordinary executor. This tests retaining
  a threat while earning income, without movement toward a new target.

Pilot `.cache/contenders/warband/sweeps/site-control-pilot-r1`: same6 training maps, both sides,
v42+starter, BASE+two arms,72 full games, traceOFF. BASE18/24, raider18/24, Ryth20/24. Against
v42:6/12,6/12,8/12; starter12/12 each. Raider flips+3/-3, pooled margin change-6.5±122.7 SE.
Ryth flips+3/-1, pooled margin change+57.4±76.6 SE. Every owned health flag0; peak91640/91613/91670.
No qualification claim from repeatedly used training maps.

Decision: leave reward raidingOFF and advance unchanged Ryth deterrence to all40pool/both sides,
v42+hunter7,320 full keys: `.cache/contenders/warband/sweeps/ryth-control-pool-r1`.
Exec session53879 is active; site-control pilot57548 completed. No extra variants or native proofs.

Full-pool `ryth-control-pool-r1` completed320/320: BASE106/160 to Ryth115/160. Against v42,
40/80 to46/80 (+18/-12); against hunter7,66/80 to69/80 (+5/-2). Pooled paired margin change
+53.7±39.8 SE, McNemar p=.188. All own health flags0, peak93882/100000. Both small/large map
halves improve (52→55 and54→60 wins /80). This is promising preliminary evidence, not robust
qualification. Source and all knobs remain unchanged; no tuning follows the positive result.

Fresh confirmation `.cache/contenders/warband/sweeps/ryth-control-legacy-r1`: BASE+Ryth on all17
legacy maps, both sides, frozen v42+hunter7,136 full keys, traceOFF. Session39439 is active;
full-pool53879 is closed. Root asked micro/foundation to inspect a few saved actual paired games
with existing tools, no new games/instrumentation or fixture gate. Their contribution evidence
must remain separate from the win-rate result. Recheck live/current main before qualification.

Fresh legacy completed136/136 with own health all0, peak91681. Against v42: BASE17/34 to16/34
(+4/-5); hunter7:30/34 to30/34 (+4/-4). Pooled47/68 to46/68, margin change+70.0±72.2 SE.
This is competitive, but it does not confirm a win-rate gain. Across pool+legacy the unchanged
candidate is62/114 against live v42 versus BASE57/114, and99/114 against hunter7 versus96/114.
Pool has one hunter7 zero-clear opponent stall on GemFields A against Ryth; removing that key
from both arms leaves159 pairs, the same +23/-14 flips and+54.17±40.03 SE pooled margin change.
The healthy owned results and opponent anomaly are distinct; raw rows are retained.

Current default is still all-OFF `76b6129855e8`; Ryth `a609f651a2ea` is promising and unpromoted.
No release or online action. LIVE readback still v42 (`main/main@0353a987e04f`); main README says
its production remains exact live v42 while its rethink experiments are private. Root is checking
current identity before any qualification decision. Foundation owns a read-only mechanism review;
micro owns a small gain/loss replay sample, with no new games or instrumentation. All owned
sessions3657,29664,57548,53879,39439 and the two LF builds have finished; no owned jobs remain.

Foundation source review found no new legality defect in the guard. Its mechanisms are broader
than deterrence alone: CD0 changes our engagement forecast and later actual spell availability;
visible ready Ryth changes v42's enemy reach estimate (melee floor2 to spell5); a reset can replace
the withheld AoE; withholding can remove mixed champion/camp collateral. Current camp_dps still
credits the withheld farm spell (brain.cpp51–57/model.h546–577), so its farm-service estimate can
overstate actual delivery. The unchanged enemy AoE caller only tries enemy-occupied centers
within4, whereas theoretical ready threat can reach5. Radius6 also includes immune/spawn enemies.
These are source mechanisms/limitations, not replay-proven causal explanations of the wins.

## Mixed camp/enemy allocation

Micro's bounded native replay sample finds actual costs and benefits of holding. Fragment A vs
v42 has matched r95 pre-cast state: OFF AoE deals100 to each of two wolves; ON reset+extra45
basic holds AoE but takes60 more camp damage by96. At106 ON spends the retained AoE to remove
100 Heart shield/stun it and deal100 to Marcella; OFF has cooldown6 and moves. Fragment B has
OFF AoE kill a12HP wolf at72 while ON holds and ends (reset cooldown7), delaying the kill and
not landing a Ryth PvP hit through99. Existing normal enemy casting also produces dual-use:
Fragment B OFF at93 damages/stuns enemy Ryth and damages a camp in one event; hunter7 Fragment A
ON at40 similarly damages/stuns enemy Ryth and finishes a20HP camp. This does not attribute wins
to those windows or prove that a new camp-centered policy improves those exact casts.

Root approved one bounded executor follow-up without waiting for a larger attribution project.
`control_ryth_mixed_aoe=true` is standalone: when a fresh visible live enemy is within the same
radius6, ready Ryth slot0 farm AoE may use an existing camp-centered candidate only if it also
hits a refreshed living enemy outside enemy spawn. Add immediate min(HP,damage) and the existing
stun value to camp value; otherwise retain the spell. No enemy nearby preserves ordinary farm
selection. It queries a bounded enemy list once per ready attempt after movement. Legal centers,
cast/movement permissions, dispatch, reset/basic and normal champion combat are unchanged. There
is no free-center expansion, lethal-camp exception, radius tuning or new forecasting model.

All-OFF source `b7012d456779` is exact raw/injected v42 SHA5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b.
Mixed source `8db1f27f8aa7`. Root-approved48-game pilot `ryth-mixed-pilot-r1`: same6 training
maps/both sides/v42+starter, BASE+mixed only; old hold arm is not repeated. BASE18/24→mixed22/24;
v42 6/12→10/12 (+4/-0); starter12/12 both. Margin change+92.2±73.3 SE, McNemar p=.125. Every own
health flag0; peak91668. Training outcome is promising, not qualification.

Decision: unchanged mixed allocation proceeds to all40pool/bothsides/v42+hunter7,320 full games,
`.cache/contenders/warband/sweeps/ryth-mixed-pool-r1`, exec session97417 active. Pilot22922 is
closed. Hunter7 is supporting same-lineage pressure, not an independent benchmark. Default stays
all-OFF; old hold candidate and every historical source identity remain preserved.

Mixed full pool completed320/320 healthy: v42 BASE40/80→46/80 (+17/-11); hunter7 66/80→69/80
(+5/-2); total106/160→115/160. Margin change+47.7±39.0 SE,p=.175,peak94166. This ties the old
hold arm's win totals with different flips and slightly lower margin; it is not an independent
sample or demonstrated improvement over hold. Each separate panel retains its identical-byte
baseline, exact keys and opponent identities.

Mixed fresh legacy completed136/136 healthy: v42 BASE17/34→15/34 (+4/-6), hunter7 30/34→31/34
(+4/-3), pooled47/68→46/68. Margin change+81.4±65.9 SE,peak91648. Combined pool+legacy mixed
is61/114 against v42 versus BASE57, and100/114 against hunter7 versus96. Fresh win confirmation
remains absent; no promotion. Root approved unchanged mixed confirmation against frozen v41,
all40pool+17legacy/both sides, BASE+mixed228 full games:
`.cache/contenders/warband/sweeps/ryth-mixed-v41-r1`, session61239 active. Prior session12066 closed.
This tests whether benefit extends beyond v42's ready-spell response; version differences mean
it is not a causal ablation. Current main was verified exact live v42, so it is not replayed.

Exact action-cost caveat from Micro's Split A/hunter7 r604 loss window: both arms apply180 total
HP damage (common90 basic, then90 AoE versus90 reset continuation). ON gains stun but loses its
escape movement; OFF resets and moves(19,30)→(18,29). The retained spell is not free added DPS.
Mixed scoring is deliberately the frozen simple heuristic: static can_stun(type) excludes known
type immunity, but it does not discount an already-stunned target, live immunity or spent actions.
The min(HP,damage) term also omits shield distinctions. These are limitations to explain after
confirmation, not silently repaired knobs/code in the running cohort.

Frozen v41 confirmation completed228/228 full games healthy: BASE50/114→mixed51/114,
flips+13/-12, margin change+14.3±52.0 SE,p=1.0. Pool37/80 for both, legacy13/34→14/34. Peak94166.
This does not establish benefit beyond the v42 matchup, and it is not a causal ablation. All
mixed results still use `8db1f27f8aa7`; all-OFF `b7012d456779` remains raw/injected exact live v42.
The current working main was verified by root as `main/main@0353a987e04f`, identical to frozen v42.
No extra current-main matches add information.

Final independent explanatory sample: `results/contenders/micro/ryth-reservation/assessment.json`
and canonical foundation U8. Four balanced gain/loss keys confirm later enemy AoE delivery with
extra camp/own HP cost, an unused-ready spell loss, and equal damage with different movement/stun
packing. Ready CD0 does not guarantee available casting, extra DPS or a terminal win. Enemy intent
and private forecast path are unobserved. This explains possible mechanisms without adding win
samples or claiming the held and mixed policies are independent experiments.

Decision: retain both spell policies as promising/uncertain OFF candidates, not a broad competitive
upgrade. First three packages and reward raiding remain OFF. No release or online action. Current
default stays `b7012d456779`, all source files LF. Mixed pool97417, legacy12066 and v41 session61239
are closed, as are every earlier owned build/game handle. No jobs or servers remain running.
Await coordinator's next structural direction rather than adjusting radii or weights on these maps.

## Retreat walk intent

Root selected a standalone action-allocation test from the observed cast/step tradeoff. Melee
retreat calls fight_enemies(false) before its escape dash/walk; the enemy spell chooser ordinarily
gets want_move=false there, so a movement-exclusive cast may consume an intended exit.
`control_retreat_walk=true` passes fresh can_move as explicit fifth argument, used only under
that knob to OR into existing want_move. Existing metadata filters exclusive spells; melee
basics, compatible spells/resets and the later escape dash/step are unchanged. It reserves an
allowance, not a proved improving exit. Compatible spells can still consume the shared cast.
This is a tradeoff hypothesis, not an unconditional correctness fix.

48 full-game pilot `retreat-walk-pilot-r1`, original6 maps/both sides/v42+starter, all prior
experimental flagsOFF. BASE `e9e07a8dc4c7`, ON `d89e1fc321b4`. v42 BASE6/12→4/12 (+2/-4),
starter12/12 both; pooled18/24→16/24, margin-131.6±110.2 SE,p=.688. Every own health flag0,
peak91638. Do not advance this losing arm to full pool; keepOFF and preserve its failure.

The fifth API argument changes module bytes (OFF raw SHA094c95c38cb7685395d0e4ef31c3e09d38b832f04c350bca5d390d82dcdcd821).
The prior and current OFF controls have SHA-identical inner gameplay replay payloads on all24
matched full-game keys, plus identical terminal gameplay. No binary-order project or extra games:
`results/contenders/warband/map-control/retreat-off-replay-control.json`. This is replay preservation
over those24 keys, not a raw-module identity claim. All sourcesLF. Current default all-OFF
`e9e07a8dc4c7`; pilot session49909 completed; no owned game/build/server/observer jobs remain.

Root allowed one final physically grounded trial, same flag and no other exceptions. It previews
at most8 existing legal neighbors and selects the best existing retreat score above staying;
only a candidate that also strictly reduces the existing depth-weighted threat reserves a walk.
Closer home/ally alone does not suffice. Later escape dash/step remains unchanged, so the preview
is not binding or a guaranteed exit. Fast read-only review found no obvious legality/control-flow
mistake. No new search/model/weights or movement policy.

`retreat-step-pilot-r1`:48 full games on the same6 training maps/both sides/v42+starter. BASE
`aa91f76fc2d0`, ON `a20a7305a215`. v42 BASE6/12→5/12 (+2/-3), starter12/12 both; pooled18/24→
17/24, margin-28.0±82.8 SE,p=1.0. All own health flags0,peak91659. Per root's stop condition,
the retreat branch is CLOSED: no broad pool, predicates, exceptions or tuning. Keep flagOFF.

Current all-OFF `aa91f76fc2d0` is raw/injected identical to replay-verified `e9e07a8dc4c7`, binding
the accepted24 actual gameplay replay matches. Proof `results/contenders/warband/map-control/
retreat-step-off-binding.json`; no raw-v42 module identity claim. Every sourceLF. Session11362
closed; all prior owned builds/games/servers/viewers/observers are closed. Held/mixed candidates
remain unpromoted, all experimental flagsOFF, and all durable records remain in the existing index.

## Actual local parties: game model before policy (23 October)

The renewed user direction rejects another objective or threshold package. The underlying
limitation is that ordinary `score_group()` counts published/hypothetical allies as damage and
health before arrival, while `force_sufficient()` can accept a headcount or any already-awake
camp. Neither establishes a feasible action schedule or individual survival. Historical party
algebra is conditional on actual contributors and covered damage; foundation E31's native
rotation is the applicable narrow mechanic. The unfinished altar-warden scaffold stays OFF.

`control_actual_party=false` introduces a locally observed work contract, not map-wide recruitment.
Both named melee bodies must already publish the same camp and stand adjacent to its sole harmful
Elk. Personally visible harmless members may remain. Exclude Varg splash, a third actual adjacent
body regardless of its slot, enemy or unknown incoming damage, channel/stun/unready participants,
unequal mitigation and an unsupported footprint. Current per-body basic-only bounds consider
either first future tank, rotate subsequent damage toward the healthier body, and retain two
volleys for escape. Optional spells and stuns earn no forecast credit.

The last three shared bytes encode group+1 (6 bits), two-actor mask (5), absolute epoch (11),
and separate ACKs (2); at most five actors leave these bytes clear of slots. The inherited SOS
and sightings protocols are mutually excluded. ACK communicates a duty, not a hit. Before both
ACKs, a participant owns only a conservative one-turn stationary action with its own exit reserve;
ordinary low-health recall is not suppressed. Real state, basic readiness and geometry are checked
each owner turn. Execution reuses the native-verified stationary basic/spell/reset seam. Current
casts are optional applied work. Priority settings affect the next round, and transient self-only
writes must not cancel the accepted party whenever the tank changes.

The duty owns safe continuation before ordinary health/tribute handling; enemy threats still
preempt. It cleans harmless members, physically vacates the helper, waits for personally known
helper vacancy, designates one capturer, and releases on observed reward/respawn transitions.
Every nonparty or failed turn releases its exact shared duty. Shared release and priority restore
are attempted independently after local intent is invalidated; the original error reaches main's
error/yield boundary. Death defers a self-only priority restoration until alive. Engine cleanup
failures remain health failures, not successful-release evidence.

Three successive 16-game diagnostic cohorts used Elkring and Ambush, both sides, exact v42 and
starter, full games and retained statistics/replays. They are eight paired keys per generation,
not 48 independent samples. R1 exposed priority handoff/acceptance defects; R2 corrected those;
R3 adds unknown-helper vacancy and exception cleanup. R3 `ab3ec406e899` BASE versus trace-ON
`e0e21bc6ae4d` finishes 16/16 healthy: v42 2/4 each, starter 4/4 each, no flips, pooled margin
change -3.2 +/- 11.8 SE, peak fuel 91,645. No strength gain or full-pool expansion follows.

Current-engine native windows in two R3 normal Elkring games establish actual repeated basics,
casts, priority writes and per-body damage, followed by sole-recipient 110-gold/22-gem captures:
Ryth on A at R79 and Marcella on B at R161. A's pair contribution ends before a third actor's
30-damage final-clear help at R72. B has pair-only represented target-camp damage. Neither case
demonstrates below-50%-health continuation; numeric priorities are unqueried. The helper departs,
but a complete new banked production cycle and advantage over matched OFF work remain unknown.

Final current default `e5c2eb026465` is raw/injected identical to accepted `aa91f76fc2d0`, SHA256
`094c95c38cb7685395d0e4ef31c3e09d38b832f04c350bca5d390d82dcdcd821`; prior 24 full replay controls
therefore bind. All new state is lazy and enabled-only; every source is LF. A final error-boundary
correction places begin/inner/finish inside one catch and independently attempts both cleanup
obligations. Three focused tests pass, including injected first/second/both cleanup failures.
That correction is build/test qualified only; it never relabels R3 gameplay. Final untraced ON
`896f4080a7aa` builds (raw/injected SHA256 `f4b8cbb56b83afc2f0aeba8888104caa985eb1a7e7085ef58943b811c01b7bfa`).
At that historical checkpoint it had no game qualification; the next section closes that gap.
All experimental defaults remain OFF.

Durable evidence, all three game generations and exact bindings: `results/contenders/warband/
local-party/assessment.json`, `native-r3-analysis.json`, `final-source-binding.json` and
`untraced-build-final.json`; the existing Warband index links them. The then-next discriminator
was matched OFF useful-work accounting plus untraced autonomous contribution; the next section
records that follow-up. A naturally useful below-half-health continuation or actual preemption
remained unobserved. No online action occurred. All owned
build, game and native-observer sessions are complete; no server, viewer or helper remains.

## Untraced party transfer and displaced labor (23 October)

The renewed ownership resumes the existing mechanics question rather than creating another
pressure threshold. `actual-party-untraced-r1` runs the final cleanup-corrected untraced ON
`896f4080a7aa` against same-run default `e5c2eb026465`: Elkring and Ambush, both sides, frozen
live v44 and starter, 16 complete native games/eight paired keys. Both arms finish 6/8,
including 2/4 against live and 4/4 starter. There are no flips; paired gem-margin change is
-3.25 +/- 11.84 SE. Every measured subject and opponent fault, exhaustion and error is zero;
the subject summary also has zero stalls/suspension. ON peak fuel is 91,673. This is bounded
execution qualification, not competitive improvement or full-pool health.

All sixteen complete uncompressed `replay.repl26a` gameplay payloads match their corresponding
R3 traced-source payloads byte for byte, without masking. Raw and injected frozen v44 modules
also equal the former v42 counterparts (SHA256 `5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`).
The live rename supplies no fresh opponent or independent strategic evidence. Exact gameplay
equality transfers the represented R3 applied contribution/release/capture interactions to
the final untraced candidate on these keys; private state, stdout and fuel are outside that
equality. The saved witnesses were analyzed without new native observers; the transfer uses
the final-candidate games rather than additional R3 games. The final untraced games have their
own directly observed technical health.

The full new replay/statistic bodies and report are preserved under
`results/contenders/warband/local-party/untraced-r1/`. `qualification.json` retains every exact
gameplay binding, both-team health, module hashes and interpretation. Existing assessment and
Warband index link the result. No gameplay source or default changed. The synchronous sweep
exited zero; no observer, server, viewer or background helper was started.

The pure matched labor audit closes the previously missing payout-to-bank suffix, using the
two selected full Elkring/starter side keys. Native R3 sole payouts join to CAMSTAT and round
timelines: A Ryth2 receives 22 at R79 and submits 22 at R87; B Marcella6 receives 22 at R161
and submits 22 at R196. Both bags are zero before payout, then 22, then zero at submission;
the team score rises by 22, with no intervening recipient collection, submission, loss or death.
This is an observed resource-conversion suffix, not token identity or another native channel
query. A still includes the third actor's final damage; no exclusive pair-only renewal follows.

The same OFF groups already pay: A at R82 to Vendetta3, and B at R168 split 11/11 between
Marcella6 and Vendetta8. Earlier local completion and recipient concentration do not establish
new useful supply. Team collected gems at R200 are equal, 231 on A and 176 on B. Team banked
at R300 is A 330 -> 341, B 308 -> 308. Actual winning completion becomes later, A 609 -> 650
and B 687 -> 710. Per-body common-horizon totals expose redistribution: at R300 A helper
Vendetta's collected/banked gems fall 88/55 -> 44/44 while its pair rises 110/77 -> 132/132;
B helper Ryth rises 44/44 -> 55/55 while its pair falls 121/121 -> 99/99. These do not price
an unobserved independent job. OFF movement/travel and private job choices remain unobserved;
the complete optional arm, including B's earlier R145 allocation divergence, is the contrast.

`results/contenders/warband/local-party/labor-audit.json` preserves exact actor/event/timeline
joins, per-body/team common horizons, source and replay hashes, plus content-addressed durable
copies of every consumed input under `labor-inputs/`. Its pure producer is
`.cache/contenders/warband/labor-audit.py`; it exits zero and launches no game/query/process.
The result is also bound to final untraced gameplay by the exact payload qualification above.

Decision: keep OFF and close this local-party activation study. Conditional legal execution and
bank conversion are established; a complete party architecture has not earned its displaced
labor. No broad pool expansion, activation tuning or additional payout witness follows. The
next structural discriminator must show actual cooperation completing valuable work unavailable
to the independent alternative, with the helper's own work and release/recovery accounted for.
Naturally useful covered low-health continuation remains a possible seam, not a requirement to
manufacture another test. No source/default was changed and no main/anchor source was read.

## Successor selection: conversion access, with missing premises (23 October)

A bounded read of existing E31/native evidence, current engine-decoded geometry and the atlas
does not yet justify a new complete architecture. Persistent damage invalidates a universal
"cannot solo" claim: the relevant comparison is useful conversion within a common finite
horizon, including independent partial siege, return and recovery. Mosaic reports its constructed
dense four-wolf case remains solo-completable with recovery; this is not binary party-only
supply. Warband's distinct question remains named cooperative capacity and survival, rather than
Mosaic's restricted one-body topology/kit ranking.

The nearest task within E31's single-Elk scope is Quadrants `g_598`: one Elk and three Gem
Clusters, 2,760 total HP, 36 gems/90 gold. The historical atlas computes a two-step approach from
the A spawn rectangle and an eleven-step cleared-group-to-altar walk. These are lone-walker
static distances, not two actual bodies' arrivals, collision-free attack positions or a safe
loaded exit. No saved named pair/kit state and independently available alternative job establish
that one-sortie cooperation buys more useful conversion than two independent partial-siege
cycles. Another prepared Elk payout would repeat the already settled mechanism.

A structurally different task family is the ordinary altar-access dependency on BossBattle.
Current decoded geometry has sole altar `(25,20)`. Of its eight neighboring cells, only `(24,20)`
has landable terrain: `(24,19),(25,19),(24,21),(25,21)` are holes and all three cells at `x=26`
are walls. The unique potential landing is occupied by Monolith native20 at `(24,20)`, alongside
native27 at `(23,20)` in group native16/`g_1023`. Current group value is 400 gems/600 gold,
two Monoliths at 20,000 HP, 200 Strength and range5 each. Static atlas engagement distance is
18 from either spawn rectangle; ordinary closed-map altar distance is unavailable, open-map
distance19. This selects a possible conversion-access task, not a feasible party schedule.

The old P5 wording (Gem2+Monolith2/414 gems and needing both Monoliths dead) does not match this
current composition or establish the necessary amount of work. M49's historical observation
that individually dead members disappear from `camp_tiles` makes a narrower hypothesis real:
kill only blocker20, occupy its tile and tribute while27 remains alive. It still needs current
dynamic occupancy/action confirmation. Damage does not cancel tribute; survival through the
remaining Monolith's actual volleys could therefore matter more than the whole group's payout.
Ordinary move, dash and crystal require legal unoccupied landings, so their documented rules
provide no initial alternative among those eight cells. Actual special-kit movement/teleport
effects and submission rules must still be checked before making a universal access claim.
This is an ordinary-access/conversion dependency candidate, not proof that every legal kit must
kill the whole group or that cooperation is necessary. The foundation owner has the correction.

Exact inputs: current CLI/SDK3.0.5, engine1.0.3; `maps/01boss-battle.map26a` SHA256
`53086e249294aed6f427fe647d578303589a081e953c62a0ff8f20cade9993fa`, `maps/00quadrants.map26a`
`261fe6c133f076653bda84e88965dc35c5a59a70e7730082b4c6df9ca892df92`. `maps/geometry.json` SHA256
`05b3cb828d665369edb7310bca6d4d6b6207fbb2c2f0d6dd40c89013a9aef7d2` names decoder engine
`029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48` and matches that BossBattle
map hash. `docs/reference/runtime-catalog.json` SHA256
`7cc2c639c314d0c739ad74e0fc20ade667304276a6040c014f92f5915f0b1d87`; bundle stamp
`docs/reference/current-version.json` SHA256 `1c74c8a5b6039652edb173e050f8dcf6cdab620100b683268371c4ebb3537347`.
The static atlas SHA256 is `2ecaa961b2069c5316089b474aa83b86dc63eff9db653193b3e90ae66843f040`.
`arena map 01boss-battle 20 17 28 23` reads the exact landing neighborhood. No new native game
or observer was launched in this selection pass.

Missing premises are concrete: an organically reachable named set of champions with actual
HP/kit/cooldowns, funded purchases rather than granted gear, legal arrival and post-action
individual damage/escape schedules against both live Monoliths, current one-member-death
landing behavior, a survivable loaded tribute, and personally available independent jobs with
their own travel/recovery/conversion outcomes. E31 rotates one Elk; it does not certify two
simultaneous Monolith targets, sum their HP into a party budget or cover unknown enemy pressure.
The empty kit/alternative premises block a successor, not a general inability to cooperate.

The minimal future discriminator is a fixed current-engine state with those premises supplied
by ordinary play, then a common-horizon comparison of a complete named cooperative access plan
against the same bodies' actual independent jobs/partial siege. Count usable bank conversion,
both bodies' remaining HP/location/bag, approach, helper release and return/recovery. Do not
disable legal solo abilities, erase partial damage, grant free helpers or count a static
walking obstruction as a teleport impossibility. No such fixed meaningful comparison is ready
in the retained evidence. Stop here: no new wrapper, native panel, instrumentation or source
change; local-party activation remains closed and every experimental default remains OFF.
