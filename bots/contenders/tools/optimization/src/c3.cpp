#include "opt/economy.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace opt::economy {
namespace {
constexpr int inf = 100000000;
enum EventKind { clear_event=0, capture_event=1, release_event=2, bank_event=3, recovery_event=4, arrival_event=5 };
template<class T> Json array_json(const std::vector<T>& xs) { Json j=Json::array(); for (const auto& x:xs) j.push_back(x); return j; }
template<class T> std::vector<T> values(const Json& j) { std::vector<T> out; for (const auto& x:j.as_array()) out.push_back(x.get<T>()); return out; }
void require(bool x,const std::string& message) { if(!x) throw std::runtime_error("C3: "+message); }
int tile(const Instance& p,const Json& j) {
    if(j.is_number()) { int t=j.get<int>(); require(t>=0 && t<p.width*p.height,"tile outside map"); return t; }
    require(j.is_array() && j.size()==2,"position must be tile id or [x,y]");
    int x=j[0].get<int>(),y=j[1].get<int>(); require(x>=0&&y>=0&&x<p.width&&y<p.height,"position outside map"); return y*p.width+x;
}
Json point_json(const Instance& p,int t) { return Json::array({t%p.width,t/p.width}); }
int cheb(const Instance& p,int a,int b) { return std::max(std::abs(a%p.width-b%p.width),std::abs(a/p.width-b/p.width)); }
int actor_index(const Instance& p,const Json& j) {
    if(j.is_number()) { int i=j.get<int>(); require(i>=0&&i<int(p.actors.size()),"unknown actor index"); return i; }
    auto name=j.get<std::string>(); for(std::size_t i=0;i<p.actors.size();++i) if(p.actors[i].id==name) return int(i);
    throw std::runtime_error("C3: unknown actor "+name);
}
Json actor_json(const Actor& a) { return Json{{"id",a.id},{"champion",a.champion},{"tile",a.tile},{"spawn",a.spawn},{"available",a.available},{"range",a.range},{"hp",a.hp},{"max_hp",a.max_hp},{"strength",a.strength},{"gold",a.gold},{"earned_gold",a.earned_gold},{"gems",a.gems},{"field_shop",a.field_shop},{"items",array_json(a.items)}}; }
Actor actor_from_json(const Json& j) {
    Actor a; a.id=j.at("id").get<std::string>(); a.champion=j.value("champion",""); a.tile=j.at("tile").get<int>(); a.spawn=j.at("spawn").get<int>();
    a.available=j.value("available",0);a.range=j.value("range",1);a.hp=j.at("hp").get<double>();a.max_hp=j.at("max_hp").get<double>();a.strength=j.at("strength").get<double>();
    a.gold=j.value("gold",0.0);a.earned_gold=j.value("earned_gold",0.0);a.gems=j.value("gems",0);a.field_shop=j.value("field_shop",false);a.items=values<int>(j.at("items"));return a;
}
Json event_json(const Event& e) { return Json{{"at",e.at},{"kind",e.kind},{"group",e.group},{"actor",e.actor},{"participants",array_json(e.participants)},{"end_tiles",array_json(e.end_tiles)},{"incoming",array_json(e.incoming)},{"recipient",e.recipient},{"member",e.member},{"return_tile",e.return_tile}}; }
Event event_from_json(const Json& j) { Event e;e.at=j.at("at").get<int>();e.kind=j.at("kind").get<int>();e.group=j.value("group",-1);e.actor=j.value("actor",-1);e.participants=values<int>(j.at("participants"));e.end_tiles=values<int>(j.at("end_tiles"));e.incoming=values<double>(j.at("incoming"));e.recipient=j.value("recipient",-1);e.member=j.value("member",-1);e.return_tile=j.value("return_tile",-1);return e; }
bool occupied(const Instance& p,const State& s,int g) { for(const auto& a:s.actors) for(int t:p.camps[g].members) if(a.tile==t)return true;return false; }
void respawn(const Instance& p,State& s) { for(std::size_t g=0;g<p.camps.size();++g) if(s.stage[g]==3 && s.ready[g]<=s.now && !occupied(p,s,int(g)))s.stage[g]=0; }
void move_actor(const Instance&p,State&s,int actor,int destination,int at){int old=s.actors[actor].tile;for(std::size_t g=0;g<p.camps.size();++g)if(s.stage[g]==3&&s.ready[g]<=at&&std::find(p.camps[g].members.begin(),p.camps[g].members.end(),old)!=p.camps[g].members.end())s.ready[g]=at+1;s.actors[actor].tile=destination;}
bool in_spawn(const Instance& p,int t) { return std::find(p.spawn_tiles.begin(),p.spawn_tiles.end(),t)!=p.spawn_tiles.end(); }
int bank_ticks(const Instance& p,int gems) { return std::max(p.submit_minimum,int(std::ceil(gems*p.submit_multiplier))); }
std::vector<unsigned char> obstacles(const Instance& p,const State& s,bool awake,int opened) {
    auto blocked=p.blocked;
    for(std::size_t g=0;g<p.camps.size();++g) {
        if(int(g)!=opened && (s.stage[g]==0||s.stage[g]==1))for(int t:p.camps[g].members)blocked[t]=1;
        // A service on an attacking group is the only modeled source of awake damage.
        // Travel is not allowed through its possible attack footprint.
        if(awake && int(g)!=opened && s.stage[g]==1 && p.camps[g].strength>0)
            for(int t=0;t<p.width*p.height;++t)for(int m:p.camps[g].members)if(cheb(p,t,m)<=p.camps[g].range){blocked[t]=1;break;}
    }
    return blocked;
}
std::vector<int> bfs(const Instance& p,const std::vector<unsigned char>& blocked,int source) {
    static thread_local std::unordered_map<std::string,std::vector<int>> cache;
    std::string key=std::to_string(p.width)+":"+std::to_string(source)+":";
    key.append(reinterpret_cast<const char*>(blocked.data()),blocked.size());
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    std::vector<int>d(p.width*p.height,inf);if(source<0||source>=int(d.size()))return d;d[source]=0;std::deque<int>q;q.push_back(source);
    while(!q.empty()){int t=q.front();q.pop_front();int x=t%p.width,y=t/p.width;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=p.width||yy>=p.height)continue;int n=yy*p.width+xx;if(blocked[n]||d[n]!=inf)continue;d[n]=d[t]+1;q.push_back(n);}}
    if(cache.size()>=256)cache.clear();
    cache.emplace(std::move(key),d);return d;
}
std::pair<int,int> nearest_root(const std::vector<int>& d,const std::vector<int>& roots) { int t=-1,cost=inf;for(int r:roots)if(d[r]<cost){cost=d[r];t=r;}return {t,cost}; }
std::vector<int> altar_roots(const Instance& p,const State& s) { auto b=obstacles(p,s,true,-1);std::vector<int> out;for(int t=0;t<p.width*p.height;++t)if(!b[t])for(int a:p.altars)if(cheb(p,t,a)==1){out.push_back(t);break;}return out; }
int attacks(double health,double damage) { if(!(health>0)||!(damage>0))return health<=0?0:inf;float hp=float(health),hit=float(damage);int n=0;while(hp>0&&n<1000000){hp-=hit;++n;}return hp>0?inf:n; }
Json walk_commands(const Instance&p,const std::vector<unsigned char>&blocked,int from,int to){auto d=bfs(p,blocked,from);if(d[to]>=inf)return Json::array();std::vector<int>reversed;int at=to;while(at!=from){reversed.push_back(at);int next=-1;for(int dy=-1;dy<=1&&next<0;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;int x=at%p.width+dx,y=at/p.width+dy;if(x>=0&&y>=0&&x<p.width&&y<p.height){int t=y*p.width+x;if(d[t]==d[at]-1){next=t;break;}}}require(next>=0,"BFS route reconstruction failed");at=next;}Json out=Json::array();for(auto it=reversed.rbegin();it!=reversed.rend();++it)out.push_back(Json{{"type","move"},{"to",point_json(p,*it)}});return out;}
struct Service { int entry{-1},travel{inf},work{},member{-1},capture_walk{};std::vector<int> attack_tiles;Json commands{Json::array()}; };
Service generated_service(const Instance& p,const State& s,int ai,int gi) {
    const auto& actor=s.actors[ai];const auto& camp=p.camps[gi];Service out;auto b=obstacles(p,s,true,-1);auto d=bfs(p,b,actor.tile);
    std::vector<int> left(camp.members.size());std::iota(left.begin(),left.end(),0);int current=actor.tile,total=0;bool first=true;
    while(!left.empty()){
        int chosen=-1,stance=-1,cost=inf;
        for(int k:left)for(int t=0;t<p.width*p.height;++t)if(!b[t]&&cheb(p,t,camp.members[k])<=actor.range&&d[t]<cost){cost=d[t];stance=t;chosen=k;}
        if(chosen<0||cost==inf)return out;
        if(first){out.entry=stance;out.travel=cost;first=false;}else total+=cost;
        auto commands=walk_commands(p,b,current,stance);for(const auto&command:commands.as_array())out.commands.push_back(command);
        int count=attacks(camp.member_hp[chosen],actor.strength);total+=count;if(total>=inf)return Service{};
        out.commands.push_back(Json{{"type","basic"},{"target_tile",point_json(p,camp.members[chosen])},{"repeat",count}});
        out.attack_tiles.push_back(stance);current=stance;d=bfs(p,b,current);left.erase(std::find(left.begin(),left.end(),chosen));
    }
    out.work=std::max(1,total);
    // Capture travels through newly cleared member tiles, while all other camps retain state.
    auto into=distances(p,s,current,true,gi);auto near=nearest_root(into,camp.members);out.member=near.first;out.capture_walk=near.second;
    if(out.capture_walk>=inf)return Service{};
    out.attack_tiles.push_back(current);return out;
}
bool guarded(const Mode& mode,const State& s,const std::vector<int>& actors) {
    auto covered=[&](const std::vector<double>& v,std::size_t i,double x,bool minimum){return i>=v.size()||(minimum?x+1e-6>=v[i]:x-1e-6<=v[i]);};
    for(std::size_t k=0;k<actors.size();++k){const auto&a=s.actors[actors[k]];
        if(a.hp<=0||a.available>s.now||!covered(mode.hp_min,k,a.hp,true)||!covered(mode.hp_max,k,a.hp,false)||!covered(mode.strength_min,k,a.strength,true)||!covered(mode.strength_max,k,a.strength,false))return false;
        if(k<mode.tested_max_hp.size()&&std::abs(a.max_hp-mode.tested_max_hp[k])>1e-6)return false;
        if(k<mode.tested_gold.size()&&std::abs(a.gold-mode.tested_gold[k])>1e-6)return false;
        if(k<mode.tested_gems.size()&&a.gems!=mode.tested_gems[k])return false;
        if(k<mode.tested_range.size()&&a.range!=mode.tested_range[k])return false;
        double incoming=k<mode.incoming.size()?mode.incoming[k]:0;if(a.hp<=incoming)return false;
        if(k<mode.required_items.size()){if(a.items.size()!=mode.required_items[k].size())return false;for(std::size_t i=0;i<a.items.size();++i)if(a.items[i]!=mode.required_items[k][i])return false;}
    }return true;
}
double earned(const State&s){double g=0;for(const auto&a:s.actors)g+=a.earned_gold;return g;}
Json action_base(const std::string& kind,const State&s,int actor){return Json{{"kind",kind},{"start",s.now},{"actor",actor},{"actor_id",s.actors[actor].id}};}
Json settings_json(const Instance&p){return Json{{"horizon",p.horizon},{"goal",p.goal},{"capture_ticks",p.capture_ticks},{"recall_ticks",p.recall_ticks},{"submit_multiplier",p.submit_multiplier},{"submit_minimum",p.submit_minimum}};}
} // namespace

