// Existing pure publication components only. No actual SDK callback, collector,
// Life observation, ProgrammeGate mutation or private publisher executes here.
#include "../src/assignment_publication.h"
#include "../src/production_job.h"
#include "../src/assignment_static_geometry.h"
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {
namespace p=tt::assignment_publication;namespace pj=tt::production_job;namespace sg=tt::assignment_static_geometry;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL publication %s\n",why);}}
struct Inventory {
    std::array<std::uint32_t,64> values{};mutable unsigned reads{};
    std::uint32_t& operator[](std::size_t i){return values[i];}
    std::uint32_t operator[](std::size_t i) const {++reads;return values[i];}
};
struct Self {
    pj::Snapshot snapshot;Inventory inventory;std::size_t inventory_size{};
    int status{},role{2},reason{};bool roster_verified{};std::uint8_t disabled_slots{};std::uint32_t unsupported_caps{};
};
Self self() {
    Self out;auto& s=out.snapshot;s.actor=2;s.body_revision=3;s.epoch=10;
    s.body.pos={-3,5};s.body.hp=750.5f;s.body.max_hp=1200;s.raw_strength=45.25f;s.raw_focus=100;
    s.missing_health_bonus=.5f;s.incoming_scale=.75f;s.gold=60;s.carried_gems=8;
    s.body.attack_range=3;s.body.ranged=s.body.splash=true;s.body.strength=99;s.body.focus=77;
    s.spells[0]={mb::service::SpellKind::damage,true,4,120,0,0,true};
    s.spells[1]={mb::service::SpellKind::aoe,false,5,100.5f,1,2,false};s.period={8,9};
    out.inventory_size=3;out.inventory[0]=1;out.inventory[1]=0;out.inventory[2]=5;return out;
}
template<class Change> void depends(Change change) {
    auto base=self(),changed=base;const auto hash=p::semantic_hash(base);change(changed);
    check(p::semantic_hash(changed)!=hash,"changing a field from the exact old hash changes this fixed-length fixture");
}
template<class Change> void ignores(Change change) {
    auto base=self(),changed=base;const auto hash=p::semantic_hash(base);change(changed);
    check(p::semantic_hash(changed)==hash,"fields omitted by the old hash remain omitted mechanically");
}
void semantic_fields() {
    const auto original=self();
    check(p::semantic_hash(original)==17739085916274655375ull,"exact old field order/float32 bits/negative-coordinate cast match the retained golden hash");
    auto zero=self();zero.snapshot.actor=7535888134350815159ull;
    check(p::semantic_hash(zero)==1,"the exact raw-zero preimage still maps to nonzero1");
    depends([](auto& x){++x.snapshot.actor;});depends([](auto& x){++x.snapshot.body.pos.x;});depends([](auto& x){++x.snapshot.body.pos.y;});
    depends([](auto& x){++x.snapshot.body.hp;});depends([](auto& x){++x.snapshot.body.max_hp;});
    depends([](auto& x){++x.snapshot.raw_strength;});depends([](auto& x){++x.snapshot.raw_focus;});
    depends([](auto& x){x.snapshot.missing_health_bonus+=.25f;});depends([](auto& x){x.snapshot.incoming_scale+=.25f;});
    depends([](auto& x){++x.snapshot.gold;});depends([](auto& x){++x.snapshot.carried_gems;});depends([](auto& x){++x.snapshot.body.attack_range;});
    depends([](auto& x){x.snapshot.body.ranged=!x.snapshot.body.ranged;});depends([](auto& x){x.snapshot.body.splash=!x.snapshot.body.splash;});
    depends([](auto& x){++x.inventory[0];});depends([](auto& x){++x.inventory[2];});depends([](auto& x){++x.inventory_size;});
    for(std::size_t i=0;i<2;++i) {
        depends([&](auto& x){x.snapshot.spells[i].kind=mb::service::SpellKind::none;});
        depends([&](auto& x){++x.snapshot.spells[i].damage;});depends([&](auto& x){++x.snapshot.spells[i].range;});
        depends([&](auto& x){++x.snapshot.spells[i].radius;});depends([&](auto& x){++x.snapshot.spells[i].duration;});
        depends([&](auto& x){x.snapshot.spells[i].exclusive=!x.snapshot.spells[i].exclusive;});depends([&](auto& x){++x.snapshot.period[i];});
    }
    ignores([](auto& x){++x.snapshot.epoch;++x.snapshot.body_revision;x.snapshot.cooldown={7,8};});
    ignores([](auto& x){x.snapshot.body.can_move=x.snapshot.body.can_attack=x.snapshot.body.can_cast=false;x.snapshot.body.moved_this_turn=true;});
    ignores([](auto& x){x.snapshot.body.shield=50;x.snapshot.body.next_basic_stun=2;x.snapshot.body.camp_damage_multiplier=.25f;});
    ignores([](auto& x){x.snapshot.body.strength=5;x.snapshot.body.focus=2;x.snapshot.basic_scale=2;x.snapshot.focus_modifier={4,5};});
    ignores([](auto& x){x.snapshot.spells[0].ready=false;x.snapshot.spells[1].ready=true;});
    ignores([](auto& x){x.snapshot.shop.effects.monster=1;x.snapshot.shop.gold=100;x.snapshot.shop.families[0].shards_owned=4;});
    ignores([](auto& x){x.snapshot.own=x.snapshot.complete=x.snapshot.stable_outgoing=x.snapshot.shop_supported=true;});
    ignores([](auto& x){x.status=1;x.role=4;x.reason=7;x.roster_verified=true;x.disabled_slots=3;x.unsupported_caps=1;});
    ignores([](auto& x){++x.inventory[3];});
}
void cooldown_and_geometry() {
    pj::Snapshot before,fresh;before.epoch=10;before.cooldown={8,0};fresh.epoch=12;fresh.cooldown={6,0};
    check(p::cooldowns_match(before,fresh),"remaining cooldown preserves the exact absolute ready date");
    fresh.cooldown[0]=5;check(!p::cooldowns_match(before,fresh),"nonzero remaining cooldown cannot move its anchored ready date");
    fresh.epoch=17;fresh.cooldown[0]=0;check(!p::cooldowns_match(before,fresh),"zero cooldown before the anchored date is rejected");
    fresh.epoch=18;check(p::cooldowns_match(before,fresh),"zero cooldown at the exact ready date is accepted");
    fresh.epoch=100;check(p::cooldowns_match(before,fresh),"already-ready cooldown remains ready at a later input");
    fresh.epoch=9;fresh.cooldown={9,1};check(p::cooldowns_match(before,fresh),"this helper alone preserves old rewind behavior when both absolute dates agree");
    before.epoch=std::numeric_limits<std::uint32_t>::max();before.cooldown={8,0};fresh=before;
    check(p::cooldowns_match(before,fresh),"absolute-ready addition uses uint64 beyond uint32 epoch range");
    const sg::StaticKey key{11,12,13,14,1,16,true,false};
    check(p::geometry_revision(key,17)==18346496228984972167ull,"exact private geometry field order matches retained golden revision");
    for(unsigned field=0;field<9;++field) {
        auto changed=key;std::uint64_t graph=17;
        switch(field){case 0:++changed.map;break;case 1:++changed.layout;break;case 2:++changed.catalogue;break;
        case 3:++changed.roots;break;case 4:++changed.graph_policy;break;case 5:++changed.potential;break;
        case 6:++graph;break;case 7:changed.require_quiet_bank=false;break;default:changed.require_quiet_home=true;break;}
        check(p::geometry_revision(changed,graph)!=p::geometry_revision(key,17),"every old geometry/graph/policy field remains a dependency");
    }
}
struct Model {
    bool valued{true};std::optional<pj::CashPlan> plan{pj::CashPlan{}};pj::Input original;
    mutable unsigned valuation_calls{},cash_calls{},input_calls{};
    bool valuation_ready() const {++valuation_calls;return valued;}
    const pj::CashPlan* cash_plan() const {++cash_calls;return plan?&*plan:nullptr;}
    const pj::Input& input() const {++input_calls;return original;}
};
struct Geometry {sg::StaticKey support{11,12,13,14,1,16,true,false};mutable unsigned reads{};const sg::StaticKey& key() const {++reads;return support;}};
struct Companion {sg::StaticKey support;pj::Key key;};
struct Cache {std::shared_ptr<const int> routes;mutable unsigned calls{};std::shared_ptr<const int> result() const {++calls;return routes;}};
struct Data {std::optional<int> anchor{1};std::optional<Self> actual;std::optional<Companion> companion;std::shared_ptr<Model> model;std::shared_ptr<const int> cash_routes;Cache cash_cache;std::shared_ptr<Geometry> geometry;};
struct Owner {std::uint64_t body{},field{7},graph_revision{17};};
struct Setup {Data data;Owner owner;std::uint64_t expected{};};
Setup setup() {
    Setup out;auto& d=out.data;d.actual=self();out.expected=p::semantic_hash(*d.actual);d.actual->inventory.reads=0;
    d.actual->snapshot.body_revision=out.expected;out.owner.body=out.expected;
    d.geometry=std::make_shared<Geometry>();d.companion=Companion{d.geometry->support,{}};
    d.companion->key.body_revision=out.expected;d.companion->key.geometry_revision=p::geometry_revision(d.geometry->support,out.owner.graph_revision);
    d.model=std::make_shared<Model>();d.model->plan->first_cash_complete=true;d.model->plan->heal_complete=80;d.model->plan->redeploy_complete=120;
    d.model->original.start=d.model->original.self.epoch=10;d.model->original.horizon=2000;d.model->original.job.repeat=true;
    d.cash_routes=std::make_shared<const int>(1);d.cash_cache.routes=d.cash_routes;return out;
}
void prepared_and_frozen_short_circuits() {
    for(unsigned fail=0;fail<=8;++fail) {
        auto test=setup();auto& d=test.data;const auto model=d.model;unsigned cash_calls{};
        switch(fail){case 0:d.anchor.reset();break;case 1:d.actual.reset();break;case 2:d.companion.reset();break;case 3:d.model.reset();break;
        case 4:d.model->valued=false;break;case 5:d.cash_routes.reset();break;case 7:d.cash_cache.routes=std::make_shared<const int>(2);break;default:break;}
        const bool result=p::prepared(std::as_const(d),[&]{++cash_calls;return fail!=6;});
        check(result==(fail==8),"prepared preserves the exact readiness/key/pointer result");
        check(model->valuation_calls==(fail>=4?1u:0u)&&cash_calls==(fail>=6?1u:0u)&&d.cash_cache.calls==(fail>=7?1u:0u),
            "prepared does not invoke any later getter/callback after an earlier failure");
    }
    for(unsigned fail=0;fail<=4;++fail) {
        auto test=setup();auto expected=test.expected;unsigned hash_calls{};
        if(fail==0)expected=0;if(fail==1)++test.owner.body;if(fail==2)++test.data.actual->snapshot.body_revision;
        const bool result=p::frozen_body(std::as_const(test.data),std::as_const(test.owner),expected,[&]{++hash_calls;return test.expected+(fail==3?1u:0u);});
        check(result==(fail==4)&&hash_calls==(fail>=3?1u:0u),"frozen body hashes ONLY after nonzero/owner/seed-revision checks");
    }
    auto test=setup();check(p::frozen_body(std::as_const(test.data),std::as_const(test.owner),test.expected),
        "the natural frozen-body overload calls the real extracted semantic hash");
    for(unsigned shape=0;shape<3;++shape) {
        auto d=setup().data;if(shape==0)d.model->plan.reset();else d.model->plan->first_cash_complete=shape==2;
        check(p::cash_complete(std::as_const(d))==(shape==2)&&d.model->cash_calls==(shape==0?1u:2u),
            "cash complete preserves null/flag tests and both original getter accesses");
    }
}
void fresh_short_circuits() {
    for(unsigned fail=0;fail<=9;++fail) {
        auto test=setup();auto actual=*test.data.actual;actual.snapshot.epoch=100;actual.inventory.reads=0;
        bool epoch=true,life=true;std::array<unsigned,4> calls{};std::array<unsigned,4> order{};unsigned cursor{};
        if(fail==0)actual.status=1;if(fail==1)epoch=false;if(fail==2)++actual.snapshot.gold;
        if(fail==3)actual.snapshot.cooldown[0]=1;if(fail==4)++test.owner.field;if(fail==5)test.data.geometry.reset();
        if(fail==6)++test.data.geometry->support.potential;if(fail==7)++test.owner.graph_revision;if(fail==8)life=false;
        const auto geometry=test.data.geometry;
        const bool result=p::fresh_matches(std::as_const(test.data),std::as_const(test.owner),std::as_const(actual),test.expected,actual.status==0,
            [&]{++calls[0];order[cursor++]=1;return epoch;},
            [&]{++calls[1];order[cursor++]=2;return test.owner.field==7;},
            [&]{++calls[2];order[cursor++]=3;return p::geometry_revision(test.data.geometry->key(),test.owner.graph_revision);},
            [&]{++calls[3];order[cursor++]=4;return life;});
        check(result==(fail==9),"fresh predicate preserves every old typed-frame rejection position");
        check(calls[0]==(fail>=1?1u:0u)&&calls[1]==(fail>=4?1u:0u)&&calls[2]==(fail>=7?1u:0u)&&calls[3]==(fail>=8?1u:0u),
            "epoch/field/revision/Life callbacks remain lazy after each earlier failure");
        check(actual.inventory.reads==(fail>=2?3u:0u),"fresh semantic hash is not evaluated before readiness and epoch checks");
        check(!geometry||geometry->reads==(fail>=7?2u:fail==6?1u:0u),"geometry is read only after field equality and only again at the lazy revision position");
        for(unsigned i=0;i<cursor;++i)check(order[i]==i+1,"invoked callbacks preserve their original order");
    }
}
void temporal_and_choice_seams() {
    auto test=setup();auto& model=*test.data.model;
    check(p::first_cash_fits(std::as_const(model),100,2000),"later publication prices the original110-input first programme without retagging");
    check(model.original.start==10&&model.original.self.epoch==10&&model.plan->redeploy_complete==120,
        "pure date predicate changes no original Input or cash-plan dates");
    model.cash_calls=model.input_calls=0;
    check(!p::first_cash_fits(model,2001,2000)&&model.cash_calls==1&&model.input_calls==1,
        "start beyond H rejects safely while preserving original preceding cash/input getter accesses");
    model.plan->redeploy_complete=9;check(!p::first_cash_fits(model,100,2000),"first end before original start rejects before subtraction");
    model.plan->redeploy_complete=120;check(!p::first_cash_fits(model,1900,2000),"first programme exceeding remaining H is rejected");
    check(p::first_cash_fits(model,1890,2000),"first programme exactly fitting remaining H is accepted inclusively");
    model.original.job.repeat=false;model.plan->heal_complete=15;check(p::first_cash_fits(model,1995,2000),"nonrepeat uses paid home-heal endpoint instead of redeployment");
    model.plan->heal_complete=10;check(p::first_cash_fits(model,2000,2000),"preserve old zero-duration fit at H; ProgrammeGate remains separate");
    struct Choice {std::uint64_t banked_gems_by_H{};};
    for(unsigned shape=0;shape<4;++shape) {
        std::optional<Choice> choice;if(shape)choice=Choice{shape==1?0u:1u};unsigned calls{};
        const bool usable=p::choice_usable(std::as_const(choice),[&]{++calls;return shape==2;});
        check(usable==(shape==3)&&calls==(shape>=2?1u:0u),"choice refutation runs only after optional presence and positive banked value");
    }
}
static_assert(std::is_same_v<decltype(p::semantic_hash(std::declval<const Self&>())),std::uint64_t>);
static_assert(std::is_same_v<decltype(p::cooldowns_match(std::declval<const pj::Snapshot&>(),std::declval<const pj::Snapshot&>())),bool>);
}
int main() {
    semantic_fields();cooldown_and_geometry();prepared_and_frozen_short_circuits();fresh_short_circuits();temporal_and_choice_seams();
    std::printf("assignment publication (pure, SDK callbacks UNEXECUTED): %d checks, %d failures\n",checks,failures);return failures?1:0;
}
