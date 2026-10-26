"""Source fixtures for strict all-started evidence; no engine or subprocess."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import kernel_attempt_log as reader


BUILD = 'kernel-attempts-source-fixture'
HP = struct.unpack('!I', struct.pack('!f', 71.25))[0]
MAX_HP = struct.unpack('!I', struct.pack('!f', 100.5))[0]
STRENGTH = struct.unpack('!I', struct.pack('!f', 23.125))[0]
FOCUS = struct.unpack('!I', struct.pack('!f', 9.5))[0]
BEGIN = dict(v=2, build=BUILD, schema=1, actor=17, round=4, event=1, id=1,
             edge=0, kind=0, origin=1, job=0, group=-1, altar=3, dropped=0,
             hasEnd=0, outcome=5, startRound=4, x=2, y=7, bag=8,
             hpBits=HP, maxHpBits=MAX_HP, known=255, binKnown=0)
BIN = dict(v=2, build=BUILD, schema=1, actor=17, round=4, id=1, event=1,
           kind=0, group=-1, altar=3, known=31, champion=1, ability0=0, ability1=6,
           range=3, strengthBits=STRENGTH, focusBits=FOCUS, itemCount=2,
           items='0:2,41:1', compositionCount=0, composition='-', context=0,
           travel=8, nominal=12)


def line(fields=None, **changes):
    values = {**(BEGIN if fields is None else fields), **changes}
    return ('p4 attempt ' + ' '.join(f'{key}={value}' for key, value in values.items()) + '\n').encode('ascii')


def end(**changes):
    return {**BEGIN, 'round': 9, 'event': 2, 'edge': 1, 'hasEnd': 1, 'outcome': 0,
            'bag': 0, **changes}


def bin_line(fields=None, **changes):
    values = {**(BIN if fields is None else fields), **changes}
    return ('p4 attempt-bin ' + ' '.join(f'{key}={value}' for key, value in values.items()) + '\n').encode('ascii')


def unknown_bin(**changes):
    return {**BIN, 'known': 0, 'champion': -1, 'ability0': -1, 'ability1': -1,
            'range': -1, 'strengthBits': 0, 'focusBits': 0, 'itemCount': 0,
            'items': '-', 'compositionCount': 0, 'composition': '-',
            'context': -1, 'travel': -1, 'nominal': -1, **changes}


def no_endpoint(outcome=4, **changes):
    return end(hasEnd=0, outcome=outcome, x=-1, y=-1, bag=-1, hpBits=0,
               maxHpBits=0, known=0, **changes)


class Tests(unittest.TestCase):
    def parse(self, raw=None, **options):
        return reader.parse(line() + line(end()) if raw is None else raw,
                            expected_build=BUILD, **options)

    def codes(self, result):
        return {problem['code'] for problem in result['problems']}

    def test_complete_prefix_is_observed_not_sealed_or_fit_supported(self):
        result = self.parse(b'engine unrelated\n\x00\x00' + line() + line(end()))
        self.assertTrue(result['valid'])
        self.assertEqual(result['problems'], [])
        self.assertEqual(result['records'], 2)
        self.assertTrue(result['coverage']['available'])
        self.assertEqual(result['coverage']['observedValidPrefixRecords'], 2)
        self.assertFalse(result['fullGameSealed'])
        self.assertFalse(result['coverage']['fullGameSealed'])
        self.assertFalse(result['fitSupported'])
        self.assertEqual(set(result['unavailableFitFields']),
                         {'kit', 'composition', 'context', 'travel', 'nominal', 'economic_tape'})
        attempt = result['attempts'][0]
        self.assertEqual((attempt['kind'], attempt['origin'], attempt['outcome']),
                         ('bank', 'bootstrap', 'completed'))
        self.assertEqual(attempt['endpoint']['hpBits'], HP)
        self.assertEqual(attempt['endpoint']['hp'], 71.25)
        self.assertEqual(attempt['endpoint']['maxHp'], 100.5)
        self.assertEqual(attempt['elapsedRounds'], 5)
        self.assertFalse(attempt['fitSupported'])

    def test_all_dispositions_keep_the_original_start_without_survivor_filtering(self):
        data = b''
        for number, outcome in enumerate(range(6), 1):
            start = {**BEGIN, 'round': number * 10, 'startRound': number * 10,
                     'id': number, 'event': number * 2 - 1, 'kind': number % 2,
                     'origin': (number - 1) % 3, 'job': number}
            finish = {**start, **end(), 'round': number * 10 + 3, 'startRound': number * 10,
                      'id': number, 'event': number * 2, 'kind': number % 2,
                      'origin': (number - 1) % 3, 'job': number, 'outcome': outcome}
            if outcome in (4, 5):
                finish.update(hasEnd=0, x=-1, y=-1, bag=-1, hpBits=0, maxHpBits=0, known=0)
            if outcome == 3:
                finish['known'] &= ~32
            data += line(start) + line(finish)
        result = self.parse(data)
        self.assertTrue(result['valid'], result['problems'])
        self.assertEqual(len(result['attempts']), 6)
        self.assertEqual([a['outcome'] for a in result['attempts']], list(reader.OUTCOMES))
        self.assertEqual([a['start']['id'] for a in result['attempts']], list(range(1, 7)))
        self.assertEqual([a['endpoint'] is None for a in result['attempts']], [False] * 4 + [True] * 2)

    def test_gap_detection_round_is_not_an_endpoint_or_duration(self):
        result = self.parse(line() + line(no_endpoint(round=40)))
        attempt = result['attempts'][0]
        self.assertEqual(attempt['end']['round'], 40)
        self.assertEqual(attempt['startRound'], 4)
        self.assertIsNone(attempt['endpoint'])
        self.assertIsNone(attempt['elapsedRounds'])
        self.assertEqual(attempt['censor'], {'reason': 'observation_gap', 'round': 40})
        self.assertTrue(attempt['rightCensored'])
        self.assertIn('endpoint_unavailable', self.codes(result))
        self.assertFalse(result['coverage']['available'])
        self.assertFalse(result['coverage']['endpointComplete'])

    def test_abort_and_replacement_can_lack_a_body_endpoint(self):
        for outcome in (1, 2):
            with self.subTest(outcome=outcome):
                result = self.parse(line() + line(no_endpoint(outcome)))
                self.assertTrue(result['valid'], result['problems'])
                self.assertIsNone(result['attempts'][0]['endpoint'])
                self.assertEqual(result['attempts'][0]['outcomeCode'], outcome)

    def test_open_start_optional_final_round_censors_without_synthetic_end(self):
        open_result = self.parse(line())
        attempt = open_result['attempts'][0]
        self.assertTrue(open_result['valid'])
        self.assertIn('open_attempt', self.codes(open_result))
        self.assertIsNone(attempt['end'])
        self.assertFalse(attempt['rightCensored'])
        result = self.parse(line(), final_round=40)
        attempt = result['attempts'][0]
        self.assertTrue(attempt['rightCensored'])
        self.assertEqual(attempt['censor'], {'reason': 'final_round', 'round': 40})
        self.assertIsNone(attempt['end'])
        self.assertIsNone(attempt['endpoint'])
        self.assertIsNone(attempt['elapsedRounds'])
        self.assertFalse(result['fullGameSealed'])
        invalid = self.parse(line(), final_round=3)
        self.assertFalse(invalid['valid'])
        self.assertIn('final_round_before_observation', self.codes(invalid))
        self.assertFalse(invalid['attempts'][0]['rightCensored'])

    def test_exact_fields_duplicates_truncation_and_ascii_are_rejected(self):
        finish = line(end())
        mutations = [finish.replace(b' schema=1', b''), finish.replace(b' schema=1', b' schema=1 schema=1'),
                     finish.replace(b' schema=1', b' schema=1 extra=0'), finish.rstrip(b'\n'),
                     finish.replace(b' known=255', b' known=\xff'), finish.replace(b' known=255', b' known=0255'),
                     finish.replace(b' known=255', b' known=+255'), finish.replace(b' v=2 ', b' v=2  '),
                     finish.replace(BUILD.encode(), b'bad\tbuild'), finish.replace(b' bag=0', b' bag=1.0')]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                result = self.parse(line() + mutation)
                self.assertFalse(result['valid'])
                self.assertFalse(result['coverage']['available'])
                self.assertEqual(len(result['attempts']), 1)
                self.assertIsNone(result['attempts'][0]['end'])
                self.assertIsNone(result['attempts'][0]['endpoint'])
                self.assertEqual(result['coverage']['observedValidPrefixRecords'], 1)

    def test_bad_ranges_enums_known_masks_and_shapes(self):
        mutations = [dict(v=1), dict(schema=2), dict(actor=-1), dict(event=0), dict(id=0),
                     dict(job=-1), dict(job=2**64), dict(actor=2**31), dict(dropped=2**64),
                     dict(group=-2), dict(altar=-2), dict(x=2**31), dict(bag=2**31),
                     dict(hpBits=2**32), dict(maxHpBits=-1), dict(edge=2), dict(kind=2),
                     dict(origin=3), dict(hasEnd=2), dict(outcome=6), dict(known=256),
                     dict(binKnown=1), dict(known=32), dict(x=-1), dict(bag=-1),
                     dict(known=254), dict(known=253), dict(known=251), dict(known=247),
                     dict(hpBits=0x7f800000), dict(hpBits=0xff800000), dict(hpBits=0x7fc00001),
                     dict(maxHpBits=0x7fc00000), dict(outcome=4), dict(outcome=3),
                     dict(hasEnd=0), dict(round=3)]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                result = self.parse(line() + line(end(**mutation)))
                self.assertFalse(result['valid'], result)
                self.assertIsNone(result['attempts'][0]['end'])
        for mutation in [dict(outcome=0), dict(hasEnd=1), dict(startRound=3)]:
            with self.subTest(begin=mutation):
                result = self.parse(line(**mutation))
                self.assertFalse(result['valid'])
                self.assertEqual(result['attempts'], [])

    def test_unsigned_counter_and_unknown_observation_boundaries(self):
        unknown = dict(x=-1, y=-1, bag=-1, hpBits=0, maxHpBits=0, known=0)
        result = self.parse(line(job=2**64 - 1, **unknown) +
                            line(no_endpoint(outcome=1, job=2**64 - 1)))
        self.assertTrue(result['valid'], result['problems'])
        self.assertEqual(result['attempts'][0]['job'], 2**64 - 1)
        self.assertIsNone(result['attempts'][0]['startState']['alive'])
        self.assertIsNone(result['attempts'][0]['startState']['hpBits'])
        # Signed zero and subnormal finite f32 values retain their exact bits.
        for bits in (0, 0x80000000, 1, 0x7f7fffff):
            result = self.parse(line(hpBits=bits) + line(end(hpBits=bits)))
            self.assertTrue(result['valid'], result['problems'])
            self.assertEqual(result['attempts'][0]['endpoint']['hpBits'], bits)

    def test_unpaired_duplicate_identity_and_order_corruption(self):
        for data, code in [(line(end()), 'unpaired_end'), (line() + line(event=2), 'attempt_order'),
                           (line() + line(end(event=3)) + line(end(event=4)), 'duplicate_end'),
                           (line() + line(end(event=1)), 'event_order'),
                           (line() + line(end(round=3)), 'end_before_start')]:
            with self.subTest(code=code):
                result = self.parse(data)
                self.assertFalse(result['valid'])
                self.assertIn(code, self.codes(result))
        for field, value in [('kind', 1), ('origin', 2), ('job', 1), ('group', 4),
                             ('altar', 2), ('startRound', 3)]:
            with self.subTest(identity=field):
                result = self.parse(line() + line(end(**{field: value})))
                self.assertIn('identity_mismatch', self.codes(result))
                self.assertIsNone(result['attempts'][0]['endpoint'])
        result = self.parse(line() + line(end(build='different-build')))
        self.assertIn('build_mismatch', self.codes(result))
        self.assertIsNone(result['attempts'][0]['endpoint'])

    def test_coverage_holes_and_drops_cannot_be_silent_success(self):
        result = self.parse(line(dropped=2) + line(end(event=4, dropped=2)))
        self.assertTrue(result['valid'], result['problems'])
        self.assertEqual(len(result['attempts']), 1)
        self.assertTrue({'dropped_records', 'event_gap'} <= self.codes(result))
        self.assertFalse(result['coverage']['available'])
        result = self.parse(line(id=3, event=3))
        self.assertTrue({'event_gap', 'attempt_gap', 'open_attempt'} <= self.codes(result))
        self.assertEqual(result['attempts'][0]['id'], 3)
        result = self.parse(line(dropped=1) + line(end(dropped=0)))
        self.assertFalse(result['valid'])
        self.assertIn('dropped_order', self.codes(result))

    def test_interleaved_actors_have_independent_sequences(self):
        result = self.parse(line() + line(actor=18) + line(end(actor=18)) + line(end()))
        self.assertTrue(result['valid'], result['problems'])
        self.assertEqual([attempt['actor'] for attempt in result['attempts']], [17, 18])
        self.assertEqual([attempt['outcome'] for attempt in result['attempts']], ['completed'] * 2)

    def test_successor_preserves_missing_predecessor_and_rejects_backward_end(self):
        successor = {**BEGIN, 'id': 2, 'event': 2, 'round': 8, 'startRound': 8}
        result = self.parse(line() + line(successor) + line(end(event=3)))
        self.assertEqual(len(result['attempts']), 2)
        self.assertIn('overlapping_begin', self.codes(result))
        self.assertIn('attempt_order', self.codes(result))
        self.assertTrue(all(a['endpoint'] is None for a in result['attempts']))

    def test_empty_unrelated_crlf_and_embedded_truncated_prefix(self):
        for raw in (b'', b'p4 summary actor=17\n'):
            result = self.parse(raw)
            self.assertTrue(result['valid'])
            self.assertIn('no_attempt_records', self.codes(result))
            self.assertFalse(result['coverage']['available'])
        self.assertTrue(self.parse((line() + line(end())).replace(b'\n', b'\r\n'))['valid'])
        result = self.parse(line() + b'p4 attempt incomplete ' + line(end()))
        self.assertFalse(result['valid'])
        self.assertIn('truncated_row', self.codes(result))
        self.assertEqual(result['matchingRows'], 3)

    def test_bad_api_options_do_not_become_evidence(self):
        for value in (-1, True, 2**31, '5'):
            with self.assertRaises(ValueError):
                self.parse(final_round=value)
        with self.assertRaises(TypeError):
            reader.parse('not bytes')
        with self.assertRaises(ValueError):
            reader.parse(b'', expected_build='')

    def test_optional_companion_preserves_start_and_exact_preaction_fields(self):
        result = self.parse(line() + bin_line() + line(end()))
        self.assertTrue(result['valid'], result['problems'])
        self.assertEqual(result['records'], 2)
        self.assertEqual(result['matchingRows'], 2)
        self.assertEqual(result['companionMatchingRows'], 1)
        self.assertEqual(result['problems'], [])
        attempt = result['attempts'][0]
        self.assertEqual(attempt['start'], BEGIN)
        self.assertEqual(attempt['start']['binKnown'], 0)
        self.assertTrue(attempt['binFieldsComplete'])
        self.assertEqual(attempt['preaction']['known'], 31)
        self.assertTrue(all(attempt['preaction'][field]['known'] for field in reader.EVIDENCE_FIELDS))
        kit = attempt['preaction']['kit']['value']
        self.assertEqual((kit['champion'], kit['ability0'], kit['ability1'], kit['range']), (1, 0, 6, 3))
        self.assertEqual((kit['strengthBits'], kit['strength'], kit['focusBits'], kit['focus']),
                         (STRENGTH, 23.125, FOCUS, 9.5))
        self.assertEqual(kit['items'], [{'kind': 0, 'count': 2}, {'kind': 41, 'count': 1}])
        self.assertEqual(attempt['preaction']['composition']['value'], [])
        for key in ('fitSupported', 'fullGameSealed'):
            self.assertFalse(result[key])
        self.assertFalse(attempt['fitSupported'])
        self.assertNotIn('partition', attempt)
        self.assertNotIn('bin', attempt)
        self.assertIn('economic_tape', result['unavailableFitFields'])

    def test_sdk3_champions_preserve_raw_kit_and_independent_unknowns(self):
        for champion in range(13):
            with self.subTest(champion=champion):
                result = self.parse(line() + bin_line(champion=champion,
                                    strengthBits=0x41b90001, focusBits=0x80000000) + line(end()))
                self.assertTrue(result['valid'], result['problems'])
                attempt = result['attempts'][0]
                self.assertEqual(attempt['start'], BEGIN)
                self.assertEqual(attempt['preaction']['known'], 31)
                kit = attempt['preaction']['kit']['value']
                self.assertEqual((kit['champion'], kit['strengthBits'], kit['focusBits']),
                                 (champion, 0x41b90001, 0x80000000))
                self.assertEqual(attempt['preaction']['companion']['schema'], 1)
                self.assertFalse(result['fitSupported'])
                self.assertFalse(attempt['fitSupported'])
        # A Heart of Ruin kit alone cannot fill any independently unknown category.
        result = self.parse(line() + bin_line(champion=12, known=1,
                            context=-1, travel=-1, nominal=0) + line(end()))
        self.assertTrue(result['valid'], result['problems'])
        attempt = result['attempts'][0]
        self.assertEqual(attempt['preaction']['known'], 1)
        self.assertEqual(attempt['preaction']['kit']['value']['champion'], 12)
        for field in reader.EVIDENCE_FIELDS[1:]:
            self.assertEqual(attempt['preaction'][field], {'known': False, 'value': None})
        self.assertFalse(attempt['binFieldsComplete'])
        self.assertFalse(attempt['fitSupported'])

    def test_absent_and_explicitly_unknown_companion_keep_each_field_unknown(self):
        for companion in (b'', bin_line(unknown_bin()), bin_line(unknown_bin(nominal=0))):
            with self.subTest(companion=companion):
                result = self.parse(line() + companion + line(end()))
                self.assertTrue(result['valid'], result['problems'])
                self.assertEqual(result['problems'], [])
                attempt = result['attempts'][0]
                self.assertFalse(attempt['binFieldsComplete'])
                for field in reader.EVIDENCE_FIELDS:
                    self.assertEqual(attempt['preaction'][field], {'known': False, 'value': None})
                self.assertEqual(attempt['start'], BEGIN)

    def test_partial_bootstrap_body_never_becomes_complete_from_companion(self):
        partial = {**BEGIN, 'hpBits': 0, 'maxHpBits': 0, 'known': 195}
        result = self.parse(line(partial) + bin_line() + line(no_endpoint(outcome=1)))
        self.assertTrue(result['valid'], result['problems'])
        attempt = result['attempts'][0]
        self.assertEqual(attempt['start'], partial)
        self.assertIsNone(attempt['startState']['hp'])
        self.assertTrue(attempt['preaction']['kit']['known'])
        self.assertFalse(attempt['binFieldsComplete'])
        for missing in (1, 2, 4, 8, 16, 32, 64, 128):
            start = {**BEGIN, 'known': 255 & ~missing}
            if missing == 1:
                start.update(x=-1, y=-1)
            elif missing == 2:
                start['bag'] = -1
            elif missing == 4:
                start['hpBits'] = 0
            elif missing == 8:
                start['maxHpBits'] = 0
            elif missing == 16:
                start['known'] &= ~32
            with self.subTest(missing=missing):
                result = self.parse(line(start) + bin_line() + line(no_endpoint(outcome=1)))
                self.assertTrue(result['valid'], result['problems'])
                self.assertFalse(result['attempts'][0]['binFieldsComplete'])
        for hp, maximum in ((0, MAX_HP), (MAX_HP, HP), (HP, 0)):
            result = self.parse(line(hpBits=hp, maxHpBits=maximum) + bin_line() + line(end()))
            self.assertFalse(result['attempts'][0]['binFieldsComplete'])

    def test_partial_evidence_does_not_infer_missing_values(self):
        for missing, field in enumerate(reader.EVIDENCE_FIELDS):
            values = {**BIN, 'known': 31 & ~(1 << missing)}
            if field == 'kit':
                values.update({key: value for key, value in unknown_bin().items()
                               if key in ('champion', 'ability0', 'ability1', 'range', 'strengthBits',
                                          'focusBits', 'itemCount', 'items')})
            elif field == 'composition':
                values.update(compositionCount=0, composition='-')
            else:
                values[field] = -1
            with self.subTest(missing=field):
                result = self.parse(line() + bin_line(values) + line(end()))
                self.assertTrue(result['valid'], result['problems'])
                attempt = result['attempts'][0]
                self.assertEqual(attempt['preaction'][field], {'known': False, 'value': None})
                self.assertFalse(attempt['binFieldsComplete'])

    def test_companion_identity_and_version_cannot_upgrade_start(self):
        changes = [dict(v=1), dict(schema=2), dict(actor=18), dict(round=5), dict(id=2),
                   dict(event=2), dict(kind=1, compositionCount=1, composition='0'),
                   dict(group=4), dict(altar=2), dict(build='another-build')]
        for change in changes:
            with self.subTest(change=change):
                result = self.parse(line() + bin_line(**change) + line(end()))
                self.assertTrue(result['problems'])
                self.assertFalse(result['coverage']['available'])
                attempt = result['attempts'][0]
                self.assertEqual(attempt['start'], BEGIN)
                self.assertEqual(attempt['outcome'], 'completed')
                self.assertFalse(attempt['binFieldsComplete'])
                self.assertEqual(attempt['preaction']['known'], 0)

    def test_duplicate_and_corrupt_companion_revoke_evidence_without_hiding_starts(self):
        invalid = [bin_line(), bin_line(schema=2), bin_line(event=2),
                   bin_line().replace(b' context=0', b''), bin_line(items='0:0,41:1'),
                   bin_line().replace(b' actor=17', b' actor=17 actor=17'),
                   bin_line().replace(b' actor=17', b' actor=18 actor=17'),
                   bin_line().replace(b' id=1', b' id=2 id=1')]
        for companion in invalid:
            for first, second in ((bin_line(), companion), (companion, bin_line())):
                with self.subTest(companion=companion, first=first):
                    result = self.parse(line() + first + second + line(end()))
                    self.assertFalse(result['valid'])
                    self.assertIn('bin_duplicate', self.codes(result))
                    self.assertEqual(result['attempts'][0]['start'], BEGIN)
                    self.assertEqual(result['attempts'][0]['preaction']['known'], 0)
                    self.assertFalse(result['attempts'][0]['binFieldsComplete'])

    def test_companion_must_follow_original_start_before_next_actor_event(self):
        for data in (bin_line() + line() + line(end()), line() + line(end()) + bin_line()):
            with self.subTest(data=data):
                result = self.parse(data)
                self.assertIn('bin_late_or_early', self.codes(result))
                self.assertFalse(result['attempts'][0]['binFieldsComplete'])
                self.assertEqual(result['attempts'][0]['preaction']['known'], 0)
        data = line() + b'p4 unrelated foo=bar\n' + line(actor=18) + bin_line() + line(end(actor=18)) + line(end())
        result = self.parse(data)
        self.assertTrue(result['valid'], result['problems'])
        self.assertTrue(result['attempts'][0]['binFieldsComplete'])
        self.assertFalse(result['attempts'][1]['binFieldsComplete'])

    def test_exact_kit_float_bits_and_bounded_arrays_are_retained(self):
        for bits in (0, 0x80000000, 1, 0x7f7fffff):
            result = self.parse(line() + bin_line(strengthBits=bits, focusBits=bits, itemCount=0, items='-') + line(end()))
            self.assertTrue(result['valid'], result['problems'])
            self.assertEqual(result['attempts'][0]['preaction']['kit']['value']['strengthBits'], bits)
        items = ','.join(f'{kind}:{2**32 - 1}' for kind in range(32))
        composition = ','.join(str(kind // 7) for kind in range(32))
        result = self.parse(line(kind=1) + bin_line(kind=1, itemCount=32, items=items,
                            compositionCount=32, composition=composition) + line(end(kind=1)))
        self.assertTrue(result['valid'], result['problems'])
        self.assertTrue(result['attempts'][0]['binFieldsComplete'])
        self.assertEqual(len(result['attempts'][0]['preaction']['kit']['value']['items']), 32)
        self.assertEqual(result['attempts'][0]['preaction']['composition']['value'],
                         [kind // 7 for kind in range(32)])

    def test_companion_corruption_arrays_ranges_sentinels_and_tokens(self):
        mutations = [dict(known=32), dict(champion=13), dict(champion=2**31 - 1),
                     dict(champion=-1), dict(ability0=8),
                     dict(ability1=-1), dict(range=-1), dict(range=2**31), dict(strengthBits=2**32),
                     dict(strengthBits=0x7f800000), dict(focusBits=0x7fc00001), dict(focusBits=0xbf800000),
                     dict(itemCount=33), dict(itemCount=1), dict(items='0:1,0:2'), dict(items='41:1,0:2'),
                     dict(items='0:0,41:1'), dict(items='0:1,42:1'), dict(items='0:1,41:4294967296'),
                     dict(items='0:1,41:01'), dict(items='0:1,41:-1'), dict(items='0:1,'),
                     dict(compositionCount=33), dict(kind=1), dict(context=2), dict(context=-1),
                     dict(travel=-1), dict(nominal=0), dict(nominal=-1), dict(travel=2**31),
                     dict(known=30), dict(known=27), dict(known=23), dict(known=15)]
        malformed = [bin_line(**change) for change in mutations]
        malformed += [bin_line().replace(b' known=31', b' known=031'),
                      bin_line().replace(b' known=31', b' known=+31'),
                      bin_line().replace(b' nominal=12', b' nominal=1.2'),
                      bin_line().replace(b' known=31', b' known=31 known=31'),
                      bin_line().replace(b' known=31', b' known=31 extra=0'),
                      bin_line().replace(b' known=31', b' known=\xff'),
                      bin_line().replace(b' known=31', b' known=31 '), bin_line().rstrip(b'\n')]
        for companion in malformed:
            with self.subTest(companion=companion):
                result = self.parse(line() + companion + (b'' if not companion.endswith(b'\n') else line(end())))
                self.assertFalse(result['valid'], result['problems'])
                self.assertEqual(result['attempts'][0]['start'], BEGIN)
                self.assertEqual(result['attempts'][0]['preaction']['known'], 0)
                self.assertFalse(result['attempts'][0]['binFieldsComplete'])
        for composition in ('1,0', '0,5', '0,-1', '0,00', '0,', '0:1'):
            result = self.parse(line(kind=1) + bin_line(kind=1, compositionCount=2, composition=composition) + line(end(kind=1)))
            self.assertFalse(result['valid'], result['problems'])

    def test_unpaired_companion_and_dropped_tail_remain_explicit(self):
        result = self.parse(bin_line())
        self.assertIn('bin_unpaired', self.codes(result))
        self.assertEqual(len(result['companionRows']), 1)
        self.assertEqual(result['attempts'], [])
        self.assertFalse(result['coverage']['available'])
        result = self.parse(line(dropped=3) + bin_line() + line(no_endpoint(outcome=4, dropped=3)))
        self.assertTrue(result['valid'], result['problems'])
        self.assertTrue({'dropped_records', 'endpoint_unavailable'} <= self.codes(result))
        self.assertFalse(result['coverage']['available'])
        self.assertEqual(result['attempts'][0]['outcome'], 'observation_gap')
        self.assertIsNone(result['attempts'][0]['elapsedRounds'])


if __name__ == '__main__':
    unittest.main()
