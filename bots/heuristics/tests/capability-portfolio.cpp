#include "../capability-portfolio/src/program.h"
#include "../capability-portfolio/src/policy.h"
#include "../common/src/capture.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace portfolio=h12_capability_portfolio;

// The production Program is driven through the same scalar adapter methods as
// the runtime. These receipts establish interpreter transitions; they do not
// simulate SDK movement, capture ownership, shop legality or future healing.
struct World {
    int turn=10,cargo=0,approaches=0,returns=0,crosses=0,clears=0;
    int buys=0,refreshes=0,continuations=0,banks=0,exits=0;
    bool move_allowance=true,attack_allowance=true,cast_allowance=true;
    bool risk=false,admissible=true,entry=false,landing=false,ready=true;
    bool cross_result=true,threshold_ok=true,continuation_result=true,capacity_ok=true;
    portfolio::WorkResult result=portfolio::WorkResult::Working;
    portfolio::BuyResult purchase=portfolio::BuyResult::Bought;
    float hp=500,maximum=500;
    std::vector<bool> cross_directions;
    bool permitted()const{return portfolio::has_action(move_allowance,attack_allowance,cast_allowance);}
    int round()const{return turn;}
    bool unsafe()const{return risk;}
    bool admit()const{return admissible;}
    bool at_entry()const{return entry;}
    void approach(){++approaches;}
    bool cross_ready()const{return ready;}
    bool cross(bool reverse){++crosses;cross_directions.push_back(reverse);return cross_result;}
    bool threshold()const{return threshold_ok;}
    portfolio::WorkResult clear(){++clears;return result;}
    bool at_landing()const{return landing;}
    void return_approach(){++returns;}
    portfolio::BuyResult field_buy(){++buys;return purchase;}
    bool refresh_capacity(){++refreshes;if(capacity_ok)maximum+=50;return capacity_ok;}
    bool useful_work(){++continuations;return continuation_result;}
    int bag()const{return cargo;}
    void bank(){++banks;}
    void exit(){++exits;}
    int actions()const{return approaches+returns+crosses+clears+buys+refreshes+continuations+banks+exits;}
    void next(){++turn;}
};

