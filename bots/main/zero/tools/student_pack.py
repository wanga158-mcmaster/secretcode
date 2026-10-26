"""Oracle-source context frames: decision origins differ from delayed label resolutions.

Source semantics are independent of actor initialization, scheduling and deployment fuel. The
Supported contract slices are one verified draft pair and a declared no-goal personal gameplay
prefix. Historical ZSTUD001 frames cannot be silently upgraded to this source contract.
"""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import uuid
import numpy as np

PACK_SCHEMA = FEATURE_SCHEMA = 3
FEATURE_ABI = 'main.zero.student.context.v1'
MODEL_ABI = 'main.zero.student.attention.v1'
COVERAGE_SCOPE = 'paired_source_units'
STATE, RAW_PHI, COMMANDS, CONTEXT_PHI = 48, 24, 15, 12
HEAD_IDS = [0, 1, 2, 3, 4, 5, 6, 10, 11, 12]
HEADS = ['goal', 'step', 'attack', 'cast0', 'cast1', 'shop', 'draft', 'aux', 'priority', 'quantity']
HEAD_NAMES = dict(zip(HEAD_IDS, HEADS))
SCALAR_HEADS = (11, 12)
COMMAND, POINTER, SCALAR = 0, 1, 2
POLICY_PROFILE = 'unconditional_camp_attention_v1'
PROFILE_MASK = ((1 << COMMANDS) - 1) & ~(1 << 9)
MAGIC = b'ZSTUD003'
LENGTH = struct.Struct('<I')
ROW_DTYPE = np.dtype([('payload', '<i4', (5,)), ('phi', '<f4', (RAW_PHI,))], align=False)
CONTEXT_DTYPE = np.dtype([('group', '<i4'), ('phi', '<f4', (CONTEXT_PHI,))], align=False)
ORIGIN_FIELDS = ('sequence', 'input', 'actor', 'team', 'round', 'phase', 'native_kind')
QUALIFICATION_FLAGS = ('source_semantic_pass', 'representation_pass', 'history_contract_pass',
                       'deployment_slice_pass', 'optimizer_allowed')
SOURCE_FLAGS = QUALIFICATION_FLAGS[:3]
GATES = SOURCE_FLAGS
DRAFT_HISTORY = 'source_draft_prefix/v1'
NO_GOAL_HISTORY = 'sourceprefix+declared_no_goal/v1'
HISTORY_CONTRACTS = (DRAFT_HISTORY, NO_GOAL_HISTORY)
FIRST_GAMEPLAY_SCOPE = {'actor': 6, 'first_input': 26, 'last_input': 30}
ECONOMIC_SCOPE = {'actor': -1, 'first_input': 21, 'last_input': 2256}
# A fixed acceptance profile, never an inverse or a label generator. Native source intent owns labels.
FIRST_GAMEPLAY_ACTIONS = {26: (6, 4), 27: (4, 1), 28: (11, 6), 29: (9, 14), 30: (8, 0)}
NATIVE_COMMANDS = {4: (1,), 5: (2,), 6: (3, 4), 8: (0,), 9: (14,), 11: (6,), 12: (7,),
                   13: (5, 10), 14: (12,), 16: (8,), 20: (11,), 21: (11,), 25: (13,)}
SOURCE_STATUSES = ('resolved', 'exogenous', 'phase_event', 'ambiguous', 'missing_facts', 'no_match',
                   'no_effect', 'unsupported', 'invalid_event', 'overflow')


def _i32(value, field):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or not -(1 << 31) <= value < 1 << 31:
        raise ValueError(f'{field}: expected signed int32')
    return int(value)


def _sha_value(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def _json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False, allow_nan=False)


