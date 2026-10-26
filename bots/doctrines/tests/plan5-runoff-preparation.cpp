#include "../runoff/src/preparation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace p5;
using namespace p5::runoff;
namespace e=p5::execution;
static int checks=0,samples=0;
static const char* current_case="setup";
static Arm current_arm=Arm::finite_harvest;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"Runoff preparation case=%s sample=%d arm=%d line=%d: %s\n",current_case,samples,int(current_arm),__LINE__,#x);std::exit(1);}}while(false)
template<class T> static bool equal(const T& a,const T& b){return a==b;}
static bool equal(double a,double b){return a==b || (std::isnan(a) && std::isnan(b));}
#define SAME(field) CHECK(equal(a.field,b.field))

// Selector order held from policy.h SHA256
// ad5ee79768138ccffd4e3e11e91231b63a51fa842644edb5978d398fb09e6e46.
// Full-default quote body held before scratch reuse, policy.h SHA256
// c25fa99a6c5338b9da08e114574369a89bb9b1e072d8d5302937e00c130efc47.
static bool legacy_valid_input(const Observation& o){
    const auto r=private_view(o.resources,o.round);
    if(o.round<0 || o.round>=o.settings.round_limit || !o.actual_marcella || !o.settings.valid() || o.settings.capture_rounds>=100000 || !o.windows.valid || !valid_ledger(o.ledger) ||
        !r.self || !r.visible || !r.own_team || !r.bag.current(o.round) || !r.gold.current(o.round) || r.bag.value<0 || r.gold.value<0 ||
        o.actor.id!=r.id || !safe(o.actor) || o.purchase_count<0 || o.purchase_count>3 || o.team_score<0 ||
        o.current_to_altar.destination<0 || o.current_to_spawn.destination<0)return false;
    for(int i=0;i<3;++i){if(!group_valid(o.groups[i],i==2))return false;for(int j=0;j<i;++j)if(o.groups[i].id==o.groups[j].id)return false;}
    return true;
}
static Quote legacy_quote(const Observation& o,Profile profile,int prefix,int pi=-1,int buy_before=0){
    Quote q;q.prefix=prefix;q.purchase=pi;q.buy_before=pi<0?-1:buy_before;
    if(!legacy_valid_input(o) || prefix<0 || prefix>2 || (pi>=0 && (prefix==0 || buy_before>=prefix)))return q;
    q.schedule=initial(o);Stock a{o.actor,0,o.resources.bag.value,o.resources.gold.value,o.team_score,current_location,o.ledger};
    std::array<int,2> remaining{};int count=0;for(int i=0;i<2;++i)if(!(a.ledger.consumed&(1u<<i)))remaining[count++]=i;
    if(prefix>count || (profile.arm==Arm::finite_harvest && o.ledger.liquidated && prefix))return q;
    if(profile.arm==Arm::bank_each_control && a.bag && !bank(o,a,q))return q;
    for(int n=0;n<prefix && !q.terminal;++n){const int i=remaining[n];
        if(pi>=0 && buy_before==n && !buy(o,i,pi,a,q))return q;
        if(!produce(o,i,a,q))return q;
        if(profile.arm==Arm::bank_each_control && !bank(o,a,q))return q;
        // A useful first-prefix winning liquidation is never delayed for B.
        if(profile.arm==Arm::finite_harvest && a.score+a.bag>=o.settings.winning_gems && !bank(o,a,q))return q;
    }
    if(!q.terminal && a.bag && !bank(o,a,q))return q;
    if(!q.terminal && !finish(o,a,q))return q;
    if(a.tick>o.settings.round_limit-o.round || (profile.arm==Arm::finite_harvest && prefix>0 &&
        q.last_bank>*std::min_element(o.windows.window.begin(),o.windows.window.end())))return q;
    q.rank=forecast_schedule(q.schedule,o.windows);q.feasible=q.rank.status==AccountingStatus::feasible;return q;
}
// Deliberately enumerate every legacy quote, including unaffordable purchases.
// Do not call quote(), best(), better(), or an internal catalogue path here.
static Quote original_best(const Observation& o,Profile p){
    Quote result;
    const auto consider=[&](const Quote& q){
        if(q.feasible && (!result.feasible || compare_results(q.rank,result.rank)>0))result=q;
    };
    for(int prefix=0;prefix<=2;++prefix){
        consider(legacy_quote(o,p,prefix));
        if(!o.ledger.purchases)
            for(int pi=0;pi<o.purchase_count;++pi)
                for(int n=0;n<prefix;++n)consider(legacy_quote(o,p,prefix,pi,n));
    }
    return result;
}
static Decision original_choose(const Observation& o,Profile profile){
    Decision d;d.input=o;d.profile=profile;
    if(o.execution.channel!=e::Channel::none){d.feasible=true;d.job=Job::preserve;return d;}
    const auto r=private_view(o.resources,o.round);
    if(o.settings.valid() && o.actual_marcella && r.self && r.own_team && r.bag.current(o.round) && r.gold.current(o.round) && r.bag.value>0 &&
        r.gold.value>=0 && o.ledger.banks<2 && o.execution.safe && o.execution.adjacent_altar && o.execution.legal_winning_bank &&
        std::int64_t(o.team_score)+r.bag.value>=o.settings.winning_gems){
        d.selected.schedule=initial(o);Stock a{o.actor,0,r.bag.value,r.gold.value,o.team_score,altar_location,o.ledger};
        auto immediate=o;immediate.current_to_altar.destination=o.execution.altar_id;
        if(bank(immediate,a,d.selected)){
            d.selected.schedule.immediate_legal_winning_bank=true;
            d.selected.rank=forecast_schedule(d.selected.schedule,o.windows);
            d.selected.feasible=d.feasible=d.selected.rank.status==AccountingStatus::feasible;
            d.job=d.feasible?Job::winning_bank:Job::unavailable;return d;
        }
    }
    if(!legacy_valid_input(o))return d;
    d.control=original_best(o,{Arm::bank_each_control});
    d.selected=profile.arm==Arm::bank_each_control?d.control:original_best(o,profile);
    d.feasible=d.selected.feasible;
    if(d.feasible)d.job=d.selected.prefix?Job::harvest:o.resources.bag.value?Job::liquidate:Job::productive_fallback;
    return d;
}
static void same_rank(const AccountingResult& a,const AccountingResult& b){
    SAME(status);CHECK(std::strcmp(a.reason,b.reason)==0);SAME(Y);SAME(D);SAME(M);
    SAME(final_bag);SAME(final_gold);SAME(actor_ids);SAME(actor_count);SAME(earned_gold);
    SAME(kills);SAME(occupied_turns);SAME(peak_bag);SAME(productive_arrival);SAME(target_id);
    SAME(windows);SAME(immediate_win);SAME(official_endpoint);
}
static void same_step(const Step& a,const Step& b){
    SAME(kind);SAME(group);SAME(index);SAME(waypoint);SAME(duration);SAME(purchase);
    SAME(health);SAME(strength);SAME(max_health);SAME(basic_modifier);
}
static void same_schedule(const RunoffSchedule& a,const RunoffSchedule& b){
    SAME(actor_count);SAME(event_count);
    CHECK(a.actor_count>=0 && a.actor_count<=int(a.actors.size()));
    CHECK(a.event_count>=0 && a.event_count<=int(a.events.size()));
    const auto same_actor=[](const auto& a,const auto& b){
        SAME(id);SAME(self);SAME(visible);SAME(bag_known);SAME(bag);SAME(gold);
        SAME(tribute_gold_per_gem);SAME(occupied_turns);
    };
    for(int i=0;i<a.actor_count;++i)same_actor(a.actors[i],b.actors[i]);
    const auto same_event=[](const auto& a,const auto& b){
        SAME(id);SAME(tick);SAME(kind);SAME(actor);SAME(group);SAME(payout_id);
        SAME(participants);SAME(holders);SAME(gems);SAME(gold);SAME(waypoint);SAME(legal);
    };
    for(int i=0;i<a.event_count;++i)same_event(a.events[i],b.events[i]);
    SAME(target_id);SAME(starting_team_score);SAME(win_score);SAME(tribute_gems_per_tick);
    SAME(tribute_min_ticks);SAME(terminal_win);SAME(terminal_win_tick);
    SAME(immediate_legal_winning_bank);SAME(doctrine_id);SAME(denial_evidenced);
    SAME(denied_gems);SAME(denied_bank_tick);SAME(projected_kills);
}
static void same_quote(const Quote& a,const Quote& b){
    SAME(feasible);SAME(terminal);SAME(count);SAME(prefix);SAME(purchase);SAME(buy_before);
    SAME(first_bank);SAME(last_bank);SAME(occupied);SAME(capture_tick);
    for(std::size_t i=0;i<a.steps.size();++i)same_step(a.steps[i],b.steps[i]);
    same_schedule(a.schedule,b.schedule);same_rank(a.rank,b.rank);
}
static void same_candidates(const Observation& o,Profile p){
    for(int prefix=0;prefix<=2;++prefix){
        same_quote(quote(o,p,prefix),legacy_quote(o,p,prefix));
        if(!o.ledger.purchases)
            for(int pi=0;pi<o.purchase_count;++pi)
                for(int n=0;n<prefix;++n)same_quote(quote(o,p,prefix,pi,n),legacy_quote(o,p,prefix,pi,n));
    }
}
static void same_leg(const RouteLeg& a,const RouteLeg& b){
    SAME(destination);SAME(distance);SAME(terrain_and_diagonals_checked);SAME(safety_checked);
}
static void same_actor(const CombatActor& a,const CombatActor& b){
    SAME(id);SAME(self);SAME(visible);SAME(ranged);SAME(health);SAME(max_health);SAME(strength);
    SAME(basic_modifier);SAME(direct_or_reset_damage);SAME(spell_known_ready);SAME(legal_first_cast);
    SAME(reserved_cast);SAME(heal_recipient);SAME(heal_amount);SAME(heal_legal_ready);SAME(shield);
    SAME(shield_expiry_tick);SAME(visible_enemy_next_phase);SAME(enemy_cast_bound_known);
}
static void same_group(const Group& a,const Group& b){
    SAME(id);SAME(gems);SAME(gold);SAME(current);SAME(available);SAME(no_other_holder);SAME(count);
    const auto same_monster=[](const auto& a,const auto& b){
        SAME(id);SAME(public_health);SAME(observed_health);SAME(effective_strength);SAME(visible);
    };
    for(std::size_t i=0;i<a.monsters.size();++i)same_monster(a.monsters[i],b.monsters[i]);
    same_leg(a.from_current,b.from_current);same_leg(a.from_altar,b.from_altar);
    same_leg(a.from_spawn,b.from_spawn);same_leg(a.to_altar,b.to_altar);
    same_leg(a.to_spawn,b.to_spawn);same_leg(a.certificate_return,b.certificate_return);
}
static void same_execution(const e::Observation& a,const e::Observation& b){
    SAME(owner_turn);SAME(remaining_rounds);SAME(forecast_turns);SAME(fuel);SAME(selection_reserve);
    SAME(navigation_reserve);SAME(action_reserve);SAME(capture_duration);SAME(group_id);
    SAME(alive);SAME(safe);SAME(can_act);SAME(at_target);SAME(at_spawn);SAME(spawn_recovery_observed);
    SAME(needs_recovery);SAME(in_threat_range);SAME(can_recall);SAME(channel);SAME(channel_valid);
    SAME(channel_safe);SAME(channel_cancellable);SAME(on_group_spawn);SAME(group_cleared);
    SAME(group_live_known);SAME(group_respawning);SAME(enemy_on_region);SAME(occupancy_reset_observed);
    SAME(helper);SAME(holder_visible_on_spawn);SAME(remaining_coalition_safety_fresh);
    SAME(remaining_coalition_safe);SAME(coalition_visible);SAME(coalition_safety_fresh);SAME(coalition_safe);
    SAME(own_payout_observed);SAME(own_payout_group);SAME(own_gems_received);SAME(own_gold_received);
    SAME(payout_receipt);SAME(bank_requested);SAME(legal_winning_bank);SAME(adjacent_altar);
    SAME(hostile_lock);SAME(lock_visible);SAME(lock_attack_legal);SAME(altar_id);SAME(own_bag);
    SAME(tribute_ticks_quote);SAME(purchase_requested);SAME(purchase_purchasable);SAME(purchase_affordable);
    SAME(field_equipment_allowed);SAME(purchase_is_equipment);SAME(purchase_id);SAME(can_move);
    SAME(can_attack);SAME(can_cast);SAME(ranged);SAME(moved_this_turn);SAME(attack_reset_after_move);
    SAME(successful_cast_this_turn);SAME(target_visible);SAME(target_category_legal);SAME(target_in_range);
    SAME(attack_target);SAME(group_count);SAME(move_count);SAME(observation_epoch);SAME(feedback_sequence);
    SAME(feedback_kind);SAME(feedback_success);SAME(feedback_target);
    const auto same_spell=[](const auto& a,const auto& b){
        SAME(target);SAME(ready);SAME(in_range_mask);SAME(visible);SAME(category_legal);SAME(useful);
        SAME(movement_exclusive);SAME(dash);SAME(path_legal);SAME(landing_legal);SAME(known_occupied);SAME(shield_needed);
    };
    for(std::size_t i=0;i<a.spells.size();++i)same_spell(a.spells[i],b.spells[i]);
    const auto same_group=[](const auto& a,const auto& b){
        SAME(id);SAME(distance);SAME(assigned);SAME(reachable);SAME(solo_safe);SAME(unknown);
        SAME(coalition_required);SAME(gem_bearing);
    };
    for(std::size_t i=0;i<a.groups.size();++i)same_group(a.groups[i],b.groups[i]);
    const auto same_move=[](const auto& a,const auto& b){
        SAME(destination);SAME(in_mask);SAME(path_legal);SAME(landing_legal);SAME(safe);SAME(known_occupied);
    };
    for(std::size_t i=0;i<a.moves.size();++i)same_move(a.moves[i],b.moves[i]);
}
static void same_input(const Observation& a,const Observation& b){
    SAME(round);SAME(team_score);SAME(actual_marcella);SAME(bank_safe);SAME(spawn_safe);
    SAME(resources.id);SAME(resources.self);SAME(resources.visible);SAME(resources.own_team);
    SAME(resources.bag.value);SAME(resources.bag.knowledge);SAME(resources.bag.round);
    SAME(resources.gold.value);SAME(resources.gold.knowledge);SAME(resources.gold.round);
    same_actor(a.actor,b.actor);
    for(std::size_t i=0;i<a.groups.size();++i)same_group(a.groups[i],b.groups[i]);
    same_leg(a.A_to_B,b.A_to_B);same_leg(a.current_to_altar,b.current_to_altar);
    same_leg(a.current_to_spawn,b.current_to_spawn);same_leg(a.altar_to_spawn,b.altar_to_spawn);
    SAME(spawn_rest);SAME(spawn_heal_per_tick);SAME(purchase_count);
    const auto same_purchase=[](const auto& a,const auto& b){
        SAME(item);SAME(family);SAME(cost);SAME(shards);SAME(strength);SAME(health);SAME(spawn_heal);
        SAME(basic_ratio);SAME(legal_on_spawn);SAME(no_new_income_passive);
    };
    for(std::size_t i=0;i<a.purchases.size();++i)same_purchase(a.purchases[i],b.purchases[i]);
    SAME(ledger.captures);SAME(ledger.banks);SAME(ledger.purchases);SAME(ledger.consumed);SAME(ledger.liquidated);
    SAME(settings.capture_rounds);SAME(settings.tribute_gems_per_tick);SAME(settings.tribute_min_ticks);
    SAME(settings.round_limit);SAME(settings.winning_gems);SAME(settings.marcella_gold_per_gem);
    SAME(windows.deadline);SAME(windows.window);SAME(windows.rate);SAME(windows.official_remaining);SAME(windows.valid);
    same_execution(a.execution,b.execution);
}
static void preparation_sample(const Observation&,Profile,const Decision&);
static void sample(const char* name,const Observation& o){
    current_case=name;++samples;
    for(Arm arm:{Arm::finite_harvest,Arm::bank_each_control}){
        current_arm=arm;const Profile p{arm};
        same_candidates(o,p);
        same_quote(best(o,p),original_best(o,p));
        const auto a=choose_job(o,p),b=original_choose(o,p);
        SAME(feasible);SAME(job);SAME(profile.arm);
        same_input(a.input,b.input);same_quote(a.selected,b.selected);same_quote(a.control,b.control);
        preparation_sample(o,p,b);
    }
}
static RouteLeg leg(int id,int distance=1){return {id,distance,true,true};}
static Group group(int id,int gems,double hp=10,double damage=0){
    Group g;g.id=id;g.gems=gems;g.current=g.available=g.no_other_holder=true;g.count=1;
    g.monsters[0]={100+id,hp,hp,damage,true};
    g.from_current=g.from_altar=g.from_spawn=leg(id);g.to_altar=leg(50);g.to_spawn=leg(60);g.certificate_return=leg(id);return g;
}
static Observation fixture(){
    Observation o;o.round=0;o.resources={10,true,true,true,{0,Knowledge::current,0},{0,Knowledge::current,0}};
    o.actual_marcella=o.bank_safe=o.spawn_safe=true;o.actor.id=10;
    o.actor.self=o.actor.visible=o.actor.enemy_cast_bound_known=true;
    o.actor.health=o.actor.max_health=100;o.actor.strength=10;
    o.groups={group(1,30),group(2,20),group(3,10)};o.groups[0].from_current=leg(1,0);
    o.A_to_B=leg(2);o.current_to_altar=leg(50);o.current_to_spawn=o.altar_to_spawn=leg(60);
    o.settings={0,10,1,200,1000,2};ScoreHistory history;CHECK(history.observe(0,0));
    o.windows=history.windows(0,200,1000,0);o.windows.window={17,17,17};
    auto& x=o.execution;x.owner_turn=0;x.remaining_rounds=200;x.fuel=100000;x.forecast_turns=30;x.observation_epoch=1;
    x.alive=x.safe=x.can_act=x.can_move=x.can_attack=x.can_cast=true;
    x.move_count=2;x.moves[0]={90,true,true,true,true,false};x.moves[1]={91,true,true,true,true,false};return o;
}
static Observation financed(){
    auto o=fixture();o.groups[1]=group(2,20,90,10);o.windows.window={40,40,40};
    o.purchase_count=1;o.purchases[0]={7,0,30,1,10,0,0,1,true,true};return o;
}
static void boundary_cases(){
    const auto base=fixture();sample("base",base);
    for(int bag:{0,1,9,10,11,29,30,49,50,99})for(int gold:{0,29,30,31,59,60}){
        auto o=financed();o.resources.bag.value=bag;o.resources.gold.value=gold;
        sample("bag-gold-boundary",o);
    }
    for(int cost:{1,29,30,31,59,60,61,90,91})for(int gold:{0,29,30,60}){
        auto o=financed();o.purchases[0].cost=cost;o.resources.gold.value=gold;
        sample("cost-financing-boundary",o);
    }
    for(int window:{0,9,10,16,17,18,27,28,29,40}){
        auto o=financed();o.windows.window={window,window+1,window+2};sample("finish-window-boundary",o);
    }
    for(int capture:{0,1,5})for(int remaining:{20,27,28,40}){
        auto o=financed();o.settings.capture_rounds=capture;o.settings.round_limit=remaining;
        o.windows.official_remaining=remaining;sample("capture-official-boundary",o);
    }
    auto tied=financed();tied.windows.window={28,28,28};tied.purchase_count=2;tied.purchases[1]=tied.purchases[0];
    sample("strict-purchase-tie",tied);
    CHECK(original_choose(tied,{Arm::bank_each_control}).selected.purchase==0);
    auto health=financed();health.purchases[0]={8,1,30,1,0,100,100,1,true,true};sample("health-purchase",health);
    auto mixed=financed();mixed.purchase_count=3;mixed.resources.gold.value=30;
    mixed.purchases[1]={8,1,30,1,0,100,100,1,true,true};mixed.purchases[2]={9,2,30,1,0,0,0,2,true,true};sample("three-purchases",mixed);
    auto capture_gold=financed();capture_gold.groups[0].gold=30;capture_gold.purchases[0].cost=30;sample("capture-funded-purchase",capture_gold);
}
static void ledger_cases(){
    for(unsigned consumed=0;consumed<4;++consumed)for(int banks=0;banks<=2;++banks)
        for(int purchases=0;purchases<=1;++purchases)for(bool liquidated:{false,true}){
            auto o=financed();o.resources.bag.value=30;o.resources.gold.value=30;
            o.ledger={int(bool(consumed&1))+int(bool(consumed&2)),banks,purchases,consumed,liquidated};sample("ledger",o);
        }
}
static void invalid_cases(){
    for(int bad=0;bad<40;++bad){
        auto o=financed();
        switch(bad){
        case 0:o.resources.gold.knowledge=Knowledge::unknown;break;
        case 1:o.resources.bag.knowledge=Knowledge::stale;break;
        case 2:o.resources.gold.round=-1;break;
        case 3:o.resources.self=false;break;
        case 4:o.resources.visible=false;break;
        case 5:o.resources.own_team=false;break;
        case 6:o.resources.bag.value=-1;break;
        case 7:o.resources.gold.value=-1;break;
        case 8:o.actor.id=11;break;
        case 9:o.actor.health=19;break;
        case 10:o.actor.health=0;break;
        case 11:o.actor.visible_enemy_next_phase=51;break;
        case 12:o.actor.enemy_cast_bound_known=false;break;
        case 13:o.actor.strength=0;break;
        case 14:o.actor.ranged=true;break;
        case 15:o.actor.health=std::numeric_limits<double>::quiet_NaN();break;
        case 16:o.groups[2].current=false;break;
        case 17:o.groups[2].id=o.groups[0].id;break;
        case 18:o.groups[0].monsters[0].observed_health=11;break;
        case 19:o.groups[0].count=2;o.groups[0].monsters[1]=o.groups[0].monsters[0];break;
        case 20:o.current_to_altar.destination=-1;break;
        case 21:o.current_to_spawn.safety_checked=false;break;
        case 22:o.A_to_B.distance=-1;break;
        case 23:o.groups[2].to_altar.destination=51;break;
        case 24:o.bank_safe=false;break;
        case 25:o.spawn_safe=false;break;
        case 26:o.purchase_count=-1;break;
        case 27:o.purchase_count=4;break;
        case 28:o.ledger.captures=1;break;
        case 29:o.ledger.consumed=4;break;
        case 30:o.ledger.banks=3;break;
        case 31:o.ledger.purchases=2;break;
        case 32:o.windows.valid=false;break;
        case 33:o.settings.tribute_gems_per_tick=0;break;
        case 34:o.round=o.settings.round_limit;break;
        case 35:o.actual_marcella=false;break;
        case 36:o.purchases[0].no_new_income_passive=false;break;
        case 37:o.purchases[0].strength=0;break;
        case 38:o.purchases[0].cost=0;break;
        case 39:o.purchases[0].spawn_heal=1;break;
        }
        sample("unsafe-corrupt",o);
    }
}
static void terminal_cases(){
    for(int score:{950,969,970,999})for(int bag:{0,1,29,30,50}){
        auto o=financed();o.team_score=score;o.resources.bag.value=bag;sample("terminal-harvest",o);
        o.execution.adjacent_altar=o.execution.legal_winning_bank=true;o.execution.altar_id=50;
        sample("immediate-bank",o);
    }
    auto immediate=fixture();immediate.team_score=970;immediate.resources.bag.value=30;
    immediate.execution.adjacent_altar=immediate.execution.legal_winning_bank=true;immediate.execution.altar_id=50;
    immediate.groups[2].current=false;sample("winning-before-general-validation",immediate);
    CHECK(original_choose(immediate,{}).job==Job::winning_bank);
    immediate.windows.valid=false;sample("winning-invalid-forecast",immediate);
    for(e::Channel channel:{e::Channel::tribute,e::Channel::recall,e::Channel::teleport}){
        auto o=immediate;o.execution.channel=channel;o.actual_marcella=false;o.resources.self=false;
        sample("channel-before-validation",o);CHECK(original_choose(o,{}).job==Job::preserve);
    }
}
// Small deterministic property table; no random runtime or unbounded fuzzing.
static void property_cases(){
    for(unsigned i=0;i<48;++i){
        auto o=financed();o.resources.bag.value=int((i*17)%71);o.resources.gold.value=int((i*29)%92);
        o.groups[0].gold=int((i*7)%31);o.groups[0].gems=1+int((i*13)%51);o.groups[1].gems=1+int((i*19)%41);
        o.groups[1].monsters[0].observed_health=10+int((i*23)%81);
        o.groups[1].monsters[0].effective_strength=int(i%13);o.purchases[0].cost=1+int((i*11)%91);
        o.purchases[0].strength=5+int(i%4)*5;o.settings.capture_rounds=int(i%6);
        o.windows.window={20+int(i%11),25+int(i%13),30+int(i%17)};
        o.A_to_B.distance=int(i%4);o.current_to_spawn.distance=int(i%3);
        sample("deterministic-property",o);
    }
}
// Preparation tests retain the independent exhaustive oracle above. The
// synchronous entrypoints are checked as well, so scheduling does not hide a
// public API regression. --focused runs the ten preparation groups first.
static int preparation_groups=0,preparation_mutations=0;
static void preparation_case(const char* name){current_case=name;++preparation_groups;}
static void same_decision(const Decision& a,const Decision& b){
    SAME(feasible);SAME(job);SAME(profile.arm);same_input(a.input,b.input);
    same_quote(a.selected,b.selected);same_quote(a.control,b.control);
}
static Decision poisoned(){
    Decision d;d.feasible=true;d.job=Job::harvest;d.selected.feasible=d.control.feasible=true;return d;
}
static void no_authority(const Decision& d){
    CHECK(!d.feasible);CHECK(d.job==Job::unavailable);CHECK(!d.selected.feasible);CHECK(!d.control.feasible);
}
static void pending(Preparation& p,const Observation& o,PreparationDomain domain={}){
    auto d=poisoned();CHECK(p.publish_into(o,d,domain)==PreparationStatus::incomplete);no_authority(d);
}
static void settle(Preparation& p,const Observation& o,Profile profile={},PreparationDomain domain={}){
    const auto restarts=p.restarts;
    for(int calls=0;p.status==PreparationStatus::incomplete && calls<25;++calls){
        const int cursor=p.cursor;p.advance(o,profile,domain);
        CHECK(p.restarts==restarts);CHECK(p.cursor>=cursor && p.cursor<=cursor+1);CHECK(p.cursor<=24);
        if(p.status==PreparationStatus::incomplete)pending(p,o,domain);
    }
    CHECK(p.status==PreparationStatus::complete);
}
static void preparation_sample(const Observation& o,Profile profile,const Decision& expected){
    Preparation p;p.advance(o,profile);
    if(p.status==PreparationStatus::incomplete){CHECK(p.cursor==0);pending(p,o);settle(p,o,profile);}
    auto d=poisoned();const auto status=p.publish_into(o,d);
    if(status==PreparationStatus::invalid){no_authority(d);CHECK(!expected.feasible);}
    else{CHECK(status==PreparationStatus::complete);same_decision(d,expected);}
}
static void warm(Preparation& p,const Observation& o,Profile profile={},PreparationDomain domain={}){
    CHECK(p.advance(o,profile,domain)==PreparationStatus::incomplete);CHECK(p.cursor==0);
    CHECK(p.advance(o,profile,domain)==PreparationStatus::incomplete);CHECK(p.cursor==1);
    CHECK(p.advance(o,profile,domain)==PreparationStatus::incomplete);CHECK(p.cursor==2);
}
static void retime(Observation& o,int round){
    o.round=round;o.resources.bag.round=o.resources.gold.round=round;
    o.execution.owner_turn=round;o.execution.remaining_rounds=o.settings.round_limit-round;
    ++o.execution.observation_epoch;o.execution.fuel-=100;
    o.windows.official_remaining=o.settings.round_limit-round;
    for(int i=0;i<3;++i)o.windows.deadline[i]=o.windows.official_remaining-i;
}
static void focused_static(){
    preparation_case("focused-static-full-decision");
    auto tied=financed();tied.purchase_count=2;tied.purchases[1]=tied.purchases[0];tied.resources.gold.value=30;
    for(const auto& o:{fixture(),financed(),tied})for(Arm arm:{Arm::finite_harvest,Arm::bank_each_control}){
        current_arm=arm;preparation_sample(o,{arm},original_choose(o,{arm}));
    }
}
static void advancing_rounds(){
    preparation_case("advancing-owner-turns");
    for(Arm arm:{Arm::finite_harvest,Arm::bank_each_control}){
        current_arm=arm;auto o=financed();o.purchase_count=3;o.resources.gold.value=90;
        o.purchases[1]={8,1,30,1,0,100,100,1,true,true};o.purchases[2]={9,2,30,1,0,0,0,2,true,true};
        Preparation p;CHECK(p.advance(o,{arm})==PreparationStatus::incomplete);CHECK(p.cursor==0);
        for(int calls=0;p.status==PreparationStatus::incomplete && calls<25;++calls){
            const int cursor=p.cursor;retime(o,o.round+1);p.advance(o,{arm});
            CHECK(p.restarts==0);CHECK(p.cursor==cursor+1);CHECK(p.cursor<=24);
            if(p.status==PreparationStatus::incomplete)pending(p,o);
        }
        CHECK(p.status==PreparationStatus::complete);CHECK(p.cursor==(arm==Arm::finite_harvest?24:12));
        auto d=poisoned();CHECK(p.publish_into(o,d)==PreparationStatus::complete);
        same_decision(d,original_choose(o,{arm}));same_decision(d,choose_job(o,{arm}));
        const int cursor=p.cursor;retime(o,o.round+1);CHECK(p.advance(o,{arm})==PreparationStatus::complete);
        CHECK(p.cursor==cursor);CHECK(p.publish_into(o,d)==PreparationStatus::complete);same_input(d.input,o);
    }
}
static void partial_publication(){
    preparation_case("partial-publication-zero-authority");
    auto o=financed();Preparation p;CHECK(p.advance(o)==PreparationStatus::incomplete);CHECK(p.cursor==0);pending(p,o);
    CHECK(p.advance(o)==PreparationStatus::incomplete);CHECK(p.cursor==1);pending(p,o);
    settle(p,o);auto d=poisoned();CHECK(p.publish_into(o,d)==PreparationStatus::complete);CHECK(d.feasible);
    o.resources.bag.value=1;const auto restarts=p.restarts;
    CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);no_authority(d);CHECK(p.cursor==0);CHECK(p.restarts==restarts+1);
}
template<class Mutation> static void mutation(const char* name,Mutation change){
    current_case=name;++preparation_mutations;auto o=financed();Preparation p;warm(p,o);
    const auto restarts=p.restarts;change(o);CHECK(valid_input(o));auto d=poisoned();
    CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);no_authority(d);CHECK(p.cursor==0);CHECK(p.restarts==restarts+1);
    settle(p,o);CHECK(p.publish_into(o,d)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));
}
static void physical_key_mutations(){
    preparation_case("physical-key-families");
    mutation("score",[](auto& o){++o.team_score;});
    mutation("self-identity",[](auto& o){++o.resources.id;++o.actor.id;});
    mutation("bag-value",[](auto& o){++o.resources.bag.value;});
    mutation("gold-value",[](auto& o){++o.resources.gold.value;});
    mutation("bank-safety",[](auto& o){o.bank_safe=false;});
    mutation("spawn-safety",[](auto& o){o.spawn_safe=false;});
    mutation("actor-health",[](auto& o){--o.actor.health;});
    mutation("actor-max-health",[](auto& o){++o.actor.max_health;});
    mutation("actor-strength",[](auto& o){++o.actor.strength;});
    mutation("actor-basic-modifier",[](auto& o){o.actor.basic_modifier+=.1;});
    mutation("actor-shield",[](auto& o){o.actor.shield=1;});
    mutation("actor-shield-expiry",[](auto& o){o.actor.shield_expiry_tick=1;});
    mutation("actor-threat",[](auto& o){o.actor.visible_enemy_next_phase=.1;});
    for(int i=0;i<3;++i){
        mutation("group-gems",[i](auto& o){++o.groups[i].gems;});
        mutation("group-gold",[i](auto& o){++o.groups[i].gold;});
        mutation("monster-identity",[i](auto& o){++o.groups[i].monsters[0].id;});
        mutation("monster-public-health",[i](auto& o){++o.groups[i].monsters[0].public_health;});
        mutation("monster-observed-health",[i](auto& o){--o.groups[i].monsters[0].observed_health;});
        mutation("monster-strength",[i](auto& o){++o.groups[i].monsters[0].effective_strength;});
        mutation("monster-visibility",[i](auto& o){o.groups[i].monsters[0].visible=false;});
        for(int route=0;route<6;++route)mutation("group-route",[i,route](auto& o){
            auto& g=o.groups[i];RouteLeg* routes[]={&g.from_current,&g.from_altar,&g.from_spawn,&g.to_altar,&g.to_spawn,&g.certificate_return};
            ++routes[route]->distance;
        });
    }
    mutation("finite-current",[](auto& o){o.groups[0].current=false;});
    mutation("finite-availability",[](auto& o){o.groups[0].available=false;});
    mutation("finite-holder",[](auto& o){o.groups[0].no_other_holder=false;});
    mutation("monster-count",[](auto& o){o.groups[0].count=2;o.groups[0].monsters[1]={999,10,10,0,true};});
    mutation("A-to-B",[](auto& o){++o.A_to_B.distance;});
    mutation("altar-route",[](auto& o){++o.current_to_altar.distance;});
    mutation("spawn-route",[](auto& o){++o.current_to_spawn.distance;});
    mutation("altar-to-spawn",[](auto& o){++o.altar_to_spawn.distance;});
    mutation("route-destination",[](auto& o){++o.groups[0].from_current.destination;});
    mutation("route-terrain-proof",[](auto& o){o.groups[0].from_current.terrain_and_diagonals_checked=false;});
    mutation("route-safety-proof",[](auto& o){o.groups[0].from_current.safety_checked=false;});
    mutation("spawn-rest",[](auto& o){++o.spawn_rest;});
    mutation("spawn-heal",[](auto& o){++o.spawn_heal_per_tick;});
    mutation("purchase-count",[](auto& o){o.purchase_count=0;});
    mutation("purchase-identity",[](auto& o){++o.purchases[0].item;});
    mutation("purchase-family",[](auto& o){++o.purchases[0].family;});
    mutation("purchase-cost",[](auto& o){++o.purchases[0].cost;});
    mutation("purchase-shards",[](auto& o){++o.purchases[0].shards;});
    mutation("purchase-strength",[](auto& o){++o.purchases[0].strength;});
    mutation("purchase-health",[](auto& o){++o.purchases[0].health;});
    mutation("purchase-heal",[](auto& o){++o.purchases[0].spawn_heal;});
    mutation("purchase-basic-ratio",[](auto& o){o.purchases[0].basic_ratio+=.1;});
    mutation("purchase-legal",[](auto& o){o.purchases[0].legal_on_spawn=false;});
    mutation("purchase-income",[](auto& o){o.purchases[0].no_new_income_passive=false;});
    mutation("ledger-capture-consumption",[](auto& o){o.ledger.captures=1;o.ledger.consumed=1;});
    mutation("ledger-bank",[](auto& o){o.ledger.banks=1;});
    mutation("ledger-purchase",[](auto& o){o.ledger.purchases=1;});
    mutation("ledger-liquidation",[](auto& o){o.ledger.liquidated=true;});
    mutation("settings-capture",[](auto& o){++o.settings.capture_rounds;});
    mutation("settings-tribute-rate",[](auto& o){++o.settings.tribute_gems_per_tick;});
    mutation("settings-tribute-minimum",[](auto& o){++o.settings.tribute_min_ticks;});
    mutation("settings-round-limit",[](auto& o){++o.settings.round_limit;});
    mutation("settings-winning-gems",[](auto& o){++o.settings.winning_gems;});
    mutation("settings-income",[](auto& o){++o.settings.marcella_gold_per_gem;});
}
static void domain_and_profile(){
    preparation_case("domain-and-profile");const auto o=financed();
    for(bool geometry:{false,true}){
        Preparation p;PreparationDomain domain{12,34};warm(p,o,{},domain);const auto restarts=p.restarts;
        if(geometry)++domain.geometry;else ++domain.position;auto d=poisoned();
        CHECK(p.publish_into(o,d,domain)==PreparationStatus::incomplete);no_authority(d);
        CHECK(p.cursor==0);CHECK(p.restarts==restarts+1);settle(p,o,{},domain);
        CHECK(p.publish_into(o,d,domain)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));
    }
    Preparation p;warm(p,o);const auto restarts=p.restarts;
    CHECK(p.advance(o,{Arm::bank_each_control})==PreparationStatus::incomplete);CHECK(p.cursor==0);CHECK(p.restarts==restarts+1);
    settle(p,o,{Arm::bank_each_control});Decision d;CHECK(p.publish_into(o,d)==PreparationStatus::complete);
    same_decision(d,original_choose(o,{Arm::bank_each_control}));
}
static void ranking_windows(){
    preparation_case("window-deadline-rate-and-endpoint");auto o=financed();Preparation p;warm(p,o);const int cursor=p.cursor;
    o.windows.deadline={100,110,120};o.windows.rate={.125,3.5,20};
    CHECK(p.advance(o)==PreparationStatus::incomplete);CHECK(p.cursor==cursor+1);CHECK(p.restarts==0);
    settle(p,o);Decision d;CHECK(p.publish_into(o,d)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));
    ++o.windows.window[1];CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);CHECK(p.cursor==0);CHECK(p.restarts==1);no_authority(d);
    auto endpoint=fixture();endpoint.windows.window={17,17,17};endpoint.windows.deadline={17,17,17};
    Preparation q;warm(q,endpoint);endpoint.windows.official_remaining=17;
    CHECK(q.publish_into(endpoint,d)==PreparationStatus::incomplete);CHECK(q.cursor==0);CHECK(q.restarts==1);no_authority(d);
    auto invalid=financed();Preparation r;warm(r,invalid);invalid.windows.deadline[0]=invalid.windows.window[0]-1;
    CHECK(r.publish_into(invalid,d)==PreparationStatus::invalid);no_authority(d);
}
static void horizon_floors(){
    preparation_case("successful-horizon-tests-and-failed-direct-branch");
    auto o=fixture();o.groups[2].from_current.distance=20;retime(o,169);o.windows.deadline={31,31,31};
    const auto before=legacy_quote(o,{Arm::bank_each_control},0);CHECK(before.feasible);CHECK(before.steps[0].kind==StepKind::renew);
    Preparation p;CHECK(p.advance(o)==PreparationStatus::incomplete);CHECK(p.advance(o)==PreparationStatus::incomplete);
    CHECK(p.proof.game_floor==31);CHECK(p.proof.official_floor==22);retime(o,170);o.windows.deadline={30,30,30};
    const auto after=legacy_quote(o,{Arm::bank_each_control},0);CHECK(after.feasible);CHECK(after.steps[0].kind==StepKind::recover);
    auto d=poisoned();CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);CHECK(p.cursor==0);CHECK(p.restarts==1);no_authority(d);
    // Failed old direct-time tests stay false as horizons shrink. The successful
    // recovered branch remains valid, so this frame must retain its first quote.
    Preparation q;CHECK(q.advance(o)==PreparationStatus::incomplete);CHECK(q.advance(o)==PreparationStatus::incomplete);
    CHECK(q.proof.game_floor==16);retime(o,171);o.windows.deadline={29,29,29};
    CHECK(q.publish_into(o,d)==PreparationStatus::incomplete);CHECK(q.cursor==1);CHECK(q.restarts==0);no_authority(d);
    settle(q,o);CHECK(q.publish_into(o,d)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));
    // Accounting's occupied-turn test has an independent official horizon.
    auto official=fixture();official.groups[2].from_current.distance=20;official.windows.window={1,1,1};
    Preparation r;CHECK(r.advance(official)==PreparationStatus::incomplete);CHECK(r.advance(official)==PreparationStatus::incomplete);
    CHECK(r.proof.official_floor==22);official.windows.official_remaining=21;official.windows.deadline={21,21,21};
    CHECK(r.publish_into(official,d)==PreparationStatus::incomplete);CHECK(r.cursor==0);CHECK(r.restarts==1);no_authority(d);
}
static void invalid_stamps_and_time(){
    preparation_case("fresh-self-stamps-and-monotone-time");
    for(int bad=0;bad<8;++bad){
        auto o=financed();Preparation p;warm(p,o);switch(bad){
        case 0:o.resources.bag.knowledge=Knowledge::stale;break;
        case 1:o.resources.gold.knowledge=Knowledge::unknown;break;
        case 2:o.resources.bag.round=-1;break;
        case 3:o.resources.gold.round=-1;break;
        case 4:o.resources.self=false;break;
        case 5:o.resources.visible=false;break;
        case 6:o.resources.own_team=false;break;
        case 7:++o.round;break;
        }
        auto d=poisoned();CHECK(p.publish_into(o,d)==PreparationStatus::invalid);CHECK(p.cursor==0);no_authority(d);
    }
    auto o=financed();retime(o,5);Preparation p;warm(p,o);retime(o,6);Decision d;
    CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);CHECK(p.cursor==2);CHECK(p.restarts==0);
    retime(o,5);CHECK(p.publish_into(o,d)==PreparationStatus::incomplete);CHECK(p.cursor==0);CHECK(p.restarts==1);
    Preparation q;warm(q,o);o.windows.official_remaining-=1;o.windows.deadline={100,100,100};
    CHECK(q.publish_into(o,d)==PreparationStatus::incomplete);CHECK(q.cursor==2);
    ++o.windows.official_remaining;CHECK(q.publish_into(o,d)==PreparationStatus::incomplete);CHECK(q.cursor==0);CHECK(q.restarts==1);
}
static void immediate_shortcuts(){
    preparation_case("channel-and-immediate-winning-shortcuts");
    for(e::Channel channel:{e::Channel::tribute,e::Channel::recall,e::Channel::teleport}){
        auto o=fixture();o.execution.channel=channel;o.resources.self=false;o.actual_marcella=false;
        Preparation p;CHECK(p.advance(o)==PreparationStatus::complete);CHECK(p.cursor==0);Decision d;
        CHECK(p.publish_into(o,d)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));
    }
    auto o=financed();o.team_score=970;o.resources.bag.value=30;
    o.execution.adjacent_altar=o.execution.legal_winning_bank=true;o.execution.altar_id=50;o.groups[2].current=false;
    Preparation p;CHECK(p.advance(o)==PreparationStatus::complete);CHECK(p.cursor==0);Decision d;
    CHECK(p.publish_into(o,d)==PreparationStatus::complete);same_decision(d,original_choose(o,{}));CHECK(d.job==Job::winning_bank);
    o.windows.valid=false;CHECK(p.advance(o)==PreparationStatus::complete);CHECK(p.publish_into(o,d)==PreparationStatus::complete);
    same_decision(d,original_choose(o,{}));CHECK(!d.feasible);
}
static void deferred_install(Observation& o,Project& p){
    CHECK(p.pending_replan);const auto ledger=p.ledger;const auto serial=p.executor.consumed_payout;
    const auto feedback=p.executor.consumed_feedback;const bool vacate=p.must_vacate;o.ledger=ledger;
    Preparation preparation;CHECK(preparation.advance(o,p.selected.profile)==PreparationStatus::incomplete);
    settle(preparation,o,p.selected.profile);Decision d;CHECK(preparation.publish_into(o,d)==PreparationStatus::complete);
    same_decision(d,original_choose(o,p.selected.profile));install_replan(p,d);
    CHECK(!p.pending_replan);CHECK(!p.initialized);CHECK(p.index==0);CHECK(p.intent==0);CHECK(p.stage==Stage::running);
    CHECK(p.ledger.captures==ledger.captures);CHECK(p.ledger.banks==ledger.banks);CHECK(p.ledger.purchases==ledger.purchases);
    CHECK(p.ledger.consumed==ledger.consumed);CHECK(p.ledger.liquidated==ledger.liquidated);
    CHECK(p.must_vacate==vacate);CHECK(p.executor.consumed_payout==serial);CHECK(p.executor.consumed_feedback==feedback);
}
static void deferred_caller(){
    current_case="deferred-executor-receipts-and-install";auto o=financed();auto& x=o.execution;
    x.at_target=x.group_live_known=x.target_visible=x.target_category_legal=x.target_in_range=true;
    x.group_id=1;x.attack_target=101;x.capture_duration=0;Facts f;f.navigation_round=0;f.navigation_goal=1;
    auto p=start_project(choose_job(o,{Arm::bank_each_control}),f);
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::attack);
    retime(o,1);f.navigation_round=1;x.group_cleared=x.on_group_spawn=true;o.groups[0].monsters[0].observed_health=0;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::hold);
    retime(o,2);f.navigation_round=2;o.resources.bag.value=30;x.own_payout_observed=true;x.own_payout_group=1;
    x.own_gems_received=30;x.payout_receipt=f.capture_receipt=1;const auto old_prefix=p.selected.selected.prefix;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);
    CHECK(p.pending_replan);CHECK(p.ledger.captures==1);CHECK(p.ledger.consumed==1);CHECK(p.must_vacate);
    CHECK(p.executor.consumed_payout==1);CHECK(p.selected.selected.prefix==old_prefix);
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);CHECK(p.ledger.captures==1);
    deferred_install(o,p);f.navigation_goal=50;f.moves_leave_region=true;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::vacate);CHECK(p.executor.consumed_payout==1);
    retime(o,3);f.navigation_round=3;x.own_payout_observed=x.on_group_spawn=false;x.adjacent_altar=true;x.altar_id=50;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::start_tribute);
    retime(o,6);f.navigation_round=6;o.resources.bag.value=0;o.resources.gold.value=60;o.team_score=30;
    f.bank_confirmed=true;f.bank_receipt=1;f.bank_gold=60;f.banked_gems=30;f.bank_altar=50;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);CHECK(p.pending_replan);CHECK(p.ledger.banks==1);
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);CHECK(p.ledger.banks==1);deferred_install(o,p);
    CHECK(p.selected.selected.purchase==0);f.navigation_goal=60;f.gear_item=7;f.shard_equivalent_count=4;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::move);
    retime(o,7);f.navigation_round=7;x.at_spawn=true;x.adjacent_altar=false;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::yield);
    retime(o,8);f.navigation_round=8;f.recovery_receipt=1;f.recovery_round=8;x.spawn_recovery_observed=true;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::buy);
    o.resources.gold.value=30;o.actor.strength=20;f.purchase_receipt=1;f.purchase_confirmed=true;
    f.purchased_item=7;f.purchased_family=0;f.purchased_cost=30;f.purchased_shards=1;f.shard_equivalent_count=5;
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);CHECK(p.pending_replan);CHECK(p.ledger.purchases==1);
    CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::refresh);CHECK(p.ledger.purchases==1);deferred_install(o,p);
    CHECK(p.executor.consumed_payout==1);CHECK(p.ledger.captures==1 && p.ledger.banks==1 && p.ledger.purchases==1);
    f.navigation_goal=2;CHECK(execute_job(o,f,p,false,true).action.kind==e::ActionKind::move);
}
static void old_api_and_reset(){
    preparation_case("old-public-api-and-explicit-reset");const auto o=financed();
    same_candidates(o,{});same_quote(best(o,{}),original_best(o,{}));same_decision(choose_job(o),original_choose(o,{}));
    CHECK(renewal_certificate(o,o.actor,200));auto actor=o.actor;
    Stock stock{actor,0,0,0,0,current_location,{}};Quote result;CHECK(finish(o,stock,result));
    Preparation p;warm(p,o);auto changed=o;++changed.team_score;Decision d;
    CHECK(p.publish_into(changed,d)==PreparationStatus::incomplete);const auto restarts=p.restarts;p.reset();
    CHECK(p.status==PreparationStatus::invalid);CHECK(p.cursor==0);CHECK(p.proof.game_floor==0);CHECK(p.proof.official_floor==0);CHECK(p.restarts==restarts);
    d=poisoned();CHECK(p.publish_into(o,d)==PreparationStatus::invalid);no_authority(d);
    CHECK(p.advance(o)==PreparationStatus::incomplete);CHECK(p.cursor==0);CHECK(p.restarts==restarts);
}
int main(int argc,char** argv){
    focused_static();advancing_rounds();partial_publication();physical_key_mutations();domain_and_profile();
    ranking_windows();horizon_floors();invalid_stamps_and_time();immediate_shortcuts();old_api_and_reset();deferred_caller();
    const bool focused=argc>1 && std::strcmp(argv[1],"--focused")==0;
    if(!focused){boundary_cases();ledger_cases();invalid_cases();terminal_cases();property_cases();}
    std::printf("Runoff preparation: %d focused groups, %d physical mutations, %d catalogue observations, both arms, %d semantic checks passed\n",
        preparation_groups,preparation_mutations,samples,checks);return 0;
}


