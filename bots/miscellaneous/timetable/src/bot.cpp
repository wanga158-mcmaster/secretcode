// Champion decisions: survive, bank, then run this body's part of the plan.
#include "bot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <variant>
#include "config.h"
#include "availability.h"
#include "dispatch.h"
#include "health_dispatch.h"
#include "equipment.h"
#include "calendar.h"
#include "draft.h"
#include "util.h"

namespace tt {

namespace {

P2 p2(vec2 v) { return { v.x, v.y }; }
vec2 v2(P2 p) { return { p.x, p.y }; }

using IT = cambc::item_type;
inline constexpr bool kObservedAvailability = cfg::availability_evidence || cfg::itinerary_jobs;

bool& dispatch_pending()
{
    static bool pending = false;
    return pending;
}

// Shard of family f (monster 0, power 1, durability 2, treasure 3, jungle 4, home 5, bruiser 6)
// for stat s (strength 0, focus 1, health 2); the emblem is the next enum value.
IT shard(int f, int s) { return IT(static_cast<std::uint32_t>(f * 6 + s * 2)); }
IT emblem(int f, int s) { return IT(static_cast<std::uint32_t>(f * 6 + s * 2 + 1)); }

enum Fam { kMonster = 0, kPower = 1, kDurability = 2, kTreasure = 3, kJungle = 4, kHome = 5, kBruiser = 6 };
enum Stat { kStr = 0, kFocus = 1, kHealth = 2 };

struct BuildStep {
    int fam, stat;
};

ItineraryCalendar& itinerary()
{
    static ItineraryCalendar calendar;
    return calendar;
}

const EquipmentModel& owned_equipment(bool refresh = false)
{
    static EquipmentModel effects;
    static bool loaded = false;
    if (!loaded || refresh) {
        EquipmentModel next;
        const auto counts = cambc::self().item_count();
        for (int family = 0; family < 7; ++family)
            for (int stat = 0; stat < 3; ++stat) {
                const auto item = emblem(family, stat);
                if (counts[item] == 0) continue;
                const auto info = item.info();
                if (info.passive) next.observe(static_cast<int>(*info.passive), info.passive_magnitude);
            }
        if constexpr (cfg::equipment_trace) {
            if (next.camp_taken() != effects.camp_taken() || next.monster != effects.monster || next.treasure != effects.treasure)
                TT_LOG(1, "equipment r%u taken %.3f monster %.3f treasure %.3f\n", cambc::round_number(), next.camp_taken(), next.monster, next.treasure);
        }
        effects = next;
        loaded = true;
    }
    return effects;
}

const Kit& any_kit(cambc::champion_type t)
{
    static std::array<std::optional<Kit>, 13> kits;
    auto& k = kits[static_cast<std::size_t>(t)];
    if (!k) k = kit_of(t);
    return *k;
}

} // namespace

// ----------------------------------------------------------------------------------- setup

void Bot::init_self()
{
    auto& me = cambc::self();
    auto& w = World::get();
    kit_ = kit_of(me);
    allies_ = cambc::champions(cambc::team::ally);
    enemies_ = cambc::champions(cambc::team::enemy);
    know_.resize(w.groups.size());
    solo_.assign(w.groups.size(), SoloCache{});
    rem_.resize(w.groups.size());
    rem_dirty_.assign(w.groups.size(), 1);
    claims_.assign(kMaxBodies, -1);
    status_.assign(kMaxBodies, kSolo);
    body_absent_.assign(kMaxBodies, -1000);
    enemy_mem_.assign(enemies_.size(), EnemyMem{});

    if (cfg::plan_mode != 0) {
        const auto& dm = cambc::draft_memory;
        decode_plan_into(reinterpret_cast<const std::uint8_t*>(dm.data()), dm.size(), plan_);
        plan_ok_ = plan_.valid && plan_.width == w.W && plan_.height == w.H
            && plan_.n_groups == std::min<int>(static_cast<int>(w.groups.size()), kMaxGroups);
    }
    const int my_type = static_cast<int>(kit_.type);
    if (plan_ok_) {
        n_bodies_ = plan_.n_bodies;
        for (int b = 0; b < plan_.n_bodies; ++b)
            if (plan_.roster[b] == my_type) slot_ = b;
        if (slot_ < 0) plan_ok_ = false;
    }
    // Slots by champion type: identical in every ally instance.
    std::vector<int> types;
    for (const auto& a : allies_) types.push_back(static_cast<int>(a.champion_name()));
    if (!plan_ok_) {
        std::vector<int> sorted = types;
        std::sort(sorted.begin(), sorted.end());
        n_bodies_ = std::min<int>(static_cast<int>(sorted.size()), kMaxBodies);
        plan_ = Plan{};
        plan_.n_bodies = n_bodies_;
        for (int b = 0; b < n_bodies_; ++b) {
            plan_.roster[b] = sorted[static_cast<std::size_t>(b)];
            plan_.body_unit[b] = b;
            if (sorted[static_cast<std::size_t>(b)] == my_type) slot_ = b;
        }
        plan_.n_units = n_bodies_;
    }
    unit_ = slot_ >= 0 ? plan_.body_unit[slot_] : -1;
    g_log_slot = slot_;
    for (int b = 0; b < n_bodies_; ++b)
        if (plan_.body_unit[b] == unit_) mates_.push_back(b);
    for (int t : types) {
        int s = -1;
        for (int b = 0; b < n_bodies_; ++b)
            if (plan_.roster[b] == t) s = b;
        ally_slot_.push_back(s);
    }
    last_gems_ = static_cast<int>(me.gem_count());
    last_gold_ = static_cast<int>(me.gold_count());
    int own = 0;
    if (plan_ok_)
        for (int g = 0; g < plan_.n_groups; ++g)
            if (plan_.owner[g] == unit_) ++own;
    TT_LOG(1, "init slot %d unit %d of %d mates %d plan %d structure %d own %d est %d\n", slot_, unit_, plan_.n_units,
           static_cast<int>(mates_.size()), plan_ok_ ? 1 : 0, plan_.structure, own, plan_.est_rounds);
}

// ----------------------------------------------------------------------------- observation

void Bot::observe()
{
    auto& me = cambc::self();
    auto& w = World::get();
    round_ = static_cast<int>(cambc::round_number());
    pos_ = me.position();
    hp_ = me.health();
    max_hp_ = std::max(1.0f, me.max_health());
    str_ = me.strength();
    focus_ = me.focus();
    gems_ = static_cast<int>(me.gem_count());
    gold_ = static_cast<int>(me.gold_count());
    if constexpr (cfg::equipment_model) owned_equipment(true);
    const auto st = me.state();
    can_move_ = st.contains(cambc::champion_state::can_move);
    can_attack_ = st.contains(cambc::champion_state::can_attack);
    can_cast_ = st.contains(cambc::champion_state::can_cast);
    const auto ab = me.abilities();
    for (int i = 0; i < 2; ++i)
        cooldown_[i] = kit_.slot[i].kind == Slot::none ? 99 : ab[static_cast<std::size_t>(i)].cooldown();
    on_spawn_ = w.my_spawn.is_set(pos_);
    cambc::vision::visible_tiles_into(visible_);
    cambc::vision::camp_tiles_into(camp_occ_);
    cambc::vision::champion_tiles_into(cambc::team::ally, units_);
    cambc::vision::champion_tiles_into(cambc::team::enemy, scratch_);
    units_ |= scratch_;
    units_ |= camp_occ_;
    cambc::vision::teleport_tiles_into(scratch_);
    units_ |= scratch_;
    read_claims();
    update_threats();
    update_groups();
}

void Bot::update_threats()
{
    auto& w = World::get();
    enemies_now_.clear();
    allies_now_.clear();
    allies_now_slot_.clear();
    camps_now_.clear();
    camps_now_member_.clear();
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        const auto& e = enemies_[i];
        if (!e.is_visible()) continue;
        Fighter f;
        f.pos = p2(e.position());
        f.hp = e.health();
        f.max_hp = std::max(1.0f, e.max_health());
        f.basic = e.strength();
        f.range = e.attack_range();
        f.melee = f.range <= 1;
        const float focus = e.focus();
        const auto type = e.champion_name();
        const Kit& k = any_kit(type);
        f.dps = pvp_dps(k, f.basic, focus);
        const auto ab = e.abilities();
        for (int s = 0; s < 2; ++s) {
            const SlotKit& sk = k.slot[s];
            if (sk.kind == Slot::none) continue;
            if (ab[static_cast<std::size_t>(s)].cooldown() != 0) continue;
            switch (sk.kind) {
            case Slot::reset: f.reset_ready = true; break;
            case Slot::dash:
                f.dash_ready = true;
                f.dash_range = static_cast<int>(sk.range);
                if (sk.modifier > 0) f.dash_hit = sk.modifier * focus;
                break;
            case Slot::damage:
            case Slot::cc:
            case Slot::aoe:
                if (sk.modifier > 0 && (sk.enemies || sk.kind == Slot::aoe)) {
                    const float dmg = sk.modifier * focus;
                    if (dmg > f.spell) {
                        f.spell = dmg;
                        f.spell_range = static_cast<int>(sk.range);
                        f.spell_radius = static_cast<int>(sk.radius);
                    }
                }
                break;
            default: break;
            }
        }
        enemy_mem_[i].f = f;
        enemy_mem_[i].seen = round_;
        enemy_mem_[i].type = type;
    }
    for (auto& m : enemy_mem_) {
        if (m.seen < 0 || round_ - m.seen > cfg::enemy_memory) continue;
        Fighter f = m.f;
        f.age = round_ - m.seen;
        // An enemy last seen on its own spawn is not hunting us.
        if (f.age > 0 && w.enemy_spawn.is_set(v2(f.pos))) continue;
        enemies_now_.push_back(f);
    }
    auto& me = cambc::self();
    for (std::size_t i = 0; i < allies_.size(); ++i) {
        const auto& a = allies_[i];
        if (a == me || !a.is_visible()) continue;
        Fighter f;
        f.pos = p2(a.position());
        f.hp = a.health();
        f.max_hp = std::max(1.0f, a.max_health());
        f.basic = a.strength();
        f.range = a.attack_range();
        f.melee = f.range <= 1;
        f.dps = pvp_dps(any_kit(a.champion_name()), f.basic, a.focus());
        allies_now_.push_back(f);
        allies_now_slot_.push_back(ally_slot_[i]);
    }
    // S3: every awake camp, visible or remembered (bushes hide camps that still hit us).
    cambc::vision::awake_camp_tiles_into(scratch_);
    if (!scratch_.is_empty())
        for (auto t : scratch_.positions()) {
            const int m = w.member_at(t);
            if (m < 0) continue;
            bool found = false;
            for (auto& c : camp_mem_)
                if (c.member == m) { c.seen = round_; found = true; }
            if (!found) camp_mem_.push_back({ m, round_ });
        }
    for (std::size_t i = 0; i < camp_mem_.size();) {
        auto& c = camp_mem_[i];
        const auto& mem = w.members[static_cast<std::size_t>(c.member)];
        const bool stale = round_ - c.seen > 8 || (c.seen != round_ && visible_.is_set(mem.pos) && !camp_occ_.is_set(mem.pos));
        if (stale) { camp_mem_[i] = camp_mem_.back(); camp_mem_.pop_back(); continue; }
        if (c.seen == round_ || cheb(mem.pos, pos_) <= mem.stats.range + 1) {
            CampThreat t{ p2(mem.pos), mem.stats.str, mem.stats.range, true, 99 };
            if constexpr (cfg::equipment_model) t.str *= owned_equipment().camp_taken();
            for (const auto& a : allies_now_) t.ally_dist = std::min(t.ally_dist, dist(a.pos, t.pos));
            camps_now_.push_back(t);
            camps_now_member_.push_back(c.member);
        }
        ++i;
    }
}

