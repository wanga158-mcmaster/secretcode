#pragma once
// Pure wire protocol and the one-camp allocation model. No SDK calls, path searches,
// logging or coefficient-dependent feature gates belong here. Runtime state lives
// outside Knowledge and Brain so the default-off chassis keeps its old layout.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mb::joint {

enum class Tag : std::uint8_t { news = 0, request = 1, commit = 2, abort = 3 };
using Wire = std::array<std::uint8_t, 3>;
struct Packet
{
    Tag tag{ Tag::news };
    int group{ -1 };
    int round{ -1 };       // REQUEST/ABORT: absolute request round
    int owner{ -1 };
    int urgency{};
    int deadline{ -1 };    // COMMIT: absolute useful arrival deadline, not a modulo clock
    std::uint8_t mask{};
    int until{};          // NEWS: original exact uint16 respawn end
};

inline bool encode(const Packet& p, Wire& out)
{
    if (p.group < 0 || p.group >= 63) return false;
    unsigned payload = 0;
    switch (p.tag)
    {
    case Tag::news:
        if (p.until < 0 || p.until > 65535) return false;
        payload = static_cast<unsigned>(p.until);
        break;
    case Tag::request:
    case Tag::abort:
        if (p.round < 0 || p.round > 2047 || p.owner < 0 || p.owner >= 5 || p.urgency < 0 || p.urgency > 3) return false;
        payload = static_cast<unsigned>(p.round | (p.owner << 11) | (p.urgency << 14));
        break;
    case Tag::commit:
        if (p.deadline < 0 || p.deadline > 2047 || p.mask == 0 || p.mask > 31) return false;
        payload = static_cast<unsigned>(p.deadline | (static_cast<unsigned>(p.mask) << 11));
        break;
    default: return false;
    }
    out = { static_cast<std::uint8_t>((static_cast<unsigned>(p.tag) << 6) | (p.group + 1)),
            static_cast<std::uint8_t>(payload & 255u), static_cast<std::uint8_t>(payload >> 8) };
    return true;
}

inline Packet decode(const Wire& b)
{
    Packet p;
    p.tag = static_cast<Tag>(b[0] >> 6);
    p.group = static_cast<int>(b[0] & 63u) - 1;
    const int payload = static_cast<int>(b[1]) | (static_cast<int>(b[2]) << 8);
    if (p.tag == Tag::news) p.until = payload;
    else if (p.tag == Tag::commit)
    {
        p.deadline = payload & 2047;
        p.mask = static_cast<std::uint8_t>(payload >> 11);
    }
    else
    {
        p.round = payload & 2047;
        p.owner = (payload >> 11) & 7;
        p.urgency = payload >> 14;
    }
    return p;
}

inline bool supported(int team, int groups, int max_rounds, int bytes, bool confirmed_only)
{
    return team > 0 && team <= 5 && groups > 0 && groups <= 63 && max_rounds > 0 && max_rounds <= 2047 &&
           bytes == 8 && confirmed_only;
}

// Bids deliberately retain status=idle (0b11). Each new auction atomically clears
// all five slots; each actor invalidates its own bid before acting and certifies
// its completed end position afterward. A missed restoration can make a
// commitment temporarily absent; it cannot announce a cleared or capturing camp.
// Four upper prices are initial seed values in gold-equivalent / champion-round.
// Overflow is unavailable rather than silently underpricing a valuable farmer.
inline constexpr std::array<float, 4> kPriceUpper{ 1.0f, 2.0f, 4.0f, 8.0f };
struct Bid
{
    bool valid{};
    bool eligible{};
    int eta{ 15 };
    int price{};
};
inline float price_upper(int bin) { return bin >= 0 && bin < 4 ? kPriceUpper[static_cast<unsigned>(bin)] : 0.0f; }
inline std::uint8_t encode_bid(int eta, float farm_rate, bool eligible = true)
{
    int bin = 0;
    if (!std::isfinite(farm_rate) || farm_rate < 0.0f || farm_rate > kPriceUpper.back()) eligible = false;
    while (bin < 3 && farm_rate > kPriceUpper[static_cast<unsigned>(bin)]) ++bin;
    if (eta < 0 || eta > 14) eligible = false;
    return static_cast<std::uint8_t>(0xc0u | (static_cast<unsigned>(bin) << 4) | (eligible ? eta : 15));
}
inline Bid decode_bid(std::uint8_t byte)
{
    Bid b;
    b.valid = (byte & 0xc0u) == 0xc0u;
    b.eta = byte & 15u;
    b.price = (byte >> 4) & 3u;
    b.eligible = b.valid && b.eta < 15;
    return b;
}

