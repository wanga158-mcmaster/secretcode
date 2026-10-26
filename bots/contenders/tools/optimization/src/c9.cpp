#include "opt/endgame.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>

namespace opt::endgame {
namespace {
int distance(Position a, Position b) { return std::max(std::abs(a.x-b.x), std::abs(a.y-b.y)); }
int integer(const Json& j, const std::string& key, int fallback, int low, int high) {
    if (!j.contains(key)) return fallback;
    const auto& v=j.at(key);
    if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() != std::floor(v.get<double>())
        || v.get<double>() < low || v.get<double>() > high)
        throw std::runtime_error("C9 field '"+key+"' must be an integer in the declared domain");
    return v.get<int>();
}
Position position(const Json& j) {
    if (!j.is_array() || j.size()!=2) throw std::runtime_error("C9 positions must be [x,y]");
    Json o{{"x",j.at(0)},{"y",j.at(1)}};
    return {integer(o,"x",0,0,127),integer(o,"y",0,0,127)};
}
Json pos_json(Position p) { return Json::array({p.x,p.y}); }
void clear_channel(Actor& a) { a.channel_altar=-1; a.channel_timer=-1; }
void consume_basic(Actor& a) { a.can_attack=false; if (a.ranged) a.can_move=false; }
void hurt(const Domain& d, State& s, int victim, int damage, int killer) {
    auto& a=s.actors[victim];
    int absorbed=std::min(a.shield,damage); a.shield-=absorbed; damage-=absorbed;
    a.hp=std::max(0,a.hp-damage);
    if (a.alive()) return;
    clear_channel(a); a.stun=-1; a.respawn=d.settings.respawn_time;
    s.actors[killer].gems+=a.gems; a.gems=0;
    s.actors[killer].gold+=d.settings.kill_gold; s.earned_gold[killer]+=d.settings.kill_gold;
    ++s.kills[killer];
}
bool in_altar_range(const Domain& d, const Actor& a, int altar, int range) {
    return altar>=0 && altar<static_cast<int>(d.altars.size()) && distance(a.pos,d.altars[altar])<=range;
}
}

bool Domain::walkable(Position p) const {
    if (p.x<0 || p.y<0 || p.x>=width || p.y>=height) return false;
    if (terrain[static_cast<std::size_t>(p.y)][static_cast<std::size_t>(p.x)]!='.') return false;
    return std::find(altars.begin(),altars.end(),p)==altars.end();
}
bool terminal(const Domain& d,const State& s) {
    return s.score[0]>=d.settings.winning_gems || s.score[1]>=d.settings.winning_gems || s.round>d.settings.max_rounds;
}
int terminal_payoff(const Domain& d,const State& s) {
    if (!terminal(d,s)) throw std::runtime_error("C9 terminal payoff requested for nonterminal state");
    if (s.score[0]>=d.settings.winning_gems) return 1;
    if (s.score[1]>=d.settings.winning_gems) return -1;
    const auto a=std::tie(s.score[0],s.earned_gold[0],s.kills[0]);
    const auto b=std::tie(s.score[1],s.earned_gold[1],s.kills[1]);
    return a>b ? 1 : -1; // Exact tie is Team B.
}

void owner_start(const Domain& d,State& s) {
    if (terminal(d,s)) return;
    auto& a=s.actors[s.current_team()];
    if (!a.alive() && a.respawn==0) {
        const auto& enemy=s.actors[1-s.current_team()];
        if (!enemy.alive() || !(enemy.pos==a.spawn)) { a.pos=a.spawn; a.hp=a.max_hp; a.respawn=-1; a.cooldown={0,0}; }
    }
    if (a.stun==0) a.stun=-1;
    if (a.shield_timer==0) { a.shield=0; a.shield_timer=-1; }
    if (a.alive() && a.channel_altar>=0 && a.channel_timer==0) {
        const int altar=a.channel_altar;
        if (s.altar_locks[static_cast<std::size_t>(altar)][1-s.current_team()]==0) {
            s.score[s.current_team()]+=a.gems; a.gems=0;
        }
        clear_channel(a);
    }
    a.can_move=true; a.can_attack=true; a.can_cast=true; a.used_lock=false;
    a.tribute_started=false; a.tribute_cancelled=false;
}
void owner_end(const Domain& d,State& s) {
    auto& a=s.actors[s.current_team()];
    for (auto& cd:a.cooldown) if (cd>0) --cd;
    if (a.stun>0) --a.stun;
    if (a.shield_timer>0) --a.shield_timer;
    if (a.channel_timer>0) --a.channel_timer;
    if (!a.alive() && a.respawn>0) --a.respawn;
    if (a.alive() && a.pos==a.spawn) a.hp=a.max_hp;
    if (++s.slot==2) { s.slot=0; ++s.round; }
    owner_start(d,s);
}

