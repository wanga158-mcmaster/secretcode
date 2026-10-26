// Reuse the frozen SDK startup doubles and typed fact constructors. Its main is
// renamed, so these tests execute only the selected sparse kernel below.
#define main frozen_transition_fixture_main
#include "transition.cpp"
#undef main
#include "selected_transition.h"
#include <type_traits>
namespace cambc::detail {
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++sdk_calls;*out={};return 0;}
}

struct SparseFixture {
    std::vector<mosaic::frame::SourceCamp> catalog;
    mosaic::spatial::Index index;
    mosaic::selected::Packet value;
    void assign(const Packet& source,Command command) {
        using namespace mosaic;
        std::vector<spatial::Entry> layout;
        for(const auto& camp:source.camps) {
            auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(camp.handle),
                static_cast<cambc::handle_camp_group_t>(camp.group),cambc::camp_type::wolf,camp.spawn);
            catalog.push_back({handle,camp});layout.push_back({camp.handle,camp.spawn.x,camp.spawn.y,camp.range});
        }
        assert(index.build(layout,true));value.layout=index.borrow();value.catalog=catalog;
        value.stamp={source.epoch,source.actor,source.revision};value.request={command,13};value.nonce=17;
        value.origin_stamp=value.stamp;value.origin_complete=true;
        value.self=source.self;value.groups=source.groups;value.enemies=source.enemies;
        value.complete=value.self_complete=value.enemies_complete=value.move_test_complete=value.move_legal=true;
        value.effect_champions_clear=true;
        for(std::size_t i=0;i<source.camps.size();++i)value.rows.push_back({i,source.camps[i],value.stamp,true,true,true,true,true,true});
    }
};

