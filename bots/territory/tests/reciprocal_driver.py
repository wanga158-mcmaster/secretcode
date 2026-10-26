"""Actual reciprocal-field helpers, warm publication and caller over a graph SDK double.

This checks value and fallback invariants, not native fuel or gameplay health.
"""
from contextlib import nullcontext
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/arena'))
from core import Slots
from wasi import compile_fixture, run_wasm
from startup_driver import extract
from live_driver import live_caller
from phase_driver import phase_macros

def source(runtime):
    names = ['int distance(const cambc::distance_field& field, vec2 p) const',
             'int distance(const cambc::distance_field& field, const bitboard& b) const',
             'void compute(cambc::distance_field& field, const bitboard& roots)',
             'bool reciprocal_sources_ready() const',
             'int reciprocal_source_distance(int source, vec2 origin) const', 'bool warm()']
    bodies = {n: extract(runtime, n) for n in names}
    caller = live_caller(runtime)
    bodies['dynamic caller'] = caller
    return r'''#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>
namespace cambc {
inline unsigned remaining=100000, rounds=1, computes=0; inline bool fail=false;
inline unsigned fuel_remaining(){return remaining;} inline unsigned round_number(){return rounds;}
struct vec2 { int x=0,y=0; };
class bitboard { public:
 std::array<unsigned char,256> bits{};
 void set(vec2 p,bool v=true){bits[p.y*16+p.x]=v;}
 bool is_set(vec2 p)const{return bits[p.y*16+p.x];}
 void draw_chebyshev(vec2 p,int r){for(int y=std::max(0,p.y-r);y<=std::min(15,p.y+r);++y)for(int x=std::max(0,p.x-r);x<=std::min(15,p.x+r);++x)set({x,y});}
 std::vector<vec2> positions()const{std::vector<vec2> out;for(int i=0;i<256;++i)if(bits[i])out.push_back({i%16,i/16});return out;}
 friend bitboard operator~(bitboard b){for(auto&v:b.bits)v=!v;return b;}
 friend bitboard operator&(bitboard a,const bitboard&b){for(int i=0;i<256;++i)a.bits[i]&=b.bits[i];return a;}
};
class distance_field { public:
 static constexpr unsigned unreachable=65535; std::array<unsigned,256> values{};
 distance_field(){values.fill(unreachable);}
 unsigned get(vec2 p)const{return values[p.y*16+p.x];}
 void compute(const bitboard&walls,const bitboard&roots,bitboard&visited,unsigned cap){
  ++computes;values.fill(unreachable);std::vector<int> q;
  for(int i=0;i<256;++i)if(roots.bits[i]&&!walls.bits[i]){values[i]=0;q.push_back(i);}
  if(fail){fail=false;throw std::runtime_error("partial write");}
  for(unsigned h=0;h<q.size();++h){int at=q[h];visited.bits[at]=1;if(values[at]>=cap)continue;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int x=at%16+dx,y=at/16+dy;if((!dx&&!dy)||x<0||x>=16||y<0||y>=16)continue;int n=y*16+x;
    if(!walls.bits[n]&&values[n]==unreachable){values[n]=values[at]+1;q.push_back(n);}}}}
};
enum class team {ally}; struct camp{};struct champion{};
namespace map {inline bitboard spawn_region(team){bitboard b;b.set({0,0});return b;}}
namespace vision {inline std::vector<champion> champions(team){return {};}}
}
using cambc::vec2;using cambc::bitboard;using cambc::camp;using cambc::champion;using cambc::team;
enum class Design {harvest_and_exit,islander}; struct Frame{};struct Group{bitboard spawns;};
inline bitboard around(vec2 p,int r){bitboard b;b.draw_chebyshev(p,r);return b;}
''' + phase_macros(runtime) + r'''
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"assertion line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
class Probe {public:
 Design design_=Design::harvest_and_exit;std::vector<Group> groups_;bitboard terrain_;
 std::array<std::optional<cambc::distance_field>,8> source_fields_;
 std::array<bool,8> reciprocal_source_ready_{};cambc::distance_field current_field_;
 std::vector<vec2> altars_;std::array<std::optional<cambc::distance_field>,2> port_fields_;
 std::array<int,2> port_home_{};std::array<std::array<int,8>,2> port_distances_{};
 std::array<std::array<int,8>,8> source_distances_{};int warm_=2;
 bool sample_turn_=false;std::array<unsigned,8> sample_fuel_{};
 unsigned frame_calls=0,geometry_calls=0,downstream=0;vec2 physical{1,1};
 bitboard port_goals(int)const{return {};}
''' + '\n'.join(bodies[n] for n in names) + r'''
 Frame frame(const std::vector<camp>&,const std::vector<champion>&,const std::vector<champion>&){++frame_calls;return {};}
 void geometry(Frame&){++geometry_calls;}
 void dynamic(){auto round=cambc::round_number();struct Actor{vec2 p;vec2 position()const{return p;}}me{physical};std::vector<camp>camps;std::vector<champion>enemies;
''' + caller + r'''
 ++downstream;}
 void compare(){
  for(int at=0;at<256;++at){vec2 p{at%16,at/16};if(terrain_.is_set(p))continue;
   compute(current_field_,around(p,0));
   for(int i=0;i<static_cast<int>(groups_.size());++i)CHECK(reciprocal_source_distance(i,p)==distance(current_field_,groups_[i].spawns));}}
};
int main(){
 for(int shape=0;shape<5;++shape){Probe p;
  for(int y=0;y<16;++y)if(shape>=1)p.terrain_.set({8,y});
  if(shape==2)p.terrain_.set({8,8},false);
  if(shape==3)for(int x=1;x<8;++x)p.terrain_.set({x,5});
  if(shape==4)for(int i=0;i<256;++i)if((i*17+11)%7==0)p.terrain_.set({i%16,i/16});
  for(auto roots:std::vector<std::vector<vec2>>{{{2,2}},{{12,12},{13,12}},{{1,13},{2,13},{2,14}}}){
   Group g;for(auto r:roots)if(!p.terrain_.is_set(r))g.spawns.set(r);p.groups_.push_back(g);}
  CHECK(!p.reciprocal_sources_ready());
  for(unsigned i=0;i<p.groups_.size();++i){CHECK(p.warm());CHECK(p.reciprocal_source_ready_[i]);}
  CHECK(p.reciprocal_sources_ready());p.compare();
  unsigned before=cambc::computes;p.dynamic();CHECK(cambc::computes==before);CHECK(p.frame_calls==1&&p.geometry_calls==1&&p.downstream==1);
  p.design_=Design::islander;CHECK(!p.reciprocal_sources_ready());p.dynamic();CHECK(cambc::computes==before+1);
  p.design_=Design::harvest_and_exit;p.reciprocal_source_ready_[1]=false;
  p.dynamic();CHECK(cambc::computes==before+2);CHECK(!p.reciprocal_sources_ready());
  CHECK(p.reciprocal_source_distance(1,p.physical)==p.distance(p.current_field_,p.groups_[1].spawns));
 }
 Probe failed;Group g;g.spawns.set({3,3});failed.groups_.push_back(g);cambc::fail=true;
 try{failed.warm();CHECK(false);}catch(const std::runtime_error&){}
 CHECK(!failed.reciprocal_source_ready_[0]&&!failed.reciprocal_sources_ready());
 unsigned before=cambc::computes;failed.dynamic();CHECK(cambc::computes==before+1);CHECK(failed.frame_calls==1);
 Probe cap;Group c;c.spawns.set({0,0});cap.groups_.push_back(c);cap.source_fields_[0].emplace();bitboard visited;
 cap.source_fields_[0]->compute(cap.terrain_,c.spawns,visited,3); // Test equal capped refusal directly.
 cap.reciprocal_source_ready_[0]=true;cap.current_field_.compute(cap.terrain_,around({15,15},0),visited,3);
 CHECK(cap.reciprocal_source_distance(0,{15,15})==-1&&cap.distance(cap.current_field_,c.spawns)==-1);
 std::printf("{\"checks\":%u,\"actual_helpers_and_warm_and_caller\":true,\"native_fuel_claim\":false}\n",checks);
}
''', {n: hashlib.sha256(v.encode()).hexdigest() for n,v in bodies.items()}

