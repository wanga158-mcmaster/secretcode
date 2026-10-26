"""Fixed Main production tests over prepared inputs and actual parent admission.

No CLI entrypoint: gate calls run_route in-process while owning its real Slot and
ordered VERSION locks. Every native leaf is brokered by that typed capability.
Original unit cases and fixture predicates are executed; no supplied pass values.
"""
from __future__ import annotations

from contextlib import ExitStack, redirect_stdout, redirect_stderr
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest.mock import patch
import zipfile

import core
import wasi

ROOT = core.ROOT
ROUTE = "main-production-v1"
SCHEMA = "arena.owner-cli-tests.v2"
ROUTE_TIMEOUT = 33600
COMPILE_TIMEOUT = 600
UNIT_TIMEOUT = 600
GAME_TIMEOUT = 1800
GAME_WALL_TIMEOUT = 1920
PYTHON_TIMEOUT = 300
MAX_REPLAY_BYTES = 256 * 1024 * 1024
MAX_STATISTICS_BYTES = 64 * 1024 * 1024
_CLOCK = time.monotonic
CPP_CASES = (('main-main-model', 'bots/main/main/tests/main-model.cpp'),
 ('main-main-retaliation-reserve', 'bots/main/main/tests/main-retaliation-reserve.cpp'),
 ('main-main-skirmish-memo', 'bots/main/main/tests/main-skirmish-memo.cpp'))
PYTHON_CASES = {'main-online-test.py': ('KeeperTests.test_failed_creation_still_collects_first',
                         'KeeperTests.test_pause_drains_existing_groups_without_creating',
                         'KeeperTests.test_pause_during_fill_stops_next_creation',
                         'KeeperTests.test_create_timeout_is_not_retried',
                         'KeeperTests.test_long_command_keeps_server_reason',
                         'KeeperTests.test_only_explicit_ineligible_409_is_classified',
                         'KeeperTests.test_other_create_errors_are_not_retried_or_classified',
                         'KeeperTests.test_ineligible_opponent_is_replaced_without_using_a_free_slot',
                         'KeeperTests.test_all_ineligible_candidates_end_fill_without_looping',
                         'KeeperTests.test_unknown_successful_create_reply_aborts_fill_without_replacement',
                         'KeeperTests.test_unexpected_quota_payload_cannot_create_groups',
                         'KeeperTests.test_recent_candidates_can_be_reused_when_no_fresh_opponent_remains',
                         'CollectionTests.test_finished_matches_and_terminal_error_complete_without_scoring_error',
                         'CollectionTests.test_queued_with_terminal_error_stays_pending_and_preserves_partial_outcomes',
                         'CollectionTests.test_unknown_status_or_error_without_finished_timestamp_stays_pending',
                         'CollectionTests.test_all_terminal_errors_complete_with_diagnostics_and_no_scored_games',
                         'CollectionTests.test_mixed_group_archive_retry_keeps_finished_summary_and_is_idempotent',
                         'CollectionTests.test_checkpoint_survives_later_group_failure_without_csv_duplicates',
                         'CollectionTests.test_partial_summary_is_preserved_and_repaired_on_next_fetch',
                         'CollectionTests.test_corrupt_deflate_is_preserved_and_retried',
                         'CollectionTests.test_deferred_discovery_rotates_without_blocking_finished_artifacts',
                         'CollectionTests.test_discovery_reserves_time_for_finished_artifacts',
                         'CollectionTests.test_archive_timeout_checkpoints_summaries_and_rotates_work',
                         'CollectionTests.test_registry_checkpoint_repairs_csv_after_write_failure',
                         'CollectionTests.test_rate_limited_read_defers_without_sleep_and_reaches_quota',
                         'CollectionTests.test_keeper_slice_caps_a_downloads_long_timeout',
                         'CollectionTests.test_registry_readers_see_complete_old_or_new_json'),
 'main-liveness-test.py': ('LivenessTests.test_rle_boundary_and_late_progress',
                           'LivenessTests.test_other_team_activity_is_not_ours',
                           'LivenessTests.test_missing_metric_is_unavailable',
                           'LivenessTests.test_missing_report_is_unavailable',
                           'LivenessTests.test_channel_timer_changes_do_not_confirm_a_stall',
                           'LivenessTests.test_adjacent_equal_runs_are_not_progress',
                           'LivenessTests.test_partial_final_round_is_included',
                           'LivenessTests.test_invalid_coverage_and_counters_are_unavailable',
                           'LivenessTests.test_file_coverage_and_variant_filter',
                           'LivenessTests.test_missing_subject_is_unavailable')}