def _hash(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate_origin(origin):
    if not isinstance(origin, dict) or any(field not in origin for field in ORIGIN_FIELDS):
        raise ValueError('both origins require sequence/input/actor/team/round/phase/native_kind')
    values = {field: _i32(origin[field], field) for field in ORIGIN_FIELDS}
    if values['sequence'] < 0 or values['input'] < 1 or values['team'] not in (0, 1) or values['round'] < 0:
        raise ValueError('invalid source origin')
    return origin


def origin_key(origin):
    return tuple(origin[field] for field in ORIGIN_FIELDS)


def unit_key(metadata):
    return origin_key(metadata['decision_origin']), origin_key(metadata['label_origin']), metadata['label_kind']


@dataclass(frozen=True)
class Record:
    metadata: dict
    state: np.ndarray
    payload: np.ndarray
    phi: np.ndarray
    context_payload: np.ndarray | None = None
    context_phi: np.ndarray | None = None

    def __getattr__(self, name):
        if name in ('kind', 'head', 'command', 'n', 'label', 'won', 'event_role', 'label_kind', 'history_type',
                    'qualification', 'decision_origin', 'label_origin', 'rule_mask', 'admitted_mask', 'history', 'facts',
                    'policy_profile', 'profile_mask', 'training_rule_mask', 'context_n'):
            return self.metadata[name]
        if name in ('sequence', 'input', 'actor', 'team', 'round'):
            return self.metadata['decision_origin'][name]
        raise AttributeError(name)

    @property
    def annotation(self):
        return self.label_kind.startswith('derived_')

    @property
    def source_sequence(self):
        return self.decision_origin['sequence']

    @property
    def mask(self):
        return self.training_rule_mask if self.label_kind == 'observed_engine_action' else self.rule_mask


def validate_record(record):
    m = record.metadata
    if not isinstance(m, dict) or m.get('feature_schema') != FEATURE_SCHEMA:
        raise ValueError('feature3 context metadata required; older frames cannot be upgraded implicitly')
    context_n = _i32(m.get('context_n'), 'context_n')
    if context_n < 0 or _i32(m.get('context_phi'), 'context_phi') != CONTEXT_PHI:
        raise ValueError('context metadata requires a nonnegative row count and twelve raw facts')
    context_ids, context = np.asarray(record.context_payload), np.asarray(record.context_phi)
    if (context_ids.dtype != np.int32 or context_ids.shape != (context_n,)
            or not np.array_equal(context_ids, np.arange(context_n, dtype=np.int32))
            or context.dtype != np.float32 or context.shape != (context_n, CONTEXT_PHI) or not np.isfinite(context).all()):
        raise ValueError('all canonical static camp groups require explicit int32 IDs and lossless float32 context facts')
    for field in ('kind', 'head', 'command', 'n', 'label', 'won'):
        _i32(m.get(field), field)
    if m['won'] not in (-1, 0, 1) or m['n'] < 0:
        raise ValueError('invalid outcome or candidate count')
    validate_origin(m.get('decision_origin')); validate_origin(m.get('label_origin'))
    for field in ('event_role', 'label_kind', 'history_type'):
        if not isinstance(m.get(field), str) or not m[field]:
            raise ValueError(f'explicit {field} required')
    q = m.get('qualification')
    if (not isinstance(q, dict) or any(type(q.get(field)) is not bool for field in QUALIFICATION_FLAGS)
            or q.get('purpose') not in ('contract_test', 'bc_source', 'diagnostic')):
        raise ValueError('source/representation/history/deployment qualification must be separate')
    if q['deployment_slice_pass'] or q['optimizer_allowed']:
        raise ValueError('source contract frames have no deployment or optimizer permission')
    for field in ('rule_mask', 'admitted_mask'):
        if field not in m or (m[field] is not None and not 0 <= _i32(m[field], field) < 1 << COMMANDS):
            raise ValueError('semantic rule and optional admitted masks must be explicit')
    if not isinstance(m.get('pairproof'), dict):
        raise ValueError('pairproof object required')
    state, payload, phi = map(np.asarray, (record.state, record.payload, record.phi))
    if state.shape != (STATE,) or state.dtype != np.float32 or not np.isfinite(state).all():
        raise ValueError('48 finite float32 decision-origin state facts required')
    if payload.shape != (m['n'], 5) or payload.dtype != np.int32:
        raise ValueError('n*5 int32 executable identities required')
    if phi.shape != (m['n'], RAW_PHI) or phi.dtype != np.float32 or not np.isfinite(phi).all():
        raise ValueError('complete n*24 finite float32 descriptors required')
    if m['kind'] == COMMAND:
        if (m['head'] != -1 or m['n'] or m['command'] != m['label'] or not 0 <= m['label'] < COMMANDS
                or m['rule_mask'] is None or not m['rule_mask'] & (1 << m['label'])):
            raise ValueError('command CE requires semantic legality, independent of admission')
    elif m['kind'] in (POINTER, SCALAR):
        if m['head'] not in HEAD_NAMES or m['n'] < 1:
            raise ValueError('unknown family or empty selected pointer')
        if m['kind'] == SCALAR:
            if m['head'] not in SCALAR_HEADS or m['n'] != 1 or (m['head'] == 12 and m['label'] < 1):
                raise ValueError('scalar singleton requires a source int32 regression target')
        elif m['head'] in SCALAR_HEADS or not 0 <= m['label'] < m['n']:
            raise ValueError('categorical label must index the full pointer list')
        if (np.any(payload[:, 0] < 0) or np.any(payload[:, 0] > 255) or np.any(payload[:, 2] < 1)
                or np.any(payload[:, 3:] < -32768) or np.any(payload[:, 3:] > 32767)):
            raise ValueError('payload exceeds Cand ABI')
    else:
        raise ValueError('unknown record kind')
    if m.get('feature_origin', m['decision_origin']) != m['decision_origin']:
        raise ValueError('features must precede decision origin, not label resolution')
    if m['label_kind'] == 'observed_draft_choice':
        _validate_draft_pair(record)
    elif m['label_kind'] == 'observed_engine_action':
        _validate_gameplay(record)
    elif any(q[field] for field in SOURCE_FLAGS):
        raise ValueError('diagnostic/historical labels have no observed source contract')
    return record


def _validate_draft_pair(record):
    m, d, label = record.metadata, record.decision_origin, record.label_origin
    if (m['event_role'] != 'draft_dispatch' or m['history_type'] != 'source_draft_prefix/v1'
            or (m['kind'], m['head'], m['command']) != (POINTER, 6, -1)
            or m['rule_mask'] is not None or m['admitted_mask'] is not None):
        raise ValueError('draft is one phase-specific pointer example, never a primitive root')
    if (d['actor'] != -1 or label['actor'] != -1 or d['phase'] != 12 or label['phase'] != 12
            or d['native_kind'] != 7 or label['native_kind'] != 19 or d['team'] != label['team']
            or d['round'] != label['round'] or label['input'] != d['input'] + 1 or label['sequence'] != d['sequence'] + 1):
        raise ValueError('verified same-team/round phase12 dispatch/resolution pair required')
    p = m['pairproof']
    expected = {'verified': True, 'phase_checked': True, 'dispatch_api': 'Game_Action_Draft',
        'resolution_api': 'Game_Runner_EndTurn', 'dispatch_input': d['input'], 'resolution_input': label['input'],
        'team': d['team'], 'round': d['round']}
    if any(p.get(key) != value for key, value in expected.items()) or not _sha_value(p.get('proof_sha256')):
        raise ValueError('kind7 is a decision only with an explicit phase-checked inverse proof')
    pick = _i32(p.get('resolved_raw_pick'), 'resolved_raw_pick')
    if not 0 <= pick < 12 or int(record.payload[m['label'], 1]) != pick:
        raise ValueError('draft target must come from paired resolution label')


def expected_stages(command):
    """Structural command-to-head ABI only. Source intent and labels are supplied by native C++."""
    command = _i32(command, 'command')
    if not 0 <= command < COMMANDS or command == 9:
        raise ValueError('the declared no-goal contract has no internal REPLAN labels')
    if command == 0:
        return ['command']
    head = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 14: 11}.get(command, 10)
    return ['command', f'family:{head}'] + (['family:12'] if command == 5 else [])


def record_stage(record):
    return 'command' if record.kind == COMMAND else f'family:{record.head}'