std::vector<int> distances(const Instance& p,const State& s,int source,bool awake,int opened){return bfs(p,obstacles(p,s,awake,opened),source);}

Instance load_instance(const Json& config,const fs::path& repo){
    Instance p;p.horizon=config.value("horizon",2000);p.goal=config.value("goal",1000);p.allow_estimates=config.value("allow_uncalibrated_costs",false);
    require(p.horizon>0&&p.horizon<=1000000&&p.goal>0,"positive finite horizon and goal required");
    Json source=config;Json catalog=Json::object();bool imported=config.contains("map");
    if(imported){
        require(!repo.empty(),"real-map import requires --repo");auto geometry=read_json(repo/"maps/geometry.json");auto map_id=config.at("map").get<std::string>();bool found=false;
        auto map_key=[](std::string name){for(auto&c:name){if(c=='-')c='_';else if(c>='A'&&c<='Z')c=char(c-'A'+'a');}return name;};
        for(const auto&m:geometry.at("maps").as_array())if(map_key(m.at("id").get<std::string>())==map_key(map_id)){source=m;found=true;break;}
        require(found,"unknown map "+map_id);
        catalog=read_json(repo/"docs/reference/runtime-catalog.json").at("result");
        p.provenance=Json{{"map_id",map_id},{"map_sha256",source.at("mapSha256")},{"geometry_sha256",file_sha256(repo/"maps/geometry.json")},{"runtime_catalog_sha256",file_sha256(repo/"docs/reference/runtime-catalog.json")},{"engine","1.0.3"},{"mode_scope","uncontested macro model; basic-only generated services, no native calibration"}};
        const auto& st=catalog.at("settings");p.capture_ticks=st.at("capture_time").get<int>();p.recall_ticks=st.at("recall_time").get<int>();p.submit_multiplier=st.at("submit_gem_duration_multiplier").get<double>();p.submit_minimum=st.at("submit_gem_minimum_duration").get<int>();
    }else p.provenance=Json{{"source","explicit input"},{"mode_scope","input-defined macro costs; not native qualified"}};
    if(config.contains("settings")){const auto& st=config.at("settings");p.capture_ticks=st.value("capture_time",p.capture_ticks);p.recall_ticks=st.value("recall_time",p.recall_ticks);p.submit_multiplier=st.value("submit_multiplier",p.submit_multiplier);p.submit_minimum=st.value("submit_minimum",p.submit_minimum);}
    require(p.capture_ticks>=0&&p.recall_ticks>=1&&p.submit_multiplier>=0&&std::isfinite(p.submit_multiplier)&&p.submit_minimum>=1,"invalid timing settings");
    p.id=config.value("id",imported?config.at("map").get<std::string>():"economy");p.width=source.at("width").get<int>();p.height=source.at("height").get<int>();
    require(p.width>0&&p.height>0&&p.width<=200&&p.height<=200,"map dimensions outside supported 1..200 range");p.blocked.assign(p.width*p.height,0);
    if(imported){
        const auto& terrain=source.at("terrain");for(const std::string kind:{"wall","hole","altar"})for(const auto&t:terrain.at(kind).as_array()){int q=tile(p,t);p.blocked[q]=1;if(kind=="altar")p.altars.push_back(q);}
        auto side=config.value("side","A");require(side=="A"||side=="B","side must be A or B");
        for(const auto& team:{std::string("teamA"),std::string("teamB")}){const auto& r=source.at("spawns").at(team);for(int y=r.at("minY").get<int>();y<r.at("maxY").get<int>();++y)for(int x=r.at("minX").get<int>();x<r.at("maxX").get<int>();++x){int q=tile(p,Json::array({x,y}));if((side=="A")== (team=="teamA"))p.spawn_tiles.push_back(q);else p.blocked[q]=1;}}
    }else{
        if(source.contains("blocked"))for(const auto&t:source.at("blocked").as_array())p.blocked[tile(p,t)]=1;
        for(const auto&t:source.at("altars").as_array()){int q=tile(p,t);p.altars.push_back(q);p.blocked[q]=1;}
        for(const auto&t:source.at("spawns").as_array())p.spawn_tiles.push_back(tile(p,t));
    }
    require(!p.spawn_tiles.empty()&&!p.altars.empty(),"at least one own spawn and altar required");
    Json actor_input=config.contains("actors")?config.at("actors"):Json::array({Json{{"id","ryth"},{"champion","ryth"}},Json{{"id","marcella"},{"champion","marcella"}},Json{{"id","vendetta"},{"champion","vendetta"}}});
    require(actor_input.size()>0&&actor_input.size()<=8,"support 1..8 named actors");std::set<std::string> actor_ids;
    for(std::size_t i=0;i<actor_input.size();++i){const auto& j=actor_input[i];Actor a;a.id=j.value("id","actor"+std::to_string(i));require(actor_ids.insert(a.id).second,"duplicate actor id");a.champion=j.value("champion","");
        if(imported){bool found=false;for(const auto& c:catalog.at("champions").as_array())if(c.at("name").get<std::string>()==a.champion){const auto&st=c.at("stats");a.hp=a.max_hp=st.at("base_health").get<double>();a.strength=st.at("base_strength").get<double>();a.range=st.at("base_attack_range").get<int>();found=true;break;}require(found,"unknown runtime champion "+a.champion);a.field_shop=a.champion=="tavi";}
        a.spawn=p.spawn_tiles[i%p.spawn_tiles.size()];a.tile=j.contains("position")?tile(p,j.at("position")):a.spawn;a.hp=j.value("hp",a.hp);a.max_hp=j.value("max_hp",a.max_hp);a.strength=j.value("strength",a.strength);a.range=j.value("range",a.range);a.gold=j.value("gold",0.0);a.earned_gold=j.value("earned_gold",0.0);a.gems=j.value("gems",0);a.field_shop=j.value("field_shop",a.field_shop);a.available=j.value("available",0);
        require(a.hp>0&&a.hp<=a.max_hp&&a.max_hp>0&&a.strength>0&&a.gold>=0&&a.gems>=0&&a.range>=1&&a.range<=4&&!p.blocked[a.tile],"invalid actor initial state");p.actors.push_back(a);
    }
    if(config.contains("items"))for(const auto&j:config.at("items").as_array()){Item x;x.id=j.at("id").get<std::string>();x.cost=j.at("cost").get<double>();x.strength=j.value("strength",0.0);x.health=j.value("health",0.0);x.max_count=j.value("max_count",20);require(x.cost>0&&x.strength>=0&&x.health>=0&&x.max_count>=0,"invalid item");p.items.push_back(x);}
    else if(imported){Item item;item.id="runtime_strength_shard_stat_mode";item.max_count=20;bool priced=false;for(const auto&i:catalog.at("items").as_array())if(i.value("name","").find("shard")!=std::string::npos){item.cost=i.at("cost").get<double>();priced=true;break;}require(priced,"runtime shard price missing");for(const auto&a:p.actors){bool found=false;for(const auto&c:catalog.at("champions").as_array())if(c.at("name").get<std::string>()==a.champion){item.strength_by_actor.push_back(c.at("stats").at("strength_growth").get<double>());found=true;break;}require(found,"runtime growth missing");}p.items.push_back(item);}
    for(auto&a:p.actors)a.items.assign(p.items.size(),0);
    const auto& groups=source.at(imported?"groups":"camps");
    for(std::size_t g=0;g<groups.size();++g){const auto&j=groups[g];Camp c;c.id=j.value(imported?"key":"id","group"+std::to_string(g));c.gems=j.value(imported?"gemValue":"gems",0);c.gold=j.value(imported?"goldValue":"gold",0.0);c.respawn=j.value(imported?"cooldown":"respawn",50);c.known_available=j.value("known_available",true);
        require(c.gems>=0&&c.gold>=0&&c.respawn>=0,"negative group reward/respawn");
        if(imported){for(const auto&m:j.at("members").as_array()){c.members.push_back(tile(p,Json::array({m.at("x"),m.at("y")})));bool found=false;for(const auto&spec:catalog.at("camps").as_array())if(spec.at("id").get<int>()==m.at("typeId").get<int>()){const auto&st=spec.at("stats");double hp=st.at("base_health").get<double>();c.member_hp.push_back(hp);c.hp+=hp;c.strength+=st.at("base_strength").get<double>();c.range=std::max(c.range,st.at("base_attack_range").get<int>());found=true;break;}require(found,"unknown runtime camp type");}}
        else{for(const auto&t:j.at("members").as_array())c.members.push_back(tile(p,t));c.hp=j.value("hp",120.0);c.strength=j.value("strength",0.0);c.range=j.value("range",3);if(j.contains("member_hp"))c.member_hp=values<double>(j.at("member_hp"));else c.member_hp.assign(c.members.size(),c.hp/std::max<std::size_t>(1,c.members.size()));}
        require(!c.members.empty()&&c.member_hp.size()==c.members.size()&&c.hp>0,"invalid camp member health");
        if(c.strength<=0&&j.value("generate_basic_modes",true)){for(std::size_t a=0;a<p.actors.size();++a){Mode m;m.id="basic_solo_"+p.actors[a].id;m.actors={int(a)};m.generated_basic=true;m.qualification="conservative_basic_macro_model";c.modes.push_back(m);}}
        if(j.contains("modes"))for(const auto& mj:j.at("modes").as_array()){
            Mode m;m.id=mj.at("id").get<std::string>();m.endpoint=mj.value("endpoint","clear");require(m.endpoint=="clear","only endpoint=clear cost modes supported; do not double-count captured atlas outcomes");m.qualification=mj.value("qualification","model_assumption");m.work_rounds=mj.at("work_rounds").get<int>();require(m.work_rounds>=1,"mode work_rounds must be positive");for(const auto&a:mj.at("actors").as_array())m.actors.push_back(actor_index(p,a));require(!m.actors.empty(),"empty party mode");auto distinct=m.actors;std::sort(distinct.begin(),distinct.end());require(std::adjacent_find(distinct.begin(),distinct.end())==distinct.end(),"duplicate party participant");
            if(mj.contains("incoming"))m.incoming=values<double>(mj.at("incoming"));else m.incoming.assign(m.actors.size(),0);
            for(double v:m.incoming)require(v>=0&&std::isfinite(v),"invalid incoming damage");
            require(m.incoming.size()==m.actors.size(),"incoming must name each participant");
            for(const auto& [key,dest]:std::vector<std::pair<std::string,std::vector<double>*>>{{"entry_hp_min",&m.hp_min},{"entry_hp_max",&m.hp_max},{"strength_min",&m.strength_min},{"strength_max",&m.strength_max}})if(mj.contains(key)){*dest=values<double>(mj.at(key));require(dest->size()==m.actors.size(),"entry-domain vectors must name every participant");for(double value:*dest)require(std::isfinite(value)&&value>=0,"entry-domain values must be finite and nonnegative");}
            if(mj.contains("entry_positions"))for(const auto&t:mj.at("entry_positions").as_array())m.entry_tiles.push_back(tile(p,t));
            if(mj.contains("end_positions"))for(const auto&t:mj.at("end_positions").as_array())m.end_tiles.push_back(tile(p,t));
            if(mj.contains("required_items")){for(const auto&a:mj.at("required_items").as_array()){m.required_items.push_back(values<int>(a));require(m.required_items.back().size()==p.items.size(),"required_items must cover the current item catalog");for(int count:m.required_items.back())require(count>=0,"negative required item count");}require(m.required_items.size()==m.actors.size(),"required_items must name every participant");}
            require(m.entry_tiles.size()==m.actors.size()&&m.end_tiles.size()==m.actors.size(),"explicit modes require named entry/end positions");c.modes.push_back(m);
        }
        if(c.strength>0&&p.allow_estimates&&c.modes.empty()){for(std::size_t a=0;a<p.actors.size();++a){Mode m;m.id="uncalibrated_solo_"+p.actors[a].id;m.actors={int(a)};m.explicit_estimate=true;m.generated_basic=true;m.qualification="uncalibrated_aggregate_estimate";c.modes.push_back(m);}}
        p.camps.push_back(c);
    }
    if(config.contains("atlas_file")){
        fs::path atlas_path=config.at("atlas_file").get<std::string>();if(atlas_path.is_relative())atlas_path=repo/atlas_path;auto atlas=read_json(atlas_path);
        require(atlas.value("schema","")=="contenders-atlas-cost-modes/v1","unsupported atlas cost-mode schema");require(config.contains("atlas_bindings"),"atlas_file requires explicit atlas_bindings");
        p.provenance["atlas_file_sha256"]=file_sha256(atlas_path);p.provenance["atlas_bindings"]=config.at("atlas_bindings");p.provenance["atlas_import_scope"]="basic-only clear endpoints at exact tested entry HP/maxHP/strength/range and empty equipment; one fresh-owner overhead; model-only mapping";
        for(const auto&binding:config.at("atlas_bindings").as_array()){
            auto group_id=binding.at("group").get<std::string>();int gi=-1;for(std::size_t g=0;g<p.camps.size();++g)if(p.camps[g].id==group_id)gi=int(g);require(gi>=0,"atlas binding group not found");
            const Json* record=nullptr;for(const auto&r:atlas.at("modes").as_array())if(r.at("id")==binding.at("mode")){record=&r;break;}require(record,"atlas binding mode not found");const auto&r=*record;
            require(r.at("endpoint").get<std::string>()=="clear","atlas endpoint is not clear; capture/release cannot be subtracted into a clear estimate");require(r.contains("entry_state")&&r.contains("exit_state")&&r.contains("action_trace"),"atlas lacks full entry/exit state or action trace; regenerate with current C4");
            const auto&entry=r.at("entry_state");const auto&exit=r.at("exit_state");
            for(const auto&action:r.at("action_trace").as_array()){auto kind=action.value("type","");require(kind=="basic"||kind=="move"||kind=="end"||kind=="priority","C3 atlas importer supports basic/move/end/priority traces only; casts/channels need a richer state-transfer model");}
            int dx=0,dy=0;if(binding.contains("translation")){dx=binding.at("translation")[0].get<int>();dy=binding.at("translation")[1].get<int>();}
            auto translated=[&](const Json&position){return tile(p,Json::array({position[0].get<int>()+dx,position[1].get<int>()+dy}));};
            require(entry.at("actors").size()==r.at("participants").size(),"atlas contains other actors outside the named party");for(const auto&a:entry.at("actors").as_array())require(a.at("team").get<int>()==0,"contested atlas modes are not uncontested economy costs");
            std::set<int> local_blocked;for(const auto&t:entry.at("blocked").as_array())local_blocked.insert(translated(t));for(const auto&t:entry.at("altars").as_array())local_blocked.insert(translated(t));
            int local_width=entry.at("width").get<int>(),local_height=entry.at("height").get<int>();
            for(int y=0;y<local_height;++y)for(int x=0;x<local_width;++x){int actual=translated(Json::array({x,y}));require(bool(p.blocked[actual])==bool(local_blocked.count(actual)),"atlas terrain/altar region differs from mapped real geometry");}
            for(std::size_t other=0;other<p.camps.size();++other)if(int(other)!=gi)for(int t:p.camps[other].members){int x=t%p.width-dx,y=t/p.width-dy;require(x<0||y<0||x>=local_width||y>=local_height,"unrepresented camp lies inside atlas mapping region");}
            std::vector<int>source_members;double source_hp=0,source_strength=0;int source_range=0;
            int goal_group=entry.at("goal").at("group").get<int>();
            for(const auto&c:entry.at("camps").as_array())require(c.at("group").get<int>()==goal_group,"atlas contains unrepresented non-goal camps");
            for(const auto&c:entry.at("camps").as_array())if(c.at("group").get<int>()==goal_group){source_members.push_back(translated(Json::array({c.at("x"),c.at("y")})));source_hp+=c.at("hp").get<double>();source_strength+=c.at("strength").get<double>();source_range=std::max(source_range,c.at("range").get<int>());}
            auto target_members=p.camps[gi].members;std::sort(source_members.begin(),source_members.end());std::sort(target_members.begin(),target_members.end());
            require(source_members==target_members&&std::abs(source_hp-p.camps[gi].hp)<1e-6&&std::abs(source_strength-p.camps[gi].strength)<1e-6&&source_range==p.camps[gi].range,"atlas target geometry/stats do not match bound group");
            Mode m;m.id="atlas:"+r.at("id").get<std::string>();m.qualification="mapped_model_only";m.work_rounds=r.at("work_rounds").get<int>()+1;m.provenance=Json{{"atlas_mode",r},{"binding",binding},{"fresh_owner_overhead",1},{"native_qualification","unvalidated"}};
            for(std::size_t k=0;k<r.at("participants").size();++k){auto source_id=r.at("participants")[k].get<std::string>();require(binding.at("actors").contains(source_id),"atlas actor lacks explicit actor binding");int ai=actor_index(p,binding.at("actors").at(source_id));m.actors.push_back(ai);
                const Json*initial_actor=nullptr;const Json*final_actor=nullptr;for(const auto&a:entry.at("actors").as_array())if(a.at("id").get<std::string>()==source_id)initial_actor=&a;for(const auto&a:exit.at("actors").as_array())if(a.at("id").get<std::string>()==source_id)final_actor=&a;require(initial_actor&&final_actor,"atlas actor state missing");
                const auto&a=*initial_actor;const auto&z=*final_actor;
                require(a.value("can_attack",false)&&a.value("can_move",false)&&a.value("channel","").empty()&&a.value("enhanced_damage",0.0)==0&&a.value("enhanced_stun",0)==0,"atlas entry is not a fresh basic/movement state");
                require(a.value("shield",0.0)==0&&z.value("shield",0.0)==0&&a.value("stun",0)==0&&a.value("mitigation",1.0)==1&&a.value("retaliation",0.0)==0&&a.value("basic_radius",0)==0,"atlas buffs/mitigation/splash require richer C3 actor state");
                require(a.value("camp_damage_multiplier",1.0)==1&&a.value("gold_multiplier",1.0)==1&&!a.value("reset_on_kill",false),"atlas passive damage/reward/reset state is outside basic C3 transfer support");
                for(const auto&sp:a.at("spells").as_array())require(sp.value("cooldown",0)==0,"atlas entry cooldown is not represented by C3");
                for(const auto&sp:z.at("spells").as_array())require(sp.value("cooldown",0)==0,"atlas exit cooldown is not represented by C3");
                require(!a.contains("items")||a.at("items").empty(),"atlas equipment mapping unsupported; provide an explicit independently qualified mode");
                m.hp_min.push_back(a.at("hp").get<double>());m.hp_max.push_back(a.at("hp").get<double>());m.tested_max_hp.push_back(a.at("max_hp").get<double>());m.tested_gold.push_back(a.value("gold",0.0));m.tested_gems.push_back(a.value("gems",0));m.strength_min.push_back(a.at("strength").get<double>());m.strength_max.push_back(a.at("strength").get<double>());m.tested_range.push_back(a.at("range").get<int>());m.required_items.emplace_back(p.items.size(),0);m.incoming.push_back(a.at("hp").get<double>()-z.at("hp").get<double>());require(m.incoming.back()>=0,"atlas healing cannot be reduced to incoming damage");m.entry_tiles.push_back(translated(r.at("start_positions")[k]));m.end_tiles.push_back(translated(r.at("end_positions")[k]));
            }
            {auto party=m.actors;std::sort(party.begin(),party.end());require(std::adjacent_find(party.begin(),party.end())==party.end(),"atlas binding maps multiple participants to the same actor");}
            if(binding.value("replace_generated_modes",false))p.camps[gi].modes.clear();
            p.camps[gi].modes.push_back(std::move(m));
        }
    }
    require(!p.camps.empty(),"no camp groups");Json summary=Json{{"config",config},{"source",source},{"catalog",catalog},{"provenance",p.provenance}};p.resolved_identity=sha256(summary.dump());return p;
}

