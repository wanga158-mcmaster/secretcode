#include "../src/joint_model.h"
#include <cassert>
#include <iostream>

using namespace mb::joint;

static void packets()
{
    for (int g : { 0, 31, 62 })
        for (int until : { 0, 2047, 2048, 65535 })
        {
            Packet p; p.group = g; p.until = until;
            Wire b{}; assert(encode(p, b));
            assert(b[0] == g + 1 && b[1] == (until & 255) && b[2] == (until >> 8));
            const auto q = decode(b); assert(q.tag == Tag::news && q.group == g && q.until == until);
        }
    for (int r : { 0, 2000, 2047 })
        for (int owner = 0; owner < 5; ++owner)
            for (int urgency = 0; urgency < 4; ++urgency)
            {
                Packet p; p.tag = Tag::request; p.group = 62; p.round = r; p.owner = owner; p.urgency = urgency;
                Wire b{}; assert(encode(p, b)); const auto q = decode(b);
                assert(q.round == r && q.owner == owner && q.urgency == urgency && q.group == 62);
                p.tag = Tag::abort; assert(encode(p, b)); assert(decode(b).tag == Tag::abort);
            }
    for (int mask = 1; mask < 32; ++mask)
    {
        Packet p; p.tag = Tag::commit; p.group = 0; p.deadline = 2047; p.mask = static_cast<std::uint8_t>(mask);
        Wire b{}; assert(encode(p, b)); const auto q = decode(b); assert(q.mask == mask && q.deadline == 2047);
    }
    Packet p; Wire b{ 4, 5, 6 }; p.group = 63; assert(!encode(p, b)); assert((b == Wire{ 4, 5, 6 }));
    p.group = 0; p.until = 65536; assert(!encode(p, b));
    p.tag = Tag::request; p.round = 2048; p.owner = 0; assert(!encode(p, b));
    p.round = 5; p.owner = 5; assert(!encode(p, b));
    p.tag = Tag::commit; p.deadline = 2048; p.mask = 3; assert(!encode(p, b));
    p.deadline = 100; p.mask = 0; assert(!encode(p, b));
    assert(supported(5, 63, 2047, 8, true));
    assert(!supported(6, 63, 2000, 8, true)); assert(!supported(5, 64, 2000, 8, true));
    assert(!supported(5, 63, 2048, 8, true)); assert(!supported(5, 63, 2000, 9, true));
    assert(!supported(5, 63, 2000, 8, false));
}

static void bids()
{
    assert(!decode_bid(0).valid); // atomic reset excludes a missed bidder
    for (int eta = 0; eta < 15; ++eta)
        for (float rate : { 0.0f, 1.0f, 1.01f, 2.0f, 3.99f, 8.0f })
        {
            const auto raw = encode_bid(eta, rate); const auto b = decode_bid(raw);
            assert((raw >> 6) == 3 && b.valid && b.eligible && b.eta == eta);
            assert(price_upper(b.price) >= rate);
        }
    assert(!decode_bid(encode_bid(0, 8.01f)).eligible);
    assert(!decode_bid(encode_bid(15, 1.0f)).eligible);
    assert(!decode_bid(encode_bid(0, 0.0f, false)).eligible);
    assert(!decode_bid(encode_bid(0, std::numeric_limits<float>::quiet_NaN())).eligible);
}

