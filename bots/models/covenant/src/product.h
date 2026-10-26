#pragma once
#include "covenant.h"
#include <memory>

namespace plan4::covenant {
enum class BindingGap { none, source, execution, unknown_guard, future_checkpoint, repair, continuation, budget, phase };
struct FixedResult {
    bool supported=false, composed=false, redundant=false, guard_known=false, protected_progress=false;
    BindingGap gap=BindingGap::unknown_guard;
    Guard guard{};
    int continuation_steps=0, repair_menu_index=-1;
};
inline JobKey physical_key(const Information& info,const Job& job) {
    JobKey key; key.kind=job.kind;
    if (job.group>=0 && job.group<info.group_count) key.first_group=info.groups[job.group].id;
    if (job.second_group>=0 && job.second_group<info.group_count) key.second_group=info.groups[job.second_group].id;
    if (job.altar>=0 && job.altar<info.altar_count) key.altar=info.altars[job.altar].id;
    return key;
}
inline bool same_physical(const Information& info,const Observation& observation) {
    return current(observation) && observation.round==info.round && observation.own.id==info.own.id &&
        observation.own.position==info.own.position && observation.own.health==info.own.health &&
        observation.own.maximum_health==info.own.maximum_health && observation.own.bag==info.own.bag &&
        observation.own.gold==info.own.gold && observation.own.channel==info.own.channel &&
        observation.own.channel_ticks==info.own.channel_ticks && observation.own.alive==info.own.alive;
}

// Repairs begin at a common physical checkpoint. An unfinished phase is executed
// forward to its owner-action cut; aggregate finish times cannot rebase it.
// Every supported repair runs the common physical primitive and complete fixed
// cyclic tail. No callback may add a reward or overwrite a physical source row.
class FixedProduct {
    std::unique_ptr<Forecast> first_=std::make_unique<Forecast>(), flat_=std::make_unique<Forecast>(),
        path_a_=std::make_unique<Forecast>(), path_b_=std::make_unique<Forecast>();
    std::unique_ptr<SimulationState> state_a_=std::make_unique<SimulationState>(), state_b_=std::make_unique<SimulationState>();
    struct Latch { int group=-1,until=-1; };
    static bool equal(const Forecast& a,const Forecast& b) {
        if (a.event_count!=b.event_count || a.macros_started!=b.macros_started || a.final_round!=b.final_round ||
            a.first_conversion_round!=b.first_conversion_round || a.our_secured!=b.our_secured || a.enemy_secured!=b.enemy_secured ||
            a.final_bag!=b.final_bag || a.final_gold!=b.final_gold || a.final_health!=b.final_health || a.terminal_z!=b.terminal_z ||
            a.truncated!=b.truncated || a.abort_reason!=b.abort_reason || a.stock_unknown!=b.stock_unknown ||
            a.travel_unknown!=b.travel_unknown || a.lock_unknown!=b.lock_unknown || a.terminal_unknown!=b.terminal_unknown ||
            a.no_conversion!=b.no_conversion || a.completed_conversion!=b.completed_conversion) return false;
        for (int e=0;e<a.event_count;++e) {
            const auto& x=a.events[e];const auto& y=b.events[e];
            if (x.kind!=y.kind || x.start.round!=y.start.round || x.start.phase!=y.start.phase || x.end.round!=y.end.round || x.end.phase!=y.end.phase ||
                x.group_id!=y.group_id || x.bag_before!=y.bag_before || x.bag_after!=y.bag_after || x.secured!=y.secured ||
                x.gold_after!=y.gold_after || x.health_after!=y.health_after || x.status!=y.status) return false;
        }
        return true;
    }
    static bool allowed(int group,int now,const Information& info,const void*,void* path) {
        const auto& latch=*static_cast<const Latch*>(path);
        return group<0 || group>=info.group_count || info.groups[group].id!=latch.group || now>=latch.until;
    }
    static bool unknown_future(const Forecast& path) {
        return path.stock_unknown || path.travel_unknown || path.lock_unknown ||
            path.abort_reason==AbortReason::unknown_stock || path.abort_reason==AbortReason::unknown_travel ||
            path.abort_reason==AbortReason::contested || path.abort_reason==AbortReason::unsupported_channel ||
            path.abort_reason==AbortReason::unsupported_wait || path.abort_reason==AbortReason::event_capacity;
    }
    static int milestone_finish(Stage stage,const Information& info,const Forecast& first) {
        int restore_finish=-1;
        for (int e=0;e<first.event_count;++e) {
            const auto& event=first.events[e];
            if (stage==Stage::restore && event.kind==EventKind::heal) restore_finish=event.end.round;
            if ((stage==Stage::approach && event.kind==EventKind::travel) ||
                (stage==Stage::clear && event.kind==EventKind::clear) ||
                (stage==Stage::capture && event.kind==EventKind::capture) ||
                (stage==Stage::convert && event.kind==EventKind::bank)) return event.end.round;
        }
        // A horizon abort proves an unfinished milestone lies beyond H. This
        // lower bound may establish a missed promise; it is never a fabricated
        // completion time or physical checkpoint. The forward cut still has to
        // succeed before any future repair is admitted.
        if (first.abort_reason==AbortReason::horizon && !first.budget_exhausted && !unknown_future(first))
            return info.round+horizon(info)+1;
        return restore_finish;
    }
    static int repair_job(const Information& info,const Menu& menu,const Repair& repair) {
        for (int n=0;n<menu.count;++n) {
            const auto& job=menu.jobs[n];
            if (repair.kind==RepairKind::convert_retained && job.kind==JobKind::bank && job.altar>=0 && job.altar<info.altar_count &&
                info.altars[job.altar].id==repair.altar_id && info.altars[job.altar].accessible &&
                info.altars[job.altar].lock_known && !info.altars[job.altar].hostile_lock) return n;
            if ((repair.kind==RepairKind::renew || repair.kind==RepairKind::safe_retreat_renew) && job.kind==JobKind::renew) return n;
            if (repair.kind==RepairKind::replan && job.kind==JobKind::hold && info.own.channel==Channel::none && info.in_progress_group_id<0) return n;
        }
        return -1;
    }
    static Job remaining_job(const Information& info,const Job& candidate,const Covenant& monitor) {
        Job result=candidate;
        if (!monitor.accepted() || monitor.contract().stage==Stage::done || monitor.contract().key!=physical_key(info,candidate) || candidate.kind!=JobKind::farm) return result;
        const unsigned completed=monitor.contract().completed_mask;
        if (completed&1u) {
            if (candidate.second_group>=0 && !(completed&2u)) { result.group=candidate.second_group; result.second_group=-1; }
            else { result.kind=JobKind::bank; result.group=result.second_group=-1; }
        } else if (completed&2u) result.second_group=-1;
        return result;
    }
    bool quote_future_repairs(const Information& info,const Menu& menu,const Forecast& prefix,
        const SimulationState& state,RepairQuotes& quotes,bool& denied) {
        quotes={};denied=false;
        // Keep the retained bank order. A checkpoint changes physical costs,
        // not the candidate menu or the actual repair table's preference.
        for (int n=0;n<menu.count;++n) {
            const auto& job=menu.jobs[n];
            if (job.kind!=JobKind::bank || quotes.bank_count>=2 || job.altar<0 || job.altar>=info.altar_count) continue;
            const auto& altar=info.altars[job.altar];
            auto& bank=quotes.banks[quotes.bank_count++];bank.altar_id=altar.id;
            if (!altar.accessible || !altar.lock_known || altar.hostile_lock) continue;
            if (!can_refine(info)) {denied=true;return false;}
            path_b_->reset();
            const bool ran=plan4::run_job_from_into(info,menu,prefix,state,job,*path_b_,*state_b_);
            if (path_b_->budget_exhausted || !can_refine(info)) {denied=true;return false;}
            if (ran && path_b_->abort_reason==AbortReason::none && !path_b_->truncated &&
                !unknown_future(*path_b_) && path_b_->final_round>=state.now) {
                bank.nominal_duration=path_b_->final_round-state.now;bank.accessible=true;
            }
        }
        if (state.health>=info.own.maximum_health) return true;
        for (int n=0;n<menu.count;++n) if (menu.jobs[n].kind==JobKind::renew) {
            if (!can_refine(info)) {denied=true;return false;}
            path_b_->reset();
            const bool ran=plan4::run_job_from_into(info,menu,prefix,state,menu.jobs[n],*path_b_,*state_b_);
            if (path_b_->budget_exhausted || !can_refine(info)) {denied=true;return false;}
            if (ran && path_b_->abort_reason==AbortReason::none && !path_b_->truncated &&
                !unknown_future(*path_b_) && path_b_->final_health==info.own.maximum_health && path_b_->final_round>=state.now) {
                quotes.renew_duration=path_b_->final_round-state.now;
                // The common resume API admits recall safety at the physical
                // checkpoint. An unsupported changed-position recall has no quote.
                quotes.retreat_duration=quotes.renew_duration;
            }
            break;
        }
        return true;
    }
public:
    FixedResult predict_into(const Information& info,const Job& candidate,const Menu& menu,const Covenant& monitor,
        const Observation& observation,const RepairQuotes& quotes,bool enabled,bool execution_bound,bool phase_supported,Forecast& out) {
        FixedResult result;
        // Baseline is the SAME complete common body, not a fabricated score or
        // a private continuation menu. Current inventory/stock stay physical.
        const auto physical_candidate=remaining_job(info,candidate,monitor);
        plan4::simulate_first_into(info,physical_candidate,menu,*first_,*state_a_);
        if (!plan4::continue_from_into(info,menu,*first_,*state_a_,*flat_)) {
            plan4::predict_into(info,physical_candidate,menu,out); result.gap=BindingGap::continuation; return result;
        }
        out=*flat_;
        if (out.budget_exhausted) { result.gap=BindingGap::budget; return result; }
        if (!same_physical(info,observation)) { result.gap=BindingGap::source; return result; }
        if (!execution_bound) { result.gap=BindingGap::execution; return result; }
        if (!monitor.accepted() || physical_key(info,candidate)!=monitor.contract().key) { result.gap=BindingGap::phase; return result; }
        const auto target=monitor.remaining();
        result.guard=monitor.forecast_guard(monitor.contract().key,target,phase_supported?milestone_finish(target.stage,info,*first_):-1,observation,quotes);
        result.protected_progress=result.guard.protected_progress;
        if (result.protected_progress) { result.supported=true; result.gap=BindingGap::none; return result; }
        if (!result.guard.supported || !result.guard.activate) { result.gap=phase_supported?BindingGap::unknown_guard:BindingGap::phase; return result; }
        result.guard_known=true;
        const bool future=result.guard.repair_start_round!=info.round;
        if (future) {
            PhaseCheckpoint checkpoint;
            path_a_->reset();
            if (!plan4::simulate_first_until_into(info,physical_candidate,menu,result.guard.repair_start_round,
                *path_a_,*state_a_,checkpoint)) {
                result.gap=path_a_->budget_exhausted || !can_refine(info)?BindingGap::budget:BindingGap::future_checkpoint;return result;
            }
            if (path_a_->budget_exhausted) {result.gap=BindingGap::budget;return result;}
            if (unknown_future(*path_a_)) {result.gap=BindingGap::future_checkpoint;return result;}
            if (checkpoint.protected_progress) {
                result.protected_progress=result.guard.protected_progress=true;
                result.guard.repair={RepairKind::preserve_progress,-1,0,state_a_->bag};
                result.supported=true;result.gap=BindingGap::none;return result;
            }
            if (checkpoint.first_completed) {
                result.supported=result.redundant=true;result.gap=BindingGap::none;return result;
            }
            if (path_a_->abort_reason!=AbortReason::none || path_a_->truncated || state_a_->stopped ||
                state_a_->now!=result.guard.repair_start_round) {result.gap=BindingGap::future_checkpoint;return result;}
            RepairQuotes future_quotes;bool denied=false;
            if (!quote_future_repairs(info,menu,*path_a_,*state_a_,future_quotes,denied)) {
                result.gap=denied?BindingGap::budget:BindingGap::repair;return result;
            }
            // This is a physical-value projection for the SAME repair table,
            // never an exact future Observation or a new covenant promise.
            Observation projected;projected.own=info.own;
            projected.own.position=state_a_->position;projected.own.health=state_a_->health;
            projected.own.bag=state_a_->bag;projected.own.gold=state_a_->gold;
            projected.own.channel=Channel::none;projected.own.channel_ticks=0;
            result.guard.repair=minimal_repair(result.guard.cause,projected,future_quotes,target.altar);
        }
        const auto& repair=result.guard.repair;
        result.repair_menu_index=repair_job(info,menu,repair);
        if (repair.quoted_duration<0 || result.repair_menu_index<0) { result.gap=BindingGap::repair; return result; }
        Latch latch{target.group,result.guard.blocked_until>=0?result.guard.blocked_until:
            result.guard.repair_start_round<=std::numeric_limits<int>::max()-8?result.guard.repair_start_round+8:std::numeric_limits<int>::max()};
        ForecastHooks hooks; hooks.path=&latch; hooks.acquisition_allowed=allowed;
        if (future) {
            path_b_->reset();
            if (!plan4::run_job_from_into(info,menu,*path_a_,*state_a_,menu.jobs[result.repair_menu_index],*path_b_,*state_b_,hooks)) {
                result.gap=path_b_->budget_exhausted?BindingGap::budget:BindingGap::repair;return result;
            }
            path_a_.swap(path_b_);state_a_.swap(state_b_);
        } else plan4::simulate_first_into(info,menu.jobs[result.repair_menu_index],menu,*path_a_,*state_a_,hooks);
        if (path_a_->budget_exhausted) { result.gap=BindingGap::budget; return result; }
        if (future && unknown_future(*path_a_)) {result.gap=BindingGap::repair;return result;}
        if (path_a_->final_round-result.guard.repair_start_round!=repair.quoted_duration || path_a_->abort_reason!=AbortReason::none) { result.gap=BindingGap::repair; return result; }
        int cursor=0;
        for (int step=0;step<=max_macros;++step) {
            const auto advance=plan4::advance_fixed_into(info,menu,*path_a_,*state_a_,*path_b_,*state_b_,cursor,hooks);
            if (!advance.valid) { result.gap=BindingGap::continuation; return result; }
            if (path_b_->budget_exhausted) { result.gap=BindingGap::budget; return result; }
            if (future && unknown_future(*path_b_)) {result.gap=BindingGap::continuation;return result;}
            if (advance.advanced) {
                ++result.continuation_steps;
                // This fixed tail cannot interpose another repair inside its
                // bank phase. Do not silently ignore an unexpired failed altar.
                if (target.altar>=0 && menu.jobs[advance.job_index].altar>=0 &&
                    info.altars[menu.jobs[advance.job_index].altar].id==target.altar) {
                    for (int e=path_a_->event_count;e<path_b_->event_count;++e)
                        if ((path_b_->events[e].kind==EventKind::bank_start || path_b_->events[e].kind==EventKind::bank) &&
                            path_b_->events[e].start.round<latch.until) { result.gap=BindingGap::continuation; return result; }
                }
            }
            path_a_.swap(path_b_); state_a_.swap(state_b_);
            if (!advance.advanced) {
                result.supported=true;result.gap=BindingGap::none;
                if (!can_refine(info)) {result.supported=false;result.gap=BindingGap::budget;return result;}
                result.redundant=equal(*flat_,*path_a_);
                if (enabled) { out=*path_a_; result.composed=true; }
                return result;
            }
        }
        result.gap=BindingGap::continuation; return result;
    }
};
} // namespace plan4::covenant
