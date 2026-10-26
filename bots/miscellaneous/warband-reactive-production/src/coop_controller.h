#pragma once
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <vector>
#include "config.h"
#include "coop_jobs.h"
#include "coop_micro.h"
#include "camp_mask_ledger.h"
#include "staging_egress.h"

namespace wb {
inline const char* error_stage="entry";
using cambc::vec2;
inline int dist(vec2 a, vec2 b) { return int(a.chebyshev_distance(b)); }
inline int native(const cambc::champion& c) { return int(static_cast<cambc::handle_champion_t>(c)); }
inline cambc::bitboard one(vec2 p) { cambc::bitboard b; b.set(p); return b; }
inline cambc::bitboard around(vec2 p, int n) { cambc::bitboard b; b.draw_chebyshev(p,n); return b; }
inline cambc::bitboard border(const cambc::bitboard& source) { cambc::bitboard b; b.clear(); for(auto p:source.positions()) b|=around(p,1); return b; }

inline void production_draft() {
    constexpr std::array order{cambc::champion_draft::marcella, cambc::champion_draft::ryth,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale, cambc::champion_draft::tavi};
    const auto picked = cambc::draft::current_draft(cambc::team::ally);
    for (auto c : order) if (std::find(picked.begin(),picked.end(),c)==picked.end()) {
        cambc::draft::pick(c); return;
    }
}

struct GroupWork {
    cambc::map::camp_group group;
    cambc::bitboard tiles;
    std::vector<Monster> monsters;
    std::vector<float> maximum_hp;
    std::vector<bool> member_visible;
    std::vector<std::uint8_t> observation_attempted;
    // 0 dead, 1 freshly observed asleep, 2 freshly observed awake,
    // 3 unobserved: possibly awake. A selected attack adds its whole group.
    std::vector<std::uint8_t> activity;
    std::vector<std::size_t> mask_keys;
    std::vector<int> health_round;
    bool observed=false, reward=false, respawning=false, all_known=false,state_known=false;
    bool masks_dirty=false;
    int seen=-1, earliest_respawn=-1;
};
struct QuoteBody {
    int id,range; vec2 position; float health,maximum,strength; unsigned gems;
};

class Production {
    std::vector<GroupWork> groups_;
    std::vector<cambc::map::camp_group> initial_groups_;
    std::array<cambc::camp_stats,6> designs_{};
    std::array<bool,6> designs_loaded_{};
    std::size_t initial_cursor_=0;
    std::size_t seed_group_=0,seed_member_=0,mask_key_count_=0;
    CampMaskLedger masks_;
    struct PendingMember {std::size_t group,member;bool alive;float hp;std::uint8_t activity;};
    std::optional<PendingMember> pending_member_;
    mutable unsigned ledger_steps_left_=8;
    std::vector<cambc::champion> allies_;
    std::vector<cambc::champion> enemy_handles_;
    std::vector<NativeChampionObservation> visible_enemies_;
    std::vector<vec2> spawn_tiles_, altars_;
    std::vector<std::uint8_t> blocked_bytes_;
    std::array<std::uint8_t,8> mail_{};
    int slot_=-1, target_=-1, excluded_=-1, trace_left_=12, round_=-1, scan_cursor_=0;
    int frontier_member_=-1,frontier_direction_=-1;
    vec2 frontier_landing_{};
    enum class Search { chosen, deferred, exhausted, empty, need_observation, no_current_command, no_safe_prefix };
    Search search_=Search::empty;
    bool admitted_=false;
    bool proposal_requested_=false;
    bool initializing_=false, initialized_=false, recovering_=false;
    bool micro_geometry_done_=false;
    bool camp_masks_dirty_=false;
    bool occupancy_dirty_=true;
    cambc::bitboard blocked_, danger_, terrain_, camp_blocked_, camp_danger_;
    std::uint64_t observe_cost_=0, setup_cost_=0;
    std::uint64_t route_cost_=0, pads_cost_=0, episode_cost_=0, escape_cost_=0;
    int examined_=0;
    mutable cambc::distance_field escape_field_;
    mutable cambc::bitboard escape_reached_;
    mutable bool escape_ready_=false;
    mutable int escape_group_=-2;
    mutable cambc::bitboard wake_mask_;
    mutable int wake_mask_group_=-1;
    mutable std::uint32_t wake_members_=0;
    mutable std::size_t wake_member_cursor_=0;
    mutable CampMaskLedger wake_ledger_;
    mutable bool geometry_deferred_=false;
    mutable int wake_service_job_=-1;
    int preferred_certificate_=-1;
    PreparedMicroFrame prepared_micro_;
    std::uint64_t input_generation_=0,view_revision_=0;
    std::vector<staging::Support> staging_supports_;
    std::vector<std::uint8_t> staging_wall_witness_;
    vec2 staging_origin_{-1,-1};
    std::uint8_t staging_failures_=0;
    staging::Kind staging_result_=staging::Kind::Unsupported;

    MicroFrameStamp micro_stamp(const cambc::map::camp_group& group) const {
        return {std::uint32_t(native(cambc::self())),
            std::uint32_t(static_cast<cambc::handle_camp_group_t>(group)),std::uint32_t(round_),
            input_generation_,view_revision_};
    }
    void invalidate_micro_view() {prepared_micro_.reset();++view_revision_;}
    bool strike_current(const cambc::map::camp_group& group,bool* output_attempted) {
        bool attempted=false;
        try {
            // The token is one-use and consumed before the first primitive.
            // Priority and team-memory writes are the only intervening commands;
            // both preserve this camp/personal-view frame.
            const bool acted=prepared_micro_.sealed()
                ? strike_preobserved(group,prepared_micro_,micro_stamp(group),&attempted)
                : strike(group,&attempted);
            if(attempted) invalidate_micro_view();
            if(output_attempted) *output_attempted=attempted;
            return acted;
        } catch(...) {
            if(attempted) invalidate_micro_view();
            if(output_attempted) *output_attempted=attempted;
            throw;
        }
    }

    std::size_t index(vec2 p) const {return std::size_t(p.y)*cambc::map::width()+p.x;}
    bool in_map(vec2 p) const {return p.x>=0&&p.y>=0&&p.x<cambc::map::width()&&p.y<cambc::map::height();}
    bool local_blocked(vec2 p) const {return !in_map(p)||(blocked_bytes_[index(p)>>3]&(1u<<(index(p)&7)));}
    void packed_set(std::vector<std::uint8_t>& bytes,vec2 p) const {
        if(in_map(p)) bytes[index(p)>>3]|=std::uint8_t(1u<<(index(p)&7));
    }
    cambc::bitboard packed_board(std::span<const std::uint8_t> bytes) const {
        cambc::bitboard result;result.import_linear_buffer(bytes);return result;
    }

