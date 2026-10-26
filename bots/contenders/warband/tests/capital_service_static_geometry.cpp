#include "../src/capital_service_static_geometry.h"
#include <array>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

namespace g=wb::capital::service_static_geometry;namespace b=wb::capital::service_bitmaps;
static_assert(!std::is_copy_constructible_v<g::Preparer>&&!std::is_move_constructible_v<g::Preparer>);
static unsigned checks=0,failures=0;
static void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
static void mark(std::span<std::uint8_t> mask,unsigned width,int x,int y){const unsigned cell=unsigned(y)*width+unsigned(x);mask[cell/8]|=std::uint8_t(1u<<(cell%8));}
static bool test(std::span<const std::uint8_t> mask,unsigned cell){return (mask[cell/8]&(1u<<(cell%8)))!=0;}
struct Budget {std::uint64_t units{1000000},paid{},calls{},denied{},max_bytes{};unsigned cell_allocations{};bool step_bytes_valid{true};};
static bool permit(void* context,const g::Step& step){auto& p=*static_cast<Budget*>(context);++p.calls;p.max_bytes=std::max<std::uint64_t>(p.max_bytes,step.bytes);
    if(step.stage==g::Stage::Raster)p.step_bytes_valid=p.step_bytes_valid&&step.bytes==step.units*8;
    if(step.stage==g::Stage::AllocateBlockedCells||step.stage==g::Stage::AllocateSpawnCells||step.stage==g::Stage::AllocateForeignCells){++p.cell_allocations;p.step_bytes_valid=p.step_bytes_valid&&step.units==1&&step.bytes<=10000;}
    if(p.units<step.units){++p.denied;return false;}p.units-=step.units;p.paid+=step.units;return true;}
static bool cancel_work(void* context,const g::Step&){static_cast<g::Preparer*>(context)->cancel();return true;}
static g::Result run(g::Preparer& p,const g::Input& input,unsigned chunk=97,Budget* supplied=nullptr){auto r=p.begin(input);Budget local;auto& budget=supplied?*supplied:local;unsigned calls=0;
    while((r.state==g::State::Building||r.state==g::State::Deferred)&&calls++<1000000){r=p.advance(input.source,chunk,{&budget,&permit});
        check(r.last_units<=std::min(chunk,g::chunk_limit),"every allocation/copy/clear/catalogue/raster chunk is bounded");
        if(r.state!=g::State::Complete)check(!p.product(input.source),"no partial owned geometry is published");if(r.state==g::State::Deferred&&budget.units==0)break;}
    check(calls<1000000,"finite static preparation terminates");return r;}
