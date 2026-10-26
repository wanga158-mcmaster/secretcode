#include <cassert>
#include <functional>
#include <type_traits>
#include "../itinerary/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using p11_itinerary::RouteJob;

RouteJob job(int id,int x,int y,int gems=0,int gold=0) {
    RouteJob result;result.candidate.id=id;result.x=x;result.y=y;
    result.candidate.gems=gems;result.candidate.gold=gold;return result;
}

struct Oracle {int first=-1;double value=-std::numeric_limits<double>::infinity();std::vector<int> ids;};

// Reconstruct every possible one/two-job itinerary independently. In particular,
// neither the production route evaluator nor Candidate's timing/tribute helpers
// are used: all captures finish sequentially and all gems share the final bank.
Oracle brute_force(const State&s,std::span<const RouteJob> jobs) {
    Oracle best;
    std::vector<int> path;
    std::function<void()> enumerate=[&] {
        if(!path.empty()) {
            bool legal=true;double loss=0;int elapsed=0,total_gems=0;
            std::vector<int> captures,ids;
            for(std::size_t at=0;at<path.size();++at) {
                const auto&entry=jobs[path[at]];const auto&c=entry.candidate;
                if(c.id<0||c.travel<0||c.clear<0||c.capture<0||c.altar<0||c.gems<0||c.gold<0||c.allies<0
                    ||(!c.ready&&c.clear>=10000)||!std::isfinite(c.loss)||c.loss<0
                    ||std::find(ids.begin(),ids.end(),c.id)!=ids.end()) {legal=false;break;}
                loss+=c.loss;
                if(double(s.hp)-loss<double(s.maximum)*.25) {legal=false;break;}
                if(at==0)elapsed=c.travel;
                else {
                    const auto&previous=jobs[path[at-1]];
                    elapsed+=std::max(std::abs(entry.x-previous.x),std::abs(entry.y-previous.y));
                }
                elapsed+=c.clear+c.capture;
                if(elapsed>s.limit-s.round) {legal=false;break;}
                captures.push_back(elapsed);ids.push_back(c.id);total_gems+=c.gems;
            }
            if(legal) {
                int bank=elapsed;
                if(total_gems>0) {
                    const int ticks=std::max(s.tribute_minimum,s.tribute_base
                        +int(std::ceil((s.bag+total_gems)*s.tribute_multiplier)));
                    bank+=jobs[path.back()].candidate.altar+ticks;
                    legal=bank<=s.limit-s.round;
                }
                double value=0;
                if(legal)for(std::size_t at=0;at<path.size();++at) {
                    const auto&c=jobs[path[at]].candidate;
                    const int spread=(c.id*3+s.identity*11)%7;
                    const double gold=(s.mature?.04:3.)*c.gold
                        /(1+captures[at]+(s.field_shop?0:s.recall+1)+12*c.allies+spread);
                    const double gems=total_gems>0?3.*c.gems/(1+bank+12*c.allies+spread):0;
                    value+=(gold+gems)*(c.committed?1.10:1.);
                }
                if(legal&&std::isfinite(value)&&(value>best.value||(value==best.value&&ids<best.ids)))
                    best={ids.front(),value,ids};
            }
        }
        if(path.size()==2)return;
        for(int next=0;next<int(std::min(jobs.size(),std::size_t(3)));++next) {
            if(std::find(path.begin(),path.end(),next)!=path.end())continue;
            path.push_back(next);enumerate();path.pop_back();
        }
    };
    enumerate();return best;
}

void verify(const State&s,std::span<const RouteJob> jobs,int expected) {
    assert(brute_force(s,jobs).first==expected);
    assert(p11_itinerary::Policy::select_route(s,jobs)==expected);
}
}

