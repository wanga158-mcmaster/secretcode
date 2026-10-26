#include <cambc.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include <cstdio>
#include <string>
#include <type_traits>
#include <cmath>
#include "policy.h"
#include "profile.h"
#include "clockwork.h"

#if CLOCKWORK_FUEL_DIAGNOSTICS
#define CW_FUEL_STAGE(value) fuel_stage(value)
#else
#define CW_FUEL_STAGE(value)
#endif
#if CLOCKWORK_CONTINUATION_REPAIR
#define CW_DETAIL_GUARD if (detail_ready())
#else
#define CW_DETAIL_GUARD
#endif
#if CLOCKWORK_COMPONENT_TRACE
#define CW_COMPONENT_STEP(value) component_trace_step_ = value
#else
#define CW_COMPONENT_STEP(value)
#endif

namespace {
using cambc::vec2;
using cambc::team;
using cambc::champion;
using cambc::camp;
using cambc::bitboard;
using cambc::item_type;

int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
bitboard around(vec2 p, int radius) {
    bitboard result;
    result.draw_chebyshev(p, radius);
    return result;
}

void draft() {
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
        cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order)
        if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
            cambc::draft::pick(choice);
            return;
        }
}

struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    float health = 0, incoming = 0;
    bool monolith = false;
    unsigned unavailable_until = 0, key = 0;
    clockwork::Observation clock;
    unsigned held_round = 0, capture_round = 0, last_scan = 0;
    bool residual_pending = false;
    bool blocked_clock_logged = false;
    bool clock_invalidated = false;
    unsigned prediction_due = 0;
};

class Bot {
#if CLOCKWORK_COMPONENT_TRACE
    unsigned component_trace_step_ = 0, component_trace_tiles_ = 0;
    unsigned component_trace_walls_ = 0;
    std::array<unsigned, 3> component_trace_approach_{}, component_trace_hits_{};
#endif
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bitboard observed_tiles_, observed_champions_;
    bool initialized_ = false, recovering_ = false, partial_clear_ = false;
    bool has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0, identity_ = 0;
    unsigned fields_this_turn_ = 0, navigation_failures_ = 0, purchases_ = 0;
    unsigned target_changes_ = 0, blocked_retries_ = 0, planning_guards_ = 0;
    unsigned seq_ = 0, details_ = 0, summaries_ = 0, dropped_ = 0;
    unsigned last_turn_ = 0, turn_gaps_ = 0, summary_bucket_ = 0;
    std::string actor_;
    float camp_multiplier_ = 1, incoming_multiplier_ = 1;
    std::array<int, 3> circuit_{-1, -1, -1};
    std::array<bool, 3> done_{};
    std::array<unsigned, 3> unreachable_{};
    unsigned circuit_count_ = 0, last_completed_ = 2, lap_ = 0;
    int committed_ = -1, circuit_stage_ = -1;
    unsigned waited_ = 0, premature_visits_ = 0, empty_visits_ = 0;
    unsigned wait_rounds_ = 0, completed_laps_ = 0, timing_checks_ = 0;
    unsigned censored_checks_ = 0, deferrals_ = 0, invalidations_ = 0;
    bool no_circuit_logged_ = false;
    unsigned last_navigation_round_ = 0;
    unsigned probe_start_ = 0, probe_phase_ = 0;
    std::array<int, 3> fixture_{-1, -1, -1};
    bool probe_finished_ = false;
    bool probe_opportunity_ = false, probe_defer_seen_ = false;
    bool probe_choice_seen_ = false, probe_empty_seen_ = false;
#if CLOCKWORK_PLANNING_REPAIR
    // Ranking can use several fresh owner turns. No action is taken from a
    // partially scanned menu, and construction gets its own fresh fuel reserve.
    unsigned rank_cursor_ = 0;
    int rank_best_ = -1, rank_partial_ = -1;
    float rank_best_score_ = -1, rank_partial_score_ = -1;
    bool rank_ready_ = false;
    vec2 rank_origin_{};
    int rank_stage_ = -1;
#endif
#if CLOCKWORK_STARTUP_REPAIR
    bool setup_started_ = false;
    bool setup_component_ready_ = false;
    unsigned setup_cursor_ = 0;
    std::vector<cambc::map::camp_group> setup_groups_;
    bitboard home_component_;
#endif
#if CLOCKWORK_REACHABILITY_REPAIR
    unsigned reachability_round_ = 0;
    unsigned checked_navigation_failures_ = 0;
#endif
#if CLOCKWORK_CONTINUATION_REPAIR
    int contested_group_ = -1;
    unsigned contested_since_ = 0;
    unsigned contested_last_round_ = 0;
#endif
#if CLOCKWORK_FUEL_DIAGNOSTICS
    unsigned fuel_origin_round_ = 0, fuel_count_ = 0;
    std::array<unsigned, 24> fuel_ids_{}, fuel_rounds_{}, fuel_values_{};
    void fuel_stage(unsigned id) {
        if (fuel_origin_round_ < 90 || fuel_origin_round_ > 100 || fuel_count_ >= fuel_ids_.size()) return;
        fuel_ids_[fuel_count_] = id;
        fuel_rounds_[fuel_count_] = cambc::round_number();
        fuel_values_[fuel_count_++] = cambc::fuel_remaining();
    }
    void fuel_begin() {
        if (fuel_count_ && cambc::fuel_remaining() > 85000) {
            char line[512];
            int used = std::snprintf(line, sizeof(line), "CWFUEL actor=%u round=%u", identity_, fuel_origin_round_);
            for (unsigned i = 0; i < fuel_count_ && used > 0 && used < 450; ++i)
                used += std::snprintf(line+used, sizeof(line)-used, " %u:%u:%u", fuel_ids_[i],fuel_rounds_[i],fuel_values_[i]);
            if (used > 0 && used < 510) { line[used++]='\n'; std::cout.write(line,used); }
        }
        fuel_count_ = 0; fuel_origin_round_ = cambc::round_number();
    }
#endif

