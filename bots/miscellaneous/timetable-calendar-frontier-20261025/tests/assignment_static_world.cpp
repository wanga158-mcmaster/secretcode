#include "../src/assignment_static_world.h"
#include "../src/production_potential.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace {
namespace sw=tt::assignment_static_world;
namespace sg=tt::assignment_static_geometry;
namespace ag=tt::assignment_geometry;
namespace pp=mb::production::potential;
int checks{},failures{};
void check(bool value,const char* message) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}
}
struct WorldPoint {int x{},y{};};
struct WorldMember {
    WorldPoint pos;float max_hp{},strength{};int range{};
    bool alive{},awake{}; // Not a premise of the immutable all-NPC upper.
};
struct WorldGroup {std::vector<WorldMember> members;};
struct WorldMock {
    int width{9},height{7};std::vector<WorldGroup> groups;
    std::vector<WorldPoint> altars,spawn_tiles;
};
using Builder=sw::Builder<WorldMock,pp::Field>;
inline constexpr std::uint64_t revision=77;
const std::array<int,2> canonical{{1,0}};

WorldMock world() {
    WorldMock out;
    // Actual local enumeration is east first. Canonical source0 is west; the
    // explicit inverse order must govern both catalogue and closed spawn map.
    out.groups={WorldGroup{{{{4,3},350,7,2,false,false}}},
                WorldGroup{{{{3,2},210,13,1,false,false},{{3,3},180,5,1,false,false}}}};
    out.altars={{0,6}};out.spawn_tiles={{0,0}};return out;
}
std::vector<pp::Source> sources(const WorldMock& value) {
    std::vector<pp::Source> result;
    for(const auto& group:value.groups)for(const auto& member:group.members)
        result.push_back({{member.pos.x,member.pos.y},member.strength,member.range});
    return result;
}
void finish_field(pp::Field& field) {
    for(unsigned unit=0;unit<100000&&field.status()==pp::Status::NeedCompute;++unit) {
        field.advance(1);check(field.last_work()<=1,"real all-NPC field obeys its one-unit source/cell bound");
        if(field.status()!=pp::Status::Ready)
            check(field.at({0,0}).status!=pp::Status::Ready,"unfinished real potential does not publish a zero prefix");
    }
    check(field.status()==pp::Status::Ready,"real complete all-NPC Field finishes the fixture");
}
std::vector<std::uint8_t> packed(const WorldMock& value,
    std::initializer_list<WorldPoint> extra={}) {
    const auto cells=static_cast<std::size_t>(value.width)*value.height;
    std::vector<std::uint8_t> result(cells/8+(cells%8!=0),0);
    const auto block=[&](WorldPoint point) {
        const auto index=static_cast<std::size_t>(point.y)*value.width+point.x;
        result[index/8]|=static_cast<std::uint8_t>(std::uint8_t{1}<<(index%8));
    };
    block({7,0});block({8,0});block({8,6});
    // The real World terrain is walls|holes|altars|enemy-spawn, not walls alone.
    for(const auto point:value.altars)block(point);
    for(const auto point:extra)block(point);
    // 9*7 has one unused trailing SDK bit. It is not a64th map cell.
    result.back()|=std::uint8_t{0x80};return result;
}
void terminal(Builder& builder,std::uint64_t current=revision,std::size_t slice=1) {
    for(unsigned call=0;call<100000&&builder.status()==ag::Status::NeedCompute;++call) {
        const auto before=builder.work_used();builder.advance(current,slice);
        check(builder.last_work()<=slice&&builder.work_used()-before==builder.last_work(),
            "staged World capture charges only the requested retained units");
        if(builder.status()!=ag::Status::Ready)
            check(!builder.result(),"partial cells/catalogue/roots/fields never publish StaticGeometry");
    }
    check(builder.status()!=ag::Status::NeedCompute,"finite staged World capture reaches a terminal state");
}
std::shared_ptr<const sg::StaticGeometry> snapshot(const WorldMock& value,const pp::Field& field,
    std::uint64_t current=revision,std::initializer_list<WorldPoint> extra={}) {
    Builder builder{value,field,canonical,current,packed(value,extra)};terminal(builder,current);
    check(builder.status()==ag::Status::Ready,"real Field plus complete World produces sealed StaticGeometry");
    return builder.result();
}
void packed_capture_and_canonical_catalogue() {
    auto value=world();pp::Field field(value.width,value.height,sources(value));finish_field(field);
    auto bytes=packed(value);
    check(bytes.size()==8&&(bytes[0]&0x80)!=0&&(bytes[1]&1)!=0&&(bytes.back()&0xc0)==0xc0,
        "fixture independently places LSB-first packed terrain across bytes7/8 and ignores padding63");
    Builder builder{value,field,canonical,revision,std::move(bytes)};
    check(builder.status()==ag::Status::NeedCompute&&builder.work_used()==0&&!builder.result(),
        "constructor copies bounded order but performs no staged allocation/cell work");
    builder.advance(revision,0);
    check(builder.last_work()==0&&builder.work_used()==0&&!builder.result(),"zero units cannot capture even a first cell");
    builder.advance(revision,1);
    check(builder.status()==ag::Status::NeedCompute&&builder.last_work()==1&&builder.work_used()==1&&!builder.result(),
        "first individually admitted storage reserve remains behind the publication fence");
    builder.advance(revision,66);
    check(builder.status()==ag::Status::NeedCompute&&builder.work_used()==67&&!builder.result(),
        "a completely copied terrain/raw grid still lacks canonical catalogue and both complete root fields");
    terminal(builder);
    const auto data=builder.result();if(!data)return;
    const auto& grid=data->grid();const auto& key=data->key();
    check(data->size()==63&&grid.terrain.size()==63&&grid.camp_group.size()==63&&grid.raw_upper.size()==63,
        "padding does not fabricate a tile or shorten the captured grid");
    for(int y=0;y<value.height;++y)for(int x=0;x<value.width;++x) {
        const ag::Point point{x,y};const auto i=data->index(point);const auto expected=field.at({x,y});
        const bool blocked=(x==7&&y==0)||(x==8&&y==0)||(x==8&&y==6)||(x==0&&y==6);
        check(grid.terrain[i]==static_cast<std::uint8_t>(blocked)&&expected.status==pp::Status::Ready&&
            grid.raw_upper[i]==expected.upper,"every captured cell preserves packed terrain and exact complete Field upper");
    }
    check(data->catalogue().size()==2&&data->catalogue()[0].canonical==0&&data->catalogue()[1].canonical==1&&
        data->catalogue()[0].members.size()==2&&data->catalogue()[1].members.size()==1&&
        data->catalogue()[0].members[0].pos==ag::Point{3,2}&&data->catalogue()[0].members[0].max_hp==210&&
        data->catalogue()[0].members[0].strength==13&&data->catalogue()[0].members[0].range==1&&
        data->catalogue()[1].members[0].pos==ag::Point{4,3},
        "reversed local group enumeration is sealed in the supplied canonical order with full NPC metadata");
    check(grid.camp_group[data->index({3,2})]==0&&grid.camp_group[data->index({3,3})]==0&&
        grid.camp_group[data->index({4,3})]==1&&grid.camp_group[data->index({0,0})]==-1,
        "canonical IDs name the exact actual spawn cells, not the World vector slot");
    check(grid.exclusive_ownership&&grid.require_quiet_bank&&key.require_quiet_bank&&key.graph_policy==1&&
        key.map&&key.layout&&key.catalogue&&key.roots&&key.potential,
        "sealed immutable support has explicit nonzero keys and quiet-bank graph policy");
    check(data->bank_field().ready()&&data->home_field().ready()&&
        data->bank_field().distance(data->index({1,1}))!=ag::detail::none&&
        data->home_field().distance(data->index({1,1}))!=ag::detail::none,
        "Ready includes both fully computed actual all-spawn-closed bank/home fields");
    check(!data->closed(data->index({3,2}))&&!data->closed(data->index({4,3}))&&
        !data->closed(data->index({0,6}))&&!data->closed(data->index({8,6})),
        "NPC spawn, altar and terrain obstacles remain closed in completed graph fields");
    const auto overlap=data->raw_upper({3,2}),quiet=data->raw_upper({0,6});
    check(overlap&&*overlap>=25&&*overlap-25<1e-10&&quiet&&*quiet==0,
        "all three fixed NPC sources overlap conservatively without current Life/wake discounts");
    const auto work=builder.work_used();builder.advance(revision,1000);
    check(builder.last_work()==0&&builder.work_used()==work&&builder.result().get()==data.get(),
        "completed sealed geometry is retained without recopy/rebuild");
}
void independent_keys_and_prior_immutability() {
    auto value=world();pp::Field field(value.width,value.height,sources(value));finish_field(field);
    auto first=snapshot(value,field);auto terrain=snapshot(value,field,revision,{{5,5}});
    auto changed_potential=snapshot(value,field,revision+1);
    if(!first||!terrain||!changed_potential)return;
    const auto original=first->key();const auto original_upper=*first->raw_upper({3,2});
    check(terrain->key().layout!=original.layout&&terrain->key().map!=original.map&&
        terrain->key().catalogue==original.catalogue&&terrain->key().roots==original.roots&&
        terrain->key().potential==original.potential,
        "changed terrain changes layout/map only; it cannot inherit a stale layout from identical NPC catalogue/roots");
    check(changed_potential->key().potential!=original.potential&&changed_potential->key().layout==original.layout&&
        changed_potential->key().map==original.map&&changed_potential->key().catalogue==original.catalogue&&
        changed_potential->key().roots==original.roots,
        "a new complete potential revision has a distinct field key while immutable map/catalogue/roots agree");
    check(first->grid().terrain[first->index({5,5})]==0&&terrain->grid().terrain[terrain->index({5,5})]==1&&
        first->key()==original&&*first->raw_upper({3,2})==original_upper,
        "new cache construction cannot edit the pinned previous terrain/key/potential");
    // The builders above are dead; their immutable capture inputs may now be
    // replaced. The pinned result owns its own catalogue and all copied fields.
    value.groups[1].members[0].max_hp=999;value.groups[1].members[0].strength=99;
    value.altars[0]={8,0};value.spawn_tiles[0]={8,6};
    check(first->catalogue()[0].members[0].max_hp==210&&first->catalogue()[0].members[0].strength==13&&
        first->grid().altars[0]==ag::Point{0,6}&&first->grid().home_roots[0]==ag::Point{0,0}&&
        first->key()==original&&*first->raw_upper({3,2})==original_upper,
        "pinned prior geometry contains no borrowed World member/root arrays");
    std::shared_ptr<const sg::StaticGeometry> detached;
    {auto local=world();pp::Field local_field(local.width,local.height,sources(local));finish_field(local_field);detached=snapshot(local,local_field);}
    check(detached&&detached->catalogue()[1].members[0].strength==7&&detached->raw_upper({3,2})&&
        *detached->raw_upper({3,2})>=25,"sealed geometry survives destruction of World and real potential inputs");
}
void stale_revision_never_resumes_a_partial_copy() {
    auto value=world();pp::Field field(value.width,value.height,sources(value));finish_field(field);
    Builder builder{value,field,canonical,revision,packed(value)};
    builder.advance(revision,3);const auto work=builder.work_used();
    check(builder.status()==ag::Status::NeedCompute&&!builder.result(),"stale test starts with a real partial copied prefix");
    check(builder.advance(revision+1,1)==ag::Status::Stale&&builder.reason()==ag::Reason::Changed&&
        builder.last_work()==0&&builder.work_used()==work&&!builder.result(),
        "mid-copy potential revision drift withdraws before consuming or publishing mixed data");
    builder.advance(revision,100000);
    check(builder.status()==ag::Status::Stale&&builder.last_work()==0&&builder.work_used()==work&&!builder.result(),
        "an old revision cannot revive the already stale capture cursor");
}
void invalid_inputs_are_not_geometry() {
    auto value=world();pp::Field field(value.width,value.height,sources(value));finish_field(field);
    for(const auto order:std::array<std::array<int,2>,3>{{{{0,0}},{{0,2}},{{-1,0}}}}) {
        Builder builder{value,field,order,revision,packed(value)};
        check(builder.status()==ag::Status::Unsupported&&builder.reason()==ag::Reason::Group&&!builder.result(),
            "duplicate/out-of-range/negative canonical orders reject before a source is aliased");
    }
    const std::array<int,1> short_order{{0}};
    Builder short_groups{value,field,short_order,revision,packed(value)};
    check(short_groups.status()==ag::Status::Unsupported&&short_groups.reason()==ag::Reason::Shape,
        "canonical order must cover every supplied source group");
    Builder bad_terrain{value,field,canonical,revision,std::vector<std::uint8_t>(7,0)};
    check(bad_terrain.status()==ag::Status::Unsupported&&bad_terrain.reason()==ag::Reason::Shape,
        "unsupported packed/dense terrain byte count cannot publish missing cells as open");
    auto bad_shape=value;bad_shape.width=0;
    Builder shape{bad_shape,field,canonical,revision,{}};
    check(shape.status()==ag::Status::Unsupported&&shape.reason()==ag::Reason::Shape,"invalid map dimensions fail explicitly");
    Builder no_revision{value,field,canonical,0,packed(value)};
    check(no_revision.status()==ag::Status::Unsupported,"a missing complete-potential revision cannot name a reusable field");
    sg::Limits limits;limits.cells=62;
    Builder capacity{value,field,canonical,revision,packed(value),limits};
    check(capacity.status()==ag::Status::Unsupported&&capacity.reason()==ag::Reason::Capacity,
        "declared geometry capacity rejects before any copied cell becomes visible");
    pp::Field unfinished(value.width,value.height,sources(value));unfinished.advance(4);
    Builder not_ready{value,unfinished,canonical,revision,packed(value)};terminal(not_ready);
    check(not_ready.status()==ag::Status::Unsupported&&not_ready.reason()==ag::Reason::Number&&!not_ready.result(),
        "an actual unfinished Field cannot donate even initialized zeros as complete all-NPC support");
    pp::Field too_small(1,1,{});finish_field(too_small);
    Builder partial_field{value,too_small,canonical,revision,packed(value)};terminal(partial_field);
    check(partial_field.status()==ag::Status::Unsupported&&!partial_field.result(),
        "a completed but incomplete-extent Field cannot silently fill the rest of the World with zero");
    auto duplicate=value;duplicate.groups[0].members[0].pos=duplicate.groups[1].members[0].pos;
    Builder duplicate_spawn{duplicate,field,canonical,revision,packed(duplicate)};terminal(duplicate_spawn);
    check(duplicate_spawn.status()==ag::Status::Unsupported&&duplicate_spawn.reason()==ag::Reason::Group&&!duplicate_spawn.result(),
        "one physical spawn cell cannot belong to two canonical groups");
    auto empty=value;empty.groups[0].members.clear();
    Builder missing_members{empty,field,canonical,revision,packed(empty)};terminal(missing_members);
    check(missing_members.status()==ag::Status::Unsupported&&missing_members.reason()==ag::Reason::Group,
        "empty declared source groups do not become usable static geometry");
}
void charged_bank_has_a_distinct_quiet_recovery_family() {
    auto value=world();value.groups[0].members[0].pos={3,5};value.groups[0].members[0].range=3;
    value.spawn_tiles.push_back({1,5});
    pp::Field field(value.width,value.height,sources(value));finish_field(field);
    Builder old_family{value,field,canonical,revision,packed(value)};terminal(old_family);
    check(old_family.status()==ag::Status::Unsupported&&old_family.reason()==ag::Reason::Roots,
        "old quiet-bank family remains unsupported when every bank stand is charged");
    Builder charged{value,field,canonical,revision,packed(value),{},sw::Policy{false,true}};terminal(charged);
    check(charged.status()==ag::Status::Ready,"charged geometry nominates complete bank and quiet recovery paths");
    const auto data=charged.result();if(!data)return;
    check(!data->key().require_quiet_bank&&data->key().require_quiet_home,
        "bank/recovery family policies are explicit immutable key dependencies");
    check(data->bank_field().distance(data->index({1,5}))==0&&*data->raw_upper({1,5})>0,
        "a charged bank stand is geometry only, not free channel authority");
    check(data->home_field().distance(data->index({0,0}))==0&&data->home_field().distance(data->index({1,5}))>0,
        "charged own-spawn cells cannot become free-heal recovery roots");
    value.spawn_tiles={{1,5}};
    Builder no_recovery{value,field,canonical,revision,packed(value),{},sw::Policy{false,true}};terminal(no_recovery);
    check(no_recovery.status()==ag::Status::Unsupported&&no_recovery.reason()==ag::Reason::Roots,
        "charged banking without a supported quiet recovery endpoint remains unsupported");
}
} // namespace
int main() {
    packed_capture_and_canonical_catalogue();independent_keys_and_prior_immutability();
    stale_revision_never_resumes_a_partial_copy();invalid_inputs_are_not_geometry();
    charged_bank_has_a_distinct_quiet_recovery_family();
    std::printf("assignment static World (real potential, pure): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
