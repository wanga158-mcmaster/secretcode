#include "freecenter.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace mb::freecenter;
int main() {
    // Exact normal-v40 witness: enemy-centered casts cover two; free midpoint covers three.
    const std::array enemies{Enemy{{17,9},160,0},Enemy{{16,9},160,0},Enemy{{16,11},160,20}};
    const std::array awake_camp{Camp{{18,10},120,false}};
    auto s=evaluate({17,9},{16,10},1,enemies,awake_camp);
    assert(s.eligible&&s.gain==160&&s.lost_camp==120&&s.added_retaliation==20&&s.net==20);
    s=evaluate({17,9},{17,10},1,enemies,awake_camp);
    assert(s.eligible&&s.gain==160&&s.lost_camp==0&&s.added_retaliation==20&&s.net==140);
    // A wider enemy footprint does not excuse losing the original damage/control commitment.
    s=evaluate({17,9},{16,11},1,enemies,{});assert(!s.eligible);
    // Waking a new damaging camp is a distinct risk, not a positive damage credit.
    const std::array sleeping{Camp{{15,10},100,true}};
    s=evaluate({17,9},{16,10},1,enemies,sleeping);assert(!s.eligible&&s.new_danger);
    // Exactly breaking even leaves the original cast; there is no gain from a tie.
    const std::array equal_cost{Camp{{18,10},140,false}};
    s=evaluate({17,9},{16,10},1,enemies,equal_cost);assert(!s.eligible&&s.net==0);
    const std::array expensive{Camp{{18,10},141,false}};
    s=evaluate({17,9},{16,10},1,enemies,expensive);assert(!s.eligible&&s.net<0);
    // No added enemy means no action change, even if a center would hit an extra camp.
    s=evaluate({17,9},{16,9},1,enemies,{});assert(!s.eligible&&s.gain==0);
    // The same geometry can be unsafe when retaliation consumes all added value.
    const std::array strong_ren{Enemy{{17,9},160,0},Enemy{{16,9},160,0},Enemy{{16,11},160,161}};
    s=evaluate({17,9},{16,10},1,strong_ren,{});assert(!s.eligible);
    // A harmless newly touched camp is not the damaging-camp wake veto.
    const std::array harmless{Camp{{15,10},36,false}};
    s=evaluate({17,9},{16,10},1,enemies,harmless);assert(s.eligible);
    // Translating and mirroring the witness preserves the geometric/resource law.
    const std::array mirrored{Enemy{{12,20},160,0},Enemy{{13,20},160,0},Enemy{{13,18},160,20}};
    const std::array mirrored_camp{Camp{{11,19},120,false}};
    s=evaluate({12,20},{13,19},1,mirrored,mirrored_camp);assert(s.eligible&&s.net==20);
    std::puts("freecenter: ten decision/cost cases passed");
}
