"""Read-only root confusion on the settled source pack, never a fit or natural-play claim."""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path

import numpy as np

import student_pack as pack
import student_train as train

NAMES = ('END', 'MOVE', 'ATTACK', 'CAST0', 'CAST1', 'BUY', 'RECALL', 'TRIBUTE', 'CANCEL',
         'REPLAN', 'BUY_CONSUMABLE', 'USE_CONSUMABLE', 'WARD', 'SELL', 'PRIORITY')
SUPPORTED = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 14)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def counts(values):
    return {NAMES[int(key)]: int(value) for key, value in sorted(Counter(map(int, values)).items())}


def summary(rows, indices, labels, prediction, legal, scores):
    indices = np.asarray(indices, dtype=np.int64)
    if not len(indices):
        return {'n': 0}
    target, predicted = labels[indices], prediction[indices]
    eligible = legal[indices, target]
    confusion = {}
    for teacher, chosen in zip(target, predicted):
        bucket = confusion.setdefault(NAMES[int(teacher)], {})
        key = NAMES[int(chosen)]
        bucket[key] = bucket.get(key, 0) + 1
    margins = []
    for index in indices:
        available = scores[index, legal[index]]
        if len(available) > 1:
            top = np.partition(available, -2)[-2:]
            margins.append(float(top.max() - top.min()))
    useful = np.isin(target, (1, 2))
    return {'n': len(indices), 'teacher': counts(target), 'predicted': counts(predicted),
            'labels_in_mask': int(eligible.sum()), 'labels_outside_mask': int((~eligible).sum()),
            'accuracy_all_source_labels': float(np.mean(target == predicted)),
            'accuracy_on_labels_in_mask': float(np.mean(target[eligible] == predicted[eligible])) if eligible.any() else None,
            'source_non_end_predicted_end': int(np.sum((target != 0) & (predicted == 0))),
            'source_move_attack': int(useful.sum()),
            'source_move_attack_predicted_end': int(np.sum(useful & (predicted == 0))),
            'source_move_attack_predicted_priority': int(np.sum(useful & (predicted == 14))),
            'mean_greedy_logit_margin': float(np.mean(margins)) if margins else None,
            'confusion': confusion}


def conditions(row):
    state = row.state
    command_index = row.history['command_index_before']
    if command_index != int(round(float(state[25]) * 8)):
        raise ValueError('recorded and encoded command history disagree')
    return {'command_index': command_index, 'alive': bool(state[5] > 0),
            'channel': ('none', 'recall', 'tribute', 'teleport')[int(round(float(state[22])))],
            'underfoot_group': bool(state[29] >= 0), 'underfoot_status_encoded': float(state[29]),
            'carrying_gems': bool(state[11] > 0),
            'team': row.team}


def masked(logits, mask):
    legal = train.command_mask(mask, len(logits))
    return legal, np.where(legal, logits, -np.inf).argmax(1)


def examples(rows, labels, logits, semantic_prediction, supported_prediction, semantic_legal, supported_legal):
    cases = {
        'premature_END_semantic': (semantic_prediction == 0) & (labels != 0),
        'premature_END_supported_on_MOVE_ATTACK': (supported_prediction == 0) & np.isin(labels, (1, 2)),
        'PRIORITY_supported_on_MOVE_ATTACK': (supported_prediction == 14) & np.isin(labels, (1, 2)),
        'unsupported_teacher_family': ~np.isin(labels, (0, 1, 2, 14)),
        'unsupported_semantic_choice_on_supported_teacher': ~np.isin(semantic_prediction, (0, 1, 2, 14)) & np.isin(labels, (0, 1, 2, 14)),
        'supported_mask_changes_semantic_to_END': (semantic_prediction != supported_prediction) & (supported_prediction == 0),
        'MOVE_ATTACK_cross_confusion': np.isin(labels, (1, 2)) & np.isin(supported_prediction, (1, 2)) & (labels != supported_prediction),
    }
    result = {}
    for category, selected in cases.items():
        chosen = np.flatnonzero(selected)
        items = []
        for index in chosen[:5]:
            row = rows[int(index)]
            item = {'decision_origin': row.decision_origin, 'label_origin': row.label_origin,
                    'teacher': NAMES[int(labels[index])], 'semantic_prediction': NAMES[int(semantic_prediction[index])],
                    'supported_prediction': NAMES[int(supported_prediction[index])], 'conditions': conditions(row),
                    'raw_rule_mask': row.rule_mask, 'profile_semantic_mask': row.mask}
            for name, legal in (('semantic', semantic_legal), ('supported', supported_legal)):
                order = np.flatnonzero(legal[index])
                order = order[np.argsort(-logits[index, order], kind='stable')]
                item[name + '_top_scores'] = [{'command': NAMES[int(command)], 'logit': float(logits[index, command])}
                                            for command in order[:4]]
            items.append(item)
        result[category] = {'n': int(len(chosen)), 'first_origins': items}
    return result