void Bot::update_groups()
{
    auto& w = World::get();
    for (std::size_t g = 0; g < w.groups.size(); ++g) {
        const auto& grp = w.groups[g];
        if (cheb(grp.centre, pos_) > 11) continue;
        auto& k = know_[g];
        bool any_visible = false, all_visible = true, any_alive = false;
        const bool is_target = static_cast<int>(g) == target_;
        if (!k.left_valid) {
            // Full health until seen otherwise.
            for (std::size_t i = 0; i < grp.members.size() && i < static_cast<std::size_t>(kMaxMembers); ++i)
                k.left[i] = w.members[static_cast<std::size_t>(grp.members[i])].stats.hp;
            k.left_valid = true;
        }
        for (std::size_t i = 0; i < grp.members.size(); ++i) {
            const auto& mem = w.members[static_cast<std::size_t>(grp.members[i])];
            if (!visible_.is_set(mem.pos)) { all_visible = false; continue; }
            any_visible = true;
            if (camp_occ_.is_set(mem.pos)) {
                any_alive = true;
                if (is_target && i < static_cast<std::size_t>(kMaxMembers) && mem.camp && mem.camp->is_visible()) {
                    const float h = mem.camp->health();
                    if (h != k.left[i]) rem_dirty_[g] = 1;
                    k.left[i] = h;
                }
            } else if (i < static_cast<std::size_t>(kMaxMembers)) {
                if (k.left[i] != 0) rem_dirty_[g] = 1;
                k.left[i] = 0;
            }
        }
        if (!any_visible) continue;
        k.seen = round_;
        if constexpr (kObservedAvailability) {
            if (!any_alive) {
                // A visible member tile licenses this query even if another member is hidden.
                // Capture claims and an expired timer never substitute for this observation.
                const auto state = grp.handle->state();
                if (std::holds_alternative<cambc::map::camp_group_alive>(state)) any_alive = true;
                else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state))
                    observe_lifecycle(k.dead, k.reward, k.respawn_at, Lifecycle::reward, round_);
                else if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state))
                    observe_lifecycle(k.dead, k.reward, k.respawn_at, Lifecycle::respawning, round_, r->timer);
            }
        }
        if (any_alive) {
            if (k.dead) {
                // Respawned at full health.
                rem_dirty_[g] = 1;
                k.dead = false;
                k.reward = false;
                k.respawn_at = -1;
                k.damaged = false;
                for (std::size_t i = 0; i < grp.members.size() && i < static_cast<std::size_t>(kMaxMembers); ++i)
                    if (!visible_.is_set(w.members[static_cast<std::size_t>(grp.members[i])].pos))
                        k.left[i] = w.members[static_cast<std::size_t>(grp.members[i])].stats.hp;
            }
            if (is_target) {
                bool dmg = false;
                for (std::size_t i = 0; i < grp.members.size() && i < static_cast<std::size_t>(kMaxMembers); ++i) {
                    const auto& mem = w.members[static_cast<std::size_t>(grp.members[i])];
                    if (visible_.is_set(mem.pos) && k.left[i] < mem.stats.hp - 0.5f) dmg = true;
                }
                k.damaged = dmg;
            }
        } else if (!kObservedAvailability && all_visible) {
            const bool was_dead = k.dead;
            k.dead = true;
            if (!was_dead || k.reward || k.respawn_at < 0) {
                // One state query tells a waiting reward from a respawn timer.
                const auto state = grp.handle->state();
                if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                    k.reward = true;
                } else if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state)) {
                    k.reward = false;
                    k.respawn_at = round_ + r->timer;
                }
            }
        }
    }
}

// Shared memory: byte b = body b's claim: bits 0-5 group + 1 (0 none), bits 6-7 Status.
void Bot::read_claims()
{
    std::array<std::uint8_t, 8> buf{};
    cambc::shared_memory.read(0, std::span<std::uint8_t>(buf.data(), buf.size()));
    for (int b = 0; b < n_bodies_ && b < 8; ++b) {
        const int v = buf[static_cast<std::size_t>(b)];
        claims_[static_cast<std::size_t>(b)] = (v & 63) - 1;
        status_[static_cast<std::size_t>(b)] = v >> 6;
        if ((v >> 6) == kAbsent) body_absent_[static_cast<std::size_t>(b)] = round_;
        const int g = claims_[static_cast<std::size_t>(b)];
        if (!kObservedAvailability && b != slot_ && (v >> 6) == kCapturing && g >= 0 && g < static_cast<int>(know_.size())) {
            auto& k = know_[static_cast<std::size_t>(g)];
            const int due = round_ + cfg::capture_rounds + World::get().groups[static_cast<std::size_t>(g)].cooldown;
            if (!k.dead || k.reward || k.respawn_at < round_) {
                k.dead = true;
                k.reward = false;
                k.respawn_at = due;
            }
        }
    }
}

void Bot::write_claim()
{
    if (slot_ < 0 || slot_ >= 8) return;
    int status = kSolo;
    if (!was_alive_ || mode_ == Mode::home || mode_ == Mode::bank || mode_ == Mode::flee) status = kAbsent;
    else if (mode_ == Mode::capture) status = kCapturing;
    else if (party_) status = kParty;
    const int g = status == kAbsent ? -1 : target_;
    const std::uint8_t v = static_cast<std::uint8_t>(((g + 1) & 63) | (status << 6));
    cambc::shared_memory[static_cast<std::size_t>(slot_)] = v;
    status_[static_cast<std::size_t>(slot_)] = status;
    claims_[static_cast<std::size_t>(slot_)] = g;
}

// --------------------------------------------------------------------------------- helpers

Fighter Bot::self_fighter() const
{
    Fighter f;
    f.pos = p2(pos_);
    f.hp = hp_;
    f.max_hp = max_hp_;
    f.basic = str_;
    f.range = kit_.attack_range;
    f.melee = kit_.melee;
    f.dps = pvp_dps(kit_, str_, focus_);
    const int rs = kit_.slot_of(Slot::reset);
    f.reset_ready = rs >= 0 && cooldown_[rs] == 0;
    return f;
}

bool Bot::tile_free(vec2 p) const
{
    const auto& w = World::get();
    return w.in_bounds(p) && !w.walls_static.is_set(p) && !units_.is_set(p);
}

float Bot::camp_in_at(vec2 p) const
{
    return camp_incoming(p2(p), camps_now_.data(), static_cast<int>(camps_now_.size()));
}

float Bot::danger_at(vec2 p) const
{
    return enemy_incoming(p2(p), enemies_now_.data(), static_cast<int>(enemies_now_.size())) + camp_in_at(p);
}

// S3 at a destination: our health must cover the hit on arrival and every step back out.
bool Bot::reserve_ok(vec2 p) const
{
    const float in = camp_in_at(p);
    if (in <= 0) return true;
    const int steps = camp_escape_steps(p2(p), camps_now_.data(), static_cast<int>(camps_now_.size()));
    return hp_ > in * (static_cast<float>(steps) + cfg::camp_escape_rounds + 1.0f);
}

float Bot::my_dps() const
{
    if constexpr (cfg::equipment_model) return camp_dps(kit_, str_, focus_, 1) * owned_equipment().camp_scale();
    return camp_dps(kit_, str_, focus_, 1);
}

int Bot::here(const Field* f) const
{
    if (!f) return -1;
    const int v = f->get(pos_);
    if (v < kInf) return v;
    // On a camp tile (a wall in most fields): the best walkable neighbour plus the step.
    const auto& w = World::get();
    int best = kInf;
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (w.in_bounds(q) && !w.walls_static.is_set(q)) best = std::min(best, f->get(q));
    }
    return best < kInf ? best + 1 : kInf;
}

int Bot::field_dist(int g) const
{
    auto& w = World::get();
    const Field* f = w.group_field(g, false);
    return f ? here(f) : -1;
}

int Bot::estimate_dist(int g) const
{
    auto& w = World::get();
    const int fd = field_dist(g);
    if (fd >= 0) return fd;
    if (plan_ok_ && target_ >= 0 && target_ != g) {
        const int via = plan_.distance(target_, g);
        const Field* tf = w.group_field(target_, false);
        if (via >= 0 && via < kInf && tf) return tf->get(pos_) + via;
    }
    return cheb(pos_, w.groups[static_cast<std::size_t>(g)].centre) * 5 / 4 + 2;
}

bool Bot::group_awake(int g) const
{
    auto& w = World::get();
    for (int m : camps_now_member_)
        if (w.members[static_cast<std::size_t>(m)].group == g) return true;
    return false;
}

bool Bot::mate_on_group(int g) const
{
    auto& w = World::get();
    for (const auto& a : allies_now_)
        if (w.group_at(v2(a.pos)) == g) return true;
    return false;
}

int Bot::leader() const
{
    for (int b : mates_) {
        if (b == slot_) return slot_;
        if (status_[static_cast<std::size_t>(b)] != kAbsent) return b;
    }
    return slot_;
}

int Bot::alive_mates() const
{
    int n = 0;
    for (int b : mates_)
        if (b == slot_ || status_[static_cast<std::size_t>(b)] != kAbsent) ++n;
    return n;
}

int Bot::mates_near() const
{
    int n = 1;
    for (std::size_t i = 0; i < allies_now_.size(); ++i) {
        const int s = allies_now_slot_[i];
        if (s < 0 || s == slot_) continue;
        if (std::find(mates_.begin(), mates_.end(), s) == mates_.end()) continue;
        if constexpr (cfg::itinerary_jobs) {
            if (target_ < 0 || claims_[static_cast<std::size_t>(s)] != target_
                || status_[static_cast<std::size_t>(s)] != kParty) continue;
        }
        if (dist(allies_now_[i].pos, p2(pos_)) <= (cfg::itinerary_jobs ? 2 : 6)) ++n;
    }
    return n;
}

// A group needs the unit when its forecast does not fit our health at full strength.
const GroupSpec& Bot::remaining_spec(int g) const
{
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    const auto& k = know_[static_cast<std::size_t>(g)];
    if (!k.damaged || k.dead) return grp.spec;
    if (!rem_dirty_[static_cast<std::size_t>(g)]) return rem_[static_cast<std::size_t>(g)];
    rem_dirty_[static_cast<std::size_t>(g)] = 0;
    GroupSpec& spec = rem_[static_cast<std::size_t>(g)];
    spec.n = 0;
    spec.hp_total = spec.str_total = 0;
    spec.max_range = 0;
    spec.harmless = true;
    for (std::size_t i = 0; i < grp.members.size(); ++i) {
        const auto& m = w.members[static_cast<std::size_t>(grp.members[i])];
        const float left = i < static_cast<std::size_t>(kMaxMembers) ? k.left[i] : m.stats.hp;
        if (left > 0) spec.add(left, m.stats.str, m.stats.range);
    }
    spec.gems = grp.gems;
    spec.gold = grp.gold;
    spec.cooldown = grp.cooldown;
    spec.sort_members();
    return spec;
}