def _validate_gameplay(record):
    m, d, label = record.metadata, record.decision_origin, record.label_origin
    command = m['command']
    if (m['history_type'] != NO_GOAL_HISTORY or m['admitted_mask'] is not None
            or m['event_role'] != ('observed_owner_end' if command == 0 else 'observed_attempt')):
        raise ValueError('explicit no-goal source gameplay role/history and null admission required')
    if (m.get('policy_profile') != POLICY_PROFILE or _i32(m.get('profile_mask'), 'profile_mask') != PROFILE_MASK
            or m['rule_mask'] is None
            or _i32(m.get('training_rule_mask'), 'training_rule_mask') != (m['rule_mask'] & PROFILE_MASK)
            or not m['training_rule_mask'] & (1 << command)):
        raise ValueError('the no-goal protocol profile removes only internal REPLAN from the raw semantic mask')
    if (d['phase'] != 10 or label['phase'] not in (10, 13) or (label['phase'] == 13 and command != 0)
            or any(d[field] != label[field] for field in ORIGIN_FIELDS if field != 'phase')):
        raise ValueError('gameplay evidence must retain the same source event with an allowed after phase')
    if d['actor'] < 0 or command not in NATIVE_COMMANDS.get(d['native_kind'], ()):
        raise ValueError('observed source native kind does not identify this command family')
    if record_stage(record) not in expected_stages(command):
        raise ValueError('observed command acquired the wrong family stage')
    if record.kind != COMMAND and ((record.head in SCALAR_HEADS) != (record.kind == SCALAR)):
        raise ValueError('scalar/categorical stage differs from the command ABI')
    history = m.get('history')
    if (not isinstance(history, dict) or history.get('initial_goal') != 'NONE'
            or history.get('memory_init') != 'Memory{}' or _i32(history.get('goal_kind'), 'goal_kind') != -1
            or _i32(history.get('goal_target'), 'goal_target') != -1
            or _i32(history.get('goal_since'), 'goal_since') != -1 or _i32(history.get('goal_decided'), 'goal_decided') != -1
            or _i32(history.get('owner_turn'), 'owner_turn') < 0
            or _i32(history.get('command_index_before'), 'command_index_before') < 0
            or not isinstance(history.get('personal_prefix_inputs'), list)):
        raise ValueError('declared no-goal personal prefix/counters are incomplete')
    prefix = [_i32(value, 'personal_prefix_input') for value in history['personal_prefix_inputs']]
    if not prefix or prefix[-1] != d['input'] or any(value < 1 for value in prefix) or prefix != sorted(set(prefix)):
        raise ValueError('personal source prefix must be ordered, unique and end at the current observation')
    owner = history.get('owner_prefix_inputs', prefix)  # original five-action contract has just one owner prefix
    if not isinstance(owner, list):
        raise ValueError('declared owner prefix must be an input list')
    owner = [_i32(value, 'owner_prefix_input') for value in owner]
    if (not owner or owner[-1] != d['input'] or owner != sorted(set(owner)) or any(value not in prefix for value in owner)
            or history['command_index_before'] != len(owner) - 1):
        raise ValueError('owner source prefix must match its declared within-owner attempt count')
    facts = m.get('facts')
    if not isinstance(facts, dict) or type(facts.get('self_buffs_known')) is not bool:
        raise ValueError('gameplay requires an explicit raw self-buff known fact')
    if record.qualification['representation_pass'] and facts['self_buffs_known'] is not True:
        raise ValueError('unknown raw self buffs cannot qualify representation')
    proof = m['pairproof']
    if (proof.get('oracle') != 'ReplayNext' or proof.get('inverse_status') != 'resolved'
            or not isinstance(proof.get('inverse_reason'), str) or not proof['inverse_reason']
            or not _sha_value(proof.get('source_sha256'))):
        raise ValueError('source gameplay label requires a resolved shared native inverse and source hash')


def read_records(path):
    data = Path(path).read_bytes()
    if data[:8] != MAGIC:
        raise ValueError('ZSTUD003 required; older streams cannot silently qualify context features')
    at, records = 8, []
    while at < len(data):
        if len(data) - at < LENGTH.size:
            raise ValueError('truncated metadata length')
        size = LENGTH.unpack_from(data, at)[0]; at += LENGTH.size
        if not size or size > len(data) - at:
            raise ValueError('truncated paired metadata')
        m = json.loads(data[at:at + size].decode('utf-8')); at += size
        n = _i32(m.get('n'), 'n')
        context_n = _i32(m.get('context_n'), 'context_n')
        if (n < 0 or context_n < 0 or _i32(m.get('context_phi'), 'context_phi') != CONTEXT_PHI
                or STATE * 4 + context_n * CONTEXT_DTYPE.itemsize + n * ROW_DTYPE.itemsize > len(data) - at):
            raise ValueError('truncated state/candidate facts')
        state = np.frombuffer(data, '<f4', STATE, at).copy(); at += STATE * 4
        context = np.frombuffer(data, CONTEXT_DTYPE, context_n, at); at += context_n * CONTEXT_DTYPE.itemsize
        rows = np.frombuffer(data, ROW_DTYPE, n, at); at += n * ROW_DTYPE.itemsize
        records.append(validate_record(Record(m, state, rows['payload'].copy(), rows['phi'].copy(),
                                             context['group'].copy(), context['phi'].copy())))
    if not records:
        raise ValueError('empty source frame stream')
    return records


def write_sink(records, path):
    with Path(path).open('wb') as stream:
        stream.write(MAGIC)
        for record in records:
            validate_record(record)
            metadata = _json(record.metadata).encode('utf-8')
            stream.write(LENGTH.pack(len(metadata))); stream.write(metadata)
            stream.write(record.state.astype('<f4', copy=False).tobytes())
            context = np.empty(record.context_n, dtype=CONTEXT_DTYPE)
            context['group'], context['phi'] = record.context_payload, record.context_phi
            stream.write(context.tobytes())
            rows = np.empty(record.n, dtype=ROW_DTYPE)
            rows['payload'], rows['phi'] = record.payload, record.phi
            stream.write(rows.tobytes())


def manifest_sha256(manifest):
    return hashlib.sha256(_json(manifest).encode()).hexdigest()


def make_manifest(*, source_sha256, producer_binary_sha256, feature_sources, label_source_refs,
                  source_root=None, producer_binary_path=None, purpose='contract_test', evidence_refs=None,
                  history_contracts=None, policy_profile=None):
    histories = list(history_contracts) if history_contracts is not None else [DRAFT_HISTORY]
    return verify_manifest({'pack_schema': PACK_SCHEMA, 'feature_schema': FEATURE_SCHEMA, 'feature_abi': FEATURE_ABI,
        'model_abi': MODEL_ABI, 'producer': 'oracle_replay_source', 'sink_magic': MAGIC.decode(),
        'state': STATE, 'raw_phi': RAW_PHI, 'commands': COMMANDS, 'context_phi': CONTEXT_PHI,
        'heads': HEADS, 'head_ids': HEAD_IDS,
        'storage': 'float32_lossless_ragged', 'fuel_feature': False, 'obstacle_path_facts': False,
        'allied_claim_facts': False, 'history_contracts': histories,
        'policy_profile': policy_profile if policy_profile is not None else POLICY_PROFILE if NO_GOAL_HISTORY in histories else None,
        'draft_abi': 'shared_source_draft_facts/v1', 'purpose': purpose,
        'source_sha256': dict(source_sha256), 'producer_binary_sha256': producer_binary_sha256,
        'source_root': str(Path(source_root).resolve()) if source_root is not None else None,
        'producer_binary_path': str(Path(producer_binary_path).resolve()) if producer_binary_path is not None else None,
        'feature_sources': feature_sources, 'label_source_refs': label_source_refs, 'evidence_refs': evidence_refs or []})


