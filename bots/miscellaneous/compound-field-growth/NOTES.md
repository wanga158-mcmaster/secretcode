# Compound: practical capital contender

The previous independent implementation is retired at `bots/miscellaneous/compound/` by the
coordinator. Parked `_compound-*` finite-episode/scout/simulator research is STOPPED; preserve it,
do not resume it. Its exact source-bound evidence remains in `results/contenders/compound/`.

This new bot starts from frozen `anchors/main-v42@9fc5d7469074` execution (20 source files).
Owner: compound agent. No online actions, anchor edits or release are authorized. The goal is
an economic contender that turns income and additional workers into practical whole-game wins.
Measure coherent policies first; explain promising results or conspicuous failures afterward.
Hard limits, bounded fuel and default-OFF preservation remain. No new pricing certificates.

## First package tournament

Run `.cache/contenders/compound/packages-v42-pilot`, exec session21788 completed exit0.
Baseline source `contenders/compound@ad71190353b1`; existing defaults unchanged. No custom code.
96 full games: baseline + three packages, six maps, both sides, opponents frozen v42 and starter.
Maps: `00ambush,00eight,00gem-fields,00lost-world,01big-world,01corridors`.
Opponents: `anchors/main-v42@9fc5d7469074`, `anchors/starter-cpp@3785135fc7c7`.
Trace/debug/scores OFF. Poll the existing handle only; never restart after model/tool failure.

| Package | Economic composition and buying | Returns and bags |
| --- | --- | --- |
| baseline | inherited v42 draft8 and Treasure-first shop7 | inherited value returns, bag25 |
| workers | draft0, Cassi & Issac extra worker; Jungle-main then Treasure shop6 | opening90g returns, later180g; bag60/danger15, nearby2 |
| factory | draft19, Heart/Vesper/Marcella early; Treasure-first shop7 | opening150g returns, later300g; bag120/danger25, nearby1 |
| durable | draft1, Heart/Ryth/Marcella/Ren/Stellar; Treasure-Health/Jungle/health shop14 | opening90g returns, later180g; bag20/danger10, nearby6; heal at60% |

Exact overrides (baseline has none):

```
workers: draft_mode=0 shop_plan=6 shop_recall_mode=0 early_shop_gold=90 early_shop_until=400 shop_recall_gold=180 shop_recall_detour=12 tribute_gems=60 tribute_gems_danger=15 tribute_detour_tiles=2 passive_rewards=true
factory: draft_mode=19 shop_plan=7 shop_recall_mode=0 early_shop_gold=150 early_shop_until=600 shop_recall_gold=300 shop_recall_detour=20 tribute_gems=120 tribute_gems_danger=25 tribute_detour_tiles=1 safe_recall_frac=0.35 passive_rewards=true
durable: draft_mode=1 shop_plan=14 shop_recall_mode=0 early_shop_gold=90 early_shop_until=400 shop_recall_gold=180 shop_recall_detour=20 tribute_gems=20 tribute_gems_danger=10 tribute_detour_tiles=6 recall_health_frac=0.60
```

Fast read-only review confirms actual draft/shop/tribute/return consumption. One requested setting
is inert: `safe_recall_frac=.35` only raises the inherited `.50` fraction, so factory's effective
threshold is `.50`, not `.35`. Preserve the measured package as launched; do not rerun/tiny-tune.

All96 full games completed, engine1.0.3. No subject faults, exhaustion, errors, zero-clear stalls
or suspension. Max fuel91,642. Results are evidence of these selected training maps, not a gain.

| Arm/source | vs v42 W-L; margin | vs starter W-L; margin | Starter completion rounds |
| --- | --- | --- | --- |
| base `ad71190353b1` | 6-6;0 | 12-0;+942.50 | 856.833 |
| workers `e81762780048` | 4-8;-319.25 | 12-0;+944.67 | 920.833 |
| factory `e04da97880e4` | 6-6;-206.08 | 12-0;+921.25 | 964.917 |
| durable `bb73549754e2` | 3-9;-416.50 | 12-0;+891.67 | 987.833 |

