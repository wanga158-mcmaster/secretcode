#pragma once
// Bounded FIRST-request witness. No enumeration, formatting, logging, new SDK observation or
// policy evaluation belongs in the capture path. Float fields are emitted as uint32 IEEE bits
// so an offline caller can reconstruct the exact Input, including non-finite rejected inputs.
// This diagnostic is evidence only after checking the native decision prefix against its seed.
#include "joint_model.h"
#include <cstdio>
#include <cstring>

namespace mb::joint::audit {
enum Reason : unsigned {
    dead=1u, bid_fuel=2u, incomplete=4u, capture=8u, phase_veto=16u,
    channel=32u, winning_bank=64u, immediate_capture=128u, low_hp=256u,
    no_field=512u, eta_range=1024u, price_range=2048u
};
enum Resolution : int { none=0, veto=1, choose_fuel=2, owner_early_return=3,
    owner_missing=4, choose_farm=5, choose_party=6, observed_commit=10, observed_abort=11 };
struct FarmTerm {
    bool sampled{}, included{};
    float requested_reward{}, requested_term{};
    int requested_travel{-1};
};
struct BidSample {
    bool seen{};
    int round{-1}, phase{-1}, raw{-1}, eta{-1}, bin{-1};
    float price{}, bin_price{};
    int x{-1}, y{-1}, field{-1};
    unsigned reasons{}, checked{}, known{}; // known: position1, field2, price4, requested-term8
    FarmTerm farm{};
};
struct Record {
    bool seen{}, resolved{}, input_seen{}, choice_seen{}, owner_result{}, emitted{};
    int self{-1}, roster{};
    Packet request{};
    FarmTerm farm{};
    BidSample bid{};
    int result_round{-1}, result_code{}, result_tag{-1};
    unsigned long long fuel_before_choose{};
    std::array<int,5> raw_slots{{-1,-1,-1,-1,-1}};
    Input input{};
    Choice choice{};
};
inline Record& record() { static Record r; return r; }
inline bool& started() { static bool value=false; return value; }
inline bool same(const Packet& p) {
    const auto& r=record();
    return r.seen && !r.emitted && r.request.group==p.group && r.request.round==p.round && r.request.owner==p.owner;
}
inline bool capturing(const Packet& p) { return same(p) && !record().resolved; }
inline void start(const Packet& p,int self,int roster) {
    auto& r=record();
    if (!r.seen && !r.emitted) { r.seen=true; r.request=p; r.self=self; r.roster=roster; started()=true; }
}
inline void farm_sample(const Packet& p,const FarmTerm& term) { if (capturing(p)) record().farm=term; }
inline void bid(const Packet& p,int now,int phase,int raw,int eta,float price,int x,int y,int field,
                unsigned reasons,unsigned checked,unsigned known) {
    if (!capturing(p)) return;
    auto& r=record(); auto& b=r.bid;
    b.seen=true; b.round=now; b.phase=phase; b.raw=raw; b.eta=eta; b.price=price;
    const auto decoded=decode_bid(static_cast<std::uint8_t>(raw));
    b.bin=decoded.price; b.bin_price=price_upper(decoded.price);
    b.x=x; b.y=y; b.field=field; b.reasons=reasons; b.checked=checked; b.known=known;
    b.farm=FarmTerm{};
    if (known&4u) { b.farm=r.farm; if (b.farm.sampled) b.known|=8u; }
}
inline void result(const Packet& p,int now,int code,int tag,bool owner=false) {
    if (!same(p)) return;
    auto& r=record();
    if (!r.resolved || owner) {
        r.resolved=true; r.result_round=now; r.result_code=code; r.result_tag=tag; r.owner_result=owner;
    }
}
inline void chosen(const Packet& p,int now,const Input& input,const Choice& choice,
                   const std::array<int,5>& raw,unsigned long long fuel) {
    if (!same(p)) return;
    auto& r=record(); r.input=input; r.choice=choice; r.raw_slots=raw;
    r.input_seen=true; r.choice_seen=true; r.fuel_before_choose=fuel;
    result(p,now,choice.mask?choose_party:choose_farm,static_cast<int>(choice.mask?Tag::commit:Tag::abort),true);
}
inline bool ready(int now) {
    const auto& r=record();
    return !r.emitted && r.seen && now>=151 && (r.resolved || now>r.request.round+4);
}
inline std::uint32_t bits(float v) {
    static_assert(sizeof(float)==sizeof(std::uint32_t));
    std::uint32_t out; std::memcpy(&out,&v,sizeof out); return out;
}
struct Writer {
    char data[4096]{}; std::size_t used{}; bool bad{};
    void text(const char* s) {
        while (*s) { if (used+1>=sizeof data) { bad=true; return; } data[used++]=*s++; }
        data[used]=0;
    }
    void integer(long long v) {
        const int n=std::snprintf(data+used,sizeof data-used,"%lld",v);
        if (n<0 || static_cast<std::size_t>(n)>=sizeof data-used) { bad=true; return; }
        used+=static_cast<std::size_t>(n);
    }
    template<class T,std::size_t N> void array(const char* key,const std::array<T,N>& values) {
        text(key); text("[");
        for (std::size_t i=0;i<N;++i) { if (i) text(","); integer(static_cast<long long>(values[i])); }
        text("]");
    }
};
// Emit only at turn BEGIN >=151 with ample actual fuel. One line per actor, outside MB_LOG's
//400-line quota. Capture remains open until the first observed request, even after151.
// A missing line is unknown (unseen request, unfinished game, budget or engine log cap), never0.
// Schema v1: rq=[seen,round,group,owner,urgency]; b=[seen,round,phase,raw,eta,priceBits,
// bin,binPriceBits,included,x,y,field,reasons,checked,known,termRewardBits,termTravel,termBits].
// phase0=start invalidation,1=completed end bid,2=owner current-position override.
// z=[resolved,round,reason,resultTag,inputSeen,choiceSeen,ownerResult]. Raw slots precede
// the owner's override. ii lists Input's integer fields in declaration order then alive_mask;
// ff=[reward,camp_hp,camp_dps,requester_hp,threat_dps,death_gold,safety_margin] as float bits.
// bv repeats [valid,eligible,eta,price] for all5 slots; dp/hp contain exact float bits.
// ch=[mask,deadline,arrival,clear_rounds,masks_evaluated,valueBits,task_rewardBits,
// displaced_farmBits,travel_costBits,spread_costBits,recovery_costBits]. cm is a completeness
// mask: request1,bid2,result4,input8,choice16,ownerResult32,sealed64; n=actual roster count.
inline void emit(int now,int self,int roster) {
    auto& r=record(); if (!ready(now)) return;
    Writer w; w.text("{\"v\":1,\"i\":"); w.integer(self); w.text(",\"n\":"); w.integer(roster);
    w.text(",\"at\":"); w.integer(now);
    w.array(",\"rq\":",std::array<long long,5>{r.seen,r.request.round,r.request.group,r.request.owner,r.request.urgency});
    const auto& b=r.bid;
    w.array(",\"b\":",std::array<long long,18>{b.seen,b.round,b.phase,b.raw,b.eta,bits(b.price),b.bin,bits(b.bin_price),
        b.farm.included,b.x,b.y,b.field,b.reasons,b.checked,b.known,bits(b.farm.requested_reward),b.farm.requested_travel,bits(b.farm.requested_term)});
    w.array(",\"z\":",std::array<int,7>{r.resolved,r.result_round,r.result_code,r.result_tag,r.input_seen,r.choice_seen,r.owner_result});
    w.array(",\"raw\":",r.raw_slots);
    const auto& in=r.input;
    w.array(",\"ii\":",std::array<int,9>{in.now,in.deadline,in.requester,in.roster_size,in.horizon,in.capture_rounds,in.recall_cycle,in.completion_rounds,in.alive_mask});
    w.array(",\"ff\":",std::array<std::uint32_t,7>{bits(in.reward),bits(in.camp_hp),bits(in.camp_dps),bits(in.requester_hp),bits(in.threat_dps),bits(in.death_gold),bits(in.safety_margin)});
    std::array<int,20> bids{}; std::array<std::uint32_t,5> dps{},hp{};
    for (int i=0;i<5;++i) {
        bids[4*i]=in.bids[i].valid; bids[4*i+1]=in.bids[i].eligible; bids[4*i+2]=in.bids[i].eta; bids[4*i+3]=in.bids[i].price;
        dps[i]=bits(in.dps[i]); hp[i]=bits(in.hp[i]);
    }
    w.array(",\"bv\":",bids); w.array(",\"dp\":",dps); w.array(",\"hp\":",hp);
    const auto& c=r.choice;
    w.array(",\"ch\":",std::array<long long,11>{c.mask,c.deadline,c.arrival,c.clear_rounds,c.masks_evaluated,bits(c.value),bits(c.task_reward),bits(c.displaced_farm),bits(c.travel_cost),bits(c.spread_cost),bits(c.recovery_cost)});
    w.text(",\"fuel\":"); w.integer(static_cast<long long>(r.fuel_before_choose));
    const int complete=(r.seen?1:0)|(b.seen?2:0)|(r.resolved?4:0)|(r.input_seen?8:0)|(r.choice_seen?16:0)|(r.owner_result?32:0)|64;
    w.text(",\"cm\":"); w.integer(complete); w.text("}");
    if (w.bad) std::printf("JA {\"v\":1,\"i\":%d,\"n\":%d,\"at\":%d,\"truncated\":1}\n",self,roster,now);
    else std::printf("JA %s\n",w.data);
    r.emitted=true;
}
}
