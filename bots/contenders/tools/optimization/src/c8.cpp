#include "opt/scenarios.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace opt::scenarios {
namespace {
int number(const Json& j, const std::string& name, int fallback = 0) {
    return j.value(name, fallback);
}
int x(const Json& j) { return number(j, "x"); }
int y(const Json& j) { return number(j, "y"); }
bool stopped(const std::function<bool()>& stop) { return stop && stop(); }
int group(const Json& c) { return number(c, "group"); }
int distance(const Json& a, const Json& b) {
    return std::max(std::abs(x(a)-x(b)), std::abs(y(a)-y(b)));
}
std::size_t bounded_size(const Json& j, const char* key, std::size_t fallback,
                         std::size_t lower, std::size_t upper) {
    const auto v = j.value<std::int64_t>(key, static_cast<std::int64_t>(fallback));
    if (v < static_cast<std::int64_t>(lower) || v > static_cast<std::int64_t>(upper))
        throw std::runtime_error(std::string("C8 invalid ") + key);
    return static_cast<std::size_t>(v);
}
bool occupied(const Json& s, int px, int py) {
    for (const auto* name : {"actors", "camps"})
        if (s.contains(name)) for (const auto& a : s.at(name).as_array())
            if (x(a) == px && y(a) == py) return true;
    for (const auto* name : {"altars"})
        if (s.contains(name)) for (const auto& p : s.at(name).as_array())
            if (p.at(0).get<int>() == px && p.at(1).get<int>() == py) return true;
    if (s.contains("actors")) for (const auto& a : s.at("actors").as_array())
        if (a.contains("spawn") && a.at("spawn").at(0).get<int>() == px
            && a.at("spawn").at(1).get<int>() == py) return true;
    return false;
}
std::set<std::pair<int,int>> blocked(const Json& s) {
    std::set<std::pair<int,int>> out;
    if (s.contains("blocked")) for (const auto& p : s.at("blocked").as_array())
        out.emplace(p.at(0).get<int>(), p.at(1).get<int>());
    return out;
}
bool terrain_connected(const Json& s) {
    const int width = number(s,"width"), height = number(s,"height");
    const auto walls = blocked(s);
    if (width <= 0 || height <= 0 || width*height <= static_cast<int>(walls.size())) return false;
    std::pair<int,int> root{-1,-1};
    for (int py=0;py<height && root.first<0;++py) for (int px=0;px<width;++px)
        if (!walls.count({px,py})) { root={px,py}; break; }
    std::set<std::pair<int,int>> seen{root};
    std::deque<std::pair<int,int>> queue{root};
    while (!queue.empty()) {
        const auto [px,py]=queue.front(); queue.pop_front();
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            if (dx==0 && dy==0) continue;
            const auto p=std::make_pair(px+dx,py+dy);
            if (p.first>=0 && p.first<width && p.second>=0 && p.second<height
                && !walls.count(p) && seen.insert(p).second) queue.push_back(p);
        }
    }
    return seen.size()+walls.size() == static_cast<std::size_t>(width*height);
}
int team(const Json& s) {
    return s.contains("goal") ? s.at("goal").value("team",0) : 0;
}
Json trace(const sim::State& final, Json actions, bool limited, std::size_t expanded = 0, int scoring_team = 0) {
    return Json{{"status",sim::status(final)}, {"summary",sim::summary(final)},
        {"utility",sim::utility(final,scoring_team)}, {"actions",std::move(actions)},
        {"final_state",sim::encode_state(final)}, {"limited",limited},
        {"expanded",expanded}};
}
std::string action_type(const Json& action) { return action.value("type", ""); }
bool response_policy(const std::string& policy) {
    return policy=="default" || policy=="basic_first" || policy=="pressure" || policy=="cautious";
}
std::string opponent(const Json& scenario) { return scenario.value("opponent_policy","default"); }
std::string family(const Json& scenario) { return scenario.value("family",scenario.value("id","unnamed")); }
sim::Action reference_action(const sim::State& state, const std::string& reference) {
    if (reference == "default") return sim::baseline_action(state);
    const auto legal = sim::legal_actions(state);
    for (const auto& a : legal) {
        const auto kind=action_type(a);
        if (reference=="basic_first" && (kind=="basic" || kind=="attack")) return a;
        if (reference=="walk_first" && kind=="move") return a;
    }
    return sim::baseline_action(state);
}
Json baseline(const Json& scenario, const Limits& bounds, const std::function<bool()>& stop) {
    auto state=sim::initial(scenario);
    Json actions=Json::array();
    for (std::size_t n=0;n<bounds.max_actions && !sim::terminal(state);++n) {
        if (stopped(stop)) return Json{{"interrupted",true}};
        const int actor=sim::active_actor(state);
        const bool ours=actor>=0 && static_cast<std::size_t>(actor)<state.at("actors").size()
            && state.at("actors").at(static_cast<std::size_t>(actor)).value("team",0)==team(scenario);
        auto action=ours ? reference_action(state,scenario.value("reference_policy",bounds.baseline))
                         : opponent_action(state,opponent(scenario));
        state=sim::step(state,action);
        actions.push_back(std::move(action));
    }
    return trace(state,std::move(actions),!sim::terminal(state),0,team(scenario));
}
Json oracle(const Json& scenario, const Limits& bounds, const Json& incumbent,
            const std::function<bool()>& stop) {
    struct Node { sim::State state; std::size_t parent; Json action; double value; std::size_t depth; };
    const auto root=sim::initial(scenario);
    const auto estimated_state_bytes=std::max<std::size_t>(4096,root.dump().size()*12);
    const auto memory_states=std::max<std::size_t>(1,bounds.memory_mb*1024*1024/estimated_state_bytes);
    const auto state_limit=std::min(bounds.oracle_nodes,memory_states);
    std::vector<Node> nodes;
    nodes.reserve(std::min<std::size_t>(state_limit+1,100000));
    nodes.push_back(Node{root,0,Json(),sim::utility(root,team(scenario)),0});
    std::vector<std::size_t> frontier{0};
    std::unordered_set<std::string> visited{sim::state_key(root)};
    Json best=incumbent;
    double best_value=incumbent.at("utility").get<double>();
    std::size_t expanded=0, discarded=0, rollout_steps=0;
    bool capped=false;
    auto materialize=[&](std::size_t index) {
        std::vector<Json> reversed;
        auto cursor=index;
        while (cursor) { reversed.push_back(nodes[cursor].action); cursor=nodes[cursor].parent; }
        std::reverse(reversed.begin(),reversed.end());
        return Json(std::move(reversed));
    };
    while (!frontier.empty()) {
        // Complete a few promising prefixes with the fixed legal reference. This
        // produces real feasible witnesses even when primitive-action beam search
        // cannot reach a long capture horizon within its retained-state cap.
        for (std::size_t f=0;f<std::min<std::size_t>(4,frontier.size());++f) {
            const auto index=frontier[f];
            auto completion=nodes[index].state;
            auto actions=materialize(index);
            while (!sim::terminal(completion) && actions.size()<bounds.max_actions) {
                if (stopped(stop)) return Json{{"interrupted",true}};
                const int actor=sim::active_actor(completion);
                const bool ours=completion.at("actors").at(static_cast<std::size_t>(actor)).value("team",0)==team(scenario);
                auto action=ours ? sim::baseline_action(completion) : opponent_action(completion,opponent(scenario));
                completion=sim::step(completion,action);
                actions.push_back(std::move(action));
                ++rollout_steps;
            }
            const auto value=sim::utility(completion,team(scenario));
            if (sim::status(completion)=="success" && value>best_value) {
                best_value=value;
                best=trace(completion,std::move(actions),false,0,team(scenario));
            }
        }
        std::vector<std::size_t> next;
        for (const auto index : frontier) {
            if (stopped(stop)) return Json{{"interrupted",true}};
            if (expanded>=bounds.oracle_nodes) { capped=true; break; }
            if (nodes[index].depth>=bounds.max_actions) { capped=true; continue; }
            const auto current=nodes[index].state; // vector reallocation cannot invalidate it
            auto actions=sim::legal_actions(current);
            const int current_actor=sim::active_actor(current);
            if (current.contains("actors") && current_actor>=0
                && static_cast<std::size_t>(current_actor)<current.at("actors").size()
                && current.at("actors").at(static_cast<std::size_t>(current_actor)).value("team",0)!=team(scenario))
                actions={opponent_action(current,opponent(scenario))};
            ++expanded;
            for (const auto& action : actions) {
                if (nodes.size()>=state_limit) { capped=true; break; }
                auto child=sim::step(current,action);
                if (!visited.insert(sim::state_key(child)).second) continue;
                const auto id=nodes.size();
                const auto value=sim::utility(child,team(scenario));
                const auto depth=nodes[index].depth+1;
                nodes.push_back(Node{std::move(child),index,action,value,depth});
                if (sim::status(nodes.back().state)=="success" && value>best_value) {
                    best_value=value;
                    best=trace(nodes.back().state,materialize(id),false,0,team(scenario));
                }
                if (!sim::terminal(nodes.back().state)) next.push_back(id);
            }
            if (capped) break;
        }
        if (capped) break;
        std::stable_sort(next.begin(),next.end(),[&](auto a,auto b) {
            if (nodes[a].value != nodes[b].value) return nodes[a].value>nodes[b].value;
            return a<b;
        });
        if (next.size()>bounds.beam_width) {
            discarded+=next.size()-bounds.beam_width;
            next.resize(bounds.beam_width);
        }
        frontier=std::move(next);
    }
    best["expanded"]=expanded;
    best["states_generated"]=nodes.size();
    best["beam_discarded"]=discarded;
    best["completion_rollout_steps"]=rollout_steps;
    best["search_capped"]=capped;
    best["retained_state_cap"]=state_limit;
    best["estimated_bytes_per_state"]=estimated_state_bytes;
    best["memory_accounting"]="serialized-size estimate with overhead; not a measured RSS guarantee";
    best["search_kind"]="bounded beam plus legal completion rollouts; feasible alternatives, no optimum claim";
    return best;
}
Json find_design(const Json& catalog,const char* section,const std::string& name) {
    if (!catalog.is_object() || !catalog.contains("result")) return Json();
    const auto& result=catalog.at("result");
    if (!result.contains(section)) return Json();
    for (const auto& design : result.at(section).as_array())
        if (design.value("name","")==name) return design;
    return Json();
}
double stat(const Json& design,const char* name,double fallback) {
    return design.is_object() && design.contains("stats")
        ? design.at("stats").value(name,fallback) : fallback;
}
} // namespace

