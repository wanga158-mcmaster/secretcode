#include "support_field.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace mosaic::support_field;
namespace {
unsigned checks{};
void check(bool condition, int line) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "support-field check %u failed line %d\n", checks, line); std::abort(); }
}
#define CHECK(x) check(bool(x), __LINE__)

void geometry_and_complete_setup() {
    Field f;
    CHECK(!f.damageAt({0, 0}, 0, 0).valid);
    CHECK(f.reset(7, 6, 3, 128).applied());
    CHECK(f.addCamp(0, {2, 2}, 1, 20).applied());
    CHECK(f.addCamp(0, {3, 2}, 1, 60).applied());
    CHECK(f.addCamp(1, {2, 3}, 0, 120).applied());
    CHECK(f.addCamp(2, {6, 5}, 100, 0).applied());
    CHECK(f.campCount() == 4);
    CHECK(f.entryCount() == 13); // 12 merged group0 tiles, plus group1
    CHECK(!f.damageAt({2, 2}, 0, 0).valid);
    CHECK(f.seal(3).status == Status::incomplete);
    CHECK(f.seal(4).applied());
    CHECK(f.addCamp(1, {0, 0}, 0, 1).status == Status::sealed);
    CHECK(f.potentialAt({2, 2}).amount == 80);
    CHECK(f.potentialAt({2, 3}).amount == 200);
    CHECK(f.potentialAt({1, 1}).amount == 20);
    CHECK(f.potentialAt({4, 3}).amount == 60);
    CHECK(f.potentialAt({0, 0}).amount == 0);
    CHECK(!f.damageAt({-1, 0}, 0, 0).valid);
    CHECK(!f.damageAt({7, 0}, 0, 0).valid);
    CHECK(!f.damageAt({0, 6}, 0, 0).valid);
    CHECK(f.absence(0, 10, 100).applied());
    CHECK(f.damageAt({2, 3}, 10, 36).amount == 120);
    CHECK(f.damageAt({4, 3}, 10, 36).amount == 0);
    CHECK(f.damageAt({4, 3}, 9, 36).amount == 60); // no future observation authority
    CHECK(f.restore(0, 11).applied());
    CHECK(f.damageAt({2, 3}, 11, 36).amount == 200);
}

void deadlines_and_freshness() {
    Field f;
    CHECK(f.reset(3, 3, 2, 20).applied());
    CHECK(f.addCamp(0, {1, 1}, 1, 20).applied());
    CHECK(f.addCamp(1, {1, 1}, 0, 60).applied());
    CHECK(f.seal(2).applied());
    CHECK(f.absence(0, 100, 136).applied());
    const auto repeated = f.revision();
    CHECK(f.absence(0, 100, 136).applied());
    CHECK(f.revision() == repeated);
    CHECK(f.damageAt({1, 1}, 100, 35).amount == 60);
    CHECK(f.damageAt({1, 1}, 100, 36).amount == 80); // equality is threatened
    CHECK(f.damageAt({1, 1}, 135, 0).amount == 60);
    CHECK(f.damageAt({1, 1}, 136, 0).amount == 80);
    CHECK(f.damageAt({1, 1}, 2000, 0).amount == 80); // expiry does not require mutation
    const auto revision = f.revision();
    CHECK(f.absence(0, 99, 1000).status == Status::stale);
    CHECK(f.restore(0, 99).status == Status::stale);
    CHECK(f.revision() == revision);
    CHECK(f.absence(0, 100, 1000).applied()); // same event cannot extend credit
    CHECK(f.damageAt({1, 1}, 100, 36).amount == 80);
    CHECK(f.absence(0, 101, 200).applied());
    CHECK(f.damageAt({1, 1}, 101, 36).amount == 60);
    CHECK(f.restore(0, 101).applied());
    const auto restored = f.revision();
    CHECK(f.restore(0, 101).applied());
    CHECK(f.revision() == restored);
    CHECK(f.damageAt({1, 1}, 101, 36).amount == 80);
    CHECK(f.absence(0, 101, 200).status == Status::stale); // alive/generation wins tie
    CHECK(f.absence(0, 102, 200).applied());
    CHECK(f.absence(1, 102, 200).applied());
    CHECK(f.damageAt({1, 1}, 102, 36).amount == 0);
    CHECK(f.absence(0, 103, 103).applied()); // nonfuture deadline revokes old discount
    CHECK(f.damageAt({1, 1}, 103, 36).amount == 20);
    CHECK(f.restore(1, 104).applied());
    CHECK(f.damageAt({1, 1}, 104, 36).amount == 80);
}

