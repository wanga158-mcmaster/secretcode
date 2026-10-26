// fastrun: the native local game runner for cambc2026a (tools/fastrun; `arena exp` runs every experiment on it).
//
// The official engine (the pinned visualiser WASM, engine 1.0.3) and C++ bots (their platform WASM, e.g. an arena
// snapshot's .cambc/build-release/bot.wasm) run in one process on wasmtime, the runtime the platform runner uses.
// Bots get wasmtime fuel as on the platform: a budget per turn (draft 1,000,000 then 100,000; play 200,000 then
// 100,000), suspension when it runs out and resumption on the owner's next turn, one instance per drafting team and
// per champion. Every bot import maps onto the engine's Game_API_* / Game_Action_* / Bitboard_* / DistanceField_*
// exports from the bot's own perspective (bridge below; argument conventions probed against the engine). Whole
// games match the CLI (winner, rounds, gems; per-turn fuel on probe bots): check with tools/fastrun/validate.py
// after any CLI/SDK/engine update.
//
//   fastrun --engine E.wasm --a A.wasm --b B.wasm --map M.map26a [--map ...] [--sides A|B|both] [--jobs N]
//           [--out rows.jsonl] [--bundles DIR] [--rounds N] [--log-limit BYTES] [--cache DIR] [--name-a X] [--name-b Y]
//           [--stdout-type N] [--params-a W.txt] [--params-b W.txt] [--procs N]
//   fastrun --engine E.wasm --bot KEY=BOT.wasm [--bot ...] --games FILE --procs N --out rows.jsonl
//
// A row per game (JSON): map, side the first bot played, winner, rounds, tributed gems, per-team fuel max/total,
// fuel exhaustions, bot faults, logged error lines, milliseconds, team totals (kills, deaths, earned/spent gold,
// collected gems, damage). --bundles writes replay.repl26a, team-{a,b}.stdout and team-{a,b}.fuel (the CLI's .fuel
// format) per game. Use --procs (single-job processes) for throughput: threads in one process stop scaling at 2-4.
#include <wasmtime.h>
#include <wasmtime/async.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <process.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace {

using Raw = wasmtime_val_raw_t;

// ------------------------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------------------------
[[noreturn]] void die(const std::string& message)
{
    std::fprintf(stderr, "fastrun: %s\n", message.c_str());
    std::exit(2);
}

std::string take_error(wasmtime_error_t* error)
{
    wasm_byte_vec_t text;
    wasmtime_error_message(error, &text);
    std::string out(text.data, text.size);
    wasm_byte_vec_delete(&text);
    wasmtime_error_delete(error);
    return out;
}

std::string take_trap(wasm_trap_t* trap)
{
    wasm_byte_vec_t text;
    wasm_trap_message(trap, &text);
    std::string out(text.data, text.size);
    wasm_byte_vec_delete(&text);
    wasm_trap_delete(trap);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

void ok(wasmtime_error_t* error, const char* what)
{
    if (error) die(std::string(what) + ": " + take_error(error));
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) die("cannot read " + path);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const void* data, std::size_t size)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) die("cannot write " + path.string());
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes, std::uint64_t h = 1469598103934665603ull)
{
    for (auto b : bytes) h = (h ^ b) * 1099511628211ull;
    return h;
}

inline Raw I32(std::int32_t v) { Raw r; r.i64 = 0; r.i32 = v; return r; }
[[noreturn]] void arity(const char* name, std::size_t got, std::size_t want)
{
    die(std::string("engine call ") + name + " with " + std::to_string(got) + " arguments, expects " + std::to_string(want));
}
inline Raw I64(std::int64_t v) { Raw r; r.i64 = v; return r; }
inline Raw F32(float v) { Raw r; r.i64 = 0; r.f32 = v; return r; }

std::string json_escape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
        else out += c;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------------------------
// Engine exports used by the runner and the bridge
// ------------------------------------------------------------------------------------------------------------------
#define ENGINE_FUNCTIONS(X) \
    X(Init0, "_initialize") X(Malloc, "Engine_Malloc") X(Free, "Engine_Free") X(LastError, "GetLastErrorMsg") \
    X(MB_Create, "MapBuilder_Create") X(MB_SetChamps, "MapBuilder_SetNumChampsPerTeam") X(MB_SetSpawn, "MapBuilder_SetTeamASpawnArea") \
    X(MB_AddWall, "MapBuilder_AddWall") X(MB_AddBush, "MapBuilder_AddBush") X(MB_AddHole, "MapBuilder_AddHole") \
    X(MB_AddAltar, "MapBuilder_AddAltar") X(MB_AddCamp, "MapBuilder_AddCamp") X(MB_Verify, "MapBuilder_VerifyIntegrity") \
    X(MB_Encode, "MapBuilder_Encode") X(MB_FromBytes, "MapBuilder_FromFileBytes") X(MB_Destroy, "MapBuilder_Destroy") \
    X(MB_GetWall, "MapBuilder_GetWall") X(MB_NumGroups, "MapBuilder_GetNumCampGroups") \
    X(FromMap, "Game_FromMapFileBytes") X(Destroy, "Game_Destroy") X(RunnerInit, "Game_Runner_Init") \
    X(Checkpoints, "Game_Runner_SetCheckpointsEnabled") X(GetState, "Game_Runner_GetState") X(Advance, "Game_Runner_Advance") \
    X(EndTurn, "Game_Runner_EndTurn") X(SaveReplay, "Game_SaveReplay") X(FileBuffer, "Game_FileBuffer") \
    X(TimelineSize, "Game_Visualiser_TimelineSnapshotSize") X(ReadTimeline, "Game_Visualiser_ReadTimelineState") \
    X(BoardSize, "Bitboard_GetRawBufferSize") X(FieldSize, "DistanceField_GetRawBufferSize") \
    X(MapMeta, "Game_API_Map_GetBitboardMetadata") \
    X(GetSettings, "Game_API_GetSettings") X(GetItemInfo, "Game_API_GetItemInfo") X(GetConsumableInfo, "Game_API_GetConsumableInfo") \
    X(ReadShared, "Game_API_ReadSharedMemory") X(WriteShared, "Game_API_WriteSharedMemory") \
    X(ReadDraft, "Game_API_ReadDraftMemory") X(WriteDraft, "Game_API_WriteDraftMemory") \
    X(SharedSize, "Game_API_GetSharedMemorySize") X(DraftSize, "Game_API_GetDraftMemorySize") \
    X(ChampionDesign, "Game_API_Champion_Design") X(CampDesign, "Game_API_Camp_Design") \
    X(MapBush, "Game_API_Map_GetBushBitboard") X(MapWall, "Game_API_Map_GetWallBitboard") \
    X(MapAltar, "Game_API_Map_GetAltarBitboard") X(MapHole, "Game_API_Map_GetHoleBitboard") \
    X(MapNumGroups, "Game_API_Map_GetNumCampGroups") X(MapGroups, "Game_API_Map_GetCampGroups") \
    X(MapGroupNumCamps, "Game_API_Map_CampGroup_GetNumCamps") X(MapGroupCampInfo, "Game_API_Map_CampGroup_GetCampInfo") \
    X(MapGroupGet, "Game_API_Map_CampGroup_Get") X(GroupState, "Game_API_CampGroup_GetState") \
    X(GroupCooldown, "Game_API_CampGroup_GetCooldown") X(MapCampInfoGet, "Game_API_Map_CampInfo_Get") \
    X(MapCampInfoGroup, "Game_API_Map_CampInfo_GetGroup") X(MapCampAt, "Game_API_Map_GetCampAt") \
    X(VisionMask, "Game_API_GetVisionMask") X(WardVisionMask, "Game_API_GetWardVisionMask") X(GetVision, "Game_API_GetVision") \
    X(InVision, "Game_API_InVision") X(HasCamp, "Game_API_HasCamp") X(GetCamp, "Game_API_GetCamp") \
    X(CampGetState, "Game_API_Camp_GetState") X(CampGet, "Game_API_Camp_Get") X(CampGetBuffs, "Game_API_Camp_GetBuffs") \
    X(HasChampion, "Game_API_HasChampion") X(GetChampion, "Game_API_GetChampion") X(ChampionGet, "Game_API_Champion_Get") \
    X(ChampionIdentity, "Game_API_Champion_GetIdentity") X(ChampionRestrictions, "Game_API_Champion_GetRestrictions") \
    X(ChampionCanShop, "Game_API_Champion_CanShop") X(ChampionCanSell, "Game_API_Champion_CanSell") \
    X(GetAllyWard, "Game_API_GetAllyWard") X(GetEnemyWard, "Game_API_GetEnemyWard") X(WardGet, "Game_API_Ward_Get") \
    X(ChampionGold, "Game_API_Champion_GetGold") X(ChampionGems, "Game_API_Champion_GetGems") \
    X(ChampionItems, "Game_API_Champion_GetItems") X(ChampionConsumables, "Game_API_Champion_GetConsumables") \
    X(ChampionCooldown, "Game_API_Champion_GetCooldown") \
    X(CampOcc, "Game_API_GetCampOccupancy") X(AwakeCampOcc, "Game_API_GetAwakeCampOccupancy") \
    X(AsleepCampOcc, "Game_API_GetAsleepCampOccupancy") X(AllyOcc, "Game_API_GetAllyOccupancy") \
    X(EnemyOcc, "Game_API_GetEnemyOccupancy") X(AllyWardOcc, "Game_API_GetAllyWardOccupancy") \
    X(EnemyWardOcc, "Game_API_GetEnemyWardOccupancy") X(ChampionAllies, "Game_API_Champion_GetAllies") \
    X(ChampionEnemies, "Game_API_Champion_GetEnemies") X(ChampionIsAlive, "Game_API_Champion_IsAlive") \
    X(SpawnRegion, "Game_API_GetSpawnRegion") X(SubmittedGems, "Game_API_GetSubmittedGemCount") \
    X(NumAllies, "Game_API_Champion_GetNumAllies") X(NumEnemies, "Game_API_Champion_GetNumEnemies") \
    X(DraftGet, "Game_API_Draft_Get") X(DraftNumRounds, "Game_API_Draft_GetNumRounds") \
    X(AbilityDamage, "Game_API_Champion_GetAbilityDamage") X(AbilityDamageAgainst, "Game_API_Champion_GetAbilityDamageAgainst") \
    X(AbilityHeal, "Game_API_Champion_GetAbilityHeal") X(AbilityHealAgainst, "Game_API_Champion_GetAbilityHealAgainst") \
    X(AbilityShield, "Game_API_Champion_GetAbilityShield") X(AbilityShieldAgainst, "Game_API_Champion_GetAbilityShieldAgainst") \
    X(BuffData, "Game_API_Champion_GetBuffData") X(AbilityProps, "Game_API_Champion_GetAbilityProps") \
    X(ChampionWards, "Game_API_Champion_GetWards") X(IsOccupiedByTeleport, "Game_API_IsOccupiedByTeleport") \
    X(TeleportOcc, "Game_API_GetTeleportOccupancy") X(AllyAltarLockOcc, "Game_API_GetAllyAltarLockOccupancy") \
    X(EnemyAltarLockOcc, "Game_API_GetEnemyAltarLockOccupancy") X(GetAltarLock, "Game_API_GetAltarLock") \
    X(GetAltarLocks, "Game_API_GetAltarLocks") X(GetWards, "Game_API_GetWards") X(GetTeleports, "Game_API_GetTeleports") \
    X(GetTeleport, "Game_API_GetTeleport") X(ChannelTime, "Game_API_Champion_GetChannelTime") \
    X(Priority, "Game_API_Champion_GetPriority") X(WardPositions, "Game_API_Champion_GetWardPositions") \
    X(GroupRespawnTime, "Game_API_Map_CampGroup_GetRespawnTime") X(MapSymmetry, "Game_API_Map_GetSymmetry") \
    X(RoundNumber, "Game_API_GetRoundNumber") X(StatsSummary, "Game_Statistics_GetSummary") X(StatsChampion, "Game_Statistics_GetChampion") \
    X(Draft, "Game_Action_Draft") X(Resign, "Game_Action_Resign") X(SetPriority, "Game_Action_SetPriority") \
    X(Recall, "Game_Action_Recall") X(CancelChannel, "Game_Action_CancelChannel") X(Attack, "Game_Action_Attack") \
    X(Move, "Game_Action_Move") X(Cast, "Game_Action_Cast") X(SubmitGems, "Game_Action_SubmitGems") X(Sell, "Game_Action_Sell") \
    X(BuyItem, "Game_Action_BuyItem") X(BuyConsumable, "Game_Action_BuyConsumable") \
    X(UseConsumable, "Game_Action_UseConsumable") X(PlaceWard, "Game_Action_PlaceWard") \
    X(BB_Zero, "Bitboard_Zero") X(BB_One, "Bitboard_One") X(BB_ImportRaw, "Bitboard_ImportRawBuffer") \
    X(BB_ExportRaw, "Bitboard_ExportRawBuffer") X(BB_ImportLinear, "Bitboard_ImportLinearBuffer") \
    X(BB_ExportLinear, "Bitboard_ExportLinearBuffer") X(BB_Test, "Bitboard_Test") X(BB_Set, "Bitboard_Set") \
    X(BB_Toggle, "Bitboard_Toggle") X(BB_IsZero, "Bitboard_IsZero") X(BB_NumSetBits, "Bitboard_NumSetBits") \
    X(BB_GetSetBits, "Bitboard_GetSetBits") X(BB_Not, "Bitboard_Not") X(BB_Union, "Bitboard_Union") \
    X(BB_Intersect, "Bitboard_Intersect") X(BB_Xor, "Bitboard_Xor") X(BB_IsEqual, "Bitboard_IsEqual") \
    X(BB_ShiftRight, "Bitboard_ShiftRight") X(BB_ShiftUp, "Bitboard_ShiftUp") X(BB_Reflect, "Bitboard_Reflect") \
    X(BB_DrawCircle, "Bitboard_DrawCircle") X(BB_DrawChebyshev, "Bitboard_DrawChebyshev") \
    X(BB_ReflectHPos, "Bitboard_ReflectHorizontalPosition") X(BB_ReflectVPos, "Bitboard_ReflectVerticalPosition") \
    X(BB_Rot180Pos, "Bitboard_Rotate180Position") X(BB_Reachable, "Bitboard_ReachableMask") \
    X(BB_ShadowCast, "Bitboard_ShadowCast") X(BB_ReachableScratch, "Bitboard_ReachableMaskWithScratch")     X(BB_ReachableCount, "Bitboard_ReachableMaskWithExpansionCount") X(BB_ShadowCount, "Bitboard_ShadowCastWithScanCount")     X(BB_ReachableScratchCount, "Bitboard_ReachableMaskWithScratchAndExpansionCount") \
    X(DF_ImportRaw, "DistanceField_ImportRawBuffer") X(DF_ExportRaw, "DistanceField_ExportRawBuffer") \
    X(DF_ImportLinear, "DistanceField_ImportLinearBuffer") X(DF_ExportLinear, "DistanceField_ExportLinearBuffer") \
    X(DF_Get, "DistanceField_Get") X(DF_Set, "DistanceField_Set") X(DF_Copy, "DistanceField_Copy") \
    X(DF_CopyMask, "DistanceField_CopyWithMask") X(DF_Compute, "DistanceField_ComputeWithExpansionCount") \
    X(DF_ComputeTo, "DistanceField_ComputeToWithExpansionCount")

enum Fn : int {
#define X(id, name) F_##id,
    ENGINE_FUNCTIONS(X)
#undef X
    F_COUNT
};
const char* const FN_NAMES[F_COUNT] = {
#define X(id, name) name,
    ENGINE_FUNCTIONS(X)
#undef X
};

const bool kTrace = std::getenv("FASTRUN_TRACE") != nullptr;
bool g_counting = false;  // --calls-log: per-turn import calls and host charges
#define TRACE(...) do { if (kTrace) { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr); } } while (0)
constexpr std::uint64_t kReserve = 1ull << 40;  // fuel held in reserve so a yield interval suspends instead of trapping
constexpr std::uint32_t kNoChampion = 0xffffffffu;

// ------------------------------------------------------------------------------------------------------------------
// Engine runtime: one engine instance per worker thread, games created and destroyed in it
// ------------------------------------------------------------------------------------------------------------------
struct EngineRt
{
    wasmtime_store_t* store{};
    wasmtime_context_t* ctx{};
    wasmtime_instance_t instance{};
    wasmtime_memory_t memory{};
    wasmtime_func_t fn[F_COUNT]{};
    std::size_t nparams[F_COUNT]{};
    std::uint32_t s_out{}, s_persp{}, s_ent{}, s_pos{}, s_meta{};
    std::uint32_t random_state = 0x2026a;

    std::uint8_t* mem() { return wasmtime_memory_data(ctx, &memory); }
    std::size_t mem_size() { return wasmtime_memory_data_size(ctx, &memory); }

    // Call an engine export; returns the first result as i32 (0 when there is none).
    std::int32_t call(Fn f, std::initializer_list<Raw> args)
    {
        Raw buf[12];
        std::size_t n = 0;
        for (const auto& a : args) buf[n++] = a;
        if (n != nparams[f]) arity(FN_NAMES[f], n, nparams[f]);
        if (n == 0) buf[0].i64 = 0;
        wasm_trap_t* trap = nullptr;
        if (auto* error = wasmtime_func_call_unchecked(ctx, &fn[f], buf, std::max<std::size_t>(n, 1), &trap))
            die(std::string("engine call ") + FN_NAMES[f] + ": " + take_error(error));
        if (trap) die(std::string("engine trap in ") + FN_NAMES[f] + ": " + take_trap(trap));
        return buf[0].i32;
    }
    std::uint32_t malloc(std::uint32_t size)
    {
        const auto p = static_cast<std::uint32_t>(call(F_Malloc, { I32(static_cast<std::int32_t>(std::max<std::uint32_t>(size, 1))) }));
        if (!p) die("Engine_Malloc failed");
        return p;
    }
    void free(std::uint32_t p) { if (p) call(F_Free, { I32(static_cast<std::int32_t>(p)) }); }
    std::string last_error()
    {
        const auto p = static_cast<std::uint32_t>(call(F_LastError, {}));
        const auto* m = mem();
        std::string s;
        for (std::size_t i = p; i < mem_size() && m[i]; ++i) s += static_cast<char>(m[i]);
        return s;
    }
};

