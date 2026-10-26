#include "residual_observation.h"
#include "config.h"
#include "residual_probe.h"
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace residual::observation {
namespace {
constexpr std::uint64_t reserve=12000;
bool same(int epoch) {
    if(epoch<0)return false;const auto actual=cambc::round_number();
    if constexpr(tt::cfg::residual_probe)probe::clock(epoch,static_cast<int>(actual));
    return actual==static_cast<unsigned>(epoch);
}
bool funded(int epoch,std::uint64_t leaf=2000) {
    if(!same(epoch))return false;const auto fuel=cambc::fuel_remaining();
    if constexpr(tt::cfg::residual_probe){probe::budget(epoch,reserve+leaf,fuel);probe::offer(epoch,reserve+leaf,fuel,probe::write);}
    return fuel>reserve+leaf&&same(epoch);
}
ReadStatus interrupted(int epoch) {return same(epoch)?ReadStatus::NeedCompute:ReadStatus::Stale;}
bool unknown(std::int32_t code) {
    return code==static_cast<int>(cambc::error_code::api_not_in_vision)||
        code==static_cast<int>(cambc::error_code::api_not_found);
}
bool finite(float value) {return std::isfinite(value)&&value>=0;}
Role role(int type) {
    using T=cambc::champion_type;
    switch(static_cast<T>(type)) {
    case T::varg:return Role::Varg;case T::marcella:return Role::Marcella;
    case T::ryth:return Role::Ryth;case T::vesper_vale:return Role::Vesper;
    case T::heart_of_ruin:return Role::Heart;default:return Role::Other;
    }
}
bool body_record(const cambc::detail::champion_info& raw) {
    return finite(raw.health)&&finite(raw.max_health)&&raw.max_health>0&&raw.health<=raw.max_health&&
        finite(raw.strength)&&finite(raw.focus)&&raw.attack_range>=0;
}
void clear(Frame& frame,int epoch,bool walked) {
    frame.ready=false;frame.epoch=epoch;frame.role=Role::Other;frame.body=Body{};frame.body.walked=walked;
    frame.spells={};frame.gold=frame.gems=0;
    frame.alive=frame.own_spawn=frame.submitting=frame.recalling=frame.teleporting=false;
    frame.channel_remaining=0;frame.camps.clear();frame.groups.clear();frame.foes.clear();frame.allies.clear();
}
// One slot has a fixed-size payload and at most two live imports. The caller
// funds/fences each slot separately. Amount already contains caster passive;
// only the separately observed Monster item effect is added for camp damage.
bool spell(const cambc::ability& ability,Spell& out,bool can_cast,float camp_bonus) {
    out={};out.slot=ability.slot();const auto& info=ability.info();if(!info)return true;
    const auto cooldown=ability.cooldown();if(cooldown<0)return false;
    out.cooldown=static_cast<unsigned>(cooldown);
    bool valid=true;
    std::visit([&](const auto& typed) {
        using T=std::decay_t<decltype(typed)>;
        if(typed.max_cooldown<0){valid=false;return;}
        out.period=static_cast<unsigned>(typed.max_cooldown);out.exclusive=typed.exclusive_with_movement;
        out.target_camps=typed.valid_targets.contains(cambc::ability_target_mask::camps);
        out.target_self=typed.valid_targets.contains(cambc::ability_target_mask::self);
        out.target_ground=typed.valid_targets.contains(cambc::ability_target_mask::ground);
        out.target_enemies=typed.valid_targets.contains(cambc::ability_target_mask::enemies);
        if constexpr(std::is_same_v<T,cambc::ability_attack_reset>) {
            out.kind=SpellKind::Reset;out.enhanced=typed.enhanced_attack;
        } else {
            if(!finite(typed.range)||static_cast<double>(typed.range)>static_cast<double>(std::numeric_limits<int>::max())){valid=false;return;}
            out.range=static_cast<int>(std::floor(typed.range));
            if constexpr(std::is_same_v<T,cambc::ability_shield>) {
                out.kind=SpellKind::Shield;out.shield=ability.shield_amount();out.duration=typed.duration;
            } else if constexpr(std::is_same_v<T,cambc::ability_heal>) {
                out.kind=SpellKind::Heal;out.heal=ability.heal_amount();
            } else {
                out.damage=ability.damage_amount()*camp_bonus;
                if constexpr(std::is_same_v<T,cambc::ability_damage>)out.kind=SpellKind::Targeted;
                else if constexpr(std::is_same_v<T,cambc::ability_cc>) {
                    out.kind=SpellKind::Targeted;out.duration=out.stun=typed.duration;
                } else {
                    if(!finite(typed.radius)||static_cast<double>(typed.radius)>static_cast<double>(std::numeric_limits<int>::max())){valid=false;return;}
                    out.radius=static_cast<int>(std::floor(typed.radius));out.duration=typed.duration;
                    if constexpr(std::is_same_v<T,cambc::ability_dash>) {out.kind=SpellKind::Dash;out.stun=typed.duration;}
                    else out.kind=SpellKind::Area;
                }
            }
        }
    },*info);
    out.ready=valid&&cooldown==0&&can_cast;
    return valid&&finite(out.damage)&&finite(out.shield)&&finite(out.heal)&&out.duration>=0;
}
}

struct WorldCache::Impl {
    explicit Impl(Limits l):limits(l) {}
    Limits limits;ReadStatus status{ReadStatus::NeedCompute};Failure failure{};unsigned stage{};
    World world;std::shared_ptr<Grid> grid;std::unique_ptr<std::uint32_t[]> layout;
    std::size_t layout_size{},layout_capacity{};
    std::size_t cell{},cursor{},remaining{};contract::GroupHeader header;
    std::vector<cambc::camp> camps,group_members;
    std::vector<cambc::map::camp_group> groups;
    std::vector<std::uint8_t> camp_tiles;
    std::vector<std::uint32_t> last_visible_foes;
    std::uint32_t self_handle{};
    cambc::detail::champion_identity identity{};cambc::detail::champion_design design{};
    std::array<std::optional<cambc::camp_stats>,6> camp_designs;
    std::array<std::optional<cambc::item_info>,64> items;
    struct Packed {
        std::unique_ptr<std::uint8_t[]> bytes;
        std::span<const std::uint8_t> view(std::size_t size)const {return {bytes.get(),size};}
    };
    // Linear SDK bitmaps are packed, ceil(map area / 8) bytes, despite the
    // public export comment's byte-per-tile wording. Never decode raw chunks.
    std::array<Packed,5> terrain;
    unsigned terrain_layer{};
    std::array<std::unique_ptr<cambc::bitboard>,3> current_masks;
    std::array<Packed,3> current_packed;
    std::size_t area{},packed_size{};
    int last_frame{-1};
    ReadStatus reject(Failure why) {failure=why;status=ReadStatus::Unsupported;return status;}
    bool inside(Point p)const {return p.x>=0&&p.y>=0&&p.x<grid->width&&p.y<grid->height;}
    bool test(const Packed& packed,Point p)const {
        return inside(p)&&contract::packed_test(packed.view(packed_size),area,
            static_cast<std::size_t>(p.y)*grid->width+p.x);
    }
};
WorldCache::WorldCache(Limits limits):impl_(std::make_unique<Impl>(limits)) {}
WorldCache::~WorldCache()=default;
WorldCache::WorldCache(WorldCache&&) noexcept=default;
WorldCache& WorldCache::operator=(WorldCache&&) noexcept=default;
ReadStatus WorldCache::status()const noexcept{return impl_->status;}
Failure WorldCache::failure()const noexcept{return impl_->failure;}
unsigned WorldCache::stage()const noexcept{return impl_->stage;}
const World* WorldCache::result()const noexcept{return status()==ReadStatus::Ready?&impl_->world:nullptr;}

ReadStatus WorldCache::advance(std::size_t max_units) {
    auto& v=*impl_;if(v.status!=ReadStatus::NeedCompute)return v.status;
    const auto raw_epoch=cambc::round_number();if(raw_epoch>0x7fffffffu)return v.reject(Failure::Shape);
    const int epoch=static_cast<int>(raw_epoch);
    for(std::size_t unit=0;unit<max_units;++unit) {
        const auto leaf=v.stage==0?40000ull:v.stage==14?8000ull+2*v.area:8000ull;
        if(!funded(epoch,leaf))return interrupted(epoch);
        switch(v.stage) {
        case 0: {
            const auto width=cambc::map::width(),height=cambc::map::height();
            if(width<=0||height<=0||v.limits.cells==0||v.limits.cells>65536||
               v.limits.groups==0||v.limits.groups>256||v.limits.camps==0||v.limits.camps>1024||
               v.limits.members_per_group==0||v.limits.members_per_group>64||
               v.limits.champions_per_team==0||v.limits.champions_per_team>32||
               static_cast<std::uint64_t>(width)*static_cast<unsigned>(height)>v.limits.cells)
                return v.reject(Failure::Capacity);
            v.grid=std::make_shared<Grid>();v.grid->width=width;v.grid->height=height;
            const auto size=static_cast<std::size_t>(width)*static_cast<unsigned>(height);
            v.area=size;v.packed_size=contract::packed_bytes(size);
            // Reserve storage without traversing/zeroing the entire map in one
            // cold leaf. Terrain bits are constructed one cursor unit below.
            v.grid->blocked.reserve(size);v.camp_tiles.reserve(size);
            v.grid->own_spawn.reserve(size);v.grid->enemy_spawn.reserve(size);v.grid->altars.reserve(size);
            v.world.camps.reserve(v.limits.camps);v.camps.reserve(v.limits.camps);
            v.world.groups.reserve(v.limits.groups);v.groups.reserve(v.limits.groups);
            v.group_members.reserve(v.limits.members_per_group);v.stage=1;break;
        }
        case 1:
            v.layout_capacity=5*v.limits.groups+4*v.limits.camps;
            v.layout=std::unique_ptr<std::uint32_t[]>(new std::uint32_t[v.layout_capacity]);v.stage=2;break;
        case 2: {
            std::uint32_t count{};
            cambc::detail::check(cambc::detail::sdk_map_camp_layout(v.layout.get(),static_cast<unsigned>(v.layout_capacity),&count));
            if(count>v.layout_capacity)return v.reject(Failure::Capacity);
            v.layout_size=count;v.stage=3;break;
        }
        case 3: {
            // Each allocation and export has a separate fresh admission. No
            // large zero-filled aggregate, and no host Test call per map tile.
            v.terrain[v.terrain_layer].bytes=std::unique_ptr<std::uint8_t[]>(new std::uint8_t[v.packed_size]);
            v.stage=14;break;
        }
        case 14: {
            const cambc::bitboard* mask=nullptr;
            switch(v.terrain_layer) {
            case 0:mask=&cambc::map::walls();break;
            case 1:mask=&cambc::map::holes();break;
            case 2:mask=&cambc::map::altars();break;
            case 3:mask=&cambc::map::spawn_region(cambc::team::enemy);break;
            case 4:mask=&cambc::map::spawn_region(cambc::team::ally);break;
            default:return v.reject(Failure::Layout);
            }
            mask->export_linear_buffer({v.terrain[v.terrain_layer].bytes.get(),v.packed_size});
            if(!same(epoch))return ReadStatus::Stale;
            ++v.terrain_layer;v.stage=v.terrain_layer==5?15:3;break;
        }
        case 15: {
            if(v.cell==v.area){v.stage=4;break;}
            // A fixed pure batch shares one admission; SDK round/fuel getters
            // are not paid again for every locally decoded bit. The cell cursor
            // is still exact, and no world result escapes before full sealing.
            const auto end=std::min(v.area,v.cell+32);
            while(v.cell<end) {
                const Point p{static_cast<int>(v.cell%v.grid->width),static_cast<int>(v.cell/v.grid->width)};
                const bool wall=v.test(v.terrain[0],p),hole=v.test(v.terrain[1],p);
                const bool altar=v.test(v.terrain[2],p),enemy=v.test(v.terrain[3],p),own=v.test(v.terrain[4],p);
                v.grid->blocked.push_back(static_cast<std::uint8_t>(wall||hole||altar||enemy));v.camp_tiles.push_back(0);++v.cell;
                if(altar)v.grid->altars.push_back(p);
                if(enemy)v.grid->enemy_spawn.push_back(p);
                if(own)v.grid->own_spawn.push_back(p);
            }
            break;
        }
        case 4: {
            if(v.cursor==v.layout_size){v.stage=7;break;}
            if(v.world.groups.size()>=v.limits.groups)return v.reject(Failure::Capacity);
            if(!contract::group_header({v.layout.get(),v.layout_size},v.cursor,v.header))return v.reject(Failure::Layout);
            if(v.header.members>v.limits.members_per_group||v.header.members>v.limits.camps-v.world.camps.size())
                return v.reject(Failure::Capacity);
            for(const auto& group:v.world.groups)if(group.handle==v.header.handle)return v.reject(Failure::Duplicate);
            GroupSpec group;group.handle=v.header.handle;group.gold=v.header.gold;group.gems=v.header.gems;group.respawn=v.header.respawn;
            group.members.reserve(v.header.members);v.world.groups.push_back(std::move(group));
            v.remaining=v.header.members;v.cursor+=5;v.stage=5;break;
        }
        case 5: {
            if(v.remaining==0){v.stage=6;break;}
            if(v.cursor>v.layout_size||v.layout_size-v.cursor<4)return v.reject(Failure::Layout);
            const auto h=v.layout[v.cursor],kind=v.layout[v.cursor+1];
            if(kind>=v.camp_designs.size())return v.reject(Failure::Stats);
            const Point p{static_cast<int>(v.layout[v.cursor+2]),static_cast<int>(v.layout[v.cursor+3])};
            if(!v.inside(p))return v.reject(Failure::Shape);
            const auto index=static_cast<std::size_t>(p.y)*v.grid->width+p.x;
            if(v.camp_tiles[index])return v.reject(Failure::Duplicate);
            for(const auto& camp:v.world.camps)if(camp.handle==h)return v.reject(Failure::Duplicate);
            auto native=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(h),
                static_cast<cambc::handle_camp_group_t>(v.header.handle),static_cast<cambc::camp_type>(kind),{p.x,p.y});
            if(!v.camp_designs[kind])v.camp_designs[kind]=native.base_stats();
            const auto& stats=*v.camp_designs[kind];
            if(!finite(stats.base_health)||stats.base_health==0||!finite(stats.base_strength)||stats.base_attack_range<0)
                return v.reject(Failure::Stats);
            const auto group=static_cast<int>(v.world.groups.size()-1);
            v.world.groups.back().members.push_back(static_cast<int>(v.world.camps.size()));
            v.world.camps.push_back({h,group,p,stats.base_health,stats.base_strength,stats.base_attack_range});
            v.camps.push_back(native);v.group_members.push_back(std::move(native));v.camp_tiles[index]=1;
            v.cursor+=4;--v.remaining;break;
        }
        case 6:
            v.groups.push_back(cambc::detail::handle_access::make_camp_group(
                static_cast<cambc::handle_camp_group_t>(v.header.handle),v.header.gold,v.header.gems,
                static_cast<int>(v.header.respawn),std::move(v.group_members)));
            v.group_members.clear();v.group_members.reserve(v.limits.members_per_group);v.stage=4;break;
        case 7:
            v.self_handle=static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()));
            cambc::detail::check(cambc::detail::sdk_champion_identity(v.self_handle,&v.identity));
            if constexpr(tt::cfg::residual_probe)probe::select(static_cast<unsigned>(role(v.identity.type)));
            v.stage=8;break;
        case 8:
            cambc::detail::check(cambc::detail::sdk_champion_design(v.identity.type,&v.design));v.stage=9;break;
        case 9:
            v.world.grid=v.grid;v.status=ReadStatus::Ready;return same(epoch)?v.status:ReadStatus::Stale;
        default:return v.reject(Failure::Layout);
        }
        if(!same(epoch))return ReadStatus::Stale;
    }
    return ReadStatus::NeedCompute;
}

