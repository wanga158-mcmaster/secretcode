// Probe: combat and ability semantics. Scenario = map width - 30. Team A acts, team B is mostly
// dummies; scripts are in each side's own coordinates (probe::own mirrors team B).
#include "script.h"

using namespace cambc;
using namespace probe;
using enum Op;
using cd = champion_draft;
using ct = champion_type;

static int scenario() { return map::width() - 30; }

static std::vector<cd> picks() {
    switch (scenario()) {
    case 0: return side() == 0 ? std::vector<cd>{cd::vesper_vale, cd::tavi, cd::willow, cd::ryth, cd::stellar}
                               : std::vector<cd>{cd::varg, cd::vendetta, cd::vesper_vale, cd::ryth, cd::stellar};
    case 1: return side() == 0 ? std::vector<cd>{cd::cassi_and_issac, cd::tavi, cd::ryth, cd::prism}
                               : std::vector<cd>{cd::vesper_vale, cd::ryth, cd::marcella, cd::willow};
    case 2: return side() == 0 ? std::vector<cd>{cd::heart_of_ruin, cd::vesper_vale, cd::vendetta, cd::ren}
                               : std::vector<cd>{cd::varg, cd::marcella, cd::ryth, cd::stellar, cd::willow};
    case 3: return side() == 0 ? std::vector<cd>{cd::varg, cd::stellar, cd::willow, cd::vendetta}
                               : std::vector<cd>{cd::ren, cd::vesper_vale, cd::ryth, cd::marcella};
    case 4: case 5: return side() == 0 ? std::vector<cd>{cd::willow, cd::vendetta, cd::ren, cd::marcella}
                                       : std::vector<cd>{cd::cassi_and_issac, cd::prism, cd::ryth, cd::vesper_vale};
    default: return {cd::vesper_vale, cd::ryth, cd::stellar, cd::varg, cd::tavi};
    }
}

