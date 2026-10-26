#pragma once
// Runtime preparation of immutable public map facts. The pure source builder remains unchanged.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "student_context.h"

namespace zero::student {

class CampContextCache
{
public:
    CampContextCache() = default;
    CampContextCache(const CampContextCache&) = delete;
    CampContextCache& operator=(const CampContextCache&) = delete;
    CampContextCache(CampContextCache&&) = delete;
    CampContextCache& operator=(CampContextCache&&) = delete;
    // Prepare once after Map::finish(), outside an ordinary decision. Strong exception guarantee.
    // The Map object, its groups and members must remain immutable at the same address until prepare
    // is called again. Death/respawn and observation refresh do not invalidate this public-map cache.
    void prepare(const Map& map);
    bool prepared_for(const Map& map) const noexcept;

private:
    struct GroupFacts { Tile center{}; std::size_t members{}; };
    struct MemberView
    {
        Tile at{};
        int group{ -1 };
        std::uint8_t type{};
        std::size_t last{};
        std::uint64_t generation{};
    };
    const Map* map_{};
    std::vector<CampContext> static_rows_;
    std::vector<GroupFacts> groups_;
    std::vector<MemberView> members_;
    std::uint64_t generation_{};
    friend void camp_context_cached(const Context&, CampContextCache&, std::vector<CampContext>&);
};

// Exactly the frozen camp_context facts and operation order, with expected O(groups+observed camps)
// decision work. Cache must be prepared for ctx.map; stale/unprepared input throws, with no lazy
// rebuild or fallback. Calls sharing one mutable cache must not overlap. Generation wrap clears its
// finite member table once; this does not change any facts. Output stays uncapped and caller-owned.
void camp_context_cached(const Context& ctx, CampContextCache& cache, std::vector<CampContext>& out);

}  // namespace zero::student