void interpreter_cases() {
    using portfolio::Phase;
    using portfolio::Role;
    {
        World world;portfolio::Program program;
        program.start(Role::Ordinary,world.turn);
        assert(!program.tick(world)&&!program.active()&&world.actions()==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Access,world.turn);
        assert(program.tick(world)&&program.phase()==Phase::Approach&&world.approaches==1);
        world.entry=true;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Cross&&world.crosses==0);
        world.cross_result=false;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Cross&&world.crosses==1);
        // A hidden occupant's false cast receipt cannot advance the program.
        world.cross_result=true;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Clear&&world.crosses==2);
        assert(world.cross_directions==std::vector<bool>({false,false}));
        // The own source remains incumbent while clear/capture is incomplete.
        world.next();assert(program.tick(world)&&program.phase()==Phase::Clear&&world.clears==1);
        world.next();assert(program.tick(world)&&program.phase()==Phase::Clear&&world.clears==2);
        world.result=portfolio::WorkResult::Captured;world.cargo=9;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Return);
        world.next();assert(program.tick(world)&&world.returns==1&&program.phase()==Phase::Return);
        world.landing=true;world.ready=false;world.next();
        assert(program.tick(world)&&world.crosses==2&&program.phase()==Phase::Return);
        world.ready=true;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Bank&&world.crosses==3);
        assert(world.cross_directions.back());
        world.next();assert(program.tick(world)&&world.banks==1&&program.phase()==Phase::Bank);
        // A bank call is not itself the owner-start payout receipt.
        world.next();assert(program.tick(world)&&world.banks==2&&program.phase()==Phase::Bank);
        world.cargo=0;world.next();
        assert(!program.tick(world)&&program.phase()==Phase::Complete&&world.banks==2);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Access,world.turn);world.admissible=false;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.exits==1);
        assert(world.approaches==0&&world.crosses==0&&world.clears==0);
    }
    {
        World world;portfolio::Program program;world.entry=true;
        program.start(Role::Access,world.turn);assert(program.tick(world));
        world.ready=false;world.turn+=11;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.crosses==0&&world.exits==1);
    }
    {
        World world;portfolio::Program program;world.entry=true;world.cross_result=false;
        program.start(Role::Access,world.turn);assert(program.tick(world));
        for(int i=0;i<3;++i){world.next();assert(program.tick(world)&&program.phase()==Phase::Cross);}
        world.next();assert(program.tick(world)&&program.phase()==Phase::Refused&&world.crosses==3);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.threshold_ok=false;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.clears==0&&world.buys==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;world.cargo=12;
        assert(program.tick(world)&&program.phase()==Phase::Buy&&world.buys==0);
        world.next();assert(program.tick(world)&&program.phase()==Phase::Continue&&world.buys==1&&world.refreshes==1);
        // Capacity refresh does not authorize a second permanent purchase.
        const auto hp=world.hp;
        world.next();assert(program.tick(world)&&program.phase()==Phase::Bank&&world.continuations==1);
        assert(world.hp==hp&&world.maximum==550&&world.buys==1);
        world.next();assert(program.tick(world)&&world.banks==1);
        world.cargo=0;world.next();assert(!program.tick(world)&&program.phase()==Phase::Complete);
        assert(world.buys==1&&world.refreshes==1&&world.exits==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;
        assert(program.tick(world));world.purchase=portfolio::BuyResult::Wait;world.turn+=7;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.buys==0&&world.refreshes==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;
        assert(program.tick(world));world.purchase=portfolio::BuyResult::Bought;world.turn+=7;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.buys==0&&world.refreshes==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;
        assert(program.tick(world));world.purchase=portfolio::BuyResult::Refused;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.refreshes==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;
        assert(program.tick(world));world.capacity_ok=false;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Refused);
        assert(world.buys==1&&world.refreshes==1&&world.exits==1&&world.continuations==0);
        assert(world.hp==500&&world.maximum==500);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.risk=true;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.clears==0&&world.buys==0);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Supply,world.turn);world.result=portfolio::WorkResult::Captured;
        assert(program.tick(world));world.next();assert(program.tick(world));
        // A field Health-like maximum increase leaves actual HP unchanged.
        // A fresh risk veto after that refresh wins over promised continuation.
        assert(world.hp==500&&world.maximum==550);world.risk=true;world.next();
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.continuations==0);
    }
    for(const auto role:{Role::Access,Role::Supply}) {
        World world;portfolio::Program program;
        program.start(role,world.turn);world.move_allowance=world.attack_allowance=world.cast_allowance=false;
        const auto phase=program.phase();world.turn+=200;
        assert(program.tick(world)&&program.phase()==phase&&world.actions()==0);
    }
    // The fresh allowance veto is re-read at every phase, including the
    // purchase, return, conversion and expiry/exit branches.
    for(int stage=0;stage<7;++stage) {
        World world;portfolio::Program program;
        const bool supply=stage>=5;
        program.start(supply?Role::Supply:Role::Access,world.turn);
        world.entry=world.landing=true;
        if(!supply&&stage>=1){assert(program.tick(world));world.next();}
        if(!supply&&stage>=2){assert(program.tick(world));world.next();}
        if(!supply&&stage>=3){world.result=portfolio::WorkResult::Captured;assert(program.tick(world));world.next();}
        if(!supply&&stage>=4){assert(program.tick(world));world.next();world.cargo=9;}
        if(supply){world.result=portfolio::WorkResult::Captured;assert(program.tick(world));world.next();}
        if(stage==6){assert(program.tick(world));world.next();}
        const auto before=world.actions();const auto phase=program.phase();
        world.move_allowance=world.attack_allowance=world.cast_allowance=false;
        world.turn+=200;
        assert(program.tick(world)&&program.phase()==phase&&world.actions()==before);
    }
    {
        World world;portfolio::Program program;
        program.start(Role::Access,world.turn);world.turn+=161;
        assert(program.tick(world)&&program.phase()==Phase::Refused&&world.approaches==0);
    }
}

