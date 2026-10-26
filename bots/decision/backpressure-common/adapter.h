#pragma once
#include "../common/forecasts.h"
#include "queue.h"
#include <climits>

namespace plan3_backpressure {
// This deliberately narrower production domain protects every inherited helper,
// including unrelated terminal loops. It is not the pure kernel's INT_MAX domain.
// Public metadata preparation may resume; actor/observation validation never does.
class ForecastDomain {
public:
    enum class Reason { none,availability,raw,numeric,fuel_items,fuel_groups,clearbounds,views,fuel_views,fuel_locks,continuation };
    struct Report { Reason reason=Reason::none; int index=-1; };
    static const char* reason_name(Reason reason) {
        switch(reason) {
            case Reason::none:return "none";
            case Reason::availability:return "availability";
            case Reason::raw:return "raw";
            case Reason::numeric:return "numeric";
            case Reason::fuel_items:return "fuel_items";
            case Reason::fuel_groups:return "fuel_groups";
            case Reason::clearbounds:return "clearbounds";
            case Reason::views:return "views";
            case Reason::fuel_views:return "fuel_views";
            case Reason::fuel_locks:return "fuel_locks";
            case Reason::continuation:return "continuation";
        }
        return "unknown";
    }
private:
    static constexpr double scalar_limit=1e12;
    static constexpr std::uint64_t time_limit=1000000;
    std::size_t group_=0,member_=0,item_=0;
    std::vector<unsigned char> occupied_;
    std::array<cambc::item_info,static_cast<unsigned>(cambc::item_type::count)> items_{};
    bool unavailable_=false,ready_=false;
    std::uint64_t largest_gem_=0,second_gem_=0,largest_gold_=0,second_gold_=0;
    static bool scalar(double x) {return std::isfinite(x) && x>=0 && x<=scalar_limit;}
    static bool duration(std::int64_t x) {return x>=0 && std::uint64_t(x)<=time_limit;}
    static void largest(std::uint64_t x,std::uint64_t& a,std::uint64_t& b) {
        if(x>=a) {b=a;a=x;}else b=std::max(b,x);
    }
    static bool deny(Report* out,Reason reason,int index=-1) {
        if(out)*out={reason,index};return false;
    }
public:
    bool ready() const {return ready_;}
    bool unavailable() const {return unavailable_;}
    // No signed narrowing, tile multiplication or forecast occurs before this.
    bool raw_supported(Report* out=nullptr) const {
        if(out)*out={};
        const auto& me=cambc::self();
        const auto w=cambc::map::width(),h=cambc::map::height();
        if(w<1 || w>100 || h<1 || h>100 || cambc::constants::max_rounds>time_limit ||
           cambc::round_number()>cambc::constants::max_rounds || !cambc::constants::winning_gems ||
           cambc::constants::winning_gems>INT_MAX || me.gem_count()>INT_MAX || me.gold_count()>INT_MAX ||
           cambc::global_submitted_gems(cambc::team::ally)>INT_MAX ||
           cambc::global_submitted_gems(cambc::team::enemy)>INT_MAX)return deny(out,Reason::raw);
        if(!duration(cambc::constants::capture_time) || !duration(cambc::constants::recall_time) ||
           !duration(cambc::constants::respawn_time) || cambc::constants::altar_lock_hits>time_limit ||
           !duration(cambc::constants::submit_gem_base_duration) ||
           !duration(cambc::constants::submit_gem_minimum_duration) ||
           !duration(cambc::constants::submit_gems_range) || cambc::constants::submit_gems_range>100 ||
           !scalar(cambc::constants::submit_gem_duration_multiplier) ||
           !scalar(cambc::constants::spawn_healing) || cambc::constants::spawn_healing<=0 ||
           cambc::constants::passive_gold>INT_MAX)return deny(out,Reason::raw);
        // A dead actor's sentinel position is not a forecast input. Preserve
        // the inherited dead-shopping branch once metadata is initialized.
        if(!me.is_alive())return true;
        const auto p=me.position();
        const bool supported=p.x>=0 && p.y>=0 && p.x<w && p.y<h && scalar(me.max_health()) && me.max_health()>0 &&
            std::isfinite(me.health()) && std::abs(double(me.health()))<=scalar_limit &&
            scalar(me.strength()) && scalar(me.focus());
        return supported?true:deny(out,Reason::raw);
    }
    // Called after ONLY PersonalInformation's layout stage, before its first
    // group reward cast or aggregation. Four member/item units per callback.
    bool advance_static(const plan3_runtime::PersonalInformation& personal) {
        if(unavailable_ || ready_)return ready_;
        try {
            if(!personal.initialization_layout)return false;
            const auto tiles=std::size_t(cambc::map::width())*std::size_t(cambc::map::height());
            if(occupied_.empty())occupied_.resize(tiles);
            if(personal.initialization_groups.size()>tiles || personal.spawn_positions.empty() ||
                personal.altars.empty() || personal.team_size<1 || personal.team_size>5) {unavailable_=true;return false;}
            unsigned work=0;
            while(group_<personal.initialization_groups.size() && work<4 && cambc::fuel_remaining()>=65000) {
                const auto& group=personal.initialization_groups[group_];
                if(member_==0) {
                    if(group.gem_value()>INT_MAX || group.gold_value()>INT_MAX ||
                       !duration(group.max_respawn_timer()) || group.camps().empty() || group.camps().size()>tiles) {
                        unavailable_=true;return false;
                    }
                }
                const auto& camp=group.camps()[member_];const auto s=camp.base_stats();const auto p=camp.spawn_position();
                if(p.x<0 || p.y<0 || p.x>=cambc::map::width() || p.y>=cambc::map::height() ||
                    !scalar(s.base_health) || !scalar(s.base_strength) || camp.attack_range()<0 || camp.attack_range()>10000) {
                    unavailable_=true;return false;
                }
                const auto tile=std::size_t(p.y)*std::size_t(cambc::map::width())+std::size_t(p.x);
                if(occupied_[tile]) {unavailable_=true;return false;}occupied_[tile]=1;
                ++work;++member_;
                if(member_==group.camps().size()) {
                    largest(group.gem_value(),largest_gem_,second_gem_);
                    largest(group.gold_value(),largest_gold_,second_gold_);
                    ++group_;member_=0;
                }
            }
            while(group_==personal.initialization_groups.size() && item_<items_.size() && work<4 && cambc::fuel_remaining()>=65000) {
                const auto type=cambc::item_type{static_cast<cambc::item_type::enum_type>(item_)};
                const auto info=type.info();
                const bool shop_item=type==cambc::item_type::monster_shard_of_strength ||
                    type==cambc::item_type::jungle_shard_of_health || type==cambc::item_type::power_shard_of_strength;
                if((shop_item && !info.cost) || info.health_points<0 || info.strength_points<0 || info.focus_points<0 ||
                   !std::isfinite(info.passive_magnitude) || info.passive_magnitude<0 || info.passive_magnitude>1) {
                    unavailable_=true;return false;
                }
                items_[item_]=info;++item_;++work;
            }
            if(group_==personal.initialization_groups.size() && item_==items_.size()) {
                if(largest_gem_+second_gem_>INT_MAX || largest_gold_+second_gold_>INT_MAX)unavailable_=true;
                else ready_=true;
            }
            return ready_;
        } catch(...) {unavailable_=true;throw;}
    }
    bool unknown_gem_opportunity(const plan3_runtime::PersonalInformation& p,unsigned) const {
        // Lifecycle omission from Q is not evidence of an eligible gem job.
        // This bridge skips NO individual forecast for numeric reasons: a
        // failed global preflight yields before the menu/gold gate entirely.
        // Preserve that unsupported-opportunity state without broadening the
        // existing complete-prefix gold rule to unseen whole-map stocks.
        return (!ready_ || unavailable_) && p.any_gems;
    }
    static bool views_supported(const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies,Report* out=nullptr) {
        if(out)*out={};
        if(camps.size()>10000 || enemies.size()>5)return deny(out,Reason::views);
        const auto live_ok=[](const auto& x) {
            const auto pos=x.position();
            return std::isfinite(x.health()) && std::abs(double(x.health()))<=scalar_limit && scalar(x.strength()) &&
                x.attack_range()>=0 && x.attack_range()<=10000 && pos.x>=0 && pos.y>=0 &&
                pos.x<cambc::map::width() && pos.y<cambc::map::height();
        };
        // Visible indices are flattened camps first, then enemies.
        int index=0;
        for(const auto& c:camps) {
            if(cambc::fuel_remaining()<45000)return deny(out,Reason::fuel_views,index);
            if(!live_ok(c))return deny(out,Reason::views,index);
            ++index;
        }
        for(const auto& e:enemies) {
            if(cambc::fuel_remaining()<45000)return deny(out,Reason::fuel_views,index);
            if(!live_ok(e))return deny(out,Reason::views,index);
            ++index;
        }
        return true;
    }
    bool forecast_supported(const plan3_runtime::PersonalInformation& p,
                            const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies,
                            bool views_validated=false,Report* out=nullptr) const {
        if(out)*out={};
        if(!ready_ || unavailable_)return deny(out,Reason::availability);
        if(!raw_supported(out))return false;
        const auto& me=cambc::self();
        if(!me.is_alive() || me.health()<0 || me.health()>me.max_health())return deny(out,Reason::numeric);
        const std::uint64_t bag=me.gem_count(),score=cambc::global_submitted_gems(cambc::team::ally);
        const auto full=bag+largest_gem_+second_gem_;
        if(full>INT_MAX || score+full>INT_MAX)return deny(out,Reason::numeric);
        const auto base=me.base_stats();
        if(!scalar(base.base_health) || !scalar(base.base_strength) || !scalar(base.base_focus) ||
           !scalar(base.health_growth) || !scalar(base.strength_growth) || !scalar(base.focus_growth) ||
           !scalar(base.camp_damage_multiplier) || base.camp_damage_multiplier<=0)return deny(out,Reason::numeric);
        double damage=base.camp_damage_multiplier,mitigation=1,capture=1;
        double health=base.base_health,strength=base.base_strength,focus=base.base_focus;
        std::array<bool,6> passives{};const auto counts=me.item_count();
        for(unsigned i=0;i<items_.size();++i) {
            if(cambc::fuel_remaining()<65000)return deny(out,Reason::fuel_items,int(i));
            const auto count=counts[cambc::item_type{static_cast<cambc::item_type::enum_type>(i)}];
            if(!count)continue;const auto& item=items_[i];
            // The immutable stats helper multiplies unsigned count by signed
            // points before conversion to floating point. Prove that product.
            if(std::uint64_t(count)*unsigned(item.health_points)>INT_MAX ||
               std::uint64_t(count)*unsigned(item.strength_points)>INT_MAX ||
               std::uint64_t(count)*unsigned(item.focus_points)>INT_MAX)return deny(out,Reason::numeric,int(i));
            health+=double(count)*item.health_points*base.health_growth;
            strength+=double(count)*item.strength_points*base.strength_growth;
            focus+=double(count)*item.focus_points*base.focus_growth;
            if(item.passive) {
                const auto k=static_cast<unsigned>(*item.passive);
                if(k>=passives.size())return deny(out,Reason::numeric,int(i));
                if(passives[k])continue;passives[k]=true;
                switch(*item.passive) {
                    case cambc::item_passive::monster:damage*=1+item.passive_magnitude;break;
                    case cambc::item_passive::jungle:case cambc::item_passive::durability:mitigation*=1-item.passive_magnitude;break;
                    case cambc::item_passive::treasure:capture+=item.passive_magnitude;break;
                    case cambc::item_passive::home:break;
                }
            }
        }
        if(!scalar(health) || !scalar(strength) || !scalar(focus) || !scalar(damage) || damage<=0 ||
           !scalar(mitigation) || !scalar(capture))return deny(out,Reason::numeric);
        // Cover both the pre-catalogue fallback and exact owned passive path.
        const double rate=std::max(1.0,(1.125*me.strength()+0.12*me.focus())*std::min(damage,double(base.camp_damage_multiplier)));
        double clear=0;
        if(p.observations.size()!=p.groups.size())return deny(out,Reason::availability);
        for(std::size_t i=0;i<p.groups.size();++i) {
            if(cambc::fuel_remaining()<65000)return deny(out,Reason::fuel_groups,int(i));
            const auto& d=p.groups[i];const auto& o=p.observations[i];
            if(!scalar(d.full_health) || !scalar(d.incoming) || !scalar(o.remaining_health) || !scalar(o.incoming) ||
               o.round>cambc::round_number() || !duration(o.respawn_remaining) || !duration(d.max_respawn_timer))return deny(out,Reason::numeric,int(i));
            const double turns=std::ceil(std::max(d.full_health,o.remaining_health)/rate);
            if(!std::isfinite(turns) || turns>time_limit || turns>INT_MAX-2)return deny(out,Reason::clearbounds,int(i));
            const double loss=std::max(d.incoming,o.incoming)*std::max(1.0,mitigation)*(turns+2);
            if(!std::isfinite(loss) || loss>scalar_limit)return deny(out,Reason::clearbounds,int(i));
            clear=std::max(clear,turns);
        }
        if(!views_validated && !views_supported(camps,enemies,out))return false;
        int lockIndex=0;
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy)) {
            if(cambc::fuel_remaining()<65000)return deny(out,Reason::fuel_locks,lockIndex);
            if(lock.hits_remaining()>time_limit)return deny(out,Reason::continuation,lockIndex);
            ++lockIndex;
        }
        const auto channel=full?plan3::tribute_completion_turns(int(full),cambc::constants::submit_gem_duration_multiplier,
            cambc::constants::submit_gem_base_duration,cambc::constants::submit_gem_minimum_duration):std::optional<int>{0};
        const double healing=std::ceil(1.0/cambc::constants::spawn_healing)+1;
        if(!channel || !std::isfinite(healing) || healing>time_limit ||
           !std::isfinite(cambc::constants::spawn_healing*me.max_health()) ||
           cambc::constants::spawn_healing*me.max_health()<=0)return deny(out,Reason::continuation);
        // All inherited acquisition/response/renewal expressions consist of
        // fewer than64 nonnegative terms bounded by these components. The
        // factor also covers two clears, waits, race/denial and return paths.
        const double component=double(cambc::map::width())*cambc::map::height()+clear+
            cambc::constants::capture_time+1+time_limit+cambc::constants::altar_lock_hits+1+
            cambc::constants::respawn_time+cambc::constants::recall_time+healing+*channel+10002;
        const double bound=64*component;
        if(!std::isfinite(bound) || bound>INT_MAX ||
           double(cambc::constants::passive_gold)*bound>INT_MAX)return deny(out,Reason::continuation);
        return true;
    }
};

