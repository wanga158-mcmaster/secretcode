#include "../src/gremlins-policy.h"
static_assert(gremlins::qualifies(true, true, true, 24, 1));
static_assert(!gremlins::qualifies(false, true, true, 44, 1));
static_assert(!gremlins::qualifies(true, false, true, 44, 1));
static_assert(!gremlins::qualifies(true, true, false, 44, 1));
static_assert(!gremlins::qualifies(true, true, true, 23, 1));
static_assert(!gremlins::qualifies(true, true, true, 44, 0));
static_assert(!gremlins::qualifies(true, true, true, 44, 2));
static_assert(gremlins::precedes(44, 5, 9, 847, 24, 0, 1, 1));
static_assert(gremlins::precedes(44, 4, 9, 847, 44, 5, 1, 1));
static_assert(gremlins::precedes(44, 4, 8, 847, 44, 4, 9, 1));
static_assert(gremlins::precedes(44, 4, 8, 847, 44, 4, 8, 859));
static_assert(!gremlins::precedes(44, 4, 8, 847, 44, 4, 8, 847));
static_assert(!gremlins::expired(10, 10, 0));
static_assert(!gremlins::expired(10, 17, 2));
static_assert(gremlins::expired(10, 18, 0));
static_assert(gremlins::expired(10, 11, 3));
static_assert(gremlins::expired(10, 9, 0));
static_assert(!gremlins::cooldown_complete(43, 42));
static_assert(gremlins::cooldown_complete(43, 43));
static_assert(gremlins::cooldown_until(10) == 43);
static_assert(!gremlins::cooldown_complete(gremlins::cooldown_until(10), 42));
static_assert(gremlins::cooldown_complete(gremlins::cooldown_until(10), 43));
static_assert(!gremlins::needs_route_field(true, 44, 44));
static_assert(!gremlins::needs_route_field(true, 44, 43));
static_assert(gremlins::needs_route_field(true, 44, 45));
static_assert(gremlins::needs_route_field(false, 0, 24));
static_assert(!gremlins::precedes(44, 1, 1, 1, 44, 0, 100, 100));
static_assert(!gremlins::precedes(43, 0, 1, 1, 44, 0, 100, 100));
int main() {
    // Exhaustively verify the skipped positive-distance alternatives can never
    // beat an already legal zero-distance placement, regardless of later ties.
    for (unsigned zero_bag = 24; zero_bag <= 144; ++zero_bag)
        for (unsigned remote_bag = 24; remote_bag <= 144; ++remote_bag)
            for (int distance = 1; distance <= 8; ++distance)
                for (unsigned actor = 0; actor < 8; ++actor)
                    if (!gremlins::needs_route_field(true, zero_bag, remote_bag)
                        && gremlins::precedes(remote_bag, distance, actor, 0, zero_bag, 0, 100, 100)) return 1;
}
