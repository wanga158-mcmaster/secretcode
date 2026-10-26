"""Paired camp-attention activity smoke through arena exp; never a runner or strength gate.

Default invocation prints the validated recipe. --execute runs one private experiment and closes
it after recording its answer, including failures. No training is performed here. The untrained
input must reproduce the qualified fresh fit's seeded student initialization exactly.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / 'tools/arena'
PROFILE = 'unconditional_camp_attention_v1'
OPPONENT = 'anchors/main-v53'
LABELS = ('untrained', 'trained')
CACHE_LABELS = ('uncached', 'cached')
ACTIVITY_FIELDS = ('gold', 'monster_damage', 'collected', 'gems')


class OwnedArenaExitUnconfirmed(RuntimeError):
    """Do not close/prune inputs while an owned experiment process might still be alive."""


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8', newline='\n')


def runtime_closure(source_root=None):
    source_root = Path(source_root) if source_root is not None else ROOT / 'bots/main/zero/src'
    return {'bots/main/zero/src/' + path.relative_to(source_root).as_posix(): digest(path)
            for path in sorted(source_root.rglob('*'))
            if path.is_file() and '.cambc' not in path.relative_to(source_root).parts}


def validate_models(trained_path, untrained_path):
    # Reuse the trainer's source/data/deployment contract. This adds no predictive-accuracy or
    # activity condition before fitting; activity is the outcome of the experiment below.
    import student_train as train
    trained_path, untrained_path = trained_path.resolve(strict=True), untrained_path.resolve(strict=True)
    trained, verification = train._verified_initialization(trained_path)
    sidecar = trained_path.with_name(trained_path.name + '.model.json')
    sidecar_bytes = sidecar.read_bytes()
    if hashlib.sha256(sidecar_bytes).hexdigest() != verification['sidecar_sha256'] \
            or digest(trained_path) != verification['sha256']:
        raise ValueError('trained checkpoint or provenance changed after trainer verification')
    metadata = json.loads(sidecar_bytes)
    if metadata.get('policy_profile') != PROFILE or train.POLICY_PROFILE != PROFILE:
        raise ValueError('activity smoke requires unconditional_camp_attention_v1 source and runtime profile')
    if metadata.get('feature_schema') != 3 or metadata.get('layout_version') != 2 \
            or metadata.get('layout', {}).get('student_parameters') != 5592 \
            or metadata.get('layout', {}).get('student_end') != 30434:
        raise ValueError('activity smoke requires the appended 5592-parameter attention graph')
    canary = metadata.get('canary', {})
    if (type(canary.get('steps')) is not int or canary['steps'] <= 0
            or canary.get('optimizer') != 'Adam' or type(canary.get('seed')) is not int):
        raise ValueError('trained input needs a real finite BC optimizer record')
    if metadata.get('initialization', {}).get('kind') != 'zero_reserved_legacy_prefix':
        raise ValueError('this first-BC smoke needs a fresh fit and its untrained seeded control; resumed fits need their actual initial checkpoint')
    untrained_hash = digest(untrained_path)
    untrained = train.load(untrained_path)
    if digest(untrained_path) != untrained_hash:
        raise ValueError('untrained checkpoint changed while loading its control parameters')
    np = train.np
    expected = train.init(np.random.default_rng(canary['seed']), attention=True)
    if (not np.array_equal(train.flatten(untrained.params), train.flatten(expected))
            or not np.array_equal(untrained.legacy_prefix, trained.legacy_prefix)
            or np.any(trained.legacy_prefix != 0)):
        raise ValueError('untrained control does not reproduce the fresh fit seed and reserved prefix')
    initial_hashes = train._initial_weight_hashes(untrained.params, untrained.legacy_prefix)
    if any(canary.get(key) is not None and canary[key] != value for key, value in initial_hashes.items()):
        raise ValueError('untrained control differs from the fit-recorded pre-update weight hashes')
    control_provenance = {}
    if canary.get('initial_control') is not None:
        control = canary['initial_control']
        if (Path(control['path']).resolve() != untrained_path or control.get('sha256') != untrained_hash
                or control.get('optimizer_updates') != 0):
            raise ValueError('untrained input differs from the fit-recorded zero-update control')
        control_sidecar = Path(control['sidecar_path']).resolve(strict=True)
        control_sidecar_bytes = control_sidecar.read_bytes()
        if hashlib.sha256(control_sidecar_bytes).hexdigest() != control['sidecar_sha256']:
            raise ValueError('fit-recorded initial control sidecar changed')
        control_metadata = json.loads(control_sidecar_bytes)
        if (control_metadata.get('artifact_role') != 'pre_bc_initialization_control'
                or control_metadata.get('optimizer_updates') != 0):
            raise ValueError('initial control sidecar does not describe pre-update weights')
        control_provenance = {'sidecar_path': str(control_sidecar), 'sidecar_sha256': control['sidecar_sha256']}
    for checkpoint in (trained, untrained):
        if any(checkpoint.config[index] != 1 for index in (5, 6, 7, 8)):
            raise ValueError('both checkpoints must enable configs 5, 6, 7 and 8')
        if len(checkpoint.config) != 16 or checkpoint.config[0] != 0 or checkpoint.config[4] != 0:
            raise ValueError('activity comparison requires deterministic greedy inference with telemetry config4 off')
    if not np.array_equal(trained.config, untrained.config):
        raise ValueError('control and trained configs must match exactly')
    source_hashes = metadata['feature_source_sha256']
    runtime_sources = {}
    for relative, expected_hash in source_hashes.items():
        normalized = str(relative).replace('\\', '/')
        if not normalized.startswith('bots/main/zero/src/'):
            continue
        current = (ROOT / normalized).resolve(strict=True)
        if not current.is_relative_to(ROOT / 'bots/main/zero') or digest(current) != expected_hash:
            raise ValueError(f'current runtime differs from qualified feature source: {relative}')
        runtime_sources[normalized] = expected_hash
    if not runtime_sources:
        raise ValueError('qualified feature provenance does not identify the runtime source closure')
    if digest(trained_path) != verification['sha256'] or digest(sidecar) != verification['sidecar_sha256']:
        raise ValueError('trained checkpoint or sidecar changed during activity input validation')
    return {'trained': {'path': str(trained_path), 'sha256': verification['sha256'],
                        'sidecar_path': str(sidecar), 'sidecar_sha256': verification['sidecar_sha256']},
            'untrained': {'path': str(untrained_path), 'sha256': untrained_hash,
                          'initialization_seed': canary['seed'], 'kind': 'exact_fresh_seed_control',
                          **initial_hashes, **control_provenance},
            'model_abi': train.MODEL_ABI, 'feature_schema': train.FEATURE_SCHEMA,
            'policy_profile': PROFILE, 'layout': metadata['layout'],
            'verified_checkpoint': verification, 'runtime_sources': runtime_sources,
            'deployment_receipt': metadata.get('deployment_receipt')}


def validate_cache_pair(pair_path):
    """Verify an engineering-only config9 intervention against its unchanged fitted parent."""
    import student_train as train
    pair_path = pair_path.resolve(strict=True)
    pair = json.loads(pair_path.read_text(encoding='utf-8'))
    if pair.get('schema') != 'main.zero.runtime_variant_pair/v1':
        raise ValueError('cache mode requires the immutable student_variant pair')
    for record in (pair['parent_weights'], pair['parent_sidecar']):
        if digest(record['path']) != record['sha256']:
            raise ValueError('original fitted parent changed after engineering variant creation')
    parent, parent_verification = train._verified_initialization(pair['parent_weights']['path'])
    parent_metadata = json.loads(Path(pair['parent_sidecar']['path']).read_text(encoding='utf-8'))
    if parent_verification['sha256'] != pair['parent_weights']['sha256'] \
            or parent_verification['sidecar_sha256'] != pair['parent_sidecar']['sha256']:
        raise ValueError('variant parent differs from its verified fitted provenance')
    np = train.np

    def block(path):
        values = np.asarray(Path(path).read_text(encoding='utf-8').split(), dtype='<f4')
        if len(values) != 16 + 32768 or not np.isfinite(values).all():
            raise ValueError('cache comparison requires the complete finite32784-float payload')
        return values

    parent_block = block(pair['parent_weights']['path'])
    learned = parent_block[16:].view('<u4')
    learned_sha = hashlib.sha256(parent_block[16:].tobytes()).hexdigest()
    if learned_sha != pair['learned_block_sha256']:
        raise ValueError('pair learned-block hash differs from its original fitted parent')
    inputs = {}
    for label, key, enabled in (('uncached', 'off', 0), ('cached', 'on', 1)):
        record = pair[key]
        loaded, verification = train._verified_initialization(record['path'])
        if verification['sha256'] != record['sha256'] or verification['sidecar_sha256'] != record['sidecar_sha256']:
            raise ValueError('cache variant weights/sidecar changed')
        values = block(record['path'])
        keep = [index for index in range(16) if index != 9]
        if (values[9] != enabled or not np.array_equal(values[16:].view('<u4'), learned)
                or not np.array_equal(values[:16].view('<u4')[keep], parent_block[:16].view('<u4')[keep])):
            raise ValueError('engineering comparison changed learned bits or a config other than9')
        if any(loaded.config[index] != 1 for index in (5, 6, 7, 8)):
            raise ValueError('cache variants must retain the same attention policy profile')
        metadata = json.loads(Path(record['sidecar_path']).read_text(encoding='utf-8'))
        variant = metadata.get('runtime_variant', {})
        if (variant.get('schema') != 'main.zero.runtime_variant/v1'
                or variant.get('changed_config_indices') != [9]
                or variant.get('parent_weights') != pair['parent_weights']
                or variant.get('parent_sidecar') != pair['parent_sidecar']
                or variant.get('learned_block_sha256') != learned_sha
                or variant.get('optimizer_rerun') is not False):
            raise ValueError('cache variant lineage is not the declared no-fit config9 intervention')
        inputs[label] = {**record, 'config9': enabled}
    if parent_metadata.get('policy_profile') != PROFILE:
        raise ValueError('cache variants have the wrong fitted attention policy')
    return {**inputs, 'subject_labels': list(CACHE_LABELS), 'mode': 'context_cache_runtime',
            'model_abi': parent_metadata['model_abi'], 'feature_schema': parent_metadata['feature_schema'],
            'policy_profile': PROFILE, 'layout': parent_metadata['layout'],
            'runtime_sources': {}, 'verified_checkpoint': parent_verification,
            'deployment_receipt': parent_metadata.get('deployment_receipt'),
            'variant_pair': {'path': str(pair_path), 'sha256': digest(pair_path),
                             'parent_weights': pair['parent_weights'], 'parent_sidecar': pair['parent_sidecar'],
                             'learned_block_sha256': learned_sha, 'changed_config_indices': [9],
                             'all32768_learned_bits_identical': True, 'other15_config_bits_identical': True}}


def select_maps(core, settled_path):
    settled_path = settled_path.resolve(strict=True)
    settled = json.loads(settled_path.read_text(encoding='utf-8'))
    # Map selection consults only the pre-existing recipe, never its outcomes.
    pool = sorted(set(settled['maps']['pool']))[:2]
    if len(pool) != 2 or any(name not in core.map_set('pool') for name in pool):
        raise ValueError('settled recipe must supply at least two genuine pool keys')
    baseline_path = Path(settled['cache']) / 'aggregate.json'
    baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
    return {'pool': pool, 'train': []}, {
        'recipe_path': str(settled_path), 'recipe_sha256': digest(settled_path),
        'selection': 'first two lexicographically sorted pool keys from the completed first-BC recipe',
        'settled_aggregate_path': str(baseline_path), 'settled_aggregate_sha256': digest(baseline_path),
        'settled_trained_totals': baseline['subjects']['trained']['totals']}


def make_recipe(args, inputs, core):
    if not re.fullmatch(r'main/[A-Za-z0-9_.-]+', args.name):
        raise ValueError('experiment name must be main/NAME')
    selected, settled = select_maps(core, args.settled_recipe)
    maps = selected['pool'] + selected['train']
    cache = ROOT / '.cache/main/zero/activity' / args.name.split('/', 1)[1]
    models = cache / 'models'
    labels = tuple(inputs.get('subject_labels', LABELS))
    cache_mode = inputs.get('mode') == 'context_cache_runtime'
    command = [os.environ.get('COMSPEC', r'C:\Windows\System32\cmd.exe'), '/d', '/c', r'..\..\arena.cmd', 'exp', 'run', args.name,
               '-q', ('Does fact-identical context caching admit the same learned policy and yield first income?'
                      if cache_mode else 'Does learned camp attention pay on both sides of a fixed pool map beyond its fresh same-graph control?'),
               '-s', f'{labels[0]}=main/zero@{models / (labels[0] + ".txt")}',
               '-s', f'{labels[1]}=main/zero@{models / (labels[1] + ".txt")}',
               '--vs', OPPONENT, '--maps', ','.join(maps), '--sides', 'both',
               '--keep', 'none', '--private-snapshots']
    keys = len(maps) * 2
    engineering = {'bot_cpp_sha256': digest(ROOT / 'bots/main/zero/src/bot.cpp'),
                   'sdk_cpp_sha256': digest(ROOT / 'bots/main/zero/src/sdk.cpp'),
                   'runtime_source_sha256': runtime_closure()}
    if args.runtime_evidence is not None:
        evidence = args.runtime_evidence.resolve(strict=True)
        engineering['measured_scope_evidence'] = {'path': str(evidence), 'sha256': digest(evidence)}
    if args.runtime_root_seed is not None:
        engineering['declared_root_seed'] = args.runtime_root_seed
        engineering['seed_scope'] = 'Current engineering seed recorded after the referenced point; the referenced point did not test this seed.'
    if args.cache_fact_proof is not None:
        proof = args.cache_fact_proof.resolve(strict=True)
        engineering['cache_fact_parity'] = {'path': str(proof), 'sha256': digest(proof)}
    if args.uncached_root_seed is not None or args.cached_root_seed is not None:
        engineering['declared_root_seeds'] = {'uncached': args.uncached_root_seed, 'cached': args.cached_root_seed}
    bearing = args.bearing_evidence.resolve(strict=True)
    return {'schema': 'zero-student-attention-activity-recipe/v1', 'experiment': args.name,
            'driver_sha256': digest(Path(__file__)),
            'scope': 'Two fixed pool keys, both sides; paying-activity diagnostic, no strength or release claim.',
            'inputs': inputs, 'maps': selected, 'settled_baseline': settled,
            'mode': 'context_cache_runtime' if cache_mode else 'attention_learning', 'subject_labels': list(labels),
            'before_visibility_bearing_evidence': {'path': str(bearing), 'sha256': digest(bearing)},
            'coverage': {'maps': len(maps), 'opponents': 1, 'sides': ['A', 'B'],
                         'keys_per_subject': keys, 'subjects': 2, 'games': keys * 2},
            'forecast': {'paying_interaction_proxy': 'gold>0 AND monster_damage>0',
                         'prediction': ('Cache reduces context/admission cost enough for the first stopped actor state to MOVE and yields first gold on at least one paired game key.'
                                        if cache_mode else 'Both sides of at least one fixed map pay; its trained gold exceeds the fresh control on both paired keys.'),
                         'measured_health': 'zero subject/opponent exhausted, faults, errors, runner_error and truncation',
                         'comparison': 'Paired income/activity differences only; no win-rate or accuracy requirement.',
                         'stop_rule': ('Stop on measured health failure or no pre-root/full-budget admission improvement; no fit/data/math changes.'
                                       if cache_mode else 'Any measured health failure stops this work; all four trained zero-gold games require bounded traces before expanding data/maps/steps.')},
            'smoke_size_reason': 'Eight games answer this narrow paying-route question against settled zero-income evidence; no broad panel.',
            'private_snapshots': True, 'reuse_shared_snapshots': False,
            'runtime_engineering': engineering,
            'command': command, 'timeout_seconds': args.timeout, 'cache': str(cache)}


def active(row):
    return row['gold'] > 0 and row['monster_damage'] > 0


def aggregate_rows(recipe, exp):
    labels = tuple(recipe.get('subject_labels', LABELS))
    baseline_label, variant_label = labels
    cache_mode = recipe.get('mode') == 'context_cache_runtime'
    directory, spec = exp.load(recipe['experiment'])
    if spec.get('private_snapshots') is not True or spec.get('reuse_shared_snapshots') is not False:
        raise ValueError('recorded experiment did not preserve private inputs without shared reuse')
    _, expected = exp.game_plan(spec)
    rows = [json.loads(line) for line in (directory / 'rows.jsonl').read_text(encoding='utf-8').splitlines() if line.strip()]
    actual = exp.validate_progress_rows(rows, set(expected), spec=spec)
    if set(actual) != set(expected):
        raise ValueError(f'activity coverage incomplete: {len(actual)}/{len(expected)} rows')
    if spec['opponent_ids'] != [OPPONENT] or spec.get('sides') != 'both' or spec.get('rounds') != 0:
        raise ValueError('activity comparison changed its opponent, sides or full-game scope')
    if set(spec['provenance']['maps']) != set(recipe['maps']['pool'] + recipe['maps']['train']):
        raise ValueError('recorded maps differ from the predeclared paired smoke')
    if [subject['label'] for subject in spec['subjects']] != list(labels) \
            or any(subject['bot'] != 'main/zero' or subject.get('set') for subject in spec['subjects']):
        raise ValueError('recorded subjects differ from the same-runtime weighted comparison')
    for label in labels:
        binding = spec['provenance']['weights'][label]
        if binding['sha256'] != recipe['inputs'][label]['sha256'] \
                or Path(binding['path']).resolve() != (Path(recipe['cache']) / 'models' / f'{label}.txt').resolve():
            raise ValueError('injected weights differ from owner-private frozen model inputs')
    if spec['entrants'][variant_label] != spec['entrants'][baseline_label] \
            or spec['provenance']['wasm'][variant_label] != spec['provenance']['wasm'][baseline_label]:
        raise ValueError('trained and control did not execute the same compiled compact runtime')
    for label in (*labels, OPPONENT):
        if exp.recorded_snapshot_root(spec, label).resolve() != (ROOT / '.cache/main/snap').resolve():
            raise ValueError('experiment payload escaped its owner-private snapshot namespace')
    # Bind the actual executed private input tree, not just current-checkout names or point paths.
    entry = exp.core.cached_snapshot(spec['entrants'][baseline_label],
                                     snapshot_root=exp.recorded_snapshot_root(spec, baseline_label),
                                     snapshot_owner='main')
    if runtime_closure(entry.path / 'src') != recipe['runtime_engineering']['runtime_source_sha256']:
        raise ValueError('executed private runtime source differs from the recipe closure')
    groups = {label: {(row['opponent'], row['map'], row['side']): row for row in rows if row['subject'] == label}
              for label in labels}
    if set(groups[baseline_label]) != set(groups[variant_label]):
        raise ValueError('subjects do not share identical complete game keys')
    per_subject = {}
    for label, games in groups.items():
        for row in games.values():
            if any(field not in row for field in ACTIVITY_FIELDS):
                raise ValueError('activity telemetry is missing; do not silently count missing data as zero')
        count = len(games)
        nactive = sum(active(row) for row in games.values())
        per_subject[label] = {'games': count, 'active_games': nactive, 'active_fraction': nactive / count,
                              'active_wilson95': exp.stats.wilson(nactive, count),
                              'health_flagged_games': sum(bool(row['health']) for row in games.values()),
                              'opponent_health_flagged_games': sum(bool(row['opp_health']) for row in games.values()),
                              'truncated_games': sum(bool(row.get('truncated')) for row in games.values()),
                              'totals': {field: sum(row[field] for row in games.values()) for field in ACTIVITY_FIELDS}}
    paired_activity = exp.stats.paired(groups[baseline_label], groups[variant_label], win=active,
                                      margin=lambda row: float(active(row)))
    paired = {'activity': {'n': paired_activity['n'], 'gained': len(paired_activity['gained']),
                           'lost': len(paired_activity['lost']), 'mcnemar_p': paired_activity['mcnemar_p'],
                           'difference': paired_activity['diff_mean'], 'standard_error': paired_activity['diff_se']}}
    for field in ACTIVITY_FIELDS:
        comparison = exp.stats.paired(groups[baseline_label], groups[variant_label], margin=lambda row, field=field: row[field])
        paired[field] = {'baseline_mean': comparison['margin_base'], 'variant_mean': comparison['margin_var'],
                         'difference': comparison['diff_mean'], 'standard_error': comparison['diff_se']}
    health = all(not subject[key] for subject in per_subject.values()
                 for key in ('health_flagged_games', 'opponent_health_flagged_games', 'truncated_games'))
    paying_maps, unmatched_maps = [], []
    for name in recipe['maps']['pool']:
        keys = [(OPPONENT, name, side) for side in ('A', 'B')]
        if all(active(groups[variant_label][key]) for key in keys):
            paying_maps.append(name)
            if all(groups[variant_label][key]['gold'] > groups[baseline_label][key]['gold'] for key in keys):
                unmatched_maps.append(name)
    paying_keys = [key for key in groups[variant_label] if active(groups[variant_label][key])
                   and groups[variant_label][key]['gold'] > groups[baseline_label][key]['gold']]
    activity = bool(paying_keys) if cache_mode else bool(unmatched_maps)
    zero_income = per_subject[variant_label]['totals']['gold'] == 0
    outcome = ('measured_health_failure' if not health else 'paying_route_forecast_met' if activity
               else 'zero_income_inspect_bounded_traces' if zero_income else 'partial_paying_activity')
    return {'schema': 'zero-student-attention-activity-result/v1', 'experiment': recipe['experiment'],
            'outcome': outcome, 'clean_measured_health': health, 'activity_forecast_met': activity,
            'coverage': recipe['coverage'], 'subjects': per_subject, 'paired': paired,
            'mode': recipe.get('mode'), 'subject_labels': list(labels), 'paired_paying_progress_keys': paying_keys,
            'paying_maps_both_sides': paying_maps, 'variant_exceeds_baseline_both_sides': unmatched_maps,
            'next_step': ('stop and diagnose health' if not health else
                          'inspect bounded traces before any data/map/step expansion' if zero_income else
                          'inspect paying interaction witness; no automatic expansion'),
            'provenance': spec['provenance'], 'recipe_sha256': digest(Path(recipe['cache']) / 'recipe.json'),
            'scope': recipe['scope'],
            'limits': ['No capture-event counter or per-turn stall detector exists in arena rows.',
                       'Gold plus monster damage is an income proxy; a replay attributes the first paying camp event.',
                       'Collected gems and final gems are reported directly; they are not invented capture counts.',
                       'Private diagnostic inputs do not establish release qualification.']}


def arena_cli(arguments, directory, timeout, native):
    # Intentionally _command, NOT _slotted_command: arena CLI owns shared Slots/version locks.
    # The existing private Job contains the CLI and descendants, including its nested native Jobs.
    directory.mkdir(parents=True, exist_ok=False)
    try:
        result = native._command(arguments, ROOT / 'bots/main', directory, timeout)
    except BaseException as error:
        process = getattr(error, 'compiled_process', None)
        if process is not None:
            try:
                if not process.empty_confirmed:
                    process.terminate_tree()
                if not process.empty_confirmed:
                    raise RuntimeError('owned arena process tree is still alive')
                process.close()
            except BaseException as cleanup_error:
                raise OwnedArenaExitUnconfirmed('cannot close experiment before owned arena process exit is confirmed') from cleanup_error
        raise
    if not result.get('tree_exit_confirmed'):
        raise OwnedArenaExitUnconfirmed('owned arena process tree exit was not confirmed')
    summary = {key: result.get(key) for key in ('returncode', 'timed_out', 'tree_exit_confirmed', 'forced_termination', 'elapsed_seconds')}
    if result.get('returncode') or result.get('timed_out') or result.get('forced_termination'):
        summary['stderr_tail'] = (directory / 'stderr.bin').read_text(encoding='utf-8', errors='replace')[-3000:]
    return summary


def owns_experiment(recipe, spec):
    """A failed name-race must not close somebody else's experiment."""
    expected_subjects = [
        {'label': label, 'bot': 'main/zero', 'set': {},
         'weights': str((Path(recipe['cache']) / 'models' / f'{label}.txt').resolve())}
        for label in recipe.get('subject_labels', LABELS)
    ]
    return (spec.get('name') == recipe['experiment'] and spec.get('subjects') == expected_subjects
            and spec.get('opponents') == [OPPONENT] and spec.get('sides') == 'both'
            and spec.get('maps') == ','.join(recipe['maps']['pool'] + recipe['maps']['train'])
            and spec.get('rounds') == 0 and spec.get('private_snapshots') is True
            and spec.get('reuse_shared_snapshots') is False)