Factory flips +4/-4 versus baseline on12v42 keys; margin difference-206.08 +/-239.89SE.
It is the strongest nontrivial package here, not an improvement. Workers flips+1/-3; durable+1/-4.
All12 starter games finish in each arm. Paired completion differences versus baseline:
workers+64.00 +/-25.03SE; factory+108.08 +/-42.38SE; durable+131.00 +/-36.77SE (slower).
Fast review confirmed all package knobs consumed except factory's inert safe recall setting.

## Full-pool follow-up (complete; factory rejected)

The unchanged factory is selected for the required broad test. Root confirmed frozen aggressive
`anchors/main-hunter7@417c0489eb1f` as the second opponent. It shares main lineage; do not call
it an independent implementation. No opponent-discovery project or small retuning.

Run `.cache/contenders/compound/packages-v42-pool`, exec session7611 completed exit0,320 full games:
same-source baseline + factory, all40pool maps/both sides versus v42+hunter7. Baseline/variant
remain `ad71190353b1`/`e04da97880e4`; traceOFF. All320 unique full-game keys complete.

| Opponent | Baseline W-L; margin | Factory W-L; margin | Flips | Paired margin change |
| --- | --- | --- | --- | --- |
| v42,80keys | 40-40;0 | 27-53;-211.70 | +14/-27 | -211.70 +/-88.62SE |
| Hunter7,80keys | 66-14;+489.16 | 57-23;+332.49 | +8/-17 | -156.68 +/-75.92SE |
| Pooled160keys | 106-54 | 84-76 | +22/-44,p=.009 | -184.2 +/-58.2SE |

All subject faults, exhaustion, errors, zero-clear stalls and suspension are0; maximum fuel
baseline93,864/factory95,092. Opponent faults/exhaustion/errors/suspension are0, but Hunter7
has ONE zero-clear game: factoryA on00gem-fields, factory wins at909, Hunter0gems/8deaths.
Replay `games/00gem-fields_compound_main-hunter7_81a159b1.repl26a`. Do not call the entire
opponent panel health-clean or use that win as evidence of general strength. The rejection does
not rely on it: excluding that matched key from BOTH arms leaves159pairs,+22/-44flips and
margin-185.30 +/-58.56SE. Primary320 rows remain unchanged. Pilot keys overlap this pool;
repetitions are not independent extra samples.

Decision: REJECT factory, retain unchanged v42 default, no legacy confirmation or promotion.
One next direction: direct productive-item tempo on the baseline melee roster rather than fine-
tuning this rejected Vesper/Treasure/large-bag package. The coordinator approved an economic-only
comparison and specified UNCHANGED baseline carrying for interpretability (see next tournament).
No causal attribution among the rejected package's draft/shop/bank components is claimed.
All jobs/handles21788/7611 are closed. NEW notes and existing durable index contain the results;
the old pricing/scout/simulator project remains stopped.

## Item-tempo comparison (complete; economic-only pilot)

Run `.cache/contenders/compound/item-tempo-v42-pilot`:72full games, baseline+two arms, same6maps,
both sides versus v42+starter, traceOFF. Draft8, bag25 and all banking settings remain baseline.
No code edits; existing working knobs only. Same-source baseline is `ad71190353b1`.
Exec78271 completed exit0; tempo `056ab7a28856`, adaptive `eb3f495ff362`.

```
tempo: shop_plan=99 shop_seq={435,35,630,-1,-1,-1,-1,-1} shop_seq_ranged={435,35,630,-1,-1,-1,-1,-1} shop_recall_mode=0 early_shop_gold=90 early_shop_until=400 shop_recall_gold=180 shop_recall_detour=12
adaptive: shop_plan=100
```

Tempo buys Jungle-main5 -> Monster-main5 -> Bruiser-main without limit and uses actual earlier
90/180gold returns. Adaptive uses existing adaptive purchases together with inherited adaptive
recalls (mode1), testing that executable pairing against fixed purchases/adaptive recalls.
Only a healthy competitive/improved arm advances; do not automatically broaden the least bad.
No detailed prices or finite forecasts before games. The measurement child owns its new handle;
never restart a native job after a model failure.

