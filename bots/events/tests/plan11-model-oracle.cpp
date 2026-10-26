#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include "../common/src/events.h"

#ifdef NDEBUG
#error This oracle requires assertions enabled.
#endif

namespace {
constexpr int max_jobs = 6;
constexpr int job_types = 9; // HP {1,2,3} x nonnegative weight {0,1,2}.
constexpr int reserved_phases = 2;

struct Job { int hp, weight; };
struct PhaseResult { int turns = 0, loss = 0; };
struct Counts {
    std::uint64_t cases = 0, permutations = 0, threshold_points = 0;
    std::uint64_t phase_pairs = 0;
};
Counts counts;

Job decode(int type) { return {1 + type / 3, type % 3}; }

// Independent oracle: no call to attacks(), target_rank(), clear_schedule(),
// ceil(), or a weighted-completion formula. Integer HP/damage make every
// subtraction exact in the production float subset tested exhaustively here.
PhaseResult phase_loop(const std::array<int, max_jobs>& order, int size, int damage) {
    std::array<Job, max_jobs> remaining{};
    int alive_weight = 0;
    for (int i = 0; i < size; ++i) {
        remaining[i] = decode(order[i]);
        alive_weight += remaining[i].weight;
    }
    PhaseResult result;
    // clear_schedule adds two common exposure phases to its lethal-before-
    // next-camp-phase model. Their position is immaterial to the ordering.
    for (int phase = 0; phase < reserved_phases; ++phase) result.loss += alive_weight;
    for (int target = 0; target < size; ++target) {
        while (remaining[target].hp > 0) {
            remaining[target].hp -= damage;
            ++result.turns;
            if (remaining[target].hp <= 0) alive_weight -= remaining[target].weight;
            // The basic action kills before this next camp-action phase.
            result.loss += alive_weight;
        }
    }
    assert(alive_weight == 0);
    return result;
}

void check_multiset(const std::array<int, max_jobs>& types, int size) {
    for (int damage = 1; damage <= 3; ++damage) {
        auto order = types;
        PhaseResult optimum{0, std::numeric_limits<int>::max()};
        do {
            const auto outcome = phase_loop(order, size, damage);
            if (outcome.loss < optimum.loss) optimum = outcome;
            ++counts.permutations;
        } while (std::next_permutation(order.begin(), order.begin() + size));
        std::array<p11::Monster, max_jobs> monsters{};
        for (int i = 0; i < size; ++i) {
            const auto job = decode(types[i]);
            monsters[i] = {float(job.hp), float(job.weight), 0};
        }
        const auto actual = p11::clear_schedule(std::span(monsters).first(size), float(damage));
        if (!actual.bounded || actual.turns != optimum.turns || actual.loss != float(optimum.loss)) {
            std::fprintf(stderr, "oracle mismatch: jobs=%d damage=%d actual=(%d,%g) optimum=(%d,%d)\n",
                         size, damage, actual.turns, double(actual.loss), optimum.turns, optimum.loss);
            for (int i = 0; i < size; ++i) {
                const auto job = decode(types[i]);
                std::fprintf(stderr, "  hp=%d weight=%d\n", job.hp, job.weight);
            }
        }
        assert(actual.bounded);
        assert(actual.turns == optimum.turns);
        assert(actual.loss == float(optimum.loss));
        ++counts.cases;
    }
}

void enumerate_multisets(std::array<int, max_jobs>& types, int size, int position, int minimum) {
    if (position == size) { check_multiset(types, size); return; }
    for (int type = minimum; type < job_types; ++type) {
        types[position] = type;
        enumerate_multisets(types, size, position + 1, type);
    }
}

void check_phase_order() {
    constexpr std::array global{p11::Phase::respawn, p11::Phase::spell_update,
                                p11::Phase::capture, p11::Phase::camp_attack};
    constexpr std::array owner{p11::Phase::owner_start, p11::Phase::action, p11::Phase::owner_end};
    std::array<p11::Event, 39> expected{};
    std::size_t size = 0;
    // Build the expected sequence from the documented two nested clocks,
    // independently of Event::key() and the enum's underlying integers.
    for (int round = 7; round <= 9; ++round) {
        for (auto phase : global) expected[size++] = {round, phase, 0};
        for (int actor = 0; actor < 3; ++actor)
            for (auto phase : owner) expected[size++] = {round, phase, actor};
    }
    assert(size == expected.size());
    for (std::size_t i = 0; i < size; ++i) {
        for (std::size_t j = 0; j < size; ++j) {
            assert((expected[i] < expected[j]) == (i < j));
            assert((expected[i] == expected[j]) == (i == j));
            ++counts.phase_pairs;
        }
    }
    for (auto phase : global) {
        const p11::Event first{8, phase, 0}, other{8, phase, 99};
        assert(first == other); // Global phases have no champion-slot clock.
    }
    assert((p11::Event{8, p11::Phase::owner_end, 0}
            < p11::Event{8, p11::Phase::owner_start, 1}));
}

void check_thresholds() {
    // Exactly representable damages/products isolate division rounding at a
    // threshold from the separate repeated-subtraction assumption below.
    constexpr std::array damages{.25f, .5f, 1.f, 2.f, 3.f, 5.f, 7.f, 10.f, 13.f, 32.f, 100.f};
    const float infinity = std::numeric_limits<float>::infinity();
    for (float damage : damages) {
        for (int hits = 1; hits <= 64; ++hits) {
            const float threshold = float(hits) * damage;
            assert(p11::attacks(std::nextafter(threshold, -infinity), damage) == hits);
            assert(p11::attacks(threshold, damage) == hits);
            assert(p11::attacks(std::nextafter(threshold, infinity), damage) == hits + 1);
            counts.threshold_points += 3;
        }
    }
    assert(p11::attacks(0, 1) == 0);
    assert(p11::attacks(-1, 1) == 0);
    assert(p11::attacks(1, 0) == 10000);
}

void check_continuous_witness() {
    // Damage=3: HP/weight says A before B (1 < 1.5), while both need one
    // actual hit, so removing B's weight 2 first is strictly better.
    const Job a{1, 1}, b{3, 2};
    assert(float(a.hp) / a.weight < float(b.hp) / b.weight);
    std::array<int, max_jobs> continuous{1, 8}; // A then B.
    std::array<int, max_jobs> integer{8, 1}; // B then A.
    const auto continuous_result = phase_loop(continuous, 2, 3);
    const auto integer_result = phase_loop(integer, 2, 3);
    const std::array<p11::Monster, 2> monsters{{{1, 1, 0}, {3, 2, 0}}};
    const auto schedule = p11::clear_schedule(monsters, 3);
    assert(continuous_result.turns == 2 && integer_result.turns == 2);
    assert(continuous_result.loss == 8 && integer_result.loss == 7);
    assert(schedule.loss == float(integer_result.loss));
}

int subtraction_hits(float hp, float damage) {
    int hits = 0;
    while (hp > 0 && hits < 10000) { hp -= damage; ++hits; }
    return hits;
}

void print_arithmetic_witness() {
    const float hp = .3f, damage = .1f;
    const int quotient = int(std::ceil(hp / damage));
    int multiplied = 1;
    while (float(multiplied) * damage < hp) ++multiplied;
    const int subtracted = subtraction_hits(hp, damage);
    // The repaired helper follows sequential float32 subtraction. The abstract
    // input remains distinct from the separately retained native encounter.
    assert(quotient == 3 && multiplied == 3 && subtracted == 4);
    assert(p11::attacks(hp, damage) == subtracted);
    std::printf("{\"arithmeticWitness\":{\"hp\":%.9g,\"damage\":%.9g,"
                "\"ceilQuotient\":%d,\"floatProductHits\":%d,\"repeatedSubtractionHits\":%d,"
                "\"currentModelHits\":%d,\"engineArithmeticVerified\":false}}\n",
                double(hp), double(damage), quotient, multiplied, subtracted, p11::attacks(hp, damage));
}
} // namespace

int main() {
    std::array<int, max_jobs> types{};
    for (int size = 0; size <= max_jobs; ++size) enumerate_multisets(types, size, 0, 0);
    assert(counts.cases == 15015);
    assert(counts.permutations == 1793613);
    check_phase_order();
    check_thresholds();
    check_continuous_witness();
    print_arithmetic_witness();
    std::printf("{\"passed\":true,\"scheduleCases\":%llu,\"killOrderPermutations\":%llu,"
                "\"phasePairs\":%llu,\"thresholdPoints\":%llu,"
                "\"continuousWitness\":{\"damage\":3,\"jobs\":[[1,1],[3,2]],"
                "\"continuousOrderLoss\":8,\"integerOrderLoss\":7,"
                "\"weightedCompletion\":[5,4]}}\n",
                static_cast<unsigned long long>(counts.cases),
                static_cast<unsigned long long>(counts.permutations),
                static_cast<unsigned long long>(counts.phase_pairs),
                static_cast<unsigned long long>(counts.threshold_points));
}
