#include "../src/production_job.h"
#include <cstdio>
#include <utility>

namespace {
namespace pj=tt::production_job;
namespace pi=tt::production_investment;
namespace sm=mb::service;
int checks{},failures{};
void check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
pj::Leg path(std::initializer_list<pj::Point> points,std::uint32_t handoff=1)
{
    pj::Leg result;result.complete=result.disjoint_supported=true;result.handoff_inputs=handoff;
    for(auto point:points)result.points.push_back({point,0});return result;
}
pj::Input input(std::uint32_t horizon=200)
{
    pj::Input value;value.start=10;value.horizon=horizon;
    value.key={0,0,7,3,4,5,6};auto& self=value.self;
    self.actor=0;self.body_revision=3;self.epoch=10;self.own=self.complete=self.stable_outgoing=true;
    self.body.pos={1,2};self.body.hp=self.body.max_hp=1000;self.body.strength=100;self.body.focus=100;
    self.raw_strength=self.raw_focus=100;self.body.attack_range=1;self.body.camp_damage_multiplier=1;
    auto& job=value.job;job.group=0;job.generation=7;job.life_revision=4;job.geometry_revision=5;job.word_revision=6;
    job.life=pj::Life::Alive;job.life_supported=true;job.observed_round=10;job.available_from=10;
    job.perch={2,2};job.width=8;job.height=8;job.member_count=1;
    job.members[0]={0,0,{3,2},201,0,1,false,0,true};job.full_hp[0]=201;
    job.gems=11;job.gold=0;job.capture_payout_inputs=6;job.respawn_inputs=20;
    job.full_group_known=job.continuation_supported=job.external_sources_complete=true;
    job.initial=path({{1,2},{2,2}});job.capture=path({{2,2},{3,2}},0);
    job.release=path({{3,2},{2,2}});job.bank=path({{2,2},{1,3}});
    job.renewal=path({{1,3},{2,2}});job.home=path({{1,3},{0,3}});
    job.redeploy=path({{0,3},{1,2},{2,2}});
    return value;
}
pj::Quote price(pj::Input value)
{
    pj::Producer producer{std::move(value)};
    for(unsigned units=0;units<100000 && producer.status()==pj::Status::NeedCompute;++units)
    {producer.advance(1);check(producer.last_work()<=1,"one advance unit never hides a whole selection/cycle loop");}
    check(producer.status()!=pj::Status::NeedCompute,"finite declared fixture completes within its structural limit");
    return producer.result();
}
void complete_calendar_and_late_custody()
{
    const auto complete=price(input());
    check(complete.status==pj::Status::Ready && complete.banked_gems_by_H==11 && complete.carried_gems==0 &&
        complete.last_clear==14 && complete.last_payout==21 && complete.last_release==23 && complete.last_bank==27,
        "actual compatible basics, paid capture/release/altar path and tribute create a complete dated job");
    const auto late=price(input(26));
    check(late.status==pj::Status::Ready && late.banked_gems_by_H==0 && late.carried_gems==11 && late.last_payout==21,
        "a collected late bag is carried resource, never banked score at H");
    check(!late.terminal_resources_available,"a horizon cut inside an actuator calendar does not pretend its endpoint body/resources were reached");
    auto split=input();split.job.capturers=2;split.job.capture_rank=0;
    const auto low=price(split);split.job.capture_rank=1;const auto high=price(split);
    check(low.banked_gems_by_H==6 && high.banked_gems_by_H==5 && low.banked_gems_by_H+high.banked_gems_by_H==11 &&
        !low.disjoint_supported && !high.disjoint_supported,
        "integer remainder belongs to lowest ID; splitting never invents group value or an independently executable two-body capture");
    auto bigger=input();bigger.job.gems=22;
    check(price(bigger).banked_gems_by_H==22,"different source value survives to the literal banked objective");
}
void channel_payment_is_not_the_entry_payment()
{
    auto hazardous=input();hazardous.self.body.hp=25;
    hazardous.job.members[0].hp=hazardous.job.full_hp[0]=1;
    hazardous.job.gems=21;hazardous.job.bank.handoff_inputs=0;
    const auto stand=hazardous.job.bank.points.back().point;
    for(auto* leg:{&hazardous.job.initial,&hazardous.job.capture,&hazardous.job.release,&hazardous.job.bank,&hazardous.job.renewal,&hazardous.job.home,&hazardous.job.redeploy})
        for(auto& step:leg->points)if(step.point==stand)step.raw_upper=10;
    // A non-selected external source at(2,4),range1 covers the final altar
    // stand(1,3), not the work/capture/release poses. Every path occurrence
    // of that stand carries the same complete immutable exposure upper.
    // Actual input: entry loss10 + one refused-entry reserve10 fits HP25.
    // The separate three-input tribute then owes30, exceeding remainingHP15.
    check(25>10+10 && 25-10<3*10,
        "the counterexample passes one movement/refusal payment but cannot survive the complete bank channel");
    const auto refused=price(hazardous);
    check(refused.status==pj::Status::Unsupported && refused.reason==pj::Reason::BankHazard &&
        refused.last_release==21 && refused.last_bank==0 && refused.banked_gems_by_H==0 && refused.carried_gems==21,
        "a positive all-source endpoint cannot acquire three free channel inputs after its entry was paid");
    hazardous.self.body.hp=1000;const auto still_unsupported=price(hazardous);
    check(still_unsupported.status==pj::Status::Unsupported && still_unsupported.reason==pj::Reason::BankHazard,
        "hazardous banking is an explicitly unsupported family, not a blanket claim that a richer healthy controller cannot bank there");
    hazardous.self.body.hp=25;++hazardous.key.geometry_revision;++hazardous.job.geometry_revision;
    for(auto* leg:{&hazardous.job.initial,&hazardous.job.capture,&hazardous.job.release,&hazardous.job.bank,&hazardous.job.renewal,&hazardous.job.home,&hazardous.job.redeploy})
        for(auto& step:leg->points)step.raw_upper=0;
    const auto quiet=price(hazardous);
    check(quiet.status==pj::Status::Ready && quiet.banked_gems_by_H==21 && quiet.carried_gems==0 && quiet.last_bank==25,
        "the otherwise identical quiet endpoint supports and dates the full three-input tribute");
}
void compatible_words_and_cooldowns()
{
    auto basic=input();const auto baseline=price(basic);
    auto reset=basic;reset.self.spells[0]={sm::SpellKind::reset,true,0,0,0,0,false};reset.self.period[0]=8;
    const auto reset_quote=price(reset);
    check(reset_quote.status==pj::Status::Ready && reset_quote.last_clear==13 && baseline.last_clear==14 && reset_quote.commands==4,
        "Basic Reset Basic is an actual compatible three-primitive word with one global cast, not increased basic rate");
    auto same_input=input();same_input.self.body.pos={2,2};same_input.job.initial=path({{2,2}},0);
    const auto fresh=price(same_input);same_input.self.body.can_attack=same_input.self.body.can_cast=false;
    same_input.self.body.can_move=false;same_input.self.body.moved_this_turn=true;
    const auto spent=price(same_input);
    check(spent.status==pj::Status::Ready && spent.last_clear==fresh.last_clear+1,
        "initial same-input spent allowances and actual walking are not refreshed before the next owner input");
    auto area=input();area.job.members[0].pos={4,2};area.job.members[1]={1,0,{4,3},201,0,1,false,0,true};
    area.job.member_count=2;area.job.full_hp[1]=201;
    area.job.capture=path({{2,2},{3,2},{4,2}},0);area.job.release=path({{4,2},{3,2},{2,2}});
    area.self.spells[0]={sm::SpellKind::aoe,true,4,100,1,0,true};area.self.period[0]=8;area.self.focus_modifier[0]=1;
    const auto ready=price(area);area.self.cooldown[0]=7;const auto cold=price(area);
    check(ready.status==pj::Status::Ready && ready.last_clear==28 && ready.commands==3 && cold.last_clear==33,
        "actual fixed-perch AoE geometry and absolute cooldown date set three legal cast opportunities");
    auto unreachable=input();unreachable.self.body.attack_range=0;
    check(price(unreachable).status==pj::Status::Unsupported,"max legal reach is never used as a fictitious productive basic");
    auto heart=input();heart.job.members[0].hp=heart.job.full_hp[0]=420;heart.job.members[0].strength=50;
    heart.job.members[0].awake=true;
    for(auto* leg:{&heart.job.initial,&heart.job.capture,&heart.job.release,&heart.job.bank,&heart.job.renewal,&heart.job.home,&heart.job.redeploy})
        for(auto& step:leg->points)step.raw_upper=sm::distance(step.point,heart.job.members[0].pos)<=1?50:0;
    const auto fixed=price(heart);heart.self.missing_health_bonus=.5f;const auto dynamic=price(heart);
    check(dynamic.status==pj::Status::Ready && fixed.status==pj::Status::Ready && dynamic.last_clear<fixed.last_clear,
        "known Heart missing-health outgoing is recomputed after charged ingress and each NPC volley");
    auto recovery=heart;recovery.job.repeat=true;recovery.job.spawn_healing=.1f;recovery.job.heal_inputs=1;
    check(price(recovery).status==pj::Status::Incomplete,"one partial spawn-heal tick cannot invent a full recovery");
    recovery.job.heal_inputs=10;
    check(price(recovery).status==pj::Status::Ready,"queried fractional spawn healing is simulated through the declared paid recovery inputs");
}
void own_wallet_breakpoints()
{
    auto value=input(35);value.job.repeat=true;value.job.respawn_inputs=0;value.job.purchase_when_home=true;value.job.gold=30;value.job.spawn_healing=1;
    value.self.shop_supported=true;auto& shop=value.self.shop;
    shop.metadata_complete=shop.shop_at_declared_spawn=shop.stable_raw_stat_projection=true;
    shop.body={1000,1000,100,100};shop.growth={10,5,5};
    for(std::size_t i=0;i<pi::family_count;++i)
    {
        auto& family=shop.families[i];family.has_result=true;
        family.shard={static_cast<std::uint32_t>(i),30,5,{0,1,0},pi::Passive::None,0,true};
        family.result={static_cast<std::uint32_t>(20+i),150,0,{0,5,0},i==0?pi::Passive::Monster:pi::Passive::None,i==0?.1f:0,false};
    }
    shop.families[0].shards_owned=4;
    const auto fifth=price(value);value.self.shop.families[0].shards_owned=0;const auto first=price(value);
    check(fifth.status==pj::Status::Ready && first.status==pj::Status::Ready && fifth.purchase_calls==1 && first.purchase_calls==1 &&
        fifth.last_projected_basic>first.last_projected_basic && fifth.last_purchase_readback==32 && fifth.owned_gold==0 && first.owned_gold==0 &&
        fifth.banked_gems_by_H==11 && first.banked_gems_by_H==11,
        "the same OWN30gold buys a fifth-shard passive breakpoint or a first shard, never an added gold objective weight");
    auto banker=value;banker.job.bank_gold_per_gem=2;
    const auto converted=price(banker);
    check(converted.status==pj::Status::Ready && converted.owned_gold==22 && converted.purchase_calls==1,
        "owned banking conversion enters that actor's future wallet, not a fabricated team purse");
    auto fraction=value;fraction.job.gold_multiplier=1.33f;
    check(price(fraction).status==pj::Status::Incomplete,"unrepresented fractional future wallet cannot cross a claimed shop breakpoint by truncation");
    auto split=input();split.job.gold=25;split.job.capturers=2;split.job.capture_rank=0;
    const auto low=price(split);split.job.capture_rank=1;const auto high=price(split);
    check(low.owned_gold==13 && high.owned_gold==12 && low.gold_exact && high.gold_exact,
        "base gold integer split/remainder precedes the recipient's multiplier");
}
void dates_trace_and_resource_generation()
{
    auto value=input(500);value.job.repeat=true;value.job.common_full_generation=true;
    const auto quote=price(value);
    check(quote.status==pj::Status::Ready && quote.cycles>2 && quote.events.size()<32 && quote.shift_invariant &&
        quote.banked_gems_by_H+quote.carried_gems==quote.cycles*11,
        "repeated full generations follow own payout/release/respawn and consume each nominal generation once; identical cycles retain compressed events");
    const auto same=pj::rebase(quote,quote.start,quote.horizon,quote.key);
    check(same.status==pj::Status::Ready && same.banked_gems_by_H==quote.banked_gems_by_H && same.carried_gems==quote.carried_gems,
        "event trace reproduces the complete original common-H objective");
    const auto shifted=pj::rebase(quote,110,184,quote.key);
    check(shifted.status==pj::Status::Ready && shifted.banked_gems_by_H==22 && shifted.carried_gems==11,
        "late handshake cheaply rebases the relative bank prefix and separately retains an unbanked payout");
    auto changed=quote.key;++changed.body_revision;
    check(pj::rebase(quote,110,184,changed).status==pj::Status::Incomplete,
        "actual changed own-body provenance withdraws a cached future program");
    check(pj::rebase(quote,9,184,quote.key).status==pj::Status::Incomplete,
        "calendar cannot shift earlier than the proved ready body and cooldown origin");
    auto cold=value;cold.self.spells[0]={sm::SpellKind::damage,false,1,100,0,0,false};
    cold.self.focus_modifier[0]=1;cold.self.period[0]=8;cold.self.cooldown[0]=5;
    const auto dated=price(cold);
    check(!dated.shift_invariant && pj::rebase(dated,110,184,dated.key).status==pj::Status::Incomplete,
        "absolute cold-start availability cannot be silently shifted as an all-ready quote");
    auto partial=value;partial.job.members[0].hp=1;partial.job.life=pj::Life::Reward;partial.job.life_supported=false;
    const auto common=price(partial);
    check(common.status==pj::Status::Ready && common.banked_gems_by_H==quote.banked_gems_by_H &&
        common.enabler==pj::Enabler::ObserveAlive && (common.assumptions&pj::FutureHp),
        "common conditional full generation never inherits either observer's actual Reward/partial-HP facts");
    auto bad=value;bad.job.release=path({{3,2}},0);
    check(price(bad).status==pj::Status::Unsupported,"retaining occupation of a selected source cannot manufacture renewable generations");
}
void explicit_boundaries_and_retention()
{
    auto value=input();value.self.own=false;
    check(price(value).status==pj::Status::Incomplete,"no foreign wallet/body/readiness is manufactured");
    value=input();value.job.life=pj::Life::Unknown;value.job.enabler=pj::Enabler::None;
    check(price(value).status==pj::Status::Incomplete,"unknown Life without an enabling operator is incomplete");
    value.job.enabler=pj::Enabler::ObserveAlive;value.job.members[0].hp_exact=false;value.job.conditional_hp=true;
    const auto nomination=price(value);
    check(nomination.status==pj::Status::Ready && nomination.conditional && nomination.enabler==pj::Enabler::ObserveAlive,
        "explicit observation/full-HP assumption can produce a dated conditional nomination without current action authority");
    value=input();value.job.external_sources_complete=false;
    check(price(value).status==pj::Status::Incomplete,"omitted external sources are not implicit quietness");
    value=input();value.job.external_capture_raw=1;
    check(price(value).status==pj::Status::Unsupported,"harmful external capture requires another represented operator family");
    value=input();pj::Producer retained{std::move(value)};
    const auto before=retained.result().work_used;retained.advance(0);
    check(retained.result().work_used==before && retained.last_work()==0,"zero-unit advancement never restarts or advances retained preparation");
    retained.advance(1);const auto after=retained.result().work_used;retained.advance(0);
    check(after==before+1 && retained.result().work_used==after,"only actual bounded pure progress moves the retained cursor");
}
}
int main()
{
    complete_calendar_and_late_custody();channel_payment_is_not_the_entry_payment();compatible_words_and_cooldowns();own_wallet_breakpoints();
    dates_trace_and_resource_generation();explicit_boundaries_and_retention();
    std::printf("production job: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
