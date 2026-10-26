#define TT_SAFETY_PURE
#include "../src/independent_safety.cpp"
#include <cstdio>

int main()
{
    using namespace tt::safety_detail;
    int failed=0,checks=0;
    const auto check=[&](bool value){ ++checks; if (!value) ++failed; };
    std::vector<std::uint8_t> packet_visible{1,0,32},packet_living{1,0,32},packet_awake{0,0,32};
    const mb::service::Point packet_origin{2,3},packet_other{3,3};
    check(can_borrow_packet(true,true,7,7,packet_origin,packet_origin,11,2,3,3,3));
    check(!can_borrow_packet(false,true,7,7,packet_origin,packet_origin,11,2,3,3,3)); // NeedObservation
    check(!can_borrow_packet(true,false,7,7,packet_origin,packet_origin,11,2,3,3,3));
    check(!can_borrow_packet(true,true,7,8,packet_origin,packet_origin,11,2,3,3,3));
    check(!can_borrow_packet(true,true,7,7,packet_origin,packet_other,11,2,3,3,3));
    check(!can_borrow_packet(true,true,7,7,packet_origin,packet_origin,11,2,2,3,3));
    check(!can_borrow_packet(true,true,7,7,packet_origin,packet_origin,11,2,3,4,3));
    check(!can_borrow_packet(true,true,7,7,packet_origin,packet_origin,11,2,3,3,0));
    const MaskView visible_view{11,2,packet_visible},living_view{11,2,packet_living},awake_view{11,2,packet_awake};
    const auto last=packet_presence(visible_view,living_view,awake_view,10,1);
    check(last.visible && last.living && last.awake);
    const auto hidden=packet_presence(visible_view,living_view,awake_view,9,1);
    check(!hidden.visible && possible_alive(hidden) && possible_awake(hidden,true,false,20));
    const auto sleeping=packet_presence(visible_view,living_view,awake_view,0,0);
    check(sleeping.visible && sleeping.living && !possible_awake(sleeping,true,true,20));
    std::size_t checkpoints=0;
    for (std::size_t i=0;i<160;++i) checkpoints+=source_checkpoint(i);
    check(checkpoints==10 && source_checkpoint(0) && !source_checkpoint(15) && source_checkpoint(16));
    struct GeometryGroup { std::vector<Source> members; };
    const std::vector<GeometryGroup> geometry{{{
        {0,0,{0,0},40,3,0,false,false,false},
        {0,1,{10,10},20,2,0,false,false,false},
        {0,2,{4,4},0,100,0,true,true,false}}}};
    WitnessScratch reusable;
    check(reusable.prepare(geometry));
    const auto initial_flags=reusable.flags.data();
    const auto initial_tags=reusable.queried.data();
    const auto initial_wakes=reusable.new_wake.data();
    const auto initial_sources=reusable.sources.data();
    const auto initial_offsets=reusable.offsets.data();
    int live_reads=0;
    const auto first_flags=reusable.presence(0,[&] { ++live_reads; return CampPresence{true,true,true}; });
    check(first_flags.awake && live_reads==1);
    check(reusable.presence(0,[&] { ++live_reads; return CampPresence{}; }).awake && live_reads==1);
    reusable.new_wake[0]=1;
    reusable.sources.push_back({0,0,{0,0},999,3,0,true,true,true});
    check(reusable.prepare(geometry));
    check(reusable.sources.empty() && reusable.new_wake[0]==0 && reusable.queried[0]==0);
    const auto next_flags=reusable.presence(0,[&] { ++live_reads; return CampPresence{true,false,false}; });
    check(!next_flags.living && !next_flags.awake && live_reads==2);
    check(reusable.flags.data()==initial_flags && reusable.queried.data()==initial_tags &&
        reusable.new_wake.data()==initial_wakes && reusable.sources.data()==initial_sources &&
        reusable.offsets.data()==initial_offsets);
    auto different_geometry=geometry;
    different_geometry[0].members.push_back({});
    check(!reusable.prepare(different_geometry)); // unknown shape, never stale reuse
    // Unknown/invisible/inactive flags cannot remove a possible harmful range.
    check(!immutable_quiet_point(geometry,{3,3}));
    check(immutable_quiet_point(geometry,{4,3}));
    check(!immutable_quiet_point(geometry,{8,8}));
    check(immutable_quiet_point(geometry,{7,8}));
    check(immutable_quiet_point(std::vector<GeometryGroup>{},{0,0}));
    enum class Entry { Ready, NeedCompute, NeedObservation, Stale, Unreachable };
    const mb::service::Point here{2,3},elsewhere{3,3};
    int stamp_epoch=7;
    check(position_epoch_current(here,7,[&] { return here; },[&] { return stamp_epoch; }));
    check(!position_epoch_current(here,7,[&] { ++stamp_epoch; return here; },
        [&] { return stamp_epoch; })); // unchanged position query resumed in next epoch
    check(!position_epoch_current(here,8,[&] { return elsewhere; },[&] { return stamp_epoch; }));
    Entry global=Entry::NeedObservation;
    // Applied basic invalidated global facts; a fresh same-origin work frame can
    // authorize fresh coverage without promoting that global snapshot to Ready.
    check(entry_status(global,7,7,here,here,true,Entry::Ready,7,here)==Entry::Ready);
    check(global==Entry::NeedObservation);
    check(entry_status(global,7,7,here,here,false,Entry::Ready,7,here)==Entry::Ready);
    check(entry_status(global,7,8,here,here,true,Entry::Ready,7,here)==Entry::Stale);
    check(entry_status(global,7,7,here,elsewhere,true,Entry::Ready,7,elsewhere)==Entry::Stale);
    check(entry_status(global,7,7,here,here,true,Entry::NeedObservation,7,here)==Entry::NeedObservation);
    check(entry_status(global,7,7,here,here,true,Entry::Ready,6,here)==Entry::Stale);
    check(entry_status(global,7,7,here,here,true,Entry::Ready,7,elsewhere)==Entry::NeedObservation);
    check(entry_status(Entry::NeedCompute,7,7,here,here,true,Entry::Ready,7,here)==Entry::NeedCompute);
    // Actual AoE center, not target distance, determines collateral membership.
    const std::vector<Candidate> candidates{
        {0,{0,0},true},{1,{2,0},true},{1,{9,0},true},{2,{3,0},true},{3,{1,1},false}};
    const auto woken=wake_groups(candidates,{true,{1,0},1},4);
    check(woken[0] && woken[1] && !woken[2] && !woken[3]);
    mb::service::State command;
    command.member_count=1;
    command.members[0].id=7;
    command.members[0].pos={0,0};
    command.body.splash=true;
    command.spells[0].kind=mb::service::SpellKind::aoe;
    command.spells[0].radius=1;
    mb::service::Primitive cast{mb::service::PrimitiveKind::cast,7,0,{1,0}};
    const auto actual_cast=command_footprint(command,&cast);
    check(actual_cast.center.x==1 && actual_cast.radius==1);
    check(wake_groups(candidates,actual_cast,4)==woken);
    mb::service::Primitive basic{mb::service::PrimitiveKind::basic,7,-1,{99,99}};
    const auto actual_basic=command_footprint(command,&basic);
    check(actual_basic.center.x==0 && actual_basic.center.y==0 && actual_basic.radius==1);
    command.spells[0].kind=mb::service::SpellKind::shield;
    check(!command_footprint(command,&cast).hit);
    command.spells[0].kind=mb::service::SpellKind::reset;
    check(!command_footprint(command,&cast).hit);
    check(stationary_command(command,&basic));
    check(stationary_command(command,&cast));
    // The same quiet point remains no-incoming after an applied basic invalidates
    // global facts, provided a fresh same-origin work frame refines the next cast.
    check(immutable_quiet_point(geometry,{4,3}) && stationary_command(command,&cast) &&
        entry_status(Entry::NeedObservation,7,7,here,here,true,Entry::Ready,7,here)==Entry::Ready);
    command.spells[0].kind=mb::service::SpellKind::none;
    check(!stationary_command(command,&cast)); // unsupported moving spell family
    cast.slot=2;
    check(!stationary_command(command,&cast));
    cast.slot=0;
    basic.member_id=8;
    check(!command_footprint(command,&basic).hit);
    // Group1's harmless struck cluster at2,0 wakes its distant harmful sibling.
    std::vector<Source> sources{{1,0,{2,0},0,3,0,true,static_cast<bool>(woken[1]),false},
                                {1,1,{9,0},40,8,0,true,static_cast<bool>(woken[1]),false}};
    check(rate(sources,{1,0})==40.0f);
    check(wake_groups(candidates,{false,{1,0},1},4)==std::vector<unsigned char>(4,0));
    // Null damage and actual dead candidates cannot create a new group wake.
    check(wake_groups(candidates,{true,{1,1},0},4)[3]==0);

    // D=4 exposed volleys decompose into currentp0 plus3 suffix volleys.
    const std::vector<Source> constant{{0,0,{0,0},10,3,0,true,true,true}};
    const std::vector<Point> path{{0,0},{1,0},{2,0},{3,0},{4,0}};
    check(suffix_damage(constant,path,1.0f)==30.0f);
    check(rate(constant,path.front())+suffix_damage(constant,path,1.0f)==40.0f);
    check(suffix_damage(constant,{{0,0},{4,0}},1.0f)==0.0f);
    check(suffix_damage(constant,{{4,0}},1.0f)==0.0f);
    check(suffix_damage(constant,path,0.5f)==15.0f);
    const std::vector<Source> changing{{0,0,{0,0},10,1,0,true,true,false},
                                      {1,0,{3,0},30,0,0,true,true,false}};
    check(suffix_damage(changing,path,1.0f)==40.0f);
    // Current stun may deny current volley; future suffix grants no persistence.
    auto stunned=constant;
    stunned[0].stun=1;
    check(rate(stunned,{0,0},false,true)==0.0f);
    check(suffix_damage(stunned,path,1.0f)==30.0f);
    // The actual global-packet consumer must not credit an unqueried stun.
    stunned[0].stun=observed_stun(5,false);
    check(rate(stunned,{0,0},false,true)==10.0f);
    stunned[0].stun=observed_stun(5,true);
    check(rate(stunned,{0,0},false,true)==0.0f);
    // Current known sources are separately charged by the micro component.
    auto mixed=constant;
    mixed.push_back({1,0,{0,0},20,3,0,true,true,false});
    check(rate(mixed,{0,0},true,true)==20.0f);
    check(rate(mixed,{0,0},false,true)==30.0f);
    check(rate(mixed,{4,0})==0.0f);
    mixed[1].alive=false;
    check(rate(mixed,{0,0},true,true)==0.0f);

    // The witness support includes every landing, even a source that cannot
    // reach the current point. Its current sleep does not prove future quiet.
    const std::vector<Point> return3{{0,0},{1,0},{2,0},{3,0}};
    check(intersects_landings({2,1},1,std::span<const Point>{return3}));
    check(!intersects_landings({2,1},1,std::span<const Point>{return3}.first(1)));
    check(!intersects_landings({8,8},1,std::span<const Point>{return3}));
    const auto open=[](Point,std::size_t) { return WitnessCell::Open; };
    check(validate_witness_path(std::span<const Point>{return3},{0,0},10,10,open)==WitnessPath::Valid);
    check(validate_witness_path(std::span<const Point>{return3},{1,0},10,10,open)==WitnessPath::Malformed);
    check(validate_witness_path(std::span<const Point>{},{0,0},10,10,open)==WitnessPath::Malformed);
    const std::vector<Point> gap{{0,0},{2,0}};
    check(validate_witness_path(std::span<const Point>{gap},{0,0},10,10,open)==WitnessPath::Malformed);
    check(validate_witness_path(std::span<const Point>{return3},{0,0},2,10,open)==WitnessPath::Malformed);
    check(validate_witness_path(std::span<const Point>{return3},{0,0},10,10,
        [](Point p,std::size_t) { return p.x==2 ? WitnessCell::Blocked : WitnessCell::Open; })==WitnessPath::Blocked);
    check(validate_witness_path(std::span<const Point>{return3},{0,0},10,10,
        [](Point,std::size_t i) { return i==1 ? WitnessCell::Unknown : WitnessCell::Open; })==WitnessPath::Unknown);
    const std::vector<Point> quiet_only{{4,0}};
    check(validate_witness_path(std::span<const Point>{quiet_only},{4,0},10,10,open)==WitnessPath::Valid);

    // A harmless hit wakes a whole external group. Keep only its harmful sibling
    // supported by the return cone; unseen possible awake is conservative too.
    const std::vector<Candidate> cone_hits{{1,{0,1},true}};
    const auto cone_wake=wake_groups(cone_hits,{true,{0,1},0},2);
    std::vector<Source> cone_sources{{1,1,{1,1},12,1,0,true,cone_wake[1]!=0,false}};
    check(rate(cone_sources,{0,0},true,true)==12.0f);
    check(suffix_damage(cone_sources,return3,1.0f)==24.0f);
    cone_sources[0].awake=possible_awake({},true,false,12);
    check(rate(cone_sources,{0,0},true,true)==12.0f);
    check(!immutable_quiet_point(std::vector<GeometryGroup>{{cone_sources}},{2,0}));
    check(immutable_quiet_point(std::vector<GeometryGroup>{{cone_sources}},{3,0}));

    // Three ordinary exit steps charge p0 once plus p1/p2; p3 is quiet. An
    // observed shield is usable only at p0, never on every later landing.
    const std::vector<Source> three_volleys{{0,0,{0,0},10,2,0,true,true,false}};
    const float exit_suffix=suffix_damage(three_volleys,return3,1.0f);
    check(exit_suffix==20.0f);
    const auto no_work=witness_damage(31,4,0,rate(three_volleys,{0,0}),exit_suffix,false);
    check(no_work.known_next==0 && no_work.current==6 && no_work.reserve==26 && no_work.safe);
    check(!witness_damage(26,4,0,10,20,false).safe); // strict positive residual HP
    const auto zero=witness_damage(1,100,0,0,0,false);
    check(zero.current==0 && zero.reserve==0 && zero.safe);
    mb::service::State shielded;
    shielded.body.pos={0,0}; shielded.body.hp=100; shielded.body.shield=30;
    shielded.member_count=1;
    shielded.members[0]={0,0,{0,1},100,40,1,true,0,true};
    shielded.incoming_complete=true;
    const auto known_volley=mb::service::next_camp_volley(shielded);
    check(known_volley.complete && known_volley.hp_damage==10);
    const auto with_work=witness_damage(shielded.body.hp,shielded.body.shield,
        known_volley.hp_damage,20,40,true);
    check(with_work.known_next==10 && with_work.external_next==20 &&
        with_work.current==30 && with_work.reserve==60 && with_work.safe);
    // Fresh WorkReady after a command supports the body; it cannot repair stale
    // origin/epoch or change a NeedObservation mask packet into Ready.
    check(entry_status(global,7,7,here,here,true,Entry::Ready,7,here)==Entry::Ready &&
        global==Entry::NeedObservation &&
        !can_borrow_packet(false,true,7,7,here,here,11,2,3,3,3));
    std::printf("independent safety: %d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