void atomic_setup_and_capacity() {
    Field f;
    CHECK(f.reset(3, 3, 2, 20, 30).status == Status::deferred);
    CHECK(!f.ready());
    CHECK(f.reset(3, 3, 2, 20, 31).applied());
    CHECK(f.addCamp(0, {1, 1}, 1, 20, 26).status == Status::deferred);
    CHECK(f.campCount() == 0 && f.entryCount() == 0);
    CHECK(f.addCamp(0, {1, 1}, 1, 20, 27).applied());
    CHECK(f.addCamp(0, {1, 1}, 1, 60, 35).status == Status::deferred);
    CHECK(f.campCount() == 1 && f.entryCount() == 9);
    CHECK(f.addCamp(0, {1, 1}, 1, 60, 36).applied());
    CHECK(f.seal(2).applied());
    CHECK(f.potentialAt({1, 1}).amount == 80);
    const auto old_revision = f.revision();
    CHECK(f.reset(10, 10, 1, 10, 1).status == Status::deferred);
    CHECK(f.revision() == old_revision && f.ready());
    CHECK(f.potentialAt({1, 1}).amount == 80);

    Field small;
    CHECK(small.reset(3, 3, 2, 1).applied());
    CHECK(small.addCamp(0, {1, 1}, 0, 20).applied());
    CHECK(small.addCamp(1, {1, 1}, 1, 60).status == Status::capacity);
    CHECK(small.entryCount() == 1 && small.campCount() == 1);
    CHECK(small.seal(1).applied());
    CHECK(small.damageAt({1, 1}, 1, 0).amount == 20);
    CHECK(small.damageAt({0, 0}, 1, 0).amount == 0);
}

void no_hot_catalog_scan_and_deferred_query() {
    constexpr std::size_t count = 500;
    Field f;
    CHECK(f.reset(2, 1, count, count).applied());
    for (std::size_t g = 0; g < count; ++g) {
        CHECK(f.addCamp(g, {0, 0}, 0, 1).applied());
        CHECK(f.absence(g, 10, 100).applied());
    }
    CHECK(f.seal(count).applied());
    CHECK(f.damageAt({0, 0}, 10, 36).amount == 0);
    CHECK(f.damageAt({0, 0}, 10, 36).work == count);
    const auto zero = f.damageAt({0, 0}, 10, 36, 0);
    CHECK(zero.valid && !zero.exact && zero.amount == count && zero.work == 0);
    const auto partial = f.damageAt({0, 0}, 10, 36, 7);
    CHECK(partial.valid && !partial.exact && partial.amount == count - 7 && partial.work == 7);
    CHECK(f.damageAt({0, 0}, 100, 0, 0).exact); // global deadline fast path
    CHECK(f.damageAt({0, 0}, 100, 0, 0).amount == count);
    CHECK(f.damageAt({1, 0}, 10, 36, 0).exact); // empty tile fast path
    CHECK(f.damageAt({1, 0}, 10, 36, 0).work == 0);

    // 500 individual bodies in ONE group create one hot entry, not 500.
    Field merged;
    CHECK(merged.reset(1, 1, 1, 1).applied());
    for (std::size_t c = 0; c < count; ++c) CHECK(merged.addCamp(0, {0, 0}, 0, 1).applied());
    CHECK(merged.seal(count).applied());
    CHECK(merged.entryCount() == 1);
    CHECK(merged.absence(0, 10, 100).applied());
    const auto q = merged.damageAt({0, 0}, 10, 36, 1);
    CHECK(q.valid && q.exact && q.amount == 0 && q.work == 1);
}