    static unsigned tile(vec2 p) { return p.y * cambc::map::width() + p.x; }
    static std::string boolean(bool value) { return value ? "true" : "false"; }
    std::string key(int i) const { return "g_" + std::to_string(groups_[i].key); }
    bool visible(const Group& g) const { return !(g.spawns & observed_tiles_).is_empty(); }
    bool fully_visible(const Group& g) const { return (g.spawns & ~observed_tiles_).is_empty(); }
    bool empty_equipment() const {
        const auto inventory=cambc::self().item_count();
        return std::all_of(inventory.begin(),inventory.end(),[](unsigned count){return count==0;});
    }
    int slot(int group) const {
        for (unsigned j = 0; j < circuit_count_; ++j) if (circuit_[j] == group) return j;
        return -1;
    }
    int equipment_stage() const {
        return monster_shards_ < item_type::monster_shard_of_strength.combine_count() ? 0
            : jungle_shards_ < item_type::jungle_shard_of_health.combine_count() ? 1 : 2;
    }
    bool stage_eligible(int i) const {
#if CLOCKWORK_CONTINUATION_REPAIR
        if (groups_[i].unavailable_until > cambc::round_number()) return false;
#endif
#if CLOCKWORK_STARTUP_REPAIR
        // A group is a possible job when ANY of its original spawn tiles is
        // in this actor's public terrain component, not only its anchor.
        if ((groups_[i].spawns & home_component_).is_empty()) return false;
#endif
        return !groups_[i].monolith && !(has_non_monolith_gems_ && equipment_stage() == 2
            && groups_[i].group.gem_value() == 0);
    }
#if CLOCKWORK_CONTINUATION_REPAIR
    bool detail_ready() {
        // This guard precedes argument construction. Checking only in event()
        // still pays for clock_data and its strings after all detail slots end.
        if (details_ >= 32 || cambc::fuel_remaining() < 30000) { ++dropped_; return false; }
        return true;
    }
#endif
    void event(const char* kind, const char* subtype, const std::string& data = "", bool essential = false) {
        if (cambc::fuel_remaining() < 8000) { ++dropped_; return; }
        const bool summary = std::string(kind) == "summary", start = std::string(kind) == "start";
        const unsigned cap = profile::probe_mode && !essential ? 24 : 32;
        if ((!start && !summary && details_ >= cap) || (summary && summaries_ >= 62)) { ++dropped_; return; }
        char line[513];
        const int length = std::snprintf(line, sizeof(line),
            "BC26T{\"s\":1,\"v\":\"%s\",\"c\":\"%s\",\"a\":\"%s\",\"r\":%u,\"q\":%u,\"k\":\"%s\",\"d\":{\"subtype\":\"%s\"%s%s}}\n",
            profile::version_id, profile::core_id, actor_.c_str(), cambc::round_number(), seq_+1,
            kind, subtype, data.empty() ? "" : ",", data.c_str());
        if (length <= 0 || length > 512) { ++dropped_; return; }
        ++seq_; if (summary) ++summaries_; else if (!start) ++details_;
        std::cout.write(line, length);
    }
    std::string clock_data(int i, const char* reason) const {
        const auto& g = groups_[i];
        const unsigned r = cambc::round_number();
        return "\"group\":\"" + key(i) + "\",\"native_group_id\":"
            + std::to_string(static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(g.group)))
            + ",\"raw_timer\":" + std::to_string(g.clock.raw_timer)
            + ",\"nominal_due\":" + std::to_string(g.clock.due)
            + ",\"age\":" + std::to_string(r-g.clock.round)
            + ",\"arrival_estimate\":" + std::to_string(r+distance(cambc::self().position(), g.anchor))
            + ",\"lap\":" + std::to_string(lap_) + ",\"reason\":\"" + reason + "\"";
    }
    void telemetry_tick() {
        const unsigned round = cambc::round_number();
        if (last_turn_ && round > last_turn_+1) turn_gaps_ += round-last_turn_-1;
        last_turn_ = round;
        if (round/32 > summary_bucket_) {
            summary_bucket_ = round/32;
            event("summary", "counters", "\"lastObservedRound\":" + std::to_string(round)
                + ",\"droppedEvents\":" + std::to_string(dropped_) + ",\"turn_gaps\":" + std::to_string(turn_gaps_)
                + ",\"premature_visits\":" + std::to_string(premature_visits_)
                + ",\"empty_visits\":" + std::to_string(empty_visits_) + ",\"wait_rounds\":" + std::to_string(wait_rounds_)
                + ",\"completed_laps\":" + std::to_string(completed_laps_)
                + ",\"timing_checks\":" + std::to_string(timing_checks_) + ",\"censored_checks\":" + std::to_string(censored_checks_)
                + ",\"deferrals\":" + std::to_string(deferrals_) + ",\"invalidations\":" + std::to_string(invalidations_));
        }
    }
    void mark_visit(int i, bool completed, const char* reason) {
        const int s = slot(i);
        if (s >= 0) {
            done_[s] = true;
            if (completed) last_completed_ = s;
        }
        if (!completed) {
            ++deferrals_;
            CW_DETAIL_GUARD event("activation", "clockwork_defer", clock_data(i, reason), profile::probe_mode && probe_opportunity_);
            if (profile::probe_mode && probe_opportunity_ && i == fixture_[0] && profile::core_enabled)
                probe_defer_seen_ = true;
        }
        if (committed_ == i) committed_ = -1;
        if (target_ == i) target_ = -1;
        waited_ = 0;
    }
    void interrupt_circuit(const char* reason) {
#if CLOCKWORK_CONTINUATION_REPAIR
        contested_group_ = -1;
#endif
#if CLOCKWORK_PLANNING_REPAIR
        rank_cursor_ = 0; rank_ready_ = false; rank_stage_ = -1;
#endif
        if (committed_ >= 0) {
            ++invalidations_;
            CW_DETAIL_GUARD event("observation", "clockwork_invalidate", clock_data(committed_, reason));
        }
        // Banking/recovery clears a commitment, never a lap or membership.
        committed_ = -1; target_ = -1; waited_ = 0;
    }
    void observe_group(int i) {
        auto& g = groups_[i];
        const unsigned r = cambc::round_number();
        if (!visible(g)) { if (g.residual_pending) g.clock.censored = true; return; }
        if (g.last_scan == r) return;
        g.last_scan = r;
        const auto old = g.clock;
        const auto state = g.group.state(); // Above guard is the SDK's exact visibility precondition.
        clockwork::Lifecycle next = clockwork::Lifecycle::alive;
        int timer = 0;
        if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state)) {
            next = clockwork::Lifecycle::respawning; timer = respawn->timer;
        } else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) next = clockwork::Lifecycle::reward;
        if (old.round+1 != r || !fully_visible(g)) g.clock.censored = true;
        const auto occupied = observed_champions_ & g.spawns;
        if (!occupied.is_empty()) g.clock.censored = true;
        const bool captured = next == clockwork::Lifecycle::respawning
            && old.state == clockwork::Lifecycle::reward && g.held_round+1 == r
            && g.spawns.is_set(cambc::self().position());
        g.clock.round = r; g.clock.raw_timer = timer; g.clock.state = next;
        if (next == clockwork::Lifecycle::respawning) {
            g.clock.due = clockwork::nominal_due(r, timer);
            if (old.state != next) {
                g.prediction_due = g.clock.due; g.residual_pending = true;
                g.blocked_clock_logged = false;
                g.clock_invalidated = false;
                g.clock.censored = !fully_visible(g) || !occupied.is_empty();
            }
        }
        if (old.state != next || captured)
            CW_DETAIL_GUARD event("observation", "clockwork_observe", clock_data(i, captured ? "capture_confirmed" : "personal_lifecycle")
                + ",\"state\":" + std::to_string(static_cast<int>(next)), profile::probe_mode && (captured || probe_opportunity_));
        if (captured) {
            g.capture_round = r; mark_visit(i, true, "captured");
        } else if (next == clockwork::Lifecycle::respawning && old.state != next
            && old.state != clockwork::Lifecycle::unknown && committed_ == i) {
            g.clock_invalidated = true;
            ++invalidations_;
            CW_DETAIL_GUARD event("observation", "clockwork_invalidate", clock_data(i, "unexpected_respawn_or_other_capture"));
            // A known live/reward camp changing cycle under another actor is
            // an invalidated commitment. A first unknown scouting observation
            // is handled as an ordinary empty visit below instead.
            ++empty_visits_; mark_visit(i,false,"unexpected_respawn_or_other_capture");
        }
        if (next == clockwork::Lifecycle::respawning && timer <= 0 && !occupied.is_empty()
            && !g.blocked_clock_logged) {
            g.blocked_clock_logged = true; ++invalidations_;
            g.clock_invalidated = true;
            CW_DETAIL_GUARD event("observation","clockwork_invalidate",clock_data(i,"observed_spawn_occupancy_blocks_return"));
        }
        if (g.residual_pending && next == clockwork::Lifecycle::alive) {
            ++timing_checks_; if (g.clock.censored) ++censored_checks_;
            CW_DETAIL_GUARD event("observation", "clockwork_observe", clock_data(i, "next_live_checked")
                + ",\"prediction_due\":" + std::to_string(g.prediction_due)
                + ",\"residual\":" + std::to_string(static_cast<int>(r)-static_cast<int>(g.prediction_due))
                + ",\"censored\":" + boolean(g.clock.censored), profile::probe_mode);
            g.residual_pending = false;
            if (profile::probe_mode && probe_opportunity_ && i == fixture_[0] && probe_choice_seen_
                && ((profile::core_enabled && probe_defer_seen_) || (!profile::core_enabled && probe_empty_seen_)))
                probe_stop(g.clock_invalidated ? "not_exercised" : "mechanism_success",
                    g.clock_invalidated ? "clock_invalidated_before_next_live_observation" : "calendar_contrast_and_next_live_observation");
        }
    }
    void observe() {
        // Observe the bounded working set before elective candidate scanning.
        // Scanning every map group here could consume the planning reserve and
        // permanently starve even an already committed capture.
        if (target_ >= 0) observe_group(target_);
        if (profile::probe_mode) {
            for (const int i : fixture_) if (i >= 0) observe_group(i);
        } else {
            for (unsigned j = 0; j < circuit_count_; ++j) observe_group(circuit_[j]);
        }
    }

