"""Transport a bounded raw source prefix to the native student-record producer.

This module copies native facts and source events only. It does not calculate features,
candidates, labels, command counters, memory updates or goal transitions. The native producer
owns those operations. ``--sidecar`` saves the returned source evidence as JSON.
"""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import uuid

try:
    from . import exact_records as exact
except ImportError:
    import exact_records as exact


TOKEN_SCHEMA = 1
SOURCE_SHA256 = '8e076da869523cec00a5856f9cecf7b22bc9a332675580c2c23f76618904c9ec'
HISTORY_TYPE = 'sourceprefix+declared_no_goal/v1'


def _sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _object(value, name):
    if not isinstance(value, dict):
        raise ValueError(f'{name}: expected object')
    return value


def _action(record, metadata, sequence, previous_input):
    """Validate the original native ordering without classifying phases or decoding actions."""
    seq = exact._uint(record['sequence'], 'sequence')
    index = exact._uint(record['input'], 'input')
    if seq != sequence or index != previous_input + 1:
        raise ValueError(f'non-contiguous input sequence {seq}/{index}; expected {sequence}/{previous_input + 1}')
    action = _object(record['action'], 'action')
    before = _object(record['before'], 'before')
    moment = _object(before['moment'], 'before.moment')
    kind = exact._uint(action['kind'], 'action.kind')
    if exact._uint(action['input'], 'action.input') != index or exact._uint(moment['input'], 'before.moment.input') != index - 1:
        raise ValueError('before/action input indexes disagree with record')
    if moment['awaitingInput'] is not True or exact._uint(action['substep'], 'action.substep') != 0:
        raise ValueError('input lacks a ready predecessor or substep-zero successor')
    names = exact._list(metadata['nativeInputKinds'], 'metadata.nativeInputKinds', 1, 256)
    expected_name = names[kind] if kind < len(names) else f'unknown-{kind}'
    if action['name'] != expected_name:
        raise ValueError('action name does not match numeric native kind')
    return index, expected_name


def _actors(record, emit):
    roster = {}
    for actor in exact._list(record['roster'], 'actors.roster', 1, 16):
        identity = exact._uint(actor['id'], 'actor.id')
        if identity in roster:
            raise ValueError(f'duplicate actor id {identity}')
        team = exact._integer(actor['team'], 'actor.team', 0, 1)
        kind = exact._integer(actor['type'], 'actor.type', 0, 255)
        roster[identity] = (team, kind)
        emit('ACTOR', identity, team, kind)
    designs = exact._list(record['designs'], 'actors.designs', 1, 256)
    design_ids = set()
    for design in designs:
        kind = exact._integer(design['type'], 'design.type', 0, 255)
        if kind in design_ids:
            raise ValueError(f'duplicate design type {kind}')
        design_ids.add(kind)
        emit('DESIGN', kind, exact._hex(design['rawHex'], 'design.rawHex', 256))
    if any(kind not in design_ids for _, kind in roster.values()):
        raise ValueError('actor type lacks a native static design')
    return roster, len(designs)


def _gameplay(record, roster, emit, missing):
    before, action = record['before'], record['action']
    moment = before['moment']
    me = _object(before['self'], 'self')
    actor = exact._uint(me['id'], 'self.id')
    team = exact._integer(me['team'], 'self.team', 0, 1)
    kind = exact._integer(me['type'], 'self.type', 0, 255)
    if roster.get(actor) != (team, kind):
        raise ValueError('self identity/team/type differs from native actor roster')
    if exact._uint(moment['actor'], 'before.moment.actor') != actor or exact._uint(action['actor'], 'action.actor') != actor:
        raise ValueError('before/action actor differs from self.id')
    if exact._integer(moment['team'], 'before.moment.team', 0, 1) != team or exact._integer(action['team'], 'action.team', 0, 1) != team:
        raise ValueError('before/action team differs from self.team')
    owner_turn = exact._uint(moment['turn'], 'before.moment.turn')
    if exact._uint(action['turn'], 'action.turn') != owner_turn:
        raise ValueError('action owner turn differs from before-input owner turn')
    round_number = exact._uint(moment['round'], 'before.moment.round')
    if exact._uint(action['round'], 'action.round') != round_number:
        raise ValueError('action round differs from before-input round')
    # exact._unit intentionally permits missing gold/buffs on visible units. SELF must never
    # acquire the helper's optional defaults, even when the known native value is zero/empty.
    exact._uint(me['gold'], 'self.gold')
    exact._list(me['buffs'], 'self.buffs', 0, 3)
    shield = exact._number(me['shield'], 'self.shield')
    stunned = exact._boolean(me['stunned'], 'self.stunned')
    origins = (exact._uint(moment['phase'], 'before.moment.phase'),
               exact._uint(action['phase'], 'action.phase'), owner_turn,
               exact._uint(moment['substep'], 'before.moment.substep'),
               exact._uint(action['substep'], 'action.substep'))

    def native_emit(tag, *values):
        if tag == 'ENDINPUT':
            emit('SELFBUFF', shield, stunned)
        emit(tag, *values)
        if tag == 'BEGIN':
            emit('ORIGINS', *origins)

    exact._gameplay(record, native_emit, missing)
    return actor


