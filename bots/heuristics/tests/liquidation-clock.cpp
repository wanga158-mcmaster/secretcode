#include "../liquidation-clock/src/clock.h"
#include "../liquidation-clock/src/mechanics.h"
#include "../liquidation-clock/src/policy.h"
#include "../common/src/capture.h"
#include <array>
#include <cassert>
#include <cstdio>

namespace lc=h12_liquidation_clock;

// The real dispatcher and lifecycle helpers run here. This adapter supplies
// engine-owned action receipts, capture phases and channel owner-start effects;
// it never equates an accepted plan, an issued action, personal payout or bank.
struct World {
    lc::Timing timing;
    lc::TerminalFacts f;
    bool alive=true,allowed=true,on_altar=true,bank_receipt=true;
    bool hold_receipt=false,gold_receipt=true,exit_receipt=true;
    bool gold_reward=false,gold_paid=false,source_complete=false;
    int bank_calls=0,hold_calls=0,gold_calls=0,exit_calls=0;
    int tribute_ticks=0,submitted=0,gold=0,kills=0;
    int gold_members=2,gold_capture_ticks=0;
    int held_source=-1,hold_started=-1,physical_exits=0;
    heuristics::CaptureWatch watch;
    lc::TerminalFacts terminal_facts()const{return f;}
    bool bank_terminal(){
        ++bank_calls;
        if(!alive||!allowed||!on_altar||!bank_receipt||!timing.start_fits(f.round,f.bag))return false;
        tribute_ticks=timing.duration(f.bag);f.channel=true;
        return true;
    }
    bool hold_terminal(int id){
        ++hold_calls;
        assert(id==f.source&&f.held_pad&&f.held_reward);
        if(watch.expired(id,f.round,timing.capture))return exit_terminal();
        held_source=id;if(hold_started<0)hold_started=f.round;
        return hold_receipt;
    }
    bool gold_terminal(){
        ++gold_calls;
        if(!alive||!allowed||!gold_receipt)return false;
        if(gold_members>0){--gold_members;++kills;source_complete=gold_members==0;return true;}
        gold_reward=true;
        if(gold_capture_ticks==0)gold_capture_ticks=timing.capture;
        return false; // Physical holding is productive, but no action receipt.
    }
    bool exit_terminal(){
        ++exit_calls;
        if(!alive||!allowed||!exit_receipt)return false;
        ++physical_exits;f.held_pad=false;return true;
    }
    void death(){alive=false;allowed=false;f.channel=false;tribute_ticks=0;f.bag=0;}
    void respawn(){alive=true;allowed=true;f.source=-1;f.held_pad=false;f.held_reward=false;}
    void owner_end(){if(f.channel&&tribute_ticks>0)--tribute_ticks;}
    void next_owner(int captured_gems=0,int captured_gold=0){
        ++f.round;
        // The actual engine phase order: capture first, then owner-start channel.
        f.bag+=captured_gems;gold+=captured_gold;
        if(f.channel&&tribute_ticks==0){submitted+=f.bag;f.bag=0;f.channel=false;}
    }
    void gold_capture_phase(){
        if(gold_reward&&source_complete&&gold_capture_ticks>0&&--gold_capture_ticks==0){gold+=40;gold_paid=true;gold_reward=false;}
    }
};

void timing_cases(){
    lc::Timing t;
    assert(t.duration(1)==1&&t.duration(10)==1&&t.duration(11)==2);
    assert(t.reserve(0)==24&&t.reserve(250)==35);
    assert(t.start_fits(1999,1)&&!t.start_fits(2000,1));
    assert(!t.start_fits(1999,11)&&!t.start_fits(1999,0));
    // Runtime settings, not the catalog's copied 0/1/.1 constants, own D.
    t.base=3;t.minimum=5;t.multiplier=.25;t.capture=7;
    assert(t.duration(1)==5&&t.duration(12)==6&&t.reserve(100)==42);
    t.horizon=73;assert(t.start_fits(68,1)&&!t.start_fits(69,1));
    t=lc::Timing{};
    // Already-held progress is unknown and can pay next phase. A new fresh
    // gold capture still reserves its initial capture phase and full timer.
    assert(t.held_fits(1998,0)&&!t.held_fits(1999,0));
    assert(t.gold_fits(1999,0,0,true)&&!t.gold_fits(1999,0,0,false));
    assert(t.gold_fits(1990,2,1.2f)&&!t.gold_fits(1990,3,1.2f));
}

