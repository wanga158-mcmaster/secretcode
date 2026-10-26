#define HEURISTICS_ENABLED 1
#define CIRCUIT_YIELD_RENEWAL 1
#define CIRCUIT_EARNED_RENEWAL 1
#include "../learned-circuit/src/policy.h"
#include <cassert>
#include <vector>

namespace circuit=h12_learned_circuit;
using heuristics::Job;
using heuristics::State;

static Job source(int id,int gems,int travel=1) {
    Job j;j.id=id;j.gems=gems;j.gold=20;j.travel=travel;j.altar=2;
    j.work=2;j.original_hp=j.remaining_hp=120;j.affordable=true;j.known=true;
    return j;
}

// This adapter feeds the production receipt and program with owner-start
// observations and postaction baselines. A nominal group reward never enters
// learning: only the actual private delta is passed through Receipts.
struct RouteWorld {
    circuit::Policy policy;
    circuit::Receipts receipts;
    State s;
    RouteWorld() {
        s.round=10;s.limit=2000;s.hp=s.maximum=1000;s.altar=2;
        receipts.sample(s.round,s.bag,s.gold,true,false,-1,false,false);
    }
    int choose(const std::vector<Job>&jobs) {return policy.select(s,jobs,false);}
    void pay(int id,int own_gems,int own_gold,int nominal_gems=-1,int nominal_gold=-1) {
        assert(policy.circuit.current()==id);
        policy.circuit.holding();
        receipts.after(s.bag,s.gold,true,id,true);
        for(int tick=0;tick<s.capture;++tick) {
            ++s.round;
            const auto g=receipts.sample(s.round,s.bag,s.gold,true,false,id,true,false);
            assert(!g.payout&&!g.bank&&policy.circuit.stage==circuit::Stage::hold);
            receipts.after(s.bag,s.gold,true,id,true);
        }
        ++s.round;s.bag+=own_gems;s.gold+=own_gold;
        const auto gain=receipts.sample(s.round,s.bag,s.gold,true,false,id,false,true);
        assert(gain.payout&&!gain.bank&&gain.source==id);
        policy.circuit.measured_payout(gain.source,s.round,s.bag,50,gain.own_gems,gain.own_gold,
            nominal_gems<0?own_gems:nominal_gems,nominal_gold<0?own_gold:nominal_gold);
        receipts.after(s.bag,s.gold,true,id,false);
    }
    void bank() {
        assert(policy.circuit.stage==circuit::Stage::bank&&policy.bank(s));
        const int ticks=std::max(1,int(std::ceil(s.bag*s.tribute)));
        receipts.submit(s.round,s.bag,s.tribute);
        receipts.after(s.bag,s.gold,true,-1,false);
        for(int tick=1;tick<ticks;++tick) {
            ++s.round;
            const auto g=receipts.sample(s.round,s.bag,s.gold,true,true,-1,false,false);
            assert(!g.bank&&!g.payout);
            receipts.after(s.bag,s.gold,true,-1,false);
        }
        ++s.round;s.bag=0;
        const auto gain=receipts.sample(s.round,s.bag,s.gold,true,false,-1,false,false);
        assert(gain.bank&&!gain.payout);
        policy.circuit.banked(s.round);
        receipts.after(s.bag,s.gold,true,-1,false);
    }
};

static void paid_ordered_laps() {
    RouteWorld w;
    std::vector<Job> initial{source(10,7),source(20,5,3)};
    assert(w.choose(initial)==10);
    assert(w.policy.circuit.count==2&&w.policy.circuit.stops[1].id==20);
    std::vector<Job> distraction=initial;
    distraction.push_back(source(30,100,0));
    for(int lap=0;lap<2;++lap) {
        if(lap)assert(w.choose(distraction)==10);
        // A better new source cannot rewrite the actor's productive program.
        assert(w.choose(distraction)==10);
        w.pay(10,3,9); // shared reward, not the nominal seven gems/twenty gold
        assert(w.policy.circuit.current()==20&&w.choose(distraction)==20);
        w.pay(20,2,7);
        // Five gems bank at route end even though the safety cap is 24.
        assert(w.s.bag==5&&w.policy.circuit.end_bank&&w.policy.bank(w.s));
        w.bank();
        assert(w.policy.circuit.boundary&&w.policy.circuit.laps==lap+1);
        ++w.s.round;
    }
    const auto&c=w.policy.circuit;
    assert(c.stops[0].id==10&&c.stops[1].id==20);
    assert(c.stops[0].payouts==2&&c.stops[1].payouts==2);
    assert(c.profitable_laps==2&&c.total_payouts==4&&c.total_banks==2&&c.replacements==0);
}