def transport(source: Path | str, tokens: Path | str, first_input=21, last_input=30,
              emit_actor=6, expected_sha=SOURCE_SHA256):
    """Write raw source tokens atomically and return JSON-serializable source-only evidence.

    The supplied SHA pins the entire compressed export, including records outside the retained
    range. Draft inputs are checked for chronology, but never transported or used for training.
    Only records in the explicit inclusive range become BEGIN/ENDINPUT source events.
    ``emit_actor=-1`` includes every retained source actor in the declared raw actor scope;
    this scope does not assert native inverse eligibility.
    """
    source, tokens = Path(source), Path(tokens)
    if source.resolve() == tokens.resolve():
        raise ValueError('source and token output must differ')
    first_input = exact._integer(first_input, 'first_input', 1, exact.U32)
    last_input = exact._integer(last_input, 'last_input', first_input, exact.U32)
    emit_actor = exact._integer(emit_actor, 'emit_actor', -1, exact.U32)
    expected_sha = exact._hex(expected_sha, 'expected_sha', 64).lower()
    source_sha = _sha256(source)
    if source_sha != expected_sha:
        raise ValueError(f'{source}: source SHA256 mismatch: expected {expected_sha}, got {source_sha}')
    temporary = tokens.with_name(tokens.name + f'.{uuid.uuid4().hex}.tmp')
    opener = gzip.open if source.suffix == '.gz' else open
    metadata = map_record = actor_record = map_info = trailer = None
    roster = None
    design_count = sequence = previous_input = draft_inputs = gameplay_seen = 0
    kinds, missing = Counter(), Counter()
    retained, expected_emit_inputs = [], []
    tokens.parent.mkdir(parents=True, exist_ok=True)
    try:
        with opener(source, 'rt', encoding='utf-8') as stream, temporary.open('x', encoding='utf-8', newline='\n') as target:
            def emit(tag, *values):
                target.write(' '.join([tag, *(str(x) for x in values)]) + '\n')

            for line_number, line in enumerate(stream, 1):
                try:
                    record = json.loads(line, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f'nonfinite JSON {value}')))
                    exact._finite_json(record)
                    _object(record, 'record')
                    record_type = record.get('type')
                    if trailer is not None:
                        raise ValueError('data after trailer')
                    if record_type == 'metadata':
                        if metadata is not None or line_number != 1:
                            raise ValueError('metadata must be the first and only metadata record')
                        if record['schema'] not in exact.EXPORT_SCHEMAS:
                            raise ValueError(f'unsupported export schema {record["schema"]!r}')
                        metadata = record
                        emit('SOURCE', TOKEN_SCHEMA, exact._integer(metadata['winner'], 'winner', -1, 1),
                             first_input, last_input, emit_actor)
                        emit('SOURCE_SHA', source_sha)
                    elif metadata is None:
                        raise ValueError('missing leading metadata')
                    elif record_type == 'static-map':
                        if map_info is not None or gameplay_seen:
                            raise ValueError('duplicate or late static map')
                        map_info = exact._static_map(record, emit)
                        map_record = record
                    elif record_type == 'actors':
                        if map_info is None or roster is not None or gameplay_seen:
                            raise ValueError('actors must follow map and precede gameplay')
                        roster, design_count = _actors(record, emit)
                        if emit_actor >= 0 and emit_actor not in roster:
                            raise ValueError('emit_actor is absent from native actor roster')
                        actor_record = record
                    elif record_type == 'input':
                        index, name = _action(record, metadata, sequence, previous_input)
                        kinds[name] += 1
                        before = record['before']
                        is_draft = bool(before.get('draft')) or before.get('self') is None
                        if is_draft:
                            draft_inputs += 1
                            if first_input <= index <= last_input:
                                raise ValueError('retained source scope contains a draft input')
                        else:
                            if map_info is None or roster is None:
                                raise ValueError('gameplay input before static map and actors')
                            gameplay_seen += 1
                            if first_input <= index <= last_input:
                                actor = _gameplay(record, roster, emit, missing)
                                retained.append(record)
                                if emit_actor == -1 or actor == emit_actor:
                                    expected_emit_inputs.append(index)
                        sequence += 1
                        previous_input = index
                    elif record_type == 'trailer':
                        trailer = record
                        if trailer['pass'] is not True or exact._uint(trailer['nonReadyInputs'], 'trailer.nonReadyInputs') != 0:
                            raise ValueError('export trailer reports failure or non-ready inputs')
                        if any(exact._uint(trailer[key], f'trailer.{key}') != sequence for key in ('records', 'observedInputs')):
                            raise ValueError('export trailer input counts disagree with stream')
                        if exact._uint(trailer['lastInput'], 'trailer.lastInput') != previous_input or trailer['firstInput'] != 1:
                            raise ValueError('export trailer input bounds disagree with stream')
                        if trailer['kinds'] != dict(kinds):
                            raise ValueError('export trailer kind counts disagree with stream')
                    else:
                        raise ValueError(f'unknown JSONL record type {record_type!r}')
                except (KeyError, TypeError, ValueError) as error:
                    raise ValueError(f'{source}:{line_number}: {error}') from error
            if any(value is None for value in (metadata, map_info, roster, trailer)):
                raise ValueError(f'{source}: incomplete export (metadata/map/actors/trailer required)')
            if len(retained) != last_input - first_input + 1:
                raise ValueError(f'{source}: retained source scope is incomplete')
            if not expected_emit_inputs:
                raise ValueError(f'{source}: retained scope contains no emit_actor input')
            emit('FINISH', len(retained), first_input, last_input, emit_actor)
        # Detect an export being replaced while it is transported, before publishing tokens.
        if _sha256(source) != source_sha:
            raise ValueError(f'{source}: source changed during transport')
        temporary.replace(tokens)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    return {'schema': 'zero-student-source-transport/v1', 'token_schema': TOKEN_SCHEMA,
            'source': str(source.resolve()), 'source_sha256': source_sha, 'metadata': metadata,
            'map': map_info, 'static_counts': {'actors': len(roster), 'designs': design_count,
                'groups': map_info['groups'], 'member_info_ids': map_info['member_info_ids'],
                'consumables': len(map_record.get('consumables', []))},
            'source_static_map': map_record, 'source_actors': actor_record, 'source_trailer': trailer,
            'raw_records': retained, 'retained_records': len(retained),
            'first_input': first_input, 'last_input': last_input, 'emit_actor': emit_actor,
            'expected_emit_inputs': expected_emit_inputs,
            'expected_emit_inputs_scope': 'Raw actor scope only; native inverse decides eligibility and exclusions.',
            'missing_fields': dict(missing),
            'export_records': sequence, 'draft_inputs_skipped': draft_inputs,
            'history': {'type': HISTORY_TYPE, 'initial_memory': {}, 'initial_goal': 'NONE',
                        'raw_scope': [first_input, last_input],
                        'scope': 'Declared source prefix only; no earlier bot memory or internal goal observations.'},
            'tokens': str(tokens.resolve())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--tokens', type=Path, required=True)
    parser.add_argument('--first-input', type=int, default=21)
    parser.add_argument('--last-input', type=int, default=30)
    parser.add_argument('--emit-actor', type=int, default=6, help='Native source actor id; -1 selects all source actors')
    parser.add_argument('--expected-sha', default=SOURCE_SHA256)
    parser.add_argument('--sidecar', type=Path)
    args = parser.parse_args()
    if args.sidecar is not None and args.sidecar.resolve() in (args.input.resolve(), args.tokens.resolve()):
        parser.error('sidecar must differ from input and tokens')
    result = transport(args.input, args.tokens, args.first_input, args.last_input,
                       args.emit_actor, args.expected_sha)
    serialized = json.dumps(result, separators=(',', ':'), allow_nan=False)
    if args.sidecar is not None:
        args.sidecar.parent.mkdir(parents=True, exist_ok=True)
        args.sidecar.write_text(serialized + '\n', encoding='utf-8', newline='\n')
        print(json.dumps({'sidecar': str(args.sidecar.resolve()), 'tokens': result['tokens'],
                          'source_sha256': result['source_sha256'],
                          'retained_records': result['retained_records'],
                          'expected_emit_inputs': result['expected_emit_inputs']}, separators=(',', ':')))
    else:
        print(serialized)


if __name__ == '__main__':
    main()
