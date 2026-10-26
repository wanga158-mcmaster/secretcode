#include "../src/config.h"
#include "../src/mechanics.h"
#include <cassert>
#include <cstdint>
using h12_liquidation_clock::CampMemory;
int main(){
    constexpr std::uint64_t a=1,b=2,c=4,hidden=8;
    CampMemory<std::uint64_t> memory;
    assert(memory.refresh(a|b,a|b)==(a|b));
    assert(memory.refresh(c,c)==(a|b|c)); // Turning away keeps both old blockers.
    assert(memory.refresh(a|c,a)==(a|b)); // A seen empty tile can be captured.
    assert(memory.refresh(b,0)==a);       // A later personal empty view clears B.
    assert(memory.refresh(0,hidden)==a);  // Occupancy outside personal vision is not remembered.
    assert(memory.refresh(a,0)==0);
}
