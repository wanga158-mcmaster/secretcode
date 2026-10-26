#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <span>
#include <stdexcept>
#include "../common/src/altar_minimum.h"

namespace {
unsigned checks=0;
void check(bool value){++checks;assert(value);}
int old_minimum(int range,std::span<const int>distances){
    int result=10000;
    for(const int distance:distances)result=std::min(result,std::max(0,distance-range));
    return result;
}
void sequence(int range,std::span<const int>distances){
    p11::AltarMinimum cache;
    unsigned fallbacks=0;
    const auto old=[&]{++fallbacks;return old_minimum(range,distances);};
    check(!cache.ready()&&cache.cursor()==0);
    check(!cache.append(1)&&!cache.ready()&&cache.cursor()==0);
    check(cache.get(old)==old_minimum(range,distances)&&fallbacks==1);
    cache.begin(range,distances.size());
    check(cache.cursor()==0&&cache.ready()==distances.empty());
    for(std::size_t i=0;i<distances.size();++i){
        // A partial minimum must never be returned as a complete value.
        const unsigned before=fallbacks;
        check(cache.get(old)==old_minimum(range,distances)&&fallbacks==before+1);
        check(cache.append(distances[i]));
        check(cache.cursor()==i+1&&cache.ready()==(i+1==distances.size()));
    }
    const unsigned before=fallbacks;
    check(cache.get(old)==old_minimum(range,distances)&&fallbacks==before);
    check(cache.get([]()->int{assert(false);return-1;})==old_minimum(range,distances));
    const auto finished=cache.cursor();
    check(!cache.append(0)&&cache.ready()&&cache.cursor()==finished);
    check(cache.get(old)==old_minimum(range,distances)&&fallbacks==before);
}
}

int main(){
    // Later values lower the first partial minimum. An early zero still must
    // not publish completion before the declared final altar has been added.
    sequence(2,std::array{4,20,3});
    sequence(2,std::array{0,100,3});
    sequence(0,std::array{20000,10001,9999});
    sequence(-17,std::array{0,5,80});
    sequence(-10001,std::array{0,5,80});
    sequence(10001,std::array{0,5,80});
    sequence(std::numeric_limits<int>::max(),std::array{0,1,127});
    sequence(0,std::span<const int>{});
    sequence(-17,std::span<const int>{});

    // An unready fallback is evaluated once per access and never published.
    p11::AltarMinimum legacy;
    int calls=0;
    check(legacy.get([&]{++calls;return 37;})==37&&calls==1);
    check(legacy.get([&]{++calls;return 41;})==41&&calls==2);
    try{legacy.get([]()->int{throw std::runtime_error("fixture fallback");});assert(false);}catch(const std::runtime_error&){}
    check(!legacy.ready()&&legacy.cursor()==0);
    legacy.begin(1,2);check(legacy.append(5)&&!legacy.ready());
    try{legacy.get([]()->int{throw std::runtime_error("partial fallback");});assert(false);}catch(const std::runtime_error&){}
    check(!legacy.ready()&&legacy.cursor()==1);
    check(legacy.append(2)&&legacy.ready());
    check(legacy.get([]()->int{assert(false);return-1;})==1);

    // Beginning a different (map, anchor, range) construction resets an old
    // complete result. Runtime binds each object to one immutable group.
    legacy.begin(8,1);check(!legacy.ready()&&legacy.cursor()==0);
    check(legacy.get([]{return 19;})==19);
    check(legacy.append(10)&&legacy.ready());
    check(legacy.get([]()->int{assert(false);return-1;})==2);
    legacy.begin(0,0);check(legacy.ready()&&legacy.cursor()==0);
    check(legacy.get([]()->int{assert(false);return-1;})==10000);
    std::printf("{\"checks\":%u,\"scope\":\"pure-cache-lifecycle\"}\n",checks);
}
