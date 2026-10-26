#include "production.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures{};
void check(bool value, const char* name)
{
    if (!value) { std::printf("FAIL %s\n", name); ++failures; }
}

mb::production::Quote cost(float reward = 30.0f, int travel = 5, int back = 10,
                           int left = 2000, float fight_rounds = 20.0f,
                           float damage = 800.0f, float hp = 1000.0f,
                           float maximum = 1000.0f, float reserve = 200.0f)
{
    return mb::production::quote(reward, travel, back, 3, 5, 6, 4, 2, left,
                                 fight_rounds, damage, 1.0f, hp, maximum, reserve);
}
}

int main()
{
    using namespace mb::production;
    const WakeMember live[] = { {0, 0, 0, true}, {1, 1, 0, true}, {1, 5, 0, true}, {2, 4, 4, true} };
    const auto splash = collateral_wake_groups(live, 0, 1, 3);
    check(splash.size() == 3 && splash[1] && !splash[0] && !splash[2],
          "harmless member at radius1 wakes distant harmful sibling by whole group identity");
    const WakeMember dead[] = { {0, 0, 0, true}, {1, 1, 0, false}, {1, 5, 0, true} };
    check(!collateral_wake_groups(dead, 0, 1, 2)[1], "dead collateral member cannot wake distant sibling");
    check(!collateral_wake_groups(live, -1, 1, 3)[1] && !collateral_wake_groups(live, 0, 0, 3)[1],
          "ordinary evacuation and no splash do not invent a group wake");
    Episode episode;
    episode.begin(7, 10, 2400.0f, 12);
    check(episode.group == 7 && episode.phase == Phase::approach && episode.bags_before == 12,
          "entry has actual group identity and original bag");
    episode.observe_progress(12, 2200.0f);
    check(episode.phase == Phase::work && episode.last_progress_round == 12 && episode.best_hp == 2200.0f,
          "observed partial work advances progress");
    episode.observe_progress(13, 2200.0f);
    episode.observe_progress(14, 2400.0f);
    check(episode.last_progress_round == 12 && episode.best_hp == 2200.0f,
          "equal and increased HP are not progress");
    episode.recover(15);
    episode.recover(16);
    check(episode.group == 7 && episode.best_hp == 2200.0f && episode.recovery_round == 15 &&
              episode.recoveries == 1 && episode.phase == Phase::recovery,
          "repeated escape turns retain one recovery and partial work");
    episode.resume(30);
    check(episode.group == 7 && episode.phase == Phase::approach && episode.best_hp == 2200.0f &&
              episode.last_progress_round == 30 && episode.recovery_round == -1,
          "return retains job but renews progress clock");
    episode.observe_progress(33, 1000.0f);
    episode.recover(35);
    episode.resume(50);
    check(episode.group == 7 && episode.best_hp == 1000.0f && episode.recoveries == 2,
          "partial work survives two real recovery trips");
    episode.release();
    check(episode.group == -1 && episode.phase == Phase::idle && episode.started_round == -1 &&
              episode.last_progress_round == -1 && episode.recovery_round == -1 &&
              episode.best_hp == -1.0f && episode.bags_before == 0 && episode.recoveries == 0,
          "release clears old episode identity");
    episode.observe_progress(60, 0.0f);
    episode.recover(60);
    episode.resume(60);
    check(episode.group == -1 && episode.phase == Phase::idle, "released job cannot resume itself");
    episode.begin(3, 70, -1.0f, 0);
    episode.observe_progress(80, 500.0f);
    check(episode.best_hp == 500.0f && episode.last_progress_round == 70,
          "first unknown HP observation establishes a value without fabricated progress");
    episode.observe_progress(81, std::numeric_limits<float>::quiet_NaN());
    check(episode.best_hp == 500.0f && episode.last_progress_round == 70, "invalid HP cannot advance work");

    const auto fresh = cost();
    check(fresh.feasible && fresh.trips == 1 && fresh.rounds == 37 && fresh.incoming_damage == 800.0f,
          "fresh one-trip work includes travel capture and conversion");
    const auto depleted = cost(30, 5, 10, 2000, 20, 800, 200);
    check(depleted.feasible && depleted.trips == 2 && depleted.rounds == 56,
          "depleted first trip pays exit recall heal and return");
    const auto two = cost(30, 5, 10, 2000, 20, 1600);
    check(two.feasible && two.trips == 2 && two.rounds == 56,
          "two complete damage pools require one recovery");
    const auto harmless = cost(7, 5, 10, 2000, 1, 0, 1, 1000, 1000);
    check(harmless.feasible && harmless.trips == 1 && harmless.rounds == 18,
          "harmless work needs no health pool even at low HP");
    check(!cost(30, 5, 10, 2000, 20, 1, 1000, 1000, 1000).feasible,
          "damage with no post-reserve health pool is unsupported");
    const auto nearby = cost(30, 2, 3, 2000, 20, 1600);
    const auto distant = cost(30, 30, 80, 2000, 20, 1600);
    check(nearby.feasible && distant.feasible && nearby.score > distant.score && nearby.rounds < distant.rounds,
          "recovery route changes complete-episode opportunity cost");
    const auto giant = cost(200, 5, 40, 2000, 300, 20000);
    check(giant.feasible && nearby.score > giant.score,
          "larger nominal reward can lose after complete repeated-trip costs");
    check(cost(30, 5, 10, 37).feasible && !cost(30, 5, 10, 36).feasible,
          "complete conversion must fit remaining game time");
    const auto invalid = cost(std::numeric_limits<float>::quiet_NaN());
    check(!invalid.feasible && invalid.score == 0.0f && std::isfinite(invalid.score),
          "NaN reward cannot poison candidate ranking");
    check(!cost(30, -1).feasible && !cost(30, 5, 10, 2000, -1).feasible,
          "negative route and forecast inputs reject");
    const auto overflow = cost(30, 5, 10, 2000, 20, std::numeric_limits<float>::max(), 1000, 1000, 999.99f);
    check(!overflow.feasible && std::isfinite(overflow.score), "unbounded trip count rejects before integer conversion");
    std::printf("production kernel: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