void invalid_inputs_and_large_coordinates() {
    Field f;
    CHECK(f.reset(0, 2, 1, 1).status == Status::invalid);
    CHECK(f.reset(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 1, 1).status == Status::capacity);
    CHECK(f.reset(2, 2, Field::max_groups + 1, 1).status == Status::invalid);
    const auto oversized = f.reset(2, 2, 1, std::numeric_limits<std::uint32_t>::max() - 1, 0);
    CHECK(oversized.status == Status::capacity || oversized.status == Status::deferred);
    CHECK(f.reset(2, 2, 1, 4).applied());
    CHECK(f.addCamp(1, {0, 0}, 0, 1).status == Status::invalid);
    CHECK(f.addCamp(0, {-1, 0}, 0, 1).status == Status::invalid);
    CHECK(f.addCamp(0, {0, 0}, -1, 1).status == Status::invalid);
    CHECK(f.addCamp(0, {0, 0}, std::numeric_limits<int>::max(), 1).applied());
    CHECK(f.addCamp(0, {0, 0}, 0, mosaic::return_budget::cost_limit).status == Status::invalid);
    CHECK(f.campCount() == 1);
    CHECK(f.seal(1).applied());
    CHECK(f.damageAt({1, 1}, 0, 0).amount == 1);
    CHECK(!f.damageAt({0, 0}, -1, 0).valid);
    CHECK(!f.damageAt({0, 0}, 1, std::numeric_limits<std::uint64_t>::max()).valid);
    CHECK(f.absence(0, -1, 20).status == Status::invalid);
    CHECK(f.absence(0, 1, -1).status == Status::invalid);
    CHECK(f.restore(0, -1).status == Status::invalid);
    CHECK(f.absence(0, 1, std::numeric_limits<std::int64_t>::max()).applied());
    CHECK(f.damageAt({0, 0}, std::numeric_limits<std::int64_t>::max() - 1, 0).amount == 0);
    CHECK(f.damageAt({0, 0}, std::numeric_limits<std::int64_t>::max() - 1, 1).amount == 1);
    CHECK(f.damageAt({0, 0}, 1, std::numeric_limits<std::uint64_t>::max() - 1).amount == 1);
}

void independent_oracle() {
    struct Camp { std::size_t group; Point pos; int range; Cost hit; };
    const std::array camps{
        Camp{0, {0, 0}, 3, 20}, Camp{0, {3, 0}, 2, 20},
        Camp{1, {4, 4}, 4, 60}, Camp{2, {8, 8}, 5, 200},
        Camp{3, {2, 7}, 3, 0}, Camp{3, {0, 8}, 0, 120}
    };
    const std::array<std::int64_t, 4> until{25, 40, 60, 80};
    Field f;
    CHECK(f.reset(9, 9, until.size(), 400).applied());
    for (const auto& camp : camps) CHECK(f.addCamp(camp.group, camp.pos, camp.range, camp.hit).applied());
    CHECK(f.seal(camps.size()).applied());
    for (std::size_t group = 0; group < until.size(); ++group) CHECK(f.absence(group, 10, until[group]).applied());
    for (int now = 10; now <= 85; now += 5) for (unsigned horizon : {0u, 1u, 15u, 36u})
        for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) {
            Cost expected = 0;
            for (const auto& camp : camps)
                if (mosaic::return_budget::distance({x, y}, camp.pos) <= unsigned(camp.range) &&
                    until[camp.group] <= std::int64_t(now + horizon)) expected += camp.hit;
            const auto actual = f.damageAt({x, y}, now, horizon);
            CHECK(actual.valid && actual.exact && actual.amount == expected);
            const auto deferred = f.damageAt({x, y}, now, horizon, 1);
            CHECK(deferred.valid && deferred.amount >= expected && deferred.amount <= f.potentialAt({x, y}).amount);
        }
}

void trace_repricing_revokes_old_anchor() {
    Field f;
    CHECK(f.reset(3, 1, 1, 3).applied());
    CHECK(f.addCamp(0, {0, 0}, 1, 20).applied());
    CHECK(f.seal(1).applied());
    CHECK(f.absence(0, 10, 50).applied());
    mosaic::return_budget::Trace trace;
    CHECK(trace.reset_quiet({0, 0}, f.damageAt({0, 0}, 10, 36).damage()));
    CHECK(trace.observe_step({0, 0}, {1, 0}, f.damageAt({1, 0}, 10, 36).damage(), true) == mosaic::return_budget::Reason::ok);
    // Metadata does not change when the horizon crosses the deadline. The
    // adapter must reprice on time/horizon as well as metadata revision.
    const auto stamp = f.revision();
    const std::array damage{f.damageAt(trace.point(0), 14, 36).damage()};
    CHECK(f.revision() == stamp);
    CHECK(trace.refresh_damage(damage) == mosaic::return_budget::Reason::anchor_not_quiet);
    CHECK(!trace.valid());
}
}

int main() {
    geometry_and_complete_setup();
    deadlines_and_freshness();
    atomic_setup_and_capacity();
    no_hot_catalog_scan_and_deferred_query();
    invalid_inputs_and_large_coordinates();
    independent_oracle();
    trace_repricing_revokes_old_anchor();
    std::printf("support field: %u checks\n", checks);
}