void irreversible_and_ablation_cases(){
    lc::Clock c;lc::Timing t;World w;w.f.round=1975;w.f.bag=1;
    assert(!c.enter(t,w.f.round,w.f.bag,7));
    assert(lc::execute_terminal(c,t,w).plan==lc::Program::Ordinary);
    ++w.f.round;assert(c.enter(t,w.f.round,w.f.bag,7));
    assert(c.terminal()&&c.entered()==1976&&c.held()==-1);
    auto r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&r.acted&&w.bank_calls==1);
    assert(w.f.bag==1&&w.submitted==0); // Start is not completion.
    w.owner_end();w.next_owner();assert(w.f.bag==0&&w.submitted==1);
    r=lc::execute_terminal(c,t,w);
    assert(c.terminal()&&r.plan==lc::Program::Gold&&w.gold_calls==1);
    w.death();assert(c.terminal());
    assert(!c.enter(t,1980,0,9)); // Death cannot create a second cutover hold.
    w.respawn();w.f.round=1980;
    r=lc::execute_terminal(c,t,w);
    assert(c.terminal()&&c.entered()==1976&&c.held()==-1);
    assert(r.plan==lc::Program::Gold&&w.gold_calls==2&&w.bank_calls==1);
    lc::Clock disabled;
    for(int round:std::array{1976,1999,2000}){
        assert(!disabled.enter(t,round,200,7,false));w.f.round=round;
        assert(lc::execute_terminal(disabled,t,w).plan==lc::Program::Ordinary);
    }
    assert(!disabled.terminal());
    {
        // A real earlier two-tick channel can finish a much larger capture
        // bag. Cutover at remaining40 uses D(300)=30, then the already-running
        // channel banks at remaining39. Recomputing terminal from empty bag
        // would incorrectly reopen ordinary production until remaining24.
        lc::Clock large;World overlap;overlap.f.round=1959;overlap.f.bag=11;
        assert(overlap.bank_terminal());overlap.owner_end();
        overlap.next_owner(289);assert(overlap.f.round==1960&&overlap.f.bag==300);
        assert(t.reserve(overlap.f.bag)==40&&large.enter(t,1960,300,-1));
        assert(lc::execute_terminal(large,t,overlap).plan==lc::Program::Channel);
        overlap.owner_end();overlap.next_owner();
        assert(overlap.submitted==300&&overlap.f.bag==0&&t.horizon-overlap.f.round>t.reserve(0));
        assert(lc::execute_terminal(large,t,overlap).plan==lc::Program::Gold&&large.terminal());
        overlap.death();overlap.respawn();
        assert(lc::execute_terminal(large,t,overlap).plan==lc::Program::Gold&&large.entered()==1960);
    }
}

void priority_and_receipt_cases(){
    lc::Clock c;lc::Timing t;World w;
    w.f.round=1976;w.f.source=7;w.f.held_pad=w.f.held_reward=true;
    assert(c.enter(t,w.f.round,0,7));
    w.f.bag=1; // A fresh payout must preempt a tempting held/new gem job.
    auto r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&r.acted);
    assert(w.bank_calls==1&&w.hold_calls==0&&w.gold_calls==0&&c.held()==-1);
    assert(w.submitted==0&&w.f.bag==1);
    w.owner_end();w.next_owner();assert(w.submitted==1);
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Gold&&r.acted&&w.hold_calls==0);
    // Plan selection does not fabricate a refused engine action.
    w.f.bag=5;w.bank_receipt=false;
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&!r.acted&&w.submitted==1&&w.f.bag==5);
    w.bank_receipt=true;w.allowed=false;
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&!r.acted);
    w.allowed=true;r=lc::execute_terminal(c,t,w);
    assert(r.acted&&w.f.channel&&w.f.bag==5&&w.submitted==1);
    // A successful irreversible start cannot lose its receipt merely because
    // the next decision sees a channel rather than a Bank plan.
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Channel&&!r.acted&&w.bank_calls==4);
    w.owner_end();w.next_owner();assert(w.submitted==6);
}

