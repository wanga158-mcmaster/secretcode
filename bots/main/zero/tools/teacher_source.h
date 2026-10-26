#pragma once
// Fixed five-action SDK validation only. Source data belongs to the separate oracle producer.
// This diagnostic never calls a neural network, supplies engineering masks or emits learning frames.
#include <cstddef>

namespace zero_source_probe {
struct Step {
    std::int32_t sequence,input,raw_kind,type,kind,target,count,x,y,target_kind,target_id,scalar_known;
    std::int64_t scalar;
};
struct Control {
    char magic[16];
    std::int32_t count,stage,failed,completed,current,source_status,build_status,selected,priority_parity;
    std::uint64_t observation_fuel,argument_fuel,dispatch_fuel,refresh_fuel;
    Step steps[5];
};
struct Observation {
    std::int32_t input,command_index,actor,type,x,y,gold,gems,cd0,cd1,state,channel,remaining,next_priority;
    float hp,shield;
    std::uint32_t allies,allies_n,enemies,enemies_n,camps,camps_n,unit_stride,camp_stride,unit_id_offset,camp_id_offset;
};
inline constexpr char magic[16]={'z','e','r','o','.','s','d','k','f','i','v','e',0,0,0,0};
}

#ifdef ZERO_TEACHER_WASM
#if defined(ZERO_SDK_SOURCE_SEQUENCE)
#include "source_intent.h"
#include "../src/core/student_history.h"
volatile zero_source_probe::Control source_control{{'z','e','r','o','.','s','d','k','f','i','v','e',0,0,0,0}};

