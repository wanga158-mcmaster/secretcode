#include "opt/sim.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>

namespace opt::sim {
namespace {
float f(const J& j, const std::string& k, float d=0) { return j.value<float>(k,d); }
int n(const J& j, const std::string& k, int d=0) {
    if(!j.contains(k))return d;
    const double value=j.at(k).get<double>();
    if(!std::isfinite(value)||std::trunc(value)!=value||value<std::numeric_limits<int>::min()||value>std::numeric_limits<int>::max())throw std::runtime_error("restricted sim: integer required for "+k);
    return static_cast<int>(value);
}
bool b(const J& j, const std::string& k, bool d=false) { return j.value<bool>(k,d); }
int dist(int x,int y,const J& q) { return std::max(std::abs(x-n(q,"x")),std::abs(y-n(q,"y"))); }
bool live(const J& a) { return f(a,"hp")>0; }
bool same_tile(int x,int y,const J& a) { return x==n(a,"x") && y==n(a,"y"); }
bool cell(const J& a,int x,int y) {
    for(const auto& p:a.as_array()) if(p.at(0).get<int>()==x && p.at(1).get<int>()==y) return true;
    return false;
}
bool inside(const J& s,int x,int y) { return x>=0&&y>=0&&x<n(s,"width")&&y<n(s,"height"); }
bool walkable(const J& s,int x,int y,int who) {
    if(!inside(s,x,y)||cell(s.at("blocked"),x,y)||cell(s.at("altars"),x,y)) return false;
    for(std::size_t i=0;i<s.at("actors").size();++i) if(static_cast<int>(i)!=who) {
        const auto& a=s.at("actors").at(i);
        if(live(a)&&same_tile(x,y,a)) return false;
        if(n(a,"team")!=n(s.at("actors").at(who),"team") &&
           a.at("spawn").at(0).get<int>()==x && a.at("spawn").at(1).get<int>()==y) return false;
    }
    for(const auto& c:s.at("camps").as_array()) if(live(c)&&same_tile(x,y,c)) return false;
    return true;
}
bool on_group(const J& s,const J& a,int group) {
    for(const auto& c:s.at("camps").as_array()) if(n(c,"group")==group && same_tile(n(a,"x"),n(a,"y"),c)) return true;
    return false;
}
bool on_spawn(const J& a) { return n(a,"x")==a.at("spawn").at(0).get<int>()&&n(a,"y")==a.at("spawn").at(1).get<int>(); }
int group_index(const J& s,int id) {
    for(std::size_t i=0;i<s.at("groups").size();++i) if(n(s.at("groups").at(i),"id")==id) return static_cast<int>(i);
    return -1;
}
void invalidate_capture(J& s) {
    // Native capture progress is cancelled by the action which removes the
    // final allied occupant or admits a contesting enemy, not the next phase.
    for(auto& g:s["groups"].as_array()) if(g.value("status","")=="reward"&&n(g,"capture_team")>=0) {
        bool present[2]={false,false};
        for(const auto& actor:s.at("actors").as_array()) if(live(actor)&&on_group(s,actor,n(g,"id")))present[n(actor,"team")]=true;
        const int team=n(g,"capture_team");
        if(!present[team]||present[1-team]) {g["capture_team"]=-1;g["capture_left"]=-1;}
    }
}
void require(bool good,const std::string& message) { if(!good) throw std::runtime_error("restricted sim: "+message); }
void finite(float v,const std::string& what,bool positive=false) {
    require(std::isfinite(v)&&(positive?v>0:v>=0),"invalid "+what);
}
J normalized_spell(const J& in) {
    J s=in;
    const auto kind=s.value("kind","none");
    require(kind=="none"||kind=="damage"||kind=="stun"||kind=="reset"||kind=="shield","unsupported spell kind "+kind);
    s["kind"]=kind;
    for(const auto& k:{"range","radius","damage","shield","enhanced_damage"}) { const float v=f(s,k); finite(v,k); s[k]=v; }
    for(const auto& k:{"cooldown","duration","enhanced_stun"}) { require(n(s,k)>=0,std::string("negative spell ")+k); s[k]=n(s,k); }
    s["max_cooldown"]=n(s,"max_cooldown",8);
    require(n(s,"max_cooldown")>=0,"negative max cooldown");
    s["exclusive"]=b(s,"exclusive");
    return s;
}
void reset_permissions(J& a) {
    const bool allowed=live(a)&&n(a,"stun")==0&&a.value("channel","").empty();
    a["can_move"]=allowed;a["can_attack"]=allowed;a["can_cast"]=allowed;
}
void owner_start(J& s,int who) {
    auto& a=s["actors"][static_cast<std::size_t>(who)];
    if(n(a,"shield_ticks")==0) a["shield"]=0.0f;
    const auto channel=a.value("channel","");
    if(live(a)&&!channel.empty()&&n(a,"channel_ticks")==0) {
        bool completed=true;
        if(channel=="recall") {
            const int x=a.at("spawn").at(0).get<int>(),y=a.at("spawn").at(1).get<int>();
            if(walkable(s,x,y,who)){a["x"]=x;a["y"]=y;invalidate_capture(s);}else completed=false;
        }
        if(channel=="tribute") {
            const int team=n(a,"team"),gems=n(a,"gems");
            s["score"][static_cast<std::size_t>(team)]=s.at("score").at(team).get<int>()+gems;
            const float gain=static_cast<float>(gems)*f(a,"tribute_gold_per_gem")*f(a,"gold_multiplier",1);
            a["gold"]=f(a,"gold")+gain;a["earned_gold"]=f(a,"earned_gold")+gain;a["gems"]=0;
        }
        if(completed){a["channel"]="";a["channel_ticks"]=-1;}
    }
    reset_permissions(a);
}
void owner_end(J& s,int who) {
    auto& a=s["actors"][static_cast<std::size_t>(who)];
    for(auto& sp:a["spells"].as_array()) if(n(sp,"cooldown")>0) sp["cooldown"]=n(sp,"cooldown")-1;
    if(n(a,"stun")>0) a["stun"]=n(a,"stun")-1;
    if(n(a,"shield_ticks")>0) a["shield_ticks"]=n(a,"shield_ticks")-1;
    if(!a.value("channel","").empty()&&n(a,"channel_ticks")>0) a["channel_ticks"]=n(a,"channel_ticks")-1;
    if(live(a)&&on_spawn(a)) a["hp"]=std::min(f(a,"max_hp"),f(a,"hp")+f(a,"max_hp")*f(s.at("settings"),"spawn_healing"));
    a["completed"]=true;
}
void kill_actor(J& s,int victim,int killer) {
    auto& a=s["actors"][static_cast<std::size_t>(victim)];
    const int bag=n(a,"gems");
    a["hp"]=0.0f;a["shield"]=0.0f;a["channel"]="";a["channel_ticks"]=-1;a["gems"]=0;
    a["can_move"]=false;a["can_attack"]=false;a["can_cast"]=false;
    if(killer>=0) {
        auto& k=s["actors"][static_cast<std::size_t>(killer)];
        k["gems"]=n(k,"gems")+bag;
        const float reward=f(s.at("settings"),"kill_gold",100)*f(k,"gold_multiplier",1);
        k["gold"]=f(k,"gold")+reward;k["earned_gold"]=f(k,"earned_gold")+reward;
        s["kills"][static_cast<std::size_t>(n(k,"team"))]=s.at("kills").at(n(k,"team")).get<int>()+1;
        if(b(k,"reset_on_kill")) for(auto& sp:k["spells"].as_array()) sp["cooldown"]=0;
    }
    invalidate_capture(s);
}
void hurt_actor(J& s,int victim,float amount,int killer,bool monster) {
    auto& a=s["actors"][static_cast<std::size_t>(victim)];
    if(!live(a)||amount<=0) return;
    const float scaled=amount*(monster?f(a,"mitigation",1):f(a,"champion_mitigation",1));
    const float shield_take=std::min(f(a,"shield"),scaled);
    a["shield"]=f(a,"shield")-shield_take;
    const float hp_take=std::min(f(a,"hp"),scaled-shield_take);
    a["hp"]=f(a,"hp")-(scaled-shield_take);
    a["damage_taken"]=f(a,"damage_taken")+shield_take+hp_take;
    a["hp_damage_taken"]=f(a,"hp_damage_taken")+hp_take;
    if(a.value("channel","")=="recall") { a["channel"]="";a["channel_ticks"]=-1; }
    const float retaliation=f(a,"retaliation");
    if(f(a,"hp")<=0) kill_actor(s,victim,killer);
    if(killer>=0&&retaliation>0&&live(s.at("actors").at(killer))) hurt_actor(s,killer,retaliation,-1,false);
}
void stun_actor(J& a,int ticks) {
    if(!live(a)||b(a,"stun_immune")||ticks<=0) return;
    a["stun"]=std::max(n(a,"stun"),ticks);a["channel"]="";a["channel_ticks"]=-1;
    a["can_move"]=false;a["can_attack"]=false;a["can_cast"]=false;
}
void wake_group(J& s,int group) {
    for(auto& c:s["camps"].as_array()) if(n(c,"group")==group&&live(c)) { c["awake"]=true;c["quiet"]=0; }
}
void hurt_camp(J& s,int target,float amount,int killer) {
    auto& c=s["camps"][static_cast<std::size_t>(target)];
    if(!live(c)||amount<=0) return;
    wake_group(s,n(c,"group"));
    c["hp"]=std::max(0.0f,f(c,"hp")-amount);
    if(!live(c)&&killer>=0&&b(s.at("actors").at(killer),"reset_on_kill"))
        for(auto& sp:s["actors"][static_cast<std::size_t>(killer)]["spells"].as_array()) sp["cooldown"]=0;
    bool dead=true;
    for(const auto& x:s.at("camps").as_array()) if(n(x,"group")==n(c,"group")&&live(x)) dead=false;
    const int gi=group_index(s,n(c,"group"));
    if(dead&&gi>=0&&s.at("groups").at(gi).value("status","")=="alive") s["groups"][static_cast<std::size_t>(gi)]["status"]="reward";
}
void make_order(J& s,bool apply_pending=true) {
    std::vector<int> teams[2];
    for(std::size_t i=0;i<s.at("actors").size();++i) {
        auto& a=s["actors"][i];if(apply_pending)a["priority"]=n(a,"next_priority");a["completed"]=false;
        teams[n(a,"team")].push_back(static_cast<int>(i));
    }
    for(auto& t:teams) std::stable_sort(t.begin(),t.end(),[&](int a,int c){return n(s.at("actors").at(a),"priority")>n(s.at("actors").at(c),"priority");});
    J order=J::array();const int first=(n(s,"round")+1)%2; // A starts odd gameplay rounds.
    for(std::size_t i=0;i<std::max(teams[0].size(),teams[1].size());++i) for(int k=0;k<2;++k) {
        const int team=(first+k)%2;if(i<teams[team].size()) order.push_back(teams[team][i]);
    }
    s["order"]=order;s["turn"]=0;
}
void camp_phase(J& s) {
    for(auto& g:s["groups"].as_array()) {
        const int id=n(g,"id");
        if(g.value("status","")=="paid") {
            if(n(g,"respawn_left")>0) g["respawn_left"]=n(g,"respawn_left")-1;
            bool occupied=false;
            for(const auto& a:s.at("actors").as_array()) if(live(a)&&on_group(s,a,id)) occupied=true;
            if(n(g,"respawn_left")==0&&!occupied) {
                for(auto& c:s["camps"].as_array()) if(n(c,"group")==id) {c["hp"]=f(c,"max_hp");c["awake"]=false;c["quiet"]=0;c["stun"]=0;}
                g["status"]="alive";g["capture_left"]=-1;g["capture_team"]=-1;g["generation"]=n(g,"generation")+1;
            }
        }
        if(g.value("status","")!="reward") continue;
        std::vector<int> present[2];
        for(std::size_t i=0;i<s.at("actors").size();++i) {
            const auto& a=s.at("actors").at(i);if(live(a)&&on_group(s,a,id)) present[n(a,"team")].push_back(static_cast<int>(i));
        }
        const int team=present[0].empty()?(present[1].empty()?-1:1):(present[1].empty()?0:-1);
        if(team<0) { g["capture_left"]=-1;g["capture_team"]=-1;continue; }
        if(n(g,"capture_team")!=team||n(g,"capture_left")<0) {g["capture_team"]=team;g["capture_left"]=n(s.at("settings"),"capture_time");}
        else g["capture_left"]=std::max(0,n(g,"capture_left")-1);
        if(n(g,"capture_left")!=0) continue;
        auto recipients=present[team];
        std::stable_sort(recipients.begin(),recipients.end(),[&](int lhs,int rhs) {
            const auto& left=s.at("actors").at(lhs).at("id");const auto& right=s.at("actors").at(rhs).at("id");
            if(left.is_number_integer()!=right.is_number_integer())return left.is_number_integer();
            if(left.is_number_integer())return left.get<std::int64_t>()<right.get<std::int64_t>();
            return left.get<std::string>()<right.get<std::string>();
        });
        const int base=n(g,"gems")/static_cast<int>(recipients.size());int remainder=n(g,"gems")%static_cast<int>(recipients.size());
        for(const int index:recipients) {
            auto& a=s["actors"][static_cast<std::size_t>(index)];
            a["gems"]=n(a,"gems")+base+(remainder>0?1:0);if(remainder>0)--remainder;
            const float gain=(f(g,"gold")/static_cast<float>(recipients.size()))*f(a,"gold_multiplier",1);
            a["gold"]=f(a,"gold")+gain;a["earned_gold"]=f(a,"earned_gold")+gain;
        }
        g["status"]="paid";g["paid_team"]=team;g["capture_team"]=-1;g["respawn_left"]=n(g,"respawn");g["payouts"][static_cast<std::size_t>(team)]=g.at("payouts").at(team).get<int>()+1;
    }
    std::vector<int> rank(s.at("actors").size());
    for(std::size_t i=0;i<s.at("order").size();++i) rank[s.at("order").at(i).get<int>()]=static_cast<int>(i);
    for(std::size_t ci=0;ci<s.at("camps").size();++ci) {
        auto& c=s["camps"][ci];if(!live(c)) continue;
        if(n(c,"stun")>0) {c["stun"]=n(c,"stun")-1;continue;}
        if(!b(c,"awake")) continue;
        int best=-1,distance=std::numeric_limits<int>::max();
        for(std::size_t ai=0;ai<s.at("actors").size();++ai) {
            const auto& a=s.at("actors").at(ai);if(!live(a))continue;
            const int d=chebyshev(a,c);if(d>n(c,"range"))continue;
            if(best<0||d<distance||(d==distance&&rank[ai]<rank[best])) {best=static_cast<int>(ai);distance=d;}
        }
        if(best<0) {c["quiet"]=n(c,"quiet")+1;if(n(c,"quiet")>=n(s.at("settings"),"camp_sleep_time")) c["awake"]=false;}
        else {c["quiet"]=0;hurt_actor(s,best,f(c,"strength"),-1,true);}
    }
}
void advance_owner(J& s) {
    owner_end(s,active_actor(s));s["turn"]=n(s,"turn")+1;
    if(n(s,"turn")>=static_cast<int>(s.at("order").size())) {
        s["round"]=n(s,"round")+1;make_order(s);
        if(n(s,"round")>n(s.at("settings"),"max_rounds"))return;
        camp_phase(s);
    }
    owner_start(s,active_actor(s));
}
J default_settings() {return J{{"capture_time",5},{"camp_sleep_time",2},{"recall_time",5},{"spawn_healing",1.0},{"submit_multiplier",0.1},{"submit_minimum",1},{"winning_gems",1000},{"max_rounds",2000},{"kill_gold",100}};}
}

int chebyshev(const Json& a,const Json& c) {return dist(n(a,"x"),n(a,"y"),c);}
int active_actor(const State& s) {return s.at("order").at(static_cast<std::size_t>(n(s,"turn"))).get<int>();}

State initial(const Json& scenario) {
    require(scenario.is_object(),"scenario must be object");J s=J::object();
    s["model"]=version;s["width"]=n(scenario,"width",7);s["height"]=n(scenario,"height",7);
    require(n(s,"width")>0&&n(s,"height")>0&&n(s,"width")<=100&&n(s,"height")<=100,"grid outside1..100");
    s["blocked"]=scenario.contains("blocked")?scenario.at("blocked"):J::array();s["altars"]=scenario.contains("altars")?scenario.at("altars"):J::array();
    for(const auto* field:{"blocked","altars"}) for(const auto& p:s.at(field).as_array()) {
        require(p.is_array()&&p.size()==2,"bad tile coordinate");require(inside(s,p.at(0).get<int>(),p.at(1).get<int>()),"tile outside map");
    }
    J settings=default_settings();if(scenario.contains("settings")) for(const auto& kv:scenario.at("settings").as_object()) settings[kv.first]=kv.second;
    for(const auto* k:{"capture_time","camp_sleep_time","recall_time","submit_minimum","winning_gems","max_rounds"}) require(n(settings,k)>=(std::string(k)=="capture_time"?0:1),std::string("bad setting ")+k);
    require(n(settings,"max_rounds")<=100000,"horizon too large for restricted finite domain");
    finite(f(settings,"spawn_healing"),"spawn_healing");finite(f(settings,"submit_multiplier"),"submit_multiplier");s["settings"]=settings;
    s["actors"]=J::array();require(scenario.contains("actors")&&scenario.at("actors").size()>0&&scenario.at("actors").size()<=10,"require1..10 actors");
    std::set<std::pair<int,int>> occupied;
    for(const auto& input:scenario.at("actors").as_array()) {
        J a=input;const int index=static_cast<int>(s.at("actors").size());
        require(!a.contains("passive")||a.value("passive","none")=="none"||a.value("passive","")=="already_applied","unmodeled actor passive; supply explicit already-applied stats and supported scalar effects");
        for(const auto* gear:{"items","equipment"})if(a.contains(gear))require(a.at(gear).is_array()&&a.at(gear).empty(),"named equipment is outside this domain; supply final scalar stats and supported effects");
        a["id"]=a.contains("id")?a.at("id"):J(std::string("actor")+std::to_string(index));require(a.at("id").is_string()||a.at("id").is_number_integer(),"actor id must string or integer");a["team"]=n(a,"team");require(n(a,"team")==0||n(a,"team")==1,"team must0or1");
        a["x"]=n(a,"x");a["y"]=n(a,"y");require(inside(s,n(a,"x"),n(a,"y"))&&!cell(s.at("blocked"),n(a,"x"),n(a,"y"))&&!cell(s.at("altars"),n(a,"x"),n(a,"y")),"actor on invalid tile");
        a["max_hp"]=f(a,"max_hp",f(a,"hp",100));a["hp"]=f(a,"hp",f(a,"max_hp"));finite(f(a,"max_hp"),"max_hp",true);finite(f(a,"hp"),"hp");require(f(a,"hp")<=f(a,"max_hp"),"hp exceeds maximum");
        if(live(a)) require(occupied.emplace(n(a,"x"),n(a,"y")).second,"overlapping live units");
        a["spawn"]=a.contains("spawn")?a.at("spawn"):J::array({n(a,"x"),n(a,"y")});require(a.at("spawn").size()==2,"bad spawn");
        require(inside(s,a.at("spawn").at(0).get<int>(),a.at("spawn").at(1).get<int>()),"spawn outside map");
        for(const auto* k:{"strength","focus","shield","damage_taken","hp_damage_taken","retaliation","tribute_gold_per_gem","enhanced_damage"}) {a[k]=f(a,k);finite(f(a,k),k);}
        require(f(a,"retaliation")==0,"retaliation chains and camp retaliation are outside this domain");
        for(const auto* k:{"mitigation","champion_mitigation","gold_multiplier","camp_damage_multiplier","champion_damage_multiplier"}) {a[k]=f(a,k,1);finite(f(a,k),k);}
        a["range"]=n(a,"range",1);require(n(a,"range")>=0&&n(a,"range")<=100,"bad attack range");a["ranged"]=b(a,"ranged",n(a,"range")>1);
        for(const auto* k:{"shield_ticks","stun","priority","gems","enhanced_stun"}) {a[k]=n(a,k);if(std::string(k)!="priority") require(n(a,k)>=0,std::string("negative actor ")+k);}
        a["next_priority"]=n(a,"next_priority",n(a,"priority"));a["gold"]=f(a,"gold");a["earned_gold"]=f(a,"earned_gold",f(a,"gold"));finite(f(a,"gold"),"gold");finite(f(a,"earned_gold"),"earned_gold");
        a["stun_immune"]=b(a,"stun_immune");a["reset_on_kill"]=b(a,"reset_on_kill");a["basic_radius"]=n(a,"basic_radius");require(n(a,"basic_radius")>=0&&n(a,"basic_radius")<=2,"unsupported basic radius");
        a["channel"]=a.value("channel","");require(a.value("channel","").empty()||a.value("channel","")=="recall"||a.value("channel","")=="tribute","unsupported channel");a["channel_ticks"]=n(a,"channel_ticks",-1);require(a.value("channel","").empty()||n(a,"channel_ticks")>=0,"negative active channel timer");
        J spells=J::array();if(a.contains("spells")) for(const auto& sp:a.at("spells").as_array()) spells.push_back(normalized_spell(sp));require(spells.size()<=2,"at most2spells");a["spells"]=spells;a["completed"]=false;reset_permissions(a);s["actors"].push_back(a);
    }
    s["camps"]=J::array();s["groups"]=J::array();
    if(scenario.contains("camps")) for(const auto& in:scenario.at("camps").as_array()) {
        J c=in;const int index=static_cast<int>(s.at("camps").size());c["id"]=c.contains("id")?c.at("id"):J(std::string("camp")+std::to_string(index));require(c.at("id").is_string()||c.at("id").is_number_integer(),"camp id must string or integer");c["group"]=n(c,"group");c["x"]=n(c,"x");c["y"]=n(c,"y");
        require(inside(s,n(c,"x"),n(c,"y"))&&!cell(s.at("blocked"),n(c,"x"),n(c,"y"))&&!cell(s.at("altars"),n(c,"x"),n(c,"y")),"camp on invalid tile");
        c["max_hp"]=f(c,"max_hp",f(c,"hp",100));c["hp"]=f(c,"hp",f(c,"max_hp"));finite(f(c,"max_hp"),"camp maxhp",true);finite(f(c,"hp"),"camp hp");require(f(c,"hp")<=f(c,"max_hp"),"camp hp exceeds max");
        if(live(c)) require(occupied.emplace(n(c,"x"),n(c,"y")).second,"overlapping live units");
        c["strength"]=f(c,"strength");finite(f(c,"strength"),"camp strength");c["range"]=n(c,"range",3);require(n(c,"range")>=0,"negative camp range");c["gems"]=n(c,"gems");c["gold"]=f(c,"gold");finite(f(c,"gold"),"camp gold");require(n(c,"gems")>=0,"negative gems");c["respawn"]=n(c,"respawn",60);require(n(c,"respawn")>0,"bad respawn");
        c["awake"]=b(c,"awake");c["quiet"]=n(c,"quiet");c["stun"]=n(c,"stun");require(n(c,"quiet")>=0&&n(c,"stun")>=0,"negative camp timer");s["camps"].push_back(c);
        int gi=group_index(s,n(c,"group"));if(gi<0) {s["groups"].push_back(J{{"id",n(c,"group")},{"status","reward"},{"capture_team",-1},{"capture_left",-1},{"respawn_left",-1},{"paid_team",-1},{"gems",0},{"gold",0.0},{"respawn",1},{"generation",0},{"payouts",J::array({0,0})}});gi=static_cast<int>(s.at("groups").size())-1;}
        auto& g=s["groups"][static_cast<std::size_t>(gi)];g["gems"]=n(g,"gems")+n(c,"gems");g["gold"]=f(g,"gold")+f(c,"gold");g["respawn"]=std::max(n(g,"respawn"),n(c,"respawn"));if(live(c))g["status"]="alive";
    }
    std::set<std::string> actor_ids,camp_ids;std::set<std::pair<int,int>> camp_tiles;
    for(const auto& a:s.at("actors").as_array())require(actor_ids.insert(a.at("id").dump()).second,"duplicate actor id");
    for(const auto& c:s.at("camps").as_array()) {require(camp_ids.insert(c.at("id").dump()).second,"duplicate camp id");require(camp_tiles.emplace(n(c,"x"),n(c,"y")).second,"overlapping camp spawn tiles");}
    for(std::size_t i=0;i<s.at("camps").size();++i)for(std::size_t j=i+1;j<s.at("camps").size();++j) {
        const auto& a=s.at("camps").at(i);const auto& c=s.at("camps").at(j);
        if(std::abs(n(a,"x")-n(c,"x"))+std::abs(n(a,"y")-n(c,"y"))==1)require(n(a,"group")==n(c,"group"),"cardinally adjacent camps must share a group");
    }
    for(const auto& group:s.at("groups").as_array()) {
        std::vector<int> members;for(std::size_t i=0;i<s.at("camps").size();++i)if(n(s.at("camps").at(i),"group")==n(group,"id"))members.push_back(static_cast<int>(i));
        std::set<int> seen{members.front()};bool added=true;
        while(added){added=false;for(int candidate:members)if(!seen.count(candidate))for(int reached:seen){const auto& a=s.at("camps").at(candidate);const auto& c=s.at("camps").at(reached);if(std::abs(n(a,"x")-n(c,"x"))+std::abs(n(a,"y")-n(c,"y"))==1){seen.insert(candidate);added=true;break;}}}
        require(seen.size()==members.size(),"a camp group must be cardinally connected");
    }
    if(scenario.contains("group_states"))for(const auto& input:scenario.at("group_states").as_array()) {
        const int gi=group_index(s,n(input,"id"));require(gi>=0,"unknown group state id");auto& group=s["groups"][static_cast<std::size_t>(gi)];
        for(const auto* key:{"capture_team","capture_left","respawn_left","paid_team","generation"})if(input.contains(key))group[key]=n(input,key);
        if(input.contains("status")){const auto value=input.at("status").get<std::string>();require(value=="alive"||value=="reward"||value=="paid","bad group status");group["status"]=value;}
        if(input.contains("payouts")){require(input.at("payouts").size()==2,"payouts must contain2teams");group["payouts"]=input.at("payouts");}
    }
    for(const auto& group:s.at("groups").as_array()) {
        bool living=false;for(const auto& c:s.at("camps").as_array())if(n(c,"group")==n(group,"id")&&live(c))living=true;
        require((group.value("status","")=="alive")==living,"group status contradicts living members");
        require(n(group,"capture_team")>=-1&&n(group,"capture_team")<=1&&n(group,"paid_team")>=-1&&n(group,"paid_team")<=1,"invalid group team");
        require(n(group,"capture_left")>=-1&&n(group,"capture_left")<=n(settings,"capture_time")&&n(group,"respawn_left")>=-1&&n(group,"generation")>=0,"invalid group timer/generation");
        if(group.value("status","")=="paid")require(n(group,"respawn_left")>=0,"paid group requires respawn timer");
        for(const auto& count:group.at("payouts").as_array())require(count.is_number_integer()&&count.get<int>()>=0,"invalid payout count");
    }
    s["goal"]=scenario.contains("goal")?scenario.at("goal"):J{{"kind","capture_depart"},{"team",0},{"group",s.at("groups").empty()?0:n(s.at("groups").at(0),"id")}};
    const auto kind=s.at("goal").value("kind","capture_depart");require(kind=="capture_depart"||kind=="bank"||kind=="clear"||kind=="survive","unsupported goal");require(n(s.at("goal"),"team")==0||n(s.at("goal"),"team")==1,"bad goal team");
    if(kind=="capture_depart"||kind=="clear") require(group_index(s,n(s.at("goal"),"group"))>=0,"goal group missing");
    s["round"]=n(scenario,"round",1);s["start_round"]=n(scenario,"round",1);require(n(s,"round")>=1&&n(s,"round")<=n(settings,"max_rounds"),"initial round outside gameplay horizon");s["owner_actions"]=0;s["score"]=scenario.contains("score")?scenario.at("score"):J::array({0,0});s["kills"]=J::array({0,0});require(s.at("score").size()==2,"score must2entries");
    s["provenance"]=scenario.contains("provenance")?scenario.at("provenance"):J{{"state_origin","constructed"},{"native_qualification","unvalidated"}};
    make_order(s,false);
    if(scenario.contains("active_actor")) {const int desired=n(scenario,"active_actor");bool found=false;for(std::size_t i=0;i<s.at("order").size();++i)if(s.at("order").at(i).get<int>()==desired){s["turn"]=static_cast<int>(i);found=true;}require(found,"active actor out of range");}
    owner_start(s,active_actor(s));
    for(std::size_t i=0;i<s.at("actors").size();++i) {
        const auto& source=scenario.at("actors").at(i);auto& actor=s["actors"][i];
        if(source.contains("completed"))actor["completed"]=b(source,"completed");
        if(static_cast<int>(i)==active_actor(s)&&live(actor)&&n(actor,"stun")==0&&actor.value("channel","").empty())
            for(const auto* permission:{"can_move","can_attack","can_cast"})if(source.contains(permission))actor[permission]=b(source,permission);
    }
    return s;
}

State decode_state(const Json& checkpoint) { require(checkpoint.value("model","")==version,"checkpoint model version mismatch");require(checkpoint.contains("order")&&!checkpoint.at("order").empty(),"bad checkpoint order");(void)active_actor(checkpoint);return checkpoint; }
Json encode_state(const State& state) {return state;}
std::string state_key(const State& state) {
    // Diagnostic action counts do not affect future physics. In particular a free
    // priority0->100->0 sequence must return to the same transposition key.
    J key=state;key.as_object().erase("owner_actions");
    for(auto& a:key["actors"].as_array()) {a.as_object().erase("damage_taken");a.as_object().erase("hp_damage_taken");}
    return key.dump();
}

std::string status(const State& s) {
    const auto& goal=s.at("goal");const int team=n(goal,"team");const auto kind=goal.value("kind","capture_depart");
    if(kind=="bank"&&s.at("score").at(team).get<int>()>=n(goal,"score",n(s.at("settings"),"winning_gems"))) return "success";
    bool alive=false;for(const auto& a:s.at("actors").as_array())if(n(a,"team")==team&&live(a))alive=true;
    if(!alive)return"failure";
    if(kind=="survive"&&n(s,"round")-n(s,"start_round")>=n(goal,"rounds",10)) return "success";
    const int gi=group_index(s,n(goal,"group"));
    if((kind=="clear"||kind=="capture_depart")&&gi>=0) {
        if(kind=="clear") {bool dead=true;for(const auto& c:s.at("camps").as_array())if(n(c,"group")==n(goal,"group")&&live(c))dead=false;if(dead)return"success";}
        else if(s.at("groups").at(gi).at("payouts").at(team).get<int>()>0) {
            bool left=true;for(const auto& a:s.at("actors").as_array()) if(n(a,"team")==team&&live(a)&&on_group(s,a,n(goal,"group")))left=false;if(left)return"success";
        }
    }
    if(n(s,"round")>n(s.at("settings"),"max_rounds"))return"failure";
    return"active";
}
bool terminal(const State& state) {return status(state)!="active";}

std::vector<Action> legal_actions(const State& s) {
    std::vector<Action> out;if(terminal(s))return out;out.push_back(J{{"type","end"}});
    const int who=active_actor(s);const auto& a=s.at("actors").at(who);
    if(!live(a)||n(a,"stun")>0||!a.value("channel","").empty())return out;
    if(b(a,"can_move"))for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&walkable(s,n(a,"x")+dx,n(a,"y")+dy,who))out.push_back(J{{"type","move"},{"x",n(a,"x")+dx},{"y",n(a,"y")+dy}});
    if(b(a,"can_attack")) {
        for(std::size_t i=0;i<s.at("actors").size();++i) {const auto& t=s.at("actors").at(i);if(live(t)&&n(t,"team")!=n(a,"team")&&chebyshev(a,t)<=n(a,"range"))out.push_back(J{{"type","basic"},{"target_kind","actor"},{"target",static_cast<int>(i)}});}
        for(std::size_t i=0;i<s.at("camps").size();++i) {const auto& t=s.at("camps").at(i);if(live(t)&&chebyshev(a,t)<=n(a,"range"))out.push_back(J{{"type","basic"},{"target_kind","camp"},{"target",static_cast<int>(i)}});}
    }
    if(b(a,"can_cast"))for(std::size_t slot=0;slot<a.at("spells").size();++slot) {
        const auto& sp=a.at("spells").at(slot);const auto kind=sp.value("kind","none");if(kind=="none"||n(sp,"cooldown")>0||(b(sp,"exclusive")&&!b(a,"can_move")))continue;
        // Native code26 rejects resetting an attack which is still available.
        if(kind=="reset") {if(!b(a,"can_attack"))out.push_back(J{{"type","cast"},{"slot",static_cast<int>(slot)}});}
        else if(kind=="shield")out.push_back(J{{"type","cast"},{"slot",static_cast<int>(slot)}});
        else if(f(sp,"radius")>0) {
            for(int y=0;y<n(s,"height");++y)for(int x=0;x<n(s,"width");++x)if(dist(x,y,a)<=f(sp,"range")&&!cell(s.at("blocked"),x,y)) {
                bool hits=false;for(const auto& c:s.at("camps").as_array())if(live(c)&&dist(x,y,c)<=f(sp,"radius"))hits=true;
                for(const auto& t:s.at("actors").as_array())if(live(t)&&n(t,"team")!=n(a,"team")&&dist(x,y,t)<=f(sp,"radius"))hits=true;
                if(hits)out.push_back(J{{"type","cast"},{"slot",static_cast<int>(slot)},{"x",x},{"y",y}});
            }
        } else {
            for(const auto* field:{"actors","camps"})for(std::size_t i=0;i<s.at(field).size();++i) {const auto& t=s.at(field).at(i);if(live(t)&&(std::string(field)=="camps"||n(t,"team")!=n(a,"team"))&&chebyshev(a,t)<=f(sp,"range"))out.push_back(J{{"type","cast"},{"slot",static_cast<int>(slot)},{"target_kind",std::string(field)=="camps"?"camp":"actor"},{"target",static_cast<int>(i)}});}
        }
    }
    if(b(a,"can_move")&&b(a,"can_attack")&&b(a,"can_cast")) {
        out.push_back(J{{"type","recall"}});
        if(n(a,"gems")>0)for(const auto& altar:s.at("altars").as_array())if(std::max(std::abs(n(a,"x")-altar.at(0).get<int>()),std::abs(n(a,"y")-altar.at(1).get<int>()))<=1) {out.push_back(J{{"type","tribute"}});break;}
    }
    for(int p:{0,100})if(n(a,"next_priority")!=p)out.push_back(J{{"type","priority"},{"value",p}});
    return out;
}