// A group needs the unit when what is left of it does not fit our health at full strength.
const Bot::SoloCache& Bot::solo(int g) const
{
    auto& c = solo_[static_cast<std::size_t>(g)];
    const float dps = my_dps();
    if (c.dps != dps || c.max_hp != max_hp_) {
        auto& w = World::get();
        const auto& grp = w.groups[static_cast<std::size_t>(g)];
        BodySpec body = body_spec(kit_, max_hp_, str_, focus_);
        if constexpr (cfg::equipment_model) owned_equipment().apply(body);
        c.dps = dps;
        c.max_hp = max_hp_;
        c.clear = smith_fast(grp.spec, dps);
        c.damage = solo_damage(grp.spec, body, c.clear);
        const float margin = body.fragile ? cfg::camp_margin * 1.5f : cfg::camp_margin;
        c.party = !grp.harmless && c.damage * margin > max_hp_;
        const float weight = plan_ok_ ? plan_.gold_weight : cfg::gold_weight;
        c.value = grp.gems + weight * grp.gold * body.gold_mult;
    }
    return c;
}

bool Bot::needs_party(int g) const
{
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    const auto& k = know_[static_cast<std::size_t>(g)];
    if (grp.harmless || k.dead) return false;
    if (!k.damaged) return solo(g).party;
    const GroupSpec& spec = remaining_spec(g);
    BodySpec body = body_spec(kit_, max_hp_, str_, focus_);
    if constexpr (cfg::equipment_model) owned_equipment().apply(body);
    const Clear c = smith_clear(spec, my_dps());
    const float margin = body.fragile ? cfg::camp_margin * 1.5f : cfg::camp_margin;
    return solo_damage(spec, body, c) * margin > max_hp_;
}

// ---------------------------------------------------------------------------------- moving

// Every move goes through here: one move a turn, and a hidden occupant only reveals the tile.
bool Bot::try_move(vec2 q)
{
    if (!can_move_ || moved_ || q == pos_) return false;
    bool ok = false;
    try {
        ok = cambc::self().move_absolute(q);
    } catch (const cambc::error& e) {
        // Diagnosis of refused steps (the turn-boundary handler still logs the error line).
        std::printf("[%d] move refused r%d %d,%d -> %d,%d units %d champ %d camp %d wall %d ward %d\n", slot_, round_, pos_.x, pos_.y, q.x,
                    q.y, units_.is_set(q) ? 1 : 0, cambc::vision::has_champion(q) ? 1 : 0, cambc::vision::has_camp(q) ? 1 : 0,
                    World::get().walls_static.is_set(q) ? 1 : 0, cambc::vision::has_ward(q) ? 1 : 0);
        throw;
    }
    if (ok) {
        moved_ = true;
        pos_ = q;
        if (!kit_.melee) can_attack_ = false; // ranged champions trade the step for the attack
        return true;
    }
    units_.set(q);
    return false;
}

bool Bot::step_on_field(const Field* f, bool avoid_danger)
{
    if (!f || !can_move_ || moved_) return false;
    const int cur = f->get(pos_);
    struct Cand { float score; vec2 p; };
    std::array<Cand, 8> cands{};
    int n = 0;
    const float here_danger = avoid_danger ? danger_at(pos_) : 0.0f;
    // Progress first; a sideways step only to get around a blocker, a step back only when stuck.
    const int slack = stuck_ >= 3 ? 1 : stuck_ >= 1 ? 0 : -1;
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (!tile_free(q)) continue;
        const int v = f->get(q);
        if (v >= kInf || (cur < kInf && v > cur + slack)) continue;
        float score = static_cast<float>(v);
        if (avoid_danger) {
            const float dz = danger_at(q) - here_danger;
            if (dz > 0) {
                if (!reserve_ok(q)) continue; // no step into reach without the health to walk out
                score += 4.0f + dz / 25.0f;
            }
        }
        cands[static_cast<std::size_t>(n++)] = { score, q };
    }
    std::sort(cands.begin(), cands.begin() + n, [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (int i = 0; i < n && i < 3; ++i) {
        if (try_move(cands[static_cast<std::size_t>(i)].p)) return true;
    }
    return false;
}

// Never hold a respawn: step off camp tiles, outwards from the block when it is large.
bool Bot::leave_camp_tiles()
{
    auto& w = World::get();
    if (w.group_at(pos_) < 0 || !can_move_ || moved_) return false;
    const Field* sf = w.spawn_field(false);
    // Depth of a tile: Chebyshev distance to the nearest free non-camp tile (window of 4).
    auto depth = [&](vec2 q) {
        for (int r = 0; r <= 4; ++r)
            for (int dx = -r; dx <= r; ++dx)
                for (int dy = -r; dy <= r; ++dy) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const vec2 t{ q.x + dx, q.y + dy };
                    if (w.in_bounds(t) && !w.walls_static.is_set(t) && w.member_at(t) < 0) return r;
                }
        return 5;
    };
    vec2 best = pos_;
    float bs = 1e9f;
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (!tile_free(q)) continue;
        const float s = static_cast<float>(depth(q)) * 100.0f + danger_at(q) + (sf ? static_cast<float>(std::min(sf->get(q), 300)) * 0.01f : 0.0f);
        if (s < bs) { bs = s; best = q; }
    }
    if (best == pos_) return false;
    return try_move(best);
}

bool Bot::step_greedy(vec2 goal)
{
    if (!can_move_ || moved_) return false;
    vec2 best = pos_;
    int bd = cheb(pos_, goal);
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (!tile_free(q)) continue;
        const int v = cheb(q, goal);
        if (v < bd) { bd = v; best = q; }
    }
    if (best == pos_) return false;
    return try_move(best);
}

// S2: move to the reachable tile with the least damage expected next round; among equals the
// one deepest outside every enemy's reach, then the one nearest home.
bool Bot::step_to_safety(bool home)
{
    if (!can_move_ || moved_) return false;
    auto& w = World::get();
    const Field* sf = w.spawn_field(true);
    const int n_e = static_cast<int>(enemies_now_.size());
    auto score = [&](vec2 q) {
        const float dmg = danger_at(q);
        const int margin = n_e ? std::min(3, safety_margin(p2(q), enemies_now_.data(), n_e)) : 3;
        const int hd = sf ? std::min(sf->get(q), 500) : 0;
        return dmg * 10.0f - static_cast<float>(margin) * 30.0f + (home ? static_cast<float>(hd) : 0.0f);
    };
    const float here = score(pos_);
    vec2 best = pos_;
    float bs = here;
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (!tile_free(q)) continue;
        const float s = score(q);
        if (s < bs) { bs = s; best = q; }
    }
    if (best != pos_) {
        if (try_move(best)) return true;
    }
    return false;
}

bool Bot::travel_dash(const Field* f)
{
    if (!f || cast_ || !can_cast_ || !enemies_now_.empty()) return false;
    const int s = kit_.slot_of(Slot::dash);
    if (s < 0 || cooldown_[s] != 0) return false;
    cambc::bitboard dash_range, dash_walls;
    if constexpr (cfg::dash_refresh) {
        auto& me=cambc::self();
        pos_=me.position(); // action origin, independently of the cached route origin
        const auto abilities=me.abilities();
        if(!abilities[static_cast<std::size_t>(s)].is_ready()) return false;
        abilities[static_cast<std::size_t>(s)].mask_into(dash_range); // range only, not legality
        cambc::vision::visible_tiles_into(visible_);
        cambc::vision::camp_tiles_into(camp_occ_);
        cambc::vision::champion_tiles_into(cambc::team::ally,units_);
        cambc::vision::champion_tiles_into(cambc::team::enemy,scratch_);
        units_|=scratch_; units_|=camp_occ_;
        cambc::vision::teleport_tiles_into(scratch_); units_|=scratch_;
        dash_walls=cambc::map::walls() | cambc::map::altars() | World::get().enemy_spawn;
    }
    const auto& w = World::get();
    const int cur = f->get(pos_);
    if (cur < 8 || cur >= kInf) return false;
    const SlotKit& sk = kit_.slot[s];
    const int range = std::max(1, static_cast<int>(sk.range));
    vec2 best = pos_;
    int bv = cur;
    for (int dx = -range; dx <= range; ++dx)
        for (int dy = -range; dy <= range; ++dy) {
            const vec2 q{ pos_.x + dx, pos_.y + dy };
            if (cheb(q, pos_) != range || !tile_free(q)) continue;
            if constexpr (cfg::dash_refresh) { if(!dash_range.is_set(q)) continue; }
            const int v = f->get(q);
            if (v >= bv) continue;
            // Landing hits (Heart) would wake camps: never land beside one.
            if (sk.modifier > 0) {
                bool near_camp = false;
                for (const auto d : kDirs)
                    if (w.member_at(q + d) >= 0) near_camp = true;
                if (near_camp) continue;
            }
            // The field guarantees a walkable path; its middle tile must be free of units.
            bool path = false;
            for (const auto d : kDirs) {
                const vec2 mid = pos_ + d;
                if (cheb(mid, q) <= 1) {
                    if constexpr (cfg::dash_refresh) {
                        if(w.in_bounds(mid) && !dash_walls.is_set(mid) && !units_.is_set(mid)) path=true;
                    } else if(tile_free(mid)) path=true;
                }
            }
            if (!path) continue;
            bv = v;
            best = q;
        }
    if (best == pos_ || bv > cur - range) return false;
    const auto ab = cambc::self().abilities();
    if (ab[static_cast<std::size_t>(s)].use(best)) {
        cast_ = true;
        if constexpr (cfg::dash_refresh) pos_=cambc::self().position();
        else pos_ = best;
        return true;
    }
    units_.set(best);
    return false;
}

// ---------------------------------------------------------------------------------- turn