enum class Phase { normal, bid, restore };
// This clock is private to ONE actor. Every actor independently observes the
// mailbox before parsing ordinary slots. Request r permits a complete bid round
// r+1 even when created in the middle of r; its owner decides at r+2. Bids remain
// published until a result is observed, so an earlier actor cannot erase them
// before a later owner reads. A full restoration round precedes normal parsing.
struct PhaseClock
{
    Packet request{};
    Packet plan{};
    bool has_request{}, has_plan{};
    int issue{ -1 }, restore_until{ -1 }, first_seen{ -1 };
    bool result_seen{};

    bool same_request(const Packet& p) const
    {
        return has_request && p.group == request.group && p.round == request.round && p.owner == request.owner;
    }
    bool note(const Packet& p, int now)
    {
        if (p.group < 0 || p.group >= 63 || now < 0) return false;
        if (p.tag == Tag::request)
        {
            if (p.owner < 0 || p.owner >= 5 || p.round < 0 || p.round > 2047 || now < p.round || now > p.round + 3) return false;
            if (same_request(p)) return true;
            if (phase(now) != Phase::normal) return false; // first lease wins
            request = p; has_request = true; has_plan = false; result_seen = false;
            issue = p.round + 2; restore_until = p.round + 3; first_seen = now;
            return true;
        }
        if (p.tag == Tag::commit)
        {
            if (p.mask == 0 || p.mask > 31 || p.deadline < 0 || p.deadline > 2047) return false;
            if (has_plan && plan.group == p.group && plan.deadline == p.deadline && plan.mask == p.mask) return true;
            // Participants have the original request because a cleared-to-zero
            // slot cannot become a valid bid without seeing it. Missing-context
            // observers protect one full following round after first sight.
            const bool context = has_request && request.group == p.group && now >= request.round + 2 && now <= request.round + 3;
            plan = p; has_plan = true; result_seen = true; first_seen = now;
            issue = context ? request.round + 2 : now;
            restore_until = issue + 1;
            return true;
        }
        if (p.tag == Tag::abort)
        {
            if (p.owner < 0 || p.owner >= 5 || p.round < 0 || p.round > 2047 || now < p.round + 2 || now > p.round + 3) return false;
            request = p; has_request = true; has_plan = false; result_seen = true;
            issue = p.round + 2; restore_until = issue + 1;
            return true;
        }
        return false;
    }
    Phase phase(int now) const
    {
        if (result_seen) return now <= restore_until ? Phase::restore : Phase::normal;
        if (has_request && now >= request.round && now <= request.round + 2) return Phase::bid;
        if (has_request && now == request.round + 3) return Phase::restore;
        return Phase::normal;
    }
    bool owner_decides(int now, int self) const
    {
        return has_request && !result_seen && self == request.owner && now == request.round + 2;
    }
};

