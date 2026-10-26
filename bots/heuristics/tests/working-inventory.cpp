#include "../working-inventory/src/flow.h"
#include "../working-inventory/src/policy.h"
#undef HEURISTICS_ENABLED
#undef HEURISTICS_TRACE
#undef HEURISTICS_DIAGNOSTIC
#include "../splash-harvester/src/policy.h"
#include <cassert>
#include <string>
#include <utility>

namespace stock=h12_working_inventory;

// Mutable action adapter: the production flow sees actual post-action HP and
// allowances. The forecast may be wrong; it does not mutate the live target.
struct World {
    stock::Menu menu;
    bool fuel=true,allow=true,succeed=true,consume_both=false,refresh_cast=false;
    bool die_before_legal=false,stop_after_action=false;
    float actual_scale=1;
    unsigned observations=0,attempts=0,actions=0;
    std::string events;
    std::array<int,2> indexes{-1,-1};
    bool enough_fuel()const{return fuel&&(!stop_after_action||actions==0);}
    bool available(bool basic,bool cast)const{return (basic&&menu.basic)||(cast&&menu.cast);}
    stock::Menu observe(){++observations;return menu;}
    bool legal(const stock::Plan&p) {
        if(!allow||p.index<0||p.index>=int(std::min(menu.count,6u)))return false;
        auto&t=menu.targets[p.index];
        if(die_before_legal)t.hp=0;
        if(t.hp<=0)return false;
        return p.kind==stock::Kind::basic?menu.basic&&t.basic:
            menu.cast&&p.slot>=0&&p.slot<2&&t.spell[p.slot];
    }
    bool act(const stock::Plan&p) {
        ++attempts;if(!succeed)return false;
        indexes[actions]=p.index;++actions;
        const bool basic=p.kind==stock::Kind::basic;
        events+=basic?'B':'S';
        auto&t=menu.targets[p.index];
        t.hp=std::max(0.f,t.hp-(basic?t.basic_damage:t.loaded[p.slot])*actual_scale);
        if(basic)menu.basic=false;else menu.cast=false;
        if(consume_both)menu.basic=menu.cast=false;
        if(refresh_cast)menu.cast=true;
        return true;
    }
    std::pair<unsigned,unsigned>effect(const stock::Menu&before,const stock::Plan&)const {
        unsigned hits=0,kills=0;
        for(unsigned i=0;i<std::min(before.count,6u);++i)if(before.targets[i].hp>0) {
            hits+=menu.targets[i].hp<before.targets[i].hp;
            kills+=menu.targets[i].hp<=0;
        }
        return {hits,kills};
    }
};

static World loaded_world() {
    World w;w.menu.count=2;w.menu.basic=w.menu.cast=true;
    auto&t=w.menu.targets[0];t.hp=165;t.basic_damage=40;t.basic=true;
    t.loaded={180,120};t.empty={150,100};t.spell={true,true};
    auto&other=w.menu.targets[1];other.hp=80;other.basic_damage=40;other.basic=true;
    return w;
}

