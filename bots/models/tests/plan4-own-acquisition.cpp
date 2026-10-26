#include "../common/own-acquisition.h"
#include <cassert>
#include <iostream>

using namespace plan4;
using namespace plan4::runtime;

static OwnStockBoundary boundary(int round, unsigned bag, int group = 42,
    Lifecycle lifecycle = Lifecycle::cleared) {
    OwnStockBoundary b;
    b.actor = 7; b.round = round; b.position = {3,4}; b.bag = bag;
    b.alive = b.channel_none = b.complete = b.on_unique_spawn = true;
    b.own_provenance = {round, Source::own_state, Status::exact_now};
    b.group = {7, round, group, 30, lifecycle,
        {round, Source::personal_vision, Status::exact_now}, true, true};
    b.group_gems = 20; b.respawn_time = 60;
    b.enemy_ids[0] = 10; b.enemy_ids[1] = 11; b.enemy_count = 2;
    b.enemies_complete = b.enemies_all_alive = true;
    return b;
}
static void anchor(OwnAcquisitionTracker& tracker, const OwnStockBoundary& end) {
    tracker.start(end);
    tracker.finish(end);
}
static void check_row(const OwnStockFrame& frame, int index, int group, int captured,
    unsigned gems, int now) {
    assert(frame.known && frame.stock_epoch != 0 && index >= 0 && index < frame.count);
    const auto& row = frame.acquisitions[index];
    assert(row.group_id == group && row.capture_round == captured && row.gems == gems);
    assert(row.stock_epoch == frame.stock_epoch && row.provenance.observed_round == now);
    assert(row.provenance.source == Source::own_state && row.provenance.status == Status::exact_now);
}
int main() {
    int cases = 0;
    // The measured share, including an integer remainder, is certified. It is
    // deliberately smaller than the public whole-group reward.
    OwnAcquisitionTracker tracker;
    const auto end = boundary(10, 0);
    const auto start = boundary(11, 7, 42, Lifecycle::respawning);
    anchor(tracker, end);
    const auto epoch = tracker.frame().stock_epoch;
    tracker.start(start);
    assert(tracker.capture());
    const auto receipt = *tracker.capture();
    assert(receipt.origin == CaptureOrigin::isolated_own_stock && receipt.actor_attribution_known);
    assert(receipt.actor_id == 7 && receipt.group_id == 42 && receipt.round == 11);
    assert(receipt.event_status == Status::derived && receipt.personally_witnessed);
    assert(receipt.bag_before == 0 && receipt.bag_after == 7 && receipt.received_gems == 7);
    assert(receipt.stock_epoch == epoch && receipt.own_provenance.observed_round == 11);
    assert(receipt.own_provenance.source == Source::own_state && receipt.own_provenance.status == Status::exact_now);
    check_row(tracker.frame(), 0, 42, 11, 7, 11); ++cases;

    tracker.start(start);
    assert(!tracker.capture() && tracker.frame().count == 1 && tracker.frame().stock_epoch == epoch);
    check_row(tracker.frame(), 0, 42, 11, 7, 11); ++cases;

    // Own actions can move after START. Moving onto the second camp does not
    // detach the first share; the next stationary boundary captures its share.
    auto second_end = boundary(11, 7, 43);
    second_end.position = {9,8};
    tracker.finish(second_end);
    assert(!tracker.capture());
    auto second_start = boundary(12, 19, 43, Lifecycle::respawning);
    second_start.position = {9,8};
    tracker.start(second_start);
    assert(tracker.capture() && tracker.capture()->received_gems == 12 && tracker.frame().count == 2);
    check_row(tracker.frame(), 0, 42, 11, 7, 12);
    check_row(tracker.frame(), 1, 43, 12, 12, 12); ++cases;

    // Refresh only the current stock proof; historical capture rounds persist.
    auto held_end = second_start;
    held_end.position = {8,8}; held_end.on_unique_spawn = false;
    held_end.group = {}; held_end.enemies_complete = false;
    tracker.finish(held_end);
    auto held_start = held_end;
    held_start.round = 13; held_start.own_provenance.observed_round = 13;
    tracker.start(held_start);
    assert(!tracker.capture() && tracker.frame().stock_epoch == epoch && tracker.frame().count == 2);
    check_row(tracker.frame(), 0, 42, 11, 7, 13);
    check_row(tracker.frame(), 1, 43, 12, 12, 13); ++cases;

    // Third certified occurrence evicts the oldest row, retaining the actual
    // measured amounts even if additional unassociated stock was acquired.
    auto third_end = boundary(13, 22, 44);
    tracker.finish(third_end);
    tracker.start(boundary(14, 42, 44, Lifecycle::respawning));
    assert(tracker.capture() && tracker.capture()->received_gems == 20 && tracker.frame().count == 2);
    check_row(tracker.frame(), 0, 43, 12, 12, 14);
    check_row(tracker.frame(), 1, 44, 14, 20, 14); ++cases;

    // Every acquisition premise fails closed independently. Enemy failures
    // include hidden public alive flags, identity mismatch and malformed lists.
    for (int change = 0; change < 39; ++change) {
        OwnAcquisitionTracker test;
        auto a = end, b = start;
        switch (change) {
            case 0: b.bag = 0; break;
            case 1: b.bag = 21; break;
            case 2: a.channel_none = false; break;
            case 3: b.channel_none = false; break;
            case 4: a.on_unique_spawn = false; break;
            case 5: b.on_unique_spawn = false; break;
            case 6: b.position.x++; break;
            case 7: a.group.actor = 8; break;
            case 8: b.group.actor = 8; break;
            case 9: a.group.round--; break;
            case 10: b.group.round--; break;
            case 11: a.group_gems = 0; b.group_gems = 0; break;
            case 12: b.group_gems = 21; break;
            case 13: a.respawn_time = b.respawn_time = 2; break;
            case 14: b.respawn_time = 61; break;
            case 15: a.enemies_complete = false; break;
            case 16: b.enemies_complete = false; break;
            case 17: a.enemies_all_alive = false; break;
            case 18: b.enemies_all_alive = false; break;
            case 19: b.enemy_ids[1] = 12; break;
            case 20: b.enemy_ids[1] = 10; break;
            case 21: b.enemy_ids[0] = 12; break;
            case 22: b.enemy_count = 17; break;
            case 23: b.enemy_count = -1; break;
            case 24: b.enemy_count = 0; break;
            case 25: b.enemy_count = 1; break;
            case 26: b.enemy_ids[0] = -1; break;
            case 27: b.enemy_ids[0] = b.actor; break;
            case 28: a.group.personally_visible = false; break;
            case 29: b.group.personally_visible = false; break;
            case 30: a.group.provenance.status = Status::derived; break;
            case 31: b.group.provenance.source = Source::public_layout; break;
            case 32: b.group.provenance.observed_round--; break;
            case 33: b.group.respawn_rounds++; break;
            case 34: a.group.group_id = -1; break;
            case 35: b.group.group_id++; break;
            case 36: a.group.lifecycle = Lifecycle::living; break;
            case 37: b.group.lifecycle = Lifecycle::cleared; break;
            case 38: b.group.actor_alive = false; break;
        }
        anchor(test, a); test.start(b);
        assert(!test.capture() && test.frame().count == 0); ++cases;
    }

    // Proof failure alone must retain a preexisting association if the bag
    // observation is independently continuous, even when its amount grows.
    for (int change = 0; change < 7; ++change) {
        OwnAcquisitionTracker test;
        anchor(test, end); test.start(start);
        auto a = boundary(11, 7, 43), b = boundary(12, 12, 43, Lifecycle::respawning);
        switch (change) {
            case 0: b.group = {}; break;
            case 1: b.enemies_all_alive = false; break;
            case 2: b.enemies_complete = false; break;
            case 3: b.channel_none = false; break;
            case 4: b.position.x++; break;
            case 5: b.on_unique_spawn = false; break;
            case 6: b.bag = 7; break;
        }
        test.finish(a); test.start(b);
        assert(!test.capture() && test.frame().count == 1);
        check_row(test.frame(), 0, 42, 11, 7, 12); ++cases;
    }

    // Own observation failures break continuity and cannot refresh old stock.
    for (int change = 0; change < 13; ++change) {
        OwnAcquisitionTracker test;
        anchor(test, end); test.start(start);
        const auto old_epoch = test.frame().stock_epoch;
        auto a = boundary(11, 7, 43), b = boundary(12, 12, 43, Lifecycle::respawning);
        test.finish(a);
        switch (change) {
            case 0: b.complete = false; break;
            case 1: b.alive = false; break;
            case 2: b.actor = -1; break;
            case 3: b.actor = 8; break;
            case 4: b.round = -1; break;
            case 5: b.round++; b.own_provenance.observed_round++; break;
            case 6: b.own_provenance.observed_round--; break;
            case 7: b.own_provenance.source = Source::personal_vision; break;
            case 8: b.own_provenance.status = Status::derived; break;
            case 9: b.position.x = -1; break;
            case 10: b.position.y = -1; break;
            case 11: b.bag = 0; break;
            case 12: b.bag = 6; break;
        }
        test.start(b);
        assert(!test.capture() && test.frame().count == 0 && test.frame().stock_epoch != old_epoch);
        ++cases;
    }

    // Missing END, malformed repeated START, a foreign END and an END resumed
    // in a later round all discard associations. None can anchor a receipt.
    for (int change = 0; change < 7; ++change) {
        OwnAcquisitionTracker test;
        anchor(test, end); test.start(start);
        const auto old_epoch = test.frame().stock_epoch;
        auto b = boundary(12, 12, 43, Lifecycle::respawning);
        switch (change) {
            case 0: test.start(b); break;
            case 1: { auto replay = start; replay.bag++; test.start(replay); break; }
            case 2: { auto wrong = boundary(11, 7, 43); wrong.actor++; test.finish(wrong); test.start(b); break; }
            case 3: test.finish(boundary(12, 7, 43)); test.start(boundary(13, 12, 43, Lifecycle::respawning)); break;
            case 4: { auto dead = boundary(11, 7, 43); dead.alive = false; test.finish(dead); test.start(b); break; }
            case 5: { auto partial = boundary(11, 7, 43); partial.complete = false; test.finish(partial); test.start(b); break; }
            case 6: { auto decrease = boundary(11, 6, 43); test.finish(decrease); b.bag = 6; test.start(b); break; }
        }
        assert(!test.capture() && test.frame().count == 0 && test.frame().stock_epoch != old_epoch);
        ++cases;
    }

    // A lone END is insufficient. Round arithmetic must not wrap; the largest
    // representable round can still be a legal final, freshly observed START.
    {
        OwnAcquisitionTracker test;
        test.finish(end); test.start(start);
        assert(!test.capture() && test.frame().count == 0); ++cases;
        const int last_round = std::numeric_limits<int>::max();
        OwnAcquisitionTracker edge;
        anchor(edge, boundary(last_round - 1, 0));
        edge.start(boundary(last_round, 20, 42, Lifecycle::respawning));
        assert(edge.capture() && edge.frame().count == 1);
        edge.finish(boundary(last_round, 20));
        edge.start(boundary(0, 40, 42, Lifecycle::respawning));
        assert(!edge.capture() && edge.frame().count == 0); ++cases;
    }
    // Replaying a whole old sequence must not create a fresh epoch promise
    // for its historical occurrence, even after clearing the current rows.
    {
        OwnAcquisitionTracker test;
        anchor(test, end); test.start(start);
        test.finish(boundary(11, 7));
        test.start(end); test.finish(end); test.start(start);
        assert(!test.capture() && !test.frame().known && test.frame().count == 0); ++cases;
        test.start(boundary(12, 7));
        test.finish(boundary(12, 7));
        test.start(boundary(13, 20, 42, Lifecycle::respawning));
        assert(test.capture() && test.capture()->received_gems == 13); ++cases;
    }
    std::cout << "{\"schema\":\"plan4-own-acquisition-tests/1\",\"passed\":true,\"cases\":" << cases << "}\n";
}
