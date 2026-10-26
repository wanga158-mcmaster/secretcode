#include "../src/joint_catalogue.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <type_traits>
#include <vector>

namespace jc=mosaic::joint_catalogue;
unsigned checks=0;
void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"joint-catalogue failed line%d check%u\n",line,checks);std::abort();}}
#define CHECK(value) check(bool(value),__LINE__)

constinit jc::Index<64,64> static_index{};
constinit jc::Selection<64> static_selection{};
static_assert(!std::is_copy_constructible_v<jc::Index<64,64>>);
static_assert(!std::is_move_constructible_v<jc::Index<64,64>>);
static_assert(!std::is_copy_constructible_v<jc::Selection<64>>);
static_assert(std::is_trivially_destructible_v<jc::Index<64,64>>);

struct Expected {
    std::vector<std::size_t> represented,overflow;
    std::vector<bool> represented_incoming,overflow_incoming;
    bool omitted_any{};
};
Expected literal(std::span<const jc::Footprint> records,jc::Point origin,int raw_speed,
                 int job,int capture,std::size_t cap){
    Expected answer;
    // Direct literal two-pass loop, independently testing distance rather than
    // the index's clipped-square incidence construction.
    for(unsigned pass=0;pass<2;++pass)for(std::size_t index=0;index<records.size();++index){
        const auto& record=records[index];
        const bool selected=record.group==job||record.group==capture;
        if(selected!=(pass==0))continue;
        const auto dx=std::int64_t(origin.x)-record.x,dy=std::int64_t(origin.y)-record.y;
        const auto distance=std::max(dx<0?-dx:dx,dy<0?-dy:dy);
        const bool incoming=distance<=std::int64_t(record.range)+std::max(0,raw_speed);
        if(!selected&&!incoming){answer.omitted_any=true;continue;}
        if(answer.represented.size()<cap){answer.represented.push_back(index);answer.represented_incoming.push_back(incoming);}
        else{answer.overflow.push_back(index);answer.overflow_incoming.push_back(incoming);answer.omitted_any=true;}
    }
    return answer;
}

template<std::size_t N,std::size_t C>
void equivalent(const jc::Index<N,C>& index,std::span<const jc::Footprint> records,
                jc::Point origin,int speed,int job,int capture,std::size_t cap){
    jc::Selection<N> actual;
    const auto expected=literal(records,origin,speed,job,capture,cap);
    CHECK(index.query(origin,speed,job,capture,cap,actual)==jc::QueryStatus::ok);
    CHECK(actual.represented_count==expected.represented.size());
    CHECK(actual.overflow_count==expected.overflow.size());
    CHECK(actual.omitted_any==expected.omitted_any);
    for(std::size_t n=0;n<actual.represented_count;++n){
        CHECK(actual.represented[n]==expected.represented[n]);
        CHECK(actual.represented_incoming[n]==expected.represented_incoming[n]);
        CHECK(actual.represented_incoming[n]==jc::within_halo(records[actual.represented[n]],origin,speed));
    }
    for(std::size_t n=0;n<actual.overflow_count;++n){
        CHECK(actual.overflow[n]==expected.overflow[n]);
        CHECK(actual.overflow_incoming[n]==expected.overflow_incoming[n]);
        CHECK(actual.overflow_incoming[n]==jc::within_halo(records[actual.overflow[n]],origin,speed));
    }
}

template<std::size_t N,std::size_t C>
void complete(jc::Index<N,C>& index,int width,int height,std::span<const jc::Footprint> records){
    auto progress=index.begin(width,height,records);
    CHECK(progress.units==0&&!progress.failed());
    std::size_t ticks=0,previous_copied=0,previous_cleared=0,previous_incidences=0;
    while(!progress.ready()){
        CHECK(++ticks<100000);
        jc::Selection<N> partial;partial.represented_count=partial.overflow_count=3;partial.omitted_any=true;
        CHECK(index.query({0,0},2,-1,-1,16,partial)==jc::QueryStatus::not_ready);
        CHECK(partial.represented_count==0&&partial.overflow_count==0&&!partial.omitted_any);
        CHECK(!index.record(0)&&index.size()==0);
        const auto unchanged=index.buildStep(0);
        CHECK(unchanged.units==0&&unchanged.state==progress.state);
        const std::size_t budget=1+ticks%7;
        progress=index.buildStep(budget);
        CHECK(!progress.failed()&&progress.units>0&&progress.units<=budget);
        CHECK(progress.records_copied>=previous_copied&&progress.clear_words_done>=previous_cleared&&progress.incidences_done>=previous_incidences);
        previous_copied=progress.records_copied;previous_cleared=progress.clear_words_done;previous_incidences=progress.incidences_done;
    }
    CHECK(index.ready()&&index.size()==records.size());
    CHECK(index.buildStep(100).units==0);
    for(std::size_t n=0;n<records.size();++n){
        const auto* saved=index.record(n);CHECK(saved);
        CHECK(saved->x==records[n].x&&saved->y==records[n].y&&saved->range==records[n].range&&saved->group==records[n].group&&saved->physical_id==records[n].physical_id);
    }
    CHECK(!index.record(records.size()));
}

