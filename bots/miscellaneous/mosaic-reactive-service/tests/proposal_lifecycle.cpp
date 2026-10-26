#define main frozen_proposal_transition_fixture_main
#include "transition.cpp"
#undef main
#include "proposal_lifecycle.h"
#include "service_task.h"
namespace cambc::detail {
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++sdk_calls;*out={};return 0;}
}
struct ProposalFixture {
    mosaic::spatial::Index index;
    std::vector<mosaic::frame::SourceCamp> catalog;
    mosaic::selected::Packet value;
    std::array<int,1> duration{50};
    std::array<std::size_t,9> members{0,1,2,3,4,5,6,7,8};
    ProposalFixture() {
        using namespace mosaic;
        std::vector<spatial::Entry> layout;
        for(int y=16;y<=18;++y)for(int x=10;x<=12;++x) {
            frame::Camp c;c.handle=int(catalog.size());c.group=0;c.member=c.handle;c.pos=c.spawn={x,y};
            c.hp=c.design_hp=120;c.range=3;c.known=c.visible=c.alive=c.possible_alive=c.buffs_known=true;
            auto h=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(c.handle),
                static_cast<cambc::handle_camp_group_t>(0),cambc::camp_type::gold_cluster,c.spawn);
            catalog.push_back({h,c});layout.push_back({c.handle,x,y,3});
        }
        assert(index.build(layout,true));value.layout=index.borrow();value.catalog=catalog;
        value.self=::packet().self;value.self.pos={9,15};value.self.splash_radius=1;
        value.complete=value.self_complete=value.enemies_complete=value.move_test_complete=value.move_legal=value.effect_champions_clear=true;
        value.origin_complete=true;value.nonce=7;value.request.command.kind=Kind::wait;value.request.serial=3;
        for(std::size_t i=0;i<catalog.size();++i)value.rows.push_back({i,catalog[i].design,{},false,true,true,true,true,false});
        frame::Group g;g.id=0;g.state=frame::GroupState::alive;value.groups.push_back(g);epoch(10);
    }
    void epoch(int epoch,std::uint64_t revision=0) {
        value.stamp={epoch,2,revision};value.origin_stamp=value.stamp;
        value.request.command.expected_epoch=epoch;value.request.command.expected_actor=2;value.request.command.expected_revision=revision;
        for(auto& row:value.rows)row.stamp=value.stamp;
        value.groups[0].observed_round=epoch;
    }
    void dead(std::size_t i){value.rows[i].camp.alive=false;value.rows[i].camp.hp=0;}
};
int main() {
    using namespace mosaic;int checks=0;const auto initial=sdk_calls;
    {
        ProposalFixture f;f.value.self.pos={10,15};proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));
        f.dead(0);assert(negatives.observe(f.value,f.value.stamp,f.duration));++checks;
        assert(negatives.suppressed(0,10)&&negatives.suppressed(0,58)&&!negatives.suppressed(0,59)&&negatives.marked(0));++checks;
        auto reader=[&](std::size_t i){const auto& c=f.catalog[i].design;
            return service_task::Member{{c.spawn.x,c.spawn.y},120,negatives.may_propose(i,11)};};
        const auto next=service_task::nominate_basic(f.members,{10,15},1,40,reader);
        assert(next&&*next==1&&!negatives.may_propose(0,11));++checks;
        // A direct retained request uses exactly the same predicate.
        assert(!negatives.may_propose(std::size_t(f.value.rows[0].camp.handle),11));++checks;
        auto alive=f.value.groups[0];alive.observed_round=11;
        assert(negatives.observe_group(alive,{11,2,0},{11,2,0})&&negatives.suppressed(0,11));++checks;
        f.epoch(11);f.value.groups[0].observed_round=10;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&negatives.suppressed(0,58)&&!negatives.suppressed(0,59));++checks;
    }
    {
        ProposalFixture f;proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));
        for(auto i:{0u,1u,3u,4u})f.dead(i);
        assert(negatives.observe(f.value,f.value.stamp,f.duration));++checks;
        auto all=[&](std::size_t i){const auto& c=f.catalog[i].design;return service_task::Member{{c.spawn.x,c.spawn.y},120,true};};
        auto filtered=[&](std::size_t i){auto c=all(i);c.proposed=negatives.may_propose(i,11);return c;};
        assert(service_task::in_frontier(f.members,{9,15},1,all)&&!service_task::in_frontier(f.members,{9,15},1,filtered));++checks;
        assert(!service_task::nominate_basic(f.members,{9,15},1,40,filtered));++checks;
        auto potential=[&](service_task::Point p){int best=1000;for(auto i:f.members) {
            if(filtered(i).proposed)best=std::min(best,std::max(0,std::max(std::abs(p.x-filtered(i).pos.x),std::abs(p.y-filtered(i).pos.y))-1));
        }return best;};
        assert(potential({9,15})==2&&potential({10,16})==1);++checks;
        assert(f.catalog[0].design.spawn==cambc::vec2(10,16));++checks; // Reward goals still use every spawn.
        f.epoch(11);f.value.request.command.kind=Kind::move;f.value.request.command.destination={10,16};
        const auto move=selected::assess(f.value,f.value.request);
        assert(move.certificate&&move.result.action.eligible);++checks;
    }
    {
        ProposalFixture f;proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));
        f.dead(0);f.value.groups[0].state=frame::GroupState::respawning;f.value.groups[0].cooldown=4;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&negatives.suppressed(0,12)&&!negatives.suppressed(0,13));++checks;
        f.epoch(20);f.value.groups[0].cooldown=0;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&negatives.suppressed(0,20)&&!negatives.suppressed(0,21));++checks;
        f.epoch(30);f.value.groups[0].state=frame::GroupState::alive;f.value.groups[0].observed_round=29;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.suppressed(0,31));++checks;
    }
    {
        ProposalFixture f;proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));
        f.dead(0);f.value.complete=false;
        assert(!negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.marked(0));++checks;
        f.value.complete=true;f.value.rows[0].camp.known=false;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.marked(0));++checks;
        f.value.rows[0].camp.known=true;f.value.rows[0].state_fresh=false;
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.marked(0));++checks;
    }
    {
        ProposalFixture f;proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));f.dead(0);
        f.duration[0]=std::numeric_limits<int>::max();
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.suppressed(0,11));++checks;
        f.epoch(20);f.duration[0]=std::numeric_limits<int>::min();
        assert(negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.suppressed(0,21));++checks;
    }
    {
        ProposalFixture f;proposal::Lifecycle negatives;assert(negatives.bind(f.catalog,f.value.layout,1));
        f.dead(0);assert(negatives.observe(f.value,f.value.stamp,f.duration));
        auto reward=f.value.groups[0];reward.state=frame::GroupState::reward;reward.observed_round=11;
        assert(negatives.observe_group(reward,{11,2,1},{11,2,1}));++checks;
        auto replay=reward;replay.state=frame::GroupState::alive;
        assert(!negatives.observe_group(replay,{11,2,0},{11,2,1})&&negatives.suppressed(0,11));++checks;
        assert(!negatives.observe_group(replay,{11,2,0},{11,2,0})&&negatives.suppressed(0,11));++checks;
        auto born=reward;born.state=frame::GroupState::alive;born.observed_round=70;
        assert(negatives.observe_group(born,{70,2,0},{70,2,0})&&!negatives.marked(0)&&negatives.group_generation(0)==1);++checks;
        // Apply a current packet only after cycle invalidation: new deaths survive.
        f.epoch(70);f.dead(0);assert(negatives.observe(f.value,f.value.stamp,f.duration)&&negatives.marked(0));++checks;
        f.epoch(71);f.value.rows[0].camp.alive=true;f.value.rows[0].camp.hp=120;
        assert(negatives.marked(0)&&negatives.observe(f.value,f.value.stamp,f.duration)&&!negatives.marked(0));++checks;
    }
    assert(sdk_calls==initial);++checks;
    std::printf("proposal lifecycle checks: %d passed; host calls: %d\n",checks,sdk_calls-initial);
}
