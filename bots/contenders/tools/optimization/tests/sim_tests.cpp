#include "opt/sim.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using opt::J;
using namespace opt::sim;
static int checks=0;
static void check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
static bool has(const State& s,const Action& a){for(const auto& q:legal_actions(s))if(q==a)return true;return false;}
static J actor(int x,int y,bool ranged=false){return J{{"id","worker"},{"team",0},{"x",x},{"y",y},{"spawn",J::array({0,4})},{"hp",100.0},{"max_hp",100.0},{"strength",20.0},{"range",ranged?3:1},{"ranged",ranged}};}
static J camp(int x,int y,float hp=100){return J{{"id","target"},{"group",0},{"x",x},{"y",y},{"hp",hp},{"max_hp",hp},{"strength",0.0},{"range",3},{"gems",10},{"gold",20.0},{"respawn",20}};}
static J fixture(J a,J c){return J{{"width",5},{"height",5},{"actors",J::array({a})},{"camps",J::array({c})},{"settings",J{{"capture_time",1},{"spawn_healing",0.0},{"max_rounds",30}}},{"goal",J{{"kind","capture_depart"},{"team",0},{"group",0}}}};}
int main(){try{
    J a=actor(1,1,true);a["spells"]=J::array({J{{"kind","damage"},{"damage",30.0},{"range",4},{"exclusive",true},{"max_cooldown",8}},J{{"kind","reset"},{"max_cooldown",8}}});
    State s=initial(fixture(a,camp(3,1)));
    const J shot{{"type","basic"},{"target_kind","camp"},{"target",0}};
    check(!has(s,J{{"type","cast"},{"slot",1}}),"native code26 forbids reset while attack available");
    bool ready_reset_rejected=false;try{(void)step(s,J{{"type","cast"},{"slot",1}});}catch(const std::exception&){ready_reset_rejected=true;}check(ready_reset_rejected,"step rejects premature reset");
    s=step(s,J{{"type","move"},{"x",2},{"y",1}});check(!has(s,shot),"ranged move must spend basic");
    check(!has(s,J{{"type","cast"},{"slot",0},{"target_kind","camp"},{"target",0}}),"exclusive spell after step rejected");
    s=step(s,J{{"type","cast"},{"slot",1}});check(has(s,shot),"reset restores ranged basic");check(!s.at("actors").at(0).at("can_move").get<bool>(),"reset cannot restore movement");
    check(!has(s,J{{"type","cast"},{"slot",0},{"target_kind","camp"},{"target",0}}),"single cast budget");
    s=initial(fixture(a,camp(3,1)));s=step(s,J{{"type","cast"},{"slot",0},{"target_kind","camp"},{"target",0}});check(has(s,shot),"exclusive cast retains basic");
    s=step(s,shot);check(s.at("camps").at(0).at("hp").get<float>()==50,"cast then basic applied sequentially");
    const auto k=state_key(s);s=step(s,J{{"type","priority"},{"value",100}});s=step(s,J{{"type","priority"},{"value",0}});check(state_key(s)==k,"free priority cycle canonicalized");
    J w=camp(2,0,100);w["id"]="wolf";w["strength"]=20;
    J q=fixture(actor(0,0),camp(1,0,20));q["camps"].push_back(w);s=initial(q);s=step(s,shot);check(s.at("camps").at(1).at("awake").get<bool>(),"harmless member wakes whole group");s=step(s,J{{"type","end"}});check(s.at("actors").at(0).at("hp").get<float>()==80,"woken sibling hits before next owner input");
    q=fixture(actor(1,1),camp(2,1,20));s=initial(q);s=step(s,shot);s=step(s,J{{"type","move"},{"x",2},{"y",1}});s=step(s,J{{"type","end"}});check(s.at("groups").at(0).at("capture_left").get<int>()==1,"first phase arms capture");s=step(s,J{{"type","end"}});check(s.at("actors").at(0).at("gems").get<int>()==10,"next capture phase pays");check(status(s)=="active","payout must leave spawn tile");s=step(s,J{{"type","move"},{"x",1},{"y",1}});check(status(s)=="success","capture departure finishes");
    q=fixture(actor(1,1),camp(2,1,20));s=initial(q);s=step(s,shot);s=step(s,J{{"type","move"},{"x",2},{"y",1}});s=step(s,J{{"type","end"}});s=step(s,J{{"type","end"}});
    check(s.at("groups").at(0).value("status","")=="paid"&&s.at("groups").at(0).at("capture_team").get<int>()==-1,"native payout clears active capture ownership");
    check(s.at("groups").at(0).at("paid_team").get<int>()==0&&s.at("groups").at(0).at("payouts").at(0).get<int>()==1,"payout keeps historical team reward after neutral cleanup");
    q=fixture(actor(1,1),camp(2,1,649));q["actors"][0]["strength"]=129.8;q["goal"]["kind"]="clear";s=initial(q);int hits=0;while(status(s)=="active"){if(has(s,shot)){s=step(s,shot);++hits;}else s=step(s,J{{"type","end"}});}check(hits==6,"float32 subtraction needs sixth hit");
    q=fixture(actor(1,1),camp(2,1,500));q["actors"][0]["gems"]=20;q["altars"]=J::array({J::array({0,1})});q["goal"]=J{{"kind","bank"},{"team",0},{"score",20}};s=initial(q);s=step(s,J{{"type","tribute"}});check(s.at("score").at(0).get<int>()==0,"tribute not immediate");s=step(s,J{{"type","end"}});check(s.at("score").at(0).get<int>()==0,"tribute waits second tick");s=step(s,J{{"type","end"}});check(s.at("score").at(0).get<int>()==20,"tribute at following owner start");
    q=fixture(actor(1,1),camp(2,1,500));q["actors"][0]["hp"]=20;q["actors"][0]["spawn"]=J::array({0,4});q["settings"]["spawn_healing"]=1.0;q["settings"]["recall_time"]=2;s=initial(q);s=step(s,J{{"type","recall"}});s=step(s,J{{"type","end"}});s=step(s,J{{"type","end"}});check(s.at("actors").at(0).at("x").get<int>()==0,"recall resolves at owner start");check(s.at("actors").at(0).at("hp").get<float>()==20,"arrival heals at end not start");s=step(s,J{{"type","end"}});check(s.at("actors").at(0).at("hp").get<float>()==100,"spawn healing on owner end");
    bool rejected=false;try{q["actors"][0]["passive"]="heart_missing_health";(void)initial(q);}catch(const std::exception&){rejected=true;}check(rejected,"unsupported passive fails closed");
    q["actors"][0]["passive"]="already_applied";q["actors"][0]["items"]=J::array({"mystery-item"});rejected=false;try{(void)initial(q);}catch(const std::exception&){rejected=true;}check(rejected,"named item effects cannot be silently ignored");
    q=fixture(actor(1,1),camp(4,4,500));J enemy=actor(2,1);enemy["team"]=1;enemy["id"]="enemy";enemy["spawn"]=J::array({4,0});q["actors"].push_back(enemy);q["actors"][0]["strength"]=100;q["goal"]=J{{"kind","survive"},{"team",0},{"rounds",5}};s=initial(q);check(active_actor(s)==0,"teamA starts odd rounds");s=step(s,J{{"type","basic"},{"target_kind","actor"},{"target",1}});s=step(s,J{{"type","end"}});check(active_actor(s)==1&&!s.at("actors").at(1).at("can_attack").get<bool>(),"sequential death denies later actor basic");
    q["actors"][0]["strength"]=20;q["actors"][0]["spells"]=J::array({J{{"kind","stun"},{"damage",0.0},{"duration",1},{"range",4},{"max_cooldown",8}}});s=initial(q);s=step(s,J{{"type","cast"},{"slot",0},{"target_kind","actor"},{"target",1}});s=step(s,J{{"type","end"}});check(active_actor(s)==1&&!s.at("actors").at(1).at("can_attack").get<bool>(),"stun denies next target input");s=step(s,J{{"type","end"}});check(active_actor(s)==1&&s.at("actors").at(1).at("can_attack").get<bool>(),"target can act ahead next round after stun expiry");
    q["round"]=2;q["active_actor"]=0;q["actors"][0]["can_move"]=false;q["actors"][0]["can_attack"]=false;q["actors"][0]["priority"]=0;q["actors"][0]["next_priority"]=100;s=initial(q);check(active_actor(s)==0&&!s.at("actors").at(0).at("can_attack").get<bool>(),"explicit input allowance and actor preserved");check(s.at("actors").at(0).at("priority").get<int>()==0,"pending priority not applied at same owner input");
    rejected=false;try{q["round"]=-1;(void)initial(q);}catch(const std::exception&){rejected=true;}check(rejected,"negative round fails closed");
    q=fixture(actor(0,0),camp(1,0,20));J remote=camp(3,0,20);remote["id"]="remote";q["camps"].push_back(remote);rejected=false;try{(void)initial(q);}catch(const std::exception&){rejected=true;}check(rejected,"disconnected group fails closed");q["camps"][1]["x"]=2;q["camps"][1]["group"]=1;rejected=false;try{(void)initial(q);}catch(const std::exception&){rejected=true;}check(rejected,"cardinal neighbors cannot have different groups");
    q=fixture(actor(1,1),camp(4,4,100));J ally=actor(2,1);ally["id"]="ally";ally["priority"]=50;ally["next_priority"]=0;q["actors"].push_back(ally);q["actors"][0]["priority"]=0;q["actors"][0]["next_priority"]=100;s=initial(q);check(active_actor(s)==1,"initial order uses current not pending priorities");s=step(s,J{{"type","end"}});s=step(s,J{{"type","end"}});check(active_actor(s)==0,"next round applies pending priority");
    q=fixture(actor(1,1),camp(4,4,100));q["actors"][0]["team"]=0;enemy=actor(2,1);enemy["team"]=1;enemy["id"]="enemy";q["actors"].push_back(enemy);q["round"]=2;s=initial(q);check(active_actor(s)==1,"teamB starts even rounds");
    q=fixture(actor(1,1),camp(2,1,20));s=initial(q);s=step(s,shot);s=step(s,J{{"type","move"},{"x",2},{"y",1}});s=step(s,J{{"type","end"}});
    check(s.at("groups").at(0).at("capture_team").get<int>()==0,"capture active before withdrawal");
    s=step(s,J{{"type","move"},{"x",1},{"y",1}});
    check(s.at("groups").at(0).at("capture_team").get<int>()==-1&&s.at("groups").at(0).at("capture_left").get<int>()==-1,"last occupant withdrawal immediately cancels capture before end");
    q=fixture(actor(1,1),camp(1,1,20));q["camps"][0]["hp"]=0;q["actors"][0]["id"]=90;
    ally=actor(2,1);ally["id"]=10;q["actors"].push_back(ally);J second=camp(2,1,20);second["id"]="second";second["hp"]=0;second["gems"]=1;q["camps"].push_back(second);q["settings"]["capture_time"]=0;
    s=initial(q);s=step(s,J{{"type","end"}});s=step(s,J{{"type","end"}});
    check(s.at("actors").at(0).at("gems").get<int>()==5&&s.at("actors").at(1).at("gems").get<int>()==6,"lowest numeric actor ID receives gem remainder irrespective input order");
    q=fixture(actor(1,1),camp(4,4,100));q["round"]=30;q["goal"]=J{{"kind","bank"},{"team",0},{"score",10}};q["actors"][0]["gems"]=10;q["altars"]=J::array({J::array({0,1})});s=initial(q);
    check(status(s)=="active","last gameplay round permits champion actions");
    s=step(s,J{{"type","tribute"}});s=step(s,J{{"type","end"}});
    check(status(s)=="failure"&&s.at("score").at(0).get<int>()==0&&s.at("actors").at(0).at("channel_ticks").get<int>()==0,"no extra postgame owner start completes a pending bank");
    std::cout<<"sim: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"sim test failure: "<<e.what()<<"\n";return 1;}}
