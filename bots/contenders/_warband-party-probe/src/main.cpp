// Conditional actual Brain party-caller fixture. SDK actions reach all positions/HP/allowances.
// Only the caller's named target/alternative selection is stipulated, using actual scores/fields.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#define private public
#include "../../_warband-coalition/src/brain.h"
#undef private
#include "../../_warband-coalition/src/brain.cpp"
#define main unused_production_entrypoint
#include "../../_warband-coalition/src/main.cpp"
#undef main

namespace mb::probe {
using namespace cambc;
inline constexpr int probe_active_side = 0;
inline constexpr int probe_epoch = 30;
inline constexpr bool probe_normal_farm = false;
inline constexpr bool probe_compact_actions = false;
int side() { return map::spawn_region(team::ally).is_set({1, 1}) ? 0 : 1; }
bool active() { return side() == probe_active_side; }
vec2 frame(vec2 p) { return probe_active_side == 0 ? p : vec2{map::width()-1-p.x, map::height()-1-p.y}; }
const char* who() {
    if (self().champion_name() == champion_type::ryth) return "leader";
    if (self().champion_name() == champion_type::marcella) return "helper";
    return "reserve";
}
int group_at(vec2 p) { return World::get().group_at(frame(p)); }
void draft_turn() {
    const std::array party_picks{champion_draft::ryth, champion_draft::marcella, champion_draft::tavi};
    const std::array passive_picks{champion_draft::willow, champion_draft::prism, champion_draft::stellar};
    const auto& picks = active() ? party_picks : passive_picks;
    const auto chosen = draft::current_draft(team::ally);
    for (const auto pick : picks) if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end()) {
        draft::pick(pick); return;
    }
}
void observe(Ctx& ctx) {
    ctx.observe();
    auto& knowledge = Knowledge::get();
    knowledge.round = static_cast<int>(round_number());
    knowledge.read_slots();
    knowledge.observe_groups(ctx, kit().vision);
    ctx.gem_weight = Brain::gem_weight(ctx);
}
void log_state(const char* phase, const Ctx& ctx, int group, bool handled, const TurnActions& acts) {
    const auto& knowledge = Knowledge::get();
    const auto& belief = knowledge.groups[static_cast<std::size_t>(group)];
    const auto& camp = World::get().group_objects[static_cast<std::size_t>(group)].camps()[0];
    float hp = -1;
    if (ctx.visible.is_set(camp.spawn_position())) hp = camp.is_alive() ? camp.health() : 0;
    std::array<std::uint8_t, 8> bytes{};
    shared_memory.read(0, std::span<std::uint8_t>{bytes});
    const auto record = party::Record::decode({bytes[6], bytes[7]});
    auto& me = self();
    const auto p = me.position();
    std::printf("GP phase=%s r=%u side=%c who=%s x=%d y=%d hp=%.0f max=%.0f camp=%.0f belief=%d seen=%d p=%d epoch=%d Lg=%d Ls=%d Hg=%d Hs=%d prev=%d next=%d A=%d M=%d C=%d handled=%d moved=%d attacked=%d cast=%d gems=%u gold=%u fuel=%llu\n",
        phase, round_number(), side()==0?'A':'B', who(), p.x, p.y, me.health(), me.max_health(), hp,
        static_cast<int>(belief.status), belief.seen_round, party_state().phase, record.round,
        static_cast<int>(bytes[0]&63u)-1, static_cast<int>(bytes[0]>>6),
        static_cast<int>(bytes[1]&63u)-1, static_cast<int>(bytes[1]>>6),
        me.prev_priority(), me.priority(), me.can_attack(), me.can_move(), me.can_cast(),
        handled, acts.moved, acts.attacked, acts.cast, me.gem_count(), me.gold_count(),
        static_cast<unsigned long long>(fuel_remaining()));
}
void normal_turn(Brain& brain) {
    // This arm uses the whole production pipeline, including its actual chooser and shortlist.
    brain.turn();
    if (!fuel_ok(16000) || !self().is_alive()) return;
    const auto& me = self();
    const auto& state = party_state();
    const auto& camp = World::get().group_objects[static_cast<std::size_t>(group_at({10,10}))].camps()[0];
    const auto visible = me.visible_tiles();
    const float hp = visible.is_set(camp.spawn_position()) ? (camp.is_alive() ? camp.health() : 0) : -1;
    const auto pos = me.position();
    if constexpr (probe_compact_actions) {
        int other_awake = 0;
        for (const auto tile : vision::awake_camp_tiles().positions())
            if (World::get().group_at(tile) != group_at({10,10})) ++other_awake;
        const auto abilities = me.abilities();
        std::printf("GP phase=normal r=%u side=%c who=%s x=%d y=%d hp=%.0f camp=%.0f target=%d need=%d why=%s p=%d lease=%d contract=%d epoch=%d next=%d gems=%u gold=%u A=%d M=%d C=%d c0=%d c1=%d otherawake=%d fuel=%llu\n",
            round_number(), side()==0?'A':'B', who(), pos.x, pos.y, me.health(), hp,
            brain.target_, brain.target_need_, brain.why_, state.phase, state.lease.valid(), state.lease.group,
            state.lease.record.round, me.priority(), me.gem_count(), me.gold_count(),
            me.can_attack(), me.can_move(), me.can_cast(), abilities[0].cooldown(), abilities[1].cooldown(),
            other_awake, static_cast<unsigned long long>(fuel_remaining()));
        return;
    }
    std::printf("GP phase=normal r=%u side=%c who=%s x=%d y=%d hp=%.0f camp=%.0f target=%d need=%d score=%.5f why=%s engaged=%d capturing=%d p=%d lease=%d contract=%d epoch=%d next=%d gems=%u gold=%u a0=%d v0=%.5f t0=%d a1=%d v1=%.5f t1=%d a2=%d v2=%.5f t2=%d a3=%d v3=%.5f t3=%d fuel=%llu\n",
        round_number(), side()==0?'A':'B', who(), pos.x, pos.y, me.health(), hp,
        brain.target_, brain.target_need_, brain.target_score_, brain.why_, brain.engaged_, brain.capturing_,
        state.phase, state.lease.valid(), state.lease.group, state.lease.record.round,
        me.priority(), me.gem_count(), me.gold_count(),
        state.alternatives[0].group, state.alternatives[0].score, state.alternatives[0].round,
        state.alternatives[1].group, state.alternatives[1].score, state.alternatives[1].round,
        state.alternatives[2].group, state.alternatives[2].score, state.alternatives[2].round,
        state.alternatives[3].group, state.alternatives[3].score, state.alternatives[3].round,
        static_cast<unsigned long long>(fuel_remaining()));
}
void turn() {
    if (!active() || !self().is_alive()) return;
    static Brain brain;
    static bool initialised = false, seeded = false, finished = false;
    if (!initialised) {
        // The real turn initializer runs while the fresh Elk is outside every initial action range.
        brain.turn(); initialised = true; return;
    }
    const auto type = self().champion_name();
    const bool leader = type == champion_type::ryth;
    const bool helper = type == champion_type::marcella;
    auto& me = self();
    const int r = static_cast<int>(round_number());
    const vec2 destination = frame(leader ? vec2{9,9} : helper ? vec2{7,11} : vec2{2,30});
    if (r < probe_epoch) {
        if (me.position() != destination && me.can_move() && !vision::has_champion(destination)) me.move_towards(destination);
        if (leader || helper) {
            if (r == 12) World::get().group_field(group_at({10,10}), true, 18000);
            if (r == 13) World::get().group_field(group_at({20,30}), true, 18000);
            if (r == 14) World::get().group_field(group_at({29,19}), true, 18000);
            if (r == probe_epoch-1) {
                Ctx ctx; observe(ctx);
                brain.cancel_party(ctx);
                brain.publish(-1, static_cast<int>(SlotStatus::idle));
            }
        }
        return;
    }
    if ((!leader && !helper) || finished) return;
    if constexpr (probe_normal_farm) { normal_turn(brain); return; }
    Ctx ctx; observe(ctx);
    const int group = group_at({10,10});
    const int alternative = group_at({20,30});
    // The real turn pipeline yields during an existing channel before low-health arbitration.
    if (party_channel(ctx.state)) {
        brain.cancel_party(ctx);
        brain.publish(-1, static_cast<int>(SlotStatus::idle));
        return;
    }
    if (!seeded) {
        World::get().group_field(group, true, 18000);
        World::get().group_field(alternative, true, 18000);
        int need = 0, alt_need = 0;
        const float score = brain.score_group(ctx, group, &need);
        const float alt_score = brain.score_group(ctx, alternative, &alt_need);
        std::printf("GP phase=seed r=%d side=%c who=%s group=%d score=%.5f need=%d alternative=%d alt_score=%.5f alt_need=%d hp=%.0f target_hp=%.0f\n",
            r, side()==0?'A':'B', who(), group, score, need, alternative, alt_score, alt_need,
            me.health(), Knowledge::get().groups[static_cast<std::size_t>(group)].hp_left);
        // Do not invent a productive party need or a zero-cost unavailable alternative.
        if (need < 2 || alt_need != 1 || score <= 0 || alt_score <= 0 || brain.engaged_ || brain.capturing_) {
            std::printf("GP refusal=actual_caller_prerequisites r=%d who=%s engaged=%d capturing=%d\n", r, who(), brain.engaged_, brain.capturing_);
            finished = true; return;
        }
        brain.begin_party_choices(ctx);
        brain.note_party_choice(ctx, alternative, alt_score, alt_need);
        brain.target_ = leader ? group : alternative;
        brain.target_score_ = leader ? score : alt_score;
        brain.target_need_ = leader ? need : alt_need;
        brain.chosen_round_ = ctx.round;
        seeded = true;
    }
    TurnActions acts;
    // Preserve the actual production ordering: low-health arbitration runs before farming.
    const bool low_health = brain.handle_low_health(ctx, acts);
    if (low_health) brain.cancel_party(ctx);
    if (ctx.hp < 0.5f * ctx.max_hp || low_health)
        std::printf("GP phase=health r=%d side=%c who=%s hp=%.0f max=%.0f handled=%d\n",
            r, side()==0?'A':'B', who(), me.health(), me.max_health(), low_health);
    bool handled = low_health || brain.handle_party(ctx, acts, false);
    // Release continues through the real farm caller toward its accepted named alternative.
    if (!handled && party_state().phase == 3) handled = brain.handle_farm(ctx, acts);
    log_state("after", ctx, group, handled, acts);
    if (leader && me.gem_count() >= 15 && me.gold_count() >= 90) {
        std::printf("GP phase=payout r=%d side=%c who=leader gems=%u gold=%u\n", r, side()==0?'A':'B', me.gem_count(), me.gold_count());
        finished = true;
    }
}
} // namespace mb::probe

int main() {
    while (true) {
        try { if (cambc::is_draft()) mb::probe::draft_turn(); else mb::probe::turn(); }
        catch (const std::exception& e) { std::printf("party probe error: %s\n", e.what()); }
        cambc::end_turn();
    }
}
