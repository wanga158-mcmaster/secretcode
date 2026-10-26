#pragma once
#include <algorithm>
#include <cstddef>

namespace p11 {
// Bound to one runtime's immutable altar list and a Group's immutable native
// anchor. The range is the queried cached setting, not a copied rules value.
// Unfinished/legacy construction always retains the exact formula fallback.
class AltarMinimum {
    int range_=0,minimum_=10000;
    std::size_t total_=0,cursor_=0;
    bool begun_=false,complete_=false;
public:
    void begin(int queried_range,std::size_t altar_count) {
        complete_=false;range_=queried_range;minimum_=10000;
        total_=altar_count;cursor_=0;begun_=true;
        complete_=altar_count==0;
    }
    bool ready()const{return complete_;}
    std::size_t cursor()const{return cursor_;}
    bool append(int anchor_altar_distance) {
        if(!begun_||complete_)return false;
        minimum_=std::min(minimum_,std::max(0,anchor_altar_distance-range_));
        ++cursor_;complete_=cursor_==total_;
        return true;
    }
    template<class Fallback>int get(Fallback fallback)const {
        return complete_?minimum_:fallback();
    }
};
}
