#include <cassert>
#include <utility>
#include "../matching/src/farm_observation.h"
#include "../matching/src/policy.h"

namespace {
using namespace p11_matching;
using p11::Candidate;
using p11::State;
struct Record {
    int type,x,y,attack_range,state;
    float health,max_health,strength,focus;
};
State map_state(const FarmObservation&observation) {
    State state;
    state.hp=observation.hp;state.maximum=observation.maximum;
    state.strength=observation.strength;state.focus=observation.focus;
    state.identity=observation.type;state.field_shop=observation.type==11;
    return state;
}
void same(const FarmObservation&a,const FarmObservation&b) {
    assert(a.x==b.x&&a.y==b.y&&a.range==b.range&&a.type==b.type&&a.state==b.state);
    assert(a.hp==b.hp&&a.maximum==b.maximum&&a.strength==b.strength&&a.focus==b.focus);
}
Job cluster(const FarmObservation&observation) {
    Job job;job.x=2;job.y=0;
    auto&c=job.candidate;c.id=2;c.hp=120;c.gold=20;c.known=true;
    c.travel=std::max(0,std::max(std::abs(observation.x-job.x),std::abs(observation.y-job.y))-observation.range);
    c.clear=p11::attacks(c.hp,observation.strength);c.capture=6;
    return job;
}

void coherent_mapping() {
    int reads=0;
    auto changing=[&] {
        const int g=++reads;
        return Record{11,10+g,20+g,2+g,100+g,900.f-g,1000.f+g,45.f+g,110.f+g};
    };
    const auto first=observe_self(changing);
    assert(reads==1);
    same(first,FarmObservation{11,21,3,11,101,899,1001,46,111});
    const auto second=observe_self(changing);
    assert(reads==2);
    same(second,FarmObservation{12,22,4,11,102,898,1002,47,112});
    assert(first.hp==899&&first.range==3); // Owned values do not follow later reads.

    const Record fixed{11,0,0,3,37,900,900,45,110};
    int snapshot_reads=0,getter_reads=0;
    const auto snapshot=observe_self([&] {++snapshot_reads;return fixed;});
    auto getter=[&] {++getter_reads;return fixed;};
    const auto position=getter();
    const float hp=getter().health,maximum=getter().max_health;
    const float strength=getter().strength,focus=getter().focus;
    const int range=getter().attack_range,type=getter().type,state=getter().state;
    const FarmObservation separate{position.x,position.y,range,type,state,hp,maximum,strength,focus};
    same(snapshot,separate);
    assert(snapshot_reads==1&&getter_reads==8);

    const std::array<Worker,2> workers{{{0,0,0,900,900,45,true,11},{1,0,2,1000,1000,40,true,10}}};
    const std::array<Job,1> jobs{{cluster(snapshot)}};
    const std::array<Candidate,1> details{{jobs[0].candidate}};
    const auto mapped=map_state(snapshot),public_state=map_state(separate);
    const int assigned=Policy::assign(mapped,jobs,workers,0);
    assert(assigned==2&&assigned==Policy::assign(public_state,jobs,workers,0));
    assert(Policy::select(mapped,details,assigned)==2);
    const std::array<BasicTargetView,1> targets{{{8,120,0}}};
    const auto target=baseline_from_detail(targets,snapshot.strength,45,45);
    assert(target&&*target==8);
}

void baseline_semantics() {
    std::array<BasicTargetView,2> tied{{{20,100,10},{10,100,10}}};
    auto target=baseline_from_detail(tied,50,7,7);
    assert(target&&*target==20); // Strict-first, not lowest physical ID.
    std::reverse(tied.begin(),tied.end());
    target=baseline_from_detail(tied,50,7,7);
    assert(target&&*target==10);
    target=baseline_from_detail({},50,7,7);
    assert(target&&*target==-1);
    assert(!baseline_from_detail(tied,50,7,8));
    assert(!baseline_from_detail({},50,7,8));

    // The actual float32 subtraction recurrence needs six hits at649HP,
    //118Strength and Monster1.1; a quotient/ceil shortcut gives a false tie.
    const float damage=118.f*1.1f;
    assert(p11::attacks(649.f,damage)==6&&p11::attacks(600.f,damage)==5);
    const std::array<BasicTargetView,2> threshold{{{10,649,1},{20,600,1}}};
    target=baseline_from_detail(threshold,damage,45,45);
    assert(target&&*target==20);
}

void private_wait() {
    int reads=0,target_calls=0;
    const auto varg=observe_self([&] {++reads;return Record{10,0,2,1,37,1000,1000,40,100};});
    const auto state=map_state(varg);
    const std::array<Worker,2> workers{{{0,0,0,900,900,45,true,11},{1,0,2,1000,1000,40,true,10}}};
    const std::array<Job,1> jobs{{cluster(varg)}};
    const std::array<Candidate,1> details{{jobs[0].candidate}};
    const int assigned=Policy::assign(state,jobs,workers,1);
    const int selected=Policy::select(state,details,assigned);
    assert(assigned==-1&&selected==-1&&p11::choose<p11_tempo::Policy>(state,details)==2);
    auto continue_completed_assignment=[&] {
        // This is the actual caller's private-wait gate. The baseline helper
        // remains unreachable when the exact solver assigned a wait.
        if(assigned<0)return selected;
        ++target_calls;
        const std::array<BasicTargetView,1> views{{{8,120,0}}};
        const auto target=baseline_from_detail(views,varg.strength,45,45);
        return target?*target:-1;
    };
    assert(continue_completed_assignment()==-1&&target_calls==0&&reads==1);
}
}

int main() {
    coherent_mapping();baseline_semantics();private_wait();
}
