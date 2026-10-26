#include <cassert>
#include <iostream>
#include "../catalyst-depots/src/policy.h"
#include "../envelope-draft/src/policy.h"
#include "../deferred-forks/src/policy.h"
#include "../exposure-works/src/policy.h"
#include "../initiative-works/src/policy.h"

#ifdef NDEBUG
#error Plan10 fixtures require active assertions in optimized builds.
#endif

static p10::Group source(int id,int x,int y){
    p10::Group g;g.id=id;g.position={x,y};g.approach={x,y};g.spawns={{x,y}};
    g.observed=g.complete=g.living=g.route_known=g.safe_solo=true;
    g.travel=3;g.to_bank=2;g.to_spawn=4;g.clear_turns=2;g.gems=7;g.gold=40;g.respawn=40;
    p10::Monster m;m.id=id*10;m.group=id;m.position={x,y};m.observed=m.alive=true;
    m.hp=m.max_hp=120;m.strength=0;m.range=3;m.respawn=40;g.monsters.push_back(m);g.hp=120;
    return g;
}
static p10::World world(p10::Champion type){
    p10::World w;w.round=10;w.self.id=1;w.self.pick=0;w.self.type=type;w.self.position={0,0};
    w.self.visible=w.self.alive=w.self.ally=w.self.can_move=w.self.can_attack=w.self.can_cast=true;
    w.self.hp=w.self.max_hp=900;w.self.strength=70;w.self.focus=100;w.self.range=1;
    w.self.abilities[1].dash=true;w.self.abilities[1].range=2;w.to_spawn=3;w.spawn={0,0};
    return w;
}

static void depots(){
    using namespace p10_depots;
    using Policy=p10_depots::LegacyPolicy;
    Depot d;d.group=1;d.monster=10;d.preparedRound=3;d.liquidationDeadline=30;d.state=Stock::proposed;
    StockObservation o{1,10,10,60,0,70,true,true,true,true,true,true,false};
    assert(refresh(d,o));
    ResetUse use{10,2,6,0,1,true,false,true,false,true};
    assert(consume(d,use));assert(dash_after_kill(d,use));assert(d.interveningJob==2);
    auto ready=d;ready.state=Stock::verified;use.cooldownBefore=0;assert(!consume(ready,use));
    use.cooldownBefore=6;use.confirmedKill=false;assert(!consume(ready,use));
    use.confirmedKill=true;use.castAlreadyUsed=true;assert(consume(ready,use));assert(!dash_after_kill(ready,use));
    // Killing refreshes cooldown, never the shared successful-cast allowance.
    use.castAlreadyUsed=false;use.unavoidableOrdinaryKill=true;ready.state=Stock::verified;assert(!consume(ready,use));
    o.sameLifecycle=false;assert(!refresh(d,o));assert(d.state==Stock::invalid);
    o.sameLifecycle=true;o.hp=120;o.strikerStrength=180; // fresh high-Strength one-hit kill is insufficient
    o.attackingMembersDead=false;assert(!refresh(d,o));
    o.attackingMembersDead=true;o.enemyPresent=true;assert(!refresh(d,o));
    std::array<Script,4> scripts{};
    for(int i=0;i<4;++i){auto&s=scripts[i];s.kind=Script::Kind(i);s.jobTravel=10;s.jobWork=4;s.altarTravel=2;s.jobGems=14;s.depotGems=7;s.jobGold=40;s.depotGold=20;s.sourceRespawn=70;s.complete=i<2;}
    scripts[0].jobTravel+=12;scripts[1].dashSaved=2;scripts[1].legalReset=true;
    assert(choose(scripts,true)==Script::reserved);assert(choose(scripts,false)==Script::immediate);
    scripts[1].preparation=40;assert(choose(scripts,true)==Script::immediate);
    scripts[1].preparation=0;scripts[1].exclusiveHolder=false;assert(!forecast(scripts[1]).legal);
    scripts[1].exclusiveHolder=true;scripts[1].enemyTakesStock=true;assert(!forecast(scripts[1]).legal);
    Loop loop;assert(!loop.buy(30));assert(loop.capture(7,40));assert(loop.buy(30));assert(loop.ownGold==10);
    assert(loop.prepare());assert(loop.work());assert(!loop.reset(false));assert(loop.reset(true));
    assert(loop.tribute(7));assert(loop.liquidate(true));assert(loop.renew(true));assert(loop.stage==Loop::renewed);
    auto w=world(p10::Champion::vendetta);w.groups={source(1,1,0),source(2,3,0)};
    w.self.cooldown[1]=6;w.groups[0].travel=0;w.groups[0].monsters[0].hp=50;
    Policy active,control;
    (void)active.step(w,p10::Arm::active); // only proposed stock, no immediate reset credit
    ++w.round;
    const auto chosen=active.step(w,p10::Arm::active);
    assert(chosen.task==p10::Task::attack&&chosen.monster==10&&chosen.cast_slot==1);
    assert(control.step(w,p10::Arm::control).task==p10::Task::farm);
    w.self.cooldown[1]=0;assert(active.step(w,p10::Arm::active).cast_slot!=1);
    std::cout<<"PASS 06 stock manufacture/cross-job use/liquidation; ready, theft, lifecycle, cast-limit, split negatives\n";
}