State initial_state(const Instance&p,const Json&config){
    if(config.contains("initial_state")){State s=state_from_json(config.at("initial_state"));require(s.actors.size()==p.actors.size()&&s.stage.size()==p.camps.size()&&s.ready.size()==p.camps.size(),"replan state dimensions mismatch");require(s.now>=0&&s.now<=p.horizon,"replan time outside horizon");s.trace=0;s.deferred=0;require(s.banked>=0,"negative score");for(const auto&a:s.actors)require(a.tile>=0&&a.tile<p.width*p.height&&a.spawn>=0&&a.spawn<p.width*p.height&&a.hp>0&&a.hp<=a.max_hp&&a.strength>0&&a.gold>=0&&a.gems>=0&&a.items.size()==p.items.size(),"invalid replanned actor state");for(int stage:s.stage)require(stage>=0&&stage<=3,"invalid replanned camp stage");for(const auto&e:s.events){require(e.at>=s.now&&e.kind>=0&&e.kind<=5,"invalid replanned event timing/kind");if(e.kind==clear_event||e.kind==capture_event)require(e.group>=0&&e.group<int(p.camps.size()),"invalid replanned event group");if(e.kind==clear_event){require(e.participants.size()==e.end_tiles.size()&&e.participants.size()==e.incoming.size(),"invalid clear event dimensions");for(std::size_t k=0;k<e.participants.size();++k)require(e.participants[k]>=0&&e.participants[k]<int(s.actors.size())&&e.end_tiles[k]>=0&&e.end_tiles[k]<p.width*p.height&&e.incoming[k]>=0,"invalid clear event participant");}else if(e.kind==capture_event)require(e.recipient>=0&&e.recipient<int(s.actors.size())&&std::find(p.camps[e.group].members.begin(),p.camps[e.group].members.end(),e.member)!=p.camps[e.group].members.end(),"invalid capture recipient/member");else require(e.actor>=0&&e.actor<int(s.actors.size())&&e.return_tile>=0&&e.return_tile<p.width*p.height,"invalid event actor/destination");}return s;}
    State s;s.actors=p.actors;s.stage.assign(p.camps.size(),0);s.ready.assign(p.camps.size(),0);for(std::size_t g=0;g<p.camps.size();++g)if(!p.camps[g].known_available){s.stage[g]=3;s.ready[g]=p.horizon+1;}return s;
}
Json state_json(const State&s){Json actors=Json::array(),events=Json::array();for(const auto&a:s.actors)actors.push_back(actor_json(a));for(const auto&e:s.events)events.push_back(event_json(e));return Json{{"now",s.now},{"deferred",s.deferred},{"banked",s.banked},{"winning_round",s.winning_round},{"actors",actors},{"stage",array_json(s.stage)},{"ready",array_json(s.ready)},{"events",events},{"trace",s.trace}};}
State state_from_json(const Json&j){State s;s.now=j.at("now").get<int>();s.deferred=j.value("deferred",0);s.banked=j.value("banked",0);s.winning_round=j.value("winning_round",-1);for(const auto&a:j.at("actors").as_array())s.actors.push_back(actor_from_json(a));s.stage=values<int>(j.at("stage"));s.ready=values<int>(j.at("ready"));for(const auto&e:j.at("events").as_array())s.events.push_back(event_from_json(e));s.trace=j.value<std::size_t>("trace",0);return s;}
std::string state_key(const State&s){Json j=state_json(s);j["trace"]=0;return j.dump();}
Json trace_json(const std::vector<Trace>&traces,std::size_t index){Json::array_t reversed;while(index){require(index<traces.size(),"invalid trace index");reversed.push_back(traces[index].action);index=traces[index].parent;}std::reverse(reversed.begin(),reversed.end());return Json(std::move(reversed));}

