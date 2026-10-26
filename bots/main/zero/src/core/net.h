#pragma once
// The network (DESIGN.md 2.3; activation softsign, not tanh): shared trunk over the state, value heads, and one scoring head per decision:
//   t = a(W2 a(W1 s + b1) + b2), a = softsign                          (48 -> 48 -> 48)
//   logit(c) = (U_h t) . a(P_h phi(c) + c_h) + (V_h t) . phi(c)         (U: 8x48, V: phi_h x 48, P: 8 x phi_h)
//   value = v . t + v0 (P(win) logit), margin = m . t + m0 (tanh(margin / 400))
// The draft has its own trunk (map statistics + picks -> 48 -> 32) and head. Weights are one flat float array
// (the learned-parameters block; layout below, mirrored by tools/train.py). Every matrix is stored TRANSPOSED
// (column-major: for input k, its `rows` output weights are contiguous) - see dense() in net.cpp.
#include <cstdint>
#include "cand.h"

namespace zero::net {

inline constexpr int kHidden = 48, kProj = 8, kDraftHidden = 32;

// Flat layout offsets.
struct Layout
{
    int w1, b1, w2, b2, v, v0, m, m0;
    int u[h_count], vv[h_count], p[h_count], c[h_count];
    int dw1, db1;  // draft trunk (kState -> kDraftHidden); the draft head uses U/V/P/c of h_draft over it
    int total;
};
const Layout& layout();

// The trunk embedding for one state (computed once per turn).
struct Trunk
{
    float t[kHidden];
};

void trunk(const float* w, const float* state, Trunk& out);
void draft_trunk(const float* w, const float* state, Trunk& out);  // first kDraftHidden entries used
float value(const float* w, const Trunk& t);
float margin(const float* w, const Trunk& t);

// Logits of a candidate set (head-specific weights; draft heads read the draft trunk's first 32 entries).
void logits(const float* w, const Trunk& t, const CandSet& set, float* out);

// Exact-math deployment path. The old entry points above stay unchanged for the default-off
// primitive controller. Fixed-row kernels preserve the input accumulation and softsign order.
void trunk_fast(const float* w, const float* state, Trunk& out);
void draft_trunk_fast(const float* w, const float* state, Trunk& out);

// U_h*t and V_h*t depend only on the weights, trunk and head, not on candidates. Project once
// and reuse through a macro traversal or spatial X/Y choices while that trunk stays unchanged.
// Every set scored with this context must have the same head used by project_fast().
struct HeadContext
{
    Head head{};
    float u[kProj];
    float v[kMaxPhi];
};
void project_fast(const float* w, const Trunk& t, Head head, HeadContext& out);
void logits_cached(const float* w, const HeadContext& head, const CandSet& set, float* out);
void logits_fast(const float* w, const Trunk& t, const CandSet& set, float* out);

// Deterministic sampling (per-game seed) or argmax (temperature <= 0). Returns the index and its log-probability.
struct Pick
{
    int index{};
    float logp{};
};
Pick choose(const float* logit, int n, float temperature, std::uint64_t& rng);

std::uint64_t mix(std::uint64_t x);  // seed mixing (splitmix64)

}  // namespace zero::net
