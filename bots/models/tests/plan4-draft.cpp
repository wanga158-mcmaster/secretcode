// Pure deterministic draft fixtures; these do not run games or prove strength.
#include "../common/draft.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace plan4::drafting;
static unsigned checks = 0;
#define CHECK(e) do { ++checks; if (!(e)) { std::fprintf(stderr, "check %u line %d: %s\n", checks, __LINE__, #e); std::exit(1); } } while (false)

static constexpr std::uint16_t bit(Pick pick) {
    return std::uint16_t(1u << static_cast<unsigned>(pick));
}
static constexpr auto old_mask = std::uint16_t(all_mask & ~bit(Pick::heart_of_ruin));
static constexpr std::array<Pick, 12> fallback{
    Pick::tavi, Pick::varg, Pick::vendetta, Pick::vesper_vale, Pick::marcella,
    Pick::ryth, Pick::ren, Pick::prism, Pick::stellar, Pick::willow,
    Pick::cassi_and_issac, Pick::heart_of_ruin
};
static constexpr std::array<std::array<Pick, 5>, 17> prefixes{{
    {Pick::tavi, Pick::varg, Pick::vendetta, Pick::prism, Pick::marcella},
    {Pick::cassi_and_issac, Pick::varg, Pick::ryth, Pick::willow, Pick::marcella},
    {Pick::vesper_vale, Pick::tavi, Pick::vendetta, Pick::heart_of_ruin, Pick::prism},
    {Pick::vendetta, Pick::vesper_vale, Pick::ren, Pick::prism, Pick::tavi},
    {Pick::heart_of_ruin, Pick::ryth, Pick::varg, Pick::marcella, Pick::willow},
    {Pick::vendetta, Pick::marcella, Pick::tavi, Pick::ren, Pick::vesper_vale},
    {Pick::vendetta, Pick::varg, Pick::marcella, Pick::willow, Pick::stellar},
    {Pick::marcella, Pick::vesper_vale, Pick::vendetta, Pick::tavi, Pick::prism},
    {Pick::marcella, Pick::varg, Pick::heart_of_ruin, Pick::willow, Pick::tavi},
    {Pick::prism, Pick::tavi, Pick::vesper_vale, Pick::stellar, Pick::willow},
    {Pick::stellar, Pick::tavi, Pick::prism, Pick::cassi_and_issac, Pick::willow},
    {Pick::ren, Pick::marcella, Pick::tavi, Pick::vesper_vale, Pick::heart_of_ruin},
    {Pick::tavi, Pick::varg, Pick::vendetta, Pick::prism, Pick::marcella},
    {Pick::tavi, Pick::vesper_vale, Pick::marcella, Pick::prism, Pick::vendetta},
    {Pick::cassi_and_issac, Pick::vendetta, Pick::varg, Pick::prism, Pick::vesper_vale},
    {Pick::vesper_vale, Pick::vendetta, Pick::tavi, Pick::marcella, Pick::ren},
    {Pick::marcella, Pick::tavi, Pick::vesper_vale, Pick::heart_of_ruin, Pick::varg}
}};
static constexpr std::array<const char*, 17> names{
    "reference", "foundry", "governor", "habitat", "repairman", "archipelago",
    "timetable", "stewards", "bulkheads", "chronicle", "quotient", "counterpart",
    "kernel", "capital", "commons", "metronome", "covenant"
};
static void complete_orders() {
    for (int profile = 0; profile < profile_count; ++profile) {
        const auto value = static_cast<Profile>(profile);
        const auto& actual = order(value);
        std::uint16_t seen = 0;
        for (int index = 0; index < pick_count; ++index) {
            const int pick = static_cast<int>(actual[index]);
            CHECK(pick >= 0 && pick < pick_count);
            CHECK((seen & bit(actual[index])) == 0);
            seen |= bit(actual[index]);
        }
        CHECK(seen == all_mask);
        for (int index = 0; index < 5; ++index) CHECK(actual[index] == prefixes[profile][index]);
        int tail = 5;
        for (const auto candidate : fallback) {
            bool preferred = false;
            for (const auto first : prefixes[profile]) preferred |= first == candidate;
            if (!preferred) CHECK(actual[tail++] == candidate);
        }
        CHECK(tail == pick_count);
        CHECK(std::strcmp(profile_name(value), names[profile]) == 0);
    }
    CHECK(order(Profile::reference) == order(Profile::kernel));
    CHECK(order(static_cast<Profile>(-1)) == order(Profile::reference));
    CHECK(std::strcmp(profile_name(Profile::count), "unknown") == 0);
}

