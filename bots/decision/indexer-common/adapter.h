#pragma once
#include "numeric-domain.h"
#include "projects.h"
#include "../common/two-trip-service.h"

namespace plan3_indexer {
// Immutable physical family and actual-history cursor only. No forecast or
// selected result survives a callback. Initialization follows sorted public data.
struct Universe {
    std::array<int,maximum_projects> keys{},indices{};
    unsigned count=0;
    std::size_t preparation=0;
    int cursor=-1;
    bool ready=false;
    template<class Ready> bool advance(const plan3_runtime::PersonalInformation& p,Ready&& enough) {
        if(ready)return true;
        unsigned work=0;
        while(preparation<p.groups.size() && count<maximum_projects && work++<32) {
            if(!enough())return false;
            const auto i=preparation++;
            if(p.groups[i].gem_value>0) { keys[count]=p.groups[i].key;indices[count++]=int(i); }
        }
        ready=count==maximum_projects || preparation==p.groups.size();
        if(ready && count && cursor<0)cursor=keys[0];
        return ready;
    }
    bool contains(int key) const {
        for(unsigned i=0;i<count;++i)if(keys[i]==key)return true;
        return false;
    }
    int cursor_key() const {return cursor;}
    Input physical(int round,int observation,int limit,int width,int height,std::int64_t bag=0) const {
        Input out;out.round=round;out.observationRound=observation;out.roundLimit=limit;
        out.width=width;out.height=height;out.carriedBag=bag;
        out.physicalCount=count;out.physicalKeys=keys;out.physicalCursor=cursor;return out;
    }
};
struct Geometry {
    int originTile=-1,endpointTile=-1,groupField=-1,altarField=-1,spawnField=-1;
    int currentEndpointTile=-1;
};
struct Mapped {
    Input input;
    std::array<Geometry,maximum_projects> geometry{};
};
struct ServiceOverride {
    int key=-1;
    plan3_runtime::two_trip::Status status=plan3_runtime::two_trip::Status::unknown;
    bool currentUnknown=false;
    std::optional<plan3_runtime::two_trip::PeriodService> service;
};
using ServiceOverrides=std::array<ServiceOverride,maximum_projects>;
// Fresh full-health service uses the same direct group/altar/tie arithmetic as
// immutable17 loops(), without its global rate pruning or a second acquisition.
template<class Ready>
Mapped map_menu(const plan3_runtime::ForecastContext& c,const Universe& family,
                const std::vector<plan3::JobForecast>& jobs,Ready&& enough,
                const ServiceOverrides* services=nullptr) {
    const auto& p=c.personal;const auto origin=cambc::self().position();
    Mapped mapped;mapped.input=family.physical(c.info.round,int(p.observation_round),c.info.roundLimit,
        p.width,cambc::map::height(),c.info.carriedGems);
    mapped.input.projectCount=family.count;
    if(!family.ready || c.cutoff) {mapped.input.cutoff=true;return mapped;}
    for(unsigned at=0;at<family.count;++at) {
        auto& out=mapped.input.projects[at];out.key=family.keys[at];out.state=State::unknown;
        if(!enough()) {mapped.input.cutoff=true;return mapped;}
        const auto i=std::size_t(family.indices[at]);const auto& group=p.groups[i];
        const ServiceOverride* service=nullptr;
        if(services)for(const auto& entry:*services)if(entry.key==out.key) {service=&entry;break;}
        if(service && service->status==plan3_runtime::two_trip::Status::cutoff) {
            mapped.input.cutoff=true;return mapped;
        }
        const auto& observed=p.observations[i];
        // An expired remembered countdown is not a freshly observed arrival.
        const bool current=p.observation_round==unsigned(c.info.round) && observed.lifecycle &&
            observed.round==unsigned(c.info.round);
        const bool countdown=p.observation_round==unsigned(c.info.round) && observed.lifecycle==plan3_runtime::Lifecycle::respawning &&
            observed.round<unsigned(c.info.round) && std::int64_t(observed.respawn_remaining)>
                std::int64_t(c.info.round)-observed.round;
        if(!current && !countdown)continue;
        const plan3::JobForecast* job=nullptr;
        for(const auto& j:jobs)if(j.id.kind==plan3::JobKind::cycle && j.id.firstGroup==out.key) {job=&j;break;}
        if(!job) {out.state=State::unavailable;continue;}
        out.currentComplete=job->complete;out.currentAdmitted=plan3::admitted(c.info,*job,false);
        out.routeKnown=job->routeKnown;out.recoveryKnown=job->projectedRenewalAndReturnTime>=0;
        const auto* n=job->nominal();
        if(!n || !out.currentComplete || !out.currentAdmitted || !out.routeKnown) {
            out.state=service && service->currentUnknown?State::unknown:
                (job->complete?State::unavailable:State::unknown);continue;
        }
        if(n->deltaS!=group.gem_value || c.info.carriedGems!=0) {out.state=State::unavailable;continue;}
        out.tau=n->tau;out.G=group.gem_value;out.Rcurrent=job->projectedRenewalAndReturnTime;
        out.regenMax=group.max_respawn_timer;
        const auto fresh=c.clear(i,c.info.maximumHealth,0);out.freshClearAdmitted=fresh.admitted;
        if(!fresh.admitted) {
            if(!service || !service->service) {
                out.state=service && service->status!=plan3_runtime::two_trip::Status::unsupported?
                    State::unknown:State::unavailable;
                continue;
            }
            const auto& s=*service->service;
            const auto* nominal=job->nominal();
            if(service->status!=plan3_runtime::two_trip::Status::ready ||
               s.currentJob!=job->id || s.preparedRound!=unsigned(c.info.round) ||
               s.decisionOrigin!=origin || s.groupKey!=group.key || s.groupGems!=group.gem_value ||
               s.regenMax!=group.max_respawn_timer || !nominal ||
               s.currentEndpoint.y*p.width+s.currentEndpoint.x!=nominal->terminal.position.tile ||
               s.Rcurrent!=job->projectedRenewalAndReturnTime ||
               cambc::round_number()!=unsigned(c.info.round)) {
                out.state=State::unknown;continue;
            }
            out.freshClearAdmitted=true;out.recoveryKnown=true;
            out.Cfresh=s.Cfresh;out.Rfresh=s.Rfresh;out.Rcurrent=s.Rcurrent;
            int altar_field=-1;
            for(std::size_t a=0;a<p.altars.size();++a)
                if(p.altars[a].y*p.width+p.altars[a].x==s.altarTile)altar_field=p.altar_fields[a];
            mapped.geometry[at]={origin.y*p.width+origin.x,s.endpoint.y*p.width+s.endpoint.x,
                group.field,altar_field,p.spawn_field,nominal->terminal.position.tile};
            out.state=State::known;continue;
        }
        const int travel=c.paths.get(group.field,origin);
        const int return_to_origin=c.paths.get(p.spawn_field,origin);
        if(travel<0 || return_to_origin<0) {out.state=travel==plan3_runtime::StaticPaths::unknown || return_to_origin==plan3_runtime::StaticPaths::unknown?State::unknown:State::unavailable;continue;}
        if(!enough()) {mapped.input.cutoff=true;return mapped;}
        const int altar=c.altar_for(group.field,group.gem_value);
        if(altar<0) {out.state=State::unknown;continue;}
        const int back=c.paths.minimum_between(p.altar_fields[std::size_t(altar)],group.field);
        if(back<0) {out.state=back==plan3_runtime::StaticPaths::unknown?State::unknown:State::unavailable;continue;}
        const auto endpoint=c.altar_endpoint(altar,group.field);
        const int to_spawn=c.paths.get(p.spawn_field,endpoint);
        if(c.cutoff || to_spawn<0) {out.state=State::unknown;continue;}
        const int channel=c.tribute(group.gem_value);
        if(channel<0) {out.state=State::unavailable;continue;}
        out.Cfresh=std::int64_t(travel)+fresh.turns+cambc::constants::capture_time+1+
            back+channel+c.lock_delay(altar);
        out.Rfresh=std::int64_t(c.recall)+c.healing_turns(c.info.maximumHealth-fresh.healthLoss)+return_to_origin;
        mapped.geometry[at]={origin.y*p.width+origin.x,endpoint.y*p.width+endpoint.x,
            group.field,p.altar_fields[std::size_t(altar)],p.spawn_field,n->terminal.position.tile};
        out.state=State::known;
    }
    if(c.cutoff)mapped.input.cutoff=true;
    return mapped;
}
struct Selection {
    plan3::JobIntent intent,companion;
    Input input;
    Triple triple;
    std::array<Geometry,maximum_projects> geometry{};
    bool priority=false,bootstrap=false;
};
// This is metadata about the ONE selected common menu, never every unseen
// public camp. A full-fresh unsafe surrogate cannot settle an unknown live
// lifecycle: it could legally turn out to be a free reward. Current unsafe or
// known-unreachable jobs remain known rejections. Past positive respawn evidence
// supports a countdown; its expiry is not evidence of arrival.
template<class Ready>
std::optional<bool> unknown_gem_for_menu(const plan3_runtime::ForecastContext& c,
        const std::vector<plan3::JobForecast>& jobs,Ready&& enough) {
    if(c.cutoff && c.personal.any_gems)return true;
    const auto& p=c.personal;
    for(const auto& job:jobs)if(job.id.kind==plan3::JobKind::cycle) {
        if(!enough())return std::nullopt;
        const auto found=std::lower_bound(p.groups.begin(),p.groups.end(),job.id.firstGroup,
            [](const auto& group,int key){return group.key<key;});
        if(found==p.groups.end() || found->key!=job.id.firstGroup || found->gem_value<=0)continue;
        const auto origin=cambc::self().position();
        const int distance=c.paths.get(found->field,origin);
        if(distance==plan3_runtime::StaticPaths::unreachable)continue;
        const auto i=std::size_t(found-p.groups.begin());const auto& o=p.observations[i];
        const bool current=p.observation_round==unsigned(c.info.round) && o.lifecycle && o.round==unsigned(c.info.round);
        const bool countdown=p.observation_round==unsigned(c.info.round) &&
            o.lifecycle==plan3_runtime::Lifecycle::respawning && o.round<unsigned(c.info.round) &&
            std::int64_t(o.respawn_remaining)>std::int64_t(c.info.round)-o.round;
        if(!current && !countdown)return true;
        // A current independently unsafe clear is already rejected, regardless
        // of pending geometry. A safe reward/clear with unfinished required
        // travel, altar or recovery support is still an unknown opportunity.
        if(!job.fullClearAdmitted)continue;
        if(distance==plan3_runtime::StaticPaths::unknown || !job.complete)return true;
        if(!job.routeKnown) {
            if(c.paths.get(p.spawn_field,origin)==plan3_runtime::StaticPaths::unknown)return true;
            for(const int field:p.altar_fields)if(!c.paths.complete(field))return true;
        }
    }
    return false;
}
template<class Ready>
Selection select_menu(const plan3_runtime::ForecastContext& c,const Universe& family,
        const std::vector<plan3::JobForecast>& jobs,unsigned mode,
        const plan3::ControllerContext& context,bool unknown_gem,Ready&& enough,
        bool opening_unfinished=false,const ServiceOverrides* services=nullptr) {
    Selection result;const auto menu=plan3::canonical_menu(jobs);const auto& state=c.info;
    const auto chosen=[&](const plan3::JobForecast& j,double value,plan3::DecisionReason why) {
        result.priority=true;result.intent={plan3::IntentStatus::selected,j.id,value,why,false,menu.truncated};
        result.companion=result.intent;return result;
    };
    if(context.protectedChannelOrCapture && !context.sharedAbort) {
        result.priority=true;result.intent={plan3::IntentStatus::preserve,context.activeJob,0,plan3::DecisionReason::protected_obligation};
        result.companion=result.intent;return result;
    }
    const plan3::JobForecast* winning=nullptr;const plan3::JobForecast* bank=nullptr;const plan3::JobForecast* renewal=nullptr;
    const plan3::JobForecast* gold=nullptr;double gold_rate=-1;bool gem=unknown_gem;
    bool service_cutoff=false;
    if(services)for(const auto& entry:*services)if(family.contains(entry.key)) {
        gem|=entry.currentUnknown;
        service_cutoff|=entry.status==plan3_runtime::two_trip::Status::cutoff;
    }
    const plan3::JobForecast* bootstrap_gold=nullptr;double bootstrap_rate=-1;
    for(const auto* j:menu.jobs) {
        if(!enough()) {result.triple.state=State::cutoff;result.triple.reason=Reason::cutoff;return result;}
        if(!plan3::admitted(state,*j,false))continue;
        const auto earlier=[&](const plan3::JobForecast* old) {return !old || j->nominal()->tau<old->nominal()->tau ||
            (j->nominal()->tau==old->nominal()->tau && j->id<old->id);};
        if(j->id.kind==plan3::JobKind::bank) {
            if(earlier(bank))bank=j;
            if(context.newlyFeasibleWinningBank && state.carriedGems>=std::max(0,state.winningGems-state.ownPermanentScore) &&
                j->nominal()->deltaS>=state.winningGems-state.ownPermanentScore && earlier(winning))winning=j;
        }
    }
    if(winning)return chosen(*winning,double(winning->nominal()->deltaS),plan3::DecisionReason::immediate_winning_conversion);
    for(const auto* j:menu.jobs)if(plan3::admitted(state,*j,false)) {
        if(j->mandatorySafety)return chosen(*j,0,plan3::DecisionReason::mandatory_recovery);
        if(j->id.kind==plan3::JobKind::renew && (!renewal || j->id<renewal->id))renewal=j;
        gem|=j->nominal()->deltaS>0;
        if(state.carriedGems==0 && j->rewardOnlyEndpoint && plan3::productive(j->id.kind) && j->campGold>0 && !j->nominal()->deltaS) {
            const double value=double(j->campGold)/std::max(1,j->nominal()->tau+j->projectedRenewalAndReturnTime);
            if(!gold || plan3::strictly_better(value,gold_rate)) {gold=j;gold_rate=value;}
            // The common executor replans non-Options Doubles at their first
            // acquisition milestone. Bootstrap funding uses one complete Cycle.
            if(j->id.kind==plan3::JobKind::cycle && j->id.secondGroup<0 &&
               (!bootstrap_gold || plan3::strictly_better(value,bootstrap_rate))) {
                bootstrap_gold=j;bootstrap_rate=value;
            }
        }
    }
    // Fresh nonempty-bag boundaries have one common conversion prefix.
    if(state.carriedGems>0) {
        if(bank)return chosen(*bank,double(bank->nominal()->deltaS)/std::max(1,bank->nominal()->tau),plan3::DecisionReason::objective);
        if(renewal)return chosen(*renewal,0,plan3::DecisionReason::mandatory_recovery);
        result.priority=true;return result;
    }
    // A hard selected-single construction cutoff is not a completed common
    // opportunity menu, including on monolith-only or entirely gold-only maps.
    // Optional Double floor stops use MenuAudit::double_prefix instead, so a
    // completed single-project family can still publish its entire triple.
    if(c.cutoff || service_cutoff) {result.triple.state=State::cutoff;result.triple.reason=Reason::cutoff;return result;}
    if(!gem && gold) {
        const auto unknown=unknown_gem_for_menu(c,jobs,enough);
        if(!unknown) {result.triple.state=State::cutoff;result.triple.reason=Reason::cutoff;return result;}
        // Unfinished openings continue through the same complete project
        // evaluation below, including on known-gold-only maps, so selecting
        // funding work also records its eventual shop-return obligation.
        if(!*unknown && !opening_unfinished)
            return chosen(*gold,gold_rate,plan3::DecisionReason::gold_fallback);
    }
    if(state.horizon()==0) {
        if(renewal)return chosen(*renewal,0,plan3::DecisionReason::mandatory_recovery);
        result.priority=true;return result;
    }
    auto mapped=map_menu(c,family,jobs,enough,services);result.input=std::move(mapped.input);result.geometry=mapped.geometry;
    result.triple=evaluate(result.input,enough);
    if(result.triple.state!=State::known)return result;
    // Unknown projects remain unknown. When none can currently be serviced,
    // the common fixed opening can still earn capacity from admitted gold work.
    // This is a shared bootstrap prefix, not evidence that unseen gems are empty.
    // Preserve the complete project table rather than replacing it with a
    // default Selection, so its unknown/rejection coverage remains available.
    if(!result.triple.eligibleCount && opening_unfinished && bootstrap_gold) {
        result.priority=true;result.bootstrap=true;
        result.intent={plan3::IntentStatus::selected,bootstrap_gold->id,bootstrap_rate,
            plan3::DecisionReason::gold_fallback,false,menu.truncated};
        result.companion=result.intent;
        return result;
    }
    const auto convert=[&](const Choice& choice) {
        plan3::JobIntent out;
        if(choice.present)for(const auto& j:jobs)if(j.id.kind==plan3::JobKind::cycle && j.id.firstGroup==choice.key) {
            out={plan3::IntentStatus::selected,j.id,double(choice.score.numerator)/choice.score.denominator,
                plan3::DecisionReason::objective,false,menu.truncated};break;
        }
        return out;
    };
    const auto a=convert(result.triple.active),r=convert(result.triple.rate),circuit=convert(result.triple.circuit);
    result.intent=mode==1?r:mode==2?circuit:a;result.companion=mode==0?r:a;
    if(!result.intent.job && renewal)return chosen(*renewal,0,plan3::DecisionReason::mandatory_recovery);
    return result;
}
} // namespace plan3_indexer
