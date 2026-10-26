#pragma once
// Complete, bounded production forecasts. The caller supplies represented inputs
// and an execution model with runtime preemption; admission is not a native safety
// proof. This module neither invents unknown state nor repeats an unrenewed camp.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

namespace wb {

struct FrontierMember {
    int x{}, y{};
    bool visible{false};
    std::uint8_t attempted{};
};

struct Frontier {
    bool valid{false};
    int memberIndex{-1};
    int landingx{}, landingy{};
    int direction{-1};
    int distance{65535};
};

// Direction/attempt bits use these explicit row-major offsets, not an SDK enum:
// 0(-1,-1), 1(0,-1), 2(1,-1), 3(-1,0), 4(1,0), 5(-1,1), 6(0,1), 7(1,1).
// This bounded observation menu covers at most thirty static members. An empty
// result means no eligible unattempted adjacent landing in these inputs, never
// that a complete job is impossible or that any candidate is safe/reward-bearing.
// The lookup is called once per remaining candidate landing, after member,
// attempt, bounds, self and occupancy checks, at most 30*8 calls. Unreachable is
// represented by uint16_t(65535). No whole-map distance export is required.
template<class DistanceLookup>
inline Frontier select_frontier_with_distance(int width, int height, int self_x, int self_y,
                                              std::span<const FrontierMember> members,
                                              std::span<const std::uint8_t> packed_blocked,
                                              DistanceLookup&& distance_lookup) {
    Frontier best;
    if (width <= 0 || height <= 0 || self_x < 0 || self_y < 0
        || self_x >= width || self_y >= height || members.size() > 30) return best;
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) return best;
    const auto cells = w * h;
    const auto bytes = cells / 8 + (cells % 8 != 0);
    if (packed_blocked.size() < bytes) return best;
    constexpr int dx[]{-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dy[]{-1,-1,-1,  0, 0,  1, 1, 1};
    std::size_t best_index = 0;
    for (std::size_t member_index = 0; member_index < members.size(); ++member_index) {
        const auto& member = members[member_index];
        if (member.visible || member.x < 0 || member.y < 0
            || member.x >= width || member.y >= height) continue;
        for (int direction = 0; direction < 8; ++direction) {
            if ((member.attempted & (1u << direction)) != 0) continue;
            const int x = member.x + dx[direction], y = member.y + dy[direction];
            if (x < 0 || y < 0 || x >= width || y >= height
                || (x == self_x && y == self_y)) continue;
            const auto index = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
            if ((packed_blocked[index >> 3] & (1u << (index & 7))) != 0) continue;
            const std::uint16_t distance = distance_lookup(x,y);
            if (distance == 65535) continue;
            if (!best.valid || distance < best.distance
                || (distance == best.distance && (static_cast<int>(member_index) < best.memberIndex
                    || (static_cast<int>(member_index) == best.memberIndex && index < best_index)))) {
                best = {true, static_cast<int>(member_index), x, y, direction, distance};
                best_index = index;
            }
        }
    }
    return best;
}

inline Frontier select_frontier(int width, int height, int self_x, int self_y,
                                std::span<const FrontierMember> members,
                                std::span<const std::uint8_t> packed_blocked,
                                std::span<const std::uint16_t> routes) {
    if (width <= 0 || height <= 0) return {};
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h || routes.size() < w * h) return {};
    return select_frontier_with_distance(width,height,self_x,self_y,members,packed_blocked,
        [&](int x,int y) {return routes[static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x)];});
}

struct Episode {
    int job{-1}, actor{-1}, helper{-1};
    // Historical API name: caller-admitted for its bounded execution model.
    // It does not turn approximate paths, work or recovery into proved outcomes.
    bool knownsafe{false};
    bool estimate{true};
    double gems{}, gold{};
    int approach{}, work{}, capture{}, bank{}, channel{}, recovery{};
    float endhp{};
    // For pairs, complete helper time through release, its exit and recovery. If
    // absent, charge it the full owner's cycle rather than assuming a free helper.
    int helper_duration{-1};
    float helper_endhp{};

    int totalduration() const {
        const int parts[]{approach, work, capture, bank, channel, recovery};
        long long total = 0;
        for (int part : parts) {
            if (part < 0) return -1;
            total += part;
        }
        return total > std::numeric_limits<int>::max() ? -1 : static_cast<int>(total);
    }
    int banktime() const {
        const int total = totalduration();
        return total < 0 ? -1 : total - recovery;
    }
    bool valid() const {
        return knownsafe && job >= 0 && actor >= 0 && helper >= -1 && helper != actor
            && totalduration() > 0 && banktime() >= 0
            && std::isfinite(gems) && gems >= 0 && std::isfinite(gold) && gold >= 0
            && std::isfinite(endhp) && endhp >= 0
            && helper_duration >= -1 && std::isfinite(helper_endhp) && helper_endhp >= 0;
    }
    bool bankedby(int horizon) const {
        return valid() && horizon > 0 && banktime() <= horizon;
    }
};

