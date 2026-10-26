#pragma once

#include "service_micro.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Pure Submit/Hold compiler. Native observation, final dispatch freshness/fuel,
// and the declared external-event exclusions belong to the producer. A complete
// explicit roster is required; an unseen living body never becomes "no foe".
namespace wb::service_pressure {
using Authority = service_micro::Authority;
using Position = service_micro::Position;
inline constexpr std::size_t max_enemies = 16;
inline constexpr std::size_t max_spawn_cells = 64;
inline constexpr unsigned max_enemy_inputs = 8;
inline constexpr std::uint64_t max_energy = 1U << 20;

enum class Kit { Unknown, Heart, Ryth, Marcella };
enum class Channel { Unknown, None, Tribute, Recall, Teleport, Other };
enum class Lock { Unknown, Unlocked, Friendly, Hostile };
enum class InventoryModel { Unknown, StatsLockCrystalOnly };
enum class CompileStatus { Certified, NeedObservation, NeedPressureModel, Unsafe, Unsupported };
enum class Reason {
    None, InvalidIdentity, StaleAuthority, WrongOwner, InvalidActor, ActorDead,
    WrongKit, ChannelUnknown, AlreadyChannelled, SubmitUnavailable, ReceiptMissing,
    BagRange, BagChanged, AltarUnknown, AltarRange, AltarChanged, SettingsUnknown,
    UnsupportedSettings, RosterUnknown, RosterCapacity, DuplicateBody, MissingBody,
    UnsupportedKit, HiddenAlive, MissingStats, MissingCooldown, UnsupportedBuff,
    InventoryUnknown, UnsupportedDamage, SpawnUnknown, StaticFieldUnknown,
    ExternalInterference, LockRisk, ShoppingReach, DeadMayReach, NumericDomain,
    InsufficientHealth, EpisodeReplay, ReceiptReplay, AwaitResult, AwaitEffect,
    ConsumedInput, RefusalLimit, StartWindowMissed, Deadline, StartNotObserved,
    ChannelInterrupted, ChannelChanged, BankProofMissing, BankProofMismatch,
    EnvelopeChanged, DispatchInvalidated, ActualBanked
};

struct CarriedReceipt {
    bool present{}, verified{};
    int owner{-1}, group{-1}, gems{};
    std::uint64_t episode{}, serial{};
};
struct Subject {
    Authority authority{};
    bool observed{}, alive{}, passive_known{}, stun_immune{}, stunned{};
    Kit kit{Kit::Unknown};
    Position position{};
    float hp{}, max_hp{};
    int bag{}, gold{};
    std::uint64_t inventory_fingerprint{};
    Channel channel{Channel::Unknown};
    bool channel_time_known{}, submit_permitted{};
    int channel_remaining{-1};
    // Retained literal resources; deliberately NOT Submit preconditions.
    bool move{}, attack{}, cast{}, own_cooldowns_known{};
    std::array<int, 2> own_cooldowns{};
};
struct Altar {
    Authority authority{};
    bool observed{};
    int id{-1};
    Position position{};
    Lock lock{Lock::Unknown};
};
struct Rules {
    bool queried{};
    double submit_multiplier{0.1};
    int submit_base{}, submit_minimum{1}, submit_range{1};
    int ability_cooldown{8}, minimum_shop_recall{3};
    int maximum_rounds{2000};
    int crystal_ticks{3}, crystal_range{3};
    int maximum_attack_reach{5}, lock_range{2};
};
struct Enemy {
    Authority authority{};
    int id{-1};
    Kit kit{Kit::Unknown};
    bool life_known{}, alive{}, personally_visible{}, position_known{};
    Position position{};
    bool stats_known{}, cooldowns_known{}, buffs_known{};
    float strength{}, focus{};
    std::array<int, 2> cooldowns{};
    unsigned buff_count{};
    InventoryModel inventory{InventoryModel::Unknown};
    bool enhanced_damage_source{};
    bool held_lock_known{}, held_lock{}, crystals_known{};
    unsigned crystals{};
    Channel channel{Channel::Unknown};
    bool at_spawn_known{}, at_spawn{};
};
struct Roster {
    Authority authority{};
    std::uint64_t serial{};
    bool complete{};
    std::array<int, max_enemies> expected_ids{};
    std::size_t expected_count{}, observed_count{};
    std::array<Enemy, max_enemies> bodies{};
};
struct SpawnSet {
    std::uint64_t geometry_serial{};
    bool complete{};
    int width{}, height{};
    std::array<Position, max_spawn_cells> cells{};
    std::size_t count{};
};
struct ExternalField {
    Authority authority{};
    std::uint64_t geometry_serial{}, roster_serial{};
    Position hold_position{};
    bool static_sources_complete{};
    unsigned static_source_count{}, potentially_harmful_overlaps{};
    // Conditional producer obligations, not inferred from empty personal vision.
    // The second exclusion concerns OUR bag/altar/position and unmodeled source
    // changes; it does not freeze Heart's HP (his maximum passive is charged).
    bool no_unrepresented_damage{}, no_ally_interference{};
};
struct Problem {
    std::uint64_t episode{}, attempt_serial{}, initial_own_bank_serial{};
    Subject subject{};
    Altar altar{};
    CarriedReceipt receipt{};
    Rules rules{};
    Roster roster{};
    SpawnSet enemy_spawn{};
    ExternalField external{};
};
struct ShoppingProof {
    Authority authority{};
    std::uint64_t roster_serial{}, geometry_serial{};
    int enemy{-1};
    unsigned future_inputs{}, recall_spent_inputs{}, walk_branches{};
    int minimum_recall_damage_margin{std::numeric_limits<int>::max()};
    int minimum_recall_lock_margin{std::numeric_limits<int>::max()};
    int minimum_walk_damage_margin{std::numeric_limits<int>::max()};
    int minimum_walk_lock_margin{std::numeric_limits<int>::max()};
};
enum class Coverage { CurrentStats, CurrentReachExcluded, DeadSpawnExcluded };
struct EnemyBound {
    int id{-1};
    Coverage coverage{Coverage::CurrentStats};
    unsigned inputs{}, mobility{};
    float basic_upper{}, slot0_upper{}, slot1_upper{};
    std::uint64_t damage_energy{};
    int damage_reach_margin{}, lock_reach_margin{};
    ShoppingProof shopping{};
};
struct Certificate {
    Problem problem{};
    unsigned channel_ticks{}, failed_start_reserve{1}, owner_end_bound{}, enemy_inputs{};
    int deadline_round{-1}, latest_start_round{-1};
    std::uint64_t initial_health_energy{}, damage_energy{}, surviving_health_energy{};
    std::array<EnemyBound, max_enemies> enemies{};
    std::size_t count{};
};
struct Compilation {
    // Only Certified authorizes a word. All other statuses can retain partial
    // diagnostic fields; those fields supply no attempt permission.
    CompileStatus status{CompileStatus::NeedObservation};
    Reason reason{Reason::InvalidIdentity};
    Certificate certificate{};
};

inline int distance(Position a, Position b) {
    const auto dx = std::int64_t(a.x) - b.x, dy = std::int64_t(a.y) - b.y;
    const auto x = dx < 0 ? -dx : dx, y = dy < 0 ? -dy : dy;
    const auto value = x > y ? x : y;
    return value > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : int(value);
}
inline bool point_valid(Position p, const SpawnSet& s) {
    return p.x >= 0 && p.y >= 0 && p.x < s.width && p.y < s.height;
}
inline bool supported(Kit kit) { return kit == Kit::Heart || kit == Kit::Ryth || kit == Kit::Marcella; }
inline unsigned ceil_div(unsigned n, unsigned d) { return n / d + unsigned(n % d != 0); }
inline unsigned mobility(Kit kit, unsigned inputs) {
    if (!inputs) return 0;
    // Deliberately early/overcounted: ordinary movement AND all possible crystal
    // hops, plus Heart's dash. Ignores walls, channel exclusivity and cooldowns.
    return inputs + (kit == Kit::Heart ? 2U * ceil_div(inputs, 8) : 0U)
        + 3U * ceil_div(inputs, 3);
}
inline float multiply_up(float a, float b) {
    const double exact = double(a) * double(b);
    if (!std::isfinite(exact) || exact < 0 || exact > double(std::numeric_limits<float>::max()))
        return std::numeric_limits<float>::infinity();
    float value = float(exact);
    if (double(value) < exact) value = std::nextafter(value, std::numeric_limits<float>::infinity());
    return value;
}
inline bool energy(float value, std::uint64_t& out) {
    if (!std::isfinite(value) || value < 0 || double(value) > double(max_energy)) return false;
    out = std::uint64_t(std::ceil(double(value)));
    return true;
}
inline bool same(Authority a, Authority b) { return a == b; }

inline Compilation compile(const Problem& p) {
    Compilation result;
    auto reject = [&](CompileStatus status, Reason reason) {
        result.status = status; result.reason = reason; return result;
    };
    const auto& self = p.subject;
    const auto authority = self.authority;
    if (!service_micro::valid(authority) || !p.episode || !p.attempt_serial)
        return reject(CompileStatus::NeedObservation, Reason::InvalidIdentity);
    if (!self.observed) return reject(CompileStatus::NeedObservation, Reason::InvalidActor);
    if (!self.alive) return reject(CompileStatus::Unsafe, Reason::ActorDead);
    if (self.kit != Kit::Marcella || !self.passive_known || !self.stun_immune || self.stunned)
        return reject(CompileStatus::Unsupported, Reason::WrongKit);
    if (!std::isfinite(self.hp) || self.hp <= 0 || double(self.hp) > double(max_energy))
        return reject(CompileStatus::Unsupported, Reason::NumericDomain);
    if (self.channel == Channel::Unknown) return reject(CompileStatus::NeedObservation, Reason::ChannelUnknown);
    if (self.channel != Channel::None) return reject(CompileStatus::Unsupported, Reason::AlreadyChannelled);
    if (!self.submit_permitted) return reject(CompileStatus::NeedObservation, Reason::SubmitUnavailable);
    if (self.bag < 1 || self.bag > 30) return reject(CompileStatus::Unsupported, Reason::BagRange);
    if (!p.receipt.present || !p.receipt.verified || p.receipt.owner != authority.owner
        || p.receipt.group < 0 || p.receipt.episode != p.episode || !p.receipt.serial || p.receipt.gems != self.bag)
        return reject(CompileStatus::NeedObservation, Reason::ReceiptMissing);
    if (!p.rules.queried) return reject(CompileStatus::NeedObservation, Reason::SettingsUnknown);
    if (p.rules.submit_multiplier != 0.1 || p.rules.submit_base != 0 || p.rules.submit_minimum != 1
        || p.rules.submit_range != 1 || p.rules.ability_cooldown != 8 || p.rules.minimum_shop_recall != 3
        || p.rules.crystal_ticks != 3 || p.rules.crystal_range != 3
        || p.rules.maximum_attack_reach != 5 || p.rules.lock_range != 2 || p.rules.maximum_rounds <= 0)
        return reject(CompileStatus::Unsupported, Reason::UnsupportedSettings);
    if (!p.altar.observed || p.altar.id < 0 || !same(p.altar.authority, authority))
        return reject(CompileStatus::NeedObservation, Reason::AltarUnknown);
    if (p.altar.lock != Lock::Unlocked) return reject(CompileStatus::NeedPressureModel, Reason::AltarChanged);
    if (distance(self.position, p.altar.position) != 1)
        return reject(CompileStatus::Unsupported, Reason::AltarRange);
    const auto& roster = p.roster;
    if (!roster.complete || !roster.serial || !same(roster.authority, authority) || !roster.expected_count)
        return reject(CompileStatus::NeedPressureModel, Reason::RosterUnknown);
    if (roster.expected_count > max_enemies || roster.observed_count > max_enemies)
        return reject(CompileStatus::NeedPressureModel, Reason::RosterCapacity);
    if (roster.expected_count != roster.observed_count)
        return reject(CompileStatus::NeedPressureModel, Reason::MissingBody);
    const auto& spawn = p.enemy_spawn;
    if (!spawn.complete || !spawn.geometry_serial || !spawn.count || spawn.count > max_spawn_cells
        || spawn.width <= 0 || spawn.height <= 0 || spawn.width > 512 || spawn.height > 512)
        return reject(CompileStatus::NeedPressureModel, Reason::SpawnUnknown);
    if (!point_valid(self.position, spawn) || !point_valid(p.altar.position, spawn))
        return reject(CompileStatus::Unsupported, Reason::NumericDomain);
    for (std::size_t i = 0; i < spawn.count; ++i) {
        if (!point_valid(spawn.cells[i], spawn)) return reject(CompileStatus::NeedPressureModel, Reason::SpawnUnknown);
        for (std::size_t j = 0; j < i; ++j)
            if (spawn.cells[i] == spawn.cells[j]) return reject(CompileStatus::NeedPressureModel, Reason::SpawnUnknown);
    }
    const auto& ext = p.external;
    if (!same(ext.authority, authority) || ext.geometry_serial != spawn.geometry_serial
        || ext.roster_serial != roster.serial || ext.hold_position != self.position || !ext.static_sources_complete)
        return reject(CompileStatus::NeedPressureModel, Reason::StaticFieldUnknown);
    if (ext.potentially_harmful_overlaps || !ext.no_unrepresented_damage || !ext.no_ally_interference)
        return reject(CompileStatus::NeedPressureModel, Reason::ExternalInterference);
    for (std::size_t i = 0; i < roster.expected_count; ++i) {
        if (roster.expected_ids[i] < 0 || roster.expected_ids[i] == authority.owner)
            return reject(CompileStatus::NeedPressureModel, Reason::InvalidIdentity);
        for (std::size_t j = 0; j < i; ++j)
            if (roster.expected_ids[i] == roster.expected_ids[j] || roster.bodies[i].id == roster.bodies[j].id)
                return reject(CompileStatus::NeedPressureModel, Reason::DuplicateBody);
        unsigned occurrences = 0;
        for (std::size_t j = 0; j < roster.observed_count; ++j) occurrences += roster.bodies[j].id == roster.expected_ids[i];
        if (occurrences != 1) return reject(CompileStatus::NeedPressureModel, Reason::MissingBody);
    }
    auto& c = result.certificate;
    c.problem = p;
    c.channel_ticks = unsigned((self.bag + 9) / 10);
    c.owner_end_bound = c.channel_ticks + 1; // Includes one failed-start wait.
    c.enemy_inputs = 2 * c.owner_end_bound;
    if (c.enemy_inputs > max_enemy_inputs || authority.round > std::numeric_limits<int>::max() - int(c.owner_end_bound))
        return reject(CompileStatus::Unsupported, Reason::NumericDomain);
    c.deadline_round = authority.round + int(c.owner_end_bound);
    if (c.deadline_round > p.rules.maximum_rounds) return reject(CompileStatus::Unsupported, Reason::Deadline);
    c.latest_start_round = authority.round + 1;
    c.initial_health_energy = std::uint64_t(std::floor(double(self.hp)));
    c.count = roster.observed_count;
    for (std::size_t i = 0; i < c.count; ++i) {
        const auto& enemy = roster.bodies[i];
        auto& bound = c.enemies[i];
        bound.id = enemy.id; bound.inputs = c.enemy_inputs; bound.mobility = mobility(enemy.kit, c.enemy_inputs);
        if (!same(enemy.authority, authority) || !enemy.life_known)
            return reject(CompileStatus::NeedObservation, Reason::StaleAuthority);
        if (!supported(enemy.kit)) return reject(CompileStatus::NeedPressureModel, Reason::UnsupportedKit);
        int spawn_damage_margin = std::numeric_limits<int>::max(), spawn_lock_margin = spawn_damage_margin;
        for (std::size_t j = 0; j < spawn.count; ++j) {
            spawn_damage_margin = std::min(spawn_damage_margin, distance(spawn.cells[j], self.position) - int(bound.mobility) - p.rules.maximum_attack_reach);
            spawn_lock_margin = std::min(spawn_lock_margin, distance(spawn.cells[j], p.altar.position) - int(bound.mobility) - p.rules.lock_range);
        }
        if (!enemy.alive) {
            if (spawn_damage_margin <= 0 || spawn_lock_margin <= 0)
                return reject(CompileStatus::NeedPressureModel, Reason::DeadMayReach);
            bound.coverage = Coverage::DeadSpawnExcluded;
            bound.damage_reach_margin = spawn_damage_margin; bound.lock_reach_margin = spawn_lock_margin;
            continue;
        }
        if (!enemy.personally_visible || !enemy.position_known)
            return reject(CompileStatus::NeedPressureModel, Reason::HiddenAlive);
        if (!point_valid(enemy.position, spawn) || !enemy.stats_known || !std::isfinite(enemy.strength)
            || !std::isfinite(enemy.focus) || enemy.strength < 0 || enemy.focus < 0)
            return reject(CompileStatus::NeedPressureModel, Reason::MissingStats);
        if (!enemy.cooldowns_known || enemy.cooldowns[0] < 0 || enemy.cooldowns[0] > 8
            || enemy.cooldowns[1] < 0 || enemy.cooldowns[1] > 8)
            return reject(CompileStatus::NeedPressureModel, Reason::MissingCooldown);
        if (!enemy.buffs_known || enemy.buff_count) return reject(CompileStatus::NeedPressureModel, Reason::UnsupportedBuff);
        if (enemy.inventory != InventoryModel::StatsLockCrystalOnly || !enemy.held_lock_known || !enemy.crystals_known)
            return reject(CompileStatus::NeedPressureModel, Reason::InventoryUnknown);
        if (enemy.enhanced_damage_source) return reject(CompileStatus::NeedPressureModel, Reason::UnsupportedDamage);
        if (enemy.channel == Channel::Unknown || enemy.channel == Channel::Other || !enemy.at_spawn_known)
            return reject(CompileStatus::NeedPressureModel, Reason::ChannelUnknown);
        bound.lock_reach_margin = distance(enemy.position, p.altar.position) - int(bound.mobility) - p.rules.lock_range;
        if (enemy.held_lock && bound.lock_reach_margin <= 0)
            return reject(CompileStatus::NeedPressureModel, Reason::LockRisk);
        auto& shop = bound.shopping;
        shop.authority = authority; shop.roster_serial = roster.serial; shop.geometry_serial = spawn.geometry_serial;
        shop.enemy = enemy.id; shop.future_inputs = c.enemy_inputs;
        shop.recall_spent_inputs = enemy.at_spawn || enemy.channel == Channel::Recall ? 0U : 3U;
        const unsigned recall_left = c.enemy_inputs > shop.recall_spent_inputs ? c.enemy_inputs - shop.recall_spent_inputs : 0U;
        for (std::size_t j = 0; j < spawn.count; ++j) {
            const auto cell = spawn.cells[j];
            if (recall_left) {
                const int reach = int(mobility(enemy.kit, recall_left));
                shop.minimum_recall_damage_margin = std::min(shop.minimum_recall_damage_margin, distance(cell, self.position) - reach - p.rules.maximum_attack_reach);
                shop.minimum_recall_lock_margin = std::min(shop.minimum_recall_lock_margin, distance(cell, p.altar.position) - reach - p.rules.lock_range);
            }
            for (unsigned k = 0; k <= c.enemy_inputs; ++k) {
                if (distance(enemy.position, cell) > int(mobility(enemy.kit, k))) continue;
                // Reuse the arrival input after shopping: an intentional extra
                // opportunity avoids losing a remaining move/cast in that input.
                const unsigned spent = k ? k - 1 : 0;
                const unsigned left = c.enemy_inputs - spent;
                const int reach = int(mobility(enemy.kit, left));
                ++shop.walk_branches;
                shop.minimum_walk_damage_margin = std::min(shop.minimum_walk_damage_margin, distance(cell, self.position) - reach - p.rules.maximum_attack_reach);
                shop.minimum_walk_lock_margin = std::min(shop.minimum_walk_lock_margin, distance(cell, p.altar.position) - reach - p.rules.lock_range);
            }
        }
        if (shop.minimum_recall_damage_margin <= 0 || shop.minimum_recall_lock_margin <= 0
            || shop.minimum_walk_damage_margin <= 0 || shop.minimum_walk_lock_margin <= 0)
            return reject(CompileStatus::NeedPressureModel, Reason::ShoppingReach);
        bound.damage_reach_margin = distance(enemy.position, self.position) - int(bound.mobility) - p.rules.maximum_attack_reach;
        if (bound.damage_reach_margin > 0) { bound.coverage = Coverage::CurrentReachExcluded; continue; }
        bound.basic_upper = enemy.kit == Kit::Heart ? multiply_up(enemy.strength, 1.5f) : enemy.strength;
        if (enemy.kit == Kit::Heart) {
            bound.slot0_upper = 0;
            bound.slot1_upper = multiply_up(multiply_up(enemy.focus, 1.5f), 1.5f);
        } else if (enemy.kit == Kit::Ryth) {
            bound.slot0_upper = enemy.focus; bound.slot1_upper = enemy.strength;
        } else {
            bound.slot0_upper = enemy.strength; bound.slot1_upper = multiply_up(enemy.focus, 2.0f);
        }
        std::uint64_t basic{}, slot0{}, slot1{};
        if (!energy(bound.basic_upper, basic) || !energy(bound.slot0_upper, slot0) || !energy(bound.slot1_upper, slot1))
            return reject(CompileStatus::NeedPressureModel, Reason::NumericDomain);
        // N<=8 and supported kits have no cooldown refresh. Each slot is charged
        // once without initial-CD credit; N>=4 also covers the one-cast/input cap.
        bound.damage_energy = c.enemy_inputs * basic + slot0 + slot1;
        if (bound.damage_energy > max_energy || c.damage_energy > max_energy - bound.damage_energy)
            return reject(CompileStatus::NeedPressureModel, Reason::NumericDomain);
        c.damage_energy += bound.damage_energy;
    }
    if (c.initial_health_energy <= c.damage_energy)
        return reject(CompileStatus::Unsafe, Reason::InsufficientHealth);
    c.surviving_health_energy = c.initial_health_energy - c.damage_energy;
    // Sufficient f32 prefix argument: retain integer E<=floor(actualHP), charge
    // ceil(upward-rounded primitive damage) as y, and require E-y>=1. Since
    // E-y is an exactly representable integer <=2^20, monotonic f32 subtraction
    // preserves actualHP'>=E-y. Sum all nonnegative charges before granting any
    // survival; own shields/mitigation/healing are never credited. Continued
    // play through the deadline is conditional, not an opponent-win bound.
    result.status = CompileStatus::Certified; result.reason = Reason::None; return result;
}

enum class Phase { Idle, Armed, StartSubmitted, ChannelHold, ActualBanked, Interrupted };
enum class Command { None, Submit, Hold };
enum class Status { NoCommand, NeedObservation, Issued, Holding, ActualBanked, Interrupted, Rejected };
enum class Acceptance { Accepted, Refused, Invalidated };
enum class BankSource { None, OwnActorReceipt, TeamCounterOnly };
struct OwnBankProof {
    // Producer provenance: an actor-attributed bank event, or a complete
    // validated OWN tribute-start -> fresh living same-altar channel/bag
    // completion history with every compatible nonbank bag loss excluded.
    // Neither timer0, bag0 alone nor an unqualified team-score delta establishes
    // this kind. The pure kernel checks the identity and quantities, not SDK
    // history that the producer has failed to observe.
    Authority authority{};
    BankSource source{BankSource::None};
    bool present{}, verified{};
    int owner{-1}, altar{-1}, gems{}, before_bag{}, after_bag{};
    std::uint64_t episode{}, carried_receipt_serial{}, attempt_serial{}, serial{};
};
struct Observation {
    Subject subject{};
    Altar altar{};
    std::uint64_t roster_serial{}, geometry_serial{};
    bool envelope_revoked{};
    OwnBankProof own_bank{};
};
struct Residual {
    bool observed{};
    Authority authority{};
    Position position{};
    float hp{};
    int bag{}, gold{};
    std::uint64_t inventory_fingerprint{};
    Channel channel{Channel::Unknown};
};
struct Decision {
    Status status{Status::NoCommand};
    Command command{Command::None};
    Reason reason{Reason::None};
    Phase phase{Phase::Idle};
    Authority authority{};
    std::uint64_t attempt_serial{};
    Residual residual{};
};

class Kernel {
    Certificate certificate_{};
    Phase phase_{Phase::Idle};
    Reason terminal_reason_{Reason::None};
    Authority issued_{}, last_{}, consumed_{};
    bool pending_{}, start_seen_{}, consumed_valid_{};
    unsigned refusals_{};
    int last_channel_remaining_{-1};
    std::uint64_t last_episode_{}, last_receipt_{};