void final_tick_and_overlap_cases(){
    lc::Clock c;lc::Timing t;World w;w.f.round=1999;w.f.bag=1;
    assert(c.enter(t,w.f.round,w.f.bag,-1));
    auto r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&r.acted&&w.submitted==0);
    w.owner_end();assert(w.f.channel&&w.tribute_ticks==0&&w.submitted==0);
    w.next_owner();assert(w.f.round==2000&&w.submitted==1&&!w.f.channel);
    World late;late.f.round=2000;late.f.bag=1;
    r=lc::execute_terminal(c,t,late);
    assert(r.plan==lc::Program::Bank&&!r.acted&&!late.f.channel&&late.submitted==0);
    // A loaded actor begins tribute while holding a cleared pad adjacent to
    // the altar. Capture is allowed to add much more than the start bag; no
    // P + D(newbag) refusal or bag-growth restart may replace that channel.
    World overlap;overlap.f.round=1998;overlap.f.bag=1;
    overlap.f.source=7;overlap.f.held_pad=overlap.f.held_reward=true;
    r=lc::execute_terminal(c,t,overlap);
    assert(r.plan==lc::Program::Bank&&r.acted&&overlap.tribute_ticks==1);
    overlap.owner_end();overlap.next_owner(100,30);
    assert(overlap.submitted==101&&overlap.gold==30&&overlap.f.bag==0);
    // While an actual two-tick channel is running, even a larger bag and
    // damage that does not make current exposure unsafe preserve it first.
    World running;running.f.round=1998;running.f.bag=11;
    assert(lc::execute_terminal(c,t,running).acted);
    running.f.bag=31;running.f.held_reward=true;running.f.safe=true;
    assert(!t.start_fits(1998,running.f.bag));
    r=lc::execute_terminal(c,t,running);
    assert(r.plan==lc::Program::Channel&&!r.acted&&running.bank_calls==1&&running.tribute_ticks==2);
    running.owner_end();running.next_owner();running.owner_end();running.next_owner();
    assert(running.submitted==31&&running.f.round==2000);
}

void held_reward_and_gold_cases(){
    lc::Timing t;lc::Clock c;World w;
    w.f.round=1998;w.f.source=7;w.f.held_pad=w.f.held_reward=true;
    assert(c.enter(t,w.f.round,0,7));
    auto r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Held&&!r.acted&&w.hold_calls==1&&w.bank_calls==0);
    w.next_owner(7,40);assert(w.f.round==1999&&w.f.bag==7&&w.submitted==0);
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Bank&&r.acted&&c.held()==-1&&w.submitted==0);
    w.owner_end();w.next_owner();assert(w.submitted==7);
    // Finishing or abandoning the one cutover-held source cannot admit a new
    // gem campaign, even if that fresh source is cleared and physically held.
    w.f.source=8;w.f.bag=0;
    r=lc::execute_terminal(c,t,w);
    assert(r.plan==lc::Program::Gold&&w.hold_calls==1);
    for(int refusal=0;refusal<4;++refusal){
        lc::Clock refused;World f;f.f.round=1976;f.f.source=7;
        f.f.held_reward=f.f.held_pad=true;
        assert(refused.enter(t,f.f.round,0,7));
        if(refusal==0)f.f.held_pad=false;
        if(refusal==1)f.f.held_reward=false;
        if(refusal==2)f.f.source=8;
        if(refusal==3)f.f.safe=false;
        const auto q=lc::execute_terminal(refused,t,f);
        assert(refused.held()==-1&&f.hold_calls==0);
        assert(q.plan==(refusal==3?lc::Program::Exit:lc::Program::Gold));
    }
    // Empty terminal work actually dispatches actions, clears ALL members and
    // physically captures gold; gold income itself supplies no gem bank.
    lc::Clock gold_clock;World g;g.f.round=1976;
    assert(gold_clock.enter(t,g.f.round,0,-1));
    assert(lc::execute_terminal(gold_clock,t,g).acted&&g.gold_members==1);
    assert(!g.source_complete&&g.gold==0&&!g.gold_paid);
    assert(lc::execute_terminal(gold_clock,t,g).acted&&g.gold_members==0);
    assert(g.source_complete&&g.kills==2&&g.gold==0&&!g.gold_paid);
    assert(!lc::execute_terminal(gold_clock,t,g).acted&&g.gold_reward);
    for(int tick=0;tick<t.capture;++tick)g.gold_capture_phase();
    assert(g.gold_paid&&g.gold==40&&g.f.bag==0&&g.submitted==0&&g.bank_calls==0);
    assert(lc::execute_terminal(gold_clock,t,g).plan==lc::Program::Gold&&gold_clock.terminal());
    // This is the same admission helper called by production selection/work.
    assert(lc::gold_admitted(0,40,true,true));
    assert(!lc::gold_admitted(7,400,true,true)); // Attractive gems remain forbidden.
    assert(!lc::gold_admitted(0,0,true,true));
    assert(!lc::gold_admitted(0,40,false,true));
    assert(!lc::gold_admitted(0,40,true,false));
    // Hidden contest makes a held reward unpaid: elapsed production watchdog
    // survives no receipt and ends in an actual physical departure.
    lc::Clock timed;World h;h.f.round=1976;h.f.source=7;
    h.f.held_pad=h.f.held_reward=true;assert(timed.enter(t,h.f.round,0,7));
    for(int age=0;age<7;++age){h.f.round=1976+age;lc::execute_terminal(timed,t,h);assert(!h.physical_exits);}
    h.f.round=1983;r=lc::execute_terminal(timed,t,h);
    assert(r.plan==lc::Program::Held&&r.acted&&h.physical_exits==1&&!h.f.held_pad);
    r=lc::execute_terminal(timed,t,h);
    assert(r.plan==lc::Program::Gold&&timed.held()==-1);
}

