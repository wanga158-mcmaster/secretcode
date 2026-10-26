"""Read actor-local ``p4 attempt`` evidence without importing the engine.

Schema 1 records starts and their observed dispositions.  Optional attempt-bin
companions preserve pre-action fields without changing the original start or
establishing table/fit support. A raw log (even with a supplied final round)
does not independently establish sealed full-game coverage.
"""

import math
import re
import struct


FIELDS = tuple('v build schema actor round event id edge kind origin job group altar '
               'dropped hasEnd outcome startRound x y bag hpBits maxHpBits known binKnown'.split())
PREFIX = b'p4 attempt '
UINT64_MAX = 2**64 - 1
INT_MAX = 2**31 - 1
OUTCOMES = ('completed', 'aborted', 'replaced', 'death', 'observation_gap', 'unknown')
KINDS = ('bank', 'one_camp')
ORIGINS = ('planned', 'bootstrap', 'legacy')
IDENTITY = ('build', 'schema', 'actor', 'id', 'kind', 'origin', 'job', 'group', 'altar', 'startRound')
UNAVAILABLE_FIT_FIELDS = ('kit', 'composition', 'context', 'travel', 'nominal', 'economic_tape')
BIN_PREFIX = b'p4 attempt-bin '
BIN_FIELDS = tuple('v build schema actor round id event kind group altar known '
                   'champion ability0 ability1 range strengthBits focusBits '
                   'itemCount items compositionCount composition context travel nominal'.split())
EVIDENCE_FIELDS = ('kit', 'composition', 'context', 'travel', 'nominal')


def _integer(value):
    if len(value) > 21 or re.fullmatch(r'-?(?:0|[1-9][0-9]*)', value) is None:
        raise ValueError
    return int(value)


def _float(bits):
    return struct.unpack('!f', struct.pack('!I', bits))[0]


