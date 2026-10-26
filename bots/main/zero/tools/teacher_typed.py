"""Read-only BUY/TRIBUTE source typing for restored mechanical probes.

resolve_teacher_source() reads only explicitly supplied files and never launches a
process, writes an artifact, discovers a manifest, or changes qualification. BUY
requires an independent canonical record AND a unique physical inventory inverse.
TRIBUTE uses the raw accepted event, ordered static altar coordinates, personally
known lock facts, and its immediate channel successor. It does not manufacture a
canonical training record. Optional expected hashes pin the caller's saved inputs.

The returned count is the executable BUY scalar; pointer_count remains one.
match_fields uses student_decision.h: kind=1, target=2, count=4, tile=8.
No native slot or lock handle is ever interpreted as an item/altar identity.
"""
from __future__ import annotations

import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import zlib


class SourceValidationError(ValueError):
    """Missing, mismatched, ambiguous, or unsupported saved-source evidence."""


def _require(ok, message):
    if not ok:
        raise SourceValidationError(message)


def _integer(value, name, low=0, high=0xffffffff):
    _require(type(value) is int and low <= value <= high, f'invalid {name}')
    return value


def _point(value, name):
    _require(isinstance(value, list) and len(value) == 2, f'invalid {name}')
    return [_integer(v, name, -32768, 32767) for v in value]


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _digest(value, name):
    _require(isinstance(value, str) and len(value) == 64 and
             all(c in '0123456789abcdef' for c in value), f'invalid {name}')
    return value


def _json_hash(value):
    return _sha(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode())


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        _require(key not in result, f'duplicate JSON field {key}')
        result[key] = value
    return result


def _json(raw):
    return json.loads(raw, object_pairs_hook=_pairs,
                      parse_constant=lambda x: (_ for _ in ()).throw(SourceValidationError(f'invalid JSON {x}')))


def _read(path, expected, name):
    raw = Path(path).read_bytes()
    actual = _sha(raw)
    if expected is not None:
        _require(actual == _digest(expected, name), f'{name} mismatch')
    return raw, actual


def _slice(raw, number):
    """Keep one source point and successor; count every observed owner attempt."""
    metadata = static = actors = row = successor = None
    command_index = None
    counts = {}
    previous = 0
    stream = gzip.GzipFile(fileobj=io.BytesIO(raw)) if raw[:2] == b'\x1f\x8b' else io.BytesIO(raw)
    with stream:
        for line in stream:
            record = _json(line)
            tag = record['type']
            if tag == 'metadata':
                _require(metadata is None and previous == 0, 'duplicate/late source metadata')
                metadata = record
            elif tag == 'static-map':
                _require(static is None, 'duplicate static map')
                static = record
            elif tag == 'actors':
                _require(actors is None, 'duplicate actor roster')
                actors = record
            elif tag == 'input':
                _integer(record['input'], 'prefix input', 1, 0x7fffffff)
                _integer(record['sequence'], 'prefix sequence', 0, 0x7fffffff)
                _require(record['input'] == previous + 1 and record['sequence'] == previous,
                         'source input/sequence gap or duplicate')
                previous += 1
                own = record['before'].get('self')
                if own is not None:
                    moment = record['before']['moment']
                    key = (own['id'], moment['round'], moment['turn'])
                    index = counts.get(key, 0)
                    counts[key] = index + 1
                    if previous == number:
                        command_index = index
                if previous == number:
                    row = record
                elif previous == number + 1:
                    successor = record
                    break
    _require(all(x is not None for x in (metadata, static, actors, row, successor, command_index)),
             'source point, static facts, roster, or immediate successor missing')
    roster = [r for r in actors['roster'] if r['id'] == row['before']['self']['id']]
    _require(len(roster) == 1 and all(roster[0][k] == row['before']['self'][k]
                                    for k in ('id', 'team', 'type')), 'source roster identity mismatch')
    return metadata, static, row, successor, command_index