sim::Action opponent_action(const sim::State& state,const std::string& policy) {
    if (!response_policy(policy)) throw std::runtime_error("C8 unsupported opponent_policy");
    if (policy=="default") return sim::baseline_action(state);
    if (policy=="basic_first") return reference_action(state,"basic_first");
    const auto legal=sim::legal_actions(state);
    if (legal.empty()) return Json{{"type","end"}};
    const auto& actor=state.at("actors").at(static_cast<std::size_t>(sim::active_actor(state)));
    const int actor_team=actor.value("team",0);
    const Json* target=nullptr;
    for (const auto& candidate:state.at("actors").as_array())
        if (candidate.value("team",0)!=actor_team && candidate.value("hp",0.0)>0
            && (!target || distance(actor,candidate)<distance(actor,*target))) target=&candidate;
    if (!target) return sim::baseline_action(state);
    const bool retreat=policy=="cautious" && actor.value("hp",0.0)<actor.value("max_hp",1.0)*.5;
    double best=-1e100; auto chosen=legal.front();
    for (const auto& action:legal) {
        const auto kind=action_type(action); double value=-1e9;
        if (kind=="end") value=0;
        else if (kind=="tribute") value=20000;
        else if (kind=="basic") value=action.value("target_kind","")=="actor"
            ? 10000+actor.value("strength",0.0) : -10;
        else if (kind=="move") {
            const int before=distance(actor,*target),after=distance(action,*target);
            value=100*(retreat?after-before:before-after)+(retreat?after:-after);
        } else if (kind=="cast") {
            const auto& spell=actor.at("spells").at(static_cast<std::size_t>(action.value("slot",0)));
            const auto sk=spell.value("kind","");
            if (sk=="reset") value=7000;
            else if (sk=="shield") value=retreat?8000:20;
            else {
                int hits=0;
                if (action.contains("target")) hits=action.value("target_kind","")=="actor";
                else for (const auto& candidate:state.at("actors").as_array())
                    if (candidate.value("team",0)!=actor_team && candidate.value("hp",0.0)>0
                        && distance(action,candidate)<=spell.value("radius",0.0)) ++hits;
                value=hits ? 10000+hits*(spell.value("damage",0.0)+(sk=="stun"?10:0)) : -20;
            }
        }
        if (value>best) { best=value; chosen=action; }
    }
    return chosen;
}

