#include "../src/assignment_owner.h"
#include "../src/assignment_protocol.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {
namespace ao=tt::assignment_owner;
namespace ag=tt::assignment_geometry;
namespace pj=tt::production_job;
namespace ap=tt::assignment_protocol;
int checks{},failures{};
void check(bool value,const char* message) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}
}
ao::Seed seed() {
    ao::Seed v;auto& grid=v.grid;grid.width=9;grid.height=7;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{0,6}};grid.home_roots={{0,0}};
    grid.exclusive_ownership=grid.require_quiet_bank=true;
    v.stamp={5,6,3,7,10,{1,2}};v.model_start=10;v.horizon=100;v.handoff_inputs.fill(1);
    v.complete_npc_catalogue=v.conditional_no_future_traffic=true;
    auto& s=v.self;s.actor=0;s.body_revision=3;s.epoch=10;
    s.own=s.complete=s.stable_outgoing=true;s.body.pos={1,2};s.body.hp=s.body.max_hp=1000;
    s.body.strength=s.raw_strength=100;s.body.focus=s.raw_focus=100;s.body.attack_range=1;
    s.spells[0]={mb::service::SpellKind::aoe,true,3,100,1,0,false};s.period[0]=2;
    // Complete current spell provenance: raw Focus100 * modifier1 = observed100.
    // Producer refuses a positive spell without its queried focus multiplier.
    s.focus_modifier[0]=1;
    for(unsigned i=0;i<2;++i) {
        ao::Group g;g.canonical=static_cast<std::uint8_t>(i);g.geometry_group=static_cast<int>(i);
        auto& j=g.metadata;j.group=i;j.generation=7+i;j.life_revision=4+i;
        j.geometry_revision=5;j.word_revision=6;j.life=pj::Life::Alive;j.life_supported=true;
        j.observed_round=j.available_from=10;j.width=9;j.height=7;j.member_count=1;
        const pj::Point p=i==0?pj::Point{3,2}:pj::Point{7,4};
        j.members[0]={0,static_cast<int>(i),p,201,0,1,false,0,true};j.full_hp[0]=201;
        j.gems=i==0?11:220;j.capture_payout_inputs=6;j.submit_divisor=1000;j.spawn_healing=1;
        j.full_group_known=j.conditional_hp=j.continuation_supported=j.external_sources_complete=
            j.common_full_generation=j.repeat=true;
        grid.camp_group[static_cast<std::size_t>(p.y)*9+p.x]=static_cast<int>(i);
        v.groups.push_back(std::move(g));
    }
    return v;
}
void seed_self_contract() {
    auto v=seed();pj::Input input;input.self=v.self;input.job=v.groups[0].metadata;
    const auto& j=input.job;
    input.key={v.self.actor,j.group,j.generation,v.self.body_revision,j.life_revision,j.geometry_revision,j.word_revision};
    input.start=v.model_start;input.horizon=v.horizon;pj::Producer producer(std::move(input));
    // Constructor checks actual damage reproduction before path work begins.
    if(producer.status()!=pj::Status::NeedCompute)
        std::printf("seed producer status%u reason%u\n",static_cast<unsigned>(producer.status()),
            static_cast<unsigned>(producer.result().reason));
    check(producer.status()==pj::Status::NeedCompute,"synthetic current body/spell exactly satisfies Producer construction");
}
std::optional<ao::Handle> next(std::shared_ptr<ao::Runtime> menu,ao::FrontierCursor& cursor) {
    ao::Diagnostic last_producer;std::size_t skipped{};
    for(unsigned work=0;work<200000;++work) {
        const auto diagnostic=menu->diagnostic();
        if(diagnostic.producer_status!=0xffffffffu)last_producer=diagnostic;
        const auto row=menu->poll_complete(menu,cursor);
        if(row.handle)return row.handle;
        if(row.state==ao::FrontierState::Skipped)++skipped;
        if(row.state==ao::FrontierState::End||row.state==ao::FrontierState::Stale) {
            std::printf("frontier stopped status%u reason%u phase%u producer%u units%llu skipped%zu\n",
                static_cast<unsigned>(menu->status()),static_cast<unsigned>(menu->reason()),diagnostic.phase,
                last_producer.producer_status,static_cast<unsigned long long>(last_producer.producer_work),skipped);
            return std::nullopt;
        }
        if(row.state==ao::FrontierState::NeedCompute)menu->advance(1);
    }
    std::printf("frontier structural bound exhausted status%u reason%u producer%u units%llu\n",
        static_cast<unsigned>(menu->status()),static_cast<unsigned>(menu->reason()),last_producer.producer_status,
        static_cast<unsigned long long>(last_producer.producer_work));
    return std::nullopt;
}
// Pure caller/backend ordering fixture: letters are witnesses, not SDK actions.
// Self keys and Life supplied below are declared synthetic test inputs.
struct Caller {
    enum class Owner { Ordinary,Work,Capture,PaidObserve };
    enum class Result { Waiting,Started,Refused };
    std::shared_ptr<ao::Runtime> menu;
    std::optional<ao::ProgrammeLease> staged,active;
    ap::ProgrammeGate date;
    Owner owner{Owner::Ordinary};unsigned paid_observation_inputs{},work_entries{};
    std::optional<std::uint32_t> funded_epoch;std::vector<char> order;
    void ordinary_publication(std::uint32_t now) {funded_epoch=now;order.push_back('P');}
    bool optional_unit(std::uint32_t now) {
        if(funded_epoch!=now)return false;
        order.push_back('B');menu->advance(1);return true;
    }
    bool stage(const ao::Handle& h,std::uint32_t now,std::uint32_t start,const pj::Key& current,
               const ao::PhysicalRefutation* blocked=nullptr) {
        if(funded_epoch!=now||!menu)return false;
        auto lease=menu->programme(h,start,100,current,blocked);
        if(!lease||!date.stage(start,100))return false;
        staged=std::move(lease);return true;
    }
    Result admit(std::uint32_t now,bool fresh_self,const pj::Key& current,pj::Life life,
                 const ao::PhysicalRefutation* blocked=nullptr) {
        if(!staged||funded_epoch!=now)return Result::Refused;
        const auto& h=staged->handle();const auto pin=h.runtime.lock();
        auto exact=pin?pin->programme(h,staged->choice().start,100,current,blocked):std::nullopt;
        const auto admission=date.admit(now,fresh_self&&exact.has_value(),life!=pj::Life::Respawning);
        if(admission==ap::ProgrammeAdmission::Waiting)return Result::Waiting;
        if(admission!=ap::ProgrammeAdmission::Started) {staged.reset();return Result::Refused;}
        active=std::move(exact);staged.reset();
        if(life==pj::Life::Unknown) {owner=Owner::PaidObserve;++paid_observation_inputs;}
        else if(life==pj::Life::Reward)owner=Owner::Capture;
        else {owner=Owner::Work;++work_entries;}
        return Result::Started;
    }
};
void completed_prefix_and_identity() {
    Caller c;c.menu=std::make_shared<ao::Runtime>(seed());ao::FrontierCursor cursor;
    check(!c.optional_unit(10),"optional work cannot precede funded ordinary publication");
    c.ordinary_publication(10);check(c.optional_unit(10),"funded ordinary publication permits optional pure work");
    check(c.order.size()==2&&c.order[0]=='P'&&c.order[1]=='B',"pure backend witnesses incumbent before optional builder");
    const auto h=next(c.menu,cursor);check(h.has_value(),"first real completed Producer/prefix is discoverable");
    if(!h)return;
    const auto coverage=c.menu->coverage();
    check(!coverage.all_groups_finished&&coverage.finished==1&&coverage.eligible==1,
        "caller can use one complete prefix before later families/groups finish");
    check(!c.menu->best(0,10,100),"legacy group argmax really is unavailable at this frontier");
    check(c.stage(*h,10,10,h->programme_key),"current-date local fallback stages exact handle");
    check(c.staged&&c.staged->forecast().status==pj::Status::Ready&&
        c.staged->bank_prefix().back()==c.staged->forecast().banked_gems_by_H,
        "stage owns the completed Producer and its fully accumulated event prefix");
    check(c.admit(10,true,h->programme_key,pj::Life::Alive)==Caller::Result::Started,
        "current-date fresh local fallback can enter its named work");
    check(c.active&&c.active->handle()==*h&&c.work_entries==1,"adoption keeps exact identity without group lookup");
    std::optional<ao::Handle> other;
    for(unsigned n=0;n<ao::max_candidates;++n) {
        const auto row=next(c.menu,cursor);if(!row)break;
        const auto value=c.menu->lookup_exact(*row,10,100,row->programme_key);
        if(value&&value->group==c.active->choice().group&&value->family!=c.active->choice().family){other=row;break;}
    }
    check(other.has_value(),"a genuinely different cached family exists for the same group");
    if(other)check(c.active->handle()==*h&&!(c.active->handle()==*other),
        "later cached family cannot substitute for the adopted exact handle");
    const auto original=c.active->input().key;const auto* programme=&c.active->input();
    std::weak_ptr<ao::Runtime> lifetime=c.menu;c.menu.reset();
    check(!lifetime.expired() && &c.active->input()==programme && c.active->input().key==original,
        "controller menu reset leaves pinned programme and original key intact");
    c.active.reset();check(lifetime.expired(),"releasing the final lease releases its old Runtime");
}
void dates_freshness_and_enablers() {
    auto menu=std::make_shared<ao::Runtime>(seed());ao::FrontierCursor cursor;const auto h=next(menu,cursor);
    check(h.has_value(),"date/enabler cases have a real completed programme");if(!h)return;
    Caller stale;stale.menu=menu;stale.ordinary_publication(10);
    check(stale.stage(*h,10,10,h->programme_key),"fresh premise stages before a later body change");
    auto wrong=h->programme_key;++wrong.body_revision;
    check(stale.admit(10,true,wrong,pj::Life::Alive)==Caller::Result::Refused&&
        stale.owner==Caller::Owner::Ordinary&&!stale.date.no_shop(),
        "wrong current self refuses without replacing physical incumbent or claiming NoShop");
    Caller pair;pair.menu=menu;pair.ordinary_publication(10);
    check(pair.stage(*h,10,14,h->programme_key),"future paired date stages the same exact programme");
    check(pair.admit(10,true,h->programme_key,pj::Life::Alive)==Caller::Result::Waiting&&
        pair.owner==Caller::Owner::Ordinary&&!pair.date.no_shop()&&pair.work_entries==0,
        "future-date quote grants no early action or NoShop ownership");
    pair.ordinary_publication(14);
    check(pair.admit(14,true,h->programme_key,pj::Life::Alive)==Caller::Result::Started,
        "future paired date admits only at its exact common start");
    check(pair.active&&pair.active->choice().start==14&&pair.active->input().key==h->programme_key,
        "calendar view changes without relabeling original producer provenance");
    Caller expired;expired.menu=menu;expired.ordinary_publication(10);
    check(expired.stage(*h,10,14,h->programme_key),"expired case stages real future programme");
    expired.ordinary_publication(15);
    check(expired.admit(15,true,h->programme_key,pj::Life::Alive)==Caller::Result::Refused&&
        expired.owner==Caller::Owner::Ordinary,"missed local date cannot be restamped or act late");
    Caller unknown;unknown.menu=menu;unknown.ordinary_publication(10);
    check(unknown.stage(*h,10,10,h->programme_key),"Unknown case retains a conditional programme");
    check(unknown.admit(10,true,h->programme_key,pj::Life::Unknown)==Caller::Result::Started&&
        unknown.owner==Caller::Owner::PaidObserve&&unknown.paid_observation_inputs==1&&unknown.work_entries==0,
        "Unknown explicitly consumes one modeled observation input and never becomes Alive work");
    Caller respawning;respawning.menu=menu;respawning.ordinary_publication(10);
    check(respawning.stage(*h,10,10,h->programme_key),"Respawning case has the same cached programme");
    check(respawning.admit(10,true,h->programme_key,pj::Life::Respawning)==Caller::Result::Refused&&
        respawning.owner==Caller::Owner::Ordinary&&!respawning.date.no_shop(),
        "actual Respawning refuses that date rather than borrowing conditional Alive totals");
    const auto choice=menu->lookup_exact(*h,10,100,h->programme_key);check(choice.has_value(),"refutation case resolves exact geometry");
    if(!choice)return;
    const auto blocked=ao::PhysicalRefutation::from(*choice);auto current=h->programme_key;
    menu->refresh_group(0,900,901);current.generation=900;current.life_revision=901;
    for(const unsigned date:{14u,22u}) {
        Caller retry;retry.menu=menu;retry.ordinary_publication(date);
        check(!retry.stage(*h,date,date,current,&blocked)&&retry.owner==Caller::Owner::Ordinary,
            "physical refutation survives new Life/generation and a different calendar");
    }
}
void transient_seed_rearming() {
    using Failure=ap::FrontierSeedFailure;
    // Symbolic collector outcomes: these do not claim SDK bag/buff observations.
    for(const char* refusal:{"carrying","active-buff","collector-nonready"}) {
        Caller c;bool has_self=false;Failure failure=Failure::Transient;
        c.ordinary_publication(10);
        check(!has_self&&!c.menu&&!c.staged&&c.owner==Caller::Owner::Ordinary,
            "initial funded transient refusal owns no self, model or programme");
        c.ordinary_publication(11); // A different actual-publication opportunity in the fixture.
        const bool rearm=ap::frontier_seed_rearm(failure,c.funded_epoch==11,
            static_cast<bool>(c.menu),false,c.staged.has_value());
        check(rearm,"resolved later funded publication rearms transient seed collection");
        if(!rearm)continue;
        auto resolved=seed();resolved.self.epoch=11;resolved.stamp.epoch=11;resolved.model_start=11;
        c.menu=std::make_shared<ao::Runtime>(std::move(resolved));has_self=true;failure=Failure::None;
        ao::FrontierCursor cursor;const auto h=next(c.menu,cursor);
        if(!h)std::printf("transient rearm did not produce programme: %s\n",refusal);
        check(has_self&&failure==Failure::None&&h.has_value(),
            "resolved collector supplies a real physically consistent completed programme");
        if(h)check(c.stage(*h,11,11,h->programme_key)&&c.staged&&c.staged->handle()==*h&&
            c.staged->input().key==h->programme_key&&c.owner==Caller::Owner::Ordinary,
            "rearmed exact programme stages without replacing ordinary incumbent");
    }
    struct Refusal {Failure failure;bool funded,model,seed,pin;};
    for(const auto blocked:{Refusal{Failure::Transient,false,false,false,false},
        Refusal{Failure::Permanent,true,false,false,false},Refusal{Failure::None,true,false,false,false},
        Refusal{Failure::Transient,true,true,false,false},Refusal{Failure::Transient,true,false,true,false},
        Refusal{Failure::Transient,true,false,false,true}}) {
        Caller incumbent;const bool allowed=ap::frontier_seed_rearm(blocked.failure,blocked.funded,
            blocked.model,blocked.seed,blocked.pin);
        check(!allowed&&incumbent.owner==Caller::Owner::Ordinary&&!incumbent.date.no_shop(),
            "unfunded/permanent/already-owned seed paths refuse rearm and preserve incumbent");
    }
}
}
int main() {
    seed_self_contract();completed_prefix_and_identity();dates_freshness_and_enablers();
    transient_seed_rearming();
    std::printf("assignment frontier caller (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
