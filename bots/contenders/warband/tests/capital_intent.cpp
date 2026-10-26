#include "../src/capital_intent.h"
#include <cstdio>
#include <cstdint>
#include <limits>

using namespace wb::capital::intent;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (false)

void all_actors_and_preservation() {
    CHECK(sizeof(std::uint64_t) == 8);
    CHECK(actors * slot_bits + magic_bits == 64);
    std::uint64_t payload = 0;
    for (unsigned i = 0; i < actors; ++i) {
        payload = encode(payload, i, int(i * 15), i & 3U, 6);
        CHECK((payload & magic_mask) == signature);
        for (unsigned j = 0; j <= i; ++j) {
            const auto got = decode(payload, j, 6);
            CHECK(got.present && got.group == int(j * 15) && got.mode == (j & 3U));
        }
    }
    const auto before = payload;
    payload = encode(payload, 2, 62, 3, 7);
    CHECK(decode(payload, 2, 7).present && decode(payload, 2, 7).group == 62);
    CHECK(decode(payload, 2, 7).mode == 3);
    for (unsigned i = 0; i < actors; ++i) if (i != 2)
        CHECK(((payload >> (i * slot_bits)) & slot_mask) == ((before >> (i * slot_bits)) & slot_mask));
    payload = encode(payload, 2, -1, 0, 7);
    CHECK(!decode(payload, 2, 7).present);
    CHECK(decode(payload, 0, 7).present && decode(payload, 4, 7).present);
}

void round_edges() {
    auto payload = encode(0, 4, 0, 2, 7);
    CHECK(decode(payload, 4, 7).present);
    CHECK(decode(payload, 4, 8).present); // 7 -> 0 stamp carry.
    CHECK(!decode(payload, 4, 9).present);
    CHECK(!decode(payload, 4, 6).present);
    CHECK(decode(payload, 4, 15).present); // Explicit eight-round alias.
    payload = encode(payload, 4, 1, 0, 8);
    CHECK(decode(payload, 4, 8).present && decode(payload, 4, 8).group == 1);
    CHECK(decode(payload, 4, 9).present);
    CHECK(!decode(payload, 4, 10).present);
    payload = encode(payload, 0, 2, 1, std::numeric_limits<unsigned>::max());
    CHECK(decode(payload, 0, 0).present); // Unsigned round wrap is modulo eight too.
}

void rejection_and_full_domain() {
    const std::uint64_t foreign = std::numeric_limits<std::uint64_t>::max();
    CHECK(!decode(0, 0, 0).present);
    CHECK(!decode(foreign, 0, 7).present);
    const auto fresh = encode(foreign, 1, 3, 2, 7);
    CHECK(decode(fresh, 1, 7).present);
    CHECK(!decode(fresh, 0, 7).present && !decode(fresh, 4, 7).present);
    CHECK(!decode(fresh, 5, 7).present);
    CHECK(!decode(fresh, std::numeric_limits<unsigned>::max(), 7).present);
    CHECK(encode(fresh, 5, 0, 0, 7) == fresh);
    CHECK(encode(fresh, 0, -2, 0, 7) == fresh);
    CHECK(encode(fresh, 0, 63, 0, 7) == fresh);
    CHECK(encode(fresh, 0, 0, 4, 7) == fresh);
    CHECK(!decode(signature, 0, 0).present);
    for (unsigned actor = 0; actor < actors; ++actor)
        for (int group = 0; group <= 62; ++group)
            for (unsigned mode = 0; mode < 4; ++mode) {
                const auto payload = encode(0, actor, group, mode, 3);
                const auto got = decode(payload, actor, 3);
                CHECK(got.present && got.group == group && got.mode == mode);
                CHECK(decode(payload, actor, 4).present);
                CHECK(!decode(payload, actor, 5).present);
            }
}
} // namespace
int main() {
    all_actors_and_preservation(); round_edges(); rejection_and_full_domain();
    std::printf("capital_intent: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
