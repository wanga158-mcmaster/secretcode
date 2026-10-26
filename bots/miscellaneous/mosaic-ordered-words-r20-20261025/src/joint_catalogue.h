#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::joint_catalogue {

struct Point { int x,y; };
struct Footprint { int x,y,range,group;std::uint32_t physical_id; };

// Catalogue physics only. Mutable life, damage and observation receipts belong
// to the runtime. Widen before subtraction/addition, including INT_MAX halos.
constexpr bool within_halo(const Footprint& record,Point origin,int raw_move_speed) noexcept {
    const std::int64_t halo=raw_move_speed>0?raw_move_speed:0;
    const std::int64_t dx=std::int64_t(origin.x)-record.x;
    const std::int64_t dy=std::int64_t(origin.y)-record.y;
    const auto ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    return (ax>ay?ax:ay)<=std::int64_t(record.range)+halo;
}

enum class BuildState { idle,copying,clearing,indexing,ready,failed };
enum class Failure { none,invalid_dimensions,cell_capacity,record_capacity,invalid_record };
enum class QueryStatus { ok,not_ready,invalid_origin,invalid_capacity };
struct BuildProgress {
    BuildState state;Failure failure;std::size_t units,records_copied,clear_words_done,incidences_done;
    constexpr bool ready()const noexcept{return state==BuildState::ready;}
    constexpr bool failed()const noexcept{return state==BuildState::failed;}
};

template<std::size_t MaxRecords> struct Selection {
    static constexpr std::size_t represented_capacity=16;
    std::size_t represented_count{},overflow_count{};bool omitted_any{};
    std::array<std::size_t,represented_capacity> represented;
    std::array<std::size_t,MaxRecords> overflow;
    // Labels are valid only after QueryStatus::ok and inside the corresponding
    // returned count. They use the same nearby mask as selection, including
    // selected rows that are retained despite being remote.
    std::array<bool,represented_capacity> represented_incoming;
    std::array<bool,MaxRecords> overflow_incoming;
    Selection()=default;
    Selection(const Selection&)=delete;Selection& operator=(const Selection&)=delete;
    Selection(Selection&&)=delete;Selection& operator=(Selection&&)=delete;
};