inline JobKey key(const plan3::JobId& x) {return {int(x.kind),x.firstGroup,x.secondGroup,x.altarTile,x.equipmentBlock};}
inline plan3::JobId id(const JobKey& x) {return {plan3::JobKind(x[0]),x[1],x[2],x[3],x[4]};}
namespace detail {
// Borrow only during this call: the canonical menu and its const jobs stay
// unchanged and alive. No menu pointers or live observations are retained.
template<class Ready>
void map_canonical_menu(Input& input,const plan3_runtime::PersonalInformation& personal,const plan3::InformationState& state,
        const plan3::CandidateMenu& menu,bool submitting,bool prefix,Ready&& ready) {
    // Reuse capacity only. Old entries are unreachable after these counts reset;
    // every appended entry gets fresh defaults before any field is mapped.
    input.state=State::known;input.groupCount=0;input.jobCount=0;
    input.snapshotRound=state.round;input.observationRound=int(personal.observation_round);
    input.roundLimit=state.roundLimit;input.width=personal.width;input.height=cambc::map::height();
    input.carriedGems=state.carriedGems;input.ownPermanentScore=state.ownPermanentScore;
    input.winningGems=state.winningGems;input.submitting=submitting;input.candidatePrefix=prefix;
    input.candidatePrefix|=menu.truncated;
    for(const auto* job:menu.jobs) {
        if(!ready()) {input.state=State::cutoff;return;}
        if(input.jobCount>=maximum_jobs) {input.state=State::unavailable;return;}
        auto& out=input.jobs[input.jobCount++];out=JobInput{};out.key=key(job->id);
        out.complete=job->complete;out.routeKnown=job->routeKnown;out.fullClearAdmitted=job->fullClearAdmitted;
        if(!plan3::admitted(state,*job,false)) {out.state=State::unavailable;out.unavailableReason=Reason::incomplete;}
        out.sourceDomainValidated=true;out.renewalRequired=job->renewalRequired || job->mandatorySafety;
        out.renewalReturn=job->projectedRenewalAndReturnTime;
        const auto* n=job->nominal();out.nominalKnown=n!=nullptr;
        if(n) {out.nominalTau=n->tau;out.converted=n->deltaS;}
        for(const int groupKey:{job->id.firstGroup,job->id.secondGroup}) {
            if(groupKey<0 || group_index(input,groupKey)>=0)continue;
            if(input.groupCount>=maximum_groups) {input.state=State::unavailable;return;}
            auto& group=input.groups[input.groupCount++];group=GroupInput{};group.key=groupKey;group.state=State::unavailable;
            for(std::size_t i=0;i<personal.groups.size();++i) {
                if(i%8==0 && !ready()) {input.state=State::cutoff;return;}
                if(personal.groups[i].key!=groupKey)continue;
                group.gems=personal.groups[i].gem_value;const auto* o=personal.current(i);
                if(o && personal.observation_round==unsigned(state.round) && o->round==unsigned(state.round)) {
                    group.state=State::known;group.observedRound=int(o->round);
                    group.lifecycle=o->lifecycle==plan3_runtime::Lifecycle::alive?Lifecycle::alive:
                        o->lifecycle==plan3_runtime::Lifecycle::reward?Lifecycle::reward:Lifecycle::respawning;
                }
                break;
            }
        }
    }
}
} // namespace detail
template<class Ready>
Input map_menu(const plan3_runtime::PersonalInformation& personal,const plan3::InformationState& state,
        const std::vector<plan3::JobForecast>& jobs,bool submitting,bool prefix,Ready&& ready) {
    const auto menu=plan3::canonical_menu(jobs);
    Input input;detail::map_canonical_menu(input,personal,state,menu,submitting,prefix,ready);return input;
}
// Synthetic/pure fixture convenience only. The production paired selector
// below always supplies one live guard object through mapping AND evaluation.
inline Input map_menu(const plan3_runtime::PersonalInformation& personal,const plan3::InformationState& state,
        const std::vector<plan3::JobForecast>& jobs,bool submitting,bool prefix) {
    return map_menu(personal,state,jobs,submitting,prefix,[] {return true;});
}
struct Selection {
    plan3::JobIntent intent,companion;
    PairResult pair;
    bool priority=false;
};
template<class Ready>
Selection select_menu(const plan3_runtime::PersonalInformation& personal,const plan3::InformationState& state,
        const std::vector<plan3::JobForecast>& jobs,bool rate,bool submitting,bool prefix,
        const plan3::ControllerContext& context,bool unknown_gem,Ready&& ready,Input* storage=nullptr) {
    Selection result;const auto menu=plan3::canonical_menu(jobs);
    const auto selected=[&](const plan3::JobForecast& j,double value,plan3::DecisionReason why) {
        result.priority=true;result.intent={plan3::IntentStatus::selected,j.id,value,why,false,menu.truncated};
        result.companion=result.intent;return result;
    };
    if(context.protectedChannelOrCapture && !context.sharedAbort) {
        result.priority=true;result.intent={plan3::IntentStatus::preserve,context.activeJob,0,plan3::DecisionReason::protected_obligation};
        result.companion=result.intent;return result;
    }
    const plan3::JobForecast* winning=nullptr;const plan3::JobForecast* gold=nullptr;
    double gold_rate=-1;bool gem=unknown_gem;
    for(const auto* j:menu.jobs)if(plan3::admitted(state,*j,false)) {
        if(context.newlyFeasibleWinningBank && state.carriedGems>=std::max(0,state.winningGems-state.ownPermanentScore) &&
           j->id.kind==plan3::JobKind::bank && j->nominal()->deltaS>=state.winningGems-state.ownPermanentScore &&
           (!winning || j->nominal()->tau<winning->nominal()->tau))winning=j;
    }
    if(winning)return selected(*winning,double(winning->nominal()->deltaS),plan3::DecisionReason::immediate_winning_conversion);
    for(const auto* j:menu.jobs)if(plan3::admitted(state,*j,false)) {
        if(j->mandatorySafety)return selected(*j,0,plan3::DecisionReason::mandatory_recovery);
        gem|=j->nominal()->deltaS>0;
        if(state.carriedGems==0 && j->rewardOnlyEndpoint && plan3::productive(j->id.kind) && j->campGold>0 && !j->nominal()->deltaS) {
            const double value=double(j->campGold)/std::max(1,j->nominal()->tau+j->projectedRenewalAndReturnTime);
            if(!gold || plan3::strictly_better(value,gold_rate)) {gold=j;gold_rate=value;}
        }
    }
    if(!gem && gold)return selected(*gold,gold_rate,plan3::DecisionReason::gold_fallback);
    const auto paired=[&](Input& input) {
        detail::map_canonical_menu(input,personal,state,menu,submitting,prefix,ready);
        return evaluate(input,ready);
    };
    if(storage)result.pair=paired(*storage);
    else {Input input;result.pair=paired(input);}
    if(result.pair.state!=State::known)return result;
    const auto convert=[&](const Choice& choice) {
        plan3::JobIntent out;
        if(choice.present)out={plan3::IntentStatus::selected,id(choice.key),
            double(choice.score.numerator)/choice.score.denominator,plan3::DecisionReason::objective,false,prefix};
        return out;
    };
    const auto active=convert(result.pair.active.present?result.pair.active:result.pair.requiredRenew);
    const auto control=convert(result.pair.control.present?result.pair.control:result.pair.requiredRenew);
    result.intent=rate?control:active;result.companion=rate?active:control;return result;
}
} // namespace plan3_backpressure