static void failure_learning() {
    State s;s.round=10;
    std::vector<Job> jobs{source(10,7),source(20,5,3),source(30,3,6),source(40,2,7)};
    circuit::Circuit c;c.plan(s,std::span<const Job>(jobs.data(),2));
    c.failed(10,11,0);c.failed(20,12,0);
    assert(c.boundary&&c.laps==1);
    s.round=13;c.plan(s,jobs);
    assert(c.current()==10&&c.stops[0].failures==1&&c.stops[1].failures==1&&c.replacements==0);
    c.failed(10,14,0);c.failed(20,15,0);
    assert(c.boundary&&c.laps==2);
    s.round=16;c.plan(s,jobs);
    assert(c.replacements==1&&c.replaced&&c.current()==30);
    assert(c.stops[0].id==30&&c.stops[0].failures==0&&c.stops[1].id==20&&c.stops[1].failures==2);
    // Replanning within the same lap cannot replace a second failed stop.
    s.round=17;c.plan(s,jobs);
    assert(c.replacements==1&&c.stops[1].id==20);
    c.payout(30,18,3,50);
    assert(c.stops[0].payouts==1&&c.stops[0].failures==0&&c.stops[0].due==68&&c.current()==20);
    // Renewing B is deferred without another failure; the replacement remains.
    c.sleeping(20,19,3,55);
    assert(c.stage==circuit::Stage::bank&&c.stops[1].failures==2&&c.stops[1].due==55);
    c.banked(20);s.round=21;c.plan(s,jobs);
    assert(c.stops[0].id==30&&c.stops[0].payouts==1&&c.replacements==2&&c.stops[1].id==10);
}

static void unknown_recovery_and_expiry() {
    circuit::Policy p;State s;s.round=10;
    std::vector<Job> jobs{source(10,7),source(20,5,3)};
    assert(p.select(s,jobs,false)==10);
    const int begun=p.circuit.begun;
    std::vector<Job> omitted{source(99,100,0)};s.round=11;
    assert(p.select(s,omitted,false)==-1);
    assert(p.circuit.current()==10&&p.circuit.stops[0].failures==0&&p.circuit.begun==begun);
    p.circuit.holding();p.circuit.interrupt(true,11);
    assert(p.circuit.stage==circuit::Stage::recover&&p.circuit.recovery_resume==circuit::Stage::hold);
    p.circuit.recovered(12);
    assert(p.circuit.stage==circuit::Stage::hold&&p.circuit.current()==10&&p.circuit.stops[0].failures==0);
    p.circuit.interrupt(false,12);
    assert(p.circuit.stage==circuit::Stage::bank&&!p.circuit.end_bank);
    p.circuit.banked(12);
    assert(p.circuit.stage==circuit::Stage::hold&&p.circuit.current()==10&&!p.circuit.boundary);
    p.circuit.died(13);p.circuit.recovered(74);
    assert(p.circuit.stage==circuit::Stage::visit&&p.circuit.current()==10&&p.circuit.stops[0].failures==1&&p.circuit.boundary);

    circuit::Circuit c;c.visit(100);c.observe_progress(100,10,1000);
    assert(!c.expired(111)&&c.expired(112));
    // Equal or worse oscillating distances buy no new progress lease.
    c.observe_progress(111,11,1000);assert(c.expired(112));
    c.observe_progress(112,9,1000);assert(!c.expired(123)&&c.expired(124));
    for(int round=113;round<220;++round)c.observe_progress(round,9,float(1113-round));
    assert(!c.expired(219)&&c.expired(220)); // hard excursion horizon despite ongoing work
}

