#include "opt/micro.hpp"
#include "opt/micro_campaign.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>

namespace opt::micro {
namespace {
double number(const Json& j,const char* a,const char* b,double fallback=0.) {
    return j.contains(a)?j.at(a).get<double>():j.value<double>(b,fallback);
}
const Json& champion(const Json& state,int id) {
    for(const auto& a:state.at("champions").as_array())if(a.at("id").get<int>()==id)return a;
    throw std::runtime_error("micro native state omits champion "+std::to_string(id));
}
bool alive(const Json& a){return a.value("alive",number(a,"health","hp")>0);}
int owner(const Json& s){return s.contains("current_owner")?s.at("current_owner").get<int>():s.at("moment").at("currentChampion").get<int>();}
int round(const Json& s){return s.contains("round")?s.at("round").get<int>():s.at("moment").at("round").get<int>();}
Json participants(const Json& initial,const Json& objective) {
    if(objective.contains("participants"))return objective.at("participants");
    Json ids=Json::array();for(const auto& a:initial.at("champions").as_array())ids.push_back(a.at("id"));return ids;
}
Json compact_actions(const std::vector<Action>& actions) {
    Json list=Json::array();for(const auto& a:actions)list.push_back(Json::array({a.type,a.x,a.y,a.slot,a.target_kind,a.target_id}));return list;
}
Action decode_action(const Json& a) {
    if(a.is_object())return Action::parse(a);
    if(!a.is_array()||a.size()!=6)throw std::runtime_error("invalid compact micro action");
    return Action{a.at(0).get<std::string>(),a.at(1).get<int>(),a.at(2).get<int>(),a.at(3).get<int>(),a.at(4).get<int>(),a.at(5).get<int>()};
}
Json json_prefix(const std::vector<Action>& actions) {
    Json list=Json::array();for(const auto& a:actions)list.push_back(a.json());return list;
}
bool has_id(const Json& ids,int id) {
    for(const auto& x:ids.as_array())if(x.get<int>()==id)return true;return false;
}
}

Options native_options(const Options& options) {
    Options out=options;if(out.has("evaluator"))return out;
    auto python=options.get("python");
    if(python.empty())for(const auto& path:{options.repo/"runtime/python/python.exe",options.repo/".tools/python/python.exe"})if(fs::is_regular_file(path)){python=path.string();break;}
    if(python.empty())if(const char* home=std::getenv("USERPROFILE")) {
        const auto path=fs::path(home)/".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
        if(fs::is_regular_file(path))python=path.string();
    }
    if(python.empty())python="python3";
    out.extra["evaluator"]={python};out.extra["evaluator-arg"]={"-B","-I","-X","utf8",(options.repo/"bots/contenders/tools/optimization/adapters/native_micro.py").string()};
    return out;
}

Json Action::json() const {
    Json value{{"type",type}};
    if(type=="move"||type=="cast"||type=="submit_gems"){value["x"]=x;value["y"]=y;}
    if(type=="cast")value["slot"]=slot;
    if(type=="cast")value["target"]=nullptr;
    if(target_kind>=0)value["target"]=Json{{"kind",target_kind},{"id",target_id}};
    return value;
}
Action Action::parse(const Json& value) {
    if(!value.is_object()||!value.contains("type"))throw std::runtime_error("invalid native micro action");
    Action a;a.type=value.at("type").get<std::string>();a.x=value.value("x",-1);a.y=value.value("y",-1);a.slot=value.value("slot",-1);
    if(value.contains("target")&&!value.at("target").is_null()) {
        a.target_kind=value.at("target").at("kind").get<int>();a.target_id=value.at("target").at("id").get<int>();
    }
    const std::map<std::string,std::set<std::string>> grammar{{"move",{"type","x","y"}},
        {"attack",{"type","target"}},{"cast",{"type","slot","target","x","y"}},
        {"end",{"type"}},{"recall",{"type"}},{"cancel_channel",{"type"}},{"submit_gems",{"type","x","y"}}};
    if(!grammar.contains(a.type))throw std::runtime_error("unsupported native micro primitive: "+a.type);
    const auto& allowed=grammar.at(a.type);
    for(const auto& [key,item]:value.as_object()){(void)item;if(!allowed.contains(key))throw std::runtime_error("unrepresented native action field: "+key);}
    if((a.type=="move"||a.type=="submit_gems")&&(!value.contains("x")||!value.contains("y")))throw std::runtime_error("micro position missing");
    if(a.type=="cast"&&(!value.contains("x")||!value.contains("y")||(a.slot!=0&&a.slot!=1)))throw std::runtime_error("invalid micro cast position or slot");
    if(a.type=="attack"&&a.target_kind<0)throw std::runtime_error("micro attack target missing");
    if(a.target_kind>=0&&(a.target_kind>6||a.target_id<0))throw std::runtime_error("invalid micro target");
    return a;
}

std::vector<Action> interleave_actions(std::vector<Action> actions) {
    std::array<std::vector<Action>,10> buckets;
    for(auto& action:actions) {
        std::size_t bucket=0;
        if(action.type=="attack")bucket=1;
        else if(action.type=="cast")bucket=action.target_kind<0&&action.x>=0?5+action.slot:2+action.slot;
        else if(action.type=="move")bucket=4;
        else if(action.type=="recall")bucket=7;
        else if(action.type=="cancel_channel")bucket=8;
        else if(action.type=="submit_gems")bucket=9;
        buckets.at(bucket).push_back(std::move(action));
    }
    std::vector<Action> ordered;ordered.reserve(actions.size());
    for(std::size_t offset=0;ordered.size()<actions.size();++offset)
        for(auto& bucket:buckets)if(offset<bucket.size())ordered.push_back(std::move(bucket[offset]));
    return ordered;
}

Evaluation evaluate(const Json& initial,const Json& state,const Json& objective,int team,int actor) {
    Evaluation result;const auto ids=participants(initial,objective);
    double own_hp=0,enemy_hp=0,own_initial=0,enemy_initial=0,shield_advantage=0;
    int own_alive=0,enemy_alive=0,own_was_alive=0,enemy_was_alive=0;
    Json positions=Json::array(),cooldowns=Json::array();double own_gems=0,enemy_gems=0,own_gold=0;
    for(const auto& a:state.at("champions").as_array()) {
        const auto id=a.at("id").get<int>();if(!has_id(ids,id))continue;
        const auto& before=champion(initial,id);const bool ours=a.at("team").get<int>()==team;
        const double maximum=std::max(1.,number(a,"maxHealth","max_hp",1));
        const double health=alive(a)?std::max(0.,number(a,"health","hp"))/maximum:0;
        const double old_max=std::max(1.,number(before,"maxHealth","max_hp",1));
        const double old_health=alive(before)?std::max(0.,number(before,"health","hp"))/old_max:0;
        if(ours){own_hp+=health;own_initial+=old_health;own_alive+=alive(a);own_was_alive+=alive(before);own_gems+=a.value<double>("gems",0);own_gold+=a.value<double>("gold",0);}
        else{enemy_hp+=health;enemy_initial+=old_health;enemy_alive+=alive(a);enemy_was_alive+=alive(before);enemy_gems+=a.value<double>("gems",0);}
        shield_advantage+=(ours?1.:-1.)*a.value<double>("shield",0)/maximum;
        positions.push_back(Json::array({id,a.at("x"),a.at("y"),alive(a)}));
        cooldowns.push_back(Json{{"actor",id},{"cooldowns",a.value("cooldowns",Json::array())}});
        for(const auto& buff:a.value("buffs",Json::array()).as_array())if(buff.value("timer",1)==0)result.unstable=true;
        if(alive(a)&&health<.25)result.unstable=true;
    }
    if(state.contains("spells")&&!state.at("spells").empty())result.unstable=true;
    const int own_deaths=own_was_alive-own_alive,enemy_deaths=enemy_was_alive-enemy_alive;
    const auto& self=champion(state,actor);
    result.local_win=enemy_was_alive>0&&enemy_alive==0&&own_alive>0;
    result.local_loss=own_alive==0||(objective.value("survival_required",true)&&!alive(self));
    const int elapsed=round(state)-round(initial);
    const double health_swing=(own_hp-own_initial)-(enemy_hp-enemy_initial);
    result.metrics={{"elapsed_rounds",elapsed},{"friendly_alive",own_alive},{"enemy_alive",enemy_alive},
        {"friendly_deaths",own_deaths},{"enemy_deaths",enemy_deaths},{"friendly_hp_fraction",own_hp},{"enemy_hp_fraction",enemy_hp},
        {"health_swing",health_swing},{"shield_advantage_fraction",shield_advantage},{"friendly_carried_gems",own_gems},
        {"enemy_carried_gems",enemy_gems},{"friendly_gold",own_gold},{"ending_positions",positions},{"cooldowns",cooldowns},
        {"actor_hp",number(self,"health","hp")},{"actor_shield",self.value<double>("shield",0)},
        {"native_round",round(state)},{"native_owner",owner(state)}};
    const auto kind=objective.value("kind","combat");
    if(kind=="farm") {
        Json targets=objective.value("camp_ids",Json::array());const int group_id=objective.value("group",-1);
        const Json* group=nullptr;for(const auto& g:state.at("groups").as_array())if(g.at("id").get<int>()==group_id)group=&g;
        if(targets.empty()&&group&&group->contains("members"))for(const auto& member:group->at("members").as_array())targets.push_back(member.is_object()?member.at("id"):member);
        double initial_health=0,remaining=0;bool departed=true;std::size_t found=0;
        for(const auto& c:state.at("camps").as_array())if(has_id(targets,c.at("id").get<int>())) {
            ++found;remaining+=std::max(0.,number(c,"health","hp"));
            for(const auto& old:initial.at("camps").as_array())if(old.at("id")==c.at("id"))initial_health+=std::max(0.,number(old,"health","hp"));
            const auto& spawn=c.contains("spawn")?c.at("spawn"):c;
            for(const auto& a:state.at("champions").as_array())if(a.at("team").get<int>()==team&&has_id(ids,a.at("id").get<int>())
                &&alive(a)&&a.at("x")==spawn.at("x")&&a.at("y")==spawn.at("y"))departed=false;
        }
        if(targets.empty()||found!=targets.size())throw std::runtime_error("farm goal needs complete native camp membership");
        const bool cleared=remaining<=0;
        bool captured=false;
        if(group&&group->contains("paid_team"))captured=group->at("paid_team").get<int>()==team;
        Json payouts=Json::array();
        if(state.contains("objective_history"))for(const auto& paid:state.at("objective_history").value("payouts",Json::array()).as_array())
            if(paid.at("group").get<int>()==group_id&&paid.at("team").get<int>()==team&&paid.value("attribution_unique",false)) {
                captured=true;payouts.push_back(paid);
            }
        if(objective.value("require_capture",true)&&!group)throw std::runtime_error("farm capture goal needs a native group id");
        if(captured&&group&&group->contains("rewardAvailable")&&group->at("rewardAvailable").get<bool>())captured=false;
        result.task_complete=cleared&&(!objective.value("require_capture",true)||captured)&&(!objective.value("require_depart",true)||departed)&&!result.local_loss;
        const double progress=initial_health>0?std::clamp(1.-remaining/initial_health,0.,1.):1.;
        const double travel=objective.value<double>("completion_time_scale",128.);
        if(travel<=0)throw std::runtime_error("completion_time_scale must be positive");
        // Completion dominates every incomplete branch, and completed routes
        // remain strictly ordered by time beyond the nominal time scale. HP,
        // cooldowns and position are preserved separately for endpoint choice.
        const double normalized_hp=std::clamp(own_hp/std::max(1,own_was_alive),0.,1.);
        result.value=result.local_loss?-1.:result.task_complete?.75+.24/(1.+std::max(0,elapsed)/travel):.4*progress+.1*captured+.025*normalized_hp;
        result.metrics["target_camps"]=targets;result.metrics["camp_hp_remaining"]=remaining;
        result.metrics["cleared"]=cleared;result.metrics["captured"]=captured;result.metrics["departed"]=departed;
        result.metrics["native_payouts"]=payouts;
    } else if(kind=="combat") {
        const auto weights=objective.value("heuristic",Json::object());
        const double estimate=weights.value("kill_balance",2.)*(enemy_deaths-own_deaths)+weights.value("health_swing",1.)*health_swing
            +weights.value("shield_fraction",.2)*shield_advantage;
        result.value=result.local_loss?-1.:result.local_win?1.:.9*std::tanh(estimate);
        result.task_complete=result.local_win;
    } else throw std::runtime_error("micro objective kind must be combat or farm");
    result.metrics["task_complete"]=result.task_complete;result.metrics["local_win"]=result.local_win;result.metrics["local_loss"]=result.local_loss;
    result.metrics["cutoff_estimate"]=result.value;return result;
}

Json encode(const Node& n) {
    Json children=Json::array();for(auto c:n.children)children.push_back(c);
    return Json::array({n.parent,Json::array({n.incoming.type,n.incoming.x,n.incoming.y,n.incoming.slot,n.incoming.target_kind,n.incoming.target_id}),
        n.owner,n.team,n.round,n.turns,n.same_turn_actions,n.value,n.estimate,n.visits,n.evaluated,n.terminal,n.invalid,n.cutoff,n.task_complete,n.next_action,
        compact_actions(n.actions),children,n.metrics});
}
Node decode(const Json& j) {
    if(!j.is_array()||j.size()!=19)throw std::runtime_error("invalid micro checkpoint node");
    Node n;n.parent=j.at(0).get<std::uint64_t>();n.incoming=decode_action(j.at(1));n.owner=j.at(2).get<int>();n.team=j.at(3).get<int>();n.round=j.at(4).get<int>();
    n.turns=j.at(5).get<int>();n.same_turn_actions=j.at(6).get<int>();n.value=j.at(7).get<double>();n.estimate=j.at(8).get<double>();n.visits=j.at(9).get<std::uint64_t>();
    n.evaluated=j.at(10).get<bool>();n.terminal=j.at(11).get<bool>();n.invalid=j.at(12).get<bool>();n.cutoff=j.at(13).get<bool>();n.task_complete=j.at(14).get<bool>();n.next_action=j.at(15).get<std::size_t>();
    for(const auto& a:j.at(16).as_array())n.actions.push_back(decode_action(a));for(const auto& c:j.at(17).as_array())n.children.push_back(c.get<std::uint64_t>());n.metrics=j.at(18);
    if(!std::isfinite(n.value)||!std::isfinite(n.estimate)||n.next_action>n.actions.size())throw std::runtime_error("invalid micro checkpoint statistics");return n;
}
std::vector<Action> prefix(const std::vector<Node>& nodes,std::uint64_t id) {
    std::vector<Action> out;std::set<std::uint64_t> seen;
    while(id) {
        if(id>=nodes.size()||!seen.insert(id).second)throw std::runtime_error("micro parent cycle/out-of-range");
        out.push_back(nodes[id].incoming);id=nodes[id].parent;
    }
    std::reverse(out.begin(),out.end());return out;
}
} // namespace opt::micro