static bool naive_equal(const g::Product& out,const g::Input& in){const unsigned width=unsigned(in.terrain.width),height=unsigned(in.terrain.height);
    if(!b::validate(out.blocked_cells).valid||!b::validate(out.own_spawn_cells).valid||!b::validate(out.foreign_cells).valid||
        out.blocked_cells.width!=int(width)||out.blocked_cells.height!=int(height)||out.own_spawn_cells.width!=int(width)||out.own_spawn_cells.height!=int(height)||
        out.foreign_cells.width!=int(width)||out.foreign_cells.height!=int(height))return false;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const unsigned cell=y*width+x;bool blocked=test(in.terrain.bytes,cell),foreign=false;
        for(const auto& npc:in.catalogue)if(npc.group!=in.source.candidate_group){blocked=blocked||(npc.position.x==int(x)&&npc.position.y==int(y));
            const int distance=std::max(std::abs(npc.position.x-int(x)),std::abs(npc.position.y-int(y)));foreign=foreign||(npc.damage_positive&&float(distance)<=npc.range);}
        if(test(out.blocked.bytes,cell)!=blocked||test(out.foreign.bytes,cell)!=foreign||test(out.own_spawn.bytes,cell)!=test(in.own_spawn.bytes,cell)||
            out.blocked_cells.bytes[cell]!=unsigned(blocked)||out.foreign_cells.bytes[cell]!=unsigned(foreign)||out.own_spawn_cells.bytes[cell]!=unsigned(test(in.own_spawn.bytes,cell)))return false;}
    return true;
}
struct Case31 {
    std::array<std::uint8_t,121> terrain{},spawn{};std::array<g::SourceNpc,10> npcs{{
        {0,{7,2},0,false},{0,{7,3},0,false},{0,{8,2},3,true},{0,{8,3},3,true},
        {1,{23,28},0,false},{1,{24,28},0,false},{1,{23,27},4,true},{2,{0,30},2.9f,true},
        {2,{0,30},2.9f,true},{3,{30,0},0,true}}};
    Case31(){mark(terrain,31,15,15);mark(terrain,31,30,30);mark(terrain,31,3,20);for(int x=0;x<3;++x)for(int y=0;y<3;++y)mark(spawn,31,x,y);}
    g::Input input(int candidate=0){return {{7,13,candidate},b::PackedView{31,31,terrain},b::PackedView{31,31,spawn},npcs,true};}
};
static void complete_and_tiny(){Case31 data;g::Preparer full,tiny;const auto input=data.input();Budget a,c;const auto one=run(full,input,999999,&a),small=run(tiny,input,1,&c);
    check(one.state==g::State::Complete&&small.state==g::State::Complete,"Packs-sized constructed catalogue completes under one-unit and capped chunks");
    const auto product=full.product(input.source),other=tiny.product(input.source);check(product&&other&&full.current(*product,input.source),"only exact complete source produces a current geometry view");
    check(product&&naive_equal(*product,input)&&other&&naive_equal(*other,input),"clipped rectangle differences equal naive Chebyshev union and all-NPC blocking");
    check(one.counts.work==small.counts.work&&a.paid==one.counts.work&&c.paid==small.counts.work,"chunking preserves work and geometry rather than silently skipping prefixes");
    const auto expected=2*data.npcs.size()+7+3*121+32*32+31*31;
    check(one.counts.work==expected&&one.counts.validated_npcs==10&&one.counts.overlaid_npcs==10&&one.counts.raster_cells==961,"validation, copying, complete difference clear and raster are all counted");
    check(one.counts.copied_bytes==242&&one.counts.cleared_bytes==121+32*32*4&&one.counts.allocations==7&&one.counts.allocated_bytes==3*121+32*32*4+3*961,"all seven allocations and full used-prefix initialization are explicit");
    check(one.counts.cell_writes==3*961&&a.cell_allocations==3&&a.step_bytes_valid&&c.step_bytes_valid,"three owned cell arrays are allocated separately and written during the one funded raster");
    check(product&&!test(product->blocked.bytes,2*31+7)&&test(product->blocked.bytes,28*31+24),"selected source spawn is left to terrain while nonselected harmless NPC still blocks");
    check(product&&b::validate(product->blocked).valid&&b::validate(product->foreign).valid&&b::validate(product->own_spawn).valid,"packed tails remain canonical after all overlays");
    if(product){auto altered=*product;altered.blocked.width=1;check(!full.current(altered,input.source),"generation cannot certify an edited product shape");
        altered=*product;altered.foreign_cells=b::CellView{1,961,product->foreign_cells.bytes};check(!full.current(altered,input.source),"cell-view dimensions are checked even at equal cell count");
        altered=*product;altered.own_spawn_cells=product->blocked_cells;check(!full.current(altered,input.source),"cell-view source pointer cannot be exchanged under an otherwise valid generation");}
    std::array<std::uint8_t,121> repacked{};check(b::pack_cells(product->blocked_cells,b::PackedBuffer{31,31,repacked},{1,1}).state==b::State::Complete&&std::equal(repacked.begin(),repacked.end(),product->blocked.bytes.begin()),"owned blocked cells roundtrip to the exact packed prefix");
    check(b::pack_cells(product->own_spawn_cells,b::PackedBuffer{31,31,repacked},{1,1}).state==b::State::Complete&&std::equal(repacked.begin(),repacked.end(),product->own_spawn.bytes.begin()),"owned spawn cells roundtrip to the exact packed prefix");
    check(b::pack_cells(product->foreign_cells,b::PackedBuffer{31,31,repacked},{1,1}).state==b::State::Complete&&std::equal(repacked.begin(),repacked.end(),product->foreign.bytes.begin()),"owned foreign cells roundtrip to the exact packed prefix");
    auto wrong=input.source;++wrong.catalogue_revision;check(!full.product(wrong)&&!full.current(*product,wrong),"catalogue revision is part of the cache identity");
    const auto copied=product->blocked.bytes[0],cell=product->own_spawn_cells.bytes[0];data.terrain[0]=255;data.spawn[0]=0;check(product->blocked.bytes[0]==copied&&product->own_spawn.bytes[0]!=0&&product->own_spawn_cells.bytes[0]==cell,"completed prefix owns packed and cell masks instead of borrowing input storage");
}
static void refusals_and_reuse(){Case31 data;g::Preparer p;auto input=data.input();auto r=p.begin(input);check(r.counts.work==0&&r.counts.allocations==0&&!p.product(input.source),"begin allocates and clears no hidden full arrays");
    Budget none{0};r=p.advance(input.source,4,{&none,&permit});check(r.state==g::State::Deferred&&r.counts.work==0&&r.counts.allocated_bytes==0&&!p.product(input.source),"budget refusal mutates no output prefix");
    Budget funded;r=p.advance(input.source,4,{&funded,&permit});check(r.counts.validated_npcs==4&&r.last_units==4,"funded validation resumes at the retained exact record cursor");
    unsigned calls=0;while(r.state==g::State::Building&&calls++<10000)r=p.advance(input.source,11,{&funded,&permit});check(r.state==g::State::Complete,"funded continuation closes previously deferred preparation");
    const auto old=*p.product(input.source);auto changed=data.input(1);r=p.begin(changed);check(!p.current(old,input.source)&&!p.product(changed.source),"new candidate revokes old borrowed views before touching storage");
    while(r.state==g::State::Building)r=p.advance(changed.source,512);check(r.state==g::State::Complete&&naive_equal(*p.product(changed.source),changed),"retained allocation is fully reinitialized for another candidate");
    const auto fresh=*p.product(changed.source);p.cancel();check(!p.current(fresh,changed.source)&&!p.product(changed.source),"cancellation revokes every previous publication");
    r=p.begin(input);r=p.advance(input.source,1,{&p,&cancel_work});check(r.state==g::State::Invalid&&r.reason==g::Reason::Source&&!p.product(input.source),"callback cancellation is fenced before any staged mutation");
    run(p,input);const auto previous=*p.product(input.source);g::Input alias{data.input(2).source,previous.blocked,previous.own_spawn,data.npcs,true};
    check(p.begin(alias).reason==g::Reason::Alias&&!p.current(previous,input.source),"old owned output cannot be reused as borrowed mutable input to itself");
    std::array<std::uint8_t,1> zero{{0}},spawn{{1}};const g::Input small{{8,14,2},b::PackedView{1,1,zero},b::PackedView{1,1,spawn},{},true};r=run(p,small,1);
    const auto tiny=p.product(small.source);check(r.state==g::State::Complete&&r.counts.allocations==0&&tiny&&tiny->blocked.bytes.size()==1&&tiny->blocked.bytes[0]==0&&tiny->foreign.bytes[0]==0&&tiny->own_spawn.bytes[0]==1&&
        tiny->blocked_cells.bytes.size()==1&&tiny->blocked_cells.bytes[0]==0&&tiny->foreign_cells.bytes[0]==0&&tiny->own_spawn_cells.bytes[0]==1&&r.counts.cell_writes==3,"large-to-small reuse cannot publish poisoned packed/cell/difference tails");
    auto stale=small.source;++stale.map_revision;check(p.advance(stale,1).reason==g::Reason::Source&&!p.product(small.source),"wrong source revokes completed state rather than returning stale geometry");
}
static void malformed(){Case31 data;g::Preparer p;auto input=data.input();input.complete_catalogue=false;check(p.begin(input).state==g::State::Unknown&&!p.product(input.source),"incomplete catalogue is Unknown, never a false empty foreign area");
    input=data.input();input.source.map_revision=0;check(p.begin(input).reason==g::Reason::Source,"map and catalogue source keys are required");
    input=data.input();input.own_spawn=b::PackedView{1,961,data.spawn};check(p.begin(input).reason==g::Reason::Shape,"equal packed size does not erase different map dimensions");
    data.terrain.back()|=128;input=data.input();check(p.begin(input).reason==g::Reason::Tail,"invalid packed input tail remains an explicit rejection");data.terrain.back()=1;
    data.npcs[0].range=std::numeric_limits<float>::quiet_NaN();input=data.input();auto r=run(p,input);check(r.state==g::State::Invalid&&r.reason==g::Reason::Range&&!p.product(input.source)&&r.counts.allocations==0,"NaN range cannot become a clipped empty footprint");
    data.npcs[0].range=-1;r=run(p,input);check(r.state==g::State::Invalid&&r.reason==g::Reason::Range,"negative ranges are malformed even on selected sources");
    data.npcs[0].range=0;data.npcs[0].position={31,0};r=run(p,input);check(r.state==g::State::Invalid&&r.reason==g::Reason::Point,"off-map catalogue center cannot be clamped into fictitious geometry");
    data.npcs[0].position={7,2};data.npcs[0].group=-1;r=run(p,input);check(r.state==g::State::Invalid&&r.reason==g::Reason::Group,"unknown NPC group cannot silently join the foreign set");
    std::vector<g::SourceNpc> too_many(g::npc_limit+1,{1,{0,0},0,false});input=data.input();input.catalogue=too_many;r=p.begin(input);check(r.reason==g::Reason::Capacity&&r.state==g::State::Unknown,"catalogue capacity is explicit, with no truncation");
}
static void deny_every_stage(){Case31 data;g::Preparer p;const auto input=data.input();auto r=p.begin(input);unsigned stages=0,calls=0;
    while(r.state==g::State::Building&&calls++<1000){const auto before=r;stages|=1u<<unsigned(before.stage);Budget none{0};r=p.advance(input.source,1024,{&none,&permit});
        check(r.state==g::State::Deferred&&r.stage==before.stage&&r.last_units==0&&r.counts.work==before.counts.work&&r.counts.allocated_bytes==before.counts.allocated_bytes&&
            r.counts.copied_bytes==before.counts.copied_bytes&&r.counts.cleared_bytes==before.counts.cleared_bytes&&r.counts.cell_writes==before.counts.cell_writes&&!p.product(input.source),"every validation/allocation/copy/clear/overlay/raster stage obeys denial before mutation");
        r=p.advance(input.source,1024);}
    unsigned expected=0;for(unsigned stage=unsigned(g::Stage::ValidateCatalogue);stage<=unsigned(g::Stage::Raster);++stage)expected|=1u<<stage;
    check(r.state==g::State::Complete&&stages==expected&&naive_equal(*p.product(input.source),input),"all explicit stages resume from denied state without dropping any geometry work");
}
static void cell_growth_and_partial_raster(){g::Preparer p;std::array<std::uint8_t,1> empty{{0}},spawn{{1}};
    const g::Input tiny{{101,102,0},b::PackedView{1,1,empty},b::PackedView{1,1,spawn},{},true};auto r=run(p,tiny,1);const auto old=*p.product(tiny.source);
    check(r.counts.allocations==7&&r.counts.cell_writes==3,"small geometry allocates exactly three owned cell arrays without an implicit clear");
    Case31 data;const auto input=data.input();r=p.begin(input);check(!p.current(old,tiny.source),"growth invalidates all previous packed and cell views");
    while(r.state==g::State::Building&&r.stage!=g::Stage::Raster)r=p.advance(input.source,1024);
    check(r.stage==g::Stage::Raster&&r.counts.allocations==7&&r.counts.cell_writes==0&&!p.product(input.source),"larger cell allocations finish before any raster cell is written or exposed");
    r=p.advance(input.source,13);check(r.state==g::State::Building&&r.counts.raster_cells==13&&r.counts.cell_writes==39&&!p.product(input.source),"partial raster funds exactly three stores per visited cell and publishes no partial cell view");
    while(r.state==g::State::Building)r=p.advance(input.source,1024);check(r.state==g::State::Complete&&r.counts.raster_cells==961&&r.counts.cell_writes==2883&&naive_equal(*p.product(input.source),input),"retained raster completes all route cells without a second conversion pass");
    const auto complete=*p.product(input.source);g::Input alias{data.input(1).source,b::PackedView{31,31,complete.own_spawn_cells.bytes.first(121)},b::PackedView{31,31,data.spawn},data.npcs,true};
    check(p.begin(alias).reason==g::Reason::Alias&&!p.current(complete,input.source),"owned cell storage cannot alias a newly borrowed packed input");
    std::vector<std::uint8_t> terrain(1250,255),home(1250,0);mark(home,100,99,99);const std::array<g::SourceNpc,1> npc{{{1,{50,50},0,true}}};
    const g::Input large{{103,104,0},b::PackedView{100,100,terrain},b::PackedView{100,100,home},npc,true};r=run(p,large,511);
    check(r.state==g::State::Complete&&r.counts.allocations==7&&r.counts.cell_writes==30000&&naive_equal(*p.product(large.source),large),"all three cell buffers reallocate and overwrite the whole grown map");
}
static void rectangles_and_large(){std::vector<std::uint8_t> terrain(1250,0),spawn(1250,0);mark(spawn,100,0,0);g::Preparer p;
    std::array<g::SourceNpc,6> npcs{{{1,{0,0},.99f,true},{1,{99,99},1.5f,true},{2,{50,50},3.75f,true},{2,{50,50},3.75f,true},{3,{8,7},1000000.f,false},{0,{80,80},1e30f,true}}};
    g::Input input{{20,21,0},b::PackedView{100,100,terrain},b::PackedView{100,100,spawn},npcs,true};auto r=run(p,input,37);
    check(r.state==g::State::Complete&&naive_equal(*p.product(input.source),input),"100x100 edge clipping, fractional radius and duplicate rectangles match naive union");
    check(r.difference_entries==10201&&r.difference_entries<=2*r.cells+2,"all extra-row/column difference storage is bounded and explicitly cleared");
    std::vector<g::SourceNpc> many(10000,{1,{0,0},std::numeric_limits<float>::max(),true});input.catalogue=many;r=run(p,input,1024);
    const auto full=p.product(input.source);bool filled=full.has_value();if(full){for(auto byte:full->foreign.bytes)filled=filled&&byte==255;for(auto cell:full->foreign_cells.bytes)filled=filled&&cell==1;}
    check(r.state==g::State::Complete&&filled&&r.counts.positive_rectangles==10000&&r.counts.raster_cells==10000,"10000 overlapping huge finite rectangles remain exact without N*area rasterization");
    check(r.counts.work==2*10000+7+3*1250+10201+10000&&r.counts.cell_writes==30000,"all large-catalogue work includes final cell outputs in the same O(catalogue+map area) pass");
    std::vector<std::uint8_t> line(1250,0);input={{22,23,0},b::PackedView{10000,1,line},b::PackedView{10000,1,line},{},true};r=run(p,input,1024);
    bool cleared=true;for(auto cell:p.product(input.source)->foreign_cells.bytes)cleared=cleared&&cell==0;
    check(r.state==g::State::Complete&&r.difference_entries==20002&&r.counts.cleared_bytes==1250+20002*4&&r.counts.cell_writes==30000&&cleared,"one-row maximum difference clear and previously poisoned cell outputs are fully counted");
}
struct LeafBudget {unsigned allow{1000000},calls{},granted{},denied{},raster_granted{},atomic_allocations{};bool one_leaf{true},same_stage{true};g::Stage expected{g::Stage::Empty};};
static bool leaf_permit(void* pointer,const g::Step& step){auto& l=*static_cast<LeafBudget*>(pointer);++l.calls;l.one_leaf=l.one_leaf&&step.units==1;l.same_stage=l.same_stage&&step.stage==l.expected;
    if(!l.allow){++l.denied;return false;}--l.allow;++l.granted;if(step.stage==g::Stage::Raster){++l.raster_granted;l.one_leaf=l.one_leaf&&step.bytes==8;}
    if(step.stage>=g::Stage::AllocateBlocked&&step.stage<=g::Stage::AllocateForeignCells)++l.atomic_allocations;return true;}