void Bot::play_turn()
{
    noise_burn();
    const long f0 = fuel_left();
    auto& w = World::get();
    const bool first = !init_;
    if (first) {
        first_fuel_[0] = f0 - fuel_left();
        init_self();
        init_ = true;
        first_fuel_[1] = f0 - fuel_left();
        // The static setup is paid once; on a heavy map the first move waits a round.
        if (fuel_left() < cfg::first_turn_floor) {
            write_claim();
            return;
        }
    } else if (first_fuel_[0] >= 0 && !first_logged_) {
        first_logged_ = true;
        TT_LOG(1, "first turn fuel world %ld init %ld observe %ld total %ld | w %ld %ld %ld %ld %ld\n", first_fuel_[0], first_fuel_[1],
               first_fuel_[2], first_fuel_[3], w.init_fuel[0], w.init_fuel[1], w.init_fuel[2], w.init_fuel[3], w.init_fuel[4]);
        TT_LOG(1, "init parts %ld %ld %ld\n", w.init_parts[0], w.init_parts[1], w.init_parts[2]);
    }
    w.fields_this_turn = 0;
    w.field_budget = first ? cfg::fields_first_turn : cfg::fields_per_turn;
    auto& me = cambc::self();
    if (!me.is_alive()) {
        if (was_alive_) {
            ++deaths_;
            TT_LOG(1, "death r%d gems %d target %d\n", static_cast<int>(cambc::round_number()), last_gems_, target_);
            was_alive_ = false;
            target_ = -1;
            party_ = false;
            mode_ = Mode::farm;
        }
        write_claim();
        gold_ = static_cast<int>(me.gold_count());
        shop();
        ++ledger_[0];
        return;
    }
    was_alive_ = true;
    observe();
    if (first) first_fuel_[2] = f0 - fuel_left();
    // The first turn pays for the static map: when little is left, start next round instead.
    if (first && fuel_left() < cfg::first_turn_floor) {
        write_claim();
        return;
    }
    moved_ = attacked_ = cast_ = false;

    // Captures show up as a jump in the bag or purse while we stand on a member tile.
    const int under = w.group_at(pos_);
    if (under >= 0 && (gems_ > last_gems_ || gold_ > last_gold_ + 15)) {
        ++captures_;
        const bool own = plan_ok_ && plan_.owner[under] == unit_;
        TT_LOG(1, "cap r%d g%d own %d party %d gems %d gold %d\n", round_, under, own ? 1 : 0, party_ ? 1 : 0, gems_ - last_gems_,
               gold_ - last_gold_);
        auto& k = know_[static_cast<std::size_t>(under)];
        k.dead = true;
        k.reward = false;
        k.respawn_at = round_ + w.groups[static_cast<std::size_t>(under)].cooldown;
        if constexpr (cfg::itinerary_jobs) {
            if (itinerary().release(under, JobRelease::captured))
                TT_LOG(1, "job complete r%d g%d next %d\n", round_, under, itinerary().cursor);
        }
        if (under == target_) { target_ = -1; party_ = false; }
        mode_ = Mode::farm;
    }
    last_gems_ = gems_;
    last_gold_ = gold_;

    // Warm the fields this body always needs (first turns have the budget).
    if (!first) {
        w.spawn_field(true);
        w.altar_field(true);
    }
    if constexpr (cfg::scheduler_fuel_guard || cfg::equipment_model || cfg::itinerary_jobs) {
        if (fuel_left() < 40000) { write_claim(); return; }
    }

    const long fuel0 = (cfg::trace_slot == slot_ || cfg::trace_slot == -2) ? fuel_left() : 0;
    const char* path = "farm";
    if (handle_channel()) {
        path = "channel";
    } else if (cfg::idle_action_guard && !channel_actions_available(can_move_, can_attack_, can_cast_)) {
        path = "disabled";
    } else {
        if (on_spawn_ || (kit_.passive == cambc::passive_type::remote_shop && gold_ >= 30)) shop();
        if (handle_enemies()) path = "enemies";
        else if (handle_health()) path = "health";
        else if (handle_bank()) path = "bank";
        else farm();
    }
    write_claim();
    account(path);
    if (first) first_fuel_[3] = f0 - fuel_left();
    if ((cfg::trace_slot == slot_ || cfg::trace_slot == -2))
        std::printf("[%d] t r%d %s pos %d,%d hp %.0f mode %d tgt %d party %d mv %d at %d cs %d fd %d st %d lead %d near %d fuel %ld\n", slot_,
                    round_, path, pos_.x, pos_.y, hp_, static_cast<int>(mode_), target_, party_ ? 1 : 0, moved_ ? 1 : 0, attacked_ ? 1 : 0,
                    cast_ ? 1 : 0, target_ >= 0 ? field_dist(target_) : -2, stuck_, leader(), mates_near(), fuel0 - fuel_left());
    if (cfg::log_level >= 2 && round_ % 10 == 0)
        TT_LOG(2, "st r%d mode %d tgt %d party %d pos %d,%d hp %.0f/%.0f gems %d gold %d lead %d near %d\n", round_,
               static_cast<int>(mode_), target_, party_ ? 1 : 0, pos_.x, pos_.y, hp_, max_hp_, gems_, gold_, leader(), mates_near());
}

void Bot::account(const char* path)
{
    int a = 6;
    const std::string_view p(path);
    if (p == "channel") a = mode_ == Mode::bank ? 2 : 1;
    else if (p == "enemies") a = 3;
    else if (p == "health") a = on_spawn_ ? 13 : 4;
    else if (p == "bank") a = 5;
    else if (target_ < 0) a = on_spawn_ ? 13 : 12;
    else if (mode_ == Mode::capture) a = 10;
    else if (attacked_ || (cast_ && !moved_)) a = 8;
    else if (stage_since_ >= 0) a = 9;
    else if (know_[static_cast<std::size_t>(target_)].dead && !know_[static_cast<std::size_t>(target_)].reward) a = 11;
    else if (engaged_round_ >= 0) a = 7;
    ++ledger_[a];
    if (round_ % 400 == 0 && round_ > 0)
        TT_LOG(1, "ledger r%d dead %d rec %d trib %d flee %d retr %d bank %d trav %d appr %d fight %d stage %d cap %d wait %d idle %d spawn %d caps %d\n",
               round_, ledger_[0], ledger_[1], ledger_[2], ledger_[3], ledger_[4], ledger_[5], ledger_[6], ledger_[7], ledger_[8],
               ledger_[9], ledger_[10], ledger_[11], ledger_[12], ledger_[13], captures_);
}

bool Bot::handle_channel()
{
    auto& me = cambc::self();
    const auto st = me.state();
    if (st.contains(cambc::champion_state::recalling)) { mode_ = Mode::home; return true; }
    if (st.contains(cambc::champion_state::teleporting)) return true;
    if (st.contains(cambc::champion_state::submitting_gems)) {
        mode_ = Mode::bank;
        // Death costs the whole bag: cancel when the enemies in reach could kill us first.
        const int rem = me.remaining_channel_time();
        const float dmg = danger_at(pos_) * static_cast<float>(rem + 1);
        if (dmg >= hp_ * 0.8f) {
            me.cancel_submit_gems();
            TT_LOG(1, "tribute cancel r%d gems %d\n", round_, gems_);
            return false;
        }
        return true;
    }
    if (mode_ == Mode::bank && gems_ == 0) mode_ = Mode::farm;
    return false;
}

// ----------------------------------------------------------------------------------- PvP

bool Bot::handle_enemies()
{
    if (enemies_now_.empty()) {
        if (mode_ == Mode::flee) mode_ = Mode::farm;
        return false;
    }
    auto& w = World::get();
    const int n_e = static_cast<int>(enemies_now_.size());
    int nearest = 99;
    for (const auto& e : enemies_now_) nearest = std::min(nearest, dist(p2(pos_), e.pos));
    // Groups near enemies are not planned for a while: no contact without a way out.
    for (std::size_t g = 0; g < w.groups.size(); ++g)
        for (const auto& e : enemies_now_)
            if (e.age == 0 && cheb(w.groups[g].centre, v2(e.pos)) <= threat_reach(e) + 3) know_[g].danger_until = round_ + 12;
    if (nearest > cfg::threat_radius + 2) {
        if (mode_ == Mode::flee) mode_ = Mode::farm;
        return false;
    }

    // The skirmish as it would go from here: us and the allies in view versus them.
    std::vector<Fighter> ours{ self_fighter() };
    for (const auto& a : allies_now_)
        if (dist(a.pos, p2(pos_)) <= 6) ours.push_back(a);
    std::vector<Fighter> theirs;
    for (const auto& e : enemies_now_)
        if (dist(e.pos, p2(pos_)) <= 9) theirs.push_back(e);
    const Verdict v = skirmish(ours.data(), static_cast<int>(ours.size()), theirs.data(), static_cast<int>(theirs.size()));

    // Strike a visible enemy in reach when the verdict is clean, or finish one cheaply.
    int victim = -1;
    float victim_hp = 1e9f;
    auto& me = cambc::self();
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        const auto& m = enemy_mem_[i];
        if (m.seen != round_) continue;
        if (dist(m.f.pos, p2(pos_)) > kit_.attack_range) continue;
        if (m.f.hp < victim_hp) { victim_hp = m.f.hp; victim = static_cast<int>(i); }
    }
    const float burst = str_ * (self_fighter().reset_ready ? 2.0f : 1.0f);
    const bool finish = victim >= 0 && victim_hp <= burst * cfg::finish_hits && v.our_deaths == 0;
    if (victim >= 0 && can_attack_ && (favourable(v) || finish)) {
        const auto& e = enemies_[static_cast<std::size_t>(victim)];
        if (e.is_visible()) {
            me.attack(e);
            attacked_ = true;
            can_attack_ = me.can_attack();
            const int rs = kit_.slot_of(Slot::reset);
            if (rs >= 0 && cooldown_[rs] == 0 && !cast_ && me.can_cast() && !can_attack_) {
                const auto ab = me.abilities();
                if (ab[static_cast<std::size_t>(rs)].use()) {
                    cast_ = true;
                    if (me.can_attack() && e.is_visible() && e.health() > 0) me.attack(e);
                }
            }
            TT_LOG(2, "strike r%d hp %.0f\n", round_, victim_hp);
            return true;
        }
    }
    if (safe_from(p2(pos_), enemies_now_.data(), n_e)) {
        if (mode_ == Mode::flee) mode_ = Mode::farm;
        return false;
    }
    if (favourable(v)) return false; // hold ground and keep farming; we answer if they come
    // Out of reach first. If no step leaves their reach and one stands in ours, strike back.
    const vec2 before = pos_;
    step_to_safety(true);
    if (pos_ == before && victim >= 0 && can_attack_) {
        const auto& e = enemies_[static_cast<std::size_t>(victim)];
        if (e.is_visible()) { me.attack(e); attacked_ = true; }
    }
    if (mode_ != Mode::flee) TT_LOG(2, "flee r%d nearest %d hp %.0f\n", round_, nearest, hp_);
    mode_ = Mode::flee;
    target_ = -1;
    party_ = false;
    return true;
}

// -------------------------------------------------------------------------------- health

bool Bot::try_recall(const char* why)
{
    if (on_spawn_ || moved_ || attacked_ || cast_) return false;
    cambc::self().recall();
    if constexpr (cfg::itinerary_jobs) itinerary().recover();
    mode_ = Mode::home;
    home_since_ = round_;
    target_ = -1;
    party_ = false;
    TT_LOG(1, "recall r%d %s hp %.0f gold %d gems %d\n", round_, why, hp_, gold_, gems_);
    return true;
}