// WASI preview1 for the engine: no files or environment, output discarded, deterministic random_get.
wasm_trap_t* engine_memory(wasmtime_caller_t* caller, std::uint8_t*& base, std::size_t& size)
{
    wasmtime_extern_t item;
    if (!wasmtime_caller_export_get(caller, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY)
        return wasmtime_trap_new("engine memory", 13);
    auto* cx = wasmtime_caller_context(caller);
    base = wasmtime_memory_data(cx, &item.of.memory);
    size = wasmtime_memory_data_size(cx, &item.of.memory);
    return nullptr;
}
#define ENGINE_HOST(name) wasm_trap_t* name(void* env, wasmtime_caller_t* caller, Raw* a, std::size_t)
ENGINE_HOST(ew_environ_sizes_get)
{
    std::uint8_t* m; std::size_t n;
    if (auto* t = engine_memory(caller, m, n)) return t;
    std::memset(m + static_cast<std::uint32_t>(a[0].i32), 0, 4);
    std::memset(m + static_cast<std::uint32_t>(a[1].i32), 0, 4);
    a[0].i32 = 0;
    return nullptr;
}
ENGINE_HOST(ew_zero) { a[0].i32 = 0; return nullptr; }
ENGINE_HOST(ew_badf) { a[0].i32 = 8; return nullptr; }
ENGINE_HOST(ew_espipe) { a[0].i32 = 70; return nullptr; }
ENGINE_HOST(ew_fdstat)
{
    std::uint8_t* m; std::size_t n;
    if (auto* t = engine_memory(caller, m, n)) return t;
    if (a[0].i32 > 2) { a[0].i32 = 8; return nullptr; }
    auto* out = m + static_cast<std::uint32_t>(a[1].i32);
    std::memset(out, 0, 24);
    out[0] = 2;
    a[0].i32 = 0;
    return nullptr;
}
ENGINE_HOST(ew_fd_read)
{
    std::uint8_t* m; std::size_t n;
    if (auto* t = engine_memory(caller, m, n)) return t;
    std::memset(m + static_cast<std::uint32_t>(a[3].i32), 0, 4);
    a[0].i32 = 0;
    return nullptr;
}
ENGINE_HOST(ew_fd_write)
{
    std::uint8_t* m; std::size_t n;
    if (auto* t = engine_memory(caller, m, n)) return t;
    const auto iovs = static_cast<std::uint32_t>(a[1].i32), count = static_cast<std::uint32_t>(a[2].i32);
    std::uint32_t total = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t len;
        std::memcpy(&len, m + iovs + i * 8 + 4, 4);
        total += len;
    }
    std::memcpy(m + static_cast<std::uint32_t>(a[3].i32), &total, 4);
    a[0].i32 = 0;
    return nullptr;
}
ENGINE_HOST(ew_proc_exit) { return wasmtime_trap_new("engine proc_exit", 16); }
ENGINE_HOST(ew_random_get)
{
    std::uint8_t* m; std::size_t n;
    if (auto* t = engine_memory(caller, m, n)) return t;
    auto& state = static_cast<EngineRt*>(wasmtime_context_get_data(wasmtime_caller_context(caller)))->random_state;
    auto* out = m + static_cast<std::uint32_t>(a[0].i32);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(a[1].i32); ++i)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        out[i] = static_cast<std::uint8_t>(state & 0xff);
    }
    a[0].i32 = 0;
    return nullptr;
}
ENGINE_HOST(ew_assert) { return wasmtime_trap_new("engine assertion failed", 23); }

// ------------------------------------------------------------------------------------------------------------------
// Bots
// ------------------------------------------------------------------------------------------------------------------
struct Game;

struct Bot
{
    Game* game{};
    int team{};
    int champion{};  // -1: draft instance
    int slot{};      // which bot program (0 = --a, 1 = --b)
    wasmtime_store_t* store{};
    wasmtime_context_t* ctx{};
    wasmtime_instance_t instance{};
    wasmtime_memory_t memory{};
    wasmtime_func_t start{};
    wasmtime_call_future_t* future{};
    wasm_trap_t* trap{};
    wasmtime_error_t* error{};
    bool yielded{}, resume{}, dead{};
    std::uint64_t fuel_at_yield{};
    int turns{};
    std::vector<std::uint32_t> calls;      // calls per import this turn (--calls-log)
    std::vector<std::uint64_t> charged;    // host fuel charged per import this turn (--calls-log)
    std::size_t current_slot = 0;          // import being served (for `charged`)
    std::uint64_t out_bytes = 0;           // bytes written to stdout/stderr this turn
    std::uint64_t units{};                // variable work this turn: list elements, expansions
    ~Bot()
    {
        if (future) wasmtime_call_future_delete(future);
        if (trap) wasm_trap_delete(trap);
        if (error) wasmtime_error_delete(error);
        if (store) wasmtime_store_delete(store);
    }
};

struct Program
{
    std::string path, name;
    wasmtime_module_t* module{};
    wasmtime_linker_t* linker{};
    wasmtime_instance_pre_t* pre{};
    // Learned-weights block (bots/main/main/src/learn.h): offset of its magic in linear memory
    // after instantiation (-1: none) and the number of floats it holds.
    std::int64_t params_at = -1;
    std::uint32_t params_count = 0;
};

// Weights written into a bot's learn.h block before it starts (empty: the bot's own).
using Weights = std::vector<float>;

struct Options
{
    std::string engine_path, out_path, bundles, cache = ".cache/fastrun", sides = "A";
    std::string bot_path[2], bot_name[2], params_path[2], games_path;
    std::vector<std::string> maps;
    int jobs = 1, rounds = 0, stdout_type = 0, procs = 1;
    std::string calls_log;
    std::size_t log_limit = 1u << 20;
};

struct Game
{
    EngineRt* E{};
    const Options* opt{};
    Program* program[2]{};  // program per team for this game
    const Weights* weights[2]{};  // learned weights per team (null or empty: the bot's own)
    std::int64_t handle{};
    std::int32_t width{}, height{};
    std::uint32_t chunked_w{}, chunked_h{}, stride{}, board_bytes{}, field_bytes{};
    std::uint64_t chunks{}, field_words{}, board_words{};  // fuel units of board and field work
    std::uint32_t bb[4]{}, df[2]{}, buf{}, buf_cap{};
    std::unique_ptr<Bot> draft[2];
    std::vector<std::unique_ptr<Bot>> champions;
    std::vector<std::uint32_t> layout;  // map_camp_layout words
    bool layout_ready = false;
    std::string log[2];
    std::vector<std::uint64_t> fuel[2];
    std::uint64_t fuel_max[2]{}, fuel_total[2]{};
    int exhausted[2]{}, faults[2]{};
    std::string fault_text;
    std::FILE* calls_out{};

    std::uint32_t scratch(std::uint32_t size)
    {
        if (size > buf_cap)
        {
            E->free(buf);
            buf_cap = std::max<std::uint32_t>(size, buf_cap * 2);
            buf = E->malloc(buf_cap);
        }
        return buf;
    }
};

// Charge `fuel` to the calling bot. The active chunk of the yield interval is resized so the turn still suspends
// when its budget runs out (wasmtime refills chunks from the reserve, so plain subtraction would move the stop).
void charge(wasmtime_caller_t* caller, std::uint64_t fuel)
{
    if (!fuel) return;
    auto* cx = wasmtime_caller_context(caller);
    if (g_counting)
    {
        Bot* b = static_cast<Bot*>(wasmtime_context_get_data(cx));
        if (b->charged.size() <= b->current_slot) b->charged.resize(b->current_slot + 1);
        b->charged[b->current_slot] += fuel;
    }
    std::uint64_t total = 0;
    wasmtime_context_get_fuel(cx, &total);
    const std::int64_t next = static_cast<std::int64_t>(total) - static_cast<std::int64_t>(kReserve) - static_cast<std::int64_t>(fuel);
    const std::uint64_t chunk = next > 0 ? static_cast<std::uint64_t>(next) : 1;
    wasmtime_context_fuel_async_yield_interval(cx, chunk);
    wasmtime_context_set_fuel(cx, kReserve + chunk);
}
inline std::uint64_t words(std::uint64_t bytes) { return (bytes + 7) / 8; }

inline Bot* bot_of(wasmtime_caller_t* caller) { return static_cast<Bot*>(wasmtime_context_get_data(wasmtime_caller_context(caller))); }

// Guest memory view; `ok` checks a range.
struct Guest
{
    std::uint8_t* base;
    std::size_t size;
    bool ok(std::uint32_t p, std::size_t n) const { return static_cast<std::size_t>(p) + n <= size; }
};
inline Guest guest(wasmtime_caller_t* caller, Bot* b)
{
    auto* cx = wasmtime_caller_context(caller);
    return { wasmtime_memory_data(cx, &b->memory), wasmtime_memory_data_size(cx, &b->memory) };
}

inline std::uint32_t perspective(Game& g, Bot* b, std::int32_t champion_override = -2)
{
    auto* m = g.E->mem() + g.E->s_persp;
    const std::uint32_t team = static_cast<std::uint32_t>(b->team);
    const std::uint32_t champ = champion_override != -2 ? static_cast<std::uint32_t>(champion_override)
                                : b->champion < 0 ? kNoChampion : static_cast<std::uint32_t>(b->champion);
    std::memcpy(m, &team, 4);
    std::memcpy(m + 4, &champ, 4);
    return g.E->s_persp;
}

#define HOST(name) wasm_trap_t* name(void*, wasmtime_caller_t* caller, Raw* a, std::size_t)
#define CTX                     \
    Bot* b = bot_of(caller);    \
    Game& g = *b->game;         \
    EngineRt& E = *g.E;         \
    (void)E;
#define ARG(i) a[i].i32
#define UARG(i) static_cast<std::uint32_t>(a[i].i32)

// Query with a fixed-size out record: engine(game, [persp], args..., out) then copy `size` bytes to guest `out`.
std::int32_t query(wasmtime_caller_t* caller, Bot* b, Fn f, bool persp, std::initializer_list<std::int32_t> args, std::uint32_t out,
                   std::uint32_t size, std::initializer_list<std::int32_t> tail = {})
{
    Game& g = *b->game;
    EngineRt& E = *g.E;
    const Guest gm = guest(caller, b);
    if (size && !gm.ok(out, size)) return -1;
    std::uint32_t eout = E.s_out;
    if (size > 4096) eout = g.scratch(size);
    std::memset(E.mem() + eout, 0, size);
    Raw raw[10];
    std::size_t n = 0;
    raw[n++] = I64(g.handle);
    if (persp) raw[n++] = I32(static_cast<std::int32_t>(perspective(g, b)));
    for (auto v : args) raw[n++] = I32(v);
    if (size) raw[n++] = I32(static_cast<std::int32_t>(eout));
    for (auto v : tail) raw[n++] = I32(v);
    if (n != E.nparams[f]) arity(FN_NAMES[f], n, E.nparams[f]);
    wasm_trap_t* trap = nullptr;
    if (auto* error = wasmtime_func_call_unchecked(E.ctx, &E.fn[f], raw, n, &trap)) die(FN_NAMES[f] + std::string(": ") + take_error(error));
    if (trap) die(FN_NAMES[f] + std::string(" trapped: ") + take_trap(trap));
    const std::int32_t status = raw[0].i32;
    if (status == 0 && size) std::memcpy(gm.base + out, E.mem() + eout, size);
    return status;
}

// List query: engine(game, [persp], args..., buf, cap, count) with `elem`-byte elements into guest (buf, cap, count).
std::int32_t list(wasmtime_caller_t* caller, Bot* b, Fn f, bool persp, std::initializer_list<std::int32_t> args, std::uint32_t gbuf,
                  std::uint32_t cap, std::uint32_t gcount, std::uint32_t elem, bool charged = true)
{
    Game& g = *b->game;
    EngineRt& E = *g.E;
    const Guest gm = guest(caller, b);
    if (!gm.ok(gcount, 4) || (cap && !gm.ok(gbuf, static_cast<std::size_t>(cap) * elem))) return -1;
    const std::uint32_t ebuf = g.scratch(std::max<std::uint32_t>(cap * elem, 8));
    std::memset(E.mem() + E.s_out, 0, 4);
    Raw raw[10];
    std::size_t n = 0;
    raw[n++] = I64(g.handle);
    if (persp) raw[n++] = I32(static_cast<std::int32_t>(perspective(g, b)));
    for (auto v : args) raw[n++] = I32(v);
    raw[n++] = I32(static_cast<std::int32_t>(cap ? ebuf : 0));
    raw[n++] = I32(static_cast<std::int32_t>(cap));
    raw[n++] = I32(static_cast<std::int32_t>(E.s_out));
    if (n != E.nparams[f]) arity(FN_NAMES[f], n, E.nparams[f]);
    wasm_trap_t* trap = nullptr;
    if (auto* error = wasmtime_func_call_unchecked(E.ctx, &E.fn[f], raw, n, &trap)) die(FN_NAMES[f] + std::string(": ") + take_error(error));
    if (trap) die(FN_NAMES[f] + std::string(" trapped: ") + take_trap(trap));
    const std::int32_t status = raw[0].i32;
    std::uint32_t count;
    std::memcpy(&count, E.mem() + E.s_out, 4);
    if (status == 0 || count)
    {
        if (cap) std::memcpy(gm.base + gbuf, E.mem() + ebuf, static_cast<std::size_t>(std::min(count, cap)) * elem);
        std::memcpy(gm.base + gcount, &count, 4);
        if (cap && charged) charge(caller, words(static_cast<std::uint64_t>(std::min(count, cap)) * elem));
    }
    return status;
}

// Plain call with i32 args after the game handle; returns the status.
std::int32_t action(Bot* b, Fn f, std::initializer_list<std::int32_t> args)
{
    Raw raw[10];
    std::size_t n = 0;
    raw[n++] = I64(b->game->handle);
    for (auto v : args) raw[n++] = I32(v);
    if (n != b->game->E->nparams[f]) arity(FN_NAMES[f], n, b->game->E->nparams[f]);
    wasm_trap_t* trap = nullptr;
    if (auto* error = wasmtime_func_call_unchecked(b->game->E->ctx, &b->game->E->fn[f], raw, n, &trap)) die(FN_NAMES[f] + std::string(": ") + take_error(error));
    if (trap) die(FN_NAMES[f] + std::string(" trapped: ") + take_trap(trap));
    return raw[0].i32;
}

inline void put_i32(EngineRt& E, std::uint32_t at, std::int32_t v0, std::int32_t v1)
{
    auto* m = E.mem() + at;
    std::memcpy(m, &v0, 4);
    std::memcpy(m + 4, &v1, 4);
}

// Bitboard / distance-field marshalling: guest buffers go to engine slots (one slot per distinct guest pointer, so
// aliasing behaves as on guest memory), and come back after the call.
struct Marshal
{
    wasmtime_caller_t* caller;
    Bot* b;
    Game& g;
    Guest gm;
    std::uint32_t gp[8]{}, ep[8]{}, sz[8]{};
    bool writes[8]{};
    int n = 0, boards = 0, fields = 0;
    bool bad = false;
    Marshal(wasmtime_caller_t* c, Bot* bot) : caller(c), b(bot), g(*bot->game), gm(guest(c, bot)) {}
    std::uint32_t add(std::uint32_t p, std::uint32_t size, bool write, bool field)
    {
        for (int i = 0; i < n; ++i)
            if (gp[i] == p && sz[i] == size) { writes[i] |= write; return ep[i]; }
        if (!gm.ok(p, size) || n == 8) { bad = true; return 0; }
        std::uint32_t e;
        if (field) { if (fields == 2) { bad = true; return 0; } e = g.df[fields++]; }
        else { if (boards == 4) { bad = true; return 0; } e = g.bb[boards++]; }
        std::memcpy(g.E->mem() + e, gm.base + p, size);
        gp[n] = p; ep[n] = e; sz[n] = size; writes[n] = write;
        return ep[n++];
    }
    std::uint32_t board(std::uint32_t p, bool write = false) { return add(p, g.board_bytes, write, false); }
    std::uint32_t field(std::uint32_t p, bool write = false) { return add(p, g.field_bytes, write, true); }
    std::int32_t call(Fn f, std::initializer_list<Raw> args)
    {
        if (bad) return -1;
        Raw raw[12];
        std::size_t k = 0;
        raw[k++] = I32(static_cast<std::int32_t>(g.E->s_meta));
        for (const auto& v : args) raw[k++] = v;
        if (k != g.E->nparams[f]) arity(FN_NAMES[f], k, g.E->nparams[f]);
        wasm_trap_t* trap = nullptr;
        if (auto* error = wasmtime_func_call_unchecked(g.E->ctx, &g.E->fn[f], raw, k, &trap)) die(FN_NAMES[f] + std::string(": ") + take_error(error));
        if (trap) die(FN_NAMES[f] + std::string(" trapped: ") + take_trap(trap));
        auto* m = g.E->mem();
        for (int i = 0; i < n; ++i)
            if (writes[i]) std::memcpy(gm.base + gp[i], m + ep[i], sz[i]);
        return raw[0].i32;
    }
};
inline Raw U(std::uint32_t v) { return I32(static_cast<std::int32_t>(v)); }

// Copy `n` bytes of engine scratch output to the guest (range-checked by the caller).
inline void out_bytes(Game& g, const Guest& gm, std::uint32_t gptr, std::uint32_t eptr, std::size_t n) { std::memcpy(gm.base + gptr, g.E->mem() + eptr, n); }

// ---------------------------------------------------------------- static catalogue
HOST(h_game_settings)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(0), 120)) { a[0].i32 = -1; return nullptr; }
    std::memset(E.mem() + E.s_out, 0, 120);
    const auto st = E.call(F_GetSettings, { U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(0), E.s_out, 120);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_item_info)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(1), 40)) { a[0].i32 = -1; return nullptr; }
    const auto st = E.call(F_GetItemInfo, { a[0], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(1), E.s_out, 40);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_consumable_info)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(1), 8)) { a[0].i32 = -1; return nullptr; }
    const auto st = E.call(F_GetConsumableInfo, { a[0], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(1), E.s_out, 8);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_champion_design) { CTX; a[0].i32 = query(caller, b, F_ChampionDesign, false, { ARG(0) }, UARG(1), 128); return nullptr; }
HOST(h_camp_design) { CTX; a[0].i32 = query(caller, b, F_CampDesign, false, { ARG(0) }, UARG(1), 124); return nullptr; }

// ---------------------------------------------------------------- identity, errors, fuel
HOST(h_my_champion) { CTX; a[0].i32 = b->champion; return nullptr; }
HOST(h_my_team) { CTX; a[0].i32 = b->team; return nullptr; }
HOST(h_fuel_remaining)
{
    std::uint64_t fuel = 0;
    wasmtime_context_get_fuel(wasmtime_caller_context(caller), &fuel);
    const auto remaining = static_cast<std::int64_t>(fuel) - static_cast<std::int64_t>(kReserve);
    a[0].i64 = std::max<std::int64_t>(remaining, 0);
    return nullptr;
}
HOST(h_last_error)
{
    CTX;
    const std::string text = E.last_error();
    const auto cap = UARG(1);
    if (cap)
    {
        const Guest gm = guest(caller, b);
        const auto n = std::min<std::size_t>(cap, text.size());
        if (!gm.ok(UARG(0), n)) { a[0].i32 = -1; return nullptr; }
        std::memcpy(gm.base + UARG(0), text.data(), n);
        charge(caller, words(n));
    }
    a[0].i32 = static_cast<std::int32_t>(text.size());
    return nullptr;
}
HOST(h_noop) { a[0].i32 = 0; return nullptr; }

