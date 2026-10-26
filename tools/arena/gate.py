"""Release and robustness gate: exit 0 only if every check passes.

  arena gate BOT [--ref REF] [--maps pool] [--health-vs SEL] [--min-ref-score 0.5] [--jobs N]
                 [--rounds N] [--out DIR] [--keep-replays flagged|all|none] [--fail-fast]

  1 build      BOT's snapshot builds (core.prepare(...).ok); the later checks need it.
  2 tests      bots/<suite>/tests/run.py <bot> exits 0 (skipped with a note when the suite has none).
  3 health     BOT plays SEL (default anchors/starter-cpp,anchors/fundamentals; selectors and
               ladder:nearN as in `arena sweep --vs`) on the maps from both sides.
               In every game the gate plays (the reference games included) the subject
               must show 0 faults, 0 fuel exhaustion, 0 error lines and 0 stalls (no camp clear);
               an error or skipped game fails it too. Offenders are listed with their replays.
  4 reference  with --ref: BOT vs REF head to head on the maps from both sides; the score (draws
               half) over finished games, shown with a 95% Wilson interval, must be >= --min-ref-score.
Every step runs unless the build fails (or --fail-fast stops at the first failure). Games cut by
--rounds have no statistics: they show errors and suspended turns only, so health and reference
report "part" and the gate cannot pass (a smoke of the plumbing, not a release check).
Output: .cache/arena/runs/<time>-gate-<bot>/ with games.jsonl and gate.json (the checklist).
"""
from __future__ import annotations

import json
import hashlib
import os
import re
import shlex
import subprocess
import sys
import time
import zipfile
import importlib.util
from contextlib import ExitStack
from types import SimpleNamespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import BOTS, ROOT, ArenaError, Bot, map_set, prepare, prepare_many, read_closure, version_of  # noqa: E402
from core import bot as find_bot  # noqa: E402
from games import KEEP, GameSpec, add_resume_args, deadline_from, health, perspective, play  # noqa: E402
from sweep import _now, _rel, brief, out_dir, resolve_opponents, score_text, tail_lines, tally  # noqa: E402
import core  # noqa: E402
import games as cli_games  # noqa: E402

DEFAULT_HEALTH_VS = "anchors/starter-cpp,anchors/fundamentals"
TEST_TIMEOUT = 3 * 3600
PASSING = ("pass", "skip")
LIMIT = 12

OWNER_INPUT_SCHEMA = "arena.owner-cli-inputs.v1"
OWNER_GATE_SCHEMA = "arena.owner-cli-gate.v1"
OWNER_TEST_SCHEMA = "arena.owner-cli-tests.v1"
OWNER_INPUT_V2 = "arena.owner-cli-inputs.v2"
OWNER_TEST_V2 = "arena.owner-cli-tests.v2"
# Entries are added only after reviewing a runner's complete owner-local route. A manifest
# cannot nominate executable code or replace this review with a supplied pass document.
OWNER_TEST_ROUTES: dict[str, dict] = {}

