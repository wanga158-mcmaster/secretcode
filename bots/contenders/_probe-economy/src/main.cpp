// Probe: economy mechanics (gold multipliers, splits, kill rewards, shards and emblems, shop rules).
// Scenario = map width - 40. Each scenario lists its draft per side and a script per champion.
#include "script.h"

using namespace cambc;
using namespace probe;

static int scenario() { return map::width() - 40; }

static std::vector<champion_draft> picks() {
    using D = champion_draft;
    switch (scenario()) {
    case 0: return side() == 0 ? std::vector<D>{D::ryth, D::vesper_vale, D::stellar} : std::vector<D>{D::vesper_vale, D::ryth, D::stellar};
    case 1: return side() == 0 ? std::vector<D>{D::vesper_vale, D::vendetta, D::marcella} : std::vector<D>{D::stellar, D::tavi, D::marcella};
    case 2: return {D::ryth, D::tavi, D::prism};
    case 3: return {D::ryth, D::stellar, D::vesper_vale};
    case 4: return {D::ryth, D::vesper_vale, D::marcella, D::stellar};
    case 5: return {D::tavi, D::ryth, D::stellar};
    default: return {D::ryth, D::vesper_vale, D::stellar};
    }
}

static std::vector<Cmd> script(champion_type who) {
    using enum Op;
    const int s = scenario();
    if (s == 0) {
        // econ-cap: M = gold(6,20) gem(7,20) gem(8,20) split three ways; S1 gold (6,14) by Vesper alone;
        // S2 gold (6,8) by Ryth alone; T = gold (12,14),(13,14) by Vesper alone.
        std::vector<Cmd> common{{1, FARM, 6, 20, 8, 20}, {1, WAITFARM}};
        switch (who) {
        case champion_type::ryth:
            common.insert(common.end(), {{1, GOTO, 6, 20}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, STATS},
                                         {1, FARM, 6, 8, 6, 8}, {1, WAITFARM}, {1, GOTO, 6, 8}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS}});
            break;
        case champion_type::vesper_vale:
            common.insert(common.end(), {{1, GOTO, 7, 20}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, STATS},
                                         {1, FARM, 6, 14, 6, 14}, {1, WAITFARM}, {1, GOTO, 6, 14}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS},
                                         {1, FARM, 12, 14, 13, 14}, {1, WAITFARM}, {1, GOTO, 12, 14}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS}});
            break;
        case champion_type::stellar:
            common.insert(common.end(), {{1, GOTO, 8, 20}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, STATS}, {1, GOTO, 9, 23}});
            break;
        default: break;
        }
        if (side() == 0 && who == champion_type::stellar) common.push_back({110, RESIGN});
        return common;
    }
    if (s == 1) {
        // econ-kill (width 41, mirror x' = 40 - x). Gem clusters (5,17), (5,11), (5,25); gold (5,22),(6,22);
        // golem (10,25); altar (8,14). B's Stellar and Tavi carry 7 gems each to (21,14) / (21,18) absolute.
        if (side() == 0) {
            switch (who) {
            case champion_type::vesper_vale:
                return {{1, GOTO, 18, 16}, {1, WAITAT}, {1, WAITUNIT, 21, 18}, {1, ATK, 21, 18}, {1, WAITUNIT, 21, 14},
                        {1, ATKN, 21, 14}, {1, WAITNOUNIT, 21, 14}, {1, STATS}, {140, RESIGN}};
            case champion_type::vendetta:
                return {{1, GOTO, 20, 18}, {1, WAITAT}, {1, WAITUNIT, 21, 18}, {1, ATKN, 21, 18}, {1, WAITNOUNIT, 21, 18}, {1, STATS}};
            case champion_type::marcella:
                return {{1, FARM, 5, 17, 5, 17}, {1, WAITFARM}, {1, GOTO, 5, 17}, {1, WAITAT}, {1, WAITGEMGAIN},
                        {1, FARM, 5, 11, 5, 11}, {1, WAITFARM}, {1, GOTO, 5, 11}, {1, WAITAT}, {1, WAITGEMGAIN},
                        {1, GOTO, 7, 14}, {1, WAITAT}, {1, STATS}, {1, TRIB, 8, 14}, {1, WAITGAIN}, {1, STATS}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::stellar:
            return {{1, FARM, 5, 17, 5, 17}, {1, WAITFARM}, {1, GOTO, 5, 17}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, GOTO, 19, 14}};
        case champion_type::tavi:
            return {{1, FARM, 5, 11, 5, 11}, {1, WAITFARM}, {1, GOTO, 5, 11}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, GOTO, 19, 18}};
        case champion_type::marcella:
            return {{1, FARM, 5, 22, 6, 22}, {1, WAITFARM}, {1, GOTO, 5, 22}, {1, WAITAT}, {1, WAITGAIN},
                    {1, FARM, 5, 25, 5, 25}, {1, WAITFARM}, {1, GOTO, 5, 25}, {1, WAITAT}, {1, WAITGEMGAIN},
                    {1, GOTO, 9, 24}, {1, WAITAT}, {1, ATK, 10, 25}, {1, WAITDEAD}, {1, STATS},
                    {1, BUY, 0, 0, 1}, {1, STATS}, {1, WAITALIVE}, {1, STATS}};
        default: return {};
        }
    }
    if (s == 2) {
        // econ-shop (width 42). Gold lines L1 y17, L2 y11, L3 y20, L4 y8 (x 3..12, 200 gold each), L5 y23 (x 3..7,
        // 100 gold); gem (16,14); altar (16,17); wolf (17,4); elk (17,26). Both teams run the same script.
        switch (who) {
        case champion_type::ryth:
            return {{1, FARM, 3, 17, 12, 17}, {1, WAITFARM}, {1, GOTO, 7, 17}, {1, WAITAT}, {1, WAITGAIN},
                    {1, FARM, 3, 20, 12, 20}, {1, WAITFARM}, {1, GOTO, 7, 20}, {1, WAITAT}, {1, WAITGAIN},
                    {1, GOTO, 16, 4}, {1, WAITAT}, {1, ATK, 17, 4}, {1, HOLD, 0, 0, 3}, {1, GOTO, 0, 15}, {1, WAITAT},
                    {1, STATS}, {1, BUY, 0, 0, 16, 1}, {1, BUY, 0, 0, 12, 1}, {1, BUY, 0, 0, 14, 1}, {1, BUY, 0, 0, 10, 5},
                    {1, STATS}};
        case champion_type::tavi:
            return {{1, FARM, 3, 11, 12, 11}, {1, WAITFARM}, {1, GOTO, 7, 11}, {1, WAITAT}, {1, WAITGAIN},
                    {1, FARM, 3, 8, 12, 8}, {1, WAITFARM}, {1, GOTO, 7, 8}, {1, WAITAT}, {1, WAITGAIN},
                    {1, GOTO, 14, 4}, {1, WAITAT}, {1, WAITTURN}, {1, ATK, 17, 4}, {1, HOLD, 0, 0, 2}, {1, GOTO, 11, 4}, {1, WAITAT},
                    {1, STATS}, {1, BUY, 0, 0, 22, 1}, {1, BUY, 0, 0, 18, 2}, {1, BUY, 0, 0, 20, 1}, {1, BUYC, 0, 0, 1},
                    {1, GOTO, 1, 15}, {1, WAITAT}, {1, STATS}, {1, BUY, 0, 0, 22, 1}, {1, BUYC, 0, 0, 1}, {1, BUYC, 0, 0, 1},
                    {1, SELL, 0, 0, 18, 1}, {1, SELL, 0, 0, 18, 1}, {1, SELL, 0, 0, 20, 1}, {1, WAITTURN}, {1, SELL, 0, 0, 18, 1},
                    {1, BUY, 0, 0, 36, 5}, {1, STATS}};
        case champion_type::prism:
            return {{1, FARM, 3, 23, 7, 23}, {1, WAITFARM}, {1, GOTO, 5, 23}, {1, WAITAT}, {1, WAITGAIN},
                    {1, GOTO, 2, 15}, {1, WAITAT}, {1, BUY, 0, 0, 28, 1}, {1, BUY, 0, 0, 24, 1}, {1, BUY, 0, 0, 26, 1}, {1, STATS},
                    {1, FARM, 16, 14, 16, 14}, {1, WAITFARM}, {1, GOTO, 16, 14}, {1, WAITAT}, {1, WAITGEMGAIN}, {1, STATS},
                    {1, GOTO, 16, 16}, {1, WAITAT}, {1, TRIB, 16, 17}, {1, HOLD, 0, 0, 3}, {1, STATS},
                    {1, GOTO, 15, 24}, {1, WAITAT}, {1, WAITTURN}, {1, ATK, 17, 26}, {1, WAITDEAD}, {1, BUYC, 0, 0, 0}, {1, STATS},
                    {1, WAITALIVE}, {1, STATS}, {1, WAITTURN}, {1, WAITTURN}, {1, RESIGN}};
        default: return {};
        }
    }
    if (s == 3) {
        // econ-emblem (width 43, mirror x' = 42 - x). Gold lines y17, y11, y20, y8, y23 (x 3..12), y26 (x 3..7);
        // wolf W1 (17,4) for Ryth's damage-taken tests, wolf W2 (17,14) for Stellar's Monster tests.
        const bool a = side() == 0;
        switch (who) {
        case champion_type::ryth: {
            std::vector<Cmd> v{{1, FARM, 3, 17, 12, 17}, {1, WAITFARM}, {1, GOTO, 7, 17}, {1, WAITAT}, {1, WAITGAIN},
                               {1, FARM, 3, 20, 12, 20}, {1, WAITFARM}, {1, GOTO, 7, 20}, {1, WAITAT}, {1, WAITGAIN},
                               {1, FARM, 3, 23, 12, 23}, {1, WAITFARM}, {1, GOTO, 7, 23}, {1, WAITAT}, {1, WAITGAIN}};
            const int buys[] = {28, 16, 28, -1}; // jungle(health), durability(health), jungle again, none
            for (int b : buys) {
                v.insert(v.end(), {{1, GOTO, 16, 4}, {1, WAITAT}, {1, NOTE, 0, 0, 100 + b}, {1, ATK, 17, 4}, {1, HOLD, 0, 0, 3},
                                   {1, GOTO, 0, 15}, {1, WAITAT}, {1, STATS}});
                if (b >= 0) v.insert(v.end(), {{1, BUY, 0, 0, b, 5}, {1, STATS}});
            }
            if (a) v.insert(v.end(), {{1, GOTO, 20, 24}, {1, WAITAT}, {1, WAITUNIT, 21, 24}, {1, WAITTURN}, {1, WAITTURN}, {1, STATS}});
            else v.insert(v.end(), {{1, GOTO, 21, 24}, {1, WAITAT}, {1, WAITUNIT, 22, 24}, {1, ATK, 22, 24}, {1, STATS}});
            return v;
        }
        case champion_type::stellar: {
            std::vector<Cmd> v{{1, FARM, 3, 11, 12, 11}, {1, WAITFARM}, {1, GOTO, 7, 11}, {1, WAITAT}, {1, WAITGAIN}};
            for (int round = 0; round < 2; ++round) {
                v.insert(v.end(), {{1, GOTO, 14, 14}, {1, WAITAT}, {1, WAITTURN}, {1, NOTE, 0, 0, 200 + round}, {1, ATK, 17, 14},
                                   {1, WAITTURN}, {1, CAST, 17, 14, 0, 0}, {1, GOTO, 2, 15}, {1, WAITAT}, {1, STATS}});
                if (round == 0) v.insert(v.end(), {{1, BUY, 0, 0, 4, 5}, {1, STATS}});
            }
            if (a) v.insert(v.end(), {{1, GOTO, 20, 20}, {1, WAITAT}, {1, NOTE, 0, 0, 299}});
            return v;
        }
        case champion_type::vesper_vale: {
            std::vector<Cmd> v{{1, FARM, 3, 8, 12, 8}, {1, WAITFARM}, {1, GOTO, 7, 8}, {1, WAITAT}, {1, WAITGAIN},
                               {1, GOTO, 1, 15}, {1, WAITAT}, {1, BUY, 0, 0, 22, 5}, {1, STATS},
                               {1, FARM, 3, 26, 7, 26}, {1, WAITFARM}, {1, GOTO, 5, 26}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS},
                               {1, GOTO, 1, 15}, {1, WAITAT}, {1, BUY, 0, 0, 34, 5}, {1, STATS},
                               {1, GOTO, 9, 2}, {1, WAITAT}, {1, NOTE, 0, 0, 300}, {1, RECALL}, {1, WAITAT}};
            if (!a) v.insert(v.end(), {{1, GOTO, 19, 20}, {1, WAITAT}, {1, WAITUNIT, 22, 20}, {1, WAITTURN}, {1, ATKN, 22, 20},
                                       {1, WAITNOUNIT, 22, 20}, {1, STATS}, {1, WAITTURN}, {1, WAITTURN}, {1, RESIGN}});
            return v;
        }
        default: return {};
        }
    }
    if (s == 4) {
        // econ-pass (width 44, height 40, mirror x' = 43 - x; spawn x 0..2, y 18..20).
        // Ryth: lines y31, y34, y37 (x 3..12), wolf (17,34). Vesper: single golds (4..16 step 2, 4).
        // Marcella: gold line y10 (x 3..12), gems (5,14), (8,14), altar (11,16). Stellar: victim.
        const bool a = side() == 0;
        switch (who) {
        case champion_type::ryth: {
            std::vector<Cmd> v{{1, FARM, 3, 34, 12, 34}, {1, WAITFARM}, {1, GOTO, 7, 34}, {1, WAITAT}, {1, WAITGAIN},
                               {1, FARM, 3, 37, 12, 37}, {1, WAITFARM}, {1, GOTO, 7, 37}, {1, WAITAT}, {1, WAITGAIN},
                               {1, FARM, 3, 31, 12, 31}, {1, WAITFARM}, {1, GOTO, 7, 31}, {1, WAITAT}, {1, WAITGAIN}};
            const int buys[] = {28, 16, 28, -1}; // jungle(health), durability(health), jungle again, none
            for (int b : buys) {
                v.insert(v.end(), {{1, GOTO, 16, 34}, {1, WAITAT}, {1, NOTE, 0, 0, 100 + b}, {1, ATK, 17, 34}, {1, HOLD, 0, 0, 3},
                                   {1, GOTO, 0, 20}, {1, WAITAT}, {1, STATS}});
                if (b >= 0) v.insert(v.end(), {{1, BUY, 0, 0, b, 5}, {1, STATS}});
            }
            if (a) v.insert(v.end(), {{1, GOTO, 21, 26}, {1, WAITAT}, {1, WAITUNIT, 22, 26}, {1, WAITTURN}, {1, WAITTURN},
                                      {1, STATS}, {1, WAITTURN}, {1, RESIGN}});
            else v.insert(v.end(), {{1, GOTO, 21, 26}, {1, WAITAT}, {1, WAITUNIT, 22, 26}, {1, ATK, 22, 26}, {1, STATS}});
            return v;
        }
        case champion_type::vesper_vale: {
            std::vector<Cmd> v;
            for (int x = 4; x <= 14; x += 2)
                v.insert(v.end(), {{1, FARM, x, 4, x, 4}, {1, WAITFARM}, {1, GOTO, x, 4}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS}});
            v.insert(v.end(), {{1, GOTO, 1, 20}, {1, WAITAT}, {1, BUY, 0, 0, 22, 5}, {1, STATS},
                               {1, FARM, 16, 4, 16, 4}, {1, WAITFARM}, {1, GOTO, 16, 4}, {1, WAITAT}, {1, WAITGAIN}, {1, STATS}});
            if (!a) v.insert(v.end(), {{1, GOTO, 19, 8}, {1, WAITAT}, {1, WAITUNIT, 22, 8}, {1, WAITTURN}, {1, ATKN, 22, 8},
                                       {1, WAITNOUNIT, 22, 8}, {1, STATS}});
            return v;
        }
        case champion_type::marcella: {
            std::vector<Cmd> v{{1, FARM, 3, 10, 12, 10}, {1, WAITFARM}, {1, GOTO, 7, 10}, {1, WAITAT}, {1, WAITGAIN},
                               {1, GOTO, 2, 20}, {1, WAITAT}, {1, BUY, 0, 0, 18, 5}, {1, STATS},
                               {1, FARM, 5, 14, 5, 14}, {1, WAITFARM}, {1, GOTO, 5, 14}, {1, WAITAT}, {1, WAITGEMGAIN},
                               {1, FARM, 8, 14, 8, 14}, {1, WAITFARM}, {1, GOTO, 8, 14}, {1, WAITAT}, {1, WAITGEMGAIN},
                               {1, GOTO, 11, 15}, {1, WAITAT}, {1, STATS}, {1, TRIB, 11, 16}, {1, WAITGAIN}, {1, STATS}};
            if (a) v.insert(v.end(), {{1, GOTO, 21, 12}, {1, WAITAT}, {1, WAITUNIT, 22, 12}, {1, ATKN, 22, 12},
                                      {1, WAITNOUNIT, 22, 12}, {1, STATS}});
            return v;
        }
        case champion_type::stellar:
            if (a) return {{120, GOTO, 21, 8}, {1, WAITAT}, {1, NOTE, 0, 0, 400}};
            return {{120, GOTO, 21, 12}, {1, WAITAT}, {1, NOTE, 0, 0, 401}};
        default: return {};
        }
    }
    if (s == 5) {
        // econ-sell (width 45, mirror x' = 44 - x). Tavi lines y11, y5; Ryth line y20 (x 3..12).
        const bool a = side() == 0;
        switch (who) {
        case champion_type::tavi: {
            std::vector<Cmd> v{{1, FARM, 3, 11, 12, 11}, {1, WAITFARM}, {1, GOTO, 7, 11}, {1, WAITAT}, {1, WAITGAIN},
                               {1, FARM, 3, 5, 12, 5}, {1, WAITFARM}, {1, GOTO, 7, 5}, {1, WAITAT}, {1, WAITGAIN},
                               {1, BUY, 0, 0, 18, 1}, {1, BUY, 0, 0, 20, 1}, {1, SELL, 0, 0, 18, 1},
                               {1, GOTO, 1, 15}, {1, WAITAT}, {1, SELL, 0, 0, 18, 1}, {1, SELL, 0, 0, 20, 1},
                               {1, BUY, 0, 0, 6, 5}, {1, STATS}, {1, SELL, 0, 0, 7, 1}, {1, STATS},
                               {1, BUY, 0, 0, 16, 5}, {1, STATS}};
            if (a) v.insert(v.end(), {{1, GOTO, 21, 8}, {1, WAITAT}, {1, WAITUNIT, 24, 8}, {1, WAITTURN}, {1, CAST, 24, 8, 1, 0}, {1, STATS}});
            else v.insert(v.end(), {{1, GOTO, 20, 8}, {1, WAITAT}, {1, WAITUNIT, 23, 8}, {1, WAITTURN}, {1, WAITTURN},
                                    {1, WAITTURN}, {1, CAST, 23, 8, 1, 0}, {1, STATS}});
            return v;
        }
        case champion_type::ryth: {
            std::vector<Cmd> v{{1, FARM, 3, 20, 12, 20}, {1, WAITFARM}, {1, GOTO, 7, 20}, {1, WAITAT}, {1, WAITGAIN},
                               {1, GOTO, 0, 15}, {1, WAITAT}, {1, BUY, 0, 0, 12, 1}, {1, SELL, 0, 0, 12, 1},
                               {1, BUY, 0, 0, 4, 5}, {1, STATS}};
            if (a) v.insert(v.end(), {{1, GOTO, 21, 24}, {1, WAITAT}, {1, WAITUNIT, 22, 24}, {1, WAITTURN}, {1, ATK, 22, 24},
                                      {1, STATS}, {1, WAITROUND, 0, 0, 170}, {1, RESIGN}});
            else v.insert(v.end(), {{1, GOTO, 22, 24}, {1, WAITAT}});
            return v;
        }
        default: return {};
        }
    }
    return {};
}

static Runner* runner = nullptr;

int main() {
    while (true) {
        try {
            if (is_draft()) draft_list(picks());
            else {
                if (!runner) { runner = new Runner{}; runner->cmds = script(self().champion_name()); }
                runner->turn();
                if (side() == 0 && round_number() >= 400) { PLOG("safety resign"); resign(); }
            }
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
    }
}