const cambc::camp* WorldCache::camp(int index)const noexcept {
    return status()==ReadStatus::Ready&&index>=0&&static_cast<std::size_t>(index)<impl_->camps.size()?&impl_->camps[index]:nullptr;
}
const cambc::map::camp_group* WorldCache::group(int index)const noexcept {
    return status()==ReadStatus::Ready&&index>=0&&static_cast<std::size_t>(index)<impl_->groups.size()?&impl_->groups[index]:nullptr;
}
std::optional<cambc::champion> WorldCache::foe(std::uint32_t handle)const {
    const auto& v=*impl_;
    if(!funded(v.last_frame,3000)||std::find(v.last_visible_foes.begin(),v.last_visible_foes.end(),handle)==v.last_visible_foes.end())return std::nullopt;
    return cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(handle));
}

ReadStatus WorldCache::observe(Frame& out,bool walked,int selected_group) {
    auto& v=*impl_;out.ready=false;v.last_frame=-1;v.last_visible_foes.clear();
    if(v.status!=ReadStatus::Ready)return v.status;
    const auto round=cambc::round_number();if(round>0x7fffffffu)return ReadStatus::Unsupported;
    const int epoch=static_cast<int>(round);clear(out,epoch,walked);
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,100,v.stage,1);
    if(selected_group< -1||(selected_group>=0&&static_cast<std::size_t>(selected_group)>=v.world.groups.size()))
        return ReadStatus::Unsupported;
    const GroupSpec* selected=selected_group>=0?&v.world.groups[static_cast<std::size_t>(selected_group)]:nullptr;
    if(selected&&selected->members.size()>32)return ReadStatus::Unsupported;
    if(!funded(epoch,6000))return interrupted(epoch);
    if(selected){out.camps.reserve(selected->members.size());out.groups.reserve(1);}
    out.foes.reserve(v.limits.champions_per_team);out.allies.reserve(v.limits.champions_per_team);
    cambc::detail::champion_info raw{};cambc::detail::check(cambc::detail::sdk_champion(v.self_handle,&raw));
    if(!same(epoch))return ReadStatus::Stale;
    if(!body_record(raw))return ReadStatus::Unsupported;
    out.role=role(raw.type);
    if(out.role==Role::Other||raw.type!=v.identity.type)return ReadStatus::Unsupported;
    out.alive=(raw.state&cambc::champion_state::alive.bits())!=0;
    auto& body=out.body;body.pos={raw.x,raw.y};body.hp=raw.health;body.max_hp=raw.max_health;
    body.raw_strength=body.strength=raw.strength;body.focus=raw.focus;body.range=raw.attack_range;body.ranged=body.range>1;
    body.can_move=(raw.state&cambc::champion_state::can_move.bits())!=0;
    body.can_attack=(raw.state&cambc::champion_state::can_attack.bits())!=0;
    body.can_cast=(raw.state&cambc::champion_state::can_cast.bits())!=0;
    out.submitting=(raw.state&cambc::champion_state::submitting_gems.bits())!=0;
    out.recalling=(raw.state&cambc::champion_state::recalling.bits())!=0;
    out.teleporting=(raw.state&cambc::champion_state::teleporting.bits())!=0;
    if(!out.alive){out.ready=same(epoch);return out.ready?ReadStatus::Ready:ReadStatus::Stale;}
    if(!v.inside(body.pos)||body.hp<=0)return ReadStatus::Unsupported;
    const auto& me=cambc::self();
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,101,v.stage,1);
    if(!funded(epoch))return interrupted(epoch);
    out.gold=me.gold_count();out.gems=me.gem_count();
    if(out.submitting||out.recalling||out.teleporting)out.channel_remaining=me.remaining_channel_time();
    if(!funded(epoch))return interrupted(epoch);
    const auto buffs=me.buffs();
    body.stunned=buffs.has_stun();if(body.stunned)body.stun=buffs.get<cambc::buff_stun>().duration;
    if(buffs.has_shield()){const auto shield=buffs.get<cambc::buff_shield>();body.shield=shield.value;body.shield_next_batch=shield.duration>=1;}
    body.enhanced=buffs.has_enhanced_attack();
    const auto passive=static_cast<cambc::passive_type>(v.design.definition.passive_type);
    body.splash=passive==cambc::passive_type::splash_attack;body.splash_scale=body.splash?1.0f:0.0f;
    body.splash_radius=body.splash?static_cast<int>(cambc::constants::passive_abilities::varg_splash_radius):0;
    body.immune=passive==cambc::passive_type::stun_immunity;
    body.next_basic_stun=body.enhanced&&body.immune?1:0; // Marcella's observed enhanced attack; rules §champions.
    if(passive==cambc::passive_type::missing_health_damage)
        body.strength*=1+cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus*(1-body.hp/body.max_hp);
    if(!funded(epoch))return interrupted(epoch);
    const auto counts=me.item_count();if(counts.size()>v.items.size())return ReadStatus::Unsupported;
    float monster{},jungle{},durability{};
    for(std::uint32_t i=0;i<counts.size();++i)if(counts[cambc::item_type{i}]) {
        if(!funded(epoch))return interrupted(epoch);
        if(!v.items[i])v.items[i]=cambc::item_type{i}.info();
        const auto& item=*v.items[i];if(!finite(item.passive_magnitude))return ReadStatus::Unsupported;
        if(item.passive==cambc::item_passive::monster)monster=std::max(monster,item.passive_magnitude);
        if(item.passive==cambc::item_passive::jungle)jungle=std::max(jungle,item.passive_magnitude);
        if(item.passive==cambc::item_passive::durability)durability=std::max(durability,item.passive_magnitude);
    }
    if(jungle>1||durability>1||!finite(v.design.base_stats.camp_damage_multiplier))return ReadStatus::Unsupported;
    body.strength*=1+monster;body.incoming_scale=v.design.base_stats.camp_damage_multiplier*(1-jungle)*(1-durability);
    if(!finite(body.strength)||!finite(body.incoming_scale)||!finite(body.shield))return ReadStatus::Unsupported;
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,102,v.stage,1);
    const auto abilities=me.abilities();
    for(std::size_t i=0;i<abilities.size();++i) {
        if(!funded(epoch,3000))return interrupted(epoch);
        if(!spell(abilities[i],out.spells[i],body.can_cast,1+monster))return ReadStatus::Unsupported;
        // Ryth's actual ground-area payload carries stun despite the SDK's
        // is_cc() classification being false for the Area kind.
        if(out.role==Role::Ryth&&out.spells[i].kind==SpellKind::Area)
            out.spells[i].stun=out.spells[i].duration;
    }
    // Exactly three live masks, each with separately funded allocation, fill
    // and packed export. Storage alone survives; every call refreshes contents.
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,103,v.stage,1);
    for(std::size_t layer=0;layer<3;++layer) {
        if(!funded(epoch,8000))return interrupted(epoch);
        if(!v.current_masks[layer])v.current_masks[layer]=std::make_unique<cambc::bitboard>(cambc::uninitialized);
        if(!v.current_packed[layer].bytes)
            v.current_packed[layer].bytes=std::unique_ptr<std::uint8_t[]>(new std::uint8_t[v.packed_size]);
        if(!same(epoch))return ReadStatus::Stale;
        if(!funded(epoch,8000))return interrupted(epoch);
        if(layer==0)cambc::vision::visible_tiles_into(*v.current_masks[layer]);
        else cambc::vision::champion_tiles_into(layer==1?cambc::team::ally:cambc::team::enemy,*v.current_masks[layer]);
        if(!same(epoch))return ReadStatus::Stale;
        if(!funded(epoch,8000+2*v.area))return interrupted(epoch);
        v.current_masks[layer]->export_linear_buffer({v.current_packed[layer].bytes.get(),v.packed_size});
        if(!same(epoch))return ReadStatus::Stale;
    }
    out.own_spawn=v.test(v.terrain[4],body.pos);
    if(selected) {
        if constexpr(tt::cfg::residual_probe)probe::before(epoch,104,v.stage,1);
        if(!funded(epoch,3000))return interrupted(epoch);
        std::uint32_t observed_state{};int timer=-1;
        bool visible=false;
        for(const auto member:selected->members) {
            const auto p=v.world.camps[static_cast<std::size_t>(member)].pos;
            if(v.test(v.current_packed[0],p)){visible=true;break;}
        }
        if(visible) {
            const auto code=cambc::detail::sdk_camp_group_state(selected->handle,&observed_state);
            if(!same(epoch))return ReadStatus::Stale;
            if(unknown(code))observed_state=0;
            else {
                cambc::detail::check(code);
                if(observed_state>3)throw std::runtime_error("unknown SDK camp-group lifecycle");
            }
            if(observed_state==3) {
                if(!funded(epoch))return interrupted(epoch);
                cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(selected->handle,&timer));
                if(timer<0)return ReadStatus::Unsupported;
            }
        }
        out.groups.push_back(contract::group_reading(selected_group,observed_state,timer,
            static_cast<unsigned>(std::max(0,cambc::constants::capture_time))));
        if constexpr(tt::cfg::residual_probe)probe::before(epoch,105,v.stage,1);
        for(const auto member:selected->members) {
            const auto i=static_cast<std::size_t>(member);
            if(!funded(epoch,3000))return interrupted(epoch);
            const auto p=v.world.camps[i].pos;if(!v.test(v.current_packed[0],p))continue;
            cambc::detail::camp_state current{};const auto code=cambc::detail::sdk_camp(v.world.camps[i].handle,&current);
            if(!same(epoch))return ReadStatus::Stale;
            if(unknown(code))continue;
            cambc::detail::check(code);
            if(!finite(current.health)||current.x<0||current.y<0)return ReadStatus::Unsupported;
            CampReading reading;reading.index=member;reading.pos={current.x,current.y};reading.hp=current.health;
            if(!funded(epoch))return interrupted(epoch);
            const auto state=v.camps[i].state();reading.alive=state.is_alive();reading.awake=state.is_awake();reading.awake_known=true;
            if(!funded(epoch))return interrupted(epoch);
            const auto current_buffs=v.camps[i].buffs();
            if(current_buffs.has_shield())reading.shield=current_buffs.get<cambc::buff_shield>().value;
            if(current_buffs.has_stun())reading.stun=current_buffs.get<cambc::buff_stun>().duration;
            if(!finite(reading.shield)||reading.stun<0)return ReadStatus::Unsupported;
            out.camps.push_back(reading);
        }
    }
    // Iterate packed occupancy bytes locally. No hidden roster is requested,
    // and no host bit Test is paid for any map/catalogue cell.
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,106,v.stage,1);
    for(std::size_t layer=1;layer<3;++layer) {
        const auto occupancy=v.current_packed[layer].view(v.packed_size);
        std::size_t count{};
        for(std::size_t byte=0;byte<occupancy.size();++byte) {
            const auto bits=occupancy[byte];if(!bits)continue;
            for(unsigned bit=0;bit<8;++bit) {
                if((bits&(1u<<bit))==0)continue;
                const auto cell=byte*8+bit;if(cell>=v.area)continue;
                if(++count>v.limits.champions_per_team)return ReadStatus::Unsupported;
                const Point p{static_cast<int>(cell%v.grid->width),static_cast<int>(cell/v.grid->width)};
                if(layer==1){if(p!=body.pos)out.allies.push_back(p);continue;}
                if(!funded(epoch,3000))return interrupted(epoch);
                std::uint32_t handle{};const auto at=cambc::detail::sdk_champion_at(p.x,p.y,&handle);
                if(!same(epoch)||unknown(at))return ReadStatus::Stale;
                cambc::detail::check(at);
                if(!funded(epoch,3000))return interrupted(epoch);
                cambc::detail::champion_info foe{};const auto code=cambc::detail::sdk_champion(handle,&foe);
                if(!same(epoch)||unknown(code))return ReadStatus::Stale;
                cambc::detail::check(code);
                if(foe.x!=p.x||foe.y!=p.y||!(foe.state&cambc::champion_state::alive.bits()))return ReadStatus::Stale;
                if(!body_record(foe))return ReadStatus::Unsupported;
                if(!funded(epoch,6000))return interrupted(epoch);
                auto native=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(handle));
                if(!same(epoch))return ReadStatus::Stale;
                FoeReading reading;reading.handle=handle;reading.pos=p;reading.hp=foe.health;
                reading.strength=foe.strength;reading.focus=foe.focus;reading.range=foe.attack_range;
                if(!funded(epoch))return interrupted(epoch);
                const auto current_buffs=native.buffs();if(current_buffs.has_shield())reading.shield=current_buffs.get<cambc::buff_shield>().value;
                const auto slots=native.abilities();
                for(std::size_t i=0;i<slots.size();++i) {
                    if(!funded(epoch,3000))return interrupted(epoch);
                    if(!spell(slots[i],reading.spells[i],(foe.state&cambc::champion_state::can_cast.bits())!=0,1))return ReadStatus::Unsupported;
                }
                out.foes.push_back(reading);v.last_visible_foes.push_back(handle);
            }
        }
    }
    if constexpr(tt::cfg::residual_probe)probe::before(epoch,107,v.stage,1);
    if(!funded(epoch))return interrupted(epoch);
    cambc::detail::champion_info final{};cambc::detail::check(cambc::detail::sdk_champion(v.self_handle,&final));
    if(!same(epoch)||final.x!=raw.x||final.y!=raw.y||final.health!=raw.health||final.max_health!=raw.max_health||
       final.strength!=raw.strength||final.focus!=raw.focus||final.state!=raw.state)return ReadStatus::Stale;
    out.ready=true;v.last_frame=epoch;return ReadStatus::Ready;
}
}