Json validation_request(const Json& evaluation,const std::string& which,std::size_t state_limit) {
    if (which!="baseline" && which!="oracle") throw std::runtime_error("C8 invalid comparison trace name");
    const auto& actions=evaluation.at(which).at("actions");
    Json request{{"schema","contenders-scenario-validation/v2"},{"kind","scenario_validation"},
        {"scenario",evaluation.at("scenario")},{"actions",actions},
        {"scenario_sha256",evaluation.at("scenario_sha256")},{"trace_sha256",sha256(actions.dump())},
        {"model_version",sim::version},{"model_capabilities",sim::capabilities()},
        {"comparison_projection","restricted-model-after-every-action"},
        {"comparison_tolerance",Json{{"integer_and_boolean","exact"},{"position_and_order","exact"},
            {"float_absolute",0.0001},{"float_relative",0.000001},
            {"reason","model effects use float32; tolerate serialization/representation rounding, not different event ordering"}}},
        {"required_native_interface",Json::array({"controlled multi-actor initial state including camp group/reward timers and terrain",
            "ordered legal primitive actions with actual actor-event order",
            "native state projection after every action, including owner-end/camp/capture phases"})},
        {"qualification","request_only_not_native_validation"}};
    if (actions.size()>state_limit) {
        request["supported"]=false; request["reason"]="trace exceeds declared validation_state_limit";
        return request;
    }
    auto state=sim::initial(evaluation.at("scenario"));
    request["initial_prediction"]=sim::encode_state(state);
    Json predictions=Json::array();
    for (const auto& action:actions.as_array()) {
        state=sim::step(state,action); predictions.push_back(sim::encode_state(state));
    }
    request["prediction_sha256"]=sha256(predictions.dump());
    request["predicted_states"]=std::move(predictions); request["supported"]=true;
    return request;
}

Limits limits(const Json& config) {
    Limits b;
    b.oracle_nodes=bounded_size(config,"oracle_nodes",b.oracle_nodes,1,10000000);
    b.beam_width=bounded_size(config,"beam_width",b.beam_width,1,100000);
    b.max_actions=bounded_size(config,"max_actions",b.max_actions,1,100000);
    b.population=bounded_size(config,"population",b.population,2,10000);
    b.archive=bounded_size(config,"archive_limit",b.archive,1,10000);
    b.mutation_attempts=bounded_size(config,"mutation_attempts",b.mutation_attempts,1,10000);
    b.max_blocked=bounded_size(config,"max_blocked",b.max_blocked,0,1000);
    b.minimization_trials=bounded_size(config,"minimization_trials",b.minimization_trials,0,10000);
    b.memory_mb=bounded_size(config,"memory_mb",b.memory_mb,16,24576);
    b.minimum_regret=config.value("minimum_regret",b.minimum_regret);
    b.baseline=config.value("baseline",b.baseline);
    if (!std::isfinite(b.minimum_regret) || b.minimum_regret<0)
        throw std::runtime_error("C8 minimum_regret must be finite and nonnegative");
    if (b.baseline!="default" && b.baseline!="basic_first" && b.baseline!="walk_first")
        throw std::runtime_error("C8 baseline must be default, basic_first or walk_first");
    return b;
}

Json validate(const Json& scenario) {
    if (!response_policy(opponent(scenario))) throw std::runtime_error("C8 unsupported opponent_policy");
    const auto reference=scenario.value("reference_policy","default");
    if (reference!="default" && reference!="basic_first" && reference!="walk_first")
        throw std::runtime_error("C8 unsupported reference_policy");
    const auto width=number(scenario,"width"), height=number(scenario,"height");
    if (width<3 || height<3 || width>64 || height>64)
        throw std::runtime_error("C8 grids must be 3..64 tiles per axis");
    auto walls=blocked(scenario);
    if (scenario.contains("blocked") && walls.size()!=scenario.at("blocked").size())
        throw std::runtime_error("C8 duplicate blocked tiles");
    for (const auto& p : walls) {
        if (p.first<0 || p.second<0 || p.first>=width || p.second>=height || occupied(scenario,p.first,p.second))
            throw std::runtime_error("C8 obstacle overlaps an object/spawn or lies outside the grid");
    }
    if (!terrain_connected(scenario)) throw std::runtime_error("C8 disconnected walkable terrain");
    if (!scenario.contains("actors") || scenario.at("actors").empty()
        || !scenario.contains("camps") || scenario.at("camps").empty())
        throw std::runtime_error("C8 camp curricula require actors and camps");
    if (scenario.at("actors").size()>4 || scenario.at("camps").size()>32)
        throw std::runtime_error("C8 curriculum domain supports at most four actors and 32 camp members");
    const auto& camps=scenario.at("camps").as_array();
    std::map<int,std::vector<std::size_t>> groups;
    for (std::size_t i=0;i<camps.size();++i) {
        groups[group(camps[i])].push_back(i);
        for (std::size_t j=0;j<i;++j) {
            const int manhattan=std::abs(x(camps[i])-x(camps[j]))+std::abs(y(camps[i])-y(camps[j]));
            if (manhattan==1 && group(camps[i])!=group(camps[j]))
                throw std::runtime_error("C8 cardinally touching camp members must share a group");
        }
    }
    for (const auto& [unused,members] : groups) {
        (void)unused;
        std::set<std::size_t> seen{members.front()};
        for (bool changed=true;changed;) {
            changed=false;
            for (const auto i : members) if (!seen.count(i))
                for (const auto j : seen)
                    if (std::abs(x(camps[i])-x(camps[j]))+std::abs(y(camps[i])-y(camps[j]))==1) {
                        seen.insert(i); changed=true; break;
                    }
        }
        if (seen.size()!=members.size()) throw std::runtime_error("C8 a camp group is not cardinally connected");
    }
    (void)sim::initial(scenario);
    return scenario;
}

std::string descriptor(const Json& s) {
    const auto& a=s.at("actors").at(0);
    double partial=0;
    for (const auto& c:s.at("camps").as_array())
        partial+=c.value("hp",0.0)/std::max(1.0,c.value("max_hp",c.value("hp",1.0)));
    const int hpbin=std::clamp(static_cast<int>(a.value("hp",1.0)/std::max(1.0,a.value("max_hp",1.0))*5),0,5);
    const int campbin=static_cast<int>(partial/std::max<std::size_t>(1,s.at("camps").size())*5);
    int ready=0;
    if (a.contains("spells")) for (const auto& spell:a.at("spells").as_array())
        ready+=spell.value("cooldown",0)==0 && spell.value("kind","")!="none";
    int nearest=1000;
    for (const auto& c:s.at("camps").as_array()) nearest=std::min(nearest,distance(a,c));
    return std::to_string(hpbin)+":"+std::to_string(campbin)+":"+std::to_string(ready)+":"
        +std::to_string(nearest/2)+":"+std::to_string(blocked(s).size()/3)+":"
        +std::to_string(s.at("actors").size())+":"+opponent(s)+":"+family(s);
}

