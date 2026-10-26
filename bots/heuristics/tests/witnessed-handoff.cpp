#define HEURISTICS_ENABLED 1
#include "../witnessed-handoff/src/policy.h"
#include "../witnessed-handoff/src/flow.h"
#include "../common/src/capture.h"
#include <array>
#include <cassert>
#include <cstdint>

namespace hand=h12_witnessed_handoff;
static void solo_policy() {
    hand::Policy policy;heuristics::State s;s.round=10;s.altar=10;
    heuristics::Job rich;rich.id=1;rich.gems=12;rich.travel=2;rich.work=4;rich.altar=8;
    rich.known=rich.affordable=true;
    auto quiet=rich;quiet.id=2;quiet.gems=8;
    std::array jobs{rich,quiet};
    assert(heuristics::choose(policy,s,jobs)==1);
    jobs[0].allies=2;
    assert(heuristics::choose(policy,s,jobs)==2); // larger shared reward loses to solo work
    jobs[0]=rich;jobs[0].affordable=false;
    assert(heuristics::choose(policy,s,jobs)==2); // executable work precedes an attractive partial
    std::array partial{jobs[0]};
    assert(heuristics::choose(policy,s,partial)==1);
    s.hp=700;assert(heuristics::choose(policy,s,partial)==-1);
    partial[0].committed=true;assert(heuristics::choose(policy,s,partial)==1);

    s.hp=s.maximum;s.bag=1;assert(!policy.bank(s));
    s.altar=3;assert(policy.bank(s));
    s.altar=10;s.bag=12;assert(policy.bank(s));
    s.bag=1;s.limit=28;assert(heuristics::deadline(s)&&policy.bank(s));
    s.bag=0;assert(!policy.bank(s));
    using P=heuristics::Purchase;
    assert(policy.purchase(10,0,0)==P::monster_strength&&policy.purchase(7,0,0)==P::monster_strength);
    assert(policy.purchase(10,5,0)==P::jungle_strength&&policy.purchase(7,5,0)==P::jungle_health);
    assert(policy.purchase(10,5,5)==P::power_strength&&policy.purchase(7,5,5)==P::power_health);
    static_assert(hand::Policy::roster[0]==1&&hand::Policy::roster[1]==4&&hand::Policy::roster[2]==0);
}
struct World {
    struct Actor {
        hand::Controller flow;hand::Receipts receipts;
        int type=10,position=100,bag=0,gold=0;
        float hp=1000;bool alive=true,channel=false,eligible=true;
    };
    std::array<Actor,2> actors;
    std::uint64_t bus=0;
    int round=10,pads=2,capture=5,remaining=-1,depart_round=-1,pay_round=-1,outside_work_round=-1;
    int outside_hp=100,movement_attempts=0,bank_attempts=0,source_gems=8,source_gold=40;
    bool reward=true,renewed=false,visible=true,blocked=false,stay_inside=false,blocked_approach=false;
    bool source_mapping=true,interfere=false,suspend=false;
    World() {
        actors[1].type=7;actors[1].position=99;
        for(auto&a:actors)a.receipts.after(a.bag,a.gold,a.alive,a.position==100||a.position==101?100:-1,reward);
    }
    bool on_source(int actor)const{return actors[actor].position==100||actors[actor].position==101;}
    struct Adapter {
        World&w;int id;
        std::uint64_t read(){return w.bus;}
        bool write_if(std::uint64_t old,std::uint64_t replacement,int round) {
            if(w.interfere){w.interfere=false;w.bus=0;}
            if(w.round!=round||w.bus!=old)return false;
            w.bus=replacement;return true;
        }
        hand::Observation observe(const hand::Packet&p) {
            const auto&a=w.actors[id];hand::Observation o;
            o.round=w.round;o.type=a.type;o.capture=w.capture;o.bag=a.bag;o.hp=a.hp;o.maximum=1000;
            o.alive=a.alive;o.channel=a.channel;o.source_valid=w.source_mapping&&p.tile==100;
            o.reward=w.reward;o.renewed=w.renewed;o.on_source=w.on_source(id);
            o.holder_visible_on_source=w.visible&&w.on_source(1)&&w.actors[1].alive;
            o.eligible=a.eligible;o.usable_pads=w.pads;o.approach=o.on_source?0:1;return o;
        }
        bool approach(int tile) {
            assert(tile==100);
            ++w.movement_attempts;
            if(w.blocked_approach)return false;
            w.actors[id].position=101;return true;
        }
        bool depart(int tile,int outside) {
            assert(tile==100&&outside==2);
            ++w.movement_attempts;
            if(w.blocked)return false;
            w.actors[id].position=w.stay_inside?101:200;
            if(w.suspend)++w.round;
            if(!w.on_source(id))w.depart_round=w.round;return true;
        }
    };
    Adapter adapter(int id){return {*this,id};}
    hand::Proposal proposal()const {
        hand::Proposal p;p.tile=100;p.holder=7;p.outside=2;p.approach=1;p.pads=pads;p.capture=capture;
        p.fresh_witness=p.visible_eligible_holder=p.outside_viable=true;return p;
    }
    void fund() {actors[0].flow.own_payout(200);actors[0].flow.purchased();}
    bool offer() {auto a=adapter(0);return actors[0].flow.offer(a,proposal());}
    hand::Dispatch tick(int id) {
        auto a=adapter(id);auto d=actors[id].flow.dispatch(a);
        // The SDK action adapter gives channels/recovery priority over a
        // queued banking demand; a decision cannot cancel that owner state.
        if(d.kind==hand::Kind::banking&&actors[id].alive&&!actors[id].channel)++bank_attempts;
        if(d.kind==hand::Kind::outside&&d.outside==2&&actors[id].position==200&&outside_hp>0) {
            outside_hp=std::max(0,outside_hp-20);
            if(outside_work_round<0)outside_work_round=round;
        }
        return d;
    }
    void post(int id) {
        auto&a=actors[id];a.receipts.after(a.bag,a.gold,a.alive,on_source(id)?100:-1,reward);
    }
    void next() {
        ++round;
        if(reward&&(on_source(0)||on_source(1))) {
            if(remaining<0)remaining=capture;
            else if(--remaining==0) {
                reward=false;renewed=true;pay_round=round;
                int holders=int(on_source(0))+int(on_source(1));
                for(int i=0;i<2;++i)if(on_source(i)) {actors[i].bag+=source_gems/holders;actors[i].gold+=source_gold/holders;}
            }
        }
        for(int i=0;i<2;++i) {
            auto&a=actors[i];auto gain=a.receipts.sample(round,a.bag,a.gold,a.alive,a.channel,
                on_source(i)?100:-1,reward,renewed);
            if(gain.payout){a.flow.sync_release(bus,round);a.flow.own_payout(gain.source,a.bag);}
            if(gain.bank)a.flow.banked();
        }
    }
};

