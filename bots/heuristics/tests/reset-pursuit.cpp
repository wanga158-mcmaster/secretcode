#include "../reset-pursuit/src/config.h"
#undef RESET_MENU_DIAGNOSTIC
#define RESET_MENU_DIAGNOSTIC 1
#include "../reset-pursuit/src/flow.h"
#include "../reset-pursuit/src/safety.h"
#include "../reset-pursuit/src/returnability.h"
#include "../reset-pursuit/src/policy.h"
#undef HEURISTICS_ENABLED
#undef HEURISTICS_TRACE
#undef HEURISTICS_DIAGNOSTIC
#include "../renewal-regret/src/policy.h"
#include <array>
#include <cassert>
#include <string>

// This mutable world is the observation/action interface used by the actual
// production executor. Every basic mutates HP, availability and live cooldown.
struct World {
    struct Target {float hp;bool visible=true,in_range=true;};
    std::array<Target,3> targets{{{20},{30},{30}}};
    float damage=40;
    int remaining_cooldown=5;
    bool attack=true,cast=true,fuel=true,reset_succeeds=true;
    bool stop_after_basic=false,hide_after_reset=false;
    unsigned basic_count=0;
    std::string events;

    bool enough_fuel()const{return fuel&&(!stop_after_basic||basic_count==0);}
    bool can_attack()const{return attack;}
    bool can_cast()const{return cast;}
    bool legal(int id)const{
        return id>=0&&id<int(targets.size())&&targets[id].visible
            &&targets[id].in_range&&targets[id].hp>0;
    }
    bool alive(int id)const{return targets[id].hp>0;}
    int choose(int excluded)const{
        for(int id=0;id<int(targets.size());++id)if(id!=excluded&&legal(id))return id;
        return -1;
    }
    int cooldown()const{return remaining_cooldown;}
    bool reset_ready()const{return remaining_cooldown==0;}
    bool basic(int id){
        assert(attack&&legal(id));
        events+=char('A'+id);attack=false;++basic_count;
        targets[id].hp-=damage;
        if(!alive(id))remaining_cooldown=0;
        return true;
    }
    bool reset(){
        events+='R';assert(cast&&remaining_cooldown==0);
        if(!reset_succeeds)return false;
        remaining_cooldown=8;attack=true;cast=false;
        if(hide_after_reset)targets[1].visible=false;
        return true;
    }
};

static void flow_cases(){
    using h12_reset_pursuit::execute;
    World w;
    auto r=execute(w);
    assert(w.events=="ARB"&&w.targets[2].hp==30);
    assert(r.basics==2&&r.casts==1&&r.attempts==3&&r.refreshes==2);
    assert(r.first==0&&r.second==1&&r.different_hits==1&&r.first_refresh);
    assert(!w.cast&&!w.attack&&w.remaining_cooldown==0);

    // Ready at entry is not a kill refresh; B's post-cast death still is one.
    w={};w.remaining_cooldown=0;r=execute(w);
    assert(w.events=="ARB"&&r.refreshes==1&&r.casts==1&&!r.first_refresh);
    // A refreshed cooldown cannot restore the already-spent cast allowance.
    w={};w.cast=false;r=execute(w);
    assert(w.events=="A"&&r.refreshes==1&&r.casts==0&&r.attempts==1);
    // Surviving A gives no refresh, so a cooling reset cannot be used.
    w={};w.targets[0].hp=80;r=execute(w);
    assert(w.events=="A"&&w.targets[0].hp==40&&r.refreshes==0&&r.casts==0);
    // A ready reset can still finish a live target as an ordinary double hit.
    w={};w.targets[0].hp=80;w.remaining_cooldown=0;r=execute(w);
    assert(w.events=="ARA"&&r.basics==2&&r.different_hits==0&&r.refreshes==1);

    // No reset is wasted if all other candidates are hidden, out of range or
    // dead. C is also unavailable so each refusal reaches an empty live menu.
    for(int reason=0;reason<3;++reason){
        w={};w.targets[2].visible=false;
        if(reason==0)w.targets[1].visible=false;
        if(reason==1)w.targets[1].in_range=false;
        if(reason==2)w.targets[1].hp=0;
        r=execute(w);
        assert(w.events=="A"&&r.casts==0&&r.attempts==1&&r.second==-1);
    }
    w={};w.reset_succeeds=false;r=execute(w);
    assert(w.events=="AR"&&r.basics==1&&r.casts==0&&r.attempts==2);
    assert(w.targets[1].hp==30);
    // Revalidate the selected B after reset instead of trusting a stale menu.
    w={};w.hide_after_reset=true;r=execute(w);
    assert(w.events=="AR"&&r.casts==1&&r.basics==1&&r.attempts==2);
    w={};w.fuel=false;r=execute(w);
    assert(w.events.empty()&&r.attempts==0);
    w={};w.stop_after_basic=true;r=execute(w);
    assert(w.events=="A"&&r.casts==0&&r.basics==1);
    w={};w.attack=false;r=execute(w);
    assert(w.events.empty()&&r.attempts==0);
}