    Decision answer(const Observation& o, Status status, Command command = Command::None, Reason reason = Reason::None) const;
    Decision interrupt(const Observation& o, Reason reason);
public:
    Compilation begin(const Problem& problem);
    Decision step(const Observation& observation);
    Status complete(Authority issued, Acceptance acceptance);
    Phase phase() const { return phase_; }
    const Certificate& certificate() const { return certificate_; }
};

inline Decision Kernel::answer(const Observation& o, Status status, Command command, Reason reason) const {
    const auto& self = o.subject;
    return {status, command, reason, phase_, self.authority, certificate_.problem.attempt_serial,
        {self.observed, self.authority, self.position, self.hp, self.bag, self.gold,
            self.inventory_fingerprint, self.channel}};
}
inline Decision Kernel::interrupt(const Observation& o, Reason reason) {
    phase_ = Phase::Interrupted; terminal_reason_ = reason; pending_ = false;
    return answer(o, Status::Interrupted, Command::None, reason);
}
inline Compilation Kernel::begin(const Problem& p) {
    Compilation rejected;
    rejected.status = CompileStatus::Unsupported;
    if (phase_ != Phase::Idle && phase_ != Phase::Interrupted && phase_ != Phase::ActualBanked) {
        rejected.reason = Reason::AwaitResult; return rejected;
    }
    if (p.episode <= last_episode_) { rejected.reason = Reason::EpisodeReplay; return rejected; }
    if (p.receipt.serial <= last_receipt_) { rejected.reason = Reason::ReceiptReplay; return rejected; }
    if (service_micro::valid(last_) && (p.subject.authority.round <= last_.round
        || p.subject.authority.input_generation <= last_.input_generation)) {
        rejected.reason = Reason::StaleAuthority; return rejected;
    }
    auto result = compile(p);
    if (result.status != CompileStatus::Certified) return result;
    certificate_ = result.certificate;
    last_episode_ = p.episode; last_receipt_ = p.receipt.serial;
    phase_ = Phase::Armed; terminal_reason_ = Reason::None;
    pending_ = start_seen_ = consumed_valid_ = false;
    refusals_ = 0; last_channel_remaining_ = -1;
    issued_ = last_ = consumed_ = {};
    return result;
}
inline Status Kernel::complete(Authority issued, Acceptance acceptance) {
    if (!pending_ || issued != issued_ || phase_ != Phase::StartSubmitted) return Status::Rejected;
    pending_ = false;
    if (acceptance == Acceptance::Invalidated) {
        phase_ = Phase::Interrupted; terminal_reason_ = Reason::DispatchInvalidated;
        return Status::Interrupted;
    }
    if (acceptance == Acceptance::Refused) {
        if (++refusals_ > certificate_.failed_start_reserve) {
            phase_ = Phase::Interrupted; terminal_reason_ = Reason::RefusalLimit;
            return Status::Interrupted;
        }
        phase_ = Phase::Armed; return Status::NeedObservation;
    }
    // Acceptance alone does not prove the physical channel started.
    return Status::NeedObservation;
}
inline Decision Kernel::step(const Observation& o) {
    if (phase_ == Phase::Idle) return answer(o, Status::Rejected, Command::None, Reason::InvalidIdentity);
    const auto& self = o.subject;
    const auto a = self.authority;
    const auto& p = certificate_.problem;
    if (!self.observed || !service_micro::valid(a))
        return answer(o, Status::NeedObservation, Command::None, Reason::InvalidActor);
    if (a.owner != p.subject.authority.owner)
        return answer(o, Status::NeedObservation, Command::None, Reason::WrongOwner);
    if (a.round < p.subject.authority.round || a.input_generation < p.subject.authority.input_generation
        || (service_micro::same_input(a, p.subject.authority) && a.view_revision < p.subject.authority.view_revision))
        return answer(o, Status::NeedObservation, Command::None, Reason::StaleAuthority);
    if (service_micro::valid(last_)) {
        if (a.round < last_.round || a.input_generation < last_.input_generation
            || (service_micro::same_input(a, last_) && a.view_revision < last_.view_revision)
            || (!service_micro::same_input(a, last_) && a.round <= last_.round))
            return answer(o, Status::NeedObservation, Command::None, Reason::StaleAuthority);
    }
    last_ = a;
    if (phase_ == Phase::Interrupted) return answer(o, Status::Interrupted, Command::None, terminal_reason_);
    if (phase_ == Phase::ActualBanked) return answer(o, Status::ActualBanked, Command::None, Reason::ActualBanked);
    if (!self.alive) return interrupt(o, Reason::ActorDead);
    if (!std::isfinite(self.hp) || self.hp <= 0 || self.hp < float(certificate_.surviving_health_energy))
        return interrupt(o, Reason::EnvelopeChanged);
    if (a.round > certificate_.deadline_round) return interrupt(o, Reason::Deadline);
    if (o.roster_serial != p.roster.serial || o.geometry_serial != p.enemy_spawn.geometry_serial
        || o.envelope_revoked || self.position != p.subject.position)
        return interrupt(o, Reason::EnvelopeChanged);
    if (self.kit != Kit::Marcella || !self.passive_known || !self.stun_immune || self.stunned)
        return interrupt(o, Reason::WrongKit);
    if (!o.altar.observed || o.altar.authority != a || o.altar.id != p.altar.id || o.altar.position != p.altar.position)
        return answer(o, Status::NeedObservation, Command::None, Reason::AltarUnknown);
    if (o.altar.lock != Lock::Unlocked) return interrupt(o, Reason::AltarChanged);
    if (self.channel == Channel::Unknown)
        return answer(o, Status::NeedObservation, Command::None, Reason::ChannelUnknown);
    const auto& bank = o.own_bank;
    if (bank.present) {
        if (!start_seen_) return interrupt(o, Reason::StartNotObserved);
        if (!bank.verified || bank.source != BankSource::OwnActorReceipt || bank.authority != a
            || bank.owner != a.owner || bank.altar != p.altar.id || bank.episode != p.episode
            || bank.carried_receipt_serial != p.receipt.serial || bank.attempt_serial != p.attempt_serial
            || bank.serial <= p.initial_own_bank_serial || bank.gems != p.subject.bag
            || bank.before_bag != p.subject.bag || bank.after_bag != 0 || self.bag != 0
            || self.channel != Channel::None || a.round <= issued_.round
            || service_micro::same_input(a, issued_))
            return interrupt(o, Reason::BankProofMismatch);
        phase_ = Phase::ActualBanked;
        return answer(o, Status::ActualBanked, Command::None, Reason::ActualBanked);
    }
    if (self.bag != p.subject.bag)
        return interrupt(o, self.bag == 0 ? Reason::BankProofMissing : Reason::BagChanged);
    // Completion may be read at the deadline's owner START. Without its actual
    // proof, another owner END would exceed the certified n+1 charged phases.
    if (a.round == certificate_.deadline_round) return interrupt(o, Reason::Deadline);
    if (self.inventory_fingerprint != p.subject.inventory_fingerprint)
        return interrupt(o, Reason::EnvelopeChanged);
    if (pending_) return answer(o, Status::NeedObservation, Command::None, Reason::AwaitResult);
    if (phase_ == Phase::StartSubmitted) {
        if (self.channel != Channel::Tribute) {
            // Missing same-input effect may still be read again in this input;
            // a later owner input without actual start revokes the word.
            if (service_micro::same_input(a, issued_))
                return answer(o, Status::NeedObservation, Command::None, Reason::AwaitEffect);
            return interrupt(o, Reason::StartNotObserved);
        }
        if (a == issued_ || !self.channel_time_known || self.channel_remaining < 0
            || self.channel_remaining > int(certificate_.channel_ticks))
            return answer(o, Status::NeedObservation, Command::None, Reason::AwaitEffect);
        start_seen_ = true; phase_ = Phase::ChannelHold;
        last_channel_remaining_ = self.channel_remaining;
    }
    if (phase_ == Phase::ChannelHold) {
        if (self.channel != Channel::Tribute) return interrupt(o, Reason::ChannelInterrupted);
        if (!self.channel_time_known || self.channel_remaining < 0)
            return answer(o, Status::NeedObservation, Command::None, Reason::ChannelUnknown);
        if (self.channel_remaining > last_channel_remaining_)
            return interrupt(o, Reason::ChannelChanged);
        last_channel_remaining_ = self.channel_remaining;
        if (consumed_valid_ && service_micro::same_input(a, consumed_))
            return answer(o, Status::NoCommand, Command::None, Reason::ConsumedInput);
        consumed_ = a; consumed_valid_ = true;
        return answer(o, Status::Holding, Command::Hold);
    }
    if (phase_ != Phase::Armed) return interrupt(o, Reason::ChannelChanged);
    if (self.channel != Channel::None) return interrupt(o, Reason::ChannelInterrupted);
    if (consumed_valid_ && service_micro::same_input(a, consumed_))
        return answer(o, Status::NoCommand, Command::None, Reason::ConsumedInput);
    const int wanted_start = p.subject.authority.round + int(refusals_);
    if (a.round != wanted_start || a.round > certificate_.latest_start_round)
        return interrupt(o, Reason::StartWindowMissed);
    if (!refusals_ && !service_micro::same_input(a, p.subject.authority))
        return interrupt(o, Reason::StaleAuthority);
    if (!self.submit_permitted) return interrupt(o, Reason::SubmitUnavailable);
    issued_ = consumed_ = a; consumed_valid_ = true; pending_ = true;
    phase_ = Phase::StartSubmitted;
    return answer(o, Status::Issued, Command::Submit);
}
} // namespace wb::service_pressure
