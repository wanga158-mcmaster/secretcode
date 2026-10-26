#include "student_net.h"
#include "net.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(__wasm__)
#include <wasm_simd128.h>
#endif

namespace zero::student::net {

const Layout& layout()
{
    static const Layout L = [] {
        Layout l{};
        int at = ::zero::net::layout().total;
        l.begin = at;
        auto take = [&](int n) { const int offset = at; at += n; return offset; };
        l.w1 = take(kHidden * kState);
        l.b1 = take(kHidden);
        l.w2 = take(kHidden * kHidden);
        l.b2 = take(kHidden);
        l.command_w = take(kCommands * kHidden);
        l.command_b = take(kCommands);
        for (int i = 0; i < kFamilies; ++i)
        {
            l.family_w[i] = take(kPhi * kHidden);
            l.family_b[i] = take(kPhi);
        }
        l.v = take(kHidden);
        l.v0 = take(1);
        l.total = at;
        if (l.begin != kLegacyPrefix || l.total != kWeightEnd || l.total > kWeightCapacity)
            throw std::logic_error("error student weight layout mismatch");
        return l;
    }();
    return L;
}

const AttentionLayout& attention_layout()
{
    static const AttentionLayout L = [] {
        AttentionLayout l{};
        int at = layout().total;
        l.begin = at;
        auto take = [&](int n) { const int offset = at; at += n; return offset; };
        l.w_e = take(kCampContextPhi * kAttentionHidden);
        l.b_e = take(kAttentionHidden);
        l.w_q = take(kHidden * kAttentionHidden);
        l.b_q = take(kAttentionHidden);
        l.w_r = take(kAttentionHidden * kHidden);
        l.total = at;
        if (l.begin != kWeightEnd || l.total != kAttentionWeightEnd || l.total > kWeightCapacity)
            throw std::logic_error("error student attention weight layout mismatch");
        return l;
    }();
    return L;
}

int family_index(Head head)
{
    switch (head)
    {
    case h_goal: return 0;
    case h_step: return 1;
    case h_attack: return 2;
    case h_cast0: return 3;
    case h_cast1: return 4;
    case h_shop: return 5;
    case h_draft: return 6;
    case h_aux: return 7;
    case h_priority: return 8;
    case h_quantity: return 9;
    default: return -1;
    }
}

namespace {

inline float act(float x) { return x / (1.0f + (x < 0.0f ? -x : x)); }

#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
void dense4(const float* matrix, const float* bias, const float* input, int cols, float* out)
{
#if defined(__wasm__)
    v128_t accumulator = wasm_v128_load(bias);
    for (int k = 0; k < cols; ++k)
    {
        const float x = input[k];
        if (x == 0.0f) continue;
        accumulator = wasm_f32x4_add(accumulator,
            wasm_f32x4_mul(wasm_v128_load(matrix + k * 4), wasm_f32x4_splat(x)));
    }
    wasm_v128_store(out, accumulator);
#else
    for (int r = 0; r < 4; ++r) out[r] = bias[r];
    for (int k = 0; k < cols; ++k)
    {
        const float x = input[k];
        if (x == 0.0f) continue;
        for (int r = 0; r < 4; ++r) out[r] += matrix[k * 4 + r] * x;
    }
#endif
}

// Increasing input order, separate f32 multiply/add, exactly the scalar reference arithmetic.
// Fixed named accumulators keep all partial sums in WASM locals, with no dynamic output loop.
template<int Rows>
#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
void dense(const float* matrix, const float* bias, const float* input, int cols, float* out)
{
    static_assert(Rows == 16 || Rows == 24);
#if defined(__wasm__)
    v128_t a0 = wasm_v128_load(bias), a1 = wasm_v128_load(bias + 4);
    v128_t a2 = wasm_v128_load(bias + 8), a3 = wasm_v128_load(bias + 12);
    [[maybe_unused]] v128_t a4 = wasm_f32x4_splat(0.0f), a5 = wasm_f32x4_splat(0.0f);
    if constexpr (Rows == 24) { a4 = wasm_v128_load(bias + 16); a5 = wasm_v128_load(bias + 20); }
    for (int k = 0; k < cols; ++k)
    {
        const float x = input[k];
        if (x == 0.0f) continue;
        const v128_t s = wasm_f32x4_splat(x);
        const float* col = matrix + k * Rows;
        a0 = wasm_f32x4_add(a0, wasm_f32x4_mul(wasm_v128_load(col), s));
        a1 = wasm_f32x4_add(a1, wasm_f32x4_mul(wasm_v128_load(col + 4), s));
        a2 = wasm_f32x4_add(a2, wasm_f32x4_mul(wasm_v128_load(col + 8), s));
        a3 = wasm_f32x4_add(a3, wasm_f32x4_mul(wasm_v128_load(col + 12), s));
        if constexpr (Rows == 24)
        {
            a4 = wasm_f32x4_add(a4, wasm_f32x4_mul(wasm_v128_load(col + 16), s));
            a5 = wasm_f32x4_add(a5, wasm_f32x4_mul(wasm_v128_load(col + 20), s));
        }
    }
    wasm_v128_store(out, a0); wasm_v128_store(out + 4, a1);
    wasm_v128_store(out + 8, a2); wasm_v128_store(out + 12, a3);
    if constexpr (Rows == 24) { wasm_v128_store(out + 16, a4); wasm_v128_store(out + 20, a5); }
#else
    for (int r = 0; r < Rows; ++r) out[r] = bias[r];
    for (int k = 0; k < cols; ++k)
    {
        const float x = input[k];
        if (x == 0.0f) continue;
        const float* col = matrix + k * Rows;
        for (int r = 0; r < Rows; ++r) out[r] += col[r] * x;
    }
#endif
}

}  // namespace

void embed(const float* weights, const float* state, Embedding& out)
{
    const auto& L = layout();
    float first[kHidden];
    dense<kHidden>(weights + L.w1, weights + L.b1, state, kState, first);
    for (float& x : first) x = act(x);
    dense<kHidden>(weights + L.w2, weights + L.b2, first, kHidden, out.h);
    for (float& x : out.h) x = act(x);
}

#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
bool embed_attention(const float* weights, const float* state, std::span<const CampContext> context,
                     Embedding& out, AttentionDiagnostics* diagnostics)
{
    const auto& L = layout();
    const auto& A = attention_layout();
    float first[kHidden], pre[kHidden], base[kHidden], query[kAttentionHidden];
    dense<kHidden>(weights + L.w1, weights + L.b1, state, kState, first);
    for (float& x : first) x = act(x);
    dense<kHidden>(weights + L.w2, weights + L.b2, first, kHidden, pre);
    for (int k = 0; k < kHidden; ++k) base[k] = act(pre[k]);
    dense4(weights + A.w_q, weights + A.b_q, base, kHidden, query);
    struct Entity { float z[kAttentionHidden]; float score; };
    std::vector<Entity> entities;
    entities.reserve(context.size());
    float maximum = -std::numeric_limits<float>::infinity();
    for (const auto& group : context)
    {
        Entity entity;
        dense4(weights + A.w_e, weights + A.b_e, group.phi, kCampContextPhi, entity.z);
        entity.score = 0.0f;
        for (int k = 0; k < kAttentionHidden; ++k)
        {
            entity.z[k] = act(entity.z[k]);
            entity.score += query[k] * entity.z[k];
        }
        if (!std::isfinite(entity.score)) return false;
        maximum = std::max(maximum, entity.score);
        entities.push_back(entity);
    }
    float denominator = 0.0f, pool[kAttentionHidden]{};
#if defined(__wasm__)
    v128_t numerator = wasm_f32x4_splat(0.0f);
#endif
    for (const auto& entity : entities)
    {
        const float mass = std::exp(entity.score - maximum);
        denominator += mass;
#if defined(__wasm__)
        numerator = wasm_f32x4_add(numerator,
            wasm_f32x4_mul(wasm_v128_load(entity.z), wasm_f32x4_splat(mass)));
#else
        for (int k = 0; k < kAttentionHidden; ++k) pool[k] += mass * entity.z[k];
#endif
    }
#if defined(__wasm__)
    wasm_v128_store(pool, numerator);
#endif
    if (!entities.empty())
    {
        if (!(denominator > 0.0f) || !std::isfinite(denominator)) return false;
        for (float& x : pool) x /= denominator;
    }
    dense<kHidden>(weights + A.w_r, pre, pool, kAttentionHidden, out.h);
    for (float& x : out.h)
    {
        x = act(x);
        if (!std::isfinite(x)) return false;
    }
    if (diagnostics)
    {
        diagnostics->entities = context.size();
        diagnostics->maximum_score = entities.empty() ? 0.0f : maximum;
        diagnostics->denominator = denominator;
        for (int k = 0; k < kAttentionHidden; ++k) diagnostics->pool[k] = pool[k];
    }
    return true;
}

#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
void command_logits(const float* weights, const Embedding& state, float* out)
{
    const auto& L = layout();
#if defined(__wasm__)
    const float* bias = weights + L.command_b;
    v128_t a0 = wasm_v128_load(bias), a1 = wasm_v128_load(bias + 4), a2 = wasm_v128_load(bias + 8);
    float a12 = bias[12], a13 = bias[13], a14 = bias[14];
    for (int k = 0; k < kHidden; ++k)
    {
        const float x = state.h[k];
        if (x == 0.0f) continue;
        const v128_t s = wasm_f32x4_splat(x);
        const float* col = weights + L.command_w + k * kCommands;
        a0 = wasm_f32x4_add(a0, wasm_f32x4_mul(wasm_v128_load(col), s));
        a1 = wasm_f32x4_add(a1, wasm_f32x4_mul(wasm_v128_load(col + 4), s));
        a2 = wasm_f32x4_add(a2, wasm_f32x4_mul(wasm_v128_load(col + 8), s));
        a12 += col[12] * x; a13 += col[13] * x; a14 += col[14] * x;
    }
    wasm_v128_store(out, a0); wasm_v128_store(out + 4, a1); wasm_v128_store(out + 8, a2);
    out[12] = a12; out[13] = a13; out[14] = a14;
#else
    for (int r = 0; r < kCommands; ++r) out[r] = weights[L.command_b + r];
    for (int k = 0; k < kHidden; ++k)
    {
        const float x = state.h[k];
        if (x == 0.0f) continue;
        const float* col = weights + L.command_w + k * kCommands;
        for (int r = 0; r < kCommands; ++r) out[r] += col[r] * x;
    }
#endif
}

bool project(const float* weights, const Embedding& state, Head head, Projection& out)
{
    out.head = head;
    const int family = family_index(head);
    if (family < 0)
    {
        for (float& x : out.q) x = 0.0f;
        return false;
    }
    const auto& L = layout();
    dense<kPhi>(weights + L.family_w[family], weights + L.family_b[family], state.h, kHidden, out.q);
    return true;
}

#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
float score(const Projection& head, const float* phi)
{
    float s = 0.0f;
#if defined(__wasm__)
    // Multiply four independent terms together, then add lanes in the scalar reference's order.
    // No horizontal tree reduction: this preserves every f32 rounding point of q dot phi.
    for (int k = 0; k < kPhi; k += 4)
    {
        const v128_t p = wasm_f32x4_mul(wasm_v128_load(head.q + k), wasm_v128_load(phi + k));
        s += wasm_f32x4_extract_lane(p, 0); s += wasm_f32x4_extract_lane(p, 1);
        s += wasm_f32x4_extract_lane(p, 2); s += wasm_f32x4_extract_lane(p, 3);
    }
#else
    for (int k = 0; k < kPhi; ++k) s += head.q[k] * phi[k];
#endif
    return s;
}

float value(const float* weights, const Embedding& state)
{
    const auto& L = layout();
    float s = weights[L.v0];
    for (int k = 0; k < kHidden; ++k) s += weights[L.v + k] * state.h[k];
    return s;
}

}  // namespace zero::student::net
