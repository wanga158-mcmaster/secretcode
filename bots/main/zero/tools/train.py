"""Imitation training for main/zero (DESIGN.md 3.1): the bot's network (src/core/net.cpp) in numpy - same layout,
same maths (softsign, column-major weights, logit = (U t) . a(P phi + c) + (V t) . phi) - trained by cross-entropy
on the top teams' choices per head, scalar priority/quantity regression, and the value head on game results.

  python -B -X utf8 bots/main/zero/tools/train.py [--steps 20000] [--batch 512] [--lr 0.001] [--out W.txt] [--init W.txt]

Held-out: 10% of games (by index). The command-mode entrypoint writes deployable weights (config[5]=1)
and a model provenance sidecar. Generic save/load remain available for explicit diagnostic configurations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import time
import zlib
import uuid
from pathlib import Path

import numpy as np
from pack import COVERAGE_SCOPE, PACK_SCHEMA, verify_manifest

ROOT = Path(__file__).resolve().parents[4]
PACK = ROOT / '.cache/main/zero/exact/pack'
LEGACY_HEADS = ['goal', 'step', 'attack', 'cast0', 'cast1', 'shop', 'draft', 'tail', 'ranged']
HEADS = LEGACY_HEADS + ['action', 'aux', 'priority', 'quantity']
KPHI = [24, 24, 16, 24, 24, 16, 24, 16, 24, 24, 24, 4, 4]
KSTATE, KHID, KPROJ, KDRAFT = 48, 48, 8, 32
# Capacity experiments only (the bot's net.h fixes 48/8): ZERO_KHID / ZERO_KPROJ override the widths.
KHID = int(os.environ.get('ZERO_KHID', KHID))
KPROJ = int(os.environ.get('ZERO_KPROJ', KPROJ))
KCONFIG, KWEIGHTS = 16, 32768
LEGACY_CAPACITY = 20480
INTERMEDIATE_CAPACITY = 24576
SCALAR_HEADS = ('priority', 'quantity')
PRIORITY_MIN, PRIORITY_MAX, PRIORITY_SCALE = -(2 ** 31), 2 ** 31 - 1, 10.0
MODEL_ABI = 'main.zero.command.v1'
MODEL_SOURCES = ('tools/train.py', 'src/core/net.h', 'src/core/net.cpp', 'src/core/cand.h',
                 'src/weights.h', 'src/weights.cpp', 'src/bot.cpp', 'src/draft.cpp')


def ss(x):
    return x / (1.0 + np.abs(x))


def dss(x):
    return 1.0 / (1.0 + np.abs(x)) ** 2


def priority_target(raw):
    raw = np.asarray(raw, dtype=np.float64)
    if not np.all(np.isfinite(raw)) or np.any(raw < PRIORITY_MIN) or np.any(raw > PRIORITY_MAX) or np.any(raw != np.trunc(raw)):
        raise ValueError('priority labels must be signed int32 values')
    return np.arcsinh(raw) / PRIORITY_SCALE


def priority_raw(target):
    target = np.asarray(target, dtype=np.float64)
    if not np.all(np.isfinite(target)):
        raise ValueError('priority predictions must be finite')
    target = np.clip(target, priority_target(PRIORITY_MIN), priority_target(PRIORITY_MAX))
    raw = np.sinh(target * PRIORITY_SCALE)
    rounded = np.where(raw >= 0.0, np.floor(raw + 0.5), np.ceil(raw - 0.5))
    return np.clip(rounded, PRIORITY_MIN, PRIORITY_MAX).astype(np.int64)


def quantity_target(raw):
    target = priority_target(raw)
    if np.any(np.asarray(raw) < 1):
        raise ValueError('quantity labels must be positive int32 values')
    return target


def quantity_raw(target):
    return np.maximum(priority_raw(target), 1)


def scalar_target(name, raw):
    return quantity_target(raw) if name == 'quantity' else priority_target(raw)


def scalar_accuracy(name, prediction, label, target):
    if name == 'quantity':
        return quantity_raw(prediction) == label
    return np.sign(prediction) == np.sign(target)


# ---- parameters (the C++ layout, each matrix as [inputs, outputs]) ------------------------------------------

def shapes():
    out = [('w1', (KSTATE, KHID)), ('b1', (KHID,)), ('w2', (KHID, KHID)), ('b2', (KHID,)), ('v', (KHID,)), ('v0', (1,)),
           ('m', (KHID,)), ('m0', (1,))]
    def add_head(h, name):
        inp = KDRAFT if name == 'draft' else KHID
        out.extend([(f'u_{name}', (inp, KPROJ)), (f'vv_{name}', (inp, KPHI[h])), (f'p_{name}', (KPHI[h], KPROJ)), (f'c_{name}', (KPROJ,))])
    for h, name in enumerate(LEGACY_HEADS):
        add_head(h, name)
    out += [('dw1', (KSTATE, KDRAFT)), ('db1', (KDRAFT,))]
    for h in range(len(LEGACY_HEADS), len(HEADS)):
        add_head(h, HEADS[h])
    return out


def legacy_size():
    at = 0
    for name, shape in shapes():
        if name == 'u_action':
            break
        at += math.prod(shape)
    return at


def intermediate_size():
    at = 0
    for name, shape in shapes():
        if name == 'u_priority':
            break
        at += math.prod(shape)
    return at


def quantity_prefix_size():
    at = 0
    for name, shape in shapes():
        if name == 'u_quantity':
            break
        at += math.prod(shape)
    return at


def init(rng):
    P = {}
    for name, shp in shapes():
        if len(shp) == 2:
            P[name] = (rng.standard_normal(shp) * math.sqrt(1.0 / shp[0])).astype(np.float32)
        else:
            P[name] = np.zeros(shp, dtype=np.float32)
    # The tail starts indifferent (zero U and V give zero logits), then learns from pre-start labels and
    # self-play; P stays random so U gets gradient.
    P['u_tail'][:] = 0.0
    P['vv_tail'][:] = 0.0
    return P


def flatten(P) -> np.ndarray:
    return np.concatenate([P[name].reshape(-1) for name, _ in shapes()])


def unflatten(w: np.ndarray):
    needed = sum(math.prod(shape) for _, shape in shapes())
    if len(w) < needed:
        raise ValueError(f'{len(w)} learned weights; current layout requires {needed}')
    P, at = {}, 0
    for name, shp in shapes():
        n = int(np.prod(shp))
        P[name] = w[at:at + n].reshape(shp).astype(np.float32)
        at += n
    return P


def save(P, path: Path, config=(0.0, 0.0, 0.0, 0.0)):
    flat = flatten(P)
    if flat.size > KWEIGHTS:
        print(f'not saved: {flat.size} weights exceed the block ({KWEIGHTS})')
        return
    cfg = np.zeros(KCONFIG, dtype=np.float32)
    cfg[:len(config)] = config
    vec = np.concatenate([cfg, flat, np.zeros(KWEIGHTS - flat.size, dtype=np.float32)])
    path.write_text(' '.join(f'{x:.9g}' for x in vec) + '\n', encoding='utf-8')


def load(path: Path):
    vec = np.array([float(x) for x in path.read_text(encoding='utf-8').split()], dtype=np.float32)
    w = vec[KCONFIG:]
    prefix = legacy_size()
    intermediate = intermediate_size()
    prior_quantity = quantity_prefix_size()
    needed = sum(math.prod(shape) for _, shape in shapes())
    if len(w) < prefix:
        raise ValueError(f'{path}: {len(w)} learned weights; legacy prefix requires {prefix}')
    if len(w) in (prefix, LEGACY_CAPACITY) and not np.any(w[prefix:] != 0.0):
        retained, retained_heads = prefix, len(LEGACY_HEADS)
    elif len(w) in (intermediate, INTERMEDIATE_CAPACITY) and not np.any(w[intermediate:] != 0.0):
        retained, retained_heads = intermediate, HEADS.index('priority')
    elif len(w) in (prior_quantity, INTERMEDIATE_CAPACITY) and not np.any(w[prior_quantity:] != 0.0):
        retained, retained_heads = prior_quantity, HEADS.index('quantity')
    elif len(w) >= needed:
        return unflatten(w)
    else:
        raise ValueError(f'{path}: incomplete current layout; expected {needed} learned weights or a zero-padded legacy block')
    # Preserve every available checkpoint parameter. New heads start at zero output with random projections
    # so their supervised gradients can learn them; old zero padding is never interpreted as parameters.
    P = init(np.random.default_rng(0))
    at = 0
    for name, shape in shapes():
        if at == retained:
            break
        n = math.prod(shape)
        P[name] = w[at:at + n].reshape(shape).copy()
        at += n
    new_heads = HEADS[retained_heads:]
    for name in new_heads:
        P[f'u_{name}'][:] = 0.0
        P[f'vv_{name}'][:] = 0.0
    return P


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def _model_source_hashes():
    return {name: _sha256(ROOT / 'bots/main/zero' / name) for name in MODEL_SOURCES}


def command_checkpoint_provenance(pack_dir, files, shards):
    """Capture the verified immutable dataset used by this command-mode training run."""
    pack_dir = Path(pack_dir).resolve()
    if not files or len(files) != len(shards):
        raise ValueError('checkpoint provenance requires every loaded game shard')
    verify_manifest(pack_dir=pack_dir, exact=True)
    validate_shards(files, shards)
    manifest_path = pack_dir / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schema') != PACK_SCHEMA or manifest.get('producer') != 'exact-inputs' or manifest.get('heads') != HEADS or manifest.get('kphi') != KPHI:
        raise ValueError('checkpoint feature manifest does not match the exact command model')
    refs = []
    for file, shard in zip(files, shards):
        path = Path(file).resolve()
        game = np.asarray(shard.get('game_id'))
        source = np.asarray(shard.get('source_sha256'))
        if game.shape != () or str(game.item()) != path.stem or source.shape != ():
            raise ValueError(f'{path}: missing stable game/source provenance')
        source_hash = str(source.item())
        if len(source_hash) != 64 or any(c not in '0123456789abcdef' for c in source_hash):
            raise ValueError(f'{path}: invalid export source hash')
        stat = path.stat()
        refs.append({'path': str(path), 'sha256': _sha256(path), 'source_sha256': source_hash,
                     'game_id': str(game.item()), 'size_bytes': stat.st_size, 'mtime_ns': stat.st_mtime_ns,
                     'coverage': {'scope': str(np.asarray(shard['coverage_scope']).item()),
                                  **{key: int(np.asarray(shard[key]).item()) for key in ('input_count', 'eligible_policy_inputs', 'eligible_policy_labelled_inputs', 'excluded_inputs')}},
                     'head_records': {h: len(shard[f'{h}_n']) for h in HEADS if f'{h}_n' in shard}})
    return {'pack_dir': str(pack_dir), 'pack_schema': PACK_SCHEMA,
            'feature_manifest_path': str(manifest_path), 'feature_manifest_sha256': _sha256(manifest_path),
            'feature_source_sha256': manifest.get('source_sha256', {}), 'dataset_refs': refs,
            'model_source_sha256': _model_source_hashes()}


def save_command_checkpoint(P, path, *, provenance, step=None):
    """Export deployment-ready command weights plus model provenance; generic save/load stay neutral."""
    if (KSTATE, KHID, KPROJ, KDRAFT, KCONFIG, KWEIGHTS) != (48, 48, 8, 32, 16, 32768):
        raise ValueError('command checkpoints require the compiled runtime dimensions; use generic save for capacity diagnostics')
    path = Path(path)
    manifest = Path(provenance['feature_manifest_path'])
    if provenance.get('pack_schema') != PACK_SCHEMA or _sha256(manifest) != provenance['feature_manifest_sha256']:
        raise ValueError('feature manifest changed after the training data was loaded')
    if _model_source_hashes() != provenance['model_source_sha256']:
        raise ValueError('model sources changed during this training run')
    for ref in provenance['dataset_refs']:
        stat = Path(ref['path']).stat()
        if stat.st_size != ref['size_bytes'] or stat.st_mtime_ns != ref['mtime_ns']:
            raise ValueError(f'dataset shard changed during training: {ref["path"]}')
    flat = flatten(P).astype(np.float32)
    learned = sum(math.prod(shape) for _, shape in shapes())
    if flat.size != learned or learned > KWEIGHTS or not np.isfinite(flat).all():
        raise ValueError('model parameters do not fit the finite learned-weight layout')
    cfg = np.zeros(KCONFIG, dtype=np.float32)
    cfg[5] = 1.0
    path.parent.mkdir(parents=True, exist_ok=True)
    token = uuid.uuid4().hex
    temporary = path.with_name(f'{path.name}.{token}.tmp')
    save(P, temporary, config=cfg)
    injected = np.array(temporary.read_text(encoding='utf-8').split(), dtype=np.float32)
    if injected.size != KCONFIG + KWEIGHTS or injected[5] != 1.0 or not np.array_equal(injected[:KCONFIG], cfg):
        raise ValueError('exported command checkpoint has an invalid injection block or mode')
    if not np.array_equal(flatten(load(temporary)), flat):
        raise ValueError('exported command checkpoint does not reload exactly')
    at, parameters, heads = 0, [], []
    offsets = {}
    for name, shape in shapes():
        count = math.prod(shape)
        offsets[name] = at
        parameters.append({'name': name, 'shape': list(shape), 'offset': at, 'count': count})
        at += count
    for index, name in enumerate(HEADS):
        heads.append({'index': index, 'name': name, 'phi': KPHI[index],
                      'input': KDRAFT if name == 'draft' else KHID,
                      'offsets': {key: offsets[f'{key}_{name}'] for key in ('u', 'vv', 'p', 'c')}})
    metadata = {'model_abi': MODEL_ABI, 'pack_schema': PACK_SCHEMA, 'step': step, **provenance,
                'layout': {'state': KSTATE, 'hidden': KHID, 'projection': KPROJ, 'draft_hidden': KDRAFT,
                           'activation': 'softsign', 'config_floats': KCONFIG, 'capacity_floats': KWEIGHTS,
                           'learned_floats': learned, 'injected_floats': injected.size,
                           'injected_bytes': injected.size * np.dtype(np.float32).itemsize,
                           'head_count': len(HEADS), 'heads': heads, 'parameters': parameters},
                'weights': {'path': str(path.resolve()), 'sha256': _sha256(temporary),
                            'config': [float(x) for x in cfg], 'command_mode': True}}
    sidecar = path.with_name(path.name + '.model.json')
    sidecar_tmp = sidecar.with_name(f'{sidecar.name}.{token}.tmp')
    sidecar_tmp.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)
    sidecar_tmp.replace(sidecar)
    return sidecar


# ---- forward / backward ------------------------------------------------------------------------------------

def trunk(P, s):
    a1 = s @ P['w1'] + P['b1']
    h1 = ss(a1)
    a2 = h1 @ P['w2'] + P['b2']
    return ss(a2), (s, a1, h1, a2)


def trunk_back(P, cache, dt, G):
    s, a1, h1, a2 = cache
    da2 = dt * dss(a2)
    G['w2'] += h1.T @ da2
    G['b2'] += da2.sum(0)
    da1 = (da2 @ P['w2'].T) * dss(a1)
    G['w1'] += s.T @ da1
    G['b1'] += da1.sum(0)


def draft_trunk(P, s):
    z = s @ P['dw1'] + P['db1']
    t = np.zeros((len(s), KHID), dtype=z.dtype)
    t[:, :KDRAFT] = ss(z)  # C++ pads the draft embedding to the common trunk width
    return t, (s, z)


def draft_trunk_back(P, cache, dt, G):
    s, z = cache
    dz = dt[:, :KDRAFT] * dss(z)
    G['dw1'] += s.T @ dz
    G['db1'] += dz.sum(0)


def head_forward(P, name, t, phi, mask):
    tx = t[:, :KDRAFT] if name == 'draft' else t
    u = tx @ P[f'u_{name}']                     # [B, K]
    v = tx @ P[f'vv_{name}']                    # [B, F]
    z = phi @ P[f'p_{name}'] + P[f'c_{name}']   # [B, C, K]
    g = ss(z)
    logit = np.einsum('bk,bck->bc', u, g) + np.einsum('bf,bcf->bc', v, phi)
    logit = np.where(mask, logit, -1e9)
    return logit, (u, v, z, g)


def head_loss_back(P, name, t, phi, mask, label, cache, G, scale):
    tx = t[:, :KDRAFT] if name == 'draft' else t
    logit_cache = cache
    u, v, z, g = logit_cache[1]
    logit = logit_cache[0]
    B = logit.shape[0]
    if name in SCALAR_HEADS:
        if logit.shape[1] != 1 or not np.all(mask):
            raise ValueError(f'{name} requires one unmasked scalar output per record')
        target = scalar_target(name, label)
        error = logit[:, 0] - target
        loss = np.square(error).mean()
        acc = scalar_accuracy(name, logit[:, 0], label, target).mean()
        d = (2.0 * error * scale / B)[:, None]
    else:
        mx = logit.max(1, keepdims=True)
        e = np.exp(logit - mx) * mask
        p = e / e.sum(1, keepdims=True)
        loss = -np.log(p[np.arange(B), label] + 1e-12).mean()
        acc = (logit.argmax(1) == label).mean()
        d = p.copy()
        d[np.arange(B), label] -= 1.0
        d *= scale / B
    du = np.einsum('bc,bck->bk', d, g)
    dz = (d[:, :, None] * u[:, None, :]) * dss(z)
    G[f'p_{name}'] += np.einsum('bcf,bck->fk', phi, dz)
    G[f'c_{name}'] += dz.sum((0, 1))
    dv = np.einsum('bc,bcf->bf', d, phi)
    G[f'u_{name}'] += tx.T @ du
    G[f'vv_{name}'] += tx.T @ dv
    dt = du @ P[f'u_{name}'].T + dv @ P[f'vv_{name}'].T
    if name == 'draft':
        padded = np.zeros_like(t)
        padded[:, :KDRAFT] = dt
        dt = padded
    return loss, acc, dt


# ---- data --------------------------------------------------------------------------------------------------

def validate_shards(files, shards):
    def scalar_equals(shard, key, expected):
        value = np.asarray(shard.get(key))
        return value.shape == () and value.item() == expected
    stale = [f.name for f, d in zip(files, shards) if not scalar_equals(d, 'schema', PACK_SCHEMA)]
    if stale:
        raise SystemExit(f'{len(stale)} shards need repacking for schema {PACK_SCHEMA} (first: {stale[0]})')
    uncovered = [f.name for f, d in zip(files, shards) if not scalar_equals(d, 'coverage_pass', 1)]
    if uncovered:
        raise SystemExit(f'{len(uncovered)} shards lack scalar coverage_pass=1 (first: {uncovered[0]})')
    for file, shard in zip(files, shards):
        counts = {}
        for key in ('input_count', 'eligible_policy_inputs', 'eligible_policy_labelled_inputs', 'excluded_inputs'):
            value = np.asarray(shard.get(key))
            if value.shape != () or not np.issubdtype(value.dtype, np.integer) or value.item() < 0:
                raise SystemExit(f'{file.name}: missing qualified source coverage count {key}')
            counts[key] = int(value.item())
        if (not scalar_equals(shard, 'coverage_scope', COVERAGE_SCOPE) or not scalar_equals(shard, 'source_facts_complete', 1)
                or counts['eligible_policy_inputs'] != counts['eligible_policy_labelled_inputs']
                or counts['eligible_policy_inputs'] + counts['excluded_inputs'] != counts['input_count']):
            raise SystemExit(f'{file.name}: qualified eligible-policy source coverage is incomplete')


class Head:
    """One head's records from every game shard, kept in float16 (cast per batch)."""

    def __init__(self, name, shards):
        parts = {k: [] for k in ('state', 'phi', 'n', 'label', 'won', 'game')}
        C = 0
        for gi, d in enumerate(shards):
            if f'{name}_n' not in d:
                continue
            for k in ('state', 'phi', 'n', 'label', 'won'):
                parts[k].append(d[f'{name}_{k}'])
            parts['game'].append(np.full(len(d[f'{name}_n']), gi, dtype=np.int32))
            C = max(C, d[f'{name}_phi'].shape[1])
        self.name = name
        self.empty = not parts['n']
        if self.empty:
            return
        N = sum(len(x) for x in parts['n'])
        self.phi = np.zeros((N, C, parts['phi'][0].shape[2]), dtype=np.float16)
        at = 0
        for x in parts['phi']:
            self.phi[at:at + len(x), :x.shape[1]] = x
            at += len(x)
        self.state = np.concatenate(parts['state'])
        self.n = np.concatenate(parts['n']).astype(np.int64)
        labels = np.concatenate(parts['label'])
        if name in SCALAR_HEADS:
            scalar_target(name, labels)
        self.label = labels.astype(np.int64)
        self.won = np.concatenate(parts['won'])
        self.game = np.concatenate(parts['game'])
        self.mask = np.arange(C)[None, :] < self.n[:, None]
        if name == 'priority':
            if C != 1 or self.phi.shape[2] != 4 or np.any(self.n != 1) or not np.all(self.phi[:, 0] == [1, 0, 0, 0]):
                raise ValueError('priority records require one constant candidate phi=[1,0,0,0]')
            priority_target(self.label)  # validate signed int32 bounds before any training
        elif name == 'quantity':
            if C != 1 or self.phi.shape[2] != 4 or np.any(self.n != 1) or not np.all(self.phi[:, 0, 0] == 1) or not np.all(np.isfinite(self.phi)):
                raise ValueError('quantity records require one candidate with four finite item facts and phi[0]=1')

    def batch(self, j):
        return self.state[j].astype(np.float32), self.phi[j].astype(np.float32), self.mask[j], self.label[j]

    def split(self, holdout):
        te = np.isin(self.game, holdout)
        self.train_idx = np.flatnonzero(~te)
        self.test_idx = np.flatnonzero(te)