_MAIN_REVIEWED_ROUTE = {'version': 2,
 'suites': ('main',),
 'bots': ('main/main',),
 'script': 'tools/arena/owner_main_tests.py',
 'files': {'bots/main/tests/run.py': '31445cb4d7df1feac4e69e6f8bd04e6d4d72bf8f83072eac75ac19531faba601',
           'bots/main/tests/fixturelib.py': '94207d3bb899153b6e9a2d0fafc808f317b140f4cc7f5cb14c74e5a32de5043e',
           'bots/main/tests/main-online-test.py': 'c9d901d57caa803d74bb8f6b5ebd976c59eeb5a1c2e20c0f0bdb696160a67617',
           'bots/main/tests/main-liveness-test.py': '8abf6ed912815b6202ef5080386c3a5b0cb790fcdfa6d6327910b3cae75ffda1',
           'bots/main/tests/main-heal-fixture.py': '4bd2999eec51a776bb8c81a19d322baac786ee28de992ffa24550fa94f29d4de',
           'bots/main/tests/main-watchdog-fixture.py': 'f8259643bb0932932c41e96129a330e97d40a6b0695c68d9e599a08646017d4a',
           'bots/main/tests/main-spawn-fixture.py': 'e281a3dc0558d31124fbcd2e5090d3fac522c5452ca755633fdd3c9bfc2059bc',
           'bots/main/tests/main-capture-fixture.py': '36744727488c63109c16584a5e47ea430306559346d5e3918a8f4e6de3293144',
           'bots/main/tests/main-retaliation-fixture.py': '18621adbddcce351b8c52667531e485367a7eed42cf0f463cd32b738d195a5a3',
           'bots/main/tests/main-liveness-fixture.py': '21a494cfba35e4a45613dcd1641f9188ae766f027df5279ee89f8421a539333a',
           'bots/main/tools/online.py': '4ac96349ae3a36cfcab3bd27c47dc8c43c125045f703d562a43a824261eb39a9',
           'bots/main/tools/scout.py': '527d8a90e94b903198c9b35ca20edef31d2d65952331a34c6707b9f3763733bb',
           'bots/main/tools/liveness.py': 'fcdedb972cb3fa894f058d44b4bc1983e826d2d0973eb4be6aef8ba14f61936c',
           'bots/main/main/tests/main-model.cpp': '91396381ac8b25209b0c64fb26c47c6cd93a8f1fbcc041b7f08780e65e391933',
           'bots/main/main/tests/main-retaliation-reserve.cpp': '78e5bab2d083421aed686afe8768c504d3462345e96475d2aab90754ba18351d',
           'bots/main/main/tests/main-skirmish-memo.cpp': 'd2197f1dc0c5a23bba9bcc5d84d46f500897c7f71ae41d5283a0f8c271c007ff',
           'tools/arena/core.py': 'c8bdf9a72bd2d20e032258d6b1b330cfde9719f43edd0e32b82b9a78adc3cd5e',
           'tools/arena/wasi.py': 'a6f896463768ffbb75a019a54ad1c7efa7e5566f79f9fb7b4e72f41f4a70013d',
           'tools/arena/wasi-run.mjs': '056a7757f8057f5d781642897b143353296d96cf541310c1bcd4e093d80817b1',
           'tools/arena/owner_main_tests.py': 'ac8693963e567d249d91b3beb1915d9492b117c186c34f5794f9aaf7017c93d7'},
 'inventory': ['cpp:main-main-model',
               'cpp:main-main-retaliation-reserve',
               'cpp:main-main-skirmish-memo',
               'python:main-online-test.py:KeeperTests.test_failed_creation_still_collects_first',
               'python:main-online-test.py:KeeperTests.test_pause_drains_existing_groups_without_creating',
               'python:main-online-test.py:KeeperTests.test_pause_during_fill_stops_next_creation',
               'python:main-online-test.py:KeeperTests.test_create_timeout_is_not_retried',
               'python:main-online-test.py:KeeperTests.test_long_command_keeps_server_reason',
               'python:main-online-test.py:KeeperTests.test_only_explicit_ineligible_409_is_classified',
               'python:main-online-test.py:KeeperTests.test_other_create_errors_are_not_retried_or_classified',
               'python:main-online-test.py:KeeperTests.test_ineligible_opponent_is_replaced_without_using_a_free_slot',
               'python:main-online-test.py:KeeperTests.test_all_ineligible_candidates_end_fill_without_looping',
               'python:main-online-test.py:KeeperTests.test_unknown_successful_create_reply_aborts_fill_without_replacement',
               'python:main-online-test.py:KeeperTests.test_unexpected_quota_payload_cannot_create_groups',
               'python:main-online-test.py:KeeperTests.test_recent_candidates_can_be_reused_when_no_fresh_opponent_remains',
               'python:main-online-test.py:CollectionTests.test_finished_matches_and_terminal_error_complete_without_scoring_error',
               'python:main-online-test.py:CollectionTests.test_queued_with_terminal_error_stays_pending_and_preserves_partial_outcomes',
               'python:main-online-test.py:CollectionTests.test_unknown_status_or_error_without_finished_timestamp_stays_pending',
               'python:main-online-test.py:CollectionTests.test_all_terminal_errors_complete_with_diagnostics_and_no_scored_games',
               'python:main-online-test.py:CollectionTests.test_mixed_group_archive_retry_keeps_finished_summary_and_is_idempotent',
               'python:main-online-test.py:CollectionTests.test_checkpoint_survives_later_group_failure_without_csv_duplicates',
               'python:main-online-test.py:CollectionTests.test_partial_summary_is_preserved_and_repaired_on_next_fetch',
               'python:main-online-test.py:CollectionTests.test_corrupt_deflate_is_preserved_and_retried',
               'python:main-online-test.py:CollectionTests.test_deferred_discovery_rotates_without_blocking_finished_artifacts',
               'python:main-online-test.py:CollectionTests.test_discovery_reserves_time_for_finished_artifacts',
               'python:main-online-test.py:CollectionTests.test_archive_timeout_checkpoints_summaries_and_rotates_work',
               'python:main-online-test.py:CollectionTests.test_registry_checkpoint_repairs_csv_after_write_failure',
               'python:main-online-test.py:CollectionTests.test_rate_limited_read_defers_without_sleep_and_reaches_quota',
               'python:main-online-test.py:CollectionTests.test_keeper_slice_caps_a_downloads_long_timeout',
               'python:main-online-test.py:CollectionTests.test_registry_readers_see_complete_old_or_new_json',
               'python:main-liveness-test.py:LivenessTests.test_rle_boundary_and_late_progress',
               'python:main-liveness-test.py:LivenessTests.test_other_team_activity_is_not_ours',
               'python:main-liveness-test.py:LivenessTests.test_missing_metric_is_unavailable',
               'python:main-liveness-test.py:LivenessTests.test_missing_report_is_unavailable',
               'python:main-liveness-test.py:LivenessTests.test_channel_timer_changes_do_not_confirm_a_stall',
               'python:main-liveness-test.py:LivenessTests.test_adjacent_equal_runs_are_not_progress',
               'python:main-liveness-test.py:LivenessTests.test_partial_final_round_is_included',
               'python:main-liveness-test.py:LivenessTests.test_invalid_coverage_and_counters_are_unavailable',
               'python:main-liveness-test.py:LivenessTests.test_file_coverage_and_variant_filter',
               'python:main-liveness-test.py:LivenessTests.test_missing_subject_is_unavailable',
               'fixture:heal',
               'fixture:watchdog',
               'fixture:spawn',
               'fixture:capture',
               'fixture:retaliation',
               'fixture:liveness'],
 'fixtures': {'games': [{'declared_map': '01fracture',
                         'fixture': 'heal',
                         'id': 'fixture:heal:0',
                         'index': 0,
                         'map': '01fracture',
                         'opponent_role': 'heal-opponent',
                         'side': 'A',
                         'subject_role': 'subject-heal'},
                        {'declared_map': '00lanes',
                         'fixture': 'heal',
                         'id': 'fixture:heal:1',
                         'index': 1,
                         'map': '00lanes',
                         'opponent_role': 'heal-opponent',
                         'side': 'B',
                         'subject_role': 'subject-heal'},
                        {'declared_map': '01broken-curve',
                         'fixture': 'watchdog',
                         'id': 'fixture:watchdog:0',
                         'index': 0,
                         'map': '01broken-curve',
                         'opponent_role': 'watchdog-opponent',
                         'side': 'A',
                         'subject_role': 'subject'},
                        {'declared_map': '01broken-curve',
                         'fixture': 'watchdog',
                         'id': 'fixture:watchdog:1',
                         'index': 1,
                         'map': '01broken-curve',
                         'opponent_role': 'watchdog-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '00reverse-spiral',
                         'fixture': 'spawn',
                         'id': 'fixture:spawn:0',
                         'index': 0,
                         'map': '00reverse-spiral',
                         'opponent_role': 'starter-opponent',
                         'side': 'A',
                         'subject_role': 'subject'},
                        {'declared_map': '00reverse-spiral',
                         'fixture': 'spawn',
                         'id': 'fixture:spawn:1',
                         'index': 1,
                         'map': '00reverse-spiral',
                         'opponent_role': 'starter-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '01gold-rush',
                         'fixture': 'capture',
                         'id': 'fixture:capture:0',
                         'index': 0,
                         'map': '01gold-rush',
                         'opponent_role': 'capture-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '01hooks',
                         'fixture': 'retaliation',
                         'id': 'fixture:retaliation:0',
                         'index': 0,
                         'map': '01hooks',
                         'opponent_role': 'retaliation-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '01gold_rush',
                         'fixture': 'retaliation',
                         'id': 'fixture:retaliation:1',
                         'index': 1,
                         'map': '01gold-rush',
                         'opponent_role': 'retaliation-opponent',
                         'side': 'A',
                         'subject_role': 'subject'},
                        {'declared_map': '01gold_rush',
                         'fixture': 'retaliation',
                         'id': 'fixture:retaliation:2',
                         'index': 2,
                         'map': '01gold-rush',
                         'opponent_role': 'retaliation-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '00road-of-riches',
                         'fixture': 'liveness',
                         'id': 'fixture:liveness:0',
                         'index': 0,
                         'map': '00road-of-riches',
                         'opponent_role': 'starter-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '00corners',
                         'fixture': 'liveness',
                         'id': 'fixture:liveness:1',
                         'index': 1,
                         'map': '00corners',
                         'opponent_role': 'brawl-opponent',
                         'side': 'A',
                         'subject_role': 'subject'},
                        {'declared_map': '00corners',
                         'fixture': 'liveness',
                         'id': 'fixture:liveness:2',
                         'index': 2,
                         'map': '00corners',
                         'opponent_role': 'brawl-opponent',
                         'side': 'B',
                         'subject_role': 'subject'},
                        {'declared_map': '00gem-fields',
                         'fixture': 'liveness',
                         'id': 'fixture:liveness:3',
                         'index': 3,
                         'map': '00gem-fields',
                         'opponent_role': 'hunter-opponent',
                         'side': 'A',
                         'subject_role': 'subject'},
                        {'declared_map': '00gem-fields',
                         'fixture': 'liveness',
                         'id': 'fixture:liveness:4',
                         'index': 4,
                         'map': '00gem-fields',
                         'opponent_role': 'hunter-opponent',
                         'side': 'B',
                         'subject_role': 'subject'}],
              'roles': {'brawl-opponent': {'bot': 'anchors/main-v40',
                                           'expected_key': 'anchors/main-v40@5cdb05f32d84',
                                           'overrides': {'draft_mode': '16',
                                                         'focus_builds': 'true',
                                                         'hunt_mode': '1',
                                                         'hunt_radius': '12',
                                                         'squad_size': '3'},
                                           'sources': {'anchors/main-v40/src/brain.cpp': '021517ad41609be3fa18ed9d2f4241d52a09bde2c0b2d8e9b3ced9ae58eab589',
                                                       'anchors/main-v40/src/brain.h': '911bdc1f82e800acb041091152ebe234f73adb4729a617253234d7d6f502405e',
                                                       'anchors/main-v40/src/combat.cpp': '1c0b95736fa48fb20cf46b23a291981851d436864aaf583641df82af80f3eae1',
                                                       'anchors/main-v40/src/combat.h': '327d01f0837f882fbce02dc581746e9a65853c6727fe6cd71de0b70c2061647f',
                                                       'anchors/main-v40/src/config.h': 'ad7263ff4ac40f2e2b78dc9ca859c6244bd7a3b13db14f32fba7584a806be862',
                                                       'anchors/main-v40/src/ctx.cpp': 'ffbe32eb0cf13d745e266f60ae9947b7d7d3d516f571de830101166f0652c020',
                                                       'anchors/main-v40/src/ctx.h': 'ca1e527a8d0c426f913a86ee00683201cb72210c9d4f85afd39e4d7ab12aa368',
                                                       'anchors/main-v40/src/draft.cpp': '1f0e2410ab4aae40fb40ad603abfbb3a412269362f9a775552c3a8450e1225f0',
                                                       'anchors/main-v40/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                       'anchors/main-v40/src/knowledge.cpp': 'e14ecf29450def5f66ff279b48c658466ce184e4ed74e4e31bc4d3685f264764',
                                                       'anchors/main-v40/src/knowledge.h': '4f5b6d10f849c6932842a15f4050883dacc0a3b136853a5e556a12294c40b3f1',
                                                       'anchors/main-v40/src/main.cpp': '16790cb8aa09354f5a72524c454026ddbd67500c1e36493c415b4faf4b9ae05d',
                                                       'anchors/main-v40/src/model.h': '796db6173b76794c322ba0052f867dae3cdff1db43540e3f490276dd38a153e4',
                                                       'anchors/main-v40/src/nav.cpp': '6eb4a3be9c9e2cd4ce4f95541503b140dd8d5700f9811e54da9697a554ce3183',
                                                       'anchors/main-v40/src/nav.h': '5d6d34e331cd09928137da403348ede573b58e7d941cbc51d57f00b27f5bb8a1',
                                                       'anchors/main-v40/src/shop.cpp': '6a44aec39cb8a00dbd78ebdb04239bd3cef8712546871437313d2870b3c0c087',
                                                       'anchors/main-v40/src/shop.h': '7176dd7f85a91cade2a6f3197d01bb89b746e147330d5a98310e7d883bd8187f',
                                                       'anchors/main-v40/src/util.h': '669a8785cbd8e74195da017a44a307eb050f820f0ef79607248cef585527d3b9',
                                                       'anchors/main-v40/src/world.cpp': '1268746d8efdb7318019478aaf1196c8a071b7bc318d1aff1f10c001d7102636',
                                                       'anchors/main-v40/src/world.h': '6b6014a15b28716b802f4ab876c2bbbf26061592673cdd862711bed33c556c4f'}},
                        'capture-opponent': {'bot': 'anchors/main-hunter6',
                                             'expected_key': 'anchors/main-hunter6@1daf24504192',
                                             'overrides': {},
                                             'sources': {'anchors/main-hunter6/src/brain.cpp': '6cde724a3321c446bf1f9a0c0c31b30df053e44b59a89374ceeb32159e4ffdd4',
                                                         'anchors/main-hunter6/src/brain.h': '9284566b821e93b5e4ec94b444a55e8186f1a6b9eaddf5b908c1d2af73231f1a',
                                                         'anchors/main-hunter6/src/combat.cpp': '189372bfdf95f18201b14473ff2fd90c2d2b0c0f37e4d25e88f149efb02cc6f2',
                                                         'anchors/main-hunter6/src/combat.h': 'd0df717deca4121598e8770f2a5168b0850bd517e9e17a00c433c32410dc3a80',
                                                         'anchors/main-hunter6/src/config.h': '84c35b52da21b328cd1404586804a31cae4bc10e9cfc0cf1a4007997c30a72a6',
                                                         'anchors/main-hunter6/src/ctx.cpp': '0a132c6f50955fd4e5585a48fdc3d5b8f201af16e0ce049df181360d98bad838',
                                                         'anchors/main-hunter6/src/ctx.h': 'ca1e527a8d0c426f913a86ee00683201cb72210c9d4f85afd39e4d7ab12aa368',
                                                         'anchors/main-hunter6/src/draft.cpp': 'de2d679723bffae0a77bdb08e887313a7b5e1375d5e6dcbaba69f44adbccae52',
                                                         'anchors/main-hunter6/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                         'anchors/main-hunter6/src/knowledge.cpp': '84d6f5e6fc765c24abeaf81ca27a0d8f34431c2666d7eeca2ace493e323e5987',
                                                         'anchors/main-hunter6/src/knowledge.h': '1de7db691f625da7f9a08864a7f53d35ccf95aef937476643bb13cbe16074317',
                                                         'anchors/main-hunter6/src/main.cpp': 'bd155dcbf0506c682fb4c8dee4058248b6846060de108d6ebf8f568538ad2298',
                                                         'anchors/main-hunter6/src/model.h': 'bb17cdc5edf4693097f38cac21028bd601bcf8a726d75d5d74aa0f8477c512d7',
                                                         'anchors/main-hunter6/src/nav.cpp': '70f863090fdcc1bd3ade6d3514d353d2fdd73e7608d403bd06975653f882edde',
                                                         'anchors/main-hunter6/src/nav.h': 'f2c88bc57ebd4a47ca34557db5338abcd93127c3aa4677cb81ce4dbf9ed94af5',
                                                         'anchors/main-hunter6/src/shop.cpp': 'e7830f01ef41e7e48468e68a9f96c204f297ece964d018de8354a72e484e6637',
                                                         'anchors/main-hunter6/src/shop.h': '9a08cba90d67bf7557446265097c9262a60a10c3dafcccb471f5083f5e380311',
                                                         'anchors/main-hunter6/src/util.h': '1bb2b2873244cde49b061d457bd0f9180dba7766fbe87dc015142103306e48c3',
                                                         'anchors/main-hunter6/src/world.cpp': '43ea9a6347d8d4801187e8148df777d51de2c63119f267f64dfd04bc55e11406',
                                                         'anchors/main-hunter6/src/world.h': '82cae5b048f0127dc4de178ea8981d2d34c41d981572aa00c2db0c5f9625e475'}},
                        'heal-opponent': {'bot': 'anchors/main-v18',
                                          'expected_key': 'anchors/main-v18@6f7c6c87f0b5',
                                          'overrides': {},
                                          'sources': {'anchors/main-v18/src/brain.cpp': '14830bbfc028dfdf43c10a76e974feeee9af0ee801da3dd53569e529d37d1d88',
                                                      'anchors/main-v18/src/brain.h': '6ebe7f135f77a2d044eec9cf737030158b4ca29cfd27678970da1527a7708f75',
                                                      'anchors/main-v18/src/combat.cpp': '7321d5cb74435747b8f3f41780d2a51c2906c624141832fdb653bad6c24058e8',
                                                      'anchors/main-v18/src/combat.h': 'd0df717deca4121598e8770f2a5168b0850bd517e9e17a00c433c32410dc3a80',
                                                      'anchors/main-v18/src/config.h': '639b99ccd63c4b300dac4d327073a3b71f2d513c03434fee8fedb971273f5f3a',
                                                      'anchors/main-v18/src/ctx.cpp': 'ef1d098d4b71d6e9b6ab6c77e6d417a4d33b42419b2006e8632d2bf6c9345336',
                                                      'anchors/main-v18/src/ctx.h': '70db1028449b8c15d197a48cf496d596c98b859945952a0d7704e01936e8252d',
                                                      'anchors/main-v18/src/draft.cpp': '7248b73a7d571c5174ad8f0ba945d3fed5bcc9873797a962d9a3494669c4c373',
                                                      'anchors/main-v18/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                      'anchors/main-v18/src/knowledge.cpp': '84d6f5e6fc765c24abeaf81ca27a0d8f34431c2666d7eeca2ace493e323e5987',
                                                      'anchors/main-v18/src/knowledge.h': '1de7db691f625da7f9a08864a7f53d35ccf95aef937476643bb13cbe16074317',
                                                      'anchors/main-v18/src/main.cpp': 'bd155dcbf0506c682fb4c8dee4058248b6846060de108d6ebf8f568538ad2298',
                                                      'anchors/main-v18/src/model.h': 'f4e890666de475afeedf52a04d7962f1f15c823fb1751d1514a367c7c6768184',
                                                      'anchors/main-v18/src/nav.cpp': '70f863090fdcc1bd3ade6d3514d353d2fdd73e7608d403bd06975653f882edde',
                                                      'anchors/main-v18/src/nav.h': 'f2c88bc57ebd4a47ca34557db5338abcd93127c3aa4677cb81ce4dbf9ed94af5',
                                                      'anchors/main-v18/src/shop.cpp': 'a011568d866c939be853ee4ca294be0ea1a4b404527dc13e103762078c3e2f24',
                                                      'anchors/main-v18/src/shop.h': '9a08cba90d67bf7557446265097c9262a60a10c3dafcccb471f5083f5e380311',
                                                      'anchors/main-v18/src/util.h': '1bb2b2873244cde49b061d457bd0f9180dba7766fbe87dc015142103306e48c3',
                                                      'anchors/main-v18/src/world.cpp': '215cd30687639ae948e30afba9b2aeaaf58394b042a7716f5d2afa96798fb99c',
                                                      'anchors/main-v18/src/world.h': '05585a4b6d6df0f80ceb46cbd5c54c82b233825a14fd0ef861d130a09d5da426'}},
                        'hunter-opponent': {'bot': 'anchors/main-v40',
                                            'expected_key': 'anchors/main-v40@aa76b3342e10',
                                            'overrides': {'fight_better_trades_only': 'false',
                                                          'hunt_health': '0.4f',
                                                          'hunt_mode': '1',
                                                          'hunt_radius': '20',
                                                          'limited_chase': 'false'},
                                            'sources': {'anchors/main-v40/src/brain.cpp': '021517ad41609be3fa18ed9d2f4241d52a09bde2c0b2d8e9b3ced9ae58eab589',
                                                        'anchors/main-v40/src/brain.h': '911bdc1f82e800acb041091152ebe234f73adb4729a617253234d7d6f502405e',
                                                        'anchors/main-v40/src/combat.cpp': '1c0b95736fa48fb20cf46b23a291981851d436864aaf583641df82af80f3eae1',
                                                        'anchors/main-v40/src/combat.h': '327d01f0837f882fbce02dc581746e9a65853c6727fe6cd71de0b70c2061647f',
                                                        'anchors/main-v40/src/config.h': '696f102209ad5bc349700cae9dfa3b1e42a4e69b76b10e467335d51c444d63b0',
                                                        'anchors/main-v40/src/ctx.cpp': 'ffbe32eb0cf13d745e266f60ae9947b7d7d3d516f571de830101166f0652c020',
                                                        'anchors/main-v40/src/ctx.h': 'ca1e527a8d0c426f913a86ee00683201cb72210c9d4f85afd39e4d7ab12aa368',
                                                        'anchors/main-v40/src/draft.cpp': '1f0e2410ab4aae40fb40ad603abfbb3a412269362f9a775552c3a8450e1225f0',
                                                        'anchors/main-v40/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                        'anchors/main-v40/src/knowledge.cpp': 'e14ecf29450def5f66ff279b48c658466ce184e4ed74e4e31bc4d3685f264764',
                                                        'anchors/main-v40/src/knowledge.h': '4f5b6d10f849c6932842a15f4050883dacc0a3b136853a5e556a12294c40b3f1',
                                                        'anchors/main-v40/src/main.cpp': '16790cb8aa09354f5a72524c454026ddbd67500c1e36493c415b4faf4b9ae05d',
                                                        'anchors/main-v40/src/model.h': '796db6173b76794c322ba0052f867dae3cdff1db43540e3f490276dd38a153e4',
                                                        'anchors/main-v40/src/nav.cpp': '6eb4a3be9c9e2cd4ce4f95541503b140dd8d5700f9811e54da9697a554ce3183',
                                                        'anchors/main-v40/src/nav.h': '5d6d34e331cd09928137da403348ede573b58e7d941cbc51d57f00b27f5bb8a1',
                                                        'anchors/main-v40/src/shop.cpp': '6a44aec39cb8a00dbd78ebdb04239bd3cef8712546871437313d2870b3c0c087',
                                                        'anchors/main-v40/src/shop.h': '7176dd7f85a91cade2a6f3197d01bb89b746e147330d5a98310e7d883bd8187f',
                                                        'anchors/main-v40/src/util.h': '669a8785cbd8e74195da017a44a307eb050f820f0ef79607248cef585527d3b9',
                                                        'anchors/main-v40/src/world.cpp': '1268746d8efdb7318019478aaf1196c8a071b7bc318d1aff1f10c001d7102636',
                                                        'anchors/main-v40/src/world.h': '6b6014a15b28716b802f4ab876c2bbbf26061592673cdd862711bed33c556c4f'}},
                        'retaliation-opponent': {'bot': 'anchors/main-v33',
                                                 'expected_key': 'anchors/main-v33@63368cb25914',
                                                 'overrides': {'draft_mode': '16', 'focus_builds': 'true'},
                                                 'sources': {'anchors/main-v33/src/brain.cpp': 'e1ff37f6599191bbf80bd757199780dcaf478e49690f64ecda3f11bc74881805',
                                                             'anchors/main-v33/src/brain.h': 'd9f499f19aebbea03758ae80ab7a88fc0564acb13135067fe7e5ff0fde32a148',
                                                             'anchors/main-v33/src/combat.cpp': 'cc80a7e3e8653a73a0982c645c78c92b2d07251b7b459b79532d44dfd1f1d01e',
                                                             'anchors/main-v33/src/combat.h': '327d01f0837f882fbce02dc581746e9a65853c6727fe6cd71de0b70c2061647f',
                                                             'anchors/main-v33/src/config.h': '9d61a48f21b0c88cac348508ff0c7d14790146900b9cab74607b8fee0212844a',
                                                             'anchors/main-v33/src/ctx.cpp': 'ffbe32eb0cf13d745e266f60ae9947b7d7d3d516f571de830101166f0652c020',
                                                             'anchors/main-v33/src/ctx.h': '50dc50d2f38b90696dbd87adb96d0ca59c2a411470dc9bcce66d411b5dd7c7ff',
                                                             'anchors/main-v33/src/draft.cpp': 'a1498a40bffb37b3a725d3ffe2be08bf29bafd88f9367bf13a36139c6fe028db',
                                                             'anchors/main-v33/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                             'anchors/main-v33/src/knowledge.cpp': 'e67c4605c3b083f70b3ce2cc29d25f2e20ba97465babc9fec8e7449ec16f7a4a',
                                                             'anchors/main-v33/src/knowledge.h': '7062b8a5b9e9209787ad3e72f3a9a1b7809f238a8f81d2683de7ec6486abe38c',
                                                             'anchors/main-v33/src/main.cpp': '16790cb8aa09354f5a72524c454026ddbd67500c1e36493c415b4faf4b9ae05d',
                                                             'anchors/main-v33/src/model.h': '18b12233e870eac9019b447df1ec5708ca41728dbf42b529dddabf08d69c217a',
                                                             'anchors/main-v33/src/nav.cpp': '6eb4a3be9c9e2cd4ce4f95541503b140dd8d5700f9811e54da9697a554ce3183',
                                                             'anchors/main-v33/src/nav.h': '5d6d34e331cd09928137da403348ede573b58e7d941cbc51d57f00b27f5bb8a1',
                                                             'anchors/main-v33/src/shop.cpp': '3b6b8b1b1c607b5bcc2423e01940e370c1dbcba012640045f449904e2f9be79c',
                                                             'anchors/main-v33/src/shop.h': '9a08cba90d67bf7557446265097c9262a60a10c3dafcccb471f5083f5e380311',
                                                             'anchors/main-v33/src/util.h': '669a8785cbd8e74195da017a44a307eb050f820f0ef79607248cef585527d3b9',
                                                             'anchors/main-v33/src/world.cpp': '1268746d8efdb7318019478aaf1196c8a071b7bc318d1aff1f10c001d7102636',
                                                             'anchors/main-v33/src/world.h': '6b6014a15b28716b802f4ab876c2bbbf26061592673cdd862711bed33c556c4f'}},
                        'starter-opponent': {'bot': 'anchors/starter-cpp',
                                             'expected_key': 'anchors/starter-cpp@3785135fc7c7',
                                             'overrides': {},
                                             'sources': {'anchors/starter-cpp/src/bot.cpp': '6a41464c3174906ff45190a87dd57b2735dd2b6352494be8c62bb471434d1622',
                                                         'anchors/starter-cpp/src/bot.h': 'eecd001cbe86eba692d3d9d41fa0637bbf14258f2c257688bb8dfeedcd3bb9bf',
                                                         'anchors/starter-cpp/src/draft.cpp': '2c9d049104e026e52212b23fe671304d5b8d96dd36a7c241e72550063fb2f6c0',
                                                         'anchors/starter-cpp/src/draft.h': '15ddeaeb4ab39f6e3e80647fad56de9c0da7fc728d4c24ca83475060703a7bfd',
                                                         'anchors/starter-cpp/src/main.cpp': 'aa72d15d53da5734ccaa0e7d804844724ca2c9be9990f6c7f5cb3975fe9b7a2b',
                                                         'anchors/starter-cpp/src/random.h': '5ae547e5a1686c12e00be206016fef798691db1da563adb3ed83fce355284910'}},
                        'subject': {'bot': 'main/main', 'overrides': {}},
                        'subject-heal': {'bot': 'main/main', 'overrides': {'draft_mode': '14'}},
                        'watchdog-opponent': {'bot': 'anchors/main-v24',
                                              'expected_key': 'anchors/main-v24@e79333df5187',
                                              'overrides': {},
                                              'sources': {'anchors/main-v24/src/brain.cpp': '2bfc6a6e245982f82f08c495af2808322c936ef7db1b9865aa412bacfa772fe9',
                                                          'anchors/main-v24/src/brain.h': '12cb0c488f01a4b80f9099ac9f95af7c75d5617fad3a738d54e732a86cf49258',
                                                          'anchors/main-v24/src/combat.cpp': 'e0b1b8b44895d189e72b22217806a98501373bc8fee118a6f702a385c2e7f676',
                                                          'anchors/main-v24/src/combat.h': 'd0df717deca4121598e8770f2a5168b0850bd517e9e17a00c433c32410dc3a80',
                                                          'anchors/main-v24/src/config.h': '8c85f70d53c28ddc09227a224a5e8db762cb5761de89f27a4deb4fcc78ff85a3',
                                                          'anchors/main-v24/src/ctx.cpp': '0a132c6f50955fd4e5585a48fdc3d5b8f201af16e0ce049df181360d98bad838',
                                                          'anchors/main-v24/src/ctx.h': 'ca1e527a8d0c426f913a86ee00683201cb72210c9d4f85afd39e4d7ab12aa368',
                                                          'anchors/main-v24/src/draft.cpp': 'de2d679723bffae0a77bdb08e887313a7b5e1375d5e6dcbaba69f44adbccae52',
                                                          'anchors/main-v24/src/draft.h': 'b5166955e2b56a1af16baa290bc6a1554f4559aa8a3a449c3a361d5aeb4f08f2',
                                                          'anchors/main-v24/src/knowledge.cpp': '84d6f5e6fc765c24abeaf81ca27a0d8f34431c2666d7eeca2ace493e323e5987',
                                                          'anchors/main-v24/src/knowledge.h': '1de7db691f625da7f9a08864a7f53d35ccf95aef937476643bb13cbe16074317',
                                                          'anchors/main-v24/src/main.cpp': 'bd155dcbf0506c682fb4c8dee4058248b6846060de108d6ebf8f568538ad2298',
                                                          'anchors/main-v24/src/model.h': '23c18853b40514d3c6c46f06d547fa7a916e37d1bd9d2e4197ca46f8fac88905',
                                                          'anchors/main-v24/src/nav.cpp': '70f863090fdcc1bd3ade6d3514d353d2fdd73e7608d403bd06975653f882edde',
                                                          'anchors/main-v24/src/nav.h': 'f2c88bc57ebd4a47ca34557db5338abcd93127c3aa4677cb81ce4dbf9ed94af5',
                                                          'anchors/main-v24/src/shop.cpp': '67eac5217c42402479770017c8713d02f22e8f2840e039193fd03bfe139e4fc7',
                                                          'anchors/main-v24/src/shop.h': '9a08cba90d67bf7557446265097c9262a60a10c3dafcccb471f5083f5e380311',
                                                          'anchors/main-v24/src/util.h': '1bb2b2873244cde49b061d457bd0f9180dba7766fbe87dc015142103306e48c3',
                                                          'anchors/main-v24/src/world.cpp': '43ea9a6347d8d4801187e8148df777d51de2c63119f267f64dfd04bc55e11406',
                                                          'anchors/main-v24/src/world.h': '82cae5b048f0127dc4de178ea8981d2d34c41d981572aa00c2db0c5f9625e475'}}}},
 'timeout_seconds': 33600}
