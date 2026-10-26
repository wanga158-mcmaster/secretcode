#include <cassert>
#include <cmath>
#include <cstdint>
#include "../reserve/src/policy.h"

namespace {
using p11::State;using p11::Candidate;using P=p11_reserve::Policy;
struct Renewal {int capacity=0,tax=-1;};

// Exhaustive bounded product tests form the oracle. They neither divide health
// by loss nor call either production helper; the tax is found by enumeration.
Renewal oracle(const State&s,const Candidate&c) {
    if(!std::isfinite(s.hp)||!std::isfinite(s.maximum)||!std::isfinite(c.loss)
       ||s.hp<0||s.maximum<=0||c.loss<0||s.recall<0||c.travel<0||c.clear<0||c.capture<0)return {};
    const auto horizon=std::int64_t(s.limit)-s.round;
    if(horizon<=0)return {};
    const auto repeat=std::max<std::int64_t>(1,std::int64_t(c.travel)+c.clear+c.capture);
    const long double available=std::max(0.L,(long double)s.hp-.25L*s.maximum);
    int count=0;
    for(int n=1;n<=10000;++n) {
        if(std::int64_t(n)*repeat>horizon)break;
        if(!c.ready&&c.loss>0&&(long double)n*c.loss>available)break;
        count=n;
    }
    if(count==0)return {};
    if(c.ready||c.loss==0)return {count,0};
    const auto cycle=std::int64_t(s.recall)+1+c.travel;
    for(int tax=1;tax<=10000;++tax)
        if(std::int64_t(tax)*count>=cycle)return {count,tax};
    assert(false);return {};
}

double oracle_score(const State&s,const Candidate&c) {
    const auto renewal=oracle(s,c);
    if(renewal.tax<0)return -std::numeric_limits<double>::infinity();
    const int payout=c.travel+c.clear+c.capture+renewal.tax;
    const int bag=s.bag+c.gems;
    const int tribute=bag>0?std::max(s.tribute_minimum,s.tribute_base+int(std::ceil(bag*s.tribute_multiplier))):0;
    const int bank=payout+c.altar+tribute;
    if(c.gems>0&&bank>s.limit-s.round)return -std::numeric_limits<double>::infinity();
    const int spread=(c.id*3+s.identity*11)%7;
    const double gems=3.*c.gems/(1+bank+12*c.allies+spread);
    const double gold=(s.mature?.04:3.)*c.gold/(1+payout+(s.field_shop?0:s.recall+1)+12*c.allies+spread);
    return (gems+gold)*(c.committed?1.10:1.);
}

void check_model(const State&s,const Candidate&c) {
    const auto expected=oracle(s,c);
    assert(p11_reserve::expedition_capacity(s,c)==expected.capacity);
    assert(p11_reserve::recovery_ticks(s,c)==expected.tax);
    const double score=P::score(s,c),reference=oracle_score(s,c);
    assert((!std::isfinite(score)&&!std::isfinite(reference))
        ||std::abs(score-reference)<1e-12*std::max(1.,std::abs(reference)));
    assert(P::screen(s,c)==score);
}

template<std::size_t N> void check_choice(const State&s,const std::array<Candidate,N>&jobs) {
    int expected=-1;double best=-std::numeric_limits<double>::infinity();
    for(const auto&c:jobs) {
        check_model(s,c);
        // The actual selector retains Tempo's feasibility gate, separately
        // from the reserve renewal charge used by score and screen.
        if(!c.ready&&!(c.clear<10000&&s.hp-c.loss>=s.maximum*.25f))continue;
        const double value=oracle_score(s,c);
        if(std::isfinite(value)&&value>best){best=value;expected=c.id;}
    }
    assert(p11::choose<P>(s,jobs)==expected);
}
} // namespace

