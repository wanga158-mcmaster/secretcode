"""Transport exact-export JSONL to the native shared-core record builder's token stream.

  arena's Python -B bots/main/zero/tools/exact_records.py --input inputs.jsonl.gz --tokens inputs.tokens

This module transports numbers, coordinates, lists and source actions only. C++ builds every feature,
candidate and label. Unknown native input kinds are retained; unknown JSONL record types are errors.
"""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import uuid


TOKEN_SCHEMA = 2  # WARDRESOURCES carries an explicit priority-known flag, without reserving any int32 value
EXPORT_SCHEMAS = {f'zero-exact-inputs/v{version}' for version in ('1', '1.1', '1.2', '1.3', '1.4', '1.5', '1.6')}
U32 = (1 << 32) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1
F32_MAX = 3.4028234663852886e38


def _integer(value, name, lo=I32_MIN, hi=I32_MAX):
    if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
        raise ValueError(f'{name}: expected integer in [{lo}, {hi}], got {value!r}')
    return value


def _uint(value, name):
    return _integer(value, name, 0, U32)


def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or abs(value) > F32_MAX:
        raise ValueError(f'{name}: expected finite float32-range number, got {value!r}')
    return value


def _boolean(value, name):
    if not isinstance(value, bool):
        raise ValueError(f'{name}: expected boolean, got {value!r}')
    return int(value)


def _list(value, name, minimum=0, maximum=1000000):
    if not isinstance(value, list) or not minimum <= len(value) <= maximum:
        raise ValueError(f'{name}: expected list with {minimum}..{maximum} entries')
    return value


def _point(value, name):
    values = _list(value, name, 2, 2)
    return [_integer(x, f'{name}[{i}]') for i, x in enumerate(values)]


def _hex(value, name, length):
    if not isinstance(value, str) or len(value) != length or re.fullmatch('[0-9a-fA-F]+', value) is None:
        raise ValueError(f'{name}: expected {length} hexadecimal digits')
    return value


def _finite_json(value):
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError('nonfinite JSON number')
    if isinstance(value, list):
        for item in value:
            _finite_json(item)
    elif isinstance(value, dict):
        for item in value.values():
            _finite_json(item)


def _unit(unit, name, emit):
    if not isinstance(unit, dict):
        raise ValueError(f'{name}: expected object')
    cooldown = _list(unit['cooldown'], f'{name}.cooldown', 2, 2)
    buffs = _list(unit.get('buffs', []), f'{name}.buffs', 0, 3)
    values = [
        _uint(unit['id'], f'{name}.id'), _integer(unit['team'], f'{name}.team', 0, 1),
        _integer(unit['type'], f'{name}.type', 0, 255), *_point(unit['at'], f'{name}.at'),
        *[_number(unit[k], f'{name}.{k}') for k in ('hp', 'maxHp', 'strength', 'focus')],
        _uint(unit.get('gold', 0), f'{name}.gold'), _uint(unit['gems'], f'{name}.gems'),
        _uint(unit['stateBits'], f'{name}.stateBits'),
        *[_integer(x, f'{name}.cooldown[{i}]') for i, x in enumerate(cooldown)],
        _integer(unit['restrictions'], f'{name}.restrictions', 0, 255),
        -1 if unit.get('channelRemaining') is None else _integer(unit['channelRemaining'], f'{name}.channelRemaining'),
        len(buffs),
    ]
    for index, buff in enumerate(buffs):
        values += [_uint(buff['type'], f'{name}.buffs[{index}].type'),
                   _integer(buff['timer'], f'{name}.buffs[{index}].timer'),
                   _uint(buff['payloadRaw'], f'{name}.buffs[{index}].payloadRaw')]
    emit(name, *values)


def _inventory(values, name, emit, maximum):
    values = _list(values, name, 0, maximum)
    emit(name, len(values), *[_uint(x, f'{name}[{i}]') for i, x in enumerate(values)])


