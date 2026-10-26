#define WB_SERVICE_FORECAST_NATIVE_HOST_FREE
#define WB_ENCOUNTER_NATIVE_HOST_FREE
#define WB_SERVICE_NATIVE_HOST_FREE
#include "../src/capital_service_forecast_native.h"
#include <cstdio>
#include <initializer_list>

namespace f=wb::capital::service_forecast;
namespace n=wb::capital::service_forecast_native;
static unsigned checks=0,failures=0;
static void check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
struct Work {unsigned permits{100000},paid{};};
static bool permit(void* pointer,unsigned units){auto& w=*static_cast<Work*>(pointer);if(w.permits<units)return false;w.permits-=units;w.paid+=units;return true;}
static f::Domain domain(){return {9,11,42,13,77,true,true,true,true,true,false};}
static void mark(const n::ModelContext& c,std::vector<std::uint8_t>& mask,f::Point p){const auto at=unsigned(p.y*c.width+p.x);mask[at/8]|=std::uint8_t(1u<<(at%8));}
static void leg(f::Program& p,unsigned actor,f::LegKind kind,f::Point origin,std::initializer_list<f::Point> points){
    p.bodies[actor].legs[unsigned(kind)]=int(p.leg_count);auto& l=p.legs[p.leg_count++];l.origin=origin;l.begin=p.route_count;l.count=unsigned(points.size());l.complete=true;
    for(auto point:points)p.route[p.route_count++]={point,0,1000};
}
static f::World world(){f::World w;w.round=0;w.horizon=16;w.team=0;w.revision=9;w.actors=w.npcs=w.slots=1;
    auto& b=w.bodies[0];b.id=3;b.kit=f::Kit::Varg;b.pose={1,1};b.hp=600;b.max_hp=1000;b.strength=50;b.focus=100;
    b.actual=b.bag_known=b.gold_known=true;b.shield_modifier=2.5f;b.shield_duration=5;b.heal_modifier=3;b.heal_range=4;
    b.splash_radius=1;b.splash_radius_known=true;b.effective_recall_ticks=5;
    auto& c=w.camps[0];c.id=10;c.pose={2,1};c.hp=45;c.max_hp=500;c.range=3;c.actual=true;c.empty_batches_known=true;
    w.group.id=4;w.group.life=f::Life::Alive;w.group.roster_complete=true;w.group.gold=60;w.group.gems=14;
    w.rules={true,2,2,10,8,8,8,5,0.1,1,true,true,2000,1000,0};w.even[0]=w.odd[0]={true,0,-1};return w;
}
static f::Program program(){f::Program p{};p.id=17;p.bodies[0].post_renewal=f::PostRenewal::ConditionalHold;
    leg(p,0,f::LegKind::Work,{1,1},{});leg(p,0,f::LegKind::Capture,{1,1},{{2,1}});
    leg(p,0,f::LegKind::Release,{2,1},{{3,1}});leg(p,0,f::LegKind::Altar,{3,1},{{4,1}});
    leg(p,0,f::LegKind::Spawn,{4,1},{{3,1},{2,2},{1,2},{0,1}});leg(p,0,f::LegKind::Renew,{0,1},{{1,1}});
    p.bodies[0].altar={5,1};p.bodies[0].altar_known=true;p.bodies[0].purchase_count=1;
    p.purchase_count=1;p.purchases[0]={41,1,101,30};return p;
}
static n::ModelContext context(Work& work,const f::World& w){n::ModelContext c;c.scope=n::Scope::ClosedConditionalModel;
    c.initial_world=w.revision;c.static_map=21;c.reply=42;c.assumptions=77;c.width=12;c.height=5;c.first_round=w.round;c.horizon=w.horizon;c.submit_range=1;
    const auto bytes=unsigned((c.width*c.height+7)/8);c.terrain.assign(bytes,0);c.spawn.assign(bytes,0);c.altar_tiles.assign(bytes,0);c.foreign_npc_exposure.assign(bytes,0);
    mark(c,c.spawn,{0,1});mark(c,c.altar_tiles,{5,1});c.altars.push_back({{5,1},true,true});
    n::VisionField v;v.actor=w.bodies[0].id;v.pose=w.bodies[0].pose;v.kit=w.bodies[0].kit;v.body_vision.assign(bytes,255);
    if(const unsigned tail=unsigned(c.width*c.height)%8)v.body_vision.back()=std::uint8_t((1u<<tail)-1);
    v.sdk_design_projection=true;c.views.push_back(v);
    c.foreign_source_mask_complete=c.all_relevant_occupants_represented=c.fixed_external_hold=c.no_pending_external_effects=c.no_new_altar_locks=true;
    c.work_context=&work;c.work_permit=&permit;n::PurchaseTransition q;q.actor=w.bodies[0].id;q.intent={41,1,101,30};q.before=w.bodies[0].items;
    q.effect.complete=true;q.effect.revision=101;q.effect.cost=30;q.effect.health_growth=100;q.effect.strength_growth=5;q.effect.focus_growth=2;
    q.effect.items=q.before;++q.effect.items[41];q.effect.effective_recall_ticks=5;c.purchases.push_back(q);return c;
}
static const f::Result& run(f::Simulator& sim,const f::World& w,const f::Program& p,n::ModelContext& c,unsigned chunk=13){
    sim.begin(w,p,domain(),n::callbacks(c));unsigned calls=0;while(sim.result().status==f::Status::Building&&calls++<20000){sim.advance(chunk);
        check(sim.result().last_call_quanta<=std::min(chunk,f::call_quanta_limit),"advance pays only bounded atomic work");}
    check(calls<20000,"bounded fixture continuation terminates");return sim.result();
}
static int first(const f::Result& r,f::EventKind kind){for(unsigned i=0;i<r.event_count;++i)if(r.events[i].kind==kind)return int(i);return -1;}
static unsigned count(const f::Result& r,f::EventKind kind){unsigned total=0;for(unsigned i=0;i<r.event_count;++i)total+=r.events[i].kind==kind;return total;}

