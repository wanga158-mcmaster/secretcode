# miscellaneous: retired contenders, kept as semi-competitive opponents

## Doctrine
No single axis. This suite keeps the complete bots that the contenders' coordinator (`contenders/*`,
[charter](../contenders/README.md)) retires when it finds a better direction. Each was a serious attempt
at main-level strength from its own derivation of the shared [foundation](../main/foundation/README.md),
so together they are what the other suites mostly are not: competitive, varied opposition for
`main`, the contenders and the ladder.

## Bots
| Bot | Retired from, and what makes it play differently | State |
| --- | --- | --- |
| [compound-field-growth](compound-field-growth/NOTES.md) | Complete Compound checkpoint, including default-OFF Tavi field-growth, field-capital and banking experiments; preserved before clean successor Mosaic. | Retired 23 October 2026 after the field-growth design failed its 480-game pool comparison: 25/80 vs live-equivalent v42, control 40/80. Subject health was clean; no promotion. Default remains its preserved control, not the rejected ON arm. |
| [mosaic-reactive-service](mosaic-reactive-service/NOTES.md) | Complete action/geometry runtime before the retained selected-service interpreter; all 49 source, fixture and note files preserved with matching path, size and SHA256 manifests. | Retired 23 October 2026. R18 `00d66a796cd8` clears once and banks 28 on Packs A/passive, with clean runtime health, then remains unproductive through round 2000 after an actual spawn return. Repeated rejected-candidate reconstruction motivated the rebuild. No main-strength qualification. |
| [warband-local-party](warband-local-party/NOTES.md) | Complete Warband checkpoint before its independent complete-production rebuild; optional local party contribution, priority, release and conversion are retained. | Retired 23 October 2026. Final untraced cohort: 16 full technically healthy games/eight paired keys, no win changes or added early team supply. Preserved default remains its control; experimental flags are OFF. |
| [warband-reactive-production](warband-reactive-production/NOTES.md) | Complete reactive production runtime and notes, preserved before the explicit finite SERVICE interpreter. | Retired 23 October 2026. Source `8ff5abf3bcb3` failed the full Yellow Brick Road/passive prerequisite: no clear or income, one death and two native fuel exhaustions. The directory is preserved intact; the successor's later results do not qualify this runtime. |
| [timetable-residual-service](timetable-residual-service/NOTES.md) | Complete Timetable checkpoint, including new residual-service components and inherited main-derived runtime dependencies; preserved before an independent clean rebuild. | Retired 23 October 2026 for independence. Source `8ab060b6f62e` has 16 technically healthy paired games but wins 0/4 live and 3/4 starter against control 2/4 and 4/4. All experimental flags remain OFF; historical control evidence stays here. |
| [mosaic-guarded-perch-service](mosaic-guarded-perch-service/NOTES.md) | Complete retained selected-service interpreter, quiet R2 production repair and experimental harmful-perch action/capture/return arm; all source, fixtures and notes preserved with matching manifests. | Retired 23 October 2026. Quiet R2 banks 210/242 on Packs/passive both roles, with late team production but a Varg loaded idle suffix. Siege R4 is technically clean yet fails repeated work (0/8/0/0 banked on Packs/BigWorld both roles); one Ryth harmful clear/bank/purchase witness is retained. No strength qualification; flags remain OFF. |
| [mosaic-ordered-words-r20-20261025](mosaic-ordered-words-r20-20261025/NOTES.md) | Complete retired R20 group-owned service and ordered-word implementation; 98 files preserved before the compatible-word successor. | Retired 25 October 2026. The retained ordinary guarded continuation `d2087ce32dce` has zero clears, receipts or banking, 4,375 monster damage and a stall, with no recorded technical health flags. Earlier group-service work clears and receives 26 gems but never banks them. Productive continuation and current-Main qualification remain unproved. |
| [timetable-calendar-frontier-20261025](timetable-calendar-frontier-20261025/NOTES.md) | Complete retired global menu/calendar-frontier implementation, held source `875518c7e180` (66 files), before the body-owned residual-production successor. | Retired 25 October 2026 after the actual reachability audit. The diagnostic changes native chronology at round 320, before its first packet, so its later collector-ready/no-menu state does not establish ordinary private reachability. Complete productive continuation, healthy current-Main coverage and distinct contribution remain unresolved. |

## Rules
- **Retiring.** When a contender's direction is superseded, its coordinator moves the bot here (keep the
  source as it is, add a row above with a one-line description and its ladder standing), then
  rebuilds the slot from scratch or from new insight. `arena new bot miscellaneous/<name>
  --from contenders/<x>` copies it; then move the old source under the removal rules or start the
  slot afresh. Name it after what it does, not after the slot.
- **Frozen in spirit.** Retired bots are not developed further; they are fixed points. Health fixes
  are the exception. A bot that later proves redundant (same style, near-even head-to-head with a
  neighbour, `arena ladder styles`) is moved out under the removal rules.
- **Self-contained.** Every bot owns its whole source; no shared modules, no includes across suites.

## Layout and checks
- `bots/miscellaneous/<bot>/src/main.cpp` (id `miscellaneous/<bot>`); notes may come along in the
  bot directory.
- `arena build miscellaneous`; `bots/miscellaneous/tests/run.py` for any fixtures that come along.
