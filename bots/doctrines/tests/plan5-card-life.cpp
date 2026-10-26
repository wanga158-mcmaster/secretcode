#include "../common/src/card-life.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

using namespace p5::diagnostics;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "Card-life check %d failed at line %d: %s\n", checks, __LINE__, #condition); \
    std::abort(); } } while (false)

static_assert(std::is_trivially_copyable_v<CardLifeRecord>);
static CardLifeRecord record(CardLifeEvent event = CardLifeEvent::purchase, int round = 541) {
    CardLifeRecord r;
    r.event = event; r.project = 1; r.actor = 0; r.round = round;
    r.job = 8; r.group = 34; r.renewal = 30; r.altar = 1179; r.family = 0; r.renewal_entry = 575;
    r.from_stage = 7; r.stage = 8; r.phase = 0; r.reason = 0; r.action = 1; r.outcome = 2;
    r.flags = 0x125; r.position = 1084; r.observed_group = 34; r.budget = 80; r.gold = 98; r.bag = 0;
    r.purchase = 13; r.capture = 8; r.bank = 4;
    return r;
}
static void same(const CardLifeRecord& a, const CardLifeRecord& b) {
    CHECK(a.event == b.event && a.project == b.project && a.actor == b.actor && a.round == b.round);
    CHECK(a.job == b.job && a.group == b.group && a.renewal == b.renewal && a.altar == b.altar &&
          a.family == b.family && a.renewal_entry == b.renewal_entry);
    CHECK(a.from_stage == b.from_stage && a.stage == b.stage && a.phase == b.phase &&
          a.reason == b.reason && a.action == b.action && a.outcome == b.outcome);
    CHECK(a.flags == b.flags && a.position == b.position && a.observed_group == b.observed_group &&
          a.budget == b.budget && a.gold == b.gold && a.bag == b.bag);
    CHECK(a.purchase == b.purchase && a.capture == b.capture && a.bank == b.bank);
}
static void empty_and_arm() {
    CardLifeQueue q;
    CHECK(!q.armed() && !q.pending() && q.front() == nullptr && q.count() == 0 && q.dropped() == 0);
    q.consume(); CHECK(q.front() == nullptr && q.count() == 0);
    CHECK(!q.push(record(CardLifeEvent::terminal)) && q.dropped() == 0);
    for (int bad = 0; bad < 5; ++bad) {
        auto r = record();
        if (bad == 0) r.event = CardLifeEvent::production;
        if (bad == 1) r.project = 0;
        if (bad == 2) r.project = -1;
        if (bad == 3) r.actor = -1;
        if (bad == 4) r.round = -1;
        CHECK(!q.arm(r) && !q.armed() && q.count() == 0 && q.dropped() == 0);
    }
    auto r = record(); const auto expected = r;
    CHECK(q.arm(r) && q.armed() && q.pending() && q.count() == 1);
    r.project = 9; r.actor = 4; r.round = 999; r.job = r.group = r.renewal = r.altar = r.family = -1;
    r.renewal_entry = r.stage = r.phase = r.reason = r.action = r.outcome = -1;
    r.flags = r.position = r.observed_group = r.budget = r.gold = r.bag = -1;
    r.purchase = r.capture = r.bank = 999;
    same(*q.front(), expected);
    CHECK(!q.arm(record()) && q.count() == 1 && q.dropped() == 0);
    CHECK(!q.push(record()) && q.count() == 1 && q.dropped() == 0);
    q.consume(); CHECK(q.armed() && !q.pending() && q.count() == 1);
    CHECK(!q.arm(record()) && !q.pending());
}
static void fifo_and_owned_copies() {
    CardLifeQueue q;
    std::array<CardLifeRecord, 8> expected{};
    expected[0] = record(); CHECK(q.arm(expected[0]));
    for (int i = 1; i < 8; ++i) {
        auto r = record(static_cast<CardLifeEvent>(i + 1), 541 + i);
        r.from_stage = i; r.stage = i + 1; r.phase = i + 2; r.reason = i + 3;
        r.action = i + 4; r.outcome = i + 5; r.flags = i * 7; r.position = i * 11;
        r.observed_group = i * 13; r.budget = 80 - i; r.gold = 98 + i; r.bag = i;
        r.purchase += i; r.capture += i * 2; r.bank += i * 3;
        expected[i] = r;
        CHECK(q.push(r) && q.count() == unsigned(i + 1));
        r.gold = r.bag = r.position = -999; r.purchase = 0;
    }
    CHECK(q.count() == 8 && q.dropped() == 0);
    for (const auto& r : expected) {
        CHECK(q.pending() && q.front() != nullptr); same(*q.front(), r); q.consume();
    }
    CHECK(!q.pending() && q.front() == nullptr && q.count() == 8 && q.armed());
    q.consume(); q.consume(); CHECK(q.count() == 8 && q.front() == nullptr);
    CHECK(!q.push(record(CardLifeEvent::terminal, 999)) && !q.arm(record()));
}
static void frozen_context_and_origin() {
    CardLifeQueue q; const auto first = record(); CHECK(q.arm(first));
    for (int bad = 0; bad < 12; ++bad) {
        auto r = record(CardLifeEvent::proof_wait, 542);
        switch (bad) {
        case 0: ++r.project; break; // Same group does not identify this project.
        case 1: ++r.actor; break;
        case 2: ++r.job; break;
        case 3: ++r.group; break;
        case 4: ++r.renewal; break;
        case 5: ++r.altar; break;
        case 6: ++r.family; break;
        case 7: ++r.renewal_entry; break;
        case 8: r.round = first.round - 1; break;
        case 9: r.event = static_cast<CardLifeEvent>(0); break;
        case 10: r.event = static_cast<CardLifeEvent>(9); break;
        default: r.event = static_cast<CardLifeEvent>(-1); break;
        }
        const unsigned drops = q.dropped();
        CHECK(!q.push(r) && q.count() == 1 && q.dropped() == drops + 1);
        same(*q.front(), first);
    }
    // Equal-origin rounds are valid. The queue retains insertion order; it
    // does not infer stages or require enum order from the caller's payload.
    CHECK(q.push(record(CardLifeEvent::proof_wait, first.round)));
    CHECK(q.count() == 2 && q.dropped() == 12);
    q.consume(); same(*q.front(), record(CardLifeEvent::proof_wait, first.round));
}
static void lifetime_dedup_and_terminal_reservation() {
    CardLifeQueue q; CHECK(q.arm(record())); q.consume();
    for (int event = 2; event <= 7; ++event) {
        const auto r = record(static_cast<CardLifeEvent>(event), 541 + event);
        CHECK(q.push(r) && q.count() == unsigned(event));
        for (int repeated = 0; repeated < 20; ++repeated) {
            auto later = r; later.round += repeated + 1; later.flags = repeated;
            CHECK(!q.push(later) && q.dropped() == 0 && q.count() == unsigned(event));
        }
        same(*q.front(), r); q.consume();
        CHECK(!q.pending() && q.armed());
        CHECK(!q.push(r) && q.dropped() == 0); // Drain never frees a milestone.
    }
    CHECK(q.count() == 7 && !q.pending());
    auto terminal = record(CardLifeEvent::terminal, 900); terminal.reason = 77;
    CHECK(q.push(terminal) && q.pending() && q.count() == 8);
    same(*q.front(), terminal); q.consume();
    CHECK(q.count() == 8 && !q.pending() && q.armed());
    CHECK(!q.arm(record()) && !q.push(record(CardLifeEvent::payout, 901)) && q.dropped() == 0);
    // An early terminal is also valid for abort, without implying completed
    // production. Consuming either terminal cannot implicitly rearm a queue.
    CardLifeQueue aborted; CHECK(aborted.arm(record()));
    CHECK(aborted.push(record(CardLifeEvent::terminal, 542)) && aborted.count() == 2);
    aborted.consume(); aborted.consume();
    CHECK(!aborted.pending() && !aborted.arm(record()) && !aborted.push(record(CardLifeEvent::production, 543)));
}
static void dropped_and_saturation() {
    CardLifeQueue q; CHECK(q.arm(record()));
    for (unsigned i = 0; i < 1024; ++i) {
        CHECK(!q.push(record(static_cast<CardLifeEvent>(99), 542)));
        CHECK(q.dropped() == i + 1 && q.count() == 1);
    }
    // No API can append a ninth distinct valid event. Invalid attempts cannot
    // consume the reserved terminal slot or change the accepted purchase.
    CHECK(q.push(record(CardLifeEvent::terminal, 543)) && q.count() == 2);
    CHECK(q.dropped() == 1024); same(*q.front(), record());
    CHECK(card_life_increment(0) == 1 && card_life_increment(17) == 18);
    const unsigned maximum = std::numeric_limits<unsigned>::max();
    CHECK(card_life_increment(maximum - 1) == maximum);
    CHECK(card_life_increment(maximum) == maximum);
}
int main() {
    empty_and_arm(); fifo_and_owned_copies(); frozen_context_and_origin();
    lifetime_dedup_and_terminal_reservation(); dropped_and_saturation();
    std::printf("Card lifecycle queue checks passed: %d (pure; no native integration claim)\n", checks);
    return 0;
}
