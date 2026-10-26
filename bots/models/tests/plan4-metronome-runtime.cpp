#include "../metronome/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace mb=plan4::runtime::metronome_binding;
namespace {
int checks=0;
void require(bool okay,const char* why){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct World {
    int width=12,height=12,round=20,queries=0,terrain_calls=0,resource_reads=0;
    int deny_after=-1,work_calls=0,advance_on_read=-1,increase_on_read=-1;
    int clamp_on_read=-1;
    std::uint64_t fuel=100000,lookup_cost=0,terrain_cost=0;
    std::uint64_t clamped_fuel=65000;
    bool allowed=true,hide_spawn=true,hide_all=false,hide_cross=false;
    Point camps[2]{{2,2},{8,8}},altars[2]{{2,3},{8,9}};
    Point spawn{0,0};
    std::array<unsigned char,2500> blocked{};
    std::array<int,144*144> distances{};
    void prepare(){
        const int count=width*height;
        if(count>144)return;
        std::array<int,144> queue;
        for(int start=0;start<count;++start){auto* d=distances.data()+start*144;std::fill_n(d,count,-1);
            if(blocked[start])continue;
            int head=0,tail=0;queue[tail++]=start;d[start]=0;
            while(head<tail){const int p=queue[head++];for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const int x=p%width+dx,y=p/width+dy;if((!dx&&!dy)||x<0||x>=width||y<0||y>=height)continue;
                const int key=y*width+x;if(!blocked[key]&&d[key]<0){d[key]=d[p]+1;queue[tail++]=key;}}}
        }
    }
};
bool work(const void* p){auto& w=*const_cast<World*>(static_cast<const World*>(p));++w.work_calls;
    return w.allowed&&(w.deny_after<0||w.work_calls<w.deny_after);}
bool walkable(Point p,const void* c){auto& w=*const_cast<World*>(static_cast<const World*>(c));++w.terrain_calls;
    w.fuel-=std::min(w.fuel,w.terrain_cost);
    return p.x>=0&&p.y>=0&&p.x<w.width&&p.y<w.height&&!w.blocked[p.y*w.width+p.x];}
int travel(Point a,Point b,const void* p){auto& w=*const_cast<World*>(static_cast<const World*>(p));++w.queries;
    w.fuel-=std::min(w.fuel,w.lookup_cost);
    if(a==b)return 0;
    if(w.hide_all)return -1;
    if(w.hide_cross)for(int g=0;g<2;++g)if((a==w.camps[g]&&b==w.altars[1-g])||(b==w.camps[g]&&a==w.altars[1-g]))return -1;
    // Only spawn-to-field routes are missing; the retained common menu still
    // has the actual cached own->camp and camp->altar distances.
    if(w.hide_spawn&&(a==w.spawn||b==w.spawn))return -1;
    if(a.x<0||a.y<0||b.x<0||b.y<0||a.x>=w.width||b.x>=w.width||a.y>=w.height||b.y>=w.height)return -1;
    return w.distances[(a.y*w.width+a.x)*144+b.y*w.width+b.x];
}
mb::Resources resources(const void* p){auto& w=*const_cast<World*>(static_cast<const World*>(p));++w.resource_reads;
    if(w.resource_reads==w.advance_on_read)++w.round;
    if(w.resource_reads==w.increase_on_read)w.fuel+=100000;
    if(w.resource_reads==w.clamp_on_read)w.fuel=w.clamped_fuel;
    return {w.round,w.fuel,true};}
mb::Profile profile(){mb::Profile p;
    p.law={77,100,metronome::ClockLaw::engine_calendar_and_phase_order,true,true};p.limitation={101,77,true,true,true};
    p.cost={102,77,1000,8,true};p.identity=200;p.geometry_identity=201;p.origin=mb::Origin::synthetic_test;p.production_frozen=true;return p;}
struct Fixture {
    World world;
    Information info;
    Menu menu;
    rt::SpatialFrame frame;
    Fixture(){world.prepare();info.round=world.round;info.own.id=7;info.own.position={8,8};info.own_spawn=world.spawn;
        info.own.health=400;info.own.maximum_health=1000;info.own.recall_safe=true;
        info.settings.max_rounds=2000;info.settings.winning_gems=10000;info.settings.tribute_multiplier=0.1;
        info.group_count=2;info.altar_count=1;info.altars[0].id=90;info.altars[0].position={8,7};info.altars[0].lock_known=true;
        for(int g=0;g<2;++g){auto& c=info.groups[g];c.id=30+g;c.position={2+g*2,2};c.full_health=c.health=100;
            c.clear_rounds=c.full_clear_rounds=4;c.incoming_damage=c.full_incoming_damage=20;c.gems=20+g*40;c.gold=5;
            c.lifecycle=Lifecycle::living;c.visible=c.all_spawns_visible=c.occupancy_known=true;c.respawn_rounds=30;
            c.provenance=c.health_provenance={info.round,Source::personal_vision,Status::exact_now};}
        info.travel={travel,&world};info.work_budget={work,&world};
        // A deterministic common-style frozen menu. No candidate is added or
        // removed by Metronome; all kinds and an ordered pair are retained.
        menu.count=6;menu.single_count=2;
        menu.jobs[0]={JobKind::farm,0,-1,0,0,12,30};menu.jobs[1]={JobKind::farm,1,-1,0,1,10,30};
        menu.jobs[2]={JobKind::farm,0,1,0,2,16,50};menu.jobs[3]={JobKind::bank,-1,-1,0,3,1,1};
        menu.jobs[4]={JobKind::renew,-1,-1,-1,4,0,6};menu.jobs[5]={JobKind::hold,-1,-1,-1,5,0,1};
        frame.clock={info.round,{info.own.id}};frame.information=&info;frame.menu=&menu;
        frame.topology={world.width,world.height,true,walkable,&world};frame.travel=info.travel;
        frame.graph_quota=192;frame.reference_vertices_used=0;frame.optional_fuel_floor=50000;
    }
    void bind(rt::Metronome& model,mb::Profile p=profile()){
        model.configure(p);model.observe_resources({resources,&world});model.observe_spatial(frame);model.observe_snapshot(info);
    }
};
void same(const Forecast& a,const Forecast& b){
    require(a.event_count==b.event_count&&a.final_round==b.final_round&&a.final_bag==b.final_bag&&a.final_gold==b.final_gold&&
        a.final_health==b.final_health&&a.terminal_z==b.terminal_z&&a.our_secured==b.our_secured&&a.enemy_secured==b.enemy_secured&&
        a.first_conversion_round==b.first_conversion_round&&a.abort_reason==b.abort_reason&&a.truncated==b.truncated&&
        a.travel_unknown==b.travel_unknown&&a.stock_unknown==b.stock_unknown&&a.budget_exhausted==b.budget_exhausted,"whole forecast equals ordinary M0");
    for(int n=0;n<a.event_count;++n){const auto& x=a.events[n];const auto& y=b.events[n];
        require(x.kind==y.kind&&x.start.round==y.start.round&&x.start.phase==y.start.phase&&x.end.round==y.end.round&&x.end.phase==y.end.phase&&
            x.group_id==y.group_id&&x.bag_before==y.bag_before&&x.bag_after==y.bag_after&&x.secured==y.secured&&x.gold_after==y.gold_after&&
            x.health_after==y.health_after&&x.status==y.status,"complete chronological events equal M0");}
}
void m0_equal(Fixture& f,rt::Metronome& model){auto expected=std::make_unique<Evaluation>(),actual=std::make_unique<Evaluation>();
    plan4::evaluate_into(f.info,f.menu,*expected);model.evaluate_into(f.info,f.menu,*actual);
    require(actual->evaluated_count==expected->evaluated_count&&actual->chosen==expected->chosen,"fallback keeps exact fixed menu and selector");
    for(int n=0;n<expected->evaluated_count;++n)same(actual->forecasts[n],expected->forecasts[n]);
    require(!model.model_evidence().supported,"fallback never reports supported refinement");}
}
int main(){
    {auto f=std::make_unique<Fixture>();rt::Metronome model;f->bind(model);m0_equal(*f,model);
        require(f->world.resource_reads==0&&model.spatial_vertices()==0,"synthetic measurements cannot enable ordinary production build");}
    for(int defect=0;defect<7;++defect){auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);auto p=profile();
        if(defect==0)p.cost.measured_operations=0;
        if(defect==1)p.limitation.competing_refinements_exceed_reserve=false;
        if(defect==2)p.law.receipt_source=0;
        if(defect==3)p.production_frozen=false;
        if(defect==4)p.cost.law_identity=76;
        if(defect==5)p.origin=mb::Origin::missing;
        if(defect==6)p.law.clock=metronome::ClockLaw::unsupported;
        f->bind(model,p);m0_equal(*f,model);require(f->world.resource_reads==0,"missing retained prerequisite avoids optional resource queries");}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->bind(model);f->info.own.health-=1;m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->frame.clock.round--;f->bind(model);m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->frame.reference_vertices_used=257;f->bind(model);m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->frame.reference_vertices_used=240;f->frame.graph_quota=17;f->bind(model);m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->frame.topology.complete=false;f->bind(model);m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->bind(model);model.observe_resources({});m0_equal(*f,model);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->world.hide_all=true;f->bind(model);m0_equal(*f,model);
        require(model.model_evidence().reason==int(mb::Reason::bounds),"unproved finite upper bound never becomes a profitable completed tuple");}
    for(bool enabled:{false,true}){
        auto f=std::make_unique<Fixture>();rt::Metronome model(enabled,mb::Use::conditional_test);f->bind(model);auto out=std::make_unique<Evaluation>();
        model.evaluate_into(f->info,f->menu,*out);
        require(out->evaluated_count==f->menu.count&&!out->budget_exhausted,"all fixed job kinds receive complete same-law forecasts");
        require(model.model_evidence().supported,"synthetic conditional profile applies declared allocation law");
        require(model.model_evidence().coupling_enabled==enabled,"only named priority allocation is disabled in control");
        require(model.spatial_vertices()<=192&&model.refinements()<=8,"same optional graph allowance and eight-operation cap");
        require(out->chosen==choose(f->info,f->menu,out->forecasts),"shared exact selector ranks completed publication");
        auto baseline=std::make_unique<Evaluation>();plan4::evaluate_into(f->info,f->menu,*baseline);
        require(out->forecasts[4].our_secured>baseline->forecasts[4].our_secured&&model.spatial_vertices()>0,
            "completed pending spawn route publishes productive common renewal and full fixed tail");
    }
    for(int reference:{0,80,240,256}){
        auto f=std::make_unique<Fixture>();rt::Metronome model(false,mb::Use::conditional_test);
        f->frame.reference_vertices_used=reference;f->frame.graph_quota=std::min(192,256-reference);f->bind(model);
        auto out=std::make_unique<Evaluation>();model.evaluate_into(f->info,f->menu,*out);
        require(model.spatial_vertices()+reference<=256&&model.spatial_vertices()<=f->frame.graph_quota,"actual reference visits debit shared pool before optional work");
        require(out->evaluated_count==f->menu.count,"movement reservation is not falsely counted as visited vertices");
    }
    {
        int choices[2]{-1,-1};unsigned secured[2]{};int visits[2]{};
        for(bool enabled:{false,true}){auto f=std::make_unique<Fixture>();auto& w=f->world;
            w.camps[1]={4,2};w.altars[1]={4,3};
            w.hide_spawn=false;w.hide_cross=true;w.lookup_cost=1;w.terrain_cost=1;w.clamp_on_read=2;
            f->info.own.position={10,5};f->info.own.health=1000;f->info.altar_count=2;
            for(int g=0;g<2;++g){auto& camp=f->info.groups[g];camp.position=w.camps[g];camp.clear_rounds=camp.full_clear_rounds=1;
                camp.incoming_damage=camp.full_incoming_damage=0;camp.respawn_rounds=15;camp.gems=g?70:20;
                f->info.altars[g].id=90+g;f->info.altars[g].position=w.altars[g];f->info.altars[g].accessible=f->info.altars[g].lock_known=true;}
            f->menu=make_menu(f->info);f->frame.menu=&f->menu;
            auto p=profile();p.cost.maximum_measured_cost=10000;
            rt::Metronome model(enabled,mb::Use::conditional_test);f->bind(model,p);auto out=std::make_unique<Evaluation>();
            model.evaluate_into(f->info,f->menu,*out);
            require(model.model_evidence().supported&&out->evaluated_count==f->menu.count,"two-altar partial cache keeps actual common-generated menu and all complete candidates");
            require(model.refinements()==1,"identical 1.5 measured-cost reserve admits exactly one competing refinement");
            choices[enabled]=out->chosen;secured[enabled]=out->forecasts[out->chosen].our_secured;visits[enabled]=model.spatial_vertices();
            auto baseline=std::make_unique<Evaluation>();plan4::evaluate_into(f->info,f->menu,*baseline);
            bool changed=false;for(int n=0;n<f->menu.count;++n)changed|=out->forecasts[n].our_secured!=baseline->forecasts[n].our_secured;
            require(changed,"only a completed three-rollout operation replaces its candidate's ordinary nominal forecast");
        }
        require(visits[0]<=32&&visits[1]<=32,"both allocation laws use the identical per-operation32-vertex cap");
        require(choices[0]==3&&choices[1]==1&&secured[0]==300&&secured[1]==280,
            "synthetic competing cross-altar routes produce distinct priority and round-robin completed choices without asserting benefit");
    }
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->world.hide_spawn=false;f->bind(model);auto out=std::make_unique<Evaluation>();
        model.evaluate_into(f->info,f->menu,*out);require(model.refinements()==0&&model.spatial_vertices()==0,"fully exact distances need no speculative graph work");
        auto baseline=std::make_unique<Evaluation>();plan4::evaluate_into(f->info,f->menu,*baseline);
        for(int n=0;n<f->menu.count;++n)same(out->forecasts[n],baseline->forecasts[n]);}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(false,mb::Use::conditional_test);
        f->info.group_count=1;f->menu.jobs[1]=f->menu.jobs[4];f->menu.jobs[2]=f->menu.jobs[5];f->menu.count=3;f->menu.single_count=1;
        f->bind(model);auto out=std::make_unique<Evaluation>();
        model.evaluate_into(f->info,f->menu,*out);const auto secured=out->forecasts[1].our_secured;
        require(model.spatial_vertices()>0,"first decision completes an actual public terrain route");
        ++f->info.round;++f->world.round;f->frame.clock.round=f->info.round;
        for(int g=0;g<f->info.group_count;++g)f->info.groups[g].provenance.observed_round=f->info.groups[g].health_provenance.observed_round=f->info.round;
        f->bind(model);model.evaluate_into(f->info,f->menu,*out);
        require(out->evaluated_count==f->menu.count&&out->forecasts[1].our_secured==secured&&model.spatial_vertices()==0&&model.refinements()>0,
            "new physical epoch recomputes completed forecasts while reusing only immutable terrain routes");}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->world.fuel=51499;f->bind(model);auto out=std::make_unique<Evaluation>();
        model.evaluate_into(f->info,f->menu,*out);require(model.refinements()==0,"less than 1.5 measured maximum plus identical reserve denies optional operation");}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(false,mb::Use::conditional_test);f->world.terrain_cost=100;
        auto p=profile();p.cost.maximum_measured_cost=1;f->bind(model,p);m0_equal(*f,model);
        require(model.model_evidence().reason==int(mb::Reason::cost),"operation exceeding frozen measured envelope invalidates all private publication");}
    for(int defect=0;defect<3;++defect){auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);
        if(defect==0)f->world.allowed=false;
        if(defect==1)f->world.advance_on_read=2;
        if(defect==2)f->world.increase_on_read=2;
        f->bind(model);auto out=std::make_unique<Evaluation>();model.evaluate_into(f->info,f->menu,*out);
        require(out->budget_exhausted&&out->evaluated_count==0&&!model.model_evidence().supported,"denied budget or suspended/nonmonotone resource clock cannot publish an old decision");}
    {auto f=std::make_unique<Fixture>();rt::Metronome model(true,mb::Use::conditional_test);f->world.deny_after=10;f->bind(model);
        auto out=std::make_unique<Evaluation>();model.evaluate_into(f->info,f->menu,*out);
        require(out->budget_exhausted&&out->evaluated_count==0&&model.spatial_vertices()<=192,"denied inner rollout discards complete prefix consistently");}
    {auto world=std::make_unique<World>();auto& w=*world;w.width=w.height=50;w.prepare();metronome::Graph graph;
        require(graph.bind({50,50,true,walkable,&w},99),"large public terrain initializes without a hidden search");
        int distance=-1,total=0;
        for(int turn=0;turn<100&&distance<0;++turn){graph.begin_turn();distance=graph.advance({0,0},{49,49},32,{work,&w});
            require(graph.vertices()<=32,"each large-map operation expands at most32 actual vertices");total+=graph.vertices();}
        require(distance==49&&total<=2500,"long unfinished route progresses across bounded turns to exact completed terrain distance");
        const int calls=w.terrain_calls;require(graph.cached({49,49},{0,0})==49&&w.terrain_calls==calls,"sealed forecast cache reads do no terrain queries or graph expansion");
        graph.begin_turn();require(graph.advance({0,0},{49,49},0,{work,&w})==49&&graph.vertices()==0,"completed route survives while physical forecast is invalidated");
    }
    {auto world=std::make_unique<World>();auto& w=*world;w.width=w.height=12;w.prepare();metronome::Graph graph;graph.bind({12,12,true,walkable,&w},1);
        w.allowed=false;require(graph.advance({0,0},{11,11},32,{work,&w})<0&&graph.vertices()==0,"denied setup never publishes partially cleared bitmap");
        w.allowed=true;int d=-1;for(int n=0;n<10&&d<0;++n){graph.begin_turn();d=graph.advance({0,0},{11,11},32,{work,&w});}
        require(d==11,"allowed later turn resumes a previously denied setup safely");
        w.blocked[5*12+5]=1;graph.bind({12,12,true,walkable,&w},2);require(graph.cached({0,0},{11,11})<0,"new immutable geometry identity invalidates old completed routes");}
    std::printf("plan4-metronome-runtime: %d checks passed\n",checks);
}
