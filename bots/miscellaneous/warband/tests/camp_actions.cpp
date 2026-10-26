// Exercise the production basic/reset scheduler with action allowance and death feedback.
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include "camp_actions.h"

struct Fixture {
    std::array<int, 2> hp{45, 90};
    bool attack_ready = true, cast_ready = true, refuse = false;
    int resets = 0, selections = 0;
    std::vector<int> hits;
    bool run() {
        return wb::camp_basic_sequence(
            [&]() { return attack_ready; },
            [&]() {
                ++selections;
                for (int i = 0; i < 2; ++i) if (hp[static_cast<unsigned>(i)] > 0) return i;
                return -1;
            },
            [&](int target) {
                assert(attack_ready && hp[static_cast<unsigned>(target)] > 0);
                if (refuse) return false;
                hits.push_back(target);
                hp[static_cast<unsigned>(target)] -= 45;
                attack_ready = false;
                return true;
            },
            [&]() {
                if (!cast_ready || (hp[0] <= 0 && hp[1] <= 0)) return false;
                ++resets;
                cast_ready = false;
                attack_ready = true;
                return true;
            });
    }
};

int main() {
    {
        // Preceding lethal spell: ordinary basic survives, but the single cast is already spent.
        Fixture f;
        f.hp[0] = 0;
        f.cast_ready = false;
        assert(f.run() && f.hits == std::vector<int>{1} && f.resets == 0 && f.hp[1] == 45);
    }
    {
        // A lethal first basic can reset and transfer its second basic to the surviving member.
        Fixture f;
        assert(f.run() && f.hits == (std::vector<int>{0, 1}) && f.resets == 1 && f.hp[1] == 45);
    }
    {
        // Surviving target remains best, so the reset may strike it again.
        Fixture f;
        f.hp[0] = 90;
        assert(f.run() && f.hits == (std::vector<int>{0, 0}) && f.resets == 1);
    }
    {
        Fixture f;
        f.hp = {0, 0};
        assert(!f.run() && f.hits.empty() && f.resets == 0);
    }
    {
        // Movement already consumed ranged attack: no selection, attack, or reset is attempted.
        Fixture f;
        f.attack_ready = false;
        assert(!f.run() && f.selections == 0 && f.resets == 0);
    }
    {
        Fixture f;
        f.refuse = true;
        assert(!f.run() && f.hits.empty() && f.resets == 0);
    }
    {
        // Completing the group must not spend a reset on a corpse.
        Fixture f;
        f.hp[1] = 0;
        assert(f.run() && f.hits == std::vector<int>{0} && f.resets == 0);
    }
    std::puts("camp action scheduler: 7 cases passed");
}