def adam_step(P, G, M, V, t, lr, b1=0.9, b2=0.999, eps=1e-8):
    for k in P:
        M[k] = b1 * M[k] + (1 - b1) * G[k]
        V[k] = b2 * V[k] + (1 - b2) * G[k] * G[k]
        P[k] -= lr * (M[k] / (1 - b1 ** t)) / (np.sqrt(V[k] / (1 - b2 ** t)) + eps)


def evaluate(P, heads):
    out = {}
    for h in heads:
        idx = h.test_idx
        if len(idx) == 0:
            continue
        losses, accs, base = [], [], []
        for k in range(0, len(idx), 4096):
            j = idx[k:k + 4096]
            st, ph, mk, _ = h.batch(j)
            t, _ = draft_trunk(P, st) if h.name == 'draft' else trunk(P, st)
            logit, _ = head_forward(P, h.name, t, ph, mk)
            if h.name in SCALAR_HEADS:
                target = scalar_target(h.name, h.label[j])
                losses.append(np.square(logit[:, 0] - target))
                accs.append(scalar_accuracy(h.name, logit[:, 0], h.label[j], target))
                continue
            mx = logit.max(1, keepdims=True)
            e = np.exp(logit - mx) * h.mask[j]
            p = e / e.sum(1, keepdims=True)
            losses.append(-np.log(p[np.arange(len(j)), h.label[j]] + 1e-12))
            accs.append(logit.argmax(1) == h.label[j])
            base.append(1.0 / h.n[j])
        out[h.name] = (float(np.concatenate(losses).mean()), float(np.concatenate(accs).mean()), float(np.concatenate(base).mean()) if base else 0.0)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--steps', type=int, default=20000)
    ap.add_argument('--batch', type=int, default=512)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--value-weight', type=float, default=0.5)
    ap.add_argument('--out', default=str(ROOT / '.cache/main/zero/imitation.txt'))
    ap.add_argument('--init')
    ap.add_argument('--pack-dir', type=Path, default=PACK)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--max-games', type=int, default=0, help='train on the first N non-held-out games only (learning curves)')
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    files = sorted(f for f in (a.pack_dir / 'games').glob('*.npz') if not f.name.endswith('.tmp.npz'))
    if not files:
        raise SystemExit('no packed game shards')
    verify_manifest(pack_dir=a.pack_dir, exact=True)
    games = [f.stem for f in files]
    shards = [dict(np.load(f)) for f in files]
    validate_shards(files, shards)
    checkpoint_provenance = command_checkpoint_provenance(a.pack_dir, files, shards)
    # Held out: a stable tenth of the matches by id (the same games however many are added later).
    holdout = np.array([i for i, g in enumerate(games) if zlib.crc32(g.encode()) % 10 == 0])
    print(f'{len(games)} games, {len(holdout)} held out', flush=True)
    heads = []
    for name in HEADS:
        h = Head(name, shards)
        if h.empty:
            continue
        h.split(holdout)
        if a.max_games:
            keep = np.setdiff1d(np.arange(len(games)), holdout)[:a.max_games]
            h.train_idx = h.train_idx[np.isin(h.game[h.train_idx], keep)]
        heads.append(h)
        print(f'{name}: {len(h.train_idx)} train, {len(h.test_idx)} test, up to {h.phi.shape[1]} candidates', flush=True)
    del shards  # the heads hold their own copies
    P = load(Path(a.init)) if a.init else init(rng)
    M = {k: np.zeros_like(v) for k, v in P.items()}
    V = {k: np.zeros_like(v) for k, v in P.items()}
    t0 = time.time()
    for step in range(1, a.steps + 1):
        G = {k: np.zeros_like(v) for k, v in P.items()}
        stats = []
        for h in heads:
            j = rng.choice(h.train_idx, size=min(a.batch, len(h.train_idx)), replace=False)
            st, ph, mk, lb = h.batch(j)
            t, cache = draft_trunk(P, st) if h.name == 'draft' else trunk(P, st)
            fwd = head_forward(P, h.name, t, ph, mk)
            loss, acc, dt = head_loss_back(P, h.name, t, ph, mk, lb, fwd, G, 1.0)
            # Value head on the same states (results known: won 0/1).
            w = h.won[j]
            ok = w >= 0
            if ok.any():
                vl = t @ P['v'] + P['v0'][0]
                pv = 1.0 / (1.0 + np.exp(-vl))
                dv = np.where(ok, (pv - (w == 1)), 0.0) * a.value_weight / max(1, ok.sum())
                G['v'] += t.T @ dv
                G['v0'] += dv.sum()
                dt = dt + dv[:, None] * P['v'][None, :]
            if h.name == 'draft':
                draft_trunk_back(P, cache, dt, G)
            else:
                trunk_back(P, cache, dt, G)
            stats.append((h.name, loss, acc))
        adam_step(P, G, M, V, step, a.lr)
        if step % 500 == 0 or step == 1:
            print(f'step {step} ({time.time() - t0:.0f}s) ' + ' '.join(f'{n} mse {l:.4g} sign {c:.2f}' if n == 'priority' else f'{n} mse {l:.4g} rounded_acc {c:.2f}' if n == 'quantity' else f'{n} {l:.3f}/{c:.2f}' for n, l, c in stats), flush=True)
        if step % 5000 == 0 or step == a.steps:
            ev = evaluate(P, heads)
            print('  held-out: ' + ' '.join(f'{n} mse {l:.4g} sign {c:.3f}' if n == 'priority' else f'{n} mse {l:.4g} rounded_acc {c:.3f}' if n == 'quantity' else f'{n} loss {l:.3f} acc {c:.3f} (uniform {b:.3f})' for n, (l, c, b) in ev.items()), flush=True)
            save_command_checkpoint(P, Path(a.out), provenance=checkpoint_provenance, step=step)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
