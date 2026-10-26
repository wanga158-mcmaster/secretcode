#pragma once
// Included once, at the end of brain.cpp. This private experiment reuses that caller's
// evaluated alternatives, then makes one named, expiring two-body camp contract. E8/G8
// motivate the party; C22 forbids treating the invitation as already-arrived damage.
#include "party_model.h"

namespace mb {
namespace {

constexpr int party_ttl = 60;
constexpr int party_arrival_limit = 10;
constexpr int party_choice_age = 24;
constexpr float party_damage_margin = 1.25f;

struct PartyChoice { int group = -1; float score = 0; int round = -1000; };
struct PartyState {
    party::Lease lease;
    PartyChoice alternatives[4];
    int choice_round = -1000;
    int phase = 0; // 0 none, 1 offered/accepted, 2 locally observed commitment
    int saved_target = -1;
    float saved_score = 0;
    int saved_need = 1;
    int displaced_group = -1;
    float displaced_score = 0;
    int released_round = -1000;
    int best_distance = 1 << 20;
    int progress_round = -1000;
    int last_trace_round = -1000;
    int traces = 0;
    bool priority_owned = false;
    int saved_priority = 0;
};
PartyState& party_state() { static PartyState state; return state; }

void party_restore_priority() {
    auto& state = party_state();
    if (!state.priority_owned) return;
    // Death does not license a self action; retain the restoration obligation until alive.
    if (!cambc::self().is_alive()) return;
    if (cambc::self().priority() != state.saved_priority)
        cambc::self().set_priority(state.saved_priority);
    state.priority_owned = false;
}

void party_trace(const Ctx& ctx, const char* reason, int group, float a = 0, float b = 0) {
    if constexpr (cfg::coalition_party_trace) {
        auto& s = party_state();
        // At most one line per actor/round, and a finite per-instance diagnostic allowance.
        if (s.traces < 72 && s.last_trace_round != ctx.round && fuel_ok(2500)) {
            ++s.traces; s.last_trace_round = ctx.round;
            MB_LOG("party r%d a%d g%d %s %.2f %.2f\n", ctx.round,
                   Knowledge::get().self_index, group, reason, a, b);
        }
    }
}

// Kept outside PartyState so scout=OFF retains the existing contract's allocation and
// initialization. Only the enabled fallback touches this diagnostic/scout state.
struct PartyScoutState {
    int round = -1000;
    int group = -1;
    int scanned = 0, total = 0;
    int route = -1, return_route = -1;
    float bound = 0, score = 0, displaced = 0, budget = 0;
    const char* reason = "none";
};
PartyScoutState& party_scout_state() { static PartyScoutState state; return state; }

void party_scout_trace(const Ctx& ctx, int target) {
    if constexpr (cfg::coalition_party_scout && cfg::coalition_party_trace) {
        auto& trace = party_state();
        const auto& scout = party_scout_state();
        if (scout.round != ctx.round || trace.traces >= 72 || trace.last_trace_round == ctx.round
            || !fuel_ok(2500)) return;
        ++trace.traces; trace.last_trace_round = ctx.round;
        MB_LOG("party r%d a%d g%d scout-%s alt%d scan%d/%d score%.3f route%d return%d cost%.2f budget%.2f\n",
            ctx.round, Knowledge::get().self_index, target, scout.reason, scout.group,
            scout.scanned, scout.total, scout.score, scout.route, scout.return_route,
            scout.displaced, scout.budget);
    }
}

party::Record party_read() {
    party::Bytes bytes{};
    const auto size = cambc::shared_memory_buffer::size();
    if (size < Knowledge::get().allies.size() + bytes.size()) return {};
    cambc::shared_memory.read(size - bytes.size(), std::span<std::uint8_t>{bytes});
    return party::Record::decode(bytes);
}
void party_write(party::Record record) {
    const auto bytes = record.encode();
    const auto size = cambc::shared_memory_buffer::size();
    if (size < Knowledge::get().allies.size() + bytes.size()) return;
    cambc::shared_memory.write(size - bytes.size(), std::span<const std::uint8_t>{bytes});
}
party::Slot party_slot(int actor) {
    const auto& slots = Knowledge::get().slots();
    if (actor < 0 || actor >= static_cast<int>(slots.size())) return {};
    const auto& s = slots[static_cast<std::size_t>(actor)];
    return {s.group, static_cast<party::Intent>(s.status)};
}
bool party_channel(cambc::champion_state state) {
    return state.is_dead() || state.contains(cambc::champion_state::recalling)
        || state.contains(cambc::champion_state::submitting_gems)
        || state.contains(cambc::champion_state::teleporting);
}

struct PartyActor {
    int index = -1;
    vec2 pos{};
    float hp = 0, max_hp = 0, strength = 0, focus = 0, multiplier = 1;
    int range = 1;
    cambc::champion_type type{};
    cambc::champion_state state{};
};
float party_multiplier(const cambc::champion& actor) {
    float multiplier = 1;
    bool jungle = false, durability = false;
    const auto counts = actor.item_count();
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(cambc::item_type::count); ++i) {
        const cambc::item_type item{i};
        if (counts[item] == 0) continue;
        const auto passive = item.passive();
        if (!passive) continue;
        if ((*passive == cambc::item_passive::jungle && !jungle)
            || (*passive == cambc::item_passive::durability && !durability)) {
            (*passive == cambc::item_passive::jungle ? jungle : durability) = true;
            multiplier *= 1 - item.passive_magnitude();
        }
    }
    return multiplier;
}
bool party_actor(const Ctx& ctx, int index, PartyActor& out) {
    const auto& kn = Knowledge::get();
    if (index < 0 || index >= static_cast<int>(kn.allies.size())) return false;
    if (index == kn.self_index) {
        out = {index, ctx.pos, ctx.hp, ctx.max_hp, ctx.strength, ctx.focus,
               ctx.camp_damage_taken_mult, kit().attack_range, kit().type, ctx.state};
    } else {
        const auto& actor = kn.allies[static_cast<std::size_t>(index)];
        // The roster is shared; its vision and current allowances are not.
        if (!actor.is_alive() || !actor.is_visible()) return false;
        const auto pos = actor.position();
        if (!ctx.visible.is_set(pos)) return false;
        out = {index, pos, actor.health(), actor.max_health(), actor.strength(), actor.focus(),
               party_multiplier(actor), actor.attack_range(), actor.champion_name(), actor.state()};
    }
    const auto& actor = kn.allies[static_cast<std::size_t>(index)];
    return out.hp > 0 && !party_channel(out.state) && !actor.buffs().has_stun();
}
bool party_peer_basic_ready(const PartyActor& peer) {
    // A completed owner has no second current-turn basic; its next normal owner turn
    // restores that allowance. The caller already queried life, channel and fresh stun.
    return peer.state.contains(cambc::champion_state::can_attack)
        || peer.state.contains(cambc::champion_state::completed_turn);
}

float party_value(int group) {
    const auto& info = World::get().groups[static_cast<std::size_t>(group)];
    // Fixed common captured-resource currency. This is neither a 50/50 payout nor banked score;
    // personal passives, jackpot preferences and carried-gem utilities are deliberately absent.
    return cfg::gold_value * static_cast<float>(info.gold)
        + cfg::gem_value * static_cast<float>(info.gems);
}
int party_distance(const cambc::distance_field* field, vec2 at) {
    if (!field) return -1; // never license a contract with the Chebyshev fallback
    const auto d = field->get(at);
    return d == cambc::distance_field::unreachable ? -1 : static_cast<int>(d);
}
int party_departure_distance(const cambc::distance_field* field, int group) {
    // Other groups' fields intentionally block this group's occupied member tiles. Its
    // geometric center can therefore be unreachable even though every attack/exit tile is
    // reachable. Price a named finite perimeter exit, not that blocked-center artifact.
    int best = 1 << 20;
    for (const auto tile : World::get().groups[static_cast<std::size_t>(group)].tiles)
        for (const auto direction : kDirs) {
            const vec2 exit{tile.x + direction.x, tile.y + direction.y};
            if (exit.x < 0 || exit.y < 0 || exit.x >= World::get().width || exit.y >= World::get().height) continue;
            const int d = party_distance(field, exit);
            if (d >= 0) best = std::min(best, d);
        }
    return best == (1 << 20) ? -1 : best;
}
float party_dps(const PartyActor& actor, float neighbours) {
    auto summary = design_summary(actor.type);
    summary.strength = actor.strength;
    summary.focus = actor.focus;
    // Same sustained-kit approximation as the ordinary chooser, without hypothetical bodies.
    // It is a calibrated planning belief; it is not a turn-exact damage certificate.
    return model::camp_dps(summary, neighbours, cfg::camp_dash_damage);
}
bool party_reaches(const Ctx& ctx, int group, const PartyActor& actor) {
    bool harmful = false;
    for (const auto& camp : ctx.camps) if (camp.group == group && camp.hp > 0 && camp.strength > 0) {
        harmful = true;
        if (cheb(actor.pos, camp.pos) > actor.range) return false;
    }
    return harmful;
}

struct PartyPlan {
    bool valid = false, arrived = false, safe = false;
    int self_travel = -1, peer_travel = -1;
    float self_damage = 0, peer_damage = 0;
    float prefix = 0;
    model::FightForecast fight;
    const cambc::distance_field* field = nullptr;
};
PartyPlan party_plan(const Ctx& ctx, int group, const PartyActor& self, const PartyActor& peer,
                     bool committed) {
    PartyPlan plan;
    auto& world = World::get();
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    if (belief.seen_round != ctx.round || belief.status != GroupStatus::alive) return plan;
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    if (info.harmless || info.has_monolith || info.tiles.size() > 10) return plan;
    // Every member tile must be personally visible: partial group HP is not a clear model.
    for (const auto tile : info.tiles) if (!ctx.visible.is_set(tile)) return plan;
    const auto monsters = live_monsters(ctx, group);
    // The initial actual executor has no assigned harmless clean-up suffix. Do not admit
    // mixed groups and then abandon surviving clusters when harmful-member reach disappears.
    if (monsters.size() != 1 || monsters.front().strength <= 0) return plan;
    plan.field = world.group_field(group, true, 18000);
    const int sd = party_distance(plan.field, self.pos);
    const int pd = party_distance(plan.field, peer.pos);
    if (sd < 0 || pd < 0) return plan;
    // The root route is a conservative occupation-price proxy. Actual arrival below uses
    // fresh basic reach, never this ETA, and waiting uses only the difference in these costs.
    plan.self_travel = party_reaches(ctx, group, self) ? 0 : std::max(0, sd - 1);
    plan.peer_travel = party_reaches(ctx, group, peer) ? 0 : std::max(0, pd - 1);
    if (!committed && std::max(plan.self_travel, plan.peer_travel) > party_arrival_limit) return plan;
    plan.arrived = party_reaches(ctx, group, self) && party_reaches(ctx, group, peer);
    const float dps = party_dps(self, info.neighbours) + party_dps(peer, info.neighbours);
    plan.fight = model::forecast_camp_fight(monsters, dps);
    const int cycle_bound = std::max(plan.self_travel, plan.peer_travel) + plan.fight.rounds
        + cambc::constants::capture_time + 1 + 2 + 2;
    if (plan.fight.rounds <= 0 || cycle_bound > party_ttl) return plan;
    float other_rate = 0;
    for (const auto& camp : ctx.camps)
        if (camp.group != group && camp.awake && camp.strength > 0) other_rate += camp.strength;
    for (int i = 0; i < ctx.n_unseen; ++i) other_rate += ctx.unseen[i].strength;
    // Unknown paths through awake camps cannot be certified. Price a conservative known
    // prefix for ALL currently awake camps, then resample every owner turn while staging.
    plan.prefix = other_rate * static_cast<float>(std::max(plan.self_travel, plan.peer_travel) + 1);
    const float already_awake = belief.awake ? plan.fight.peak_rate : 0;
    const float exposure = party_damage_margin * plan.fight.damage;
    plan.self_damage = (exposure + already_awake + plan.prefix) * self.multiplier;
    plan.peer_damage = (exposure + already_awake + plan.prefix) * peer.multiplier;
    // Inherited melee execution does not rotate tanks. Each potentially targeted member
    // covers the WHOLE remaining burden, and keeps the caller's recall threshold plus exit.
    const float self_pool = self.hp - cfg::recall_health_frac * self.max_hp;
    const float peer_pool = peer.hp - cfg::recall_health_frac * peer.max_hp;
    plan.safe = party::individual_wave_survives(std::max(0.0f, self_pool), 0,
                    plan.self_damage, 2 * plan.fight.peak_rate * self.multiplier)
        && party::individual_wave_survives(std::max(0.0f, peer_pool), 0,
                    plan.peer_damage, 2 * plan.fight.peak_rate * peer.multiplier);
    plan.valid = true;
    return plan;
}

struct PartyRotation {
    bool supported = false;
    bool arrived = false;
    const CampView* elk = nullptr;
    PartyActor leader, helper;
    party::RotationForecast forecast;
};

bool party_no_other_camp(const Ctx& ctx, int group, vec2 a, vec2 b) {
    if (ctx.n_unseen != 0 || ctx.hit_unseen) return false;
    const auto& world = World::get();
    // Camps are stationary. Check static possible firing positions near either actor so an
    // unseen but in-range camp is not silently assigned zero damage by the visible snapshot.
    for (std::size_t g = 0; g < world.groups.size(); ++g) {
        if (static_cast<int>(g) == group) continue;
        const auto& info = world.groups[g];
        if (info.harmless) continue;
        for (std::size_t m = 0; m < info.tiles.size(); ++m) {
            const auto& stats = world.camp_stats[info.types[m]];
            if (stats.strength <= 0 || (cheb(info.tiles[m], a) > stats.range
                && cheb(info.tiles[m], b) > stats.range)) continue;
            if (!ctx.visible.is_set(info.tiles[m])) return false;
        }
    }
    for (const auto& camp : ctx.camps)
        if (camp.group != group && camp.awake && camp.strength > 0
            && (cheb(camp.pos, a) <= camp.range || cheb(camp.pos, b) <= camp.range)) return false;
    return true;
}

party::RotationForecast party_rotation_bound(const PartyActor& leader, const PartyActor& helper,
                                              const CampView& elk) {
    auto out = party::rotate_remaining_wave(leader.hp, helper.hp, elk.strength,
        leader.multiplier, leader.strength, helper.strength, elk.hp);
    if (!out.valid) return out;
    const float volley = elk.strength * leader.multiplier;
    const float hp_after_round = elk.hp - leader.strength - helper.strength;
    // While one actor is between its own setting and its mate's later turn, NEXT priorities
    // can transiently disagree. Bound EITHER first future tank, then the verified healthier
    // policy, rather than credit an immediate resort or assume the later self-only write.
    for (int first = 0; first < 2; ++first) {
        float hp[2]{leader.hp, helper.hp};
        hp[first] -= volley;
        if (hp[0] <= 0 || hp[1] <= 0) { out.safe = false; return out; }
        float damage[2]{first == 0 ? volley : 0, first == 1 ? volley : 0};
        if (hp_after_round > 0) {
            const auto tail = party::rotate_remaining_wave(hp[0], hp[1], elk.strength,
                leader.multiplier, leader.strength, helper.strength, hp_after_round);
            if (!tail.valid || !tail.safe) { out.safe = false; return out; }
            damage[0] += tail.damage[0]; damage[1] += tail.damage[1];
        }
        for (int i = 0; i < 2; ++i) out.damage[i] = std::max(out.damage[i], damage[i]);
    }
    out.hp[0] = leader.hp - out.damage[0]; out.hp[1] = helper.hp - out.damage[1];
    out.safe = out.hp[0] > out.escape_reserve && out.hp[1] > out.escape_reserve;
    return out;
}

PartyRotation party_rotation(const Ctx& ctx, int group, const PartyActor& self,
                             const PartyActor& peer, party::Record record) {
    PartyRotation out;
    if constexpr (!cfg::coalition_party_rotation) return out;
    if (group < 0 || !record.valid() || !ctx.enemies.empty() || party_channel(self.state)
        || party_channel(peer.state) || self.range != 1 || peer.range != 1
        || std::abs(self.multiplier - peer.multiplier) > 0.0001f) return out;
    // Varg's ordinary splash can wake another sleeping group beside the Elk. The first
    // rotation model admits no such extra incoming source, so reject BOTH possible callers
    // until a personally visible full-footprint action model licenses that broader kit.
    if (self.type == cambc::champion_type::varg || peer.type == cambc::champion_type::varg) return out;
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    if (belief.seen_round != ctx.round || belief.status != GroupStatus::alive) return out;
    int living = 0;
    for (const auto& camp : ctx.camps) if (camp.group == group && camp.hp > 0) {
        ++living;
        if (camp.strength > 0 && camp.camp->is_elk()) out.elk = &camp;
    }
    if (living != 1 || !out.elk) return out;
    for (const auto tile : World::get().groups[static_cast<std::size_t>(group)].tiles)
        if (!ctx.visible.is_set(tile)) return out;
    if (!party_no_other_camp(ctx, group, self.pos, peer.pos)) return out;
    out.leader = self.index == record.pair.leader ? self : peer;
    out.helper = self.index == record.pair.helper ? self : peer;
    out.arrived = cheb(out.leader.pos, out.elk->pos) == 1
        && cheb(out.helper.pos, out.elk->pos) == 1;
    out.forecast = party_rotation_bound(out.leader, out.helper, *out.elk);
    out.supported = out.forecast.valid;
    return out;
}

int party_rotation_priority(const PartyRotation& rotation, int actor) {
    const bool leader_tanks = !(rotation.helper.hp > rotation.leader.hp + 0.1f);
    return ((actor == rotation.leader.index) == leader_tanks) ? 100 : 0;
}
void party_configure_priority(const PartyRotation& rotation) {
    auto& state = party_state();
    auto& me = cambc::self();
    if (!state.priority_owned) { state.saved_priority = me.priority(); state.priority_owned = true; }
    const int wanted = party_rotation_priority(rotation, Knowledge::get().self_index);
    if (me.priority() != wanted) me.set_priority(wanted);
}
bool party_priorities_ready(const PartyRotation& rotation) {
    const auto& allies = Knowledge::get().allies;
    return allies[static_cast<std::size_t>(rotation.leader.index)].priority()
            == party_rotation_priority(rotation, rotation.leader.index)
        && allies[static_cast<std::size_t>(rotation.helper.index)].priority()
            == party_rotation_priority(rotation, rotation.helper.index);
}

bool party_stage(Ctx& ctx, TurnActions& acts, int group, const PartyPlan& plan) {
    if (!cambc::self().can_move() || !plan.field) return false;
    // Sleeping camps do not attack proximity. Walk into ACTUAL future basic reach without
    // attacking; original go_stage holds outside range and would never satisfy the handshake.
    auto score = [&](vec2 tile) {
        const int distance = party_distance(plan.field, tile);
        if (distance < 0) return -(1 << 28);
        int worst_gap = 0;
        for (const auto& camp : ctx.camps) if (camp.group == group && camp.hp > 0 && camp.strength > 0)
            worst_gap = std::max(worst_gap, cheb(tile, camp.pos) - kit().attack_range);
        int incoming = 0;
        for (const auto& camp : ctx.camps)
            if (camp.group != group && camp.awake && cheb(tile, camp.pos) <= camp.range)
                incoming += static_cast<int>(camp.strength);
        return (worst_gap <= 0 ? 100000 : -100 * distance - worst_gap) - 1000 * incoming;
    };
    if (step_best(score, score(ctx.pos))) {
        acts.moved = true; ctx.refresh_self(); return true;
    }
    return false;
}

} // namespace

