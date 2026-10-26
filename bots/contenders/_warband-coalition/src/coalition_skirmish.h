#pragma once
// Conditional forced-focus forecast for contact coalitions. The caller must witness that the
// selected enemy is personally visible and attackable now. That target is vulnerable immediately;
// its offense still waits for Fighter::arrive. Other fighters retain the original simulator's
// contact/arrival assumptions. This is an aggregate simultaneous-damage model, not a per-pair,
// actor-order, movement, permission or future-support certificate.
#include "model.h"

namespace mb::coalition {

// Compare policies within this conditional model, not actual-game safety. Ties retain useful
// coordination; a worse shared proposal leaves the caller's original target/verdict intact.
inline constexpr bool accept_shared_outcome(const model::SkirmishOutcome& baseline,
                                            const model::SkirmishOutcome& shared) {
    return shared.our_losses <= baseline.our_losses
        && shared.their_losses >= baseline.their_losses
        && (!baseline.we_survive || shared.we_survive)
        && (baseline.they_survive || !shared.they_survive);
}

inline model::SkirmishOutcome simulate_forced_focus(const model::Fighter* f, int n, int max_rounds,
                                                   int forced_enemy, bool threat_focus = false) {
    using model::SkirmishOutcome;
    n = std::min(n, 16);
    if (forced_enemy < 0 || forced_enemy >= n || f[forced_enemy].ours || f[forced_enemy].hp <= 0.0f)
        return model::simulate_skirmish(f, n, max_rounds, threat_focus);

    SkirmishOutcome out;
    float hp[16];
    bool dead[16];
    int order[2][16];
    int count[2]{};
    float rate[2]{};
    int alive[2]{};
    int last_arrival = 1;
    for (int i = 0; i < n; ++i) {
        hp[i] = f[i].hp;
        dead[i] = hp[i] <= 0.0f;
        const int side = f[i].ours ? 0 : 1;
        int k = count[side]++;
        while (k > 0 && f[order[side][k - 1]].hp > f[i].hp) {
            order[side][k] = order[side][k - 1];
            --k;
        }
        order[side][k] = i;
        if (!dead[i]) ++alive[side];
        if (!dead[i] && f[i].arrive <= 1) rate[side] += f[i].dps;
        last_arrival = std::max(last_arrival, f[i].arrive);
    }
    int round = 1;
    const auto arrived = [&](int i) { return f[i].arrive <= round; };
    float threat[16];
    if (threat_focus)
        for (int i = 0; i < n; ++i)
            threat[i] = f[i].dps + (f[i].cast[0] + f[i].cast[1]) / static_cast<float>(std::max(1, f[i].cooldown));
    int focus[2] = {-1, -1};
    const auto rescan = [&](int side) {
        // The forced enemy's present targetability is caller evidence, independent of when its
        // own offense arrives. Keep it as our first target until death, including later arrivals.
        if (side == 1 && !dead[forced_enemy]) {
            focus[side] = forced_enemy;
            return;
        }
        int best = -1;
        for (int k = 0; k < count[side]; ++k) {
            const int i = order[side][k];
            if (dead[i] || !arrived(i)) continue;
            const bool better = best < 0 || (threat_focus && side == 1
                ? threat[i] * hp[best] > threat[best] * hp[i] : hp[i] < hp[best]);
            if (better) best = i;
        }
        focus[side] = best;
    };
    const auto any_alive = [&](int side) { return alive[side] > 0; };
    const auto kill = [&](int i) {
        int sides = 0;
        for (int guard = 0; guard < 2 && i >= 0 && i < n && !dead[i]; ++guard) {
            dead[i] = true;
            const int side = f[i].ours ? 0 : 1;
            sides |= 1 << side;
            --alive[side];
            if (arrived(i)) rate[side] -= f[i].dps;
            (side == 0 ? out.our_losses : out.their_losses) += f[i].value;
            i = f[i].linked;
        }
        for (int side = 0; side < 2; ++side)
            if (sides & (1 << side)) rescan(side);
    };
    rescan(0);
    rescan(1);

    constexpr int kMaxRounds = 32;
    constexpr int kMaxEvents = 160;
    max_rounds = std::min(max_rounds, kMaxRounds);
    int ev_head[kMaxRounds + 1];
    for (int r = 0; r <= kMaxRounds; ++r) ev_head[r] = -1;
    int ev_next[kMaxEvents], ev_who[kMaxEvents];
    float ev_dmg[kMaxEvents];
    int n_ev = 0;
    const auto add_event = [&](int at, int who, float damage) {
        if (at < 1 || at > max_rounds || n_ev >= kMaxEvents) return;
        ev_who[n_ev] = who;
        ev_dmg[n_ev] = damage;
        ev_next[n_ev] = ev_head[at];
        ev_head[at] = n_ev++;
    };
    for (int i = 0; i < n; ++i) {
        const int cooldown = std::max(1, f[i].cooldown);
        const bool a = f[i].cast[0] > 0.0f, b = f[i].cast[1] > 0.0f;
        const int from = std::max(1, f[i].arrive);
        int t0 = a ? std::max(from, f[i].ready[0]) : 1 << 20;
        int t1 = b ? std::max(from, f[i].ready[1]) : 1 << 20;
        if (a && b && t0 == t1) (f[i].cast[0] >= f[i].cast[1] ? t1 : t0) += 1;
        for (int t = t0; a && t <= max_rounds; t += cooldown) add_event(t, i, f[i].cast[0]);
        for (int t = t1; b && t <= max_rounds; t += cooldown) add_event(t, i, f[i].cast[1]);
    }
    out.first_target = focus[1];
    for (int r = 0; r < max_rounds; ++r) {
        round = r + 1;
        if (round > 1 && round <= last_arrival) {
            bool joined[2]{};
            for (int i = 0; i < n; ++i)
                if (!dead[i] && f[i].arrive == round) {
                    rate[f[i].ours ? 0 : 1] += f[i].dps;
                    joined[f[i].ours ? 0 : 1] = true;
                }
            for (int side = 0; side < 2; ++side)
                if (joined[side]) rescan(side);
        }
        if (out.first_target < 0) out.first_target = focus[1];
        float dealt[2] = {rate[0], rate[1]};
        for (int event = ev_head[round]; event >= 0; event = ev_next[event])
            if (!dead[ev_who[event]]) dealt[f[ev_who[event]].ours ? 0 : 1] += ev_dmg[event];
        bool landed = false;
        for (int side = 0; side < 2; ++side) {
            const int victim_side = 1 - side;
            float left = dealt[side];
            while (left > 0.0f) {
                const int target = focus[victim_side];
                if (target < 0) break;
                const float take = std::min(left, hp[target]);
                hp[target] -= take;
                left -= take;
                landed = true;
                if (hp[target] <= 0.0f) kill(target);
            }
        }
        if (!landed) {
            if (round >= last_arrival) break;
            continue;
        }
        out.rounds = round;
        if (!any_alive(0) || !any_alive(1)) break;
    }
    out.we_survive = any_alive(0);
    out.they_survive = any_alive(1);
    return out;
}

} // namespace mb::coalition
