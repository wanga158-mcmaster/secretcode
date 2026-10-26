#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "coalition.h"
#include "party_model.h"

using namespace mb::party;

static Facts ready() { return {true, true, true, true, true, true, true, true, true}; }

int main() {
    {
        // Six actors have fifteen distinct unordered pairs, with a deterministic lower-ID leader.
        bool used[16]{};
        for (int a = 0; a < max_actors; ++a)
            for (int b = a + 1; b < max_actors; ++b) {
                const auto pair = pair_of(b, a);
                assert((pair == Pair{a, b}));
                const int code = pair_code(pair);
                assert(code > 0 && code <= 15 && !used[code]);
                used[code] = true;
                assert(decode_pair(code) == pair);
                for (int round : {0, 15, 16, 31, 32, 255, 256, 1023, 1024, 2000, 2047}) {
                    const Record source{pair, round};
                    const auto decoded = Record::decode(source.encode());
                    assert(decoded == source && decoded.fresh(round, 0));
                    assert(decoded.fresh(round + 4, 4) && !decoded.fresh(round + 5, 4));
                    assert(!decoded.fresh(round - 1, 4));
                    assert(!decoded.fresh(round + 32, 4) && !decoded.fresh(round + 256, 4));
                }
            }
        for (int code = 1; code <= 15; ++code) assert(used[code]);
        assert(!pair_of(2, 2).valid() && !pair_of(-1, 2).valid() && !pair_of(0, 6).valid());
        assert(!decode_pair(0).valid() && !decode_pair(16).valid());
    }
    {
        // Initial memory and the other protocol's valid frames never become offers.
        assert(!Record::decode({0, 0}).valid());
        for (int enemy = 0; enemy < 8; ++enemy)
            for (int round : {0, 255, 1024, 2047}) {
                auto focus = mb::coalition::FocusRecord{enemy, round}.encode();
                assert(!Record::decode(focus).valid());
            }
        for (int round : {0, 255, 1024, 2047}) {
            auto camp = Record{Pair{0, 5}, round}.encode();
            assert(!mb::coalition::FocusRecord::decode(camp).valid());
        }
        for (auto invalid : {Record{Pair{0, 1}, -1}, Record{Pair{0, 1}, 2048},
                             Record{Pair{1, 0}, 100}, Record{Pair{0, 6}, 100}})
            assert(!Record::decode(invalid.encode()).valid());
    }
    {
        // Seeing an offer permits acceptance, not production. A named ACK is still not arrival.
        const Record offer{Pair{1, 4}, 100};
        const Lease local{offer, 7, 110};
        const Slot pending_leader{7, Intent::idle}, committed{7, Intent::engaging};
        auto facts = ready();
        assert(may_accept(offer, 101, 10, 4, pending_leader, 7, facts));
        assert(!may_accept(offer, 101, 10, 2, pending_leader, 7, facts));
        assert(!may_accept(offer, 101, 10, 4, committed, 7, facts));
        assert(!may_accept(offer, 101, 10, 4, Slot{8, Intent::idle}, 7, facts));
        assert(!may_accept(offer, 111, 10, 4, pending_leader, 7, facts));
        assert(may_commit(local, offer, 101, 10, 1, pending_leader, committed, facts));
        facts.helper_arrived = false;
        assert(may_accept(offer, 101, 10, 4, pending_leader, 7, facts));
        assert(!may_commit(local, offer, 101, 10, 1, pending_leader, committed, facts));
        assert(!may_produce(local, offer, 101, 10, 4, committed, committed, facts));
        facts = ready();
        assert(!may_commit(local, offer, 101, 10, 1, pending_leader, Slot{8, Intent::engaging}, facts));
        assert(!may_commit(local, offer, 101, 10, 4, pending_leader, committed, facts));
        assert(!may_commit(local, offer, 101, 10, 1, pending_leader, Slot{7, Intent::capturing}, facts));
        assert(may_produce(local, offer, 102, 10, 1, committed, committed, facts));
        assert(may_produce(local, offer, 102, 10, 4, committed, committed, facts));
        assert(!may_produce(Lease{}, offer, 102, 10, 4, committed, committed, facts));
        assert(!may_produce(local, offer, 102, 10, 2, committed, committed, facts));
        assert(!may_produce(local, offer, 102, 10, 4, pending_leader, committed, facts));
        assert(!may_produce(local, offer, 102, 10, 4, committed, Slot{9, Intent::engaging}, facts));
        assert(!may_produce(local, Record{Pair{1, 4}, 101}, 102, 10, 4, committed, committed, facts));
        assert(!may_produce(local, Record{Pair{1, 5}, 100}, 102, 10, 4, committed, committed, facts));
        assert(!may_produce(Lease{offer, 7, 101}, offer, 102, 10, 4, committed, committed, facts));
        assert(!may_produce(local, offer, 99, 10, 4, committed, committed, facts));
    }
    {
        // Each hard refusal remains independently effective after an apparently valid ACK.
        const Record offer{Pair{0, 1}, 200};
        const Lease local{offer, 3, 210};
        const Slot leader{3, Intent::idle}, helper{3, Intent::engaging};
        auto facts = ready(); facts.fresh_alive_group = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.participants_available = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.routes_known = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.useful_first_hit = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.own_cost_passes = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.legal_first_contribution = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.prefix_survives = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
        facts = ready(); facts.one_wave_feasible = false;
        assert(!may_commit(local, offer, 201, 10, 0, leader, helper, facts));
    }
    {
        const Record offer{Pair{0, 2}, 600};
        const Lease local{offer, 4, 612};
        assert(reward_role(local, offer, 603, 12, 0, true) == RewardRole::capture);
        assert(reward_role(local, offer, 603, 12, 2, true) == RewardRole::release);
        assert(reward_role(local, offer, 603, 12, 1, true) == RewardRole::none);
        assert(reward_role(local, offer, 603, 12, 0, false) == RewardRole::none);
        assert(reward_role(local, offer, 613, 12, 0, true) == RewardRole::none);
    }
    {
        // Waiting is only the extra delay after each participant's own legal arrival.
        assert(rendezvous_wait(3, 8) == 5 && rendezvous_wait(8, 3) == 0);
        assert(3 + rendezvous_wait(3, 8) == 8 + rendezvous_wait(8, 3));
        assert(rendezvous_wait(-1, 8) == -1);
        Work base; base.travel = 5; base.clear = 15;
        const auto ordinary = opportunity(100, 2, true, base, false);
        assert(ordinary.valid && ordinary.positive && ordinary.occupied == 20 && ordinary.displaced == 40);
        for (int term = 0; term < 6; ++term) {
            auto work = base;
            switch (term) {
            case 0: work.extra_wait = 6; break;
            case 1: work.recovery = 6; break;
            case 2: work.rejoin = 6; break;
            case 3: work.abort_rounds = 6; break;
            case 4: work.bank = 6; break;
            default: work.capture_approach = 6; break;
            }
            assert(!opportunity(100, 2, true, work, true).positive);
        }
        auto work = base; work.capture_hold = 6;
        assert(!opportunity(100, 2, true, work, true).positive);
        assert(opportunity(100, 2, true, work, false).positive);  // helper leaves before capture
        work = base; work.capture_approach = 5;
        assert(!opportunity(100, 2, true, work, true).positive);  // equality is no surplus
        assert(!opportunity(100, 2, true, base, false, 0.2f).positive);
        assert(!opportunity(100, 2, false, base, false).valid);
        assert(!opportunity(0, 2, true, base, false).valid);
        assert(!opportunity(100, -1, true, base, false).valid);
        assert(!opportunity(100, 2, true, base, false, 1).valid);
        work = base; work.rejoin = -1;
        assert(!opportunity(100, 2, true, work, false).valid);
        work = base; work.recovery = std::numeric_limits<float>::infinity();
        assert(!opportunity(100, 2, true, work, false).valid);
    }
    {
        // Without implemented covered rotation, two health pools cannot be added together.
        assert(60 + 60 > 100);
        assert(!individual_wave_survives(60, 0, 100, 0));
        assert(individual_wave_survives(140, 0, 90, 25));
        assert(!individual_wave_survives(140, 30, 90, 25));
        assert(!individual_wave_survives(140, 25, 90, 25));
        assert(!individual_wave_survives(140, 0, 90, -1));
    }
    {
        // Verified equal-adjacent basic-only policy, conservatively allowing one more camp
        // volley than the native wake/clear witness: 2400/(45+45) rounds up to 27.
        const auto wave = rotate_remaining_wave(1200, 1200, 60, 1, 45, 45, 2400);
        assert(wave.valid && wave.safe && wave.volleys == 27 && wave.escape_reserve == 120);
        assert(wave.damage[0] == 840 && wave.damage[1] == 780);
        assert(wave.hp[0] == 360 && wave.hp[1] == 420);
        assert(!individual_wave_survives(1200, 0, 27 * 60, 120));

        // An asymmetric pair gives the healthier helper the first hit; no equal split shortcut.
        const auto asymmetric = rotate_remaining_wave(240, 360, 60, 1, 45, 45, 270);
        assert(asymmetric.valid && asymmetric.safe && asymmetric.volleys == 3);
        assert(asymmetric.damage[0] == 60 && asymmetric.damage[1] == 120);
        assert(asymmetric.hp[0] == 180 && asymmetric.hp[1] == 240);
        // Reserve equality fails even though neither actor dies; one less volley can pass.
        const auto strict = rotate_remaining_wave(180, 300, 60, 1, 45, 45, 270);
        assert(strict.valid && !strict.safe && strict.hp[0] == strict.escape_reserve);
        const auto fewer = rotate_remaining_wave(180, 300, 60, 1, 45, 45, 180);
        assert(fewer.valid && fewer.safe && fewer.damage[0] == 0 && fewer.damage[1] == 120);
        const auto boundary = rotate_remaining_wave(180, 179, 60, 1, 45, 45, 90);
        const auto above = rotate_remaining_wave(180.2f, 179, 60, 1, 45, 45, 90);
        assert(boundary.valid && boundary.volleys == 1 && !boundary.safe);
        assert(boundary.hp[0] == boundary.escape_reserve);
        assert(above.valid && above.safe);

        // The native executor uses a 0.1 HP tie band and the leader wins ties.
        const auto tie = rotate_remaining_wave(300, 300.05f, 60, 1, 45, 45, 90);
        const auto helper = rotate_remaining_wave(300, 300.2f, 60, 1, 45, 45, 90);
        assert(tie.valid && tie.safe && tie.damage[0] == 60 && tie.damage[1] == 0);
        assert(helper.valid && helper.safe && helper.damage[0] == 0 && helper.damage[1] == 60);
        const auto mitigation = rotate_remaining_wave(1200, 1200, 60, 0.5f, 45, 45, 2400);
        assert(mitigation.valid && mitigation.safe && mitigation.escape_reserve == 60);
        assert(mitigation.damage[0] == 420 && mitigation.damage[1] == 390);
    }
    {
        // Counts are bounded rather than truncated into an optimistic certificate.
        const auto limit = rotate_remaining_wave(10000, 10000, 60, 1, 45, 45, 5400);
        assert(limit.valid && limit.safe && limit.volleys == 60);
        assert(!rotate_remaining_wave(10000, 10000, 60, 1, 45, 45, 5400.5f).valid);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (int changed = 0; changed < 7; ++changed)
            for (float invalid : {-1.0f, 0.0f, nan, inf}) {
                float input[] = {1200, 1200, 60, 1, 45, 45, 2400};
                input[changed] = invalid;
                const auto bad = rotate_remaining_wave(input[0], input[1], input[2], input[3],
                    input[4], input[5], input[6]);
                assert(!bad.valid && !bad.safe);
            }
        assert(!rotate_remaining_wave(1200, 1200, std::numeric_limits<float>::max(),
            2, 45, 45, 90).valid);
    }
    std::puts("party model: ownership, admission, cost, exposure and conditional rotation passed");
}
