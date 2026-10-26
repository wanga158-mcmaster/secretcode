"""Pure saved-native bank -> credited camp clear -> paid capture -> bank witness.

This narrow prototype never qualifies a bot-specific mechanism or admission.
Native CAMSTAT owner/group credit is required; coordinate candidates are ignored.
No native command, replay decoder, build, or game is run by this module.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import io
import math
import re
import zipfile

ACTIVITY_SHA = '6fb9d5f144c4bb706c9706a58f7b5819caeb1c8b6671b8fe031d3889715dc9db'
VALIDATOR_SHA = 'feaa0715e0cf5160715772fbc369dbf4ef9005250ece75d8c3a55ed9ef4ababd'
EXTRACTOR_SHA = 'c6c3ce6dec2e606dd3a5e3248be96dbfc0a653e5eb3772473729eca92643c603'
KINDS = ('campClear','gemCollection','gemSubmission','gemLoss','death')

class EvidenceError(ValueError): pass

def _finite_tree(value):
    if type(value) is float and not math.isfinite(value):
        raise EvidenceError('nonfinite saved numeric observation')
    if isinstance(value, dict):
        for key, item in value.items():
            if type(key) is not str:
                raise EvidenceError('nonstring JSON object key')
            _finite_tree(item)
    elif isinstance(value, list):
        for item in value:
            _finite_tree(item)


def _observation_rows(event):
    if type(event) is not dict or type(event.get('observed')) is not list:
        raise EvidenceError('observed transition list unavailable')
    found = {}
    for row in event['observed']:
        if (type(row) is not dict or set(row) != {'entity', 'before', 'after'} or
                type(row['entity']) is not str or not row['entity'] or
                type(row['before']) is not dict or type(row['after']) is not dict):
            raise EvidenceError('malformed observed transition')
        entity = row['entity']
        if entity in found:
            raise EvidenceError('ambiguous duplicate observed entity')
        _finite_tree(row)
        prefix = entity.split(':', 1)[0]
        if prefix in {'champion', 'score', 'group'}:
            if not re.fullmatch(r'(champion|score|group):[0-9]+', entity):
                raise EvidenceError('malformed observed native entity ID')
            ident = count(entity.split(':')[1])
            if prefix == 'score' and ident not in (0, 1):
                raise EvidenceError('invalid observed score owner')
        for side in ('before', 'after'):
            state = row[side]
            if prefix == 'champion':
                if 'gems' in state:
                    integer(state['gems'])
                if 'gold' in state:
                    gold = state['gold']
                    if type(gold) is int:
                        integer(gold)
                    elif type(gold) is not float or not math.isfinite(gold) or gold < 0:
                        raise EvidenceError('invalid finite personal gold')
            elif prefix == 'score' and 'value' in state:
                integer(state['value'])
            elif prefix == 'group':
                if 'captureTeam' in state and (type(state['captureTeam']) is not int or
                                               state['captureTeam'] not in (0, 1, 2)):
                    raise EvidenceError('invalid exact group capture team')
                if 'rewardAvailable' in state and type(state['rewardAvailable']) is not bool:
                    raise EvidenceError('invalid group reward flag')
        found[entity] = row
    return found


def _native_event(event, replay_prefix=None, roster=None):
    required = {'record', 'id', 'round', 'kind', 'actor', 'team', 'native', 'observed'}
    if type(event) is not dict or not required <= set(event) or event['record'] != 'event':
        raise EvidenceError('malformed native event record')
    if type(event['kind']) is not str or not event['kind']:
        raise EvidenceError('missing event kind label')
    _finite_tree(event)
    native = event['native']
    if type(native) is not dict or set(native) != {
            'round', 'input', 'substep', 'phase', 'kind', 'snapshotRound',
            'actor', 'currentChampion', 'processPhaseAfter', 'awaitingInputAfter'}:
        raise EvidenceError('unsupported native event fields')
    for key in ('round', 'input', 'substep', 'phase', 'kind', 'snapshotRound',
                'currentChampion', 'processPhaseAfter'):
        integer(native[key])
    actor = native['actor']
    if (type(actor) is not dict or set(actor) != {'kind', 'id'} or
            type(actor['kind']) is not int or actor['kind'] not in (0, 1, 2)):
        raise EvidenceError('invalid native actor identity')
    integer(actor['id'])
    if type(native['awaitingInputAfter']) is not bool:
        raise EvidenceError('invalid native awaiting-input flag')
    if integer(event['round']) != native['round']:
        raise EvidenceError('event/native round mismatch')
    if actor['kind'] == 1:
        if (type(event['actor']) is not int or event['actor'] != actor['id'] or
                type(event['team']) is not int or event['team'] not in (0, 1)):
            raise EvidenceError('event/native champion identity mismatch')
        if roster is not None and roster.get(actor['id']) != event['team']:
            raise EvidenceError('event actor/team not in native roster')
    elif event['actor'] is not None or event['team'] is not None:
        raise EvidenceError('nonchampion native event has champion alias')
    key = event['id']
    if type(key) is not str:
        raise EvidenceError('event replay ID unavailable')
    prefix = key.split(':', 1)[0]
    if not re.fullmatch(r'[0-9a-f]{16}', prefix) or replay_prefix is not None and prefix != replay_prefix:
        raise EvidenceError('event replay ID prefix mismatch')
    parts = [native[k] for k in ('round', 'input', 'substep', 'phase', 'kind')]
    parts += [actor['kind'], actor['id'], native['processPhaseAfter'],
              int(native['awaitingInputAfter']), native['snapshotRound']]
    if key != prefix + ':' + ':'.join(str(value) for value in parts):
        raise EvidenceError('event ID/native fields mismatch')
    _observation_rows(event)
    return native


def _trace_meta(meta, replay, archive_path, root, helper, decoder, payload):
    if type(meta) is not dict or meta.get('record') != 'metadata':
        raise EvidenceError('trace metadata missing')
    _finite_tree(meta)
    if (meta.get('readOnly') is not True or meta.get('schemaVersion') != '2.0' or
            meta.get('engineVersion') != '1.0.3' or meta.get('decoderProfile') != 'cli-3.0.5' or
            meta.get('replayFormatVersion') != '0.0.3' or meta.get('replaySha256') != replay or
            helper.local(root, meta.get('replayPath')) != archive_path or
            meta.get('payloadSha256') != payload or meta.get('decoderSha256') != decoder):
        raise EvidenceError('trace native/replay/payload/decoder identity mismatch')
    if (meta.get('profileRegistrySha256') != '97d499cc07741f35cdd47fbdf2fe4d12b62967f4dc85010f74c1e672499641a0' or
            meta.get('profileModuleSha256') != 'ab8a759756512de90d6571e64661adbd753627a658eb676782e5cf6f5b05c68f'):
        raise EvidenceError('unsupported reviewed decoder profile')
    if meta.get('helperSha256') != {
            'replay-interaction-events.mjs': EXTRACTOR_SHA,
            'replay-interaction-native.mjs': '7eb68e4a979015a60696e92ebb6c996cb12ddf9522f12510e279e936f1dccc15',
            'replay-native-slot.py': '6bbb65fe8cb42b92e7eca5d231f4ce147f02f1d06963bce14029f605aa5d10ed'}:
        raise EvidenceError('unsupported pinned native extraction semantics')
    interaction = meta.get('interactions')
    if type(interaction) is not dict or interaction.get('schemaVersion') != 'interaction-trace-v1':
        raise EvidenceError('unsupported interaction schema')
    if interaction.get('filters') != {'actors': None, 'teams': None, 'targets': None, 'kinds': None, 'nearbyRadius': 4}:
        raise EvidenceError('trace filters unsupported')
    if type(interaction['filters']['nearbyRadius']) is not int:
        raise EvidenceError('trace filter numeric alias')
    coverage = interaction.get('coverage')
    keys = {'requested', 'firstObserved', 'lastObserved', 'lastRetained', 'nativeObservations',
            'duplicateStates', 'candidateEvents', 'omittedByFilter', 'retainedEvents', 'maxEvents',
            'maxBytes', 'estimatedPayloadBytes', 'complete', 'truncated', 'historyExhausted',
            'gameFinished', 'requestedEndReached', 'emptyWindow', 'lastNativeMoment', 'stopped',
            'firstBeforeStateAvailable', 'queryFailures', 'queryObservationComplete', 'note'}
    if type(coverage) is not dict or set(coverage) != keys:
        raise EvidenceError('trace coverage fields missing or unsupported')
    for key in ('nativeObservations', 'duplicateStates', 'candidateEvents', 'omittedByFilter',
                'retainedEvents', 'maxEvents', 'maxBytes', 'estimatedPayloadBytes'):
        integer(coverage[key])
    for key in ('complete', 'requestedEndReached', 'firstBeforeStateAvailable', 'queryObservationComplete'):
        if coverage[key] is not True:
            raise EvidenceError('trace coverage incomplete')
    for key in ('truncated', 'historyExhausted', 'gameFinished', 'emptyWindow'):
        if coverage[key] is not False:
            raise EvidenceError('unsupported or incomplete point-window ending')
    if (type(coverage['queryFailures']) is not dict or coverage['queryFailures'] or
            coverage['omittedByFilter'] != 0 or coverage['candidateEvents'] != coverage['retainedEvents'] or
            not 0 < coverage['retainedEvents'] <= coverage['maxEvents'] or
            coverage['nativeObservations'] < coverage['candidateEvents'] or
            coverage['nativeObservations'] < coverage['duplicateStates'] + coverage['candidateEvents'] or
            coverage['duplicateStates'] > coverage['nativeObservations'] or
            not 0 < coverage['estimatedPayloadBytes'] <= coverage['maxBytes'] or
            coverage['stopped'] != 'round_range_end' or meta.get('stopped') != coverage['stopped'] or
            type(coverage['note']) is not str or not coverage['note']):
        raise EvidenceError('trace coverage counters or terminal shape contradict completeness')
    requested = coverage['requested']
    if type(requested) is not dict or set(requested) != {'fromRound', 'toRound'}:
        raise EvidenceError('trace requested window unavailable')
    lower, upper = integer(requested['fromRound']), integer(requested['toRound'])
    if (lower > upper or integer(meta.get('fromRound')) != lower or integer(meta.get('toRound')) != upper or
            integer(meta.get('timelineInputs')) == 0 or integer(meta.get('inspectedSteps')) < coverage['nativeObservations']):
        raise EvidenceError('trace requested window mismatch')
    endpoints = {}
    for key in ('firstObserved', 'lastObserved', 'lastRetained'):
        stamp = coverage[key]
        if type(stamp) is not dict or set(stamp) != {'round', 'input', 'substep', 'phase', 'kind', 'snapshotRound'}:
            raise EvidenceError('trace endpoint fields unavailable')
        for value in stamp.values():
            integer(value)
        if not lower <= stamp['round'] <= upper:
            raise EvidenceError('trace endpoint outside requested window')
        endpoints[key] = stamp
    if (endpoints['firstObserved']['round'] != lower or
            moment(endpoints['firstObserved']) > moment(endpoints['lastObserved']) or
            endpoints['lastObserved'] != endpoints['lastRetained']):
        raise EvidenceError('trace observed/retained endpoints mismatch')
    last = coverage['lastNativeMoment']
    if type(last) is not dict or set(last) != {'round', 'input', 'substep', 'phase', 'awaitingInput', 'step'}:
        raise EvidenceError('trace stopping boundary unavailable')
    for key in ('round', 'input', 'substep', 'phase'):
        integer(last[key])
    step = last['step']
    if type(step) is not dict or set(step) != {'round', 'input', 'substep', 'phase', 'kind'}:
        raise EvidenceError('trace stopping step unavailable')
    for value in step.values():
        integer(value)
    if (last['awaitingInput'] is not False or last['round'] != upper + 1 or last['phase'] != 1 or
            step['phase'] != 0 or step['kind'] != 0 or moment(last) != moment(step) or
            moment(last) <= moment(endpoints['lastObserved'])):
        raise EvidenceError('unsupported point-window stopping boundary')
    return coverage, lower, upper, endpoints



def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def descriptor(path, root):
    path=Path(path).resolve(); root=Path(root).resolve()
    key='path' if path.is_relative_to(root) else 'file'
    return {key:path.relative_to(root).as_posix() if key=='path' else str(path),
            'sha256':sha(path),'size':path.stat().st_size}

def dependency(root):
    path=root/'tools/arena/activity_metrics.py'
    if sha(path)!=ACTIVITY_SHA: raise EvidenceError('reviewed activity helper changed')
    if sha(root/'tools/arena/compiled_runner.py')!=VALIDATOR_SHA: raise EvidenceError('frozen strict validator changed')
    spec=importlib.util.spec_from_file_location('_renewal_activity',path)
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module,path

def integer(value):
    if type(value) is not int or not 0 <= value <= 18446744073709551615:
        raise EvidenceError('missing exact unsigned64 integer')
    return value

def count(value):
    if type(value) is str and re.fullmatch(r'[0-9]{1,20}', value):
        value = int(value)
    return integer(value)

def moment(stamp):
    if not isinstance(stamp,dict): raise EvidenceError('missing timestamp')
    return tuple(integer(stamp.get(k)) for k in ('round','input','substep'))

def group_id(entry):
    target=entry.get('target')
    if not isinstance(target,dict) or target.get('kind')!='campGroup':
        raise EvidenceError('native group target unavailable')
    return integer(target.get('id'))

def observed(event, entity):
    if type(entity) is not str:
        raise EvidenceError('observed entity selector malformed')
    return _observation_rows(event).get(entity)

def numeric_delta(change, key):
    if change is None:
        return None
    if type(change) is not dict or type(change.get('before')) is not dict or type(change.get('after')) is not dict:
        raise EvidenceError('numeric transition unavailable')
    before, after = change['before'].get(key), change['after'].get(key)
    if type(before) not in (int, float) or type(after) not in (int, float):
        return None
    if any(type(value) is float and not math.isfinite(value) for value in (before, after)):
        raise EvidenceError('nonfinite transition endpoint')
    delta = after - before
    if type(delta) is float and not math.isfinite(delta):
        raise EvidenceError('nonfinite transition delta')
    return delta

def unknown(reason,activity=None):
    return {'schema':'economic-renewal-observer-prototype/v1','status':'unknown',
            'economic_sequence':{'status':'unknown','witnesses':[],'reason':reason},
            'behavior_coverage':{'status':'unknown','verified':False,
              'reason':'Generic economic continuation does not establish bot-specific mechanism, refusal, distinctness, fresh applicability, or admission.'},
            'activity_status':activity,'native_work_started':False,'jobs':False}

def event_tables(stats):
    roster_rows=stats['roster']['champions']
    if not isinstance(roster_rows,list) or not roster_rows: raise EvidenceError('native roster unavailable')
    roster={}
    for row in roster_rows:
        if not isinstance(row,dict): raise EvidenceError('malformed native roster member')
        actor=integer(row.get('id'))
        if actor in roster or row.get('team') not in ('teamA','teamB') or not isinstance(row.get('name'),str) or not row['name']:
            raise EvidenceError('duplicate/malformed native roster identity')
        roster[actor]=row
    groups=stats['events']
    if not isinstance(groups,list): raise EvidenceError('native event tables unavailable')
    maximum_round=count(stats['match']['roundsStarted'])
    final_input=count(stats['match']['finalInput'])
    final_substep=count(stats['match']['finalSubstep'])
    tables={}; complete_moments=[]
    for group in groups:
        if not isinstance(group,dict): raise EvidenceError('malformed native event table')
        if group.get('kind') not in KINDS: continue
        owner=group.get('owner')
        if not isinstance(owner,dict): raise EvidenceError('native event owner unavailable')
        actor=integer(owner.get('id')); identity=roster.get(actor)
        if (identity is None or owner.get('kind')!='champion' or
            any(owner.get(k)!=identity[k] for k in ('team','name'))):
            raise EvidenceError('native event owner mismatch')
        key=(actor,group['kind'])
        if key in tables: raise EvidenceError('duplicate native event group')
        entries=group.get('entries')
        if not isinstance(entries,list): raise EvidenceError('missing native event entries')
        previous=None
        for entry in entries:
            if not isinstance(entry,dict): raise EvidenceError('malformed native event entry')
            stamp=entry.get('timestamp')
            if not isinstance(stamp,dict) or set(stamp)!={'round','input','substep','phase','ordinal'}:
                raise EvidenceError('timestamp fields missing')
            when=moment(stamp); ordinal=integer(stamp.get('ordinal'))
            phase=stamp.get('phase')
            if type(phase) is not str or not phase or len(phase)>32 or '\0' in phase:
                raise EvidenceError('missing/malformed native phase')
            if when[0]>maximum_round or when[1]>final_input or (when[1]==final_input and when[2]>final_substep):
                raise EvidenceError('timestamp outside complete match')
            if previous is not None and (when<previous[0] or ordinal<=previous[1]):
                raise EvidenceError('unordered full native event moments')
            previous=(when,ordinal)
            count(entry.get('value'))
            if group['kind']=='campClear' or group['kind']=='gemCollection' and entry.get('context')=='campGroup':
                group_id(entry)
            complete_moments.append((ordinal,when,actor,group['kind']))
        tables[key]=entries
    if any((actor,kind) not in tables for actor in roster for kind in KINDS):
        raise EvidenceError('dense native owner/event table missing')
    # Relevant CAMSTAT ordinals must locate one unambiguous native point and
    # agree with the full native clock across every owner and economic kind.
    # Equal full moments at distinct ordinals are permitted; critical equal
    # moments are separately refused below rather than assigned a causal order.
    complete_moments.sort()
    previous=None
    for ordinal,when,actor,kind in complete_moments:
        if previous is not None and (ordinal==previous[0] or when<previous[1]):
            raise EvidenceError('ambiguous/inconsistent complete native moments')
        previous=(ordinal,when)
    return roster,tables

def replay_matches(events, entry, kind, actor, team):
    integer(actor)
    if type(team) is not int or team not in (0, 1):
        raise EvidenceError('invalid requested team')
    expected = {'gemSubmission': ('gems-submitted', 18, 14, 'championStartTurn'),
                'campClear': ('attack', 5, 10, 'champions'),
                'gemCollection': ('capture', 2, 7, 'campCaptures')}
    label, native_kind, phase, stats_phase = expected[kind]
    if entry['timestamp']['phase'] != stats_phase:
        return []
    matched = []
    for event in events:
        native = _native_event(event)
        if event['kind'] != label or moment(native) != moment(entry['timestamp']):
            continue
        if native['kind'] != native_kind or native['phase'] != phase:
            continue
        if kind != 'gemCollection' and (event['actor'] != actor or event['team'] != team or
                native['actor'] != {'kind': 1, 'id': actor} or native['currentChampion'] != actor):
            continue
        if kind == 'gemCollection' and event['actor'] is not None:
            continue
        matched.append(event)
    return matched

def bank_valid(event, entry, actor, team):
    value = count(entry['value'])
    own, score = observed(event, 'champion:' + str(actor)), observed(event, 'score:' + str(team))
    return bool(value > 0 and own is not None and score is not None and
                own['before'].get('gems') == value and own['after'].get('gems') == 0 and
                numeric_delta(score, 'value') == value)

def clear_valid(event, entry):
    change = observed(event, 'group:' + str(group_id(entry)))
    return bool(count(entry['value']) == 1 and change is not None and
                change['before'].get('rewardAvailable') is False and
                change['after'].get('rewardAvailable') is True)

def capture_valid(event, entry, actor, team):
    own = observed(event, 'champion:' + str(actor))
    group = observed(event, 'group:' + str(group_id(entry)))
    value = count(entry['value'])
    gold = numeric_delta(own, 'gold')
    return bool(value > 0 and entry.get('context') == 'campGroup' and own is not None and group is not None and
                own['before'].get('gems') == 0 and own['after'].get('gems') == value and
                gold is not None and gold > 0 and group['before'].get('rewardAvailable') is True and
                group['before'].get('captureTeam') == team and
                group['after'].get('rewardAvailable') is False and group['after'].get('captureTeam') == 2)

def recognize(stats,events,actor):
    """Internal pure predicate. Caller must separately validate pinned provenance."""
    try:
        actor=integer(actor); roster,tables=event_tables(stats)
        if actor not in roster: return unknown('requested actor absent')
        team={'teamA':0,'teamB':1}[roster[actor]['team']]
        captures=tables[(actor,'gemCollection')]; banks=tables[(actor,'gemSubmission')]
        all_clears=[(owner,e) for (owner,kind),entries in tables.items() if kind=='campClear' for e in entries]
        for capture in captures:
            if capture.get('context')!='campGroup': continue
            group=group_id(capture); cm=moment(capture['timestamp']); co=capture['timestamp']['ordinal']
            cap_events=replay_matches(events,capture,'gemCollection',actor,team)
            if len(cap_events)!=1 or not capture_valid(cap_events[0],capture,actor,team): continue
            group_clears=[(owner,e) for owner,e in all_clears if group_id(e)==group]
            # A clear at the payout's same complete moment is ambiguous. The
            # native owner credit alone does not invent its relative ordering.
            if any(moment(e['timestamp'])==cm or e['timestamp']['ordinal']==co for _,e in group_clears): continue
            preceding=[(owner,e) for owner,e in group_clears
                       if moment(e['timestamp'])<cm and e['timestamp']['ordinal']<co]
            if not preceding: continue
            latest=max(moment(e['timestamp']) for _,e in preceding)
            latest_entries=[pair for pair in preceding if moment(pair[1]['timestamp'])==latest]
            if len(latest_entries)!=1: continue
            owner,clear=latest_entries[0]
            if owner!=actor: continue
            wm=moment(clear['timestamp']); wo=clear['timestamp']['ordinal']
            if not wm<cm or not wo<co: continue
            work_events=replay_matches(events,clear,'campClear',actor,team)
            if len(work_events)!=1 or not clear_valid(work_events[0],clear): continue
            for first in banks:
                fm=moment(first['timestamp']); fo=first['timestamp']['ordinal']
                if not fm<wm or not fo<wo: continue
                first_events=replay_matches(events,first,'gemSubmission',actor,team)
                if len(first_events)!=1 or not bank_valid(first_events[0],first,actor,team): continue
                for last in banks:
                    lm=moment(last['timestamp']); lo=last['timestamp']['ordinal']
                    if not cm<lm or not co<lo or count(last['value'])!=count(capture['value']): continue
                    last_events=replay_matches(events,last,'gemSubmission',actor,team)
                    if len(last_events)!=1 or not bank_valid(last_events[0],last,actor,team): continue
                    # Cargo events at either critical endpoint are ambiguous
                    # too. Exclude only the exact capture and bank being proved.
                    interruptions=[(kind,e) for kind in ('gemCollection','gemLoss','gemSubmission','death')
                        for e in tables[(actor,kind)] if e is not capture and e is not last and
                        (cm<=moment(e['timestamp'])<=lm or co<=e['timestamp']['ordinal']<=lo)]
                    if interruptions: continue
                    witness={'actor':actor,'team':team,'champion':roster[actor]['name'],'native_group':group,
                      'first_bank':{'native':first,'replay_event':first_events[0]['id']},
                      'credited_camp_clear':{'native':clear,'replay_event':work_events[0]['id']},
                      'paid_capture':{'native':capture,'replay_event':cap_events[0]['id'],
                        'gold_gained':numeric_delta(observed(cap_events[0],'champion:'+str(actor)),'gold')},
                      'later_bank':{'native':last,'replay_event':last_events[0]['id']}}
                    out=unknown('Bot-specific claims remain outside this observer.')
                    out['status']='witnessed'
                    out['economic_sequence']={'status':'pass','witnesses':[witness],
                      'semantics':'Native owner-credited clear/action, same native group personal paid reward, and personal banks. No solo-clear or causal-damage attribution is inferred from coordinate candidates.'}
                    return out
        return unknown('No complete explicitly credited and ordered economic renewal chain in retained evidence.')
    except (KeyError,TypeError,ValueError,IndexError,AttributeError,OverflowError) as exc:
        return unknown('Missing/malformed required sequence evidence: '+str(exc))

def load_traces(spec, root, helper):
    events, pins = {}, []
    traces = spec.get('traces')
    if type(traces) is not list or not traces:
        raise EvidenceError('no retained trace descriptors')
    saved = spec['saved_case']
    replay = saved['archive']['sha256']
    if type(replay) is not str or not re.fullmatch(r'[0-9a-f]{64}', replay):
        raise EvidenceError('invalid replay digest')
    archive_path, archive_raw = helper.pinned(root, saved['archive'])
    with zipfile.ZipFile(io.BytesIO(archive_raw)) as archive:
        if archive.namelist().count('replay.repl26a') != 1:
            raise EvidenceError('replay payload unavailable')
        payload = hashlib.sha256(archive.read('replay.repl26a')).hexdigest()
    _, raw = helper.pinned(root, saved['run'])
    run = helper.strict_json(raw)
    decoder = run['engine']['mapEditorWasmSha256']
    if decoder != '029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48':
        raise EvidenceError('unsupported reviewed native decoder')
    _, raw = helper.pinned(root, saved['statistics'])
    stats = helper.strict_json(raw)
    last_round = count(stats['match']['roundsStarted'])
    final_input = count(stats['match']['finalInput'])
    final_substep = count(stats['match']['finalSubstep'])
    roster = {}
    for member in stats['roster']['champions']:
        actor = integer(member['id'])
        if actor in roster or member['team'] not in ('teamA', 'teamB'):
            raise EvidenceError('native roster malformed or duplicate')
        roster[actor] = 0 if member['team'] == 'teamA' else 1
    for evidence in traces:
        path, raw = helper.pinned(root, evidence)
        lines = raw.splitlines()
        if not lines or any(not line.strip() for line in lines):
            raise EvidenceError('empty trace record')
        rows = [helper.strict_json(line) for line in lines]
        coverage, lower, upper, endpoints = _trace_meta(rows[0], replay, archive_path, root, helper, decoder, payload)
        if upper > last_round:
            raise EvidenceError('trace window exceeds native match')
        if coverage['retainedEvents'] != len(rows) - 1:
            raise EvidenceError('trace retainedEvents differs from actual rows')
        previous, local_ids = None, set()
        for row in rows[1:]:
            native = _native_event(row, replay[:16], roster)
            when = moment(native)
            if (not lower <= native['round'] <= upper or
                    native['input'] > final_input or
                    native['input'] == final_input and native['substep'] > final_substep or
                    when < moment(endpoints['firstObserved']) or when > moment(endpoints['lastObserved']) or
                    previous is not None and when <= previous):
                raise EvidenceError('unordered or out-of-window trace event')
            previous = when
            key = row['id']
            if key in local_ids:
                raise EvidenceError('duplicate native event in one trace')
            local_ids.add(key)
            if key in events and events[key] != row:
                raise EvidenceError('conflicting duplicate replay event')
            events[key] = row
        for endpoint, row in [('firstObserved', rows[1]), ('lastObserved', rows[-1]), ('lastRetained', rows[-1])]:
            native = row['native']
            if endpoints[endpoint] != {key: native[key] for key in endpoints[endpoint]}:
                raise EvidenceError('trace endpoint does not match actual event')
        pins.append(evidence)
    return list(events.values()), pins

def analyze_saved(spec,root):
    root=Path(root).resolve(); helper,path=dependency(root)
    saved=spec.get('saved_case',{})
    # Activity helper reuses the frozen strict native statistics contract and all
    # consumed-payload, map, side and replay identity checks. Positions are not
    # needed for an event sequence, so quiet-interval completeness stays unknown.
    activity=helper.analyze_saved(saved,root)
    imported=sys.modules.get('compiled_runner')
    if imported is None or Path(imported.__file__).resolve()!=root/'tools/arena/compiled_runner.py':
        raise EvidenceError('strict validator import came from another root')
    if any(issue!='position evidence absent' for issue in activity['issues']):
        out=unknown('Saved native activity/identity incomplete: '+'; '.join(activity['issues']),activity['status'])
    else:
        _,raw=helper.pinned(root,saved['statistics']); stats=helper.strict_json(raw)
        try:
            events,pins=load_traces(spec,root,helper)
            out=recognize(stats,events,spec.get('actor'))
        except (EvidenceError,KeyError,TypeError,ValueError,IndexError,AttributeError,OverflowError) as exc:
            pins=[]
            out=unknown('Missing/malformed retained trace evidence: '+str(exc),activity['status'])
        side='team'+saved['subject_side']
        if not any(r['id']==spec.get('actor') and r['team']==side for r in stats['roster']['champions']):
            out=unknown('requested actor is not on the selected subject side',activity['status'])
        out['trace_inputs']=pins; out['activity_status']=activity['status']
    if sha(path)!=ACTIVITY_SHA or sha(root/'tools/arena/compiled_runner.py')!=VALIDATOR_SHA:
        raise EvidenceError('pure helper or strict validator drift')
    out.update({'subject_snapshot':saved.get('subject_snapshot'),'case':saved.get('case'),
      'map_sha256':activity['map_file_sha256'],'consumed':activity['consumed'],
      'engine':activity['native_match']['engineVersion'],'engine_build':activity['native_match']['engineBuildHash'],
      'activity_helper':descriptor(path,root),'strict_statistics_validator':descriptor(root/'tools/arena/compiled_runner.py',root),
      'saved_existing_team_health':activity['existing_team_health'],'input_descriptors':spec,
      'limits':['Saved complete point windows can prove these ordered events; gaps do not prove uninterrupted routing or readiness.',
        'Only native campClear owner/group credit is accepted for work; inferred coordinate candidates and shared payouts alone never establish own work.',
        'This strict prototype recognizes basic-attack credited clears and positive gold plus gems capture into an empty bag; unsupported spells/reward shapes stay unknown.',
        'No policy trigger, probe benefit, denial, threshold commitment, refusal, replacement counterfactual, distinctness, fresh applicability, or admission is verified.',
        'Exact generic witness support does not change whole-panel coverage, team health, or release gates.']})
    return out

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',required=True); parser.add_argument('--case-spec',required=True); parser.add_argument('--out',required=True)
    args=parser.parse_args(); root=Path(args.root).resolve(); helper,_=dependency(root)
    configured=Path(args.case_spec).resolve(); spec=helper.strict_json(configured.read_bytes())
    report=analyze_saved(spec,root)
    report['case_spec_configured_file']=str(configured)
    with Path(args.out).open('x',encoding='utf-8',newline='\n') as stream:
        json.dump(report,stream,indent=2,allow_nan=False); stream.write('\n')
    print(json.dumps({'status':report['status'],'sequence':report['economic_sequence']['status'],
                      'behavior_coverage':report['behavior_coverage']['status'],'jobs':False}))
    return 0

if __name__=='__main__': raise SystemExit(main())
