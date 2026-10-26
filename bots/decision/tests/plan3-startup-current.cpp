// Queue/caller checks using actual SDK wrappers and the explicit8x8 host.
int (*startup_bitset_probe)()=nullptr;
#include "observation-fixture.h"

namespace {
unsigned startup_shapes=0,startup_callers=0,field_comparisons=0;
unsigned new_first=0,old_first=0;
unsigned cadence_cases=0,urgent_callbacks=0,second_gem_waits=0;
void public_shape(unsigned shape) {
    reset_case();observation_shield=0;observation_shield_ticks=0;
    if(shape&1)raw_gold.fill(0);
    if(shape&2)raw_gems.fill(0);
    if(shape==4)raw_gems[1]=0;
}
std::unique_ptr<Campaign> initialized_queue(unsigned shape) {
    public_shape(shape);auto c=std::make_unique<Campaign>(Controller::ledger,false,"startup-queue");
    unsigned limit=0;while(!c->initialized_){verify(++limit<128);c->initialize_campaign();}
    while(c->field_cursor_<c->field_order_.size())c->register_fields();return c;
}
std::array<int,3> expected_roles(const Campaign& c) {
    std::array<int,3> roles{{-1,-1,-1}};
    std::vector<std::size_t> order;
    for(std::size_t i=0;i<c.personal_.groups.size();++i)order.push_back(i);
    const auto p=cambc::self().position();
    std::sort(order.begin(),order.end(),[&](auto a,auto b) {
        const auto& x=c.personal_.groups[a];const auto& y=c.personal_.groups[b];
        return std::tuple{x.incoming!=0,sr::distance(p,x.anchor),x.key}<
               std::tuple{y.incoming!=0,sr::distance(p,y.anchor),y.key};
    });
    verify(order==c.field_order_);
    for(const auto i:order){const auto& g=c.personal_.groups[i];
        if(g.gem_value>0){if(roles[1]<0)roles[1]=int(i);else if(roles[2]<0)roles[2]=int(i);}
        else if(g.gold_value>0 && roles[0]<0)roles[0]=int(i);}
    return roles;
}
void check_queue(unsigned shape) {
    auto c=initialized_queue(shape);auto& p=c->personal_;const auto roles=expected_roles(*c);
    verify(c->initial_groups_==roles && p.altars.size()==3 && p.altar_fields.size()==3 && p.spawn_field==0);
    verify((roles[0]<0)==bool(shape&1) && (roles[1]<0)==bool(shape&2));
    verify((roles[2]<0)==bool((shape&2) || shape==4));
    if(roles[2]>=0)verify(roles[1]!=roles[2]);
    unsigned next=1;for(const int g:roles)if(g>=0)verify(p.groups[std::size_t(g)].field==int(next++));
    std::vector<std::size_t> altars{0,1,2};const auto origin=cambc::self().position();
    std::sort(altars.begin(),altars.end(),[&](auto a,auto b){const auto x=p.altars[a],y=p.altars[b];
        return std::pair{sr::distance(origin,x),x.y*p.width+x.x}<std::pair{sr::distance(origin,y),y.y*p.width+y.x};});
    verify(altars==c->altar_order_ && altars[0]==1); // Nearest is NOT canonical index0.
    for(const auto a:altars)verify(p.altar_fields[a]==int(next++));
    std::vector<int> ids{p.spawn_field};for(const auto& g:p.groups)ids.push_back(g.field);
    ids.insert(ids.end(),p.altar_fields.begin(),p.altar_fields.end());std::sort(ids.begin(),ids.end());
    verify(ids.size()==c->paths_.fields_.size());for(unsigned i=0;i<ids.size();++i)verify(ids[i]==int(i));
    for(std::size_t a=0;a<p.altars.size();++a) {
        std::vector<int> expected;
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)
            if(sr::distance({x,y},p.altars[a])<=cambc::constants::submit_gems_range && !p.terrain.is_set({x,y}))expected.push_back(y*8+x);
        verify(c->paths_.roots(p.altar_fields[a])==expected);
    }
    for(const int id:ids)verify(!c->paths_.complete(id) && c->paths_.get(id,origin)==StaticPaths::unknown);
    // Explicit field-only prior queue: spawn, ALL altars, then groups sorted by
    // zero incoming/key. Same StaticPaths budget; no native-fuel/caller claim.
    StaticPaths baseline;baseline.initialize(p.terrain);baseline.add(p.own_spawn);
    for(const auto a:p.altars)baseline.add(radius(a,cambc::constants::submit_gems_range));
    std::vector<std::size_t> oldOrder;for(std::size_t i=0;i<p.groups.size();++i)oldOrder.push_back(i);
    std::sort(oldOrder.begin(),oldOrder.end(),[&](auto a,auto b){return std::pair{p.groups[a].incoming!=0,p.groups[a].key}<std::pair{p.groups[b].incoming!=0,p.groups[b].key};});
    const int target=roles[0]>=0?roles[0]:roles[1];int baselineTarget=-1;
    for(const auto i:oldOrder){const int id=baseline.add(p.groups[i].spawns);if(int(i)==target)baselineTarget=id;}
    unsigned proposedAt=0,baselineAt=0;
    for(unsigned callback=1;callback<=64 && (!c->paths_.ready() || !baseline.ready());++callback) {
        c->paths_.begin_turn();baseline.begin_turn();c->paths_.advance();baseline.advance();
        if(target>=0 && !proposedAt && c->paths_.complete(p.groups[std::size_t(target)].field))proposedAt=callback;
        if(target>=0 && !baselineAt && baseline.complete(baselineTarget))baselineAt=callback;
    }
    verify(c->paths_.ready() && baseline.ready());for(const int id:ids)verify(c->paths_.complete(id));
    verify(c->startup_routes_ready());
    if(target>=0){verify(proposedAt>0 && proposedAt<baselineAt);++field_comparisons;if(!shape){new_first=proposedAt;old_first=baselineAt;}}
    ++startup_shapes;
}
void cadence_budget() {
    auto c=initialized_queue(0);auto& paths=c->paths_;const unsigned beforeActions=actions;
    const auto pending=[&]{for(const auto& g:c->personal_.groups)if(!paths.complete(g.field))
        verify(paths.get(g.field,cambc::self().position())==StaticPaths::unknown);};
    verify(!c->startup_routes_ready());pending();paths.begin_turn();
    // Active/recovering/nonspawn callers supply idle=false. Bagged idle callers
    // keep urgent=false; none gets a forced noncadence preparation callback.
    for(unsigned domain=0;domain<3;++domain){verify(!paths.prepare_idle(false,1,false,true) && paths.work_==0);++cadence_cases;}
    verify(!paths.prepare_idle(true,1,false,false) && paths.work_==0);++cadence_cases;
    fuel=29999;verify(paths.prepare_idle(true,1,false,true) && paths.work_==0);pending();fuel=1000000;++cadence_cases;
    verify(paths.prepare_idle(true,1,false,true) && paths.work_==2 && !paths.take_work());++cadence_cases;
    const auto generation=paths.generation();const auto next=paths.next_;const auto prepared=paths.fields_[next].prepared;
    verify(paths.prepare_idle(true,1,false,true) && paths.work_==2 && paths.generation()==generation &&
        paths.next_==next && paths.fields_[next].prepared==prepared);++cadence_cases;
    bool oldCadenceObserved=false;
    for(unsigned turn=2;turn<=64 && !paths.ready();++turn) {
        paths.begin_turn();const bool urgent=!c->startup_routes_ready();
        const bool worked=paths.prepare_idle(true,turn,false,urgent);
        verify(paths.work_<=2);pending();
        if(urgent)verify(worked);
        else if(turn%4){verify(!worked && paths.work_==0);oldCadenceObserved=true;}
    }
    verify(paths.ready() && oldCadenceObserved && actions==beforeActions);++cadence_cases;
}
void cold_caller(Controller controller) {
    public_shape(0);group_state[0]=1;group_state[2]=2;
    member_states[0]=2;member_health[0]=cambc::map::camp_groups()[0].camps()[0].base_stats().base_health;
    member_states[4]=member_states[5]=0;member_health[4]=member_health[5]=0;
    auto c=std::make_unique<Campaign>(controller,false,"startup-cold-caller");
    bool committed=false;JobId first;
    for(unsigned n=0;n<128;++n) {
        const bool pending=c->initialized_ && !c->active_ && !c->recovering_ && on_spawn() && !bag && !c->startup_routes_ready();
        const unsigned beforeActions=actions,beforePreparations=c->telemetry_.preparations;
        if(pending && c->paths_.complete(c->personal_.groups[std::size_t(c->initial_groups_[1])].field) &&
           !c->paths_.complete(c->personal_.groups[std::size_t(c->initial_groups_[2])].field))++second_gem_waits;
        c->turn();
        verify(c->paths_.work_<=2);
        if(pending){verify(actions==beforeActions && !c->active_ && c->telemetry_.preparations==beforePreparations+1);++urgent_callbacks;}
        if(c->active_) {
            if(!committed){first=c->active_->id;committed=true;}
            verify(c->active_->id==first && (first.kind==JobKind::cycle || first.kind==JobKind::double_cycle) && c->active_->complete && c->active_->routeKnown);
            const int g=c->group_index(first.firstGroup);verify(g>=0 && c->paths_.complete(c->personal_.groups[std::size_t(g)].field));
            if(first.secondGroup>=0){const int second=c->group_index(first.secondGroup);verify(second>=0 && c->paths_.complete(c->personal_.groups[std::size_t(second)].field));}
            const int a=c->altar_index(first.altarTile);verify(a>=0 && c->paths_.complete(c->personal_.altar_fields[std::size_t(a)]));
            if(moves)break;
        }
        advance_round();
    }
    verify(committed && c->active_ && moves>0 && c->telemetry_.activations>0 && !c->sortie_episode_);
    const auto state=c->information();const auto context=c->forecast_context(state);unsigned admitted=0;
    for(const int g:c->initial_groups_)if(g>=0){const auto j=context.acquisition(g);if(plan3::admitted(state,j,false))++admitted;}
    verify(admitted==3);++startup_callers;
    std::printf("STARTUP_CALLER {\"side\":%u,\"controller\":%u,\"firstKind\":%u,\"firstGroup\":%d,\"secondGroup\":%d,\"altarIndex\":%d,\"moves\":%u,\"admittedRoleAlternatives\":%u}\n",bp_side(),unsigned(controller),unsigned(first.kind),first.firstGroup,first.secondGroup,c->altar_index(first.altarTile),moves,admitted);
}
Campaign* error_campaign=nullptr;unsigned error_kind=0,error_cursor=0,error_field_count=0;int error_group=-1;
int startup_error() {
    auto& c=*error_campaign;
    if(error_kind==0)verify(c.initial_group_cursor_==error_cursor && c.personal_.groups[std::size_t(error_group)].field<0);
    if(error_kind==1)verify(c.altar_initialization_cursor_==error_cursor && c.personal_.altar_fields[c.altar_order_[error_cursor]]<0);
    if(error_kind==2)verify(c.field_cursor_==error_cursor && c.personal_.groups[c.field_order_[error_cursor]].field<0);
    verify(c.paths_.fields_.size()==error_field_count);
    std::fprintf(stderr,"STARTUP_EXPECTED_UNCOMMITTED_ERROR kind=%u\n",error_kind);return 1;
}
void error_child(unsigned kind) {
    public_shape(kind==2?3:0);auto c=std::make_unique<Campaign>(Controller::ledger,false,"startup-error");unsigned n=0;
    while(c->personal_.spawn_field<0){verify(++n<128);c->initialize_campaign();}
    if(kind>=1)while(c->initial_group_cursor_<c->initial_groups_.size())c->initialize_campaign();
    if(kind==2)while(!c->initialized_)c->initialize_campaign();
    error_campaign=c.get();error_kind=kind;error_field_count=unsigned(c->paths_.fields_.size());
    if(kind==0){error_cursor=c->initial_group_cursor_;error_group=c->initial_groups_[error_cursor];}
    if(kind==1)error_cursor=unsigned(c->altar_initialization_cursor_);
    if(kind==2){error_cursor=unsigned(c->field_cursor_);while(c->personal_.groups[c->field_order_[error_cursor]].field>=0)++error_cursor;}
    startup_bitset_probe=startup_error;
    if(kind==2)c->register_fields();else c->initialize_campaign();
    verify(false); // This WASI host aborts on SDK throw; no unwind/retry claim.
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(const char* e=std::getenv("P3_STARTUP_ERROR"))error_child(unsigned(std::atoi(e)));
    for(unsigned shape=0;shape<5;++shape)check_queue(shape);
    cadence_budget();
    cold_caller(Controller::ledger);cold_caller(Controller::atlas);
    verify(urgent_callbacks>0 && second_gem_waits>0);
    std::printf("STARTUP_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"roleShapes\":%u,\"coldCallers\":%u,\"fieldOnlyComparisons\":%u,\"earlyTargetCallbacks\":%u,\"priorQueueTargetCallbacks\":%u,\"cadenceCases\":%u,\"urgentCallbacks\":%u,\"secondGemWaits\":%u,\"nativeGames\":0}\n",bp_side(),checks,startup_shapes,startup_callers,field_comparisons,new_first,old_first,cadence_cases,urgent_callbacks,second_gem_waits);
}