// One fixed cell per encodable group, not a single overwritable pending record.
// Coalesce only observations of the SAME group: the later exact end dominates.
// FIFO across groups prevents a rich recurring observation from burying a clear.
struct PendingNews
{
    std::array<int, 63> until{};
    std::array<unsigned, 63> order{};
    unsigned serial{};
    bool push(int group, int end)
    {
        if (group < 0 || group >= 63 || end < 0 || end > 65535) return false;
        const auto g = static_cast<unsigned>(group);
        if (order[g] == 0) order[g] = ++serial;
        until[g] = std::max(until[g], end);
        return true;
    }
    bool pop(int now, Packet& p)
    {
        int best = -1;
        for (int g = 0; g < 63; ++g)
        {
            if (order[static_cast<unsigned>(g)] == 0) continue;
            if (until[static_cast<unsigned>(g)] <= now)
            {
                order[static_cast<unsigned>(g)] = 0; until[static_cast<unsigned>(g)] = 0; continue;
            }
            if (best < 0 || order[static_cast<unsigned>(g)] < order[static_cast<unsigned>(best)]) best = g;
        }
        if (best < 0) return false;
        p = {}; p.tag = Tag::news; p.group = best; p.until = until[static_cast<unsigned>(best)];
        order[static_cast<unsigned>(best)] = 0; until[static_cast<unsigned>(best)] = 0;
        return true;
    }
};

struct Input
{
    int now{}, deadline{}, requester{}, roster_size{ 5 }, horizon{ 32 }, capture_rounds{ 5 }, recall_cycle{ 6 }, completion_rounds{ 32 };
    std::uint8_t alive_mask{ 31 };
    std::array<Bid, 5> bids{};
    std::array<float, 5> dps{}, hp{};
    float reward{}, camp_hp{}, camp_dps{}, requester_hp{}, threat_dps{}, death_gold{ 100.0f }, safety_margin{ 1.2f };
};
struct Choice
{
    std::uint8_t mask{}; // zero is the complete all-FARM assignment
    int deadline{}, arrival{}, clear_rounds{}, masks_evaluated{};
    float value{}, task_reward{}, displaced_farm{}, travel_cost{}, spread_cost{}, recovery_cost{};
};
inline int members(unsigned mask)
{
    int n = 0;
    for (; mask; mask >>= 1) n += static_cast<int>(mask & 1u);
    return n;
}
inline bool finite_nonnegative(float x) { return std::isfinite(x) && x >= 0.0f; }
inline int bounded_rounds(float amount, float rate, int cap)
{
    if (!finite_nonnegative(amount) || !std::isfinite(rate) || rate <= 0.0f || cap < 1) return cap + 1;
    const double t = std::ceil(static_cast<double>(amount) / static_cast<double>(rate));
    return t > cap ? cap + 1 : std::max(1, static_cast<int>(t));
}

