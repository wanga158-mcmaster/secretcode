#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// SDK export_linear_buffer is PACKED row-major bits: ceil(width*height/8).
// Route maps consume CELL bytes: width*height, nonzero means set. These types
// grant shape/completion facts only, never SDK freshness or action authority.
namespace wb::capital::service_bitmaps {
inline constexpr unsigned cell_limit=10000;
struct PackedView {int width{},height{};std::span<const std::uint8_t> bytes{};
    PackedView()=default;explicit PackedView(int w,int h,std::span<const std::uint8_t> b):width(w),height(h),bytes(b){}};
struct CellView {int width{},height{};std::span<const std::uint8_t> bytes{};
    CellView()=default;explicit CellView(int w,int h,std::span<const std::uint8_t> b):width(w),height(h),bytes(b){}};
struct PackedBuffer {int width{},height{};std::span<std::uint8_t> bytes{};
    PackedBuffer()=default;explicit PackedBuffer(int w,int h,std::span<std::uint8_t> b):width(w),height(h),bytes(b){}};
struct CellBuffer {int width{},height{};std::span<std::uint8_t> bytes{};
    CellBuffer()=default;explicit CellBuffer(int w,int h,std::span<std::uint8_t> b):width(w),height(h),bytes(b){}};
struct Stamp {std::uint64_t source{},revision{};friend bool operator==(Stamp,Stamp)=default;};
enum class State {Empty,Building,Deferred,Complete,Invalid};
enum class Reason {None,Dimensions,Size,Tail,Pointer,Alias,Source,Budget,State,Coordinates};
struct Shape {bool valid{};Reason reason{Reason::Dimensions};unsigned cells{},packed_bytes{};};
struct Result {State state{State::Empty};Reason reason{Reason::None};unsigned done{},total{},last_cells{};};
struct Lookup {bool valid{},value{};Reason reason{Reason::None};};
struct Work {void* context{};bool(*permit)(void*,unsigned cells){};};
inline Shape dimensions(int width,int height){if(width<=0||height<=0)return {};
    const auto cells=std::uint64_t(unsigned(width))*unsigned(height);if(cells>cell_limit)return {};
    return {true,Reason::None,unsigned(cells),unsigned((cells+7)/8)};}
inline bool addressable(const std::uint8_t* pointer,std::size_t bytes){return pointer&&bytes<=std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(pointer);}
inline Shape shape(int w,int h,std::span<const std::uint8_t> bytes,bool packed){auto out=dimensions(w,h);if(!out.valid)return out;
    if(bytes.size()!=(packed?out.packed_bytes:out.cells))return {false,Reason::Size,out.cells,out.packed_bytes};
    if(!addressable(bytes.data(),bytes.size()))return {false,Reason::Pointer,out.cells,out.packed_bytes};return out;}
inline Shape validate(PackedView view){auto out=shape(view.width,view.height,view.bytes,true);if(!out.valid)return out;
    const unsigned tail=out.cells%8;if(tail&&(view.bytes.back()&std::uint8_t(255u<<tail)))return {false,Reason::Tail,out.cells,out.packed_bytes};return out;}
inline Shape validate(CellView view){return shape(view.width,view.height,view.bytes,false);}
inline Shape validate(PackedBuffer view){return shape(view.width,view.height,view.bytes,true);}
inline Shape validate(CellBuffer view){return shape(view.width,view.height,view.bytes,false);}
inline bool overlap(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b){const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<y+b.size()&&y<x+a.size();}
inline Lookup bit(PackedView view,int x,int y){const auto s=validate(view);if(!s.valid)return {false,false,s.reason};
    if(x<0||y<0||x>=view.width||y>=view.height)return {false,false,Reason::Coordinates};const unsigned cell=unsigned(y)*unsigned(view.width)+unsigned(x);
    return {true,(view.bytes[cell/8]&(1u<<(cell%8)))!=0,Reason::None};}
inline Lookup bit(CellView view,int x,int y){const auto s=validate(view);if(!s.valid)return {false,false,s.reason};
    if(x<0||y<0||x>=view.width||y>=view.height)return {false,false,Reason::Coordinates};return {true,view.bytes[unsigned(y)*unsigned(view.width)+unsigned(x)]!=0,Reason::None};}

class Converter {
    enum class Mode {None,Unpack,Pack};Mode mode_{Mode::None};State state_{State::Empty};Reason reason_{Reason::None};
    Stamp stamp_{};int width_{},height_{};unsigned cursor_{},total_{},last_{};std::uint64_t generation_{};
    std::span<const std::uint8_t> input_{};std::span<std::uint8_t> output_{};
    Result result()const{return {state_,reason_,cursor_,total_,last_};}
    Result fail(Reason reason){state_=State::Invalid;reason_=reason;last_=0;return result();}
    bool generation(){if(generation_==std::numeric_limits<std::uint64_t>::max())return false;++generation_;return true;}
    Result begin(Mode mode,int width,int height,std::span<const std::uint8_t> input,std::span<std::uint8_t> output,Stamp stamp,Shape source,Shape target,int out_width,int out_height){
        mode_=Mode::None;cursor_=total_=last_=0;input_={};output_={};stamp_={};
        if(!generation())return fail(Reason::Source);if(!source.valid)return fail(source.reason);if(!target.valid)return fail(target.reason);
        if(width!=out_width||height!=out_height)return fail(Reason::Dimensions);
        if(!stamp.source||!stamp.revision)return fail(Reason::Source);
        if(overlap(input,output))return fail(Reason::Alias);
        mode_=mode;width_=width;height_=height;input_=input;output_=output;total_=source.cells;stamp_=stamp;state_=State::Building;reason_=Reason::None;return result();
    }
public:
    // Buffers are borrowed until completion/cancel. Caller must keep input
    // immutable, output exclusive, and both alive; current Stamp is checked at
    // every advance. No raw partially written buffer is a completed typed view.
    Result begin_unpack(PackedView input,CellBuffer output,Stamp stamp){return begin(Mode::Unpack,input.width,input.height,input.bytes,output.bytes,stamp,validate(input),validate(output),output.width,output.height);}
    Result begin_pack(CellView input,PackedBuffer output,Stamp stamp){return begin(Mode::Pack,input.width,input.height,input.bytes,output.bytes,stamp,validate(input),validate(output),output.width,output.height);}
    // One budget callback per bounded chunk, with its exact cell count. Null
    // callback means the caller has separately admitted the entire chunk. It
    // is not an unqualified native fuel bound. Denial writes nothing this call.
    Result advance(Stamp current,unsigned max_cells,Work work={}){
        last_=0;if(!(current==stamp_))return fail(Reason::Source);
        if(state_==State::Complete)return result();if(state_!=State::Building&&state_!=State::Deferred)return fail(Reason::State);
        const unsigned cells=std::min(max_cells,total_-cursor_);if(!cells){state_=State::Deferred;reason_=Reason::Budget;return result();}
        const auto version=generation_;const bool paid=!work.permit||work.permit(work.context,cells);
        if(version!=generation_)return fail(Reason::Source);
        if(!paid){state_=State::Deferred;reason_=Reason::Budget;return result();}
        const unsigned end=cursor_+cells;for(unsigned cell=cursor_;cell<end;++cell){
            if(mode_==Mode::Unpack)output_[cell]=std::uint8_t((input_[cell/8]>>(cell%8))&1u);
            else {if(cell%8==0)output_[cell/8]=0;if(input_[cell])output_[cell/8]|=std::uint8_t(1u<<(cell%8));}}
        cursor_=end;last_=cells;state_=cursor_==total_?State::Complete:State::Building;reason_=Reason::None;return result();
    }
    std::optional<CellView> completed_cells(Stamp current)const{if(state_!=State::Complete||mode_!=Mode::Unpack||!(current==stamp_))return {};
        return CellView{width_,height_,output_};}
    std::optional<PackedView> completed_packed(Stamp current)const{if(state_!=State::Complete||mode_!=Mode::Pack||!(current==stamp_))return {};
        return PackedView{width_,height_,output_};}
    void cancel(){generation();mode_=Mode::None;state_=State::Empty;reason_=Reason::None;stamp_={};input_={};output_={};cursor_=total_=last_=0;}
};
inline Result unpack(PackedView input,CellBuffer output,Stamp stamp,Work work={}){Converter converter;auto result=converter.begin_unpack(input,output,stamp);
    return result.state==State::Invalid?result:converter.advance(stamp,result.total,work);}
inline Result pack_cells(CellView input,PackedBuffer output,Stamp stamp,Work work={}){Converter converter;auto result=converter.begin_pack(input,output,stamp);
    return result.state==State::Invalid?result:converter.advance(stamp,result.total,work);}
} // namespace wb::capital::service_bitmaps
