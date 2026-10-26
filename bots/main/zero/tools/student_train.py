"""Compact student math and an explicitly bounded BC canary entrypoint.

Mirrors core/student_net.h: a 48 -> 16 -> 16 encoder conditioned by learned attention
over every static camp group's 12 factual inputs, 15 direct command logits, ten
linear pointer heads and a value logit. Context and argument rows are ragged and complete.
The old compact parameter prefix and generic diagnostic functions remain available.
Source eligibility is independent of deployment proof.
Fresh runs zero the reserved legacy prefix; --init verifies a student model and preserves its prefix.
Adam moments restart on --init. Optional value BCE applies only to command batches (default disabled).
"""
from __future__ import annotations

from dataclasses import dataclass
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import sys
import uuid

import numpy as np

STATE, HIDDEN, RAW_PHI, COMMANDS = 48, 16, 24, 15
FEATURE_SCHEMA, LAYOUT_VERSION = 3, 2
CONFIG, CAPACITY, LEGACY_PREFIX = 16, 32768, 24842
HEAD_IDS = (0, 1, 2, 3, 4, 5, 6, 10, 11, 12)
HEADS = ('goal', 'step', 'attack', 'cast0', 'cast1', 'shop', 'draft', 'aux', 'priority', 'quantity')
SCALAR_HEADS = ('priority', 'quantity')
MODEL_ABI = 'main.zero.student.attention.v1'
POLICY_PROFILE = 'unconditional_camp_attention_v1'
PROFILE_MASK = ((1 << COMMANDS) - 1) & ~(1 << 9)
INT32_MIN, INT32_MAX = -(2 ** 31), 2 ** 31 - 1


def shapes(attention=False):
    out = [('w1', (STATE, HIDDEN)), ('b1', (HIDDEN,)),
           ('w2', (HIDDEN, HIDDEN)), ('b2', (HIDDEN,)),
           ('command_w', (HIDDEN, COMMANDS)), ('command_b', (COMMANDS,))]
    for head in HEADS:
        out += [(f'{head}_w', (HIDDEN, RAW_PHI)), (f'{head}_b', (RAW_PHI,))]
    out += [('v', (HIDDEN,)), ('v0', (1,))]
    return out + _attention_module().shapes() if attention else out


PARAMETERS = sum(math.prod(shape) for _, shape in shapes())
WEIGHT_END = LEGACY_PREFIX + PARAMETERS
assert PARAMETERS == 5408 and WEIGHT_END == 30250 <= CAPACITY
ATTENTION_PARAMETERS, ATTENTION_WEIGHT_END = 5592, 30434


def _attention_module():
    import student_attention_math
    return student_attention_math


def _has_attention(P):
    return 'att_w_e' in P


def _parameter_shapes(P):
    return shapes(attention=_has_attention(P))


def layout(attention=False):
    at, out = LEGACY_PREFIX, {}
    for name, shape in shapes(attention):
        out[name] = at
        at += math.prod(shape)
    assert at == (ATTENTION_WEIGHT_END if attention else WEIGHT_END)
    return out


def init(rng=None, *, attention=False):
    rng = rng if isinstance(rng, np.random.Generator) else np.random.default_rng(rng)
    out = {name: (rng.normal(0, math.sqrt(1 / shape[0]), shape).astype(np.float32)
                   if len(shape) == 2 else np.zeros(shape, dtype=np.float32))
            for name, shape in shapes()}
    if attention:
        # Append initialization without changing the settled base-weight/minibatch stream.
        stream = copy.deepcopy(rng.bit_generator.state)
        out.update(_attention_module().init(rng))
        rng.bit_generator.state = stream
    return out


def zero_grads(P):
    return {name: np.zeros_like(P[name]) for name, _ in _parameter_shapes(P)}


def _matrix(value, columns, name):
    value = np.asarray(value)
    if value.ndim != 2 or value.shape[1] != columns or not np.isfinite(value).all():
        raise ValueError(f'{name} requires finite [N,{columns}] values')
    return value


def softsign(value):
    return value / (1 + np.abs(value))


def softsign_grad(value):
    return 1 / np.square(1 + np.abs(value))


def embed(P, state, context_phi=None, context_offsets=None):
    if _has_attention(P):
        return _attention_module().embed(P, state, context_phi, context_offsets)
    state = _matrix(state, STATE, 'student state')
    first = state @ P['w1'] + P['b1']
    hidden = softsign(first)
    second = hidden @ P['w2'] + P['b2']
    return softsign(second), (state, first, hidden, second)


def embed_back(P, cache, derivative, G):
    if _has_attention(P):
        return _attention_module().embed_back(P, cache, derivative, G)
    state, first, hidden, second = cache
    second_grad = derivative * softsign_grad(second)
    G['w2'] += hidden.T @ second_grad
    G['b2'] += second_grad.sum(0)
    first_grad = (second_grad @ P['w2'].T) * softsign_grad(first)
    G['w1'] += state.T @ first_grad
    G['b1'] += first_grad.sum(0)


def command_logits(P, embedding):
    return embedding @ P['command_w'] + P['command_b']


