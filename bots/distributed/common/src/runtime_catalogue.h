#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace p6::runtime_catalogue {
// Public immutable inputs only. The key combines source work and distance from
// this actor's own initial spawn; it must contain no zero-gem penalty.
struct Site {
    double key = 0;
    int id = -1;
    std::int64_t gold = 0, gems = 0;
    double total_health = -1, total_strength = -1;
    int members = 0;
};
struct Actor {
    double health = 0, max_health = 0, strength = 0;
    bool ranged = false;
    // Only a current complete whole-group basic mask from the actor's exact
    // stationary position may supply this ID. It is not a future path promise.
    int stationary_basic_group = -1;
};
struct Order {
    std::array<std::size_t, 8> indices{};
    std::size_t count = 0;
};
template<class Position> struct VerificationPads {
    std::array<Position,3> pads{};
    bool available = false;
};
// Public staging geometry only. Actual current SDK masks remain necessary.
// Intersect each role's whole-member range square, then choose distinct legal
// pads. Ranged roles prefer separation from the source center where possible.
template<class Position,class Legal>
VerificationPads<Position> verification_pads(std::span<const Position> members,
    const std::array<int,3>& ranges,Position origin,Legal legal) {
    VerificationPads<Position> result;
    if(members.empty() || members.size()>32)return result;
    int min_x=members[0].x,max_x=min_x,min_y=members[0].y,max_y=min_y;
    for(const auto p:members) {
        min_x=std::min(min_x,p.x);max_x=std::max(max_x,p.x);
        min_y=std::min(min_y,p.y);max_y=std::max(max_y,p.y);
    }
    const Position center{min_x+(max_x-min_x)/2,min_y+(max_y-min_y)/2};
    auto equal=[](Position a,Position b){return a.x==b.x && a.y==b.y;};
    auto distance=[](Position a,Position b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));};
    auto from_origin=[&](Position p){return std::abs(p.x-origin.x)+std::abs(p.y-origin.y);};
    std::array<int,3> order{0,1,2};
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return ranges[a]<ranges[b];});
    for(int at=0;at<3;++at) {
        const int role=order[at],range=ranges[role];
        if(range<1 || range>8)return {};
        bool found=false;Position best{};
        for(int y=max_y-range;y<=min_y+range;++y)for(int x=max_x-range;x<=min_x+range;++x) {
            const Position p{x,y};if(!legal(p))continue;
            bool occupied=false;
            for(int prior=0;prior<at;++prior)occupied |= equal(p,result.pads[order[prior]]);
            if(occupied)continue;
            const bool farther=range>1 && distance(p,center)!=distance(best,center);
            if(!found || (farther?distance(p,center)>distance(best,center):
                from_origin(p)!=from_origin(best)?from_origin(p)<from_origin(best):
                p.y!=best.y?p.y<best.y:p.x<best.x)) {best=p;found=true;}
        }
        if(!found)return {};
        result.pads[role]=best;
    }
    result.available=true;return result;
}
inline Order select(std::span<const Site> sites, unsigned ordinal = 0) {
    Order gems, gold, out;
    const auto before = [&](std::size_t a, std::size_t b) {
        if (sites[a].key != sites[b].key) return sites[a].key < sites[b].key;
        if (sites[a].id != sites[b].id) return sites[a].id < sites[b].id;
        return a < b;
    };
    // Keep only the bounded best candidates. Insertion, ties and output scans
    // visit at most eight local entries; no SDK calls or allocations occur.
    const auto insert = [&](Order& order, std::size_t index) {
        for (std::size_t i = 0; i < order.count; ++i)
            if (sites[order.indices[i]].id == sites[index].id) return;
        std::size_t at = 0;
        while (at < order.count && !before(index, order.indices[at])) ++at;
        if (at == order.indices.size()) return;
        if (order.count < order.indices.size()) ++order.count;
        for (std::size_t i = order.count - 1; i > at; --i)
            order.indices[i] = order.indices[i - 1];
        order.indices[at] = index;
    };
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const auto& site = sites[i];
        if (site.id < 0 || !std::isfinite(site.key) || site.key < 0 ||
            site.gold < 0 || site.gems < 0) continue;
        if (site.gems > 0) insert(gems, i);
        else if (site.gold > 0) insert(gold, i);
    }
    const auto append = [&](std::size_t index) {
        if (out.count == out.indices.size()) return;
        for (std::size_t i = 0; i < out.count; ++i)
            if (sites[out.indices[i]].id == sites[index].id) return;
        out.indices[out.count++] = index;
    };
    if (gold.count) {
        // Spread only among equally local minimum-key sources. Rotating over
        // every gold source could send later picks to distant enemy-side ones.
        std::size_t ties = 1;
        while (ties < gold.count && sites[gold.indices[ties]].key == sites[gold.indices[0]].key) ++ties;
        append(gold.indices[ordinal % ties]);
    }
    const std::size_t gem_limit = gold.count ? 7 : 8;
    for (std::size_t i = 0; i < gems.count && i < gem_limit; ++i) append(gems.indices[i]);
    // Small maps retain additional useful gold sources instead of losing slots.
    for (std::size_t i = 0; i < gold.count; ++i) append(gold.indices[i]);
    return out;
}
// This public basic-work screen chooses what to inspect first, not what is safe
// to execute. Travel, ambient enemies, live membership and completion still
// require the ordinary fresh evaluated quote and reference caller.
inline bool basic_work_viable(const Site& site, const Actor& actor) {
    if (!std::isfinite(site.total_health) || site.total_health < 0 ||
        !std::isfinite(site.total_strength) || site.total_strength < 0 || site.members < 1 ||
        !std::isfinite(actor.health) || actor.health <= 0 ||
        !std::isfinite(actor.max_health) || actor.max_health <= 0 ||
        !std::isfinite(actor.strength) || actor.strength <= 0) return false;
    const bool stationary = actor.stationary_basic_group >= 0 &&
                            actor.stationary_basic_group == site.id;
    const double rate = actor.strength * (actor.ranged && !stationary ? .5 : 1.);
    // sum(ceil(memberHP/rate)) <= ceil(totalHP/rate) + members-1.
    // Add the ordinary model's two switching ticks per subsequent member, and
    // keep every member's incoming Strength for the entire basic work bound.
    const double turns = std::ceil(site.total_health / rate) + 3. * (site.members - 1);
    const double incoming = turns * site.total_strength;
    const double remaining = actor.health - incoming;
    return std::isfinite(turns) && std::isfinite(incoming) && std::isfinite(remaining) &&
           remaining > 0 && remaining >= std::max(.20 * actor.max_health, 2 * site.total_strength);
}
inline std::size_t preferred(std::span<const Site> sites, const Order& order, const Actor& actor) {
    std::size_t first = sites.size(), bootstrap = sites.size();
    for (std::size_t i = 0; i < std::min(order.count, order.indices.size()); ++i) {
        const auto index = order.indices[i];
        if (index >= sites.size()) continue;
        if (first == sites.size()) first = index;
        const auto& site = sites[index];
        if (site.gems > 0 && basic_work_viable(site, actor)) return index;
        if (bootstrap == sites.size() && site.gems == 0 && site.gold > 0) bootstrap = index;
    }
    return bootstrap != sites.size() ? bootstrap : first;
}
// Convenience for an already-selected catalogue stored in site order.
inline std::size_t preferred(std::span<const Site> selected, const Actor& actor) {
    Order identity;
    identity.count = std::min(selected.size(), identity.indices.size());
    for (std::size_t i = 0; i < identity.count; ++i) identity.indices[i] = i;
    return preferred(selected, identity, actor);
}
} // namespace p6::runtime_catalogue
