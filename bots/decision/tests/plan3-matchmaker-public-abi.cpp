#include <cambc.h>
#include <cstddef>
using namespace cambc;
static_assert(sizeof(detail::game_settings)==120);
static_assert(offsetof(detail::game_settings,submit_gem_duration_multiplier)==16);
static_assert(offsetof(detail::game_settings,spawn_healing)==56);
static_assert(sizeof(detail::metadata)==28);
static_assert(sizeof(detail::champion_design)==128);
static_assert(offsetof(detail::champion_design,base_stats)==88);
static_assert(sizeof(detail::camp_design)==124);
static_assert(offsetof(detail::camp_design,base_stats)==88);
static_assert(sizeof(champion_stats)==40 && sizeof(camp_stats)==36);
static_assert(sizeof(detail::consumable_info)==8);
static_assert(sizeof(std::uint16_t)==2 && sizeof(float)==4 && sizeof(double)==8);
static_assert(static_cast<int>(champion_type::tavi)==11);
static_assert(static_cast<int>(champion_type::varg)==10);
static_assert(static_cast<int>(champion_type::vendetta)==2);
static_assert(static_cast<int>(champion_type::vesper_vale)==1);
static_assert(static_cast<int>(champion_type::marcella)==7);
