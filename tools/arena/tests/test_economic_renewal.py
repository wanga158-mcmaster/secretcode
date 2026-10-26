"""Self-contained pure economic predicate regressions; provenance tested separately."""
import copy
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import economic_renewal as e


def fixture():
    actors = [{'id': 0, 'team': 'teamA', 'name': 'Enemy'},
              {'id': 5, 'team': 'teamB', 'name': 'Worker'},
              {'id': 6, 'team': 'teamB', 'name': 'Ally'}]
    def entry(round_, phase, value, ordinal, target=0, context='0'):
        return {'timestamp': {'round': round_, 'input': round_*10, 'substep': 0,
                              'phase': phase, 'ordinal': ordinal},
                'value': str(value), 'target': target, 'context': context}
    bank1 = entry(2, 'championStartTurn', 7, 20)
    clear = entry(4, 'champions', 1, 40, {'kind': 'campGroup', 'id': 10})
    capture = entry(5, 'campCaptures', 14, 50, {'kind': 'campGroup', 'id': 10}, 'campGroup')
    bank2 = entry(7, 'championStartTurn', 14, 70)
    stats = {'match': {'roundsStarted': 10, 'finalInput': 100, 'finalSubstep': 9},
             'roster': {'champions': actors},
             'events': [{'kind': kind, 'owner': dict(actor, kind='champion'), 'entries': []}
                        for actor in actors for kind in e.KINDS]}
    group(stats, 'gemSubmission')['entries'] = [bank1, bank2]
    group(stats, 'campClear')['entries'] = [clear]
    group(stats, 'gemCollection')['entries'] = [capture]
    def event(round_, label, phase, kind, observations):
        capture_event = label == 'capture'
        event_id='0000000000000000:' + ':'.join(map(str, [round_, round_*10, 0, phase, kind,
                    0 if capture_event else 1, 0 if capture_event else 5, phase, int(label == 'attack'), round_]))
        return {'id': event_id, 'record': 'event', 'kind': label, 'round': round_,
                'actor': None if capture_event else 5, 'team': None if capture_event else 1,
                'native': {'round': round_, 'input': round_*10, 'substep': 0, 'phase': phase,
                           'kind': kind, 'snapshotRound': round_,
                           'actor': {'kind': 0 if capture_event else 1, 'id': 0 if capture_event else 5},
                           'currentChampion': 4294967295 if capture_event else 5,
                           'processPhaseAfter': phase, 'awaitingInputAfter': label == 'attack'},
                'observed': observations}
    def change(entity, before, after):
        return {'entity': entity, 'before': before, 'after': after}
    events = [
        event(2, 'gems-submitted', 14, 18, [change('champion:5', {'gems':7}, {'gems':0}), change('score:1', {'value':0}, {'value':7})]),
        event(4, 'attack', 10, 5, [change('group:10', {'rewardAvailable':False}, {'rewardAvailable':True})]),
        event(5, 'capture', 7, 2, [change('champion:5', {'gems':0, 'gold':0}, {'gems':14, 'gold':26}),
                                change('group:10', {'rewardAvailable':True, 'captureTeam':1}, {'rewardAvailable':False, 'captureTeam':2})]),
        event(7, 'gems-submitted', 14, 18, [change('champion:5', {'gems':14}, {'gems':0}), change('score:1', {'value':7}, {'value':21})]),
    ]
    return stats, events


def group(stats, kind, actor=5):
    return next(g for g in stats['events'] if g['kind'] == kind and g['owner']['id'] == actor)


def set_path(value, path, replacement):
    for key in path[:-1]:
        value = value[key]
    value[path[-1]] = replacement


