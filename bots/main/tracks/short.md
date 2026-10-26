# Short track: current-bot optimisation and release

Updated 25 Oct 2026, 21:30 UTC. RESUMED by kickoff 25 Oct ~21:00 UTC.
Previous note (provenance detail, closed lines): `D:\files\battlecode-removed\bots\main\tracks\short.md.before-20261025T2130`.

## Ownership and authority
- Own `main/`, releases, `DECISIONS.md`, this file and the main-bot section of `README.md`.
- Outside `bots/main` read only docs/sdk/maps/tools/.tools/.cache/main/results/online/results/releases;
  write only `.cache/main` and `results/online`. Never read or copy another bot's source.
- All main submissions and the whole online quota are mine. Allowed: read-only ladder, matches
  list/show/download/summary, match-groups quota/show/download/create, main submissions (never
  `-n/--note/--name`). Weaker online experiments are permitted for stronger data.
- Goal: first place, international qualifier 4 Nov. Never release faults, exhaustion, error lines, stalls.

## Approach change (21:10 UTC, after fresh review given only the old note)
Diagnosis: ~21 h without improvement; tests of 48-96 games gated on a few net wins (SE ~5 pp)
rejected real gains; mechanism-forecast criteria killed positive leads; provenance work displaced play.
New rules:
- Decide on paired WIN RATE + health only. >=1,500 paired keys per claim on unseen maps
  (heldout/legacy/fresh), both sides; no gates on <500 games; no mechanism-forecast kill criteria.
- Head-to-head vs live and broad gauntlet both count; prefer the candidate that is not worse on any archetype.
- Combine positive knobs jointly; feed knobs into ES rather than burying them after one screen.
- Release once a day when something passes; online A/B vs ranks 1-30 is the transfer check.
- Exact-build + health + CLI gate remain; no historical byte custody ceremony.

## State
- **Live v55** (26 Oct ~01:30 UTC, platform 55, anchors/main-v55) = v53 + hazard_threshold 0.3 + capture_handoff 1
  (D362). Gate PASS: 240 clean, vs v53 44-36. Unseen claim +224/-141 (p<.001), faster vs every opponent.
- v54x (hz0.3 only) was online-neutral vs v53: +0.2 +- 1.7 pp over 700 matched games.
- Known rare exhaustion ~1/5,000 local games in base and v55 alike (not in the CLI gate); s-fuel fixing.
- v53 online: 15% ranks 1-10, 30% 11-30; camp gold at r300 predicts losses (review-s2).

## Always-on data and review (user, 26 Oct: never pause)
- Our games: quota keeper (`short/keeper.py`, log `short/keeper-run.log`), 50 groups/rolling hour.
- Public top-20 games: loop every ~20 min + run (`short/topvtop.py --band 20`, log `short/topvtop-loop.log`;
  stop file `short/stop-topvtop`) into results/online/topvtop.
- Review agent in cycles: state `.cache/main/review/state.json`, findings `.cache/main/review/findings.md`.
  Relaunch a new cycle immediately after each report (brief = cycle 1's: v55 triage, deep losses, top-vs-top,
  handoff/hazard behaviour, <=4 ranked testable levers).
- After any restart/reboot: restart all three before anything else.

## Running / next
- `cem1` (`bots/main/tools/train/knobcem.py`, log `.cache/main/train/cem1.log`): CEM over 23 economy/risk knobs,
  fitness = contested score vs v53/hx1/compound-field-growth + (1000 - rounds vs passive)/1000.
  Prediction: best candidate +3..6 pp contested and 50-100 rounds faster vs passive. Confirm the final mean on
  heldout (>=1,500 keys) before any release.
- Online A/B: compare v54 vs v53 rates by band once >=500 v54 games exist (standing.py).
- After cem1: ES on the 94 learned weights with the CEM knobs, same mixed objective (`es.py --pace-vs`).
- Subagents (25 Oct 22:30 UTC, user request), each in its own copy, knob default OFF, success = heldout+legacy
  >=1,500 keys vs v53/hx1/compound-field-growth, p<.05 or >=30 rounds faster pace, health clean:
  `main/s-open` opening pace + shop timing; `main/s-route` travel/target chaining; `main/s-safe` early deaths.
  Merge into main only after my own re-run on fresh/heldout keys.

## Lessons kept
- Local proxies lack top-team low-cost kills (ranged carry behind screen + stun chains,
  `.cache/main/study/fights/report.md`); our roster is all melee. Fight-forecast lacks heals/control.
- Exploiter ES chain v51->v53 gained online (#62->#36); exploit4a vs v53 converged n.s. (D346).
- Compile new helper bodies OFF too; OFF must replay old games bit for bit.

## Online A/B log
- 26 Oct ~00:00 UTC: `bots/main/tools/vpair.py 53 54` (opponent/map/side-matched): v54 42.8% vs v53-expected 46.3%,
  -3.5 +- 2.2 pp over 390 games (11-30 -6.5, 31-60 -6.3). Opponent drift biases this against v54. Decide at ~1,000
  games; if still negative, run a v53 control block (alternate blocks to separate drift from effect).
- s-safe (early_hazard, t0.12 until r250, bots/main/s-safe): heldout+legacy+fresh 1,362 keys +159/-168 (p .66);
  deaths -0.47/game (-1.23 vs hx1), r150 deaths vs hx1 1.07->0.67; pace +2; health clean; OFF identical.
  Locally neutral; candidate ONLINE arm (first blood matters more online). Diagnosis: lone champion stuck at
  distance 1-3 from one hunter ~10 rounds, retreat cannot separate at equal speed; Ryth 43/121 early deaths.
- s-open: no gain. capture_recall=0 -50 vs v53 (p .001); recall slack/walk at a sharp optimum; early_shop inert;
  opening_party inconsistent; chain lookahead -127 (p<.001). Pace within +-7 rounds for all. Keep thresholds.
- s-route: capture_handoff=1 (one ally holds a cleared capture when no enemy in view, others leave): claim
  heldout+legacy+fresh 1,362 keys 916->986 (+197/-127, p 1.2e-4), pace -17; route_chain null. Merged into
  main/main 26 Oct (default OFF, D362); my independent verification `s2-handoff-verify` running.
- s-race: race_intercept (lock-cancel winning enemy channels) -14/-17 net, race_finish (jackpot boost) -18/-20,
  pool+train 1,440 keys, n.s.; both OFF. Finding: any ON variant changes 21-45% of games via fuel-dependent
  field warming ("fuel chaos"), so rare mechanisms need trigger-gated comparisons, not all-game pairs.
