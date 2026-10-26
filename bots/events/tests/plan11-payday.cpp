#include <cassert>
#include <type_traits>
#include "../payday/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using p11_payday::Policy;

Candidate job(int id,int gold,int gems=0,int travel=0) {
    Candidate c;c.id=id;c.gold=gold;c.gems=gems;c.travel=travel;c.capture=1;return c;
}

struct Receipt {int gold=0,gems=0,capture=-1,bank=-1;};
// Independent bounded phase oracle: work consumes action slots; capture pays
// before owner start; a channel ticks at owner end and pays at a later start.
// Supplied clear/capture costs remain assumptions, as in the real selector.
Receipt tick(const State&s,const Candidate&c) {
    Receipt r;
    if(c.id<0||s.identity<0||s.round<0||s.limit<0||s.bag<0||s.recall<0
        ||c.travel<0||c.clear<0||c.capture<0||c.altar<0||c.allies<0
        ||c.gold<0||c.gems<0||!std::isfinite(s.tribute_multiplier)
        ||s.tribute_multiplier<0||s.tribute_base<0||s.tribute_minimum<1)return r;
    int travel=c.travel,clear=c.clear,capture=c.capture,altar=c.altar;
    int channel_ticks=0;
    if(s.bag+c.gems>0) {
        while(double(channel_ticks)<double(s.bag+c.gems)*s.tribute_multiplier)++channel_ticks;
        channel_ticks=std::max(s.tribute_minimum,s.tribute_base+channel_ticks);
    }
    bool paid=false,channel=false,banked=false;
    for(int round=0;round<=s.limit-s.round;++round) {
        // Capture phase, before this owner's channel completion.
        if(!paid&&travel==0&&clear==0&&capture==0) {
            paid=true;r.capture=round;r.gold=c.gold;
        }
        // Owner start.
        if(channel&&channel_ticks==0&&!banked) {
            banked=true;r.bank=round;r.gems=c.gems;
        }
        // Action phase.
        if(!paid) {
            if(travel>0)--travel;
            else if(clear>0)--clear;
            else if(capture>0)--capture;
        } else if(altar>0)--altar;
        else if(!channel&&s.bag+c.gems>0)channel=true;
        // Owner end, including the channel's starting turn.
        if(channel&&channel_ticks>0)--channel_ticks;
    }
    return r;
}

double oracle_score(const State&s,const Candidate&c,const Receipt&r) {
    if(r.gold<=0&&r.gems<=0)return -std::numeric_limits<double>::infinity();
    const int spread=(3*c.id+11*s.identity)%7;
    const double gems=r.gems>0?3.*r.gems/(1+r.bank+12*c.allies+spread):0.;
    const double gold=r.gold>0?(s.mature?.04:3.)*r.gold
        /(1+r.capture+(s.field_shop?0:s.recall+1)+12*c.allies+spread):0.;
    return (gems+gold)*(c.committed?1.10:1.);
}

int verify(const State&s,std::span<const Candidate>menu) {
    int expected=-1;double best=-std::numeric_limits<double>::infinity();
    for(const auto&c:menu) {
        const auto receipt=tick(s,c);
        const auto payable=p11_payday::payable_rewards(s,c);
        assert(payable.gold==receipt.gold&&payable.gems==receipt.gems);
        const double expected_score=oracle_score(s,c,receipt),actual=Policy::score(s,c);
        assert(Policy::screen(s,c)==actual);
        if(std::isfinite(expected_score))
            assert(std::fabs(actual-expected_score)<=1e-12*std::max(1.,std::fabs(expected_score)));
        else assert(!std::isfinite(actual)&&actual<0);
        const bool feasible=c.ready||(c.clear<10000&&s.hp-c.loss>=s.maximum*.25f);
        if(feasible&&std::isfinite(expected_score)&&expected_score>best) {
            best=expected_score;expected=c.id;
        }
    }
    // Actual production farm selector: every alternative is supplied together.
    assert(p11::choose<Policy>(s,menu)==expected);
    return expected;
}
} // namespace

