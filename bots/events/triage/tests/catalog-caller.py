"""Catalog-consistent supplied host under exact production collection/arbitration/fight.

No native opportunity is created or observed. Only a fresh --out directory is
written; shared fixtures, sources and old evidence remain untouched.
"""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core,wasi
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pin=lambda p:{'path':p.relative_to(ROOT).as_posix(),'sha256':sha(p)}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=Path);args=ap.parse_args()
    out=args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT):ap.error('--out must be a fresh repository directory')
    out.mkdir(parents=True)
    assert core.version(core.Bot('events','triage'))=='a74ba55348de'
    runtime=ROOT/'bots/events/common/src/runtime.h';body=runtime.read_bytes().decode('utf-8')
    assert sha(runtime)=='8b3cb30f76b2bc361874e555fc9aa1ddb47dd1284eb427d3e861c8f2d1ae362d'
    a=body.index('    template<class Target>\n    bool fight(');b=body.index('\n    bool event_recover(',a);fight=body[a:b]
    a=body.index('        if constexpr(requires{Policy::bounded_scan;}) {\n            observed_models.resize(',body.index('    void farm('));b=body.index('        std::size_t planned=',a);collect=body[a:b]
    a=body.index('        std::optional<camp> target;',b);b=body.index('        bitboard goals;',a);target=body[a:b]
    assert fight.count('me.attack(target)')==2 and fight.count('ability.use(target)')==1
    assert target.count('p11::focus::select<Policy>')==1 and target.count('fight(*target, false, entry.incoming > 0)')==1
    assert collect.count('target_observations[group].push_back({m,hp,incoming})')==1
    preflight=ROOT/'results/population/triage-opportunity/preflight.json'
    assert sha(preflight)=='0df94e90c97a76caacbed3e75681185220019dd4a840b08085e36d5293f7d9e9'
    catalog=ROOT/'docs/reference/runtime-catalog.json';geometry=ROOT/'maps/geometry.json'
    stats={c['id']:c['stats'] for c in json.loads(catalog.read_bytes())['result']['camps']}
    maps={m['id']:m for m in json.loads(geometry.read_bytes())['maps']}
    defs=[];cases=[]
    for label,map_id,key,strength,shards in [('large','00ambush','g_4320',410,5),('low','00gemfight','g_156',55,0)]:
        m=maps[map_id];groups=m['groups'] if 'groups' in m else m['campGroups'];g=next(g for g in groups if g['key']==key)
        members=g['members'];entries=[]
        for member in members:
            st=stats[member['typeId']]
            entries.append('{'+','.join(map(str,[member['nativeId'],member['x'],member['y'],st['base_health'],st['base_strength']]))+'}')
        width=m['width'];height=m['height']
        defs.append(f'CaseDef {label}(){{return {{"{label}",{width},{height},{strength}.f,{shards},{{'+','.join(entries)+'}};}')
        cases.append({'label':label,'map_id':map_id,'group_key':key,'group':g,'supplied_strength':strength,'monster_shards':shards,'map':pin(core.map_path(map_id))})
    source=r'''#include <cassert>
#include <compare>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include "POLICY"
#include "FOCUS"
struct Pos{int x,y;auto operator<=>(const Pos&)const=default;};
struct bitboard{std::set<Pos>tiles;bool is_set(Pos p)const{return tiles.contains(p);}};
struct Seed{int id,x,y;float hp,incoming;};
struct CaseDef{const char*name;int width,height;float strength;unsigned shards;std::vector<Seed>seeds;};
DEFS
struct Action{const char*kind;int target;float before,after;};
struct World{CaseDef def;std::vector<Seed>monsters;float strength,damage;bool basic=true,invisible=false,flip_mask=false;int only=-1,round=20,round_reads=0,mask_reads=0,health_reads=0,nav=0,finish=0;std::vector<int>epochs,fuels;int fuel_reads=0;bool spell=false,reset=false;float spell_damage=0;std::vector<Action>actions;
 explicit World(CaseDef d):def(d),monsters(d.seeds),strength(d.strength),damage(d.strength*(d.shards>=5?1.1f:1.f)){}
 bool legal(int id)const{return only<0||id==only;}
};
static World*W=nullptr;
struct camp{int index;operator int()const{return W->monsters[index].id;}Pos spawn_position()const{const auto&m=W->monsters[index];return {m.x,m.y};}Pos position()const{return spawn_position();}bool is_alive()const{return W->monsters[index].hp>0;}bool is_visible()const{return !W->invisible;}float health()const{++W->health_reads;return W->monsters[index].hp;}float strength()const{return W->monsters[index].incoming;}};
struct Ability{bool reset;bool is_damage()const{return !reset;}bool is_cc()const{return false;}bool is_ready()const{return reset?W->reset:W->spell;}bool is_shield()const{return false;}bool is_attack_reset()const{return reset;}bitboard valid_tiles()const{bitboard b;for(const auto&m:W->monsters)b.tiles.insert({m.x,m.y});return b;}bool use(const camp&t)const{assert(!reset&&W->spell);auto&m=W->monsters[t.index];const float old=m.hp;m.hp-=W->spell_damage;W->actions.push_back({"spell",m.id,old,m.hp});W->spell=false;return true;}bool use()const{assert(reset&&W->reset);W->reset=false;W->basic=true;W->actions.push_back({"reset",-1,0,0});return true;}};
struct Actor{operator int()const{return 0;}bool can_attack()const{return W->basic;}float strength()const{return W->strength;}std::vector<Ability>abilities()const{return {{false},{true}};}bitboard attack_tiles()const{++W->mask_reads;bitboard b;for(const auto&m:W->monsters)if(W->legal(m.id)&&!(W->flip_mask&&W->mask_reads>=2&&m.id==35))b.tiles.insert({m.x,m.y});return b;}void attack(const camp&t)const{auto&m=W->monsters[t.index];assert(W->basic&&m.hp>0&&!W->invisible&&W->legal(m.id));assert(!(W->flip_mask&&W->mask_reads>=2&&m.id==35));const float old=m.hp;m.hp-=W->damage;W->actions.push_back({"basic",m.id,old,m.hp});W->basic=false;}};
namespace cambc{using handle_camp_t=int;using handle_champion_t=int;Actor&self(){static Actor actor;return actor;}int fuel_remaining(){const int i=W->fuel_reads++;return W->fuels.empty()?60000:W->fuels[std::min(i,int(W->fuels.size()-1))];}int round_number(){const int i=W->round_reads++;return W->epochs.empty()?W->round:W->epochs[std::min(i,int(W->epochs.size()-1))];}namespace map{int width(){return W->def.width;}}}
template<class Policy>struct Harness{
 struct Entry{bitboard spawns;float incoming=0;};struct TargetObservation{camp target;float hp,incoming;};
 std::vector<Entry>groups_;std::vector<int>group_at_tile_;unsigned monster_shards_=0,identity_=11;
 Harness(){groups_.resize(1);group_at_tile_.resize(W->def.width*W->def.height,-1);monster_shards_=W->def.shards;for(const auto&m:W->def.seeds){groups_[0].spawns.tiles.insert({m.x,m.y});group_at_tile_[m.y*W->def.width+m.x]=0;groups_[0].incoming+=m.incoming;}}
 template<class T>bool tactical_fight(const T&,bool,bool){assert(false);return false;}
 template<class...T>void publish_commitment(T&&...){assert(false);}
 void finish_clear_turn(Entry&,int){++W->finish;}
FIGHT
 void run(){auto&me=cambc::self();const int round=W->round;p11::State s;s.round=round;s.strength=W->strength;s.hp=s.maximum=W->def.name==std::string("large")?1275.f:1000.f;
  std::vector<camp>camps;for(std::size_t i=0;i<W->monsters.size();++i)camps.push_back({int(i)});
  std::vector<int>order{0};std::vector<std::vector<p11::Monster>>observed_models;std::vector<std::vector<TargetObservation>>target_observations(groups_.size());
COLLECT
  const int selected=0;auto&entry=groups_[selected];
TARGET
  ++W->nav;
 }
};
static int checks=0;void check(bool b){++checks;if(!b){std::fflush(stdout);std::fprintf(stderr,"fixture check=%d round=%d damage=%.9g actions=%zu nav=%d\n",checks,W?W->round:-1,W?W->damage:0,W?W->actions.size():0,W?W->nav:-1);if(W)for(const auto&a:W->actions)std::fprintf(stderr,"action %s target%d hp%.9g->%.9g\n",a.kind,a.target,a.before,a.after);}assert(b);}
void print_case(const char*name,const World&w){std::printf("{\"name\":\"%s\",\"navigation_boundary\":%d,\"finish_stub_calls\":%d,\"health_reads\":%d,\"actions\":[",name,w.nav,w.finish,w.health_reads);for(std::size_t i=0;i<w.actions.size();++i){const auto&a=w.actions[i];std::printf("%s{\"kind\":\"%s\",\"target\":%d,\"hp_before\":%.9g,\"hp_after\":%.9g}",i?",":"",a.kind,a.target,a.before,a.after);}std::printf("]}\n");}
template<class P>void turn(World&w){W=&w;w.round_reads=w.fuel_reads=w.mask_reads=0;Harness<P>h;h.run();}
int first_basic(const World&w){for(const auto&a:w.actions)if(std::string(a.kind)=="basic")return a.target;return -1;}
template<class P>World three(CaseDef d,const char*name,std::array<int,3>expected,float exposure){World w(d);float total=0;for(int phase=0;phase<3;++phase){w.round=20+phase;w.basic=true;const auto count=w.actions.size();turn<P>(w);check(w.actions.size()==count+1&&w.actions.back().target==expected[phase]&&std::string(w.actions.back().kind)=="basic");for(const auto&m:w.monsters)if(m.hp>0)total+=m.incoming;}check(total==exposure);print_case(name,w);return w;}
int main(){
 check(p11::attacks(1350.f,451.f)==3&&p11::attacks(2400.f,451.f)==6);
 three<p11_tempo::Policy>(large(),"large-baseline-three",{33,33,33},240);three<p11_triage::Policy>(large(),"large-triage-three",{35,35,35},220);
 check(p11::attacks(1350.f,55.f)==25&&p11::attacks(120.f,55.f)==3);
 three<p11_tempo::Policy>(low(),"low-baseline-three",{22,22,22},60);three<p11_triage::Policy>(low(),"low-triage-three",{26,22,22},60);
 {World w(low());w.strength=w.damage=60;turn<p11_triage::Policy>(w);check(first_basic(w)==22);print_case("lower-boundary60-no-deviation",w);}
 {World w(large());w.fuels={45001};turn<p11_triage::Policy>(w);check(first_basic(w)==35);print_case("fuel45001",w);}
 {World w(large());w.fuels={45000};turn<p11_triage::Policy>(w);check(first_basic(w)==33);print_case("fuel45000-baseline-action",w);}
 {World w(large());w.fuels={45001,45000,45000};turn<p11_triage::Policy>(w);check(first_basic(w)==33);print_case("post-mask45000-baseline-action",w);}
 {World w(large());w.epochs={21};turn<p11_triage::Policy>(w);check(w.actions.empty()&&w.nav==0);print_case("stale-before-selection",w);}
 {World w(large());w.epochs={20,20,21};turn<p11_triage::Policy>(w);check(w.actions.empty()&&w.nav==0);print_case("stale-after-selection",w);}
 {World w(large());w.basic=false;turn<p11_triage::Policy>(w);check(w.actions.empty()&&w.nav==1);print_case("no-basic-no-reset",w);}
 {World w(large());w.basic=false;w.reset=true;turn<p11_triage::Policy>(w);check(w.actions.size()==2&&first_basic(w)==33);print_case("no-basic-reset-baseline-action",w);}
 {World w(large());w.only=33;turn<p11_triage::Policy>(w);check(first_basic(w)==33);print_case("mask-only-baseline",w);}
 {World w(large());w.flip_mask=true;turn<p11_triage::Policy>(w);check(w.actions.empty()&&w.nav==1);print_case("fresh-mask-refuses-planned-basic",w);}
 {World w(low());turn<p11_triage::Policy>(w);check(first_basic(w)==26);w.basic=true;w.round=21;w.only=22;turn<p11_triage::Policy>(w);check(w.actions.size()==2&&w.actions.back().target==22);print_case("fresh-next-owner-replans",w);}
 {World w(large());w.strength=w.damage=0;turn<p11_triage::Policy>(w);check(first_basic(w)==33);print_case("zero-damage-baseline-executor",w);}
 {World w(large());w.monsters.push_back({999,0,0,1,1000});turn<p11_triage::Policy>(w);check(first_basic(w)==35);print_case("out-of-group-filtered",w);}
 {World w(large());w.spell=true;w.spell_damage=2000;turn<p11_triage::Policy>(w);check(w.actions.size()==1&&std::string(w.actions[0].kind)=="spell"&&w.actions[0].target==35&&first_basic(w)==-1);print_case("offensive-spell-kills-before-basic",w);}
 {World w(large());w.spell=true;w.spell_damage=10;turn<p11_triage::Policy>(w);check(w.actions.size()==2&&std::string(w.actions[0].kind)=="spell"&&first_basic(w)==35);print_case("offensive-spell-precedes-basic",w);}
 {World w(large());w.invisible=true;turn<p11_triage::Policy>(w);check(w.actions.empty()&&w.nav==1);print_case("fresh-invisible-refuses-fight",w);}
 std::printf("{\"summary\":true,\"checks\":%d,\"pass\":true}\n",checks);
}
'''.replace('POLICY',(ROOT/'bots/events/triage/src/policy.h').as_posix()).replace('FOCUS',(ROOT/'bots/events/common/src/focus_caller.h').as_posix()).replace('DEFS','\n'.join(defs)).replace('FIGHT',fight).replace('COLLECT',collect).replace('TARGET',target)
    fixture=out/'caller.cpp';fixture.write_text(source,encoding='utf-8',newline='\n');wasm=out/'caller.wasm'
    argv=[str(core.CLANG),*wasi.FLAGS,'-Werror',f'-I{ROOT/"sdk/cpp"}',str(fixture),f'-L{wasi.EH}','-lunwind','-o',str(wasm)]
    with core.Slots(1):compile_run=subprocess.run(argv,cwd=ROOT,capture_output=True)
    (out/'compile.stdout').write_bytes(compile_run.stdout);(out/'compile.stderr').write_bytes(compile_run.stderr)
    report={'schema':'triage-catalog-caller/v1','source_version':'a74ba55348de','driver':{**pin(Path(__file__)),'body_utf8':Path(__file__).read_bytes().decode('utf-8')},'fixture':pin(fixture),'runtime':pin(runtime),'catalog':pin(catalog),'geometry':pin(geometry),'opportunity':pin(preflight),'cases':cases,'extraction':{k:{'sha256':hashlib.sha256(v.encode('utf-8')).hexdigest(),'body_utf8':v} for k,v in [('observation_collection',collect),('target_to_fight',target),('fight',fight)]},'compile_argv':argv,'compile_exit':compile_run.returncode,'compile_stdout':pin(out/'compile.stdout'),'compile_stderr':pin(out/'compile.stderr'),'pass':False,'native_queries':0,'games':0,'whole_bot_builds':0,'jobs':False}
    if compile_run.returncode==0:
        argv=[str(core.NODE),'--disable-warning=ExperimentalWarning',str(wasi.DRIVER),str(wasm)]
        with core.Slots(1):run=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=600)
        (out/'run.stdout').write_bytes(run.stdout);(out/'run.stderr').write_bytes(run.stderr)
        report.update(run_argv=argv,run_exit=run.returncode,stdout=pin(out/'run.stdout'),stderr=pin(out/'run.stderr'),wasm=pin(wasm))
        lines=run.stdout.decode('utf-8').splitlines();unknown=[line for line in lines if not(line.startswith('{')or line.startswith('p11triage '))]
        report['unknown_stdout_lines']=unknown;report['receipts']=[json.loads(line) for line in lines if line.startswith('{')]
        report['pass']=run.returncode==0 and not unknown and bool(report['receipts']) and report['receipts'][-1].get('pass') is True
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'pass':report['pass'],'report':pin(out/'report.json'),'compile_exit':compile_run.returncode,'run_exit':report.get('run_exit')}))
    return 0 if report['pass'] else 1
if __name__=='__main__':sys.exit(main())