def command_mask(mask, count):
    mask = np.asarray(mask)
    if mask.dtype == np.bool_ and mask.shape == (count, COMMANDS):
        legal = mask
    elif mask.shape == (count,) and np.issubdtype(mask.dtype, np.integer):
        if np.any(mask < 0) or np.any(mask >= (1 << COMMANDS)):
            raise ValueError('command bit mask exceeds the 15-command vocabulary')
        legal = (mask[:, None] & (1 << np.arange(COMMANDS))) != 0
    else:
        raise ValueError('command masks require [N] integer bits or [N,15] booleans')
    if np.any(~legal.any(1)):
        raise ValueError('every command record requires a legal choice')
    return legal


def _labels(labels, count):
    labels = np.asarray(labels)
    if (labels.shape != (count,) or not np.isfinite(labels).all() or np.any(labels != np.trunc(labels))
            or np.any(labels < INT32_MIN) or np.any(labels > INT32_MAX)):
        raise ValueError('labels require one signed int32 per state')
    return labels.astype(np.int64)


def command_loss_back(P, embedding, mask, labels, G, scale=1.0):
    count = len(embedding)
    if not count:
        raise ValueError('command loss requires a nonempty batch')
    legal, labels = command_mask(mask, count), _labels(labels, count)
    if np.any(labels < 0) or np.any(labels >= COMMANDS) or not np.all(legal[np.arange(count), labels]):
        raise ValueError('source command must be legal under its captured mask')
    logits = np.where(legal, command_logits(P, embedding), -np.inf)
    maximum = logits.max(1, keepdims=True)
    exp = np.exp(logits - maximum)
    probability = exp / exp.sum(1, keepdims=True)
    loss = (np.log(exp.sum(1)) + maximum[:, 0] - logits[np.arange(count), labels]).mean()
    accuracy = (logits.argmax(1) == labels).mean()
    derivative = probability.copy()
    derivative[np.arange(count), labels] -= 1
    derivative *= scale / count
    G['command_w'] += embedding.T @ derivative
    G['command_b'] += derivative.sum(0)
    return float(loss), float(accuracy), derivative @ P['command_w'].T


def head_name(head):
    if isinstance(head, str) and head in HEADS:
        return head
    if isinstance(head, (int, np.integer)) and head in HEAD_IDS:
        return HEADS[HEAD_IDS.index(head)]
    raise ValueError(f'unsupported student argument head: {head}')


def _ragged(phi, offsets, count):
    phi = _matrix(phi, RAW_PHI, 'student raw phi')
    offsets = np.asarray(offsets)
    if (offsets.shape != (count + 1,) or not np.issubdtype(offsets.dtype, np.integer)
            or offsets[0] != 0 or offsets[-1] != len(phi) or np.any(np.diff(offsets) <= 0)):
        raise ValueError('ragged offsets must contain one nonempty complete segment per state')
    return phi, offsets.astype(np.int64), np.repeat(np.arange(count), np.diff(offsets))


def project(P, embedding, head):
    name = head_name(head)
    return embedding @ P[f'{name}_w'] + P[f'{name}_b']


def family_logits(P, embedding, head, phi, offsets):
    phi, offsets, owner = _ragged(phi, offsets, len(embedding))
    coefficients = project(P, embedding, head)
    return np.sum(coefficients[owner] * phi, axis=1), (phi, offsets, owner)


def scalar_target(head, labels):
    name = head_name(head)
    raw = np.asarray(labels, dtype=np.float64)
    minimum = 1 if name == 'quantity' else INT32_MIN
    if name not in SCALAR_HEADS or not np.isfinite(raw).all() or np.any(raw != np.trunc(raw)) or np.any(raw < minimum) or np.any(raw > INT32_MAX):
        raise ValueError('scalar labels must be signed int32 priorities or positive int32 quantities')
    return np.arcsinh(raw) / 10.0


def scalar_raw(head, prediction, maximum=None):
    name = head_name(head)
    prediction = np.asarray(prediction, dtype=np.float64)
    if name not in SCALAR_HEADS or not np.isfinite(prediction).all():
        raise ValueError('scalar predictions must be finite')
    minimum = 1 if name == 'quantity' else INT32_MIN
    upper = INT32_MAX if maximum is None else int(maximum)
    if upper < minimum or upper > INT32_MAX or (name == 'priority' and maximum is not None):
        raise ValueError('invalid scalar output bound')
    bounded = np.clip(prediction, np.arcsinh(minimum) / 10, np.arcsinh(upper) / 10)
    raw = np.sinh(bounded * 10)
    rounded = np.where(raw >= 0, np.floor(raw + 0.5), np.ceil(raw - 0.5))
    return np.clip(rounded, minimum, upper).astype(np.int64)


def family_loss_back(P, embedding, head, phi, offsets, labels, G, scale=1.0):
    name, count = head_name(head), len(embedding)
    if not count:
        raise ValueError('family loss requires a nonempty batch')
    logits, (phi, offsets, owner) = family_logits(P, embedding, name, phi, offsets)
    labels = _labels(labels, count)
    if name in SCALAR_HEADS:
        if np.any(np.diff(offsets) != 1):
            raise ValueError('scalar heads require one candidate even for source quantity one')
        target = scalar_target(name, labels)
        error = logits - target
        loss = np.square(error).mean()
        accuracy = ((scalar_raw(name, logits) == labels).mean() if name == 'quantity'
                    else (np.sign(logits) == np.sign(target)).mean())
        derivative = 2 * error * scale / count
    else:
        derivative = np.zeros_like(logits)
        losses, correct = [], []
        for row in range(count):
            begin, end = offsets[row:row + 2]
            if labels[row] < 0 or labels[row] >= end - begin:
                raise ValueError('categorical label is outside its complete argument segment')
            segment = logits[begin:end]
            exp = np.exp(segment - segment.max())
            probability = exp / exp.sum()
            losses.append(np.log(exp.sum()) + segment.max() - segment[labels[row]])
            correct.append(segment.argmax() == labels[row])
            derivative[begin:end] = probability
            derivative[begin + labels[row]] -= 1
        loss, accuracy = np.mean(losses), np.mean(correct)
        derivative *= scale / count
    coefficient_grad = np.zeros((count, RAW_PHI), dtype=derivative.dtype)
    np.add.at(coefficient_grad, owner, derivative[:, None] * phi)
    G[f'{name}_w'] += embedding.T @ coefficient_grad
    G[f'{name}_b'] += coefficient_grad.sum(0)
    return float(loss), float(accuracy), coefficient_grad @ P[f'{name}_w'].T


