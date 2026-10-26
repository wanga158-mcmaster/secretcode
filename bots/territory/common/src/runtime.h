#pragma once
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
#include "competitive.h"
#include "doctrines_a.h"
#include "doctrines_b.h"
#include "native_nav.h"

#ifndef P7_BOUNDED_INITIALIZATION
#define P7_BOUNDED_INITIALIZATION 0
#endif
#ifndef P7_STARTUP_TRACE
#define P7_STARTUP_TRACE 0
#endif
#ifndef P7_LIVE_TRACE
#define P7_LIVE_TRACE 0
#endif
#ifndef P7_REUSE_CURRENT_FIELD
#define P7_REUSE_CURRENT_FIELD 0
#endif
#ifndef P7_RECIPROCAL_SOURCE_FIELDS
#define P7_RECIPROCAL_SOURCE_FIELDS 0
#endif
#ifndef P7_RANGE_RECOVERY
#define P7_RANGE_RECOVERY 0
#endif
#if P7_RANGE_RECOVERY
#include "recovery_nav.h"
#endif
#ifndef P7_PHASE_TRACE
#define P7_PHASE_TRACE 0
#endif
#if P7_PHASE_TRACE
#define P7_PHASE_CLOCK(value, site) phase_clock((value), (site))
#define P7_PHASE_FUEL(value, site) phase_fuel((value), (site))
#define P7_PHASE_SCOPE(id) PhaseScope phase_scope_##id{*this, (id)}
#define P7_PHASE_ENTER(id) phase_event(1, (id))
#define P7_PHASE_LEAVE(id) phase_event(2, (id))
#else
#define P7_PHASE_CLOCK(value, site) (value)
#define P7_PHASE_FUEL(value, site) (value)
#define P7_PHASE_SCOPE(id)
#define P7_PHASE_ENTER(id)
#define P7_PHASE_LEAVE(id)
#endif

namespace p7::live {
namespace native {
using cambc::vec2;
using cambc::bitboard;
using cambc::champion;
using cambc::camp;
using cambc::team;
inline int separation(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
inline bitboard around(vec2 p, int r) { bitboard b; b.draw_chebyshev(p, r); return b; }
// The pinned SDK's scalar accessors each request this same host record. Read
// one record for a visible runtime unit, and keep it within this action-free
// observation pipeline. Never retain it after a move, cast, purchase or yield.
inline cambc::detail::camp_state observe(const camp& unit) {
    cambc::detail::camp_state value{};
    cambc::detail::check(cambc::detail::sdk_camp(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(unit)), &value));
    return value;
}
inline cambc::detail::champion_info observe(const champion& unit) {
    cambc::detail::champion_info value{};
    cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(unit)), &value));
    return value;
}
inline cambc::champion_draft pick(std::string_view s) {
    using D = cambc::champion_draft;
    if (s == "ren") return D::ren;
    if (s == "willow") return D::willow;
    if (s == "stellar") return D::stellar;
    if (s == "prism") return D::prism;
    if (s == "heart_of_ruin") return D::heart_of_ruin;
    if (s == "varg") return D::varg;
    if (s == "vendetta") return D::vendetta;
    if (s == "vesper_vale") return D::vesper_vale;
    if (s == "marcella") return D::marcella;
    if (s == "ryth") return D::ryth;
    return D::tavi;
}
inline Task choose(const Frame& f, State& s) {
    switch (f.design) {
    case Design::concentric: return choose_01(f, s);
    case Design::interior_lines: return choose_02(f, s);
    case Design::frontier: return choose_03(f, s);
    case Design::islander: return choose_04(f, s);
    case Design::forks: return choose_05(f, s);
    case Design::gatehouse: return choose_06(f, s);
    case Design::causeway: return choose_07(f, s);
    case Design::sanctuary: return choose_08(f, s);
    case Design::ports: return choose_09(f, s);
    case Design::migration: return choose_10(f, s);
    case Design::elastic_border: return choose_11(f, s);
    case Design::counterfront: return choose_12(f, s);
    case Design::encirclement: return choose_13(f, s);
    case Design::harvest_and_exit: return choose_14(f, s);
    case Design::reclamation: return choose_15(f, s);
    case Design::veil: return choose_16(f, s);
    }
    return fallback(f, s);
}
struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    float health = 0, incoming = 0;
    int home_distance = -1;
};

// Each process owns its observations, routes, inventory receipts and doctrine
// state. Draft memory carries only the fixed team size; there is no live mailbox.
class Bot {
    Design design_; bool control_;
    const std::array<const char*, 5>& order_;
    State state_;
    CompetitiveState competitive_;
    std::vector<Group> groups_;
    std::vector<vec2> altars_;
    std::vector<bitboard> altar_approaches_;
    bitboard all_altar_approaches_;
    bitboard terrain_, visible_;
    cambc::distance_field current_field_;
#if P7_REUSE_CURRENT_FIELD
    int current_field_root_ = -1;
    bool current_field_valid_ = false;
#endif
    cambc::distance_field movement_field_;
    std::optional<cambc::distance_field> home_field_;
    std::array<std::optional<cambc::distance_field>, 2> port_fields_;
    std::array<std::optional<cambc::distance_field>, 8> source_fields_;
#if P7_RECIPROCAL_SOURCE_FIELDS
    std::array<bool, 8> reciprocal_source_ready_{};
#endif
    std::array<std::array<int, 8>, 2> port_distances_{};
    std::array<std::array<int, 8>, 8> source_distances_{};
    std::array<int, 2> port_home_{};
    std::array<std::array<std::vector<int>, 2>, 8> exits_;
    std::array<NativeMobilityRoute, 8> mobile_in_, mobile_out_;
    std::array<int, 8> alternate_port_{}, connector_{}, opened_by_{};
    std::array<bool, 8> cut_{}, local_gate_{}, geometry_done_{};
    NativeNav nav_;
    std::vector<unsigned char> static_blocked_, holes_;
    bool initialized_ = false, recovering_ = false, submitting_ = false, capture_pending_ = false;
#if P7_RANGE_RECOVERY
    struct RecoveryCamp { unsigned id; RecoveryHazard hazard; };
    std::vector<RecoveryCamp> recovery_camps_;
    void remember_recovery_camps(const std::vector<camp>& camps) {
        // Monsters do not chase. Losing personal vision does not remove an
        // observed awake monster's attack envelope. This is a conservative
        // observation memory, never a hidden live-state query or team message.
        for (const auto& m : camps) {
            if (cambc::fuel_remaining() < 25000) return;
            const auto id = static_cast<unsigned>(static_cast<cambc::handle_camp_t>(m));
            auto found = std::find_if(recovery_camps_.begin(), recovery_camps_.end(),
                [id](const RecoveryCamp& c) { return c.id == id; });
            const auto state = m.state();
            if (!state.is_awake()) {
                if (found != recovery_camps_.end()) recovery_camps_.erase(found);
                continue;
            }
            const auto value = observe(m);
            if (value.health <= 0 || value.strength <= 0) {
                if (found != recovery_camps_.end()) recovery_camps_.erase(found);
                continue;
            }
            const RecoveryHazard h{value.x, value.y, m.attack_range(), value.strength * own_camp_factor()};
            if (found == recovery_camps_.end()) recovery_camps_.push_back({id, h});
            else found->hazard = h;
        }
    }
#endif
    bool is_willow_ = false;
    bool sample_turn_ = false;
    std::array<unsigned, 8> sample_fuel_{};
    int sample_goal_ = -1, sample_source_ = -1, sample_move_ = -1;
    int sample_proposal_goal_ = -1, sample_proposal_source_ = -1, sample_flags_ = 0, sample_proposal_flags_ = 0;
    int width_ = 0, height_ = 0, ordinal_ = 0, team_size_ = 3, warm_ = 0;
    int last_tile_ = -1, last_source_ = -1, observed_round_ = -1;
    int last_renewal_source_ = -1;
    int last_capture_source_ = -1;
    unsigned last_bag_ = 0, last_gold_ = 0, last_score_ = 0;
    unsigned strength_shards_ = 0, health_shards_ = 0, focus_shards_ = 0;
    unsigned next_purchase_cost_ = 0;
    float jungle_reduction_ = 0;
    Task committed_task_{};
    int commitment_until_ = -1;
    std::array<unsigned, 64> score_history_{};
    std::array<int, 64> history_round_{};
#if P7_LIVE_TRACE
    struct LivePacket {
        unsigned round = 0, actor = 0, completed_round = 0;
        int stage = 0, exit = 0, warm_before = 0, warm_after = 0;
        int ports = 0, sources = 0, proposal_goal = -1, proposal_source = -1;
        int task_goal = -1, task_source = -1, farm_source = -1, inspect = -1, x = -1, y = -1;
        std::array<int, 8> fuel{};
    } live_packet_;
    bool live_trace_active_ = false, live_trace_pending_ = false;
    void live_trace_flush() {
        // A packet captured below the logging floor waits for a fresh owner
        // turn. Logging is diagnostic and never supplies an action reserve.
        if (!live_trace_pending_ || cambc::fuel_remaining() < 90000) return;
        const auto& p = live_packet_;
        std::cout << "p7 live r=" << p.round << " actor=" << p.actor << " done=" << p.completed_round
                  << " stage=" << p.stage << " exit=" << p.exit
                  << " warm=" << p.warm_before << ':' << p.warm_after
                  << " fields=" << p.ports << ':' << p.sources << " fuel=";
        for (int value : p.fuel) std::cout << value << ',';
        std::cout << " proposal=" << p.proposal_goal << ':' << p.proposal_source
                  << " task=" << p.task_goal << ':' << p.task_source
                  << " farm=" << p.farm_source << ':' << p.inspect
                  << " pos=" << p.x << ':' << p.y << '\n';
        live_trace_pending_ = false;
    }
    void live_trace_begin(unsigned round) {
        live_trace_active_ = round == 16 || round == 32 || round == 64 || round == 128;
        // Preserve an unflushed packet instead of overwriting low-fuel evidence.
        if (live_trace_pending_) live_trace_active_ = false;
        if (!live_trace_active_) return;
        live_packet_ = {};
        live_packet_.fuel.fill(-1);
        live_packet_.round = round;
        live_packet_.actor = static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()));
        live_packet_.warm_before = warm_;
        live_packet_.fuel[0] = cambc::fuel_remaining();
    }
    void live_trace_mark(int stage, int slot = -1, int exit = 0) {
        if (!live_trace_active_) return;
        live_packet_.stage = stage;
        live_packet_.exit = exit;
        if (slot >= 0) live_packet_.fuel[slot] = cambc::fuel_remaining();
    }
    void live_trace_end() {
        if (!live_trace_active_) return;
        const auto remaining = cambc::fuel_remaining();
        live_packet_.fuel[7] = remaining;
        live_packet_.warm_after = warm_;
        if (remaining >= 1000) {
            live_packet_.completed_round = cambc::round_number();
            for (unsigned p = 0; p < port_fields_.size(); ++p) if (port_fields_[p]) live_packet_.ports |= 1 << p;
            for (unsigned i = 0; i < source_fields_.size(); ++i) if (source_fields_[i]) live_packet_.sources |= 1 << i;
            const auto position = cambc::self().position();
            live_packet_.x = position.x; live_packet_.y = position.y;
        }
        live_trace_active_ = false; live_trace_pending_ = true;
    }
    struct LiveTraceScope { Bot& bot; ~LiveTraceScope() { bot.live_trace_end(); } };
