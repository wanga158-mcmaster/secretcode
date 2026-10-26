#pragma once
#include "adapter.h"
#include "../common/two-trip-menu.h"

namespace plan3_indexer {
// Static preparation belongs to each retained physical project. Live records
// are owned by one choose callback and consumed by all three rankings together.
class MacroMenu {
    using TT=plan3_runtime::two_trip::Status;
    std::array<plan3_runtime::two_trip::Menu,maximum_projects> current_;
    std::array<plan3_runtime::two_trip::FreshServiceBuilder,maximum_projects> fresh_;
public:
    struct Preparation {
        unsigned count=0;
        std::array<int,maximum_projects> indices{};
        std::array<bool,maximum_projects> macroSlots{};
        std::array<std::optional<plan3_runtime::two_trip::FreshService>,maximum_projects> fresh;
        ServiceOverrides services;
        bool cutoff=false,unknown=false;
        bool macro_slot(int index) const {
            for(unsigned i=0;i<count;++i)if(indices[i]==index)return macroSlots[i];
            return false;
        }
        bool macro_unknown(int index) const {
            for(unsigned i=0;i<count;++i)if(indices[i]==index)return services[i].currentUnknown;
            return false;
        }
    };
    template<class Ready>
    void advance_preparation(plan3_runtime::StaticPaths& paths,Ready&& enough) {
        for(unsigned i=0;i<maximum_projects;++i) {
            fresh_[i].advance_preparation(paths,enough);
            current_[i].advance_preparation(paths,enough);
        }
    }
    template<class Ready>
    std::unique_ptr<Preparation> prepare(const plan3_runtime::ForecastContext& c,plan3_runtime::StaticPaths& paths,const Universe& family,
            const std::optional<plan3_runtime::EquipmentStats>& stats,Ready&& enough) {
        auto out=std::make_unique<Preparation>();
        for(auto& menu:current_)menu.discard_candidate();
        if(&c.paths!=&paths) {out->cutoff=true;return out;}
        if(!stats || !family.ready || c.info.carriedGems ||
           c.info.actorOrdinal!=int(cambc::champion_type::tavi))return out;
        out->count=family.count;
        for(unsigned at=0;at<family.count;++at) {
            out->indices[at]=family.indices[at];
            auto& service=out->services[at];service.key=family.keys[at];
            const auto index=std::size_t(family.indices[at]);
            const auto& observed=c.personal.observations[index];
            const bool current=c.personal.observation_round==unsigned(c.info.round) && observed.lifecycle &&
                observed.round==unsigned(c.info.round);
            const bool countdown=c.personal.observation_round==unsigned(c.info.round) &&
                observed.lifecycle==plan3_runtime::Lifecycle::respawning && observed.round<unsigned(c.info.round) &&
                std::int64_t(observed.respawn_remaining)>std::int64_t(c.info.round)-observed.round;
            if(!current && !countdown)continue;
            if(!enough()) {out->cutoff=true;return out;}
            // This one-way gate is exactly acquisition()'s ordinary full-clear
            // admission. A rejected ordinary Single is not constructed again.
            const bool ordinary=c.clear(index,c.info.health).admitted;
            if(!enough() || c.cutoff) {out->cutoff=true;return out;}
            const bool healthy=c.clear(index,c.info.maximumHealth,0).admitted;
            if(c.cutoff) {out->cutoff=true;return out;}
            TT macro_status=TT::unsupported;
            if(!ordinary) {
                out->macroSlots[at]=true;
                plan3_runtime::two_trip::Menu::Prefix prefix;
                prefix.groups.emplace_back(-1,int(index));
                const auto status=current_[at].prepare_early(c.personal,stats,paths,c.info,prefix,enough);
                macro_status=status;
                if(status==TT::cutoff) {out->cutoff=true;return out;}
                service.currentUnknown=status==TT::pending || status==TT::unknown;
                out->unknown|=service.currentUnknown;
            }
            if(!healthy && (ordinary || macro_status==TT::ready)) {
                const auto fresh=fresh_[at].prepare(c.personal,index,*stats,paths,c.info,enough,
                    current_[at].prepared_candidate());
                service.status=fresh.status;
                if(fresh.status==TT::cutoff) {out->cutoff=true;return out;}
                out->fresh[at]=fresh.service;
            } else service.status=healthy?TT::unsupported:macro_status;
        }
        return out;
    }
    void merge(const plan3::InformationState& state,std::vector<plan3::JobForecast>& jobs,Preparation& prepared) {
        for(unsigned at=0;at<prepared.count;++at) {
            auto& row=prepared.services[at];
            if(prepared.macroSlots[at]) {
                const auto status=current_[at].merge(state,jobs);
                if(status==TT::cutoff) {prepared.cutoff=true;return;}
            }
            if(!prepared.fresh[at])continue;
            for(auto& job:jobs)if(job.id.kind==plan3::JobKind::cycle && job.id.firstGroup==row.key && job.id.secondGroup<0) {
                auto bound=plan3_runtime::two_trip::bind_current(*prepared.fresh[at],job,state);
                row.status=bound.status;row.service=std::move(bound.service);
                if(row.service) {
                    job.projectedRenewalAndReturnTime=row.service->Rcurrent;
                    const auto* nominal=job.nominal();
                    for(auto& outcome:job.outcomes)if(&outcome==nominal)
                        outcome.terminal.preparationTurns=row.service->Rcurrent;
                }
                break;
            }
        }
    }
    bool owns(const plan3::JobId& id) const {
        for(const auto& menu:current_)if(menu.owns(id))return true;
        return false;
    }
    std::unique_ptr<plan3_runtime::two_trip::Plan> take(const plan3::JobId& id) {
        for(auto& menu:current_)if(menu.owns(id))return menu.take(id);
        return nullptr;
    }
};
} // namespace plan3_indexer