def _origin(row):
    own, moment, action = row['before']['self'], row['before']['moment'], row['action']
    number = _integer(row['input'], 'input', 1, 0x7fffffff)
    _integer(row['sequence'], 'sequence', 0, 0x7fffffff)
    _require(row['sequence'] == number - 1, 'source sequence/input mismatch')
    actor = _integer(own['id'], 'actor', 0, 0x7fffffff)
    team = _integer(own['team'], 'team', 0, 1)
    round_ = _integer(moment['round'], 'round', 0, 0x7fffffff)
    _integer(moment['turn'], 'owner turn', 0, 0x7fffffff)
    _require(moment['input'] == number - 1 and moment['awaitingInput'] is True and
             moment['phase'] == 10 and moment['actor'] == actor and moment['team'] == team,
             'source pre-input identity/phase mismatch')
    _require(action['input'] == number and action['actor'] == actor and action['team'] == team and
             action['round'] == round_ and action['stepRound'] == round_ and
             action['stepInput'] == number and action['phase'] == 10 and action['stepPhase'] == 10 and
             action['turn'] == moment['turn'] and action['substep'] == 0 and
             action['stepActor'] == {'kind': 1, 'id': actor}, 'source action identity/phase mismatch')
    _integer(action['kind'], 'native kind')
    for key in ('input', 'actor', 'team', 'round', 'stepRound', 'stepInput', 'phase', 'stepPhase', 'turn', 'substep'):
        _integer(action[key], f'action {key}', 0, 0x7fffffff)
    _integer(action['slot'], 'native slot')  # validated but NEVER used for typing
    _integer(action['priority'], 'native priority', -0x80000000, 0x7fffffff)
    _point(action['from'], 'source from')
    _point(action['to'], 'source to')
    return {'sequence': row['sequence'], 'input': number, 'actor': actor, 'team': team,
            'round': round_, 'phase': moment['phase'], 'native_kind': action['kind']}


def _join_successor(row, successor):
    _require(successor['input'] == row['input'] + 1 and successor['sequence'] == row['sequence'] + 1,
             'immediate successor identity mismatch')
    _digest(row['beforeTimelineSha256'], 'before timeline hash')
    _digest(row['afterTimelineSha256'], 'after timeline hash')
    _require(row['afterTimelineSha256'] == successor['beforeTimelineSha256'], 'successor timeline mismatch')
    _require(successor['before']['moment'] == {k: v for k, v in row['action'].items() if k != 'name'},
             'successor moment mismatch')
    own, after, next_own = row['before']['self'], row['afterSelf'], successor['before']['self']
    _require(next_own is not None and all(next_own[k] == own[k] for k in ('id', 'team', 'type')),
             'successor owner mismatch')
    fields = ('id', 'at', 'hp', 'gold', 'gems', 'items', 'consumables')
    _require(isinstance(after, dict) and set(after) == set(fields) and
             all(after[k] == next_own[k] for k in fields), 'successor self projection mismatch')
    return next_own


def _canonical(artifact, row, source_sha):
    _require(artifact['source_sha256'] == source_sha, 'canonical source hash mismatch')
    matches = [r for r in artifact['records'] if r['input'] == row['input']]
    _require(len(matches) == 1, 'canonical source record absent or ambiguous')
    record, origin = matches[0], _origin(row)
    _require(record['status'] == 'resolved' and record['reason'] == 'observed_command',
             'canonical source is unresolved/unsupported')
    _require(record['input'] == row['input'] and record['sequence'] == row['sequence'] and
             record['raw_kind'] == row['action']['kind'] and
             _json_hash(record['decision_origin']) == _json_hash(origin) and
             _json_hash(record['label_origin']) == _json_hash(origin),
             'canonical source origin mismatch')
    a, moment = row['action'], row['before']['moment']
    event = {'slot': a['slot'], 'from': a['from'], 'to': a['to'], 'priority': a['priority'],
             'before_substep': moment['substep'], 'after_substep': a['substep'], 'owner_turn': moment['turn']}
    _require(_json_hash(record['source_event']) == _json_hash(event), 'canonical physical event mismatch')
    return record


def _inventory(own, n_items):
    items, consumables = own['items'], own['consumables']
    _require(isinstance(items, list) and n_items <= len(items) <= 48 and
             isinstance(consumables, list) and len(consumables) == 4, 'incomplete inventory facts')
    for value in items + consumables:
        _integer(value, 'inventory count')
    _require(not any(items[n_items:]), 'inventory outside catalog')
    return items, consumables, _integer(own['gold'], 'gold')


