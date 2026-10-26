#include <cassert>
#include "../geodesic/src/policy.h"
#include "../common/src/routes.h"

int main() {
    using namespace p11;using P=p11_geodesic::Policy;using Base=p11_tempo::Policy;
    static_assert(P::bounded_scan&&P::window_size==3&&P::geodesic&&P::trace_scan);
    State s;s.mature=true;
    std::array<Candidate,2> straight{};
    straight[0].id=0;straight[0].travel=2;straight[0].clear=2;straight[0].capture=6;
    straight[0].altar=3;straight[0].gems=20;straight[0].gold=50;
    straight[1]=straight[0];straight[1].id=7;straight[1].travel=4;
    // IDs 0 and 7 have identical tempo spread, so only travel differs.
    assert(P::score(s,straight[0])==Base::score(s,straight[0]));
    assert(choose<P>(s,straight)==0);
    const std::array<std::uint16_t,6> field{0,30,4,65535,3,65535};
    const std::array<int,1> first_goal{1},second_goal{2},blocked_goal{3},altar_goal{4};
    auto routed=straight;
    routed[0].travel=minimum_distance(field,first_goal);
    routed[1].travel=minimum_distance(field,second_goal);
    for(auto&c:routed)c.altar=minimum_distance(field,altar_goal);
    assert(choose<P>(s,routed)==7); // a wall detour reverses Chebyshev ranking

    std::array<Candidate,3> reachable{};
    reachable[0]=routed[1];
    reachable[1]=straight[0];reachable[1].travel=minimum_distance(field,blocked_goal);reachable[1].gems=0;reachable[1].gold=100000;
    reachable[2]=straight[0];reachable[2].id=14;reachable[2].altar=minimum_distance(field,blocked_goal);reachable[2].gems=100000;
    State funding=s;funding.mature=false;
    assert(choose<Base>(funding,reachable)==0); // gold-only sentinel would otherwise remain attractive
    assert(choose<P>(s,reachable)==7);
    assert(!std::isfinite(P::screen(s,reachable[1]))&&!std::isfinite(P::score(s,reachable[2])));
    auto gold=straight[0];gold.gems=0;gold.altar=10000;
    assert(std::isfinite(P::score(s,gold))&&P::score(s,gold)==Base::score(s,gold));

    auto deadline=straight[0];s.round=s.limit-deadline.bank_time(s);
    assert(choose<P>(s,std::span(&deadline,1))==0);
    ++deadline.travel;assert(choose<P>(s,std::span(&deadline,1))==-1);
    s.round=0;

    std::array<Candidate,5> public_menu{};
    for(int i=0;i<5;++i){public_menu[i]=straight[0];public_menu[i].id=i;public_menu[i].gems=60;}
    public_menu[0]=reachable[1];public_menu[0].id=0;
    public_menu[1].gems=1;public_menu[1].committed=true;
    public_menu[2].gems=2;public_menu[4].gems=500;
    const auto order=shortlist<P>(s,public_menu,P::window_size,1,2);
    assert((order==std::vector<int>{4,1,2})); // best routed candidate plus protected current/exploration
    std::vector<Candidate> detailed;
    for(int id:order)detailed.push_back(public_menu[id]);
    assert(choose<P>(s,detailed)==4);
    const auto blocked_current=shortlist<P>(s,public_menu,P::window_size,0,2);
    assert(std::find(blocked_current.begin(),blocked_current.end(),0)==blocked_current.end());

    for(int bag:{0,11,20,40})for(int altar:{0,3,20}) {
        s.bag=bag;s.altar=altar;assert(P::bank(s)==Base::bank(s));
    }
    assert(P::retreat(300,1000,50)==Base::retreat(300,1000,50));
    assert(P::retreat(800,1000,50)==Base::retreat(800,1000,50));
}