struct PlannerContext {
    bool needsfunding{false};
    // Remaining gold for one identified purchase, not a global gold/gem exchange rate.
    double fundinggold{};
};

struct Choice {
    bool valid{false};
    Episode episode{};
    std::size_t index{};
    double value{}, rate{};
    int duration{};
    bool funding{false}, funding_complete{false};
    bool estimate{true};
};

inline bool funding_stage(PlannerContext context) {
    return context.needsfunding && std::isfinite(context.fundinggold) && context.fundinggold > 0;
}

inline bool better_choice(const Choice& a, const Choice& b) {
    if (!b.valid) return a.valid;
    if (!a.valid) return false;
    if (a.funding_complete != b.funding_complete) return a.funding_complete;
    if (a.rate != b.rate) return a.rate > b.rate;
    if (a.value != b.value) return a.value > b.value;
    if (a.duration != b.duration) return a.duration < b.duration;
    if (a.episode.job != b.episode.job) return a.episode.job < b.episode.job;
    return a.index < b.index;
}

// One represented episode only. A long horizon never implies another respawn or
// available second cycle. `work/recovery` may already include certified partial
// siege sorties, but those transitions must actually be represented by the caller.
inline Choice choose(std::span<const Episode> solo, int actor, int horizon,
                     PlannerContext context = {}) {
    Choice best, gem_fallback;
    const bool funding = funding_stage(context);
    for (std::size_t i = 0; i < solo.size(); ++i) {
        const Episode& episode = solo[i];
        if (episode.actor != actor || episode.helper != -1 || !episode.bankedby(horizon)) continue;
        Choice candidate;
        candidate.valid = true;
        candidate.episode = episode;
        candidate.estimate = episode.estimate;
        candidate.index = i;
        candidate.duration = episode.totalduration();
        candidate.value = episode.gems;
        candidate.rate = candidate.value / candidate.duration;
        if (candidate.value > 0 && better_choice(candidate, gem_fallback)) gem_fallback = candidate;
        if (!funding) {
            if (candidate.value > 0 && better_choice(candidate, best)) best = candidate;
            continue;
        }
        if (episode.gold <= 0) continue;
        candidate.funding = true;
        candidate.funding_complete = episode.gold >= context.fundinggold;
        candidate.value = std::min(episode.gold, context.fundinggold);
        candidate.rate = candidate.value / candidate.duration;
        if (better_choice(candidate, best)) best = candidate;
    }
    // A safe incomplete funding job is progress toward the named purchase; it is
    // never labelled gem output or a completed purchase. If no funding job is
    // available, an actual gem episode still plays rather than waiting forever.
    return best.valid ? best : gem_fallback;
}

struct Comparison {
    bool admit{false};
    double pairbanked{}, independentbanked{};
    int pairlabor{}, independentlabor{};
    bool unknown{true};
    double pairgold{}, independentgold{};
    bool conservative_helper_hold{false};
    bool estimate{true};
    double ownerbanked{}, helperbanked{}, ownergold{}, helpergold{};
};

struct RewardSplit {
    unsigned owner{}, helper{};
};

// Integer base reward only: the caller separately models each recipient's own
// gold modifiers. Native identities must be distinct; the lower ID gets remainder.
inline RewardSplit split_reward(unsigned total, int owner_id, int helper_id) {
    const unsigned half = total / 2;
    return owner_id < helper_id ? RewardSplit{half + total % 2, half}
                                : RewardSplit{half, half + total % 2};
}

inline int occupied_within(int duration, int horizon) {
    return duration < 0 || horizon < 0 ? 0 : std::min(duration, horizon);
}

inline int labor_sum(int first, int second) {
    const long long total = static_cast<long long>(first) + second;
    return total > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(total);
}

