"""Lossless ZSTUD001/ZSTUD002 transport for production-selector records.

No function in this module constructs game features, candidate sets or source labels. Raw float32
bytes are copied unchanged. The caller must reconcile trace frames with the final selector status
and qualify the source/history/fuel gates before a sink is eligible for training.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from numbers import Integral
from pathlib import Path
import struct
from typing import Iterable, Mapping, Sequence

MAGIC = b'ZSTUD001'
HEADER = struct.Struct('<12i')
PAYLOAD = struct.Struct('<5i')
STATE_COUNT, PHI_COUNT, COMMANDS = 48, 24, 15
STATE_BYTES, ROW_BYTES = STATE_COUNT * 4, PAYLOAD.size + PHI_COUNT * 4
COMMAND, POINTER, SCALAR = 0, 1, 2
FIELDS = ('kind', 'head', 'command', 'n', 'label', 'mask', 'sequence', 'input',
          'actor', 'team', 'round', 'won')
HEAD_COMMANDS = {0: (9,), 1: (1,), 2: (2,), 3: (3,), 4: (4,), 5: (5,), 6: (-1,),
                 10: (0, 6, 7, 8, 10, 11, 12, 13), 11: (14,), 12: (5,)}


@dataclass(frozen=True)
class Origin:
    sequence: int
    input: int
    actor: int
    team: int
    round: int
    won: int = -1


@dataclass(frozen=True)
class Frame:
    """Validated record view. All slices are the exact original little-endian bytes."""
    header: tuple[int, ...]
    state: bytes
    rows: bytes
    encoded: bytes

    def fields(self) -> dict[str, int]:
        return dict(zip(FIELDS, self.header))


def _i32(value, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, Integral) or not -(1 << 31) <= value < (1 << 31):
        raise ValueError(f'{name}: expected signed int32')
    return int(value)


def _float_bytes(values: bytes | bytearray | memoryview | Iterable[float], count: int,
                 name: str) -> bytes:
    if isinstance(values, (bytes, bytearray, memoryview)):
        result = bytes(values)
        if len(result) != count * 4:
            raise ValueError(f'{name}: expected {count} float32 values')
    else:
        supplied = tuple(values)
        if len(supplied) != count:
            raise ValueError(f'{name}: expected {count} float32 values')
        try:
            result = struct.pack(f'<{count}f', *supplied)
        except (OverflowError, TypeError, struct.error) as error:
            raise ValueError(f'{name}: invalid float32 values') from error
    # Checking exponent bits avoids conversion/re-encoding and preserves negative zero/subnormals.
    for (bits,) in struct.iter_unpack('<I', result):
        if bits & 0x7f800000 == 0x7f800000:
            raise ValueError(f'{name}: nonfinite float32')
    return result


def _validate_header(header: Sequence[int]) -> tuple[int, ...]:
    if len(header) != len(FIELDS):
        raise ValueError('record header must contain 12 signed-int32 fields')
    values = tuple(_i32(value, field) for field, value in zip(FIELDS, header))
    h = dict(zip(FIELDS, values))
    if h['input'] < 1 or h['team'] not in (0, 1) or h['round'] < 0 or h['won'] not in (-1, 0, 1):
        raise ValueError('invalid source origin')
    if not 0 <= h['mask'] < 1 << COMMANDS:
        raise ValueError('command mask has unknown bits')
    if h['kind'] == COMMAND:
        if (h['head'] != -1 or h['n'] != 0 or h['command'] != h['label']
                or not 0 <= h['label'] < COMMANDS or not h['mask'] & (1 << h['label'])):
            raise ValueError('root label must be present in admitted mask; root has no rows')
    elif h['kind'] in (POINTER, SCALAR):
        if h['head'] not in HEAD_COMMANDS or h['n'] < 1 or h['command'] not in HEAD_COMMANDS[h['head']]:
            raise ValueError('selected argument family/command/count mismatch')
        if h['head'] == 6 and (h['actor'] != -1 or h['sequence'] < 0):
            raise ValueError('draft records require actor=-1 and a real source input')
        if h['kind'] == SCALAR:
            if h['head'] not in (11, 12) or h['n'] != 1 or (h['head'] == 12 and h['label'] < 1):
                raise ValueError('scalar record requires one row and a valid source int32 target')
        elif h['head'] in (11, 12) or not 0 <= h['label'] < h['n']:
            raise ValueError('pointer label must name a complete candidate row')
    else:
        raise ValueError('unknown record kind')
    return values


def validate_frame(data: bytes | bytearray | memoryview, *,
                   scalar_bounds: tuple[int, int] | None = None) -> Frame:
    """Validate exactly one frame, without MAGIC, preserving every supplied feature bit.

    scalar_bounds is optional source/production audit data; the transport never infers affordability
    or any other game constraint. Pointer labels are checked against the supplied full row count.
    """
    encoded = bytes(data)
    if len(encoded) < HEADER.size + STATE_BYTES:
        raise ValueError('truncated record header/state')
    header = _validate_header(HEADER.unpack_from(encoded))
    n = header[3]
    if len(encoded) != HEADER.size + STATE_BYTES + n * ROW_BYTES:
        raise ValueError('record row count does not match exact byte length')
    state = _float_bytes(encoded[HEADER.size:HEADER.size + STATE_BYTES], STATE_COUNT, 'state')
    rows = encoded[HEADER.size + STATE_BYTES:]
    for index in range(n):
        at = index * ROW_BYTES
        kind, _target, count, x, y = PAYLOAD.unpack_from(rows, at)
        if not 0 <= kind <= 255 or count < 1 or not -32768 <= x <= 32767 or not -32768 <= y <= 32767:
            raise ValueError('executable payload exceeds Cand ABI')
        _float_bytes(rows[at + PAYLOAD.size:at + ROW_BYTES], PHI_COUNT, f'phi[{index}]')
    if scalar_bounds is not None:
        if header[0] != SCALAR or len(scalar_bounds) != 2:
            raise ValueError('scalar bounds require a scalar record and explicit lower/upper values')
        lower, upper = (_i32(value, 'scalar bound') for value in scalar_bounds)
        if lower > upper or not lower <= header[4] <= upper:
            raise ValueError('scalar source target is outside supplied production bounds')
    return Frame(header, state, rows, encoded)


def encode_record(*, kind: int, head: int, command: int, label: int, mask: int,
                  origin: Origin, state, payload: Iterable[Sequence[int]] = (),
                  phi: Iterable[Iterable[float] | bytes] = (),
                  scalar_bounds: tuple[int, int] | None = None) -> bytes:
    """Frame explicit production arrays. Use float32 bytes to avoid any host numeric conversion."""
    payload_rows, phi_rows = tuple(payload), tuple(phi)
    if len(payload_rows) != len(phi_rows):
        raise ValueError('payload and phi row counts differ')
    n = len(payload_rows)
    header = _validate_header((kind, head, command, n, label, mask, origin.sequence, origin.input,
                               origin.actor, origin.team, origin.round, origin.won))
    pieces = [HEADER.pack(*header), _float_bytes(state, STATE_COUNT, 'state')]
    for index, (identity, facts) in enumerate(zip(payload_rows, phi_rows)):
        if len(identity) != 5:
            raise ValueError('each executable payload requires kind,target,count,x,y')
        pieces.append(PAYLOAD.pack(*(_i32(value, f'payload[{index}]') for value in identity)))
        pieces.append(_float_bytes(facts, PHI_COUNT, f'phi[{index}]'))
    return validate_frame(b''.join(pieces), scalar_bounds=scalar_bounds).encoded


def iter_frames(data: bytes | bytearray | memoryview, *, includes_magic: bool = False):
    """Validate a native bundle and yield frames in exact supplied order; counts have no fixed cap."""
    encoded = bytes(data)
    at = 0
    if includes_magic:
        if not encoded.startswith(MAGIC):
            raise ValueError('native bundle requires ZSTUD001 magic')
        at = len(MAGIC)
    while at < len(encoded):
        if len(encoded) - at < HEADER.size + STATE_BYTES:
            raise ValueError('truncated native bundle')
        header = HEADER.unpack_from(encoded, at)
        n = header[3]
        if n < 0:
            raise ValueError('negative native candidate count')
        end = at + HEADER.size + STATE_BYTES + n * ROW_BYTES
        if end > len(encoded):
            raise ValueError('native frame exceeds remaining bytes')
        yield validate_frame(encoded[at:end])
        at = end


def empty_coverage(*, source_inputs: Sequence[Mapping] = ()) -> dict:
    """An explicitly unqualified audit skeleton. The producer supplies all facts/counts/gate results."""
    return {
        'scope': 'production_student_eligible_policy',
        'production_feature_parity_pass': False, 'history_parity_pass': False,
        'actual_caller_100k_pass': False, 'source_facts_complete': False,
        'raw_retained_inputs': 0, 'eligible_policy_inputs': 0, 'eligible_policy_labelled_inputs': 0,
        'nonpolicy_inputs': {'kind7_carrier': 0, 'kind26_fuel_boundary': 0},
        'unrecoverable_intent_inputs': {'kind23_inverse_ambiguous_intent': 0},
        'missing_selected_stages': {}, 'candidate_omissions': {}, 'unknown_commands': {},
        'source_fact_gaps': {}, 'source_inputs': [dict(event) for event in source_inputs],
        'label_mode': 'diagnostic_control', 'internal_annotations': [],
        'missing_internal_annotations': {},
    }


class SinkWriter:
    """Append validated native frames to a new sink, retaining explicit caller provenance.

    Existing files are never overwritten. append_native validates the entire incoming bundle before
    writing any of it. A final selector failure must be excluded by the caller before this method.
    The audit is transport evidence, not a training qualification or a source-label inference.
    """
    def __init__(self, path: str | Path, *, provenance: Mapping):
        if not isinstance(provenance, Mapping) or not provenance:
            raise ValueError('sink needs explicit production provenance')
        self.path = Path(path)
        self.provenance = dict(provenance)
        self._output = self.path.open('xb')
        self._output.write(MAGIC)
        self._offset = len(MAGIC)
        self._count = 0
        self._appends = []
        self._closed = False

    def append_native(self, data, *, provenance: Mapping, includes_magic: bool = False) -> int:
        if self._closed:
            raise ValueError('sink is closed')
        if not isinstance(provenance, Mapping) or not provenance:
            raise ValueError('each native append needs explicit source/final-status provenance')
        frames = list(iter_frames(data, includes_magic=includes_magic))
        if not frames:
            raise ValueError('cannot append an empty native bundle')
        block = b''.join(frame.encoded for frame in frames)
        self._output.write(block)
        self._appends.append({'offset': self._offset, 'bytes': len(block), 'records': len(frames),
                              'sha256': hashlib.sha256(block).hexdigest(),
                              'origins': [frame.fields() for frame in frames],
                              'provenance': dict(provenance)})
        self._offset += len(block)
        self._count += len(frames)
        return len(frames)

    def close(self, audit_path: str | Path | None = None) -> dict:
        if not self._closed:
            self._output.close()
            self._closed = True
        audit = {'schema': 'zero-student-sink-transport/v1', 'sink': str(self.path.resolve()),
                 'sha256': hashlib.sha256(self.path.read_bytes()).hexdigest(), 'bytes': self._offset,
                 'records': self._count, 'provenance': self.provenance, 'appends': self._appends,
                 'coverage': empty_coverage()}
        if audit_path is not None:
            with Path(audit_path).open('x', encoding='utf-8', newline='\n') as output:
                json.dump(audit, output, indent=2, allow_nan=False)
                output.write('\n')
        return audit

    def __enter__(self):
        return self

    def __exit__(self, _type, _value, _traceback):
        self.close()


# Version 2 retains separate decision and observed-label origins. Version 1 APIs above remain
# historical and intentionally unchanged; a v2 frame cannot be passed to a v1 writer or validator.
MAGIC_V2 = b'ZSTUD002'
METADATA_LENGTH_V2 = struct.Struct('<I')
REQUIRED_METADATA_V2 = frozenset({
    'kind', 'head', 'command', 'n', 'label', 'feature_schema', 'rule_mask', 'admitted_mask',
    'decision_origin', 'label_origin', 'event_role', 'label_kind', 'history_type',
    'qualification', 'won', 'pairproof',
})


@dataclass(frozen=True)
class FrameV2:
    """One v2 frame without stream magic; feature and payload slices preserve their raw bytes."""
    metadata: dict
    metadata_bytes: bytes
    state: bytes
    rows: bytes
    encoded: bytes


def _canonical_json_v2(value) -> bytes:
    try:
        return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False,
                          allow_nan=False).encode('utf-8')
    except (TypeError, ValueError, UnicodeEncodeError) as error:
        raise ValueError('v2 metadata must be finite canonical UTF-8 JSON') from error


def _unique_json_object_v2(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate v2 metadata key: {key}')
        result[key] = value
    return result


def _reject_constant_v2(value):
    raise ValueError(f'nonfinite v2 JSON constant: {value}')


def _origin_v2(origin, name):
    if not isinstance(origin, dict):
        raise ValueError(f'{name}: expected an explicit native origin object')
    # Origin vocabulary is carried by the producer. This transport checks supplied integer facts;
    # it never converts a label origin to a decision origin or infers one origin from another.
    required = ('sequence', 'input', 'actor', 'team', 'round', 'phase', 'native_kind')
    if not all(field in origin for field in required):
        raise ValueError(f'{name}: explicit sequence/input/actor/team/round/phase/native_kind required')
    for field in required:
        _i32(origin[field], f'{name}.{field}')
    if origin['sequence'] < 0 or origin['input'] < 1 or origin['round'] < 0:
        raise ValueError(f'{name}: invalid native sequence/input/round')
    if origin['team'] not in (0, 1):
        raise ValueError(f'{name}.team: expected BEFORE acting team 0 or 1')


def _validate_metadata_v2(metadata) -> dict:
    if not isinstance(metadata, dict) or not REQUIRED_METADATA_V2 <= metadata.keys():
        raise ValueError('v2 metadata is missing required fields')
    for field in ('kind', 'head', 'command', 'n', 'label', 'feature_schema', 'won'):
        _i32(metadata[field], field)
    if metadata['feature_schema'] != 2 or metadata['won'] not in (-1, 0, 1):
        raise ValueError('v2 requires feature_schema=2 and an explicit result value')
    for field in ('rule_mask', 'admitted_mask'):
        value = metadata[field]
        if value is not None and not 0 <= _i32(value, field) < 1 << COMMANDS:
            raise ValueError(f'{field}: unknown command mask bits')
    _origin_v2(metadata['decision_origin'], 'decision_origin')
    _origin_v2(metadata['label_origin'], 'label_origin')
    for field in ('event_role', 'label_kind', 'history_type'):
        if not isinstance(metadata[field], str) or not metadata[field]:
            raise ValueError(f'{field}: expected explicit nonempty provenance vocabulary')
    if not isinstance(metadata['qualification'], dict):
        raise ValueError('qualification must be a producer-supplied object')
    qualification = metadata['qualification']
    for field in ('source_semantic_pass', 'representation_pass', 'history_contract_pass',
                  'deployment_slice_pass', 'optimizer_allowed'):
        if not isinstance(qualification.get(field), bool):
            raise ValueError(f'qualification.{field}: explicit producer boolean required')
    if not isinstance(qualification.get('purpose'), str) or not qualification['purpose']:
        raise ValueError('qualification.purpose: explicit nonempty producer purpose required')
    if not isinstance(metadata['pairproof'], dict):
        raise ValueError('pairproof must be a producer-supplied object')
    kind, head, command, n, label = (metadata[field] for field in ('kind', 'head', 'command', 'n', 'label'))
    if kind == COMMAND:
        if head != -1 or n != 0 or command != label or not 0 <= label < COMMANDS:
            raise ValueError('v2 root requires direct command label and no candidate rows')
        if metadata['admitted_mask'] is None or not metadata['admitted_mask'] & (1 << label):
            raise ValueError('v2 root label is absent from the actual admitted mask')
    elif kind in (POINTER, SCALAR):
        if head not in HEAD_COMMANDS or n < 1 or command not in HEAD_COMMANDS[head]:
            raise ValueError('v2 selected family/command/count mismatch')
        if kind == SCALAR:
            if head not in (11, 12) or n != 1 or (head == 12 and label < 1):
                raise ValueError('v2 scalar requires one row and a valid signed source target')
        elif head in (11, 12) or not 0 <= label < n:
            raise ValueError('v2 pointer label must name a complete candidate row')
        if head == 6 and any(metadata[field]['actor'] != -1 for field in ('decision_origin', 'label_origin')):
            raise ValueError('v2 draft origins require native actor=-1')
        if head == 6:
            proof = metadata['pairproof']
            for field in ('verified', 'phase_checked'):
                if not isinstance(proof.get(field), bool):
                    raise ValueError(f'pairproof.{field}: explicit producer boolean required')
            for field in ('dispatch_input', 'resolution_input', 'team', 'round', 'resolved_raw_pick'):
                if field not in proof:
                    raise ValueError(f'pairproof.{field}: explicit native integer required')
                _i32(proof[field], f'pairproof.{field}')
            for field in ('dispatch_api', 'resolution_api'):
                if not isinstance(proof.get(field), str) or not proof[field]:
                    raise ValueError(f'pairproof.{field}: explicit native API vocabulary required')
            proof_hash = proof.get('proof_sha256')
            if (not isinstance(proof_hash, str) or len(proof_hash) != 64
                    or any(character not in '0123456789abcdef' for character in proof_hash)):
                raise ValueError('pairproof.proof_sha256: native proof hash required')
    else:
        raise ValueError('unknown v2 record kind')
    # Round-trip through canonical JSON both validates nested finite numbers and makes a detached
    # provenance object. Boolean truth/qualification and causal pairing are supplied by the producer;
    # validating their field types does not establish any proof or deployment qualification.
    return json.loads(_canonical_json_v2(metadata))


def validate_frame_v2(data: bytes | bytearray | memoryview, *,
                      scalar_bounds: tuple[int, int] | None = None) -> FrameV2:
    """Validate one v2 frame without MAGIC, retaining exact canonical metadata and feature bytes."""
    encoded = bytes(data)
    if len(encoded) < METADATA_LENGTH_V2.size:
        raise ValueError('truncated v2 metadata length')
    metadata_length = METADATA_LENGTH_V2.unpack_from(encoded)[0]
    metadata_end = METADATA_LENGTH_V2.size + metadata_length
    if not metadata_length or metadata_end + STATE_BYTES > len(encoded):
        raise ValueError('v2 metadata/state exceeds remaining bytes')
    metadata_bytes = encoded[METADATA_LENGTH_V2.size:metadata_end]
    try:
        parsed = json.loads(metadata_bytes.decode('utf-8'), object_pairs_hook=_unique_json_object_v2,
                            parse_constant=_reject_constant_v2)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError('invalid UTF-8 JSON v2 metadata') from error
    metadata = _validate_metadata_v2(parsed)
    if _canonical_json_v2(metadata) != metadata_bytes:
        raise ValueError('v2 metadata JSON is not canonical')
    n = metadata['n']
    if len(encoded) != metadata_end + STATE_BYTES + n * ROW_BYTES:
        raise ValueError('v2 candidate count does not match exact byte length')
    state = _float_bytes(encoded[metadata_end:metadata_end + STATE_BYTES], STATE_COUNT, 'state')
    rows = encoded[metadata_end + STATE_BYTES:]
    for index in range(n):
        at = index * ROW_BYTES
        kind, _target, count, x, y = PAYLOAD.unpack_from(rows, at)
        if not 0 <= kind <= 255 or count < 1 or not -32768 <= x <= 32767 or not -32768 <= y <= 32767:
            raise ValueError('v2 executable payload exceeds Cand ABI')
        _float_bytes(rows[at + PAYLOAD.size:at + ROW_BYTES], PHI_COUNT, f'phi[{index}]')
    if scalar_bounds is not None:
        if metadata['kind'] != SCALAR or len(scalar_bounds) != 2:
            raise ValueError('v2 scalar bounds require one scalar record and lower/upper values')
        lower, upper = (_i32(value, 'scalar bound') for value in scalar_bounds)
        if lower > upper or not lower <= metadata['label'] <= upper:
            raise ValueError('v2 scalar target is outside supplied production bounds')
    return FrameV2(metadata, metadata_bytes, state, rows, encoded)


def encode_record_v2(metadata: Mapping, state, payload: Iterable[Sequence[int]] | bytes = (),
                     phi: Iterable[Iterable[float] | bytes] | bytes = (), *,
                     scalar_bounds: tuple[int, int] | None = None) -> bytes:
    """Encode explicit v2 metadata/facts; raw payload and phi bytes are copied unchanged.

    Byte payload is n packed5i32 rows; byte phi is n packed24f32 rows. Explicit arrays are also
    accepted. n comes from metadata and must exactly match both arrays; it is never inferred.
    """
    metadata = _validate_metadata_v2(dict(metadata))
    n = metadata['n']
    if isinstance(payload, (bytes, bytearray, memoryview)):
        payload_bytes = bytes(payload)
        if len(payload_bytes) != n * PAYLOAD.size:
            raise ValueError('v2 raw payload byte count does not match n')
        payload_rows = [payload_bytes[index * PAYLOAD.size:(index + 1) * PAYLOAD.size] for index in range(n)]
    else:
        supplied = tuple(payload)
        if len(supplied) != n:
            raise ValueError('v2 payload row count does not match n')
        payload_rows = []
        for index, row in enumerate(supplied):
            if len(row) != 5:
                raise ValueError('v2 payload requires kind,target,count,x,y')
            payload_rows.append(PAYLOAD.pack(*(_i32(value, f'payload[{index}]') for value in row)))
    if isinstance(phi, (bytes, bytearray, memoryview)):
        phi_bytes = _float_bytes(phi, n * PHI_COUNT, 'phi')
        phi_rows = [phi_bytes[index * PHI_COUNT * 4:(index + 1) * PHI_COUNT * 4] for index in range(n)]
    else:
        supplied = tuple(phi)
        if len(supplied) != n:
            raise ValueError('v2 phi row count does not match n')
        phi_rows = [_float_bytes(row, PHI_COUNT, f'phi[{index}]') for index, row in enumerate(supplied)]
    metadata_bytes = _canonical_json_v2(metadata)
    if len(metadata_bytes) > 0xffffffff:
        raise ValueError('v2 metadata exceeds uint32 byte length')
    pieces = [METADATA_LENGTH_V2.pack(len(metadata_bytes)), metadata_bytes,
              _float_bytes(state, STATE_COUNT, 'state')]
    pieces.extend(identity + facts for identity, facts in zip(payload_rows, phi_rows))
    return validate_frame_v2(b''.join(pieces), scalar_bounds=scalar_bounds).encoded


def iter_frames_v2(data: bytes | bytearray | memoryview, *, includes_magic: bool = False):
    """Validate a native v2 bundle in source order, without a candidate-count cap."""
    encoded = bytes(data)
    at = 0
    if includes_magic:
        if not encoded.startswith(MAGIC_V2):
            raise ValueError('native v2 bundle requires ZSTUD002 magic')
        at = len(MAGIC_V2)
    while at < len(encoded):
        if len(encoded) - at < METADATA_LENGTH_V2.size:
            raise ValueError('truncated v2 bundle metadata length')
        metadata_length = METADATA_LENGTH_V2.unpack_from(encoded, at)[0]
        metadata_end = at + METADATA_LENGTH_V2.size + metadata_length
        if not metadata_length or metadata_end + STATE_BYTES > len(encoded):
            raise ValueError('native v2 metadata/state exceeds remaining bytes')
        try:
            metadata = json.loads(encoded[at + METADATA_LENGTH_V2.size:metadata_end].decode('utf-8'),
                                  object_pairs_hook=_unique_json_object_v2,
                                  parse_constant=_reject_constant_v2)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError('invalid native v2 metadata') from error
        metadata = _validate_metadata_v2(metadata)
        end = metadata_end + STATE_BYTES + metadata['n'] * ROW_BYTES
        if end > len(encoded):
            raise ValueError('native v2 frame exceeds remaining bytes')
        yield validate_frame_v2(encoded[at:end])
        at = end


class SinkWriterV2:
    """New-file v2 sink; no feature construction, causal inference or qualification inference."""
    def __init__(self, path: str | Path, *, provenance: Mapping):
        if not isinstance(provenance, Mapping) or not provenance:
            raise ValueError('v2 sink needs explicit production provenance')
        self.path = Path(path)
        self.provenance = json.loads(_canonical_json_v2(dict(provenance)))
        self._output = self.path.open('xb')
        self._output.write(MAGIC_V2)
        self._offset = len(MAGIC_V2)
        self._count = 0
        self._appends = []
        self._closed = False

    def append_native(self, data, *, provenance: Mapping, includes_magic: bool = False) -> int:
        if self._closed:
            raise ValueError('v2 sink is closed')
        if not isinstance(provenance, Mapping) or not provenance:
            raise ValueError('v2 native append needs explicit final-status provenance')
        provenance = json.loads(_canonical_json_v2(dict(provenance)))
        frames = list(iter_frames_v2(data, includes_magic=includes_magic))
        if not frames:
            raise ValueError('cannot append an empty native v2 bundle')
        block = b''.join(frame.encoded for frame in frames)
        self._output.write(block)
        self._appends.append({'offset': self._offset, 'bytes': len(block), 'records': len(frames),
                              'sha256': hashlib.sha256(block).hexdigest(),
                              'origins': [{'decision_origin': frame.metadata['decision_origin'],
                                           'label_origin': frame.metadata['label_origin']} for frame in frames],
                              'provenance': provenance})
        self._offset += len(block)
        self._count += len(frames)
        return len(frames)

    def close(self, audit_path: str | Path | None = None) -> dict:
        if not self._closed:
            self._output.close()
            self._closed = True
        audit = {'schema': 'zero-student-sink-transport/v2', 'sink': str(self.path.resolve()),
                 'magic': MAGIC_V2.decode('ascii'), 'sha256': hashlib.sha256(self.path.read_bytes()).hexdigest(),
                 'bytes': self._offset, 'records': self._count, 'provenance': self.provenance,
                 'appends': self._appends, 'coverage': empty_coverage()}
        if audit_path is not None:
            with Path(audit_path).open('x', encoding='utf-8', newline='\n') as output:
                json.dump(audit, output, indent=2, allow_nan=False)
                output.write('\n')
        return audit

    def __enter__(self):
        return self

    def __exit__(self, _type, _value, _traceback):
        self.close()