def value(P, embedding):
    return embedding @ P['v'] + P['v0'][0]


def value_loss_back(P, embedding, won, G, scale=1.0):
    won = _labels(won, len(embedding))
    if np.any((won < -1) | (won > 1)):
        raise ValueError('value labels must be -1 unknown, zero loss, or one win')
    known = won >= 0
    if not known.any():
        return 0.0, np.zeros_like(embedding)
    logits = value(P, embedding)
    loss = (np.logaddexp(0, logits[known]) - won[known] * logits[known]).mean()
    probability = np.exp(-np.logaddexp(0, -logits))
    derivative = np.where(known, probability - won, 0) * scale / known.sum()
    G['v'] += embedding.T @ derivative
    G['v0'] += derivative.sum()
    return float(loss), derivative[:, None] * P['v'][None, :]


def flatten(P):
    shape_list = _parameter_shapes(P)
    if set(P) != {name for name, _ in shape_list}:
        raise ValueError('student parameter names do not match the compiled layout')
    for name, shape in shape_list:
        if np.shape(P[name]) != shape:
            raise ValueError(f'{name}: expected shape {shape}')
    return np.concatenate([np.asarray(P[name]).reshape(-1) for name, _ in shape_list])


def unflatten(values):
    values = np.asarray(values)
    attention = values.shape == (ATTENTION_PARAMETERS,)
    if values.shape not in ((PARAMETERS,), (ATTENTION_PARAMETERS,)):
        raise ValueError('student layout requires exactly 5408 or 5592 values')
    at, out = 0, {}
    for name, shape in shapes(attention):
        size = math.prod(shape)
        out[name] = values[at:at + size].reshape(shape).copy()
        at += size
    return out


@dataclass
class Checkpoint:
    params: dict[str, np.ndarray]
    legacy_prefix: np.ndarray
    config: np.ndarray


def save(P, path, legacy_prefix, config=()):
    """Generic diagnostic output: explicit config is preserved and no sidecar is generated."""
    prefix = np.asarray(legacy_prefix, dtype=np.float32)
    config = np.asarray(config, dtype=np.float32)
    student = flatten(P).astype(np.float32)
    if prefix.shape != (LEGACY_PREFIX,) or not np.isfinite(prefix).all() or not np.isfinite(student).all():
        raise ValueError('checkpoint requires a finite complete legacy prefix and student layout')
    if config.ndim != 1 or len(config) > CONFIG or not np.isfinite(config).all():
        raise ValueError('config requires at most 16 finite values')
    cfg = np.zeros(CONFIG, dtype=np.float32)
    cfg[:len(config)] = config
    block = np.concatenate((cfg, prefix, student, np.zeros(CAPACITY - LEGACY_PREFIX - len(student), dtype=np.float32)))
    Path(path).write_text(' '.join(f'{x:.9g}' for x in block) + '\n', encoding='utf-8')


def load(path):
    block = np.array(Path(path).read_text(encoding='utf-8').split(), dtype=np.float32)
    if not CONFIG + WEIGHT_END <= len(block) <= CONFIG + CAPACITY or not np.isfinite(block).all():
        raise ValueError('checkpoint does not contain a finite complete student layout')
    end = ATTENTION_WEIGHT_END if block[8] > 0 else WEIGHT_END
    if len(block) < CONFIG + end:
        raise ValueError('checkpoint is missing attention parameters')
    values = block[CONFIG + LEGACY_PREFIX:CONFIG + end]
    return Checkpoint(unflatten(values), block[CONFIG:CONFIG + LEGACY_PREFIX].copy(), block[:CONFIG].copy())


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _model_sources():
    root = Path(__file__).resolve().parents[1]
    names = ('tools/student_train.py', 'tools/student_pack.py', 'tools/student_attention_math.py',
             'src/core/student_net.h', 'src/core/student_net.cpp')
    return {name: _sha256(root / name) for name in names}


def _pack_module():
    import student_pack
    if (tuple(student_pack.HEADS) != HEADS or tuple(student_pack.HEAD_IDS) != HEAD_IDS
            or (student_pack.STATE, student_pack.RAW_PHI, student_pack.COMMANDS, student_pack.FEATURE_SCHEMA,
                student_pack.MODEL_ABI, student_pack.POLICY_PROFILE, student_pack.PROFILE_MASK)
            != (STATE, RAW_PHI, COMMANDS, FEATURE_SCHEMA, MODEL_ABI, POLICY_PROFILE, PROFILE_MASK)):
        raise ValueError('student pack and compact model have different ABIs')
    return student_pack


