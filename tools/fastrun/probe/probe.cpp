// Fuel probe bot (bots/main fastrun calibration, D336): measures the fuel of single raw imports between two
// fuel_remaining() readings and prints "P <name> <fuel>" lines. Run the same game in the CLI and in fastrun; the
// per-name difference is the host's fuel charge for that import.
#include <cambc.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <wasi/api.h>
#include <cstdlib>
#include <unistd.h>

using namespace cambc::detail;

namespace {
alignas(16) std::uint64_t A[4096], B[4096], C[4096], D[4096];
alignas(16) std::uint16_t F[65536], G[65536];
alignas(16) std::uint8_t BYTES[65536];
std::uint32_t WORDS[16384];
int printed = 0;

const char* g_names[1024];
long long g_costs[1024];
long long g_after[1024];
int g_n = 0;
#define PROBE(name, expr)                                               \
    do {                                                                \
        const auto f0 = sdk_fuel_remaining();                           \
        expr;                                                           \
        const auto f1 = sdk_fuel_remaining();                           \
        if (g_n < 1024) { g_names[g_n] = strdup(name); g_costs[g_n] = f0 - f1; g_after[g_n] = f1; ++g_n; } \
        ++printed;                                                      \
    } while (0)

// Results are recorded silently and printed at turn end, so printing never runs between probes.
void flush_probes()
{
    for (int i = 0; i < g_n; ++i) std::printf("P %s %lld\nC after_%s %lld\n", g_names[i], g_costs[i], g_names[i], g_after[i]);
    g_n = 0;
}

void board_probes(const metadata& m)
{
    PROBE("bb_zero", sdk_Bitboard_Zero(A));
    PROBE("bb_one", sdk_Bitboard_One(B));
    PROBE("bb_set", sdk_Bitboard_Set(A, 3, 4, 1));
    std::int32_t v = 0;
    PROBE("bb_test", sdk_Bitboard_Test(A, 3, 4, &v));
    PROBE("bb_union", sdk_Bitboard_Union(A, B, C));
    PROBE("bb_intersect", sdk_Bitboard_Intersect(A, B, C));
    PROBE("bb_xor", sdk_Bitboard_Xor(A, B, C));
    PROBE("bb_not", sdk_Bitboard_Not(A, C));
    PROBE("bb_is_zero", sdk_Bitboard_IsZero(A, &v));
    PROBE("bb_is_equal", sdk_Bitboard_IsEqual(A, B, &v));
    std::uint32_t n = 0;
    PROBE("bb_num_set_bits_1", sdk_Bitboard_NumSetBits(A, &n));
    PROBE("bb_num_set_bits_all", sdk_Bitboard_NumSetBits(B, &n));
    PROBE("bb_get_set_bits_cap0", sdk_Bitboard_GetSetBits(B, nullptr, 0, &n));
    PROBE("bb_get_set_bits_1", sdk_Bitboard_GetSetBits(A, BYTES, 1024, &n));
    PROBE("bb_get_set_bits_all", sdk_Bitboard_GetSetBits(B, BYTES, 8000, &n));
    std::printf("I set_bits_all %u\n", n);
    PROBE("bb_draw_cheb_2", sdk_Bitboard_DrawChebyshev(A, 10, 10, 2.0f));
    PROBE("bb_draw_cheb_8", sdk_Bitboard_DrawChebyshev(A, 10, 10, 8.0f));
    PROBE("bb_draw_circle_8", sdk_Bitboard_DrawCircle(A, 10, 10, 8.0f));
    PROBE("bb_shift_right_1", sdk_Bitboard_ShiftRight(A, C, 1));
    PROBE("bb_shift_up_3", sdk_Bitboard_ShiftUp(A, C, 3));
    PROBE("bb_reflect", sdk_Bitboard_Reflect(A, C, 2));
    std::int32_t ox, oy;
    PROBE("bb_rotate_pos", sdk_Bitboard_Rotate180Position(3, 4, &ox, &oy));
    PROBE("bb_export_raw", sdk_Bitboard_ExportRawBuffer(B, BYTES, m.board_bytes));
    PROBE("bb_import_raw", sdk_Bitboard_ImportRawBuffer(C, BYTES, m.board_bytes));
    const std::uint32_t linear = static_cast<std::uint32_t>((m.width * m.height + 7) / 8);
    PROBE("bb_export_linear", sdk_Bitboard_ExportLinearBuffer(B, BYTES, linear));
    PROBE("bb_import_linear", sdk_Bitboard_ImportLinearBuffer(C, BYTES, linear));
    // walls: the map's walls; reachability and shadow casting at small and large radii
    sdk_bc_map_mask(0, D);
    PROBE("bb_reachable_3", sdk_Bitboard_ReachableMask(D, m.width / 2, m.height / 2, C, 3));
    PROBE("bb_reachable_40", sdk_Bitboard_ReachableMask(D, m.width / 2, m.height / 2, C, 40));
    PROBE("bb_shadow_4", sdk_Bitboard_ShadowCast(D, m.width / 2, m.height / 2, C, 4));
    PROBE("bb_shadow_12", sdk_Bitboard_ShadowCast(D, m.width / 2, m.height / 2, C, 12));
    {
        static const int dists[] = { 1, 2, 3, 4 };  // the full sweep (to 60) is done; it exhausts the draft turn
        static const int spots[][2] = { { 35, 35 }, { 5, 5 }, { 20, 50 } };
        char name[64];
        for (const auto& s : spots)
            for (int d : dists)
            {
                std::snprintf(name, sizeof name, "reach_%d_%d_d%d", s[0], s[1], d);
                PROBE(name, sdk_Bitboard_ReachableMask(D, s[0], s[1], C, static_cast<std::uint16_t>(d)));
            }
        for (const auto& s : spots)
            for (int d : dists)
            {
                std::snprintf(name, sizeof name, "shadow_%d_%d_d%d", s[0], s[1], d);
                PROBE(name, sdk_Bitboard_ShadowCast(D, s[0], s[1], C, static_cast<std::uint16_t>(d)));
            }
        alignas(16) static std::uint64_t FR[4096], SC[4096];
        for (int d : { 3, 12, 40 })
        {
            std::snprintf(name, sizeof name, "reach_scratch_d%d", d);
            PROBE(name, sdk_Bitboard_ReachableMaskWithScratch(D, 35, 35, C, static_cast<std::uint16_t>(d), FR, SC));
        }
    }
    // distance fields: one root, limited and unlimited; many roots; towards a destination
    sdk_Bitboard_Zero(A);
    sdk_Bitboard_Set(A, m.width / 2, m.height / 2, 1);
    std::uint16_t count = 0;
    PROBE("df_compute_max3", sdk_DistanceField_ComputeWithExpansionCount(D, A, C, F, 3, &count));
    std::printf("I df_compute_max3_count %u\n", count);
    PROBE("df_compute_max20", sdk_DistanceField_ComputeWithExpansionCount(D, A, C, F, 20, &count));
    std::printf("I df_compute_max20_count %u\n", count);
    PROBE("df_compute_full", sdk_DistanceField_ComputeWithExpansionCount(D, A, C, F, 0xffff, &count));
    std::printf("I df_compute_full_count %u\n", count);
    sdk_Bitboard_Zero(B);
    sdk_Bitboard_Set(B, 1, 1, 1);
    PROBE("df_compute_to", sdk_DistanceField_ComputeToWithExpansionCount(D, A, B, C, F, 0xffff, &count));
    std::printf("I df_compute_to_count %u\n", count);
    std::uint16_t dv = 0;
    PROBE("df_get", sdk_DistanceField_Get(F, 2, 2, &dv));
    PROBE("df_set", sdk_DistanceField_Set(F, 2, 2, 7));
    PROBE("df_copy", sdk_DistanceField_Copy(F, G));
    PROBE("df_copy_mask", sdk_DistanceField_CopyWithMask(F, D, G));
    PROBE("df_export_raw", sdk_DistanceField_ExportRawBuffer(F, BYTES, m.field_bytes));
    PROBE("df_import_raw", sdk_DistanceField_ImportRawBuffer(G, BYTES, m.field_bytes));
    const std::uint32_t lin = static_cast<std::uint32_t>(m.width * m.height * 2);
    PROBE("df_export_linear", sdk_DistanceField_ExportLinearBuffer(F, BYTES, lin));
    PROBE("df_import_linear", sdk_DistanceField_ImportLinearBuffer(G, BYTES, lin));
}

void static_probes()
{
    PROBE("fuel_remaining", (void)0);
    game_settings gs{};
    PROBE("game_settings", sdk_get_settings(&gs));
    champion_design cd{};
    PROBE("champion_design", sdk_champion_design(0, &cd));
    camp_design kd{};
    PROBE("camp_design", sdk_camp_design(1, &kd));
    item_info ii{};
    PROBE("item_info", sdk_item_info(0, &ii));
    consumable_info ci{};
    PROBE("consumable_info", sdk_consumable_info(0, &ci));
    metadata m{};
    PROBE("bc_metadata", sdk_bc_metadata(&m));
    PROBE("map_mask_wall", sdk_bc_map_mask(0, A));
    PROBE("map_mask_altar", sdk_bc_map_mask(3, A));
    std::int32_t sym = 0;
    PROBE("map_symmetry", sdk_bc_map_symmetry(&sym));
    aabb box{};
    PROBE("spawn_region", sdk_spawn_region(0, &box));
    std::uint32_t n = 0;
    PROBE("map_num_camp_groups", sdk_map_num_camp_groups(&n));
    std::printf("I groups %u\n", n);
    std::uint32_t count = 0;
    PROBE("map_camp_groups_cap0", sdk_map_camp_groups(nullptr, 0, &count));
    PROBE("map_camp_groups_full", sdk_map_camp_groups(WORDS, 4096, &count));
    PROBE("map_camp_group_num_camps", sdk_map_camp_group_num_camps(WORDS[0], &n));
    std::printf("I group0_camps %u\n", n);
    PROBE("map_camp_group_camps", sdk_map_camp_group_camps(WORDS[0], WORDS + 8192, 64, &count));
    camp_group_info gi{};
    PROBE("map_camp_group", sdk_map_camp_group(WORDS[0], &gi));
    camp_info info{};
    PROBE("map_camp_info", sdk_map_camp_info(WORDS[8192], &info));
    PROBE("map_camp_layout_cap0", sdk_map_camp_layout(nullptr, 0, &count));
    std::printf("I layout_words %u\n", count);
    PROBE("map_camp_layout_full", sdk_map_camp_layout(WORDS, 16384, &count));
    std::uint32_t sz = 0;
    PROBE("shared_memory_size", sdk_shared_memory_size(&sz));
    PROBE("draft_memory_size", sdk_draft_memory_size(&sz));
    PROBE("read_draft_memory_8", sdk_read_draft_memory(0, BYTES, 8));
    PROBE("read_draft_memory_1024", sdk_read_draft_memory(0, BYTES, 1024));
    std::uint32_t round = 0;
    PROBE("bc_round_number", sdk_bc_round_number(&round));
    PROBE("my_team", (void)sdk_my_team());
    PROBE("my_champion", (void)sdk_my_champion());
    board_probes(m);
}

void champion_probes(std::uint32_t me)
{
    champion_info info{};
    PROBE("champion", sdk_champion(me, &info));
    champion_identity id{};
    PROBE("champion_identity", sdk_champion_identity(me, &id));
    std::int32_t alive = 0;
    PROBE("champion_is_alive", sdk_champion_is_alive(me, &alive));
    std::uint32_t v = 0;
    PROBE("champion_gold", sdk_champion_gold(me, &v));
    PROBE("champion_gems", sdk_champion_gems(me, &v));
    PROBE("champion_items", sdk_champion_items(me, WORDS));
    PROBE("champion_consumables", sdk_champion_consumables(me, WORDS));
    std::int32_t cd = 0;
    PROBE("champion_cooldown", sdk_champion_cooldown(me, 0, &cd));
    ability_props ap{};
    PROBE("champion_ability_props", sdk_champion_ability_props(me, 0, &ap));
    float dmg = 0;
    PROBE("champion_ability_damage", sdk_champion_ability_damage(me, 0, &dmg));
    std::uint8_t r = 0;
    PROBE("champion_restrictions", sdk_champion_restrictions(me, &r));
    std::int32_t ch = 0;
    PROBE("champion_channel_time", sdk_champion_channel_time(me, &ch));
    PROBE("champion_can_shop", sdk_champion_can_shop(me, 0, &ch));
    PROBE("champion_priority", sdk_bc_champion_priority(me, 0, &ch));
    PROBE("num_allies", sdk_num_allies(&v));
    std::uint32_t count = 0;
    PROBE("allies_cap0", sdk_allies(nullptr, 0, &count));
    PROBE("allies_full", sdk_allies(WORDS, 16, &count));
    PROBE("enemies_full", sdk_enemies(WORDS, 16, &count));
    PROBE("board_vision", sdk_vision(A));
    PROBE("champion_vision", sdk_bc_champion_vision(me, A));
    PROBE("vision_mask", sdk_hypothetical_vision(static_cast<std::int32_t>(info.type), info.x, info.y, A));
    std::int32_t yes = 0;
    PROBE("is_visible", sdk_is_visible(info.x, info.y, &yes));
    PROBE("has_camp", sdk_has_camp(info.x, info.y, &yes));
    PROBE("has_champion", sdk_has_champion(info.x, info.y, &yes));
    PROBE("champion_at", sdk_champion_at(info.x, info.y, &v));
    PROBE("ally_occupancy", sdk_ally_occupancy(A));
    PROBE("camp_occupancy", sdk_camp_occupancy(A));
    PROBE("awake_camp_occupancy", sdk_awake_camp_occupancy(A));
    std::uint32_t gid = 0, gc = 0;
    sdk_map_camp_groups(&gid, 1, &gc);
    std::uint32_t state = 0;
    PROBE("camp_group_state", sdk_camp_group_state(gid, &state));
    std::int32_t cool = 0;
    PROBE("camp_group_cooldown", sdk_camp_group_cooldown(gid, &cool));
    std::uint32_t camps[8], nc = 0;
    sdk_map_camp_group_camps(gid, camps, 8, &nc);
    camp_state cs{};
    PROBE("camp", sdk_camp(camps[0], &cs));
    PROBE("camp_state", sdk_camp_state(camps[0], &state));
    std::uint32_t gems = 0;
    PROBE("submitted_gems", sdk_bc_submitted_gems(0, &gems));
    PROBE("read_shared_memory_8", sdk_read_shared_memory(0, BYTES, 8));
    PROBE("write_shared_memory_8", sdk_write_shared_memory(0, BYTES, 8));
    PROBE("write_shared_memory_1", sdk_write_shared_memory(3, BYTES, 1));
    sdk_Bitboard_One(A);
    PROBE("get_set_bits_cap4_of_all", sdk_Bitboard_GetSetBits(A, BYTES, 4, &count));
    std::printf("I get_set_bits_cap4_count %u\n", count);
    sdk_Bitboard_Zero(A);
    for (int i = 0; i < 30; ++i) sdk_Bitboard_Set(A, i, 7, 1);
    PROBE("get_set_bits_cap10_of_30", sdk_Bitboard_GetSetBits(A, BYTES, 10, &count));
    PROBE("get_set_bits_cap30_of_30", sdk_Bitboard_GetSetBits(A, BYTES, 30, &count));
    PROBE("get_set_bits_cap64_of_30", sdk_Bitboard_GetSetBits(A, BYTES, 64, &count));
    PROBE("allies_cap2", sdk_allies(WORDS, 2, &count));
    PROBE("map_camp_groups_cap3", sdk_map_camp_groups(WORDS, 3, &count));
    PROBE("play_read_draft_memory_1024", sdk_read_draft_memory(0, BYTES, 1024));
    PROBE("play_read_draft_memory_8", sdk_read_draft_memory(0, BYTES, 8));
    PROBE("play_map_camp_layout_256", sdk_map_camp_layout(WORDS, 256, &count));
    __wasi_fdstat_t fdstat{};
    PROBE("wasi_fdstat", (void)__wasi_fd_fdstat_get(1, &fdstat));
    __wasi_filesize_t where = 0;
    PROBE("wasi_seek", (void)__wasi_fd_seek(1, 0, __WASI_WHENCE_CUR, &where));
    __wasi_size_t ec = 0, es = 0;
    PROBE("wasi_environ_sizes", (void)__wasi_environ_sizes_get(&ec, &es));
    __wasi_ciovec_t iov{ reinterpret_cast<const std::uint8_t*>("x\n"), 2 };
    __wasi_size_t nw = 0;
    PROBE("wasi_write_2", (void)__wasi_fd_write(1, &iov, 1, &nw));
    std::memset(BYTES, 'y', 4096);
    BYTES[4095] = '\n';
    __wasi_ciovec_t big{ BYTES, 4096 };
    PROBE("wasi_write_4096", (void)__wasi_fd_write(1, &big, 1, &nw));
    std::int32_t moved = 0;
    PROBE("move_invalid", sdk_move_to(info.x + 5, info.y, &moved));
    PROBE("move_adjacent", sdk_move_to(info.x + 1, info.y + 1, &moved));
    PROBE("attack_invalid", sdk_attack(2, 0));
    std::int32_t cast_ok = 0;
    PROBE("cast_invalid", sdk_cast(0, 2, 0, 0, 0, &cast_ok));
    PROBE("buy_item", sdk_buy_item(0, 1));
    PROBE("buy_consumable", sdk_buy_consumable(2));
    PROBE("set_priority", sdk_set_priority(0));
    PROBE("submit_gems_invalid", sdk_submit_gems(0, 0));
    std::int32_t placed = 0;
    PROBE("place_ward_invalid", sdk_place_ward(0, 0, &placed));
    PROBE("recall", sdk_recall());
    PROBE("cancel_channel", sdk_cancel_channel());
    PROBE("buff_data", sdk_champion_buff_data(me, reinterpret_cast<buff_data*>(BYTES), 16, &count));
    PROBE("ward_vision_mask", sdk_ward_vision(info.x, info.y, A));
    PROBE("wards_list", sdk_wards(reinterpret_cast<ward_info*>(BYTES), 16, &count));
    PROBE("altar_locks_list", sdk_altar_locks(reinterpret_cast<altar_lock_info*>(BYTES), 16, &count));
    PROBE("teleports_list", sdk_teleports(reinterpret_cast<teleport_info*>(BYTES), 16, &count));
    PROBE("ward_positions", sdk_champion_ward_positions(me, reinterpret_cast<position*>(BYTES), 16, &count));
    PROBE("camp_buffs", sdk_camp_buffs(camps[0], reinterpret_cast<camp_buff_data*>(BYTES), 16, &count));
    PROBE("ally_ward_occupancy", sdk_ally_ward_occupancy(A));
    PROBE("teleport_occupancy", sdk_teleport_occupancy(A));
    PROBE("ally_altar_lock_occupancy", sdk_ally_altar_lock_occupancy(A));
    char err[256];
    sdk_champion_cooldown(me, 9, &cd);
    PROBE("last_error_size", (void)sdk_bc_get_last_error(nullptr, 0));
    PROBE("last_error_text", (void)sdk_bc_get_last_error(err, 256));
}
} // namespace

