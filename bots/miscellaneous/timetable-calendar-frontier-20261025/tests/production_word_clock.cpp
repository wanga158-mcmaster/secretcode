#include "../src/production_word_clock.h"
#include "../src/production_job.h"
#include "../src/assignment_owner.h"
#include <array>
#include <cstdio>
#include <limits>

namespace {
namespace wc=tt::production_word_clock;
namespace sm=mb::service;
namespace pj=tt::production_job;
int checks{},failures{};
void check(bool value,const char* message) {++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
using Spells=std::array<sm::Spell,wc::slots>;
using Cooldowns=std::array<std::uint32_t,wc::slots>;
void represented_roster_examples() {
    // Role labels describe the collector's ALREADY classified input; the
    // clock never guesses ability support from a champion name or SDK data.
    Spells vesper{};vesper[0]={sm::SpellKind::reset,true,0,0,0,0,false};
    const Cooldowns vesper_actual{0,8};const auto before=vesper_actual;
    const auto v=wc::project(100,vesper,vesper_actual);
    check(v.status==wc::Status::Ready&&v.dependencies==1&&v.earliest_ready==100&&
        v.cooldown==Cooldowns{0,0}&&v.ready_at==Cooldowns{100,100},
        "Vesper's omitted dash cooldown8 cannot postpone the represented ready reset/Basic programme");
    check(vesper_actual==before&&vesper[0].kind==sm::SpellKind::reset&&vesper[0].ready,
        "projecting programme clocks leaves actual all-slot cooldowns and represented spell metadata unchanged");
    Spells ryth{};ryth[1]={sm::SpellKind::aoe,false,4,100,1,2,true};
    const auto r=wc::project(100,ryth,{17,6});
    check(r.status==wc::Status::Ready&&r.dependencies==2&&r.earliest_ready==106&&
        r.cooldown==Cooldowns{0,6}&&r.ready_at==Cooldowns{100,106},
        "Ryth's represented ground AoE/stun keeps its actual cooldown despite currently false readiness");
    Spells marcella{};const auto m=wc::project(100,marcella,{9,0});
    check(m.status==wc::Status::Ready&&m.dependencies==0&&m.earliest_ready==100,
        "Marcella's already omitted enhanced reset adds no dependency to ordinary Basic service");
    Spells heart{};heart[1]={sm::SpellKind::shield,false,0,0,0,4,false};
    const auto h=wc::project(100,heart,{12,3});
    check(h.status==wc::Status::Ready&&h.dependencies==2&&h.earliest_ready==103&&h.cooldown==Cooldowns{0,3},
        "Heart's omitted heal is irrelevant while every represented shield clock is retained, including zero current amount");
    Spells attack{};attack[0].kind=sm::SpellKind::damage;attack[1].kind=sm::SpellKind::cc;
    const auto a=wc::project(10,attack,{2,7});
    check(a.dependencies==3&&a.earliest_ready==17&&a.ready_at==Cooldowns{12,17},
        "both targeted damage and control remain represented readiness dependencies");
}
void exact_dates_and_failure_scope() {
    const auto maximum=std::numeric_limits<std::uint32_t>::max();
    Spells spells{};spells[0].kind=sm::SpellKind::reset;
    const auto edge=wc::project(maximum-2,spells,{2,maximum});
    check(edge.status==wc::Status::Ready&&edge.earliest_ready==maximum&&edge.ready_at[0]==maximum&&edge.cooldown[1]==0,
        "exact maximum absolute ready date is valid and an omitted slot is not added to epoch");
    const auto overflow=wc::project(maximum-2,spells,{3,0});
    check(overflow.status==wc::Status::Overflow,"a represented absolute readiness overflow is explicit rather than wrapped");
    spells[1].kind=static_cast<sm::SpellKind>(99);
    check(wc::dependency_mask(spells).status==wc::Status::InvalidKind&&
        wc::project(10,spells,{0,0}).status==wc::Status::InvalidKind,
        "unknown represented operations cannot silently become omitted dependencies");
    const auto dependencies=wc::dependency_mask(Spells{});
    check(!dependencies.uses(0)&&!dependencies.uses(wc::slots)&&!dependencies.uses(1000),
        "dependency query rejects slots outside the fixed represented word");
    for(const auto kind:{sm::SpellKind::damage,sm::SpellKind::aoe,sm::SpellKind::cc,sm::SpellKind::reset,sm::SpellKind::shield}) {
        Spells one{};one[0].kind=kind;one[0].ready=false;
        const auto clock=wc::project(20,one,{5,100});
        check(clock.status==wc::Status::Ready&&clock.dependencies==1&&clock.earliest_ready==25&&clock.cooldown[0]==5,
            "every Producer-supported operation remains dependent when currently unavailable");
    }
}
void pure_programme_copy_boundary() {
    pj::Snapshot actual;actual.epoch=100;actual.cooldown={0,8};
    actual.spells[0]={sm::SpellKind::reset,true,0,0,0,0,false};
    actual.body.can_move=false;actual.body.can_attack=true;actual.body.can_cast=false;actual.body.moved_this_turn=true;
    actual.gold=30;actual.carried_gems=11;actual.actor=3;actual.body_revision=71;
    auto programme=actual;
    const auto clock=wc::project(actual.epoch,programme.spells,programme.cooldown);
    if(clock.status==wc::Status::Ready)programme.cooldown=clock.cooldown;
    check(actual.cooldown==Cooldowns{0,8}&&programme.cooldown==Cooldowns{0,0},
        "only the retained private programme copy drops an omitted operation clock");
    check(programme.actor==actual.actor&&programme.body_revision==actual.body_revision&&programme.epoch==actual.epoch&&
        programme.gold==actual.gold&&programme.carried_gems==actual.carried_gems&&
        !programme.body.can_move&&programme.body.can_attack&&!programme.body.can_cast&&programme.body.moved_this_turn,
        "projection does not renew current M/A/C/walk history, alter custody or relabel self provenance");
    // A cold represented reset remains cold. Its earliest warm date alone is
    // NOT a proof that a forecast starting100 can be shifted to105 unchanged.
    programme.spells[0].ready=false;programme.cooldown[0]=5;
    const auto cold=wc::project(100,programme.spells,programme.cooldown);
    check(cold.earliest_ready==105&&cold.cooldown[0]==5&&!programme.spells[0].ready,
        "safe invariant date preserves cold readiness instead of granting an immediate cast or rebase certificate");
}
pj::Leg path(std::initializer_list<pj::Point> points,std::uint32_t handoff=1) {
    pj::Leg result;result.complete=result.disjoint_supported=true;result.handoff_inputs=handoff;
    for(auto point:points)result.points.push_back({point,0});return result;
}
pj::Input input() {
    pj::Input value;value.start=100;value.horizon=200;value.key={0,0,7,3,4,5,6};
    auto& self=value.self;self.actor=0;self.body_revision=3;self.epoch=100;
    self.own=self.complete=self.stable_outgoing=true;self.body.pos={1,2};self.body.hp=self.body.max_hp=1000;
    self.body.strength=self.raw_strength=100;self.body.focus=self.raw_focus=100;
    self.spells[0]={sm::SpellKind::reset,true,0,0,0,0,false};self.period[0]=8;self.cooldown={0,8};
    auto& job=value.job;job.group=0;job.generation=7;job.life_revision=4;job.geometry_revision=5;job.word_revision=6;
    job.perch={2,2};job.width=8;job.height=8;job.member_count=1;
    job.members[0]={0,0,{3,2},201,0,1,false,0,true};job.full_hp[0]=201;job.gems=11;
    job.capture_payout_inputs=6;job.respawn_inputs=20;
    job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=job.common_full_generation=true;
    job.initial=path({{1,2},{2,2}});job.capture=path({{2,2},{3,2}},0);job.release=path({{3,2},{2,2}});
    job.bank=path({{2,2},{1,3}});job.renewal=path({{1,3},{2,2}});
    job.home=path({{1,3},{0,3}});job.redeploy=path({{0,3},{1,2},{2,2}});return value;
}
pj::Quote produce(pj::Input value) {
    pj::Producer producer{std::move(value)};
    for(unsigned work=0;work<100000&&producer.status()==pj::Status::NeedCompute;++work)producer.advance(1);
    check(producer.status()!=pj::Status::NeedCompute,"real represented programme terminates within the fixture structural bound");
    return producer.result();
}
void actual_producer_certificate_boundary() {
    const auto actual=input();auto programme=actual;
    const auto clock=wc::project(programme.self.epoch,programme.self.spells,programme.self.cooldown);
    programme.self.cooldown=clock.cooldown;
    const auto all_slot=produce(actual),represented=produce(programme);
    check(all_slot.status==pj::Status::Ready&&represented.status==pj::Status::Ready&&
        all_slot.banked_gems_by_H==represented.banked_gems_by_H&&all_slot.last_clear==represented.last_clear&&
        all_slot.last_payout==represented.last_payout&&all_slot.last_bank==represented.last_bank&&
        all_slot.commands==represented.commands,
        "removing only a None clock preserves the actual same-start Basic/reset production and complete bank calendar");
    check(!all_slot.shift_invariant&&represented.shift_invariant&&actual.self.cooldown==Cooldowns{0,8},
        "real Producer obtains the represented-word shift certificate without mutating the actual all-slot snapshot");
    auto cold=programme;cold.self.cooldown[0]=5;cold.self.spells[0].ready=false;
    const auto still_cold=wc::project(cold.self.epoch,cold.self.spells,cold.self.cooldown);
    cold.self.cooldown=still_cold.cooldown;const auto dated=produce(cold);
    check(dated.status==pj::Status::Ready&&!dated.shift_invariant&&still_cold.earliest_ready==105&&
        pj::rebase(dated,105,200,dated.key).status==pj::Status::Incomplete,
        "a real cold represented reset remains dated and cannot be shifted merely because its warm date was computed");
}
tt::assignment_owner::Seed runtime_seed(std::uint32_t start=10) {
    namespace ao=tt::assignment_owner;
    ao::Seed value;auto& grid=value.grid;grid.width=7;grid.height=5;
    grid.terrain.assign(35,0);grid.camp_group.assign(35,-1);grid.raw_upper.assign(35,0);
    grid.altars={{0,4}};grid.home_roots={{0,0}};grid.exclusive_ownership=grid.require_quiet_bank=true;
    value.stamp={5,6,3,7,10,{1,1}};value.model_start=start;value.horizon=80;
    value.handoff_inputs.fill(1);value.complete_npc_catalogue=value.conditional_no_future_traffic=true;
    auto& self=value.self;self.actor=0;self.body_revision=3;self.epoch=10;
    self.own=self.complete=self.stable_outgoing=true;self.body.pos={1,1};self.body.hp=self.body.max_hp=1000;
    self.body.strength=self.raw_strength=100;self.body.focus=self.raw_focus=100;
    self.spells[0]={sm::SpellKind::reset,true,0,0,0,0,false};self.period={4,8};self.cooldown={0,8};
    for(unsigned i=0;i<2;++i) {
        ao::Group group;group.canonical=static_cast<std::uint8_t>(i);group.geometry_group=static_cast<int>(i);
        auto& job=group.metadata;job.group=i;job.generation=7+i;job.life_revision=4+i;job.geometry_revision=5;job.word_revision=6;
        job.width=7;job.height=5;job.member_count=1;const pj::Point point=i==0?pj::Point{3,1}:pj::Point{5,3};
        job.members[0]={0,static_cast<int>(i),point,201,0,1,false,0,true};job.full_hp[0]=201;
        job.gems=11;job.capture_payout_inputs=6;job.respawn_inputs=20;
        job.full_group_known=job.conditional_hp=job.continuation_supported=
            job.external_sources_complete=job.common_full_generation=true;
        grid.camp_group[static_cast<std::size_t>(point.y)*7+point.x]=static_cast<int>(i);
        value.groups.push_back(std::move(group));
    }
    return value;
}
std::shared_ptr<tt::assignment_owner::IndexedProgramme> runtime_first(
    const std::shared_ptr<tt::assignment_owner::Runtime>& runtime,bool represented_word) {
    namespace ao=tt::assignment_owner;ao::FrontierCursor cursor;
    for(unsigned unit=0;unit<200000&&runtime->status()==ao::Status::NeedCompute;++unit) {
        if(represented_word)runtime->advance_word_programmes(1);else runtime->advance_programmes(1);
        check(runtime->last_work()<=1,"real Runtime word-clock wrapper preserves the requested structural work slice");
        const auto row=runtime->poll_programme(runtime,cursor);
        if(row.handle) {
            auto programme=ao::IndexedProgramme::open(runtime,*row.handle);
            check(static_cast<bool>(programme),"a real completed Producer opens its exact lifetime-pinned calendar sidecar");
            if(!programme)return {};
            for(unsigned work=0;work<10000&&programme->status()==tt::production_calendar::Status::NeedCompute;++work)
                programme->advance(1);
            check(programme->valuation_ready()&&programme->status()==tt::production_calendar::Status::Ready,
                "Producer completion plus exact calendar/full-total agreement establishes real valued eligibility");
            return programme;
        }
        if(row.state==ao::FrontierState::Stale)break;
    }
    check(false,"the tiny retained real Runtime publishes a finite supported programme");return {};
}
void runtime_projection_before_producer_construction() {
    namespace ao=tt::assignment_owner;
    auto actual=std::make_shared<ao::Runtime>(runtime_seed());
    const auto projected=runtime_first(actual,true);
    if(!projected)return;
    check(actual->seed().self.cooldown==Cooldowns{0,8}&&actual->seed().self.period==Cooldowns{4,8}&&
        actual->seed().self.epoch==10&&actual->seed().model_start==10,
        "actual Runtime Seed retains every SDK cooldown and period while word projection is confined to its programme copy");
    const auto& programme=projected->input();
    check(programme.self.cooldown==Cooldowns{0,0}&&programme.self.period==Cooldowns{4,8}&&
        programme.self.spells[0].kind==sm::SpellKind::reset&&programme.self.spells[0].ready&&
        programme.self.spells[1].kind==sm::SpellKind::none&&programme.self.epoch==10,
        "new real caller projected omitted dash before Producer construction and preserved represented ready reset metadata");
    check(projected->forecast().shift_invariant&&
        projected->lookup_exact(11,80,projected->handle().programme_key).has_value(),
        "checked real programme supports a later common start once irrelevant omitted clocks are removed");
    auto old=std::make_shared<ao::Runtime>(runtime_seed());const auto old_cold=runtime_first(old,false);
    if(!old_cold)return;
    check(old_cold->input().self.cooldown==Cooldowns{0,8}&&!old_cold->forecast().shift_invariant&&
        old_cold->lookup_exact(10,80,old_cold->handle().programme_key).has_value()&&
        !old_cold->lookup_exact(11,80,old_cold->handle().programme_key)&&
        !old_cold->lookup_exact(18,80,old_cold->handle().programme_key),
        "unchanged old caller retains the cold omitted clock: its original dated forecast does not become shiftable merely as time passes");
    auto warm=std::make_shared<ao::Runtime>(runtime_seed(18));const auto old_warm=runtime_first(warm,false);
    if(!old_warm)return;
    check(old_warm->input().self.cooldown==Cooldowns{0,8}&&old_warm->forecast().shift_invariant&&
        old_warm->lookup_exact(19,80,old_warm->handle().programme_key).has_value(),
        "the old path requires a separately constructed proper all-slot warm-start programme, not relabeling the earlier cold quote");
}
}
int main() {
    represented_roster_examples();exact_dates_and_failure_scope();pure_programme_copy_boundary();
    actual_producer_certificate_boundary();
    runtime_projection_before_producer_construction();
    std::printf("production word clock: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