int main() {
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan);

    State s;s.round=100;s.limit=104;s.recall=20;
    std::array<Candidate,2> menu{{job(0,100,80,1),job(7,20,0,1)}};
    menu[0].clear=1;menu[0].capture=2;menu[0].altar=20;
    assert(menu[0].payout()==4&&menu[0].bank_time(s)==32);
    assert(p11::choose<p11_tempo::Policy>(s,menu)==7);
    assert(verify(s,menu)==0); // Missed gems preserve exact-deadline capture gold.
    assert(Policy::score(s,menu[0])==300./26); // Shopping may finish after the deadline.
    s.limit=103;assert(verify(s,menu)==7); // Capture itself must still finish.
    menu[0]=job(0,1000,0,4);assert(verify(s,menu)==7); // No post-horizon gold credit.
    menu[1].gold=0;assert(verify(s,menu)==-1); // No zero-reward fallback job.
    assert(!std::isfinite(Policy::score(s,job(0,0))));

    s=State{};s.limit=4;s.field_shop=true;
    menu={{job(0,1,10,1),job(7,1)}};menu[0].altar=1;
    assert(verify(s,menu)==0); // Bank completes exactly at round four.
    auto payable=p11_payday::payable_rewards(s,menu[0]);
    assert(payable.gold==1&&payable.gems==10);
    s.limit=3;assert(verify(s,menu)==7);
    payable=p11_payday::payable_rewards(s,menu[0]);
    assert(payable.gold==1&&payable.gems==0);
    s.bag=11;s.limit=4;assert(verify(s,menu)==7); // Channel uses the existing bag too.
    s.limit=6;assert(verify(s,menu)==0);

    s=State{};s.tribute_multiplier=.25;s.tribute_base=2;s.tribute_minimum=7;
    menu={{job(0,1,10),job(7,2)}};menu[0].altar=2;
    s.limit=9;assert(verify(s,menu)==7);s.limit=10;assert(verify(s,menu)==0);
    s.bag=11;s.limit=10;assert(verify(s,menu)==7); // Actual 21-gem bag needs eight ticks.
    s.limit=11;assert(verify(s,menu)==0);
    s.bag=31;assert(verify(s,menu)==7); // Actual 41-gem bag lengthens the channel.
    s.limit=16;assert(verify(s,menu)==0);

    s=State{};s.mature=true;s.field_shop=true;
    menu={{job(0,20,10,2),job(7,10,5,1)}};
    for(auto&c:menu) {c.allies=2;c.committed=true;c.altar=3;}
    verify(s,menu);
    for(const auto&c:menu)assert(Policy::score(s,c)==p11_tempo::Policy::score(s,c));
    s.mature=false;s.field_shop=false;s.identity=3;verify(s,menu);
    for(const auto&c:menu)assert(Policy::score(s,c)==p11_tempo::Policy::score(s,c));
    menu[0].loss=900;assert(verify(s,menu)==7);
    menu[0].ready=true;assert(verify(s,menu)==0);
    menu[0].ready=false;menu[0].clear=10000;assert(verify(s,menu)==7);
    s.limit=s.round-1;assert(verify(s,menu)==-1);
    verify(State{},std::span<const Candidate>{});

    // Malformed costs/IDs/settings never produce a selectable reward.
    s=State{};
    for(int field=0;field<10;++field) {
        auto bad=job(0,100,10);
        switch(field) {
        case 0:bad.id=-1;break;case 1:bad.travel=-1;break;case 2:bad.clear=-1;break;
        case 3:bad.capture=-1;break;case 4:bad.altar=-1;break;case 5:bad.allies=-1;break;
        case 6:bad.gold=-1;break;case 7:bad.gems=-1;break;
        case 8:s.tribute_multiplier=std::numeric_limits<double>::quiet_NaN();break;
        case 9:s.bag=-1;break;
        }
        menu={{bad,job(7,1)}};
        if(field<8)assert(verify(s,menu)==7);else assert(verify(s,menu)==-1);
        s=State{};
    }
    auto huge=job(0,100,10);huge.altar=std::numeric_limits<int>::max();
    payable=p11_payday::payable_rewards(s,huge);
    assert(payable.gold==100&&payable.gems==0&&std::isfinite(Policy::score(s,huge)));

    s=State{};s.bag=20;s.altar=10;s.limit=5;
    assert(p11_tempo::Policy::bank(s)&&!Policy::bank(s));
    menu={{job(0,100,80,1),job(7,1)}};assert(verify(s,menu)==0);
    s=State{};s.bag=10;s.altar=1;s.limit=1;assert(!Policy::bank(s));
    s.limit=2;assert(Policy::bank(s)); // Exact current-bag bank equality.
    s.bag=0;assert(!Policy::bank(s));
    s=State{};s.bag=11;s.altar=2;s.tribute_multiplier=.25;
    s.tribute_base=2;s.tribute_minimum=7;s.limit=8;assert(!Policy::bank(s));
    s.limit=9;assert(Policy::bank(s));
    s.bag=21;s.tribute_minimum=1;assert(!Policy::bank(s));
    s.limit=10;assert(Policy::bank(s));
    s=State{};s.bag=10;s.altar=30;assert(!Policy::bank(s)); // Preserve Tempo's bank preference.
    for(int hp:{100,400,800})for(int incoming:{0,50,200})
        assert(Policy::retreat(hp,1000,incoming)==p11_tempo::Policy::retreat(hp,1000,incoming));

    int cases=0,capture_only=0,both=0,missed=0;
    for(int mode=0;mode<6;++mode)for(int code=0;code<64;++code) {
        State state;state.round=100;state.limit=100+code%37;
        state.identity=mode;state.mature=mode&1;state.field_shop=mode&2;
        state.recall=2+code%8;state.bag=code*3%29;
        state.tribute_multiplier=mode%3==0?.25:mode%3==1?.1:0.;
        state.tribute_base=code%3;state.tribute_minimum=1+code%4;
        state.hp=1000-code%4*50;
        std::array<Candidate,3> alternatives{};
        for(int j=0;j<3;++j) {
            auto&c=alternatives[j];c.id=j*7+code%3;
            c.travel=(code/(j+1)+j*3)%10;c.clear=(code+2*j)%5;
            c.capture=(code+3*j)%7;c.altar=(code*3+j*5)%15;
            c.gems=(code*(j+2)+j*7)%36;c.gold=(code*(j+3)+j*11)%41;
            c.allies=(code+j)%3;c.committed=code&(1<<j);
            c.loss=float((code+j*5)%12*80);c.ready=code%4==0;
            const auto r=tick(state,c);
            if(r.gold>0&&r.gems==0)++capture_only;
            if(r.gold>0&&r.gems>0)++both;
            if(r.gold==0&&r.gems==0)++missed;
        }
        verify(state,alternatives);++cases;
    }
    assert(cases==384&&capture_only>0&&both>0&&missed>0);
}