static void flow_cases() {
    auto w=loaded_world();const auto p=stock::choose(w.menu);
    assert(p.kind==stock::Kind::spell&&p.index==0&&p.slot==0&&p.stock&&p.kill_threshold);
    assert(stock::stock_kill(165,180,150));
    assert(!stock::stock_kill(180,180,150)); // equality has no safety margin
    assert(!stock::stock_kill(150,180,150)); // empty inventory already kills
    assert(!stock::stock_kill(0,180,150));
    const auto r=stock::execute(w);
    assert(w.events=="SB"&&w.indexes[0]==0&&w.indexes[1]==1);
    assert(r.actions==2&&r.casts==1&&r.basics==1&&r.hits==2&&r.kills==1);
    assert(w.menu.targets[0].hp==0&&w.menu.targets[1].hp==40&&w.observations==2);

    // A normal last hit takes priority over spending a loaded spell. A useful
    // stock threshold can also save a discrete basic without forecasting death.
    w=loaded_world();w.menu.targets[1].hp=20;
    assert(stock::choose(w.menu).kind==stock::Kind::basic);
    auto t=loaded_world().menu.targets[0];t.hp=220;t.loaded[0]=180;t.empty[0]=150;
    assert(!stock::stock_kill(t.hp,t.loaded[0],t.empty[0])&&stock::stock_useful(t,0));
    t.hp=215;t.empty[0]=175;assert(!stock::stock_useful(t,0));
    t.empty[0]=150;t.spell[0]=false;assert(!stock::stock_useful(t,0));

    // Both spells and basics receive the same actual Monster-emblem camp
    // bonus. Its discrete finishing threshold changes the inventory option.
    t={};t.hp=285;t.loaded[0]=180;t.empty[0]=170;t.spell[0]=true;
    t.basic_damage=stock::camp_damage(100,0);
    assert(stock::remaining_attacks(t.hp,t.loaded[0],t.basic_damage)==2);
    assert(stock::remaining_attacks(t.hp,t.empty[0],t.basic_damage)==2);
    assert(!stock::stock_useful(t,0));
    t.basic_damage=stock::camp_damage(100,.1f);
    assert(std::abs(t.basic_damage-110)<.001f);
    assert(stock::remaining_attacks(t.hp,t.loaded[0],t.basic_damage)==1);
    assert(stock::remaining_attacks(t.hp,t.empty[0],t.basic_damage)==2);
    assert(stock::stock_useful(t,0));
    stock::Menu last_hit;last_hit.count=1;last_hit.basic=last_hit.cast=true;
    last_hit.targets[0]=t;last_hit.targets[0].hp=105;last_hit.targets[0].basic=true;
    assert(stock::choose(last_hit).kind==stock::Kind::basic);
    last_hit.targets[0].basic_damage=stock::camp_damage(100,0);
    assert(stock::choose(last_hit).kind==stock::Kind::spell);

    // Fresh observations, not the kill forecast, govern the second action.
    w=loaded_world();w.menu.count=1;w.actual_scale=.5f;
    const auto reduced=stock::execute(w);
    assert(w.events=="SB"&&reduced.kills==0&&w.menu.targets[0].hp==55);
    w=loaded_world();w.consume_both=true;
    assert(stock::execute(w).actions==1&&w.events=="S"&&w.observations==1);
    w=loaded_world();w.refresh_cast=true;
    const auto refreshed=stock::execute(w);
    assert(refreshed.actions==2&&refreshed.casts==1&&refreshed.basics==1&&w.events=="SB");
    w=loaded_world();w.die_before_legal=true;
    assert(stock::execute(w).actions==0&&w.attempts==0);
    w=loaded_world();w.allow=false;assert(stock::execute(w).actions==0&&w.attempts==0);
    w=loaded_world();w.succeed=false;assert(stock::execute(w).actions==0&&w.attempts==1);
    w=loaded_world();w.fuel=false;assert(stock::execute(w).actions==0&&w.observations==0);
    w=loaded_world();w.menu.basic=w.menu.cast=false;
    assert(stock::execute(w).actions==0&&w.observations==0);
    w=loaded_world();w.stop_after_action=true;
    assert(stock::execute(w).actions==1&&w.observations==1);

    // A malformed larger count still cannot escape the six-target, two-slot,
    // two-transition production bounds or spend an extra action allowance.
    w=loaded_world();w.menu.count=100;
    for(auto&victim:w.menu.targets){victim.hp=300;victim.basic_damage=40;victim.basic=true;
        victim.loaded={180,120};victim.empty={150,100};victim.spell={true,true};}
    const auto bounded=stock::execute(w);
    assert(bounded.actions==2&&bounded.casts==1&&bounded.basics==1&&w.observations==2);
}

