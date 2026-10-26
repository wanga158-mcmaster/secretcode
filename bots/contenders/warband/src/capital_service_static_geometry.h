#pragma once
#include "capital_service_bitmaps.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

// Public, immutable STATIC geometry only. No NPC-Life discount, dynamic
// occupancy, personal observation, policy/value or command permission lives
// here. Packed SDK exports stay typed; the same final raster also emits owned
// route CELL bytes without a second conversion pass or restarted preparation.
namespace wb::capital::service_static_geometry {
namespace b=service_bitmaps;
inline constexpr unsigned cell_limit=10000,npc_limit=10000,chunk_limit=1024;
struct Point {int x{},y{};};
struct SourceNpc {int group{-1};Point position{};float range{};bool damage_positive{};};
struct Source {std::uint64_t map_revision{},catalogue_revision{};int candidate_group{-1};friend bool operator==(Source,Source)=default;};
struct Input {Source source{};b::PackedView terrain{},own_spawn{};std::span<const SourceNpc> catalogue{};bool complete_catalogue{};};
enum class State {Empty,Building,Deferred,Complete,Unknown,Invalid};
enum class Reason {None,Source,Shape,Tail,Incomplete,Capacity,Alias,Point,Range,Group,Budget,State,Coverage};
enum class Stage {Empty,ValidateCatalogue,AllocateBlocked,AllocateSpawn,AllocateForeign,AllocateDifference,
    AllocateBlockedCells,AllocateSpawnCells,AllocateForeignCells,CopyTerrain,CopySpawn,ClearForeign,ClearDifference,OverlayCatalogue,Raster,Complete};
// bytes is allocation size on allocation stages and a conservative bound on
// destination byte writes otherwise. It is NOT a CPU/native fuel certificate.
struct Step {Stage stage{Stage::Empty};unsigned units{},bytes{};};
struct Work {void* context{};bool(*permit)(void*,const Step&){};};
struct Counts {std::uint64_t work{},allocated_bytes{},copied_bytes{},cleared_bytes{};
    unsigned validated_npcs{},overlaid_npcs{},blocked_sources{},positive_rectangles{},raster_cells{},allocations{},cell_writes{};};
struct Result {State state{State::Empty};Reason reason{Reason::None};Stage stage{Stage::Empty},last_stage{Stage::Empty};
    unsigned last_units{},cells{},packed_bytes{},difference_entries{},catalogue_count{};std::uint64_t generation{};Counts counts{};};
class Preparer;
struct Product {Source source{};std::uint64_t generation{};b::PackedView blocked{},own_spawn{},foreign{};
    b::CellView blocked_cells{},own_spawn_cells{},foreign_cells{};
private:const Preparer* owner_{};friend class Preparer;};

class Preparer {
    Source source_{};State state_{State::Empty};Reason reason_{Reason::None};Stage stage_{Stage::Empty},last_stage_{Stage::Empty};
    unsigned width_{},height_{},cells_{},bytes_{},difference_count_{},stride_{},catalogue_count_{},cursor_{},last_units_{};
    std::uint64_t generation_{};Counts counts_{};b::PackedView terrain_{},spawn_input_{};std::span<const SourceNpc> catalogue_{};
    std::unique_ptr<std::uint8_t[]> blocked_,spawn_,foreign_;std::unique_ptr<std::int32_t[]> difference_;
    unsigned blocked_capacity_{},spawn_capacity_{},foreign_capacity_{},difference_capacity_{};
    std::unique_ptr<std::uint8_t[]> blocked_cells_,spawn_cells_,foreign_cells_;
    unsigned blocked_cells_capacity_{},spawn_cells_capacity_{},foreign_cells_capacity_{};
    bool next_generation(){if(generation_==std::numeric_limits<std::uint64_t>::max())return false;++generation_;return true;}
    Result result()const{return {state_,reason_,stage_,last_stage_,last_units_,cells_,bytes_,difference_count_,catalogue_count_,generation_,counts_};}
    Result fail(Reason why,State state=State::Invalid){state_=state;reason_=why;return result();}
    void transition(Stage stage){stage_=stage;cursor_=0;}
    void tick(){++last_units_;++counts_.work;}
    bool point(Point p)const{return p.x>=0&&p.y>=0&&unsigned(p.x)<width_&&unsigned(p.y)<height_;}
    bool record(const SourceNpc& npc){if(npc.group<0){fail(Reason::Group);return false;}if(!point(npc.position)){fail(Reason::Point);return false;}
        if(!std::isfinite(npc.range)||npc.range<0){fail(Reason::Range);return false;}return true;}
    bool aliases(std::span<const std::uint8_t> input)const {
        auto overlaps=[&](const std::uint8_t* data,unsigned bytes){return data&&b::overlap(input,{data,bytes});};
        return overlaps(blocked_.get(),blocked_capacity_)||overlaps(spawn_.get(),spawn_capacity_)||overlaps(foreign_.get(),foreign_capacity_)||
            overlaps(blocked_cells_.get(),blocked_cells_capacity_)||overlaps(spawn_cells_.get(),spawn_cells_capacity_)||overlaps(foreign_cells_.get(),foreign_cells_capacity_)||
            overlaps(reinterpret_cast<const std::uint8_t*>(difference_.get()),difference_capacity_*unsigned(sizeof(std::int32_t)));
    }
    unsigned remaining()const {switch(stage_){
        case Stage::ValidateCatalogue:case Stage::OverlayCatalogue:return catalogue_count_-cursor_;
        case Stage::AllocateBlocked:case Stage::AllocateSpawn:case Stage::AllocateForeign:case Stage::AllocateDifference:
        case Stage::AllocateBlockedCells:case Stage::AllocateSpawnCells:case Stage::AllocateForeignCells:return 1;
        case Stage::CopyTerrain:case Stage::CopySpawn:case Stage::ClearForeign:return bytes_-cursor_;
        case Stage::ClearDifference:return difference_count_-cursor_;
        case Stage::Raster:return cells_-cursor_;default:return 0;}}
    Step step(unsigned units)const {unsigned bytes=0;switch(stage_){
        case Stage::AllocateBlocked:bytes=blocked_capacity_<bytes_?bytes_:0;break;
        case Stage::AllocateSpawn:bytes=spawn_capacity_<bytes_?bytes_:0;break;
        case Stage::AllocateForeign:bytes=foreign_capacity_<bytes_?bytes_:0;break;
        case Stage::AllocateDifference:bytes=difference_capacity_<difference_count_?difference_count_*unsigned(sizeof(std::int32_t)):0;break;
        case Stage::AllocateBlockedCells:bytes=blocked_cells_capacity_<cells_?cells_:0;break;
        case Stage::AllocateSpawnCells:bytes=spawn_cells_capacity_<cells_?cells_:0;break;
        case Stage::AllocateForeignCells:bytes=foreign_cells_capacity_<cells_?cells_:0;break;
        case Stage::CopyTerrain:case Stage::CopySpawn:case Stage::ClearForeign:bytes=units;break;
        case Stage::ClearDifference:bytes=units*unsigned(sizeof(std::int32_t));break;
        case Stage::OverlayCatalogue:bytes=units*17;break;
        case Stage::Raster:bytes=units*8;break;default:break;}return {stage_,units,bytes};}
    void allocate(std::unique_ptr<std::uint8_t[]>& buffer,unsigned& capacity,unsigned bytes){if(capacity<bytes){
            // Default-initialized byte storage: no hidden array zeroing. Every
            // used byte is later copied/cleared through funded chunks.
            buffer.reset(new std::uint8_t[bytes]);capacity=bytes;counts_.allocated_bytes+=bytes;++counts_.allocations;}}
    void overlay(const SourceNpc& npc){if(npc.group==source_.candidate_group)return;const unsigned cell=unsigned(npc.position.y)*width_+unsigned(npc.position.x);
        blocked_[cell/8]|=std::uint8_t(1u<<(cell%8));++counts_.blocked_sources;if(!npc.damage_positive)return;
        // Grid Chebyshev distance is integral. Truncating a finite nonnegative
        // radius preserves <=range exactly; clip huge finite ranges before any
        // integer conversion. The center itself is required to be on the map.
        const unsigned bound=std::max(width_,height_);const int radius=npc.range>=float(bound)?int(bound):int(npc.range);
        const int left=std::max(0,npc.position.x-radius),right=std::min(int(width_)-1,npc.position.x+radius);
        const int top=std::max(0,npc.position.y-radius),bottom=std::min(int(height_)-1,npc.position.y+radius);
        ++difference_[unsigned(top)*stride_+unsigned(left)];--difference_[unsigned(top)*stride_+unsigned(right+1)];
        --difference_[unsigned(bottom+1)*stride_+unsigned(left)];++difference_[unsigned(bottom+1)*stride_+unsigned(right+1)];++counts_.positive_rectangles;
    }
    bool raster(unsigned cell){const unsigned x=cell%width_,y=cell/width_,at=y*stride_+x;
        const std::int64_t count=std::int64_t(difference_[at])+(x?difference_[at-1]:0)+(y?difference_[at-stride_]:0)-(x&&y?difference_[at-stride_-1]:0);
        if(count<0||count>catalogue_count_){fail(Reason::Coverage);return false;}difference_[at]=std::int32_t(count);
        if(count)foreign_[cell/8]|=std::uint8_t(1u<<(cell%8));
        blocked_cells_[cell]=std::uint8_t((blocked_[cell/8]>>(cell%8))&1u);
        spawn_cells_[cell]=std::uint8_t((spawn_[cell/8]>>(cell%8))&1u);foreign_cells_[cell]=std::uint8_t(count>0);
        counts_.cell_writes+=3;++counts_.raster_cells;return true;
    }
public:
    // Product identity pins this stable owning object, not a movable buffer.
    Preparer()=default;Preparer(const Preparer&)=delete;Preparer& operator=(const Preparer&)=delete;
    Preparer(Preparer&&)=delete;Preparer& operator=(Preparer&&)=delete;
    // begin borrows immutable input storage, but performs no allocation, large
    // copy, catalogue scan or clear. Keep inputs alive through Complete/cancel.
    // map_revision must bind the terrain AND the caller's own-team spawn map.
    Result begin(const Input& in){last_units_=0;counts_={};state_=State::Empty;reason_=Reason::None;stage_=last_stage_=Stage::Empty;
        source_=in.source;terrain_={};spawn_input_={};catalogue_={};cursor_=0;cells_=bytes_=difference_count_=catalogue_count_=0;
        if(!next_generation())return fail(Reason::Source);
        if(!in.source.map_revision||!in.source.catalogue_revision||in.source.candidate_group<0)return fail(Reason::Source);
        if(!in.complete_catalogue)return fail(Reason::Incomplete,State::Unknown);
        const auto terrain=b::validate(in.terrain),spawn=b::validate(in.own_spawn);
        if(!terrain.valid||!spawn.valid)return fail((terrain.reason==b::Reason::Tail||spawn.reason==b::Reason::Tail)?Reason::Tail:Reason::Shape);
        if(in.terrain.width!=in.own_spawn.width||in.terrain.height!=in.own_spawn.height)return fail(Reason::Shape);
        if(in.catalogue.size()>npc_limit)return fail(Reason::Capacity,State::Unknown);
        if(in.catalogue.size()&&!in.catalogue.data())return fail(Reason::Incomplete,State::Unknown);
        if(aliases(in.terrain.bytes)||aliases(in.own_spawn.bytes))return fail(Reason::Alias);
        width_=unsigned(in.terrain.width);height_=unsigned(in.terrain.height);cells_=terrain.cells;bytes_=terrain.packed_bytes;stride_=width_+1;
        difference_count_=(width_+1)*(height_+1);catalogue_count_=unsigned(in.catalogue.size());terrain_=in.terrain;spawn_input_=in.own_spawn;catalogue_=in.catalogue;
        state_=State::Building;transition(catalogue_count_?Stage::ValidateCatalogue:Stage::AllocateBlocked);return result();
    }
    // Exactly one permit callback for the bounded current-stage chunk, before
    // any allocation/copy/clear/overlay/raster mutation. A null callback means
    // the caller has admitted this chunk separately; no native cost is proved.
    Result advance(Source current,unsigned max_units,Work work={}){last_units_=0;last_stage_=stage_;
        if(!(current==source_))return fail(Reason::Source);if(state_==State::Complete)return result();
        if(state_!=State::Building&&state_!=State::Deferred)return fail(Reason::State);
        const unsigned units=std::min({max_units,chunk_limit,remaining()});if(!units){state_=State::Deferred;reason_=Reason::Budget;return result();}
        const auto request=step(units);const auto generation=generation_;const bool paid=!work.permit||work.permit(work.context,request);
        if(generation!=generation_)return fail(Reason::Source);if(!paid){state_=State::Deferred;reason_=Reason::Budget;return result();}state_=State::Building;reason_=Reason::None;
        switch(stage_){
        case Stage::ValidateCatalogue:for(unsigned n=0;n<units;++n){tick();if(!record(catalogue_[cursor_]))return result();++cursor_;++counts_.validated_npcs;}
            if(cursor_==catalogue_count_)transition(Stage::AllocateBlocked);break;
        case Stage::AllocateBlocked:allocate(blocked_,blocked_capacity_,bytes_);tick();transition(Stage::AllocateSpawn);break;
        case Stage::AllocateSpawn:allocate(spawn_,spawn_capacity_,bytes_);tick();transition(Stage::AllocateForeign);break;
        case Stage::AllocateForeign:allocate(foreign_,foreign_capacity_,bytes_);tick();transition(Stage::AllocateDifference);break;
        case Stage::AllocateDifference:if(difference_capacity_<difference_count_){difference_.reset(new std::int32_t[difference_count_]);difference_capacity_=difference_count_;
                counts_.allocated_bytes+=difference_count_*unsigned(sizeof(std::int32_t));++counts_.allocations;}tick();transition(Stage::AllocateBlockedCells);break;
        case Stage::AllocateBlockedCells:allocate(blocked_cells_,blocked_cells_capacity_,cells_);tick();transition(Stage::AllocateSpawnCells);break;
        case Stage::AllocateSpawnCells:allocate(spawn_cells_,spawn_cells_capacity_,cells_);tick();transition(Stage::AllocateForeignCells);break;
        case Stage::AllocateForeignCells:allocate(foreign_cells_,foreign_cells_capacity_,cells_);tick();transition(Stage::CopyTerrain);break;
        case Stage::CopyTerrain:for(unsigned n=0;n<units;++n){blocked_[cursor_]=terrain_.bytes[cursor_];++cursor_;++counts_.copied_bytes;tick();}if(cursor_==bytes_)transition(Stage::CopySpawn);break;
        case Stage::CopySpawn:for(unsigned n=0;n<units;++n){spawn_[cursor_]=spawn_input_.bytes[cursor_];++cursor_;++counts_.copied_bytes;tick();}if(cursor_==bytes_)transition(Stage::ClearForeign);break;
        case Stage::ClearForeign:for(unsigned n=0;n<units;++n){foreign_[cursor_++]=0;++counts_.cleared_bytes;tick();}if(cursor_==bytes_)transition(Stage::ClearDifference);break;
        case Stage::ClearDifference:for(unsigned n=0;n<units;++n){difference_[cursor_++]=0;counts_.cleared_bytes+=sizeof(std::int32_t);tick();}
            if(cursor_==difference_count_)transition(catalogue_count_?Stage::OverlayCatalogue:Stage::Raster);break;
        case Stage::OverlayCatalogue:for(unsigned n=0;n<units;++n){tick();if(!record(catalogue_[cursor_]))return result();overlay(catalogue_[cursor_++]);++counts_.overlaid_npcs;}
            if(cursor_==catalogue_count_)transition(Stage::Raster);break;
        case Stage::Raster:for(unsigned n=0;n<units;++n){tick();if(!raster(cursor_++))return result();}if(cursor_==cells_){state_=State::Complete;stage_=Stage::Complete;
                terrain_={};spawn_input_={};catalogue_={};}break;
        default:return fail(Reason::State);
        }return result();
    }
    // Opt-in leaf scheduling. Legacy advance and storage layout above remain
    // unchanged. Each permit protects ONE actual leaf, never a whole requested
    // chunk. Allocation is still one atomic new[]/reset operation; this API
    // does not subdivide it or establish a native allocation/fuel bound.
    Result advance_guarded(Source current,unsigned max_units,Work work={}) {
        const auto generation=generation_;const auto original_stage=stage_;
        const unsigned limit=std::min(max_units,chunk_limit);unsigned admitted=0;
        do {const auto leaf=advance(current,limit?1u:0u,work);
            // A reentrant begin/cancel revokes the old pure prefix. Never
            // subtract reset counters or graft old units onto a new source.
            if(generation_!=generation){last_units_=0;return fail(Reason::Source);}
            admitted+=leaf.last_units;
            if(leaf.state!=State::Building||stage_!=original_stage||!leaf.last_units||admitted>=limit)break;
        }while(true);
        last_units_=admitted;last_stage_=original_stage;return result();
    }
    std::optional<Product> product(Source current)const{if(state_!=State::Complete||!(current==source_))return {};Product out;out.source=source_;out.generation=generation_;out.owner_=this;
        out.blocked=b::PackedView{int(width_),int(height_),{blocked_.get(),bytes_}};out.own_spawn=b::PackedView{int(width_),int(height_),{spawn_.get(),bytes_}};
        out.foreign=b::PackedView{int(width_),int(height_),{foreign_.get(),bytes_}};
        out.blocked_cells=b::CellView{int(width_),int(height_),{blocked_cells_.get(),cells_}};out.own_spawn_cells=b::CellView{int(width_),int(height_),{spawn_cells_.get(),cells_}};
        out.foreign_cells=b::CellView{int(width_),int(height_),{foreign_cells_.get(),cells_}};return out;}
    bool current(const Product& value,Source expected)const{
        auto view=[&](b::PackedView v,const std::uint8_t* data){return v.width==int(width_)&&v.height==int(height_)&&v.bytes.data()==data&&v.bytes.size()==bytes_;};
        auto cells=[&](b::CellView v,const std::uint8_t* data){return v.width==int(width_)&&v.height==int(height_)&&v.bytes.data()==data&&v.bytes.size()==cells_;};
        return state_==State::Complete&&expected==source_&&value.source==source_&&value.generation==generation_&&value.owner_==this&&
            view(value.blocked,blocked_.get())&&view(value.own_spawn,spawn_.get())&&view(value.foreign,foreign_.get())&&
            cells(value.blocked_cells,blocked_cells_.get())&&cells(value.own_spawn_cells,spawn_cells_.get())&&cells(value.foreign_cells,foreign_cells_.get());}
    void cancel(){next_generation();state_=State::Empty;stage_=Stage::Empty;reason_=Reason::None;terrain_={};spawn_input_={};catalogue_={};last_units_=cursor_=0;}
};
} // namespace wb::capital::service_static_geometry
