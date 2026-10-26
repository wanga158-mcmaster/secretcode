#include "capital_service_forecast.h"
#include <cstdio>
#include <initializer_list>
namespace f=wb::capital::service_forecast;
static unsigned checks{},failures{};
static_assert(std::is_same_v<decltype(f::Body{}.hp),float>);
static_assert(std::is_same_v<decltype(f::Body{}.strength),float>);
static_assert(std::is_same_v<decltype(f::Body{}.camp_multiplier),float>);
static_assert(std::is_same_v<decltype(f::Body{}.shield_modifier),float>);
static_assert(std::is_same_v<decltype(f::Npc{}.hp),float>);
static_assert(std::is_same_v<decltype(f::External{}.hp),float>);
static_assert(std::is_same_v<decltype(f::Effect{}.amount),float>);
static_assert(std::is_same_v<decltype(f::PurchaseEffect{}.health_growth),float>);
static void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
struct Environment {bool known{true},open{true},purchase_known{true},arrival_known{true},multi_home{},home_completion{};
    int damage_round{-1},stun_round{-1},lock_round{-1},recall_update{-1};f::Scalar damage{},health_growth{100.0f},strength_growth{5.0f},focus_growth{2.0f};unsigned permits{100000},damage_actor{};};
static f::Verdict legal(void* c,const f::World&,unsigned,const f::Primitive&){return static_cast<Environment*>(c)->known?f::Verdict::Allowed:f::Verdict::Unknown;}
static f::Verdict visible(void* c,const f::World&,unsigned,int){return static_cast<Environment*>(c)->known?f::Verdict::Allowed:f::Verdict::Unknown;}
static f::Verdict spawn(void* c,const f::World&,unsigned,f::Point p){if(!static_cast<Environment*>(c)->known)return f::Verdict::Unknown;
    const auto& e=*static_cast<Environment*>(c);return p.x==0&&(p.y==1||(e.multi_home&&p.y>=1&&p.y<=3))?f::Verdict::Allowed:f::Verdict::Blocked;}
static f::Verdict altar(void* c,const f::World& w,unsigned,f::Point p){const auto& e=*static_cast<Environment*>(c);
    if(e.lock_round>=0&&(w.round>e.lock_round||(w.round==e.lock_round&&w.slot>0)))return f::Verdict::Blocked;
    return e.open&&p==(e.multi_home?f::Point{5,2}:f::Point{5,1})?f::Verdict::Allowed:f::Verdict::Unknown;}
static f::Verdict contested(void* c,const f::World&){return static_cast<Environment*>(c)->known?f::Verdict::Allowed:f::Verdict::Unknown;}
static f::Reply reply(void* c,const f::World& w,int){auto& e=*static_cast<Environment*>(c);f::Reply r;r.complete=e.known;r.domain=42;
    if(w.round==e.damage_round)r.effects[r.count++]={f::EffectKind::Damage,w.bodies[e.damage_actor].id,e.damage};
    if(w.round==e.stun_round)r.effects[r.count++]={f::EffectKind::Stun,w.bodies[0].id,0,1};
    if(w.round==e.lock_round)r.effects[r.count++]={f::EffectKind::BankLock,w.bodies[0].id};return r;}
static bool arrival(void* c,const f::World&,unsigned a,f::Point& p){const auto& e=*static_cast<Environment*>(c);p={0,e.multi_home?int(a)+1:1};return e.arrival_known;}
static f::PurchaseEffect purchase(void* c,const f::World& w,unsigned a,const f::Purchase& p){f::PurchaseEffect q;
    q.complete=static_cast<Environment*>(c)->purchase_known;q.revision=p.quote_revision;q.cost=p.quoted_cost;
    const auto& e=*static_cast<Environment*>(c);q.health_growth=e.health_growth;q.strength_growth=e.strength_growth;q.focus_growth=e.focus_growth;q.items=w.bodies[a].items;q.items[p.item]+=p.count;
    // This test callback supplies the exact queried transition for a fifth
    // Home Strength shard (pinned SDK IDs30/31). The simulator never infers it.
    if(e.home_completion&&p.item==30&&q.items[30]==5){q.items[30]=0;++q.items[31];}
    q.camp_multiplier=w.bodies[a].camp_multiplier;q.taken_multiplier=w.bodies[a].taken_multiplier;q.npc_taken_multiplier=w.bodies[a].npc_taken_multiplier;q.gold_multiplier=w.bodies[a].gold_multiplier;
    q.effective_recall_ticks=e.recall_update<0?w.bodies[a].effective_recall_ticks:unsigned(e.recall_update);return q;}
static bool work(void* c,unsigned units){auto& e=*static_cast<Environment*>(c);if(e.permits<units)return false;e.permits-=units;return true;}
static f::Callbacks callbacks(Environment& e){return {&e,&legal,&visible,&spawn,&altar,&contested,&reply,&arrival,&purchase,&work,&visible};}
static f::Domain domain(){return {9,11,42,13,77,true,true,true,true,true,false};}
static f::World world(){f::World w;w.round=0;w.horizon=16;w.team=0;w.actors=w.npcs=w.slots=1;w.revision=9;
    auto& b=w.bodies[0];b.id=3;b.kit=f::Kit::Varg;b.pose={1,1};b.hp=b.max_hp=1000;b.strength=50;b.focus=100;
    b.actual=b.bag_known=b.gold_known=true;b.shield_modifier=2.5;b.shield_duration=5;b.heal_modifier=3;b.heal_range=4;
    b.splash_radius=1.0f;b.splash_radius_known=true;
    b.effective_recall_ticks=2;
    auto& c=w.camps[0];c.id=10;c.pose={2,1};c.hp=45;c.max_hp=500;c.range=3;c.actual=true;c.empty_batches_known=true;
    w.group.id=4;w.group.life=f::Life::Alive;w.group.roster_complete=true;w.group.gold=60;w.group.gems=14;
    w.rules={true,2,2,10,8,8,8,2,0.1,1,true,true,2000,1000,0};w.even[0]=w.odd[0]={true,0,-1};return w;}