Json mutate(const Json& parent,std::mt19937_64& rng,const Limits& bounds) {
    static constexpr std::array<double,12> fractions{.05,.1,.15,.2,.3,.4,.5,.6,.7,.8,.9,1.0};
    for (std::size_t attempt=0;attempt<bounds.mutation_attempts;++attempt) {
        auto s=parent;
        const auto choice=rng()%12;
        if (choice==0) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            actor["hp"]=std::max(1.0,std::floor(actor.value("max_hp",actor.value("hp",1.0))*fractions[rng()%fractions.size()]));
        } else if (choice==1) {
            auto& camp=s["camps"].at(static_cast<std::size_t>(rng()%s.at("camps").size()));
            camp["hp"]=std::max(1.0,std::floor(camp.value("max_hp",camp.value("hp",1.0))*fractions[rng()%fractions.size()]));
        } else if (choice==2) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            if (!actor.contains("spells") || actor.at("spells").empty()) continue;
            auto& spell=actor["spells"].at(static_cast<std::size_t>(rng()%actor.at("spells").size()));
            const int maximum=std::max(0,spell.value("max_cooldown",8));
            spell["cooldown"]=static_cast<int>(rng()%(static_cast<std::uint64_t>(maximum)+1));
        } else if (choice==3) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            const int px=static_cast<int>(rng()%number(s,"width")), py=static_cast<int>(rng()%number(s,"height"));
            if (occupied(s,px,py) || blocked(s).count({px,py})) continue;
            actor["x"]=px; actor["y"]=py;
        } else if (choice==4) {
            auto& terrain=s["blocked"];
            if (terrain.is_null()) terrain=Json::array();
            if (!terrain.empty() && (rng()%3==0 || terrain.size()>=bounds.max_blocked))
                terrain.as_array().erase(terrain.as_array().begin()+static_cast<std::ptrdiff_t>(rng()%terrain.size()));
            else {
                if (terrain.size()>=bounds.max_blocked) continue;
                const int px=static_cast<int>(rng()%number(s,"width")), py=static_cast<int>(rng()%number(s,"height"));
                if (occupied(s,px,py) || blocked(s).count({px,py})) continue;
                terrain.push_back(Json::array({px,py}));
            }
        } else if (choice==5) {
            auto& camps=s["camps"].as_array();
            const int picked=group(camps[static_cast<std::size_t>(rng()%camps.size())]);
            const int dx=static_cast<int>(rng()%3)-1,dy=static_cast<int>(rng()%3)-1;
            if (dx==0 && dy==0) continue;
            for (auto& c:camps) if (group(c)==picked) { c["x"]=x(c)+dx; c["y"]=y(c)+dy; }
        } else if (choice==6) {
            bool contested=false;
            for (const auto& actor:s.at("actors").as_array()) contested|=actor.value("team",0)!=team(s);
            if (!contested) continue;
            static constexpr const char* policies[]{"default","basic_first","pressure","cautious"};
            s["opponent_policy"]=policies[rng()%4];
        } else if (choice==7) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            actor["gems"]=static_cast<int>(rng()%128);
        } else if (choice==8) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            actor["stun"]=static_cast<int>(rng()%3);
        } else if (choice==9) {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            actor["shield"]=std::floor(actor.value("max_hp",actor.value("hp",1.0))*.1*(rng()%4));
            actor["shield_ticks"]=static_cast<int>(rng()%6);
        } else if (choice==10) {
            auto& camp=s["camps"].at(static_cast<std::size_t>(rng()%s.at("camps").size()));
            camp["stun"]=static_cast<int>(rng()%3);
        } else {
            auto& actor=s["actors"].at(static_cast<std::size_t>(rng()%s.at("actors").size()));
            actor["priority"]=(rng()%2)?0:100;
        }
        try {
            validate(s);
            if (s!=parent) return s;
        } catch (const std::exception&) { }
    }
    throw std::runtime_error("C8 no distinct valid mutation within mutation_attempts");
}

Json evaluate_case(const Json& scenario,const Limits& bounds,const std::function<bool()>& stop) {
    validate(scenario);
    auto reference=baseline(scenario,bounds,stop);
    if (reference.value("interrupted",false)) return Json{{"interrupted",true}};
    auto alternative=oracle(scenario,bounds,reference,stop);
    if (alternative.value("interrupted",false)) return Json{{"interrupted",true}};
    const double regret=std::max(0.0,alternative.at("utility").get<double>()-reference.at("utility").get<double>());
    const bool feasible=alternative.value("status","")=="success";
    const bool reference_complete=!reference.value("limited",true);
    const bool witness=feasible && reference_complete && regret>=bounds.minimum_regret && regret>0;
    return Json{{"schema","contenders-curriculum-case/v1"}, {"scenario",scenario},
        {"scenario_sha256",sha256(scenario.dump())}, {"model_version",sim::version},
        {"provenance","constructed local-model case; natural reachability is not established"},
        {"baseline_policy",scenario.value("reference_policy",bounds.baseline)}, {"baseline",std::move(reference)},
        {"oracle",std::move(alternative)}, {"heuristic_model_regret",regret},
        {"witness",witness}, {"feasible_alternative",feasible},{"reference_completed",reference_complete},
        {"descriptor",descriptor(scenario)},
        {"family",family(scenario)},{"opponent_policy",opponent(scenario)},
        {"opponent_assumption","the same finite reactive opponent program controls all opposing inputs in both comparisons and every oracle completion rollout"},
        {"objective","sim.utility: success 1000000 - 100*elapsed_rounds + controlled_team_hp; failure -1000000 + controlled_team_hp; active 100*score + 10*carried_gems + hp - goal_camp_hp - 10*elapsed_rounds"},
        {"role","adaptive_training_curriculum"},
        {"native_validation",Json{{"status","not_requested"},{"native_validated",false}}},
        {"claim","bounded-model controller comparison; not native strength or an optimum"}};
}