static void lost_route_end_bag_restarts_execution() {
    RouteWorld w;
    const std::vector<Job> jobs{source(10,7),source(20,5,3)};
    assert(w.choose(jobs)==10);
    w.pay(10,3,9);w.pay(20,2,7);
    auto&c=w.policy.circuit;
    assert(c.cursor==c.count&&c.current()==-1&&c.stage==circuit::Stage::bank&&c.end_bank);
    assert(c.total_payouts==2&&c.lap_payouts==2&&w.s.bag==5);
    ++w.s.round;w.s.bag=0;
    const auto dead=w.receipts.sample(w.s.round,0,w.s.gold,false,false,-1,false,false);
    assert(!dead.bank&&!dead.payout);
    c.died(w.s.round);
    assert(c.stage==circuit::Stage::recover&&c.boundary&&c.cursor==0&&c.current()==10&&!c.end_bank);
    assert(c.laps==0&&c.profitable_laps==0&&c.total_banks==0&&c.lap_payouts==0);
    assert(c.total_payouts==2&&c.stops[0].payouts==1&&c.stops[1].payouts==1);
    assert(c.stops[0].last_paid>=0&&c.stops[1].last_paid>=0);
    c.recovered(w.s.round+61);w.s.round+=61;
    assert(w.choose(jobs)==10&&c.stage==circuit::Stage::visit&&!c.boundary);
    assert(c.current()>=0&&c.cursor<c.count&&c.laps==0&&c.total_banks==0);
}

static void early_bank_survives_recovery() {
    for(const auto original:{circuit::Stage::visit,circuit::Stage::hold}) {
        circuit::Circuit c;State s;s.round=10;
        const std::vector<Job> jobs{source(10,7),source(20,5,3)};
        c.plan(s,jobs);
        // Interrupt B in an incomplete lap so both cursor and payout history
        // must survive the nested bank/recover continuation.
        c.payout(10,16,3,50);
        if(original==circuit::Stage::hold)c.holding();
        assert(c.stage==original&&c.current()==20&&c.cursor==1&&c.lap_payouts==1);
        c.interrupt(false,17);
        assert(c.stage==circuit::Stage::bank&&c.resume==original&&!c.end_bank);
        c.interrupt(true,18);
        assert(c.stage==circuit::Stage::recover&&c.recovery_resume==circuit::Stage::bank&&c.resume==original);
        c.recovered(24);
        assert(c.stage==circuit::Stage::bank&&c.current()==20&&c.cursor==1);
        c.banked(25);
        assert(c.stage==original&&c.current()==20&&c.cursor==1&&!c.boundary&&!c.end_bank);
        assert(c.total_banks==1&&c.laps==0&&c.profitable_laps==0&&c.lap_payouts==1);
        s.round=26;c.plan(s,jobs);
        assert(c.stage==original&&c.current()==20&&c.cursor==1&&c.stops[0].payouts==1);
    }
}

static void deliberate_interruptions_pause_work_leases() {
    const std::vector<Job> jobs{source(10,7),source(20,5,3)};
    for(const auto original:{circuit::Stage::visit,circuit::Stage::hold}) {
        circuit::Circuit recovery;State s;s.round=10;recovery.plan(s,jobs);
        if(original==circuit::Stage::hold)recovery.holding();
        recovery.observe_progress(15,4,50);
        recovery.interrupt(true,15);
        recovery.interrupt(true,30); // repeated recovery turns do not restart/count the pause
        assert(recovery.paused==15&&recovery.recoveries==1);
        recovery.recovered(50); // 35 deliberate recovery rounds exceed a work lease
        assert(recovery.stage==original&&recovery.current()==10&&recovery.cursor==0);
        assert(recovery.begun==45&&recovery.progress==50&&recovery.paused<0&&!recovery.expired(50));
        assert(recovery.best_distance==10000&&recovery.best_hp>1e20f);
        assert(recovery.stops[0].failures==0&&recovery.laps==0&&recovery.profitable_laps==0);
        // Resumed observations establish fresh travel/HP references even if
        // farther or healthier than before the interruption.
        recovery.observe_progress(51,12,120);
        assert(recovery.progress==51&&recovery.best_distance==12&&recovery.best_hp==120);
        assert(!recovery.expired(62)&&recovery.expired(63));

        circuit::Circuit banking;s.round=10;banking.plan(s,jobs);
        if(original==circuit::Stage::hold)banking.holding();
        banking.observe_progress(15,4,50);banking.interrupt(false,15);banking.banked(50);
        assert(banking.stage==original&&banking.current()==10&&banking.cursor==0&&!banking.expired(50));
        assert(banking.begun==45&&banking.progress==50&&banking.paused<0&&banking.recoveries==0);
        assert(banking.total_banks==1&&banking.laps==0&&banking.profitable_laps==0&&banking.stops[0].failures==0);

        circuit::Circuit nested;s.round=10;nested.plan(s,jobs);nested.payout(10,16,3,50);
        if(original==circuit::Stage::hold)nested.holding();
        nested.observe_progress(20,3,40);nested.interrupt(false,20);nested.interrupt(true,40);
        assert(nested.paused==20&&nested.recoveries==0&&nested.recovery_resume==circuit::Stage::bank);
        nested.recovered(70);
        assert(nested.stage==circuit::Stage::bank&&nested.paused==20&&nested.begun==16&&nested.progress==20);
        nested.banked(90);
        assert(nested.stage==original&&nested.current()==20&&nested.cursor==1&&!nested.expired(90));
        assert(nested.begun==86&&nested.progress==90&&nested.paused<0&&nested.recoveries==0);
        assert(nested.stops[1].failures==0&&nested.total_banks==1&&nested.laps==0&&nested.profitable_laps==0&&nested.lap_payouts==1);
    }
}