class EconomicSequenceTests(unittest.TestCase):
    def setUp(self):
        self.stats, self.events = fixture()

    def check(self, status):
        result = e.recognize(self.stats, self.events, 5)
        self.assertEqual(result['economic_sequence']['status'], status)
        self.assertEqual(result['behavior_coverage']['status'], 'unknown')
        self.assertFalse(result['behavior_coverage']['verified'])
        return result

    def test_supported_complete_internal_shape(self):
        result = self.check('pass')
        witness = result['economic_sequence']['witnesses'][0]
        self.assertEqual(witness['native_group'], 10)
        self.assertEqual(witness['paid_capture']['gold_gained'], 26)

    def test_missing_required_event_or_dense_table_unknown(self):
        for kind in ('campClear', 'gemCollection', 'gemSubmission', 'gemLoss', 'death'):
            with self.subTest(kind=kind):
                self.setUp()
                self.stats['events'].remove(group(self.stats, kind))
                self.check('unknown')

    def test_no_replay_point_unknown(self):
        for index in range(4):
            with self.subTest(index=index):
                self.setUp()
                del self.events[index]
                self.check('unknown')

    def test_wrong_owner_group_unknown(self):
        own = group(self.stats, 'campClear')['entries'].pop()
        group(self.stats, 'campClear', 6)['entries'].append(own)
        self.check('unknown')
        self.setUp()
        group(self.stats, 'campClear')['entries'][0]['target']['id'] = 11
        self.check('unknown')
        self.setUp()
        group(self.stats, 'gemCollection')['entries'][0]['target']['id'] = 11
        self.check('unknown')

    def test_malformed_identity_aliases_unknown(self):
        cases = [(1, ['actor'], 5.0), (1, ['team'], 1.0), (1, ['team'], True),
                 (1, ['native','actor','id'], 5.0), (1, ['native','actor','kind'], True),
                 (1, ['native','currentChampion'], 5.0), (1, ['native','kind'], 5.0),
                 (1, ['native','phase'], 10.0), (2, ['native','actor','kind'], False),
                 (2, ['native','currentChampion'], 4294967295.0)]
        for index, path, value in cases:
            with self.subTest(path=path, value=value):
                self.setUp()
                set_path(self.events[index], path, value)
                self.check('unknown')

    def test_malformed_personal_counters_unknown(self):
        cases = [('champion:5', 'after', 'gems', 14.0),
                 ('champion:5', 'before', 'gems', False),
                 ('champion:5', 'after', 'gold', float('inf')),
                 ('champion:5', 'before', 'gold', float('nan')),
                 ('group:10', 'before', 'captureTeam', True)]
        for entity, phase, field, value in cases:
            with self.subTest(entity=entity, field=field, value=value):
                self.setUp()
                observed = next(c for c in self.events[2]['observed'] if c['entity'] == entity)
                observed[phase][field] = value
                self.check('unknown')

    def test_banks_need_personal_exact_cargo_and_score(self):
        for index in (0, 3):
            self.setUp()
            e.observed(self.events[index], 'score:1')['after']['value'] = True
            self.check('unknown')
            self.setUp()
            e.observed(self.events[index], 'champion:5')['after']['gems'] = False
            self.check('unknown')

    def test_full_clock_and_ordinal_contradictions_unknown(self):
        for ordinal in (39, 50):
            with self.subTest(enemy_clear_ordinal=ordinal):
                self.setUp()
                clear = copy.deepcopy(group(self.stats, 'campClear')['entries'][0])
                clear['timestamp'].update(round=4, input=45, ordinal=ordinal)
                group(self.stats, 'campClear', 0)['entries'].append(clear)
                self.check('unknown')
        for ordinal in (70, 80):
            with self.subTest(cargo_loss_ordinal=ordinal):
                self.setUp()
                loss = copy.deepcopy(group(self.stats, 'gemCollection')['entries'][0])
                loss.update(target=0, context='0')
                loss['timestamp'].update(round=6, input=60, ordinal=ordinal)
                group(self.stats, 'gemLoss')['entries'].append(loss)
                self.check('unknown')

    def test_normal_newer_enemy_clear_and_loss_unknown(self):
        clear = copy.deepcopy(group(self.stats, 'campClear')['entries'][0])
        clear['timestamp'].update(round=4, input=45, ordinal=45)
        group(self.stats, 'campClear', 0)['entries'].append(clear)
        self.check('unknown')
        self.setUp()
        loss = copy.deepcopy(group(self.stats, 'gemCollection')['entries'][0])
        loss.update(target=0, context='0')
        loss['timestamp'].update(round=6, input=60, ordinal=60)
        group(self.stats, 'gemLoss')['entries'].append(loss)
        self.check('unknown')

    def test_equal_complete_clear_and_cargo_boundary_moments_unknown(self):
        clear = copy.deepcopy(group(self.stats, 'campClear')['entries'][0])
        clear['timestamp'].update(round=4, input=40, ordinal=41)
        group(self.stats, 'campClear', 0)['entries'].append(clear)
        self.check('unknown')
        for moment, ordinal in ((5, 51), (7, 69)):
            self.setUp()
            loss = copy.deepcopy(group(self.stats, 'gemCollection')['entries'][0])
            loss.update(target=0, context='0')
            loss['timestamp'].update(round=moment, input=moment*10, ordinal=ordinal)
            group(self.stats, 'gemLoss')['entries'].append(loss)
            self.check('unknown')

    def test_timestamp_and_roster_aliases_unknown(self):
        for field in ('round', 'input', 'substep', 'ordinal'):
            self.setUp()
            stamp = group(self.stats, 'campClear')['entries'][0]['timestamp']
            stamp[field] = float(stamp[field])
            self.check('unknown')
        self.setUp()
        self.stats['roster']['champions'][1]['id'] = 5.0
        self.check('unknown')

    def test_counter_parser_rejects_aliases(self):
        for value in (True, False, 1.0, -1, '1.0', '１', '1'*21):
            with self.subTest(value=value):
                with self.assertRaises(e.EvidenceError):
                    e.count(value)


if __name__ == '__main__':
    unittest.main(verbosity=2)