std::vector<Action> legal_actions(const Domain& d,const State& s) {
    if (terminal(d,s)) return {};
    const int team=s.current_team(); const auto& a=s.actors[team]; const auto& e=s.actors[1-team];
    std::vector<Action> out{{"end"}};
    if (!a.alive() || a.stun>0) return out;
    if (a.channel_altar>=0) {
        if (!a.tribute_cancelled) out.push_back({"cancel"});
        return out;
    }
    if (a.can_move) for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        Position p{a.pos.x+x,a.pos.y+y};
        if ((!x&&!y) || !d.walkable(p) || (e.alive()&&p==e.pos) || p==e.spawn) continue;
        out.push_back({"move",p.x,p.y});
    }
    if (a.can_attack && e.alive() && distance(a.pos,e.pos)<=a.attack_range) out.push_back({"basic"});
    if (a.can_cast && a.cooldown[0]==0 && e.alive() && distance(a.pos,e.pos)<=a.spell_range
        && (!a.spell_exclusive || a.can_move)) out.push_back({"cast"});
    if (a.can_cast && !a.can_attack && a.cooldown[1]==0) out.push_back({"reset"});
    for (int altar=0;altar<static_cast<int>(d.altars.size());++altar) {
        const int enemy_hits=s.altar_locks[static_cast<std::size_t>(altar)][1-team];
        if (a.can_attack && enemy_hits>0 && in_altar_range(d,a,altar,a.attack_range))
            out.push_back({"lock_hit",0,0,altar});
        if (!a.used_lock && a.locks>0 && s.altar_locks[static_cast<std::size_t>(altar)][team]==0
            && in_altar_range(d,a,altar,d.settings.lock_range)) out.push_back({"lock",0,0,altar});
        if (a.gems>0 && !a.tribute_started && in_altar_range(d,a,altar,d.settings.submit_range)
            && (enemy_hits==0 || a.can_attack)) out.push_back({"tribute",0,0,altar});
    }
    return out;
}
std::vector<Action> planning_actions(const Domain& d,const State& s) {
    auto actions=legal_actions(d,s);
    if (terminal(d,s)) return actions;
    const auto& actor=s.actors[s.current_team()];
    // In this no-passive, no-camp, single-owner decision domain these operations
    // change no world resource in our favor and only consume permissions. The
    // opponent cannot act between them. Keeping the action available is weakly
    // better than burning it now. Existing-channel cancellation remains legal.
    actions.erase(std::remove_if(actions.begin(),actions.end(),[&](const Action& a) {
        return (a.kind=="basic" && actor.strength==0)
            || (a.kind=="cast" && actor.spell_damage==0 && actor.spell_stun==0)
            || (a.kind=="cancel" && actor.tribute_started);
    }),actions.end());
    return actions;
}
State transition(const Domain& d,const State& state,const Action& action) {
    const auto legal=legal_actions(d,state);
    if (std::find(legal.begin(),legal.end(),action)==legal.end()) throw std::runtime_error("C9 illegal action "+action.kind);
    State s=state; const int team=s.current_team(); auto& a=s.actors[team]; auto& e=s.actors[1-team];
    if (action.kind=="end") { owner_end(d,s); return s; }
    if (action.kind=="cancel") { clear_channel(a); a.tribute_cancelled=true; return s; }
    if (action.kind=="move") { a.pos={action.x,action.y}; a.can_move=false; if (a.ranged) a.can_attack=false; }
    else if (action.kind=="basic") { consume_basic(a); hurt(d,s,1-team,a.strength,team); }
    else if (action.kind=="cast") {
        a.can_cast=false; a.cooldown[0]=d.settings.ability_cooldown;
        if (a.spell_exclusive) a.can_move=false;
        hurt(d,s,1-team,a.spell_damage,team);
        if (e.alive() && a.spell_stun>0 && !e.stun_immune) { e.stun=std::max(e.stun,a.spell_stun); clear_channel(e); }
    } else if (action.kind=="reset") { a.can_cast=false; a.cooldown[1]=d.settings.ability_cooldown; a.can_attack=true; }
    else if (action.kind=="lock") {
        --a.locks; a.used_lock=true; s.altar_locks[static_cast<std::size_t>(action.altar)][team]=d.settings.lock_hits;
        if (e.channel_altar==action.altar) clear_channel(e);
    } else if (action.kind=="lock_hit" || (action.kind=="tribute" && s.altar_locks[static_cast<std::size_t>(action.altar)][1-team]>0)) {
        consume_basic(a); --s.altar_locks[static_cast<std::size_t>(action.altar)][1-team];
    } else if (action.kind=="tribute") {
        a.tribute_started=true; a.channel_altar=action.altar;
        a.channel_timer=std::max(1,(a.gems+d.settings.submit_divisor-1)/d.settings.submit_divisor);
    }
    return s;
}