#include "runtime.inc"

    bool affordable(int i, const std::vector<camp>& camps) const {
        const auto& g = groups_[i]; const auto& me = cambc::self();
        if (visible(g) && g.clock.state == clockwork::Lifecycle::reward) return true;
        float hp = g.health, incoming = g.incoming;
        if (fully_visible(g)) {
            hp = incoming = 0;
            for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) {
                hp += m.health(); incoming += m.strength();
            }
        }
        return fundamentals::affordable_camp(me.health(), me.max_health(), hp,
            incoming*incoming_multiplier_, (me.strength()*1.125f+me.focus()*0.12f)*camp_multiplier_);
    }
    int ordinary_target(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        auto& me = cambc::self();
        int best = -1, partial = -1; float score_best = -1, partial_best = -1;
        const float dps = me.strength()*1.125f+me.focus()*0.12f;
        for (unsigned i = 0; i < groups_.size(); ++i) {
            if (cambc::fuel_remaining() < 45000) { ++planning_guards_; break; }
            auto& g = groups_[i];
            if (!stage_eligible(i) || g.unavailable_until > cambc::round_number()) continue;
            const bool seen = visible(g);
            if (seen) observe_group(i);
            if (seen && g.clock.state == clockwork::Lifecycle::respawning) continue;
            const bool reward = seen && g.clock.state == clockwork::Lifecycle::reward;
            float remaining = g.health;
            if (fully_visible(g)) {
                remaining = 0;
                for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) remaining += m.health();
            }
            float travel = distance(me.position(), g.anchor);
            for (const auto& ally : allies) if (ally != me && distance(ally.position(), g.anchor) <= 3) travel += 12;
            travel += (i*3+identity_*11)%7;
            float score = (g.group.gem_value()*2.0f+g.group.gold_value()*0.4f)
                / (travel+(reward ? 0 : remaining/std::max(1.0f,dps))+10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            if (affordable(i,camps)) {
                if (score > score_best) { score_best=score; best=i; }
            } else if ((me.health() >= me.max_health()*0.75f || target_ == static_cast<int>(i)) && score > partial_best) {
                partial_best=score; partial=i;
            }
        }
        partial_clear_ = best < 0 && partial >= 0;
        return best >= 0 ? best : partial;
    }
    void construct_circuit(int first, const std::vector<camp>& camps, const char* reason) {
#if CLOCKWORK_REACHABILITY_REPAIR
        reachability_round_ = 0;
#endif
        circuit_.fill(-1); done_.fill(false); unreachable_.fill(0);
        circuit_count_=0; committed_=-1; waited_=0; last_completed_=2;
        circuit_stage_=equipment_stage();
        if (first >= 0 && stage_eligible(first) && affordable(first,camps)) circuit_[circuit_count_++]=first;
        while (circuit_count_ > 0 && circuit_count_ < 3) {
            int next=-1, best_distance=100000;
            const auto anchor=groups_[circuit_[circuit_count_-1]].anchor;
            // Public anchor distances only. No candidate path fields.
            for (unsigned i=0;i<groups_.size();++i) {
                if (cambc::fuel_remaining()<45000) break;
                if (slot(i)>=0 || !stage_eligible(i) || !affordable(i,camps)
                    || (visible(groups_[i]) && groups_[i].clock.state==clockwork::Lifecycle::respawning)) continue;
                const int d=distance(anchor,groups_[i].anchor);
                if (d<best_distance) {best_distance=d;next=i;}
            }
            if(next<0)break;
            circuit_[circuit_count_++]=next;
        }
        last_completed_=circuit_count_ ? circuit_count_-1 : 2;
        if (!circuit_count_) {
            if(!no_circuit_logged_) {event("observation","clockwork_circuit","\"reason\":\"no_circuit\"");no_circuit_logged_=true;}
            return;
        }
#if CLOCKWORK_CONTINUATION_REPAIR
        if (!detail_ready()) return;
#endif
        std::string members="\"group_count\":"+std::to_string(circuit_count_);
        for(unsigned j=0;j<circuit_count_;++j)members+=",\"group"+std::to_string(j)+"\":\""+key(circuit_[j])+"\"";
        event("activation","clockwork_circuit",members+",\"stage\":"+std::to_string(circuit_stage_)
            +",\"reason\":\""+reason+"\",\"lap\":"+std::to_string(lap_),profile::probe_mode);
    }
    bool locally_reachable() {
        CW_COMPONENT_STEP(30);
        CW_FUEL_STAGE(17);
        if (cambc::fuel_remaining()<45000 || fields_this_turn_>=2) return false;
#if CLOCKWORK_REACHABILITY_REPAIR
        // Consecutive failed certificates, rather than a promise that an
        // elective certificate is recomputed on every adjacent owner turn.
        // Any newly reachable member is reset below from the fresh field.
#else
        if(last_navigation_round_+1!=cambc::round_number())unreachable_.fill(0);
#endif
        last_navigation_round_=cambc::round_number();
        auto walls=blocked();walls.set(cambc::self().position(),false);
        cambc::distance_field field;bitboard visited;
        ++fields_this_turn_;
        // A single bounded component field checks all three destinations.
        // This is separate from construction and leaves one field for a move.
        field.compute(walls,around(cambc::self().position(),0),visited,256);
#if CLOCKWORK_COMPONENT_TRACE
        if (identity_ == static_cast<unsigned>(cambc::champion_type::tavi) && cambc::round_number() >= 696 && cambc::round_number() <= 720) {
            component_trace_tiles_ = visited.count();
            component_trace_walls_ = walls.count();
        }
#endif
        CW_FUEL_STAGE(18);
        for(unsigned j=0;j<circuit_count_;++j) {
            bitboard approach;
            for(const auto& m:groups_[circuit_[j]].group) approach|=around(m.spawn_position(),cambc::self().attack_range());
            approach&=~walls;
#if CLOCKWORK_COMPONENT_TRACE
            if (identity_ == static_cast<unsigned>(cambc::champion_type::tavi) && cambc::round_number() >= 696 && cambc::round_number() <= 720) {
                component_trace_approach_[j] = approach.count();
                component_trace_hits_[j] = (approach & visited).count();
            }
#endif
            if((approach&visited).is_empty()) {
#if CLOCKWORK_REACHABILITY_REPAIR
                unreachable_[j] += 2;
#else
                ++unreachable_[j];
#endif
            } else unreachable_[j]=0;
        }
        CW_FUEL_STAGE(19);
        return true;
    }
    bool safe_wait(int i, const std::vector<camp>& camps) const {
        const auto p=cambc::self().position();
        if(groups_[i].spawns.is_set(p))return false;
        for(const auto& m:camps) if(m.is_alive() && m.is_awake() && m.strength()>0
            && distance(p,m.position())<=m.attack_range())return false;
        for(const auto& enemy:cambc::vision::champions(team::enemy))
            if(distance(p,enemy.position())<=enemy.attack_range()+1)return false;
        return true;
    }
    bool respawning_visit(int i,const std::vector<camp>& camps) {
        auto& g=groups_[i];
        if(!visible(g) || g.clock.state!=clockwork::Lifecycle::respawning)return false;
        if(waited_==0) {
            ++empty_visits_;
            if(clockwork::premature(g.clock,cambc::round_number(),distance(cambc::self().position(),g.anchor)))++premature_visits_;
            CW_DETAIL_GUARD event("observation","clockwork_observe",clock_data(i,"empty_visit"),profile::probe_mode && probe_opportunity_);
            if(profile::probe_mode && probe_opportunity_ && i==fixture_[0])probe_empty_seen_=true;
        }
        // A capture leaves us on an original spawn. Always step off, even when
        // the timer is zero or waiting would otherwise be allowed.
        if(g.spawns.is_set(cambc::self().position())) {
            bitboard goals=around(g.anchor,2)&~g.spawns;
            bitboard danger;
            for(const auto& m:camps)if(m.is_alive() && m.is_awake() && m.strength()>0)danger|=around(m.position(),m.attack_range());
            move_to(goals,&danger);
            mark_visit(i,false,"leave_spawn_after_capture");return true;
        }
        if(profile::core_enabled && clockwork::can_wait(g.clock.raw_timer,waited_) && safe_wait(i,camps)) {
            ++waited_;++wait_rounds_;
            CW_DETAIL_GUARD event("activation","clockwork_wait",clock_data(i,"observed_imminent")+",\"waited\":"+std::to_string(waited_));
            return true;
        }
        mark_visit(i,false,profile::core_enabled ? "respawn_not_imminent_or_wait_cap" : "disabled_empty_visit");
        return true;
    }
    int clockwork_target(int ordinary,const std::vector<camp>& camps) {
        CW_COMPONENT_STEP(40);
        CW_FUEL_STAGE(12);
        if(cambc::fuel_remaining()<45000)return -2;
        if(!circuit_count_ || circuit_stage_!=equipment_stage())construct_circuit(ordinary,camps,circuit_count_?"equipment_stage_changed":"first_farm");
        if(!circuit_count_){CW_COMPONENT_STEP(41);return -1;}
        CW_FUEL_STAGE(13);
#if CLOCKWORK_REACHABILITY_REPAIR
        // The component certificate is elective planning, not part of the
        // action budget. Refresh it on a new circuit or an actual failed path
        // attempt, then yield. Successful walks within the same component do
        // not invalidate it. Every walk still rebuilds current blockers and
        // every combat call retains its current visibility and health guards.
        bool blocked_circuit=true, exhausted_circuit=true;
        for (unsigned j=0;j<circuit_count_;++j) {
            blocked_circuit &= unreachable_[j]>=clockwork::unreachable_limit;
            exhausted_circuit &= unreachable_[j]>=clockwork::rebuild_limit;
        }
        // Once every member is excluded there can be no new path attempt to
        // invalidate the cache. Recheck that real dead-end until a path opens
        // or the unchanged rebuild threshold is reached.
        if (!reachability_round_ || checked_navigation_failures_ != navigation_failures_
            || (blocked_circuit && !exhausted_circuit)) {
            if (!locally_reachable()) {CW_COMPONENT_STEP(42);return -2;}
            reachability_round_ = cambc::round_number();
            checked_navigation_failures_ = navigation_failures_;
            CW_COMPONENT_STEP(43);return -2;
        }
#else
        if(!locally_reachable())return -2;
#endif
        CW_FUEL_STAGE(15);
        bool all_unreachable=true;
        for(unsigned j=0;j<circuit_count_;++j)all_unreachable&=unreachable_[j]>=clockwork::rebuild_limit;
        if(all_unreachable) {
            if(ordinary<0) ordinary=ordinary_target(camps,cambc::vision::champions(team::ally));
            construct_circuit(ordinary,camps,"all_members_unreachable_eight_turns");CW_COMPONENT_STEP(44);return -2;
        }
        if(committed_>=0) {
            const int s=slot(committed_);
            if(s<0 || !stage_eligible(committed_) || !affordable(committed_,camps)) {
                ++invalidations_;CW_DETAIL_GUARD event("observation","clockwork_invalidate",clock_data(committed_,"unaffordable_or_stage"));
                mark_visit(committed_,false,"unaffordable_or_stage");return -2;
            }
            if(unreachable_[s]>=clockwork::unreachable_limit) {
                ++invalidations_;CW_DETAIL_GUARD event("observation","clockwork_invalidate",clock_data(committed_,"unreachable_four_turns"));
                mark_visit(committed_,false,"unreachable_four_turns");return -2;
            }
            if(respawning_visit(committed_,camps))return -2;
            return committed_;
        }
        bool all_done=true;for(unsigned j=0;j<circuit_count_;++j)all_done&=done_[j];
        if(all_done) {done_.fill(false);++lap_;++completed_laps_;}
        int chosen=-1,earliest=-1;
        for(unsigned offset=1;offset<=circuit_count_;++offset) {
            const unsigned s=(last_completed_+offset)%circuit_count_;
            if(done_[s])continue;
            const int i=circuit_[s];
            if(!stage_eligible(i) || !affordable(i,camps) || unreachable_[s]>=clockwork::unreachable_limit) {mark_visit(i,false,"ineligible_or_unreachable");continue;}
            const auto& g=groups_[i];
            const unsigned travel=distance(cambc::self().position(),g.anchor);
            if(!profile::core_enabled || !clockwork::premature(g.clock,cambc::round_number(),travel)) {chosen=i;break;}
            if(earliest<0 || g.clock.due<groups_[earliest].clock.due
                || (g.clock.due==groups_[earliest].clock.due && (travel<static_cast<unsigned>(distance(cambc::self().position(),groups_[earliest].anchor))
                    || (travel==static_cast<unsigned>(distance(cambc::self().position(),groups_[earliest].anchor)) && g.key<groups_[earliest].key))))earliest=i;
            mark_visit(i,false,"nominal_due_after_arrival");
        }
        if(chosen<0)chosen=earliest;
        bool now_done=true;for(unsigned j=0;j<circuit_count_;++j)now_done&=done_[j];
        if(now_done) {done_.fill(false);++lap_;++completed_laps_;}
        // Do not scan the newly cleared lap again this turn.
        if(chosen<0){CW_COMPONENT_STEP(45);return -2;}
        committed_=chosen;waited_=0;target_=chosen;
        if(profile::probe_mode && probe_opportunity_ && !probe_choice_seen_) {
            probe_choice_seen_=true;
            const int expected=profile::core_enabled?fixture_[1]:fixture_[0];
            event("action_result","clockwork_circuit",clock_data(chosen,"released_choice")
                +",\"expected_group\":\""+key(expected)+"\"",true);
            if(chosen!=expected || (profile::core_enabled && !probe_defer_seen_))probe_stop("mechanism_failed","released_calendar_choice_mismatch");
        }
        return chosen;
    }

    bool bank() {
        auto& me=cambc::self();
        if(!me.gem_count() || altars_.empty())return false;
        int nearest=10000;for(const auto a:altars_)nearest=std::min(nearest,distance(me.position(),a));
        const unsigned last=cambc::constants::max_rounds>100?cambc::constants::max_rounds-100:0;
        if(me.gem_count()<24 && nearest>4 && cambc::round_number()<last)return false;
        interrupt_circuit("banking");
        bitboard goals;
        for(const auto a:altars_) {
            if(distance(me.position(),a)<=cambc::constants::submit_gems_range) {
                for(const auto& lock:cambc::vision::altar_locks(team::enemy))if(lock.position()==a) {
                    if(me.can_attack() && me.attack_tiles().is_set(a))me.attack(lock);
                    return true;
                }
                me.submit_gems(a);
                CW_DETAIL_GUARD event("action_result","tribute_start","\"bag\":"+std::to_string(me.gem_count())
                    +",\"active\":"+boolean(me.is_submitting_gems()));
                return true;
            }
            goals|=around(a,cambc::constants::submit_gems_range);
        }
        move_to(goals);return true;
    }
    void work_group(int i,const std::vector<camp>& camps) {
        CW_COMPONENT_STEP(50);
        auto& me=cambc::self();auto& g=groups_[i];
#if CLOCKWORK_CONTINUATION_REPAIR
        if(cambc::fuel_remaining()<30000)return;
#else
        if(cambc::fuel_remaining()<16000)return;
#endif
        if(visible(g) && g.clock.state==clockwork::Lifecycle::respawning) {
            respawning_visit(i,camps);return;
        }
        if(visible(g) && g.clock.state==clockwork::Lifecycle::reward) {
            if(g.spawns.is_set(me.position()))g.held_round=cambc::round_number();
            else if(!move_to(g.spawns) && me.can_move())++navigation_failures_;
            if(g.spawns.is_set(me.position()))g.held_round=cambc::round_number();
            return;
        }
        std::optional<camp> target;float weakest=std::numeric_limits<float>::max();
        for(const auto& m:camps)if(g.spawns.is_set(m.spawn_position()) && m.is_alive()) {
            const float score=m.health()+distance(me.position(),m.position())*(partial_clear_?1000.0f:100.0f);
            if(score<weakest || (score==weakest && (!target || tile(m.spawn_position())<tile(target->spawn_position())))) {target=m;weakest=score;}
        }
        if(target && fight(*target,false,g.incoming>0))return;
        bitboard goals;
        if(target)goals=around(target->position(),me.attack_range());
        else for(const auto& m:g.group)goals|=around(m.spawn_position(),1);
        if(move_to(goals)) {
            for(const auto& m:cambc::vision::camps())if(g.spawns.is_set(m.spawn_position()) && m.is_alive()
                && me.attack_tiles().is_set(m.position())) {fight(m,true,g.incoming>0);break;}
        } else if(me.can_move() && !goals.is_set(me.position()))++navigation_failures_;
    }
    void farm(const std::vector<camp>& camps,const std::vector<champion>& allies) {
        CW_COMPONENT_STEP(10);
        CW_FUEL_STAGE(6);
        // An existing circuit replaces ordinary target selection. Do not spend
        // its execution reserve re-ranking unrelated groups on every visit.
        const bool needs_first = !circuit_count_ || circuit_stage_!=equipment_stage();
#if CLOCKWORK_PLANNING_REPAIR
        int ordinary = -1;
        if (needs_first) {
            // Keep partial work alive between planning episodes. Recovery and
            // bank interruptions clear its target through interrupt_circuit.
            if (target_ >= 0 && partial_clear_ && stage_eligible(target_)
                && !(visible(groups_[target_]) && groups_[target_].clock.state == clockwork::Lifecycle::respawning)) {
                ordinary = target_;
            } else {
                ordinary = staged_target(camps, allies);
                if (ordinary == -2) {CW_COMPONENT_STEP(11);return;}
            }
        }
#else
        const int ordinary=needs_first?ordinary_target(camps,allies):-1;
#endif
        const int circuit=clockwork_target(ordinary,camps);
        CW_FUEL_STAGE(9);
        if(circuit==-2)return;
        const int selected=circuit>=0?circuit:ordinary;
        if(selected<0) {
            CW_COMPONENT_STEP(12);
            if(!cambc::map::spawn_region(team::ally).is_set(cambc::self().position()))recovering_=true;
            return;
        }
        if(circuit>=0)partial_clear_=false;
        if(target_!=selected)++target_changes_;
        target_=selected;CW_FUEL_STAGE(10);work_group(selected,camps);CW_FUEL_STAGE(11);
    }
