#pragma once
// Native bounded planner interface. Generated source receives only permitted
// personal observations, remembered observations and immutable map metadata.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "optimizer_config.h"

namespace mb::optimizer {

inline float unit(float value) { return std::clamp(value, 0.0f, 1.0f); }

inline std::array<float, 12> job_features(const Ctx& ctx, const GroupInfo& info,
                                         const GroupBelief& belief, int travel) {
    return {unit(static_cast<float>(ctx.round) / static_cast<float>(std::max(1u, cambc::constants::max_rounds))),
            unit(ctx.hp_frac()),
            unit(static_cast<float>(ctx.gems) / static_cast<float>(std::max(1u, cambc::constants::winning_gems))),
            unit(static_cast<float>(ctx.gold) / 1000.0f),
            unit(static_cast<float>(ctx.enemies.size()) / 5.0f),
            unit(static_cast<float>(ctx.allies.size()) / 5.0f),
            unit(static_cast<float>(travel) / 100.0f),
            unit(static_cast<float>(info.gems) / 200.0f),
            unit(static_cast<float>(info.gold) / 300.0f),
            belief.status == GroupStatus::reward ? 0.0f :
                belief.hp_left >= 0.0f && info.total_hp > 0.0f ? unit(belief.hp_left / info.total_hp) : 1.0f,
            info.harmless ? 1.0f : 0.0f,
            unit(static_cast<float>(cheb(info.center, World::get().ally_spawn_center)) / 100.0f)};
}

struct ActorState {
    int round{ -1 };
    unsigned decisions{};
    std::uint32_t rng{};
    int preference{};
};

inline ActorState& actor_state() { static ActorState state; return state; }

inline std::uint32_t random_word(ActorState& state) {
    state.rng ^= state.rng << 13;
    state.rng ^= state.rng >> 17;
    state.rng ^= state.rng << 5;
    return state.rng;
}

inline int preference(const Ctx& ctx) {
    auto& state = actor_state();
    if (state.round == ctx.round) return state.preference;
    state.round = ctx.round;
    state.preference = 0;
    // This gate exists in traced and untraced policies. No stochastic choice is
    // made outside it, so a sampled choice is never silently omitted from training.
    if (state.decisions >= generated::max_decisions || fuel() < 65000) return 0;
    // Actor context is independent of the first candidate visited. Construct it
    // once, not a full job-feature vector for every bound and expensive score.
    const std::array<float, 12> x{
        unit(static_cast<float>(ctx.round) / static_cast<float>(std::max(1u, cambc::constants::max_rounds))),
        unit(ctx.hp_frac()),
        unit(static_cast<float>(ctx.gems) / static_cast<float>(std::max(1u, cambc::constants::winning_gems))),
        unit(static_cast<float>(ctx.gold) / 1000.0f),
        unit(static_cast<float>(ctx.enemies.size()) / 5.0f),
        unit(static_cast<float>(ctx.allies.size()) / 5.0f),
        0, 0, 0, 0, 0,
        unit(static_cast<float>(cheb(ctx.pos, World::get().ally_spawn_center)) / 100.0f)};
    float logits[5], probabilities[5];
    float maximum = -20.0f;
    for (int a = 0; a < 5; ++a) {
        float value = generated::weights[a][12];
        for (int j = 0; j < 12; ++j) value += generated::weights[a][j] * x[static_cast<std::size_t>(j)];
        logits[a] = std::clamp(value, -20.0f, 20.0f);
        maximum = std::max(maximum, logits[a]);
    }
    float sum = 0;
    for (int a = 0; a < 5; ++a) { probabilities[a] = std::exp(logits[a] - maximum); sum += probabilities[a]; }
    for (float& probability : probabilities) probability /= sum;
    const auto actor = static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self()));
    int selected = 0;
    if constexpr (generated::stochastic) {
        if (state.rng == 0) {
            state.rng = generated::seed ^ ((actor + 1u) * 0x9e3779b9u);
            if (state.rng == 0) state.rng = 0x6d2b79f5u;
        }
        const float draw = (static_cast<float>(random_word(state) >> 8) + 0.5f) / 16777216.0f;
        float accumulated = 0;
        selected = 4;
        for (int a = 0; a < 5; ++a) {
            accumulated += probabilities[a];
            if (draw < accumulated) { selected = a; break; }
        }
    } else {
        for (int a = 1; a < 5; ++a) if (logits[a] > logits[selected]) selected = a;
    }
    ++state.decisions;
    state.preference = selected;
    if constexpr (generated::trace) {
        // Integer bit fields preserve every sampled float without the substantial
        // WASI fuel cost of formatting eighteen floating-point decimal values.
        // Fixed strings plus at most twenty ten-digit integers fit in 1024 bytes.
        char text[1024];
        char* cursor = text;
        auto literal = [&](const char* value) { while (*value) *cursor++ = *value++; };
        auto integer = [&](std::uint32_t value) {
            char digits[10]; unsigned length = 0;
            do { digits[length++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
            while (length) *cursor++ = digits[--length];
        };
        auto bits = [&](float value) { std::uint32_t result; std::memcpy(&result, &value, sizeof result); integer(result); };
        literal("OPT_TRAJ {\"schema\":\"contenders-actor-observation/v1\",\"feature_schema\":\"contenders-actor-context/v1\",\"encoding\":\"float32-bits\",\"round\":");
        integer(static_cast<std::uint32_t>(ctx.round)); literal(",\"actor\":"); integer(actor);
        literal(",\"decision\":"); integer(state.decisions); literal(",\"feature_bits\":[");
        for (int j = 0; j < 12; ++j) { if (j) literal(","); bits(x[static_cast<std::size_t>(j)]); }
        literal("],\"action\":"); integer(static_cast<std::uint32_t>(selected)); literal(",\"probability_bits\":[");
        for (int a = 0; a < 5; ++a) { if (a) literal(","); bits(probabilities[a]); }
        literal("],\"legal_mask\":[true,true,true,true,true]}\n");
        std::fwrite(text, 1, static_cast<std::size_t>(cursor - text), stdout);
    }
    return selected;
}

inline float factor(const Ctx& ctx, const GroupInfo& info, const GroupBelief& belief, int travel) {
    if constexpr (!generated::is_policy) {
        const auto x = job_features(ctx, info, belief, travel);
        return generated::program(x.data());
    }
    else {
        switch (preference(ctx)) {
        case 1: return 0.25f + 3.75f * unit(static_cast<float>(info.gems) / 200.0f);
        case 2: return 0.25f + 3.75f * unit(static_cast<float>(info.gold) / 300.0f);
        case 3: return 0.25f + 3.75f * (1.0f - unit(static_cast<float>(travel) / 100.0f));
        case 4: return 0.25f + 3.75f * (1.0f - unit(static_cast<float>(cheb(info.center, World::get().ally_spawn_center)) / 100.0f));
        default: return 1.0f;
        }
    }
}

inline float upper_bound(const Ctx& ctx, const GroupInfo& info, const GroupBelief& belief, int travel) {
    // The policy's current scalar is known before expensive group evaluation.
    // A blanket bound of four needlessly disables pruning, even after sampling
    // ends and the actual preference is neutral. The exact scalar is admissible.
    if constexpr (generated::is_policy) return factor(ctx, info, belief, travel);
    else return generated::bound;
}

} // namespace mb::optimizer