// ---------------------------------------------------------------- champions
HOST(h_champion) { CTX; a[0].i32 = query(caller, b, F_ChampionGet, true, { ARG(0) }, UARG(1), 44); return nullptr; }
HOST(h_champion_identity) { CTX; a[0].i32 = query(caller, b, F_ChampionIdentity, true, { ARG(0) }, UARG(1), 8); return nullptr; }
HOST(h_champion_is_alive) { CTX; a[0].i32 = query(caller, b, F_ChampionIsAlive, false, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_champion_gold) { CTX; a[0].i32 = query(caller, b, F_ChampionGold, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_champion_gems) { CTX; a[0].i32 = query(caller, b, F_ChampionGems, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_champion_items) { CTX; a[0].i32 = query(caller, b, F_ChampionItems, true, { ARG(0) }, UARG(1), 42 * 4, { 42 }); return nullptr; }
HOST(h_champion_consumables) { CTX; a[0].i32 = query(caller, b, F_ChampionConsumables, true, { ARG(0) }, UARG(1), 4 * 4, { 4 }); return nullptr; }
HOST(h_champion_cooldown) { CTX; a[0].i32 = query(caller, b, F_ChampionCooldown, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_ability_props) { CTX; a[0].i32 = query(caller, b, F_AbilityProps, true, { ARG(0), ARG(1) }, UARG(2), 36); return nullptr; }
HOST(h_champion_ability_damage) { CTX; a[0].i32 = query(caller, b, F_AbilityDamage, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_ability_damage_against) { CTX; a[0].i32 = query(caller, b, F_AbilityDamageAgainst, true, { ARG(0), ARG(1), ARG(2) }, UARG(3), 4); return nullptr; }
HOST(h_champion_ability_heal) { CTX; a[0].i32 = query(caller, b, F_AbilityHeal, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_ability_heal_against) { CTX; a[0].i32 = query(caller, b, F_AbilityHealAgainst, true, { ARG(0), ARG(1), ARG(2) }, UARG(3), 4); return nullptr; }
HOST(h_champion_ability_shield) { CTX; a[0].i32 = query(caller, b, F_AbilityShield, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_ability_shield_against) { CTX; a[0].i32 = query(caller, b, F_AbilityShieldAgainst, true, { ARG(0), ARG(1), ARG(2) }, UARG(3), 4); return nullptr; }
HOST(h_champion_buff_data)
{
    CTX;
    a[0].i32 = list(caller, b, F_BuffData, true, { ARG(0) }, UARG(1), UARG(2), UARG(3), 12, false);  // free on the platform
    if (kTrace) { const Guest gm = guest(caller, b); std::uint32_t n = 0; if (gm.ok(UARG(3), 4)) std::memcpy(&n, gm.base + UARG(3), 4); TRACE("arg buff_data cap %u count %u status %d\n", UARG(2), n, a[0].i32); }
    return nullptr;
}
HOST(h_champion_restrictions) { CTX; a[0].i32 = query(caller, b, F_ChampionRestrictions, true, { ARG(0) }, UARG(1), 1); return nullptr; }
HOST(h_champion_wards) { CTX; a[0].i32 = query(caller, b, F_ChampionWards, true, { ARG(0) }, UARG(1), 8); return nullptr; }
HOST(h_champion_ward_positions) { CTX; a[0].i32 = list(caller, b, F_WardPositions, true, { ARG(0) }, UARG(1), UARG(2), UARG(3), 8); return nullptr; }
HOST(h_champion_channel_time) { CTX; a[0].i32 = query(caller, b, F_ChannelTime, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_champion_can_shop) { CTX; a[0].i32 = query(caller, b, F_ChampionCanShop, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_can_sell) { CTX; a[0].i32 = query(caller, b, F_ChampionCanSell, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_champion_priority) { CTX; a[0].i32 = query(caller, b, F_Priority, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_num_allies) { CTX; a[0].i32 = query(caller, b, F_NumAllies, true, {}, UARG(0), 4); return nullptr; }
HOST(h_num_enemies) { CTX; a[0].i32 = query(caller, b, F_NumEnemies, true, {}, UARG(0), 4); return nullptr; }
HOST(h_allies) { CTX; a[0].i32 = list(caller, b, F_ChampionAllies, true, {}, UARG(0), UARG(1), UARG(2), 4); return nullptr; }
HOST(h_enemies) { CTX; a[0].i32 = list(caller, b, F_ChampionEnemies, true, {}, UARG(0), UARG(1), UARG(2), 4); return nullptr; }
HOST(h_champion_vision)
{
    CTX;
    // A champion's own vision: that champion's perspective on our team.
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(1), g.board_bytes)) { a[0].i32 = -1; return nullptr; }
    std::memset(E.mem() + g.bb[0], 0, g.board_bytes);
    const auto p = perspective(g, b, ARG(0));
    const auto st = action(b, F_GetVision, { static_cast<std::int32_t>(p), static_cast<std::int32_t>(g.bb[0]) });
    if (st == 0) out_bytes(g, gm, UARG(1), g.bb[0], g.board_bytes);
    charge(caller, g.chunks);
    a[0].i32 = st;
    return nullptr;
}

// ---------------------------------------------------------------- boards and tiles
#define BOARD_QUERY(fname, F) HOST(fname) { CTX; a[0].i32 = query(caller, b, F, true, {}, UARG(0), g.board_bytes); charge(caller, g.chunks); return nullptr; }
BOARD_QUERY(h_ally_occupancy, F_AllyOcc)
BOARD_QUERY(h_enemy_occupancy, F_EnemyOcc)
BOARD_QUERY(h_camp_occupancy, F_CampOcc)
BOARD_QUERY(h_awake_camp_occupancy, F_AwakeCampOcc)
BOARD_QUERY(h_asleep_camp_occupancy, F_AsleepCampOcc)
BOARD_QUERY(h_ally_ward_occupancy, F_AllyWardOcc)
BOARD_QUERY(h_enemy_ward_occupancy, F_EnemyWardOcc)
BOARD_QUERY(h_teleport_occupancy, F_TeleportOcc)
BOARD_QUERY(h_ally_altar_lock_occupancy, F_AllyAltarLockOcc)
BOARD_QUERY(h_enemy_altar_lock_occupancy, F_EnemyAltarLockOcc)
BOARD_QUERY(h_vision, F_GetVision)
HOST(h_hypothetical_vision) { CTX; a[0].i32 = query(caller, b, F_VisionMask, true, { ARG(0), ARG(1), ARG(2) }, UARG(3), g.board_bytes); charge(caller, 2 * g.chunks); return nullptr; }
HOST(h_ward_vision) { CTX; a[0].i32 = query(caller, b, F_WardVisionMask, true, { ARG(0), ARG(1) }, UARG(2), g.board_bytes); charge(caller, 2 * g.chunks); return nullptr; }
HOST(h_is_visible) { CTX; a[0].i32 = query(caller, b, F_InVision, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_has_camp) { CTX; a[0].i32 = query(caller, b, F_HasCamp, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_has_champion) { CTX; a[0].i32 = query(caller, b, F_HasChampion, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_camp_at) { CTX; a[0].i32 = query(caller, b, F_GetCamp, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_champion_at) { CTX; a[0].i32 = query(caller, b, F_GetChampion, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_is_occupied_by_teleport) { CTX; a[0].i32 = query(caller, b, F_IsOccupiedByTeleport, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_teleports) { CTX; a[0].i32 = list(caller, b, F_GetTeleports, true, {}, UARG(0), UARG(1), UARG(2), 28); return nullptr; }
HOST(h_teleport_at) { CTX; a[0].i32 = query(caller, b, F_GetTeleport, true, { ARG(0), ARG(1) }, UARG(2), 28); return nullptr; }
HOST(h_ally_ward_at) { CTX; a[0].i32 = query(caller, b, F_GetAllyWard, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_enemy_ward_at) { CTX; a[0].i32 = query(caller, b, F_GetEnemyWard, true, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_ward_at)
{
    CTX;
    // Either team's ward: ours first, then theirs; "not in vision" wins over "not found" when both fail.
    const auto first = query(caller, b, F_GetAllyWard, true, { ARG(0), ARG(1) }, UARG(2), 4);
    if (first == 0) { a[0].i32 = 0; return nullptr; }
    const auto second = query(caller, b, F_GetEnemyWard, true, { ARG(0), ARG(1) }, UARG(2), 4);
    a[0].i32 = second;
    (void)first;
    return nullptr;
}
HOST(h_ward) { CTX; a[0].i32 = query(caller, b, F_WardGet, true, { ARG(0) }, UARG(1), 24); return nullptr; }
HOST(h_wards) { CTX; a[0].i32 = list(caller, b, F_GetWards, true, {}, UARG(0), UARG(1), UARG(2), 24); return nullptr; }
HOST(h_altar_lock) { CTX; a[0].i32 = query(caller, b, F_GetAltarLock, true, { ARG(0), ARG(1), ARG(2) }, UARG(3), 24); return nullptr; }
HOST(h_altar_locks) { CTX; a[0].i32 = list(caller, b, F_GetAltarLocks, true, {}, UARG(0), UARG(1), UARG(2), 24); return nullptr; }

// ---------------------------------------------------------------- map and camps
HOST(h_metadata)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(0), 28)) { a[0].i32 = -1; return nullptr; }
    const std::uint32_t words[7] = { static_cast<std::uint32_t>(g.width), static_cast<std::uint32_t>(g.height), g.chunked_w, g.chunked_h, g.stride,
                                     g.board_bytes, g.field_bytes };
    std::memcpy(gm.base + UARG(0), words, 28);
    a[0].i32 = 0;
    return nullptr;
}
HOST(h_map_mask)
{
    CTX;
    static const Fn masks[4] = { F_MapWall, F_MapBush, F_MapHole, F_MapAltar };
    if (ARG(0) < 0 || ARG(0) > 3) { a[0].i32 = 1; return nullptr; }
    a[0].i32 = query(caller, b, masks[ARG(0)], true, {}, UARG(1), g.board_bytes);
    charge(caller, g.chunks);
    return nullptr;
}
HOST(h_map_symmetry) { CTX; a[0].i32 = query(caller, b, F_MapSymmetry, false, {}, UARG(0), 4); return nullptr; }
HOST(h_spawn_region) { CTX; a[0].i32 = query(caller, b, F_SpawnRegion, false, { ARG(0) }, UARG(1), 16); return nullptr; }
HOST(h_round_number) { CTX; a[0].i32 = query(caller, b, F_RoundNumber, false, {}, UARG(0), 4); return nullptr; }
HOST(h_submitted_gems) { CTX; a[0].i32 = query(caller, b, F_SubmittedGems, false, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_map_num_camp_groups) { CTX; a[0].i32 = query(caller, b, F_MapNumGroups, false, {}, UARG(0), 4); return nullptr; }
HOST(h_map_camp_groups) { CTX; a[0].i32 = list(caller, b, F_MapGroups, false, {}, UARG(0), UARG(1), UARG(2), 4); return nullptr; }
HOST(h_map_camp_group_num_camps) { CTX; a[0].i32 = query(caller, b, F_MapGroupNumCamps, false, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_map_camp_group_camps) { CTX; a[0].i32 = list(caller, b, F_MapGroupCampInfo, false, { ARG(0) }, UARG(1), UARG(2), UARG(3), 4); return nullptr; }
HOST(h_map_camp_group) { CTX; a[0].i32 = query(caller, b, F_MapGroupGet, false, { ARG(0) }, UARG(1), 8); return nullptr; }
HOST(h_map_camp_info) { CTX; a[0].i32 = query(caller, b, F_MapCampInfoGet, false, { ARG(0) }, UARG(1), 12); return nullptr; }
HOST(h_map_camp_group_of) { CTX; a[0].i32 = query(caller, b, F_MapCampInfoGroup, false, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_map_camp_at) { CTX; a[0].i32 = query(caller, b, F_MapCampAt, false, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_camp_group_max_respawn) { CTX; a[0].i32 = query(caller, b, F_GroupRespawnTime, false, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_camp_group_state) { CTX; a[0].i32 = query(caller, b, F_GroupState, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_camp_group_cooldown) { CTX; a[0].i32 = query(caller, b, F_GroupCooldown, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_camp) { CTX; a[0].i32 = query(caller, b, F_CampGet, true, { ARG(0) }, UARG(1), 32); return nullptr; }
HOST(h_camp_state) { CTX; a[0].i32 = query(caller, b, F_CampGetState, true, { ARG(0) }, UARG(1), 4); return nullptr; }
HOST(h_camp_buffs) { CTX; a[0].i32 = list(caller, b, F_CampGetBuffs, true, { ARG(0) }, UARG(1), UARG(2), UARG(3), 12); return nullptr; }

// Batched static layout: per group [group, gold, gems, respawn, size] then size x [camp, type, x, y].
void build_layout(Game& g)
{
    EngineRt& E = *g.E;
    auto u32 = [&](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, E.mem() + at, 4); return v; };
    auto must = [&](std::int32_t st, const char* what) { if (st) die(std::string("layout: ") + what + " failed: " + E.last_error()); };
    must(E.call(F_MapNumGroups, { I64(g.handle), U(E.s_out) }), "groups");
    const auto groups = u32(E.s_out);
    const auto ids = g.scratch(std::max<std::uint32_t>(groups * 4, 8) + 64);
    must(E.call(F_MapGroups, { I64(g.handle), U(ids), U(groups), U(E.s_out) }), "group list");
    std::vector<std::uint32_t> group_ids(groups);
    for (std::uint32_t i = 0; i < groups; ++i) group_ids[i] = u32(ids + i * 4);
    for (auto id : group_ids)
    {
        must(E.call(F_MapGroupGet, { I64(g.handle), U(id), U(E.s_out) }), "group");
        const auto gems = u32(E.s_out), gold = u32(E.s_out + 4);
        must(E.call(F_GroupRespawnTime, { I64(g.handle), U(id), U(E.s_out) }), "respawn");
        const auto respawn = u32(E.s_out);
        must(E.call(F_MapGroupNumCamps, { I64(g.handle), U(id), U(E.s_out) }), "camps");
        const auto members = u32(E.s_out);
        const auto camps = g.scratch(std::max<std::uint32_t>(members * 4, 8) + 64);
        must(E.call(F_MapGroupCampInfo, { I64(g.handle), U(id), U(camps), U(members), U(E.s_out) }), "camp list");
        std::vector<std::uint32_t> camp_ids(members);
        for (std::uint32_t i = 0; i < members; ++i) camp_ids[i] = u32(camps + i * 4);
        g.layout.insert(g.layout.end(), { id, gold, gems, respawn, members });
        for (auto c : camp_ids)
        {
            must(E.call(F_MapCampInfoGet, { I64(g.handle), U(c), U(E.s_out) }), "camp");
            const auto x = u32(E.s_out), y = u32(E.s_out + 4), type = u32(E.s_out + 8);
            g.layout.insert(g.layout.end(), { c, type, x, y });
        }
    }
    g.layout_ready = true;
}
HOST(h_map_camp_layout)
{
    CTX;
    if (!g.layout_ready) build_layout(g);
    const Guest gm = guest(caller, b);
    const auto cap = UARG(1);
    if (!gm.ok(UARG(2), 4) || (cap && !gm.ok(UARG(0), static_cast<std::size_t>(cap) * 4))) { a[0].i32 = -1; return nullptr; }
    const auto count = static_cast<std::uint32_t>(g.layout.size());
    // A short buffer still receives the first `cap` words (and is charged for them), as on the platform.
    const auto copied = std::min(cap, count);
    if (copied) { std::memcpy(gm.base + UARG(0), g.layout.data(), static_cast<std::size_t>(copied) * 4); charge(caller, words(copied * 4ull)); }
    std::memcpy(gm.base + UARG(2), &count, 4);
    a[0].i32 = 0;
    return nullptr;
}

// ---------------------------------------------------------------- memory (team)
HOST(h_shared_memory_size) { CTX; a[0].i32 = query(caller, b, F_SharedSize, false, {}, UARG(0), 4); return nullptr; }
HOST(h_draft_memory_size) { CTX; a[0].i32 = query(caller, b, F_DraftSize, false, {}, UARG(0), 4); return nullptr; }
std::int32_t team_read(wasmtime_caller_t* caller, Bot* b, Fn f, std::int32_t offset, std::uint32_t gptr, std::uint32_t len)
{
    Game& g = *b->game;
    const Guest gm = guest(caller, b);
    if (!gm.ok(gptr, len)) return -1;
    const auto e = g.scratch(std::max<std::uint32_t>(len, 8));
    const auto st = action(b, f, { b->team, offset, static_cast<std::int32_t>(e), static_cast<std::int32_t>(len) });
    if (st == 0) out_bytes(g, gm, gptr, e, len);
    charge(caller, words(len));
    return st;
}
std::int32_t team_write(wasmtime_caller_t* caller, Bot* b, Fn f, std::int32_t offset, std::uint32_t gptr, std::uint32_t len)
{
    Game& g = *b->game;
    const Guest gm = guest(caller, b);
    if (!gm.ok(gptr, len)) return -1;
    const auto e = g.scratch(std::max<std::uint32_t>(len, 8));
    std::memcpy(g.E->mem() + e, gm.base + gptr, len);
    charge(caller, words(len));
    return action(b, f, { b->team, offset, static_cast<std::int32_t>(e), static_cast<std::int32_t>(len) });
}
HOST(h_read_shared_memory) { CTX; a[0].i32 = team_read(caller, b, F_ReadShared, ARG(0), UARG(1), UARG(2)); return nullptr; }
HOST(h_write_shared_memory) { CTX; a[0].i32 = team_write(caller, b, F_WriteShared, ARG(0), UARG(1), UARG(2)); return nullptr; }
HOST(h_read_draft_memory) { CTX; a[0].i32 = team_read(caller, b, F_ReadDraft, ARG(0), UARG(1), UARG(2)); return nullptr; }
HOST(h_submit_draft_memory)
{
    CTX;
    // Oversized input is truncated to capacity; shorter input leaves the remaining bytes unchanged.
    std::memset(E.mem() + E.s_out, 0, 4);
    E.call(F_DraftSize, { I64(g.handle), U(E.s_out) });
    std::uint32_t capacity;
    std::memcpy(&capacity, E.mem() + E.s_out, 4);
    a[0].i32 = team_write(caller, b, F_WriteDraft, 0, UARG(0), std::min<std::uint32_t>(UARG(1), capacity));
    return nullptr;
}

// ---------------------------------------------------------------- draft
HOST(h_draft_num_rounds) { CTX; a[0].i32 = query(caller, b, F_DraftNumRounds, false, {}, UARG(0), 1); return nullptr; }
HOST(h_draft_get) { CTX; a[0].i32 = query(caller, b, F_DraftGet, false, { ARG(0), ARG(1) }, UARG(2), 4); return nullptr; }
HOST(h_draft_champion) { CTX; a[0].i32 = action(b, F_Draft, { ARG(0) }); return nullptr; }

// ---------------------------------------------------------------- actions
HOST(h_attack)
{
    CTX;
    put_i32(E, E.s_ent, ARG(0), ARG(1));
    a[0].i32 = action(b, F_Attack, { static_cast<std::int32_t>(E.s_ent) });
    return nullptr;
}
HOST(h_cast)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(5), 4)) { a[0].i32 = -1; return nullptr; }
    put_i32(E, E.s_ent, ARG(1), ARG(2));
    put_i32(E, E.s_pos, ARG(3), ARG(4));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = action(b, F_Cast, { ARG(0), static_cast<std::int32_t>(E.s_ent), static_cast<std::int32_t>(E.s_pos), static_cast<std::int32_t>(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(5), E.s_out, 4);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_use_consumable)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(3), 4)) { a[0].i32 = -1; return nullptr; }
    put_i32(E, E.s_pos, ARG(1), ARG(2));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = action(b, F_UseConsumable, { ARG(0), static_cast<std::int32_t>(E.s_pos), static_cast<std::int32_t>(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(3), E.s_out, 4);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_place_ward)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(2), 4)) { a[0].i32 = -1; return nullptr; }
    put_i32(E, E.s_pos, ARG(0), ARG(1));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = action(b, F_PlaceWard, { static_cast<std::int32_t>(E.s_pos), static_cast<std::int32_t>(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(2), E.s_out, 4);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_move_to)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(2), 4)) { a[0].i32 = -1; return nullptr; }
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = action(b, F_Move, { ARG(0), ARG(1), static_cast<std::int32_t>(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(2), E.s_out, 4);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_submit_gems)
{
    CTX;
    put_i32(E, E.s_pos, ARG(0), ARG(1));
    a[0].i32 = action(b, F_SubmitGems, { static_cast<std::int32_t>(E.s_pos) });
    return nullptr;
}
HOST(h_buy_consumable) { CTX; a[0].i32 = action(b, F_BuyConsumable, { ARG(0) }); return nullptr; }
HOST(h_buy_item) { CTX; a[0].i32 = action(b, F_BuyItem, { ARG(0), ARG(1) }); return nullptr; }
HOST(h_sell) { CTX; a[0].i32 = action(b, F_Sell, { ARG(0) }); return nullptr; }
HOST(h_recall) { CTX; a[0].i32 = action(b, F_Recall, {}); return nullptr; }
HOST(h_cancel_channel) { CTX; a[0].i32 = action(b, F_CancelChannel, {}); return nullptr; }
HOST(h_set_priority) { CTX; a[0].i32 = action(b, F_SetPriority, { ARG(0) }); return nullptr; }
HOST(h_resign) { CTX; a[0].i32 = action(b, F_Resign, {}); return nullptr; }

// ---------------------------------------------------------------- bitboards
#define M Marshal m(caller, b)
HOST(h_bb_zero) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_Zero, { U(p) }); charge(caller, g.chunks); return nullptr; }
HOST(h_bb_one) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_One, { U(p) }); charge(caller, g.chunks); return nullptr; }
HOST(h_bb_import_raw)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    const auto size = UARG(2);
    if (!gm.ok(UARG(1), size)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0), true);
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    std::memcpy(E.mem() + e, gm.base + UARG(1), size);
    a[0].i32 = m.call(F_BB_ImportRaw, { U(p), U(e), U(size) });
    charge(caller, g.chunks);
    return nullptr;
}
HOST(h_bb_import_linear)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    const auto size = UARG(2);
    if (!gm.ok(UARG(1), size)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0), true);
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    std::memcpy(E.mem() + e, gm.base + UARG(1), size);
    a[0].i32 = m.call(F_BB_ImportLinear, { U(p), U(e), U(size) });
    TRACE("arg import_linear size %u status %d\n", size, a[0].i32);
    charge(caller, g.chunks);
    return nullptr;
}
HOST(h_bb_export_raw)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    const auto size = UARG(2);
    if (!gm.ok(UARG(1), size)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0));
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    const auto st = m.call(F_BB_ExportRaw, { U(p), U(e), U(size) });
    if (st == 0) out_bytes(g, gm, UARG(1), e, size);
    charge(caller, g.chunks);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_bb_export_linear)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    const auto size = UARG(2);
    if (!gm.ok(UARG(1), size)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0));
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    const auto st = m.call(F_BB_ExportLinear, { U(p), U(e), U(size) });
    if (st == 0) out_bytes(g, gm, UARG(1), e, size);
    charge(caller, g.chunks);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_bb_test)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(3), 4)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_BB_Test, { U(p), a[1], a[2], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(3), E.s_out, 4);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_bb_set) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_Set, { U(p), a[1], a[2], a[3] }); return nullptr; }
