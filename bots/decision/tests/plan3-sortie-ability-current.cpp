// Current queried Tavi/wolf designs over the explicit inherited 8x8 SDK host.
// This file never injects a Campaign intent/candidate or loads a native engine.
int (*sortie_cast_probe)(unsigned,int*)=nullptr;
void (*sortie_query_probe)(unsigned)=nullptr;
bool sortie_bad_kit=false;
#if defined(P3_SORTIE_ROWS)
namespace {bool rows_meter_enabled=false;unsigned rows_sdk_query_cost=16;}
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "sortie-basic-fixture.h"
#pragma clang diagnostic pop

namespace {
#if !defined(P3_SORTIE_ROWS)
unsigned ability_paths=0,ability_cold_choices=0,ability_cold_conversions=0,ability_faults=0,ability_hidden=0;
unsigned ability_bursts=0,ability_resets=0;
unsigned cursor_cases=0,cursor_pending=0,cursor_resumes=0,cursor_mutations=0,phase_bindings=0,phase_faults=0;
unsigned cold_window_cuts=0;
void current_unfunded() {
    reset_case();
    verify(actor.health==900 && actor.max_health==900 && actor.strength==45 && actor.focus==110);
    verify(gold==0 && bag==0 && std::all_of(items.begin(),items.end(),[](auto x){return x==0;}));
}
SortieFixture ability_fixture(bool hidden=false) {
    current_unfunded();auto campaign=std::make_unique<Campaign>(Controller::ledger,false,"Tavi-current-ability-fixture");
    initialize(*campaign);const auto stats=campaign->capital_.exact_stats();verify(stats.has_value());
    verify(stats->health==900 && stats->strength==45 && stats->focus==110);
    const auto index=std::size_t(campaign->group_index(bp_key(1)));
    verify(index<campaign->personal_.groups.size());
    verify(!plan3::estimate_clear(4050,60,45,110,1,1,900,900).admitted);
    sr::Builder builder;if(hidden)visible[0]=0;
    const auto hp=calls("sdk_camp"),states=calls("sdk_camp_state");
    std::unique_ptr<sr::Candidate> candidate;
    for(unsigned n=0;n<128;++n) {
        campaign->paths_.begin_turn();auto result=builder.prepare(campaign->personal_,index,*stats,campaign->paths_,[]{return true;});
        if(result.status==sr::Status::pending){advance_round();continue;}
        if(!result.candidate)std::fprintf(stderr,"ABILITY prepare status=%u reason=%s model=%u/%u\n",unsigned(result.status),result.reason,result.model?unsigned(result.model->state):99,result.model?unsigned(result.model->reason):99);
        verify(result.status==sr::Status::ready && result.candidate);candidate=std::move(result.candidate);break;
    }
    verify(bool(candidate) && candidate->input.tavi.enabled && candidate->input.tavi.known);
    verify(candidate->input.tavi.focus==110 && candidate->input.tavi.burstModifier==double(float(1.4)));
    verify(candidate->input.tavi.resetMaxCooldown==8 && candidate->input.tavi.burstMaxCooldown==8);
    verify(candidate->input.tavi.burstRange==4 && !candidate->accounting.stunCredited);
    verify(candidate->accounting.deltaS==66 && candidate->accounting.sortieCount<=4 && candidate->accounting.attackCount<=512);
    verify(candidate->accounting.burstCount>0 && candidate->accounting.resetCount>0);
    if(hidden){verify(calls("sdk_camp")==hp && calls("sdk_camp_state")==states);++ability_hidden;}
    visible[0]=~std::uint64_t{0};return {std::move(campaign),std::move(candidate),0};
}
void ability_complete() {
    auto fixture=ability_fixture(true);auto& plan=fixture.candidate->plan;
    const unsigned began=current_round;const int modeled=fixture.candidate->accounting.tau;
    bool held=false,waiting=false,recovered=false;
    for(unsigned n=0;n<512 && !own_score;++n) {
        const auto phase=plan.phase;const auto cast_before=casts;
        const auto result=sortie_step(fixture);
        verify(plan.remainingModelEvaluations==0);
        if(!fixture.candidate->plan.combatAt && plan.combatBound)++phase_bindings;
        if(result.status==sr::Status::abort || result.status==sr::Status::cutoff)
            std::fprintf(stderr,"ABILITY step status=%u phase=%u reason=%s round=%u hp=%.17g actions=%u casts=%u attacks=%u\n",unsigned(result.status),unsigned(plan.phase),result.reason,current_round,double(actor.health),actions,casts,attacks);
        verify(result.status==sr::Status::handled || result.status==sr::Status::ordinary_handoff);
        verify(casts-cast_before<=1 && attacks<=512 && plan.sortie<4);
        held|=plan.phase==sr::Phase::escape || plan.phase==sr::Phase::recall;
        recovered|=phase==sr::Phase::healing && plan.phase==sr::Phase::returning;
        if(result.status==sr::Status::ordinary_handoff){verify(plan.phase==sr::Phase::conversion_wait && bag==66 && own_score==0);waiting=true;}
        advance_round();
    }
    verify(own_score==66 && bag==0 && captures==1 && submissions==1 && casts>0 && held && recovered && waiting);
    verify(current_round-began<=unsigned(modeled));
    const auto endpoint=sortie_step(fixture);verify(endpoint.status==sr::Status::abort && plan.phase==sr::Phase::conversion_wait);
    ability_bursts+=fixture.candidate->accounting.burstCount;ability_resets+=fixture.candidate->accounting.resetCount;++ability_paths;
    std::printf("SORTIE_ABILITY_PATH {\"side\":%u,\"modeledTurns\":%d,\"actualTurns\":%u,\"casts\":%u,\"basics\":%u,\"gems\":66}\n",bp_side(),modeled,current_round-began,casts,attacks);
}
int failed_cast(unsigned,int* success){*success=0;return 1;}
int drift_cast(unsigned,int* success){++current_round;*success=0;return 1;}
void ability_fault_cases() {
    for(int code:{97,98}) {
        auto fixture=ability_fixture();arrive_at_q(fixture);
        camp_state_error_id=fixture.candidate->plan.members[0].native;camp_state_error_code=code;
        const auto action_before=actions;const auto result=sortie_step(fixture);
        verify(result.status==sr::Status::abort && actions==action_before && casts==0);++ability_faults;
    }
    for(unsigned mode=0;mode<2;++mode) {
        auto fixture=ability_fixture();arrive_at_q(fixture);const auto hp=member_health;
        sortie_cast_probe=mode?drift_cast:failed_cast;const auto result=sortie_step(fixture);sortie_cast_probe=nullptr;
        verify(result.status==sr::Status::abort && casts==0 && attacks==0 && member_health==hp && fixture.candidate->plan.combatAt==0);++ability_faults;
    }
    // Bind a real first burst, then deny the already planned next-turn reset
    // or basic. No missing cast/allowance is converted into retained damage.
    for(unsigned mode=0;mode<3;++mode) {
        auto fixture=ability_fixture();arrive_at_q(fixture);
        verify(sortie_step(fixture).status==sr::Status::handled && casts==1);advance_round();
        if(mode==0)cooldown[0]=8;
        if(mode==1)actor.state&=~(1<<11);
        if(mode==2)actor.state&=~(1<<4);
        const auto cast_before=casts;const auto result=sortie_step(fixture);
        verify(result.status==sr::Status::abort && casts==cast_before);++ability_faults;
    }
    auto fixture=ability_fixture();arrive_at_q(fixture);
    // A queried walked/no-basic state cannot authorize the movement-exclusive
    // burst. The reset remains walking-compatible; this is an allowance fault,
    // not a synthetic physical movement or a claim of engine equivalence.
    actor.state&=~((1<<3)|(1<<4));
    const auto before=casts;const auto result=sortie_step(fixture);
    verify(result.status==sr::Status::abort && casts==before);
    verify(cooldown[1]==0);++ability_faults;
}
void cursor_builder_cases() {
    auto fixture=ability_fixture();auto& c=*fixture.campaign;
    const auto index=std::size_t(c.group_index(bp_key(1)));const auto stats=c.capital_.exact_stats();verify(stats.has_value());
    sr::Builder builder;std::unique_ptr<sr::Candidate> complete;unsigned previous=0;bool private_complete_pending=false;
    for(unsigned n=0;n<128 && !complete;++n) {
        fuel=100000;c.paths_.begin_turn();
        const unsigned limit=(builder.evaluation_?builder.evaluation_->owner_turns():0)+2;
        const auto ready=[&] {if(builder.evaluation_ && builder.evaluation_->owner_turns()>=limit)fuel=44000;return fuel>=45000;};
        auto result=builder.prepare(c.personal_,index,*stats,c.paths_,ready);
        if(result.model) {
            verify(result.model->chunkOwnerTurns<=4 && result.model->completedOwnerTurns>=previous);
            cursor_resumes+=result.model->resumed;previous=result.model->completedOwnerTurns;
            if(result.model->complete && !result.candidate)private_complete_pending=true;
        }
        if(result.candidate){verify(result.status==sr::Status::ready);complete=std::move(result.candidate);break;}
        verify(result.status==sr::Status::pending || result.status==sr::Status::cutoff);++cursor_pending;
        fuel=100000;advance_round();
    }
    verify(complete && cursor_resumes>0 && cursor_pending>2 && private_complete_pending);++cursor_cases;
    // A terminal private result is reusable only after fresh exact semantic
    // AND physical keys. Equal-distance origins deliberately exercise both.
    const auto saved=actor;const auto old_items=items;const auto old_cooldown=cooldown;const auto old_round=current_round;
    for(unsigned mode=0;mode<5;++mode) {
        actor=saved;items=old_items;cooldown=old_cooldown;current_round=old_round;fuel=100000;
        // Warm the completed key again after the preceding independent fault.
        for(unsigned n=0;n<64;++n){c.paths_.begin_turn();auto r=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});if(r.candidate)break;verify(r.status==sr::Status::pending);advance_round();}
        if(mode==0)--actor.health;
        if(mode==1){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
        if(mode==2)actor.y=bp_axis(1);
        if(mode==3)cooldown[1]=1;
        if(mode==4)actor.state&=~(1<<11);
        const auto changed=c.capital_.exact_stats();verify(changed.has_value());c.paths_.begin_turn();
        auto result=builder.prepare(c.personal_,index,*changed,c.paths_,[]{return true;});
        verify(!result.candidate);
        if(result.model)verify(!result.model->resumed && result.model->chunkOwnerTurns<=4);
        if(mode==2)verify(!builder.has_complete_work()); // R23 rejects the physical key before advancing a replacement model.
        ++cursor_mutations;
    }
    actor=saved;items=old_items;cooldown=old_cooldown;current_round=old_round;fuel=100000;
    for(unsigned n=0;n<64;++n){c.paths_.begin_turn();auto r=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});if(r.candidate)break;verify(r.status==sr::Status::pending);advance_round();}
    current_round=unsigned(cambc::constants::max_rounds)-1;c.paths_.begin_turn();
    auto expired=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(!expired.candidate && expired.status==sr::Status::pending && !builder.has_complete_work());++cursor_mutations;
}
int drift_actor(unsigned,void*){++current_round;sortie_champion_probe=nullptr;return 0;}
void cached_phase_fault_cases() {
    for(unsigned mode=0;mode<9;++mode) {
        auto fixture=ability_fixture();arrive_at_q(fixture);auto& plan=fixture.candidate->plan;
        const auto actions_before=actions;
        if(mode==0)actor.health=plan.accounting.phases[0].entryEndHealth-1;
        if(mode==1){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
        if(mode==2)--actor.x;
        if(mode==3)cooldown[1]=unsigned(plan.accounting.phases[0].combatEntryBurstCooldown+1);
        if(mode==4)actor.state&=~(1<<11);
        if(mode==5)member_health[plan.members[0].native]=plan.input.members[0].health+1;
        if(mode==6){member_health[plan.members[0].native]=0;member_states[plan.members[0].native]=0;}
        if(mode==7)plan.deadline=int(current_round)+plan.accounting.tau-plan.accounting.phases[0].combatStartedAt-1;
        if(mode==8)sortie_champion_probe=drift_actor;
        const auto result=sortie_step(fixture);sortie_champion_probe=nullptr;
        verify((result.status==sr::Status::abort || result.status==sr::Status::cutoff) && actions==actions_before && !casts && !attacks);
        verify(plan.remainingModelEvaluations==0);++phase_faults;
    }
}
Campaign* window_campaign=nullptr;
unsigned window_limit=0;
std::int64_t cold_window_fuel() {
    // Exactly the production execution floor. This is a guard-window fixture,
    // not a native fuel-consumption measurement.
    if(window_campaign->sortie_episode_)return 45000;
    const auto& cursor=window_campaign->sortie_builder_.evaluation_;
    if(cursor && cursor->owner_turns()>=window_limit){++cold_window_cuts;return 44000;}
    return 100000;
}
void ability_cold_campaign(Controller controller) {
    current_unfunded();auto campaign=std::make_unique<Campaign>(controller,false,"Tavi-current-unfunded-cold");
    bool selected=false,executed=false,ordinary=false;JobId id;unsigned selected_at=0;
    for(unsigned n=0;n<512 && campaign->telemetry_.converted!=66;++n) {
        window_campaign=campaign.get();
        window_limit=(campaign->sortie_builder_.evaluation_?campaign->sortie_builder_.evaluation_->owner_turns():0)+2;
        optional_fuel_probe=cold_window_fuel;
        campaign->turn();
        optional_fuel_probe=nullptr;
        if(campaign->personal_.initialization_complete) {
            // Retain the original two distinct gem groups and gold group;
            // neither the ordinary menu nor its competing physical rows is
            // replaced by a supplied intent/candidate in this cold callback.
            verify(campaign->personal_.groups.size()==3);
            verify(std::count_if(campaign->personal_.groups.begin(),campaign->personal_.groups.end(),[](const auto& g){return g.gem_value>0;})==2);
            verify(std::count_if(campaign->personal_.groups.begin(),campaign->personal_.groups.end(),[](const auto& g){return !g.gem_value && g.gold_value>0;})==1);
        }
        if(campaign->sortie_episode_) {
            verify(campaign->active_ && campaign->active_->id.kind==JobKind::cycle);
            if(!selected){selected=true;id=campaign->active_->id;selected_at=current_round;verify(gold==0 && buys==0);}
            verify(campaign->active_->id==id && campaign->sortie_episode_->input.tavi.enabled);
            verify(campaign->sortie_episode_->remainingModelEvaluations==0);
            executed|=casts>0 && attacks>0;
        } else if(campaign->active_ && (campaign->active_->id.kind==JobKind::cycle || campaign->active_->id.kind==JobKind::double_cycle)) {
            // R23 preserves genuine ordinary work while private macro work is
            // pending. This legacy8x8 case stops at that actual cold choice;
            // its direct66 endpoint cases above remain mechanic evidence.
            ordinary=true;id=campaign->active_->id;selected_at=current_round;break;
        }
        if(campaign->telemetry_.converted!=66)advance_round();
    }
    verify(campaign->telemetry_.decisions>0 && (ordinary || selected));++ability_cold_choices;
    if(!ordinary) {
        verify(executed && campaign->telemetry_.completed>0 && campaign->telemetry_.converted==66);
        verify(own_score==66 && !bag && captures==1 && submissions==1 && !campaign->sortie_episode_);++ability_cold_conversions;
    }
    std::printf("SORTIE_ABILITY_COLD_CHOICE {\"side\":%u,\"controller\":%u,\"selectedRound\":%u,\"macroSelected\":%s,\"job\":[%d,%d,%d,%d,%d],\"physicalConverted\":%u,\"scope\":\"legacy8x8 first actual choice; direct66 mechanics separate\"}\n",bp_side(),unsigned(controller),selected_at,selected?"true":"false",int(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock,campaign->telemetry_.converted);
}
#else
// Actual retained public50x50 Rows map/designs. Initial visibility, no opponents,
// lifecycle and deterministic action transitions remain declared SDK-host inputs.
unsigned rows_paths=0,rows_fast=0,rows_mutations=0,rows_ordinary=0,rows_pending=0;
std::unique_ptr<Campaign> rows_ready();
Campaign* rows_window_campaign=nullptr;
std::int64_t rows_window_fuel(){
    if(rows_window_campaign->sortie_builder_.evaluation_&&rows_window_campaign->sortie_builder_.evaluation_->owner_turns()>=2)fuel=44000;
    return fuel;
}
int rows_drift_actor(unsigned,void*){++current_round;sortie_champion_probe=nullptr;return 0;}
void rows_reset() {
    rows_meter_enabled=false;reset_case();fuel=100000;
    actor.x=actor.y=bp_side()?47:2;actor.health=actor.max_health=900;actor.strength=45;actor.focus=110;
    raw_gems={12,12,19,19,26,26,470,0,0};raw_gold={180,180,160,160,120,120,600,80,80};
    group_state.fill(3);member_states.fill(0);member_health.fill(0);respawn_count.fill(0);
    const unsigned target=bp_side(),ordinary=bp_side()?7:8;group_state[target]=group_state[ordinary]=1;
    for(unsigned id=0;id<54;++id)if(bp_group[id]==target || bp_group[id]==ordinary) {
        cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));
        member_states[id]=2;member_health[id]=design.base_stats.base_health;
    }
    rows_vision(visible.data(),actor.x,actor.y);
    verify(!gold&&!bag&&!own_score&&std::all_of(items.begin(),items.end(),[](auto n){return !n;}));
}
void rows_input(const sr::Candidate& candidate) {
    const auto& in=candidate.input;const auto& p=candidate.plan;const bool bottom=bp_side();
    const int a[]={168,169,218,219,268,269},b[]={2230,2231,2280,2281,2330,2331};
    verify(bp_key(bp_side())==(bottom?2230:168));
    verify(in.memberCount==6&&in.gems==12&&in.health==900&&in.maximumHealth==900&&in.strength==45&&in.tavi.focus==110);
    verify(in.round==int(current_round)&&in.horizon==int(cambc::constants::max_rounds-current_round)&&in.campDamage==1&&in.mitigation==1);
    for(unsigned i=0;i<6;++i){const bool wolf=bottom?i%2!=0:i%2==0;verify(in.members[i].key==(bottom?b[i]:a[i])&&in.members[i].health==(wolf?1350:120)&&in.members[i].incoming==(wolf?20:0));}
    verify(p.decisionOrigin==cambc::vec2{bottom?47:2,bottom?47:2});
    verify(p.origin==cambc::vec2{bottom?33:16,bottom?43:2});
    verify(in.outward.turns==14&&in.outward.exposureTurns==1&&in.escape.turns==3&&in.escape.exposureTurns==2);
    verify(in.returnToCamp.turns==14&&in.returnToCamp.exposureTurns==1);
    verify(in.capture.turns==(bottom?3:2)&&in.bank.turns==(bottom?12:15));
    verify(in.recallTicks==5&&in.captureTicks==5&&in.tributeTicks==2&&in.spawnHealing==1);
    verify(candidate.accounting.sortieCount==5&&candidate.accounting.deltaS==12&&candidate.accounting.tau==(bottom?193:191));
    verify(!candidate.accounting.stunCredited&&candidate.accounting.attackCount<=512);
    std::printf("SORTIE_ROWS_INPUT {\"side\":%u,\"members\":6,\"gems\":12,\"sorties\":5,\"tau\":%d,\"outward\":[14,1],\"escape\":[3,2],\"return\":[14,1],\"capture\":%d,\"bank\":%d,\"declaredQuietHost\":true}\n",bp_side(),candidate.accounting.tau,in.capture.turns,in.bank.turns);
}
void rows_pending_fairness() {
    auto c=rows_ready();auto& builder=c->sortie_builder_;builder.evaluation_.reset();
    const auto index=std::size_t(c->group_index(bp_side()?2230:168));const auto stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto context_before=c->forecast_context(c->information());const auto pending_actions=actions;
    fuel=80000;rows_meter_enabled=true;
    rows_window_campaign=c.get();optional_fuel_probe=rows_window_fuel;
    c->prepare_sortie(context_before,*stats,true);
    optional_fuel_probe=nullptr;rows_window_campaign=nullptr;rows_meter_enabled=false;
    verify(!c->sortie_candidate_&&fuel<45000&&builder.evaluation_&&!builder.has_complete_work());
    verify(c->sortie_preparation_trace_&&c->sortie_preparation_trace_->model&&!c->sortie_preparation_trace_->model->complete);
    // Flush that genuine deferred caller trace through the actual low-fuel
    // callback. The turn cannot plan/act below45k; it does not invent a trace.
    rows_meter_enabled=true;c->turn();rows_meter_enabled=false;
    verify(actions==pending_actions&&!c->active_&&!c->sortie_preparation_trace_);
    const auto held=builder.evaluation_->owner_turns(),before=actions;advance_round();c->paths_.begin_turn();
    fuel=80000;rows_meter_enabled=true;
    const auto no=builder.prepare(c->personal_,index,*stats,c->paths_,[]{return fuel>=45000;},false);
    verify(no.status==sr::Status::pending&&!no.candidate&&builder.evaluation_->owner_turns()==held);
    const auto camps=cambc::vision::camps();c->personal_.observe(camps,{});auto state=c->information();
    auto context=c->forecast_context(state);const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,{actor.x,actor.y});
    const auto jobs=c->menu(context,false,camps,prefix);rows_meter_enabled=false;
    unsigned ordinary=0;for(const auto& job:jobs)if((job.id.kind==JobKind::cycle||job.id.kind==JobKind::double_cycle)&&plan3::admitted(state,job,false))++ordinary;
    verify(fuel>=60000&&ordinary>0&&actions==before&&!c->active_);++rows_pending;
    std::printf("SORTIE_ROWS_WINDOW {\"side\":%u,\"stagedReadyDenial\":true,\"initialFuel\":80000,\"deniedFuel\":44000,\"privateTurns\":%u,\"ordinaryFuelAfter\":%lld,\"ordinaryRows\":%u,\"sdkQueryCost\":%u,\"nativeFuelClaim\":false}\n",bp_side(),held,static_cast<long long>(fuel),ordinary,rows_sdk_query_cost);
}
std::unique_ptr<Campaign> rows_ready() {
    rows_reset();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-prepared-work");initialize(*c);
    verify(c->personal_.altars.size()==2&&c->personal_.spawn_positions.size()==16&&c->personal_.groups.size()==9);
    const auto index=std::size_t(c->group_index(bp_side()?2230:168));verify(index<c->personal_.groups.size());
    const auto stats=c->capital_.exact_stats();verify(stats.has_value());
    for(unsigned n=0;n<512;++n) {
        c->paths_.begin_turn();const auto result=c->sortie_builder_.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});
        if(result.candidate){rows_input(*result.candidate);verify(c->sortie_builder_.has_complete_work());return c;}
        if(result.status!=sr::Status::pending&&result.status!=sr::Status::cutoff)std::fprintf(stderr,"ROWS prepare %u %s at%u\n",unsigned(result.status),result.reason,current_round);
        verify(result.status==sr::Status::pending||result.status==sr::Status::cutoff);advance_round();
    }
    verify(false);return c;
}
void rows_fast_publication() {
    auto c=rows_ready();const auto index=std::size_t(c->group_index(bp_side()?2230:168));const auto stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto turns=c->sortie_builder_.evaluation_->owner_turns(),design=calls("sdk_camp_design"),vision=calls("sdk_hypothetical_vision");
    fuel=80000;rows_meter_enabled=true;
    auto fast=c->sortie_builder_.prepare(c->personal_,index,*stats,c->paths_,[]{return fuel>=45000;},false);
    rows_meter_enabled=false;
    verify(fast.candidate&&fast.status==sr::Status::ready&&fuel>=60000);
    verify(calls("sdk_camp_design")==design&&calls("sdk_hypothetical_vision")==vision&&c->sortie_builder_.evaluation_->owner_turns()==turns);
    rows_input(*fast.candidate);++rows_fast;
    // Incomplete nonadvancing callbacks do not even enter an SDK import.
    sr::Builder incomplete;const auto queries=query_trace.size();
    const auto no=incomplete.prepare(c->personal_,index,*stats,c->paths_,[]{return true;},false);
    verify(no.status==sr::Status::pending&&!no.candidate&&query_trace.size()==queries);
    const auto saved=actor;const auto old_items=items;const auto old_cd=cooldown;const auto old_round=current_round;
    for(unsigned mode=0;mode<7;++mode) {
        if(mode){c=rows_ready();}
        const auto selected=std::size_t(c->group_index(bp_side()?2230:168));
        if(mode==0)--actor.health;
        if(mode==1)actor.y+=bp_side()?-1:1; // Equal outward distance, changed physical origin.
        if(mode==2){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
        if(mode==3)cooldown[1]=1;
        if(mode==4)actor.state&=~(1<<11);
        if(mode==5)sortie_champion_probe=rows_drift_actor;
        if(mode==6)current_round=unsigned(cambc::constants::max_rounds)-1;
        const auto changed=c->capital_.exact_stats();verify(changed.has_value());const auto before=actions;
        const auto dirty=c->sortie_builder_.prepare(c->personal_,selected,*changed,c->paths_,[]{return true;},false);
        sortie_champion_probe=nullptr;verify(!dirty.candidate&&actions==before);++rows_mutations;
        actor=saved;items=old_items;cooldown=old_cd;current_round=old_round;
    }
    for(unsigned mode=0;mode<2;++mode) {
        c=rows_ready();const auto selected=std::size_t(c->group_index(bp_side()?2230:168));const auto fresh_stats=c->capital_.exact_stats();verify(fresh_stats.has_value());
        auto alternate=std::make_unique<PersonalInformation>(c->personal_);auto* personal=&c->personal_;
        if(!mode)personal=alternate.get();
        else {const auto old_buffer=c->personal_.terrain.ptr();cambc::bitboard replacement(c->personal_.terrain);c->personal_.terrain=std::move(replacement);verify(c->personal_.terrain.ptr()!=old_buffer);}
        const auto before=actions;const auto changed=c->sortie_builder_.prepare(*personal,selected,*fresh_stats,c->paths_,[]{return true;},false);
        verify(!changed.candidate&&changed.status==sr::Status::pending&&!c->sortie_builder_.has_complete_work()&&actions==before);++rows_mutations;
    }
}
void rows_campaign(Controller controller,bool prepared,bool metrics_flow=false) {
    auto c=prepared?rows_ready():std::unique_ptr<Campaign>{};
    if(!prepared){rows_reset();c=std::make_unique<Campaign>(controller,false,"Rows-cold-query-meter");}
    else verify(controller==Controller::ledger);
    bool selected=false;JobId original;unsigned committed=0;unsigned ordinary_callbacks=0;
    for(unsigned n=0;n<1400;++n) {
        fuel=100000;rows_meter_enabled=true;c->turn();rows_meter_enabled=false;
        if(c->sortie_episode_) {
            if(!selected){selected=true;original=c->active_->id;committed=current_round;if(prepared){verify(!gold&&!buys&&actor.max_health==900&&actor.strength==45&&actor.focus==110);verify(c->sortie_episode_->accounting.sortieCount==5);}}
            verify(c->active_&&c->active_->id==original&&c->sortie_episode_->remainingModelEvaluations==0);
        } else if(c->active_){++ordinary_callbacks;verify(controller!=Controller::atlas||!c->sortie_episode_);}
        if(!prepared&&c->active_&&(c->active_->id.kind==JobKind::cycle||c->active_->id.kind==JobKind::double_cycle)) {
            if(!selected){original=c->active_->id;committed=current_round;}
            if(controller==Controller::atlas){verify(c->information().horizon()==128&&c->active_->id.firstGroup!=int(bp_side()?2230:168));++rows_ordinary;}
            break; // Cold fairness ends at a genuine productive choice, never at Renew fallback.
        }
        if(c->telemetry_.converted==12)break;
        advance_round();
    }
    if(prepared) {
        if(!selected||c->telemetry_.converted!=12)std::fprintf(stderr,"ROWS endpoint failed selected%u round%u conversion%u hp%g actions%u\n",unsigned(selected),current_round,c->telemetry_.converted,double(actor.health),actions);
        verify(selected&&c->telemetry_.converted==12&&own_score==12&&!bag&&captures==1&&submissions==1&&!c->sortie_episode_);++rows_paths;
    } else if(controller==Controller::ledger)verify(selected||ordinary_callbacks>0); // Never freeze the ordinary actor for private work.
    std::printf("SORTIE_ROWS_CALLER {\"side\":%u,\"controller\":%u,\"preparedWork\":%s,\"selected\":%s,\"job\":[%d,%d,%d,%d,%d],\"commitRound\":%u,\"finalRound\":%u,\"converted\":%u,\"ordinaryCallbacks\":%u,\"sdkQueryCost\":%u,\"nativeFuelClaim\":false}\n",bp_side(),unsigned(controller),prepared?"true":"false",selected?"true":"false",int(original.kind),original.firstGroup,original.secondGroup,original.altarTile,original.equipmentBlock,committed,current_round,c->telemetry_.converted,ordinary_callbacks,rows_sdk_query_cost);
    if(metrics_flow) {
        const auto m=c->sortie_builder_.metrics().snapshot();
        std::printf("SORTIE_METRICS_ENDPOINT {\"side\":%u,\"converted\":%u,\"modelStarts\":%lld,\"modelTerminals\":%lld,\"modelSchedules\":%lld,\"modelResets\":%lld,\"postConversionReset\":%lld,\"discardedCompletedOwnerTurns\":%lld,\"candidateSnapshots\":%lld,\"forecasts\":%lld,\"episodes\":%lld,\"conversions\":%lld,\"aborts\":%lld,\"job\":[%d,%d,%d,%d,%d],\"nativeFuelClaim\":false}\n",bp_side(),c->telemetry_.converted,static_cast<long long>(m[3]),static_cast<long long>(m[4]),static_cast<long long>(m[5]),static_cast<long long>(m[6]),static_cast<long long>(m[9]),static_cast<long long>(m[7]),static_cast<long long>(m[13]),static_cast<long long>(m[14]),static_cast<long long>(m[15]),static_cast<long long>(m[16]),static_cast<long long>(m[17]),int(original.kind),original.firstGroup,original.secondGroup,original.altarTile,original.equipmentBlock);
        verify(prepared&&selected&&m[3]==1&&m[4]==1&&m[5]==1&&m[6]==1&&m[13]>=2);
        verify(m[14]==1&&m[15]==1&&m[16]==1&&m[17]==0);
        // After conversion the same callback resumes ordinary idle planning.
        // Its completed input still belongs to the original physical origin,
        // so one typed origin invalidation is expected, not lost completion.
        verify(cambc::vec2{actor.x,actor.y}!=c->sortie_builder_.evaluationOrigin_);
        verify(m[9]==int(sr::ResetReason::origin)&&m[7]==m[12]&&m[7]>0);
    }
}
void rows_offspawn() {
    rows_reset();current_round=bp_side()?568:784;
    actor.x=bp_side()?2:49;actor.y=bp_side()?31:16;gold=bp_side()?8:24;
    items.fill(0);
    if(bp_side()){items[unsigned(cambc::item_type::monster_emblem_of_strength)]=1;items[unsigned(cambc::item_type::jungle_shard_of_health)]=1;}
    else items[unsigned(cambc::item_type::monster_shard_of_strength)]=4;
    sync_stats();actor.health=bp_side()?720:900;cooldown.fill(0);cooldown[0]=bp_side()?1:0;
    verify(actor.max_health==(bp_side()?975:900)&&actor.strength==(bp_side()?70:65)&&actor.focus==110&&actor.attack_range==3);
    group_state.fill(3);member_states.fill(0);member_health.fill(0);
    // Quiet declared lifecycle: nearby real Gold is alive, plus the real gem
    // targets. Hidden current HP is not reconstructed from the saved replay.
    for(unsigned id=0;id<54;++id)if(bp_group[id]==(bp_side()?8u:7u)||bp_group[id]==(bp_side()?5u:4u)||(bp_side()&&bp_group[id]==1u)) {
        group_state[bp_group[id]]=1;member_states[id]=2;
        cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));member_health[id]=design.base_stats.base_health;
    }
    rows_vision(visible.data(),actor.x,actor.y);
}
std::vector<int> rows_q_reference(const PersonalInformation& personal,std::size_t index,int range) {
    auto blocked=personal.terrain;for(const auto& g:personal.groups)blocked|=g.spawns;
    const auto& g=personal.groups[index];const auto first=*std::min_element(g.spawn_positions.begin(),g.spawn_positions.end(),[](auto a,auto b){return a.y*50+a.x<b.y*50+b.x;});
    std::vector<int> result;
    for(int y=first.y-range;y<=first.y+range;++y)for(int x=first.x-range;x<=first.x+range;++x) {
        const cambc::vec2 p{x,y};if(x<0||y<0||x>=50||y>=50||blocked.is_set(p))continue;
        if(!std::all_of(g.spawn_positions.begin(),g.spawn_positions.end(),[&](auto s){return std::max(std::abs(s.x-x),std::abs(s.y-y))<=range;}))continue;
        const auto vision=cambc::self().visible_tiles_from(p);
        if(std::all_of(g.spawn_positions.begin(),g.spawn_positions.end(),[&](auto s){return vision.is_set(s);}))result.push_back(y*50+x);
    }
    std::sort(result.begin(),result.end());return result;
}
void rows_q_set(const sr::Builder& builder,const std::vector<int>& reference) {
    std::vector<int> actual;for(unsigned i=0;i<builder.qFeasibleCount_;++i)actual.push_back(builder.qFeasible_[i].y*50+builder.qFeasible_[i].x);
    std::sort(actual.begin(),actual.end());verify(builder.qComplete_&&actual==reference);
}
int rows_q_rank(const std::vector<int>& feasible,cambc::vec2 origin) {
    verify(!feasible.empty());return *std::min_element(feasible.begin(),feasible.end(),[&](int a,int b){const auto rank=[&](int k){return std::pair{std::max(std::abs(k%50-origin.x),std::abs(k/50-origin.y)),k};};return rank(a)<rank(b);});
}
void rows_q_direct() {
    rows_offspawn();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-retained-offspawn-Q-reference");initialize(*c);
    const int key=bp_side()?2230:192;const auto index=std::size_t(c->group_index(key));const auto stats=c->capital_.exact_stats();verify(stats.has_value());auto& b=c->sortie_builder_;
    for(unsigned n=0;!b.qStarted_&&n<32;++n){c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!result.candidate);if(!b.qStarted_)advance_round();}
    verify(b.qStarted_&&b.qCursor_>0&&b.qCursor_<=8&&!b.qComplete_&&!b.has_complete_work());
    const auto prefix=b.qCursor_;const auto initial=cambc::vec2{actor.x,actor.y};
    // One actual legal SDK move; no actor rewind, target, intent or forecast injection.
    bool moved=false;for(int dy=-1;dy<=1&&!moved;++dy)for(int dx=-1;dx<=1&&!moved;++dx){const cambc::vec2 to{actor.x+dx,actor.y+dy};if((!dx&&!dy)||to.x<0||to.y<0||to.x>=50||to.y>=50||c->personal_.terrain.is_set(to)||occupied_by_living_camp(to.x,to.y))continue;verify(cambc::self().move_absolute(to));moved=true;}
    verify(moved&&cambc::vec2{actor.x,actor.y}!=initial);advance_round();unsigned callbacks=0;
    while(!b.qComplete_&&callbacks++<32){const auto before=b.qCursor_,champ=calls("sdk_champion"),queries=calls("sdk_hypothetical_vision");c->paths_.begin_turn();b.advance_preparation(c->paths_,[]{return true;});verify(b.qCursor_>=before&&b.qCursor_-before<=8&&calls("sdk_hypothetical_vision")-queries<=8&&calls("sdk_champion")==champ&&!b.has_complete_work());if(!b.qComplete_)advance_round();}
    verify(b.qComplete_&&b.qCursor_==49&&b.qCursor_>=prefix);
    const auto reference=rows_q_reference(c->personal_,index,3);rows_q_set(b,reference);
    const auto latest=cambc::vec2{actor.x,actor.y};const auto queries=calls("sdk_hypothetical_vision");c->paths_.begin_turn();const auto bound=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});
    verify(!bound.candidate&&b.route_&&b.qDecisionOrigin_==latest&&b.qBestKey_==rows_q_rank(reference,latest)&&calls("sdk_hypothetical_vision")==queries);
    const int best=b.qBestKey_;const auto feasible=b.qFeasibleCount_;
    // Actual owner-range fault, then a distinct owned public metadata source;
    // both must exclude the old complete set/route from publication.
    actor.attack_range=2;const auto changed=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!changed.candidate&&!b.qComplete_&&!b.route_&&b.range_==2);actor.attack_range=3;
    auto other=std::make_unique<PersonalInformation>(c->personal_);const auto owned=b.prepare(*other,index,*stats,c->paths_,[]{return true;});verify(!owned.candidate&&!b.qComplete_&&!b.route_);
    const auto before=actions;actor.type=10;const auto wrong_class=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!wrong_class.candidate&&wrong_class.status==sr::Status::unsupported&&actions==before);actor.type=11;
    std::printf("SORTIE_Q_REFERENCE {\"side\":%u,\"target\":%d,\"partialCursor\":%u,\"backgroundCallbacks\":%u,\"feasible\":%u,\"firstBindingOrigin\":[%d,%d],\"boundKey\":%d,\"identityFaults\":3,\"scope\":\"direct Builder retained target; no governing choice\",\"nativeFuelClaim\":false}\n",bp_side(),key,prefix,callbacks,feasible,latest.x,latest.y,best);
}
Campaign* q_caller=nullptr;unsigned q_cut_queries=0,q_gate_dumps=0,q_gate_turn=0,q_cut_callbacks=0;int q_gold_key=-1;bool q_cut_enabled=false,q_cut_seen=false,q_this_cut=false,q_gate_busy=false;
bool q_diagnostic(){const auto* value=std::getenv("P3_Q_DIAGNOSTIC");return value&&*value=='1';}
void rows_q_gate() {
    auto& c=*q_caller;if(!q_diagnostic()||q_gate_busy||q_gate_dumps>=3||!c.initialized_||!c.telemetry_.turns||c.context_.ownerTurn!=int(c.telemetry_.turns)||c.active_||q_gate_turn==c.telemetry_.turns)return;
    q_gate_turn=c.telemetry_.turns;++q_gate_dumps;q_gate_busy=true;const auto metering=rows_meter_enabled;rows_meter_enabled=false;
    const auto state=c.information();std::optional<EquipmentStats> exact;const auto context=c.forecast_context(state,&exact);
    std::printf("SORTIE_Q_GATE {\"round\":%u,\"turns\":%u,\"cadence\":%s,\"controller\":%u,\"selector\":%s,\"idle\":true,\"newWinningBank\":%s,\"bag\":%u,\"ownScore\":%u,\"actorType\":%d,\"health\":%g,\"maximumHealth\":%g,\"position\":[%d,%d],\"stateFlags\":%d,\"exactStats\":%s,\"sortieTarget\":%d,\"groups\":[",current_round,c.telemetry_.turns,c.telemetry_.turns%4==2?"true":"false",unsigned(c.controller_),c.selector_?"true":"false",c.context_.newlyFeasibleWinningBank?"true":"false",bag,own_score,actor.type,double(actor.health),double(actor.max_health),actor.x,actor.y,actor.state,exact?"true":"false",c.sortie_target_?c.personal_.groups[*c.sortie_target_].key:-1);
    bool comma=false;for(std::size_t i=0;i<c.personal_.groups.size();++i)if(c.personal_.groups[i].gem_value>0){const auto& d=c.personal_.groups[i];const auto& o=c.personal_.observations[i];const auto clear=context.clear(i,state.health);std::printf("%s{\"key\":%d,\"retry\":%u,\"route\":%d,\"clearAdmitted\":%s,\"clearTurns\":%d,\"loss\":%g,\"publicHealth\":%g,\"observedRemaining\":%g,\"observationRound\":%u}",comma?",":"",d.key,i<c.sortie_retry_after_.size()?c.sortie_retry_after_[i]:0,c.paths_.get(d.field,{actor.x,actor.y}),clear.admitted?"true":"false",clear.turns,clear.healthLoss,d.full_health,o.remaining_health,o.round);comma=true;}
    std::printf("]}\n");rows_meter_enabled=metering;q_gate_busy=false;
}
std::int64_t rows_q_caller_fuel(){rows_q_gate();if(!q_this_cut&&q_cut_enabled&&q_caller->active_&&q_caller->active_->id.kind==JobKind::cycle&&q_caller->active_->id.firstGroup==q_gold_key&&calls("sdk_hypothetical_vision")>q_cut_queries){fuel=64000;q_this_cut=q_cut_seen=true;++q_cut_callbacks;}return fuel;}
void rows_q_caller() {
    rows_offspawn();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-public-metadata-warmed-Q-scan-cold");const int natural=bp_side()?2206:192,gold_key=bp_side()?1551:798;
    initialize(*c);c->candidate_audit_=false;
    // Native summaries bracket these callbacks with turns=round-26. Public
    // SDK fields/catalogue were ready at that historical point. Warm only the
    // real Builder's public wall/member metadata, denying Ready BEFORE any Q
    // slot. No pending Campaign target/job/intent/forecast is supplied.
    auto& builder=c->sortie_builder_;const auto index=std::size_t(c->group_index(natural));const auto exact=c->capital_.exact_stats();verify(exact.has_value());
    const auto warm_actor=actor;const auto warm_items=items;const auto warm_gold=gold,warm_actions=actions,warm_round=current_round;
    unsigned metadata_calls=0;
    while(!builder.qStarted_&&metadata_calls++<16){const auto result=builder.prepare(c->personal_,index,*exact,c->paths_,[&]{return !builder.qStarted_;});verify(!result.candidate);}
    verify(builder.qStarted_&&builder.wallCursor_==builder.groups_&&builder.publicCursor_==builder.publicCount_);
    verify(builder.qCursor_==0&&builder.qFeasibleCount_==0&&!builder.qComplete_&&!builder.route_&&!builder.evaluation_);
    verify(actor.x==warm_actor.x&&actor.y==warm_actor.y&&actor.health==warm_actor.health&&items==warm_items&&gold==warm_gold&&actions==warm_actions&&current_round==warm_round);
    const unsigned prior_turns=bp_side()?541:757;c->telemetry_.turns=prior_turns;
    verify(!c->sortie_target_&&!c->active_&&!c->sortie_candidate_&&!c->sortie_forecast_);
    bool partial=false,complete=false,moved_partial=false,gold_progress=false,ranked=false,direct_binding=false;unsigned partial_round=0,complete_round=0,cut_before=0,cut_after=100000,maximum_callback_slots=0;int best=-1;JobId gold_job;q_gold_key=gold_key;q_cut_seen=false;q_cut_callbacks=0;
    for(unsigned n=0;n<96&&!complete;++n) {
        auto& b=c->sortie_builder_;const auto cursor=b.qCursor_,count=b.qFeasibleCount_;const auto position=cambc::vec2{actor.x,actor.y};const auto before_actions=actions;
        fuel=100000;rows_meter_enabled=true;q_caller=c.get();q_cut_queries=calls("sdk_hypothetical_vision");q_cut_enabled=partial&&!moved_partial&&!b.qComplete_;q_this_cut=false;optional_fuel_probe=rows_q_caller_fuel;if(q_cut_enabled)cut_before=unsigned(fuel);
        c->turn();optional_fuel_probe=nullptr;q_cut_enabled=false;q_caller=nullptr;rows_meter_enabled=false;if(q_this_cut)cut_after=std::min(cut_after,unsigned(fuel));
        if(q_diagnostic()&&q_gate_dumps>=3){std::printf("SORTIE_Q_GATE_RESULT {\"status\":\"observed\",\"side\":%u,\"records\":%u}\n",bp_side(),q_gate_dumps);return;}
        if(b.groupKey_==natural&&b.qCursor_>=cursor){maximum_callback_slots=std::max(maximum_callback_slots,b.qCursor_-cursor);verify(b.qCursor_-cursor<=16);} // Two bounded calls can occur in one callback.
        if(c->active_&&c->active_->id.kind==JobKind::cycle&&c->active_->id.firstGroup==gold_key){gold_progress|=actions>before_actions;gold_job=c->active_->id;}
        if(b.qStarted_&&!b.qComplete_){verify(!c->sortie_candidate_);if(!partial){partial=true;partial_round=current_round;}else if(b.groupKey_==natural){verify(b.qCursor_>=cursor&&b.qFeasibleCount_>=count);moved_partial|=cambc::vec2{actor.x,actor.y}!=position;}}
        if(b.qComplete_&&b.groupKey_==natural){if(!complete){complete=true;complete_round=current_round;}if(b.route_){const auto reference=rows_q_reference(c->personal_,std::size_t(c->group_index(natural)),actor.attack_range);rows_q_set(b,reference);verify(b.qBestKey_==rows_q_rank(reference,b.qDecisionOrigin_));best=b.qBestKey_;ranked=true;}}
        if(!complete)advance_round();
    }
    if(complete&&!ranked){
        // This query-only direct binding is NOT a governing Campaign decision.
        // Existing idle cadence can delay binding while ordinary Gold runs.
        const auto latest=cambc::vec2{actor.x,actor.y};const auto stats=c->capital_.exact_stats();verify(stats.has_value());const auto before=actions;
        const auto bound=builder.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});
        const auto reference=rows_q_reference(c->personal_,index,actor.attack_range);rows_q_set(builder,reference);
        verify(!bound.candidate&&builder.route_&&builder.qDecisionOrigin_==latest&&builder.qBestKey_==rows_q_rank(reference,latest)&&actions==before);
        best=builder.qBestKey_;ranked=direct_binding=true;
    }
    if(!partial||!complete||!ranked||!gold_progress||!moved_partial||!q_cut_seen)std::fprintf(stderr,"Q caller side%u p%u c%u r%u gold%u moved%u cut%u round%u key%d cursor%u active%d\n",bp_side(),partial,complete,ranked,gold_progress,moved_partial,q_cut_seen,current_round,c->sortie_builder_.groupKey_,c->sortie_builder_.qCursor_,c->active_?c->active_->id.firstGroup:-1);
    verify(partial&&complete&&ranked&&gold_progress&&moved_partial&&q_cut_seen&&cut_after>=45000);
    std::printf("SORTIE_Q_CALLER {\"side\":%u,\"naturalTarget\":%d,\"partialRound\":%u,\"completeRound\":%u,\"boundKey\":%d,\"goldJob\":[%d,%d,%d,%d,%d],\"legalMoveWhilePartial\":true,\"ordinaryGoldProgress\":true,\"maximumCallbackSlots\":%u,\"stagedBudgetBefore\":%u,\"stagedBudgetAfter\":%u,\"stagedCallbacks\":%u,\"metadataCalls\":%u,\"qScanInitiallyEmpty\":true,\"directFirstBinding\":%s,\"initialPriorTurns\":%u,\"coldBootClaim\":false,\"scope\":\"PUBLIC-METADATA-WARMED Q-SCAN-COLD historical-phase actual Campaign; optional direct query-only binding\",\"nativeFuelClaim\":false}\n",bp_side(),natural,partial_round,complete_round,best,int(gold_job.kind),gold_job.firstGroup,gold_job.secondGroup,gold_job.altarTile,gold_job.equipmentBlock,maximum_callback_slots,cut_before,cut_after,q_cut_callbacks,metadata_calls,direct_binding?"true":"false",prior_turns);
    q_cut_seen=false;
}
struct ExposureBindingWitness {bool observed=false;int key=-1;unsigned round=0,exposure=0;int turns=0;cambc::vec2 origin{};};
Campaign* exposure_caller=nullptr;ExposureBindingWitness exposure_binding;
bool exposure_hold_until_move=false,exposure_cut_this_callback=false;unsigned exposure_cut_callbacks=0;
std::int64_t rows_exposure_fuel() {
    if(exposure_hold_until_move&&!exposure_cut_this_callback&&exposure_caller&&exposure_caller->active_&&
       exposure_caller->sortie_builder_.exposure_&&exposure_caller->sortie_builder_.exposure_->pending()) {
        const auto index=exposure_caller->group_index(exposure_caller->active_->id.firstGroup);
        if(index>=0&&exposure_caller->personal_.groups[std::size_t(index)].gem_value==0) {
            fuel=std::min<std::int64_t>(fuel,64000);exposure_cut_this_callback=true;++exposure_cut_callbacks;
        }
    }
    if(exposure_caller&&!exposure_binding.observed&&exposure_caller->sortie_builder_.outward_) {
        const auto& b=exposure_caller->sortie_builder_;const auto& outward=*b.outward_;
        verify(outward.origin==cambc::vec2{actor.x,actor.y});
        exposure_binding={true,b.groupKey_,current_round,outward.exposure,outward.turns,outward.origin};
    }
    return fuel;
}
void rows_q_cold_caller(bool follow_exposure=false) {
    rows_offspawn();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-cold-controller-opportunity-cadence");
    verify(!c->initialized_&&c->initialization_stage_==0&&!c->personal_.initialization_complete);
    verify(c->telemetry_.turns==0&&c->sortie_idle_opportunities_==0&&!c->sortie_target_&&!c->active_);
    verify(!c->sortie_builder_.walls_&&!c->sortie_builder_.publicStarted_&&!c->sortie_builder_.qStarted_&&c->sortie_builder_.qCursor_==0);
    verify(!c->sortie_builder_.route_&&!c->sortie_builder_.evaluation_&&!c->sortie_candidate_&&!c->sortie_forecast_);
    const auto initial_round=current_round;const auto initial_position=cambc::vec2{actor.x,actor.y};
    unsigned target_round=0,target_opportunity=0,wall_round=0,wall_opportunity=0,public_round=0,q_round=0,q_opportunity=0,complete_round=0,first_gold_round=0;
    unsigned gold_actions=0,gold_moves=0,active_background_slots=0,maximum_callback_slots=0,minimum_callback_fuel=100000;
    unsigned exposure_round=0,exposure_gold_actions=0,exposure_gold_moves=0,exposure_background_nodes=0;int exposure_key=-1;
    int first_target=-1,q_key=-1;JobId first_gold;bool complete=false;
    exposure_binding={};
    for(unsigned n=0;n<900&&current_round<cambc::constants::max_rounds;++n) {
        auto& b=c->sortie_builder_;const auto before_actions=actions,before_moves=moves,before_cursor=b.qCursor_;
        const auto before_key=b.groupKey_;const auto opportunity=c->sortie_idle_opportunities_;
        const bool ordinary_before=c->active_&&(c->active_->id.kind==JobKind::cycle||c->active_->id.kind==JobKind::double_cycle);
        const auto gold_job=[&]{if(!c->active_)return false;const auto i=c->group_index(c->active_->id.firstGroup);return i>=0&&c->personal_.groups[std::size_t(i)].gem_value==0;};
        const bool gold_before=ordinary_before&&gold_job();
        const bool exposure_pending=bool(b.exposure_)&&b.exposure_->pending();const auto cached=b.exposure_?b.exposure_->cached_nodes():0;
        fuel=100000;rows_meter_enabled=true;if(follow_exposure){exposure_caller=c.get();optional_fuel_probe=rows_exposure_fuel;}c->turn();optional_fuel_probe=nullptr;exposure_caller=nullptr;rows_meter_enabled=false;
        minimum_callback_fuel=std::min(minimum_callback_fuel,unsigned(std::max<std::int64_t>(fuel,0)));
        verify(c->sortie_idle_opportunities_>=opportunity&&c->sortie_idle_opportunities_-opportunity<=1);
        if(c->sortie_target_&&!target_round){target_round=current_round;first_target=c->personal_.groups[*c->sortie_target_].key;target_opportunity=c->sortie_idle_opportunities_;}
        if(b.wallCursor_&&!wall_round){wall_round=current_round;wall_opportunity=c->sortie_idle_opportunities_;}
        if(b.publicStarted_&&!public_round)public_round=current_round;
        if(b.qStarted_&&!q_round){q_round=current_round;q_opportunity=c->sortie_idle_opportunities_;q_key=b.groupKey_;}
        if(b.groupKey_==before_key&&b.qCursor_>=before_cursor){const auto slots=b.qCursor_-before_cursor;verify(slots<=16);maximum_callback_slots=std::max(maximum_callback_slots,slots);if(ordinary_before)active_background_slots+=slots;}
        if(b.qStarted_&&!b.qComplete_)verify(!c->sortie_candidate_&&!c->sortie_forecast_&&!b.evaluation_);
        if(b.exposure_&&b.exposure_->pending()) {
            if(!exposure_round){exposure_round=current_round;exposure_key=b.groupKey_;}
            if(!b.outward_)verify(!c->sortie_candidate_&&!c->sortie_forecast_);
        }
        if(c->sortie_candidate_){const auto& candidate=*c->sortie_candidate_;verify(b.exposure_&&b.exposure_->cost(candidate.plan.decisionOrigin)==unsigned(candidate.input.outward.exposureTurns));}
        const bool ordinary_after=c->active_&&(c->active_->id.kind==JobKind::cycle||c->active_->id.kind==JobKind::double_cycle);
        const bool gold_after=ordinary_after&&gold_job();
        if(gold_after&&!first_gold_round){first_gold_round=current_round;first_gold=c->active_->id;}
        if(gold_before||gold_after){gold_actions+=actions-before_actions;gold_moves+=moves-before_moves;}
        if(exposure_pending&&(gold_before||gold_after)){exposure_gold_actions+=actions-before_actions;exposure_gold_moves+=moves-before_moves;}
        if(exposure_pending&&b.exposure_&&b.groupKey_==before_key&&b.exposure_->cached_nodes()>=cached)exposure_background_nodes+=unsigned(b.exposure_->cached_nodes()-cached);
        if(b.qComplete_&&!complete){complete=true;complete_round=current_round;q_key=b.groupKey_;}
        if((!follow_exposure&&complete)||(follow_exposure&&exposure_binding.observed))break;
        advance_round();
    }
    const auto& b=c->sortie_builder_;
    std::printf("SORTIE_COLD_CALLER {\"side\":%u,\"initialRound\":%u,\"initialPosition\":[%d,%d],\"initialPriorTurns\":0,\"metadataWarmCalls\":0,\"initialPrivateWork\":false,\"controllerInitialized\":%s,\"turns\":%u,\"eligibleOpportunities\":%u,\"firstTarget\":%d,\"firstTargetRound\":%u,\"firstTargetOpportunity\":%u,\"firstWallRound\":%u,\"firstWallOpportunity\":%u,\"publicMemberRound\":%u,\"qStartRound\":%u,\"qStartOpportunity\":%u,\"qTarget\":%d,\"qCursor\":%u,\"qComplete\":%s,\"qCompleteRound\":%u,\"firstGoldRound\":%u,\"goldJob\":[%d,%d,%d,%d,%d],\"ordinaryGoldActions\":%u,\"ordinaryGoldMoves\":%u,\"activeBackgroundSlots\":%u,\"maximumCallbackSlots\":%u,\"minimumCallbackFuel\":%u,\"finalHealth\":%g,\"bag\":%u,\"scope\":\"cold controller at declared offspawn world; no full birth or replay history\",\"nativeFuelClaim\":false}\n",bp_side(),initial_round,initial_position.x,initial_position.y,c->initialized_?"true":"false",c->telemetry_.turns,c->sortie_idle_opportunities_,first_target,target_round,target_opportunity,wall_round,wall_opportunity,public_round,q_round,q_opportunity,q_key,b.qCursor_,complete?"true":"false",complete_round,first_gold_round,int(first_gold.kind),first_gold.firstGroup,first_gold.secondGroup,first_gold.altarTile,first_gold.equipmentBlock,gold_actions,gold_moves,active_background_slots,maximum_callback_slots,minimum_callback_fuel,double(actor.health),bag);
    verify(c->initialized_&&target_round&&target_opportunity==2&&public_round&&q_round&&complete&&b.qCursor_==49);
    verify(first_gold_round&&gold_actions&&gold_moves&&active_background_slots&&maximum_callback_slots<=16&&actor.health>0);
    if(follow_exposure) {
        auto plan=std::make_unique<sr::Plan>();plan->origin=b.origin_;plan->route=b.route_;plan->memberCount=b.publicCount_;plan->members=b.publicMembers_;
        const auto reference=sr::path_exposure(*plan,exposure_binding.origin,[]{return true;});
        std::printf("SORTIE_EXPOSURE_CALLER {\"side\":%u,\"requestRound\":%u,\"target\":%d,\"ordinaryGoldActionsWhilePending\":%u,\"ordinaryGoldMovesWhilePending\":%u,\"backgroundCachedNodes\":%u,\"bound\":%s,\"boundRound\":%u,\"boundOrigin\":[%d,%d],\"turns\":%d,\"exposure\":%u,\"reference\":%d,\"actorSnapshotCheckedAtBinding\":true,\"nativeFuelClaim\":false}\n",bp_side(),exposure_round,exposure_key,exposure_gold_actions,exposure_gold_moves,exposure_background_nodes,exposure_binding.observed?"true":"false",exposure_binding.round,exposure_binding.origin.x,exposure_binding.origin.y,exposure_binding.turns,exposure_binding.exposure,reference?int(*reference):-1);
        verify(exposure_round&&exposure_binding.observed&&exposure_gold_actions&&exposure_gold_moves&&exposure_background_nodes);
        verify(reference&&*reference==exposure_binding.exposure&&plan->route->distance(exposure_binding.origin)==exposure_binding.turns);
    }
}
unsigned rows_exposure_check(const sr::Plan& plan,cambc::vec2 from,bool all_cuts) {
    const auto scalar=sr::path_exposure(plan,from,[]{return true;});verify(scalar.has_value());
    sr::PathExposure cache(plan,50,50);verify(cache.request(from));unsigned ready_calls=0,chunks=0;
    while(!cache.cost(from)&&chunks++<64){const auto nodes=cache.cached_nodes();cache.advance([&]{++ready_calls;return true;});verify(cache.cached_nodes()>=nodes&&cache.cached_nodes()-nodes<=8);}
    verify(cache.cost(from)==scalar&&!cache.failed());unsigned cuts=0;
    if(all_cuts)for(unsigned cut=0;cut<=ready_calls;++cut) {
        sr::PathExposure interrupted(plan,50,50);verify(interrupted.request(from));unsigned calls=0;
        interrupted.advance([&]{return calls++<cut;});
        if(const auto known=interrupted.cost(from))verify(known==scalar);
        unsigned resumes=0;while(!interrupted.cost(from)&&resumes++<64){const auto nodes=interrupted.cached_nodes();interrupted.advance([]{return true;});verify(interrupted.cached_nodes()-nodes<=8);}
        verify(interrupted.cost(from)==scalar&&!interrupted.failed());++cuts;
    }
    // Every cached result belongs to its exact original point; the root/Q
    // contributes zero even when it is itself exposed to camp attacks.
    auto point=from;for(unsigned n=0;point!=plan.origin&&n<128;++n){verify(cache.cost(point)==sr::path_exposure(plan,point,[]{return true;}));const auto next=plan.route->toward_origin(point);verify(next.has_value());point=*next;}
    verify(cache.cost(plan.origin)==0);
    sr::PathExposure moved(plan,50,50);verify(moved.request(from));moved.advance([]{return true;});
    const auto nodes=moved.cached_nodes();const auto next=plan.route->toward_origin(from);verify(next.has_value()&&moved.request(*next));
    verify(!moved.request({-1,0})&&!moved.cost({-1,0})&&moved.cached_nodes()==nodes);
    unsigned resumes=0;while(!moved.cost(*next)&&resumes++<64)moved.advance([]{return true;});
    verify(moved.cost(*next)==sr::path_exposure(plan,*next,[]{return true;})&&!moved.failed());
    return cuts;
}
void rows_exposure_reference() {
    rows_offspawn();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-exposure-reference");initialize(*c);
    const int key=bp_side()?2230:192;const auto index=std::size_t(c->group_index(key));const auto stats=c->capital_.exact_stats();verify(stats.has_value());auto& b=c->sortie_builder_;
    for(unsigned n=0;n<128&&(!b.route_||!b.route_->ready());++n){c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!result.candidate);if(!b.route_||!b.route_->ready())advance_round();}
    verify(b.route_&&b.route_->ready());auto plan=std::make_unique<sr::Plan>();plan->origin=b.origin_;plan->route=b.route_;plan->memberCount=b.publicCount_;plan->members=b.publicMembers_;
    const cambc::vec2 from{actor.x,actor.y};const auto cuts=rows_exposure_check(*plan,from,true);
    unsigned origins=1;for(const auto spawn:c->personal_.spawn_positions){verify(sr::path_exposure(*plan,spawn,[]{return true;}).has_value());rows_exposure_check(*plan,spawn,false);++origins;}
    // Independent synthetic boundary topology, NOT a production Rows route.
    cambc::bitboard walls;std::fill_n(walls.ptr(),56,UINT64_MAX);
    for(int y:{1,3,5,7})for(int x=1;x<=47;++x)walls.set({x,y},false);
    for(const auto p:{cambc::vec2{47,2},cambc::vec2{1,4},cambc::vec2{47,6}})walls.set(p,false);
    auto corridor=std::make_shared<tt::RouteField>(walls,cambc::vec2{1,1});
    for(unsigned n=0;!corridor->ready()&&n<32;++n){c->paths_.begin_turn();corridor->advance(c->paths_,[]{return true;});}
    verify(corridor->ready());cambc::vec2 at128{-1,-1},at129{-1,-1};
    for(int y=0;y<50;++y)for(int x=0;x<50;++x){if(corridor->distance({x,y})==128)at128={x,y};if(corridor->distance({x,y})==129)at129={x,y};}
    verify(at128.x>=0&&at129.x>=0);auto edge=std::make_unique<sr::Plan>();edge->origin={1,1};edge->route=corridor;edge->memberCount=1;edge->members[0].incoming=1;edge->members[0].spawn=edge->origin;edge->members[0].range=16;
    verify(sr::exposed(*edge,edge->origin));const auto edge_cuts=rows_exposure_check(*edge,at128,true);
    verify(!sr::path_exposure(*edge,at129,[]{return true;}));sr::PathExposure too_far(*edge,50,50);verify(!too_far.request(at129)&&!too_far.cost(at129)&&!too_far.request({0,0}));
    b.reset_preparation();verify(!b.exposure_&&!b.route_&&!b.evaluation_);
    std::printf("SORTIE_EXPOSURE_REFERENCE {\"side\":%u,\"target\":%d,\"actualRowsOrigins\":%u,\"actualReadyCuts\":%u,\"boundaryReadyCuts\":%u,\"maximumLeg\":128,\"rejectedLeg\":129,\"perCallUnits\":8,\"originalPointCostsChecked\":true,\"rootExcluded\":true,\"resetClearsBinding\":true,\"boundaryTopology\":\"declared synthetic corridor, separate from Rows\",\"nativeFuelClaim\":false}\n",bp_side(),key,origins,cuts,edge_cuts);
}
void rows_exposure_prepared_caller() {
    rows_offspawn();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-prepared-static-route-exposure-cold");initialize(*c);c->candidate_audit_=false;
    const int key=bp_side()?2206:192;const auto index=std::size_t(c->group_index(key));auto& b=c->sortie_builder_;
    auto stats=c->capital_.exact_stats();verify(stats.has_value());
    // Prepare ONLY the immutable Q/route. Once a field exists, background
    // completion cannot request outward work; that belongs to the real caller.
    for(unsigned n=0;!b.route_&&n<64;++n){c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!result.candidate);if(!b.route_)advance_round();}
    verify(b.route_&&!b.route_->ready()&&!b.exposure_&&!b.outward_&&!b.evaluation_);
    for(unsigned n=0;!b.route_->ready()&&n<64;++n){advance_round();c->paths_.begin_turn();b.advance_preparation(c->paths_,[]{return true;});}
    verify(b.route_->ready()&&!b.exposure_&&!b.outward_&&!b.evaluation_&&!c->sortie_target_&&!c->active_&&!c->sortie_candidate_&&!c->sortie_forecast_);
    verify(!actions&&!moves&&!c->telemetry_.turns&&!c->sortie_idle_opportunities_);
    c->personal_.observe(cambc::vision::camps(),{});stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto context=c->forecast_context(c->information());fuel=100000;rows_meter_enabled=true;
    c->prepare_sortie(context,*stats,true);rows_meter_enabled=false;
    verify(c->sortie_target_&&c->personal_.groups[*c->sortie_target_].key==key&&b.exposure_&&b.exposure_->pending());
    verify(!b.outward_&&!b.evaluation_&&!c->sortie_candidate_&&!c->sortie_forecast_&&!c->active_&&!actions);
    const auto request_round=current_round;unsigned gold_actions=0,gold_moves=0,background_nodes=0,min_cut_fuel=100000;JobId original_gold;bool gold_seen=false,moved=false;
    exposure_binding={};exposure_cut_callbacks=0;
    for(unsigned n=0;!exposure_binding.observed&&n<160;++n) {
        const auto before_actions=actions,before_moves=moves;const auto before_nodes=b.exposure_->cached_nodes();const bool pending=b.exposure_->pending();
        bool gold_before=false;if(c->active_){const auto at=c->group_index(c->active_->id.firstGroup);gold_before=at>=0&&c->personal_.groups[std::size_t(at)].gem_value==0;}
        fuel=100000;rows_meter_enabled=true;exposure_caller=c.get();exposure_hold_until_move=!moved;exposure_cut_this_callback=false;optional_fuel_probe=rows_exposure_fuel;
        c->turn();optional_fuel_probe=nullptr;exposure_caller=nullptr;exposure_hold_until_move=false;rows_meter_enabled=false;
        if(exposure_cut_this_callback)min_cut_fuel=std::min(min_cut_fuel,unsigned(fuel));
        bool gold_after=false;if(c->active_){const auto at=c->group_index(c->active_->id.firstGroup);gold_after=at>=0&&c->personal_.groups[std::size_t(at)].gem_value==0;if(gold_after&&!gold_seen){gold_seen=true;original_gold=c->active_->id;}}
        if(pending&&(gold_before||gold_after)){gold_actions+=actions-before_actions;gold_moves+=moves-before_moves;moved|=moves>before_moves;}
        if(pending&&b.exposure_&&b.groupKey_==key&&b.exposure_->cached_nodes()>=before_nodes)background_nodes+=unsigned(b.exposure_->cached_nodes()-before_nodes);
        if(b.exposure_&&b.exposure_->pending()&&!b.outward_)verify(!c->sortie_candidate_&&!c->sortie_forecast_);
        if(!exposure_binding.observed)advance_round();
    }
    auto plan=std::make_unique<sr::Plan>();plan->origin=b.origin_;plan->route=b.route_;plan->memberCount=b.publicCount_;plan->members=b.publicMembers_;
    const auto reference=sr::path_exposure(*plan,exposure_binding.origin,[]{return true;});
    std::printf("SORTIE_EXPOSURE_CALLER {\"side\":%u,\"scope\":\"PREPARED-STATIC-ROUTE EXPOSURE-COLD actual Campaign request/turns; not cold startup\",\"requestRound\":%u,\"target\":%d,\"goldJob\":[%d,%d,%d,%d,%d],\"ordinaryGoldActionsWhilePending\":%u,\"ordinaryGoldMovesWhilePending\":%u,\"backgroundCachedNodes\":%u,\"bound\":%s,\"boundRound\":%u,\"boundOrigin\":[%d,%d],\"turns\":%d,\"exposure\":%u,\"reference\":%d,\"actorSnapshotCheckedAtBinding\":true,\"stagedAvailabilityCallbacks\":%u,\"stagedFuelMinimum\":%u,\"initialDecisions\":0,\"initialOutwardWork\":false,\"nativeFuelClaim\":false}\n",bp_side(),request_round,key,int(original_gold.kind),original_gold.firstGroup,original_gold.secondGroup,original_gold.altarTile,original_gold.equipmentBlock,gold_actions,gold_moves,background_nodes,exposure_binding.observed?"true":"false",exposure_binding.round,exposure_binding.origin.x,exposure_binding.origin.y,exposure_binding.turns,exposure_binding.exposure,reference?int(*reference):-1,exposure_cut_callbacks,min_cut_fuel);
    verify(gold_seen&&gold_actions&&gold_moves&&background_nodes&&exposure_binding.observed&&exposure_binding.key==key);
    verify(reference&&*reference==exposure_binding.exposure&&plan->route->distance(exposure_binding.origin)==exposure_binding.turns&&actor.health>0);
    verify(exposure_cut_callbacks&&min_cut_fuel>=45000&&min_cut_fuel<65000);
}
void rows_metrics_transitions() {
    rows_reset();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-metrics-transitions");initialize(*c);
    auto& b=c->sortie_builder_;const auto index=std::size_t(c->group_index(bp_side()?2230:168));auto stats=c->capital_.exact_stats();verify(stats.has_value());
    for(unsigned n=0;!b.evaluation_&&n<256;++n){c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!result.candidate);if(!b.evaluation_)advance_round();}
    verify(b.evaluation_&&!b.has_complete_work());const auto initial=b.metrics().snapshot();const auto lost=b.evaluation_->owner_turns();
    verify(initial[3]==1&&initial[4]==0&&initial[5]==0&&initial[6]==0&&initial[10]==-1&&initial[11]==-1&&lost==4);
    bool moved=false;for(int dy=-1;dy<=1&&!moved;++dy)for(int dx=-1;dx<=1&&!moved;++dx){const cambc::vec2 to{actor.x+dx,actor.y+dy};if((!dx&&!dy)||to.x<0||to.y<0||to.x>=50||to.y>=50||c->personal_.terrain.is_set(to)||occupied_by_living_camp(to.x,to.y))continue;verify(cambc::self().move_absolute(to));moved=true;}
    verify(moved);advance_round();stats=c->capital_.exact_stats();verify(stats.has_value());
    for(unsigned n=0;b.metrics().snapshot()[3]<2&&n<64;++n){c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});verify(!result.candidate);if(b.metrics().snapshot()[3]<2)advance_round();}
    const auto replaced=b.metrics().snapshot();
    verify(replaced[3]==2&&replaced[6]==1&&replaced[7]==std::int64_t(lost)&&replaced[9]==int(sr::ResetReason::origin));
    verify((std::uint64_t(replaced[8])&(1u<<unsigned(sr::ResetReason::origin)))!=0);
    for(unsigned n=0;!b.has_complete_work()&&n<128;++n){advance_round();c->paths_.begin_turn();const auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;});if(result.candidate)verify(b.has_complete_work());}
    verify(b.has_complete_work());const auto completed=b.metrics().snapshot();
    verify(completed[3]==2&&completed[4]==1&&completed[5]==1&&completed[6]==1&&completed[10]>=0&&completed[11]>=0);
    for(unsigned i=0;i<2;++i){const auto clone=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;},false);verify(clone.candidate&&clone.candidate->plan.decisionOrigin==cambc::vec2{actor.x,actor.y});}
    const auto cloned=b.metrics().snapshot();verify(cloned[3]==completed[3]&&cloned[4]==completed[4]&&cloned[5]==completed[5]&&cloned[6]==completed[6]&&cloned[13]==completed[13]+2);
    --actor.health;const auto before=actions;const auto dirty=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;},false);const auto invalid=b.metrics().snapshot();
    verify(!dirty.candidate&&!b.has_complete_work()&&actions==before&&invalid[6]==2&&invalid[9]==int(sr::ResetReason::semantic));
    const auto queries=query_trace.size();const auto cheap=b.prepare(c->personal_,index,*stats,c->paths_,[]{return true;},false);verify(!cheap.candidate&&query_trace.size()==queries&&b.metrics().snapshot()[6]==invalid[6]);
    std::printf("SORTIE_METRICS_TRANSITIONS {\"side\":%u,\"initialOwnerTurns\":%u,\"originReset\":%lld,\"originLostTurns\":%lld,\"starts\":%lld,\"terminals\":%lld,\"schedules\":%lld,\"clonedPublications\":2,\"finalResets\":%lld,\"completedStaleReset\":%lld,\"queryFreeAfterReset\":true,\"freshOriginChecked\":true,\"nativeFuelClaim\":false}\n",bp_side(),lost,static_cast<long long>(replaced[9]),static_cast<long long>(replaced[7]),static_cast<long long>(completed[3]),static_cast<long long>(completed[4]),static_cast<long long>(completed[5]),static_cast<long long>(invalid[6]),static_cast<long long>(invalid[9]));
}
void rows_metrics_abort() {
    auto c=rows_ready();
    for(unsigned n=0;!c->sortie_episode_&&n<64;++n){fuel=100000;c->turn();if(!c->sortie_episode_)advance_round();}
    verify(c->sortie_episode_&&c->active_);const auto committed=c->sortie_builder_.metrics().snapshot();verify(committed[15]==1&&committed[16]==0&&committed[17]==0);
    // An explicit SDK-host death fault, not a fabricated conversion/endpoint.
    advance_round();actor.state&=~1u;fuel=100000;c->turn();const auto aborted=c->sortie_builder_.metrics().snapshot();
    verify(!c->sortie_episode_&&!c->active_&&aborted[15]==1&&aborted[16]==0&&aborted[17]==1);
    advance_round();fuel=100000;c->turn();verify(c->sortie_builder_.metrics().snapshot()[17]==1);
    std::printf("SORTIE_METRICS_ABORT {\"side\":%u,\"episodes\":1,\"conversions\":0,\"aborts\":1,\"fault\":\"explicit SDK-host death after actual commit\",\"nativeFuelClaim\":false}\n",bp_side());
}
sr::Builder* static_kit_builder=nullptr;unsigned static_kit_queries=0;bool static_kit_cut=false,static_kit_drift=false;
void rows_static_kit_query(unsigned hash) {
    if(hash!=trace_hash("sdk_champion_items")&&hash!=trace_hash("sdk_champion_ability_props")&&hash!=trace_hash("sdk_champion_cooldown"))return;
    verify(static_kit_builder&&static_kit_builder->geometry_); // Exact query-time phase, not a post-call inference.
    ++static_kit_queries;
    if(static_kit_cut){fuel=44000;static_kit_cut=false;}
    if(static_kit_drift){++current_round;static_kit_drift=false;}
}
void rows_static_live_queries() {
    rows_reset();auto c=std::make_unique<Campaign>(Controller::ledger,false,"Rows-cold-Builder-static-kit-defer");initialize(*c);
    auto& b=c->sortie_builder_;const auto index=std::size_t(c->group_index(bp_side()?2230:168));auto stats=c->capital_.exact_stats();verify(stats.has_value());
    verify(!b.walls_&&!b.publicStarted_&&!b.qStarted_&&!b.route_&&!b.geometry_&&!b.evaluation_);
    static_kit_queries=0;sortie_bad_kit=true;unsigned static_calls=0,late_calls=0;bool rejected=false;
    const auto invoke=[&] {
        fuel=100000;c->paths_.begin_turn();static_kit_builder=&b;sortie_query_probe=rows_static_kit_query;
        auto result=b.prepare(c->personal_,index,*stats,c->paths_,[]{return fuel>=45000;});
        sortie_query_probe=nullptr;static_kit_builder=nullptr;return result;
    };
    for(unsigned n=0;n<256&&!rejected;++n) {
        const auto queries=static_kit_queries;const auto result=invoke();verify(!result.candidate&&!b.evaluation_);
        if(static_kit_queries==queries)++static_calls;else ++late_calls;
        if(!b.geometry_)verify(static_kit_queries==queries);
        if(b.geometry_&&result.status==sr::Status::unsupported)rejected=true;else advance_round();
    }
    verify(rejected&&static_calls>1&&late_calls==1&&b.geometry_&&b.qComplete_&&b.route_->ready()&&b.outward_&&!b.evaluation_);
    verify(b.metrics().snapshot()[3]==0&&static_kit_queries>0);
    sortie_bad_kit=false;static_kit_cut=true;const auto low=invoke();
    verify(!low.candidate&&!b.evaluation_&&fuel<45000&&!static_kit_cut);
    static_kit_drift=true;const auto stale=invoke();
    verify(!stale.candidate&&!b.evaluation_&&!static_kit_drift);
    // Explicit SDK-host live-fact changes AFTER static geometry completed.
    // They are not purchases, actions, or a native-reachable gear claim.
    ++items[unsigned(cambc::item_type::monster_shard_of_strength)];sync_stats();actor.health=899;cooldown[0]=1;cooldown[1]=3;
    actor.state&=~((1<<3)|(1<<4)|(1<<11));stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto fresh=invoke();verify(!fresh.candidate&&b.evaluation_&&b.metrics().snapshot()[3]==1);
    const auto& input=b.evaluationInput_;
    verify(input.health==899&&input.strength==50&&input.tavi.resetCooldown==1&&input.tavi.burstCooldown==3);
    verify(!input.tavi.initialCanAttack&&!input.tavi.initialCanCast&&!input.tavi.initialBurstAllowed);
    verify(b.evaluationInventory_[unsigned(cambc::item_type::monster_shard_of_strength)]==1);
    verify(!actions&&!moves);
    std::printf("SORTIE_STATIC_KIT {\"side\":%u,\"staticCalls\":%u,\"staticKitQueries\":0,\"lateKitQueries\":%u,\"queryTimeGeometryGuard\":true,\"unsupportedKitRejected\":true,\"lateFuelDenied\":true,\"lateRoundRejected\":true,\"freshInput\":[899,50,1,3,0,0,0,1],\"modelStarts\":1,\"scope\":\"cold Builder over initialized public SDK context; explicit live-fact faults\",\"nativeFuelClaim\":false}\n",bp_side(),static_calls,static_kit_queries);
}
#endif
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
#if defined(P3_SORTIE_ROWS)
    if(const auto* live=std::getenv("P3_STATIC_KIT_ONLY");live&&*live=='1'){rows_static_live_queries();rows_q_cold_caller();rows_campaign(Controller::ledger,true,true);return 0;}
    if(const auto* metrics=std::getenv("P3_METRICS_ONLY");metrics&&*metrics=='1'){rows_metrics_transitions();rows_campaign(Controller::ledger,true,true);rows_metrics_abort();return 0;}
    if(const auto* exposure=std::getenv("P3_EXPOSURE_ONLY");exposure&&*exposure=='1'){rows_exposure_reference();rows_exposure_prepared_caller();return 0;}
    if(const auto* cold=std::getenv("P3_Q_COLD_ONLY");cold&&*cold=='1'){rows_q_cold_caller();return 0;}
    if(q_diagnostic()){rows_q_caller();return 0;}
    rows_q_direct();rows_q_caller();
    rows_fast_publication();rows_pending_fairness();rows_campaign(Controller::ledger,true);rows_campaign(Controller::ledger,false);rows_campaign(Controller::atlas,false);
    std::printf("SORTIE_ROWS_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"paths\":%u,\"fastPublications\":%u,\"mutations\":%u,\"ordinaryAlternatives\":%u,\"pendingWindows\":%u,\"nativeGames\":0}\n",bp_side(),checks,rows_paths,rows_fast,rows_mutations,rows_ordinary,rows_pending);
#else
    ability_complete();ability_fault_cases();cursor_builder_cases();cached_phase_fault_cases();ability_cold_campaign(Controller::ledger);ability_cold_campaign(Controller::atlas);
    std::printf("SORTIE_ABILITY_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"directMechanicEndpoints66\":%u,\"coldChoices\":%u,\"coldPhysicalConversions66\":%u,\"faults\":%u,\"hiddenPrepares\":%u,\"plannedBursts\":%u,\"plannedResets\":%u,\"cursorCases\":%u,\"pendingCallbacks\":%u,\"resumedChunks\":%u,\"inputMutations\":%u,\"phaseFaults\":%u,\"coldWindowCuts\":%u,\"nativeGames\":0}\n",bp_side(),checks,ability_paths,ability_cold_choices,ability_cold_conversions,ability_faults,ability_hidden,ability_bursts,ability_resets,cursor_cases,cursor_pending,cursor_resumes,cursor_mutations,phase_faults,cold_window_cuts);
#endif
}