int main() {
    using namespace mosaic;
    int checks=0;const auto initialized=sdk_calls;
    static_assert(!std::is_copy_constructible_v<selected::Certificate>);
    static_assert(!std::is_constructible_v<selected::Certificate,const selected::Packet&,const selected::Request&>);
    {
        auto f=packet();camp(f,1,1,1,0,100,20);f.camps[0].awake=true;
        SparseFixture sparse;sparse.assign(f,basic(f,1));
        const auto result=selected::evaluate(sparse.value,sparse.value.request);const auto old=evaluate(f,basic(f,1));
        assert(result.status==selected::Status::ready&&result.action.eligible&&result.origin_wait.complete&&result.origin_wait.safe);++checks;
        assert(result.action.camps.size()==old.camps.size()&&result.action.camps[0].after_hp==old.camps[0].after_hp&&
            result.action.next_volley==old.next_volley&&result.action.hp_after_prefix==old.hp_after_prefix&&
            result.action.max_prefix_loss==old.max_prefix_loss);++checks;
        // Sleeping now cannot prove quiet: a later teammate may wake the group.
        sparse.value.rows[0].camp.awake=false;
        const auto sleeping=selected::evaluate(sparse.value,sparse.value.request);
        assert(sleeping.action.next_volley==20&&sleeping.origin_wait.next_volley==20);++checks;
        auto cert=selected::certify(sparse.value,sparse.value.request);assert(cert);++checks;
        assert(cert->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        assert(!cert->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        auto cert2=selected::certify(sparse.value,sparse.value.request);assert(cert2);auto wrong=sparse.value.request;++wrong.serial;
        assert(!cert2->consume(sparse.value.stamp,wrong,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        assert(!cert2->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        auto assessed=selected::assess(sparse.value,sparse.value.request);assert(assessed.certificate&&assessed.result.action.eligible);
        auto moved=std::move(*assessed.certificate);
        assert(!assessed.certificate->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        assert(moved.consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce));++checks;
        auto wrong_origin=selected::certify(sparse.value,sparse.value.request);assert(wrong_origin);
        assert(!wrong_origin->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,{1,1},sparse.value.nonce));++checks;
        auto wrong_nonce=selected::certify(sparse.value,sparse.value.request);assert(wrong_nonce);
        assert(!wrong_nonce->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,f.self.pos,sparse.value.nonce+1));++checks;
        spatial::Index foreign;const std::array other{spatial::Entry{1,1,0,3}};assert(foreign.build(other,true));
        auto wrong_layout=selected::certify(sparse.value,sparse.value.request);assert(wrong_layout);
        assert(!wrong_layout->consume(sparse.value.stamp,sparse.value.request,foreign.borrow(),f.self.pos,sparse.value.nonce));++checks;
    }
    {
        auto f=packet();f.self.splash_radius=1;
        camp(f,1,1,1,0,100,20);camp(f,2,1,1,1,100,20);camp(f,3,1,2,0,100,20);camp(f,4,1,2,1,100,20);
        camp(f,5,1,4,0,100,60,4);
        SparseFixture sparse;sparse.assign(f,basic(f,1));const auto result=selected::evaluate(sparse.value,sparse.value.request);
        assert(result.action.camps.size()==4&&result.action.useful_hp_removed==160&&result.action.next_volley==140&&
            !result.action.eligible&&!result.origin_wait.safe);++checks;
        sparse.value.rows[4].camp.known=false;sparse.value.rows[4].camp.hp=0;sparse.value.rows[4].state_fresh=false;
        const auto hidden=selected::evaluate(sparse.value,sparse.value.request);
        assert(hidden.status==selected::Status::incomplete&&!hidden.action.eligible&&!hidden.origin_wait.complete);++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,10,50);f.camps[0].shield=100;
        SparseFixture sparse;sparse.assign(f,basic(f,1));const auto shield=selected::evaluate(sparse.value,sparse.value.request);
        assert(shield.action.camps[0].after_hp==10&&shield.action.next_volley==50);++checks;
        sparse.value.rows[0].buffs_fresh=false;
        const auto missing=selected::evaluate(sparse.value,sparse.value.request);
        assert(missing.status==selected::Status::incomplete&&missing.origin_wait.complete&&missing.origin_wait.next_volley==50);++checks;
        sparse.value.rows[0].buffs_fresh=true;sparse.value.rows[0].camp.shield=0;
        sparse.value.self.basic_damage_known=false;
        const auto unknown=selected::evaluate(sparse.value,sparse.value.request);
        assert(!unknown.action.effect_exact&&unknown.action.next_volley==50);++checks;
    }
    {
        auto f=packet();f.self.pos={1,1};f.self.hp=30;camp(f,1,1,4,1,100,40,3);
        auto cmd=command(f,Kind::move);cmd.destination={0,1};SparseFixture sparse;sparse.assign(f,cmd);
        const auto result=selected::evaluate(sparse.value,sparse.value.request);
        assert(result.action.admissible&&result.action.survives_prefix&&result.action.next_volley==0&&
            result.origin_wait.complete&&!result.origin_wait.safe&&result.origin_wait.next_volley==40&&!result.action.eligible);++checks;
        sparse.value.complete=false;
        const auto partial=selected::evaluate(sparse.value,sparse.value.request);
        assert(partial.status==selected::Status::incomplete&&partial.origin_wait.complete&&!partial.origin_wait.safe);++checks;
        sparse.value.complete=true;sparse.value.rows[0].retaliation_origin=false;
        assert(!selected::evaluate(sparse.value,sparse.value.request).origin_wait.complete);++checks;
        sparse.value.rows[0].retaliation_origin=true;sparse.value.origin_complete=false;
        assert(!selected::assess_origin_wait(sparse.value,sparse.value.request).complete);++checks;
        sparse.value.origin_complete=true;++sparse.value.origin_stamp.epoch;
        assert(!selected::assess_origin_wait(sparse.value,sparse.value.request).complete);++checks;
    }
    {
        auto f=packet();f.self.pos={1,1};camp(f,1,1,5,1,100,20,3);
        auto cmd=command(f,Kind::move);cmd.destination={2,1};SparseFixture sparse;sparse.assign(f,cmd);
        sparse.value.rows[0].retaliation_after=false;
        const auto missing=selected::evaluate(sparse.value,sparse.value.request);
        assert(missing.status==selected::Status::incomplete&&missing.origin_wait.complete&&missing.origin_wait.safe);++checks;
        sparse.value.rows[0].retaliation_after=true;sparse.value.move_test_complete=false;
        assert(selected::evaluate(sparse.value,sparse.value.request).status==selected::Status::incomplete);++checks;
        sparse.value.move_test_complete=true;sparse.value.move_legal=false;
        assert(selected::evaluate(sparse.value,sparse.value.request).status==selected::Status::rejected);++checks;
    }
    {
        auto f=packet();camp(f,27,1,1,0,100);SparseFixture sparse;sparse.assign(f,basic(f,27));
        auto changed=sparse.value.request;++changed.serial;
        assert(selected::evaluate(sparse.value,changed).status==selected::Status::stale);++checks;
        ++sparse.value.rows[0].stamp.revision;
        assert(selected::evaluate(sparse.value,sparse.value.request).status==selected::Status::stale);++checks;
        --sparse.value.rows[0].stamp.revision;sparse.index.revoke();
        assert(selected::evaluate(sparse.value,sparse.value.request).status==selected::Status::stale);++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,100);SparseFixture sparse;sparse.assign(f,basic(f,1));
        frame::Enemy enemy;enemy.alive=enemy.visible=true;enemy.pos={1,0};enemy.range=1;enemy.hp=100;
        sparse.value.enemies.push_back(enemy);Constraints limits;limits.allow_unmodelled_foes=true;
        assert(selected::evaluate(sparse.value,sparse.value.request,limits).status==selected::Status::unsupported);++checks;
        sparse.value.enemies.clear();sparse.value.effect_champions_clear=false;
        assert(selected::evaluate(sparse.value,sparse.value.request).status==selected::Status::unsupported);++checks;
        for(auto kind:{Kind::cast,Kind::reset,Kind::recall,Kind::tribute}) {
            sparse.value.request.command.kind=kind;
            const auto result=selected::evaluate(sparse.value,sparse.value.request);
            assert(result.status==selected::Status::unsupported&&!result.action.eligible);++checks;
        }
    }
    {
        auto f=packet();camp(f,1,1,1,0,100,40);f.camps[0].alive=f.camps[0].possible_alive=false;
        SparseFixture sparse;sparse.assign(f,command(f,Kind::wait));frame::Group g;g.id=1;
        assert(!selected::evaluate(sparse.value,sparse.value.request).origin_wait.complete);++checks;
        g.state=frame::GroupState::alive;g.observed_round=f.epoch;sparse.value.groups.push_back(g);
        assert(selected::evaluate(sparse.value,sparse.value.request).action.eligible);++checks;
        sparse.value.groups[0].observed_round=f.epoch-1;
        assert(!selected::evaluate(sparse.value,sparse.value.request).origin_wait.complete);++checks;
        g.state=frame::GroupState::respawning;g.observed_round=f.epoch;g.cooldown=0;sparse.value.groups.push_back(g);
        sparse.value.groups.erase(sparse.value.groups.begin());
        const auto result=selected::evaluate(sparse.value,sparse.value.request);
        assert(!result.action.forecast_complete&&!result.origin_wait.complete&&!result.action.eligible);++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,0,40);f.camps[0].alive=f.camps[0].possible_alive=false;
        auto cmd=command(f,Kind::move);cmd.destination={0,1};
        SparseFixture sparse;sparse.assign(f,cmd);frame::Group g;
        g.id=1;g.state=frame::GroupState::respawning;g.observed_round=f.epoch;
        sparse.value.groups={g};
        // A corpse plus an actual countdown is a one-phase proof only with the
        // conservative publication slack. Equality at 0/1/2 stays incomplete.
        for(int remaining:{0,1,2}) {
            sparse.value.groups[0].cooldown=remaining;
            const auto blocked=selected::assess(sparse.value,sparse.value.request);
            assert(!blocked.result.origin_wait.complete&&!blocked.result.action.eligible&&!blocked.certificate);++checks;
        }
        for(int remaining:{3,50}) {
            sparse.value.groups[0].cooldown=remaining;
            auto allowed=selected::assess(sparse.value,sparse.value.request);
            assert(allowed.result.origin_wait.complete&&allowed.result.origin_wait.safe&&
                allowed.result.origin_wait.next_volley==0&&allowed.result.action.forecast_complete&&
                allowed.result.action.next_volley==0&&allowed.certificate);++checks;
            assert(allowed.certificate->consume(sparse.value.stamp,sparse.value.request,sparse.value.layout,
                sparse.value.self.pos,sparse.value.nonce));++checks;
        }
        sparse.value.groups[0].cooldown=3;sparse.value.groups[0].observed_round=f.epoch-1;
        assert(!selected::assess(sparse.value,sparse.value.request).certificate&&
            !selected::assess_origin_wait(sparse.value,sparse.value.request).complete);++checks;
        sparse.value.groups[0].observed_round=f.epoch;sparse.value.groups[0].state=frame::GroupState::unknown;
        assert(!selected::assess(sparse.value,sparse.value.request).certificate);++checks;
        sparse.value.groups[0].state=frame::GroupState::respawning;sparse.value.rows[0].camp.alive=true;
        sparse.value.rows[0].camp.hp=100;
        assert(!selected::assess(sparse.value,sparse.value.request).certificate&&
            !selected::assess_origin_wait(sparse.value,sparse.value.request).complete);++checks;
        sparse.value.rows[0].camp.alive=false;sparse.value.rows[0].state_fresh=false;
        assert(!selected::assess(sparse.value,sparse.value.request).certificate);++checks;
        sparse.value.rows[0].state_fresh=true;Constraints longer;longer.camp_volleys=2;
        assert(!selected::assess(sparse.value,sparse.value.request,longer).certificate);++checks;
    }
    {
        int cases=0;
        for(int count:{9,30,160})for(int splash:{0,1})for(int strength:{0,20}) {
            auto f=packet();f.self.hp=f.self.max_hp=1000000;f.self.splash_radius=splash;
            f.self.shield=35;f.self.shield_duration=2;
            for(int i=0;i<count;++i) {
                const int x=i<4?1+i%2:30+i%60,y=i<4?i/2:30+i/60;
                camp(f,100+3*i,1,x,y,float(20+13*i),float(strength));f.camps.back().awake=true;
            }
            const auto cmd=basic(f,100);const auto expected=evaluate(f,cmd);
            SparseFixture sparse;sparse.assign(f,cmd);
            // Actual packet storage contains only selected support, not N resets.
            std::erase_if(sparse.value.rows,[&](const selected::Row& row){
                return domain::distance(row.camp.pos,f.self.pos)>row.camp.range&&
                    domain::distance(row.camp.pos,f.camps[0].pos)>splash;
            });
            const auto actual=selected::evaluate(sparse.value,sparse.value.request).action;
            assert(actual.eligible==expected.eligible&&actual.effect_exact==expected.effect_exact&&
                actual.next_volley==expected.next_volley&&actual.max_prefix_loss==expected.max_prefix_loss&&
                actual.hp_after_prefix==expected.hp_after_prefix&&actual.useful_hp_removed==expected.useful_hp_removed&&
                actual.wake_groups==expected.wake_groups&&actual.camps.size()==expected.camps.size());
            for(std::size_t i=0;i<actual.camps.size();++i)assert(actual.camps[i].handle==expected.camps[i].handle&&
                actual.camps[i].before_hp==expected.camps[i].before_hp&&actual.camps[i].after_hp==expected.camps[i].after_hp);
            ++cases;
        }
        assert(cases==12);++checks;
    }
    assert(sdk_calls==initialized);++checks;
    std::printf("selected transition checks: %d passed; evaluator SDK calls: %d\n",checks,sdk_calls-initialized);
}
