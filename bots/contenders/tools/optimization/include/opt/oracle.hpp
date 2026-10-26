#pragma once
#include "opt/common.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace opt::oracle {
inline constexpr const char* domain = "frozen-ryth-mirror-v1";

struct Model {
    double basic_scale{1}, cast_scale{1};
    bool shield_first{true}, reset_restores_attack{true}, reset_requires_spent_attack{true};
    bool cast_consumes_attack{false}, cast_blocks_move{true}, basic_blocks_move{false};
    Json json() const {
        return Json{{"basic_scale",basic_scale},{"cast_scale",cast_scale},{"shield_first",shield_first},
            {"reset_restores_attack",reset_restores_attack},{"reset_requires_spent_attack",reset_requires_spent_attack},
            {"cast_consumes_attack",cast_consumes_attack},
            {"cast_blocks_move",cast_blocks_move},{"basic_blocks_move",basic_blocks_move}};
    }
    static Model from(const Json& j) {
        Model m;
        m.basic_scale=j.value("basic_scale",1.0); m.cast_scale=j.value("cast_scale",1.0);
        m.shield_first=j.value("shield_first",true);
        m.reset_restores_attack=j.value("reset_restores_attack",true);
        m.reset_requires_spent_attack=j.value("reset_requires_spent_attack",true);
        m.cast_consumes_attack=j.value("cast_consumes_attack",false);
        m.cast_blocks_move=j.value("cast_blocks_move",true);
        m.basic_blocks_move=j.value("basic_blocks_move",false);
        if (!std::isfinite(m.basic_scale) || !std::isfinite(m.cast_scale) ||
            m.basic_scale<0 || m.cast_scale<0 || m.basic_scale>10 || m.cast_scale>10)
            throw std::runtime_error("C11 model scales must be finite in [0,10]");
        return m;
    }
};

inline int resolve(const Json& action, const std::string& field, int fallback) {
    if (!action.contains(field)) return fallback;
    const auto& v=action[field];
    if (v.is_number_integer()) return v.get<int>();
    const auto s=v.get<std::string>();
    if (s=="actor") return 7;
    if (s=="target") return 2;
    throw std::runtime_error("Unknown oracle actor alias");
}
inline const Json& actor(const Json& state, int id) {
    for (const auto& a:state.at("actors").as_array())
        if (a.at("id").get<int>()==id) return a;
    throw std::runtime_error("Oracle projection lacks requested actor");
}
inline int distance(const Json& a,const Json& b) {
    return std::max(std::abs(a.at("x").get<int>()-b.at("x").get<int>()),
                    std::abs(a.at("y").get<int>()-b.at("y").get<int>()));
}
inline void apply_damage(Json& target,float damage,bool shield_first) {
    float shield=target.at("shield").get<float>(), hp=target.at("hp").get<float>();
    if (shield_first) {
        const float absorbed=std::min(shield,damage);
        shield=shield-absorbed; damage=damage-absorbed;
    }
    target["shield"]=shield;
    target["hp"]=std::max(0.0f,hp-damage);
    target["alive"]=target.at("hp").get<float>()>0;
}