All72 full games completed. Baseline/tempo/adaptive win18/19/19 of24; versus v42 they win
6/7/7 of12. Tempo paired flips+5/-4, margin+33.08 +/-257.05SE; adaptive+3/-2,
margin+92.75 +/-178.73SE. All arms win12/12 versus starter. Starter mean completion rounds
856.833/891.083/878.833: tempo is34.25 +/-24.41SE slower and adaptive22.00 +/-20.14SE
slower than baseline. No detectable gain or faster-economy claim. All subject health flags0;
maximum fuel91,640/91,641/91,613. V42 foe health clean; starter zero-clear proxies3/2/2,
kept separate from subject health. Saved item events confirm tempo changes actual purchases:
median Jungle completion432->276 and Monster607.5->430; adaptive is mostly Treasure-first.
This does not establish that the items caused game outcomes.

## Adaptive shopping full pool (complete; rejected)

Run `.cache/contenders/compound/adaptive-shop-v42-pool`, exec77717 completed exit0,320unique
full games: baseline+unchanged `shop_plan=100`, all40pool/both sides versus v42+hunter7.
Source identities remain `ad71190353b1`/`eb3f495ff362`, engine1.0.3; traceOFF.

| Opponent | Baseline W-L | Adaptive W-L | Flips | Paired margin change |
| --- | --- | --- | --- | --- |
| v42,80keys | 40-40 | 36-44 | +16/-20 | +4.19 +/-69.00SE |
| Hunter7,80keys | 66-14 | 69-11 | +11/-8 | +85.54 +/-61.71SE |
| Pooled160keys | 106-54 | 105-55 | +27/-28 | +44.86 +/-46.3SE |

Baseline both teams health-clean. Adaptive subject exhausted fuel on TWO keys, both00rushbait-v2A:
vs v42 `games/00rushbait-v2_compound_main-v42_e7ab27e4.repl26a`; vs hunter7
`games/00rushbait-v2_compound_main-hunter7_3c8c36f2.repl26a` (same stems nativeJSON).
All other subject flags0. Adaptive's hunter7 foe has one zero-clear game on00gem-fieldsA,
`games/00gem-fields_compound_main-hunter7_1f670456.repl26a`; other foe flags0. Do not call
the entire panel clean. REJECT adaptive: no detectable strength gain, v42 wins fall, and fuel
failures block adoption. No overhead optimization or legacy confirmation; defaults unchanged.

## Tempo full pool (complete; rejected)

Root separately authorized the unchanged healthy tempo because its own7/12 pilot and positive
margin earned transfer, not because adaptive failed. Run `.cache/contenders/compound/item-tempo-v42-pool`,
exec27866 completed exit0: baseline `ad71190353b1` + tempo `056ab7a28856`,
all40pool/both sides versus fixed v42+hunter7,320fullgames, traceOFF. Poll this handle only;
all previous handles21788/7611/78271/77717 closed. All320unique full-game keys complete,
engine1.0.3, no duplicate/missing/truncated/error/skipped rows. Both teams entirely health-clean,
including no foe zero-clear stalls; max subject fuel93,864/95,173.

| Opponent | Baseline W-L | Tempo W-L | Flips | Paired margin change |
| --- | --- | --- | --- | --- |
| v42,80keys | 40-40 | 20-60 | +9/-29 | -229.38 +/-74.43SE |
| Hunter7,80keys | 66-14 | 44-36 | +3/-25 | -251.58 +/-69.50SE |
| Pooled160keys | 106-54 | 64-96 | +12/-54 | -240.5 +/-50.8SE |

REJECT tempo. Earlier Jungle/Monster completion and healthy execution did not transfer to
better game performance. Do not attribute the loss to one component without evidence or retune
neighboring item sequences on the training maps. No legacy/promotion; unchanged baseline retained.
One next decision: review a few saved baseline losses for a materially different gold-to-useful-
strength direction before another whole-game policy trial. This is a practical observation task,
not a revival of finite models/scouts. No native jobs remain.

## Field-capital specialty (pilot complete; broad transfer earned)

