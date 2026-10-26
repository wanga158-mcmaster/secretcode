"""Immutable runtime-only cfg9 variants of one already fitted attention checkpoint.

No model, source dataset, optimizer or fitted-parent sidecar is modified. Runtime cache
parity/caller evidence belongs to the eventual activity recipe and remains explicitly pending here.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np

import student_train as train

SCHEMA = 'main.zero.runtime_variant_pair/v1'
VARIANT_SCHEMA = 'main.zero.runtime_variant/v1'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def immutable_write(path, data):
    path = Path(path)
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f'immutable variant output already differs: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as target:
        target.write(data)


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n').encode('utf-8')


def create_pair(parent, directory):
    parent = Path(parent).resolve(strict=True)
    parent_sidecar = parent.with_name(parent.name + '.model.json')
    # Existing validation checks the actual fit, data, source math, config and checkpoint layout.
    original, _ = train._verified_initialization(parent)
    original_text = parent.read_bytes()
    original_metadata = parent_sidecar.read_bytes()
    metadata = json.loads(original_metadata)
    if metadata.get('artifact_role') == 'runtime_engineering_variant':
        raise ValueError('the parent must be the original fitted checkpoint, not another runtime variant')
    if metadata.get('canary', {}).get('optimizer') != 'Adam' or metadata['canary'].get('steps', 0) < 1:
        raise ValueError('runtime variants require the actual fitted parent')
    tokens = original_text.decode('utf-8').split()
    block = np.asarray(tokens, dtype=np.float32)
    if block.shape != (train.CONFIG + train.CAPACITY,) or not np.isfinite(block).all():
        raise ValueError('the parent must contain the complete finite 16+32768 float block')
    if not np.array_equal(block[:train.CONFIG], original.config) or not np.all(block[5:9] == 1):
        raise ValueError('runtime variants require the deployed cfg5/6/7/8 attention profile')
    directory = Path(directory).resolve()
    outputs = [directory / f'cache-{name}.txt' for name in ('off', 'on')]
    if parent in outputs or parent_sidecar in [path.with_name(path.name + '.model.json') for path in outputs]:
        raise ValueError('runtime variants cannot overwrite the fitted parent')
    learned_bits = block[train.CONFIG:].astype('<f4', copy=False).tobytes()
    learned_hash = sha256(learned_bits)
    parent_weights = {'path': str(parent), 'sha256': sha256(original_text)}
    parent_ref = {'path': str(parent_sidecar), 'sha256': sha256(original_metadata)}
    variants = {}
    for name, flag, path in zip(('off', 'on'), (0, 1), outputs):
        changed = list(tokens)
        changed[9] = str(flag)
        data = (' '.join(changed) + '\n').encode('utf-8')
        check = np.asarray(data.decode('utf-8').split(), dtype=np.float32)
        other_config = [index for index in range(train.CONFIG) if index != 9]
        if (check[train.CONFIG:].astype('<f4', copy=False).tobytes() != learned_bits
                or check[other_config].tobytes() != block[other_config].tobytes()
                or check[9] != flag):
            raise ValueError('runtime variant changed a weight or unrelated config bit')
        sidecar = path.with_name(path.name + '.model.json')
        variant = copy.deepcopy(metadata)
        variant['artifact_role'] = 'runtime_engineering_variant'
        variant['weights'].update(path=str(path), sha256=sha256(data), config=check[:train.CONFIG].tolist())
        variant['runtime_variant'] = {
            'schema': VARIANT_SCHEMA, 'kind': 'immutable_camp_context_cache',
            'parent_weights': parent_weights, 'parent_sidecar': parent_ref,
            'config_index': 9, 'config_value': flag, 'changed_config_indices': [9],
            'actual_changed_config_indices': [index for index in range(train.CONFIG)
                                               if check[index:index + 1].tobytes() != block[index:index + 1].tobytes()],
            'learned_float_count': train.CAPACITY, 'learned_block_sha256': learned_hash,
            'learned_bits_unchanged': True, 'other_config_bits_unchanged': True,
            'evidence_status': 'pending_activity_binding', 'optimizer_rerun': False,
            'claim': 'Runtime engineering variant only; cache parity and current caller evidence must be bound by the activity recipe.'}
        immutable_write(path, data)
        immutable_write(sidecar, json_bytes(variant))
        restored, _ = train._verified_initialization(path)
        if (restored.legacy_prefix.tobytes() != original.legacy_prefix.tobytes()
                or train.flatten(restored.params).tobytes() != train.flatten(original.params).tobytes()
                or restored.config[other_config].tobytes() != original.config[other_config].tobytes()
                or restored.config[9] != flag):
            raise ValueError('runtime variant did not reload exactly')
        variants[name] = {'path': str(path), 'sha256': sha256(path.read_bytes()),
                          'sidecar_path': str(sidecar), 'sidecar_sha256': sha256(sidecar.read_bytes())}
    if parent.read_bytes() != original_text or parent_sidecar.read_bytes() != original_metadata:
        raise ValueError('the fitted parent changed while variants were created')
    pair = {'schema': SCHEMA, 'parent_weights': parent_weights, 'parent_sidecar': parent_ref,
            'learned_block_sha256': learned_hash, 'off': variants['off'], 'on': variants['on'],
            'bitproof': {'learned_float_count': train.CAPACITY, 'learned_bits_unchanged': True,
                         'other_config_float_count': train.CONFIG - 1, 'other_config_bits_unchanged': True,
                         'config9_off': 0, 'config9_on': 1, 'reload_exact': True,
                         'original_parent_unmodified': True},
            'evidence_status': 'pending_activity_binding', 'optimizer_rerun': False,
            'source_or_model_math_changed': False}
    pair_path = directory / 'pair.json'
    immutable_write(pair_path, json_bytes(pair))
    return pair_path, pair


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    args = parser.parse_args()
    path, pair = create_pair(args.parent, args.out_dir)
    print(json.dumps({'pair': str(path), 'off': pair['off'], 'on': pair['on'],
                      'learned_block_sha256': pair['learned_block_sha256'], 'bitproof': pair['bitproof']}))


if __name__ == '__main__':
    main()