def verify_manifest(manifest, *, verify_files=False):
    if not isinstance(manifest, dict):
        manifest = json.loads(Path(manifest).read_text(encoding='utf-8'))
    expected = {'pack_schema': PACK_SCHEMA, 'feature_schema': FEATURE_SCHEMA, 'feature_abi': FEATURE_ABI,
        'model_abi': MODEL_ABI, 'producer': 'oracle_replay_source', 'sink_magic': MAGIC.decode(),
        'state': STATE, 'raw_phi': RAW_PHI, 'commands': COMMANDS, 'context_phi': CONTEXT_PHI,
        'heads': HEADS, 'head_ids': HEAD_IDS,
        'storage': 'float32_lossless_ragged', 'fuel_feature': False, 'obstacle_path_facts': False,
        'allied_claim_facts': False, 'draft_abi': 'shared_source_draft_facts/v1'}
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise ValueError('not source-oracle feature3; older feature/context contracts are superseded')
    histories = manifest.get('history_contracts')
    if (manifest.get('purpose') not in ('contract_test', 'bc_source') or not isinstance(histories, list) or not histories
            or any(history not in HISTORY_CONTRACTS for history in histories) or len(set(histories)) != len(histories)):
        raise ValueError('an explicit supported source history contract is required')
    if NO_GOAL_HISTORY in histories and manifest.get('policy_profile') != POLICY_PROFILE:
        raise ValueError('no-goal source history requires its explicit unconditional policy profile')
    sources, roles = manifest.get('source_sha256'), manifest.get('feature_sources')
    if not isinstance(sources, dict) or not sources or any(not isinstance(p, str) or not _sha_value(s) for p, s in sources.items()):
        raise ValueError('immutable source hashes required')
    if not isinstance(roles, dict) or not {'draft', 'source_oracle', 'model', 'context'} <= set(roles):
        raise ValueError('draft/source-oracle/model/context source roles required')
    if any(not isinstance(paths, list) or not paths or any(path not in sources for path in paths) for paths in roles.values()):
        raise ValueError('source roles escape frozen closure')
    context_names = {Path(name.replace('\\', '/')).name for name in roles['context']}
    if not {'student_context.h', 'student_context.cpp'} <= context_names:
        raise ValueError('the shared context header and builder must be pinned in the source closure')
    if not _sha_value(manifest.get('producer_binary_sha256')):
        raise ValueError('source producer binary hash required')
    refs = manifest.get('label_source_refs')
    if not isinstance(refs, list) or not refs or any(not isinstance(ref, dict) or ref.get('role') != 'labels_only' or not _sha_value(ref.get('sha256')) for ref in refs):
        raise ValueError('label-only source references required')
    if verify_files:
        if not isinstance(manifest.get('source_root'), str) or not isinstance(manifest.get('producer_binary_path'), str):
            raise ValueError('immutable source/binary paths required')
        root = Path(manifest['source_root']).resolve(strict=True)
        for relative, sha in sources.items():
            path = (root / relative).resolve(strict=True)
            if Path(relative).is_absolute() or not path.is_relative_to(root) or _hash(path) != sha:
                raise ValueError(f'changed source closure: {relative}')
        if _hash(manifest['producer_binary_path']) != manifest['producer_binary_sha256']:
            raise ValueError('changed source producer binary')
    return manifest


def _gameplay_raw_scope(coverage, raw):
    scope = coverage.get('emit_scope')
    if scope not in (FIRST_GAMEPLAY_SCOPE, ECONOMIC_SCOPE):
        raise ValueError('only the agreed five-action or inputs21..2256 economic scope is supported')
    economic = scope == ECONOMIC_SCOPE
    last = 2256 if economic else 30
    ordered = sorted(raw.values(), key=lambda event: event['origin']['input'])
    if [event['origin']['input'] for event in ordered] != list(range(21, last + 1)):
        raise ValueError('the bounded chronological raw scope must be retained exactly once')
    sequences = [event['origin']['sequence'] for event in ordered]
    if sequences != list(range(sequences[0], sequences[0] + len(ordered))):
        raise ValueError('raw source sequences must remain chronological and contiguous')
    policy, excluded = set(), set()
    unrecoverable = exogenous = outside = 0
    for event in ordered:
        d, label = event['origin'], validate_origin(event.get('label_origin'))
        key = d['sequence'], d['input']
        if ((d['actor'] < 0 and d['native_kind'] != 26) or d['phase'] != 10 or label['phase'] not in (10, 13)
                or any(d[field] != label[field] for field in ORIGIN_FIELDS if field != 'phase')
                or not isinstance(event.get('source_status'), str) or not event['source_status']
                or not isinstance(event.get('supervision_scope'), str) or not event['supervision_scope']):
            raise ValueError('raw gameplay event requires its after evidence, inverse status and supervision scope')
        if not economic and d['input'] < 26:
            if event['supervision_scope'] != 'personal_prefix_only' or d['actor'] == 6:
                raise ValueError('inputs21..25 are outside actor6 emission, retained as personal prefix only')
            outside += 1
            continue
        status, native = event['source_status'], d['native_kind']
        if status not in SOURCE_STATUSES:
            raise ValueError('in-scope source status must be a shared native inverse status')
        if economic:
            if native != 26 and _i32(event.get('command_index_before'), 'raw command_index_before') < 0:
                raise ValueError('native raw owner counter must be nonnegative')
            if native != 26 and _i32(event.get('owner_turn'), 'raw owner_turn') < 0:
                raise ValueError('native raw owner turn must be explicit and nonnegative')
            if native == 23:
                if status != 'ambiguous' or event['event_role'] != 'inverse_ambiguous_attempt':
                    raise ValueError('kind23 is only an explicit inverse-ambiguous attempt')
                unrecoverable += 1; excluded.add(key); continue
            if native == 26:
                if status != 'exogenous':
                    raise ValueError('kind26 is only an exogenous engine fuel boundary')
                exogenous += 1; excluded.add(key); continue
            if native not in NATIVE_COMMANDS or status != 'resolved':
                raise ValueError('unknown or unresolved nonexcluded inverse blocks the economic source slice')
        policy.add(key)
    for field, reason, count in (('unrecoverable_intent_inputs', 'kind23_inverse_ambiguous_intent', unrecoverable),
                                 ('exogenous_inputs', 'kind26_fuel_boundary', exogenous)):
        values = coverage.get(field, {} if not economic else None)
        if (not isinstance(values, dict) or set(values) - {reason}
                or any(_i32(value, field) < 0 for value in values.values()) or values.get(reason, 0) != count):
            raise ValueError(f'{field} must account for exactly the known raw boundary/ambiguous events')
    if economic:
        previous, boundaries = {}, {}
        for event in ordered:
            d = event['origin']; actor = d['actor']
            if d['native_kind'] == 26:
                boundaries[actor] = d['input']
                previous[actor] = event
                continue
            before = previous.get(actor)
            if (before is not None and before['origin']['native_kind'] == 23
                    and before['origin']['round'] == d['round']
                    and before['owner_turn'] == event['owner_turn']
                    and before['origin']['input'] > max(boundaries.get(actor, -1), boundaries.get(-1, -1))
                    and event['command_index_before'] != before['command_index_before'] + 1):
                raise ValueError('kind23 must advance the native attempt counter before the next observation in that owner')
            previous[actor] = event
    return economic, policy, excluded, outside, unrecoverable, exogenous


