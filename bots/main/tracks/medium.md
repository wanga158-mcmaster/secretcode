# Medium track: planning and decision structure

Updated 25 Oct 2026 ~22:00 UTC. RESUMED by kickoff. Previous note (T34-T43, all closed UNKNOWN or
negative): `.cache/main/med/medium.md.before-20261025T2200`.

## Authority
- Goal: first place by the 4 Nov international qualifier; medium raises the ceiling through better
  decisions and new mechanisms (planning above fight micro).
- Own: main/{squad (current candidate), med, cand, cand3, press, gp, gp2, link, team, skim}; archetypes
  main/{gank, ball, rush}. Live main/main, releases and online belong to the short/release agent.
- Outside bots/main read only docs, sdk, maps, tools, .tools, .cache/main, results/online,
  results/releases; write only .cache/main and results/online. Never read other bots' source.
- Scratch .cache/main/med (scripts in scripts/). Private snapshots, no shared reuse. No online commands.

## Approach (changed 25 Oct after ten closed attempts without a game result)
- Evidence = paired win rate + health, as short now uses: screen on pool+train vs the panel
  (v53, hx1 hunter sparring, hunter7, v53 focus-builds; both sides), claim on heldout/fresh with
  >= 1,500 paired keys. No mechanism-forecast kill gates; no provenance ceremony.
- Candidate = main/squad = v53 source (copied 25 Oct ~21:40) + default-off knobs. Patch vs main/
  posted here when a candidate beats v53 on unseen maps.

## Facts established this session (local, engine 1.0.3)
- v53 beats every local archetype: ball 95%, gank 88%, rush 86%, hunter7 89% (pool, both sides). Ball
  farms 1/3 of v53's monster damage: it fails on economy, not deaths.
- Speedrun vs passive starter 972 rounds (v44 977; solver plan ~745). Top-vs-top online games last a
  median 1,017 rounds (1,117 games) UNDER constant pressure (invades, ganks, skirmishes), so their real
  farming is far faster than ours: farming throughput is a top-priority gap (user, 25 Oct).
- 00lanes trace: 13.4 recalls per champion in 1,047 rounds (median 86% HP, 60 gold), ~30-round
  walk-outs = 416 rounds per champion. But removing recall rules costs strength (capture recall
  -47/480 p .001, shop recall -27 p .049) while speed changes only 7-37 rounds: items pay in fights.
- Mirror v53-v53: the side with the better kill margin wins 330/395 decided games (kill margin +5 vs
  -6). Party size at clears does not follow group size: elk/golem groups cleared solo 45-50%, small
  groups with 2+ champions 35-45% (6 maps). score_group divides reward by members, so a planned party
  only beats a solo when the solo is infeasible.

## Results this session (panel screens: 1,920 paired games each, pool+train, both sides)
| Mechanism (knob, default off) | Result | Decision |
| --- | --- | --- |
| H1 cohesion: rate x(1+w) when an ally's published target is within R (cohesion_weight) | w .3/.6/1.0: -119/-154/-233 net wins (p<.001), +87..145 rounds slower (crowding), 14-25 fuel exhaustions (inflated bounds score more groups) | REJECTED |
| H2 route-aware banking: bank when the nearest altar is <= D extra tiles toward the target (bank_route_detour) | D3 +18 (p .26), D6/min1 +16 (p .39), margin +11..17 (SE 7-8), no archetype worse | neutral; combinable |
| H3 marginal join: joining committed allies valued by their rounds saved (marginal_join) | -82 (p<.001), +58 rounds slower; with 2 hypothetical joiners -68 | REJECTED as-is |

Surprise: both structural score changes slowed the economy ~60-100 rounds. Hypothesis: v53's 94 ES-trained
weights are co-adapted to the current score formula (C2 needed co-training too), so any reshaped score
breaks the fit. Test running: `main/med-l0`, the same mechanisms on learned=0 (v44 rules).

Also closed: early grouping on v53 (party3 +26 n.s., pairs150 -44, ball150 -131); escorted packs
(pair 21%, trio 10%, ball 8% vs v53; followers idle in escort); learned=0 refutes co-adaptation
(coh -79, mj -88, bank +7); hx1 losses: 118/141 deaths are long losing contact episodes.

Pressure (40 pool games each, side A, per champion by r500, vs starter/v53/hx1): camp fight 187/157/152,
travel 181/181/162, pvp 16/33/43, dead+channel 40/63/80 (`med-pressure`). Draft: fixed roster alternatives
all <= v53's (vs v53: Vesper-2nd -3, Tavi -2, Varg-1st -23, Cassi -113); oracle best-of-5 per key only 934 vs
972 incl. noise, so a map-adaptive draft is worth <= ~30 rounds with this planner (`med-draft1`).