static bool same_counts(const g::Counts& a,const g::Counts& b){return a.work==b.work&&a.allocated_bytes==b.allocated_bytes&&a.copied_bytes==b.copied_bytes&&a.cleared_bytes==b.cleared_bytes&&
    a.validated_npcs==b.validated_npcs&&a.overlaid_npcs==b.overlaid_npcs&&a.blocked_sources==b.blocked_sources&&a.positive_rectangles==b.positive_rectangles&&a.raster_cells==b.raster_cells&&a.allocations==b.allocations&&a.cell_writes==b.cell_writes;}
static bool same_product(const g::Product& a,const g::Product& b){return std::equal(a.blocked.bytes.begin(),a.blocked.bytes.end(),b.blocked.bytes.begin(),b.blocked.bytes.end())&&
    std::equal(a.own_spawn.bytes.begin(),a.own_spawn.bytes.end(),b.own_spawn.bytes.begin(),b.own_spawn.bytes.end())&&std::equal(a.foreign.bytes.begin(),a.foreign.bytes.end(),b.foreign.bytes.begin(),b.foreign.bytes.end())&&
    std::equal(a.blocked_cells.bytes.begin(),a.blocked_cells.bytes.end(),b.blocked_cells.bytes.begin(),b.blocked_cells.bytes.end())&&std::equal(a.own_spawn_cells.bytes.begin(),a.own_spawn_cells.bytes.end(),b.own_spawn_cells.bytes.begin(),b.own_spawn_cells.bytes.end())&&
    std::equal(a.foreign_cells.bytes.begin(),a.foreign_cells.bytes.end(),b.foreign_cells.bytes.begin(),b.foreign_cells.bytes.end());}