bool Bot::handle_health()
{
    // S3: camps in range take our health every round; leave before it runs out.
    const float cin = camp_in_at(pos_);
    const int steps = camp_escape_steps(p2(pos_), camps_now_.data(), static_cast<int>(camps_now_.size()));
    if (cin > 0 && hp_ <= cin * (static_cast<float>(steps) + cfg::camp_escape_rounds)) {
        step_to_safety(true);
        target_ = -1;
        party_ = false;
        return true;
    }
    if (on_spawn_) {
        if (hp_ < max_hp_ - 0.5f) { mode_ = Mode::home; return true; } // spawn heals in full at turn end
        if (mode_ == Mode::home) mode_ = Mode::farm;
        return false;
    }
    const bool safe = safe_from(p2(pos_), enemies_now_.data(), static_cast<int>(enemies_now_.size()));
    if (hp_ < cfg::recall_health_frac * max_hp_ && cin <= 0 && safe)
        if (try_recall("heal")) return true;
    // Gold buys nothing until it is spent: take a full purse home unless a capture or a party
    // fight holds us here (Tavi shops in the field instead).
    if (gold_ >= cfg::shop_recall_gold && kit_.passive != cambc::passive_type::remote_shop && cin <= 0 && safe
        && mode_ != Mode::capture && !(party_ && target_ >= 0 && group_awake(target_))) {
        const Field* sf = World::get().spawn_field(false);
        const int home = sf ? here(sf) : cheb(pos_, World::get().my_spawn_centre);
        if (home > cfg::shop_walk_tiles) {
            if (try_recall("shop")) return true;
        } else if (home > 0 && sf && step_on_field(sf, true)) {
            mode_ = Mode::home;
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------------- banking

bool Bot::handle_bank()
{
    if (gems_ <= 0) return false;
    auto& w = World::get();
    auto& me = cambc::self();
    const Field* af = w.altar_field(true);
    int ad = af ? here(af) : 30;
    if (ad >= kInf) ad = 30;
    const int banked = static_cast<int>(cambc::global_submitted_gems(cambc::team::ally));
    const bool endgame = banked + gems_ >= static_cast<int>(cambc::constants::winning_gems);
    const int threshold = plan_ok_ ? plan_.bank_gems : cfg::bank_gems;
    const bool late = round_ + ad + gems_ / 10 + 8 >= static_cast<int>(cambc::constants::max_rounds);
    const bool want = endgame || late || gems_ >= threshold || (gems_ >= cfg::bank_near_gems && ad <= cfg::bank_near_tiles);
    if (!want) return false;
    mode_ = Mode::bank;
    for (std::size_t a = 0; a < w.altars.size(); ++a) {
        const vec2 alt = w.altars[a];
        if (cheb(alt, pos_) > cambc::constants::submit_gems_range) continue;
        if (cambc::vision::has_altar_lock(alt)) {
            const auto lock = cambc::vision::altar_lock_at(alt);
            if (lock.is_enemy()) {
                if (can_attack_ && cheb(alt, pos_) <= kit_.attack_range) { me.attack(lock); attacked_ = true; return true; }
                continue;
            }
        }
        me.submit_gems(alt);
        TT_LOG(1, "bank r%d gems %d banked %d\n", round_, gems_, banked);
        return true;
    }
    if (step_on_field(af, true)) return true;
    return step_greedy(w.altars.empty() ? pos_ : w.altars.front());
}

// -------------------------------------------------------------------------------- farming

bool Bot::allowed(int g, bool relaxed) const
{
    auto& w = World::get();
    if (plan_ok_) {
        const int o = plan_.owner[g];
        if (o != kNoOwner && o != unit_) {
            // Another unit's group is fair game while all its bodies are away.
            for (int b = 0; b < n_bodies_; ++b)
                if (plan_.body_unit[b] == o && round_ - body_absent_[static_cast<std::size_t>(b)] > 30) return false;
        }
        if (o == kNoOwner && !plan_.ours[g] && !relaxed) return false;
    } else if (!relaxed) {
        const auto& grp = w.groups[static_cast<std::size_t>(g)];
        if (cheb(grp.centre, w.my_spawn_centre) > cheb(grp.centre, w.enemy_spawn_centre) + cfg::territory_margin) return false;
    }
    // One body per group, except our own unit's party. Two solo claims on one group resolve by
    // slot (the lower keeps it), so both never yield; a capture claim always holds.
    for (int b = 0; b < n_bodies_; ++b) {
        if (b == slot_ || claims_[static_cast<std::size_t>(b)] != g) continue;
        const int st = status_[static_cast<std::size_t>(b)];
        const bool mate = std::find(mates_.begin(), mates_.end(), b) != mates_.end();
        if (mate && st == kParty) continue;
        if (st == kCapturing || b < slot_ || target_ != g) return false;
    }
    return true;
}

// Gems (plus weighted gold) per champion-round of taking g with n bodies. Labour per capture
// (E8): every body walks and fights for the clear (n x clear(n) = H / delta), one captures, and the
// health the camps take, W / (n delta), is paid back in heal trips (recall + heal + walk back).
bool Bot::rate_ctx(int g, RateCtx& c) const
{
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    const auto& k = know_[static_cast<std::size_t>(g)];
    c.ok = false;
    if (grp.gems + grp.gold <= 0) return false;
    if (k.banned_until > round_ || k.danger_until > round_) return false;
    const int travel = estimate_dist(g);
    if (travel >= kInf) return false;
    const int arrive = round_ + travel;
    if (kObservedAvailability && k.respawn_at <= round_
        && observed_unavailable(k.dead, k.reward, k.seen, round_)) return false;
    c.wait = 0;
    if (k.dead && !k.reward && k.respawn_at > arrive) c.wait = static_cast<float>(k.respawn_at - arrive);
    if (c.wait > cfg::wait_max) return false;
    c.travel = static_cast<float>(travel);
    c.reward = k.dead && k.reward && round_ - k.seen <= cfg::reward_memory;
    if (k.dead && k.reward && !c.reward) {
        const int due = k.seen + cfg::capture_rounds + grp.cooldown;
        if (due > arrive) c.wait = static_cast<float>(due - arrive);
        if (c.wait > cfg::wait_max) return false;
    }
    c.spec = &remaining_spec(g);
    BodySpec body = body_spec(kit_, max_hp_, str_, focus_);
    if constexpr (cfg::equipment_model) owned_equipment().apply(body);
    c.taken = body.taken;
    c.shield = body.shield;
    c.fragile = body.fragile;
    const Field* sf = w.spawn_field(false);
    c.home = static_cast<float>(sf ? std::min(sf->get(grp.centre), 300) : cheb(grp.centre, w.my_spawn_centre));
    c.trip = cfg::heal_trip_base + c.home;
    const float weight = plan_ok_ ? plan_.gold_weight : cfg::gold_weight;
    c.value = grp.gems + weight * grp.gold * body.gold_mult;
    if (plan_ok_ && plan_.owner[g] == unit_) c.value *= 1.15f;
    c.ok = true;
    return true;
}

// Labour per capture (E8): every body walks and fights for the clear (n x clear(n) = H / delta),
// one captures, and the health the camps take, W / (n delta), is paid back in heal trips.
float Bot::rate_n(const RateCtx& c, int g, int n) const
{
    if (!c.ok || n < 1) return 0.0f;
    const float nf = static_cast<float>(n);
    Clear cl;
    if (c.reward) {
        cl = Clear{};
    } else if (n == 1 && c.spec == &World::get().groups[static_cast<std::size_t>(g)].spec) {
        const auto& sc = solo(g);
        cl = sc.clear;
        cl.damage = sc.damage;
    } else {
        cl = smith_fast(*c.spec, my_dps() * nf);
        cl.damage = c.spec->harmless ? 0.0f : std::max(0.0f, cl.damage * c.taken - c.shield * cl.rounds);
    }
    const float pool = n == 1 ? hp_ : max_hp_ * nf * 0.8f;
    const float pool_max = n == 1 ? max_hp_ : max_hp_ * nf * 0.8f;
    const float margin = c.fragile && n == 1 ? cfg::camp_margin * 1.5f : cfg::camp_margin;
    const float need = cl.damage * margin;
    if (need > pool_max * static_cast<float>(cfg::siege_max_trips)) return 0.0f; // more bodies or items
    float labour = nf * (c.travel + c.wait * cfg::wait_weight + cl.rounds) + cfg::capture_rounds;
    labour += cl.damage / (0.8f * max_hp_) * c.trip; // health is time: every bar is a heal trip
    if (need > pool) labour += nf * (6.0f + c.home - c.travel * 0.5f); // heal first
    return labour > 0 ? c.value / labour : 0.0f;
}

float Bot::target_rate(int g, int n) const
{
    RateCtx c;
    if (!rate_ctx(g, c)) return 0.0f;
    return rate_n(c, g, n);
}

int Bot::party_size(const RateCtx& c, int g, float* rate) const
{
    int best = 0;
    float br = 0.0f;
    const int cap = std::min<int>(static_cast<int>(mates_.size()), alive_mates());
    for (int n = 2; n <= cap; ++n) {
        const float r = rate_n(c, g, n);
        if (r > br) { br = r; best = n; }
    }
    if (rate) *rate = br;
    return best;
}

int Bot::party_size(int g, float* rate) const
{
    RateCtx c;
    if (!rate_ctx(g, c)) { if (rate) *rate = 0.0f; return 0; }
    return party_size(c, g, rate);
}

int Bot::choose_target(bool relaxed)
{
    if constexpr (cfg::itinerary_jobs) return choose_itinerary(relaxed);
    if constexpr (cfg::safe_dispatch) {
        if (round_ >= cfg::safe_dispatch_after && (cfg::safe_dispatch_slot < 0 || slot_ == cfg::safe_dispatch_slot))
            return choose_target_safe(relaxed);
    }
    if constexpr (cfg::scheduler_fuel_guard) {
        if (fuel_left() < 35000) return target_ >= 0 && allowed(target_, relaxed) ? target_ : -1;
    }
    if constexpr (cfg::health_score_budget) {
        if (fuel_left() < kDispatchEntryFuel) {
            dispatch_pending() = true;
            if (target_ < 0) return -1;
            const auto& known = know_[static_cast<std::size_t>(target_)];
            return known.banned_until <= round_ && known.danger_until <= round_ && allowed(target_, relaxed) ? target_ : -1;
        }
    }
    auto& w = World::get();
    const int n_groups = static_cast<int>(w.groups.size());
    // Calls of our mates (status kParty) and the mates free to answer one.
    std::array<int, kMaxGroups> called{};
    int free_mates = 0, any_call = -1;
    for (int b : mates_) {
        if (b == slot_) continue;
        const int st = status_[static_cast<std::size_t>(b)];
        const int cg = claims_[static_cast<std::size_t>(b)];
        if (st == kParty && cg >= 0 && cg < kMaxGroups) { ++called[static_cast<std::size_t>(cg)]; any_call = cg; }
        if (st == kSolo) ++free_mates;
    }
    const int unit_cap = std::min<int>(static_cast<int>(mates_.size()), alive_mates());
    // Cheap optimistic bound per allowed group, then full rates for the best few.
    constexpr int kTop = 10;
    int top[kTop];
    float top_bound[kTop];
    int n_top = 0;
    const float dps = std::max(1.0f, my_dps());
    bool discovery_incomplete = false;
    if constexpr (cfg::health_score_budget) {
        const auto shortlist = bounded_shortlist(n_groups, top, top_bound, kTop, [&](int g) {
            const auto& k = know_[static_cast<std::size_t>(g)];
            if (k.banned_until > round_ || k.danger_until > round_ || !allowed(g, relaxed)) return -1.0f;
            const auto& grp = w.groups[static_cast<std::size_t>(g)];
            const auto& sc = solo(g);
            if (sc.party && unit_cap < 2) return -1.0f;
            const float n = sc.party ? static_cast<float>(unit_cap) : 1.0f;
            const float lower = static_cast<float>(cheb(pos_, grp.centre)) + cfg::capture_rounds + grp.hp_total / (dps * n);
            float bound = sc.value / (lower * (sc.party ? 2.0f : 1.0f));
            if (called[static_cast<std::size_t>(g)] > 0) bound *= 1.3f;
            return bound;
        }, &fuel_left);
        n_top = shortlist.count;
        discovery_incomplete = shortlist.incomplete;
    } else {
    for (int g = 0; g < n_groups; ++g) {
        if constexpr (cfg::scheduler_fuel_guard) { if (fuel_left() < 30000) break; }
        const auto& k = know_[static_cast<std::size_t>(g)];
        if (k.banned_until > round_ || k.danger_until > round_) continue;
        if (!allowed(g, relaxed)) continue;
        const auto& grp = w.groups[static_cast<std::size_t>(g)];
        const auto& sc = solo(g);
        if (sc.party && unit_cap < 2) continue;
        const float n = sc.party ? static_cast<float>(unit_cap) : 1.0f;
        const float lower = static_cast<float>(cheb(pos_, grp.centre)) + cfg::capture_rounds + grp.hp_total / (dps * n);
        float bound = sc.value / (lower * (sc.party ? 2.0f : 1.0f));
        if (called[static_cast<std::size_t>(g)] > 0) bound *= 1.3f;
        int at = n_top < kTop ? n_top++ : kTop;
        if (at == kTop) {
            if (bound <= top_bound[kTop - 1]) continue;
            at = kTop - 1;
        }
        while (at > 0 && top_bound[at - 1] < bound) {
            top[at] = top[at - 1];
            top_bound[at] = top_bound[at - 1];
            --at;
        }
        top[at] = g;
        top_bound[at] = bound;
    }
    }
    // Options per group: alone; join a mate's call (up to its best party size); call a party
    // when enough mates are free. Calls already open are preferred, so calls gather, not split.
    auto option = [&](int g, bool* party) -> float {
        *party = false;
        RateCtx ctx;
        if (!rate_ctx(g, ctx)) return 0.0f;
        const int open = called[static_cast<std::size_t>(g)];
        float best = needs_party(g) ? 0.0f : rate_n(ctx, g, 1);
        if (unit_cap >= 2) {
            float pr = 0.0f;
            const int k = party_size(ctx, g, &pr);
            if (k >= 2) {
                if (open > 0 && open < k) pr *= 1.3f;                  // join
                else if (open == 0 && free_mates < k - 1) pr = 0.0f;   // nobody to call
                else if (open == 0 && any_call >= 0) pr *= 0.7f;       // answer the open call first
                else if (open >= k) pr = 0.0f;                         // full
                if (pr > best) { best = pr; *party = true; }
            }
        }
        return best;
    };
    auto audit = [&](int selected, float threshold) {
        if constexpr (cfg::dispatch_audit) {
            static bool witnessed = false;
            static bool rejected = false;
            static int attempts = 0;
            if (witnessed || attempts >= 48 || round_ < 15 || fuel_left() < 65000) return;
            ++attempts;
            for (int g = 0; g < n_groups && fuel_left() > 35000; ++g) {
                const auto& known = know_[static_cast<std::size_t>(g)];
                if (!known.dead || !known.reward || !allowed(g, relaxed)) continue;
                bool party = false;
                const float rate = option(g, &party);
                if (g != selected && rate > threshold * 1.01f) {
                    const auto& group = w.groups[static_cast<std::size_t>(g)];
                    if (known.seen != round_) continue;
                    const auto native = group.handle->state();
                    if (!std::holds_alternative<cambc::map::camp_group_reward_available>(native)) {
                        if (!rejected) {
                            std::printf("[%d] dispatch stale r%d group %d native %u state %zu pos %d,%d\n", slot_, round_, g,
                                static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(*group.handle)), native.index()+1, pos_.x,pos_.y);
                            rejected = true;
                        }
                        continue;
                    }
                    int rank = -1;
                    for (int i = 0; i < n_top; ++i) if (top[i] == g) rank = i;
                    std::printf("[%d] dispatch witness r%d chosen %d omitted %d native %u rate %.5f threshold %.5f rank %d seen %d pos %d,%d gems %d gold %d\n",
                                slot_, round_, selected, g, static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(*group.handle)), rate,
                                threshold, rank, known.seen, pos_.x, pos_.y, group.gems, group.gold);
                    witnessed = true;
                    return;
                }
            }
        }
    };
    int best = -1;
    bool best_party = false;
    float bv = 0.0f;
    if constexpr (cfg::health_score_budget) {
        const auto choice = bounded_options(top, top_bound, n_top, discovery_incomplete, target_, party_, option,
            [&](int g) {
                const auto& known = know_[static_cast<std::size_t>(g)];
                return known.banned_until <= round_ && known.danger_until <= round_ && allowed(g, relaxed);
            }, [&] {
                const int d = field_dist(target_);
                return d >= 0 && d <= 5 ? 3.0f : cfg::switch_ratio;
            }, &fuel_left);
        dispatch_pending() = dispatch_pending() || choice.incomplete;
        if (choice.kept) {
            if (!choice.unscored) audit(target_, choice.keep_threshold);
            party_ = choice.party;
            return target_;
        }
        best = choice.group;
        best_party = choice.party;
        bv = choice.rate;
    } else {
    for (int i = 0; i < n_top; ++i) {
        if constexpr (cfg::scheduler_fuel_guard) { if (fuel_left() < 25000) break; }
        if (top_bound[i] * 1.3f <= bv) break; // bounds are sorted: nothing below can win
        bool party = false;
        const float r = option(top[i], &party);
        if (r > bv) { bv = r; best = top[i]; best_party = party; }
    }
    if (target_ >= 0 && best != target_ && allowed(target_, relaxed)
        && (!cfg::scheduler_fuel_guard || fuel_left() >= 25000)) {
        bool party = false;
        const float cur = option(target_, &party);
        const int d = field_dist(target_);
        const float keep = d >= 0 && d <= 5 ? 3.0f : cfg::switch_ratio;
        if (cur > 0 && cur * keep >= bv) { audit(target_, cur * keep); party_ = party || party_; return target_; }
    }
    }
    audit(best, bv);
    if (best != target_) {
        target_since_ = round_;
        engaged_round_ = -1;
        stage_since_ = -1;
        stuck_ = 0;
    }
    party_ = best >= 0 && best_party;
    return best;
}

int Bot::choose_target_safe(bool relaxed)
{
    // The same option model as the original caller, with complete candidate coverage and
    // actual ownership/value in each bound. Fuel limits can stop search, never forge a bound.
    if (fuel_left() < 35000) return target_ >= 0 && allowed(target_, relaxed) ? target_ : -1;
    auto& w = World::get();
    const int n_groups = std::min<int>(static_cast<int>(w.groups.size()), kMaxGroups);
    std::array<int, kMaxGroups> called{};
    int free_mates = 0, any_call = -1;
    for (int b : mates_) {
        if (b == slot_) continue;
        const int st = status_[static_cast<std::size_t>(b)];
        const int g = claims_[static_cast<std::size_t>(b)];
        if (st == kParty && g >= 0 && g < kMaxGroups) { ++called[static_cast<std::size_t>(g)]; any_call = g; }
        if (st == kSolo) ++free_mates;
    }
    const int unit_cap = std::min<int>(static_cast<int>(mates_.size()), alive_mates());
    DispatchCandidate candidates[kMaxGroups];
    int count = 0;
    BodySpec body = body_spec(kit_, max_hp_, str_, focus_);
    if constexpr (cfg::equipment_model) owned_equipment().apply(body);
    const float weight = plan_ok_ ? plan_.gold_weight : cfg::gold_weight;
    for (int g = 0; g < n_groups && fuel_left() > 30000; ++g) {
        const auto& k = know_[static_cast<std::size_t>(g)];
        if (k.banned_until > round_ || k.danger_until > round_ || !allowed(g, relaxed)) continue;
        const auto& grp = w.groups[static_cast<std::size_t>(g)];
        const int travel = estimate_dist(g);
        if (travel >= kInf) continue;
        float value = grp.gems + weight * grp.gold * body.gold_mult;
        if (plan_ok_ && plan_.owner[g] == unit_) value *= 1.15f;
        if (value <= 0) continue;
        candidates[count++] = { g, dispatch_upper(value, static_cast<float>(travel), cfg::capture_rounds) };
    }
    auto option = [&](int g, bool* party) -> float {
        *party = false;
        RateCtx ctx;
        if (!rate_ctx(g, ctx)) return 0.0f;
        const int open = called[static_cast<std::size_t>(g)];
        float best = needs_party(g) ? 0.0f : rate_n(ctx, g, 1);
        if (unit_cap >= 2) {
            float pr = 0;
            const int n = party_size(ctx, g, &pr);
            if (n >= 2) {
                if (open > 0 && open < n) pr *= 1.3f;
                else if (open == 0 && free_mates < n - 1) pr = 0;
                else if (open == 0 && any_call >= 0) pr *= 0.7f;
                else if (open >= n) pr = 0;
                if (pr > best) { best = pr; *party = true; }
            }
        }
        return best;
    };
    auto choice = ordered_dispatch(candidates, count, option, [&] { return fuel_left() > 25000; });
    if (target_ >= 0 && choice.group != target_ && allowed(target_, relaxed) && fuel_left() > 25000) {
        bool party = false;
        const float current = option(target_, &party);
        const int distance = field_dist(target_);
        const float keep = distance >= 0 && distance <= 5 ? 3.0f : cfg::switch_ratio;
        if (current > 0 && current * keep >= choice.rate) { party_ = party || party_; return target_; }
    }
    if (choice.group != target_) {
        target_since_ = round_;
        engaged_round_ = -1;
        stage_since_ = -1;
        stuck_ = 0;
    }
    party_ = choice.group >= 0 && choice.party;
    return choice.group;
}

int Bot::choose_itinerary(bool relaxed)
{
    auto& w=World::get();
    auto& job=itinerary();
    auto select=[&](int g, bool party) {
        if(g!=target_) {
            target_since_=round_; engaged_round_=-1; stage_since_=-1; stuck_=0;
        }
        party_=g>=0 && party;
        return g;
    };
    if(job.group>=0) {
        const int g=job.group;
        const auto& known=know_[static_cast<std::size_t>(g)];
        const bool unavailable=observed_unavailable(known.dead,known.reward,known.seen,round_);
        if(unavailable || known.banned_until>round_ || known.danger_until>round_ || round_>job.deadline
           || !allowed(g,true)) {
            const auto reason=unavailable?JobRelease::external_capture:
                round_>job.deadline?JobRelease::expired:JobRelease::denied;
            TT_LOG(1,"job release r%d g%d why %d age %d\n",round_,g,static_cast<int>(reason),round_-job.issued);
            job.release(g,reason);
        } else {
            job.phase=known.dead&&known.reward?JobPhase::capturing:JobPhase::clearing;
            return select(g,needs_party(g) && job.crew>1);
        }
    }
    if(fuel_left()<40000) return select(-1,false);
    // Existing committed crews get first claim on members as their prior jobs complete.
    int caller=kMaxBodies, called_group=-1, called_crew=0;
    for(int b:mates_) {
        if(b==slot_ || b>=caller || status_[static_cast<std::size_t>(b)]!=kParty) continue;
        const int g=claims_[static_cast<std::size_t>(b)];
        if(g<0 || g>=static_cast<int>(know_.size()) || !allowed(g,true)) continue;
        RateCtx context;
        if(!rate_ctx(g,context)) continue;
        float rate=0;
        const int crew=party_size(context,g,&rate);
        int members=0;
        for(int mate:mates_) if(claims_[static_cast<std::size_t>(mate)]==g && status_[static_cast<std::size_t>(mate)]==kParty) ++members;
        if(crew>=2 && members<crew && rate>0) {caller=b;called_group=g;called_crew=crew;}
    }
    int order[kMaxGroups],count=0;
    if(plan_ok_) {
        for(int g=0;g<std::min<int>(static_cast<int>(w.groups.size()),kMaxGroups);++g)
            if(plan_.owner[g]==unit_) order[count++]=g;
        for(int i=1;i<count;++i) {
            int g=order[i],at=i;
            while(at>0 && plan_.tour[order[at-1]]>plan_.tour[g]) {order[at]=order[at-1];--at;}
            order[at]=g;
        }
    }
    auto issue=[&](int g,int rank,int crew) {
        RateCtx context;
        if(!rate_ctx(g,context)) return select(-1,false);
        int assembly=20;
        if(crew>1) {
            for(int b:mates_) {
                if(b==slot_) continue;
                const int from=claims_[static_cast<std::size_t>(b)];
                int travel=from>=0&&plan_ok_?plan_.distance(from,g):-1;
                if(travel<0 || travel>=kInf) travel=static_cast<int>(context.home);
                float work=0;
                if(from>=0 && from!=g && from<static_cast<int>(w.groups.size()))
                    work=smith_fast(w.groups[static_cast<std::size_t>(from)].spec,std::max(1.0f,my_dps())).rounds+cfg::capture_rounds;
                assembly=std::max(assembly,std::min(120,static_cast<int>(travel+work+10)));
            }
        }
        const auto clear=context.reward?Clear{}:smith_fast(*context.spec,std::max(1.0f,my_dps()*crew));
        const int duration=std::clamp(static_cast<int>(context.travel+context.wait+clear.rounds+context.trip*3+cfg::capture_rounds)+assembly,80,400);
        job.issue(g,rank,crew,round_,round_+static_cast<int>(context.wait),round_+assembly,round_+duration);
        TT_LOG(1,"job issue r%d g%d rank %d crew %d ready %d assembly %d end %d\n",round_,g,rank,crew,job.ready,job.assembly_deadline,job.deadline);
        return select(g,crew>1);
    };
    if(called_group>=0) {
        int rank=plan_ok_?plan_.tour[called_group]:job.cursor;
        return issue(called_group,rank,called_crew);
    }
    // The tour is an executable itinerary: a job is committed through clear/recovery/capture.
    // Only an observed completion/denial or a bounded expiry advances its release cursor.
    for(int offset=0;offset<count && fuel_left()>30000;++offset) {
        const int rank=(job.cursor+offset)%count;
        const int g=order[rank];
        const auto& known=know_[static_cast<std::size_t>(g)];
        if(known.banned_until>round_ || known.danger_until>round_ || !allowed(g,relaxed)) continue;
        RateCtx context;
        if(!rate_ctx(g,context)) continue;
        int crew=1;
        float rate=rate_n(context,g,1);
        if(needs_party(g)) {crew=party_size(context,g,&rate);}
        if(crew<1 || rate<=0) continue;
        return issue(g,rank,crew);
    }
    // Preserve useful work when every itinerary job is unavailable; never invent availability.
    const int fallback=choose_target_safe(relaxed);
    if(fallback>=0) return issue(fallback,job.cursor,party_?std::max(2,party_size(fallback,nullptr)):1);
    return select(-1,false);
}

void Bot::farm()
{
    auto& w = World::get();
    if (mode_ == Mode::bank || mode_ == Mode::flee) mode_ = Mode::farm;
    const int under = w.group_at(pos_);
    const int prev = target_;
    if (under >= 0) {
        const auto& ku = know_[static_cast<std::size_t>(under)];
        // On a cleared group's tile with the reward waiting: hold until it arrives.
        if (ku.dead && ku.reward) {
            if constexpr (cfg::itinerary_jobs) {
                int owner=slot_;
                for(std::size_t i=0;i<allies_now_.size();++i) {
                    const int body=allies_now_slot_[i];
                    if(body>=0 && body<owner && w.group_at(v2(allies_now_[i].pos))==under) owner=body;
                }
                if(owner!=slot_) {
                    itinerary().release(under,JobRelease::assistance_finished);
                    target_=-1; party_=false; mode_=Mode::farm;
                    leave_camp_tiles();
                    return;
                }
                itinerary().phase=JobPhase::capturing;
            }
            if (target_ != under) TT_LOG(2, "hold r%d g%d\n", round_, under);
            target_ = under;
            party_ = false;
            mode_ = Mode::capture;
            return;
        }
        // A respawning group waits for its tiles to clear: never stand on them.
        if (ku.dead && !ku.reward) leave_camp_tiles();
    }
    {
        // A mate's open call is answered at once by an idle body (stale target), else every few rounds.
        const bool stale = target_ < 0 || round_ >= next_choose_ || know_[static_cast<std::size_t>(target_)].banned_until > round_
            || !allowed(target_, true);
        if (stale) {
            if constexpr (cfg::health_score_budget) dispatch_pending() = false;
            target_ = choose_target(false);
            if (target_ < 0) target_ = choose_target(true);
            next_choose_ = round_ + cfg::replan_rounds;
            if constexpr (cfg::health_score_budget) {
                if (dispatch_pending()) next_choose_ = round_ + 1;
            }
        }
    }
    if (target_ != prev && target_ >= 0)
        TT_LOG(2, "target r%d g%d own %d party %d\n", round_, target_, plan_ok_ && plan_.owner[target_] == unit_ ? 1 : 0, party_ ? 1 : 0);
    mode_ = Mode::farm;
    if (target_ < 0) {
        party_ = false;
        if (under >= 0) step_to_safety(true);
        else if (cheb(pos_, w.my_spawn_centre) > 4) step_on_field(w.spawn_field(true), true);
        return;
    }
    auto& k = know_[static_cast<std::size_t>(target_)];
    if (k.dead && !k.reward && (k.respawn_at > round_
        || (kObservedAvailability && observed_unavailable(k.dead, k.reward, k.seen, round_)))) {
        if (kObservedAvailability && round_ - target_since_ > cfg::wait_max) {
            k.banned_until = round_ + cfg::ban_rounds;
            target_ = -1;
            party_ = false;
            return;
        }
        // Wait out the respawn off the camp tiles, within reach.
        if (under >= 0) step_to_safety(false);
        else if (field_dist(target_) > 3) step_on_field(w.group_field(target_, true), true);
        return;
    }
    bool progress;
    if (k.dead) {
        bool mate_capturing = mate_on_group(target_);
        for (int b : mates_)
            if (b != slot_ && status_[static_cast<std::size_t>(b)] == kCapturing && claims_[static_cast<std::size_t>(b)] == target_)
                mate_capturing = true;
        if (mate_capturing && under != target_) {
            // A mate is capturing it: move on.
            k.banned_until = round_ + 3;
            target_ = -1;
            party_ = false;
            return;
        }
        progress = capture_group(target_);
    } else {
        progress = party_ ? party_fight(target_) : fight_group(target_, false);
    }
    stuck_ = progress ? 0 : stuck_ + 1;
    if (stuck_ > cfg::stuck_limit && target_ >= 0) {
        know_[static_cast<std::size_t>(target_)].banned_until = round_ + cfg::ban_rounds;
        TT_LOG(2, "stuck r%d g%d\n", round_, target_);
        target_ = -1;
        party_ = false;
        stuck_ = 0;
    }
}

bool Bot::attack_member(int m)
{
    auto& w = World::get();
    const auto& mem = w.members[static_cast<std::size_t>(m)];
    if (!can_attack_ || !mem.camp || cheb(mem.pos, pos_) > kit_.attack_range) return false;
    if (!camp_occ_.is_set(mem.pos) || !mem.camp->is_visible()) return false;
    auto& me = cambc::self();
    me.attack(*mem.camp);
    attacked_ = true;
    last_hit_round_ = round_;
    can_attack_ = me.can_attack();
    if (!kit_.melee) can_move_ = false;
    return true;
}

void Bot::cast_after_attack(int g)
{
    auto& me = cambc::self();
    if (cast_ || !me.can_cast()) return;
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    const auto ab = me.abilities();
    // Alive visible members of the target group.
    std::vector<int> alive;
    for (int m : grp.members) {
        const auto& mem = w.members[static_cast<std::size_t>(m)];
        if (camp_occ_.is_set(mem.pos) && visible_.is_set(mem.pos)) alive.push_back(m);
    }
    if (alive.empty()) return;
    for (int s = 0; s < 2 && !cast_; ++s) {
        const SlotKit& sk = kit_.slot[s];
        if (cooldown_[s] != 0) continue;
        if (sk.exclusive && moved_) continue;
        const auto& a = ab[static_cast<std::size_t>(s)];
        if ((sk.kind == Slot::damage || sk.kind == Slot::cc) && sk.camps && sk.modifier > 0) {
            int best = -1;
            float bscore = -1;
            for (int m : alive) {
                const auto& mem = w.members[static_cast<std::size_t>(m)];
                if (cheb(mem.pos, pos_) > static_cast<int>(sk.range)) continue;
                const float score = mem.stats.str * 10.0f + 1.0f;
                if (score > bscore) { bscore = score; best = m; }
            }
            if (best >= 0 && w.members[static_cast<std::size_t>(best)].camp->is_visible()) {
                if (a.use(*w.members[static_cast<std::size_t>(best)].camp)) { cast_ = true; last_hit_round_ = round_; }
            }
        } else if (sk.kind == Slot::aoe && sk.modifier > 0) {
            // Ground cast covering the most members within its radius.
            vec2 best = pos_;
            int hits = 0;
            for (int m : alive) {
                const auto& c = w.members[static_cast<std::size_t>(m)].pos;
                if (cheb(c, pos_) > static_cast<int>(sk.range)) continue;
                int h = 0;
                for (int o : alive)
                    if (cheb(w.members[static_cast<std::size_t>(o)].pos, c) <= static_cast<int>(sk.radius)) ++h;
                if (h > hits) { hits = h; best = c; }
            }
            if (hits > 0 && a.use(best)) { cast_ = true; last_hit_round_ = round_; }
        } else if (sk.kind == Slot::reset && attacked_ && !me.can_attack()) {
            // Only worth it with a member still in reach.
            int pick = -1;
            float bs = -1;
            for (int m : alive) {
                const auto& mem = w.members[static_cast<std::size_t>(m)];
                if (cheb(mem.pos, pos_) > kit_.attack_range || !mem.camp->is_visible()) continue;
                const float sc = mem.stats.str / std::max(1.0f, mem.camp->health()) + 0.001f;
                if (sc > bs) { bs = sc; pick = m; }
            }
            if (pick >= 0 && a.use()) {
                cast_ = true;
                can_attack_ = me.can_attack();
                attack_member(pick);
            }
        } else if (sk.kind == Slot::shield && camp_in_at(pos_) > 0) {
            if (a.use()) cast_ = true;
        }
    }
}

// A party target: stage outside the camps' reach until the unit is together, then fight.
bool Bot::party_fight(int g)
{
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    const int alive = alive_mates();
    if (alive < 2) { party_ = false; return fight_group(g, false); }
    // E8: what matters is whether the bodies here can take the trip, not whether all have come.
    const int present = mates_near();
    bool ready = group_awake(g) || present >= alive;
    if (!ready && present >= 2) {
        const GroupSpec& spec = remaining_spec(g);
        const float n = static_cast<float>(present);
        const Clear c = smith_fast(spec, my_dps() * n);
        const float pool = max_hp_ * n * 0.8f;
        ready = c.damage * cfg::camp_margin <= pool * static_cast<float>(cfg::siege_max_trips);
    }
    if (ready) {
        stage_since_ = -1;
        return fight_group(g, true);
    }
    if (stage_since_ < 0) stage_since_ = round_;
    if ((cfg::itinerary_jobs && itinerary().group==g) ? round_>itinerary().assembly_deadline : round_-stage_since_>40) {
        know_[static_cast<std::size_t>(g)].banned_until = round_ + 60;
        TT_LOG(2, "stage timeout r%d g%d\n", round_, g);
        target_ = -1;
        party_ = false;
        return true;
    }
    // Staging tile: as close as possible while outside every living member's range.
    const Field* f = w.group_field(g, true);
    auto outside = [&](vec2 q) {
        for (int m : grp.members) {
            const auto& mem = w.members[static_cast<std::size_t>(m)];
            if (mem.stats.str <= 0) continue;
            if (visible_.is_set(mem.pos) && !camp_occ_.is_set(mem.pos)) continue;
            if (cheb(q, mem.pos) <= mem.stats.range + 1) return false;
        }
        return true;
    };
    if (!outside(pos_)) {
        // Back off until every living member is out of reach of our tile.
        vec2 best = pos_;
        int bs = -99;
        for (const auto d : kDirs) {
            const vec2 q = pos_ + d;
            if (!tile_free(q)) continue;
            int m_min = 99;
            for (int m : grp.members) {
                const auto& mem = w.members[static_cast<std::size_t>(m)];
                if (mem.stats.str > 0) m_min = std::min(m_min, cheb(q, mem.pos) - mem.stats.range);
            }
            if (m_min > bs) { bs = m_min; best = q; }
        }
        try_move(best);
        return true;
    }
    if (!f || !can_move_) return true;
    const int cur = f->get(pos_);
    vec2 best = pos_;
    int bv = cur;
    for (const auto d : kDirs) {
        const vec2 q = pos_ + d;
        if (!tile_free(q) || !outside(q)) continue;
        const int v = f->get(q);
        if (v < bv) { bv = v; best = q; }
    }
    try_move(best);
    return true;
}

bool Bot::fight_group(int g, bool party)
{
    auto& w = World::get();
    const auto& grp = w.groups[static_cast<std::size_t>(g)];
    auto& k = know_[static_cast<std::size_t>(g)];
    // Living members we can see, and the best one in reach (Smith: strength per health first).
    int best_in = -1, best_any = -1;
    float s_in = -1, s_any = -1;
    bool any_visible_alive = false;
    const bool awake = group_awake(g);
    for (std::size_t i = 0; i < grp.members.size(); ++i) {
        const int m = grp.members[i];
        const auto& mem = w.members[static_cast<std::size_t>(m)];
        if (!visible_.is_set(mem.pos) || !camp_occ_.is_set(mem.pos)) continue;
        any_visible_alive = true;
        const float left = i < static_cast<std::size_t>(kMaxMembers) && k.left[i] > 0 ? k.left[i] : mem.stats.hp;
        const float score = mem.stats.str > 0 ? 1000.0f * mem.stats.str / left : 1.0f / left;
        if (score > s_any) { s_any = score; best_any = m; }
        if (cheb(mem.pos, pos_) <= kit_.attack_range && score > s_in) { s_in = score; best_in = m; }
    }
    const Field* f = w.group_field(g, true);
    if (!any_visible_alive) {
        // Not in view yet: walk there (S2: avoid tiles enemies or awake camps cover).
        if (f && travel_dash(f)) return true;
        if (step_on_field(f, true)) return true;
        if (!f) return step_greedy(grp.centre);
        return false;
    }
    // S1 + S3: before waking a sleeping group alone, the forecast must fit our health.
    if (!awake && !grp.harmless && !party) {
        GroupSpec spec;
        for (std::size_t i = 0; i < grp.members.size(); ++i) {
            const auto& mem = w.members[static_cast<std::size_t>(grp.members[i])];
            const float left = i < static_cast<std::size_t>(kMaxMembers) && k.damaged ? k.left[i] : mem.stats.hp;
            if (left > 0) spec.add(left, mem.stats.str, mem.stats.range);
        }
        spec.sort_members();
        BodySpec body = body_spec(kit_, max_hp_, str_, focus_);
        if constexpr (cfg::equipment_model) owned_equipment().apply(body);
        Clear c = smith_clear(spec, my_dps());
        const float dmg = solo_damage(spec, body, c);
        const float margin = body.fragile ? cfg::camp_margin * 1.5f : cfg::camp_margin;
        // Other awake camps covering our tile add to the bill.
        const float extra = camp_in_at(pos_) * c.rounds;
        if ((dmg + extra) * margin > hp_) {
            // A siege trip starts healthy and must be one of at most siege_max_trips.
            const bool siege = (dmg + extra) * margin <= max_hp_ * static_cast<float>(cfg::siege_max_trips);
            if (hp_ < max_hp_ * 0.9f) {
                if (!on_spawn_ && try_recall("heal-before")) return true;
            } else if (siege) {
                // Fight on: the escape reserve (S3) ends the trip, the damage stays (M10).
            } else {
                k.banned_until = round_ + cfg::ban_rounds;
                target_ = -1;
                return true;
            }
        }
    }
    if (engaged_round_ < 0) { engaged_round_ = round_; last_hit_round_ = round_; }
    if (round_ - last_hit_round_ > cfg::fight_watchdog) {
        k.banned_until = round_ + cfg::ban_rounds;
        TT_LOG(2, "watchdog r%d g%d\n", round_, g);
        target_ = -1;
        party_ = false;
        return true;
    }
    if (best_in >= 0 && can_attack_) {
        attack_member(best_in);
        cast_after_attack(g);
        return true;
    }
    // Approach the Smith target: melee to an adjacent tile, ranged to any tile within range
    // (ranged prefer tiles where a monster has a nearer champion to strike).
    if (best_any >= 0) {
        const auto& tgt = w.members[static_cast<std::size_t>(best_any)];
        vec2 best = pos_;
        float bs = 1e9f;
        auto tile_score = [&](vec2 q) {
            const int dt = cheb(q, tgt.pos);
            const int fv = f ? f->get(q) : dt;
            float s = static_cast<float>(std::max(0, dt - kit_.attack_range)) * 20.0f + static_cast<float>(std::min(fv, 400));
            s += enemy_incoming(p2(q), enemies_now_.data(), static_cast<int>(enemies_now_.size())) * 2.0f;
            if (!kit_.melee) s += camp_in_at(q) * 0.2f;
            return s;
        };
        bool refused = false;
        for (const auto d : kDirs) {
            const vec2 q = pos_ + d;
            if (!tile_free(q)) continue;
            if (!reserve_ok(q)) { refused = true; continue; }
            const float s = tile_score(q);
            if (s < bs) { bs = s; best = q; }
        }
        if (refused && best == pos_) {
            // Too weak to step in and back out: heal first.
            if (try_recall("reserve")) return true;
            return step_to_safety(true);
        }
        if (best != pos_ && bs < tile_score(pos_) && can_move_ && !moved_) {
            if (try_move(best)) {
                if (kit_.melee && can_attack_) {
                    // Melee walk and strike in one turn.
                    int pick = -1;
                    float ps = -1;
                    for (int m : grp.members) {
                        const auto& mem = w.members[static_cast<std::size_t>(m)];
                        if (!camp_occ_.is_set(mem.pos) || cheb(mem.pos, pos_) > kit_.attack_range) continue;
                        const float sc = mem.stats.str + 0.001f;
                        if (sc > ps) { ps = sc; pick = m; }
                    }
                    if (pick >= 0) {
                        attack_member(pick);
                        cast_after_attack(g);
                    }
                }
                return true;
            }
            units_.set(best);
        }
        if (step_on_field(f, true)) return true;
    }
    return false;
}

bool Bot::capture_group(int g)
{
    auto& w = World::get();
    mode_ = Mode::capture;
    if (w.group_at(pos_) == g) return true; // standing on a member tile: the capture runs
    const Field* f = w.group_field(g, true);
    if (step_on_field(f, true)) return true;
    return f ? false : step_greedy(w.groups[static_cast<std::size_t>(g)].centre);
}

// ---------------------------------------------------------------------------------- shop

void Bot::shop()
{
    auto& me = cambc::self();
    if (gold_ < 30) return;
    bool bought = false;
    // Main stat: the one whose shard adds the most camp damage.
    const float d0 = camp_dps(kit_, str_, focus_, 1);
    const float ds = camp_dps(kit_, str_ + kit_.str_growth, focus_, 1) - d0;
    const float df = camp_dps(kit_, str_, focus_ + kit_.focus_growth, 1) - d0;
    const int main = df > ds ? kFocus : kStr;
    // Jungle (camp damage -30%) and Treasure (+20% gold) of the main stat, then health and damage.
    static constexpr std::array<BuildStep, 4> head{ { { kJungle, -1 }, { kTreasure, -1 }, { kBruiser, kHealth }, { kPower, -1 } } };
    for (int guard = 0; guard < 12 && gold_ >= 30; ++guard) {
        const auto counts = me.item_count();
        IT item = shard(kPower, main);
        bool found = false;
        for (const auto& step : head) {
            const int stat = step.stat < 0 ? main : step.stat;
            const int have = static_cast<int>(counts[shard(step.fam, stat)]) + 5 * static_cast<int>(counts[emblem(step.fam, stat)]);
            if (have < 5) { item = shard(step.fam, stat); found = true; break; }
        }
        if (!found) {
            // Afterwards alternate Bruiser health and Power of the main stat.
            const int bh = static_cast<int>(counts[shard(kBruiser, kHealth)]) + 5 * static_cast<int>(counts[emblem(kBruiser, kHealth)]);
            const int pm = static_cast<int>(counts[shard(kPower, main)]) + 5 * static_cast<int>(counts[emblem(kPower, main)]);
            item = bh <= pm ? shard(kBruiser, kHealth) : shard(kPower, main);
        }
        if (!me.can_buy(item, 1)) break;
        me.buy(item, 1);
        if constexpr (cfg::equipment_model) bought = true;
        gold_ = static_cast<int>(me.gold_count());
        TT_LOG(2, "buy r%d item %u gold %d\n", round_, static_cast<unsigned>(item), gold_);
    }
    if constexpr (cfg::equipment_model) {
        if (bought) {
            owned_equipment(true);
            max_hp_ = std::max(1.0f, me.max_health());
            hp_ = me.health();
            str_ = me.strength();
            focus_ = me.focus();
            for (auto& cache : solo_) cache.dps = -1;
        }
    }
}

} // namespace tt
