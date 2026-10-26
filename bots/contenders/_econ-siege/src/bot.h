#pragma once
#include <cambc.h>
#include <optional>

// Per-champion siege state (each champion runs its own instance).
class Bot {
    enum Phase { INIT, FARM, SHOP, RALLY, FIGHT, RETREAT, RECALLING, DONE };
    Phase phase = INIT;
    int slot = -1;
    int my_wave = 0;        // last wave this champion joined
    float last_hp_end = -1.0f;
    long last_gold = -1, last_gems = -1;
    bool bought = false;
    std::optional<cambc::map::camp_group> target;
    std::optional<cambc::map::camp_group> farm;

    void status(int round);
    void farm_turn(int round);
    void rally_turn(int round);
    void fight_turn(int round);
    void retreat_turn(int round);
    void capture_turn(int round);
    void attack_turn(int round, bool allow_move);

public:
    void play_turn();
};
