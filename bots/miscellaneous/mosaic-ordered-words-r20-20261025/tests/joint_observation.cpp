#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <array>

// This is the SDK producer's actual pure prefix, with no runtime facsimile.
struct Pure {
#define MOSAIC_JOINT_OBSERVATION_PURE_ONLY
#include "../src/joint_observation.h"
#undef MOSAIC_JOINT_OBSERVATION_PURE_ONLY
};
using Cursor=Pure::JointObservationCursor;
using Funding=Pure::JointObservationFunding;
using Fuel=std::uint64_t;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"joint-observation failed %d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

template<std::size_t N> bool sequential(Fuel fuel,const std::array<Fuel,N>& spending){
    for(const auto cost:spending){if(cost>=fuel)return false;fuel-=cost;}return true;
}
void packet_keeps_evaluation_and_command(){
    constexpr Fuel command=42000,evaluation=16000;
    // Independent physical sequence: body record, restrictions, then buffs.
    constexpr std::array<Fuel,3> components{1500,1000,2500};
    Fuel actual=63001;Cursor cursor{};cursor.reset(unsigned(components.size()));
    for(unsigned index=0;index<components.size();++index){
        const auto cost=components[index];
        CHECK(Funding::before(actual,command,evaluation,cost)==
            sequential(actual,std::array<Fuel,3>{cost,evaluation,command}));
        CHECK(Funding::before(actual,command,evaluation,cost));actual-=cost;
        CHECK(Funding::after(actual,command,evaluation));
        CHECK(cursor.commit(index,true));
    }
    CHECK(cursor.complete()&&cursor.next==3&&actual==58001);
    // The packet is actually complete before pricing. Evaluation is unpaid
    // until now; after spending it the42000 physical operation still fits.
    CHECK(sequential(actual,std::array<Fuel,2>{evaluation,command}));
    actual-=evaluation;CHECK(actual==42001);actual-=command;CHECK(actual==1);

    // An insufficient packet stops before the next SDK component. One real
    // receipt is progress, but it does not claim a complete body packet.
    cursor.reset(4);const auto initial_revision=cursor.revision;actual=60000;
    CHECK(Funding::before(actual,command,evaluation,components[0]));actual-=components[0];
    CHECK(cursor.commit(0,true)&&cursor.revision==initial_revision+1);
    CHECK(!Funding::before(actual,command,evaluation,components[1]));
    CHECK(cursor.next==1&&!cursor.complete()&&Funding::after(actual,command,evaluation));
}
void actual_count_two_vs_four(){
    constexpr Fuel entry=70000,evaluation=16000,packet=6000;
    constexpr Fuel two_callbacks=47000,four_callbacks=57000;
    CHECK(Funding::before(entry,two_callbacks,evaluation,packet));
    CHECK(sequential(entry,std::array<Fuel,3>{packet,evaluation,two_callbacks}));
    CHECK(!Funding::before(entry,four_callbacks,evaluation,packet));
    CHECK(!sequential(entry,std::array<Fuel,3>{packet,evaluation,four_callbacks}));
    Fuel actual=entry-packet;CHECK(Funding::after(actual,two_callbacks,evaluation));
    actual-=evaluation;CHECK(actual==48000&&actual>two_callbacks);
    // No incumbent/paid-evaluation state exists in this helper. It must keep
    // the first evaluation even when a metadata cursor has just completed.
    CHECK(!Funding::after(63000,two_callbacks,evaluation));
    CHECK(Funding::after(63001,two_callbacks,evaluation));
}
void receipts_cannot_be_renamed_progress(){
    Cursor cursor{};cursor.reset(3);const auto start=cursor.revision;
    CHECK(cursor.next==0&&cursor.total==3&&!cursor.complete());
    CHECK(!cursor.commit(0,false)&&cursor.next==0&&cursor.revision==start);
    CHECK(!cursor.commit(1,true)&&cursor.next==0&&cursor.revision==start);
    CHECK(cursor.commit(0,true)&&cursor.next==1&&cursor.revision==start+1);
    CHECK(!cursor.commit(0,true)&&cursor.next==1&&cursor.revision==start+1);
    cursor.reset(3);CHECK(cursor.next==1&&cursor.revision==start+1); // unchanged declaration
    CHECK(!cursor.commit(1,false)&&cursor.next==1&&cursor.revision==start+1);
    CHECK(cursor.commit(1,true)&&cursor.commit(2,true)&&cursor.complete());
    const auto complete_revision=cursor.revision;cursor.reset(3);
    CHECK(cursor.complete()&&cursor.next==3&&cursor.revision==complete_revision);
    CHECK(!cursor.commit(3,true)&&!cursor.commit(2,true)&&cursor.revision==complete_revision);
    cursor.reset(1);CHECK(cursor.next==0&&cursor.total==1&&!cursor.complete());
    CHECK(cursor.commit(0,true)&&cursor.complete());
}
void repeated_failed_queries_leave_real_tail(){
    constexpr Fuel command=42000,evaluation=16000,component=1500;
    Cursor cursor{};cursor.reset(2);const auto revision=cursor.revision;
    Fuel actual=65000;unsigned attempts=0;
    while(Funding::before(actual,command,evaluation,component)){
        actual-=component;++attempts;
        CHECK(!cursor.commit(0,false));
        CHECK(Funding::after(actual,command,evaluation));
    }
    CHECK(attempts==4&&actual==59000&&cursor.next==0&&cursor.revision==revision&&!cursor.complete());
    CHECK(sequential(actual,std::array<Fuel,2>{evaluation,command}));
    CHECK(!Funding::before(actual,command,evaluation,component));
}
void independent_boundaries_and_overflow(){
    constexpr Fuel evaluation=16000;
    for(Fuel fuel=0;fuel<=100000;fuel+=271)
    for(Fuel command:std::array<Fuel,3>{42000,47000,57000})
    for(Fuel component:std::array<Fuel,4>{0,1,1500,6000}){
        CHECK(Funding::before(fuel,command,evaluation,component)==
            sequential(fuel,std::array<Fuel,3>{component,evaluation,command}));
        CHECK(Funding::after(fuel,command,evaluation)==
            sequential(fuel,std::array<Fuel,2>{evaluation,command}));
    }
    const auto maximum=std::numeric_limits<Fuel>::max();
    CHECK(!Funding::before(maximum,maximum,16000,1));
    CHECK(!Funding::before(maximum,42000,maximum,1));
    CHECK(!Funding::before(maximum,42000,16000,maximum));
    CHECK(!Funding::after(maximum,maximum-3,8));
    CHECK(!Funding::after(58000,42000,16000)&&Funding::after(58001,42000,16000));
}
int main(){
    packet_keeps_evaluation_and_command();actual_count_two_vs_four();receipts_cannot_be_renamed_progress();
    repeated_failed_queries_leave_real_tail();independent_boundaries_and_overflow();
    std::printf("%u joint-observation checks passed\n",checks);
}
