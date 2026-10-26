#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::spatial {
// Conditional model premise: in pinned1.0.3, documented camp AI transitions are
// exhaustive. Public effects do not displace camps; camp range is immutable kind
// design. This is not an unconditional native-engine theorem. Every already-read
// live position must be observed against the canonical layout; contradiction
// revokes all borrowed tokens. No query below calls the SDK.
inline constexpr std::size_t capacity=160;
struct Entry {int handle{-1},x{},y{},range{};};
enum class BuildState {empty,collecting,building,ready,failed};
class Index;
struct Token {
    const Index* index{};
    std::uint64_t generation{};
    std::size_t count{};
    bool valid(std::size_t current_count) const;
};
struct Selection {
    std::array<std::size_t,capacity> values;
    std::size_t count{};
    bool complete{};
    const std::size_t* begin() const{return values.data();}
    const std::size_t* end() const{return values.data()+count;}
    std::size_t size() const{return count;}
    std::size_t operator[](std::size_t i) const{return values[i];}
};
class Index {
    struct Row {int y{};std::size_t begin{},end{};};
    enum class Phase {empty,collecting,handle_sort,unique_handles,coordinate_sort,rows,ready,failed};
    std::array<Entry,capacity> entries_;
    std::array<std::size_t,capacity> order_;
    std::array<std::size_t,capacity> scratch_;
    std::array<Row,capacity> rows_;
    std::size_t count_{},row_count_{};
    std::size_t input_count_{},width_{1},left_{},middle_{},right_{},a_{},b_{},output_{},cursor_{};
    int max_range_{};
    std::uint64_t generation_{};
    bool valid_{},source_order_{true},geometry_in_scratch_{},merge_active_{};
    Phase phase_{Phase::empty};

    void fail_build(){valid_=false;count_=0;phase_=Phase::failed;}
    void start_sort(Phase phase){phase_=phase;width_=1;left_=0;merge_active_=false;}
    bool less(std::size_t a,std::size_t b) const {
        const auto& x=entries_[a];const auto& y=entries_[b];
        if(phase_==Phase::handle_sort)return x.handle<y.handle;
        if(x.y!=y.y)return x.y<y.y;
        if(x.x!=y.x)return x.x<y.x;
        return a<b;
    }
    // One work unit emits at most one merge element, checks one adjacent handle,
    // or forms one coordinate-row member. Pass transitions only update cursors;
    // there is no hidden whole-array swap, copy, scan or allocation.
    void advance_unit(){
        for(;;) {
            if(phase_==Phase::handle_sort||phase_==Phase::coordinate_sort) {
                if(width_>=count_) {
                    if(phase_==Phase::handle_sort){phase_=Phase::unique_handles;cursor_=1;}
                    else {geometry_in_scratch_=!source_order_;phase_=Phase::rows;cursor_=0;row_count_=0;}
                    continue;
                }
                if(left_>=count_){source_order_=!source_order_;width_*=2;left_=0;merge_active_=false;continue;}
                if(!merge_active_) {
                    middle_=std::min(left_+width_,count_);right_=std::min(left_+2*width_,count_);
                    a_=left_;b_=middle_;output_=left_;merge_active_=true;
                }
                const auto& source=source_order_?order_:scratch_;
                auto& destination=source_order_?scratch_:order_;
                const bool take_a=a_<middle_&&(b_>=right_||less(source[a_],source[b_]));
                destination[output_++]=take_a?source[a_++]:source[b_++];
                if(output_==right_){left_=right_;merge_active_=false;}
                return;
            }
            if(phase_==Phase::unique_handles) {
                if(cursor_>=count_){start_sort(Phase::coordinate_sort);continue;}
                const auto& source=source_order_?order_:scratch_;
                if(entries_[source[cursor_-1]].handle==entries_[source[cursor_]].handle){fail_build();return;}
                ++cursor_;return;
            }
            if(phase_==Phase::rows) {
                if(cursor_>=count_){valid_=true;phase_=Phase::ready;return;}
                const auto& source=geometry_in_scratch_?scratch_:order_;
                const int y=entries_[source[cursor_]].y;
                if(row_count_==0||rows_[row_count_-1].y!=y)rows_[row_count_++]={y,cursor_,cursor_+1};
                else rows_[row_count_-1].end=cursor_+1;
                ++cursor_;return;
            }
            return;
        }
    }

