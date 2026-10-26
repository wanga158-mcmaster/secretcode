// Probe: monster mechanics (targeting, wake/sleep, timing, range, damage). Scenario = map width - 30.
// Draft order (ids 0,1,2 for team A, 3,4,5 for team B): vesper, ryth, stellar.
#include "script.h"

using namespace cambc;
using namespace probe;

static int scenario() { return map::width() - 30; }

static std::vector<Cmd> script(champion_type who) {
    const int s = scenario();
    using enum Op;
    if (s == 0 || s == 1) {
        // W1 wolf (9,14); elk (9,4); group G (9,24),(9,25),(10,25); diagonal wolf D (10,23).
        const bool vesper_opens = s == 1;
        switch (who) {
        case champion_type::vesper_vale:
            return {{1, GOTO, 6, 14}, {20, vesper_opens ? ATK : NOTE, 9, 14}, {31, GOTO, 5, 14},
                    {40, GOTO, 6, 7}, {50, ATK, 9, 4}, {51, GOTO, 5, 8}, {53, GOTO, 5, 9}, {75, RESIGN}};
        case champion_type::stellar:
            return {{1, GOTO, 9, 11}, {20, vesper_opens ? NOTE : ATK, 9, 14}, {33, GOTO, 12, 11}, {37, GOTO, 13, 10}};
        case champion_type::ryth:
            return {{1, GOTO, 13, 14}, {22, GOTO, 11, 14}, {27, GOTO, 13, 14},
                    {40, GOTO, 8, 23}, {55, ATKN, 9, 24}, {60, STOP}, {60, GOTO, 4, 20}};
        default: return {};
        }
    }
    if (s == 2) {
        // Camps (team-A coordinates, mirrored): elk (10,14), wolf (9,4) beside bush (7,4), wolf (9,24), gem (4,19).
        if (side() == 0) {
            // (a) nearest metric: V Chebyshev 4 / Euclid 4, S Chebyshev 3 / Euclid 4.24, R wakes the elk and walks away.
            // (b) bush: V stands in a bush 2 from the wolf, S (3 away) wakes it.
            switch (who) {
            case champion_type::vesper_vale: return {{1, GOTO, 6, 14}, {30, GOTO, 7, 4}, {60, RESIGN}};
            case champion_type::stellar: return {{1, GOTO, 7, 11}, {30, GOTO, 9, 7}, {40, ATK, 9, 4}};
            case champion_type::ryth: return {{1, GOTO, 10, 15}, {20, ATK, 10, 14}, {21, GOTO, 10, 19}, {30, GOTO, 14, 22}};
            default: return {};
            }
        }
        // (c) tie with priority: S acts first in team B but has the higher id; both 3 from the wolf.
        // (d) gem cluster: R wakes it, then recalls beside it.
        switch (who) {
        case champion_type::vesper_vale: return {{1, GOTO, 6, 24}};
        case champion_type::stellar: return {{1, GOTO, 9, 21}, {3, PRIO, 0, 0, 5}, {20, ATK, 9, 24}};
        case champion_type::ryth: return {{1, GOTO, 5, 19}, {20, ATK, 4, 19}, {21, RECALL}};
        default: return {};
        }
    }
    if (s == 3) {
        // Range-4 spells against a range-3 wolf (9,14) and a range-4 elk (9,4) from Chebyshev 4.
        switch (who) {
        case champion_type::stellar: return {{1, GOTO, 5, 14}, {15, CAST, 9, 14, 0}, {23, CAST, 9, 14, 0}, {40, RESIGN}};
        case champion_type::ryth: return {{1, GOTO, 5, 4}, {15, CAST, 9, 4, 0, 1}};
        default: return {};
        }
    }
    return {};
}

static Runner* runner = nullptr;

static void play_turn() {
    if (!runner) {
        runner = new Runner{};
        runner->cmds = script(self().champion_name());
    }
    runner->turn();
}

int main() {
    while (true) {
        try {
            if (is_draft()) draft_list({champion_draft::vesper_vale, champion_draft::ryth, champion_draft::stellar,
                                        champion_draft::varg, champion_draft::tavi});
            else play_turn();
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
    }
}