template<std::size_t MaxRecords=512,std::size_t MaxCells=16384> class Index {
    static_assert(MaxRecords>0&&MaxCells>0);
    static_assert(MaxRecords<=std::numeric_limits<std::size_t>::max()-63);
    static constexpr std::size_t words=(MaxRecords+63)/64;
    static_assert(MaxCells<=std::numeric_limits<std::size_t>::max()/words);
    static_assert(MaxRecords<=std::numeric_limits<std::size_t>::max()/words);
    // Default initialization leaves these ordinary, already-live objects
    // untouched. Only initialized prefixes are read. Noncopyability prevents
    // copying indeterminate unused elements. Static constinit Index{} is valid;
    // automatic users should default-initialize, then call begin().
    std::array<Footprint,MaxRecords> records_;
    std::array<std::uint64_t,MaxCells*words> cells_;
    std::array<std::uint64_t,MaxRecords*words> group_masks_;
    std::array<int,MaxRecords> group_keys_;
    std::array<std::size_t,MaxRecords> order_,merge_;
    std::span<const Footprint> source_{};
    BuildState state_{BuildState::idle};Failure failure_{Failure::none};
    int width_{},height_{};
    std::size_t count_{},cell_count_{},used_words_{},copied_{},cleared_{},incidences_{},record_cursor_{};
    enum class IndexPhase { sorting,groups,spatial };
    IndexPhase phase_{IndexPhase::sorting};
    std::size_t group_count_{},group_cursor_{},sort_width_{},sort_left_{},sort_mid_{},sort_right_{},sort_i_{},sort_j_{},sort_k_{};
    bool sort_primary_{true};
    int left_{},right_{},top_{},bottom_{},x_{},y_{};

    BuildProgress progress(std::size_t units)const noexcept {
        return {state_,failure_,units,copied_,cleared_,incidences_};
    }
    BuildProgress fail(Failure failure,std::size_t units=0)noexcept {
        failure_=failure;state_=BuildState::failed;source_={};return progress(units);
    }
    static int clipped(std::int64_t value,int maximum)noexcept {
        return value<0?0:value>maximum?maximum:int(value);
    }
    void rectangle()noexcept {
        const auto& record=records_[record_cursor_];
        left_=clipped(std::int64_t(record.x)-record.range,width_-1);
        right_=clipped(std::int64_t(record.x)+record.range,width_-1);
        top_=clipped(std::int64_t(record.y)-record.range,height_-1);
        bottom_=clipped(std::int64_t(record.y)+record.range,height_-1);
        x_=left_;y_=top_;
    }
    void merge_run()noexcept {
        const auto remaining=count_-sort_left_;
        sort_mid_=sort_left_+std::min(sort_width_,remaining);
        sort_right_=sort_mid_+std::min(sort_width_,count_-sort_mid_);
        sort_i_=sort_left_;sort_j_=sort_mid_;sort_k_=sort_left_;
    }
    void start_indexing()noexcept {
        state_=BuildState::indexing;group_count_=group_cursor_=0;
        sort_primary_=true;sort_width_=1;sort_left_=0;
        phase_=count_>1?IndexPhase::sorting:IndexPhase::groups;
        if(count_>1)merge_run();
    }
    void merge_one()noexcept {
        const auto& source=sort_primary_?order_:merge_;
        auto& destination=sort_primary_?merge_:order_;
        bool take_left=sort_j_==sort_right_;
        if(sort_i_!=sort_mid_&&sort_j_!=sort_right_){
            const auto left=source[sort_i_],right=source[sort_j_];
            take_left=records_[left].group<records_[right].group||
                (records_[left].group==records_[right].group&&left<right);
        }else if(sort_i_==sort_mid_)take_left=false;
        destination[sort_k_++]=source[take_left?sort_i_++:sort_j_++];
        if(sort_k_!=sort_right_)return;
        sort_left_=sort_right_;
        if(sort_left_!=count_){merge_run();return;}
        sort_primary_=!sort_primary_;
        if(sort_width_>=count_-sort_width_){phase_=IndexPhase::groups;return;}
        sort_width_+=sort_width_;sort_left_=0;merge_run();
    }
    std::size_t group_slot(int group)const noexcept {
        if(group<0)return MaxRecords;
        std::size_t low=0,high=group_count_;
        while(low<high){
            const auto middle=low+(high-low)/2;
            if(group_keys_[middle]<group)low=middle+1;else high=middle;
        }
        return low<group_count_&&group_keys_[low]==group?low:MaxRecords;
    }

public:
    Index()=default;
    Index(const Index&)=delete;Index& operator=(const Index&)=delete;
    Index(Index&&)=delete;Index& operator=(Index&&)=delete;
    static constexpr std::size_t record_capacity=MaxRecords,cell_capacity=MaxCells;
    BuildState state()const noexcept{return state_;}
    Failure failure()const noexcept{return failure_;}
    bool ready()const noexcept{return state_==BuildState::ready;}
    std::size_t size()const noexcept{return ready()?count_:0;}
    const Footprint* record(std::size_t index)const noexcept {
        return ready()&&index<count_?&records_[index]:nullptr;
    }

    // source must remain alive and unchanged until the copying stage finishes.
    // begin is constant work; one buildStep unit copies one record (and its
    // ordinal), clears one spatial/group mask word, moves one merge output, or
    // inserts one group/spatial incidence. No query exposes a partial index.
    BuildProgress begin(int width,int height,std::span<const Footprint> source)noexcept {
        state_=BuildState::idle;failure_=Failure::none;width_=width;height_=height;
        source_={};count_=cell_count_=used_words_=copied_=cleared_=incidences_=record_cursor_=0;
        group_count_=group_cursor_=sort_width_=sort_left_=sort_mid_=sort_right_=sort_i_=sort_j_=sort_k_=0;
        phase_=IndexPhase::sorting;sort_primary_=true;
        if(width<=0||height<=0)return fail(Failure::invalid_dimensions);
        const auto cells=std::uint64_t(width)*std::uint64_t(height);
        if(cells>MaxCells)return fail(Failure::cell_capacity);
        if(source.size()>MaxRecords)return fail(Failure::record_capacity);
        count_=source.size();cell_count_=std::size_t(cells);used_words_=(count_+63)/64;source_=source;
        state_=count_?BuildState::copying:BuildState::ready;
        return progress(0);
    }
    BuildProgress buildStep(std::size_t max_units)noexcept {
        std::size_t units=0;
        while(units<max_units){
            if(state_==BuildState::copying){
                const auto& record=source_[copied_];++units;
                if(record.x<0||record.y<0||record.x>=width_||record.y>=height_||record.range<0||record.group<0)
                    return fail(Failure::invalid_record,units);
                order_[copied_]=copied_;records_[copied_++]=record;
                if(copied_==count_){source_={};state_=BuildState::clearing;}
            }else if(state_==BuildState::clearing){
                const auto spatial_words=cell_count_*used_words_;
                const bool spatial=cleared_<spatial_words;
                const auto cursor=spatial?cleared_:cleared_-spatial_words;
                const auto row=cursor/used_words_,word=cursor%used_words_;
                if(spatial)cells_[row*words+word]=0;
                else group_masks_[row*words+word]=0;
                ++cleared_;++units;
                if(cleared_==spatial_words+count_*used_words_)start_indexing();
            }else if(state_==BuildState::indexing){
                if(phase_==IndexPhase::sorting){merge_one();++units;continue;}
                if(phase_==IndexPhase::groups){
                    const auto index=(sort_primary_?order_:merge_)[group_cursor_];
                    const int group=records_[index].group;
                    if(!group_count_||group_keys_[group_count_-1]!=group)group_keys_[group_count_++]=group;
                    group_masks_[(group_count_-1)*words+index/64]|=std::uint64_t{1}<<(index%64);
                    ++group_cursor_;++incidences_;++units;
                    if(group_cursor_==count_){phase_=IndexPhase::spatial;rectangle();}
                    continue;
                }
                const auto cell=std::size_t(y_)*std::size_t(width_)+std::size_t(x_);
                cells_[cell*words+record_cursor_/64]|=std::uint64_t{1}<<(record_cursor_%64);
                ++incidences_;++units;
                if(x_<right_)++x_;
                else if(y_<bottom_){x_=left_;++y_;}
                else if(++record_cursor_==count_)state_=BuildState::ready;
                else rectangle();
            }else break;
        }
        return progress(units);
    }

    QueryStatus query(Point origin,int raw_move_speed,int job,int capture_group,
                      std::size_t cap,Selection<MaxRecords>& output)const noexcept {
        output.represented_count=output.overflow_count=0;output.omitted_any=false;
        if(!ready())return QueryStatus::not_ready;
        if(origin.x<0||origin.y<0||origin.x>=width_||origin.y>=height_)return QueryStatus::invalid_origin;
        if(cap>Selection<MaxRecords>::represented_capacity)return QueryStatus::invalid_capacity;
        std::array<std::uint64_t,words> incoming,selected,nonselected;
        const auto job_slot=group_slot(job),capture_slot=group_slot(capture_group);
        for(std::size_t word=0;word<used_words_;++word){
            incoming[word]=0;selected[word]=0;
            if(job_slot!=MaxRecords)selected[word]|=group_masks_[job_slot*words+word];
            if(capture_slot!=MaxRecords)selected[word]|=group_masks_[capture_slot*words+word];
        }
        const std::int64_t halo=raw_move_speed>0?raw_move_speed:0;
        const int far_x=origin.x>width_-1-origin.x?origin.x:width_-1-origin.x;
        const int far_y=origin.y>height_-1-origin.y?origin.y:height_-1-origin.y;
        if(halo>=far_x&&halo>=far_y){
            for(std::size_t word=0;word<used_words_;++word)incoming[word]=~std::uint64_t{0};
        }else{
            const int left=clipped(std::int64_t(origin.x)-halo,width_-1);
            const int right=clipped(std::int64_t(origin.x)+halo,width_-1);
            const int top=clipped(std::int64_t(origin.y)-halo,height_-1);
            const int bottom=clipped(std::int64_t(origin.y)+halo,height_-1);
            for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
                const auto cell=std::size_t(y)*std::size_t(width_)+std::size_t(x);
                for(std::size_t word=0;word<used_words_;++word)incoming[word]|=cells_[cell*words+word];
            }
        }
        // On a rectangular valid-cell domain, intersecting the range square
        // with the clipped halo square is equivalent to d_inf<=range+halo:
        // each coordinate's intervals overlap, and both centers are valid.
        // Clear the unused high bits BEFORE cardinality or enumeration. The
        // fill-all shortcut intentionally sets them; there is no shift by64.
        if(used_words_&&count_%64){
            const auto valid=(std::uint64_t{1}<<(count_%64))-1;
            incoming[used_words_-1]&=valid;selected[used_words_-1]&=valid;
        }
        std::size_t selected_count=0,incoming_nonselected=0;
        for(std::size_t word=0;word<used_words_;++word){
            nonselected[word]=incoming[word]&~selected[word];
            selected_count+=std::size_t(std::popcount(selected[word]));
            incoming_nonselected+=std::size_t(std::popcount(nonselected[word]));
        }
        output.omitted_any=incoming_nonselected<count_-selected_count;
        // No record metadata is visited here. Group lookup is binary search on
        // prebuilt keys; membership/counting is word-wise. Enumerating low bits
        // in increasing word order is exactly the original catalogue order.
        for(unsigned pass=0;pass<2;++pass)for(std::size_t word=0;word<used_words_;++word){
            auto mask=pass==0?selected[word]:nonselected[word];
            while(mask){
                const auto bit=std::size_t(std::countr_zero(mask)),index=word*64+bit;
                const bool nearby=(incoming[word]&(std::uint64_t{1}<<bit))!=0;
                if(output.represented_count<cap){
                    const auto at=output.represented_count++;
                    output.represented[at]=index;output.represented_incoming[at]=nearby;
                }else{
                    const auto at=output.overflow_count++;
                    output.overflow[at]=index;output.overflow_incoming[at]=nearby;output.omitted_any=true;
                }
                mask&=mask-1;
            }
        }
        return QueryStatus::ok;
    }
};

} // namespace mosaic::joint_catalogue