void lifecycle_and_guard_cases(){
    lc::SpawnRest rest;
    assert(rest.entry(100,true,true));
    for(int round=101;round<110;++round)assert(!rest.entry(round,true,true));
    assert(!rest.entry(110,false,true)&&rest.entry(111,true,true));
    rest.death();assert(rest.entry(172,true,true)&&!rest.entry(173,true,true));
    lc::SpawnRest full;assert(!full.entry(200,true,false));
    assert(!full.entry(201,true,true)); // Continued chip is not a new arrival.
    lc::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,0);
    assert(!paid.receipt(21,false,30,7)&&paid.source()==7);
    assert(paid.receipt(21,true,30,7)&&paid.source()==-1);
    assert(!paid.receipt(21,true,30,7));
    paid.expect(7);paid.hold(8,22,30,7);
    assert(!paid.possible(23,60,14)); // Ally/other-pad payout cannot establish ours.
    paid.hold(7,22,30,7);assert(!paid.receipt(23,true,30,7));
    paid.completed(7);assert(paid.source()==-1);
    paid.expect(7);paid.hold(7,30,30,7);paid.reset();
    assert(!paid.receipt(31,true,60,14)); // Death/conversion clears old expectation.
    lc::RouteWatch route;route.adopt(3,10);
    for(int step=0;step<=50;++step)route.measured({20,20},50-step,10+step);
    assert(!route.stale(67,180)&&route.stale(68,180));
    lc::Renewal renewal;assert(renewal.due(180,180,false,false));renewal.begin(180);
    assert(!renewal.due(359,359,false,false));renewal.begin(360);
    assert(renewal.attempts()==2&&!renewal.due(2000,2000,false,false));
    assert(lc::has_action(false,false,true)&&!lc::has_action(false,false,false));
    assert(!lc::cast_compatible(true,true,true)&&lc::cast_compatible(true,false,true));
    assert(lc::component_admitted(80000)&&!lc::component_admitted(79999));
}

struct RespawnWorld {
    int turn=100,timer=0,quarantines=0,departures=0,alternates=0,recoveries=0;
    int until=0,last_source=-1,actual_moves=0;
    bool on_pad=true,depart_result=true,recover_result=true,nested_refusal=false;
    lc::WorkResult alternate_result{};
    void quarantine_respawn(int source){
        ++quarantines;last_source=source;until=lc::respawn_quarantine(turn,timer);
    }
    bool on_respawn_pad(int source)const{return on_pad&&source==last_source;}
    bool depart_respawn_pad(int source){
        assert(source==last_source);++departures;
        if(!depart_result)return false;
        ++actual_moves;on_pad=false;return true;
    }
    lc::WorkResult alternate_respawn_work(){
        ++alternates;assert(alternates<=1);
        // Runtime's one alternate calls its work path with retry disabled.
        if(nested_refusal)return lc::handle_respawn_refusal(*this,45,false);
        return alternate_result;
    }
    bool recover_after_respawn(){++recoveries;return recover_result;}
};

