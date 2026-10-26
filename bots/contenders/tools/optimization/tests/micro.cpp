#include "opt/micro.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace {
int checks=0;
void check(bool condition,const char* message) {
    ++checks;
    if(!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function,const char* message) {
    bool failed=false;
    try { function(); } catch(const std::exception&) { failed=true; }
    check(failed,message);
}
void action_and_prefix_contract() {
    using opt::Json;
    using opt::micro::Action;
    const auto primitives=Json::array({
        Json{{"type","move"},{"x",34},{"y",17}},
        Json{{"type","attack"},{"target",Json{{"kind",1},{"id",2}}}},
        Json{{"type","cast"},{"slot",1},{"target",Json()},{"x",-1},{"y",-1}},
        Json{{"type","attack"},{"target",Json{{"kind",1},{"id",2}}}},
        Json{{"type","end"}}
    });
    std::vector<opt::micro::Node> nodes(1);
    nodes[0].parent=0;
    for(std::size_t i=0;i<primitives.size();++i) {
        const auto action=Action::parse(primitives[i]);
        check(action.json()==primitives[i],"native primitive JSON changed its target, slot, coordinates, or null target");
        opt::micro::Node node;
        node.parent=i; node.incoming=action;
        nodes.push_back(node);
    }
    const auto ordered=opt::micro::prefix(nodes,nodes.size()-1);
    check(ordered.size()==5,"prefix omitted a primitive");
    for(std::size_t i=0;i<ordered.size();++i)
        check(ordered[i].json()==primitives[i],"prefix reordered move/attack/reset/attack/end");
    check(opt::micro::prefix(nodes,0).empty(),"root prefix must be empty");
    auto alternate=nodes[1]; alternate.parent=0;
    alternate.incoming=Action::parse(Json{{"type","move"},{"x",3},{"y",9}});
    nodes.push_back(alternate);
    const auto branched=opt::micro::prefix(nodes,nodes.size()-1);
    check(branched.size()==1&&branched[0].x==3&&branched[0].y==9,"prefix crossed an unrelated sibling branch");
    nodes.back().parent=nodes.size()-1;
    rejects([&] { (void)opt::micro::prefix(nodes,nodes.size()-1); },"corrupt parent cycle was traversed");
    rejects([&] { (void)opt::micro::prefix(nodes,nodes.size()+20); },"out-of-range parent was traversed");
    rejects([] { (void)Action::parse(Json{{"type","invented-action"}}); },"unsupported primitive accepted");
}
void checkpoint_record_contract() {
    using opt::Json;
    opt::micro::Node node;
    node.parent=9; node.visits=71;
    node.incoming=opt::micro::Action::parse(Json{{"type","cast"},{"slot",0},{"target",Json()},{"x",33},{"y",17}});
    node.owner=2; node.team=0; node.round=161; node.turns=4; node.same_turn_actions=3;
    node.value=-31.5; node.estimate=7.25;
    node.evaluated=true; node.terminal=false; node.invalid=false; node.cutoff=true; node.task_complete=false;
    node.next_action=2;
    node.actions={opt::micro::Action::parse(Json{{"type","end"}}),opt::micro::Action::parse(Json{{"type","move"},{"x",1},{"y",2}})};
    node.children={10,17}; node.metrics=Json{{"witness","selected enemy response"},{"health_delta",-100}};
    const auto saved=opt::micro::encode(node);
    const auto restored=opt::micro::decode(Json::parse(saved.dump()));
    check(opt::micro::encode(restored)==saved,"checkpoint changed a node's ordered actions, minimax fields, visits, or pending index");
    check(restored.team==0&&restored.owner==2&&restored.value==-31.5&&restored.next_action==2,"checkpoint lost enemy-owner or search cursor semantics");
    node.next_action=3;
    rejects([&] { (void)opt::micro::decode(opt::micro::encode(node)); },"checkpoint accepted a cursor beyond its action list");
}
void farm_goal_contract() {
    using opt::Json;
    const auto champion=[](int id,int team,int x,int y) {
        return Json{{"id",id},{"team",team},{"x",x},{"y",y},{"alive",true},{"health",100},{"maxHealth",100},
            {"gold",0},{"gems",0},{"cooldowns",Json::array({0,0})},{"buffs",Json::array()}};
    };
    Json initial{{"round",10},{"current_owner",0},
        {"champions",Json::array({champion(0,0,9,9),champion(1,1,1,1),champion(2,0,2,2)})},
        {"camps",Json::array({Json{{"id",48},{"health",50},{"spawn",Json{{"x",1},{"y",1}}}},
                             Json{{"id",84},{"health",50},{"spawn",Json{{"x",2},{"y",2}}}}})},
        {"groups",Json::array({Json{{"id",18},{"members",Json::array({48,84})},{"state",1},{"captureTeam",2},{"rewardAvailable",true}}})}};
    Json goal{{"kind","farm"},{"group",18},{"camp_ids",Json::array({48,84})},{"participants",Json::array({0,1})},
        {"require_capture",true},{"require_depart",true},{"completion_time_scale",128}};
    auto cleared=initial;
    cleared["round"]=11;
    for(auto& camp:cleared["camps"].as_array())camp["health"]=0;
    cleared["groups"][0]["state"]=3;
    cleared["groups"][0]["rewardAvailable"]=false;
    const auto score=[&](const Json& state,const Json& objective) {return opt::micro::evaluate(initial,state,objective,0,0);};
    check(!score(cleared,goal).task_complete,"neutral SDK capture marker was mistaken for a credited payout");
    const auto history=[](int group,int team,bool unique) {
        return Json{{"payouts",Json::array({Json{{"group",group},{"team",team},{"attribution_unique",unique},{"round",11}}})}};
    };
    cleared["objective_history"]=history(18,1,true);
    check(!score(cleared,goal).task_complete,"enemy payout completed our farm objective");
    cleared["objective_history"]=history(18,0,false);
    check(!score(cleared,goal).task_complete,"ambiguous resource event was treated as a proved group payout");
    cleared["objective_history"]=history(17,0,true);
    check(!score(cleared,goal).task_complete,"another group's payout completed the selected farm objective");
    cleared["objective_history"]=history(18,0,true);
    const auto finished=score(cleared,goal);
    check(finished.task_complete,"unique native payout plus selected-party departure did not complete the objective");
    check(finished.metrics.at("departed").get<bool>(),"enemy or unselected ally occupancy incorrectly blocked selected-party departure");
    auto selected_ally=goal;selected_ally["participants"]=Json::array({0,1,2});
    check(!score(cleared,selected_ally).task_complete,"selected friendly participant standing on a camp did not block departure");
    auto still_occupied=cleared;still_occupied["champions"][0]["x"]=1;still_occupied["champions"][0]["y"]=1;
    check(!score(still_occupied,goal).task_complete,"selected actor standing on a camp was considered departed");
    auto unpaid=cleared;unpaid["groups"][0]["rewardAvailable"]=true;
    check(!score(unpaid,goal).task_complete,"available global reward contradicted credited-payout completion");
    auto no_group=cleared;no_group["groups"]=Json::array();
    rejects([&] { (void)score(no_group,goal); },"capture-required goal accepted missing native group metadata");
    auto late=cleared;late["round"]=10000;
    auto later=cleared;later["round"]=20000;
    const auto late_value=score(late,goal),later_value=score(later,goal);
    check(late_value.task_complete&&later_value.task_complete,"late completion was lost beyond the nominal time scale");
    check(finished.value>late_value.value&&late_value.value>later_value.value,"completed routes were not strictly ordered by completion time");
    check(later_value.value>score(unpaid,goal).value,"an incomplete route outranked a very late completed route");
}
void complete_action_interleaving_contract() {
    using opt::Json;
    using opt::micro::Action;
    std::vector<Action> proposals;
    const auto add=[&](Json action) {proposals.push_back(Action::parse(action));};
    add(Json{{"type","end"}});
    for(int i=0;i<8;++i)add(Json{{"type","move"},{"x",i},{"y",20}});
    const auto attack=Action::parse(Json{{"type","attack"},{"target",Json{{"kind",1},{"id",2}}}});
    proposals.push_back(attack);
    const auto targeted=Action::parse(Json{{"type","cast"},{"slot",0},{"target",Json{{"kind",1},{"id",2}}},{"x",-1},{"y",-1}});
    proposals.push_back(targeted);
    for(int i=0;i<82;++i)add(Json{{"type","cast"},{"slot",0},{"target",Json()},{"x",i%10},{"y",i/10}});
    const auto reset=Action::parse(Json{{"type","cast"},{"slot",1},{"target",Json()},{"x",-1},{"y",-1}});
    proposals.push_back(reset);
    add(Json{{"type","cast"},{"slot",1},{"target",Json()},{"x",70},{"y",70}});
    add(Json{{"type","recall"}});
    add(Json{{"type","cancel_channel"}});
    add(Json{{"type","submit_gems"},{"x",70},{"y",70}});
    const auto ordered=opt::micro::interleave_actions(proposals);
    check(ordered.size()==proposals.size(),"interleaving changed the complete native proposal count");
    std::multiset<std::string> before,after;
    for(const auto& action:proposals)before.insert(action.json().dump());
    for(const auto& action:ordered)after.insert(action.json().dump());
    check(before==after,"interleaving pruned or invented an action");
    check(ordered==opt::micro::interleave_actions(proposals),"proposal interleaving is not deterministic");
    const auto rank=[&](const Action& action) {return std::find(ordered.begin(),ordered.end(),action)-ordered.begin();};
    check(rank(reset)<10,"82 ground casts starved the other ability's self/reset action");
    check(rank(attack)<10,"movement or ground fanout starved the basic attack");
    check(rank(targeted)<10,"ground fanout starved an entity-targeted ability");
    std::vector<Action> original_ground,ordered_ground;
    for(const auto& action:proposals)if(action.type=="cast"&&action.slot==0&&action.target_kind<0&&action.x>=0)original_ground.push_back(action);
    for(const auto& action:ordered)if(action.type=="cast"&&action.slot==0&&action.target_kind<0&&action.x>=0)ordered_ground.push_back(action);
    check(original_ground==ordered_ground,"interleaving changed within-class native proposal order");
    std::reverse(proposals.begin(),proposals.end());
    const auto reversed=opt::micro::interleave_actions(proposals);
    check(std::find(reversed.begin(),reversed.end(),reset)-reversed.begin()<10,"class fairness depended on the input placing reset before ground fanout");
}
}

int main(int argc,char** argv) {
    opt::install_signal_handlers();
    try {
        if(argc>1&&(std::string(argv[1])=="micro"||std::string(argv[1])=="c12"))
            return opt::run_micro(opt::parse_options(argc,argv));
        action_and_prefix_contract();
        checkpoint_record_contract();
        farm_goal_contract();
        complete_action_interleaving_contract();
        std::cout<<"micro checks="<<checks<<" node_bytes="<<sizeof(opt::micro::Node)
                 <<" action_bytes="<<sizeof(opt::micro::Action)<<'\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"micro verification error: "<<error.what()<<'\n';
        return 1;
    }
}