static std::vector<Cmd> script(ct who) {
    const int s = scenario();
    if (s == 0) {
        // Dummies of team B stand at absolute (16,10) (16,12) (16,8) (16,15) (16,16).
        if (side() == 1) {
            switch (who) {
            case ct::varg: return {{1, GOTO, 13, 10}};
            case ct::vendetta: return {{1, GOTO, 13, 12}};
            case ct::vesper_vale: return {{1, GOTO, 13, 8}};
            case ct::ryth: return {{1, GOTO, 13, 15}};
            case ct::stellar: return {{1, GOTO, 13, 16}};
            default: return {};
            }
        }
        switch (who) {
        case ct::vesper_vale: // ranged allowance; reset before the first attack; dash after attack, attack after dash
            return {{1, GOTO, 13, 10}, {19, STOP},
                    {20, ATK, 16, 10}, {20, STEP, 0, 1},
                    {21, STEP, 0, 1}, {21, ATK, 16, 10},
                    {22, CASTSELF, 0, 0, 0}, {22, ATK, 16, 10}, {22, ATK, 16, 10}, {22, STEP, 0, -1},
                    {24, ATK, 16, 10}, {24, CAST, 11, 11, 1, 1}, {24, STEP, 0, -1},
                    {33, CAST, 13, 10, 1, 1}, {33, ATK, 16, 10}, {33, STEP, 0, 1},
                    {40, RESIGN}};
        case ct::tavi: // attack, reset, attack, move; damaging stun then attack
            return {{1, GOTO, 13, 12}, {19, STOP},
                    {20, ATK, 16, 12}, {20, CASTSELF, 0, 0, 0}, {20, ATK, 16, 12}, {20, STEP, 0, 1},
                    {21, QDMG, 16, 12, 1}, {21, CAST, 16, 12, 1, 0}, {21, ATK, 16, 12}, {21, STEP, 0, 1}};
        case ct::willow: // attack, reset, move, attack; heal self (illegal?), heal a full ally, heal a hurt ally
            return {{1, GOTO, 13, 8}, {19, STOP},
                    {20, ATK, 16, 8}, {20, CASTSELF, 0, 0, 1}, {20, STEP, 0, -1}, {20, ATK, 16, 8},
                    {21, CAST, -99, 0, 0, 0}, {21, QDMG, 13, 11, 0}, {21, CAST, 13, 11, 0, 0},
                    {23, GOTO, 12, 13}, {29, QDMG, 15, 15, 0}, {29, CAST, 15, 15, 0, 0}};
        case ct::ryth: // melee move+attack both orders; reset then second cast; area stun incl. own tile
            return {{1, GOTO, 14, 15}, {19, STOP},
                    {20, STEP, 1, 0}, {20, ATK, 16, 15},
                    {21, ATK, 16, 15}, {21, STEP, -1, 0},
                    {22, STEP, 1, 0}, {22, ATK, 16, 15}, {22, CASTSELF, 0, 0, 1}, {22, ATK, 16, 15}, {22, CAST, 16, 16, 0, 1},
                    {23, QDMG, 16, 15, 0}, {23, CAST, 16, 15, 0, 1}};
        case ct::stellar: // excludes-walking casts vs moves; attack around casts; area damage beside an ally
            return {{1, GOTO, 13, 16}, {19, STOP},
                    {20, STEP, 0, 1}, {20, CAST, 16, 16, 0, 0},
                    {21, QDMG, 16, 16, 0}, {21, CAST, 16, 16, 0, 0}, {21, STEP, 0, -1}, {21, ATK, 16, 16},
                    {22, ATK, 16, 16}, {22, QDMG, 16, 15, 1}, {22, CAST, 16, 15, 1, 1}};
        default: return {};
        }
    }
    if (s == 1) {
        // Stun semantics. Absolute layout (width 31, team B x = 30 - own x): Cassi (13,12) then (14,8);
        // B0 vesper oscillates (16,12)/(16,13); wolf (10,4) woken by Issac, stunned by Tavi from (10,8);
        // Marcella oscillates (16,9)/(16,10); Willow recall/teleport at (17,8), 1-tick tribute from (17,6);
        // B ryth 2-tick tribute (14 gems) from (17,5) at altar (18,5).
        if (side() == 1) {
            switch (who) {
            case ct::vesper_vale:
                return {{1, GOTO, 14, 12}, {19, OSC, 14, 12}, {21, WARD, 14, 14},
                        {31, WARD, 14, 14}, {31, PRIO, 0, 0, 2}, {31, SMW, 0, 0, 1, 9},
                        {44, WARD, 14, 14}, {44, PRIO, 0, 0, 3}, {44, SMR}, {72, WARD, 14, 14}, {72, SMR}};
            case ct::ryth:
                return {{1, GOTO, 6, 18}, {8, ATKN, 5, 18}, {14, ATKN, 5, 19}, {18, STOP}, {18, GOTO, 5, 18},
                        {26, GOTO, 13, 5}, {45, TRIB, 12, 5}};
            case ct::marcella: return {{1, GOTO, 14, 9}, {33, OSC, 14, 9}, {39, STOP}};
            case ct::willow:
                return {{1, GOTO, 3, 13}, {3, ATKN, 3, 15}, {7, STOP}, {7, GOTO, 3, 15}, {15, GOTO, 2, 12},
                        {18, BUYC, 0, 0, 0}, {19, GOTO, 13, 8}, {20, ATKN, 11, 9}, {23, STOP}, {23, GOTO, 11, 9},
                        {30, GOTO, 13, 8}, {36, RECALL}, {42, GOTO, 13, 8}, {53, USEC, 10, 8, 0}, {58, GOTO, 13, 6},
                        {62, TRIB, 12, 5}};
            default: return {};
            }
        }
        switch (who) {
        case ct::cassi:
            return {{1, GOTO, 13, 12}, {20, LOGV, 0, 0, 1}, {21, CAST, 16, 12, 1, 0}, {22, LOGV, 0, 0, 0},
                    {30, CAST, 16, 12, 1, 0}, {31, GOTO, 14, 8}, {38, CAST, 17, 8, 1, 0}, {46, CAST, 17, 5, 1, 0},
                    {54, CAST, 17, 8, 1, 0}, {63, CAST, 17, 6, 1, 0}, {65, GOTO, 13, 12}, {71, CAST, 16, 12, 1, 0},
                    {76, RESIGN}};
        case ct::issac: return {{1, GOTO, 10, 5}, {20, ATK, 10, 4}, {30, GOTO, 10, 10}};
        case ct::tavi:
            return {{1, GOTO, 10, 8}, {25, CAST, 10, 4, 1, 0}, {28, GOTO, 13, 10}, {35, CAST, 16, 9, 1, 0},
                    {43, CAST, 16, 12, 1, 0}};
        case ct::prism: return {{1, GOTO, 13, 14}, {43, CAST, 16, 12, 0, 0}, {71, CAST, 16, 12, 0, 0}};
        case ct::ryth: return {{1, GOTO, 5, 10}};
        default: return {};
        }
    }
    if (s == 2) {
        // Heart of Ruin and dash rules. Absolute layout (width 32, team B x = 31 - own x): Heart (8,20) dashes to
        // (10,20) at r30: inside radius 1 B varg (11,20), B marcella (11,21), A vendetta (9,21), wolf (11,19);
        // outside B ryth (7,20). B stellar (13,22) and willow (13,18) hit Heart r31-r37; second dash to (10,22) r38.
        // Ren at (5,4) tries illegal dashes (wall (6,3..5), range 3, hole (4,6), occupied (5,6)) then a legal one;
        // vesper at (5,6) dashes over the hole row (4..6,7) to (5,8).
        if (side() == 1) {
            switch (who) {
            case ct::varg: return {{1, GOTO, 20, 20}};
            case ct::marcella: return {{1, GOTO, 20, 21}};
            case ct::ryth: return {{1, GOTO, 24, 20}};
            case ct::stellar: return {{1, GOTO, 18, 22}, {31, ATKN, 21, 20}, {38, STOP}};
            case ct::willow: return {{1, GOTO, 18, 18}, {31, ATKN, 21, 20}, {38, STOP}};
            default: return {};
            }
        }
        switch (who) {
        case ct::heart_of_ruin:
            return {{1, GOTO, 8, 20}, {28, STOP}, {29, QDMG, 10, 20, 1}, {29, ATK, 7, 20}, {30, CAST, 10, 20, 1, 1},
                    {32, QDMG, 10, 20, 0}, {32, CASTSELF, 0, 0, 0}, {37, QDMG, 11, 21, 1}, {37, ATK, 11, 20},
                    {38, QDMG, 11, 21, 1}, {38, CAST, 10, 22, 1, 1}, {46, RESIGN}};
        case ct::vendetta: return {{1, GOTO, 9, 21}};
        case ct::ren:
            return {{1, GOTO, 5, 4}, {28, STOP}, {30, CAST, 7, 4, 1, 1}, {30, CAST, 8, 4, 1, 1}, {30, CAST, 4, 6, 1, 1},
                    {30, CAST, 5, 6, 1, 1}, {30, CAST, 3, 4, 1, 1}, {38, CAST, 5, 2, 1, 1}};
        case ct::vesper_vale: return {{1, GOTO, 5, 6}, {28, STOP}, {31, CAST, 5, 8, 1, 1}};
        default: return {};
        }
    }
    if (s == 3) {
        // Varg shield (after a Durability emblem), Varg splash, Ren retaliation, Vendetta reset on a champion kill.
        // Absolute layout (width 33, team B x = 32 - own x): gold block (4..7,4..5); Bd1 B ryth (12,18),
        // Bd2 B marcella (13,18), wolf (12,19), B vesper (14,16) shoots Varg r40-r45; Ren (13,15).
        if (side() == 1) {
            switch (who) {
            case ct::ren: return {{1, GOTO, 19, 15}};
            case ct::vesper_vale: return {{1, GOTO, 18, 16}, {40, ATKN, 21, 18}, {46, STOP}};
            case ct::ryth: return {{1, GOTO, 20, 18}};
            case ct::marcella: return {{1, GOTO, 19, 18}};
            default: return {};
            }
        }
        switch (who) {
        case ct::varg:
            return {{1, GOTO, 5, 6}, {5, FARM, 0, 0, 1}, {14, GOTO, 5, 5}, {22, FARM, 0, 0, 0}, {22, GOTO, 1, 11},
                    {27, LOGITEMS}, {27, BUY, 0, 0, 16, 5}, {27, LOGITEMS}, {28, GOTO, 11, 18}, {39, STOP},
                    {40, QDMG, 0, 0, 1}, {40, CASTSELF, 0, 0, 1}, {47, ATK, 12, 18}, {52, GOTO, 12, 16},
                    {54, ATKN, 13, 15, 0, 150}, {65, RESIGN}};
        case ct::stellar:
            return {{1, GOTO, 5, 8}, {5, FARM, 0, 0, 1}, {14, FARM, 0, 0, 0}, {15, GOTO, 10, 14}, {49, ATK, 13, 15},
                    {51, CAST, 13, 15, 1, 1}, {54, ATKN, 13, 15, 0, 150}};
        case ct::willow:
            return {{1, GOTO, 8, 7}, {5, FARM, 0, 0, 1}, {14, FARM, 0, 0, 0}, {15, GOTO, 11, 17}, {50, ATK, 13, 15},
                    {54, ATKN, 13, 15, 0, 150}};
        case ct::vendetta:
            return {{1, GOTO, 8, 5}, {5, FARM, 0, 0, 1}, {14, FARM, 0, 0, 0}, {15, GOTO, 12, 14}, {44, STOP},
                    {52, CAST, 14, 14, 1, 1}};
        default: return {};
        }
    }
    if (s == 4) {
        // Absolute layout (width 34, team B x = 33 - own x). Ren (8,5) fights wolf W1 (8,4) (retaliation vs a
        // monster); Willow heals Ren (capped) r22 and Vendetta (uncapped) r30; Vendetta + Marcella kill W2a (8,16) of
        // the 3-wolf column (8,16..18) (Vendetta kill reset); Marcella's enhanced attack on B ryth (4,22) r40; B prism
        // (7 gems) tributes at altar (11,6) from (12,6) at r44 and Ren stuns it after its turn; C&I killed at (12,22).
        if (side() == 1) {
            switch (who) {
            case ct::cassi: return {{1, GOTO, 6, 8}, {4, FARM, 0, 0, 1}, {10, FARM, 0, 0, 0}, {10, GOTO, 4, 8}, {20, GOTO, 21, 22}};
            case ct::issac: return {{1, GOTO, 5, 18}, {4, FARM, 0, 0, 1}, {10, FARM, 0, 0, 0}, {10, GOTO, 4, 18}, {20, GOTO, 19, 22}};
            case ct::prism: return {{1, GOTO, 6, 14}, {4, FARM, 0, 0, 1}, {12, FARM, 0, 0, 0}, {12, GOTO, 4, 14}, {22, GOTO, 21, 6},
                                    {44, TRIB, 22, 6}};
            case ct::ryth: return {{1, GOTO, 29, 22}};
            default: return {};
            }
        }
        switch (who) {
        case ct::ren: return {{1, GOTO, 8, 5}, {19, STOP}, {20, ATK, 8, 4}, {21, ATKN, 8, 4}, {44, CAST, 12, 6, 0, 0}};
        case ct::willow:
            return {{1, GOTO, 8, 8}, {22, QDMG, 8, 5, 0}, {22, CAST, 8, 5, 0, 0}, {23, GOTO, 10, 12}, {30, QDMG, 9, 16, 0},
                    {30, CAST, 9, 16, 0, 0}, {34, GOTO, 12, 19}, {50, ATKN, 12, 22}};
        case ct::vendetta: return {{1, GOTO, 9, 16}, {19, STOP}, {33, GOTO, 12, 12}, {40, GOTO, 13, 22}, {50, ATKN, 12, 22}};
        case ct::marcella:
            return {{1, GOTO, 7, 16}, {19, STOP}, {20, ATKN, 8, 16, 0, 160}, {33, STOP}, {33, GOTO, 5, 22}, {39, STOP},
                    {40, ATK, 4, 22}, {40, CASTSELF, 0, 0, 0}, {40, ATK, 4, 22}, {46, GOTO, 11, 22}, {50, ATKN, 12, 22},
                    {60, RESIGN}};
        default: return {};
        }
    }
    if (s == 5) {
        // Scenario 4 repeated without waking the B-side wolves (cassi/issac/prism hit only their gem cluster), so
        // Cassi & Issac reach the kill zone with 7 gems each; Ren logs the stun buff right after casting it.
        // Width 35: team B x = 34 - own x.
        if (side() == 1) {
            switch (who) {
            case ct::cassi: return {{1, GOTO, 6, 8}, {4, ATKN, 4, 8}, {10, STOP}, {10, GOTO, 4, 8}, {20, GOTO, 22, 22}};
            case ct::issac: return {{1, GOTO, 5, 18}, {4, ATKN, 4, 18}, {12, STOP}, {12, GOTO, 4, 18}, {20, GOTO, 20, 22}};
            case ct::prism: return {{1, GOTO, 6, 14}, {4, ATKN, 4, 14}, {12, STOP}, {12, GOTO, 4, 14}, {22, GOTO, 22, 6},
                                    {44, TRIB, 23, 6}};
            case ct::ryth: return {{1, GOTO, 30, 22}};
            default: return {};
            }
        }
        switch (who) {
        case ct::ren: return {{1, GOTO, 8, 5}, {19, STOP}, {20, ATK, 8, 4}, {21, ATKN, 8, 4}, {44, CAST, 12, 6, 0, 0}, {44, LOGT, 12, 6}};
        case ct::willow: return {{1, GOTO, 12, 19}, {50, ATKN, 12, 22}};
        case ct::vendetta: return {{1, GOTO, 13, 21}, {50, ATKN, 12, 22}};
        case ct::marcella: return {{1, GOTO, 11, 22}, {50, ATKN, 12, 22}, {62, RESIGN}};
        default: return {};
        }
    }
    return {};
}