def run(work, *, slot_held=False):
    with nullcontext() if slot_held else Slots(1, quiet=True):
        runtime = (HERE.parent / 'common/src/runtime.h').read_text(encoding='utf-8')
        cpp, extracted = source(runtime)
        work.mkdir(parents=True, exist_ok=True)
        path, wasm = work / 'reciprocal.cpp', work / 'reciprocal.wasm'
        path.write_text(cpp, encoding='utf-8', newline='\n')
        ok, log = compile_fixture(path, wasm, defines=['P7_RECIPROCAL_SOURCE_FIELDS=1','P7_REUSE_CURRENT_FIELD=0','P7_LIVE_TRACE=0'])
        assert ok, log
        ok, log = run_wasm(wasm); assert ok, log
        observed = json.loads(log.splitlines()[-1]); assert observed['checks'] > 3000
        report = {'extracted': extracted, 'observed': observed, 'limitations': 'Undirected BFS double; native SDK behavior, fuel and activity require actual games.'}
        (work / 'result.json').write_text(json.dumps(report, indent=2)+'\n',encoding='utf-8',newline='\n')
        print(json.dumps(observed))
        return report
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slot-held', action='store_true')
    args = parser.parse_args()
    run(ROOT / '.cache/arena/orchestration/mass-activation-next/territory_activation/reciprocal-fixture', slot_held=args.slot_held)