namespace opt {
namespace {
using micro::Node;using micro::Action;
void micro_backup(std::vector<Node>& nodes,std::uint64_t id,int team) {
    for(;;) {
        auto& n=nodes.at(static_cast<std::size_t>(id));
        bool any=false;double value=n.team==team?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();
        for(auto c:n.children)if(nodes.at(c).evaluated&&!nodes.at(c).invalid) {
            any=true;value=n.team==team?std::max(value,nodes[c].value):std::min(value,nodes[c].value);
        }
        n.value=any?value:n.estimate;if(!id)break;id=n.parent;
    }
}
std::vector<std::uint64_t> micro_variation(const std::vector<Node>& nodes,int team) {
    std::vector<std::uint64_t> path;std::uint64_t id=0;
    for(std::size_t depth=0;depth<nodes.size();++depth) {
        const auto& n=nodes.at(id);std::uint64_t best=0;double value=n.team==team?-2.:2.;bool any=false;
        for(auto c:n.children)if(nodes.at(c).evaluated&&!nodes.at(c).invalid) {
            const bool improve=!any||(n.team==team?nodes[c].value>value:nodes[c].value<value);
            if(improve){best=c;value=nodes[c].value;any=true;}
        }
        if(!any)break;path.push_back(best);id=best;
    }
    return path;
}
}

int run_micro(const Options& options) {
    if(!options.input.empty()&&micro_campaign_requested(read_json(options.input)))return run_micro_campaign(options);
    Run run(options,"c12",micro::version);const auto search=run.config.value("search",Json::object());
    if(!run.config.contains("scenario"))throw std::runtime_error("c12 needs an actual native replay scenario");
    Json objective=run.config.value("objective",Json{{"kind","combat"}}),scenario=run.config.at("scenario");
    const int actor=scenario.at("actor").get<int>();
    if(scenario.contains("participants"))objective["participants"]=scenario.at("participants");
    const auto oracle=micro::native_options(options);
    double backend_seconds=0;std::uint64_t backend_calls=0,invocation_evaluations=0;
    std::uint64_t peak_native_memory=0,peak_node_rss=0;
    Json base{{"schema","contenders-micro/v1"},{"scenario",scenario},{"objective",objective},
        {"generation",run.config.value("generation",Json{{"ground_full",true},{"channels",true}})}};
    auto call=[&](Json request,const fs::path& directory) {
        const auto started=std::chrono::steady_clock::now();
        const auto response=evaluate(oracle,request,directory);
        backend_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();++backend_calls;
        const auto stats=response.value("stats",Json::object());
        peak_native_memory=std::max(peak_native_memory,stats.value<std::uint64_t>("wasm_memory_bytes",0));
        peak_node_rss=std::max(peak_node_rss,stats.value<std::uint64_t>("node_rss_bytes",0));
        const bool fixture=options.has("allow-test-fixture");
        if(!response.value("supported",false)||(!response.value("native",false)&&!fixture))throw std::runtime_error("micro backend refused native scenario: "+response.value("reason","missing native capability"));
        if(!response.contains("identity"))throw std::runtime_error("micro backend omitted frozen identity");
        if(run.state.contains("native_identity")&&response.at("identity")!=run.state.at("native_identity"))throw std::runtime_error("micro native identity changed");
        return response;
    };
    Json initial,identity_response;
    std::vector<Node> nodes;Json pending=Json::array();
    const auto max_nodes=search.value<std::size_t>("max_nodes",100000),batch_size=search.value<std::size_t>("batch_size",32);
    const auto primitive_cap=search.value<int>("max_actions_per_turn",8),horizon=search.value<int>("horizon_turns",4),round_horizon=search.value<int>("horizon_rounds",3),quiescence=search.value<int>("quiescence_turns",2);
    const auto prefix_cap=search.value<std::size_t>("max_prefix_actions",512);
    const double explore=search.value<double>("exploration",.15),widen=search.value<double>("progressive_widening",2.),power=search.value<double>("widening_power",.5);
    if(!max_nodes||!batch_size||batch_size>256||primitive_cap<1||primitive_cap>64||horizon<1||horizon>128||round_horizon<1||quiescence<0||quiescence>128||!prefix_cap||prefix_cap>512||explore<0||widen<=0||power<=0||power>1)
        throw std::runtime_error("invalid micro search limits");
    const auto checkpoint_limit=search.value<std::size_t>("max_checkpoint_mb",384)*1024*1024;
    if(checkpoint_limit<1024*1024||checkpoint_limit>768ULL*1024*1024)throw std::runtime_error("micro checkpoint limit must be1..768MiB");
    const auto memory_limit=search.value<std::size_t>("max_state_memory_mb",512)*1024*1024;
    if(memory_limit<16ULL*1024*1024||memory_limit>16384ULL*1024*1024)throw std::runtime_error("micro state-memory limit must be16..16384MiB");
    if(!run.resumed){run.state={{"nodes",Json::array()},{"pending",Json::array()},{"evaluations",0}};run.checkpoint();}
    if(run.stop()) {
        if(run.state.contains("native_identity")&&fs::is_regular_file(options.out/"report.json")) {
            auto retained=read_json(options.out/"report.json");retained["no_new_work_this_invocation"]=true;
            run.finish(std::move(retained));
        } else run.finish(Json{{"status","checkpointed_before_native_identity"},{"native_evaluations",run.iteration}});
        return 0;
    }
    Json identity_request=base;identity_request["kind"]="identity";identity_response=call(identity_request,options.out/"identity");
    if(run.state.contains("root_state"))initial=run.state.at("root_state");else initial=identity_response.at("state");
    const auto team=micro::champion(initial,actor).at("team").get<int>();
    if(micro::owner(initial)!=actor)throw std::runtime_error("micro source is not the requested actor input");
    if(!run.state.contains("native_identity")) {
        run.state["native_identity"]=identity_response.at("identity");run.state["root_state"]=initial;
        write_json(options.out/"root-state.json",initial);write_json(options.out/"native-identity.json",identity_response.at("identity"));
    }
    auto initialize=[&](Node& n,const Json& state,const Json& actions) {
        n.owner=micro::owner(state);n.team=micro::champion(state,n.owner).at("team").get<int>();n.round=micro::round(state);
        const auto value=micro::evaluate(initial,state,objective,team,actor);n.estimate=n.value=value.value;n.metrics=value.metrics;n.task_complete=value.task_complete;n.evaluated=true;
        if(state.contains("fingerprint"))n.metrics["native_state_fingerprint"]=state.at("fingerprint");
        if(state.contains("observation"))n.metrics["owner_observation_sha256"]=sha256(state.at("observation").dump());
        const bool cutoff=n.turns>=horizon||n.round-micro::round(initial)>=round_horizon;
        const bool extend=cutoff&&value.unstable&&n.turns<horizon+quiescence;
        n.cutoff=cutoff&&!extend;n.terminal=value.local_loss||value.task_complete||n.cutoff;
        std::set<std::string> seen;
        if(!n.terminal)for(const auto& raw:actions.as_array()) {
            const auto action=Action::parse(raw);if(n.same_turn_actions>=primitive_cap&&action.type!="end")continue;
            if(seen.insert(action.json().dump()).second)n.actions.push_back(action);
        }
        n.actions=micro::interleave_actions(std::move(n.actions));
        if(!n.terminal&&n.actions.empty()){n.cutoff=true;n.terminal=true;}
    };
    if(run.resumed&&!run.state.at("nodes").empty()) {
        for(const auto& encoded:run.state.at("nodes").as_array())nodes.push_back(micro::decode(encoded));
        pending=run.state.at("pending");
        for(std::size_t i=0;i<nodes.size();++i){if(i&&nodes[i].parent>=i)throw std::runtime_error("micro invalid checkpoint parent ordering");for(auto child:nodes[i].children)if(child>=nodes.size()||nodes[child].parent!=i)throw std::runtime_error("micro invalid checkpoint child");}
    } else {Node root;initialize(root,initial,identity_response.at("actions"));nodes.push_back(std::move(root));}
    auto memory_estimate=[&]() {
        // The compact JSON representation is compact on disk, not in RAM. A
        // six-field action contains seven Json objects and a vector allocation.
        // Budget both old and replacement checkpoint DOMs at their peak, plus
        // native C++ nodes/actions, metrics, pending prefixes and output space.
        std::uint64_t bytes=nodes.capacity()*sizeof(Node);
        for(const auto& n:nodes) {
            bytes+=n.actions.capacity()*sizeof(Action)+n.children.capacity()*sizeof(std::uint64_t);
            bytes+=2*(32*sizeof(Json)+n.actions.size()*(8*sizeof(Json)+64)+n.children.size()*sizeof(Json));
            bytes+=64*n.metrics.dump().size();
        }
        bytes+=16*pending.dump().size()+16*1024*1024;
        return bytes;
    };
    std::uint64_t estimated_peak_memory=memory_estimate();
    auto checkpoint=[&]() {
        estimated_peak_memory=std::max(estimated_peak_memory,memory_estimate());
        Json encoded=Json::array();for(const auto& node:nodes)encoded.push_back(micro::encode(node));
        if(encoded.dump().size()>checkpoint_limit)throw std::runtime_error("micro checkpoint exceeds configured restorable size; previous committed checkpoint retained");
        run.state["nodes"]=std::move(encoded);run.state["pending"]=pending;run.state["evaluations"]=run.iteration;run.checkpoint();
    };
    checkpoint();
    if(!run.state.contains("baseline")&&!run.stop()) {
        Json request=base;request["kind"]="baseline";request["horizon_turns"]=search.value<int>("baseline_turns",std::max(16,horizon));
        const auto response=call(request,options.out/"baseline");
        if(!response.value("prefix_valid",true))throw std::runtime_error("micro native baseline prefix failed independent replay");
        const auto evaluated=micro::evaluate(initial,response.at("state"),objective,team,actor);
        run.state["baseline"]=Json{{"value",evaluated.value},{"task_complete",evaluated.task_complete},{"metrics",evaluated.metrics},{"prefix",response.at("prefix")},
            {"scope","Feasible deterministic native greedy continuation; a control, not an optimal or adversarial guarantee."}};
        write_json(options.out/"baseline.json",response);checkpoint();
    }
    bool exhausted=false,resource_limit=false;
    auto selectable=[&](const auto& self,std::uint64_t id)->bool {
        const auto& n=nodes.at(id);if(n.invalid||n.terminal||!n.evaluated)return false;
        if(n.next_action<n.actions.size())return true;
        for(auto child:n.children)if(self(self,child))return true;return false;
    };
    auto reserve=[&]()->bool {
        // Follow a native-verified feasible control one edge per batch before
        // ordinary widening. Long capture waits otherwise have a flat partial
        // score, so a short known solution can be buried among move variants.
        // Every seeded edge is still evaluated and charged normally, and all
        // other friendly choices and adverse replies remain searchable.
        if(search.value("seed_baseline",true)&&run.state.contains("baseline")) {
            std::uint64_t seed_id=0;std::vector<std::uint64_t> ancestors;
            for(const auto& raw:run.state.at("baseline").at("prefix").as_array()) {
                auto& n=nodes.at(seed_id);
                if(!n.evaluated||n.invalid||n.terminal)break;
                ancestors.push_back(seed_id);const auto wanted=Action::parse(raw);
                bool found=false;
                for(auto child:n.children)if(nodes.at(child).incoming==wanted){seed_id=child;found=true;break;}
                if(found)continue;
                std::size_t proposal=n.next_action;
                while(proposal<n.actions.size()&&!(n.actions[proposal]==wanted))++proposal;
                if(proposal==n.actions.size())break;
                std::swap(n.actions[proposal],n.actions[n.next_action]);
                const auto action=n.actions[n.next_action++];const auto next_id=nodes.size();
                Node next;next.parent=seed_id;next.incoming=action;next.turns=n.turns+(action.type=="end");
                next.same_turn_actions=action.type=="end"?0:n.same_turn_actions+1;
                n.children.push_back(next_id);nodes.push_back(std::move(next));
                for(auto ancestor:ancestors)nodes.at(ancestor).visits++;
                pending.push_back(Json{{"id",std::to_string(next_id)},{"prefix",micro::json_prefix(micro::prefix(nodes,next_id))}});
                return true;
            }
        }
        std::uint64_t id=0;std::vector<std::uint64_t> path;
        for(std::size_t depth=0;depth<10000;++depth) {
            auto& node=nodes.at(id);path.push_back(id);
            const auto width=std::min(node.actions.size(),std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(widen*std::pow(static_cast<double>(node.visits+1),power)))));
            bool expandable_child=false;for(auto child:node.children)if(selectable(selectable,child)){expandable_child=true;break;}
            if(node.next_action<node.actions.size()&&(node.next_action<width||!expandable_child)) {
                const auto action=node.actions.at(node.next_action++);const auto next_id=nodes.size();
                Node next;next.parent=id;next.incoming=action;next.turns=node.turns+(action.type=="end");
                next.same_turn_actions=action.type=="end"?0:node.same_turn_actions+1;
                node.children.push_back(next_id);nodes.push_back(std::move(next));
                for(auto ancestor:path)nodes.at(ancestor).visits++;
                pending.push_back(Json{{"id",std::to_string(next_id)},{"prefix",micro::json_prefix(micro::prefix(nodes,next_id))}});return true;
            }
            double best=-std::numeric_limits<double>::infinity();std::uint64_t chosen=0;bool found=false;
            for(auto child:node.children)if(selectable(selectable,child)) {
                const auto& c=nodes.at(child);const double priority=(node.team==team?1.:-1.)*c.value+explore*std::sqrt(std::log(static_cast<double>(node.visits+2))/static_cast<double>(c.visits+1));
                if(!found||priority>best){found=true;best=priority;chosen=child;}
            }
            if(!found)return false;id=chosen;
        }
        throw std::runtime_error("micro search path exceeds safe command bound");
    };
    while(!run.stop()) {
        if(pending.empty()) {
            // Leave room for one bounded native batch and final exports. This
            // is an explicit conservative estimate, not a measured RSS claim.
            if(memory_estimate()>memory_limit*4/5){resource_limit=true;break;}
            const auto remaining=options.iterations>run.iteration?options.iterations-run.iteration:0;
            // Reserve an entire deterministic batch even at a partial iteration
            // cap. Its unevaluated suffix remains checkpointed, so resuming at
            // any cap selects precisely the same work as an uninterrupted run.
            (void)remaining;
            for(std::size_t count=0;count<batch_size&&nodes.size()<max_nodes;++count)if(!reserve())break;
            if(pending.empty()){exhausted=!selectable(selectable,0);resource_limit=!exhausted;break;}
            checkpoint();
        }
        const auto capacity=std::min<std::uint64_t>(pending.size(),options.iterations-run.iteration);
        Json jobs=Json::array();for(std::size_t i=0;i<capacity;++i)jobs.push_back(pending.at(i));
        Json request=base;request["kind"]="evaluate";request["jobs"]=jobs;
        request["include_trace"]=false;
        request["trace_mode"]="summary";
        const auto response=call(request,options.out/"evaluations"/std::to_string(run.iteration));
        std::map<std::string,const Json*> results;
        for(const auto& item:response.at("results").as_array())if(!results.emplace(item.at("id").get<std::string>(),&item).second)throw std::runtime_error("duplicate native micro result id");
        if(results.size()!=jobs.size())throw std::runtime_error("native micro omitted a prefix");
        for(const auto& job:jobs.as_array()) {
            const auto key=job.at("id").get<std::string>();if(!results.contains(key))throw std::runtime_error("native micro result id mismatch");
            const auto id=static_cast<std::uint64_t>(std::stoull(key));auto& n=nodes.at(id);const auto& item=*results.at(key);
            if(!item.value("supported",true))throw std::runtime_error("native micro branch unsupported");
            if(!item.value("prefix_valid",false)){n.invalid=true;n.terminal=true;n.evaluated=true;n.estimate=n.value=0;}
            else {
                initialize(n,item.at("state"),item.at("actions"));
                const auto terminal=item.value("terminal",Json());
                if(terminal.is_object()&&terminal.contains("reason")&&!terminal.at("reason").is_null()) {
                    n.terminal=true;n.cutoff=!n.task_complete;n.metrics["native_terminal"]=terminal;
                }
                if(micro::prefix(nodes,id).size()>=prefix_cap){n.terminal=true;n.cutoff=!n.task_complete;n.actions.clear();n.metrics["prefix_bound_reached"]=true;}
            }
            micro_backup(nodes,id,team);run.advance();++invocation_evaluations;
        }
        Json suffix=Json::array();for(std::size_t i=capacity;i<pending.size();++i)suffix.push_back(pending.at(i));pending=std::move(suffix);checkpoint();
        run.progress(Json{{"native_evaluations",run.iteration},{"nodes",nodes.size()},{"backed_cutoff_value",nodes[0].value}});
        if(nodes.size()>=max_nodes){resource_limit=true;break;}
        if(memory_estimate()>memory_limit*4/5){resource_limit=true;break;}
        // Prevent a search from approaching the shared JSON reader's1GiB limit.
        if(run.state.at("nodes").dump().size()+16*1024*1024>checkpoint_limit){resource_limit=true;break;}
    }
    checkpoint();const auto variation=micro_variation(nodes,team);Json policy=Json::array();
    std::uint64_t leaf=0;for(auto id:variation){const auto& n=nodes.at(id);policy.push_back(Json{{"node",id},{"owner",nodes.at(n.parent).owner},{"team",nodes.at(n.parent).team},{"action",n.incoming.json()},
        {"after",n.metrics},{"backed_cutoff_value",n.value}});leaf=id;}
    Json root_actions=Json::array();std::ostringstream csv;csv<<"node,action,visits,backed_value,invalid,cutoff\n";
    for(auto id:nodes[0].children) {
        const auto& n=nodes[id];if(!n.evaluated)continue;
        root_actions.push_back(Json{{"node",id},{"action",n.incoming.json()},{"visits",n.visits},{"value",n.value},{"invalid",n.invalid},{"cutoff",n.cutoff}});
        csv<<id<<','<<n.incoming.type<<','<<n.visits<<','<<n.value<<','<<n.invalid<<','<<n.cutoff<<'\n';
    }
    write_json(options.out/"policy.json",Json{{"schema","native-micro-policy/v1"},{"native_identity",run.state.at("native_identity")},{"principal_variation",policy},
        {"root_actions",root_actions},{"scope","Observed-state native analysis; replan on every action or opponent deviation. No information-safe deployment certificate."}});
    Json tree=Json::array();std::uint64_t invalid_count=0,cutoff_count=0,enemy_nodes=0,completed_count=0,unfinished_count=0;
    Json primitive_counts=Json::object();
    for(std::size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i];invalid_count+=n.invalid;cutoff_count+=n.cutoff;completed_count+=n.task_complete;
        enemy_nodes+=n.evaluated&&!n.invalid&&n.team!=team;unfinished_count+=!n.evaluated;
        if(i){const auto count=primitive_counts.value<std::uint64_t>(n.incoming.type,0);primitive_counts[n.incoming.type]=count+1;}
        Json children=Json::array();std::uint64_t preferred=0;bool found=false;
        for(auto child:n.children)if(nodes[child].evaluated&&!nodes[child].invalid) {
            children.push_back(child);if(!found||(n.team==team?nodes[child].value>nodes[preferred].value:nodes[child].value<nodes[preferred].value)){preferred=child;found=true;}
        }
        tree.push_back(Json{{"id",i},{"parent",n.parent},{"incoming",i?n.incoming.json():Json()},{"owner",n.owner},{"team",n.team},
            {"role",n.team==team?"MAX":"MIN"},{"evaluated",n.evaluated},{"invalid",n.invalid},{"terminal",n.terminal},{"cutoff",n.cutoff},
            {"value",n.value},{"preferred_child",found?Json(preferred):Json()},{"observed_legal_children",children},
            {"remaining_proposals",n.actions.size()-n.next_action},{"state",n.metrics}});
    }
    write_json(options.out/"counterplay-tree.json",Json{{"schema","native-micro-counterplay/v1"},{"root_identity",run.state.at("native_identity")},
        {"nodes",tree},{"scope","Conditional explored tree keyed by exact ordered prefixes. MIN choices are adverse replies. Unexplored replies and projected observations carry no certified value bound."}});
    atomic_write(options.out/"candidate-actions.csv",csv.str());
    // Parent/action links share every prefix. Duplicating a full path for every
    // endpoint would turn a bounded tree into an O(nodes*depth) export.
    Json frontier=Json::array();for(std::size_t i=0;i<nodes.size();++i)if(nodes[i].task_complete&&!nodes[i].invalid)frontier.push_back(Json{{"node",i},{"metrics",nodes[i].metrics}});
    write_json(options.out/"frontier.json",Json{{"kind","feasible task endpoints, no universal HP/time conversion"},{"endpoints",frontier},
        {"prefix_graph","counterplay-tree.json"},{"prefix_encoding","Follow parent links from endpoint node to root, then reverse incoming actions."},{"complete_pareto_frontier",false}});
    std::ostringstream readable;readable<<"Native micro continuation\nActor "<<actor<<", source input "<<scenario.at("input").dump()<<", objective "<<objective.value("kind","combat")
        <<"\nSelected enemy owners choose adverse replies in this explored tree.\nCutoff values are estimates; unsearched replies have no certified bound.\n\n";
    for(auto id:variation) {
        const auto& n=nodes[id];const auto& parent=nodes[n.parent];readable<<"Round "<<parent.round<<" owner "<<parent.owner<<(parent.team==team?" friendly: ":" enemy reply: ")<<n.incoming.json().dump()
            <<" -> actor HP "<<n.metrics.value<double>("actor_hp",0)<<", enemy HP fraction "<<n.metrics.value<double>("enemy_hp_fraction",0);
        if(n.metrics.contains("camp_hp_remaining"))readable<<", camp HP "<<n.metrics.at("camp_hp_remaining").dump()<<", paid "<<n.metrics.at("captured").dump()<<", departed "<<n.metrics.at("departed").dump();
        readable<<'\n';
    }
    readable<<"\nAll explored responses: counterplay-tree.json\nOther feasible task endpoints: frontier.json (shared prefix graph)\n";
    atomic_write(options.out/"best-sequence.txt",readable.str());
    bool replay_verified=false;
    if(leaf&&!interrupted()&&!time_limit_reached()&&run.remaining_seconds()>.2) {
        Json request=base;request["kind"]="evaluate";request["include_trace"]=true;
        request["trace_mode"]="full";
        request["jobs"]=Json::array({Json{{"id","chosen"},{"prefix",micro::json_prefix(micro::prefix(nodes,leaf))}}});
        const auto replay=call(request,options.out/"chosen-replay");if(!replay.at("results").at(0).value("prefix_valid",false))throw std::runtime_error("micro chosen exported prefix failed native replay");
        write_json(options.out/"native-trace.json",replay);
        replay_verified=true;
    }
    Json result{{"status",exhausted?"exhausted_declared_tree":resource_limit?"resource_limited":"checkpointed_anytime"},
        {"native_evaluations",run.iteration},{"nodes",nodes.size()},{"native_identity",run.state.at("native_identity")},
        {"backend_authority",options.has("allow-test-fixture")?"test_fixture":"native_gameplay"},
        {"algorithm","progressively widened native primitive MAX/MIN tree; actual owner order, checkpointed batches"},
        {"proposal_order","Complete deterministic round robin over end, attack, both entity/self slots, movement, both ground slots and channel classes; no proposal removed."},
        {"verified_control_seeding",search.value("seed_baseline",true)},
        {"root_action",variation.empty()?Json():nodes.at(variation.front()).incoming.json()},
        {"principal_variation",policy},{"baseline",run.state.value("baseline",Json::object())},
        {"best",Json{{"node",leaf},{"value",nodes[0].value},{"metrics",nodes.at(leaf).metrics},{"task_complete",nodes.at(leaf).task_complete}}},
        {"chosen_prefix_replay_verified",replay_verified},
        {"coverage",Json{{"enemy_decision_nodes",enemy_nodes},{"invalid_native_prefixes",invalid_count},{"cutoff_nodes",cutoff_count},
            {"feasible_task_endpoints",completed_count},{"pending_evaluations",unfinished_count},{"reserved_primitive_counts",primitive_counts},
            {"all_declared_proposals_exhausted",exhausted}}},
        {"performance",Json{{"invocation_backend_calls",backend_calls},{"invocation_native_evaluations",invocation_evaluations},
            {"invocation_backend_seconds",backend_seconds},{"invocation_evaluations_per_backend_second",backend_seconds>0?invocation_evaluations/backend_seconds:0},
            {"peak_native_wasm_bytes",peak_native_memory},{"peak_backend_node_rss_bytes",peak_node_rss},
            {"checkpoint_size_limit_bytes",checkpoint_limit},{"checkpoint_state_bytes",run.state.dump().size()},
            {"estimated_search_state_peak_bytes",estimated_peak_memory},{"search_state_memory_limit_bytes",memory_limit},
            {"memory_estimate_scope","Conservative C++ nodes plus old/new JSON checkpoint DOMs and export allowance; not a measured host RSS bound."},
            {"method","Native compilation amortized across batches; exact identical prefix reuse within a batch; no projected-state merge."}}},
        {"horizon",Json{{"selected_owner_ends",horizon},{"rounds",round_horizon},{"quiescence_owner_ends",quiescence},{"primitive_cap_per_owner",primitive_cap},{"prefix_action_cap",prefix_cap}}},
        {"artifacts",Json{{"policy","policy.json"},{"readable_sequence","best-sequence.txt"},{"counterplay_tree","counterplay-tree.json"},{"root_alternatives","candidate-actions.csv"},{"feasible_endpoints","frontier.json"},{"baseline","baseline.json"},{"root_state","root-state.json"},
            {"chosen_trace",replay_verified?Json("native-trace.json"):Json()}}},
        {"scope","Native action-sequence and counterplay analysis from a pinned real equipped game state. Cutoff utilities are estimates, never proved interaction or full-game wins."},
        {"limitations",Json::array({"State construction is actual replay-prefix reconstruction; arbitrary champion/item JSON restoration is not assumed.",
            "Action lists are native-validated proposals; action bounds, widening, horizon and fixed background policies restrict coverage.",
            "Only selected enemy owners are MIN nodes. Yield/greedy backgrounds are explicit assumptions, not adversarial guarantees.",
            "No state transposition is inferred from projected HP/position equality; only identical ordered prefixes are cached.",
            "An omniscient offline teacher is not automatically an information-safe playable policy; personal observations remain separate.",
            "Feasible farm endpoints are native upper bounds in the declared background; no certified optimality gap is manufactured."})}};
    run.finish(std::move(result));return 0;
}
} // namespace opt