static void unpaid_recoveries_and_active_horizon_stay_bounded() {
    circuit::Circuit c;State s;s.round=100;
    const std::vector<Job> jobs{source(10,7),source(20,5,3)};c.plan(s,jobs);
    for(int number=1;number<=3;++number) {
        const int start=100+number*40,end=start+20;
        c.observe_progress(start,10-number,1000-number*100.f);
        c.interrupt(true,start);c.recovered(end);
        c.observe_progress(end,9-number,900-number*100.f);
        assert(c.recoveries==number&&c.current()==10&&c.stops[0].failures==0);
        assert(c.expired(end)==(number>2)); // two unpaid recoveries, then stop despite renewed progress
    }
    assert(c.laps==0&&c.profitable_laps==0&&c.total_banks==0);

    circuit::Circuit active;active.plan(s,jobs);
    active.observe_progress(160,2,100);active.interrupt(false,160);active.banked(200);
    assert(active.begun==140&&active.progress==200&&!active.expired(200));
    // Forty bank rounds are excluded, while the sixty prebank work rounds
    // remain charged against the same absolute 120 active-round excursion.
    for(int round=201;round<=260;++round)active.observe_progress(round,2,float(1000-round));
    assert(!active.expired(259)&&active.expired(260));
    assert(active.current()==10&&active.cursor==0&&active.total_banks==1&&active.laps==0&&active.profitable_laps==0);
}

static void lethal_work_teaches_a_failed_visit() {
    State s;s.round=10;
    const std::vector<Job> learned{source(10,7),source(20,5,3)};
    std::vector<Job> alternatives=learned;alternatives.push_back(source(30,100,0));
    circuit::Circuit c;c.plan(s,learned);
    c.payout(10,16,3,50);c.payout(20,22,5,50);c.banked(25);
    assert(c.laps==1&&c.profitable_laps==1&&c.total_banks==1);
    s.round=26;c.plan(s,alternatives);assert(c.current()==10);
    c.died(27);
    assert(c.stops[0].failures==1&&c.stops[1].failures==0);
    assert(c.stops[0].payouts==1&&c.stops[1].payouts==1&&c.total_payouts==2);
    assert(c.boundary&&c.laps==1&&c.profitable_laps==1&&c.total_banks==1);
    c.recovered(88);s.round=88;c.plan(s,alternatives);
    assert(c.current()==10&&c.replacements==0); // one lethal visit retains the learned stop
    c.holding();c.interrupt(true,89);c.died(95); // lethal unfinished work during recovery also teaches
    assert(c.stops[0].failures==2&&c.stops[1].payouts==1);
    c.recovered(156);s.round=156;c.plan(s,alternatives);
    assert(c.current()==30&&c.replacements==1&&c.stops[1].id==20);
    assert(c.stops[1].payouts==1&&c.stops[1].failures==0);
    assert(c.laps==1&&c.profitable_laps==1&&c.total_banks==1);
    c.payout(30,162,2,50);
    assert(c.stops[0].payouts==1&&c.stops[0].failures==0&&c.current()==20);
}