Saved baseline losses identify delayed conversion, without proving safe return opportunities:
BigWorldA `7648da8e`, Varg banks407/170gold and476/398gold, next shop508; FractureA
`071a6472`, Marcella banks374/154gold and382/236gold, dies412 and shops413 to finish Jungle
and Durability. StarvationA `365db7fb`, Heart holds300gold at400, first shop474, with no earlier
bank. NativeJSON establishes timing, not recall safety or an improvement from spending earlier.
Reviewer proposed one shop service after an own bank with150gold; keep that lead parked. Main
already explores explicit harvest/invest controllers, so root chose a distinct champion mechanic.

New `field_capital=false` draft flag changes only mode8 when ON: Heart, Marcella, Tavi, Vendetta,
Varg, Vesper, Stellar, Ryth, Cassi/Issac, Prism, Willow, Ren. Tavi replaces Ryth third; the two
swap positions, all other ordering stays fixed. Shop7, carrying and banking are unchanged. Existing
`ctx.on_spawn || kit().remote_shop` calls the same bounded shop each field turn for Tavi. Rules
permit Tavi's shard purchases anywhere; field Health does not heal, and consumables still require
spawn/death. No new planner or purchase model. Default OFF compile exclusion retains old policy.

Run `.cache/contenders/compound/field-capital-v42-pilot`: same-source baseline +
`field: field_capital=true`, six training maps/both sides versus v42+starter,48fullgames, traceOFF,
all replays/stats retained. Exec2793 completed exit0: OFF `c8a9b60a0f76`, ON `2a2edd9ea0f0`.
Build passed; OFF raw AND injected modules equal previous `ad71190353b1`, each SHA256
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
Inherited draft.cpp CRLF was normalized after the pilot; final current OFF `d8a42d1a2555`,
ON `ce3327905370` rebuilt successfully. Both raw+injected modules equal their measured pilot
counterparts,4/4 exact. ON raw+injected SHA256
`7a8a4ec4511719f454f6e0a69311c0be5681cf028389984fb9d5ca5e0f0fa83b`.
Game rows retain measured sourcekeys; LF-only source bindings do not relabel evidence.

All48 unique full games completed. V42 baseline6-6 -> field5-7, flips+3/-4,
paired margin-73.00 +/-199.62SE; starter12-0 both, margin-17.83 +/-7.92SE. Pooled18-6 ->17-7,
margin-45.4 +/-97.9SE: no detectable strength gain. Starter completion856.833 ->830.250,
paired-26.583 +/-12.940SE, the first tested economic package with faster passive completion.
All subject health flags0, max91,640/91,663; v42 foes clean. Starter foes have3zero-clear games
PERarm (LostWorldbothsides, CorridorssubjectA), kept separate from subject health.

Actual field spending is confirmed in AmbushA nativeJSON `6a02dcb9`: Tavi clears at82(70,45),
buys2TreasureSTR at98(71,44); clears622(29,55), completes Treasure+buys2Jungle at630(29,54).
Tavi completes Treasure19/24games, Jungle16/24, Monster15/24. This establishes executed purchases,
not causal attribution or qualification. No planner/model or neighboring draft tuning.

The unchanged full-pool transfer earned testing from actual faster passive completion and the
competitive uncertain v42 result. Root confirmed all40pool/both sides versus v42+hunter7,
same-run baseline, keepall. Run `.cache/contenders/compound/field-capital-v42-pool`, current OFF
`d8a42d1a2555` /ON `ce3327905370`, only field flag ON; exec15035 completed exit0.

| Opponent | Baseline W-L | Field W-L | Flips | Paired margin change |
| --- | --- | --- | --- | --- |
| v42,80keys | 40-40 | 29-51 | +15/-26 | -91.54 +/-87.26SE |
| Hunter7,80keys | 66-14 | 69-11 | +10/-7 | +132.65 +/-77.55SE |
| Pooled160keys | 106-54 | 98-62 | +25/-33 | +20.6 +/-58.9SE |

