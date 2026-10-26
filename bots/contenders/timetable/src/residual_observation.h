#pragma once
#include "residual_types.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace cambc { class camp; class champion; namespace map { class camp_group; } }

namespace residual::observation {
// Limits describe this adapter's supported allocation/work envelope, not the
// game's physical limits. Exceeding one returns Unsupported, never "unreachable".
struct Limits {
    std::size_t cells{16384},groups{128},camps{512},members_per_group{32},champions_per_team{16};
};
enum class Failure : unsigned {None,Capacity,Shape,Layout,Duplicate,Stats};
namespace contract {
struct GroupHeader {std::uint32_t handle{},gold{},gems{},respawn{},members{};};
// The pinned SDK layout is explicit IDs; zero is a valid identity.
inline bool group_header(std::span<const std::uint32_t> words,std::size_t cursor,GroupHeader& out) {
    if(cursor>words.size()||words.size()-cursor<5)return false;
    out={words[cursor],words[cursor+1],words[cursor+2],words[cursor+3],words[cursor+4]};
    return out.respawn<=0x7fffffffu&&out.members>0&&
        out.members<=(words.size()-cursor-5)/4;
}
inline Life life(std::uint32_t state) {
    return state==1?Life::Living:state==2?Life::Reward:state==3?Life::Respawning:Life::Unknown;
}
inline GroupReading group_reading(int index,std::uint32_t state,int timer,unsigned required) {
    GroupReading out;out.index=index;out.life=life(state);
    out.respawn=out.life==Life::Respawning?timer:-1;out.required=required;
    // No capture-progress getter exists in the pinned SDK. Neither required
    // duration nor a zero respawn timer is an observed capture/respawn transition.
    return out;
}
inline std::size_t packed_bytes(std::size_t cells) {return cells/8+(cells%8!=0);}
inline bool packed_test(std::span<const std::uint8_t> bytes,std::size_t cells,std::size_t cell) {
    return cell<cells&&cell/8<bytes.size()&&(bytes[cell/8]&(std::uint8_t{1}<<(cell%8)))!=0;
}
}

class WorldCache {
public:
    explicit WorldCache(Limits limits={}); // Allocation only; no SDK imports.
    ~WorldCache();
    WorldCache(WorldCache&&) noexcept;
    WorldCache& operator=(WorldCache&&) noexcept;
    WorldCache(const WorldCache&)=delete;
    WorldCache& operator=(const WorldCache&)=delete;
    // Retains immutable work across inputs. Every leaf gets its own fuel fence.
    ReadStatus advance(std::size_t max_units=256);
    ReadStatus status() const noexcept;
    Failure failure() const noexcept;
    unsigned stage() const noexcept;
    const World* result() const noexcept;
    // Fresh LOCAL transaction: own body/effects and visible player contact,
    // plus only selected_group's personally observed Life/members. All unrelated
    // groups/camps are absent/Unknown; their static potential is not discounted.
    // selected_group is an own World vector index; -1 requests own/contact only.
    // No partial Frame is Ready; no previous input is reused.
    // A NeedCompute result grants no action authority. The next call starts fresh.
    ReadStatus observe(Frame&,bool walked,int selected_group=-1);
    // Own catalogue indices, NOT dense native IDs. Returned immutable SDK views
    // retain the exact IDs from map_camp_layout. No lookup grants visibility.
    const cambc::camp* camp(int index) const noexcept;
    const cambc::map::camp_group* group(int index) const noexcept;
    // Reconstructs only an explicitly observed foe ID from the last Ready frame,
    // at that same epoch. Construction/actuation still performs SDK validation.
    std::optional<cambc::champion> foe(std::uint32_t handle) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