static void envelope(){
    using namespace p10_envelope;
    using Policy=p10_envelope::LegacyPolicy;
    std::array<Source,6> sources{};
    for(int i=0;i<6;++i){auto&s=sources[i];s.id=i;s.travel=2;s.altarTravel=2;s.spawnTravel=3;s.respawn=40;s.gems=7;s.gold=40;s.route=s.freeHold=s.exit=true;s.count=1;s.monsters[0]={120,0,3,i,0};}
    auto designs=[](Kit k){Design d;d.hp=600;d.strength=40;d.focus=100;d.strengthGrowth=5;d.focusGrowth=10;
        d.reset=k==Kit::vendetta||k==Kit::varg||k==Kit::marcella||k==Kit::tavi||k==Kit::issac;
        d.splash=k==Kit::varg;d.heal=k==Kit::willow;d.retaliate=k==Kit::ren;
        if(k==Kit::vendetta)d.strength=70;
        if(k==Kit::prism||k==Kit::tavi||k==Kit::cassi){d.damageModifier=2;d.range=3;}
        return d;
    };
    std::array<Envelope,4> e{};for(int p=0;p<4;++p)e[p]=evaluate(package(p,3),sources,6,designs);
    for(const auto&out:e){assert(out.feasible);assert(out.firstBank<192);assert(out.circuitsUsed>=2);assert(out.certificates<=144);}
    const int selected=select(e,true);assert(selected==1);assert(select(e,false)==0);
    std::cout<<"07 cluster motif selected="<<selected;for(const auto&out:e)std::cout<<" p"<<out.packageId<<"="<<out.secured<<"/"<<out.firstBank;std::cout<<'\n';
    // A second actual source motif changes the feasible package. The cheap
    // cluster choice above is not an actor-count or hand-edited output score.
    auto wolves=sources;
    // Remaining workers still have their own complete cheap-source fallback;
    // the richer grouped job pays only its certified lead/helper appointment.
    for(int i=0;i<2;++i){auto&s=wolves[i];s.count=2;s.gems=14;s.monsters[0]={1350,20,3,0,0};s.monsters[1]={1350,20,3,1,0};}
    auto groupedDesign=[&](Kit k){auto d=designs(k);if(k==Kit::varg){d.hp=1000;d.shieldModifier=2.5;d.shieldDuration=5;}return d;};
    std::array<Envelope,4> grouped{};for(int p=0;p<4;++p)grouped[p]=evaluate(package(p,3),wolves,6,groupedDesign);
    assert(select(grouped,true)==0);assert(grouped[0].feasible);assert(grouped[0].secured>grouped[1].secured);
    assert(package(3,3).actors==4);assert(package(3,3).duties[0].work==Work::paired);
    // A favorable circuit cannot qualify a package while silently discarding
    // its other workers. No actor-count or infeasible-extension bonus.
    auto noFallback=sources;for(int i=1;i<6;++i)noFallback[i].route=false;
    assert(!evaluate(package(1,3),noFallback,6,designs).feasible);
    // Exact next phase refuses unsafe work even if capture reward is large.
    Source danger=sources[0];danger.monsters[0]={10000,800,4,0,0};
    assert(!run(danger,designs(Kit::varg),nullptr,0,-1,0,5,.1,0,1,30).feasible);
    Source changed=sources[0];changed.freeHold=false;
    assert(!run(changed,designs(Kit::vendetta),nullptr,0,-1,0,5,.1,0,1,30).feasible);
    changed=sources[0];changed.count=9;assert(!run(changed,designs(Kit::varg),nullptr,0,-1,0,5,.1,0,1,30).feasible);
    assert(!certify(changed,designs(Kit::varg),nullptr,0,-1,0,5,.1,0,1,30).feasible);
    // Every live construction restores only bounded IDs: no geometry, design
    // catalogue, source simulation or fuel reader is needed to recognize it.
    for(int id=0;id<4;++id)for(int picks=3;picks<=5;++picks){
        const auto expected=roster(id);std::vector<p10::Champion> actual(expected.draft.begin(),expected.draft.begin()+picks);
        if(id==3)actual.push_back(p10::Champion::issac);
        Policy restored;const auto result=restored.fastrestore(actual);assert(restored.selected==id);
        assert(restored.organization.actors==picks+(id==3));assert(restored.assigned.circuitsUsed==0);
        for(int i=0;i<picks;++i)assert(std::find(result.draft.begin(),result.draft.end(),expected.draft[i])!=result.draft.end());
    }
    const std::vector<p10::Champion> failedDraft{p10::Champion::tavi,p10::Champion::heart,p10::Champion::stellar,p10::Champion::vesper,p10::Champion::vendetta};
    Policy actualOnly;const auto actualProfile=actualOnly.fastrestore(failedDraft);assert(actualOnly.selected==-1);
    for(int i=0;i<5;++i)assert(actualProfile.draft[i]==failedDraft[i]);
    auto actualWorld=world(p10::Champion::stellar);actualWorld.groups={source(4,2,0)};
    assert(actualOnly.step(actualWorld,p10::Arm::active).task==p10::Task::farm);assert(actualOnly.fresh_trajectories==0);
    p10::DraftContext draft;draft.picks=3;
    for(int i=0;i<240;++i){auto g=source(i,i+1,0);g.travel=i+1;g.approach={i,0};g.approach_pads={{i,0},{i,-1}};draft.groups.push_back(g);}
    for(int i=0;i<=int(Kit::issac);++i){const auto kit=Kit(i);const auto d=designs(kit);p10::Actor a;
        a.type=champion(kit);a.hp=a.max_hp=d.hp;a.strength=d.strength;a.focus=d.focus;a.range=d.range;
        a.abilities[0].reset=d.reset;a.abilities[0].heal=d.heal;a.abilities[1].damage=d.damageModifier>0;
        a.abilities[1].modifier=d.damageModifier;a.abilities[1].range=d.damageRange;draft.champions.push_back(a);
    }
    Policy selector;const auto draftRoster=selector.draft(draft,p10::Arm::active);assert(selector.assigned.feasible);
    assert(selector.assigned.certificates<=144&&selector.fresh_trajectories==0);
    Policy physical;physical.fastrestore({draftRoster.draft[0],draftRoster.draft[1],draftRoster.draft[2]});assert(physical.selected==selector.selected);
    Policy frozen;frozen.draft(draft,p10::Arm::control);assert(frozen.selected==0);
    RenewalTrace loop;assert(!loop.buy(30));assert(loop.capture(1,7,40));assert(loop.buy(30));assert(loop.gold==10);assert(loop.bank(7));assert(loop.renew(1));
    auto w=world(p10::Champion::varg);w.groups={source(1,2,0)};w.self.at_spawn=true;w.own_payout=true;w.own_gold=40;
    Policy policy;auto buy=policy.step(w,p10::Arm::active);assert(buy.task==p10::Task::buy);
    w.own_payout=false;w.own_purchase=true;w.self.bag=7;assert(policy.step(w,p10::Arm::active).task==p10::Task::bank);
    w.own_purchase=false;w.self.bag=0;w.own_bank_completed=true;const auto resumed=policy.step(w,p10::Arm::active);
    assert(resumed.task==p10::Task::farm||resumed.task==p10::Task::attack);assert(policy.fresh_trajectories<=2);
    Policy pendingBuy;pendingBuy.fastrestore({p10::Champion::willow,p10::Champion::varg,p10::Champion::marcella});
    w.self.at_spawn=false;w.own_payout=true;assert(pendingBuy.step(w,p10::Arm::active).task==p10::Task::recover);
    std::cout<<"PASS 07 four complete package envelopes, unavailable/changed premises, no actor-count reward, purchase/bank/renewal\n";
}

