#include "party_record.h"
#include <cassert>
#include <cstdio>
#include <limits>

using mb::party::LocalRecord;

int main() {
    {
        LocalRecord first{0, 0, 1, 0, 0};
        assert(first.valid() && first.fresh(0, 0));
        assert((first.encode() == std::array<std::uint8_t, 3>{0xc1, 0x00, 0x00}));
        const auto decoded = LocalRecord::decode(first.encode());
        assert(decoded.same_duty(first) && decoded.ack == 0);
        LocalRecord last{62, 3, 4, 2047, 3};
        assert(last.valid());
        assert((last.encode() == std::array<std::uint8_t, 3>{0x3f, 0xfe, 0xff}));
        const auto edge = LocalRecord::decode(last.encode());
        assert(edge.same_duty(last) && edge.ack == 3);
    }
    {
        // ACKs cannot impersonate the peer and do not change the duty's identity.
        LocalRecord offer{17, 1, 4, 300, 0};
        auto accepted = offer;
        accepted.acknowledge(1);
        assert(accepted.acknowledged(1) && !accepted.acknowledged(4));
        assert(!accepted.acknowledged(0) && !accepted.contains(3));
        accepted.acknowledge(3);
        assert(accepted.ack == 1);
        accepted.acknowledge(4);
        assert(accepted.acknowledged(1) && accepted.acknowledged(4));
        assert(accepted.same_duty(offer));
        assert(LocalRecord::decode(accepted.encode()).same_duty(offer));
        assert(LocalRecord::decode(accepted.encode()).ack == 3);
        auto changed = offer;
        ++changed.epoch;
        assert(!changed.same_duty(offer));
        changed = offer; ++changed.group;
        assert(!changed.same_duty(offer));
        changed = offer; changed.helper = 3;
        assert(!changed.same_duty(offer));
        assert(!LocalRecord{}.same_duty(LocalRecord{}));
    }
    {
        LocalRecord duty{3, 0, 2, 1983, 0};
        assert(duty.fresh(1983) && duty.fresh(2047));
        assert(!duty.fresh(2048) && !duty.fresh(1982));
        assert(!duty.fresh(-1) && !duty.fresh(1983, -1));
        assert(!duty.fresh(1983 + 2048));
        assert(!duty.fresh(std::numeric_limits<int>::max()));
        assert(!duty.fresh(std::numeric_limits<int>::min()));
        assert(duty.fresh(1984, 1) && !duty.fresh(1985, 1));
    }
    {
        // Reject malformed records rather than clipping them into another duty.
        const LocalRecord invalid[] = {{-1, 0, 1, 0, 0}, {63, 0, 1, 0, 0},
            {0, -1, 1, 0, 0}, {0, 1, 1, 0, 0}, {0, 4, 1, 0, 0},
            {0, 0, 5, 0, 0}, {0, 0, 1, -1, 0}, {0, 0, 1, 2048, 0},
            {0, 0, 1, 0, 4}};
        for (auto record : invalid) {
            assert(!record.valid());
            assert((record.encode() == std::array<std::uint8_t, 3>{}));
            assert(!record.contains(0) && !record.acknowledged(0));
            record.acknowledge(0);
            assert(!record.valid());
        }
        assert(!LocalRecord::decode({}).valid());
        for (unsigned mask : {0u, 1u, 7u, 31u}) {
            const unsigned word = 1u | (mask << 6) | (10u << 11) | (3u << 22);
            assert(!LocalRecord::decode({static_cast<std::uint8_t>(word),
                static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16)}).valid());
        }
        // A populated pair/epoch with the explicit no-duty group code is invalid.
        assert(!LocalRecord::decode({0xc0, 0x00, 0xc0}).valid());
    }
    {
        // Exhaust the ten legal pairs, all groups and all four ACK states at both
        // epoch boundaries. This catches overlap among the packed bit fields.
        for (int leader = 0; leader < 5; ++leader)
            for (int helper = leader + 1; helper < 5; ++helper)
                for (int group = 0; group <= 62; ++group)
                    for (int epoch : {0, 2047})
                        for (unsigned ack = 0; ack < 4; ++ack) {
                            const LocalRecord record{group, leader, helper, epoch, ack};
                            const auto decoded = LocalRecord::decode(record.encode());
                            assert(decoded.same_duty(record) && decoded.ack == record.ack);
                        }
    }
    std::puts("local party record: exact packing, ACK ownership, identity and absolute expiry passed");
}