static void leg(f::Program& p,unsigned actor,f::LegKind kind,f::Point origin,std::initializer_list<f::Point> points){
    auto& b=p.bodies[actor];b.legs[unsigned(kind)]=int(p.leg_count);auto& l=p.legs[p.leg_count++];l.origin=origin;l.begin=p.route_count;l.count=unsigned(points.size());l.complete=true;
    for(auto q:points)p.route[p.route_count++]={q,0,1000};}
static f::Program program(){f::Program p{};p.id=17;
    p.bodies[0].post_renewal=f::PostRenewal::ConditionalHold;
    leg(p,0,f::LegKind::Work,{1,1},{});leg(p,0,f::LegKind::Capture,{1,1},{{2,1}});
    leg(p,0,f::LegKind::Release,{2,1},{{3,1}});leg(p,0,f::LegKind::Altar,{3,1},{{4,1}});
    leg(p,0,f::LegKind::Spawn,{4,1},{{3,1},{2,2},{1,2},{0,1}});leg(p,0,f::LegKind::Renew,{0,1},{{1,1}});
    p.bodies[0].altar={5,1};p.bodies[0].altar_known=true;p.bodies[0].purchase_count=1;
    p.purchase_count=1;p.purchases[0]={41,1,101,30};return p;}
static const f::Result& run(f::Simulator& sim,const f::World& w,const f::Program& p,Environment& e,f::Domain d=domain(),unsigned chunk=13){
    sim.begin(w,p,d,callbacks(e));unsigned calls=0;while(sim.result().status==f::Status::Building&&calls++<20000){sim.advance(chunk);
        check(sim.result().last_call_quanta<=std::min(chunk,f::call_quanta_limit),"advance quantum cap");}
    check(calls<20000,"finite retained simulation");return sim.result();}
