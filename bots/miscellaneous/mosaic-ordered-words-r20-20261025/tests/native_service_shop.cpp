#include "../src/native_service_execution.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace native_service=mosaic::native_service;
namespace ns=native_service;
struct Pure {
#define MOSAIC_NATIVE_SERVICE_FACTS_PURE_ONLY
#include "../src/native_service_facts.h"
#undef MOSAIC_NATIVE_SERVICE_FACTS_PURE_ONLY
#define MOSAIC_NATIVE_SERVICE_SHOP_PURE_ONLY
#include "../src/native_service_shop.h"
#undef MOSAIC_NATIVE_SERVICE_SHOP_PURE_ONLY
};
static unsigned checks;
static void check(bool okay,int line){++checks;if(!okay){std::fprintf(stderr,"native shop line%d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
struct Host {
    ns::Facts facts{};std::uint64_t fuel{100000};std::uint32_t money{500};
    Pure::NativeShopInventory items{};unsigned permissions{},metadata_reads{},inventories{},gold_reads{},buys{},combines{},invalidations{};
    bool allowed{true};int error{};unsigned bad_metadata{99};
    static std::uint64_t clock(void* object){return static_cast<Host*>(object)->fuel;}
    ns::Epoch epoch()const{return facts.epoch;}
    unsigned item(unsigned index)const{return index==0?101:index==1?202:303;}
    int permission(unsigned,std::int32_t& out){++permissions;fuel-=50;out=allowed;return 0;}
    int metadata(unsigned index,Pure::NativeShopMetadata& out){
        ++metadata_reads;fuel-=50;
        out=index==2?Pure::NativeShopMetadata{150,0,0,4,0.3f,false}:
            Pure::NativeShopMetadata{30,5,index==0?303u:404u,0,0,true};
        if(index==bad_metadata)out.combine_count=0;return 0;
    }
    int inventory(unsigned,Pure::NativeShopInventory& out){++inventories;fuel-=50;out=items;return 0;}
    int gold(unsigned,std::uint32_t& out){++gold_reads;fuel-=50;out=money;return 0;}
    int buy(unsigned id,unsigned count){
        ++buys;fuel-=100;if(error)return error;
        money-=30*count;
        auto& shards=id==101?items.jungle_shards:items.power_shards;shards+=count;
        if(shards>=5){shards-=5;++combines;if(id==101)++items.jungle_emblems;}
        return 0;
    }
    void investment(const Pure::NativeShopInventory&,const Pure::NativeShopMetadata&){}
    void invalidate(){++invalidations;facts.attempted(ns::EffectScope::self_only);}
    void denied(bool hard){if(hard)facts.reject();}
    static void check(int code){if(code)throw std::runtime_error("error fake buy");}
    void fresh(int round,bool gold_actual=false,bool alive=true){
        fuel=100000;facts.begin(round);facts.self={};auto& self=facts.self;self.epoch=facts.epoch;self.id=91;
        self.alive=alive;self.raw_m=self.raw_a=self.raw_c=true;self.pose={1,1};
        self.body_actual=self.restrictions_actual=self.buffs_actual=self.channel_actual=true;
        self.gold_actual=gold_actual;self.gold=money;
    }
};
static auto run(Host& host,Pure::NativeShopState& state){
    ns::Budget budget(&host,Host::clock,host.fuel);budget.reserve(1234);
    const auto result=Pure::NativeShopLogic::step(host,budget,state,host.facts.self);
    CHECK(budget.held_tail==1234);return result;
}
int main(){
    Host host;Pure::NativeShopState state;host.fresh(1);
    auto result=run(host,state);
    CHECK(result.applied&&result.attempted&&result.owned()&&result.prepared);
    CHECK(result.item==101&&result.count==5&&host.combines==1&&host.buys==1);
    CHECK(host.permissions==1&&host.metadata_reads==3&&host.inventories==1&&host.gold_reads==1);
    CHECK(state.cursor==3&&state.last_attempt_round==1&&host.money==350&&host.items.jungle_emblems==1);
    CHECK(!host.facts.self.body_actual&&host.invalidations==1);
    host.fresh(1);result=run(host,state);CHECK(result.status==Pure::NativeShopStatus::already_attempted&&!result.owned());
    CHECK(host.buys==1&&host.inventories==1);
    host.fresh(2,true);result=run(host,state);
    CHECK(result.applied&&result.item==202&&result.count==5&&!result.prepared);
    CHECK(host.metadata_reads==3&&host.inventories==2&&host.gold_reads==1&&host.combines==2);
    CHECK(host.money==200&&host.buys==2&&host.invalidations==2);
    // Fresh inventory is authoritative; do not reuse a previous shard count.
    host.items.power_shards=4;host.fresh(3);result=run(host,state);
    CHECK(result.applied&&result.count==1&&host.combines==3&&host.money==170);
    host.money=29;host.fresh(4);result=run(host,state);
    CHECK(result.status==Pure::NativeShopStatus::unaffordable&&!result.owned()&&!result.attempted);
    CHECK(host.facts.self.actual(host.epoch())&&host.buys==3);
    host.money=500;host.allowed=false;host.fresh(5,false,false);
    const auto inventory_before=host.inventories;result=run(host,state);
    CHECK(result.status==Pure::NativeShopStatus::blocked&&!result.owned());CHECK(host.inventories==inventory_before);
    host.allowed=true;host.fresh(6,false,false);result=run(host,state);
    CHECK(result.applied&&!host.facts.self.alive&&host.buys==4);
    // A rejected SDK command still invalidates self and cannot be repeated.
    host.error=52;host.fresh(7);bool threw=false;
    try{(void)run(host,state);}catch(const std::runtime_error&){threw=true;}
    CHECK(threw&&state.last_attempt_round==7&&!host.facts.self.body_actual);
    host.error=0;host.fresh(7);result=run(host,state);CHECK(result.status==Pure::NativeShopStatus::already_attempted);
    // Optional allowance refusal does not erase the retained fresh body.
    host.fresh(8);ns::Budget limited(&host,Host::clock,host.fuel);limited.reserve(95000);
    const auto reads=host.permissions;result=Pure::NativeShopLogic::step(host,limited,state,host.facts.self);
    CHECK(result.status==Pure::NativeShopStatus::unfunded&&!result.owned());
    CHECK(!limited.poisoned&&host.facts.self.actual(host.epoch())&&host.permissions==reads&&limited.held_tail==95000);
    host.fresh(9);ns::Budget stale(&host,Host::clock,host.fuel);host.fuel=~std::uint64_t{};
    result=Pure::NativeShopLogic::step(host,stale,state,host.facts.self);
    CHECK(stale.poisoned&&host.epoch().round==-1&&!result.attempted);
    Host malformed;Pure::NativeShopState bad;malformed.bad_metadata=0;malformed.fresh(10);
    result=run(malformed,bad);CHECK(result.status==Pure::NativeShopStatus::invalid&&!result.attempted);
    CHECK(bad.cursor==3&&malformed.buys==0);
    std::printf("native-service-shop %u checks passed\n",checks);
}