void source_facts(int stage,zero::Head head,zero::ActionType command,const float* state,
                  const std::vector<zero::Cand>& rows,int label,std::uint32_t semantic_mask)
{
    using namespace zero;
    zero_lifecycle::Trace wire{};wire.stage=stage;wire.head=head;wire.command=command;wire.count=static_cast<int>(rows.size());wire.label=label;
    wire.rule=semantic_mask;wire.admitted=0;wire.used=0;wire.state=reinterpret_cast<std::uintptr_t>(state);
    wire.rows=reinterpret_cast<std::uintptr_t>(rows.data());wire.stride=sizeof(Cand);wire.kind_offset=offsetof(Cand,kind);
    wire.target_offset=offsetof(Cand,target);wire.count_offset=offsetof(Cand,count);wire.x_offset=offsetof(Cand,at)+offsetof(Tile,x);
    wire.y_offset=offsetof(Cand,at)+offsetof(Tile,y);wire.phi_offset=offsetof(Cand,phi);
    zero_student_probe(7,&wire,sizeof wire);
}
void source_observation(int input,const zero::Obs& o)
{
    using namespace zero;
    zero_source_probe::Observation wire{};wire.input=input;wire.command_index=o.command_index;wire.actor=o.self.id;wire.type=o.self.type;
    wire.x=o.self.at.x;wire.y=o.self.at.y;wire.gold=o.self.gold;wire.gems=o.self.gems;wire.cd0=o.self.cooldown[0];wire.cd1=o.self.cooldown[1];wire.state=o.state_bits;
    wire.channel=o.self.recalling?1:o.self.tributing?2:o.teleporting?3:0;wire.remaining=o.channel_remaining;wire.next_priority=o.priority;
    wire.hp=o.self.hp;wire.shield=o.self.shield;wire.allies=reinterpret_cast<std::uintptr_t>(o.allies.data());wire.allies_n=o.allies.size();
    wire.enemies=reinterpret_cast<std::uintptr_t>(o.enemies.data());wire.enemies_n=o.enemies.size();wire.camps=reinterpret_cast<std::uintptr_t>(o.camps.data());wire.camps_n=o.camps.size();
    wire.unit_stride=sizeof(UnitView);wire.camp_stride=sizeof(CampView);wire.unit_id_offset=offsetof(UnitView,id);wire.camp_id_offset=offsetof(CampView,id);
    zero_student_probe(9,&wire,sizeof wire);
}
int source_sdk_main()
{
    using namespace zero;
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    try {
        Map map;Obs observation;Memory memory;sdk::set_explicit_channel_mode(true);
        fuel_hook=[](){return cambc::fuel_remaining();};
        source_control.stage=1;sdk::load_terrain(map);sdk::load_groups(map);map.finish();
        auto before=cambc::fuel_remaining();sdk::observe(map,observation);observe(observation,map,memory);student::begin_commands(observation);
        source_control.observation_fuel=before-cambc::fuel_remaining();Context context{&map,nullptr,&observation,&memory};
        if(source_control.count!=5){source_control.failed=1;while(true)cambc::end_turn();}
        for(int index=0;index<5;++index){
            source_control.current=index;source_control.stage=2;
            const auto& request=source_control.steps[index];source::Intent intent;intent.type=static_cast<ActionType>(request.type);
            intent.argument.kind=static_cast<std::uint8_t>(request.kind);intent.argument.target=request.target;intent.argument.count=request.count;
            intent.argument.at={static_cast<std::int16_t>(request.x),static_cast<std::int16_t>(request.y)};
            intent.target_kind=static_cast<source::TargetKind>(request.target_kind);intent.target_id=request.target_id;intent.scalar=request.scalar;intent.scalar_known=request.scalar_known!=0;
            source_observation(request.input,observation);
            before=cambc::fuel_remaining();float state[student::kState];student::state_vector(context,state);
            const auto mask=student::rule_mask(context,kAllActions);
            const std::vector<Cand> empty;source_facts(0,h_action,intent.type,state,empty,intent.type,mask);
            if(source_control.failed)break;
            if(intent.type>=a_count||!(mask&action_bit(intent.type))){source_control.failed=2;break;}
            std::vector<Cand> arguments;Cand selected=intent.argument;int chosen=-1;
            if(intent.type!=a_end){
                const auto built=student::argument_candidates(context,intent.type,arguments);source_control.build_status=static_cast<int>(built.status);
                if(built.status!=ActionBuildStatus::ok){source_control.failed=3;break;}
                for(int i=0;i<static_cast<int>(arguments.size());++i)if(source::match_argument(intent,arguments[i])){chosen=i;break;}
                if(chosen<0){source_control.failed=4;break;}
                selected=arguments[chosen];source_control.selected=chosen;
                int label=chosen;
                if(intent.type==a_priority){if(!intent.scalar_known||intent.scalar<INT32_MIN||intent.scalar>INT32_MAX){source_control.failed=5;break;}selected.target=static_cast<std::int32_t>(intent.scalar);label=selected.target;}
                source_facts(intent.type==a_priority?2:1,student::argument_head(intent.type),intent.type,state,arguments,label,mask);
                if(source_control.failed)break;
            }
            source_control.argument_fuel+=before-cambc::fuel_remaining();source_control.stage=3;
            before=cambc::fuel_remaining();const auto receipt=sdk::primitive_result(intent.type,observation,selected);source_control.dispatch_fuel+=before-cambc::fuel_remaining();
            if(receipt!=sdk::PrimitiveResult::accepted){source_control.failed=6;break;}
            zero_student_probe(8,&index,sizeof index);
            if(source_control.failed)break;
            if(intent.type!=a_end){
                student::record_command(observation);source_control.stage=4;before=cambc::fuel_remaining();
                if(intent.type==a_priority&&source_control.priority_parity)source_control.refresh_fuel+=zero_priority_probe::refresh(map,observation,memory,selected);
                else{sdk::refresh_after_primitive(map,observation,intent.type,selected);observe(observation,map,memory);source_control.refresh_fuel+=before-cambc::fuel_remaining();}
            }
            source_control.completed=index+1;
        }
        source_control.stage=5;
    }catch(const std::exception& error){std::printf("source SDK error: %s\n",error.what());source_control.failed=7;}
    catch(...){std::printf("source SDK error: unknown\n");source_control.failed=8;}
    while(true)cambc::end_turn();
}
#endif
#else
namespace {
struct SourceFactBody { int input,stage,head,command,count,label;std::uint32_t semantic_mask;std::vector<std::uint8_t> body; };
std::vector<SourceFactBody> source_fact_bodies;
std::vector<std::vector<std::uint8_t>> source_expected_timeline,source_expected_full;
std::ptrdiff_t source_control_offset{};
int source_expected_cursor{};
}
#endif