static stock::StockObservation option(int round=1,int bag=12) {
    stock::StockObservation o;o.round=round;o.bag=bag;o.prism=true;o.useful=o.cashable=true;return o;
}

static void phase_cases() {
    stock::Inventory inventory;auto o=option(0,0);
    assert(inventory.update(o)==stock::Phase::replenish);
    // Simulated capture creates a working balance. The same production menu
    // establishes a kill threshold while its empty-stock forecast cannot kill.
    int bag=12;float focus=120,empty_focus=100;
    auto w=loaded_world();o=option(1,bag);o.useful=stock::stock_useful(w.menu.targets[0],0);
    inventory.source=3;assert(inventory.update(o)==stock::Phase::work);
    assert(stock::execute(w).kills==1&&bag==12&&focus==120);
    inventory.note_progress(1);
    // Last monster is now dead. Pending capture protects the cheap safe payout
    // despite the vanished combat threshold, rather than reflexively banking.
    o=option(2,bag);o.useful=false;o.pending_capture=true;
    assert(inventory.update(o)==stock::Phase::work);
    inventory.note_progress(4);bag+=8;focus+=12;
    o=option(5,bag);assert(inventory.update(o)==stock::Phase::work);
    o.round=6;o.useful=false;
    assert(inventory.update(o)==stock::Phase::liquidate);
    o.round=7;o.useful=true;assert(inventory.update(o)==stock::Phase::liquidate); // committed journey
    // Simulated tribute completion reduces actual carried stock and Focus. The
    // production state invalidates the old source; replenishment must reobserve.
    const float loaded_focus=focus;bag=0;focus=empty_focus;
    o=option(9,bag);assert(inventory.update(o)==stock::Phase::replenish);
    assert(focus<loaded_focus&&inventory.source==-1);
    auto cheap=loaded_world();cheap.menu.targets[0].loaded=cheap.menu.targets[0].empty;
    const auto after_bank=stock::choose(cheap.menu);
    assert(!after_bank.stock&&!after_bank.kill_threshold);
    assert(stock::execute(cheap).actions>0); // cheaper fallback still works
    bag=8;o=option(10,bag);assert(inventory.update(o)==stock::Phase::work);

    auto refuse=[&](auto change){stock::Inventory x;auto v=option();change(v);
        assert(x.update(v)==stock::Phase::liquidate);};
    refuse([](auto&v){v.forced=true;v.pending_capture=true;}); // winning/deadline/risk override
    refuse([](auto&v){v.useful=false;});
    refuse([](auto&v){v.valid=false;});
    refuse([](auto&v){v.cashable=false;});
    refuse([](auto&v){v.prism=false;});
    refuse([](auto&v){v.bag=24;});
    stock::Inventory no_progress;o=option(0);
    assert(no_progress.update(o)==stock::Phase::work);o.round=6;
    assert(no_progress.update(o)==stock::Phase::liquidate);
    stock::Inventory hard_stop;o=option(0);assert(hard_stop.update(o)==stock::Phase::work);
    for(int round=1;round<12;++round){hard_stop.note_progress(round);o.round=round;
        assert(hard_stop.update(o)==stock::Phase::work);}
    hard_stop.note_progress(12);o.round=12;
    assert(hard_stop.update(o)==stock::Phase::liquidate);
    assert(stock::execution_lease(4,0,5,true));
    assert(!stock::execution_lease(5,0,5,true));
    assert(!stock::execution_lease(1,0,5,false));
}

