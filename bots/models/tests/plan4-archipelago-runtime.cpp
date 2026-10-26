// SDK-free legal DTO fixtures. Cached routes are sealed before prediction.
#include "../archipelago/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <queue>

namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0, bytes = 0;
void record(std::size_t size) {
    if (enabled) { ++calls; bytes += size; }
}
}
void* operator new(std::size_t size) {
    if (void* result = std::malloc(size ? size : 1)) { allocation_probe::record(size); return result; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace plan4;
namespace rt = plan4::runtime;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct World {
    int width, height;
    std::vector<unsigned char> walls, occupied, visible;
    std::vector<Point> targets;
    std::vector<std::vector<int>> fields;
    bool missing = false, short_prefix = false;
    World(int w, int h) : width(w), height(h), walls(w*h, 1), occupied(w*h, 0), visible(w*h, 1) {}
    int key(Point p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height ? p.y*width+p.x : -1; }
    bool open(Point p) const { const int k = key(p); return k >= 0 && !walls[k]; }
    void seal(std::initializer_list<Point> goals) {
        targets.assign(goals); fields.clear();
        for (Point goal : targets) {
            std::vector<int> distances(width*height, -1); std::queue<Point> q;
            if (open(goal)) { distances[key(goal)] = 0; q.push(goal); }
            while (!q.empty()) {
                const Point p = q.front(); q.pop();
                for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) if (dx || dy) {
                    const Point next{p.x+dx,p.y+dy}; const int k = key(next);
                    if (open(next) && distances[k] < 0) { distances[k] = distances[key(p)]+1; q.push(next); }
                }
            }
            fields.push_back(std::move(distances));
        }
    }
    static bool walkable(Point p, const void* context) { return static_cast<const World*>(context)->open(p); }
    static rt::TileOccupancy occupancy(Point p, const void* context) {
        const auto& w = *static_cast<const World*>(context); const int k = w.key(p);
        if (k < 0 || !w.visible[k]) return rt::TileOccupancy::unknown;
        return w.occupied[k] ? rt::TileOccupancy::occupied : rt::TileOccupancy::free;
    }
    static int distance(Point a, Point b, const void* context) {
        const auto& w = *static_cast<const World*>(context); if (w.missing || !w.open(a)) return -1;
        for (std::size_t n=0; n<w.targets.size(); ++n) if (w.targets[n] == b) return w.fields[n][w.key(a)];
        return -1;
    }
    static int prefix(Point a, Point b, Point* out, int capacity, const void* context) {
        const auto& w = *static_cast<const World*>(context);
        const int total = distance(a,b,context); if (total < 0 || !out || capacity < 0) return -1;
        int count = std::min(total,capacity); if (w.short_prefix && count) --count;
        for (int n=0; n<count; ++n) {
            Point best{}; bool found = false; const int d = distance(a,b,context);
            for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) if (dx || dy) {
                const Point p{a.x+dx,a.y+dy};
                if (distance(p,b,context) == d-1 && (!found || point_less(p,best))) { found=true; best=p; }
            }
            if (!found) return -1;
            out[n]=best; a=best;
        }
        return count;
    }
};
struct Fixture {
    World world;
    std::unique_ptr<Information> info = std::make_unique<Information>();
    Menu menu{};
    std::array<rt::PublicGroup,2> groups{};
    std::array<rt::PublicMember,2> members{};
    bool layout_valid = true;
    Fixture(int width=7, bool ring=false) : world(width,ring?5:3) {
        for (int x=0; x<width; ++x) {
            world.walls[world.key({x,1})]=0;
            if (ring) world.walls[world.key({x,3})]=0;
        }
        if (ring) { world.walls[world.key({0,2})]=0; world.walls[world.key({width-1,2})]=0; }
        auto& i=*info; i.round=10; i.own.id=3; i.own.position={0,1};
        i.own.health=i.own.maximum_health=1000; i.group_count=2; i.altar_count=1;
        i.altars[0].id=90; i.altars[0].position={0,1}; i.altars[0].altar_position={0,0}; i.altars[0].lock_known=true;
        i.travel={World::distance,&world};
        for (int g=0; g<2; ++g) {
            auto& group=i.groups[g]; group.id=g?10:30; group.position={width-2+g,1};
            group.health=group.full_health=100; group.clear_rounds=group.full_clear_rounds=1;
            group.gems=8; group.gold=4; group.respawn_rounds=50; group.lifecycle=Lifecycle::living;
            group.visible=group.all_spawns_visible=group.occupancy_known=true;
            group.provenance=group.health_provenance={i.round,Source::personal_vision,Status::exact_now};
            groups[g]={group.id,g,1,true}; members[g]={group.id,100+g,0,1,0,3,group.position,100};
            menu.jobs[g].kind=JobKind::farm; menu.jobs[g].group=g; menu.jobs[g].altar=0; menu.jobs[g].menu_rank=g;
        }
        menu.count=menu.single_count=2;
        world.seal({i.groups[0].position,i.groups[1].position,i.altars[0].position});
    }
    void next() { ++info->round; for (int n=0;n<2;++n) info->groups[n].provenance.observed_round=info->groups[n].health_provenance.observed_round=info->round; }
    rt::SpatialFrame frame(int quota) const {
        return {{info->round,{info->own.id}},info.get(),&menu,{world.width,world.height,true,World::walkable,&world},
            World::occupancy,&world,{World::distance,&world},World::prefix,&world,true,false,quota};
    }
    void feed(rt::Archipelago& model,int quota=192) {
        model.observe_spatial(frame(quota));
        model.observe_layout({{info->round,{info->own.id}},groups.data(),layout_valid?2:0,members.data(),layout_valid?2:0,false});
        model.observe_snapshot(*info);
    }
};
bool same(const Forecast& a,const Forecast& b) {
    if (a.event_count!=b.event_count || a.macros_started!=b.macros_started || a.final_round!=b.final_round ||
        a.first_conversion_round!=b.first_conversion_round || a.our_secured!=b.our_secured || a.enemy_secured!=b.enemy_secured ||
        a.final_bag!=b.final_bag || a.final_gold!=b.final_gold || a.final_health!=b.final_health || a.terminal_z!=b.terminal_z ||
        a.terminal_unknown!=b.terminal_unknown || a.stock_unknown!=b.stock_unknown || a.travel_unknown!=b.travel_unknown ||
        a.lock_unknown!=b.lock_unknown || a.truncated!=b.truncated || a.completed_conversion!=b.completed_conversion ||
        a.no_conversion!=b.no_conversion || a.budget_exhausted!=b.budget_exhausted || a.abort_reason!=b.abort_reason) return false;
    for (int e=0;e<a.event_count;++e) {
        const auto& x=a.events[e]; const auto& y=b.events[e];
        if (x.kind!=y.kind || x.start.round!=y.start.round || x.start.phase!=y.start.phase || x.end.round!=y.end.round ||
            x.end.phase!=y.end.phase || x.group_id!=y.group_id || x.bag_before!=y.bag_before || x.bag_after!=y.bag_after ||
            x.secured!=y.secured || x.gold_after!=y.gold_after || x.health_after!=y.health_after || x.status!=y.status) return false;
    }
    return true;
}
std::unique_ptr<Evaluation> evaluate(rt::Archipelago& model,Fixture& f,int quota=192) {
    f.feed(model,quota); auto out=std::make_unique<Evaluation>(); rt::evaluate_model(*f.info,f.menu,model,*out);
    check(model.spatial_vertices()>=0 && model.spatial_vertices()<=quota,"copy, decomposition, route and search share per-turn quota");
    check(out->menu.count==f.menu.count && out->evaluated_count==f.menu.count,"same menu and completed prefix retained"); return out;
}
void exact(const Evaluation& output,Fixture& f) {
    auto baseline=std::make_unique<Evaluation>(); plan4::evaluate_into(*f.info,f.menu,*baseline);
    for (int n=0;n<f.menu.count;++n) check(same(output.forecasts[n],baseline->forecasts[n]),"unsupported or unblocked candidate uses exact M0 body and full fixed tail");
}
void constructor_and_denied_initialization_do_not_allocate() {
    // The constructor has no owner-round fuel guard. Catch regressions that
    // allocate/initialize unused full observation, route or forecast capacities.
    static_assert(sizeof(rt::Archipelago) <= 8192);
    alignas(rt::Archipelago) unsigned char storage[sizeof(rt::Archipelago)];
    for (bool enabled : {false,true}) {
        allocation_probe::calls=allocation_probe::bytes=0; allocation_probe::enabled=true;
        auto* model=::new(static_cast<void*>(storage)) rt::Archipelago(enabled);
        allocation_probe::enabled=false;
        check(allocation_probe::calls==0 && allocation_probe::bytes==0,
            "active and control constructors allocate no unused heap capacities before fuel admission");
        check(model->spatial_vertices()==0 && model->model_evidence().reason==1 && model->model_evidence().revision==0,
            "uninitialized model diagnostics are safe without graph storage");
        auto f=std::make_unique<Fixture>();
        const WorkBudget denied{[](const void*){return false;},nullptr};
        allocation_probe::enabled=true;
        model->static_spatial({{f->info->round,{f->info->own.id}},f->frame(0).topology,denied,128});
        allocation_probe::enabled=false;
        check(allocation_probe::calls==0 && model->spatial_vertices()==0 && !model->evidence().graph_complete,
            "denied first static call does not reserve topology or construct graph helpers");
        f->next();
        model->static_spatial({{f->info->round,{f->info->own.id}},f->frame(0).topology,{},1});
        check(model->spatial_vertices()==1 && model->evidence().topology_copied==1,
            "admitted initialization still starts with exactly its charged topology prefix");
        model->~Archipelago();
    }
}
void bounded_progress_and_repeated_clock() {
    auto f=std::make_unique<Fixture>(); auto model=std::make_unique<rt::Archipelago>();
    auto initial=evaluate(*model,*f,1); exact(*initial,*f);
    check(model->spatial_vertices()==1 && model->evidence().topology_copied==1,"one-cell quota does not initialize a whole topology");
    const int used=model->spatial_vertices(); f->feed(*model,192);
    check(model->spatial_vertices()==used,"repeat same clock cannot replenish allowance");
    int turns=0;
    while (!model->evidence().graph_complete && turns++<2000) { f->next(); initial=evaluate(*model,*f,1); }
    check(model->evidence().graph_complete,"topology and iterative decomposition resume with one unit per owner round");
    for (int n=0;n<20 && model->evidence().certified_routes!=2;++n) { f->next(); initial=evaluate(*model,*f,64); }
    check(model->evidence().certified_routes==2,"route copying and connector registration eventually complete"); exact(*initial,*f);
}
void true_cut_propagates_and_loss_of_sight_is_uncertain() {
    auto f=std::make_unique<Fixture>(); auto active=std::make_unique<rt::Archipelago>(), control=std::make_unique<rt::Archipelago>(false);
    f->world.occupied[f->world.key({3,1})]=1;
    std::unique_ptr<Evaluation> a,c; int turns=0;
    do { f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f);
        check(active->spatial_vertices()==control->spatial_vertices(),"disabled arm performs identical bounded spatial preparation");
    } while (active->evidence().applied_jobs!=2 && ++turns<100);
    check(active->evidence().applied_jobs==2 && control->evidence().applied_jobs==1,"true articulation blockage propagates to dependent jobs only in active arm");
    const auto reported=active->model_evidence(), disabled=control->model_evidence();
    check(reported.reported && reported.supported && reported.coupling_enabled && reported.paths==2 &&
        reported.work==active->spatial_vertices() && reported.revision>0 && disabled.supported && !disabled.coupling_enabled,
        "diagnostics report actual influenced paths and charged work, including disabled control influence");
    check(active->evidence().conditional_unreachable==2 && a->forecasts[0].travel_unknown && a->forecasts[1].travel_unknown,
        "complete cut proof aborts the first macro through the common travel guard");
    for (int n=0;n<2;++n) check(a->forecasts[n].event_count>0 && a->forecasts[n].events[0].kind==EventKind::abort &&
        a->forecasts[n].events[0].secured==0 && a->forecasts[n].events[0].bag_after==0,
        "failed first travel cannot create reward; future fixed continuation retains its actual geometry");
    const int other=1-control->evidence().attempted;
    auto baseline=std::make_unique<Forecast>(); predict_into(*f->info,f->menu.jobs[other],f->menu,*baseline);
    check(same(c->forecasts[other],*baseline) && baseline->our_secured>0,"control retains independent unattempted forecast");
    f->world.visible[f->world.key({3,1})]=0; f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f);
    check(active->evidence().applied_jobs==0 && active->evidence().uncertain_risks>0,"lost sight discards exact blockage, retaining only uncertainty"); exact(*a,*f); exact(*c,*f);
    check(!active->model_evidence().coupling_enabled && active->model_evidence().paths==0,"lost sight cannot retain last batch's influence diagnostics");
    f->world.visible[f->world.key({3,1})]=1; f->world.occupied[f->world.key({3,1})]=0; f->next(); a=evaluate(*active,*f);
    check(active->evidence().applied_jobs==0,"fresh free observation clears temporary blockage without terrain mutation"); exact(*a,*f);
}
void second_route_is_repaired_instead_of_false_disconnect() {
    auto f=std::make_unique<Fixture>(7,true); auto active=std::make_unique<rt::Archipelago>(), control=std::make_unique<rt::Archipelago>(false);
    f->world.occupied[f->world.key({3,1})]=1;
    std::unique_ptr<Evaluation> a,c; int turns=0, search_turns=0;
    do { f->next(); a=evaluate(*active,*f,64); c=evaluate(*control,*f,64);
        search_turns+=active->evidence().search_work>0;
        check(active->spatial_vertices()==control->spatial_vertices(),"both arms resume same ordinary shortest-path search");
    } while (active->evidence().alternate_routes<1 && ++turns<200);
    check(active->evidence().alternate_routes==1 && control->evidence().alternate_routes==1,"known second route becomes a completed repair in both arms");
    check(active->evidence().conditional_unreachable==0 && search_turns>1,"incremental alternate BFS avoids a false hard disconnection");
    for (int n=0;n<2;++n) check(same(a->forecasts[n],c->forecasts[n]),"nonconnector evidence affects only attempted route in both arms");
}
void missing_routes_and_metadata_fall_back() {
    auto f=std::make_unique<Fixture>(); auto model=std::make_unique<rt::Archipelago>();
    f->world.short_prefix=true; std::unique_ptr<Evaluation> out;
    for (int n=0;n<12;++n) { f->next(); out=evaluate(*model,*f); }
    check(model->evidence().graph_complete && model->evidence().unsupported_jobs==2,"truncated cached prefixes remain unsupported"); exact(*out,*f);
    check(!model->model_evidence().supported && model->model_evidence().reason==3,"missing route has explicit current-batch unsupported reason");
    f->world.short_prefix=false; f->layout_valid=false; f->world.occupied[f->world.key({3,1})]=1;
    for (int n=0;n<6;++n) { f->next(); out=evaluate(*model,*f); }
    check(model->evidence().unsupported_jobs==2 && model->evidence().applied_jobs==0,"missing public composition cannot fabricate production endpoint exemptions"); exact(*out,*f);
    f->layout_valid=true;
    for (int n=0;n<6 && model->evidence().applied_jobs!=2;++n) { f->next(); out=evaluate(*model,*f); }
    check(model->evidence().applied_jobs==2,"fresh valid metadata restores supported connector forecasts");
}
void production_slots_and_two_camp_path_stay_physical() {
    auto f=std::make_unique<Fixture>(); auto active=std::make_unique<rt::Archipelago>(), control=std::make_unique<rt::Archipelago>(false);
    f->menu.jobs[0].second_group=1; f->menu.count=1; f->menu.single_count=0;
    for (int g=0;g<2;++g) f->world.occupied[f->world.key(f->info->groups[g].position)]=1;
    std::unique_ptr<Evaluation> a,c;
    for (int n=0;n<14;++n) { f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f); }
    check(active->evidence().certified_routes==1 && active->evidence().applied_jobs==0,"production members are common endpoint exemptions, not blocked graph cuts"); exact(*a,*f); exact(*c,*f);
    check(a->forecasts[0].our_secured==16,"ordered two-camp macro conserves both physical rewards before its bank");
}
void invalid_frames_and_denied_budget_never_publish_repairs() {
    auto f=std::make_unique<Fixture>(); auto model=std::make_unique<rt::Archipelago>(); std::unique_ptr<Evaluation> out;
    for (int n=0;n<14;++n) { f->next(); out=evaluate(*model,*f); }
    f->next(); auto frame=f->frame(192); frame.topology.complete=false;
    model->observe_spatial(frame); model->observe_snapshot(*f->info); out=std::make_unique<Evaluation>(); rt::evaluate_model(*f->info,f->menu,*model,*out);
    check(model->spatial_vertices()==0 && !model->evidence().topology_supported,"incomplete topology cannot reuse a completed exact graph"); exact(*out,*f);
    f->next(); frame=f->frame(192); --frame.clock.round; model->observe_spatial(frame); model->observe_snapshot(*f->info);
    rt::evaluate_model(*f->info,f->menu,*model,*out); check(model->spatial_vertices()==0,"stale spatial frame cannot perform work or influence"); exact(*out,*f);
    f->next(); f->info->work_budget={[](const void*){return false;},nullptr}; f->feed(*model);
    rt::evaluate_model(*f->info,f->menu,*model,*out);
    check(model->spatial_vertices()==0 && out->budget_exhausted && out->evaluated_count==0,"fuel denial leaves no partial forecast published");
}
void excessive_route_length_stays_explicitly_unsupported() {
    auto f=std::make_unique<Fixture>(140); auto model=std::make_unique<rt::Archipelago>(); std::unique_ptr<Evaluation> out;
    for (int n=0;n<100 && !model->evidence().graph_complete;++n) { f->next(); out=evaluate(*model,*f); }
    for (int n=0;n<3;++n) { f->next(); out=evaluate(*model,*f); }
    check(model->evidence().graph_complete && model->evidence().unsupported_jobs==2 && model->evidence().applied_jobs==0,"long combined route cannot masquerade as complete bounded certificate"); exact(*out,*f);
}
void static_work_and_planning_share_one_owner_allowance() {
    auto f=std::make_unique<Fixture>(); auto model=std::make_unique<rt::Archipelago>();
    const auto static_frame=[&](int quota,WorkBudget budget=WorkBudget{}) {
        return rt::StaticSpatialFrame{{f->info->round,{f->info->own.id}},f->frame(0).topology,budget,quota};
    };
    model->static_spatial(static_frame(1));
    check(model->spatial_vertices()==1 && model->evidence().topology_copied==1,"static owner start copies only its bounded prefix");
    model->static_spatial(static_frame(128));
    check(model->spatial_vertices()==1,"duplicate static call cannot enlarge its original allowance");
    auto out=evaluate(*model,*f,4);
    check(model->spatial_vertices()==4 && model->evidence().topology_copied==4,"first planning frame can spend only the remainder of total allowance");
    f->feed(*model,192);
    check(model->spatial_vertices()==4,"repeated planning call cannot replenish total static plus planning work");
    model->static_spatial(static_frame(128)); check(model->spatial_vertices()==4,"late duplicate static callback cannot spend beyond its original cap");
    f->next(); model->static_spatial(static_frame(128)); const int static_used=model->spatial_vertices();
    check(static_used>0 && static_used<=128 && model->evidence().occupancy_queries==0 && model->evidence().certified_routes==0,
        "nonplanning static work cannot inspect occupancy or bind a menu route");
    auto stale=f->frame(192); --stale.clock.round; model->observe_spatial(stale);
    check(model->spatial_vertices()==static_used,"stale planning frame cannot erase valid current-round static charges");
    out=evaluate(*model,*f,192);
    check(model->spatial_vertices()>=static_used && model->spatial_vertices()<=192,"later planning uses one combined graph-work total");
    f->info->round+=9; model->static_spatial(static_frame(128));
    check(model->spatial_vertices()<=128,"owner-round gap resets work count without discarding immutable graph progress");
    for (int n=0;n<20 && !model->evidence().graph_complete;++n) { f->next(); model->static_spatial(static_frame(128)); }
    check(model->evidence().graph_complete,"static-only owner starts eventually complete graph even without a planning call");
    f->next(); model->static_spatial(static_frame(128,{[](const void*){return false;},nullptr}));
    check(model->spatial_vertices()==0,"denied static budget never charges or expands graph work");
    auto fresh=std::make_unique<rt::Archipelago>(); int permits=3;
    const WorkBudget limited{[](const void* p){return --*static_cast<int*>(const_cast<void*>(p))>=0;},&permits};
    fresh->static_spatial(static_frame(128,limited));
    check(fresh->spatial_vertices()==3 && fresh->evidence().topology_copied==3,"static fuel guard stops inside initialization and preserves written prefix");
    f->next(); fresh->static_spatial(static_frame(128));
    check(fresh->spatial_vertices()>0 && fresh->evidence().topology_copied==18,"next owner resumes after budget stop without recopying initialized prefix");
}
void complete_common_menu_has_supported_unchanged_nonroute_jobs() {
    auto f=std::make_unique<Fixture>();
    f->menu=make_menu(*f->info);
    check(f->menu.single_count==2 && f->menu.count==7 && f->menu.jobs[5].kind==JobKind::renew &&
        f->menu.jobs[6].kind==JobKind::hold,"production menu includes singles, ordered pairs, bank, renewal and hold");
    auto active=std::make_unique<rt::Archipelago>(), control=std::make_unique<rt::Archipelago>(false);
    std::unique_ptr<Evaluation> a,c;
    for (int n=0;n<24;++n) { f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f);
        check(active->spatial_vertices()==control->spatial_vertices(),"complete-menu arms charge identical graph work"); }
    check(active->model_evidence().supported && control->model_evidence().supported &&
        active->evidence().unsupported_jobs==0 && control->evidence().unsupported_jobs==0 &&
        active->evidence().certified_routes==5,"ordinary nonroute jobs do not make complete route coverage unsupported");
    exact(*a,*f); exact(*c,*f);
    check(!active->model_evidence().coupling_enabled && !control->model_evidence().coupling_enabled,
        "supported common no-op coverage does not invent topology influence");
    f->world.occupied[f->world.key({3,1})]=1;
    for (int n=0;n<12;++n) { f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f); }
    check(active->model_evidence().supported && control->model_evidence().supported &&
        active->evidence().applied_jobs>=2 && control->evidence().applied_jobs<=1 &&
        active->model_evidence().coupling_enabled && !control->model_evidence().coupling_enabled,
        "complete menu preserves supported active propagation and attempted-route-only control");
    auto ordinary=std::make_unique<Evaluation>(); plan4::evaluate_into(*f->info,f->menu,*ordinary);
    for (int n=5;n<7;++n) check(same(a->forecasts[n],ordinary->forecasts[n]) && same(c->forecasts[n],ordinary->forecasts[n]),
        "topology coupling leaves RENEW/HOLD and their complete common tails unchanged");
    for (int defect=0;defect<2;++defect) {
        f=std::make_unique<Fixture>(); f->menu=make_menu(*f->info);
        if (defect==0) f->menu.jobs[0].group=-1;
        else f->menu.jobs[4].altar=-1;
        active=std::make_unique<rt::Archipelago>(); control=std::make_unique<rt::Archipelago>(false);
        for (int n=0;n<24;++n) { f->next(); a=evaluate(*active,*f); c=evaluate(*control,*f); }
        check(active->evidence().graph_complete && active->evidence().unsupported_jobs==1 &&
            control->evidence().unsupported_jobs==1 && !active->model_evidence().supported &&
            !control->model_evidence().supported && active->model_evidence().reason==3,
            "malformed FARM/BANK route remains explicitly unsupported in a complete menu");
        exact(*a,*f); exact(*c,*f);
    }
}
}
int main() {
    constructor_and_denied_initialization_do_not_allocate();
    bounded_progress_and_repeated_clock();
    true_cut_propagates_and_loss_of_sight_is_uncertain();
    second_route_is_repaired_instead_of_false_disconnect();
    missing_routes_and_metadata_fall_back();
    production_slots_and_two_camp_path_stay_physical();
    invalid_frames_and_denied_budget_never_publish_repairs();
    excessive_route_length_stays_explicitly_unsupported();
    static_work_and_planning_share_one_owner_allowance();
    complete_common_menu_has_supported_unchanged_nonroute_jobs();
    std::printf("plan4 archipelago runtime: 10 scenarios, %d checks passed\n",checks);
}
