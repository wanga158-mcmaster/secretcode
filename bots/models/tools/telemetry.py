"""Validate the models bots' own `p4 ...` stdout telemetry rows (decision, summary, milestone,
abort, draft, phase, input) against their fixed schemas and bounds.

Library use:  sys.path.insert(0, str(ROOT / "bots/models/tools")); from telemetry import parse_telemetry
    parse_telemetry(stdout_bytes, stats, side, expected_version, expected_model)
`stats` needs {"roster": {"champions": [{"id", "team", "name", "draftPick"?}], "draft"?: [...]},
"match": {"roundsCompleted", "width", "height"}}; `side` is the team string used in that roster;
`expected_version` is the bot's PLAN4_REFERENCE_VERSION build string; `expected_model` is the
decision-row model name ("m0" for the reference). Raises ValueError on the first schema violation.
Only the bot's personal stdout is read; native truth never feeds online features.
"""
import re


def require(ok, message):
    if not ok:
        raise ValueError(message)


def parse_telemetry(data, stats, side, expected_version='reference-v1', expected_model='m0'):
    required = {
        'decision': set('v model actor round job kind group second altar info menu candidates evaluated first secured z terminalUnknown fuelBefore fuelAfter fallback'.split()),
        'summary': set('v actor round fuel bag score active'.split()),
        'milestone': set('v actor round job bag score completed censor'.split()),
        'abort': set('reason job actor round'.split()),
        'draft': {'actual'},
        'phase': set('v actor round name before after'.split()),
        'input': set('v actor round job x y bag hp maxHp strength focus champion monsterShards jungleShards groupId visible healthStatus groupHp incoming gems nominal abort'.split()),
    }
    matches = list(re.finditer(rb'p4 (decision|summary|milestone|abort|draft|phase|input) ([^\r\n]*)\r?\n', data))
    require(len(matches) == len(re.findall(rb'p4 (?:decision|summary|milestone|abort|draft|phase|input) ', data)), 'Truncated telemetry record')
    records = []
    numeric = set('v actor round job kind group second altar info menu candidates evaluated first secured z terminalUnknown fuelBefore fuelAfter fuel bag score active completed actual requested teamSize before after x y hp maxHp strength focus champion monsterShards jungleShards groupId visible healthStatus groupHp incoming gems nominal abort'.split())
    movement_fields = {'status', 'nextX', 'nextY', 'remaining', 'moved'}
    movement_work_fields = {'preparedFuel', 'visits', 'generation'}
    actor_fields = {'actorCount', 'ownObserved', 'rosterComplete', 'truncated',
                    'stopReason', 'minimumFuel', 'stopFuel'}
    observer_fields = {'truncated', 'layoutTruncated', 'stopStage', 'stopGroup', 'stopMember',
                       'groups', 'visited', 'collectionFuel', 'stopFuel', 'stopFloor', 'stopMargin'}
    numeric |= movement_fields | movement_work_fields | actor_fields | observer_fields
    for match in matches:
        kind = match[1].decode('ascii')
        try:
            tokens = match[2].decode('ascii').split(' ')
        except UnicodeDecodeError as exc:
            raise ValueError('Non-ASCII bytes inside telemetry') from exc
        fields = {}
        for token in tokens:
            require(token.count('=') == 1, 'Malformed telemetry field')
            key, value = token.split('=')
            require(key not in fields and value, 'Duplicate/empty telemetry field')
            if key in numeric:
                require(re.fullmatch(r'-?\d+', value), 'Nonnumeric telemetry field')
                value = int(value)
            fields[key] = value
        schema = required[kind]
        actor_diagnostics = kind == 'phase' and fields.get('v') == 3 and fields.get('name') == 'actors'
        observer_diagnostics = kind == 'phase' and fields.get('v') == 4 and fields.get('name') == 'observer'
        if kind == 'draft' and expected_version != 'reference-v1':
            schema = {'requested', 'completed'}
            if set(fields) != schema:
                schema = schema | {'v', 'build', 'profile', 'teamSize'}
                require(fields.get('v') == 2, 'Unknown telemetry version')
                require(fields.get('build') == expected_version, 'Telemetry build differs from frozen version')
        elif fields.get('v') == 2:
            schema = schema | {'build'}
            if kind == 'abort':
                schema = schema | {'v'}
            require(fields['build'] == expected_version, 'Telemetry build differs from frozen version')
        elif actor_diagnostics:
            schema = schema | {'build'} | actor_fields
            require(fields.get('build') == expected_version, 'Telemetry build differs from frozen version')
        elif observer_diagnostics:
            schema = schema | {'build'} | observer_fields
            require(fields.get('build') == expected_version, 'Telemetry build differs from frozen version')
        movement_diagnostics = kind == 'phase' and bool((movement_fields | movement_work_fields) & set(fields))
        if movement_diagnostics:
            require(fields.get('v') == 2 and fields.get('name') == 'movement', 'Unexpected movement diagnostics')
            schema = schema | movement_fields
            if movement_work_fields & set(fields):
                schema = schema | movement_work_fields
        require(set(fields) == schema, 'Telemetry field schema differs')
        if 'v' in fields:
            require((fields['v'] in (1, 2) or actor_diagnostics or observer_diagnostics) and
                    (fields['v'] == 1) == (expected_version == 'reference-v1'), 'Unknown telemetry version')
        if kind == 'draft' and 'profile' in fields:
            profile = 'reference' if re.fullmatch(r'reference-v[1-9][0-9]*', expected_version) else expected_model
            require(fields['profile'] == profile and (profile == 'reference' or
                    re.fullmatch(r'model-' + re.escape(profile) + r'-v[1-9][0-9]*', expected_version)),
                    'Telemetry draft profile differs from frozen version')
            require(1 <= fields['teamSize'] <= 12 and 0 <= fields['completed'] < fields['teamSize'] and
                    0 <= fields['requested'] < 12, 'Invalid profiled draft bounds')
        if kind == 'phase':
            require(actor_diagnostics or fields['name'] in ('observer', 'navigation', 'menu', 'evaluation', 'movement'),
                    'Unknown fuel phase')
            require(fields['before'] >= fields['after'] >= 0, 'Invalid fuel phase boundary')
            if observer_diagnostics:
                require(all(fields[key] in (0, 1) for key in ('truncated', 'layoutTruncated')) and
                        0 <= fields['stopStage'] <= 14 and 0 <= fields['visited'] <= fields['groups'] <= 256 and
                        all(-1 <= fields[key] <= 0x7fffffff for key in ('stopGroup', 'stopMember')) and
                        all(0 <= fields[key] <= 0xffffffff for key in
                            ('before', 'after', 'collectionFuel', 'stopFloor', 'stopMargin')) and
                        -1 <= fields['stopFuel'] <= 0xffffffff, 'Invalid observer diagnostics')
                if fields['stopStage'] == 0:
                    require(fields['stopGroup'] == fields['stopMember'] == fields['stopFuel'] == -1 and
                            fields['stopFloor'] == fields['stopMargin'] == 0, 'Contradictory observer no-stop receipt')
                else:
                    require(0 < fields['stopMargin'] <= 15000 and
                            0 <= fields['stopFuel'] < fields['stopFloor'] + fields['stopMargin'],
                            'Contradictory observer budget stop')
            if actor_diagnostics:
                require(0 <= fields['actorCount'] <= 9 and
                        all(fields[key] in (0, 1) for key in ('ownObserved', 'rosterComplete', 'truncated')) and
                        0 <= fields['stopReason'] <= 4 and 0 <= fields['minimumFuel'] <= 0xffffffff and
                        -1 <= fields['stopFuel'] <= 0xffffffff, 'Invalid actor diagnostics')
                require((not fields['ownObserved'] or fields['rosterComplete']) and
                        (fields['ownObserved'] or fields['actorCount'] == 0), 'Contradictory actor evidence coverage')
                require(fields['truncated'] == bool(fields['stopReason']) and
                        (fields['stopReason'] != 1 or 0 <= fields['stopFuel'] < fields['minimumFuel']) and
                        (fields['stopReason'] not in (0, 3, 4) or fields['stopFuel'] == -1),
                        'Contradictory actor evidence stop')
            if movement_diagnostics:
                width, height = stats['match'].get('width'), stats['match'].get('height')
                require(type(width) is int and type(height) is int and width > 0 and height > 0,
                        'Movement diagnostics need native map dimensions')
                require(0 <= fields['status'] <= 5 and fields['moved'] in (0, 1) and
                        0 <= fields['nextX'] < width and 0 <= fields['nextY'] < height and
                        -1 <= fields['remaining'] <= width * height and
                        (fields['status'] != 0 or fields['remaining'] > 0) and
                        (fields['moved'] == 0 or fields['status'] == 0), 'Invalid movement diagnostics')
                if movement_work_fields <= set(fields):
                    require(fields['after'] <= fields['preparedFuel'] <= fields['before'] and
                            0 <= fields['visits'] <= 256 and 0 <= fields['generation'] <= 0xffffffff,
                            'Invalid movement work diagnostics')
        if kind == 'decision':
            require(fields['model'] == expected_model and 0 <= fields['evaluated'] <= fields['candidates'] <= 16, 'Invalid decision bounds')
            require(fields['fallback'] in ('none', 'budget', 'bootstrap'), 'Unknown fallback reason')
            require(fields['fuelBefore'] >= fields['fuelAfter'] >= 0, 'Invalid model fuel boundary')
        records.append({**fields, 'rowType': kind})
    actors = {int(a['id']) for a in stats['roster']['champions'] if a['team'] == side}
    drafts = [r for r in records if r['rowType'] == 'draft']
    profiled_drafts = [r for r in drafts if 'profile' in r]
    if profiled_drafts:
        team_size = profiled_drafts[0]['teamSize']
        require(len(profiled_drafts) == len(drafts) == team_size and
                all(r['teamSize'] == team_size for r in profiled_drafts) and
                [r['completed'] for r in profiled_drafts] == list(range(team_size)) and
                len({r['requested'] for r in profiled_drafts}) == team_size, 'Incomplete/duplicate profiled draft receipts')
        native_draft = [(n, pick) for n, pick in enumerate(stats['roster'].get('draft', [])) if pick['team'] == side]
        require(len(native_draft) == team_size, 'Profiled draft size differs from native draft')
        # SDK draft enum order; one Cassi & Issac pick creates two roster actors.
        pick_names = ('Ryth', 'Vesper Vale', 'Vendetta', 'Cassi & Issac', 'Stellar', 'Prism',
                      'Marcella', 'Willow', 'Ren', 'Varg', 'Tavi', 'Heart of Ruin')
        team_roster = [a for a in stats['roster']['champions'] if a['team'] == side]
        draft_indices = {n for n, _ in native_draft}
        require(all(type(a.get('draftPick')) is int and a['draftPick'] in draft_indices for a in team_roster),
                'Profiled draft roster linkage differs')
        for receipt, (draft_index, native_pick) in zip(profiled_drafts, native_draft):
            name = pick_names[receipt['requested']]
            require(native_pick['champion'] == name, 'Profiled draft request differs from native draft')
            expected_names = ['Cassi', 'Issac'] if name == 'Cassi & Issac' else [name]
            require(sorted(a['name'] for a in team_roster if a['draftPick'] == draft_index) == sorted(expected_names),
                    'Profiled draft roster identity differs')
    summaries = [r for r in records if r['rowType'] == 'summary']
    require(actors and {r['actor'] for r in summaries} == actors, 'Required actor summaries unavailable')
    require(any(r['rowType'] == 'decision' for r in records), 'Required decision telemetry unavailable')
    require(any(r['rowType'] == 'draft' for r in records), 'Actual draft receipt unavailable')
    for actor in actors:
        rounds = [r['round'] for r in summaries if r['actor'] == actor]
        require(rounds == sorted(set(rounds)) and all(b-a <= 64 for a, b in zip(rounds, rounds[1:])), 'Missing summary epochs')
        require(rounds[0] <= 2 and rounds[-1] >= int(stats['match']['roundsCompleted']) - 64, 'Terminal summary coverage missing')
        jobs = [r['job'] for r in records if r['rowType'] == 'decision' and r['actor'] == actor]
        require(jobs == list(range(1, len(jobs) + 1)), 'Missing/duplicate decision sequence')
        if expected_version != 'reference-v1' and jobs:
            phases = {r['name'] for r in records if r['rowType'] == 'phase' and r['actor'] == actor}
            require({'observer', 'navigation', 'menu', 'evaluation'} <= phases, 'Required fuel phase coverage missing')
            inputs = [r['job'] for r in records if r['rowType'] == 'input' and r['actor'] == actor]
            require(inputs == jobs, 'Missing/duplicate actor-local input receipts')
    require(all(r.get('actor', next(iter(actors))) in actors for r in records), 'Unknown telemetry actor')
    return {'status': 'parsed', 'records': len(records), 'decisions': sum(r['rowType'] == 'decision' for r in records),
            'summaries': len(summaries), 'milestones': sum(r['rowType'] == 'milestone' for r in records),
            'actualDraft': [a['name'] for a in stats['roster']['champions'] if a['team'] == side],
            'source': 'candidate personal stdout only; native truth excluded from online features', 'rows': records}