static heuristics::Job job(int id,int travel,int gems,float work=1){
    heuristics::Job j;
    j.id=id;j.travel=travel;j.gems=gems;j.work=work;
    j.original_hp=j.remaining_hp=work*40;j.known=true;j.affordable=true;
    return j;
}

static void policy_cases(){
    using namespace heuristics;
    using Policy=h12_reset_pursuit::Policy;
    State s;s.monster=s.jungle=5;s.mature=true;
    std::array jobs{job(0,1,4),job(1,3,30)};
    Policy p;h12_renewal_regret::Policy predecessor;
    // Executable nearby completion beats the greater distant throughput. The
    // predecessor makes the other real production choice on this same menu.
    assert(choose(p,s,jobs)==0);
    assert(choose(predecessor,s,jobs)==1);

    // Near the horizon, an executable kill whose gems cannot be banked loses
    // to a slower job whose reward reaches an altar before the final round.
    p={};s.round=1980;jobs[0].altar=100;jobs[1].altar=1;
    assert(!Policy::model(s,jobs[0]).cashable&&Policy::model(s,jobs[1]).cashable);
    assert(choose(p,s,jobs)==1);
    // With no cashable job, preserve useful work rather than a blanket refusal.
    p={};s.round=1999;assert(choose(p,s,jobs)==0);
    assert(!Policy::model(s,jobs[1]).cashable);
    auto edge=job(2,2,10,2);edge.altar=1;
    s.round=1987;assert(Policy::model(s,edge).cash==13&&Policy::model(s,edge).cashable);
    s.round=1988;assert(!Policy::model(s,edge).cashable);

    s={};s.monster=s.jungle=5;s.mature=true;jobs={job(0,1,4),job(1,3,30)};
    // Admission and invalidation apply even to a recently selected quick kill.
    p={};assert(choose(p,s,jobs)==0);
    jobs[0].affordable=false;s.round=1;assert(choose(p,s,jobs)==1);
    // Partial work is available at full health, or as a previously committed
    // job at lower health; an unsuccessful full preview preserves that option.
    p={};jobs[1].affordable=false;s.hp=1000;assert(choose(p,s,jobs)==0);
    s.hp=700;jobs[0].committed=true;assert(choose(p,s,jobs)==0);
    jobs[0].committed=false;assert(choose(p,s,jobs)==-1);

    // Replay-derived travel-cycle regression: small local travel/payout changes
    // cannot reverse a useful route each turn. Repeated old minima are not new
    // progress; after a capture-time window the stale commitment expires.
    s={};s.monster=s.jungle=5;s.mature=true;
    p={};jobs={job(0,5,10),job(1,6,10)};
    assert(choose(p,s,jobs)==0);
    jobs[0].travel=6;jobs[1].travel=5;s.round=1;
    assert(choose(p,s,jobs)==0);
    jobs[0].travel=5;s.round=3;assert(choose(p,s,jobs)==0);
    s.round=5;assert(choose(p,s,jobs)==0);
    // At equal cost ID0 remains selected; make ID1 strictly cheaper at expiry.
    jobs[1].travel=4;assert(choose(p,s,jobs)==1);

    // Banking needs no personal equipment-funding phase or large travel batch.
    s={};s.altar=15;s.bag=10;assert(p.bank(s));
    predecessor={};assert(!predecessor.bank(s));
    s.bag=4;s.hp=590;assert(p.bank(s));
    s.hp=1000;assert(!p.bank(s));
    s.altar=2;s.bag=1;assert(p.bank(s));
    s.altar=15;s.round=1999;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
    assert(p.retreat(400,1000,20,4));
    assert(!p.retreat(400,1000,20,0));
    assert(p.build(0,0)==fundamentals::BuildStage::monster_strength);
    assert(p.build(5,0)==fundamentals::BuildStage::jungle_health);
    assert(p.build(5,5)==fundamentals::BuildStage::power_strength);
    assert((Policy::roster==std::array{2,0,4,1,3}));
}


