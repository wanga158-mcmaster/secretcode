#include "../runoff/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace p5;using namespace p5::runoff;namespace e=p5::execution;
static int checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"Runoff CHECK%d line%d: %s\n",checks,__LINE__,#x);std::exit(1);}}while(false)
static RouteLeg leg(int id,int d=1){return {id,d,true,true};}
static Group group(int id,int gems,double hp=10,double damage=0){
    Group g;g.id=id;g.gems=gems;g.current=g.available=g.no_other_holder=true;g.count=1;g.monsters[0]={100+id,hp,hp,damage,true};
    g.from_current=g.from_altar=g.from_spawn=leg(id);g.to_altar=leg(50);g.to_spawn=leg(60);g.certificate_return=leg(id);return g;
}
static Observation fixture(){
    Observation o;o.round=0;o.resources={10,true,true,true,{0,Knowledge::current,0},{0,Knowledge::current,0}};
    o.actual_marcella=o.bank_safe=o.spawn_safe=true;o.actor.id=10;o.actor.self=o.actor.visible=o.actor.enemy_cast_bound_known=true;
    o.actor.health=o.actor.max_health=100;o.actor.strength=10;o.groups={group(1,30),group(2,20),group(3,10)};
    o.groups[0].from_current=leg(1,0);o.A_to_B=leg(2);o.current_to_altar=leg(50);o.current_to_spawn=o.altar_to_spawn=leg(60);
    o.settings={0,10,1,200,1000,2};ScoreHistory h;CHECK(h.observe(0,0));o.windows=h.windows(0,200,1000,0);o.windows.window={17,17,17};
    auto& x=o.execution;x.owner_turn=0;x.remaining_rounds=200;x.fuel=100000;x.forecast_turns=30;x.observation_epoch=1;
    x.alive=x.safe=x.can_act=x.can_move=x.can_attack=x.can_cast=true;x.capture_duration=0;
    x.move_count=2;x.moves[0]={90,true,true,true,true,false};x.moves[1]={91,true,true,true,true,false};return o;
}
static Observation tick(Observation o,int round){const int delta=round-o.round;o.round=round;o.execution.owner_turn=round;o.execution.remaining_rounds=200-round;
    o.resources.bag.round=o.resources.gold.round=round;o.windows.official_remaining=200-round;
    for(auto& w:o.windows.window)w=std::max(0,w-delta);for(auto& d:o.windows.deadline)d=std::max(0,d-delta);return o;}
static Facts facts(int round=0,int goal=1){Facts f;f.navigation_round=round;f.navigation_goal=goal;return f;}
static Observation at_A(){auto o=fixture();auto& x=o.execution;x.at_target=x.group_live_known=x.target_visible=x.target_category_legal=x.target_in_range=true;
    x.group_id=1;x.attack_target=101;return o;}