void guard_cases() {
    {
        portfolio::RouteWatch route;route.adopt(1,10);
        route.measured({20,20},63,10);
        for(int round=11;round<=30;++round){route.measured({20,20},73-round,round);assert(!route.stale(round,200));}
        // A successful side step or source switch does not manufacture measured
        // improvement. The global no-work history belongs to the caller.
        assert(route.stale(38,200));
        route.measured({20,20},55,39);assert(route.stale(39,200));
        route.adopt(2,40);assert(!route.stale(40,200));
        portfolio::Renewal renewal;
        assert(!renewal.due(179,179,false,false));
        assert(!renewal.due(180,180,true,false));
        assert(!renewal.due(180,180,false,true));
        assert(renewal.due(180,180,false,false));renewal.begin(180);
        assert(!renewal.due(200,200,false,false));
        assert(renewal.due(360,360,false,false));renewal.begin(360);
        assert(renewal.attempts()==2&&!renewal.due(1000,1000,false,false));
        renewal.useful_work(1000);assert(!renewal.due(1001,1,false,false));
        assert(renewal.due(1180,180,false,false));
    }
    for(int bits=0;bits<8;++bits)
        assert(portfolio::has_action(bits&1,bits&2,bits&4)==(bits!=0));
    assert(!portfolio::cast_compatible(false,false,false));
    assert(!portfolio::cast_compatible(true,true,true));
    assert(portfolio::cast_compatible(true,true,false));
    assert(portfolio::cast_compatible(true,false,true));
    assert(!portfolio::component_admitted(79999)&&portfolio::component_admitted(80000));
    assert(portfolio::component_needs_refresh(false,false));
    assert(portfolio::component_needs_refresh(true,false));
    assert(!portfolio::component_needs_refresh(true,true));
    assert(!portfolio::cancel_for_exposure(true,0));
    assert(!portfolio::cancel_for_exposure(false,100));
    assert(portfolio::cancel_for_exposure(true,1));
    assert(portfolio::quarantine_after_refusal(100)==148);
    assert(portfolio::capture_receipt(true,true,20,0,40,9));
    assert(portfolio::capture_receipt(true,true,20,0,20,9));
    assert(portfolio::capture_receipt(true,true,20,0,40,0));
    assert(!portfolio::capture_receipt(false,true,20,0,40,9));
    assert(!portfolio::capture_receipt(true,false,20,0,40,9));
    assert(!portfolio::capture_receipt(false,false,20,0,40,9));
    // Old income cannot stand in for a selected-source owner-start receipt.
    assert(!portfolio::capture_receipt(true,true,40,9,40,9));
    assert(!portfolio::capture_receipt(true,true,40,9,30,8));
    assert(!portfolio::defend_allowed(true,100,0));
    assert(!portfolio::defend_allowed(false,100,101));
    assert(portfolio::defend_allowed(false,101,101));
    heuristics::CaptureWatch capture;
    assert(!capture.expired(17,100,5));
    assert(!capture.expired(17,106,5));
    // No reset occurs merely because a personal enemy-vision list is empty.
    assert(capture.expired(17,107,5)&&capture.source()==17);
    capture.reset();assert(capture.source()==-1);
    assert(!capture.expired(17,108,5));
    portfolio::Policy policy;heuristics::State state;state.bag=0;state.altar=0;
    assert(!policy.bank(state));state.bag=1;assert(policy.bank(state));
    assert(portfolio::Policy::purchase(11,0,0)==heuristics::Purchase::monster_focus);
    assert(portfolio::Policy::purchase(1,0,0)==heuristics::Purchase::monster_strength);
    assert(portfolio::Policy::purchase(7,5,5)==heuristics::Purchase::power_health);
}

portfolio::Grid boundary_grid(bool crossing=true,bool banking=true) {
    portfolio::Grid grid{30,10,std::vector<std::uint8_t>(300,0)};
    grid.flags[grid.index({1,5})]|=portfolio::AllySpawn;
    grid.flags[grid.index({22,5})]|=portfolio::Camp;
    if(crossing)for(int y=0;y<10;++y)grid.flags[grid.index({20,y})]|=portfolio::Hole;
    if(banking)for(auto p:{portfolio::Point{3,3},portfolio::Point{22,3}})
        grid.flags[grid.index(p)]|=portfolio::Altar;
    return grid;
}