OWNER_TEST_ROUTES["main-production-v1"] = {
    **_MAIN_REVIEWED_ROUTE,
    "script": ROOT / _MAIN_REVIEWED_ROUTE["script"],
    "files": {ROOT / path: digest for path, digest in _MAIN_REVIEWED_ROUTE["files"].items()},
}


def _hash(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _direct(path: Path) -> Path:
    if not path.is_absolute() or path.resolve() != path or core._is_reparse(path):
        raise ArenaError(f"owner gate path is redirected or not absolute: {path}")
    return path


def _owner_root(owner: str) -> Path:
    if type(owner) is not str or owner.casefold() == "arena":
        raise ArenaError("invalid owner gate owner")
    return core.resolve_snapshot_root(ROOT / ".cache" / owner / "snap", owner)


def _owner_path(path, owner: str) -> Path:
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    base = ROOT / ".cache" / owner
    if not path.is_relative_to(base) or path == base or any(p in (".", "..") for p in path.parts):
        raise ArenaError("owner gate output/input is outside its owner namespace")
    if path.resolve() != path:
        raise ArenaError("owner gate output/input is redirected")
    return path


def _owner_from_path(path) -> tuple[Path, str]:
    """Admit the lexical owner before resolving or reading any supplied evidence path."""
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    try:
        parts = path.relative_to(ROOT / ".cache").parts
    except ValueError as exc:
        raise ArenaError("owner gate evidence is outside an owner namespace") from exc
    if len(parts) < 2:
        raise ArenaError("owner gate evidence must name an owner-local file/directory")
    owner = parts[0]
    _owner_root(owner)
    return _owner_path(path, owner), owner


def _file(path: Path) -> dict:
    _direct(path)
    if not path.is_file():
        raise ArenaError(f"owner gate input is missing: {path}")
    data = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _source_files(path: Path) -> dict:
    result = {}
    pending = [path]
    while pending:
        parent = _direct(pending.pop())
        with os.scandir(parent) as entries:
            for entry in entries:
                item = Path(entry.path)
                if item.name == ".cambc":
                    continue
                _direct(item)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(item)
                elif entry.is_file(follow_symlinks=False):
                    result[item.relative_to(path).as_posix()] = _file(item)["sha256"]
                else:
                    raise ArenaError("owner gate source contains a nonregular input")
    return dict(sorted(result.items()))


def _entry(key: str, owner: str, *, allow_overrides: bool = False) -> tuple[object, dict]:
    root = _owner_root(owner)
    ent = core.cached_snapshot(key, snapshot_root=root, snapshot_owner=owner)
    if ent.overrides and not allow_overrides:
        raise ArenaError("owner release gate supports source-only weights/configuration, not overrides")
    snap = root / ent.version
    # Describe exactly the validated build closure. Native CLI checks can leave
    # compile_commands.json and other metadata beside src/; those are outputs,
    # while quoted suite dependencies outside this bot's src/ remain inputs.
    sources = {}
    for path in core.closure(SimpleNamespace(path=ent.path)):
        path = _direct(path)
        if not path.is_relative_to(snap / "bots"):
            raise ArenaError("owner gate source closure escaped its validated snapshot")
        sources[path.relative_to(snap / "bots").as_posix()] = _file(path)["sha256"]
    return ent, {"root": root.relative_to(ROOT).as_posix(), "key": key,
                 "sources": dict(sorted(sources.items())),
                 "meta": _file(snap / "meta.json"), "toolchain": _file(snap / ".toolchain"),
                 "raw": _file(ent.path / ".cambc/build-release/bot.wasm"),
                 "sdk": _file(ent.path / ".cambc/build-release/bot-injected.wasm")}


def _runtime(*, production=False) -> dict:
    bundle_file = ROOT / "docs/reference/current-version.json"
    identity_file = ROOT / "docs/reference/engine-identity.json"
    bundle, identity = (json.loads(_direct(path).read_text(encoding="utf-8")) for path in (bundle_file, identity_file))
    cli = _file(core.CAMBC)
    if cli["sha256"] != bundle["cli"]["windowsX64"]["sha256"] or cli["sha256"] != identity["nativeCli"]["sha256"]:
        raise ArenaError("owner gate CLI differs from pinned native runtime")
    if identity["engine"]["version"] != bundle["engine"]["version"] or str(identity["engine"]["version"]) != cli_games.ENGINE:
        raise ArenaError("owner gate native engine identity or row label differs from the bundle")
    names = ["gate.py", "games.py", "core.py", "compiled_runner.py", "release.py", "health_context.py"]
    if production:
        names += ["released_proof.py", "owner_main_tests.py", "wasi.py", "wasi-run.mjs", "exp.py", "stats.py", "sweep.py",
                  "panel_executables.py", "population.py"]
    return {"cli": cli, "bundle": _file(bundle_file), "identity": _file(identity_file), "pool": _file(ROOT / "maps/pool.json"),
            "engine": identity["engine"], "toolchain": core.toolchain(),
            "tools": [_file(ROOT / "tools/arena" / name) for name in names]}


def _owner_pool() -> dict:
    catalog = json.loads(_direct(ROOT / "maps/pool.json").read_text(encoding="utf-8"))
    entries = catalog.get("maps") if isinstance(catalog, dict) else None
    if not isinstance(entries, list) or not entries:
        raise ArenaError("owner gate has no pinned public pool catalog")
    labels, ids = set(), set()
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        if (type(name) is not str or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*", name) or name.endswith(".") or
                re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", name, re.I) or name.casefold() in labels):
            raise ArenaError("owner gate pool has invalid or duplicate catalog map names")
        if (type(entry.get("sha256")) is not str or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) or
                type(entry.get("size")) is not int or entry["size"] <= 0):
            raise ArenaError("owner gate pool has an invalid catalog hash or size")
        if "id" in entry:
            ident = entry["id"]
            if type(ident) is not str or not ident or ident.casefold() in ids:
                raise ArenaError("owner gate pool has invalid or duplicate catalog map IDs")
            ids.add(ident.casefold())
        labels.add(name.casefold())
    maps, canonical = {}, set()
    public_maps = _direct(ROOT / "maps")
    for entry in entries:
        name = entry["name"]
        path = core.map_path(name)
        spellings = {name, name.replace("_", "-")}
        if path.parent != public_maps or path.suffix != ".map26a" or path.stem not in spellings:
            raise ArenaError("owner gate pool map resolved outside its direct canonical public map")
        # map_path returns a resolved path: retain the lexical reparse guard on
        # each existing direct spelling as well as on the canonical file itself.
        for spelling in spellings:
            candidate = public_maps / (spelling + ".map26a")
            if candidate.exists():
                _direct(candidate)
        if path.stem.casefold() in canonical:
            raise ArenaError("owner gate pool has duplicate canonical map aliases")
        descriptor = _file(path)
        if descriptor["sha256"] != entry.get("sha256") or descriptor["size"] != entry.get("size"):
            raise ArenaError("owner gate map bytes differ from the public pool catalog")
        canonical.add(path.stem.casefold())
        maps[path.stem] = descriptor
    return maps


def _test_route(name: str, owner: str, suite: str, bot_id: str | None = None) -> dict:
    route = OWNER_TEST_ROUTES.get(name)
    if not route or suite not in route.get("suites", ()):
        raise ArenaError("owner gate tests unsupported: no reviewed owner-local test route")
    if route.get("bots") and bot_id not in route["bots"]:
        raise ArenaError("owner gate test route is not reviewed for this exact subject bot")
    files = route.get("files")
    script = Path(route.get("script", ""))
    if not isinstance(files, dict) or script not in files:
        raise ArenaError("owner gate test route has no pinned source closure")
    descriptors = []
    for path, expected in sorted(files.items(), key=lambda item: str(item[0])):
        path = Path(path)
        if not any(path.is_relative_to(base) for base in
                   (ROOT / "tools", ROOT / "bots" / suite / "tests", ROOT / "bots" / suite / "tools",
                    ROOT / "bots" / suite / "main" / "tests", ROOT / ".cache" / owner)):
            raise ArenaError("owner gate test route source is outside its reviewed namespace")
        actual = _file(path)
        if actual["sha256"] != expected:
            raise ArenaError("owner gate test route source changed")
        descriptors.append(actual)
    result = {"route": name, "script": script.relative_to(ROOT).as_posix(), "sources": descriptors,
            "python": {"path": str(Path(sys.executable)), "sha256": hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest()},
            "argv": ["-B", script.relative_to(ROOT).as_posix(), "--owner", owner, "--inputs", "INPUTS", "--out", "OUTPUT"]}
    if route.get("version") == 2:
        inventory, plan = route.get("inventory"), route.get("fixtures")
        timeout = route.get("timeout_seconds")
        if (not isinstance(inventory, list) or len(inventory) != 46 or
                any(type(case) is not str or not case for case in inventory) or len(set(inventory)) != 46 or
                not isinstance(plan, dict) or type(timeout) is not int or timeout < 15 * 1800 or
                set(plan) != {"roles", "games"} or len(plan["roles"]) != 9 or len(plan["games"]) != 15):
            raise ArenaError("owner production route lacks its fixed complete test/fixture inventory")
        result.pop("argv")
        result.update(version=2, entrypoint="run_route", verifier="verify_result", inventory=list(inventory),
                      fixtures=plan, timeout_seconds=timeout)
        module = _route_module(result)
        result["unit_runtime"] = module.runtime_inputs()
        if not isinstance(result["unit_runtime"], dict) or not result["unit_runtime"]:
            raise ArenaError("owner production route lacks actual unit runtime bindings")
    elif "version" in route:
        raise ArenaError("unsupported reviewed owner test route version")
    return result


def _route_module(route: dict):
    """Only trusted registration can choose executable route code; manifests cannot."""
    path = _direct(ROOT / route["script"])
    if _file(path) not in route["sources"]:
        raise ArenaError("owner production route helper is outside its reviewed source closure")
    name = "_arena_owner_route_" + _file(path)["sha256"]
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ArenaError("owner production route helper cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    if not all(callable(getattr(module, attr, None)) for attr in ("runtime_inputs", "run_route", "verify_result")):
        raise ArenaError("owner production route helper lacks its fixed interface")
    return module


def _require_released_executable_proof(reference, release_record: dict, proof=None) -> dict:
    if proof is None:
        raise ArenaError("owner CLI qualification unsupported: independent released RAW/SDK executable proof is required")
    import released_proof
    return released_proof.verify(proof, reference=reference, release_record=release_record)


def _live_reference(reference, live_record: dict | None = None, *, proof=None, original_identity=False) -> dict:
    base = ROOT / "results/releases"
    live_file = base / "LIVE.json"
    current = json.loads(_direct(live_file).read_text(encoding="utf-8"))
    if live_record is not None and any(current.get(k) != live_record.get(k) for k in ("name", "anchor", "key")):
        raise ArenaError("owner gate LIVE reference changed")
    name = current.get("name")
    if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9][a-z0-9.-]*", name) or not current.get("key"):
        raise ArenaError("owner gate lacks independently recorded LIVE release evidence")
    rec_file, bundle = base / name / "release.json", base / name / "main.zip"
    rec = json.loads(_direct(rec_file).read_text(encoding="utf-8"))
    identities = {rec.get("anchor")}
    if original_identity:
        identities.add(rec.get("bot"))
    if (rec.get("name") != name or reference.bot.id not in identities or
            current.get("anchor") != rec.get("anchor") or rec.get("key") != current["key"]):
        raise ArenaError("owner gate reference is not the independently recorded LIVE release")
    with zipfile.ZipFile(_direct(bundle)) as archive:
        members = [n for n in archive.namelist() if not n.endswith("/")]
        if (not members or len(members) != len(set(members)) or
                any(not n.startswith("src/") or ".." in Path(n).parts or "\\" in n for n in members)):
            raise ArenaError("LIVE release bundle is not a safe self-contained source input")
        source_bytes = {n.removeprefix("src/"): archive.read(n) for n in members}
        packaged = {name: hashlib.sha256(data).hexdigest() for name, data in source_bytes.items()}
    if packaged != _source_files(reference.path / "src"):
        raise ArenaError("owner gate reference source differs from LIVE release bundle")
    released_bot = rec.get("bot")
    if (type(released_bot) is not str or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*", released_bot)):
        raise ArenaError("owner gate LIVE release has no verifiable original source identity")
    original = {ROOT / "bots" / released_bot / "src" / name: data for name, data in source_bytes.items()}
    if rec["key"] != f"{released_bot}@{version_of(original)}":
        raise ArenaError("owner gate LIVE bundle source differs from its recorded released key")
    reproduced = _require_released_executable_proof(reference, rec, proof) if proof is not None else _require_released_executable_proof(reference, rec)
    result = {"name": name, "anchor": rec["anchor"], "released_key": rec["key"],
              "live": _file(live_file), "record": _file(rec_file), "bundle": _file(bundle)}
    if proof is not None:
        result["executable_proof"] = reproduced
    return result


def _fixture_inputs(owner: str, subject, subject_entry: dict, plan: dict, keys: dict) -> dict:
    if not isinstance(keys, dict) or set(keys) != set(plan["roles"]):
        raise ArenaError("owner production gate requires every exact prepared fixture role")
    roles = {}
    for role, desired in plan["roles"].items():
        ent, entry = _entry(keys[role], owner, allow_overrides=True)
        settings = desired.get("overrides")
        if ent.bot.id != desired.get("bot") or dict(ent.overrides) != settings:
            raise ArenaError("owner fixture role has another bot or configuration")
        if role in ("subject", "subject-heal"):
            snap = subject.path.parents[2]
            contents = {ROOT / "bots" / name: _direct(snap / "bots" / name).read_bytes()
                        for name in subject_entry["sources"]}
            configured = core.apply_overrides(subject.bot, contents, settings) if settings else contents
            expected_key = f"{subject.bot.id}@{version_of(configured, settings)}"
        else:
            expected_key = desired.get("expected_key")
            if not isinstance(expected_key, str) or entry["sources"] != desired.get("sources"):
                raise ArenaError("owner fixture anchor role lacks a reviewed exact source identity")
        if ent.key != expected_key:
            raise ArenaError("owner fixture role source is not the reviewed exact configured input")
        roles[role] = {"bot": ent.bot.id, "overrides": dict(ent.overrides), "key": ent.key, "entry": entry}
    maps = {}
    for game in plan["games"]:
        if (not isinstance(game, dict) or game.get("subject_role") not in roles or game.get("opponent_role") not in roles or
                game.get("side") not in ("A", "B") or type(game.get("map")) is not str):
            raise ArenaError("owner fixture matrix has an invalid exact game key")
        name = game["map"]
        maps[name] = _file(ROOT / "maps" / (name + ".map26a"))
    if len({game.get("id") for game in plan["games"]}) != 15:
        raise ArenaError("owner fixture matrix has duplicate or incomplete keys")
    return {"roles": roles, "games": plan["games"], "maps": maps}


def bind_owner_inputs(owner: str, subject: str, reference: str, health: list[str], maps: list[str],
                      test_route: str, *, live_record: dict | None = None, fixture_roles=None, released_proof=None) -> dict:
    """Describe already prepared inputs; never prepare, warm, or touch shared snapshot data."""
    _owner_root(owner)
    if not health or len(health) != len(set(health)) or subject in health or subject == reference:
        raise ArenaError("owner gate needs distinct explicit health and LIVE reference inputs")
    if not maps or len(maps) != len(set(maps)):
        raise ArenaError("owner gate needs unique explicit maps")
    pool = _owner_pool()
    if any(type(name) is not str or name not in pool for name in maps):
        raise ArenaError("owner gate maps must be canonical names in the public pool")
    entrants, bindings = {}, {}
    for key in dict.fromkeys([subject, reference, *health]):
        entrants[key], bindings[key] = _entry(key, owner)
    test = _test_route(test_route, owner, entrants[subject].bot.suite, entrants[subject].bot.id)
    v2 = test.get("version") == 2
    if not v2 and len({e.bot.id for e in entrants.values()}) != len(entrants):
        raise ArenaError("owner gate has conflicting versions for one entrant identity")
    if not v2 and (fixture_roles is not None or released_proof is not None):
        raise ArenaError("owner v1 gate cannot accept partial production-route fields")
    result = {"schema": OWNER_INPUT_V2 if v2 else OWNER_INPUT_SCHEMA, "owner": owner, "subject": subject, "reference": reference,
            "health": health, "weights_policy": "source-only", "entrants": bindings,
            "maps": {name: pool[name] for name in maps}, "runtime": _runtime(production=v2), "tests": test,
            "live_reference": _live_reference(entrants[reference], live_record, proof=released_proof, original_identity=v2)}
    if v2:
        if released_proof is None:
            raise ArenaError("owner production gate requires bound released executable reproduction")
        result.update(fixtures=_fixture_inputs(owner, entrants[subject], bindings[subject], test["fixtures"], fixture_roles),
                      released_proof=released_proof)
    return result


def validate_owner_inputs(value: dict, *, live_record: dict | None = None) -> dict:
    if not isinstance(value, dict) or value.get("schema") not in (OWNER_INPUT_SCHEMA, OWNER_INPUT_V2):
        raise ArenaError("unsupported owner gate input schema")
    try:
        owner = value["owner"]
        _owner_root(owner)
        root = (ROOT / ".cache" / owner / "snap").relative_to(ROOT).as_posix()
        keys = set([value["subject"], value["reference"], *value["health"]])
        if set(value["entrants"]) != keys or any(v.get("root") != root for v in value["entrants"].values()):
            raise ArenaError("owner gate entrants conflict or escape their lexical owner root")
        v2 = value["schema"] == OWNER_INPUT_V2
        if not v2 and any(field in value for field in ("fixtures", "released_proof")):
            raise ArenaError("owner gate production fields cannot be downgraded to v1")
        options = ({"fixture_roles": {role: item["key"] for role, item in value["fixtures"]["roles"].items()},
                    "released_proof": value["released_proof"]} if v2 else {})
        actual = bind_owner_inputs(owner, value["subject"], value["reference"], value["health"],
                                   list(value["maps"]), value["tests"]["route"], live_record=live_record, **options)
    except (KeyError, TypeError, ValueError) as exc:
        raise ArenaError(f"invalid owner gate inputs: {exc}") from exc
    if value != actual:
        raise ArenaError("owner gate input bytes, source, runtime, maps, or test route changed")
    return actual


def _bound_file(value: dict, owner: str) -> Path:
    if not isinstance(value, dict) or set(value) != {"path", "size", "sha256"}:
        raise ArenaError("invalid owner gate file binding")
    path = _owner_path(value["path"], owner)
    if _file(path) != value:
        raise ArenaError("owner gate evidence file changed")
    return path


def _json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ArenaError(f"owner gate JSON is unavailable: {path}") from exc


def _process_evidence(process: dict, dispatch: dict, argv: list[str]) -> None:
    containment = process.get("containment") or {}
    if (type(process.get("returncode")) is not int or process["returncode"] != 0 or
            process.get("timed_out") is not False or process.get("tree_exit_confirmed") is not True or
            process.get("forced_termination") is not False or containment.get("kind") != "windows-private-job" or
            any(containment.get(flag) is not True for flag in
                ("assigned_before_resume", "resumed", "empty_confirmed", "kill_on_close")) or
            containment.get("forced_termination") is not False or dispatch.get("argv") != argv or
            dispatch.get("cwd") != str(ROOT)):
        raise ArenaError("owner gate command/Job evidence is not a clean contained execution")


def _test_evidence(receipt: dict, inputs: dict, out: Path) -> dict:
    owner, fingerprint = inputs["owner"], _hash(inputs)
    if inputs["tests"].get("version") == 2:
        return _production_test_evidence(receipt, inputs, out)
    if (not isinstance(receipt, dict) or receipt.get("schema") != OWNER_TEST_SCHEMA or
            receipt.get("input_fingerprint") != fingerprint or receipt.get("route") != inputs["tests"] or
            receipt.get("slots") != {"kind": "shared-core-slots", "count": 1}):
        raise ArenaError("owner gate test evidence has another input or route")
    listing = _bound_file(receipt["inputs"], owner)
    if _json(listing) != inputs or listing != out / "inputs.json":
        raise ArenaError("owner gate test command used other inputs")
    result_path = _bound_file(receipt["result"], owner)
    result = _json(result_path)
    cases = result.get("tests") if isinstance(result, dict) else None
    if (not isinstance(result, dict) or result.get("schema") != OWNER_TEST_SCHEMA or
            result.get("owner") != owner or result.get("input_fingerprint") != fingerprint or
            result.get("passed") is not True or not isinstance(cases, list) or not cases or
            any(not isinstance(c, dict) or type(c.get("name")) is not str or not c["name"] or
                c.get("passed") is not True for c in cases) or len({c["name"] for c in cases}) != len(cases)):
        raise ArenaError("owner gate tests did not produce a complete passing case result")
    dispatch_path = _bound_file(receipt["dispatch"], owner)
    process_path = _bound_file(receipt["process"], owner)
    if (not result_path.is_relative_to(out / "tests") or not process_path.is_relative_to(out / "tests") or
            dispatch_path.parent != process_path.parent):
        raise ArenaError("owner gate test evidence is outside this gate")
    argv = [str(Path(sys.executable)), "-B", str(ROOT / inputs["tests"]["script"]), "--owner", owner,
            "--inputs", str(listing), "--out", str(result_path.parent)]
    _process_evidence(_json(process_path), _json(dispatch_path), argv)
    return check("tests", "pass", f"reviewed route {inputs['tests']['route']}: {len(cases)} passing test(s)")


def _production_test_evidence(receipt: dict, inputs: dict, out: Path) -> dict:
    owner, fingerprint = inputs["owner"], _hash(inputs)
    versions = sorted({key.rsplit("@", 1)[1] for key in inputs["entrants"]} |
                      {role["key"].rsplit("@", 1)[1] for role in inputs["fixtures"]["roles"].values()})
    if (not isinstance(receipt, dict) or receipt.get("schema") != OWNER_TEST_V2 or
            receipt.get("input_fingerprint") != fingerprint or receipt.get("route") != inputs["tests"] or
            receipt.get("admission") != {"kind": "shared-core-slots", "count": 1, "versions": versions} or
            receipt.get("driver") != _file(ROOT / "tools/arena/gate.py")):
        raise ArenaError("owner production tests have another actual parent admission or route")
    listing = _bound_file(receipt["inputs"], owner)
    result_path = _bound_file(receipt["result"], owner)
    if (listing != out / "inputs.json" or _json(listing) != inputs or
            result_path.name != "tests.json" or not result_path.is_relative_to(out / "tests")):
        raise ArenaError("owner production test evidence belongs to another gate output")
    result = _json(result_path)
    cases = result.get("tests") if isinstance(result, dict) else None
    expected = inputs["tests"]["inventory"]
    if (not isinstance(result, dict) or result.get("schema") != OWNER_TEST_V2 or
            result.get("owner") != owner or result.get("input_fingerprint") != fingerprint or
            result.get("passed") is not True or not isinstance(cases, list) or len(cases) != len(expected) or
            any(not isinstance(case, dict) or case.get("passed") is not True or type(case.get("name")) is not str
                for case in cases) or {case["name"] for case in cases} != set(expected)):
        raise ArenaError("owner production tests do not cover the exact complete passing inventory")
    verified = _route_module(inputs["tests"]).verify_result(inputs, result_path.parent)
    if verified != result:
        raise ArenaError("owner production result differs from its independently verified native leaves")
    return check("tests", "pass", f"reviewed route {inputs['tests']['route']}: {len(cases)} passing test(s)")


class OwnerAdmission:
    """Actual in-process parent authority; no serialized value can create admission."""
    def __init__(self, inputs: dict, out: Path, slots, locks=None):
        self.inputs, self.out, self.slots = inputs, out, slots
        self.locks = locks or ExitStack()
        self.retained, self.released, self.dispatched = False, False, set()

    def __exit__(self, *exc):
        if not self.released:
            self.released = True
            try:
                self.locks.close()
            finally:
                self.slots.__exit__(*exc)

    def verify(self):
        if self.released or not isinstance(self.slots, core.Slots) or self.slots.count != 1 or len(self.slots.fds) != 1:
            raise ArenaError("owner production test lost its actual single-slot admission")
        validate_owner_inputs(self.inputs)

    def command(self, kind, case_id, argv, cwd, directory, timeout, *, env):
        import compiled_runner as transport
        from exp import recover_native_cleanup
        unit_ids = {name for name in self.inputs["tests"]["inventory"] if name.startswith("cpp:")}
        game_ids = {game["id"] for game in self.inputs["fixtures"]["games"]}
        kinds = {"unit-compile", "unit-run", "fixture-game"}
        directory = _owner_path(directory, self.inputs["owner"])
        if (kind not in kinds or case_id not in (game_ids if kind == "fixture-game" else unit_ids) or
                not isinstance(argv, list) or not argv or any(type(arg) is not str for arg in argv) or Path(cwd) != ROOT or
                not directory.is_relative_to(self.out) or directory == self.out or
                type(timeout) not in (int, float) or not 0 < timeout <= self.inputs["tests"]["timeout_seconds"] or
                not isinstance(env, dict) or not env or any(type(k) is not str or type(v) is not str for k, v in env.items())):
            raise ArenaError("owner production route requested an unsupported typed command")
        if kind == "fixture-game" and (argv[1:2] != ["run-batch"] or "--release" not in argv or "--rounds" in argv):
            raise ArenaError("owner production fixture command is not an unrestricted release game")
        self.verify()
        request = {"kind": kind, "case_id": case_id, "argv": argv, "cwd": str(cwd), "directory": str(directory),
                   "timeout": timeout, "env": env}
        planned = [leaf for leaf in _route_module(self.inputs["tests"]).native_plan(self.inputs, self.out)
                   if leaf["kind"] == kind and leaf["case_id"] == case_id]
        identity = (kind, case_id)
        if len(planned) != 1 or request != planned[0] or identity in self.dispatched:
            raise ArenaError("owner production command differs from its exact fixed native leaf")
        self.dispatched.add(identity)
        try:
            process = transport._command(argv, ROOT, directory, timeout, env=env)
        except BaseException as error:
            process = getattr(error, "compiled_process", None)
            if process is not None and not process.empty_confirmed:
                self.retained = True
                if not any(item["process"] is process for item in transport._PENDING_CLEANUP):
                    transport._PENDING_CLEANUP.append({"process": process, "slots": self, "directory": directory, "error": error})
            raise
        finally:
            # Cancellation cannot release the enclosing Slot or VERSION locks
            # while any owned native tree still has retained cleanup handles.
            recover_native_cleanup()
        _process_evidence(process, _json(directory / "dispatch.json"), argv)
        self.verify()
        return process


def _run_production_tests(inputs: dict, out: Path) -> dict:
    import compiled_runner as transport
    from exp import recover_native_cleanup
    directory = out / "tests" / str(time.time_ns()) / "output"
    directory.mkdir(parents=True)
    versions = sorted({key.rsplit("@", 1)[1] for key in inputs["entrants"]} |
                      {role["key"].rsplit("@", 1)[1] for role in inputs["fixtures"]["roles"].values()})
    module = _route_module(inputs["tests"])
    slots, locks = core.Slots(1, quiet=True), ExitStack()
    slots.__enter__()
    admission = OwnerAdmission(inputs, directory, slots, locks)
    try:
        for version in versions:
            locks.enter_context(core.FileLock(core.CACHE / "snap" / (version + ".lock")))
        admission.verify()
        try:
            result = module.run_route(inputs, directory, admission)
        finally:
            recover_native_cleanup()
        admission.verify()
        result_path = directory / "tests.json"
        if _json(_direct(result_path)) != result:
            raise ArenaError("owner production helper did not publish its actual complete result")
        receipt = {"schema": OWNER_TEST_V2, "input_fingerprint": _hash(inputs), "route": inputs["tests"],
                   "admission": {"kind": "shared-core-slots", "count": slots.count, "versions": versions},
                   "driver": _file(ROOT / "tools/arena/gate.py"), "inputs": _file(out / "inputs.json"),
                   "result": _file(result_path)}
        _production_test_evidence(receipt, inputs, out)
    finally:
        recover_native_cleanup()
        if not admission.retained:
            admission.__exit__(None, None, None)
    return receipt


def _run_owner_tests(inputs: dict, out: Path) -> dict:
    receipt_path = out / "tests.json"
    if receipt_path.is_file():
        receipt = _json(_direct(receipt_path))
        _test_evidence(receipt, inputs, out)
        return receipt
    if inputs["tests"].get("version") == 2:
        receipt = _run_production_tests(inputs, out)
        receipt_path.write_text(json.dumps(receipt, indent=1) + "\n", encoding="utf-8")
        return receipt
    import compiled_runner as transport
    directory = out / "tests" / str(time.time_ns())
    output = directory / "output"
    output.mkdir(parents=True)
    process_dir = directory / "process"
    listing = out / "inputs.json"
    argv = [str(Path(sys.executable)), "-B", str(ROOT / inputs["tests"]["script"]), "--owner", inputs["owner"],
            "--inputs", str(listing), "--out", str(output)]
    admitted = []
    def command(count):
        validate_owner_inputs(inputs)
        for path in (out, output, process_dir, listing):
            _owner_path(path, inputs["owner"])
        if _json(_direct(listing)) != inputs:
            raise ArenaError("owner gate test inputs changed during Slot admission")
        admitted.append(count)
        return argv
    transport._slotted_command(command, ROOT, process_dir, TEST_TIMEOUT, jobs=1)
    if admitted != [1]:
        raise ArenaError("owner gate tests did not acquire their shared slot")
    receipt = {"schema": OWNER_TEST_SCHEMA, "input_fingerprint": _hash(inputs), "route": inputs["tests"],
               "slots": {"kind": "shared-core-slots", "count": admitted[0]},
               "inputs": _file(listing), "result": _file(output / "tests.json"),
               "dispatch": _file(process_dir / "dispatch.json"), "process": _file(process_dir / "process-end.json")}
    validate_owner_inputs(inputs)
    _test_evidence(receipt, inputs, out)
    receipt_path.write_text(json.dumps(receipt, indent=1) + "\n", encoding="utf-8")
    return receipt


def _owner_specs(inputs: dict) -> tuple[list, dict]:
    owner = inputs["owner"]
    entrants = {key: core.cached_snapshot(key, snapshot_root=_owner_root(owner), snapshot_owner=owner)
                for key in inputs["entrants"]}
    subject = entrants[inputs["subject"]]
    opponents = list(dict.fromkeys([*inputs["health"], inputs["reference"]]))
    specs = []
    for key in opponents:
        for name in inputs["maps"]:
            for side in "AB":
                tags = {"subject": side, "opponent": entrants[key].bot.id}
                if inputs["schema"] == OWNER_INPUT_V2:
                    tags["opponent_key"] = key
                specs.append(GameSpec(*((subject, entrants[key]) if side == "A" else (entrants[key], subject)), name, tags))
    return specs, entrants


def _owner_rows(rows: list[dict], inputs: dict, out: Path) -> None:
    specs, entrants = _owner_specs(inputs)
    fingerprint = _hash(inputs)
    expected = {cli_games._resume_key(g.a.key, g.b.key, g.map, g.tags) for g in specs}
    if len(rows) != len(expected):
        raise ArenaError("owner gate rows do not cover its complete both-side matrix")
    seen = set()
    receipts = {}
    for row in rows:
        if (not isinstance(row, dict) or row.get("owner_input_fingerprint") != fingerprint or
                any(marker in row for marker in ("diagnostic_only", "private_snapshots")) or
                row.get("engine") != cli_games.ENGINE or row.get("winner") not in ("A", "B", "draw") or
                ("truncated" in row and row["truncated"] is not False) or type(row.get("rounds")) is not int):
            raise ArenaError("owner gate row has another mode or inputs")
        key = cli_games._resume_key(row.get("a"), row.get("b"), row.get("map"), row.get("tags"))
        if key not in expected or key in seen:
            raise ArenaError("owner gate has unexpected or duplicate game rows")
        seen.add(key)
        descriptor = row.get("owner_transport")
        receipt_path = _bound_file(descriptor, inputs["owner"])
        if not receipt_path.is_relative_to(out / "games"):
            raise ArenaError("owner gate game transport is outside this gate")
        if str(receipt_path) not in receipts:
            receipts[str(receipt_path)] = _json(receipt_path)
        receipt = receipts[str(receipt_path)]
        if (receipt.get("schema") != "arena-owner-cli-transport-v1" or receipt.get("owner") != inputs["owner"] or
                receipt.get("owner_input_fingerprint") != fingerprint or key not in receipt.get("game_keys", []) or
                receipt.get("rows_sha256", {}).get(key) != _hash({k: v for k, v in row.items() if k != "owner_transport"})):
            raise ArenaError("owner gate game transport has other inputs or keys")
    for receipt_name, receipt in receipts.items():
        manifest = _bound_file(receipt["manifest"], inputs["owner"])
        dispatch_path = _bound_file(receipt["dispatch"], inputs["owner"])
        process_path = _bound_file(receipt["process"], inputs["owner"])
        if (not manifest.is_relative_to(out / "games") or
                dispatch_path.parent != process_path.parent or process_path.parent != Path(receipt_name).parent):
            raise ArenaError("owner gate game command/Job evidence belongs to another dispatch")
        dispatch, process = _json(dispatch_path), _json(process_path)
        argv = dispatch.get("argv")
        if (not isinstance(argv, list) or len(argv) < 8 or argv[:3] != [str(core.CAMBC), "run-batch", str(manifest)] or
                argv[3:5] != ["--release", "--jobs"] or not str(argv[5]).isdigit() or int(argv[5]) < 1 or
                argv[6:8] != ["--color", "never"] or
                argv[8:] != (["--timeout", str(cli_games.GAME_TIMEOUT)] if cli_games.GAME_TIMEOUT > 0 else [])):
            raise ArenaError("owner gate did not use the supported unrestricted CLI command")
        _process_evidence(process, dispatch, argv)
        keys = receipt.get("game_keys")
        if not isinstance(keys, list) or not keys or len(keys) != len(set(keys)) or not set(keys).issubset(expected):
            raise ArenaError("owner gate transport game-key binding differs")
        lines = manifest.read_text(encoding="utf-8").splitlines()
        if len(lines) != len(keys):
            raise ArenaError("owner gate transport manifest/key count differs")
        for line, key in zip(lines, keys):
            a, b, name, _ = json.loads(key)
            fields = line.split()
            if (len(fields) != 7 or fields[0] != str(entrants[a].path) or fields[1] != str(entrants[b].path) or
                    fields[2] != str(core.map_path(name)) or fields[4] != "--statistics" or fields[6] != "--json"):
                raise ArenaError("owner gate CLI manifest differs from the bound game")
            for output_path in (fields[3], fields[5]):
                if not _owner_path(output_path, inputs["owner"]).is_relative_to(out / "games"):
                    raise ArenaError("owner gate CLI output escapes this gate")


def _owner_checks(inputs: dict, rows: list[dict], tests: dict, out: Path, minimum: float) -> list[dict]:
    _, entrants = _owner_specs(inputs)
    reference_rows = ([row for row in rows if (row.get("tags") or {}).get("opponent_key") == inputs["reference"]]
                      if inputs["schema"] == OWNER_INPUT_V2 else rows)
    return [check("build", "pass", f"{len(entrants)} prepared inputs verified"), _test_evidence(tests, inputs, out),
            health_check(rows), ref_check(reference_rows, entrants[inputs["reference"]].bot, minimum)]


def verify_owner_gate(gate_dir, *, subject_key: str, live_record: dict) -> dict:
    """Read-only release consumption of a complete bound gate; no cached builds or source fallback."""
    out, owner = _owner_from_path(gate_dir)
    record = _json(_direct(out / "gate.json"))
    if not isinstance(record, dict) or record.get("schema") != OWNER_GATE_SCHEMA:
        raise ArenaError("unsupported owner CLI gate schema")
    inputs = validate_owner_inputs(record["inputs"], live_record=live_record)
    if inputs["owner"] != owner:
        raise ArenaError("owner CLI gate embedded owner differs from its lexical namespace")
    if (record.get("owner") != inputs["owner"] or record.get("key") != subject_key or
            subject_key != inputs["subject"] or record.get("input_fingerprint") != _hash(inputs) or
            type(record.get("rounds")) is not int or record["rounds"] != 0 or record.get("pass") is not True or
            any(marker in record for marker in ("diagnostic_only", "private_snapshots"))):
        raise ArenaError("owner CLI gate identity, mode, or qualification differs")
    if set(_owner_pool()) - set(inputs["maps"]):
        raise ArenaError("owner CLI gate does not cover every pool map")
    minimum = record.get("min_ref_score")
    if type(minimum) not in (int, float) or not 0.5 <= minimum <= 1:
        raise ArenaError("owner CLI gate reference score threshold is invalid")
    rows_path = _bound_file(record["rows"], inputs["owner"])
    if rows_path != out / "games.jsonl":
        raise ArenaError("owner CLI gate rows are outside this gate")
    rows = [json.loads(line) for line in rows_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    _owner_rows(rows, inputs, out)
    tests = _json(_bound_file(record["tests"], inputs["owner"]))
    checks = _owner_checks(inputs, rows, tests, out, minimum)
    _, entrants = _owner_specs(inputs)
    if (record.get("checks") != checks or any(c["status"] != "pass" for c in checks) or
            record.get("games") != len(rows) or record.get("maps") != list(inputs["maps"]) or
            record.get("bot") != entrants[subject_key].bot.id or
            record.get("ref") != entrants[inputs["reference"]].bot.id or
            record.get("health_vs") != [entrants[k].bot.id for k in inputs["health"]] or
            record.get("evidence_fingerprint") != _hash({k: v for k, v in record.items() if k != "evidence_fingerprint"})):
        raise ArenaError("owner CLI gate checks, rows, or evidence binding differs")
    return record


def _cmd_owner_gate(args) -> int:
    manifest_path, owner = _owner_from_path(args.owner_inputs)
    inputs = validate_owner_inputs(_json(_direct(manifest_path)))
    if inputs["owner"] != owner:
        raise ArenaError("owner gate manifest embedded owner differs from its lexical namespace")
    fingerprint = _hash(inputs)
    specs, entrants = _owner_specs(inputs)
    if (args.bot != entrants[inputs["subject"]].bot.id or
            (args.ref and args.ref != entrants[inputs["reference"]].bot.id) or args.test_args or args.rounds or
            args.health_vs != DEFAULT_HEALTH_VS or
            set(_owner_pool() if args.maps == "pool" else args.maps.split(",")) != set(inputs["maps"]) or
            not 0.5 <= args.min_ref_score <= 1 or not args.out):
        raise ArenaError("owner gate arguments conflict with its explicit inputs or qualification mode")
    out = _owner_path(args.out, owner)
    root = _owner_root(owner)
    if out.is_relative_to(root) or root.is_relative_to(out):
        raise ArenaError("owner gate output cannot be a snapshot tree or source ancestor")
    if (out / "gate.json").exists():
        verify_owner_gate(out, subject_key=inputs["subject"], live_record=_json(ROOT / "results/releases/LIVE.json"))
        return 0
    checkpoint = out / "games.jsonl"
    if checkpoint.exists():
        raw = _direct(checkpoint).read_bytes()
        try:
            prior = [json.loads(line) for line in raw.splitlines() if line.strip()]
            keys = [cli_games._resume_key(r["a"], r["b"], r["map"], r["tags"]) for r in prior]
            if (len(keys) != len(set(keys)) or any(r.get("winner") not in ("A", "B", "draw") for r in prior)):
                raise ArenaError("owner gate checkpoint contains terminal failures or duplicate rows; use a new output")
            cli_games._owner_rows(checkpoint, fingerprint,
                                 {cli_games._resume_key(g.a.key, g.b.key, g.map, g.tags) for g in specs}, 0)
        except (ValueError, KeyError, TypeError) as exc:
            raise ArenaError("owner gate checkpoint is invalid") from exc
    listing = out / "inputs.json"
    if listing.exists() and _json(_direct(listing)) != inputs:
        raise ArenaError("owner gate output already binds different inputs")
    out.mkdir(parents=True, exist_ok=True)
    if not listing.exists():
        listing.write_text(json.dumps(inputs, indent=1) + "\n", encoding="utf-8")
    tests = _run_owner_tests(inputs, out)
    verify = lambda: validate_owner_inputs(inputs)
    rows = play(specs, out, jobs=args.jobs, rounds=0, keep_replays=args.keep_replays, resume=True,
                deadline=deadline_from(args.max_minutes), owner_run=cli_games.OwnerRun(owner, fingerprint, verify))
    verify()
    _owner_rows(rows, inputs, out)
    checks = _owner_checks(inputs, rows, tests, out, args.min_ref_score)
    record = {"schema": OWNER_GATE_SCHEMA, "owner": owner, "inputs": inputs, "input_fingerprint": fingerprint,
              "bot": entrants[inputs["subject"]].bot.id, "key": inputs["subject"], "ts": _now(),
              "pass": all(c["status"] == "pass" for c in checks), "maps": list(inputs["maps"]),
              "health_vs": [entrants[k].bot.id for k in inputs["health"]],
              "ref": entrants[inputs["reference"]].bot.id, "min_ref_score": args.min_ref_score,
              "rounds": 0, "games": len(rows), "checks": checks, "rows": _file(out / "games.jsonl"),
              "tests": _file(out / "tests.json")}
    record["evidence_fingerprint"] = _hash(record)
    (out / "gate.json").write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
    return 0 if record["pass"] else 1


def check(step: str, status: str, note: str, details: list[str] | None = None) -> dict:
    return {"step": step, "status": status, "note": note, "details": list(details or [])}


def run_tests(b: Bot, timeout: int = TEST_TIMEOUT, extra: list[str] | None = None) -> dict:
    """bots/<suite>/tests/run.py <bot> from the repo root; skip when the suite has no runner."""
    runner = BOTS / b.suite / "tests" / "run.py"
    if not runner.is_file():
        return check("tests", "skip", f"no {_rel(runner)}")
    cmd = [sys.executable, "-B", str(runner), b.name] + list(extra or [])
    shown = " ".join([_rel(runner), b.name] + list(extra or []))
    try:
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return check("tests", "fail", f"{shown}: timed out after {timeout}s")
    except OSError as exc:
        return check("tests", "fail", f"{shown}: cannot run: {exc}")
    lines = [l.rstrip() for l in ((proc.stdout or "") + (proc.stderr or "")).splitlines() if l.strip()]
    last = brief(lines[-1], 120) if lines else "no output"
    if proc.returncode == 0:
        return check("tests", "pass", f"{shown}: exit 0 ({last})")
    return check("tests", "fail", f"{shown}: exit {proc.returncode}", lines[-LIMIT:])


def health_check(rows: list[dict]) -> dict:
    """Subject health over every row (tags.subject): faults, exhaustion, error lines, stalls, failures."""
    offenders, totals = [], {"faults": 0, "exhausted": 0, "errors": 0, "suspended": 0, "stall": 0, "failed": 0,
                             "unmeasured": 0}
    truncated, views = 0, []
    for r in rows:
        p = perspective(r)
        if p is None:
            offenders.append(f"{r.get('map')}: no subject side in tags")
            totals["failed"] += 1
            continue
        views.append(p)
        where = f"{r.get('map')} as {p['side']} vs {p['opponent_name']}"
        if p["result"] in ("error", "skipped"):
            totals["failed"] += 1
            offenders.append(f"{where}: {p['result']}: {brief(r.get('error'))}")
            continue
        truncated += bool(p["truncated"])
        problems = health(r, p["side"])
        for item in problems:
            name, _, n = item.partition(" ")
            totals[name] = totals.get(name, 0) + (int(n) if n.isdigit() else 1)
        # Legacy complete rows predate the marker and may have pruned their
        # replay. Preserve their measurements, but never turn missing values
        # or the parser's old incompleteness notes into a clean health pass.
        measured = all(type((r.get(side) or {}).get(k)) is int and r[side][k] >= 0
                       for side in "AB"
                       for k in ("faults", "exhausted", "errors", "clears", "fuel_max", "fuel_at_budget"))
        note = str(r.get("note") or "").lower()
        measured = measured and not any(text in note for text in (
            "replay missing", "replay unreadable", "replay health unreadable", "not measured",
            "no team-a.stdout", "no team-b.stdout", "no team-a.fuel", "no team-b.fuel",
            "team-a.fuel:", "team-b.fuel:", "detailed capture incomplete"))
        for flag in ("replay_health_complete", "measurement_complete", "execution_complete", "detailedCaptureComplete"):
            if flag in r and r[flag] is not True:
                measured = False
        if not measured:
            totals["unmeasured"] += 1
            problems.append("replay health incomplete")
        if problems:
            offenders.append(f"{where}: {', '.join(problems)}" + (f"  {r['replay']}" if r.get("replay") else ""))
    fuel = max((p["fuel_max"] for p in views if p["result"] in ("win", "loss", "draw")), default=0)
    counts = (f"{totals['faults']} faults, {totals['exhausted']} exhausted, {totals['errors']} error lines, "
              f"{totals['stall']} stalls" + (f", {totals['suspended']} suspended turns" if totals["suspended"] else "")
              + (f", {totals['unmeasured']} unmeasured games" if totals["unmeasured"] else "")
              + (f", {totals['failed']} failed games" if totals["failed"] else "") + f"; fuel max {fuel}")
    if not rows:
        return check("health", "fail", "no games played")
    if offenders:
        return check("health", "fail", f"{len(offenders)} of {len(rows)} games: {counts}", offenders)
    if truncated:
        return check("health", "part", f"{len(rows)} games, {truncated} cut by --rounds (faults, exhaustion and "
                                        f"stalls unmeasured there): {counts}")
    return check("health", "pass", f"{len(rows)} games: {counts}")


def ref_check(rows: list[dict], ref: Bot, min_score: float) -> dict:
    """Head-to-head score of the subject against `ref` over finished games."""
    views = [p for p in (perspective(r) for r in rows if (r.get("tags") or {}).get("opponent") == ref.id) if p]
    t = tally(views)
    text = f"vs {ref.id}: {score_text(t)}"
    if not views:
        return check("reference", "fail", f"vs {ref.id}: no games")
    if not t["n"]:
        return check("reference", "fail" if t["failed"] else "part", f"{text} ({t['failed']} failed, "
                                                                      f"{t['truncated']} cut by --rounds)")
    ok = t["score"] >= min_score and not t["failed"]
    return check("reference", "pass" if ok else "fail",
                 f"{text} {'>=' if t['score'] >= min_score else '<'} {min_score:g}; gem margin {t['margin']:+.0f}"
                 + (f"; {t['failed']} game(s) failed" if t["failed"] else ""))


def checklist(header: str, checks: list[dict], out: Path | None) -> tuple[list[str], bool]:
    ok = all(c["status"] in PASSING for c in checks)
    lines = [header]
    for c in checks:
        lines.append(f"  {c['status'].upper() if c['status'] == 'fail' else c['status']:<7} {c['step']:<10} {c['note']}")
        shown = c["details"][:LIMIT]
        lines += [f"            {d}" for d in shown]
        if len(c["details"]) > LIMIT:
            lines.append(f"            ... {len(c['details']) - LIMIT} more in gate.json")
    passed = sum(c["status"] == "pass" for c in checks)
    skipped = sum(c["status"] == "skip" for c in checks)
    word = "PASS" if ok else "INCOMPLETE" if all(c["status"] in PASSING + ("part",) for c in checks) else "FAIL"
    lines.append(f"gate {word}: {passed} passed, {skipped} skipped, {len(checks) - passed - skipped} not passed"
                 + (f"  out {_rel(out)}" if out else ""))
    return lines, ok


def _cmd_gate(args) -> int:
    if getattr(args, "owner_inputs", ""):
        return _cmd_owner_gate(args)
    deadline = deadline_from(args.max_minutes)
    b = find_bot(args.bot)
    maps = map_set(args.maps)
    if not maps:
        raise ArenaError("no maps selected")
    ref = find_bot(args.ref) if args.ref else None
    contents = read_closure(b)  # one read: the subject (and any opponent that is the subject) builds from it
    health_bots, notes = resolve_opponents(args.health_vs, b, version_of(contents))
    out = out_dir(args.out, "runs", f"gate-{b.id}")
    t0 = time.monotonic()
    subject = prepare(b, None, "", contents=contents)
    header = (f"gate {subject.key}  maps {len(maps)} ({args.maps})  health vs {', '.join(o.id for o in health_bots)}"
              f"  ref {ref.id if ref else '-'}" + (f"  rounds {args.rounds}" if args.rounds else ""))
    print(header, flush=True)
    for n in notes:
        print(f"note: {n}", flush=True)
    checks = [check("build", "pass" if subject.ok else "fail", subject.key,
                    [] if subject.ok else tail_lines(subject.log, LIMIT))]
    rows: list[dict] = []
    stop = not subject.ok

    def failing() -> bool:
        return args.fail_fast and any(c["status"] not in PASSING for c in checks)
    if not stop:
        if (BOTS / b.suite / "tests" / "run.py").is_file():
            print(f"tests: bots/{b.suite}/tests/run.py {b.name}", flush=True)
        checks.append(run_tests(b, extra=shlex.split(args.test_args or "")))
        stop = failing()
    if not stop:
        bots = health_bots + ([ref] if ref and ref not in health_bots else [])
        ents = prepare_many([(o, None, "", contents if o == b else None) for o in bots])
        specs = [GameSpec(*((subject, e) if side == "A" else (e, subject)), m, {"subject": side, "opponent": e.bot.id})
                 for e in ents for m in maps for side in ("A", "B")]
        print(f"games: {len(specs)} ({len(bots)} opponent(s) x {len(maps)} map(s) x 2 sides)", flush=True)
        rows = play(specs, out, jobs=args.jobs, rounds=args.rounds, keep_replays=args.keep_replays,
                    resume=True, deadline=deadline)
        checks.append(health_check(rows))
        if ref and not failing():
            checks.append(ref_check(rows, ref, args.min_ref_score))
    names = ["build", "tests", "health"] + (["reference"] if ref else [])
    done = {c["step"] for c in checks}
    checks += [check(n, "not run", "an earlier check failed") for n in names if n not in done]
    if not ref:
        checks.append(check("reference", "skip", "no --ref"))
    lines, ok = checklist(header, checks, out)
    print("\n".join(lines[1:]))
    record = {"bot": b.id, "key": subject.key, "ts": _now(), "pass": ok, "maps": maps, "notes": notes,
              "health_vs": [o.id for o in health_bots], "ref": ref.id if ref else None,
              "min_ref_score": args.min_ref_score, "rounds": args.rounds, "games": len(rows),
              "seconds": round(time.monotonic() - t0), "checks": checks}
    (out / "gate.json").write_text(json.dumps(record, indent=1), encoding="utf-8")
    return 0 if ok else 1


def register(sub) -> None:
    p = sub.add_parser("gate", help="release gate: build, tests, health vs anchors, head to head vs --ref")
    p.add_argument("bot", help="a selector matching one bot")
    p.add_argument("--owner-inputs", default="", metavar="JSON",
                   help="standalone owner-bound CLI gate over a verified prepared-input manifest (reviewed tests required)")
    p.add_argument("--ref", default="", help="reference bot for the head-to-head check (e.g. anchors/main-v28)")
    p.add_argument("--maps", default="pool", help="map set (default pool)")
    p.add_argument("--health-vs", default=DEFAULT_HEALTH_VS,
                   help=f"health opponents: selectors and ladder:nearN (default {DEFAULT_HEALTH_VS})")
    p.add_argument("--min-ref-score", type=float, default=0.5, help="minimum score vs --ref (default 0.5)")
    p.add_argument("--test-args", default="", metavar="ARGS",
                   help='extra arguments for the suite tests step, e.g. --test-args "--fixtures"')
    p.add_argument("--jobs", type=int, default=0, help="machine slots to use (default: all free)")
    p.add_argument("--rounds", type=int, default=0, help="stop games after N rounds (smoke only: cannot pass)")
    p.add_argument("--out", default="", help="output dir (default .cache/arena/runs/<time>-gate-<bot>/)")
    p.add_argument("--keep-replays", default="flagged", choices=KEEP, help="replays to keep (default flagged)")
    p.add_argument("--fail-fast", action="store_true", help="stop at the first failing check")
    add_resume_args(p)
    p.set_defaults(func=_cmd_gate)