#if CLOCKWORK_PLANNING_REPAIR
    int staged_target(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        auto& me = cambc::self();
        if (rank_stage_ != equipment_stage() || rank_origin_ != me.position()) {
            rank_cursor_ = 0; rank_ready_ = false; rank_stage_ = equipment_stage();
            rank_origin_ = me.position();
        }
        if (rank_ready_) {
            const int chosen = rank_best_ >= 0 ? rank_best_ : rank_partial_;
            partial_clear_ = rank_best_ < 0 && rank_partial_ >= 0;
            rank_cursor_ = 0; rank_ready_ = false;
            // The menu was collected without movement. Recheck its selected
            // member's current personal lifecycle before handing it to the
            // unchanged circuit affordability and execution guards.
            if (chosen >= 0) {
                observe_group(chosen);
                if (!stage_eligible(chosen) || groups_[chosen].unavailable_until > cambc::round_number()
                    || (visible(groups_[chosen]) && groups_[chosen].clock.state == clockwork::Lifecycle::respawning)) return -2;
            }
            return chosen;
        }
        if (rank_cursor_ == 0) {
            rank_best_ = rank_partial_ = -1; rank_best_score_ = rank_partial_score_ = -1;
        }
        const float dps = me.strength()*1.125f + me.focus()*0.12f;
        while (rank_cursor_ < groups_.size()) {
            if (cambc::fuel_remaining() < 45000) { ++planning_guards_; return -2; }
            const unsigned i = rank_cursor_++;
            auto& g = groups_[i];
            if (!stage_eligible(i) || g.unavailable_until > cambc::round_number()) continue;
            const bool seen = visible(g);
            if (seen) observe_group(i);
            if (seen && g.clock.state == clockwork::Lifecycle::respawning) continue;
            const bool reward = seen && g.clock.state == clockwork::Lifecycle::reward;
            float remaining = g.health;
            if (fully_visible(g)) {
                remaining = 0;
                for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) remaining += m.health();
            }
            float travel = distance(me.position(), g.anchor);
            for (const auto& ally : allies) if (ally != me && distance(ally.position(), g.anchor) <= 3) travel += 12;
            travel += (i*3+identity_*11)%7;
            float score = (g.group.gem_value()*2.0f + g.group.gold_value()*0.4f)
                / (travel + (reward ? 0 : remaining/std::max(1.0f,dps)) + 10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            if (affordable(i,camps)) {
                if (score > rank_best_score_) { rank_best_score_ = score; rank_best_ = i; }
            } else if ((me.health() >= me.max_health()*0.75f || target_ == static_cast<int>(i)) && score > rank_partial_score_) {
                rank_partial_score_ = score; rank_partial_ = i;
            }
        }
        rank_ready_ = true;
        CW_DETAIL_GUARD event("observation", "clockwork_prepare", "\"reason\":\"complete_menu_fresh_construction_next_turn\",\"groups\":"
            + std::to_string(rank_cursor_));
        return -2;
    }