static void forks(){
    using namespace p10_forks;
    using Policy=p10_forks::LegacyPolicy;
    Fork f;f.createdRound=10;enumerate(f,10);
    for(int p=0;p<2;++p){auto&program=f.skeletons[p];program.id=p;program.next={Action{Atomic::move,1,0},Action{Atomic::move,2,0}};program.useful={true,true};program.legal={true,true};program.safe={true,true};program.capital=p;program.bank={0,14,true};
        for(int row=0;row<8;++row)f.preference[p][row]=(p==0)==f.rows[row].usable?2:1;
    }
    auto prefix=common_prefix(f);assert(prefix.certified&&prefix.count==2);
    assert(decide(f,true)==Decision::prefix);assert(decide(f,false)==Decision::inspect);
    f.skeletons[1].next[0].irreversible=true;assert(common_prefix(f).count==0);assert(decide(f,true)==Decision::inspect);
    f.skeletons[1].next[0].irreversible=false;
    f.extraDistinction=true;assert(!common_prefix(f).certified);assert(decide(f,true)==Decision::safe_fallback);
    f.extraDistinction=false;f.splitInterval=true;assert(!common_prefix(f).certified);
    f.splitInterval=false;f.rowsUsed=9;assert(!common_prefix(f).certified);
    f.rowsUsed=8;f.predicates[0]={Provenance::observed,10,false};enumerate(f,10);
    for(int i=0;i<8;++i)if(f.rows[i].usable)assert(!f.rows[i].plausible);
    f.predicates[0].round=9;enumerate(f,10);for(const auto&r:f.rows)assert(r.plausible);
    f.skeletons[0].bank={15,21,true};f.skeletons[1].bank={0,14,true};assert(decide(f,true)==Decision::commit_a);
    Trace loop;assert(loop.common(true));assert(!loop.purchase(30,false));assert(loop.capture(7,40));assert(!loop.purchase(30,false));assert(loop.purchase(30,true));assert(loop.ownGold==10);assert(loop.bank(7));assert(loop.renewal(true));
    auto w=world(p10::Champion::tavi);w.groups={source(1,5,0),source(2,6,0)};
    w.groups[0].observed=false;w.groups[0].complete=false;w.groups[0].travel=5;w.groups[1].travel=6;
    Policy active,control;assert(active.step(w,p10::Arm::active).task==p10::Task::move);
    assert(control.step(w,p10::Arm::control).task==p10::Task::farm);
    w.groups[1].approach={0,6}; // actual fork no longer shares a safe next action
    const auto changed=active.step(w,p10::Arm::active);assert(changed.task!=p10::Task::move);
    // A settled productive fork must not admit another source indefinitely
    // after its actual receipt. Both arms commit personal conversion first.
    for(auto type:{p10::Champion::tavi,p10::Champion::vendetta,p10::Champion::vesper}){
        auto loaded=world(type);loaded.groups={source(1,5,0),source(2,6,0)};
        p10::Altar near;near.id=0;near.route_known=near.visible=true;near.travel=3;near.position={4,0};
        p10::Altar far=near;far.id=1;far.travel=9;far.position={10,0};loaded.altars={near,far};
        loaded.self.bag=500;loaded.own_gold=40;loaded.own_payout=true;
        Policy activeLoaded,controlLoaded;auto a=activeLoaded.step(loaded,p10::Arm::active),c=controlLoaded.step(loaded,p10::Arm::control);
        assert(a.task==p10::Task::bank&&c.task==p10::Task::bank&&a.altar==0&&a.expected_min==loaded.round+53);
        loaded.round=loaded.max_rounds-54;loaded.own_payout=false;
        assert(activeLoaded.step(loaded,p10::Arm::active).task==p10::Task::bank);
        loaded.self.channel=p10::Channel::tribute;assert(activeLoaded.step(loaded,p10::Arm::active).task==p10::Task::wait);
        loaded.self.channel=p10::Channel::none;loaded.self.bag=0;loaded.own_bank_completed=true;loaded.score=500;loaded.round=11;
        auto renewed=activeLoaded.step(loaded,p10::Arm::active);assert(renewed.task!=p10::Task::bank&&activeLoaded.fork.createdRound==11);
        // A changed locked altar costs its real remaining basic attacks. The
        // farther unlocked route can then become the earlier conversion.
        loaded.own_bank_completed=false;loaded.self.bag=10;loaded.altars[0].enemy_lock=true;loaded.altars[0].lock_hits=5;loaded.altars[1].travel=7;
        assert(activeLoaded.step(loaded,p10::Arm::active).altar==1);
    }
    std::cout<<"PASS 08 eight-world action agreement, irreversible/extra-distinction/age guards, delayed personal capital and renewal\n";
}

