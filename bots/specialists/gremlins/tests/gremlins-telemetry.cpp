#include <cstdint>
#include <string>
// Pure framing fixture: no engine, match, SDK action or gameplay state.
namespace cambc {
using handle_champion_t = unsigned;
unsigned fixture_round = 1;
unsigned fuel_remaining() { return 100000; }
unsigned round_number() { return fixture_round; }
struct FixtureSelf {
    unsigned champion_name() const { return 1; }
    operator handle_champion_t() const { return 7; }
};
FixtureSelf& self() { static FixtureSelf value; return value; }
std::string to_string(unsigned) { return "varg"; }
}
#include "../src/profile.h"
#include "../src/telemetry.h"
int main() {
    Telemetry telemetry;
    for (cambc::fixture_round = 1; cambc::fixture_round <= 2000; ++cambc::fixture_round) {
        if (cambc::fixture_round == 100) continue; // one observed owner-turn gap
        telemetry.tick();
        telemetry.event("observation", "bounded_fixture", "\"value\":1", cambc::fixture_round % 100 == 0);
    }
}