#endif
    void probe_stop(const char* kind,const char* reason) {
        event(kind,"clockwork_probe","\"scenario_id\":\""+std::string(profile::scenario_id)
            +"\",\"reason\":\""+reason+"\"",true);
        probe_finished_=true;
    }
    unsigned reflected_key(unsigned index) const {
        if(!profile::probe_mirror_with_home)return index;
        const auto ours=cambc::map::spawn_region(team::ally).positions();
        const auto theirs=cambc::map::spawn_region(team::enemy).positions();
        if(ours.empty() || theirs.empty() || tile(ours.front())<tile(theirs.front()))return index;
        bitboard point;point.set({static_cast<int>(index%cambc::map::width()),static_cast<int>(index/cambc::map::width())});
        return tile(point.reflect(cambc::map::symmetry()).positions().front());
    }
    bool select_fixture() {
        for(unsigned j=0;j<3;++j) {
            const unsigned wanted=reflected_key(profile::probe_circuit_keys[j]);
            for(unsigned i=0;i<groups_.size();++i)if(groups_[i].key==wanted)fixture_[j]=i;
            if(fixture_[j]<0) {probe_stop("not_exercised","declared_physical_group_missing");return false;}
            const auto& g=groups_[fixture_[j]];
            if(g.monolith || g.group.camps().size()!=1 || g.incoming>0 || g.group.gold_value()==0 || g.group.gem_value()!=0) {
                probe_stop("not_exercised","declared_harmless_solo_cluster_missing");return false;
            }
        }
        event("observation","clockwork_circuit","\"reason\":\"legal_fixture_selected\",\"scenario_id\":\""
            +std::string(profile::scenario_id)+"\",\"group0\":\""+key(fixture_[0])+"\",\"group1\":\""+key(fixture_[1])
            +"\",\"group2\":\""+key(fixture_[2])+"\",\"gear_items\":0,\"purchases\":"+std::to_string(purchases_),true);
        return true;
    }
    bool leave_spawns(int i,const std::vector<camp>& camps) {
        auto& me=cambc::self();
        if(!groups_[i].spawns.is_set(me.position()))return true;
        bitboard safe=around(groups_[i].anchor,2);
        for(const auto& g:groups_)safe&=~g.spawns;
        bitboard danger;
        for(const auto& m:camps)if(m.is_alive() && m.is_awake() && m.strength()>0)danger|=around(m.position(),m.attack_range());
        move_to(safe,&danger);return false;
    }
    bool probe_tick(const std::vector<camp>& camps) {
        auto& me=cambc::self();
        if(me.champion_name()!=cambc::champion_type::tavi)return true;
        if(probe_finished_)return true;
        if(!probe_start_)probe_start_=cambc::round_number();
        if(cambc::round_number()-probe_start_>=profile::max_setup_rounds && !probe_opportunity_) {
            probe_stop("not_exercised","legal_setup_cap_without_release_predicate");return true;
        }
        if(probe_opportunity_ && cambc::round_number()>=profile::probe_round_limit) {
            probe_stop("not_exercised","next_live_observation_not_reached");return true;
        }
        if(cambc::fuel_remaining()<45000)return true;
        if(!empty_equipment() || purchases_!=0) {
            probe_stop("not_exercised","empty_equipment_common_opening_violated");return true;
        }
        if(probe_phase_==0) {
            if(!select_fixture())return true;
            probe_phase_=1;
        }
        if(probe_phase_==1) {
            const int middle=fixture_[1];
            // Preparation uses the exact production combat and capture actions.
            // The middle group's first capture creates its earlier real clock.
            if(!groups_[middle].capture_round) {target_=middle;work_group(middle,camps);return true;}
            event("observation","clockwork_observe",clock_data(middle,"legal_gold_capture_empty_equipment")
                +",\"gold\":"+std::to_string(me.gold_count())+",\"gear_items\":0,\"purchases\":0",true);
            probe_phase_=2;
        }
        if(probe_phase_==2) {
            const int middle=fixture_[1];
            if(!leave_spawns(middle,camps))return true;
            const auto& g=groups_[middle];
            if(!visible(g) || g.clock.state!=clockwork::Lifecycle::respawning
                || g.clock.raw_timer>static_cast<int>(profile::probe_preparation_timer))return true;
            // Fixed diagnostic membership is declared in the fixture. Lap bits
            // start empty: later completion/defer events arise from real work.
            circuit_=fixture_;circuit_count_=3;circuit_stage_=equipment_stage();
            last_completed_=2;committed_=-1;done_.fill(false);unreachable_.fill(0);
            probe_phase_=3;
            event("activation","clockwork_circuit","\"reason\":\"identical_opening_released\",\"middle_raw_timer\":"+std::to_string(g.clock.raw_timer),true);
        }
        if(probe_phase_==3) {
            // Run A -> deferred B -> C in both arms, with calendar disabled
            // ONLY for this common scripted opening. Production is untouched.
            if(!groups_[fixture_[0]].capture_round) {committed_=fixture_[0];target_=committed_;work_group(committed_,camps);return true;}
            if(!done_[1]) {
                if(!leave_spawns(fixture_[0],camps))return true;
                if(visible(groups_[fixture_[1]]) && groups_[fixture_[1]].clock.state==clockwork::Lifecycle::respawning)
                    mark_visit(fixture_[1],false,"identical_opening_observed_middle_respawn");
                else if(visible(groups_[fixture_[1]])) {probe_stop("not_exercised","middle_returned_before_identical_opening_defer");return true;}
                else {move_to(around(groups_[fixture_[1]].anchor,1));return true;}
            }
            if(!groups_[fixture_[2]].capture_round) {committed_=fixture_[2];target_=committed_;work_group(committed_,camps);return true;}
            if(!leave_spawns(fixture_[2],camps))return true;
            const auto& first=groups_[fixture_[0]];
            const auto& middle=groups_[fixture_[1]];
            const bool first_premature=clockwork::premature(first.clock,cambc::round_number(),distance(me.position(),first.anchor));
            const bool middle_live=visible(middle) && middle.clock.state==clockwork::Lifecycle::alive;
            if(!first_premature || !middle_live)return true;
            probe_opportunity_=true;probe_phase_=4;
            committed_=-1;target_=-1;waited_=0;
            event("opportunity","clockwork_probe",clock_data(fixture_[0],"first_due_later_than_arrival_middle_personally_alive")
                +",\"alternative_group\":\""+key(fixture_[1])+"\",\"calendar_from_capture\":true",true);
        }
        if(probe_phase_==4) {
            farm(camps,cambc::vision::champions(team::ally));return true;
        }
        return true;
    }
