# anchors: frozen reference bots

Fixed points for the ladder, for sweeps and for fixtures. They give ratings a stable floor and
ceiling and keep claims such as "beats B0" or "beats the live version" meaningful across months.
Nothing here is developed: never edit these sources. New anchors arrive only through
`arena release` (each release freezes its exact sources here). To develop from one, copy it into
a developing suite (`arena new bot <suite>/<name> --from anchors/<bot>`).

| Bot | What it is | Typical use |
| --- | --- | --- |
| starter-cpp | Official randomized starter (unchanged) | Ladder zero point (rated 1000), robustness |
| fundamentals | B0: conservative clear -> capture -> tribute economy; fixed draft Tavi, Varg, Vendetta, Vesper Vale, Marcella; affordable camps with retreat; Monster Strength -> Jungle Health -> Power Strength buys; gold tiebreak on monolith-only maps | Floor for every suite; the `specialists` chassis |
| main-v28 | Live online submission (platform "bot version 29"); bundle in `results/releases/main-v28/` | Release reference: candidates gate against the live anchor (`results/releases/LIVE.json`) |
| main-v28p1, main-v28p2 | v28 with two other pick orders | Pooled opponents for main sweeps (pick order flips outcomes chaotically) |
| main-hunter7 | v28 plus an aggressive hunting block | PvP proxy for sweeps |
| main-v24, main-v18, main-hunter6 | Older main versions and hunter | Opponents of the main game fixtures (they reproduce past bugs); rating rungs |
| main-v29 | Release of main/main@02d955855258 (2026-10-22); against main-v28: vs anchors/main-v28: 37-43-0 score 0.463 (95% CI 0.36-0.57) >= 0.3; gem margin +4 | Release reference once live |
| main-v30 | Release of main/main@fb06ede9b645 (2026-10-22); against main-v29: vs anchors/main-v29: 46-34-0 score 0.575 (95% CI 0.47-0.68) >= 0.5; gem margin +44 | Release reference once live |
| main-v31 | Release of main/main@c5a34415f3ac (2026-10-22); against main-v30: vs anchors/main-v30: 40-40-0 score 0.500 (95% CI 0.39-0.61) >= 0.3; gem margin +64 | Release reference once live |
| main-v32 | Release of main/main@d5bc9e7b7a7b (2026-10-22); against main-v31: vs anchors/main-v31: 42-38-0 score 0.525 (95% CI 0.42-0.63) >= 0.3; gem margin +3 | Release reference once live |
| main-v33 | Release of main/main@8a7337f4849f (2026-10-22); against main-v32: vs anchors/main-v32: 41-39-0 score 0.512 (95% CI 0.40-0.62) >= 0.5; gem margin +22 | Release reference once live |
| main-v34 | Release of main/main@dce540466d11 (2026-10-22); against main-v33: vs anchors/main-v33: 41-39-0 score 0.512 (95% CI 0.40-0.62) >= 0.5; gem margin +25 | Release reference once live |
| main-v35 | Release of main/main@e030a0be6587 (2026-10-22); against main-v33: vs anchors/main-v33: 41-39-0 score 0.512 (95% CI 0.40-0.62) >= 0.5; gem margin +3 | Release reference once live |
| main-v36 | Release of main/main@e8e1cf35b90f (2026-10-22); against main-v35: vs anchors/main-v35: 42-38-0 score 0.525 (95% CI 0.42-0.63) >= 0.5; gem margin +17 | Release reference once live |
| main-v37 | Release of main/main@35090853666a (2026-10-22); against main-v36: vs anchors/main-v36: 41-39-0 score 0.512 (95% CI 0.40-0.62) >= 0.5; gem margin +93 | Release reference once live |
| main-v38 | Release of main/main@d243650467f7 (2026-10-22); against main-v37: vs anchors/main-v37: 48-32-0 score 0.600 (95% CI 0.49-0.70) >= 0.5; gem margin +78 | Release reference once live |
| main-v39 | Release of main/main@0d4f9dc900df (2026-10-22); against main-v38: vs anchors/main-v38: 52-28-0 score 0.650 (95% CI 0.54-0.75) >= 0.5; gem margin +116 | Release reference once live |
| main-v40 | Release of main/main@45108c43eb8a (2026-10-22); against main-v39: vs anchors/main-v39: 49-31-0 score 0.613 (95% CI 0.50-0.71) >= 0.5; gem margin +113 | Release reference once live |
| main-v41 | Release of main/main@db6ca9bc4e39 (2026-10-23); against main-v40: vs anchors/main-v40: 47-33-0 score 0.588 (95% CI 0.48-0.69) >= 0.5; gem margin +127 | Release reference once live |
| main-v42 | Release of main/main@0353a987e04f (2026-10-23); against main-v41: vs anchors/main-v41: 37-43-0 score 0.463 (95% CI 0.36-0.57) >= 0.45; gem margin -46 | Release reference once live |
| main-v44 | Release of main/main@0353a987e04f (2026-10-23); against main-v42: vs anchors/main-v42: 40-40-0 score 0.500 (95% CI 0.39-0.61) >= 0.5; gem margin +0 | Release reference once live |
| main-v45 | Release of main/main@8c249f9bf378 (2026-10-23); against main-v44: vs anchors/main-v44: 45-35-0 score 0.562 (95% CI 0.45-0.67) >= 0.5; gem margin +144 | Release reference once live |
| main-v46 | Release of main/main@a68114575f8e (2026-10-23); against main-v44: vs anchors/main-v44: 33-47-0 score 0.412 (95% CI 0.31-0.52) >= 0.35; gem margin -15 | Release reference once live |
| main-v47 | Release of main/main@b22f3ccd8759 (2026-10-24); against main-v45: vs anchors/main-v45: 41-39-0 score 0.512 (95% CI 0.40-0.62) >= 0.35; gem margin -94 | Release reference once live |
| main-v48 | Release of main/main@0addedec7c1f (2026-10-24); against main-v45: vs anchors/main-v45: 34-46-0 score 0.425 (95% CI 0.32-0.53) >= 0.35; gem margin -161 | Release reference once live |
| main-v49 | Release of main/main@9e2ad8926be1 (2026-10-24); against main-v47: vs anchors/main-v47: 44-36-0 score 0.550 (95% CI 0.44-0.65) >= 0.3; gem margin -49 | Release reference once live |
| main-v50 | Release of main/main@1006267813ac (2026-10-24); against main-v44: vs anchors/main-v44: 54-26-0 score 0.675 (95% CI 0.57-0.77) >= 0.5; gem margin +202 | Release reference once live |
| main-v51 | Release of main/main@f60768fae92a (2026-10-24); against main-v50: vs anchors/main-v50: 43-37-0 score 0.537 (95% CI 0.43-0.64) >= 0.5; gem margin +82 | Release reference once live |
| main-v52 | Release of main/main@c1b46ae95692 (2026-10-24); against main-v51: vs anchors/main-v51: 52-28-0 score 0.650 (95% CI 0.54-0.75) >= 0.5; gem margin +158 | Release reference once live |
| main-v53 | Release of main/main@e39f62accd4a (2026-10-24); against main-v52: vs anchors/main-v52: 45-35-0 score 0.562 (95% CI 0.45-0.67) >= 0.5; gem margin +82 | Release reference once live |
| main-v55 | Release of main/main@f051129c2ff2 (2026-10-26); against main-v53: vs anchors/main-v53: 44-36-0 score 0.550 (95% CI 0.44-0.65) >= 0.5; gem margin +104 | Release reference once live |

All of them build in release mode with zero faults; the main versions have zero fuel exhaustion
in every recorded configuration. Older qualifications (CLI 2.2.5 era) do not carry over to
fundamentals' current strength; measure it on the ladder.
