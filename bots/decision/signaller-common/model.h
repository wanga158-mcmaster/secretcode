#pragma once
#include "signals.h"
#include "../commitment-common/model.h"

namespace plan3_signaller {
struct Geometry {int width=0,height=0,ownSpawn=-1,enemySpawn=-1;};
struct Model {
    unsigned units=1024,contestedMargin=2;
    bool assumeObserved=true;
    // All probabilities are declared hypotheses, not fitted responses.
    Weights common{8,4,2,2};
    std::array<Weights,4> reaction{Weights{8,4,2,2},Weights{12,2,1,1},Weights{4,6,4,2},Weights{2,4,8,2}};
};
struct Mapping {
    State state=State::unavailable;
    Input input;
    unsigned canonical=0,omittedUnknown=0;
    bool truncated=false;
};
inline bool valid(const Geometry& geometry) {
    const auto area=std::int64_t(geometry.width)*geometry.height;
    return geometry.width>0 && geometry.height>0 && area<=INT32_MAX &&
        geometry.ownSpawn>=0 && geometry.enemySpawn>=0 && geometry.ownSpawn<area && geometry.enemySpawn<area &&
        geometry.ownSpawn!=geometry.enemySpawn;
}
inline int distance(int a,int b,const Geometry& geometry) {
    return std::max(std::abs(a%geometry.width-b%geometry.width),std::abs(a/geometry.width-b/geometry.width));
}
inline Signal classify(int target,const Geometry& geometry,unsigned margin) {
    const auto own=std::int64_t(distance(target,geometry.ownSpawn,geometry));
    const auto enemy=std::int64_t(distance(target,geometry.enemySpawn,geometry));
    if(own+margin<enemy)return Signal::home;
    if(enemy+margin<own)return Signal::forward;
    return Signal::contested;
}
inline Key key(const plan3::JobId& id) {return plan3_commitment::key(id);}
inline plan3::JobId job_id(const Key& key) {return plan3_commitment::job_id(key);}
template<class Ready>
Mapping map(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
            const Geometry& geometry,const Model& model,Ready&& ready) {
    Mapping result;
    const auto stop=[](State state) {Mapping empty;empty.state=state;return empty;};
    if(!ready())return stop(State::cutoff);
    if(!valid(geometry) || !model.units || model.units>1000000 || model.contestedMargin>100 ||
       !mass(model.common) || info.position.tile<0 ||
       info.position.tile>=std::int64_t(geometry.width)*geometry.height || info.groups.size()>4096)
        return stop(State::invalid);
    for(const auto& weights:model.reaction)if(!mass(weights))return stop(State::invalid);
    plan3_commitment::Model own;own.units=model.units;own.effortGems={0,0,0,0};own.switchGems=0;
    // Consume ONLY the existing own-Y/lifecycle projection. Its follower
    // columns and switch terms play no role in this signal model.
    const auto projected=plan3_commitment::map(info,jobs,own,std::nullopt,ready);
    if(projected.state!=plan3_commitment::State::known)
        return stop(projected.state==plan3_commitment::State::cutoff?State::cutoff:State::unavailable);
    result.canonical=projected.canonical;result.omittedUnknown=projected.omittedUnknown;result.truncated=projected.truncated;
    result.input.common=model.common;
    for(unsigned j=0;j<projected.input.jobCount;++j) {
        if(!ready())return stop(State::cutoff);
        const auto& source=projected.input.jobs[j];Row row;row.key=source.key;row.known=true;
        const auto kind=static_cast<plan3::JobKind>(row.key[0]);
        if(plan3::productive(kind)) {
            bool supported=true;
            for(unsigned g=1;g<=unsigned(kind==plan3::JobKind::double_cycle?2:1);++g) {
                const int target=row.key[g];unsigned found=0;
                if(target<0 || target>=std::int64_t(geometry.width)*geometry.height) {supported=false;break;}
                for(const auto& group:info.groups) {
                    if(!ready())return stop(State::cutoff);
                    if(group.minimumSpawnTile==target)++found;
                }
                if(found!=1)supported=false;
            }
            if(!supported) {++result.omittedUnknown;continue;}
            row.signal=classify(row.key[1],geometry,model.contestedMargin);
        }
        for(unsigned r=0;r<4;++r)row.ownOutput[r]=source.cells[r].leader;
        row.response=model.assumeObserved && row.signal!=Signal::none?
            model.reaction[static_cast<unsigned>(row.signal)]:model.common;
        // No unpriced message action: complete-job Y already contains the
        // executor's movement, combat, capture and conversion occupation.
        row.additionalCost=0;
        result.input.jobs[result.input.jobCount++]=row;
    }
    result.state=State::known;return result;
}
inline Mapping map(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                   const Geometry& geometry,const Model& model={}) {
    return map(info,jobs,geometry,model,[] {return true;});
}
} // namespace plan3_signaller
