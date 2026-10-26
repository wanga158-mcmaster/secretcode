#include <cassert>
#include "../surveyor/src/policy.h"

int main() {
    using namespace p11;using P=p11_surveyor::Policy;
    static_assert(P::bounded_scan&&P::window_size==3&&P::trace_scan);
    State s;s.mature=true;
    std::array<Candidate,3> menu{};
    menu[0].id=0;menu[0].gems=40;menu[0].travel=3;menu[0].clear=12;menu[0].altar=6;
    menu[0].hp=1000;menu[0].incoming=40;menu[0].loss=500;menu[0].known=true;
    menu[1].id=1;menu[1].gems=24;menu[1].travel=2;menu[1].clear=4;menu[1].altar=3;
    menu[1].hp=400;menu[1].incoming=10;menu[1].loss=30;menu[1].known=true;
    menu[2]=menu[0];menu[2].id=2;menu[2].gems=1000;menu[2].loss=900;
    // Supply every real alternative to the caller: detailed exposure reverses
    // the tempting public screen, while the nominal largest reward is lethal.
    assert(P::screen(s,menu[0])>P::screen(s,menu[1]));
    assert(P::score(s,menu[0])<P::score(s,menu[1]));
    assert(choose<P>(s,menu)==1);
    std::reverse(menu.begin(),menu.end());assert(choose<P>(s,menu)==1);

    std::array<Candidate,2> fog{};
    fog[0]=menu[1];fog[0].id=5;fog[0].known=false;
    fog[1]=fog[0];fog[1].id=6;fog[1].known=true;
    assert(P::screen(s,fog[0])==P::screen(s,fog[1]));
    assert(P::score(s,fog[0])<P::score(s,fog[1]));
    assert(choose<P>(s,fog)==6); // no hidden-state certainty enters screening

    Candidate deadline;deadline.id=7;deadline.gems=20;deadline.travel=2;
    deadline.clear=1;deadline.altar=3;deadline.known=true;
    const int duration=P::finish(s,deadline);
    assert(duration==14); // 2 travel +1 hit +6 capture +3 transport +2 tribute
    s.round=s.limit-duration;
    assert(choose<P>(s,std::span(&deadline,1))==7);
    ++s.round;assert(choose<P>(s,std::span(&deadline,1))==-1);

    s.round=0;std::array<Candidate,2> custody{};
    custody[0]=deadline;custody[0].id=8;custody[0].ready=true;custody[0].clear=10000;
    custody[1]=custody[0];custody[1].id=9;custody[1].committed=true;
    assert(choose<P>(s,custody)==9); // completed ready incumbent survives equal alternatives
    custody[1].altar=3000;assert(choose<P>(s,custody)==8); // continuity cannot waive the deadline

    // A high-value late public index must compete with the complete menu;
    // neither the public policy nor the detailed selector uses a map prefix.
    std::array<Candidate,12> broad{};
    for(int i=0;i<int(broad.size());++i) {broad[i]=deadline;broad[i].id=i;broad[i].gems=10;}
    broad.back().gems=40;
    assert(P::screen(s,broad.back())>P::screen(s,broad.front()));
    assert(choose<P>(s,broad)==11);
    std::array<Candidate,20> public_menu{};
    for(int i=0;i<20;++i){public_menu[i]=deadline;public_menu[i].id=i;public_menu[i].gems=10;}
    public_menu[19].gems=50;public_menu[17].gems=1;public_menu[17].committed=true;
    auto order=shortlist<P>(s,public_menu,8,17,18);
    assert(order.size()==8);
    for(const int id:{19,17,18})assert(std::find(order.begin(),order.end(),id)!=order.end());
    std::vector<Candidate> detailed;for(const int id:order)detailed.push_back(public_menu[id]);
    assert(choose<P>(s,detailed)==19); // late group reaches actual detailed caller
    auto sorted=order;std::sort(sorted.begin(),sorted.end());
    assert(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end());
    auto actual=shortlist<P>(s,public_menu,P::window_size,17,18);
    assert(actual.size()==3);
    for(const int id:{19,17,18})assert(std::find(actual.begin(),actual.end(),id)!=actual.end());
    Candidate reveal=public_menu[19];reveal.known=false;reveal.incoming=40;reveal.loss=900;
    assert(!feasible(s,reveal)&&P::admissible(s,reveal));
    reveal.known=true;assert(!P::admissible(s,reveal));

    Candidate empty;empty.id=10;assert(choose<P>(s,std::span(&empty,1))==-1);
    State capital;Candidate gold;gold.id=11;gold.gold=100;gold.known=true;gold.clear=4;
    assert(P::finish(capital,gold)==16); // capture10 + personal recall/shop6
    capital.field_shop=true;assert(P::finish(capital,gold)==10);
    gold.altar=500;assert(P::finish(capital,gold)==10); // gold never needs tribute transport
    s.bag=23;s.altar=10;assert(!P::bank(s));s.bag=24;assert(P::bank(s));
    s.bag=11;s.altar=2;assert(P::bank(s));s.bag=0;assert(!P::bank(s));
    assert(P::retreat(300,1000,0));assert(P::retreat(400,1000,125));
    assert(!P::retreat(800,1000,100));
}
