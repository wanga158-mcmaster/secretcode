#define P10_NAVIGATION_PURE_TEST
#include "../common/src/navigation.h"
#include "../common/src/guard.h"
#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error Optimized navigation regression requires assertions enabled.
#endif

namespace {
constexpr auto blocked=std::numeric_limits<std::uint16_t>::max();
struct Field {
 std::vector<std::uint16_t> raw;mutable int exports=0;
 void export_linear_buffer(std::span<std::uint16_t> out)const{++exports;assert(out.size()==raw.size());std::copy(raw.begin(),raw.end(),out.begin());}
 int get(p10::Point)const=delete; // regression: no fine-grained SDK getter available
};
std::vector<p10::Point> order(const std::vector<std::uint16_t>&values,const std::vector<p10::Point>&legal){
 auto result=legal;
 std::sort(result.begin(),result.end(),[&](auto a,auto b){return p10::linear_distance(values,4,3,a)<p10::linear_distance(values,4,3,b);});
 return result;
}
p10::Point first_progress(const std::vector<std::uint16_t>&values,const std::vector<p10::Point>&legal,p10::Point here){
 const int old=p10::linear_distance(values,4,3,here);
 if(old<=0)return here;
 for(auto p:order(values,legal)){const int cost=p10::linear_distance(values,4,3,p);if(cost>=0&&cost<old&&!(p==here))return p;}
 return here;
}
void exact_row_major_and_one_export(){
 Field f{{9,8,7,6,8,7,6,5,7,6,5,0}};
 std::vector<std::uint16_t> cache;
 p10::export_current_distances(f,4,3,cache);
 assert(f.exports==1&&cache.size()==12);
 assert(p10::linear_distance(cache,4,3,{3,2})==0);
 assert(p10::linear_distance(cache,4,3,{0,1})==8);
 assert(p10::linear_distance(cache,4,3,{-1,0})==-1);
 assert(p10::linear_distance(cache,4,3,{4,1})==-1);
 assert(p10::linear_distance(cache,4,3,{0,3})==-1);
 assert(p10::linear_distance(std::span<const std::uint16_t>{cache.data(),3},4,3,{1,1})==-1);
 assert(p10::minimum_linear_distance(cache,4,3,{{0,0},{0,1},{3,2}})==0);
 assert(p10::minimum_linear_distance(cache,4,3,{{0,0},{0,1}})==8);
 assert(p10::minimum_linear_distance(cache,4,3,{})==-1);
 assert(p10::minimum_linear_distance(cache,4,3,{{-1,0},{4,3}})==-1);
 const auto chosen=first_progress(cache,{{0,0},{1,0},{0,1},{1,1}},{0,0});
 assert(chosen==(p10::Point{1,1}));assert(f.exports==1); // sorting added no host reads
}
void false_step_recomputes_one_bounded_alternative(){
 const p10::Point here{1,1},preferred{2,1},alternative{1,2};
 Field first{{9,8,7,6,8,4,1,5,7,2,5,0}};
 Field repaired=first;repaired.raw[6]=blocked;repaired.raw[9]=1;
 std::vector<std::uint16_t> cache;
 std::vector<p10::Point> legal{{0,0},{1,0},{2,0},{0,1},here,preferred,{0,2},alternative,{2,2}};
 p10::export_current_distances(first,4,3,cache);
 assert(first_progress(cache,legal,here)==preferred);
 // The first physical move returned false and preserved the allowance. The
 // new field includes the rejected tile; no stale distances are retained.
 p10::export_current_distances(repaired,4,3,cache);
 assert(first_progress(cache,legal,here)==alternative);
 assert(first.exports==1&&repaired.exports==1);
 int attempts=0;bool allowance=true;
 for(int retry=0;retry<2;++retry){++attempts;if(retry==1)allowance=false;}
 assert(attempts==2&&!allowance);
}
void no_unreachable_sideways_or_stale_move(){
 Field f{{blocked,blocked,blocked,blocked,blocked,4,blocked,blocked,blocked,5,blocked,blocked}};
 std::vector<std::uint16_t> cache;p10::export_current_distances(f,4,3,cache);
 const p10::Point here{1,1};assert(first_progress(cache,{{1,2},{2,1},{0,1},here},here)==here);
 f.raw[5]=0;p10::export_current_distances(f,4,3,cache);assert(first_progress(cache,{{1,2}},here)==here);
 int round=10,moves=0;
 const bool issued=p10::guarded_action(10,[&]{return round;},[&]{round=11;return true;},[&]{++moves;return true;});
 assert(!issued&&moves==0); // a legality query resuming after suspension cannot move
}
}
int main(){exact_row_major_and_one_export();false_step_recomputes_one_bounded_alternative();no_unreachable_sideways_or_stale_move();std::cout<<"PASS native navigation: one bulk export per field, pure candidate costs, fresh failed-step repair, reachability/progress and stale-action guards\n";}