def _buy(row, static, catalog, canonical_record):
    _require(catalog is not None and canonical_record is not None,
             'BUY requires explicit catalog and independent canonical record')
    before, after = row['before']['self'], row['afterSelf']
    items = catalog['result']['items']
    _require(isinstance(items, list) and 0 < len(items) <= 48, 'unsupported item catalog')
    for index, item in enumerate(items):
        _require(type(item['id']) is int and item['id'] == index, 'catalog item order/identity mismatch')
        for key in ('cost', 'combine_count', 'result'):
            _integer(item[key], f'item {key}')
        _integer(item['purchasable'], 'purchasable', 0, 1)
    bi, bc, gold = _inventory(before, len(items))
    ai, ac, after_gold = _inventory(after, len(items))
    _require(len(bi) == len(ai), 'inventory dimensions mismatch')
    _require(before['shopAllowed'] is True and after_gold < gold, 'BUY lacks permission or positive spend')
    consumables = {}
    for entry in static['consumables']:
        raw_id = _integer(entry['rawId'], 'raw consumable id', 0, 3)
        _require(raw_id not in consumables, 'duplicate consumable facts')
        consumables[raw_id] = _integer(entry['cost'], 'consumable cost', 1)
    _require(0 in consumables and 2 in consumables, 'missing typed consumable costs')
    matches = []
    spent = gold - after_gold
    for item in items:
        if not item['purchasable']:
            continue
        cost = item['cost']
        _require(cost > 0, 'unsupported zero-cost purchase rule')
        if spent % cost:
            continue
        quantity = spent // cost
        candidate = bi.copy()
        target = item['id']
        candidate[target] += quantity
        _require(candidate[target] <= 0xffffffff, 'purchase inventory overflow')
        combine = item['combine_count']
        if combine and candidate[target] >= combine:
            result = item['result']
            _require(result < len(items) and result != target and not items[result]['combine_count'],
                     'unsupported chained/self purchase combine')
            combined, candidate[target] = divmod(candidate[target], combine)
            candidate[result] += combined
            _require(candidate[result] <= 0xffffffff, 'purchase result overflow')
        if candidate == ai and bc == ac:
            matches.append((5, target, quantity))
    # Same uniqueness set as source_intent.cpp: raw consumables 0/2 compete with all items.
    for typed, raw_id in enumerate((0, 2)):
        if ac[raw_id] <= bc[raw_id]:
            continue
        quantity = ac[raw_id] - bc[raw_id]
        candidate = bc.copy()
        candidate[raw_id] += quantity
        if candidate == ac and bi == ai and quantity * consumables[raw_id] == spent:
            matches.append((10, typed, quantity))
    _require(len(matches) == 1, 'BUY physical inverse has no unique match')
    type_, target, quantity = matches[0]
    _require(type_ == 5, 'consumable purchase is outside this adapter')
    _integer(quantity, 'executable BUY quantity', 1, 0x7fffffff)
    _require(items[target]['cost'] * quantity <= gold, 'BUY quantity unaffordable')
    intent = {'type': 5, 'argument': {'kind': 1, 'target': target, 'count': 1, 'x': 0, 'y': 0},
              'target_kind': 6, 'target_id': target, 'scalar': quantity, 'scalar_known': True}
    _require(_json_hash(canonical_record['intent']) == _json_hash(intent),
             'canonical BUY typed payload/scalar mismatch')
    return intent, quantity, 3, {'unique_inventory_inverse': True, 'quantity_affordable': True,
                               'source_shop_permission': True}


