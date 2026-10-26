#pragma once
#include <cstdint>

namespace zero_sequence {
inline constexpr char magic[16] = {'z','e','r','o','.','t','r','i','b','u','t','e','.','v','1',0};
enum Mode : std::int32_t { natural = 0, placebo = 1, treatment = 2 };
#pragma pack(push, 1)
struct Payload { std::int32_t type{},kind{},target{-1},count{1},x{},y{},status{}; };
struct Control {
    char marker[16];
    std::int32_t phase{},mode{},failed{},interval{},installed{},last_init{-1};
    std::int32_t sources{},decisions{},dispatches{},altar{-1},preflight{},last_receipt{-1};
    Payload mirror{},choice{};
    std::uint64_t root{},known{},dispatch_known{},arguments[15]{},dispatch[15]{};
    std::uint64_t refresh_known{},refresh[15]{};
};
struct Span { std::uint32_t name{},data{},bytes{}; std::int32_t index{-1}; };
struct Frame {
    std::int32_t phase{},interval{},round{},actor{},team{},x{},y{},gems{},bank_us{},bank_them{};
    std::int32_t channel{},remaining{},command_index{},init_stage{},turns{},path_fields{},cache_prepared{};
    std::int32_t observer_complete{},observer_reset{};
    std::uint64_t observer_revision{};
    std::uint32_t spans{},count{};
};
struct Choice {
    Payload payload;
    std::int32_t round{},command_index{},natural{},admitted{};
    std::uint32_t rule{},used{},admitted_mask{};
    std::uint64_t fuel[7]{};
};
struct Dispatch {
    Payload payload;
    std::int32_t receipt{},round{},command_index{};
    std::uint64_t before{},after{};
};
struct Trace {
    std::int32_t stage{},head{},command{},label{},natural{},forced{},status{};
    std::uint32_t rule{},admitted{},used{},state{},contexts{},context_count{},context_stride{},group_offset{},context_phi{};
    std::uint32_t rows{},row_count{},row_stride{},kind{},target{},count{},x{},y{},phi{},scores{},score_count{};
    float scalar{};
};
#pragma pack(pop)
}
