#include "coop_protocol.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace p=wb::protocol;

static std::uint16_t word(int round,int job) {
    const auto encoded=p::encode(round,job);assert(encoded);return *encoded;
}
static bool ready(p::Disposition d) {
    return d==p::Disposition::leader_ready||d==p::Disposition::support_ready
        ||d==p::Disposition::working;
}

struct World {
    std::array<p::Machine,4> machines{p::Machine{0},p::Machine{1},p::Machine{2},p::Machine{3}};
    std::array<std::uint16_t,4> memory{};
    std::array<p::Decision,4> decisions{};
    std::array<bool,4> consent{true,true,true,true},alive{true,true,true,true};
    std::array<bool,4> visible{true,true,true,true},available{true,true,true,true};
    std::array<bool,4> arrived{},safe{},awake{};
    int deadline=150,last_tick=-1;

    p::Input input(int slot,int round) const {
        p::Input in;in.round=round;in.peer=p::decode(memory[slot^1]);
        in.proposed_job=slot<2?1:2;in.local_consent=consent[slot];
        in.operator_deadline=deadline;in.own_alive=alive[slot];in.own_available=available[slot];
        in.peer_alive=alive[slot^1];in.peer_available=available[slot^1];
        in.peer_visible=visible[slot];in.own_arrived=arrived[slot];
        in.peer_arrived=arrived[slot^1];in.safety=safe[slot];in.group_awake=awake[slot];
        return in;
    }
    p::Decision apply(int slot,const p::Input& in) {
        const auto before=memory;
        const auto result=machines[slot].step(in);
        if(result.write) {
            assert(result.write->slot==slot);
            assert(result.write->byte_offset()==2*slot);
            memory[result.write->slot]=result.write->word;
        }
        for(int other=0;other<4;++other) if(other!=slot) assert(memory[other]==before[other]);
        decisions[slot]=result;return result;
    }
    p::Decision step(int slot,int round) {return apply(slot,input(slot,round));}
    void tick(int round,bool reverse=false) {
        if(reverse) for(int slot=3;slot>=0;--slot) step(slot,round);
        else for(int slot=0;slot<4;++slot) step(slot,round);
        last_tick=round;
    }
    void until_offer(bool reverse=false) {
        for(int r=0;r<20;++r) {
            tick(r,reverse);
            if(machines[0].stage()==p::Stage::offer_wait) return;
        }
        assert(false);
    }
    void until_active(bool reverse=false) {
        for(int r=0;r<25;++r) {
            tick(r,reverse);
            bool all=true;
            for(const auto& m:machines) all=all&&m.stage()==p::Stage::active;
            if(all) return;
        }
        assert(false);
    }
};

static void encoding_and_ownership() {
    assert(p::decode(word(2000,31)).round==2000);
    assert(p::decode(word(2000,31)).job==31);
    assert(!p::encode(2001,1)&&!p::encode(2048,1)&&!p::encode(-1,1));
    assert(!p::encode(12,32)&&!p::encode(12,-1));
    assert(!p::decode(std::uint16_t((2001<<5)|1)).valid);
    assert(!p::fresh({9,1,true},8)&&!p::fresh({5,1,true},8));
    assert(p::fresh({7,1,true},8));
    for(int j=1;j<=31;++j) {
        assert(p::assigned_job(0,j)==p::assigned_job(1,j));
        assert(p::assigned_job(2,j)==p::assigned_job(3,j));
        assert(p::assigned_job(0,j)!=p::assigned_job(2,j));
    }
    assert(!p::assigned_job(4,1)&&!p::assigned_job(0,0));
    p::Machine absent{4};p::Input in;in.round=0;in.own_alive=in.own_available=true;
    auto out=absent.step(in);assert(!out.write&&out.disposition==p::Disposition::disabled);
    p::Machine late{0};in.round=2001;
    out=late.step(in);assert(!out.write&&out.disposition==p::Disposition::disabled);
    in.round=0;in.round_limit=2048;p::Machine unsupported{0};
    out=unsupported.step(in);assert(out.write&&p::decode(out.write->word).job==0);
}

