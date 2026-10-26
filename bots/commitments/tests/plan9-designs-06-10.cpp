#include "../dividend-redeployment/src/live-policy.h"
#include "../decoupled-prizes/src/live-policy.h"
#include "../bounty-syndicate/src/live-policy.h"
#include "../spent-response-exchange/src/live-policy.h"
#include "../prepaid-column/src/live-policy.h"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace p9::live;
int checks=0;
void check(bool ok,const char* description) {
    ++checks;if(!ok) {std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
Actor actor(int pick,Champion type) {
    Actor a;a.id=pick;a.pick=pick;a.type=type;a.visible=a.alive=a.ally=true;
    a.can_move=a.can_attack=a.can_cast=true;a.at_spawn=true;a.position={0,pick};
    a.hp=a.max_hp=1000;a.strength=100;a.focus=100;a.range=1;a.cooldown={0,0};return a;
}
World fixture(const Profile& profile,int pick=0) {
    World w;w.round=10;w.picks=3;w.max_rounds=2000;w.win_gems=1000;w.capture_turns=5;
    w.defense_cost=5;w.own_gold=10;w.safe_now=true;w.windows={120,120,120};
    w.tribute_multiplier=.1;w.tribute_minimum=1;w.productive_group=10;
    w.self=actor(pick,profile.draft[pick]);
    for(int i=0;i<3;++i) if(i!=pick) w.allies[w.ally_count++]=actor(i,profile.draft[i]);
    w.group_count=3;
    for(int i=0;i<3;++i) {
        auto& g=w.groups[i];g.id=10*(i+1);g.position={4+i,0};g.approach=g.position;
        g.observed=g.route_known=g.safe_solo=g.living=true;g.travel=1+i;g.to_bank=1;
        g.hp=20;g.incoming=1;g.clear_turns=1;
        g.gems=i==0?10:i==1?100:30;g.gold=i==0?100:i==1?10:5;
    }
    w.altar_count=1;w.altars[0].id=99;w.altars[0].position={1,0};
    w.altars[0].visible=w.altars[0].route_known=true;w.altars[0].travel=1;
    w.route_count=3;
    for(int i=0;i<3;++i) {w.routes[i].actor=i;w.routes[i].altars[0]=1;
        for(int g=0;g<3;++g) w.routes[i].groups[g]=1+g;}
    return w;
}
Actor& ally(World& w,int pick) {
    if(w.self.pick==pick) return w.self;
    for(int i=0;i<w.ally_count;++i) if(w.allies[i].pick==pick) return w.allies[i];
    std::abort();
}
void dividend() {
    using p9::dividend_redeployment::Policy;const auto profile=p9::dividend_redeployment::profile;
    auto w=fixture(profile);Policy active,control;
    check(active.step(w,Arm::active).task==Task::independent,"06 predicted tribute cannot fund a purchase");
    w.own_bank_completed=true;w.bank_lower=10;w.last_bank_round=w.round;
    check(active.step(w,Arm::active).task==Task::buy_defense&&control.step(w,Arm::control).task==Task::independent,
        "06 actual bank and current personal gold change active role investment");
    ++w.round;w.own_bank_completed=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;w.self.defense_shards=1;
    auto d=active.step(w,Arm::active);
    check(d.task==Task::support&&d.actor==1,"06 actual purchase changes Marcella from collector to Varg support");
    ++w.round;ally(w,1).bag=20;
    check(active.step(w,Arm::active).task==Task::support,"06 existing bag remains with Varg while Marcella helps");
    ++w.round;ally(w,1).bag=0;
    check(active.step(w,Arm::active).task==Task::farm,"06 released collector returns Marcella to original production without remote bank claim");
    auto poor=fixture(profile);poor.own_bank_completed=true;poor.bank_lower=10;poor.last_bank_round=poor.round;poor.own_gold=4;
    Policy unfunded;check(unfunded.step(poor,Arm::active).task==Task::independent,"06 no purchase on unearned or insufficient funds");
    auto collector=fixture(profile,1);ally(collector,0).defense_shards=1;Policy varg;
    collector.groups[0].cleared=true;collector.groups[0].living=false;
    check(varg.step(collector,Arm::active).task==Task::capture,"06 Varg becomes future payout owner from visible developed proposal");
}
void prizes() {
    using p9::decoupled_prizes::Policy;const auto profile=p9::decoupled_prizes::profile;
    auto w=fixture(profile);Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::farm&&a.group==10&&c.task==Task::farm&&c.group==20,
        "07 active separates gold-heavy owner from control's whole-score-rich principal cycle");
    w.own_payout=true;w.last_payout_group=10;w.last_payout_round=w.round;
    check(active.step(w,Arm::active).task==Task::buy_defense,"07 actual gold-source payout funds Tavi's own development");
    ++w.round;w.own_payout=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;w.self.defense_shards=1;
    const auto guard=active.step(w,Arm::active);
    check(guard.task==Task::support&&guard.actor==1&&guard.group==20,"07 developed Tavi takes combat duty for different gem owner");
    auto converter=fixture(profile,1);Policy marcella;
    check(marcella.step(converter,Arm::active).group==20,"07 Marcella owns complete gem-heavy source, with no currency transfer");
    auto equal=fixture(profile);equal.group_count=2;equal.groups[0].gold=10;equal.groups[0].gems=10;
    equal.groups[1].gold=100;equal.groups[1].gems=100;Policy missing;
    check(missing.step(equal,Arm::active).task==Task::independent,"07 absent source-composition difference blocks organization");
    auto zero=fixture(profile);zero.groups[0].gems=0;Policy infinite;
    check(infinite.step(zero,Arm::active).group==10,"07 positive gold and zero gems has defined infinite ratio");
    auto speculative=fixture(profile);speculative.own_gold=100;Policy unearned;
    check(unearned.step(speculative,Arm::active).task!=Task::buy_defense,"07 prospective gold-heavy reward cannot fund development");
    auto lifecycle=fixture(profile);Policy captured;
    check(captured.step(lifecycle,Arm::active).group==10,"07 realistic lifecycle latches initial gold source before capture");
    ++lifecycle.round;lifecycle.groups[0].respawning=true;lifecycle.groups[0].living=false;
    lifecycle.groups[0].cleared=false;lifecycle.self.bag=10;lifecycle.own_payout=true;
    lifecycle.last_payout_group=10;lifecycle.last_payout_round=lifecycle.round;
    check(captured.step(lifecycle,Arm::active).task==Task::bank,"07 payout source respawns while actual collector banks its received bag");
    ++lifecycle.round;lifecycle.own_payout=false;lifecycle.self.bag=0;lifecycle.own_bank_completed=true;
    lifecycle.last_bank_round=lifecycle.round;
    check(captured.step(lifecycle,Arm::active).task==Task::buy_defense,"07 latched actual payout funds purchase after gold source becomes respawning");
    ++lifecycle.round;lifecycle.own_purchase_receipt=true;lifecycle.last_purchase_round=lifecycle.round;
    lifecycle.self.defense_shards=1;
    const auto respawn_guard=captured.step(lifecycle,Arm::active);
    check(respawn_guard.task==Task::support&&respawn_guard.actor==1&&respawn_guard.group==20,
        "07 gold source remains respawning while developed Tavi changes to separate gem-source combat duty");
    auto zero_lifecycle=fixture(profile);zero_lifecycle.groups[0].gems=0;Policy gold_only;
    check(gold_only.step(zero_lifecycle,Arm::active).group==10,"07 gold-only lifecycle latches its positive-gold source");
    ++zero_lifecycle.round;zero_lifecycle.groups[0].respawning=true;zero_lifecycle.groups[0].living=false;
    zero_lifecycle.own_payout=true;zero_lifecycle.last_payout_group=10;zero_lifecycle.last_payout_round=zero_lifecycle.round;
    check(gold_only.step(zero_lifecycle,Arm::active).task==Task::buy_defense,
        "07 actual gold-only payout reaches development without requiring a gem-bag increase");
    auto unavailable=fixture(profile);Policy no_receipt;
    no_receipt.step(unavailable,Arm::active);++unavailable.round;unavailable.groups[0].respawning=true;
    unavailable.own_gold=100;
    check(no_receipt.step(unavailable,Arm::active).task==Task::independent,
        "07 unavailable gold source without personal payout neither credits development nor repeats capture");
    unavailable.own_payout=true;unavailable.last_payout_group=20;unavailable.last_payout_round=unavailable.round;
    check(no_receipt.step(unavailable,Arm::active).task==Task::independent,
        "07 another source payout cannot replace the latched gold-source receipt");
}
#if P9_DECOUPLED_INTEGER_RATIOS
void reward_composition_order() {
    constexpr Gems rewards[]{0,1,60,4294967294LL,4294967295LL};
    for(const auto ag:rewards) for(const auto am:rewards)
        for(const auto bg:rewards) for(const auto bm:rewards) {
            Group a,b;a.gold=ag;a.gems=am;b.gold=bg;b.gems=bm;
            const long double old_a=am==0?std::numeric_limits<long double>::infinity():static_cast<long double>(ag)/am;
            const long double old_b=bm==0?std::numeric_limits<long double>::infinity():static_cast<long double>(bg)/bm;
            const int expected=(old_a>old_b)-(old_a<old_b);
            check(p9::decoupled_prizes::compare_reward_composition(a,b)==expected,
                "07 integer composition preserves SDK-width ratio order, ties and zero-gem infinity");
        }
}
#endif
void bounty() {
    using p9::bounty_syndicate::Policy;const auto profile=p9::bounty_syndicate::profile;
    auto w=fixture(profile);w.enemy_count=1;w.enemies[0]=actor(9,Champion::tavi);
    w.enemies[0].ally=false;w.enemies[0].position={1,0};w.enemies[0].bag=20;w.enemies[0].hp=50;w.enemies[0].strength=10;
    Policy active,control;
    check(active.step(w,Arm::active).task==Task::intercept&&control.step(w,Arm::control).task==Task::intercept,
        "08 both arms take the same favorable local fight without promised credit");
    ++w.round;w.enemy_count=0;w.own_kill_receipt=true;w.last_kill_round=w.round;w.last_kill_target=9;
    check(active.step(w,Arm::active).task==Task::buy_defense&&control.step(w,Arm::control).task==Task::independent,
        "08 only actual Vesper killer receipt funds changed post-fight assignment");
    ++w.round;w.own_kill_receipt=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;w.self.defense_shards=1;
    w.own_bank_completed=true;w.last_bank_round=w.round;
    const auto expanded=active.step(w,Arm::active);
    check(expanded.task==Task::farm&&expanded.group!=w.productive_group,
        "08 purchased killer expands to new productive source; prior loot bank does not finish expansion");
    auto no_credit=fixture(profile);no_credit.last_kill_target=9;no_credit.last_kill_round=no_credit.round;no_credit.own_gold=100;
    Policy assistant;check(assistant.step(no_credit,Arm::active).task==Task::independent,"08 assist or another actor's bounty is not a killer receipt");
    no_credit.own_kill_receipt=true;no_credit.last_kill_round-=3;Policy stale;
    check(stale.step(no_credit,Arm::active).task==Task::independent,"08 stale unrelated kill history cannot admit expansion");
}
World response_world() {
    auto w=fixture(p9::spent_response_exchange::profile);ally(w,1).bag=10;
    w.enemy_count=1;w.enemies[0]=actor(9,Champion::tavi);w.enemies[0].ally=false;
    w.enemies[0].cooldown={0,0};w.response_window_verified=true;w.verified_response_enemy=9;w.verified_response_until=100;
    return w;
}
void expenditure() {
    using p9::spent_response_exchange::Policy;
    auto w=response_world();Policy active,control;
    check(active.step(w,Arm::active).task==Task::independent&&control.step(w,Arm::control).task==Task::support,
        "09 control deploys on own feasibility while active waits for actual queried expenditure");
    ++w.round;w.enemies[0].cooldown={8,8};
    check(active.step(w,Arm::active).task==Task::support,"09 same rival fresh ready-to-cooldown transition admits protected conversion");
    w.response_window_verified=false;
    check(active.step(w,Arm::active).task==Task::independent,"09 loss of threat coverage immediately removes protected-window claim");
    auto rejected=[](int mode) {
        Policy p;auto x=response_world();p.step(x,Arm::active);++x.round;x.enemies[0].cooldown={8,8};
        if(mode==0) x.enemies[0].type=Champion::vendetta;
        if(mode==1) x.enemies[0].cooldown[1]=0;
        if(mode==2) x.enemies[0].cooldown[1]=-1;
        if(mode==3) ++x.round;
        if(mode==4) x.enemies[0].id=8;
        if(mode==5) x.extra_opposition=true;
        if(mode==6) ally(x,1).bag=100;
        if(mode==7) x.response_window_verified=false;
        return p.step(x,Arm::active).task==Task::independent;
    };
    check(rejected(0),"09 Vendetta reset opportunities disqualify lasting cooldown window");
    check(rejected(1),"09 another ready response invalidates a protected window");
    check(rejected(2),"09 unknown cooldown cannot be treated as unavailable response");
    check(rejected(3),"09 missing observation round cannot establish fresh expenditure");
    check(rejected(4),"09 different rival cannot inherit the prior ready observation");
    check(rejected(5),"09 unrepresented opposition invalidates expenditure window");
    check(rejected(6),"09 critical conversion stage must finish before currently queried cooldown");
    check(rejected(7),"09 unknown substitutes cannot be silently excluded");
}
void prepaid() {
    using p9::prepaid_column::Policy;const auto profile=p9::prepaid_column::profile;
    auto w=fixture(profile);ally(w,1).hp=800;w.column_without_healer_verified=true;w.verified_column_group=20;
    Policy active,control;
    check(active.step(w,Arm::active).task==Task::heal&&control.step(w,Arm::control).task==Task::heal,
        "10 both arms restore the same actual missing Varg health");
    ++w.round;w.own_heal_receipt=true;w.last_heal_round=w.round;w.last_heal_actor=1;w.last_heal_amount=100;
    ally(w,1).hp=900;w.self.cooldown[0]=7;
    check(active.step(w,Arm::active).task==Task::support&&control.step(w,Arm::control).task==Task::support,
        "10 actual heal alone does not prove safe pair departure");
    ++w.round;w.own_heal_receipt=false;ally(w,1).position={2,0};ally(w,2).position={2,1};
    check(active.step(w,Arm::active).task==Task::farm&&control.step(w,Arm::control).task==Task::support,
        "10 witnessed unsupported-safe departure releases active Willow while control remains escort");
    auto no_receipt=fixture(profile);ally(no_receipt,1).hp=900;no_receipt.column_without_healer_verified=true;
    no_receipt.verified_column_group=20;no_receipt.self.can_cast=false;Policy predicted;
    check(predicted.step(no_receipt,Arm::active).task==Task::independent,"10 predicted health or future heal cannot license release");
    auto unsafe=fixture(profile);unsafe.own_heal_receipt=true;unsafe.last_heal_round=unsafe.round;
    unsafe.last_heal_actor=1;unsafe.last_heal_amount=100;unsafe.verified_column_group=20;
    unsafe.column_without_healer_verified=false;Policy no_route;
    check(no_route.step(unsafe,Arm::active).task==Task::independent,"10 actual heal cannot replace unverified unsupported complete routes");
    auto zero=unsafe;zero.column_without_healer_verified=true;zero.last_heal_amount=0;zero.self.can_cast=false;Policy overheal;
    check(overheal.step(zero,Arm::active).task==Task::independent,"10 zero restored health does not count as prepaid capability");
}
void campaign_caps() {
    using p9::decoupled_prizes::Policy;
    auto w=fixture(p9::decoupled_prizes::profile);Policy p;
    check(p.step(w,Arm::active).task==Task::farm,"07 cap fixture starts first admitted campaign");
    for(int offset=1;offset<15;++offset) {
        w.round=10+offset;
        check(p.step(w,Arm::active).task==Task::farm,"07 bounded source work continues before sixteenth occupied turn");
    }
    w.round=25;
    check(p.step(w,Arm::active).task==Task::independent,"07 sixteenth campaign turn retires strategic deployment");
    w.round=26;w.own_payout=true;w.last_payout_group=10;w.last_payout_round=26;
    check(p.step(w,Arm::active).task==Task::buy_defense,"07 receipt can fund the epoch's second bounded attempt");
    w.own_payout=false;w.round=41;
    check(p.step(w,Arm::active).task==Task::independent,"07 second attempt still retires at sixteen turns");
    w.round=42;
    check(p.step(w,Arm::active).task==Task::independent,"07 two spent attempts hold until the next epoch");
    w.own_payout=false;w.round=65;
    check(p.step(w,Arm::active).task==Task::buy_defense,"07 second epoch permits one second funded campaign");
    w.round=80;
    check(p.step(w,Arm::active).task==Task::independent,"07 failed purchase retries still consume bounded campaign turns");
    w.round=129;
    check(p.step(w,Arm::active).task==Task::buy_defense,"07 third epoch renews funded activity without a lifetime lockout");
}
void funded_purchase_targets() {
    {
        auto w=fixture(p9::dividend_redeployment::profile);w.self.defense_shards=5;
        w.own_bank_completed=true;w.bank_lower=10;w.last_bank_round=w.round;
        p9::dividend_redeployment::Policy policy;
        auto purchase=policy.step(w,Arm::active);
        check(purchase.task==Task::buy_defense&&purchase.purchase_target==6,"06 already-equipped owner requests a new funded shard");
        ++w.round;w.own_bank_completed=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;
        check(policy.step(w,Arm::active).task==Task::buy_defense,"06 unrelated receipt without the requested equipment cannot change roles");
        ++w.round;w.last_purchase_round=w.round;w.self.defense_shards=6;
        check(policy.step(w,Arm::active).task==Task::support,"06 newly observed equipment completes the funded role exchange");
    }
    {
        auto w=fixture(p9::decoupled_prizes::profile);w.self.defense_shards=5;
        p9::decoupled_prizes::Policy policy;(void)policy.step(w,Arm::active);
        ++w.round;w.own_payout=true;w.last_payout_group=10;w.last_payout_round=w.round;
        auto purchase=policy.step(w,Arm::active);
        check(purchase.task==Task::buy_defense&&purchase.purchase_target==6,"07 existing equipment does not turn funded development into a no-op");
        ++w.round;w.own_payout=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;
        check(policy.step(w,Arm::active).task==Task::buy_defense,"07 old equipment plus a different receipt is insufficient");
        ++w.round;w.last_purchase_round=w.round;w.self.defense_shards=6;
        check(policy.step(w,Arm::active).task==Task::support,"07 exact new equipment permits separate-owner protection");
    }
    {
        auto w=fixture(p9::bounty_syndicate::profile);w.self.defense_shards=5;
        w.own_kill_receipt=true;w.last_kill_target=9;w.last_kill_round=w.round;
        p9::bounty_syndicate::Policy policy;
        auto purchase=policy.step(w,Arm::active);
        check(purchase.task==Task::buy_defense&&purchase.purchase_target==6,"08 actual killer capital buys beyond the first equipment bundle");
        ++w.round;w.own_kill_receipt=false;w.own_purchase_receipt=true;w.last_purchase_round=w.round;
        check(policy.step(w,Arm::active).task==Task::buy_defense,"08 unrelated purchase receipt does not certify the requested investment");
        ++w.round;w.last_purchase_round=w.round;w.self.defense_shards=6;
        check(policy.step(w,Arm::active).task==Task::farm,"08 observed investment permits productive expansion");
    }
}
}
int main() {
#if P9_DECOUPLED_INTEGER_RATIOS
    reward_composition_order();
#endif
    dividend();prizes();bounty();expenditure();prepaid();campaign_caps();funded_purchase_targets();
    std::printf("plan9-designs-06-10: %d checks passed\n",checks);return 0;
}
