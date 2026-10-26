// Probe: fuel cost of static map queries on the first turn.
#include <cambc.h>
#include <cstdio>
#include <vector>

int main() {
    while (true) {
        try {
            if (cambc::is_draft()) {
                const auto chosen = cambc::draft::current_draft(cambc::team::ally);
                cambc::draft::pick(static_cast<cambc::champion_draft>(chosen.size()));
            } else if (cambc::round_number() <= 1) {
                long f0 = (long)cambc::fuel_remaining();
                auto groups = cambc::map::camp_groups();
                long f1 = (long)cambc::fuel_remaining();
                int members = 0;
                for (auto& g : groups) members += (int)g.camps().size();
                long f2 = (long)cambc::fuel_remaining();
                cambc::bitboard b;
                for (auto& g : groups) for (auto& c : g.camps()) b.set(c.spawn_position());
                long f3 = (long)cambc::fuel_remaining();
                std::vector<short> v(cambc::map::width() * cambc::map::height(), -1);
                long f4 = (long)cambc::fuel_remaining();
                auto st = groups[0].camps()[0].base_stats();
                long f5 = (long)cambc::fuel_remaining();
                std::printf("groups %zu members %d: camp_groups %ld count %ld sets %ld vector %ld base_stats %ld (hp %.0f)\n",
                    groups.size(), members, f0 - f1, f1 - f2, f2 - f3, f3 - f4, f4 - f5, st.base_health);
            }
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        cambc::end_turn();
    }
}
