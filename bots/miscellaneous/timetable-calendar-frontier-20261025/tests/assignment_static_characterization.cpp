// Exact YBR geometry and catalogue-conditional metadata, embedded for cache
// independence. This measures admitted structural units, NOT native fuel/time,
// current champion starts/stats, supported prices or SDK action authority.
#include "../src/assignment_static_canonical.h"
#include "../src/assignment_static_world.h"
#include "../src/assignment_static_menu.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {
namespace ag=tt::assignment_geometry;namespace sg=tt::assignment_static_geometry;
namespace sw=tt::assignment_static_world;namespace sm=tt::assignment_static_menu;
namespace sc=tt::assignment_static_canonical;namespace pj=tt::production_job;
// BEGIN GENERATED YBR LITERALS
inline constexpr int literal_width=40,literal_height=30;
inline constexpr char literal_map_sha[]="130453395278cdabb1ff80564357a99ca8394afaef006fdede8beb88924a8fa0";
inline constexpr char literal_catalog_sha[]="7cc2c639c314d0c739ad74e0fc20ade667304276a6040c014f92f5915f0b1d87";
struct LiteralMember {int x,y;float max_hp,strength;int range;};
struct LiteralGroup {std::size_t offset,count;};
struct LiteralKit {const char* name;int basic_range,family,cast_range,radius;};
inline constexpr std::array<LiteralMember,160> literal_members{{
    {3,0,120.0f,0.0f,3},
    {4,0,2400.0f,60.0f,4},
    {5,0,120.0f,0.0f,3},
    {4,1,120.0f,0.0f,3},
    {16,0,120.0f,0.0f,3},
    {17,0,1350.0f,20.0f,3},
    {18,0,120.0f,0.0f,3},
    {17,1,1350.0f,20.0f,3},
    {18,1,120.0f,0.0f,3},
    {29,0,1350.0f,20.0f,3},
    {30,0,120.0f,0.0f,3},
    {31,0,1350.0f,20.0f,3},
    {30,1,120.0f,0.0f,3},
    {0,3,120.0f,0.0f,3},
    {0,4,2400.0f,60.0f,4},
    {1,4,120.0f,0.0f,3},
    {0,5,1350.0f,20.0f,3},
    {1,5,120.0f,0.0f,3},
    {0,6,120.0f,0.0f,3},
    {11,4,120.0f,0.0f,3},
    {12,4,1350.0f,20.0f,3},
    {13,4,120.0f,0.0f,3},
    {22,5,1350.0f,20.0f,3},
    {22,6,120.0f,0.0f,3},
    {28,5,1350.0f,20.0f,3},
    {29,5,120.0f,0.0f,3},
    {35,5,120.0f,0.0f,3},
    {36,5,1350.0f,20.0f,3},
    {37,5,2400.0f,60.0f,4},
    {6,8,1350.0f,20.0f,3},
    {7,8,120.0f,0.0f,3},
    {6,9,1350.0f,20.0f,3},
    {7,9,120.0f,0.0f,3},
    {17,9,120.0f,0.0f,3},
    {18,9,2400.0f,60.0f,4},
    {5,12,120.0f,0.0f,3},
    {6,12,120.0f,0.0f,3},
    {7,12,120.0f,0.0f,3},
    {8,12,120.0f,0.0f,3},
    {9,12,120.0f,0.0f,3},
    {10,12,120.0f,0.0f,3},
    {11,12,120.0f,0.0f,3},
    {12,12,120.0f,0.0f,3},
    {13,12,120.0f,0.0f,3},
    {14,12,120.0f,0.0f,3},
    {15,12,120.0f,0.0f,3},
    {16,12,120.0f,0.0f,3},
    {17,12,120.0f,0.0f,3},
    {18,12,120.0f,0.0f,3},
    {19,12,120.0f,0.0f,3},
    {20,12,120.0f,0.0f,3},
    {21,12,120.0f,0.0f,3},
    {22,12,120.0f,0.0f,3},
    {23,12,120.0f,0.0f,3},
    {24,12,120.0f,0.0f,3},
    {25,12,120.0f,0.0f,3},
    {26,12,120.0f,0.0f,3},
    {27,12,120.0f,0.0f,3},
    {28,12,120.0f,0.0f,3},
    {29,12,120.0f,0.0f,3},
    {30,12,120.0f,0.0f,3},
    {31,12,120.0f,0.0f,3},
    {32,12,120.0f,0.0f,3},
    {33,12,120.0f,0.0f,3},
    {34,12,120.0f,0.0f,3},
    {8,14,120.0f,0.0f,3},
    {9,14,1350.0f,20.0f,3},
    {8,15,120.0f,0.0f,3},
    {9,15,1350.0f,20.0f,3},
    {12,14,120.0f,0.0f,3},
    {13,14,2400.0f,60.0f,4},
    {12,15,120.0f,0.0f,3},
    {13,15,2400.0f,60.0f,4},
    {16,14,120.0f,0.0f,3},
    {17,14,1350.0f,20.0f,3},
    {18,14,2400.0f,60.0f,4},
    {16,15,120.0f,0.0f,3},
    {17,15,1350.0f,20.0f,3},
    {18,15,2400.0f,60.0f,4},
    {19,15,120.0f,0.0f,3},
    {20,14,120.0f,0.0f,3},
    {21,14,2400.0f,60.0f,4},
    {22,14,1350.0f,20.0f,3},
    {23,14,120.0f,0.0f,3},
    {21,15,2400.0f,60.0f,4},
    {22,15,1350.0f,20.0f,3},
    {23,15,120.0f,0.0f,3},
    {26,14,2400.0f,60.0f,4},
    {27,14,120.0f,0.0f,3},
    {26,15,2400.0f,60.0f,4},
    {27,15,120.0f,0.0f,3},
    {30,14,1350.0f,20.0f,3},
    {31,14,120.0f,0.0f,3},
    {30,15,1350.0f,20.0f,3},
    {31,15,120.0f,0.0f,3},
    {5,17,120.0f,0.0f,3},
    {6,17,120.0f,0.0f,3},
    {7,17,120.0f,0.0f,3},
    {8,17,120.0f,0.0f,3},
    {9,17,120.0f,0.0f,3},
    {10,17,120.0f,0.0f,3},
    {11,17,120.0f,0.0f,3},
    {12,17,120.0f,0.0f,3},
    {13,17,120.0f,0.0f,3},
    {14,17,120.0f,0.0f,3},
    {15,17,120.0f,0.0f,3},
    {16,17,120.0f,0.0f,3},
    {17,17,120.0f,0.0f,3},
    {18,17,120.0f,0.0f,3},
    {19,17,120.0f,0.0f,3},
    {20,17,120.0f,0.0f,3},
    {21,17,120.0f,0.0f,3},
    {22,17,120.0f,0.0f,3},
    {23,17,120.0f,0.0f,3},
    {24,17,120.0f,0.0f,3},
    {25,17,120.0f,0.0f,3},
    {26,17,120.0f,0.0f,3},
    {27,17,120.0f,0.0f,3},
    {28,17,120.0f,0.0f,3},
    {29,17,120.0f,0.0f,3},
    {30,17,120.0f,0.0f,3},
    {31,17,120.0f,0.0f,3},
    {32,17,120.0f,0.0f,3},
    {33,17,120.0f,0.0f,3},
    {34,17,120.0f,0.0f,3},
    {21,20,2400.0f,60.0f,4},
    {22,20,120.0f,0.0f,3},
    {32,20,120.0f,0.0f,3},
    {33,20,1350.0f,20.0f,3},
    {32,21,120.0f,0.0f,3},
    {33,21,1350.0f,20.0f,3},
    {17,23,120.0f,0.0f,3},
    {17,24,1350.0f,20.0f,3},
    {39,23,120.0f,0.0f,3},
    {38,24,120.0f,0.0f,3},
    {39,24,1350.0f,20.0f,3},
    {38,25,120.0f,0.0f,3},
    {39,25,2400.0f,60.0f,4},
    {39,26,120.0f,0.0f,3},
    {2,24,2400.0f,60.0f,4},
    {3,24,1350.0f,20.0f,3},
    {4,24,120.0f,0.0f,3},
    {10,24,120.0f,0.0f,3},
    {11,24,1350.0f,20.0f,3},
    {26,25,120.0f,0.0f,3},
    {27,25,1350.0f,20.0f,3},
    {28,25,120.0f,0.0f,3},
    {9,28,120.0f,0.0f,3},
    {8,29,1350.0f,20.0f,3},
    {9,29,120.0f,0.0f,3},
    {10,29,1350.0f,20.0f,3},
    {21,28,120.0f,0.0f,3},
    {22,28,1350.0f,20.0f,3},
    {21,29,120.0f,0.0f,3},
    {22,29,1350.0f,20.0f,3},
    {23,29,120.0f,0.0f,3},
    {35,28,120.0f,0.0f,3},
    {34,29,120.0f,0.0f,3},
    {35,29,2400.0f,60.0f,4},
    {36,29,120.0f,0.0f,3},
}};
inline constexpr std::array<LiteralGroup,28> literal_groups{{
    {0,4},
    {4,5},
    {9,4},
    {13,6},
    {19,3},
    {22,2},
    {24,2},
    {26,3},
    {29,4},
    {33,2},
    {35,30},
    {65,4},
    {69,4},
    {73,7},
    {80,7},
    {87,4},
    {91,4},
    {95,30},
    {125,2},
    {127,4},
    {131,2},
    {133,6},
    {139,3},
    {142,2},
    {144,3},
    {147,4},
    {151,5},
    {156,4},
}};
inline constexpr std::array<ag::Point,10> literal_altars{{
    {37,2},
    {16,4},
    {6,5},
    {24,6},
    {19,14},
    {20,15},
    {15,23},
    {33,24},
    {23,25},
    {2,27},
}};
inline constexpr std::array<std::uint32_t,1200> literal_upper{{
    120,120,120,120,120,60,60,60,60,0,0,0,0,0,40,40,40,40,40,40,40,0,0,0,
    0,0,20,20,40,40,40,40,40,20,20,0,0,0,0,0,120,120,120,120,120,60,60,60,
    60,20,20,20,20,20,60,60,40,40,40,40,40,0,0,0,0,0,20,20,40,40,40,40,
    40,80,80,60,60,60,60,60,140,140,140,140,120,60,60,60,60,20,20,20,20,20,60,60,
    40,40,40,60,60,20,20,20,20,40,40,40,60,60,60,60,40,100,100,80,80,80,80,80,
    140,140,140,140,120,60,60,60,60,20,20,20,20,20,60,60,40,40,40,60,60,20,20,20,
    20,40,40,40,60,60,60,60,40,100,100,80,80,80,80,80,140,140,140,140,120,60,60,60,
    60,20,20,20,20,20,40,40,20,20,20,40,40,20,20,20,20,40,20,20,20,20,20,20,
    0,80,80,80,80,80,80,80,80,80,80,100,80,20,20,20,20,40,20,20,20,20,80,80,
    60,60,60,80,80,80,80,20,20,40,20,20,20,20,20,20,0,80,80,80,80,80,80,80,
    80,80,80,120,100,40,40,40,40,60,20,20,20,20,80,80,60,60,60,80,80,80,80,20,
    20,40,20,20,20,20,20,20,0,80,80,80,80,80,80,80,80,80,80,120,100,40,40,40,
    40,60,20,20,20,20,80,80,60,60,60,80,80,80,80,20,20,40,20,20,20,20,20,20,
    0,80,80,80,80,80,80,80,80,80,80,120,100,40,40,40,40,40,0,0,0,0,60,60,
    60,60,60,80,80,80,80,20,20,40,20,20,20,20,20,20,0,80,80,80,80,80,80,80,
    0,0,0,40,40,40,40,40,40,40,0,0,0,0,60,60,60,60,60,60,60,60,60,0,
    0,0,0,0,0,0,0,0,0,60,60,60,60,60,60,60,0,0,0,40,40,40,40,40,
    40,100,60,60,60,60,180,180,180,240,180,180,180,180,240,120,120,120,60,60,60,60,60,0,
    0,0,0,0,0,0,0,0,0,0,0,40,40,40,60,60,60,180,140,140,140,120,320,320,
    320,440,320,340,340,320,440,260,260,260,120,140,140,140,140,20,20,20,0,0,0,0,0,0,
    0,0,0,20,20,20,60,60,60,180,160,160,160,120,340,340,340,460,340,380,380,340,460,280,
    280,280,120,160,160,160,160,40,40,40,0,0,0,0,0,0,0,0,0,0,0,0,40,40,
    40,160,160,160,160,120,340,340,340,460,340,380,380,340,460,280,280,280,120,160,160,160,160,40,
    40,40,0,0,0,0,0,0,0,0,0,0,0,0,40,40,40,160,160,160,160,120,280,280,
    280,400,280,320,320,280,400,280,280,280,120,160,160,160,160,40,40,40,0,0,0,0,0,0,
    0,0,0,0,0,0,40,40,40,160,160,160,160,120,280,280,280,400,280,320,320,280,400,280,
    280,280,120,160,160,160,160,40,40,40,0,0,0,0,0,0,0,0,0,0,0,0,40,40,
    40,160,160,160,160,120,280,280,280,460,340,380,380,340,460,340,340,340,120,160,160,160,160,40,
    40,40,0,0,0,0,0,0,0,0,0,0,0,0,40,40,40,160,160,160,160,120,280,280,
    280,460,340,380,380,340,460,340,340,340,120,160,160,160,180,60,60,60,20,20,20,0,0,0,
    0,0,0,0,0,0,20,20,20,140,140,140,140,120,260,260,260,440,320,340,340,320,440,320,
    320,320,120,140,140,140,180,60,60,60,40,40,40,0,0,0,0,0,0,0,0,0,0,0,
    0,60,60,60,60,60,120,120,120,240,180,180,180,180,240,180,180,180,60,60,60,60,100,40,
    40,40,40,40,40,0,0,0,60,60,60,60,60,60,60,0,0,0,0,0,0,0,0,0,
    0,60,60,60,60,60,60,60,60,60,0,0,0,0,40,40,40,40,40,40,40,0,0,0,
    80,80,80,80,80,80,80,0,20,20,20,20,20,20,40,20,20,80,80,80,80,60,60,60,
    60,60,0,0,0,0,40,40,40,40,40,100,120,80,80,80,80,80,80,80,80,80,80,0,
    20,20,20,20,20,20,40,20,20,80,80,80,80,60,60,60,80,80,20,20,20,20,60,40,
    40,40,40,100,120,80,80,80,80,80,80,80,80,80,80,0,20,20,20,20,20,20,40,20,
    20,80,80,80,80,60,60,60,80,80,20,20,20,20,60,40,40,40,40,100,120,80,80,80,
    80,80,80,80,80,80,80,0,20,20,20,20,20,20,40,20,20,80,80,80,80,60,60,60,
    80,80,20,20,20,20,40,20,20,20,20,80,100,80,80,80,80,80,80,80,80,80,80,0,
    20,20,20,20,20,20,40,20,20,20,20,40,40,20,20,20,40,40,20,20,20,20,20,60,
    60,60,60,120,140,140,140,140,80,80,80,80,80,100,100,40,60,60,60,60,40,40,40,20,
    20,20,20,60,60,40,40,40,60,60,20,20,20,20,20,60,60,60,60,120,140,140,140,140,
    80,80,80,80,80,100,100,40,60,60,60,60,40,40,40,20,20,20,20,60,60,40,40,40,
    60,60,20,20,20,20,20,60,60,60,60,120,140,140,140,140,60,60,60,60,60,80,80,40,
    40,40,40,40,20,20,0,0,0,0,0,40,40,40,40,40,60,60,20,20,20,20,20,60,
    60,60,60,120,120,120,120,120,0,0,0,0,0,20,20,40,40,40,40,40,20,20,0,0,
    0,0,0,40,40,40,40,40,40,40,0,0,0,0,0,60,60,60,60,120,120,120,120,120,
}};
inline constexpr std::array<std::uint8_t,150> literal_terrain_a{{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,32,0,0,0,0,0,0,0,1,0,0,
    64,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,0,0,8,0,60,
    0,0,16,0,60,0,0,0,0,60,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,0,0,0,0,0,0,0,2,
    0,0,128,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
}};
inline constexpr std::array<ag::Point,16> literal_homes_a{{
    {2,13},
    {3,13},
    {4,13},
    {5,13},
    {2,14},
    {3,14},
    {4,14},
    {5,14},
    {2,15},
    {3,15},
    {4,15},
    {5,15},
    {2,16},
    {3,16},
    {4,16},
    {5,16},
}};
inline constexpr ag::Point literal_origin_a{4,15};
inline constexpr std::array<std::uint8_t,150> literal_terrain_b{{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,32,0,0,0,0,0,0,0,1,0,0,
    64,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,0,0,0,0,60,0,8,0,0,
    60,0,16,0,0,60,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,0,0,0,0,0,0,0,2,
    0,0,128,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
}};
inline constexpr std::array<ag::Point,16> literal_homes_b{{
    {34,13},
    {35,13},
    {36,13},
    {37,13},
    {34,14},
    {35,14},
    {36,14},
    {37,14},
    {34,15},
    {35,15},
    {36,15},
    {37,15},
    {34,16},
    {35,16},
    {36,16},
    {37,16},
}};
inline constexpr ag::Point literal_origin_b{36,15};
inline constexpr std::array<LiteralKit,2> literal_kits{{
    {"marcella",1,1,4,0},
    {"ryth",1,2,4,1},
}};
// END GENERATED YBR LITERALS

