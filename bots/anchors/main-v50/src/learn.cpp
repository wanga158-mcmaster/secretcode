#include "learn.h"
#include <algorithm>
#include <bit>

namespace mb::learn {

Params params{ { 'm', 'b', '.', 'l', 'e', 'a', 'r', 'n', '.', 'p', 'a', 'r', 'a', 'm', 's', '\0' }, kCount, 1, {},
               // weights BEGIN (written by bots/main/tools/train/bake.py; {} = all zero = the hand-written rules)
               {}
               // weights END
};

float dot(Block b, const float* x) { return dot(b, 0, x, kSize[b]); }

float dot(Block b, int from, const float* x, int count)
{
    float s = 0.0f;
    for (int i = 0; i < count; ++i) s += params.w[kOffset[b] + from + i] * x[i];
    return s;
}

float factor(float logit)
{
    // (1 + l/8)^8 ~ e^l without a libm call (exactly 1 at 0); a non-finite logit counts as 0.
    if (!(logit == logit)) return 1.0f;
    float y = 1.0f + std::clamp(logit, -3.0f, 3.0f) * 0.125f;
    y *= y;
    y *= y;
    y *= y;
    return y;
}

float draft_weight(int champion) { return champion >= 0 && champion < kSize[draft] ? params.w[kOffset[draft] + champion] : 0.0f; }

float shop_weight(int index) { return index >= 0 && index < kSize[shop] ? params.w[kOffset[shop] + index] : 0.0f; }

bool soft_yes(float logit, std::uint32_t key)
{
    if (!(logit > 0.0f)) return false;
    const float l = std::min(logit, 8.0f);
    // sigmoid(l) - 0.5 = 0.5 tanh(l / 2); tanh via its [3/3] Pade form on the clamped range.
    const float h = 0.5f * l, h2 = h * h;
    const float p = std::min(0.5f, 0.5f * h * (15.0f + h2) / (15.0f + 6.0f * h2));
    std::uint32_t z = key * 0x9E3779B1u;
    z ^= z >> 15;
    z *= 0x85EBCA6Bu;
    z ^= z >> 13;
    return static_cast<float>(z >> 8) * (1.0f / 16777216.0f) < p;
}

float log1p_fast(float x)
{
    // log2(v) = e + log2(m), m in [1, 2); log2(m) ~ (m - 1)(1.3466 - 0.3466(m - 1)).
    const float v = 1.0f + std::max(0.0f, x);
    const auto bits = std::bit_cast<std::uint32_t>(v);
    const int e = static_cast<int>((bits >> 23) & 0xff) - 127;
    const float m = std::bit_cast<float>((bits & 0x007fffffu) | 0x3f800000u) - 1.0f;
    return (static_cast<float>(e) + m * (1.3466f - 0.3466f * m)) * 0.6931472f;
}

} // namespace mb::learn