int main() {
    using p11_itinerary::Policy;
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan&&Policy::route_select);
    State state;state.mature=true;state.field_shop=true;
    std::array<RouteJob,3> camps{{job(0,10,0,10),job(7,-20,0,12),job(14,-21,0,12)}};
    camps[0].candidate.travel=10;camps[1].candidate.travel=20;camps[2].candidate.travel=21;
    for(auto&camp:camps)camp.candidate.altar=5;
    std::array<Candidate,3> individual{{camps[0].candidate,camps[1].candidate,camps[2].candidate}};
    assert(p11::choose<p11_tempo::Policy>(state,individual)==0);
    verify(state,camps,7); // The neighboring pair beats the best individual camp.
    assert((brute_force(state,camps).ids==std::vector<int>{7,14}));
    assert(Policy::score(state,individual[0])==p11_tempo::Policy::score(state,individual[0]));
    assert(Policy::screen(state,individual[0])==p11_tempo::Policy::score(state,individual[0]));
    assert(Policy::bank(state)==p11_tempo::Policy::bank(state));
    assert(Policy::retreat(400,1000,50)==p11_tempo::Policy::retreat(400,1000,50));

    // The entire route spends one HP budget, including supplied ready-job loss.
    camps[1].candidate.loss=300;camps[2].candidate.loss=450;
    verify(state,camps,7); // Exactly 25% of maximum HP remains.
    camps[2].candidate.loss=451;verify(state,camps,0);
    camps[1].candidate.ready=camps[2].candidate.ready=true;
    camps[1].candidate.loss=camps[2].candidate.loss=400;verify(state,camps,0);
    camps[1].candidate.loss=camps[2].candidate.loss=0;
    camps[2].candidate.id=7;verify(state,camps,0); // One group cannot pay twice.

    State deadline;deadline.field_shop=true;deadline.limit=8;
    std::array<RouteJob,1> one{{job(0,0,0,0,10)}};
    one[0].candidate.travel=2;one[0].candidate.altar=100;
    verify(deadline,one,0); // Gold pays on capture, even when no bank can finish.
    deadline.limit=7;verify(deadline,one,-1);
    deadline.limit=9;one[0].candidate.gems=10;one[0].candidate.altar=1;
    verify(deadline,one,-1); // Capture fits; bank at tick 10 does not.
    deadline.limit=10;verify(deadline,one,0);
    deadline.bag=11;verify(deadline,one,-1); // Tribute includes the existing bag.
    deadline.limit=11;verify(deadline,one,-1);
    deadline.limit=12;verify(deadline,one,0);
    deadline.tribute_multiplier=.25;deadline.tribute_base=2;deadline.tribute_minimum=9;
    deadline.limit=17;verify(deadline,one,-1);
    deadline.limit=18;verify(deadline,one,0); // Queried tribute parameters matter.

    // Only the terminal altar is charged. The second capture can make a gem
    // itinerary bankable even though the first camp's own altar is too distant.
    std::array<RouteJob,2> terminal{{job(0,0,0,10),job(7,0,0,0,1)}};
    terminal[0].candidate.capture=terminal[1].candidate.capture=1;
    terminal[0].candidate.altar=200;
    deadline=State{};deadline.field_shop=true;deadline.limit=3;
    verify(deadline,terminal,0);
    assert((brute_force(deadline,terminal).ids==std::vector<int>{0,7}));
    terminal[1].candidate.clear=1;verify(deadline,terminal,7); // No clear/capture overlap.

    // Full Chebyshev anchor distance is charged, with no interaction-radius
    // subtraction. At the horizon the extra diagonal step disallows the pair.
    terminal[1].candidate.clear=0;terminal[1].x=terminal[1].y=1;
    verify(deadline,terminal,7);
    deadline.limit=4;verify(deadline,terminal,0);

    State clean;clean.field_shop=true;
    one[0]=job(3,0,0,0,10);one[0].candidate.ready=true;
    for(auto member:std::array<int Candidate::*,7>{&Candidate::travel,&Candidate::clear,&Candidate::capture,
            &Candidate::altar,&Candidate::gems,&Candidate::gold,&Candidate::allies}) {
        auto invalid=one;invalid[0].candidate.*member=-1;verify(clean,invalid,-1);
    }
    one[0].candidate.id=-1;verify(clean,one,-1);one[0].candidate.id=3;
    for(float loss:std::array<float,4>{-1,std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),751}) {
        one[0].candidate.loss=loss;verify(clean,one,-1);
    }
    one[0].candidate.loss=750;verify(clean,one,3);
    one[0].candidate.loss=0;one[0].candidate.clear=10000;one[0].candidate.capture=1;
    clean.limit=10001;verify(clean,one,3); // Ready only exempts the clear sentinel.
    clean.limit=10000;verify(clean,one,-1); // Its supplied work still takes time.
    clean.limit=10001;one[0].candidate.ready=false;verify(clean,one,-1);
    verify(clean,std::span<const RouteJob>{},-1);
    one[0]=job(3,0,0);verify(clean,one,3); // Finite zero rewards remain selectable.

    std::array<RouteJob,3> ties{{job(14,0,0,0,10),job(7,0,0,0,10),job(0,0,0,0,10)}};
    verify(clean,ties,0);
    assert((brute_force(clean,ties).ids==std::vector<int>{0,7}));
    std::reverse(ties.begin(),ties.end());verify(clean,ties,0);
    ties[0].candidate.gold=0;ties[1].candidate.gold=ties[2].candidate.gold=0;
    assert((brute_force(clean,ties).ids==std::vector<int>{0})); // Singleton wins a prefix tie.
    verify(clean,ties,0);

    // 384 bounded menus cover identities, early/mature gold, field/spawn shops,
    // custom tribute settings, crowding, commitment, loss and varying horizons.
    int cases=0;
    for(int mode=0;mode<8;++mode)for(int code=0;code<48;++code) {
        State s;s.identity=(mode&4)?5:0;s.mature=mode&1;s.field_shop=mode&2;
        s.round=100;s.limit=s.round+15+code%43;s.bag=code*3%23;s.recall=2+code%6;
        s.hp=1000-code%4*50;s.tribute_multiplier=code%2?.15:.1;
        s.tribute_base=code%3;s.tribute_minimum=1+code%2;
        std::array<RouteJob,3> menu{};
        for(int j=0;j<3;++j) {
            auto&entry=menu[j];auto&c=entry.candidate;
            c.id=code%2?2+j*3:j*7;entry.x=(code*(j+1)+j*9)%21-10;
            entry.y=(code*(j+3)+j*5)%19-9;
            c.travel=(code/(j+1)+j*3)%13;c.clear=(code+2*j)%5;c.capture=1+(code+3*j)%6;
            c.altar=(code*3+j*5)%15;c.gems=(code*(j+2)+j*7)%31;
            c.gold=(code*(j+3)+j*11)%41;c.allies=(code+j)%3;
            c.loss=float((code+j*5)%8*80);c.ready=code%4==0;c.committed=code&(1<<j);
        }
        if(code%11==0)menu[1].candidate.id=menu[0].candidate.id;
        if(code%17==0)menu[2].candidate.loss=-1;
        const auto bounded=std::span<const RouteJob>(menu.data(),std::size_t(code%4));
        assert(Policy::select_route(s,bounded)==brute_force(s,bounded).first);++cases;
    }
    assert(cases==384);
}
