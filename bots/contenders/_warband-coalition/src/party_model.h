#pragma once
// Pure two-body camp contract helpers. A frame is an invitation/lease identity, not shared
// vision, arrival, action permission or guaranteed damage. The actual caller must supply fresh
// facts at acceptance and again at commitment; an ACK alone cannot authorize waking a camp.
#include <array>
#include <cmath>
#include <cstdint>

namespace mb::party {

using Bytes = std::array<std::uint8_t, 2>;
inline constexpr int max_actors = 6;
inline constexpr int max_round = 2047;

struct Pair {
    int leader = -1;
    int helper = -1;
    constexpr bool valid() const { return leader >= 0 && leader < helper && helper < max_actors; }
    constexpr bool contains(int actor) const { return valid() && (actor == leader || actor == helper); }
    friend constexpr bool operator==(const Pair&, const Pair&) = default;
};

inline constexpr Pair pair_of(int a, int b) {
    const Pair pair = a < b ? Pair{a, b} : Pair{b, a};
    return pair.valid() ? pair : Pair{};
}

inline constexpr int pair_code(Pair pair) {
    if (!pair.valid()) return 0;
    int code = 1;
    for (int leader = 0; leader < max_actors; ++leader)
        for (int helper = leader + 1; helper < max_actors; ++helper, ++code)
            if (pair == Pair{leader, helper}) return code;
    return 0;
}

inline constexpr Pair decode_pair(int wanted) {
    int code = 1;
    for (int leader = 0; leader < max_actors; ++leader)
        for (int helper = leader + 1; helper < max_actors; ++helper, ++code)
            if (wanted == code) return {leader, helper};
    return {};
}

struct Record {
    Pair pair;
    int round = -1;  // immutable offer epoch, not a modulo timestamp or a heartbeat
    constexpr bool valid() const { return pair.valid() && round >= 0 && round <= max_round; }
    constexpr int age(int now) const { return valid() && now >= round ? now - round : -1; }
    constexpr bool fresh(int now, int ttl) const {
        const int elapsed = age(now);
        return ttl >= 0 && elapsed >= 0 && elapsed <= ttl;
    }
    constexpr Bytes encode() const {
        if (!valid()) return {};
        // bit15=0 identifies camp; low4bits are pair1..15; bits4..14 are absolute round.
        const auto word = static_cast<std::uint16_t>(pair_code(pair) | (round << 4));
        return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)};
    }
    static constexpr Record decode(Bytes bytes) {
        const auto word = static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
        if (word & 0x8000u) return {};  // a contact-focus frame is never a camp offer
        return {decode_pair(word & 15u), static_cast<int>(word >> 4)};
    }
    friend constexpr bool operator==(const Record&, const Record&) = default;
};

// Intent adapters deliberately have no SDK dependency. Group -1 means no named camp.
enum class Intent { engaging, capturing, cleared, idle };
struct Slot {
    int group = -1;
    Intent intent = Intent::idle;
};
struct Lease {
    Record record;
    int group = -1;
    int deadline = -1;
    constexpr bool valid() const {
        return record.valid() && group >= 0 && deadline >= record.round;
    }
};

inline constexpr bool active(const Lease& local, const Record& shared, int now, int ttl) {
    return local.valid() && local.record == shared && shared.fresh(now, ttl) && now <= local.deadline;
}

struct Facts {
    bool fresh_alive_group = false;
    bool participants_available = false;
    bool routes_known = false;
    bool useful_first_hit = false;  // real action-path ETA before an already achievable completion
    bool own_cost_passes = false;  // this participant's named displaced alternative, not a team bid
    bool helper_arrived = false;
    bool legal_first_contribution = false;
    bool prefix_survives = false;
    bool one_wave_feasible = false;
};
inline constexpr bool offer_feasible(const Facts& facts) {
    return facts.fresh_alive_group && facts.participants_available && facts.routes_known
        && facts.useful_first_hit && facts.own_cost_passes;
}
inline constexpr bool commit_feasible(const Facts& facts) {
    return offer_feasible(facts) && facts.helper_arrived && facts.legal_first_contribution
        && facts.prefix_survives && facts.one_wave_feasible;
}

inline constexpr bool may_accept(const Record& offer, int now, int ttl, int self,
                                 Slot leader, int group, const Facts& facts) {
    return offer.fresh(now, ttl) && self == offer.pair.helper && group >= 0
        && leader.group == group && leader.intent == Intent::idle && offer_feasible(facts);
}

inline constexpr bool may_commit(const Lease& offered, const Record& shared, int now, int ttl,
                                 int self, Slot leader, Slot helper, const Facts& facts) {
    return active(offered, shared, now, ttl) && self == offered.record.pair.leader
        && leader.group == offered.group && leader.intent == Intent::idle
        && helper.group == offered.group && helper.intent == Intent::engaging
        && commit_feasible(facts);
}

inline constexpr bool may_produce(const Lease& accepted, const Record& shared, int now, int ttl,
                                  int self, Slot leader, Slot helper, const Facts& facts) {
    // A helper cannot adopt an already committed frame it never accepted: no local lease, no hit.
    return active(accepted, shared, now, ttl) && accepted.record.pair.contains(self)
        && leader.group == accepted.group && leader.intent == Intent::engaging
        && helper.group == accepted.group && helper.intent == Intent::engaging
        && commit_feasible(facts);
}