// This is an intentionally restricted, immediate projection model. It neither
// predicts hidden world turns nor supplies a replacement for the complete sim.
inline Json compare_transition(const Json& transition,const Model& model,double tolerance=1e-5) {
    const auto& before=transition.at("before");
    const auto& after=transition.at("after");
    const auto& action=transition.at("action");
    const auto type=action.value("type",action.value("kind",""));
    if (type=="next_owner")
        return Json{{"classification","observed_boundary"},{"compared",false},{"loss",0.0},
            {"reason","Other world actors and owner-boundary state are observed, not predicted by this projection"}};
    if (type=="move")
        return Json{{"classification","insufficient_observation"},{"compared",false},{"loss",0.0},
            {"reason","Two-actor projection omits terrain and other occupants"}};
    if (type!="basic" && type!="cast" && type!="reset")
        return Json{{"classification","insufficient_observation"},{"compared",false},{"loss",0.0},
            {"reason","Action is outside the declared projection domain"}};
    const int owner=before.at("owner").get<int>();
    const int target_id=resolve(action,"target",2);
    Json a=actor(before,owner), target=actor(before,target_id);
    const bool alive=a.value("alive",false), stunned=a.value("stunned",false);
    bool legal=alive && !stunned;
    bool target_effect=false;
    int slot=type=="reset"?1:action.value("slot",0);
    if (type=="basic") {
        legal=legal && a.at("attack").get<bool>() && target.at("alive").get<bool>() &&
            target.at("team").get<int>()!=a.at("team").get<int>() &&
            distance(a,target)<=a.at("range").get<int>();
        if (legal) {
            a["attack"]=false;
            if (model.basic_blocks_move) a["move"]=false;
            apply_damage(target,static_cast<float>(a.at("strength").get<float>()*model.basic_scale),model.shield_first);
            target_effect=true;
        }
    } else {
        if (slot<0 || slot>1) legal=false;
        legal=legal && a.at("cast").get<bool>() && slot>=0 && slot<2 &&
            a.at("cooldowns").at(static_cast<std::size_t>(std::max(0,slot))).get<int>()==0;
        if (slot==0) {
            const auto& ability=a.at("abilities").at(0);
            const auto& payload=ability.at("payload");
            legal=legal && target.at("alive").get<bool>() &&
                distance(a,target)<=static_cast<int>(payload.at("range").get<double>());
            if (legal) {
                apply_damage(target,static_cast<float>(a.at("focus").get<float>()*
                    payload.at("modifier").get<float>()*model.cast_scale),model.shield_first);
                target_effect=true;
                if (model.cast_blocks_move) a["move"]=false;
                if (model.cast_consumes_attack) a["attack"]=false;
            }
        } else if (slot==1) {
            legal=legal && (!model.reset_requires_spent_attack || !a.at("attack").get<bool>());
            if (legal && model.reset_restores_attack) a["attack"]=true;
        }
        if (legal) {
            a["cast"]=false;
            a["cooldowns"][static_cast<std::size_t>(slot)]=
                a.at("abilities").at(static_cast<std::size_t>(slot)).at("maxCooldown");
        }
    }
    const bool observed=transition.at("applied").get<bool>();
    if (legal!=observed)
        return Json{{"classification","prediction_error"},{"compared",true},{"loss",1.0},
            {"reason","Predicted legality disagrees with the native action result"},{"predicted_legal",legal},
            {"native_applied",observed}};
    if (!observed)
        return Json{{"classification","invalid_action_generation"},{"compared",true},{"loss",0.0},
            {"reason","Generated request was refused by native engine and model"}};
    Json differences=Json::array();
    double loss=0;
    const auto compare=[&](const Json& predicted,const Json& actual,const std::string& field) {
        if (predicted.at(field).is_boolean()) {
            if (predicted.at(field)!=actual.at(field)) {
                loss+=1;
                differences.push_back(Json{{"field",field},{"predicted",predicted.at(field)},{"native",actual.at(field)}});
            }
        } else {
            const double error=std::abs(predicted.at(field).get<double>()-actual.at(field).get<double>());
            if (error>tolerance) {
                loss+=error/std::max(1.0,std::abs(actual.at(field).get<double>()));
                differences.push_back(Json{{"field",field},{"predicted",predicted.at(field)},{"native",actual.at(field)},{"absolute_error",error}});
            }
        }
    };
    const auto& actual_a=actor(after,owner);
    for (const auto& field:{"move","attack","cast"}) compare(a,actual_a,field);
    if (type!="basic" && a.at("cooldowns")!=actual_a.at("cooldowns")) {
        loss+=1; differences.push_back(Json{{"field","cooldowns"},{"predicted",a.at("cooldowns")},{"native",actual_a.at("cooldowns")}});
    }
    if (target_effect) {
        const auto& actual_target=actor(after,target_id);
        for (const auto& field:{"hp","shield","alive"}) compare(target,actual_target,field);
    }
    return Json{{"classification",loss>0?"prediction_error":"transition_match"},{"compared",true},
        {"loss",loss},{"differences",differences},{"projection","HP/shield/life and current owner allowances/cooldowns"}};
}
inline Json compare_case(const Json& item,const Model& model,double tolerance=1e-5) {
    if (!item.value("supported",false))
        return Json{{"classification","insufficient_observation"},{"loss",0.0},{"compared",0},
            {"reason",item.value("failure","Unsupported native setup")}};
    Json transitions=Json::array();
    int compared=0, mismatches=0; double loss=0;
    for (const auto& transition:item.at("transitions").as_array()) {
        auto result=compare_transition(transition,model,tolerance);
        if (result.at("compared").get<bool>()) {
            ++compared; loss+=result.at("loss").get<double>();
            if (result.at("classification").get<std::string>()=="prediction_error") ++mismatches;
        }
        transitions.push_back(std::move(result));
    }
    return Json{{"classification",mismatches?"prediction_error":compared?"transition_match":"insufficient_observation"},
        {"loss",compared?loss/compared:0.0},{"compared",compared},{"mismatches",mismatches},
        {"transitions",std::move(transitions)}};
}
// The fixture deliberately covers one immediate command only. A two-body local
// state cannot reproduce the intervening actors/world of the retained replay.
inline Json simulator_fixture(const Json& transition) {
    const auto& action=transition.at("action");
    const auto type=action.value("type","");
    if (type!="basic" && type!="cast" && type!="reset") return Json{};
    const auto& before=transition.at("before");
    Json actors=Json::array();
    int active=-1;
    for (std::size_t index=0;index<before.at("actors").size();++index) {
        const auto& a=before.at("actors").at(index);
        if (a.at("id").get<int>()==before.at("owner").get<int>()) active=static_cast<int>(index);
        Json spells=Json::array();
        for (std::size_t slot=0;slot<a.at("abilities").size();++slot) {
            const auto& ability=a.at("abilities").at(slot);
            if (slot==0) spells.push_back(Json{{"kind","stun"},
                {"range",ability.at("payload").at("range")},{"radius",ability.at("payload").at("radius")},
                {"damage",static_cast<float>(a.at("focus").get<float>()*ability.at("payload").at("modifier").get<float>())},
                {"duration",ability.at("payload").at("duration")},{"exclusive",true},
                {"cooldown",a.at("cooldowns").at(slot)},{"max_cooldown",ability.at("maxCooldown")}});
            else spells.push_back(Json{{"kind","reset"},{"exclusive",false},{"cooldown",a.at("cooldowns").at(slot)},
                {"max_cooldown",ability.at("maxCooldown")}});
        }
        actors.push_back(Json{{"id","native-"+std::to_string(a.at("id").get<int>())},{"team",a.at("team")},{"x",a.at("x")},{"y",a.at("y")},
            {"spawn",Json::array({0,0})},{"hp",a.at("hp")},{"max_hp",a.at("max_hp")},{"strength",a.at("strength")},
            {"focus",a.at("focus")},{"range",a.at("range")},{"ranged",false},{"shield",a.at("shield")},
            {"shield_ticks",2},{"stun",a.at("stun_timer").is_null()?0:a.at("stun_timer").get<int>()},
            {"can_move",a.at("move")},{"can_attack",a.at("attack")},{"can_cast",a.at("cast")},
            {"completed",a.at("completed")},{"spells",spells}});
    }
    if (active<0) throw std::runtime_error("Native fixture owner missing");
    const int target_id=resolve(action,"target",2);
    int target=target_id==7?0:1;
    Json local_action;
    if (type=="basic") local_action=Json{{"type","basic"},{"target_kind","actor"},{"target",target}};
    else if (type=="reset" || action.value("slot",0)==1) local_action=Json{{"type","cast"},{"slot",1}};
    else {
        const auto& recipient=actor(before,target_id);
        local_action=Json{{"type","cast"},{"slot",0},{"x",action.value("x",recipient.at("x").get<int>())},
            {"y",action.value("y",recipient.at("y").get<int>())}};
    }
    return Json{{"scenario",Json{{"width",50},{"height",40},{"round",before.at("round")},
            {"actors",actors},{"active_actor",active},{"camps",Json::array()},
            {"goal",Json{{"kind","survive"},{"team",1},{"rounds",1}}},
            {"provenance",Json{{"state_origin","native-immediate-projection"},
                {"native_qualification","one command projection only; full world omitted"}}}}},
        {"action",local_action},{"native_action_applied",transition.at("applied")},
        {"expected_native_projection",transition.at("after")},
        {"scope","Immediate pair HP/shield/allowances only; not native whole-trace certification"}};
}
Json invoke(const Options& options,const Json& request,const fs::path& directory);
} // namespace opt::oracle