HOST(h_bb_toggle) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_Toggle, { U(p), a[1], a[2] }); return nullptr; }
std::int32_t board_scalar(wasmtime_caller_t* caller, Bot* b, Fn f, std::uint32_t board, std::uint32_t out)
{
    Marshal m(caller, b);
    Game& g = *b->game;
    const Guest gm = guest(caller, b);
    if (!gm.ok(out, 4)) return -1;
    const auto p = m.board(board);
    std::memset(g.E->mem() + g.E->s_out, 0, 4);
    const auto st = m.call(f, { U(p), U(g.E->s_out) });
    if (st == 0) out_bytes(g, gm, out, g.E->s_out, 4);
    charge(caller, g.chunks);
    return st;
}
HOST(h_bb_is_zero) { CTX; a[0].i32 = board_scalar(caller, b, F_BB_IsZero, UARG(0), UARG(1)); return nullptr; }
HOST(h_bb_num_set_bits) { CTX; a[0].i32 = board_scalar(caller, b, F_BB_NumSetBits, UARG(0), UARG(1)); return nullptr; }
HOST(h_bb_get_set_bits)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    const auto cap = UARG(2);
    if (!gm.ok(UARG(3), 4) || (cap && !gm.ok(UARG(1), static_cast<std::size_t>(cap) * 8))) { a[0].i32 = -1; return nullptr; }
    const auto p = m.board(UARG(0));
    const auto e = g.scratch(std::max<std::uint32_t>(cap * 8, 8));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_BB_GetSetBits, { U(p), U(cap ? e : 0), U(cap), U(E.s_out) });
    std::uint32_t count;
    std::memcpy(&count, E.mem() + E.s_out, 4);
    if (st == 0 || count)
    {
        if (cap) out_bytes(g, gm, UARG(1), e, static_cast<std::size_t>(std::min(count, cap)) * 8);
        std::memcpy(gm.base + UARG(3), &count, 4);
    }
    charge(caller, g.chunks + (cap ? std::min(count, cap) : 0));
    a[0].i32 = st;
    return nullptr;
}
HOST(h_bb_not) { CTX; M; const auto x = m.board(UARG(0)); const auto r = m.board(UARG(1), true); a[0].i32 = m.call(F_BB_Not, { U(x), U(r) }); charge(caller, g.chunks); return nullptr; }
#define BB_BINARY(fname, F)                                                                                         \
    HOST(fname)                                                                                                     \
    {                                                                                                               \
        CTX; M;                                                                                                     \
        const auto x = m.board(UARG(0)); const auto y = m.board(UARG(1)); const auto r = m.board(UARG(2), true);   \
        a[0].i32 = m.call(F, { U(x), U(y), U(r) }); charge(caller, g.chunks);                                                                 \
        return nullptr;                                                                                             \
    }
BB_BINARY(h_bb_union, F_BB_Union)
BB_BINARY(h_bb_intersect, F_BB_Intersect)
BB_BINARY(h_bb_xor, F_BB_Xor)
HOST(h_bb_is_equal)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(2), 4)) { a[0].i32 = -1; return nullptr; }
    const auto x = m.board(UARG(0)), y = m.board(UARG(1));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_BB_IsEqual, { U(x), U(y), U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(2), E.s_out, 4);
    charge(caller, g.chunks);
    a[0].i32 = st;
    return nullptr;
}
#define BB_TRANSFORM(fname, F)                                                                  \
    HOST(fname)                                                                                 \
    {                                                                                           \
        CTX; M;                                                                                 \
        const auto x = m.board(UARG(0)); const auto r = m.board(UARG(1), true);                 \
        a[0].i32 = m.call(F, { U(x), U(r), a[2] }); charge(caller, g.chunks);                                             \
        return nullptr;                                                                         \
    }
BB_TRANSFORM(h_bb_shift_right, F_BB_ShiftRight)
BB_TRANSFORM(h_bb_shift_up, F_BB_ShiftUp)
BB_TRANSFORM(h_bb_reflect, F_BB_Reflect)
std::uint64_t square_chunks(const Game& g, std::int32_t x, std::int32_t y, float radius)
{
    const auto r = static_cast<std::int32_t>(radius);
    const auto x0 = std::max(0, x - r), x1 = std::min(g.width - 1, x + r), y0 = std::max(0, y - r), y1 = std::min(g.height - 1, y + r);
    if (x0 > x1 || y0 > y1 || r < 0) return 0;
    return static_cast<std::uint64_t>((x1 >> 3) - (x0 >> 3) + 1) * static_cast<std::uint64_t>((y1 >> 3) - (y0 >> 3) + 1);
}
HOST(h_bb_draw_circle) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_DrawCircle, { U(p), a[1], a[2], a[3] }); charge(caller, square_chunks(g, ARG(1), ARG(2), a[3].f32)); return nullptr; }
HOST(h_bb_draw_chebyshev) { CTX; M; const auto p = m.board(UARG(0), true); a[0].i32 = m.call(F_BB_DrawChebyshev, { U(p), a[1], a[2], a[3] }); charge(caller, square_chunks(g, ARG(1), ARG(2), a[3].f32)); return nullptr; }
std::int32_t reflect_position(wasmtime_caller_t* caller, Bot* b, Fn f, Raw* a)
{
    Game& g = *b->game;
    const Guest gm = guest(caller, b);
    if (!gm.ok(static_cast<std::uint32_t>(a[2].i32), 4) || !gm.ok(static_cast<std::uint32_t>(a[3].i32), 4)) return -1;
    Marshal m(caller, b);
    std::memset(g.E->mem() + g.E->s_out, 0, 8);
    const auto st = m.call(f, { a[0], a[1], U(g.E->s_out), U(g.E->s_out + 4) });
    if (st == 0)
    {
        out_bytes(g, gm, static_cast<std::uint32_t>(a[2].i32), g.E->s_out, 4);
        out_bytes(g, gm, static_cast<std::uint32_t>(a[3].i32), g.E->s_out + 4, 4);
    }
    return st;
}
HOST(h_bb_reflect_h_pos) { CTX; a[0].i32 = reflect_position(caller, b, F_BB_ReflectHPos, a); return nullptr; }
HOST(h_bb_reflect_v_pos) { CTX; a[0].i32 = reflect_position(caller, b, F_BB_ReflectVPos, a); return nullptr; }
HOST(h_bb_rotate_pos) { CTX; a[0].i32 = reflect_position(caller, b, F_BB_Rot180Pos, a); return nullptr; }
std::uint32_t read_count(Game& g) { std::uint32_t v = 0; std::memcpy(&v, g.E->mem() + g.E->s_out, 4); return v; }

// Platform fuel for a reachability search (calibrated on 42 probes, D336): chunks + 2, then per step the chunks of
// the bounding rectangle of everything reached so far, plus (step + 1) for each chunk entering that rectangle.
// Replays the engine's 8-neighbour search over the walls board for `steps` steps.
std::uint64_t reach_fuel(const Game& g, const std::uint8_t* walls, std::int32_t x, std::int32_t y, std::uint32_t steps)
{
    const std::int32_t w = g.width, h = g.height;
    std::uint64_t total = g.chunks + 2;
    if (x < 0 || y < 0 || x >= w || y >= h) return total;
    auto wall = [&](std::int32_t tx, std::int32_t ty) {
        const auto word = static_cast<std::size_t>((ty >> 3) * static_cast<std::int32_t>(g.stride) + (tx >> 3));
        std::uint64_t bits;
        std::memcpy(&bits, walls + word * 8, 8);
        return (bits >> (((ty & 7) << 3) + (tx & 7))) & 1u;
    };
    thread_local std::vector<std::uint32_t> seen_tile, frontier, next;
    thread_local std::uint32_t epoch = 0;
    if (seen_tile.size() < static_cast<std::size_t>(w) * h) seen_tile.assign(static_cast<std::size_t>(w) * h, 0);
    if (++epoch == 0) { std::fill(seen_tile.begin(), seen_tile.end(), 0); epoch = 1; }
    frontier.clear();
    frontier.push_back(static_cast<std::uint32_t>(y * w + x));
    seen_tile[static_cast<std::size_t>(y * w + x)] = epoch;
    std::int32_t cx0 = x >> 3, cx1 = x >> 3, cy0 = y >> 3, cy1 = y >> 3;  // rectangle of reached chunks
    std::int32_t sx0 = cx0, sx1 = cx1, sy0 = cy0, sy1 = cy1;              // largest rectangle charged so far
    for (std::uint32_t s = 1; s <= steps && !frontier.empty(); ++s)
    {
        next.clear();
        for (auto t : frontier)
        {
            const std::int32_t tx = static_cast<std::int32_t>(t) % w, ty = static_cast<std::int32_t>(t) / w;
            for (std::int32_t dy = -1; dy <= 1; ++dy)
                for (std::int32_t dx = -1; dx <= 1; ++dx)
                {
                    const std::int32_t nx = tx + dx, ny = ty + dy;
                    if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const auto u = static_cast<std::size_t>(ny * w + nx);
                    if (seen_tile[u] == epoch || wall(nx, ny)) continue;
                    seen_tile[u] = epoch;
                    next.push_back(static_cast<std::uint32_t>(u));
                    cx0 = std::min(cx0, nx >> 3); cx1 = std::max(cx1, nx >> 3);
                    cy0 = std::min(cy0, ny >> 3); cy1 = std::max(cy1, ny >> 3);
                }
        }
        const std::uint64_t area = static_cast<std::uint64_t>(cx1 - cx0 + 1) * static_cast<std::uint64_t>(cy1 - cy0 + 1);
        const std::int32_t ox0 = std::max(cx0, sx0), ox1 = std::min(cx1, sx1), oy0 = std::max(cy0, sy0), oy1 = std::min(cy1, sy1);
        const std::uint64_t overlap = (ox0 <= ox1 && oy0 <= oy1) ? static_cast<std::uint64_t>(ox1 - ox0 + 1) * static_cast<std::uint64_t>(oy1 - oy0 + 1) : 0;
        total += area + (s + 1) * (area - overlap);
        sx0 = std::min(sx0, cx0); sx1 = std::max(sx1, cx1); sy0 = std::min(sy0, cy0); sy1 = std::max(sy1, cy1);
        frontier.swap(next);
    }
    return total;
}

HOST(h_bb_reachable)
{
    CTX; M;
    const auto w = m.board(UARG(0)); const auto r = m.board(UARG(3), true);
    std::memset(E.mem() + E.s_out, 0, 4);
    a[0].i32 = m.call(F_BB_ReachableCount, { U(w), a[1], a[2], U(r), a[4], U(E.s_out) });
    const auto n = read_count(g);
    TRACE("reachable count %u chunks %llu\n", n, static_cast<unsigned long long>(g.chunks));
    charge(caller, reach_fuel(g, E.mem() + w, ARG(1), ARG(2), n));
    return nullptr;
}
HOST(h_bb_shadow_cast)
{
    CTX; M;
    const auto w = m.board(UARG(0)); const auto r = m.board(UARG(3), true);
    std::memset(E.mem() + E.s_out, 0, 4);
    a[0].i32 = m.call(F_BB_ShadowCount, { U(w), a[1], a[2], U(r), a[4], U(E.s_out) });
    const auto n = read_count(g);
    TRACE("shadow count %u chunks %llu\n", n, static_cast<unsigned long long>(g.chunks));
    charge(caller, 2 * g.chunks + n / 2);  // calibrated on 42 probes (D336)
    return nullptr;
}
HOST(h_bb_reachable_scratch)
{
    CTX; M;
    const auto w = m.board(UARG(0)); const auto r = m.board(UARG(3), true);
    const auto fr = m.board(UARG(5), true); const auto sc = m.board(UARG(6), true);
    std::memset(E.mem() + E.s_out, 0, 4);
    a[0].i32 = m.call(F_BB_ReachableScratchCount, { U(w), a[1], a[2], U(r), a[4], U(fr), U(sc), U(E.s_out) });
    charge(caller, reach_fuel(g, E.mem() + w, ARG(1), ARG(2), read_count(g)));
    return nullptr;
}

// ---------------------------------------------------------------- distance fields
std::int32_t field_import(wasmtime_caller_t* caller, Bot* b, Fn f, Raw* a)
{
    Game& g = *b->game;
    Marshal m(caller, b);
    const Guest gm = guest(caller, b);
    const auto size = static_cast<std::uint32_t>(a[2].i32);
    if (!gm.ok(static_cast<std::uint32_t>(a[1].i32), size)) return -1;
    const auto p = m.field(static_cast<std::uint32_t>(a[0].i32), true);
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    std::memcpy(g.E->mem() + e, gm.base + static_cast<std::uint32_t>(a[1].i32), size);
    const auto st = m.call(f, { U(p), U(e), U(size) });
    charge(caller, f == F_DF_ImportRaw ? g.field_words : words(size));
    return st;
}
std::int32_t field_export(wasmtime_caller_t* caller, Bot* b, Fn f, Raw* a)
{
    Game& g = *b->game;
    Marshal m(caller, b);
    const Guest gm = guest(caller, b);
    const auto size = static_cast<std::uint32_t>(a[2].i32);
    if (!gm.ok(static_cast<std::uint32_t>(a[1].i32), size)) return -1;
    const auto p = m.field(static_cast<std::uint32_t>(a[0].i32));
    const auto e = g.scratch(std::max<std::uint32_t>(size, 8));
    const auto st = m.call(f, { U(p), U(e), U(size) });
    if (st == 0) out_bytes(g, gm, static_cast<std::uint32_t>(a[1].i32), e, size);
    charge(caller, f == F_DF_ExportRaw ? g.field_words : words(size));
    return st;
}
HOST(h_df_import_raw) { CTX; a[0].i32 = field_import(caller, b, F_DF_ImportRaw, a); return nullptr; }
HOST(h_df_import_linear) { CTX; a[0].i32 = field_import(caller, b, F_DF_ImportLinear, a); return nullptr; }
HOST(h_df_export_raw) { CTX; a[0].i32 = field_export(caller, b, F_DF_ExportRaw, a); return nullptr; }
HOST(h_df_export_linear) { CTX; a[0].i32 = field_export(caller, b, F_DF_ExportLinear, a); return nullptr; }
HOST(h_df_get)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(3), 2)) { a[0].i32 = -1; return nullptr; }
    const auto p = m.field(UARG(0));
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_DF_Get, { U(p), a[1], a[2], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(3), E.s_out, 2);
    a[0].i32 = st;
    return nullptr;
}
HOST(h_df_set) { CTX; M; const auto p = m.field(UARG(0), true); a[0].i32 = m.call(F_DF_Set, { U(p), a[1], a[2], a[3] }); return nullptr; }
HOST(h_df_copy) { CTX; M; const auto s = m.field(UARG(0)); const auto d = m.field(UARG(1), true); a[0].i32 = m.call(F_DF_Copy, { U(s), U(d) }); charge(caller, g.field_words); return nullptr; }
HOST(h_df_copy_mask)
{
    CTX; M;
    const auto s = m.field(UARG(0)); const auto k = m.board(UARG(1)); const auto d = m.field(UARG(2), true);
    a[0].i32 = m.call(F_DF_CopyMask, { U(s), U(k), U(d) });
    charge(caller, g.field_words + g.board_words);
    return nullptr;
}
HOST(h_df_compute)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(5), 2)) { a[0].i32 = -1; return nullptr; }
    const auto w = m.board(UARG(0)); const auto r = m.board(UARG(1)); const auto v = m.board(UARG(2), true);
    const auto d = m.field(UARG(3), true);
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_DF_Compute, { U(w), U(r), U(v), U(d), a[4], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(5), E.s_out, 2);
    { std::uint16_t n = 0; std::memcpy(&n, E.mem() + E.s_out, 2); charge(caller, g.field_words + g.chunks * n); TRACE("arg df_compute max %d count %u status %d\n", ARG(4), n, st); }
    a[0].i32 = st;
    return nullptr;
}
HOST(h_df_compute_to)
{
    CTX; M;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(6), 2)) { a[0].i32 = -1; return nullptr; }
    const auto w = m.board(UARG(0)); const auto r = m.board(UARG(1)); const auto t = m.board(UARG(2));
    const auto v = m.board(UARG(3), true); const auto d = m.field(UARG(4), true);
    std::memset(E.mem() + E.s_out, 0, 4);
    const auto st = m.call(F_DF_ComputeTo, { U(w), U(r), U(t), U(v), U(d), a[5], U(E.s_out) });
    if (st == 0) out_bytes(g, gm, UARG(6), E.s_out, 2);
    { std::uint16_t n = 0; std::memcpy(&n, E.mem() + E.s_out, 2); charge(caller, g.field_words + g.chunks * n); }
    a[0].i32 = st;
    return nullptr;
}
#undef M