def source_provenance(pack_dir, files, shards):
    """Capture paired source semantics, representation and history without optimizer permission."""
    result = _pack_module().source_provenance(pack_dir, files, shards)
    result['model_source_sha256'] = _model_sources()
    return result


def checkpoint_provenance(pack_dir, files, shards, *, deployment_receipt=None):
    """Require the pack's current deployment contract in addition to source eligibility."""
    result = _pack_module().checkpoint_provenance(pack_dir, files, shards, deployment_receipt=deployment_receipt)
    result['model_source_sha256'] = _model_sources()
    return result


def _verify_checkpoint_provenance(provenance):
    packed = _pack_module()
    if (provenance.get('model_abi') != MODEL_ABI
            or provenance.get('feature_abi') != packed.FEATURE_ABI or provenance.get('pack_schema') != packed.PACK_SCHEMA):
        raise ValueError('student checkpoint requires current source-oracle camp-context provenance')
    manifest = packed.verify_manifest(provenance['feature_manifest_path'], verify_files=True)
    if (_sha256(provenance['feature_manifest_path']) != provenance['feature_manifest_file_sha256']
            or packed.manifest_sha256(manifest) != provenance['feature_manifest_sha256']):
        raise ValueError('student feature manifest changed after data was loaded')
    if provenance.get('model_source_sha256') != _model_sources():
        raise ValueError('student model sources changed after data was loaded')
    initial = provenance.get('initialization', {})
    if initial.get('kind') == 'verified_student_checkpoint':
        if _sha256(initial['path']) != initial['sha256'] or _sha256(initial['sidecar_path']) != initial['sidecar_sha256']:
            raise ValueError('initialization weights or model sidecar changed after validation')
    if not provenance.get('dataset_refs'):
        raise ValueError('student checkpoint needs explicit source dataset references')
    files, shards = [], []
    for ref in provenance['dataset_refs']:
        path = Path(ref['path'])
        if _sha256(path) != ref['sha256']:
            raise ValueError('student dataset shard changed after data was loaded')
        if not ref.get('data_sidecar_path') or _sha256(ref['data_sidecar_path']) != ref.get('data_sidecar_sha256'):
            raise ValueError('student dataset sidecar changed after data was loaded')
        files.append(path)
        with np.load(path, allow_pickle=False) as source:
            shards.append(dict(source))
    # Source qualification and measured deployment scope remain independently bound.
    current = packed.checkpoint_provenance(Path(provenance['feature_manifest_path']).parent, files, shards,
                                          deployment_receipt=provenance.get('deployment_receipt'))
    if provenance.get('policy_profile') != POLICY_PROFILE:
        raise ValueError('BC checkpoint requires the unconditional camp-attention policy profile')
    if any(provenance.get(key) != value for key, value in current.items()):
        raise ValueError('student source or deployment qualification changed after data was loaded')
    return manifest


def save_student_checkpoint(P, path, *, legacy_prefix, provenance):
    """Write command+student mode and a bound model sidecar; generic save keeps explicit configs."""
    if (STATE, HIDDEN, RAW_PHI, COMMANDS, CONFIG, CAPACITY, LEGACY_PREFIX, PARAMETERS, WEIGHT_END) != (48, 16, 24, 15, 16, 32768, 24842, 5408, 30250):
        raise ValueError('student checkpoint dimensions do not match the compiled runtime')
    if not _has_attention(P) or len(flatten(P)) != ATTENTION_PARAMETERS or ATTENTION_WEIGHT_END > CAPACITY:
        raise ValueError('camp-attention checkpoint requires the full appended 184 parameters')
    manifest = _verify_checkpoint_provenance(provenance)
    path = Path(path)
    _validate_output_paths(path, provenance, manifest)
    path.parent.mkdir(parents=True, exist_ok=True)
    config = np.zeros(CONFIG, dtype=np.float32)
    config[5] = config[6] = config[7] = config[8] = 1
    token = uuid.uuid4().hex
    temporary = path.with_name(f'{path.name}.{token}.tmp')
    save(P, temporary, legacy_prefix, config=config)
    restored = load(temporary)
    prefix = np.asarray(legacy_prefix, dtype=np.float32)
    student = flatten(P).astype(np.float32)
    if (restored.legacy_prefix.tobytes() != prefix.tobytes() or flatten(restored.params).tobytes() != student.tobytes()
            or not np.array_equal(restored.config, config) or len(temporary.read_text().split()) != CONFIG + CAPACITY):
        raise ValueError('student checkpoint did not reload with exact weights, prefix, and config')
    absolute = layout(attention=True)
    metadata = {**provenance, 'model_abi': MODEL_ABI, 'feature_schema': FEATURE_SCHEMA, 'layout_version': LAYOUT_VERSION,
                'policy_profile': POLICY_PROFILE,
                'synthetic_only': manifest.get('synthetic_only') is True,
                'layout': {'state': STATE, 'hidden': HIDDEN, 'raw_phi': RAW_PHI, 'commands': COMMANDS,
                           'family_heads': [{'name': name, 'head': head} for name, head in zip(HEADS, HEAD_IDS)],
                           'legacy_prefix': LEGACY_PREFIX, 'student_parameters': ATTENTION_PARAMETERS, 'student_end': ATTENTION_WEIGHT_END,
                           'context_phi': 12, 'attention_hidden': 4, 'context_storage': 'uncapped_canonical_camp_groups',
                           'capacity': CAPACITY, 'config_floats': CONFIG, 'injected_floats': CONFIG + CAPACITY,
                           'injected_bytes': 4 * (CONFIG + CAPACITY),
                           'parameters': [{'name': name, 'shape': list(shape), 'offset': absolute[name], 'count': math.prod(shape)} for name, shape in shapes(attention=True)]},
                'weights': {'path': str(path.resolve()), 'sha256': _sha256(temporary), 'config': config.tolist(),
                            'legacy_prefix_sha256': hashlib.sha256(prefix.astype('<f4').tobytes()).hexdigest(),
                            'student_sha256': hashlib.sha256(student.astype('<f4').tobytes()).hexdigest()}}
    sidecar = path.with_name(path.name + '.model.json')
    sidecar_tmp = sidecar.with_name(f'{sidecar.name}.{token}.tmp')
    sidecar_tmp.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)
    sidecar_tmp.replace(sidecar)
    return sidecar


