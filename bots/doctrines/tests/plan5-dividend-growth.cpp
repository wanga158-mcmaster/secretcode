// Independent economic/controller cases. The reusable provider oracle still
// compares schedules against the unchanged general combat kernel.
#define main dividend_provider_checks
#include "plan5-dividend-provider.cpp"
#undef main

static Observation cash_case(int bag=0) {
    auto in=base();in.self.gold.value=32;in.self.bag.value=bag;
    in.current_to_spawn=route(60);in.current_to_altar=route(50,2);
    in.altar_to_spawn=route(60,3);in.bank_return={};in.count=1;
    in.purchase_count=1;in.purchases[0]={0,1,30,20,0,0,0,true};
    auto& g=in.opportunities[0];
    g.gems=10;g.gold=0;g.combat.monsters[0]={1,180,180,5,true};
    g.from_current=route(1,9);g.from_spawn=route(1,1);g.to_altar=route(50,2);
    // The unmodified actor cannot clear the first group safely. The exact
    // purchased kit has a separately supplied, synthetic safe return.
    g.variant_returns_known=true;g.variant_returns[0]={};g.variant_returns[1]=route(2,1);
    return in;
}
static Decision cash_choice(const Observation& in,Arm arm=Arm::active) {
    return choose_job(in,{},Profile{arm});
}
static ProjectFacts initial_cash_facts(const Observation& in) {
    ProjectFacts f;f.observation_round=0;f.own_gold=32;f.own_bag=int(in.self.bag.value);
    f.own_bank_receipt=4;f.own_capture_receipt=8;f.own_purchase_receipt=12;
    f.own_stats=in.stats;f.own_stats_known=true;return f;
}
static execution::Observation point(int round=0) {
    execution::Observation o;o.owner_turn=round;o.remaining_rounds=200;o.forecast_turns=40;
    o.fuel=100000;o.alive=o.safe=o.can_act=o.can_move=o.can_attack=true;
    o.at_spawn=true;o.purchase_requested=o.purchase_purchasable=o.purchase_affordable=true;
    o.purchase_id=0;o.altar_id=50;o.tribute_ticks_quote=2;
    o.group_id=1;o.group_live_known=o.at_target=o.target_visible=true;
    o.target_category_legal=o.target_in_range=true;o.attack_target=90;
    o.group_count=1;o.groups[0]={1,0,true,true,true,false,false,true};return o;
}
static ProjectFacts bought(const ProjectFacts& before,const Observation& in) {
    auto f=before;f.observation_round=1;f.own_gold=2;f.own_purchase_receipt=13;
    f.own_purchase_confirmed=true;f.purchased_family=0;f.purchased_count=1;f.purchased_cost=30;
    f.own_stats=in.stats;f.own_stats.strength+=20;f.own_stats_known=true;return f;
}
static void cash_controller() {
    namespace e=execution;
    const auto in=cash_case(7);const auto selected=cash_choice(in);
    CHECK(selected.feasible && selected.job==Job::cash_produce);
    auto f=initial_cash_facts(in);auto p=start_project(selected,f);auto o=point();o.own_bag=7;
    CHECK(p.stage==ProjectStage::purchase_first);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy);
    CHECK(p.purchase_intent_outstanding && !p.bank_intent_outstanding);
    const auto outstanding=p;
    const auto valid=bought(f,in);
    for(int bad=0;bad<10;++bad) {
        auto trial=outstanding;auto facts=valid;auto now=point(1);now.own_bag=7;
        switch(bad) {
        case 0:facts.own_purchase_receipt=12;break;
        case 1:facts.purchased_family=1;break;
        case 2:facts.purchased_count=5;break;
        case 3:facts.purchased_cost=29;break;
        case 4:facts.own_gold=3;break;
        case 5:facts.own_bag=0;break;
        case 6:facts.own_stats_known=false;break;
        case 7:facts.own_stats.strength=10;break;
        case 8:facts.own_stats.max_health=90;break;
        default:facts.own_stats.health=1;break;
        }
        const auto refused=execute_job(now,facts,trial);
        CHECK(refused.action.kind!=e::ActionKind::attack && refused.action.kind!=e::ActionKind::buy);
        CHECK(trial.stage!=ProjectStage::production);
    }
    f=valid;o=point(1);o.own_bag=7;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh);
    CHECK(p.stage==ProjectStage::purchase_inspection && !p.purchase_intent_outstanding);
    f.observation_round=2;f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=true;
    o=point(2);o.at_spawn=false;o.own_bag=7;o.observation_epoch=1;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::production);
    o.observation_epoch=2;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    // Forecasted capture time is not a receipt. Only actual payout advances.
    o=point(4);o.at_spawn=false;o.own_bag=7;o.on_group_spawn=o.group_cleared=true;
    f.observation_round=4;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::hold);
    o=point(7);o.at_spawn=false;o.own_bag=17;o.on_group_spawn=o.group_respawning=true;
    o.own_payout_observed=true;o.own_payout_group=1;o.own_gems_received=10;o.payout_receipt=9;
    o.fuel=0;f.observation_round=7;f.own_bag=17;f.own_capture_receipt=9;
    (void)execute_job(o,f,p);CHECK(p.stage==ProjectStage::output_bank);
    o=point(8);o.at_spawn=false;o.own_bag=17;o.adjacent_altar=true;
    o.bank_requested=true;o.purchase_requested=false;f.observation_round=8;
    const auto bank=execute_job(o,f,p);
    CHECK(bank.action.kind==e::ActionKind::start_tribute && bank.action.whole_bag);
    CHECK(bank.action.starting_bag==17 && p.bank_intent_outstanding);
    o=point(11);o.at_spawn=false;o.own_bag=0;o.purchase_requested=false;
    f.observation_round=11;f.own_bag=0;f.own_gold=36;f.own_bank_receipt=5;
    (void)execute_job(o,f,p);CHECK(p.stage==ProjectStage::renewal);
    f.observation_round=12;f.at_named_productive_waypoint=true;f.productive_waypoint=2;
    o=point(12);o.at_spawn=false;o.own_bag=0;
    (void)execute_job(o,f,p);CHECK(p.stage==ProjectStage::complete);
}
static Project inspected_purchase(const Observation& in,ProjectFacts& f) {
    namespace e=execution;
    f=initial_cash_facts(in);auto p=start_project(cash_choice(in),f);
    auto o=point();CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy);
    f=bought(f,in);o=point(1);
    // Actual542-style shop arrival: the kit/debit are known, all camps are
    // unknown. Even malicious source flags cannot make this packet attack.
    o.group_count=0;o.group_live_known=o.at_target=o.target_visible=false;
    f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=true;
    const auto confirmed=execute_job(o,f,p);
    CHECK(confirmed.action.kind==e::ActionKind::refresh);
    CHECK(p.stage==ProjectStage::purchase_inspection && p.execution_state.target==1);
    CHECK(!p.purchase_intent_outstanding && p.purchase_baseline==13);
    CHECK(f.own_gold==2 && f.own_bag==0 && p.selected.schedule.event_count==5);
    f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=false;
    return p;
}
static void purchase_inspection_checks() {
    namespace e=execution;
    const auto in=cash_case();ProjectFacts f;
    auto p=inspected_purchase(in,f);auto o=point(2);f.observation_round=2;
    o.at_spawn=false;o.group_count=0;o.group_live_known=o.at_target=false;
    o.move_count=1;o.moves[0]={201,true,true,true,true,false};o.observation_epoch=10;
    const auto walking=execute_job(o,f,p);
    CHECK(walking.action.kind==e::ActionKind::move && walking.action.target==201);
    CHECK(p.stage==ProjectStage::purchase_inspection && !p.bank_intent_outstanding && !p.purchase_intent_outstanding);
    // A failed move is consumed once. Same-epoch hidden occupancy refreshes;
    // a fresh packet permits another safe destination instead of an attack.
    o.feedback_sequence=walking.action.sequence;o.feedback_kind=e::ActionKind::move;
    o.feedback_target=201;o.feedback_success=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.execution_state.failed_moves==1);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.execution_state.failed_moves==1);
    ++o.observation_epoch;o.move_count=2;o.moves[1]={202,true,true,true,true,false};
    const auto alternative=execute_job(o,f,p);
    CHECK(alternative.action.kind==e::ActionKind::move && alternative.action.target==202);
    // Fresh target proof admits only a rebound production observation.
    f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=true;
    o=point(3);f.observation_round=3;o.at_spawn=false;o.observation_epoch=20;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::production);
    ++o.observation_epoch;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    for(int bad=0;bad<10;++bad) {
        auto trial=inspected_purchase(in,f);o=point(2);f.observation_round=2;
        f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=true;
        switch(bad) {
        case 0:o.alive=false;break;
        case 1:o.safe=false;f.own_stats.health=1;break;
        case 2:o.needs_recovery=true;break;
        case 3:o.remaining_rounds=0;break;
        case 4:o.owner_turn=f.observation_round=trial.execution_state.phase_start+trial.execution_state.phase_budget;break;
        case 5:f.purchase_tail_safe=false;break;
        case 6:f.purchase_source_reached=false;break;
        case 7:o.group_id=999;break;
        case 8:o.at_target=false;break;
        default:o.safe=false;f.own_stats.health=30;break;
        }
        const auto stopped=execute_job(o,f,trial);
        CHECK(trial.stage==ProjectStage::aborted && stopped.action.kind==e::ActionKind::yield);
        CHECK(!trial.purchase_intent_outstanding && trial.purchase_baseline==13 && f.own_gold==2);
    }
    p=inspected_purchase(in,f);o=point(2);f.observation_round=2;
    f.purchase_source_reached=f.purchase_tail_checked=true;f.purchase_tail_safe=false;o.fuel=o.selection_reserve-1;
    const auto starved=execute_job(o,f,p);
    CHECK(starved.action.kind==e::ActionKind::yield && starved.reason==e::Reason::low_fuel);
    CHECK(p.stage==ProjectStage::purchase_inspection);
    o.at_target=false;f.purchase_source_reached=false;o.fuel=o.action_reserve-1;
    CHECK(execute_job(o,f,p).reason==e::Reason::low_fuel && p.stage==ProjectStage::purchase_inspection);
    // A stale facts packet never authorizes source admission.
    o=point(3);f.purchase_source_reached=f.purchase_tail_safe=f.purchase_tail_checked=true;
    CHECK(execute_job(o,f,p).reason==e::Reason::illegal && p.stage==ProjectStage::purchase_inspection);
    // An existing 50000-fuel packet can still lack a completed quote. Neither
    // that budget failure nor unknown members may become a safety verdict.
    p=inspected_purchase(in,f);o=point(2);f.observation_round=2;
    const int inspection_start=p.execution_state.phase_start;
    f.purchase_source_reached=true;o.fuel=50000;o.group_count=0;o.group_live_known=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::purchase_inspection);
    CHECK(p.execution_state.phase_start==inspection_start && !p.purchase_intent_outstanding);
    o=point(3);f.observation_round=3;f.purchase_tail_checked=f.purchase_tail_safe=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::production);
    p=inspected_purchase(in,f);o=point(2);f.observation_round=2;
    f.purchase_source_reached=true;o.group_count=0;o.group_live_known=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::purchase_inspection);
    o.owner_turn=f.observation_round=p.execution_state.phase_start+p.execution_state.phase_budget;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::aborted);
    // An unknown or wrong target without a reached certificate remains travel.
    p=inspected_purchase(in,f);o=point(2);f.observation_round=2;
    o.at_target=false;o.group_id=999;o.group_count=0;o.group_live_known=false;
    o.move_count=1;o.moves[0]={203,true,true,true,true,false};
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::move && p.stage==ProjectStage::purchase_inspection);
    // Valid channels precede even an expired phase/official deadline. Once the
    // channel is gone the unchanged inspection deadline terminates the stage.
    const int expiry=p.execution_state.phase_start+p.execution_state.phase_budget;
    o=point(expiry);f.observation_round=expiry;o.remaining_rounds=0;o.fuel=0;
    o.channel=e::Channel::tribute;o.channel_valid=o.channel_safe=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::preserve_channel && p.stage==ProjectStage::purchase_inspection);
    o.channel=e::Channel::none;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::aborted);
}
static void finance_purchase_inspection() {
    namespace e=execution;
    auto in=cash_case(6);in.self.gold.value=20;const auto selected=cash_choice(in);
    CHECK(selected.job==Job::finance_produce && selected.schedule.event_count==7);
    auto f=initial_cash_facts(in);f.own_gold=20;auto p=start_project(selected,f);
    auto o=point();o.at_spawn=false;o.adjacent_altar=true;o.own_bag=6;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute);
    o=point(1);f.observation_round=1;f.own_bag=0;f.own_gold=32;f.own_bank_receipt=5;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy && p.stage==ProjectStage::purchase);
    f=bought(f,in);f.observation_round=2;o=point(2);o.group_count=0;o.at_target=o.group_live_known=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh);
    CHECK(p.stage==ProjectStage::purchase_inspection && !p.purchase_intent_outstanding && p.bank_baseline==5);
}
int main() {
    CHECK(dividend_provider_checks()==0);
    for(int bag:{0,7}) {
        auto in=cash_case(bag);const auto active=cash_choice(in),control=cash_choice(in,Arm::route_first_control);
        CHECK(active.feasible && active.job==Job::cash_produce && active.purchase_family==0);
        same(active,control);compare(in);
        CHECK(active.schedule.events[0].kind==AccountingEventKind::purchase);
        CHECK(active.schedule.events[0].gold==30 && active.schedule.actors[0].gold==32);
        int captures=0,banks=0,returns=0;bool captured=false;
        for(int i=0;i<active.schedule.event_count;++i) {
            const auto& event=active.schedule.events[i];
            if(event.kind==AccountingEventKind::capture){++captures;captured=true;}
            if(event.kind==AccountingEventKind::tribute_start)CHECK(captured);
            if(event.kind==AccountingEventKind::tribute_complete){++banks;CHECK(event.gems==-1);}
            if(event.kind==AccountingEventKind::return_to_work){++returns;CHECK(event.waypoint==2);}
        }
        CHECK(captures==1 && banks==1 && returns==1);
        CHECK(active.rank.Y[0]==10+bag && active.rank.final_gold[0]==2+2*(10+bag));
        auto unsafe=in;unsafe.opportunities[0].variant_returns[1]={};CHECK(!cash_choice(unsafe).feasible);
        auto unaffordable=in;unaffordable.self.gold.value=29;unaffordable.self.bag.value=0;
        CHECK(cash_choice(unaffordable).job!=Job::cash_produce);
        auto unknown=in;unknown.self.gold.knowledge=Knowledge::stale;CHECK(!cash_choice(unknown).feasible);
        auto illegal=in;illegal.current_to_spawn.safety_checked=false;CHECK(!cash_choice(illegal).feasible);
        auto late=in;late.settings.round_limit=5;ScoreHistory h;CHECK(h.observe(0,0));
        late.windows=h.windows(0,5,1000,0);CHECK(!cash_choice(late).feasible);
        Oracle oracle;oracle.hold=in.settings.capture_rounds+1;oracle.fail_at=1;
        CombatQuoteProvider provider{&oracle,Oracle::quote,true};oracle.provider=&provider;
        CHECK(!choose_job(in,{},Profile{Arm::active},&provider).feasible && !provider.complete);
    }
    auto financing=cash_case(6);financing.self.gold.value=20;
    CHECK(cash_choice(financing).job==Job::finance_produce);
    CHECK(cash_choice(financing,Arm::route_first_control).job!=Job::cash_produce);
    cash_controller();
    purchase_inspection_checks();
    finance_purchase_inspection();
    std::printf("Dividend ordinary-cash/controller checks passed: %d (synthetic, no native claim)\n",checks);
    return 0;
}