def _bin_row(payload):
    """Validate an exact companion; never normalize or invent unknown facts.

    Mask bits are kit=1, composition=2, context=4, travel=8, nominal=16.
    Unknown kit is champion/ability0/ability1/range=-1, zero stat bits, and
    itemCount=0 items=-. Unknown composition is compositionCount=0 composition=-;
    unknown context/travel use -1, and unknown nominal accepts 0 or -1. A known
    empty inventory is valid. Known bank composition is empty; known one_camp
    composition is nonempty. Both arrays are sorted, with unique item kinds.
    Numeric kinds follow the pinned SDK3 (champion 0..12, ability 0..7 including
    unused slot 0, item 0..41, camp 0..4); both array capacities are 32.
    """
    try:
        tokens = payload.decode('ascii').split(' ')
    except UnicodeDecodeError:
        return None, 'bin_non_ascii', 'Attempt-bin row is not ASCII.'
    row = {}
    for token in tokens:
        if token.count('=') != 1:
            return None, 'bin_malformed_field', 'Attempt-bin requires single space-separated key=value fields.'
        key, value = token.split('=')
        if key in row:
            return None, 'bin_duplicate_field', 'Attempt-bin field appears more than once: ' + key
        if key not in BIN_FIELDS or not value:
            return None, 'bin_unknown_or_empty_field', 'Attempt-bin field is unknown or empty: ' + key
        if key in ('build', 'items', 'composition'):
            if re.fullmatch(r'[!-~]+', value) is None:
                return None, 'bin_malformed_field', 'Attempt-bin token is not printable ASCII: ' + key
            row[key] = value
        else:
            try:
                row[key] = _integer(value)
            except ValueError:
                return None, 'bin_non_integer', 'Attempt-bin integer is malformed or out of range: ' + key
    if set(row) != set(BIN_FIELDS):
        return None, 'bin_missing_field', 'Attempt-bin does not have exactly the schema fields.'
    if row['v'] != 2 or row['schema'] != 1:
        return None, 'bin_schema', 'Attempt-bin version/schema is unsupported.'
    for key in ('actor', 'round'):
        if not 0 <= row[key] <= INT_MAX:
            return None, 'bin_integer_range', 'Attempt-bin integer is out of range: ' + key
    for key in ('id', 'event'):
        if not 1 <= row[key] <= UINT64_MAX:
            return None, 'bin_integer_range', 'Attempt-bin integer is out of range: ' + key
    for key in ('group', 'altar'):
        if not -1 <= row[key] <= INT_MAX:
            return None, 'bin_integer_range', 'Attempt-bin integer is out of range: ' + key
    for key in ('strengthBits', 'focusBits'):
        if not 0 <= row[key] <= 2**32 - 1:
            return None, 'bin_integer_range', 'Attempt-bin float bits are out of range: ' + key
    for key, lower, upper in (('kind', 0, 1), ('known', 0, 31), ('champion', -1, 12),
                              ('ability0', -1, 7), ('ability1', -1, 7), ('range', -1, INT_MAX),
                              ('itemCount', 0, 32), ('compositionCount', 0, 32),
                              ('context', -1, 1), ('travel', -1, INT_MAX), ('nominal', -1, INT_MAX)):
        if not lower <= row[key] <= upper:
            return None, 'bin_value_range', 'Attempt-bin value is out of range: ' + key
    items, composition = [], []
    try:
        if row['items'] != '-':
            for token in row['items'].split(','):
                if token.count(':') != 1:
                    raise ValueError
                kind, count = map(_integer, token.split(':'))
                if not 0 <= kind < 42 or not 1 <= count <= 2**32 - 1:
                    raise ValueError
                if items and items[-1]['kind'] >= kind:
                    raise ValueError
                items.append({'kind': kind, 'count': count})
                if len(items) > 32:
                    raise ValueError
        if row['composition'] != '-':
            for token in row['composition'].split(','):
                kind = _integer(token)
                if not 0 <= kind <= 4 or (composition and composition[-1] > kind):
                    raise ValueError
                composition.append(kind)
                if len(composition) > 32:
                    raise ValueError
    except ValueError:
        return None, 'bin_array', 'Attempt-bin arrays require bounded exact sorted kinds and positive unique item counts.'
    if len(items) != row['itemCount'] or len(composition) != row['compositionCount']:
        return None, 'bin_array_count', 'Attempt-bin array length differs from its declared count.'
    row['items'], row['composition'] = items, composition
    if row['known'] & 1:
        if min(row[key] for key in ('champion', 'ability0', 'ability1', 'range')) < 0:
            return None, 'bin_kit', 'Known kit requires champion, both ability slots and attack range.'
        if any(not math.isfinite(_float(row[key])) or _float(row[key]) < 0
               for key in ('strengthBits', 'focusBits')):
            return None, 'bin_kit', 'Known kit requires finite nonnegative exact stats.'
    elif any(row[key] != -1 for key in ('champion', 'ability0', 'ability1', 'range')) or items or any(
            row[key] != 0 for key in ('strengthBits', 'focusBits')):
        return None, 'bin_unknown_value', 'Unknown kit must use its sentinels and empty inventory.'
    if row['known'] & 2:
        if (row['kind'] == 0 and composition) or (row['kind'] == 1 and not composition):
            return None, 'bin_composition', 'Known composition must be empty for bank and nonempty for one_camp.'
    elif composition:
        return None, 'bin_unknown_value', 'Unknown composition must be empty.'
    for bit, key in ((4, 'context'), (8, 'travel'), (16, 'nominal')):
        if row['known'] & bit:
            if row[key] < (1 if key == 'nominal' else 0):
                return None, 'bin_known_sentinel', 'Known attempt-bin value uses an unknown/invalid sentinel: ' + key
        elif row[key] not in ((-1, 0) if key == 'nominal' else (-1,)):
            return None, 'bin_unknown_value', 'Unknown attempt-bin value must use its sentinel: ' + key
    return row, None, None


def _empty_preaction():
    return {'known': 0, 'companion': None,
            **{key: {'known': False, 'value': None} for key in EVIDENCE_FIELDS}}


def _bin_identity_hints(payload, starts):
    """Identify duplicate corrupt companions without accepting their contents."""
    hints = {}
    for key in ('actor', 'id'):
        values = re.findall(rb'(?:^| )' + key.encode() + rb'=([^ ]+)(?= |$)', payload)
        if not values:
            return set()
        try:
            hints[key] = {_integer(value.decode('ascii')) for value in values}
        except (ValueError, UnicodeDecodeError):
            return set()
    if len(hints['actor']) == len(hints['id']) == 1:
        return {(next(iter(hints['actor'])), next(iter(hints['id'])))}
    # Conflicting duplicate identity fields cannot leave an earlier candidate
    # looking unique. Bound ambiguity to retained starts rather than expanding
    # an untrusted Cartesian product of tokens.
    return {key for key in starts if key[0] in hints['actor'] and key[1] in hints['id']}