void geometry_cases() {
    using portfolio::Package;
    using portfolio::Role;
    const std::vector<portfolio::Group> sources{{97,{{22,5}},220,0,10,40}};
    const auto access=portfolio::summarize(boundary_grid(),sources,{1,5});
    const auto supply=portfolio::summarize(boundary_grid(false),sources,{1,5});
    const auto ordinary=portfolio::summarize(boundary_grid(false,false),sources,{1,5});
    assert(access.package==Package::Access&&access.roles[0]==Role::Access);
    assert(access.witness.group_id==97&&access.identities[0]==1&&access.picks[0]==3);
    assert(portfolio::separation(access.witness.entry,access.witness.landing)==2);
    assert(access.witness.pad==portfolio::Point({22,5}));
    assert(supply.package==Package::Supply&&supply.roles[1]==Role::Supply);
    assert(supply.picks[1]==0&&supply.identities[1]==11&&supply.witness.group_id==97);
    assert(ordinary.package==Package::Ordinary);
    for(const auto role:ordinary.roles)assert(role==Role::Ordinary);
    // The exact public geometry boundary changes the production interpreter's
    // first phase as well as its roster. Other workers stay independent.
    for(const auto*source:{&access,&supply,&ordinary}) {
        auto plan=*source;plan.pick_count=plan.expected_picks;
        World world;portfolio::Program vesper,tavi,marcella;
        vesper.start(portfolio::role_for(plan,1),world.turn);
        tavi.start(portfolio::role_for(plan,11),world.turn);
        marcella.start(portfolio::role_for(plan,7),world.turn);
        assert(marcella.phase()==portfolio::Phase::Ordinary);
        if(plan.package==Package::Access) {
            assert(vesper.phase()==portfolio::Phase::Approach&&tavi.phase()==portfolio::Phase::Ordinary);
            assert(vesper.tick(world)&&world.approaches==1&&world.clears==0);
        } else if(plan.package==Package::Supply) {
            assert(vesper.phase()==portfolio::Phase::Ordinary&&tavi.phase()==portfolio::Phase::Clear);
            assert(tavi.tick(world)&&world.clears==1&&world.approaches==0);
        } else assert(vesper.phase()==portfolio::Phase::Ordinary&&tavi.phase()==portfolio::Phase::Ordinary);
    }
    {
        // Reachability of one pad does not certify the rest of a living group.
        auto split=sources;split[0].pads.push_back({1,6});
        auto grid=boundary_grid();grid.flags[grid.index({1,6})]|=portfolio::Camp;
        assert(portfolio::summarize(grid,split,{1,5}).package==Package::Ordinary);
    }
    {
        // No legal non-camp landing, hence no reversible exact crossing.
        auto grid=boundary_grid();
        for(int y=0;y<10;++y)grid.flags[grid.index({21,y})]|=portfolio::Wall;
        assert(portfolio::summarize(grid,sources,{1,5}).package==Package::Ordinary);
    }
    {
        auto grid=boundary_grid();
        for(int y=0;y<10;++y)grid.flags[grid.index({21,y})]|=portfolio::EnemySpawn;
        assert(portfolio::summarize(grid,sources,{1,5}).package==Package::Ordinary);
    }
    {
        auto grid=boundary_grid();
        for(int y=0;y<10;++y)grid.flags[grid.index({20,y})]=portfolio::Wall;
        assert(portfolio::summarize(grid,sources,{1,5}).package==Package::Ordinary);
    }
    {
        auto costly=sources;costly[0].hp=5000;
        assert(portfolio::summarize(boundary_grid(),costly,{1,5}).package==Package::Ordinary);
        costly=sources;costly[0].strength=1000;
        assert(portfolio::summarize(boundary_grid(),costly,{1,5}).package==Package::Ordinary);
    }
    {
        auto cheap=sources;cheap[0].gold=29;
        assert(portfolio::summarize(boundary_grid(false),cheap,{1,5}).package==Package::Ordinary);
        cheap=sources;cheap[0].gems=3;
        assert(portfolio::summarize(boundary_grid(false),cheap,{1,5}).package==Package::Supply);
        cheap[0].gems=0;
        assert(portfolio::summarize(boundary_grid(),cheap,{1,5}).package==Package::Access);
    }
    {
        auto interrupted=[](){return false;};
        assert(portfolio::summarize(boundary_grid(),sources,{1,5},interrupted).package==Package::Ordinary);
        auto bad=boundary_grid();bad.flags.pop_back();
        assert(portfolio::summarize(bad,sources,{1,5}).package==Package::Ordinary);
        assert(portfolio::summarize(boundary_grid(),sources,{-1,5}).package==Package::Ordinary);
    }
}