static void economics(){
    const auto o=fixture();const auto a=choose_job(o),c=choose_job(o,{Arm::bank_each_control});
    std::printf("Runoff finite oracle Y=%lld/%lld, bank/return=%d/%d versus %d/%d; full serial B bank/return=%d/%d\n",
        (long long)a.selected.rank.Y[0],(long long)c.selected.rank.Y[0],a.selected.last_bank,a.selected.occupied,c.selected.last_bank,c.selected.occupied,
        quote(o,{Arm::bank_each_control},2).last_bank,quote(o,{Arm::bank_each_control},2).occupied);
    CHECK(a.feasible && c.feasible && a.job==Job::harvest && a.selected.prefix==2);
    CHECK(a.selected.rank.Y==(std::array<AccountingStock,3>{50,50,50}) && c.selected.rank.Y[0]==30);
    CHECK(a.selected.capture_tick[0]==4 && a.selected.capture_tick[1]==9 && a.selected.last_bank==17 && a.selected.occupied==20);
    CHECK(c.selected.last_bank==10 && c.selected.occupied==13 && quote(o,{Arm::bank_each_control},2).last_bank==20 && quote(o,{Arm::bank_each_control},2).occupied==23);
    CHECK(a.selected.rank.final_bag[0]==0 && a.selected.rank.final_gold[0]==100 && a.selected.rank.earned_gold==100 && same_actors(a.selected.rank,c.selected.rank));
    CHECK(a.selected.schedule.actor_count==1 && a.selected.schedule.event_count==5 && a.selected.count==4);
    auto t=o;t.settings.capture_rounds=t.execution.capture_duration=5;t.windows.window={27,27,27};
    const auto configured=choose_job(t),configured_c=choose_job(t,{Arm::bank_each_control});
    CHECK(configured.selected.rank.Y[0]==50 && configured_c.selected.rank.Y[0]==30);
    CHECK(configured.selected.capture_tick[0]==9 && configured.selected.capture_tick[1]==19 && configured.selected.last_bank==27 && configured.selected.occupied==30);
    t=o;t.windows.window={16,17,17};CHECK(choose_job(t).selected.prefix==1); // Expansion misses conservative deadline.
    t=o;t.groups[1].current=false;CHECK(choose_job(t).selected.prefix==1);
    t=o;t.groups[0].available=t.groups[1].available=false;CHECK(choose_job(t).job==Job::productive_fallback && choose_job(t).selected.rank.Y[0]==0);
    t=o;t.groups[2].current=false;CHECK(!choose_job(t).feasible);
    t=o;t.groups[2].id=1;CHECK(!choose_job(t).feasible);
    t=o;t.A_to_B.distance=-1;CHECK(choose_job(t).selected.prefix==1);
    t=o;t.groups[0].to_altar.destination=51;CHECK(!quote(t,{},1).feasible);
    t=o;t.groups[2].to_altar.destination=51;CHECK(!choose_job(t).feasible);
    t=o;t.resources.gold.knowledge=Knowledge::unknown;CHECK(!choose_job(t).feasible);
    t=o;t.actor.enemy_cast_bound_known=false;CHECK(!choose_job(t).feasible);
    t=o;t.ledger.captures=2;t.ledger.consumed=3;t.ledger.banks=2;CHECK(choose_job(t).selected.prefix==0);
    t.ledger.captures=1;CHECK(!choose_job(t).feasible); // Counts cannot reset independently of actual receipts.
    t=o;t.team_score=970;const auto win=choose_job(t);CHECK(win.selected.capture_tick[0]==4 && win.selected.capture_tick[1]<0 && win.selected.terminal && win.selected.last_bank==10);
    t=o;t.resources.bag.value=30;t.team_score=970;t.execution.adjacent_altar=t.execution.legal_winning_bank=true;t.execution.altar_id=50;t.groups[2].current=false;
    const auto immediate=choose_job(t),immediate_c=choose_job(t,{Arm::bank_each_control});
    CHECK(immediate.job==Job::winning_bank && immediate_c.job==Job::winning_bank && immediate.selected.last_bank==3 && immediate.selected.rank.immediate_win);
    auto p=start_project(immediate,facts());CHECK(execute_job(t,facts(),p).action.kind==e::ActionKind::start_tribute);
    t=o;t.execution.channel=e::Channel::tribute;t.execution.channel_valid=t.execution.channel_safe=true;t.groups[2].current=false;
    CHECK(choose_job(t).job==Job::preserve);
    t=o;t.groups[0].count=2;t.groups[0].monsters[1]=t.groups[0].monsters[0];CHECK(!quote(t,{},1).feasible);
    for(int bad=0;bad<5;++bad){t=o;auto& m=t.groups[0].monsters[0];if(bad==0)m.id=-1;if(bad==1)m.public_health=0;if(bad==2)m.observed_health=-1;
        if(bad==3)m.observed_health=11;if(bad==4)m.effective_strength=-1;CHECK(!choose_job(t).feasible);}
    t=o;t.groups[0].monsters[0].observed_health=0;CHECK(quote(t,{},1).feasible && quote(t,{},1).capture_tick[0]==3);
}
static Observation financed(bool health=false){auto o=at_A();o.groups[1]=group(2,20,90,10);o.windows.window={40,40,40};
    o.purchase_count=1;o.purchases[0]={7,0,30,1,health?0.:10.,health?100.:0.,health?100.:0.,1,true,true};return o;}
