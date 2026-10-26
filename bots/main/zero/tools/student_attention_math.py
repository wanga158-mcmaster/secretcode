"""The cfg8 camp-attention encoder and its exact analytic gradient; no training driver.

shapes()/init() describe only the 184 appended parameters. embed()/embed_back() use the existing
base encoder parameters too. All pointer and command losses remain in student_train.py. Context
rows are concatenated across a batch with offsets[N+1]; every row participates in one softmax.
"""
from __future__ import annotations

import math
import numpy as np

STATE, HIDDEN, CONTEXT, ATTENTION = 48, 16, 12, 4
BASE_END, PARAMETERS, WEIGHT_END = 30250, 184, 30434
FEATURE_SCHEMA, LAYOUT_VERSION = 3, 2
MODEL_ABI = 'main.zero.student.attention.v1'
POLICY_PROFILE = 'unconditional_camp_attention_v1'
HEADS = ('goal', 'step', 'attack', 'cast0', 'cast1', 'shop', 'draft', 'aux', 'priority', 'quantity')


def shapes():
    return [('att_w_e', (CONTEXT, ATTENTION)), ('att_b_e', (ATTENTION,)),
            ('att_w_q', (HIDDEN, ATTENTION)), ('att_b_q', (ATTENTION,)),
            ('att_w_r', (ATTENTION, HIDDEN))]


def layout():
    at, out = BASE_END, {}
    for name, shape in shapes():
        out[name] = at
        at += math.prod(shape)
    assert at == WEIGHT_END and at - BASE_END == PARAMETERS
    return out


def init(rng=None):
    rng = rng if isinstance(rng, np.random.Generator) else np.random.default_rng(rng)
    return {name: (rng.normal(0, math.sqrt(1 / shape[0]), shape).astype(np.float32)
                   if len(shape) == 2 else np.zeros(shape, dtype=np.float32))
            for name, shape in shapes()}


def full_shapes():
    out = [('w1', (STATE, HIDDEN)), ('b1', (HIDDEN,)),
           ('w2', (HIDDEN, HIDDEN)), ('b2', (HIDDEN,)),
           ('command_w', (HIDDEN, 15)), ('command_b', (15,))]
    for head in HEADS:
        out += [(head + '_w', (HIDDEN, 24)), (head + '_b', (24,))]
    return out + [('v', (HIDDEN,)), ('v0', (1,)), *shapes()]


def flatten(P):
    return np.concatenate([np.asarray(P[name]).reshape(-1) for name, _ in full_shapes()])


def unflatten(values):
    values = np.asarray(values)
    if values.ndim != 1 or len(values) != 5408 + PARAMETERS:
        raise ValueError('attention values must contain all 5592 parameters')
    at, out = 0, {}
    for name, shape in full_shapes():
        count = math.prod(shape)
        out[name] = values[at:at + count].reshape(shape).copy()
        at += count
    return out


def _act(value):
    return value / (1 + np.abs(value))


def _act_grad(value):
    return 1 / np.square(1 + np.abs(value))


def _inputs(state, context_phi, context_offsets):
    state, context_phi, offsets = map(np.asarray, (state, context_phi, context_offsets))
    if state.ndim != 2 or state.shape[1] != STATE or not np.isfinite(state).all():
        raise ValueError('attention state must be finite [N,48]')
    if context_phi.ndim != 2 or context_phi.shape[1] != CONTEXT or not np.isfinite(context_phi).all():
        raise ValueError('attention context must be finite [M,12]')
    if (offsets.ndim != 1 or len(offsets) != len(state) + 1
            or not np.issubdtype(offsets.dtype, np.integer) or offsets[0] != 0
            or offsets[-1] != len(context_phi) or np.any(offsets[1:] < offsets[:-1])):
        raise ValueError('attention offsets must be monotonic [N+1] from zero through M')
    return state, context_phi, offsets.astype(np.int64, copy=False)


def embed(P, state, context_phi, context_offsets):
    state, context_phi, offsets = _inputs(state, context_phi, context_offsets)
    first_pre = state @ P['w1'] + P['b1']
    first = _act(first_pre)
    base_pre = first @ P['w2'] + P['b2']
    base = _act(base_pre)
    query = base @ P['att_w_q'] + P['att_b_q']
    entity_pre = context_phi @ P['att_w_e'] + P['att_b_e']
    entity = _act(entity_pre)
    owner = np.repeat(np.arange(len(state)), np.diff(offsets))
    scores = np.sum(entity * query[owner], axis=1)
    alpha = np.zeros_like(scores)
    pool = np.zeros((len(state), ATTENTION), dtype=np.result_type(entity, query))
    for sample, (begin, end) in enumerate(zip(offsets[:-1], offsets[1:])):
        if begin == end:
            continue
        mass = np.exp(scores[begin:end] - np.max(scores[begin:end]))
        alpha[begin:end] = mass / mass.sum()
        pool[sample] = np.sum(alpha[begin:end, None] * entity[begin:end], axis=0)
    output_pre = base_pre + pool @ P['att_w_r']
    output = _act(output_pre)
    if not np.isfinite(output).all():
        raise ValueError('attention encoder produced nonfinite output')
    return output, {'state': state, 'context_phi': context_phi, 'offsets': offsets, 'owner': owner,
                    'first_pre': first_pre, 'first': first, 'base_pre': base_pre, 'base': base,
                    'query': query, 'entity_pre': entity_pre, 'entity': entity,
                    'alpha': alpha, 'pool': pool, 'output_pre': output_pre}


def embed_back(P, cache, derivative, G):
    derivative = np.asarray(derivative)
    if derivative.shape != cache['output_pre'].shape or not np.isfinite(derivative).all():
        raise ValueError('attention derivative must be finite [N,16]')
    owner, entity, alpha = cache['owner'], cache['entity'], cache['alpha']
    dpre = derivative * _act_grad(cache['output_pre'])
    G['att_w_r'] += cache['pool'].T @ dpre
    dpool = dpre @ P['att_w_r'].T
    dalpha = np.sum(entity * dpool[owner], axis=1)
    average = np.zeros(len(dpre), dtype=np.result_type(dpre, entity))
    np.add.at(average, owner, alpha * dalpha)
    dscore = alpha * (dalpha - average[owner])
    dquery = np.zeros_like(cache['query'])
    np.add.at(dquery, owner, dscore[:, None] * entity)
    dentity = alpha[:, None] * dpool[owner] + dscore[:, None] * cache['query'][owner]
    dentity_pre = dentity * _act_grad(cache['entity_pre'])
    G['att_w_e'] += cache['context_phi'].T @ dentity_pre
    G['att_b_e'] += dentity_pre.sum(0)
    G['att_w_q'] += cache['base'].T @ dquery
    G['att_b_q'] += dquery.sum(0)
    dpre += (dquery @ P['att_w_q'].T) * _act_grad(cache['base_pre'])
    G['w2'] += cache['first'].T @ dpre
    G['b2'] += dpre.sum(0)
    dfirst_pre = (dpre @ P['w2'].T) * _act_grad(cache['first_pre'])
    G['w1'] += cache['state'].T @ dfirst_pre
    G['b1'] += dfirst_pre.sum(0)
