#pragma once
#include <array>
#include <cstddef>
#include <algorithm>
#include <cstdlib>

// A finite action-word executor. Selection belongs to the caller; this grammar
// owns allowance/history composition and never predicts a successful command.
namespace tt::contact_words {
struct Point { int x{},y{}; bool operator==(const Point&) const = default; };
enum class Kind { stop,move,basic,reset,shield,heal,unit_cast,ground_cast,dash };
struct Action
{
    Kind kind{Kind::stop}; int slot{-1},target{-1}; Point aim{}; bool exclusive{};
    bool operator==(const Action&) const = default;
};
struct Frame
{
    int epoch{-1}; Point pos{};
    bool live{},channeling{},move{},attack{},cast{};
};
struct History
{
    bool walked{},cast{},dashed{};
    unsigned basics{},accepted{};
};
struct Record { Action action; Point from{},to{}; };
struct Result
{
    History history;
    std::array<Record,4> records{};
    std::size_t count{};
    bool interrupted{};
};
inline bool is_cast(Kind k)
{ return k!=Kind::stop && k!=Kind::move && k!=Kind::basic; }
inline bool permitted(const Action& a,const Frame& f,const History& h)
{
    if (!f.live || f.channeling || h.accepted>=4) return false;
    if (a.kind==Kind::stop) return false;
    if (a.kind==Kind::move) return f.move && !h.walked;
    if (a.kind==Kind::basic) return f.attack && h.basics<2;
    if (!f.cast || h.cast || a.slot<0 || a.slot>=2 || (a.exclusive && h.walked)) return false;
    // A reset restores A, never M. Wasting an available A is outside this word.
    return a.kind!=Kind::reset || !f.attack;
}
struct Spell
{
    Kind kind{Kind::stop};
    bool ready{},exclusive{},self{},enemies{},ground{};
    float range{};
};
inline bool available_within_two_inputs(int cooldown) { return cooldown<=2; }
// This names an ordered visible-reply score, never an average damage rate or a
// survival bound. Each admitted input can carry Basic -> Reset -> Basic; the
// caller separately supplies geometric reach and unsupported-source scope.
inline float visible_reply_score(unsigned inputs,float strength,bool reset,float cast)
{ return static_cast<float>(inputs)*((reset ? 2 : 1)*strength+cast); }
inline bool cast_guard(const Action& a,const Frame& f,const Spell& spell)
{
    if (!is_cast(a.kind) || a.kind!=spell.kind || !spell.ready || !f.cast ||
        a.exclusive!=spell.exclusive) return false;
    if (a.kind==Kind::reset) return spell.self && !f.attack;
    if (a.kind==Kind::shield || a.kind==Kind::heal) return spell.self;
    const int d=std::max(std::abs(f.pos.x-a.aim.x),std::abs(f.pos.y-a.aim.y));
    if (d>spell.range) return false;
    if (a.kind==Kind::unit_cast) return spell.enemies;
    // An AoE with a duration is still a ground payload, not an enemy-target cast.
    return (a.kind==Kind::ground_cast || a.kind==Kind::dash) && spell.ground;
}
template<class Clear>
bool two_step_dash_path(Point from,Point end,Clear clear)
{
    const auto distance=[](Point a,Point b) {
        return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
    };
    const int d=distance(from,end);
    if (d<1 || d>2 || !clear(end,true)) return false;
    if (d==1) return true;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
    {
        const Point mid{from.x+dx,from.y+dy};
        if ((dx || dy) && distance(mid,end)==1 && clear(mid,false)) return true;
    }
    return false;
}

// Backend refresh/validate/same_input are all live guards. The final fence runs
// after target/path queries. Refusal records no allowances, walking or cooldown;
// nominations see its identity and may try a different bounded candidate.
template<class Backend>
Result execute(Backend& backend,History initial={})
{
    Result result; result.history=initial;
    std::array<Action,12> refused{}; std::size_t refusals=0;
    for (unsigned attempt=0;attempt<12 && result.history.accepted<4;++attempt)
    {
        if (!backend.budget()) { result.interrupted=true; break; }
        const Frame observed=backend.refresh();
        if (!backend.budget()) { result.interrupted=true; break; }
        if (!observed.live || observed.channeling) break;
        const Action action=backend.nominate(observed,result.history,refused,refusals);
        if (action.kind==Kind::stop) break;
        if (!backend.budget()) { result.interrupted=true; break; }
        const Frame fresh=backend.refresh();
        if (!backend.budget()) { result.interrupted=true; break; }
        if (fresh.epoch!=observed.epoch || fresh.pos!=observed.pos)
        { result.interrupted=true; break; }
        const bool guard=permitted(action,fresh,result.history) && backend.validate(action,fresh) &&
            backend.same_input(fresh);
        if (!guard)
        {
            if (refusals==refused.size()) break;
            refused[refusals++]=action; continue;
        }
        if (!backend.apply(action))
        {
            if (refusals==refused.size()) break;
            refused[refusals++]=action; continue;
        }
        const Point after=backend.position();
        result.records[result.count++]={action,fresh.pos,after};
        ++result.history.accepted;
        if (action.kind==Kind::move) result.history.walked=true;
        else if (action.kind==Kind::basic) ++result.history.basics;
        else { result.history.cast=true; if (action.kind==Kind::dash) result.history.dashed=true; }
        if (backend.epoch()!=fresh.epoch) { result.interrupted=true; break; }
    }
    return result;
}
} // namespace tt::contact_words