static void deaths_on_bank_and_probe_do_not_blame_stops() {
    const std::vector<Job> learned{source(10,7),source(20,5,3)};
    for(const bool nested_recovery:{false,true}) {
        circuit::Circuit c;State s;s.round=10;c.plan(s,learned);
        c.payout(10,16,3,50);
        assert(c.current()==20&&c.cursor==1&&c.cursor<c.count);
        c.interrupt(false,17);
        if(nested_recovery)c.interrupt(true,18);
        c.died(20);
        assert(c.stops[0].failures==0&&c.stops[1].failures==0);
        assert(c.stops[0].payouts==1&&c.stops[1].payouts==0&&c.total_payouts==1);
        assert(c.laps==0&&c.profitable_laps==0&&c.total_banks==0&&c.lap_payouts==0);
        c.recovered(81);s.round=81;c.plan(s,learned);
        assert(c.current()==10&&c.cursor==0&&!c.boundary);
    }
    for(const auto work_stage:{circuit::Stage::visit,circuit::Stage::hold}) {
        circuit::Circuit c;State s;s.round=10;c.plan(s,learned);
        if(work_stage==circuit::Stage::hold)c.holding();
        c.interrupt(true,11);c.died(12);
        assert(c.stops[0].failures==1&&c.stops[1].failures==0);
    }
    circuit::Circuit probe;State s;s.round=10;probe.plan(s,learned);
    probe.sleeping(10,11,0,60);probe.sleeping(20,12,0,60);
    const std::vector<Job> fresh{source(30,4)};s.round=13;probe.plan(s,fresh);
    assert(probe.current()==30&&probe.detour==30&&probe.stops[0].id==10);
    probe.died(14);
    assert(probe.stops[0].failures==0&&probe.stops[1].failures==0&&probe.detour<0);
}

static circuit::Receipts holding(int bag=0,int gold=0) {
    circuit::Receipts r;
    r.sample(10,bag,gold,true,false,4,true,false);
    r.after(bag,gold,true,4,true);
    return r;
}

static void private_payout_receipts() {
    auto gold_only=holding();
    auto g=gold_only.sample(16,0,6,true,false,4,false,true);
    assert(g.payout&&g.source==4&&!g.bank); // actual personal gold-only share
    auto shared=holding(2,10);
    g=shared.sample(16,3,13,true,false,4,false,true);
    assert(g.payout&&g.source==4); // any positive own share, no nominal amount
    auto no_share=holding();
    assert(!no_share.sample(16,0,0,true,false,4,false,true).payout);
    auto kill=holding();
    assert(!kill.sample(16,0,100,true,false,4,true,false).payout);
    auto no_reward=holding();no_reward.after(0,0,true,4,false);
    assert(!no_reward.sample(16,3,20,true,false,4,false,true).payout);
    auto departure=holding();departure.after(0,0,true,-1,false);
    assert(!departure.sample(16,3,20,true,false,4,false,true).payout);
    auto switched=holding();
    assert(!switched.sample(16,3,20,true,false,5,false,true).payout);
    auto death=holding();
    assert(!death.sample(16,3,20,false,false,-1,false,true).payout);
    assert(!death.sample(77,3,20,true,false,4,false,true).payout);
    // A gain already present in the saved postaction baseline cannot teach a
    // source later, even if an unrelated capture changes its lifecycle.
    auto old_gain=holding();old_gain.after(0,100,true,4,true);
    assert(!old_gain.sample(16,0,100,true,false,4,false,true).payout);
}

static circuit::Receipts liquid(int gems) {
    circuit::Receipts r;r.sample(10,gems,30,true,false,-1,false,false);
    r.after(gems,30,true,-1,false);return r;
}