static int first(const f::Result& r,f::EventKind kind){for(unsigned n=0;n<r.event_count;++n)if(r.events[n].kind==kind)return int(n);return -1;}
static void complete_cycle(){static f::Simulator sim,incremental;Environment e,e2;const auto w=world();const auto p=program();
    const auto& r=run(sim,w,p,e);if(r.status!=f::Status::Complete)std::printf("cycle status %u reason %u round %d events %u\n",unsigned(r.status),unsigned(r.reason),r.terminal.round,r.event_count);
    check(r.status==f::Status::Complete&&r.physical_complete&&r.service_tail==f::Tail::Complete,"complete physical paid lifecycle");
    check(r.bodies[0].banked==14&&r.terminal.team_score==14&&r.terminal.bodies[0].bag==0,"whole bag conserved and paid once");
    check(r.bodies[0].work_damage==100&&r.bodies[0].renewal_damage==55,"persistent initial plus renewed work only");
    check(r.bodies[0].spent_gold==30&&r.terminal.bodies[0].gold==30&&r.terminal.bodies[0].items[41]==1,"queried purchase cost and high item id");
    check(r.bodies[0].released&&r.bodies[0].recovered&&r.bodies[0].renewed,"release recovery and useful renewal are physical");
    const auto armed=first(r,f::EventKind::CaptureArmed),paid=first(r,f::EventKind::Captured),start=first(r,f::EventKind::BankStarted),bank=first(r,f::EventKind::Banked);
    check(armed>=0&&paid>=0&&r.events[paid].round-r.events[armed].round==2,"capture requires arming plus queried later phases");
    check(start>=0&&bank>=0&&r.events[start].amount==0&&r.events[start].aux==2&&r.events[bank].round==r.events[start].round+2,"N2 start credit zero and later owner START payout");
    const auto& b=run(incremental,w,p,e2,domain(),1);check(b.status==r.status&&b.event_count==r.event_count&&b.quanta==r.quanta,"one-quantum continuation equals batch");
    bool equal=b.event_count==r.event_count;for(unsigned n=0;equal&&n<r.event_count;++n)equal=b.events[n].kind==r.events[n].kind&&b.events[n].round==r.events[n].round&&b.events[n].amount==r.events[n].amount;
    check(equal,"dated event order independent of chunking");check(!f::comparable(r,b),"one-group does not price omitted displaced groups");
}
static void missing_contracts(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.group.life=f::Life::Reward;check(!sim.begin(w,p,domain(),callbacks(e))&&sim.result().reason==f::Reason::MissingObservation,"no inferred existing capture history");
    w=world();w.camps[0].actual=false;check(!sim.begin(w,p,domain(),callbacks(e)),"nominal member HP cannot seed forecast");
    w=world();w.horizon=5;const auto& short_result=run(sim,w,p,e);check(short_result.status==f::Status::Unknown&&short_result.physical_complete&&short_result.service_tail==f::Tail::Unknown,"finite H does not erase unpaid tail");
    w=world();e={};e.open=false;const auto& locked=run(sim,w,p,e);check(locked.status==f::Status::Unknown&&locked.terminal.team_score==0,"unknown future altar never unlocked free");
    e={};e.purchase_known=false;const auto& noquote=run(sim,w,p,e);check(noquote.status==f::Status::Unknown&&noquote.bodies[0].banked==14&&noquote.bodies[0].purchases==0,"paid bank does not invent investment");
    p.bodies[0].legs[unsigned(f::LegKind::Spawn)]=-1;e={};check(run(sim,w,p,e).reason==f::Reason::MissingRoute,"walking recovery leg cannot be missing");
    p=program();w=world();w.camps[0].strength=10;e={};const auto& exposure=run(sim,w,p,e);
    check(exposure.status==f::Status::Unknown&&exposure.physical_complete&&exposure.bodies[0].renewed&&exposure.terminal.bodies[0].hp<exposure.terminal.bodies[0].max_hp,"later renewed exposure cannot lose its recovery tail");
    w=world();w.camps[0].max_hp=45;e={};const auto& secondclear=run(sim,w,p,e);
    check(secondclear.status==f::Status::Unknown&&secondclear.terminal.group.life==f::Life::Reward,"renewal clear cannot erase second-generation capture obligation");
}
static void recall_and_interrupt(){static f::Simulator sim;auto w=world();auto p=program();p.bodies[0].use_recall=true;w.bodies[0].hp=500;
    Environment e;const auto& r=run(sim,w,p,e);check(r.status==f::Status::Complete,"conditional Recall lifecycle closes");
    const auto start=first(r,f::EventKind::RecallStarted),land=first(r,f::EventKind::Arrived),heal=first(r,f::EventKind::SpawnHeal);
    check(start>=0&&land>=0&&r.events[land].round-r.events[start].round==2,"Recall counts real owner ENDs then START landing");
    check(heal>=0&&r.events[heal].round==r.events[land].round,"landing START heals only at owner END");
    e={};e.arrival_known=false;check(run(sim,w,p,e).reason==f::Reason::Arrival,"unknown landing is not free recovery");
    w.slots=2;w.even[1]=w.odd[1]={false,0,9};e={};e.damage_round=6;e.damage=1;
    const auto& interrupted=run(sim,w,p,e);check(first(interrupted,f::EventKind::RecallInterrupted)>=0,"real damage interrupts Recall");
    check(interrupted.status==f::Status::Complete,"interrupted Recall must pay a fresh restart");
}
static void primitive_mechanics(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.npcs=2;w.camps[0].hp=w.camps[0].max_hp=50;w.camps[1]=w.camps[0];w.camps[1].id=11;w.camps[1].pose={2,2};w.bodies[0].strength=100;
    p.bodies[0].legs[unsigned(f::LegKind::Capture)]=-1;const auto& splash=run(sim,w,p,e);
    check(splash.bodies[0].work_damage==100&&splash.bodies[0].raw_damage==200,"Varg splash persistent HP excludes overkill");
    w=world();p=program();w.camps[0].hp=w.camps[0].max_hp=5;w.camps[0].shield=30;w.camps[0].shield_present=true;w.camps[0].shield_ticks=5;
    p.bodies[0].legs[unsigned(f::LegKind::Capture)]=-1;e={};const auto& shield=run(sim,w,p,e);
    check(shield.bodies[0].work_damage==5&&shield.bodies[0].shield_damage==30&&shield.bodies[0].raw_damage==50,"shield absorption separate from persistent HP");
    w=world();p=program();w.npcs=2;w.bodies[0].kit=f::Kit::Vendetta;w.bodies[0].strength=100;w.bodies[0].cooldown={5,7};
    w.camps[0].hp=w.camps[0].max_hp=50;w.camps[1]=w.camps[0];w.camps[1].id=11;w.camps[1].pose={2,2};
    p.bodies[0].legs[unsigned(f::LegKind::Capture)]=-1;e={};const auto& vendetta=run(sim,w,p,e);
    check(vendetta.bodies[0].work_damage==100&&vendetta.terminal.bodies[0].cooldown[0]==0&&vendetta.terminal.bodies[0].cooldown[1]==0&&!vendetta.terminal.bodies[0].c,"kill refreshes CDs but not spent C");
    w.bodies[0].c=false;w.phase=f::Phase::ActorAction;w.horizon=0;e={};const auto& spent=run(sim,w,p,e);
    check(spent.terminal.camps[1].hp==50&&spent.bodies[0].work_damage==50&&!spent.terminal.bodies[0].c,"kill with C already spent cannot fabricate same-input reset");
}
static void ranged_composition_and_mitigation(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.phase=f::Phase::ActorAction;w.horizon=0;w.bodies[0].pose={0,1};w.bodies[0].kit=f::Kit::Willow;w.bodies[0].attack_range=3;w.bodies[0].reset_slot=1;
    w.camps[0].hp=w.camps[0].max_hp=500;p.route_count=p.leg_count=0;leg(p,0,f::LegKind::Work,{0,1},{{1,1}});
    const auto& r=run(sim,w,p,e);const auto m=first(r,f::EventKind::Move),reset=first(r,f::EventKind::Reset),hit=first(r,f::EventKind::Basic);
    check(m>=0&&reset>m&&hit>reset&&r.bodies[0].work_damage==50,"ranged Move Reset Basic uses restored A and one C");
    w.bodies[0].c=false;e={};const auto& spent=run(sim,w,p,e);check(spent.bodies[0].work_damage==0&&first(spent,f::EventKind::Reset)<0,"ranged walking alone does not restore A");
    w=world();p=program();p.bodies[0].initial=f::Stage::Done;w.horizon=0;w.slots=2;w.even[1]=w.odd[1]={false,0,9};
    w.bodies[0].taken_multiplier=.9;w.bodies[0].npc_taken_multiplier=.63;w.camps[0].strength=100;w.camps[0].awake=true;
    e={};e.damage_round=0;e.damage=100;const auto& damage=run(sim,w,p,e);
    check(damage.status==f::Status::Unknown&&damage.reason==f::Reason::MissingContract,"combined mitigation order remains unsupported");
    w.camps[0].empty_batches_known=false;e={};check(sim.begin(w,p,domain(),callbacks(e))&&!sim.world().camps[0].empty_batches_known,"unknown counter remains unknown until an actual dependency is used");
}
static void party_case(f::World& w,f::Program& p){p.id=21;w.actors=w.npcs=w.slots=3;w.horizon=25;w.group.gold=100;
    for(unsigned a=0;a<3;++a){w.bodies[a]=w.bodies[0];auto& b=w.bodies[a];b.id=a==0?3:a==1?8:2;b.pose={1,int(a)+1};
        b.kit=a==0?f::Kit::Varg:a==1?f::Kit::Willow:f::Kit::Vendetta;b.attack_range=a==1?3:1;b.reset_slot=a==1?1:0;b.gold_known=false;
        w.even[a]=w.odd[a]={true,a,-1};w.camps[a]=w.camps[0];w.camps[a].id=int(10+a);w.camps[a].pose={2,int(a)+1};w.camps[a].hp=5;w.camps[a].max_hp=1000;
        const int y=int(a)+1;leg(p,a,f::LegKind::Work,{1,y},{});leg(p,a,f::LegKind::Capture,{1,y},{{2,y}});
        leg(p,a,f::LegKind::Release,{2,y},{{3,y}});leg(p,a,f::LegKind::Altar,{3,y},{{4,y}});
        leg(p,a,f::LegKind::Spawn,{4,y},{{3,y},{2,y},{1,y},{0,y}});leg(p,a,f::LegKind::Renew,{0,y},{{1,y}});
        p.bodies[a].altar={5,2};p.bodies[a].altar_known=true;p.bodies[a].purchase_begin=a;p.bodies[a].purchase_count=1;
        p.bodies[a].post_renewal=f::PostRenewal::ConditionalHold;
        p.purchases[a]={41,1,101,30};}
    p.purchase_count=3;}