// ---------------------------------------------------------------- WASI for bots
HOST(bw_fd_write)
{
    CTX;
    const Guest gm = guest(caller, b);
    const std::int32_t fd = ARG(0);
    const std::uint32_t iovs = UARG(1), count = UARG(2), nwritten = UARG(3);
    if (!gm.ok(iovs, static_cast<std::size_t>(count) * 8) || !gm.ok(nwritten, 4)) { a[0].i32 = 21; return nullptr; }  // EFAULT
    if (fd != 1 && fd != 2) { a[0].i32 = 8; return nullptr; }
    // Like wasmtime-wasi (the platform runner), write only the first non-empty buffer and report its length;
    // libc loops for the rest, and that loop is guest fuel the bot pays on the platform too.
    std::uint32_t total = 0;
    auto& log = g.log[b->team];
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t p, len;
        std::memcpy(&p, gm.base + iovs + i * 8, 4);
        std::memcpy(&len, gm.base + iovs + i * 8 + 4, 4);
        if (!gm.ok(p, len)) { a[0].i32 = 21; return nullptr; }
        if (len == 0) continue;
        b->out_bytes += len;
        if (log.size() < g.opt->log_limit)
            log.append(reinterpret_cast<const char*>(gm.base + p), std::min<std::size_t>(len, g.opt->log_limit - log.size()));
        total = len;
        break;
    }
    std::memcpy(gm.base + nwritten, &total, 4);  // output is free (checked against CLI fuel streams)
    a[0].i32 = 0;
    return nullptr;
}
HOST(bw_fdstat)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (ARG(0) > 2 || ARG(0) < 0) { a[0].i32 = 8; return nullptr; }
    if (!gm.ok(UARG(1), 24)) { a[0].i32 = 21; return nullptr; }
    // fdstat layout: filetype u8 @0, flags u16 @2, rights_base u64 @8, rights_inheriting u64 @16.
    static const std::uint16_t flags = static_cast<std::uint16_t>(std::strtoul(std::getenv("FASTRUN_FD_FLAGS") ? std::getenv("FASTRUN_FD_FLAGS") : "0", nullptr, 0));
    static const std::uint64_t rights = std::strtoull(std::getenv("FASTRUN_FD_RIGHTS") ? std::getenv("FASTRUN_FD_RIGHTS") : "0", nullptr, 0);
    static const int error = std::getenv("FASTRUN_FD_ERROR") ? std::atoi(std::getenv("FASTRUN_FD_ERROR")) : 0;
    if (error) { a[0].i32 = error; return nullptr; }
    std::memset(gm.base + UARG(1), 0, 24);
    gm.base[UARG(1)] = static_cast<std::uint8_t>(g.opt->stdout_type);
    std::memcpy(gm.base + UARG(1) + 2, &flags, 2);
    std::memcpy(gm.base + UARG(1) + 8, &rights, 8);
    std::memcpy(gm.base + UARG(1) + 16, &rights, 8);
    a[0].i32 = 0;
    return nullptr;
}
HOST(bw_environ_sizes_get)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(0), 4) || !gm.ok(UARG(1), 4)) { a[0].i32 = 21; return nullptr; }
    std::memset(gm.base + UARG(0), 0, 4);
    std::memset(gm.base + UARG(1), 0, 4);
    a[0].i32 = 0;
    return nullptr;
}
HOST(bw_zero) { a[0].i32 = 0; return nullptr; }
HOST(bw_espipe) { a[0].i32 = 70; return nullptr; }
HOST(bw_badf) { a[0].i32 = 8; return nullptr; }
HOST(bw_proc_exit) { return wasmtime_trap_new("bot called proc_exit", 20); }
HOST(bw_fd_read)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(3), 4)) { a[0].i32 = 21; return nullptr; }
    std::memset(gm.base + UARG(3), 0, 4);
    a[0].i32 = 0;
    return nullptr;
}
HOST(bw_args_sizes_get) { CTX; const Guest gm = guest(caller, b); if (!gm.ok(UARG(0), 4) || !gm.ok(UARG(1), 4)) { a[0].i32 = 21; return nullptr; } std::memset(gm.base + UARG(0), 0, 4); std::memset(gm.base + UARG(1), 0, 4); a[0].i32 = 0; return nullptr; }
HOST(bw_clock_time_get) { CTX; const Guest gm = guest(caller, b); if (!gm.ok(UARG(2), 8)) { a[0].i32 = 21; return nullptr; } std::memset(gm.base + UARG(2), 0, 8); a[0].i32 = 0; return nullptr; }
HOST(bw_random_get)
{
    CTX;
    const Guest gm = guest(caller, b);
    if (!gm.ok(UARG(0), UARG(1))) { a[0].i32 = 21; return nullptr; }
    std::uint32_t state = 0x2026a + static_cast<std::uint32_t>(b->champion + 2) * 7919u;
    for (std::uint32_t i = 0; i < UARG(1); ++i) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; gm.base[UARG(0) + i] = static_cast<std::uint8_t>(state); }
    a[0].i32 = 0;
    return nullptr;
}

// ---------------------------------------------------------------- yield (async)
bool yield_ready(void* env) { return static_cast<Bot*>(env)->resume; }
void h_yield(void*, wasmtime_caller_t* caller, const wasmtime_val_t*, std::size_t, wasmtime_val_t*, std::size_t, wasm_trap_t**,
             wasmtime_async_continuation_t* continuation)
{
    Bot* b = bot_of(caller);
    std::uint64_t fuel = 0;
    wasmtime_context_get_fuel(wasmtime_caller_context(caller), &fuel);
    b->fuel_at_yield = fuel;
    b->yielded = true;
    b->resume = false;
    continuation->callback = yield_ready;
    continuation->env = b;
    continuation->finalizer = nullptr;
}

struct ImportEntry
{
    const char* name;
    wasmtime_func_unchecked_callback_t fn;
};
extern const ImportEntry kImports[];
extern const std::size_t kImportCount;
wasm_trap_t* counting(void* env, wasmtime_caller_t* caller, Raw* a, std::size_t n)
{
    const auto slot = reinterpret_cast<std::uintptr_t>(env);
    Bot* b = bot_of(caller);
    if (b->calls.size() <= slot) b->calls.resize(kImportCount + 16);
    ++b->calls[slot];
    b->current_slot = slot;
    return kImports[slot].fn(nullptr, caller, a, n);
}
extern const ImportEntry kImports[] = {
    { "game_settings", h_game_settings }, { "item_info", h_item_info }, { "consumable_info", h_consumable_info },
    { "champion_design", h_champion_design }, { "camp_design", h_camp_design },
    { "my_champion", h_my_champion }, { "my_team", h_my_team }, { "fuel_remaining", h_fuel_remaining },
    { "bc_get_last_error", h_last_error }, { "debug_dot", h_noop }, { "debug_line", h_noop },
    { "champion", h_champion }, { "champion_identity", h_champion_identity }, { "champion_is_alive", h_champion_is_alive },
    { "champion_gold", h_champion_gold }, { "champion_gems", h_champion_gems }, { "champion_items", h_champion_items },
    { "champion_consumables", h_champion_consumables }, { "champion_cooldown", h_champion_cooldown },
    { "champion_ability_props", h_champion_ability_props }, { "champion_ability_damage", h_champion_ability_damage },
    { "champion_ability_damage_against", h_champion_ability_damage_against }, { "champion_ability_heal", h_champion_ability_heal },
    { "champion_ability_heal_against", h_champion_ability_heal_against }, { "champion_ability_shield", h_champion_ability_shield },
    { "champion_ability_shield_against", h_champion_ability_shield_against }, { "champion_buff_data", h_champion_buff_data },
    { "champion_restrictions", h_champion_restrictions }, { "champion_wards", h_champion_wards },
    { "bc_champion_ward_positions", h_champion_ward_positions }, { "bc_champion_channel_time", h_champion_channel_time },
    { "champion_can_shop", h_champion_can_shop }, { "champion_can_sell", h_champion_can_sell },
    { "bc_champion_priority", h_champion_priority }, { "num_allies", h_num_allies }, { "num_enemies", h_num_enemies },
    { "allies", h_allies }, { "enemies", h_enemies }, { "bc_champion_vision", h_champion_vision },
    { "board_ally_occupancy", h_ally_occupancy }, { "board_enemy_occupancy", h_enemy_occupancy },
    { "board_camp_occupancy", h_camp_occupancy }, { "board_awake_camp_occupancy", h_awake_camp_occupancy },
    { "board_asleep_camp_occupancy", h_asleep_camp_occupancy }, { "board_ally_ward_occupancy", h_ally_ward_occupancy },
    { "board_enemy_ward_occupancy", h_enemy_ward_occupancy }, { "board_teleport_occupancy", h_teleport_occupancy },
    { "board_ally_altar_lock_occupancy", h_ally_altar_lock_occupancy }, { "board_enemy_altar_lock_occupancy", h_enemy_altar_lock_occupancy },
    { "board_vision", h_vision }, { "board_vision_mask", h_hypothetical_vision }, { "board_ward_vision_mask", h_ward_vision },
    { "is_visible", h_is_visible }, { "has_camp", h_has_camp }, { "has_champion", h_has_champion }, { "camp_at", h_camp_at },
    { "champion_at", h_champion_at }, { "is_occupied_by_teleport", h_is_occupied_by_teleport }, { "teleports", h_teleports },
    { "teleport_at", h_teleport_at }, { "ward_at", h_ward_at }, { "ally_ward_at", h_ally_ward_at }, { "enemy_ward_at", h_enemy_ward_at },
    { "ward", h_ward }, { "wards", h_wards }, { "altar_lock", h_altar_lock }, { "altar_locks", h_altar_locks },
    { "bc_metadata", h_metadata }, { "bc_map_mask", h_map_mask }, { "bc_map_symmetry", h_map_symmetry },
    { "spawn_region", h_spawn_region }, { "bc_round_number", h_round_number }, { "bc_submitted_gems", h_submitted_gems },
    { "map_num_camp_groups", h_map_num_camp_groups }, { "map_camp_groups", h_map_camp_groups },
    { "map_camp_group_num_camps", h_map_camp_group_num_camps }, { "map_camp_group_camps", h_map_camp_group_camps },
    { "map_camp_group", h_map_camp_group }, { "map_camp_info", h_map_camp_info }, { "map_camp_layout", h_map_camp_layout },
    { "map_camp_group_of", h_map_camp_group_of }, { "map_camp_at", h_map_camp_at },
    { "bc_camp_group_max_respawn", h_camp_group_max_respawn }, { "camp_group_state", h_camp_group_state },
    { "camp_group_cooldown", h_camp_group_cooldown }, { "camp", h_camp }, { "camp_state", h_camp_state }, { "camp_buffs", h_camp_buffs },
    { "shared_memory_size", h_shared_memory_size }, { "read_shared_memory", h_read_shared_memory },
    { "write_shared_memory", h_write_shared_memory }, { "draft_memory_size", h_draft_memory_size },
    { "read_draft_memory", h_read_draft_memory }, { "bc_submit_draft_memory", h_submit_draft_memory },
    { "draft_num_rounds", h_draft_num_rounds }, { "draft_get", h_draft_get }, { "draft_champion", h_draft_champion },
    { "attack", h_attack }, { "cast_with_result", h_cast }, { "use_consumable_with_result", h_use_consumable },
    { "place_ward_with_result", h_place_ward }, { "move_to_with_result", h_move_to }, { "submit_gems", h_submit_gems },
    { "buy_consumable", h_buy_consumable }, { "buy_item", h_buy_item }, { "sell", h_sell }, { "recall", h_recall },
    { "cancel_channel", h_cancel_channel }, { "set_priority", h_set_priority }, { "resign", h_resign },
    { "bc_Bitboard_Zero", h_bb_zero }, { "bc_Bitboard_One", h_bb_one }, { "bc_Bitboard_ImportRawBuffer", h_bb_import_raw },
    { "bc_Bitboard_ImportLinearBuffer", h_bb_import_linear }, { "bc_Bitboard_ExportRawBuffer", h_bb_export_raw },
    { "bc_Bitboard_ExportLinearBuffer", h_bb_export_linear }, { "bc_Bitboard_Print", h_noop }, { "bc_Bitboard_Test", h_bb_test },
    { "bc_Bitboard_Set", h_bb_set }, { "bc_Bitboard_Toggle", h_bb_toggle }, { "bc_Bitboard_IsZero", h_bb_is_zero },
    { "bc_Bitboard_NumSetBits", h_bb_num_set_bits }, { "bc_Bitboard_GetSetBits", h_bb_get_set_bits }, { "bc_Bitboard_Not", h_bb_not },
    { "bc_Bitboard_Union", h_bb_union }, { "bc_Bitboard_Intersect", h_bb_intersect }, { "bc_Bitboard_Xor", h_bb_xor },
    { "bc_Bitboard_IsEqual", h_bb_is_equal }, { "bc_Bitboard_ShiftRight", h_bb_shift_right }, { "bc_Bitboard_ShiftUp", h_bb_shift_up },
    { "bc_Bitboard_Reflect", h_bb_reflect }, { "bc_Bitboard_DrawCircle", h_bb_draw_circle },
    { "bc_Bitboard_DrawChebyshev", h_bb_draw_chebyshev }, { "bc_Bitboard_ReflectHorizontalPosition", h_bb_reflect_h_pos },
    { "bc_Bitboard_ReflectVerticalPosition", h_bb_reflect_v_pos }, { "bc_Bitboard_Rotate180Position", h_bb_rotate_pos },
    { "bc_Bitboard_ReachableMask", h_bb_reachable }, { "bc_Bitboard_ShadowCast", h_bb_shadow_cast },
    { "bc_Bitboard_ReachableMaskWithScratch", h_bb_reachable_scratch },
    { "bc_DistanceField_ImportRawBuffer", h_df_import_raw }, { "bc_DistanceField_ImportLinearBuffer", h_df_import_linear },
    { "bc_DistanceField_ExportRawBuffer", h_df_export_raw }, { "bc_DistanceField_ExportLinearBuffer", h_df_export_linear },
    { "bc_DistanceField_Print", h_noop }, { "bc_DistanceField_Get", h_df_get }, { "bc_DistanceField_Set", h_df_set },
    { "bc_DistanceField_Copy", h_df_copy }, { "bc_DistanceField_CopyWithMask", h_df_copy_mask },
    { "bc_DistanceField_ComputeWithExpansionCount", h_df_compute },
    { "bc_DistanceField_ComputeToWithExpansionCount", h_df_compute_to },
};
extern const std::size_t kImportCount;
const ImportEntry kWasi[] = {
    { "fd_write", bw_fd_write }, { "fd_fdstat_get", bw_fdstat }, { "environ_sizes_get", bw_environ_sizes_get },
    { "environ_get", bw_zero }, { "fd_close", bw_zero }, { "fd_seek", bw_espipe }, { "proc_exit", bw_proc_exit },
    { "fd_prestat_get", bw_badf }, { "fd_prestat_dir_name", bw_badf }, { "fd_read", bw_fd_read }, { "args_get", bw_zero },
    { "args_sizes_get", bw_args_sizes_get }, { "clock_time_get", bw_clock_time_get }, { "random_get", bw_random_get },
    { "sched_yield", bw_zero },
};
extern const std::size_t kImportCount = sizeof(kImports) / sizeof(kImports[0]);

// ------------------------------------------------------------------------------------------------------------------
// Shared runtime: engines, compiled modules, linkers (built once per process, used by every worker)
// ------------------------------------------------------------------------------------------------------------------
struct Shared
{
    wasm_engine_t* engine_engine{};
    wasm_engine_t* bot_engine{};
    wasmtime_module_t* engine_module{};
    wasmtime_linker_t* engine_linker{};
    std::vector<Program> programs;  // filled before any game starts (games keep pointers into it)
};

wasmtime_module_t* load_module(wasm_engine_t* engine, const std::string& path, const std::string& cache, const char* tag)
{
    const auto bytes = read_file(path);
    const auto key = fnv1a(bytes, fnv1a(std::vector<std::uint8_t>(tag, tag + std::strlen(tag))));
    char name[64];
    std::snprintf(name, sizeof name, "%016llx-%s.cwasm", static_cast<unsigned long long>(key), tag);
    const fs::path cached = fs::path(cache) / name;
    wasmtime_module_t* module = nullptr;
    if (!cache.empty() && fs::exists(cached))
    {
        const auto data = read_file(cached.string());
        if (auto* error = wasmtime_module_deserialize(engine, data.data(), data.size(), &module)) { take_error(error); module = nullptr; }
        else return module;
    }
    ok(wasmtime_module_new(engine, bytes.data(), bytes.size(), &module), ("compile " + path).c_str());
    if (!cache.empty())
    {
        wasm_byte_vec_t out;
        if (!wasmtime_module_serialize(module, &out))
        {
            fs::create_directories(cache);
            write_file(cached, out.data, out.size);
            wasm_byte_vec_delete(&out);
        }
    }
    return module;
}

std::string name_of(const wasm_name_t* n) { return std::string(n->data, n->size); }

void link_bot(Shared& sh, Program& p)
{
    p.linker = wasmtime_linker_new(sh.bot_engine);
    wasm_importtype_vec_t imports;
    wasmtime_module_imports(p.module, &imports);
    for (std::size_t i = 0; i < imports.size; ++i)
    {
        const auto* it = imports.data[i];
        const std::string module = name_of(wasm_importtype_module(it)), name = name_of(wasm_importtype_name(it));
        const auto* type = wasm_externtype_as_functype_const(wasm_importtype_type(it));
        if (!type) die("non-function import " + module + "." + name);
        if (module == "env" && name == "yield")
        {
            ok(wasmtime_linker_define_async_func(p.linker, module.data(), module.size(), name.data(), name.size(), type, h_yield, nullptr, nullptr),
               "define yield");
            continue;
        }
        const ImportEntry* found = nullptr;
        std::size_t slot = 0;
        if (module == "env") { for (std::size_t k = 0; k < kImportCount; ++k) if (name == kImports[k].name) { found = &kImports[k]; slot = k; break; } }
        else if (module == "wasi_snapshot_preview1") { for (const auto& e : kWasi) if (name == e.name) { found = &e; break; } }
        if (!found) die("unsupported bot import " + module + "." + name + " in " + p.path);
        const bool count = g_counting && module == "env";
        ok(wasmtime_linker_define_func_unchecked(p.linker, module.data(), module.size(), name.data(), name.size(), type,
                                                 count ? counting : found->fn, count ? reinterpret_cast<void*>(slot) : nullptr, nullptr),
           ("define " + name).c_str());
    }
    wasm_importtype_vec_delete(&imports);
    ok(wasmtime_linker_instantiate_pre(p.linker, p.module, &p.pre), "instantiate_pre");
}

void link_engine(Shared& sh)
{
    sh.engine_linker = wasmtime_linker_new(sh.engine_engine);
    struct E { const char* module; const char* name; wasmtime_func_unchecked_callback_t fn; };
    const E table[] = {
        { "wasi_snapshot_preview1", "environ_get", ew_zero }, { "wasi_snapshot_preview1", "environ_sizes_get", ew_environ_sizes_get },
        { "wasi_snapshot_preview1", "fd_close", ew_zero }, { "wasi_snapshot_preview1", "fd_fdstat_get", ew_fdstat },
        { "wasi_snapshot_preview1", "fd_prestat_get", ew_badf }, { "wasi_snapshot_preview1", "fd_prestat_dir_name", ew_badf },
        { "wasi_snapshot_preview1", "fd_read", ew_fd_read }, { "wasi_snapshot_preview1", "fd_seek", ew_espipe },
        { "wasi_snapshot_preview1", "fd_write", ew_fd_write }, { "wasi_snapshot_preview1", "proc_exit", ew_proc_exit },
        { "wasi_snapshot_preview1", "random_get", ew_random_get }, { "deep_wasm", "on_assert_fail_impl", ew_assert },
    };
    wasm_importtype_vec_t imports;
    wasmtime_module_imports(sh.engine_module, &imports);
    for (std::size_t i = 0; i < imports.size; ++i)
    {
        const auto* it = imports.data[i];
        const std::string module = name_of(wasm_importtype_module(it)), name = name_of(wasm_importtype_name(it));
        const auto* type = wasm_externtype_as_functype_const(wasm_importtype_type(it));
        const E* found = nullptr;
        for (const auto& e : table) if (module == e.module && name == e.name) { found = &e; break; }
        if (!found || !type) die("unsupported engine import " + module + "." + name);
        ok(wasmtime_linker_define_func_unchecked(sh.engine_linker, module.data(), module.size(), name.data(), name.size(), type, found->fn, nullptr, nullptr),
           ("define engine " + name).c_str());
    }
    wasm_importtype_vec_delete(&imports);
}

