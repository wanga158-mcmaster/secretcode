#pragma once
#include "context.h"
#include "organization.h"
#include "profile.h"
#include "defaults.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

namespace p8::sdk {
inline bool exact_actor_observations(const p6::Frame& frame,const p6::ViewClock& clock) {
    if (!(frame.clock==clock)||!frame.valid())return false;
    for(int i=0;i<frame.actor_count;++i) {
        const auto& seen=frame.actors[i];
        if(!seen.current(clock,seen.subject)||seen.value.id!=seen.subject||
            (seen.value.team!=p6::Team::ally&&seen.value.team!=p6::Team::enemy))return false;
    }
    return true;
}
// Completeness is supplied only by Adapter::observed_allies, never inferred
// from a Frame's actor count. Channels do not promise an outsider's release.
inline bool observed_allied_approaches_clear(const p6::Frame& frame,const p6::ViewClock& clock,
                                            const p6::Schedule& schedule,std::span<const p6::Point> approaches) {
    if(!exact_actor_observations(frame,clock)||schedule.actor_count<1||schedule.actor_count>p6::max_actors)return false;
    const auto clear=[&](const p6::Actor& actor) {
        if(actor.team!=p6::Team::ally||!actor.alive||p6::actor_index(schedule,actor.id)>=0)return true;
        for(const auto approach:approaches)if(actor.position==approach)return false;
        return true;
    };
    if(!clear(frame.own.actor))return false;
    for(int i=0;i<frame.actor_count;++i)if(!clear(frame.actors[i].value))return false;
    return true;
}
struct SpawnShopVisit {
    bool alive=false,at_spawn=false,stunned=false,channeling=false;
    std::int32_t host_item_shop=0;
};
inline bool verified_spawn_shop_visit(const SpawnShopVisit& visit) {
    return visit.alive&&visit.at_spawn&&!visit.stunned&&!visit.channeling&&visit.host_item_shop!=0;
}
// Data supplied by authenticated own queries only. This small causal boundary
// is also compiled without the SDK in the pure fixture below.
struct TributeSample {
    p6::ViewClock clock{};
    p6::Point position{};
    std::int64_t bag = 0;
    bool alive = false, stunned = false, tribute = false, cancelled = false;
    bool altar_visible_legal = false;
};
struct OwnTributeWatch {
    bool active = false;
    int group = -1;
    p6::Point position{};
    p6::ViewClock last{};
    std::int64_t lower = 0, last_bag = 0;
    std::int64_t earliest_completion = -1;
    bool begin(const TributeSample& started, int source_group, int channel_ticks=1) {
        if (active || source_group < 0 || started.clock.self < 0 || started.clock.round < 0 ||
            started.clock.revision < 0 || channel_ticks <= 0 ||
            !started.alive || started.stunned || !started.tribute || started.cancelled ||
            !started.altar_visible_legal || started.bag <= 0) return false;
        active = true; group = source_group; position = started.position;
        last = started.clock; lower = last_bag = started.bag;
        earliest_completion=std::int64_t(started.clock.round)+channel_ticks; return true;
    }
    void invalidate() { active = false; }
    p6::Observation<Milestones> observe(const TributeSample& now) {
        if (!active) return {};
        if (now.clock.self != last.self || now.clock.revision < 0 || now.clock.round < last.round ||
            std::int64_t(now.clock.round) > std::int64_t(last.round)+1 ||
            (now.clock.round == last.round && now.clock.revision < last.revision) ||
            !now.alive || now.stunned || now.cancelled || !now.altar_visible_legal ||
            !(now.position == position) || now.bag < 0) { invalidate(); return {}; }
        if (now.tribute) {
            if (now.bag < last_bag || now.bag <= 0) { invalidate(); return {}; }
            last = now.clock; last_bag = now.bag; return {};
        }
        // A same-turn query cannot complete a channel whose minimum duration
        // is one turn-end tick. No score delta enters this attribution.
        const bool completed = std::int64_t(now.clock.round) == std::int64_t(last.round)+1 &&
            now.clock.round>=earliest_completion && now.bag == 0;
        invalidate();
        if (!completed) return {};
        return {{group,false,true,0,lower},now.clock,now.clock.self,p6::Knowledge::exact};
    }
};
} // namespace p8::sdk

#ifndef P8_SDK_STATE_ONLY
#include <cambc.h>

