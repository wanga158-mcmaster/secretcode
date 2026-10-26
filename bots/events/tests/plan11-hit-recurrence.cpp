#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "../common/src/events.h"

#ifdef NDEBUG
#error This recurrence fixture requires assertions enabled.
#endif

namespace {
constexpr int sentinel=10000;
constexpr float infinity=std::numeric_limits<float>::infinity();

// Independent scalar oracle: one explicitly rounded subtraction for every
// modeled hit. No quotient, float-product threshold, bit jump or production
// helper participates in the answer. Stagnation terminates impossible kills.
int scalar_hits(float hp,float damage) {
    if(hp<=0) return 0;
    if(!std::isfinite(hp)||std::isnan(damage)||damage<=0) return sentinel;
    if(std::isinf(damage)) return 1;
    for(int hits=1;hits<=sentinel;++hits) {
        volatile float rounded=hp-damage;
        const float next=rounded;
        if(next<=0) return hits;
        if(!(next<hp)) return sentinel;
        hp=next;
    }
    return sentinel;
}

std::uint32_t random_state=0x394e8a17u;
std::uint32_t random_word() {
    random_state^=random_state<<13;
    random_state^=random_state>>17;
    random_state^=random_state<<5;
    return random_state;
}
float from_bits(std::uint32_t bits) {return std::bit_cast<float>(bits);}
int cases=0,max_iterations=0;
void check(float hp,float damage,int iteration_limit=510) {
    const int expected=scalar_hits(hp,damage);
    const auto actual=p11::hit_count(hp,damage);
    assert(actual.hits==expected);
    assert(p11::attacks(hp,damage)==expected);
    assert(actual.iterations>=0&&actual.iterations<=iteration_limit);
    max_iterations=std::max(max_iterations,actual.iterations);
    ++cases;
}

void scalar_counterexamples() {
    const float damage=118.f*1.1f;
    assert(std::bit_cast<std::uint32_t>(damage)==1124191437u);
    assert(5.f*damage==649.f&&scalar_hits(649.f,damage)==6);
    check(649.f,damage);
    assert(p11::attacks(649.f,damage)==6);
    assert(3.f*.1f>=.3f&&scalar_hits(.3f,.1f)==4);
    check(.3f,.1f);
    assert(p11::attacks(.3f,.1f)==4);

    // A jump may not include the normal binade's lower endpoint: the rounding
    // cell becomes narrower below it. The inclusive-endpoint prototype gave
    // 1909, whereas individual float32 subtractions require1908.
    const float boundary_hp=from_bits(0x3f803399u);
    const float boundary_damage=from_bits(0x3a099b00u);
    assert(scalar_hits(boundary_hp,boundary_damage)==1908);
    check(boundary_hp,boundary_damage);
    assert(p11::attacks(boundary_hp,boundary_damage)==1908);

    // Actual clear_schedule caller: the old five-hit product estimate put A
    // first (5/11 < 5/10). Six rounded hits put B first (5/10 < 6/11).
    // B then A costs 60+132=192 exposure; A then B would cost 77+120=197.
    const std::array<p11::Monster,2> monsters{{{649.f,11.f,0},{600.f,10.f,0}}};
    assert(p11::attacks(600.f,damage)==5);
    assert(p11::target_rank(649.f,11.f,damage)>
           p11::target_rank(600.f,10.f,damage));
    const auto clear=p11::clear_schedule(monsters,damage);
    assert(clear.bounded&&clear.turns==11&&clear.loss==192.f);
}

void retained_replay_suffixes() {
    // Native 3.0.4/1.0.2 retained encounter, Payday actor2/camp7, r545-552.
    // See transition-validation/*r540-554-arithmetic.json. Its nine basic hits
    // distinguish intermediate rounding from a product residual, although
    // both arithmetic models happen to predict the same total lethal count.
    const float damage=142.f*1.1f;
    assert(std::bit_cast<std::uint32_t>(damage)==1125921587u);
    constexpr std::array<std::uint32_t,9> observed{{
        1150630298u,1149350708u,1146902939u,1144343758u,1141784577u,
        1137600104u,1132481742u,1120455890u,0u
    }};
    float hp=1350.f;
    for(int at=0;at<int(observed.size());++at) {
        assert(scalar_hits(hp,damage)==9-at);
        check(hp,damage);
        volatile float rounded=hp-damage;
        hp=std::max(0.f,float(rounded));
        assert(std::bit_cast<std::uint32_t>(hp)==observed[at]);
    }
    check(hp,damage);
}

void special_and_boundary_values() {
    constexpr float nan=std::numeric_limits<float>::quiet_NaN();
    constexpr float denorm=std::numeric_limits<float>::denorm_min();
    constexpr float normal=std::numeric_limits<float>::min();
    constexpr float maximum=std::numeric_limits<float>::max();
    for(float hp:{-infinity,-1.f,-0.f,0.f,denorm,normal,1.f,maximum,infinity,nan})
        for(float damage:{-infinity,-1.f,-0.f,0.f,denorm,normal,1.f,maximum,infinity,nan})
            check(hp,damage);
    for(std::uint32_t hp_bits:{1u,2u,3u,0x003fffffu,0x007ffffeu,0x007fffffu,
                              0x00800000u,0x00800001u,0x00800002u,
                              0x7f7ffffdu,0x7f7ffffeu,0x7f7fffffu}) {
        const float hp=from_bits(hp_bits);
        for(float damage:{denorm,2.f*denorm,3.f*denorm,normal,
                          hp,std::nextafter(hp,0.f),std::nextafter(hp,infinity)})
            check(hp,damage);
    }
    // Every finite normal binade, with values on both sides of its lower
    // boundary. Half-ULP and neighboring damages exercise round-to-even,
    // alternating significand parity, binade crossing and stagnation.
    for(std::uint32_t exponent=1;exponent<=254;++exponent) {
        const std::uint32_t boundary=exponent<<23;
        const float unit=from_bits(boundary);
        const float ulp=std::nextafter(unit,infinity)-unit;
        for(std::uint32_t hp_bits:{boundary-1,boundary,boundary+1,boundary+2}) {
            const float hp=from_bits(hp_bits);
            for(float damage:{ulp*.5f,std::nextafter(ulp*.5f,0.f),
                              std::nextafter(ulp*.5f,infinity),ulp,ulp*1.5f})
                check(hp,damage);
            for(float divisor:{1.f,2.f,3.f,7.f,63.f,9999.f,10000.f,10001.f}) {
                const float damage=hp/divisor;
                check(hp,damage);
                check(hp,std::nextafter(damage,0.f));
                check(hp,std::nextafter(damage,infinity));
            }
        }
    }
}

void deterministic_raw_float_pairs() {
    // Whole finite encoding space, independent raw operands. Also exercise
    // signs without letting the NaN/Inf invalid-input branch dominate cases.
    for(int at=0;at<8192;++at) {
        const std::uint32_t hp_bits=random_word()%0x7f800000u;
        const std::uint32_t damage_bits=random_word()%0x7f800000u;
        check(from_bits(hp_bits),from_bits(damage_bits));
        if(at%31==0) check(from_bits(hp_bits|0x80000000u),from_bits(damage_bits));
    }
    // Correlate operands only to cover many different finite hit counts;
    // the answer still comes from the independent scalar recurrence.
    for(int at=0;at<4096;++at) {
        const float hp=from_bits(1u+random_word()%0x7f7fffffu);
        const int divisor=1+int(random_word()%11000u);
        float damage=hp/float(divisor);
        if(at%3==0) damage=std::nextafter(damage,0.f);
        if(at%3==1) damage=std::nextafter(damage,infinity);
        check(hp,damage);
    }
}

void current_catalog_work_bounds() {
    // Current roster attack growth is five (Tavi/Varg) or six (Vendetta).
    // Up to five Monster Strength shards plus100 Power Strength shards fits
    // the current baseline shop. The emblem applies one1.1f damage factor.
    // HP values cover the current weak-to-boss camp catalog. This is an
    // algorithmic outer-loop check, not an engine fuel qualification.
    constexpr std::array<float,5> health{{120.f,1350.f,2400.f,5000.f,20000.f}};
    constexpr std::array<std::array<int,2>,3> roster{{{{45,5}},{{40,5}},{{70,6}}}};
    for(const auto& champion:roster) for(int upgrades=0;upgrades<=105;++upgrades) {
        const float strength=float(champion[0]+champion[1]*upgrades);
        for(float multiplier:{1.f,1.1f}) for(float hp:health)
            check(hp,strength*multiplier,32);
    }
}
} // namespace

int main() {
    scalar_counterexamples();
    retained_replay_suffixes();
    special_and_boundary_values();
    deterministic_raw_float_pairs();
    current_catalog_work_bounds();
    std::printf("hit-recurrence: %d scalar-oracle cases; maximum outer iterations %d\n",
                cases,max_iterations);
}
