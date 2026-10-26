// Conditional pure bindings; these synthetic rows are never production data.
#include "../kernel/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace r=plan4::runtime;namespace k=plan4::kernel;
namespace {
int checks=0;void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool same(const Forecast& a,const Forecast& b){
    if(a.event_count!=b.event_count||a.macros_started!=b.macros_started||a.final_round!=b.final_round||a.first_conversion_round!=b.first_conversion_round||
        a.our_secured!=b.our_secured||a.enemy_secured!=b.enemy_secured||a.final_bag!=b.final_bag||a.final_gold!=b.final_gold||a.final_health!=b.final_health||
        a.terminal_z!=b.terminal_z||a.terminal_unknown!=b.terminal_unknown||a.stock_unknown!=b.stock_unknown||a.travel_unknown!=b.travel_unknown||
        a.lock_unknown!=b.lock_unknown||a.truncated!=b.truncated||a.completed_conversion!=b.completed_conversion||a.no_conversion!=b.no_conversion||
        a.budget_exhausted!=b.budget_exhausted||a.abort_reason!=b.abort_reason||a.objective.paths!=b.objective.paths)return false;
    for(int n=0;n<a.event_count;++n){const auto& x=a.events[n];const auto& y=b.events[n];
        if(x.kind!=y.kind||x.start.round!=y.start.round||x.start.phase!=y.start.phase||x.end.round!=y.end.round||x.end.phase!=y.end.phase||
            x.group_id!=y.group_id||x.bag_before!=y.bag_before||x.bag_after!=y.bag_after||x.secured!=y.secured||x.gold_after!=y.gold_after||
            x.health_after!=y.health_after||x.status!=y.status)return false;}return true;
}
struct Fixture {
    std::unique_ptr<Information> i=std::make_unique<Information>();Menu menu{};
    r::kernel_binding::ExactKitFrame kit{};r::CombatKit combat{};r::ActorFrame actors{};
    std::array<r::PublicGroup,2> groups{};std::array<r::PublicMember,2> members{};std::array<r::VisibleMember,2> visible{};
    std::array<k::StartedRow,9> rows;std::unique_ptr<k::FrozenTable> table;
    k::BinKey key{};bool missing_distance=false;
    Fixture(){auto& info=*i;info.round=10;info.settings.max_rounds=74;info.own.id=0;info.own.health=info.own.maximum_health=1000;
        info.own_spawn={0,0};info.own.on_spawn=true;info.travel={distance,this};info.group_count=info.altar_count=1;
        auto& g=info.groups[0];g.id=20;g.position={2,0};g.health=g.full_health=225;g.clear_rounds=g.full_clear_rounds=2;
        g.incoming_damage=g.full_incoming_damage=1;g.gems=24;g.gold=4;g.respawn_rounds=12;g.lifecycle=Lifecycle::living;
        g.visible=g.all_spawns_visible=g.occupancy_known=true;g.provenance=g.health_provenance={10,Source::personal_vision,Status::exact_now};
        info.altars[0].id=50;info.altars[0].position={4,0};info.altars[0].altar_position={4,1};info.altars[0].lock_known=true;
        menu=make_menu(info);kit.kit.known=true;kit.kit.champion=1;kit.kit.first_ability=2;kit.kit.second_ability=3;kit.kit.attack_range=1;kit.kit.strength=100;
        kit.kit_version=5;kit.stat_epoch=2;kit.complete_inventory=kit.abilities_known=true;
        groups[0]={20,0,1,true};members[0]={20,100,0,1,2,3,{2,0},225};
        sync();rebuild();
    }
    static int distance(Point a,Point b,const void* p){const auto& f=*static_cast<const Fixture*>(p);
        return f.missing_distance||a.y||b.y||a.x<0||b.x<0||a.x>8||b.x>8?-1:std::abs(a.x-b.x);}
    void sync(){auto& info=*i;kit.clock={info.round,{info.own.id}};kit.provenance={info.round,Source::own_state,Status::exact_now};
        combat={kit.clock,kit.kit.champion,kit.kit.attack_range,kit.kit.strength,kit.kit.focus,0,0,kit.kit_version,kit.stat_epoch,true};
        actors.clock=kit.clock;actors.roster_complete=actors.own_observed=true;actors.allied_count=actors.enemy_count=1;
        actors.allied_roster[0]=info.own.id;actors.enemy_roster[0]=7;actors.own.id=info.own.id;actors.own.ally=actors.own.alive=true;
        actors.own.position=info.own.position;actors.own.health=info.own.health;actors.own.maximum_health=info.own.maximum_health;actors.own.bag=info.own.bag;
        actors.own.champion=kit.kit.champion;actors.own.attack_range=kit.kit.attack_range;actors.own.strength=kit.kit.strength;actors.own.provenance=kit.provenance;
        for(int g=0;g<info.group_count;++g)visible[g]={info.groups[g].id,100+g,0,1,info.groups[g].position,info.groups[g].health,{info.round,Source::personal_vision,Status::exact_now}};
    }
    k::BinKey bin(k::Macro macro=k::Macro::one_camp){k::Composition comp;comp.known=true;if(macro==k::Macro::one_camp){comp.count=1;comp.kinds[0]=2;}
        return k::preaction_bin(macro,kit.kit,comp,actors.count?k::Context::other_actor_visible:k::Context::solo_visible_context,
            i->own.bag,i->own.health,i->own.maximum_health,4);}
    void rebuild(int count=8){key=bin();for(int n=0;n<9;++n){auto& row=rows[n];row=k::StartedRow{};row.bin=key;row.key={0,0,unsigned(n),0};
            row.origin=k::Origin::synthetic_test;row.right_censored=false;row.required_receipts=row.solo_receipts=true;
            row.nominal_duration=15;row.duration=n%2?100:20;row.endpoint=n%2?k::Symbol::altar:k::Symbol::target;
            row.phase_count=n%2?5:3;row.phases[0]={k::PhaseKind::approach,{0,1},{1,10}};row.phases[1]={k::PhaseKind::clear,{1,10},{2,10}};
            row.phases[2]={k::PhaseKind::capture,{2,10},n%2?k::Fraction{6,10}:k::Fraction{1,1}};
            row.event_count=n%2?3:2;row.tape[0]={k::EconomicKind::group_capture,2,{1,1},Phase::capture,false};
            row.tape[1]={k::EconomicKind::solo_reward,2,{1,1},Phase::capture,false};
            if(n%2){row.phases[3]={k::PhaseKind::conversion_approach,{6,10},{7,10}};row.phases[4]={k::PhaseKind::conversion,{7,10},{1,1}};
                row.tape[2]={k::EconomicKind::whole_bag_conversion,4,{1,1},Phase::owner_start,false};}}
        freeze(count);
    }
    void freeze(int count=8){table=std::make_unique<k::FrozenTable>();const std::array<k::BinKey,1> bins{{key}};
        check(table->freeze(bins,std::span<const k::StartedRow>(rows.data(),count),k::FreezeMode::synthetic_tests_allowed),"synthetic table frozen in explicit test mode");}
    void feed(r::Kernel& model,bool configure=true){if(configure)check(model.configure({table.get(),123,false}),"test-only conditional profile supplied explicitly");
        model.observe_exact_kit(kit);model.observe_combat(combat);model.observe_actors(actors);
        model.observe_layout({kit.clock,groups.data(),i->group_count,members.data(),i->group_count,false});
        model.observe_snapshot(*i,{visible.data(),i->group_count,false});}
    int farm()const{for(int n=0;n<menu.count;++n)if(menu.jobs[n].kind==JobKind::farm&&menu.jobs[n].second_group<0)return n;return -1;}
};
std::unique_ptr<Forecast> predict(r::Kernel& model,Fixture& f,int index=-1){auto out=std::make_unique<Forecast>();
    model.predict_into(*f.i,f.menu.jobs[index<0?f.farm():index],f.menu,*out);return out;}
void fallback(r::Kernel& model,Fixture& f,const char* text){auto out=predict(model,f),base=std::make_unique<Forecast>();
    plan4::predict_into(*f.i,f.menu.jobs[f.farm()],f.menu,*base);check(same(*out,*base),text);check(!model.model_evidence().supported,"fallback does not claim empirical support");}
void cutoff_and_actual_tail(){Fixture f;r::Kernel active(true,k::Use::synthetic_tests_allowed),control(false,k::Use::synthetic_tests_allowed);f.feed(active);f.feed(control);
    auto a=predict(active,f),c=predict(control,f);
    if(!a->objective.paths)std::printf("initial fallback reason=%d work=%d\n",active.model_evidence().reason,active.model_evidence().work);
    check(a->objective.paths==8&&c->objective.paths==8,"all eight outcomes in both pairings have full pathwise evaluation");
    check(a->objective.our_secured==0&&c->objective.our_secured>0,"near-cutoff joint/control difference is computed after each path cutoff");
    check(active.model_evidence().supported&&active.model_evidence().paths==16&&active.model_evidence().samples==8,"diagnostics report all sixteen mapped checked paths");
    check(control.model_evidence().supported&&!control.model_evidence().coupling_enabled,"control uses identical tapes and common continuations");
    check(a->final_bag==24&&a->our_secured==0,"captured target endpoint stays occupied and cannot silently reharvest same stock");
    std::printf("conditional cutoff secured sums active=%llu control=%llu (8 paths)\n",static_cast<unsigned long long>(a->objective.our_secured),static_cast<unsigned long long>(c->objective.our_secured));
    auto again=predict(active,f);check(a->objective.our_secured==again->objective.our_secured&&a->objective.first_conversion_round==again->objective.first_conversion_round,"path state resets across decisions");
    f.i->settings.winning_gems=24;f.feed(active,false);f.feed(control,false);a=predict(active,f);c=predict(control,f);
    check(a->objective.terminal_z==0&&c->objective.terminal_z==4,"terminal threshold applies to individual conversions before expectation");
}
void default_and_production_gate(){Fixture f;for(bool enabled:{true,false}){r::Kernel model(enabled);check(!model.configure({f.table.get(),123,false}),"test table cannot bind production profile");
        f.feed(model,false);fallback(model,f,"ordinary default or synthetic table is exact M0 in production");
        check(model.configure({f.table.get(),123,true}),"table identity alone is not the cohort qualification check");f.feed(model,false);fallback(model,f,"synthetic rows remain excluded even with claimed production flag");}}
void source_and_bin_guards(){for(int change=0;change<12;++change)for(bool enabled:{true,false}){Fixture f;r::Kernel model(enabled,k::Use::synthetic_tests_allowed);f.feed(model);
        switch(change){case 0:{auto v=f.kit;v.complete_inventory=false;model.observe_exact_kit(v);break;}
        case 1:{auto v=f.kit;v.abilities_known=false;model.observe_exact_kit(v);break;}
        case 2:{auto v=f.kit;--v.clock.round;model.observe_exact_kit(v);break;}
        case 3:{auto v=f.combat;v.version++;model.observe_combat(v);break;}
        case 4:{auto v=f.actors;v.truncated=true;model.observe_actors(v);break;}
        case 5:f.i->own.bag=1;break;
        case 6:f.kit.kit.items[0]={9,1};f.kit.kit.item_count=1;model.observe_exact_kit(f.kit);break;
        case 7:f.kit.kit.first_ability=9;model.observe_exact_kit(f.kit);break;
        case 8:f.i->public_layout_truncated=true;break;
        case 9:f.missing_distance=true;break;
        case 10:{auto v=f.actors;v.roster_complete=false;model.observe_actors(v);break;}
        case 11:f.i->groups[0].eligible_allied_occupants=2;break;}
        fallback(model,f,"stale source/exact kit/inventory/bin/context/quote cannot reuse old empirical forecast");}}
void fresh_new_bins_are_not_stale_source_fallbacks(){for(int change=0;change<3;++change){Fixture f;
        if(change==0)f.i->own.health=700;
        if(change==1)f.i->own.bag=1;
        if(change==2){f.actors.count=1;auto& other=f.actors.samples[0];other.id=7;other.position={6,0};other.alive=true;
            other.health=other.maximum_health=1000;other.provenance={10,Source::personal_vision,Status::exact_now};}
        f.sync();r::Kernel model(true,k::Use::synthetic_tests_allowed);f.feed(model);fallback(model,f,"new exact HP/bag/other-visible context needs its own declared bin");
        check(model.model_evidence().reason==int(r::kernel_binding::Reason::cohort),"fresh changed bin is distinct from stale source");}}
void composition_and_endpoint_guards(){for(int change=0;change<10;++change)for(bool enabled:{true,false}){Fixture f;r::Kernel model(enabled,k::Use::synthetic_tests_allowed);
        switch(change){case 0:f.members[0].kind=8;break;case 1:f.members[0].maximum_health=226;break;
        case 2:f.visible[0].health=224;break;case 3:f.visible[0].provenance.observed_round=9;break;
        case 4:f.rows[0].event_count=0;break; // Uncaptured target: no legal residual work state.
        case 5:f.rows[0].endpoint=k::Symbol::failed_approach;break;
        case 6:f.rows[0].endpoint=k::Symbol::spawn;break;
        case 7:f.rows[1].phases[3].begin={4,10};break; // Travel cannot depart before capture.
        case 8:f.groups[0].complete=false;break;
        case 9:f.rows[1].event_count=2;break;}
        if((change>=4&&change<=7)||change==9)f.freeze();
        f.feed(model);fallback(model,f,"composition or physical endpoint alias rejects all sixteen rows in both arms");}}
void fixed_menu_and_stock_depletion(){Fixture f;f.i->group_count=2;f.i->groups[1]=f.i->groups[0];f.i->groups[1].id=21;f.i->groups[1].position={6,0};f.i->groups[1].gems=16;
    f.groups[1]={21,1,1,true};f.members[1]={21,101,0,1,2,3,{6,0},225};f.sync();f.menu=make_menu(*f.i);
    r::Kernel model(true,k::Use::synthetic_tests_allowed);f.feed(model);auto result=std::make_unique<Evaluation>();r::evaluate_model(*f.i,f.menu,model,*result);
    check(result->evaluated_count==f.menu.count&&result->menu.count==f.menu.count,"same fixed menu includes M0 pair BANK RENEW HOLD candidates");
    check(result->chosen==choose(*f.i,f.menu,result->forecasts),"common selector alone compares exact rational objectives");
    bool other=false;for(int n=0;n<f.menu.count;++n){const auto& j=f.menu.jobs[n];const auto& p=result->forecasts[n];
        if(j.kind!=JobKind::farm||j.second_group>=0){auto base=std::make_unique<Forecast>();plan4::predict_into(*f.i,j,f.menu,*base);check(same(p,*base),"unmodeled job kinds retain whole fixed M0 forecast");}
        if(j.kind==JobKind::farm&&j.second_group<0&&p.objective.paths){for(int e=0;e<p.event_count;++e)other|=p.events[e].kind==EventKind::capture&&p.events[e].group_id!=f.i->groups[j.group].id;}}
    check(other,"known captured endpoint advances into actual different physical camp through the common cyclic list");
}
void bank_full_continuation(){Fixture f;f.i->own.bag=11;f.sync();f.menu=make_menu(*f.i);f.key=f.bin(k::Macro::bank);
    for(int n=0;n<8;++n){auto& row=f.rows[n];row=k::StartedRow{};row.bin=f.key;row.key={0,0,unsigned(n),0};row.origin=k::Origin::synthetic_test;
        row.right_censored=false;row.required_receipts=row.solo_receipts=true;row.nominal_duration=6;row.duration=12;row.endpoint=k::Symbol::altar;
        row.phase_count=2;row.phases[0]={k::PhaseKind::approach,{0,1},{1,2}};row.phases[1]={k::PhaseKind::conversion,{1,2},{1,1}};
        row.event_count=1;row.tape[0]={k::EconomicKind::whole_bag_conversion,1,{1,1},Phase::owner_start,false};}
    f.freeze();int bank=-1;for(int n=0;n<f.menu.count;++n)if(f.menu.jobs[n].kind==JobKind::bank)bank=n;
    check(bank>=0,"fixed menu contains current positive-bag BANK");
    for(bool enabled:{true,false}){r::Kernel model(enabled,k::Use::synthetic_tests_allowed);f.feed(model);auto out=predict(model,f,bank);
        check(out->objective.paths==8&&model.model_evidence().supported,"BANK uses all eight full common continuations in both arms");
        check(out->events[0].kind==EventKind::bank&&out->events[0].secured==11,"BANK instantiates candidate whole bag instead of training credit");
        bool capture=false;for(int e=1;e<out->event_count;++e)capture|=out->events[e].kind==EventKind::capture&&out->events[e].group_id==20;
        check(capture,"BANK endpoint preserves initial farm stock for actual later common acquisition");}
}
void censor_and_budget(){for(int change=0;change<4;++change)for(bool enabled:{true,false}){Fixture f;
        if(change==0)f.rows[0].right_censored=true;
        if(change==1)f.rows[0].required_receipts=false;
        if(change==2)f.rows[0].origin=k::Origin::reference_held_out;
        if(change==3)f.rows[0].solo_receipts=false;
        f.freeze(change==2?8:9);r::Kernel model(enabled,k::Use::synthetic_tests_allowed);f.feed(model);
        fallback(model,f,"censored/missing/nonsolo cohort cannot select survivors; held-out rows supply no fit coverage");}
    struct Gate{int left=180;static bool available(const void* p){return --const_cast<Gate*>(static_cast<const Gate*>(p))->left>=0;}};
    for(bool enabled:{true,false}){Fixture f;Gate gate;f.i->work_budget={Gate::available,&gate};r::Kernel model(enabled,k::Use::synthetic_tests_allowed);f.feed(model);
        auto out=std::make_unique<Evaluation>();r::evaluate_model(*f.i,f.menu,model,*out);check(out->budget_exhausted&&out->evaluated_count==0&&out->chosen==-1,"budget loss discards the entire batch without a surviving-row expectation");
        check(!model.model_evidence().supported&&model.model_evidence().reason==int(r::kernel_binding::Reason::budget),"callback budget denial remains classified as budget");}
}
}
int main(){cutoff_and_actual_tail();default_and_production_gate();source_and_bin_guards();fresh_new_bins_are_not_stale_source_fallbacks();composition_and_endpoint_guards();fixed_menu_and_stock_depletion();bank_full_continuation();censor_and_budget();
    std::printf("plan4-kernel-runtime: %d checks passed (conditional synthetic table; no production cohort or engine witnesses)\n",checks);}
