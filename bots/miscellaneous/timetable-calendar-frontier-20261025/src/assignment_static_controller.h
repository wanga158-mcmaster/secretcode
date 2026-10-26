#pragma once
// Included only inside independent_controller.cpp's private namespace, after
// the ordinary publication/admission helpers. This adapter grants no command
// authority: the existing physical dispatcher still refreshes every actuator.
namespace ssg=assignment_static_geometry;
namespace sfb=assignment_fresh_binding;
namespace sbp=assignment_bound_programme;
namespace ssm=assignment_static_menu;
namespace ssw=assignment_static_world;
namespace scr=assignment_cash_routes;
namespace spub=assignment_publication;
[[maybe_unused]] std::uint64_t static_geometry_revision(const Controller& state,const ssg::StaticKey& key)
{
    return spub::geometry_revision(key,state.graph_revision);
}

[[maybe_unused]] void retire_static_ownership(Controller& state)
{
    auto& data=static_data(state);
    data.anchor.reset();data.actual.reset();data.companion.reset();
    data.origin.reset();data.binding.reset();data.target.reset();data.model.reset();data.active.reset();
    data.cash_routes.reset();data.active_routes.reset();data.cash.reset();data.active_programme=0;
    data.cash_renewal_not_before=data.cash_timing_epoch=-1;
    data.cash_receipt_seen=false;
    data.incumbent_group=data.incumbent_phase=data.checked_epoch=-1;
    data.preparing=data.nominated=false;
    data.independent_preparation=false;
    // The sealed World/target compiler survives actual ordinary handoff. It
    // contains no current own body, wallet, Life or programme valuation. Cash
    // goal fields have that same static lifetime; active routes are a separate pin.
}
[[maybe_unused]] void static_assignment_published(Controller& state,
    const assignment_api::SelfCheckpoint& anchor,std::uint64_t query,int epoch)
{
    auto& data=static_data(state);auto& owner=assignment_data(state);
    owner.serviced_epoch=epoch;owner.query=query;
    if(state.phase!=Phase::Work||state.group<0||state.escape_required||!current(epoch))return;
    data.anchor=anchor;data.publication_query=query;
    data.independent_preparation=false;
    data.incumbent_group=state.group;data.incumbent_phase=static_cast<int>(state.phase);
    data.checked_epoch=-1;data.nominated=false;
    // Actual collection belongs to a fresh pre-action prefix after the
    // incumbent's mandatory route is Ready, rather than after optional work.
}
[[maybe_unused]] production_job::Job static_job_metadata(Controller& state,
    const assignment_api::SelfInputs& self,std::uint8_t canonical,Availability life)
{
    namespace pj=production_job;
    const auto& owner=assignment_data(state);const auto& world=World::get();
    const auto local=owner.canonical.local_by_canonical[canonical];
    const auto& catalogue=world.groups[static_cast<std::size_t>(local)];
    const auto& memory=state.memory[static_cast<std::size_t>(local)];
    const auto& geometry=static_data(state).geometry;
    pj::Job job;job.group=canonical;job.generation=memory.generation;
    job.life_revision=assignment_life_revision(memory);job.geometry_revision=static_geometry_revision(state,geometry->key());
    job.word_revision=self.snapshot.body_revision;job.width=world.width;job.height=world.height;
    job.member_count=catalogue.members.size();
    for(std::size_t i=0;i<std::min(job.member_count,pj::max_members);++i) {
        const auto& member=catalogue.members[i];job.full_hp[i]=member.max_hp;
        job.members[i]={static_cast<int>(i),static_cast<int>(canonical),{member.pos.x,member.pos.y},
            member.max_hp,member.strength,member.range,true,0,false};
    }
    job.gems=catalogue.gems;job.gold=catalogue.gold;
    job.respawn_inputs=static_cast<std::uint32_t>(std::max(0,catalogue.respawn));
    job.capture_payout_inputs=static_cast<std::uint32_t>(std::max(1,cambc::constants::capture_time));
    job.submit_minimum=static_cast<std::uint32_t>(std::max(0,cambc::constants::submit_gem_minimum_duration));
    job.submit_base=static_cast<std::uint32_t>(std::max(0,cambc::constants::submit_gem_base_duration));
    job.submit_multiplier=static_cast<float>(cambc::constants::submit_gem_duration_multiplier);
    job.spawn_healing=cambc::constants::spawn_healing;
    job.heal_inputs=job.spawn_healing>0?static_cast<std::uint32_t>(
        std::min(1000000.0,std::ceil(1.0/static_cast<double>(job.spawn_healing)))):1;
    job.purchase_readback_inputs=1;job.purchase_when_home=false;
    job.bank_policy=pj::BankPolicy::ChargedHome;
    ssw::mix(job.word_revision,static_cast<std::uint64_t>(job.bank_policy));
    job.word_revision=ssw::nonzero(job.word_revision);
    job.gold_multiplier=1+self.snapshot.shop.effects.treasure;
    if(self.role==3)job.gold_multiplier+=static_cast<float>(cambc::constants::passive_abilities::vesper_vale_bonus_gold_percent)/100;
    if(self.role==1)job.bank_gold_per_gem=static_cast<float>(cambc::constants::passive_abilities::marcella_tribute_gold_per_gem);
    job.life=static_cast<pj::Life>(life);job.enabler=pj::Enabler::ObserveAlive;
    job.observed_round=memory.last_seen>=0?static_cast<std::uint32_t>(memory.last_seen):0;
    job.full_group_known=true;job.conditional_hp=true;job.life_supported=life!=Availability::Unknown;
    job.continuation_supported=job.external_sources_complete=true;
    job.repeat=job.common_full_generation=true;
    return job;
}
[[maybe_unused]] sfb::Current static_binding_current(const assignment_api::SelfInputs& self,
    const production_job::Job& job,const ssg::StaticKey& support,std::uint64_t query)
{
    const auto& value=self.snapshot;sfb::Current out;
    out.key={value.actor,job.group,job.generation,value.body_revision,job.life_revision,
        job.geometry_revision,job.word_revision};
    out.support=support;
    out.origin={support.layout,support.potential,value.body_revision,query,
        static_cast<int>(value.epoch),{value.body.pos.x,value.body.pos.y}};
    out.inventory=out.wallet=out.effects=out.clock=out.values=1469598103934665603ull;
    ssw::mix(out.inventory,self.inventory_size);
    for(std::size_t i=0;i<self.inventory_size;++i)ssw::mix(out.inventory,self.inventory[i]);
    ssw::mix(out.wallet,value.gold);ssw::mix(out.wallet,value.carried_gems);
    ssw::mix(out.effects,value.body_revision);
    for(std::size_t i=0;i<value.cooldown.size();++i) {
        // An anchored absolute-ready identity is retained only while fresh SDK
        // reads validate assignment_cooldowns; passing time alone never does.
        ssw::mix(out.clock,value.cooldown[i]?static_cast<std::uint64_t>(value.epoch)+value.cooldown[i]:0);
        ssw::mix(out.clock,value.period[i]);
    }
    for(const auto number:{static_cast<std::uint64_t>(job.gems),static_cast<std::uint64_t>(job.gold),
        static_cast<std::uint64_t>(job.respawn_inputs),static_cast<std::uint64_t>(job.capture_payout_inputs),
        static_cast<std::uint64_t>(job.submit_minimum),static_cast<std::uint64_t>(job.submit_base),
        static_cast<std::uint64_t>(job.bank_policy)})ssw::mix(out.values,number);
    for(const auto number:{job.submit_multiplier,job.spawn_healing,job.gold_multiplier,job.bank_gold_per_gem})
        ssw::mix(out.values,std::bit_cast<std::uint32_t>(number));
    out.life=job.life;out.enabler=job.enabler;out.allowances=sfb::allowances(value.body);
    return out;
}
[[maybe_unused]] void clear_static_query(AssignmentStaticRuntime& data,bool keep_anchor)
{
    if(!keep_anchor){data.anchor.reset();data.independent_preparation=false;}
    data.actual.reset();data.companion.reset();data.origin.reset();data.binding.reset();
    data.target.reset();data.model.reset();data.preparing=false;
    data.cash_routes.reset();
    data.candidate=data.candidate_support=0;
}
[[maybe_unused]] std::uint64_t static_candidate_support(const sfb::Current& current)
{
    auto value=1469598103934665603ull;
    for(const auto part:{current.key.actor,current.key.group,current.key.generation,
        current.key.body_revision,current.key.life_revision,current.key.geometry_revision,current.key.word_revision,
        current.inventory,current.wallet,current.effects,current.clock,current.values,
        static_cast<std::uint64_t>(current.life),static_cast<std::uint64_t>(current.enabler)})ssw::mix(value,part);
    return ssw::nonzero(value);
}
[[maybe_unused]] void decline_static_candidate(Controller& state)
{
    auto& data=static_data(state);const auto publication=data.candidate,support=data.candidate_support;
    clear_static_query(data,true);
    // Release every query pin before optional residency retirement. Rejection
    // is the pair (immutable recipe, semantic own support), never the group.
    if(data.menu&&publication&&support) {
        data.menu->decline(publication,support,ssm::CompletedVerdict::Unsupported);
        const auto& choice=assignment_data(state).choice;
        data.menu->retire(publication,support,choice?choice->candidate_id:0);
    }
}
[[maybe_unused]] bool completed_static_model_rejection(const sbp::Programme& model)
{
    // Calendar/allocation/partial input failures are implementation or capacity
    // boundaries, not completed evidence against this physical recipe.
    if(model.reason()!=sbp::Reason::Producer||model.producer_status()!=production_job::Status::Unsupported)return false;
    const auto reason=model.producer_reason();
    return reason==production_job::Reason::Survival||reason==production_job::Reason::Geometry||
        reason==production_job::Reason::Traffic||reason==production_job::Reason::Word||
        reason==production_job::Reason::Shop||reason==production_job::Reason::Wallet||
        reason==production_job::Reason::NoProgress||reason==production_job::Reason::BankHazard||
        reason==production_job::Reason::RecoveryHazard;
}
[[maybe_unused]] scr::Goals static_cash_goals(const ssg::TargetGeometry& target,std::uint64_t recipe)
{
    const auto& geometry=target.result();
    return {geometry.bank_stand,geometry.home,target.perch(),recipe*3+1,recipe*3+2,recipe*3+3};
}
[[maybe_unused]] bool static_cash_matches(const AssignmentStaticRuntime& data)
{
    return data.geometry&&data.target&&data.candidate&&
        data.cash_cache.key()==scr::Key{data.geometry.get(),data.geometry->key(),
            static_cash_goals(*data.target,data.candidate)};
}
[[maybe_unused]] bool static_kit_matches(const ssm::Entry& entry,const production_job::Snapshot& self)
{
    const auto kits=ssm::detail::geometry(self);
    for(const auto& kit:kits.families)if(kit&&*kit==entry.geometry)return true;
    return false;
}
[[maybe_unused]] bool static_life_matches(Controller& state,const sfb::Current& expected,int epoch,vec2 origin)
{
    const auto canonical=static_cast<std::size_t>(expected.key.group);
    if(canonical>=assignment_data(state).canonical.count)return false;
    const auto local=assignment_data(state).canonical.local_by_canonical[canonical];
    const auto life=admission_lifecycle(state,local,epoch,origin);
    if(life.status!=Status::Ready||life.availability==Availability::Respawning||
       static_cast<production_job::Life>(life.availability)!=expected.life)return false;
    const auto& memory=state.memory[static_cast<std::size_t>(local)];
    return memory.generation==expected.key.generation&&assignment_life_revision(memory)==expected.key.life_revision;
}
[[maybe_unused]] bool publish_static_programme(Controller& state,int epoch)
{
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,20);
    auto& data=static_data(state);auto& owner=assignment_data(state);
    if(!spub::prepared(data,[&]{return static_cash_matches(data);})) {
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,1,0,0);
        return false;
    }
    const auto expected_body=data.companion->key.body_revision;
    if(!spub::frozen_body(data,owner,expected_body))
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,2,0,0);
     clear_static_query(data,false);return !current(epoch);}
    if(!spub::cash_complete(data))
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,3,0,0);
     decline_static_candidate(state);return false;}
    // This fresh collector owns the prefix BEFORE any optional compute. A
    // failure retires the query; it never repeats an unreachable resource gate.
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::begin_collect(epoch,20);
    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_called(
        assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Publication);
    const auto actual=assignment_api::collect_self(*data.anchor,epoch,expected_body);
    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_result(
        assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Publication,
        static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::end_collect(epoch,
        static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
    if(!spub::fresh_matches(data,owner,actual,expected_body,
       actual.status==assignment_api::CollectStatus::Ready,
       [&]{return current(epoch);},[&]{return owner.field==state.potential_generation;},
       [&]{return static_geometry_revision(state,data.geometry->key());},
       [&]{return static_life_matches(state,*data.companion,epoch,{actual.snapshot.body.pos.x,actual.snapshot.body.pos.y});}))
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,4,0,0);
     clear_static_query(data,false);return !current(epoch);}
    const auto start=static_cast<std::uint32_t>(epoch),horizon=static_cast<std::uint32_t>(cambc::constants::max_rounds);
    if(!spub::first_cash_fits(*data.model,start,horizon))
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,5,0,0);
     decline_static_candidate(state);return false;}
    const auto choice=data.model->lookup(start,horizon,data.companion->key);
    if(!spub::choice_usable(choice,[&]{return frontier_refuted(state,*choice);}))
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,6,0,0);
     decline_static_candidate(state);return false;}
    if(!owner.programme.stage(start,horizon)||
       owner.programme.admit(start,true,true)!=assignment_protocol::ProgrammeAdmission::Started)
    {if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(assignment_reachability_probe::role(),epoch,7,0,0);
     clear_static_query(data,false);return false;}
    owner.choice=*choice;owner.choice_local=owner.canonical.local_by_canonical[choice->group];
    owner.self=actual;owner.body=expected_body;
    owner.pending_choice.reset();owner.pending_paired=false;owner.validated=actual;owner.validated_epoch=epoch;
    ++owner.nominations;++frontier_data(state).accepted;
    data.active=data.model;data.preparing=false;data.anchor.reset();data.binding.reset();data.origin.reset();
    data.active_routes=data.cash_routes;data.active_programme=ssw::nonzero(++data.serial);
    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::activate(assignment_reachability_probe::role(),epoch);
    data.cash_renewal_not_before=data.cash_timing_epoch=-1;
    data.cash_receipt_seen=false;
    state.group=owner.choice_local;
    state.phase=data.companion->life==production_job::Life::Reward?Phase::Capture:Phase::Work;
    if(state.phase==Phase::Capture)state.capture_bag=actual.snapshot.carried_gems;
    state.close_front=false;state.basic_next=false;state.escape_required=false;state.partial_progress=false;
    state.acquisition.reset();invalidate(state);
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,21);
    if(budget(10000))std::printf("assignment frontier r%d start%u role%u g%d canonical%u candidate%zu perch%d,%d family%d bank%llu programme=noshop paired=0 coverage=partial life=%u valuation=index\n",
        epoch,start,actual.role,state.group,choice->group,choice->candidate_id,choice->perch.x,choice->perch.y,
        static_cast<int>(choice->family),static_cast<unsigned long long>(choice->banked_gems_by_H),
        static_cast<unsigned>(data.companion->life));
    return true;
}
[[maybe_unused]] bool begin_static_binding(Controller& state,int epoch)
{
    auto& data=static_data(state);auto& owner=assignment_data(state);
    if(!data.actual||!data.geometry||!data.menu)return false;
    const auto position=data.actual->snapshot.body.pos;
    for(const auto& entry:data.menu->entries()) {
        if(entry.state!=ssm::EntryState::Ready||!entry.target||!static_kit_matches(entry,data.actual->snapshot)||
           entry.canonical_group<0||static_cast<std::size_t>(entry.canonical_group)>=owner.canonical.count)continue;
        const auto local=owner.canonical.local_by_canonical[static_cast<std::size_t>(entry.canonical_group)];
        const auto life=admission_lifecycle(state,local,epoch,{position.x,position.y});
        if(life.status!=Status::Ready||!current(epoch))return false;
        if(life.availability==Availability::Respawning)continue;
        auto metadata=static_job_metadata(state,*data.actual,static_cast<std::uint8_t>(entry.canonical_group),life.availability);
        auto companion=static_binding_current(*data.actual,metadata,data.geometry->key(),data.publication_query);
        const auto support=static_candidate_support(companion);
        if(!data.menu->eligible(entry.publication,support))continue;
        // Cash fields belong to this immutable target recipe, not to the fresh
        // body/Life forecast below. Replacement/constructor gets its own funded
        // boundary; future field units may continue after that forecast retires.
        const scr::Key cash_key{data.geometry.get(),data.geometry->key(),
            static_cash_goals(*entry.target,entry.publication)};
        if(data.cash_cache.key()!=cash_key) {
            if(!budget(50000)||!current(epoch))return false;
            data.cash_cache.ensure(data.geometry,cash_key.goals);
            return false;
        }
        // Matching terminal preparation failure is typed unavailability, not
        // permission to rebuild a body forecast against the same failed fields.
        // Keep the physical fallback; in particular Allocation/Capacity never
        // becomes a semantic Menu verdict or an unbounded admitted retry loop.
        if(data.cash_cache.status()!=scr::Status::NeedCompute&&data.cash_cache.status()!=scr::Status::Ready)
            return false;
        const auto clock=production_word_clock::project(data.actual->snapshot.epoch,data.actual->snapshot.spells,data.actual->snapshot.cooldown);
        if(clock.status!=production_word_clock::Status::Ready)return false;
        data.origin=std::make_unique<ssg::OriginBuilder>(data.geometry,companion.origin,ssg::OriginMode::BindOnly);
        data.origin->advance(companion.origin,1);
        if(data.origin->status()!=ssg::Status::Ready)continue;
        sfb::Request request;request.origin=data.origin->result();request.target=entry.target;
        request.self=data.actual->snapshot;request.metadata=std::move(metadata);request.current=companion;
        request.start=clock.earliest_ready;request.horizon=cambc::constants::max_rounds;
        request.handoff_inputs=entry.handoff_inputs;request.capture_policy=entry.capture_policy;
        request.exact_own_state=request.exact_personal_life=request.project_omitted_clocks=true;
        data.target=entry.target;data.companion=companion;
        data.candidate=entry.publication;data.candidate_support=support;
        data.cash_routes=data.cash_cache.result();
        data.binding=std::make_unique<sfb::Builder>(std::move(request));data.preparing=true;return true;
    }
    // Reconstruction is a new residency of the SAME immutable recipe, under
    // genuinely changed support. One fresh named read per invocation keeps a
    // long retired history from becoming an unbounded SDK prefix.
    if(data.menu->retired_count()&&budget(50000)) {
        const auto index=data.reconstruction_cursor++%data.menu->retired_count();
        const auto* entry=data.menu->retired_at(index);
        if(entry&&entry->canonical_group>=0&&static_cast<std::size_t>(entry->canonical_group)<owner.canonical.count&&
           static_kit_matches(*entry,data.actual->snapshot)) {
            const auto local=owner.canonical.local_by_canonical[static_cast<std::size_t>(entry->canonical_group)];
            const auto life=admission_lifecycle(state,local,epoch,{position.x,position.y});
            if(life.status==Status::Ready&&life.availability!=Availability::Respawning&&current(epoch)) {
                const auto metadata=static_job_metadata(state,*data.actual,static_cast<std::uint8_t>(entry->canonical_group),life.availability);
                const auto companion=static_binding_current(*data.actual,metadata,data.geometry->key(),data.publication_query);
                data.menu->reconstruct(entry->publication_id,static_candidate_support(companion));
            }
        }
    }
    return false;
}
// Calculation consumes only an owning dated Request/Snapshot. No argument is
// presented as a later Current; successful computation grants no command or
// publication authority. Actual self/Life/CD/target admission remains separate.
[[maybe_unused]] bool advance_static_nomination(Controller& state,int epoch)
{
    auto& data=static_data(state);bool progressed=false;
    const auto finished=[&]{return !current(epoch);};
    if(!data.actual||!data.companion||!data.target){clear_static_query(data,false);return finished();}
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,15);
    while(budget(50000)&&current(epoch)) {
        if(data.binding) {
            const auto result=data.binding->advance_nomination(1);
            progressed|=data.binding->last_work()!=0;
            if(result==sfb::Status::InputReady) {
                if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::mark(
                    assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::BindingReady);
                if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,0x200u+static_cast<unsigned>(result));
                auto input=data.binding->take();const auto& result_geometry=data.target->result();
                sbp::Seed seed;seed.input=std::move(*input);
                seed.nomination={static_cast<std::size_t>(data.candidate),static_cast<std::uint8_t>(data.target->group()),
                    data.target->kit().family,data.target->perch(),result_geometry.capture_tile,result_geometry.release_tile,
                    result_geometry.bank_stand,result_geometry.home};
                data.model=sbp::Programme::make(std::move(seed));data.binding.reset();
                if(!data.model){clear_static_query(data,true);return finished();}
                progressed=true;
            } else if(result!=sfb::Status::NeedCompute) {
                if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,0x200u+static_cast<unsigned>(result));
                if(result==sfb::Status::Unsupported&&data.binding->reason()!=sfb::Reason::Allocation&&
                   data.binding->reason()!=sfb::Reason::Capacity)decline_static_candidate(state);
                else clear_static_query(data,true);
                return finished();
            }
        } else if(data.model) {
            if(data.model->valuation_ready()) {
                if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::mark(
                    assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::ValuationReady);
                if(!static_cash_matches(data)){clear_static_query(data,true);return finished();}
                const auto result=data.cash_cache.advance(1);
                progressed|=data.cash_cache.last_work()!=0;
                if(result==scr::Status::Ready){
                    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::mark(
                        assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::CashReady);
                    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,0x400u+static_cast<unsigned>(result));
                    data.cash_routes=data.cash_cache.result();return progressed||finished();}
                if(result!=scr::Status::NeedCompute) {
                    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,0x400u+static_cast<unsigned>(result));
                    if(result==scr::Status::Unsupported&&data.cash_cache.reason()!=scr::Reason::Allocation&&
                       data.cash_cache.reason()!=scr::Reason::Capacity)decline_static_candidate(state);
                    else clear_static_query(data,true);
                    return finished();
                }
                continue;
            }
            const auto result=data.model->advance(1);
            progressed|=data.model->last_work()!=0;
            if constexpr(cfg::assignment_reachability_audit)
                if(result==sbp::Status::ValuationReady)assignment_reachability_probe::mark(
                    assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::ValuationReady);
            if(result==sbp::Status::Unsupported) {
                if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::reject(
                    assignment_reachability_probe::role(),epoch,20,static_cast<unsigned>(result),
                    static_cast<unsigned>(data.model->producer_reason()),true);
                if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,0x300u+static_cast<unsigned>(result));
                if(completed_static_model_rejection(*data.model))
                    decline_static_candidate(state);
                else clear_static_query(data,true);
                return finished();
            }
            // Complete cash fields are part of the physical programme. Fresh
            // next-prefix collection precedes admission once those are sealed.
        } else {clear_static_query(data,false);return finished();}
    }
    if constexpr(cfg::assignment_reachability_audit)
        if(progressed)assignment_reachability_probe::progress(assignment_reachability_probe::role(),epoch);
    return progressed||finished(); // No current input is owned without finite progress.
}
[[maybe_unused]] bool static_assignment_input(Controller& state)
{
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(state.last_epoch,10);
    auto& data=static_data(state);auto& owner=assignment_data(state);
    if(data.active||!data.anchor)return false;
    if(data.independent_preparation&&data.nomination_group<0)return false;
    const int epoch=static_cast<int>(cambc::round_number());const auto& actor=cambc::self();
    const auto position=actor.position();
    if((data.independent_preparation?(state.phase!=Phase::Acquire||state.group>=0):
        (state.group!=data.incumbent_group||static_cast<int>(state.phase)!=data.incumbent_phase))||
       position!=data.anchor->pos||state.escape_required||(data.field&&data.field!=state.potential_generation))
    {clear_static_query(data,false);return !current(epoch);}
    if(!data.geometry||!data.menu||(!data.independent_preparation&&
        (state.route.status!=Status::Ready||state.route.graph_revision!=state.graph_revision))||
       data.checked_epoch==epoch||!immutable_camp_quiet(position))return false;
    if(data.nominated&&!data.preparing&&data.menu->ready_count()==0)return false;
    const auto flags=actor.state();
    if(!flags.contains(cambc::champion_state::alive)||!flags.contains(cambc::champion_state::can_move)||
       !flags.contains(cambc::champion_state::can_attack)||!flags.contains(cambc::champion_state::can_cast)||
       flags.contains(cambc::champion_state::recalling)||flags.contains(cambc::champion_state::submitting_gems)||
       flags.contains(cambc::champion_state::teleporting)||!cambc::vision::champions(cambc::team::enemy).empty())
    {clear_static_query(data,false);return !current(epoch);}
    if(!current(epoch))return true;
    data.checked_epoch=epoch;
    if(static_cash_matches(data))data.cash_routes=data.cash_cache.result();
    else data.cash_routes.reset();
    if(data.model&&data.model->valuation_ready()&&data.cash_routes)return publish_static_programme(state,epoch);
    // Retained nomination work is paid BEFORE the expensive fresh collector
    // and Life reads. The original dated inputs stay unchanged; only the
    // completed programme reaches the fresh publication gate above.
    if(data.preparing)return advance_static_nomination(state,epoch);
    bool created=false;
    if(!data.actual) {
        if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::begin_collect(epoch,11);
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_called(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial);
        const auto actual=assignment_api::collect_self(*data.anchor,epoch,1);
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_result(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial,
            static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
        if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::end_collect(epoch,
            static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
        if(actual.status!=assignment_api::CollectStatus::Ready||!current(epoch))
        {clear_static_query(data,false);return !current(epoch);}
        data.actual=actual;owner.self=actual;owner.body=assignment_hash(actual);
        owner.self->snapshot.body_revision=owner.body;data.actual->snapshot.body_revision=owner.body;
        owner.field=state.potential_generation;
    }
    if(!data.preparing) {
        // The earlier sample may have scheduled geometry on a different input.
        // A NEW body-dependent query gets actual SDK reads at its own date;
        // never redated Snapshot values or a newer Life receipt on an old self.
        if(data.actual->snapshot.epoch!=static_cast<std::uint32_t>(epoch)) {
            if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::begin_collect(epoch,13);
            if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_called(
                assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial);
            const auto actual=assignment_api::collect_self(*data.anchor,epoch,1);
            if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_result(
                assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial,
                static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
            if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::end_collect(epoch,
                static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
            if(actual.status!=assignment_api::CollectStatus::Ready||!current(epoch))
            {clear_static_query(data,false);return !current(epoch);}
            data.actual=actual;owner.self=actual;owner.body=assignment_hash(actual);
            owner.self->snapshot.body_revision=owner.body;data.actual->snapshot.body_revision=owner.body;
            owner.field=state.potential_generation;
        }
        const auto canonical=data.independent_preparation?data.nomination_group:
            owner.canonical.canonical_by_local[static_cast<std::size_t>(state.group)];
        if(canonical<0){clear_static_query(data,false);return false;}
        ssm::Request request;request.canonical_group=canonical;request.self=data.actual->snapshot;
        request.nomination={data.geometry->key().layout,data.geometry->key().potential,owner.body,data.publication_query,
            epoch,{position.x,position.y}};
        request.handoff_inputs.fill(1);data.menu->request(request);data.nominated=true;
        if(!begin_static_binding(state,epoch))return !current(epoch);
        created=true; // One actual retained preparation transition this input.
    }
    if(!data.companion||!static_life_matches(state,*data.companion,epoch,position)||
       static_geometry_revision(state,data.geometry->key())!=data.companion->key.geometry_revision)
    {if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,14);
     clear_static_query(data,false);return !current(epoch);}
    return advance_static_nomination(state,epoch)||created;
}
[[maybe_unused]] void advance_static_tail(Controller& state)
{
    auto& data=static_data(state);
    if(!state.potential||state.potential->status()!=mb::production::potential::Status::Ready)return;
    const int epoch=static_cast<int>(cambc::round_number());
    if(state.last_epoch!=epoch)return;
    if(data.field&&data.field!=state.potential_generation) {
        if(!budget(50000)||!current(epoch))return;
        retire_assignment(state,false);data.world.reset();data.geometry.reset();data.menu.reset();
        data.cash_cache.reset();
        data.canonical_work.reset();data.terrain.clear();data.cold_stage=0;
        data.unsupported_world=false;data.field=0;
    }
    if(data.unsupported_world)return;
    if(!data.world) {
        if(!budget(50000)||!current(epoch))return;
        auto& owner=assignment_data(state);
        if(data.cold_stage==0) {
            data.field=state.potential_generation;
            if(owner.canonical.supported)data.cold_stage=2;
            else {
                data.canonical_work=std::make_unique<assignment_static_canonical::CanonicalBuilder<World>>(
                    World::get(),mb::service::max_members);
                data.cold_stage=1;
            }
            return; // Allocation does not share admission with a traversal/export.
        }
        if(data.cold_stage==1) {
            while(budget(50000)&&current(epoch)) {
                const auto status=data.canonical_work->advance(1);
                if(status==assignment_static_canonical::Status::Ready) {
                    const auto& result=*data.canonical_work->result();
                    owner.canonical={result.supported,result.count,result.local_by_canonical,result.canonical_by_local};
                    data.canonical_work.reset();data.cold_stage=2;return;
                }
                if(status!=assignment_static_canonical::Status::NeedCompute){data.unsupported_world=true;return;}
            }
            return;
        }
        if(data.cold_stage==2) {
            // A NEW owned export. The live World's SDK bitboard is never moved.
            data.terrain=World::get().terrain.export_linear_buffer();data.cold_stage=3;return;
        }
        if(data.cold_stage==3) {
            data.world=std::make_unique<assignment_static_world::Builder<World,mb::production::potential::Field>>(
                World::get(),*state.potential,std::span<const int>{owner.canonical.local_by_canonical.data(),owner.canonical.count},
                data.field,std::move(data.terrain),assignment_static_geometry::Limits{},
                assignment_static_world::Policy{false,true});
            data.cold_stage=4;return; // Storage reserves are separately staged by Builder.
        }
        data.unsupported_world=true;return;
    }
    while(budget(50000)&&current(epoch)) {
        if(!data.geometry) {
            const auto result=data.world->advance(state.potential_generation,1);
            if(result==ssg::Status::Ready) {
                data.geometry=data.world->result();data.menu=std::make_unique<ssm::Menu>(data.geometry);
                if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::mark(
                    assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::WorldReady);
            } else if(result!=ssg::Status::NeedCompute) {data.unsupported_world=true;break;}
            if(!data.world->last_work())break;
        } else {
            // Static cash fields have priority over optional new target/perch
            // enrichment. Each field unit has a fresh independent admission;
            // Body/Life invalidation never throws this preparation away.
            if(data.cash_cache.status()==scr::Status::NeedCompute) {
                data.cash_cache.advance(1);
                if(!data.cash_cache.last_work())break;
                continue;
            }
            data.menu->advance(1);
            if(!data.menu->last_work())break;
        }
    }
    if constexpr(cfg::assignment_reachability_audit)
        if(data.menu&&data.menu->ready_count())assignment_reachability_probe::mark(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::MenuReady);
    if constexpr(cfg::assignment_reachability_audit)
        if(data.cash_cache.status()==scr::Status::Ready)assignment_reachability_probe::mark(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Milestone::CashReady);
}
// One funded first-programme preparation owner precedes the OPTIONAL global
// acquisition query. Actual capital, custody, recovery, escape and contact have
// priority. No published physical incumbent is displaced merely to enrich a menu.
[[maybe_unused]] bool static_preparation_input(Controller& state)
{
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(state.last_epoch,1);
    auto& data=static_data(state);auto& owner=assignment_data(state);
    if(state.phase!=Phase::Acquire||!state.acquisition||state.group>=0||state.escape_required||owner.choice||data.active||
       state.pending_route||state.request_assembly||data.unsupported_world||!state.potential||
       state.potential->status()!=mb::production::potential::Status::Ready||!static_audit_budget(70000,state.last_epoch,1))return false;
    const int epoch=static_cast<int>(cambc::round_number());const auto& actor=cambc::self();
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,2);
    const auto origin=actor.position();const auto flags=actor.state();
    if(!flags.contains(cambc::champion_state::alive)||!flags.contains(cambc::champion_state::can_move)||
       !flags.contains(cambc::champion_state::can_attack)||!flags.contains(cambc::champion_state::can_cast)||
       flags.contains(cambc::champion_state::recalling)||flags.contains(cambc::champion_state::submitting_gems)||
       flags.contains(cambc::champion_state::teleporting)||actor.gem_count()!=0||
       !immutable_camp_quiet(origin)||!cambc::vision::champion_tiles(cambc::team::enemy).is_empty())return false;
    if(World::get().own_spawn.is_set(origin)&&actor.health()<actor.max_health())return false;
    if(actor.position()!=origin||!current(epoch))return true;
    if(!data.geometry) {
        if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::mark(epoch,3);
        advance_static_tail(state);
        return !current(epoch); // Retained acquisition keeps its funded cheap continuation.
    }
    if(!data.actual||!data.anchor||!data.independent_preparation) {
        auto own_support=1469598103934665603ull;
        for(const auto value:{origin.x,origin.y,actor.attack_range(),static_cast<int>(actor.passive())})
            ssw::mix(own_support,static_cast<std::uint32_t>(value));
        for(const float value:{actor.health(),actor.max_health(),actor.strength(),actor.focus()})
            ssw::mix(own_support,std::bit_cast<std::uint32_t>(value));
        ssw::mix(own_support,actor.gold_count());ssw::mix(own_support,actor.gem_count());
        const auto counts=actor.item_count();ssw::mix(own_support,counts.size());
        for(const auto count:counts)ssw::mix(own_support,count);
        ssw::mix(own_support,actor.buffs().size());own_support=ssw::nonzero(own_support);
        if(!current(epoch)||actor.position()!=origin)return true;
        if(data.unsupported_collection==own_support)return false;
        assignment_api::SelfCheckpoint anchor;
        if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::begin_collect(epoch,4);
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_called(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial);
        const auto actual=assignment_api::collect_current_self(epoch,1,state.walked_this_turn,&anchor);
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::collector_result(
            assignment_reachability_probe::role(),epoch,assignment_reachability_probe::Collector::Initial,
            static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
        if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::end_collect(epoch,
            static_cast<unsigned>(actual.status),static_cast<unsigned>(actual.reason));
        if(actual.status!=assignment_api::CollectStatus::Ready||!current(epoch)) {
            if(current(epoch)&&actual.status==assignment_api::CollectStatus::Unsupported)
                data.unsupported_collection=own_support;
            return !current(epoch);
        }
        data.unsupported_collection=0;
        data.anchor=anchor;data.actual=actual;data.independent_preparation=true;
        owner.self=actual;owner.body=assignment_hash(actual);
        owner.self->snapshot.body_revision=owner.body;data.actual->snapshot.body_revision=owner.body;
        owner.field=state.potential_generation;data.publication_query=++data.serial;data.checked_epoch=-1;
        if(data.preparation_body!=owner.body) {
            data.preparation_body=owner.body;data.nomination_mask=0;data.nomination_group=-1;data.nominated=false;
            data.nomination_scan=data.nomination_member=0;data.nomination_best=-1;
            data.nomination_distance=std::numeric_limits<int>::max();
        }
    }
    if(!data.preparing&&(!data.nominated||(data.menu->complete()&&data.menu->ready_count()==0))) {
        // This is a geometric nomination order only. Complete work/cash/return
        // valuation, actual Life and fresh physical admission still decide use.
        while(budget(50000)&&current(epoch)&&data.nomination_scan<data.geometry->catalogue().size()) {
            const auto i=data.nomination_scan;
            if(data.nomination_mask&(std::uint32_t{1}<<i))
                {++data.nomination_scan;data.nomination_member=0;continue;}
            const auto& members=data.geometry->catalogue()[i].members;
            if(data.nomination_member==members.size())
                {++data.nomination_scan;data.nomination_member=0;continue;}
            const auto& member=members[data.nomination_member++];
            const auto lower=distance(origin,{member.pos.x,member.pos.y});
            if(lower<data.nomination_distance){data.nomination_distance=lower;data.nomination_best=static_cast<int>(i);}
        }
        if(!current(epoch))return true;
        if(data.nomination_scan<data.geometry->catalogue().size())return false;
        const int selected=data.nomination_best;
        data.nomination_scan=data.nomination_member=0;data.nomination_best=-1;
        data.nomination_distance=std::numeric_limits<int>::max();
        if(selected<0){clear_static_query(data,false);return false;}
        data.nomination_group=selected;data.nomination_mask|=std::uint32_t{1}<<selected;data.nominated=false;
    }
    if(static_assignment_input(state))return true;
    if(!current(epoch))return true;
    if(!data.independent_preparation)return false;
    advance_static_tail(state);
    return !current(epoch); // Pure enrichment cannot consume the incumbent's remaining physical budget.
}
