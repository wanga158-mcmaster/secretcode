#include "../src/native_service_stationary_index.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace N=mosaic::native_service;
using C=N::StationaryConfigure;
using A=N::StationaryAdd;
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"stationary index check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
static constinit N::StationaryIndex<> static_index{};
static_assert(std::is_trivially_copyable_v<N::StationaryIndex<>>);
static_assert(std::is_standard_layout_v<N::StationaryIndex<>>);
static_assert(noexcept(static_index.configure(1,1))&&noexcept(static_index.add({},0))&&noexcept(static_index.at({})));

static void zero_initialized_cells_and_ordinal_zero(){
    CHECK(!static_index.configured()&&static_index.width()==0&&static_index.height()==0);
    CHECK(static_index.at({0,0})==-1&&static_index.add({0,0},0)==A::unconfigured);
    CHECK(static_index.configure(128,128)==C::configured);
    for(int y=0;y<128;++y)for(int x=0;x<128;++x)CHECK(static_index.at({x,y})==-1);
    CHECK(static_index.add({0,0},0)==A::added&&static_index.at({0,0})==0);
    CHECK(static_index.add({127,127},std::numeric_limits<int>::max())==A::added);
    CHECK(static_index.at({127,127})==std::numeric_limits<int>::max());
    CHECK(static_index.at({1,0})==-1&&static_index.at({127,126})==-1);
    ++groups;
}

static void configuration_preserves_the_existing_geometry_lifetime(){
    N::StationaryIndex<64> index{};
    CHECK(index.configure(0,8)==C::invalid&&index.configure(8,-1)==C::invalid&&!index.configured());
    CHECK(index.configure(std::numeric_limits<int>::max(),std::numeric_limits<int>::max())==C::invalid);
    CHECK(index.configure(6,5)==C::configured);
    for(int y=0;y<5;++y)for(int x=0;x<6;++x)CHECK(index.add({x,y},y*6+x)==A::added);
    CHECK(index.configure(6,5)==C::retained);
    CHECK(index.configure(5,6)==C::changed&&index.width()==6&&index.height()==5);
    CHECK(index.configure(8,9)==C::invalid&&index.width()==6&&index.height()==5);
    for(int y=0;y<5;++y)for(int x=0;x<6;++x)CHECK(index.at({x,y})==y*6+x);
    CHECK(index.at({5,4})==29&&index.at({5,5})==-1);
    ++groups;
}

static void duplicates_conflicts_and_outside_points_cannot_replace_a_binding(){
    N::StationaryIndex<9> index{};CHECK(index.configure(3,3)==C::configured);
    CHECK(index.add({1,1},0)==A::added);
    CHECK(index.add({1,1},0)==A::duplicate&&index.at({1,1})==0);
    CHECK(index.add({1,1},1)==A::conflict&&index.at({1,1})==0);
    CHECK(index.add({1,1},-1)==A::invalid_ordinal&&index.at({1,1})==0);
    for(auto point:std::array<N::Point,6>{{{-1,0},{0,-1},{3,0},{0,3},
            {std::numeric_limits<int>::min(),1},{1,std::numeric_limits<int>::max()}}}){
        CHECK(index.at(point)==-1&&index.add(point,4)==A::out_of_bounds);
    }
    CHECK(index.at({0,0})==-1&&index.at({2,2})==-1);
    CHECK(index.add({2,2},2)==A::added&&index.at({2,2})==2);
    CHECK(index.configure(1,9)==C::changed&&index.at({2,2})==2&&index.at({1,1})==0);
    ++groups;
}

int main(){
    zero_initialized_cells_and_ordinal_zero();configuration_preserves_the_existing_geometry_lifetime();
    duplicates_conflicts_and_outside_points_cannot_replace_a_binding();
    std::printf("PASS native_service_stationary_index: %u checks, %u groups\n",checks,groups);
}
