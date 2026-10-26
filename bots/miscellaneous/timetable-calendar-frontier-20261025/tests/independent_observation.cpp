#define TT_OBSERVATION_PURE_ONLY
#include "../src/independent_observation.cpp"
#include <cstdio>
#include <limits>

namespace {
struct BootstrapFixture
{
    struct Published {std::uint32_t raw{},gold{},gems{};std::vector<std::uint32_t> members;};
    std::vector<Published> published;
    Published pending;
    std::size_t headers{},appends{},group_imports{},member_imports{},reserved{};
    bool bad_group_count{},bad_member_count{};
    int* suspended_epoch{};
    std::uint32_t group_count(){return 2;}
    void reserve_groups(std::size_t count){published.reserve(count);}
    bool group_ids(std::span<std::uint32_t> ids)
    {
        ++group_imports;
        if(ids.size()!=2)return false;
        ids[0]=21;ids[1]=7;return !bad_group_count;
    }
    std::size_t begin_group(std::uint32_t raw,std::size_t ordinal)
    {
        ++headers;
        pending={raw,ordinal?400u:200u,ordinal?40u:20u,{}};
        return raw==21?2:1;
    }
    void reserve_members(std::size_t count){reserved=count;pending.members.reserve(count);}
    bool member_ids(std::uint32_t raw,std::span<std::uint32_t> ids)
    {
        ++member_imports;
        if(raw==21 && ids.size()==2){ids[0]=42;ids[1]=100;}
        else if(raw==7 && ids.size()==1)ids[0]=15;
        else return false;
        return !bad_member_count;
    }
    void append_member(std::uint32_t raw,std::uint32_t member,std::size_t ordinal,std::size_t member_ordinal)
    {
        if(raw!=pending.raw || ordinal!=published.size() || member_ordinal!=pending.members.size())
            throw std::runtime_error("error mismatched bootstrap ordinal");
        pending.members.push_back(member);++appends;
        if(suspended_epoch){++*suspended_epoch;suspended_epoch=nullptr;}
    }
    void publish_group(std::uint32_t raw,std::size_t ordinal)
    {
        if(raw!=pending.raw || ordinal!=published.size() || pending.members.size()!=reserved)
            throw std::runtime_error("error partial bootstrap group publication");
        published.push_back(std::move(pending));
    }
};
template<class Check>
void bootstrap_checks(Check check)
{
    using namespace tt::observation_detail;
    struct Point {int x{},y{};bool operator==(const Point&)const=default;};
    std::vector<std::uint8_t> bytes{0x81,0xff};
    std::vector<Point> points;points.reserve(10);
    std::size_t cursor=0;
    check(!advance_packed_positions(5,2,bytes,cursor,0,points) && cursor==0 && points.empty(),
          "zero startup work does not decode a partial packed map");
    check(!advance_packed_positions(5,2,bytes,cursor,1,points) && cursor==1 &&
          points==std::vector<Point>{{0,0},{2,1}},
          "actual bootstrap packed decoder resumes at a byte boundary across nonaligned rows");
    check(advance_packed_positions(5,2,bytes,cursor,1,points) && cursor==2 &&
          points==std::vector<Point>{{0,0},{2,1},{3,1},{4,1}},
          "bootstrap packed tail ignores padding while retaining prior immutable positions");
    BootstrapCursor layout;BootstrapFixture backend;
    check(layout.advance(backend,10,0,[]{return true;})==BootstrapStatus::NeedCompute &&
          backend.headers==0 && layout.last_work()==0,"zero work performs no native-layout decoding");
    std::size_t slices=0;
    while(layout.status()==BootstrapStatus::NeedCompute && slices++<100)
    {
        const auto before=backend.appends;
        layout.advance(backend,10,1,[]{return true;});
        check(layout.last_work()<=1 && backend.appends-before<=1,
              "actual layout cursor limits each one-unit input to at most one camp publication");
        if(backend.published.empty())check(backend.appends<=2,"no group is published before its last member");
    }
    check(layout.status()==BootstrapStatus::Ready && backend.published.size()==2 && slices>10 &&
          backend.headers==2 && backend.group_imports==1 && backend.member_imports==2,
          "finite startup completes without re-fetching or resetting prior groups across owner inputs");
    check(backend.published.size()==2 && backend.published[0].raw==21 && backend.published[1].raw==7 &&
          backend.published[0].members==std::vector<std::uint32_t>{42,100} &&
          backend.published[1].members==std::vector<std::uint32_t>{15} &&
          backend.published[0].gold==200 && backend.published[0].gems==20,
          "opaque raw IDs returned out of numeric order retain group/member vector ordinals and named rewards");
    const auto final_appends=backend.appends;
    check(layout.advance(backend,10,64,[]{return true;})==BootstrapStatus::Ready &&
          layout.last_work()==0 && backend.appends==final_appends,"ready bootstrap is idempotent");

    BootstrapCursor suspended;BootstrapFixture delayed;int epoch=3;
    delayed.suspended_epoch=&epoch;
    for(int inputs=0;inputs<100 && !delayed.appends;++inputs)
        suspended.advance(delayed,10,64,[&]{return epoch==3;});
    check(delayed.appends==1 && suspended.member()==1 && suspended.status()==BootstrapStatus::NeedCompute,
          "a simulated import suspension withdraws after preserving exactly one completed immutable member");
    const auto held_group=suspended.group(),held_member=suspended.member();
    suspended.advance(delayed,10,64,[]{return false;});
    check(suspended.last_work()==0 && suspended.group()==held_group && suspended.member()==held_member,
          "missing fresh headroom/epoch retains the actual decoder rank without a retry");
    for(int inputs=0;inputs<100 && suspended.status()==BootstrapStatus::NeedCompute;++inputs)
        suspended.advance(delayed,10,64,[]{return true;});
    check(suspended.status()==BootstrapStatus::Ready && delayed.appends==3 && delayed.headers==2,
          "fresh owner inputs resume a suspended decoder without duplicate native member publication");
    // Exercise the production batch driver over the ACTUAL legacy layout
    // chunks, not a mirrored estimate of how many owner inputs they require.
    BootstrapCursor together;BootstrapFixture joined;int joined_chunks=0;
    const auto combined=advance_bootstrap_batch(64,[&] {
        ++joined_chunks;
        return together.advance(joined,10,64,[]{return true;})==BootstrapStatus::Ready;
    },[]{return true;},[]{return true;});
    check(combined.status==BootstrapBatchStatus::Ready && combined.chunks>1 &&
          combined.chunks==static_cast<std::size_t>(joined_chunks) && joined.published.size()==2 &&
          joined.group_imports==1 && joined.member_imports==2 && joined.appends==3,
          "one bounded invocation joins multiple legacy chunks and publishes each actual immutable group once");

    BootstrapCursor capped;BootstrapFixture capped_backend;
    const auto limited=advance_bootstrap_batch(2,[&] {
        return capped.advance(capped_backend,10,64,[]{return true;})==BootstrapStatus::Ready;
    },[]{return true;},[]{return true;});
    check(limited.status==BootstrapBatchStatus::NeedCompute && limited.chunks==2 &&
          capped.status()==BootstrapStatus::NeedCompute,
          "the finite chunk cap returns without pretending that partial World storage is Ready");
    const auto capped_resume=advance_bootstrap_batch(64,[&] {
        return capped.advance(capped_backend,10,64,[]{return true;})==BootstrapStatus::Ready;
    },[]{return true;},[]{return true;});
    check(capped_resume.status==BootstrapBatchStatus::Ready && capped_backend.headers==2 &&
          capped_backend.appends==3 && capped_backend.group_imports==1,
          "the next bounded invocation resumes retained cap-limited decoder rank without duplicate imports");

    BootstrapCursor fuel_cursor;BootstrapFixture fuel_backend;int remaining=1000;
    const auto fuel_limited=advance_bootstrap_batch(64,[&] {
        remaining-=100;
        return fuel_cursor.advance(fuel_backend,10,64,[]{return true;})==BootstrapStatus::Ready;
    },[]{return true;},[&]{return remaining>500;});
    check(fuel_limited.status==BootstrapBatchStatus::NeedCompute && fuel_limited.chunks==5 &&
          remaining==500 && fuel_cursor.status()==BootstrapStatus::NeedCompute,
          "headroom is sampled between actual chunks and prevents starting a sixth chunk");
    remaining=1000;
    const auto fuel_resume=advance_bootstrap_batch(64,[&] {
        remaining-=100;
        return fuel_cursor.advance(fuel_backend,10,64,[]{return true;})==BootstrapStatus::Ready;
    },[]{return true;},[&]{return remaining>500;});
    check(fuel_resume.status==BootstrapBatchStatus::Ready && fuel_backend.appends==3 &&
          fuel_backend.group_imports==1 && fuel_backend.headers==2,
          "fresh headroom completes the retained batch without restarting already decoded groups");

    BootstrapCursor crossing;BootstrapFixture crossing_backend;int batch_epoch=4;
    crossing_backend.suspended_epoch=&batch_epoch;
    const auto crossed=advance_bootstrap_batch(64,[&] {
        return crossing.advance(crossing_backend,10,64,[&]{return batch_epoch==4;})==BootstrapStatus::Ready;
    },[&]{return batch_epoch==4;},[]{return true;});
    check(crossed.status==BootstrapBatchStatus::Stale && crossing_backend.appends==1 &&
          crossing.member()==1 && crossing_backend.published.empty(),
          "suspension during a real metadata chunk stops rebatching and retains the completed member exactly once");
    const auto crossing_resume=advance_bootstrap_batch(64,[&] {
        return crossing.advance(crossing_backend,10,64,[&]{return batch_epoch==5;})==BootstrapStatus::Ready;
    },[&]{return batch_epoch==5;},[]{return true;});
    check(crossing_resume.status==BootstrapBatchStatus::Ready && crossing_backend.appends==3 &&
          crossing_backend.headers==2,
          "fresh owner epoch resumes a withdrawn immutable batch without losing rank or duplicating groups");

    int empty_calls=0;
    const auto empty_batch=advance_bootstrap_batch(0,[&]{++empty_calls;return true;},
        [&]{++empty_calls;return true;},[&]{++empty_calls;return true;});
    check(empty_batch.status==BootstrapBatchStatus::NeedCompute && empty_batch.chunks==0 && empty_calls==0,
          "zero chunk capacity performs no imports or guard queries");
    int final_epoch=8;
    const auto stale_ready=advance_bootstrap_batch(2,[&]{++final_epoch;return true;},
        [&]{return final_epoch==8;},[]{return true;});
    check(stale_ready.status==BootstrapBatchStatus::Stale && stale_ready.chunks==1,
          "Ready from a chunk that crosses owner epoch is never returned as fresh completion");
    int guard_epoch=11,guard_chunks=0;
    const auto stale_headroom=advance_bootstrap_batch(2,[&]{++guard_chunks;return false;},
        [&]{return guard_epoch==11;},[&]{++guard_epoch;return true;});
    check(stale_headroom.status==BootstrapBatchStatus::Stale && guard_chunks==0,
          "a suspended headroom query is fenced before beginning an indivisible chunk");
    int final_fuel=1000,final_queries=0;
    const auto final_ready=advance_bootstrap_batch(2,[&]{final_fuel=0;return true;},
        []{return true;},[&]{++final_queries;return final_fuel>500;});
    check(final_ready.status==BootstrapBatchStatus::Ready && final_ready.chunks==1 && final_queries==1,
          "completed final chunk needs no headroom for a nonexistent additional chunk");

    BootstrapCursor wrong_groups;BootstrapFixture wrong;wrong.bad_group_count=true;
    for(int inputs=0;inputs<100 && wrong_groups.status()==BootstrapStatus::NeedCompute;++inputs)
        wrong_groups.advance(wrong,10,64,[]{return true;});
    check(wrong_groups.status()==BootstrapStatus::Invalid && wrong.headers==0 && wrong.published.empty(),
          "a group list count mismatch cannot publish a truncated immutable World");
    BootstrapCursor wrong_members;BootstrapFixture wrong_member;wrong_member.bad_member_count=true;
    for(int inputs=0;inputs<100 && wrong_members.status()==BootstrapStatus::NeedCompute;++inputs)
        wrong_members.advance(wrong_member,10,64,[]{return true;});
    check(wrong_members.status()==BootstrapStatus::Invalid && wrong_member.appends==0 && wrong_member.published.empty(),
          "a member list count mismatch cannot manufacture missing member identities");
    BootstrapCursor overflow;BootstrapFixture oversized;
    overflow.advance(oversized,1,64,[]{return true;});
    check(overflow.status()==BootstrapStatus::Invalid && oversized.group_imports==0,
          "impossible immutable group count is rejected before buffer decoding");
}
}

