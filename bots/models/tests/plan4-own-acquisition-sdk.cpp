#include "../common/own-acquisition-sdk.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace plan4;
using namespace plan4::runtime;
namespace mock = cambc::mock;
using Query = mock::Query;

namespace {
struct Entry { cambc::map::camp_group group; cambc::bitboard spawns; };
struct Fixture {
    std::vector<Entry> entries;
    std::vector<int> lookup;
    int width = mock::board_width;
    Fixture() : lookup(mock::board_width * mock::board_height, -1) {
        mock::reset();
        add(42, {3, 4});
    }
    void add(int id, cambc::vec2 spawn) {
        cambc::bitboard tiles;
        tiles.set(spawn);
        const int index = static_cast<int>(entries.size());
        entries.push_back({cambc::map::camp_group{id}, tiles});
        lookup[static_cast<std::size_t>(mock::tile(spawn))] = index;
        if (id != 42) mock::groups.push_back({id, 1, 30, 30, 24, {spawn}});
    }
    void start(OwnAcquisitionCollector& c) const { c.start(entries, lookup, width); }
    void finish(OwnAcquisitionCollector& c) const { c.finish(entries, lookup, width); }
    void anchor(OwnAcquisitionCollector& c) const {
        mock::group(42).state = 0;
        start(c);
        mock::group(42).state = 1;
        finish(c);
    }
    void reward() const {
        mock::round = 2;
        mock::fuel = 100000;
        mock::own.bag = 12;
        mock::group(42).state = 2;
    }
};
void no_hidden_bodies() {
    assert(mock::unauthorized == 0);
    assert(mock::count(Query::enemy_position) == 0);
    assert(mock::count(Query::enemy_bag) == 0);
    assert(mock::count(Query::enemy_state) == 0);
    assert(mock::count(Query::enemy_visible) == 0);
}
void no_receipt(const OwnAcquisitionCollector& c) {
    assert(!c.capture());
    assert(c.frame().count == 0);
    no_hidden_bodies();
}
void retained(const OwnStockFrame& f, std::uint64_t epoch, int round, unsigned bag) {
    assert(f.known && f.bag == bag && f.stock_epoch == epoch && f.count == 1);
    assert(f.clock.round == round && f.clock.own.id == 7);
    const auto& row = f.acquisitions[0];
    assert(row.group_id == 42 && row.capture_round == 2 && row.gems == 12);
    assert(row.stock_epoch == epoch && row.provenance.observed_round == round);
    assert(row.provenance.source == Source::own_state && row.provenance.status == Status::exact_now);
}

[[gnu::noinline]] void measured_share_and_stock() {
    Fixture f;
    OwnAcquisitionCollector c;
    f.anchor(c);
    assert(c.frame().known && c.frame().bag == 0 && !c.capture());
    const auto epoch = c.frame().stock_epoch;
    f.reward();
    f.start(c);
    assert(c.capture());
    const auto receipt = *c.capture();
    assert(receipt.group_id == 42 && receipt.round == 2 && receipt.received_gems == 12);
    assert(receipt.bag_before == 0 && receipt.bag_after == 12);
    assert(receipt.origin == CaptureOrigin::isolated_own_stock);
    assert(receipt.actor_attribution_known && receipt.actor_id == 7 && receipt.stock_epoch == epoch);
    assert(receipt.personally_witnessed && receipt.event_status == Status::derived);
    assert(receipt.own_provenance.observed_round == 2 && receipt.own_provenance.source == Source::own_state);
    assert(receipt.own_provenance.status == Status::exact_now);
    retained(c.frame(), epoch, 2, 12);
    const int observed_queries = mock::queries;
    f.start(c);
    assert(c.capture() && c.capture()->group_id == receipt.group_id);
    assert(c.capture()->round == receipt.round && c.capture()->stock_epoch == receipt.stock_epoch);
    assert(c.capture()->received_gems == receipt.received_gems && mock::queries == observed_queries);
    retained(c.frame(), epoch, 2, 12);
    // Current stock survives leaving the captured region and the disappearance
    // of personal camp vision; no live group state may be read there.
    mock::own.position = {1, 1};
    mock::visible.fill(false);
    const int group_reads = mock::count(Query::group_state);
    f.finish(c);
    mock::round = 3;
    mock::own.bag = 15;
    f.start(c);
    assert(!c.capture());
    retained(c.frame(), epoch, 3, 15);
    assert(mock::count(Query::group_state) == group_reads);
    assert(mock::count(Query::roster) == 1);
    assert(mock::count(Query::enemy_alive) >= 6);
    no_hidden_bodies();
}

[[gnu::noinline]] void enemy_and_roster_rejections() {
    for (int change = 0; change < 8; ++change) {
        Fixture f;
        OwnAcquisitionCollector c;
        if (change == 0) mock::enemies[0].alive = false;
        if (change == 2) mock::roster = {9, 9};
        if (change == 3) {
            mock::roster.clear(); mock::enemies.clear();
            for (int id = 20; id < 37; ++id) {
                mock::roster.push_back(id);
                mock::enemies.push_back({id, {8, 8}, 0, true, false, {}});
            }
        }
        if (change == 4) mock::roster.clear();
        if (change == 5) mock::roster = {7, 9};
        if (change == 6) mock::roster = {3, 3, 9};
        if (change == 7) mock::roster = {-1, 9};
        f.anchor(c);
        f.reward();
        if (change == 1) mock::enemies[1].alive = false;
        f.start(c);
        no_receipt(c);
        assert(mock::count(Query::roster) == 1);
    }
    // Exactly sixteen public alive flags, presented out of ID order, remain
    // within the supported roster bound even when every body is hidden.
    Fixture f;
    OwnAcquisitionCollector c;
    mock::enemies.clear(); mock::roster.clear();
    for (int id = 35; id >= 20; --id) {
        mock::enemies.push_back({id, {8, 8}, 999, true, false, cambc::champion_state::recalling});
        mock::roster.push_back(id);
    }
    f.anchor(c);
    f.reward();
    f.start(c);
    assert(c.capture() && c.capture()->received_gems == 12);
    assert(mock::count(Query::roster) == 1);
    no_hidden_bodies();
}

[[gnu::noinline]] void channel_movement_and_death_rejections() {
    for (int change = 0; change < 10; ++change) {
        Fixture f;
        OwnAcquisitionCollector c;
        f.start(c);
        if (change < 3) mock::own.state = change == 0 ? cambc::champion_state::recalling :
            change == 1 ? cambc::champion_state::submitting_gems : cambc::champion_state::teleporting;
        if (change == 6) mock::own.alive = false;
        f.finish(c);
        mock::own.state = {};
        mock::own.alive = true;
        f.reward();
        if (change >= 3 && change < 6) mock::own.state = change == 3 ? cambc::champion_state::recalling :
            change == 4 ? cambc::champion_state::submitting_gems : cambc::champion_state::teleporting;
        if (change == 7) mock::own.alive = false;
        if (change == 8) { mock::own.position = {4, 4}; f.entries[0].spawns.set({4, 4});
            f.lookup[static_cast<std::size_t>(mock::tile({4, 4}))] = 0;
            mock::visible[static_cast<std::size_t>(mock::tile({4, 4}))] = true; }
        if (change == 9) mock::own.bag = 25;
        f.start(c);
        no_receipt(c);
        if (change == 7) assert(!c.frame().known);
    }
}

[[gnu::noinline]] void visibility_and_lookup_rejections() {
    for (int change = 0; change < 12; ++change) {
        Fixture f;
        OwnAcquisitionCollector c;
        if (change == 0) mock::visible.fill(false);
        if (change == 1) f.lookup[static_cast<std::size_t>(mock::tile({3, 4}))] = -1;
        if (change == 2) f.lookup[static_cast<std::size_t>(mock::tile({3, 4}))] = 999;
        if (change == 3) f.lookup.resize(3);
        if (change == 4) f.width = 0;
        if (change == 5) f.width = -1;
        if (change == 6) { f.add(43, {3, 4}); f.lookup[static_cast<std::size_t>(mock::tile({3, 4}))] = 0; }
        if (change == 7) { f.add(43, {4, 4}); f.lookup[static_cast<std::size_t>(mock::tile({3, 4}))] = 1; }
        if (change == 8) f.entries[0].spawns.set({3, 4}, false);
        if (change == 9) f.entries.push_back(f.entries[0]);
        if (change == 10) { mock::own.position = {-1, 4}; }
        if (change == 11) {
            const auto entry = f.entries[0];
            while (f.entries.size() < 257) f.entries.push_back(entry);
        }
        f.anchor(c);
        f.reward();
        f.start(c);
        no_receipt(c);
        if (change == 0) assert(mock::count(Query::group_state) == 0);
        assert(mock::count(Query::spawn_tile) <= 3 * 256);
    }
}

[[gnu::noinline]] void budget_and_drift_rejections() {
    // Lose the budget or round at each external observation category, including
    // the final live group query and the last enemy flag of an END sample.
    constexpr std::array<Query, 8> queries{Query::own_alive, Query::own_position,
        Query::own_bag, Query::own_state, Query::vision, Query::visible_tile,
        Query::group_state, Query::enemy_alive};
    for (const auto q : queries) for (bool drift : {false, true}) for (bool at_end : {false, true}) {
        Fixture f;
        OwnAcquisitionCollector c;
        if (at_end) {
            f.start(c);
            mock::arm(q, drift, drift ? -1 : 1000, q == Query::enemy_alive ? 2 : 1);
            f.finish(c);
            const bool completed_body = q != Query::own_alive && q != Query::own_position && q != Query::own_bag;
            assert(c.frame().known == (!drift && completed_body));
            mock::hook = {};
            f.reward();
            f.start(c);
        } else {
            f.anchor(c);
            f.reward();
            mock::arm(q, drift, drift ? -1 : 1000, q == Query::enemy_alive ? 2 : 1);
            f.start(c);
            const bool completed_body = q != Query::own_alive && q != Query::own_position && q != Query::own_bag;
            assert(c.frame().known == (!drift && completed_body));
        }
        no_receipt(c);
    }
    // Optional capture evidence can run out of budget after the fresh own body
    // has established that a previously certified share is still carried.
    constexpr std::array<Query, 4> optional{Query::vision, Query::visible_tile,
        Query::group_state, Query::enemy_alive};
    for (auto q : optional) for (bool at_end : {false, true}) {
        Fixture f;
        OwnAcquisitionCollector c;
        f.anchor(c);
        f.reward();
        f.start(c);
        assert(c.capture());
        const auto epoch = c.frame().stock_epoch;
        if (!at_end) {
            f.finish(c);
            mock::round = 3;
        }
        mock::arm(q, false, 1000, q == Query::enemy_alive ? 2 : 1);
        if (at_end) f.finish(c); else f.start(c);
        assert(!c.capture());
        retained(c.frame(), epoch, at_end ? 2 : 3, 12);
        no_hidden_bodies();
    }
    for (bool at_end : {false, true}) {
        Fixture f;
        OwnAcquisitionCollector c;
        if (at_end) {
            f.start(c);
            mock::fuel = 29999;
            f.finish(c);
        } else {
            f.anchor(c);
            f.reward();
            mock::fuel = 59999;
            f.start(c);
        }
        assert(!c.frame().known);
        no_receipt(c);
    }
}
}
int main() {
    measured_share_and_stock();
    enemy_and_roster_rejections();
    channel_movement_and_death_rejections();
    visibility_and_lookup_rejections();
    budget_and_drift_rejections();
    std::cout << "own acquisition SDK fixture: 106 scenarios passed; zero games\n";
}