std::unique_ptr<EngineRt> new_engine(Shared& sh)
{
    auto E = std::make_unique<EngineRt>();
    E->store = wasmtime_store_new(sh.engine_engine, E.get(), nullptr);
    E->ctx = wasmtime_store_context(E->store);
    wasm_trap_t* trap = nullptr;
    ok(wasmtime_linker_instantiate(sh.engine_linker, E->ctx, sh.engine_module, &E->instance, &trap), "instantiate engine");
    if (trap) die("engine instantiation trapped: " + take_trap(trap));
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(E->ctx, &E->instance, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY) die("engine memory export");
    E->memory = item.of.memory;
    for (int f = 0; f < F_COUNT; ++f)
    {
        if (!wasmtime_instance_export_get(E->ctx, &E->instance, FN_NAMES[f], std::strlen(FN_NAMES[f]), &item) || item.kind != WASMTIME_EXTERN_FUNC)
            die(std::string("engine export missing: ") + FN_NAMES[f]);
        E->fn[f] = item.of.func;
        wasm_functype_t* type = wasmtime_func_type(E->ctx, &item.of.func);
        E->nparams[f] = wasm_functype_params(type)->size;
        wasm_functype_delete(type);
    }
    E->call(F_Init0, {});
    const auto base = E->malloc(4096 + 64 * 4);
    E->s_out = base;
    E->s_persp = base + 4096;
    E->s_ent = base + 4096 + 16;
    E->s_pos = base + 4096 + 32;
    E->s_meta = base + 4096 + 64;
    return E;
}

// ------------------------------------------------------------------------------------------------------------------
// Running a game
// ------------------------------------------------------------------------------------------------------------------
std::unique_ptr<Bot> new_bot(Shared& sh, Game& g, int team, int champion)
{
    TRACE("new bot team %d champion %d\n", team, champion);
    auto b = std::make_unique<Bot>();
    b->game = &g;
    b->team = team;
    b->champion = champion;
    Program& p = *g.program[team];
    b->store = wasmtime_store_new(sh.bot_engine, b.get(), nullptr);
    b->ctx = wasmtime_store_context(b->store);
    ok(wasmtime_context_set_fuel(b->ctx, kReserve), "set fuel");
    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* error = nullptr;
    auto* future = wasmtime_instance_pre_instantiate_async(p.pre, b->ctx, &b->instance, &trap, &error);
    TRACE("  instantiate future %p\n", static_cast<void*>(future));
    while (!wasmtime_call_future_poll(future)) {}
    TRACE("  instantiated\n");
    wasmtime_call_future_delete(future);
    if (error) die("bot instantiation: " + take_error(error));
    if (trap) die("bot instantiation trapped: " + take_trap(trap));
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(b->ctx, &b->instance, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY) die("bot memory export");
    b->memory = item.of.memory;
    if (!wasmtime_instance_export_get(b->ctx, &b->instance, "_start", 6, &item) || item.kind != WASMTIME_EXTERN_FUNC) die("bot _start export");
    b->start = item.of.func;
    if (const Weights* w = g.weights[team]; w && !w->empty())
    {
        if (p.params_at < 0) die("weights given for a bot without a learned-weights block: " + p.path);
        if (w->size() > p.params_count)
            die("weights for " + p.path + ": " + std::to_string(w->size()) + " values, block holds " + std::to_string(p.params_count));
        // The vector replaces the whole block; a shorter one (an older layout: blocks are appended) is zero-padded.
        auto* dst = wasmtime_memory_data(b->ctx, &b->memory) + p.params_at + 32;
        std::memset(dst, 0, p.params_count * sizeof(float));
        std::memcpy(dst, w->data(), w->size() * sizeof(float));
    }
    return b;
}

// Locate the learned-weights block (bots/main/main/src/learn.h: 16-byte magic, u32 count, u32
// version, 8 reserved bytes, then the floats) in a fresh instance's memory. Exactly one copy or none.
void find_params(Shared& sh, Program& p)
{
    wasmtime_store_t* store = wasmtime_store_new(sh.bot_engine, nullptr, nullptr);
    wasmtime_context_t* ctx = wasmtime_store_context(store);
    ok(wasmtime_context_set_fuel(ctx, kReserve), "set fuel");
    wasmtime_instance_t instance;
    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* error = nullptr;
    auto* future = wasmtime_instance_pre_instantiate_async(p.pre, ctx, &instance, &trap, &error);
    while (!wasmtime_call_future_poll(future)) {}
    wasmtime_call_future_delete(future);
    if (error) die("bot instantiation: " + take_error(error));
    if (trap) die("bot instantiation trapped: " + take_trap(trap));
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(ctx, &instance, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY) die("bot memory export");
    const std::uint8_t* data = wasmtime_memory_data(ctx, &item.of.memory);
    const std::size_t size = wasmtime_memory_data_size(ctx, &item.of.memory);
    static const char magic[16] = { 'm', 'b', '.', 'l', 'e', 'a', 'r', 'n', '.', 'p', 'a', 'r', 'a', 'm', 's', '\0' };
    const auto* end = data + size;
    const auto* at = std::search(data, end, magic, magic + 16);
    if (at != end)
    {
        if (std::search(at + 16, end, magic, magic + 16) != end) die("two learned-weights blocks in " + p.path);
        if (at + 32 > end) die("truncated learned-weights block in " + p.path);
        p.params_at = at - data;
        std::memcpy(&p.params_count, at + 16, 4);
        if (at + 32 + 4 * static_cast<std::size_t>(p.params_count) > end) die("truncated learned-weights block in " + p.path);
    }
    wasmtime_store_delete(store);
}

Weights read_weights(const std::string& path)
{
    const auto bytes = read_file(path);
    const std::string text(bytes.begin(), bytes.end());
    Weights w;
    const char* s = text.c_str();
    char* e = nullptr;
    for (;;)
    {
        const float v = std::strtof(s, &e);
        if (e == s) break;
        if (!std::isfinite(v)) die("non-finite weight in " + path);
        w.push_back(v);
        s = e;
    }
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') ++s;
    if (*s) die("unreadable weight in " + path + " near: " + std::string(s, std::min<std::size_t>(20, std::strlen(s))));
    if (w.empty()) die("no weights in " + path);
    return w;
}

// One owner turn: 0 yielded, 1 fuel exhausted (suspended), 2 fault. `used` is the fuel the turn consumed.
int run_turn(Game& g, Bot& b, std::uint64_t budget, std::uint64_t& used)
{
    if (b.dead) { used = 0; return 2; }
    TRACE("turn team %d champion %d budget %llu turns %d\n", b.team, b.champion, static_cast<unsigned long long>(budget), b.turns);
    ok(wasmtime_context_fuel_async_yield_interval(b.ctx, budget), "yield interval");
    ok(wasmtime_context_set_fuel(b.ctx, budget + kReserve), "set fuel");
    b.yielded = false;
    b.resume = true;
    ++b.turns;
    if (!b.future) b.future = wasmtime_func_call_async(b.ctx, &b.start, nullptr, 0, nullptr, 0, &b.trap, &b.error);
    TRACE("  poll\n");
    const bool done = wasmtime_call_future_poll(b.future);
    TRACE("  poll returned done=%d yielded=%d\n", done ? 1 : 0, b.yielded ? 1 : 0);
    if (done)
    {
        b.dead = true;
        std::string why = b.trap ? take_trap(b.trap) : b.error ? take_error(b.error) : "bot returned from _start";
        b.trap = nullptr;
        b.error = nullptr;
        wasmtime_call_future_delete(b.future);
        b.future = nullptr;
        if (g.fault_text.empty()) g.fault_text = why;
        used = budget;
        return 2;
    }
    if (b.yielded)
    {
        const auto left = static_cast<std::int64_t>(b.fuel_at_yield) - static_cast<std::int64_t>(kReserve);
        used = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(budget) - left));
        return 0;
    }
    used = budget;
    return 1;
}

// Team totals from the engine's statistics (the same records as the CLI statistics JSON).
struct TeamStats
{
    std::uint64_t kills{}, deaths{}, earned{}, collected{}, spent{};
    double monster_damage{}, champion_damage{};
};

bool team_stats(EngineRt& E, std::int64_t game, TeamStats out[2])
{
    out[0] = out[1] = TeamStats{};
    const auto buf = E.malloc(1024);
    bool ok = E.call(F_StatsSummary, { I64(game), U(buf) }) == 0;
    std::uint32_t count = 0;
    if (ok) std::memcpy(&count, E.mem() + buf + 12, 4);
    for (std::uint32_t id = 0; ok && id < count; ++id)
    {
        if (E.call(F_StatsChampion, { I64(game), I32(static_cast<std::int32_t>(id)), U(buf) })) { ok = false; break; }
        const std::uint8_t* m = E.mem() + buf;
        std::uint32_t team, kills, deaths;
        std::uint64_t earned, collected, spent;
        double monster, champion;
        std::memcpy(&team, m + 4, 4);
        std::memcpy(&kills, m + 12, 4);
        std::memcpy(&deaths, m + 16, 4);
        std::memcpy(&earned, m + 24, 8);
        std::memcpy(&collected, m + 32, 8);
        std::memcpy(&spent, m + 48, 8);
        std::memcpy(&monster, m + 616, 8);
        std::memcpy(&champion, m + 632, 8);
        if (team > 1) continue;
        auto& t = out[team];
        t.kills += kills;
        t.deaths += deaths;
        t.earned += earned;
        t.collected += collected;
        t.spent += spent;
        t.monster_damage += monster;
        t.champion_damage += champion;
    }
    E.free(buf);
    return ok;
}

struct Result
{
    std::string map, tag, a_name, b_name;
    int a_team{};
    int rounds{}, outcome{};
    std::uint64_t gems[2]{};
    TeamStats stats[2];  // end of game (the engine finalises statistics only then)
    bool have_stats{};
    std::uint64_t fuel_max[2]{}, fuel_total[2]{};
    int exhausted[2]{}, faults[2]{}, errors[2]{}, turns[2]{};
    double ms{};
    std::string error;
    bool truncated{};
};

int count_error_lines(const std::string& log)
{
    // Match arena.games.count_errors: normal fields such as abs_health_error
    // and plural "errors" are not exceptions. Preserve the historical
    // UNEXPECTED_ENGINE_ERROR marker, which has no leading word boundary.
    static const std::regex error_line(R"(\berror\b|engine_error\b)", std::regex::icase);
    int n = 0;
    std::size_t start = 0;
    while (start < log.size())
    {
        auto end = log.find_first_of("\n\r\v\f", start);
        if (end == std::string::npos) end = log.size();
        std::string line = log.substr(start, end - start);
        line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
        if (std::regex_search(line, error_line)) ++n;
        start = end + 1;
    }
    return n;
}

void write_fuel_stream(const fs::path& path, const std::vector<std::uint64_t>& used)
{
    std::vector<std::uint8_t> out = { 'a', '6', '2', 'l', 'e', 'u', 'f', 0, 0, 0, 1, 0, 0 };
    const std::uint64_t budgets[8] = { 1000000, 100000, 200000, 100000, 1000000, 100000, 200000, 100000 };
    auto put = [&](std::uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i))); };
    for (auto v : budgets) put(v);
    for (auto v : used) put(v);
    write_file(path, out.data(), out.size());
}

Result play(Shared& sh, EngineRt& E, const Options& opt, const std::string& map_path, int a_team, const std::string& bundle_dir,
            Program* prog_a, Program* prog_b, const Weights* weights_a, const Weights* weights_b, const std::string& tag)
{
    const auto t0 = std::chrono::steady_clock::now();
    Result r;
    r.map = fs::path(map_path).stem().string();
    r.tag = tag;
    r.a_team = a_team;
    Game g;
    g.E = &E;
    g.opt = &opt;
    if (g_counting) g.calls_out = std::fopen((opt.calls_log + "." + fs::path(map_path).stem().string() + (a_team == 0 ? "_A" : "_B") + ".jsonl").c_str(), "wb");
    g.program[a_team] = prog_a;
    g.program[1 - a_team] = prog_b;
    r.a_name = prog_a->name;
    r.b_name = prog_b->name;
    g.weights[a_team] = weights_a;
    g.weights[1 - a_team] = weights_b;
    // Game from the map file.
    const auto bytes = read_file(map_path);
    const auto mp = E.malloc(static_cast<std::uint32_t>(bytes.size()));
    std::memcpy(E.mem() + mp, bytes.data(), bytes.size());
    if (E.call(F_FromMap, { U(mp), U(static_cast<std::uint32_t>(bytes.size())), U(E.s_out), I32(0) })) die("Game_FromMapFileBytes: " + E.last_error());
    std::memcpy(&g.handle, E.mem() + E.s_out, 8);
    E.free(mp);
    E.call(F_Checkpoints, { I64(g.handle), I32(0) });
    if (E.call(F_RunnerInit, { I64(g.handle) })) die("Game_Runner_Init: " + E.last_error());
    if (E.call(F_MapMeta, { I64(g.handle), U(E.s_meta) })) die("bitboard metadata: " + E.last_error());
    std::uint32_t meta[5];
    std::memcpy(meta, E.mem() + E.s_meta, 20);
    g.width = static_cast<std::int32_t>(meta[0]);
    g.height = static_cast<std::int32_t>(meta[1]);
    g.chunked_w = meta[2];
    g.chunked_h = meta[3];
    g.stride = meta[4];
    E.call(F_BoardSize, { U(E.s_meta), U(E.s_out) });
    std::memcpy(&g.board_bytes, E.mem() + E.s_out, 4);
    E.call(F_FieldSize, { U(E.s_meta), U(E.s_out) });
    std::memcpy(&g.field_bytes, E.mem() + E.s_out, 4);
    g.chunks = static_cast<std::uint64_t>(g.chunked_w) * g.chunked_h;
    g.field_words = g.field_bytes / 8;
    g.board_words = g.board_bytes / 8;
    for (auto& p : g.bb) p = E.malloc(g.board_bytes);
    for (auto& p : g.df) p = E.malloc(g.field_bytes);
    g.buf_cap = 65536;
    g.buf = E.malloc(g.buf_cap);

    for (;;)
    {
        E.call(F_Advance, { I64(g.handle) });
        E.call(F_GetState, { I64(g.handle), U(E.s_out) });
        std::uint32_t st[4];
        std::memcpy(st, E.mem() + E.s_out, 16);
        const auto phase = st[0], team = st[1], round = st[2];
        const auto champion = static_cast<std::int32_t>(st[3]);
        if (phase == 2) break;
        if (phase == 1 && opt.rounds && static_cast<int>(round) > opt.rounds) { r.truncated = true; break; }
        if (team > 1) { r.error = "runner state without a team"; break; }
        Bot* bot;
        bool draft = phase == 0;
        if (draft)
        {
            if (!g.draft[team]) g.draft[team] = new_bot(sh, g, static_cast<int>(team), -1);
            bot = g.draft[team].get();
        }
        else
        {
            if (champion < 0) { r.error = "playing turn without a champion"; break; }
            if (static_cast<std::size_t>(champion) >= g.champions.size()) g.champions.resize(static_cast<std::size_t>(champion) + 1);
            auto& slot = g.champions[static_cast<std::size_t>(champion)];
            if (!slot) slot = new_bot(sh, g, static_cast<int>(team), champion);
            bot = slot.get();
        }
        const std::uint64_t budget = bot->turns == 0 ? (draft ? 1000000 : 200000) : 100000;
        std::uint64_t used = 0;
        if (g_counting)
        {
            std::fill(bot->calls.begin(), bot->calls.end(), 0u);
            std::fill(bot->charged.begin(), bot->charged.end(), 0ull);
            bot->out_bytes = 0;
        }
        const int reason = run_turn(g, *bot, budget, used);
        if (g_counting && g.calls_out)
        {
            std::string line = "{\"team\":" + std::to_string(team) + ",\"champion\":" + std::to_string(bot->champion) + ",\"turn\":" +
                               std::to_string(g.fuel[team].size()) + ",\"fuel\":" + std::to_string(used) + ",\"reason\":" + std::to_string(reason) + ",\"calls\":{";
            bool first = true;
            for (std::size_t k = 0; k < bot->calls.size() && k < kImportCount; ++k)
                if (bot->calls[k]) { line += (first ? "\"" : ",\"") + std::string(kImports[k].name) + "\":" + std::to_string(bot->calls[k]); first = false; }
            line += "},\"out\":" + std::to_string(bot->out_bytes) + ",\"charged\":{";
            first = true;
            for (std::size_t k = 0; k < bot->charged.size() && k < kImportCount; ++k)
                if (bot->charged[k]) { line += (first ? "\"" : ",\"") + std::string(kImports[k].name) + "\":" + std::to_string(bot->charged[k]); first = false; }
            line += "}}\n";
            std::fputs(line.c_str(), g.calls_out);
        }
        g.fuel[team].push_back(used);
        g.fuel_max[team] = std::max(g.fuel_max[team], used);
        g.fuel_total[team] += used;
        if (reason == 1) ++g.exhausted[team];
        if (reason == 2) ++g.faults[team];
        if (E.call(F_EndTurn, { I64(g.handle), I32(reason) }))
        {
            r.error = "end turn refused (" + std::string(draft ? "draft" : "play") + " team " + std::to_string(team) + "): " + E.last_error();
            break;
        }
    }
    // Header from the timeline snapshot: round @8, outcome @28, submitted gems u64 @32/@40.
    const auto size = static_cast<std::uint32_t>(E.call(F_TimelineSize, {}));
    const auto snap = E.malloc(size);
    E.call(F_ReadTimeline, { I64(g.handle), U(snap) });
    std::uint32_t round, outcome;
    std::memcpy(&round, E.mem() + snap + 8, 4);
    std::memcpy(&outcome, E.mem() + snap + 28, 4);
    std::memcpy(&r.gems[0], E.mem() + snap + 32, 8);
    std::memcpy(&r.gems[1], E.mem() + snap + 40, 8);
    E.free(snap);
    r.rounds = static_cast<int>(round);
    // A finished match reports rounds completed from its statistics summary, like the CLI's rows.
    if (!r.truncated && r.error.empty())
    {
        const auto summary = E.malloc(128);
        if (E.call(F_StatsSummary, { I64(g.handle), U(summary) }) == 0) std::memcpy(&r.rounds, E.mem() + summary + 8, 4);
        E.free(summary);
        // The summary counts the final round; a tribute win ends it part-way, which the CLI does not count.
        if (std::max(r.gems[0], r.gems[1]) >= 1000) --r.rounds;
    }
    r.have_stats = team_stats(E, g.handle, r.stats);
    if (std::getenv("FASTRUN_DEBUG_MEM"))
        std::fprintf(stderr, "mem %s engine %.1f MB\n", r.map.c_str(), static_cast<double>(wasmtime_memory_data_size(E.ctx, &E.memory)) / 1e6);
    r.outcome = static_cast<int>(outcome);
    for (int t = 0; t < 2; ++t)
    {
        r.fuel_max[t] = g.fuel_max[t];
        r.fuel_total[t] = g.fuel_total[t];
        r.exhausted[t] = g.exhausted[t];
        r.faults[t] = g.faults[t];
        r.errors[t] = count_error_lines(g.log[t]);
        r.turns[t] = static_cast<int>(g.fuel[t].size());
    }
    if (r.error.empty() && !g.fault_text.empty()) r.error = "bot fault: " + g.fault_text;
    if (!bundle_dir.empty())
    {
        const fs::path dir = fs::path(bundle_dir) / (r.map + (a_team == 0 ? "_A" : "_B") + (tag.empty() ? "" : "_" + tag));
        fs::create_directories(dir);
        if (!E.call(F_SaveReplay, { I64(g.handle) }) && !E.call(F_FileBuffer, { I64(g.handle), U(E.s_out), U(E.s_out + 4) }))
        {
            std::uint32_t p, n;
            std::memcpy(&p, E.mem() + E.s_out, 4);
            std::memcpy(&n, E.mem() + E.s_out + 4, 4);
            write_file(dir / "replay.repl26a", E.mem() + p, n);
        }
        write_file(dir / "team-a.stdout", g.log[0].data(), g.log[0].size());
        write_file(dir / "team-b.stdout", g.log[1].data(), g.log[1].size());
        write_fuel_stream(dir / "team-a.fuel", g.fuel[0]);
        write_fuel_stream(dir / "team-b.fuel", g.fuel[1]);
    }
    // Bots first (their futures hold fibers), then the game and its scratch.
    g.draft[0].reset();
    g.draft[1].reset();
    g.champions.clear();
    E.call(F_Destroy, { I64(g.handle) });
    for (auto p : g.bb) E.free(p);
    for (auto p : g.df) E.free(p);
    E.free(g.buf);
    if (g.calls_out) std::fclose(g.calls_out);
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

std::string row_json(const Result& r, const Options& opt)
{
    // Winner from the first bot's point of view: "A" = the bot given with --a won.
    const char* winner = r.outcome == 0 ? "draw" : ((r.outcome == 1) == (r.a_team == 0)) ? "a" : "b";
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "{\"map\":\"%s\",\"a\":\"%s\",\"b\":\"%s\",\"a_side\":\"%s\",\"winner\":\"%s\",\"rounds\":%d,\"gems\":[%llu,%llu],"
                  "\"fuel_max\":[%llu,%llu],\"fuel_total\":[%llu,%llu],\"turns\":[%d,%d],\"exhausted\":[%d,%d],\"faults\":[%d,%d],"
                  "\"errors\":[%d,%d],\"truncated\":%s,\"ms\":%.1f",
                  json_escape(r.map).c_str(), json_escape(r.a_name).c_str(), json_escape(r.b_name).c_str(), r.a_team == 0 ? "A" : "B",
                  winner, r.rounds, static_cast<unsigned long long>(r.gems[0]), static_cast<unsigned long long>(r.gems[1]),
                  static_cast<unsigned long long>(r.fuel_max[0]), static_cast<unsigned long long>(r.fuel_max[1]),
                  static_cast<unsigned long long>(r.fuel_total[0]), static_cast<unsigned long long>(r.fuel_total[1]), r.turns[0], r.turns[1],
                  r.exhausted[0], r.exhausted[1], r.faults[0], r.faults[1], r.errors[0], r.errors[1], r.truncated ? "true" : "false", r.ms);
    std::string out = buf;
    if (r.have_stats)
    {
        // Team totals in team order [A, B], like gems: kills, deaths, earned/spent gold, collected gems,
        // damage dealt to monsters and champions.
        auto pair = [](std::uint64_t a, std::uint64_t b) { return "[" + std::to_string(a) + "," + std::to_string(b) + "]"; };
        const auto& s = r.stats;
        out += ",\"kills\":" + pair(s[0].kills, s[1].kills) + ",\"deaths\":" + pair(s[0].deaths, s[1].deaths) +
               ",\"gold\":" + pair(s[0].earned, s[1].earned) + ",\"spent\":" + pair(s[0].spent, s[1].spent) +
               ",\"collected\":" + pair(s[0].collected, s[1].collected) +
               ",\"monster_damage\":" + pair(static_cast<std::uint64_t>(s[0].monster_damage), static_cast<std::uint64_t>(s[1].monster_damage)) +
               ",\"champion_damage\":" + pair(static_cast<std::uint64_t>(s[0].champion_damage), static_cast<std::uint64_t>(s[1].champion_damage));
    }
    if (!r.tag.empty()) out += ",\"tag\":\"" + json_escape(r.tag) + "\"";
    if (!r.error.empty()) out += ",\"error\":\"" + json_escape(r.error) + "\"";
    return out + "}";
}