static void exposure(){
    using namespace p10_exposure;
    using Policy=p10_exposure::LegacyPolicy;
    Mill m;m.count=2;m.ren={1,800,900,35,100,true,false};m.willow={2,700,700,0,120,true,true};
    m.stellar={3,750,750,0,85,false,false};m.exitLegal=m.exclusiveAggro=m.holderSafe=m.healArrival=true;
    m.gems=14;m.gold=80;m.altar=2;m.channel=2;m.returnTravel=4;m.respawn=40;
    m.attackers[0]={10,15,10,3,1,true,true,true,false};
    m.attackers[1]={11,100,10,3,1,true,true,true,false};
    const auto active=choose(m,true),control=choose(m,false);
    assert(active.legal&&control.legal);
    assert(active.kind==Template::direct_elsewhere);assert(control.kind!=Template::direct_elsewhere);
    assert(active.naturalFinishes>=1);assert(active.steps[0].directTarget==11);assert(active.bank<control.bank);
    bool actualHit=false;for(int i=0;i<active.hitCount;++i)if(active.hits[i].attacker==10&&active.hits[i].damage>0)actualHit=true;
    assert(actualHit);
    auto lethal=m;lethal.attackers[1].damage=1000;assert(evaluate(lethal,Template::direct_elsewhere).legal==false);
    auto rival=m;rival.exclusiveAggro=false;assert(!evaluate(rival,Template::direct_elsewhere).legal);
    auto missed=m;missed.ren.hp=100;missed.healArrival=false;missed.reserve=80;
    assert(!evaluate(missed,Template::direct_elsewhere).legal);
    auto zero=m;zero.attackers[0].damage=0;assert(!evaluate(zero,Template::direct_elsewhere).legal);
    auto shield=m;shield.attackers[0].qualifying=false;assert(!evaluate(shield,Template::direct_elsewhere).legal);
    auto offrange=m;offrange.attackers[1].renReachable=false;assert(!evaluate(offrange,Template::direct_elsewhere).legal);
    RenewalTrace loop;assert(!loop.hit(0,true));assert(loop.hit(10,true));loop.payout(14,40);assert(loop.buy(30));assert(loop.gold==10);assert(loop.bank(14));assert(loop.renew(true));
    auto w=world(p10::Champion::ren);w.self.strength=35;w.self.hp=800;w.self.max_hp=900;w.groups={source(1,1,0)};w.groups[0].monsters.clear();
    for(int i=0;i<2;++i){p10::Monster a;a.id=10+i;a.group=1;a.position={1,i};a.observed=a.alive=true;a.hp=i?100:15;a.strength=10;a.range=3;w.groups[0].monsters.push_back(a);}
    p10::Actor healer;healer.id=2;healer.pick=1;healer.type=p10::Champion::willow;healer.position={-2,0};healer.visible=healer.alive=healer.ally=true;healer.hp=healer.max_hp=700;healer.focus=120;healer.abilities[0].ready=true;healer.can_cast=true;w.allies.push_back(healer);
    Policy activePolicy,controlPolicy;
    auto ad=activePolicy.step(w,p10::Arm::active),cd=controlPolicy.step(w,p10::Arm::control);
    assert(ad.monster==11&&cd.monster==10);
    w.allies.clear();w.self.hp=80;assert(activePolicy.step(w,p10::Arm::active).monster!=11);
    std::cout<<"PASS 09 qualifying per-attacker reaction/direct-work economy; lethal, zero-hit, competing target, missed healer negatives\n";
}