void selected_union_and_overflow_order(){
    const std::array<jc::Footprint,6> records{{
        {9,9,0,1,99},{1,0,1,0,8},{9,8,0,2,8},
        {0,0,0,1,2},{9,9,0,0,1},{0,0,0,3,0}
    }};
    // Source5 represents a zero-damage NPC in the caller. Physics has no damage
    // field, so it cannot be discarded and must still consume/overflow capacity.
    jc::Index<8,100> index;complete(index,10,10,records);
    jc::Selection<8> output;
    CHECK(index.query({0,0},0,1,2,2,output)==jc::QueryStatus::ok);
    CHECK(output.represented_count==2&&output.represented[0]==0&&output.represented[1]==2);
    CHECK(!output.represented_incoming[0]&&!output.represented_incoming[1]);
    CHECK(output.overflow_count==3&&output.overflow[0]==3&&output.overflow[1]==1&&output.overflow[2]==5&&output.omitted_any);
    CHECK(output.overflow_incoming[0]&&output.overflow_incoming[1]&&output.overflow_incoming[2]);
    CHECK(index.query({0,0},0,1,2,0,output)==jc::QueryStatus::ok);
    const std::array<std::size_t,5> all{0,2,3,1,5};
    CHECK(output.represented_count==0&&output.overflow_count==all.size());
    for(std::size_t n=0;n<all.size();++n){CHECK(output.overflow[n]==all[n]);CHECK(output.overflow_incoming[n]==(n>=2));}
    equivalent(index,records,{0,0},0,1,1,16); // overlap selected once
    equivalent(index,records,{0,0},0,-1,-1,16);
    CHECK(!jc::within_halo(records[0],{0,0},0)); // remote selected still retained
    CHECK(jc::within_halo(records[1],{0,0},0)&&jc::within_halo(records[5],{0,0},0));
}

void exhaustive_cells_groups_halos_caps(){
    constexpr int width=7,height=6;
    std::array<jc::Footprint,40> records;
    for(std::size_t n=0;n<records.size();++n)
        records[n]={int((n*3)%width),int((n*5)%height),int(n%4),int(n%5),std::uint32_t(1000-(n*17))};
    records[3]=records[2]; // duplicate entire footprint and ID, distinct catalogue row
    records[17]={6,5,INT_MAX,4,0};
    records[25]={0,0,0,1,0}; // exact zero-range boundary and scrambled/duplicate ID
    complete(static_index,width,height,records);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        for(int job=-1;job<=5;++job)for(int capture=-1;capture<=5;++capture)
            for(const int speed:std::array{-17,0,1,2,20,INT_MAX})
                for(const std::size_t cap:std::array<std::size_t,3>{0,1,16})
                    equivalent(static_index,records,{x,y},speed,job,capture,cap);
    CHECK(static_index.query({0,0},0,1,2,16,static_selection)==jc::QueryStatus::ok);
    const auto original=records[0];records[0]={-1,-1,-1,-1,0};
    const auto* saved=static_index.record(0);
    CHECK(saved&&saved->x==original.x&&saved->group==original.group&&saved->physical_id==original.physical_id);
}

