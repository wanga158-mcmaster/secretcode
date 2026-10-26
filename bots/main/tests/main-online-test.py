"""Keeper regressions: mocked server calls and real files only in .cache/main/online-repair."""
import contextlib
import csv
import gzip
import io
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import online

class StopLoop(Exception):
    pass

class KeeperTests(unittest.TestCase):
    def cycle(self, paused, cli, fetch, teams=None, pick=None):
        with patch.object(online, 'fetch', fetch), patch.object(online, 'cli', cli), \
             patch.object(online, 'PAUSE') as pause, patch.object(online, 'ladder_teams', return_value=teams or []), \
             patch.object(online, 'survey_pick', side_effect=pick, return_value={'teamId':'opponent', 'name':'team', 'rank':10}), \
             patch.object(online, 'load', return_value=[]), patch.object(online, 'save') as save, \
             patch.object(online.time, 'sleep', side_effect=StopLoop), contextlib.redirect_stdout(io.StringIO()):
            pause.exists.side_effect = paused
            with self.assertRaises(StopLoop):
                online.keep(SimpleNamespace(hours=1, every=1))
            return save.call_count

    def test_failed_creation_still_collects_first(self):
        events = []
        def cli(*args, **kwargs):
            events.append(args[1])
            if args[1] == 'quota':
                return '{"quota":{"limit":1,"used":0}}'
            raise RuntimeError('create failed: opponent unavailable')
        self.assertEqual(self.cycle([False, False], cli, lambda args, **kw: events.append('fetch')), 0)
        self.assertEqual(events, ['fetch', 'quota', 'create'])

    def test_pause_drains_existing_groups_without_creating(self):
        fetch, cli = Mock(), Mock()
        self.cycle([True], cli, fetch)
        fetch.assert_called_once()
        cli.assert_not_called()

    def test_pause_during_fill_stops_next_creation(self):
        cli = Mock(return_value='{"quota":{"limit":5,"used":0}}')
        self.cycle([False, True], cli, Mock())
        self.assertEqual(cli.call_args_list[0].args, ('match-groups', 'quota', '--json'))
        self.assertEqual(cli.call_count, 1)

    def test_create_timeout_is_not_retried(self):
        with patch.object(online.subprocess, 'run', side_effect=subprocess.TimeoutExpired('create', 3)) as run:
            with self.assertRaisesRegex(RuntimeError, 'outcome unknown'):
                online.cli('match-groups', 'create', 'opponent', timeout=3)
            self.assertEqual(run.call_count, 1)

    def test_long_command_keeps_server_reason(self):
        self.assertEqual(online.brief_error(RuntimeError('cambc create ' + 'map,' * 100 +
                                                       ' failed: opponent unavailable')), 'opponent unavailable')

    def test_only_explicit_ineligible_409_is_classified(self):
        for apostrophe in ("'", '\u2019', '\ufffd'):
            message = f"error: Sillycate doesn{apostrophe}t have an eligible bot yet (HTTP 409)"
            with self.subTest(apostrophe=apostrophe), \
                 patch.object(online.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', message)) as run, \
                 patch.object(online.time, 'sleep') as sleep:
                with self.assertRaises(online.IneligibleOpponent):
                    online.create_group('bad', '00ambush', 'a')
                run.assert_called_once()
                sleep.assert_not_called()

    def test_other_create_errors_are_not_retried_or_classified(self):
        for message in ('error: group conflict (HTTP 409)', 'error: server busy (HTTP 500)',
                        'error: does not have an eligible bot (HTTP 500)'):
            with self.subTest(message=message), \
                 patch.object(online.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', message)) as run, \
                 patch.object(online.time, 'sleep') as sleep:
                with self.assertRaises(RuntimeError) as raised:
                    online.create_group('opponent', '00ambush', 'a')
                self.assertNotIsInstance(raised.exception, online.IneligibleOpponent)
                run.assert_called_once()
                sleep.assert_not_called()

    def test_ineligible_opponent_is_replaced_without_using_a_free_slot(self):
        teams = [{'teamId': 'bad', 'name': 'Sillycate', 'rank': 10},
                 {'teamId': 'good', 'name': 'eligible', 'rank': 30}]
        choices, created = [], []
        def pick(available, recent, rng):
            choices.append([t['teamId'] for t in available])
            return available[0]
        def cli(*args, **kwargs):
            if args[1] == 'quota':
                return '{"quota":{"limit":1,"used":0}}'
            created.append(args[2])
            if args[2] == 'bad':
                raise online.IneligibleOpponent("Sillycate doesn't have an eligible bot (HTTP 409)")
            return '{"groupId":"created-good"}'
        self.assertEqual(self.cycle([False, False, False], cli, Mock(), teams=teams, pick=pick), 1)
        self.assertEqual(created, ['bad', 'good'])
        self.assertEqual(choices, [['bad', 'good'], ['good']])

    def test_all_ineligible_candidates_end_fill_without_looping(self):
        teams = [{'teamId': 'bad', 'name': 'Sillycate', 'rank': 10}]
        created = []
        real_pick = online.survey_pick
        def cli(*args, **kwargs):
            if args[1] == 'quota':
                return '{"quota":{"limit":2,"used":0}}'
            created.append(args[2])
            raise online.IneligibleOpponent("Sillycate doesn't have an eligible bot (HTTP 409)")
        self.assertEqual(self.cycle([False, False, False], cli, Mock(), teams=teams, pick=real_pick), 0)
        self.assertEqual(created, ['bad'])

    def test_unknown_successful_create_reply_aborts_fill_without_replacement(self):
        for reply in ('not JSON', '[]', '{"maps":[]}', '{"groupId":null}', '{"groupId":""}'):
            events = []
            def cli(*args, **kwargs):
                events.append(args[1])
                return '{"quota":{"limit":2,"used":0}}' if args[1] == 'quota' else reply
            with self.subTest(reply=reply):
                self.assertEqual(self.cycle([False, False], cli, Mock()), 0)
                self.assertEqual(events, ['quota', 'create'])

    def test_unexpected_quota_payload_cannot_create_groups(self):
        for reply in ('["00ambush"]', '{"maps":["00ambush"]}', '{"quota":[]}',
                      '{"quota":{"limit":true,"used":0}}', '{"quota":{"limit":50,"used":-1}}',
                      '{"quota":{"limit":"50","used":0}}', 'not JSON'):
            events = []
            def cli(*args, **kwargs):
                events.append(args[1])
                return reply
            with self.subTest(reply=reply):
                self.assertEqual(self.cycle([False], cli, Mock()), 0)
                self.assertEqual(events, ['quota'])

    def test_recent_candidates_can_be_reused_when_no_fresh_opponent_remains(self):
        team = {'teamId': 'only', 'name': 'eligible', 'rank': 10}
        self.assertEqual(online.survey_pick([team], ['only'], online.random.Random(0)), team)
        with self.assertRaisesRegex(ValueError, 'no eligible survey opponents'):
            online.survey_pick([], [], online.random.Random(0))


class CollectionTests(unittest.TestCase):
    def setUp(self):
        scratch = online.ROOT / '.cache' / 'main' / 'online-repair'
        scratch.mkdir(parents=True, exist_ok=True)
        temp = tempfile.TemporaryDirectory(prefix='test-', dir=scratch)
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.assertTrue(self.root.is_relative_to(scratch.resolve()))
        for name, value in {
            'ONLINE': self.root / 'online', 'REGISTRY': self.root / 'online' / 'groups.json',
            'CSV': self.root / 'online' / 'online.csv', 'STAGING': self.root / 'staging',
            'PAUSE': self.root / 'pause', 'LIVE': self.root / 'LIVE.json',
        }.items():
            mocked = patch.object(online, name, value)
            mocked.start()
            self.addCleanup(mocked.stop)
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        self.calls = []

    def group(self, name):
        return {'version': '39', 'group': name, 'opponent': 'opponent', 'maps': '00ambush',
                'sides': 'a', 'state': 'pending', 'created': {}}

    def show(self, group):
        return {'matches': [{'matchId': 'm-' + group, 'map': '00ambush', 'status': 'finished',
                             'teams': [{'teamId': online.OUR_TEAM, 'side': 'a', 'result': 'win'}]}]}

    def error(self, group):
        return {'matchId': 'e-' + group, 'map': '00contest', 'status': 'error',
                'finishedAt': '2026-10-22T23:03:47.701Z', 'teams': [],
                'error': {'message': 'match failed before rosters were available'}}

    def server(self, *args, **kwargs):
        self.calls.append(args)
        if args[:2] == ('match-groups', 'show'):
            return json.dumps(self.show(args[2]))
        target = Path(args[args.index('--output') + 1])
        if args[:2] == ('matches', 'summary'):
            target.write_text(json.dumps({'match': {'roundsCompleted': 10}}), encoding='utf-8')
        elif args[:2] == ('match-groups', 'download'):
            with zipfile.ZipFile(target, 'w') as archive:
                archive.writestr('complete.txt', 'downloaded')
        else:
            self.fail(f'unexpected CLI call: {args}')
        return ''

    def csv_groups(self):
        with online.CSV.open(newline='', encoding='utf-8') as source:
            return [row['group'] for row in csv.DictReader(source)]

    def fetch(self, server=None):
        with patch.object(online, 'cli', side_effect=server or self.server):
            online.fetch(SimpleNamespace(), max_seconds=60)

    def test_finished_matches_and_terminal_error_complete_without_scoring_error(self):
        show = self.show('g1')
        for number in range(2, 5):
            match = self.show('g1')['matches'][0]
            match.update(matchId=f'm-g1-{number}', map=f'map-{number}')
            show['matches'].append(match)
        error = self.error('g1')
        show.update(groupId='g1', nextCursor=None, matches=show['matches'] + [error])
        online.save([self.group('g1')])
        with patch.object(self, 'show', return_value=show):
            self.fetch()
        group = online.load()[0]
        self.assertEqual(group['state'], 'done')
        self.assertEqual(group['show'], show)
        self.assertEqual(group['terminal_errors'], [error])
        self.assertEqual(len(group['results']), 4)
        self.assertTrue(all(result.endswith(':win') for result in group['results']))
        self.assertEqual(group['collection_pending'], [])
        summaries = [args[2] for args in self.calls if args[:2] == ('matches', 'summary')]
        self.assertEqual(summaries, [match['matchId'] for match in show['matches'][:-1]])
        with online.CSV.open(newline='', encoding='utf-8') as source:
            self.assertTrue(next(csv.DictReader(source))['results'].startswith('W4/4 '))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            online.status(SimpleNamespace(version='39'))
        self.assertIn('done errors=1', output.getvalue())
        with patch.object(online.scout, 'load_summary', return_value=None):
            self.assertEqual(len(online.games('39')), 4)

    def test_queued_with_terminal_error_stays_pending_and_preserves_partial_outcomes(self):
        show = self.show('g1')
        error = self.error('g1')
        show['matches'] += [error, {'matchId': 'queued', 'map': '00swirl', 'status': 'queued'}]
        online.save([self.group('g1')])
        with patch.object(self, 'show', return_value=show):
            self.fetch()
        group = online.load()[0]
        self.assertEqual(group['state'], 'pending')
        self.assertEqual(group['show'], show)
        self.assertEqual(group['terminal_errors'], [error])
        self.assertNotIn('results', group)
        self.assertNotIn('collection_pending', group)
        self.assertEqual(self.calls, [('match-groups', 'show', 'g1', '--json')])

    def test_unknown_status_or_error_without_finished_timestamp_stays_pending(self):
        for status, timestamp in [('cancelled', '2026-10-22T23:03:47.701Z'),
                                  ('error', None), ('error', ''), ('error', '   '), ('error', 1)]:
            with self.subTest(status=status, timestamp=timestamp):
                error = self.error('g1')
                error.update(status=status, finishedAt=timestamp)
                show = self.show('g1')
                show['matches'].append(error)
                online.save([self.group('g1')])
                self.calls.clear()
                with patch.object(self, 'show', return_value=show):
                    self.fetch()
                group = online.load()[0]
                self.assertEqual(group['state'], 'pending')
                self.assertEqual(group['show'], show)
                self.assertNotIn('terminal_errors', group)
                self.assertNotIn('collection_pending', group)
                self.assertEqual(self.calls, [('match-groups', 'show', 'g1', '--json')])

    def test_all_terminal_errors_complete_with_diagnostics_and_no_scored_games(self):
        errors = [self.error('g1'), self.error('g1-second')]
        show = {'matches': errors, 'diagnostic': 'no matches could start'}
        online.save([self.group('g1')])
        with patch.object(self, 'show', return_value=show):
            self.fetch()
        group = online.load()[0]
        self.assertEqual(group['state'], 'done')
        self.assertEqual(group['show'], show)
        self.assertEqual(group['terminal_errors'], errors)
        self.assertEqual(group['results'], [])
        self.assertEqual(group['collection_pending'], [])
        self.assertFalse(any(args[:2] == ('matches', 'summary') for args in self.calls))
        self.assertEqual(self.csv_groups(), [])
        self.assertEqual(online.games('39'), {})
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            online.status(SimpleNamespace(version='39'))
        self.assertIn('done errors=2', output.getvalue())

    def test_mixed_group_archive_retry_keeps_finished_summary_and_is_idempotent(self):
        show = self.show('g1')
        error = self.error('g1')
        show['matches'].append(error)
        online.save([self.group('g1')])
        def unavailable_archive(*args, **kwargs):
            if args[:2] == ('match-groups', 'download'):
                self.calls.append(args)
                raise RuntimeError('archive unavailable for a failed match')
            return self.server(*args, **kwargs)
        with patch.object(self, 'show', return_value=show):
            self.fetch(unavailable_archive)
            self.fetch(unavailable_archive)
        group = online.load()[0]
        self.assertEqual(group['state'], 'done')
        self.assertEqual(group['collection_pending'], ['group.zip'])
        self.assertEqual(group['terminal_errors'], [error])
        packed = online.ONLINE / '39' / 'g1' / 'm-g1-summary.json.gz'
        self.assertEqual(json.loads(gzip.decompress(packed.read_bytes()))['match']['roundsCompleted'], 10)
        self.fetch()
        self.assertEqual(online.load()[0]['collection_pending'], [])
        self.assertEqual(sum(args[:2] == ('matches', 'summary') for args in self.calls), 1)
        self.assertEqual(sum(args[:2] == ('match-groups', 'show') for args in self.calls), 1)
        self.assertEqual(self.csv_groups(), ['g1'])
        before, calls = online.load(), list(self.calls)
        self.fetch()
        self.assertEqual(online.load(), before)
        self.assertEqual(self.calls, calls)

    def test_checkpoint_survives_later_group_failure_without_csv_duplicates(self):
        online.save([self.group('g1'), self.group('g2')])
        def fail_second(*args, **kwargs):
            if args[:3] == ('match-groups', 'show', 'g2'):
                raise RuntimeError('show failed')
            return self.server(*args, **kwargs)
        self.fetch(fail_second)
        self.assertEqual(online.load()[0]['state'], 'done')
        self.assertEqual(online.load()[0]['collection_pending'], [])
        self.assertEqual(self.csv_groups(), ['g1'])
        self.fetch()
        self.fetch()
        self.assertEqual(self.csv_groups(), ['g1', 'g2'])
        self.assertTrue(all(not g['collection_pending'] for g in online.load()))
        self.assertEqual(self.calls.count(('match-groups', 'show', 'g1', '--json')), 1)

    def test_partial_summary_is_preserved_and_repaired_on_next_fetch(self):
        online.save([self.group('g1')])
        dest = online.ONLINE / '39' / 'g1'
        dest.mkdir(parents=True)
        packed = dest / 'm-g1-summary.json.gz'
        packed.write_bytes(b'partial gzip')
        raw = dest / 'm-g1-summary.json'
        raw.write_text('{"match":', encoding='utf-8')
        def partial(*args, **kwargs):
            if args[:2] == ('matches', 'summary'):
                Path(args[args.index('--output') + 1]).write_text('{"match":', encoding='utf-8')
                return ''
            return self.server(*args, **kwargs)
        self.fetch(partial)
        self.assertEqual(online.load()[0]['state'], 'done')
        self.assertIn('m-g1', online.load()[0]['collection_pending'])
        self.assertEqual(len(list(dest.glob('*.invalid-*'))), 2)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            online.status(SimpleNamespace(version='39'))
        self.assertIn('artifacts-pending=', output.getvalue())
        self.fetch()
        self.assertEqual(online.load()[0]['collection_pending'], [])
        self.assertEqual(self.csv_groups(), ['g1'])
        self.assertEqual(json.loads(gzip.decompress(packed.read_bytes()))['match']['roundsCompleted'], 10)
        # Exercise the real comparison consumer with its normal file loader.
        stats = {'gems': 100, 'g300': 30, 'c300': 50, 'kills': 1, 'deaths': 0}
        with patch.object(online.scout, 'team_stats', return_value=stats):
            result = online.games('39')[('opponent', '00ambush', 'a')]
        self.assertTrue(result['win'])
        self.assertEqual(result['rounds'], 10)

    def test_corrupt_deflate_is_preserved_and_retried(self):
        group = self.group('g1')
        group.update(state='done', show=self.show('g1'), results=['00ambush:a:win'],
                     collection_pending=['m-g1'])
        online.save([group])
        dest = online.ONLINE / '39' / 'g1'
        dest.mkdir(parents=True)
        packed = dest / 'm-g1-summary.json.gz'
        corrupt = b'\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03' + b'\xff' * 12
        with self.assertRaises(zlib.error):
            gzip.decompress(corrupt)
        packed.write_bytes(corrupt)
        self.fetch()
        self.assertEqual(online.load()[0]['collection_pending'], [])
        retained = list(dest.glob('*.invalid-*'))
        self.assertEqual(len(retained), 1)
        self.assertEqual(retained[0].read_bytes(), corrupt)

    def test_deferred_discovery_rotates_without_blocking_finished_artifacts(self):
        finished = self.group('g2')
        finished.update(state='done', show=self.show('g2'), results=['00ambush:a:win'],
                        collection_pending=['m-g2', 'group.zip'])
        online.save([self.group('g1'), finished, self.group('g3')])
        def deferred(*args, **kwargs):
            if args[:3] == ('match-groups', 'show', 'g1'):
                raise online.ReadDeferred('HTTP 429 retry after 3600 seconds')
            return self.server(*args, **kwargs)
        self.fetch(deferred)
        self.assertEqual(online.load()[1]['collection_pending'], [])
        self.assertNotIn('show_attempted', online.load()[2])
        self.fetch(deferred)
        self.assertEqual(online.load()[2]['collection_pending'], [])
        self.assertEqual(online.load()[0]['state'], 'pending')

    def test_discovery_reserves_time_for_finished_artifacts(self):
        finished = self.group('g2')
        finished.update(state='done', show=self.show('g2'), results=['00ambush:a:win'],
                        collection_pending=['m-g2'])
        online.save([self.group('g1'), finished])
        elapsed = 0.0
        timeouts = []
        def slow_status(*args, **kwargs):
            nonlocal elapsed
            timeouts.append((args[:2], kwargs['timeout']))
            if args[:2] == ('match-groups', 'show'):
                elapsed += kwargs['timeout']
                raise online.ReadDeferred('show timeout')
            return self.server(*args, **kwargs)
        with patch.object(online.time, 'monotonic', side_effect=lambda: elapsed):
            self.fetch(slow_status)
        self.assertEqual(timeouts[0], (('match-groups', 'show'), 30))
        self.assertEqual(online.load()[1]['collection_pending'], [])

    def test_archive_timeout_checkpoints_summaries_and_rotates_work(self):
        online.save([self.group('g1'), self.group('g2')])
        def slow_archive(*args, **kwargs):
            if args[:3] == ('match-groups', 'download', 'g1'):
                raise online.ReadDeferred('archive timeout')
            return self.server(*args, **kwargs)
        self.fetch(slow_archive)
        first, second = online.load()
        self.assertEqual(first['collection_pending'], ['group.zip'])
        self.assertEqual(second['state'], 'done')
        self.assertNotIn('collection_attempted', second)
        self.fetch(slow_archive)
        self.assertEqual(online.load()[1]['collection_pending'], [])
        self.assertEqual(self.csv_groups(), ['g1', 'g2'])
        self.fetch()
        self.assertTrue(all(not g['collection_pending'] for g in online.load()))
        self.assertEqual(sum(args[:3] == ('matches', 'summary', 'm-g1') for args in self.calls), 1)

    def test_registry_checkpoint_repairs_csv_after_write_failure(self):
        online.save([self.group('g1')])
        real_write = online.write_csv
        attempts = 0
        def interrupted(groups):
            nonlocal attempts
            attempts += 1
            if attempts == 2:
                raise OSError('CSV replacement interrupted')
            real_write(groups)
        with patch.object(online, 'write_csv', side_effect=interrupted):
            with self.assertRaises(OSError):
                self.fetch()
        self.assertEqual(online.load()[0]['state'], 'done')
        self.fetch()
        self.assertEqual(self.csv_groups(), ['g1'])

    def test_rate_limited_read_defers_without_sleep_and_reaches_quota(self):
        online.save([self.group('g1')])
        calls = []
        def run(argv, **kwargs):
            calls.append(argv[1:3])
            if argv[1:3] == ['match-groups', 'show']:
                return subprocess.CompletedProcess(argv, 1, '', 'HTTP 429 retry after 3600 seconds')
            self.assertEqual(argv[1:3], ['match-groups', 'quota'])
            return subprocess.CompletedProcess(argv, 0, '{"quota":{"limit":1,"used":1}}', '')
        with patch.object(online.subprocess, 'run', side_effect=run), \
             patch.object(online.time, 'sleep', side_effect=StopLoop) as sleep, \
             patch.object(online, 'ladder_teams') as ladder:
            with self.assertRaises(StopLoop):
                online.keep(SimpleNamespace(hours=1, every=1))
        self.assertEqual(calls, [['match-groups', 'show'], ['match-groups', 'quota']])
        sleep.assert_called_once_with(1)
        ladder.assert_not_called()

    def test_keeper_slice_caps_a_downloads_long_timeout(self):
        group = self.group('g1')
        group.update(state='done', show=self.show('g1'), results=['00ambush:a:win'],
                     collection_pending=['group.zip'])
        online.save([group])
        timeouts = []
        def run(argv, **kwargs):
            timeouts.append(kwargs['timeout'])
            raise subprocess.TimeoutExpired(argv, kwargs['timeout'])
        with patch.object(online.subprocess, 'run', side_effect=run):
            online.fetch(SimpleNamespace(), max_seconds=0.5)
        self.assertEqual(len(timeouts), 1)
        self.assertGreater(timeouts[0], 0)
        self.assertLessEqual(timeouts[0], 0.5)
        self.assertEqual(online.load()[0]['collection_pending'], ['group.zip'])

    def test_registry_readers_see_complete_old_or_new_json(self):
        old, new = [self.group('old')], [self.group('new')]
        online.save(old)
        replace = Path.replace
        observed = []
        def inspect(staged, target):
            self.assertEqual(online.load(), old)
            self.assertEqual(json.loads(staged.read_text(encoding='utf-8')), new)
            observed.append('old')
            result = replace(staged, target)
            self.assertEqual(online.load(), new)
            observed.append('new')
            return result
        with patch.object(Path, 'replace', inspect):
            online.save(new)
        self.assertEqual(observed, ['old', 'new'])
        with patch.object(Path, 'replace', side_effect=PermissionError('reader temporarily holds target')):
            with self.assertRaises(PermissionError):
                online.save(old)
        self.assertEqual(online.load(), new)

if __name__ == '__main__':
    unittest.main()
