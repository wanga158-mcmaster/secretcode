#include "learn.h"
#include <algorithm>
#include <bit>

namespace mb::learn {

Params params{ { 'm', 'b', '.', 'l', 'e', 'a', 'r', 'n', '.', 'p', 'a', 'r', 'a', 'm', 's', '\0' }, kCount, 1, {},
               // weights BEGIN (written by bots/main/tools/train/bake.py; {} = all zero = the hand-written rules)
               { -0.219593003f, 0.214698002f, 0.159492001f, -0.490285009f, 0.400350004f, 0.279886991f, 0.0377840996f, 0.197718993f,
                 0.189950004f, 0.182279006f, 0.0601971f, -0.0562839992f, -0.127514005f, 0.120383002f, 0.245115995f, -0.130498007f,
                 0.100616001f, 0.305321991f, 0.160193995f, 0.361705005f, -0.278721988f, -0.00386985997f, -0.147294998f, -0.00869143009f,
                 -0.0611035004f, -0.265799999f, 0.0156852994f, 0.203645006f, 0.211687997f, -0.200809002f, -0.00207078992f, 0.114467002f,
                 0.0746363029f, 0.145396993f, -0.0151450997f, 0.110701002f, 0.157633007f, 0.123839997f, -0.211402997f, 0.0566614009f,
                 0.229023993f, -0.0421098992f, 0.0760539994f, -0.0569996014f, -0.0790141001f, 0.258343995f, 0.214284003f, -0.135684997f,
                 -0.0427575f, -0.105829999f, -0.441408992f, -0.189104006f, -0.171597004f, 0.0632603988f, -0.0640145019f, -0.0185077004f,
                 0.929062009f, 0.00423861993f, -0.0133183002f, 0.228421003f, -2.69886994f, -1.10940003f, -0.280227989f, 1.73065996f,
                 -0.476765007f, -2.16518998f, -0.210692003f, 0.547927976f, -1.32774997f, -1.08073997f, 0.107437f, 2.64092994f,
                 -2.23596001f, 0.65170902f, 1.05605996f, -1.78279996f, 0.634581983f, -0.100578003f, -1.34274006f, 0.799596012f,
                 -0.634648025f, -0.762628973f, 0.400041014f, 0.103096001f, -0.693647027f, 0.364564002f, -0.474705011f, -0.440427989f,
                 0.167704999f, 0.0857940018f, 0.0716049001f, 0.208059996f, -0.0118465004f, -0.0800713971f }
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
