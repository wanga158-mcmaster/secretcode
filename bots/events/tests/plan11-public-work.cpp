#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "../common/src/public_work.h"
#include "../overlap/src/policy.h"

namespace {
int computations=0;
int actual(float health,float damage) {++computations;return p11::attacks(health,damage);}

void invalidation() {
    p11::PublicHitWork cache;
    const auto check=[&](float hp,float damage,int expected_calls) {
        assert(cache.get(hp,damage,actual)==p11::attacks(hp,damage));
        assert(computations==expected_calls);
    };
    computations=0;
    check(649,129.8f,1);assert(cache.get(649,129.8f,actual)==6);
    check(649,129.8f,1);
    check(649,std::nextafter(129.8f,1000.f),2);
    check(std::nextafter(649.f,1000.f),129.8f,3);
    check(649,129.8f,4);

    const float infinity=std::numeric_limits<float>::infinity();
    const float nan1=std::bit_cast<float>(std::uint32_t{0x7fc00001});
    const float nan2=std::bit_cast<float>(std::uint32_t{0x7fc00002});
    check(nan1,100,5);check(nan1,100,5);check(nan2,100,6);
    check(100,nan1,7);check(100,nan1,7);check(100,nan2,8);
    check(infinity,100,9);check(100,infinity,10);
    check(100,0.f,11);check(100,-0.f,12);
    check(0.f,100,13);check(-0.f,100,14);
    check(-1,100,15);check(100,-1,16);

    // A callback that fails before completing may not publish a new key or
    // corrupt the old complete value; this matches the turn-boundary retry.
    bool threw=false;
    try { cache.get(123,55,[](float,float)->int{throw std::runtime_error("fixture");}); }
    catch(const std::runtime_error&) {threw=true;}
    assert(threw);check(100,-1,16);check(123,55,17);check(123,55,17);
}

template<class Policy> void choices() {
    std::array<p11::PublicHitWork,34> cache{};
    constexpr std::array health{120.f,240.f,360.f,2520.f,2700.f,2940.f,5480.f};
    for(int frame=0;frame<64;++frame) {
        p11::State state;state.round=frame%2?1990:100;state.limit=2000;
        state.hp=state.maximum=1000;state.bag=frame%3*7;state.identity=frame%13;
        state.strength=40.f+frame%4*23.5f;state.mature=frame%2;
        const float damage=state.strength*(frame%3?1.f:1.1f);
        std::vector<p11::Candidate> reference,cached;
        for(int id=0;id<34;++id) {
            p11::Candidate c;c.id=id;c.hp=health[(id+frame/8)%health.size()];
            c.travel=(id*3+frame)%15;c.altar=(id+frame)%7;
            c.gems=1+(id*7)%33;c.gold=(id*11)%60;c.incoming=id%3;
            c.clear=p11::attacks(c.hp,damage);c.capture=6;c.committed=id==frame%34;
            c.loss=c.incoming*(c.clear+2);
            reference.push_back(c);
            c.clear=cache[id].get(c.hp,damage,actual);
            assert(c.clear==reference.back().clear);
            cached.push_back(c);
        }
        const int calls=computations;
        for(const auto&c:cached)assert(cache[c.id].get(c.hp,damage,actual)==c.clear);
        assert(computations==calls); // warm values genuinely skip the hit kernel
        assert(p11::shortlist<Policy>(state,reference,3,frame%34,(frame+17)%34)
            ==p11::shortlist<Policy>(state,cached,3,frame%34,(frame+17)%34));
        assert(p11::choose<Policy>(state,reference)==p11::choose<Policy>(state,cached));
        assert(p11::choose_partial<Policy>(state,reference)==p11::choose_partial<Policy>(state,cached));
    }
}
}

int main() {
    invalidation();
    choices<p11_tempo::Policy>();
    choices<p11_overlap::Policy>();
}