def _positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return number


def _nonnegative_int(value):
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError('must be nonnegative')
    return number


def build_parser():
    parser = argparse.ArgumentParser(description='Bounded BC canary from source-oracle facts with a current deployment proof.')
    parser.add_argument('--pack-dir', type=Path, required=True)
    parser.add_argument('--steps', type=_positive_int, required=True)
    parser.add_argument('--batch', type=_positive_int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=_nonnegative_int, required=True)
    parser.add_argument('--lr', type=float, default=1e-3)
    parser.add_argument('--value-weight', type=float, default=0.0, help='optional value BCE on command batches only')
    parser.add_argument('--init', type=Path, help='verified student checkpoint; Adam moments restart')
    parser.add_argument('--deployment-receipt', type=Path, required=True,
                        help='measured current-feature useful-primitive and continuation receipt')
    parser.add_argument('--control-out', type=Path,
                        help='exact pre-Adam checkpoint (default: OUT.initial.txt)')
    return parser


@dataclass(frozen=True)
class CanaryRow:
    record: object
    game: str

    @property
    def native_kind(self):
        return self.record.decision_origin['native_kind']

    @property
    def input_type(self):
        return f"decision:{self.native_kind}/label:{self.record.label_origin['native_kind']}"

    @property
    def origin(self):
        packed = _pack_module()
        return (self.game, packed.origin_key(self.record.decision_origin), packed.origin_key(self.record.label_origin),
                self.record.label_kind)

    def source_metadata(self):
        return {'game': self.game, 'decision_origin': dict(self.record.decision_origin),
                'label_origin': dict(self.record.label_origin), 'event_role': self.record.event_role,
                'label_kind': self.record.label_kind, 'history_type': self.record.history_type,
                'qualification': dict(self.record.qualification),
                **{field: self.record.metadata[field] for field in ('history', 'facts', 'pairproof', 'rule_mask', 'training_rule_mask',
                                                                  'profile_mask', 'policy_profile', 'admitted_mask')
                   if field in self.record.metadata}}


def source_rows(files, shards):
    """Adapt validated paired records for math/metrics; this never grants optimizer permission."""
    packed = _pack_module()
    if len(files) != len(shards):
        raise ValueError('source files and shards must have matching lengths')
    rows = {head: [] for head in ('command',) + HEADS}
    for file, shard in zip(files, shards):
        for record in packed.records_from_shard(shard):
            head = 'command' if record.kind == packed.COMMAND else head_name(record.head)
            rows[head].append(CanaryRow(record, Path(file).stem))
    return rows


def _verified_initialization(path):
    path = Path(path).resolve()
    sidecar = path.with_name(path.name + '.model.json')
    metadata = json.loads(sidecar.read_text(encoding='utf-8'))
    if (metadata.get('synthetic_only') is True or metadata.get('model_abi') != MODEL_ABI
            or metadata.get('feature_schema') != FEATURE_SCHEMA or metadata.get('layout_version') != LAYOUT_VERSION
            or metadata.get('policy_profile') != POLICY_PROFILE):
        raise ValueError('initialization requires a real student model sidecar with the current ABI')
    initial_manifest = _verify_checkpoint_provenance(metadata)
    if initial_manifest.get('synthetic_only') is True:
        raise ValueError('initialization cannot reference a synthetic-only feature manifest')
    loaded = load(path)
    expected_layout = {'state': STATE, 'hidden': HIDDEN, 'raw_phi': RAW_PHI, 'commands': COMMANDS,
                       'legacy_prefix': LEGACY_PREFIX, 'student_parameters': ATTENTION_PARAMETERS, 'student_end': ATTENTION_WEIGHT_END,
                       'context_phi': 12, 'attention_hidden': 4,
                       'capacity': CAPACITY, 'config_floats': CONFIG, 'injected_floats': CONFIG + CAPACITY}
    if any(metadata.get('layout', {}).get(key) != value for key, value in expected_layout.items()):
        raise ValueError('initialization model layout differs from the compiled student graph')
    expected_parameters = [{'name': name, 'shape': list(shape), 'offset': layout(attention=True)[name], 'count': math.prod(shape)} for name, shape in shapes(attention=True)]
    weights = metadata.get('weights', {})
    if (metadata['layout'].get('parameters') != expected_parameters or weights.get('sha256') != _sha256(path)
            or not np.array_equal(np.asarray(weights.get('config')), loaded.config)
            or loaded.config[5] != 1 or loaded.config[6] != 1 or loaded.config[7] != 1 or loaded.config[8] != 1
            or weights.get('legacy_prefix_sha256') != hashlib.sha256(loaded.legacy_prefix.astype('<f4').tobytes()).hexdigest()
            or weights.get('student_sha256') != hashlib.sha256(flatten(loaded.params).astype('<f4').tobytes()).hexdigest()):
        raise ValueError('initialization weights/config/prefix do not match their model sidecar')
    return loaded, {'kind': 'verified_student_checkpoint', 'path': str(path), 'sha256': _sha256(path),
                    'sidecar_path': str(sidecar), 'sidecar_sha256': _sha256(sidecar), 'adam_moments': 'restart',
                    'frozen_paths': sorted(str(item) for item in _frozen_input_paths(metadata, initial_manifest))}