static p10_initiative::SlotProgram initiative_program(bool starts){
    using namespace p10_initiative;
    SlotProgram p;p.now=10;p.ourTeamStarts=starts;p.prerequisites=true;p.count=2;p.holder=p.originalHolder=0;
    p.workers[0]={1,0,15,priority(Dispatch::completion,0),10,0,0,2,true,false,true};
    p.workers[1]={2,1,14,priority(Dispatch::protection,1),10,0,0,0,true,false,true};
    p.eventsUsed=8;p.events[0]={1,11,0,-1,-1,false,true,{}};
    p.events[1]={0,11,0,0,-1,false,true,{}};
    p.events[2]={0,11,0,1,-1,true,true,{}};
    p.events[3]={0,11,5,2,-1,true,true,{}};
    p.events[4]={0,11,2,3,-1,false,true,{}};
    p.events[5]={0,11,2,4,-1,false,true,{}};
    p.events[6]={1,11,0,1,-1,false,true,{}};
    p.events[7]={1,11,1,5,6,false,true,{}};
    p.bankEvent=5;p.renewEvent=7;p.secured=14;p.personalGold=40;return p;
}
static void initiative(){
    using namespace p10_initiative;
    using Policy=p10_initiative::LegacyPolicy;
    static_assert(priority(Dispatch::protection,0)==79);
    static_assert(priority(Dispatch::completion,1)==62);
    static_assert(priority(Dispatch::loaded,2)==45);
    static_assert(priority(Dispatch::source,3)==28);
    static_assert(priority(Dispatch::circulation,4)==11);
    static_assert(priority(Dispatch::source,16)==-1);
    for(bool starts:{true,false}){
        auto active=initiative_program(starts),control=active;
        assert(solve(active,true));assert(solve(control,false));assert(better(active,control));
        assert(active.events[5].at.round<control.events[5].at.round);
        assert(active.events[3].at.round-active.events[2].at.round==5); // no shortened capture
        assert(rank(active,1,10,true)==rank(control,1,10,false)); // self-set applies next round
        assert((rank(active,1,11,true)%2)==(starts?1:0)); // alternating start parity
        auto harmed=active;harmed.displacedUsefulTurns=2;assert(!better(harmed,control));
        auto unseen=active;unseen.prerequisites=false;assert(!solve(unseen,true));
        auto campFirst=active;campFirst.events[0].campPhase=true;assert(solve(campFirst,true));assert(campFirst.events[0].at.phase==0);
    }
    Commitment c;assert(c.set(10,0,Dispatch::completion,false,true,false));
    assert(!c.set(11,0,Dispatch::circulation,false,true,false));
    assert(!c.set(12,0,Dispatch::protection,true,true,true)); // already channeling
    assert(c.set(12,0,Dispatch::protection,false,true,true));
    Commitment unseen;assert(unseen.set(10,0,Dispatch::protection,false,false,false));assert(unseen.category==Dispatch::loaded);
    Trace loop;assert(loop.capture({17,0,0},14,40));assert(loop.buy(30));assert(loop.gold==10);assert(loop.bank({21,1,2},14));assert(loop.renew({22,1,0}));
    auto w=world(p10::Champion::marcella);w.self.bag=14;p10::Altar altar;altar.id=0;altar.position={1,0};altar.visible=true;w.altars.push_back(altar);
    Policy active,control;auto ad=active.step(w,p10::Arm::active),cd=control.step(w,p10::Arm::control);
    assert(ad.task==p10::Task::bank&&ad.priority==priority(Dispatch::completion,0));assert(cd.priority==0);
    w.self.channel=p10::Channel::tribute;assert(active.step(w,p10::Arm::active).task==p10::Task::wait);
    assert(active.step(w,p10::Arm::active).priority==0);
    std::cout<<"PASS 10 next-round dispatch, real protection/custody/bank dependency, both parities, commitment/channel/hidden guards\n";
}
static void structural_wave(){
    using namespace p10;
    // 06 The policy itself manufactures surviving on-route stock. Its other
    // productive job and the consumer's route are actual physical alternatives.
    auto prep=world(Champion::marcella);prep.groups={source(1,1,0),source(2,4,0)};
    prep.groups[0].travel=0;prep.groups[1].travel=3;prep.own_payout=true;prep.own_gold=0;
    Actor consumer=prep.self;consumer.id=2;consumer.type=Champion::vendetta;consumer.cooldown[1]=6;prep.allies={consumer};
    p10_depots::Policy depot;
    const auto make=depot.step(prep,Arm::active);assert(make.task==Task::attack&&make.monster==10&&make.reserve_reset&&make.cast_slot==-1);
    assert(make.safety_verified&&make.safety_round==prep.round);assert(depot.preparerJob==2);
    assert(depot.manufactured<0);prep.own_payout=false;++prep.round;prep.groups[0].monsters[0].hp=50;
    prep.receipts={{Receipt::Kind::attack,prep.round-1,1,prep.self.id,0,0,0,70}};
    const auto released=depot.step(prep,Arm::active);assert(released.group==2&&released.task!=Task::attack);
    assert(depot.manufactured==1);
    auto use=prep;use.self=consumer;use.allies.clear();use.groups[0].monsters[0].hp=50;
    p10_depots::Policy striker;auto reset=striker.step(use,Arm::active);
    assert(reset.task==Task::attack&&reset.monster==10&&reset.cast_slot==1&&striker.confirmedUse<0);
    ++use.round;use.groups[0].monsters[0].alive=false;use.groups[0].monsters[0].hp=0;use.groups[0].cleared=true;
    use.receipts={{Receipt::Kind::attack,use.round-1,1,use.self.id,0,0,0,50}};
    use.self.channel=Channel::tribute;striker.step(use,Arm::active);assert(striker.confirmedUse==use.round-1);
    p10_depots::Policy ready;use.self.channel=Channel::none;use.self.cooldown[1]=0;assert(ready.step(use,Arm::active).cast_slot!=1);

    // 07 Reconstruct identical geometry appointments from a different actor,
    // vector order, and actor-relative route costs, without replaying draft.
    auto ew=world(Champion::varg);ew.groups={source(1,1,0),source(2,6,0),source(3,10,0),source(4,14,0)};
    for(auto&g:ew.groups)g.approach_pads={{g.position.x-1,0},{g.position.x-1,1}};
    p10_envelope::Policy envelopeA,envelopeB;
    envelopeA.fastrestore({Champion::willow,Champion::varg,Champion::marcella});
    envelopeB.fastrestore({Champion::willow,Champion::varg,Champion::marcella});
    auto ed=envelopeA.step(ew,Arm::active);assert(ed.group==1&&envelopeA.appointmentDuty==0&&ed.safety_verified);
    auto changed=ew;changed.self.position={4,1};std::reverse(changed.groups.begin(),changed.groups.end());
    for(auto&g:changed.groups){g.travel=100-g.id;g.approach={g.position.x+1,1};}
    envelopeB.step(changed,Arm::active);
    assert(envelopeA.appointmentGroup==envelopeB.appointmentGroup&&envelopeA.appointmentPad==envelopeB.appointmentPad);
    auto healer=ew;healer.self.type=Champion::willow;healer.self.range=3;healer.self.abilities[0].heal=true;healer.self.abilities[0].ready=true;healer.self.abilities[0].range=4;
    Actor lead=ew.self;lead.id=2;lead.hp=500;healer.allies={lead};
    p10_envelope::Policy service;service.fastrestore({Champion::willow,Champion::varg,Champion::marcella});
    assert(service.step(healer,Arm::active).task==Task::heal);
    healer.allies[0].hp=healer.allies[0].max_hp;const auto fallback=service.step(healer,Arm::active);assert(fallback.task!=Task::wait&&fallback.group!=1);
    assert(envelopeA.fresh_trajectories<=2&&envelopeB.fresh_trajectories<=2);

    // 08 Relative distance order jitter does not retire the actual fork or buy
    // capital. An actual divergence can pay an observed one-basic threshold.
    auto fw=world(Champion::tavi);fw.self.range=3;fw.groups={source(1,5,0),source(2,6,0)};fw.visible_open_tiles={{1,0},{2,0}};
    p10_forks::Policy fork;auto prefix=fork.step(fw,Arm::active);assert(prefix.task==Task::move&&prefix.template_id==30);
    const int created=fork.fork.createdRound;const auto retained=fork.sources;
    ++fw.round;fw.self.position={1,0};fw.groups[0].travel=8;fw.groups[1].travel=1;std::reverse(fw.groups.begin(),fw.groups.end());
    assert(fork.step(fw,Arm::active).template_id==30);assert(fork.sources==retained&&fork.fork.createdRound==created);
    fw.group(1);for(auto&g:fw.groups)if(g.id==2)g.approach={0,6};
    fw.self.strength_growth=10;fw.own_payout=true;fw.own_gold=30;for(auto&g:fw.groups){g.monsters[0].hp=75;g.approach=g.id==1?Point{4,0}:Point{0,6};}
    const auto threshold=fork.step(fw,Arm::active);assert(threshold.task==Task::buy&&threshold.item==Item::strength);
    assert(fork.fresh_trajectories<=2&&fork.committedSource>=0);
    auto broke=fw;broke.own_gold=0;p10_forks::Policy poor;assert(poor.step(broke,Arm::active).task!=Task::buy);

    // 09 Only the reaction-paying attacker needs strict nearest. A different
    // monster may have an equally near ally; killing that monster remains work.
    auto rw=world(Champion::ren);rw.self.hp=800;rw.self.strength=35;rw.groups={source(1,1,0)};rw.groups[0].monsters.clear();
    Monster weak;weak.id=10;weak.group=1;weak.position={1,0};weak.observed=weak.alive=true;weak.hp=15;weak.max_hp=100;weak.strength=10;weak.range=3;
    Monster other=weak;other.id=11;other.position={0,1};other.hp=30;other.strength=0;rw.groups[0].monsters={weak,other};
    Actor ally=rw.self;ally.id=3;ally.type=Champion::stellar;ally.position={-1,1};rw.allies={ally};
    p10_exposure::Policy mill;const auto reaction=mill.step(rw,Arm::active);assert(reaction.monster==11&&reaction.template_id==41&&reaction.reserve_reset&&reaction.safety_verified);
    auto tie=rw;tie.allies[0].position={2,0};p10_exposure::Policy invalid;assert(invalid.step(tie,Arm::active).template_id!=41);
    auto shield=rw;shield.self.shield=100;p10_exposure::Policy absorbed;assert(absorbed.step(shield,Arm::active).template_id!=41);
    auto approach=rw;approach.self.position={-4,0};approach.allies.clear();p10_exposure::Policy ingress;assert(ingress.step(approach,Arm::active).task==Task::move);

    // 10 Issuing a priority is not adoption. Release needs the real physical
    // holder, complete custody safety, and both current priorities observed.
    auto iw=world(Champion::vendetta);iw.self.pick=1;iw.groups={source(1,1,0),source(2,5,0)};
    auto&clear=iw.groups[0];clear.cleared=true;clear.living=false;clear.monsters[0].alive=false;clear.monsters[0].hp=0;
    Actor holder=iw.self;holder.id=2;holder.pick=0;holder.type=Champion::marcella;holder.position={1,0};holder.priority=0;iw.allies={holder};
    p10_initiative::Policy pipeline;const auto issued=pipeline.step(iw,Arm::active);assert(issued.task!=Task::release&&issued.priority!=0&&!pipeline.priorityAdopted);
    ++iw.round;iw.self.priority=p10_initiative::priority(p10_initiative::Dispatch::completion,1);iw.allies[0].priority=p10_initiative::priority(p10_initiative::Dispatch::loaded,0);
    const auto handoff=pipeline.step(iw,Arm::active);assert(handoff.task==Task::release&&pipeline.priorityAdopted);
    ++iw.round;iw.self.position={0,1};assert(pipeline.step(iw,Arm::active).group==2);
    auto lethal=iw;lethal.allies[0].hp=1;p10_initiative::Policy refuse;assert(refuse.step(lethal,Arm::active).task!=Task::release);
    // Both public alternatives stay selectable through the actual Policy API.
    p10_initiative::Policy prior;prior.variant=1;auto loaded=world(Champion::marcella);loaded.self.bag=7;assert(prior.step(loaded,Arm::active).task==Task::bank);
    std::cout<<"PASS structural06-10 actual callers: physical manufacture/receipt, immutable appointments, stable forks, strict reaction, adopted custody pipeline\n";
}
int main(){depots();envelope();forks();exposure();initiative();structural_wave();std::cout<<"PASS all Plan10 cards06-10 pure/live-contract fixtures (no native engine evidence)\n";}
