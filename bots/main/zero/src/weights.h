#pragma once
// The learned-parameters block: fastrun finds it by its magic and writes per-game weights before the bot starts
// (--games lines / --params-a); a submitted bot carries its trained weights in the initialiser (weights_data.h).
#include <cstdint>

namespace zero {

inline constexpr int kWeights = 32768;  // >= net::layout().total (checked at start)
inline constexpr int kConfig = 16;      // run-time settings carried in the block (see below)

struct Params
{
    char magic[16];
    std::uint32_t count;
    std::uint32_t version;
    std::uint32_t reserved[2];
    // config[0] temperature (0 = argmax), config[1] log sampling rate, config[2] seed salt, config[3] decision period;
    // config[4] diagnostics, config[5] explicit learned command controller (default off);
    // then the network weights.
    volatile float w[kConfig + kWeights];
};
extern Params params;

}  // namespace zero
