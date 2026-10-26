#pragma once
#include <cambc.h>
#include <optional>

// Per-champion probe state (each champion runs its own instance).
class Bot {
    int phase = -1;          // 0 gold farm, 1 shop, 2 target, 3 tribute, 4 done
    int loops_done = 0;
    int buys_done = 0;
    long last_gold = -1;
    long last_gems = -1;
    float last_hp_end = -1.0f;
    bool healing = false;
    std::optional<cambc::map::camp_group> target_group;
    std::optional<cambc::map::camp_group> gold_group;

    void log_status(int round);
    bool fight(const cambc::map::camp_group& group, int round, bool may_capture);
    void shop(int round);
    void tribute(int round);
    bool retreat_and_recall(const cambc::map::camp_group& group, int round);

public:
    void play_turn();
};