Json state_json(const State& s) {
    Json j{{"round",s.round},{"slot",s.slot},{"actors",Json::array()},
        {"score",Json::array({s.score[0],s.score[1]})},{"earned_gold",Json::array({s.earned_gold[0],s.earned_gold[1]})},
        {"kills",Json::array({s.kills[0],s.kills[1]})},{"altar_locks",Json::array()}};
    for (const auto& a:s.actors) j["actors"].push_back(Json{{"pos",pos_json(a.pos)},{"spawn",pos_json(a.spawn)},
        {"hp",a.hp},{"max_hp",a.max_hp},{"shield",a.shield},{"shield_timer",a.shield_timer},
        {"strength",a.strength},{"attack_range",a.attack_range},{"spell_damage",a.spell_damage},{"spell_range",a.spell_range},
        {"spell_stun",a.spell_stun},{"stun",a.stun},{"respawn",a.respawn},{"gems",a.gems},{"gold",a.gold},{"locks",a.locks},
        {"cooldown",Json::array({a.cooldown[0],a.cooldown[1]})},{"ranged",a.ranged},{"spell_exclusive",a.spell_exclusive},
        {"stun_immune",a.stun_immune},{"can_move",a.can_move},{"can_attack",a.can_attack},{"can_cast",a.can_cast},
        {"used_lock",a.used_lock},{"tribute_started",a.tribute_started},{"tribute_cancelled",a.tribute_cancelled},
        {"channel_altar",a.channel_altar},{"channel_timer",a.channel_timer}});
    for (auto a:s.altar_locks) j["altar_locks"].push_back(Json::array({a[0],a[1]}));
    return j;
}
State state_from_json(const Json& j) {
    State s; s.round=j.at("round").get<int>(); s.slot=j.at("slot").get<int>();
    for (int i=0;i<2;++i) {
        s.score[i]=j.at("score").at(i).get<int>(); s.earned_gold[i]=j.at("earned_gold").at(i).get<std::int64_t>(); s.kills[i]=j.at("kills").at(i).get<int>();
        const auto& v=j.at("actors").at(i); auto& a=s.actors[i];
        a.pos=position(v.at("pos")); a.spawn=position(v.at("spawn"));
        a.hp=v.at("hp").get<int>(); a.max_hp=v.at("max_hp").get<int>(); a.shield=v.at("shield").get<int>(); a.shield_timer=v.at("shield_timer").get<int>();
        a.strength=v.at("strength").get<int>(); a.attack_range=v.at("attack_range").get<int>(); a.spell_damage=v.at("spell_damage").get<int>(); a.spell_range=v.at("spell_range").get<int>();
        a.spell_stun=v.at("spell_stun").get<int>(); a.stun=v.at("stun").get<int>(); a.respawn=v.at("respawn").get<int>();
        a.gems=v.at("gems").get<int>(); a.gold=v.at("gold").get<std::int64_t>(); a.locks=v.at("locks").get<int>();
        a.cooldown={v.at("cooldown").at(0).get<int>(),v.at("cooldown").at(1).get<int>()};
        a.ranged=v.at("ranged").get<bool>(); a.spell_exclusive=v.at("spell_exclusive").get<bool>(); a.stun_immune=v.at("stun_immune").get<bool>();
        a.can_move=v.at("can_move").get<bool>(); a.can_attack=v.at("can_attack").get<bool>(); a.can_cast=v.at("can_cast").get<bool>();
        a.used_lock=v.at("used_lock").get<bool>(); a.tribute_started=v.at("tribute_started").get<bool>(); a.tribute_cancelled=v.at("tribute_cancelled").get<bool>();
        a.channel_altar=v.at("channel_altar").get<int>(); a.channel_timer=v.at("channel_timer").get<int>();
    }
    for (const auto& a:j.at("altar_locks").as_array()) s.altar_locks.push_back({a.at(0).get<int>(),a.at(1).get<int>()});
    return s;
}
std::string state_key(const State& s) { return state_json(s).dump(); }
Json action_json(const Action& a) { return Json{{"kind",a.kind},{"x",a.x},{"y",a.y},{"altar",a.altar}}; }
Action action_from_json(const Json& a) { return {a.at("kind").get<std::string>(),a.value("x",0),a.value("y",0),a.value("altar",-1)}; }