Json minimize(const Json& evaluation,const Json& original_seed,const Limits& bounds,
              const std::function<bool()>& stop) {
    if (!evaluation.value("witness",false) || bounds.minimization_trials==0) return evaluation;
    auto best=evaluation;
    std::size_t trials=0,accepted=0;
    while (trials<bounds.minimization_trials && !stopped(stop)) {
        std::vector<Json> proposals;
        const auto& start=best.at("scenario");
        if (start.contains("blocked")) for (std::size_t i=0;i<start.at("blocked").size();++i) {
            auto p=start; p["blocked"].as_array().erase(p["blocked"].as_array().begin()+static_cast<std::ptrdiff_t>(i));
            proposals.push_back(std::move(p));
        }
        for (const auto* kind : {"actors","camps"}) {
            if (!original_seed.contains(kind) || original_seed.at(kind).size()!=start.at(kind).size()) continue;
            for (std::size_t i=0;i<start.at(kind).size();++i) {
                for (const auto* field : {"hp","x","y","spells"}) {
                    if (!original_seed.at(kind).at(i).contains(field)
                        || start.at(kind).at(i).at(field)==original_seed.at(kind).at(i).at(field)) continue;
                    auto p=start; p[kind].at(i)[field]=original_seed.at(kind).at(i).at(field);
                    proposals.push_back(std::move(p));
                }
            }
        }
        bool simplified=false;
        for (auto& p:proposals) {
            if (trials>=bounds.minimization_trials || stopped(stop)) break;
            ++trials;
            try {
                auto trial=evaluate_case(p,bounds,stop);
                if (trial.value("interrupted",false)) break;
                if (trial.value("witness",false)) { best=std::move(trial); ++accepted; simplified=true; break; }
            } catch (const std::exception&) { }
        }
        if (!simplified) break;
    }
    best["minimization"]=Json{{"trials",trials},{"accepted",accepted},
        {"kind","bounded simplification; not a globally minimal counterexample"},
        {"original_sha256",evaluation.at("scenario_sha256")}};
    return best;
}

Json default_case(const Json& catalog) {
    const auto ryth=find_design(catalog,"champions","ryth");
    const auto wolf=find_design(catalog,"camps","wolf");
    const auto gold=find_design(catalog,"camps","gold_cluster");
    auto actor=Json{{"id","ryth-0"},{"team",0},{"x",2},{"y",2},{"spawn",Json::array({0,0})},
        {"hp",stat(ryth,"base_health",1200)},{"max_hp",stat(ryth,"base_health",1200)},
        {"strength",stat(ryth,"base_strength",45)},{"focus",stat(ryth,"base_focus",100)},
        {"range",static_cast<int>(stat(ryth,"base_attack_range",1))},{"ranged",false},
        {"spells",Json::array({Json{{"kind","stun"},{"range",4},{"radius",1},
             {"damage",stat(ryth,"base_focus",100)},{"duration",1},{"cooldown",0},{"max_cooldown",8},{"exclusive",true}},
            Json{{"kind","reset"},{"cooldown",0},{"max_cooldown",8},{"exclusive",false}}})}};
    auto scenario=Json{{"id","ryth-cast-order"},{"width",7},{"height",7},
        {"blocked",Json::array()},{"altars",Json::array({Json::array({0,1})})},
        {"actors",Json::array({actor})},
        {"camps",Json::array({Json{{"id","gold-0"},{"group",0},{"x",3},{"y",2},
            {"hp",30},{"max_hp",stat(gold,"base_health",120)},
            {"strength",stat(gold,"base_strength",0)},{"range",3},{"gems",0},{"gold",20},{"respawn",50}},
          Json{{"id","wolf-1"},{"group",0},{"x",3},{"y",3},{"hp",stat(wolf,"base_health",1350)},
            {"max_hp",stat(wolf,"base_health",1350)},{"strength",stat(wolf,"base_strength",20)},
            {"range",static_cast<int>(stat(wolf,"base_attack_range",3))},{"gems",4},{"gold",40},{"respawn",40}}})},
        {"settings",Json{{"capture_time",5},{"camp_sleep_time",2},{"recall_time",5},
            {"spawn_healing",1.0},{"submit_multiplier",.1},{"submit_minimum",1},
            {"winning_gems",1000},{"max_rounds",90}}},
        {"goal",Json{{"kind","capture_depart"},{"team",0},{"group",0}}},
        {"provenance",Json{{"kind","constructed"},{"reference","foundation micro U1-U4 cast/basic headroom"},
            {"unit_stats_from_catalog",!ryth.is_null()},{"native_validated",false}}}};
    if (!ryth.is_null() && !wolf.is_null() && !gold.is_null()) {
        // Read the entire supported kit/settings from the queried catalog. The
        // scalar fallback above remains an explicit no-catalog fixture only.
        auto specification=scenario;
        auto& unit=specification["actors"].at(0);
        unit["design"]="ryth";
        for (const auto* field : {"strength","focus","range","ranged","spells"})
            unit.as_object().erase(field);
        specification["camps"].at(0)["design"]="gold_cluster";
        specification["camps"].at(1)["design"]="wolf";
        specification["settings"]=Json{{"max_rounds",90}};
        scenario=sim::expand_case(specification,catalog);
        scenario["provenance"]["reference"]="foundation micro U1-U4 cast/basic headroom";
    }
    return validate(scenario);
}
} // namespace opt::scenarios