def qualified_coverage(coverage, records):
    """Count source events, decision units and represented stages separately."""
    if coverage.get('scope') != COVERAGE_SCOPE:
        raise ValueError('paired source-unit coverage scope required')
    events, units = coverage.get('raw_events'), coverage.get('units')
    if not isinstance(events, list) or not isinstance(units, list):
        raise ValueError('raw events and explicit decision units required')
    raw, expected, actual = {}, {}, {}
    for field in ('semantic_omissions', 'representation_omissions', 'history_omissions'):
        if not isinstance(coverage.get(field), dict):
            raise ValueError(f'explicit {field} counters required')
    gameplay = 'emit_scope' in coverage or any(unit.get('label_kind') == 'observed_engine_action' for unit in units)
    for event in events:
        origin = validate_origin(event.get('origin'))
        key = origin['sequence'], origin['input']
        if key in raw or not isinstance(event.get('event_role'), str) or not event['event_role']:
            raise ValueError('duplicate/missing raw event role')
        raw[key] = event
    if gameplay:
        economic, policy_raw, excluded_raw, outside, unrecoverable, exogenous = _gameplay_raw_scope(coverage, raw)
    for unit in units:
        validate_origin(unit.get('decision_origin')); validate_origin(unit.get('label_origin'))
        key = unit_key(unit)
        if key in expected:
            raise ValueError('duplicate source decision unit')
        d, label = unit['decision_origin'], unit['label_origin']
        if gameplay:
            command = _i32(unit.get('command'), 'command')
            if (d['actor'] < 0 or command not in NATIVE_COMMANDS.get(d['native_kind'], ())
                    or (not economic and (d['actor'] != 6 or FIRST_GAMEPLAY_ACTIONS.get(d['input']) != (d['native_kind'], command)))
                    or (d['sequence'], d['input']) not in policy_raw
                    or unit.get('label_kind') != 'observed_engine_action' or unit.get('history_type') != NO_GOAL_HISTORY
                    or unit.get('event_role') != ('observed_owner_end' if command == 0 else 'observed_attempt')
                    or unit.get('expected_stages') != expected_stages(command)):
                raise ValueError('observed gameplay unit must retain every command-derived stage in the agreed profile')
            event = raw.get((d['sequence'], d['input']))
            if (event is None or event['origin'] != d or event['label_origin'] != label
                    or event['event_role'] != unit['event_role']):
                raise ValueError('gameplay unit does not match its one retained raw source event')
        else:
            if unit.get('expected_stages') != ['family:6'] or unit.get('label_kind') != 'observed_draft_choice':
                raise ValueError('one draft pointer stage per paired source unit required')
            decision_event = raw.get((d['sequence'], d['input']))
            label_event = raw.get((label['sequence'], label['input']))
            if (decision_event is None or label_event is None or decision_event['origin'] != d or label_event['origin'] != label
                    or decision_event['event_role'] != 'draft_dispatch' or label_event['event_role'] != 'draft_resolution'):
                raise ValueError('both paired raw events require their distinct roles')
        expected[key] = unit
    for record in records:
        validate_record(record); key = unit_key(record.metadata)
        stage = record_stage(record)
        if key not in expected or stage not in expected[key]['expected_stages']:
            raise ValueError('extra CE or fresh decision at a resolution/outside-scope receipt')
        stages = actual.setdefault(key, {})
        if stage in stages:
            raise ValueError('duplicate CE stage at one source decision unit')
        if any(record.metadata[field] != expected[key][field] for field in ('event_role', 'label_kind', 'history_type')):
            raise ValueError('label/history differs from declared source unit')
        if gameplay:
            if (record.command != expected[key]['command'] or record.metadata['pairproof']['source_sha256'] != coverage.get('source_sha256')):
                raise ValueError('gameplay command/source hash differs from its native source unit')
            if stages:
                previous = next(iter(stages.values()))
                same = ('rule_mask', 'training_rule_mask', 'profile_mask', 'policy_profile', 'admitted_mask',
                        'history', 'facts', 'pairproof', 'qualification', 'won')
                if (record.state.tobytes() != previous.state.tobytes()
                        or record.context_payload.tobytes() != previous.context_payload.tobytes()
                        or record.context_phi.tobytes() != previous.context_phi.tobytes()
                        or any(record.metadata.get(field) != previous.metadata.get(field) for field in same)):
                    raise ValueError('root/family stages must share identical pre-input state bits, masks and causal metadata')
        stages[stage] = record
    complete = {key for key, stages in actual.items() if set(stages) == set(expected[key]['expected_stages'])}
    if complete != set(expected) and not coverage['representation_omissions']:
        raise ValueError('missing represented source stage was not reported')
    if gameplay:
        source_inputs = {(unit['decision_origin']['sequence'], unit['decision_origin']['input']) for unit in units}
        if source_inputs != policy_raw and not coverage['semantic_omissions']:
            raise ValueError('an omitted observed policy unit requires an explicit semantic omission')
        raw_by_input = {event['origin']['input']: event for event in raw.values()}
        actor_events = {}
        for event in sorted(raw.values(), key=lambda event: event['origin']['input']):
            actor_events.setdefault(event['origin']['actor'], []).append(event)
        owner_segments, owner_starts = {}, {}
        for key, stages in actual.items():
            record = next(iter(stages.values())); h = record.metadata['history']
            prefix = [event['origin']['input'] for event in actor_events[record.actor] if event['origin']['input'] <= record.input]
            if h['personal_prefix_inputs'] != prefix:
                raise ValueError('personal history must retain every observed source event for that actor')
            if economic:
                if 'owner_prefix_inputs' not in h:
                    raise ValueError('economic history requires a separate native owner prefix')
                owner = h['owner_prefix_inputs']
                for count, index in enumerate(owner):
                    event = raw_by_input.get(index)
                    if (event is None or event['origin']['actor'] != record.actor or event['origin']['round'] != record.round
                            or event['origin']['native_kind'] == 26 or event['command_index_before'] != count
                            or event['owner_turn'] != h['owner_turn']):
                        raise ValueError('owner history/counter disagrees with native raw owner observations')
                current = raw_by_input[record.input]
                if h['command_index_before'] != current['command_index_before'] or h['owner_turn'] != current['owner_turn']:
                    raise ValueError('record attempt counter disagrees with its native raw source fact')
                segment = record.actor, record.round, h['owner_turn']
                start = record.actor, owner[0]
                if owner_segments.setdefault(segment, owner[0]) != owner[0] or owner_starts.setdefault(start, segment) != segment:
                    raise ValueError('native owner turn and owner-prefix start must name one consistent segment')
                for event in actor_events[record.actor]:
                    d = event['origin']
                    if (d['actor'] == record.actor and d['round'] == record.round and owner[0] <= d['input'] <= record.input
                            and d['native_kind'] == 23 and d['input'] not in owner):
                        raise ValueError('an inverse-ambiguous attempt was omitted from owner history')
    elif set(raw) != {(origin[0], origin[1]) for key in expected for origin in key[:2]}:
        raise ValueError('draft contract scope retains exactly its explicitly paired events')
    semantic = bool(expected) and not coverage['semantic_omissions']
    if gameplay:
        semantic &= all(raw[key]['source_status'] == 'resolved' for key in policy_raw)
    representation = complete == set(expected) and not coverage['representation_omissions']
    if len({record.context_n for record in records}) > 1:
        raise ValueError('one source map must retain the same full static group count at every decision')
    history = not coverage['history_omissions']
    for record in records:
        semantic &= record.qualification['source_semantic_pass']
        representation &= record.qualification['representation_pass']
        if gameplay:
            representation &= record.metadata['facts']['self_buffs_known']
        history &= record.qualification['history_contract_pass']
    return {'source_eligible': bool(semantic), 'representation_eligible': bool(representation), 'history_eligible': bool(history),
        'raw_retained_events': len(raw), 'decision_units': len(expected), 'represented_units': len(complete),
        'outside_emit_scope_events': outside if gameplay else 0, 'ce_records': len(records), 'optimizer_allowed': False,
        **({'eligible_policy_units': len(policy_raw), 'unrecoverable_intent_events': unrecoverable,
            'exogenous_events': exogenous} if gameplay and economic else {})}