def execute(recipe, exp, native):
    cache = Path(recipe['cache'])
    experiment = exp.exp_dir(recipe['experiment'])
    if cache.exists() or experiment.exists():
        raise ValueError('use a new experiment name; existing experiments and activity evidence are never replaced')
    (cache / 'models').mkdir(parents=True)
    for label in recipe.get('subject_labels', LABELS):
        source = Path(recipe['inputs'][label]['path'])
        target = cache / 'models' / f'{label}.txt'
        shutil.copyfile(source, target)
        if digest(target) != recipe['inputs'][label]['sha256']:
            raise ValueError('checkpoint changed while copying owner-private inputs')
        if recipe['inputs'][label].get('sidecar_path'):
            copied_sidecar = target.with_name(target.name + '.model.json')
            shutil.copyfile(recipe['inputs'][label]['sidecar_path'], copied_sidecar)
            if digest(copied_sidecar) != recipe['inputs'][label]['sidecar_sha256']:
                raise ValueError(f'{label} provenance changed while copying owner-private inputs')
    write_json(cache / 'recipe.json', recipe)
    result = {'schema': 'zero-student-attention-activity-result/v1', 'experiment': recipe['experiment'],
              'outcome': 'experiment_failed', 'scope': recipe['scope']}
    run_error = None
    try:
        if runtime_closure() != recipe['runtime_engineering']['runtime_source_sha256']:
            raise ValueError('runtime changed between recipe preparation and arena launch')
        launch = arena_cli(recipe['command'], cache / 'launch/run', recipe['timeout_seconds'], native)
        if launch['returncode'] or launch['timed_out'] or launch['forced_termination']:
            raise RuntimeError(f'arena experiment failed: {launch}')
        result = aggregate_rows(recipe, exp)
        result['run_process'] = launch
        for relative, expected in recipe['inputs']['runtime_sources'].items():
            if digest(ROOT / relative) != expected:
                raise ValueError('runtime source changed during the experiment')
    except BaseException as error:
        run_error = error
        result.update(outcome='experiment_failed', error={'type': type(error).__name__, 'message': str(error)[:4000]})
    finally:
        write_json(cache / 'aggregate.json', result)
        if isinstance(run_error, OwnedArenaExitUnconfirmed):
            result['experiment_closed'] = False
            result['cleanup_pending'] = 'Owned arena process exit must be confirmed before experiment close.'
            write_json(cache / 'aggregate.json', result)
        elif (experiment / 'spec.json').is_file():
            # Same arena close implementation/locks as cmd_close. Keep identity verification and
            # close in one operation lock so a concurrent --force cannot replace the checked spec.
            with exp.operation_lock(experiment), exp.replay_close_lock(experiment):
                _, recorded_spec = exp.load(recipe['experiment'])
                if owns_experiment(recipe, recorded_spec):
                    note = f"{result['outcome']}; fixed-pool paying activity diagnostic only; answer {cache.relative_to(ROOT).as_posix()}/aggregate.json"
                    close_code = exp._close_experiment(argparse.Namespace(name=recipe['experiment'], note=note))
                    if close_code:
                        raise RuntimeError(f'experiment close failed: returncode {close_code}')
                    result['experiment_closed'] = True
                    result['close_transport'] = 'arena exp close under its operation/replay locks'
                else:
                    result['experiment_closed'] = False
                    result['close_skipped'] = 'Recorded name belongs to different inputs; this launch did not create that experiment.'
        elif 'experiment_closed' not in result:
            result['experiment_closed'] = None  # arena failed before creating an experiment
        write_json(cache / 'aggregate.json', result)
        # Only our regenerable cache transport logs are removed, after Job exit/experiment closure.
        launch_root = (cache / 'launch').resolve()
        if not isinstance(run_error, OwnedArenaExitUnconfirmed) and launch_root.is_relative_to(cache.resolve()) and launch_root.exists():
            shutil.rmtree(launch_root)
    if run_error is not None:
        raise run_error
    print(json.dumps(result, indent=2))
    return 0 if result['clean_measured_health'] and result['activity_forecast_met'] else 1