namespace p8::sdk {
inline p6::Point point(cambc::vec2 p) { return {p.x,p.y}; }
inline cambc::vec2 native(p6::Point p) { return {p.x,p.y}; }
inline int identity(const cambc::champion& a) {
    return int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(a)));
}
inline int identity(const cambc::camp& a) {
    return int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(a)));
}
inline int identity(const cambc::map::camp_group& g) {
    return int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(g)));
}
inline p6::Champion type(cambc::champion_type c) {
    switch (c) {
#define P8_SDK_TYPE(name) case cambc::champion_type::name: return p6::Champion::name
    P8_SDK_TYPE(tavi); P8_SDK_TYPE(marcella); P8_SDK_TYPE(varg); P8_SDK_TYPE(vendetta);
    P8_SDK_TYPE(vesper_vale); P8_SDK_TYPE(willow); P8_SDK_TYPE(prism); P8_SDK_TYPE(ren);
#undef P8_SDK_TYPE
    default: return p6::Champion::other;
    }
}
inline p6::Champion type(cambc::champion_draft c) {
    switch (c) {
#define P8_SDK_PICK(name) case cambc::champion_draft::name: return p6::Champion::name
    P8_SDK_PICK(tavi); P8_SDK_PICK(marcella); P8_SDK_PICK(varg); P8_SDK_PICK(vendetta);
    P8_SDK_PICK(vesper_vale); P8_SDK_PICK(willow); P8_SDK_PICK(prism); P8_SDK_PICK(ren);
#undef P8_SDK_PICK
    default: return p6::Champion::other;
    }
}
inline cambc::champion_draft draft_type(p6::Champion c) {
    switch (c) {
#define P8_SDK_DRAFT(name) case p6::Champion::name: return cambc::champion_draft::name
    P8_SDK_DRAFT(tavi); P8_SDK_DRAFT(marcella); P8_SDK_DRAFT(varg); P8_SDK_DRAFT(vendetta);
    P8_SDK_DRAFT(vesper_vale); P8_SDK_DRAFT(willow); P8_SDK_DRAFT(prism); P8_SDK_DRAFT(ren);
#undef P8_SDK_DRAFT
    default: throw std::invalid_argument("unsupported P8 draft champion");
    }
}
inline common::DraftRoster roster() {
    common::DraftRoster out;
    const auto selected = cambc::draft::current_draft(cambc::team::ally);
    out.picks = int(selected.size());
    if (out.picks < 3 || out.picks > 5) return out;
    const auto handles = cambc::champions(cambc::team::ally);
    for (int i=0;i<out.picks;++i) {
        out.champions[i] = type(selected[i]);
        for (const auto& a:handles) if (type(a.champion_name()) == out.champions[i]) {
            if (out.actors[i] >= 0) { out.actors[i] = -1; return out; }
            out.actors[i] = identity(a);
        }
    }
    return out;
}
inline p6::Settings settings() {
    return {int(cambc::constants::max_rounds),int(cambc::constants::winning_gems),cambc::constants::capture_time};
}
inline std::uint64_t settings_identity() {
    std::uint64_t h=1469598103934665603ULL;
    auto number=[&](double value) { h=hash_word(h,std::bit_cast<std::uint64_t>(value)); };
    number(cambc::constants::max_rounds); number(cambc::constants::winning_gems);
    number(cambc::constants::capture_time); number(cambc::constants::recall_time);
    number(cambc::constants::respawn_time);
    number(cambc::constants::submit_gem_duration_multiplier);
    number(cambc::constants::submit_gem_base_duration); number(cambc::constants::submit_gem_minimum_duration);
    number(cambc::constants::submit_gems_range); number(cambc::constants::spawn_healing);
    number(cambc::map::width()); number(cambc::map::height());
    for (const auto& design:cambc::champion_designs) if (common::supported_champion(type(design.champion_name()))) {
        const auto s=design.base_stats(); h=hash_word(h,std::uint64_t(type(design.champion_name())));
        number(s.base_health); number(s.health_growth); number(s.base_strength); number(s.strength_growth);
        number(s.base_focus); number(s.focus_growth); number(s.camp_damage_multiplier);
        number(s.vision_radius); number(s.base_move_speed); number(s.base_attack_range);
    }
    return h;
}
inline std::uint64_t equipment_identity(const cambc::champion& me) {
    std::uint64_t h=1469598103934665603ULL; const auto items=me.item_count();
    for (std::uint32_t i=0;i<static_cast<std::uint32_t>(cambc::item_type::count);++i) {
        h=hash_word(h,i); h=hash_word(h,items[static_cast<cambc::item_type>(i)]);
    }
    return h;
}
inline int altar_id(p6::Point p) { return p.y*cambc::map::width()+p.x; }
inline p6::Point altar_point(int id) { return {id%cambc::map::width(),id/cambc::map::width()}; }
struct Snapshot {
    p6::Frame frame{};
    NumericState own{};
    common::DraftRoster draft{};
    p6::Observation<bool> spawn_shop_legal{};
    bool valid = false;
};

// Each Adapter belongs to exactly one authenticated champion. It retains no
// foreign private data, cached live handles, replay state or shared proof.
class Adapter {
    int revision_=0;
    unsigned diagnostic_unavailable_=0;
    bool dispatched_since_observe_=true;
    bool allied_observation_complete_=false;
    common::DraftRoster draft_cache_{};
    Snapshot previous_=native::exact_default<Snapshot>(),current_=native::exact_default<Snapshot>();
    std::optional<cambc::map::camp_group> selected_;
    p6::Observation<Milestones> milestones_{};
    OwnTributeWatch tribute_{};
    p6::Point tribute_altar_{};
    Command last_dispatch_{};
    int useful_attack_round_=-1, useful_attack_group_=-1;