State step(const State& input,const Action& action) {
    const auto legal=legal_actions(input);require(std::find(legal.begin(),legal.end(),action)!=legal.end(),"illegal action "+action.dump());
    J s=input;const int who=active_actor(s);auto& a=s["actors"][static_cast<std::size_t>(who)];const auto type=action.value("type","");s["owner_actions"]=n(s,"owner_actions")+1;
    if(type=="end") {advance_owner(s);return s;}
    if(type=="priority") {a["next_priority"]=n(action,"value");return s;}
    if(type=="move") {a["x"]=n(action,"x");a["y"]=n(action,"y");a["can_move"]=false;if(b(a,"ranged"))a["can_attack"]=false;invalidate_capture(s);return s;}
    if(type=="recall"||type=="tribute") {
        a["channel"]=type;a["channel_ticks"]=type=="recall"?n(s.at("settings"),"recall_time"):std::max(n(s.at("settings"),"submit_minimum"),static_cast<int>(std::ceil(n(a,"gems")*f(s.at("settings"),"submit_multiplier"))));reset_permissions(a);return s;
    }
    if(type=="basic") {
        const int target=n(action,"target");const bool camp=action.value("target_kind","")=="camp";const J victim=s.at(camp?"camps":"actors").at(target);const float damage=f(a,"strength")+f(a,"enhanced_damage");const int stun=n(a,"enhanced_stun");
        a["can_attack"]=false;if(b(a,"ranged"))a["can_move"]=false;a["enhanced_damage"]=0.0f;a["enhanced_stun"]=0;
        if(camp) {hurt_camp(s,target,damage*f(a,"camp_damage_multiplier",1),who);if(live(s.at("camps").at(target)))s["camps"][static_cast<std::size_t>(target)]["stun"]=std::max(n(s.at("camps").at(target),"stun"),stun);}
        else {hurt_actor(s,target,damage*f(a,"champion_damage_multiplier",1),who,false);stun_actor(s["actors"][static_cast<std::size_t>(target)],stun);}
        if(n(a,"basic_radius")>0) {
            for(std::size_t i=0;i<s.at("camps").size();++i)if((!camp||static_cast<int>(i)!=target)&&chebyshev(victim,s.at("camps").at(i))<=n(a,"basic_radius"))hurt_camp(s,static_cast<int>(i),f(a,"strength")*f(a,"camp_damage_multiplier",1),who);
            for(std::size_t i=0;i<s.at("actors").size();++i)if(n(s.at("actors").at(i),"team")!=n(a,"team")&&(!camp&&static_cast<int>(i)==target?false:true)&&chebyshev(victim,s.at("actors").at(i))<=n(a,"basic_radius"))hurt_actor(s,static_cast<int>(i),f(a,"strength")*f(a,"champion_damage_multiplier",1),who,false);
        }
        return s;
    }
    const int slot=n(action,"slot");const J sp=a.at("spells").at(slot);const auto kind=sp.value("kind","none");a["can_cast"]=false;if(b(sp,"exclusive"))a["can_move"]=false;a["spells"][static_cast<std::size_t>(slot)]["cooldown"]=n(sp,"max_cooldown");
    if(kind=="reset") {a["can_attack"]=true;a["enhanced_damage"]=f(sp,"enhanced_damage");a["enhanced_stun"]=n(sp,"enhanced_stun");return s;}
    if(kind=="shield") {a["shield"]=f(sp,"shield");a["shield_ticks"]=n(sp,"duration");return s;}
    std::vector<int> actors,camps;
    if(action.contains("target")) {(action.value("target_kind","")=="camp"?camps:actors).push_back(n(action,"target"));}
    else {
        for(std::size_t i=0;i<s.at("actors").size();++i)if(n(s.at("actors").at(i),"team")!=n(a,"team")&&dist(n(action,"x"),n(action,"y"),s.at("actors").at(i))<=f(sp,"radius"))actors.push_back(static_cast<int>(i));
        for(std::size_t i=0;i<s.at("camps").size();++i)if(dist(n(action,"x"),n(action,"y"),s.at("camps").at(i))<=f(sp,"radius"))camps.push_back(static_cast<int>(i));
    }
    for(int target:actors) {hurt_actor(s,target,f(sp,"damage")*f(a,"champion_damage_multiplier",1),who,false);if(kind=="stun")stun_actor(s["actors"][static_cast<std::size_t>(target)],n(sp,"duration"));}
    for(int target:camps) {hurt_camp(s,target,f(sp,"damage")*f(a,"camp_damage_multiplier",1),who);if(kind=="stun"&&live(s.at("camps").at(target)))s["camps"][static_cast<std::size_t>(target)]["stun"]=std::max(n(s.at("camps").at(target),"stun"),n(sp,"duration"));}
    return s;
}