// Forecast identical events with the former shared capacities, including every
// result field used for candidate ranking or complete-cycle certification.
static AccountingSchedule expanded(const RunoffSchedule& s){
    AccountingSchedule out;std::copy(s.actors.begin(),s.actors.end(),out.actors.begin());out.actor_count=s.actor_count;
    std::copy(s.events.begin(),s.events.end(),out.events.begin());out.event_count=s.event_count;
    out.target_id=s.target_id;out.starting_team_score=s.starting_team_score;out.win_score=s.win_score;
    out.tribute_gems_per_tick=s.tribute_gems_per_tick;out.tribute_min_ticks=s.tribute_min_ticks;
    out.terminal_win=s.terminal_win;out.terminal_win_tick=s.terminal_win_tick;out.immediate_legal_winning_bank=s.immediate_legal_winning_bank;
    out.doctrine_id=s.doctrine_id;out.denial_evidenced=s.denial_evidenced;out.denied_gems=s.denied_gems;
    out.denied_bank_tick=s.denied_bank_tick;out.projected_kills=s.projected_kills;return out;
}
static void same_forecast(const AccountingResult& a,const AccountingResult& b){
    CHECK(a.status==b.status && std::strcmp(a.reason,b.reason)==0);
    CHECK(a.Y==b.Y && a.D==b.D && a.M==b.M);
    CHECK(a.final_bag==b.final_bag && a.final_gold==b.final_gold && a.actor_ids==b.actor_ids && a.actor_count==b.actor_count);
    CHECK(a.earned_gold==b.earned_gold && a.kills==b.kills && a.occupied_turns==b.occupied_turns && a.peak_bag==b.peak_bag);
    CHECK(a.productive_arrival==b.productive_arrival && a.target_id==b.target_id && a.windows==b.windows);
    CHECK(a.immediate_win==b.immediate_win && a.official_endpoint==b.official_endpoint);
    CHECK(same_actors(a,b) && compare_results(a,b)==0);
}
static void storage_forecast(const Quote& q,const FinishWindows& windows){
    const auto compact=forecast_schedule(q.schedule,windows);same_forecast(compact,forecast_schedule(expanded(q.schedule),windows));
    if(q.feasible)same_forecast(compact,q.rank);
}
static void storage_equivalence(){
    static_assert(RunoffSchedule{}.actors.size()==1 && RunoffSchedule{}.events.size()==16);
    static_assert(sizeof(RunoffSchedule)<sizeof(AccountingSchedule));
    auto base=fixture(),capital=financed();auto own_cash=capital;own_cash.resources.gold.value=30;
    auto bag=base;bag.resources.bag.value=20;auto winning=base;winning.team_score=970;
    auto immediate=winning;immediate.resources.bag.value=30;immediate.execution.adjacent_altar=immediate.execution.legal_winning_bank=true;immediate.execution.altar_id=50;
    auto consumed=base;consumed.ledger={2,1,1,3,true};
    for(const auto& o:{base,capital,own_cash,bag,winning,immediate,consumed})for(Arm arm:{Arm::finite_harvest,Arm::bank_each_control}){
        for(int prefix=0;prefix<=2;++prefix){storage_forecast(quote(o,{arm},prefix),o.windows);
            for(int pi=0;pi<o.purchase_count;++pi)for(int n=0;n<prefix;++n)storage_forecast(quote(o,{arm},prefix,pi,n),o.windows);}
        const auto d=choose_job(o,{arm});storage_forecast(d.selected,o.windows);storage_forecast(d.control,o.windows);
    }
    Quote full;full.schedule=initial(base);
    for(int i=0;i<event_cap;++i)CHECK(event(full,AccountingEventKind::return_to_work,i,0,0,base.groups[2].id));
    CHECK(full.schedule.event_count==event_cap && !event(full,AccountingEventKind::return_to_work,event_cap,0,0,base.groups[2].id));
    CHECK(full.schedule.event_count==event_cap);storage_forecast(full,base.windows);
    auto invalid=full;invalid.schedule.events[1].id=invalid.schedule.events[0].id;
    CHECK(forecast_schedule(invalid.schedule,base.windows).status==AccountingStatus::invalid);storage_forecast(invalid,base.windows);
}
static void capital(){
    auto o=financed();o.windows.window={28,28,28};const auto a=choose_job(o),c=choose_job(o,{Arm::bank_each_control});
    std::printf("Runoff financed control Y=%lld versus finite Y=%lld, control bank/return=%d/%d\n",(long long)c.selected.rank.Y[0],(long long)a.selected.rank.Y[0],c.selected.last_bank,c.selected.occupied);
    CHECK(a.feasible && c.feasible && a.selected.rank.Y[0]==30 && c.selected.rank.Y[0]==50 && c.selected.purchase==0 && c.selected.buy_before==1);
    CHECK(c.selected.last_bank==28 && c.selected.occupied==31 && c.selected.rank.final_gold[0]==70 && c.selected.rank.earned_gold==100);
    CHECK(!quote(o,{},2,0,1).feasible && !quote(o,{},2,0,0).feasible); // No advance spending of future tribute.
    o.resources.gold.value=30;const auto own_cash=choose_job(o);CHECK(own_cash.selected.prefix==2 && own_cash.selected.purchase==0 && own_cash.selected.rank.final_gold[0]==100);
    o=financed();o.purchases[0].strength=0;CHECK(choose_job(o,{Arm::bank_each_control}).selected.purchase<0); // Useless focus-like quote.
    o=financed();o.ledger.purchases=1;CHECK(choose_job(o,{Arm::bank_each_control}).selected.purchase<0);
    o=financed();o.purchases[0].no_new_income_passive=false;CHECK(choose_job(o,{Arm::bank_each_control}).selected.purchase<0);
}
static void controller(){
    auto o=at_A();auto f=facts();const auto d=choose_job(o);auto p=start_project(d,f);
    auto lost=p;auto no_bag=o;no_bag.resources.bag.value=1;CHECK(execute_job(no_bag,f,lost).action.kind==e::ActionKind::refresh && lost.stage==Stage::reobserve);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    auto stale_plan=p;auto short_window=o;short_window.windows.window={1,1,1};CHECK(execute_job(short_window,f,stale_plan).action.kind==e::ActionKind::refresh && stale_plan.stage==Stage::reobserve);
    o=tick(o,1);f.navigation_round=1;o.execution.group_cleared=o.execution.on_group_spawn=true;o.groups[0].monsters[0].observed_health=0;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::hold);
    o=tick(o,2);f.navigation_round=2;o.execution.own_payout_observed=true;o.execution.own_payout_group=1;o.execution.own_gems_received=30;
    o.execution.payout_receipt=f.capture_receipt=1;o.resources.bag.value=30;
    const auto first_payout=execute_job(o,f,p);
    std::printf("Runoff first payout action/stage/captures/prefix=%d/%d/%d/%d job=%d feasible=%d reason=%s\n",int(first_payout.action.kind),int(p.stage),p.ledger.captures,p.selected.selected.prefix,int(p.selected.job),p.selected.feasible,p.selected.selected.rank.reason);
    CHECK(first_payout.action.kind==e::ActionKind::refresh && p.ledger.captures==1 && p.selected.selected.prefix==1);
    auto deadline=p;auto late=o;late.windows.window={5,5,5};deadline.selected=choose_job([&]{auto copy=late;copy.ledger=deadline.ledger;return copy;}());deadline.initialized=false;
    CHECK(deadline.selected.job==Job::liquidate && deadline.selected.selected.prefix==0);
    f.navigation_goal=2;f.moves_leave_region=true;CHECK(execute_job(o,f,p).action.kind==e::ActionKind::vacate);
    o=tick(o,3);f.navigation_round=3;o.execution.on_group_spawn=o.execution.group_cleared=o.execution.own_payout_observed=false;o.execution.group_id=2;o.execution.attack_target=102;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    o=tick(o,4);f.navigation_round=4;o.execution.group_cleared=o.execution.on_group_spawn=true;o.groups[1].monsters[0].observed_health=0;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::hold);
    o=tick(o,5);f.navigation_round=5;o.execution.own_payout_observed=true;o.execution.own_payout_group=2;o.execution.own_gems_received=20;
    o.execution.payout_receipt=f.capture_receipt=2;o.resources.bag.value=50;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.ledger.captures==2 && p.ledger.consumed==3 && p.selected.selected.prefix==0);
    f.navigation_goal=50;CHECK(execute_job(o,f,p).action.kind==e::ActionKind::vacate);
    o=tick(o,6);f.navigation_round=6;o.execution.own_payout_observed=o.execution.on_group_spawn=false;o.execution.adjacent_altar=true;o.execution.altar_id=50;
    auto injured=p;auto hurt=o;hurt.actor.health=30;hurt.groups[2].monsters[0].effective_strength=25;
    CHECK(execute_job(hurt,f,injured).action.kind==e::ActionKind::refresh && injured.stage==Stage::reobserve);
    const auto bank=execute_job(o,f,p);CHECK(bank.action.kind==e::ActionKind::start_tribute && bank.action.whole_bag && bank.action.starting_bag==50 && bank.action.channel_ticks==5);
    auto missing=p;auto absent=tick(o,12);CHECK(execute_job(absent,f,missing).action.kind==e::ActionKind::refresh && missing.stage==Stage::reobserve);
    o.execution.channel=e::Channel::tribute;o.execution.channel_valid=o.execution.channel_safe=true;o.execution.fuel=1;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::preserve_channel);
    o=tick(o,11);o.execution.channel=e::Channel::none;o.execution.fuel=100000;o.resources.bag.value=0;o.resources.gold.value=100;o.team_score=50;
    f.navigation_round=11;f.navigation_goal=3;f.bank_receipt=1;f.bank_confirmed=true;f.banked_gems=50;f.bank_gold=100;f.bank_altar=50;
    auto wrong=p;auto bad=f;bad.bank_gold=99;CHECK(execute_job(o,bad,wrong).action.kind==e::ActionKind::refresh && wrong.stage==Stage::reobserve);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.ledger.banks==1 && p.ledger.captures==2 && p.ledger.liquidated);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::move);
    o=tick(o,12);f.navigation_round=12;f.at_productive=true;f.productive_round=12;f.productive_group=3;o.execution.adjacent_altar=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.stage==Stage::complete);
}
static void purchases(){for(bool health:{false,true}){
    auto o=financed(health);auto f=facts();auto p=start_project(choose_job(o,{Arm::bank_each_control}),f);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    o=tick(o,1);f.navigation_round=1;o.execution.group_cleared=o.execution.on_group_spawn=true;o.groups[0].monsters[0].observed_health=0;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::hold);
    o=tick(o,2);f.navigation_round=2;o.resources.bag.value=30;o.execution.own_payout_observed=true;o.execution.own_payout_group=1;
    o.execution.own_gems_received=30;o.execution.payout_receipt=f.capture_receipt=1;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.ledger.captures==1);
    f.navigation_goal=50;f.moves_leave_region=true;CHECK(execute_job(o,f,p).action.kind==e::ActionKind::vacate);
    o=tick(o,3);f.navigation_round=3;o.execution.own_payout_observed=o.execution.on_group_spawn=false;o.execution.adjacent_altar=true;o.execution.altar_id=50;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute);
    o=tick(o,6);f.navigation_round=6;o.resources.bag.value=0;o.resources.gold.value=60;o.team_score=30;
    f.bank_confirmed=true;f.bank_receipt=1;f.bank_gold=60;f.banked_gems=30;f.bank_altar=50;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.ledger.banks==1 && p.selected.selected.purchase==0);
    f.navigation_goal=60;f.gear_item=7;f.shard_equivalent_count=4;CHECK(execute_job(o,f,p).action.kind==e::ActionKind::move);
    o=tick(o,7);f.navigation_round=7;o.execution.at_spawn=true;o.execution.adjacent_altar=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield); // Arrival alone is no healing receipt.
    o=tick(o,8);f.navigation_round=8;f.recovery_receipt=1;f.recovery_round=8;o.execution.spawn_recovery_observed=true;
    const auto buy=execute_job(o,f,p);CHECK(buy.action.kind==e::ActionKind::buy && buy.action.target==7);
    auto missing=p;auto no_receipt=tick(o,9);CHECK(execute_job(no_receipt,f,missing).action.kind==e::ActionKind::refresh && missing.stage==Stage::reobserve);
    o.resources.gold.value=30;o.actor.strength=health?10:20;o.actor.max_health=o.actor.health=health?200:100;
    f.purchase_receipt=1;f.purchase_confirmed=true;f.purchased_item=7;f.purchased_family=0;f.purchased_cost=30;f.purchased_shards=1;f.shard_equivalent_count=5;
    for(int bad_case=0;bad_case<6;++bad_case){auto trial=p;auto bf=f;auto b=o;
        if(bad_case==0)bf.purchased_item=8;if(bad_case==1)bf.purchased_shards=2;if(bad_case==2)bf.shard_equivalent_count=0;
        if(bad_case==3)b.actor.health=health?50:30;if(bad_case==4)b.resources.gold.value=60;if(bad_case==5)b.groups[2].current=false;
        CHECK(execute_job(b,bf,trial).action.kind==e::ActionKind::refresh && trial.stage==Stage::reobserve);}
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.ledger.purchases==1 && p.selected.selected.purchase<0);
    f.navigation_goal=2;CHECK(execute_job(o,f,p).action.kind==e::ActionKind::move);
}}
static void movement(){auto o=fixture();auto f=facts();auto p=start_project(choose_job(o),f);
    const auto first=execute_job(o,f,p);CHECK(first.action.kind==e::ActionKind::move && first.action.target==90);
    o.execution.feedback_sequence=first.action.sequence;o.execution.feedback_kind=e::ActionKind::move;o.execution.feedback_target=90;o.execution.feedback_success=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh);
    ++o.execution.observation_epoch;const auto next=execute_job(o,f,p);CHECK(next.action.kind==e::ActionKind::move && next.action.target==91);
}
static void draft(){for(int size:{3,4,5}){DraftContext c;c.team_size=size;c.pick_index=0;c.pool_count=12;c.independent_cells=6;
    for(int i=0;i<12;++i)c.pool[i]={draft_aliases[i],true,draft_actor_count(draft_aliases[i]),1,100,0,0,1};
    CHECK(choose_draft(c,Profile::draft_request(c)).alias=="marcella");c.own_picks[0]="marcella";c.own_count=c.pick_index=1;
    for(int i=0;i<12;++i)if(draft_aliases[i]!="marcella"){auto t=c;t.pool[i].productivity=100000;CHECK(choose_draft(t,Profile::draft_request(t)).alias==draft_aliases[i]);}}}
int main(){economics();storage_equivalence();capital();controller();purchases();movement();draft();std::printf("Runoff checks passed: %d (bounded two-segment core; native integration open)\n",checks);}
