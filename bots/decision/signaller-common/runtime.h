#pragma once
#include "../common/runtime.h"
#include "policy.h"

namespace plan3_signaller {
struct Runtime {
    Planner planner;
    plan3_runtime::Campaign* campaign=nullptr;
    bool governingControl=false,geometryLoaded=false,dirty=false;
    std::array<bool,2> reported{};
    explicit Runtime(bool control):planner(control),governingControl(control) {}
};
inline Geometry public_geometry() {
    Geometry geometry;geometry.width=cambc::map::width();geometry.height=cambc::map::height();
    if(geometry.width<=0 || geometry.height<=0 || std::int64_t(geometry.width)*geometry.height>INT32_MAX)return {};
    // Public cached immutable masks. Bounded extraction rejects, never truncates.
    std::array<cambc::vec2,256> positions;
    for(auto side:{cambc::team::ally,cambc::team::enemy}) {
        const auto& spawn=cambc::map::spawn_region(side);const auto count=spawn.count();
        if(!count || count>positions.size())return {};
        if(spawn.positions_into(std::span<cambc::vec2>(positions.data(),count))!=count)return {};
        int minimum=INT32_MAX;
        for(unsigned i=0;i<count;++i) {
            const auto p=positions[i];
            if(p.x<0 || p.y<0 || p.x>=geometry.width || p.y>=geometry.height)return {};
            minimum=std::min(minimum,p.y*geometry.width+p.x);
        }
        if(side==cambc::team::ally)geometry.ownSpawn=minimum;else geometry.enemySpawn=minimum;
    }
    return geometry;
}
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    auto selected=runtime.planner.preview(control,info,jobs,context,[] {return cambc::fuel_remaining()>=45000;});
    if((!runtime.reported[control] || (runtime.dirty && control==runtime.governingControl)) && cambc::fuel_remaining()>=65000) {
        const auto& stats=runtime.planner.statistics();plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"signaller\",\"arm\":"<<control<<",\"state\":"<<int(selected.state)
            <<",\"signal\":"<<int(selected.signal)<<",\"omitted\":"<<selected.omitted
            <<",\"episodes\":"<<stats.committed<<",\"targetward_openings\":"<<stats.targetwardOpenings
            <<",\"motion_unknown\":"<<stats.motionUnknown<<",\"unwitnessed_endpoints\":"<<stats.unwitnessedEndpoints
            <<",\"opponent_visibility\":\"unknown\",\"reaction_model_assumed\":true,\"equilibrium_claim\":false";
        runtime.campaign->report_policy(fields.view());runtime.reported[control]=true;
        if(control==runtime.governingControl)runtime.dirty=false;
    }
    if(selected.state!=State::known)return {};
    selected.intent.policyToken=selected.token;return selected.intent;
}
inline void observer(void* opaque,const plan3::PolicyEvent& event) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    if(!runtime.planner.observe(event))throw std::logic_error("Signaller job feedback mismatch");
    if(event.token && event.kind!=plan3::PolicyEvent::Kind::committed)runtime.dirty=true;
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    Runtime runtime(control);runtime.campaign=&campaign;
    campaign.supply_selector(select,&runtime,false,observer);
    while(true) {
        try {
            if(cambc::is_draft())plan3_runtime::fixed_draft();
            else {
                if(!runtime.geometryLoaded && cambc::fuel_remaining()>=65000) {
                    runtime.planner.geometry(public_geometry());runtime.geometryLoaded=true;
                }
                unsigned beforeRound=0;auto& me=cambc::self();Motion motion;
                std::optional<cambc::vec2> before;
                if(cambc::fuel_remaining()>=65000) {
                    beforeRound=cambc::round_number();motion.round=static_cast<int>(beforeRound);
                    motion.aliveBefore=me.is_alive();if(motion.aliveBefore)before=me.position();
                }
                campaign.turn();
                if(before && cambc::fuel_remaining()>=45000) {
                    motion.aliveAfter=me.is_alive();
                    const auto after=motion.aliveAfter?std::optional<cambc::vec2>{me.position()}:std::nullopt;
                    const auto width=cambc::map::width();
                    motion.valid=after && beforeRound<=INT32_MAX && cambc::round_number()==beforeRound;
                    if(motion.valid) {motion.before=before->y*width+before->x;motion.after=after->y*width+after->x;}
                }
                runtime.planner.motion(motion);
            }
        } catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_signaller