static void cargo_cases() {
    using R=stock::Receipt;
    stock::Cargo cargo;
    assert(cargo.observe(0,100,true,false)==R::none);
    assert(cargo.observe(8,112,true,false)==R::gain);
    assert(cargo.bag==8&&cargo.focus==112&&!cargo.pending_submit);
    // One-tick tribute can finish before any subsequent entry observes the
    // channel. Recording the actual start is what distinguishes this bank.
    cargo.started_submit(true);
    assert(cargo.pending_submit);
    assert(cargo.observe(0,100,true,false)==R::bank);
    assert(cargo.bag==0&&cargo.focus==100&&!cargo.pending_submit);
    assert(cargo.observe(0,108,true,false)==R::none); // equipment alone is no receipt

    assert(cargo.observe(8,120,true,false)==R::gain);
    cargo.started_submit(true);
    assert(cargo.observe(8,120,true,true)==R::none&&cargo.pending_submit);
    // Interrupted channel with the same bag clears attribution immediately.
    assert(cargo.observe(8,120,true,false)==R::none&&!cargo.pending_submit);
    assert(cargo.observe(0,108,false,false)==R::loss);

    stock::Cargo dying;
    assert(dying.observe(12,118,true,false)==R::none);
    dying.started_submit(true);
    assert(dying.observe(0,100,false,false)==R::loss&&!dying.pending_submit);
    stock::Cargo rejected;
    rejected.observe(8,112,true,false);rejected.started_submit(false);
    assert(rejected.observe(0,100,true,false)==R::loss);
    stock::Cargo partial;
    partial.observe(12,118,true,false);partial.started_submit(true);
    assert(partial.observe(4,106,true,false)==R::loss);
    assert(partial.bag==4&&partial.focus==106&&!partial.pending_submit);
}

static heuristics::Job job(int id,int travel,int gems) {
    heuristics::Job j;j.id=id;j.travel=travel;j.gems=gems;j.work=1;
    j.remaining_hp=j.original_hp=100;j.known=true;j.affordable=true;return j;
}

static void policy_cases() {
    using namespace heuristics;using Policy=stock::Policy;
    State s;s.identity=6;s.mature=true;s.monster=s.jungle=5;s.altar=10;
    std::array jobs{job(0,1,20),job(1,2,8)};
    Policy p;h12_splash_harvester::Policy previous;
    // Finance queue limits excess personal inventory after meeting the batch.
    // Previous04's payback screen instead selects the larger nearby source.
    assert(choose(p,s,jobs)==1&&choose(previous,s,jobs)==0);
    p={};p.inventory.phase=stock::Phase::work;p.inventory.source=0;
    assert(choose(p,s,jobs)==0);
    jobs[0].affordable=false;assert(choose(p,s,jobs)==1);
    jobs[1].affordable=false;s.hp=500;assert(choose(p,s,jobs)==-1);
    jobs[1].committed=true;assert(choose(p,s,jobs)==1);
    p={};s={};s.identity=6;s.bag=12;s.altar=10;
    p.inventory.phase=stock::Phase::work;assert(!p.bank(s));
    p.inventory.phase=stock::Phase::liquidate;assert(p.bank(s));
    p.inventory.phase=stock::Phase::work;p.forced_bank=true;assert(p.bank(s));
    p.forced_bank=false;s.round=1999;assert(p.bank(s));
    s.round=0;s.bag=0;assert(!p.bank(s));
    s.identity=7;s.bag=6;assert(p.bank(s));
    s.bag=3;assert(!p.bank(s));s.altar=2;assert(p.bank(s));
    s.altar=10;s.hp=590;assert(p.bank(s));
    assert(p.retreat(420,1000,20,12));assert(!p.retreat(420,1000,20,0));
    using P=Purchase;
    assert(Policy::purchase(6,0,0)==P::monster_focus);
    assert(Policy::purchase(6,5,0)==P::jungle_health);
    assert(Policy::purchase(6,5,5)==P::power_focus);
    assert(Policy::purchase(8,5,0)==P::jungle_focus);
    assert(Policy::purchase(7,0,0)==P::monster_strength);
    assert(Policy::purchase(7,5,0)==P::jungle_strength);
    assert(Policy::purchase(7,5,5)==P::power_health);
    assert(Policy::purchase(1,5,0)==P::jungle_health);
    assert((Policy::roster==std::array{5,4,3,0,7}));
}

int main(){flow_cases();phase_cases();cargo_cases();policy_cases();}