def parser():
    out = argparse.ArgumentParser(description=__doc__)
    source = out.add_mutually_exclusive_group(required=True)
    source.add_argument('--trained', type=Path)
    source.add_argument('--variant-pair', type=Path, help='same fitted policy, immutable config9 off/on pair')
    out.add_argument('--untrained', type=Path)
    out.add_argument('--name', required=True, help='fresh main/NAME experiment')
    out.add_argument('--settled-recipe', type=Path, default=ROOT / '.cache/main/zero/activity/zero-student-activity-first-bc/recipe.json')
    out.add_argument('--bearing-evidence', type=Path, default=ROOT / '.cache/main/zero/attention-math/math.json')
    out.add_argument('--timeout', type=int, default=1800, help='process containment timeout, not a game count')
    out.add_argument('--runtime-evidence', type=Path, help='attach existing measured engineering scope without adding a training gate')
    out.add_argument('--runtime-root-seed', type=int, help='record the current engineering seed separately from point evidence')
    out.add_argument('--cache-fact-proof', type=Path, help='attach the existing cached-versus-pure raw-fact proof')
    out.add_argument('--uncached-root-seed', type=int)
    out.add_argument('--cached-root-seed', type=int)
    out.add_argument('--execute', action='store_true', help='run the printed arena experiment after input validation')
    return out


def main(argv=None):
    args = parser().parse_args(argv)
    if args.timeout < 1:
        raise ValueError('timeout must be positive')
    sys.path.insert(0, str(TOOLS))
    import core
    import exp
    import compiled_runner as native
    if args.variant_pair is not None:
        if args.untrained is not None:
            raise ValueError('cache comparison uses the same fitted policy, not an untrained control')
        if args.execute and (args.cache_fact_proof is None or args.runtime_evidence is None
                             or args.uncached_root_seed is None or args.cached_root_seed is None):
            raise ValueError('cache execution needs the existing fact proof, current caller point and both declared engineering seeds')
        inputs = validate_cache_pair(args.variant_pair)
    else:
        if args.untrained is None:
            raise ValueError('learning comparison needs its exact untrained input')
        inputs = validate_models(args.trained, args.untrained)
    recipe = make_recipe(args, inputs, core)
    if not args.execute:
        print(json.dumps(recipe, indent=2))
        return 0
    return execute(recipe, exp, native)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'error student activity: {error}', file=sys.stderr)
        raise SystemExit(2)