#endif
#if P7_PHASE_TRACE
    struct PhaseJournal {
        // Memory-only records. A completion event orders C++ work; it does not
        // assert that work completed in its entry round. Only clock records do.
        std::array<std::uint64_t, 64> events{};
        unsigned size = 0, entry_round = 0, actor = 0, flags = 0, attempts = 0;
        bool active = false, pending = false, armed = false;
    } phase_;
    void phase_event(unsigned kind, unsigned id, unsigned value = 0) {
        if (!phase_.active) return;
        if (phase_.size == phase_.events.size()) {
            phase_.flags |= 2; // Overflow invalidates attribution; keep prefix.
            phase_.active = false; phase_.pending = true; return;
        }
        phase_.events[phase_.size++] = (static_cast<std::uint64_t>(kind) << 56) |
            (static_cast<std::uint64_t>(id) << 40) | value;
    }
    void phase_begin(unsigned round, unsigned actor) {
        if (!initialized_ || phase_.armed || round != 19 || actor != 2) return;
        phase_.armed = phase_.active = true;
        phase_.entry_round = round; phase_.actor = actor;
        phase_event(3, 1, round);
    }
    unsigned phase_clock(unsigned value, unsigned site) {
        // The caller passes an existing production query's result exactly
        // once. Do not read another SDK clock or fuel value here.
        phase_event(3, site, value);
        if (phase_.active && value != phase_.entry_round) {
            phase_.flags |= 1;
            phase_.active = false; phase_.pending = true;
        }
        return value;
    }
    std::uint64_t phase_fuel(std::uint64_t value, unsigned site) {
        phase_event(4, site, static_cast<unsigned>(value));
        return value;
    }
    void phase_finish(unsigned flag = 4) {
        if (!phase_.active) return;
        phase_.flags |= flag; // 4 normal end, 8 caught exception; no clock claim.
        phase_.active = false; phase_.pending = true;
    }
    bool phase_flush() {
        if (!phase_.pending) return false;
        // One attempt, not one successful write. The >=90k policy is measured
        // separately; it is not a proof that arbitrary logging is safe.
        if (cambc::fuel_remaining() < 90000) return true;
        ++phase_.attempts; phase_.pending = false;
        const auto emitted_round = cambc::round_number();
        char buffer[1200]; unsigned used = 0;
        auto text = [&](const char* s) { while (*s) buffer[used++] = *s++; };
        auto number = [&](unsigned n) {
            char digits[10]; unsigned count = 0;
            do { digits[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n);
            while (count) buffer[used++] = digits[--count];
        };
        text("p7 phase entry="); number(phase_.entry_round);
        text(" actor="); number(phase_.actor); text(" emit="); number(emitted_round);
        text(" flags="); number(phase_.flags); text(" attempts="); number(phase_.attempts);
        text(" count="); number(phase_.size); text(" events=");
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < phase_.size; ++i) {
            const auto value = phase_.events[i];
            for (int shift = 60; shift >= 0; shift -= 4) buffer[used++] = hex[(value >> shift) & 15];
        }
        buffer[used++] = '\n';
        // 64*16 hex bytes plus <128 header bytes, bounded by this fixed buffer.
        std::cout.write(buffer, used);
        return true;
    }
    struct PhaseScope {
        Bot& bot; unsigned id;
        PhaseScope(Bot& b, unsigned p) : bot(b), id(p) { bot.phase_event(1, id); }
        ~PhaseScope() { bot.phase_event(2, id); }
    };
#endif
    static bool navigation_fuel() { return cambc::fuel_remaining() > 55000; }
    float own_camp_factor() const { return own_camp_damage_factor(health_shards_, jungle_reduction_); }
    static constexpr std::array<vec2, 8> directions{
        vec2{-1,-1}, vec2{0,-1}, vec2{1,-1}, vec2{-1,0}, vec2{1,0}, vec2{-1,1}, vec2{0,1}, vec2{1,1}};
    int tile(vec2 p) const { return p.y * width_ + p.x; }
    vec2 point(int i) const { return {i % width_, i / width_}; }
    int distance(const cambc::distance_field& field, vec2 p) const {
        const auto d = field.get(p);
        return d == cambc::distance_field::unreachable ? -1 : static_cast<int>(d);
    }
    int distance(const cambc::distance_field& field, const bitboard& b) const {
        int best = -1;
        for (auto p : b.positions()) { int d = distance(field, p); if (d >= 0 && (best < 0 || d < best)) best = d; }
        return best;
    }
    int ordinal(champion actor) const {
        const auto name = cambc::to_string(actor.champion_name());
        for (int i = 0; i < 5; ++i) if (name == std::string_view(order_[i])) return i;
        return 0;
    }
    bitboard port_goals(int p) const {
        if (p < 0 || p >= static_cast<int>(altar_approaches_.size())) return {};
        return altar_approaches_[p];
    }
    void compute(cambc::distance_field& field, const bitboard& roots) {
        bitboard visited;
        // An empty destination set in compute_to is already satisfied: it does
        // not request a complete field. These fields serve many later queries.
        field.compute(terrain_, roots & ~terrain_, visited, 256);
    }
#if P7_RECIPROCAL_SOURCE_FIELDS
    bool reciprocal_sources_ready() const {
        // Islander also asks its current-root field about a mobility ingress
        // outside the source component. Keep that independent witness intact.
        if (design_ == Design::islander) return false;
        for (int i = 0; i < static_cast<int>(groups_.size()); ++i)
            if (!source_fields_[i] || !reciprocal_source_ready_[i]) return false;
        return true;
    }
    int reciprocal_source_distance(int source, vec2 origin) const {
        // The fixed terrain has undirected eight-neighbour edges. A complete
        // source-root field gives the same capped distance as taking the
        // minimum current-root distance over that source's spawn tiles.
        if (reciprocal_sources_ready()) return distance(*source_fields_[source], origin);
        return distance(current_field_, groups_[source].spawns);
    }
#endif
#if P7_REUSE_CURRENT_FIELD
    void prepare_current_field(vec2 origin) {
        // Only this static field is retained: fixed terrain/cap and the exact
        // requested root. Live observations and movement obstacles stay fresh.
        const int requested = tile(origin);
        if (current_field_valid_ && current_field_root_ == requested) return;
        // A failed SDK write may partially replace the old buffer. Publish
        // validity only after success, under the captured pre-compute root.
        current_field_valid_ = false;
        compute(current_field_, around(origin, 0));
        current_field_root_ = requested;
        current_field_valid_ = true;
    }
#endif
    static void prepare_budget() {
        // Only immutable map preparation may span owner turns. Never resume a
        // live observation/action pipeline with locals from an earlier round.
        if (cambc::fuel_remaining() < 65000) cambc::end_turn();
    }
#if P7_STARTUP_TRACE
    static void startup_marker(int phase) {
        std::cout << "p7 startup phase=" << phase << " r=" << cambc::round_number()
                  << " fuel=" << cambc::fuel_remaining() << '\n';
    }