def pack_records(records, out, *, manifest, coverage, allow_unqualified=False):
    records = [validate_record(record) for record in records]
    if not records:
        raise ValueError('empty contract frame')
    manifest = verify_manifest(manifest, verify_files=True)
    if any(record.label_kind not in ('observed_draft_choice', 'observed_engine_action') for record in records):
        raise ValueError('diagnostic/historical hindsight labels cannot enter observed source CE')
    if any(record.history_type not in manifest['history_contracts'] or record.qualification['purpose'] != manifest['purpose'] for record in records):
        raise ValueError('record history/purpose is not declared by its source manifest')
    source_hash = coverage.get('source_sha256')
    if source_hash not in {ref['sha256'] for ref in manifest['label_source_refs']}:
        raise ValueError('source frame lacks label-only oracle provenance')
    flags = qualified_coverage(coverage, records)
    if not all(flags[key] for key in ('source_eligible', 'representation_eligible', 'history_eligible')) and not allow_unqualified:
        raise ValueError('source semantics/representation/history is incomplete')
    arrays = {'schema': np.array(PACK_SCHEMA, dtype=np.int32), 'feature_schema': np.array(FEATURE_SCHEMA, dtype=np.int32),
        'manifest_sha256': np.array(manifest_sha256(manifest)), 'source_sha256': np.array(source_hash),
        'coverage_json': np.array(_json(coverage)), 'qualification_json': np.array(_json(flags)),
        'record_count': np.array(len(records), dtype=np.int64), 'training_qualified': np.array(False)}
    arrays['context_offsets'] = np.concatenate(([0], np.cumsum([row.context_n for row in records], dtype=np.int64)))
    arrays['context_payload'] = np.concatenate([row.context_payload for row in records])
    arrays['context_phi'] = np.concatenate([row.context_phi for row in records])
    root = [(index, row) for index, row in enumerate(records) if row.kind == COMMAND]
    if root:
        arrays['command_state'] = np.stack([row.state for _, row in root])
        arrays['command_mask'] = np.array([row.mask for _, row in root], dtype=np.uint32)
        arrays['command_rule_mask'] = np.array([row.rule_mask for _, row in root], dtype=np.uint32)
        arrays['command_label'] = np.array([row.label for _, row in root], dtype=np.int32)
        arrays['command_metadata'] = np.array([_json(row.metadata) for _, row in root])
        arrays['command_record_index'] = np.array([index for index, _ in root], dtype=np.int64)
    for head, name in HEAD_NAMES.items():
        rows = [(index, row) for index, row in enumerate(records) if row.head == head]
        if not rows:
            continue
        arrays[f'{name}_state'] = np.stack([row.state for _, row in rows])
        arrays[f'{name}_phi'] = np.concatenate([row.phi for _, row in rows])
        arrays[f'{name}_payload'] = np.concatenate([row.payload for _, row in rows])
        arrays[f'{name}_offsets'] = np.concatenate(([0], np.cumsum([row.n for _, row in rows], dtype=np.int64)))
        arrays[f'{name}_metadata'] = np.array([_json(row.metadata) for _, row in rows])
        arrays[f'{name}_record_index'] = np.array([index for index, _ in rows], dtype=np.int64)
        arrays[f'{name}_label'] = np.array([row.label for _, row in rows], dtype=np.int32)
    out = Path(out); out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(f'{out.stem}.{uuid.uuid4().hex}.tmp.npz'); np.savez_compressed(tmp, **arrays); tmp.replace(out)
    audit = {'data_abi': 'main.zero.student.data.v3', 'manifest_sha256': manifest_sha256(manifest),
        'npz_sha256': _hash(out), 'source_sha256': source_hash, 'coverage': coverage, 'qualification': flags,
        'purpose': manifest['purpose'], 'deployment_proof': None}
    sidecar = out.with_name(out.name + '.data.json'); tmp = sidecar.with_name(f'{sidecar.name}.{uuid.uuid4().hex}.tmp')
    tmp.write_text(json.dumps(audit, indent=2, allow_nan=False) + '\n', encoding='utf-8'); tmp.replace(sidecar)
    return {'path': str(out), **flags}


