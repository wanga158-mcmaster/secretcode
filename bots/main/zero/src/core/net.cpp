#include "net.h"
#include <cmath>
#if defined(__wasm__)
#include <wasm_simd128.h>
#endif

namespace zero::net {

const Layout& layout()
{
    static const Layout L = [] {
        Layout l{};
        int at = 0;
        auto take = [&](int n) { const int o = at; at += n; return o; };
        l.w1 = take(kHidden * kState);
        l.b1 = take(kHidden);
        l.w2 = take(kHidden * kHidden);
        l.b2 = take(kHidden);
        l.v = take(kHidden);
        l.v0 = take(1);
        l.m = take(kHidden);
        l.m0 = take(1);
        auto take_head = [&](int h) {
            const int in = h == h_draft ? kDraftHidden : kHidden;
            l.u[h] = take(kProj * in);
            l.vv[h] = take(kPhi[h] * in);
            l.p[h] = take(kProj * kPhi[h]);
            l.c[h] = take(kProj);
        };
        // Keep the nine-head checkpoint prefix, including its draft trunk, at its original offsets.
        for (int h = 0; h <= h_ranged; ++h) take_head(h);
        l.dw1 = take(kDraftHidden * kState);
        l.db1 = take(kDraftHidden);
        for (int h = h_ranged + 1; h < h_count; ++h) take_head(h);
        l.total = at;
        return l;
    }();
    return L;
}

namespace {

// out[r] = b[r] + sum_k Wt[k*rows + r] x[k]: weights stored transposed (column-major: one column of `rows`
// outputs per input k), so each input updates four outputs per SIMD operation with no horizontal reduction.
// `rows` is a multiple of 4 (every layer below is); inputs that are exactly zero are skipped (one-hot facts).
#if defined(__wasm__)
__attribute__((target("simd128"))) void dense(const float* Wt, const float* b, const float* x, int rows, int cols, float* out)
{
    v128_t acc[12];
    const int q = rows / 4;
    for (int i = 0; i < q; ++i) acc[i] = b ? wasm_v128_load(b + 4 * i) : wasm_f32x4_splat(0.0f);
    for (int k = 0; k < cols; ++k)
    {
        const float xk = x[k];
        if (xk == 0.0f) continue;
        const v128_t s = wasm_f32x4_splat(xk);
        const float* col = Wt + k * rows;
        for (int i = 0; i < q; ++i) acc[i] = wasm_f32x4_add(acc[i], wasm_f32x4_mul(wasm_v128_load(col + 4 * i), s));
    }
    for (int i = 0; i < q; ++i) wasm_v128_store(out + 4 * i, acc[i]);
}
#else
void dense(const float* Wt, const float* b, const float* x, int rows, int cols, float* out)
{
    for (int r = 0; r < rows; ++r) out[r] = b ? b[r] : 0.0f;
    for (int k = 0; k < cols; ++k)
    {
        const float xk = x[k];
        if (xk == 0.0f) continue;
        const float* col = Wt + k * rows;
        for (int r = 0; r < rows; ++r) out[r] += col[r] * xk;
    }
}
#endif

// The legacy kernel's runtime-indexed accumulator array can spill to linear memory in WASM.
// Each instantiation below has named accumulators and a constant output stride: no q loop,
// accumulator-array address calculation, or reassociation of any output's input sum.
template<int Rows>
#if defined(__wasm__)
__attribute__((target("simd128")))
#endif
void dense_fast(const float* Wt, const float* b, const float* x, int cols, float* out)
{
    static_assert(Rows == 4 || Rows == 8 || Rows == 16 || Rows == 24 || Rows == 32 || Rows == 48);
#if defined(__wasm__)
#define ZERO_ACC_INIT(N) [[maybe_unused]] v128_t a##N = wasm_f32x4_splat(0.0f); \
    if constexpr (Rows >= 4 * (N + 1)) { if (b) a##N = wasm_v128_load(b + 4 * N); }
    ZERO_ACC_INIT(0);
    ZERO_ACC_INIT(1); ZERO_ACC_INIT(2); ZERO_ACC_INIT(3); ZERO_ACC_INIT(4); ZERO_ACC_INIT(5);
    ZERO_ACC_INIT(6); ZERO_ACC_INIT(7); ZERO_ACC_INIT(8); ZERO_ACC_INIT(9); ZERO_ACC_INIT(10); ZERO_ACC_INIT(11);
#undef ZERO_ACC_INIT
    for (int k = 0; k < cols; ++k)
    {
        const float xk = x[k];
        if (xk == 0.0f) continue;
        const v128_t s = wasm_f32x4_splat(xk);
        const float* col = Wt + k * Rows;
#define ZERO_ACC_ADD(N) a##N = wasm_f32x4_add(a##N, wasm_f32x4_mul(wasm_v128_load(col + 4 * N), s))
        ZERO_ACC_ADD(0);
        if constexpr (Rows >= 8) { ZERO_ACC_ADD(1); }
        if constexpr (Rows >= 16) { ZERO_ACC_ADD(2); ZERO_ACC_ADD(3); }
        if constexpr (Rows >= 24) { ZERO_ACC_ADD(4); ZERO_ACC_ADD(5); }
        if constexpr (Rows >= 32) { ZERO_ACC_ADD(6); ZERO_ACC_ADD(7); }
        if constexpr (Rows >= 48) { ZERO_ACC_ADD(8); ZERO_ACC_ADD(9); ZERO_ACC_ADD(10); ZERO_ACC_ADD(11); }
#undef ZERO_ACC_ADD
    }
#define ZERO_ACC_STORE(N) wasm_v128_store(out + 4 * N, a##N)
    ZERO_ACC_STORE(0);
    if constexpr (Rows >= 8) { ZERO_ACC_STORE(1); }
    if constexpr (Rows >= 16) { ZERO_ACC_STORE(2); ZERO_ACC_STORE(3); }
    if constexpr (Rows >= 24) { ZERO_ACC_STORE(4); ZERO_ACC_STORE(5); }
    if constexpr (Rows >= 32) { ZERO_ACC_STORE(6); ZERO_ACC_STORE(7); }
    if constexpr (Rows >= 48) { ZERO_ACC_STORE(8); ZERO_ACC_STORE(9); ZERO_ACC_STORE(10); ZERO_ACC_STORE(11); }
#undef ZERO_ACC_STORE
#else
    for (int r = 0; r < Rows; ++r) out[r] = b ? b[r] : 0.0f;
    for (int k = 0; k < cols; ++k)
    {
        const float xk = x[k];
        if (xk == 0.0f) continue;
        const float* col = Wt + k * Rows;
        for (int r = 0; r < Rows; ++r) out[r] += col[r] * xk;
    }
#endif
}

void projection_fast(const float* Wt, const float* x, int rows, int cols, float* out)
{
    switch (rows)
    {
    case 4: dense_fast<4>(Wt, nullptr, x, cols, out); break;
    case 8: dense_fast<8>(Wt, nullptr, x, cols, out); break;
    case 16: dense_fast<16>(Wt, nullptr, x, cols, out); break;
    case 24: dense_fast<24>(Wt, nullptr, x, cols, out); break;
    case 32: dense_fast<32>(Wt, nullptr, x, cols, out); break;
    case 48: dense_fast<48>(Wt, nullptr, x, cols, out); break;
    }
}

// The network's activation is softsign, x / (1 + |x|): smooth, monotonic, one division (tools/train.py uses the
// same function, so there is no approximation between training and play).
inline float act(float x) { return x / (1.0f + (x < 0.0f ? -x : x)); }

}  // namespace

void trunk(const float* w, const float* state, Trunk& out)
{
    const auto& L = layout();
    float h1[kHidden];
    dense(w + L.w1, w + L.b1, state, kHidden, kState, h1);
    for (float& v : h1) v = act(v);
    dense(w + L.w2, w + L.b2, h1, kHidden, kHidden, out.t);
    for (float& v : out.t) v = act(v);
}

void draft_trunk(const float* w, const float* state, Trunk& out)
{
    const auto& L = layout();
    dense(w + L.dw1, w + L.db1, state, kDraftHidden, kState, out.t);
    for (int i = 0; i < kDraftHidden; ++i) out.t[i] = act(out.t[i]);
    for (int i = kDraftHidden; i < kHidden; ++i) out.t[i] = 0.0f;
}

float value(const float* w, const Trunk& t)
{
    const auto& L = layout();
    float s = w[L.v0];
    for (int i = 0; i < kHidden; ++i) s += w[L.v + i] * t.t[i];
    return s;
}

float margin(const float* w, const Trunk& t)
{
    const auto& L = layout();
    float s = w[L.m0];
    for (int i = 0; i < kHidden; ++i) s += w[L.m + i] * t.t[i];
    return s;
}

void logits(const float* w, const Trunk& t, const CandSet& set, float* out)
{
    const auto& L = layout();
    const int h = set.head;
    const int in = h == h_draft ? kDraftHidden : kHidden;
    const int phi = kPhi[h];
    float u[kProj], v[kMaxPhi];
    dense(w + L.u[h], nullptr, t.t, kProj, in, u);
    dense(w + L.vv[h], nullptr, t.t, phi, in, v);
    for (int i = 0; i < set.n; ++i)
    {
        float hid[kProj];
        dense(w + L.p[h], w + L.c[h], set.c[i].phi, kProj, phi, hid);
        float s = 0.0f;
        for (int k = 0; k < kProj; ++k) s += u[k] * act(hid[k]);
        for (int k = 0; k < phi; ++k) s += v[k] * set.c[i].phi[k];
        out[i] = s;
    }
}

void trunk_fast(const float* w, const float* state, Trunk& out)
{
    const auto& L = layout();
    float h1[kHidden];
    dense_fast<kHidden>(w + L.w1, w + L.b1, state, kState, h1);
    for (float& v : h1) v = act(v);
    dense_fast<kHidden>(w + L.w2, w + L.b2, h1, kHidden, out.t);
    for (float& v : out.t) v = act(v);
}

void draft_trunk_fast(const float* w, const float* state, Trunk& out)
{
    const auto& L = layout();
    dense_fast<kDraftHidden>(w + L.dw1, w + L.db1, state, kState, out.t);
    for (int i = 0; i < kDraftHidden; ++i) out.t[i] = act(out.t[i]);
    for (int i = kDraftHidden; i < kHidden; ++i) out.t[i] = 0.0f;
}

#if defined(__wasm__)
// U t (8 rows) and V t (R rows) in one pass over the trunk: each output still sums its inputs in index order
// from zero, skipping exactly-zero inputs, as dense_fast does.
template<int R>
__attribute__((target("simd128"))) void project_pair(const float* U, const float* V, const float* x, int cols, float* u, float* v)
{
    static_assert(R == 4 || R == 16 || R == 24);
    v128_t u0 = wasm_f32x4_splat(0.0f), u1 = u0;
    [[maybe_unused]] v128_t v0 = u0, v1 = u0, v2 = u0, v3 = u0, v4 = u0, v5 = u0;
    for (int k = 0; k < cols; ++k)
    {
        const float xk = x[k];
        if (xk == 0.0f) continue;
        const v128_t s = wasm_f32x4_splat(xk);
        const float* cu = U + k * kProj;
        u0 = wasm_f32x4_add(u0, wasm_f32x4_mul(wasm_v128_load(cu), s));
        u1 = wasm_f32x4_add(u1, wasm_f32x4_mul(wasm_v128_load(cu + 4), s));
        const float* cv = V + k * R;
        v0 = wasm_f32x4_add(v0, wasm_f32x4_mul(wasm_v128_load(cv), s));
        if constexpr (R >= 16)
        {
            v1 = wasm_f32x4_add(v1, wasm_f32x4_mul(wasm_v128_load(cv + 4), s));
            v2 = wasm_f32x4_add(v2, wasm_f32x4_mul(wasm_v128_load(cv + 8), s));
            v3 = wasm_f32x4_add(v3, wasm_f32x4_mul(wasm_v128_load(cv + 12), s));
        }
        if constexpr (R >= 24)
        {
            v4 = wasm_f32x4_add(v4, wasm_f32x4_mul(wasm_v128_load(cv + 16), s));
            v5 = wasm_f32x4_add(v5, wasm_f32x4_mul(wasm_v128_load(cv + 20), s));
        }
    }
    wasm_v128_store(u, u0);
    wasm_v128_store(u + 4, u1);
    wasm_v128_store(v, v0);
    if constexpr (R >= 16) { wasm_v128_store(v + 4, v1); wasm_v128_store(v + 8, v2); wasm_v128_store(v + 12, v3); }
    if constexpr (R >= 24) { wasm_v128_store(v + 16, v4); wasm_v128_store(v + 20, v5); }
}
#endif

void project_fast(const float* w, const Trunk& t, Head head, HeadContext& out)
{
    const auto& L = layout();
    const int in = head == h_draft ? kDraftHidden : kHidden;
    out.head = head;
#if defined(__wasm__)
    switch (kPhi[head])
    {
    case 24: project_pair<24>(w + L.u[head], w + L.vv[head], t.t, in, out.u, out.v); return;
    case 16: project_pair<16>(w + L.u[head], w + L.vv[head], t.t, in, out.u, out.v); return;
    case 4: project_pair<4>(w + L.u[head], w + L.vv[head], t.t, in, out.u, out.v); return;
    default: break;
    }
#endif
    dense_fast<kProj>(w + L.u[head], nullptr, t.t, in, out.u);
    projection_fast(w + L.vv[head], t.t, kPhi[head], in, out.v);
}

#if defined(__wasm__)
// One candidate's logit with the projection's 8 outputs in two vectors. Every float operation is the scalar
// path's, in its order: the hidden sums accumulate inputs in index order, softsign is x / (1 + |x|) per lane
// (exact IEEE ops either way), and the two dot products add their terms in index order. A zero fact adds
// v * 0 = 0 to the second sum, so it is skipped (weights are finite).
static_assert(kProj == 8);
__attribute__((target("simd128"))) float logit_one(const float* P, const float* c, const HeadContext& head, const float* x, int phi)
{
    // Nonzero facts as a bit mask (four lanes per compare), visited in index order.
    std::uint32_t mask = 0;
    for (int k = 0; k < phi; k += 4)
        mask |= static_cast<std::uint32_t>(wasm_i32x4_bitmask(wasm_f32x4_ne(wasm_v128_load(x + k), wasm_f32x4_splat(0.0f)))) << k;
    v128_t a0 = wasm_v128_load(c), a1 = wasm_v128_load(c + 4);
    for (std::uint32_t m = mask; m; m &= m - 1)
    {
        const int k = __builtin_ctz(m);
        const v128_t s = wasm_f32x4_splat(x[k]);
        const float* col = P + k * kProj;
        a0 = wasm_f32x4_add(a0, wasm_f32x4_mul(wasm_v128_load(col), s));
        a1 = wasm_f32x4_add(a1, wasm_f32x4_mul(wasm_v128_load(col + 4), s));
    }
    const v128_t one = wasm_f32x4_splat(1.0f);
    a0 = wasm_f32x4_div(a0, wasm_f32x4_add(one, wasm_f32x4_abs(a0)));
    a1 = wasm_f32x4_div(a1, wasm_f32x4_add(one, wasm_f32x4_abs(a1)));
    alignas(16) float a[kProj];
    wasm_v128_store(a, a0);
    wasm_v128_store(a + 4, a1);
    float s = 0.0f;
    for (int k = 0; k < kProj; ++k) s += head.u[k] * a[k];
    for (std::uint32_t m = mask; m; m &= m - 1)
    {
        const int k = __builtin_ctz(m);
        s += head.v[k] * x[k];
    }
    return s;
}
#endif

void logits_cached(const float* w, const HeadContext& head, const CandSet& set, float* out)
{
    const auto& L = layout();
    const int h = head.head, phi = kPhi[h];
    for (int i = 0; i < set.n; ++i)
    {
#if defined(__wasm__)
        out[i] = logit_one(w + L.p[h], w + L.c[h], head, set.c[i].phi, phi);
#else
        float hid[kProj];
        dense_fast<kProj>(w + L.p[h], w + L.c[h], set.c[i].phi, phi, hid);
        float s = 0.0f;
        for (int k = 0; k < kProj; ++k) s += head.u[k] * act(hid[k]);
        for (int k = 0; k < phi; ++k) s += head.v[k] * set.c[i].phi[k];
        out[i] = s;
#endif
    }
}

void logits_fast(const float* w, const Trunk& t, const CandSet& set, float* out)
{
    HeadContext head;
    project_fast(w, t, set.head, head);
    logits_cached(w, head, set, out);
}

std::uint64_t mix(std::uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

Pick choose(const float* logit, int n, float temperature, std::uint64_t& rng)
{
    Pick p;
    if (n <= 0) return p;
    float mx = logit[0];
    int arg = 0;
    for (int i = 1; i < n; ++i)
        if (logit[i] > mx) { mx = logit[i]; arg = i; }
    const float tau = temperature > 0.0f ? temperature : 1.0f;
    float z = 0.0f, e[kMaxCands];
    for (int i = 0; i < n; ++i) { e[i] = std::exp((logit[i] - mx) / tau); z += e[i]; }
    int idx = arg;
    if (temperature > 0.0f)
    {
        rng = mix(rng);
        const float u = static_cast<float>(rng >> 40) / static_cast<float>(1ull << 24) * z;
        float acc = 0.0f;
        idx = n - 1;
        for (int i = 0; i < n; ++i)
        {
            acc += e[i];
            if (u < acc) { idx = i; break; }
        }
    }
    p.index = idx;
    p.logp = std::log(e[idx] / z);
    return p;
}

}  // namespace zero::net