#endif
    void initialize() {
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
#endif
#if P7_STARTUP_TRACE
        startup_marker(1);
#endif
        const auto jungle = cambc::item_type::jungle_emblem_of_health.info();
        if (jungle.passive == cambc::item_passive::jungle && std::isfinite(jungle.passive_magnitude) &&
            jungle.passive_magnitude >= 0 && jungle.passive_magnitude <= 1)
            jungle_reduction_ = jungle.passive_magnitude;
        width_ = cambc::map::width(); height_ = cambc::map::height();
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(team::enemy);
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
#endif
#if P7_STARTUP_TRACE
        startup_marker(2);
#endif
        static_blocked_.assign(width_ * height_, 0); holes_.assign(width_ * height_, 0);
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
        unsigned prepared_tiles = 0;
        for (auto p : terrain_.positions()) {
            if ((prepared_tiles++ & 63u) == 0) prepare_budget();
            static_blocked_[tile(p)] = 1;
        }
        prepare_budget();
#else
        for (auto p : terrain_.positions()) static_blocked_[tile(p)] = 1;
#endif
#if P7_STARTUP_TRACE
        startup_marker(3);
#endif
#if P7_BOUNDED_INITIALIZATION
        prepared_tiles = 0;
        for (auto p : cambc::map::holes().positions()) {
            if ((prepared_tiles++ & 63u) == 0) prepare_budget();
            holes_[tile(p)] = 1;
        }
        prepare_budget();
#else
        for (auto p : cambc::map::holes().positions()) holes_[tile(p)] = 1;
#endif
#if P7_STARTUP_TRACE
        startup_marker(4);
#endif
        nav_.init(width_, height_, static_blocked_);
        nav_.set_guard(navigation_fuel);
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
#endif
#if P7_STARTUP_TRACE
        startup_marker(5);
#endif
        alternate_port_.fill(-1); connector_.fill(-1); opened_by_.fill(-1);
        for (auto& row : port_distances_) row.fill(-1);
        for (auto& row : source_distances_) row.fill(-1);
        port_home_.fill(-1);
        ordinal_ = ordinal(cambc::self());
        is_willow_ = std::string_view(order_[ordinal_]) == "willow";
        if (cambc::draft_memory.size() >= 2 && cambc::draft_memory[0] == std::byte{7})
            team_size_ = static_cast<int>(cambc::draft_memory[1]);
        altars_ = cambc::map::altars().positions();
        // The two closest public altar approaches are represented; other legal
        // altars remain available for emergency banking but prohibit denial.
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
#endif
#if P7_STARTUP_TRACE
        startup_marker(6);
#endif
        home_field_.emplace(); compute(*home_field_, cambc::map::spawn_region(team::ally));
#if P7_BOUNDED_INITIALIZATION
        prepare_budget();
#endif
#if P7_STARTUP_TRACE
        startup_marker(7);
#endif
        // Query each immutable approach once. Re-querying inside the sort can
        // consume several turns on maps with many altars.
        std::vector<std::pair<vec2, int>> altar_order;
        for (auto p : altars_) {
            prepare_budget();
            int d = distance(*home_field_, around(p, cambc::constants::submit_gems_range) & ~terrain_);
            altar_order.emplace_back(p, d < 0 ? 100000 : d);
        }
        std::sort(altar_order.begin(), altar_order.end(), [&](const auto& a, const auto& b) {
            return a.second != b.second ? a.second < b.second : tile(a.first) < tile(b.first);
        });
        for (std::size_t i = 0; i < altar_order.size(); ++i) altars_[i] = altar_order[i].first;
        // These legal approach tiles depend only on the immutable map and
        // queried submission range. Live locks and occupants remain fresh.
        for (const auto& altar : altars_) {
            prepare_budget();
            altar_approaches_.push_back(around(altar, cambc::constants::submit_gems_range) & ~terrain_);
            all_altar_approaches_ |= altar_approaches_.back();
        }
        std::vector<Group> all;
        for (auto g : cambc::map::camp_groups()) {
            prepare_budget();
            auto members = g.camps(); if (members.empty()) continue;
            Group x{g, g.spawn_tiles(), members.front().spawn_position()};
            for (const auto& m : members) { x.health += m.base_stats().base_health; x.incoming += m.base_stats().base_strength; }
            x.home_distance = distance(*home_field_, x.spawns);
            all.push_back(std::move(x));
        }
#if P7_STARTUP_TRACE
        startup_marker(8);
#endif
        std::vector<CompetitiveSource> candidates;
        for (const auto& g : all) candidates.push_back({static_cast<int>(static_cast<cambc::handle_camp_group_t>(g.group)), g.home_distance, static_cast<int>(g.group.gem_value()), tile(g.anchor)});
        // The complete candidate list is immutable public geometry. Give its
        // bounded diversity selection a fresh preparation turn, rather than
        // spending the tail left by camp metadata collection.
        if (cambc::fuel_remaining() < 95000) cambc::end_turn();
        const auto selected = competitive_shortlist(candidates, design_, ordinal_, width_, team_size_);
        groups_.reserve(selected.size());
        for (int i : selected) { prepare_budget(); groups_.push_back(std::move(all[i])); }
        history_round_.fill(-1); initialized_ = true;
#if P7_STARTUP_TRACE
        startup_marker(9);
#endif

    }
    // A preparation turn builds one immutable field and its reusable queries.
    // It ends before live observations/actions, preserving their fuel budget.
    bool warm() {
        P7_PHASE_SCOPE(4);
        if (P7_PHASE_FUEL(cambc::fuel_remaining(), 3) < 65000) return false;
        if (warm_ < 2) {
            int p = warm_++;
            if (p < static_cast<int>(altars_.size())) {
                port_fields_[p].emplace(); compute(*port_fields_[p], port_goals(p));
                port_home_[p] = distance(*port_fields_[p], cambc::map::spawn_region(team::ally));
                for (int i = 0; i < static_cast<int>(groups_.size()); ++i)
                    port_distances_[p][i] = distance(*port_fields_[p], groups_[i].spawns);
            }
#if P7_LIVE_TRACE
            live_trace_mark(3, -1, 7);
#endif
            return true;
        } else if (warm_ - 2 < static_cast<int>(groups_.size())) {
            int i = warm_++ - 2; source_fields_[i].emplace(); compute(*source_fields_[i], groups_[i].spawns);
            for (int j = 0; j < static_cast<int>(groups_.size()); ++j)
                source_distances_[i][j] = distance(*source_fields_[i], groups_[j].spawns);
#if P7_RECIPROCAL_SOURCE_FIELDS
            // An exception or suspended partial write cannot publish readiness.
            // All immutable preparation remains separate from live work.
            reciprocal_source_ready_[i] = true;
#endif
#if P7_LIVE_TRACE
            live_trace_mark(3, -1, 7);
#endif
            return true;
        }
        return false;
    }
    void shop() {
        P7_PHASE_SCOPE(3);
        auto& me = cambc::self();
        using I = cambc::item_type;
        const auto kind = me.champion_name();
        const bool caster = kind != cambc::champion_type::vendetta && kind != cambc::champion_type::varg &&
            kind != cambc::champion_type::vesper_vale &&
            (me.focus() > me.strength() * 1.4f || kind == cambc::champion_type::willow);
        // Spend at each policy boundary in one batch, so a healthy departure
        // does not strand earned gold after buying only one shard.
        for (int stage = 0; stage < 4; ++stage) {
            const unsigned attack = strength_shards_ + focus_shards_;
            const bool health = attack >= 5 && health_shards_ < 5;
            const bool power = attack >= 5 && health_shards_ >= 5;
            I item = health ? I::jungle_shard_of_health : power ?
                (caster ? I::power_shard_of_focus : I::power_shard_of_strength) :
                (caster ? I::monster_shard_of_focus : I::monster_shard_of_strength);
            const auto cost = item.cost(); next_purchase_cost_ = cost; if (!cost) break;
            unsigned count = me.gold_count() / cost;
            if (health) count = std::min(count, 5 - health_shards_);
            else if (!power) count = std::min(count, (attack < 5 ? 5u : 10u) - attack);
            if (!count || !me.can_buy(item, count)) break;
            me.buy(item, count);
            if (health) health_shards_ += count;
            else if (caster) focus_shards_ += count; else strength_shards_ += count;
            if (power) break;
        }
        // can_buy enforces actual shop access, purchasability and gold.
        if (design_ == Design::islander && !me.consumables()[cambc::consumable_type::tp_crystal].count()
            && me.can_buy(cambc::consumable_type::tp_crystal)) me.buy(cambc::consumable_type::tp_crystal);
    }
    bitboard blocked() const {
        auto b = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        b.set(cambc::self().position(), false); return b;
    }
    bool exclusive(const cambc::ability& a) const {
        auto info = a.info();
        return info && std::visit([](const auto& x) { return x.exclusive_with_movement; }, *info);
    }
    template<class Target> bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        const unsigned reserve = std::is_same_v<Target, champion> ? 30000 : 40000;
        if (cambc::fuel_remaining() < reserve) return false;
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        for (const auto& a : me.abilities()) {
            if (cambc::fuel_remaining() < 12000) return acted;
            if (!me.can_cast() || !a.is_ready() || (walked && exclusive(a))) continue;
            if (!(a.is_damage() || a.is_cc() || a.is_aoe_damage())) continue;
            if (!a.valid_tiles().is_set(target.position())) continue;
            bool entity = false;
            if constexpr (std::is_same_v<Target, camp>) entity = a.can_target_camps();
            else entity = a.can_target_enemies();
            if (entity) acted = a.use(target);
            else if (a.can_target_ground()) acted = a.use(target.position());
            if (acted || !valid()) break;
        }
        if (!valid() || !me.attack_tiles().is_set(target.position())) return acted;
        if (dangerous) for (const auto& a : me.abilities())
            if (me.can_cast() && a.is_ready() && a.is_shield() && a.can_target_self() && !(walked && exclusive(a))) {
                acted |= a.use(); break;
            }
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        if (!me.can_attack()) for (const auto& a : me.abilities())
            if (me.can_cast() && a.is_ready() && a.is_attack_reset() && a.can_target_self() && !(walked && exclusive(a))) {
                if (a.use()) { acted = true; if (me.can_attack() && valid() && me.attack_tiles().is_set(target.position())) me.attack(target); }
                break;
            }
        return acted;
    }
    // Route against current occupants. The planning fields intentionally ignore
    // camps, but an executable route cannot walk through a living monster or
    // oscillate around it using geometric distance alone.
    bool move_to(bitboard goals) {
        P7_PHASE_SCOPE(15);
#if P7_LIVE_TRACE
        live_trace_mark(13);
#endif
        if (sample_turn_) { sample_fuel_[7] = cambc::fuel_remaining(); sample_move_ = 0; }
        auto& me = cambc::self(); if (!me.can_move() || goals.is_set(me.position())) return false;
        for (int retry = 0; retry < 3 && me.can_move(); ++retry) {
            if (P7_PHASE_FUEL(cambc::fuel_remaining(), 4) < 25000) {
#if P7_LIVE_TRACE
                live_trace_mark(13, -1, 42);
#endif
                if (sample_turn_) sample_move_ = 1; return false;
            }
            P7_PHASE_ENTER(16);
            auto walls = blocked(); auto free_goals = goals & ~walls;
            P7_PHASE_LEAVE(16);
            if (free_goals.is_empty()) { if (sample_turn_) sample_move_ = 2; return false; }
            bitboard visited;
            P7_PHASE_ENTER(17);
            movement_field_.compute_to(walls, free_goals, around(me.position(), 0), visited);
            P7_PHASE_LEAVE(17);
            P7_PHASE_ENTER(18);
            auto delta = movement_field_.next_move_from(me.position());
            P7_PHASE_LEAVE(18);
            if (!delta) { if (sample_turn_) sample_move_ = 3; return false; }
            auto next = me.position() + *delta;
#if P7_PHASE_TRACE
            P7_PHASE_ENTER(19);
            const bool moved = !blocked().is_set(next) && me.move_absolute(next);
            P7_PHASE_LEAVE(19);
            if (moved) { if (sample_turn_) sample_move_ = 4; return true; }
#else
            if (!blocked().is_set(next) && me.move_absolute(next)) { if (sample_turn_) sample_move_ = 4; return true; }
#endif
        }
        return false;
    }
    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies, bool refresh = false) {
        P7_PHASE_SCOPE(6);
        if (cambc::fuel_remaining() < (refresh ? 35000 : 20000)) return false;
        // A ward replacement or failed route attempt can change visibility.
        // Refresh before reading units retained by the earlier frame.
        const auto refreshed_camps = refresh ? cambc::vision::camps() : std::vector<camp>{};
        const auto refreshed_enemies = refresh ? cambc::vision::champions(team::enemy) : std::vector<champion>{};
        const auto& current_camps = refresh ? refreshed_camps : camps;
        const auto& current_enemies = refresh ? refreshed_enemies : enemies;
        auto& me = cambc::self(); bool safe = true; auto origin = me.position();
        if (cambc::map::spawn_region(team::ally).is_set(origin)) return false;
#if P7_RANGE_RECOVERY
        if (refresh) remember_recovery_camps(current_camps);
        std::vector<RecoveryHazard> hazards;
        for (const auto& c : recovery_camps_) hazards.push_back(c.hazard);
        for (const auto& e : current_enemies) {
            const auto value = observe(e);
            if (value.x >= 0 && value.y >= 0 &&
                cambc::champion_state::from_bits_retain(value.state).contains(cambc::champion_state::alive))
                hazards.push_back({value.x, value.y, 7, value.strength + 100});
        }
        const auto obstacles = blocked();
        // One compact snapshot avoids a host query for every search edge. The
        // snapshot is used only before this turn's first recovery action.
        const auto occupancy = obstacles.export_linear_buffer();
        const auto route = recovery_step(origin.x, origin.y, width_, height_, hazards,
            [&](int x, int y) { const int i = y * width_ + x; return (occupancy[i >> 3] >> (i & 7)) & 1; },
            [] { return cambc::fuel_remaining() >= 25000; });
        if (route.complete) {
            const vec2 next{route.x, route.y};
            if (next == origin) { me.recall(); return true; }
            if (me.can_move() && me.move_absolute(next)) return true;
            return false;
        }
        // A cutoff never permits recall and must leave an ordinary move reserve.
        // Prefer less incoming damage, then more clearance from the nearest
        // recovery envelope. Do not invoke another full field after a cutoff.
        vec2 best = origin; float best_damage = INFINITY; int best_margin = -100000;
        for (const auto p : obstacles.reachable_tiles(origin, 1).positions()) {
            float damage = 0; int margin = 100000;
            for (const auto& h : hazards) {
                const int d = separation(p, {h.x, h.y});
                if (d <= h.range) damage += h.damage;
                margin = std::min(margin, d - h.range - 1);
            }
            if (damage < best_damage || (damage == best_damage && margin > best_margin)) {
                best_damage = damage; best_margin = margin; best = p;
            }
        }
        return best != origin && me.can_move() && me.move_absolute(best);
#else
        struct Threat { vec2 position; float damage; };
        std::vector<Threat> monsters, opponents;
        for (const auto& m : current_camps) if (m.is_alive() && m.is_awake()) {
            if (cambc::fuel_remaining() < 12000) return false;
            const float damage = m.strength(); if (damage <= 0) continue;
            const auto position = m.position(); monsters.push_back({position, damage * own_camp_factor()});
            if (separation(origin, position) <= m.attack_range() + 1) safe = false;
        }
        for (const auto& e : current_enemies) {
            const auto value = observe(e);
            if (value.x < 0 || value.y < 0 ||
                !cambc::champion_state::from_bits_retain(value.state).contains(cambc::champion_state::alive)) continue;
            const vec2 position{value.x, value.y}; opponents.push_back({position, value.strength});
            if (separation(origin, position) <= 8) safe = false;
        }
        if (safe) { me.recall(); return true; }
        auto best = origin; float score = -1e30f;
        const auto legal = blocked().reachable_tiles(origin, 1).positions();
        for (auto p : legal) {
            float n = 0;
            for (const auto& m : monsters) n += std::min(6, separation(p, m.position)) * m.damage;
            for (const auto& e : opponents) n += std::min(9, separation(p, e.position)) * (e.damage + 100);
            if (n > score) { score = n; best = p; }
        }
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        return move_to(cambc::map::spawn_region(team::ally));
#endif
    }
    bool bank(int port) {
        P7_PHASE_SCOPE(7);
        if (cambc::fuel_remaining() < 20000) return false;
        auto& me = cambc::self(); if (!me.gem_count()) return false;
        const auto origin = me.position();
        const auto locks = cambc::vision::altar_locks(team::enemy);
        if (locks.empty()) {
            for (int i = 0; i < static_cast<int>(altars_.size()); ++i) {
                if (port >= 0 && i != port) continue;
                if (separation(origin, altars_[i]) <= cambc::constants::submit_gems_range) {
                    me.submit_gems(altars_[i]); submitting_ = me.is_submitting_gems(); state_.bank_pending = submitting_; return true;
                }
            }
            if (move_to(port >= 0 ? port_goals(port) : all_altar_approaches_)) return true;
            return port >= 0 ? bank(-1) : false;
        }
        bitboard open_goals, locked_goals;
        for (int i = 0; i < static_cast<int>(altars_.size()); ++i) {
            if (port >= 0 && i != port) continue;
            auto lock = std::find_if(locks.begin(), locks.end(), [&](const auto& x) { return x.position() == altars_[i]; });
            if (lock == locks.end()) {
                if (separation(origin, altars_[i]) <= cambc::constants::submit_gems_range) {
                    me.submit_gems(altars_[i]); submitting_ = me.is_submitting_gems(); state_.bank_pending = submitting_; return true;
                }
                open_goals |= port_goals(i);
            } else {
                if (me.can_attack() && me.attack_tiles().is_set(lock->position())) { me.attack(*lock); return true; }
                locked_goals |= port_goals(i);
            }
        }
        if (!open_goals.is_empty() && move_to(open_goals)) return true;
        if (!locked_goals.is_empty() && move_to(locked_goals)) return true;
        return port >= 0 ? bank(-1) : false;
    }
    std::vector<int> path(const cambc::distance_field& field, int from) const {
        std::vector<int> out; int d = distance(field, point(from));
        if (d < 0 || d > 256) return out;
        out.push_back(from);
        while (d > 0) {
            if (!navigation_fuel()) return {};
            auto delta = field.next_move_from(point(out.back()));
            if (!delta) return {};
            out.push_back(tile(point(out.back()) + *delta)); --d;
        }
        return out;
    }
    void geometry(Frame& f) {
        P7_PHASE_SCOPE(11);
        if (f.source_count == 0) return;
        // A cached exit's port label must be final before retaining the route.
        for (int p = 0; p < f.port_count; ++p) if (!port_fields_[p]) return;
        // Proof work is cached and attempted for one source per turn. A guard
        // cutoff leaves the mechanism unknown and the ordinary policy active.
        int i = f.round % f.source_count; auto& x = f.sources[i];
        if (x.port >= 0 && port_fields_[x.port] && exits_[i][0].empty())
            exits_[i][0] = path(*port_fields_[x.port], x.tile);
        if (!geometry_done_[i] && navigation_fuel() && !exits_[i][0].empty()) {
            const auto& first = exits_[i][0];
            for (int p : first) {
                if (!navigation_fuel()) break;
                int degree = static_cast<int>((around(point(p), 1) & ~terrain_).count()) - 1;
                if (degree <= 3 && p != first.front() && p != first.back()) { local_gate_[i] = true; connector_[i] = p; break; }
            }
            if (design_ == Design::forks && f.port_count == 2) {
                int other = 1 - x.port;
                if (port_fields_[other]) {
                    auto candidate = path(*port_fields_[other], x.tile);
                    if (candidate.size() >= 3 && first.size() >= 3 && candidate.back() != first.back()) {
                        std::vector<unsigned char> excluded(width_ * height_, 0);
                        for (std::size_t n = 1; n + 1 < first.size(); ++n) excluded[first[n]] = 1;
                        auto second = nav_.route(x.tile, candidate.back(), &excluded, width_ * height_);
                        if (second.size() >= 3 && second != first) { exits_[i][1] = std::move(second); alternate_port_[i] = other; }
                        if (nav_.last_complete) geometry_done_[i] = true;
                    }
                }
            } else if (design_ == Design::causeway || design_ == Design::gatehouse) {
                if (navigation_fuel()) {
                    // The SDK's full static field gives the exact connected
                    // component without allocating and scanning a second C++
                    // graph in the live action budget. No distance cap here.
                    auto removed = terrain_ | groups_[i].spawns;
                    bitboard reached; cambc::distance_field test;
                    test.compute(removed, cambc::map::spawn_region(team::ally) & ~removed, reached);
                    for (int j = 0; j < f.source_count; ++j) if (j != i && f.sources[j].home_connected) {
                        bool lost = (groups_[j].spawns & reached).is_empty();
                        if (lost) { cut_[i] = true; opened_by_[j] = i; connector_[i] = x.tile; }
                    }
                    geometry_done_[i] = true;
                }
            } else geometry_done_[i] = true;
        }
        if (design_ == Design::islander && !x.home_connected && f.spawn >= 0 && navigation_fuel()) {
            // A conservative supported circuit crosses exactly one hole each
            // way, using an owned dash. Consumables are only safe tactical
            // substitutes; one crystal is never credited for two crossings.
            bool owns_dash = false;
            for (const auto& a : cambc::self().abilities()) owns_dash |= a.is_dash();
            int approach = -1;
            for (auto p : (around(groups_[i].anchor, 1) & ~terrain_ & ~groups_[i].spawns).positions())
                if (approach < 0 || separation(p, point(f.spawn)) < separation(point(approach), point(f.spawn))) approach = tile(p);
            if (owns_dash && approach >= 0) {
                if (mobile_in_[i].tiles.empty()) mobile_in_[i] = nav_.mobility_route(f.spawn, approach, holes_, 2, width_ * height_);
                else if (mobile_out_[i].tiles.empty() && f.port_count > 0) {
                    int terminal = -1; for (auto p : port_goals(0).positions()) if (terminal < 0 || separation(p, point(approach)) < separation(point(terminal), point(approach))) terminal = tile(p);
                    if (terminal >= 0) mobile_out_[i] = nav_.mobility_route(approach, terminal, holes_, 2, width_ * height_);
                }
            }
        }
        for (int j = 0; j < f.source_count; ++j) {
            auto& s = f.sources[j]; s.connector = connector_[j]; s.cut_proven = cut_[j]; s.local_gate = local_gate_[j]; s.opened_by = opened_by_[j];
            s.redundant = !exits_[j][0].empty() && !exits_[j][1].empty() && alternate_port_[j] >= 0 && alternate_port_[j] != s.port;
            s.alternate_port = alternate_port_[j];
            if (s.redundant && s.pressured) {
                const int replacement = static_cast<int>(exits_[j][1].size()) - 1;
                const int change = replacement - s.exit_distance;
                s.exit_distance = replacement;
                if (s.cycle_turns > 0) {
                    s.cycle_turns += change;
                    for (int scenario = 0; scenario < 3; ++scenario)
                        s.score[scenario] = s.cycle_turns <= f.windows[scenario] ? static_cast<int>(f.bag) + s.payout : 0;
                }
            }
            if (s.opened_by >= 0) {
                int gate = s.opened_by;
                auto spawns = groups_[gate].spawns;
                bool clear = (spawns & ~visible_).is_empty() && (spawns & (cambc::vision::camp_tiles() | cambc::vision::champion_tiles() | cambc::vision::teleport_tiles())).is_empty();
                if (clear && source_fields_[j]) {
                    auto access = path(*source_fields_[j], f.tile);
                    clear = !access.empty(); auto occupied = blocked();
                    for (int p : access) if (!groups_[j].spawns.is_set(point(p)) && (!visible_.is_set(point(p)) || occupied.is_set(point(p)))) clear = false;
                } else clear = false;
                if (!clear) s.feasible = s.joint_feasible = false;
            }
            if (design_ == Design::islander && mobile_in_[j].crossings == 1 && mobile_out_[j].crossings == 1 &&
                mobile_in_[j].complete && mobile_out_[j].complete && !mobile_in_[j].tiles.empty() && !mobile_out_[j].tiles.empty()) {
                s.mobility = true; s.reachable = true; s.port = 0;
                int ingress = static_cast<int>(mobile_in_[j].tiles.size()) - 1;
                int exit = static_cast<int>(mobile_out_[j].tiles.size()) - 1;
                int cooldown = 0; for (const auto& a : cambc::self().abilities()) if (a.is_dash() && a.info()) cooldown = std::max(cooldown, std::visit([](const auto& z) { return z.max_cooldown; }, *a.info()));
                s.distance = ingress; s.exit_distance = exit; s.recovery_distance = ingress;
                int work = static_cast<int>(std::ceil(s.health / std::max(1.f, f.damage))) + cambc::constants::capture_time + 1;
                int tribute = std::max(cambc::constants::submit_gem_minimum_duration, static_cast<int>(std::ceil((f.bag + s.gems) * cambc::constants::submit_gem_duration_multiplier)) + cambc::constants::submit_gem_base_duration);
                s.cycle_turns = ingress + work + exit + tribute + ingress + cooldown * 2 + cambc::constants::recall_time + 4;
                s.feasible = s.seen && !s.respawning && f.health > self_camp_incoming(f, s) * (work + 2) + f.max_health * .2f;
                for (int scenario = 0; scenario < 3; ++scenario) s.score[scenario] = s.cycle_turns <= f.windows[scenario] ? static_cast<int>(f.bag) + s.payout : 0;
            }
            // Occlusion is a personally observed bush approach plus the actor's
            // own ward on a currently visible retained exit. It gives no camp
            // immunity and no claim about the opponent's unseen wards.
            if (design_ == Design::veil && s.seen && !exits_[j][0].empty()) {
                bool connected = false, covered = false;
                auto bushes = cambc::map::bushes();
                const auto& route = exits_[j][0];
                auto here = std::find(route.begin(), route.end(), f.tile);
                if (here != route.end() && bushes.is_set(point(*here))) {
                    auto exit = here;
                    while (exit + 1 != route.end() && bushes.is_set(point(*(exit + 1))) &&
                        std::abs(point(*exit).x - point(*(exit + 1)).x) + std::abs(point(*exit).y - point(*(exit + 1)).y) == 1) ++exit;
                    if (exit + 1 != route.end() && !bushes.is_set(point(*(exit + 1))) && visible_.is_set(point(*(exit + 1)))) {
                        connected = true;
                        for (auto w : cambc::self().ward_positions())
                            if (std::abs(w.x - point(*(exit + 1)).x) + std::abs(w.y - point(*(exit + 1)).y) <= 1) covered = true;
                    }
                }
                s.occluded = connected && covered;
            }
        }
    }
    bool follow(const std::vector<int>& route, bool mobile) {
        auto& me = cambc::self(); int here = tile(me.position());
        auto it = std::find(route.begin(), route.end(), here);
        if (it == route.end() || it + 1 == route.end()) return false;
        vec2 next = point(*(it + 1));
        if (blocked().is_set(next)) return false;
        if (separation(me.position(), next) == 1) return me.can_move() && me.move_absolute(next);
        if (!mobile || !visible_.is_set(next)) return false;
        // A retained mobility witness contains an actual two-step hole path.
        // Recheck the intermediate tile and all dynamic landing restrictions.
        bool legal_path = false;
        for (auto delta : directions) {
            auto p = me.position() + delta;
            if (p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_ && holes_[tile(p)] && separation(p, next) == 1) legal_path = true;
        }
        if (!legal_path) return false;
        for (const auto& a : me.abilities()) if (a.is_dash() && a.is_ready() && a.can_target_ground() && a.valid_tiles().is_set(next) && me.can_cast()) return a.use(next);
        // A crystal is consumed only when the dash is unavailable, the landing
        // remains valid, and the current observed position is safe to channel.
        bool safe = true; for (const auto& e : cambc::vision::champions(team::enemy)) if (separation(e.position(), me.position()) <= 8) safe = false;
        for (const auto& m : cambc::vision::camps()) if (m.is_alive() && m.is_awake() && separation(m.position(), me.position()) <= m.attack_range()) safe = false;
        if (safe && me.consumables()[cambc::consumable_type::tp_crystal].count()) return me.consumables()[cambc::consumable_type::tp_crystal].use(next);
        return true; // Wait at the witnessed fork for the owned dash cooldown.
    }
    void ward(const Task& task, const Frame& f) {
        auto& me = cambc::self();
        int source = valid_source(f, task.source) ? task.source : state_.source;
        if ((!task.ward && design_ != Design::veil) || !f.own_ward_ready || !me.can_move() || !valid_source(f, source) ||
            cambc::constants::active_wards == 0) return;
        const auto& route = exits_[source][0];
        auto legal = me.ward_placement_tiles() & visible_ & ~blocked() & ~cambc::vision::ward_tiles(team::ally);
        auto existing = me.ward_positions();
        for (auto it = route.rbegin(); it != route.rend(); ++it) if (legal.is_set(point(*it))) {
            if (cambc::fuel_remaining() < 22000) return;
            if (!existing.empty() && separation(existing.front(), point(*it)) < 3) return;
            me.place_ward(point(*it)); return;
        }
    }
    void denial(Frame& f, const std::vector<champion>& enemies) {
        if (design_ != Design::encirclement || control_ || f.ordinal >= 2 || f.bag ||
            f.safe_capture || f.altar_count > 2 || !cambc::self().can_cast()) return;
        const int independent = best(f);
        // Narrow, executable interruption hypothesis: an observed current
        // tribute can be stunned now, and even the universal fastest restart
        // misses every represented horizon. Fighting, waiting and another altar
        // all require a fresh tribute; no terrain exclusion is inferred.
        for (const auto& e : enemies) {
            if (!e.is_visible() || !e.is_alive() || !e.is_submitting_gems() || !e.gem_count() || e.champion_name() == cambc::champion_type::marcella) continue;
            const int finish = e.remaining_channel_time() + 1;
            const int restart = std::max(cambc::constants::submit_gem_minimum_duration,
                static_cast<int>(std::ceil(e.gem_count() * cambc::constants::submit_gem_duration_multiplier)) + cambc::constants::submit_gem_base_duration) + 1;
            bool timing = true;
            for (int s = 0; s < 3; ++s) if (f.windows[s] < finish || f.windows[s] >= restart ||
                (independent >= 0 && f.sources[independent].score[s] >= static_cast<int>(e.gem_count()))) timing = false;
            if (!timing) continue;
            for (const auto& a : cambc::self().abilities()) {
                if (!a.is_cc() || !a.is_ready() || !a.can_target_enemies() || !a.valid_tiles().is_set(e.position())) continue;
                f.denial_certificate = true; f.denial_observed_round = f.round;
                f.denial_enemy = static_cast<int>(static_cast<cambc::handle_champion_t>(e)); f.denial_waypoint = tile(e.position()); f.denial_bag = e.gem_count();
                for (int s = 0; s < 3; ++s) { f.denial[s] = e.gem_count(); f.denial_force_independent[s] = independent >= 0 ? f.sources[independent].score[s] : 0; }
                return;
            }
        }
    }
    bool interrupt(const Task& task) {
        if (design_ != Design::encirclement || task.goal != Goal::pressure || !task.mechanism) return false;
        for (const auto& e : cambc::vision::champions(team::enemy)) if (static_cast<int>(static_cast<cambc::handle_champion_t>(e)) == task.peer && e.is_visible() && e.is_alive() && e.is_submitting_gems() && e.champion_name() != cambc::champion_type::marcella)
            for (const auto& a : cambc::self().abilities()) if (a.is_cc() && a.is_ready() && a.can_target_enemies() && a.valid_tiles().is_set(e.position()) && cambc::self().can_cast()) return a.use(e);
        return false;
    }
    void receipts() {
        P7_PHASE_SCOPE(2);
        auto& me = cambc::self();
        const auto score = cambc::global_submitted_gems(team::ally);
        if (observed_round_ >= 0 && observed_round_ != static_cast<int>(P7_PHASE_CLOCK(cambc::round_number(), 2))) {
            const bool consecutive = static_cast<int>(P7_PHASE_CLOCK(cambc::round_number(), 3)) - observed_round_ == 1;
            if (consecutive && capture_pending_ && last_source_ >= 0 && last_tile_ >= 0 && groups_[last_source_].spawns.is_set(point(last_tile_)) &&
                groups_[last_source_].spawns.is_set(me.position()) &&
                !(groups_[last_source_].spawns & cambc::vision::visible_tiles()).is_empty() &&
                std::holds_alternative<cambc::map::camp_group_respawning>(groups_[last_source_].group.state()) &&
                (me.gem_count() > last_bag_ || me.gold_count() > last_gold_ + cambc::constants::passive_gold)) {
                ++state_.captures; state_.captured[last_source_] = true;
                if (state_.source == last_source_) state_.payout_seen = true;
                last_capture_source_ = last_source_;
            }
            if (consecutive && submitting_ && !me.is_submitting_gems() && me.gem_count() == 0 && last_bag_ > 0 && score > last_score_) {
                ++state_.banks;
                if (state_.payout_seen && state_.source == last_capture_source_) state_.bank_seen = true;
                state_.bank_pending = false; submitting_ = false;
            }
            if (submitting_ && !me.is_submitting_gems() && me.gem_count() == 0) {
                // A missed observation interval is censored, never a bank
                // receipt. Release a vanished pending bag without credit.
                submitting_ = false; state_.bank_pending = false;
            }
        }
        // End-of-turn remember() captures the final location and inventories.
    }
    Frame frame(const std::vector<camp>& camps, const std::vector<champion>& allies, const std::vector<champion>& enemies) {
        P7_PHASE_SCOPE(10);
        auto& me = cambc::self(); Frame f;
        // Reuse live observations only inside this observation pipeline. Repeated
        // host queries in every source/support loop are expensive in WASM fuel.
        struct CampView { vec2 position, spawn; float health, damage; };
        std::vector<CampView> observed_camps;
        for (const auto& monster : camps) {
            const auto value = observe(monster);
            if (value.health > 0) observed_camps.push_back({{value.x, value.y}, monster.spawn_position(), value.health, value.strength});
        }
        const auto own = observe(me);
        f.design = design_; f.control = control_; f.round = P7_PHASE_CLOCK(cambc::round_number(), 4); f.self = static_cast<int>(static_cast<cambc::handle_champion_t>(me));
        f.ordinal = ordinal_; f.team_size = team_size_; f.tile = tile(me.position()); f.bag = me.gem_count(); f.gold = me.gold_count();
        f.health = own.health; f.max_health = own.max_health; f.damage = own.strength; f.focus = own.focus;
        f.kit_level = strength_shards_ + health_shards_ + focus_shards_;
        f.camp_damage_factor = own_camp_factor();
        f.captures = state_.captures; f.banks = state_.banks; f.last_capture_source = last_capture_source_;
        f.converting = !control_ && f.bag > 0 && committed_task_.goal == Goal::bank;
        f.next_purchase_cost = next_purchase_cost_;
        f.at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        f.own_score = cambc::global_submitted_gems(team::ally); f.enemy_score = cambc::global_submitted_gems(team::enemy);
        f.source_count = groups_.size(); f.altar_count = altars_.size(); f.port_count = std::min(2, f.altar_count);
        f.current = -1; f.own_ward_ready = me.ward_cooldown() == 0;
        auto spawn = cambc::map::spawn_region(team::ally).positions(); if (!spawn.empty()) f.spawn = tile(spawn.front());
        for (auto& row : f.distances) row.fill(-1);
        const int slot = f.round % 64; int elapsed = history_round_[slot] >= 0 ? f.round - history_round_[slot] : 0;
        unsigned delta = elapsed > 0 ? f.enemy_score - score_history_[slot] : 0;
        int horizon = std::min(120, std::max(0, static_cast<int>(cambc::constants::max_rounds) - f.round));
        if (delta > 0 && elapsed > 0) horizon = std::min(horizon, static_cast<int>((std::max(0, static_cast<int>(cambc::constants::winning_gems) - static_cast<int>(f.enemy_score)) * static_cast<long long>(elapsed)) / delta));
        int remaining = std::max(0, static_cast<int>(cambc::constants::max_rounds) - f.round);
        f.windows = {std::max(0, horizon * 3 / 4), horizon, std::min({120, remaining, horizon + horizon / 4})};
        score_history_[slot] = f.enemy_score; history_round_[slot] = f.round;
        for (const auto& a : me.abilities()) { f.healing_ready |= a.is_heal() && a.is_ready(); f.dash_ready |= a.is_dash() && a.is_ready(); }
        f.teleport_ready = me.consumables()[cambc::consumable_type::tp_crystal].count() > 0;
        for (const auto& a : allies) if (a != me && a.is_visible() && a.is_alive() && f.ally_count < 6) {
            const auto value = observe(a); const vec2 position{value.x, value.y}; const auto range = value.attack_range;
            if (position.x < 0 || position.y < 0) continue;
            Ally x; x.id = static_cast<int>(static_cast<cambc::handle_champion_t>(a)); x.ordinal = ordinal(a); x.tile = tile(position); x.visible = true;
            x.health = value.health; x.max_health = value.max_health; x.damage = value.strength; x.focus = value.focus; x.bag = a.gem_count();
            x.can_attack = a.can_attack();
            for (const auto& ability : a.abilities()) x.can_heal |= ability.is_heal() && ability.is_ready();
            for (int i = 0; i < f.source_count; ++i) if (groups_[i].spawns.is_set(position) || separation(position, groups_[i].anchor) <= range) { x.group = i; x.at_capture = groups_[i].spawns.is_set(position); break; }
            f.allies[f.ally_count++] = x;
        }
        for (const auto& e : enemies) if (e.is_visible() && e.is_alive() && f.enemy_count < 2) {
            const auto value = observe(e);
            if (value.x < 0 || value.y < 0) continue;
            Enemy x; x.id = static_cast<int>(static_cast<cambc::handle_champion_t>(e)); x.tile = tile({value.x, value.y}); x.health = value.health; x.damage = value.strength; x.bag = e.gem_count(); x.visible = true;
            x.channeling = e.is_submitting_gems() || e.is_recalling() || e.is_teleporting(); x.stun_immune = e.champion_name() == cambc::champion_type::marcella;
            f.enemies[f.enemy_count++] = x;
        }
        // Current capture and physical contact survive an estimator fuel cutoff.
        // Only one occupied group needs a live lifecycle query here.
        for (int i = 0; i < f.source_count; ++i) if (groups_[i].spawns.is_set(me.position())) {
            f.current = i;
            if ((groups_[i].spawns & ~visible_).is_empty() &&
                std::holds_alternative<cambc::map::camp_group_reward_available>(groups_[i].group.state())) {
                f.safe_capture = true;
                for (const auto& e : enemies) {
                    const auto enemy = observe(e);
                    const vec2 position{enemy.x, enemy.y};
                    if (position.x >= 0 && position.y >= 0 &&
                        cambc::champion_state::from_bits_retain(enemy.state).contains(cambc::champion_state::alive) &&
                        groups_[i].spawns.is_set(position)) {
                        f.safe_capture = false;
                        f.capture_enemy = static_cast<int>(static_cast<cambc::handle_champion_t>(e));
                        f.capture_enemy_tile = tile(position);
                        break;
                    }
                }
            }
            break;
        }
        // An optional estimator cutoff must not turn a stationary attack into
        // an apparently stalled journey. Observe the committed camp directly.
        if (f.current < 0 && competitive_.source >= 0 && competitive_.source < f.source_count) {
            const auto& group = groups_[competitive_.source];
            const auto attacks = me.attack_tiles();
            for (const auto& monster : observed_camps)
                if (group.spawns.is_set(monster.spawn) && attacks.is_set(monster.position)) {
                    f.current = competitive_.source; break;
                }
        }
        std::vector<std::pair<champion, bitboard>> ally_sight;
        for (const auto& a : allies) if (a != me && a.is_visible() && a.is_alive() && a.can_attack()) {
            if (cambc::fuel_remaining() < 90000) break;
            bool near = false;
            const auto position = a.position(); const auto range = a.attack_range();
            for (const auto& m : observed_camps) if (separation(position, m.position) <= range) { near = true; break; }
            if (near) ally_sight.emplace_back(a, a.visible_tiles_from(a.position()));
        }
        // Travel is charged separately. Once in range, stationary ranged
        // champions can make one basic attack each owner turn as melee can.
        const float rate = std::max(1.f, own.strength);
        auto price = [&](Source& x) {
            const int work = static_cast<int>(std::ceil(x.health / rate)) + cambc::constants::capture_time + 1;
            const int tribute = std::max(cambc::constants::submit_gem_minimum_duration, static_cast<int>(std::ceil((f.bag + x.gems) * cambc::constants::submit_gem_duration_multiplier)) + cambc::constants::submit_gem_base_duration);
            const int restore = static_cast<int>(std::ceil(std::max(0.f, f.max_health - f.health + self_camp_incoming(f, x) * std::max(0, work - cambc::constants::capture_time)) / std::max(1.f, f.max_health * cambc::constants::spawn_healing)));
            const int service_home = x.port >= 0 ? port_home_[x.port] : -1;
            x.cycle_turns = x.distance >= 0 && x.exit_distance >= 0 && x.spawn_distance >= 0 && service_home >= 0 ? x.distance + std::max(0, x.ready_after) + work + x.exit_distance + tribute + service_home + x.spawn_distance + restore + 2 : -1;
            x.ring = x.exit_distance >= 0 && service_home >= 0 && x.spawn_distance >= 0 ? (x.exit_distance + service_home + x.spawn_distance) / 8 : -1;
            return work;
        };
        int contact_priority = f.current >= 0 ? 3 : -1;
        // Cheap identities and current reachability remain available even when
        // detailed estimates stop. Unknown estimates never become feasible.
        std::array<int, 8> estimate_order{};
        for (int i = 0; i < f.source_count; ++i) {
            auto& g = groups_[i]; auto& x = f.sources[i]; x.id = static_cast<int>(static_cast<cambc::handle_camp_group_t>(g.group)); x.tile = tile(g.anchor);
#if P7_RECIPROCAL_SOURCE_FIELDS
            x.distance = reciprocal_source_distance(i, me.position()); x.reachable = x.distance >= 0;
#else
            x.distance = distance(current_field_, g.spawns); x.reachable = x.distance >= 0;
#endif
            x.spawn_distance = g.home_distance; x.home_connected = x.spawn_distance >= 0;
            x.gems = g.group.gem_value(); x.gold = g.group.gold_value(); x.health = g.health; x.incoming = g.incoming;
            x.seen = !(g.spawns & visible_).is_empty(); x.alive = true; x.ready_after = 0;
            if (x.seen) {
                auto status = g.group.state();
                x.reward_available = std::holds_alternative<cambc::map::camp_group_reward_available>(status);
                if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&status)) { x.respawning = true; x.ready_after = r->timer; }
                x.alive = !x.reward_available && !x.respawning;
                if (x.reward_available) x.health = x.incoming = 0;
            }
            for (int p = 0; p < f.port_count; ++p) if (port_distances_[p][i] >= 0 && (x.exit_distance < 0 || port_distances_[p][i] < x.exit_distance)) {
                x.port = p; x.exit_distance = port_distances_[p][i];
            }
            x.port_distance = x.exit_distance; x.recovery_distance = x.reserve_distance = x.spawn_distance;
            price(x);
            estimate_order[i] = i;
            if (x.home_connected && (f.home < 0 || x.spawn_distance < f.sources[f.home].spawn_distance)) f.home = i;
            for (int j = 0; j < f.source_count; ++j) f.distances[i][j] = source_distances_[i][j];
        }
        std::sort(estimate_order.begin(), estimate_order.begin() + f.source_count, [&](int a, int b) {
            auto priority = [&](int i) { return i == f.current ? 0 : i == competitive_.source ? 1 : i == state_.source ? 2 : i == f.round % f.source_count ? 3 : 4; };
            if (priority(a) != priority(b)) return priority(a) < priority(b);
            int da = f.sources[a].distance, db = f.sources[b].distance;
            if (da < 0) da = 100000; if (db < 0) db = 100000;
            return da != db ? da < db : a < b;
        });
        for (int n = 0; n < f.source_count; ++n) {
            if (cambc::fuel_remaining() < 60000) break;
            int i = estimate_order[n]; auto& g = groups_[i]; auto& x = f.sources[i];
            if (x.seen) {
                if ((g.spawns & ~visible_).is_empty()) { x.health = 0; x.incoming = 0; for (const auto& m : observed_camps) if (g.spawns.is_set(m.spawn)) { x.health += m.health; x.incoming += m.damage; } }
            }
            int exit = x.exit_distance;
            if (x.reward_available) { x.health = 0; x.incoming = 0; }
            int work = price(x);
            int tribute = std::max(cambc::constants::submit_gem_minimum_duration, static_cast<int>(std::ceil((f.bag + x.gems) * cambc::constants::submit_gem_duration_multiplier)) + cambc::constants::submit_gem_base_duration);
            bool survives = f.health > self_camp_incoming(f, x) * (std::ceil(x.health / rate) + 2) + f.max_health * .2f;
            x.feasible = x.reachable && x.seen && x.cycle_turns > 0 && !x.respawning && survives;
            float observed_support = 0; int helpers = 0;
            for (const auto& support : ally_sight) {
                const auto& ally = support.first; const auto& sight = support.second;
                if (!ally.can_attack() || ally.health() <= x.incoming * 3) continue;
                // One currently legal hit, never recurring remote DPS. The
                // hypothetical geometry is public and excludes ward credit.
                bool can_hit = false;
                const auto position = ally.position(); const auto range = ally.attack_range();
                for (const auto& monster : observed_camps) if (g.spawns.is_set(monster.spawn) &&
                    sight.is_set(monster.position) && separation(position, monster.position) <= range) can_hit = true;
                if (can_hit) { observed_support += ally.strength(); ++helpers; }
            }
            x.joint_feasible = x.reachable && x.seen && x.cycle_turns > 0 && !x.respawning && helpers && f.health > self_camp_incoming(f, x) * (std::ceil(std::max(0.f, x.health - observed_support) / rate) + 2) + f.max_health * .2f;
            x.progress = g.health > 0 ? std::max(0.f, (g.health - x.health) / g.health) : 0;
            x.payout = static_cast<int>(x.gems) / std::max(1, helpers + 1);
            for (int j = 0; j < f.enemy_count; ++j) if (separation(point(f.enemies[j].tile), g.anchor) <= 8) { x.pressured = true; f.fresh_pressure = true; f.pressure_tile = f.enemies[j].tile; f.pressure_port = x.port; }
            for (int s = 0; s < 3; ++s) if (x.cycle_turns > 0) {
                // Only complete cycles are scored. Renewal can contribute one
                // additional payout, with the real group respawn delay.
                int first = x.cycle_turns; int second = first + std::max(static_cast<int>(g.group.max_respawn_timer()), work + exit * 2 + tribute);
                x.score[s] = first <= f.windows[s] ? static_cast<int>(f.bag) + x.payout : 0;
                if (second <= f.windows[s]) x.score[s] += x.payout;
            }
            if (x.feasible && (f.safest < 0 || x.incoming < f.sources[f.safest].incoming)) f.safest = i;
            if (g.spawns.is_set(me.position())) { f.current = i; contact_priority = 3; }
            else if (x.seen) for (const auto& m : observed_camps)
                if (g.spawns.is_set(m.spawn) && me.attack_tiles().is_set(m.position)) {
                    int priority = i == state_.previous_source ? 2 : i == state_.source ? 1 : 0;
                    if (priority > contact_priority) { f.current = i; contact_priority = priority; }
                    break;
                }
            // Occupied reward/contest facts were observed before the estimator
            // cutoff. Do not query the same live group and enemy positions twice.
        }
        return f;
    }
    void opportunistic_heal() {
        if (!is_willow_ || cambc::fuel_remaining() < 85000) return;
        auto& me = cambc::self();
        const auto own = observe(me);
        const auto own_state = cambc::champion_state::from_bits_retain(own.state);
        if (!own_state.contains(cambc::champion_state::alive) || !own_state.contains(cambc::champion_state::can_cast) ||
            !std::isfinite(own.health) || !std::isfinite(own.max_health) || own.max_health <= 0 ||
            own.health < own.max_health * .5f || own.x < 0 || own.y < 0 || own.x >= width_ || own.y >= height_) return;
        // abilities() returns an owning array by value; keep it alive through the cast.
        const auto abilities = me.abilities();
        const cambc::ability* spell = nullptr;
        for (const auto& a : abilities) if (a.is_heal()) { spell = &a; break; }
        if (!spell || !spell->is_ready() || !spell->can_target_allies() || exclusive(*spell)) return;
        const float nominal = spell->heal_amount();
        if (!std::isfinite(nominal) || nominal <= 0) return;
        const auto range = spell->valid_tiles();
        std::optional<champion> best;
        float best_restore = 0;
        std::uint32_t best_id = 0;
        unsigned inspected = 0;
        for (const auto& ally : cambc::vision::champions(team::ally)) {
            if (ally == me) continue;
            if (inspected == 5 || cambc::fuel_remaining() < 65000) break;
            ++inspected;
            if (!ally.is_visible()) continue;
            const auto value = observe(ally);
            if (!cambc::champion_state::from_bits_retain(value.state).contains(cambc::champion_state::alive) ||
                !std::isfinite(value.health) || !std::isfinite(value.max_health) || value.health <= 0 ||
                value.health >= value.max_health || value.x < 0 || value.y < 0 || value.x >= width_ || value.y >= height_ ||
                !range.is_set({value.x, value.y})) continue;
            const float restored = spell->heal_amount(ally);
            if (!std::isfinite(restored) || restored < nominal * .5f) continue;
            const auto id = static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(ally));
            if (!best || restored > best_restore || (restored == best_restore && id < best_id)) {
                best = ally; best_restore = restored; best_id = id;
            }
        }
        if (!best || cambc::fuel_remaining() < 50000 || !me.can_cast() || !spell->is_ready() ||
            !spell->can_target_allies() || !best->is_visible()) return;
        const auto current = observe(*best);
        if (!cambc::champion_state::from_bits_retain(current.state).contains(cambc::champion_state::alive) ||
            !std::isfinite(current.health) || !std::isfinite(current.max_health) || current.health <= 0 ||
            current.health >= current.max_health || current.x < 0 || current.y < 0 || current.x >= width_ || current.y >= height_ ||
            !spell->valid_tiles().is_set({current.x, current.y})) return;
        const float current_nominal = spell->heal_amount();
        const float current_restore = spell->heal_amount(*best);
        if (std::isfinite(current_nominal) && current_nominal > 0 && std::isfinite(current_restore) &&
            current_restore >= current_nominal * .5f) spell->use(*best);
    }
    bool heal(const Task& t) {
        auto& me = cambc::self(); if (!t.heal || !me.can_cast()) return false;
        for (const auto& ally : cambc::vision::champions(team::ally)) if (ally != me && ally.is_visible() && ally.is_alive() &&
            (t.peer < 0 || static_cast<int>(static_cast<cambc::handle_champion_t>(ally)) == t.peer) && ally.health() < ally.max_health())
            for (const auto& a : me.abilities()) if (a.is_heal() && a.is_ready() && a.can_target_allies() && a.valid_tiles().is_set(ally.position())) return a.use(ally);
        return false;
    }
    bool execute_commitment(const std::vector<champion>& enemies) {
        P7_PHASE_SCOPE(8);
        const Task& task = committed_task_;
        const bool converting = task.goal == Goal::bank && cambc::self().gem_count() > 0;
        if (control_ || (!converting && static_cast<int>(P7_PHASE_CLOCK(cambc::round_number(), 5)) > commitment_until_) ||
            task.mobility || task.route || task.heal || task.release_helper) return false;
        // A plain conversion intent retains no live observation. A changed bag
        // always re-enters the planner first, to bind its actual capture receipt.
        if (task.goal == Goal::bank) { bank(task.port); return true; }
        if (task.source < 0 || task.source >= static_cast<int>(groups_.size()) ||
            (task.goal != Goal::farm && task.goal != Goal::inspect)) return false;
        // This is a short-lived intent, not a retained observation. Every camp,
        // target, obstacle, ward and action allowance is queried again below.
        if (task.goal == Goal::inspect && !(groups_[task.source].spawns & visible_).is_empty()) return false;
        const auto& group = groups_[task.source];
        if (!enemies.empty() && group.spawns.is_set(cambc::self().position()) &&
            std::holds_alternative<cambc::map::camp_group_reward_available>(group.group.state())) {
            for (const auto& enemy : enemies) {
                const auto value = observe(enemy);
                if (value.x >= 0 && value.y >= 0 &&
                    cambc::champion_state::from_bits_retain(value.state).contains(cambc::champion_state::alive) &&
                    group.spawns.is_set({value.x, value.y})) return false;
            }
        }
        if (sample_turn_) { sample_goal_ = static_cast<int>(task.goal); sample_source_ = task.source; }
        Frame live; live.source_count = groups_.size(); live.own_ward_ready = cambc::self().ward_cooldown() == 0;
        ward(task, live);
        if (task.waypoint >= 0 && tile(cambc::self().position()) != task.waypoint &&
            !groups_[task.source].spawns.is_set(point(task.waypoint))) {
            move_to(around(point(task.waypoint), 0)); return true;
        }
        farm(task.source, task.goal == Goal::inspect); return true;
    }
    void farm(int source, bool inspect) {
        P7_PHASE_SCOPE(14);
#if P7_LIVE_TRACE
        live_trace_mark(12);
        if (live_trace_active_) { live_packet_.farm_source = source; live_packet_.inspect = inspect; }
#endif
        if (source < 0 || source >= static_cast<int>(groups_.size())) return;
        if (P7_PHASE_FUEL(cambc::fuel_remaining(), 6) < 30000) {
#if P7_LIVE_TRACE
            live_trace_mark(12, -1, 31);
#endif
            return;
        }
        auto& me = cambc::self(); auto& g = groups_[source];
        if (!(g.spawns & cambc::vision::visible_tiles()).is_empty()) {
            auto status = g.group.state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(status)) { if (!g.spawns.is_set(me.position())) move_to(g.spawns); return; }
            if (std::holds_alternative<cambc::map::camp_group_respawning>(status)) {
                // Vacate all spawn tiles immediately after our payout.
                if (g.spawns.is_set(me.position())) move_to(around(me.position(), 2) & ~g.spawns & ~terrain_);
                return;
            }
        }
        std::optional<camp> target, available;
        float weakest_health = 0, available_health = 0;
        const bool attack_ready = !inspect && me.can_attack();
        std::optional<bitboard> attacks;
        if (attack_ready) attacks = me.attack_tiles();
        for (const auto& m : cambc::vision::camps()) {
            if (P7_PHASE_FUEL(cambc::fuel_remaining(), 7) < 25000) {
#if P7_LIVE_TRACE
                live_trace_mark(12, -1, 32);
#endif
                return;
            }
            if (!g.spawns.is_set(m.spawn_position())) continue;
            // One fresh record within this action-free, selected-source scan.
            const auto value = observe(m);
            if (value.health <= 0) continue;
            if (!target || value.health < weakest_health) { target = m; weakest_health = value.health; }
            if (attacks && attacks->is_set({value.x, value.y}) &&
                (!available || value.health < available_health)) { available = m; available_health = value.health; }
        }
        // Try current reach before routing toward a weaker distant member.
        // If fight lacks fuel, retain the original target and path fallback.
        if (available && fight(*available, false, g.incoming > 0)) return;
        if (target && !inspect && fight(*target, false, g.incoming > 0)) return;
        bitboard goals;
        if (target) goals = around(target->position(), me.attack_range());
        else for (const auto& m : g.group.camps()) goals |= around(m.spawn_position(), 1);
        if (move_to(goals) && !inspect && P7_PHASE_FUEL(cambc::fuel_remaining(), 8) >= 35000) for (const auto& m : cambc::vision::camps()) {
            if (P7_PHASE_FUEL(cambc::fuel_remaining(), 9) < 25000) return;
            if (m.is_alive() && g.spawns.is_set(m.spawn_position()) && me.attack_tiles().is_set(m.position())) { fight(m, true, g.incoming > 0); break; }
        }
    }
