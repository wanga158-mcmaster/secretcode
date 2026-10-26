// Data-dump probe (bots/main fastrun validation, D336): prints FNV hashes and sizes of what the raw imports return,
// so the CLI and fastrun can be compared value by value. Lines: "D <name> <status> <hash> <n>".
#include <cambc.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace cambc::detail;

namespace {
alignas(16) std::uint64_t A[4096];
alignas(16) std::uint16_t F[65536];
std::uint32_t WORDS[16384];
std::uint8_t BYTES[65536];

std::uint64_t fnv(const void* p, std::size_t n)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}
void out(const char* name, int status, const void* p, std::size_t n)
{
    std::printf("D %s %d %016llx %zu\n", name, status, static_cast<unsigned long long>(fnv(p, n)), n);
}

void dump_static()
{
    metadata m{};
    out("metadata", sdk_bc_metadata(&m), &m, sizeof m);
    game_settings gs{};
    out("settings", sdk_get_settings(&gs), &gs, sizeof gs);
    std::uint32_t count = 0;
    int st = sdk_map_camp_layout(WORDS, 16384, &count);
    out("layout", st, WORDS, count * 4);
    for (std::uint32_t i = 0; i < count && i < 40; ++i) std::printf("L %u %u\n", i, WORDS[i]);
    st = sdk_map_camp_groups(WORDS, 4096, &count);
    out("groups", st, WORDS, count * 4);
    for (int k = 0; k < 4; ++k)
    {
        char name[32];
        std::snprintf(name, sizeof name, "mask%d", k);
        out(name, sdk_bc_map_mask(k, A), A, m.board_bytes);
    }
    for (int c = 0; c < 13; ++c)
    {
        champion_design d{};
        char name[32];
        std::snprintf(name, sizeof name, "design%d", c);
        out(name, sdk_champion_design(c, &d), &d, sizeof d);
    }
    for (int c = 0; c < 6; ++c)
    {
        camp_design d{};
        char name[32];
        std::snprintf(name, sizeof name, "campdesign%d", c);
        out(name, sdk_camp_design(c, &d), &d, sizeof d);
    }
    for (int i = 0; i < 42; ++i)
    {
        item_info ii{};
        char name[32];
        std::snprintf(name, sizeof name, "item%d", i);
        out(name, sdk_item_info(static_cast<std::uint32_t>(i), &ii), &ii, sizeof ii);
    }
    aabb box{};
    out("spawn0", sdk_spawn_region(0, &box), &box, sizeof box);
    out("spawn1", sdk_spawn_region(1, &box), &box, sizeof box);
}

void dump_champion(std::uint32_t me)
{
    std::uint32_t count = 0;
    out("allies", sdk_allies(WORDS, 16, &count), WORDS, count * 4);
    out("enemies", sdk_enemies(WORDS, 16, &count), WORDS, count * 4);
    champion_info info{};
    out("self", sdk_champion(me, &info), &info, sizeof info);
    for (std::uint32_t id = 0; id < 10; ++id)
    {
        champion_identity ci{};
        char name[32];
        std::snprintf(name, sizeof name, "identity%u", id);
        out(name, sdk_champion_identity(id, &ci), &ci, sizeof ci);
        std::snprintf(name, sizeof name, "champ%u", id);
        out(name, sdk_champion(id, &info), &info, sizeof info);
        for (std::uint32_t s = 0; s < 2; ++s)
        {
            ability_props ap{};
            std::snprintf(name, sizeof name, "props%u_%u", id, s);
            out(name, sdk_champion_ability_props(id, s, &ap), &ap, sizeof ap);
        }
    }
    out("vision", sdk_vision(A), A, 8 * 128);
    out("ally_occ", sdk_ally_occupancy(A), A, 8 * 128);
    out("camp_occ", sdk_camp_occupancy(A), A, 8 * 128);
    std::uint32_t buffs = 0;
    out("buffs", sdk_champion_buff_data(me, reinterpret_cast<buff_data*>(BYTES), 16, &buffs), BYTES, buffs * 12);
    std::uint32_t items[42];
    out("items", sdk_champion_items(me, items), items, sizeof items);
    std::uint32_t cons[4];
    out("consumables", sdk_champion_consumables(me, cons), cons, sizeof cons);
    std::uint32_t round = 0;
    out("round", sdk_bc_round_number(&round), &round, 4);
    std::uint32_t gid = 0, gc = 0;
    sdk_map_camp_groups(&gid, 1, &gc);
    std::uint32_t state = 0;
    out("group0_state", sdk_camp_group_state(gid, &state), &state, 4);
    std::int32_t cool = 0;
    out("group0_cool", sdk_camp_group_cooldown(gid, &cool), &cool, 4);
    std::int32_t yes = 0;
    out("ward_at_self", sdk_ward_at(info.x, info.y, WORDS), WORDS, 4);
    out("has_camp_self", sdk_has_camp(info.x, info.y, &yes), &yes, 4);
    out("champion_at_self", sdk_champion_at(info.x, info.y, WORDS), WORDS, 4);
    std::int32_t cd = 0;
    out("cooldown0", sdk_champion_cooldown(me, 0, &cd), &cd, 4);
    out("cooldown9", sdk_champion_cooldown(me, 9, &cd), &cd, 4);
    char err[256] = {};
    const int n = sdk_bc_get_last_error(err, 255);
    std::printf("E %d %s\n", n, err);
}
} // namespace

int main()
{
    int turn = 0;
    while (true)
    {
        if (cambc::is_draft())
        {
            if (turn == 0) dump_static();
            for (int pick = 0; pick < 12; ++pick)
                if (sdk_draft_champion(pick) == 0) break;
        }
        else if (turn == 0 || turn == 3)
            dump_champion(static_cast<std::uint32_t>(sdk_my_champion()));
        ++turn;
        cambc::end_turn();
    }
}