def _tribute(row, static, next_own):
    before, action = row['before']['self'], row['action']
    width = _integer(static['width'], 'map width', 1, 32767)
    height = _integer(static['height'], 'map height', 1, 32767)
    _require(static['tileEncoding'] == 'y*width+x', 'unsupported tile encoding')
    altars = static['terrain']['altar']
    _require(isinstance(altars, list) and len(altars) == len(set(altars)), 'ambiguous static altar list')
    for tile in altars:
        _integer(tile, 'altar tile', 0, width * height - 1)
    at = _point(action['to'], 'TRIBUTE destination')
    _require(0 <= at[0] < width and 0 <= at[1] < height, 'TRIBUTE tile outside map')
    matches = [i for i, tile in enumerate(altars) if [tile % width, tile // width] == at]
    _require(len(matches) == 1, 'TRIBUTE destination has no unique static altar')
    target = matches[0]
    _integer(target, 'altar ordinal', 0, 0x7fffffff)
    state = _integer(before['stateBits'], 'source state bits')
    gems = _integer(before['gems'], 'source gems', 1)
    _require(type(before['hp']) in (int, float) and math.isfinite(before['hp']) and before['hp'] > 0 and
             bool(state & 1) and not (state & ((1 << 1) | (1 << 2) | (1 << 5) | (1 << 7) | (1 << 12))) and
             before['stunned'] is False and before['channel'] == 'none' and before['recalling'] is False and
             before['tributing'] is False and before['channelRemaining'] is None,
             'TRIBUTE source cannot begin a channel')
    _require(_integer(before['restrictions'], 'source restrictions') == 0,
             'nonzero TRIBUTE restrictions are outside this adapter')
    settings = bytes.fromhex(static['settingsRawHex'])
    _require(len(settings) == 120, 'unsupported queried settings ABI')
    range_ = _integer(struct.unpack_from('<i', settings, 32)[0], 'queried submit range', 0, 32767)
    own_at = _point(before['at'], 'self tile')
    _require(max(abs(own_at[i] - at[i]) for i in (0, 1)) <= range_, 'TRIBUTE outside queried range')
    destination = at[1] * width + at[0]
    vision = row['before']['vision']['tiles']
    _require(isinstance(vision, list) and destination in vision, 'TRIBUTE destination lock facts unknown')
    for tile in vision:
        _integer(tile, 'vision tile', 0, width * height - 1)
    _require(len(vision) == len(set(vision)), 'duplicate vision tile')
    locks = row['before']['altarLocks']
    _require(isinstance(locks, list), 'TRIBUTE lock observations missing')
    lock_identities = set()
    for lock in locks:
        lock_at = _point(lock.get('at', [lock.get('x'), lock.get('y')]), 'lock tile')
        lock_tile = lock_at[1] * width + lock_at[0]
        team = _integer(lock['team'], 'lock team', 0, 1)
        identity = (tuple(lock_at), team)
        _require(lock_tile in altars and identity not in lock_identities,
                 'lock lacks unique static altar')
        lock_identities.add(identity)
        _integer(lock['handle'], 'native lock handle')  # NEVER used as an altar ordinal
        _integer(lock['hitsRemaining'], 'lock hits')
        _require(lock['kind'] == 6, 'unsupported lock observation kind')
        _require(lock_at != at or team == before['team'], 'TRIBUTE blocked by enemy lock')
    for banked in (row['before']['banked'], row['_successor_banked']):
        _require(isinstance(banked, list) and len(banked) == 2, 'incomplete banked counts')
        for value in banked:
            _integer(value, 'banked count')
    next_state = _integer(next_own['stateBits'], 'successor state bits')
    _require(next_own['channel'] == 'tribute' and next_own['tributing'] is True and
             next_own['recalling'] is False and
             _integer(next_own['channelRemaining'], 'successor tribute timer', 1, 0x7fffffff) > 0 and
             (next_state & (1 << 2)) != 0 and _integer(next_own['gems'], 'successor gems') == gems and
             next_own['allowances'] == {'move': False, 'attack': False, 'cast': False} and
             next_own['at'] == own_at and row['before']['banked'] == row['_successor_banked'],
             'TRIBUTE immediate channel successor mismatch')
    intent = {'type': 7, 'argument': {'kind': 7, 'target': target, 'count': 1, 'x': at[0], 'y': at[1]},
              'target_kind': 5, 'target_id': target, 'scalar': 0, 'scalar_known': False}
    return intent, 1, 11, {'unique_static_altar': True, 'personally_known_target_locks': True,
                          'source_channel_preconditions': True, 'queried_range': range_,
                          'immediate_channel_start': True, 'observed_channel_remaining': next_own['channelRemaining']}


def _resolve(metadata, static, row, successor, command_index, source_sha, canonical, catalog,
             canonical_sha, catalog_sha):
    _require(metadata['schema'] == 'zero-exact-inputs/v1.6', 'unsupported exact source schema')
    for field in ('engineSha256', 'replaySha256', 'payloadSha256'):
        _digest(metadata[field], field)
    origin = _origin(row)
    _require((row['action']['kind'], row['action']['name']) in ((13, 'shop'), (12, 'tribute')),
             'source family unsupported; only physical BUY/TRIBUTE accepted')
    next_own = _join_successor(row, successor)
    record = _canonical(canonical, row, source_sha) if canonical is not None else None
    if catalog is not None:
        _require(catalog['status'] == 'passed' and catalog['engine']['sha256'] == metadata['engineSha256'],
                 'catalog engine identity/status mismatch')
        _require(any(entry['sha256'] == catalog_sha for entry in metadata['sources']),
                 'catalog bytes are not a declared exact-source dependency')
    if origin['native_kind'] == 13:
        intent, count, mask, facts = _buy(row, static, catalog, record)
        basis = 'independent_canonical_and_unique_physical_inventory_inverse'
    else:
        # Do not mutate source records while passing the joined banked fact to the pure family validator.
        intent, count, mask, facts = _tribute({**row, '_successor_banked': successor['before']['banked']}, static, next_own)
        if record is not None:
            _require(_json_hash(record['intent']) == _json_hash(intent), 'canonical TRIBUTE typed payload mismatch')
        basis = 'independent_physical_event_static_altar_and_immediate_channel_successor'
    argument = intent['argument']
    return {'schema': 'zero-teacher-typed/v1', 'input': row['input'], 'sequence': row['sequence'],
            'origin': origin, 'command_index': _integer(command_index, 'command index', 0, 0x7fffffff),
            'type': intent['type'], 'kind': argument['kind'], 'target': argument['target'],
            'count': count, 'pointer_count': argument['count'], 'x': argument['x'], 'y': argument['y'],
            'tile': [argument['x'], argument['y']], 'match_fields': mask,
            'target_kind': intent['target_kind'], 'target_id': intent['target_id'],
            'scalar_known': intent['scalar_known'], 'scalar': intent['scalar'],
            'source_sha256': source_sha, 'canonical_sha256': canonical_sha, 'catalog_sha256': catalog_sha,
            'prestate_sha256': _json_hash(row['before']), 'static_map_sha256': _json_hash(static),
            'before_timeline_sha256': row['beforeTimelineSha256'], 'after_timeline_sha256': row['afterTimelineSha256'],
            'engine_sha256': metadata['engineSha256'], 'replay_sha256': metadata['replaySha256'],
            'payload_sha256': metadata['payloadSha256'], 'typing_basis': basis, 'validated_facts': facts,
            'scope': {'mechanical_only': True, 'training_eligible': False, 'deployment_qualified': False,
                      'sdk_dispatch_proved': False, 'ordinary_caller_fuel_proved': False,
                      'banking_completion_proved': False, 'private_expert_history_inferred': False,
                      'manifest_bindings_validated': False,
                      'history': 'observed external attempts only; restored SDK/NoGoal history unproved'}}


def resolve_teacher_source(exact_path, source_input, canonical_path=None, catalog_path=None, *,
                           expected_source_sha256=None, expected_canonical_sha256=None,
                           expected_catalog_sha256=None):
    """Return a validated TeacherSource dict; raise SourceValidationError on rejection.

    Required paths are explicit; no default catalog/canonical path is discovered.
    Expected hashes are caller pins, not manifest repairs. Returned hashes always
    identify the bytes read. BUY requires both optional paths; TRIBUTE can stand
    alone. An explicitly supplied canonical artifact must contain this exact row.
    """
    try:
        number = _integer(source_input, 'source input', 1, 0x7ffffffe)
        raw, source_sha = _read(exact_path, expected_source_sha256, 'exact source SHA256')
        metadata, static, row, successor, command_index = _slice(raw, number)
        canonical = catalog = canonical_sha = catalog_sha = None
        _require(canonical_path is not None or expected_canonical_sha256 is None, 'canonical hash without path')
        _require(catalog_path is not None or expected_catalog_sha256 is None, 'catalog hash without path')
        if canonical_path is not None:
            raw, canonical_sha = _read(canonical_path, expected_canonical_sha256, 'canonical SHA256')
            canonical = _json(raw)
        if catalog_path is not None:
            raw, catalog_sha = _read(catalog_path, expected_catalog_sha256, 'catalog SHA256')
            catalog = _json(raw)
        return _resolve(metadata, static, row, successor, command_index, source_sha, canonical, catalog,
                        canonical_sha, catalog_sha)
    except SourceValidationError:
        raise
    except (KeyError, TypeError, IndexError, ValueError, OverflowError, OSError, EOFError, zlib.error) as error:
        raise SourceValidationError(f'missing or malformed source evidence: {error}') from error