    void publish(int group) {
        if(cambc::round_number()!=round_) return;
        error_stage="intent-publish";
        if (slot_<0 || slot_>=4 || allies_.size()>5 || cambc::shared_memory_buffer::size()<8) return;
        if (cambc::round_number()!=round_ || cambc::fuel_remaining()<7000) return;
        const int code=group>=0 && group<31 ? group+1 : 0;
        const std::uint16_t word=(std::uint16_t(round_)<<5)|code;
        std::array<std::uint8_t,2> bytes{std::uint8_t(word),std::uint8_t(word>>8)};
        cambc::shared_memory.write(std::size_t(slot_)*2,std::span<const std::uint8_t>{bytes});
    }
    int intent(int slot) const {
        if (slot<0 || slot>=4 || allies_.size()>5) return -1;
        const unsigned word=unsigned(mail_[2*slot])|(unsigned(mail_[2*slot+1])<<8);
        const int stamp=int(word>>5),code=int(word&31);
        return code && stamp<=round_ && round_-stamp<=1 ? code-1 : -1;
    }
    void trace(const char* event,int job) {
        if constexpr (config::trace) if(trace_left_-- > 0 && cambc::fuel_remaining()>10000)
            std::cout<<"wb r"<<round_<<" s"<<slot_<<" g"<<job<<" "<<event
                <<" p"<<cambc::self().position().x<<","<<cambc::self().position().y
                <<" cursor"<<scan_cursor_<<" search"<<int(search_)
                <<" fuel"<<cambc::fuel_remaining()<<" obs"<<observe_cost_<<" setup"<<setup_cost_
                <<" routes"<<route_cost_<<" pads"<<pads_cost_<<" episode"<<episode_cost_
                <<" escape"<<escape_cost_<<" examined"<<examined_<<'\n';
    }
    void camp_masks() {
        if(!masks_.valid()) throw std::runtime_error("incomplete camp mask ledger publication");
        error_stage="camp-mask-import";
        camp_blocked_=packed_board(masks_.living_bytes());camp_danger_=packed_board(masks_.threat_bytes());
    }
    bool advance_ledger(CampMaskLedger& ledger) const {
        while(ledger.pending_update()&&ledger_steps_left_>0
            &&cambc::round_number()==round_&&cambc::fuel_remaining()>=40000) {
            --ledger_steps_left_;
            if(ledger.advance(128)==LedgerStep::failed) throw std::runtime_error("invalid camp mask ledger update");
            if(cambc::round_number()!=round_) break;
        }
        return !ledger.pending_update();
    }
    bool finish_member_update() {
        if(!pending_member_) return true;
        if(!advance_ledger(masks_)) return false;
        const auto pending=*pending_member_;auto& w=groups_[pending.group];auto& m=w.monsters[pending.member];
        m.alive=pending.alive;m.hp=pending.hp;w.activity[pending.member]=pending.activity;
        camp_masks_dirty_=true;pending_member_.reset();return true;
    }
    bool update_member(GroupWork& w,std::size_t j,bool alive,float hp,std::uint8_t activity) {
        auto& m=w.monsters[j];
        const bool old_threat=m.alive&&m.damage>0&&w.activity[j]!=1;
        const bool new_threat=alive&&m.damage>0&&activity!=1;
        if(m.alive==alive&&old_threat==new_threat) {m.hp=hp;w.activity[j]=activity;return true;}
        const auto group=std::size_t(&w-groups_.data());
        if(!masks_.begin_transition(w.mask_keys[j],m.pos.x,m.pos.y,m.range,m.alive,old_threat,alive,new_threat))
            throw std::runtime_error("camp mask ledger premise mismatch");
        pending_member_=PendingMember{group,j,alive,hp,activity};
        return finish_member_update()&&cambc::round_number()==round_;
    }
    void init() {
        error_stage="initialize";
        const auto initial_fuel=cambc::fuel_remaining();
        const int initial_round=cambc::round_number();
        if(!initializing_) {
            allies_=cambc::champions(cambc::team::ally);
            enemy_handles_=cambc::champions(cambc::team::enemy);
            spawn_tiles_=cambc::map::spawn_region(cambc::team::ally).positions();
            altars_=cambc::map::altars().positions();
            for(std::size_t i=0;i<allies_.size();++i) if(native(allies_[i])==native(cambc::self())) slot_=int(i);
            initial_groups_=cambc::map::camp_groups();
            micro_initialize(initial_groups_);
            std::size_t members=0;for(const auto& group:initial_groups_) members+=group.camps().size();
            if(!masks_.reset(cambc::map::width(),cambc::map::height(),members))
                throw std::runtime_error("invalid camp mask ledger dimensions");
            terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()
                |cambc::map::spawn_region(cambc::team::enemy);
            initializing_=true;
        }
        if(!micro_geometry_done_) {
            error_stage="micro-geometry-seed";
            auto status=micro_initialize_status();
            while(status==MicroInitResult::deferred&&ledger_steps_left_>0
                &&cambc::round_number()==initial_round&&cambc::fuel_remaining()>=40000) {
                --ledger_steps_left_;
                status=micro_initialize_step(128);
                if(cambc::round_number()!=initial_round) return;
            }
            if(status==MicroInitResult::deferred) return;
            // Unsupported static support disables area casts only. Completed
            // setup still yields before ordinary metadata/observation work.
            micro_geometry_done_=true;
            return;
        }
        for(;initial_cursor_<initial_groups_.size();) {
            // Preserve completed groups; never restart the static world on the
            // next input or charge initialization as a free planning prefix.
            if(cambc::round_number()!=initial_round||cambc::fuel_remaining()<40000) return;
            const auto& g=initial_groups_[initial_cursor_];
            GroupWork work{g,g.spawn_tiles(),{}};
            for(auto c:g.camps()) {
                const auto kind=static_cast<std::size_t>(c.kind());
                if(kind>=designs_.size()) throw std::runtime_error("unknown camp design");
                if(!designs_loaded_[kind]) {designs_[kind]=c.base_stats();designs_loaded_[kind]=true;}
                const auto s=designs_[kind];
                work.monsters.push_back({int(static_cast<cambc::handle_camp_t>(c)),c.spawn_position(),
                    s.base_health,s.base_strength,s.base_attack_range,true});
                work.maximum_hp.push_back(s.base_health);
                work.member_visible.push_back(false);work.observation_attempted.push_back(0);
                work.activity.push_back(3);
                work.mask_keys.push_back(mask_key_count_+work.mask_keys.size());
                work.health_round.push_back(-1);
                if(s.base_strength>0) staging_supports_.push_back({c.spawn_position().x,c.spawn_position().y,
                    s.base_attack_range,s.base_strength});
            }
            mask_key_count_+=work.monsters.size();
            groups_.push_back(std::move(work));
            ++initial_cursor_;
        }
        error_stage="camp-mask-seed";
        for(;seed_group_<groups_.size();) {
            auto& work=groups_[seed_group_];
            if(seed_member_>=work.monsters.size()) {++seed_group_;seed_member_=0;continue;}
            const auto& m=work.monsters[seed_member_];
            if(!masks_.pending_update()
                &&!masks_.begin_transition(work.mask_keys[seed_member_],m.pos.x,m.pos.y,m.range,false,false,m.alive,m.damage>0))
                throw std::runtime_error("invalid camp mask ledger seed");
            if(!advance_ledger(masks_)) return;
            ++seed_member_;
            if(cambc::round_number()!=initial_round) return;
        }
        if(cambc::round_number()!=initial_round||cambc::fuel_remaining()<25000) return;
        camp_masks();
        // Actual guest binding parity for the complete current camp mask. The
        // packed row-major LSB codec also has independent native-engine coverage.
        const auto bytes=camp_blocked_.export_linear_buffer();
        const auto imported=packed_board(bytes);
        if(!(camp_blocked_^imported).is_empty()) throw std::runtime_error("packed mask caller parity");
        setup_cost_=cambc::round_number()==initial_round ? initial_fuel-cambc::fuel_remaining() : 0;
        initialized_=true;
    }
    bool observe(const NativeChampionObservation& owner) {
        prepared_micro_.reset();
        error_stage="observe";
        const auto initial_fuel=cambc::fuel_remaining();
        const auto visible=cambc::vision::visible_tiles();
        auto visible_bytes=visible.export_linear_buffer();
        MicroFrame selected_rows;int captured_group=-1;
        const auto seen=[&](vec2 p){return (visible_bytes[index(p)>>3]&(1u<<(index(p)&7)))!=0;};
        for(auto& w:groups_) {
            for(std::size_t j=0;j<w.monsters.size();++j) {
                w.member_visible[j]=seen(w.monsters[j].pos);
                if(w.member_visible[j]) w.observation_attempted[j]=0;
            }
            w.observed=std::any_of(w.monsters.begin(),w.monsters.end(),[&](const auto& m){return seen(m.pos);});
            w.all_known=false;w.state_known=false;
            bool group_changed=w.masks_dirty;
            if(!w.observed) {
                // A death observation does not establish the hidden group's
                // current generation. It may have cleared, paid and respawned
                // without another observation, including from a partial clear.
                // Restore possible occupancy immediately on lost visibility;
                // this is uncertainty, never known work or another reward.
                for(std::size_t j=0;j<w.monsters.size();++j) {
                    const auto& m=w.monsters[j];group_changed=group_changed||!m.alive||w.activity[j]!=3;
                    if(!update_member(w,j,true,m.alive?m.hp:w.maximum_hp[j],3)) return false;
                }
                if(group_changed) w.masks_dirty=false;
                if(cambc::round_number()!=round_) return false;
                continue;
            }
            w.reward=false;w.respawning=false;
            w.seen=round_;w.all_known=true;w.state_known=true;
            const auto members=w.group.camps();bool any_fresh_alive=false;
            const int group_index=int(&w-groups_.data());
            const bool detail=group_index==target_||group_index==preferred_certificate_;
            struct MemberFrame {cambc::camp_state state;float hp{},damage{};bool health_read{};};
            std::vector<MemberFrame> member_frame(members.size());
            // Read-only capture stays local. A single final clock fence rejects
            // mixed-input records before any member state is committed. Camp
            // state/awake remains separate from the owning combat-stat record.
            try {
                for(std::size_t j=0;j<members.size();++j) if(seen(w.monsters[j].pos)) {
                    error_stage="observe-state";
                    member_frame[j].state=members[j].state();
                    if(!member_frame[j].state.is_alive()) member_frame[j].health_read=true;
                    else if(detail) {
                        error_stage="observe-health";
                        const auto observed=native_camp(members[j]);
                        member_frame[j].hp=observed.health();member_frame[j].damage=observed.strength();
                        member_frame[j].health_read=true;
                    }
                }
            } catch(const cambc::error& error) {
                if(cambc::round_number()!=round_
                    &&(error.code()==cambc::error_code::api_not_in_vision
                        ||error.code()==cambc::error_code::api_not_found)) return false;
                throw;
            }
            if(cambc::round_number()!=round_) return false;
            if(group_index==target_&&members.size()<=selected_rows.members.size()) {
                selected_rows.count=members.size();captured_group=group_index;
                for(std::size_t j=0;j<members.size();++j) {
                    auto& row=selected_rows.members[j];const auto& m=w.monsters[j];
                    row={m.key,m.pos,0,0,false,false};
                    if(!seen(m.pos)) continue;
                    row.visible=true;row.alive=member_frame[j].state.is_alive();
                    if(row.alive) {
                        if(!member_frame[j].health_read) return false;
                        row.hp=member_frame[j].hp;row.damage=member_frame[j].damage;
                    }
                }
            }
            for(std::size_t j=0;j<members.size();++j) {
                auto& m=w.monsters[j];
                if(!seen(m.pos)) {
                    w.all_known=false;w.state_known=false;
                    group_changed=group_changed||!m.alive||w.activity[j]!=3;
                    if(!update_member(w,j,true,m.alive?m.hp:w.maximum_hp[j],3)) return false;
                    w.masks_dirty=group_changed;
                    continue;
                }
                const auto state_m=member_frame[j].state;
                const bool alive=state_m.is_alive();
                const std::uint8_t activity=alive ? (state_m.is_asleep()?1:2) : 0;
                group_changed=group_changed||m.alive!=alive||w.activity[j]!=activity;
                const bool exact_health=member_frame[j].health_read;
                const float hp=exact_health ? member_frame[j].hp : m.alive ? m.hp : w.maximum_hp[j];
                if(!update_member(w,j,alive,hp,activity)) return false;
                if(exact_health) w.health_round[j]=round_;else w.all_known=false;
                w.masks_dirty=group_changed;
                any_fresh_alive=any_fresh_alive||m.alive;
            }
            if(any_fresh_alive) w.earliest_respawn=-1;
            else {
                // One fresh living member proves the group's active lifecycle;
                // do not re-import vision and re-test members for that same fact.
                // With no such witness the native group query is still needed.
                error_stage="observe-lifecycle";
                w.masks_dirty=group_changed;
                const auto state=w.group.state();
                if(cambc::round_number()!=round_) return false;
                w.reward=std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                w.respawning=std::holds_alternative<cambc::map::camp_group_respawning>(state);
                if(w.respawning) w.earliest_respawn=round_+std::get<cambc::map::camp_group_respawning>(state).timer;
                else if(w.reward) w.earliest_respawn=round_+1+w.group.max_respawn_timer();
                else w.earliest_respawn=-1;
                if(w.reward||w.respawning) {
                    w.all_known=true;w.state_known=true;
                    for(std::size_t j=0;j<w.monsters.size();++j) {
                        auto& m=w.monsters[j];group_changed=group_changed||m.alive||w.activity[j]!=0;
                        if(!update_member(w,j,false,0,0)) return false;
                        w.health_round[j]=round_;
                    }
                }
            }
            if(group_changed) w.masks_dirty=false;
            if(cambc::round_number()!=round_) return false;
        }
        if(camp_masks_dirty_) {camp_masks();camp_masks_dirty_=false;}
        if(cambc::round_number()!=round_) return false;
        error_stage="observe-blocked";
        blocked_=terrain_|camp_blocked_;danger_=camp_danger_;
        blocked_|=cambc::vision::camp_tiles()|cambc::vision::champion_tiles()|cambc::vision::teleport_tiles();
        if(cambc::round_number()!=round_) return false;
        blocked_.set(owner.position(),false);
        blocked_bytes_=blocked_.export_linear_buffer();
        occupancy_dirty_=false;
        error_stage="observe-enemies";visible_enemies_.clear();
        // Global handles grant no live fields. One checked record per handle
        // either proves personal visibility or returns the SDK's hidden error;
        // avoid enumerating positions and later re-binding a now-stale tile.
        for(const auto& e:enemy_handles_) {
            if(cambc::round_number()!=round_) return false;
            const auto enemy=native_visible_champion(e);
            if(cambc::round_number()!=round_) return false;
            if(!enemy||!enemy->is_alive()) continue;
            visible_enemies_.push_back(*enemy);
            danger_|=around(enemy->position(),std::max(4,enemy->attack_range()+1));
        }
        escape_ready_=false;
        observe_cost_=initial_fuel-cambc::fuel_remaining();
        if(cambc::round_number()!=round_) return false;
        if(captured_group>=0&&masks_.valid()&&!pending_member_)
            prepared_micro_.seal(micro_stamp(groups_[captured_group].group),selected_rows,
                cambc::map::width(),cambc::map::height(),std::move(visible_bytes));
        return cambc::round_number()==round_;
    }
    bool move(cambc::bitboard goals, bool avoid=false) {
        if(cambc::round_number()!=round_) return false;
        error_stage="move-prepare";
        auto& me=cambc::self();
        const auto origin=me.position();
        if(!me.can_move() || goals.is_set(origin)) return false;
        error_stage="move-occupancy";
        auto walls=blocked_;
        if(occupancy_dirty_) walls|=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        walls.set(origin,false);
        if(avoid) { walls|=danger_; walls.set(origin,false); }
        error_stage="move-goals";goals&=~walls;
        if(cambc::round_number()!=round_||goals.is_empty() || cambc::fuel_remaining()<18000) return false;
        cambc::distance_field f; cambc::bitboard visited;
        error_stage="move-field";
        f.compute_to(walls,goals,one(origin),visited);
        if(cambc::round_number()!=round_||!visited.is_set(origin)) return false;
        auto best=origin;auto value=f.get(best);
        for(auto p:walls.reachable_tiles(best,me.base_stats().base_move_speed).positions())
            if(const auto next=f.get(p);next<value) {value=next;best=p;}
        error_stage="move-step";
        const auto current=me.position();
        if(current!=origin||best==origin) return false;
        invalidate_micro_view();
        if(cambc::fuel_remaining()<7000||cambc::round_number()!=round_) return false;
        const bool applied=me.move_absolute(best);
        // A successful step changes the personal view; a hidden-obstruction
        // refusal can reveal a new blocker without spending movement. Either
        // invalidates same-input occupancy reuse before another attempt.
        occupancy_dirty_=true;
        return applied;
    }
    bool escape() {
        if(cambc::round_number()!=round_) return false;
        auto& me=cambc::self();
        const auto position=me.position();const bool exposed=danger_.is_set(position);
        if(cambc::round_number()!=round_) return false;
        if(!exposed) {
            invalidate_micro_view();
            if(cambc::fuel_remaining()<7000||cambc::round_number()!=round_) return false;
            me.recall();return true;
        }
        return move(~danger_&~blocked_);
    }
    float nearby_damage(vec2 p,int wake_group=-1) const {
        float damage=0;
        for(std::size_t i=0;i<groups_.size();++i) {const auto& w=groups_[i];
            for(std::size_t j=0;j<w.monsters.size();++j) {const auto& m=w.monsters[j];
                if(m.alive&&(int(i)==wake_group||w.activity[j]!=1)&&m.damage>0&&dist(p,m.pos)<=m.range) damage+=m.damage;
            }
        }
        return damage;
    }
    bool prepare_wake_geometry(int wake_group,bool& selected_harmful) const {
        selected_harmful=false;
        if(wake_group>=0) {
            const auto& work=groups_[wake_group];
            if(work.monsters.size()>32) {geometry_deferred_=true;return false;}
            std::uint32_t members=0;
            for(std::size_t j=0;j<work.monsters.size();++j)
                if(work.monsters[j].alive&&work.monsters[j].damage>0) members|=std::uint32_t(1)<<j;
            selected_harmful=members!=0;
            if(selected_harmful) {
                if(wake_mask_group_!=wake_group||(members&~wake_members_)!=0) {
                    const auto support=wake_mask_group_==wake_group ? wake_members_|members : members;
                    if(!wake_ledger_.reset(cambc::map::width(),cambc::map::height(),work.monsters.size()))
                        throw std::runtime_error("invalid wake mask ledger dimensions");
                    wake_mask_group_=wake_group;wake_members_=support;wake_member_cursor_=0;
                }
                error_stage="wake-mask-seed";
                while(wake_member_cursor_<work.monsters.size()) {
                    const auto& m=work.monsters[wake_member_cursor_];
                    const bool harmful=(wake_members_&(std::uint32_t(1)<<wake_member_cursor_))!=0;
                    if(!wake_ledger_.pending_update()
                        &&!wake_ledger_.begin_transition(wake_member_cursor_,m.pos.x,m.pos.y,m.range,false,false,harmful,harmful))
                        throw std::runtime_error("invalid wake mask ledger seed");
                    if(!advance_ledger(wake_ledger_)) {geometry_deferred_=true;wake_service_job_=wake_group;return false;}
                    ++wake_member_cursor_;
                    if(cambc::round_number()!=round_) {geometry_deferred_=true;wake_service_job_=wake_group;return false;}
                }
                if(!wake_ledger_.valid()) throw std::runtime_error("incomplete wake mask ledger publication");
                // This is reusable immutable geometric support. Previously
                // living members may remain in this conservative superset;
                // fresh canonical life alone supplies damage/HP authority.
                if(wake_member_cursor_==work.monsters.size()) {
                    wake_mask_=packed_board(wake_ledger_.threat_bytes());++wake_member_cursor_;
                }
            }
        }
        if(wake_service_job_==wake_group) wake_service_job_=-1;
        return true;
    }
    int escape_steps(vec2 p,float* prefix_damage=nullptr,int wake_group=-1,bool nomination=false) const {
        error_stage=nomination ? "nomination-escape-field" : "escape-field";
        geometry_deferred_=false;
        if(prefix_damage) *prefix_damage=-1;
        bool selected_harmful=false;
        if(wake_group>=0&&!prepare_wake_geometry(wake_group,selected_harmful)) return -1;
        const auto exposure=selected_harmful ? danger_|wake_mask_ : danger_;
        if(!exposure.is_set(p)) {if(prefix_damage)*prefix_damage=0;return 0;}
        if(escape_group_!=wake_group) {escape_ready_=false;escape_group_=wake_group;}
        if(!escape_ready_) {
            if(cambc::fuel_remaining()<22000) {geometry_deferred_=true;return -1;}
            const auto goals=~exposure&~blocked_;
            if(goals.is_empty()) return -1;
            error_stage=nomination ? "nomination-escape-compute" : "escape-compute";
            escape_field_.compute(blocked_,goals,escape_reached_,12);
            if(cambc::round_number()!=round_) return -1;
            escape_ready_=true;
        }
        error_stage=nomination ? "nomination-escape-lookup" : "escape-lookup";
        const auto value=in_map(p) ? escape_field_.get(p) : cambc::distance_field::unreachable;
        int steps=value!=cambc::distance_field::unreachable ? int(value) : 10000;
        if(steps==10000) for(int y=p.y-1;y<=p.y+1;++y) for(int x=p.x-1;x<=p.x+1;++x) {
            const vec2 q{x,y};
            if(in_map(q)&&!local_blocked(q)) {
                const auto next=escape_field_.get(q);
                if(next!=cambc::distance_field::unreachable) steps=std::min(steps,1+int(next));
            }
        }
        if(steps>=10000) return -1;
        if(prefix_damage) {
            // Every prefix tile of a shortest steps-long walk lies within steps
            // of p. Charge every potentially reaching camp for every step, plus
            // the current exposure and one revealed-blocker allowance. This is
            // a conservative camp-only envelope, not an enemy-interference proof.
            float possible_volley=0;
            for(std::size_t i=0;i<groups_.size();++i) {const auto& w=groups_[i];
                for(std::size_t j=0;j<w.monsters.size();++j) {const auto& m=w.monsters[j];
                    if(m.alive&&(int(i)==wake_group||w.activity[j]!=1)&&m.damage>0&&dist(p,m.pos)<=m.range+steps)
                        possible_volley+=m.damage;
                }
            }
            *prefix_damage=possible_volley*float(steps+2);
        }
        return steps;
    }
    std::vector<vec2> station_positions(const GroupWork& w,int range,bool members=false) const {
        std::vector<std::uint8_t> bytes(cambc::bitboard::linear_buffer_size(),0);
        std::vector<vec2> positions;
        const auto add=[&](vec2 p){
            if(local_blocked(p)) return;
            auto& byte=bytes[index(p)>>3];const auto bit=std::uint8_t(1u<<(index(p)&7));
            if(!(byte&bit)) {byte|=bit;positions.push_back(p);}
        };
        if(w.observed&&w.reward) {
            // Clearing and capturing are distinct jobs. The current native
            // lifecycle state, rather than a remembered death, permits these
            // spawn landing goals even when somebody else did the damage.
            for(const auto& m:w.monsters) add(m.pos);
            return positions;
        }
        const Monster* first=nullptr;
        for(const auto& m:w.monsters) if(m.alive&&m.damage>0) {first=&m;break;}
        if(members||!first) {
            for(const auto& m:w.monsters) if(m.alive)
                for(int y=m.pos.y-range;y<=m.pos.y+range;++y) for(int x=m.pos.x-range;x<=m.pos.x+range;++x) add({x,y});
            return positions;
        }
        for(int y=first->pos.y-range;y<=first->pos.y+range;++y)
            for(int x=first->pos.x-range;x<=first->pos.x+range;++x) {
                const vec2 p{x,y}; if(local_blocked(p)) continue;bool hits=true;
                for(const auto& m:w.monsters) if(m.alive&&m.damage>0&&dist(p,m.pos)>range) {hits=false;break;}
                if(hits) add(p);
            }
        return positions;
    }
    cambc::bitboard stations(const GroupWork& w,int range) const {
        std::vector<std::uint8_t> bytes(cambc::bitboard::linear_buffer_size(),0);
        for(auto p:station_positions(w,range)) packed_set(bytes,p);
        return packed_board(bytes);
    }
    cambc::bitboard member_stations(const GroupWork& w,int range) const {
        std::vector<std::uint8_t> bytes(cambc::bitboard::linear_buffer_size(),0);
        for(const auto& m:w.monsters) if(m.alive)
            for(int y=m.pos.y-range;y<=m.pos.y+range;++y) for(int x=m.pos.x-range;x<=m.pos.x+range;++x) {
                const vec2 p{x,y};if(!local_blocked(p)) packed_set(bytes,p);
            }
        return packed_board(bytes);
    }
    int nearest(cambc::bitboard goals,vec2 p) const {
        int best=10000;
        for(auto q:goals.positions()) best=std::min(best,dist(p,q));
        return best;
    }
    int altar_walk(vec2 p) const {
        int best=10000;
        for(auto a:altars_) best=std::min(best,std::max(0,dist(p,a)-1));
        return best;
    }
    bool shop() {
        error_stage="funding-shop";
        auto& me=cambc::self();
        const auto counts=me.item_count();
        cambc::item_type item=cambc::item_type::jungle_shard_of_strength;
        if(counts[cambc::item_type::jungle_emblem_of_strength]) item=cambc::item_type::monster_shard_of_strength;
        if(counts[cambc::item_type::monster_emblem_of_strength]) item=cambc::item_type::power_shard_of_strength;
        const unsigned remaining=5-std::min<unsigned>(4,counts[item]);
        const auto n=std::min<unsigned>(remaining,me.gold_count()/item.cost());
        if(n && me.can_buy(item,n)) {
            invalidate_micro_view();
            if(cambc::fuel_remaining()<7000||cambc::round_number()!=round_) return false;
            me.buy(item,n);return true;
        }
        return false;
    }
    bool funded_shop_access() const {
        auto& me=cambc::self(); const auto counts=me.item_count();
        auto item=cambc::item_type::jungle_shard_of_strength;
        if(counts[cambc::item_type::jungle_emblem_of_strength]) item=cambc::item_type::monster_shard_of_strength;
        if(counts[cambc::item_type::monster_emblem_of_strength]) item=cambc::item_type::power_shard_of_strength;
        const unsigned remaining=5-std::min<unsigned>(4,counts[item]);
        return me.gold_count()>=remaining*item.cost()&&!me.can_buy(item,remaining);
    }
    PlannerContext funding(const cambc::champion& c) const {
        const auto count=c.item_count();
        if(cambc::round_number()!=round_) return {};
        if(count[cambc::item_type::jungle_emblem_of_strength]) return {};
        const auto shards=count[cambc::item_type::jungle_shard_of_strength];
        // Peer gold is private. Inventory proves bought shards; an unknown wallet
        // never supplies a funding certificate or becomes observed zero gold.
        if(native(c)!=native(cambc::self())) return {true,double(5-shards)*30.0}; // upper obligation, wallet UNKNOWN.
        const double deficit=std::max(0.0,double(5-shards)*30.0-double(c.gold_count()));
        return {deficit>0,deficit};
    }
    Body body(const cambc::champion& c,int wake_group=-1) const {
        const auto frame=native_champion(c);
        if(cambc::round_number()!=round_) return {};
        const bool melee=c.base_stats().base_attack_range==1;
        Body b{native(c),frame.position(),frame.health(),frame.max_health(),frame.strength(),frame.can_attack(),melee};
        b.range=frame.attack_range();
        b.acted_this_round=frame.state().contains(cambc::champion_state::completed_turn);
        if(cambc::round_number()!=round_) {b.hp=0;return b;}
        escape_steps(b.pos,&b.escape_damage,wake_group);
        return b;
    }
    QuoteBody quote_body(const cambc::champion& c) const {
        const auto frame=native_champion(c);
        if(cambc::round_number()!=round_) return {};
        return {native(c),frame.attack_range(),frame.position(),frame.health(),frame.max_health(),frame.strength(),c.gem_count()};
    }
    Episode solo(int job,const QuoteBody& c,vec2 station,int approach) const {
        const auto& w=groups_[job];
        Episode e; e.job=job; e.actor=c.id; e.gems=w.group.gem_value(); e.gold=w.group.gold_value();
        e.approach=approach; e.capture=cambc::constants::capture_time+1;
        e.bank=e.gems>0 ? altar_walk(station) : 0;
        e.channel=e.gems>0 ? std::max(1,int(std::ceil((c.gems+e.gems)/10.0))) : 0;
        if(!w.observed||!w.all_known||w.respawning||approach>=10000||e.bank>=10000) return e;
        float volley=0; int harmful=0,hits=0;const float strength=std::max(1.0f,c.strength);
        const float health=c.health,maximum=c.maximum;
        for(const auto& m:w.monsters) if(m.alive) {
            if(cambc::fuel_remaining()<18000) return e;
            const int count=micro_hits(m.hp,strength);
            if(count<0||hits>16000-count) return e;
            hits+=count;volley+=m.damage;harmful+=m.damage>0;
        }
        float path_damage=-1; const int steps=escape_steps(station,&path_damage,job,true);
        if(steps<0) return e;
        volley=std::max(volley,nearby_damage(station,job));
        const float reserve=std::max(path_damage,volley*float(steps+1));
        const float available=health-reserve;
        const int first=volley>0 ? int(std::floor(available/volley))-1 : hits;
        const int later=volley>0 ? int(std::floor((maximum-reserve)/volley))-1 : hits;
        if(hits>0 && (first<1||later<1)) return e;
        const int trips=hits<=std::max(0,first) ? 1 : 1+(hits-first+later-1)/later;
        // This is an explicit conservative partial-siege estimate, not a native safety proof.
        // Runtime escape and actual target/member visibility preempt every future action.
        int return_walk=10000;for(auto p:spawn_tiles_) return_walk=std::min(return_walk,dist(p,station));
        e.work=hits+std::max(0,harmful-1)*2+(trips-1)*(steps+cambc::constants::recall_time+1+return_walk);
        e.recovery=cambc::constants::recall_time+1;
        if(e.gems==0) e.recovery+=steps;
        e.endhp=std::max(1.0f,health-volley*float(std::min(hits,std::max(0,first))));
        e.knownsafe=true;
        return e;
    }
    int choose_job(const NativeChampionObservation& owner,unsigned gems,bool proposal_only=false) {
        auto& me=cambc::self();
        if(!proposal_only&&cambc::fuel_remaining()<45000) {
            proposal_requested_=true;search_=Search::deferred;return -1;
        }
        std::uint64_t phase_fuel=0;
        if constexpr(config::trace) {phase_fuel=cambc::fuel_remaining();route_cost_=pads_cost_=episode_cost_=escape_cost_=0;examined_=0;}
        error_stage="job-routes";
        const QuoteBody self_quote{native(me),owner.attack_range(),owner.position(),owner.health(),owner.max_health(),owner.strength(),gems};
        const auto context=funding(me);
        cambc::distance_field routes; cambc::bitboard reached;
        auto route_walls=blocked_|cambc::vision::camp_tiles()|cambc::vision::champion_tiles();
        route_walls.set(self_quote.position,false);
        routes.compute(route_walls,one(self_quote.position),reached,64);
        if(cambc::round_number()!=round_) {search_=Search::deferred;return -1;}
        if constexpr(config::trace) {route_cost_=phase_fuel-cambc::fuel_remaining();}
        struct Near {int index,d;}; std::vector<Near> near;
        for(std::size_t i=0;i<groups_.size();++i) {
            const auto& w=groups_[i];
            if((w.observed&&w.respawning)||int(i)==excluded_) continue;
            int d=10000; for(const auto& m:w.monsters) d=std::min(d,dist(self_quote.position,m.pos));
            near.push_back({int(i),d});
        }
        std::stable_sort(near.begin(),near.end(),[](auto a,auto b){return a.d<b.d;});
        struct Nomination {int job;vec2 station;int approach;Choice rank;};
        std::vector<Nomination> options; int explore=-1; double explore_cost=1e30;
        int explore_member=-1,explore_direction=-1;vec2 explore_landing{};
        std::size_t start=near.empty()?0:std::size_t(scan_cursor_)%near.size();
        if(preferred_certificate_>=0) {
            bool found=false;
            for(std::size_t j=0;j<near.size();++j) if(near[j].index==preferred_certificate_
                &&groups_[near[j].index].all_known) {start=j;found=true;break;}
            if(!found) preferred_certificate_=-1;
        }
        std::size_t examined=0;
        for(;examined<near.size()&&examined<10;) {
            if(cambc::fuel_remaining()<28000) break;
            const int i=near[(start+examined)%near.size()].index; ++examined;
            const auto& w=groups_[i];
            if(!w.state_known) {
                error_stage="observation-frontier";
                std::vector<FrontierMember> members;members.reserve(w.monsters.size());
                for(std::size_t j=0;j<w.monsters.size();++j) members.push_back({w.monsters[j].pos.x,w.monsters[j].pos.y,
                    w.member_visible[j],w.observation_attempted[j]});
                const auto frontier=select_frontier_with_distance(cambc::map::width(),cambc::map::height(),self_quote.position.x,
                    self_quote.position.y,members,blocked_bytes_,[&](int x,int y){return routes.get({x,y});});
                if(!frontier.valid) continue; // This represented frontier is empty; the job is still unknown.
                double hp=0,damage=0;for(const auto& m:w.monsters) {hp+=m.hp;damage+=m.damage;}
                const double cost=frontier.distance+hp/std::max(1.0f,self_quote.strength)+damage;
                if(cost<explore_cost) {
                    explore=i;explore_cost=cost;explore_member=frontier.memberIndex;
                    explore_direction=frontier.direction;explore_landing={frontier.landingx,frontier.landingy};
                }
                continue;
            }
            if constexpr(config::trace) phase_fuel=cambc::fuel_remaining();
            error_stage="job-stations";
            auto pads=station_positions(w,self_quote.range);
            if(pads.empty()) pads=station_positions(w,self_quote.range,true);
            if(pads.empty()) continue;
            vec2 station{}; int approach=10000;
            for(auto p:pads) {
                if(cambc::fuel_remaining()<18000) break; // A bounded represented pad prefix, not an exhaustive refutation.
                const auto value=routes.get(p);
                if(value!=cambc::distance_field::unreachable
                    &&(value<approach||(value==approach&&index(p)<index(station)))) {station=p;approach=value;}
            }
            if constexpr(config::trace) {pads_cost_+=phase_fuel-cambc::fuel_remaining();phase_fuel=cambc::fuel_remaining();}
            if(approach>=10000) continue; // Unknown beyond the bounded route search, not universal infeasibility.
            // This rough work-only rank nominates a candidate; it is neither
            // an admitted episode nor an incoming-damage/f32-hit certificate.
            // Exactly one nominated job receives the expensive full quote.
            int work=0,harmful=0;
            for(std::size_t j=0;j<w.monsters.size();++j) if(const auto& m=w.monsters[j];m.alive) {
                const auto hp=w.health_round[j]==round_ ? m.hp : w.maximum_hp[j];
                work+=std::max(1,int(std::ceil(double(hp)/std::max(1.0f,self_quote.strength))));
                harmful+=m.damage>0;
            }
            const int bank=w.group.gem_value()>0 ? altar_walk(station) : 0;
            const int channel=w.group.gem_value()>0 ? std::max(1,int(std::ceil((gems+w.group.gem_value())/10.0))) : 0;
            Choice rank;rank.valid=true;rank.episode.job=i;rank.estimate=true;
            rank.duration=approach+work+std::max(0,harmful-1)*2+cambc::constants::capture_time+1+bank+channel+cambc::constants::recall_time+1;
            rank.funding=funding_stage(context);
            rank.funding_complete=rank.funding&&w.group.gold_value()>=context.fundinggold;
            rank.value=rank.funding ? std::min(double(w.group.gold_value()),context.fundinggold) : double(w.group.gem_value());
            int others=0;
            for(std::size_t a=0;a<allies_.size()&&a<4;++a) if(int(a)!=slot_&&intent(int(a))==i&&allies_[a].is_alive()) ++others;
            rank.duration+=8*others; // contention is displaced time, not extra DPS.
            rank.rate=rank.value/std::max(1,rank.duration);
            options.push_back({i,station,approach,rank});
            if(i==preferred_certificate_) break;
        }
        if(!near.empty()) scan_cursor_=int((start+examined)%near.size());
        if constexpr(config::trace) examined_=int(examined);
        const Nomination* candidate=nullptr;
        for(const auto& option:options)
            if(!candidate||option.job==preferred_certificate_||better_choice(option.rank,candidate->rank)) candidate=&option;
        bool chosen=false;int chosen_job=-1;
        if(candidate) {
            if(proposal_only||!groups_[candidate->job].all_known) {
                preferred_certificate_=candidate->job;proposal_requested_=false;
                search_=Search::need_observation;frontier_member_=frontier_direction_=-1;
                return candidate->job;
            }
            error_stage="job-one-certificate";
            const auto quote=solo(candidate->job,self_quote,candidate->station,candidate->approach);
            if(cambc::round_number()!=round_) {search_=Search::deferred;return -1;}
            if(geometry_deferred_) {
                preferred_certificate_=candidate->job;search_=Search::deferred;return -1;
            }
            preferred_certificate_=-1;
            chosen=quote.valid();chosen_job=chosen ? quote.job : -1;
        }
        search_=chosen ? Search::chosen : explore>=0 ? Search::need_observation
            : near.empty() ? Search::empty : examined<near.size() ? Search::deferred : Search::no_current_command;
        excluded_=-1;
        frontier_member_=chosen ? -1 : explore_member;frontier_direction_=chosen ? -1 : explore_direction;
        if(!chosen&&explore>=0) frontier_landing_=explore_landing;
        return chosen ? chosen_job : explore;
    }
    bool quiet_for_proposal(vec2 position) const {
        // This is a fresh local gate for an action-free planning input, not an
        // adversarial PvP or future-route survival certificate.
        error_stage="proposal-local-observation";
        for(const auto& w:groups_) for(std::size_t j=0;j<w.monsters.size();++j) {
            const auto& m=w.monsters[j];if(m.damage<=0||dist(position,m.pos)>m.range) continue;
            // An action-free input is still a physical wait. Fresh sleep does
            // not exclude another actor waking this group before camp phase;
            // dated death does not exclude an unseen respawn.
            return false;
        }
        for(const auto& handle:enemy_handles_) {
            const auto enemy=native_visible_champion(handle);
            if(cambc::round_number()!=round_) return false;
            if(enemy&&enemy->is_alive()&&dist(position,enemy->position())<=std::max(4,enemy->attack_range()+1)) return false;
        }
        return cambc::round_number()==round_;
    }
    bool quiet_for_geometry(vec2 position) const {
        // Preparation must not add a physical wait in a possible camp volley.
        // Include sleeping and dated-dead members: another actor can wake a
        // sleeping group, and an unseen respawn invalidates a dead-only gate.
        for(const auto& work:groups_) for(const auto& member:work.monsters)
            if(member.damage>0&&dist(position,member.pos)<=member.range) return false;
        return quiet_for_proposal(position);
    }
    bool quiet_staging_step(const NativeChampionObservation& owner) {
        // This is a one-step camp-only service, not a general route or PvP
        // certificate. It runs before expensive observation/planning and never
        // credits a blocked move as physical progress.
        if(!owner.can_move()||owner.move_speed()<1||staging_supports_.size()>staging::max_supports) return false;
        const auto origin=owner.position();bool exposed=false;
        for(const auto& support:staging_supports_)
            if(staging::covers(support,{origin.x,origin.y})) {exposed=true;break;}
        if(!exposed) return false;
        error_stage="quiet-staging-enemies";
        for(const auto& handle:enemy_handles_) {
            const auto enemy=native_visible_champion(handle);
            if(cambc::round_number()!=round_) return false;
            // Hidden-response safety is not established by this local gate.
            if(enemy&&enemy->is_alive()) return false;
        }
        error_stage="quiet-staging-occupancy";
        auto walls=terrain_|cambc::vision::camp_tiles()|cambc::vision::champion_tiles()|cambc::vision::teleport_tiles();
        walls.set(origin,false);
        auto witness=walls.export_linear_buffer();
        if(cambc::round_number()!=round_) return false;
        if(staging_origin_!=origin||staging_wall_witness_!=witness) {
            staging_origin_=origin;staging_failures_=0;staging_wall_witness_=std::move(witness);
        }
        const auto reachable=walls.reachable_tiles(origin,1);
        std::array<staging::Landing,8> landings{};std::size_t count=0;
        const auto direction=[&](vec2 p) {
            const int raw=(p.y-origin.y+1)*3+(p.x-origin.x+1);
            return raw<4 ? raw : raw-1;
        };
        for(auto p:reachable.positions()) if(p!=origin) {
            if(dist(p,origin)!=1||count==landings.size()) return false;
            const int bit=direction(p);
            if(bit<0||bit>=8) return false;
            if(!(staging_failures_&(1u<<bit))) landings[count++]={p.x,p.y};
        }
        if(cambc::round_number()!=round_) return false;
        error_stage="quiet-staging-quote";
        const auto quote=staging::quote(origin.x,origin.y,owner.health(),staging_supports_,
            std::span<const staging::Landing>{landings.data(),count});
        staging_result_=quote.kind;
        if(quote.kind!=staging::Kind::Chosen) return false;
        auto& me=cambc::self();const auto fresh=native_champion(me);
        if(fresh.position()!=origin||!fresh.can_move()||fresh.move_speed()<1
            ||double(fresh.health())<=quote.origin_raw) return false;
        publish(-1);invalidate_micro_view();
        error_stage="quiet-staging-move";
        if(cambc::fuel_remaining()<7000||cambc::round_number()!=round_) return false;
        const vec2 destination{quote.landing.x,quote.landing.y};
        const bool moved=me.move_absolute(destination);
        occupancy_dirty_=true;
        if(!moved) staging_failures_|=std::uint8_t(1u<<direction(destination));
        // Either branch completes this input. The next input must freshly
        // establish a quiet planning state; failed candidates only lose their
        // place in this exact observed-wall/origin menu, never become infeasible.
        return true;
    }
    void request_wake_preparation(int group) {
        if(group<0||group>=int(groups_.size())) return;
        for(const auto& member:groups_[group].monsters) if(member.alive&&member.damage>0) {
            wake_service_job_=group;return;
        }
    }
    bool bank() {
        error_stage="bank";
        auto& me=cambc::self(); cambc::bitboard goals; goals.clear();
        for(auto a:altars_) {
            if(dist(me.position(),a)<=cambc::constants::submit_gems_range) {
                publish(-1);
                invalidate_micro_view();
                if(cambc::fuel_remaining()<7000||cambc::round_number()!=round_) return false;
                me.submit_gems(a); trace("tribute",target_); return true;
            }
            goals|=around(a,cambc::constants::submit_gems_range);
        }
        return move(goals,true)||move(goals);
    }
public:
    void play_turn() {
        ++input_generation_;prepared_micro_.reset();
        ledger_steps_left_=8;
        round_=cambc::round_number();
        if(!initialized_) {init();return;}
        if(pending_member_) {
            error_stage="camp-mask-resume";finish_member_update();return;
        }
        error_stage="intent-read";
        if(allies_.size()<=5&&cambc::shared_memory_buffer::size()>=8) cambc::shared_memory.read(0,std::span<std::uint8_t>{mail_});
        auto& me=cambc::self();
        if(!me.is_alive()) {publish(-1); shop(); return;}
        const auto initial_owner=native_champion(me);
        if(cambc::round_number()!=round_) return;
        const auto initial_state=initial_owner.state();
        if(initial_state.contains(cambc::champion_state::recalling)
            ||initial_state.contains(cambc::champion_state::submitting_gems)
            ||initial_state.contains(cambc::champion_state::teleporting)) {publish(-1);return;}
        if(!initial_owner.can_move()&&!initial_owner.can_attack()&&!initial_owner.can_cast()) {publish(-1);return;}
        const bool bought=shop();
        // Shopping can change all combat stats. This owning record is valid only
        // through the following observation/planning phase, before our actions.
        const auto owner=bought ? native_champion(me) : initial_owner;const auto position=owner.position();
        const unsigned gems=me.gem_count();
        if(cambc::round_number()!=round_) return;
        if((proposal_requested_||wake_service_job_>=0)&&gems==0&&quiet_staging_step(owner)) return;
        if(cambc::round_number()!=round_) return;
        if(wake_service_job_>=0) {
            const int job=wake_service_job_;
            if(job!=target_&&job!=preferred_certificate_) wake_service_job_=-1;
            else if(gems==0&&quiet_for_geometry(position)) {
                bool harmful=false;geometry_deferred_=false;prepare_wake_geometry(job,harmful);
                // Finishing geometry completes an input phase. The next input
                // must observe the actual world again before any command.
                if(cambc::round_number()==round_) publish(target_);
                return;
            }
        }
        if(cambc::round_number()!=round_) return;
        if(proposal_requested_&&gems==0&&quiet_for_proposal(position)) {
            target_=choose_job(owner,gems,true);admitted_=false;
            request_wake_preparation(target_);
            proposal_requested_=target_<0;
            if(cambc::round_number()==round_) publish(target_);
            return;
        }
        if(cambc::round_number()!=round_) return;
        const bool spawn=cambc::map::spawn_region(cambc::team::ally).is_set(position);
        if(spawn) { recovering_=false; if(owner.health()<owner.max_health()) {publish(-1);return;} }
        if(!observe(owner)) return;
        if constexpr(config::phase_audit) if((round_==50||round_==90)&&cambc::fuel_remaining()>15000) {
            const auto p=position;int asleep=0,awake=0,unknown=0;
            for(const auto& w:groups_) for(std::size_t j=0;j<w.monsters.size();++j)
                if(w.monsters[j].alive&&w.monsters[j].damage>0&&dist(p,w.monsters[j].pos)<=w.monsters[j].range) {
                    asleep+=w.activity[j]==1;awake+=w.activity[j]==2;unknown+=w.activity[j]==3;
                }
            std::cout<<"wb state r"<<round_<<" s"<<slot_<<" p"<<p.x<<","<<p.y<<" hp"<<owner.health()
                <<" target"<<target_<<" frontier"<<frontier_member_<<" recover"<<recovering_
                <<" threats"<<asleep<<","<<awake<<","<<unknown<<" fuel"<<cambc::fuel_remaining()<<'\n';
        }
        const bool holding_capture=target_>=0&&groups_[target_].observed&&groups_[target_].reward
            &&groups_[target_].tiles.is_set(position);
        if(!spawn&&gems==0&&!holding_capture&&funded_shop_access()) recovering_=true;
        const int out=escape_steps(position);
        if(cambc::round_number()!=round_) return;
        const float exposure=nearby_damage(position);
        if(exposure>0&&(out<0||owner.health()<=exposure*float(out+2))) recovering_=true;
        for(const auto& enemy:visible_enemies_) {
            if(dist(enemy.position(),position)<=4) recovering_=true;
        }
        if(recovering_) {
            publish(-1);if(gems>0&&!danger_.is_set(position)&&bank()) return;
            const auto origin=position;escape();
            // Finish the required exit before considering a basic. A ranged
            // basic before this move can consume the very allowance needed to
            // survive. Fresh readiness after an actual move permits only legal
            // remaining work; blocked movement or recall supplies no such exit.
            if(cambc::round_number()!=round_) return;
            const auto exited=native_champion(me);
            if(cambc::round_number()!=round_) return;
            if(exited.position()!=origin&&exited.can_attack())
                for(const auto& e:enemy_handles_) {
                    const auto enemy=native_visible_champion(e);
                    if(cambc::round_number()!=round_) return;
                    if(!enemy||!enemy->is_alive()
                        ||dist(exited.position(),enemy->position())>exited.attack_range()) continue;
                    const bool in_range=me.attack_tiles().is_set(enemy->position());
                    if(cambc::round_number()!=round_) return;
                    if(in_range) {
                        invalidate_micro_view();
                        if(cambc::fuel_remaining()>=7000&&cambc::round_number()==round_) me.attack(e);
                        break;
                    }
                }
            return;
        }
        if(gems>0) {publish(-1);bank();return;}
        if(target_<0||target_>=int(groups_.size())||(groups_[target_].observed&&groups_[target_].respawning)) {
            target_=choose_job(owner,gems); admitted_=target_>=0&&groups_[target_].observed&&groups_[target_].all_known;
            request_wake_preparation(target_);
            trace("select",target_);
            // Selection completes an input phase. The selected job persists,
            // and actuation receives the next input's fresh observations and
            // budget instead of inheriting a variable planning remainder.
            if(cambc::round_number()==round_) publish(target_);
            return;
        }
        if(target_<0) {publish(-1); if(danger_.is_set(position)) escape();return;}
        auto& job=groups_[target_];
        if(!admitted_&&job.observed&&job.all_known&&!job.reward&&cambc::fuel_remaining()>50000) {
            const int selected=choose_job(owner,gems);
            if(selected!=target_) {target_=selected;admitted_=false;request_wake_preparation(target_);publish(target_);return;}
            admitted_=true;
            return;
        }
        publish(target_);
        if(job.observed&&job.reward) {
            // Ordinary workers retain their actual shares. No unpriced universal
            // lowest-slot collector may confiscate their next funded capacity.
            if(job.tiles.is_set(position)) {trace("capture",target_);return;}
            move(job.tiles);return;
        }
        if(frontier_member_>=0) {
            const auto member=std::size_t(frontier_member_);
            if(member>=job.monsters.size()||job.member_visible[member]) {
                // This particular observation objective was actually resolved
                // by the new personal frame; a remaining sibling is a new job.
                trace("frontier-observed",target_);target_=-1;frontier_member_=-1;publish(-1);return;
            }
            if(position==frontier_landing_) {
                // An arrived landing that did not reveal the specified member
                // is a failed observation attempt, not a full-job refutation.
                job.observation_attempted[member]|=std::uint8_t(1u<<frontier_direction_);
                target_=-1;frontier_member_=-1;publish(-1);return;
            }
            float escape_prefix=-1;const int escape_length=escape_steps(position,&escape_prefix,target_);
            if(cambc::round_number()!=round_) return;
            if(geometry_deferred_) {search_=Search::deferred;return;}
            // All static group members, including unseen siblings, contribute
            // possible awake threat to this camp-only prefix bound. A fresh
            // observed subset may make primitive work without inventing its
            // missing siblings or crediting the group's eventual reward.
            if(job.observed&&escape_length>=0&&escape_prefix>=0
                &&owner.health()>escape_prefix+nearby_damage(position,target_)&&cambc::fuel_remaining()>30000) {
                error_stage="frontier-subset-micro";
                bool attempted=false;const bool acted=strike_current(job.group,&attempted);
                occupancy_dirty_=occupancy_dirty_||attempted;
                if(acted) return;
                if(cambc::round_number()!=round_) return;
            }
            move(one(frontier_landing_));return;
        }
        auto pads=stations(job,owner.attack_range());
        if(pads.is_empty()) pads=member_stations(job,owner.attack_range());
        if(!pads.is_set(position)) {
            const bool could_move=owner.can_move()&&cambc::fuel_remaining()>28000;
            if(!move(pads)&&could_move&&cambc::fuel_remaining()>18000) {
                // A revealed blocker is refreshed by the second attempt; failure
                // releases this current goal so another complete job can be tried.
                if(!move(pads)) {excluded_=target_;target_=-1;admitted_=false;publish(-1);}
            }
            return;
        }
        float commanded_escape=-1;
        const int commanded_steps=escape_steps(position,&commanded_escape,target_);
        if(cambc::round_number()!=round_) return;
        if(geometry_deferred_) {search_=Search::deferred;return;}
        if(commanded_steps<0||commanded_escape<0
            ||owner.health()<=commanded_escape+nearby_damage(position,target_)) {
            // Freshly sleeping groups are not current retaliation. The command
            // about to damage one nevertheless wakes its entire group, so this
            // first-command prefix must be rechecked at the actual actor state.
            // This proposed wake lacks the model's sufficient prefix. It does
            // not establish that the current quiet position requires egress.
            // Current actual exposure was handled before candidate selection.
            search_=Search::no_safe_prefix;excluded_=target_;target_=-1;admitted_=false;publish(-1);return;
        }
        bool party=false;
        if constexpr(config::cooperation) if(slot_<4&&target_<31&&job.all_known&&cambc::fuel_remaining()>50000) {
            for(std::size_t a=0;a<allies_.size()&&a<4;++a) if(int(a)!=slot_&&intent(int(a))==target_&&allies_[a].is_alive()&&allies_[a].is_visible()) {
                auto peer=allies_[a];
                if(peer.is_recalling()||peer.is_submitting_gems()||peer.is_teleporting()) continue;
                if(peer.restrictions().contains(cambc::buff_restrictions::attack)) continue;
                bool third=false;
                for(std::size_t other=0;other<allies_.size();++other) if(int(other)!=slot_&&other!=a
                    &&allies_[other].is_alive()&&allies_[other].is_visible()) {
                    const auto p=allies_[other].position();
                    if(intent(int(other))==target_||job.tiles.is_set(p)) third=true;
                    for(const auto& m:job.monsters) if(m.alive&&m.damage>0&&dist(p,m.pos)<=1) third=true;
                }
                if(third) continue;
                const auto own_body=body(me,target_);
                if(cambc::round_number()!=round_) return;
                const auto peer_body=body(peer,target_);
                if(cambc::round_number()!=round_) return;
                std::array<Body,2> actors{own_body,peer_body};
                error_stage="party-certificate";
                const auto cert=certify(actors,job.monsters);
                if(cambc::round_number()!=round_) return;
                if(!cert.feasible) continue;
                error_stage="party-alternatives";
                const auto self_quote=quote_body(me);
                if(cambc::round_number()!=round_) return;
                const auto peer_quote=quote_body(peer);
                if(cambc::round_number()!=round_) return;
                const auto self_funding=funding(me);
                if(cambc::round_number()!=round_) return;
                const auto peer_funding=funding(peer);
                if(cambc::round_number()!=round_) return;
                const auto solo_owner=solo(target_,self_quote,self_quote.position,0);
                if(cambc::round_number()!=round_) return;
                // A different actual personally observed fallback must exist for the helper.
                std::vector<Episode> alternatives;
                for(std::size_t j=0;j<groups_.size();++j) if(int(j)!=target_&&groups_[j].observed&&groups_[j].all_known&&!groups_[j].respawning) {
                    if(cambc::fuel_remaining()<28000) break;
                    const auto candidates=station_positions(groups_[j],peer_quote.range);
                    if(candidates.empty()) continue;
                    vec2 pad=candidates[0];int route=dist(pad,peer_quote.position);
                    for(auto p:candidates) if(dist(p,peer_quote.position)<route
                        ||(dist(p,peer_quote.position)==route&&index(p)<index(pad))) {pad=p;route=dist(p,peer_quote.position);}
                    auto fallback=solo(int(j),peer_quote,pad,route);
                    if(cambc::round_number()!=round_) return;
                    if(fallback.valid()) alternatives.push_back(fallback);
                }
                const auto alternative=choose(alternatives,native(peer),std::max(1,2000-round_),peer_funding);
                const Episode helper_alt=alternative.valid ? alternative.episode : Episode{};
                Episode pair=solo_owner; pair.helper=native(peer);pair.knownsafe=true;
                int cleanup=0;
                for(const auto& m:job.monsters) if(m.alive&&m.damage<=0)
                    cleanup+=micro_hits(m.hp,std::min(owner.strength(),peer_quote.strength));
                pair.work=cert.turns+cleanup+int(job.monsters.size())*2;
                pair.endhp=cert.hp0;pair.helper_endhp=cert.hp1;
                const auto gems=split_reward(job.group.gem_value(),native(me),native(peer));
                const auto gold=split_reward(job.group.gold_value(),native(me),native(peer));
                pair.gems=gems.owner;pair.gold=gold.owner;
                pair.channel=pair.gems>0 ? std::max(1,int(std::ceil((self_quote.gems+pair.gems)/10.0))) : 0;
                Episode receipt=solo(target_,peer_quote,peer_quote.position,0);
                if(cambc::round_number()!=round_) return;
                receipt.helper=-1;receipt.knownsafe=true;receipt.work=pair.work;
                receipt.gems=gems.helper;receipt.gold=gold.helper;receipt.endhp=cert.hp1;
                receipt.channel=receipt.gems>0 ? std::max(1,int(std::ceil((peer_quote.gems+receipt.gems)/10.0))) : 0;
                pair.helper_duration=receipt.totalduration();
                const int horizon=std::max(1,std::min(2000-round_,std::max(pair.banktime(),receipt.banktime())));
                auto comparison=compare_shared(pair,receipt,solo_owner,helper_alt,horizon,self_funding,peer_funding);
                if(!comparison.admit) continue;
                configure_next_priority(job.group,peer,std::min(native(me),native(peer)),round_);
                party=true;trace("party",target_);break;
            }
        }
        if(cambc::round_number()!=round_) return;
        if(!party) me.set_priority(0);
        if(cambc::round_number()!=round_) return;
        error_stage="camp-micro";bool attempted=false;strike_current(job.group,&attempted);
        occupancy_dirty_=occupancy_dirty_||attempted;
        if(cambc::round_number()!=round_) return;
        if(!me.can_attack()&&!me.can_cast()) return;
        if(!job.observed) move(pads);
    }
};
}
