#pragma once
#include "product.h"
#include "../../common/model-runtime.h"
#include "../../common/production-evidence.h"

namespace plan4::runtime {
class Covenant : public M0 {
    covenant::Covenant monitor_;
    covenant::FixedProduct product_;
    bool enabled_=true, selected_=false, execution_bound_=false, phase_supported_=false, source_current_=false, batch_budget_=false;
    Job selected_job_{}, issued_repair_{};
    bool issued_repair_valid_=false;
    std::uint64_t job_epoch_=0, revision_=0;
    const Information* snapshot_=nullptr;
    ObservationClock snapshot_clock_{};
    struct BoundReceipt {
        PhaseReceipt source{};
        std::uint64_t contract_epoch=0, contract_sequence=0;
        bool repair=false;
        int following_clear_nominal=-1;
    };
    std::array<BoundReceipt,4> receipts_{};
    PhaseStart phase_start_{};
    std::uint64_t phase_contract_epoch_=0, phase_contract_sequence_=0, latest_sequence_=0;
    bool phase_repair_=false;
    bool phase_pending_=false;
    int receipt_count_=0, unsupported_receipts_=0;
    OwnStockFrame own_stock_{};
    int last_capture_actor_=-1,last_capture_group_=-1,last_capture_round_=-1;
    std::uint64_t last_capture_epoch_=0;
    covenant::Observation observation_{};
    covenant::RepairQuotes quotes_{};
    detour::Route alternate_{};
    const Information* alternate_information_=nullptr;
    const Menu* alternate_menu_=nullptr;
    ObservationClock alternate_clock_{};
    covenant::JobKey alternate_key_{};
    std::uint64_t alternate_job_epoch_=0, alternate_contract_epoch_=0, alternate_contract_sequence_=0;
    int alternate_remaining_group_=-1;
    bool alternate_pending_=false;
    std::unique_ptr<Forecast> quote_=std::make_unique<Forecast>();
    std::unique_ptr<SimulationState> quote_state_=std::make_unique<SimulationState>();
    int supported_paths_=0, composed_paths_=0, unknown_paths_=0, work_=0, samples_=0;
    covenant::BindingGap gap_=covenant::BindingGap::source;
    static bool fresh(Provenance p,int round) { return p.observed_round==round && p.status==Status::exact_now &&
        (p.source==Source::personal_vision || p.source==Source::own_state || p.source==Source::own_occupancy); }
    static bool valid_info(const Information& info) {return info.round>=0 && info.own.id>=0 && info.group_count>=0 && info.group_count<=max_groups &&
        info.altar_count>=0 && info.altar_count<=max_altars && std::isfinite(info.own.health) && std::isfinite(info.own.maximum_health) &&
        info.own.maximum_health>0 && info.own.health>=0 && info.own.health<=info.own.maximum_health;}
    bool current_stock(const Information& info) const {
        if (!own_stock_.known || !own_stock_.stock_epoch || !info.own.alive || own_stock_.clock.round!=info.round ||
            own_stock_.clock.own.id!=info.own.id || own_stock_.bag!=info.own.bag || own_stock_.count<0 || own_stock_.count>2) return false;
        std::uint64_t retained=0;
        for (int n=0;n<own_stock_.count;++n) {
            const auto& row=own_stock_.acquisitions[n];
            if (row.group_id<0 || row.capture_round<0 || row.capture_round>info.round || row.stock_epoch!=own_stock_.stock_epoch ||
                !row.gems || row.provenance.source!=Source::own_state || !fresh(row.provenance,info.round)) return false;
            for (int before=0;before<n;++before) if (own_stock_.acquisitions[before].group_id==row.group_id &&
                own_stock_.acquisitions[before].capture_round==row.capture_round) return false;
            retained+=row.gems;
        }
        return retained<=info.own.bag;
    }
    static bool route_proof(const ExecutionRepairFrame& frame) {
        const auto* route=frame.alternate_route;const auto* info=frame.information;
        return info && route && frame.ordinary_goal_bound && frame.phase_known && frame.phase==ProductionPhase::travel &&
            (frame.selected_job.kind==JobKind::farm || frame.selected_job.kind==JobKind::bank) && detour::valid(*route) &&
            route->actor==info->own.id && route->round==info->round && route->origin==info->own.position &&
            frame.clock.round==route->round && frame.clock.own.id==route->actor;
    }
    static bool same_route(const detour::Route& left,const detour::Route& right) {
        if(left.actor!=right.actor || left.round!=right.round || left.origin!=right.origin || left.obstruction!=right.obstruction ||
            left.goal!=right.goal || left.count!=right.count || left.visits!=right.visits || left.proven!=right.proven) return false;
        for(int n=0;n<left.count;++n) if(left.steps[n]!=right.steps[n]) return false;
        return true;
    }
    covenant::Observation observe(const Information& info) {
        covenant::Observation out;out.round=info.round;out.own=info.own;
        out.physical_provenance={info.round,Source::own_state,Status::exact_now};out.revision=++revision_;
        if (current_stock(info)) for (int n=0;n<own_stock_.count;++n) {
            const auto& row=own_stock_.acquisitions[n];
            out.acquisitions[out.acquisition_count++]={row.group_id,row.stock_epoch,row.provenance};
        }
        if (info.in_progress_group_id>=0) { out.live_capture=true;out.capture_group=info.in_progress_group_id;
            out.capture_provenance={info.round,Source::own_occupancy,Status::exact_now}; }
        if (!monitor_.accepted()) return out;
        const auto target=monitor_.remaining();
        for (int g=0;g<info.group_count && out.premise_count<covenant::premise_limit;++g) if (info.groups[g].id==target.group) {
            const auto& group=info.groups[g];auto truth=covenant::Truth::unknown;
            if (group.visible && fresh(group.provenance,info.round)) {
                if (group.lifecycle==Lifecycle::living || (group.lifecycle==Lifecycle::cleared && group.reward_available)) truth=covenant::Truth::satisfied;
                else if (group.lifecycle==Lifecycle::captured || group.lifecycle==Lifecycle::respawning) truth=covenant::Truth::contradicted;
            }
            out.premises[out.premise_count++]={covenant::PremiseKind::target_available,target.group,truth,group.provenance,revision_};
        }
        for (int a=0;a<info.altar_count && out.premise_count<covenant::premise_limit;++a) if (info.altars[a].id==target.altar) {
            const auto& altar=info.altars[a];
            out.premises[out.premise_count++]={covenant::PremiseKind::altar_unlocked,target.altar,
                !altar.lock_known?covenant::Truth::unknown:altar.hostile_lock?covenant::Truth::contradicted:covenant::Truth::satisfied,
                {info.round,Source::personal_vision,altar.lock_known?Status::exact_now:Status::unknown},revision_};
        }
        return out;
    }
    covenant::RepairQuotes quote_repairs(const Information& info,const Menu& menu) {
        covenant::RepairQuotes out;
        for (int n=0;n<menu.count;++n) {
            const auto& job=menu.jobs[n];
            if (job.kind==JobKind::bank && out.bank_count<2 && job.altar>=0 && job.altar<info.altar_count) {
                const auto& altar=info.altars[job.altar];const int d=distance(info,info.own.position,altar.position);
                out.banks[out.bank_count++]={altar.id,d<0?-1:d+tribute_ticks(info.own.bag,info.settings),
                    d>=0 && altar.accessible && altar.lock_known && !altar.hostile_lock};
            }
        }
        for (int n=0;n<menu.count;++n) if (menu.jobs[n].kind==JobKind::renew) {
            if (!can_refine(info)) break;
            plan4::simulate_first_into(info,menu.jobs[n],menu,*quote_,*quote_state_);
            if (!quote_->budget_exhausted && quote_->abort_reason==AbortReason::none && info.own.health<info.own.maximum_health &&
                quote_->final_health==info.own.maximum_health) {
                out.renew_duration=quote_->final_round-info.round;
                if (info.own.on_spawn || info.own.recall_safe) out.retreat_duration=out.renew_duration;
            }
            break;
        }
        return out;
    }
    void reconcile_receipts(const Information& info) {
        for (int n=0;n<receipt_count_;++n) {
            const auto& bound=receipts_[n];const auto& source=bound.source;
            if (!monitor_.accepted() || source.actor!=info.own.id || source.job_epoch!=job_epoch_ || !source.completed || source.end_round<source.start_round ||
                source.end_round>info.round || std::int64_t(info.round)-source.end_round>1 ||
                bound.contract_epoch!=monitor_.contract().epoch || bound.contract_sequence!=monitor_.contract().phase_sequence ||
                source.executed_mode!=source.requested_mode || source.legal_wait_rounds<0 || source.waiting_bound<source.legal_wait_rounds ||
                source.legal_wait_rounds>source.end_round-source.start_round || !source.waiting_legal ||
                !source.endpoint_observed || source.emergency || source.censor!=PhaseCensor::none) { ++unsupported_receipts_;continue; }
            auto receipt=covenant::Receipt{monitor_.contract().key,monitor_.contract().epoch,monitor_.contract().phase_sequence,
                info.own.id,info.round,covenant::Milestone::arrival,{info.round,Source::own_state,Status::exact_now},true,false,-1,0};
            covenant::ReceiptResult result=covenant::ReceiptResult::unsupported;
            const Group* group=nullptr;
            for(int g=0;g<info.group_count;++g) if(info.groups[g].id==source.group_id) {
                if(group) {group=nullptr;break;} group=&info.groups[g];
            }
            const bool current_group=group && group->visible && fresh(group->provenance,info.round) &&
                source.group_id==monitor_.remaining().group;
            // An END receipt may be reconciled at the immediately following
            // START. Its timestamp stays in source; this milestone is confirmed
            // now from fresh physical evidence, never a backdated snapshot.
            if (source.phase==ProductionPhase::travel && !bound.repair && selected_job_.kind==JobKind::farm && current_group &&
                (group->lifecycle==Lifecycle::living || (bound.following_clear_nominal>0 && group->lifecycle==Lifecycle::cleared)) &&
                info.own.channel==Channel::none) {
                const int nominal=bound.following_clear_nominal>0?bound.following_clear_nominal:group->clear_rounds;
                if(nominal>0) result=monitor_.complete(receipt,nominal,-1,observation_);
            } else if (source.phase==ProductionPhase::clear && !bound.repair && selected_job_.kind==JobKind::farm &&
                current_group && group->lifecycle==Lifecycle::cleared && group->reward_available && info.own.channel==Channel::none) {
                receipt.milestone=covenant::Milestone::clear;
                receipt.observation=group->provenance;
                const int nominal=info.settings.capture_rounds>=0 && info.settings.capture_rounds<max_horizon?info.settings.capture_rounds+1:-1;
                result=monitor_.complete(receipt,nominal,-1,observation_);
            } else if (source.phase==ProductionPhase::restore &&
                info.own.on_spawn && info.own.health==info.own.maximum_health && info.own.channel==Channel::none) {
                // A no-action START can queue the observed endpoint before the
                // ordinary snapshot runs. The shared one-round receipt window
                // permits fresh full-health confirmation now; never backdate it.
                receipt.milestone=covenant::Milestone::restoration;receipt.own_attribution=true;
                result=bound.repair?monitor_.complete_repair(receipt,observation_):monitor_.complete(receipt,0,-1,observation_);
            } else if (source.phase==ProductionPhase::bank && source.end_round==info.round && info.own.bag==0 && info.own.channel==Channel::none) {
                receipt.milestone=covenant::Milestone::conversion;receipt.own_attribution=true;
                result=bound.repair?monitor_.complete_repair(receipt,observation_):monitor_.complete(receipt,0,-1,observation_);
            } else if (source.phase==ProductionPhase::travel && !bound.repair && selected_job_.kind==JobKind::bank &&
                selected_job_.altar>=0 && selected_job_.altar<info.altar_count && source.end_round<=info.round) {
                const auto target=info.altars[selected_job_.altar].altar_position;
                if (std::max(std::abs(info.own.position.x-target.x),std::abs(info.own.position.y-target.y))<=1)
                    result=monitor_.complete(receipt,tribute_ticks(info.own.bag,info.settings),-1,observation_);
            }
            if (result==covenant::ReceiptResult::advanced) { ++samples_;++revision_; }
            else ++unsupported_receipts_;
        }
        receipt_count_=0;
    }
    void begin_batch(const Information& info,const Menu& menu) {
        supported_paths_=composed_paths_=unknown_paths_=work_=0;batch_budget_=false;gap_=covenant::BindingGap::source;
        source_current_=snapshot_==&info && snapshot_clock_.round==info.round && snapshot_clock_.own.id==info.own.id && valid_info(info);
        if (source_current_) { observation_=observe(info);quotes_=quote_repairs(info,menu); }
    }
    void predict_current(const Information& info,const Job& job,const Menu& menu,Forecast& out) {
        if (!source_current_) { plan4::predict_into(info,job,menu,out);++unknown_paths_;return; }
        const auto result=product_.predict_into(info,job,menu,monitor_,observation_,quotes_,enabled_,execution_bound_,phase_supported_,out);
        ++work_;work_+=result.continuation_steps;batch_budget_|=out.budget_exhausted || result.gap==covenant::BindingGap::budget;
        if (result.supported) ++supported_paths_;else { ++unknown_paths_;gap_=result.gap; }
        composed_paths_+=result.composed && !result.redundant;
    }
    struct CandidateOnly { Covenant& self;void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& f) {self.predict_current(i,j,m,f);} };
public:
    explicit Covenant(bool enabled=true):monitor_(enabled),enabled_(enabled) {}
    // Staged fixtures warm up with flat forecasts, then restore their arm.
    // Forecast mode never resets the actual monitor or its accepted promise.
    void set_enabled(bool enabled) { enabled_=enabled; monitor_.set_enabled(enabled); }
    const char* name() const {return "covenant";}
    void observe_own_stock(const OwnStockFrame& frame) {
        bool stable=true;
        if (own_stock_.known && frame.known && own_stock_.stock_epoch==frame.stock_epoch &&
            own_stock_.clock.own.id==frame.clock.own.id && frame.count>=0 && frame.count<=2 &&
            own_stock_.count>=0 && own_stock_.count<=2) {
            if (frame.clock.round<own_stock_.clock.round) stable=false;
            for (int n=0;n<frame.count;++n) {
                const auto& row=frame.acquisitions[n];bool previous_group=false,previous_capture=false;
                for (int before=0;before<own_stock_.count;++before) {
                    const auto& previous=own_stock_.acquisitions[before];
                    if (row.group_id!=previous.group_id) continue;
                    previous_group=true;
                    if (row.capture_round==previous.capture_round) {
                        previous_capture=true;if (row.gems!=previous.gems) stable=false;
                    }
                }
                // A refreshed stock proof retains the capture timestamp. Only
                // a newly observed capture can replace that association.
                if (previous_group && !previous_capture && row.capture_round!=frame.clock.round) stable=false;
            }
        }
        own_stock_=frame;if (!stable) own_stock_.known=false;
    }
    void observe_snapshot(const Information& info,MemberFrame = {}) {
        alternate_pending_=false;
        snapshot_=&info;snapshot_clock_={info.round,{info.own.id}};source_current_=false;
        supported_paths_=composed_paths_=unknown_paths_=work_=0;batch_budget_=false;
        if (!valid_info(info)) {snapshot_=nullptr;return;}
        observation_=observe(info);reconcile_receipts(info);
    }
    bool capture_receipt(const Information& info,const CapturedGroup& source) {
        const auto reject=[&]() {++unsupported_receipts_;return false;};
        if (!valid_info(info) || snapshot_!=&info || snapshot_clock_.round!=info.round || snapshot_clock_.own.id!=info.own.id ||
            !selected_ || issued_repair_valid_ || selected_job_.kind!=JobKind::farm || !monitor_.accepted() ||
            monitor_.contract().actor!=info.own.id || monitor_.contract().key!=covenant::physical_key(info,selected_job_) ||
            monitor_.remaining().stage!=covenant::Stage::capture || monitor_.remaining().group!=source.group_id ||
            info.own.channel!=Channel::none || info.in_progress_group_id>=0 || !current_stock(info) || source.origin!=CaptureOrigin::isolated_own_stock ||
            !source.actor_attribution_known || source.actor_id!=info.own.id || source.round!=info.round ||
            source.own_provenance.source!=Source::own_state || !fresh(source.own_provenance,info.round) ||
            !source.stock_epoch || source.stock_epoch!=own_stock_.stock_epoch || source.bag_after!=info.own.bag ||
            source.bag_after<=source.bag_before || !source.received_gems || source.received_gems!=source.bag_after-source.bag_before ||
            (last_capture_actor_==source.actor_id && last_capture_group_==source.group_id &&
                last_capture_round_==source.round && last_capture_epoch_==source.stock_epoch)) return reject();
        bool associated=false;
        for (int n=0;n<own_stock_.count;++n) {
            const auto& row=own_stock_.acquisitions[n];
            associated|=row.group_id==source.group_id && row.capture_round==source.round &&
                row.stock_epoch==source.stock_epoch && row.gems==source.received_gems;
        }
        if (!associated) return reject();
        const Group* captured=nullptr;
        for (int g=0;g<info.group_count;++g) if (info.groups[g].id==source.group_id) {
            if (captured) return reject();captured=&info.groups[g];
        }
        if (captured && captured->visible && (captured->provenance.source!=Source::personal_vision ||
            !fresh(captured->provenance,info.round) || captured->reward_available ||
            (captured->lifecycle!=Lifecycle::captured && captured->lifecycle!=Lifecycle::respawning))) return reject();
        const auto& contract=monitor_.contract();int nominal=-1;bool found=false;
        if (contract.remaining_group==0 && contract.key.second_group>=0 && !(contract.completed_mask&2u)) {
            for (int g=0;g<info.group_count;++g) if (info.groups[g].id==contract.key.second_group) {
                if (found) return reject();found=true;nominal=distance(info,info.own.position,info.groups[g].position);
            }
        } else {
            for (int a=0;a<info.altar_count;++a) if (info.altars[a].id==contract.key.altar) {
                if (found) return reject();found=true;const int travel=distance(info,info.own.position,info.altars[a].position);
                const auto duration=std::int64_t(travel)+tribute_ticks(info.own.bag,info.settings);
                if (travel>=0 && duration<=max_horizon) nominal=int(duration);
            }
        }
        if (nominal<0) return reject();
        observation_=observe(info);
        const covenant::Receipt receipt{contract.key,contract.epoch,contract.phase_sequence,source.actor_id,source.round,
            covenant::Milestone::capture_receipt,source.own_provenance,true,true,source.group_id,source.stock_epoch};
        if (monitor_.complete(receipt,nominal,-1,observation_)!=covenant::ReceiptResult::advanced) return reject();
        last_capture_actor_=source.actor_id;last_capture_group_=source.group_id;last_capture_round_=source.round;last_capture_epoch_=source.stock_epoch;
        ++samples_;observation_=observe(info);return true;
    }
    void selected(const Information& info,const Job& job,std::uint64_t epoch,bool productive) {
        alternate_pending_=false;
        if (!productive || !valid_info(info)) {selected_=false;phase_supported_=false;return;}
        // A proof about a previously executing physical job cannot bind a new
        // contract. Same-key retries retain the original deadline/latch state.
        if (!monitor_.accepted() || covenant::physical_key(info,job)!=monitor_.contract().key) execution_bound_=false;
        if(epoch!=job_epoch_ || covenant::physical_key(info,job)!=monitor_.contract().key) {phase_pending_=false;receipt_count_=0;}
        selected_=true;selected_job_=job;job_epoch_=epoch;issued_repair_valid_=false;
        observation_=observe(info);
        const auto key=covenant::physical_key(info,job);auto stage=covenant::Stage::done;int nominal=-1;
        if (job.kind==JobKind::farm && job.group>=0 && job.group<info.group_count) {
            stage=covenant::Stage::approach;nominal=distance(info,info.own.position,info.groups[job.group].position);
        } else if (job.kind==JobKind::bank && job.altar>=0 && job.altar<info.altar_count) {
            nominal=distance(info,info.own.position,info.altars[job.altar].position);
            stage=nominal==0?covenant::Stage::convert:covenant::Stage::approach;
            if (!nominal) nominal=info.own.channel==Channel::tribute?info.own.channel_ticks:tribute_ticks(info.own.bag,info.settings);
        } else if (job.kind==JobKind::renew) {stage=covenant::Stage::restore;nominal=nominal_restore(info);}
        phase_supported_=job.kind==JobKind::bank && nominal>=0;
        if (nominal>=0) monitor_.accept(key,stage,nominal,-1,observation_);
    }
    void selection_canceled() {selected_=false;phase_supported_=false;alternate_pending_=false;}
    bool phase_started(const PhaseStart& source) {
        const int expected_mode=source.phase==ProductionPhase::travel?0:source.phase==ProductionPhase::clear?2:
            source.phase==ProductionPhase::bank?1:source.phase==ProductionPhase::restore?3:-1;
        if (source.actor!=snapshot_clock_.own.id || source.job_epoch!=job_epoch_ ||
            source.start_round!=observation_.round || source.nominal_duration<=0 || expected_mode<0 || source.mode!=expected_mode || source.emergency || phase_pending_ ||
            source.phase_sequence<=latest_sequence_ || !monitor_.accepted() || (!selected_ && !issued_repair_valid_)) {++unsupported_receipts_;return false;}
        const auto stage=monitor_.remaining().stage;
        const bool farm=selected_job_.kind==JobKind::farm && source.group_id>=0 && source.group_id==monitor_.remaining().group;
        BoundReceipt* arrival=receipt_count_>0?&receipts_[receipt_count_-1]:nullptr;
        const bool after_arrival=source.phase==ProductionPhase::clear && stage==covenant::Stage::approach && farm && !issued_repair_valid_ &&
            arrival && !arrival->repair && arrival->contract_epoch==monitor_.contract().epoch &&
            arrival->contract_sequence==monitor_.contract().phase_sequence && arrival->source.phase==ProductionPhase::travel &&
            arrival->source.actor==source.actor && arrival->source.job_epoch==source.job_epoch && arrival->source.group_id==source.group_id &&
            arrival->source.end_round==source.start_round && arrival->source.completed && arrival->source.endpoint_observed &&
            !arrival->source.emergency && arrival->source.censor==PhaseCensor::none &&
            arrival->source.executed_mode==arrival->source.requested_mode && arrival->following_clear_nominal<0;
        const bool ordinary=(source.phase==ProductionPhase::travel && stage==covenant::Stage::approach &&
                ((farm && !issued_repair_valid_) || (selected_job_.kind==JobKind::bank && source.group_id==-1))) ||
            (source.phase==ProductionPhase::clear && stage==covenant::Stage::clear && farm && !issued_repair_valid_) || after_arrival ||
            (source.phase==ProductionPhase::bank && source.group_id==-1 && stage==covenant::Stage::convert) ||
            (source.phase==ProductionPhase::restore && source.group_id==-1 && stage==covenant::Stage::restore && selected_job_.kind==JobKind::renew);
        const bool repair=monitor_.contract().failed && issued_repair_valid_ && source.group_id==-1 &&
            ((issued_repair_.kind==JobKind::bank && source.phase==ProductionPhase::bank) ||
             (issued_repair_.kind==JobKind::renew && source.phase==ProductionPhase::restore));
        // Capture lifecycle evidence has no own reward/stock association. It
        // cannot advance the acquisition milestone, even with a larger bag.
        if(!ordinary && !repair) {++unsupported_receipts_;return false;}
        phase_start_=source;phase_pending_=true;latest_sequence_=source.phase_sequence;
        phase_contract_epoch_=monitor_.contract().epoch;phase_contract_sequence_=monitor_.contract().phase_sequence;phase_repair_=repair;
        if(after_arrival) {
            // The producer may move and begin a targeted clear in one owner
            // turn. Preserve their actual order until the next fresh snapshot;
            // neither a start nor a queued arrival fabricates a clear endpoint.
            arrival->following_clear_nominal=source.nominal_duration;++phase_contract_sequence_;
        }
        return true;
    }
    void phase_completed(const PhaseReceipt& source) {
        if (!phase_pending_ || source.actor!=phase_start_.actor || source.job_epoch!=phase_start_.job_epoch ||
            source.phase_sequence!=phase_start_.phase_sequence || source.phase!=phase_start_.phase || source.start_round!=phase_start_.start_round ||
            source.nominal_duration!=phase_start_.nominal_duration || source.requested_mode!=phase_start_.mode || source.stat_epoch!=phase_start_.stat_epoch ||
            source.group_id!=phase_start_.group_id) {
            ++unsupported_receipts_;return;
        }
        phase_pending_=false;
        if (receipt_count_==int(receipts_.size())) {++unsupported_receipts_;return;}
        receipts_[receipt_count_++]={source,phase_contract_epoch_,phase_contract_sequence_,phase_repair_};
    }
    ExecutionRepair execution_repair(const ExecutionRepairFrame& frame) {
        alternate_pending_=false;
        ExecutionRepair out;out.job_epoch=frame.job_epoch;
        const auto* info=frame.information;const auto* menu=frame.fixed_menu;
        if (!info || !menu || !frame.same_epoch || frame.job_epoch!=job_epoch_ || !selected_ ||
            frame.clock.round!=info->round || frame.clock.own.id!=info->own.id || menu->count<0 || menu->count>max_jobs ||
            !valid_info(*info) || !can_refine(*info)) return out;
        const auto key=covenant::physical_key(*info,frame.selected_job);
        if (key!=covenant::physical_key(*info,selected_job_) && (!issued_repair_valid_ || key!=covenant::physical_key(*info,issued_repair_))) return out;
        if (!issued_repair_valid_ && frame.selected_job.kind==JobKind::farm && monitor_.accepted() && monitor_.remaining().group>=0 &&
            (frame.remaining_group<0 || frame.remaining_group>=info->group_count || info->groups[frame.remaining_group].id!=monitor_.remaining().group)) return out;
        execution_bound_=true;observation_=observe(*info);reconcile_receipts(*info);
        if (frame.protected_progress || covenant::protected_progress(observation_)) {out.supported=true;out.action=ExecutionRepairAction::preserve;return out;}
        if (!monitor_.accepted()) return out;
        quotes_=quote_repairs(*info,*menu);
        const bool route_bound=route_proof(frame) && !issued_repair_valid_ &&
            monitor_.bind_approach_connector(covenant::connector_id(frame.alternate_route->obstruction),observation_);
        if(route_bound && observation_.premise_count<covenant::premise_limit) {
            observation_.premises[observation_.premise_count++]={covenant::PremiseKind::connector_open,
                covenant::connector_id(frame.alternate_route->obstruction),covenant::Truth::contradicted,
                {info->round,Source::personal_vision,Status::exact_now},observation_.revision};
            quotes_.alternate_path_duration=frame.alternate_route->count;
        }
        const auto known=monitor_.forecast_guard(monitor_.contract().key,monitor_.remaining(),-1,observation_,quotes_);
        phase_supported_=frame.phase_known && ((frame.phase==ProductionPhase::travel && monitor_.remaining().stage==covenant::Stage::approach) ||
            (frame.phase==ProductionPhase::clear && monitor_.remaining().stage==covenant::Stage::clear) ||
            (frame.phase==ProductionPhase::bank && monitor_.remaining().stage==covenant::Stage::convert) ||
            (frame.phase==ProductionPhase::restore && monitor_.remaining().stage==covenant::Stage::restore));
        if (!known.activate && !phase_supported_) return out;
        const auto guard=monitor_.observe(observation_,quotes_);
        if (!guard.activate) {out.supported=phase_supported_;return out;}
        if (!guard.supported || guard.repair.quoted_duration<0) return out;
        using Kind=covenant::RepairKind;
        if (guard.repair.kind==Kind::convert_retained) {
            for (int n=0;n<menu->count;++n) if (menu->jobs[n].kind==JobKind::bank && menu->jobs[n].altar>=0 &&
                menu->jobs[n].altar<info->altar_count && info->altars[menu->jobs[n].altar].id==guard.repair.altar_id) {
                out.supported=true;out.action=ExecutionRepairAction::bank;out.bank_menu_index=n;
                issued_repair_=menu->jobs[n];issued_repair_valid_=true;break;
            }
        } else if (guard.repair.kind==Kind::renew || guard.repair.kind==Kind::safe_retreat_renew) {
            for (int n=0;n<menu->count;++n) if (menu->jobs[n].kind==JobKind::renew) {
                out.supported=true;
                out.action=guard.repair.kind==Kind::renew?ExecutionRepairAction::renew:ExecutionRepairAction::safe_retreat;
                issued_repair_=menu->jobs[n];issued_repair_valid_=true;break;
            }
        }
        else if (guard.repair.kind==Kind::replan) {out.supported=true;out.action=ExecutionRepairAction::replan;}
        else if (guard.repair.kind==Kind::alternate_path && guard.cause==covenant::Cause::approach_obstruction && route_bound) {
            out.supported=true;out.action=ExecutionRepairAction::alternate_path;
            alternate_=*frame.alternate_route;alternate_information_=info;alternate_menu_=menu;alternate_clock_=frame.clock;
            alternate_key_=key;alternate_job_epoch_=frame.job_epoch;alternate_remaining_group_=frame.remaining_group;
            alternate_contract_epoch_=monitor_.contract().epoch;alternate_contract_sequence_=monitor_.contract().phase_sequence;alternate_pending_=true;
        }
        return out;
    }
    bool alternate_route_result(const ExecutionRepairFrame& frame,bool moved,Point actual_position) {
        if(!alternate_pending_ || !selected_ || !frame.same_epoch || !route_proof(frame) ||
            frame.information!=alternate_information_ || frame.fixed_menu!=alternate_menu_ || frame.job_epoch!=alternate_job_epoch_ ||
            job_epoch_!=alternate_job_epoch_ || frame.remaining_group!=alternate_remaining_group_ ||
            frame.clock.round!=alternate_clock_.round || frame.clock.own.id!=alternate_clock_.own.id ||
            !same_route(*frame.alternate_route,alternate_) || !valid_info(*frame.information) ||
            covenant::physical_key(*frame.information,frame.selected_job)!=alternate_key_ ||
            covenant::physical_key(*frame.information,selected_job_)!=alternate_key_ || !monitor_.accepted() ||
            monitor_.contract().epoch!=alternate_contract_epoch_ || monitor_.contract().phase_sequence!=alternate_contract_sequence_ ||
            !monitor_.contract().failed || monitor_.contract().failed_cause!=covenant::Cause::approach_obstruction) return false;
        alternate_pending_=false;
        if(!moved || actual_position!=alternate_.steps[0] || frame.protected_progress) return false;
        auto progress=observe(*frame.information);progress.own.position=actual_position;
        if(covenant::protected_progress(progress) || progress.premise_count>=covenant::premise_limit) return false;
        const int new_connector=covenant::connector_id(actual_position);
        progress.premises[progress.premise_count++]={covenant::PremiseKind::connector_open,new_connector,covenant::Truth::satisfied,
            {progress.round,Source::own_state,Status::exact_now},progress.revision};
        const int remaining=alternate_.count-1;
        if(!monitor_.alternate_route_progress(covenant::connector_id(alternate_.obstruction),new_connector,remaining,progress)) return false;
        // The old production phase was interrupted. Its queued evidence cannot
        // become an arrival or clear receipt for the resumed promise.
        unsupported_receipts_+=receipt_count_;receipt_count_=0;phase_pending_=false;observation_=progress;
        ++samples_;phase_supported_=true;
        if(remaining==0 && actual_position==alternate_.goal) {
            int next_nominal=-1;
            const auto& info=*frame.information;
            if(frame.selected_job.kind==JobKind::farm && frame.remaining_group>=0 && frame.remaining_group<info.group_count) {
                const auto& group=info.groups[frame.remaining_group];
                if(group.id==monitor_.remaining().group && group.visible && fresh(group.provenance,info.round) &&
                    (group.lifecycle==Lifecycle::living || group.lifecycle==Lifecycle::cleared) && group.clear_rounds>=0) next_nominal=group.clear_rounds;
            } else if(frame.selected_job.kind==JobKind::bank) next_nominal=tribute_ticks(info.own.bag,info.settings);
            if(next_nominal>=0) {
                const auto& contract=monitor_.contract();
                const covenant::Receipt arrival{contract.key,contract.epoch,contract.phase_sequence,progress.own.id,progress.round,
                    covenant::Milestone::arrival,{progress.round,Source::own_state,Status::exact_now},true,false,-1,0};
                if(monitor_.complete(arrival,next_nominal,-1,progress)==covenant::ReceiptResult::advanced) ++samples_;
            }
        }
        return true;
    }
    ActiveJobConsultation active_job_consultation(const ExecutionRepairFrame& frame) const {
        if (!frame.information || !selected_ || !execution_bound_ || !phase_supported_ || issued_repair_valid_ ||
            frame.job_epoch!=job_epoch_ || !monitor_.accepted() || monitor_.contract().failed ||
            snapshot_!=frame.information || snapshot_clock_.round!=frame.clock.round ||
            snapshot_clock_.own.id!=frame.clock.own.id || !valid_info(*frame.information) ||
            covenant::physical_key(*frame.information,frame.selected_job)!=monitor_.contract().key) return {};
        const auto& contract=monitor_.contract();
        ActiveJobConsultation result;result.supported=true;result.epoch=contract.epoch;
        result.sequence=contract.phase_sequence;result.deadline=contract.deadline;result.completed=contract.completed_mask;
        return result;
    }
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out) {begin_batch(info,menu);CandidateOnly candidates{*this};evaluate_model(info,menu,candidates,out);batch_budget_|=out.budget_exhausted;}
    void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out) {begin_batch(info,menu);predict_current(info,job,menu,out);}
    ModelEvidence model_evidence() const {
        ModelEvidence out;out.reported=true;out.reason=!source_current_?int(covenant::BindingGap::source):batch_budget_?int(covenant::BindingGap::budget):supported_paths_?0:int(gap_);
        out.supported=out.reason==0;
        // An incomplete batch may still contain a completed influenced prefix;
        // report that fact even while its budget/support reason is nonzero.
        out.coupling_enabled=source_current_ && enabled_ && composed_paths_>0;
        out.samples=samples_;out.unknown=unknown_paths_+unsupported_receipts_;out.paths=supported_paths_;out.work=work_;out.revision=revision_;return out;
    }
    const covenant::Covenant& monitor() const {return monitor_;}
};
} // namespace plan4::runtime