def _frozen_input_paths(provenance, manifest):
    paths = {Path(provenance['feature_manifest_path']).resolve(), Path(manifest['producer_binary_path']).resolve()}
    root = Path(manifest['source_root'])
    paths.update((root / relative).resolve() for relative in manifest['source_sha256'])
    model_root = Path(__file__).resolve().parents[1]
    paths.update((model_root / relative).resolve() for relative in _model_sources())
    for ref in provenance['dataset_refs']:
        paths.update((Path(ref['path']).resolve(), Path(ref['data_sidecar_path']).resolve()))
    for ref in provenance.get('label_source_refs', []):
        if isinstance(ref.get('path'), str):
            paths.add(Path(ref['path']).resolve())
    initial = provenance.get('initialization', {})
    if initial.get('kind') == 'verified_student_checkpoint':
        paths.update((Path(initial['path']).resolve(), Path(initial['sidecar_path']).resolve()))
        paths.update(Path(item).resolve() for item in initial.get('frozen_paths', []))
    receipt = provenance.get('deployment_receipt', {})
    if isinstance(receipt, dict) and isinstance(receipt.get('path'), str):
        paths.add(Path(receipt['path']).resolve())
    return paths


def _validate_output_paths(out, provenance, manifest):
    out = Path(out)
    frozen = _frozen_input_paths(provenance, manifest)
    outputs = (out, out.with_name(out.name + '.model.json'), out.with_name(out.name + '.metrics.json'))
    if any(path.resolve() in frozen for path in outputs):
        raise ValueError('checkpoint/model/metrics output would overwrite an immutable training input')
    if any(path.exists() and path.is_dir() for path in outputs):
        raise ValueError('checkpoint outputs must be files, not existing directories')


def prepare_canary(args):
    """Validate every source/model/data gate before RNG sampling or any optimizer can be entered."""
    if not math.isfinite(args.lr) or args.lr <= 0 or not math.isfinite(args.value_weight) or args.value_weight < 0:
        raise ValueError('learning rate must be positive; value weight must be nonnegative; both finite')
    packed = _pack_module()
    manifest = packed.verify_manifest(args.pack_dir / 'manifest.json', verify_files=True)
    if manifest.get('synthetic_only') is True:
        raise ValueError('the actual BC CLI rejects synthetic-only manifests')
    files = packed.game_shards(args.pack_dir)
    shards = []
    for file in files:
        with np.load(file, allow_pickle=False) as source:
            shards.append(dict(source))
    provenance = checkpoint_provenance(args.pack_dir, files, shards,
                                       deployment_receipt=getattr(args, 'deployment_receipt', None))
    _verify_checkpoint_provenance(provenance)
    rows = source_rows(files, shards)
    if not any(rows.values()):
        raise ValueError('the qualified student pack contains no labelled records')
    initial = None
    if args.init is not None:
        if args.init.resolve() == args.out.resolve():
            raise ValueError('output must differ from the immutable initialization checkpoint')
        initial, provenance['initialization'] = _verified_initialization(args.init)
    else:
        provenance['initialization'] = {'kind': 'zero_reserved_legacy_prefix', 'count': LEGACY_PREFIX}
    _validate_output_paths(args.out, provenance, manifest)
    control = _control_output(args)
    _validate_output_paths(control, provenance, manifest)
    main_outputs = {args.out.resolve(), args.out.with_name(args.out.name + '.model.json').resolve(),
                    args.out.with_name(args.out.name + '.metrics.json').resolve()}
    control_outputs = {control.resolve(), control.with_name(control.name + '.model.json').resolve()}
    if main_outputs & control_outputs:
        raise ValueError('trained and pre-Adam control outputs must be distinct')
    return rows, provenance, initial


def _batch(rows, indices):
    chosen = [rows[int(index)] for index in indices]
    records = [row.record for row in chosen]
    state = np.stack([record.state for record in records])
    if state.dtype != np.float32:
        raise ValueError('BC batches must preserve float32 production state facts')
    labels = np.array([record.label for record in records], dtype=np.int32)
    won = np.array([record.won for record in records], dtype=np.int32)
    context_phi = np.concatenate([record.context_phi for record in records])
    context_offsets = np.concatenate(([0], np.cumsum([len(record.context_phi) for record in records], dtype=np.int64)))
    if context_phi.dtype != np.float32:
        raise ValueError('BC batches must preserve float32 camp-context facts')
    if records[0].kind == 0:
        return chosen, state, labels, won, np.array([record.mask for record in records], dtype=np.uint32), None, context_phi, context_offsets
    phi = np.concatenate([record.phi for record in records])
    if phi.dtype != np.float32:
        raise ValueError('BC batches must preserve float32 production argument facts')
    offsets = np.concatenate(([0], np.cumsum([record.n for record in records], dtype=np.int64)))
    return chosen, state, labels, won, phi, offsets, context_phi, context_offsets