    static bool inside(p6::Point p) {
        return p.x>=0 && p.y>=0 && p.x<cambc::map::width() && p.y<cambc::map::height();
    }
    static int distance(p6::Point a,p6::Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
    static cambc::bitboard terrain() {
        return cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
            cambc::map::spawn_region(cambc::team::enemy);
    }
    static cambc::bitboard blocked() {
        auto b=terrain()|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        if (cambc::self().is_alive()) b.set(cambc::self().position(),false);
        return b;
    }
    static bool visible_altar_legal(p6::Point altar) {
        if (!inside(altar) || !cambc::map::altars().is_set(native(altar)) ||
            !cambc::vision::visible_tiles().is_set(native(altar))) return false;
        for (const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if (point(lock.position()) == altar) return false;
        return true;
    }
    p6::Actor actor(const cambc::champion& a,p6::Team team,const common::DraftRoster& draft,double* focus=nullptr) const {
        p6::Actor v; v.id=identity(a); v.team=team;bool roster_type=false;
        for (int i=0;i<draft.picks && i<5;++i) if (draft.actors[i]==v.id) {
            v.pick_ordinal=i;
            if(team==p6::Team::ally){v.type=draft.champions[i];roster_type=true;}
        }
        if(!roster_type)v.type=type(a.champion_name());
        v.alive=a.is_alive();
        // Private callers pass only self or a current vision champion, never
        // a design. This is the pinned SDK's info() query shared by all these
        // wrappers. Keep its error propagation; discard a resumed observation
        // at the surrounding round checks before any capability reaches work.
        cambc::detail::champion_info raw{};
        cambc::detail::check(cambc::detail::sdk_champion(
            static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(a)),&raw));
        v.position=v.alive?p6::Point{raw.x,raw.y}:p6::Point{-1,-1};
        v.health=double(raw.health);v.max_health=double(raw.max_health);v.strength=double(raw.strength);
        v.attack_range=raw.attack_range;v.ranged=v.attack_range>1;v.bag=a.gem_count();
        if(focus)*focus=double(raw.focus);
        const auto state=cambc::champion_state::from_bits_retain(raw.state);
        v.can_move=v.alive && state.contains(cambc::champion_state::can_move);
        v.can_attack=v.alive && state.contains(cambc::champion_state::can_attack);
        v.channel=state.contains(cambc::champion_state::submitting_gems)?p6::Channel::tribute:
            state.contains(cambc::champion_state::recalling)?p6::Channel::recall:
            state.contains(cambc::champion_state::teleporting)?p6::Channel::teleport:p6::Channel::none;
        v.channel_valid=v.alive && v.channel!=p6::Channel::none && !a.buffs().has_stun();
        if (v.alive && selected_ && selected_->spawn_tiles().is_set(native(v.position))) {
            v.spawn_group=identity(*selected_); v.action=p6::WitnessAction::occupied_spawn;
        }
        // Readiness and presence are capabilities, never a helper attack receipt.
        if (v.id==identity(cambc::self()) && useful_attack_round_==int(cambc::round_number())) {
            v.action=p6::WitnessAction::useful_attack; v.action_group=useful_attack_group_;
        }
        return v;
    }
    TributeSample tribute_sample(const Snapshot& s) const {
        auto& me=cambc::self(); const auto state=me.state();
        return {s.frame.clock,s.frame.own.actor.position,s.frame.own.actor.bag,s.frame.own.actor.alive,
            me.buffs().has_stun(),s.frame.own.actor.channel==p6::Channel::tribute,
            state.contains(cambc::champion_state::submit_gems_cancelled),visible_altar_legal(tribute_altar_)};
    }
    std::optional<cambc::camp> attack_target(int group) const {
        if (!selected_ || identity(*selected_)!=group || !cambc::self().can_attack()) return {};
        const auto spawns=selected_->spawn_tiles(); const auto range=cambc::self().attack_tiles();
        auto camps=cambc::vision::camps();
        std::sort(camps.begin(),camps.end(),[](const auto& a,const auto& b){return identity(a)<identity(b);});
        for (const auto& camp:camps) if (spawns.is_set(camp.spawn_position()) && camp.is_visible() &&
            camp.is_alive() && range.is_set(camp.position())) return camp;
        return {};
    }
public:
    Snapshot observe(const cambc::map::camp_group* selected=nullptr,const Work* work=nullptr) {
        allied_observation_complete_=false;
        const int round=int(cambc::round_number());
        const auto current=[&] {return int(cambc::round_number())==round&&cambc::fuel_remaining()>=20000;};
        const auto unavailable=[&](int stage,int actors=0) {
            tribute_.invalidate();milestones_={};previous_.valid=false;current_.valid=false;
            if(diagnostic_unavailable_<3&&cambc::fuel_remaining()>=8000) {
                ++diagnostic_unavailable_;
                std::printf("p8 DIAG observe_unavailable stage=%d actors=%d r=%d entered=%d fuel=%llu\n",
                    stage,actors,int(cambc::round_number()),round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            }
            return native::exact_default<Snapshot>();
        };
        if(!current())return unavailable(0);
        if(!draft_cache_.picks&&cambc::is_draft())return unavailable(5);
        auto& me=cambc::self();
        if (selected) selected_=*selected; else selected_.reset();
        Snapshot out=native::exact_default<Snapshot>();out.frame.clock={round,identity(me),++revision_};
        if(!draft_cache_.picks) {
            const auto completed=roster();const auto picks=cambc::draft::team_size();
            bool own=false;
            for(int i=0;i<completed.picks&&i<5;++i)own|=completed.actors[i]==identity(me);
            if(!completed.valid()||completed.picks!=int(picks)||!own||!current())return unavailable(5);
            // Only immutable completed gameplay identities are retained. Live
            // actor stats, resources and capabilities are queried below.
            draft_cache_=completed;
        }
        out.draft=draft_cache_;
        double own_focus=0;
        out.frame.own.actor=actor(me,p6::Team::ally,out.draft,&own_focus); out.frame.own.gold=me.gold_count();
        const auto& own_actor=out.frame.own.actor;
        out.frame.own.at_spawn=own_actor.alive && cambc::map::spawn_region(cambc::team::ally).is_set(native(own_actor.position));
        out.frame.own.spawn_recovery_observed=out.frame.own.at_spawn && own_actor.health==own_actor.max_health;
        out.frame.own_team_score=cambc::global_submitted_gems(cambc::team::ally);
        const auto stats=me.base_stats();
        out.own={own_actor.health,own_actor.max_health,own_actor.strength,own_focus,
            double(stats.strength_growth),own_actor.bag,equipment_identity(me)};
        // Query the host's spawn transaction permission; the later bundle
        // action still needs its own can_buy(item,count) and current price.
        // Use the host capability which can_buy queries before adding its
        // affordability check. can_sell is a distinct Tavi-only permission.
        const auto raw_self=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
        cambc::detail::require_self(raw_self);std::int32_t item_shop=0;
        cambc::detail::check(cambc::detail::sdk_champion_can_shop(raw_self,false,&item_shop));
        out.spawn_shop_legal={verified_spawn_shop_visit({out.frame.own.actor.alive,out.frame.own.at_spawn,
            me.buffs().has_stun(),out.frame.own.actor.channel!=p6::Channel::none,item_shop}),
            out.frame.clock,out.frame.clock.self,p6::Knowledge::exact};
        for (const auto side:{cambc::team::ally,cambc::team::enemy}) {
            if(!current())return unavailable(1,out.frame.actor_count);
            auto subjects=cambc::vision::champions(side);
            std::sort(subjects.begin(),subjects.end(),[](const auto& a,const auto& b){return identity(a)<identity(b);});
            bool complete=true;
            for (const auto& a:subjects) {
                if (identity(a)==out.frame.clock.self)continue;
                if (out.frame.actor_count==p6::max_actors) {complete=false;continue;}
                if(!current())return unavailable(2,out.frame.actor_count);
                auto value=actor(a,side==cambc::team::ally?p6::Team::ally:p6::Team::enemy,out.draft);
                if(!current())return unavailable(3,out.frame.actor_count);
                out.frame.actors[out.frame.actor_count++]={value,out.frame.clock,value.id,p6::Knowledge::exact};
            }
            if(side==cambc::team::ally)allied_observation_complete_=complete;
        }
        if (selected_) {
            if(!current())return unavailable(4,out.frame.actor_count);
            p6::Group group; group.id=identity(*selected_); const auto spawns=selected_->spawn_tiles();
            const auto visible=cambc::vision::visible_tiles(); group.all_spawns_visible=true;
            for (const auto p:spawns.positions()) {
                group.original_spawn_visible|=visible.is_set(p); group.all_spawns_visible&=visible.is_set(p);
            }
            const auto sites=selected_->camps();
            if (!sites.empty()) group.spawn=point(sites.front().spawn_position());
            if (work && work->plan.group==group.id && route_valid(work->plan.capture) && route_valid(work->plan.renewal)) {
                group.approach=work->plan.capture.tiles[0];
                group.retreat=work->plan.renewal.tiles[work->plan.renewal.count-1];
            }
            // Bush occupancy is not established by a terrain-visibility bit.
            group.uncontested_now=group.all_spawns_visible && (spawns&cambc::map::bushes()).is_empty() &&
                (spawns&cambc::vision::champion_tiles(cambc::team::enemy)).is_empty();
            if (group.original_spawn_visible) {
                const auto state=selected_->state();
                group.lifecycle=std::holds_alternative<cambc::map::camp_group_alive>(state)?p6::Lifecycle::living:
                    std::holds_alternative<cambc::map::camp_group_reward_available>(state)?p6::Lifecycle::reward_available:p6::Lifecycle::respawning;
                out.frame.group={group,out.frame.clock,group.id,p6::Knowledge::exact};
            }
        }
        out.valid=out.frame.valid() && out.draft.valid() && valid_numeric_state(out.own) && int(cambc::round_number())==round;
        milestones_={};
        if (!out.valid) tribute_.invalidate();
        else if (tribute_.active) milestones_=tribute_.observe(tribute_sample(out));
        if (out.valid && cambc::constants::respawn_time>=2 && selected_ && out.draft.picks>0 &&
            selected_->gem_value()/out.draft.picks>0 && work && work->active && work->stage==PhysicalStage::hold && previous_.valid &&
            previous_.frame.clock.self==out.frame.clock.self && previous_.frame.clock.round+1==round &&
            last_dispatch_.clock.self==previous_.frame.clock.self &&
            last_dispatch_.clock.round==previous_.frame.clock.round &&
            last_dispatch_.clock.revision<=previous_.frame.clock.revision &&
            last_dispatch_.group==work->plan.group && last_dispatch_.action==PhysicalAction::hold &&
            previous_.frame.own.actor.spawn_group==work->plan.group && out.frame.own.actor.spawn_group==work->plan.group &&
            previous_.frame.own.actor.channel==p6::Channel::none && out.frame.own.actor.channel==p6::Channel::none &&
            previous_.frame.own.actor.position==out.frame.own.actor.position &&
            out.frame.own.actor.alive && !me.buffs().has_stun() &&
            out.frame.group.current(out.frame.clock,work->plan.group) &&
            out.frame.group.value.lifecycle==p6::Lifecycle::respawning &&
            previous_.frame.group.current(previous_.frame.clock,work->plan.group) &&
            previous_.frame.group.value.lifecycle==p6::Lifecycle::reward_available &&
            out.frame.own.actor.bag>previous_.frame.own.actor.bag) {
            // A concurrent kill or passive reward may also increase the bag.
            // Claim only the share guaranteed even if every own pick held a
            // capture tile; the actual positive delta is a separate upper cap.
            milestones_={{work->plan.group,true,false,
                std::min<std::int64_t>(out.frame.own.actor.bag-previous_.frame.own.actor.bag,
                    selected_->gem_value()/out.draft.picks),0},
                out.frame.clock,out.frame.clock.self,p6::Knowledge::exact};
        }
        previous_=out; current_=out;
        if(out.valid)dispatched_since_observe_=false;
        return out;
    }
    const p6::Frame* observed_allies(const p6::ViewClock& clock) const {
        if(!current_.valid||!allied_observation_complete_||dispatched_since_observe_||
            revision_!=clock.revision||int(cambc::round_number())!=clock.round||
            !exact_actor_observations(current_.frame,clock))return nullptr;
        return &current_.frame;
    }
    // Only the audited ordinary-U caller may attach its personally queried
    // current group. No live fact is re-clocked and no receipt is processed.
    bool attach_ordinary_group(Snapshot& snapshot,const cambc::map::camp_group& selected,
                               const p6::Observation<p6::Group>& observed,const Work& work) {
        const auto clock=snapshot.frame.clock;const int source=identity(selected);
        const auto current=[&] {
            return snapshot.valid&&current_.valid&&previous_.valid&&!dispatched_since_observe_&&
                snapshot.frame.clock==clock&&current_.frame.clock==clock&&previous_.frame.clock==clock&&
                revision_==clock.revision&&identity(cambc::self())==clock.self&&
                int(cambc::round_number())==clock.round&&cambc::fuel_remaining()>=25000;
        };
        if(!current()||work.active||selected_||!snapshot.frame.valid()||
            !snapshot.frame.own.actor.alive||snapshot.frame.own.actor.bag!=0||
            snapshot.frame.own.actor.channel!=p6::Channel::none||
            !observed.current(clock,source)||observed.value.id!=source||
            !observed.value.original_spawn_visible||observed.value.lifecycle==p6::Lifecycle::unknown)return false;
        if(!(snapshot.frame.own==current_.frame.own)||!(snapshot.frame.own==previous_.frame.own)||
            snapshot.frame.actor_count!=current_.frame.actor_count||snapshot.frame.actor_count!=previous_.frame.actor_count||
            snapshot.frame.own_team_score!=current_.frame.own_team_score||
            snapshot.draft.picks!=current_.draft.picks||snapshot.draft.actors!=current_.draft.actors||
            snapshot.draft.champions!=current_.draft.champions||!(snapshot.spawn_shop_legal==current_.spawn_shop_legal)||
            snapshot.own.health!=current_.own.health||snapshot.own.max_health!=current_.own.max_health||
            snapshot.own.strength!=current_.own.strength||snapshot.own.focus!=current_.own.focus||
            snapshot.own.growth!=current_.own.growth||snapshot.own.bag!=current_.own.bag||
            snapshot.own.equipment_hash!=current_.own.equipment_hash)return false;
        for(int i=0;i<snapshot.frame.actor_count;++i)
            if(!(snapshot.frame.actors[i]==current_.frame.actors[i])||
                !(snapshot.frame.actors[i]==previous_.frame.actors[i]))return false;
        const auto sites=selected.camps();if(sites.empty())return false;
        auto group=observed;
        // Match observe's static fields, including its inactive-Work predicate.
        group.value.spawn=point(sites.front().spawn_position());
        group.value.approach={};group.value.retreat={};
        if(work.plan.group==source&&route_valid(work.plan.capture)&&route_valid(work.plan.renewal)) {
            group.value.approach=work.plan.capture.tiles[0];
            group.value.retreat=work.plan.renewal.tiles[work.plan.renewal.count-1];
        }
        struct Occupancy {
            int spawn=-1,action_group=-1;
            p6::WitnessAction action=p6::WitnessAction::none;
        };
        std::array<Occupancy,p6::max_actors+1> occupancy{};
        const auto stage=[&](const p6::Actor& actor) {
            Occupancy value;
            if(actor.alive)for(const auto& camp:sites)if(point(camp.spawn_position())==actor.position) {
                value.spawn=source;value.action=p6::WitnessAction::occupied_spawn;break;
            }
            if(actor.id==clock.self&&useful_attack_round_==clock.round) {
                value.action=p6::WitnessAction::useful_attack;value.action_group=useful_attack_group_;
            }
            return value;
        };
        occupancy[0]=stage(snapshot.frame.own.actor);
        for(int i=0;i<snapshot.frame.actor_count;++i)occupancy[i+1]=stage(snapshot.frame.actors[i].value);
        std::optional<cambc::map::camp_group> retained(selected);
        static_assert(std::is_nothrow_move_assignable_v<decltype(selected_)>);
        if(!current())return false;
        selected_=std::move(retained);
        const auto publish=[&](p6::Frame& frame) {
            frame.group=group;
            const auto actor=[](p6::Actor& value,const Occupancy& patch) {
                value.spawn_group=patch.spawn;value.action=patch.action;value.action_group=patch.action_group;
            };
            actor(frame.own.actor,occupancy[0]);
            for(int i=0;i<frame.actor_count;++i)actor(frame.actors[i].value,occupancy[i+1]);
        };
        publish(snapshot.frame);publish(current_.frame);publish(previous_.frame);
        return true;
    }
    bool admission_local(const Snapshot& snapshot,const Plan& plan,
                         std::span<const p6::CycleCertificate> certificates,const Work& work,LocalFrame& out) const {
        out.admission_only=true;out.action_reserve=8000;
        out.fuel_remaining=std::int64_t(cambc::fuel_remaining());out.fuel_resumed=true;
        out.current_round=-1;out.safety_count=0;out.legal.knowledge=p6::Knowledge::unknown;
        out.legal.value={};out.milestones={};
        const auto clock=snapshot.frame.clock;
        const auto current=[&] {
            return snapshot.valid&&current_.valid&&!dispatched_since_observe_&&
                snapshot.frame.clock==current_.frame.clock&&revision_==clock.revision&&
                identity(cambc::self())==clock.self&&int(cambc::round_number())==clock.round&&
                cambc::fuel_remaining()>=std::uint64_t(out.action_reserve);
        };
        if(!current()||work.active||plan.organization!=Organization::U||plan.crew_count!=1||
            plan.collector!=clock.self||plan.ingress.count<2||!selected_||identity(*selected_)!=plan.group||
            !snapshot.frame.valid()||!(snapshot.frame.own==current_.frame.own)||
            !snapshot.frame.own.actor.alive||snapshot.frame.own.actor.bag!=0||
            snapshot.frame.own.actor.channel!=p6::Channel::none||
            !snapshot.frame.group.current(clock,plan.group)||
            snapshot.frame.group.value.lifecycle!=p6::Lifecycle::living||
            certificates.size()!=1)return false;
        // Camps do not move. Waiting at this origin cannot add target damage,
        // even if another actor wakes the group before our next owner turn.
        const auto sites=selected_->camps();if(sites.empty())return false;
        for(const auto& camp:sites) {
            if(!current())return false;
            const int range=camp.attack_range();
            if(range<0||distance(snapshot.frame.own.actor.position,point(camp.spawn_position()))<=range)return false;
        }
        auto& me=cambc::self();
        if(!me.is_alive()||me.buffs().has_stun()||!current())return false;
        out.frame=snapshot.frame;out.current_round=clock.round;
        out.safety_count=1;out.safety[0]=certificates[0];out.milestones=milestones_;
        out.legal.value.safe_now=true;
        out.legal.clock=clock;out.legal.subject=clock.self;out.legal.knowledge=p6::Knowledge::exact;
        if(!current())return false;
        out.fuel_remaining=std::int64_t(cambc::fuel_remaining());out.fuel_resumed=false;
        return true;
    }
    LocalFrame local(const Snapshot& snapshot,const Plan& plan,std::span<const p6::CycleCertificate> certificates,
                     bool safe_now,bool recall_safe,bool renewal_productive,
                     std::optional<p6::Point> escape={},const Work* work=nullptr) const {
        LocalFrame out=native::exact_default<LocalFrame>();out.frame=snapshot.frame;out.current_round=int(cambc::round_number());
        out.fuel_remaining=std::int64_t(cambc::fuel_remaining());
        out.fuel_resumed=!snapshot.valid || !current_.valid || snapshot.frame.clock.round!=out.current_round ||
            !(snapshot.frame.clock==current_.frame.clock);
        if (out.fuel_resumed || certificates.size()>out.safety.size()) return out;
        out.safety_count=int(certificates.size()); std::copy(certificates.begin(),certificates.end(),out.safety.begin());
        out.milestones=milestones_; auto& me=cambc::self(); Legality legal;
        const bool alive=me.is_alive(), stunned=me.buffs().has_stun();
        legal.safe_now=safe_now && alive && !stunned;
        legal.attack_legal=legal.safe_now && bool(attack_target(plan.group));
        const auto state=me.state(); const bool channel=me.is_recalling()||me.is_submitting_gems()||me.is_teleporting();
        legal.recall_safe=legal.safe_now && recall_safe && !channel &&
            !state.contains(cambc::champion_state::recall_started) && !state.contains(cambc::champion_state::recall_cancelled);
        legal.renewal_productive=renewal_productive;
        const auto altar=altar_point(plan.altar);
        legal.altar_legal=legal.safe_now && !channel && me.gem_count()>0 &&
            !state.contains(cambc::champion_state::submit_gems_started) &&
            !state.contains(cambc::champion_state::submit_gems_cancelled) &&
            distance(snapshot.frame.own.actor.position,altar)<=cambc::constants::submit_gems_range && visible_altar_legal(altar);
        const auto obstacles=blocked(), static_walls=terrain();
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access=legal.routes[int(leg)]; const auto& route=route_for(plan,leg);
            access.assumptions_hold=route_valid(route);
            for (int i=0;i<route.count && i<max_route_tiles;++i)
                if (!inside(route.tiles[i]) || static_walls.is_set(native(route.tiles[i]))) access.assumptions_hold=false;
            if (!access.assumptions_hold) continue;
            int at=work ? work->cursor[int(leg)] : 0;
            if (at<0 || at>=route.count) continue;
            const auto position=snapshot.frame.own.actor.position;
            if (at+1<route.count && position==route.tiles[at+1]) ++at;
            else if (!(position==route.tiles[at])) {
                at=-1; for (int i=0;i<route.count;++i) if (position==route.tiles[i]) {at=i;break;}
            }
            if (at<0 || at+1>=route.count) continue;
            access.next_tile=route.tiles[at+1];
            if (leg==Leg::bank)for (const auto& seen:snapshot.frame.actors)
                if (seen.current(snapshot.frame.clock,seen.subject)&&seen.value.team==p6::Team::ally&&
                    seen.value.alive&&seen.value.position==access.next_tile)access.friendly_queue=true;
            access.next_move_legal=alive && !stunned && !channel && me.can_move() &&
                distance(position,access.next_tile)==1 && !obstacles.is_set(native(access.next_tile));
        }
        const bool can_cancel=(me.is_recalling() && !state.contains(cambc::champion_state::recall_cancelled)) ||
            (me.is_submitting_gems() && !state.contains(cambc::champion_state::submit_gems_cancelled));
        if (escape && inside(*escape) && alive && !stunned && (!channel || can_cancel) && me.can_move() &&
            distance(snapshot.frame.own.actor.position,*escape)==1 && !obstacles.is_set(native(*escape))) {
            legal.escape_legal=true; legal.escape_tile=*escape;
        }
        out.legal={legal,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        if (int(cambc::round_number())!=out.current_round) { out.fuel_resumed=true; out.legal={}; }
        return out;
    }
    ActionReceipt dispatch(const Command& command,const LocalFrame& local,const Work& work) {
        dispatched_since_observe_=true;
        ActionReceipt result{command.clock,{int(cambc::round_number()),identity(cambc::self()),revision_},
            command.token,command.action,false,false};
        if(local.admission_only)return result;
        if (!current_.valid || !local.fresh() || local.fuel_resumed || !(command.clock==local.frame.clock) ||
            !(command.clock==current_.frame.clock) || !(result.current_clock==command.clock) ||
            !work.pending || command.token!=work.issued.token || command.action!=work.issued.action ||
            !(command.clock==work.issued.clock) || command.route_hash!=work.issued.route_hash ||
            !(command.target==work.issued.target) || command.group!=work.issued.group || command.leg!=work.issued.leg)
            return result;
        auto& me=cambc::self(); const auto& legal=local.legal.value;
        auto ready=[&]() {
            result.current_clock={int(cambc::round_number()),identity(me),revision_};
            const bool ok=result.current_clock==command.clock &&
                cambc::fuel_remaining()>=std::uint64_t(local.action_reserve);
            if (!ok) tribute_.invalidate();
            return ok;
        };
        if (me.is_alive() && (point(me.position())!=local.frame.own.actor.position ||
            double(me.health())!=local.frame.own.actor.health || std::int64_t(me.gem_count())!=local.frame.own.actor.bag)) return result;
        const bool channel=me.is_recalling()||me.is_submitting_gems()||me.is_teleporting();
        if (command.action==PhysicalAction::preserve_channel) {
            result.attempted=true; result.success=me.is_alive() && channel && legal.safe_now && !me.buffs().has_stun();
        } else if (command.action==PhysicalAction::yield || command.action==PhysicalAction::hold ||
                   command.action==PhysicalAction::inspect || command.action==PhysicalAction::recover) {
            result.attempted=result.success=true;
        } else {
            if (!me.is_alive() || me.buffs().has_stun() || (channel && command.action!=PhysicalAction::escape) ||
                local.fuel_remaining<local.action_reserve || cambc::fuel_remaining()<std::uint64_t(local.action_reserve)) return result;
            if (command.action==PhysicalAction::move) {
                if (int(command.leg)<0 || int(command.leg)>=int(legal.routes.size())) return result;
                const auto& step=legal.routes[int(command.leg)];
                if (!legal.safe_now || !step.assumptions_hold || !step.next_move_legal ||
                    !(command.target==step.next_tile) || command.route_hash!=plan_route_hash(work.plan) ||
                    !inside(command.target) || distance(point(me.position()),command.target)!=1 ||
                    !me.can_move() || blocked().is_set(native(command.target))) return result;
                if (!ready()) return result;
                result.attempted=true; result.success=me.move_absolute(native(command.target));
            } else if (command.action==PhysicalAction::escape) {
                if (!legal.escape_legal || !(command.target==legal.escape_tile) || !me.can_move() ||
                    !inside(command.target) || distance(point(me.position()),command.target)!=1 || blocked().is_set(native(command.target))) return result;
                if (!ready()) return result;
                if (channel) {
                    const auto state=me.state();
                    if (me.is_teleporting()) return result;
                    if (me.is_recalling() && !state.contains(cambc::champion_state::recall_cancelled)) {
                        result.attempted=true; me.cancel_recall();
                    } else if (me.is_submitting_gems() && !state.contains(cambc::champion_state::submit_gems_cancelled)) {
                        result.attempted=true; me.cancel_submit_gems();
                    }
                    else return result;
                    tribute_.invalidate();
                    if (!me.can_move() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting() ||
                        blocked().is_set(native(command.target)) || !ready()) return result;
                }
                tribute_.invalidate(); result.attempted=true; result.success=me.move_absolute(native(command.target));
            } else if (command.action==PhysicalAction::attack) {
                if (!legal.safe_now || !legal.attack_legal) return result;
                auto target=attack_target(command.group); if (!target) return result;
                const double before=target->health(); if (!ready()) return result;
                result.attempted=true; me.attack(*target);
                // Refresh the target rather than reading a pre-action view.
                for (const auto& after:cambc::vision::camps()) if (identity(after)==identity(*target)) {
                    result.success=double(after.health())<before; break;
                }
                if (!target->is_alive()) result.success=true;
                if (result.success) { useful_attack_round_=command.clock.round; useful_attack_group_=command.group; }
            } else if (command.action==PhysicalAction::tribute) {
                const auto altar=altar_point(work.plan.altar);
                if (!legal.safe_now || !legal.altar_legal || !visible_altar_legal(altar) || me.gem_count()==0 ||
                    distance(point(me.position()),altar)>cambc::constants::submit_gems_range) return result;
                if (!ready()) return result;
                result.attempted=true; me.submit_gems(native(altar));
                result.success=me.is_submitting_gems(); tribute_altar_=altar;
                if (result.success) {
                    Snapshot started=current_; started.frame.own.actor.channel=p6::Channel::tribute;
                    started.frame.own.actor.bag=me.gem_count();
                    const double ticks=std::max(double(cambc::constants::submit_gem_minimum_duration),
                        std::ceil(double(started.frame.own.actor.bag)*cambc::constants::submit_gem_duration_multiplier)+
                        cambc::constants::submit_gem_base_duration);
                    // Consecutive alive owner observations exclude a hidden
                    // death/respawn only when respawn cannot fit between them.
                    if (cambc::constants::respawn_time>=2 && std::isfinite(ticks) && ticks>=1 && ticks<=std::numeric_limits<int>::max())
                        tribute_.begin(tribute_sample(started),command.group,int(ticks));
                }
            } else if (command.action==PhysicalAction::recall) {
                if (!legal.safe_now || !legal.recall_safe) return result;
                if (!ready()) return result;
                tribute_.invalidate(); result.attempted=true; me.recall(); result.success=me.is_recalling();
            }
        }
        result.current_clock={int(cambc::round_number()),identity(me),revision_};
        if (result.current_clock==command.clock && result.attempted) last_dispatch_=command;
        else tribute_.invalidate();
        return result;
    }
};
} // namespace p8::sdk
#endif