public:
#if CLOCKWORK_COMPONENT_TRACE
    void component_trace_finish() {
        if (identity_ != static_cast<unsigned>(cambc::champion_type::tavi)) return;
        const auto round = cambc::round_number();
        if (round < 696 || round > 720 || cambc::fuel_remaining() < 15000) return;
        const auto& me = cambc::self();
        char line[512];
        const int current = circuit_count_ ? circuit_[0] : -1;
        const auto camps = cambc::vision::camps();
        const int size = std::snprintf(line,sizeof(line),
            "CWCOMP r=%u step=%u fuel=%u pos=%d,%d circuit=%u g=%d committed=%d target=%d reach_round=%u nav=%u checked=%u fields=%u unreach=%u,%u,%u done=%u,%u,%u comp=%u walls=%u approach=%u,%u,%u hits=%u,%u,%u home=%u lap=%u eligible=%u affordable=%u clock=%u clock_round=%u hp=%g incoming=%g\n",
            round,component_trace_step_,static_cast<unsigned>(cambc::fuel_remaining()),me.position().x,me.position().y,circuit_count_,
            circuit_count_?static_cast<int>(groups_[circuit_[0]].key):-1,committed_,target_,reachability_round_,navigation_failures_,checked_navigation_failures_,fields_this_turn_,
            unreachable_[0],unreachable_[1],unreachable_[2],unsigned(done_[0]),unsigned(done_[1]),unsigned(done_[2]),component_trace_tiles_,component_trace_walls_,
            component_trace_approach_[0],component_trace_approach_[1],component_trace_approach_[2],component_trace_hits_[0],component_trace_hits_[1],component_trace_hits_[2],home_component_.count(),lap_,
            unsigned(current>=0 && stage_eligible(current)),unsigned(current>=0 && affordable(current,camps)),
            current>=0?static_cast<unsigned>(groups_[current].clock.state):0,current>=0?groups_[current].clock.round:0,
            current>=0?static_cast<double>(groups_[current].health):0.0,current>=0?static_cast<double>(groups_[current].incoming):0.0);
        if (size>0 && size<512)std::cout.write(line,size);
    }