int main()
{
    int failures=0,checks=0;
    const auto check=[&](bool valid,const char* label) { ++checks; if(!valid) { std::printf("FAIL %s\n",label); ++failures; } };
    bootstrap_checks(check);
    using tt::observation_detail::PackedMask;
    // Exhaust every in-bounds bit on non-byte-aligned rows and map tails. The
    // expected set is specified by coordinates, independently of byte offsets.
    bool masks_valid=true;
    for (int width=1;width<=19;++width)
        for (int height=1;height<=13;++height)
        {
            const auto count=static_cast<std::size_t>(width*height);
            for (std::size_t selected=0;selected<count;++selected)
            {
                std::vector<std::uint8_t> bytes(count/8+(count%8!=0));
                bytes[selected/8]=static_cast<std::uint8_t>(1u<<(selected%8));
                // Set every trailing padding bit. A row has no separate padding.
                if (count%8) bytes.back()|=static_cast<std::uint8_t>(0xffu<<(count%8));
                const PackedMask mask(width,height,std::move(bytes));
                const int chosen_x=static_cast<int>(selected%static_cast<std::size_t>(width));
                const int chosen_y=static_cast<int>(selected/static_cast<std::size_t>(width));
                for (int y=0;y<height;++y)
                    for (int x=0;x<width;++x)
                        masks_valid&=mask.test(x,y)==(x==chosen_x && y==chosen_y);
            }
        }
    check(masks_valid,"packed LSB mask exhausts tile positions across row/byte boundaries and nonzero tail padding");
    const auto rejects=[&](int width,int height,std::vector<std::uint8_t> bytes) {
        try { (void)PackedMask(width,height,std::move(bytes)); return false; }
        catch(const std::runtime_error&) { return true; }
    };
    check(rejects(0,1,{}) && rejects(1,0,{}) && rejects(-1,2,{}) && rejects(2,-1,{}) &&
          rejects(9,2,{0,0}) && rejects(9,2,{0,0,0,0}),
          "packed mask rejects invalid dimensions and short/long buffers");
    const PackedMask padded(9,2,{0,0,0xfc});
    bool outside_rejected=true;
    for (const auto point:std::array<std::array<int,2>,4>{{{{-1,0}},{{0,-1}},{{9,0}},{{0,2}}}})
    {
        try { (void)padded.test(point[0],point[1]); outside_rejected=false; }
        catch(const std::out_of_range&) {}
    }
    check(outside_rejected,"packed mask never exposes padding or an out-of-map coordinate");
    std::vector<std::uint8_t> exported{0x12,0x01};
    const auto* exported_storage=exported.data();
    PackedMask transferable(9,1,std::move(exported));
    check(transferable.test(1,0) && transferable.test(4,0) && transferable.test(8,0),
          "validated packed export remains readable before publication");
    const auto published=std::move(transferable).take_bytes();
    check(published.data()==exported_storage && published==std::vector<std::uint8_t>({0x12,0x01}),
          "Ready publication transfers the validated export allocation without another copy");
    using Point=mb::service::Point;
    struct ServicePacket
    {
        bool camp_presence_complete{true},ally_presence_complete{true},body_complete{};
        mb::service::Body service_body;
        std::vector<std::uint8_t> visible_bits,living_bits,awake_bits;
    } service;
    tt::observation_detail::declare_service_scope(service);
    check(!service.camp_presence_complete && !service.ally_presence_complete && !service.body_complete,
          "service scope declares omitted camp/ally families Unknown before any Ready publication");
    mb::service::Body service_body; service_body.pos={2,3}; service_body.hp=75; service_body.shield=12;
    PackedMask service_visible(9,1,{0x12,0x01}),service_living(9,1,{0x02,0x00}),service_awake(9,1,{0x00,0x00});
    const auto* service_storage=service_visible.view().data();
    check(!tt::observation_detail::publish_service_ready(service,service_body,4,5,Point{2,3},Point{2,3},
              service_visible,service_living,service_awake) && !service.body_complete && service.visible_bits.empty(),
          "service publication rejects a suspension epoch without publishing complete body/masks");
    check(!tt::observation_detail::publish_service_ready(service,service_body,4,4,Point{2,3},Point{3,3},
              service_visible,service_living,service_awake) && !service.body_complete,
          "service publication rejects actual displacement before moving buffers");
    check(tt::observation_detail::publish_service_ready(service,service_body,4,4,Point{2,3},Point{2,3},
              service_visible,service_living,service_awake) && service.body_complete &&
          service.service_body.hp==75 && service.service_body.shield==12 &&
          service.visible_bits.data()==service_storage && service.visible_bits==std::vector<std::uint8_t>({0x12,0x01}) &&
          service.living_bits==std::vector<std::uint8_t>({0x02,0x00}) && service.awake_bits==std::vector<std::uint8_t>({0x00,0x00}) &&
          !service.camp_presence_complete && !service.ally_presence_complete,
          "Ready service publication binds body and all three stable packed allocations while omitted families stay Unknown");
    struct WorkPacket
    {
        bool masks_complete{};
        std::vector<std::uint8_t> visible_bits,living_bits,awake_bits;
    } work_masks;
    std::optional<PackedMask> work_visible,work_living,work_awake;
    check(!tt::observation_detail::publish_work_masks(work_masks,true,4,4,Point{2,3},Point{2,3},
              work_visible,work_living,work_awake) && !work_masks.masks_complete,
          "borrowed Work may remain Ready without claiming ownership of original packet masks");
    work_visible.emplace(9,1,std::vector<std::uint8_t>{0x12,0x01});
    work_living.emplace(9,1,std::vector<std::uint8_t>{0x02,0x00});
    work_awake.emplace(9,1,std::vector<std::uint8_t>{0x00,0x00});
    const auto* work_storage=work_visible->view().data();
    check(!tt::observation_detail::publish_work_masks(work_masks,false,4,4,Point{2,3},Point{2,3},
              work_visible,work_living,work_awake) && !work_masks.masks_complete && work_masks.visible_bits.empty(),
          "incomplete selected work never publishes masks_complete");
    check(!tt::observation_detail::publish_work_masks(work_masks,true,4,5,Point{2,3},Point{2,3},
              work_visible,work_living,work_awake) &&
          !tt::observation_detail::publish_work_masks(work_masks,true,4,4,Point{2,3},Point{3,3},
              work_visible,work_living,work_awake) && !work_masks.masks_complete,
          "stale or displaced selected work cannot publish fresh mask authority");
    check(tt::observation_detail::publish_work_masks(work_masks,true,4,4,Point{2,3},Point{2,3},
              work_visible,work_living,work_awake) && work_masks.masks_complete &&
          work_masks.visible_bits.data()==work_storage && work_masks.visible_bits==service.visible_bits &&
          work_masks.living_bits==service.living_bits && work_masks.awake_bits==service.awake_bits,
          "fresh post-command Work transfers its already-exported masks without copying or altering packed contents");
    const std::vector<Point> hero_tiles{{4,4},{2,3},{8,1}};
    std::vector<Point> allies;
    std::vector<std::pair<Point,int>> enemies;
    int resolutions=0;
    const auto resolver=[&](Point tile) { ++resolutions; return tile.y*100+tile.x; };
    bool complete=tt::observation_detail::publish_heroes<false>(hero_tiles,Point{4,4},[]{return true;},
        [&](Point tile){allies.push_back(tile);},resolver);
    check(complete && resolutions==0 && allies.size()==2 && allies[0].x==2 && allies[0].y==3 &&
          allies[1].x==8 && allies[1].y==1,
          "actual ally publication preserves nonself positions without resolving any handle");
    complete=tt::observation_detail::publish_heroes<true>(hero_tiles,Point{4,4},[]{return true;},
        [&](Point tile,int handle){enemies.push_back({tile,handle});},resolver);
    check(complete && resolutions==2 && enemies.size()==2 &&
          enemies[0].first.x==2 && enemies[0].first.y==3 && enemies[0].second==302 &&
          enemies[1].first.x==8 && enemies[1].first.y==1 && enemies[1].second==108,
          "actual enemy publication resolves once per nonself tile and preserves handle-position association");
    enemies.clear(); resolutions=0;
    int hero_guards=0;
    complete=tt::observation_detail::publish_heroes<true>(hero_tiles,Point{4,4},[&]{return ++hero_guards<=1;},
        [&](Point tile,int handle){enemies.push_back({tile,handle});},resolver);
    check(!complete && hero_guards==2 && enemies.size()==1 && resolutions==1,
          "bounded hero publication stops before resolving or appending an unfunded suffix");
    const tt::observation_detail::PackedView view(9,1,published);
    check(view.data()==published.data() && view.test(1,0) && view.test(4,0) && view.test(8,0),
          "borrowed packed view reads the original allocation without copying");
    bool bad_view_rejected=false;
    try { (void)tt::observation_detail::PackedView(9,1,std::span<const std::uint8_t>(published.data(),1)); }
    catch(const std::runtime_error&) { bad_view_rejected=true; }
    check(bad_view_rejected,"borrowed packed view requires complete exact-length provenance");
    using tt::observation_detail::PacketPath;
    using tt::observation_detail::use_packet;
    int borrowed=0,refreshed=0;
    const auto borrow=[&]{++borrowed;};
    const auto refresh=[&]{++refreshed;};
    auto path=use_packet(true,true,true,4,4,Point{2,3},Point{2,3},borrow,refresh);
    check(path==PacketPath::Borrow && borrowed==1 && refreshed==0,
          "complete Ready packet at current epoch and actual origin uses zero refresh callbacks");
    path=use_packet(false,true,true,4,4,Point{2,3},Point{2,3},borrow,refresh);
    check(path==PacketPath::Refresh && borrowed==1 && refreshed==1,
          "post-command NeedObservation refreshes even when old body/mask storage remains complete");
    path=use_packet(true,true,true,4,5,Point{2,3},Point{2,3},borrow,refresh);
    check(path==PacketPath::Reject && borrowed==1 && refreshed==1,
          "owner epoch change rejects a prior Ready packet without borrowing or refreshing stale origin");
    path=use_packet(true,true,true,4,4,Point{2,3},Point{3,3},borrow,refresh);
    check(path==PacketPath::Reject && borrowed==1 && refreshed==1,
          "actual displacement rejects a Ready stamp before any borrowed callback");
    path=use_packet(true,true,false,4,4,Point{2,3},Point{2,3},borrow,refresh);
    check(path==PacketPath::Refresh && borrowed==1 && refreshed==2,
          "missing body or buffer provenance falls back to fresh resolution rather than borrowing");
    path=use_packet(false,false,true,4,4,Point{2,3},Point{2,3},borrow,refresh);
    check(path==PacketPath::Reject && borrowed==1 && refreshed==2,
          "invalid packet status cannot authorize a borrow or refresh branch");
    struct Spec { Point pos; float max_hp,strength; int range,group,member; };
    const Spec harmless{{5,4},120,0,3,6,2};
    int hp_queries=0,stun_queries=0;
    const auto hp=[&]{++hp_queries; return 17.0f;};
    const auto stun=[&]{++stun_queries; return 2;};
    auto observed=tt::observation_detail::member_reading(harmless,true,true,false,Point{4,4},hp,stun);
    check(observed.hp==17 && observed.hp_exact && hp_queries==1 && stun_queries==0,
          "sleeping harmless residual is exact and queried once without an irrelevant stun query");
    hp_queries=stun_queries=0;
    path=use_packet(true,true,true,4,4,Point{2,3},Point{2,3},[&] {
        observed=tt::observation_detail::member_reading(harmless,true,true,false,Point{4,4},hp,stun);
    },refresh);
    check(path==PacketPath::Borrow && hp_queries==1 && observed.hp==17 && observed.hp_exact && refreshed==2,
          "body/mask borrowing does not replace the selected operator's fresh exact camp HP query");
    hp_queries=stun_queries=0;
    observed=tt::observation_detail::member_reading(harmless,true,false,false,Point{4,4},hp,stun);
    check(observed.hp==0 && observed.hp_exact && hp_queries==0 && stun_queries==0,
          "visible absence is exact without querying an absent object");
    observed=tt::observation_detail::member_reading(harmless,false,true,true,Point{4,4},hp,stun);
    check(observed.hp==120 && !observed.hp_exact && !observed.awake && hp_queries==0,
          "unseen living-mask data cannot authorize exact HP or awake state");
    const Spec threat{{5,4},1350,20,3,6,1};
    observed=tt::observation_detail::member_reading(threat,true,true,true,Point{4,4},hp,stun);
    check(observed.hp==17 && observed.stun_remaining==2 && observed.awake && stun_queries==1,
          "in-range incoming source receives actual stun provenance");
    hp_queries=stun_queries=0;
    observed=tt::observation_detail::member_reading(threat,true,true,true,Point{20,20},hp,stun);
    check(observed.hp_exact && observed.stun_remaining==0 && hp_queries==1 && stun_queries==0,
          "out-of-range source keeps exact HP without an irrelevant stun read");
    bool error=false;
    try { (void)tt::observation_detail::member_reading(threat,true,true,true,Point{4,4},
        []()->float { throw std::runtime_error("error fixture failed query"); },stun); }
    catch(const std::runtime_error&) { error=true; }
    check(error,"unexpected health query failure propagates rather than inventing zero HP");
    error=false;
    try { (void)tt::observation_detail::member_reading(threat,true,true,true,Point{4,4},
        []{return std::numeric_limits<float>::quiet_NaN();},stun); }
    catch(const std::runtime_error&) { error=true; }
    check(error,"invalid queried HP cannot enter the pure service state");
    using tt::observation_detail::StationaryRefresh;
    using tt::observation_detail::SelectedLife;
    using tt::observation_detail::stationary_refresh_kind;
    using tt::observation_detail::refresh_selected_member;
    mb::service::State before_stationary;
    before_stationary.body.pos={4,4};
    before_stationary.body.ranged=true;before_stationary.body.can_move=false;
    before_stationary.member_count=1;
    before_stationary.members[0]={1,6,{5,4},17,20,3,true,2,true};
    mb::service::Primitive accepted_basic{mb::service::PrimitiveKind::basic,1,-1,{5,4}};
    check(stationary_refresh_kind(before_stationary,accepted_basic,false)==StationaryRefresh::ResolveSelected,
          "ranged basic's spent movement is not invented ordinary-walk history");
    check(stationary_refresh_kind(before_stationary,accepted_basic,true)==StationaryRefresh::Reject,
          "explicit movement ward equipment or vision invalidator rejects same-pose reuse");
    before_stationary.body.moved_this_turn=true;
    check(stationary_refresh_kind(before_stationary,accepted_basic,false)==StationaryRefresh::Reject,
          "retained actual ordinary-walk history independently rejects local reuse");
    before_stationary.body.moved_this_turn=false;
    before_stationary.spells[0].kind=mb::service::SpellKind::reset;
    mb::service::Primitive accepted_reset{mb::service::PrimitiveKind::cast,-1,0,{4,4}};
    check(stationary_refresh_kind(before_stationary,accepted_reset,false)==StationaryRefresh::PreserveSelected &&
          !before_stationary.body.can_move,
          "self reset permits selected-state retention but does not restore movement");
    before_stationary.spells[0].kind=mb::service::SpellKind::shield;
    check(stationary_refresh_kind(before_stationary,accepted_reset,false)==StationaryRefresh::PreserveSelected,
          "self shield keeps camp state while its own body and shield must refresh");
    before_stationary.spells[0].kind=mb::service::SpellKind::aoe;
    check(stationary_refresh_kind(before_stationary,accepted_reset,false)==StationaryRefresh::ResolveSelected,
          "accepted damaging AoE must resolve selected camp changes");
    before_stationary.spells[0].kind=mb::service::SpellKind::none;
    check(stationary_refresh_kind(before_stationary,accepted_reset,false)==StationaryRefresh::Reject,
          "unsupported cast cannot borrow the stationary continuation ticket");
    accepted_reset.slot=2;
    check(stationary_refresh_kind(before_stationary,accepted_reset,false)==StationaryRefresh::Reject,
          "invalid slot cannot inspect a selected spell");

    auto prior_member=before_stationary.members[0];
    int life_queries=0;
    hp_queries=stun_queries=0;
    auto local_member=refresh_selected_member(threat,prior_member,Point{4,4},
        [&]{++life_queries;return SelectedLife::Dead;},hp,stun);
    check(local_member.hp_exact && local_member.hp==0 && life_queries==1 &&
          hp_queries==0 && stun_queries==0,
          "fresh DEAD state is exact without live camp info or stun accessor");
    prior_member.hp_exact=false;life_queries=hp_queries=stun_queries=0;
    local_member=refresh_selected_member(threat,prior_member,Point{4,4},
        [&]()->SelectedLife{++life_queries;throw std::runtime_error("unexpected unseen probe");},hp,stun);
    check(!local_member.hp_exact && local_member.hp==threat.max_hp && life_queries==0 &&
          hp_queries==0 && stun_queries==0,
          "normal unknown support makes no exception-bearing state health or stun query");
    prior_member.hp_exact=true;
    local_member=refresh_selected_member(threat,prior_member,Point{4,4},
        [&]{++life_queries;return SelectedLife::Unknown;},hp,stun);
    check(!local_member.hp_exact && local_member.hp==threat.max_hp && life_queries==1 &&
          hp_queries==0 && stun_queries==0,
          "withdrawn visibility is Unknown rather than a death or stale exact HP");
    local_member=refresh_selected_member(threat,prior_member,Point{4,4},
        []{return SelectedLife::Awake;},hp,stun);
    check(local_member.hp_exact && local_member.hp==17 && local_member.awake &&
          local_member.stun_remaining==2 && hp_queries==1 && stun_queries==1,
          "fresh living state resolves actual HP awake state and in-range stun");
    error=false;
    try
    {
        (void)refresh_selected_member(threat,prior_member,Point{4,4},
            []()->SelectedLife{throw std::runtime_error("error state failure");},hp,stun);
    }
    catch(const std::runtime_error&){error=true;}
    check(error,"unexpected selected-state failure propagates to the turn boundary");
    const auto heart=tt::observation_detail::outgoing_strength(40,500,1000,true,0.5f);
    check(heart==50 && tt::observation_detail::outgoing_strength(40,1000,1000,true,0.5f)==40 &&
          tt::observation_detail::outgoing_strength(40,500,1000,false,0.5f)==40,
          "fresh missing-health basic multiplier applies only to the owning passive");
    std::printf("independent_observation: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
