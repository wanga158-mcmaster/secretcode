#include "../common/online-evidence.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
using namespace plan4::runtime;
namespace cm=cambc::mock;
struct Entry { cambc::map::camp_group group; cambc::vec2 anchor; };
struct Fixture {
    std::vector<Entry> groups;
    std::vector<int> lookup=std::vector<int>(2500,-1);
    void add(std::vector<cambc::vec2> points,bool visible=true,float hp=100) {
        std::vector<cambc::camp> members;const int id=int(groups.size());
        for(const auto p:points){members.emplace_back(id*100+int(members.size()),p,hp);lookup[cm::tile(p)]=id;cm::visible[cm::tile(p)]=visible;}
        groups.push_back({cambc::map::camp_group{id,std::move(members)},points.front()});
    }
    void sample(OnlineEvidence& out,int target=-1){out.start(groups,lookup,50,target);}
};
int main(){
    int cases=0;
    cm::reset();Fixture hidden;hidden.add({{5,5}},false);
    auto out=std::make_unique<OnlineEvidence>();hidden.sample(*out,0);
    assert(!cm::group_queries&&!cm::member_queries&&!cm::unauthorized);++cases;
    cm::reset();Fixture cleared;cleared.add({{5,5},{6,5}},true,0);cm::visible[cm::tile({6,5})]=false;
    cleared.sample(*out,0);assert(out->members().count==1 && out->members().samples[0].health==0);
    assert(out->members().samples[0].provenance.status==Status::exact_now && !cm::unauthorized);++cases;
    cm::reset();Fixture partial;partial.add({{5,5},{6,5}});cm::visible[cm::tile({6,5})]=false;cm::states[0]=0;
    partial.sample(*out,0);assert(out->members().count==1&&cm::member_queries==2&&!out->lifecycles().groups[0].all_spawns_visible);++cases;
    for(int opposed=0;opposed<2;++opposed){
        cm::reset();Fixture f;f.add({{5,5},{6,5}});
        cm::actors.push_back({1,{5,5},true,true,cambc::team::enemy});
        if(opposed)cm::actors.push_back({2,{6,5},true,true,cambc::team::ally});
        f.sample(*out);const auto p=out->producers();assert(p.count==1+opposed);
        for(int n=0;n<p.count;++n)assert(p.samples[n].producing&&p.samples[n].ambiguous==bool(opposed));
        assert(!p.complete&&!p.full_patch_visibility[0]);++cases;
    }
    cm::reset();Fixture many;many.add({{5,5}});
    for(int n=1;n<=5;++n)cm::actors.push_back({n,{n+10,10},true,true,cambc::team::ally});
    many.sample(*out,0);assert(out->producers().count==4&&out->producers().truncated);++cases;
    cm::reset();Fixture members;
    for(int g=0;g<3;++g){std::vector<cambc::vec2> points;for(int n=0;n<24;++n)points.push_back({n+1,g+2});members.add(points);cm::states[g]=0;
        cm::actors.push_back({g+1,{1,g+2},true,true,cambc::team::ally});}
    members.sample(*out);assert(out->members().count<=64&&out->members().truncated&&out->lifecycles().count<=8);++cases;
    cm::reset();Fixture large;std::vector<cambc::vec2> points;for(int n=0;n<33;++n)points.push_back({n+1,2});large.add(points);
    large.sample(*out,0);assert(!cm::group_queries&&out->lifecycles().truncated);++cases;
    for(int fault=0;fault<6;++fault){
        cm::reset();Fixture f;f.add({{5,5}});cm::actors.push_back({1,{5,5},true,true,cambc::team::enemy});
        out=std::make_unique<OnlineEvidence>();f.sample(*out);out->finish(f.groups);
        cm::round=11;cm::fuel=100000;cm::states[0]=2;
        if(fault==1)cm::round=12;
        if(fault==2)cm::visible[cm::tile({5,5})]=false;
        if(fault==3)cm::actors[0].alive=false;
        if(fault==4){cm::round=12;cm::states[0]=0;}
        if(fault==5)cm::shift_after=cm::queries+10;
        f.sample(*out);
        if(!fault){assert(out->capture_count()==1);const auto& e=out->capture(0);assert(e.supported&&e.receipt.origin==CaptureOrigin::lifecycle_boundaries&&!e.receipt.actor_attribution_known);}
        else assert(!out->capture_count());
        assert(!cm::unauthorized);++cases;
    }
    for(int cut=1;cut<=35;++cut){
        cm::reset();Fixture f;f.add({{5,5},{6,5}});cm::states[0]=0;
        cm::actors.push_back({1,{5,5},true,true,cambc::team::ally});
        cm::stop_after=cut;out=std::make_unique<OnlineEvidence>();f.sample(*out,0);
        assert(out->lifecycles().count<=8&&out->members().count<=64&&out->producers().count<=4&&!cm::unauthorized);
        if(cm::queries>=cut)assert(out->producers().truncated);
        ++cases;
    }
    std::cout<<"{\"schema\":\"plan4-online-evidence-tests/1\",\"passed\":true,\"cases\":"<<cases<<",\"engineWitnesses\":0}\n";
}