static void menu_diagnostic_cases(){
    using namespace heuristics;
    using Policy=h12_reset_pursuit::Policy;
    // The real twelve-slot producer keeps its harmless fallback, including
    // when that source is outside the nearest twelve ordinary candidates.
    std::vector<Detail> detail;
    for(int i=0;i<13;++i)detail.push_back({i,i,true,false});
    auto order=detail_order(detail,-1);
    assert(order.size()==12&&order.front()==0);
    assert(std::find(order.begin(),order.end(),12)==order.end());
    detail.push_back({13,99,true,true});
    order=detail_order(detail,-1);
    assert(order.size()==12&&order.front()==13);
    assert(std::find(order.begin(),order.end(),11)==order.end());
    assert(std::find(order.begin(),order.end(),12)==order.end());

    State s;s.round=3;s.identity=RESET_MENU_DIAGNOSTIC_ROLE;
    auto gems=job(0,3,11,3),gold=job(1,3,0,3);
    gems.gold=40;gold.gold=80;
    std::array jobs{gems,gold};
    Policy p;
    auto&d=p.menu_diagnostic;
    assert(d.begin(s.round,s.identity,2,88000,{p.current,p.progress_round,p.best_travel,p.best_hp}));
    std::array actual_detail{Detail{0,3,true,false},Detail{1,3,true,true}};
    d.capture_eligible(actual_detail);
    // Exactly the ordinary production choose call populates the diagnostic.
    const int selected=choose(p,s,jobs);
    assert(selected==0&&d.frame.selections==1);
    const auto&r=d.frame.selection[0];
    assert(!r.partial&&r.supplied==2&&r.admitted==2&&r.ranked==2);
    assert(r.returned==selected&&!r.lease_return&&!r.has_quick);
    assert(r.ranks[0].value==12&&r.ranks[1].value==1);
    struct Anchor {int x,y;};
    struct CachedGroup {Anchor anchor;};
    std::array groups{CachedGroup{{1,2}},CachedGroup{{3,4}}};
    std::array scan{0,1};std::array<int,12>stages{};stages[0]=stages[1]=9;
    d.frame.fuel_before_choose=80000;d.frame.fuel_after_choose=79000;
    d.finish(s,jobs,scan,stages,2,2,false,selected,selected,
        {p.current,p.progress_round,p.best_travel,p.best_hp},3,76000,groups);
    assert(d.frame.captured&&!d.frame.active&&!d.frame.overflow);
    assert(d.frame.job_count==2&&d.frame.scan_count==2&&d.frame.eligible==2&&d.frame.eligible_recorded==2);
    assert(d.frame.jobs[1].gold==80&&d.frame.scan[1].x==3&&d.frame.scan[1].stage==9);
    assert(!d.flush(20,100000)&&!d.flush(21,89999));
    const int lines=d.line_count();
    for(int i=0;i<lines;++i){assert(d.flush(21+i,100000));assert(d.emitted==i+1);}
    assert(d.flushed&&!d.flush(99,100000)&&!d.wants(4,s.identity));
    h12_reset_pursuit::MenuDiagnostic bounded;
    assert(bounded.begin(3,s.identity,65,88000,{-1,0,10000,INFINITY}));
    std::vector<Detail> too_many;
    for(int i=0;i<65;++i)too_many.push_back({i,i,true,false});
    bounded.capture_eligible(too_many);
    assert(bounded.frame.eligible_recorded==64&&bounded.frame.overflow);

    // Preserve the actual full/partial admission chain; the diagnostic does not
    // cause another select invocation.
    p={};s.hp=s.maximum;gold.affordable=false;std::array partial_jobs{gold};
    assert(p.menu_diagnostic.begin(3,s.identity,1,88000,{-1,0,10000,INFINITY}));
    assert(choose(p,s,partial_jobs)==1);
    assert(p.menu_diagnostic.frame.selections==2);
    assert(!p.menu_diagnostic.frame.selection[0].partial&&p.menu_diagnostic.frame.selection[0].admitted==0);
    assert(p.menu_diagnostic.frame.selection[1].partial&&p.menu_diagnostic.frame.selection[1].admitted==1);

    // Unchanged financial commitment: one next-shard value, no compulsory
    // funding phase and no reward multiplier from a single initial shard.
    gold.affordable=true;gold.gold=30;s.monster=0;s.gold=0;
    const auto thirty=Policy::model(s,gold);
    gold.gold=300;
    assert(thirty.value==Policy::model(s,gold).value&&thirty.value==1);
    s.gold=150;assert(Policy::model(s,gold).value==1);
    s.monster=5;assert(Policy::model(s,gold).value==0);
    s.monster=0;s.gold=0;gold.travel=1;gold.work=1;
    p={};std::array tier_jobs{gems,gold};
    // A truly quick Gold completion can win the existing tier, even though its
    // capped value is smaller. Do not replace this tier to satisfy a diagnosis.
    assert(choose(p,s,tier_jobs)==1);
}