Case parse_case(const Json& input,const std::string& fallback_id) {
    Case c; c.definition=input; c.id=input.value("id",fallback_id);
    if (c.id.empty() || c.id.size()>64 || c.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || c.id=="." || c.id=="..")
        throw std::runtime_error("C9 case id must be a simple filename component");
    auto& d=c.domain; const Json settings=input.value("settings",Json::object());
    d.settings.winning_gems=integer(settings,"winning_gems",1000,1,1000000); d.settings.max_rounds=integer(settings,"max_rounds",2000,1,1000000);
    d.settings.submit_range=integer(settings,"submit_range",1,0,20); d.settings.submit_divisor=integer(settings,"submit_divisor",10,1,1000000);
    d.settings.lock_hits=integer(settings,"lock_hits",5,1,100); d.settings.lock_range=integer(settings,"lock_range",2,0,20);
    d.settings.respawn_time=integer(settings,"respawn_time",60,1,1000000); d.settings.kill_gold=integer(settings,"kill_gold",100,0,1000000);
    d.settings.ability_cooldown=integer(settings,"ability_cooldown",8,1,1000000); d.settings.shield_duration=integer(settings,"shield_duration",5,1,1000000);
    if (input.contains("terrain")) {
        for (const auto& row:input.at("terrain").as_array()) d.terrain.push_back(row.get<std::string>());
        d.height=static_cast<int>(d.terrain.size()); d.width=d.height ? static_cast<int>(d.terrain[0].size()) : 0;
    } else {
        d.width=integer(input,"width",7,2,128); d.height=integer(input,"height",5,2,128);
        d.terrain.assign(static_cast<std::size_t>(d.height),std::string(static_cast<std::size_t>(d.width),'.'));
    }
    if (d.width<2 || d.height<2 || d.width>128 || d.height>128) throw std::runtime_error("C9 terrain dimensions outside 2..128");
    for (const auto& row:d.terrain) if (static_cast<int>(row.size())!=d.width || row.find_first_not_of(".#~")!=std::string::npos) throw std::runtime_error("C9 terrain supports rectangular . # ~ only");
    if (!input.contains("altars") || input.at("altars").empty() || input.at("altars").size()>16)
        throw std::runtime_error("C9 requires1..16 altars to bound each copied frontier state");
    for (const auto& p:input.at("altars").as_array()) {
        auto q=position(p); if (!d.walkable(q)) throw std::runtime_error("C9 altar overlaps blocked terrain or another altar"); d.altars.push_back(q);
    }
    auto& s=c.state; s.round=integer(input,"round",std::max(1,d.settings.max_rounds-2),1,d.settings.max_rounds+1); s.slot=integer(input,"slot",0,0,1);
    if (d.settings.max_rounds-s.round+1>32) throw std::runtime_error("C9 is restricted to the final 32 rounds; split earlier play into explicit endgame cases");
    const auto& actors=input.at("actors"); if (!actors.is_array() || actors.size()!=2) throw std::runtime_error("C9 requires exactly one actor per team, A then B");
    for (int t=0;t<2;++t) {
        const auto& v=actors.at(t); auto& a=s.actors[t]; a.pos=position(v.at("pos")); a.spawn=position(v.value("spawn",v.at("pos")));
        a.max_hp=integer(v,"max_hp",1200,1,1000000); a.hp=integer(v,"hp",a.max_hp,0,a.max_hp);
        a.shield=integer(v,"shield",0,0,1000000); a.shield_timer=integer(v,"shield_timer",a.shield?d.settings.shield_duration:-1,-1,1000000);
        a.strength=integer(v,"strength",45,0,1000000); a.attack_range=integer(v,"attack_range",1,1,20);
        a.spell_damage=integer(v,"spell_damage",100,0,1000000); a.spell_range=integer(v,"spell_range",4,0,20); a.spell_stun=integer(v,"spell_stun",1,0,100);
        a.stun=integer(v,"stun",-1,-1,100); a.respawn=integer(v,"respawn",a.hp? -1:d.settings.respawn_time,-1,1000000);
        a.gems=integer(v,"gems",0,0,1000000); a.gold=integer(v,"gold",0,0,1000000); a.locks=integer(v,"locks",1,0,1);
        a.ranged=v.value("ranged",a.attack_range>1); a.spell_exclusive=v.value("spell_exclusive",a.ranged); a.stun_immune=v.value("stun_immune",false);
        const auto passive=v.value("passive","none"); if (passive!="none" && passive!="stun_immunity") throw std::runtime_error("C9 unsupported passive: "+passive);
        if (passive=="stun_immunity") a.stun_immune=true;
        if (v.contains("cooldown")) { const auto& cd=v.at("cooldown"); if (cd.size()!=2) throw std::runtime_error("C9 cooldown requires two slots"); Json cdj{{"a",cd.at(0)},{"b",cd.at(1)}}; a.cooldown={integer(cdj,"a",0,0,1000000),integer(cdj,"b",0,0,1000000)}; }
        a.channel_altar=integer(v,"channel_altar",-1,-1,static_cast<int>(d.altars.size())-1); a.channel_timer=integer(v,"channel_timer",a.channel_altar<0?-1:1,-1,1000000);
        if ((a.channel_altar<0)!=(a.channel_timer<0)) throw std::runtime_error("C9 channel altar and timer must both be absent or present");
        if (!d.walkable(a.pos) || !d.walkable(a.spawn)) throw std::runtime_error("C9 actor/spawn on blocked terrain");
    }
    if ((s.actors[0].alive() && s.actors[1].alive() && s.actors[0].pos==s.actors[1].pos) || s.actors[0].spawn==s.actors[1].spawn) throw std::runtime_error("C9 overlapping actors or spawns");
    for (int t=0;t<2;++t) {
        if (s.actors[t].pos==s.actors[1-t].spawn) throw std::runtime_error("C9 actor on enemy spawn");
        auto get_team=[&](const char* name,int fallback) { if (!input.contains(name)) return fallback; const auto& a=input.at(name); if (a.size()!=2) throw std::runtime_error(std::string("C9 ")+name+" needs two team values"); Json v{{"v",a.at(t)}}; return integer(v,"v",fallback,0,100000000); };
        s.score[t]=get_team("score",0); s.earned_gold[t]=get_team("earned_gold",s.actors[t].gold); s.kills[t]=get_team("kills",0);
        if (s.earned_gold[t]<s.actors[t].gold) throw std::runtime_error("C9 lifetime earned gold below current gold");
    }
    s.altar_locks.assign(d.altars.size(),{0,0});
    if (input.contains("altar_locks")) {
        if (input.at("altar_locks").size()!=d.altars.size()) throw std::runtime_error("C9 altar lock length mismatch");
        for (std::size_t i=0;i<d.altars.size();++i) { const auto& a=input.at("altar_locks").at(i); if (a.size()!=2) throw std::runtime_error("C9 altar locks need both teams"); Json v{{"a",a.at(0)},{"b",a.at(1)}}; s.altar_locks[i]={integer(v,"a",0,0,d.settings.lock_hits),integer(v,"b",0,0,d.settings.lock_hits)}; }
    }
    for (int t=0;t<2;++t) if (s.actors[t].channel_altar>=0) {
        if (!s.actors[t].alive() || s.actors[t].stun>0 || !in_altar_range(d,s.actors[t],s.actors[t].channel_altar,d.settings.submit_range)
            || s.altar_locks[static_cast<std::size_t>(s.actors[t].channel_altar)][1-t]>0) throw std::runtime_error("C9 invalid initial channel");
    }
    if (s.score[0]>=d.settings.winning_gems && s.score[1]>=d.settings.winning_gems) throw std::runtime_error("C9 both teams cannot already have won");
    owner_start(d,s); return c;
}
int maximum_remaining_actions(const Domain& d,const State& s) {
    if (terminal(d,s)) return 0;
    const auto owners=2LL*(d.settings.max_rounds-s.round+1)-s.slot;
    return static_cast<int>(std::min(owners*8+1,static_cast<long long>(std::numeric_limits<int>::max())));
}
double heuristic(const Domain& d,const State& s) {
    if (terminal(d,s)) return terminal_payoff(d,s);
    double score=static_cast<double>(s.score[0]-s.score[1]);
    for (int t=0;t<2;++t) {
        const auto& a=s.actors[t]; int nearest=d.width+d.height;
        for (auto p:d.altars) nearest=std::min(nearest,distance(a.pos,p));
        double v=(a.alive()? 0.35*a.gems/(1+nearest) : 0) + 0.01*a.hp + 0.02*a.shield + 0.002*s.earned_gold[t];
        if (a.channel_altar>=0) v+=0.5*a.gems/(1+a.channel_timer);
        score+=(t==0?1:-1)*v;
    }
    return std::clamp(std::tanh(score/std::max(10.0,d.settings.winning_gems*0.05)),-0.95,0.95);
}
} // namespace opt::endgame

