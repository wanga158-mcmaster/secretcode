"""Check the actual altar cache on all pool maps and every possible member anchor.

This is pure WASI value coverage, not native member ordering or fuel evidence.
No runtime range is guessed: every nonnegative range class on each map is
covered, plus explicit signed-domain edges where the original formula is defined.
"""
from __future__ import annotations
import hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/arena'))
from core import Slots,map_path,pool_maps
from wasi import run_case

def digest(path:Path)->str:return hashlib.sha256(path.read_bytes()).hexdigest()

def main()->int:
    geometry_path=ROOT/'maps/geometry.json'
    geometry=json.loads(geometry_path.read_bytes().decode('utf-8'))
    bundle=json.loads((ROOT/'docs/reference/current-version.json').read_bytes().decode('utf-8'))
    assert geometry['schemaVersion']==f"game-map-geometry/{bundle['cli']['version']}-v1"
    assert geometry['engineSha256']==bundle['engine']['mapEditorWasmSha256']
    assert digest(geometry_path)==bundle['geometry']['sha256']
    names=pool_maps();assert len(names)==40 and len(set(names))==40
    index_path=ROOT/'.cambc/map-pool.json'
    index=json.loads(index_path.read_bytes().decode('utf-8'))['maps']
    pool=json.loads((ROOT/'maps/pool.json').read_bytes().decode('utf-8'))['maps']
    pool_by_id={item['id']:item for item in pool}
    by_name={world['id']:world for world in geometry['maps']}
    index_by_stem={Path(item['file']).stem:item for item in index}
    assert set(index_by_stem)==set(names) and len(by_name)==40
    assert geometry['pool']['count']==40 and geometry['pool']['sha256']==digest(ROOT/'maps/pool.json')
    worlds={name:by_name[pool_by_id[index_by_stem[name]['id']]['name']] for name in names}
    altar_data=[];anchor_data=[];map_data=[];coverage=[]
    for map_index,name in enumerate(names):
        world=worlds[name];width,height=world['width'],world['height']
        assert world['poolId']==index_by_stem[name]['id']
        assert world['mapSha256']==index_by_stem[name]['sha256']==pool_by_id[world['poolId']]['sha256']==digest(map_path(name))
        altar_offset=len(altar_data)
        altars=[(tile%width,tile//width) for tile in world['terrain']['altar']]
        assert len(altars)==len(set(altars))
        assert all(0<=x<width and 0<=y<height for x,y in altars)
        altar_data.extend(altars)
        anchor_offset=len(anchor_data);groups=world['groups'];members=0
        for group in groups:
            assert group['members'],'Catalog construction requires at least one member'
            seen=set()
            for member in group['members']:
                x,y=member['x'],member['y'];assert member['tile']==y*width+x
                assert 0<=x<width and 0<=y<height and (x,y) not in seen
                seen.add((x,y));anchor_data.append((map_index,x,y));members+=1
        # Any native first member is one of these coordinates. Member order is
        # intentionally irrelevant: every exported member is tested as anchor.
        diameter=max(width-1,height-1)
        map_data.append((altar_offset,len(altars),anchor_offset,members,diameter))
        coverage.append({'map':name,'geometry_id':world['id'],'pool_id':world['poolId'],'map_sha256':world['mapSha256'],'groups':len(groups),'possible_member_anchors':members,
            'altars':len(altars),'nonnegative_ranges':{'first':0,'last':diameter+1},
            'extra_ranges':[-10001,-17,10001,2147483647]})
    def initializers(rows):return ',\n'.join('{'+','.join(str(value) for value in row)+'}' for row in rows)
    source=r'''#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "HEADER"
struct Position{int x,y;};
struct Anchor{int map,x,y;};
struct World{int altar_offset,altar_count,anchor_offset,anchor_count,diameter;};
static constexpr Position altars[]={ALTARS};
static constexpr Anchor anchors[]={ANCHORS};
static constexpr World worlds[]={WORLDS};
static unsigned long long cases=0,steps=0,checks=0,fallbacks=0;
static void check(bool b){++checks;assert(b);}
static int distance(const Anchor&a,const Position&b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
static int old_minimum(const Anchor&anchor,const World&world,int range){
    int minimum=10000;
    for(int i=0;i<world.altar_count;++i)
        minimum=std::min(minimum,std::max(0,distance(anchor,altars[world.altar_offset+i])-range));
    return minimum;
}
static void one(const Anchor&anchor,const World&world,int range){
    ++cases;const int expected=old_minimum(anchor,world,range);
    p11::AltarMinimum cache;
    const auto fallback=[&]{++fallbacks;return old_minimum(anchor,world,range);};
    const auto calls_before=fallbacks;
    check(cache.get(fallback)==expected&&fallbacks==calls_before+1);
    cache.begin(range,std::size_t(world.altar_count));
    check(cache.ready()==(world.altar_count==0)&&cache.cursor()==0);
    for(int i=0;i<world.altar_count;++i){
        // Voluntary suspension between each append does not publish a partial
        // minimum. A zero found early also waits for the full declared input.
        const auto before=fallbacks;
        check(cache.get(fallback)==expected&&fallbacks==before+1);
        check(cache.append(distance(anchor,altars[world.altar_offset+i])));++steps;
        check(cache.cursor()==std::size_t(i+1)&&cache.ready()==(i+1==world.altar_count));
    }
    const auto warm=fallbacks;
    check(cache.get(fallback)==expected&&fallbacks==warm);
    check(cache.get([]()->int{assert(false);return-1;})==expected);
}
int main(){
    for(int map=0;map<int(sizeof(worlds)/sizeof(worlds[0]));++map){
        const auto&world=worlds[map];
        for(int i=0;i<world.anchor_count;++i){
            const auto&anchor=anchors[world.anchor_offset+i];check(anchor.map==map);
            // All larger nonnegative ranges are equivalent to diameter+1:
            // every positive-altar candidate clamps to0; no-altar stays10000.
            for(int range=0;range<=world.diameter+1;++range)one(anchor,world,range);
            for(const int range:{-10001,-17,10001,2147483647})one(anchor,world,range);
        }
    }
    // Synthetic empty input supplies no-altar coverage even if every pool map
    // has altars. It exercises the actual begin/get publication path.
    const Anchor synthetic{-1,0,0};const World empty{0,0,0,0,0};
    for(const int range:{-10001,-17,0,1,10001,2147483647})one(synthetic,empty,range);
    std::printf("{\"cases\":%llu,\"append_steps\":%llu,\"checks\":%llu,\"fallback_calls\":%llu}\n",cases,steps,checks,fallbacks);
}
'''.replace('HEADER',(ROOT/'bots/events/common/src/altar_minimum.h').as_posix()).replace('ALTARS',initializers(altar_data)).replace('ANCHORS',initializers(anchor_data)).replace('WORLDS',initializers(map_data))
    scratch=ROOT/'.cache/arena/orchestration/events-altar-cache/values';scratch.mkdir(parents=True,exist_ok=True)
    fixture=scratch/'all-maps.cpp';fixture.write_text(source,encoding='utf-8',newline='\n')
    unit=ROOT/'bots/events/tests/plan11-altar-minimum.cpp'
    with Slots(1):
        unit_ok,unit_log=run_case('events-altar-minimum-lifecycle',unit,out_dir=scratch/'wasi')
        ok,log=run_case('events-altar-minimum-all-maps',fixture,out_dir=scratch/'wasi') if unit_ok else (False,'UNIT FAILED\n'+unit_log)
    result=json.loads(log) if ok else None
    expected=sum(item['possible_member_anchors']*(item['nonnegative_ranges']['last']+1+len(item['extra_ranges'])) for item in coverage)+6
    if ok:assert result['cases']==expected
    report={'schema':1,'pass':ok,'scope':'Pure actual-cache value/lifecycle fixture. Every group member is tested as a possible native first anchor; no exported-order assumption and no native fuel/trajectory claim.',
        'maps':coverage,'map_count':len(coverage),'group_count':sum(item['groups'] for item in coverage),'possible_member_anchors':len(anchor_data),
        'range_coverage':'All nonnegative integer range equivalence classes0..map Chebyshev diameter+1; larger ranges clamp identically. Explicit-10001/-17/10001/INT_MAX edges remain in the original formula defined domain; -10001 exercises the10000sentinel cap on actual map distances.',
        'range1':'Included as a fixture input, not asserted as a queried native runtime value.',
        'geometry':{'path':'maps/geometry.json','sha256':digest(geometry_path),'engine_sha256':geometry['engineSha256']},
        'pool':{'path':'maps/pool.json','sha256':digest(ROOT/'maps/pool.json')},
        'map_index':{'path':'.cambc/map-pool.json','sha256':digest(index_path)},
        'implementation':{'path':'bots/events/common/src/altar_minimum.h','sha256':digest(ROOT/'bots/events/common/src/altar_minimum.h')},
        'unit_fixture':{'path':unit.relative_to(ROOT).as_posix(),'sha256':digest(unit),'pass':unit_ok,'output':unit_log},
        'driver':{'path':Path(__file__).relative_to(ROOT).as_posix(),'sha256':digest(Path(__file__))},
        'source':{'path':fixture.relative_to(ROOT).as_posix(),'sha256':digest(fixture),'utf8':source},'result':result,'output':log,
        'limitations':['Geometry membership completeness is relied on and pinned; every member position is enumerated, so the native first-member order is not assumed.',
            'The production caller/ranking fixture and native games are separate evidence. This fixture does not establish atomic fuel safety or complete initialization publication.']}
    assert hashlib.sha256(source.encode('utf-8')).hexdigest()==report['source']['sha256']
    out=ROOT/'results/population/events-altar-cache';out.mkdir(parents=True,exist_ok=True)
    (out/'value-fixture.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'pass':ok,'unit_output':unit_log,'maps':len(coverage),'anchors':len(anchor_data),'result':result,'output':log if not ok else None}));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
