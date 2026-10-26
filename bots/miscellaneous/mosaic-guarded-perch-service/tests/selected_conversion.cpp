#define main frozen_conversion_transition_fixture_main
#include "transition.cpp"
#undef main
#include "selected_conversion.h"
#include <type_traits>
namespace cambc::detail {
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++sdk_calls;*out={};return 0;}
}
struct ServiceFixture {
    mosaic::spatial::Index index;
    std::vector<mosaic::frame::SourceCamp> catalog;
    mosaic::selected::Packet packet;
    mosaic::conversion::Request request;
    mosaic::conversion::Facts facts;
    ServiceFixture() {
        using namespace mosaic;
        assert(index.build({},true));packet.layout=index.borrow();packet.stamp={20,2,7};packet.nonce=19;
        packet.origin_stamp=packet.stamp;packet.origin_complete=true;
        packet.request.command.kind=transition::Kind::wait;packet.request.serial=11;
        packet.request.command.expected_epoch=20;packet.request.command.expected_actor=2;packet.request.command.expected_revision=7;
        packet.self=::packet().self;packet.self.gems=21;
        packet.complete=packet.self_complete=packet.enemies_complete=true;
        request.kind=conversion::Kind::tribute_start;request.stamp=packet.stamp;request.serial=23;request.altar={1,0};
        facts.stamp=packet.stamp;facts.packet_nonce=packet.nonce;facts.layout=packet.layout;facts.origin=packet.self.pos;
        facts.complete=facts.history_complete=facts.bag_complete=facts.channel_complete=facts.settings_complete=true;
        facts.altar_complete=facts.lock_complete=facts.altar_visible=facts.altar_valid=true;
        facts.history.epoch=20;facts.history.actor=2;facts.gems=21;facts.recall_duration=5;
        facts.submit_range=1;facts.submit_min=1;facts.submit_multiplier=0.1;facts.altar=request.altar;bind();
    }
    void bind(){facts.request=request;}
    void channel(mosaic::conversion::Channel channel,int remaining=2) {
        using mosaic::conversion::Channel;
        packet.self.recalling=channel==Channel::recall;packet.self.submitting=channel==Channel::tribute;
        packet.self.teleporting=channel==Channel::teleport;facts.channel=channel;facts.remaining=remaining;
    }
    void harmful(float strength=40) {
        using namespace mosaic;
        frame::Camp camp;camp.handle=1;camp.group=1;camp.pos=camp.spawn={1,0};camp.hp=100;camp.strength=strength;
        camp.range=3;camp.known=camp.visible=camp.alive=camp.possible_alive=camp.buffs_known=true;
        auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(1),
            static_cast<cambc::handle_camp_group_t>(1),cambc::camp_type::wolf,camp.spawn);
        catalog.push_back({handle,camp});const std::array entries{spatial::Entry{1,1,0,3}};
        assert(index.build(entries,true));packet.layout=index.borrow();packet.catalog=catalog;facts.layout=packet.layout;
        packet.rows.push_back({0,camp,packet.stamp,false,true,true,true,true,false});
    }
};
int main() {
    using namespace mosaic;
    namespace cv=mosaic::conversion;
    int checks=0;const auto initialized=sdk_calls;
    static_assert(!std::is_copy_constructible_v<cv::Certificate>);
    static_assert(!std::is_constructible_v<cv::Certificate,const selected::Packet&,const cv::Request&>);
    {
        ServiceFixture f;const auto assessed=cv::assess(f.packet,f.facts,f.request);
        assert(assessed.result.eligible&&assessed.certificate&&assessed.result.duration==3&&
            assessed.result.expected_completion_epoch==23&&assessed.result.starting_bag==21&&assessed.result.completion_is_conditional);++checks;
        for(auto [gems,duration]:{std::pair{1,1},std::pair{10,1},std::pair{11,2},std::pair{20,2},std::pair{21,3}}) {
            f.facts.gems=f.packet.self.gems=gems;assert(cv::evaluate(f.packet,f.facts,f.request).duration==duration);
        }
        ++checks;f.facts.gems=f.packet.self.gems=21;f.facts.submit_base=2;
        assert(cv::evaluate(f.packet,f.facts,f.request).duration==5);++checks;
        f.facts.submit_base=0;f.facts.submit_min=4;
        assert(cv::evaluate(f.packet,f.facts,f.request).duration==4);++checks;
    }
    {
        ServiceFixture f;f.request.kind=cv::Kind::recall_start;f.bind();
        const auto normal=cv::evaluate(f.packet,f.facts,f.request);assert(normal.eligible&&normal.duration==5&&normal.expected_completion_epoch==25);++checks;
        f.facts.recall_duration=3;assert(cv::evaluate(f.packet,f.facts,f.request).expected_completion_epoch==23);++checks;
        f.facts.history.recall_started=true;assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.facts.history.recall_started=false;f.facts.history.recall_cancelled=true;
        assert(cv::evaluate(f.packet,f.facts,f.request).eligible);++checks; // One start still unused this input.
        f.packet.self.stunned=true;assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.packet.self.stunned=false;
        f.channel(cv::Channel::tribute);assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        assert(cv::recall_start_for_capture(59,5,5)==60&&cv::recall_start_for_capture(59,5,3)==62);++checks;
        assert(cv::completion_epoch(60,5)==65&&*cv::completion_epoch(59,5)<65);++checks;
        assert(!cv::recall_start_for_capture(59,0,5)&&!cv::completion_epoch(std::numeric_limits<int>::max(),1));++checks;
    }
    {
        ServiceFixture f;f.facts.altar_visible=false;
        assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::incomplete);++checks;
        f.facts.altar_visible=true;f.facts.enemy_lock=true;
        assert(cv::evaluate(f.packet,f.facts,f.request).flags&cv::altar_locked);++checks;
        f.facts.enemy_lock=false;f.request.altar={1,1};f.bind();f.facts.altar={1,1};
        assert(cv::evaluate(f.packet,f.facts,f.request).eligible);++checks; // Chebyshev diagonal range1.
        f.request.altar=f.facts.altar={2,0};f.bind();
        assert(cv::evaluate(f.packet,f.facts,f.request).flags&cv::bad_altar);++checks;
        f.request.altar=f.facts.altar={1,0};f.bind();f.facts.gems=f.packet.self.gems=0;
        assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.facts.gems=10;f.packet.self.gems=11;
        assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::incomplete);++checks;
    }
    {
        ServiceFixture f;f.request.kind=cv::Kind::recall_cancel;f.bind();f.channel(cv::Channel::recall);
        f.facts.settings_complete=false;f.facts.bag_complete=false;
        assert(cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.harmful();f.packet.self.hp=10;
        const auto endangered=cv::assess(f.packet,f.facts,f.request);
        assert(endangered.certificate&&endangered.result.eligible&&endangered.result.requires_egress&&
            !endangered.result.origin_wait.safe&&(endangered.result.flags&cv::egress_required));++checks;
        f.facts.history.tribute_cancelled=true;
        assert(cv::evaluate(f.packet,f.facts,f.request).flags&cv::repeated_cancel);++checks;
        f.facts.history.tribute_cancelled=false;f.channel(cv::Channel::teleport);
        assert(cv::evaluate(f.packet,f.facts,f.request).flags&cv::wrong_channel);++checks;
        f.channel(cv::Channel::recall);f.facts.remaining=-1;
        assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::incomplete);++checks;
    }
    {
        ServiceFixture f;f.request.kind=cv::Kind::tribute_cancel;f.bind();f.channel(cv::Channel::tribute,1);
        assert(cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.facts.history.tribute_cancelled=true;assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        f.facts.history.tribute_cancelled=false;f.channel(cv::Channel::none);
        assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
    }
    {
        ServiceFixture f;f.harmful();
        const auto exposure=cv::evaluate(f.packet,f.facts,f.request);
        assert(!exposure.eligible&&(exposure.flags&cv::unsafe_wait)&&exposure.origin_wait.next_volley==40);++checks;
        f.request.kind=cv::Kind::recall_start;f.bind();assert(!cv::evaluate(f.packet,f.facts,f.request).eligible);++checks;
        ServiceFixture quiet;quiet.packet.origin_complete=false;
        assert(!cv::evaluate(quiet.packet,quiet.facts,quiet.request).eligible);++checks;
        quiet.packet.origin_complete=true;frame::Enemy foe;foe.alive=foe.visible=true;foe.pos={10,10};quiet.packet.enemies.push_back(foe);
        assert(cv::evaluate(quiet.packet,quiet.facts,quiet.request).flags&cv::unmodelled_enemy);++checks;
    }
    {
        ServiceFixture f;auto request=f.request;++request.serial;
        assert(cv::evaluate(f.packet,f.facts,request).status==selected::Status::stale);++checks;
        ++f.facts.packet_nonce;assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::stale);++checks;
        --f.facts.packet_nonce;f.facts.history.epoch=19;
        assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::stale);++checks;
        f.facts.history.epoch=20;f.facts.submit_multiplier=std::numeric_limits<double>::infinity();
        assert(cv::evaluate(f.packet,f.facts,f.request).flags&cv::bad_duration);++checks;
        f.request.kind=cv::Kind::buy;f.request.item=0;f.request.count=5;f.bind();
        assert(cv::evaluate(f.packet,f.facts,f.request).status==selected::Status::unsupported);++checks;
    }
    {
        ServiceFixture f;auto assessed=cv::assess(f.packet,f.facts,f.request);assert(assessed.certificate);
        assert(assessed.certificate->consume(f.packet.stamp,f.request,f.packet.request,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        assert(!assessed.certificate->consume(f.packet.stamp,f.request,f.packet.request,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        auto again=cv::assess(f.packet,f.facts,f.request);auto wrong_basis=f.packet.request;++wrong_basis.serial;
        assert(!again.certificate->consume(f.packet.stamp,f.request,wrong_basis,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        assert(!again.certificate->consume(f.packet.stamp,f.request,f.packet.request,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        auto moved_source=cv::assess(f.packet,f.facts,f.request);auto moved=std::move(*moved_source.certificate);
        assert(!moved_source.certificate->consume(f.packet.stamp,f.request,f.packet.request,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        assert(moved.consume(f.packet.stamp,f.request,f.packet.request,f.packet.layout,f.packet.self.pos,f.packet.nonce));++checks;
        auto next=cv::advanced(f.facts.history,f.request);assert(next&&next->tribute_started);++checks;
        ++f.request.stamp.epoch;assert(!cv::advanced(f.facts.history,f.request));++checks;
    }
    assert(sdk_calls==initialized);++checks;
    std::printf("selected conversion checks: %d passed; evaluator SDK calls: %d\n",checks,sdk_calls-initialized);
}