static void complete_lifecycle(){static f::Simulator sim,incremental;const auto w=world();const auto p=program();Work a,b;auto ca=context(a,w),cb=context(b,w);
    const auto& r=run(sim,w,p,ca);if(r.status!=f::Status::Complete)std::printf("bridge cycle status %u reason %u round %d\n",unsigned(r.status),unsigned(r.reason),r.terminal.round);
    check(r.status==f::Status::Complete&&r.physical_complete&&r.service_tail==f::Tail::Complete,"native callback bridge simulates complete declared lifecycle");
    check(r.forecast&&r.domain.conditional&&!r.displaced_service_comparable,"component result remains conditional forecast, not observed receipt or omitted-service value");
    check(r.bodies[0].clear&&r.bodies[0].captured_gems==14&&r.bodies[0].capture_gold==60,"one physical clear and one source reward");
    check(r.bodies[0].banked==14&&r.terminal.team_score==14&&r.terminal.bodies[0].bag==0&&count(r,f::EventKind::Banked)==1,"whole original bag is paid exactly once");
    check(r.bodies[0].work_damage==100&&r.bodies[0].renewal_damage==55,"persistent camp HP and useful renewed work are separate from shield/overkill");
    check(r.bodies[0].released&&r.bodies[0].recovered&&r.bodies[0].renewed,"release, restored service and useful renewal all occur");
    check(r.bodies[0].spent_gold==30&&r.terminal.bodies[0].gold==30&&r.terminal.bodies[0].items[41]==1,"exact cached purchase spends captured capital once");
    check(r.terminal.bodies[0].hp==1100&&r.terminal.bodies[0].max_hp==1100,"spawn END healing and exact health purchase growth restore actual modeled HP");
    const auto clear=first(r,f::EventKind::Clear),arm=first(r,f::EventKind::CaptureArmed),capture=first(r,f::EventKind::Captured),release=first(r,f::EventKind::Released);
    const auto start=first(r,f::EventKind::BankStarted),bank=first(r,f::EventKind::Banked),heal=first(r,f::EventKind::SpawnHeal),buy=first(r,f::EventKind::Purchased),renew=first(r,f::EventKind::Renewed);
    check(clear>=0&&arm>clear&&capture>arm&&release>capture&&start>release&&bank>start&&heal>bank&&buy>heal&&renew>buy,"dated full lifecycle order survives callback bridge");
    check(arm>=0&&capture>=0&&r.events[capture].round-r.events[arm].round==2,"capture countdown uses queried later camp phases");
    check(start>=0&&bank>=0&&r.events[start].amount==0&&r.events[start].aux==2&&r.events[bank].round-r.events[start].round==2,"N2 start grants no score before owner START payout");
    check(a.paid==r.quanta,"each simulator transition paid through native work callback");
    const auto& s=run(incremental,w,p,cb,1);bool same=s.status==r.status&&s.event_count==r.event_count&&s.quanta==r.quanta;
    for(unsigned i=0;same&&i<r.event_count;++i)same=s.events[i].kind==r.events[i].kind&&s.events[i].round==r.events[i].round&&s.events[i].amount==r.events[i].amount&&s.events[i].actor==r.events[i].actor;
    check(same,"retained one-event calls equal bounded batches at the same dated horizon");
}
static void missing_model_facts(){static f::Simulator sim;auto w=world();auto p=program();Work paid;auto c=context(paid,w);
    c.scope=n::Scope::Unknown;const auto& absent=run(sim,w,p,c);check(absent.status==f::Status::Unknown&&absent.bodies[0].work_damage==0,"unknown scope cannot acquire forecast action authority");
    c=context(paid,w);c.spawn.pop_back();const auto& partial=run(sim,w,p,c);check(partial.status==f::Status::Unknown&&partial.bodies[0].work_damage==0,"partial world mask is unknown instead of clear");
    c=context(paid,w);c.views[0].body_vision.pop_back();const auto& vision=run(sim,w,p,c);check(vision.status==f::Status::Unknown&&vision.bodies[0].work_damage==0,"partial vision export cannot authorize a Basic");
    c=context(paid,w);c.views[0].body_vision.back()|=128;const auto& tail=run(sim,w,p,c);check(tail.status==f::Status::Unknown&&tail.bodies[0].work_damage==0,"noncanonical packed vision tail cannot authorize a Basic");
    c=context(paid,w);c.spawn.resize(unsigned(c.width*c.height),0);const auto& cells=run(sim,w,p,c);check(cells.status==f::Status::Unknown&&cells.bodies[0].work_damage==0,"cell-byte mask cannot silently enter the packed model contract");
    c=context(paid,w);c.views[0].pose={0,1};const auto& moved=run(sim,w,p,c);check(moved.status==f::Status::Unknown&&moved.bodies[0].work_damage==0,"pose-specific vision cannot be reused at another owner pose");
    c=context(paid,w);c.views[0].kit=f::Kit::Willow;const auto& kit=run(sim,w,p,c);check(kit.status==f::Status::Unknown&&kit.bodies[0].work_damage==0,"vision design projection binds queried kit");
    c=context(paid,w);c.purchases.clear();const auto& price=run(sim,w,p,c);check(price.status==f::Status::Unknown&&price.bodies[0].banked==14&&price.bodies[0].purchases==0,"actual modeled bank does not fabricate a future purchase quote");
    c=context(paid,w);c.purchases[0].before[0]=1;const auto& items=run(sim,w,p,c);check(items.status==f::Status::Unknown&&items.bodies[0].purchases==0,"purchase transition binds complete preceding inventory");
    c=context(paid,w);c.no_new_altar_locks=false;const auto& lock=run(sim,w,p,c);check(lock.status==f::Status::Unknown&&lock.bodies[0].banked==0,"missing future lock premise cannot start modeled tribute");
    c=context(paid,w);c.altars[0].observed=false;const auto& unseen=run(sim,w,p,c);check(unseen.status==f::Status::Unknown&&unseen.bodies[0].banked==0,"actual altar tile without observed lock state stays unknown");
}
static void legal_geometry(){auto w=world();Work paid;auto c=context(paid,w);const f::Primitive into_altar{f::Action::Move,{5,1}};
    check(n::model_legal(&c,w,0,into_altar)==f::Verdict::Blocked,"altar tile is physically blocked even when terrain mask omits it");
    const f::Primitive step{f::Action::Move,{3,1}};mark(c,c.foreign_npc_exposure,{3,1});
    check(n::model_legal(&c,w,0,step)==f::Verdict::Unknown,"unpriced foreign NPC exposure remains unknown rather than an invented wall");
    c=context(paid,w);c.foreign_source_mask_complete=false;check(n::model_legal(&c,w,0,step)==f::Verdict::Unknown,"partial source coverage cannot authorize Move");
    c=context(paid,w);c.all_relevant_occupants_represented=false;check(n::model_legal(&c,w,0,step)==f::Verdict::Unknown,"omitted relevant occupants cannot authorize Move");
    c=context(paid,w);check(n::model_legal(&c,w,0,{f::Action::Recall})==f::Verdict::Unknown,"walking first family invents no Recall start/landing proof");
    w.bodies[0].pose={4,1};c=context(paid,w);check(n::model_altar(&c,w,0,{5,1})==f::Verdict::Allowed,"observed unlocked altar is available only within queried range");
    w.bodies[0].pose={3,1};check(n::model_altar(&c,w,0,{5,1})==f::Verdict::Blocked,"altar use checks current modeled distance");
}
static void reply_and_work(){static f::Simulator sim;auto w=world();auto p=program();Work paid;auto c=context(paid,w);
    w.externals=1;w.others[0]={9,1,{10,4},1000,0,1,true,true,true};w.slots=2;w.even[1]=w.odd[1]={false,0,9};
    c=context(paid,w);const auto hold=n::model_reply(&c,w,9);check(hold.complete&&hold.domain==42&&!hold.count,"explicit fixed Hold is a coherent declared reply, not adaptive main");
    c.fixed_external_hold=false;check(!n::model_reply(&c,w,9).complete,"missing stationary reply commitment stays incomplete");
    c=context(paid,w);c.no_pending_external_effects=false;check(!n::model_reply(&c,w,9).complete,"Hold cannot erase existing unresolved external effects");
    c=context(paid,w);check(!n::model_reply(&c,w,99).complete,"unknown external ID cannot acquire a fabricated Hold slot");
    c=context(paid,w);w.others[0].pose=w.camps[0].pose;check(n::model_uncontested(&c,w)==f::Verdict::Blocked,"represented enemy source occupancy contests capture");
    w=world();c=context(paid,w);paid.permits=0;check(sim.begin(w,p,domain(),n::callbacks(c)),"cold begin uses supplied immutable model");sim.advance(64);
    check(sim.result().status==f::Status::Building&&!sim.result().last_call_quanta&&!sim.result().event_count,"budget denial preserves exact retained cursor without a transition");
    paid.permits=3;sim.advance(64);check(sim.result().status==f::Status::Building&&sim.result().last_call_quanta==3&&paid.paid==3,"funded work resumes only the paid prefix");
}
static void entity_namespace_collision(){static f::Simulator sim;auto w=world();auto p=program();w.actors=w.slots=2;w.horizon=0;w.phase=f::Phase::ActorAction;
    w.bodies[0].id=10;w.bodies[0].hp=950;p.bodies[0].initial=f::Stage::Done;w.camps[0].hp=w.camps[0].max_hp=500;w.camps[0].range=0;
    w.bodies[1]=w.bodies[0];auto& willow=w.bodies[1];willow.id=4;willow.pose={1,2};willow.kit=f::Kit::Willow;willow.hp=1000;willow.focus=10;willow.strength=20;
    willow.reset_slot=1;willow.heal_slot=0;willow.heal_modifier=3;willow.heal_range=4;
    p.bodies[1].heal_for_work=true;p.bodies[1].reset_for_work=false;p.bodies[1].post_renewal=f::PostRenewal::ConditionalHold;leg(p,1,f::LegKind::Work,{1,2},{});
    w.even[1]=w.odd[1]={true,1,-1};Work paid;auto c=context(paid,w);n::VisionField v;v.actor=4;v.pose={1,2};v.kit=f::Kit::Willow;v.sdk_design_projection=true;v.body_vision.assign(c.terrain.size(),0);
    mark(c,v.body_vision,w.bodies[0].pose);c.views.push_back(v);
    check(w.camps[0].id==w.bodies[0].id&&!(w.camps[0].pose==w.bodies[0].pose),"fixture uses colliding NPC and champion numeric handles at distinct positions");
    check(n::model_legal(&c,w,1,{f::Action::Heal,{},10})==f::Verdict::Allowed,"Heal legality resolves champion target namespace");
    check(n::model_champion_vision(&c,w,1,10)==f::Verdict::Allowed,"typed champion visibility uses actual recipient pose");
    check(n::model_vision(&c,w,1,10)==f::Verdict::Blocked,"Basic visibility resolves NPC target namespace rather than colliding champion");
    const auto& r=run(sim,w,p,c);check(r.bodies[1].useful_heals==1&&r.terminal.bodies[0].hp==980,"simulator Heal must preserve champion visibility through secondary callback");
    auto no_champion=n::callbacks(c);no_champion.visible_champion=nullptr;sim.begin(w,p,domain(),no_champion);
    unsigned calls=0;while(sim.result().status==f::Status::Building&&calls++<100)sim.advance(16);
    check(sim.result().status==f::Status::Unknown&&sim.result().reason==f::Reason::Vision&&sim.result().bodies[1].useful_heals==0,"missing typed champion visibility stays Unknown instead of falling back to NPC namespace");
}
int main(){complete_lifecycle();missing_model_facts();legal_geometry();reply_and_work();entity_namespace_collision();
    std::printf("capital_service_forecast_native checks %u failures %u\n",checks,failures);return failures?1:0;}