Json summary(const State& s) {
    J out{{"elapsed_rounds",n(s,"round")-n(s,"start_round")},{"round",n(s,"round")},{"active_actor",active_actor(s)},{"owner_actions",n(s,"owner_actions")},{"status",status(s)}};
    for(const auto* k:{"hp","shield","shield_ticks","alive","stun","cooldowns","positions","damage_taken","hp_damage_taken","permissions"})out[k]=J::array();
    J gems=J::array(),gold=J::array();
    for(const auto& a:s.at("actors").as_array()) {
        for(const auto* k:{"hp","shield","damage_taken","hp_damage_taken"})out[k].push_back(f(a,k));
        out["shield_ticks"].push_back(n(a,"shield_ticks"));out["stun"].push_back(n(a,"stun"));out["alive"].push_back(live(a));out["positions"].push_back(J::array({n(a,"x"),n(a,"y")}));out["permissions"].push_back(J::array({b(a,"can_move"),b(a,"can_attack"),b(a,"can_cast")}));
        J cd=J::array();for(const auto& sp:a.at("spells").as_array())cd.push_back(n(sp,"cooldown"));out["cooldowns"].push_back(cd);gems.push_back(n(a,"gems"));gold.push_back(f(a,"gold"));
    }
    out["resources"]=J{{"score",s.at("score")},{"gems",gems},{"gold",gold}};out["groups"]=s.at("groups");out["shields"]=out.at("shield");
    J continuation=J{{"active_actor",active_actor(s)},{"order",s.at("order")},{"turn",s.at("turn")},{"round_parity",n(s,"round")%2},{"groups",s.at("groups")},{"camps",s.at("camps")},{"actors",J::array()}};
    for(const auto& a:s.at("actors").as_array())continuation["actors"].push_back(J{{"team",n(a,"team")},{"channel",a.value("channel","")},{"channel_ticks",n(a,"channel_ticks")},{"stun",n(a,"stun")},{"priority",n(a,"priority")},{"next_priority",n(a,"next_priority")},{"can_move",b(a,"can_move")},{"can_attack",b(a,"can_attack")},{"can_cast",b(a,"can_cast")},{"completed",b(a,"completed")},{"enhanced_damage",f(a,"enhanced_damage")},{"enhanced_stun",n(a,"enhanced_stun")}});
    out["continuation_signature"]=continuation;return out;
}
double utility(const State& s,int team) {
    double hp=0,bag=0,camp_hp=0;for(const auto& a:s.at("actors").as_array())if(n(a,"team")==team){hp+=f(a,"hp");bag+=n(a,"gems");}
    for(const auto& c:s.at("camps").as_array())if(n(c,"group")==n(s.at("goal"),"group"))camp_hp+=f(c,"hp");
    const double elapsed=n(s,"round")-n(s,"start_round");const auto st=status(s);if(st=="success")return 1e6-100*elapsed+hp;if(st=="failure")return-1e6+hp;
    return 100*s.at("score").at(team).get<int>()+10*bag+hp-camp_hp-10*elapsed;
}
Action baseline_action(const State& s) {
    auto actions=legal_actions(s);if(actions.empty())return J{{"type","end"}};const auto& a=s.at("actors").at(active_actor(s));
    int gx=n(a,"x"),gy=n(a,"y");const auto kind=s.at("goal").value("kind","capture_depart");
    if(kind=="bank"&&n(a,"gems")>0&&!s.at("altars").empty()){const auto& p=s.at("altars").at(0);gx=p.at(0).get<int>();gy=p.at(1).get<int>();}
    else for(const auto& c:s.at("camps").as_array())if(n(c,"group")==n(s.at("goal"),"group")){gx=n(c,"x");gy=n(c,"y");if(live(c))break;}
    const int gi=group_index(s,n(s.at("goal"),"group"));const bool paid=gi>=0&&s.at("groups").at(gi).at("payouts").at(n(a,"team")).get<int>()>0;
    double best=-1e100;J chosen=actions.front();
    for(const auto& action:actions){double score=-1000;const auto type=action.value("type","");if(type=="end")score=0;
        else if(type=="tribute")score=1e6;else if(type=="basic")score=1e4+f(a,"strength");
        else if(type=="cast"){const auto& sp=a.at("spells").at(n(action,"slot"));const auto sk=sp.value("kind","");if(sk=="damage"||sk=="stun")score=1e4+f(sp,"damage")+(sk=="stun"?10:0);if(sk=="reset"&&!b(a,"can_attack"))score=9000;}
        else if(type=="move"){const int before=std::max(std::abs(n(a,"x")-gx),std::abs(n(a,"y")-gy)),after=std::max(std::abs(n(action,"x")-gx),std::abs(n(action,"y")-gy));score=paid?after-before:before-after;}
        if(score>best){best=score;chosen=action;}}
    return chosen;
}
Json capabilities() {return J{{"model",version},{"qualification","model_only_unvalidated"},{"information","fully_observed_local_state_not_a_deployable_policy"},{"supported",J::array({"float32_damage","sequential_actor_actions","ranged_step_basic_sharing","single_cast","reset","exclusive_cast","stun_owner_ticks","shield_replace","nearest_camp_target","next_round_priority","whole_group_wake","capture_split","occupied_respawn_delay","recall","tribute"})},{"not_supported",J::array({"personal_vision_or_bushes","dash_or_teleport","items_or_shopping","champion_respawn","linked_death","retaliation","gem_focus","heart_missing_health_scaling","automatic_full_game_rules","native_action_masks_or_fuel"})},{"action_subset","priorities0and100; no channel cancellation; channel start requires unspent move/basic/cast"},{"recall_domain","one declared spawn tile; waits if occupied"},{"initial_boundary","owner_input_no_extra_initial_camp_volley; A starts odd rounds"},{"exactness","exhaustion_can_prove_only_this_explicit_finite_model"}};}

