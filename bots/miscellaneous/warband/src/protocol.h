#pragma once
// The pack protocol: the team's 8 shared bytes. Pure (no SDK calls) so it is unit-tested.
//
// All-zero bytes (the game's initial memory) decode as "nothing": indices and coordinates are
// stored plus one.
// byte 0  focus group + 1: the camp group the whole pack converges on (0: none)
// byte 1  anchor x + 1 (0: none)    byte 2  anchor y + 1  (centre of the flock's zone)
// byte 3  bits 0-2 mode, bits 3-5 focus enemy index + 1 (0: none), bits 6-7 sighting age
// byte 4  low 8 bits of the round the leader last wrote
// byte 5  availability: bit i set when ally i is with the pack (alive, not recalling or healing)
// byte 6  busy: bit i set while ally i works a task (clearing or capturing a camp of the zone)
// byte 7  sighting: enemy offset from the anchor, (dx + 8) << 4 | (dy + 8), each in [-8, 7]
//         (0: none; the offset (-8, -8) is clamped to (-8, -7) to keep 0 free).
#include <array>
#include <cstdint>

namespace wb {

enum class Mode : std::uint8_t {
    travel = 0,   // the flock harvests its zone while the anchor walks to the focus group
    gather = 1,   // members near the anchor converge on the focus group's edge
    clear = 2,    // the pack is attacking the focus group
    fight = 3,    // champion fight: focus the focus target
    retreat = 4,  // the pack falls back towards the anchor
};

struct PackMsg {
    int objective = -1;
    int ax = -1, ay = -1;
    Mode mode = Mode::travel;
    int focus = -1;
    int stamp = -1;
    std::uint8_t avail = 0;
    std::uint8_t busy = 0;
    int sdx = 0, sdy = 0;     // sighting offset from the anchor
    bool sighting = false;
    int sight_age = 3;

    static PackMsg decode(const std::array<std::uint8_t, 8>& b) {
        PackMsg m;
        m.objective = static_cast<int>(b[0]) - 1;
        m.ax = b[1] == 0 || b[2] == 0 ? -1 : static_cast<int>(b[1]) - 1;
        m.ay = b[1] == 0 || b[2] == 0 ? -1 : static_cast<int>(b[2]) - 1;
        m.mode = static_cast<Mode>(b[3] & 7u);
        if (static_cast<unsigned>(m.mode) > 4u) m.mode = Mode::travel;
        const int f = (b[3] >> 3) & 7;
        m.focus = f - 1;
        m.sight_age = (b[3] >> 6) & 3;
        m.stamp = b[4];
        m.avail = b[5];
        m.busy = b[6];
        m.sighting = b[7] != 0;
        if (m.sighting) {
            m.sdx = (b[7] >> 4) - 8;
            m.sdy = (b[7] & 15) - 8;
        }
        return m;
    }

    std::array<std::uint8_t, 8> encode() const {
        std::array<std::uint8_t, 8> b{};
        b[0] = objective < 0 || objective > 254 ? 0 : static_cast<std::uint8_t>(objective + 1);
        const bool anchor = ax >= 0 && ay >= 0 && ax <= 254 && ay <= 254;
        b[1] = anchor ? static_cast<std::uint8_t>(ax + 1) : 0;
        b[2] = anchor ? static_cast<std::uint8_t>(ay + 1) : 0;
        const int f = focus < 0 || focus > 6 ? 0 : focus + 1;
        const int age = sight_age < 0 ? 0 : (sight_age > 3 ? 3 : sight_age);
        b[3] = static_cast<std::uint8_t>((static_cast<unsigned>(mode) & 7u) | (static_cast<unsigned>(f) << 3)
            | (static_cast<unsigned>(age) << 6));
        b[4] = static_cast<std::uint8_t>(stamp < 0 ? 0 : stamp & 255);
        b[5] = avail;
        b[6] = busy;
        if (!sighting) {
            b[7] = 0;
        } else {
            int dx = sdx < -8 ? -8 : (sdx > 7 ? 7 : sdx);
            int dy = sdy < -8 ? -8 : (sdy > 7 ? 7 : sdy);
            if (dx == -8 && dy == -8) dy = -7;
            b[7] = static_cast<std::uint8_t>(((dx + 8) << 4) | (dy + 8));
        }
        return b;
    }

    bool has_anchor() const { return ax >= 0 && ay >= 0; }
    bool has_sighting() const { return sighting && sight_age < 3 && has_anchor(); }
    // The leader wrote during the current round (stamps are round & 255).
    bool fresh(int round) const { return stamp == (round & 255); }
    bool recent(int round, int rounds) const { return ((round - stamp) & 255) <= rounds; }
    static int popcount(std::uint8_t v) {
        int n = 0;
        for (; v; v &= static_cast<std::uint8_t>(v - 1)) ++n;
        return n;
    }
};

} // namespace wb
