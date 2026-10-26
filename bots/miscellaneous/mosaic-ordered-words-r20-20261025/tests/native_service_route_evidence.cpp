#include "native_service_route_evidence.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace N=mosaic::native_service;
using Update=N::RouteUpdate;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;
    if(!value){std::fprintf(stderr,"route evidence check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

static constinit N::RouteCell constant_cell{};
static_assert(std::is_trivially_copyable_v<N::RouteCell>);
static_assert(std::is_standard_layout_v<N::RouteCell>);
static_assert(noexcept(constant_cell.observe({},false)));
static_assert(noexcept(constant_cell.invalidate_before({})));
static constexpr bool compile_time_merge(){
    N::RouteCell cell;
    return cell.observe({1,1,0},true)==Update::changed && cell.excluded() && cell.known() &&
        cell.observe({1,1,1},false)==Update::changed && !cell.excluded() && cell.known() &&
        cell.observe({1,1,0},true)==Update::stale && !cell.excluded();
}
static_assert(compile_time_merge());

static void defaults_and_invalid_epochs(){
    ++groups;
    N::RouteCell cell;
    CHECK(!cell.known()&&!cell.excluded()&&!cell.conflicted());
    CHECK(cell.seen_epoch().round==-1&&cell.invalidation_epoch().round==-1);
    CHECK(!constant_cell.known()&&!constant_cell.excluded());
    CHECK(cell.observe({-1,0,0},true)==Update::invalid);
    CHECK(cell.observe({-2,9,9},false)==Update::invalid);
    CHECK(cell.invalidate_before({-1,9,9})==Update::invalid);
    CHECK(!cell.known()&&!cell.excluded()&&cell.seen_epoch().round==-1);
    CHECK(cell.observe({0,0,0},false)==Update::unchanged);
    CHECK(cell.known()&&!cell.excluded()&&cell.seen_epoch()==N::Epoch{0,0,0});
    CHECK(cell.observe({-1,99,99},true)==Update::invalid);
    CHECK(cell.known()&&!cell.excluded()&&cell.seen_epoch()==N::Epoch{0,0,0});
}

static void stale_bulk_cannot_replace_new_empty(){
    ++groups;
    N::RouteCell cell;
    const N::Epoch mask{17,3,0},dead{18,4,1},self{18,5,2};
    CHECK(cell.observe(mask,true)==Update::changed);
    CHECK(cell.known()&&cell.excluded()&&cell.seen_epoch()==mask);
    CHECK(cell.observe(dead,false)==Update::changed);
    CHECK(cell.known()&&!cell.excluded());
    CHECK(cell.observe(mask,true)==Update::stale);
    CHECK(cell.seen_epoch()==dead&&!cell.excluded());
    CHECK(cell.observe(self,false)==Update::unchanged);
    CHECK(cell.seen_epoch()==self&&cell.known());
    CHECK(cell.observe(dead,true)==Update::stale);
    CHECK(cell.seen_epoch()==self&&!cell.excluded());
    CHECK(cell.observe({19,0,0},true)==Update::changed);
    CHECK(cell.known()&&cell.excluded()&&cell.seen_epoch()==N::Epoch{19,0,0});
}

static void revisions_and_false_attempts_order_evidence(){
    ++groups;
    N::RouteCell cell;
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    CHECK(cell.observe({25,7,3},true)==Update::changed);
    // A FALSE attempt changes attempt even when world revision is unchanged.
    CHECK(cell.observe({25,7,4},false)==Update::changed);
    CHECK(cell.observe({25,7,3},true)==Update::stale&&!cell.excluded());
    CHECK(cell.observe({25,8,0},true)==Update::changed);
    CHECK(cell.observe({25,7,maximum},false)==Update::stale&&cell.excluded());
    CHECK(cell.observe({24,maximum,maximum},false)==Update::stale);
    CHECK(cell.observe({26,0,0},false)==Update::changed);
    CHECK(cell.seen_epoch()==N::Epoch{26,0,0}&&cell.known()&&!cell.excluded());
}

static void equal_epoch_conflicts_are_explicit_and_sticky(){
    ++groups;
    const N::Epoch epoch{30,2,1};
    for(const bool first:{false,true}){
        N::RouteCell cell;
        CHECK(cell.observe(epoch,first)==(first?Update::changed:Update::unchanged));
        CHECK(cell.observe(epoch,first)==Update::unchanged&&cell.known());
        CHECK(cell.observe(epoch,!first)==Update::conflict);
        CHECK(cell.conflicted()&&cell.excluded()&&!cell.known()&&cell.seen_epoch()==epoch);
        CHECK(cell.observe(epoch,false)==Update::conflict);
        CHECK(cell.observe(epoch,true)==Update::conflict);
        CHECK(cell.invalidate_before(epoch)==Update::unchanged&&cell.conflicted());
        CHECK(cell.observe({30,2,0},false)==Update::stale&&cell.conflicted());
        CHECK(cell.observe({30,2,2},false)==Update::changed);
        CHECK(cell.known()&&!cell.conflicted()&&!cell.excluded());
        CHECK(cell.observe(epoch,true)==Update::stale&&!cell.excluded());
    }
    N::RouteCell cell;
    CHECK(cell.observe(epoch,true)==Update::changed);
    CHECK(cell.observe(epoch,false)==Update::conflict);
    CHECK(cell.observe({30,3,0},true)==Update::unchanged);
    CHECK(cell.known()&&cell.excluded()&&!cell.conflicted());
}

static void lifecycle_retires_nominations_without_empty_truth(){
    ++groups;
    N::RouteCell cell;
    const N::Epoch old{40,1,0},boundary{41,2,0};
    CHECK(cell.observe(old,true)==Update::changed);
    CHECK(cell.invalidate_before(old)==Update::unchanged&&cell.known()&&cell.excluded());
    CHECK(cell.invalidate_before(boundary)==Update::changed);
    CHECK(!cell.known()&&!cell.excluded()&&!cell.conflicted());
    CHECK(cell.seen_epoch()==old&&cell.invalidation_epoch()==boundary);
    CHECK(cell.observe({40,99,99},true)==Update::stale&&!cell.excluded());
    CHECK(cell.observe(old,false)==Update::stale&&!cell.known());
    CHECK(cell.invalidate_before(old)==Update::stale&&cell.invalidation_epoch()==boundary);
    CHECK(cell.observe(boundary,true)==Update::changed&&cell.known());
    CHECK(cell.invalidate_before(boundary)==Update::unchanged&&cell.excluded());
    CHECK(cell.observe({42,0,0},false)==Update::changed&&cell.known());
    CHECK(cell.invalidate_before({43,0,0})==Update::unchanged);
    CHECK(!cell.known()&&!cell.excluded()); // Revoked EMPTY also becomes unknown.
    CHECK(cell.observe({42,99,99},true)==Update::stale&&!cell.excluded());
    CHECK(cell.observe({43,0,0},false)==Update::unchanged&&cell.known());
    CHECK(cell.observe({44,0,0},true)==Update::changed);
    CHECK(cell.invalidate_before({43,1,0})==Update::unchanged);
    CHECK(cell.seen_epoch()==N::Epoch{44,0,0}&&cell.known()&&cell.excluded());
    CHECK(cell.observe({43,9,9},false)==Update::stale&&cell.excluded());
}

static void delayed_delivery_permutations_keep_latest_actual(){
    ++groups;
    // Deliver old bulk, newer actual empty, and a still newer actual body in
    // every order. Delayed old evidence must never replace the latest fact.
    constexpr std::array epochs{N::Epoch{60,1,0},N::Epoch{60,1,1},N::Epoch{61,0,0}};
    constexpr std::array values{true,false,true};
    constexpr std::array<std::array<unsigned,3>,6> orders{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    for(const auto order:orders){
        N::RouteCell cell;
        for(const auto i:order)cell.observe(epochs[i],values[i]);
        CHECK(cell.known()&&cell.excluded()&&!cell.conflicted());
        CHECK(cell.seen_epoch()==epochs[2]);
        CHECK(cell.invalidate_before({62,0,0})==Update::changed);
        for(const auto i:order)CHECK(cell.observe(epochs[i],values[i])==Update::stale);
        CHECK(!cell.known()&&!cell.excluded());
    }
}

int main(){
    defaults_and_invalid_epochs();stale_bulk_cannot_replace_new_empty();
    revisions_and_false_attempts_order_evidence();equal_epoch_conflicts_are_explicit_and_sticky();
    lifecycle_retires_nominations_without_empty_truth();delayed_delivery_permutations_keep_latest_actual();
    std::printf("PASS native_service_route_evidence: %u checks, %u groups\n",checks,groups);
}