All320 unique full-game keys complete, no duplicate/missing/truncated/error/skipped rows, all
native stats/replays retained, engine1.0.3. BOTH teams entirely health-clean, including no foe
zero-clear stalls. Maximum subject fuel93,864/95,127. REJECT field capital for the main-opponent
objective: passive improvement and real field purchases did not transfer to v42 strength. Its
hunter7 tradeoff is descriptive, not generic dominance. Keep `field_capital=false`, unchanged
default execution. No legacy/promotion and no arbitrary neighboring Tavi/order/shop retuning.

One next decision: retain the default and choose a different executed growth mechanism with the
coordinator; the concrete bank-service lead above remains parked, not implemented in parallel.
No old pricing/scout/simulator work is resumed. All native jobs/handles are closed.

## Bank service after own tribute (pilot complete; default OFF)

Root chose the concrete saved BigWorld/Fracture service lead after field capital failed: baseline
roster8/shop7/bag25, `field_capital=false`. One new default-off `bank_service` rule: after our own
successfully completed tribute with at least150gold, take one safe home/shop service before
starting another camp. This is a fixed post-bank service seam, not an economic forecast, team
controller or new harvest/invest architecture. No item-order/carry tuning.

Implementation changed brain.h/brain.cpp/config.h only. Arm a positive own bag only after
successful submit plus fresh submitting state; alive ownchannel completion and ownbag0 trigger
the receipt. Cancellation/death disarm; allied score alone never triggers. Active channels,
threats, health recovery and actual current capture/fight retain precedence. Service reuses safe
recall and existing homeward steps, with a30round lease, then clears on shop/spawn/death. An
awake old target alone is not current work: fight precedence needs a visible living target and
our own hit within one round. A blocked/unactionable home step aborts service instead of waiting.
Default OFF adds no policy work or Brain layout fields. Builds passed: OFF `37753ac0782b`,
ON `637085078b1c`. OFF raw+injected equal previous `d8a42d1a2555` (SHA256 `5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`).
ON raw+injected SHA256 `e9f4621d2e097003d1c3ca42637a08d01929682366b54d9afac347df9b5e2301`.
All changed sources LF; build handles closed. Run `.cache/contenders/compound/bank-service-v42-pilot`,
baseline + `service: bank_service=true`,48fullgames early versus v42+starter, both sides/six
training maps, keepall/traceOFF. No pre-game models/probes or extra infrastructure. Fast caller
review runs alongside games; no separate review gate. Exec45015 completed exit0, all48 unique
full games/stats/replays retained. Sources remain OFF `37753ac0782b` /ON `637085078b1c`.

V42 baseline6-6 ->service6-6, flips+2/-2, paired margin-45.08 +/-103.79SE. Starter12-0 both,
completion856.833 ->863.333, paired+6.50 +/-13.29SE (slower point estimate), margin-19.75
+/-8.75SE. Pooled18-6 both, margin-32.4 +/-51.0SE: no detectable strength or economic gain.
All subject flags0, max91,640/91,663; v42 foes clean. Starter zero-clear proxies base3/service2
(LostWorldbothsidesbotharms, baseline additionallyCorridorssubjectA); other foe flags0.

One actual normal service cycle is confirmed, no synthetic probe: AmbushA vs v42 `61606682`
nativeJSON/replay, Heart tributes37 at666(33,20). ON logs receipt and recall with250gold at666;
at671 spawn(3,3) Heart buys5DurabilityHealth+3MonsterFocus shards for240gold. Activation does
not establish a useful improvement. Whole-panel first-item completion medians are slightly later.
Keep `bank_service=false`; do not tune150gold or30round lease on these six maps. Root CLOSED
the lead without broad transfer: no measured economic gain. Fast review found the measured
unsafe fallback caveat: earlier threat/health phases returning false did not certify step_home's
destination. The small current-source correction aborts unsafe/unavailable optional service
instead of forcing that step; it is unmeasured and does not earn another sweep. Measured
`637085078b1c` remains the pilot source and is never relabeled as corrected. No native jobs remain.

## Tavi field-growth combat controller (rejected; default OFF)

