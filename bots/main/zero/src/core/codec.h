#pragma once
// Compact text codec for training logs (tools/train.py decodes it): each value in [-4, 4] -> two printable
// characters (94 x 94 levels, step ~0.0009); values outside are clamped. Cheap to emit (no printf per value).
#include <cstdint>

namespace zero::codec {

inline constexpr float kLo = -4.0f, kHi = 4.0f;
inline constexpr int kLevels = 94 * 94;

inline void put2(char* out, float v)
{
    if (v < kLo) v = kLo;
    if (v > kHi) v = kHi;
    int q = static_cast<int>((v - kLo) / (kHi - kLo) * static_cast<float>(kLevels - 1) + 0.5f);
    out[0] = static_cast<char>(33 + q / 94);
    out[1] = static_cast<char>(33 + q % 94);
}

inline float get2(const char* in)
{
    const int q = (in[0] - 33) * 94 + (in[1] - 33);
    return kLo + static_cast<float>(q) * (kHi - kLo) / static_cast<float>(kLevels - 1);
}

}  // namespace zero::codec
