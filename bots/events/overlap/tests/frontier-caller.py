"""Actual runtime frontier/promotion block: supplied-input OFF/ON equivalence.

Writes only a fresh --out directory. No game, engine, replay query or fixture
under bots/events/tests is modified or run through its evidence-writing entrypoint.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
import wasi

sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def pin(p):return {'path':p.relative_to(ROOT).as_posix(),'sha256':sha(p)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    out=args.out.resolve()
    if not out.is_relative_to(ROOT) or out.exists():parser.error('--out must be a fresh repository directory')
    out.mkdir(parents=True)
    runtime=ROOT/'bots/events/common/src/runtime.h'
    body=runtime.read_bytes().decode('utf-8')
    start=body.index('            const int explore=',body.index('    void farm('))
    end=body.index('            promote(target_);promote(fallback);',start)+len('            promote(target_);promote(fallback);')
    block=body[start:end]
    assert block.count('p11::shortlist<Policy>')==1 and 'Policy::detail_frontier' in block
    source=r'''#include <cassert>
#include <bit>
#include <cstdio>
#include <cstdint>
#define P11_EXPEDITION_RECEIPT 0
#include "POLICY"
struct Clock{template<class P,class...A>std::vector<int>frontier(A&&...)const{return{};}};
struct Harness{
 std::vector<int>groups_;unsigned identity_=7;int target_=-1;Clock clock;
 const Clock&information_clock()const{return clock;}
 template<class Policy>std::vector<int>actual(const p11::State&s,const std::vector<p11::Candidate>&preview,int fallback,int round)const{
  std::vector<int>order;
BLOCK
  return order;
 }
};
using Policy=p11_overlap::Policy;
static std::uint64_t digest=1469598103934665603ULL,checks=0,cases=0,ties=0;
void mix(std::uint64_t v){digest^=v;digest*=1099511628211ULL;}
void check(bool v){++checks;assert(v);}
void order_hash(const std::vector<int>&v){mix(v.size());for(int x:v)mix(std::uint32_t(x));}
auto fields(const p11::Candidate&c){return std::array<std::uint32_t,17>{
std::uint32_t(c.id),std::uint32_t(c.travel),std::uint32_t(c.clear),std::uint32_t(c.capture),std::uint32_t(c.altar),std::uint32_t(c.gems),std::uint32_t(c.gold),std::uint32_t(c.allies),std::uint32_t(c.enemy_eta),std::uint32_t(c.renewal),std::uint32_t(c.age),std::bit_cast<std::uint32_t>(c.loss),std::bit_cast<std::uint32_t>(c.hp),std::bit_cast<std::uint32_t>(c.incoming),std::uint32_t(c.ready),std::uint32_t(c.known),std::uint32_t(c.committed)};}
std::vector<int>reference(const p11::State&s,const std::vector<p11::Candidate>&preview,int limit,int target,int fallback,int explore){
 const int owned=target>=0?target:fallback;
 const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
 return p11::shortlist<Policy>(s,preview,limit,owned,alternative);
}
int main(){
 for(int length:std::array{0,1,2,3,5,12,65})for(int frame=0;frame<4;++frame){
  p11::State s;s.round=frame==3?1999:frame*101;s.identity=frame*3+1;s.bag=frame*7;s.hp=frame==2?600:1000;s.mature=frame==1;s.field_shop=frame==2;
  std::vector<p11::Candidate>preview;
  for(int i=0;i<length;++i){p11::Candidate c;c.id=i%11==0?-1:(i%9==0?-2:(i%7)*7);c.travel=i%4;c.clear=i%5;c.altar=i%3;c.gems=(i%4)*5;c.gold=100+(i%3)*20;c.hp=120;c.loss=(i%3)*150;c.incoming=i%4;c.ready=i%5==0;c.known=i%2;c.committed=i%6==0;preview.push_back(c);}
  // Deliberate equal-score, duplicate and invalid-ID rows preserve stable ties.
  if(length>=3){preview[0]=preview[1];preview[0].id=0;preview[1].id=7;preview[2]=preview[1];}
  const auto saved=preview;
  for(const auto&c:preview){for(auto v:fields(c))mix(v);mix(std::bit_cast<std::uint64_t>(Policy::score(s,c)));mix(std::bit_cast<std::uint64_t>(Policy::screen(s,c)));}
  for(std::size_t i=0;i<preview.size();++i)for(std::size_t j=0;j<i;++j)if(Policy::screen(s,preview[i])==Policy::screen(s,preview[j]))++ties;
  for(int target:std::array{-2,-1,0,2,7,99})for(int fallback:std::array{-1,0,2,7,99}){
   Harness h;h.groups_.resize(length);h.target_=target;h.identity_=unsigned(s.identity);
   const int explore=length==0?-1:int((unsigned(s.round/8)+h.identity_)%h.groups_.size());
   auto expected=reference(s,preview,3,target,fallback,explore);
   auto promote=[&](int id){auto found=std::find(expected.begin(),expected.end(),id);if(found!=expected.end())std::rotate(expected.begin(),found,found+1);};promote(target);promote(fallback);
   auto actual=h.actual<Policy>(s,preview,fallback,s.round);check(actual==expected);order_hash(actual);++cases;
   for(int ex:std::array{-1,0,2,7,99})for(int limit:std::array{-7,0,1,2,3,5,32,99}){
    auto base=reference(s,preview,limit,target,fallback,ex);
#if P11_OVERLAP_INLINE_SHORTLIST
    auto hooked=Policy::detail_frontier(s,preview,limit,target,fallback,ex);check(hooked==base);
#endif
    order_hash(base);++cases;
   }
   mix(std::uint32_t(p11::choose<Policy>(s,preview)));mix(std::uint32_t(p11::choose_partial<Policy>(s,preview)));
  }
  check(preview.size()==saved.size());for(std::size_t i=0;i<preview.size();++i)check(fields(preview[i])==fields(saved[i]));
 }
 assert(ties>0);std::printf("{\"digest\":\"%016llx\",\"cases\":%llu,\"ties\":%llu}\n",(unsigned long long)digest,(unsigned long long)cases,(unsigned long long)ties);
}
'''.replace('POLICY',(ROOT/'bots/events/overlap/src/policy.h').as_posix()).replace('BLOCK',block)
    fixture=out/'caller.cpp';fixture.write_text(source,encoding='utf-8',newline='\n')
    arms=[]
    for knob in (0,1):
        wasm=out/f'caller-{knob}.wasm'
        argv=[str(core.CLANG),*wasi.FLAGS,'-Werror',f'-I{ROOT/"sdk/cpp"}',f'-DP11_OVERLAP_INLINE_SHORTLIST={knob}',str(fixture),f'-L{wasi.EH}','-lunwind','-o',str(wasm)]
        with core.Slots(1):compiled=subprocess.run(argv,cwd=ROOT,capture_output=True)
        (out/f'compile-{knob}.stdout').write_bytes(compiled.stdout);(out/f'compile-{knob}.stderr').write_bytes(compiled.stderr)
        arm={'knob':knob,'compile_argv':argv,'compile_exit':compiled.returncode,'compile_stdout':pin(out/f'compile-{knob}.stdout'),'compile_stderr':pin(out/f'compile-{knob}.stderr')}
        arms.append(arm)
        if compiled.returncode:break
        argv=[str(core.NODE),'--disable-warning=ExperimentalWarning',str(wasi.DRIVER),str(wasm)]
        with core.Slots(1):run=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=600)
        (out/f'run-{knob}.stdout').write_bytes(run.stdout);(out/f'run-{knob}.stderr').write_bytes(run.stderr)
        arm.update(wasm=pin(wasm),run_argv=argv,run_exit=run.returncode,stdout=pin(out/f'run-{knob}.stdout'),stderr=pin(out/f'run-{knob}.stderr'))
        if run.returncode:break
        arm['result']=json.loads(run.stdout)
    passed=len(arms)==2 and all(a.get('run_exit')==0 for a in arms) and arms[0]['result']==arms[1]['result']
    report={'schema':'overlap-frontier-caller/v1','pass':passed,'driver':pin(Path(__file__)),'fixture':pin(fixture),'runtime':pin(runtime),'policy':pin(ROOT/'bots/events/overlap/src/policy.h'),'arms':arms,'scope':'Extracted actual runtime frontier+promotion caller and actual common shortlist; supplied-input OFF/ON equivalence, not native fuel or Road feasibility. Direct actual hook calls cover clamp limits; production literal3 block covers promotions.','native_games':0,'native_queries':0}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'pass':passed,'report':pin(out/'report.json'),'results':[a.get('result') for a in arms]}))
    return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
