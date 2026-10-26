#pragma once
#include <cambc.h>
#include "capital_contact_packets.h"
#include "capital_word.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

// Bounded SDK observation -> conditional own-word adapter. No action is issued.
// Packed row-major export buffers are decoded locally; the prefix provider
// performs ZERO SDK calls. Candidate effects remain predictions. Native callers
// must refresh permissions/effects after each accepted primitive and freshly
// nominate after a kill; a refused prefix is never its intended endpoint.
namespace wb::capital::word_native {
using cambc::vec2;
using Snapshot=contact_native::Snapshot;
inline word::Point point(vec2 p){return {p.x,p.y};}
inline vec2 point(word::Point p){return {p.x,p.y};}
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline word::Interval bracket(float x) {
    if(!(x>0))return {};
    return {std::max(0.f,std::nextafter(x,-std::numeric_limits<float>::infinity())),
        std::nextafter(x,std::numeric_limits<float>::infinity())};
}
struct Packed {
    std::vector<std::uint8_t> bytes;
    int width=0,height=0;
    bool valid() const{return width>0&&height>0&&bytes.size()==(static_cast<std::size_t>(width)*height+7)/8;}
    bool inside(word::Point p) const{return p.x>=0&&p.y>=0&&p.x<width&&p.y<height;}
    bool get(word::Point p) const {
        if(!valid()||!inside(p))return false;
        const auto bit=static_cast<std::size_t>(p.y)*width+p.x;
        return (bytes[bit/8]>>(bit%8))&1u;
    }
    void clear(word::Point p) {
        if(!valid()||!inside(p))return;
        const auto bit=static_cast<std::size_t>(p.y)*width+p.x;
        bytes[bit/8]&=static_cast<std::uint8_t>(~(1u<<(bit%8)));
    }
};
inline Packed packed(const cambc::bitboard& b) {
    return {b.export_linear_buffer(),cambc::map::width(),cambc::map::height()};
}
enum class Status {Ready,NeedWarmup,NeedFuel,Stale,UnsupportedOwner,UnsupportedRoster,InvalidSnapshot,NoFamily};
struct Geometry {
    Packed walking_static{},holes{};
    bool ready=false,identity_checked=false,owner_effects_closed=false;
    int own_type=-1;
    cambc::detail::champion_design design{};
    std::uint64_t serial=0;
};
inline Geometry& geometry(){static Geometry value;return value;}
// Call from an initially quiet, preferably first-body larger-budget input.
// This caches only immutable terrain, own design and complete roster identity.
// A partial warmup returns false without issuing any gameplay primitive.
inline bool prime_geometry() {
    auto& g=geometry();const auto epoch=cambc::round_number();
    const int width=cambc::map::width(),height=cambc::map::height();
    if(width<=0||height<=0||width>100||height>100)return false;
    const auto area=static_cast<unsigned>(width*height);
    if(!g.ready) {
        if(cambc::fuel_remaining()<20000+6u*area)return false;
        g.walking_static=packed(cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
            cambc::map::spawn_region(cambc::team::enemy));
        if(cambc::round_number()!=epoch)return false;
        g.holes=packed(cambc::map::holes());
        if(cambc::round_number()!=epoch)return false;
        if(!g.walking_static.valid()||!g.holes.valid())return false;
        g.ready=true;
    }
    if(!g.identity_checked) {
        if(cambc::fuel_remaining()<16000)return false;
        g.own_type=static_cast<int>(cambc::self().champion_name());
        cambc::detail::check(cambc::detail::sdk_champion_design(g.own_type,&g.design));
        g.owner_effects_closed=true;
        const auto roster=cambc::champions(cambc::team::enemy);
        if(roster.empty())return false; // do not freeze an incomplete draft roster
        for(const auto& enemy:roster) {
            const auto kind=enemy.champion_name(); // immutable identity, not vision
            switch(kind) {
            case cambc::champion_type::ryth:case cambc::champion_type::vesper_vale:
            case cambc::champion_type::vendetta:case cambc::champion_type::stellar:
            case cambc::champion_type::prism:case cambc::champion_type::marcella:
            case cambc::champion_type::willow:case cambc::champion_type::varg:
            case cambc::champion_type::tavi:case cambc::champion_type::heart_of_ruin:break;
            default:g.owner_effects_closed=false;break; // retaliation, linked death or unknown
            }
        }
        if(cambc::round_number()!=epoch)return false;
        g.identity_checked=true;
    }
    return true;
}
struct Provenance {bool walked=false,exclusive_cast=false;};
struct Options {
    bool require_known_paths=false;
    // The caller may supply complete collateral coverage; initial Varg menus
    // otherwise omit unknown world effects, while the owner-reaction closure
    // still comes from the COMPLETE immutable opponent roster above.
    bool collateral_covered=false;
    // Root-view work -> dash -> walk needs no hypothetical view.
    unsigned hypothetical_visions=0;
    unsigned dispatch_reserve=20000;
    bool nominate_root_moves=false;
};
enum class Family {MoveBasicDash,BasicDashTail,StationaryCampWork};
inline Family family_for_camp(word::Point owner,word::Point target,unsigned attack_range) {
    return word::distance(owner,target)<=attack_range?Family::BasicDashTail:Family::MoveBasicDash;
}
using ExitFilter=bool(*)(void*,word::Point,int);
struct OptionsFamily {
    Family family=Family::MoveBasicDash;
    bool require_known_paths=true;
    unsigned dispatch_reserve=30000,max_exits=16;
    ExitFilter exit_filter=nullptr;void* exit_context=nullptr;
};
struct RootOptions {unsigned dispatch_reserve=30000;bool collect_walk=true;};
struct RootLifetime {bool valid=false;std::uint64_t generation=0;};
struct RootView {
    Snapshot binding{};const Snapshot* source=nullptr;Provenance provenance{};
    int owner=-1,round=-1,kit=-1;word::Point position{};std::uint64_t generation=0;
    std::optional<cambc::bitboard> walking_blockers,walking_mask;
    Packed occupancy{},visible{},root_move{};std::vector<vec2> move_positions;
    std::shared_ptr<RootLifetime> lifetime=std::make_shared<RootLifetime>();
    Status status=Status::InvalidSnapshot;
    RootView()=default;RootView(const RootView&)=delete;RootView& operator=(const RootView&)=delete;
    ~RootView(){lifetime->valid=false;}
};
inline bool same_ability(const contact::Ability& a,const contact::Ability& b) {
    return a.kind==b.kind&&a.cooldown==b.cooldown&&a.max_cooldown==b.max_cooldown&&a.range==b.range&&
        a.radius==b.radius&&a.damage==b.damage&&a.stun_inputs==b.stun_inputs&&a.excludes_walk==b.excludes_walk&&
        a.enhanced_reset==b.enhanced_reset;
}
inline bool same_snapshot(const Snapshot& a,const Snapshot& b) {
    const auto& x=a.raw_self;const auto& y=b.raw_self;
    if(x.team!=y.team||x.type!=y.type||x.x!=y.x||x.y!=y.y||x.health!=y.health||x.max_health!=y.max_health||
        x.strength!=y.strength||x.focus!=y.focus||x.move_speed!=y.move_speed||x.attack_range!=y.attack_range||x.state!=y.state||
        a.self_state.bits()!=b.self_state.bits()||a.self_restrictions.bits()!=b.self_restrictions.bits()||
        a.self_stunned!=b.self_stunned||a.self_enhanced!=b.self_enhanced||a.self_stun_inputs!=b.self_stun_inputs||
        a.self_shield_now!=b.self_shield_now||a.self_mitigation!=b.self_mitigation||
        a.hidden_living_foes!=b.hidden_living_foes||a.omitted_dead_foes!=b.omitted_dead_foes||a.enemies.size()!=b.enemies.size()||
        a.enemies.size()>contact::max_foes)return false;
    for(unsigned i=0;i<2;++i)if(!same_ability(a.self_abilities[i],b.self_abilities[i]))return false;
    const auto& f=a.frame;const auto& g=b.frame;
    if(f.owner!=g.owner||f.round!=g.round||f.position.x!=g.position.x||f.position.y!=g.position.y||f.hp!=g.hp||
        f.max_hp!=g.max_hp||f.shield!=g.shield||f.foe_count!=g.foe_count||f.fresh!=g.fresh||
        f.coverage_complete!=g.coverage_complete||f.shield_valid_through_horizon!=g.shield_valid_through_horizon||
        f.self_stun_immune!=g.self_stun_immune||f.foe_count>contact::max_foes)return false;
    for(unsigned i=0;i<f.foe_count;++i) {
        const auto& p=f.foes[i];const auto& q=g.foes[i];
        if(p.id!=q.id||p.position.x!=q.position.x||p.position.y!=q.position.y||p.position_radius!=q.position_radius||
            p.hp!=q.hp||p.max_hp!=q.max_hp||p.shield!=q.shield||p.basic_damage!=q.basic_damage||p.attack_range!=q.attack_range||
            p.move_speed!=q.move_speed||p.stun_inputs!=q.stun_inputs||p.opportunities!=q.opportunities||p.ranged!=q.ranged||
            p.first_fresh!=q.first_fresh||p.can_move!=q.can_move||p.can_attack!=q.can_attack||p.can_cast!=q.can_cast||
            p.enhanced!=q.enhanced||p.stun_immune!=q.stun_immune||p.heart!=q.heart||p.hp_may_fall!=q.hp_may_fall||
            p.cooldowns_may_refresh!=q.cooldowns_may_refresh||p.represented!=q.represented)return false;
        for(unsigned slot=0;slot<2;++slot)if(!same_ability(p.abilities[slot],q.abilities[slot]))return false;
    }
    for(unsigned i=0;i<a.enemies.size();++i)if(static_cast<cambc::handle_champion_t>(a.enemies[i])!=
        static_cast<cambc::handle_champion_t>(b.enemies[i])||a.foe_mitigation[i]!=b.foe_mitigation[i])return false;
    return true;
}
inline bool root_matches(const RootView& root,const Snapshot& snapshot,Provenance history) {
    return root.status==Status::Ready&&root.lifetime->valid&&root.lifetime->generation==root.generation&&root.source==&snapshot&&
        root.provenance.walked==history.walked&&root.provenance.exclusive_cast==history.exclusive_cast&&same_snapshot(root.binding,snapshot);
}
inline void invalidate(RootView& root){root.lifetime->valid=false;root.status=Status::Stale;}
inline std::vector<vec2> packed_positions(const Packed& mask) {
    std::vector<vec2> result;if(!mask.valid())return result;
    for(unsigned byte=0;byte<mask.bytes.size();++byte)for(unsigned bits=mask.bytes[byte];bits;bits&=bits-1) {
        const auto at=byte*8+std::countr_zero(bits);if(at>=static_cast<unsigned>(mask.width*mask.height))continue;
        result.push_back({static_cast<int>(at%mask.width),static_cast<int>(at/mask.width)});
    }
    return result;
}
inline Status prepare_root(RootView& root,const Snapshot& snapshot,Provenance history,RootOptions options={}) {
    invalidate(root);root.source=nullptr;root.walking_blockers.reset();root.walking_mask.reset();root.move_positions.clear();
    const auto epoch=cambc::round_number();
    if(!snapshot.frame.fresh||snapshot.frame.round!=static_cast<int>(epoch)||snapshot.frame.owner!=
        static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))||snapshot.frame.position.x!=snapshot.raw_self.x||
        snapshot.frame.position.y!=snapshot.raw_self.y)return root.status=Status::Stale;
    if(!prime_geometry())return root.status=Status::NeedWarmup;
    auto& g=geometry();const auto area=static_cast<unsigned>(g.walking_static.width*g.walking_static.height);
    if(cambc::fuel_remaining()<options.dispatch_reserve+8000u+4u*area)return root.status=Status::NeedFuel;
    if(snapshot.raw_self.move_speed<0||snapshot.raw_self.move_speed>10)return root.status=Status::UnsupportedOwner;
    auto champions=cambc::vision::champion_tiles();champions.set({snapshot.raw_self.x,snapshot.raw_self.y},false);
    auto occupied=champions|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
    root.occupancy=packed(occupied);root.visible=packed(cambc::vision::visible_tiles());
    static std::optional<cambc::bitboard> terrain;
    if(!terrain){terrain.emplace(cambc::uninitialized);terrain->import_linear_buffer(g.walking_static.bytes);}
    root.walking_blockers.emplace(*terrain|occupied);root.walking_blockers->set({snapshot.raw_self.x,snapshot.raw_self.y},false);
    if(options.collect_walk&&snapshot.self_state.contains(cambc::champion_state::can_move)&&
        !snapshot.self_restrictions.contains(cambc::buff_restrictions::move)&&!history.exclusive_cast&&snapshot.raw_self.move_speed>0) {
        root.walking_mask.emplace(root.walking_blockers->reachable_tiles({snapshot.raw_self.x,snapshot.raw_self.y},snapshot.raw_self.move_speed));
        root.root_move=packed(*root.walking_mask);root.move_positions=packed_positions(root.root_move);
    } else root.root_move={std::vector<std::uint8_t>((area+7)/8),g.walking_static.width,g.walking_static.height};
    if(cambc::round_number()!=epoch)return root.status=Status::Stale;
    if(!root.occupancy.valid()||!root.visible.valid()||!root.root_move.valid())return root.status=Status::InvalidSnapshot;
    // Remove only our champion, not a co-located pending-teleport marker. The
    // SDK walking board clears its start for BFS; this dynamic packet does not.
    root.binding=snapshot;root.source=&snapshot;root.provenance=history;
    root.owner=snapshot.frame.owner;root.round=snapshot.frame.round;root.kit=snapshot.raw_self.type;root.position={snapshot.raw_self.x,snapshot.raw_self.y};
    root.generation=++g.serial;if(!root.generation)root.generation=++g.serial;
    root.lifetime->generation=root.generation;root.lifetime->valid=true;return root.status=Status::Ready;
}
struct Binding {unsigned kind=word::enemy;int id=-1,enemy_index=-1;};
struct Vision {word::Point position{};Packed visible{};};
struct Scene {
    word::Input input{};
    word::Point objective{};
    Packed occupancy{},visible{},root_move{};
    std::array<Packed,2> root_cast{};
    std::array<bool,2> has_root_cast{};
    std::array<Binding,word::max_targets> bindings{};
    std::optional<cambc::camp> camp_target;
    int camp_index=-1;
    std::array<Vision,5> hypothetical{};
    unsigned hypothetical_count=0;
    Options options{};
    Status status=Status::InvalidSnapshot;
    bool camp_omitted=false,targets_partial=false;
    bool compile_deferred=false;
    std::shared_ptr<RootLifetime> shared_root{};std::uint64_t root_generation=0;
    bool family_specific=false,family_no_exit=false;Family prepared_family=Family::MoveBasicDash;
    bool family_prepare_deferred=false;
    word::Point family_ingress{};OptionsFamily family_options{};
};
// Cells currently known blocked stay blocked even after a predicted kill.
// A new actual scene may release them after native death/occupancy observation.
inline bool free(const Scene& scene,word::Point q,bool dash) {
    const auto& g=geometry();
    if(!g.walking_static.valid()||!scene.occupancy.valid()||!g.walking_static.inside(q)||scene.occupancy.get(q))return false;
    const bool blocked=g.walking_static.get(q);
    return !blocked||(dash&&g.holes.get(q));
}
inline bool seen(const Scene& scene,word::Point q){return scene.visible.get(q);}
inline bool vision_at(const Scene& scene,word::Point pose,word::Point target) {
    if(pose==scene.input.initial.position)return scene.visible.get(target);
    for(unsigned i=0;i<scene.hypothetical_count;++i)
        if(scene.hypothetical[i].position==pose)return scene.hypothetical[i].visible.get(target);
    return false; // no invented continued vision from a new hypothetical pose
}
inline bool ray_path(const Scene& scene,word::Point from,word::Point to,bool dash,bool known) {
    const int dx=to.x-from.x,dy=to.y-from.y;
    const int length=std::max(std::abs(dx),std::abs(dy));
    if(length<1||length>2)return false;
    // Initial supported dash geometry uses the eight straight rays. It does
    // not equate an arbitrary SDK BFS mask with the native dash algorithm.
    if(dx&&dy&&std::abs(dx)!=std::abs(dy))return false;
    const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
    for(int i=1;i<=length;++i) {
        const word::Point q{from.x+sx*i,from.y+sy*i};
        if(!free(scene,q,dash)||(known&&!seen(scene,q)))return false;
    }
    return dash?!geometry().holes.get(to):true;
}
inline unsigned separation(const word::State& state,word::Point p) {
    unsigned best=1000;
    for(unsigned i=0;i<state.target_count;++i)if(state.targets[i].hp.upper>0)
        best=std::min(best,word::distance(p,state.targets[i].position));
    return best;
}
struct Landings {word::Point toward{},away{};bool has_toward=false,has_away=false;};
inline Landings landings(const Scene& scene,const word::State& state,bool dash,unsigned range,int slot=-1) {
    range=std::min(range,2u); // explicit bounded local nomination class
    Landings out;unsigned nearest=1000,farthest=0;
    auto goal=scene.objective;
    if(scene.camp_index>=0&&state.targets[scene.camp_index].hp.lower>0)
        goal=state.targets[scene.camp_index].position;
    else {
        unsigned close=1000;
        for(unsigned i=0;i<state.target_count;++i)if(state.targets[i].hp.lower>0) {
            const auto d=word::distance(state.position,state.targets[i].position);
            if(d<close){close=d;goal=state.targets[i].position;}
        }
    }
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
        for(unsigned length=1;length<=range;++length) {
            const word::Point q{state.position.x+dx*static_cast<int>(length),
                state.position.y+dy*static_cast<int>(length)};
            if(!ray_path(scene,state.position,q,dash,scene.options.require_known_paths))continue;
            if(state.prefix==0) {
                if(!dash&&!scene.root_move.get(q))continue;
                if(dash) {
                    const bool allowed=slot>=0&&slot<2&&scene.has_root_cast[slot]&&scene.root_cast[slot].get(q);
                    if(!allowed)continue;
                }
            }
            const auto near=word::distance(q,goal),away=separation(state,q);
            if(!out.has_toward||near<nearest||(near==nearest&&away>separation(state,out.toward))) {
                out.toward=q;out.has_toward=true;nearest=near;
            }
            if(!out.has_away||away>farthest||(away==farthest&&near<word::distance(out.away,goal))) {
                out.away=q;out.has_away=true;farthest=away;
            }
        }
    return out;
}
inline void add(word::Menu& menu,const word::State& state,word::Kind kind,int slot=-1,int target=-1,
                word::Point destination={}) {
    if(menu.count>=word::max_candidates){menu.partial=true;return;}
    menu.candidates[menu.count++]=word::bind(state,kind,slot,target,destination,
        state.prefix==0?word::Permission::Observed:word::Permission::Derived);
}
inline word::Menu provider(const word::State& state,void* context) {
    const auto& scene=*static_cast<const Scene*>(context);word::Menu menu;menu.partial=true;
    const auto& rules=scene.input.rules;
    if(state.m&&!state.exclusive_cast) {
        const auto moves=landings(scene,state,false,rules.move_speed);
        if(moves.has_toward)add(menu,state,word::Kind::Move,-1,-1,moves.toward);
        if(moves.has_away&&(!moves.has_toward||!(moves.away==moves.toward)))
            add(menu,state,word::Kind::Move,-1,-1,moves.away);
    }
    if(state.a) {
        int first=-1,second=-1;
        for(unsigned i=0;i<state.target_count;++i) {
            const auto& t=state.targets[i];
            if(!t.observed||t.hp.lower<=0||word::distance(state.position,t.position)>rules.attack_range||
                !vision_at(scene,state.position,t.position))continue;
            if(first<0||static_cast<int>(i)==scene.camp_index||
                (first!=scene.camp_index&&t.hp.upper<state.targets[first].hp.upper)){second=first;first=i;}
            else if(second<0||t.hp.upper<state.targets[second].hp.upper)second=i;
        }
        if(first>=0)add(menu,state,word::Kind::Basic,-1,first);
        if(second>=0&&second!=first)add(menu,state,word::Kind::Basic,-1,second);
    }
    if(state.c&&!state.cast_used)for(unsigned slot=0;slot<2;++slot) {
        const auto& a=rules.abilities[slot];
        if(state.cooldown[slot]||a.kind==word::AbilityKind::None||(a.excludes_walk&&state.walked))continue;
        // This native adapter intentionally supports A-spent Reset only. A-ready
        // Reset remains outside its measured class; this is not an SDK law.
        if(a.kind==word::AbilityKind::Reset){if(!state.a)add(menu,state,word::Kind::Reset,slot);}
        else if(a.kind==word::AbilityKind::Shield)add(menu,state,word::Kind::Shield,slot);
        else if(a.kind==word::AbilityKind::Dash) {
            const auto jumps=landings(scene,state,true,static_cast<unsigned>(a.range),slot);
            if(jumps.has_toward)add(menu,state,word::Kind::Dash,slot,-1,jumps.toward);
            if(jumps.has_away&&(!jumps.has_toward||!(jumps.away==jumps.toward)))
                add(menu,state,word::Kind::Dash,slot,-1,jumps.away);
        } else if(a.kind==word::AbilityKind::Damage) {
            int target=-1;
            for(unsigned i=0;i<state.target_count;++i) {
                const auto& t=state.targets[i];
                if(!t.observed||t.hp.lower<=0||(!(a.target_mask&t.kind)&&!(a.area&&(a.target_mask&word::ground)))||
                    word::distance(state.position,t.position)>a.range||!vision_at(scene,state.position,t.position))continue;
                if(state.prefix==0&&(!scene.has_root_cast[slot]||!scene.root_cast[slot].get(t.position)))continue;
                if(target<0||static_cast<int>(i)==scene.camp_index||t.hp.upper<state.targets[target].hp.upper)target=i;
            }
            if(target>=0)add(menu,state,word::Kind::Spell,slot,
                (a.target_mask&state.targets[target].kind)?target:-1,state.targets[target].position);
        }
    }
    return menu;
}
inline bool add_vision(Scene& scene,word::Point pose,unsigned epoch) {
    if(pose==scene.input.initial.position)return true;
    for(unsigned i=0;i<scene.hypothetical_count;++i)if(scene.hypothetical[i].position==pose)return true;
    if(scene.hypothetical_count>=std::min<unsigned>(5,scene.options.hypothetical_visions))return false;
    if(cambc::round_number()!=epoch||cambc::fuel_remaining()<scene.options.dispatch_reserve+18000+2u*scene.visible.width*scene.visible.height)return false;
    auto visible=packed(cambc::self().visible_tiles_from(point(pose)));
    if(cambc::round_number()!=epoch)return false;
    scene.hypothetical[scene.hypothetical_count++]={pose,std::move(visible)};return true;
}
inline float camp_multiplier() {
    const auto counts=cambc::self().item_count();
    for(const auto item:std::array{cambc::item_type::monster_emblem_of_strength,
        cambc::item_type::monster_emblem_of_focus,cambc::item_type::monster_emblem_of_health})
        if(counts[item]>0) {
            const auto info=item.info();
            if(info.passive!=cambc::item_passive::monster||!finite(info.passive_magnitude))return 0;
            return 1.f+info.passive_magnitude; // one family, never count duplicates
        }
    return 1.f;
}
inline Status prepare(Scene& scene,const Snapshot& snapshot,Provenance history,word::Point objective,
                      const cambc::camp* camp_target=nullptr,Options options={}) {
    scene={};scene.objective=objective;scene.options=options;
    const auto epoch=cambc::round_number();auto& g=geometry();
    if(!snapshot.frame.fresh||snapshot.frame.round!=static_cast<int>(epoch)||
        snapshot.frame.owner!=static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self())))
        return scene.status=Status::Stale;
    if(!prime_geometry())return scene.status=Status::NeedWarmup;
    const auto kind=static_cast<cambc::champion_type>(snapshot.raw_self.type);
    if(kind!=cambc::champion_type::varg&&kind!=cambc::champion_type::willow&&
        kind!=cambc::champion_type::vendetta&&kind!=cambc::champion_type::ryth&&
        kind!=cambc::champion_type::marcella)return scene.status=Status::UnsupportedOwner;
    if(g.own_type!=static_cast<int>(kind))return scene.status=Status::InvalidSnapshot;
    if(!g.owner_effects_closed)return scene.status=Status::UnsupportedRoster;
    if(!finite(snapshot.raw_self.strength)||!finite(snapshot.self_shield_now))
        return scene.status=Status::InvalidSnapshot;
    const auto area=static_cast<unsigned>(g.walking_static.width*g.walking_static.height);
    // Empirical preparation allowance PLUS the caller's dispatch reserve; this
    // is not a universal bound on SDK host work. Exact enabled native paths
    // must qualify fuel health before relying on the estimate.
    if(cambc::fuel_remaining()<options.dispatch_reserve+8000u+12u*area)return scene.status=Status::NeedFuel;
    auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
    scene.occupancy=packed(occupied);scene.visible=packed(cambc::vision::visible_tiles());
    if(options.nominate_root_moves&&snapshot.self_state.contains(cambc::champion_state::can_move)&&
        !snapshot.self_restrictions.contains(cambc::buff_restrictions::move)&&!history.exclusive_cast) {
        auto blocked=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
            cambc::map::spawn_region(cambc::team::enemy)|occupied;
        blocked.set({snapshot.raw_self.x,snapshot.raw_self.y},false);
        scene.root_move=packed(blocked.reachable_tiles({snapshot.raw_self.x,snapshot.raw_self.y},
            static_cast<unsigned>(std::clamp(snapshot.raw_self.move_speed,0,2))));
    } else scene.root_move={std::vector<std::uint8_t>((area+7)/8),g.walking_static.width,g.walking_static.height};
    if(!scene.occupancy.valid()||!scene.visible.valid()||!scene.root_move.valid())
        return scene.status=Status::InvalidSnapshot;
    scene.occupancy.clear({snapshot.raw_self.x,snapshot.raw_self.y});
    auto& in=scene.input;auto& s=in.initial;auto& r=in.rules;
    s.owner=snapshot.frame.owner;s.round=snapshot.frame.round;s.generation=++g.serial;if(!s.generation)s.generation=++g.serial;
    s.position={snapshot.raw_self.x,snapshot.raw_self.y};
    s.active=snapshot.self_state.contains(cambc::champion_state::alive)&&!snapshot.self_stunned&&
        !snapshot.self_state.contains(cambc::champion_state::recalling)&&
        !snapshot.self_state.contains(cambc::champion_state::submitting_gems)&&
        !snapshot.self_state.contains(cambc::champion_state::teleporting)&&
        !snapshot.self_state.contains(cambc::champion_state::completed_turn);
    s.m=snapshot.self_state.contains(cambc::champion_state::can_move)&&
        !snapshot.self_restrictions.contains(cambc::buff_restrictions::move);
    s.a=snapshot.self_state.contains(cambc::champion_state::can_attack)&&
        !snapshot.self_restrictions.contains(cambc::buff_restrictions::attack);
    s.c=snapshot.self_state.contains(cambc::champion_state::can_cast)&&
        !snapshot.self_restrictions.contains(cambc::buff_restrictions::cast);
    s.cast_used=!snapshot.self_state.contains(cambc::champion_state::can_cast);
    s.walked=history.walked;s.exclusive_cast=history.exclusive_cast;s.enhanced=snapshot.self_enhanced;
    s.self_shield={snapshot.self_shield_now,snapshot.self_shield_now};
    r.ranged=g.design.definition.attack_type==1;r.any_kill_refresh=kind==cambc::champion_type::vendetta;
    r.kill_effects_known=true;r.move_speed=std::max(0,snapshot.raw_self.move_speed);r.attack_range=std::max(0,snapshot.raw_self.attack_range);
    if(r.move_speed>10)return scene.status=Status::UnsupportedOwner; // pure kernel's declared resource bound
    r.basic_splash_radius=kind==cambc::champion_type::varg?cambc::constants::passive_abilities::varg_splash_radius:0.f;
    r.enhanced_basic_stun=kind==cambc::champion_type::marcella?1:0; // documented native enhanced-basic class
    const unsigned visible=std::min<unsigned>(snapshot.enemies.size(),word::max_targets);
    for(unsigned i=0;i<visible;++i) {
        const auto& f=snapshot.frame.foes[i];auto& t=s.targets[s.target_count];
        t={f.id,word::enemy,{f.position.x,f.position.y},{f.hp,f.hp},{f.shield,f.shield},true,f.stun_immune,f.stun_inputs};
        scene.bindings[s.target_count]={word::enemy,f.id,static_cast<int>(i)};
        r.basic_damage[s.target_count]=bracket(snapshot.raw_self.strength*snapshot.foe_mitigation[i]);++s.target_count;
    }
    scene.targets_partial=snapshot.enemies.size()>visible;
    float monster=1.f;
    if(camp_target&&s.target_count<word::max_targets) {
        if(cambc::round_number()!=epoch)return scene.status=Status::Stale;
        if(camp_target->is_visible()&&camp_target->is_alive()) {
            if(cambc::round_number()!=epoch)return scene.status=Status::Stale;
            const auto hp=camp_target->health();const auto position=camp_target->position();float shield=0;int stun=0;
            const auto buffs=camp_target->buffs();
            for(const auto& buff:buffs) {
                if(const auto* q=std::get_if<cambc::buff_shield>(&buff))shield+=q->value;
                if(const auto* q=std::get_if<cambc::buff_stun>(&buff))stun=q->duration;
            }
            monster=camp_multiplier();
            if(hp>0&&finite(hp)&&finite(shield)&&monster>0) {
                const auto index=s.target_count++;scene.camp_index=index;scene.camp_target=*camp_target;
                const int id=static_cast<int>(static_cast<cambc::handle_camp_t>(*camp_target));
                s.targets[index]={id,word::camp,point(position),{hp,hp},{shield,shield},true,false,stun};
                scene.bindings[index]={word::camp,id,-1};r.basic_damage[index]=bracket(snapshot.raw_self.strength*monster);
            } else scene.camp_omitted=true;
        } else scene.camp_omitted=true;
    } else if(camp_target)scene.camp_omitted=true;
    for(unsigned slot=0;slot<2;++slot) {
        const auto& props=g.design.definition.abilities[slot];const auto& actual=snapshot.self_abilities[slot];auto& a=r.abilities[slot];
        s.cooldown[slot]=actual.cooldown;a.max_cooldown=actual.max_cooldown;a.range=actual.range;
        a.radius=actual.radius;a.stun_inputs=actual.stun_inputs;a.excludes_walk=actual.excludes_walk;
        a.target_mask=props.targets;a.affect_mask=word::camp|word::enemy;
        switch(static_cast<cambc::ability_kind>(props.kind)) {
        case cambc::ability_kind::attack_reset:a.kind=word::AbilityKind::Reset;a.enhanced_reset=props.payload.enhanced_attack!=0;break;
        case cambc::ability_kind::damage:case cambc::ability_kind::aoe_damage:case cambc::ability_kind::cc:
            a.kind=word::AbilityKind::Damage;a.area=static_cast<cambc::ability_kind>(props.kind)==cambc::ability_kind::aoe_damage;break;
        case cambc::ability_kind::dash:a.kind=word::AbilityKind::Dash;
            if(a.range<1||a.range>2)a.kind=word::AbilityKind::None;break;
        case cambc::ability_kind::shield:
            // No unproved additive/replacement refresh over an existing shield.
            if(snapshot.self_shield_now==0&&props.payload.shield.duration>=1) {
                a.kind=word::AbilityKind::Shield;const float shield=cambc::self().abilities()[slot].shield_amount();
                a.shield_after={shield,shield};
            }break;
        default:break; // heal belongs to the separately modeled support family
        }
        for(unsigned i=0;i<s.target_count;++i) {
            const float mitigation=scene.bindings[i].kind==word::camp?monster:snapshot.foe_mitigation[scene.bindings[i].enemy_index];
            a.damage[i]=bracket(actual.damage*mitigation);
        }
        if(s.c&&(a.kind==word::AbilityKind::Dash||a.kind==word::AbilityKind::Damage)&&s.cooldown[slot]==0) {
            if(cambc::round_number()!=epoch)return scene.status=Status::Stale;
            scene.root_cast[slot]=packed(cambc::self().abilities()[slot].valid_tiles());scene.has_root_cast[slot]=true;
        }
    }
    in.owner_effects_closed=g.owner_effects_closed;
    bool needs_collateral=r.basic_splash_radius>0;
    for(unsigned slot=0;slot<2;++slot) {
        const auto& a=r.abilities[slot];
        needs_collateral|=a.radius>0&&(a.area||a.kind==word::AbilityKind::Dash)&&
            (snapshot.self_abilities[slot].damage>0||a.stun_inputs>0);
    }
    in.effects_complete=snapshot.frame.coverage_complete&&!scene.targets_partial&&!scene.camp_omitted&&
        (!needs_collateral||options.collateral_covered);
    in.fresh=cambc::round_number()==epoch;in.owner_action_phase=true;
    if(!in.fresh)return scene.status=Status::Stale;
    if(!word::detail::valid(in))return scene.status=Status::InvalidSnapshot;
    // At most five nominated attack poses get native hypothetical static LOS.
    // The provider only reads these prepared buffers. No query occurs in DFS.
    const auto moves=landings(scene,s,false,r.move_speed);
    if(moves.has_toward)(void)add_vision(scene,moves.toward,epoch);
    if(moves.has_away)(void)add_vision(scene,moves.away,epoch);
    for(unsigned slot=0;slot<2;++slot)if(r.abilities[slot].kind==word::AbilityKind::Dash) {
        const auto jumps=landings(scene,s,true,static_cast<unsigned>(r.abilities[slot].range),slot);
        if(jumps.has_toward)(void)add_vision(scene,jumps.toward,epoch);
        if(jumps.has_away)(void)add_vision(scene,jumps.away,epoch);
        if(moves.has_toward&&!r.abilities[slot].excludes_walk) {
            auto moved=s;moved.position=moves.toward;moved.prefix=1;moved.m=false;moved.walked=true;if(r.ranged)moved.a=false;
            const auto next=landings(scene,moved,true,static_cast<unsigned>(r.abilities[slot].range),slot);
            if(next.has_toward)(void)add_vision(scene,next.toward,epoch);
        }
    }
    if(cambc::round_number()!=epoch)return scene.status=Status::Stale;
    return scene.status=Status::Ready;
}
// This one ingress is chosen from the already queried root walking mask. No
// hypothetical board, ability mask or target packet is queried in this helper.
inline bool select_family_ingress(Scene& scene,const RootView& root,bool(*guard)(void*)=nullptr,void* guard_context=nullptr) {
    if(scene.camp_index<0)return false;
    const auto& state=scene.input.initial;const auto& victim=state.targets[scene.camp_index];const auto target=victim.position;
    float hp=victim.hp.upper,shield=victim.shield.upper;
    word::detail::hit(scene.input.rules.basic_damage[scene.camp_index].lower,hp,shield);
    const int killed=hp<=0?scene.bindings[scene.camp_index].id:-1;
    int dash_slot=-1;
    for(unsigned slot=0;slot<2;++slot)if(scene.input.rules.abilities[slot].kind==word::AbilityKind::Dash&&
        !scene.input.rules.abilities[slot].excludes_walk&&(state.cooldown[slot]==0||(killed>=0&&scene.input.rules.any_kill_refresh)))
        {dash_slot=slot;break;}
    if(dash_slot<0||!scene.family_options.exit_filter)return false;
    const auto range=std::min(2u,static_cast<unsigned>(scene.input.rules.abilities[dash_slot].range));
    for(const auto tile:root.move_positions) {
        const auto q=point(tile);const auto d=word::distance(state.position,q);
        if(d==0||d>scene.input.rules.move_speed||!scene.root_move.get(q)||
            !ray_path(scene,state.position,q,false,scene.options.require_known_paths)||
            word::distance(q,target)>scene.input.rules.attack_range)continue;
        if(guard&&!guard(guard_context)){scene.family_prepare_deferred=true;return false;}
        // Joint ingress/egress feasibility precedes the sole funded LOS query.
        // This is conditional geometry/damage reasoning, never an actual hit.
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
            for(unsigned length=range;length>0;--length) {
                const word::Point exit{q.x+dx*static_cast<int>(length),q.y+dy*static_cast<int>(length)};
                if(ray_path(scene,q,exit,true,scene.options.require_known_paths)&&
                    scene.family_options.exit_filter(scene.family_options.exit_context,exit,killed)) {
                    scene.family_ingress=q;return true;
                }
            }
    }
    return false;
}
// Minimal requested family: ONE fresh camp; root-range work needs zero ingress
// views, otherwise exactly ONE ingress attack-LOS query is funded. An explicit
// StationaryCampWork request compares Basic and supported Reset->Basic suffixes
// at the unchanged root and never requires a Dash or its readiness.
// All root geometry is reused, and no root-ready-Dash/other ability mask is
// required: a preceding confirmed Vendetta kill may refresh the post-hit Dash.
inline Status prepare(Scene& scene,const Snapshot& snapshot,Provenance history,word::Point objective,
                      const cambc::camp* camp_target,const RootView* root,OptionsFamily options) {
    scene={};scene.objective=objective;scene.family_specific=true;scene.prepared_family=options.family;scene.family_options=options;
    scene.options.require_known_paths=options.require_known_paths;scene.options.dispatch_reserve=options.dispatch_reserve;
    const bool stationary=options.family==Family::StationaryCampWork;
    scene.options.hypothetical_visions=stationary?0:1;scene.options.nominate_root_moves=!stationary;
    const auto epoch=cambc::round_number();auto& g=geometry();
    if(!root||!root_matches(*root,snapshot,history)||root->round!=static_cast<int>(epoch))return scene.status=Status::Stale;
    if((options.family!=Family::MoveBasicDash&&options.family!=Family::BasicDashTail&&!stationary)||!camp_target||!options.exit_filter)
        return scene.status=Status::NoFamily;
    if(snapshot.raw_self.type!=static_cast<int>(cambc::champion_type::vendetta)||g.own_type!=snapshot.raw_self.type)
        return scene.status=Status::UnsupportedOwner;
    if(!g.owner_effects_closed)return scene.status=Status::UnsupportedRoster;
    if(!finite(snapshot.raw_self.strength)||!finite(snapshot.self_shield_now)||snapshot.enemies.size()>=word::max_targets)
        return scene.status=Status::InvalidSnapshot;
    if(cambc::fuel_remaining()<options.dispatch_reserve+8000u)return scene.status=Status::NeedFuel;
    scene.occupancy=root->occupancy;scene.visible=root->visible;scene.root_move=root->root_move;
    scene.shared_root=root->lifetime;scene.root_generation=root->generation;
    auto& in=scene.input;auto& s=in.initial;auto& r=in.rules;
    s.owner=root->owner;s.round=root->round;s.generation=root->generation;s.position=root->position;
    s.active=snapshot.self_state.contains(cambc::champion_state::alive)&&!snapshot.self_stunned&&
        !snapshot.self_state.contains(cambc::champion_state::recalling)&&!snapshot.self_state.contains(cambc::champion_state::submitting_gems)&&
        !snapshot.self_state.contains(cambc::champion_state::teleporting)&&!snapshot.self_state.contains(cambc::champion_state::completed_turn);
    s.m=snapshot.self_state.contains(cambc::champion_state::can_move)&&!snapshot.self_restrictions.contains(cambc::buff_restrictions::move);
    s.a=snapshot.self_state.contains(cambc::champion_state::can_attack)&&!snapshot.self_restrictions.contains(cambc::buff_restrictions::attack);
    s.c=snapshot.self_state.contains(cambc::champion_state::can_cast)&&!snapshot.self_restrictions.contains(cambc::buff_restrictions::cast);
    s.cast_used=!snapshot.self_state.contains(cambc::champion_state::can_cast);s.walked=history.walked;s.exclusive_cast=history.exclusive_cast;
    s.enhanced=snapshot.self_enhanced;s.self_shield={snapshot.self_shield_now,snapshot.self_shield_now};
    r.ranged=false;r.any_kill_refresh=r.kill_effects_known=true;
    r.move_speed=std::max(0,snapshot.raw_self.move_speed);r.attack_range=std::max(0,snapshot.raw_self.attack_range);
    if(!s.active||!s.a||(!stationary&&(!s.c||s.cast_used||s.exclusive_cast)))return scene.status=Status::NoFamily;
    bool dash=false;
    for(unsigned slot=0;slot<2;++slot) {
        const auto& actual=snapshot.self_abilities[slot];const auto& props=g.design.definition.abilities[slot];s.cooldown[slot]=actual.cooldown;
        if(stationary) {
            // Unsupported Reset metadata removes only that suffix, never the
            // complete Basic incumbent. No ability mask or cast query is made.
            if(static_cast<cambc::ability_kind>(props.kind)==cambc::ability_kind::attack_reset&&
                !props.max_charges&&(props.targets&word::self)) {
                auto& a=r.abilities[slot];a.kind=word::AbilityKind::Reset;a.max_cooldown=actual.max_cooldown;
                a.range=actual.range;a.radius=actual.radius;a.target_mask=props.targets;a.affect_mask=word::enemy|word::camp;
                a.excludes_walk=actual.excludes_walk;a.enhanced_reset=props.payload.enhanced_attack!=0;
            }
            continue;
        }
        if(static_cast<cambc::ability_kind>(props.kind)!=cambc::ability_kind::dash)continue;
        if(actual.excludes_walk||actual.damage!=0||actual.stun_inputs!=0||actual.range<1||actual.range>2||props.max_charges||
            !(props.targets&word::ground))return scene.status=Status::UnsupportedOwner;
        auto& a=r.abilities[slot];a.kind=word::AbilityKind::Dash;a.max_cooldown=actual.max_cooldown;a.range=actual.range;
        a.radius=actual.radius;a.target_mask=props.targets;a.affect_mask=word::enemy|word::camp;dash=true;
    }
    if(!stationary&&!dash)return scene.status=Status::NoFamily;
    for(unsigned i=0;i<snapshot.enemies.size();++i) {
        const auto& f=snapshot.frame.foes[i];auto& t=s.targets[s.target_count];
        t={f.id,word::enemy,{f.position.x,f.position.y},{f.hp,f.hp},{f.shield,f.shield},true,f.stun_immune,f.stun_inputs};
        scene.bindings[s.target_count]={word::enemy,f.id,static_cast<int>(i)};
        r.basic_damage[s.target_count]=bracket(snapshot.raw_self.strength*snapshot.foe_mitigation[i]);++s.target_count;
    }
    if(!camp_target->is_visible()||!camp_target->is_alive())return scene.status=Status::NoFamily;
    const auto hp=camp_target->health();const auto at=camp_target->position();float shield=0;int stun=0;
    for(const auto& buff:camp_target->buffs()) {
        if(const auto* q=std::get_if<cambc::buff_shield>(&buff))shield+=q->value;
        if(const auto* q=std::get_if<cambc::buff_stun>(&buff))stun=std::max(stun,q->duration);
    }
    const auto monster=camp_multiplier();if(!(hp>0)||!finite(hp)||!finite(shield)||!(monster>0))return scene.status=Status::InvalidSnapshot;
    const auto index=s.target_count++;scene.camp_index=index;scene.camp_target=*camp_target;
    const int id=static_cast<int>(static_cast<cambc::handle_camp_t>(*camp_target));
    s.targets[index]={id,word::camp,point(at),{hp,hp},{shield,shield},true,false,stun};scene.bindings[index]={word::camp,id,-1};
    r.basic_damage[index]=bracket(snapshot.raw_self.strength*monster);
    in.owner_effects_closed=true;in.effects_complete=snapshot.frame.coverage_complete;in.owner_action_phase=true;in.fresh=cambc::round_number()==epoch;
    if(!in.fresh)return scene.status=Status::Stale;
    if(!word::detail::valid(in))return scene.status=Status::InvalidSnapshot;
    if(stationary) {
        if(word::distance(s.position,s.targets[index].position)>r.attack_range||!vision_at(scene,s.position,s.targets[index].position))
            return scene.status=Status::NoFamily;
        scene.family_ingress=s.position;
        if(cambc::round_number()!=epoch||!root_matches(*root,snapshot,history))return scene.status=Status::Stale;
        return scene.status=Status::Ready;
    }
    // Choose exactly one requested service from this freshly queried packet.
    // Root-range work needs no hypothetical attack view and does not require M.
    if(family_for_camp(s.position,s.targets[index].position,r.attack_range)==Family::BasicDashTail) {
        scene.prepared_family=scene.family_options.family=Family::BasicDashTail;scene.family_ingress=s.position;
        if(!vision_at(scene,s.position,s.targets[index].position))return scene.status=Status::NoFamily;
        if(cambc::round_number()!=epoch||!root_matches(*root,snapshot,history))return scene.status=Status::Stale;
        return scene.status=Status::Ready;
    }
    scene.prepared_family=scene.family_options.family=Family::MoveBasicDash;
    if(!s.m)return scene.status=Status::NoFamily;
    const auto funded=[](void* value){return cambc::fuel_remaining()>static_cast<OptionsFamily*>(value)->dispatch_reserve+12000u;};
    if(!select_family_ingress(scene,*root,funded,&options))return scene.status=scene.family_prepare_deferred?Status::NeedFuel:Status::NoFamily;
    if(!add_vision(scene,scene.family_ingress,epoch))return scene.status=cambc::round_number()==epoch?Status::NeedFuel:Status::Stale;
    if(!vision_at(scene,scene.family_ingress,s.targets[index].position))return scene.status=Status::NoFamily;
    if(cambc::round_number()!=epoch||!root_matches(*root,snapshot,history))return scene.status=Status::Stale;
    return scene.status=Status::Ready;
}
// One deterministic branch per named family prevents the first combinatorial
// family from consuming every word slot. Toward/away are geometric nominations,
// not a utility policy or safety ranking. The caller compares complete words.
inline std::optional<word::Candidate> nominate(const Scene& scene,const word::State& state,
                                               word::Kind kind,bool away) {
    // Hot compilation requests ONE primitive kind. Do not build the full Menu
    // (eight Candidate/State copies) or scan unrelated movement/ability families.
    const auto permission=state.prefix==0?word::Permission::Observed:word::Permission::Derived;
    const auto& rules=scene.input.rules;
    if(kind==word::Kind::Move) {
        if(!state.m||state.exclusive_cast)return std::nullopt;
        const auto moves=landings(scene,state,false,rules.move_speed);
        if(!moves.has_toward&&!moves.has_away)return std::nullopt;
        auto destination=moves.has_toward?moves.toward:moves.away;
        if(away&&moves.has_away&&separation(state,moves.away)>separation(state,destination))destination=moves.away;
        return word::bind(state,kind,-1,-1,destination,permission);
    }
    if(kind==word::Kind::Basic) {
        if(!state.a)return std::nullopt;
        int target=-1;
        for(unsigned i=0;i<state.target_count;++i) {
            const auto& t=state.targets[i];
            if(!t.observed||t.hp.lower<=0||word::distance(state.position,t.position)>rules.attack_range||
                !vision_at(scene,state.position,t.position))continue;
            if(target<0||static_cast<int>(i)==scene.camp_index||
                (target!=scene.camp_index&&t.hp.upper<state.targets[target].hp.upper))target=i;
        }
        if(target<0)return std::nullopt;
        return word::bind(state,kind,-1,target,{},permission);
    }
    if(!state.c||state.cast_used)return std::nullopt;
    if(kind==word::Kind::Reset&&state.a)return std::nullopt; // native supported class, not general legality
    int dash_slot=-1;word::Point dash_destination{};unsigned dash_separation=0;
    for(unsigned slot=0;slot<2;++slot) {
        const auto& a=rules.abilities[slot];
        if(state.cooldown[slot]||(a.excludes_walk&&state.walked))continue;
        if(kind==word::Kind::Reset&&a.kind==word::AbilityKind::Reset)
            return word::bind(state,kind,slot,-1,{},permission);
        if(kind==word::Kind::Shield&&a.kind==word::AbilityKind::Shield)
            return word::bind(state,kind,slot,-1,{},permission);
        if(kind==word::Kind::Dash&&a.kind==word::AbilityKind::Dash) {
            const auto jumps=landings(scene,state,true,static_cast<unsigned>(a.range),slot);
            if(jumps.has_toward) {
                const auto value=separation(state,jumps.toward);
                if(dash_slot<0||value>dash_separation){dash_slot=slot;dash_destination=jumps.toward;dash_separation=value;}
                if(!away)return word::bind(state,kind,slot,-1,jumps.toward,permission);
            }
            if(jumps.has_away) {
                const auto value=separation(state,jumps.away);
                if(dash_slot<0||value>dash_separation){dash_slot=slot;dash_destination=jumps.away;dash_separation=value;}
                if(!away)return word::bind(state,kind,slot,-1,jumps.away,permission);
            }
        }
        if(kind==word::Kind::Spell&&a.kind==word::AbilityKind::Damage) {
            int target=-1;
            for(unsigned i=0;i<state.target_count;++i) {
                const auto& t=state.targets[i];
                if(!t.observed||t.hp.lower<=0||(!(a.target_mask&t.kind)&&!(a.area&&(a.target_mask&word::ground)))||
                    word::distance(state.position,t.position)>a.range||!vision_at(scene,state.position,t.position))continue;
                if(state.prefix==0&&(!scene.has_root_cast[slot]||!scene.root_cast[slot].get(t.position)))continue;
                if(target<0||static_cast<int>(i)==scene.camp_index||t.hp.upper<state.targets[target].hp.upper)target=i;
            }
            if(target>=0)return word::bind(state,kind,slot,
                (a.target_mask&state.targets[target].kind)?target:-1,state.targets[target].position,permission);
        }
    }
    if(dash_slot>=0)return word::bind(state,kind,dash_slot,-1,dash_destination,permission);
    return std::nullopt;
}
using CompileGuard=bool(*)(void*);
struct NativeFamily {word::Pattern pattern;bool away;};
// A funded useful incumbent is emitted before optional compositions. These
// are operator availability/order, not utility weights or a stronger SDK law.
inline constexpr std::array native_families{
    NativeFamily{{{word::Kind::Basic},1},false},
    NativeFamily{{{word::Kind::Reset,word::Kind::Basic},2},false},
    NativeFamily{{{word::Kind::Basic,word::Kind::Dash,word::Kind::Move},3},true},
    NativeFamily{{{word::Kind::Move,word::Kind::Basic,word::Kind::Reset,word::Kind::Basic},4},false},
    NativeFamily{{{word::Kind::Move,word::Kind::Reset,word::Kind::Basic},3},false},
    NativeFamily{{{word::Kind::Basic,word::Kind::Reset,word::Kind::Basic,word::Kind::Move},4},true},
    NativeFamily{{{word::Kind::Dash,word::Kind::Basic},2},false},
    NativeFamily{{{word::Kind::Basic,word::Kind::Dash},2},true},
    NativeFamily{{{word::Kind::Basic,word::Kind::Reset,word::Kind::Basic},3},false},
    NativeFamily{{{word::Kind::Move,word::Kind::Dash,word::Kind::Basic},3},false},
    NativeFamily{{{word::Kind::Shield,word::Kind::Basic},2},false},
    NativeFamily{{{word::Kind::Spell,word::Kind::Basic},2},false}
};
// Necessary resource-only preflight. Geometry, target availability and actual
// effects still belong to nomination/transition. A preceding possible hit may
// refresh Vendetta's cooldowns; it NEVER restores the single cast allowance.
inline bool family_resources_possible(const word::Input& input,const NativeFamily& family) {
    const auto& root=input.initial;const auto& rules=input.rules;
    if(!root.active||family.pattern.count==0||family.pattern.count>word::max_steps)return false;
    bool m=root.m&&!root.exclusive_cast,a=root.a,c=root.c&&!root.cast_used,walked=root.walked;
    bool damage_before=false;
    for(unsigned step=0;step<family.pattern.count;++step) {
        const auto kind=family.pattern.kinds[step];
        if(kind==word::Kind::Move) {
            if(!m||rules.move_speed==0)return false;m=false;walked=true;if(rules.ranged)a=false;continue;
        }
        if(kind==word::Kind::Basic) {
            if(!a)return false;a=false;if(rules.ranged)m=false;damage_before=true;continue;
        }
        if(!c||(kind==word::Kind::Reset&&a))return false;
        const auto wanted=kind==word::Kind::Reset?word::AbilityKind::Reset:
            kind==word::Kind::Shield?word::AbilityKind::Shield:
            kind==word::Kind::Dash?word::AbilityKind::Dash:word::AbilityKind::Damage;
        bool found=false,compatible=false;
        for(unsigned slot=0;slot<2;++slot) {
            const auto& ability=rules.abilities[slot];
            if(ability.kind!=wanted||(ability.excludes_walk&&walked)||
                (root.cooldown[slot]>0&&!(rules.any_kill_refresh&&damage_before)))continue;
            if((kind==word::Kind::Reset||kind==word::Kind::Shield)&&!(ability.target_mask&word::self))continue;
            found=true;compatible|=!ability.excludes_walk;
        }
        if(!found)return false;c=false;if(!compatible)m=false;
        if(kind==word::Kind::Reset)a=true;
        if(kind==word::Kind::Spell||kind==word::Kind::Dash)damage_before=true;
    }
    return true;
}
// Optional native scheduling hook. Default compilation/nomination is host-free.
// Deferral discards only the unfinished word and retains complete words; it is
// neither a stale observation nor exhaustion of the pure transition budget.
inline word::Result compile(Scene& scene,word::Limits limits={24,6,4},
                            CompileGuard guard=nullptr,void* guard_context=nullptr) {
    scene.compile_deferred=false;
    word::Result out;out.provider_partial=true;
    if(scene.status!=Status::Ready||!word::detail::valid(limits)||!word::detail::valid(scene.input)) {
        out.reason=word::Reason::InvalidInput;return out;
    }
    if(!scene.input.owner_effects_closed){out.reason=word::Reason::UnsupportedClass;return out;}
    out.admitted=true;unsigned serial=0;
    for(const auto& family:native_families) {
        if(!family_resources_possible(scene.input,family))continue;
        if(out.count>=limits.words){out.word_limit=true;break;}
        if(out.attempted>=limits.transitions){out.transition_limit=true;break;}
        if(family.pattern.count>limits.depth){out.depth_limit=true;continue;}
        word::Word prefix;prefix.after=scene.input.initial;prefix.effects_complete=scene.input.effects_complete;
        bool complete=true;
        for(unsigned step=0;step<family.pattern.count;++step) {
            if(out.attempted>=limits.transitions){out.transition_limit=true;complete=false;break;}
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            const auto candidate=nominate(scene,prefix.after,family.pattern.kinds[step],family.away);
            if(!candidate){complete=false;break;}
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            ++out.attempted;
            const auto transition=word::apply_candidate(scene.input,prefix.after,*candidate,++serial);
            if(!transition.admitted){++out.rejected;complete=false;break;}
            if(!word::detail::extend(prefix,*candidate,transition)) {
                ++out.rejected;out.effect_limit=true;complete=false;break;
            }
            // Even root observed MAC/range masks are not host acceptance.
            prefix.trace[prefix.count-1].may_refuse=true;
        }
        if(complete)out.words[out.count++]=prefix;
    }
    return out;
}
// Native compilation borrows reusable storage. Only words[0..count), each
// word's active traces/effects and the bound states are readable. No inactive
// tail is initialised or copied. Keep the lease alive through native execution
// and keep both ResultView and Word bindings by const reference.
struct ResultView {
    std::span<const word::Word> words{};
    unsigned count=0,attempted=0,rejected=0;
    bool admitted=false,transition_limit=false,word_limit=false,depth_limit=false,effect_limit=false,provider_partial=true;
    word::Reason reason=word::Reason::None;
    int owner=-1,round=-1;std::uint64_t generation=0,ticket=0;
    const Scene* scene=nullptr;
};
class WorkspaceLease;
class Workspace {
    // C++20 malloc implicitly creates these aggregate implicit-lifetime
    // objects. Deliberately do NOT value/default-construct 17 complete Words.
    // Active fields are assigned before any typed read; all State dependencies
    // are copied in full from the freshly bound input/accepted transition.
    static_assert(std::is_aggregate_v<word::Word>&&std::is_trivially_copyable_v<word::Word>&&
        std::is_trivially_destructible_v<word::Word>);
    word::Word* storage_=static_cast<word::Word*>(std::malloc(sizeof(word::Word)*(word::max_words+1)));
    ResultView result_{};
    const Scene* scene_=nullptr;
    int owner_=-1,round_=-1;
    std::uint64_t generation_=0,ticket_=0;
    bool borrowed_=false,compiling_=false,compiled_=false;
    friend class WorkspaceLease;
    friend const ResultView& compile_into(Scene&,WorkspaceLease&,word::Limits,CompileGuard,void*);
    friend const ResultView& compile_family_into(Scene&,WorkspaceLease&,Family,CompileGuard,void*);
public:
    Workspace()=default;
    Workspace(const Workspace&)=delete;Workspace& operator=(const Workspace&)=delete;
    ~Workspace(){std::free(storage_);}
};
inline Workspace& workspace(){static Workspace storage;return storage;}
class WorkspaceLease {
    Workspace* storage_=nullptr;std::uint64_t ticket_=0;
    friend const ResultView& compile_into(Scene&,WorkspaceLease&,word::Limits,CompileGuard,void*);
    friend const ResultView& compile_family_into(Scene&,WorkspaceLease&,Family,CompileGuard,void*);
public:
    WorkspaceLease(Workspace& storage,const Scene& scene) {
        const auto& root=scene.input.initial;
        if(!storage.storage_||storage.borrowed_||scene.status!=Status::Ready||!scene.input.fresh||
            (scene.shared_root&&(!scene.shared_root->valid||scene.shared_root->generation!=scene.root_generation))||
            root.owner<0||root.round<0||root.generation==0)return;
        storage.borrowed_=true;storage.compiling_=storage.compiled_=false;storage.scene_=&scene;
        storage.owner_=root.owner;storage.round_=root.round;storage.generation_=root.generation;
        if(++storage.ticket_==0)++storage.ticket_;
        storage_=&storage;ticket_=storage.ticket_;
    }
    WorkspaceLease(const WorkspaceLease&)=delete;WorkspaceLease& operator=(const WorkspaceLease&)=delete;
    WorkspaceLease(WorkspaceLease&& other) noexcept:storage_(std::exchange(other.storage_,nullptr)),ticket_(other.ticket_){}
    ~WorkspaceLease() {
        if(storage_&&storage_->ticket_==ticket_){storage_->borrowed_=false;storage_->scene_=nullptr;}
    }
    // This is storage lifetime, not action authority. It remains held after a
    // Scene invalidation while the executor consumes its already supplied word.
    explicit operator bool() const{return storage_&&storage_->borrowed_&&storage_->ticket_==ticket_;}
    bool matches(const Scene& scene) const {
        if(!static_cast<bool>(*this)||storage_->scene_!=&scene||scene.status!=Status::Ready||!scene.input.fresh||
            (scene.shared_root&&(!scene.shared_root->valid||scene.shared_root->generation!=scene.root_generation)))return false;
        const auto& root=scene.input.initial;
        return root.owner==storage_->owner_&&root.round==storage_->round_&&root.generation==storage_->generation_;
    }
};
inline void reset_prefix(word::Word& prefix,const word::Input& input) {
    prefix.count=prefix.effect_count=0;prefix.conditional=true;prefix.effects_complete=input.effects_complete;
    prefix.requires_renomination=false;prefix.after=input.initial;
}
inline bool append_active(word::Word& prefix,const word::Candidate& candidate,const word::Transition& transition) {
    if(prefix.count>=word::max_steps||prefix.effect_count+transition.effect_count>word::max_effects)return false;
    auto& trace=prefix.trace[prefix.count++];trace.candidate=candidate;
    trace.effect_begin=prefix.effect_count;trace.effect_count=transition.effect_count;
    trace.requires_actual_refresh=true;trace.may_refuse=true;
    trace.renomination_before=candidate.before.kill_barrier;trace.kill_barrier_after=transition.after.kill_barrier;
    for(unsigned i=0;i<transition.effect_count;++i)prefix.effects[prefix.effect_count++]=transition.effects[i];
    prefix.after=transition.after;
    prefix.requires_renomination|=transition.requires_renomination||trace.renomination_before;
    return true;
}
inline void copy_active(word::Word& target,const word::Word& source) {
    target.count=source.count;target.effect_count=source.effect_count;target.after=source.after;
    target.conditional=source.conditional;target.effects_complete=source.effects_complete;
    target.requires_renomination=source.requires_renomination;
    for(unsigned i=0;i<source.count;++i)target.trace[i]=source.trace[i];
    for(unsigned i=0;i<source.effect_count;++i)target.effects[i]=source.effects[i];
}
// A lease permits ONE invocation. Reentry/recompile returns a separate empty
// invalid view without touching the existing result or its leased word storage.
inline const ResultView& compile_into(Scene& scene,WorkspaceLease& lease,word::Limits limits={24,6,4},
                                      CompileGuard guard=nullptr,void* guard_context=nullptr) {
    static const ResultView invalid=[](){ResultView value;value.reason=word::Reason::InvalidInput;return value;}();
    if(!lease.matches(scene)||lease.storage_->compiling_||lease.storage_->compiled_)return invalid;
    auto& storage=*lease.storage_;storage.compiled_=true;
    struct Busy {bool& value;explicit Busy(bool& flag):value(flag){value=true;}~Busy(){value=false;}} busy(storage.compiling_);
    auto& out=storage.result_;
    out.words={};out.count=out.attempted=out.rejected=0;
    out.admitted=out.transition_limit=out.word_limit=out.depth_limit=out.effect_limit=false;
    out.provider_partial=true;out.reason=word::Reason::None;
    out.owner=storage.owner_;out.round=storage.round_;out.generation=storage.generation_;out.ticket=storage.ticket_;out.scene=&scene;
    scene.compile_deferred=false;
    if(!word::detail::valid(limits)||!word::detail::valid(scene.input)){out.reason=word::Reason::InvalidInput;return out;}
    if(!scene.input.owner_effects_closed){out.reason=word::Reason::UnsupportedClass;return out;}
    out.admitted=true;unsigned serial=0;auto& prefix=storage.storage_[word::max_words];
    for(const auto& family:native_families) {
        if(!family_resources_possible(scene.input,family))continue;
        if(out.count>=limits.words){out.word_limit=true;break;}
        if(out.attempted>=limits.transitions){out.transition_limit=true;break;}
        if(family.pattern.count>limits.depth){out.depth_limit=true;continue;}
        reset_prefix(prefix,scene.input);bool complete=true;
        for(unsigned step=0;step<family.pattern.count;++step) {
            if(out.attempted>=limits.transitions){out.transition_limit=true;complete=false;break;}
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            const auto candidate=nominate(scene,prefix.after,family.pattern.kinds[step],family.away);
            if(!candidate){complete=false;break;}
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            ++out.attempted;
            const auto transition=word::apply_candidate(scene.input,prefix.after,*candidate,++serial);
            if(!transition.admitted){++out.rejected;complete=false;break;}
            if(!append_active(prefix,*candidate,transition)){++out.rejected;out.effect_limit=true;complete=false;break;}
        }
        if(complete){copy_active(storage.storage_[out.count++],prefix);out.words={storage.storage_,out.count};}
    }
    return out;
}
inline const ResultView& compile_family_into(Scene& scene,WorkspaceLease& lease,Family family,
                                             CompileGuard guard=nullptr,void* guard_context=nullptr) {
    static const ResultView invalid=[](){ResultView value;value.reason=word::Reason::InvalidInput;return value;}();
    if(!lease.matches(scene)||lease.storage_->compiling_||lease.storage_->compiled_)return invalid;
    auto& storage=*lease.storage_;storage.compiled_=true;
    struct Busy {bool& value;explicit Busy(bool& flag):value(flag){value=true;}~Busy(){value=false;}} busy(storage.compiling_);
    auto& out=storage.result_;out.words={};out.count=out.attempted=out.rejected=0;
    out.admitted=out.transition_limit=out.word_limit=out.depth_limit=out.effect_limit=false;out.provider_partial=true;out.reason=word::Reason::None;
    out.owner=storage.owner_;out.round=storage.round_;out.generation=storage.generation_;out.ticket=storage.ticket_;out.scene=&scene;
    scene.compile_deferred=false;scene.family_no_exit=false;
    if(!scene.family_specific||scene.prepared_family!=family||
        (family!=Family::MoveBasicDash&&family!=Family::BasicDashTail&&family!=Family::StationaryCampWork)||
        !scene.family_options.exit_filter||scene.camp_index<0||scene.camp_index>=static_cast<int>(scene.input.initial.target_count)||
        !word::detail::valid(scene.input)){out.reason=word::Reason::InvalidInput;return out;}
    if(!scene.input.owner_effects_closed){out.reason=word::Reason::UnsupportedClass;return out;}
    if(family==Family::StationaryCampWork) {
        // One bounded, same-pose service frontier, not a geometric menu. Basic
        // is published before optional Reset suffixes; an unfinished/refused
        // modeled suffix never overwrites it. The caller still prices every
        // native failure prefix, including Basic at a nonquiet live camp.
        out.admitted=true;
        if(!scene.family_options.max_exits){scene.family_no_exit=true;return out;}
        auto& prefix=storage.storage_[word::max_words];reset_prefix(prefix,scene.input);unsigned serial=0;
        const auto camp=static_cast<unsigned>(scene.camp_index);
        const auto& root=scene.input.initial;const auto& target=root.targets[camp];
        if(!root.a||!target.observed||target.hp.lower<=0||word::distance(root.position,target.position)>scene.input.rules.attack_range||
            !vision_at(scene,root.position,target.position)){scene.family_no_exit=true;return out;}
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        const auto basic=word::bind(root,word::Kind::Basic,-1,camp,{},word::Permission::Observed);
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        ++out.attempted;const auto hit=word::apply_candidate(scene.input,root,basic,++serial);
        if(!hit.admitted){++out.rejected;scene.family_no_exit=true;return out;}
        if(!append_active(prefix,basic,hit)){++out.rejected;out.effect_limit=true;return out;}
        const auto after_basic=prefix.after;const auto basic_effects=prefix.effect_count;const auto basic_renomination=prefix.requires_renomination;
        double best_work=0;
        const auto publish=[&]() {
            const auto& remaining=prefix.after.targets[camp];
            const double work=static_cast<double>(target.hp.lower)-remaining.hp.upper;
            if(!(work>best_work))return;
            const int killed=remaining.hp.upper<=0?scene.bindings[camp].id:-1;
            if(!scene.family_options.exit_filter(scene.family_options.exit_context,root.position,killed))return;
            copy_active(storage.storage_[0],prefix);out.count=1;out.words={storage.storage_,1};best_work=work;
        };
        publish();
        // A target known or possibly dead cannot receive a target-specific
        // Basic suffix. Actual executor refresh/renomination remains required.
        if(after_basic.targets[camp].hp.lower>0&&after_basic.c&&!after_basic.cast_used) {
            for(unsigned slot=0;slot<2;++slot) {
                const auto& ability=scene.input.rules.abilities[slot];
                if(ability.kind!=word::AbilityKind::Reset||after_basic.cooldown[slot]!=0||!(ability.target_mask&word::self)||
                    (ability.excludes_walk&&after_basic.walked))continue;
                if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
                const auto reset=word::bind(after_basic,word::Kind::Reset,slot,-1,{},word::Permission::Derived);
                if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
                ++out.attempted;const auto reset_step=word::apply_candidate(scene.input,after_basic,reset,++serial);
                if(!reset_step.admitted){++out.rejected;continue;}
                prefix.count=1;prefix.effect_count=basic_effects;prefix.after=after_basic;prefix.requires_renomination=basic_renomination;
                if(!append_active(prefix,reset,reset_step)){++out.rejected;out.effect_limit=true;continue;}
                if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
                const auto second=word::bind(reset_step.after,word::Kind::Basic,-1,camp,{},word::Permission::Derived);
                if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
                ++out.attempted;const auto second_step=word::apply_candidate(scene.input,reset_step.after,second,++serial);
                if(!second_step.admitted){++out.rejected;continue;}
                if(!append_active(prefix,second,second_step)){++out.rejected;out.effect_limit=true;continue;}
                publish();
            }
        }
        scene.family_no_exit=out.count==0;return out;
    }
    const bool tail=family==Family::BasicDashTail;
    const NativeFamily pattern=tail?NativeFamily{{{word::Kind::Basic,word::Kind::Dash},2},false}:
        NativeFamily{{{word::Kind::Move,word::Kind::Basic,word::Kind::Dash},3},false};
    out.admitted=true;
    if(!family_resources_possible(scene.input,pattern)){scene.family_no_exit=true;return out;}
    auto& prefix=storage.storage_[word::max_words];reset_prefix(prefix,scene.input);unsigned serial=0;
    const auto camp=static_cast<unsigned>(scene.camp_index);
    const unsigned common_steps=tail?1u:2u;
    for(unsigned step=0;step<common_steps;++step) {
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        const auto& state=prefix.after;const auto& target=state.targets[camp];
        word::Candidate candidate;
        if(!tail&&step==0) {
            if(!state.m||state.exclusive_cast||!scene.root_move.get(scene.family_ingress)||
                !ray_path(scene,state.position,scene.family_ingress,false,scene.options.require_known_paths))return out;
            candidate=word::bind(state,word::Kind::Move,-1,-1,scene.family_ingress,word::Permission::Observed);
        } else {
            if(!state.a||!target.observed||target.hp.lower<=0||word::distance(state.position,target.position)>scene.input.rules.attack_range||
                !vision_at(scene,state.position,target.position))return out;
            candidate=word::bind(state,word::Kind::Basic,-1,camp,{},state.prefix==0?word::Permission::Observed:word::Permission::Derived);
        }
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        ++out.attempted;const auto transition=word::apply_candidate(scene.input,state,candidate,++serial);
        if(!transition.admitted){++out.rejected;return out;}
        if(!append_active(prefix,candidate,transition)){++out.rejected;out.effect_limit=true;return out;}
    }
    const auto common=prefix.after;const auto common_effects=prefix.effect_count;const auto common_renomination=prefix.requires_renomination;
    if(scene.input.initial.targets[camp].hp.lower-common.targets[camp].hp.upper<=0){scene.family_no_exit=true;return out;}
    int slot=-1;
    for(unsigned i=0;i<2;++i)if(scene.input.rules.abilities[i].kind==word::AbilityKind::Dash&&common.cooldown[i]==0&&
        common.c&&!common.cast_used&&!(scene.input.rules.abilities[i].excludes_walk&&common.walked)){slot=i;break;}
    if(slot<0){scene.family_no_exit=true;return out;} // no imagined cooldown refresh
    const auto range=std::min(2u,static_cast<unsigned>(scene.input.rules.abilities[slot].range));
    std::array<word::Point,16> endpoints{};unsigned count=0;
    const auto add_endpoint=[&](word::Point q) {
        if(q==common.position)return;
        for(unsigned i=0;i<count;++i)if(endpoints[i]==q)return;
        if(count<endpoints.size())endpoints[count++]=q;
    };
    const auto origin=scene.input.initial.position;
    int back_x=(origin.x>common.position.x)-(origin.x<common.position.x),back_y=(origin.y>common.position.y)-(origin.y<common.position.y);
    if(tail) {
        const auto target=common.targets[camp].position;
        if(std::abs(common.position.x-target.x)>=std::abs(common.position.y-target.y))back_x=(common.position.x>target.x)-(common.position.x<target.x);
        else back_y=(common.position.y>target.y)-(common.position.y<target.y);
    }
    for(unsigned length=range;length>0;--length)add_endpoint({common.position.x+back_x*static_cast<int>(length),common.position.y+back_y*static_cast<int>(length)});
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
        for(unsigned length=range;length>0;--length)add_endpoint({common.position.x+dx*static_cast<int>(length),common.position.y+dy*static_cast<int>(length)});
    const int killed=common.targets[camp].hp.upper<=0?scene.bindings[camp].id:-1;
    const auto limit=std::min(word::max_words,scene.family_options.max_exits);
    for(unsigned i=0;i<count;++i) {
        if(out.count>=limit){out.word_limit=true;break;}
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        const auto q=endpoints[i];
        if(!ray_path(scene,common.position,q,true,scene.options.require_known_paths))continue;
        const bool quiet=scene.family_options.exit_filter(scene.family_options.exit_context,q,killed);
        std::array<word::Point,16> walk_endpoints{};unsigned walk_count=0;
        if(!quiet&&tail&&common.m&&!common.exclusive_cast&&!scene.input.rules.abilities[slot].excludes_walk) {
            const auto move_range=std::min(2u,scene.input.rules.move_speed);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
                for(unsigned length=move_range;length>0;--length) {
                    if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
                    const word::Point end{q.x+dx*static_cast<int>(length),q.y+dy*static_cast<int>(length)};
                    if(ray_path(scene,q,end,false,scene.options.require_known_paths)&&
                        scene.family_options.exit_filter(scene.family_options.exit_context,end,killed)&&walk_count<walk_endpoints.size())
                        walk_endpoints[walk_count++]=end;
                }
        }
        if(!quiet&&!walk_count)continue;
        const auto candidate=word::bind(common,word::Kind::Dash,slot,-1,q,word::Permission::Derived);
        if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
        ++out.attempted;const auto transition=word::apply_candidate(scene.input,common,candidate,++serial);
        if(!transition.admitted){++out.rejected;continue;}
        prefix.count=common_steps;prefix.effect_count=common_effects;prefix.after=common;prefix.requires_renomination=common_renomination;
        if(!append_active(prefix,candidate,transition)){++out.rejected;out.effect_limit=true;continue;}
        if(quiet){copy_active(storage.storage_[out.count++],prefix);out.words={storage.storage_,out.count};continue;}
        const auto dash_effects=prefix.effect_count;const auto dash_renomination=prefix.requires_renomination;
        for(unsigned walk=0;walk<walk_count;++walk) {
            if(out.count>=limit){out.word_limit=true;break;}
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            const auto move=word::bind(transition.after,word::Kind::Move,-1,-1,walk_endpoints[walk],word::Permission::Derived);
            if(guard&&!guard(guard_context)){scene.compile_deferred=true;return out;}
            ++out.attempted;const auto moved=word::apply_candidate(scene.input,transition.after,move,++serial);
            if(!moved.admitted){++out.rejected;continue;}
            prefix.count=common_steps+1;prefix.effect_count=dash_effects;prefix.after=transition.after;prefix.requires_renomination=dash_renomination;
            if(!append_active(prefix,move,moved)){++out.rejected;out.effect_limit=true;continue;}
            copy_active(storage.storage_[out.count++],prefix);out.words={storage.storage_,out.count};
        }
    }
    scene.family_no_exit=out.count==0;return out;
}
// This is a prepared geometry/visibility witness, NOT a promise that the SDK
// must accept the word or that a rejected post-work tail is safe. It excludes
// inferred occupancy release after a modeled kill. Parent supplies the complete
// NPC/opponent/effect/fuel/refusal continuation contract separately.
inline bool whole_path_known_free(const Scene& scene,const word::Word& word) {
    for(unsigned i=0;i<word.count;++i) {
        const auto& c=word.trace[i].candidate;
        if(c.kind==word::Kind::Move&&!ray_path(scene,c.before.position,c.destination,false,true))return false;
        if(c.kind==word::Kind::Dash&&!ray_path(scene,c.before.position,c.destination,true,true))return false;
    }
    return true;
}
// Epoch equality is only a necessary fence. It does not prove that same-input
// resources/effects are unchanged. Discard/invalidate this scene after EVERY
// primitive attempt (accepted or refused), then obtain actual fresh authority.
inline bool epoch_matches(const Scene& scene) {
    return scene.status==Status::Ready&&(!scene.shared_root||(scene.shared_root->valid&&scene.shared_root->generation==scene.root_generation))&&
        scene.input.initial.owner==
        static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))&&
        scene.input.initial.round==static_cast<int>(cambc::round_number());
}
inline void invalidate(Scene& scene){
    if(scene.shared_root&&scene.shared_root->generation==scene.root_generation)scene.shared_root->valid=false;
    scene.shared_root.reset();scene.status=Status::Stale;scene.input.fresh=false;
}
} // namespace wb::capital::word_native
