#pragma once
#include "model.h"

namespace plan3_runtime {
// Public score endpoints are exact even when an actor's personal attribution
// is incomplete. One observation describes only the preceding owner interval.
// The caller certifies personalAmountKnown from its actual submission state;
// the recorder cannot infer native channel completion from public team score.
class HorizonObservations {
    bool initialized_ = false, coverage_ = true;
    int start_round_ = 0, last_round_ = 0;
    int start_own_ = 0, start_enemy_ = 0;
    int last_own_ = 0, last_enemy_ = 0, personal_ = 0;
public:
    void initialize(int round, int own, int enemy) {
        initialized_ = round >= 0 && own >= 0 && enemy >= 0;
        start_round_ = last_round_ = round;
        start_own_ = last_own_ = own;
        start_enemy_ = last_enemy_ = enemy;
        personal_ = 0; coverage_ = true;
    }
    void observe(int round, int own, int enemy, int personalLowerBound,
                 bool personalAmountKnown, std::vector<plan3::ScoreBlock>& history) {
        if (!initialized_ || round < last_round_ || own < last_own_ || enemy < last_enemy_) {
            history.clear(); initialize(round,own,enemy); return;
        }
        if (round == last_round_) {
            // Repeated observation must not credit a submission or close a
            // block twice. Conflicting scores cannot describe the same sample.
            if (own != last_own_ || enemy != last_enemy_) {
                history.clear(); initialize(round,own,enemy);
            }
            return;
        }
        if (history.size() > 4 || (!history.empty() && history.back().endRound != start_round_))
            history.clear();
        const auto age = std::int64_t(round) - start_round_;
        if (age > 32) {
            // Never distribute an overshot public increment into invented
            // 32-round blocks. Restart at this observed, possibly unaligned,
            // endpoint and wait for four new exact public windows.
            history.clear(); initialize(round,own,enemy); return;
        }
        const int own_delta = own - last_own_;
        const bool amount_valid = personalLowerBound >= 0 && personalLowerBound <= own_delta;
        const bool exact_amount = personalAmountKnown && amount_valid &&
            (personalLowerBound == 0 || personalLowerBound == own_delta);
        coverage_ &= round - last_round_ == 1 && exact_amount;
        if (amount_valid) personal_ += personalLowerBound;
        last_round_ = round; last_own_ = own; last_enemy_ = enemy;
        if (age == 32) {
            history.push_back({start_round_,round,own-start_own_,enemy-start_enemy_,personal_,coverage_});
            if (history.size() > 4) history.erase(history.begin());
            start_round_ = round; start_own_ = own; start_enemy_ = enemy;
            personal_ = 0; coverage_ = true;
        }
    }
};
} // namespace plan3_runtime
