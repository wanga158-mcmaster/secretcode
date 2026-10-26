#pragma once
#include "foundry.h"
#include "../../common/model-runtime.h"
#include <memory>

#ifndef PLAN4_FOUNDRY_ROUTE_DOMINANCE
#define PLAN4_FOUNDRY_ROUTE_DOMINANCE 0
#endif
#ifndef PLAN4_FOUNDRY_BIND_SITE
#define PLAN4_FOUNDRY_BIND_SITE 0
#endif
#ifndef PLAN4_FOUNDRY_ACTIVE_STORAGE
#define PLAN4_FOUNDRY_ACTIVE_STORAGE 0
#endif
#ifndef PLAN4_FOUNDRY_PREP_TRACE
#define PLAN4_FOUNDRY_PREP_TRACE 0
#endif
#if PLAN4_FOUNDRY_BIND_SITE || PLAN4_FOUNDRY_PREP_TRACE
#include "../../common/telemetry.h"
#endif
#if PLAN4_FOUNDRY_PREP_TRACE
#include <cambc.h>
#endif

namespace plan4::runtime {
class Foundry : public M0 {
public:
    enum class Unsupported { none, stale_frame, truncated, roster, composition, unknown_stock,
                             participation, changed_order, occupancy, geometry, route, recall, budget, forecast };
private:
    struct Storage {
        struct ReceiptPrefix {
            Job job;
            std::array<ModelPredictionEvent, 8> events;
            int count = 0;
        };
        foundry::Input input;
        foundry::FixedPlan plan;
        ActorFrame actors;
        ProducerFrame producers;
        CombatKit kit;
        SpatialFrame spatial;
        std::array<PublicGroup, 8> groups;
        std::array<PublicMember, 256> members;
        std::array<VisibleMember, 256> visible_members;
        ObservationClock layout_clock;
        int group_count = 0, member_count = 0, visible_count = 0;
        bool layout_valid = false, members_valid = false;
        std::array<int, foundry::group_limit> source_indices;
        std::array<ReceiptPrefix, max_jobs> receipts;
        const Information* receipt_source = nullptr;
        int receipt_round = -1, receipt_actor = -1, receipt_count = 0, receipt_chosen = -1;
        Point receipt_position{};
        float receipt_health = 0;
        unsigned receipt_bag = 0, receipt_our_score = 0, receipt_enemy_score = 0;
    };
    std::unique_ptr<Storage> storage_ = std::make_unique<Storage>();
    std::unique_ptr<foundry::PathState> path_ = std::make_unique<foundry::PathState>();
    bool enabled_ = true, coupled_ = false;
    int bound_allies_ = 0, completed_paths_ = 0, receipt_work_ = 0;
    int truncation_mask_ = -1;
    std::uint64_t revision_ = 0;
    Unsupported unsupported_ = Unsupported::stale_frame;
#if PLAN4_FOUNDRY_PREP_TRACE
    // A single retained native consultation, not a gameplay admission rule.
    // All timestamps are diagnostic imports; the trace can perturb execution.
    bool prep_trace_active_ = false;
    std::array<std::int64_t, 8> prep_trace_fuel_;
    std::array<int, 8> prep_trace_round_;
    int prep_trace_stop_ = -1;
    void prep_trace_sample(int stage) {
        if (prep_trace_active_) {
            prep_trace_fuel_[stage] = std::int64_t(cambc::fuel_remaining());
            prep_trace_round_[stage] = int(cambc::round_number());
        }
    }
    void prep_trace_begin(const Information& info) {
        prep_trace_active_ = info.round == 721 && info.own.id == 1;
        if (!prep_trace_active_) return;
        prep_trace_fuel_.fill(-1); prep_trace_round_.fill(-1); prep_trace_stop_ = -1; prep_trace_sample(0);
    }
    void prep_trace_emit(const Information& info, const Evaluation& out) {
        if (!prep_trace_active_) return;
        LogRow("foundry-preparation", "foundry-preparation-r721-v1")
            .field("actor", info.own.id).field("round", info.round)
            .field("entryFuel", prep_trace_fuel_[0]).field("premiseFuel", prep_trace_fuel_[1])
            .field("preparedFuel", prep_trace_fuel_[2]).field("groupFuel", prep_trace_fuel_[3])
            .field("boundFuel", prep_trace_fuel_[4]).field("candidateFuel", prep_trace_fuel_[5])
            .field("completedFuel", prep_trace_fuel_[6]).field("stopFuel", prep_trace_fuel_[7])
            .field("entryRound", prep_trace_round_[0]).field("premiseRound", prep_trace_round_[1])
            .field("preparedRound", prep_trace_round_[2]).field("groupRound", prep_trace_round_[3])
            .field("boundRound", prep_trace_round_[4]).field("candidateRound", prep_trace_round_[5])
            .field("completedRound", prep_trace_round_[6]).field("stopRound", prep_trace_round_[7])
            .field("stopSite", prep_trace_stop_).field("reason", int(unsupported_))
            .field("evaluated", out.evaluated_count).emit();
    }
#endif
    bool reject(Unsupported reason) {
        if (unsupported_ != Unsupported::budget) unsupported_ = reason;
        return false;
    }
    static bool fresh(Provenance p, int round, Source source) {
        return p.observed_round == round && p.source == source && p.status == Status::exact_now;
    }
#if PLAN4_FOUNDRY_BIND_SITE || PLAN4_FOUNDRY_PREP_TRACE
    bool budget(const Information& info, int site = __builtin_LINE()) {
        const bool available = !info.work_budget.can_refine || info.work_budget.can_refine(info.work_budget.context);
#if PLAN4_FOUNDRY_BIND_SITE
        if (!available && unsupported_ != Unsupported::budget)
            LogRow("bind-budget-site", "foundry-budget-site-v1").field("actor", info.own.id)
                .field("round", info.round).field("site", site).emit();
#endif
#if PLAN4_FOUNDRY_PREP_TRACE
        if (!available && prep_trace_active_ && prep_trace_stop_ < 0) {
            prep_trace_stop_ = site; prep_trace_sample(7);
        }
#endif
        return available || reject(Unsupported::budget);
    }
#else
    bool budget(const Information& info) {
        return !info.work_budget.can_refine || info.work_budget.can_refine(info.work_budget.context) || reject(Unsupported::budget);
    }
#endif
    void invalidate_receipts() {
        coupled_ = false; storage_->receipt_source = nullptr; storage_->receipt_count = 0; storage_->receipt_chosen = -1;
        bound_allies_ = completed_paths_ = receipt_work_ = 0; unsupported_ = Unsupported::stale_frame;
        truncation_mask_ = -1;
    }
    bool cache_receipts(const Information& info, const Job& job, int index) {
        auto& prefix = storage_->receipts[index]; prefix.job = job; prefix.count = 0;
        // RecordKind is the private Foundry receipt schema. Actor/group indices
        // are converted here; the log never exposes a local index as an ID.
        for (int n = 0; n < path_->event_count() && prefix.count < 8; ++n) {
            if ((n % 8) == 0 && !budget(info)) return false;
            ++receipt_work_;
            const auto& record = path_->event(n);
            if (record.kind != foundry::RecordKind::capture && record.kind != foundry::RecordKind::bank &&
                record.kind != foundry::RecordKind::clear && record.kind != foundry::RecordKind::death) continue;
            auto& event = prefix.events[prefix.count++];
            event.predicted_round = record.at.round; event.phase = int(record.at.phase); event.kind = int(record.kind);
            event.actor_id = record.actor < 0 ? -1 : storage_->input.actors[record.actor].id;
            event.group_id = record.group < 0 ? -1 : storage_->input.groups[record.group].id;
            event.amount_milli = std::int64_t(record.amount * 1000.0); event.bag_after = record.bag_after;
        }
        return true;
    }
    const PublicGroup* public_group(int id) const {
        for (int g = 0; g < storage_->group_count; ++g) if (storage_->groups[g].group_id == id) return &storage_->groups[g];
        return nullptr;
    }
    bool member_spawn(const PublicGroup& group, Point p) const {
        for (int m = 0; m < group.count; ++m) if (storage_->members[group.first + m].spawn == p) return true;
        return false;
    }
    bool known_free(Point p) const {
        const auto& frame = storage_->spatial;
        return frame.topology.walkable(p, frame.topology.context) &&
            frame.occupancy(p, frame.occupancy_context) == TileOccupancy::free;
    }
    int distance(Point a, Point b) const {
        if (a == b) return 0;
        const auto& frame = storage_->spatial;
        return frame.travel.distance(a, b, frame.travel.context);
    }
    static int supported_distance(Point a, Point b, const void* context) {
        auto& self = *const_cast<Foundry*>(static_cast<const Foundry*>(context));
        const auto& frame = self.storage_->spatial;
        if (!frame.information || !self.budget(*frame.information)) return unreachable;
        const int d = self.distance(a, b);
        if (d <= 0) return d;
        if (!frame.route_prefix) return unreachable;
        std::array<Point, max_horizon> prefix;
        const int wanted = std::min(d, max_horizon);
        if (!self.budget(*frame.information)) return unreachable;
        const int count = frame.route_prefix(a, b, prefix.data(), wanted, frame.route_context);
        if (count != wanted) return unreachable;
        for (int n = 0; n < count; ++n) {
            if (!self.budget(*frame.information) || !self.known_free(prefix[n])) return unreachable;
        }
        if (d <= max_horizon && prefix[count - 1] != b) return unreachable;
        return d;
    }
    bool reserved(Point p) const {
        for (int a = 1; a < storage_->input.actor_count; ++a)
            if (storage_->input.actors[a].capture_position == p) return true;
        return false;
    }
    bool waypoints(const Information& info, int local, const PublicGroup& group, Point monster) {
        auto& target = storage_->plan.targets[local]; target = {};
        int best = std::numeric_limits<int>::max();
        for (int m = 0; m < group.count; ++m) {
            if (!budget(info)) return false;
            const auto p = storage_->members[group.first + m].spawn;
            if (!known_free(p) || reserved(p)) continue;
#if PLAN4_FOUNDRY_ROUTE_DOMINANCE
            // Cached distances are sealed, pure geometry. A candidate that
            // cannot beat a fully proved incumbent needs no route proof.
            // Every retained endpoint still takes the unchanged full proof.
            const int quote = distance(info.own.position, p);
            if (quote < 0 || quote > best ||
                (quote == best && !point_less(p, target.capture_position))) continue;
#endif
            const int d = supported_distance(info.own.position, p, this);
            if (d >= 0 && (d < best || (d == best && point_less(p, target.capture_position)))) {
                best = d; target.capture_position = p; target.capture_position_is_public_spawn = true;
            }
        }
        if (!target.capture_position_is_public_spawn) return reject(Unsupported::occupancy);
        {
            best = std::numeric_limits<int>::max(); bool found = false;
            const int range = storage_->kit.attack_range;
            if (range < 1 || range > 3) return reject(Unsupported::composition);
            for (int y = monster.y - range; y <= monster.y + range; ++y)
                for (int x = monster.x - range; x <= monster.x + range; ++x) {
                    if (!budget(info)) return false;
                    Point p{x, y}; if (!known_free(p) || reserved(p) || member_spawn(group, p)) continue;
                    bool reaches_full_group = true;
                    for (int m = 0; m < group.count; ++m) {
                        if (!budget(info)) return false;
                        const auto spawn = storage_->members[group.first + m].spawn;
                        reaches_full_group &= std::max(std::abs(spawn.x - p.x), std::abs(spawn.y - p.y)) <= range;
                    }
                    if (!reaches_full_group) continue;
#if PLAN4_FOUNDRY_ROUTE_DOMINANCE
                    const int quote = distance(info.own.position, p);
                    if (quote < 0 || quote > best ||
                        (quote == best && !point_less(p, target.attack_position))) continue;
#endif
                    const int d = supported_distance(info.own.position, p, this);
                    if (d >= 0 && (d < best || (d == best && point_less(p, target.attack_position)))) {
                        found = true; best = d; target.attack_position = p;
                    }
                }
            if (!found) return reject(Unsupported::route);
        }
        target.supported = true; return true;
    }
    bool bind(const Information& info, const Menu& menu) {
        auto& s = *storage_; unsupported_ = Unsupported::none;
        const auto matches = [&](ObservationClock clock) { return clock.round == info.round && clock.own.id == info.own.id; };
        if (!budget(info)) return false;
        if (info.own.channel != Channel::none) return reject(Unsupported::forecast); // Preserve the common channel path.
        if (info.group_count < 0 || info.group_count > max_groups || info.altar_count < 0 || info.altar_count > max_altars)
            return reject(Unsupported::composition);
        // Diagnostic only: -1 means this guard was not reached. Keep all
        // matching clauses, including shared-frame truncation cascades.
        truncation_mask_ = (info.public_layout_truncated ? 1 : 0) | (menu.truncated ? 2 : 0) |
            (!s.layout_valid ? 4 : 0) | (!s.members_valid ? 8 : 0) |
            (s.actors.truncated ? 16 : 0) | (s.producers.truncated ? 32 : 0);
        if (info.public_layout_truncated || menu.truncated || !s.layout_valid || !s.members_valid ||
            s.actors.truncated || s.producers.truncated) return reject(Unsupported::truncated);
        if (!matches(s.layout_clock) || !matches(s.actors.clock) || !matches(s.producers.clock) ||
            !matches(s.kit.clock) || !matches(s.spatial.clock)) return reject(Unsupported::stale_frame);
        if (s.spatial.information != &info || s.spatial.menu != &menu) return reject(Unsupported::stale_frame);
        if (!s.actors.roster_complete || !s.actors.own_observed || !s.kit.exact || s.actors.allied_count < 1 ||
            s.actors.allied_count > 5 || s.actors.enemy_count < 0 || s.actors.enemy_count > 5 ||
            s.actors.count < 0 || s.actors.count > 9 || s.producers.count < 0 || s.producers.count > 4 ||
            menu.single_count < 1 || menu.single_count > 8 || menu.count < menu.single_count || menu.count > max_jobs)
            return reject(Unsupported::roster);
        const auto& own = s.actors.own;
        if (!fresh(own.provenance, info.round, Source::own_state) || own.id != info.own.id || !own.alive ||
            own.health != info.own.health || own.maximum_health != info.own.maximum_health || own.bag != info.own.bag ||
            own.position != info.own.position || !std::isfinite(s.kit.strength) || s.kit.strength <= 0 ||
            s.kit.strength != own.strength || own.attack_range != s.kit.attack_range) return reject(Unsupported::stale_frame);
        if (!s.spatial.current_complete || !s.spatial.topology.complete || !s.spatial.topology.walkable ||
            !s.spatial.occupancy || !s.spatial.travel.distance) return reject(Unsupported::geometry);
#if PLAN4_FOUNDRY_PREP_TRACE
        prep_trace_sample(1);
#endif
#if PLAN4_FOUNDRY_ACTIVE_STORAGE
        // Only counted input slots and individually prepared targets can be
        // consumed. Reset each such slot before writing it; do not initialize
        // unused capacity on the current planning reserve.
        s.input.group_count = 0;
        s.plan.escape_endpoint = {}; s.plan.escape_endpoint_supported = false;
#else
        s.input = {}; s.plan = {};
#endif
        s.plan.information = &info; s.plan.menu = menu;
        s.input.round = info.round; s.input.horizon = horizon(info); s.input.settings = info.settings;
        s.input.our_score = info.our_score; s.input.enemy_score = info.enemy_score;
        s.input.travel = {supported_distance, this}; s.input.work_budget = info.work_budget;
        s.input.altar_count = info.altar_count;
        for (int a = 0; a < info.altar_count; ++a) s.input.altars[a] = info.altars[a];
        s.input.actor_count = 1;
        auto& token = s.input.actors[0];
#if PLAN4_FOUNDRY_ACTIVE_STORAGE
        token = {};
#endif
        token.id = own.id; token.own = token.visible = token.alive = true;
        token.position = token.attack_position = token.capture_position = own.position;
        token.health = own.health; token.maximum_health = own.maximum_health; token.bag = own.bag;
        // Visible Strength is a declared nominal ordinary-work quote. No
        // teammate inventory, passive multiplier or ability exclusion is read.
        token.work_per_action = s.kit.strength; token.observation = own.provenance;
        token.recall_destination = info.own_spawn;
#if PLAN4_FOUNDRY_PREP_TRACE
        prep_trace_sample(2);
#endif
        for (int n = 0; n < menu.single_count; ++n) {
            if (!budget(info)) return false;
            const int index = menu.jobs[n].group;
            if (index < 0 || index >= info.group_count) return reject(Unsupported::composition);
            bool duplicate = false;
            for (int g = 0; g < s.input.group_count; ++g) duplicate |= s.source_indices[g] == index;
            if (duplicate) continue;
            const auto& observed = info.groups[index]; const auto* layout = public_group(observed.id);
            if (!layout || !layout->complete || layout->count < 1 || layout->count > 32 ||
                layout->first < 0 || layout->first + layout->count > s.member_count) return reject(Unsupported::composition);
            float full = 0; int camp_range = 0;
            for (int m = 0; m < layout->count; ++m) {
                if (!budget(info)) return false;
                const auto& member = s.members[layout->first + m];
                if (member.group_id != observed.id || member.ordinal != m || member.declared_members != layout->count)
                    return reject(Unsupported::composition);
                if (member.member_id < 0 || !std::isfinite(member.maximum_health) || member.maximum_health <= 0 || member.attack_range < 0)
                    return reject(Unsupported::composition);
                for (int n = 0; n < m; ++n) if (s.members[layout->first + n].member_id == member.member_id ||
                    s.members[layout->first + n].spawn == member.spawn) return reject(Unsupported::composition);
                full += member.maximum_health;
                camp_range = std::max(camp_range, member.attack_range);
            }
            if (full != observed.full_health) return reject(Unsupported::composition);
            if (!observed.visible || !fresh(observed.provenance, info.round, Source::personal_vision) ||
                !fresh(observed.health_provenance, info.round, Source::personal_vision) || !observed.occupancy_known ||
                observed.lifecycle == Lifecycle::unknown || observed.contested) return reject(Unsupported::unknown_stock);
#if PLAN4_FOUNDRY_PREP_TRACE
            if (n == 0) prep_trace_sample(3);
#endif
            const int local = s.input.group_count++; s.source_indices[local] = index;
            auto& group = s.input.groups[local];
#if PLAN4_FOUNDRY_ACTIVE_STORAGE
            group = {};
#endif
            group.id = observed.id; group.position = observed.position;
            group.health = observed.health; group.full_health = observed.full_health;
            group.incoming_per_round = observed.incoming_damage; group.full_incoming_per_round = observed.full_incoming_damage;
            group.gems = observed.gems; group.gold = observed.gold; group.respawn_rounds = observed.respawn_rounds;
            group.attack_range = camp_range;
            group.health_known = true; group.health_provenance = observed.health_provenance;
            group.reward_available = observed.lifecycle == Lifecycle::living || (observed.lifecycle == Lifecycle::cleared && observed.reward_available);
            group.respawn_remaining = observed.respawn_remaining;
            if (info.in_progress_group_id == observed.id && observed.capture_started_round >= 0 &&
                info.own.position == observed.position)
                group.capture_remaining = std::max(0, observed.capture_started_round + info.settings.capture_rounds - info.round);
            if (member_spawn(*layout, own.position)) {
                token.group = local; token.on_target_spawn = token.capture_position_is_public_spawn = true;
                token.capture_position = own.position;
                token.intent = observed.lifecycle == Lifecycle::cleared ? foundry::Intent::hold : foundry::Intent::attack;
            }
        }
        // Admit only direct personally visible cleared-spawn holds. A living
        // health drop never manufactures the unavailable exclusion premises.
        for (int p = 0; p < s.producers.count; ++p) {
            if (!budget(info)) return false;
            const auto& producer = s.producers.samples[p]; const VisibleActor* actor = nullptr;
            for (int a = 0; a < s.actors.count; ++a) if (s.actors.samples[a].id == producer.actor_id && s.actors.samples[a].ally) actor = &s.actors.samples[a];
            if (!actor || actor->id == own.id) continue;
            if (producer.evidence != ProductionEvidence::visible_cleared_spawn || !producer.producing || producer.ambiguous ||
                !producer.personally_visible || !fresh(producer.provenance, info.round, Source::personal_vision) ||
                !fresh(actor->provenance, info.round, Source::personal_vision) || !actor->alive || actor->position != producer.position ||
                s.spatial.occupancy(actor->position, s.spatial.occupancy_context) != TileOccupancy::occupied)
                return reject(Unsupported::participation);
            int local = -1;
            for (int g = 0; g < s.input.group_count; ++g) if (s.input.groups[g].id == producer.group_id) local = g;
            if (local < 0) return reject(Unsupported::composition); // Optional layout currently contains menu sources only.
            if (s.input.groups[local].health != 0 || !s.input.groups[local].reward_available) return reject(Unsupported::participation);
            const auto* layout = public_group(producer.group_id);
            if (!layout || !member_spawn(*layout, actor->position) || s.input.actor_count == foundry::actor_limit) return reject(Unsupported::composition);
            for (int a = 0; a < s.input.actor_count; ++a) if (s.input.actors[a].id == actor->id) return reject(Unsupported::roster);
            auto& ally = s.input.actors[s.input.actor_count++];
#if PLAN4_FOUNDRY_ACTIVE_STORAGE
            ally = {};
#endif
            ally.id = actor->id; ally.group = local; ally.intent = foundry::Intent::hold;
            ally.position = ally.attack_position = ally.capture_position = actor->position;
            ally.health = actor->health; ally.maximum_health = actor->maximum_health; ally.bag = actor->bag;
            ally.visible = ally.alive = ally.capture_position_is_public_spawn = ally.on_target_spawn = true;
            ally.initial_turn_pending = !actor->completed_turn; ally.observation = actor->provenance;
        }
        std::array<const VisibleActor*, foundry::actor_limit> actors{}; actors[0] = &own;
        for (int a = 1; a < s.input.actor_count; ++a) for (int n = 0; n < s.actors.count; ++n)
            if (s.actors.samples[n].id == s.input.actors[a].id) actors[a] = &s.actors.samples[n];
        for (int a = 0; a < s.input.actor_count; ++a) {
            if (!actors[a] || actors[a]->current_priority != actors[a]->next_priority)
                return reject(Unsupported::changed_order);
            bool in_roster = false;
            for (int n = 0; n < s.actors.allied_count; ++n) in_roster |= s.actors.allied_roster[n] == actors[a]->id;
            if (!in_roster) return reject(Unsupported::roster);
            if (s.actors.draft_order_known && (actors[a]->draft_ordinal < 0 || actors[a]->draft_ordinal >= s.actors.allied_count))
                return reject(Unsupported::changed_order);
            int rank = 0;
            for (int b = 0; b < s.input.actor_count; ++b) if (actors[b] && b != a) {
                if (actors[b]->current_priority != actors[a]->current_priority)
                    rank += actors[b]->current_priority > actors[a]->current_priority;
                else if (s.actors.draft_order_known) {
                    if (actors[b]->draft_ordinal == actors[a]->draft_ordinal) return reject(Unsupported::roster);
                    rank += actors[b]->draft_ordinal < actors[a]->draft_ordinal;
                } else if (a == 0) rank += actors[b]->completed_turn;
                else if (b == 0) rank += !actors[a]->completed_turn;
                else {
                    if (actors[a]->completed_turn == actors[b]->completed_turn) return reject(Unsupported::changed_order);
                    rank += actors[b]->completed_turn;
                }
            }
            s.input.actors[a].order = rank;
        }
        for (int a = 1; a < s.input.actor_count; ++a)
            if ((s.input.actors[a].order < s.input.actors[0].order) != actors[a]->completed_turn) return reject(Unsupported::changed_order);
        for (int g = 0; g < s.input.group_count; ++g) {
            if (!budget(info)) return false;
            const auto& observed = info.groups[s.source_indices[g]]; auto& group = s.input.groups[g];
            const auto* layout = public_group(group.id); Point monster = group.position;
            if (observed.lifecycle == Lifecycle::living) {
                int alive = 0; float sum = 0;
                for (int m = 0; m < layout->count; ++m) {
                    if (!budget(info)) return false;
                    const auto& expected = s.members[layout->first + m]; const VisibleMember* current = nullptr;
                    for (int v = 0; v < s.visible_count; ++v) {
                        if ((v % 8) == 0 && !budget(info)) return false;
                        if (s.visible_members[v].member_id == expected.member_id && s.visible_members[v].group_id == group.id) {
                            if (current) return reject(Unsupported::composition);
                            current = &s.visible_members[v];
                        }
                    }
                    if (!current || !fresh(current->provenance, info.round, Source::personal_vision) || current->spawn != expected.spawn ||
                        current->declared_members != layout->count || current->ordinal != m || !std::isfinite(current->health) || current->health < 0)
                        return reject(Unsupported::unknown_stock);
                    sum += current->health; if (current->health > 0) { ++alive; monster = current->spawn; }
                }
                if (alive != 1 || sum != observed.health) return reject(Unsupported::composition);
                group.position = monster;
            }
            unsigned modeled = 0;
            for (int a = 0; a < s.input.actor_count; ++a) modeled += s.input.actors[a].group == g && s.input.actors[a].on_target_spawn;
            group.unmodeled_occupant = observed.occupied_spawn && (!modeled || observed.eligible_allied_occupants > modeled);
            if (group.unmodeled_occupant) return reject(Unsupported::occupancy);
            if (!waypoints(info, g, *layout, monster)) return false;
        }
        s.plan.escape_endpoint = info.own.position;
        s.plan.escape_endpoint_supported = s.spatial.own_recall_here && info.own.escape_rounds == 0;
#if PLAN4_FOUNDRY_PREP_TRACE
        prep_trace_sample(4);
#endif
        return true;
    }
public:
    explicit Foundry(bool enabled = true) : enabled_(enabled) {}
    const char* name() const { return "foundry"; }
    void observe_actors(const ActorFrame& frame) { invalidate_receipts(); storage_->actors = frame; }
    void observe_producers(const ProducerFrame& frame) { invalidate_receipts(); storage_->producers = frame; }
    void observe_combat(const CombatKit& frame) { invalidate_receipts(); storage_->kit = frame; }
    void observe_spatial(const SpatialFrame& frame) { invalidate_receipts(); storage_->spatial = frame; }
    void observe_layout(const PublicLayoutFrame& frame) {
        invalidate_receipts();
        auto& s = *storage_; s.layout_valid = false; s.group_count = s.member_count = 0; s.layout_clock = frame.clock;
        if (frame.truncated || frame.group_count < 0 || frame.group_count > 8 || frame.member_count < 0 || frame.member_count > 256 ||
            (frame.group_count && !frame.groups) || (frame.member_count && !frame.members)) return;
        for (int g = 0; g < frame.group_count; ++g) s.groups[g] = frame.groups[g];
        for (int m = 0; m < frame.member_count; ++m) s.members[m] = frame.members[m];
        s.group_count = frame.group_count; s.member_count = frame.member_count; s.layout_valid = true;
    }
    void observe_snapshot(const Information&, MemberFrame frame = {}) {
        invalidate_receipts();
        auto& s = *storage_; s.members_valid = false; s.visible_count = 0;
        if (frame.truncated || frame.count < 0 || frame.count > 256 || (frame.count && !frame.samples)) return;
        for (int m = 0; m < frame.count; ++m) s.visible_members[m] = frame.samples[m];
        s.visible_count = frame.count; s.members_valid = true;
    }
    void evaluate_into(const Information& info, const Menu& menu, Evaluation& out) {
#if PLAN4_FOUNDRY_PREP_TRACE
        prep_trace_begin(info);
        struct TraceEnd {
            Foundry& model; const Information& info; const Evaluation& out;
            ~TraceEnd() { model.prep_trace_emit(info, out); }
        } trace_end{*this, info, out};
#endif
        invalidate_receipts(); ++revision_; bound_allies_ = completed_paths_ = receipt_work_ = 0;
        out.menu = menu; out.chosen = -1; out.evaluated_count = 0; out.budget_exhausted = false;
        if (!bind(info, menu)) {
            if (unsupported_ == Unsupported::budget) out.budget_exhausted = true;
            else plan4::evaluate_into(info, menu, out);
            return;
        }
        bound_allies_ = storage_->input.actor_count - 1;
        for (int n = 0; n < menu.count; ++n) {
            if (!budget(info)) { out.evaluated_count = 0; out.budget_exhausted = true; return; }
            storage_->plan.first = menu.jobs[n];
#if PLAN4_FOUNDRY_PREP_TRACE
            if (n == 0) prep_trace_sample(5);
#endif
            bool supported = foundry::predict_fixed_into(storage_->input, storage_->plan, enabled_, *path_, out.forecasts[n]);
#if PLAN4_FOUNDRY_PREP_TRACE
            if (n == 0) prep_trace_sample(6);
#endif
            for (int a = 1; a < storage_->input.actor_count; ++a) supported &= !path_->actor(a).fallback;
            if (!supported) {
                if (out.forecasts[n].budget_exhausted) { unsupported_ = Unsupported::budget; out.evaluated_count = 0; out.budget_exhausted = true; return; }
                unsupported_ = Unsupported::forecast; plan4::evaluate_into(info, menu, out); return;
            }
            if (!cache_receipts(info, menu.jobs[n], n)) {
                out.evaluated_count = 0; out.budget_exhausted = true; return;
            }
            ++out.evaluated_count;
            ++completed_paths_;
        }
        coupled_ = true; out.chosen = choose(info, menu, out.forecasts);
        auto& s = *storage_; s.receipt_source = &info; s.receipt_round = info.round; s.receipt_actor = info.own.id;
        s.receipt_position = info.own.position; s.receipt_health = info.own.health; s.receipt_bag = info.own.bag;
        s.receipt_our_score = info.our_score; s.receipt_enemy_score = info.enemy_score; s.receipt_count = menu.count;
        s.receipt_chosen = out.chosen;
    }
    ModelEvidence model_evidence() const {
        return {true, used_foundry(), enabled_, int(unsupported_), bound_allies_,
                unsupported_ == Unsupported::none ? 0 : 1, completed_paths_, receipt_work_, revision_};
    }
    void prediction_receipts(const Information& info, const Job& job, PredictionEventSink sink, void* context) const {
        const auto& s = *storage_;
        if (!sink || !coupled_ || s.receipt_source != &info || s.receipt_round != info.round || s.receipt_actor != info.own.id ||
            s.receipt_position != info.own.position || s.receipt_health != info.own.health || s.receipt_bag != info.own.bag ||
            s.receipt_our_score != info.our_score || s.receipt_enemy_score != info.enemy_score) return;
        for (int n = 0; n < s.receipt_count; ++n) {
            if (n != s.receipt_chosen) continue;
            const auto& prefix = s.receipts[n]; const auto& key = prefix.job;
            if (key.kind != job.kind || key.group != job.group || key.second_group != job.second_group || key.altar != job.altar) continue;
            for (int r = 0; r < prefix.count; ++r) {
                if (info.work_budget.can_refine && !info.work_budget.can_refine(info.work_budget.context)) return;
                if (!sink(prefix.events[r], context)) return;
            }
            return;
        }
    }
    Unsupported unsupported() const { return unsupported_; }
    int truncation_mask() const { return truncation_mask_; }
    bool used_foundry() const { return coupled_; }
    const foundry::Input& binding() const { return storage_->input; }
    const foundry::FixedPlan& binding_plan() const { return storage_->plan; }
};
} // namespace plan4::runtime
