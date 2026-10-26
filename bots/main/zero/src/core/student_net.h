#pragma once
// Compact deployment policy, feature ABI student.h. Matrices are column-major: all output
// coefficients for one input are contiguous. Every coefficient is learned; inference has no
// hidden layer for executable candidates or candidate-count bound. cfg8 adds one shared attention
// pool over map groups, with four learned latent values per group, before candidate scoring.
#include "student.h"
#include "student_context.h"
#include <span>

namespace zero::student::net {

inline constexpr int kHidden = 16, kCommands = 15, kFamilies = 10, kLayoutVersion = 1;
inline constexpr int kLegacyPrefix = 24842, kWeightCapacity = 32768;
inline constexpr Head kFamilyHeads[kFamilies] = {
    h_goal, h_step, h_attack, h_cast0, h_cast1, h_shop, h_draft, h_aux, h_priority, h_quantity
};
inline constexpr int kParameterCount = kHidden * kState + kHidden + kHidden * kHidden + kHidden
    + kCommands * kHidden + kCommands + kFamilies * (kPhi * kHidden + kPhi) + kHidden + 1;
inline constexpr int kWeightEnd = kLegacyPrefix + kParameterCount;
static_assert(kState == 48 && kPhi == 24 && kCommands == a_count);
static_assert(kWeightEnd <= kWeightCapacity);

// Absolute offsets in the same learned-parameter block as zero::net, excluding config[0..15].
// Order: W1,b1,W2,b2,command_W,command_b,(family_W,family_b) in kFamilyHeads order,value_W,value_b.
// total is the absolute end; parameter count is total-begin. Existing prefix bytes are untouched.
struct Layout
{
    int begin, w1, b1, w2, b2, command_w, command_b;
    int family_w[kFamilies], family_b[kFamilies];
    int v, v0, total;
};
const Layout& layout();
int family_index(Head head);  // -1 for a head outside kFamilyHeads

struct Embedding { float h[kHidden]; };
struct Projection { Head head{}; float q[kPhi]; };

// cfg8's attention block is appended after the complete cfg8=0 model. The old Layout, constants,
// entry points and offsets above are unchanged. All matrices remain column-major.
inline constexpr int kAttentionHidden = 4, kAttentionLayoutVersion = 2;
inline constexpr int kAttentionParameterCount = kCampContextPhi * kAttentionHidden + kAttentionHidden
    + kHidden * kAttentionHidden + kAttentionHidden + kAttentionHidden * kHidden;
inline constexpr int kAttentionWeightEnd = kWeightEnd + kAttentionParameterCount;
static_assert(kCampContextPhi == 12 && kAttentionParameterCount == 184);
static_assert(kAttentionWeightEnd <= kWeightCapacity);
struct AttentionLayout
{
    int begin, w_e, b_e, w_q, b_q, w_r, total;
};
const AttentionLayout& attention_layout();
struct AttentionDiagnostics
{
    std::size_t entities{};
    float maximum_score{}, denominator{}, pool[kAttentionHidden]{};
};

void embed(const float* weights, const float* state, Embedding& out);
// One attention pool over the complete ordered static-group context, computed once per decision.
// query=base_h*Q+bq; z_i=softsign(phi_i*E+be); alpha=softmax(query dot z_i);
// out=softsign(base_pre_activation + sum_i(alpha_i*z_i)*R). Empty context exactly returns embed().
// Context remains borrowed only through this call. No SDK, Paths or policy selection is performed.
// False denotes nonfinite derived scores/output; caller owns turn-boundary error handling.
bool embed_attention(const float* weights, const float* state, std::span<const CampContext> context,
                     Embedding& out, AttentionDiagnostics* diagnostics = nullptr);
void command_logits(const float* weights, const Embedding& state, float* out);  // kCommands outputs
// Project once for a selected family, then reuse for every raw phi under the same embedding.
// An unsupported head returns false and zeros q; callers must treat it as an interface error.
bool project(const float* weights, const Embedding& state, Head head, Projection& out);
float score(const Projection& head, const float* phi);  // exactly kPhi facts; no candidate allocation
float value(const float* weights, const Embedding& state);

}  // namespace zero::student::net