bool Brain::party_continuation(const Ctx& ctx) {
    if constexpr (!cfg::coalition_party || !cfg::coalition_party_rotation || cfg::coalition_focus) return false;
    const auto& state = party_state();
    if (!state.lease.valid() || state.phase != 2 || !fuel_ok(10000)
        || !ctx.enemies.empty() || party_channel(ctx.state) || ctx.hit_unseen) return false;
    const auto shared = party_read();
    if (!party::active(state.lease, shared, ctx.round, party_ttl)) return false;
    const int group = state.lease.group;
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    if (belief.seen_round != ctx.round) return false;
    if (belief.status == GroupStatus::reward) {
        // The proven damaging wave is finished. Preserve the factual designated capture/
        // helper release through this zero-danger suffix instead of recalling at 50% HP.
        for (const auto tile : World::get().groups[static_cast<std::size_t>(group)].tiles)
            if (!ctx.visible.is_set(tile)) return false;
        return party_no_other_camp(ctx, group, ctx.pos, ctx.pos);
    }
    const int self_index = Knowledge::get().self_index;
    const int peer_index = self_index == shared.pair.leader ? shared.pair.helper : shared.pair.leader;
    PartyActor self, peer;
    if (!party_actor(ctx, self_index, self) || !party_actor(ctx, peer_index, peer)) return false;
    const auto rotation = party_rotation(ctx, group, self, peer, shared);
    if (!rotation.supported || !rotation.arrived || !rotation.forecast.safe
        || !self.state.contains(cambc::champion_state::can_attack)
        || !party_peer_basic_ready(peer)) return false;
    const auto leader_slot = party_slot(shared.pair.leader), helper_slot = party_slot(shared.pair.helper);
    if (leader_slot.group != group || helper_slot.group != group
        || leader_slot.intent != party::Intent::engaging || helper_slot.intent != party::Intent::engaging) return false;
    // Each setting is self-only and persists. A tuple whose priority ownership was never
    // established cannot suppress the ordinary recall rule.
    const auto& allies = Knowledge::get().allies;
    const int lp = allies[static_cast<std::size_t>(shared.pair.leader)].priority();
    const int hp = allies[static_cast<std::size_t>(shared.pair.helper)].priority();
    return state.priority_owned && (lp == 0 || lp == 100) && (hp == 0 || hp == 100);
}