static void tribute_receipts() {
    auto one=liquid(4);one.submit(10,4,.1);
    // One tick may be gone before this actor ever sees a channelling turn.
    auto g=one.sample(11,0,30,true,false,-1,false,false);
    assert(g.bank&&!g.payout&&one.submit_round<0);
    assert(!one.sample(12,0,30,true,false,-1,false,false).bank);
    auto many=liquid(24);many.submit(10,24,.1);
    assert(!many.sample(11,24,30,true,true,-1,false,false).bank);
    many.after(24,30,true,-1,false);
    assert(!many.sample(12,24,30,true,true,-1,false,false).bank);
    many.after(24,30,true,-1,false);
    assert(many.sample(13,0,30,true,false,-1,false,false).bank);
    auto canceled=liquid(24);canceled.submit(10,24,.1);
    assert(!canceled.sample(11,24,30,true,false,-1,false,false).bank&&canceled.submit_round<0);
    canceled.after(24,30,true,-1,false);
    assert(!canceled.sample(13,0,30,true,false,-1,false,false).bank);
    auto premature=liquid(24);premature.submit(10,24,.1);
    assert(!premature.sample(11,0,30,true,false,-1,false,false).bank);
    auto dead=liquid(4);dead.submit(10,4,.1);
    assert(!dead.sample(11,0,30,false,false,-1,false,false).bank);
    assert(!dead.sample(72,0,30,true,false,-1,false,false).bank);
    auto recall=liquid(4);
    assert(!recall.sample(15,4,30,true,false,-1,false,false).bank);
    auto no_start=liquid(4);
    assert(!no_start.sample(11,0,30,true,false,-1,false,false).bank);
}

static void bounded_fallback_and_finance() {
    int walks=0,works=0;
    assert(circuit::leave_or_work([&]{++walks;return false;},[&]{++works;return true;}));
    assert(walks==1&&works==1);
    assert(circuit::leave_or_work([&]{++walks;return true;},[&]{++works;return true;}));
    assert(walks==2&&works==1);
    assert(!circuit::leave_or_work([]{return false;},[]{return false;}));
    circuit::Policy p;State s;s.bag=1;s.altar=15;
    assert(!p.bank(s));p.circuit.stage=circuit::Stage::bank;assert(p.bank(s));
    p.circuit.stage=circuit::Stage::visit;s.hp=500;assert(p.bank(s));
    s.hp=1000;s.round=1990;assert(p.bank(s));
    s.round=0;p.forced_bank=true;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
    using P=heuristics::Purchase;
    assert(p.purchase(1,0,0)==P::monster_strength&&p.purchase(1,5,0)==P::jungle_strength&&p.purchase(1,5,5)==P::power_health);
    assert(p.purchase(11,0,0)==P::monster_strength&&p.purchase(11,5,0)==P::jungle_health&&p.purchase(11,5,5)==P::power_strength);
    for(const int role:{7,9,5})assert(p.purchase(role,0,0)==P::monster_focus&&p.purchase(role,5,0)==P::jungle_health&&p.purchase(role,5,5)==P::power_focus);
    assert((p.roster==std::array{3,4,0,10,8}));
}

static void capital_phase_return_preserves_the_program() {
    assert(!circuit::capital_phase_due(149,0,5,30,false));
    assert(circuit::capital_phase_due(150,0,5,30,false));
    assert(circuit::capital_phase_due(299,0,5,30,false));
    assert(!circuit::capital_phase_due(89,2,5,30,false));
    assert(circuit::capital_phase_due(90,2,5,30,false));
    // Engine terms are inputs, including a different combination size/price.
    assert(!circuit::capital_phase_due(179,1,7,30,false));
    assert(circuit::capital_phase_due(180,1,7,30,false));
    assert(!circuit::capital_phase_due(299,0,0,0,true));
    assert(circuit::capital_phase_due(300,0,0,0,true));
    assert(!circuit::capital_phase_due(300,0,5,0,false));
    assert(!circuit::capital_phase_due(300,5,5,30,false));
    RouteWorld w;
    const std::vector<Job> jobs{source(10,7),source(20,5,3)};
    assert(w.choose(jobs)==10);
    w.s.gold=150;
    assert(circuit::capital_phase_due(w.s.gold,0,5,30,false));
    const auto stops=w.policy.circuit.stops;
    w.policy.circuit.interrupt(true,w.s.round);
    assert(w.policy.circuit.stage==circuit::Stage::recover);
    w.s.round+=8;w.s.gold=0;
    w.policy.circuit.recovered(w.s.round);
    assert(w.policy.circuit.stage==circuit::Stage::visit);
    assert(w.choose(jobs)==10&&w.policy.circuit.cursor==0&&w.policy.circuit.replacements==0);
    assert(w.policy.circuit.stops[0].id==stops[0].id&&w.policy.circuit.stops[1].id==stops[1].id);
    assert(w.policy.circuit.stops[0].failures==0&&w.policy.circuit.stops[1].failures==0);
}