static void visible_safety_cases(){
    using namespace h12_reset_pursuit;
    Policy policy;
    const auto unsafe=[&](float damage){return policy.retreat(1000,1000,damage,0);};
    std::vector<VisibleThreat> memory{VisibleThreat{{0,0},3,0,600,0}};
    bool active=true,mask=true;int clears=0;
    const auto clear_mask=[&]{mask=false;++clears;};
    assert(!begin_visible_observation(false,active,memory,clear_mask));
    assert(!active&&!mask&&memory.empty()&&clears==1);
    assert(visible_route_safe(memory,{0,0},unsafe));
    std::array free_steps{SafetyPoint{1,0}};
    assert(visible_escape({0,0},free_steps,memory,{9,0}).x==1);
    assert(!begin_visible_observation(false,active,memory,clear_mask)&&clears==1);
    assert(begin_visible_observation(true,active,memory,clear_mask)&&clears==2&&!active&&memory.empty());
    // Damage outside basic reach can still violate the current survival reserve.
    std::array threats{VisibleThreat{{0,0},2,4,100,300}};
    assert(visible_exposure(threats,{2,0})==400);
    assert(visible_exposure(threats,{4,0})==300);
    assert(visible_exposure(threats,{5,0})==0);
    assert(!visible_route_safe(threats,{2,0},unsafe));
    assert(visible_route_safe(threats,{4,0},unsafe));
    std::vector<int> radii;
    draw_visible_danger(threats,unsafe,[&](SafetyPoint p,int radius){assert(p.x==0&&p.y==0);radii.push_back(radius);});
    assert(radii==std::vector<int>{2}); // safe spell-only fringe is not blocked
    // Multiple individually tolerable opponents still veto their overlapping next step.
    std::array overlap{VisibleThreat{{0,0},2,0,200,0},VisibleThreat{{4,0},2,0,200,0}};
    assert(!visible_route_safe(overlap,{2,0},unsafe));
    assert(visible_route_safe(overlap,{0,0},unsafe));
    // Escape may go away from home to avoid a stronger opponent instead of
    // maximising the sum of distances and walking into another lethal envelope.
    std::array pursuit{VisibleThreat{{-2,0},2,0,600,0},VisibleThreat{{3,0},2,0,50,0}};
    std::array legal{SafetyPoint{-1,0},SafetyPoint{0,1},SafetyPoint{1,0}};
    const auto step=visible_escape({0,0},legal,pursuit,{-8,0});
    assert(step.x==1&&step.y==0&&visible_exposure(pursuit,step)==50);
    const auto held=visible_escape({0,0},{},pursuit,{-8,0});
    assert(held.x==0&&held.y==0);
    // Cargo preserves the policy's stronger existing reserve without changing
    // its quick-completion or reset executor.
    assert(!policy.retreat(1000,1000,320,0));
    assert(policy.retreat(1000,1000,320,4));
}
static void bank_returnability_cases(){
    using namespace h12_reset_pursuit;
    using Kind=BankCastKind;
    BankOpponent heart{{51,3},1,1,true};
    heart.casts[1]={Kind::dash,true,false,true,true,2,1};
    assert(bank_opponent_reach(heart)==4);
    std::array opponents{heart};
    std::vector<int> radii;
    // Actual paid Vesper R892: the current tile is outside the ready word;
    // actual NE step enters it. A western step remains outside it.
    assert(bank_returnability_mask({46,6},opponents,[&](BankPoint p,int radius){
        assert(p.x==51&&p.y==3);radii.push_back(radius);
    }));
    assert(radii==std::vector<int>{4});
    const auto inside=[&](BankPoint p){return std::max(std::abs(p.x-51),std::abs(p.y-3))<=radii.front();};
    assert(inside({47,5})&&!inside({45,5}));
    radii.clear();
    assert(!bank_returnability_mask({48,4},opponents,[&](BankPoint,int){radii.push_back(1);}));
    assert(radii.empty()); // do not wall an already exposed actor into place
    heart.casts[1].ready=false;assert(bank_opponent_reach(heart)==2);
    BankOpponent ranged{{0,0},1,3,false};
    assert(bank_opponent_reach(ranged)==3);
    ranged.casts[0]={Kind::reset,true,false,false,false,0,0};
    assert(bank_opponent_reach(ranged)==4); // no second walk
    ranged.casts[1]={Kind::dash,true,false,false,false,2,0};
    assert(bank_opponent_reach(ranged)==5); // dash+basic; never walk+dash+basic
    ranged.casts[1]={Kind::cc,true,true,true,true,4,0};
    assert(bank_opponent_reach(ranged)==4); // movement-exclusive cast
    BankOpponent marcella{{0,0},1,1,true};
    marcella.casts[1]={Kind::damage,true,true,true,false,4,0};
    assert(bank_opponent_reach(marcella)==4);
    marcella.casts[1].exclusive=false;assert(bank_opponent_reach(marcella)==5);
    assert(!bank_returnability_mask({0,0},std::vector<BankOpponent>{},[](BankPoint,int){assert(false);}));
}
int main(){flow_cases();policy_cases();menu_diagnostic_cases();visible_safety_cases();bank_returnability_cases();}