static void complete_handshake_and_physical_gates(bool reverse) {
    World w;w.until_active(reverse);
    const int epoch=w.machines[0].epoch();
    assert(epoch>=0&&w.machines[1].epoch()==epoch);
    assert(w.machines[0].job()==1&&w.machines[1].job()==1);
    assert(w.machines[2].job()==2&&w.machines[3].job()==2); // Concurrent disjoint offers.
    for(const auto& d:w.decisions) assert(!ready(d.disposition));
    int now=w.last_tick;
    w.tick(++now,reverse); // Fresh words with no physical arrival never arm.
    for(const auto& d:w.decisions) assert(!ready(d.disposition));
    w.arrived.fill(true);w.safe.fill(false);
    w.tick(++now,reverse); // Physical arrival without safety never arms.
    for(const auto& d:w.decisions) assert(!ready(d.disposition));
    w.safe.fill(true);
    w.tick(++now,reverse); // First actual witness is not a post-arrival heartbeat.
    for(const auto& d:w.decisions) assert(!ready(d.disposition));
    for(int n=0;n<3;++n) w.tick(++now,reverse);
    assert(w.decisions[0].disposition==p::Disposition::leader_ready);
    assert(w.decisions[1].disposition==p::Disposition::support_ready);
    w.awake.fill(true);w.tick(++now,reverse);
    assert(w.decisions[0].disposition==p::Disposition::working);
    assert(w.decisions[1].disposition==p::Disposition::working);
    w.arrived[1]=false;w.tick(++now,reverse);
    assert(!ready(w.decisions[0].disposition)&&!ready(w.decisions[1].disposition));
    w.arrived[1]=true;w.tick(++now,reverse);
    assert(!ready(w.decisions[0].disposition)&&!ready(w.decisions[1].disposition));
    // One owner cannot advance its protocol twice from a same-round mailbox read.
    auto duplicate=w.step(0,now);
    assert(!duplicate.write&&duplicate.disposition==p::Disposition::invalid_input);
}

static void frozen_phases_and_missing_ack() {
    World w;w.until_offer();const int p0=w.machines[0].epoch();
    const auto offered=w.memory[0];
    w.tick(p0+1);
    assert(w.memory[0]==offered); // Leader does not erase its proposal timestamp.
    assert(p::decode(w.memory[1]).round==p0+1&&p::decode(w.memory[1]).job==1);
    const auto accepted=w.memory[1];
    w.tick(p0+2);
    assert(w.memory[1]==accepted); // Follower freezes its acceptance.
    assert(p::decode(w.memory[0]).round==p0+2);
    w.tick(p0+3);
    assert(p::decode(w.memory[0]).round==p0+2); // Confirmation frozen through response.
    assert(p::decode(w.memory[1]).round==p0+3);

    World ack;ack.until_offer(true);const int a=ack.machines[0].epoch();
    ack.tick(a+1,true);ack.tick(a+2,true);ack.tick(a+3,true);
    const auto frozen_ack=ack.memory[1];
    auto held=ack.step(1,a+4); // Follower runs before leader's receipt.
    assert(!held.write&&ack.memory[1]==frozen_ack);
    ack.step(0,a+4);
    held=ack.step(0,a+5); // Order changes: leader now runs before follower.
    assert(held.disposition==p::Disposition::acknowledged&&!ready(held.disposition));
    ack.step(1,a+5);ack.step(0,a+6);ack.step(1,a+6);
    assert(ack.machines[0].stage()==p::Stage::active&&ack.machines[1].stage()==p::Stage::active);

    World missed;missed.until_offer();const int m=missed.machines[0].epoch();
    missed.step(0,m+1); // Follower misses its required p+1 input.
    auto out=missed.step(0,m+2);
    assert(out.disposition==p::Disposition::cancelled);
    assert(p::decode(missed.memory[0]).job==0);
    auto late=missed.step(1,m+2);
    assert(!ready(late.disposition)&&p::decode(missed.memory[1]).job==0);

    World skipped;skipped.until_offer();const int s=skipped.machines[0].epoch();
    skipped.tick(s+1);
    out=skipped.step(0,s+3); // Leader misses its p+2 confirmation phase.
    assert(out.disposition==p::Disposition::cancelled);
}