// One indexed camp, one complete allocation. Every subset is compared with the
// SAME all-FARM alternative. Nonmembers keep FARM; reward is credited exactly
// once. Prices reserve each recruited hero through travel, assembly and clearing;
// only the deterministic lowest-index capturer pays the capture channel. The first seed values
// recovery cost in the same gold-equivalent units as the bid prices. All-FARM can
// retreat or choose elsewhere, so it is never credited as an avoided solo death.
// It is an admission model,
// never permission to override the existing combat/safety verdict.
inline Choice choose(const Input& in)
{
    Choice best;
    if (in.roster_size < 1 || in.roster_size > 5 || in.requester < 0 || in.requester >= in.roster_size ||
        in.now < 0 || in.now > 2047 || in.deadline < in.now || in.deadline > 2047 || in.horizon < 1 || in.horizon > 256 ||
        in.capture_rounds < 0 || in.capture_rounds > 256 || in.recall_cycle < 0 || in.recall_cycle > 256 ||
        in.completion_rounds < 1 || in.completion_rounds > 256 ||
        !finite_nonnegative(in.reward) || !finite_nonnegative(in.camp_hp) || !finite_nonnegative(in.camp_dps) ||
        !finite_nonnegative(in.threat_dps) || !finite_nonnegative(in.death_gold) || !std::isfinite(in.requester_hp) || in.requester_hp <= 0.0f ||
        !std::isfinite(in.safety_margin) || in.safety_margin < 1.0f) return best;
    const unsigned limit = 1u << in.roster_size;
    const unsigned requester_bit = 1u << in.requester;
    const float requester_price = price_upper(in.bids[static_cast<unsigned>(in.requester)].price);
    const int available = std::min(in.horizon, in.deadline - in.now);
    for (unsigned mask = 1; mask < limit; ++mask)
    {
        ++best.masks_evaluated; // fixed finite work, at most31 masks, only once per auction
        if (!(mask & requester_bit) || members(mask) < 2 || (mask & ~in.alive_mask)) continue;
        bool eligible = true;
        float dps = 0.0f, hp = 0.0f, capturer_price = 0.0f, price_sum = 0.0f, travel_cost = 0.0f;
        int arrival = 0;
        for (int i = 0; i < in.roster_size; ++i)
        {
            if (!(mask & (1u << i))) continue;
            const auto k = static_cast<unsigned>(i);
            const auto& b = in.bids[k];
            if (!b.valid || !b.eligible || b.eta < 0 || b.eta > 14 || b.price < 0 || b.price > 3 ||
                !std::isfinite(in.dps[k]) || in.dps[k] <= 0.0f || !std::isfinite(in.hp[k]) || in.hp[k] <= 0.0f) { eligible = false; break; }
            const float price = price_upper(b.price);
            arrival = std::max(arrival, b.eta);
            dps += in.dps[k]; hp += in.hp[k]; price_sum += price;
            if (capturer_price==0.0f) capturer_price=price; // same lowest-index capturer as executor
            travel_cost += price * static_cast<float>(b.eta);
        }
        if (!eligible || arrival > available || !std::isfinite(dps) || !std::isfinite(hp)) continue;
        // In particular, a12-round lease never licenses a rescue arriving after
        // the requester's current damage budget. Its actual useful deadline wins.
        if (in.threat_dps * static_cast<float>(arrival) * in.safety_margin >= in.requester_hp) continue;
        const int clear = bounded_rounds(in.camp_hp, dps, in.horizon);
        if (arrival + clear + in.capture_rounds > in.horizon || clear+in.capture_rounds>in.completion_rounds) continue;
        const float damage = in.camp_dps * static_cast<float>(clear);
        if (!std::isfinite(damage) || damage * in.safety_margin > hp) continue;
        const float spread_cost = price_sum * static_cast<float>(arrival) - travel_cost;
        const float displaced = travel_cost + spread_cost + price_sum * static_cast<float>(clear) +
                                capturer_price * static_cast<float>(in.capture_rounds);
        const float owner_party_damage = damage / static_cast<float>(members(mask));
        const float owner_damage=owner_party_damage+in.threat_dps*static_cast<float>(arrival);
        if (owner_damage*in.safety_margin>=in.requester_hp) continue;
        const float damage_fraction=std::min(1.0f,owner_damage/in.requester_hp);
        const float recovery=requester_price*static_cast<float>(in.recall_cycle)*damage_fraction;
        const float value = in.reward - recovery - displaced;
        if (!std::isfinite(value) || value <= best.value) continue;
        const int evaluated = best.masks_evaluated;
        best.mask = static_cast<std::uint8_t>(mask); best.deadline = in.now + arrival;
        best.arrival = arrival; best.clear_rounds = clear; best.value = value;
        best.task_reward = in.reward; best.displaced_farm = displaced; best.travel_cost = travel_cost;
        best.spread_cost = spread_cost; best.recovery_cost = recovery;
        best.masks_evaluated = evaluated;
    }
    return best;
}

// Executor contracts are pure so their edge cases can be exercised without a
// game. Safe staging alone is NOT an accepted action. All living assigned heroes
// must have arrived before a sleeping group can be woken. Existing captures and
// channels always retain priority, and damage precludes stationary assembly.
inline bool assembled(std::uint8_t mask, std::uint8_t alive, std::uint8_t in_attack_range)
{
    return mask != 0 && (mask & alive) == mask && (mask & in_attack_range) == mask;
}
inline bool may_execute(bool channel, bool capturing, bool winning_bank, bool immediate_capture, bool safe)
{
    return !channel && !capturing && !winning_bank && !immediate_capture && safe;
}
inline bool accept_effect(bool moved, bool attacked, bool cast) { return moved || attacked || cast; }
inline bool may_wake(bool already_awake, bool party_assembled, bool damaging_stage)
{
    return already_awake || (party_assembled && !damaging_stage);
}

} // namespace mb::joint
