"""Bounded read-only current-engine observation of retained co-farm/party games.

The owned projector preserves SetPriority inputs; two guarded API queries add current/next
numeric priority. Traversal, other queries and layouts remain the pinned official inspector.
No runner action, host effect or new game is used. Output must be fresh.
"""
from __future__ import annotations
import json
from pathlib import Path
import sys

from pair_verify import ROOT,NODE,contained,once,prepare_source,sha


def observe_saved(replay:Path,dest:Path,start:int,end:int,map_id:str,geometry:Path|None=None):
    dest.mkdir(parents=True,exist_ok=False)
    observer,binding=prepare_source(dest)
    native=ROOT/'tools/replay-interaction-native.mjs'
    native_text=once(native.read_text(encoding='utf-8'),
        '  Game_API_Champion_Get: [4, 44],',
        '  Game_API_Champion_Get: [4, 44],\n  Game_API_Champion_GetPriority: [5, 4],')
    native_text=once(native_text,'    champion.cooldowns = [0, 1].map(slot => {',
        "    for (const [field, next] of [['priorityCurrent', 0], ['priorityNext', 1]]) {\n"
        "      const result = query('Game_API_Champion_GetPriority', [game, perspective, champion.id, next]);\n"
        "      champion[field] = result ? result.getInt32(queryOut, true) : null;\n"
        "    }\n    champion.cooldowns = [0, 1].map(slot => {")
    active_native=dest/'source/current-priority-native.mjs'
    active_native.write_text(native_text,encoding='utf-8',newline='\n')
    text=once(observer.read_text(encoding='utf-8'),json.dumps(native.as_uri()),json.dumps(active_native.as_uri()))
    observer.write_text(text,encoding='utf-8',newline='\n')
    binding.update({'base_native':str(native),'base_native_sha256':sha(native),
        'active_native':str(active_native),'active_native_sha256':sha(active_native),
        'active_inspector_sha256':sha(observer)})
    binding['changes'].append('two guarded read-only Game_API_Champion_GetPriority queries, output int32, per own-perspective champion snapshot')
    (dest/'source/binding.json').write_text(json.dumps(binding,indent=1)+'\n',encoding='utf-8')
    argv=[str(NODE),'--no-warnings',str(observer),'--replay',str(replay),
          '--actor','all','--trace','1','--from-round',str(start),'--to-round',str(end),
          '--captures','1','--map-id',map_id,'--max-events','30000','--max-bytes','33554432']
    if geometry is not None:argv.extend(['--geometry',str(geometry)])
    try:
        record=contained._slotted_command(lambda count:argv,ROOT,dest,120,jobs=1)
    finally:
        if not contained.confirm_pending_cleanup():raise RuntimeError('observer cleanup unconfirmed')
    (dest/'process.json').write_text(json.dumps(record,indent=1)+'\n',encoding='utf-8')
    assert record['returncode']==0 and record['tree_exit_confirmed'] and record['containment']['empty_confirmed'],record
    doc=json.loads((dest/'stdout.bin').read_bytes())
    doc['sourceProjection']={**binding,'upstreamDeclaredToolSha256':doc['toolSha256'],
        'upstreamDeclaredHelperSha256':dict(doc['helperSha256']),
        'note':'Raw stdout retains upstream fingerprint declarations; this normalized record supplies active owned identities.'}
    doc['toolSha256']=binding['active_inspector_sha256']
    doc['helperSha256']['replay-interaction-native.mjs']=binding['active_native_sha256']
    doc['helperSha256']['replay-interaction-events.mjs']=binding['active_events_sha256']
    coverage=doc['interactions']['coverage']
    assert coverage['complete'] and coverage['requestedEndReached'] and not coverage['truncated']
    assert not coverage['queryFailures'] and coverage['queryObservationComplete']
    assert doc['engineVersion']=='1.0.3' and doc['decoderProfile']=='cli-3.0.5'
    for name in('base_inspector','base_events','active_inspector','active_events','base_native','active_native'):
        assert sha(Path(binding[name]))==binding[name+'_sha256']
    (dest/'trace.json').write_text(json.dumps(doc,separators=(',',':'))+'\n',encoding='utf-8')
    return {'trace':str((dest/'trace.json').relative_to(ROOT)),'trace_sha256':sha(dest/'trace.json'),
        'replay':str(replay.relative_to(ROOT)),'replay_sha256':sha(replay),'window':[start,end],
        'coverage':coverage,'source_projection':binding,'process':record,'new_games':0}


def main():
    output=ROOT/'results/contenders/foundation/cofarm-v41'
    replay=ROOT/'.cache/main/v41-default110-pool/games/00elkring_main_starter-cpp_8d59cf1c.repl26a'
    if '--observe' in sys.argv or '--finalize' in sys.argv:
        if '--observe' in sys.argv:
            result=observe_saved(replay,output/'native',168,183,'00elkring')
        else:
            dest=output/'native';doc=json.loads((dest/'trace.json').read_text(encoding='utf-8'))
            result={'trace':str((dest/'trace.json').relative_to(ROOT)),'trace_sha256':sha(dest/'trace.json'),
                'replay':str(replay.relative_to(ROOT)),'replay_sha256':sha(replay),'window':[168,183],
                'coverage':doc['interactions']['coverage'],'source_projection':doc['sourceProjection'],
                'process':json.loads((dest/'process.json').read_text(encoding='utf-8')),'new_games':0,
                'consumer_repair':'Native observer succeeded; first row binding used inconsistent path separators. Finalized saved record with normalized paths, no observer rerun.'}
        rows=[json.loads(s) for s in (ROOT/'.cache/main/v41-default110-pool/games.jsonl').read_text(encoding='utf-8').splitlines()]
        row=next(x for x in rows if Path(x.get('replay','')).as_posix()==Path(result['replay']).as_posix())
        assert row['a']=='main/main@db6ca9bc4e39' and row['engine']=='1.0.3'
        result.update({'game_row':row,'script_sha256':sha(Path(__file__)),
            'purpose':'One retained normal v41 co-farm window; actual conditions, not a selected alternative dispatch'})
        (output/'observation.json').write_text(json.dumps(result,indent=1)+'\n',encoding='utf-8')
        print(json.dumps({'observed_games':1,'new_games':0,'events':result['coverage']['retainedEvents'],'window':result['window']}))
    else:
        doc=json.loads((output/'native/trace.json').read_text(encoding='utf-8'))
        trace=doc['interactions']
        print(json.dumps({'initial':trace['initial'],'entities':trace['entities'],
                         'states_first':trace['states'][:6]},default=str)[:12000])


if __name__=='__main__':main()