// Absolute fuel checkpoints: "C <label> <remaining>" (the first label that differs localises a gap).
#define CHECK(label) std::printf("C %s %lld\n", label, static_cast<long long>(sdk_fuel_remaining()))

int main()
{
    int turn = 0;
    while (true)
    {
        if (turn == 0)
        {
            // stdio costs, recorded silently: first printf (stream setup), a second, an explicit flush
            void* m1 = nullptr;
            PROBE("first_malloc_64", m1 = std::malloc(64));
            PROBE("second_malloc_64", (void)std::malloc(64));
            PROBE("malloc_4096", (void)std::malloc(4096));
            PROBE("malloc_200000", (void)std::malloc(200000));
            PROBE("isatty_1", (void)isatty(1));
            std::printf("%s", m1 ? "" : "?");
            PROBE("stdio_first_printf", std::printf("x\n"));
            PROBE("stdio_second_printf", std::printf("y\n"));
            PROBE("stdio_fflush", std::fflush(nullptr));
            PROBE("stdio_fflush_empty", std::fflush(nullptr));
            PROBE("stdio_printf_flush", (std::printf("z\n"), std::fflush(nullptr)));
            PROBE("stdio_stderr", (std::fprintf(stderr, "e\n"), std::fflush(stderr)));
        }
        CHECK("turn_start");
        if (cambc::is_draft())
        {
            if (turn == 0) static_probes();
            CHECK("after_static");
            flush_probes();
            for (int pick = 0; pick < 12; ++pick)
                if (sdk_draft_champion(pick) == 0) break;
            CHECK("after_pick");
        }
        else if (turn >= 1 && turn <= 4)
        {
            // Regular play turns: route-field style work, one variant per turn (whole-turn fuel is compared).
            metadata m{};
            sdk_bc_metadata(&m);
            const std::uint32_t linear = static_cast<std::uint32_t>((m.width * m.height + 7) / 8);
            std::uint16_t count = 0;
            if (turn == 1 || turn == 3)
            {
                sdk_bc_map_mask(0, D);
                sdk_Bitboard_ExportLinearBuffer(D, BYTES, linear);
                sdk_Bitboard_ImportLinearBuffer(C, BYTES, linear);
            }
            if (turn == 2 || turn == 3)
            {
                sdk_bc_map_mask(0, D);
                sdk_Bitboard_Zero(A);
                sdk_Bitboard_Set(A, m.width / 2, m.height / 2, 1);
                sdk_DistanceField_ComputeWithExpansionCount(D, A, C, F, 0xffff, &count);
            }
            if (turn == 4)
            {
                sdk_bc_map_mask(0, D);
                sdk_Bitboard_Zero(A);
                sdk_Bitboard_Set(A, 1, 1, 1);
                sdk_DistanceField_ComputeWithExpansionCount(D, A, C, F, 0xffff, &count);
            }
            std::printf("I turn%d_count %u\n", turn, count);
        }
        else if (turn == 0)
        {
            champion_probes(static_cast<std::uint32_t>(sdk_my_champion()));
            CHECK("after_champion_probes");
            flush_probes();
            std::printf("I probes %d\n", printed);
            CHECK("after_print");
        }
        ++turn;
        CHECK("before_end_turn");
        cambc::end_turn();
    }
}