static g::Result run_guarded(g::Preparer& p,const g::Input& in,unsigned chunk,LeafBudget& budget){auto r=p.begin(in);unsigned calls=0;
    while((r.state==g::State::Building||r.state==g::State::Deferred)&&calls++<100000){budget.expected=r.stage;const auto stage=r.stage;const unsigned before=budget.granted;
        r=p.advance_guarded(in.source,chunk,{&budget,&leaf_permit});check(r.last_units==budget.granted-before&&r.last_units<=std::min(chunk,g::chunk_limit),"guarded last_units equals admitted one-leaf permits");
        check(r.last_stage==stage,"guarded call retains its original stage in aggregate result");if(r.state!=g::State::Complete)check(!p.product(in.source),"guarded partial prefix has no Product");
        if(r.state==g::State::Deferred&&!budget.allow)break;}
    check(calls<100000,"finite leaf-guarded preparation terminates");return r;
}
static void guarded_equivalence(){Case31 data;const auto input=data.input();g::Preparer legacy,chunked,singles;const auto old=run(legacy,input,128);LeafBudget a,budget;
    const auto guarded=run_guarded(chunked,input,128,a),one=run_guarded(singles,input,1,budget);
    check(guarded.state==g::State::Complete&&one.state==g::State::Complete&&same_counts(old.counts,guarded.counts)&&same_counts(old.counts,one.counts),"legacy, guarded chunk and one-unit paths have exact final counts");
    check(same_product(*legacy.product(input.source),*chunked.product(input.source))&&same_product(*legacy.product(input.source),*singles.product(input.source)),"legacy and guarded paths produce identical packed and cell bits");
    check(a.one_leaf&&a.same_stage&&budget.one_leaf&&budget.same_stage&&a.raster_granted==961&&a.calls==guarded.counts.work,"every actual raster leaf receives exactly one permit and never crosses a stage in one guarded call");
    check(a.atomic_allocations==7&&budget.atomic_allocations==7,"allocation stays one atomic leaf, not a claimed subdivided native allocation");
    const auto before=a.calls;const auto done=chunked.advance_guarded(input.source,128,{&a,&leaf_permit});check(done.state==g::State::Complete&&done.last_units==0&&a.calls==before,"complete guarded preparation requests no later leaf");
}
static g::Result to_raster(g::Preparer& p,const g::Input& input){auto r=p.begin(input);while(r.state==g::State::Building&&r.stage!=g::Stage::Raster)r=p.advance(input.source,1024);return r;}
static void guarded_denial_resume(){Case31 data;const auto input=data.input();g::Preparer p;const auto prefix=to_raster(p,input);LeafBudget leaf;leaf.allow=5;leaf.expected=g::Stage::Raster;
    auto r=p.advance_guarded(input.source,128,{&leaf,&leaf_permit});
    check(r.state==g::State::Deferred&&r.reason==g::Reason::Budget&&r.last_units==5&&r.counts.work==prefix.counts.work+5&&r.counts.raster_cells==5&&r.counts.cell_writes==15,"denial after five leaves retains exactly the admitted raster prefix");
    check(leaf.calls==6&&leaf.granted==5&&leaf.denied==1&&!p.product(input.source),"first refusal stops the call before any later leaf or publication");
    const auto before=r.counts;r=p.advance_guarded(input.source,128,{&leaf,&leaf_permit});check(r.last_units==0&&same_counts(r.counts,before)&&leaf.calls==7,"repeat denial performs only its one refused guard");
    leaf.allow=3;r=p.advance_guarded(input.source,128,{&leaf,&leaf_permit});check(r.last_units==3&&r.counts.raster_cells==8&&leaf.calls==11,"resume continues from exact retained cursor and stops at the next refusal");
    leaf.allow=100000;while(r.state==g::State::Building||r.state==g::State::Deferred)r=p.advance_guarded(input.source,128,{&leaf,&leaf_permit});
    check(r.state==g::State::Complete&&r.counts.raster_cells==961&&r.counts.cell_writes==2883&&leaf.raster_granted==961&&naive_equal(*p.product(input.source),input),"refused guarded raster later completes every cell exactly once");
    g::Preparer old;to_raster(old,input);Budget batch{128};const auto legacy=old.advance(input.source,128,{&batch,&permit});
    check(legacy.last_units==128&&batch.calls==1&&legacy.counts.raster_cells==128,"legacy advance keeps its one-grant whole-chunk behavior unchanged");
    g::Preparer zero;auto empty=zero.begin(input);LeafBudget none;const auto zero_result=zero.advance_guarded(input.source,0,{&none,&leaf_permit});
    check(zero_result.state==g::State::Deferred&&zero_result.last_units==0&&same_counts(zero_result.counts,empty.counts)&&none.calls==0,"zero max_units invokes no guard and admits no work");
}
struct Reentrant {g::Preparer* preparer{};g::Input replacement{};unsigned calls{},on{3};bool begin{};};
static bool reentrant(void* pointer,const g::Step& step){auto& r=*static_cast<Reentrant*>(pointer);check(step.units==1,"reentrant test also receives true one-leaf Step");
    if(++r.calls==r.on){if(r.begin)r.preparer->begin(r.replacement);else r.preparer->cancel();}return true;}
