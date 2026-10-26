#include <cassert>
#include <cstdio>
#include "coalition.h"

using namespace mb::coalition;

int main() {
    {
        // Preserve all existing champion intent slots, including Cassi/Issac's sixth body.
        static_assert(has_room(8, 6));
        static_assert(has_room(5, 3));
        static_assert(!has_room(8, 7));
        static_assert(!has_room(1, 3));
        static_assert(!has_room(1, 0));
    }
    {
        // Initial zero memory is empty, not enemy zero in round zero.
        constexpr auto empty = FocusRecord::decode({0, 0});
        static_assert(!empty.valid());
        static_assert(!empty.fresh(0));
        constexpr FocusRecord first{0, 0};
        static_assert(first.valid());
        static_assert(FocusRecord::decode(first.encode()).enemy == 0);
        static_assert(FocusRecord::decode(first.encode()).round == 0);
        static_assert(FocusRecord::decode(first.encode()).fresh(0));
    }
    {
        // Every enemy identity survives encoding across byte and former modulo boundaries.
        for (int enemy = 0; enemy <= 7; ++enemy)
            for (int round : {0, 31, 32, 255, 256, 1023, 1024, 2000, 2047}) {
                const FocusRecord original{enemy, round};
                const auto decoded = FocusRecord::decode(original.encode());
                assert(decoded.enemy == enemy && decoded.round == round);
                assert(decoded.fresh(round) && decoded.fresh(round + 1));
                assert(!decoded.fresh(round + 2));
                assert(!decoded.fresh(round + 32));
                assert(!decoded.fresh(round + 256));
                assert(!decoded.fresh(round - 1));
            }
    }
    {
        // Refuse a future format/range instead of truncating to a different valid request.
        for (const auto invalid : {FocusRecord{-1, 1}, FocusRecord{8, 1}, FocusRecord{0, -1}, FocusRecord{0, 2048}})
            assert(!FocusRecord::decode(invalid.encode()).valid());
        auto reserved = FocusRecord{2, 50}.encode();
        reserved[1] |= 0x40u;
        assert(!FocusRecord::decode(reserved).valid());
    }
    {
        // A shared contact is useful only with an ordinary basic legal from the current tile.
        static_assert(current_basic_eligible(true, true, true, 3, 3, true));
        static_assert(current_basic_eligible(true, true, true, 1, 1, true));
        static_assert(!current_basic_eligible(true, true, true, 4, 3, true));
        static_assert(!current_basic_eligible(true, true, false, 3, 3, true));
        static_assert(!current_basic_eligible(false, true, true, 3, 3, true));
        static_assert(!current_basic_eligible(true, false, true, 3, 3, true));
        static_assert(!current_basic_eligible(true, true, true, 3, 3, false));
        static_assert(!current_basic_eligible(true, true, true, -1, 3, true));
        static_assert(!current_basic_eligible(true, true, true, 0, -1, true));
    }
    {
        const FocusRecord record{3, 256};
        const LocalOpportunity ready{3, true, true, true, true, true};
        assert(can_adopt(record, 256, ready));
        assert(can_adopt(record, 257, ready));
        assert(!can_adopt(record, 258, ready));
        assert(!can_adopt(record, 255, ready));
        auto local = ready;
        local.enemy = 4;
        assert(!can_adopt(record, 256, local));
        local = ready; local.personally_visible = false;
        assert(!can_adopt(record, 256, local));
        local = ready; local.alive = false;
        assert(!can_adopt(record, 256, local));
        local = ready; local.can_contribute = false;
        assert(!can_adopt(record, 256, local));
        local = ready; local.in_current_reach = false;
        assert(!can_adopt(record, 256, local));
        local = ready; local.locally_safe = false;
        assert(!can_adopt(record, 256, local));
    }
    std::puts("coalition protocol: bounds, identity, age, wrap and local adoption passed");
}