def _draft_facts(facts):
    """Validate and count queried draft facts without deriving a teacher pick or feature."""
    if not isinstance(facts, dict):
        raise ValueError('draftFacts: expected object')
    _integer(facts['team'], 'draftFacts.team', 0, 1)
    size = _integer(facts['teamSize'], 'draftFacts.teamSize', 1, 16)
    teams = _list(facts['picksByTeam'], 'draftFacts.picksByTeam', 2, 2)
    statuses = Counter()
    for team, slots in enumerate(teams):
        slots = _list(slots, f'draftFacts.picksByTeam[{team}]', size, size)
        for index, slot in enumerate(slots):
            if _integer(slot['round'], 'draftFacts.slot.round', 0, size - 1) != index:
                raise ValueError('draftFacts: queried slot order differs from round indexes')
            status, raw = slot['status'], slot['rawPick']
            if status == 'unavailable':
                if raw is not None or slot.get('errorCode') != 16:
                    raise ValueError('draftFacts: unavailable slot requires native error 16 and null rawPick')
            elif status == 'picked':
                _integer(raw, 'draftFacts.slot.rawPick', 0)
            elif status == 'unselected-sentinel':
                _integer(raw, 'draftFacts.slot.rawPick', I32_MIN, -1)
            else:
                raise ValueError(f'draftFacts: unknown native slot status {status!r}')
            statuses[status] += 1
    for key in ('oursRawPicks', 'theirsRawPicks'):
        for raw in _list(facts[key], f'draftFacts.{key}', 0, size):
            _integer(raw, f'draftFacts.{key}.rawPick', 0)
    available = facts.get('availableRawPickIds')
    if available is not None:
        for raw in _list(available, 'draftFacts.availableRawPickIds', 0, 256):
            _integer(raw, 'draftFacts.availableRawPickIds.rawPick', 0)
    return statuses


def _draft_labels(record, emit):
    """Transport only the native completed query table, separately from all before-input facts."""
    if record.get('completedDraftPicksLabelOnly') is not True:
        raise ValueError('actors: completed draft table must be explicitly label-only')
    size = _integer(record['completedDraftPickTeamSize'], 'actors.completedDraftPickTeamSize', 1, 16)
    teams = _list(record['completedDraftPicksByTeam'], 'actors.completedDraftPicksByTeam', 2, 2)
    count = 0
    for team, slots in enumerate(teams):
        slots = _list(slots, f'actors.completedDraftPicksByTeam[{team}]', size, size)
        for index, slot in enumerate(slots):
            if _integer(slot['round'], 'completedDraftPick.round', 0, size - 1) != index:
                raise ValueError('completed draft query table is not in round order')
            status, raw = slot['status'], slot['rawPick']
            code = _integer(slot['queryCode'], 'completedDraftPick.queryCode', 0)
            if status == 'picked':
                if code != 0:
                    raise ValueError('completed picked slot requires a successful native query')
                emit('DRAFTLABEL', team, index, _integer(raw, 'completedDraftPick.rawPick', 0))
                count += 1
            elif status == 'unavailable':
                if code != 16 or raw is not None:
                    raise ValueError('completed unavailable slot requires native error 16 and null rawPick')
            elif status == 'unselected-sentinel':
                if code != 0:
                    raise ValueError('completed sentinel slot requires a successful native query')
                _integer(raw, 'completedDraftPick.rawPick', I32_MIN, -1)
            else:
                raise ValueError(f'unknown completed native draft slot status {status!r}')
    return size, count, teams