def _preactions(raw, attempts, rows, problems, build):
    starts = {(a['actor'], a['id']): a for a in attempts}
    begin_lines = {(r['actor'], r['id']): r['line'] for r in rows if r['edge'] == 0}
    for attempt in attempts:
        attempt.update(preaction=_empty_preaction(), binFieldsComplete=False)
    matches = list(re.finditer(re.escape(BIN_PREFIX), raw))
    companions, candidates = [], {}
    for match in matches:
        line = raw.count(b'\n', 0, match.start()) + 1
        ending = raw.find(b'\n', match.end())
        next_record = re.search(rb'p4 attempt(?:-bin)? ', raw[match.end():])
        truncated = ending == -1 or (next_record and match.end() + next_record.start() < ending)
        payload = raw[match.end():ending if ending != -1 else len(raw)].removesuffix(b'\r')
        hints = _bin_identity_hints(payload, starts)
        for hint in hints:
            candidates.setdefault(hint, []).append(None)
        if truncated:
            _problem(problems, 'bin_truncated_row', 'invalid', line, 'Attempt-bin has no complete newline-terminated row.')
            continue
        row, code, message = _bin_row(payload)
        if row is None:
            _problem(problems, code, 'invalid', line, message)
            continue
        record = {'line': line, **row}
        companions.append(record)
        candidates[(row['actor'], row['id'])][-1] = record
        attempt = starts.get((row['actor'], row['id']))
        if attempt is None:
            _problem(problems, 'bin_unpaired', 'unavailable', line, 'Attempt-bin has no retained matching start.', row)
            continue
        start = attempt['start']
        if row['build'] != build or any(row[key] != start[key] for key in
                ('v', 'build', 'schema', 'actor', 'round', 'id', 'event', 'kind', 'group', 'altar')):
            _problem(problems, 'bin_identity_mismatch', 'invalid', line, 'Attempt-bin identity differs from its original start.', row)
            continue
        previous = [r for r in rows if r['actor'] == row['actor'] and r['line'] < line]
        if not previous or previous[-1]['edge'] != 0 or previous[-1]['id'] != row['id'] or line <= begin_lines[(row['actor'], row['id'])]:
            _problem(problems, 'bin_late_or_early', 'invalid', line, 'Attempt-bin must follow its start before the next actor event.', row)
            continue
        record['paired'] = True
    for key, records in candidates.items():
        attempt = starts.get(key)
        if len(records) > 1:
            for record in records:
                if record is not None:
                    _problem(problems, 'bin_duplicate', 'invalid', record['line'],
                             'Multiple attempt-bin candidates cannot establish pre-action evidence.', record)
            continue
        row = records[0]
        if attempt is None or row is None or not row.get('paired'):
            continue
        known = row['known']
        kit = {key: row[key] for key in ('champion', 'ability0', 'ability1', 'range', 'strengthBits', 'focusBits', 'itemCount', 'items')}
        kit.update(strength=_float(row['strengthBits']), focus=_float(row['focusBits']))
        values = (kit, list(row['composition']), row['context'], row['travel'], row['nominal'])
        attempt['preaction'] = {'known': known, 'companion': dict(row), **{
            key: {'known': bool(known & (1 << index)), 'value': values[index] if known & (1 << index) else None}
            for index, key in enumerate(EVIDENCE_FIELDS)}}
        state = attempt['startState']
        attempt['binFieldsComplete'] = bool(known == 31 and state['known'] == 255 and
            0 < state['hp'] <= state['maxHp'])
    return companions, len(matches)


def _problem(problems, code, severity, line, message, row=None):
    problem = {'code': code, 'severity': severity, 'line': line, 'message': message}
    if row is not None:
        problem.update({key: row[key] for key in ('actor', 'event', 'id') if key in row})
    problems.append(problem)


def _state(row):
    known = row['known']
    return {
        'round': row['round'],
        'position': {'x': row['x'], 'y': row['y']} if known & 1 else None,
        'bag': row['bag'] if known & 2 else None,
        'hp': struct.unpack('!f', struct.pack('!I', row['hpBits']))[0] if known & 4 else None,
        'maxHp': struct.unpack('!f', struct.pack('!I', row['maxHpBits']))[0] if known & 8 else None,
        'hpBits': row['hpBits'] if known & 4 else None,
        'maxHpBits': row['maxHpBits'] if known & 8 else None,
        'alive': bool(known & 32) if known & 16 else None,
        'own': bool(known & 64), 'fresh': bool(known & 128), 'known': known,
    }