void process_events(const Instance&p,State&s,int until){
    require(until>=s.now,"time reversal");
    while(true){int next=inf;for(const auto&e:s.events)if(e.at<=until)next=std::min(next,e.at);if(next==inf)break;s.now=next;
        std::vector<Event> due,remaining;for(const auto&e:s.events)(e.at==next?due:remaining).push_back(e);s.events=std::move(remaining);std::stable_sort(due.begin(),due.end(),[](const Event&a,const Event&b){return a.kind<b.kind;});
        for(const auto&e:due){
            if(e.kind==clear_event){s.stage[e.group]=2;for(std::size_t k=0;k<e.participants.size();++k){auto&a=s.actors[e.participants[k]];a.hp-=e.incoming[k];require(a.hp>0,"mode kills participant");move_actor(p,s,e.participants[k],e.end_tiles[k],e.at);}}
            else if(e.kind==capture_event){auto&a=s.actors[e.recipient];a.gold+=p.camps[e.group].gold;a.earned_gold+=p.camps[e.group].gold;a.gems+=p.camps[e.group].gems;move_actor(p,s,e.recipient,e.member,e.at);s.stage[e.group]=3;s.ready[e.group]=e.at+p.camps[e.group].respawn;}
            else if(e.kind==release_event)move_actor(p,s,e.actor,e.return_tile,e.at);
            else if(e.kind==bank_event){auto&a=s.actors[e.actor];move_actor(p,s,e.actor,e.return_tile,e.at);s.banked+=a.gems;a.gems=0;if(s.banked>=p.goal&&s.winning_round<0)s.winning_round=e.at;}
            else if(e.kind==recovery_event){auto&a=s.actors[e.actor];move_actor(p,s,e.actor,e.return_tile>=0?e.return_tile:a.spawn,e.at);a.hp=a.max_hp;}
            else if(e.kind==arrival_event)move_actor(p,s,e.actor,e.return_tile,e.at);
        }
        respawn(p,s);
    }
    s.now=until;s.deferred=0;respawn(p,s);
}