#endif
    void turn() {
        CW_COMPONENT_STEP(1);
#if CLOCKWORK_FUEL_DIAGNOSTICS
        fuel_begin();
#endif
        auto& me=cambc::self();fields_this_turn_=0;
#if CLOCKWORK_STARTUP_REPAIR
        if(!initialized_) { initialize_bounded(); return; }
#else
        if(!initialized_)initialize();
#endif
        CW_FUEL_STAGE(1);
#if CLOCKWORK_CONTINUATION_REPAIR
        const auto previous_summary_bucket=summary_bucket_;
#endif
        telemetry_tick();
#if CLOCKWORK_CONTINUATION_REPAIR
        // Formatting the bounded summary is itself real work. A summary turn
        // may not spend the normal action reserve again after emitting it.
        if(summary_bucket_!=previous_summary_bucket || cambc::fuel_remaining()<70000)return;
#endif
        if(!me.is_alive()) {
            shop();recovering_=false;interrupt_circuit("death");
            if(profile::probe_mode && !probe_finished_)probe_stop("not_exercised","measurement_actor_died");
            return;
        }
        if(me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())return;
        if(!me.can_move() && !me.can_attack() && !me.can_cast())return;
        shop();
        CW_FUEL_STAGE(2);
#if CLOCKWORK_CONTINUATION_REPAIR
        if(cambc::fuel_remaining()<45000)return;
#endif
        const bool spawn=cambc::map::spawn_region(team::ally).is_set(me.position());
        if(spawn) {recovering_=false;if(me.health()<me.max_health())return;}
        const auto camps=cambc::vision::camps();const auto enemies=cambc::vision::champions(team::enemy);
        // This snapshot is used only for pre-action calendar/group observation.
        // Movement and combat continue to use fresh SDK masks and visible units.
        observed_tiles_=cambc::vision::visible_tiles();
        observed_champions_=cambc::vision::champion_tiles();
        CW_FUEL_STAGE(3);
#if CLOCKWORK_CONTINUATION_REPAIR
        if(cambc::fuel_remaining()<45000)return;
#else
        if(cambc::fuel_remaining()<30000)return;
#endif
        float incoming=0;
        for(const auto& m:camps)if(m.is_alive() && m.is_awake() && distance(me.position(),m.position())<=m.attack_range())incoming+=m.strength();
        for(const auto& enemy:enemies)if(distance(me.position(),enemy.position())<=enemy.attack_range()+1)incoming+=enemy.strength();
        if(!spawn && fundamentals::should_retreat(me.health(),me.max_health(),incoming*(partial_clear_?2.0f:1.0f)))recovering_=true;
        if(recovering_) {interrupt_circuit("recovery");recover(camps,enemies);return;}
        observe();
        CW_FUEL_STAGE(4);
        if(profile::probe_mode && me.champion_name()!=cambc::champion_type::tavi)return;
        if (!profile::probe_mode) {
            const int at=group_at_tile_[tile(me.position())];
            if(at>=0 && visible(groups_[at]) && groups_[at].clock.state==clockwork::Lifecycle::respawning) {
                // Never let an idle/scouting/calendar decision suppress a new
                // cycle by remaining on the just-captured original spawn.
                leave_spawns(at,camps);return;
            }
        }
        // Capture is above elective banking or diagnostic behavior. Refresh its
        // lifecycle every turn, and attribute completion to that transition.
        if(target_>=0 && visible(groups_[target_]) && groups_[target_].clock.state==clockwork::Lifecycle::reward
            && groups_[target_].spawns.is_set(me.position())) {
#if CLOCKWORK_CONTINUATION_REPAIR
            const bool contested = std::any_of(enemies.begin(),enemies.end(),[&](const auto& enemy) {
                return groups_[target_].spawns.is_set(enemy.position());
            });
            if (contested) {
                if (contested_group_ != target_ || contested_last_round_+1 != cambc::round_number()) {
                    contested_group_=target_; contested_since_=cambc::round_number();
                }
                contested_last_round_=cambc::round_number();
                const unsigned window=std::max(1,cambc::constants::capture_time);
                if (cambc::round_number()-contested_since_ >= window) {
                    const int old=target_;
                    groups_[old].unavailable_until=cambc::round_number()+2*window;
                    mark_visit(old,false,"personally_contested_capture_replan");
                    circuit_count_=0; partial_clear_=false;
#if CLOCKWORK_PLANNING_REPAIR
                    rank_cursor_=0;rank_ready_=false;rank_stage_=-1;
#endif
                    contested_group_=-1;
                    leave_spawns(old,camps);
                    return;
                }
            } else contested_group_=-1;
#endif
            groups_[target_].held_round=cambc::round_number();return;
        }
        if(profile::probe_mode) {probe_tick(camps);return;}
        if(bank())return;
        for(const auto& enemy:enemies)if(distance(me.position(),enemy.position())<=1 && fight(enemy,false,true))return;
        if(!spawn && me.champion_name()!=cambc::champion_type::tavi && me.gold_count()>=300) {
            recovering_=true;interrupt_circuit("spend_recovery");recover(camps,enemies);return;
        }
        CW_FUEL_STAGE(5);farm(camps,cambc::vision::champions(team::ally));
    }
};
} // namespace

int main() {
    Bot bot;
    while(true) {
        try {if(cambc::is_draft())draft();else {bot.turn();
#if CLOCKWORK_COMPONENT_TRACE
            bot.component_trace_finish();
#endif
        }}
        catch(const std::exception& error) {std::cout << "bot error: " << error.what() << '\n';}
        catch(...) {std::cout << "bot error: unknown exception\n";}
        cambc::end_turn();
    }
}

