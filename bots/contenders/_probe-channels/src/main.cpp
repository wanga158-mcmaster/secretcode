// Probe: channels (recall, tribute, teleport crystal, altar locks) and death/respawn.
// Scenario = map width - 30. Draft order (ids 0,1,2 for team A, 3,4,5 for team B): ryth, stellar, vendetta.
#include "script.h"

using namespace cambc;
using namespace probe;

static int scenario() { return map::width() - 30; }

static std::vector<Cmd> script(champion_type who) {
    const int s = scenario();
    using enum Op;
    if (s == 0) {
        // Camps: G gem (4,5),(5,5); H gem (6,10) beside altar (6,11); F gold (4,16),(5,16); wolf (10,18).
        // Walls (11..12, 3..5), hole (13,4). Both teams run the same script on their own half.
        switch (who) {
        case champion_type::ryth: // recall: on spawn, under monster damage, in the open; actions while recalling; cancel rules
            return {{1, RECALL}, {2, CANCEL}, {3, GOTO, 9, 17}, {14, ATK, 10, 18}, {15, RECALL}, {16, TRYALL}, {17, RECALL},
                    {18, GOTO, 5, 14}, {23, RECALL}, {24, TRYALL}, {35, RECALL}, {35, CANCEL}, {35, RECALL}, {35, CANCEL},
                    {36, RECALL}, {37, CANCEL}, {37, CANCEL}, {60, RESIGN}};
        case champion_type::stellar: // tribute: 14 gems at start, +7 captured during the channel
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 6, 9}, {26, CAPTURE, 6, 10}, {30, TRIB, 6, 11},
                    {31, TRYALL}};
        case champion_type::vendetta: // clear H, take F's gold, buy a crystal, test landing rules
            return {{1, KILL, 6, 10}, {7, KILL, 5, 16}, {11, CAPTURE, 4, 16}, {24, GOTO, 1, 11}, {30, BUYC, 0, 0, 0},
                    {31, GOTO, 10, 4}, {45, USEC, 11, 4, 0}, {45, USEC, 13, 4, 0}, {45, USEC, 14, 4, 0}, {45, USEC, 13, 7, 0},
                    {45, USEC, 13, 5, 0}, {46, TRYALL}};
        default: return {};
        }
    }
    if (s == 1) {
        // Central altar C (15,11) on the symmetry axis. G gem (4,5),(5,5); F gold (4,16),(5,16); H gem (8,16).
        if (side() == 0) {
            switch (who) {
            case champion_type::stellar: // tribute at C; B locks it mid-channel; retries convert to lock hits
                return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 14, 11}, {34, TRIB, 15, 11}, {38, TRIB, 15, 11},
                        {39, TRIB, 15, 11}, {40, TRIB, 15, 11}, {41, TRIB, 15, 11}, {80, TRIB, 15, 11}, {81, TRIB, 15, 11},
                        {120, RESIGN}};
            case champion_type::ryth: return {{1, GOTO, 14, 16}, {36, ATKN, 15, 16, 0, 0, true}, {60, STOP}};
            case champion_type::vendetta: return {{1, GOTO, 14, 17}, {36, ATKN, 15, 16, 0, 0, true}, {60, STOP}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::vendetta: // gold, buy a lock, lock C from a diagonal 2 away during A's channel
            return {{1, KILL, 5, 16}, {6, CAPTURE, 4, 16}, {15, GOTO, 1, 11}, {21, BUYC, 0, 0, 1}, {22, GOTO, 17, 13, 0, 0, true},
                    {35, USEC, 15, 11, 1, 0, true}, {36, GOTO, 15, 13, 0, 0, true}, {38, USEC, 15, 11, 1, 0, true},
                    {45, ATK, 15, 11, 0, 0, true}};
        case champion_type::ryth: // gold, gems from H, then stand in the centre to be killed; act while dead
            return {{1, GOTO, 6, 16}, {6, CAPTURE, 5, 16}, {15, CAPTURE, 8, 16}, {28, GOTO, 15, 16, 0, 0, true},
                    {50, TRYALL}, {60, TRYALL}, {104, TRYALL}};
        case champion_type::stellar: // gems, then tribute at C through her own team's lock
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 14, 11}, {37, TRIB, 15, 11, 0, 0, true}};
        default: return {};
        }
    }
    if (s == 2) {
        // Altar (7,11), tribute tile (8,11) 3 from the elk (11,13). G gem (4,5),(5,5); F gold (4,16),(5,16); H gem (8,16).
        switch (who) {
        case champion_type::stellar: // tribute while the elk hits her
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 8, 11}, {28, ATK, 11, 13}, {29, TRIB, 7, 11},
                    {32, GOTO, 2, 5}, {90, side() == 0 ? RESIGN : NOTE}};
        case champion_type::vendetta: // teleport crystal channel while the elk hits him
            return {{1, KILL, 5, 16}, {6, CAPTURE, 4, 16}, {15, GOTO, 1, 11}, {20, BUYC, 0, 0, 0}, {21, GOTO, 12, 15},
                    {35, USEC, 15, 16, 0}, {39, GOTO, 15, 18}};
        case champion_type::ryth:
            if (side() == 0) // gems from H, recall while B's ryth hits it, then die to the elk carrying the gems
                return {{1, CAPTURE, 8, 16}, {13, GOTO, 15, 20, 0, 0, true}, {40, RECALL}, {45, GOTO, 12, 14}};
            return {{1, GOTO, 15, 20}, {41, ATK, 15, 20, 0, 0, true}, {42, ATK, 15, 20, 0, 0, true}, {43, GOTO, 18, 20}};
        default: return {};
        }
    }
    if (s == 3) {
        // ch-damage layout at width 33: tribute under elk damage; die to the elk while carrying gems.
        switch (who) {
        case champion_type::stellar:
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 8, 11}, {28, ATK, 11, 13}, {29, TRIB, 7, 11},
                    {32, GOTO, 2, 5}, {80, side() == 0 ? RESIGN : NOTE}};
        case champion_type::ryth: return {{1, CAPTURE, 8, 16}, {34, GOTO, 12, 14}, {38, ATKN, 11, 13}};
        default: return {};
        }
    }
    if (s == 4) {
        // Same layout at width 34; ryth waits out of the elk's range so stellar is its only target while tributing.
        switch (who) {
        case champion_type::stellar:
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 8, 11}, {28, ATK, 11, 13}, {29, TRIB, 7, 11},
                    {32, GOTO, 2, 5}, {45, side() == 0 ? RESIGN : NOTE}};
        case champion_type::ryth: return {{1, CAPTURE, 8, 16}, {18, GOTO, 4, 19}};
        default: return {};
        }
    }
    if (s == 5) {
        // Width 35 (mirror x' = 34 - x). Central gem (17,6). F gold (4,16),(5,16). Altar (5,15) / B's (29,15).
        // Bush (8,13) / B's (26,13). Team B spawn x 32..34, y 10..12.
        if (side() == 0) {
            switch (who) {
            case champion_type::ryth: // stand on the centre group B captured, through its respawn time
                return {{1, GOTO, 16, 8, 0, 0, true}, {29, CAPTURE, 17, 6, 0, 0, true}, {90, GOTO, 16, 8, 0, 0, true},
                        {100, RESIGN}};
            case champion_type::stellar: return {{1, CAPTURE, 5, 16}};
            case champion_type::vendetta: // crystal landing rules near B's spawn, lock range on B's altar
                return {{1, KILL, 4, 16}, {6, CAPTURE, 4, 16}, {16, GOTO, 1, 11}, {22, BUYC, 0, 0, 0}, {22, BUYC, 0, 0, 1},
                        {23, GOTO, 29, 13, 0, 0, true},
                        {55, USEC, 32, 12, 0, 0, true}, {55, USEC, 29, 15, 0, 0, true}, {55, USEC, 29, 10, 0, 0, true},
                        {55, USEC, 26, 13, 0, 0, true}, {60, USEC, 29, 15, 1, 0, true}, {61, GOTO, 27, 14, 0, 0, true},
                        {63, USEC, 29, 15, 1, 0, true}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::stellar: // capture the centre group, leave, later hit A's lock from range 3
            return {{1, GOTO, 17, 8, 0, 0, true}, {18, KILL, 17, 6, 0, 0, true}, {22, CAPTURE, 17, 6, 0, 0, true},
                    {29, GOTO, 19, 8, 0, 0, true}, {40, GOTO, 26, 16, 0, 0, true}, {70, ATK, 29, 15, 0, 0, true}};
        case champion_type::ryth: return {{1, CAPTURE, 5, 16}, {16, GOTO, 29, 10, 0, 0, true}};
        case champion_type::vendetta: // a second lock on an already locked altar, then a direct hit on A's lock
            return {{1, KILL, 4, 16}, {6, CAPTURE, 4, 16}, {16, GOTO, 1, 11}, {22, BUYC, 0, 0, 1},
                    {23, GOTO, 28, 16, 0, 0, true}, {66, USEC, 29, 15, 1, 0, true}, {68, ATK, 29, 15, 0, 0, true}};
        default: return {};
        }
    }
    if (s == 7) {
        // Width 37 (mirror x' = 36 - x), central altar C (18,11). G gem (4,5),(5,5); F gold (4,16),(5,16).
        // B locks C, starts a tribute through its own lock; A then places its own lock on C mid-channel.
        if (side() == 0) {
            switch (who) {
            case champion_type::stellar: // direct ranged hits on B's lock from 3 tiles
                return {{1, GOTO, 15, 11, 0, 0, true}, {45, ATKLOCK, 18, 11, 0, 0, true}, {46, ATKLOCK, 18, 11, 0, 0, true},
                        {70, RESIGN}};
            case champion_type::vendetta: // gold, lock, place it on C during B's channel, then melee hits on B's lock
                return {{1, KILL, 4, 16}, {6, CAPTURE, 4, 16}, {16, GOTO, 1, 11}, {22, BUYC, 0, 0, 1},
                        {23, GOTO, 17, 13, 0, 0, true}, {43, USEC, 18, 11, 1, 0, true}, {44, GOTO, 17, 12, 0, 0, true},
                        {47, ATKLOCK, 18, 11, 0, 0, true}};
            case champion_type::ryth: return {{1, CAPTURE, 5, 16}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::stellar: // gems, tribute through own lock (cancelled?), retry against A's lock
            return {{1, KILL, 5, 5}, {9, CAPTURE, 4, 5}, {21, GOTO, 19, 11, 0, 0, true}, {42, TRIB, 18, 11, 0, 0, true},
                    {50, TRIB, 18, 11, 0, 0, true}};
        case champion_type::vendetta:
            return {{1, KILL, 4, 16}, {6, CAPTURE, 4, 16}, {16, GOTO, 1, 11}, {22, BUYC, 0, 0, 1},
                    {23, GOTO, 19, 13, 0, 0, true}, {40, USEC, 18, 11, 1, 0, true}};
        case champion_type::ryth: return {{1, CAPTURE, 5, 16}};
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
    }
    auto& me = self();
    if (me.is_alive() && (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())) {
        try { PLOG("channel remaining=%d", me.remaining_channel_time()); } catch (const std::exception& e) { PLOG("channel time error %s", e.what()); }
    }
    runner->turn();
    PLOG("team gems A/B: %u/%u", global_submitted_gems(side() == 0 ? team::ally : team::enemy),
         global_submitted_gems(side() == 0 ? team::enemy : team::ally));
    try {
        for (const auto& l : vision::altar_locks())
            PLOG("lock at %d,%d team=%s hits_remaining=%u", l.position().x, l.position().y, l.is_ally() ? "ally" : "enemy",
                 l.hits_remaining());
    } catch (const std::exception& e) { PLOG("locks error %s", e.what()); }
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
