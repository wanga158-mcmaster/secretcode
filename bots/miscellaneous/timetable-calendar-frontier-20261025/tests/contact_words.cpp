#include "contact_words.h"
#include <cstdio>
#include <vector>
namespace {
using namespace tt::contact_words;
int failures{};
void check(bool ok,const char* message)
{ if (!ok) { std::printf("FAIL %s\n",message); ++failures; } }
struct Backend
{
    Frame frame{7,{5,5},true,false,true,true,true};
    std::vector<Action> script;
    std::vector<Kind> actual;
    std::size_t cursor{};
    int refreshes{},guards{},fences{},rejections{};
    bool ranged{},refuse_first{},cross_epoch{},fuel{true};
    Spell spell{Kind::reset,true,false,true,false,false,0};
    bool budget() const { return fuel; }
    int epoch() const { return frame.epoch; }
    Point position() const { return frame.pos; }
    Frame refresh() { ++refreshes; return frame; }
    Action nominate(const Frame&,const History&,const std::array<Action,12>& refused,std::size_t n)
    {
        while (cursor<script.size())
        {
            bool excluded=false;
            for (std::size_t i=0;i<n;++i) excluded|=script[cursor]==refused[i];
            if (!excluded) return script[cursor];
            ++cursor;
        }
        return {};
    }
    bool validate(const Action& a,const Frame& f)
    {
        ++guards;
        if (cross_epoch) ++frame.epoch;
        return !is_cast(a.kind) || cast_guard(a,f,spell);
    }
    bool same_input(const Frame& f)
    { ++fences; return frame.epoch==f.epoch && frame.pos==f.pos; }
    bool apply(const Action& a)
    {
        if (refuse_first) { refuse_first=false; ++rejections; return false; }
        actual.push_back(a.kind); ++cursor;
        if (a.kind==Kind::move) { frame.pos=a.aim; frame.move=false; if (ranged) frame.attack=false; }
        else if (a.kind==Kind::basic) { frame.attack=false; if (ranged) frame.move=false; }
        else
        {
            frame.cast=false;
            if (a.kind==Kind::reset) frame.attack=true;
            if (a.kind==Kind::dash) frame.pos=a.aim;
            if (a.exclusive) frame.move=false;
        }
        return true;
    }
};
Action move(Point p) { return {Kind::move,-1,-1,p}; }
Action basic() { return {Kind::basic,-1,0,{6,5}}; }
Action reset() { return {Kind::reset,0,-1,{5,5}}; }
}
int main()
{
    check(visible_reply_score(2,40,true,0)==160 && visible_reply_score(2,40,true,0)>=120,
        "ordered two-input reset score contains Varg's Basic Reset Basic / Basic 120 burst");
    check(available_within_two_inputs(0) && available_within_two_inputs(1) &&
        available_within_two_inputs(2) && !available_within_two_inputs(3),
        "future reply scope advances cooldowns instead of freezing current can-cast state");
    Backend ranged; ranged.ranged=true;
    ranged.script={move({6,5}),reset(),basic()};
    auto result=execute(ranged);
    check(result.count==3 && ranged.actual==std::vector<Kind>{Kind::move,Kind::reset,Kind::basic},
        "ranged ordinary Move -> Reset -> Basic is one fresh legal word");
    check(result.history.walked && result.history.cast && !ranged.frame.move && !ranged.frame.attack,
        "reset restores only A; ranged ordinary movement remains spent");
    check(result.records[0].from==Point{5,5} && result.records[0].to==Point{6,5} &&
        ranged.refreshes>=static_cast<int>(2*result.count) && ranged.fences==3,
        "actual edge and live post-command frames are retained");

    Backend melee;
    melee.script={basic(),reset(),basic(),move({4,5})};
    result=execute(melee);
    check(result.count==4 && melee.actual==std::vector<Kind>{Kind::basic,Kind::reset,Kind::basic,Kind::move},
        "melee Basic -> Reset -> Basic -> Move keeps same-input egress");
    check(result.history.basics==2 && result.history.accepted==4 && result.history.walked,
        "word obeys finite four-command and two-basic grammar");

    Backend refusal; refusal.refuse_first=true;
    refusal.script={move({6,5}),move({4,5}),basic()};
    result=execute(refusal);
    check(refusal.rejections==1 && result.count==2 && result.records[0].from==Point{5,5} &&
        result.records[0].to==Point{4,5} && result.history.walked,
        "failed movement invents no edge or spent allowance and a different fresh attempt remains legal");
    Backend failed_cast; failed_cast.frame.attack=false; failed_cast.refuse_first=true;
    failed_cast.script={reset(),{Kind::reset,1,-1,{5,5}},basic()};
    result=execute(failed_cast);
    check(failed_cast.rejections==1 && result.count==2 && result.records[0].action.slot==1 &&
        failed_cast.actual==std::vector<Kind>{Kind::reset,Kind::basic},
        "failed cast neither spends C nor fabricates attack-reset availability");

    Backend exclusive;
    exclusive.spell={Kind::ground_cast,true,true,false,false,true,4};
    exclusive.script={move({6,5}),{Kind::ground_cast,0,-1,{7,5},true},basic()};
    result=execute(exclusive);
    check(result.count==2 && !result.history.cast && exclusive.frame.cast,
        "exclusive ground cast is rejected after an actual ordinary walk");
    Backend inherited;
    inherited.script={{Kind::ground_cast,0,-1,{7,5},true}};
    inherited.spell=exclusive.spell;
    History entry; entry.walked=true;
    result=execute(inherited,entry);
    check(result.count==0 && inherited.frame.cast,"earlier ordinary history survives fresh call entry");

    Backend ground;
    ground.spell={Kind::ground_cast,true,true,false,false,true,4};
    ground.script={{Kind::ground_cast,0,-1,{9,5},true},move({4,5})};
    result=execute(ground);
    check(result.count==1 && result.history.cast && !ground.frame.move,
        "ground-only area stun uses its ground mask and exclusive cast closes walking");
    auto f=Frame{7,{5,5},true,false,true,true,true};
    check(cast_guard({Kind::ground_cast,0,-1,{9,5},true},f,ground.spell) &&
        !cast_guard({Kind::ground_cast,0,-1,{10,5},true},f,ground.spell),
        "ground CC guard enforces geometric range without requiring enemy target mask");
    auto no_ground=ground.spell; no_ground.ground=false; no_ground.enemies=true;
    check(!cast_guard({Kind::ground_cast,0,-1,{7,5},true},f,no_ground),
        "enemy-only target mask cannot authorize a ground payload");

    Backend onecast;
    onecast.frame.attack=false;
    onecast.script={reset(),{Kind::reset,1,-1,{5,5}},basic()};
    result=execute(onecast);
    check(result.count==2 && onecast.actual==std::vector<Kind>{Kind::reset,Kind::basic},
        "one global C prevents casting both nominally ready slots");

    Backend dash;
    dash.spell={Kind::dash,true,false,false,false,true,2};
    dash.script={move({6,5}),{Kind::dash,0,-1,{8,5}}};
    result=execute(dash);
    check(result.count==2 && result.history.walked && result.history.dashed &&
        result.records[1].from==Point{6,5} && result.records[1].to==Point{8,5},
        "accepted compatible dash changes position without erasing the ordinary edge");
    const auto hole=[](Point p,bool landing) {
        return p.x>=0 && p.y>=0 && p.x<10 && p.y<10 && (!landing || p!=Point{6,6});
    };
    check(two_step_dash_path({5,5},{7,7},hole) && !two_step_dash_path({5,5},{6,6},hole),
        "shared dash guard permits an interior hole but rejects a hole landing");
    check(!two_step_dash_path({5,5},{7,7},[](Point p,bool) { return p!=Point{6,6}; }) &&
        !two_step_dash_path({5,5},{8,8},[](Point,bool) { return true; }) &&
        !two_step_dash_path({5,5},{7,7},[](Point p,bool) { return p!=Point{7,7}; }),
        "shared dash guard rejects blocked unique interior, unsupported length and occupied landing");

    Backend stale; stale.cross_epoch=true; stale.script={basic()};
    result=execute(stale);
    check(result.count==0 && stale.actual.empty() && stale.frame.attack,
        "epoch drift during target validation cannot authorize a command");
    Backend changed; changed.frame.attack=false; changed.script={basic()};
    result=execute(changed);
    check(result.count==0 && changed.actual.empty(),"fresh spent A overrides a nominated basic");
    Backend stopped; stopped.fuel=false; stopped.script={basic()};
    result=execute(stopped);
    check(result.interrupted && result.count==0 && stopped.refreshes==0,
        "fuel tail stops before further observations or commands");
    std::printf("contact words failures=%d\n",failures);
    return failures ? 1 : 0;
}
