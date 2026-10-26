#include "../collateral/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace p5;
using namespace p5::collateral;
namespace e=p5::execution;
static int checks;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);std::exit(1);} } while(false)
static RouteLeg leg(int id,int n=0){return {id,n,true,true};}
static Group camp(int id,int gems,double hp,double strength){
    Group g;g.id=id;g.gems=gems;g.current=g.available=g.no_other_holder=true;
    g.count=1;g.monsters[0]={100+id,hp,hp,strength,true};return g;
}
static Observation fixture(){
    Observation o;o.round=0;o.resources={10,true,true,true,{20,Knowledge::current,0},{0,Knowledge::current,0}};
    auto& a=o.actor;a.id=10;a.self=a.visible=a.enemy_cast_bound_known=true;a.health=300;a.max_health=1000;a.strength=40;
    o.raw_modifier_excludes_passive=o.naturally_injured=true;o.focus=100;
    o.target=camp(1,30,150,40);o.renewal=camp(2,10,40,0);
    o.current_to_group=o.spawn_to_group=leg(1);o.current_to_altar=o.group_to_altar=o.renewal_to_altar=leg(50);
    o.safe_exit=leg(60);o.altar_to_spawn=leg(60);o.spawn_to_renewal=leg(2);o.renewal_return=leg(70);
    o.bank_legal=o.safe_initial_bank=o.recall_safe=true;o.recall_ticks=5;o.spawn_rest=1;o.spawn_heal_per_tick=1000;
    o.settings={0,10,1,200,1000,2};ScoreHistory h;CHECK(h.observe(0,0));o.windows=h.windows(0,200,1000,0);o.windows.window={20,20,20};
    auto& x=o.execution;x.owner_turn=0;x.remaining_rounds=200;x.fuel=100000;x.forecast_turns=24;x.capture_duration=0;
    x.alive=x.safe=x.can_act=x.can_move=x.can_attack=x.can_cast=true;
    o.shield={0,5,1.5,{10,true,true,true,true,true}};x.spells[0]=o.shield.legal;return o;
}
static Facts facts(const Observation& o){
    Facts f;f.round=o.round;f.own_actor=o.actor.id;f.own_bag=o.resources.bag.value;f.own_gold=o.resources.gold.value;
    f.own_stats=o.actor;f.own_focus=o.focus;f.raw_modifier_excludes_passive=true;
    f.current_target=o.target;f.current_renewal=o.renewal;f.group_round=f.route_safety_round=o.round;
    f.navigation_goal=1;f.navigation_round=o.round;return f;
}
static e::Observation runtime(const Observation& o){
    auto x=o.execution;x.at_target=x.group_live_known=x.target_visible=x.target_category_legal=x.target_in_range=true;
    x.group_id=1;x.attack_target=101;x.move_count=1;x.moves[0]={99,true,true,true,true,false};x.observation_epoch=1;
    return x;
}
static void clock(Facts& f,e::Observation& o,int turn){
    f.round=f.group_round=f.route_safety_round=f.navigation_round=turn;o.owner_turn=turn;o.remaining_rounds=200-turn;
}
static void economics(){
    const auto o=fixture();const auto a=choose_job(o),c=choose_job(o,{Arm::recovery_control});
    const auto active_quote=quote(o,true),control_quote=quote(o,false);
    std::printf("Collateral oracle: active feasible=%d reason=%s capture/bank/return=%d/%d/%d H=%.0f; control feasible=%d reason=%s capture/bank/return=%d/%d/%d H=%.0f\n",
        active_quote.feasible,active_quote.rank.reason,active_quote.capture_tick,active_quote.bank_tick,active_quote.occupied,active_quote.after_clear_health,
        control_quote.feasible,control_quote.rank.reason,control_quote.capture_tick,control_quote.bank_tick,control_quote.occupied,control_quote.after_clear_health);
    CHECK(a.feasible && c.feasible && a.job==Job::shield_continue && !c.selected.shielded);
    CHECK(a.selected.rank.Y==(std::array<AccountingStock,3>{50,50,50}) && c.selected.rank.Y[0]==20);
    CHECK(a.selected.capture_tick==7 && a.selected.bank_tick==14 && a.selected.occupied==24);
    CHECK(c.selected.capture_tick==19 && c.selected.bank_tick==24 && c.selected.occupied==34);
    CHECK(a.selected.after_clear_health==290 && a.selected.final_health==1000);
    CHECK(a.selected.rank.final_bag[0]==0 && a.selected.rank.final_gold[0]==0 && a.selected.schedule.actor_count==1);
    CHECK(a.selected.count==4 && c.selected.count==6 && a.selected.schedule.event_count<=event_cap && c.selected.schedule.event_count<=event_cap);
    int captures=0,banks=0,buys=0;
    for(int i=0;i<c.selected.schedule.event_count;++i){const auto k=c.selected.schedule.events[i].kind;
        captures+=k==AccountingEventKind::capture;banks+=k==AccountingEventKind::tribute_start;buys+=k==AccountingEventKind::purchase;
        if(k==AccountingEventKind::tribute_start || k==AccountingEventKind::tribute_complete)CHECK(c.selected.schedule.events[i].gems==-1);}
    CHECK(captures==1 && banks==2 && buys==0);
    auto t=o;t.windows.window={40,40,40};CHECK(choose_job(t).selected.rank.Y[0]==50 && choose_job(t,{Arm::recovery_control}).selected.rank.Y[0]==50);
    t=o;t.shield.duration=1;CHECK(!quote(t,true).feasible && choose_job(t).selected.rank.Y[0]==20);
    t=o;t.target.monsters[0].effective_strength=100;CHECK(!quote(t,true).feasible && quote(t,false).feasible);
    t=o;t.safe_exit.distance=-1;CHECK(!quote(t,true).feasible && choose_job(t).job==Job::recover);
    t=o;t.safe_exit.distance=3;CHECK(!quote(t,true).feasible && !choose_job(t).selected.shielded);
    t=o;t.naturally_injured=false;CHECK(!quote(t,true).feasible && !choose_job(t).selected.shielded);
    t=o;t.must_recover=true;CHECK(!quote(t,true).feasible && !choose_job(t).selected.shielded);
    t=o;t.raw_modifier_excludes_passive=false;CHECK(!choose_job(t).feasible);
    t=o;t.resources.bag.value=0;t.actor.health=1000;t.naturally_injured=false;
    const auto zero=choose_job(t);CHECK(zero.feasible && zero.job==Job::bootstrap && !zero.selected.shielded && zero.selected.rank.Y[0]==30);
    t=o;t.safe_initial_bank=false;CHECK(quote(t,false).feasible && quote(t,false).count==5 && quote(t,false).rank.Y[0]==0);
    t=o;t.execution.channel=e::Channel::tribute;t.execution.channel_valid=t.execution.channel_safe=true;t.target.current=false;
    CHECK(choose_job(t).job==Job::preserve);
    t=o;t.team_score=980;t.target.current=false;t.bank_legal=true;t.execution.adjacent_altar=t.execution.legal_winning_bank=true;
    t.execution.altar_id=50;const auto win=choose_job(t);CHECK(win.job==Job::winning_bank);
    auto wf=facts(t);auto wp=start_project(win,wf);CHECK(execute_job(t.execution,wf,wp).action.kind==e::ActionKind::start_tribute);
}
static void physical_bounds(){
    auto o=fixture();auto a=o.actor;a.shield=100;a.shield_expiry_tick=2;
    CHECK(expose(a,3,30) && a.health==270 && a.shield==0 && a.shield_expiry_tick==0);
    a=o.actor;a.shield=100;a.shield_expiry_tick=1;CHECK(!expose(a,3,60) && a.health==180);
    a=o.actor;int t=0;CHECK(!clear(o.target,a,t)); // reserve rejects this unshielded fight.
    a=o.actor;a.health=1000;t=0;CHECK(clear(o.target,a,t) && t==4 && a.health==840); // healed HP removes missing-health bonus.
    a=o.actor;a.basic_modifier=2;a.shield=1000;a.shield_expiry_tick=5;t=0;CHECK(clear(o.target,a,t) && t==2); // raw modifier applied once.
    for(int bad=0;bad<8;++bad){auto g=o.target;a=o.actor;a.shield=1000;a.shield_expiry_tick=5;t=0;
        if(bad==0)g.monsters[0].id=-1;if(bad==1)g.monsters[0].public_health=0;
        if(bad==2)g.monsters[0].observed_health=-1;if(bad==3)g.monsters[0].observed_health=151;
        if(bad==4)g.monsters[0].effective_strength=-1;if(bad==5){g.count=2;g.monsters[1]=g.monsters[0];}
        if(bad==6)g.monsters[0].public_health=std::numeric_limits<double>::quiet_NaN();if(bad==7)g.count=9;
        CHECK(!clear(g,a,t));}
    auto g=o.target;g.monsters[0].observed_health=0;a=o.actor;t=0;CHECK(clear(g,a,t) && t==0 && a.health==300);
    g=o.target;g.count=2;g.monsters[1]={102,10,0,10000,true};a=o.actor;a.shield=150;a.shield_expiry_tick=5;t=0;
    CHECK(clear(g,a,t) && t==3 && a.health==300);
    o.renewal.id=o.target.id;CHECK(!choose_job(o).feasible);
}
static void controller(){
    const auto input=fixture();const auto d=choose_job(input);auto f=facts(input);auto x=runtime(input);auto p=start_project(d,f);
    CHECK(p.stage==Stage::running);
    auto lost=p;auto bad=f;bad.own_bag=0;CHECK(execute_job(x,bad,lost).action.kind==e::ActionKind::refresh && lost.stage==Stage::reobserve);
    const auto cast=execute_job(x,f,p);CHECK(cast.action.kind==e::ActionKind::cast && cast.action.slot==0 && cast.action.target==10);
    auto pending=p;
    CHECK(execute_job(x,f,p).action.kind==e::ActionKind::refresh && p.stage==Stage::running);
    auto missing=p;auto mx=x;auto mf=f;clock(mf,mx,1);CHECK(execute_job(mx,mf,missing).action.kind==e::ActionKind::refresh && missing.stage==Stage::reobserve);
    x.feedback_sequence=cast.action.sequence;x.feedback_kind=e::ActionKind::cast;x.feedback_success=true;x.feedback_target=10;
    f.shield_receipt=1;f.shield_cast_sequence=cast.action.sequence;f.buff_round=0;f.own_stats.shield=150;f.own_stats.shield_expiry_tick=5;
    for(int fail=0;fail<7;++fail){auto q=pending;auto fx=f;auto ox=x;
        if(fail==0)fx.shield_receipt=0;if(fail==1)fx.shield_cast_sequence++;
        if(fail==2)fx.buff_round=-1;if(fail==3)fx.own_stats.shield=0;if(fail==4)fx.own_stats.shield_expiry_tick=0;
        if(fail==5)ox.feedback_target=11;if(fail==6)fx.current_target.monsters[0].effective_strength=1000;
        CHECK(execute_job(ox,fx,q).action.kind==e::ActionKind::refresh && q.stage==Stage::reobserve);}
    p=pending;CHECK(execute_job(x,f,p).action.kind==e::ActionKind::attack && p.shield_done);
    clock(f,x,3);x.feedback_sequence=0;f.current_target.monsters[0].observed_health=0;x.group_cleared=x.on_group_spawn=true;
    CHECK(execute_job(x,f,p).action.kind==e::ActionKind::hold);
    auto held=p;auto held_f=f;held_f.group_round=-1;CHECK(execute_job(x,held_f,held).action.kind==e::ActionKind::hold);
    clock(f,x,7);x.own_payout_observed=true;x.own_payout_group=1;x.own_gems_received=30;x.payout_receipt=f.capture_receipt=1;
    f.own_bag=50;x.adjacent_altar=true;x.altar_id=50;f.navigation_goal=50;
    const auto tribute=execute_job(x,f,p);CHECK(tribute.action.kind==e::ActionKind::start_tribute && tribute.action.whole_bag && tribute.action.starting_bag==50 && tribute.action.channel_ticks==5);
    x.channel=e::Channel::tribute;x.channel_valid=x.channel_safe=true;CHECK(execute_job(x,f,p).action.kind==e::ActionKind::preserve_channel);
    x.channel=e::Channel::none;x.own_payout_observed=false;x.on_group_spawn=false;clock(f,x,12);f.own_bag=0;f.bank_receipt=1;x.can_recall=true;f.navigation_goal=60;
    CHECK(execute_job(x,f,p).action.kind==e::ActionKind::start_recall);
    clock(f,x,18);x.at_spawn=x.spawn_recovery_observed=true;f.own_stats.health=500;f.own_stats.shield=0;f.own_stats.shield_expiry_tick=0;
    CHECK(execute_job(x,f,p).action.kind==e::ActionKind::yield);
    clock(f,x,19);f.recovery_receipt=1;f.recovery_round=19;f.own_stats.health=1000;f.navigation_goal=2;
    auto wrong=p;auto wrong_f=f;wrong_f.navigation_goal=50;CHECK(execute_job(x,wrong_f,wrong).action.kind==e::ActionKind::refresh);
    CHECK(execute_job(x,f,p).action.kind==e::ActionKind::move);
    f.at_named_productive=true;f.productive_group=2;x.at_spawn=false;CHECK(execute_job(x,f,p).action.kind==e::ActionKind::yield && p.stage==Stage::complete);
    auto stale=start_project(d,facts(input));auto stale_f=facts(input);stale_f.round=-1;
    CHECK(execute_job(runtime(input),stale_f,stale).action.kind==e::ActionKind::refresh && stale.stage==Stage::aborted);
}
static void draft(){
    for(int size:{3,4,5}){DraftContext c;c.team_size=size;c.pick_index=0;c.pool_count=12;c.independent_cells=6;
        for(int i=0;i<12;++i)c.pool[i]={draft_aliases[i],true,draft_actor_count(draft_aliases[i]),1,100,0,0,1};
        CHECK(choose_draft(c,Profile::draft_request(c)).alias=="heart_of_ruin");
        c.own_picks[0]="heart_of_ruin";c.own_count=c.pick_index=1;
        for(int i=0;i<12;++i)if(draft_aliases[i]!="heart_of_ruin"){auto t=c;t.pool[i].productivity=100000;
            CHECK(choose_draft(t,Profile::draft_request(t)).alias==draft_aliases[i]);}
    }
}
int main(){economics();physical_bounds();controller();draft();std::printf("Collateral checks passed: %d (synthetic, no native games)\n",checks);}