// ------------------------------------------------------------------------------------------------------------------
// Map generation (--genmap): fresh layouts with a random pool map's global parameters (size, symmetry,
// draft count, terrain densities, spawn box, altar count, camp-group compositions; templates from
// bots/main/tools/mapgen/poolstats.py --templates), built and validated by the engine's MapBuilder.
// ------------------------------------------------------------------------------------------------------------------
struct MapTemplate
{
    std::string id;
    int w{}, h{}, sym{}, draft{}, sw{}, sh{}, altars{};
    double wall{}, bush{}, hole{};
    std::vector<std::string> groups;  // member camp types as digits
};

std::vector<MapTemplate> read_templates(const std::string& path)
{
    const auto bytes = read_file(path);
    std::istringstream lines(std::string(bytes.begin(), bytes.end()));
    std::vector<MapTemplate> out;
    std::string line;
    while (std::getline(lines, line))
    {
        std::istringstream f(line);
        MapTemplate t;
        if (!(f >> t.id >> t.w >> t.h >> t.sym >> t.draft >> t.wall >> t.bush >> t.hole >> t.sw >> t.sh >> t.altars)) continue;
        for (std::string g; f >> g;) t.groups.push_back(g);
        out.push_back(t);
    }
    if (out.empty()) die("no templates in " + path);
    return out;
}

struct GenRng
{
    std::uint64_t s;
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    int range(int lo, int hi) { return lo + below(hi - lo + 1); }  // inclusive
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
};

enum : std::uint8_t { T_FLOOR, T_WALL, T_HOLE, T_BUSH, T_ALTAR, T_CAMP };

struct Layout
{
    int w{}, h{}, sym{};
    std::vector<std::uint8_t> t, reserved;
    int sx0{}, sy0{}, sx1{}, sy1{};  // team A spawn box, x1/y1 exclusive
    std::vector<std::array<int, 3>> camps;  // type, x, y (both halves)
    std::vector<std::pair<int, int>> altars;
    std::pair<int, int> mirror(int x, int y) const
    {
        return sym == 0 ? std::pair{ w - 1 - x, y } : sym == 1 ? std::pair{ x, h - 1 - y } : std::pair{ w - 1 - x, h - 1 - y };
    }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    std::uint8_t& at(int x, int y) { return t[static_cast<std::size_t>(y * w + x)]; }
    std::uint8_t tile(int x, int y) const { return t[static_cast<std::size_t>(y * w + x)]; }
    bool canonical(int x, int y) const
    {
        const auto [mx, my] = mirror(x, y);
        return y < my || (y == my && x <= mx);
    }
    void put(int x, int y, std::uint8_t v)
    {
        at(x, y) = v;
        const auto [mx, my] = mirror(x, y);
        at(mx, my) = v;
    }
};

// Grow a blob of `kind` from a random free tile (and its mirror) by a random walk with a brush.
void blob(Layout& L, GenRng& rng, std::uint8_t kind, int size, int brush)
{
    int x = rng.below(L.w), y = rng.below(L.h);
    for (int n = 0, guard = 0; n < size && guard < size * 20; ++guard)
    {
        for (int dy = -brush; dy <= brush && n < size; ++dy)
            for (int dx = -brush; dx <= brush && n < size; ++dx)
            {
                const int px = x + dx, py = y + dy;
                if (!L.inside(px, py) || L.reserved[static_cast<std::size_t>(py * L.w + px)] || L.tile(px, py) != T_FLOOR) continue;
                const auto [mx, my] = L.mirror(px, py);
                if (L.reserved[static_cast<std::size_t>(my * L.w + mx)] || L.tile(mx, my) != T_FLOOR) continue;
                L.put(px, py, kind);
                n += (mx == px && my == py) ? 1 : 2;
            }
        x = std::clamp(x + rng.range(-1, 1) * (brush + 1), 0, L.w - 1);
        y = std::clamp(y + rng.range(-1, 1) * (brush + 1), 0, L.h - 1);
    }
}

// A straight wall segment (horizontal, vertical or diagonal) with its mirror.
void segment(Layout& L, GenRng& rng, int length)
{
    int x = rng.below(L.w), y = rng.below(L.h);
    const int dirs[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
    const auto& d = dirs[rng.below(4)];
    for (int i = 0; i < length; ++i, x += d[0], y += d[1])
    {
        if (!L.inside(x, y)) break;
        const auto [mx, my] = L.mirror(x, y);
        if (L.reserved[static_cast<std::size_t>(y * L.w + x)] || L.reserved[static_cast<std::size_t>(my * L.w + mx)]) continue;
        if (L.tile(x, y) == T_FLOOR && L.tile(mx, my) == T_FLOOR) L.put(x, y, T_WALL);
    }
}

int count_of(const Layout& L, std::uint8_t kind) { return static_cast<int>(std::count(L.t.begin(), L.t.end(), kind)); }

int cheb_to_box(int x, int y, int x0, int y0, int x1, int y1)
{
    const int dx = x < x0 ? x0 - x : x >= x1 ? x - (x1 - 1) : 0;
    const int dy = y < y0 ? y0 - y : y >= y1 ? y - (y1 - 1) : 0;
    return std::max(dx, dy);
}

// One layout attempt; false when the constraints (placement, connectivity) fail.
bool make_layout(const MapTemplate& tp, GenRng& rng, Layout& L)
{
    L = Layout{};
    L.w = tp.w;
    L.h = tp.h;
    L.sym = tp.sym;
    L.t.assign(static_cast<std::size_t>(L.w * L.h), T_FLOOR);
    L.reserved.assign(L.t.size(), 0);
    // Spawn A in the canonical half, near an edge (corner region for rotational maps).
    const int sw = tp.sw, sh = tp.sh;
    for (int tries = 0;; ++tries)
    {
        if (tries > 200) return false;
        if (L.sym == 0) { L.sx0 = rng.range(1, std::max(1, L.w / 5)); L.sy0 = rng.range(1, L.h - sh - 1); }
        else if (L.sym == 1) { L.sx0 = rng.range(1, L.w - sw - 1); L.sy0 = rng.range(1, std::max(1, L.h / 5)); }
        else { L.sx0 = rng.range(1, std::max(1, L.w / 4)); L.sy0 = rng.range(1, std::max(1, L.h / 4)); }
        L.sx1 = L.sx0 + sw;
        L.sy1 = L.sy0 + sh;
        const auto [mx, my] = L.mirror(L.sx0, L.sy0);
        const auto [nx, ny] = L.mirror(L.sx1 - 1, L.sy1 - 1);
        const int bx0 = std::min(mx, nx), by0 = std::min(my, ny), bx1 = std::max(mx, nx) + 1, by1 = std::max(my, ny) + 1;
        if (L.sx1 <= L.w && L.sy1 <= L.h && (bx0 >= L.sx1 + 4 || bx1 + 4 <= L.sx0 || by0 >= L.sy1 + 4 || by1 + 4 <= L.sy0)) break;
    }
    for (int y = 0; y < L.h; ++y)
        for (int x = 0; x < L.w; ++x)
        {
            const auto [mx, my] = L.mirror(x, y);
            if (cheb_to_box(x, y, L.sx0, L.sy0, L.sx1, L.sy1) <= 2 || cheb_to_box(mx, my, L.sx0, L.sy0, L.sx1, L.sy1) <= 2)
                L.reserved[static_cast<std::size_t>(y * L.w + x)] = 1;
        }
    const double area = static_cast<double>(L.w * L.h);
    // Terrain to the template's densities: holes and bushes as blobs, walls as segments and blobs.
    for (int guard = 0; count_of(L, T_HOLE) < tp.hole * area && guard < 400; ++guard) blob(L, rng, T_HOLE, rng.range(6, 60), rng.range(0, 2));
    for (int guard = 0; count_of(L, T_WALL) < tp.wall * area && guard < 600; ++guard)
        if (rng.below(3)) segment(L, rng, rng.range(3, 12));
        else blob(L, rng, T_WALL, rng.range(4, 16), rng.range(0, 1));
    for (int guard = 0; count_of(L, T_BUSH) < tp.bush * area && guard < 400; ++guard) blob(L, rng, T_BUSH, rng.range(4, 30), rng.range(0, 2));
    auto free_for_feature = [&](int x, int y, int margin) {
        if (!L.inside(x, y) || L.reserved[static_cast<std::size_t>(y * L.w + x)] || L.tile(x, y) != T_FLOOR) return false;
        const auto [mx, my] = L.mirror(x, y);
        if (L.tile(mx, my) != T_FLOOR) return false;
        return cheb_to_box(x, y, L.sx0, L.sy0, L.sx1, L.sy1) >= margin && cheb_to_box(mx, my, L.sx0, L.sy0, L.sx1, L.sy1) >= margin;
    };
    // Altars: mirrored pairs, plus one self-mirrored tile when the count is odd and the map has one.
    int placed = 0;
    if (tp.altars % 2 == 1)
    {
        const int cx = L.w / 2, cy = L.h / 2;
        const auto [mx, my] = L.mirror(cx, cy);
        if (mx == cx && my == cy && L.tile(cx, cy) == T_FLOOR) { L.at(cx, cy) = T_ALTAR; L.altars.push_back({ cx, cy }); ++placed; }
    }
    for (int guard = 0; placed < tp.altars && guard < 2000; ++guard)
    {
        const int x = rng.below(L.w), y = rng.below(L.h);
        if (!L.canonical(x, y) || !free_for_feature(x, y, 6)) continue;
        const auto [mx, my] = L.mirror(x, y);
        if (mx == x && my == y) continue;
        L.put(x, y, T_ALTAR);
        L.altars.push_back({ x, y });
        L.altars.push_back({ mx, my });
        placed += 2;
    }
    if (placed == 0) return false;
    // Camp groups: half the template's compositions (mirrored), each a connected cluster on floor,
    // two tiles clear of other groups and of its own mirror.
    std::vector<std::string> comps = tp.groups;
    for (std::size_t i = comps.size(); i > 1; --i) std::swap(comps[i - 1], comps[static_cast<std::size_t>(rng.below(static_cast<int>(i)))]);
    comps.resize((comps.size() + 1) / 2);
    std::vector<std::uint8_t> near_camp(L.t.size(), 0);
    for (const auto& comp : comps)
    {
        bool ok_group = false;
        for (int tries = 0; tries < 300 && !ok_group; ++tries)
        {
            const int ax = rng.below(L.w), ay = rng.below(L.h);
            if (!L.canonical(ax, ay) || !free_for_feature(ax, ay, 5) || near_camp[static_cast<std::size_t>(ay * L.w + ax)]) continue;
            std::vector<std::pair<int, int>> cells{ { ax, ay } };
            for (std::size_t k = 0; cells.size() < comp.size() && k < cells.size(); ++k)
                for (int d = 0; d < 8 && cells.size() < comp.size(); ++d)
                {
                    static const int dx[8] = { 1, 0, -1, 0, 1, -1, 1, -1 }, dy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
                    const int x = cells[k].first + dx[d], y = cells[k].second + dy[d];
                    if (!free_for_feature(x, y, 5) || near_camp[static_cast<std::size_t>(y * L.w + x)]) continue;
                    if (std::find(cells.begin(), cells.end(), std::pair{ x, y }) != cells.end()) continue;
                    cells.push_back({ x, y });
                }
            if (cells.size() < comp.size()) continue;
            bool clear = true;
            for (const auto& [x, y] : cells)
            {
                const auto [mx, my] = L.mirror(x, y);
                for (const auto& [ox, oy] : cells)
                    if (std::max(std::abs(mx - ox), std::abs(my - oy)) <= 2) clear = false;
            }
            if (!clear) continue;
            for (std::size_t i = 0; i < cells.size(); ++i)
            {
                const auto [x, y] = cells[i];
                const auto [mx, my] = L.mirror(x, y);
                L.put(x, y, T_CAMP);
                const int type = comp[i] - '0';
                L.camps.push_back({ type, x, y });
                L.camps.push_back({ type, mx, my });
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        if (L.inside(x + dx, y + dy)) near_camp[static_cast<std::size_t>((y + dy) * L.w + x + dx)] = 1;
                        if (L.inside(mx + dx, my + dy)) near_camp[static_cast<std::size_t>((my + dy) * L.w + mx + dx)] = 1;
                    }
            }
            ok_group = true;
        }
        if (!ok_group) return false;
    }
    // Connectivity from spawn A over walkable tiles (floor, bush; not B's spawn box): B's spawn edge,
    // a neighbour of every camp group member set and of every altar must be reachable.
    const auto [bx, by] = L.mirror(L.sx0, L.sy0);
    const auto [cx, cy] = L.mirror(L.sx1 - 1, L.sy1 - 1);
    const int bx0 = std::min(bx, cx), by0 = std::min(by, cy), bx1 = std::max(bx, cx) + 1, by1 = std::max(by, cy) + 1;
    std::vector<std::uint8_t> seen(L.t.size(), 0);
    std::vector<int> queue;
    for (int y = L.sy0; y < L.sy1; ++y)
        for (int x = L.sx0; x < L.sx1; ++x) { seen[static_cast<std::size_t>(y * L.w + x)] = 1; queue.push_back(y * L.w + x); }
    for (std::size_t q = 0; q < queue.size(); ++q)
    {
        const int x = queue[q] % L.w, y = queue[q] / L.w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int px = x + dx, py = y + dy;
                if (!L.inside(px, py) || seen[static_cast<std::size_t>(py * L.w + px)]) continue;
                const auto v = L.tile(px, py);
                if ((v != T_FLOOR && v != T_BUSH) || cheb_to_box(px, py, bx0, by0, bx1, by1) == 0) continue;
                seen[static_cast<std::size_t>(py * L.w + px)] = 1;
                queue.push_back(py * L.w + px);
            }
    }
    auto reachable_near = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (L.inside(x + dx, y + dy) && seen[static_cast<std::size_t>((y + dy) * L.w + x + dx)]) return true;
        return false;
    };
    bool to_b = false;
    for (int y = by0 - 1; y <= by1 && !to_b; ++y)
        for (int x = bx0 - 1; x <= bx1 && !to_b; ++x)
            if (L.inside(x, y) && cheb_to_box(x, y, bx0, by0, bx1, by1) == 1 && seen[static_cast<std::size_t>(y * L.w + x)]) to_b = true;
    if (!to_b) return false;
    for (const auto& [x, y] : L.altars)
        if (!reachable_near(x, y)) return false;
    for (const auto& c : L.camps)
        if (!reachable_near(c[1], c[2])) return false;  // every member: conservative (groups may block their own inner tiles)
    return true;
}