def records_from_shard(shard):
    result = {}
    record_count = int(np.asarray(shard['record_count']).item())
    context_offsets, context_ids, context_phi = (np.asarray(shard[f'context_{key}']) for key in ('offsets', 'payload', 'phi'))
    if (record_count < 1 or context_offsets.dtype != np.int64 or context_offsets.shape != (record_count + 1,)
            or context_ids.dtype != np.int32 or context_ids.ndim != 1 or context_phi.dtype != np.float32
            or context_phi.shape != (len(context_ids), CONTEXT_PHI) or context_offsets[0] != 0
            or context_offsets[-1] != len(context_ids) or np.any(np.diff(context_offsets) < 0)):
        raise ValueError('invalid lossless per-record static camp context arrays')

    def context_for(index):
        if not 0 <= index < record_count:
            raise ValueError('source context record index is out of range')
        begin, end = int(context_offsets[index]), int(context_offsets[index + 1])
        return context_ids[begin:end], context_phi[begin:end]

    if 'command_state' in shard:
        state, masks, raw_masks, labels, metadata, order = (np.asarray(shard[f'command_{key}']) for key in ('state', 'mask', 'rule_mask', 'label', 'metadata', 'record_index'))
        count = len(state)
        if (state.shape != (count, STATE) or masks.dtype != np.uint32 or masks.shape != (count,)
                or raw_masks.dtype != np.uint32 or raw_masks.shape != (count,)
                or labels.dtype != np.int32 or labels.shape != (count,) or metadata.shape != (count,)
                or order.dtype != np.int64 or order.shape != (count,)):
            raise ValueError('invalid lossless source command arrays')
        for index in range(count):
            key = int(order[index])
            row = validate_record(Record(json.loads(str(metadata[index])), state[index],
                                         np.empty((0, 5), dtype=np.int32), np.empty((0, RAW_PHI), dtype=np.float32),
                                         *context_for(key)))
            if (row.kind != COMMAND or row.label != int(labels[index]) or row.mask != int(masks[index])
                    or row.rule_mask != int(raw_masks[index]) or key in result):
                raise ValueError('source command metadata/order/mask mismatch')
            result[key] = row
    for head, name in HEAD_NAMES.items():
        if f'{name}_state' not in shard:
            continue
        state, phi, payload, offsets = (np.asarray(shard[f'{name}_{key}']) for key in ('state', 'phi', 'payload', 'offsets'))
        metadata, order, labels = (np.asarray(shard[f'{name}_{key}']) for key in ('metadata', 'record_index', 'label'))
        count = len(state)
        if (state.shape != (count, STATE) or offsets.dtype != np.int64 or offsets.shape != (count + 1,)
                or offsets[0] != 0 or offsets[-1] != len(phi) or len(phi) != len(payload) or np.any(np.diff(offsets) < 1)
                or metadata.shape != (count,) or order.dtype != np.int64 or order.shape != (count,)
                or labels.dtype != np.int32 or labels.shape != (count,)):
            raise ValueError('invalid lossless paired ragged arrays')
        for index in range(count):
            m = json.loads(str(metadata[index])); begin, end = int(offsets[index]), int(offsets[index + 1])
            key = int(order[index])
            row = validate_record(Record(m, state[index], payload[begin:end], phi[begin:end], *context_for(key)))
            if row.head != head or row.n != end - begin or row.label != int(labels[index]) or key in result:
                raise ValueError('metadata/row mismatch or duplicate CE')
            result[key] = row
    if set(result) != set(range(record_count)):
        raise ValueError('source frame order/count lost')
    return [result[index] for index in range(record_count)]


def game_shards(pack_dir):
    return sorted(path for path in (Path(pack_dir) / 'games').glob('*.npz') if not path.name.endswith('.tmp.npz'))


def source_provenance(pack_dir, files, shards):
    directory = Path(pack_dir).resolve(); manifest_path = directory / 'manifest.json'
    manifest = verify_manifest(manifest_path, verify_files=True); fingerprint = manifest_sha256(manifest)
    if not files or len(files) != len(shards):
        raise ValueError('explicit source frame shards required')
    refs = []
    for file, shard in zip(files, shards):
        file = Path(file).resolve()
        for key, value in [('schema', PACK_SCHEMA), ('feature_schema', FEATURE_SCHEMA), ('manifest_sha256', fingerprint)]:
            if np.asarray(shard.get(key)).shape != () or np.asarray(shard.get(key)).item() != value:
                raise ValueError('historical or mixed-feature shard')
        coverage = json.loads(str(np.asarray(shard['coverage_json']).item()))
        records = records_from_shard(shard)
        flags = qualified_coverage(coverage, records)
        if any(record.history_type not in manifest['history_contracts'] or record.qualification['purpose'] != manifest['purpose'] for record in records):
            raise ValueError('source record history/purpose differs from its manifest')
        source_hash = coverage.get('source_sha256')
        if (source_hash not in {ref['sha256'] for ref in manifest['label_source_refs']}
                or np.asarray(shard.get('source_sha256')).shape != () or np.asarray(shard.get('source_sha256')).item() != source_hash
                or np.asarray(shard.get('qualification_json')).shape != ()
                or json.loads(str(np.asarray(shard['qualification_json']).item())) != flags
                or np.asarray(shard.get('training_qualified')).shape != () or np.asarray(shard['training_qualified']).item() is not False):
            raise ValueError('source hash/qualification does not match the source-only contract')
        sidecar = file.with_name(file.name + '.data.json'); audit = json.loads(sidecar.read_text(encoding='utf-8'))
        if (audit.get('data_abi') != 'main.zero.student.data.v3' or audit.get('manifest_sha256') != fingerprint
                or audit.get('npz_sha256') != _hash(file) or audit.get('coverage') != coverage or audit.get('qualification') != flags
                or audit.get('purpose') != manifest['purpose'] or audit.get('source_sha256') != source_hash
                or audit.get('deployment_proof') is not None):
            raise ValueError('source/data sidecar mismatch')
        refs.append({'path': str(file), 'sha256': _hash(file), 'data_sidecar_path': str(sidecar),
            'data_sidecar_sha256': _hash(sidecar), 'qualification': flags, 'coverage': coverage})
    return {'model_abi': MODEL_ABI, 'feature_abi': FEATURE_ABI, 'pack_schema': PACK_SCHEMA,
        'policy_profile': manifest.get('policy_profile'),
        'feature_manifest_path': str(manifest_path), 'feature_manifest_file_sha256': _hash(manifest_path),
        'feature_manifest_sha256': fingerprint, 'feature_source_sha256': manifest['source_sha256'],
        'dataset_refs': refs, 'label_source_refs': manifest['label_source_refs'], 'purpose': manifest['purpose'],
        'source_eligible': all(ref['qualification']['source_eligible'] for ref in refs),
        'representation_eligible': all(ref['qualification']['representation_eligible'] for ref in refs),
        'history_eligible': all(ref['qualification']['history_eligible'] for ref in refs), 'optimizer_allowed': False}