def _row(payload):
    """Return one exact schema row, or a precise rejection reason."""
    try:
        tokens = payload.decode('ascii').split(' ')
    except UnicodeDecodeError:
        return None, 'non_ascii', 'Attempt row is not ASCII.'
    row = {}
    for token in tokens:
        if token.count('=') != 1:
            return None, 'malformed_field', 'Attempt fields must be single space-separated key=value tokens.'
        key, value = token.split('=')
        if key in row:
            return None, 'duplicate_field', 'Attempt field appears more than once: ' + key
        if key not in FIELDS or not value:
            return None, 'unknown_or_empty_field', 'Attempt field is unknown or empty: ' + key
        if key == 'build':
            if re.fullmatch(r'[!-~]+', value) is None:
                return None, 'malformed_build', 'Attempt build must be a printable ASCII token.'
            row[key] = value
        else:
            if re.fullmatch(r'-?(?:0|[1-9][0-9]*)', value) is None:
                return None, 'non_integer', 'Attempt integer is malformed: ' + key
            # Bound conversion work, including on untrusted enormous integer tokens.
            if len(value) > 21:
                return None, 'integer_range', 'Attempt integer is out of range: ' + key
            row[key] = int(value)
    if set(row) != set(FIELDS):
        return None, 'missing_field', 'Attempt row does not have exactly the schema fields.'
    if row['v'] != 2 or row['schema'] != 1:
        return None, 'schema', 'Attempt version/schema is unsupported.'
    for key in ('actor', 'round', 'startRound'):
        if not 0 <= row[key] <= INT_MAX:
            return None, 'integer_range', 'Attempt integer is out of range: ' + key
    for key in ('event', 'id', 'job', 'dropped'):
        if not (1 if key in ('event', 'id') else 0) <= row[key] <= UINT64_MAX:
            return None, 'integer_range', 'Attempt integer is out of range: ' + key
    for key in ('group', 'altar', 'x', 'y', 'bag'):
        if not -1 <= row[key] <= INT_MAX:
            return None, 'integer_range', 'Attempt integer is out of range: ' + key
    for key in ('hpBits', 'maxHpBits'):
        if not 0 <= row[key] <= 2**32 - 1:
            return None, 'integer_range', 'Attempt float bits are out of range: ' + key
    for key, bound in (('edge', 1), ('kind', 1), ('origin', 2), ('hasEnd', 1), ('outcome', 5), ('known', 255)):
        if not 0 <= row[key] <= bound:
            return None, 'enum_range', 'Attempt enum/mask is out of range: ' + key
    if row['binKnown'] != 0:
        return None, 'unsupported_bin', 'Schema 1 cannot claim a known fit bin.'
    known = row['known']
    if known & 32 and not known & 16:
        return None, 'alive_without_known', 'Alive value requires aliveKnown.'
    for bit, keys in ((1, ('x', 'y')), (2, ('bag',))):
        if known & bit:
            if any(row[key] < 0 for key in keys):
                return None, 'known_sentinel', 'Known position/inventory uses an unknown sentinel.'
        elif any(row[key] != -1 for key in keys):
            return None, 'unknown_value', 'Unknown position/inventory must use the sentinel.'
    for bit, key in ((4, 'hpBits'), (8, 'maxHpBits')):
        if known & bit:
            value = struct.unpack('!f', struct.pack('!I', row[key]))[0]
            if not math.isfinite(value):
                return None, 'nonfinite_hp', 'Known HP value is nonfinite: ' + key
        elif row[key] != 0:
            return None, 'unknown_value', 'Unknown HP must use zero bits: ' + key
    if row['edge'] == 0:
        if row['hasEnd'] or row['outcome'] != 5 or row['round'] != row['startRound']:
            return None, 'begin_shape', 'Begin requires its start round, unknown outcome, and hasEnd=0.'
    else:
        if row['round'] < row['startRound']:
            return None, 'end_before_start', 'End detection precedes the start round.'
        if not row['hasEnd'] and known:
            return None, 'missing_endpoint_state', 'hasEnd=0 cannot supply an endpoint state.'
        if row['outcome'] in (4, 5) and row['hasEnd']:
            return None, 'censor_endpoint', 'Gap/unknown disposition cannot claim an endpoint.'
        if row['outcome'] == 0 and (not row['hasEnd'] or not known & 16 or not known & 32):
            return None, 'completion_endpoint', 'Completion requires an observed living endpoint.'
        if row['outcome'] == 3 and (not row['hasEnd'] or not known & 16 or known & 32):
            return None, 'death_endpoint', 'Death requires an observed dead endpoint.'
    return row, None, None