int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
constexpr std::uint64_t revision=1;constexpr unsigned unit_bound=1000000;
struct Source {ag::Point pos;float max_hp{},strength{};int range{};};
struct Group {std::vector<Source> members;};
struct World {int width{literal_width},height{literal_height};std::vector<Group> groups;std::vector<ag::Point> altars,spawn_tiles;};
struct Potential {
    enum class Status {Ready};struct Value {Status status{Status::Ready};double upper{};};
    Value at(ag::Point point) const {
        return {Status::Ready,static_cast<double>(literal_upper[static_cast<std::size_t>(point.y)*literal_width+point.x])};
    }
};
World world(unsigned side) {
    World out;out.altars.assign(literal_altars.begin(),literal_altars.end());
    for(const auto& input:literal_groups) {
        Group group;
        for(std::size_t i=0;i<input.count;++i) {
            const auto& member=literal_members[input.offset+i];
            group.members.push_back({{member.x,member.y},member.max_hp,member.strength,member.range});
        }
        out.groups.push_back(std::move(group));
    }
    const auto& homes=side?literal_homes_b:literal_homes_a;out.spawn_tiles.assign(homes.begin(),homes.end());return out;
}
struct Built {std::shared_ptr<const sg::StaticGeometry> data;std::uint64_t canonical_units{},world_units{};int status{},reason{};};
Built compile(unsigned side) {
    auto input=world(side);Potential potential;sc::CanonicalBuilder canonical{input};
    canonical.advance(0);check(!canonical.result()&&canonical.work_used()==0,"zero canonical units publish nothing");
    for(unsigned n=0;n<unit_bound&&canonical.status()==sc::Status::NeedCompute;++n) {
        canonical.advance(1);check(canonical.last_work()<=1,"one canonical slice admits at most one structural unit");
    }
    Built out;out.canonical_units=canonical.work_used();
    const auto* order=canonical.result();check(order!=nullptr,"exact embedded groups canonicalize completely");if(!order)return out;
    const auto& packed=side?literal_terrain_b:literal_terrain_a;std::vector<std::uint8_t> terrain(packed.begin(),packed.end());
    sw::Builder builder{input,potential,std::span<const int>{order->local_by_canonical.data(),order->count},
        revision,std::move(terrain),sg::Limits{},sw::Policy{false,true}};
    check(builder.work_used()==0&&!builder.result(),"World constructor performs no admitted advance units or publication");
    builder.advance(revision,0);check(builder.last_work()==0&&!builder.result(),"zero World units publish nothing");
    for(unsigned n=0;n<unit_bound&&builder.status()==ag::Status::NeedCompute;++n) {
        builder.advance(revision,1);check(builder.last_work()<=1,"one World slice admits at most one structural unit");
    }
    out.data=builder.result();out.world_units=builder.work_used();out.status=static_cast<int>(builder.status());out.reason=static_cast<int>(builder.reason());
    if(out.data)check(!out.data->key().require_quiet_bank&&out.data->key().require_quiet_home,
        "current charged-bank/quiet-home geometry policy remains explicit");
    std::printf("SC3 {\"k\":\"w\",\"s\":\"%c\",\"u\":[%llu,%llu],\"v\":[%d,%d,%d]}\n",
        side?'B':'A',static_cast<unsigned long long>(out.canonical_units),static_cast<unsigned long long>(out.world_units),
        out.status,out.reason,out.data?1:0);
    return out;
}
int nearest(const sg::StaticGeometry& geometry,ag::Point origin,std::uint32_t examined=0) {
    int group=-1,best=std::numeric_limits<int>::max();
    for(std::size_t i=0;i<geometry.catalogue().size();++i)if(!(examined&(std::uint32_t{1}<<i)))
        for(const auto& member:geometry.catalogue()[i].members) {
        const int distance=ag::detail::separation(origin,member.pos);
        if(distance<best){best=distance;group=static_cast<int>(i);}
    }
    return group;
}
pj::Snapshot geometry_kit(const LiteralKit& kit) {
    pj::Snapshot out;out.body.attack_range=kit.basic_range;
    out.spells[0].kind=kit.family==1?mb::service::SpellKind::damage:mb::service::SpellKind::aoe;
    out.spells[0].range=kit.cast_range;out.spells[0].radius=kit.radius;out.spells[0].damage=1;
    // Magnitude1 admits the actual catalogue geometry family only. This is not
    // a full own Snapshot or a combat/production forecast, and is not marked own.
    return out;
}
struct Measured {
    struct Nomination {
        int group{},status{},reason{};sm::Progress work;bool ready{},complete{};
    };
    sm::Progress first;std::uint64_t requests{},post_units{};
    std::size_t queued{},reused{},deferred{},ready_count{},families{};
    std::array<Nomination,31> nominations;std::size_t groups_examined{},completed_no_ready_groups{};
    bool ready{},complete{},bounded{},unchanged_stamp{},future_only{},optional_queued{},progress_stopped{};
    int status{},reason{},first_ready_group{-1};std::shared_ptr<const sg::TargetGeometry> target;
};
sm::Progress difference(const sm::Progress& after,const sm::Progress& before) {
    return {after.units-before.units,after.scanned_cells-before.scanned_cells,after.builder_units-before.builder_units,
        after.started-before.started,after.failed-before.failed,after.published-before.published};
}
Measured measure(const std::shared_ptr<const sg::StaticGeometry>& data,const LiteralKit& kit,
                 unsigned side,int group,bool repeat) {
    sm::Menu menu{data};sm::Request initial;initial.canonical_group=group;initial.self=geometry_kit(kit);
    initial.nomination={data->key().layout,data->key().potential,100,200,10,side?literal_origin_b:literal_origin_a};
    initial.handoff_inputs.fill(1);initial.capture_policy=1;
    Measured out;
    const auto request=[&](const sm::Request& value) {
        const auto response=menu.request(value);++out.requests;
        out.queued+=response.queued;out.reused+=response.reused;out.deferred+=response.deferred;
        check(response.status!=sm::RequestStatus::Invalid&&response.status!=sm::RequestStatus::Stale,
            "valid repeated static requests cannot become date/body stale");
    };
    std::uint32_t examined{};
    // Exactly the controller's nearest-remaining-member nomination order at a
    // FIXED favorable pose; a typed exhausted family is not group infeasibility.
    // One Menu owns every prior scan/provider, and the bound is TOTAL prefix
    // units, never a fresh million units per group or a full all-perch sweep.
    while(group>=0&&out.groups_examined<data->catalogue().size()&&menu.progress().units<unit_bound) {
        examined|=std::uint32_t{1}<<group;initial.canonical_group=group;
        request(initial);menu.advance(0);
        check(menu.last_work()==0,"zero Menu units do not scan/build/publish");
        const auto before_group=menu.progress();
        while(!menu.first_ready(group,initial.self)&&!menu.complete()&&menu.status()!=sm::Status::Unsupported&&
              menu.progress().units<unit_bound) {
            const auto n=menu.progress().units;
            if(repeat) {
                auto changed=initial;changed.nomination.body+=n+1;changed.nomination.query+=n+1;
                changed.nomination.epoch+=static_cast<int>(n+1);
                const auto& homes=side?literal_homes_b:literal_homes_a;
                changed.nomination.origin=homes[n%homes.size()];
                changed.self.body.hp=static_cast<float>(1+(n%3));changed.self.gold=static_cast<std::uint32_t>(n);
                changed.self.body_revision=changed.nomination.body;changed.self.epoch=static_cast<std::uint32_t>(changed.nomination.epoch);
                changed.self.spells[0].damage=1+static_cast<float>(n%2);changed.self.cooldown[0]=static_cast<std::uint32_t>(n%9);
                const auto before=menu.progress().units;request(changed);
                check(menu.progress().units==before,"request reuse cannot erase or invent admitted work");
            }
            const auto before=menu.progress().units;menu.advance(1);
            check(menu.last_work()<=1&&menu.progress().units>=before,"one retained Menu unit stays bounded and monotone");
            if(!menu.last_work()){out.progress_stopped=true;break;}
        }
        out.target=menu.first_ready(group,initial.self);
        out.nominations[out.groups_examined++]={group,static_cast<int>(menu.status()),static_cast<int>(menu.reason()),
            difference(menu.progress(),before_group),static_cast<bool>(out.target),menu.complete()};
        if(out.target){out.first_ready_group=group;break;}
        if(menu.complete())++out.completed_no_ready_groups;
        else break; // typed failure, capacity/no-progress or total-unit cutoff
        group=nearest(*data,initial.nomination.origin,examined);
    }
    out.ready=static_cast<bool>(out.target);out.first=menu.progress();
    out.ready_count=menu.ready_count();out.families=menu.families().size();out.complete=menu.complete();
    out.status=static_cast<int>(menu.status());out.reason=static_cast<int>(menu.reason());
    out.bounded=!out.ready&&out.groups_examined<data->catalogue().size()&&menu.status()!=sm::Status::Unsupported;
    if(out.target) {
        const auto original=out.target->stamp();
        out.unchanged_stamp=original.body==initial.nomination.body&&original.query==initial.nomination.query&&
            original.epoch==initial.nomination.epoch&&original.origin==initial.nomination.origin;
        out.future_only=!out.target->has_initial()&&!out.target->result().legs[0].complete&&out.target->result().legs[0].points.empty();
        check(out.unchanged_stamp&&out.future_only,"first publication preserves its original FutureOnly nomination stamp");
        // Optional enrichment is requested only AFTER the first real Ready.
        auto extra=initial;extra.canonical_group=nearest(*data,initial.nomination.origin,examined);
        if(extra.canonical_group>=0) {
            const auto response=menu.request(extra);out.optional_queued=response.queued!=0||response.deferred!=0;
        }
        const auto before=menu.progress().units;menu.advance(64);out.post_units=menu.progress().units-before;
        const auto retained=menu.first_ready(out.first_ready_group,initial.self);
        check(retained==out.target&&retained->stamp().epoch==original.epoch&&retained->stamp().origin==original.origin,
            "small optional enrichment cannot overwrite the earlier pinned target or retag its original date/origin");
    }
    return out;
}
bool same_progress(const sm::Progress& a,const sm::Progress& b) {
    return a.units==b.units&&a.scanned_cells==b.scanned_cells&&a.builder_units==b.builder_units&&
        a.started==b.started&&a.failed==b.failed&&a.published==b.published;
}
void characterize(unsigned side,const std::shared_ptr<const sg::StaticGeometry>& data,const LiteralKit& kit) {
    const auto pose=side?literal_origin_b:literal_origin_a;const auto group=nearest(*data,pose);
    check(group>=0,"nearest-member geometric nomination exists");if(group<0)return;
    const auto baseline=measure(data,kit,side,group,false);const auto repeated=measure(data,kit,side,group,true);
    bool same=baseline.groups_examined==repeated.groups_examined&&baseline.completed_no_ready_groups==repeated.completed_no_ready_groups&&
        baseline.first_ready_group==repeated.first_ready_group;
    for(std::size_t i=0;same&&i<baseline.groups_examined;++i) {
        const auto& a=baseline.nominations[i];const auto& b=repeated.nominations[i];
        same=a.group==b.group&&a.status==b.status&&a.reason==b.reason&&a.ready==b.ready&&a.complete==b.complete&&same_progress(a.work,b.work);
    }
    check(same&&baseline.ready==repeated.ready&&baseline.status==repeated.status&&same_progress(baseline.first,repeated.first),
        "changed date/body/origin requests preserve the exact first-Ready structural work and outcome");
    const int reported_group=baseline.ready?baseline.first_ready_group:group;
    // SC3 transport: <=4 signature points and <=4 prefix rows, with explicit
    // omissions. All measured totals and internal equality still cover the
    // entire calculation. This keeps successful stdout below the runner tail.
    std::printf("SC3 {\"k\":\"m\",\"s\":\"%c\",\"kit\":\"%s\",\"g\":%d,\"x\":[",
        side?'B':'A',kit.name,reported_group);
    auto members=data->catalogue()[static_cast<std::size_t>(reported_group)].members;
    std::sort(members.begin(),members.end(),[](const auto& a,const auto& b){return std::pair{a.pos.y,a.pos.x}<std::pair{b.pos.y,b.pos.x};});
    const auto printed_members=std::min<std::size_t>(members.size(),4);
    for(std::size_t n=0;n<printed_members;++n)std::printf("%s[%d,%d]",n?",":"",members[n].pos.x,members[n].pos.y);
    const auto printed_prefix=std::min<std::size_t>(baseline.groups_examined,4);
    std::printf("],\"mc\":[%zu,%zu],\"n\":[%d,%d,%zu,%zu],\"po\":%zu,\"p\":[",
        members.size(),members.size()-printed_members,group,baseline.first_ready_group,baseline.groups_examined,
        baseline.completed_no_ready_groups,baseline.groups_examined-printed_prefix);
    for(std::size_t i=0;i<printed_prefix;++i) {
        const auto& row=baseline.nominations[i];
        std::printf("%s[%d,%llu,%llu,%llu,%llu,%llu,%d,%d,%u]",
            i?",":"",row.group,
            static_cast<unsigned long long>(row.work.units),static_cast<unsigned long long>(row.work.scanned_cells),
            static_cast<unsigned long long>(row.work.builder_units),static_cast<unsigned long long>(row.work.started),
            static_cast<unsigned long long>(row.work.failed),row.status,row.reason,(row.ready?1u:0u)|(row.complete?2u:0u));
    }
    // f bits: Ready, bounded, full internal prefix equality, before all groups,
    // no-progress, complete-at-first, original stamp, FutureOnly, enrichment.
    const unsigned flags=(baseline.ready?1u:0u)|(baseline.bounded?2u:0u)|(same?4u:0u)|
        (baseline.ready&&baseline.groups_examined<data->catalogue().size()?8u:0u)|(baseline.progress_stopped?16u:0u)|
        (baseline.complete?32u:0u)|(repeated.unchanged_stamp?64u:0u)|(repeated.future_only?128u:0u)|(repeated.optional_queued?256u:0u);
    std::printf("],\"v\":[%d,%d],\"f\":%u,\"w\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu],\"post\":%llu}\n",
        baseline.status,baseline.reason,flags,
        static_cast<unsigned long long>(baseline.first.units),static_cast<unsigned long long>(baseline.first.scanned_cells),
        static_cast<unsigned long long>(baseline.first.builder_units),static_cast<unsigned long long>(baseline.first.started),
        static_cast<unsigned long long>(baseline.first.failed),static_cast<unsigned long long>(baseline.first.published),
        static_cast<unsigned long long>(repeated.first.units),static_cast<unsigned long long>(repeated.post_units));
}
}
int main() {
    std::printf("SC3 {\"k\":\"b\",\"m\":\"%s\",\"c\":\"%s\",\"z\":[%d,%d,%zu,%zu]}\n",
        literal_map_sha,literal_catalog_sha,literal_width,literal_height,literal_groups.size(),literal_members.size());
    for(unsigned side=0;side<2;++side) {
        const auto built=compile(side);if(!built.data)continue;
        for(const auto& kit:literal_kits)characterize(side,built.data,kit);
    }
    std::printf("assignment static characterization: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