static void aba_barrier_and_private_refusal() {
    World w;w.until_offer();const int p0=w.machines[0].epoch();
    w.tick(p0+1);const auto old_ack=w.memory[1];
    auto cancel=w.input(0,p0+2);cancel.release=true;
    auto out=w.apply(0,cancel);
    assert(out.disposition==p::Disposition::released);
    for(int r=p0+3;r<=p0+5;++r) {
        w.step(0,r); // Follower misses release; cannot reopen same J from old ACK.
        assert(w.machines[0].stage()==p::Stage::reset);
        assert(p::decode(w.memory[0]).job==0&&w.memory[1]==old_ack);
    }
    // Restarted follower cannot reconstruct an old epoch from an active word.
    p::Machine restarted{1};auto in=w.input(1,p0+6);
    in.peer=p::decode(word(p0+5,1));
    out=restarted.step(in);assert(out.write&&p::decode(out.write->word).job==0);
    assert(restarted.stage()==p::Stage::reset);

    // Real follower reads the empty release before publishing the barrier ACK.
    w.step(1,p0+6);w.step(0,p0+6);
    w.step(1,p0+7);w.step(0,p0+7);
    int new_epoch=-1;
    for(int r=p0+8;r<p0+18;++r) {
        w.step(0,r);w.step(1,r);
        if(w.machines[0].stage()==p::Stage::offer_wait) {new_epoch=w.machines[0].epoch();break;}
    }
    assert(new_epoch>p0+6);
    assert(new_epoch>p::decode(old_ack).round);
    auto stale_leader=w.machines[0];
    auto stale_input=w.input(0,new_epoch+1);stale_input.peer=p::decode(old_ack);
    stale_leader.step(stale_input);
    stale_input.round=new_epoch+2;
    const auto stale_result=stale_leader.step(stale_input);
    assert(stale_result.disposition==p::Disposition::cancelled);
    // The follower's own private alternative can reject despite the leader's quote.
    w.consent[1]=false;
    w.step(0,new_epoch+1);w.step(1,new_epoch+1);
    w.step(0,new_epoch+2);
    assert(w.machines[0].stage()==p::Stage::reset);
    assert(p::decode(w.memory[1]).job==0);
}

static void stale_future_hidden_dead_and_expiry() {
    for(int mode=0;mode<8;++mode) {
        World w;w.until_active();const int r=w.machines[0].epoch()+7;
        auto in=w.input(0,r);
        if(mode==0) in.peer={r-2,1,true};
        if(mode==1) in.peer={r+1,1,true};
        if(mode==2) in.peer_visible=false;
        if(mode==3) in.peer_alive=false;
        if(mode==4) in.peer_available=false;
        if(mode==5) in.peer={r,2,true};
        if(mode==6) in.peer={};
        if(mode==7) in.local_consent=false;
        const auto out=w.apply(0,in);
        assert(out.disposition==p::Disposition::cancelled);
        assert(out.write&&p::decode(out.write->word).job==0&&!ready(out.disposition));
    }
    World w;w.until_active();const int r=w.machines[0].epoch()+7;
    auto in=w.input(0,r);in.peer={r,1,true};in.own_alive=false;
    assert(w.apply(0,in).disposition==p::Disposition::cancelled);

    World bounded;bounded.until_active();const int t=bounded.machines[0].epoch()+7;
    in=bounded.input(0,t);in.peer={t,1,true};in.operator_deadline=t;
    bounded.apply(0,in);assert(bounded.machines[0].deadline()==t);
    in=bounded.input(0,t+1);in.peer={t+1,1,true};in.operator_deadline=t+100;
    auto out=bounded.apply(0,in);
    assert(out.disposition==p::Disposition::expired); // No free deadline extension.
}

static void limit_and_wrong_family() {
    World late;late.deadline=2000;
    // A fresh startup/reset near the end still cannot fit the whole handshake.
    for(int r=1996;r<=2000;++r) late.tick(r);
    for(int slot=0;slot<4;++slot) {
        assert(late.machines[slot].stage()!=p::Stage::active);
        assert(p::decode(late.memory[slot]).round<=2000);
    }
    World wrong;
    for(int r=0;r<10;++r) {
        for(int slot=0;slot<4;++slot) {
            auto in=wrong.input(slot,r);in.proposed_job=slot<2?2:1;
            wrong.apply(slot,in);
            assert(p::decode(wrong.memory[slot]).job==0);
        }
    }
    World collision;
    for(int r=0;r<12;++r) for(int slot=0;slot<4;++slot) {
        auto in=collision.input(slot,r);in.proposed_job=1;
        collision.apply(slot,in);
        if(slot>=2) assert(p::decode(collision.memory[slot]).job==0);
    }
    assert(collision.machines[0].stage()==p::Stage::active);
    World unpriced;unpriced.deadline=-1;
    for(int r=0;r<8;++r) unpriced.tick(r);
    for(auto stored:unpriced.memory) assert(p::decode(stored).job==0);
}

int main() {
    encoding_and_ownership();
    complete_handshake_and_physical_gates(false);
    complete_handshake_and_physical_gates(true);
    frozen_phases_and_missing_ack();
    aba_barrier_and_private_refusal();
    stale_future_hidden_dead_and_expiry();
    limit_and_wrong_family();
    std::cout<<"coop_protocol: ownership, both-order handshake, ABA reset, refusal, expiry, visibility and physical gates pass\n";
}
