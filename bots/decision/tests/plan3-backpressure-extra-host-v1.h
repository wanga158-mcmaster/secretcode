#pragma once
// Explicitly modeled 8x8 map, with two public Rows wolf-group compositions and
// one Rows gold-group composition. Placements/live state are fixture assumptions.
namespace {
std::array<std::uint32_t,3> raw_gems{66,66,0},raw_gold{270,270,105};
unsigned buys=0,submissions=0,recalls=0,actions=0;
bool all_gold_map=false;
bool bp_hidden_collision=false;
int cost_zero_item=-1;
constexpr int bp_x[9]={4,4,4,6,6,6,1,1,1};
constexpr int bp_y[9]={2,3,4,2,3,4,4,5,6};
constexpr int bp_type[9]={0,0,0,0,0,0,3,3,3};
int bp_kind(unsigned id) {return all_gold_map?3:bp_type[id];}
int bp_key(unsigned group) {int key=64;for(unsigned i=group*3;i<group*3+3;++i)key=std::min(key,bp_axis(bp_y[i])*8+bp_axis(bp_x[i]));return key;}
std::uint64_t bp_camp_mask() {
    std::uint64_t result=0;
    for(unsigned i=0;i<9;++i)if(group_state[i/3]==1 && ((visible[0]>>(bp_axis(bp_y[i])*8+bp_axis(bp_x[i])))&1u))result|=std::uint64_t{1}<<(bp_axis(bp_y[i])*8+bp_axis(bp_x[i]));
    return result;
}
}
namespace cambc::detail { extern "C" {
std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t* out,std::int32_t x,std::int32_t y,float radius) {
    record("sdk_Bitboard_DrawChebyshev");verify(std::isfinite(radius) && radius>=0);*out=0;
    for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx)if(std::max(std::abs(xx-x),std::abs(yy-y))<=radius)set(out,xx,yy);
    return 0;
}
std::int32_t sdk_camp_design(std::int32_t type,camp_design* out) {
    record("sdk_camp_design");verify(type>=0 && type<5);static_assert(sizeof(*out)==124);
    std::memcpy(out,bp_config::camps[unsigned(type)].data(),sizeof(*out));return 0;
}
std::int32_t sdk_map_camp_info(std::uint32_t id,camp_info* out) {record("sdk_map_camp_info");verify(id<9);*out={bp_axis(bp_x[id]),bp_axis(bp_y[id]),bp_kind(id)};return 0;}
std::int32_t sdk_map_camp_group_of(std::uint32_t id,std::uint32_t* out) {record("sdk_map_camp_group_of");verify(id<9);*out=id/3;return 0;}
std::int32_t sdk_camp_at(std::int32_t x,std::int32_t y,std::uint32_t* out) {
    record("sdk_camp_at");for(unsigned i=0;i<9;++i)if(bp_axis(bp_x[i])==x && bp_axis(bp_y[i])==y && group_state[i/3]==1 && ((visible[0]>>(y*8+x))&1u)){*out=i;return 0;}
    return static_cast<int>(error_code::api_not_found);
}
std::int32_t sdk_camp_state(std::uint32_t id,std::uint32_t* out) {record("sdk_camp_state");verify(id<9 && ((visible[0]>>(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])))&1u));*out=group_state[id/3]==1?1:0;return 0;}
std::int32_t sdk_camp(std::uint32_t id,camp_state* out) {
    record("sdk_camp");verify(id<9 && group_state[id/3]==1 && ((visible[0]>>(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])))&1u));
    camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));
    *out={};out->x=bp_axis(bp_x[id]);out->y=bp_axis(bp_y[id]);out->health=out->max_health=design.base_stats.base_health;out->strength=design.base_stats.base_strength;out->focus=design.base_stats.base_focus;return 0;
}
std::int32_t sdk_map_camp_layout(std::uint32_t* out,std::uint32_t capacity,std::uint32_t* count) {
    record("sdk_map_camp_layout");constexpr unsigned needed=3*(5+3*4);*count=needed;
    if(capacity<needed)return 0;unsigned at=0;
    for(unsigned g=0;g<3;++g) {
        out[at++]=g;out[at++]=raw_gold[g];out[at++]=raw_gems[g];out[at++]=40;out[at++]=3;
        for(unsigned j=0;j<3;++j){const auto id=g*3+j;out[at++]=id;out[at++]=bp_kind(id);out[at++]=bp_axis(bp_x[id]);out[at++]=bp_axis(bp_y[id]);}
    }
    verify(at==needed);return 0;
}
std::int32_t sdk_num_allies(std::uint32_t* out) {record("sdk_num_allies");*out=3;return 0;}
std::int32_t sdk_num_enemies(std::uint32_t* out) {record("sdk_num_enemies");*out=0;return 0;}
std::int32_t sdk_allies(std::uint32_t* out,std::uint32_t capacity,std::uint32_t* count) {record("sdk_allies");verify(capacity>=3);out[0]=0;out[1]=1;out[2]=2;*count=3;return 0;}
std::int32_t sdk_enemies(std::uint32_t*,std::uint32_t,std::uint32_t* count) {record("sdk_enemies");*count=0;return 0;}
std::int32_t sdk_champion_can_shop(std::uint32_t id,std::int32_t,std::int32_t* out) {record("sdk_champion_can_shop");verify(id==0);*out=1;return 0;}
std::int32_t sdk_buy_item(std::uint32_t type,std::uint32_t count) {
    record("sdk_buy_item");verify(type<items.size() && count>0);item_info info{};
    std::memcpy(&info,bp_config::items[type].data(),sizeof(info));verify(std::uint64_t(info.cost)*count<=gold);
    gold-=info.cost*count;items[type]+=count;++buys;++actions;return 0;
}
std::int32_t sdk_submit_gems(std::int32_t x,std::int32_t y) {record("sdk_submit_gems");verify(x==bp_axis(2) && y==bp_axis(3) && bag>0);++submissions;++actions;actor.state|=1<<2;return 0;}
std::int32_t sdk_recall() {record("sdk_recall");++recalls;++actions;actor.state|=1<<1;return 0;}
}}