def compact_findings(result):
    trained = result['models']['trained']['profiles']
    initial = result['models']['initial']['profiles']
    supported = trained['fixed_supported_families']['all']
    semantic = trained['profile_semantic']['all']
    return {'source_only': True,
        'trained_semantic_accuracy': semantic['accuracy_all_source_labels'],
        'initial_semantic_accuracy': initial['profile_semantic']['all']['accuracy_all_source_labels'],
        'trained_supported_accuracy_on_supported_labels': supported['accuracy_on_labels_in_mask'],
        'initial_supported_accuracy_on_supported_labels': initial['fixed_supported_families']['all']['accuracy_on_labels_in_mask'],
        'teacher_labels_outside_supported': supported['labels_outside_mask'],
        'source_MOVE_ATTACK_labels': supported['source_move_attack'],
        'trained_MOVE_ATTACK_to_END': supported['source_move_attack_predicted_end'],
        'trained_MOVE_ATTACK_to_PRIORITY': supported['source_move_attack_predicted_priority'],
        'source_nonEND_to_END_by_teacher': {teacher: values.get('END', 0) for teacher, values in supported['confusion'].items()
                                          if teacher != 'END' and values.get('END', 0)},
        'carrying_gems_source_rows': trained['profile_semantic']['strata']['carrying_gems']['true']['n'],
        'interpretation': 'Source-relative END disagreements occur on priority/unsupported-family labels, with none on MOVE/ATTACK. Fixed family masking is a substantial domain mismatch, but does not establish the cause of natural-play inactivity. Source1061 is learned correctly offline.'}