void envelope_cases() {
    assert(portfolio::plan_length==140);
    const std::vector<portfolio::Group> sources{{97,{{22,5}},220,0,10,40}};
    for(bool crossing:{false,true})for(int expected:{3,4,5}) {
        auto plan=portfolio::summarize(boundary_grid(crossing),sources,{1,5});
        plan.expected_picks=expected;plan.pick_count=expected;
        const auto bytes=portfolio::encode(plan);portfolio::Plan read;
        assert(bytes.size()==1024&&portfolio::decode(bytes,read));
        assert(read.package==plan.package&&read.picks==plan.picks&&read.identities==plan.identities&&read.roles==plan.roles);
        assert(read.pick_count==expected&&read.expected_picks==expected);
        assert(read.witness.group_id==97&&read.witness.entry==plan.witness.entry&&read.witness.landing==plan.witness.landing);
        assert(read.witness.pad==plan.witness.pad&&read.witness.altar==plan.witness.altar);
        assert(read.witness.detour==plan.witness.detour&&read.witness.work==plan.witness.work);
        assert(read.witness.gems==10&&read.witness.gold==40);
        assert(portfolio::role_for(read,crossing?1:11)==(crossing?portfolio::Role::Access:portfolio::Role::Supply));
        assert(portfolio::role_for(read,7)==portfolio::Role::Ordinary);
        for(std::size_t i=portfolio::plan_length;i<bytes.size();++i)assert(bytes[i]==0);
        // Partial drafts are valid packets but cannot activate a missing role.
        for(int picked=0;picked<expected;++picked) {
            auto partial=plan;partial.pick_count=picked;portfolio::Plan restored;
            assert(portfolio::decode(portfolio::encode(partial),restored));
            for(int identity=0;identity<=12;++identity)
                assert(portfolio::role_for(restored,identity)==portfolio::Role::Ordinary);
        }
        // Neither a short packet nor changed payload/tail can inherit a role.
        assert(!portfolio::decode(std::span<const std::uint8_t>(bytes.data(),100),read));
        for(int offset:{0,4,8,12,20,35,100,140,1023}) {
            auto corrupt=bytes;corrupt[offset]^=1;
            assert(!portfolio::decode(corrupt,read));
        }
        {
            // A recomputed checksum still cannot authorize an undefined tail.
            auto stale=bytes;stale.back()=0x7f;
            const auto checksum=portfolio::plan_checksum(stale);
            for(int i=0;i<4;++i)stale[12+i]=std::uint8_t(checksum>>(8*i));
            assert(!portfolio::decode(stale,read));
        }
        auto invalid=plan;invalid.identities[0]=11;
        assert(!portfolio::decode(portfolio::encode(invalid),read));
        invalid=plan;invalid.picks[1]=invalid.picks[0];invalid.identities[1]=invalid.identities[0];
        assert(!portfolio::decode(portfolio::encode(invalid),read));
        invalid=plan;invalid.picks[4]=11;invalid.identities[4]=3;
        assert(!portfolio::decode(portfolio::encode(invalid),read));
        invalid=plan;invalid.roles[2]=portfolio::Role::Access;
        assert(!portfolio::decode(portfolio::encode(invalid),read));
        invalid=plan;invalid.expected_picks=6;
        assert(!portfolio::decode(portfolio::encode(invalid),read));
        invalid=plan;invalid.pick_count=expected+1;
        assert(!portfolio::decode(portfolio::encode(invalid),read));
    }
}

int main(){interpreter_cases();guard_cases();geometry_cases();envelope_cases();std::puts("capability portfolio: interpreter, geometry, envelope and guards passed");}
