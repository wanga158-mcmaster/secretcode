// Explicit synthetic profile certificates. These are not a legal engine
// fixture, module attestation, or proof of the current SDK fixture's executor.
#include "../counterpart/src/runtime.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace cp=plan4::counterpart;
namespace {
struct Board {
    Point occupied{6,3},closed{-1,-1};int quotes=0;bool delay=false,interrupt=false;
    static int distance(Point a,Point b,const void*){return cp::chebyshev(a,b);}
    static bool walk(Point p,const void*){return p.x>=0 && p.y>=0 && p.x<10 && p.y<10;}
    static int open(Point p,const void* context){const auto& self=*static_cast<const Board*>(context);return walk(p,context) && p!=self.closed?1:0;}
    static rt::TileOccupancy occupancy(Point p,const void* context){return p==static_cast<const Board*>(context)->occupied?rt::TileOccupancy::occupied:rt::TileOccupancy::free;}
    static int prefix(Point from,Point to,Point* out,int count,const void*){
        for(int n=0;n<count;++n){from.x+=(to.x>from.x)-(to.x<from.x);from.y+=(to.y>from.y)-(to.y<from.y);out[n]=from;}return count;
    }
    static bool response(const rt::VisibleActor& rival,const cp::ResponseInput& input,cp::Response& out,const void* context){
        auto& self=*const_cast<Board*>(static_cast<const Board*>(context));++self.quotes;
        out={};out.rival_position=input.rival;out.supported=true;
        out.own_delay=self.delay?1:0;out.recall_interrupted=self.interrupt;
        if(input.decision.branch==cp::Branch::idle)return true;
        if(cp::chebyshev(input.rival,input.decision.target)<=rival.attack_range){
            if(input.decision.branch==cp::Branch::chase && input.decision.target_id==7 &&
                cp::chebyshev(input.rival,input.own.position)<=rival.attack_range)out.own_damage=std::min(rival.strength,input.own_health);
            return true;
        }
        const Point next{input.rival.x+(input.decision.target.x>input.rival.x)-(input.decision.target.x<input.rival.x),
                         input.rival.y+(input.decision.target.y>input.rival.y)-(input.decision.target.y<input.rival.y)};
        if(next!=input.own.position)out.rival_position=next;
        return true;
    }
};
struct Fixture {
    Board board;std::unique_ptr<Information> info=std::make_unique<Information>();Menu menu{};
    rt::ActorFrame actors{};rt::SpatialFrame spatial{};cp::PublicProfile profile{};
    Fixture(){
        auto& i=*info;i.round=10;i.own.id=7;i.own.position={3,3};i.own.health=i.own.maximum_health=100;i.own.bag=24;
        i.group_count=2;i.altar_count=1;i.own_spawn={0,0};i.travel={Board::distance,&board};
        for(int g=0;g<2;++g){auto& group=i.groups[g];group.id=100+g;group.position=g?Point{0,3}:Point{3,9};
            group.full_health=group.health=100;group.full_clear_rounds=group.clear_rounds=2;group.incoming_damage=group.full_incoming_damage=5;
            group.gems=24;group.respawn_rounds=30;group.visible=group.occupancy_known=group.all_spawns_visible=true;
            group.lifecycle=Lifecycle::living;group.provenance=group.health_provenance={10,Source::personal_vision,Status::exact_now};
            menu.jobs[g].kind=JobKind::farm;menu.jobs[g].group=g;menu.jobs[g].altar=0;menu.jobs[g].menu_rank=g;}
        i.altars[0].id=30;i.altars[0].position={9,9};i.altars[0].altar_position={9,8};i.altars[0].lock_known=true;
        menu.jobs[2]=menu.jobs[0];menu.jobs[2].second_group=1;menu.jobs[2].menu_rank=2;
        menu.jobs[3].kind=JobKind::bank;menu.jobs[3].altar=0;menu.jobs[3].menu_rank=3;
        menu.jobs[4].kind=JobKind::renew;menu.jobs[4].menu_rank=4;menu.jobs[5].kind=JobKind::hold;menu.jobs[5].menu_rank=5;
        menu.count=6;menu.single_count=2;
        actors.clock={10,{7}};actors.allied_roster[0]=7;actors.enemy_roster[0]=9;actors.allied_count=actors.enemy_count=1;
        actors.roster_complete=actors.own_observed=actors.draft_order_known=true;
        auto& own=actors.own;own.id=7;own.ally=own.alive=true;own.position=i.own.position;own.bag=i.own.bag;
        own.health=own.maximum_health=100;own.strength=20;own.attack_range=1;own.draft_ordinal=0;own.provenance={10,Source::own_state,Status::exact_now};
        actors.count=1;auto& rival=actors.samples[0];rival.id=9;rival.position=board.occupied;rival.alive=true;
        rival.health=rival.maximum_health=100;rival.strength=20;rival.attack_range=1;rival.draft_ordinal=0;
        rival.provenance={10,Source::personal_vision,Status::exact_now};
        spatial.clock=actors.clock;spatial.information=info.get();spatial.menu=&menu;spatial.topology={10,10,true,Board::walk,&board};
        spatial.occupancy=Board::occupancy;spatial.occupancy_context=&board;spatial.travel=i.travel;
        spatial.route_prefix=Board::prefix;spatial.route_context=&board;spatial.current_complete=true;spatial.graph_quota=192;
        profile.clock=actors.clock;profile.executed_module[0]=1;profile.rule_identity=123;profile.response_identity=456;profile.own_executor_identity=789;
        profile.exact_module=profile.frozen=profile.inspectable=profile.matches_rule=profile.matches_response=profile.matches_own_first_step=true;
        profile.complete_choice_inputs=profile.complete_other_actor_laws=profile.ordinary_response_exact=profile.travel_window_no_other_effects=true;
        profile.order_certified=profile.opposing_teams=profile.vision_modifiers_known=profile.rival_oriented_geometry=true;
        profile.rival_id=9;profile.turns=4;profile.public_vision_radius=9;profile.corridor_min={0,0};profile.corridor_max={9,9};
        profile.rival_geometry={Board::distance,Board::open,&board};profile.camp_count=2;
        for(int g=0;g<2;++g)profile.camp_priority[g]={i.groups[g].id,i.groups[g].position};
        profile.camp_cursor_publicly_derived=true;profile.quote_response=Board::response;profile.quote_context=&board;
    }
    void install(rt::Counterpart& model,bool with_profile=true){model.observe_actors(actors);model.observe_spatial(spatial);if(with_profile)model.observe_profile(profile);model.observe_snapshot(*info);}
};
bool same(const Forecast& a,const Forecast& b){
    return a.event_count==b.event_count && a.final_round==b.final_round && a.final_bag==b.final_bag && a.final_gold==b.final_gold &&
        a.final_health==b.final_health && a.our_secured==b.our_secured && a.macros_started==b.macros_started && a.terminal_z==b.terminal_z &&
        a.first_conversion_round==b.first_conversion_round && a.abort_reason==b.abort_reason && a.budget_exhausted==b.budget_exhausted;
}
std::unique_ptr<Evaluation> evaluate(rt::Counterpart& model,Fixture& fixture){auto out=std::make_unique<Evaluation>();rt::evaluate_model(*fixture.info,fixture.menu,model,*out);return out;}
void absent_and_malformed_profiles_use_both_arm_m0(){
    for(int fault=0;fault<15;++fault){auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Counterpart>();
        switch(fault){case 0:break;case 1:f->profile.exact_module=false;break;case 2:f->profile.executed_module={};break;
        case 3:f->profile.matches_own_first_step=false;break;case 4:f->profile.complete_choice_inputs=false;break;
        case 5:f->profile.complete_other_actor_laws=false;break;case 6:f->profile.order_certified=false;break;
        case 7:f->actors.draft_order_known=false;break;case 8:f->profile.rival_oriented_geometry=false;break;
        case 9:f->profile.vision_modifiers_known=false;break;case 10:f->profile.camp_cursor_publicly_derived=false;break;
        case 11:f->profile.travel_window_no_other_effects=false;break;case 12:f->profile.ordinary_response_exact=false;break;
        case 13:f->profile.camp_priority[0].id=999;break;case 14:f->actors.samples[0].provenance.observed_round=9;break;}
        f->install(*model,fault!=0);auto actual=evaluate(*model,*f);auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(*f->info,f->menu,*expected);
        assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
        for(int n=0;n<f->menu.count;++n)assert(same(actual->forecasts[n],expected->forecasts[n]));
        assert(model->spatial_vertices()>=0 && model->spatial_vertices()<=f->spatial.graph_quota);
    }
}
void explicit_profile_uses_full_common_tail_and_control_freezes_initial_intent(){
    auto f=std::make_unique<Fixture>();auto active=std::make_unique<rt::Counterpart>(),control=std::make_unique<rt::Counterpart>(false);
    f->install(*active);f->install(*control);auto a=evaluate(*active,*f),c=evaluate(*control,*f);
    assert(active->model_evidence().supported && active->model_evidence().coupling_enabled && !control->model_evidence().coupling_enabled);
    assert(a->evaluated_count==f->menu.count && c->evaluated_count==f->menu.count && a->menu.count==f->menu.count);
    assert(a->chosen==choose(*f->info,f->menu,a->forecasts));
    const auto* ar=active->prediction_steps(*f->info,f->menu.jobs[0]);const auto* cr=control->prediction_steps(*f->info,f->menu.jobs[0]);
    assert(ar && cr && ar->count==4 && ar->own_damage>cr->own_damage && ar->final_health<cr->final_health);
    assert(a->forecasts[0].our_secured<c->forecasts[0].our_secured); // The same tail stops an unaffordable later clear.
    const auto* second=control->prediction_steps(*f->info,f->menu.jobs[1]);assert(second);
    assert(second->steps[0].decision.target==cr->steps[0].decision.target);
    assert(a->forecasts[0].macros_started>1 && a->forecasts[2].macros_started>1);
    unsigned captured=0,banked=0;
    for(int e=0;e<a->forecasts[2].event_count;++e){const auto& event=a->forecasts[2].events[e];
        if(event.kind==EventKind::capture)captured+=event.bag_after-event.bag_before;
        if(event.kind==EventKind::bank)banked+=event.secured;}
    assert(captured+f->info->own.bag==banked+a->forecasts[2].final_bag && banked==a->forecasts[2].our_secured);
    assert((f->info->own.health==100 && f->info->own.position==Point{3,3}));
}
void profile_negative_ignores_steps_and_matching_other_laws_are_required(){
    auto f=std::make_unique<Fixture>();f->info->own.bag=f->actors.own.bag=0;
    auto active=std::make_unique<rt::Counterpart>(),control=std::make_unique<rt::Counterpart>(false);f->install(*active);f->install(*control);
    auto a=evaluate(*active,*f),c=evaluate(*control,*f);assert(active->model_evidence().supported && !active->model_evidence().coupling_enabled);
    for(int n=0;n<f->menu.count;++n)assert(same(a->forecasts[n],c->forecasts[n]));
    f->actors.allied_count=2;f->actors.allied_roster[1]=8;f->actors.samples[1]=f->actors.own;f->actors.samples[1].id=8;
    f->actors.samples[1].position={2,2};f->actors.samples[1].provenance={10,Source::personal_vision,Status::exact_now};f->actors.count=2;
    f->install(*active);a=evaluate(*active,*f);assert(!active->model_evidence().supported);
    f->profile.stationary_choice_ids[0]=8;f->profile.stationary_choice_count=1;f->install(*active);a=evaluate(*active,*f);
    assert(active->model_evidence().supported);
    f->actors.samples[1].provenance={};f->install(*active);a=evaluate(*active,*f);assert(!active->model_evidence().supported);
}
void no_unproved_prefix_or_response_mutates_the_forecast(){
    for(int fault=0;fault<6;++fault){auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Counterpart>();
        if(fault==0)f->board.closed={1,1};if(fault==1)f->board.delay=true;if(fault==2)f->board.interrupt=true;
        if(fault==3)f->info->own.channel=Channel::recall;if(fault==4)f->spatial.route_prefix=nullptr;
        if(fault==5)f->spatial.graph_quota=99;
        f->install(*model);auto actual=evaluate(*model,*f);auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(*f->info,f->menu,*expected);
        assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
        for(int n=0;n<f->menu.count;++n)assert(same(actual->forecasts[n],expected->forecasts[n]));
    }
}
void cutoff_and_stale_or_replaced_frames_are_exact(){
    auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=12;auto model=std::make_unique<rt::Counterpart>();f->install(*model);
    auto out=evaluate(*model,*f);for(int n=0;n<f->menu.count;++n)assert(out->forecasts[n].final_round<=12 && out->forecasts[n].our_secured==0);
    assert(model->prediction_steps(*f->info,f->menu.jobs[0]));model->observe_profile(f->profile);
    assert(!model->prediction_steps(*f->info,f->menu.jobs[0]) && !model->model_evidence().supported && model->model_evidence().samples==0);
    ++f->info->round;out=evaluate(*model,*f);assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
    assert(model->spatial_vertices()==0);
}
void other_enemies_and_route_dependencies_are_not_inferred(){
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Counterpart>();
    f->actors.enemy_count=2;f->actors.enemy_roster[1]=10;f->actors.samples[1]=f->actors.samples[0];
    f->actors.samples[1].id=10;f->actors.samples[1].position={8,0};f->actors.count=2;
    f->install(*model);auto out=evaluate(*model,*f);assert(!model->model_evidence().supported);
    f->profile.inert_other_enemy_count=1;f->profile.inert_other_enemy_ids[0]=10;f->install(*model);out=evaluate(*model,*f);
    assert(model->model_evidence().supported);
    f->actors.samples[1].provenance={};f->install(*model);out=evaluate(*model,*f);assert(!model->model_evidence().supported);
    f=std::make_unique<Fixture>();model=std::make_unique<rt::Counterpart>();
    f->spatial.route_prefix=[](Point,Point,Point*,int,const void*){return -1;};f->install(*model);out=evaluate(*model,*f);
    assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
    f=std::make_unique<Fixture>();model=std::make_unique<rt::Counterpart>();
    f->profile.corridor_min={std::numeric_limits<int>::max(),0};f->profile.corridor_max=f->profile.corridor_min;
    f->install(*model);out=evaluate(*model,*f);assert(!model->model_evidence().supported && model->spatial_vertices()==0);
}
void lethal_before_owner_has_no_free_move_and_control_keeps_physical_damage(){
    cp::Input input;input.round=10;input.own_id=7;input.rival_id=9;input.turns=3;input.before={{3,3},24};
    input.after[0]={{3,4},24};input.after[1]={{3,5},24};input.after[2]={{3,6},24};input.own_health=10;input.rival={4,4};
    input.rival_provenance={10,Source::personal_vision,Status::exact_now};input.rival_personally_visible=true;
    input.policy={1,true,true,true};input.geometry={Board::distance,Board::open,nullptr};input.corridor_min={0,0};input.corridor_max={9,9};
    Board board;input.geometry.context=&board;input.public_vision_radius=9;input.vision_modifiers_known=input.complete_choice_inputs=input.order_known=input.opposing_teams=true;
    input.quote_response=[](const cp::ResponseInput& in,cp::Response& out,const void*){out={};out.supported=true;out.rival_position=in.rival;
        out.own_damage=in.round==11?in.own_health:0;return true;};
    cp::Result result;cp::predict_into(input,true,result);
    assert(result.supported && result.predicted_death && result.own_turns==1 && result.final_own.position==input.after[0].position && result.final_own.bag==0);
    auto f=std::make_unique<Fixture>();f->info->own.health=10;cp::Boundary boundary;boundary.turns=3;boundary.after=input.after;
    auto physical=std::make_unique<cp::PhysicalContinuation>();auto out=std::make_unique<Forecast>();
    assert(physical->predict_into(*f->info,f->menu.jobs[0],f->menu,boundary,result,*out));
    assert(out->final_round==11 && out->final_bag==0 && out->our_secured==0 && out->final_health==0 && out->macros_started==0);
}
bool deny(const void*){return false;}
void budget_denial_and_quota_are_not_support(){
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Counterpart>();f->info->work_budget={deny,nullptr};f->install(*model);
    auto out=evaluate(*model,*f);assert(out->budget_exhausted && out->evaluated_count==0 && !model->model_evidence().coupling_enabled && f->board.quotes==0);
    f->info->work_budget={};f->spatial.graph_quota=104;f->install(*model);out=evaluate(*model,*f);
    assert(model->spatial_vertices()<=104 && model->model_evidence().unknown>0);
}
}
int main(){absent_and_malformed_profiles_use_both_arm_m0();explicit_profile_uses_full_common_tail_and_control_freezes_initial_intent();
    profile_negative_ignores_steps_and_matching_other_laws_are_required();no_unproved_prefix_or_response_mutates_the_forecast();
    cutoff_and_stale_or_replaced_frames_are_exact();lethal_before_owner_has_no_free_move_and_control_keeps_physical_damage();budget_denial_and_quota_are_not_support();
    other_enemies_and_route_dependencies_are_not_inferred();
    std::puts("counterpart runtime: 8 synthetic scenarios passed; live public profile/first-step/response telemetry binding pending");}