Current checkpoint: all552 full games complete (72 pilot plus480 pool), no owned game/build jobs
remain. `tavi_growth=false`, `field_capital=false`, `bank_service=false`. Current default
`e6d671ccf25e` raw/injected modules exactly equal live v42 SHA256
`5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b`.
Measured BASE `fab92a00127a`, field `a9c22b9baba3`, growth `06cd15d99824`; no source changes during either run.
After games, three inherited CRLF files (`ctx.h`, `knowledge.cpp`, `knowledge.h`) were normalized
to LF and the current default rebuilt; exact module equality binds that formatting-only change.
Durable exact source/replay/statistics/health and paired panels are in
`results/contenders/compound/tavi-growth/pilot.json` and `pool.json`.

The completed role closes three actual action/lifecycle contracts: visible-contact withdrawal
uses the same no-higher-current-threat landing guard as quiet withdrawal instead of falling back
to generic retreat; remote purchases refresh live current/maxHP, Strength and Focus; a new
one-round stun removes next-input threat only when the victim had already completed its current
round input, with actual fresh stun confirmation. Pending-target stun can expire before its next
action ahead of ours. These are source-level safety consequences of runtime mechanics, not proof
that a specific alternative saves Tavi. The controller replaces Tavi's generic all-in/chase path,
retains active-channel precedence, and commits withdrawal before ordinary health/tribute/farm.

Pilot `.cache/contenders/compound/tavi-growth-v42-pilot-r1`: BASE6/12, field5/12, growth7/12
against v42; all12/12 versus starter. Growth vs BASE v42 flips+3/-2, margin+51+/-219.55SE.
All subject health clean; starter has three opponent zero-clear games per arm, explicitly kept
separate from subject health. This pilot did not establish an improvement.

Frozen broad `.cache/contenders/compound/tavi-growth-v42-pool-r1`: all40poolmaps,bothsides,
v42 and frozen hunter7, three arms/480 games. V42 BASE40/80,field29/80,growth25/80;
growth vsBASE+10/-25, margin-223.35+/-77.27SE. Isolating the controller from its Tavi chassis,
growth vsFIELD+12/-16, margin-131.8125+/-73.55SE. Hunter7 BASE66/80,field69/80,growth66/80;
growth vsBASE+9/-9, margin-12.0625+/-69.99SE; vsFIELD+6/-9,margin-144.7125+/-78.62SE.
All subjects AND both opponents have zero faults/exhaustion/errors/zero-clear games/fuel-at-budget;
arena also reports zero suspensions. Subject peaks BASE93864,field95127,growth94697.

Decision: REJECT the full Tavi field-growth package and close this controller branch; no legacy,
promotion or retreat/radius tuning. Do not attribute all15lost v42 wins to micro: the field chassis
already loses11. Team r400 collected gems BASE202.89,field184.46,growth181.30 and deaths
1.59,1.86,1.21 show that fewer deaths alone did not restore productive throughput. This is an
outcome/production contrast, not proof of the lost task or its cause. A preselected AmbushA first
activation and matched native productive metrics retain the mechanics lesson below.
The next question is whether champion/task admission and completion account for the
labor displaced by contact/withdrawal; another Tavi spell preference wrapper is not warranted.

`results/contenders/compound/tavi-growth/production.json` retains all matched r400 native role
and team metrics (terminal state used if a game ended earlier). Against v42, controller-vs-field
Tavi deaths fall0.375+/-0.0896 perkey, but monster damage falls552.20+/-243.90, champion damage
falls359.97+/-87.93 and earned gold falls24.70+/-13.95. Own gems collected change+1.36+/-2.66,
submitted+1.34+/-2.58: improved survival is not an established extra scoring loop. Hunter7 also
loses monster damage792.14+/-261.03 and earned gold69.44+/-22.52. These are measured package
contrasts, not isolation of individual action rules. Native statistics do not expose recall/
return counts, so no broad return-cycle count is invented.

