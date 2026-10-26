#include "../src/assignment_fresh_binding.h"
#include "../src/production_calendar.h"
#include <cstdio>
#include <memory>
#include <utility>

namespace {
namespace fb=tt::assignment_fresh_binding;namespace sg=tt::assignment_static_geometry;
namespace ag=tt::assignment_geometry;namespace pj=tt::production_job;namespace pc=tt::production_calendar;
int checks{},failures{};
void check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
sg::StaticSeed static_seed() {
    sg::StaticSeed seed;seed.key={11,12,13,14,1,16,true};seed.complete_potential=true;
    auto& grid=seed.grid;grid.width=9;grid.height=7;grid.terrain.assign(63,0);
    grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);grid.altars={{0,6}};grid.home_roots={{0,0}};
    grid.exclusive_ownership=grid.require_quiet_bank=true;
    for(int i=0;i<2;++i) {
        const ag::Point p=i==0?ag::Point{3,2}:ag::Point{7,4};
        sg::CatalogGroup group;group.canonical=i;group.members.push_back({p,201,0,1});
        seed.catalogue.push_back(std::move(group));grid.camp_group[static_cast<std::size_t>(p.y)*9+p.x]=i;
    }
    return seed;
}
std::shared_ptr<const sg::StaticGeometry> sealed(sg::Limits limits={}) {
    sg::StaticBuilder builder{static_seed(),limits};
    for(unsigned i=0;i<100000&&builder.status()==ag::Status::NeedCompute;++i)builder.advance(1);
    check(builder.status()==ag::Status::Ready,"the actual sealed static builder finishes its complete source/root fields");
    return builder.result();
}
std::shared_ptr<const sg::OriginView> origin(std::shared_ptr<const sg::StaticGeometry> geometry,ag::Stamp stamp) {
    sg::OriginBuilder builder{std::move(geometry),stamp,sg::OriginMode::BindOnly};
    for(unsigned i=0;i<100&&builder.status()==ag::Status::NeedCompute;++i)builder.advance(stamp,1);
    check(builder.status()==ag::Status::Ready,"a current BindOnly origin seals without requiring a fresh origin BFS");
    const auto out=builder.result();check(out&&!out->has_field(),"binding fixture actually has no origin field");return out;
}
std::shared_ptr<const sg::TargetGeometry> target(const std::shared_ptr<const sg::OriginView>& view) {
    ag::Request request;request.stamp=view->stamp();request.selected_group=0;request.selected_tiles={{3,2}};
    request.perch={2,2};request.kit={ag::Family::Basic,1,0,0,{}};request.handoff_inputs.fill(1);
    request.disjoint_inputs_supported=request.exclusive_ownership=true;
    sg::TargetBuilder builder{view,std::move(request)};
    for(unsigned i=0;i<100000&&builder.status()==ag::Status::NeedCompute;++i)builder.advance(view->stamp(),1);
    check(builder.status()==ag::Status::Ready,"one sealed target builds six future paths and a retained perch field");return builder.result();
}
fb::Request request(const std::shared_ptr<const sg::StaticGeometry>& geometry,
    const std::shared_ptr<const sg::TargetGeometry>& cached,bool fresh) {
    fb::Request r;r.target=cached;r.current.support=geometry->key();
    r.current.key={0,0,fresh?8u:7u,fresh?4u:3u,fresh?5u:4u,12,fresh?44u:43u};
    r.current.origin={12,16,r.current.key.body_revision,fresh?9u:7u,fresh?20:10,fresh?ag::Point{1,3}:ag::Point{1,2}};
    r.origin=origin(geometry,r.current.origin);r.current.inventory=fresh?104:103;r.current.wallet=fresh?204:203;
    r.current.effects=fresh?304:303;r.current.clock=fresh?404:403;r.current.values=501;
    r.current.life=pj::Life::Unknown;r.current.enabler=pj::Enabler::ObserveAlive;
    auto& self=r.self;self.actor=0;self.body_revision=r.current.key.body_revision;
    self.epoch=static_cast<std::uint32_t>(r.current.origin.epoch);self.own=self.complete=self.stable_outgoing=true;
    self.body.pos={r.current.origin.origin.x,r.current.origin.origin.y};self.body.max_hp=1000;self.body.hp=fresh?800:1000;
    self.body.strength=self.raw_strength=fresh?201.0f:100.0f;self.body.focus=self.raw_focus=100;
    self.body.can_cast=!fresh;self.body.moved_this_turn=fresh;self.gold=fresh?60:30;
    self.cooldown={0,8};self.period={0,8};
    self.shop.effects.jungle=fresh?.25f:0;self.body.camp_damage_multiplier=fresh?.75f:1;
    self.shop.families[0].shards_owned=fresh?3:0;
    r.current.allowances=fb::allowances(self.body);
    auto& job=r.metadata;job.group=0;job.generation=r.current.key.generation;job.life_revision=r.current.key.life_revision;
    job.geometry_revision=12;job.word_revision=r.current.key.word_revision;job.life=r.current.life;job.enabler=r.current.enabler;
    job.observed_round=self.epoch;job.width=9;job.height=7;job.member_count=1;
    job.members[0]={0,0,{3,2},0,0,1,false,0,false};job.full_hp[0]=201;
    job.gems=11;job.capture_payout_inputs=6;job.respawn_inputs=20;job.gold_multiplier=fresh?1.25f:1;
    job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=job.common_full_generation=true;
    r.start=self.epoch;r.horizon=100;r.handoff_inputs.fill(1);r.exact_own_state=r.exact_personal_life=r.project_omitted_clocks=true;
    return r;
}
fb::Status finish(fb::Builder& builder,fb::Current current) {
    for(unsigned i=0;i<100000&&builder.status()==fb::Status::NeedCompute;++i) {
        const auto before=builder.work_used();builder.advance(current,1);
        check(builder.last_work()<=1&&builder.work_used()>before,"each positive fresh-binding slice advances bounded retained work");
    }
    return builder.status();
}
std::shared_ptr<pj::Producer> produce(fb::Builder& builder) {
    auto owned=builder.take();check(static_cast<bool>(owned),"InputReady transfers a new owned seven-leg input, not a repriced old Quote");
    if(!owned)return {};
    auto producer=std::make_shared<pj::Producer>(std::move(*owned));
    for(unsigned i=0;i<100000&&producer->status()==pj::Status::NeedCompute;++i)producer->advance(1);
    check(producer->status()==pj::Status::Ready,"fresh input independently reaches actual Producer completion");return producer;
}
void fresh_body_new_origin_and_valuation() {
    const auto geometry=sealed();if(!geometry)return;
    const auto original=origin(geometry,{12,16,3,7,10,{1,2}});if(!original)return;
    const auto cached=target(original);if(!cached)return;
    const auto old_initial=cached->result().legs[0].points.front().point;
    auto old_request=request(geometry,cached,false);const auto old_current=old_request.current;
    fb::Builder old{std::move(old_request)};check(finish(old,old_current)==fb::Status::InputReady,"old body obtains an independent complete input");
    const auto old_producer=produce(old);if(!old_producer)return;
    const auto old_quote=old_producer->result();const auto old_key=old_producer->programme().key;
    auto fresh_request=request(geometry,cached,true);const auto current=fresh_request.current;
    fb::Builder fresh{std::move(fresh_request)};
    auto later=current;++later.origin.epoch;later.origin.query+=100;
    check(fresh.advance(later,1)==fb::Status::NeedCompute,"round/query alone do not erase an unchanged semantic binding");
    check(finish(fresh,later)==fb::Status::InputReady,"new own body reuses the same static target without an all-menu barrier");
    const auto* input=fresh.input();check(input!=nullptr,"complete owned input is exposed before any full-H valuation claim");if(!input)return;
    check(input->key==current.key&&input->self.body.hp==800&&input->self.raw_strength==201&&input->self.gold==60&&
        input->self.shop.effects.jungle==.25f&&input->self.shop.families[0].shards_owned==3&&
        !input->self.body.can_cast&&input->self.body.moved_this_turn,
        "HP, stats, wallet, item effects/inventory, M/A/C and walking come from the NEW own snapshot");
    check(fresh.actual_self().cooldown==std::array<std::uint32_t,2>{0,8}&&input->self.cooldown==std::array<std::uint32_t,2>{0,0},
        "actual all-slot clock stays retained while only the new represented programme projects omitted operations");
    check(input->job.initial.points.front().point==pj::Point{1,3}&&
        input->job.initial.points.back().point==pj::Point{2,2}&&old_initial==ag::Point{1,2}&&
        cached->result().legs[0].points.front().point==old_initial&&cached->stamp().body==3,
        "fresh origin is traced to the retained perch while the old target's dated initial path is never relabeled or mutated");
    check(input->job.life==pj::Life::Unknown&&input->job.enabler==pj::Enabler::ObserveAlive&&input->job.common_full_generation,
        "exact current Unknown remains a conditional ObserveAlive nomination, never fabricated Alive action authority");
    const auto new_producer=produce(fresh);if(!new_producer)return;const auto& quote=new_producer->result();
    check(quote.key==current.key&&quote.key!=old_key&&quote.last_clear-quote.start<old_quote.last_clear-old_quote.start&&
        old_producer->programme().key==old_key&&old_producer->result().last_clear==old_quote.last_clear,
        "new actual strength recomputes a shorter work word while the old Input, Quote and its dated result remain unchanged");
    pc::Index calendar{{quote.events,{quote.start,quote.horizon,quote.initial_carried},new_producer,true}};
    check(calendar.status()==pc::Status::NeedCompute,"actual Producer completion remains distinct from checked full-H value readiness");
    for(unsigned i=0;i<100000&&calendar.status()==pc::Status::NeedCompute;++i)calendar.advance(1);
    const auto value=calendar.query(quote.horizon-quote.start);
    check(calendar.status()==pc::Status::Ready&&value.banked==quote.banked_gems_by_H&&value.carried==quote.carried_gems,
        "new full-H banked/carried forecast gains eligibility only after independent checked calendar agreement");
    check(pj::rebase(old_quote,quote.start,quote.horizon,current.key).status==pj::Status::Incomplete,
        "the old body forecast cannot be adopted by simply replacing its key with the fresh body key");
}
void withdrawal_and_support_boundaries() {
    const auto geometry=sealed();if(!geometry)return;const auto old_origin=origin(geometry,{12,16,3,7,10,{1,2}});if(!old_origin)return;
    const auto cached=target(old_origin);if(!cached)return;
    auto r=request(geometry,cached,true);const auto current=r.current;fb::Builder changing{std::move(r)};
    changing.advance(current,8);auto changed=current;++changed.inventory;
    check(changing.advance(changed,1)==fb::Status::Stale&&!changing.input()&&!changing.take(),
        "a changed full-inventory premise during staged copy cannot mix old body and new facts into an adoptable programme");
    r=request(geometry,cached,true);auto respawning=r.current;respawning.life=pj::Life::Respawning;respawning.enabler=pj::Enabler::ObserveRenewal;
    r.current=respawning;r.metadata.life=respawning.life;r.metadata.enabler=respawning.enabler;r.metadata.available_from=r.self.epoch;
    fb::Builder absent{std::move(r)};check(finish(absent,respawning)==fb::Status::InputReady&&
        absent.input()->job.life==pj::Life::Respawning&&absent.input()->job.enabler==pj::Enabler::ObserveRenewal,
        "Respawning at its current date retains a named future nomination without timer-based conversion to Alive");
    r=request(geometry,cached,true);r.self.body.attack_range=0;const auto short_current=r.current;fb::Builder short_range{std::move(r)};
    check(finish(short_range,short_current)==fb::Status::Unsupported&&short_range.reason()==fb::Reason::Kit,
        "reusable target geometry cannot turn a changed incompatible actual reach into supported work");
    r=request(geometry,cached,true);r.metadata.members[0].pos={3,3};const auto wrong_current=r.current;fb::Builder wrong_group{std::move(r)};
    check(finish(wrong_group,wrong_current)==fb::Status::Unsupported&&wrong_group.reason()==fb::Reason::Group,
        "same group id/count is insufficient when actual metadata member coordinates do not match the sealed selected set");
    const auto alias=sealed();if(!alias)return;r=request(alias,cached,true);const auto alias_current=r.current;
    fb::Builder aliased{std::move(r)};
    check(finish(aliased,alias_current)==fb::Status::Unsupported&&aliased.reason()==fb::Reason::Geometry,
        "equal semantic keys on a different sealed object cannot counterfeit the retained target's actual geometry provenance");
}
void future_only_target_requires_actual_new_ingress() {
    sg::StaticSeed seed;seed.key={21,22,23,24,1,26,true};seed.complete_potential=true;
    auto& grid=seed.grid;grid.width=11;grid.height=7;grid.terrain.assign(77,0);
    grid.camp_group.assign(77,-1);grid.raw_upper.assign(77,0);
    grid.altars={{0,6},{10,6}};grid.home_roots={{0,0},{10,0}};
    grid.exclusive_ownership=grid.require_quiet_bank=true;
    // The wall makes the original nomination pose genuinely unreachable
    // from the future work perch. Both components have real bank/home roots.
    for(int y=0;y<7;++y)grid.terrain[static_cast<std::size_t>(y)*11+4]=1;
    for(int i=0;i<2;++i) {
        const ag::Point p=i==0?ag::Point{8,2}:ag::Point{2,4};
        sg::CatalogGroup group;group.canonical=i;group.members.push_back({p,201,0,1});
        seed.catalogue.push_back(std::move(group));grid.camp_group[static_cast<std::size_t>(p.y)*11+p.x]=i;
    }
    sg::StaticBuilder static_builder{std::move(seed)};
    for(unsigned i=0;i<100000&&static_builder.status()==ag::Status::NeedCompute;++i)static_builder.advance(1);
    const auto geometry=static_builder.result();check(static_builder.status()==ag::Status::Ready&&geometry,
        "the real sealed map contains two disconnected components with independently usable bank/home roots");
    if(!geometry)return;
    const ag::Stamp nominal{22,26,3,77,10,{1,2}};
    ag::Request nomination;nomination.stamp=nominal;nomination.selected_group=0;
    nomination.selected_tiles={{8,2}};nomination.perch={7,2};nomination.kit={ag::Family::Basic,1,0,0,{}};
    nomination.handoff_inputs.fill(1);nomination.disjoint_inputs_supported=nomination.exclusive_ownership=true;
    sg::TargetBuilder target_builder{geometry,std::move(nomination),sg::TargetMode::FutureOnly};
    for(unsigned i=0;i<100000&&target_builder.status()==ag::Status::NeedCompute;++i)target_builder.advance(nominal,1);
    const auto future=target_builder.result();check(target_builder.status()==ag::Status::Ready&&future,
        "actual FutureOnly target completes six future witnesses despite the disconnected original nomination origin");
    if(!future)return;
    check(!future->has_initial()&&future->result().legs[0].points.empty()&&!future->result().legs[0].complete&&
        future->distance_to_perch(nominal.origin)==ag::detail::none,
        "FutureOnly supplies no counterfeit initial route from the unreachable old pose");
    const auto preserved=future->result().legs;
    for(std::size_t leg=1;leg<ag::leg_count;++leg)check(preserved[leg].complete&&!preserved[leg].points.empty(),
        "each reusable future leg is genuinely sealed and complete before fresh binding");
    fb::Request r;r.origin=origin(geometry,{22,26,4,99,20,{6,5}});r.target=future;if(!r.origin)return;
    r.current.key={0,0,8,4,5,22,44};r.current.support=geometry->key();r.current.origin=r.origin->stamp();
    r.current.inventory=111;r.current.wallet=222;r.current.effects=333;r.current.clock=444;r.current.values=555;
    r.current.life=pj::Life::Unknown;r.current.enabler=pj::Enabler::ObserveAlive;
    auto& self=r.self;self.actor=0;self.body_revision=4;self.epoch=20;self.own=self.complete=self.stable_outgoing=true;
    self.body.pos={6,5};self.body.hp=self.body.max_hp=1000;
    self.body.strength=self.raw_strength=201;self.body.focus=self.raw_focus=100;r.current.allowances=fb::allowances(self.body);
    auto& job=r.metadata;job.group=0;job.generation=8;job.life_revision=5;job.geometry_revision=22;job.word_revision=44;
    job.life=pj::Life::Unknown;job.enabler=pj::Enabler::ObserveAlive;job.observed_round=20;
    job.width=11;job.height=7;job.member_count=1;job.members[0]={0,0,{8,2},0,0,1,false,0,false};job.full_hp[0]=201;
    job.gems=11;job.capture_payout_inputs=6;job.respawn_inputs=20;
    job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=job.common_full_generation=true;
    r.start=20;r.horizon=100;r.handoff_inputs.fill(1);r.exact_own_state=r.exact_personal_life=r.project_omitted_clocks=true;
    const auto current=r.current;const auto distance=future->distance_to_perch(current.origin.origin);
    check(distance!=ag::detail::none&&distance>1,"the actual fresh origin requires a real multi-edge initial trace");
    fb::Builder binding{std::move(r)};
    for(unsigned i=0;i<100000&&binding.status()==fb::Status::NeedCompute;++i) {
        binding.advance(current,1);
        if(binding.status()!=fb::Status::InputReady)
            check(!binding.input(),"no owned input is published while the new ingress or another required leg is still unfinished");
    }
    const auto* bound=binding.input();check(binding.status()==fb::Status::InputReady&&bound,
        "InputReady follows new actual ingress construction plus all six future copies");if(!bound)return;
    check(bound->job.initial.complete&&bound->job.initial.points.size()==static_cast<std::size_t>(distance)+1&&
        bound->job.initial.points.front().point==pj::Point{6,5}&&bound->job.initial.points.back().point==pj::Point{7,2},
        "the fresh complete initial leg starts at the actual new pose and ends at the retained work perch");
    for(std::size_t i=1;i<bound->job.initial.points.size();++i) {
        const auto a=bound->job.initial.points[i-1].point,b=bound->job.initial.points[i].point;
        check(mb::service::distance(a,b)==1&&future->distance_to_perch({b.x,b.y})+1==future->distance_to_perch({a.x,a.y}),
            "every new initial edge is adjacent and strictly descends the sealed perch field");
    }
    const pj::Leg* complete[]{&bound->job.initial,&bound->job.capture,&bound->job.release,&bound->job.bank,
        &bound->job.renewal,&bound->job.home,&bound->job.redeploy};
    for(const auto* leg:complete)check(leg->complete&&!leg->points.empty(),"fresh bound input owns all seven complete paths");
    const auto producer=produce(binding);if(!producer)return;const auto& quote=producer->result();
    pc::Index calendar{{quote.events,{quote.start,quote.horizon,quote.initial_carried},producer,true}};
    for(unsigned i=0;i<100000&&calendar.status()==pc::Status::NeedCompute;++i)calendar.advance(1);
    const auto value=calendar.query(quote.horizon-quote.start);
    check(calendar.status()==pc::Status::Ready&&quote.complete&&quote.supported&&quote.disjoint_supported&&
        value.banked==quote.banked_gems_by_H&&value.carried==quote.carried_gems&&value.banked==11,
        "actual Producer and checked full-H calendar complete from the fresh seven-leg binding, not an old Quote");
    check(!future->has_initial()&&future->result().legs[0].points.empty()&&!future->result().legs[0].complete&&
        future->stamp().origin==nominal.origin&&future->stamp().body==nominal.body&&
        future->stamp().query==nominal.query&&future->stamp().epoch==nominal.epoch,
        "the retained FutureOnly target still has no initial leg and keeps the untouched original nominal stamp");
    for(std::size_t leg=1;leg<ag::leg_count;++leg) {
        const auto& after=future->result().legs[leg];const auto& before=preserved[leg];
        bool same=after.complete==before.complete&&after.handoff_inputs==before.handoff_inputs&&after.points.size()==before.points.size();
        for(std::size_t i=0;same&&i<after.points.size();++i)same=after.points[i].point==before.points[i].point&&
            after.points[i].raw_upper==before.points[i].raw_upper;
        check(same,"binding and fresh forecasting never mutate a reusable future witness");
    }
}
void retained_nomination_is_not_fresh_admission() {
    const auto geometry=sealed();if(!geometry)return;
    const auto original_origin=origin(geometry,{12,16,3,7,10,{1,2}});if(!original_origin)return;
    const auto cached=target(original_origin);if(!cached)return;
    auto original=request(geometry,cached,true);const auto current=original.current;
    const auto original_start=original.start,original_horizon=original.horizon;
    auto checked_request=original;auto later_guard_request=original;
    fb::Builder nomination{std::move(original)},checked{std::move(checked_request)};
    nomination.advance_nomination(0);
    check(nomination.status()==fb::Status::NeedCompute&&nomination.last_work()==0&&
        nomination.work_used()==0&&!nomination.input(),"zero nomination units cannot publish an incomplete owned input");
    auto unrelated=current;
    for(unsigned i=0;i<100000&&nomination.status()==fb::Status::NeedCompute;++i) {
        // These actual-world changes exist OUTSIDE the dated calculation.
        // They must defeat adoption, but cannot erase its mathematical work.
        ++unrelated.key.body_revision;++unrelated.key.life_revision;++unrelated.key.generation;
        ++unrelated.origin.body;++unrelated.origin.epoch;++unrelated.origin.query;
        ++unrelated.inventory;++unrelated.wallet;++unrelated.effects;++unrelated.clock;
        unrelated.life=i%2?pj::Life::Alive:pj::Life::Respawning;
        unrelated.enabler=i%2?pj::Enabler::None:pj::Enabler::ObserveRenewal;
        const auto work=nomination.work_used();nomination.advance_nomination(1);checked.advance(current,1);
        check(nomination.last_work()==1&&nomination.work_used()==work+1,
            "dated nomination makes monotone bounded progress across unrelated external body/Life/date changes");
        check(nomination.status()==checked.status()&&nomination.work_used()==checked.work_used(),
            "nomination and original-Current checked paths perform the identical retained construction units");
        if(nomination.status()!=fb::Status::InputReady)check(!nomination.input(),
            "nomination does not publish before the new initial path and all six future legs are complete");
    }
    const auto* a=nomination.input();const auto* b=checked.input();
    check(nomination.status()==fb::Status::InputReady&&a&&b,
        "nomination and unchanged-original checked binding independently finish complete inputs");if(!a||!b)return;
    check(a->key==b->key&&a->key==current.key&&a->start==original_start&&a->horizon==original_horizon&&
        a->start==b->start&&a->horizon==b->horizon&&a->self.epoch==20&&a->self.body_revision==current.key.body_revision,
        "retained original key, start, horizon and snapshot epoch are never retagged with later world state");
    check(a->self.body.pos==b->self.body.pos&&a->self.body.hp==b->self.body.hp&&a->self.body.strength==b->self.body.strength&&
        a->self.gold==b->self.gold&&a->self.cooldown==b->self.cooldown&&a->self.period==b->self.period&&
        a->self.shop.effects.jungle==b->self.shop.effects.jungle&&
        a->self.shop.families[0].shards_owned==b->self.shop.families[0].shards_owned,
        "nomination copies the same original own HP/body/wallet/items/represented clocks as the checked construction");
    check(a->job.group==b->job.group&&a->job.generation==current.key.generation&&a->job.life_revision==current.key.life_revision&&
        a->job.life==pj::Life::Unknown&&a->job.enabler==pj::Enabler::ObserveAlive&&a->job.observed_round==20&&
        a->job.geometry_revision==b->job.geometry_revision&&a->job.word_revision==b->job.word_revision&&
        a->job.gems==b->job.gems&&a->job.full_hp==b->job.full_hp&&a->job.bank_policy==b->job.bank_policy,
        "original conditional Life/enabler, generation and whole-job values remain the original nomination premises");
    const pj::Leg* left[]{&a->job.initial,&a->job.capture,&a->job.release,&a->job.bank,&a->job.renewal,&a->job.home,&a->job.redeploy};
    const pj::Leg* right[]{&b->job.initial,&b->job.capture,&b->job.release,&b->job.bank,&b->job.renewal,&b->job.home,&b->job.redeploy};
    for(std::size_t leg=0;leg<ag::leg_count;++leg) {
        bool same=left[leg]->complete&&right[leg]->complete&&left[leg]->disjoint_supported==right[leg]->disjoint_supported&&
            left[leg]->handoff_inputs==right[leg]->handoff_inputs&&left[leg]->points.size()==right[leg]->points.size();
        for(std::size_t point=0;same&&point<left[leg]->points.size();++point)
            same=left[leg]->points[point].point==right[leg]->points[point].point&&
                left[leg]->points[point].raw_upper==right[leg]->points[point].raw_upper;
        check(same,"every owned seven-leg witness exactly matches original-Current construction");
    }
    check(fb::same_premises(nomination.binding(),current)&&!fb::same_premises(nomination.binding(),unrelated)&&
        nomination.binding().origin.epoch==current.origin.epoch&&nomination.actual_self().cooldown[1]==8,
        "nomination completion does not certify later actual body/Life or alter its retained full actual clock");
    const auto work=nomination.work_used();nomination.advance_nomination(99);
    check(nomination.last_work()==0&&nomination.work_used()==work,"completed nomination reuses its unchanged input without further work");
    auto taken=nomination.take();check(taken&&nomination.status()==fb::Status::Taken&&!nomination.input()&&!nomination.take(),
        "the owned dated input transfers once and cannot be published or taken twice");
    nomination.advance_nomination(1);check(nomination.status()==fb::Status::Taken&&nomination.last_work()==0&&nomination.work_used()==work,
        "nomination advance cannot resurrect consumed state");
    fb::Builder guarded{std::move(later_guard_request)};
    for(unsigned i=0;i<100000&&guarded.status()==fb::Status::NeedCompute;++i)guarded.advance_nomination(1);
    check(guarded.status()==fb::Status::InputReady&&guarded.advance(unrelated,0)==fb::Status::Stale&&
        guarded.reason()==fb::Reason::Premises&&!guarded.input()&&!guarded.take(),
        "the original checked API still rejects changed actual premises even after nomination calculation completed");
    const auto stale_work=guarded.work_used();guarded.advance_nomination(100);
    check(guarded.status()==fb::Status::Stale&&guarded.last_work()==0&&guarded.work_used()==stale_work,
        "pure nomination advancement cannot reopen a terminal fresh-premise rejection");
}
void nomination_resource_and_premise_failures_stay_typed() {
    sg::Limits limits;limits.path_points=6;
    const auto geometry=sealed(limits);if(!geometry)return;
    const auto old_origin=origin(geometry,{12,16,3,7,10,{1,2}});if(!old_origin)return;
    const auto cached=target(old_origin);if(!cached)return;
    auto r=request(geometry,cached,true);
    r.current.origin.origin={8,0};r.origin=origin(geometry,r.current.origin);r.self.body.pos={8,0};
    const auto distance=cached->distance_to_perch({8,0});
    check(distance!=ag::detail::none&&distance>=geometry->max_path_points(),
        "resource fixture has a real static path that exceeds the declared retained point capacity");
    auto original=r;fb::Builder nomination{std::move(r)},checked{std::move(original)};
    for(unsigned i=0;i<100000&&nomination.status()==fb::Status::NeedCompute;++i)nomination.advance_nomination(1);
    const auto current=checked.binding();finish(checked,current);
    check(nomination.status()==fb::Status::Unsupported&&nomination.reason()==fb::Reason::Path&&
        checked.status()==nomination.status()&&checked.reason()==nomination.reason()&&!nomination.input()&&!nomination.take(),
        "nomination keeps the exact checked API's typed over-capacity-path refusal without a partial input");
    const auto work=nomination.work_used();nomination.advance_nomination(100);
    check(nomination.last_work()==0&&nomination.work_used()==work,"typed resource failure is terminal rather than an endless reset");
    auto invalid=request(geometry,cached,true);invalid.horizon=invalid.start-1;
    fb::Builder dates{std::move(invalid)};dates.advance_nomination(1);
    check(dates.status()==fb::Status::Unsupported&&dates.reason()==fb::Reason::Dates&&!dates.input(),
        "nomination advancement still validates original date support instead of silently extending its horizon");
}
}
int main() {
    fresh_body_new_origin_and_valuation();withdrawal_and_support_boundaries();
    future_only_target_requires_actual_new_ingress();
    retained_nomination_is_not_fresh_admission();nomination_resource_and_premise_failures_stay_typed();
    std::printf("assignment fresh binding: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