void multiple_words_and_edge_clipping(){
    std::array<jc::Footprint,130> records;
    for(std::size_t n=0;n<records.size();++n)records[n]={int(n%4),int((n/4)%4),int(n%3),int(n%4),std::uint32_t(n%2)};
    jc::Index<130,16> index;complete(index,4,4,records);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)
        for(int job=-1;job<=4;++job)for(int capture=-1;capture<=4;++capture)
            for(const int halo:std::array{0,1,2,INT_MAX})for(const std::size_t cap:std::array<std::size_t,2>{1,16})
                equivalent(index,records,{x,y},halo,job,capture,cap);
    const std::array<jc::Footprint,1> one{{{3,3,0,0,9}}};
    complete(index,4,4,one); // count shrink clears masks; unused words stay unread
    equivalent(index,one,{0,0},0,-1,-1,16);
    equivalent(index,one,{0,0},2,-1,-1,16);
    equivalent(index,one,{0,0},3,-1,-1,16);
}

void typed_failures_empty_restart_and_extremes(){
    jc::Index<2,4> index;jc::Selection<2> output;
    CHECK(index.state()==jc::BuildState::idle&&index.failure()==jc::Failure::none);
    CHECK(index.buildStep(10).units==0);
    CHECK(index.query({0,0},0,-1,-1,16,output)==jc::QueryStatus::not_ready);
    const std::array<jc::Footprint,1> valid{{{0,0,0,0,0}}};
    CHECK(index.begin(0,2,valid).failure==jc::Failure::invalid_dimensions);
    CHECK(index.begin(-1,2,valid).failure==jc::Failure::invalid_dimensions);
    CHECK(index.begin(INT_MAX,INT_MAX,valid).failure==jc::Failure::cell_capacity);
    const std::array<jc::Footprint,3> too_many{{{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}}};
    CHECK(index.begin(2,2,too_many).failure==jc::Failure::record_capacity);
    for(const auto bad:std::array<jc::Footprint,4>{{{-1,0,0,0,0},{0,2,0,0,0},{0,0,-1,0,0},{0,0,0,-1,0}}}){
        CHECK(!index.begin(2,2,std::span(&bad,1)).failed());
        const auto failed=index.buildStep(1);CHECK(failed.failed()&&failed.failure==jc::Failure::invalid_record&&failed.units==1);
        CHECK(!index.ready()&&!index.record(0)&&index.buildStep(2).units==0);
    }
    complete(index,2,2,valid);
    CHECK(index.query({-1,0},0,-1,-1,16,output)==jc::QueryStatus::invalid_origin);
    CHECK(index.query({2,0},0,-1,-1,16,output)==jc::QueryStatus::invalid_origin);
    CHECK(index.query({0,0},0,-1,-1,17,output)==jc::QueryStatus::invalid_capacity);
    CHECK(output.represented_count==0&&output.overflow_count==0&&!output.omitted_any);
    CHECK(index.begin(2,2,{}).ready());
    CHECK(index.query({0,0},INT_MAX,0,0,0,output)==jc::QueryStatus::ok);
    CHECK(!output.represented_count&&!output.overflow_count&&!output.omitted_any&&!index.record(0));
    CHECK(jc::within_halo({0,0,0,0,0},{1,1},1));
    CHECK(!jc::within_halo({0,0,0,0,0},{1,1},-1));
    CHECK(!jc::within_halo({INT_MAX,INT_MAX,INT_MAX,0,0},{INT_MIN,INT_MIN},INT_MAX));
    CHECK(jc::within_halo({INT_MAX,INT_MAX,INT_MAX,0,0},{INT_MIN+1,INT_MIN+1},INT_MAX));
}

