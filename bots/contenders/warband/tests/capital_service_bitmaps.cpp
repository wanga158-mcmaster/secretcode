#include "../src/capital_service_bitmaps.h"
#include <array>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace b=wb::capital::service_bitmaps;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
static_assert(!std::is_convertible_v<b::PackedView,b::CellView>);
static_assert(!std::is_convertible_v<b::CellView,b::PackedView>);
static_assert(!std::is_convertible_v<b::PackedBuffer,b::PackedView>);
static_assert(!std::is_convertible_v<b::CellBuffer,b::CellView>);
static constexpr b::Stamp source{7,13};
struct Budget {unsigned cells{},calls{},paid{};};
static bool permit(void* pointer,unsigned cells){auto& b=*static_cast<Budget*>(pointer);++b.calls;if(b.cells<cells)return false;b.cells-=cells;b.paid+=cells;return true;}
static bool poison_work(void* pointer,unsigned){static_cast<b::Converter*>(pointer)->cancel();return true;}
static void mark(std::span<std::uint8_t> packed,unsigned cell){packed[cell/8]|=std::uint8_t(1u<<(cell%8));}
static void packed31(){std::array<std::uint8_t,121> packed{};std::array<std::uint8_t,961> cells{};cells.fill(211);
    for(unsigned i=0;i<961;++i)if(i%7==0||i==960)mark(packed,i);
    const b::PackedView input{31,31,packed};check(b::validate(input).valid&&b::validate(input).cells==961&&b::validate(input).packed_bytes==121,"SDK 31x31 export is exactly 121 packed bytes");
    check(!b::validate(b::CellView{31,31,packed}).valid,"packed export cannot masquerade as route cell bytes");
    b::Converter converter;auto r=converter.begin_unpack(input,b::CellBuffer{31,31,cells},source);check(r.state==b::State::Building&&r.total==961&&!converter.completed_cells(source),"unpack preparation exposes no completed cell view");
    Budget budget{961,0,0};unsigned calls=0;while(r.state==b::State::Building||r.state==b::State::Deferred){r=converter.advance(source,53,{&budget,&permit});++calls;
        check(r.last_cells<=53&&r.done<=961,"each conversion chunk has a declared cell bound");if(r.state!=b::State::Complete)check(!converter.completed_cells(source),"partial cells never become a completed typed view");}
    check(r.state==b::State::Complete&&r.done==961&&budget.paid==961&&budget.calls==calls&&calls==19,"exactly 961 cells and one work request per chunk");
    const auto completed=converter.completed_cells(source);check(completed.has_value()&&completed->bytes.size()==961,"completed route view owns no invented shape");
    bool same=true;for(unsigned i=0;i<961;++i)same=same&&cells[i]==unsigned(i%7==0||i==960);check(same,"unpack applies row-major bit position rather than packing packed bytes a second time");
    check(b::bit(input,30,30).valid&&b::bit(input,30,30).value&&b::bit(*completed,30,30).value,"last map tile uses packed byte120 bit0 and cell960");
    std::array<std::uint8_t,121> roundtrip{};roundtrip.fill(255);Budget packing{961,0,0};const auto done=b::pack_cells(*completed,b::PackedBuffer{31,31,roundtrip},source,{&packing,&permit});
    check(done.state==b::State::Complete&&packing.calls==1&&packing.paid==961&&roundtrip==packed,"explicit cell-to-packed conversion is exact and charges once");
    check((roundtrip.back()&254)==0,"pack zeroes every nonexistent tail bit");
    check(!converter.completed_cells({7,14})&&!converter.completed_packed(source),"completion binds source revision and conversion direction");
}
static void invalid_shapes_and_tails(){std::array<std::uint8_t,121> packed{};std::array<std::uint8_t,961> cells{};cells.fill(99);b::Converter converter;
    check(!b::dimensions(0,31).valid&&!b::dimensions(31,-1).valid&&!b::dimensions(101,100).valid,"dimensions reject empty negative and over-limit maps");
    check(b::dimensions(100,100).valid&&b::dimensions(1,1).packed_bytes==1,"map size cap and smallest packed byte are exact");
    check(converter.begin_unpack(b::PackedView{31,31,packed},b::CellBuffer{1,961,cells},source).reason==b::Reason::Dimensions,"equal cell counts do not erase conflicting row geometry");
    check(converter.begin_unpack(b::PackedView{31,31,packed},b::CellBuffer{31,31,packed},source).reason==b::Reason::Size,"121 output bytes cannot become 961 route cells");
    check(converter.begin_pack(b::CellView{31,31,packed},b::PackedBuffer{31,31,packed},source).reason==b::Reason::Size,"already packed input fails explicit pack_cells shape");
    check(converter.begin_unpack(b::PackedView{11,11,packed},b::CellBuffer{31,31,cells},source).reason==b::Reason::Size,"121-byte storage alone cannot supply a different packed shape");
    packed.back()=128;check(converter.begin_unpack(b::PackedView{31,31,packed},b::CellBuffer{31,31,cells},source).reason==b::Reason::Tail,"nonzero nonexistent packed map tiles rejected");
    check(b::bit(b::PackedView{31,31,packed},0,0).reason==b::Reason::Tail,"lookup cannot silently ignore a malformed packed tail");packed.back()=0;
    check(b::bit(b::PackedView{31,31,packed},31,0).reason==b::Reason::Coordinates&&b::bit(b::CellView{31,31,cells},-1,0).reason==b::Reason::Coordinates,"typed lookup rejects outside coordinates");
    check(converter.begin_unpack(b::PackedView{31,31,packed},b::CellBuffer{31,31,cells},{}).reason==b::Reason::Source,"conversion cannot mint a source revision");
    bool untouched=true;for(auto cell:cells)untouched=untouched&&cell==99;check(untouched&&!converter.completed_cells(source),"every invalid preparation leaves destination poison unusable and unchanged");
    std::array<std::uint8_t,2> nine{{1,1}};std::array<std::uint8_t,9> ninecells{};check(b::unpack(b::PackedView{3,3,nine},b::CellBuffer{3,3,ninecells},source).state==b::State::Complete&&ninecells[8]==1,"nonmultiple8 map crosses byte boundary exactly");
    nine[1]|=2;check(!b::validate(b::PackedView{3,3,nine}).valid,"3x3 tail permits only the ninth cell bit");
}
static void aliasing(){std::array<std::uint8_t,1082> memory{};b::Converter converter;
    check(converter.begin_unpack(b::PackedView{31,31,{memory.data(),121}},b::CellBuffer{31,31,{memory.data(),961}},source).reason==b::Reason::Alias,"identical packed/cell backing storage cannot be converted in place");
    check(converter.begin_unpack(b::PackedView{31,31,{memory.data(),121}},b::CellBuffer{31,31,{memory.data()+100,961}},source).reason==b::Reason::Alias,"partial range overlap is rejected");
    check(converter.begin_pack(b::CellView{31,31,{memory.data(),961}},b::PackedBuffer{31,31,{memory.data()+950,121}},source).reason==b::Reason::Alias,"cell-to-packed compression also rejects overlap");
    check(b::unpack(b::PackedView{31,31,{memory.data(),121}},b::CellBuffer{31,31,{memory.data()+121,961}},source).state==b::State::Complete,"adjacent disjoint buffers are permitted");
}
static void budget_reuse_poison(){std::array<std::uint8_t,2> packed{{85,1}};std::array<std::uint8_t,9> cells{};cells.fill(33);b::Converter converter;
    converter.begin_unpack(b::PackedView{3,3,packed},b::CellBuffer{3,3,cells},source);Budget denied{2,0,0};auto r=converter.advance(source,3,{&denied,&permit});
    check(r.state==b::State::Deferred&&r.done==0&&denied.calls==1&&cells[0]==33,"budget denial performs no partial write");
    denied.cells=4;r=converter.advance(source,4,{&denied,&permit});check(r.state==b::State::Building&&r.done==4&&cells[3]==0&&cells[4]==33&&!converter.completed_cells(source),"funded prefix preserves unfinished poison outside its exact range");
    r=converter.advance(source,0);check(r.state==b::State::Deferred&&r.done==4,"zero requested work cannot finish a partial conversion");
    denied.cells=5;r=converter.advance(source,99,{&denied,&permit});check(r.state==b::State::Complete&&r.done==9&&converter.completed_cells(source).has_value(),"conversion resumes at its exact paid prefix");
    check(converter.advance({7,14},1).reason==b::Reason::Source&&!converter.completed_cells(source),"new source invalidates an old completed result");
    converter.begin_unpack(b::PackedView{3,3,packed},b::CellBuffer{3,3,cells},source);check(converter.advance(source,1,{&converter,&poison_work}).reason==b::Reason::Source&&!converter.completed_cells(source),"callback cancellation revokes authority before conversion writes");
    cells.fill(255);std::array<std::uint8_t,2> output{{211,211}};converter.begin_pack(b::CellView{3,3,cells},b::PackedBuffer{3,3,output},source);
    check(converter.advance(source,5).state==b::State::Building&&!converter.completed_packed(source),"partial pack clears only its first visited byte and stays unavailable");
    check(converter.advance(source,4).state==b::State::Complete&&output[0]==255&&output[1]==1,"pack resumes within a byte and canonicalizes nonzero cell values");
    std::array<std::uint8_t,1> tinyinput{{0}},tinyout{{99}};converter.begin_unpack(b::PackedView{1,1,tinyinput},b::CellBuffer{1,1,tinyout},{8,1});
    check(converter.advance({8,1},1).state==b::State::Complete&&tinyout[0]==0&&converter.completed_cells({8,1})->bytes.size()==1,"large-to-small converter reuse ignores stale spans and cursor");
    converter.cancel();check(!converter.completed_cells({8,1})&&converter.advance({8,1},1).state==b::State::Invalid,"cancel revokes all completed views");
    cells.fill(77);Budget none{0,0,0};check(b::unpack(b::PackedView{3,3,packed},b::CellBuffer{3,3,cells},source,{&none,&permit}).state==b::State::Deferred&&cells[0]==77,"one-shot Deferred never exposes partially converted output");
}
static void size_sweep(){for(unsigned count=1;count<=10000;count+=37){const unsigned bytes=(count+7)/8;std::vector<std::uint8_t> input(bytes,0),cells(count,19),output(bytes,255);
        for(unsigned cell=0;cell<count;++cell)if((cell*13+count)%11<4)mark(input,cell);
        const auto a=b::unpack(b::PackedView{int(count),1,input},b::CellBuffer{int(count),1,cells},source);const auto c=b::pack_cells(b::CellView{int(count),1,cells},b::PackedBuffer{int(count),1,output},source);
        check(a.state==b::State::Complete&&c.state==b::State::Complete&&input==output,"all admitted remainder classes preserve packed roundtrip");}
    std::vector<std::uint8_t> cells(10000,1),packed(1250,0);check(b::pack_cells(b::CellView{100,100,cells},b::PackedBuffer{100,100,packed},source).state==b::State::Complete&&packed.back()==255,"full10000-cell conversion has exact1250-byte result");
}
int main(){packed31();invalid_shapes_and_tails();aliasing();budget_reuse_poison();size_sweep();std::printf("capital_service_bitmaps checks %u failures %u\n",checks,failures);return failures?1:0;}