void respawn_refusal_cases(){
    assert(lc::respawn_quarantine(100,0)==112);
    assert(lc::respawn_quarantine(100,1)==112);
    assert(lc::respawn_quarantine(100,12)==112);
    assert(lc::respawn_quarantine(100,60)==160);
    {
        RespawnWorld w;
        const auto r=lc::handle_respawn_refusal(w,44,true);
        assert(r.accepted&&r.acted&&w.until==112&&w.quarantines==1);
        assert(w.actual_moves==1&&!w.on_pad&&w.departures==1);
        assert(!w.alternates&&!w.recoveries);
    }
    {
        RespawnWorld w;w.depart_result=false;w.alternate_result={true,false};
        const auto r=lc::handle_respawn_refusal(w,44,true);
        // An alternate productive capture hold needs no action receipt, and
        // must survive without being replaced by recovery just for not moving.
        assert(r.accepted&&!r.acted&&w.departures==1&&w.alternates==1);
        assert(w.recoveries==0&&w.actual_moves==0&&w.on_pad);
    }
    {
        RespawnWorld w;w.depart_result=false;w.alternate_result={false,true};
        const auto r=lc::handle_respawn_refusal(w,44,true);
        // A successful action survives even if fresh state rejects its plan.
        assert(!r.accepted&&r.acted&&w.alternates==1&&w.recoveries==0);
    }
    {
        RespawnWorld w;w.depart_result=false;w.alternate_result={false,false};
        const auto r=lc::handle_respawn_refusal(w,44,true);
        assert(r.accepted&&r.acted&&w.alternates==1&&w.recoveries==1);
        assert(w.departures==1&&w.actual_moves==0);
    }
    {
        RespawnWorld w;w.depart_result=false;w.recover_result=false;
        const auto r=lc::handle_respawn_refusal(w,44,true);
        assert(!r.accepted&&!r.acted&&w.alternates==1&&w.recoveries==1);
        assert(w.until==112&&w.actual_moves==0);
    }
    {
        RespawnWorld w;w.on_pad=false;w.timer=31;
        const auto r=lc::handle_respawn_refusal(w,44,false);
        assert(r.accepted&&r.acted&&w.until==131);
        assert(w.departures==0&&w.alternates==0&&w.recoveries==1);
    }
    {
        RespawnWorld w;w.depart_result=false;w.nested_refusal=true;
        const auto r=lc::handle_respawn_refusal(w,44,true);
        assert(r.accepted&&r.acted&&w.quarantines==2&&w.last_source==45);
        assert(w.alternates==1&&w.departures==2&&w.recoveries==1);
    }
}

void policy_cases(){
    lc::Policy p;using P=heuristics::Purchase;
    static_assert(lc::Policy::roster==std::array{0,7,3,4,2});
    assert(p.purchase(11,0,0)==P::monster_focus);
    assert(p.purchase(8,0,0)==P::monster_strength);
    assert(p.purchase(11,5,0)==P::jungle_health);
    assert(p.purchase(11,5,5)==P::power_focus);
    assert(p.purchase(8,5,5)==P::power_health);
    heuristics::State s;s.hp=s.maximum=900;s.altar=10;
    heuristics::Job short_job;short_job.id=1;short_job.gems=10;short_job.gold=30;
    short_job.travel=3;short_job.work=6;short_job.altar=10;short_job.affordable=true;
    auto long_job=short_job;long_job.id=2;long_job.work=20;
    std::array jobs{short_job,long_job};assert(heuristics::choose(p,s,jobs)==1);
    jobs[1]=short_job;jobs[1].id=2;jobs[0].allies=2;
    assert(heuristics::choose(p,s,jobs)==2);
    jobs[0].ready=true;assert(heuristics::choose(p,s,jobs)==1);
    s.bag=11;assert(!p.bank(s));s.bag=12;assert(p.bank(s));
    s.bag=1;s.hp=557;assert(p.bank(s));s.hp=900;s.round=1990;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
}

int main(){
    timing_cases();irreversible_and_ablation_cases();priority_and_receipt_cases();
    final_tick_and_overlap_cases();held_reward_and_gold_cases();lifecycle_and_guard_cases();respawn_refusal_cases();policy_cases();
    std::puts("liquidation-clock: production terminal dispatcher, timing, receipts, lifecycle and gold continuation passed");
}