static void independent_actor_phases()
{
    // Five independent instances; actor2 requests midround10, after0/1 acted.
    // Slots start as valid-looking old idle data: the atomic reset must erase it.
    std::array<PhaseClock, 5> actors{};
    std::array<std::uint8_t, 5> slots{ 0xc1, 0xd2, 0xe3, 0xf4, 0xc5 };
    std::array<std::uint8_t, 5> normal{ 1, 2, 3, 4, 5 };
    Packet req; req.tag = Tag::request; req.group = 7; req.round = 10; req.owner = 2; req.urgency = 1;
    slots.fill(0); // same atomic publication as req
    for (int i = 2; i < 5; ++i)
    {
        assert(actors[i].note(req, 10)); assert(actors[i].phase(10) == Phase::bid);
        slots[i] = encode_bid(5 - i, 1.0f);
    }
    assert(!decode_bid(slots[0]).valid && !decode_bid(slots[1]).valid);
    for (int i = 0; i < 5; ++i)
    {
        assert(actors[i].note(req, 11)); assert(actors[i].phase(11) == Phase::bid);
        assert(!actors[i].owner_decides(11, i));
        slots[i] = encode_bid(5 - i, 1.0f, i != 4); // actor4 becomes unavailable
    }
    // In round12 earlier0/1 hold bids; they cannot restore before owner2 reads.
    for (int i = 0; i < 2; ++i)
    {
        assert(actors[i].note(req, 12)); assert(actors[i].phase(12) == Phase::bid);
        slots[i] = encode_bid(5 - i, 1.0f);
    }
    assert(actors[2].owner_decides(12, 2));
    for (int i = 0; i < 4; ++i) assert(decode_bid(slots[i]).eligible);
    assert(!decode_bid(slots[4]).eligible);
    Packet commit; commit.tag = Tag::commit; commit.group = 7; commit.deadline = 17; commit.mask = 7;
    for (int i = 2; i < 5; ++i)
    {
        assert(actors[i].note(commit, 12)); assert(actors[i].phase(12) == Phase::restore);
        slots[i] = normal[i];
    }
    // Later actors restored already, but0/1 receive COMMIT on next ownerturn.
    for (int i = 0; i < 5; ++i)
    {
        assert(actors[i].note(commit, 13)); assert(actors[i].phase(13) == Phase::restore);
        slots[i] = normal[i];
    }
    for (int i = 0; i < 5; ++i)
    {
        assert(actors[i].phase(14) == Phase::normal); assert(slots[i] == normal[i]);
        assert(actors[i].issue == 12); assert(actors[i].restore_until == 13);
    }
    PhaseClock missing_context; assert(missing_context.note(commit, 13));
    assert(missing_context.phase(14) == Phase::restore && missing_context.phase(15) == Phase::normal);
    assert(missing_context.note(commit, 15)); // repeated packet must not renew its lease
    assert(missing_context.phase(15) == Phase::normal);
    Packet competing = req; competing.owner = 1;
    PhaseClock first; assert(first.note(req, 10)); assert(!first.note(competing, 10));
    // Dead/missing owner leaves no result: everyone restores on r+3, normal r+4.
    assert(first.phase(12) == Phase::bid && first.phase(13) == Phase::restore && first.phase(14) == Phase::normal);
    Packet abort = req; abort.tag = Tag::abort;
    assert(first.note(abort, 13)); assert(first.phase(13) == Phase::restore && first.phase(14) == Phase::normal);
    PhaseClock late; assert(!late.note(req, 14)); assert(!late.note(abort, 14));
    assert(!late.note(req, 9)); // future absolute request stamps are not modulo ages
}

static void exact_news()
{
    PendingNews q; Packet p;
    assert(q.push(2, 2222)); assert(q.push(5, 65535)); assert(q.push(2, 2221)); assert(q.push(2, 2223));
    assert(q.pop(2000, p) && p.group == 2 && p.until == 2223);
    Wire b{}; assert(encode(p, b)); assert((b == Wire{ 3, 0xaf, 8 })); // unchanged exact C2 packet
    assert(q.pop(2000, p) && p.group == 5 && p.until == 65535); assert(!q.pop(2000, p));
    assert(q.push(1, 10)); assert(q.push(3, 30)); assert(q.pop(11, p) && p.group == 3 && p.until == 30);
    // Every pending cleared group survives the lease, even if all63 were seen.
    for (int g = 0; g < 63; ++g) assert(q.push(g, 3000 + g));
    for (int g = 0; g < 63; ++g) assert(q.pop(2000, p) && p.group == g && p.until == 3000 + g);
    assert(!q.pop(2000, p));
}

static Input profitable()
{
    // Generic planner fixture: its supplied unit prices are intentionally independent
    // of the requested reward. It is not a witness that runtime farm_price admits it.
    Input in; in.now = 20; in.deadline = 28; in.requester = 0; in.roster_size = 5;
    in.reward = 150.0f; in.camp_hp = 100.0f; in.camp_dps = 5.0f; in.requester_hp = 100.0f;
    for (int i = 0; i < 5; ++i) { in.bids[i] = decode_bid(encode_bid(i, 1.0f)); in.dps[i] = 20.0f; in.hp[i] = 100.0f; }
    return in;
}

static void requested_target_price_coupling()
{
    // Flat-map, one living camp, five healthy actors on distinct tiles at the same
    // exact root-field distance A. The requested target is included in every actor's
    // farm bound with common valuation R. Runtime farm_price uses rectangle distance
    // minus one; for this one-tile camp its travel lower bound is A-1. No passives,
    // other targets, overflow, damage, missing bids, deadline shortage or budget gate
    // are needed for rejection. Only the coupled reward/price accounting remains.
    constexpr int capture = 5;
    constexpr int clear = 1;
    for (float reward : { 5.0f, 10.0f, 20.0f, 40.0f })
        for (int arrival = 1; arrival <= 14; ++arrival)
        {
            const int farm_travel = arrival - 1;
            const float farm_rate = reward / static_cast<float>(farm_travel + capture);
            const auto bid = decode_bid(encode_bid(arrival, farm_rate));
            assert(bid.valid && bid.eligible && bid.eta == arrival); // no overflow/ETA failure
            const float price = price_upper(bid.price);
            assert(price >= farm_rate);
            const float weaker_price_bound = reward / static_cast<float>(arrival + capture);
            assert(price >= weaker_price_bound);

            Input in;
            in.now = 20; in.deadline = in.now + arrival; in.requester = 0;
            in.roster_size = 5; in.alive_mask = 31;
            in.capture_rounds = capture; in.horizon = arrival + clear + capture;
            in.completion_rounds = clear + capture;
            in.reward = reward; in.camp_hp = 1.0f; in.camp_dps = 0.0f;
            in.requester_hp = 1000.0f; in.threat_dps = 0.0f;
            for (int i = 0; i < 5; ++i)
            {
                in.bids[i] = bid; in.dps[i] = 10.0f; in.hp[i] = 1000.0f;
            }
            // Even before upward price quantization, every permitted party costs more
            // than the single reward. This lower bound is independent of mask identity
            // because all actors share A and R. Upward bins can only increase the cost.
            for (int party = 2; party <= 5; ++party)
            {
                const int reserved = party * (arrival + clear) + capture;
                const float cost_lower = weaker_price_bound * static_cast<float>(reserved);
                const float charged = price * static_cast<float>(reserved);
                assert(cost_lower > reward);
                assert(charged + 0.0001f >= cost_lower);
            }
            const auto choice = choose(in);
            assert(choice.masks_evaluated == 31);
            assert(choice.mask == 0 && choice.value == 0.0f);
        }
}