def _static_map(record, emit):
    width = _integer(record['width'], 'static-map.width', 1, 32767)
    height = _integer(record['height'], 'static-map.height', 1, 32767)
    area = width * height
    if area > 1000000:
        raise ValueError('static-map: more than one million tiles')
    terrain = record['terrain']
    flags = [0] * area
    for key, bit in (('wall', 1), ('hole', 2), ('bush', 4), ('altar', 8)):
        tiles = _list(terrain[key], f'terrain.{key}', 0, area)
        if len(tiles) != len(set(tiles)):
            raise ValueError(f'terrain.{key}: duplicate tile')
        for tile in tiles:
            flags[_integer(tile, f'terrain.{key}.tile', 0, area - 1)] |= bit
    emit('MAP', width, height)
    emit('FLAGS', ''.join('0123456789abcdef'[x] for x in flags))
    spawns = _list(record['spawns'], 'static-map.spawns', 2, 2)
    for team, bounds in enumerate(spawns):
        bounds = _list(bounds, f'spawns[{team}]', 4, 4)
        x0, y0, x1, y1 = [_integer(x, f'spawns[{team}]') for x in bounds]
        if not 0 <= x0 < x1 <= width or not 0 <= y0 < y1 <= height:
            raise ValueError(f'spawns[{team}]: invalid exclusive rectangle {bounds}')
        tiles = [(x, y) for y in range(y0, y1) for x in range(x0, x1)]
        emit('SPAWN', team, len(tiles), *(n for point in tiles for n in point))
    altars = terrain['altar']
    emit('ALTARS', len(altars), *(n for tile in altars for n in (tile % width, tile // width)))
    group_ids, member_ids = set(), set()
    for index, group in enumerate(_list(record['groups'], 'static-map.groups', 0, 500)):
        native = _uint(group['id'], f'groups[{index}].id')
        if native in group_ids:
            raise ValueError(f'groups: duplicate id {native}')
        group_ids.add(native)
        members = _list(group['members'], f'groups[{index}].members', 1, 500)
        fields = [native, _uint(group['gold'], 'group.gold'), _uint(group['gems'], 'group.gems'),
                  _integer(group['respawn'], 'group.respawn', 0), len(members)]
        for member in members:
            identity = _uint(member['infoId'], 'member.infoId')
            if identity in member_ids:
                raise ValueError(f'groups: duplicate member info id {identity}')
            member_ids.add(identity)
            x, y = _point(member['at'], 'member.at')
            if not 0 <= x < width or not 0 <= y < height:
                raise ValueError('member.at is outside map')
            fields += [identity, x, y, _integer(member['type'], 'member.type', 0, 255)]
        emit('GROUP', *fields)
    if 'settingsRawHex' in record:
        emit('SETTINGS', _hex(record['settingsRawHex'], 'static-map.settingsRawHex', 240))
    for consumable in _list(record.get('consumables', []), 'static-map.consumables', 0, 4):
        emit('CONSUMABLEINFO', _integer(consumable['rawId'], 'consumable.rawId', 0, 3),
             _uint(consumable['cost'], 'consumable.cost'), _uint(consumable['carryLimit'], 'consumable.carryLimit'))
    return {'width': width, 'height': height, 'groups': len(group_ids), 'member_info_ids': len(member_ids),
            'settingsRawHex': record.get('settingsRawHex'), 'tileEncoding': record.get('tileEncoding')}


def _gameplay(record, emit, missing):
    before, action = record['before'], record['action']
    me = before['self']
    moment = before['moment']
    actor = _uint(me['id'], 'self.id')
    if moment.get('actor') != actor:
        raise ValueError('before.moment.actor differs from self.id')
    emit('BEGIN', record['sequence'], record['input'], action['kind'], _uint(moment['round'], 'before.round'), actor,
         *_point(action['from'], 'action.from'), *_point(action['to'], 'action.to'),
         _uint(action['slot'], 'action.slot'), _integer(action['priority'], 'action.priority'))
    _unit(me, 'SELF', emit)
    _inventory(me['items'], 'ITEMS', emit, 48)
    _inventory(me['consumables'], 'CONSUMABLES', emit, 4)
    for other in _list(before['visibleChampions'], 'visibleChampions', 0, 16):
        _unit(other, 'UNIT', emit)
    for camp in _list(before['visibleCamps'], 'visibleCamps', 0, 500):
        emit('CAMP', _uint(camp['id'], 'camp.id'), _integer(camp['type'], 'camp.type', 0, 255),
             *_point(camp['at'], 'camp.at'), _number(camp['hp'], 'camp.hp'), _uint(camp['stateBits'], 'camp.stateBits'))
    for group in _list(before['groups'], 'groups', 0, 500):
        emit('GROUPSTATE', _uint(group['id'], 'group.id'), _uint(group['state'], 'group.state'),
             -1 if group.get('cooldown') is None else _integer(group['cooldown'], 'group.cooldown'))
    for lock in _list(before['altarLocks'], 'altarLocks', 0, 128):
        # Native observer versions use either at:[x,y] or their original x/y fields.
        at = lock.get('at', [lock.get('x'), lock.get('y')])
        emit('LOCK', _integer(lock['team'], 'lock.team', 0, 1), *_point(at, 'lock.at'),
             _integer(lock['hitsRemaining'], 'lock.hitsRemaining', 0),
             _uint(lock.get('kind', 6), 'lock.kind'), _uint(lock['handle'], 'lock.handle'))
    banked = _list(before['banked'], 'banked', 2, 2)
    emit('BANKED', *[_uint(x, f'banked[{i}]') for i, x in enumerate(banked)])
    vision = _list(before['vision']['tiles'], 'vision.tiles')
    emit('VISION', len(vision), *[_uint(x, 'vision.tile') for x in vision])
    if 'reservedTiles' in before:
        reserved = _list(before['reservedTiles'], 'reservedTiles')
        emit('RESERVED', len(reserved), *(n for tile in reserved for n in _point(tile, 'reservedTiles.tile')))
    else:
        missing['reservedTiles'] += 1
    if 'allyWardTiles' in before:
        ally_wards = _list(before['allyWardTiles'], 'allyWardTiles')
        emit('ALLYWARDS', len(ally_wards), *(n for tile in ally_wards for n in _point(tile, 'allyWardTiles.tile')))
    else:
        missing['allyWardTiles'] += 1
    command = before.get('command', me.get('command'))
    if command is not None:
        emit('COMMAND', *[_boolean(command[k], f'command.{k}') for k in ('canRecall', 'canTribute', 'canCancel', 'canBuy')],
             _integer(command['wardCharges'], 'command.wardCharges', 0),
             _integer(command['wardCooldown'], 'command.wardCooldown', 0),
             _integer(command['priority'], 'command.priority'), _uint(command['utilityKnownMask'], 'command.utilityKnownMask'))
    else:
        missing['command'] += 1
    shop_keys = ('shopAllowed', 'consumableShopAllowed', 'canSell')
    if all(key in me for key in shop_keys):
        emit('SHOPFLAGS', *[_boolean(me[key], f'self.{key}') for key in shop_keys])
    elif any(key in me for key in shop_keys):
        raise ValueError('self: incomplete native shop permission fields')
    else:
        missing['shopFlags'] += 1
    if 'wardCharges' in me and 'wardCooldown' in me:
        priority_known = 'nextPriority' in me
        priority = me.get('nextPriority', 0)
        if not priority_known:
            missing['nextPriority'] += 1
        emit('WARDRESOURCES', _integer(me['wardCharges'], 'self.wardCharges', 0),
             _integer(me['wardCooldown'], 'self.wardCooldown', 0),
             _integer(priority, 'self.nextPriority'), int(priority_known))
    elif 'wardCharges' in me or 'wardCooldown' in me:
        raise ValueError('self: incomplete native ward resource fields')
    else:
        missing['wardResources'] += 1
    buyable = before.get('buyableItems', me.get('buyableItems'))
    if buyable is not None:
        buyable = _list(buyable, 'buyableItems', 0, 48)
        emit('BUYABLE', len(buyable), *[_integer(x, 'buyableItems.id', 0, 47) for x in buyable])
    utilities = before.get('utilityActions', me.get('utilityActions'))
    if utilities is not None:
        for utility in _list(utilities, 'utilityActions', 0, 100000):
            facts = _list(utility['phi'], 'utility.phi', 24, 24)
            emit('UTILITY', _integer(utility['type'], 'utility.type', 0, 255),
                 _integer(utility['target'], 'utility.target'), *_point(utility['at'], 'utility.at'),
                 *[_number(x, 'utility.phi') for x in facts])
    if 'aliveCounts' in before:
        counts = _list(before['aliveCounts'], 'aliveCounts', 2, 2)
        emit('COUNTS', *[_integer(x, 'aliveCounts.value', 0, 16) for x in counts])
    else:
        missing['aliveCounts'] += 1
    for ward in before.get('wards', me.get('wards', [])):
        emit('WARD', *_point(ward.get('at', ward) if isinstance(ward, dict) else ward, 'ward.at'))
    after = record.get('afterSelf')
    if after is None:
        emit('AFTER', -1, 0, 0)
    else:
        if _uint(after['id'], 'afterSelf.id') != actor:
            raise ValueError('afterSelf.id differs from self.id')
        items = _list(after['items'], 'afterSelf.items', 0, 48)
        consumables = _list(after['consumables'], 'afterSelf.consumables', 0, 4)
        emit('AFTER', _uint(after['gold'], 'afterSelf.gold'), len(items),
             *[_uint(x, 'afterSelf.items.value') for x in items], len(consumables),
             *[_uint(x, 'afterSelf.consumables.value') for x in consumables])
    emit('ENDINPUT')


def transport(path: Path | str, tokens: Path | str):
    """Write a validated ordered token stream and return provenance plus coverage of transported fields."""
    path, tokens = Path(path), Path(tokens)
    if path.resolve() == tokens.resolve():
        raise ValueError('input and token output must differ')
    temporary = tokens.with_name(tokens.name + f'.{uuid.uuid4().hex}.tmp')
    opener = gzip.open if path.suffix == '.gz' else open
    metadata = trailer = map_info = None
    actors_seen = False
    sequence, previous_input, gameplay, skipped, drafts, draft_inputs = 0, 0, 0, 0, 0, 0
    kinds, missing, draft_statuses = Counter(), Counter(), Counter()
    draft_facts = []
    completed_draft_size = completed_draft_count = 0
    completed_draft_picks = None
    tokens.parent.mkdir(parents=True, exist_ok=True)
    try:
        with opener(path, 'rt', encoding='utf-8') as source, temporary.open('x', encoding='utf-8', newline='\n') as target:
            def emit(tag, *values):
                target.write(' '.join([tag, *(str(x) for x in values)]) + '\n')

            for line_number, line in enumerate(source, 1):
                try:
                    record = json.loads(line, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f'nonfinite JSON {value}')))
                    _finite_json(record)
                    if not isinstance(record, dict):
                        raise ValueError('record is not an object')
                    record_type = record.get('type')
                    if trailer is not None:
                        raise ValueError('data after trailer')
                    if record_type == 'metadata':
                        if metadata is not None or line_number != 1:
                            raise ValueError('metadata must be the first and only metadata record')
                        if record.get('schema') not in EXPORT_SCHEMAS:
                            raise ValueError(f'unsupported export schema {record.get("schema")!r}')
                        metadata = record
                        emit('EXACT', TOKEN_SCHEMA, _integer(record['winner'], 'winner', -1, 1))
                    elif metadata is None:
                        raise ValueError('missing leading metadata')
                    elif record_type == 'static-map':
                        if map_info is not None or gameplay:
                            raise ValueError('duplicate or late static map')
                        map_info = _static_map(record, emit)
                    elif record_type == 'actors':
                        if map_info is None or actors_seen or gameplay:
                            raise ValueError('actors must follow the map and precede gameplay')
                        ids = set()
                        for actor in _list(record['roster'], 'actors.roster', 1, 16):
                            identity = _uint(actor['id'], 'actor.id')
                            if identity in ids:
                                raise ValueError('duplicate actor id')
                            ids.add(identity)
                            emit('ACTOR', identity, _integer(actor['team'], 'actor.team', 0, 1),
                                 _integer(actor['type'], 'actor.type', 0, 255))
                        for design in _list(record.get('designs', []), 'actors.designs', 0, 256):
                            emit('DESIGN', _integer(design['type'], 'design.type', 0, 255),
                                 _hex(design['rawHex'], 'design.rawHex', 256))
                        if 'completedDraftPicksByTeam' in record:
                            completed_draft_size, completed_draft_count, completed_draft_picks = _draft_labels(record, emit)
                            if any(item['facts']['teamSize'] != completed_draft_size for item in draft_facts):
                                raise ValueError('completed draft table size differs from native before-input team size')
                        elif metadata['schema'] == 'zero-exact-inputs/v1.6':
                            raise ValueError('v1.6 actors record lacks its native completed draft query table')
                        actors_seen = True
                    elif record_type == 'input':
                        seq = _uint(record['sequence'], 'sequence')
                        input_index = _uint(record['input'], 'input')
                        action, before = record['action'], record['before']
                        kind = _uint(action['kind'], 'action.kind')
                        if seq != sequence or input_index != previous_input + 1:
                            raise ValueError(f'non-contiguous input sequence {seq}/{input_index}; expected {sequence}/{previous_input + 1}')
                        if action.get('input') != input_index or before['moment'].get('input') != input_index - 1:
                            raise ValueError('before/action input indexes disagree with record')
                        if before['moment'].get('awaitingInput') is not True or action.get('substep') != 0:
                            raise ValueError('input lacks a ready predecessor or substep-zero successor')
                        native_names = metadata.get('nativeInputKinds', [])
                        expected_name = native_names[kind] if kind < len(native_names) else f'unknown-{kind}'
                        if action.get('name') != expected_name:
                            raise ValueError('action name does not match numeric native kind')
                        kinds[expected_name] += 1
                        if before.get('draft') or before.get('self') is None:
                            draft_inputs += 1
                            if 'draftFacts' in before:
                                facts = before['draftFacts']
                                draft_statuses.update(_draft_facts(facts))
                                draft_facts.append({'sequence': seq, 'input': input_index, 'facts': facts})
                                before_round = _uint(before['moment']['round'], 'draft.beforeRound')
                                step_round = _uint(action['stepRound'], 'draft.actionStepRound')
                                if step_round != before_round or before_round >= facts['teamSize']:
                                    raise ValueError('native draft action.stepRound must equal the before-input round and fit team size')
                                ours, theirs = facts['oursRawPicks'], facts['theirsRawPicks']
                                emit('DRAFT', seq, input_index, kind, before_round, facts['team'], facts['teamSize'], step_round,
                                     len(ours), *ours, len(theirs), *theirs)
                                drafts += 1
                            else:
                                missing['draftFacts'] += 1
                                actor = before['moment'].get('actor')
                                actor = -1 if actor is None or actor == U32 else _uint(actor, 'draft.actor')
                                emit('SKIP', seq, input_index, kind, actor, _uint(before['moment']['round'], 'draft.round'))
                                skipped += 1
                        else:
                            if map_info is None or not actors_seen:
                                raise ValueError('gameplay input before static map and actors')
                            _gameplay(record, emit, missing)
                            gameplay += 1
                        sequence += 1
                        previous_input = input_index
                    elif record_type == 'trailer':
                        trailer = record
                        if record.get('pass') is not True or record.get('nonReadyInputs') != 0:
                            raise ValueError('export trailer reports failure or non-ready inputs')
                        if record.get('records') != sequence or record.get('observedInputs') != sequence or record.get('lastInput') != previous_input or record.get('firstInput') != (1 if sequence else None):
                            raise ValueError('export trailer input counts disagree with stream')
                        if record.get('kinds') != dict(kinds):
                            raise ValueError('export trailer kind counts disagree with stream')
                        emit('FINISH', sequence, sequence, 0, 1)
                    else:
                        raise ValueError(f'unknown JSONL record type {record_type!r}')
                except (KeyError, TypeError, ValueError) as error:
                    raise ValueError(f'{path}:{line_number}: {error}') from error
            if metadata is None or trailer is None or map_info is None or not actors_seen:
                raise ValueError(f'{path}: incomplete export (metadata/map/actors/trailer required)')
        temporary.replace(tokens)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return {'token_schema': TOKEN_SCHEMA, 'export_schema': metadata['schema'], 'source_sha256': digest.hexdigest(),
            'metadata': metadata, 'map': map_info, 'records': sequence, 'gameplay_records': gameplay,
            'skipped_records': skipped, 'draft_records': drafts, 'draft_input_records': draft_inputs,
            'input_kinds': dict(kinds), 'missing_fields': dict(missing),
            'draft_facts': draft_facts,
            'draft_fact_counts': {'records': len(draft_facts), 'missing_records': draft_inputs - len(draft_facts),
                                  'queried_slots': sum(draft_statuses.values()), 'statuses': dict(draft_statuses)},
            'completed_draft_picks': completed_draft_picks,
            'completed_draft_counts': {'team_size': completed_draft_size, 'picked_slots': completed_draft_count},
            'first_input': 1 if sequence else None, 'last_input': previous_input, 'trailer': trailer,
            'tokens': str(tokens)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--tokens', type=Path, required=True)
    args = parser.parse_args()
    result = transport(args.input, args.tokens)
    print(json.dumps(result, separators=(',', ':')))


if __name__ == '__main__':
    main()