enum class RewardRole { none, capture, release };
inline constexpr RewardRole reward_role(const Lease& local, const Record& shared, int now,
                                        int ttl, int self, bool fresh_reward) {
    if (!fresh_reward || !active(local, shared, now, ttl)) return RewardRole::none;
    if (self == shared.pair.leader) return RewardRole::capture;
    if (self == shared.pair.helper) return RewardRole::release;
    return RewardRole::none;  // this decision never invents a received reward or a respawn
}

struct Work {
    float travel = 0;           // own travel to a legal useful first action
    float extra_wait = 0;       // rendezvous minus own arrival; do not add max ETA again
    float clear = 0;
    float recovery = 0;
    float rejoin = 0;           // return/detour toward the displaced alternative
    float capture_approach = 0; // final attack position -> actual member tile, with remaining allowances
    float capture_hold = 0;     // queried capture_time + 1; designated capturer only
    float abort_rounds = 0;     // bounded additional handshake/abort allowance not counted above
    float bank = 0;             // incremental channel/lock/return cost, only for banked-value utility
};

inline constexpr int rendezvous_wait(int own_first_action, int peer_first_action) {
    if (own_first_action < 0 || peer_first_action < 0) return -1;
    return peer_first_action > own_first_action ? peer_first_action - own_first_action : 0;
}

inline bool individual_wave_survives(float current_hp, float prefix_damage,
                                    float remaining_clear_damage, float escape_damage) {
    // E8's pooled-health benefit assumes real covered rotation; the inherited camp executor
    // does not provide it. Check EACH potentially targeted participant against the whole clear
    // burden, plus pre-contribution exposure and its own exit. Inputs must not overlap.
    const float terms[] = {current_hp, prefix_damage, remaining_clear_damage, escape_damage};
    for (float term : terms) if (!std::isfinite(term) || term < 0) return false;
    const float damage = prefix_damage + remaining_clear_damage + escape_damage;
    return std::isfinite(damage) && current_hp > damage;
}

struct RotationForecast {
    bool valid = false;
    bool safe = false;
    int volleys = 0;
    std::array<float, 2> damage{};
    std::array<float, 2> hp{};
    float escape_reserve = 0;
};

inline RotationForecast rotate_remaining_wave(float hp0, float hp1, float camp_damage,
                                              float common_mitigation, float strength0,
                                              float strength1, float remaining_camp_hp) {
    // Conditional basic-only, fixed-geometry policy: both named actors remain legal contributors,
    // equal-distance targets with equal mitigation, and the executor sets the healthier actor's
    // NEXT-round priority. Index0 MUST be the contract leader; ties within 0.1 HP go to it.
    // Geometry, current priority/prefix exposure and actual readiness are caller obligations.
    // Round up the remaining basic volleys without removing a current-round camp volley. The
    // extra volley is conservative for the calibrated wake/clear order. This is not a health pool.
    RotationForecast out;
    const float inputs[] = {hp0, hp1, camp_damage, common_mitigation, strength0,
        strength1, remaining_camp_hp};
    for (float input : inputs) if (!std::isfinite(input) || input <= 0) return out;
    const double needed = std::ceil(static_cast<double>(remaining_camp_hp)
        / (static_cast<double>(strength0) + static_cast<double>(strength1)));
    if (!std::isfinite(needed) || needed < 1 || needed > 60) return out;
    const float volley_damage = camp_damage * common_mitigation;
    out.escape_reserve = 2 * volley_damage;
    if (!std::isfinite(volley_damage) || volley_damage <= 0
        || !std::isfinite(out.escape_reserve)) return out;
    out.volleys = static_cast<int>(needed);
    out.hp = {hp0, hp1};
    for (int volley = 0; volley < out.volleys; ++volley) {
        const int target = out.hp[1] > out.hp[0] + 0.1f ? 1 : 0;
        out.hp[target] -= volley_damage;
        out.damage[target] += volley_damage;
        if (!std::isfinite(out.hp[target]) || !std::isfinite(out.damage[target])) return out;
    }
    out.valid = true;
    // Strict individual exit reserves remain required. Unsafe outputs only reject the plan;
    // they do not predict a completed fight after a participant would have died.
    out.safe = out.hp[0] > out.escape_reserve && out.hp[1] > out.escape_reserve;
    return out;
}

struct Cost {
    bool valid = false;
    float occupied = 0;
    float displaced = 0;
    float budget = 0;
    bool positive = false;
};

inline Cost opportunity(float common_value, float alternative_rate, bool alternative_known,
                        const Work& work, bool capturer, float margin = 0) {
    // V is common captured-resource shadow value, not an actual 50/50 payout. This sufficient
    // fluid-model test says nothing about global optimality, lumpy work or unknown alternatives.
    Cost out;
    if (!alternative_known || !std::isfinite(common_value) || common_value <= 0
        || !std::isfinite(alternative_rate) || alternative_rate < 0
        || !std::isfinite(margin) || margin < 0 || margin >= 1) return out;
    const float terms[] = {work.travel, work.extra_wait, work.clear, work.recovery, work.rejoin,
        work.capture_approach, work.capture_hold, work.abort_rounds, work.bank};
    for (float term : terms) if (!std::isfinite(term) || term < 0) return out;
    out.occupied = work.travel + work.extra_wait + work.clear + work.recovery + work.rejoin
        + work.abort_rounds + work.bank + (capturer ? work.capture_approach + work.capture_hold : 0);
    out.displaced = alternative_rate * out.occupied;
    out.budget = common_value * 0.5f * (1 - margin);
    out.valid = std::isfinite(out.occupied) && std::isfinite(out.displaced) && std::isfinite(out.budget);
    out.positive = out.valid && out.displaced < out.budget;
    return out;
}

} // namespace mb::party