static void whole_allocations()
{
    auto in = profitable(); auto c = choose(in);
    assert(c.mask != 0 && (c.mask & 1) && members(c.mask) >= 2 && c.masks_evaluated == 31);
    assert(c.deadline == in.now + c.arrival && c.deadline <= in.deadline);
    assert(c.task_reward == in.reward); // never multiplied by party size
    assert(std::abs(c.value - (c.task_reward - c.recovery_cost - c.displaced_farm)) < 0.001f);
    in = profitable(); in.reward = 0.0f; in.camp_dps = 0.0f; c = choose(in); assert(c.mask == 0 && c.value == 0.0f);
    in = profitable(); in.alive_mask = 1; c = choose(in); assert(c.mask == 0); // dead allies excluded
    in = profitable(); in.alive_mask = 30; c = choose(in); assert(c.mask == 0); // dead requester aborts
    in = profitable(); for (int i = 1; i < 5; ++i) in.bids[i] = decode_bid(0); assert(choose(in).mask == 0); // missing/stale reset
    in = profitable(); in.bids[0] = decode_bid(encode_bid(0, 8.01f)); assert(choose(in).mask == 0);
    in = profitable(); in.deadline = in.now; assert(choose(in).mask == 0);
    in = profitable(); in.deadline = in.now + 1; c = choose(in); assert(c.mask == 3 && c.arrival == 1);
    in = profitable(); in.threat_dps = 100.0f; assert(choose(in).mask == 0); // arrives after lethal damage
    in = profitable(); in.camp_dps = 10000.0f; assert(choose(in).mask == 0); // unsafe collective pool
    in = profitable(); in.reward = std::numeric_limits<float>::infinity(); assert(choose(in).mask == 0);
    in = profitable(); in.deadline = 2048; assert(choose(in).mask == 0);
    in = profitable(); in.roster_size = 6; assert(choose(in).mask == 0);
    in = profitable(); in.horizon = 1; assert(choose(in).mask == 0); // cannot complete before game-end expiry
    in = profitable(); in.completion_rounds = 5; assert(choose(in).mask == 0); // clear+capture exceeds shared expiry
    in = profitable(); in.reward = 0.0f; in.requester_hp = 20.0f;
    assert(choose(in).mask == 0); // no unsupported avoided-solo-death reward can beat all-FARM
    // More members do not create more reward: make all heroes identical. With
    // this camp no extra clear round is saved, so the cheapest useful pair wins.
    in = profitable(); in.camp_hp = 20.0f;
    for (int i = 0; i < 5; ++i) in.bids[i] = decode_bid(encode_bid(0, 1.0f));
    c = choose(in); assert(c.mask == 3 && c.task_reward == 150.0f);
}

static void executor_contracts()
{
    assert(!assembled(7, 31, 3)); assert(!assembled(7, 3, 7)); assert(assembled(7, 31, 7));
    assert(!may_wake(false, false, false)); assert(may_wake(false, true, false));
    assert(!may_wake(false, true, true)); assert(may_wake(true, false, true));
    assert(!accept_effect(false, false, false)); assert(accept_effect(true, false, false));
    assert(accept_effect(false, true, false)); assert(accept_effect(false, false, true));
    assert(!may_execute(true, false, false, false, true)); assert(!may_execute(false, true, false, false, true));
    assert(!may_execute(false, false, true, false, true)); assert(!may_execute(false, false, false, true, true));
    assert(!may_execute(false, false, false, false, false)); assert(may_execute(false, false, false, false, true));
}

int main()
{
    packets(); bids(); independent_actor_phases(); exact_news(); requested_target_price_coupling(); whole_allocations(); executor_contracts();
    std::cout << "joint protocol/planner tests passed\n";
}
