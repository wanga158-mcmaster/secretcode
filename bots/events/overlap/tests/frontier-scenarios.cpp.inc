#include "../overlap/src/policy.h"
#include "../overlap/src/frontier.h"
#include <cassert>

int main() {
    using Policy=p11_overlap::Policy;
    std::uint32_t seed=0x4381u;auto next=[&]{seed=1664525u*seed+1013904223u;return seed;};
    p11::State s;
    for(int frame=0;frame<2048;++frame) {
        s.round=int(next()%2000);s.bag=int(next()%40);s.identity=int(next()%5);
        s.mature=next()%2;s.field_shop=next()%2;
        std::vector<p11::Candidate> preview;
        for(int i=0;i<int(frame%64);++i) {
            p11::Candidate c;c.id=i*2;c.travel=int(next()%35);c.clear=int(next()%20);
            c.capture=6;c.altar=int(next()%20);c.gems=int(next()%12);c.gold=int(next()%30);
            c.committed=next()%2;preview.push_back(c);
        }
        const int owned=int(next()%130)-2,explore=int(next()%130)-2;
        auto equal=[&](int width) {
            assert(p11_overlap::unique_three_frontier<Policy>(s,preview,width,owned,explore)
                ==p11::shortlist<Policy>(s,preview,width,owned,explore));
        };
        equal(3);equal(2);equal(8);
        if(preview.size()>1) {preview[1].id=preview[0].id;equal(3);preview[0].id=-1;equal(3);}
    }
    // Stable ties, every incumbent/explore membership and empty finite sets.
    std::vector<p11::Candidate> ties(51);
    for(int i=0;i<51;++i){ties[i].id=i;ties[i].gold=0;ties[i].gems=0;}
    for(int owned=-1;owned<53;++owned)for(int explore=-1;explore<53;++explore)
        assert(p11_overlap::unique_three_frontier<Policy>(s,ties,3,owned,explore)
            ==p11::shortlist<Policy>(s,ties,3,owned,explore));
}
