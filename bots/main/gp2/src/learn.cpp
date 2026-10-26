#include "learn.h"
#include <algorithm>
#include <bit>

namespace mb::learn {

Params params{ { 'm', 'b', '.', 'l', 'e', 'a', 'r', 'n', '.', 'p', 'a', 'r', 'a', 'm', 's', '\0' }, kCount, 1, {},
               // weights BEGIN (written by bots/main/tools/train/bake.py; {} = all zero = the hand-written rules)
               { -0.553254008f, 0.641930997f, 0.454860985f, -0.0980266035f, 0.468465f, 0.222082004f, -0.182857007f, 0.0110422997f,
                 0.315113008f, 0.176387995f, 0.106861003f, -0.0993561968f, -0.131019995f, 0.333840996f, 0.101281002f, 0.0397620015f,
                 0.701274991f, 0.246496007f, 0.252770007f, 0.493638009f, -0.0473476984f, 0.087826401f, 0.0639004037f, -0.426218003f,
                 -0.283868015f, -0.176370993f, 0.076522097f, 0.119911999f, 0.505478978f, -0.0266018994f, 0.252144992f, 0.0787165016f,
                 0.492576003f, 0.418341994f, -0.233735994f, 0.0590982996f, 0.129254997f, 0.176229998f, -0.274572015f, 0.177985996f,
                 0.0799361989f, -0.0728992969f, 0.0853028968f, 0.234412998f, 0.059752699f, 0.000895995006f, 0.718982995f, -0.0259290002f,
                 -0.226817995f, -0.303364992f, -0.409107f, -0.327744991f, -0.431008995f, 0.342720002f, -0.0783896968f, -0.313214988f,
                 0.525524974f, -1.99720001f, -0.696542025f, 0.0656362027f, -6.11480999f, -0.550218999f, -0.515869975f, 0.542949021f,
                 -1.75864995f, -1.81218004f, -0.924961984f, 3.89029002f, -2.43526006f, -0.855678022f, -0.0337781012f, 2.38309002f,
                 -2.00330997f, 1.57216001f, 0.839052975f, -4.06803989f, -3.68944001f, -2.14573002f, -0.285959005f, 0.36541f,
                 -0.133359f, -1.35927999f, -0.88399601f, -0.0708134025f, 0.0890991986f, 0.589350998f, -0.119341999f, -0.361456007f,
                 0.0960908011f, -0.142783999f, -0.069402501f, -0.0367750004f, 0.388042003f, -0.114037f }
               // weights END
};

float dot(Block b, const float* x) { return dot(b, 0, x, kSize[b]); }

bool any(Block b)
{
    for (int i = 0; i < kSize[b]; ++i)
        if (params.w[kOffset[b] + i] != 0.0f) return true;
    return false;
}

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