// Encode a layout with the MapBuilder; empty on an engine rejection (reason in `why`).
std::vector<std::uint8_t> build_map(EngineRt& E, const Layout& L, int draft, bool auto_mirror, std::string& why)
{
    auto fail = [&](const char* step) { why = std::string(step) + ": " + E.last_error(); return std::vector<std::uint8_t>{}; };
    if (E.call(F_MB_Create, { I32(L.w), I32(L.h), I32(L.sym), U(E.s_out) })) return fail("Create");
    std::int64_t h;
    std::memcpy(&h, E.mem() + E.s_out, 8);
    std::vector<std::uint8_t> bytes;
    do
    {
        if (E.call(F_MB_SetChamps, { I64(h), I32(draft) })) { fail("SetNumChampsPerTeam"); break; }
        const std::int32_t box[4] = { L.sx0, L.sy0, L.sx1, L.sy1 };
        std::memcpy(E.mem() + E.s_out, box, 16);
        if (E.call(F_MB_SetSpawn, { I64(h), U(E.s_out) })) { fail("SetTeamASpawnArea"); break; }
        bool bad = false;
        for (int y = 0; y < L.h && !bad; ++y)
            for (int x = 0; x < L.w && !bad; ++x)
            {
                if (auto_mirror && !L.canonical(x, y)) continue;
                const auto v = L.tile(x, y);
                const Fn f = v == T_WALL ? F_MB_AddWall : v == T_HOLE ? F_MB_AddHole : v == T_BUSH ? F_MB_AddBush : v == T_ALTAR ? F_MB_AddAltar : F_COUNT;
                if (f != F_COUNT && E.call(f, { I64(h), I32(x), I32(y) })) { fail("Add terrain"); bad = true; }
            }
        for (const auto& c : L.camps)
        {
            if (bad) break;
            if (auto_mirror && !L.canonical(c[1], c[2])) continue;
            if (E.call(F_MB_AddCamp, { I64(h), I32(c[0]), I32(c[1]), I32(c[2]), U(E.s_out) })) { fail("AddCamp"); bad = true; }
        }
        if (bad) break;
        if (E.call(F_MB_Verify, { I64(h) })) { fail("VerifyIntegrity"); break; }
        if (E.call(F_MB_Encode, { I64(h), U(E.s_out), U(E.s_out + 4) })) { fail("Encode"); break; }
        std::uint32_t p, n;
        std::memcpy(&p, E.mem() + E.s_out, 4);
        std::memcpy(&n, E.mem() + E.s_out + 4, 4);
        bytes.assign(E.mem() + p, E.mem() + p + n);
    } while (false);
    E.call(F_MB_Destroy, { I64(h) });
    if (bytes.empty()) return bytes;
    // Round trip through the reader and the game loader (they enforce rules the encoder does not).
    const auto in = E.malloc(static_cast<std::uint32_t>(bytes.size()));
    std::memcpy(E.mem() + in, bytes.data(), bytes.size());
    bool ok_map = false;
    if (!E.call(F_MB_FromBytes, { U(in), U(static_cast<std::uint32_t>(bytes.size())), U(E.s_out) }))
    {
        std::int64_t back;
        std::memcpy(&back, E.mem() + E.s_out, 8);
        const bool verified = !E.call(F_MB_Verify, { I64(back) });
        E.call(F_MB_Destroy, { I64(back) });
        if (verified && !E.call(F_FromMap, { U(in), U(static_cast<std::uint32_t>(bytes.size())), U(E.s_out), I32(0) }))
        {
            std::int64_t game;
            std::memcpy(&game, E.mem() + E.s_out, 8);
            ok_map = !E.call(F_RunnerInit, { I64(game) });
            if (!ok_map) why = "Runner_Init: " + E.last_error();
            E.call(F_Destroy, { I64(game) });
        }
        else why = "read back: " + E.last_error();
    }
    else why = "FromFileBytes: " + E.last_error();
    E.free(in);
    if (!ok_map) bytes.clear();
    return bytes;
}

// Does the builder mirror placements itself? (A wall at (1, 1) on a 30x30 rotational map.)
bool builder_mirrors(EngineRt& E)
{
    if (E.call(F_MB_Create, { I32(30), I32(30), I32(2), U(E.s_out) })) die("MapBuilder_Create: " + E.last_error());
    std::int64_t h;
    std::memcpy(&h, E.mem() + E.s_out, 8);
    if (E.call(F_MB_AddWall, { I64(h), I32(1), I32(1) })) die("MapBuilder_AddWall: " + E.last_error());
    if (E.call(F_MB_GetWall, { I64(h), I32(28), I32(28), U(E.s_out) })) die("MapBuilder_GetWall: " + E.last_error());
    const bool mirrored = E.mem()[E.s_out] != 0;
    E.call(F_MB_Destroy, { I64(h) });
    return mirrored;
}

int genmaps(Shared& sh, const std::string& templates_path, const std::string& out_dir, int count, std::uint64_t seed)
{
    const auto templates = read_templates(templates_path);
    auto E = new_engine(sh);
    const bool auto_mirror = builder_mirrors(*E);
    std::fprintf(stderr, "genmap: builder mirrors placements itself: %s\n", auto_mirror ? "yes" : "no");
    fs::create_directories(out_dir);
    GenRng rng{ seed * 0x9E3779B97F4A7C15ull + 1 };
    int made = 0, attempts = 0, rejected = 0;
    while (made < count && attempts < count * 400)
    {
        const auto& tp = templates[static_cast<std::size_t>(rng.below(static_cast<int>(templates.size())))];
        Layout L;
        ++attempts;
        if (!make_layout(tp, rng, L)) continue;
        std::string why;
        const auto bytes = build_map(*E, L, tp.draft, auto_mirror, why);
        if (bytes.empty())
        {
            if (++rejected <= 5) std::fprintf(stderr, "genmap: engine rejected a %s-like layout: %s\n", tp.id.c_str(), why.c_str());
            continue;
        }
        char name[96];
        std::snprintf(name, sizeof name, "g%llu-%03d-%s.map26a", static_cast<unsigned long long>(seed), made, tp.id.c_str());
        write_file(fs::path(out_dir) / name, bytes.data(), bytes.size());
        ++made;
    }
    wasmtime_store_delete(E->store);
    std::fprintf(stderr, "genmap: %d maps in %d attempts (%d rejected by the engine) -> %s\n", made, attempts, rejected, out_dir.c_str());
    return made == count ? 0 : 1;
}

void usage()
{
    std::fprintf(stderr, "usage: fastrun --engine E.wasm --a A.wasm --b B.wasm --map M.map26a [--map ...] [--sides A|B|both] [--jobs N]\n"
                         "               [--out rows.jsonl] [--bundles DIR] [--rounds N] [--log-limit BYTES] [--cache DIR]\n"
                         "               [--name-a X] [--name-b Y] [--stdout-type N] [--params-a W.txt] [--params-b W.txt]\n"
                         "               [--games FILE]   (lines: MAP SIDE(A|B) WEIGHTS_A|- WEIGHTS_B|- [TAG]; replaces --map/--sides)\n"
                         "       fastrun --engine E.wasm --bot KEY=BOT.wasm [--bot ...] --games FILE [--jobs N] [--out rows.jsonl]\n"
                         "               (lines: MAP SIDE KEY_A WEIGHTS_A|- KEY_B WEIGHTS_B|- [TAG])\n"
                         "       --procs N: shard over N single-job child processes (needs --out; scales where threads do not)\n"
                         "       fastrun --engine E.wasm --genmap N --templates T.txt --outdir DIR [--seed S]\n");
    std::exit(2);
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    int gen_count = 0;
    std::string gen_templates, gen_dir;
    std::uint64_t gen_seed = 1;
    std::vector<std::pair<std::string, std::string>> bot_defs;  // --bot KEY=PATH
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (k == "--engine") opt.engine_path = next();
        else if (k == "--a") opt.bot_path[0] = next();
        else if (k == "--b") opt.bot_path[1] = next();
        else if (k == "--name-a") opt.bot_name[0] = next();
        else if (k == "--name-b") opt.bot_name[1] = next();
        else if (k == "--map") opt.maps.push_back(next());
        else if (k == "--sides") opt.sides = next();
        else if (k == "--jobs") opt.jobs = std::max(1, std::atoi(next().c_str()));
        else if (k == "--procs") opt.procs = std::max(1, std::atoi(next().c_str()));
        else if (k == "--out") opt.out_path = next();
        else if (k == "--bundles") opt.bundles = next();
        else if (k == "--rounds") opt.rounds = std::atoi(next().c_str());
        else if (k == "--log-limit") opt.log_limit = static_cast<std::size_t>(std::atoll(next().c_str()));
        else if (k == "--cache") opt.cache = next();
        else if (k == "--stdout-type") opt.stdout_type = std::atoi(next().c_str());
        else if (k == "--calls-log") { opt.calls_log = next(); g_counting = true; }
        else if (k == "--params-a") opt.params_path[0] = next();
        else if (k == "--params-b") opt.params_path[1] = next();
        else if (k == "--games") opt.games_path = next();
        else if (k == "--bot")
        {
            const std::string def = next();
            const auto eq = def.find('=');
            if (eq == std::string::npos || eq == 0) die("--bot wants KEY=PATH, got " + def);
            bot_defs.push_back({ def.substr(0, eq), def.substr(eq + 1) });
        }
        else if (k == "--genmap") gen_count = std::atoi(next().c_str());
        else if (k == "--templates") gen_templates = next();
        else if (k == "--outdir") gen_dir = next();
        else if (k == "--seed") gen_seed = std::strtoull(next().c_str(), nullptr, 10);
        else usage();
    }
    if (gen_count > 0)
    {
        if (opt.engine_path.empty() || gen_templates.empty() || gen_dir.empty()) usage();
        Shared sh;
        sh.engine_engine = wasm_engine_new_with_config(wasm_config_new());
        sh.engine_module = load_module(sh.engine_engine, opt.engine_path, opt.cache, "engine");
        link_engine(sh);
        return genmaps(sh, gen_templates, gen_dir, gen_count, gen_seed);
    }
    // Programs: --bot KEY=PATH (any number; games files name them by key), or --a/--b (keys a and b).
    if (bot_defs.empty())
    {
        if (opt.bot_path[0].empty() || opt.bot_path[1].empty()) usage();
        for (int i = 0; i < 2; ++i)
        {
            if (opt.bot_name[i].empty()) opt.bot_name[i] = fs::path(opt.bot_path[i]).parent_path().parent_path().parent_path().filename().string();
            bot_defs.push_back({ opt.bot_name[i], opt.bot_path[i] });
        }
    }
    else if (opt.games_path.empty() && bot_defs.size() != 2)
        usage();  // without a games file, the first two --bot definitions play each other
    if (opt.engine_path.empty() || (opt.maps.empty() == opt.games_path.empty())) usage();

    Shared sh;
    {
        wasm_config_t* config = wasm_config_new();
        sh.engine_engine = wasm_engine_new_with_config(config);
        wasm_config_t* bot_config = wasm_config_new();
        wasmtime_config_consume_fuel_set(bot_config, true);
        wasmtime_config_async_stack_size_set(bot_config, 8u << 20);
        sh.bot_engine = wasm_engine_new_with_config(bot_config);
    }
    const auto c0 = std::chrono::steady_clock::now();
    sh.engine_module = load_module(sh.engine_engine, opt.engine_path, opt.cache, "engine");
    link_engine(sh);
    sh.programs.resize(bot_defs.size());
    std::map<std::string, std::size_t> key_of;
    for (std::size_t i = 0; i < bot_defs.size(); ++i)
    {
        auto& p = sh.programs[i];
        if (!key_of.emplace(bot_defs[i].first, i).second) die("duplicate --bot key " + bot_defs[i].first);
        p.name = bot_defs[i].first;
        p.path = bot_defs[i].second;
        p.module = load_module(sh.bot_engine, p.path, opt.cache, "bot");
        link_bot(sh, p);
        find_params(sh, p);
    }
    auto program_of = [&](const std::string& key, const std::string& line) {
        const auto it = key_of.find(key);
        if (it == key_of.end()) die("unknown bot key " + key + " in --games line: " + line);
        return it->second;
    };
    const double compile_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();

    // Weight files are read once here; workers only read the parsed vectors.
    std::map<std::string, Weights> weights;
    auto weights_of = [&](const std::string& path) -> const Weights* {
        if (path.empty() || path == "-") return nullptr;
        auto it = weights.find(path);
        if (it == weights.end()) it = weights.emplace(path, read_weights(path)).first;
        return &it->second;
    };
    struct Task
    {
        std::string map;
        int a_team;
        std::size_t pa, pb;
        const Weights* wa;
        const Weights* wb;
        std::string tag, wa_path = "-", wb_path = "-";
    };
    std::vector<Task> tasks;
    if (!opt.games_path.empty())
    {
        // Lines: MAP SIDE KEY_A WEIGHTS_A KEY_B WEIGHTS_B [TAG] (bots by --bot key), or the two-bot
        // form MAP SIDE WEIGHTS_A WEIGHTS_B [TAG] (the first two programs).
        const auto bytes = read_file(opt.games_path);
        std::istringstream lines(std::string(bytes.begin(), bytes.end()));
        std::string line;
        while (std::getline(lines, line))
        {
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string t; f >> t;) w.push_back(t);
            if (w.empty()) continue;
            if (w.size() < 4 || (w[1] != "A" && w[1] != "B")) die("bad --games line: " + line);
            const int side = w[1] == "A" ? 0 : 1;
            if (w.size() >= 6)
                tasks.push_back({ w[0], side, program_of(w[2], line), program_of(w[4], line), weights_of(w[3]), weights_of(w[5]),
                                  w.size() > 6 ? w[6] : "", w[3], w[5] });
            else
                tasks.push_back({ w[0], side, 0, 1, weights_of(w[2]), weights_of(w[3]), w.size() > 4 ? w[4] : "", w[2], w[3] });
        }
    }
    else
    {
        const std::string pa = opt.params_path[0].empty() ? "-" : opt.params_path[0], pb = opt.params_path[1].empty() ? "-" : opt.params_path[1];
        for (const auto& m : opt.maps)
        {
            if (opt.sides == "A" || opt.sides == "both") tasks.push_back({ m, 0, 0, 1, weights_of(pa), weights_of(pb), "", pa, pb });
            if (opt.sides == "B" || opt.sides == "both") tasks.push_back({ m, 1, 0, 1, weights_of(pa), weights_of(pb), "", pa, pb });
        }
    }
    // A weight vector must fit the block of the bot it is given to (checked again per instance).
    for (const auto& t : tasks)
        for (int i = 0; i < 2; ++i)
        {
            const auto& p = sh.programs[i == 0 ? t.pa : t.pb];
            if (const Weights* w = i == 0 ? t.wa : t.wb; w && (w->size() > p.params_count || p.params_at < 0))
                die("weights for " + p.path + " hold " + std::to_string(w->size()) + " values, its block " +
                    std::to_string(p.params_count) + (p.params_at < 0 ? " (no block)" : ""));
        }
    // --procs N: shard the games over N child processes with one job each and merge their rows.
    // Threads in one process stop scaling at 2-4 jobs (a lock inside the process: 80 games took
    // ~9 s on 4 or 32 jobs); single-job processes scale to the core count (42 games/s on 24).
    if (opt.procs > 1 && tasks.size() > 1)
    {
        if (opt.out_path.empty()) die("--procs needs --out");
        if (g_counting) die("--procs does not support --calls-log");
        const int procs = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(opt.procs), tasks.size()));
        const fs::path dir = fs::temp_directory_path() / ("fastrun-" + std::to_string(_getpid()) + "-" +
                                                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        auto quote = [](const std::string& s) { return s.find_first_of(" \t\"") == std::string::npos ? s : "\"" + s + "\""; };
        std::vector<intptr_t> children;
        std::vector<fs::path> outs;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < procs; ++k)
        {
            const fs::path games = dir / ("games-" + std::to_string(k) + ".txt"), rows = dir / ("rows-" + std::to_string(k) + ".jsonl");
            std::string text;
            for (std::size_t i = static_cast<std::size_t>(k); i < tasks.size(); i += static_cast<std::size_t>(procs))
            {
                const auto& t = tasks[i];
                text += t.map + " " + (t.a_team == 0 ? "A" : "B") + " " + sh.programs[t.pa].name + " " + t.wa_path + " " +
                        sh.programs[t.pb].name + " " + t.wb_path + (t.tag.empty() ? "" : " " + t.tag) + "\n";
            }
            write_file(games, text.data(), text.size());
            std::vector<std::string> args = { argv[0], "--engine", opt.engine_path, "--games", games.string(), "--jobs", "1",
                                              "--out", rows.string(), "--cache", opt.cache, "--log-limit", std::to_string(opt.log_limit),
                                              "--stdout-type", std::to_string(opt.stdout_type) };
            for (const auto& p : sh.programs) { args.push_back("--bot"); args.push_back(p.name + "=" + p.path); }
            if (opt.rounds) { args.push_back("--rounds"); args.push_back(std::to_string(opt.rounds)); }
            if (!opt.bundles.empty()) { args.push_back("--bundles"); args.push_back(opt.bundles); }
            std::vector<std::string> quoted;
            for (const auto& a : args) quoted.push_back(quote(a));
            std::vector<const char*> cargv;
            for (const auto& a : quoted) cargv.push_back(a.c_str());
            cargv.push_back(nullptr);
            const intptr_t h = _spawnv(_P_NOWAIT, argv[0], cargv.data());
            if (h == -1) die("cannot start a child process (" + std::string(argv[0]) + ")");
            children.push_back(h);
            outs.push_back(rows);
        }
        int failed = 0;
        for (const auto h : children)
        {
            int status = 0;
            if (_cwait(&status, h, _WAIT_CHILD) == -1 || status != 0) ++failed;
        }
        std::FILE* out = std::fopen(opt.out_path.c_str(), "ab");
        if (!out) die("cannot open " + opt.out_path);
        std::size_t rows_n = 0;
        for (const auto& p : outs)
        {
            if (!fs::exists(p)) continue;
            const auto bytes = read_file(p.string());
            std::fwrite(bytes.data(), 1, bytes.size(), out);
            rows_n += static_cast<std::size_t>(std::count(bytes.begin(), bytes.end(), '\n'));
        }
        std::fclose(out);
        std::error_code ec;
        fs::remove_all(dir, ec);
        const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "fastrun: %zu games in %.0f ms on %d processes (compile/load %.0f ms)\n", rows_n, total_ms, procs, compile_ms);
        if (failed || rows_n != tasks.size())
            die(std::to_string(failed) + " child processes failed; " + std::to_string(rows_n) + " of " + std::to_string(tasks.size()) + " rows");
        return 0;
    }
    std::FILE* out = opt.out_path.empty() ? stdout : std::fopen(opt.out_path.c_str(), "ab");
    if (!out) die("cannot open " + opt.out_path);
    std::mutex lock;
    std::atomic<std::size_t> next{ 0 };
    const auto t0 = std::chrono::steady_clock::now();
    auto worker = [&]() {
        TRACE("worker start\n");
        auto E = new_engine(sh);
        TRACE("engine ready\n");
        for (;;)
        {
            const auto i = next++;
            if (i >= tasks.size()) break;
            const auto& t = tasks[i];
            const auto r = play(sh, *E, opt, t.map, t.a_team, opt.bundles, &sh.programs[t.pa], &sh.programs[t.pb], t.wa, t.wb, t.tag);
            const auto line = row_json(r, opt);
            std::lock_guard<std::mutex> guard(lock);
            std::fprintf(out, "%s\n", line.c_str());
            std::fflush(out);
        }
        wasmtime_store_delete(E->store);
    };
    std::vector<std::thread> threads;
    const int jobs = std::min<int>(opt.jobs, static_cast<int>(tasks.size()));
    for (int j = 0; j < jobs; ++j) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (out != stdout) std::fclose(out);
    std::fprintf(stderr, "fastrun: %zu games in %.0f ms on %d jobs (compile/load %.0f ms)\n", tasks.size(), total_ms, jobs, compile_ms);
    return 0;
}
