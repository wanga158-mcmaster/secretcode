#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace p8 {
// UncachedHash is the fixed, deterministic payload hash/validation function.
// This memo owns geometry only; no route address or live permission is a key.
template<class Route,std::size_t MaxTiles,auto UncachedHash,std::size_t Slots=8>
class ExactRouteHashMemo {
    static_assert(MaxTiles>0);
    static_assert(Slots>0&&Slots<=8);
    using Point=std::remove_cvref_t<decltype(std::declval<const Route&>().tiles[0])>;
    struct Entry {
        std::vector<Point> tiles;
        std::size_t count=0;
        std::uint64_t blocker_assumptions=0,exact_hash=0;
        bool verified=false,ready=false;
    };
    static_assert(std::is_nothrow_move_assignable_v<Entry>);
    std::array<Entry,Slots> entries_{};
    std::size_t next_=0;
public:
    std::uint64_t hash(const Route& route) {
        if constexpr(std::is_signed_v<decltype(route.count)>)
            if(route.count<0)return UncachedHash(route);
        const auto count=static_cast<std::size_t>(route.count);
        if(count>MaxTiles||count>route.tiles.size())return UncachedHash(route);
        for(const auto& entry:entries_) {
            if(!entry.ready||entry.count!=count||entry.verified!=route.verified||
                entry.blocker_assumptions!=route.blocker_assumptions)continue;
            bool equal=true;
            for(std::size_t i=0;i<count;++i)
                if(!(entry.tiles[i]==route.tiles[i])){equal=false;break;}
            if(equal)return entry.exact_hash;
        }
        const std::uint64_t exact=UncachedHash(route);
        if(!exact)return exact;
        // Finish all potentially throwing allocation/copy work before replacing
        // an entry. Failure preserves the old key/hash pair and propagates.
        Entry completed;
        completed.tiles.assign(route.tiles.begin(),route.tiles.begin()+count);
        completed.count=count;completed.verified=route.verified;
        completed.blocker_assumptions=route.blocker_assumptions;
        completed.exact_hash=exact;completed.ready=true;
        entries_[next_]=std::move(completed);
        next_=(next_+1)%Slots;
        return exact;
    }
};
} // namespace p8
