// Probe: camp capture (timeline, split, contest, interruption, respawn). Scenario = map width - 30.
// Draft order (ids 0,1,2 for team A, 3,4,5 for team B): ryth, stellar, vendetta.
#include "script.h"

using namespace cambc;
using namespace probe;

static int scenario() { return map::width() - 30; }

static std::vector<Cmd> script(champion_type who) {
    const int s = scenario();
    using enum Op;
    if (s == 0) {
        // G1 gold (5,9); G2 gold (6,3) + gem (7,3); G4 wolf (5,15) + gold (6,15); G5 gem (10,9).
        if (side() == 0) {
            switch (who) {
            case champion_type::ryth: // capture G1 and stay on it through the respawn timer, leave at 70
                return {{1, GROUPLOG, 5, 9}, {1, GROUPLOG, 5, 15}, {1, CAPTURE, 5, 9}, {70, GOTO, 4, 9}, {85, RESIGN}};
            case champion_type::stellar: return {{1, GROUPLOG, 6, 3}, {1, CAPTURE, 6, 3}, {40, GOTO, 6, 5}};
            case champion_type::vendetta: return {{1, CAPTURE, 7, 3}, {40, GOTO, 7, 5}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::ryth: // capture G1 but step off at 10 and back at 11; then clear G5 and return late
            return {{1, GROUPLOG, 5, 9}, {1, GROUPLOG, 10, 9}, {1, CAPTURE, 5, 9}, {10, GOTO, 4, 9}, {11, CAPTURE, 5, 9},
                    {25, CAPTURE, 10, 9}, {30, GOTO, 12, 12}, {55, CAPTURE, 10, 9}};
        case champion_type::stellar: return {{1, GROUPLOG, 6, 3}, {1, CAPTURE, 6, 3}};
        case champion_type::vendetta: // kills its tile, steps off, comes back late
            return {{1, CAPTURE, 7, 3}, {9, GOTO, 7, 1}, {12, CAPTURE, 7, 3}};
        default: return {};
        }
    }
    if (s == 1) {
        // X gold (14,4),(15,4),(16,4) and Y gold (15,9) lie on the symmetry axis; Z gem (5,15) is mirrored.
        if (side() == 0) {
            switch (who) {
            case champion_type::stellar: // clear X alone, then capture it from (14,4)
                return {{1, GROUPLOG, 15, 4}, {1, GOTO, 15, 6}, {15, KILL, 14, 4}, {19, KILL, 15, 4}, {23, KILL, 16, 4},
                        {28, CAPTURE, 14, 4}, {70, RESIGN}};
            case champion_type::ryth: return {{1, GROUPLOG, 15, 9}, {1, CAPTURE, 15, 9}}; // B's ryth waits adjacent
            case champion_type::vendetta: // Z: kill, leave, come back much later
                return {{1, GROUPLOG, 5, 15}, {1, KILL, 5, 15}, {12, GOTO, 2, 17}, {45, CAPTURE, 5, 15}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::vendetta: // step onto X while A's timer runs, leave after 3 rounds
            return {{1, GOTO, 14, 6}, {33, CAPTURE, 14, 4}, {36, GOTO, 14, 7}};
        case champion_type::ryth: return {{1, GOTO, 14, 9}}; // adjacent to Y (absolute 16,9), never on it
        case champion_type::stellar: // Z mirrored: kill, leave, come back much later
            return {{1, GROUPLOG, 5, 15}, {1, KILL, 5, 15}, {12, GOTO, 2, 17}, {45, CAPTURE, 5, 15}};
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
        runner->log_radius = 4;
        if (round_number() <= 1) {
            for (auto& g : map::camp_groups())
                PLOG("group first@%d,%d members=%zu gold=%u gems=%u max_respawn=%d", g.camps()[0].spawn_position().x,
                     g.camps()[0].spawn_position().y, g.camps().size(), g.gold_value(), g.gem_value(), g.max_respawn_timer());
        }
    }
    runner->turn();
}

int main() {
    while (true) {
        try {
            if (is_draft()) draft_list({champion_draft::ryth, champion_draft::stellar, champion_draft::vendetta,
                                        champion_draft::varg, champion_draft::tavi});
            else play_turn();
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
    }
}
