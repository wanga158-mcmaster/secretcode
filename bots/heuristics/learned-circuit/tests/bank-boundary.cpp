#include "../src/policy.h"
#include <cassert>
#include <vector>

using namespace h12_learned_circuit;
static heuristics::Job source(int id) {
    heuristics::Job j;j.id=id;j.gold=40;j.travel=2;j.work=1;j.altar=4;
    j.affordable=true;j.known=true;return j;
}
int main() {
    Policy p;auto&c=p.circuit;
    c.count=2;c.cursor=1;c.boundary=false;c.laps=2;c.lap_payouts=1;c.total_payouts=1;
    c.replaced=true;c.replacements=1; // stop2 was replaced by15 at148
    c.stops[0]=Stop{15,0,1,169,229};c.stops[1]=Stop{5,2};
    c.failed(5,181,5);
    assert(c.cursor==2&&c.stage==Stage::bank&&c.end_bank&&c.stops[1].failures==3);
    heuristics::State s;s.round=182;s.limit=2000;s.hp=s.maximum=900;s.bag=5;s.gold=26;s.altar=4;
    const std::vector<heuristics::Job> jobs{source(11),source(15),source(5)};
    // A bank-walk refusal may expose the existing one-detour fallback.
    assert(p.select(s,jobs,false)==11&&c.detour==11&&c.probed);
    c.payout(11,242,5,60);s.round=242;s.gold=66;
    assert(c.cursor==2&&c.detour<0&&c.total_payouts==2&&c.stops[0].payouts==1);
#if CIRCUIT_BANK_BOUNDARY
    assert(c.stage==Stage::bank&&p.bank(s)&&c.end_bank);
    for(int round=243;round<249;++round) {
        s.round=round;assert(p.select(s,jobs,false)==-1);
        assert(c.stage==Stage::bank&&p.bank(s)&&c.cursor==2&&c.total_banks==0);
    }
    c.interrupt(true,249);assert(c.stage==Stage::recover);
    c.recovered(255);assert(c.stage==Stage::bank&&c.end_bank);
    // Only the existing actual-bank receipt closes this paid lap.
    c.banked(260);s.round=260;s.bag=0;
    assert(c.boundary&&c.cursor==0&&c.laps==3&&c.profitable_laps==1&&c.total_banks==1);
    assert(c.stops[0].id==15&&c.stops[0].payouts==1&&c.stops[1].failures==3);
    c.plan(s,jobs);
    assert(c.stops[0].id==15&&c.stops[0].payouts==1&&c.stops[1].id==11&&c.replacements==2);
    // A normal missing-stop detour still resumes its unpaid ordered visit.
    Circuit ordinary;ordinary.count=2;ordinary.cursor=0;ordinary.boundary=false;
    ordinary.stops[0].id=15;ordinary.stops[1].id=5;ordinary.detour=11;ordinary.probed=true;
    ordinary.payout(11,20,2,60);
    assert(ordinary.cursor==0&&ordinary.stage==Stage::visit&&!ordinary.end_bank&&ordinary.current()==15);
#else
    // Reproduce the exact exhausted-cursor failure on the retained old branch.
    assert(c.stage==Stage::visit&&!p.bank(s)&&c.current()==-1&&c.end_bank);
    assert(p.select(s,jobs,false)==-1&&c.stage==Stage::visit&&c.laps==2&&c.total_banks==0);
#endif
}