void Brain::begin_party_choices(const Ctx& ctx) {
    if constexpr (cfg::coalition_party && !cfg::coalition_focus) {
        auto& state = party_state();
        state.choice_round = ctx.round;
        // Keep named previous alternatives for a bounded one-candidate recheck. A truncated
        // chooser does not establish the global best displaced task.
        for (auto& choice : state.alternatives)
            if (ctx.round - choice.round > party_choice_age) choice = {};
    }
}

void Brain::note_party_choice(const Ctx& ctx, int group, float score, int need) {
    if constexpr (cfg::coalition_party && !cfg::coalition_focus) {
        if (group < 0 || need != 1 || !std::isfinite(score) || score <= 0) return;
        auto& choices = party_state().alternatives;
        PartyChoice* selected = &choices[0];
        for (auto& choice : choices) {
            if (choice.group == group) { selected = &choice; break; }
            if (choice.group < 0 || choice.score < selected->score) selected = &choice;
        }
        if (selected->group == group || selected->group < 0 || score > selected->score)
            *selected = {group, score, ctx.round};
    }
}

void Brain::cancel_party(const Ctx& ctx) {
    if constexpr (cfg::coalition_party && !cfg::coalition_focus) {
        auto& state = party_state();
        const auto shared = party_read();
        const int self = Knowledge::get().self_index;
        party_restore_priority();
        // A declining addressed helper must erase the invitation before ordinary farming can
        // accidentally publish its group as engaging and look like an ACK.
        // A released helper has no remaining production promise to cancel. Its preemption
        // must not erase the designated leader's already-cleared capture contract.
        if (state.phase == 3) return;
        if ((state.lease.valid() && shared == state.lease.record)
            || (shared.fresh(ctx.round, party_ttl) && shared.pair.helper == self)) party_write({});
        if (state.lease.valid()) {
            party_trace(ctx, "cancel", state.lease.group);
            publish(-1, static_cast<int>(SlotStatus::idle));
            if (target_ == state.lease.group) {
                target_ = -1; target_score_ = 0; target_need_ = 1;
                capturing_ = false; engaged_ = false;
            }
        }
        state.lease = {}; state.phase = 0;
    }
}