static RouteWorld two_shared_paid_laps() {
    RouteWorld w;w.s.round=325;w.s.identity=11;
    auto a=source(0,12,1);a.gold=180;
    auto b=source(2,19,10);b.gold=160;
    const std::vector<Job> retained{a,b};
    assert(w.choose(retained)==0);
    w.s.round=355;w.pay(0,4,60,12,180); // actual receipt R361
    w.s.round=387;w.pay(2,6,53,19,160); // actual receipt R393
    w.s.round=418;w.bank();             // closed lap 94 rounds
    assert(w.policy.circuit.paid_laps==1&&w.choose(retained)==0);
    w.s.round+=18;w.pay(0,4,60,12,180);  // next observed visit duration 24
    w.s.round+=24;w.pay(2,6,53,19,160);  // next observed visit duration 30
    w.s.round+=28;w.bank();             // closed lap 83 rounds
    assert(w.policy.circuit.boundary&&w.policy.circuit.paid_laps==2);
    return w;
}

static void paid_yield_renewal_is_bounded_and_admitted() {
    const auto qualified=two_shared_paid_laps();
    auto a=source(0,12,1);a.gold=180;
    auto b=source(2,19,10);b.gold=160;
    auto outside=source(4,26,15);outside.gold=120;outside.work=12;outside.altar=15;
    const std::vector<Job> retained{a,b};
    const std::vector<Job> offered{a,b,outside};
    auto fair=qualified;
    fair.policy.circuit.stops[1].paid={{{9,80,24},{9,80,30}}};
    assert(!fair.policy.circuit.poor_paid_stop(fair.s,fair.policy.circuit.stops[1]));
    fair.s.mature=true;
    assert(!fair.policy.circuit.poor_paid_stop(fair.s,fair.policy.circuit.stops[1]));
    auto missing=qualified;
    assert(missing.choose(retained)==0&&missing.policy.circuit.yield_replacements==0);
    assert(missing.choose(offered)==0&&missing.policy.circuit.yield_replacements==0); // mid-lap
    auto crowded=qualified;outside.allies=1;
    assert(crowded.choose({a,b,outside})==0&&crowded.policy.circuit.yield_replacements==0);
    auto unsafe=qualified;outside.allies=0;outside.affordable=false;
    assert(unsafe.choose({a,b,outside})==0&&unsafe.policy.circuit.yield_replacements==0);
    auto worse=qualified;outside.affordable=true;outside.work=1000;
    assert(worse.choose({a,b,outside})==0&&worse.policy.circuit.yield_replacements==0);
    auto active=qualified;
    assert(active.choose(offered)==4&&active.policy.circuit.yield_replacements==1);
    assert(active.policy.circuit.count==2&&active.policy.circuit.stops[0].id==4&&active.policy.circuit.stops[1].id==2);
    active.pay(4,26,120,26,120);
    assert(active.policy.circuit.current()==2&&active.choose(offered)==2);
    active.pay(2,6,53,19,160);active.bank();
    assert(active.policy.circuit.boundary&&active.policy.circuit.total_banks==3);
    assert(active.policy.circuit.yield_replacements==1&&active.choose(offered)==4);
    // The stagger gates only replacement; every other role continues source0.
    constexpr std::array identities{1,7,11,9,5};
    for(int lap=2;lap<12;++lap)for(int role=0;role<5;++role) {
        auto body=qualified;body.s.identity=identities[role];body.policy.circuit.paid_laps=lap;
        assert(body.policy.circuit.renewal_window(body.s)==(lap%5==role));
        const int chosen=body.choose(offered);
        assert(chosen==(lap%5==role?4:0));
    }
    auto unknown=qualified;unknown.s.identity=0;
    assert(!unknown.policy.circuit.renewal_window(unknown.s)&&unknown.choose(offered)==0);
}

