// Envelope-only test. This fake clock is never linked into a gameplay source.
#include <cstdint>
#include <string>
namespace cambc {
using handle_champion_t = unsigned;
enum class champion_type { vesper_vale };
inline unsigned round = 0;
inline unsigned fuel_remaining() { return 100000; }
inline unsigned round_number() { return round; }
inline const char* to_string(champion_type) { return "vesper_vale"; }
struct Actor {
    champion_type champion_name() const { return champion_type::vesper_vale; }
    operator handle_champion_t() const { return 99; }
};
inline Actor self() { return {}; }
}
#include "../src/profile.h"
#include "../src/telemetry.h"
int main() {
    Telemetry telemetry;
    for (cambc::round = 1; cambc::round <= 2000; ++cambc::round) {
        telemetry.tick();
        for (unsigned i = 0; i < 6; ++i) telemetry.ward_count(i);
        if (cambc::round == 1) telemetry.event("activation", "ward_attempt",
            "\"tile\":99999,\"newInterest\":99999,\"evaluatedPlacements\":8,\"scanTruncated\":true,"
            "\"fuelAtEntry\":99999,\"fuelAfterRoutes\":99999,\"fuelAfterPlacementScan\":99999,"
            "\"oldInterest\":99999,\"lostInterest\":99999,\"cooldownBefore\":99999,\"enabled\":false", true);
        telemetry.event("opportunity", "bounded_detail", "\"scalar\":123", true);
    }
}
