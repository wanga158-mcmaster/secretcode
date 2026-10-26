#include "../capital/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;namespace c=plan4::capital;namespace r=plan4::runtime;
int checks=0;void check(bool ok,const char*why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
c::Definitions definitions(){c::Definitions d;d.public_source=123;d.quote_law=77;d.queried_public=true;d.actual_purchase_receipt_parity=true;
    d.health_growth=100;d.strength_growth=5;d.focus_growth=10;d.item[0]={30,5,0,1,0,0,5,0,.1f,true};
    d.item[1]={30,5,1,0,0,5,0,0,.3f,true};d.item[2]={30,5,0,1,0,0,6,0,0,true};return d;}
struct Fixture{
    Information info;Menu menu;c::InvestmentFrame frame;r::CombatKit kit;
    Fixture(){
        info.round=10;info.settings.max_rounds=138;info.own.id=4;info.own.position={1,0};info.own_spawn={5,0};
        info.own.health=info.own.maximum_health=1000;info.own.recall_safe=true;info.group_count=2;info.altar_count=1;
        info.altars[0].id=9;info.altars[0].position={3,0};info.altars[0].altar_position={3,1};info.altars[0].lock_known=true;
        info.travel={distance,nullptr};
        for(int g=0;g<2;++g){auto&s=info.groups[g];s.id=20+g;s.position={g*2,0};s.full_health=1125;s.health=g?1125:0;
            s.lifecycle=g?Lifecycle::living:Lifecycle::cleared;s.reward_available=!g;s.visible=s.all_spawns_visible=s.occupancy_known=true;
            s.provenance=s.health_provenance={10,Source::personal_vision,Status::exact_now};s.gems=g?20:10;s.gold=150;
            s.incoming_damage=g?20:0;s.full_incoming_damage=20;s.clear_rounds=g?20:0;s.full_clear_rounds=20;s.respawn_rounds=200;}
        menu=make_menu(info);frame.clock=kit.clock={10,{4}};frame.stats={1000,50,0,1,1};frame.definitions=definitions();
        frame.position=info.own.position;frame.health=1000;frame.provenance={10,Source::own_state,Status::exact_now};
        frame.inventory_fingerprint=10;frame.champion=11;frame.queried_passive=2;frame.parity_receipts=3;frame.kit_version=5;frame.stat_epoch=7;
        frame.exact=frame.inventory_complete=frame.passive_supported=frame.remote_shop=frame.alive=true;
        kit.champion=11;kit.strength=50;kit.attack_range=3;kit.version=5;kit.stat_epoch=7;kit.exact=true;
    }
    static int distance(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
    int pair()const{for(int n=0;n<menu.count;++n)if(menu.jobs[n].group==0&&menu.jobs[n].second_group==1)return n;return-1;}
    void publish(r::Capital&model){model.observe_combat(kit);model.observe_investment(frame);}
};
c::PurchaseReceipt receipt(c::Family family){
    c::PurchaseReceipt p;p.family=family;p.count=1;p.isolated_successful_legal_purchase=true;
    auto&a=p.before;a.clock={10,{4}};a.stats={1000,50,100,1,1};a.definitions=definitions();a.position={0,0};a.health=500;a.cash=30;
    a.provenance={10,Source::own_state,Status::exact_now};a.inventory_fingerprint=1;a.champion=1;a.queried_passive=0;
    a.exact=a.inventory_complete=a.passive_supported=a.alive=a.on_spawn=true;
    const unsigned f=unsigned(family);a.equipment.total_shards[f]=4;
    if(f==1)a.stats.maximum_health=1400;else a.stats.strength=70;
    p.after=a;auto&b=p.after;b.cash=0;b.inventory_fingerprint=2;b.equipment.total_shards[f]=5;
    if(f==0){b.stats.strength=75;b.stats.camp_outgoing=1.1f;}
    if(f==1){b.stats.maximum_health=1500;b.stats.camp_incoming=.7f;b.health=600;}
    if(f==2)b.stats.strength=80;
    return p;
}
bool no_budget(const void*){return false;}
struct Gate{int left=150;static bool available(const void*p){return --const_cast<Gate*>(static_cast<const Gate*>(p))->left>=0;}};
struct Receipts{int count=0,limit=8;static bool sink(const r::ModelPredictionEvent&row,void*context){auto&self=*static_cast<Receipts*>(context);
    check(row.actor_id==4&&(row.group_id==-1||row.group_id==20||row.group_id==21),"prediction receipt maps actual actor/source identities");
    return ++self.count<self.limit;}};
void parity(){
    c::Parity law;
    for(unsigned f=0;f<3;++f){const auto p=receipt(c::Family(f));check(c::verify_purchase_receipt(p),"isolated synthetic before/after receipt matches queried family combination law");
        check(law.observe(p),"parity coverage is accumulated from actual-shaped isolated receipt inputs");}
    check(law.supported(definitions())&&law.receipts==3,"all three families and their combination edges must be covered before production gating");
    auto bad=receipt(c::Family::monster_strength);bad.after.cash=30;
    check(!c::verify_purchase_receipt(bad),"free equipment cannot establish purchase parity");
    bad=receipt(c::Family::power_strength);bad.after.equipment.total_shards[2]=4;
    check(!c::verify_purchase_receipt(bad),"owned Power ingredients must be observed rather than inferred from strength");
    bad=receipt(c::Family::monster_strength);bad.after.definitions.item[0].cost=31;
    check(!c::verify_purchase_receipt(bad),"reused public-source labels cannot hide contradictory queried definitions");
    bad=receipt(c::Family::jungle_health);bad.before.on_spawn=bad.after.on_spawn=false;bad.before.remote_shop=bad.after.remote_shop=true;
    check(!c::verify_purchase_receipt(bad),"field Health healing is rejected by actual receipt parity");bad.after.health=500;
    check(c::verify_purchase_receipt(bad),"field capacity without healing matches the lawful quote");
    bad.isolated_successful_legal_purchase=false;check(!c::verify_purchase_receipt(bad),"round-delta correlation cannot substitute for an isolated legal buy");
    bad=receipt(c::Family::monster_strength);bad.after.stats.strength=76;
    check(!law.observe(bad)&&!law.supported(definitions()),"a contradictory receipt poisons production support instead of preserving old calibration");
}
int main(){
    parity();auto f=std::make_unique<Fixture>();auto pathA=std::make_unique<c::Path>();auto pathC=std::make_unique<c::Path>();
    auto a=std::make_unique<Forecast>();auto b=std::make_unique<Forecast>();auto ea=std::make_unique<c::ProcessEvidence>();auto eb=std::make_unique<c::ProcessEvidence>();
    const int pair=f->pair();check(pair>=0,"ordinary complete menu contains ordered two-source FARM");
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(f->menu.jobs[pair],true)&&
          c::Process(f->info,f->menu,f->frame,*pathC,*b,*eb).run(f->menu.jobs[pair],false),"both arms execute the same full ordered macro and cyclic continuation");
    int firstBank=-1,captures=0,clearA=-1,clearC=-1;
    for(int n=0;n<a->event_count;++n){const auto&e=a->events[n];if(e.kind==EventKind::bank){firstBank=n;break;}captures+=e.kind==EventKind::capture;
        if(e.kind==EventKind::clear&&e.group_id==21)clearA=e.end.round-e.start.round;}
    for(int n=0;n<b->event_count;++n)if(b->events[n].kind==EventKind::clear&&b->events[n].group_id==21){clearC=b->events[n].end.round-b->events[n].start.round;break;}
    check(firstBank>=0&&captures==2,"ordered pair preserves both captures before its single completed conversion");
    check(clearA==13&&clearC==20,"capture-funded Monster purchase changes the second camp inside the same FARM pair");
    check(a->our_secured==30&&b->our_secured==30&&a->first_conversion_round<b->first_conversion_round,
          "objective credits only completed bank output; earlier clear translates to earlier conversion");
    check(pathA->cash==pathC->cash&&pathA->equipment.total_shards==pathC->equipment.total_shards&&pathA->cash==0,
          "both arms debit the identical earned purchases once without a gold-to-score bonus");
    check(pathA->accounting.maximum_health==1500&&pathC->accounting.maximum_health==1500&&pathC->effective.maximum_health==1000&&
          pathA->health<pathA->effective.maximum_health,"field Health affects active capacity but gives no instant healing");
    f=std::make_unique<Fixture>();for(int g=0;g<2;++g)f->info.groups[g].gold=0;
    auto reference=std::make_unique<Forecast>();plan4::predict_into(f->info,f->menu.jobs[f->pair()],f->menu,*reference);
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(f->menu.jobs[f->pair()],true)&&a->our_secured==reference->our_secured&&
          a->first_conversion_round==reference->first_conversion_round&&a->final_health==reference->final_health&&a->final_bag==reference->final_bag,
          "without purchases the private process matches shared M0 economic timing and health on the same fixed pair");
    f=std::make_unique<Fixture>();f->frame.remote_shop=false;f->frame.cash=f->info.own.gold=300;f->frame.health=f->info.own.health=300;
    Job renewal;renewal.kind=JobKind::renew;
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(renewal,true)&&c::Process(f->info,f->menu,f->frame,*pathC,*b,*eb).run(renewal,false),
          "nonremote cash can be invested at actual predicted spawn arrival before restoration");
    int healsA=0,healsC=0;
    for(int n=0;n<a->event_count;++n){if(a->events[n].kind==EventKind::travel)break;healsA+=a->events[n].kind==EventKind::heal;}
    for(int n=0;n<b->event_count;++n){if(b->events[n].kind==EventKind::travel)break;healsC+=b->events[n].kind==EventKind::heal;}
    check(pathA->purchases[0].round==15&&pathC->purchases[0].round==15&&healsA==3&&healsC==4,
          "same purchase precedes the first arrival-turn heal; only active capacity/healing law changes future restore time");
    f=std::make_unique<Fixture>();f->info.settings.winning_gems=30;
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(f->menu.jobs[f->pair()],true)&&a->terminal_z==1&&a->macros_started==1,
          "first winning conversion cuts off all later macros and speculative purchases");
    f=std::make_unique<Fixture>();f->info.settings.max_rounds=30;
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(f->menu.jobs[f->pair()],true)&&a->our_secured==0&&a->final_bag==10&&
          pathA->purchase_count==1&&pathA->cash==0,"cutoff through the second clear keeps only first acquired bag and already debited purchase");
    f=std::make_unique<Fixture>();f->frame.definitions.item[0].cost=1000;
    check(c::Process(f->info,f->menu,f->frame,*pathA,*a,*ea).run(f->menu.jobs[f->pair()],true)&&
          c::Process(f->info,f->menu,f->frame,*pathC,*b,*eb).run(f->menu.jobs[f->pair()],false)&&
          a->first_conversion_round==b->first_conversion_round&&pathA->purchase_count==0&&pathC->purchase_count==0,
          "below the same affordability boundary neither arm invents an upgrade or earlier conversion");
    auto active=std::make_unique<r::Capital>(true);auto control=std::make_unique<r::Capital>(false);auto output=std::make_unique<Evaluation>();
    f=std::make_unique<Fixture>();f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(active->used_capital()&&output->evaluated_count==f->menu.count&&active->model_evidence().reported&&active->model_evidence().samples==3,
          "runtime evaluates every unchanged menu row before common selection with explicit parity evidence");
    const auto chosen=output->menu.jobs[output->chosen];Receipts receipts;
    active->prediction_receipts(f->info,chosen,Receipts::sink,&receipts);
    check(receipts.count>0&&receipts.count<=8,"chosen supported path emits only its cached salient bounded prefix");
    --f->info.own.health;Receipts stale;
    active->prediction_receipts(f->info,chosen,Receipts::sink,&stale);
    check(stale.count==0,"same-round changed physical health rejects cached Capital prediction receipts");++f->info.own.health;
    f->publish(*control);r::evaluate_model(f->info,f->menu,*control,*output);
    check(control->used_capital()&&!control->model_evidence().coupling_enabled,"disabled binds the same complete runtime and only cuts future stat coupling");
    f->frame.definitions.actual_purchase_receipt_parity=false;f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital()&&active->unsupported()==c::Unsupported::parity,"missing actual public-quote parity sends the whole decision to M0");
    auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(f->info,f->menu,*expected);
    check(output->chosen==expected->chosen&&output->evaluated_count==expected->evaluated_count,"unsupported parity cannot rank a private supported subset");
    for(int n=0;n<output->evaluated_count;++n)check(output->forecasts[n].our_secured==expected->forecasts[n].our_secured&&
        output->forecasts[n].first_conversion_round==expected->forecasts[n].first_conversion_round&&output->forecasts[n].final_gold==expected->forecasts[n].final_gold&&
        output->forecasts[n].final_health==expected->forecasts[n].final_health&&output->forecasts[n].event_count==expected->forecasts[n].event_count,
        "fallback replaces every candidate's economic forecast and event prefix with shared M0");
    f=std::make_unique<Fixture>();f->frame.equipment.total_shards[2]=10;f->frame.inventory_complete=false;f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital(),"incomplete actual Power inventory cannot be recovered from current stats");
    f=std::make_unique<Fixture>();f->frame.passive_supported=false;f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital(),"an unquoted champion passive remains unsupported");
    f=std::make_unique<Fixture>();f->frame.stat_epoch=8;f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital(),"changed kit epoch invalidates the current causal quote binding");
    f=std::make_unique<Fixture>();f->info.groups[0].occupied_spawn=true;f->info.groups[0].eligible_allied_occupants=2;
    f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital()&&active->unsupported()==c::Unsupported::occupancy,
          "observed other harvesters cannot turn team gold or a future unknown split into own investment cash");
    f=std::make_unique<Fixture>();Gate gate;f->info.work_budget={Gate::available,&gate};f->publish(*active);r::evaluate_model(f->info,f->menu,*active,*output);
    check(!active->used_capital()&&output->budget_exhausted&&output->evaluated_count==0&&output->chosen==-1,
          "budget loss discards every partial model prefix rather than mixing a funded candidate with M0 tails");
    std::printf("plan4-capital-runtime: %d checks passed (conditional synthetic frames; no engine parity witness)\n",checks);
}
