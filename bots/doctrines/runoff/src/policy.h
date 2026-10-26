#pragma once
#include "profile.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include <cstdint>

// Bounded offline core: one actual Marcella, two finite supplies and one
// distinct currently observed productive fallback. No respawn, PvP, support,
// repeated spell credit or projected third capture. Extra draft bodies stay
// independent. Quotes never substitute for actual actor-local receipts.
namespace p5::runoff {
inline constexpr int group_cap=3,segment_cap=2,purchase_cap=3,step_cap=8,event_cap=16;
// Runoff schedules always contain one actor and retain the existing event cap.
using RunoffSchedule=AccountingScheduleStorage<1,event_cap>;
inline constexpr int current_location=-1,altar_location=-2,spawn_location=-3;
struct Group {
    int id=-1,gems=0,gold=0;
    bool current=false,available=false,no_other_holder=false;
    std::array<CombatMonster,8> monsters{};std::size_t count=0;
    RouteLeg from_current{},from_altar{},from_spawn{},to_altar{},to_spawn{},certificate_return{};
};
struct Purchase {
    int item=-1,family=-1,cost=0,shards=0;
    double strength=0,health=0,spawn_heal=0,basic_ratio=1;
    // A newly activated Treasure/income passive needs a later explicit income
    // model. This core admits only a certified unchanged income rule.
    bool legal_on_spawn=false,no_new_income_passive=false;
};
struct Ledger {
    int captures=0,banks=0,purchases=0;unsigned consumed=0;
    bool liquidated=false; // active's first actual bank ends this finite harvest
};
struct Observation {
    int round=-1,team_score=0;
    bool actual_marcella=false,bank_safe=false,spawn_safe=false;
    ActorObservation resources{};CombatActor actor{};
    std::array<Group,group_cap> groups{}; // A, B, unscored complete fallback R
    RouteLeg A_to_B{},current_to_altar{},current_to_spawn{},altar_to_spawn{};
    int spawn_rest=1;double spawn_heal_per_tick=0;
    std::array<Purchase,purchase_cap> purchases{};int purchase_count=0;
    Ledger ledger{};EconomicSettings settings{};FinishWindows windows{};
    execution::Observation execution{};
};
enum class StepKind {produce,bank,shop,recover,renew};
struct Step {
    StepKind kind=StepKind::renew;int group=-1,index=-1,waypoint=-1,duration=0,purchase=-1;
    double health=0,strength=0,max_health=0,basic_modifier=0;
};
struct Quote {
    bool feasible=false,terminal=false;int count=0,prefix=0,purchase=-1,buy_before=-1;
    int first_bank=-1,last_bank=-1,occupied=0;std::array<int,2> capture_tick{{-1,-1}};
    std::array<Step,step_cap> steps{};RunoffSchedule schedule{};AccountingResult rank{};
};
enum class Job {unavailable,preserve,recover,harvest,liquidate,winning_bank,productive_fallback};
struct Decision {bool feasible=false;Job job=Job::unavailable;Profile profile{};Observation input{};Quote selected{},control{};};
inline bool finite(double n){return std::isfinite(n);}
inline bool valid_ledger(const Ledger& l){
    return l.captures>=0 && l.captures<=2 && l.banks>=0 && l.banks<=2 && l.purchases>=0 && l.purchases<=1 &&
        l.consumed<4 && l.captures==int(bool(l.consumed&1))+int(bool(l.consumed&2));
}
inline bool body_valid(const CombatActor& a){
    return a.id>=0 && a.self && a.visible && !a.ranged && a.enemy_cast_bound_known && finite(a.health) && finite(a.max_health) &&
        a.health>0 && a.max_health>=a.health && finite(a.strength) && a.strength>0 && finite(a.basic_modifier) && a.basic_modifier>0 &&
        finite(a.shield) && a.shield>=0 && a.shield_expiry_tick>=0 && finite(a.visible_enemy_next_phase) && a.visible_enemy_next_phase>=0;
}
inline bool safe(const CombatActor& a){return body_valid(a) && a.health>=std::max(.2*a.max_health,2*a.visible_enemy_next_phase);}
inline bool expose(CombatActor& a,int n,double rate){
    if(!body_valid(a) || n<0 || n>100000 || !finite(rate) || rate<0)return false;
    const int protected_ticks=std::min(n,a.shield_expiry_tick);const double absorbed=std::min(a.shield,protected_ticks*rate);
    a.health-=n*rate-absorbed;a.shield-=absorbed;a.shield_expiry_tick=std::max(0,a.shield_expiry_tick-n);
    if(!a.shield_expiry_tick)a.shield=0;return safe(a);
}
inline int turns(const RouteLeg& r){return r.safety_checked?r.turns():-1;}
inline bool walk(CombatActor& a,const RouteLeg& r,int& tick,int named=-1){
    const int n=turns(r);if(n<0 || (named>=0 && r.destination!=named) || tick>100000-n || !expose(a,n,a.visible_enemy_next_phase))return false;
    tick+=n;return true;
}
inline bool group_valid(const Group& g,bool live=true){
    if(g.id<0 || g.gems<=0 || g.gold<0 || !g.count || g.count>8 || (live && (!g.current || !g.available || !g.no_other_holder)))return false;
    for(std::size_t m=0;m<g.count;++m){const auto& x=g.monsters[m];
        if(x.id<0 || !finite(x.public_health) || x.public_health<=0 || !finite(x.observed_health) || !finite(x.effective_strength) || x.effective_strength<0 ||
            (x.visible && (x.observed_health<0 || x.observed_health>x.public_health)))return false;
        for(std::size_t n=0;n<m;++n)if(g.monsters[n].id==x.id)return false;
    }return true;
}
inline bool clear(const Group& g,CombatActor& a,int& tick){
    if(!group_valid(g) || !safe(a))return false;std::array<CombatMonster,8> live{};int count=0;
    for(std::size_t m=0;m<g.count;++m)if(!g.monsters[m].visible || g.monsters[m].observed_health>0)live[count++]=g.monsters[m];
    const auto hp=[](const CombatMonster& m){return m.visible?m.observed_health:m.public_health;};
    std::sort(live.begin(),live.begin()+count,[&](const auto& x,const auto& y){return hp(x)==hp(y)?x.id<y.id:hp(x)<hp(y);});
    for(int m=0;m<count;++m){const double raw=std::ceil(hp(live[m])/(a.strength*a.basic_modifier));
        if(!finite(raw) || raw<1 || raw>100000)return false;const int n=int(raw);double rate=a.visible_enemy_next_phase;
        for(int j=m;j<count;++j)rate+=live[j].effective_strength;
        if(tick>100000-n || !expose(a,n,rate))return false;tick+=n;
        if(m+1<count){if(!expose(a,2,rate-live[m].effective_strength))return false;tick+=2;}
    }return true;
}
inline bool event(Quote& q,AccountingEventKind kind,int tick,int gems=0,int gold=0,int group=-1){
    if(q.schedule.event_count>=event_cap || tick<0)return false;auto& e=q.schedule.events[q.schedule.event_count];e={};
    e.id=q.schedule.event_count++;e.kind=kind;e.tick=tick;e.actor=0;e.gems=gems;e.gold=gold;e.group=group;
    if(kind==AccountingEventKind::capture)e.participants=e.holders=1;if(kind==AccountingEventKind::return_to_work)e.waypoint=group;return true;
}
inline bool step(Quote& q,Step s){if(q.count>=step_cap)return false;q.steps[q.count++]=s;return true;}
struct Stock {CombatActor actor{};int tick=0,bag=0,gold=0,score=0,location=current_location;Ledger ledger{};};
// Successful time tests remain true while both remaining horizons stay above
// these floors. Failed time tests remain false when a horizon only shrinks.
// The preparation worker records all successes, including discarded quotes.
struct HorizonProof {
    int game_floor=0,official_floor=0;
    void game(int tick){game_floor=std::max(game_floor,tick);}
    void official(int tick){official_floor=std::max(official_floor,tick);}
};
inline RunoffSchedule initial(const Observation& o){
    RunoffSchedule s;s.actor_count=1;s.target_id=o.groups[2].id;s.starting_team_score=o.team_score;s.win_score=o.settings.winning_gems;
    s.tribute_gems_per_tick=o.settings.tribute_gems_per_tick;s.tribute_min_ticks=o.settings.tribute_min_ticks;s.doctrine_id=16;
    s.actors[0]={o.resources.id,true,true,true,o.resources.bag.value,o.resources.gold.value,o.settings.marcella_gold_per_gem,0};return s;
}
inline RouteLeg access(const Observation& o,int location,int i){
    if(location==current_location)return o.groups[i].from_current;if(location==altar_location)return o.groups[i].from_altar;
    if(location==spawn_location)return o.groups[i].from_spawn;if(location==0 && i==1)return o.A_to_B;return {};
}
inline RouteLeg to_spawn(const Observation& o,int location){
    if(location==current_location)return o.current_to_spawn;if(location==altar_location)return o.altar_to_spawn;
    if(location>=0 && location<3)return o.groups[location].to_spawn;return {};
}
inline bool recovery(const Observation& o,Stock& a,Quote& q,bool shop){
    const int start=a.tick;if(!o.spawn_safe || o.spawn_rest<1 || o.spawn_rest>32 || !finite(o.spawn_heal_per_tick) || o.spawn_heal_per_tick<0 ||
        !walk(a.actor,to_spawn(o,a.location),a.tick,o.current_to_spawn.destination) || !expose(a.actor,o.spawn_rest,0))return false;
    a.tick+=o.spawn_rest;a.location=spawn_location;a.actor.health=std::min(a.actor.max_health,a.actor.health+o.spawn_rest*o.spawn_heal_per_tick);
    return shop || step(q,{StepKind::recover,-1,-1,to_spawn(o,current_location).destination,a.tick-start,-1,a.actor.health});
}
inline bool renewal_certificate(const Observation& o,CombatActor a,int remaining,HorizonProof* proof=nullptr,int elapsed=0){
    int t=0;const auto& g=o.groups[2];if(!clear(g,a,t) || !expose(a,o.settings.capture_rounds+1,a.visible_enemy_next_phase))return false;t+=o.settings.capture_rounds+1;
    if(!walk(a,g.to_altar,t,o.current_to_altar.destination))return false;const int n=o.settings.channel_ticks(g.gems);
    if(n<1 || !expose(a,n,a.visible_enemy_next_phase))return false;t+=n;
    if(!walk(a,g.certificate_return,t) || t>remaining)return false;if(proof)proof->game(elapsed+t);return true;
}
inline bool finish(const Observation& o,Stock& a,Quote& q,HorizonProof* proof=nullptr){
    if(a.bag)return false;auto direct=a;const int start=a.tick;const auto leg=access(o,a.location,2);
    if(!walk(direct.actor,leg,direct.tick,o.groups[2].id) || !renewal_certificate(o,direct.actor,o.settings.round_limit-o.round-direct.tick,proof,direct.tick)){
        if(!recovery(o,a,q,false) || !walk(a.actor,o.groups[2].from_spawn,a.tick,o.groups[2].id) ||
            !renewal_certificate(o,a.actor,o.settings.round_limit-o.round-a.tick,proof,a.tick))return false;
    }else a=direct;
    if(!step(q,{StepKind::renew,o.groups[2].id,2,o.groups[2].id,a.tick-start}) || !event(q,AccountingEventKind::return_to_work,a.tick,0,0,o.groups[2].id))return false;
    a.location=2;q.occupied=a.tick;q.schedule.actors[0].occupied_turns=a.tick;return true;
}
inline bool bank(const Observation& o,Stock& a,Quote& q){
    if(a.ledger.banks>=2 || a.bag<=0 || !o.bank_safe || o.current_to_altar.destination<0)return false;const int start=a.tick;
    if(a.location!=altar_location && !walk(a.actor,a.location==current_location?o.current_to_altar:o.groups[a.location].to_altar,a.tick,o.current_to_altar.destination))return false;
    const int n=o.settings.channel_ticks(a.bag);if(n<1 || !expose(a.actor,n,a.actor.visible_enemy_next_phase) || !event(q,AccountingEventKind::tribute_start,a.tick,-1))return false;
    a.tick+=n;const std::int64_t income=std::int64_t(a.bag)*o.settings.marcella_gold_per_gem;
    if(income>std::numeric_limits<int>::max()-a.gold || a.bag>std::numeric_limits<int>::max()-a.score || !event(q,AccountingEventKind::tribute_complete,a.tick,-1))return false;
    a.gold+=int(income);a.score+=a.bag;a.bag=0;a.location=altar_location;++a.ledger.banks;
    if(!step(q,{StepKind::bank,-1,-1,o.current_to_altar.destination,a.tick-start}))return false;
    if(q.first_bank<0)q.first_bank=a.tick;q.last_bank=a.tick;
    if(a.score>=o.settings.winning_gems){q.terminal=true;q.schedule.terminal_win=true;q.schedule.terminal_win_tick=a.tick;q.occupied=a.tick;q.schedule.actors[0].occupied_turns=a.tick;}
    return true;
}
inline bool purchase_valid(const Purchase& p){return p.item>=0 && p.family>=0 && p.cost>0 && p.shards==1 && p.legal_on_spawn && p.no_new_income_passive &&
    finite(p.strength) && p.strength>=0 && finite(p.health) && p.health>=0 && finite(p.spawn_heal) && p.spawn_heal>=0 && p.spawn_heal<=p.health &&
    finite(p.basic_ratio) && p.basic_ratio>=1;}
inline bool current_capacity(const Observation& o,int i,CombatActor a,int& tick){
    if(!clear(o.groups[i],a,tick) || !expose(a,o.settings.capture_rounds+1,a.visible_enemy_next_phase))return false;tick+=o.settings.capture_rounds+1;
    if(!walk(a,o.groups[i].to_altar,tick,o.current_to_altar.destination))return false;return expose(a,o.settings.channel_ticks(o.groups[i].gems),a.visible_enemy_next_phase);
}
inline bool buy(const Observation& o,int i,int pi,Stock& a,Quote& q){
    if(pi<0 || pi>=o.purchase_count || a.ledger.purchases || !purchase_valid(o.purchases[pi]) || a.gold<o.purchases[pi].cost)return false;
    const auto& p=o.purchases[pi];const int start=a.tick;
    if(!recovery(o,a,q,true))return false;const auto before=a.actor;a.actor.strength+=p.strength;a.actor.max_health+=p.health;
    a.actor.health=std::min(a.actor.max_health,a.actor.health+p.spawn_heal);a.actor.basic_modifier*=p.basic_ratio;
    int plain=0,upgraded=0;const bool old_safe=current_capacity(o,i,before,plain),new_safe=current_capacity(o,i,a.actor,upgraded);
    // Focus-only items and speculative future emblem payback are deliberately
    // absent from this basic-only core. Capacity must affect the named source.
    if(!new_safe || (old_safe && upgraded>=plain))return false;
    if(!safe(a.actor) || !event(q,AccountingEventKind::purchase,a.tick,0,p.cost) ||
        !step(q,{StepKind::shop,-1,i,to_spawn(o,current_location).destination,a.tick-start,pi,a.actor.health,a.actor.strength,a.actor.max_health,a.actor.basic_modifier}))return false;
    a.gold-=p.cost;++a.ledger.purchases;return true;
}
inline bool produce(const Observation& o,int i,Stock& a,Quote& q){
    if(i<0 || i>=2 || a.ledger.captures>=2 || (a.ledger.consumed&(1u<<i)))return false;const int start=a.tick;
    if(!walk(a.actor,access(o,a.location,i),a.tick,o.groups[i].id) || !clear(o.groups[i],a.actor,a.tick) ||
        !expose(a.actor,o.settings.capture_rounds+1,a.actor.visible_enemy_next_phase))return false;
    a.tick+=o.settings.capture_rounds+1;
    const auto& g=o.groups[i];if(g.gems>std::numeric_limits<int>::max()-a.bag || g.gold>std::numeric_limits<int>::max()-a.gold ||
        !event(q,AccountingEventKind::capture,a.tick,g.gems,g.gold,g.id) || !step(q,{StepKind::produce,g.id,i,g.id,a.tick-start}))return false;
    a.bag+=g.gems;a.gold+=g.gold;a.location=i;++a.ledger.captures;a.ledger.consumed|=1u<<i;q.capture_tick[i]=a.tick;return true;
}
inline bool valid_input(const Observation& o){
    const auto r=private_view(o.resources,o.round);
    if(o.round<0 || o.round>=o.settings.round_limit || !o.actual_marcella || !o.settings.valid() || o.settings.capture_rounds>=100000 || !o.windows.valid || !valid_ledger(o.ledger) ||
        !r.self || !r.visible || !r.own_team || !r.bag.current(o.round) || !r.gold.current(o.round) || r.bag.value<0 || r.gold.value<0 ||
        o.actor.id!=r.id || !safe(o.actor) || o.purchase_count<0 || o.purchase_count>3 || o.team_score<0 ||
        o.current_to_altar.destination<0 || o.current_to_spawn.destination<0)return false;
    for(int i=0;i<3;++i){if(!group_valid(o.groups[i],i==2))return false;for(int j=0;j<i;++j)if(o.groups[i].id==o.groups[j].id)return false;}
    return true;
}
namespace detail {
// The public entrypoints validate once; catalogue candidates share that proof
// only for this immutable observation and this selector call.
inline void seed_schedule(RunoffSchedule& s,const Observation& o){
    s.actor_count=1;s.event_count=0;s.target_id=o.groups[2].id;s.starting_team_score=o.team_score;s.win_score=o.settings.winning_gems;
    s.tribute_gems_per_tick=o.settings.tribute_gems_per_tick;s.tribute_min_ticks=o.settings.tribute_min_ticks;
    s.terminal_win=false;s.terminal_win_tick=-1;s.immediate_legal_winning_bank=false;s.doctrine_id=16;
    s.denial_evidenced=false;s.denied_gems=0;s.denied_bank_tick=-1;s.projected_kills=0;
    s.actors[0]={o.resources.id,true,true,true,o.resources.bag.value,o.resources.gold.value,o.settings.marcella_gold_per_gem,0};
}
inline void quote_into(const Observation& o,Profile profile,int prefix,int pi,int buy_before,Quote& q,HorizonProof* proof=nullptr){
    q.feasible=false;q.terminal=false;q.count=0;q.prefix=prefix;q.purchase=pi;q.buy_before=pi<0?-1:buy_before;
    q.first_bank=-1;q.last_bank=-1;q.occupied=0;q.capture_tick={{-1,-1}};
    if(prefix<0 || prefix>2 || (pi>=0 && (prefix==0 || buy_before>=prefix)))return;
    // Counted events and steps are fully overwritten by event()/step(). The
    // scratch rank is never read until a new forecast has replaced it.
    seed_schedule(q.schedule,o);Stock a{o.actor,0,o.resources.bag.value,o.resources.gold.value,o.team_score,current_location,o.ledger};
    std::array<int,2> remaining{};int count=0;for(int i=0;i<2;++i)if(!(a.ledger.consumed&(1u<<i)))remaining[count++]=i;
    if(prefix>count || (profile.arm==Arm::finite_harvest && o.ledger.liquidated && prefix))return;
    if(profile.arm==Arm::bank_each_control && a.bag && !bank(o,a,q))return;
    for(int n=0;n<prefix && !q.terminal;++n){const int i=remaining[n];
        if(pi>=0 && buy_before==n && !buy(o,i,pi,a,q))return;
        if(!produce(o,i,a,q))return;
        if(profile.arm==Arm::bank_each_control && !bank(o,a,q))return;
        // A useful first-prefix winning liquidation is never delayed for B.
        if(profile.arm==Arm::finite_harvest && a.score+a.bag>=o.settings.winning_gems && !bank(o,a,q))return;
    }
    if(!q.terminal && a.bag && !bank(o,a,q))return;
    if(!q.terminal && !finish(o,a,q,proof))return;
    if(a.tick>o.settings.round_limit-o.round)return;if(proof)proof->game(a.tick);
    if(profile.arm==Arm::finite_harvest && prefix>0 &&
        q.last_bank>*std::min_element(o.windows.window.begin(),o.windows.window.end()))return;
    q.rank=forecast_schedule(q.schedule,o.windows);q.feasible=q.rank.status==AccountingStatus::feasible;
    // Every Runoff event and tribute completion is no later than occupied.
    // Other accounting horizon dependencies are the validated window bounds
    // and official_endpoint, both checked again by the preparation worker.
    if(q.feasible && proof)proof->official(q.occupied);
}
inline Quote quote_validated(const Observation& o,Profile profile,int prefix,int pi=-1,int buy_before=0){
    Quote q;quote_into(o,profile,prefix,pi,buy_before,q);return q;
}
} // namespace detail
inline Quote quote(const Observation& o,Profile profile,int prefix,int pi=-1,int buy_before=0){
    if(valid_input(o))return detail::quote_validated(o,profile,prefix,pi,buy_before);
    Quote q;q.prefix=prefix;q.purchase=pi;q.buy_before=pi<0?-1:buy_before;return q;
}
inline bool better(const Quote& a,const Quote& b){return a.feasible && (!b.feasible || compare_results(a.rank,b.rank)>0);}
namespace detail {
inline Quote best_validated(const Observation& o,Profile p){
    std::array<int,2> remaining{};int count=0;for(int i=0;i<2;++i)if(!(o.ledger.consumed&(1u<<i)))remaining[count++]=i;
    const int limit=p.arm==Arm::finite_harvest && o.ledger.liquidated?0:count;
    std::array<std::array<bool,purchase_cap>,2> possible{};
    if(!o.ledger.purchases && limit){
        std::int64_t gold=o.resources.gold.value,gems=o.resources.bag.value;
        bool terminal_before=p.arm==Arm::bank_each_control && gems>0 && std::int64_t(o.team_score)+gems>=o.settings.winning_gems;
        if(p.arm==Arm::bank_each_control)gold+=gems*o.settings.marcella_gold_per_gem;
        for(int n=0;n<limit;++n){
            if(n){const auto& g=o.groups[remaining[n-1]];gold+=g.gold;gems+=g.gems;
                if(p.arm==Arm::bank_each_control)gold+=std::int64_t(g.gems)*o.settings.marcella_gold_per_gem;
                if((p.arm==Arm::finite_harvest || p.arm==Arm::bank_each_control) && std::int64_t(o.team_score)+gems>=o.settings.winning_gems)terminal_before=true;}
            for(int pi=0;pi<o.purchase_count;++pi){const auto& item=o.purchases[pi];
                // Before a purchase, only preceding captures and control banks
                // add gold. An unchanged post-recovery actor cannot pass buy's
                // strict capacity improvement. Retain every candidate that may
                // terminate before reaching its purchase, even an invalid one.
                possible[n][pi]=terminal_before || (purchase_valid(item) && gold>=item.cost &&
                    (item.strength>0 || item.health>0 || item.basic_ratio>1));}
        }
    }
    Quote result,scratch;for(int prefix=0;prefix<=limit;++prefix){quote_into(o,p,prefix,-1,0,scratch);if(better(scratch,result))result=scratch;
        if(!o.ledger.purchases)for(int pi=0;pi<o.purchase_count;++pi)for(int n=0;n<prefix;++n){if(!possible[n][pi])continue;
            quote_into(o,p,prefix,pi,n,scratch);if(better(scratch,result))result=scratch;}}
    // Publish the same complete value as a fresh quote, including unused tails.
    if(result.feasible){std::fill(result.steps.begin()+result.count,result.steps.end(),Step{});
        std::fill(result.schedule.events.begin()+result.schedule.event_count,result.schedule.events.end(),AccountingEvent{});}
    return result;
}
} // namespace detail
inline Quote best(const Observation& o,Profile p){return valid_input(o)?detail::best_validated(o,p):Quote{};}
namespace detail {
inline bool winning_bank_candidate(const Observation& o){
    const auto r=private_view(o.resources,o.round);
    return o.settings.valid() && o.actual_marcella && r.self && r.own_team && r.bag.current(o.round) && r.gold.current(o.round) && r.bag.value>0 &&
        r.gold.value>=0 && o.ledger.banks<2 && o.execution.safe && o.execution.adjacent_altar && o.execution.legal_winning_bank &&
        std::int64_t(o.team_score)+r.bag.value>=o.settings.winning_gems;
}
// The caller supplies a fresh default Decision with its input/profile set.
// A failed bank deliberately falls through to the ordinary catalogue.
inline bool shortcut_into(const Observation& o,Decision& d){
    if(o.execution.channel!=execution::Channel::none){d.feasible=true;d.job=Job::preserve;return true;}
    const auto r=private_view(o.resources,o.round);
    if(winning_bank_candidate(o)){
        d.selected.schedule=initial(o);Stock a{o.actor,0,r.bag.value,r.gold.value,o.team_score,altar_location,o.ledger};
        auto immediate=o;immediate.current_to_altar.destination=o.execution.altar_id;
        if(bank(immediate,a,d.selected)){d.selected.schedule.immediate_legal_winning_bank=true;d.selected.rank=forecast_schedule(d.selected.schedule,o.windows);
            d.selected.feasible=d.feasible=d.selected.rank.status==AccountingStatus::feasible;d.job=d.feasible?Job::winning_bank:Job::unavailable;return true;}}
    return false;
}
} // namespace detail
inline Decision choose_job(const Observation& o,Profile profile={}){
    Decision d;d.input=o;d.profile=profile;if(detail::shortcut_into(o,d))return d;
    if(!valid_input(o))return d;d.control=detail::best_validated(o,{Arm::bank_each_control});d.selected=profile.arm==Arm::bank_each_control?d.control:detail::best_validated(o,profile);
    d.feasible=d.selected.feasible;if(d.feasible)d.job=d.selected.prefix?Job::harvest:o.resources.bag.value?Job::liquidate:Job::productive_fallback;return d;
}
struct Facts {
    std::uint64_t capture_receipt=0,bank_receipt=0,purchase_receipt=0,recovery_receipt=0;
    int navigation_round=-1,navigation_goal=-1,recovery_round=-1;
    bool captured_region_known=false,on_captured_region=false;
    bool moves_leave_region=false,at_productive=false;int productive_group=-1,productive_round=-1;
    bool bank_confirmed=false;int banked_gems=-1,bank_gold=-1,bank_altar=-1;
    bool purchase_confirmed=false;int purchased_item=-1,purchased_family=-1,purchased_cost=-1,purchased_shards=0;
    int gear_item=-1,shard_equivalent_count=-1; // raw shards +5*owned emblems, not raw shard count
};
enum class Stage {aborted,running,reobserve,complete};
struct Project {
    Decision selected{};execution::State executor{};Stage stage=Stage::aborted;Ledger ledger{};
    int index=0,bag=0,gold=0,intent_turn=-1,bank_ticks=0,arrival=-1,item_count=-1;
    std::uint64_t capture_base=0,bank_base=0,purchase_base=0,recovery_base=0,intent=0;
    bool initialized=false,must_vacate=false,pending_replan=false;
};
inline Project start_project(const Decision& d,const Facts&){Project p;p.selected=d;p.ledger=d.input.ledger;if(d.feasible)p.stage=Stage::running;return p;}
// Keep accepted receipts, ledger and the captured-region exit obligation while
// installing a completely prepared fresh decision. Binding will seed new bases.
inline void install_replan(Project& p,const Decision& d){
    p.selected=d;p.index=0;p.initialized=false;p.intent=0;p.pending_replan=false;p.stage=d.feasible?Stage::running:Stage::reobserve;
}
inline bool immediate_tail_safe(const Observation& o,CombatActor a,int bag,bool include_produce,int i,bool require_deadline=false){
    int t=0;Stock s{a,0,bag,o.resources.gold.value,o.team_score,current_location,o.ledger};Quote proof;proof.schedule=initial(o);
    if(include_produce){if(i<0 || i>=2)return false;
        if((!o.execution.at_target || o.execution.group_id!=o.groups[i].id) && !walk(s.actor,o.groups[i].from_current,s.tick,o.groups[i].id))return false;
        if(!clear(o.groups[i],s.actor,s.tick) || !expose(s.actor,o.settings.capture_rounds+1,s.actor.visible_enemy_next_phase))return false;
        s.tick+=o.settings.capture_rounds+1;if(bag>std::numeric_limits<int>::max()-o.groups[i].gems)return false;s.bag+=o.groups[i].gems;s.location=i;
    }else if(o.execution.adjacent_altar)s.location=altar_location;
    if(s.bag && !bank(o,s,proof))return false;
    if(include_produce && require_deadline && proof.last_bank>*std::min_element(o.windows.window.begin(),o.windows.window.end()))return false;
    if(proof.terminal)return s.tick<=o.execution.remaining_rounds;
    t=s.tick;return finish(o,s,proof) && t<=s.tick && s.tick<=o.execution.remaining_rounds;
}
inline execution::Decision execute_job(const Observation& fresh,const Facts& f,Project& p,bool bind_only=false,bool defer_replan=false){
    namespace e=execution;auto o=fresh.execution;
    auto stop=[&](Stage stage,e::Reason why){p.stage=stage;return e::emit(p.executor,e::ActionKind::refresh,why);};
    if(p.stage!=Stage::running)return e::emit(p.executor,e::ActionKind::yield,e::Reason::illegal);
    if(p.pending_replan)return e::emit(p.executor,e::ActionKind::refresh);
    if(o.channel!=e::Channel::none)return e::decision(o,p.executor);
    const auto r=private_view(fresh.resources,fresh.round);
    if(fresh.round!=o.owner_turn || r.id!=p.selected.input.resources.id || !r.self || !r.own_team || !r.bag.current(fresh.round) || !r.gold.current(fresh.round) ||
        r.bag.value<0 || r.gold.value<0 || fresh.actor.id!=r.id || !fresh.actual_marcella || !fresh.settings.valid() || fresh.settings.capture_rounds>=100000 || !valid_ledger(p.ledger))return stop(Stage::aborted,e::Reason::illegal);
    e::consume_feedback(o,p.executor);
    auto replan=[&](){if(defer_replan){p.pending_replan=true;p.intent=0;return;}
        auto now=fresh;now.ledger=p.ledger;auto d=choose_job(now,p.selected.profile);
        if(!d.feasible){p.stage=Stage::reobserve;return;}install_replan(p,d);};
    if(p.selected.job==Job::preserve)return stop(Stage::reobserve,e::Reason::ordinary);
    auto& q=p.selected.selected;
    if(p.index<0 || p.index>=q.count || q.count>step_cap)return stop(Stage::aborted,e::Reason::illegal);
    auto move=[&](int goal,e::ActionKind kind=e::ActionKind::move){if(f.navigation_round!=fresh.round || f.navigation_goal!=goal)return e::emit(p.executor,e::ActionKind::refresh,e::Reason::unknown_target,goal);
        return e::move_intent(o,p.executor,kind);};
    const auto s=q.steps[p.index];
    if(!p.initialized){if(r.bag.value!=p.selected.input.resources.bag.value || r.gold.value!=p.selected.input.resources.gold.value)return stop(Stage::reobserve,e::Reason::illegal);
        p.initialized=true;p.bag=r.bag.value;p.gold=r.gold.value;p.capture_base=f.capture_receipt;p.bank_base=f.bank_receipt;p.purchase_base=f.purchase_receipt;p.recovery_base=f.recovery_receipt;
        p.arrival=-1;p.item_count=f.shard_equivalent_count;p.intent=0;o.forecast_turns=s.duration;
        e::enter(p.executor,s.kind==StepKind::bank?e::Phase::bank:s.kind==StepKind::shop?e::Phase::shop:s.kind==StepKind::recover?e::Phase::recover:s.kind==StepKind::renew?e::Phase::renew:e::Phase::travel,o);
        p.executor.target=s.group;p.executor.travel_next=e::Phase::clear;
        // Native callers first bind the new typed goal. No shop/bank intent may
        // be latched and discarded merely because initialization changed phase.
        if(bind_only)return e::emit(p.executor,e::ActionKind::refresh);}
    if(s.kind==StepKind::produce && o.own_payout_observed && o.payout_receipt>p.executor.consumed_payout){
        const auto& g=p.selected.input.groups[s.index];
        if((p.executor.phase!=e::Phase::clear && p.executor.phase!=e::Phase::hold) || o.own_payout_group!=s.group || f.capture_receipt!=o.payout_receipt ||
            f.capture_receipt<=p.capture_base || o.own_gems_received!=g.gems || o.own_gold_received!=g.gold ||
            std::int64_t(p.bag)+g.gems!=r.bag.value || std::int64_t(p.gold)+g.gold!=r.gold.value || (p.ledger.consumed&(1u<<s.index)))return stop(Stage::reobserve,e::Reason::payout);
        p.executor.consumed_payout=o.payout_receipt;++p.ledger.captures;p.ledger.consumed|=1u<<s.index;p.must_vacate=true;replan();return e::emit(p.executor,e::ActionKind::refresh,e::Reason::payout);
    }
    if(s.kind==StepKind::bank && p.intent && f.bank_receipt>p.bank_base){
        const std::int64_t income=std::int64_t(p.bag)*fresh.settings.marcella_gold_per_gem;
        if(!f.bank_confirmed || f.banked_gems!=p.bag || f.bank_gold!=income || f.bank_altar!=s.waypoint || r.bag.value!=0 ||
            std::int64_t(p.gold)+income!=r.gold.value || o.owner_turn<p.intent_turn+p.bank_ticks)return stop(Stage::reobserve,e::Reason::illegal);
        ++p.ledger.banks;if(p.selected.profile.arm==Arm::finite_harvest)p.ledger.liquidated=true;
        if(fresh.team_score>=fresh.settings.winning_gems){p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::yield);}replan();return e::emit(p.executor,e::ActionKind::refresh,e::Reason::payout);
    }
    if(s.kind==StepKind::shop && p.intent && f.purchase_receipt>p.purchase_base){const auto& item=p.selected.input.purchases[s.purchase];
        if(!f.purchase_confirmed || f.purchased_item!=item.item || f.purchased_family!=item.family || f.purchased_cost!=item.cost || f.purchased_shards!=1 ||
            f.gear_item!=item.item || f.shard_equivalent_count!=p.item_count+1 || r.gold.value!=p.gold-item.cost || r.bag.value!=p.bag || !o.at_spawn ||
            !safe(fresh.actor) || fresh.actor.health<s.health || fresh.actor.strength<s.strength || fresh.actor.max_health<s.max_health || fresh.actor.basic_modifier<s.basic_modifier ||
            !immediate_tail_safe(fresh,fresh.actor,r.bag.value,true,s.index,p.selected.profile.arm==Arm::finite_harvest))return stop(Stage::reobserve,e::Reason::illegal);
        ++p.ledger.purchases;replan();return e::emit(p.executor,e::ActionKind::refresh);
    }
    if(s.kind==StepKind::produce && o.group_cleared && o.on_group_spawn && o.group_id==s.group && o.safe){o.spells={};o.needs_recovery=false;return e::decision(o,p.executor);}
    if(!o.alive || !o.safe || !safe(fresh.actor))return stop(Stage::aborted,e::Reason::unsafe);
    if(o.owner_turn-p.executor.phase_start>=p.executor.phase_budget)return stop(Stage::reobserve,e::Reason::timeout);
    if(r.bag.value!=p.bag || r.gold.value!=p.gold)return stop(Stage::reobserve,e::Reason::illegal);
    if(p.must_vacate && (f.captured_region_known?f.on_captured_region:o.on_group_spawn)){if(!f.moves_leave_region)return e::emit(p.executor,e::ActionKind::refresh,e::Reason::unknown_target);return move(s.waypoint,e::ActionKind::vacate);}p.must_vacate=false;
    if(s.kind==StepKind::produce){
        if(fresh.groups[s.index].id!=s.group || !immediate_tail_safe(fresh,fresh.actor,r.bag.value,true,s.index,p.selected.profile.arm==Arm::finite_harvest))return stop(Stage::reobserve,e::Reason::unsafe);
        if(!o.at_target || o.group_id!=s.group)return move(s.group);
        bool member=false;const auto& g=fresh.groups[s.index];for(std::size_t m=0;m<g.count;++m)member|=g.monsters[m].id==o.attack_target && (!g.monsters[m].visible || g.monsters[m].observed_health>0);
        if(!member && !o.group_cleared)return stop(Stage::reobserve,e::Reason::illegal);o.spells={};o.needs_recovery=false;return e::decision(o,p.executor);
    }
    if(s.kind==StepKind::bank){
        if(p.intent)return o.owner_turn>p.intent_turn+p.bank_ticks?stop(Stage::reobserve,e::Reason::illegal):e::emit(p.executor,e::ActionKind::refresh);
        if(!immediate_tail_safe(fresh,fresh.actor,r.bag.value,false,-1))return stop(Stage::reobserve,e::Reason::unsafe);
        if(!o.adjacent_altar)return move(s.waypoint);if(o.altar_id!=s.waypoint)return stop(Stage::reobserve,e::Reason::illegal);
        o.own_bag=r.bag.value;o.bank_requested=true;o.tribute_ticks_quote=fresh.settings.channel_ticks(r.bag.value);o.needs_recovery=false;
        auto out=e::decision(o,p.executor);if(out.action.kind==e::ActionKind::start_tribute){p.intent=out.action.sequence;p.intent_turn=o.owner_turn;p.bank_ticks=out.action.channel_ticks;}return out;
    }
    if(s.kind==StepKind::shop || s.kind==StepKind::recover){
        if(s.kind==StepKind::shop && p.intent)return o.owner_turn>p.intent_turn?stop(Stage::reobserve,e::Reason::illegal):e::emit(p.executor,e::ActionKind::refresh);
        if(!o.at_spawn)return move(s.waypoint);if(p.arrival<0)p.arrival=o.owner_turn;
        if(o.owner_turn-p.arrival<p.selected.input.spawn_rest || !o.spawn_recovery_observed || f.recovery_receipt<=p.recovery_base || f.recovery_round!=fresh.round)
            return e::emit(p.executor,e::ActionKind::yield,e::Reason::arrival_recovery);
        if(s.kind==StepKind::recover && fresh.actor.health<s.health)return stop(Stage::reobserve,e::Reason::unsafe);
        if(s.kind==StepKind::recover){replan();return e::emit(p.executor,e::ActionKind::refresh);}
        const auto& item=p.selected.input.purchases[s.purchase];
        if(!purchase_valid(item) || r.gold.value<item.cost || f.gear_item!=item.item || p.item_count<0)return stop(Stage::reobserve,e::Reason::illegal);
        o.purchase_requested=o.purchase_purchasable=o.purchase_affordable=true;o.purchase_id=item.item;o.needs_recovery=false;
        auto out=e::decision(o,p.executor);if(out.action.kind==e::ActionKind::buy){p.intent=out.action.sequence;p.intent_turn=o.owner_turn;}return out;
    }
    if(r.bag.value || fresh.groups[2].id!=s.group)return stop(Stage::reobserve,e::Reason::illegal);
    auto body=fresh.actor;int t=0;if((!f.at_productive && !walk(body,fresh.groups[2].from_current,t,s.group)) || !renewal_certificate(fresh,body,o.remaining_rounds-t))return stop(Stage::reobserve,e::Reason::unsafe);
    if(f.at_productive){if(f.productive_group!=s.group || f.productive_round!=fresh.round || o.on_group_spawn)return stop(Stage::reobserve,e::Reason::illegal);p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::yield);}
    return move(s.waypoint);
}
} // namespace p5::runoff