Base note: worker copies (23:00) carry main/main's v54 working defaults (hazard_threshold 0.3, D360); squad is
v53 (0.5). Integration targets current main/main; compare against both main/main and anchors/main-v53.

## Parallel workers (user request 25 Oct ~23:00 UTC; each owns one folder copied from v53)
| Folder | Question | Distinct from |
| --- | --- | --- |
| main/pack | situational grouping (group for safety/big groups, split for farming/vision); no fixed squads | escort packs |
| main/farm | farming throughput: time-use decomposition, clear efficiency, heal/shop trips, respawn timing | short's s-route |
| main/open | deterministic team allocation in the opening (party sizes, carrier captures) | short's s-open (pace/shop) |
| main/proxy | top-team proxy opponent (>=60% vs v53) so the panel can reward grouping | gank/ball archetypes |
| main/engage | fights: take the fights we should, win the ones we can | short's s-safe (death avoidance) |
| main/squad (me) | integration of positives (route banking neutral +), ES co-training; adaptive caution REJECTED (-6..-22/1920, slower) | |

## Worker results
- engage (DONE): finish_kill=2 + kill_chase=6 (main/engage, knobs default off; off = main/main row-identical).
  pool+train 1920: +201/-123 p<.001, kill margin +3.06; heldout+fresh 1680: +178/-107 p<.001, KM +3.35;
  per opponent all >= base (v53 +90/-49, hx1 +62/-39). Caveat: heldout alone +37/-36 (KM +3.25), legacy
  +15/-15: unseen gain is mostly fresh maps. Together only: fk2 +11, kc6 +23, both +78. Includes a
  camp-refresh fix after AoE/splash (stale 'attack camp failed' errors). Census: in 25-30% of favourable
  contacts we kept farming; low enemies within 2 went unhit 15-24%.
- pack (claimed): ally_help=12 heldout+fresh 1680 +41 net (p .033), margin +33, kills 8.9->10.0, deaths 6.4->5.5.
- farm (confirming): capture_recall_walk 1 + gem_value_model + danger 0.3: ~-40 rounds unopposed, +51 net p .008.
- proxy: p3f ~56% vs v53 (threat_focus carries it; Vesper drafts worse on our chassis).

## Integration (26 Oct)
- main/combo2 = current main/main (incl. short's route_chain, default off) + three-way merge (base = main/main
  snapshot b911765ab70b, the workers' copy point; .cache/main/med/base-2329) of engage, pack, farm. Clean merge,
  builds. Knobs all default off. Candidate sets: E = finish_kill=2+kill_chase=6; P = ally_help=12;
  F = capture_recall_walk=1.0f+gem_value_model=true+capture_recall_walk_danger=0.3f.
- pack FINAL (medpack-11, heldout+fresh 1680): ally_help=12 +198/-152 p .016, margin +33, rounds -17, clean.
- farm FINAL (medfarm-conf1, heldout+fresh 2100): F pace vs starter -36+-5 rounds (956 vs 993), wins +21 n.s.,
  every opponent >= base, margin +16, clean. Mechanism: capture recall values the walk back fully, except near
  recent enemies (0.3); gem value from the item model.
- `main/med-combo1` (pool+train 2400, panel + main/main): mechanisms ADD: E +111, EP +151, EF +167,
  EPF +223 net (1729->1952 p<.001), margin +117, rounds -98. EPF vs v53 324/480 (67.5%, +159/-82), vs main/main
  66% (+147/-70), vs hx1 +104/-59, hunter7 +31/-11, focus +23/-19. Blocker: 1 fuel exhaustion (EPF,
  g1-131-00road_of_riches B vs hx1, r354: farm phase 58K stacking choose+fight+travel; v53's known heavy-farm
  fragility). Claim + guards running: `main/med-claim1` (heldout+fresh+legacy; EPF, +fuel_reserve 16000,
  +choose_hold_fuel 28000).
- Workers stopped by accident 26 Oct; open and proxy relaunched with handover briefs; pack/farm finished by me
  (farm unseen confirmation `main/medfarm-conf1` resumed).

## Farming target: <= 700 rounds unopposed (user, 26 Oct)
- Now ~956-970 vs starter. Method: search-based team planner, not tuning (NP-hard scheduling).
- main/plan (copy of current main/main): `econ::plan_team` interface in plan/src/econ_plan.h (lead owns);
  planner worker owns econ_*.{h,cpp} + offline native harness (.cache/main/med/plan/planner): calibrated
  surrogate (clear time/damage per party, BFS walks, capture/recall/shop/tribute) + deterministic fixed-budget
  search (greedy+rollouts/beam/local), all instances compute the same plan. Executor worker owns the rest of
  plan/: knob team_plan (0 = main/main bit-exact), task queue drives v53's execution layer (travel, fight_camps,
  capture, tribute, shop), party staging, deterministic replanning on drift. Lesson D335: party arrival sync and
  executor weakness, not the schedule, broke the previous attempt.