def diagnose(args):
    files = pack.game_shards(args.pack_dir)
    shards = []
    for file in files:
        with np.load(file, allow_pickle=False) as data:
            shards.append(dict(data))
    provenance = pack.source_provenance(args.pack_dir, files, shards)
    all_records = [record for shard in shards for record in pack.records_from_shard(shard)]
    rows = [row for row in all_records if row.kind == pack.COMMAND]
    labels = np.array([row.label for row in rows], dtype=np.int64)
    state = np.stack([row.state for row in rows])
    context = np.concatenate([row.context_phi for row in rows])
    offsets = np.concatenate(([0], np.cumsum([len(row.context_phi) for row in rows], dtype=np.int64)))
    semantic_mask = np.array([row.mask for row in rows], dtype=np.uint32)
    raw_mask = np.array([row.rule_mask for row in rows], dtype=np.uint32)
    supported_mask = semantic_mask & SUPPORTED
    facts = [conditions(row) for row in rows]
    strata = {'command_index': {'0': [], '>0': []}, 'alive': {'true': [], 'false': []},
              'channel': {}, 'underfoot_group': {'true': [], 'false': []},
              'carrying_gems': {'true': [], 'false': []}, 'team': {'0': [], '1': []},
              'teacher_supported': {'true': [], 'false': []},
              'combined': {'alive_nochannel_index0': [], 'alive_nochannel_indexgt0': []}}
    for index, fact in enumerate(facts):
        strata['command_index']['0' if fact['command_index'] == 0 else '>0'].append(index)
        for key in ('alive', 'underfoot_group', 'carrying_gems'):
            strata[key][str(fact[key]).lower()].append(index)
        strata['channel'].setdefault(fact['channel'], []).append(index)
        strata['team'][str(fact['team'])].append(index)
        strata['teacher_supported']['true' if SUPPORTED & (1 << labels[index]) else 'false'].append(index)
        if fact['alive'] and fact['channel'] == 'none':
            strata['combined']['alive_nochannel_index0' if fact['command_index'] == 0 else 'alive_nochannel_indexgt0'].append(index)
    models = {}
    point_index = next(index for index, row in enumerate(rows) if row.input == 1061)
    point_root = rows[point_index]
    point_argument = next(row for row in all_records if row.head == 2 and row.input == 1061)
    if (point_root.state.tobytes() != point_argument.state.tobytes()
            or point_root.context_phi.tobytes() != point_argument.context_phi.tobytes()):
        raise ValueError('1061 root/argument do not share exact source facts')
    source_manifest = pack.verify_manifest(args.pack_dir / 'manifest.json')
    source_path = Path(source_manifest['label_source_refs'][0]['path'])
    source_point = None
    with gzip.open(source_path, 'rt', encoding='utf-8') as source:
        for line in source:
            value = json.loads(line)
            if value.get('type') == 'input' and value.get('input') == 1061:
                source_point = value
                break
    if source_point is None:
        raise ValueError('settled source1061 not found')
    for name, path in (('trained', args.trained), ('initial', args.initial)):
        loaded, _ = train._verified_initialization(path)
        if loaded.config[8] != 1:
            raise ValueError('diagnosis requires the actual cfg8 attention checkpoint')
        embedding, _ = train.embed(loaded.params, state, context, offsets)
        logits = train.command_logits(loaded.params, embedding)
        semantic_legal, semantic_prediction = masked(logits, semantic_mask)
        supported_legal, supported_prediction = masked(logits, supported_mask)
        _, raw_prediction = masked(logits, raw_mask)
        argument_scores, _ = train.family_logits(loaded.params, embedding[point_index:point_index + 1],
            'attack', point_argument.phi, np.array([0, point_argument.n], dtype=np.int64))
        selected_argument = int(argument_scores.argmax())
        point = {'decision_origin': point_root.decision_origin, 'conditions': conditions(point_root),
            'teacher': 'ATTACK', 'raw_rule_mask': point_root.rule_mask, 'profile_semantic_mask': point_root.mask,
            'fixed_supported_mask': int(supported_mask[point_index]),
            'logits': {command: float(logits[point_index, index]) for index, command in enumerate(NAMES)},
            'semantic_prediction': NAMES[int(semantic_prediction[point_index])],
            'supported_prediction': NAMES[int(supported_prediction[point_index])],
            'attack_arguments': {'n': point_argument.n, 'true_index': point_argument.label,
                'selected_index': selected_argument, 'selected_matches_source': selected_argument == point_argument.label,
                'true_payload_kind_target_count_x_y': point_argument.payload[point_argument.label].tolist(),
                'selected_payload_kind_target_count_x_y': point_argument.payload[selected_argument].tolist(),
                'true_score': float(argument_scores[point_argument.label]), 'selected_score': float(argument_scores[selected_argument]),
                'all_scores': argument_scores.tolist(), 'all_payloads': point_argument.payload.tolist()},
            'source_gold_before': source_point['before']['self']['gold'],
            'source_gold_after': source_point['afterSelf']['gold'],
            'official_successor_proof': '.cache/main/zero/teacher-force/e91d966c0a074323a0c460e7abf919fc/result.json',
            'scope': 'Offline source-priority and argument diagnosis only; no actual fuel, admission or lifecycle proof.'}
        profiles = {}
        for domain, legal, prediction in (('profile_semantic', semantic_legal, semantic_prediction),
                                          ('fixed_supported_families', supported_legal, supported_prediction)):
            profiles[domain] = {'all': summary(rows, range(len(rows)), labels, prediction, legal, logits),
                               'strata': {key: {value: summary(rows, indices, labels, prediction, legal, logits)
                                               for value, indices in groups.items()} for key, groups in strata.items()}}
        models[name] = {'path': str(path.resolve()), 'sha256': digest(path), 'profiles': profiles,
                        'raw_semantic_including_internal_REPLAN': {'predicted': counts(raw_prediction),
                            'REPLAN_predicted': int(np.sum(raw_prediction == 9)),
                            'note': 'Diagnostic only; cfg7 excludes internal REPLAN in the actual policy profile.'},
                        'failure_categories': examples(rows, labels, logits, semantic_prediction, supported_prediction,
                                                       semantic_legal, supported_legal),
                        'source1061': point}
    result = {'schema': 'main.zero.source_root_diagnostic/v1', 'policy_profile': train.POLICY_PROFILE,
            'source_scope': {'inputs': [21, 2256], 'root_decisions': len(rows), 'raw_events': 2236,
                             'inverse_ambiguous23': 26, 'exogenous26': 22},
            'dataset_refs': provenance['dataset_refs'], 'models': models,
            'definitions': {'profile_semantic': 'Captured semantic rule mask intersected with cfg7 no-REPLAN profile.',
                'fixed_supported_families': 'Semantic/profile mask intersected with END/MOVE/ATTACK/PRIORITY. This is a hypothetical family-domain mask, not actual fuel admission.',
                'underfoot_group': 'Encoded state29>=0: alive actor on a static public group tile; unknown status remains distinct.',
                'strata_fields': 'State5 alive,11 gem carry flag,22 channel kind,29 underfoot status; exact metadata command_index_before and acting team.'},
            'claim_scope': 'Full settled source confusion only, no natural-play competence or safety claim; no optimization or feature reconstruction.'}
    result['findings'] = compact_findings(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack-dir', type=Path, required=True)
    parser.add_argument('--trained', type=Path, required=True)
    parser.add_argument('--initial', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = diagnose(args)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(result['findings']))


if __name__ == '__main__':
    main()