bool Brain::handle_party(Ctx& ctx, TurnActions& acts, bool existing_only) {
    if constexpr (!cfg::coalition_party || cfg::coalition_focus) return false;
    auto& state = party_state();
    auto& kn = Knowledge::get();
    auto& world = World::get();
    if (state.priority_owned && !state.lease.valid()) party_restore_priority();
    const int self_index = kn.self_index;
    if (kn.allies.size() > party::max_actors || ctx.round > party::max_round
        || cambc::shared_memory_buffer::size() < kn.allies.size() + 2) return false;
    if (!fuel_ok(14500)) { if (state.lease.valid()) cancel_party(ctx); return false; }
    auto shared = party_read();
    auto restore = [&] {
        const int old_group = state.lease.group;
        const int saved = state.saved_target;
        const float score = state.saved_score;
        const int need = state.saved_need;
        cancel_party(ctx);
        if (saved >= 0 && saved != old_group && saved < static_cast<int>(kn.groups.size())
            && kn.groups[static_cast<std::size_t>(saved)].status != GroupStatus::respawning) {
            target_ = saved; target_score_ = score; target_need_ = need;
        }
    };
    auto decline = [&](const char* reason, int group, float a = 0, float b = 0) {
        party_trace(ctx, reason, group, a, b);
        const bool escape = state.lease.valid() && group >= 0
            && group < static_cast<int>(kn.groups.size())
            && kn.groups[static_cast<std::size_t>(group)].awake
            && kn.groups[static_cast<std::size_t>(group)].status == GroupStatus::alive;
        if (state.lease.valid()) restore();
        else if (shared.fresh(ctx.round, party_ttl) && shared.pair.helper == self_index) party_write({});
        if (escape) { why_ = "party-abort"; retreat(ctx, acts); return true; }
        return false;
    };

    // Continue local contracts BEFORE generic reward/capture handling. This is deliberately
    // separate from accepting a new invitation; a shared committed frame alone is insufficient.
    if (state.lease.valid()) {
        const int group = state.lease.group;
        if (state.phase == 3) {
            // Release is an actual departure, never a second capturer. Keep the local tuple
            // briefly so the ordinary chooser cannot immediately reclaim the cleared group.
            // This member may execute its named independent task while the leader captures.
            if (!party::active(state.lease, shared, ctx.round, party_ttl)
                || ctx.round - state.released_round > cambc::constants::capture_time + 6
                || (kn.groups[static_cast<std::size_t>(group)].seen_round == ctx.round
                    && kn.groups[static_cast<std::size_t>(group)].status == GroupStatus::respawning)) {
                state.lease = {}; state.phase = 0;
            } else if (state.displaced_group >= 0
                && kn.groups[static_cast<std::size_t>(state.displaced_group)].status != GroupStatus::respawning) {
                if (target_ != state.displaced_group) capturing_ = false;
                target_ = state.displaced_group; target_need_ = 1;
                target_score_ = state.displaced_score;
                chosen_round_ = ctx.round;
                return false;
            } else {
                target_ = -1; publish(-1, static_cast<int>(SlotStatus::idle));
                why_ = "party-release-home"; retreat(ctx, acts); return true;
            }
        }
        if (!state.lease.valid()) return false;
        if (!party::active(state.lease, shared, ctx.round, party_ttl)) return decline("expired", group);
        const auto& belief = kn.groups[static_cast<std::size_t>(group)];
        if (belief.seen_round == ctx.round && belief.status == GroupStatus::reward) {
            party_restore_priority();
            if (self_index == shared.pair.helper) {
                // Leave the frame for the designated capturer; erasing it here would strand
                // the leader on the next turn. Our idle slot is a factual release, not capture.
                party_trace(ctx, "release-reward", group);
                publish(-1, static_cast<int>(SlotStatus::idle));
                target_ = state.displaced_group; target_need_ = 1; chosen_round_ = ctx.round;
                target_score_ = state.displaced_score;
                capturing_ = false; engaged_ = false; state.phase = 3; state.released_round = ctx.round;
                if (target_ >= 0) travel_to_group(ctx, acts, target_);
                return true;
            }
            party_trace(ctx, "capturer", group);
            target_ = group; engaged_ = false;
            bool on_tile = false;
            for (const auto tile : world.groups[static_cast<std::size_t>(group)].tiles)
                if (tile == ctx.pos) on_tile = true;
            if (on_tile && cfg::capture_patience > 0) {
                capture_hold_turns_ = last_hold_round_ == ctx.round - 1 ? capture_hold_turns_ + 1 : 1;
                last_hold_round_ = ctx.round;
                if (capture_hold_turns_ > cfg::capture_patience) {
                    cancel_party(ctx);
                    banned_until_[static_cast<std::size_t>(group)] = ctx.round + 40;
                    capture_hold_turns_ = 0; why_ = "party-capture-contested";
                    leave_camp_tiles(ctx, acts); return true;
                }
            }
            if (!go_capture(ctx, acts, group)) {
                cancel_party(ctx);
                banned_until_[static_cast<std::size_t>(group)] = ctx.round + 10;
                why_ = "party-capture-blocked"; leave_camp_tiles(ctx, acts);
            }
            return true;
        }
        if (belief.seen_round == ctx.round && belief.status == GroupStatus::respawning)
            return decline("capture-observed", group);
    } else if (existing_only) return false;

    // A new offer uses the current ordinary selection. The default prices only an evaluated
    // shortlist alternative; the separate scout arm can deliberately score one independent job.
    int group = state.lease.valid() ? state.lease.group : -1;
    const bool accepting = !state.lease.valid() && shared.fresh(ctx.round, party_ttl)
        && shared.pair.helper == self_index;
    if (accepting) {
        const auto leader = party_slot(shared.pair.leader);
        if (leader.intent != party::Intent::idle || leader.group < 0)
            return decline("unaccepted-commit", leader.group);
        group = leader.group;
    } else if (!state.lease.valid()) {
        if (shared.fresh(ctx.round, party_ttl)) return false; // one pair, one wave
        if (target_ < 0 || engaged_ || capturing_) return false;
        if constexpr (cfg::coalition_party_rotation) { if (target_need_ < 2) return false; }
        else if (target_need_ != 2) return false;
        group = target_;
    }
    if (group < 0 || group >= static_cast<int>(world.groups.size())) return decline("bad-group", group);
    if (!ctx.enemies.empty() || ctx.hit_unseen || party_channel(ctx.state)) return decline("preempt", group);
    if (!state.lease.valid() && (engaged_ || capturing_)) return decline("busy", group);

    PartyActor self, peer;
    if (!party_actor(ctx, self_index, self)) return decline("self-unavailable", group);
    party::Record record = state.lease.valid() ? state.lease.record : shared;
    if (!record.valid() || (!state.lease.valid() && !accepting)) {
        int chosen = -1, best = 1 << 20;
        const auto* field = world.group_field(group, true, 18000);
        for (std::size_t i = static_cast<std::size_t>(self_index + 1); i < kn.allies.size(); ++i) {
            const auto slot = party_slot(static_cast<int>(i));
            if (slot.intent == party::Intent::capturing || slot.group == group) continue;
            PartyActor candidate;
            if (!party_actor(ctx, static_cast<int>(i), candidate)) continue;
            const int distance = party_distance(field, candidate.pos);
            if (distance >= 0 && distance < best) { best = distance; chosen = static_cast<int>(i); peer = candidate; }
        }
        if (chosen < 0) return decline("no-named-helper", group);
        record = {party::pair_of(self_index, chosen), ctx.round};
    } else if (!party_actor(ctx, self_index == record.pair.leader ? record.pair.helper : record.pair.leader, peer))
        return decline("peer-unseen", group);
    // First arm: melee-only, so a legal approach step can share the attack turn. Ranged
    // movement/reset contribution is a separate calibrated executor seam, not assumed here.
    if (self.range != 1 || peer.range != 1) return decline("melee-pair-only", group);
    const auto leader_now = party_slot(record.pair.leader);
    const bool committed = state.lease.valid() && (state.phase == 2
        || (leader_now.group == group && leader_now.intent == party::Intent::engaging));
    auto plan = party_plan(ctx, group, self, peer, committed);
    if (!plan.valid) return decline("unknown-group-route", group);
    if (!committed && kn.groups[static_cast<std::size_t>(group)].awake) return decline("awake-before-commit", group);
    PartyRotation rotation;
    if constexpr (cfg::coalition_party_rotation) {
        rotation = party_rotation(ctx, group, self, peer, record);
        if (!rotation.supported) return decline("rotation-unsupported", group);
        plan.self_damage = rotation.forecast.damage[self_index == record.pair.leader ? 0 : 1];
        plan.peer_damage = rotation.forecast.damage[self_index == record.pair.leader ? 1 : 0];
        plan.safe = rotation.forecast.safe
            && party::individual_wave_survives(self.hp, plan.prefix * self.multiplier,
                plan.self_damage, rotation.forecast.escape_reserve)
            && party::individual_wave_survives(peer.hp, plan.prefix * peer.multiplier,
                plan.peer_damage, rotation.forecast.escape_reserve);
        plan.self_damage += plan.prefix * self.multiplier;
        plan.peer_damage += plan.prefix * peer.multiplier;
        plan.fight.rounds = rotation.forecast.volleys;
        plan.arrived = rotation.arrived;
        if (std::max(plan.self_travel, plan.peer_travel) + plan.fight.rounds
            + cambc::constants::capture_time + 1 + 2 + 2 > party_ttl)
            return decline("rotation-cycle-too-long", group);
    }
    if (!plan.safe) return decline("individual-hp", group, plan.self_damage, plan.peer_damage);

    // A bounded named alternative, repriced in common currency. We do not infer zero cost
    // from idle actions, and a truncated ordinary shortlist is not an optimality certificate.
    party::Cost cost;
    cost.valid = cost.positive = state.lease.valid(); // the accepted opportunity budget is sunk
    int alt_group = state.displaced_group;
    float displaced_score = state.displaced_score;
    if constexpr (!cfg::coalition_party_scout) {
    if (!state.lease.valid()) {
    PartyChoice* alternative = nullptr;
    for (auto& choice : state.alternatives)
        if (choice.group >= 0 && choice.group != group && ctx.round - choice.round <= party_choice_age
            && (!alternative || choice.score > alternative->score)) alternative = &choice;
    if (!alternative) return decline("unknown-alternative", group);
    int need = 1;
    if (alternative->round != ctx.round) {
        const float score = score_group(ctx, alternative->group, &need); // one candidate, never another scan
        if (score <= 0 || need != 1) return decline("alternative-unavailable", group);
        alternative->score = score; alternative->round = ctx.round;
    }
    alt_group = alternative->group;
    displaced_score = alternative->score;
    const auto& alt = world.groups[static_cast<std::size_t>(alt_group)];
    const auto* alt_field = world.group_field(alt_group, false);
    const int alt_travel = party_distance(alt_field, ctx.route_pos);
    const int alt_return = party_departure_distance(alt_field, group);
    if (alt_travel < 0 || alt_return < 0) return decline("alternative-route", group);
    const auto& alt_belief = kn.groups[static_cast<std::size_t>(alt_group)];
    if (alt_belief.status == GroupStatus::respawning) return decline("alternative-respawning", group);
    const auto alt_forecast = model::forecast_from_constants({alt.total_hp, alt.weighted_hp, alt.total_strength},
        party_dps(self, alt.neighbours), alt_belief.status == GroupStatus::reward ? 0.0f : 1.0f);
    const float alt_cycle = static_cast<float>(alt_travel + alt_forecast.rounds + cambc::constants::capture_time + 1);
    const float alt_rate = party_value(alt_group) / std::max(1.0f, alt_cycle);
    party::Work work;
    work.travel = static_cast<float>(plan.self_travel);
    work.extra_wait = static_cast<float>(party::rendezvous_wait(plan.self_travel, plan.peer_travel));
    work.clear = static_cast<float>(plan.fight.rounds);
    // Fluid marginal recovery belief, including recall ticks and spawn healing. No actual
    // payout/banking advantage is credited, and the helper never pays a capture hold.
    work.recovery = plan.self_damage / std::max(1.0f, self.max_hp)
        * (static_cast<float>(cambc::constants::recall_time)
            + std::ceil(1.0f / std::max(0.0001f, cambc::constants::spawn_healing)));
    work.rejoin = static_cast<float>(std::max(0, alt_return - alt_travel));
    work.capture_approach = 2;
    work.capture_hold = static_cast<float>(cambc::constants::capture_time + 1);
    work.abort_rounds = 2;
    cost = party::opportunity(party_value(group), alt_rate, true, work,
                                       self_index == record.pair.leader);
    if (!cost.positive) return decline("alternative-cost", group, cost.displaced, cost.budget);
    }
    } else {
    if (!state.lease.valid()) {
    PartyChoice* alternative = nullptr;
    for (auto& choice : state.alternatives)
        if (choice.group >= 0 && choice.group != group && ctx.round - choice.round <= party_choice_age
            && (!alternative || choice.score > alternative->score)) alternative = &choice;
    int need = 1;
    int failed_alternative = -1;
    if (alternative && alternative->round != ctx.round) {
        const float score = score_group(ctx, alternative->group, &need); // one candidate, never another scan
        if (score <= 0 || need != 1) {
            if constexpr (!cfg::coalition_party_scout) return decline("alternative-unavailable", group);
            else {
                failed_alternative = alternative->group;
                *alternative = {}; alternative = nullptr;
            }
        } else { alternative->score = score; alternative->round = ctx.round; }
    }
    if constexpr (cfg::coalition_party_scout) {
        if (!alternative) {
            auto& scout = party_scout_state();
            if (scout.round == ctx.round) {
                scout.reason = "already-attempted";
                party_scout_trace(ctx, group);
                return decline("scout-already-attempted", group);
            }
            scout = {}; scout.round = ctx.round; scout.total = static_cast<int>(world.groups.size());
            auto scout_refusal = [&](const char* reason) {
                scout.reason = reason; party_scout_trace(ctx, group);
                return decline(reason, group, static_cast<float>(scout.group), scout.score);
            };
            // One cheap ranking pass (at most 128 static groups) and ONE actual score. This
            // samples a named displaced task; it cannot certify the global best alternative.
            constexpr int scout_scan_limit = 128;
            if (!fuel_ok(cfg::fuel_score_group + 3000)) return scout_refusal("fuel-before-scan");
            for (int candidate = 0; candidate < scout.total && candidate < scout_scan_limit; ++candidate) {
                if (!fuel_ok(cfg::fuel_score_group)) return scout_refusal("fuel-during-scan");
                ++scout.scanned;
                if (candidate == group || candidate == failed_alternative) continue;
                const auto& info = world.groups[static_cast<std::size_t>(candidate)];
                const auto& belief = kn.groups[static_cast<std::size_t>(candidate)];
                if (!info.harmless || belief.status == GroupStatus::respawning
                    || kn.committed(candidate) > 0 || kn.capturers(candidate) > 0
                    || (candidate < static_cast<int>(banned_until_.size())
                        && ctx.round < banned_until_[static_cast<std::size_t>(candidate)])
                    || world.walk_reachable(candidate) == 0) continue;
                const auto* known = world.group_field(candidate, false);
                int travel = known ? party_distance(known, ctx.route_pos) : cheb(ctx.route_pos, info.center);
                if (travel < 0) continue;
                const float value = party_value(candidate);
                if (!std::isfinite(value) || value <= 0) continue;
                const float bound = value / static_cast<float>(std::max(1,
                    travel + cambc::constants::capture_time + 1));
                if (bound > scout.bound) { scout.bound = bound; scout.group = candidate; }
            }
            if (scout.group < 0) return scout_refusal("no-candidate");
            // A geometric rank may nominate a job, but only a real finite field licenses its
            // travel/return price. Keep enough fuel for the one full scorer and subsequent act.
            const auto* field = world.group_field(scout.group, true, cfg::fuel_score_group + 6000);
            scout.route = party_distance(field, ctx.route_pos);
            scout.return_route = party_departure_distance(field, group);
            if (scout.route < 0 || scout.return_route < 0) return scout_refusal("route-unavailable");
            if (!fuel_ok(cfg::fuel_score_group)) return scout_refusal("fuel-before-score");
            int solo_need = 1;
            scout.score = score_group(ctx, scout.group, &solo_need);
            if (!std::isfinite(scout.score) || scout.score <= 0) return scout_refusal("score-nonpositive");
            if (solo_need != 1) return scout_refusal("need-not-solo");
            // Old stale entries must not block insertion of the newly measured candidate.
            for (auto& choice : state.alternatives)
                if (ctx.round - choice.round > party_choice_age) choice = {};
            note_party_choice(ctx, scout.group, scout.score, solo_need);
            for (auto& choice : state.alternatives)
                if (choice.group == scout.group && choice.round == ctx.round) { alternative = &choice; break; }
            if (!alternative) return scout_refusal("cache-unavailable");
            scout.reason = "priced";
            // Emit after the existing opportunity calculation, before any offer trace, so
            // the single bounded line records the candidate's actual consumed budget too.
        }
    }
    if (!alternative) return decline("unknown-alternative", group);
    alt_group = alternative->group;
    displaced_score = alternative->score;
    const auto& alt = world.groups[static_cast<std::size_t>(alt_group)];
    const auto* alt_field = world.group_field(alt_group, false);
    const int alt_travel = party_distance(alt_field, ctx.route_pos);
    const int alt_return = party_departure_distance(alt_field, group);
    if (alt_travel < 0 || alt_return < 0) return decline("alternative-route", group);
    const auto& alt_belief = kn.groups[static_cast<std::size_t>(alt_group)];
    if (alt_belief.status == GroupStatus::respawning) return decline("alternative-respawning", group);
    const auto alt_forecast = model::forecast_from_constants({alt.total_hp, alt.weighted_hp, alt.total_strength},
        party_dps(self, alt.neighbours), alt_belief.status == GroupStatus::reward ? 0.0f : 1.0f);
    const float alt_cycle = static_cast<float>(alt_travel + alt_forecast.rounds + cambc::constants::capture_time + 1);
    const float alt_rate = party_value(alt_group) / std::max(1.0f, alt_cycle);
    party::Work work;
    work.travel = static_cast<float>(plan.self_travel);
    work.extra_wait = static_cast<float>(party::rendezvous_wait(plan.self_travel, plan.peer_travel));
    work.clear = static_cast<float>(plan.fight.rounds);
    // Fluid marginal recovery belief, including recall ticks and spawn healing. No actual
    // payout/banking advantage is credited, and the helper never pays a capture hold.
    work.recovery = plan.self_damage / std::max(1.0f, self.max_hp)
        * (static_cast<float>(cambc::constants::recall_time)
            + std::ceil(1.0f / std::max(0.0001f, cambc::constants::spawn_healing)));
    work.rejoin = static_cast<float>(std::max(0, alt_return - alt_travel));
    work.capture_approach = 2;
    work.capture_hold = static_cast<float>(cambc::constants::capture_time + 1);
    work.abort_rounds = 2;
    cost = party::opportunity(party_value(group), alt_rate, true, work,
                                       self_index == record.pair.leader);
    if constexpr (cfg::coalition_party_scout) {
        auto& scout = party_scout_state();
        if (scout.round == ctx.round && scout.group == alt_group) {
            scout.displaced = cost.displaced; scout.budget = cost.budget;
            scout.reason = cost.positive ? "price-pass" : "price-refusal";
            party_scout_trace(ctx, group);
        }
    }
    if (!cost.positive) return decline("alternative-cost", group, cost.displaced, cost.budget);
    }
    }
    party::Facts facts;
    facts.fresh_alive_group = true; facts.participants_available = true; facts.routes_known = true;
    const bool self_leads = self_index == record.pair.leader;
    const auto& leader_actor = self_leads ? self : peer;
    const int helper_arrival = self_leads ? plan.peer_travel : plan.self_travel;
    const int leader_arrival = self_leads ? plan.self_travel : plan.peer_travel;
    facts.useful_first_hit = helper_arrival < leader_arrival
        + model::forecast_camp_fight(live_monsters(ctx, group),
            party_dps(leader_actor, world.groups[static_cast<std::size_t>(group)].neighbours)).rounds;
    facts.own_cost_passes = cost.positive; facts.helper_arrived = plan.arrived;
    facts.legal_first_contribution = plan.arrived && self.state.contains(cambc::champion_state::can_attack)
        && party_peer_basic_ready(peer);
    facts.prefix_survives = plan.safe; facts.one_wave_feasible = plan.safe;
    if (!facts.useful_first_hit) return decline("late-helper", group);

    if (!state.lease.valid()) {
        if (accepting && !party::may_accept(record, ctx.round, party_ttl, self_index,
                party_slot(record.pair.leader), group, facts)) return decline("accept-guard", group);
        if (!accepting && !party::offer_feasible(facts)) return decline("offer-guard", group);
        state.saved_target = target_; state.saved_score = target_score_; state.saved_need = target_need_;
        state.displaced_group = alt_group;
        state.displaced_score = displaced_score;
        state.lease = {record, group, record.round + party_ttl}; state.phase = 1;
        state.progress_round = ctx.round; state.best_distance = plan.self_travel;
        target_ = group; target_need_ = 2; capturing_ = false; engaged_ = false;
        if (accepting) {
            // This exact local lease exists BEFORE the ACK becomes visible to the leader.
            publish(group, static_cast<int>(SlotStatus::engaging));
            party_trace(ctx, "accept", group, cost.displaced, cost.budget);
        } else {
            party_write(record); shared = record;
            publish(group, static_cast<int>(SlotStatus::idle));
            party_trace(ctx, "offer", group, static_cast<float>(state.saved_need), 2.0f);
        }
    }
    shared = party_read();
    if constexpr (cfg::coalition_party_rotation) {
        if (rotation.arrived) party_configure_priority(rotation);
        // Configure each self-only NEXT-round setting during the accepted rendezvous. The
        // leader cannot wake the Elk before both queried settings match one common HP plan.
        if (state.phase == 1) facts.legal_first_contribution = facts.legal_first_contribution
            && rotation.arrived && party_priorities_ready(rotation);
    }
    if (self_index == record.pair.leader && state.phase == 1) {
        if (party::may_commit(state.lease, shared, ctx.round, party_ttl, self_index,
                party_slot(record.pair.leader), party_slot(record.pair.helper), facts)) {
            state.phase = 2;
            publish(group, static_cast<int>(SlotStatus::engaging));
            party_trace(ctx, "commit", group, static_cast<float>(plan.fight.rounds), plan.self_damage);
        }
    } else if (self_index == record.pair.helper && party_slot(record.pair.leader).intent == party::Intent::engaging)
        state.phase = 2;
    if (state.phase == 2) {
        if (!party::may_produce(state.lease, shared, ctx.round, party_ttl, self_index,
                party_slot(record.pair.leader), party_slot(record.pair.helper), facts))
            return decline("produce-guard", group);
        target_ = group; engaged_ = true; why_ = "party-fight";
        bool acted = false;
        if constexpr (cfg::coalition_party_rotation) {
            if constexpr (!cfg::coalition_party_casts) {
            // Exactly the independently verified stationary basic policy. Inherited
            // fight_camps(false) can strike-dash, so it cannot uphold this geometry contract.
            if (!rotation.arrived || !rotation.elk || !cambc::self().can_attack())
                return decline("rotation-action-lost", group);
            if (!rotation.elk->camp->is_alive() || !rotation.elk->camp->is_visible()
                || cheb(cambc::self().position(), rotation.elk->camp->position()) != 1)
                return decline("rotation-target-lost", group);
            cambc::self().attack(*rotation.elk->camp);
            acts.attacked = true; acted = true;
            } else {
                if (!rotation.arrived || !rotation.elk || !cambc::self().can_attack())
                    return decline("rotation-action-lost", group);
                acted = fight_camps_stationary(ctx, acts, group);
                if (!cambc::self().is_alive()) {
                    cancel_party(ctx); return true;
                }
                // The stationary executor preserves the camp-view storage. A cached dead
                // handle supports is_alive(), but must never receive health/position queries.
                if (rotation.elk->camp->is_alive()) {
                    if (!rotation.elk->camp->is_visible()) return decline("rotation-postcast-hidden", group);
                    const float remaining_hp = rotation.elk->camp->health();
                    for (auto& camp : ctx.camps)
                        if (camp.group == group && camp.member == rotation.elk->member) camp.hp = remaining_hp;
                    ctx.refresh_self();
                    ctx.strength = cambc::self().strength(); ctx.focus = cambc::self().focus();
                    PartyActor fresh_self, fresh_peer;
                    const int peer_index = self_index == record.pair.leader ? record.pair.helper : record.pair.leader;
                    if (!party_actor(ctx, self_index, fresh_self) || !party_actor(ctx, peer_index, fresh_peer))
                        return decline("rotation-postcast-peer", group);
                    const auto fresh_rotation = party_rotation(ctx, group, fresh_self, fresh_peer, record);
                    if (!fresh_rotation.supported || !fresh_rotation.arrived || !fresh_rotation.forecast.safe)
                        return decline("rotation-postcast-unsafe", group);
                    // Casts/resets may change live HP or stats. Configure our own NEXT-round
                    // tank setting from that fresh state, never the pre-cast forecast.
                    party_configure_priority(fresh_rotation);
                }
                // A lethal effect leaves the exact local lease intact. Fresh reward state on
                // the next owner turn releases the helper and assigns the actual capturer.
            }
        } else acted = fight_camps(ctx, acts, group, false);
        if (acts.attacked || acts.cast) last_hit_round_ = ctx.round;
        party_trace(ctx, acted ? "produce" : "no-action", group, static_cast<float>(acts.attacked), static_cast<float>(acts.cast));
        return true;
    }
    if (ctx.round - record.round > party_arrival_limit + 3) return decline("arrival-timeout", group);
    why_ = "party-stage";
    if (plan.self_travel < state.best_distance) {
        state.best_distance = plan.self_travel; state.progress_round = ctx.round;
    }
    if (plan.self_travel > 0 && ctx.round - state.progress_round > 3) return decline("blocked-arrival", group);
    if (!party_reaches(ctx, group, self)) party_stage(ctx, acts, group, plan);
    return true; // NEVER fall into the ordinary awake-or-force latch while awaiting a peer
}

} // namespace mb