void sparse512_groups_and_one_unit_build(){
    // All512 rows are remote from this empty local square. Every group ID is
    // arbitrary/noncontiguous, and its numeric order opposes catalogue order.
    // This exercises the zero-/one-/two-output query paths at full capacity;
    // source/call-graph review separately verifies binary group-key lookup and
    // word/set-bit traversal, with no record-metadata walk or fuel inference.
    std::array<jc::Footprint,512> records;
    for(std::size_t n=0;n<records.size();++n)
        records[n]={48+int(n%16),16+int(n/16),0,INT_MAX-int(n*4093),std::uint32_t(n%13)};
    jc::Index<512,4096> index;
    auto progress=index.begin(64,64,records);
    std::size_t units=0;
    while(!progress.ready()){
        progress=index.buildStep(1);CHECK(!progress.failed()&&progress.units==1);
        CHECK(++units<100000);
        CHECK(index.ready()==progress.ready());
    }
    CHECK(progress.records_copied==512);
    CHECK(progress.clear_words_done==(4096+512)*8);
    CHECK(progress.incidences_done==1024); //512 group bits +512 range-zero cells
    jc::Selection<512> output;
    CHECK(index.query({0,0},2,-1,-1,16,output)==jc::QueryStatus::ok);
    CHECK(output.represented_count==0&&output.overflow_count==0&&output.omitted_any);
    CHECK(index.query({0,0},2,records[400].group,-1,16,output)==jc::QueryStatus::ok);
    CHECK(output.represented_count==1&&output.represented[0]==400&&output.overflow_count==0&&output.omitted_any);
    CHECK(!output.represented_incoming[0]);
    CHECK(index.query({0,0},2,records[400].group,records[3].group,16,output)==jc::QueryStatus::ok);
    CHECK(output.represented_count==2&&output.represented[0]==3&&output.represented[1]==400&&output.omitted_any);
    CHECK(!output.represented_incoming[0]&&!output.represented_incoming[1]);
    for(const auto origin:std::array<jc::Point,6>{{{0,0},{15,15},{47,16},{48,16},{63,47},{63,63}}})
        for(const int halo:std::array{-1,0,1,2,INT_MAX})
            for(const int job:std::array{-1,17,records[0].group,records[31].group,records[511].group})
                for(const int capture:std::array{-1,17,records[0].group,records[255].group,records[511].group})
                    for(const std::size_t cap:std::array<std::size_t,3>{0,1,16})
                        equivalent(index,records,origin,halo,job,capture,cap);
    CHECK(index.query({0,0},INT_MAX,-1,-1,0,output)==jc::QueryStatus::ok);
    CHECK(output.represented_count==0&&output.overflow_count==512&&output.omitted_any);
    for(std::size_t n=0;n<512;++n){CHECK(output.overflow[n]==n);CHECK(output.overflow_incoming[n]);}

    // Reuse the same eight-word-capacity object with changing active-word and
    // group counts. Full-map fast paths must mask partial last words before
    // popcount/ctz, and old groups must never survive a smaller rebuild.
    std::array<jc::Footprint,129> smaller;
    for(std::size_t n=0;n<smaller.size();++n)
        smaller[n]={int(n%7),int(n%6),int(n%3),int(n%11)*100003, std::uint32_t(128-n)};
    for(const std::size_t count:std::array<std::size_t,8>{65,1,0,63,64,127,129,3}){
        const std::span<const jc::Footprint> active(smaller.data(),count);
        complete(index,7,6,active);
        for(int y=0;y<6;++y)for(int x=0;x<7;++x)
            for(const int halo:std::array{-1,0,1,2,INT_MAX})
                for(const int job:std::array{-1,0,100003,500015,INT_MAX})
                    for(const int capture:std::array{-1,0,100003,1000030,INT_MAX})
                        for(const std::size_t cap:std::array<std::size_t,3>{0,1,16})
                            equivalent(index,active,{x,y},halo,job,capture,cap);
    }
}

void copying_detaches_before_group_sort_and_clear(){
    std::array<jc::Footprint,5> source{{{0,0,0,1000003,91},{1,1,1,7,2},{2,2,0,INT_MAX,0},{0,2,2,7,99},{2,0,1,0,8}}};
    const auto saved=source;
    jc::Index<8,9> index;
    auto progress=index.begin(3,3,source);
    for(std::size_t n=0;n<source.size();++n){progress=index.buildStep(1);CHECK(progress.units==1&&!progress.failed());}
    CHECK(progress.records_copied==source.size()&&progress.state==jc::BuildState::clearing);
    for(auto& record:source)record={-1,-1,-1,-1,0};
    while(!progress.ready()){progress=index.buildStep(1);CHECK(progress.units==1&&!progress.failed());}
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)
        for(const int job:std::array{-1,0,7,1000003,INT_MAX})
            for(const int capture:std::array{-1,0,7,1000003,INT_MAX})
                equivalent(index,saved,{x,y},0,job,capture,1);
}

int main(){
    selected_union_and_overflow_order();exhaustive_cells_groups_halos_caps();
    multiple_words_and_edge_clipping();typed_failures_empty_restart_and_extremes();
    sparse512_groups_and_one_unit_build();copying_detaches_before_group_sort_and_clear();
    std::printf("joint-catalogue checks=%u all passed\n",checks);
}