ROLE_CONTRACT = {'subject': {'bot': 'main/main', 'overrides': {}},
 'subject-heal': {'bot': 'main/main', 'overrides': {'draft_mode': '14'}},
 'heal-opponent': {'bot': 'anchors/main-v18',
                   'overrides': {},
                   'expected_key': 'anchors/main-v18@6f7c6c87f0b5',
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
 'watchdog-opponent': {'bot': 'anchors/main-v24',
                       'overrides': {},
                       'expected_key': 'anchors/main-v24@e79333df5187',
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
                                   'anchors/main-v24/src/world.h': '82cae5b048f0127dc4de178ea8981d2d34c41d981572aa00c2db0c5f9625e475'}},
 'starter-opponent': {'bot': 'anchors/starter-cpp',
                      'overrides': {},
                      'expected_key': 'anchors/starter-cpp@3785135fc7c7',
                      'sources': {'anchors/starter-cpp/src/bot.cpp': '6a41464c3174906ff45190a87dd57b2735dd2b6352494be8c62bb471434d1622',
                                  'anchors/starter-cpp/src/bot.h': 'eecd001cbe86eba692d3d9d41fa0637bbf14258f2c257688bb8dfeedcd3bb9bf',
                                  'anchors/starter-cpp/src/draft.cpp': '2c9d049104e026e52212b23fe671304d5b8d96dd36a7c241e72550063fb2f6c0',
                                  'anchors/starter-cpp/src/draft.h': '15ddeaeb4ab39f6e3e80647fad56de9c0da7fc728d4c24ca83475060703a7bfd',
                                  'anchors/starter-cpp/src/main.cpp': 'aa72d15d53da5734ccaa0e7d804844724ca2c9be9990f6c7f5cb3975fe9b7a2b',
                                  'anchors/starter-cpp/src/random.h': '5ae547e5a1686c12e00be206016fef798691db1da563adb3ed83fce355284910'}},
 'capture-opponent': {'bot': 'anchors/main-hunter6',
                      'overrides': {},
                      'expected_key': 'anchors/main-hunter6@1daf24504192',
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
 'retaliation-opponent': {'bot': 'anchors/main-v33',
                          'overrides': {'focus_builds': 'true', 'draft_mode': '16'},
                          'expected_key': 'anchors/main-v33@63368cb25914',
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
 'brawl-opponent': {'bot': 'anchors/main-v40',
                    'overrides': {'squad_size': '3',
                                  'focus_builds': 'true',
                                  'draft_mode': '16',
                                  'hunt_mode': '1',
                                  'hunt_radius': '12'},
                    'expected_key': 'anchors/main-v40@5cdb05f32d84',
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
 'hunter-opponent': {'bot': 'anchors/main-v40',
                     'overrides': {'hunt_mode': '1',
                                   'hunt_radius': '20',
                                   'hunt_health': '0.4f',
                                   'fight_better_trades_only': 'false',
                                   'limited_chase': 'false'},
                     'expected_key': 'anchors/main-v40@aa76b3342e10',
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
                                 'anchors/main-v40/src/world.h': '6b6014a15b28716b802f4ab876c2bbbf26061592673cdd862711bed33c556c4f'}}}
GAME_CONTRACT = [{'id': 'fixture:heal:0',
  'fixture': 'heal',
  'index': 0,
  'subject_role': 'subject-heal',
  'opponent_role': 'heal-opponent',
  'map': '01fracture',
  'declared_map': '01fracture',
  'side': 'A'},
 {'id': 'fixture:heal:1',
  'fixture': 'heal',
  'index': 1,
  'subject_role': 'subject-heal',
  'opponent_role': 'heal-opponent',
  'map': '00lanes',
  'declared_map': '00lanes',
  'side': 'B'},
 {'id': 'fixture:watchdog:0',
  'fixture': 'watchdog',
  'index': 0,
  'subject_role': 'subject',
  'opponent_role': 'watchdog-opponent',
  'map': '01broken-curve',
  'declared_map': '01broken-curve',
  'side': 'A'},
 {'id': 'fixture:watchdog:1',
  'fixture': 'watchdog',
  'index': 1,
  'subject_role': 'subject',
  'opponent_role': 'watchdog-opponent',
  'map': '01broken-curve',
  'declared_map': '01broken-curve',
  'side': 'B'},
 {'id': 'fixture:spawn:0',
  'fixture': 'spawn',
  'index': 0,
  'subject_role': 'subject',
  'opponent_role': 'starter-opponent',
  'map': '00reverse-spiral',
  'declared_map': '00reverse-spiral',
  'side': 'A'},
 {'id': 'fixture:spawn:1',
  'fixture': 'spawn',
  'index': 1,
  'subject_role': 'subject',
  'opponent_role': 'starter-opponent',
  'map': '00reverse-spiral',
  'declared_map': '00reverse-spiral',
  'side': 'B'},
 {'id': 'fixture:capture:0',
  'fixture': 'capture',
  'index': 0,
  'subject_role': 'subject',
  'opponent_role': 'capture-opponent',
  'map': '01gold-rush',
  'declared_map': '01gold-rush',
  'side': 'B'},
 {'id': 'fixture:retaliation:0',
  'fixture': 'retaliation',
  'index': 0,
  'subject_role': 'subject',
  'opponent_role': 'retaliation-opponent',
  'map': '01hooks',
  'declared_map': '01hooks',
  'side': 'B'},
 {'id': 'fixture:retaliation:1',
  'fixture': 'retaliation',
  'index': 1,
  'subject_role': 'subject',
  'opponent_role': 'retaliation-opponent',
  'map': '01gold-rush',
  'declared_map': '01gold_rush',
  'side': 'A'},
 {'id': 'fixture:retaliation:2',
  'fixture': 'retaliation',
  'index': 2,
  'subject_role': 'subject',
  'opponent_role': 'retaliation-opponent',
  'map': '01gold-rush',
  'declared_map': '01gold_rush',
  'side': 'B'},
 {'id': 'fixture:liveness:0',
  'fixture': 'liveness',
  'index': 0,
  'subject_role': 'subject',
  'opponent_role': 'starter-opponent',
  'map': '00road-of-riches',
  'declared_map': '00road-of-riches',
  'side': 'B'},
 {'id': 'fixture:liveness:1',
  'fixture': 'liveness',
  'index': 1,
  'subject_role': 'subject',
  'opponent_role': 'brawl-opponent',
  'map': '00corners',
  'declared_map': '00corners',
  'side': 'A'},
 {'id': 'fixture:liveness:2',
  'fixture': 'liveness',
  'index': 2,
  'subject_role': 'subject',
  'opponent_role': 'brawl-opponent',
  'map': '00corners',
  'declared_map': '00corners',
  'side': 'B'},
 {'id': 'fixture:liveness:3',
  'fixture': 'liveness',
  'index': 3,
  'subject_role': 'subject',
  'opponent_role': 'hunter-opponent',
  'map': '00gem-fields',
  'declared_map': '00gem-fields',
  'side': 'A'},
 {'id': 'fixture:liveness:4',
  'fixture': 'liveness',
  'index': 4,
  'subject_role': 'subject',
  'opponent_role': 'hunter-opponent',
  'map': '00gem-fields',
  'declared_map': '00gem-fields',
  'side': 'B'}]
FIXTURES = ('heal', 'watchdog', 'spawn', 'capture', 'retaliation', 'liveness')
# Candidate headers come from the bound prepared subject; entry.sources pins them.
SOURCE_PATHS = ('bots/main/tests/run.py',
 'bots/main/tests/fixturelib.py',
 'bots/main/tests/main-online-test.py',
 'bots/main/tests/main-liveness-test.py',
 'bots/main/tests/main-heal-fixture.py',
 'bots/main/tests/main-watchdog-fixture.py',
 'bots/main/tests/main-spawn-fixture.py',
 'bots/main/tests/main-capture-fixture.py',
 'bots/main/tests/main-retaliation-fixture.py',
 'bots/main/tests/main-liveness-fixture.py',
 'bots/main/tools/online.py',
 'bots/main/tools/scout.py',
 'bots/main/tools/liveness.py',
 'bots/main/main/tests/main-model.cpp',
 'bots/main/main/tests/main-retaliation-reserve.cpp',
 'bots/main/main/tests/main-skirmish-memo.cpp',
 'tools/arena/core.py',
 'tools/arena/wasi.py',
 'tools/arena/wasi-run.mjs',
 'tools/arena/owner_main_tests.py')


def _hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _need(condition, message):
    if not condition:
        raise core.ArenaError(message)


def _direct(path):
    path = Path(path)
    _need(path.is_absolute() and path.resolve() == path, "route input/output redirected")
    for candidate in (path, *path.parents):
        if candidate.exists() or candidate.is_symlink():
            _need(not core._is_reparse(candidate), "route input/output ancestor redirected")
    return path


def _file(path):
    path = _direct(path)
    _need(path.is_file(), "route input/output missing: " + str(path))
    data = path.read_bytes()
    return {"path": str(path), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _owner_path(path, owner):
    path = _direct(path)
    base = ROOT / ".cache" / owner
    _need(path != base and path.is_relative_to(base), "route output outside owner")
    return path


def _write(path, value):
    _direct(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, sort_keys=True, indent=1, allow_nan=False) + "\n", encoding="utf-8", newline="\n")


def expected_cases():
    return tuple(["cpp:" + name for name, _ in CPP_CASES] +
                 ["python:" + filename + ":" + name for filename, names in PYTHON_CASES.items() for name in names] +
                 ["fixture:" + name for name in FIXTURES])


def fixture_contract(subject_bot="main/main"):
    _need(subject_bot == "main/main", "fixed Main route requires main/main subject")
    return {"roles": copy.deepcopy(ROLE_CONTRACT), "games": copy.deepcopy(GAME_CONTRACT)}


def fixture_retention():
    return {"scope": "Only the fixed15 selected fixture witnesses; no ordinary gate-panel keep-all policy.",
            "replays": 15, "max_replay_bytes": MAX_REPLAY_BYTES, "max_statistics_bytes": MAX_STATISTICS_BYTES,
            "lifecycle": "Retain under this owner qualification output while gate/release consumers need independent verification. "
                         "Compact only after that evidence lifecycle ends, with the owner; never leave unresolved hash descriptors. "
                         "Original fixture --keep changes retention only, not predicates, source, roles or configuration."}


def _unit_source_guard():
    """Fail closed on new units or unreviewed quote-include search inputs."""
    directory = _direct(ROOT / "bots/main/main/tests")
    expected = {_direct(ROOT / relative) for _, relative in CPP_CASES}
    actual = {_direct(path) for path in directory.glob("*.cpp")}
    _need(len(expected) == 3 and actual == expected and all(path.is_file() for path in expected),
          "Main C++ unit inventory changed; review the fixed route before qualification")
    reviewed = {ROOT / relative for relative in SOURCE_PATHS}
    _need(expected <= reviewed, "Main C++ units are outside the reviewed source closure")
    sdk = ROOT / "sdk/cpp"
    pending, visited = list(expected), set()
    while pending:
        source = pending.pop()
        if source in visited:
            continue
        visited.add(source)
        for include in core.INCLUDE.findall(source.read_text(encoding="utf-8")):
            candidate = Path(os.path.abspath(source.parent / include))
            if not candidate.exists() and not candidate.is_symlink():
                # Model/policy headers resolve through -I prepared-subject/src;
                # that immutable closure is already bound by entry.sources.
                continue
            _direct(candidate)
            _need(candidate.is_file() and (candidate in reviewed or candidate.is_relative_to(sdk)),
                  "unreviewed quoted unit dependency: " + str(candidate))
            if candidate.suffix in core.SOURCE_SUFFIXES:
                pending.append(candidate)
    return tuple(sorted(expected))


def runtime_inputs():
    """Pin the binaries/config/sysroot actually selected by wasi.py, not core.toolchain alone."""
    _unit_source_guard()
    # Packaged Windows apps expose LOCALAPPDATA through a logical alias. Pin
    # and execute the resolved actual runtime, while owner inputs stay strict.
    clang = _direct(Path(wasi.CLANG).resolve(strict=True))
    sdk = clang.parent.parent
    node = _direct(Path(wasi.NODE).resolve(strict=True))
    files = {clang, node, _direct(Path(sys.executable)), wasi.DRIVER,
             clang.parent / "clang++.cfg", clang.parent / "clang.cfg", clang.parent / "wasm-ld.exe"}
    # Check each ancestor once while walking. Re-resolving every header and all
    # of its ancestors makes repeated byte validation needlessly expensive.
    for folder in (sdk / "share/wasi-sysroot", sdk / "lib/clang", ROOT / "sdk/cpp"):
        _direct(folder)
        _need(folder.is_dir(), "required WASI runtime tree missing")
        pending = [folder]
        while pending:
            with os.scandir(pending.pop()) as entries:
                for entry in entries:
                    path = Path(entry.path)
                    _need(not entry.is_symlink() and not core._is_reparse(path), "runtime tree contains a redirect")
                    if entry.is_dir(follow_symlinks=False): pending.append(path)
                    elif entry.is_file(follow_symlinks=False): files.add(path)
                    else: raise core.ArenaError("runtime tree contains a nonregular file")
    # Compiler DLLs are loaded independently of the fixed clang/linker argv.
    files.update(_direct(path) for path in clang.parent.glob("*.dll"))
    environment = {"SystemRoot": os.environ.get("SystemRoot", r"C:\Windows"),
                   "USERPROFILE": str(Path.home()),
                   "LOCALAPPDATA": os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData/Local"))}
    environment["WINDIR"] = environment["SystemRoot"]
    environment["PATH"] = str(clang.parent) + os.pathsep + str(Path(environment["SystemRoot"]) / "System32")
    descriptors = []
    for path in sorted(files, key=str):
        _need(path.is_file() and not core._is_reparse(path), "runtime file changed type")
        raw = path.read_bytes()
        descriptors.append({"path": str(path), "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
    return {"files": descriptors, "flags": list(wasi.FLAGS),
            "eh": str(_direct(Path(wasi.EH).resolve(strict=True))), "environment": environment,
            "compiler": str(clang), "node": str(node), "driver": str(wasi.DRIVER)}


def _environment(inputs, out):
    base = dict(inputs["tests"]["unit_runtime"]["environment"])
    base.update(TEMP=str(out / "temp"), TMP=str(out / "temp"))
    return base


def _roles(inputs):
    _need(inputs.get("owner") == "main", "fixed Main route owner mismatch")
    actual = inputs["fixtures"]
    _need(actual["games"] == GAME_CONTRACT and set(actual["roles"]) == set(ROLE_CONTRACT), "fixture matrix changed")
    result = {}
    for name, required in ROLE_CONTRACT.items():
        row = actual["roles"][name]
        _need(row.get("bot") == required["bot"] and row.get("overrides") == required["overrides"], "fixture role configuration changed")
        entry = row["entry"]
        if "expected_key" in required:
            _need(row["key"] == required["expected_key"] and entry["sources"] == required["sources"],
                  "fixture anchor differs from the reviewed source closure")
        root = ROOT / entry["root"]
        ent = core.cached_snapshot(row["key"], snapshot_root=root, snapshot_owner=inputs["owner"])
        _need(ent.bot.id == required["bot"] and dict(ent.overrides) == required["overrides"], "prepared role identity changed")
        for label, filename in (("raw", "bot.wasm"), ("sdk", "bot-injected.wasm")):
            descriptor = entry[label]
            actual_file = _file(ent.path / ".cambc/build-release" / filename)
            _need(actual_file == {**descriptor, "path": str(ROOT / descriptor["path"])}, "prepared role payload changed")
        result[name] = ent
    _need(result["subject"].key == inputs["subject"], "default fixture subject differs from gate")
    return result


def native_plan(inputs, out):
    out = _owner_path(Path(out), inputs["owner"])
    roles = _roles(inputs)
    runtime = inputs["tests"]["unit_runtime"]
    env = _environment(inputs, out)
    leaves = []
    for name, source in CPP_CASES:
        case = "cpp:" + name
        module = out / "units" / name / "fixture.wasm"
        compile_argv = [runtime["compiler"], *runtime["flags"], "-Werror", "-I" + str(roles["subject"].path / "src"),
                        "-I" + str(ROOT / "sdk/cpp"), str(ROOT / source), "-L" + runtime["eh"], "-lunwind", "-o", str(module)]
        leaves += [{"kind": "unit-compile", "case_id": case, "argv": compile_argv, "timeout": COMPILE_TIMEOUT},
                   {"kind": "unit-run", "case_id": case,
                    "argv": [runtime["node"], "--disable-warning=ExperimentalWarning", runtime["driver"], str(module)], "timeout": UNIT_TIMEOUT}]
    for game in GAME_CONTRACT:
        manifest = out / "fixtures" / game["fixture"] / "commands" / str(game["index"]) / "manifest.txt"
        leaves.append({"kind": "fixture-game", "case_id": game["id"],
                       "argv": [str(core.CAMBC), "run-batch", str(manifest), "--release", "--jobs", "1", "--timeout", str(GAME_TIMEOUT)],
                       "timeout": GAME_WALL_TIMEOUT})
    for index, leaf in enumerate(leaves):
        leaf.update(cwd=str(ROOT), directory=str(out / "commands" / f"{index:02d}"), env=env)
    _need(len(leaves) == 21, "native leaf inventory changed")
    return leaves


def _validate_process(leaf, record, out):
    _need(record.get("returncode") == 0 and record.get("timed_out") is False and
          record.get("tree_exit_confirmed") is True and record.get("forced_termination") is False,
          "route native leaf did not cleanly finish")
    directory = _direct(Path(leaf["directory"]))
    _need(directory.is_relative_to(out), "route leaf directory escaped output")
    process = json.loads((directory / "process-end.json").read_bytes())
    dispatch = json.loads((directory / "dispatch.json").read_bytes())
    _need(process == record and dispatch["argv"] == leaf["argv"] and dispatch["cwd"] == leaf["cwd"] and
          dispatch["timeout"] == leaf["timeout"] and dispatch.get("environment") == leaf["env"],
          "route native process/dispatch evidence differs")
    containment = process.get("containment", {})
    _need(containment.get("kind") == "windows-private-job" and
          all(containment.get(key) is True for key in ("assigned_before_resume", "resumed", "empty_confirmed", "kill_on_close")) and
          containment.get("forced_termination") is False, "route native Job evidence incomplete")
    return {**leaf, "dispatch": _file(directory / "dispatch.json"), "process": _file(directory / "process-end.json"),
            "stdout": _file(directory / "stdout.bin"), "stderr": _file(directory / "stderr.bin")}


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class _Results(unittest.TestResult):
    def __init__(self, deadline):
        super().__init__()
        self.rows = []
        self.deadline = deadline

    def startTest(self, test):
        _need(_CLOCK() <= self.deadline, "Python route deadline expired")
        super().startTest(test)

    def addSuccess(self, test):
        super().addSuccess(test)
        self.rows.append({"name": test.id().split(".", 1)[1], "passed": True})


def _python_tests(out):
    rows = []
    tools = ROOT / "bots/main/tools"
    with ExitStack() as scope:
        old_path, old_modules = list(sys.path), dict(sys.modules)
        scope.callback(lambda: sys.path.__setitem__(slice(None), old_path))
        def restore_modules():
            for name in list(sys.modules):
                if name not in old_modules:
                    sys.modules.pop(name, None)
            for name, module in old_modules.items():
                sys.modules[name] = module
        scope.callback(restore_modules)
        sys.path.insert(0, str(tools))
        # Existing tests patch these leaves themselves when needed. An accidental
        # unmocked CLI leaf fails instead of issuing any online command.
        for name in ("online", "scout"):
            sys.modules.pop(name, None)
        scope.enter_context(patch.object(subprocess, "run", side_effect=AssertionError("unmocked subprocess in offline Python tests")))
        scope.enter_context(patch.object(subprocess, "Popen", side_effect=AssertionError("unmocked process in offline Python tests")))
        for filename, expected in PYTHON_CASES.items():
            module = _load("owner_main_python_tests", ROOT / "bots/main/tests" / filename)
            if filename == "main-online-test.py":
                scope.enter_context(patch.object(module.online, "ROOT", out / "python"))
            else:
                scope.enter_context(patch.object(module, "ROOT", out / "python"))
                scope.enter_context(patch.object(module.liveness, "ROOT", out / "python"))
            suite = unittest.defaultTestLoader.loadTestsFromModule(module)
            def identifiers(item):
                return [name for child in item for name in identifiers(child)] if isinstance(item, unittest.TestSuite) else [item.id().split(".", 1)[1]]
            _need(set(identifiers(suite)) == set(expected) and suite.countTestCases() == len(expected), "Python test inventory changed")
            deadline = _CLOCK() + PYTHON_TIMEOUT
            result = _Results(deadline)
            previous_trace = sys.gettrace()
            def deadline_trace(frame, event, arg):
                _need(_CLOCK() <= deadline, "Python route deadline expired")
                return deadline_trace
            try:
                sys.settrace(deadline_trace)
                suite.run(result)
            finally:
                sys.settrace(previous_trace)
            _need(result.wasSuccessful() and not result.skipped and not result.expectedFailures and not result.unexpectedSuccesses and
                  result.testsRun == len(expected) and {r["name"] for r in result.rows} == set(expected),
                  "Python route cases did not all pass: " + repr(result.errors + result.failures + result.skipped)[-6000:])
            rows += [{"name": "python:" + filename + ":" + row["name"], "passed": True} for row in result.rows]
    return rows


def _game_paths(out, game):
    directory = out / "fixtures" / game["fixture"]
    stem = f"{game['index']}-{game['declared_map']}-{game['side'].lower()}"
    return directory, directory / (stem + ".repl26a"), directory / (stem + ".json")


def _game_line(inputs, roles, out, game):
    _, replay, stats = _game_paths(out, game)
    own, opp = roles[game["subject_role"]], roles[game["opponent_role"]]
    a, b = (own, opp) if game["side"] == "A" else (opp, own)
    paths = [a.path, b.path, ROOT / "maps" / (game["map"] + ".map26a"), replay, stats]
    _need(all(not any(character.isspace() for character in str(path)) for path in paths), "fixture CLI manifest does not support whitespace paths")
    return f"{a.path} {b.path} {paths[2]} {replay} --statistics {stats} --json\n"


def _read_game(fixturelib, out, game, opponent):
    _, replay, stats = _game_paths(out, game)
    _need(_direct(replay).stat().st_size <= MAX_REPLAY_BYTES and _direct(stats).stat().st_size <= MAX_STATISTICS_BYTES,
          "fixture witness exceeds its bounded artifact size")
    with zipfile.ZipFile(replay) as archive:
        wanted = "team-" + game["side"].lower() + ".stdout"
        _need(archive.namelist().count(wanted) == 1, "fixture own stdout missing or ambiguous")
        _need(archive.getinfo(wanted).file_size <= MAX_STATISTICS_BYTES, "fixture stdout exceeds bounded expansion")
        stdout = archive.read(wanted).replace(b"\x00", b"").decode("utf-8", "replace")
    statistics = json.loads(stats.read_bytes())
    return fixturelib.Game(game["declared_map"], game["side"].lower(), opponent, replay, stats, stdout, statistics)


def _fixture_mains(inputs, out, roles, execute=None):
    rows, game_records = [], []
    with ExitStack() as scope:
        old_path = list(sys.path)
        old_modules = {name: sys.modules.get(name) for name in ("fixturelib", "liveness")}
        scope.callback(lambda: sys.path.__setitem__(slice(None), old_path))
        def restore():
            for name, module in old_modules.items():
                if module is None: sys.modules.pop(name, None)
                else: sys.modules[name] = module
            for name in FIXTURES:
                sys.modules.pop("owner_main_fixture_" + name, None)
        scope.callback(restore)
        sys.path.insert(0, str(ROOT / "bots/main/tools"))
        sys.path.insert(0, str(ROOT / "bots/main/tests"))
        fixturelib = _load("fixturelib", ROOT / "bots/main/tests/fixturelib.py")
        _load("liveness", ROOT / "bots/main/tools/liveness.py")
        scope.enter_context(patch.object(subprocess, "run", side_effect=AssertionError("fixture attempted its original transport")))
        scope.enter_context(patch.object(subprocess, "Popen", side_effect=AssertionError("fixture attempted its original process transport")))
        scope.enter_context(patch.object(core, "prepare", side_effect=AssertionError("fixture attempted preparation")))
        scope.enter_context(patch.object(fixturelib, "out_dir", side_effect=lambda label, bot: out / "fixtures" / label.split("/", 1)[0]))
        for name in FIXTURES:
            module = _load("owner_main_fixture_" + name, ROOT / "bots/main/tests" / ("main-" + name + "-fixture.py"))
            games = [game for game in GAME_CONTRACT if game["fixture"] == name]
            called = []
            def play(label, bot, requested, overrides=None):
                _need(not called and label.split("/", 1)[0] == name and bot == "main/main", "fixture request changed")
                called.append(label)
                required_overrides = ROLE_CONTRACT[games[0]["subject_role"]]["overrides"]
                _need(dict(overrides or {}) == required_overrides and len(requested) == len(games), "fixture configuration/game count changed")
                directory = out / "fixtures" / name
                if execute is not None:
                    directory.mkdir(parents=True, exist_ok=True)
                lines = []
                values = []
                for game, request in zip(games, requested):
                    own_map, side, opponent = request
                    required = ROLE_CONTRACT[game["opponent_role"]]
                    opp_bot, _, knobs = opponent.partition("|")
                    opp_overrides = dict(value.split("=", 1) for value in knobs.split(",") if value)
                    _need((own_map, side) == (game["declared_map"], game["side"].lower()) and
                          opp_bot == required["bot"] and opp_overrides == required["overrides"], "fixture exact game changed")
                    line = _game_line(inputs, roles, out, game)
                    command_manifest = directory / "commands" / str(game["index"]) / "manifest.txt"
                    if execute is not None:
                        command_manifest.parent.mkdir(parents=True, exist_ok=True)
                        command_manifest.write_text(line, encoding="utf-8", newline="\n")
                        execute("fixture-game", game["id"])
                    else:
                        _need(command_manifest.read_text(encoding="utf-8") == line, "retained fixture command manifest changed")
                    lines.append(line)
                    values.append(_read_game(fixturelib, out, game, opponent))
                    _, replay, stats = _game_paths(out, game)
                    game_records.append({"id": game["id"], "replay": _file(replay), "statistics": _file(stats), "manifest": _file(command_manifest)})
                aggregate = directory / "manifest.txt"
                if execute is not None: aggregate.write_text("".join(lines), encoding="utf-8", newline="\n")
                else: _need(aggregate.read_text(encoding="utf-8") == "".join(lines), "fixture fingerprint manifest changed")
                return values
            def retained_write(path, data, encoding=None, errors=None, newline=None):
                _need(path == out / "fixtures/liveness/liveness-fixture.json" and path.read_text(encoding="utf-8") == data,
                      "fixture verifier attempted to change evidence")
                return len(data)
            with ExitStack() as fixture_scope:
                fixture_scope.enter_context(patch.object(fixturelib, "play", side_effect=play))
                fixture_scope.enter_context(patch.object(sys, "argv", [str(module.__file__), "--bot", "main/main", "--keep"]))
                if execute is None:
                    fixture_scope.enter_context(patch.object(Path, "write_text", new=retained_write))
                verdict = module.main()
            _need(len(called) == 1 and type(verdict) is int, "fixture did not execute its original main verdict")
            rows.append({"name": "fixture:" + name, "passed": verdict == 0})
    return rows, game_records


def run_route(inputs, out, admission):
    """Actual parent capability only; no command-line or JSON admission equivalent."""
    _need(type(admission.slots) is core.Slots and admission.slots.count == 1 and len(admission.slots.fds) == 1,
          "route requires actual entered parent Slot")
    out = _owner_path(Path(out), inputs["owner"])
    admission.verify()
    _need(inputs["tests"]["unit_runtime"] == runtime_inputs(), "unit runtime changed")
    roles = _roles(inputs)
    leaves = native_plan(inputs, out)
    receipts, cases = [], []
    (out / "temp").mkdir(parents=True, exist_ok=True)
    def execute(kind, case):
        leaf = next((row for row in leaves if row["kind"] == kind and row["case_id"] == case), None)
        _need(leaf is not None and not any(row["kind"] == kind and row["case_id"] == case for row in receipts), "unexpected/duplicate native leaf")
        directory = Path(leaf["directory"])
        directory.mkdir(parents=True, exist_ok=False)
        if kind == "unit-compile": Path(leaf["argv"][-1]).parent.mkdir(parents=True, exist_ok=True)
        # The actual parent capability validates bound inputs immediately before
        # and after its contained native command; do not hash them twice here.
        record = admission.command(kind, case, leaf["argv"], Path(leaf["cwd"]), directory, leaf["timeout"], env=leaf["env"])
        receipts.append(_validate_process(leaf, record, out))
    log = io.StringIO()
    with redirect_stdout(log), redirect_stderr(log):
        for name, _ in CPP_CASES:
            case = "cpp:" + name
            execute("unit-compile", case)
            execute("unit-run", case)
            cases.append({"name": case, "passed": True})
        cases += _python_tests(out)
        fixture_cases, games = _fixture_mains(inputs, out, roles, execute)
        cases += fixture_cases
    (out / "route.log").write_text(log.getvalue(), encoding="utf-8", newline="\n")
    _need(set(row["name"] for row in cases) == set(expected_cases()) and len(cases) == 46 and len(receipts) == 21 and len(games) == 15,
          "incomplete route case/game/command inventory")
    admission.verify()
    result = {"schema": SCHEMA, "owner": inputs["owner"], "input_fingerprint": _hash(inputs),
              "route": ROUTE, "passed": all(row["passed"] for row in cases), "tests": cases,
              "native_commands": receipts, "fixture_games": games, "fixture_retention": fixture_retention()}
    _write(out / "tests.json", result)
    return verify_result(inputs, out)


def verify_result(inputs, out):
    out = _owner_path(Path(out), inputs["owner"])
    result = json.loads((out / "tests.json").read_bytes())
    _need(inputs["tests"]["unit_runtime"] == runtime_inputs(), "unit runtime changed since execution")
    roles = _roles(inputs)
    _need(result.get("schema") == SCHEMA and result.get("owner") == inputs["owner"] and
          result.get("input_fingerprint") == _hash(inputs) and result.get("route") == ROUTE,
          "route result has another input/owner/route")
    _need(result.get("fixture_retention") == fixture_retention(), "fixture evidence lifecycle changed")
    cases = result.get("tests", [])
    _need(len(cases) == 46 and {row.get("name") for row in cases} == set(expected_cases()) and
          all(row.get("passed") is True and set(row) == {"name", "passed"} for row in cases) and result.get("passed") is True,
          "fixed route did not pass all46 actual cases")
    expected = native_plan(inputs, out)
    receipts = result.get("native_commands", [])
    _need(len(receipts) == 21, "route command receipts incomplete")
    for leaf, receipt in zip(expected, receipts):
        for key, value in leaf.items(): _need(receipt.get(key) == value, "native leaf contract changed")
        for key in ("dispatch", "process", "stdout", "stderr"):
            _need(receipt[key] == _file(Path(receipt[key]["path"])), "route command evidence changed")
        process = json.loads(Path(receipt["process"]["path"]).read_bytes())
        _need(_validate_process(leaf, process, out) == receipt, "route native receipt changed")
    # Re-evaluate the original six predicates against the retained actual games.
    # The verifier never spawns a command or rewrites pass/fail evidence.
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        fixture_cases, game_records = _fixture_mains(inputs, out, roles)
    _need(all(row["passed"] for row in fixture_cases) and result.get("fixture_games") == game_records,
          "retained fixture games or original verdict changed")
    return result
