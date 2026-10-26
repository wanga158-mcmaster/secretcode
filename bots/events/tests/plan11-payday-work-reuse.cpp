#include <cassert>
#include <cstdio>
#include "../payday/src/policy.h"

#ifdef NDEBUG
#error This exact-work-reuse fixture requires assertions enabled.
#endif

namespace {
using p11::Candidate;
using p11::State;
using p11_payday::Policy;
using p11_payday::Rewards;
int legacy_ceil_calls=0;

int legacy_bank_time(const State&s,const Candidate&c) {
    if(std::int64_t(s.bag)+c.gems>0)++legacy_ceil_calls;
    return c.bank_time(s);
}
// Retain the previous reward and score implementation as an exact arithmetic
// oracle. The existing plan11-payday fixture separately keeps its tick oracle.
Rewards legacy_rewards(const State&s,const Candidate&c) {
    if(c.id<0||s.identity<0||s.round<0||s.limit<0||s.bag<0||s.recall<0
        ||c.travel<0||c.clear<0||c.capture<0||c.altar<0||c.allies<0
        ||c.gold<0||c.gems<0||!std::isfinite(s.tribute_multiplier)
        ||s.tribute_multiplier<0||s.tribute_base<0||s.tribute_minimum<1)return {};
    const auto maximum=std::numeric_limits<int>::max();
    const std::int64_t payout=std::int64_t(c.travel)+c.clear+c.capture;
    const std::int64_t bag=std::int64_t(s.bag)+c.gems;
    if(payout>maximum)return {};
    const std::int64_t horizon=std::int64_t(s.limit)-s.round;
    const int gold=c.payout()<=horizon?c.gold:0;
    if(bag>maximum)return {gold,0};
    ++legacy_ceil_calls;
    const double scaled=std::ceil(double(bag)*s.tribute_multiplier);
    if(!std::isfinite(scaled)||scaled>maximum-s.tribute_base)return {gold,0};
    const int ticks=bag>0?std::max(s.tribute_minimum,s.tribute_base+int(scaled)):0;
    if(payout+c.altar+ticks>maximum)return {gold,0};
    return {gold,legacy_bank_time(s,c)<=horizon?c.gems:0};
}
struct LegacyPolicy : p11_tempo::Policy {
    static double score(const State&s,const Candidate&c) {
        const auto rewards=legacy_rewards(s,c);
        if(rewards.gold<=0&&rewards.gems<=0)return -std::numeric_limits<double>::infinity();
        const int spread=int((std::int64_t(c.id)*3+std::int64_t(s.identity)*11)%7);
        const double crowd=12.*c.allies+spread;
        const double gem_rate=rewards.gems>0?3.*rewards.gems/(1.+legacy_bank_time(s,c)+crowd):0.;
        const double gold_rate=(s.mature?.04:3.)*rewards.gold
            /(1.+c.payout()+(s.field_shop?0:double(s.recall)+1)+crowd);
        return (gem_rate+gold_rate)*(c.committed?1.10:1.);
    }
    static double screen(const State&s,const Candidate&c){return score(s,c);}
};
bool exact(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
int checked_jobs=0;
void verify(const State&s,const Candidate&c) {
    ++checked_jobs;
    const auto expected=legacy_rewards(s,c);
    int rounds=0;
    const auto events=p11_payday::reward_events(s,c,[&](double value){++rounds;return std::ceil(value);});
    assert(rounds<=1);
    assert(events.payable.gold==expected.gold&&events.payable.gems==expected.gems);
    const auto wrapper=p11_payday::payable_rewards(s,c);
    assert(wrapper.gold==expected.gold&&wrapper.gems==expected.gems);
    const auto production=p11_payday::reward_events(s,c);
    assert(production.capture==events.capture&&production.bank==events.bank);
    assert(production.payable.gold==events.payable.gold&&production.payable.gems==events.payable.gems);
    if(events.capture>=0)assert(events.capture==c.payout());
    if(events.bank>=0)assert(events.bank==c.bank_time(s));
    assert(exact(Policy::score(s,c),LegacyPolicy::score(s,c)));
    assert(exact(Policy::screen(s,c),LegacyPolicy::screen(s,c)));
}
template<std::size_t N>
void menu(const State&s,const std::array<Candidate,N>&candidates) {
    for(const auto&c:candidates)verify(s,c);
    // Actual detailed farm choice and public-screen ranking callers receive
    // all competing alternatives; neither oracle supplies a winning ID.
    assert(p11::choose<Policy>(s,candidates)==p11::choose<LegacyPolicy>(s,candidates));
    const int incumbent=N?candidates[0].id:-1,explore=N?candidates[N-1].id:-1;
    assert(p11::shortlist<Policy>(s,candidates,3,incumbent,explore)
        ==p11::shortlist<LegacyPolicy>(s,candidates,3,incumbent,explore));
}
Candidate normal() {
    Candidate c;c.id=2;c.travel=1;c.clear=1;c.capture=1;c.altar=2;
    c.gold=100;c.gems=10;return c;
}
}

int main() {
    State s;s.bag=11;
    auto c=normal();
    legacy_ceil_calls=0;
    const auto previous=LegacyPolicy::score(s,c);
    assert(legacy_ceil_calls==3);
    int current_ceil_calls=0;
    const auto events=p11_payday::reward_events(s,c,[&](double value){++current_ceil_calls;return std::ceil(value);});
    assert(current_ceil_calls==1&&events.capture==3&&events.bank==8);
    assert(exact(Policy::score(s,c),previous));

    // Every original validation predicate and ordered integer-overflow exit.
    for(int field=0;field<30;++field) {
        State state;auto candidate=normal();
        const int maximum=std::numeric_limits<int>::max();
        switch(field) {
        case 0:candidate.id=-1;break;case 1:state.identity=-1;break;
        case 2:state.round=-1;break;case 3:state.limit=-1;break;
        case 4:state.bag=-1;break;case 5:state.recall=-1;break;
        case 6:candidate.travel=-1;break;case 7:candidate.clear=-1;break;
        case 8:candidate.capture=-1;break;case 9:candidate.altar=-1;break;
        case 10:candidate.allies=-1;break;case 11:candidate.gold=-1;break;
        case 12:candidate.gems=-1;break;
        case 13:state.tribute_multiplier=std::numeric_limits<double>::quiet_NaN();break;
        case 14:state.tribute_multiplier=std::numeric_limits<double>::infinity();break;
        case 15:state.tribute_multiplier=-1;break;case 16:state.tribute_base=-1;break;
        case 17:state.tribute_minimum=0;break;
        case 18:candidate.travel=maximum;break;
        case 19:state.bag=maximum;break;
        case 20:state.tribute_multiplier=std::numeric_limits<double>::max();break;
        case 21:state.tribute_base=maximum;break;
        case 22:candidate.altar=maximum;break;
        case 23:state.tribute_minimum=maximum;break;
        case 24:state.round=maximum;state.limit=0;break;
        case 25:candidate.id=maximum;state.identity=maximum;candidate.allies=maximum;break;
        case 26:candidate.gold=maximum;state.recall=maximum;break;
        case 27:state.bag=0;candidate.gems=0;state.tribute_base=maximum;state.tribute_minimum=maximum;break;
        case 28:candidate.travel=maximum-3;candidate.clear=1;candidate.capture=1;candidate.altar=0;
            candidate.gems=0;state.limit=maximum;state.tribute_multiplier=0;break;
        case 29:state.tribute_multiplier=-0.;break;
        }
        verify(state,candidate);
        std::array<Candidate,3> alternatives{{candidate,normal(),Candidate{}}};
        alternatives[1].id=7;alternatives[1].gold=1;alternatives[1].gems=0;
        menu(state,alternatives);
    }
    auto overflow=normal();overflow.altar=std::numeric_limits<int>::max();
    const auto gold_only=p11_payday::reward_events(State{},overflow);
    assert(gold_only.capture==3&&gold_only.bank==-1&&gold_only.payable.gold==100&&gold_only.payable.gems==0);

    std::uint32_t stream=0x4a11963bu;
    auto sample=[&](unsigned maximum){stream=stream*1664525u+1013904223u;return int(stream%maximum);};
    for(int example=0;example<4096;++example) {
        State state;state.round=sample(2001);state.limit=sample(2001);
        state.identity=sample(13);state.bag=sample(1000);state.recall=sample(40);
        state.hp=float(1000-sample(900));state.mature=sample(2);state.field_shop=sample(2);
        constexpr std::array multipliers{0.,.1,.25,1.,.3333333333333333};
        state.tribute_multiplier=multipliers[sample(multipliers.size())];
        state.tribute_base=sample(10);state.tribute_minimum=1+sample(12);
        std::array<Candidate,3> alternatives{};
        for(auto&candidate:alternatives) {
            candidate.id=sample(31);candidate.travel=sample(60);candidate.clear=sample(20);
            candidate.capture=sample(8);candidate.altar=sample(50);candidate.gold=sample(500);
            candidate.gems=sample(200);candidate.allies=sample(6);candidate.committed=sample(2);
            candidate.loss=float(sample(1200));candidate.ready=sample(3)==0;
        }
        menu(state,alternatives);
    }
    for(int example=0;example<256;++example) {
        State state;state.round=example;state.bag=example%73;state.identity=example%13;
        state.mature=example&1;state.field_shop=example&2;
        std::array<Candidate,20> preview{};
        for(int j=0;j<20;++j) {
            auto&candidate=preview[j];candidate=normal();candidate.id=j;
            candidate.travel=sample(40);candidate.clear=sample(12);candidate.altar=sample(30);
            candidate.gold=sample(500);candidate.gems=sample(100);candidate.committed=j==example%20;
        }
        menu(state,preview);
    }
    assert(p11::choose<Policy>(State{},std::span<const Candidate>{})==-1);
    std::printf("payday work reuse: %d exact legacy job scores, 4382 competing menus/rankings; source ceil3->1\n",checked_jobs);
}