static void shared_group_three_body(){static f::Simulator sim;auto w=world();f::Program p{};party_case(w,p);Environment e;e.multi_home=true;const auto& r=run(sim,w,p,e);
    if(r.status!=f::Status::Complete)std::printf("three body status %u reason %u round %d\n",unsigned(r.status),unsigned(r.reason),r.terminal.round);
    check(r.status==f::Status::Complete,"three-body same-world full lifecycle");
    unsigned gems=0,banked=0,purchases=0;double gold=0;for(unsigned a=0;a<3;++a){gems+=r.bodies[a].captured_gems;banked+=r.bodies[a].banked;gold+=r.bodies[a].capture_gold;purchases+=r.bodies[a].purchases;}
    check(gems==14&&banked==14&&r.terminal.team_score==14&&gold==100,"shared source reward cannot be duplicated by three actors");
    check(r.bodies[2].captured_gems==6&&r.bodies[2].capture_gold==34&&r.bodies[0].captured_gems==4,"lowest actual ID receives integer remainder");
    check(purchases==3&&!r.displaced_service_comparable,"guaranteed shares fund buys but unknown residual capital remains incomparable");
}
static void timers_sleep_and_ties(){static f::Simulator sim;auto w=world();auto p=program();p.bodies[0].initial=f::Stage::Done;w.horizon=0;
    w.bodies[0].shield=30;w.bodies[0].shield_present=true;w.bodies[0].shield_ticks=0;w.camps[0].awake=true;w.camps[0].strength=20;
    Environment e;const auto& r=run(sim,w,p,e);check(r.terminal.bodies[0].hp==1000&&r.terminal.bodies[0].shield==0,"timer zero shield absorbs camp before owner START expiry");
    w=world();w.horizon=3;w.camps[0].awake=true;w.camps[0].range=0;p=program();p.bodies[0].initial=f::Stage::Done;e={};
    const auto& sleep=run(sim,w,p,e);auto asleep=first(sleep,f::EventKind::Sleep);check(asleep>=0&&sleep.events[asleep].round==1,"two genuinely empty NPC batches sleep");
    w.camps[0].damaged_since_batch=true;e={};const auto& damaged=run(sim,w,p,e);asleep=first(damaged,f::EventKind::Sleep);
    check(asleep>=0&&damaged.events[asleep].round==2,"damage resets next empty batch before sleep countdown");
    w=world();w.actors=w.slots=3;w.horizon=0;w.camps[0].awake=true;w.camps[0].strength=20;
    for(unsigned a=0;a<3;++a){w.bodies[a]=w.bodies[0];w.bodies[a].id=int(a);w.bodies[a].pose={1,int(a)};p.bodies[a].initial=f::Stage::Done;w.even[a]=w.odd[a]={true,2-a,-1};}
    w.camps[0].pose={2,1};e={};const auto& tie=run(sim,w,p,e);
    check(tie.terminal.bodies[2].hp==980&&tie.terminal.bodies[0].hp==1000&&tie.terminal.bodies[1].hp==1000,"NPC nearest tie follows actual dated order");
}
static void useful_heal_and_privacy(){static f::Simulator sim;auto w=world();auto p=program();w.actors=w.slots=2;w.horizon=0;w.camps[0].hp=w.camps[0].max_hp=500;w.camps[0].range=0;
    w.bodies[0].hp=950;p.bodies[0].initial=f::Stage::Done;w.bodies[1]=w.bodies[0];auto& willow=w.bodies[1];willow.id=4;willow.kit=f::Kit::Willow;
    willow.pose={1,2};willow.hp=1000;willow.focus=10;willow.strength=20;willow.reset_slot=1;willow.heal_slot=0;willow.heal_modifier=3;willow.heal_range=4;
    p.bodies[1].heal_for_work=true;p.bodies[1].reset_for_work=false;leg(p,1,f::LegKind::Work,{1,2},{});w.even[1]=w.odd[1]={true,1,-1};Environment e;
    const auto& support=run(sim,w,p,e);check(support.bodies[1].useful_heals==1&&support.bodies[1].useful_support_words==1&&support.bodies[1].work_damage==20,"useful fresh ally Heal then persistent camp Basic");
    check(support.terminal.bodies[0].hp==980&&support.terminal.bodies[1].cooldown[0]==7&&!support.terminal.bodies[1].c,"heal consumes one C and real cooldown");
    w=world();p=program();w.bodies[0].gold_known=false;w.bodies[0].gold=999999;w.bodies[0].capital_floor=0;e={};
    const auto& unknown=run(sim,w,p,e);check(unknown.status==f::Status::Complete&&!unknown.terminal.bodies[0].gold_known&&unknown.terminal.bodies[0].capital_floor==30,"guaranteed captured capital pays buy without inventing peer gold");
    check(!unknown.displaced_service_comparable,"unknown residual peer capital remains incomparable");
    w.bodies[0].bag_known=false;e={};check(!sim.begin(w,p,domain(),callbacks(e)),"unknown bag cannot price custody");
}
static void refutation_and_budget(){static f::Simulator sim;auto w=world();auto p=program();w.slots=2;w.even[1]=w.odd[1]={false,0,9};
    Environment e;e.damage_round=0;e.damage=2000;auto d=domain();d.service_tail_closed=false;
    const auto& dead=run(sim,w,p,e,d);check(dead.status==f::Status::Refuted&&dead.reason==f::Reason::Dead,"definite death refutes even unknown utility tail");
    w.phase=f::Phase::ActorAction;w.bodies[0].channel=f::Channel::Recall;w.bodies[0].channel_ticks=2;w.rules.recall_damage_interrupt_known=false;e={};e.damage_round=0;e.damage=2000;
    check(run(sim,w,p,e,d).status==f::Status::Refuted,"known lethal damage outranks unknown Recall interruption tail");
    w=world();e={};e.permits=0;check(sim.begin(w,p,domain(),callbacks(e)),"retained begin");sim.advance(100000);
    check(sim.result().status==f::Status::Building&&sim.result().last_call_quanta==0,"work callback stops before transition");
    e.permits=3;sim.advance(100000);check(sim.result().last_call_quanta==3&&sim.result().status==f::Status::Building,"bounded work resumes exact cursor");
    e.permits=100000;sim.advance(100000);check(sim.result().last_call_quanta<=64,"large requested work cannot run whole horizon");
    w.bodies[0].alive=false;w.bodies[0].hp=0;d.service_tail_closed=false;check(!sim.begin(w,p,d,callbacks(e))&&sim.result().status==f::Status::Refuted,"known required death is not hidden by missing tail");
}
static void bank_interrupts_and_comparison(){static f::Simulator sim,other;auto w=world();auto p=program();w.horizon=20;w.slots=2;w.even[1]=w.odd[1]={false,0,9};
    Environment e;e.stun_round=4;const auto& r=run(sim,w,p,e);unsigned starts=0,payouts=0;
    for(unsigned n=0;n<r.event_count;++n){starts+=r.events[n].kind==f::EventKind::BankStarted;payouts+=r.events[n].kind==f::EventKind::Banked;}
    check(r.status==f::Status::Complete&&starts==2&&payouts==1&&r.bodies[0].banked==14,"stun cancels tribute and requires a fresh N2 start without duplicate credit");
    e={};e.lock_round=4;const auto& lock=run(sim,w,p,e);
    check(lock.status==f::Status::Unknown&&first(lock,f::EventKind::BankInterrupted)>=0&&lock.terminal.bodies[0].bag==14&&lock.terminal.team_score==0,"known new lock cancels channel and unknown lock-removal service cannot be free");
    w=world();auto d=domain();d.other_services_represented=true;e={};const auto& a=run(sim,w,p,e,d);Environment e2;
    const auto& b=run(other,w,p,e2,d);check(f::comparable(a,b),"identical full conditional world and H can compare");
    w.bodies[0].strength+=1;e2={};const auto& changed=run(other,w,p,e2,d);
    check(changed.status==f::Status::Complete&&!f::comparable(a,changed),"same claimed revision cannot hide changed initial physical World");
}
static void fractional_native_boundaries(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.phase=f::Phase::ActorAction;w.horizon=4;w.bodies[0].strength=129.8f;w.camps[0].hp=w.camps[0].max_hp=649.0f;p.bodies[0].reset_for_work=false;
    const auto& five=run(sim,w,p,e);
    check(std::bit_cast<std::uint32_t>(five.terminal.camps[0].hp)==0x38000000u,"native649 minus five129.8f hits retains exact2^-15 HP");
    check(five.terminal.group.life==f::Life::Alive&&!five.bodies[0].clear&&first(five,f::EventKind::Clear)<0&&five.terminal.team_score==0&&five.bodies[0].captured_gems==0,"five fractional hits cannot publish clear capture or payout");
    w.horizon=5;e={};const auto& six=run(sim,w,p,e);const auto clear=first(six,f::EventKind::Clear);
    check(six.terminal.camps[0].hp==0.0f&&clear>=0&&six.events[clear].round==5,"sixth native subtraction clears");
    w.horizon=23;e={};const auto& full=run(sim,w,p,e);const auto capture=first(full,f::EventKind::Captured),bank=first(full,f::EventKind::Banked);
    check(full.status==f::Status::Complete&&capture>=0&&bank>=0&&full.events[capture].round==8&&full.events[bank].round==11&&full.terminal.team_score==14,"capture and bank dates follow six-hit clear rather than promoted-double five-hit clear");
    w=world();p=program();w.phase=f::Phase::ActorAction;w.horizon=0;w.bodies[0].strength=129.8f;w.bodies[0].camp_multiplier=1.125f;
    w.camps[0].hp=w.camps[0].max_hp=146.0250091552734375f;p.bodies[0].reset_for_work=false;e={};
    const auto& product=run(sim,w,p,e);check(product.terminal.camps[0].hp==0.0f&&product.bodies[0].clear,"f32 product must round before HP subtraction (not floatHP with double product)");
    w.camps[0].hp=w.camps[0].max_hp=1.0f;w.camps[0].shield=146.0250091552734375f;w.camps[0].shield_present=true;w.camps[0].shield_ticks=5;e={};
    const auto& exactshield=run(sim,w,p,e);check(exactshield.terminal.camps[0].hp==1.0f&&exactshield.terminal.camps[0].shield==0.0f&&!exactshield.terminal.camps[0].shield_present,"f32 product exactly depletes shield before HP");
    w.camps[0].hp=w.camps[0].max_hp=16.225006103515625f;w.camps[0].shield=129.8f;e={};
    const auto& spill=run(sim,w,p,e);check(spill.terminal.camps[0].hp==0.0f,"f32 shield spill rounds before HP subtraction");
    w=world();p=program();w.horizon=0;p.bodies[0].initial=f::Stage::Done;w.bodies[0].hp=w.bodies[0].max_hp=116.81999969482421875f;
    w.bodies[0].taken_multiplier=w.bodies[0].npc_taken_multiplier=.9f;w.camps[0].awake=true;w.camps[0].strength=129.8f;e={};
    check(run(sim,w,p,e).status==f::Status::Refuted,"single-factor f32 mitigation rounds to lethal HP exactly");
    w=world();p=program();w.actors=w.slots=2;w.horizon=0;p.bodies[0].initial=f::Stage::Done;w.bodies[0].hp=10.0f;w.bodies[0].max_hp=399.4000244140625f;
    w.bodies[1]=w.bodies[0];auto& willow=w.bodies[1];willow.id=4;willow.kit=f::Kit::Willow;willow.pose={1,2};willow.hp=willow.max_hp;
    willow.focus=129.8f;willow.heal_modifier=3.0f;willow.heal_range=4;willow.reset_slot=1;willow.heal_slot=0;
    w.camps[0].hp=w.camps[0].max_hp=500.0f;p.bodies[1].heal_for_work=true;p.bodies[1].reset_for_work=false;
    leg(p,1,f::LegKind::Work,{1,2},{});w.even[1]=w.odd[1]={true,1,-1};e={};const auto& healed=run(sim,w,p,e);const auto heal=first(healed,f::EventKind::Heal);
    check(healed.terminal.bodies[0].hp==399.4000244140625f&&heal>=0&&healed.events[heal].amount==389.4000244140625,"heal product addition and recorded persistent gain are binary32");
    w=world();p=program();w.bodies[0].strength=w.bodies[0].max_hp=w.bodies[0].hp=129.8f;e={};e.strength_growth=e.health_growth=.1f;
    const auto& growth=run(sim,w,p,e);check(growth.terminal.bodies[0].strength==129.9000091552734375f&&growth.terminal.bodies[0].max_hp==129.9000091552734375f,"queried growth updates materialize at binary32 width");
}
static void explicit_post_renewal(){static f::Simulator sim;auto w=world();auto p=program();p.bodies[0].post_renewal=f::PostRenewal::Unknown;Environment e;
    const auto& witness=run(sim,w,p,e);check(witness.status==f::Status::Unknown&&!witness.physical_complete&&witness.service_tail==f::Tail::Unknown&&
        witness.bodies[0].renewal_hit_round>=0&&witness.bodies[0].renewal_hit_round<witness.horizon,"earliest renewed hit is not a sameH owned suffix");
    w.camps[0].strength=10000.0f;e={};const auto& absent=run(sim,w,p,e);check(absent.status==f::Status::Unknown,"missing suffix is not silently converted into a lethal artificial Hold");
    p.bodies[0].post_renewal=f::PostRenewal::ConditionalHold;e={};const auto& hold=run(sim,w,p,e);
    check(hold.status==f::Status::Refuted&&hold.program==p.id,"literal declared Hold death refutes only that ProgramID");
}
static void terminal_and_external_custody(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.team_score=990;const auto& win=run(sim,w,p,e);check(win.status==f::Status::Unknown&&win.termination==f::Termination::ModeledWin&&
        win.terminal.team_score==1004&&win.bodies[0].banked==14&&!win.bodies[0].renewed,"winning bank terminates before fictional postgame return or renewal");
    w=world();w.rules.passive_gold=1;e={};check(!sim.begin(w,p,domain(),callbacks(e))&&sim.result().reason==f::Reason::MissingContract,"unsupported passive gold is not priced zero");
    w=world();w.horizon=16;w.rules.max_rounds=16;e={};check(!sim.begin(w,p,domain(),callbacks(e))&&sim.result().termination==f::Termination::RoundBoundary,"round-limit horizon leaves actual race incumbent responsible");
    w=world();w.npcs=2;w.camps[1]=w.camps[0];w.camps[1].id=11;w.camps[1].pose={2,2};w.camps[1].hp=0;
    w.externals=1;w.others[0].id=99;w.others[0].team=w.team;w.others[0].pose={2,2};w.others[0].hp=100;w.others[0].alive=w.others[0].actual=w.others[0].npc_effects_closed=true;
    e={};const auto& allied=run(sim,w,p,e);check(allied.status==f::Status::Unknown&&allied.reason==f::Reason::UnsupportedCustody&&allied.terminal.team_score==0,"external OWN ally is unknown shared custody, never hostile contestation");
}
static void per_body_recall_durations(){static f::Simulator sim;auto w=world();f::Program p{};party_case(w,p);w.rules.recall_ticks=5;
    for(unsigned a=0;a<3;++a){w.bodies[a].hp=500.0f;w.bodies[a].effective_recall_ticks=a==1?3:5;p.bodies[a].use_recall=true;}
    w.bodies[1].items[31]=1; // Home emblem, with duration supplied separately by the producer.
    Environment e;e.multi_home=true;const auto& mixed=run(sim,w,p,e);
    check(mixed.status==f::Status::Complete,"same World supports mixed plain Home plain Recall lifecycle");
    for(unsigned a=0;a<3;++a){int start=-1,land=-1,heal=-1;const auto id=w.bodies[a].id;
        for(unsigned n=0;n<mixed.event_count;++n)if(mixed.events[n].actor==id){
            if(mixed.events[n].kind==f::EventKind::RecallStarted)start=int(n);
            if(mixed.events[n].kind==f::EventKind::Arrived)land=int(n);
            if(mixed.events[n].kind==f::EventKind::SpawnHeal&&heal<0)heal=int(n);}
        const auto ticks=a==1?3u:5u;
        check(start>=0&&land>=0&&mixed.events[start].aux==ticks&&mixed.events[land].round-mixed.events[start].round==int(ticks),"each body pays its own queried owner-END recurrence");
        check(heal>land&&mixed.events[heal].round==mixed.events[land].round&&mixed.events[heal].amount==500.0,"mixed landing is START and healing follows at that body's END");}
    w.slots=4;w.even[3]=w.odd[3]={false,0,99};e={};e.multi_home=true;e.damage_round=6;e.damage=1.0f;e.damage_actor=1;
    const auto& restart=run(sim,w,p,e);unsigned home_starts=0;int last_start=-1,home_land=-1;
    for(unsigned n=0;n<restart.event_count;++n)if(restart.events[n].actor==w.bodies[1].id){
        if(restart.events[n].kind==f::EventKind::RecallStarted){++home_starts;last_start=int(n);}
        if(restart.events[n].kind==f::EventKind::Arrived)home_land=int(n);}
    check(restart.status==f::Status::Complete&&home_starts==2&&home_land>=0&&last_start>=0&&restart.events[home_land].round-restart.events[last_start].round==3,"interrupted Home Recall restarts at its own duration rather than global5");
    w.bodies[1].effective_recall_ticks=0;e={};e.multi_home=true;const auto& missing=run(sim,w,p,e);
    check(missing.status==f::Status::Unknown&&missing.reason==f::Reason::RecallDuration,"Home inventory and base rule cannot invent an unknown per-body duration");
    bool invented=false;for(unsigned n=0;n<missing.event_count;++n)invented=invented||(missing.events[n].actor==w.bodies[1].id&&missing.events[n].kind==f::EventKind::RecallStarted);
    check(!invented,"unknown duration does not start a modeled channel");
    w=world();p=program();w.bodies[0].effective_recall_ticks=0;w.rules.recall_ticks=0;e={};
    const auto& walk=run(sim,w,p,e);check(walk.status==f::Status::Complete&&first(walk,f::EventKind::RecallStarted)<0,"walking programme has no dependency on unknown Recall duration");
    w=world();p=program();w.bodies[0].effective_recall_ticks=5;w.bodies[0].items[30]=4;w.bodies[0].strength=65.0f;w.rules.recall_ticks=5;p.purchases[0].item=30;
    e={};e.home_completion=true;e.recall_update=3;e.health_growth=e.focus_growth=0.0f;const auto& bought=run(sim,w,p,e);
    check(bought.status==f::Status::Complete&&bought.terminal.bodies[0].items[30]==0&&bought.terminal.bodies[0].items[31]==1&&bought.terminal.bodies[0].effective_recall_ticks==3,"queried fifth-shard purchase updates items and effective Recall duration together");
    e={};e.home_completion=true;e.recall_update=0;e.health_growth=e.focus_growth=0.0f;const auto& unknown_after=run(sim,w,p,e);
    check(unknown_after.status==f::Status::Complete&&unknown_after.terminal.bodies[0].items[31]==1&&unknown_after.terminal.bodies[0].effective_recall_ticks==0,"Home post-purchase duration remains unknown without its own contract, while walking remains supported");
}
static f::External external(int id,f::Point pose,int team=1){f::External e;e.id=id;e.pose=pose;e.team=team;e.hp=100.0f;
    e.alive=e.actual=e.npc_effects_closed=true;return e;}