public:
#if P7_PHASE_TRACE
    bool flush_phase_trace_turn() { return phase_flush(); }
    void finish_phase_trace_turn(unsigned flag = 4) { phase_finish(flag); }
#endif
#if P7_LIVE_TRACE
    bool flush_live_trace_turn() {
        if (!live_trace_pending_) return false;
        live_trace_flush();
        return true;
    }
#endif
    Bot(Design design, bool control, const std::array<const char*, 5>& order) : design_(design), control_(control), order_(order) {}
    void draft() {
        cambc::draft_memory = std::array{std::byte{7}, static_cast<std::byte>(cambc::draft::team_size())};
        auto selected = cambc::draft::current_draft(team::ally);
        for (const auto* name : order_) { auto choice = pick(name); if (std::find(selected.begin(), selected.end(), choice) == selected.end()) { cambc::draft::pick(choice); return; } }
    }
    void remember() {
        P7_PHASE_SCOPE(20);
        if (sample_turn_ && cambc::fuel_remaining() > 30000) {
            std::cout << "p7 decision r=" << cambc::round_number() << " actor=" << static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self())) << " fuel=";
            for (auto fuel : sample_fuel_) std::cout << fuel << ',';
            std::cout << " goal=" << sample_goal_ << " source=" << sample_source_ << " move=" << sample_move_
                      << " proposal=" << sample_proposal_goal_ << ',' << sample_proposal_source_ << ',' << sample_proposal_flags_
                      << " flags=" << sample_flags_ << std::endl;
        }
        // An interrupted optional action must not consume receipt bookkeeping's
        // final reserve. Missing observations revoke continuity next turn.
        if (P7_PHASE_FUEL(cambc::fuel_remaining(), 5) < 6000) { observed_round_ = -1; return; }
        auto& me = cambc::self(); if (!initialized_ || !me.is_alive()) return;
        P7_PHASE_ENTER(21);
        last_bag_ = me.gem_count(); last_gold_ = me.gold_count(); last_score_ = cambc::global_submitted_gems(team::ally);
        last_tile_ = tile(me.position()); last_source_ = -1; capture_pending_ = false;
        P7_PHASE_LEAVE(21);
        P7_PHASE_ENTER(22);
        for (int i = 0; i < static_cast<int>(groups_.size()); ++i) if (groups_[i].spawns.is_set(me.position())) {
            last_source_ = i;
            capture_pending_ = std::holds_alternative<cambc::map::camp_group_reward_available>(groups_[i].group.state());
            if (!capture_pending_ && state_.bank_seen && last_renewal_source_ != i &&
                std::holds_alternative<cambc::map::camp_group_alive>(groups_[i].group.state())) {
                ++state_.renewals; last_renewal_source_ = i;
            }
            break;
        }
        P7_PHASE_LEAVE(22);
        observed_round_ = P7_PHASE_CLOCK(cambc::round_number(), 11);

    }
    void turn() {
        const auto round = cambc::round_number();
#if P7_LIVE_TRACE
        live_trace_begin(round);
        LiveTraceScope live_trace_scope{*this};
        live_trace_mark(1);
#endif
        sample_turn_ = round == 128 || round == 256 || round == 512;
        if (sample_turn_) { sample_fuel_ = {}; sample_goal_ = sample_source_ = sample_move_ = -1;
            sample_proposal_goal_ = sample_proposal_source_ = -1; sample_flags_ = sample_proposal_flags_ = 0; sample_fuel_[0] = cambc::fuel_remaining(); }
        auto& me = cambc::self();
#if P7_PHASE_TRACE
        phase_begin(round, static_cast<unsigned>(static_cast<cambc::handle_champion_t>(me)));
        P7_PHASE_SCOPE(1);
#endif
#if P7_BOUNDED_INITIALIZATION
        if (!initialized_) {
            initialize();
            // Immutable preparation can resume in a later owner turn with a
            // different fuel tail. Re-enter the live pipeline next turn with
            // fresh round/sample state, never through this preparation call.
#if P7_LIVE_TRACE
            live_trace_mark(1, -1, 1);
#endif
            return;
        }
#else
        if (!initialized_) initialize();
#endif
        if (!me.is_alive()) { shop(); recovering_ = false; submitting_ = false; state_.bank_pending = false; last_bag_ = 0; last_source_ = -1; competitive_ = {}; commitment_until_ = -1; return; }
        receipts();
#if P7_LIVE_TRACE
        live_trace_mark(2, 1);
#endif

        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();

        if (cambc::map::spawn_region(team::ally).is_set(me.position())) { recovering_ = false; if (me.health() < me.max_health()) { return; } }
        if (P7_PHASE_FUEL(cambc::fuel_remaining(), 1) < 50000) return;
#if P7_LIVE_TRACE
        live_trace_mark(3, 1);
#endif
        if (warm()) { return; }
#if P7_LIVE_TRACE
        live_trace_mark(4, 2);
#endif
        if (is_willow_) {
            opportunistic_heal();
            // The run loop yields; the next turn rebuilds all world observations.
            if (P7_PHASE_FUEL(cambc::fuel_remaining(), 2) < 50000) return;
        }
        if (sample_turn_) sample_fuel_[1] = cambc::fuel_remaining();
        P7_PHASE_ENTER(5);
        visible_ = cambc::vision::visible_tiles();
        auto camps = cambc::vision::camps();
        auto enemies = cambc::vision::champions(team::enemy);
        const auto own = observe(me); const vec2 origin{own.x, own.y};
        float incoming = 0;
#if P7_RANGE_RECOVERY
        remember_recovery_camps(camps);
        for (const auto& c : recovery_camps_)
            if (separation(origin, {c.hazard.x, c.hazard.y}) <= c.hazard.range) incoming += c.hazard.damage;
#else
        for (const auto& m : camps) {
            const auto value = observe(m);
            if (value.health > 0 && value.strength > 0 &&
                separation(origin, {value.x, value.y}) <= m.attack_range() && m.is_awake()) incoming += value.strength * own_camp_factor();
        }
#endif
        for (const auto& e : enemies) {
            const auto value = observe(e);
            if (value.x >= 0 && value.y >= 0 && cambc::champion_state::from_bits_retain(value.state).contains(cambc::champion_state::alive) &&
                separation(origin, {value.x, value.y}) <= value.attack_range + 1) incoming += value.strength;
        }
#if P7_LIVE_TRACE
        live_trace_mark(5, 2);
#endif
        P7_PHASE_LEAVE(5);
        if (own.health <= incoming * 3 + own.max_health * .18f) recovering_ = true;
        if (recovering_) { recover(camps, enemies); return; }
        const bool special_exit = design_ == Design::islander || design_ == Design::forks ||
            design_ == Design::causeway || design_ == Design::ports || design_ == Design::veil;
        const auto bag = me.gem_count();
        bool bank_due = bag >= competitive_bank_target(design_) ||
            P7_PHASE_CLOCK(cambc::round_number(), 6) + 100 >= cambc::constants::max_rounds ||
            cambc::global_submitted_gems(team::ally) + bag >= cambc::constants::winning_gems;
        const auto location = me.position();
        for (const auto& altar : altars_) if (separation(location, altar) <= 3) bank_due = true;
        if (!control_ && bag && bank_due && !special_exit) {
            // Ordinary conversion can skip forecasts; specialized loaded exits
            // must pass through their design's current route/port decision.
            bool holding = false;
            for (const auto& group : groups_) if (group.spawns.is_set(me.position()) &&
                (group.spawns & ~visible_).is_empty() &&
                std::holds_alternative<cambc::map::camp_group_reward_available>(group.group.state())) {
                holding = true;
                for (const auto& enemy : enemies) if (group.spawns.is_set(enemy.position())) holding = false;
                break;
            }
            if (!holding) { bank(-1); return; }
        }
        if (bag == last_bag_ && (!bag || !bank_due || committed_task_.goal == Goal::bank) && execute_commitment(enemies)) return;
#if P7_RECIPROCAL_SOURCE_FIELDS
        if (!reciprocal_sources_ready()) {
#endif
#if P7_REUSE_CURRENT_FIELD
        P7_PHASE_ENTER(9);
        prepare_current_field(me.position());
        P7_PHASE_LEAVE(9);
        // A completed old-root field is still an immutable cache entry, but a
        // suspended caller must not feed its old observations into frame().
        if (P7_PHASE_CLOCK(cambc::round_number(), 7) != round) return;
#else
        P7_PHASE_ENTER(9);
        compute(current_field_, around(me.position(), 0));
        P7_PHASE_LEAVE(9);
#endif
#if P7_RECIPROCAL_SOURCE_FIELDS
        }
#endif
#if P7_LIVE_TRACE
        live_trace_mark(6, 3);
#endif
        if (sample_turn_) sample_fuel_[2] = cambc::fuel_remaining();
        Frame f = frame(camps, cambc::vision::champions(team::ally), enemies);
#if P7_LIVE_TRACE
        live_trace_mark(7, 4);
#endif
        if (P7_PHASE_CLOCK(cambc::round_number(), 8) != round) return;
        if (sample_turn_) sample_fuel_[3] = cambc::fuel_remaining();

        geometry(f);
#if P7_LIVE_TRACE
        live_trace_mark(8, 5);
#endif
        if (P7_PHASE_CLOCK(cambc::round_number(), 9) != round) return;
        if (sample_turn_) sample_fuel_[4] = cambc::fuel_remaining();
        if (P7_PHASE_FUEL(cambc::fuel_remaining(), 13) < 30000) {
#if P7_LIVE_TRACE
            live_trace_mark(8, -1, 16);
#endif
            return;
        }
        P7_PHASE_ENTER(12);
        denial(f, enemies);
#if P7_LIVE_TRACE
        live_trace_mark(9);
#endif
        bool ports_ready = true;
        for (int p = 0; p < f.port_count; ++p) if (!port_fields_[p]) ports_ready = false;
        State proposed = state_;
        // Conversion is an intent through the actual payout, not a three-turn
        // production commitment. Revalidate any source-bound exit in the fresh
        // frame instead of forgetting a sale halfway to its altar.
        Task proposal = f.converting ? committed_task_ : ports_ready ? choose(f, proposed) : fallback(f, proposed);
        Task task = proposal;
        if (!control_) task = competitive_plan(f, task, competitive_);
#if P7_LIVE_TRACE
        live_trace_mark(10, 6);
        if (live_trace_active_) {
            live_packet_.proposal_goal = static_cast<int>(proposal.goal); live_packet_.proposal_source = proposal.source;
            live_packet_.task_goal = static_cast<int>(task.goal); live_packet_.task_source = task.source;
        }
#endif
        // Commit admission/continuation only when its task survived arbitration.
        // A later reward from unrelated fallback work cannot fund that project.
        if (control_ || task == proposal) state_ = proposed;
        committed_task_ = task; commitment_until_ = static_cast<int>(P7_PHASE_CLOCK(cambc::round_number(), 10)) + 3;
        P7_PHASE_LEAVE(12);
        if (sample_turn_) {
            auto flags = [](const Task& t) { return (t.mechanism ? 1 : 0) | (t.heal ? 2 : 0) | (t.ward ? 4 : 0) |
                (t.mobility ? 8 : 0) | (t.release_helper ? 16 : 0) | (t.route << 5); };
            sample_fuel_[5] = cambc::fuel_remaining(); sample_goal_ = static_cast<int>(task.goal); sample_source_ = task.source;
            sample_proposal_goal_ = static_cast<int>(proposal.goal); sample_proposal_source_ = proposal.source;
            sample_flags_ = flags(task); sample_proposal_flags_ = flags(proposal);
        }

#if P7_LIVE_TRACE
        live_trace_mark(11);
#endif
        P7_PHASE_SCOPE(13);
        if (f.safe_capture && !task.release_helper) return;
        heal(task);
        ward(task, f);
        if (interrupt(task)) return;
        if (task.mobility && valid_source(f, task.source) && f.sources[task.source].mobility) {
            const auto& circuit = f.bag ? mobile_out_[task.source].tiles : mobile_in_[task.source].tiles;
            if (!circuit.empty()) {
                auto at = std::find(circuit.begin(), circuit.end(), tile(me.position()));
                if (at != circuit.end() && at + 1 != circuit.end()) {
                    if (follow(circuit, true)) return;
                    // A newly occupied landing revokes the active route. Farm
                    // or recover locally; never pretend the crossing occurred.
                    if (f.bag) { recovering_ = true; recover(camps, enemies, true); return; }
                } else if (f.bag && at == circuit.end()) {
                    move_to(around(point(circuit.front()), 0)); return;
                } else if (!f.bag && at == circuit.end() && distance(current_field_, point(circuit.front())) >= 0) {
                    move_to(around(point(circuit.front()), 0)); return;
                }
            }
        }
        if (task.route == 1 && valid_source(f, task.source) && f.bag) {
            const auto& route = exits_[task.source][1];
            if (!route.empty() && tile(me.position()) != route.back()) {
                const int here = tile(me.position());
                if (std::find(route.begin(), route.end(), here) == route.end() && groups_[task.source].spawns.is_set(me.position()) &&
                    (groups_[task.source].spawns & ~visible_).is_empty()) {
                    // A multispawn holder may join the witness only through
                    // this freshly cleared source region. The join is not a
                    // third independent exit and cannot cross another district.
                    std::vector<unsigned char> excluded(width_ * height_, 1);
                    auto obstacles = blocked();
                    for (auto p : groups_[task.source].spawns.positions()) if (!obstacles.is_set(p)) excluded[tile(p)] = 0;
                    excluded[here] = 0;
                    auto join = nav_.route(here, route.front(), &excluded, width_ * height_);
                    if (follow(join, false)) return;
                }
                if (follow(route, false)) return;
                // The retained alternate no longer starts at our physical
                // position or is blocked. Revoke before ordinary safe banking.
                exits_[task.source][1].clear(); alternate_port_[task.source] = -1;
            }
        }
        if (design_ == Design::veil && task.mechanism && state_.relocated && valid_source(f, state_.source)) {
            auto route = exits_[state_.source][0];
            if (!f.bag) std::reverse(route.begin(), route.end());
            if (!route.empty() && tile(me.position()) != route.back()) {
                if (follow(route, false)) return;
                state_.relocated = false; // Blocked or off witness: ordinary fallback.
            }
        }
        if (task.goal == Goal::recover) {
            // A full-health start with no observed feasible cycle inspects the
            // nearest public source; it does not invent income or sit forever.
            if (f.at_spawn && f.health >= f.max_health && f.source_count && !state_.inspection_failed) {
                int inspect = 0; for (int i = 1; i < f.source_count; ++i) if (f.sources[i].distance >= 0 && (f.sources[inspect].distance < 0 || f.sources[i].distance < f.sources[inspect].distance)) inspect = i;
                farm(inspect, true); return;
            }
            recovering_ = true; recover(camps, enemies, true); return;
        }
        if (task.goal == Goal::bank) { bank(task.port); return; }
        if (task.goal == Goal::guard || task.goal == Goal::pressure) {
            for (const auto& e : enemies) if ((task.peer < 0 || static_cast<int>(static_cast<cambc::handle_champion_t>(e)) == task.peer) &&
                e.is_visible() && e.is_alive()) {
                // fight checks current cast readiness, target categories and
                // spell range before considering a fresh basic attack.
                if (fight(e, false, true)) return;
            }
        }
        if (task.release_helper && valid_source(f, task.source)) {
            auto goals = around(me.position(), 3) & ~groups_[task.source].spawns & ~terrain_;
            move_to(goals); return;
        }
        if (task.waypoint >= 0 && tile(me.position()) != task.waypoint) {
            int radius = task.heal || task.goal == Goal::guard || task.goal == Goal::pressure ? me.attack_range() : 0;
            auto goals = around(point(task.waypoint), radius) & ~blocked();
            if (goals.is_empty() && valid_source(f, task.source) && groups_[task.source].spawns.is_set(point(task.waypoint))) {
                farm(task.source, task.goal == Goal::inspect || task.goal == Goal::rally); return;
            }
            if (!goals.is_set(me.position())) { move_to(goals); return; }
        }
        if (valid_source(f, task.source)) {
            if (sample_turn_) sample_fuel_[6] = cambc::fuel_remaining();

            if (competitive_.source != task.source && !f.sources[task.source].feasible && f.sources[task.source].seen && f.sources[task.source].incoming > 0 && task.goal != Goal::inspect) {
                // Productive partial clears retain actual residual monster HP;
                // reactive survival always overrides a territorial commitment.
                if (f.health < f.max_health * .8f) { recovering_ = true; recover(camps, enemies, true); return; }
            }
            farm(task.source, task.goal == Goal::inspect || task.goal == Goal::rally);

        }
    }
};
} // namespace native
inline int run(Design design, bool control, const std::array<const char*, 5>& order) {
    native::Bot bot(design, control, order);
    while (true) {
        try { if (cambc::is_draft()) bot.draft(); else {
#if P7_PHASE_TRACE
            if (!bot.flush_phase_trace_turn()) {
#endif
#if P7_LIVE_TRACE
            // A retained packet owns a fresh diagnostic turn. No live work or
            // remember() observation/sample pipeline follows its emission.
            if (!bot.flush_live_trace_turn()) {
#endif
                bot.turn(); bot.remember();
#if P7_LIVE_TRACE
            }
#endif
#if P7_PHASE_TRACE
            }
            bot.finish_phase_trace_turn();
#endif
        } }
        catch (const std::exception& e) {
#if P7_PHASE_TRACE
            bot.finish_phase_trace_turn(8);
#endif
            std::cout << "p7 error: " << e.what() << '\n';
        }
        catch (...) {
#if P7_PHASE_TRACE
            bot.finish_phase_trace_turn(8);
#endif
            std::cout << "p7 error: unknown exception\n";
        }
        cambc::end_turn();
    }
}
} // namespace p7::live
