#include "../src/residual_observation.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>

int main() {
    namespace c=residual::observation::contract;
    unsigned checks{};
    auto check=[&](bool okay,const char* name){++checks;if(!okay){std::cerr<<name<<'\n';std::exit(1);}};
    // The SDK catalogue is in physical serialization order, with explicit IDs.
    // It is not a vector indexed by native ID and zero is not an absent handle.
    constexpr std::array<std::uint32_t,22> layout={
        91,31,22,60,2, 70,0,9,7, 0,1,10,7,
        0,9,11,0,1, 812,2,3,4};
    c::GroupHeader a,b;
    check(c::group_header(layout,0,a),"first complete explicit-ID group");
    check(a.handle==91&&a.gold==31&&a.gems==22&&a.respawn==60&&a.members==2,"first metadata exact");
    check(c::group_header(layout,13,b),"second group after two four-word members");
    check(b.handle==0&&b.members==1&&b.respawn==0,"native group zero and duration zero retained");
    check(layout[5]==70&&layout[9]==0&&layout[18]==812,"member handle zero and sparse IDs retained");
    for(std::size_t cut=0;cut<13;++cut)
        check(!c::group_header(std::span<const std::uint32_t>{layout.data(),cut},0,a),"truncated first group rejected");
    check(!c::group_header(layout,layout.size()+1,a),"cursor beyond input rejected without subtraction wrap");
    auto invalid=layout;invalid[4]=0;
    check(!c::group_header(invalid,0,a),"empty source is not a complete group");
    invalid=layout;invalid[3]=0xffffffffu;
    check(!c::group_header(invalid,0,a),"negative ABI respawn duration rejected");
    invalid=layout;invalid[4]=0xffffffffu;
    check(!c::group_header(invalid,0,a),"oversized member count cannot wrap past end");
    const auto unknown=c::group_reading(0,0,0,5);
    check(unknown.index==0&&unknown.life==residual::Life::Unknown&&unknown.respawn==-1,"unknown is no living or timer observation");
    const auto reward=c::group_reading(0,2,0,5);
    check(reward.life==residual::Life::Reward&&reward.respawn==-1,"reward distinct from respawn zero");
    const auto blocked_respawn=c::group_reading(0,3,0,5);
    check(blocked_respawn.life==residual::Life::Respawning&&blocked_respawn.respawn==0,"timer zero is still respawning");
    const auto living=c::group_reading(0,1,48,5);
    check(living.life==residual::Life::Living&&living.respawn==-1,"only observed alive publishes living");
    check(!blocked_respawn.capture_progress_known&&blocked_respawn.captured==0&&blocked_respawn.required==5,
        "configured capture duration does not fabricate capture progress");
    check(c::packed_bytes(0)==0&&c::packed_bytes(1)==1&&c::packed_bytes(8)==1&&c::packed_bytes(9)==2,
        "packed export storage is ceil(area/8), including partial bytes");
    check(c::packed_bytes(1200)==150,"1200 map cells occupy 150 SDK linear bytes");
    // Linear bit order continues across row boundaries; it is neither one byte
    // per tile nor the SDK's internal 8-by-8 chunk layout.
    constexpr std::array<std::uint8_t,6> packed={0x81,0x01,0,0,0x80,0x81};
    for(std::size_t cell=0;cell<41;++cell) {
        const bool expected=cell==0||cell==7||cell==8||cell==39||cell==40;
        check(c::packed_test(packed,41,cell)==expected,"local packed bits cross byte and width-40 row boundary exactly");
    }
    check(!c::packed_test(packed,41,47),"set padding bit beyond map area remains absent");
    check(!c::packed_test(std::span<const std::uint8_t>{packed.data(),1},41,8),"short packed storage is never read past end");
    // The same helper used by bootstrap tests each immutable layer locally.
    constexpr std::array<std::uint8_t,6> wall={1,0,0,0,0,0},hole={0x80,0,0,0,0,0},
        altar={0,1,0,0,0,0},enemy_spawn={0,0,0,0,0x80,0},own_spawn={0,0,0,0,0,1};
    for(std::size_t cell=0;cell<41;++cell) {
        const bool blocked=c::packed_test(wall,41,cell)||c::packed_test(hole,41,cell)||
            c::packed_test(altar,41,cell)||c::packed_test(enemy_spawn,41,cell);
        check(blocked==(cell==0||cell==7||cell==8||cell==39),"ordinary terrain union preserves walls/holes/altars/enemy spawn");
        if(cell==40)check(c::packed_test(own_spawn,41,cell)&&!blocked,"own spawn remains a separate walkable geometric fact");
    }
    std::cout<<"residual observation contract "<<checks<<" checks\n";
}