// Compare exactly these named model alternatives at one common horizon. Admission
// ranks their forecasts; it proves neither safety nor actual economic dominance.
// Unknown alternatives reject, while known-input estimates may be compared. This
// does not claim global best independent schedules. No second cycle, future helper
// job or repeated reward is credited without another represented episode.
inline Comparison compare(const Episode& pair, const Episode& owner_alternative,
                          const Episode& helper_alternative, int horizon,
                          PlannerContext context = {}, PlannerContext helper_context = {}) {
    Comparison out;
    if (horizon <= 0 || !pair.valid() || pair.helper < 0
        || !owner_alternative.valid() || !helper_alternative.valid()
        || owner_alternative.helper != -1 || helper_alternative.helper != -1
        || owner_alternative.actor != pair.actor || helper_alternative.actor != pair.helper
        || owner_alternative.job == helper_alternative.job) return out;
    out.unknown = false;
    out.estimate = pair.estimate || owner_alternative.estimate || helper_alternative.estimate;
    out.pairbanked = pair.bankedby(horizon) ? pair.gems : 0;
    out.pairgold = pair.bankedby(horizon) ? pair.gold : 0;
    out.ownerbanked = out.pairbanked;
    out.ownergold = out.pairgold;
    for (const Episode* episode : {&owner_alternative, &helper_alternative}) {
        if (episode->bankedby(horizon)) {
            out.independentbanked += episode->gems;
            out.independentgold += episode->gold;
        }
        out.independentlabor = labor_sum(out.independentlabor,
                                        occupied_within(episode->totalduration(), horizon));
    }
    const int helper_time = pair.helper_duration >= 0 ? pair.helper_duration : pair.totalduration();
    out.conservative_helper_hold = pair.helper_duration < 0;
    out.pairlabor = labor_sum(occupied_within(pair.totalduration(), horizon), occupied_within(helper_time, horizon));
    // During a named funding stage, recruiting must not destroy represented
    // progress toward the purchase merely because gold has no gem exchange rate.
    // This context belongs to the named owner; helper gold cannot pay its bill.
    const double owner_independent_gold = owner_alternative.bankedby(horizon) ? owner_alternative.gold : 0;
    if (funding_stage(context)
        && std::min(out.pairgold, context.fundinggold)
           < std::min(owner_independent_gold, context.fundinggold)) return out;
    // A pair Episode represents sole-owner capture. Protect a helper's separately
    // identified purchase too; the owner's reward cannot fund the helper.
    const double helper_independent_gold = helper_alternative.bankedby(horizon) ? helper_alternative.gold : 0;
    if (funding_stage(helper_context)
        && std::min(helper_independent_gold, helper_context.fundinggold) > 0) return out;
    out.admit = out.pairbanked > out.independentbanked
        || (out.pairbanked == out.independentbanked && out.pairbanked > 0
            && out.pairlabor < out.independentlabor);
    return out;
}

// A shared-wave plan has TWO represented receipts and delivery/recovery paths.
// owner_receipt names the helper; helper_receipt is that actor's own plain Episode
// for the same camp, with its own share and costs. A later helper bank is never
// pulled forward to the owner's bank time. Unknown return costs reject admission.
inline Comparison compare_shared(const Episode& owner_receipt, const Episode& helper_receipt,
                                 const Episode& owner_alternative, const Episode& helper_alternative,
                                 int horizon, PlannerContext owner_context = {},
                                 PlannerContext helper_context = {}) {
    Comparison out;
    if (horizon <= 0 || !owner_receipt.valid() || owner_receipt.helper < 0
        || !helper_receipt.valid() || helper_receipt.helper != -1
        || helper_receipt.actor != owner_receipt.helper || helper_receipt.job != owner_receipt.job
        || !owner_alternative.valid() || !helper_alternative.valid()
        || owner_alternative.helper != -1 || helper_alternative.helper != -1
        || owner_alternative.actor != owner_receipt.actor
        || helper_alternative.actor != helper_receipt.actor
        || owner_alternative.job == helper_alternative.job) return out;
    out.unknown = false;
    out.estimate = owner_receipt.estimate || helper_receipt.estimate
        || owner_alternative.estimate || helper_alternative.estimate;
    out.ownerbanked = owner_receipt.bankedby(horizon) ? owner_receipt.gems : 0;
    out.helperbanked = helper_receipt.bankedby(horizon) ? helper_receipt.gems : 0;
    out.ownergold = owner_receipt.bankedby(horizon) ? owner_receipt.gold : 0;
    out.helpergold = helper_receipt.bankedby(horizon) ? helper_receipt.gold : 0;
    out.pairbanked = out.ownerbanked + out.helperbanked;
    out.pairgold = out.ownergold + out.helpergold;
    const double owner_independent_gold = owner_alternative.bankedby(horizon) ? owner_alternative.gold : 0;
    const double helper_independent_gold = helper_alternative.bankedby(horizon) ? helper_alternative.gold : 0;
    for (const Episode* alternative : {&owner_alternative, &helper_alternative}) {
        if (alternative->bankedby(horizon)) {
            out.independentbanked += alternative->gems;
            out.independentgold += alternative->gold;
        }
        out.independentlabor = labor_sum(out.independentlabor,
                                        occupied_within(alternative->totalduration(), horizon));
    }
    out.pairlabor = labor_sum(occupied_within(owner_receipt.totalduration(), horizon),
                             occupied_within(helper_receipt.totalduration(), horizon));
    if (funding_stage(owner_context)
        && std::min(out.ownergold, owner_context.fundinggold)
           < std::min(owner_independent_gold, owner_context.fundinggold)) return out;
    if (funding_stage(helper_context)
        && std::min(out.helpergold, helper_context.fundinggold)
           < std::min(helper_independent_gold, helper_context.fundinggold)) return out;
    out.admit = out.pairbanked > out.independentbanked
        || (out.pairbanked == out.independentbanked && out.pairbanked > 0
            && out.pairlabor < out.independentlabor);
    return out;
}

} // namespace wb
