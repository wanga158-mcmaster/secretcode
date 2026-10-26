#include "../src/production_job.h"
#include "../src/production_calendar.h"
#include <cstdio>
#include <memory>
#include <utility>

namespace {
namespace pj=tt::production_job;namespace pc=tt::production_calendar;namespace sm=mb::service;
int checks{},failures{};
void check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
pj::Leg path(std::initializer_list<pj::Point> points,std::uint32_t handoff=1) {
    pj::Leg out;out.complete=out.disjoint_supported=true;out.handoff_inputs=handoff;
    for(const auto p:points)out.points.push_back({p,0});return out;
}
pj::Input input(float hp=100,std::uint32_t horizon=70) {
    pj::Input in;in.start=10;in.horizon=horizon;in.key={0,0,7,3,4,5,6};
    auto& self=in.self;self.actor=0;self.body_revision=3;self.epoch=10;
    self.own=self.complete=self.stable_outgoing=true;self.body.pos={1,2};self.body.hp=hp;self.body.max_hp=100;
    self.body.strength=self.raw_strength=100;self.body.focus=self.raw_focus=100;self.gold=30;
    auto& job=in.job;job.group=0;job.generation=7;job.life_revision=4;job.geometry_revision=5;job.word_revision=6;
    job.life=pj::Life::Alive;job.life_supported=true;job.observed_round=job.available_from=10;
    job.perch={2,2};job.width=8;job.height=8;job.member_count=1;
    job.members[0]={0,0,{3,2},1,0,1,false,0,true};job.full_hp[0]=1;
    job.gems=21;job.capture_payout_inputs=6;job.respawn_inputs=100;job.spawn_healing=.25f;job.heal_inputs=4;
    job.full_group_known=job.continuation_supported=job.external_sources_complete=true;
    job.initial=path({{1,2},{2,2}});job.capture=path({{2,2},{3,2}},0);
    job.release=path({{3,2},{2,2}});job.bank=path({{2,2},{1,3}},0);
    job.renewal=path({{1,3},{2,2}});job.home=path({{1,3},{0,3}});job.redeploy=path({{0,3},{1,2},{2,2}});
    // Immutable external source support: stand(1,3) is charged10, home(0,3)
    // and all service/capture/release poses are quiet. No selected-Reward or
    // temporary shield discount is applied to this field.
    for(auto* leg:{&job.initial,&job.capture,&job.release,&job.bank,&job.renewal,&job.home,&job.redeploy})
        for(auto& step:leg->points)if(step.point==pj::Point{1,3})step.raw_upper=10;
    return in;
}
std::shared_ptr<pj::Producer> price(pj::Input in) {
    auto producer=std::make_shared<pj::Producer>(std::move(in));
    for(unsigned work=0;work<200000&&producer->status()==pj::Status::NeedCompute;++work) {
        producer->advance(1);check(producer->last_work()<=1,"a channel/recovery unit retains the requested finite work slice");
    }
    check(producer->status()!=pj::Status::NeedCompute,"declared charged-cash fixture terminates within the structural bound");return producer;
}
void default_rejection_and_actual_batches() {
    auto baseline=input();const auto old=price(baseline);
    check(old->status()==pj::Status::Unsupported&&old->result().reason==pj::Reason::BankHazard&&!old->cash_plan(),
        "default QuietOnly still rejects the same positive bank stand without allocating charged metadata");
    auto fragile=input(35);fragile.job.bank_policy=pj::BankPolicy::ChargedHome;const auto refused=price(fragile);
    const auto* plan=refused->cash_plan();
    check(plan&&plan->hp_at_bank==25&&plan->channel_ticks==3&&plan->channel_paid==2&&
        refused->status()==pj::Status::Unsupported&&refused->result().reason==pj::Reason::Survival&&
        refused->result().banked_gems_by_H==0&&refused->result().carried_gems==21&&!plan->banked,
        "actual HP25 at a raw10 stand cannot survive three channel batches despite paying separate entry/refusal costs");
    fragile=input(25);fragile.job.bank_policy=pj::BankPolicy::ChargedHome;const auto entry=price(fragile);
    check(entry->cash_plan()&&entry->cash_plan()->hp_at_bank==15&&entry->cash_plan()->channel_paid==1&&
        entry->result().banked_gems_by_H==0,"the entry payment is not reused as payment for later channel inputs");
    auto safe=input();safe.job.bank_policy=pj::BankPolicy::ChargedHome;const auto completed=price(safe);plan=completed->cash_plan();
    check(completed->status()==pj::Status::Ready&&plan&&plan->banked&&plan->first_cash_complete&&
        plan->bank_arrival==22&&plan->bank_complete==25&&plan->channel_ticks==3&&plan->channel_paid==3&&
        plan->hp_at_bank==90&&plan->hp_after_bank==60,
        "the complete safe channel debits exactly three raw10 batches before awarding the bank event");
    check(plan&&plan->home_reached&&plan->home_arrival==27&&plan->hp_at_home==60&&plan->healed&&plan->heal_complete==29&&
        plan->hp_recovered==100&&!plan->departed&&!plan->redeployed&&plan->renewal_enabler==pj::Enabler::None&&
        completed->result().banked_gems_by_H==21&&completed->result().terminal_body.hp==100&&
        completed->result().purchase_calls==0&&completed->result().owned_gold==30,
        "even a non-repeating cash programme pays home travel and actual quiet healing, with no invented item/wallet gain");
    auto cannot_exit=input(50);cannot_exit.job.bank_policy=pj::BankPolicy::ChargedHome;const auto trapped=price(cannot_exit);
    check(trapped->status()==pj::Status::Unsupported&&trapped->cash_plan()->banked&&!trapped->cash_plan()->home_reached&&
        trapped->result().reason==pj::Reason::Survival,
        "surviving the channel alone does not fund the complete paid home/refusal obligation");
}
void horizon_and_no_phantom_completion() {
    auto partial=input(35,23);partial.job.bank_policy=pj::BankPolicy::ChargedHome;const auto cut=price(partial);
    check(cut->status()==pj::Status::Ready&&cut->result().banked_gems_by_H==0&&cut->result().carried_gems==21&&
        !cut->result().terminal_resources_available&&cut->result().terminal_body.hp==15&&
        cut->cash_plan()->channel_paid==1&&!cut->cash_plan()->banked&&!cut->cash_plan()->first_cash_complete,
        "H inside the channel prices only elapsed batches and never borrows a future bank or future death");
    auto exact=input(100,25);exact.job.bank_policy=pj::BankPolicy::ChargedHome;const auto at_bank=price(exact);
    check(at_bank->status()==pj::Status::Ready&&at_bank->result().banked_gems_by_H==21&&at_bank->result().carried_gems==0&&
        at_bank->cash_plan()->bank_complete==25&&at_bank->cash_plan()->banked&&!at_bank->cash_plan()->home_reached&&
        !at_bank->cash_plan()->first_cash_complete&&!at_bank->result().terminal_resources_available,
        "a bank completed exactly at H is earned while unfinished paid exit remains explicitly incomplete");
    auto healing=input(100,28);healing.job.bank_policy=pj::BankPolicy::ChargedHome;const auto mid_heal=price(healing);
    check(mid_heal->status()==pj::Status::Ready&&mid_heal->result().banked_gems_by_H==21&&
        mid_heal->cash_plan()->home_reached&&!mid_heal->cash_plan()->healed&&!mid_heal->cash_plan()->first_cash_complete&&
        mid_heal->result().terminal_body.hp==85&&!mid_heal->result().terminal_resources_available,
        "a horizon inside healing retains actual partial HP instead of a free full-health readback");
    auto awaiting=input(100,100);awaiting.job.bank_policy=pj::BankPolicy::ChargedHome;awaiting.job.repeat=true;
    const auto quiet_wait=price(awaiting);
    check(quiet_wait->status()==pj::Status::Ready&&quiet_wait->result().banked_gems_by_H==21&&
        quiet_wait->cash_plan()->healed&&!quiet_wait->cash_plan()->departed&&!quiet_wait->cash_plan()->redeployed&&
        !quiet_wait->cash_plan()->first_cash_complete&&quiet_wait->result().terminal_body.hp==100,
        "full-H value can be complete during quiet waiting while the first paid recovery-and-return programme remains unfinished");
}
void quiet_recovery_renewal_and_guards() {
    auto repeated=input(100,150);repeated.job.bank_policy=pj::BankPolicy::ChargedHome;repeated.job.repeat=true;
    const auto producer=price(repeated);const auto& quote=producer->result();const auto* plan=producer->cash_plan();
    check(producer->status()==pj::Status::Ready&&plan&&plan->first_cash_complete&&plan->renewal_not_before==119&&
        plan->heal_complete==29&&plan->departure==116&&plan->redeploy_complete==119&&plan->redeployed&&
        plan->renewal_enabler==pj::Enabler::ObserveRenewal&&quote.renewal_enabler==pj::Enabler::ObserveRenewal&&quote.cycles>=2,
        "long respawn wait stays at quiet recovered home, with paid redeployment arriving at the nominal fresh-Life enabler date");
    pc::Index calendar{{quote.events,{quote.start,quote.horizon,quote.initial_carried},producer,true}};
    for(unsigned work=0;work<100000&&calendar.status()==pc::Status::NeedCompute;++work)calendar.advance(1);
    const auto value=calendar.query(quote.horizon-quote.start);
    check(calendar.status()==pc::Status::Ready&&value.banked==quote.banked_gems_by_H&&value.carried==quote.carried_gems,
        "new charged cash dates preserve the existing exact checked event/calendar contract");
    auto unquiet=input();unquiet.job.bank_policy=pj::BankPolicy::ChargedHome;unquiet.job.home.points.back().raw_upper=1;
    const auto invalid_home=price(unquiet);
    check(invalid_home->status()==pj::Status::Unsupported&&invalid_home->result().reason==pj::Reason::RecoveryHazard,
        "unsupported exposed healing cannot masquerade as quiet spawn recovery");
    auto shopping=input();shopping.job.bank_policy=pj::BankPolicy::ChargedHome;shopping.job.purchase_when_home=true;
    const auto unsupported_shop=price(shopping);
    check(unsupported_shop->status()==pj::Status::Unsupported&&unsupported_shop->result().reason==pj::Reason::Shop,
        "the new NoPurchase family does not silently execute an unimplemented SelectedShop contract");
    auto insufficient=input();insufficient.job.bank_policy=pj::BankPolicy::ChargedHome;insufficient.job.heal_inputs=1;
    const auto no_heal=price(insufficient);
    check(no_heal->status()==pj::Status::Incomplete&&no_heal->result().reason==pj::Reason::Survival&&
        no_heal->cash_plan()->home_reached&&!no_heal->cash_plan()->first_cash_complete,
        "an insufficient declared recovery allowance cannot invent full HP");
}
void unmodelled_shield_cannot_authorize_a_channel() {
    auto value=input(35);value.self.body.shield=1000;value.job.bank_policy=pj::BankPolicy::ChargedHome;
    const auto producer=price(value);
    check(producer->status()==pj::Status::Unsupported&&producer->result().reason==pj::Reason::Word&&
        producer->result().banked_gems_by_H==0&&producer->cash_plan()&&!producer->cash_plan()->channel_started,
        "an unpriced active shield with unknown expiry cannot authorize a raw-HP-unsafe cash programme");
}
}
int main() {
    default_rejection_and_actual_batches();horizon_and_no_phantom_completion();
    quiet_recovery_renewal_and_guards();unmodelled_shield_cannot_authorize_a_channel();
    std::printf("production charged bank: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