static void guarded_generation(){Case31 data;const auto input=data.input();
    for(unsigned mode=0;mode<3;++mode){g::Preparer p;const auto before=to_raster(p,input);Reentrant callback;callback.preparer=&p;callback.begin=mode!=0;callback.replacement=mode==2?data.input(1):input;
        const auto r=p.advance_guarded(input.source,128,{&callback,&reentrant});
        check(r.state==g::State::Invalid&&r.reason==g::Reason::Source&&r.last_units==0&&callback.calls==3&&!p.product(input.source)&&!p.product(callback.replacement.source),"cancel/new-source/same-source begin revoke old prefix with zero returned authority and no later leaf");
        check(r.generation>before.generation&&r.counts.work<(std::uint64_t(1)<<60),"generation reset cannot create unsigned work-difference underflow");
        if(callback.begin)check(r.counts.work==0&&r.counts.raster_cells==0&&r.counts.cell_writes==0,"reentrant begin reports only reset current-generation counters");
        else check(r.counts.work==before.counts.work+2&&r.counts.raster_cells==2&&r.counts.cell_writes==6,"cancel preserves current counters but grants no revoked last_units");
        const auto retry=run(p,callback.replacement);check(retry.state==g::State::Complete&&naive_equal(*p.product(callback.replacement.source),callback.replacement),"explicit new preparation can recover after reentrant invalidation");
    }
}
int main(){complete_and_tiny();refusals_and_reuse();malformed();deny_every_stage();cell_growth_and_partial_raster();rectangles_and_large();guarded_equivalence();guarded_denial_resume();guarded_generation();std::printf("capital_service_static_geometry checks %u failures %u\n",checks,failures);return failures?1:0;}