static void varg_external_splash(){static f::Simulator sim;auto w=world();auto p=program();w.externals=1;w.others[0]=external(9,{3,1});
    Environment e;const auto& inside=run(sim,w,p,e);
    check(inside.status==f::Status::Unknown&&inside.reason==f::Reason::UnsupportedEffect,"enemy Hold cannot suppress Varg passive collateral");
    check(inside.terminal.camps[0].hp==45.0f&&inside.terminal.bodies[0].a&&inside.bodies[0].work_damage==0&&inside.terminal.team_score==0,"unsupported collateral stops before MAC camp damage clear or custody changes");
    w.others[0]=external(9,{4,3});e={};const auto& outside=run(sim,w,p,e);
    check(outside.status==f::Status::Complete&&outside.bodies[0].banked==14&&outside.terminal.others[0].hp==100.0f,"enemy outside queried splash radius does not block the closed camp-only word");
    w.others[0]=external(9,{3,2},w.team);w.slots=2;w.even[1]=w.odd[1]={false,0,9};e={};
    check(run(sim,w,p,e).status==f::Status::Complete,"friendly external body in splash radius is not an enemy damage target");
    w=world();w.bodies[0].splash_radius_known=false;p=program();e={};
    check(run(sim,w,p,e).reason==f::Reason::UnsupportedEffect,"Varg Basic requires its own queried passive radius");
}
static void partial_npc_order(){static f::Simulator sim;auto w=world();auto p=program();w.horizon=0;w.bodies[0].hp=w.bodies[0].max_hp=100.0f;
    w.camps[0].awake=true;w.camps[0].strength=10.0f;w.externals=1;w.others[0]=external(9,{3,1});p.bodies[0].initial=f::Stage::Done;Environment e;
    const auto& missing=run(sim,w,p,e);check(missing.status==f::Status::Unknown&&missing.reason==f::Reason::Order&&missing.terminal.bodies[0].hp==100.0f&&missing.terminal.others[0].hp==100.0f,"nearest tie with missing external rank is Unknown before target damage");
    w.slots=2;w.even[0]=w.odd[0]={false,0,9};w.even[1]=w.odd[1]={true,0,-1};e={};const auto& first_enemy=run(sim,w,p,e);
    check(first_enemy.physical_complete&&first_enemy.terminal.others[0].hp==90.0f&&first_enemy.terminal.bodies[0].hp==100.0f,"known earlier external order wins nearest tie");
    w.even[0]=w.odd[0]={true,0,-1};w.even[1]=w.odd[1]={false,0,9};e={};const auto& first_own=run(sim,w,p,e);
    check(first_own.terminal.bodies[0].hp==90.0f&&first_own.terminal.others[0].hp==100.0f,"known earlier controlled order wins nearest tie");
    w.slots=1;w.bodies[0].pose={0,1};e={};const auto& unique=run(sim,w,p,e);
    check(unique.physical_complete&&unique.terminal.others[0].hp==90.0f&&unique.reason!=f::Reason::Order,"strict unique nearest external does not need irrelevant rank");
    w.bodies[0].pose={4,1};w.externals=2;w.others[0]=external(9,{2,3});w.others[1]=external(10,{3,1});e={};const auto& later_closer=run(sim,w,p,e);
    check(later_closer.physical_complete&&later_closer.terminal.others[1].hp==90.0f&&later_closer.terminal.others[0].hp==100.0f,"farther missing-rank tie is irrelevant to final unique closer target");
    w.others[1].id=9;e={};check(!sim.begin(w,p,domain(),callbacks(e))&&sim.result().reason==f::Reason::Order,"duplicate represented IDs cannot create an artificial rank");
    w.externals=1;w.slots=3;w.even[1]=w.odd[1]={false,0,9};w.even[2]=w.odd[2]={false,0,9};e={};
    check(!sim.begin(w,p,domain(),callbacks(e))&&sim.result().reason==f::Reason::Order,"duplicate dated order identities are rejected");
    w.slots=1;w.bodies[0].pose={0,1};w.others[0]=external(9,{3,1});w.others[0].hp=5.0f;e={};
    check(run(sim,w,p,e).reason==f::Reason::UnsupportedDeath,"unmodeled external NPC death and custody tail is not silently closed");
}
static void nonrequired_death_progress(){static f::Simulator sim;auto w=world();auto p=program();w.actors=w.slots=2;
    w.bodies[0].pose={0,1};w.bodies[1]=w.bodies[0];w.bodies[1].id=4;w.bodies[1].pose={1,1};w.bodies[1].hp=5.0f;w.bodies[1].bag=7;
    w.camps[0].awake=true;w.camps[0].strength=10.0f;p.bodies[0].initial=p.bodies[1].initial=f::Stage::Done;
    p.bodies[1].required=false;p.bodies[1].post_renewal=f::PostRenewal::ConditionalHold;w.even[1]=w.odd[1]={true,1,-1};Environment e;
    check(sim.begin(w,p,domain(),callbacks(e)),"nonrequired initially live body has a valid physical prefix");const auto status=sim.advance(64);
    check(status==f::Status::Unknown&&sim.result().reason==f::Reason::UnsupportedDeath&&sim.result().last_call_quanta<64,"dead nonrequired owner stops instead of repeated ActorStart Building");
    check(sim.result().program==p.id&&!sim.result().terminal.bodies[1].alive&&sim.result().terminal.bodies[1].bag==0&&sim.result().bodies[1].custody_lost,"unsupported death preserves exact Programme and known custody loss");
    const auto before=sim.result().quanta;sim.advance(64);check(sim.result().quanta==before&&sim.result().last_call_quanta==0,"unsupported death cannot keep spending retained quanta");
    w=world();p={};party_case(w,p);p.bodies[1].required=false;w.slots=4;w.even[3]=w.odd[3]={false,0,99};e={};e.multi_home=true;e.damage_actor=1;e.damage_round=w.horizon;e.damage=100000.0f;
    const auto& last=run(sim,w,p,e);
    check(last.status==f::Status::Unknown&&last.reason==f::Reason::UnsupportedDeath&&!last.terminal.bodies[1].alive,"death after final owner turn cannot become a completed ignored respawn tail");
}
static void unknown_sleep_dependency(){static f::Simulator sim;auto w=world();auto p=program();w.phase=f::Phase::ActorAction;w.horizon=1;
    w.camps[0].hp=w.camps[0].max_hp=500.0f;w.camps[0].strength=1.0f;w.camps[0].awake=true;w.camps[0].empty_batches_known=false;p.bodies[0].reset_for_work=false;Environment e;
    const auto& hit=run(sim,w,p,e);check(hit.physical_complete&&hit.bodies[0].work_damage==100&&hit.terminal.camps[0].empty_batches_known&&hit.terminal.bodies[0].hp==999.0f,"accepted first Basic overwrites unknown counter before next NPC phase uses it");
    w=world();p=program();w.phase=f::Phase::ActorAction;w.horizon=1;w.camps[0].hp=w.camps[0].max_hp=500.0f;w.camps[0].awake=true;w.camps[0].empty_batches_known=false;
    w.bodies[0].pose={0,1};w.bodies[0].kit=f::Kit::Willow;w.bodies[0].attack_range=3;w.bodies[0].reset_slot=1;
    p.route_count=p.leg_count=0;leg(p,0,f::LegKind::Work,{0,1},{{1,1}});e={};const auto& reset_hit=run(sim,w,p,e);
    check(reset_hit.physical_complete&&reset_hit.bodies[0].work_damage==100&&reset_hit.terminal.camps[0].empty_batches_known&&first(reset_hit,f::EventKind::Reset)>=0,"legal Move Reset Basic also resolves wake history before its first read");
    w=world();p=program();w.horizon=0;w.camps[0].awake=true;w.camps[0].empty_batches_known=false;w.camps[0].range=0;e={};
    const auto& needed=run(sim,w,p,e);check(needed.status==f::Status::Unknown&&needed.reason==f::Reason::MissingObservation&&needed.bodies[0].work_damage==0&&!needed.terminal.camps[0].empty_batches_known,"unresolved empty-counter read before a reset remains Unknown");
    w.camps[0].range=3;w.camps[0].strength=1.0f;p.bodies[0].initial=f::Stage::Done;e={};const auto& target=run(sim,w,p,e);
    check(target.physical_complete&&target.terminal.camps[0].empty_batches_known&&target.terminal.camps[0].empty_batches==0&&target.terminal.bodies[0].hp==999.0f,"known in-range target resets the old unknown counter without a blind gate bypass");
}
static void conditional_placements(){static f::Simulator sim;auto w=world();auto p=program();Environment e;
    w.externals=1;w.slots=2;w.even[1]=w.odd[1]={false,0,9};auto& hidden=w.others[0];
    hidden.id=9;hidden.team=1;hidden.pose={20,20};hidden.alive=true;hidden.conditional_placement=true;hidden.placement_revision=123;
    const auto& coherent=run(sim,w,p,e);check(coherent.status==f::Status::Complete&&!coherent.terminal.others[0].actual&&coherent.terminal.others[0].hp==0,
        "conditional distant placement invents neither hidden HP nor actual evidence");
    auto d=domain();d.conditional=false;e={};check(!sim.begin(w,p,d,callbacks(e))&&sim.result().reason==f::Reason::MissingObservation,"placement requires conditional domain");
    hidden.placement_revision=0;e={};check(!sim.begin(w,p,domain(),callbacks(e)),"unnamed placement has no source binding");
    hidden.placement_revision=123;hidden.conditional_placement=false;e={};check(!sim.begin(w,p,domain(),callbacks(e)),"assumption scalar cannot close unknown external");
    hidden.conditional_placement=true;hidden.pose={2,2};w.camps[0].strength=20;w.camps[0].awake=true;w.bodies[0].pose={9,9};w.horizon=0;e={};
    const auto& touched=run(sim,w,p,e);check(touched.status==f::Status::Unknown&&touched.reason==f::Reason::MissingContract&&sim.world().others[0].hp==0,"hypothetical NPC target needs actual stats");
    w.phase=f::Phase::ActorAction;w.bodies[0].pose={1,1};w.horizon=1;e={};const auto hp=w.camps[0].hp;
    const auto& splash=run(sim,w,p,e);check(splash.status==f::Status::Unknown&&splash.reason==f::Reason::UnsupportedEffect&&sim.world().camps[0].hp==hp,"hypothetical enemy splash is Unknown before any camp effect");
}
int main(){complete_cycle();missing_contracts();recall_and_interrupt();primitive_mechanics();ranged_composition_and_mitigation();shared_group_three_body();timers_sleep_and_ties();useful_heal_and_privacy();refutation_and_budget();bank_interrupts_and_comparison();fractional_native_boundaries();explicit_post_renewal();terminal_and_external_custody();per_body_recall_durations();varg_external_splash();partial_npc_order();nonrequired_death_progress();unknown_sleep_dependency();conditional_placements();
    std::printf("service forecast: %u checks, %u failures\n",checks,failures);return failures?1:0;}