The outcome-independent first AmbushA activation is in
`results/contenders/compound/tavi-growth/ambush-A-mechanism/assessment.json`. Three bounded
observations cover two unique windows75-90 and140-190; every owned observer Job is empty.
R81 exclusive poke removes Heart's150shield+4HP atdistance4. R153 stationary basic-reset-basic
resumes farming (120HP->75->30); group clear160 and capture168 yield80gold and0gems, then
two field TreasureStrength shards raise Strength45->55. R188 actual poke-basic hits Ryth154+55;
its next actual input189 is stunned/endturn only; stun expires190 before its100countercast.
R91-139 private withdrawal/release remains unobserved, and this sample proves no step-reset-basic.
Growth's fullgame Tavi earns1336gold,17clears,1death but collects/submits0gems; field earns772,
collects/submits51,5deaths. It establishes useful gold renewal and packing on one game, not a
complete personal scoring loop or the cause of its terminal outcome. No more native queries.

Root changed the experiment's scale after field chassis improved passive completion but lost
pressured v42 games. New default-off `tavi_growth` will enable the same Tavi-third roster plus
a Tavi-specific ranged spell-poke/withdraw lifecycle. Generic `field_capital=true` remains a
separate chassis control. Everyone else stays ordinary v42; bank_service stays false. Respect
runtime range4 exclusive spell versus range3 shared move/basic and compatible reset competing
for one cast, spend shards through existing remote shopping, and bypass generic all-in/chase
decisions for Tavi. Generic commitment is a hypothesis, not an established explanation of losses.

Implementation child owns source, measurement child waits final release, and economy scout
selects at most two retained pressure windows for micro's actual action-order inspection. Source
facts and existing native evidence inform implementation without gating first games. No new
skirmish model, synthetic fixture or forecast project. A simple Tavi-only purchase stat choice
may follow the executed role; family order7 and all other champions remain fixed, with one
coherent package rather than several tuned alternatives. Run72 full training games early:
BASE + generic field chassis + new growth controller, same six maps/both sides/v42+starter,
keepall and traceOFF. Those plans are historical; the completed keys and rejection above supersede them.

Actual retained pressure evidence, generic `ce3327905370` vs v42, engine1.0.3: AmbushA
`1548cc52` Tavi at151 basics for55 atdistance2, spending movement with cooldowns[2,3]. At152
he retreats and loses sight; at153 normal farming steps BACK, revealing adjacent Vendetta+Ryth
and taking230HP that round. At154 HP290, both slots ready and M/A/C available, he walks first
and leaves cast unused; no cast154-157, dies to Vendetta157. This is a real retreat/re-entry and
cast-versus-walk ordering episode, not proof a single stun saves him from two pursuers. Root
requires recent-pursuer/committed-withdrawal memory in the new quiet branch: loss of sight after
our step is not disappearance. Reuse bounded existing memory/homeward handling and an explicit
safe end condition, with no new belief model or fitted timeout. Micro inspects ContestA
`ecbf4208`407-417 separately; there are no additional games or windows. Its actual actions:
at410 Tavi(34,30),HP900,CD[0,0],M/A/C available, Marcella(33,33) distance3. Tavi only moves
to(34,29), ends with cast unused; Marcella's next-round spell hits120. At415 TaviHP450,
all ready, Marcella distance1: Tavi moves to(34,30), target remains distance2, then ends with
no reset/basic continuation. This repeats416 atHP170 before death417. Target-specific can_use
predicates were not queried; no claim an alternative saves him. It motivates the compatible
move/reset/basic branch while retaining Marcella stun immunity as a safety constraint.

## Parked research checkpoint

All prior jobs/observers closed; scoped OS process read found no remaining matching jobs.
Private working source `60c2b3a8fee5` is unbuilt/unverified; do not treat it as qualified.
Last verified private default `ba76e51a7354` equals v40 modules; full parked details are in
`bots/contenders/_compound-capital/NOTES.md` and durable `capital-prototype.json`.
The old original default `59a3814493a3` and its health qualification belong to the retired bot,
not this new implementation. Old game rows and experimental labels remain historical.

Do not run `.cache/contenders/compound/save_checkpoint.py` for the new bot: it belongs to the
retired implementation and asserts its old binary binding. Preserve existing index history when
adding this generation's empirical package results.