def verify_deployment_receipt(receipt, *, manifest=None):
    """Bind prior feature2 PRIORITY->MOVE evidence as the approved attention-fit authorization.

    Attention cost remains pending activity proof. This does not claim feature3 deployment,
    an expert trajectory, a whole-owner budget, or a limit on source families being learned.
    """
    expected_sha = 'f3652c3f84bbe8e0ea93438deca007f2f435a4a73f06d719eb9b7c5925ee0662'
    if receipt is None:
        raise ValueError('the approved prior measured-slice receipt is required before optimization')
    binding = receipt if isinstance(receipt, dict) else {'path': str(receipt)}
    path = Path(binding['path']).resolve()
    digest = _hash(path)
    if digest != expected_sha or binding.get('sha256', digest) != digest:
        raise ValueError('deployment receipt is not the approved immutable measured-scope summary')
    summary = json.loads(path.read_text(encoding='utf-8'))
    if (summary.get('schema') != 'zero-priority-local-boundary/v1' or summary.get('pass') is not True
            or summary.get('featureSchema') != 2 or summary.get('configs') != {'5': 1, '6': 1, '7': 1}):
        raise ValueError('deployment evidence has the wrong feature/config contract')

    def artifact(ref):
        if not isinstance(ref, dict) or not isinstance(ref.get('path'), str) or not _sha_value(ref.get('sha256')):
            raise ValueError('deployment evidence reference is incomplete')
        target = Path(ref['path']).resolve()
        if _hash(target) != ref['sha256']:
            raise ValueError('deployment evidence artifact changed')
        return {'path': str(target), 'sha256': ref['sha256']}, json.loads(target.read_text(encoding='utf-8'))

    lifecycle = summary['lifecycle']
    measured_ref, measured = artifact(lifecycle['artifact'])
    if (measured.get('schema') != 'zero-student-lifecycle/v1' or measured.get('pass') is not True
            or measured.get('lifecyclePriorityMove') is not True or measured.get('outcome', {}).get('pass') is not True
            or measured['outcome'].get('priorityMoveSameInterval') is not True
            or lifecycle.get('sameInterval') is not True or lifecycle.get('normalYield') is not True
            or lifecycle.get('faults') != 0 or lifecycle.get('errorLog') != ''
            or [entry.get('type') for entry in lifecycle.get('dispatches', [])] != [14, 1]
            or any(entry.get('receipt') != 1 for entry in lifecycle['dispatches'])):
        raise ValueError('deployment evidence lacks the successful useful primitive and continuation')
    calibration = summary['calibration']
    profile_ref, profile = artifact(calibration['newProfile'])
    enabled = (1 << 1) | (1 << 14)
    margin, reserve = calibration.get('margin'), calibration.get('cleanupReserve')
    ready = [entry for entry in lifecycle['intervals'] if entry.get('usefulPrimitive') is True]
    if (profile.get('knownMask') != enabled or profile.get('dispatchKnownMask') != enabled
            or profile.get('refreshKnownMask') != 1 << 14 or profile.get('rootBound', 0) <= 0
            or not isinstance(margin, (int, float)) or margin < 1.25 or reserve != 8000
            or lifecycle.get('ordinaryAllowance') != 100000 or not ready
            or any(entry.get('budget') != 100000 or entry.get('reserve') is not True or entry.get('failed') != 0
                   or entry.get('reason') != 0 or entry.get('used', 100000) > 100000 - reserve for entry in ready)
            or lifecycle.get('firstReadyRemaining', 0) < reserve):
        raise ValueError('deployment evidence lacks the measured profile margin and cleanup reserve')
    parity_refs = []
    for parity in summary.get('parity', []):
        ref, actual = artifact(parity['artifact'])
        if (parity.get('pass') is not True or actual.get('pass') is not True
                or parity.get('localWords') != parity.get('fullWords')
                or parity.get('firstDifference') != parity.get('localWords')):
            raise ValueError('deployment local/full fact-history parity did not pass')
        parity_refs.append(ref)
    if len(parity_refs) != 2:
        raise ValueError('deployment proof must retain its two measured parity states')
    feature_hashes, target_hashes, pending_hashes = {}, {}, {}
    if manifest is not None:
        # Pin the new immutable feature/model closure separately. The prior receipt intentionally
        # predates attention; changed/new model/context bytes are pending fuel/activity evidence.
        measured_sources = {name.replace('\\', '/'): value for name, value in measured['wholeSourceManifest'].items()}
        for name, value in manifest['source_sha256'].items():
            normal = name.replace('\\', '/')
            if ('/src/core/' in normal and 'student_decision' not in normal and 'student_probe' not in normal):
                target_hashes[name] = value
                if measured_sources.get(normal) == value:
                    feature_hashes[name] = value
                else:
                    pending_hashes[name] = value
    return {'path': str(path), 'sha256': digest, 'schema': summary['schema'], 'feature_schema': 2,
        'measured_feature_schema': 2, 'target_feature_schema': FEATURE_SCHEMA,
        'policy_profile': 'unconditional_no_goal_v1', 'measured_policy_profile': 'unconditional_no_goal_v1',
        'target_policy_profile': POLICY_PROFILE,
        'configs': {'5': 1, '6': 1, '7': 1}, 'attention_cost_pending': True, 'deployment_feature3_proven': False,
        'scope': {'enabled_families': [1, 14], 'enabled_mask': enabled,
                  'proof': 'prior feature2 diagnostic PRIORITY9 then useful MOVE in one production interval',
                  'ordinary_allowance': 100000, 'margin': margin, 'cleanup_reserve': reserve,
                  'claim': 'attention fit authorized; attention cost pending until activity; no feature3 deployment claim'},
        'actual_receipt': measured_ref, 'profile': profile_ref, 'parity_artifacts': parity_refs,
        'feature_source_sha256': feature_hashes, 'target_feature_source_sha256': target_hashes,
        'unmeasured_attention_source_sha256': pending_hashes}


def checkpoint_provenance(pack_dir, files, shards, *, deployment_receipt=None):
    result = source_provenance(pack_dir, files, shards)
    if result['purpose'] == 'contract_test':
        raise ValueError('one genuine draft frame is a contract test, not training permission')
    if not all(result[key] is True for key in ('source_eligible', 'representation_eligible', 'history_eligible')):
        raise ValueError('BC source semantic/representation/history eligibility is incomplete')
    manifest = verify_manifest(result['feature_manifest_path'], verify_files=True)
    if manifest.get('synthetic_only') is True or manifest.get('policy_profile') != POLICY_PROFILE:
        raise ValueError('BC requires the genuine unconditional no-goal source profile')
    proof = verify_deployment_receipt(deployment_receipt, manifest=manifest)
    return {**result, 'deployment_receipt': proof, 'training_qualified': True, 'optimizer_allowed': True,
            'attention_cost_pending': True, 'deployment_feature3_proven': False}
