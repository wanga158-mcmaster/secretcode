#pragma once
// Opponent context: a compact, fact-only summary of how THIS opponent has played so far this game, as seen by
// one champion. Representation only (no thresholds, no choices): the network learns what to do with it.
//
// Every style fact is a ratio of two exponential moving averages over game time (num = sum d^age * w * x,
// den = sum d^age * w, d = 2^(-1/H) per round), so it is the recency-weighted mean of its samples and is not
// biased toward zero early. A fact with no samples yet writes its neutral value (0), and three separate
// evidence facts say how much has been seen, so "unknown" and "observed zero" stay distinguishable.
//
// Update once per round (later calls in the same round are ignored: observe() runs again after each
// refresh), O(visible units + visible camps), no SDK calls, no allocation, deterministic (fixed loop order,
// Obs lists are id-sorted by canonical_observation_order, no transcendental functions except one log2 per
// fact 4 sample, computed by a fixed polynomial).
//
// Facts (all in [-1, 1]; half-lives H in rounds are representation constants):
//  0 contact   alive rounds with an enemy within 8 of us                                     (H 100)
//  1 pack      contact rounds: largest set of visible enemies within 4 of one enemy, / 5      (H 60)
//  2 press     contact rounds: enemies within 4 of us, / 5                                   (H 60)
//  3 cc        contact rounds: stunned share of our side within 8 (self + visible allies)    (H 60)
//  4 fed       rounds with an enemy in view: log4(best enemy power / best own-side power),
//              power = max_hp * strength, clamped                                            (H 100)
//  5 trade     champion deaths, ours vs theirs, from alive-count drops: 2*ours/(ours+theirs)-1
//              (theirs includes camp kills; +1 = only we die)                                (H 300)
//  6 gank      enemy sightings (per enemy per round) within 10 of our spawn centre           (H 100)
//  7 camp      enemy sightings within 3 of a visible living camp                             (H 100)
//  8 bank      their share of gems banked by both teams, centred: 2*them/(us+them)-1          (H 150)
//  9 ranged    share of distinct identified enemies with a ranged basic attack
// 10 dash      share of distinct identified enemies with a dash ability
// 11 stun      share of distinct identified enemies with a stunning ability (cc, or an area/dash with a
//              duration that targets enemies)
// 12 quiet     rounds since an enemy was last within 8 of us, / 100, clamped; 1 if never
// 13 ev_alive  alive rounds observed n / (n + 50)
// 14 ev_contact contact rounds c / (c + 20)
// 15 ev_kit    distinct enemies identified / 5
#include <cstdint>
#include "map.h"
#include "obs.h"

namespace zero {

struct OppContext
{
    static constexpr int kFacts = 16;
    static constexpr int kMaxIds = 8;

    // Feed this turn's observation (after sdk observe + zero::observe). Ignored unless o.round is newer than
    // the last round accepted.
    void update(const Obs& o, const Map& map);
    // Write kFacts values.
    void write(float* out) const;

    struct Ema
    {
        float num{}, den{};
        void decay(float d) { num *= d; den *= d; }
        void add(float x, float w = 1.0f) { num += w * x; den += w; }
        float mean(float neutral = 0.0f) const { return den > 1e-6f ? num / den : neutral; }
    };

    // State (public for tests and replay tools; plain data, copyable).
    int last_round{ -1 };
    int last_contact{ -1 };
    int alive_rounds{}, contact_rounds{};
    bool was_alive{ true };
    int prev_allies_alive{ -1 }, prev_enemies_alive{ -1 };
    std::uint32_t prev_banked_us{}, prev_banked_them{};
    bool bank_valid{};
    Ema contact, pack, press, cc, fed, trade, gank, camp, bank;
    int n_ids{};
    int ids[kMaxIds]{};
    std::uint8_t kit[kMaxIds]{};  // bit 0 ranged, 1 dash, 2 stun
};

// Kit bits of a champion type from the catalog (bit 0 ranged, 1 dash, 2 stun); 0 for an unknown type.
std::uint8_t opp_kit_bits(int champion_type);

}  // namespace zero
