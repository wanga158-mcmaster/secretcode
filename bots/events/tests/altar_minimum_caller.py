"""Equal-input preservation of saved production candidate/frontier blocks.

No native game or SDK observation is created. The supplied observation/model
panel tests cache value substitution, not its fuel benefit or route completion.
"""
from __future__ import annotations
import hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/arena'))
from core import Slots,pool_maps,map_path
from wasi import run_case
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def main() -> int:
    runtime=ROOT/'bots/events/common/src/runtime.h'
    body=runtime.read_bytes().decode('utf-8')
    start=body.index('            for(std::size_t i=0;i<groups_.size();++i) {',body.index('    void farm('))
    end=body.index('            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_preview=',start)
    preview=body[start:end]
    frontier_start=body.index('            const int explore=',end)
    frontier_end=body.index('            promote(target_);promote(fallback);',frontier_start)+len('            promote(target_);promote(fallback);')
    frontier=body[frontier_start:frontier_end]
    detail_start=body.index('            p11::Candidate c;c.id=int(i);c.known=known;c.ready=reward;')
    detail_end=body.index('            if constexpr(requires{Policy::committed_assignment;}) {',detail_start)
    detail=body[detail_start:detail_end]
    clear_start=body.rindex('            const auto clear=p11::clear_schedule(',0,detail_start)
    clear=body[clear_start:body.index('\n',clear_start)]
    assert '#if P11_CACHE_ALTAR_MINIMUM' in preview and '#if P11_CACHE_ALTAR_MINIMUM' in detail
    assert preview.count('altar_minimum.get(')==1 and detail.count('altar_minimum.get(')==1
    geometry_path=ROOT/'maps/geometry.json';geometry=json.loads(geometry_path.read_bytes().decode('utf-8'))
    by_name={m['id'].replace('_','-'):m for m in geometry['maps']}
    selected=pool_maps();assert len(selected)==40 and set(selected)==set(by_name)
    maps=[]
    for name in selected:
        m=by_name[name];assert sha(map_path(name))==m['mapSha256']
        altar=[(tile%m['width'],tile//m['width']) for tile in m['terrain']['altar']]
        groups=[(g['goldValue'],g['gemValue'],g['cooldown'],any('monolith' in x['type'] for x in g['members']),[(p['x'],p['y']) for p in g['members']]) for g in m['groups']]
        maps.append((name,m['width'],m['height'],altar,groups))
    def positions(values):return '{'+','.join('{'+str(x)+','+str(y)+'}' for x,y in values)+'}'
    map_rows=[]
    for name,width,height,altars,groups in maps:
        rows=['{'+','.join([str(gold),str(gems),str(cooldown),str(int(monolith)),positions(members)])+'}' for gold,gems,cooldown,monolith,members in groups]
        map_rows.append('{'+','.join([json.dumps(name),str(width),str(height),positions(altars),'{'+','.join(rows)+'}'])+'}')
    functions=[]
    for knob in (0,1):
        functions.append(f'''#undef P11_CACHE_ALTAR_MINIMUM
#define P11_CACHE_ALTAR_MINIMUM {knob}
    template<class Policy> Plan preview_{knob}() const {{
        const auto&s=supplied;const int round=s.round;const auto origin=from;
        std::vector<p11::Candidate>preview;std::vector<int>order;int screened=0,fallback=-1,best_fallback=100000;
{preview}
{frontier}
        return {{preview,order,fallback,screened}};
    }}
    template<class Policy> p11::Candidate detail_{knob}(std::size_t i,bool known,bool reward) const {{
        const auto&s=supplied;const auto origin=from;const auto&entry=groups_[i];
        const auto&model=models_[i];const float strength=s.strength;const int range=cached_range;
{clear}
{detail}
        return c;
    }}
''')
    source=r'''#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "ROOT/bots/events/common/src/altar_minimum.h"
#include "ROOT/bots/events/common/src/public_work.h"
#include "ROOT/bots/events/overlap/src/policy.h"
#define P11_CACHE_PUBLIC_WORK 1
#define P11_EXPEDITION_RECEIPT 0
namespace cambc{namespace constants{inline int submit_gems_range=1,capture_time=5;}namespace map{inline int map_width=1;int width(){return map_width;}}}
struct Position{int x=0,y=0;};
static int distance(Position a,Position b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Values{int gold=0,gems=0,cooldown=0;int gold_value()const{return gold;}int gem_value()const{return gems;}int max_respawn_timer()const{return cooldown;}};
struct Group{Position anchor;float health=0,incoming=0;bool monolith=false;unsigned unavailable_until=0;Values group;p11::PublicHitWork public_work;p11::AltarMinimum altar_minimum;};
struct Actor{Position at;Position position()const{return at;}bool operator!=(const Actor&other)const{return this!=&other;}};
struct Grid{int value=0;int operator[](int)const{return value;}};
struct Routes{std::vector<Grid>entry,capture;std::vector<int>altar;};
struct Clock{template<class Policy,class...Args>std::vector<int>frontier(Args&&...)const{return{};}};
struct Plan{std::vector<p11::Candidate>preview;std::vector<int>order;int fallback=-1,screened=0;};
struct GroupInput{int gold,gems,cooldown;bool monolith;std::vector<Position>members;};
struct MapInput{const char*name;int width,height;std::vector<Position>altars;std::vector<GroupInput>groups;};
static std::uint64_t assertions=0,candidates=0,plans=0,score_ties=0,detail_evaluations=0;
static void check(bool value){++assertions;assert(value);}
struct Harness{
    p11::State supplied;Position from;int cached_range=1,target_=-1;unsigned identity_=7,monster_shards_=0,jungle_shards_=0;bool packing_ready=true;
    std::vector<Group>groups_;std::vector<Position>altars_,ally_positions;
    std::vector<std::vector<p11::Monster>>models_;std::vector<Actor>allies;Actor me;Routes route;Clock clock;
    bool committed_elsewhere(int)const{return false;}
    const auto&public_models()const{return models_;}const auto&routes()const{return route;}const auto&information_clock()const{return clock;}
FUNCTIONS
};
static auto fields(const p11::Candidate&c){return std::array<std::uint32_t,17>{
    std::uint32_t(c.id),std::uint32_t(c.travel),std::uint32_t(c.clear),std::uint32_t(c.capture),std::uint32_t(c.altar),std::uint32_t(c.gems),std::uint32_t(c.gold),std::uint32_t(c.allies),std::uint32_t(c.enemy_eta),std::uint32_t(c.renewal),std::uint32_t(c.age),std::bit_cast<std::uint32_t>(c.loss),std::bit_cast<std::uint32_t>(c.hp),std::bit_cast<std::uint32_t>(c.incoming),std::uint32_t(c.ready),std::uint32_t(c.known),std::uint32_t(c.committed)};}
template<class Policy>static void same_candidate(const p11::State&s,const p11::Candidate&a,const p11::Candidate&b){
    ++candidates;check(fields(a)==fields(b));check(a.payout()==b.payout()&&a.bank_time(s)==b.bank_time(s));
    check(std::bit_cast<std::uint64_t>(Policy::score(s,a))==std::bit_cast<std::uint64_t>(Policy::score(s,b)));
    check(std::bit_cast<std::uint64_t>(Policy::screen(s,a))==std::bit_cast<std::uint64_t>(Policy::screen(s,b)));
    check(p11::feasible(s,a)==p11::feasible(s,b));
}
template<class Policy>static void compare(Harness&h,int frame){
    ++plans;const auto a=h.preview_0<Policy>(),b=h.preview_1<Policy>();
    check(a.preview.size()==b.preview.size()&&a.order==b.order&&a.fallback==b.fallback&&a.screened==b.screened);
    for(std::size_t i=0;i<a.preview.size();++i){same_candidate<Policy>(h.supplied,a.preview[i],b.preview[i]);
        for(std::size_t j=0;j<i;++j)if(Policy::score(h.supplied,a.preview[i])==Policy::score(h.supplied,a.preview[j]))++score_ties;}
    check(p11::choose<Policy>(h.supplied,a.preview)==p11::choose<Policy>(h.supplied,b.preview));
    check(p11::choose_partial<Policy>(h.supplied,a.preview)==p11::choose_partial<Policy>(h.supplied,b.preview));
    // A supplied allowance is a number of completed detail evaluations, not
    // fuel. Both candidates see the identical frontier prefix and observations.
    for(std::size_t allowance:std::array<std::size_t,4>{0,1,3,99}){
        std::vector<p11::Candidate>left,right;
        for(std::size_t slot=0;slot<std::min(allowance,a.order.size());++slot){
            const std::size_t i=std::size_t(a.order[slot]);const bool known=(int(i)+frame)%2==0,reward=(int(i)+frame)%5==0;
            const auto l=h.detail_0<Policy>(i,known,reward),r=h.detail_1<Policy>(i,known,reward);
            same_candidate<Policy>(h.supplied,l,r);++detail_evaluations;left.push_back(l);right.push_back(r);
        }
        check(p11::shortlist<Policy>(h.supplied,left,3,h.target_,a.fallback)==p11::shortlist<Policy>(h.supplied,right,3,h.target_,b.fallback));
        check(p11::choose<Policy>(h.supplied,left)==p11::choose<Policy>(h.supplied,right));
        check(p11::choose_partial<Policy>(h.supplied,left)==p11::choose_partial<Policy>(h.supplied,right));
    }
}
struct GeoTempo:p11_tempo::Policy{static constexpr bool geodesic=true;};
static Harness seed(const MapInput&m,int frame,int mode){
    Harness h;h.altars_=m.altars;if(frame%8==7)h.altars_.clear();cambc::map::map_width=m.width;
    cambc::constants::submit_gems_range=std::array<int,4>{1,0,3,128}[unsigned(frame)%4];
    h.supplied.round=frame%4==1?1999:(frame%4==2?1990:3);h.supplied.limit=2000;h.supplied.identity=frame%13;
    h.supplied.hp=frame%4==3?200.f:1200.f;h.supplied.maximum=1200;h.supplied.strength=frame%6==5?0.f:45.f+float(frame%3)*20.f;
    h.supplied.bag=frame%3*17;h.supplied.mature=frame%2;h.supplied.field_shop=frame%3==2;
    h.from={m.width/3,m.height/4};h.cached_range=1+frame%3;h.identity_=unsigned(h.supplied.identity);
    h.monster_shards_=frame%3==2?5:0;h.jungle_shards_=frame%4==3?5:0;h.packing_ready=frame%3!=1;
    h.ally_positions={{h.from.x+1,h.from.y},{m.width/2,m.height/2}};
    for(std::size_t i=0;i<m.groups.size();++i){
        const auto&in=m.groups[i];assert(!in.members.empty());Group g;g.anchor=in.members[unsigned(frame)%in.members.size()];
        g.group={in.gold,in.gems,in.cooldown};g.monolith=in.monolith;g.unavailable_until=frame%5==4&&i%9==0?unsigned(h.supplied.round+3):0;
        std::vector<p11::Monster>model;
        for(std::size_t j=0;j<in.members.size();++j){const float hp=120.f+float((i+j+unsigned(frame))%4)*30.f,strength=float((i+j)%3)*4.f;
            model.push_back({hp,strength,0});g.health+=hp;g.incoming+=strength;}
        h.groups_.push_back(g);h.models_.push_back(model);
    }
    h.target_=frame%8==0?-1:(h.groups_.empty()?-1:frame%int(h.groups_.size()));
    for(std::size_t i=0;i<h.groups_.size();++i){auto&g=h.groups_[i];
        if(mode!=1){g.altar_minimum.begin(cambc::constants::submit_gems_range,h.altars_.size());
            const std::size_t steps=mode==2?h.altars_.size()/2:h.altars_.size();
            for(std::size_t j=0;j<steps;++j)g.altar_minimum.append(distance(g.anchor,h.altars_[j]));}
        h.route.entry.push_back({int(11+i%7)});h.route.capture.push_back({int(3+i%4)});h.route.altar.push_back(777+int(i));
    }
    return h;
}
int main(){
    const std::vector<MapInput>maps{MAPS};
    std::uint64_t contexts=0,member_anchor_inputs=0;
    for(const auto&m:maps){int frames=8;for(const auto&g:m.groups)frames=std::max(frames,int(g.members.size()));
        for(int frame=0;frame<frames;++frame)for(int mode=0;mode<3;++mode){auto h=seed(m,frame,mode);++contexts;
            member_anchor_inputs+=h.groups_.size();compare<p11_tempo::Policy>(h,frame);compare<p11_overlap::Policy>(h,frame);compare<GeoTempo>(h,frame);}}
    // Tie cases are separate: they never replace an original map member-anchor
    // rotation above. IDs0,7,14 have the same financial spread term.
    const auto&tie_map=*std::find_if(maps.begin(),maps.end(),[](const auto&m){return m.groups.size()>14;});
    for(int mode=0;mode<3;++mode){auto h=seed(tie_map,0,mode);assert(h.groups_.size()>14);
        h.groups_[7]=h.groups_[0];h.groups_[14]=h.groups_[0];h.models_[7]=h.models_[0];h.models_[14]=h.models_[0];h.target_=-1;
        compare<p11_tempo::Policy>(h,0);compare<p11_overlap::Policy>(h,0);compare<GeoTempo>(h,0);++contexts;}
    // Exact retained early source-model input, independent of export ordering.
    const auto&m=*std::find_if(maps.begin(),maps.end(),[](const auto&m){return std::string(m.name)=="00lanes";});
    for(int mode=0;mode<3;++mode){Harness h;h.altars_=m.altars;h.groups_.resize(15);h.models_.resize(15);h.route.entry.resize(15);h.route.capture.resize(15);h.route.altar.resize(15,777);
        for(auto&g:h.groups_)g.monolith=true;auto&g=h.groups_[14];g.monolith=false;g.anchor={36,27};g.health=240;g.group.gold=40;h.models_[14]={{120,0,0},{120,0,0}};
        h.supplied.round=3;h.supplied.identity=7;h.supplied.hp=h.supplied.maximum=1200;h.supplied.strength=45;h.from={3,13};h.identity_=7;
        cambc::constants::submit_gems_range=1;cambc::constants::capture_time=5;cambc::map::map_width=m.width;
        if(mode!=1){g.altar_minimum.begin(1,h.altars_.size());for(std::size_t j=0;j<(mode==2?h.altars_.size()/2:h.altars_.size());++j)g.altar_minimum.append(distance(g.anchor,h.altars_[j]));}
        const auto a=h.detail_0<p11_overlap::Policy>(14,false,false),b=h.detail_1<p11_overlap::Policy>(14,false,false);same_candidate<p11_overlap::Policy>(h.supplied,a,b);
        check(a.travel==32&&a.clear==6&&a.capture==6&&a.altar==6&&a.loss==0&&a.payout()==44);
        check(p11_overlap::Policy::score(h.supplied,a)==120./51.);compare<p11_tempo::Policy>(h,0);compare<p11_overlap::Policy>(h,0);++contexts;}
    check(score_ties>0);check(maps.size()==40);
    std::printf("{\"maps\":%zu,\"contexts\":%llu,\"anchor_inputs\":%llu,\"plans\":%llu,\"candidate_comparisons\":%llu,\"detail_evaluations\":%llu,\"score_ties\":%llu,\"assertions\":%llu}\n",maps.size(),(unsigned long long)contexts,(unsigned long long)member_anchor_inputs,(unsigned long long)plans,(unsigned long long)candidates,(unsigned long long)detail_evaluations,(unsigned long long)score_ties,(unsigned long long)assertions);
}
'''.replace('ROOT',ROOT.as_posix()).replace('FUNCTIONS','\n'.join(functions)).replace('MAPS',',\n'.join(map_rows))
    scratch=ROOT/'.cache/arena/orchestration/events-altar-cache/caller';scratch.mkdir(parents=True,exist_ok=True)
    fixture=scratch/'caller.cpp';fixture.write_text(source,encoding='utf-8',newline='\n')
    with Slots(1):ok,log=run_case('events-altar-minimum-caller',fixture,out_dir=scratch/'wasi')
    dependencies=['bots/events/common/src/runtime.h','bots/events/common/src/altar_minimum.h','bots/events/common/src/events.h','bots/events/common/src/public_work.h','bots/events/tempo/src/policy.h','bots/events/overlap/src/policy.h','bots/events/overlap/src/clock.h']
    report={'schema':1,'pass':ok,'scope':'Actual saved production preview, frontier promotion and detail Candidate blocks compiled cache0/cache1 with equal supplied inputs and evaluation allowance. SDK-double fixture; no native fuel, route or initialization claim.',
        'source':{'path':fixture.relative_to(ROOT).as_posix(),'sha256':sha(fixture),'utf8':source},
        'driver':{'path':Path(__file__).relative_to(ROOT).as_posix(),'sha256':sha(Path(__file__))},
        'production_dependencies':{p:sha(ROOT/p) for p in dependencies},'geometry':{'path':'maps/geometry.json','sha256':sha(geometry_path)},
        'coverage':{'maps':selected,'cache_modes':['ready','legacy-unready','partially-built-fallback'],'policies':['Tempo','Overlap','GeoTempo override sentinel'],
            'allowances':[0,1,3,99],'states':'early/terminal, healthy/low HP, zero/positive damage, ready/known, packing on/off, commitment, exact score ties, no altars and queried-range variants1/0/3/128',
            'anchors':'Each map group rotates over its member positions across frames; exported member order is a supplied fixture order, not asserted native first-member order.',
            'candidate_fields':'All17namedfields including rawfloatbits; padding ignored. score/screen doublebits and payout/bank_time compared.'},
        'output':log,'metrics':json.loads(log) if ok else None}
    out=ROOT/'results/population/events-altar-cache';out.mkdir(parents=True,exist_ok=True)
    (out/'caller-fixture.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'pass':ok,'output':log,'driver_sha256':sha(Path(__file__)),'runtime_sha256':sha(runtime)}));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
