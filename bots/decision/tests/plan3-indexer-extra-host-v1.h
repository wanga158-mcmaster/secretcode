#pragma once
// Explicitly modeled8x8 map. Normal scene uses two Rows-shaped three-wolf
// groups and one three-gold-cluster group. Dense scene uses20 one-wolf groups
// with declared public reward22gems/90gold and regeneration40. Geometry, live
// state and dense group rewards are fixture assumptions, not captured/native
// map metadata; all unit designs/settings/catalogues retain exact public bytes.
// Both self ability slots have one modeled remaining cooldown tick. No ability
// use, damage resolution or monster phase is simulated by this host.
namespace {
std::array<std::uint32_t,20> raw_gems{66,66,0},raw_gold{270,270,105};
unsigned buys=0,submissions=0,recalls=0,actions=0;
bool all_gold_map=false;
bool monolith_map=false;
bool bp_hidden_collision=false;
int cost_zero_item=-1;
const unsigned* indexer_terminal_counter=nullptr;
unsigned indexer_drop_after=0;
constexpr int bp_x[9]={4,4,4,6,6,6,1,1,1};
constexpr int bp_y[9]={2,3,4,2,3,4,4,5,6};
constexpr int bp_type[9]={0,0,0,0,0,0,3,3,3};
constexpr int dense_x[20]={1,4,5,6,7,4,5,6,7,4,5,6,7,4,5,6,7,4,5,7};
constexpr int dense_y[20]={0,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6};
bool dense_layout(){return std::getenv("P3_IDX_DENSE")!=nullptr;}
unsigned bp_groups(){return dense_layout()?20u:3u;}
unsigned bp_members(){return dense_layout()?1u:3u;}
unsigned bp_count(){return bp_groups()*bp_members();}
unsigned bp_group(unsigned id){return id/bp_members();}
int bp_cx(unsigned id){return dense_layout()?dense_x[id]:bp_x[id];}
int bp_cy(unsigned id){return dense_layout()?dense_y[id]:bp_y[id];}
int bp_kind(unsigned id){return all_gold_map?3:monolith_map?(bp_group(id)+1<bp_groups()?4:3):dense_layout()?0:bp_type[id];}
int bp_key(unsigned group){int key=64;for(unsigned j=0;j<bp_members();++j){const auto id=group*bp_members()+j;key=std::min(key,bp_axis(bp_cy(id))*8+bp_axis(bp_cx(id)));}return key;}
std::uint64_t bp_camp_mask(){
 std::uint64_t result=0;
 for(unsigned i=0;i<bp_count();++i)if(group_state[bp_group(i)]==1 && ((visible[0]>>(bp_axis(bp_cy(i))*8+bp_axis(bp_cx(i))))&1u))result|=std::uint64_t{1}<<(bp_axis(bp_cy(i))*8+bp_axis(bp_cx(i)));
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
std::int32_t sdk_map_camp_info(std::uint32_t id,camp_info* out) {record("sdk_map_camp_info");verify(id<bp_count());*out={bp_axis(bp_cx(id)),bp_axis(bp_cy(id)),bp_kind(id)};return 0;}
std::int32_t sdk_map_camp_group_of(std::uint32_t id,std::uint32_t* out) {record("sdk_map_camp_group_of");verify(id<bp_count());*out=bp_group(id);return 0;}
std::int32_t sdk_camp_at(std::int32_t x,std::int32_t y,std::uint32_t* out) {
    record("sdk_camp_at");for(unsigned i=0;i<bp_count();++i)if(bp_axis(bp_cx(i))==x && bp_axis(bp_cy(i))==y && group_state[bp_group(i)]==1 && ((visible[0]>>(y*8+x))&1u)){*out=i;return 0;}
    return static_cast<int>(error_code::api_not_found);
}
std::int32_t sdk_camp_state(std::uint32_t id,std::uint32_t* out) {record("sdk_camp_state");verify(id<bp_count() && ((visible[0]>>(bp_axis(bp_cy(id))*8+bp_axis(bp_cx(id))))&1u));*out=group_state[bp_group(id)]==1?1:0;return 0;}
std::int32_t sdk_camp(std::uint32_t id,camp_state* out) {
    record("sdk_camp");verify(id<bp_count() && group_state[bp_group(id)]==1 && ((visible[0]>>(bp_axis(bp_cy(id))*8+bp_axis(bp_cx(id))))&1u));
    camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));
    *out={};out->x=bp_axis(bp_cx(id));out->y=bp_axis(bp_cy(id));out->health=out->max_health=design.base_stats.base_health;out->strength=design.base_stats.base_strength;out->focus=design.base_stats.base_focus;return 0;
}
std::int32_t sdk_map_camp_layout(std::uint32_t* out,std::uint32_t capacity,std::uint32_t* count) {
    record("sdk_map_camp_layout");const unsigned needed=bp_groups()*(5+bp_members()*4);*count=needed;
    if(capacity<needed)return 0;unsigned at=0;
    for(unsigned g=0;g<bp_groups();++g) {
        out[at++]=g;out[at++]=raw_gold[g];out[at++]=raw_gems[g];out[at++]=40;out[at++]=bp_members();
        for(unsigned j=0;j<bp_members();++j){const auto id=g*bp_members()+j;out[at++]=id;out[at++]=bp_kind(id);out[at++]=bp_axis(bp_cx(id));out[at++]=bp_axis(bp_cy(id));}
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
std::int32_t sdk_champion_cooldown(std::uint32_t id,std::uint32_t slot,std::int32_t* out) {
    record("sdk_champion_cooldown");verify(id==0 && slot<2);*out=1;return 0;
}
// Acknowledges one legal modeled camp attack. This fixture does not simulate
// combat damage or claim an engine transition after the acknowledged action.
std::int32_t sdk_attack(std::uint32_t kind,std::uint32_t id) {
    record("sdk_attack");verify(kind==2 && id<bp_count() && (actor.state&1) && (actor.state&(1<<4)) &&
        !(actor.state&((1<<1)|(1<<2)|(1<<12))) && actor.attack_range==1 && group_state[bp_group(id)]==1);
    const int x=bp_axis(bp_cx(id)),y=bp_axis(bp_cy(id));
    verify(((visible[0]>>(y*8+x))&1u) && std::max(std::abs(x-actor.x),std::abs(y-actor.y))<=actor.attack_range);
    std::printf("IDX_ACTION {\"kind\":\"attack\",\"targetKind\":%u,\"targetId\":%u,\"actor\":[%d,%d],\"target\":[%d,%d],\"attackRange\":%d,\"modeledAcknowledgmentOnly\":true}\n",kind,id,actor.x,actor.y,x,y,actor.attack_range);
    actor.state&=~(1<<4);++actions;return 0;
}
}}