## CANDIDATE M1 for the release agent (26 Oct): EPF on v55
- Patch against current main/ (v55): `.cache/main/med/candidate-epf.patch` (= main/src -> bots/main/combo2/src;
  code only, every new knob defaults OFF; OFF verified bit-identical to main/main on 160 pool keys, every row
  field, `med-off1`). Turn on: `finish_kill=2 kill_chase=6 ally_help=12 capture_recall_walk=1.0f
  gem_value_model=true capture_recall_walk_danger=0.3f` (capture_handoff stays 1).
- Mechanisms: finish_kill/kill_chase (engage: kill what this turn's burst covers, chase channeling or
  2-hit-killable focus targets to 6 tiles; camp refresh after AoE fixes stale camp-attack errors); ally_help
  (pack: walk to an ally in view within 12 tiles that is in contact); farm (capture recall values the walk back
  fully except near recent enemies, item-model gem value; -36 rounds unopposed).
- UNSEEN evidence `med-claim2` (heldout+fresh+legacy, both sides, 2,270 paired keys; v55 = combo2 all-off):
  +534/-248 net (1,390 -> 1,676 wins, p<.001), margin +137+-12, rounds -97+-11, own health clean.
  vs v53 74.4% (+137/-56), vs v55 head-to-head 63.9% (+138/-75), vs hx1 +91/-43, vs hunter7 +17/-6,
  vs proxy "hea" 50.4% (base 32.2%; +151/-68). Pool+train (`med-combo1`, pre-v55 base): +223 net of 2,400,
  1 exhaustion (r354 farm phase 58K; matches v55's shared rare exhaustion, D362).
- Variant M1b = M1 + opening_party=3 + threat_focus=true: +566/-242, 75.3% vs v53, 54.8% vs proxy, but one
  exhaustion (g777-029-01boss_battle A vs hx1, 1197 rounds): needs fuel work before use.
- Requests: (1) independent verification + CLI gate on the patched main; (2) online A/B of M1 vs v55 against
  ranks 1-30 (the local panel is our lineage plus the proxy); (3) note the rare shared exhaustion is not fixed by M1.

## v55 context and claim (26 Oct)
- Short released v55 = v54 + capture_handoff 1 (D362; s-route); v55 shares a rare exhaustion (1/4,994,
  delegated to short's s-fuel), which matches the single EPF exhaustion in med-combo1. combo2's knobs-off
  base had capture_handoff 0, so combo2+capture_handoff=1 == v55 + worker code.
- proxy DONE: `main/proxy+opening_party=3+threat_focus=true+capture_handoff=1+finish_kill=2+kill_chase=6+ally_help=12`
  ("hea") wins 69.6%/67.5% vs v53/main pool+train (heldout 65.8/57.5), clean; mostly engage+pack+party+focus.
  Hunting lone farmers lost in every form. Now a panel opponent.
- Claim `main/med-claim2` (heldout+fresh+legacy, vs v53/hx1/hunter7/main(v55)/proxy): base(v55), EPF,
  EPF+opening_party3+threat_focus, EPF+fuel_reserve16000.

- open DONE, negative (stop rule, 5 failures): deterministic opening list schedule (plan_open, default off;
  OFF == main/main). pool+train 1920: until60 -8, until100 -13, +carrier -8 (n.s.); pace -2..-4+-3;
  bigger parties -11 / -67 (p .004). Plan executes as built (00lanes elk party r46) but solo elk tasks end
  in heal recalls (r58-60) and later tasks expire; larger parties lose to late arrivals (D335 again).
  capture_handoff alone -15 pace (already v55).

## Next leads
- If mechanisms help on learned=0: co-train weights with the mechanism on (ES, short's es.py driver).
- Threat-aware zones: avoid lone targets where enemies were recently seen / where allies died.
- Diagnose v53 losses to hx1 (the pursuing hunter, 38% vs v53): death modes (farming vs fighting).

## Lessons
- Distinguish capacity, delivery and strength; but decide on paired wins at scale, not on small gates.
- Farming throughput is a top priority (user correction: unopposed 972 vs top teams' contested 1,017 is
  not comparable). Fights are a second gap: we decline fights we should take and lose fights we could win.
- Never fixed pairs/trios/balls: grouping must be situational (group for safety, split for farming/vision).
- Logging and diagnostics alter play; keep knobs default-off and OFF bit-exact.