def _prediction_metrics(P, head, batch):
    chosen, state, labels, _, facts, offsets, context_phi, context_offsets = batch
    embedding, _ = embed(P, state, context_phi, context_offsets)
    if head == 'command':
        legal = command_mask(facts, len(chosen))
        logits = np.where(legal, command_logits(P, embedding), -np.inf)
        maximum = logits.max(1)
        losses = np.log(np.exp(logits - maximum[:, None]).sum(1)) + maximum - logits[np.arange(len(chosen)), labels]
        prediction = logits.argmax(1)
        return losses, prediction == labels, prediction, 'accuracy'
    logits, _ = family_logits(P, embedding, head, facts, offsets)
    if head in SCALAR_HEADS:
        target = scalar_target(head, labels)
        prediction = scalar_raw(head, logits)
        correct = prediction == labels if head == 'quantity' else np.sign(logits) == np.sign(target)
        return np.square(logits - target), correct, prediction, 'rounded_accuracy' if head == 'quantity' else 'sign_accuracy'
    losses, correct, prediction = [], [], []
    for row, label in enumerate(labels):
        segment = logits[offsets[row]:offsets[row + 1]]
        maximum = segment.max()
        losses.append(np.log(np.exp(segment - maximum).sum()) + maximum - segment[label])
        prediction.append(int(segment.argmax()))
        correct.append(prediction[-1] == label)
    return np.asarray(losses), np.asarray(correct), np.asarray(prediction), 'accuracy'


def canary_metrics(P, rows, evaluation_indices):
    """Bounded, fixed source-label samples; labelled counts describe the full explicit pack."""
    by_head, groups, origins, sampled_units = {}, {}, set(), []
    decision_kinds, label_kinds = {}, {}

    def group_key(head, row):
        return (head, row.record.team, row.input_type, row.record.event_role, row.record.label_kind, row.record.history_type)

    root = {'labelled': len(rows['command']), 'source_end_labelled': 0, 'source_non_end_labelled': 0,
            'source_end_with_choice': 0, 'sampled': 0, 'sampled_forced_end': 0,
            'predicted_natural_end': 0, 'predicted_end_total': 0, 'predicted_non_end': 0, 'mask_only_end': 0}
    for head, available in rows.items():
        for row in available:
            key = group_key(head, row)
            group = groups.setdefault(key, {'labelled': 0, 'sampled': 0, 'loss_sum': 0.0, 'correct': 0})
            group['labelled'] += 1
            origins.add(row.origin)
            for counts, kind in ((decision_kinds, row.native_kind), (label_kinds, row.record.label_origin['native_kind'])):
                counts[str(kind)] = counts.get(str(kind), 0) + 1
            if head == 'command':
                root['source_end_labelled' if row.record.label == 0 else 'source_non_end_labelled'] += 1
                root['source_end_with_choice'] += row.record.label == 0 and row.record.mask != 1
                root['mask_only_end'] += row.record.mask == 1
        if not available:
            by_head[head] = {'labelled': 0, 'sampled': 0, 'loss': None, 'accuracy': None}
            continue
        batch = _batch(available, evaluation_indices[head])
        loss, correct, prediction, metric = _prediction_metrics(P, head, batch)
        by_head[head] = {'labelled': len(available), 'sampled': len(loss), 'loss': float(loss.mean()), metric: float(correct.mean()),
                         'loss_kind': 'asinh_mse' if head in SCALAR_HEADS else 'masked_ce' if head == 'command' else 'ragged_ce'}
        for index, row in enumerate(batch[0]):
            group = groups[group_key(head, row)]
            sampled_units.append({'head': head, **row.source_metadata()})
            group['sampled'] += 1
            group['loss_sum'] += float(loss[index])
            group['correct'] += int(correct[index])
            if head == 'command':
                group['sampled_forced_end'] = group.get('sampled_forced_end', 0) + (row.record.mask == 1)
                group['predicted_natural_end'] = group.get('predicted_natural_end', 0) + int(prediction[index] == 0 and row.record.mask != 1)
                group['predicted_non_end'] = group.get('predicted_non_end', 0) + int(prediction[index] != 0)
        if head == 'command':
            root['sampled'] = len(prediction)
            forced = np.array([row.record.mask == 1 for row in batch[0]])
            root['sampled_forced_end'] = int(forced.sum())
            root['predicted_natural_end'] = int(np.sum((prediction == 0) & ~forced))
            root['predicted_end_total'] = int(np.sum(prediction == 0))
            root['predicted_non_end'] = int(np.sum(prediction != 0))
    detailed = []
    for (head, side, input_type, event_role, label_kind, history_type), group in sorted(groups.items()):
        count = group['sampled']
        detailed.append({'head': head, 'side': side, 'input_type': input_type, 'labelled': group['labelled'], 'sampled': count,
                         'event_role': event_role, 'label_kind': label_kind, 'history_type': history_type,
                         'loss': group['loss_sum'] / count if count else None,
                         'correct_fraction': group['correct'] / count if count else None,
                         **({key: group.get(key, 0) for key in ('sampled_forced_end', 'predicted_natural_end', 'predicted_non_end')}
                            if head == 'command' else {})})
    return {'by_head': by_head, 'by_head_side_input_type': detailed, 'labelled_source_units': len(origins),
            'ce_records': sum(len(available) for available in rows.values()), 'decision_kind_ce_records': decision_kinds,
            'label_kind_ce_records': label_kinds, 'sampled_source_units': sampled_units,
            'root_end_vs_non_end': root, 'scope': 'bounded source-label BC fit; no activity or strength claim'}


