#include "../owner-builder/src/profile.h"
#include "../replicas/src/profile.h"
#include "../firebrigade/src/profile.h"
#include <cstdio>
#include <cstdlib>

using namespace p6;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL drafting: %s\n", message); std::exit(1); }
}
}
int main() {
    const std::array pools{owner_builder::roster, replicas::roster, firebrigade::roster};
    std::array<Champion, 15> seen{};
    int distinct = 0;
    for (const auto& pool : pools) {
        for (int i = 0; i < 5; ++i) {
            check(pool[i] != Champion::other, "every requested pick has a real selected champion");
            for (int j = 0; j < i; ++j)
                check(pool[i] != pool[j], "own draft cannot repeat a champion");
            bool known = false;
            for (int j = 0; j < distinct; ++j) if (seen[j] == pool[i]) known = true;
            if (!known) seen[distinct++] = pool[i];
        }
    }
    check(distinct >= 9, "selected designs broaden the champion pool");
    check(pools[0] != pools[1] && pools[1] != pools[2] && pools[0] != pools[2],
          "designs use different preferred pools");
    Actor responder;
    responder.team = Team::ally; responder.pick_ordinal = 2;
    responder.type = Champion::ren;
    check(firebrigade::responder(responder), "mobile reserve is assigned to selected Ren");
    responder.type = Champion::varg;
    check(!firebrigade::responder(responder), "old prescribed pick cannot impersonate selected role");
    Actor owner;
    owner.team = Team::ally; owner.pick_ordinal = 2; owner.type = Champion::vendetta;
    check(owner_builder::fixed_owner(owner, 2, 3), "third owner role follows chosen producer");
    std::printf("{\"suite\":\"drafting\",\"passed\":true,\"checks\":%d,\"distinctChampions\":%d}\n",
                checks, distinct);
}