namespace opt {
namespace {
fs::path c8_reference_path(const Options& opts,const std::string& name) {
    fs::path value=name;
    if (value.is_absolute()) return value;
    for (const auto& root : {opts.input.parent_path(),opts.repo,fs::current_path()})
        if (!root.empty() && fs::exists(root/value)) return fs::absolute(root/value);
    return fs::absolute((opts.repo.empty()?fs::current_path():opts.repo)/value);
}
Json c8_seed_row(const Json& row) {
    return row.is_object() && row.contains("scenario") ? row.at("scenario") : row;
}
Json c8_load_seeds(const Options& opts,const Json& config,Json& pins) {
    Json seeds=Json::array(),catalog;
    const auto catalog_name=config.value("catalog","docs/reference/runtime-catalog.json");
    const auto catalog_path=c8_reference_path(opts,catalog_name);
    if (fs::exists(catalog_path)) {
        catalog=read_json(catalog_path);
        pins["catalog_sha256"]=file_sha256(catalog_path);
    } else if (config.contains("catalog")) throw std::runtime_error("C8 catalog file does not exist");
    if (config.contains("cases")) for (const auto& row:config.at("cases").as_array())
        seeds.push_back(c8_seed_row(row));
    if (config.contains("cases_file")) {
        const auto path=c8_reference_path(opts,config.at("cases_file").get<std::string>());
        const auto text=read_text(path);
        pins["cases_file_sha256"]=sha256(text);
        if (path.extension()==".jsonl") {
            std::istringstream stream(text); std::string line;
            while (std::getline(stream,line)) if (line.find_first_not_of(" \t\r\n")!=std::string::npos)
                seeds.push_back(c8_seed_row(Json::parse(line)));
        } else {
            auto body=Json::parse(text);
            if (body.is_object() && body.contains("cases")) body=body.at("cases");
            if (!body.is_array()) throw std::runtime_error("C8 cases_file must contain an array or JSONL");
            for (const auto& row:body.as_array()) seeds.push_back(c8_seed_row(row));
        }
    }
    if (seeds.empty()) {
        if (config.contains("cases") || config.contains("cases_file"))
            throw std::runtime_error("C8 explicit case family is empty");
        seeds.push_back(scenarios::default_case(catalog));
    }
    for (auto& scenario:seeds.as_array()) {
        bool design_tagged=false;
        for (const auto* kind : {"actors","camps"}) if (scenario.contains(kind))
            for (const auto& row:scenario.at(kind).as_array()) design_tagged|=row.contains("design");
        if (design_tagged) {
            if (catalog.is_null()) throw std::runtime_error("C8 design-tagged cases require a runtime catalog");
            scenario=sim::expand_case(scenario,catalog);
        }
        scenario=scenarios::validate(scenario);
    }
    pins["seed_cases_sha256"]=sha256(seeds.dump());
    pins["model_version"]=sim::version;
    pins["baseline_policy"]=config.value("baseline","default");
    return seeds;
}
double c8_fitness(const Json& row) {
    return row.value("witness",false) ? row.value("heuristic_model_regret",0.0) : 0.0;
}
void c8_sort_population(Json& population,std::size_t cap) {
    auto& rows=population.as_array();
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b) {
        if (c8_fitness(a)!=c8_fitness(b)) return c8_fitness(a)>c8_fitness(b);
        return a.at("scenario_sha256").template get<std::string>()<b.at("scenario_sha256").template get<std::string>();
    });
    // Retain the best member of each descriptor before filling with remaining elites.
    Json selected=Json::array();
    std::set<std::string> descriptions,ids;
    for (const auto& row:rows) if (selected.size()<cap && descriptions.insert(row.at("descriptor").get<std::string>()).second) {
        selected.push_back(row); ids.insert(row.at("scenario_sha256").get<std::string>());
    }
    for (const auto& row:rows) if (selected.size()<cap && ids.insert(row.at("scenario_sha256").get<std::string>()).second)
        selected.push_back(row);
    population=std::move(selected);
}
Json c8_compact(const Json& evaluation,std::size_t seed_index) {
    return Json{{"scenario",evaluation.at("scenario")},{"scenario_sha256",evaluation.at("scenario_sha256")},
        {"descriptor",evaluation.at("descriptor")},{"witness",evaluation.at("witness")},
        {"heuristic_model_regret",evaluation.at("heuristic_model_regret")},{"seed_index",seed_index},
        {"family",evaluation.at("family")},{"evaluation_identity",evaluation.at("evaluation_identity")},
        {"reference_completed",evaluation.at("reference_completed")},
        {"feasible_alternative",evaluation.at("feasible_alternative")},
        {"baseline_status",evaluation.at("baseline").at("status")},
        {"alternative_status",evaluation.at("oracle").at("status")},
        {"oracle_capped",evaluation.at("oracle").value("search_capped",false)}};
}
Json c8_validate_trace(const Options& opts,const Json& evaluation,const std::string& which,
                       const fs::path& directory) {
    if (!opts.has("validator")) return Json{{"status","not_requested"},{"native_validated",false}};
    const auto call=directory/(which+"-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    fs::create_directories(call);
    const auto request=scenarios::validation_request(evaluation,which);
    if (!request.value("supported",false)) return Json{{"status","trace_export_limit"},{"native_validated",false}};
    const auto scenario_hash=request.at("scenario_sha256").get<std::string>();
    const auto trace_hash=request.at("trace_sha256").get<std::string>();
    const auto request_path=fs::absolute(call/"request.json");
    const auto response_path=fs::absolute(call/"response.json");
    write_json(request_path,request);
    std::vector<std::string> command{opts.get("validator")};
    for (const auto& a:opts.values("validator-arg")) command.push_back(a);
    command.insert(command.end(),{"--request",request_path.string(),"--response",response_path.string(),
        "--repo",opts.repo.string()});
    const auto process=run_process(command,opts.repo,call/"stdout.log",
        call/"stderr.log",opts.real("validator-timeout",120));
    if (process.exit_code!=0 || process.timed_out || !fs::exists(response_path))
        return Json{{"status","validator_failed"},{"native_validated",false},{"exit_code",process.exit_code},
            {"timed_out",process.timed_out}};
    auto response=read_json(response_path);
    const bool bound=response.value("scenario_sha256","")==scenario_hash
        && response.value("trace_sha256","")==trace_hash
        && response.value("prediction_sha256","")==request.at("prediction_sha256").get<std::string>();
    const bool complete=response.value("native",false) && response.value("passed",false)
        && response.value("scope","")=="entire-trace" && bound
        && response.value("comparison_projection","")=="restricted-model-after-every-action";
    return Json{{"status",complete?"validated":"unsupported_or_unqualified"},
        {"native_validated",complete},{"hashes_bound",bound},{"response",response}};
}
void c8_export(const Run& run,const Json& archive,const Json& seeds) {
    std::string curriculum;
    Json cases=Json::array(),failures=Json::array();
    Json requests=Json::array(); std::size_t request_count=0;
    const auto request_limit=scenarios::bounded_size(run.config,"native_request_limit",8,0,64);
    const auto state_limit=scenarios::bounded_size(run.config,"validation_state_limit",2048,1,100000);
    for (const auto& row:archive.as_array()) {
        curriculum+=row.dump()+"\n";
        cases.push_back(Json{{"id",row.at("scenario_sha256")},{"scenario",row.at("scenario")},
            {"split","train"},{"provenance",Json{{"source","C8 adaptive model curriculum"},
                {"scenario_sha256",row.at("scenario_sha256")},{"native_validation",row.at("native_validation")}}}});
        failures.push_back(Json{{"scenario_sha256",row.at("scenario_sha256")},
            {"family",row.at("family")},{"opponent_policy",row.at("opponent_policy")},
            {"heuristic_model_regret",row.at("heuristic_model_regret")},
            {"baseline_status",row.at("baseline").at("status")},
            {"oracle_status",row.at("oracle").at("status")},
            {"native_validation",row.at("native_validation")}});
        if (request_count<request_limit) {
            const auto hash=row.at("scenario_sha256").get<std::string>();
            for (const auto* which:{"baseline","oracle"}) {
                const auto relative=fs::path("native-requests")/(hash+"-"+which+".json");
                write_json(run.options.out/relative,scenarios::validation_request(row,which,state_limit));
                requests.push_back(Json{{"scenario_sha256",hash},{"trace",which},{"request",relative.generic_string()}});
            }
            ++request_count;
        }
    }
    atomic_write(run.options.out/"curriculum.jsonl",curriculum);
    write_json(run.options.out/"curriculum.json",Json{{"cases",archive},
        {"source","C8 adaptive training curriculum"},{"model_capabilities",sim::capabilities()}});
    write_json(run.options.out/"atlas-cases.json",Json{{"cases",cases},
        {"source","C8 adversarial model curriculum; constructed cases, not untouched evaluation"},
        {"model_capabilities",sim::capabilities()}});
    Json unresolved=Json::array(),retry_cases=Json::array();
    const auto saved_population=run.state.value("population",Json::array());
    for (auto row:saved_population.as_array()) if (!row.value("witness",false)) {
        row["reason"]=!row.value("reference_completed",false)?"reference_prefix_unresolved":
            !row.value("feasible_alternative",false)?"no_feasible_alternative_found":"no_positive_regret_witness_found";
        row["claim"]="bounded search outcome; no absence-of-headroom proof";
        unresolved.push_back(row); retry_cases.push_back(row.at("scenario"));
    }
    write_json(run.options.out/"unresolved-cases.json",Json{{"schema","contenders-c8-unresolved/v1"},
        {"cases",unresolved},{"scope","bounded model cases for deeper search; not negative proofs"}});
    auto retry=run.config; retry.as_object().erase("cases_file"); retry["cases"]=retry_cases;
    retry["notes"]="Unresolved C8 population cases. An empty family means nothing remains in the retained population to retry. Increase oracle_nodes/beam_width or use an independently qualified executor; these are adaptive training cases, not held-out tests.";
    write_json(run.options.out/"unresolved-input.json",retry);
    write_json(run.options.out/"curriculum-summary.json",Json{{"schema","contenders-c8-summary/v1"},
        {"iteration",run.iteration},{"archive_cases",archive.size()},{"seed_cases",seeds.size()},
        {"unresolved_cases",unresolved.size()},
        {"coverage",run.state.value("coverage",Json::object())},{"native_requests",requests},
        {"cases",failures},{"model_capabilities",sim::capabilities()},
        {"claim","feasible model alternatives against the named reference; native strength unestablished"}});
    if (!archive.empty()) {
        const auto best=std::max_element(archive.as_array().begin(),archive.as_array().end(),
            [](const auto& a,const auto& b){return c8_fitness(a)<c8_fitness(b);});
        write_json(run.options.out/"hardest-case.json",*best);
        write_json(run.options.out/"baseline-actions.json",best->at("baseline").at("actions"));
        write_json(run.options.out/"alternative-actions.json",best->at("oracle").at("actions"));
        auto replay=run.config; replay.as_object().erase("cases_file"); replay.as_object().erase("cases");
        replay["cases"]=Json::array({best->at("scenario")}); replay["minimization_trials"]=0;
        replay["notes"]="Replay this constructed model comparison with c8 --iterations 1; exact paired action traces are retained separately.";
        write_json(run.options.out/"replay-input.json",replay);
    }
}
} // namespace