def _adam_step(P, gradients, first, second, step, rate):
    for name, _ in _parameter_shapes(P):
        if not np.isfinite(gradients[name]).all():
            raise ValueError('nonfinite BC gradient; no checkpoint emitted')
        first[name] *= 0.9
        first[name] += 0.1 * gradients[name]
        second[name] *= 0.999
        second[name] += 0.001 * np.square(gradients[name])
        if not np.isfinite(first[name]).all() or not np.isfinite(second[name]).all():
            raise ValueError('nonfinite Adam moment; no checkpoint emitted')
        P[name] -= rate * (first[name] / (1 - 0.9 ** step)) / (np.sqrt(second[name] / (1 - 0.999 ** step)) + 1e-8)
        if not np.isfinite(P[name]).all():
            raise ValueError('nonfinite BC parameter; no checkpoint emitted')


def _initial_weight_hashes(P, prefix):
    """Exact pre-update state for the saved, matched control."""
    return {'initial_student_sha256': hashlib.sha256(flatten(P).astype('<f4').tobytes()).hexdigest(),
            'initial_legacy_prefix_sha256': hashlib.sha256(np.asarray(prefix, dtype='<f4').tobytes()).hexdigest()}


def _control_output(args):
    value = getattr(args, 'control_out', None)
    return Path(value) if value is not None else args.out.with_name(args.out.name + '.initial.txt')


def _run_canary(args, rows, provenance, initial):
    """Only main may enter this explicit finite optimizer loop after all qualification checks."""
    rng = np.random.default_rng(args.seed)
    P = init(rng, attention=True) if initial is None else {name: value.copy() for name, value in initial.params.items()}
    prefix = np.zeros(LEGACY_PREFIX, dtype=np.float32) if initial is None else initial.legacy_prefix.copy()
    initial_hashes = _initial_weight_hashes(P, prefix)
    control = _control_output(args)
    control_sidecar = save_student_checkpoint(P, control, legacy_prefix=prefix,
        provenance={**provenance, 'artifact_role': 'pre_bc_initialization_control',
                    'optimizer_updates': 0, 'initialization_seed': args.seed, **initial_hashes})
    control_ref = {'path': str(control.resolve()), 'sha256': _sha256(control),
                   'sidecar_path': str(control_sidecar.resolve()), 'sidecar_sha256': _sha256(control_sidecar),
                   'optimizer_updates': 0}
    evaluation_rng = np.random.default_rng(np.random.SeedSequence([args.seed, 0x53545544]))
    fixed = {head: evaluation_rng.choice(len(available), min(args.batch, len(available)), replace=False)
             for head, available in rows.items() if available}
    before = canary_metrics(P, rows, fixed)
    first, second, history = zero_grads(P), zero_grads(P), []
    for step in range(1, args.steps + 1):
        gradients, losses = zero_grads(P), {}
        for head, available in rows.items():
            if not available:
                continue
            indices = rng.choice(len(available), min(args.batch, len(available)), replace=False)
            _, state, labels, won, facts, offsets, context_phi, context_offsets = _batch(available, indices)
            embedding, cache = embed(P, state, context_phi, context_offsets)
            if head == 'command':
                loss, _, derivative = command_loss_back(P, embedding, facts, labels, gradients)
                if args.value_weight:
                    value_loss, value_derivative = value_loss_back(P, embedding, won, gradients, scale=args.value_weight)
                    derivative += value_derivative
                    losses['value_bce_weighted'] = args.value_weight * value_loss
            else:
                loss, _, derivative = family_loss_back(P, embedding, head, facts, offsets, labels, gradients)
            embed_back(P, cache, derivative, gradients)
            losses[head] = loss
        _adam_step(P, gradients, first, second, step, args.lr)
        record = {'step': step, 'loss_sum_of_head_means': float(sum(losses.values())), 'by_head': losses}
        history.append(record)
        print(json.dumps(record), flush=True)
    provenance = {**provenance, 'canary': {'steps': args.steps, 'batch': args.batch, 'seed': args.seed, 'lr': args.lr,
                                          **initial_hashes,
                                         'initial_control': control_ref,
                                         'optimizer': 'Adam', 'value_weight': args.value_weight, 'value_scope': 'command batches only',
                                         'objective': 'sum of per-head mean losses', 'adam_moments': 'restart'}}
    after = canary_metrics(P, rows, fixed)
    sidecar = save_student_checkpoint(P, args.out, legacy_prefix=prefix, provenance=provenance)
    report = {'model_abi': MODEL_ABI, 'training': provenance['canary'], 'before': before, 'after': after,
              'initial_control': control_ref,
              'steps': history, 'weights': str(args.out.resolve()), 'model_sidecar': str(sidecar.resolve())}
    metrics_path = args.out.with_name(args.out.name + '.metrics.json')
    metrics_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        rows, provenance, initial = prepare_canary(args)
        _run_canary(args, rows, provenance, initial)
    except (ValueError, OSError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