def parse(raw: bytes, expected_build=None, final_round=None):
    """Return retained starts, strict problems and explicitly limited coverage.

    Corrupt matching rows are reported as ``invalid``; dropped records, forward
    sequence holes and open starts are ``unavailable``.  Neither is silently
    accepted as complete coverage.  Unrelated engine bytes/rows are ignored.
    ``final_round`` right-censors open starts without inventing terminal rows.
    """
    if not isinstance(raw, bytes):
        raise TypeError('raw must be bytes')
    if expected_build is not None and (not isinstance(expected_build, str) or not expected_build):
        raise ValueError('expected_build must be a nonempty string or None')
    if final_round is not None and (type(final_round) is not int or not 0 <= final_round <= INT_MAX):
        raise ValueError('final_round must be a nonnegative int32 or None')
    problems, rows, attempts = [], [], []
    actors, starts = {}, {}
    build = expected_build
    matches = list(re.finditer(re.escape(PREFIX), raw))
    if not matches:
        _problem(problems, 'no_attempt_records', 'unavailable', 1, 'No attempt records were observed.')
    for match in matches:
        line = raw.count(b'\n', 0, match.start()) + 1
        ending = raw.find(b'\n', match.end())
        next_match = raw.find(PREFIX, match.end())
        if ending == -1 or (next_match != -1 and next_match < ending):
            _problem(problems, 'truncated_row', 'invalid', line, 'Attempt prefix has no complete newline-terminated row.')
            continue
        payload = raw[match.end():ending]
        if payload.endswith(b'\r'):
            payload = payload[:-1]
        row, code, message = _row(payload)
        if row is None:
            _problem(problems, code, 'invalid', line, message)
            continue
        if build is None:
            build = row['build']
        if row['build'] != build:
            _problem(problems, 'build_mismatch', 'invalid', line, 'Attempt build differs from the expected stream build.', row)
            continue
        actor = actors.setdefault(row['actor'], {'event': 0, 'round': -1, 'id': 0, 'dropped': 0})
        if row['event'] <= actor['event']:
            _problem(problems, 'event_order', 'invalid', line, 'Actor event IDs must strictly increase.', row)
            continue
        if row['round'] < actor['round']:
            _problem(problems, 'round_order', 'invalid', line, 'Actor event rounds must not decrease.', row)
            continue
        if row['dropped'] < actor['dropped']:
            _problem(problems, 'dropped_order', 'invalid', line, 'Cumulative dropped count decreased.', row)
            continue
        if row['event'] != actor['event'] + 1:
            _problem(problems, 'event_gap', 'unavailable', line, 'Actor event sequence has missing records.', row)
        if row['dropped'] > actor['dropped']:
            _problem(problems, 'dropped_records', 'unavailable', line, 'Recorder reports dropped attempt records.', row)
        actor.update(event=row['event'], round=row['round'], dropped=row['dropped'])
        key = (row['actor'], row['id'])
        if row['edge'] == 0:
            if key in starts or row['id'] <= actor['id']:
                _problem(problems, 'attempt_order', 'invalid', line, 'Begin attempt IDs must strictly increase without duplicates.', row)
                continue
            if row['id'] != actor['id'] + 1:
                _problem(problems, 'attempt_gap', 'unavailable', line, 'Actor attempt sequence has missing starts.', row)
            if any(a['actor'] == row['actor'] and a['end'] is None for a in attempts):
                _problem(problems, 'overlapping_begin', 'unavailable', line,
                         'A later start was observed before the earlier attempt had an observed end.', row)
            actor['id'] = row['id']
            attempt = {
                'actor': row['actor'], 'id': row['id'], 'kind': KINDS[row['kind']],
                'origin': ORIGINS[row['origin']], 'job': row['job'], 'group': row['group'],
                'altar': row['altar'], 'startRound': row['startRound'], 'start': dict(row),
                'startState': _state(row), 'end': None, 'endpoint': None,
                'outcome': None, 'outcomeCode': None, 'elapsedRounds': None,
                'rightCensored': False, 'censor': None, 'fitSupported': False,
            }
            starts[key] = attempt
            attempts.append(attempt)
        else:
            if row['id'] < actor['id']:
                _problem(problems, 'attempt_order', 'invalid', line, 'End attempt ID precedes a later observed start.', row)
                continue
            attempt = starts.get(key)
            if attempt is None:
                _problem(problems, 'unpaired_end', 'invalid', line, 'End has no observed matching begin.', row)
                continue
            if attempt['end'] is not None:
                _problem(problems, 'duplicate_end', 'invalid', line, 'Attempt has more than one end.', row)
                continue
            if any(row[key] != attempt['start'][key] for key in IDENTITY):
                _problem(problems, 'identity_mismatch', 'invalid', line, 'End identity/startRound differs from its begin.', row)
                continue
            attempt.update(end=dict(row), outcome=OUTCOMES[row['outcome']], outcomeCode=row['outcome'])
            if row['hasEnd']:
                attempt['endpoint'] = _state(row)
                attempt['elapsedRounds'] = row['round'] - row['startRound']
            else:
                attempt['rightCensored'] = True
                attempt['censor'] = {'reason': OUTCOMES[row['outcome']], 'round': row['round']}
                _problem(problems, 'endpoint_unavailable', 'unavailable', line,
                         'Disposition has no observed body endpoint; detection time is not a duration.', row)
        rows.append({'line': line, **row})
    final_valid = final_round is not None and all(row['round'] <= final_round for row in rows)
    if final_round is not None and not final_valid:
        _problem(problems, 'final_round_before_observation', 'invalid', raw.count(b'\n') + 1,
                 'Supplied final round precedes an observed event; it cannot censor open starts.')
    for attempt in attempts:
        if attempt['end'] is None:
            if final_valid:
                attempt['rightCensored'] = True
                attempt['censor'] = {'reason': 'final_round', 'round': final_round}
            _problem(problems, 'open_attempt', 'unavailable', raw.count(b'\n') + 1,
                     'Start has no observed end; no endpoint is inferred.', attempt['start'])
    companions, companion_matches = _preactions(raw, attempts, rows, problems, build)
    first_problem_line = min((p['line'] for p in problems), default=raw.count(b'\n') + 2)
    return {
        'schemaVersion': 'kernel-attempt-log/1', 'build': build, 'records': len(rows),
        'matchingRows': len(matches), 'rows': rows, 'attempts': attempts, 'problems': problems,
        'companionMatchingRows': companion_matches, 'companionRows': companions,
        'valid': not any(p['severity'] == 'invalid' for p in problems),
        'fitSupported': False, 'unavailableFitFields': list(UNAVAILABLE_FIT_FIELDS),
        'coverage': {
            'available': not problems, 'fullGameSealed': False,
            'source': 'observed_raw_prefix', 'finalRound': final_round,
            'observedValidPrefixRecords': sum(row['line'] < first_problem_line for row in rows),
            'observedRecords': len(rows), 'independentSeal': None,
            'endpointComplete': bool(attempts) and all(a['endpoint'] is not None for a in attempts),
            'meaning': 'Only observed rows are checked; a raw prefix and final-round censor do not prove full-game coverage.',
        },
        'fullGameSealed': False,
    }


def main(argv=None):
    """Summarize the attempt rows of one bot stdout log: python kernel_attempt_log.py LOG [--build B] [--final-round N]."""
    import argparse
    from collections import Counter
    cli = argparse.ArgumentParser(description=main.__doc__)
    cli.add_argument('log')
    cli.add_argument('--build', help='expected PLAN4_REFERENCE_VERSION build string (default: first row)')
    cli.add_argument('--final-round', type=int, help='right-censor open starts at this observed final round')
    args = cli.parse_args(argv)
    with open(args.log, 'rb') as stream:
        result = parse(stream.read(), expected_build=args.build, final_round=args.final_round)
    outcomes = Counter((a['actor'], a['kind'], a['outcome'] or 'open') for a in result['attempts'])
    print(f"build={result['build']} records={result['records']} attempts={len(result['attempts'])} "
          f"companions={len(result['companionRows'])} valid={result['valid']} problems={len(result['problems'])}")
    for (actor, kind, outcome), count in sorted(outcomes.items()):
        print(f'  actor {actor} {kind} {outcome}: {count}')
    for problem in result['problems'][:10]:
        print(f"  {problem['severity']} {problem['code']} line {problem['line']}: {problem['message']}")
    return 0 if result['valid'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