static std::uint16_t finish(Profile profile, int size, std::uint16_t mask, int capacity = 5) {
    Input input;
    input.team_size = size;
    input.available_mask = mask;
    input.actor_capacity = capacity;
    std::uint16_t seen = 0;
    for (int index = 0; index < size; ++index) {
        const auto first = choose(profile, input);
        const auto repeated = choose(profile, input);
        CHECK(first.valid && first.has_pick);
        CHECK(first.valid == repeated.valid && first.has_pick == repeated.has_pick && first.pick == repeated.pick);
        CHECK((mask & bit(first.pick)) != 0);
        CHECK((seen & bit(first.pick)) == 0);
        seen |= bit(first.pick);
        input.selected[input.selected_count++] = first.pick;
        // A short draft containing the duo still advances by one selection.
        CHECK(input.selected_count == index + 1);
    }
    const auto done = choose(profile, input);
    CHECK(done.valid && !done.has_pick);
    return seen;
}
static void all_profiles_and_sizes() {
    std::uint16_t actual_picks = 0;
    for (int profile = 0; profile < profile_count; ++profile) {
        const auto value = static_cast<Profile>(profile);
        for (const auto mask : {old_mask, all_mask}) {
            for (const int size : {3, 4, 5}) {
                const auto selected = finish(value, size, mask);
                if (mask == all_mask) actual_picks |= selected;
                if (size == 5) CHECK((selected & bit(Pick::cassi_and_issac)) == 0);
            }
        }
        // Runtime settings are authoritative outside the usual 3..5 range.
        for (int size = 1; size <= pick_count; ++size)
            finish(value, size, all_mask);
        CHECK(finish(value, 11, old_mask) == old_mask);
    }
    CHECK(actual_picks == all_mask);
    for (const auto profile : {Profile::foundry, Profile::commons}) {
        for (const int size : {3, 4}) {
            Input input;
            input.team_size = size;
            CHECK(choose(profile, input).pick == Pick::cassi_and_issac);
            CHECK((finish(profile, size, all_mask) & bit(Pick::cassi_and_issac)) != 0);
        }
        Input input;
        input.team_size = 5;
        CHECK(choose(profile, input).pick != Pick::cassi_and_issac);
        input.actor_capacity = 6;
        CHECK(choose(profile, input).pick == Pick::cassi_and_issac);
    }
}
static void constrained_pools_and_progress() {
    // Every nonempty availability pool can finish, even if capacity is smaller.
    for (unsigned mask = 1; mask <= all_mask; ++mask) {
        int size = 0;
        for (int index = 0; index < pick_count; ++index) size += (mask >> index) & 1u;
        const auto selected = finish(Profile::foundry, size, std::uint16_t(mask), 1);
        CHECK(selected == mask);
    }
    Input input;
    input.team_size = 1;
    input.actor_capacity = 1;
    input.available_mask = bit(Pick::cassi_and_issac);
    const auto only_duo = choose(Profile::foundry, input);
    CHECK(only_duo.valid && only_duo.has_pick && only_duo.pick == Pick::cassi_and_issac);
    // Own prefix exhaustion falls through to the common tail.
    input = {};
    input.team_size = 6;
    input.selected_count = 5;
    input.selected = {Pick::tavi, Pick::varg, Pick::vendetta, Pick::prism, Pick::marcella};
    CHECK(choose(Profile::reference, input).pick == Pick::vesper_vale);
    // A previously selected duo does not invalidate the remaining selections.
    input = {};
    input.team_size = 5;
    input.selected_count = 1;
    input.selected[0] = Pick::cassi_and_issac;
    const auto next = choose(Profile::foundry, input);
    CHECK(next.valid && next.has_pick && next.pick == Pick::varg);
    // Tail-only availability works when every preferred choice is unavailable.
    input = {};
    input.team_size = 1;
    input.available_mask = bit(Pick::willow) | bit(Pick::heart_of_ruin);
    CHECK(choose(Profile::reference, input).pick == Pick::willow);
}
static void malformed_inputs() {
    Input valid;
    valid.team_size = 3;
    for (const auto profile : {static_cast<Profile>(-1), Profile::count, static_cast<Profile>(99)}) {
        const auto result = choose(profile, valid);
        CHECK(!result.valid && !result.has_pick);
    }
    for (int mutation = 0; mutation < 16; ++mutation) {
        auto input = valid;
        switch (mutation) {
        case 0: input.team_size = -1; break;
        case 1: input.team_size = 0; break;
        case 2: input.team_size = pick_count + 1; break;
        case 3: input.team_size = std::numeric_limits<int>::max(); break;
        case 4: input.selected_count = -1; break;
        case 5: input.selected_count = 4; break;
        case 6: input.selected_count = std::numeric_limits<int>::max(); break;
        case 7: input.selected_count = 2; input.selected = {Pick::tavi, Pick::tavi}; break;
        case 8: input.selected_count = 1; input.selected[0] = static_cast<Pick>(-1); break;
        case 9: input.selected_count = 1; input.selected[0] = Pick::count; break;
        case 10: input.selected_count = 1; input.selected[0] = Pick::heart_of_ruin; input.available_mask = old_mask; break;
        case 11: input.available_mask = std::uint16_t(all_mask | (1u << 12)); break;
        case 12: input.available_mask = 0; break;
        case 13: input.team_size = 12; input.available_mask = old_mask; break;
        case 14: input.actor_capacity = 0; break;
        default: input.actor_capacity = -1; break;
        }
        const auto result = choose(Profile::reference, input);
        CHECK(!result.valid && !result.has_pick);
    }
    // Unused selected slots are not own picks and must not enter the filter.
    valid.selected[11] = Pick::count;
    CHECK(choose(Profile::reference, valid).valid);
    // Completed input still validates its own pick identities and uniqueness.
    valid.selected_count = valid.team_size;
    valid.selected = {Pick::tavi, Pick::varg, Pick::vendetta};
    CHECK(choose(Profile::reference, valid).valid && !choose(Profile::reference, valid).has_pick);
    valid.selected[2] = Pick::tavi;
    CHECK(!choose(Profile::reference, valid).valid);
}
int main() {
    complete_orders();
    all_profiles_and_sizes();
    constrained_pools_and_progress();
    malformed_inputs();
    std::printf("plan4 draft: %u checks passed\n", checks);
}