std::vector<Expansion> expand(const Instance&p,const State&original){
    State s=original;respawn(p,s);std::vector<Expansion> out;if(s.now>=p.horizon||s.winning_round>=0)return out;
    int ai=-1;for(std::size_t i=0;i<s.actors.size();++i)if(s.actors[i].available<=s.now&&!(s.deferred&(1<<i))){ai=int(i);break;}
    if(ai<0){int next=p.horizon;for(const auto&e:s.events)if(e.at>s.now)next=std::min(next,e.at);for(std::size_t g=0;g<p.camps.size();++g)if(s.stage[g]==3&&s.ready[g]>s.now)next=std::min(next,s.ready[g]);if(next<=s.now)return out;State n=s;process_events(p,n,next);out.push_back({n,Json{{"kind","advance"},{"start",s.now},{"end",next}}});return out;}
    const auto&a=s.actors[ai];auto d=distances(p,s,a.tile);auto b=obstacles(p,s,true,-1);
    // Deferral allows later idle actors to initiate a named joint mode. Once all
    // are deferred, time jumps only to an actual event or respawn eligibility.
    {State n=s;n.deferred|=1<<ai;out.push_back({n,action_base("defer",s,ai)});}
    // Different legal channel roots can lead to different following routes.
    // Enumerate them rather than committing the search to the nearest root.
    if(a.gems>0)for(int root:altar_roots(p,s))if(d[root]<inf){int end=s.now+d[root]+bank_ticks(p,a.gems);if(end<=p.horizon){State n=s;n.actors[ai].available=end;n.deferred&=~(1<<ai);Event e;e.at=end;e.kind=bank_event;e.actor=ai;e.return_tile=root;n.events.push_back(e);Event arrive;arrive.at=s.now+d[root];arrive.kind=arrival_event;arrive.actor=ai;arrive.return_tile=root;if(arrive.at==s.now)move_actor(p,n,ai,root,s.now);else n.events.push_back(arrive);auto action=action_base("bank",s,ai);action["arrival"]=s.now+d[root];action["end"]=end;action["starting_bag"]=a.gems;action["altar_root"]=point_json(p,root);action["precondition"]="uncontested legal route and uninterrupted tribute; current bag transfers at completion";out.push_back({n,action});}}
    if(a.hp+1e-6<a.max_hp || !in_spawn(p,a.tile)){
        // Recall is admitted only outside every currently active group's range.
        bool quiet=true;for(std::size_t g=0;g<p.camps.size();++g)if(s.stage[g]==1&&p.camps[g].strength>0)for(int t:p.camps[g].members)if(cheb(p,a.tile,t)<=p.camps[g].range)quiet=false;
        auto recovery=[&](int root,int travel,const std::string&method){if(travel>=inf||s.now+travel+1>p.horizon)return;State n=s;int arrival=s.now+travel,end=arrival+1;n.actors[ai].available=end;n.deferred&=~(1<<ai);Event e;e.at=end;e.kind=recovery_event;e.actor=ai;e.return_tile=root;n.events.push_back(e);Event arrive;arrive.at=arrival;arrive.kind=arrival_event;arrive.actor=ai;arrive.return_tile=root;if(arrival==s.now)move_actor(p,n,ai,root,s.now);else n.events.push_back(arrive);auto action=action_base("recover",s,ai);action["method"]=method;action["spawn_root"]=point_json(p,root);action["arrival"]=arrival;action["healed_departure"]=end;action["end"]=end;action["precondition"]="recall must remain undamaged; heal occurs at arrival turn end before later departure";out.push_back({n,action});};
        for(int root:p.spawn_tiles)if(!b[root])recovery(root,d[root],"walk");
        if(quiet)recovery(a.spawn,p.recall_ticks,"recall");
    }
    if(in_spawn(p,a.tile)||a.field_shop)for(std::size_t i=0;i<p.items.size();++i){const auto&item=p.items[i];if(a.items[i]>=item.max_count||a.gold+1e-6<item.cost)continue;State n=s;auto&buy=n.actors[ai];buy.gold-=item.cost;buy.strength+=item.strength_by_actor.empty()?item.strength:item.strength_by_actor[ai];buy.max_hp+=item.health;++buy.items[i];auto action=action_base("purchase",s,ai);action["item"]=item.id;action["cost"]=item.cost;action["field_health_does_not_heal"]=true;action["end"]=s.now;out.push_back({n,action});}
    for(std::size_t g=0;g<p.camps.size();++g){const auto&camp=p.camps[g];if(s.stage[g]!=0||!camp.known_available)continue;
        for(const auto&mode:camp.modes){if(std::find(mode.actors.begin(),mode.actors.end(),ai)==mode.actors.end()||!guarded(mode,s,mode.actors))continue;
            if(camp.strength>0){bool cross_fire=false;auto threatened=[&](int at){if(at<0)return false;for(int m:camp.members)if(cheb(p,at,m)<=camp.range)return true;return false;};for(std::size_t other=0;other<s.actors.size();++other)if(std::find(mode.actors.begin(),mode.actors.end(),int(other))==mode.actors.end()){if(threatened(s.actors[other].tile))cross_fire=true;for(const auto&e:s.events){if(e.actor==int(other)&&threatened(e.return_tile))cross_fire=true;for(std::size_t k=0;k<e.participants.size();++k)if(e.participants[k]==int(other)&&threatened(e.end_tiles[k]))cross_fire=true;}}if(cross_fire)continue;}
            std::vector<int>entries,end_tiles;std::vector<double>incoming=mode.incoming;int travel=0,work=mode.work_rounds,member=-1,capture_walk=0;Json basic_commands=Json::array();
            if(mode.generated_basic){if(mode.actors.size()!=1)continue;auto service=generated_service(p,s,ai,int(g));if(service.entry<0)continue;entries={service.entry};end_tiles={service.attack_tiles.back()};travel=service.travel;work=service.work;member=service.member;capture_walk=service.capture_walk;basic_commands=service.commands;
                if(mode.explicit_estimate){incoming={std::max(0,work-1)*camp.strength};if(a.hp<=incoming[0])continue;}else incoming={0};
            }else{
                entries=mode.entry_tiles;end_tiles=mode.end_tiles;std::set<int>distinct(entries.begin(),entries.end());if(distinct.size()!=entries.size())continue;
                bool legal=true;for(std::size_t k=0;k<mode.actors.size();++k){int actor=mode.actors[k];if(b[entries[k]]||b[end_tiles[k]]){legal=false;break;}auto field=distances(p,s,s.actors[actor].tile);if(field[entries[k]]>=inf){legal=false;break;}travel=std::max(travel,field[entries[k]]);}if(!legal)continue;
            }
            if(travel>=inf||work>=inf)continue;
            // The first named participant captures; alternatives belong in explicit modes.
            int recipient=mode.actors.front();int krec=0;
            if(!mode.generated_basic){auto field=distances(p,s,end_tiles[krec],true,int(g));auto near=nearest_root(field,camp.members);member=near.first;capture_walk=near.second;}
            if(member<0||capture_walk>=inf)continue;
            int clear_at=s.now+travel+work,capture_at=clear_at+capture_walk+p.capture_ticks+1,release_at=capture_at+std::max(1,capture_walk);if(release_at>p.horizon)continue;
            State n=s;n.stage[g]=1;for(std::size_t k=0;k<mode.actors.size();++k){int actor=mode.actors[k];n.actors[actor].available=actor==recipient?release_at:clear_at;n.deferred&=~(1<<actor);Event arrive;arrive.at=s.now+travel;arrive.kind=arrival_event;arrive.actor=actor;arrive.return_tile=entries[k];if(arrive.at==s.now)move_actor(p,n,actor,entries[k],s.now);else n.events.push_back(arrive);}
            Event clear;clear.at=clear_at;clear.kind=clear_event;clear.group=int(g);clear.participants=mode.actors;clear.end_tiles=end_tiles;clear.incoming=incoming;n.events.push_back(clear);
            Event capture;capture.at=capture_at;capture.kind=capture_event;capture.group=int(g);capture.recipient=recipient;capture.member=member;n.events.push_back(capture);
            Event release;release.at=release_at;release.kind=release_event;release.group=int(g);release.actor=recipient;release.return_tile=end_tiles[krec];n.events.push_back(release);
            auto action=action_base("camp",s,ai);action["group"]=camp.id;action["group_index"]=int(g);action["mode"]=mode.id;action["qualification"]=mode.qualification;action["participants"]=array_json(mode.actors);action["entry_tiles"]=array_json(entries);action["end_tiles"]=array_json(end_tiles);action["incoming"]=array_json(incoming);action["arrival"]=s.now+travel;action["clear"]=clear_at;action["capture"]=capture_at;action["end"]=release_at;action["recipient"]=recipient;action["reward_gems"]=camp.gems;action["reward_gold"]=camp.gold;action["capture_member"]=member;action["precondition"]="fresh available group, named covered participants, unchanged service-cost domain, no enemy/disturbance/cross-group collateral";action["on_violation"]="abort and replan from a new observed initial_state; never retain modeled future rewards";
            if(!mode.provenance.empty())action["cost_mode_source"]=mode.provenance;
            if(!basic_commands.empty())action["basic_commands"]=basic_commands;
            out.push_back({n,action});
        }
    }
    return out;
}
bool better(const State&c,const State&b){if(c.winning_round>=0||b.winning_round>=0){if(c.winning_round<0)return false;if(b.winning_round<0)return true;return c.winning_round<b.winning_round;}if(c.banked!=b.banked)return c.banked>b.banked;if(earned(c)!=earned(b))return earned(c)>earned(b);return c.now<b.now;}
double priority(const Instance&p,const State&s){
    double bags=0,gold=0,hp=0;for(const auto&a:s.actors){bags+=a.gems;gold+=a.gold;hp+=a.hp/a.max_hp;}
    double pending=0,reserved=0;int bank_finish=s.now;for(const auto&e:s.events){if(e.kind==capture_event)pending+=p.camps[e.group].gems;if(e.kind==bank_event){reserved+=s.actors[e.actor].gems;bank_finish=std::max(bank_finish,e.at);}}
    if(s.winning_round>=0)return 1e12-s.winning_round;
    // The objective is a completed winning tribute. Excess bags are not progress
    // toward an earlier win, and a funded goal-reaching channel deserves prompt
    // evaluation rather than being starved by indefinitely farming more gems.
    if(s.banked+reserved>=p.goal)return 1e10-bank_finish*1000.0-s.now*0.1;
    double needed=std::max(0,p.goal-s.banked),useful_bags=std::min(bags,needed),useful_pending=std::min(pending,std::max(0.0,needed-bags));
    return s.banked*1000.0+useful_bags*350.0+useful_pending*180.0+gold*0.6+hp*2.0-s.now*0.2;
}
Json validate_plan(const Instance&p,const State&initial,const Json&actions){State s=initial;std::size_t n=0;for(const auto&a:actions.as_array()){bool found=false;for(auto child:expand(p,s))if(child.action==a){s=std::move(child.state);found=true;break;}if(!found)return Json{{"valid",false},{"failed_action",n},{"reason","action is not a legal expansion of the declared macro model"}};++n;}return Json{{"valid",true},{"actions",n},{"final_state",state_json(s)}};}
} // namespace opt::economy