static void earned_renewal_waits_for_execution_without_waiting_for_work() {
    const auto qualified=two_shared_paid_laps();
    auto a=source(0,12,1);a.gold=180;
    auto b=source(2,19,10);b.gold=160;
    auto outside=source(4,26,15);outside.gold=120;outside.work=12;outside.altar=15;
    const std::vector<Job> retained{a,b},offered{a,b,outside};
    constexpr std::array identities{1,7,11,9,5};
    for(int role=0;role<5;++role)for(int refusal=0;refusal<4;++refusal) {
        auto w=qualified;auto&c=w.policy.circuit;
        w.s.identity=identities[role];c.paid_laps=role<2?role+5:role;
        auto quote=outside;
        if(refusal==1)quote.affordable=false;
        if(refusal==2)quote.allies=1;
        if(refusal==3)quote.work=1000;
        const auto jobs=refusal==0?retained:std::vector<Job>{a,b,quote};
        assert(w.choose(jobs)==0&&c.renewal_earned&&c.yield_replacements==0);
        assert(w.choose(offered)==0&&c.cursor==0); // no mid-lap replacement
        w.s.round+=24;w.pay(0,4,60,12,180);assert(w.choose(retained)==2);
        w.s.round+=24;w.pay(2,6,53,19,160);w.s.round+=28;w.bank();
        assert(c.boundary&&c.paid_laps%5!=role&&c.renewal_window(w.s));
        assert(w.choose(offered)==4&&!c.renewal_earned&&c.yield_replacements==1);
        assert(c.count==2&&c.stops[0].id==4&&c.stops[1].id==2);
        w.pay(4,26,120,26,120);assert(w.choose(offered)==2);
        w.pay(2,6,53,19,160);w.bank();
        assert(c.total_banks==4&&c.yield_replacements==1);
    }
    // Permission does not override new personal evidence of fair production.
    auto fair=qualified;auto&c=fair.policy.circuit;
    assert(fair.choose(retained)==0&&c.renewal_earned);
    for(int lap=0;lap<2;++lap) {
        fair.pay(0,12,180,12,180);fair.pay(2,19,160,19,160);fair.bank();
        if(lap==0)assert(fair.choose(retained)==0);
    }
    assert(!c.renewal_window(fair.s)&&fair.choose(offered)==0&&!c.renewal_earned);
    // Cancellation is evaluated for the completed circuit at a normal boundary.
    // Fairness of the first stop can coexist with a newly poor second stop.
    auto transferred=qualified;auto&programme=transferred.policy.circuit;
    programme.stops[1].paid={{{9,80,24},{6,53,30}}};
    assert(transferred.choose(retained)==0&&programme.renewal_earned);
    transferred.s.round+=24;transferred.pay(0,12,180,12,180);
    assert(!programme.poor_paid_stop(transferred.s,programme.stops[0])&&
           !programme.poor_paid_stop(transferred.s,programme.stops[1]));
    transferred.s.round+=24;transferred.pay(2,6,53,19,160);
    transferred.s.round+=28;transferred.bank();
    assert(programme.renewal_window(transferred.s)&&transferred.choose(offered)==0);
    assert(programme.stops[0].id==0&&programme.stops[1].id==4&&
           programme.yield_replacements==1&&!programme.renewal_earned);
    // A roster identity with no earned phase still continues its original work.
    auto unearned=qualified;unearned.s.identity=9;
    assert(!unearned.policy.circuit.renewal_window(unearned.s)&&unearned.choose(offered)==0);
}

int main() {
    paid_ordered_laps();failure_learning();unknown_recovery_and_expiry();
    lost_route_end_bag_restarts_execution();early_bank_survives_recovery();
    deliberate_interruptions_pause_work_leases();unpaid_recoveries_and_active_horizon_stay_bounded();
    lethal_work_teaches_a_failed_visit();deaths_on_bank_and_probe_do_not_blame_stops();
    private_payout_receipts();tribute_receipts();bounded_fallback_and_finance();
    capital_phase_return_preserves_the_program();
    paid_yield_renewal_is_bounded_and_admitted();
    earned_renewal_waits_for_execution_without_waiting_for_work();
}