int run_c8(const Options& opts) {
    Run run(opts,"C8 adversarial scenario curriculum",std::string("3+")+sim::version);
    const auto bounds=scenarios::limits(run.config);
    (void)scenarios::bounded_size(run.config,"native_request_limit",8,0,64);
    (void)scenarios::bounded_size(run.config,"validation_state_limit",2048,1,100000);
    Json pins=Json::object();
    const auto seeds=c8_load_seeds(run.options,run.config,pins);
    Json population=Json::array(),archive=Json::array();
    std::uint64_t evaluated=0,cache_hits=0,invalid=0;
    if (run.resumed) {
        if (!run.state.contains("pins") || run.state.at("pins")!=pins)
            throw std::runtime_error("C8 cannot resume: catalog, seed cases, baseline or model identity changed");
        population=run.state.at("population"); archive=run.state.at("archive");
        evaluated=run.state.value<std::uint64_t>("evaluated",0);
        cache_hits=run.state.value<std::uint64_t>("cache_hits",0);
        invalid=run.state.value<std::uint64_t>("invalid_mutations",0);
    }
    const auto persist=[&] {
        run.state["pins"]=pins;
        run.state["population"]=population;
        run.state["archive"]=archive;
        run.state["evaluated"]=evaluated;
        run.state["cache_hits"]=cache_hits;
        run.state["invalid_mutations"]=invalid;
        run.checkpoint();
    };
    fs::create_directories(run.options.out/"evaluations");
    while (!run.stop()) {
        Json candidate;
        std::size_t seed_index=0;
        if (run.state.contains("pending") && !run.state.at("pending").is_null()) {
            candidate=run.state.at("pending").at("scenario");
            seed_index=run.state.at("pending").value<std::size_t>("seed_index",0);
        } else {
            if (run.iteration<seeds.size()) {
                seed_index=static_cast<std::size_t>(run.iteration);
                candidate=seeds.at(seed_index);
            } else {
                // Allocate attempts to the seed families in turn. Global fitness
                // tournaments otherwise starve quieter but useful failure modes.
                seed_index=static_cast<std::size_t>((run.iteration-seeds.size())%seeds.size());
                std::vector<std::size_t> eligible;
                for (std::size_t i=0;i<population.size();++i)
                    if (population.at(i).value<std::size_t>("seed_index",0)==seed_index) eligible.push_back(i);
                const bool restart=eligible.empty() || run.rng()%8==0;
                Json parent;
                if (restart) {
                    parent=seeds.at(seed_index);
                } else {
                    auto selected=eligible[static_cast<std::size_t>(run.rng()%eligible.size())];
                    for (int n=0;n<2;++n) {
                        const auto challenger=eligible[static_cast<std::size_t>(run.rng()%eligible.size())];
                        if (c8_fitness(population.at(challenger))>c8_fitness(population.at(selected))) selected=challenger;
                    }
                    parent=population.at(selected).at("scenario");
                }
                try { candidate=scenarios::mutate(parent,run.rng,bounds); }
                catch (const std::exception& e) {
                    ++invalid; run.advance();
                    run.event(Json{{"kind","mutation_rejected"},{"reason",e.what()}});
                    persist(); continue;
                }
            }
            run.state["pending"]=Json{{"scenario",candidate},{"seed_index",seed_index}};
            persist(); // crashes retry this immutable case, without drawing a new mutation
        }
        const auto cache_key=sha256(Json{{"scenario",candidate},{"pins",pins},{"search",run.config},
            {"version","C8-v3"}}.dump());
        const auto cache_path=run.options.out/"evaluations"/(cache_key+".json");
        Json evaluation;
        bool cached=false;
        if (fs::exists(cache_path)) {
            evaluation=read_json(cache_path);
            if (evaluation.value("evaluation_identity","")!=cache_key
                || evaluation.at("scenario")!=candidate)
                throw std::runtime_error("C8 evaluation cache identity mismatch");
            ++cache_hits; cached=true;
        } else {
            evaluation=scenarios::evaluate_case(candidate,bounds,[&]{return run.stop();});
            if (evaluation.value("interrupted",false)) { persist(); break; }
            evaluation["evaluation_identity"]=cache_key;
            write_json(cache_path,evaluation);
            ++evaluated;
        }
        auto compact=c8_compact(evaluation,seed_index);
        bool present=false;
        for (const auto& row:population.as_array()) if (row.at("scenario_sha256")==compact.at("scenario_sha256")) present=true;
        if (!present) population.push_back(compact);
        c8_sort_population(population,bounds.population);
        bool new_archive=false;
        if (evaluation.value("witness",false)) {
            std::size_t replace=archive.size();
            for (std::size_t i=0;i<archive.size();++i)
                if (archive.at(i).at("descriptor")==evaluation.at("descriptor")) { replace=i; break; }
            if (replace==archive.size() || c8_fitness(evaluation)>c8_fitness(archive.at(replace))) {
                auto selected=evaluation;
                auto simplified=scenarios::minimize(evaluation,seeds.at(seed_index),bounds,[&]{return run.stop();});
                if (run.stop()) { persist(); break; }
                if (simplified.at("scenario")!=evaluation.at("scenario"))
                    selected["minimized_counterexample"]=std::move(simplified);
                if (opts.has("validator") && !run.stop()) {
                    const auto validation_dir=run.options.out/"native-validation"/selected.at("scenario_sha256").get<std::string>();
                    auto a=c8_validate_trace(run.options,selected,"baseline",validation_dir);
                    if (run.stop()) { persist(); break; }
                    auto b=c8_validate_trace(run.options,selected,"oracle",validation_dir);
                    if (run.stop()) { persist(); break; }
                    selected["native_validation"]=Json{{"native_validated",a.value("native_validated",false)&&b.value("native_validated",false)},
                        {"status","both comparison traces required"},{"baseline",a},{"oracle",b}};
                }
                if (replace<archive.size()) archive.at(replace)=selected;
                else archive.push_back(selected);
                std::stable_sort(archive.as_array().begin(),archive.as_array().end(),
                    [](const auto& a,const auto& b){return c8_fitness(a)>c8_fitness(b);});
                if (archive.size()>bounds.archive) archive.as_array().resize(bounds.archive);
                new_archive=true;
            }
        }
        auto& coverage=run.state["coverage"];
        if (coverage.is_null()) coverage=Json::object();
        auto& covered=coverage[evaluation.at("family").get<std::string>()];
        if (covered.is_null()) covered=Json{{"evaluated",0},{"witnesses",0},{"reference_completed",0},{"oracle_capped",0},{"best_model_regret",0.0},{"opponent_programs",Json::object()}};
        for (const auto& flag:std::array<std::pair<const char*,bool>,4>{{{"evaluated",true},{"witnesses",evaluation.value("witness",false)},
            {"reference_completed",evaluation.value("reference_completed",false)},{"oracle_capped",evaluation.at("oracle").value("search_capped",false)}}})
            if (flag.second) covered[flag.first]=covered.value<std::uint64_t>(flag.first,0)+1;
        covered["best_model_regret"]=std::max(covered.value("best_model_regret",0.0),c8_fitness(evaluation));
        auto& programs=covered["opponent_programs"]; const auto program=evaluation.at("opponent_policy").get<std::string>();
        programs[program]=programs.value<std::uint64_t>(program,0)+1;
        run.advance(); run.state["pending"]=Json();
        persist();
        run.progress(Json{{"evaluated_cases",evaluated},{"cache_hits",cache_hits},{"archive_cases",archive.size()},
            {"best_model_regret",archive.empty()?0.0:c8_fitness(archive.at(0))},
            {"current_model_regret",c8_fitness(evaluation)},{"invalid_mutations",invalid}});
        run.event(Json{{"kind","case_evaluated"},{"evaluation_identity",cache_key},{"cached",cached},
            {"witness",evaluation.at("witness")},{"new_archive",new_archive}});
        if (new_archive || run.iteration%16==0) c8_export(run,archive,seeds);
    }
    persist(); c8_export(run,archive,seeds);
    run.finish(Json{{"engine","C8"},{"status",run.iteration>=run.options.iterations?"completed":"checkpointed"},
        {"search_iterations",run.iteration},{"evaluated_unique_cases",evaluated},{"cache_hits",cache_hits},
        {"archive_cases",archive.size()},{"best_model_regret",archive.empty()?0.0:c8_fitness(archive.at(0))},
        {"coverage",run.state.value("coverage",Json::object())},
        {"curriculum","curriculum.jsonl"},{"atlas_cases","atlas-cases.json"},
        {"unresolved_cases","unresolved-cases.json"},{"unresolved_retry_input","unresolved-input.json"},
        {"pending_case_restarts",run.state.contains("pending")&&!run.state.at("pending").is_null()},
        {"model_capabilities",sim::capabilities()},
        {"limitations",Json::array({"Constructed fully observed local cases are not proven reachable natural game states.",
            "The bounded beam/rollout oracle supplies feasible alternatives, not an optimum or global regret bound.",
            "A truncated reference prefix is unresolved and never archived as a full-task regret witness.",
            "Reactive opponent programs are finite model policies; no minimax or actual frozen-bot exploit is claimed.",
            "Only entire-trace validation of both alternatives with matching scenario, action and prediction hashes enables native-validated comparison.",
            "An interrupted case restarts from its immutable input; completed evaluations and RNG/population/archive resume."})}});
    return 0;
}
} // namespace opt