namespace opt {
namespace {
using namespace endgame;
struct Outcome {
    int lower{-1}, upper{1}; double guess{}; std::vector<Action> pv;
};
Json outcome_json(const Outcome& o) {
    Json p=Json::array(); for (const auto& a:o.pv) p.push_back(action_json(a));
    return Json{{"lower",o.lower},{"upper",o.upper},{"guess",o.guess},{"pv",p}};
}
Outcome outcome_from(const Json& j) {
    Outcome o; o.lower=j.at("lower").get<int>(); o.upper=j.at("upper").get<int>(); o.guess=j.at("guess").get<double>();
    for (const auto& a:j.at("pv").as_array()) o.pv.push_back(action_from_json(a));
    return o;
}
struct Entry { int depth{}; Outcome outcome; };
struct Frame {
    State state; int depth{}, alpha{-2}, beta{2}; bool initialized{}, maximizing{};
    std::vector<Action> actions; std::size_t next{}; Outcome aggregate; bool has_best{};
};
Json frame_json(const Frame& f) {
    Json actions=Json::array(); for (const auto& a:f.actions) actions.push_back(action_json(a));
    return Json{{"state",state_json(f.state)},{"depth",f.depth},{"alpha",f.alpha},{"beta",f.beta},
        {"initialized",f.initialized},{"maximizing",f.maximizing},{"actions",actions},{"next",f.next},
        {"aggregate",outcome_json(f.aggregate)},{"has_best",f.has_best}};
}
Frame frame_from(const Json& j) {
    Frame f; f.state=state_from_json(j.at("state")); f.depth=j.at("depth").get<int>(); f.alpha=j.at("alpha").get<int>(); f.beta=j.at("beta").get<int>();
    f.initialized=j.at("initialized").get<bool>(); f.maximizing=j.at("maximizing").get<bool>();
    for (const auto& a:j.at("actions").as_array()) f.actions.push_back(action_from_json(a));
    f.next=j.at("next").get<std::size_t>(); f.aggregate=outcome_from(j.at("aggregate")); f.has_best=j.at("has_best").get<bool>();
    if (f.next>f.actions.size()) throw std::runtime_error("C9 corrupted search stack index");
    return f;
}

class Search {
public:
    Run& run; Case c; std::size_t tt_limit, tt_byte_limit, tt_bytes{}; int requested_depth, depth{1}, completed_depth{};
    Outcome root; std::vector<Frame> stack; std::map<std::string,Entry> tt;
    std::uint64_t hits{}, checkpoints{}, expanded{}, cache_limit_events{};
    Search(Run& r,Case input,const Json* saved,std::size_t byte_limit):run(r),c(std::move(input)),tt_byte_limit(byte_limit) {
        tt_limit=static_cast<std::size_t>(run.config.value("max_transpositions",100000));
        if (tt_limit<1 || tt_limit>2000000) throw std::runtime_error("C9 max_transpositions must be 1..2000000");
        requested_depth=std::min(run.config.value("max_depth",maximum_remaining_actions(c.domain,c.state)),maximum_remaining_actions(c.domain,c.state));
        if (requested_depth<0) throw std::runtime_error("C9 max_depth must be nonnegative");
        root.guess=heuristic(c.domain,c.state);
        if (saved) {
            if (saved->at("case_id").get<std::string>()!=c.id) throw std::runtime_error("C9 checkpoint case identity mismatch");
            depth=saved->at("depth").get<int>(); completed_depth=saved->at("completed_depth").get<int>(); root=outcome_from(saved->at("root"));
            hits=saved->value("hits",std::uint64_t{}); expanded=saved->value("expanded",std::uint64_t{}); cache_limit_events=saved->value("cache_limit_events",std::uint64_t{});
            for (const auto& f:saved->at("stack").as_array()) stack.push_back(frame_from(f));
            for (const auto& e:saved->at("transpositions").as_array()) {
                const auto key=e.at("key").get<std::string>(); Entry entry{e.at("depth").get<int>(),outcome_from(e.at("outcome"))};
                tt_bytes+=entry_bytes(key,entry); tt.emplace(key,std::move(entry));
            }
            if (tt_bytes>tt_byte_limit) throw std::runtime_error("C9 checkpoint cache exceeds its declared payload limit");
        }
        if (terminal(c.domain,c.state)) {
            const int value=terminal_payoff(c.domain,c.state); root={value,value,static_cast<double>(value),{}};
            stack.clear();
        }
    }
    static std::size_t entry_bytes(const std::string& key,const Entry& entry) {
        return Json{{"key",key},{"depth",entry.depth},{"outcome",outcome_json(entry.outcome)}}.dump().size()+1;
    }
    void cache(const std::string& key,Entry entry) {
        const auto found=tt.find(key); const auto before=found==tt.end()?0:entry_bytes(key,found->second);
        const auto after=entry_bytes(key,entry);
        if ((found==tt.end()&&tt.size()>=tt_limit) || tt_bytes-before+after>tt_byte_limit) { ++cache_limit_events; return; }
        tt_bytes=tt_bytes-before+after;
        if (found==tt.end()) tt.emplace(key,std::move(entry)); else found->second=std::move(entry);
    }
    Json save() const {
        Json frames=Json::array(), entries=Json::array();
        for (const auto& f:stack) frames.push_back(frame_json(f));
        for (const auto& [key,e]:tt) entries.push_back(Json{{"key",key},{"depth",e.depth},{"outcome",outcome_json(e.outcome)}});
        return Json{{"case_id",c.id},{"depth",depth},{"completed_depth",completed_depth},{"root",outcome_json(root)},
            {"stack",frames},{"transpositions",entries},{"hits",hits},{"expanded",expanded},{"cache_limit_events",cache_limit_events}};
    }
    bool solved() const { return root.lower==root.upper; }
    bool exhausted_depth() const { return completed_depth>=requested_depth; }
    void start_depth() { Frame f; f.state=c.state; f.depth=depth; stack.push_back(std::move(f)); }
    bool step() {
        if (stack.empty()) {
            if (terminal(c.domain,c.state)) { root={terminal_payoff(c.domain,c.state),terminal_payoff(c.domain,c.state),static_cast<double>(terminal_payoff(c.domain,c.state)),{}}; return false; }
            if (solved() || exhausted_depth()) return false;
            start_depth();
        }
        Frame& f=stack.back();
        if (!f.initialized) {
            if (run.stop()) return false;
            run.advance(); ++expanded;
            if (terminal(c.domain,f.state)) {
                const int payoff=terminal_payoff(c.domain,f.state); finish_frame({payoff,payoff,static_cast<double>(payoff),{}}); return true;
            }
            const auto key=state_key(f.state); const auto found=tt.find(key);
            if (found!=tt.end() && (found->second.depth>=f.depth || found->second.outcome.lower==found->second.outcome.upper)) {
                ++hits; finish_frame(found->second.outcome); return true;
            }
            if (f.depth==0) { Outcome o; o.guess=heuristic(c.domain,f.state); finish_frame(o); return true; }
            f.maximizing=f.state.current_team()==0; f.actions=planning_actions(c.domain,f.state);
            std::stable_sort(f.actions.begin(),f.actions.end(),[&](const Action& a,const Action& b) {
                double va=heuristic(c.domain,transition(c.domain,f.state,a)); double vb=heuristic(c.domain,transition(c.domain,f.state,b));
                return f.maximizing ? va>vb : va<vb;
            });
            f.aggregate.lower=f.aggregate.upper=f.maximizing?-1:1;
            f.aggregate.guess=f.maximizing?-2:2; f.initialized=true;
        }
        if (f.next==f.actions.size() || (f.has_best && (f.maximizing ? f.aggregate.lower>=f.beta || f.aggregate.lower==1 : f.aggregate.upper<=f.alpha || f.aggregate.upper==-1))) {
            Outcome result=f.aggregate;
            if (f.next<f.actions.size()) { if (f.maximizing) result.upper=1; else result.lower=-1; }
            finish_frame(std::move(result)); return true;
        }
        const auto action=f.actions[f.next++]; Frame child; child.state=transition(c.domain,f.state,action); child.depth=f.depth-1;
        child.alpha=f.alpha; child.beta=f.beta;
        if (f.has_best) { if (f.maximizing) child.alpha=std::max(child.alpha,f.aggregate.lower); else child.beta=std::min(child.beta,f.aggregate.upper); }
        stack.push_back(std::move(child)); return true;
    }
    void finish_frame(Outcome out) {
        const auto frame=std::move(stack.back()); stack.pop_back();
        // A policy entry needs its first action, not an unbounded duplicated suffix.
        // Proof intervals do not depend on retaining an entire principal variation.
        if (out.pv.size()>64) out.pv.resize(64);
        if (frame.depth>0) {
            const auto key=state_key(frame.state); auto found=tt.find(key);
            if (found!=tt.end()) {
                // Every interval bounds the same terminal payoff, even at different depths.
                out.lower=std::max(out.lower,found->second.outcome.lower); out.upper=std::min(out.upper,found->second.outcome.upper);
                if (out.lower>out.upper) throw std::runtime_error("C9 incompatible transposition proof intervals");
                if (frame.depth>=found->second.depth || out.lower==out.upper) cache(key,Entry{frame.depth,out});
            } else cache(key,Entry{frame.depth,out});
        }
        if (stack.empty()) {
            root=std::move(out); completed_depth=depth; ++depth;
            run.progress(Json{{"case",c.id},{"depth",completed_depth},{"lower",root.lower},{"upper",root.upper},
                {"nodes",expanded},{"transpositions",tt.size()},{"hits",hits}});
            return;
        }
        auto& parent=stack.back();
        const bool better=!parent.has_best || (parent.maximizing
            ? std::tuple(out.lower,out.guess,out.upper)>std::tuple(parent.aggregate.lower,parent.aggregate.guess,parent.aggregate.upper)
            : std::tuple(out.upper,out.guess,out.lower)<std::tuple(parent.aggregate.upper,parent.aggregate.guess,parent.aggregate.lower));
        if (better) {
            parent.aggregate.guess=out.guess; parent.aggregate.pv=out.pv;
            parent.aggregate.pv.insert(parent.aggregate.pv.begin(),parent.actions[parent.next-1]);
        }
        if (parent.maximizing) { parent.aggregate.lower=std::max(parent.aggregate.lower,out.lower); parent.aggregate.upper=std::max(parent.aggregate.upper,out.upper); }
        else { parent.aggregate.lower=std::min(parent.aggregate.lower,out.lower); parent.aggregate.upper=std::min(parent.aggregate.upper,out.upper); }
        parent.has_best=true;
    }
    Json report() const {
        Json principal=Json::array(),trace=Json::array(); State s=c.state;
        // The continuation is a feasible model trace, not an adversarial guarantee.
        std::size_t n=0; const int cap=maximum_remaining_actions(c.domain,s);
        while (!terminal(c.domain,s) && n<static_cast<std::size_t>(cap)) {
            const auto actions=legal_actions(c.domain,s); Action action;
            if (n<root.pv.size() && std::find(actions.begin(),actions.end(),root.pv[n])!=actions.end()) action=root.pv[n];
            else {
                auto key=state_key(s); auto e=tt.find(key);
                if (e!=tt.end() && !e->second.outcome.pv.empty() && std::find(actions.begin(),actions.end(),e->second.outcome.pv[0])!=actions.end()) action=e->second.outcome.pv[0];
                else {
                    action=actions.front(); double best=s.current_team()==0?-2:2;
                    for (const auto& candidate:actions) { auto child=transition(c.domain,s,candidate); const double h=heuristic(c.domain,child); if ((s.current_team()==0&&h>best)||(s.current_team()==1&&h<best)) { best=h;action=candidate; } }
                }
            }
            principal.push_back(action_json(action)); trace.push_back(Json{{"state",state_json(s)},{"action",action_json(action)}});
            s=transition(c.domain,s,action); ++n;
        }
        Json report{{"schema","contenders-c9-result/v1"},{"case",c.id},{"domain","restricted-two-actor-integer-banking-denial/v1"},
            {"initial_case",c.definition},{"initial_state",state_json(c.state)},
            {"objective","Team A engine-style terminal win payoff; exact ties favor B"},{"lower_bound",root.lower},{"upper_bound",root.upper},
            {"proven_in_domain",solved()},{"winning_team",solved()?(root.lower==1?"A":"B"):"unresolved"},
            {"completed_depth",completed_depth},{"configured_depth",requested_depth},{"remaining_action_upper_bound",maximum_remaining_actions(c.domain,c.state)},
            {"expanded_nodes",expanded},{"transposition_hits",hits},{"transposition_entries",tt.size()},{"frontier_frames",stack.size()},
            {"transposition_payload_bytes",tt_bytes},{"transposition_payload_limit",tt_byte_limit},{"cache_limit_events",cache_limit_events},
            {"resource_scope","payload cap covers serialized transposition entries; allocator, JSON object, DFS frontier and report memory are additional"},
            {"resume_semantics","explicit DFS stack, completed transpositions, next child indices and bounds are serialized; exact continuation"},
            {"pruning","Only permission-consuming no-op basic/cast and same-input tribute-start/cancel prefixes are dominated in this no-passive/no-camp sequential domain; all legal transitions remain available for trace replay"},
            {"native_validation","unqualified restricted model; source/rule semantics are not native replay certification"},
            {"observation_scope","full-information restricted state; not a deployable information-safe full-game actor"},
            {"unsupported",Json::array({"camps and capture rewards","hidden information and vision","additional actors","dashes and teleport/recall","shopping and stat changes","linked bodies","passives except stun immunity","fractional damage"})},
            {"feasible_model_trace",trace},{"principal_actions",principal},{"trace_terminal",terminal(c.domain,s)},{"trace_final_state",state_json(s)},
            {"trace_scope","one feasible continuation under selected and heuristic opponent choices; not itself a minimax proof"}};
        if (terminal(c.domain,s)) report["trace_payoff"]=terminal_payoff(c.domain,s);
        return report;
    }
    void export_policy(const fs::path& path) const {
        struct Row { Action action; int lower,upper; };
        std::map<std::string,Row> rows;
        // Always retain the latest root recommendation even when a bounded
        // transposition cache could not store its longer principal variation.
        if (!root.pv.empty()) rows.emplace(state_key(c.state),Row{root.pv[0],root.lower,root.upper});
        for (const auto& [key,e]:tt) if (!e.outcome.pv.empty() && rows.size()<10000)
            rows.emplace(key,Row{e.outcome.pv[0],e.outcome.lower,e.outcome.upper});
        std::ostringstream out;
        out<<"#pragma once\n#include <algorithm>\n#include <string_view>\n#include <utility>\nnamespace opt_endgame_policy {\n"
            <<"// Restricted full-state keys from opt::endgame::state_key; no full-game certification.\n"
            <<"inline std::string_view choose(std::string_view key) {\n  static constexpr std::pair<std::string_view,std::string_view> rows[] = {\n";
        Json table=Json::array();
        for (const auto& [key,row]:rows) {
            out<<"    {"<<Json(key).dump()<<","<<Json(action_json(row.action).dump()).dump()<<"},\n";
            table.push_back(Json{{"state_key",key},{"action",action_json(row.action)},{"lower_bound",row.lower},{"upper_bound",row.upper},{"proven_in_domain",row.lower==row.upper}});
        }
        // A sentinel avoids a zero-size array and keeps the binary-search ordering.
        out<<"    {\"~\",\"{\\\"kind\\\":\\\"end\\\"}\"}\n  };\n"
            <<"  auto it=std::lower_bound(std::begin(rows),std::end(rows),key,[](auto row,auto k){return row.first<k;});\n"
            <<"  return it!=std::end(rows)&&it->first==key?it->second:std::string_view(\"{\\\"kind\\\":\\\"end\\\"}\");\n}\n}\n";
        atomic_write(path,out.str());
        write_json(path.parent_path()/"policy.json",Json{{"schema","contenders-c9-policy/v1"},{"case",c.id},{"rows",table},{"covered_states",table.size()},
            {"root_key",state_key(c.state)},{"fallback",Json{{"kind","end"}}},{"scope","Exact restricted full-state keys; unknown rows are heuristic recommendations, not guaranteed winning actions; native observation and fuel qualification absent"}});
    }
};
}

int run_c9(const Options& options) {
    Run run(options,"c9-endgame","2");
    Json catalog_defaults=Json::object();
    const auto catalog_path=options.repo/"docs"/"reference"/"runtime-catalog.json";
    if (!options.repo.empty() && fs::is_regular_file(catalog_path)) {
        const auto catalog=read_json(catalog_path); const auto& native=catalog.at("result").at("settings");
        const double multiplier=native.at("submit_gem_duration_multiplier").get<double>();
        if (!(multiplier>0) || std::abs(1.0/multiplier-std::round(1.0/multiplier))>1e-9
            || native.at("submit_gem_base_duration").get<int>()!=0 || native.at("submit_gem_minimum_duration").get<int>()!=1
            || native.at("spawn_healing").get<double>()!=1.0 || native.at("passive_gold").get<int>()!=0)
            throw std::runtime_error("C9 runtime settings outside supported integer tribute/full spawn-heal/no passive-income domain");
        catalog_defaults=Json{{"winning_gems",native.at("winning_gems")},{"max_rounds",native.at("max_rounds")},
            {"submit_range",native.at("submit_gems_range")},{"submit_divisor",static_cast<int>(std::round(1.0/multiplier))},
            {"lock_hits",native.at("altar_lock_hits")},{"lock_range",native.at("ward_placement_radius")},
            {"respawn_time",native.at("respawn_time")},{"kill_gold",native.at("kill_gold")}};
    }
    auto materialize=[&](const Json& source,const std::string& fallback) {
        Json input=source; Json settings=catalog_defaults;
        if (source.contains("settings")) for (const auto& [key,value]:source.at("settings").as_object()) settings[key]=value;
        input["settings"]=settings;
        input["settings_source"]=catalog_defaults.empty()?"explicit case and restricted-model defaults; no runtime catalog present":"pinned runtime catalog defaults with explicit case overrides";
        return endgame::parse_case(input,fallback);
    };
    std::vector<endgame::Case> cases;
    if (run.config.contains("cases")) { for (const auto& c:run.config.at("cases").as_array()) cases.push_back(materialize(c,"case-"+std::to_string(cases.size()))); }
    else cases.push_back(materialize(run.config,"endgame"));
    if (cases.empty() || cases.size()>64) throw std::runtime_error("C9 requires 1..64 cases per independent worker packet");
    std::map<std::string,bool> ids; for (const auto& c:cases) if (!ids.emplace(c.id,true).second) throw std::runtime_error("C9 duplicate case id");
    const auto byte_limit=run.config.value("max_transposition_bytes",std::uint64_t{64}*1024*1024);
    if (byte_limit<65536 || byte_limit>std::uint64_t{128}*1024*1024) throw std::runtime_error("C9 total serialized transposition limit must be 64KiB..128MiB");
    const auto quantum=run.config.value("case_node_quantum",std::uint64_t{10000});
    if (!quantum) throw std::runtime_error("C9 case_node_quantum must be positive");
    std::vector<std::unique_ptr<Search>> searches;
    for (std::size_t i=0;i<cases.size();++i) {
        const Json* saved=run.state.contains("searches")?&run.state.at("searches").at(i):nullptr;
        searches.push_back(std::make_unique<Search>(run,cases[i],saved,static_cast<std::size_t>(byte_limit/cases.size())));
    }
    std::size_t index=run.state.value("case_index",std::size_t{});
    auto used=run.state.value("quantum_used",std::uint64_t{});
    if (index>=cases.size() || used>quantum) throw std::runtime_error("C9 invalid resumed family cursor");
    const auto interval=static_cast<std::uint64_t>(std::max(1,run.config.value("checkpoint_nodes",10000)));
    std::uint64_t next_checkpoint=((run.iteration/interval)+1)*interval;
    auto checkpoint=[&] {
        Json states=Json::array(); for (const auto& search:searches) states.push_back(search->save());
        Json state{{"schema","contenders-c9-checkpoint/v2"},{"case_index",index},{"quantum_used",used},{"searches",states}};
        // The bounded two-actor state, <=64 cases and <=128MiB total cache keep
        // normal packets comfortably below the common reader's 1GiB ceiling.
        // Refuse a pathological export before replacing its previous checkpoint.
        if (state.dump().size()>std::size_t{512}*1024*1024) throw std::runtime_error("C9 checkpoint exceeds512MiB; split the case packet");
        run.checkpoint(state);
    };
    auto done=[&](std::size_t i) { return searches[i]->solved()||searches[i]->exhausted_depth(); };
    auto all_done=[&] { for (std::size_t i=0;i<cases.size();++i) if (!done(i)) return false; return true; };
    auto export_case=[&](std::size_t i) {
        auto report=searches[i]->report(); const fs::path case_out=options.out/cases[i].id; fs::create_directories(case_out);
        write_json(case_out/"solution.json",report); searches[i]->export_policy(case_out/"policy.hpp");
        Json predictions=Json::array(); predictions.push_back(report.at("initial_state"));
        const auto& trace=report.at("feasible_model_trace");
        for (std::size_t j=0;j<trace.size();++j) predictions.push_back(j+1<trace.size()?trace.at(j+1).at("state"):report.at("trace_final_state"));
        Json request{{"schema","contenders-c9-native-trace-request/v1"},{"domain",report.at("domain")},{"case",cases[i].id},
            {"setup",cases[i].definition},{"initial_state",report.at("initial_state")},{"actions",report.at("principal_actions")},{"predicted_states",predictions},
            {"acceptance","Reconstruct an actual supported native case, apply every ordered action, compare every projected state exactly, and verify terminal payoff; unsupported reconstruction must fail closed"},
            {"native_qualification",false},{"missing_interface","Native arbitrary-state import or actual-replay suffix mapping for two actors, altar locks, resources, owner timers and terminal scoring"}};
        request["request_sha256"]=sha256(request.dump()); write_json(case_out/"native-validation-request.json",request);
        return report;
    };
    while (!run.stop() && !all_done()) {
        if (done(index) || used>=quantum) { index=(index+1)%cases.size(); used=0; continue; }
        const auto before=run.iteration;
        if (!searches[index]->step()) { index=(index+1)%cases.size(); used=0; continue; }
        used+=run.iteration-before;
        if (run.iteration>=next_checkpoint) { checkpoint(); next_checkpoint=run.iteration+interval; }
        if (done(index) || used>=quantum) {
            export_case(index); index=(index+1)%cases.size(); used=0;
        }
    }
    Json reports=Json::array(),complete=Json::array();std::size_t proven=0;
    for (std::size_t i=0;i<cases.size();++i) { auto report=export_case(i); reports.push_back(report); if (done(i)) complete.push_back(report); if (searches[i]->solved()) ++proven; }
    checkpoint();
    run.result(Json{{"status",all_done()?"completed":"checkpointed"},{"case_index",index},{"case_count",cases.size()},{"cases",reports},
        {"current",reports.at(index)},{"completed",complete},{"proven_cases",proven},{"total_transposition_payload_limit",byte_limit},
        {"family_scheduler","round-robin bounded node quanta; cursor and partially consumed quantum resume exactly"},
        {"scope","restricted full-information model solutions and bounds; native reconstruction, frequency and full-game qualification remain separate"}});
    return 0;
}
} // namespace opt