namespace opt {
namespace {
void write_schedule_csv(const fs::path&path,const Json&actions){std::ostringstream s;s<<"kind,start,end,actor,group,mode,arrival,clear,capture\n";for(const auto&a:actions.as_array())s<<a.value("kind","")<<','<<a.value("start",0)<<','<<a.value("end",a.value("start",0))<<','<<a.value("actor",-1)<<','<<a.value("group","")<<','<<a.value("mode","")<<','<<a.value("arrival",-1)<<','<<a.value("clear",-1)<<','<<a.value("capture",-1)<<'\n';atomic_write(path,s.str());}
void write_curve_csv(const fs::path&path,const economy::Instance&p,const economy::State&initial,const Json&actions){std::ostringstream out;out<<"round,banked,carried,available_gold,earned_gold,total_hp,busy_actors,pending_events\n";auto s=initial;auto row=[&]{int gems=0,busy=0;double gold=0,earned=0,hp=0;for(const auto&a:s.actors){gems+=a.gems;gold+=a.gold;earned+=a.earned_gold;hp+=a.hp;busy+=a.available>s.now;}out<<s.now<<','<<s.banked<<','<<gems<<','<<gold<<','<<earned<<','<<hp<<','<<busy<<','<<s.events.size()<<'\n';};row();for(const auto&a:actions.as_array())for(auto child:economy::expand(p,s))if(child.action==a){s=std::move(child.state);row();break;}atomic_write(path,out.str());}
Json greedy_baseline(Run&run,const economy::Instance&p,const economy::State&initial){
    const auto limit=run.config.value<std::size_t>("greedy_baseline_limit",20000);
    economy::require(limit>0&&limit<=1000000,"greedy_baseline_limit must be 1..1000000");
    auto current=initial,best=initial;Json actions=Json::array();std::size_t best_count=0,steps=0;
    bool exhausted=false;
    while(steps<limit&&!run.stop()){
        auto choices=economy::expand(p,current);if(choices.empty()){exhausted=true;break;}
        auto rank=[](const Json&a){const auto kind=a.at("kind").get<std::string>();return kind=="bank"?5:kind=="camp"?4:kind=="purchase"?3:kind=="recover"?2:kind=="advance"?1:0;};
        auto chosen=std::max_element(choices.begin(),choices.end(),[&](const auto&a,const auto&b){
            double va=economy::priority(p,a.state),vb=economy::priority(p,b.state);
            return va==vb?rank(a.action)<rank(b.action):va<vb;
        });
        actions.push_back(chosen->action);current=std::move(chosen->state);++steps;
        if(economy::better(current,best)){best=current;best_count=actions.size();}
    }
    actions.as_array().resize(best_count);auto replay=economy::validate_plan(p,initial,actions);
    economy::require(replay.at("valid").get<bool>(),"greedy macro baseline failed model replay");
    return Json{{"schema","contenders-c3-greedy-baseline/v1"},{"qualification","same restricted macro action domain; not a native controller baseline"},
        {"method","deterministic one-step scheduling priority, productive-action tie break"},{"decision_limit",limit},{"decisions",steps},
        {"status",exhausted?"exhausted":run.stop()?"time_or_work_stopped":"decision_limit"},{"banked",best.banked},{"winning_round",best.winning_round},
        {"actions",actions},{"model_replay",replay}};
}
Json execution_contract(const economy::Instance&p,const economy::State&initial,const Json&actions){
    Json modes=Json::array();for(const auto&camp:p.camps)for(const auto&mode:camp.modes)modes.push_back(Json{{"group",camp.id},{"mode",mode.id},{"qualification",mode.qualification}});
    return Json{{"schema","contenders-c3-execution-contract/v1"},{"deployment_ready",false},{"native_execution_verified",false},
        {"scope","uncontested scheduling reference; macro replay is not native execution"},{"initial_model_state",economy::state_json(initial)},
        {"service_mode_qualifications",modes},{"schedule","best-plan.json"},{"replan_state_scope","replan-state.json is a predicted model state, never an observation"},
        {"native_interfaces_required",Json::array({
            "Observed owner-boundary snapshot: actor position, health, gold, bag, equipment, cooldowns; camp life/capture/respawn state; round and allowed observations.",
            "Guarded macro executor: coordinate named participants, legal travel and per-owner actions; execute capture, shop, recall and tribute with actual event order.",
            "State-transfer/cost oracle: validate each service trace and its entry region, abilities/passives/equipment and full exit state; reject unqualified replacements.",
            "Rolling-horizon controller: compare observed states to predicted guards, abort on opponent/disturbance, remove unobserved future rewards and replan from the fresh observation."})},
        {"native_qualification_required",Json::array({"Uncontested schedule replay with prediction error curves.","Disturbance and opposing-policy execution against greedy and persistent-job native controls.","Held-out maps/states and full-game permitted-information, fuel and liveness checks."})},
        {"planned_macro_decisions",actions.size()}};
}
}
int run_c3(const Options&options){
    Run run(options,"c3-economy","1");const auto instance=economy::load_instance(run.config,options.repo);auto initial=economy::initial_state(instance,run.config);
    std::vector<economy::State> frontier;std::vector<economy::Trace> traces(1);std::unordered_set<std::string> seen;economy::State best=initial;
    std::uint64_t beam_dropped=0,duplicates=0;bool closed_limit=false,byte_limit=false;auto max_frontier=run.config.value<std::size_t>("max_frontier",2000),max_closed=run.config.value<std::size_t>("max_closed",500000);economy::require(max_frontier>0&&max_closed>0,"positive frontier/closed limits required");
    const auto max_bytes=run.config.value<std::uint64_t>("max_checkpoint_bytes",268435456);
    economy::require(max_bytes>0&&max_bytes<=4294967296ULL,"max_checkpoint_bytes must be 1..4294967296");
    if(run.resumed){economy::require(run.state.at("resolved_identity").get<std::string>()==instance.resolved_identity,"resolved map/catalog/mode input changed on resume");for(const auto&j:run.state.at("frontier").as_array())frontier.push_back(economy::state_from_json(j));best=economy::state_from_json(run.state.at("best"));traces.clear();for(const auto&j:run.state.at("traces").as_array())traces.push_back({j.at("parent").get<std::size_t>(),j.at("action")});for(const auto&j:run.state.at("seen").as_array())seen.insert(j.get<std::string>());beam_dropped=run.state.value<std::uint64_t>("beam_dropped",0);duplicates=run.state.value<std::uint64_t>("duplicates",0);closed_limit=run.state.value("closed_limit",seen.size()>=max_closed);}
    else {frontier.push_back(initial);seen.insert(economy::state_key(initial));}
    Json baseline=run.resumed&&run.state.contains("greedy_baseline")?run.state.at("greedy_baseline"):greedy_baseline(run,instance,initial);
    write_json(options.out/"greedy-plan.json",baseline);write_json(options.out/"problem.json",run.config);
    if(!run.resumed){
        best=economy::state_from_json(baseline.at("model_replay").at("final_state"));
        std::size_t parent=0;for(const auto&action:baseline.at("actions").as_array()){traces.push_back({parent,action});parent=traces.size()-1;}
        best.trace=parent;
    }
    // Bound serialized state growth before JSON checkpoint construction creates
    // temporary copies. This conservative estimate includes seen-key escaping,
    // frontier copies, trace bodies and per-record overhead; it is not RSS.
    std::uint64_t estimated_bytes=2*baseline.dump().size()+4096;
    for(const auto&key:seen)estimated_bytes+=3*key.size()+512;
    for(const auto&trace:traces)estimated_bytes+=2*trace.action.dump().size()+128;
    byte_limit=(run.resumed&&run.state.value("byte_limit",false))||estimated_bytes>=max_bytes;
    auto checkpoint=[&]{Json fsj=Json::array(),tsj=Json::array(),ssj=Json::array();for(const auto&s:frontier)fsj.push_back(economy::state_json(s));for(const auto&t:traces)tsj.push_back(Json{{"parent",t.parent},{"action",t.action}});std::vector<std::string>ordered(seen.begin(),seen.end());std::sort(ordered.begin(),ordered.end());for(const auto&k:ordered)ssj.push_back(k);run.checkpoint(Json{{"resolved_identity",instance.resolved_identity},{"frontier",fsj},{"best",economy::state_json(best)},{"traces",tsj},{"seen",ssj},{"beam_dropped",beam_dropped},{"duplicates",duplicates},{"closed_limit",closed_limit},{"byte_limit",byte_limit},{"estimated_checkpoint_bytes",estimated_bytes},{"greedy_baseline",baseline}});};
    auto export_best=[&]{auto plan=economy::trace_json(traces,best.trace);auto check=economy::validate_plan(instance,initial,plan);economy::require(check.at("valid").get<bool>(),"incumbent schedule failed model replay");write_json(options.out/"best-plan.json",Json{{"schema","contenders-economy-plan/v1"},{"instance",instance.id},{"resolved_identity",instance.resolved_identity},{"qualification","model_only"},{"banked",best.banked},{"winning_round",best.winning_round},{"settings",economy::settings_json(instance)},{"provenance",instance.provenance},{"actions",plan},{"model_replay",check}});write_schedule_csv(options.out/"schedule.csv",plan);write_curve_csv(options.out/"score-curve.csv",instance,initial,plan);write_json(options.out/"replan-state.json",economy::state_json(best));};
    auto checkpoint_every=run.config.value<std::uint64_t>("checkpoint_every",1000);if(!checkpoint_every)checkpoint_every=1;
    while(!frontier.empty()&&!run.stop()&&!closed_limit&&!byte_limit){
        auto it=std::max_element(frontier.begin(),frontier.end(),[&](const auto&a,const auto&b){return economy::priority(instance,a)<economy::priority(instance,b);});auto s=std::move(*it);frontier.erase(it);
        if(best.winning_round>=0&&s.now>=best.winning_round){run.advance();continue;}
        for(auto child:economy::expand(instance,s)){
            auto key=economy::state_key(child.state);if(seen.contains(key)){++duplicates;continue;}
            auto additional_bytes=3*key.size()+2*child.action.dump().size()+640;
            if(estimated_bytes+additional_bytes>max_bytes){byte_limit=true;break;}
            estimated_bytes+=additional_bytes;seen.insert(std::move(key));
            traces.push_back({s.trace,std::move(child.action)});child.state.trace=traces.size()-1;
            if(economy::better(child.state,best)){best=child.state;export_best();run.event(Json{{"event","incumbent"},{"banked",best.banked},{"winning_round",best.winning_round},{"elapsed_rounds",best.now}});}
            if(child.state.now<instance.horizon&&child.state.winning_round<0)frontier.push_back(std::move(child.state));
            if(seen.size()>=max_closed){closed_limit=true;break;}
        }
        if(frontier.size()>max_frontier){std::stable_sort(frontier.begin(),frontier.end(),[&](const auto&a,const auto&b){return economy::priority(instance,a)>economy::priority(instance,b);});beam_dropped+=frontier.size()-max_frontier;frontier.resize(max_frontier);}
        run.advance();if(run.iteration%checkpoint_every==0){checkpoint();run.progress(Json{{"expanded",run.iteration},{"frontier",frontier.size()},{"seen",seen.size()},{"banked",best.banked},{"winning_round",best.winning_round},{"beam_dropped",beam_dropped},{"estimated_checkpoint_bytes",estimated_bytes}});}if(closed_limit||byte_limit)break;
    }
    checkpoint();export_best();bool exhausted=frontier.empty()&&!closed_limit&&!byte_limit;bool exact=exhausted&&beam_dropped==0;
    Json result{{"engine","C3 complete economy macro planner"},{"status",exact?"exhausted_input_domain":closed_limit?"closed_memory_limit":exhausted?"approximate_frontier_exhausted":"checkpointed_anytime"},{"qualification","model_only; no native schedule execution or competitive claim"},{"instance",instance.id},{"resolved_identity",instance.resolved_identity},{"expanded",run.iteration},{"frontier",frontier.size()},{"seen",seen.size()},{"beam_dropped",beam_dropped},{"duplicate_states",duplicates},{"banked_gems",best.banked},{"winning_round",best.winning_round},{"feasible_win_round_upper_bound",best.winning_round>=0?Json(best.winning_round):Json()},{"win_round_lower_bound",exact&&best.winning_round>=0?best.winning_round:0},{"optimal_within_input_macro_domain",exact},{"provenance",instance.provenance},{"artifacts",Json::array({"best-plan.json","schedule.csv","score-curve.csv","replan-state.json","checkpoint.json"})},{"limitations",Json::array({"Uncontested macro jobs, not a full partially observed engine controller.","Explicit service modes are valid only in their declared entry-state, participant, geometry and equipment region.","Generated basic modes exclude abilities/passives; conservative separate travel and attacks are a schedule abstraction, not a native optimum.","Grid BFS blocks walls, holes, altars, living camps and enemy spawn. Unit collisions, vision, and opponent interference require native execution guards.","Helpers are held at nonmember service end tiles; one recipient captures and vacates. Reward precedes respawn and score only follows channel completion.","Beam truncation forfeits optimality. A closed-state cap stops search rather than silently discarding exact-search evidence.","To replan, supply a fresh input initial_state after observed deviations; pending modeled rewards must not be treated as observations."})}};
    result["greedy_baseline"]=Json{{"banked",baseline.at("banked")},{"winning_round",baseline.at("winning_round")},{"decisions",baseline.at("decisions")},{"status",baseline.at("status")}};
    if(byte_limit)result["status"]="checkpoint_byte_limit";
    result["estimated_checkpoint_bytes"]=estimated_bytes;result["max_checkpoint_bytes"]=max_bytes;
    result["memory_bound_scope"]="Conservative serialized-state budget, not an exact process-RAM bound; measured peak working set still required on the worker.";
    Json groups=Json::array();std::size_t covered=0;for(const auto&camp:instance.camps){covered+=!camp.modes.empty();groups.push_back(Json{{"group",camp.id},{"declared_modes",camp.modes.size()},{"known_available",camp.known_available},{"missing_cost",camp.modes.empty()}});}
    Json coverage{{"groups",groups},{"groups_with_declared_modes",covered},{"total_groups",instance.camps.size()},{"scope","Mode presence does not certify all entry states, equipment or native fidelity."}};
    result["service_coverage"]=coverage;if(covered==0)result["status"]="missing_service_costs";
    write_json(options.out/"service-coverage.json",coverage);result["artifacts"].push_back("service-coverage.json");
    result["search_minus_greedy_banked"]=best.banked-baseline.at("banked").get<int>();
    result["native_readiness"]="essential guarded native executor, state/cost transfer and rolling-observation interfaces remain absent; see execution-contract.json";
    result["artifacts"].push_back("greedy-plan.json");result["artifacts"].push_back("execution-contract.json");result["artifacts"].push_back("problem.json");
    write_json(options.out/"execution-contract.json",execution_contract(instance,initial,economy::trace_json(traces,best.trace)));
    run.finish(result);std::cout<<"C3 banked="<<best.banked<<" win_round="<<best.winning_round<<" expanded="<<run.iteration<<" status="<<result.at("status").get<std::string>()<<"\n";return 0;
}
} // namespace opt