// For a target at either tile of an oscillation pair, retarget the cast to the occupied tile.
static Runner* runner = nullptr;

static void play_turn() {
    if (!runner) {
        runner = new Runner{};
        runner->cmds = script(self().champion_name());
        if (scenario() == 3 && side() == 0 && self().champion_name() == ct::vendetta)
            runner->custom = [] {
                auto& me = self();
                if (round_number() < 54) return;
                const vec2 ren = own({13, 15});
                if (!vision::has_champion(ren)) return;
                if (me.can_attack()) Runner::attack_at(ren, "vkill");
                if (!me.can_attack() && me.abilities()[0].cooldown() == 0 && vision::has_champion(ren)) {
                    try { PLOG("vreset -> %s", me.abilities()[0].use() ? "true" : "false"); } catch (const std::exception& e) { PLOG("vreset error %s", e.what()); }
                    if (me.can_attack()) Runner::attack_at(ren, "vkill2");
                }
                auto a = me.abilities();
                PLOG("vendetta cd after %d,%d kills? gold=%u", a[0].cooldown(), a[1].cooldown(), me.gold_count());
            };
        if (scenario() == 4 && side() == 0 && self().champion_name() == ct::vendetta)
            runner->custom = [] {
                static bool dashed = false;
                auto& me = self();
                const int now = static_cast<int>(round_number());
                if (now < 20 || now >= 33) return;
                const vec2 w = own({8, 16});
                if (!vision::has_camp(w)) { PLOG("w2a gone; cd %d,%d", me.abilities()[0].cooldown(), me.abilities()[1].cooldown()); return; }
                const float hp = vision::camp_at(w).health();
                if (hp <= 420 && !dashed && me.abilities()[1].cooldown() == 0) {
                    dashed = true;
                    try { PLOG("vdash -> %s", me.abilities()[1].use(own({9, 15})) ? "true" : "false"); } catch (const std::exception& e) { PLOG("vdash error %s", e.what()); }
                }
                if (me.can_attack()) Runner::attack_at(w, "vhit");
                if (!me.can_attack() && hp <= 220 && me.abilities()[0].cooldown() == 0 && vision::has_camp(w)) {
                    try { PLOG("vreset -> %s", me.abilities()[0].use() ? "true" : "false"); } catch (const std::exception& e) { PLOG("vreset error %s", e.what()); }
                    if (me.can_attack() && vision::has_camp(w)) Runner::attack_at(w, "vhit2");
                }
                PLOG("vendetta cd %d,%d w2a=%s", me.abilities()[0].cooldown(), me.abilities()[1].cooldown(), vision::has_camp(w) ? "alive" : "dead");
            };
    }
    runner->turn();
}

int main() {
    while (true) {
        try {
            if (is_draft()) draft_list(picks());
            else play_turn();
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
    }
}