int main() {
    static_assert(P::bounded_scan&&P::window_size==3&&P::trace_scan);
    static_assert(&P::bank==&p11_tempo::Policy::bank);
    static_assert(&P::retreat==&p11_tempo::Policy::retreat);
    State s;s.mature=true;
    Candidate costly;costly.id=0;costly.travel=2;costly.clear=4;costly.altar=2;costly.gems=12;costly.loss=700;
    Candidate gentle=costly;gentle.id=7;gentle.gems=11;gentle.loss=50;
    const std::array alternatives{costly,gentle};
    assert(p11::choose<p11_tempo::Policy>(s,alternatives)==0);
    assert(p11::choose<P>(s,alternatives)==7);
    assert(p11_reserve::expedition_capacity(s,costly)==1);
    assert(p11_reserve::recovery_ticks(s,costly)==8);
    assert(p11_reserve::expedition_capacity(s,gentle)==15);
    assert(p11_reserve::recovery_ticks(s,gentle)==1);
    check_choice(s,alternatives);
    // Recovery cost is added to a copy, never to the caller's clear estimate.
    assert(costly.clear==4&&gentle.clear==4&&s.hp==1000);

    // Exact and adjacent binary32 health/loss thresholds, both health-limited
    // and horizon-limited. The oracle evaluates the products independently.
    for(int n=1;n<=16;++n)for(int side=-1;side<=1;++side) {
        State edge;edge.maximum=1024;edge.hp=256+32*n;
        Candidate c=costly;c.loss=32;
        if(side<0)c.loss=std::nextafter(c.loss,0.f);
        if(side>0)c.loss=std::nextafter(c.loss,std::numeric_limits<float>::infinity());
        auto other=gentle;other.id=14;
        check_choice(edge,std::array{c,other});
    }
    // A remaining horizon permits only two repeats of the 12-tick payout.
    State short_run=s;short_run.limit=24;
    assert(p11_reserve::expedition_capacity(short_run,gentle)==2);
    assert(p11_reserve::recovery_ticks(short_run,gentle)==4);
    check_choice(short_run,alternatives);
    short_run.limit=11;assert(p11_reserve::recovery_ticks(short_run,gentle)==-1);
    check_choice(short_run,alternatives);
    short_run.limit=0;check_choice(short_run,alternatives);
    short_run.round=1;check_choice(short_run,alternatives);

    State depleted=s;depleted.hp=250;
    Candidate free=costly;free.loss=0;
    Candidate ready=costly;ready.id=7;ready.ready=true;ready.clear=0;ready.loss=700;
    assert(p11_reserve::recovery_ticks(depleted,free)==0);
    assert(p11_reserve::recovery_ticks(depleted,ready)==0);
    assert(p11_reserve::recovery_ticks(depleted,costly)==-1);
    check_choice(depleted,std::array{costly,free,ready});
    State long_run=s;long_run.limit=2000000000;
    Candidate tiny=free;tiny.travel=0;tiny.clear=0;tiny.capture=0;tiny.loss=std::numeric_limits<float>::denorm_min();
    assert(p11_reserve::expedition_capacity(long_run,tiny)==10000);
    assert(p11_reserve::recovery_ticks(long_run,tiny)==1);
    check_choice(long_run,std::array{tiny,free});

    // Invalid inputs cannot become a selected ready camp through the default
    // feasibility bypass; zero loss is the supported non-damaging boundary.
    for(int invalid=0;invalid<10;++invalid) {
        State bad=s;Candidate c=ready;
        switch(invalid) {
        case 0:c.loss=-1;break;
        case 1:c.loss=std::numeric_limits<float>::infinity();break;
        case 2:c.loss=std::numeric_limits<float>::quiet_NaN();break;
        case 3:bad.hp=std::numeric_limits<float>::infinity();break;
        case 4:bad.maximum=std::numeric_limits<float>::quiet_NaN();break;
        case 5:bad.maximum=0;break;
        case 6:bad.hp=-1;break;
        case 7:bad.recall=-1;break;
        case 8:c.travel=-1;break;
        case 9:c.clear=-1;break;
        }
        assert(p11_reserve::recovery_ticks(bad,c)==-1);
        check_choice(bad,std::array{c,gentle});
    }

    // 240 deterministic caller-level alternatives vary health, timing, capital
    // value, commitment, readiness and reward channels without tuning enemies.
    for(int i=0;i<240;++i) {
        State state;state.hp=260+float((i*47)%741);state.maximum=1000;
        state.round=i%31;state.limit=state.round+1+(i*29)%190;
        state.recall=i%2?5:3;state.identity=i%5;state.bag=i%37;
        state.mature=i%3==0;state.field_shop=i%2==0;
        std::array<Candidate,3> jobs{};
        for(int j=0;j<3;++j) {
            auto&c=jobs[j];c.id=j;c.travel=(i+3*j)%13;c.clear=1+(i*5+j)%19;
            c.capture=1+(i+j)%6;c.altar=(i*7+j)%17;c.loss=float((i*17+j*71)%801);
            c.gems=(i+j)%5==0?0:1+(i*3+j*7)%31;c.gold=(i*11+j*23)%151;
            c.allies=(i+j)%3;c.committed=(i+j)%7==0;c.ready=(i+3*j)%23==0;
            if(c.ready)c.clear=0;
        }
        check_choice(state,jobs);
    }
}