static void transfer(bool holder_first,bool report_delay) {
    World w;w.fund();assert(w.offer());
    assert(w.actors[0].flow.offers==1);
    if(holder_first) {w.tick(1);if(!report_delay)w.tick(0);}
    else {w.tick(0);w.tick(1);}
    if(report_delay||!holder_first) {w.post(0);w.post(1);w.next();w.tick(0);}
    assert(hand::decode(w.bus).phase==hand::Phase::released);
    assert(!w.on_source(0)&&w.on_source(1));
    assert(w.actors[0].flow.donation_debt()&&w.actors[0].flow.preferred_outside()==2);
    // Physical outside work is useful even before the holder's eventual payout.
    assert(w.actors[0].position==200);
    for(int i=0;i<8&&w.pay_round<0;++i) {
        assert(w.tick(0).kind==hand::Kind::outside);
        assert(w.tick(1).kind==hand::Kind::holding);
        w.post(0);w.post(1);w.next();
    }
    assert(w.pay_round>w.depart_round&&w.outside_work_round<w.pay_round&&w.outside_hp<100);
    assert(w.actors[0].bag==0&&w.actors[1].bag==8&&w.actors[1].flow.holder_payouts==1);
    assert(w.tick(1).kind==hand::Kind::banking);
    assert(hand::decode(w.bus).phase==hand::Phase::paid);
    auto&h=w.actors[1];h.receipts.submit(w.round,h.bag,.1);w.post(1);
    ++w.round;h.bag=0;auto bank=h.receipts.sample(w.round,h.bag,h.gold,true,false,-1,false,false);
    assert(bank.bank);h.flow.banked();assert(!h.flow.needs_bank()&&h.flow.banks==1);
    // Work and teammate PAID do not clear the clearer's funding debt.
    w.tick(0);assert(!w.actors[0].flow.funded());
    w.actors[0].receipts.after(0,0,true,200,true);
    ++w.round;w.actors[0].gold=30;
    auto own=w.actors[0].receipts.sample(w.round,0,30,true,false,200,false,true);
    assert(own.payout);w.actors[0].flow.own_payout(own.source);
    assert(w.actors[0].flow.funded()&&w.actors[0].flow.outside_payouts==1);
}
static void admissions() {
    World w;auto a=w.adapter(0);assert(!w.actors[0].flow.offer(a,w.proposal()));
    w.actors[0].flow.own_payout(200);assert(!w.offer());w.actors[0].flow.purchased();
    auto p=w.proposal();p.fresh_witness=false;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.pads=1;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.outside_viable=false;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.visible_eligible_holder=false;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.approach=3;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.holder=3;assert(!w.actors[0].flow.offer(a,p));
    p=w.proposal();p.tile=65536;assert(!w.actors[0].flow.offer(a,p));
    w.pads=1;assert(!w.offer());w.pads=2;w.source_mapping=false;assert(!w.offer());
    w.source_mapping=true;w.interfere=true;w.bus=1;assert(!w.offer());
}
static void no_physical_arrival_or_exit() {
    World w;w.fund();assert(w.offer());w.blocked_approach=true;w.tick(1);
    assert(hand::decode(w.bus).phase==hand::Phase::accept);
    for(int i=0;i<2;++i)assert(w.tick(0).kind==hand::Kind::holding);
    assert(w.actors[0].flow.departures==0);
    w.blocked_approach=false;w.tick(1);w.visible=false;w.tick(0);
    assert(w.actors[0].flow.departures==0);
    w.visible=true;w.blocked=true;w.tick(0);assert(hand::decode(w.bus).phase==hand::Phase::accept);
    w.blocked=false;w.stay_inside=true;w.tick(0);
    assert(hand::decode(w.bus).phase==hand::Phase::accept&&w.actors[0].flow.departures==0);
    w.stay_inside=false;w.suspend=true;w.tick(0);
    assert(hand::decode(w.bus).phase==hand::Phase::accept); // no stale-round publish
}
static void refusals_and_aging() {
    for(int refusal=0;refusal<5;++refusal) {
        World w;w.fund();assert(w.offer());
        auto&h=w.actors[1];
        if(refusal==0)h.hp=600;if(refusal==1)h.bag=1;if(refusal==2)h.channel=true;
        if(refusal==3)h.eligible=false;if(refusal==4)w.pads=1;
        assert(w.tick(1).kind==hand::Kind::solo);
        assert(hand::decode(w.bus).phase==hand::Phase::cancel);
        assert(w.tick(0).kind==hand::Kind::solo); // ordinary productive fallback
    }
    World w;w.fund();assert(w.offer());const auto old=w.bus;
    w.actors[1].channel=true;w.tick(1);w.bus=old;w.actors[1].channel=false;
    assert(w.tick(1).kind==hand::Kind::solo); // same key cannot resurrect
    w.bus=old;w.round=18;w.tick(0);assert(hand::decode(w.bus).phase==hand::Phase::cancel);
    World death;death.fund();assert(death.offer());const auto key=death.bus;
    death.actors[1].alive=false;death.tick(1);death.actors[1].alive=true;death.bus=key;
    assert(death.tick(1).kind==hand::Kind::solo);
    World renew;renew.fund();assert(renew.offer());renew.renewed=true;renew.reward=false;
    renew.tick(1);assert(hand::decode(renew.bus).phase==hand::Phase::cancel);
    World stale;stale.fund();assert(stale.offer());stale.tick(1);stale.round+=2;
    stale.tick(0);assert(stale.actors[0].flow.departures==0);
    auto bad=hand::decode(stale.bus);bad.report=3;stale.bus=hand::encode(bad);
    stale.tick(0);assert(stale.actors[0].flow.departures==0);
    World unexplained;unexplained.fund();assert(unexplained.offer());
    auto release=hand::decode(unexplained.bus);release.phase=hand::Phase::released;
    unexplained.bus=hand::encode(release);assert(unexplained.tick(1).kind==hand::Kind::solo);
    World replaced;replaced.fund();assert(replaced.offer());const auto older=replaced.bus;
    replaced.tick(1);++replaced.round;
    auto newer=hand::decode(older);++newer.offer;++newer.deadline;
    replaced.bus=hand::encode(newer);replaced.tick(1);
    replaced.actors[1].channel=true;replaced.tick(1);replaced.bus=older;
    replaced.actors[1].channel=false;assert(replaced.tick(1).kind==hand::Kind::solo);
}
static void payout_before_holder_dispatch() {
    World w;w.fund();assert(w.offer());w.tick(1);w.tick(0);
    assert(w.actors[1].flow.active_packet().phase==hand::Phase::accept);
    assert(hand::decode(w.bus).phase==hand::Phase::released);
    // Capture resolves before this holder's next dispatch. Its actual private
    // receipt must synchronize the matching release, rather than call this a
    // shared payment simply because its previous owner turn saw ACCEPT.
    for(int i=0;i<8&&w.pay_round<0;++i){w.post(0);w.post(1);w.next();}
    assert(w.actors[1].flow.holder_payouts==1&&w.actors[1].flow.needs_bank());
    assert(w.tick(1).kind==hand::Kind::banking&&hand::decode(w.bus).phase==hand::Phase::paid);
}
static void shared_payout_is_no_transfer() {
    World w;w.fund();assert(w.offer());w.tick(1);w.blocked=true;w.tick(0);
    for(int i=0;i<8&&w.pay_round<0;++i){w.post(0);w.post(1);w.next();}
    assert(w.actors[0].bag==4&&w.actors[1].bag==4&&w.depart_round<0);
    assert(w.actors[0].flow.departures==0&&w.actors[1].flow.holder_payouts==0);
    w.tick(0);assert(hand::decode(w.bus).phase==hand::Phase::cancel);
    assert(!w.actors[0].flow.donation_debt());
}
static void gold_only_holder_stays_productive(bool clearer_first) {
    World w;w.source_gems=0;w.fund();assert(w.offer());
    if(clearer_first) {
        w.tick(0);w.tick(1);w.post(0);w.post(1);w.next();w.tick(0);
    } else {w.tick(1);w.tick(0);}
    for(int i=0;i<8&&w.pay_round<0;++i){w.post(0);w.post(1);w.next();}
    assert(w.actors[1].bag==0&&w.actors[1].gold==40);
    assert(w.actors[1].flow.holder_payouts==1&&!w.actors[1].flow.needs_bank());
    const int bank_attempts=w.bank_attempts;
    assert(w.tick(1).kind==hand::Kind::solo);
    assert(hand::decode(w.bus).phase==hand::Phase::paid&&w.bank_attempts==bank_attempts);
    assert(w.tick(1).kind==hand::Kind::solo&&w.bank_attempts==bank_attempts);
}
static void paid_holder_death_and_recovery() {
    for(int bus_case=0;bus_case<4;++bus_case) {
        World w;w.fund();assert(w.offer());w.tick(1);w.tick(0);
        for(int i=0;i<8&&w.pay_round<0;++i){w.post(0);w.post(1);w.next();}
        assert(w.tick(1).kind==hand::Kind::banking);
        assert(hand::decode(w.bus).phase==hand::Phase::paid&&w.actors[1].flow.needs_bank());
        auto key=hand::decode(w.bus);
        if(bus_case==1)w.bus=0;
        if(bus_case==2){++key.offer;++key.deadline;key.report=0;key.phase=hand::Phase::offer;w.bus=hand::encode(key);}
        if(bus_case==3){key.holder=15;w.bus=hand::encode(key);} // invalid wire also cannot retain demand
        auto&holder=w.actors[1];holder.alive=false;holder.bag=0;holder.position=99;
        const int moves=w.movement_attempts,banks=w.bank_attempts;
        assert(w.tick(1).kind==hand::Kind::solo);
        assert(!holder.flow.needs_bank()&&w.movement_attempts==moves&&w.bank_attempts==banks);
        holder.alive=true;w.bus=0;
        assert(w.tick(1).kind==hand::Kind::solo&&!holder.flow.needs_bank());
        if(bus_case==2){w.bus=hand::encode(key);assert(w.tick(1).kind==hand::Kind::solo);}
    }
    World recovery;recovery.fund();assert(recovery.offer());recovery.tick(1);recovery.tick(0);
    for(int i=0;i<8&&recovery.pay_round<0;++i){recovery.post(0);recovery.post(1);recovery.next();}
    recovery.tick(1);assert(recovery.actors[1].flow.needs_bank());
    recovery.actors[1].channel=true;const int moves=recovery.movement_attempts,banks=recovery.bank_attempts;
    assert(recovery.tick(1).kind==hand::Kind::banking);
    assert(recovery.movement_attempts==moves&&recovery.bank_attempts==banks);
    hand::Controller clearer;clearer.own_payout(200);clearer.purchased();assert(clearer.funded());
    clearer.died();assert(!clearer.funded());clearer.own_payout(201);assert(clearer.funded());
    assert(recovery.actors[0].flow.donation_debt());
    recovery.actors[0].flow.died();assert(recovery.actors[0].flow.donation_debt());
    recovery.actors[0].flow.own_payout(200);assert(!recovery.actors[0].flow.donation_debt());
}
static void receipt_integrity_and_watch() {
    hand::Receipts r;r.after(0,0,true,100,true);
    assert(!r.sample(11,0,0,true,false,100,false,true).payout); // nominal lifecycle alone
    r.after(0,0,true,100,true);
    assert(!r.sample(12,5,30,true,false,-1,false,true).payout); // departure breaks receipt
    r.after(5,30,true,-1,false);r.submit(12,5,.1);r.after(5,30,true,-1,false);
    assert(r.sample(13,0,30,true,false,-1,false,false).bank); // actual one-tick submit
    r.after(5,30,true,-1,false);r.submit(20,5,.1);r.after(5,30,true,-1,false);
    assert(!r.sample(21,0,30,false,false,-1,false,false).bank); // death is no bank
    r.after(5,30,true,-1,false);r.submit(30,5,.1);r.after(5,30,true,-1,false);
    assert(!r.sample(31,5,30,true,false,-1,false,false).bank); // cancelled submit
    assert(!r.sample(32,0,30,true,false,-1,false,false).bank); // recall/drop is no bank
    heuristics::CaptureWatch watch;
    assert(!watch.expired(4,10,5));
    for(int round=11;round<17;++round)assert(!watch.expired(4,round,5));
    assert(watch.expired(4,17,5)); // empty enemy vision cannot renew an unpaid hold
    assert(!hand::leave_or_work([]{return false;},[]{return false;}));
    assert(hand::leave_or_work([]{return false;},[]{return true;}));
    assert(watch.expired(4,30,5)); // cancellation/failed exit never touched watch
}
static void packet() {
    hand::Packet p;p.tile=65535;p.clearer=10;p.holder=11;p.phase=hand::Phase::accept;
    assert(hand::source_observation_needed(p,73,10)); // provisional offer, dates not assigned
    assert(hand::source_observation_needed(p,73,11));
    assert(!hand::source_observation_needed(p,73,7));
    p.offer=65500;p.deadline=65507;p.report=2;
    assert(hand::source_observation_needed(p,65507,10));
    assert(!hand::source_observation_needed(p,65508,10));
    assert(!hand::source_observation_needed(p,65502,7));
    const auto q=hand::decode(hand::encode(p));assert(hand::same_key(p,q));
    assert(q.deadline==p.deadline&&q.report==2&&q.phase==p.phase&&hand::valid(q,65502));
    assert(!hand::valid(q,65501));p.holder=4;assert(!hand::valid(p,65502));
    p.holder=11;p.deadline=p.offer+32;assert(!hand::valid(p,65502));
}
int main() {
    solo_policy();
    packet();admissions();no_physical_arrival_or_exit();refusals_and_aging();receipt_integrity_and_watch();
    payout_before_holder_dispatch();
    shared_payout_is_no_transfer();
    gold_only_holder_stays_productive(false);gold_only_holder_stays_productive(true);
    paid_holder_death_and_recovery();
    transfer(false,false);transfer(true,false);transfer(false,true);transfer(true,true);
}