Json expand_case(const Json& specification,const Json& runtime_catalog) {
    J out=specification;const auto& catalog=runtime_catalog.contains("result")?runtime_catalog.at("result"):runtime_catalog;
    require(catalog.contains("settings")&&catalog.contains("champions")&&catalog.contains("camps"),"catalog schema missing settings/designs");
    J settings=catalog.at("settings");settings["submit_multiplier"]=f(settings,"submit_gem_duration_multiplier",0.1f);settings["submit_minimum"]=n(settings,"submit_gem_minimum_duration",1);
    if(specification.contains("settings"))for(const auto& kv:specification.at("settings").as_object())settings[kv.first]=kv.second;
    out["settings"]=settings;
    J actors=J::array();for(const auto& input:specification.at("actors").as_array()) {
        if(!input.contains("design")){actors.push_back(input);continue;}const auto name=input.at("design").get<std::string>();const J* design=nullptr;for(const auto& d:catalog.at("champions").as_array())if(d.value("name","")==name)design=&d;require(design,"unknown champion design "+name);
        const auto& stats=design->at("stats");J a=input;a["max_hp"]=f(input,"max_hp",f(stats,"base_health"));a["hp"]=f(input,"hp",f(a,"max_hp"));a["strength"]=f(input,"strength",f(stats,"base_strength"));a["focus"]=f(input,"focus",f(stats,"base_focus"));a["range"]=n(stats,"base_attack_range");a["ranged"]=n(a,"range")>1;a["passive"]="already_applied";a["camp_damage_multiplier"]=f(input,"camp_damage_multiplier",f(stats,"camp_damage_multiplier",1));
        const int passive=n(*design,"passive");require(passive!=10&&passive!=3&&passive!=7&&name!="cassi"&&name!="issac","unsupported linked, retaliation, gem-focus or missing-health passive");
        float magnitude=0;
        if(catalog.contains("passives"))for(const auto& p:catalog.at("passives").as_array())if(p.value("name","")==name)magnitude=f(p,"magnitude");
        // Supported scalar passive effects are explicit; base Ryth has no bonus-health gain.
        if(name=="ryth"&&!input.contains("strength"))a["strength"]=f(stats,"base_strength")+std::max(0.0f,f(a,"max_hp")-f(stats,"base_health"))*magnitude;
        if(name=="marcella"){a["stun_immune"]=true;a["tribute_gold_per_gem"]=magnitude;}
        if(name=="vendetta")a["reset_on_kill"]=true;
        if(name=="varg")a["basic_radius"]=static_cast<int>(magnitude);
        if(name=="vesper_vale")a["gold_multiplier"]=1.0f+magnitude;
        J spells=J::array();for(const auto& sp:design->at("abilities").as_array()) {
            const int kind=n(sp,"kind");const auto& payload=sp.at("payload");J q{{"max_cooldown",n(sp,"cooldown")},{"cooldown",0},{"exclusive",(n(sp,"attributes")&1)!=0},{"range",f(payload,"range")},{"radius",f(payload,"radius")},{"duration",n(payload,"duration")}};
            if(kind==0)q["kind"]="none";else if(kind==1){q["kind"]="reset";if(n(payload,"enhanced_attack")!=0)q["enhanced_stun"]=1;}
            else if(kind==2||kind==3||kind==4){q["kind"]=(kind==4||(kind==3&&n(payload,"duration")>0))?"stun":"damage";q["damage"]=f(payload,"modifier")*f(a,"focus");}
            else if(kind==7){q["kind"]="shield";q["shield"]=f(payload,"modifier")*f(a,"focus");}
            else throw std::runtime_error("restricted sim: unsupported kit ability; provide an explicit restricted kit instead");
            spells.push_back(q);
        }if(input.contains("spells"))a["spells"]=input.at("spells");else a["spells"]=spells;actors.push_back(a);
    }out["actors"]=actors;
    if(out.contains("camps")){J camps=J::array();for(const auto& input:out.at("camps").as_array()) {
        if(!input.contains("design")){camps.push_back(input);continue;}const auto name=input.at("design").get<std::string>();const J* d=nullptr;for(const auto& q:catalog.at("camps").as_array())if(q.value("name","")==name)d=&q;require(d,"unknown camp design "+name);J c=input;const auto& stats=d->at("stats");c["max_hp"]=f(input,"max_hp",f(stats,"base_health"));c["hp"]=f(input,"hp",f(c,"max_hp"));c["strength"]=f(stats,"base_strength");c["range"]=n(stats,"base_attack_range");require(input.contains("gems")&&input.contains("gold")&&input.contains("respawn"),"camp rewards/respawn must be explicit measured inputs");camps.push_back(c);
    }out["camps"]=camps;}
    out["provenance"]=J{{"state_origin",specification.value("state_origin","constructed_catalog_parameters")},{"catalog_canonical_sha256",sha256(runtime_catalog.dump())},{"native_qualification","unvalidated"},{"passives","supported_explicit_scalars_only; caller overrides are final stats"}};
    return out;
}
} // namespace opt::sim