    static std::int64_t difference(int a,int b) {
        const auto value=std::int64_t(a)-std::int64_t(b);return value<0?-value:value;
    }
    template<class Predicate> Selection rectangle(int x,int y,std::int64_t radius,Predicate&& accept) const {
        Selection out;out.complete=valid_&&radius>=0;if(!out.complete)return out;
        const auto low_x=std::int64_t(x)-radius,high_x=std::int64_t(x)+radius;
        const auto low_y=std::int64_t(y)-radius,high_y=std::int64_t(y)+radius;
        std::array<std::uint64_t,(capacity+63)/64> selected{};
        const auto& order=geometry_in_scratch_?scratch_:order_;
        auto row=std::lower_bound(rows_.begin(),rows_.begin()+row_count_,low_y,
            [](const Row& value,std::int64_t key){return std::int64_t(value.y)<key;});
        for(;row!=rows_.begin()+row_count_&&std::int64_t(row->y)<=high_y;++row) {
            auto point=std::lower_bound(order.begin()+row->begin,order.begin()+row->end,low_x,
                [&](std::size_t i,std::int64_t key){return std::int64_t(entries_[i].x)<key;});
            for(;point!=order.begin()+row->end&&std::int64_t(entries_[*point].x)<=high_x;++point)
                if(accept(entries_[*point]))selected[*point/64]|=std::uint64_t{1}<<(*point%64);
        }
        // Source event order is semantic: never aggregate or geometrically reorder
        // the float32 damage events, including newly woken siblings.
        for(std::size_t word=0;word<selected.size();++word) {
            auto bits=selected[word];
            while(bits){out.values[out.count++]=word*64+std::countr_zero(bits);bits&=bits-1;}
        }
        return out;
    }
public:
    // The SourceWorld owning this object must keep its address stable for every
    // borrowed frame. Rebuild changes generation, invalidating prior borrows.
    bool build(std::span<const Entry> entries,bool accept_model) {
        revoke();count_=row_count_=input_count_=0;max_range_=0;geometry_in_scratch_=false;
        if(!accept_model||entries.size()>capacity)return false;
        count_=entries.size();
        for(std::size_t i=0;i<count_;++i) {
            if(entries[i].handle<0||entries[i].range<0){count_=0;return false;}
            entries_[i]=entries[i];order_[i]=i;max_range_=std::max(max_range_,entries[i].range);
        }
        // Canonical handles are unique; reject malformed layouts rather than
        // silently merging distinct source rows.
        std::sort(order_.begin(),order_.begin()+count_,[&](std::size_t a,std::size_t b){return entries_[a].handle<entries_[b].handle;});
        for(std::size_t i=1;i<count_;++i)if(entries_[order_[i-1]].handle==entries_[order_[i]].handle){count_=0;return false;}
        std::sort(order_.begin(),order_.begin()+count_,[&](std::size_t a,std::size_t b){
            const auto& x=entries_[a];const auto& y=entries_[b];
            if(x.y!=y.y)return x.y<y.y;if(x.x!=y.x)return x.x<y.x;return a<b;
        });
        for(std::size_t i=0;i<count_;) {
            const auto first=i;const int y=entries_[order_[i]].y;
            while(i<count_&&entries_[order_[i]].y==y)++i;
            rows_[row_count_++]={y,first,i};
        }
        valid_=true;phase_=Phase::ready;input_count_=count_;return true;
    }
    // Native initialization uses this incremental protocol. The caller feeds
    // canonical rows under its own clock/fuel fence, then advances bounded batches
    // under the same fence. No partially built layout can publish a token.
    bool begin_build(std::size_t expected_count,bool accept_model){
        revoke();count_=row_count_=input_count_=0;max_range_=0;cursor_=0;
        source_order_=true;geometry_in_scratch_=false;merge_active_=false;
        if(!accept_model||expected_count>capacity)return false;
        count_=expected_count;phase_=count_?Phase::collecting:Phase::rows;return true;
    }
    bool append(Entry entry){
        if(phase_!=Phase::collecting||input_count_>=count_||entry.handle<0||entry.range<0){fail_build();return false;}
        entries_[input_count_]=entry;order_[input_count_]=input_count_;++input_count_;
        max_range_=std::max(max_range_,entry.range);
        if(input_count_==count_)start_sort(Phase::handle_sort);
        return true;
    }
    BuildState advance_build(std::size_t work_units){
        for(std::size_t i=0;i<work_units&&status()==BuildState::building;++i)advance_unit();
        return status();
    }
    BuildState status() const {
        if(valid_)return BuildState::ready;
        if(phase_==Phase::empty)return BuildState::empty;
        if(phase_==Phase::collecting)return BuildState::collecting;
        if(phase_==Phase::failed)return BuildState::failed;
        return BuildState::building;
    }
    std::size_t input_count() const{return input_count_;}
    void revoke(){valid_=false;phase_=Phase::failed;if(++generation_==0)++generation_;}
    bool valid() const{return valid_;}
    std::size_t count() const{return count_;}
    std::uint64_t generation() const{return generation_;}
    Token borrow() const{return valid_?Token{this,generation_,count_}:Token{};}
    bool observe(std::size_t source_index,int handle,int x,int y,int range) {
        if(!valid_)return false;
        if(source_index>=count_) {revoke();return false;}
        const auto& expected=entries_[source_index];
        if(expected.handle!=handle||expected.x!=x||expected.y!=y||expected.range!=range){revoke();return false;}
        return true;
    }
    Selection hits(int x,int y,int radius) const {
        return rectangle(x,y,radius,[](const Entry&){return true;});
    }
    Selection threats(int x,int y,int extra_reach=0) const {
        if(extra_reach<0)return {};
        return rectangle(x,y,std::int64_t(max_range_)+extra_reach,[&](const Entry& entry){
            return std::max(difference(entry.x,x),difference(entry.y,y))<=std::int64_t(entry.range)+extra_reach;
        });
    }
};
inline bool Token::valid(std::size_t current_count) const {
    return index&&index->valid()&&generation==index->generation()&&count==current_count&&count==index->count();
}
} // namespace mosaic::spatial
