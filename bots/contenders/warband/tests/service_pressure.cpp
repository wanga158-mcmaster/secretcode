#include "../src/service_pressure.h"
#include <cstdio>
#include <limits>

using namespace wb::service_pressure;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (false)

Problem problem(int bag = 10) {
    Problem p;
    p.episode = 1; p.attempt_serial = 9; p.initial_own_bank_serial = 3;
    auto& self = p.subject;
    self.authority = {1, 50, 100, 1};
    self.observed = self.alive = self.passive_known = self.stun_immune = true;
    self.kit = Kit::Marcella; self.position = {2, 14}; self.hp = self.max_hp = 1200;
    self.bag = bag; self.gold = 123; self.inventory_fingerprint = 456;
    self.channel = Channel::None; self.submit_permitted = true;
    p.altar = {self.authority, true, 7, {2, 15}, Lock::Unlocked};
    p.receipt = {true, true, 1, 2, bag, p.episode, 17};
    p.rules.queried = true;
    p.roster.authority = self.authority; p.roster.serial = 44; p.roster.complete = true;
    p.roster.expected_count = p.roster.observed_count = 3;
    p.roster.expected_ids = {3, 4, 5};
    for (std::size_t i = 0; i < 3; ++i) {
        auto& e = p.roster.bodies[i];
        e.authority = self.authority; e.id = 3 + int(i);
        e.kit = i == 0 ? Kit::Heart : i == 1 ? Kit::Ryth : Kit::Marcella;
        e.life_known = true;
    }
    auto& e = p.roster.bodies[0];
    e.alive = e.personally_visible = e.position_known = e.stats_known = e.cooldowns_known = e.buffs_known = true;
    e.position = {5, 14}; e.strength = 60; e.focus = 100;
    e.inventory = InventoryModel::StatsLockCrystalOnly;
    e.held_lock_known = e.crystals_known = e.at_spawn_known = true;
    e.channel = Channel::None;
    // Packs-sized geometry, exact opposite 3x3 spawn set. One live supported
    // foe threatens the held altar; the two known dead foes are NOT dropped.
    p.enemy_spawn.geometry_serial = 55; p.enemy_spawn.complete = true;
    p.enemy_spawn.width = p.enemy_spawn.height = 31;
    for (int y = 28; y < 31; ++y) for (int x = 28; x < 31; ++x)
        p.enemy_spawn.cells[p.enemy_spawn.count++] = {x, y};
    p.external = {self.authority, 55, 44, self.position, true, 12, 0, true, true};
    return p;
}
Observation observation(const Problem& p) {
    Observation o;
    o.subject = p.subject; o.altar = p.altar;
    o.roster_serial = p.roster.serial; o.geometry_serial = p.enemy_spawn.geometry_serial;
    return o;
}
void post(Observation& o) { ++o.subject.authority.view_revision; o.altar.authority = o.subject.authority; }
void next(Observation& o) {
    ++o.subject.authority.round; ++o.subject.authority.input_generation;
    o.subject.authority.view_revision = 1; o.altar.authority = o.subject.authority;
}
void bank_proof(Observation& o, const Problem& p) {
    o.subject.bag = 0; o.subject.channel = Channel::None;
    o.own_bank = {o.subject.authority, BankSource::OwnActorReceipt, true, true, 1, 7,
        p.subject.bag, p.subject.bag, 0, p.episode, p.receipt.serial, p.attempt_serial, 4};
}
Decision start(Kernel& k, Observation& o, const Problem& p) {
    CHECK(k.begin(p).status == CompileStatus::Certified);
    auto issued = k.step(o);
    CHECK(issued.status == Status::Issued && issued.command == Command::Submit);
    CHECK(issued.phase == Phase::StartSubmitted);
    CHECK(k.complete(issued.authority, Acceptance::Accepted) == Status::NeedObservation);
    post(o); o.subject.channel = Channel::Tribute;
    o.subject.channel_time_known = true; o.subject.channel_remaining = (p.subject.bag + 9) / 10;
    const auto effect = k.step(o);
    CHECK(effect.phase == Phase::ChannelHold);
    CHECK(effect.status == Status::NoCommand && effect.reason == Reason::ConsumedInput);
    return issued;
}

void arithmetic_and_concrete_admission() {
    auto p = problem(); const auto c = compile(p);
    CHECK(c.status == CompileStatus::Certified);
    CHECK(c.certificate.channel_ticks == 1 && c.certificate.enemy_inputs == 4);
    CHECK(c.certificate.owner_end_bound == 2 && c.certificate.deadline_round == 52);
    CHECK(c.certificate.damage_energy == 585 && c.certificate.surviving_health_energy == 615);
    CHECK(c.certificate.enemies[0].basic_upper == 90);
    CHECK(c.certificate.enemies[0].slot0_upper == 0 && c.certificate.enemies[0].slot1_upper == 225);
    CHECK(c.certificate.enemies[1].coverage == Coverage::DeadSpawnExcluded);
    CHECK(c.certificate.enemies[2].coverage == Coverage::DeadSpawnExcluded);
    CHECK(c.certificate.enemies[0].shopping.minimum_recall_damage_margin > 0);
    CHECK(c.certificate.enemies[0].shopping.geometry_serial == p.enemy_spawn.geometry_serial);
    CHECK(c.certificate.enemies[0].shopping.roster_serial == p.roster.serial);
    CHECK(!p.subject.move && !p.subject.attack && !p.subject.cast && !p.subject.own_cooldowns_known);
    CHECK(compile(p).status == CompileStatus::Certified); // Submit is not MAC/CD-gated.
    for (int bag : {1, 10, 11, 20, 21, 30}) {
        p = problem(bag); const auto result = compile(p);
        CHECK(result.status == CompileStatus::Certified);
        CHECK(result.certificate.channel_ticks == unsigned((bag + 9) / 10));
        CHECK(result.certificate.enemy_inputs == 2U * (unsigned((bag + 9) / 10) + 1));
        CHECK(result.certificate.damage_energy == result.certificate.enemy_inputs * 90U + 225U);
        CHECK(result.certificate.enemy_inputs <= 8);
    }
    p = problem(); p.subject.hp = 585;
    CHECK(compile(p).status == CompileStatus::Unsafe);
    p.subject.hp = 585.99f;
    CHECK(compile(p).status == CompileStatus::Unsafe); // Conservative integer energy.
    p.subject.hp = 586;
    CHECK(compile(p).status == CompileStatus::Certified);
    p = problem(); p.roster.bodies[0].cooldowns = {8, 8};
    CHECK(compile(p).certificate.damage_energy == c.certificate.damage_energy); // No CD credit.
    CHECK(c.certificate.damage_energy > 4U * 60U + 150U); // Future Heart chip raises damage.
    p.subject.hp = 500;
    CHECK(compile(p).status == CompileStatus::Unsafe); // Full-currentHP multiplier would be unsafe.
    std::uint64_t value = 0;
    CHECK(energy(multiply_up(0.1f, 1.5f), value) && value == 1);
    CHECK(!energy(std::numeric_limits<float>::infinity(), value));
    CHECK(!energy(-1, value));
    CHECK(!energy(float(max_energy) * 2, value));
}
void supported_kit_bounds() {
    auto p = problem();
    p.roster.bodies[0].kit = Kit::Ryth; p.roster.bodies[0].strength = 45; p.roster.bodies[0].focus = 100;
    auto result = compile(p);
    CHECK(result.status == CompileStatus::Certified && result.certificate.damage_energy == 325);
    CHECK(result.certificate.enemies[0].slot0_upper == 100 && result.certificate.enemies[0].slot1_upper == 45);
    p.roster.bodies[0].kit = Kit::Marcella; p.roster.bodies[0].focus = 60;
    result = compile(p);
    CHECK(result.status == CompileStatus::Certified && result.certificate.damage_energy == 345);
    CHECK(result.certificate.enemies[0].slot0_upper == 45 && result.certificate.enemies[0].slot1_upper == 120);
    p = problem();
    for (std::size_t i = 1; i < 3; ++i) {
        auto e = p.roster.bodies[0]; e.id = 3 + int(i); e.kit = i == 1 ? Kit::Ryth : Kit::Marcella;
        e.strength = 45; e.focus = i == 1 ? 100 : 60; p.roster.bodies[i] = e;
    }
    CHECK(compile(p).status == CompileStatus::Unsafe); // Whole roster, not nearest foe.
    p.subject.hp = 1300;
    CHECK(compile(p).status == CompileStatus::Certified);
    CHECK(compile(p).certificate.damage_energy == 1255);
}
void unknown_is_never_absence() {
    auto p = problem(); p.roster.bodies[0].personally_visible = false;
    CHECK(compile(p).reason == Reason::HiddenAlive);
    p = problem(); p.roster.bodies[0].stats_known = false; CHECK(compile(p).reason == Reason::MissingStats);
    p = problem(); p.roster.bodies[0].cooldowns_known = false; CHECK(compile(p).reason == Reason::MissingCooldown);
    p = problem(); p.roster.bodies[0].cooldowns[1] = -1; CHECK(compile(p).reason == Reason::MissingCooldown);
    p = problem(); p.roster.bodies[0].kit = Kit::Unknown; CHECK(compile(p).reason == Reason::UnsupportedKit);
    p = problem(); p.roster.bodies[0].buff_count = 1; CHECK(compile(p).reason == Reason::UnsupportedBuff);
    p = problem(); p.roster.bodies[0].buffs_known = false; CHECK(compile(p).reason == Reason::UnsupportedBuff);
    p = problem(); p.roster.bodies[0].enhanced_damage_source = true; CHECK(compile(p).reason == Reason::UnsupportedDamage);
    p = problem(); p.roster.bodies[0].inventory = InventoryModel::Unknown; CHECK(compile(p).reason == Reason::InventoryUnknown);
    p = problem(); p.roster.bodies[0].crystals_known = false; CHECK(compile(p).reason == Reason::InventoryUnknown);
    p = problem(); p.roster.bodies[0].held_lock_known = false; CHECK(compile(p).reason == Reason::InventoryUnknown);
    p = problem(); p.roster.bodies[0].channel = Channel::Unknown; CHECK(compile(p).reason == Reason::ChannelUnknown);
    p = problem(); --p.roster.bodies[0].authority.view_revision; CHECK(compile(p).reason == Reason::StaleAuthority);
    p = problem(); p.roster.complete = false; CHECK(compile(p).reason == Reason::RosterUnknown);
    p = problem(); p.roster.expected_count = p.roster.observed_count = 0; CHECK(compile(p).reason == Reason::RosterUnknown);
    p = problem(); p.roster.expected_count = p.roster.observed_count = 17; CHECK(compile(p).reason == Reason::RosterCapacity);
    p = problem(); p.roster.observed_count = 2; CHECK(compile(p).reason == Reason::MissingBody);
    p = problem(); p.roster.expected_ids[2] = p.roster.expected_ids[1]; CHECK(compile(p).reason == Reason::DuplicateBody);
    p = problem(); p.roster.bodies[2].id = p.roster.bodies[1].id; CHECK(compile(p).status == CompileStatus::NeedPressureModel);
    p = problem(); p.roster.expected_count = p.roster.observed_count = 4;
    p.roster.expected_ids[3] = 6; p.roster.bodies[3] = p.roster.bodies[0];
    p.roster.bodies[3].id = 6; p.roster.bodies[3].kit = Kit::Unknown; // Extra C&I body.
    CHECK(compile(p).reason == Reason::UnsupportedKit);
    p = problem(); p.enemy_spawn.complete = false; CHECK(compile(p).reason == Reason::SpawnUnknown);
    p = problem(); p.enemy_spawn.cells[1] = p.enemy_spawn.cells[0]; CHECK(compile(p).reason == Reason::SpawnUnknown);
    p = problem(); p.external.static_sources_complete = false; CHECK(compile(p).reason == Reason::StaticFieldUnknown);
    p = problem(); p.external.potentially_harmful_overlaps = 1; CHECK(compile(p).reason == Reason::ExternalInterference);
    p = problem(); p.external.no_ally_interference = false; CHECK(compile(p).reason == Reason::ExternalInterference);
    p = problem(); p.external.roster_serial = 43; CHECK(compile(p).reason == Reason::StaticFieldUnknown);
    p = problem(); p.external.hold_position = {3, 14}; CHECK(compile(p).reason == Reason::StaticFieldUnknown);
    p = problem(); p.roster.bodies[1].life_known = false; CHECK(compile(p).reason == Reason::StaleAuthority);
    p = problem(); p.roster.bodies[0].strength = std::numeric_limits<float>::quiet_NaN(); CHECK(compile(p).reason == Reason::MissingStats);
    p = problem(); p.roster.bodies[0].focus = std::numeric_limits<float>::max(); CHECK(compile(p).reason == Reason::NumericDomain);
}
void geometry_locks_and_shop() {
    auto p = problem(); p.roster.bodies[0].held_lock = true;
    CHECK(compile(p).reason == Reason::LockRisk);
    p = problem(); p.enemy_spawn.count = 1; p.enemy_spawn.cells[0] = {10, 10};
    CHECK(compile(p).reason == Reason::ShoppingReach); // Earliest recall then buy.
    p = problem(); p.enemy_spawn.count = 1; p.enemy_spawn.cells[0] = {17, 20};
    p.roster.bodies[0].position = {16, 20}; // Walk to shop sooner than recall.
    CHECK(compile(p).reason == Reason::ShoppingReach);
    p = problem(); p.roster.bodies[0].channel = Channel::Recall;
    CHECK(compile(p).status == CompileStatus::Certified); // ANY spawn still too far.
    p = problem(); p.enemy_spawn.count = 1; p.enemy_spawn.cells[0] = {15, 15};
    p.roster.bodies[0].channel = Channel::Recall;
    CHECK(compile(p).reason == Reason::ShoppingReach);
    p = problem(); p.roster.bodies[0].alive = false;
    p.roster.bodies[0].position = {-1, -1}; p.roster.bodies[0].stats_known = false;
    CHECK(compile(p).status == CompileStatus::Certified); // Fresh life + actual spawn set.
    p.enemy_spawn.count = 1; p.enemy_spawn.cells[0] = {4, 14};
    CHECK(compile(p).reason == Reason::DeadMayReach); // No private respawn-timer credit.
    p = problem(30); p.roster.bodies[0].position = {24, 14}; p.subject.hp = 900;
    CHECK(compile(p).status == CompileStatus::Unsafe); // Crystal reach is priced even if count0.
    p = problem(); p.altar.lock = Lock::Hostile; CHECK(compile(p).reason == Reason::AltarChanged);
    p = problem(); p.rules.queried = false; CHECK(compile(p).reason == Reason::SettingsUnknown);
    p = problem(); p.rules.submit_multiplier = 0.2; CHECK(compile(p).reason == Reason::UnsupportedSettings);
    p = problem(); p.rules.maximum_rounds = 51; CHECK(compile(p).reason == Reason::Deadline);
    p = problem(); p.subject.bag = 0; CHECK(compile(p).reason == Reason::BagRange);
    p = problem(31); CHECK(compile(p).reason == Reason::BagRange);
    p = problem(); p.receipt.verified = false; CHECK(compile(p).reason == Reason::ReceiptMissing);
    p = problem(); p.receipt.group = -1; CHECK(compile(p).reason == Reason::ReceiptMissing);
    p = problem(); p.receipt.episode = 2; CHECK(compile(p).reason == Reason::ReceiptMissing);
    p = problem(); p.altar.position = {10, 10}; CHECK(compile(p).reason == Reason::AltarRange);
}
void exact_own_bank() {
    auto p = problem(); auto o = observation(p); Kernel k;
    const auto issued = start(k, o, p);
    CHECK(k.complete(issued.authority, Acceptance::Accepted) == Status::Rejected);
    next(o); bank_proof(o, p); o.subject.gold = 143;
    auto done = k.step(o);
    CHECK(done.status == Status::ActualBanked && done.phase == Phase::ActualBanked);
    CHECK(done.command == Command::None);
    CHECK(done.residual.bag == 0 && done.residual.gold == 143);
    CHECK(done.residual.inventory_fingerprint == 456 && done.residual.position == p.subject.position);
    CHECK(k.step(o).status == Status::ActualBanked);
    auto foreign = o; foreign.subject.authority.owner = 99;
    CHECK(k.step(foreign).reason == Reason::WrongOwner);
    auto stale = observation(p);
    CHECK(k.step(stale).reason == Reason::StaleAuthority);
    CHECK(k.begin(p).reason == Reason::EpisodeReplay);
    p.episode = p.receipt.episode = 2;
    CHECK(k.begin(p).reason == Reason::ReceiptReplay);
    ++p.receipt.serial;
    CHECK(k.begin(p).reason == Reason::StaleAuthority); // New serial is not same-epoch reset.
    p.subject.authority.round = 53; p.subject.authority.input_generation = 103;
    p.altar.authority = p.roster.authority = p.external.authority = p.subject.authority;
    for (auto& e : p.roster.bodies) e.authority = p.subject.authority;
    CHECK(k.begin(p).status == CompileStatus::Certified);
}
void refusal_and_finite_deadline() {
    auto p = problem(30); auto o = observation(p); Kernel k;
    CHECK(k.begin(p).status == CompileStatus::Certified);
    auto d = k.step(o);
    CHECK(d.command == Command::Submit);
    auto wrong = d.authority; ++wrong.view_revision;
    CHECK(k.complete(wrong, Acceptance::Accepted) == Status::Rejected);
    CHECK(k.step(o).reason == Reason::AwaitResult);
    CHECK(k.complete(d.authority, Acceptance::Refused) == Status::NeedObservation);
    post(o); CHECK(k.step(o).reason == Reason::ConsumedInput);
    const int deadline = k.certificate().deadline_round;
    auto newer = p; ++newer.episode; ++newer.receipt.serial; newer.receipt.episode = newer.episode;
    CHECK(k.begin(newer).reason == Reason::AwaitResult);
    CHECK(k.certificate().deadline_round == deadline);
    next(o); d = k.step(o); CHECK(d.command == Command::Submit);
    CHECK(k.complete(d.authority, Acceptance::Accepted) == Status::NeedObservation);
    post(o); o.subject.channel = Channel::Tribute; o.subject.channel_time_known = true; o.subject.channel_remaining = 3;
    CHECK(k.step(o).phase == Phase::ChannelHold);
    next(o); o.subject.channel_remaining = 2; CHECK(k.step(o).command == Command::Hold);
    CHECK(k.step(o).reason == Reason::ConsumedInput);
    next(o); o.subject.channel_remaining = 1; CHECK(k.step(o).command == Command::Hold);
    next(o); bank_proof(o, p);
    CHECK(o.subject.authority.round == deadline);
    CHECK(k.step(o).status == Status::ActualBanked);
    Kernel twice; p = problem(); o = observation(p); CHECK(twice.begin(p).status == CompileStatus::Certified);
    d = twice.step(o); CHECK(twice.complete(d.authority, Acceptance::Refused) == Status::NeedObservation);
    next(o); d = twice.step(o); CHECK(twice.complete(d.authority, Acceptance::Refused) == Status::Interrupted);
    CHECK(twice.step(o).reason == Reason::RefusalLimit);
    Kernel timer; p = problem(); o = observation(p); start(timer, o, p);
    next(o); o.subject.channel_remaining = 0;
    CHECK(timer.step(o).command == Command::Hold); // Timer0 is NOT a receipt.
    next(o); CHECK(timer.step(o).reason == Reason::Deadline);
    CHECK(timer.phase() == Phase::Interrupted);
}
void all_literal_failures() {
    for (int defect = 0; defect < 12; ++defect) {
        auto p = problem(); auto o = observation(p); Kernel k; start(k, o, p);
        next(o); bank_proof(o, p);
        switch (defect) {
        case 0: o.own_bank.source = BankSource::TeamCounterOnly; break;
        case 1: o.own_bank.verified = false; break;
        case 2: o.own_bank.owner = 2; break;
        case 3: ++o.own_bank.episode; break;
        case 4: ++o.own_bank.carried_receipt_serial; break;
        case 5: ++o.own_bank.attempt_serial; break;
        case 6: o.own_bank.serial = 3; break;
        case 7: o.own_bank.gems = 9; break;
        case 8: o.own_bank.before_bag = 9; break;
        case 9: o.own_bank.after_bag = 1; break;
        case 10: ++o.own_bank.altar; break;
        case 11: --o.own_bank.authority.view_revision; break;
        }
        CHECK(k.step(o).reason == Reason::BankProofMismatch);
        CHECK(k.phase() == Phase::Interrupted);
    }
    for (int defect = 0; defect < 7; ++defect) {
        auto p = problem(); auto o = observation(p); Kernel k; start(k, o, p); next(o);
        o.subject.gold = 77; o.subject.hp = 800;
        switch (defect) {
        case 0: o.subject.alive = false; break;
        case 1: o.subject.bag = 8; break;
        case 2: o.subject.channel = Channel::None; break;
        case 3: o.altar.lock = Lock::Hostile; break;
        case 4: o.envelope_revoked = true; break;
        case 5: ++o.roster_serial; break;
        case 6: o.subject.inventory_fingerprint = 999; break;
        }
        const auto d = k.step(o);
        CHECK(d.status == Status::Interrupted && d.command == Command::None);
        CHECK(d.residual.gold == 77 && d.residual.hp == 800 && d.residual.bag == o.subject.bag);
        CHECK(d.residual.position == o.subject.position && d.residual.inventory_fingerprint == o.subject.inventory_fingerprint);
    }
    auto p = problem(); auto o = observation(p); Kernel k; start(k, o, p);
    post(o); bank_proof(o, p); CHECK(k.step(o).reason == Reason::BankProofMismatch); // Same-input illusion.
    Kernel unseen; o = observation(p); CHECK(unseen.begin(p).status == CompileStatus::Certified);
    const auto d = unseen.step(o); CHECK(unseen.complete(d.authority, Acceptance::Accepted) == Status::NeedObservation);
    next(o); bank_proof(o, p); CHECK(unseen.step(o).reason == Reason::StartNotObserved);
    Kernel missing; o = observation(p); start(missing, o, p); next(o); o.subject.bag = 0; o.subject.channel = Channel::None;
    CHECK(missing.step(o).reason == Reason::BankProofMissing);
    Kernel invalid; o = observation(p); CHECK(invalid.begin(p).status == CompileStatus::Certified);
    const auto call = invalid.step(o); CHECK(invalid.complete(call.authority, Acceptance::Invalidated) == Status::Interrupted);
    CHECK(invalid.step(o).reason == Reason::DispatchInvalidated);
    auto foreign = o; ++foreign.subject.authority.owner;
    CHECK(invalid.step(foreign).reason == Reason::WrongOwner);
    Kernel wrong_input; o = observation(p); CHECK(wrong_input.begin(p).status == CompileStatus::Certified);
    ++o.subject.authority.input_generation; o.altar.authority = o.subject.authority;
    CHECK(wrong_input.step(o).reason == Reason::StaleAuthority);
    Kernel old_view; p = problem(); p.subject.authority.view_revision = 3;
    p.altar.authority = p.roster.authority = p.external.authority = p.subject.authority;
    for (auto& e : p.roster.bodies) e.authority = p.subject.authority;
    o = observation(p); CHECK(old_view.begin(p).status == CompileStatus::Certified);
    --o.subject.authority.view_revision; o.altar.authority = o.subject.authority;
    CHECK(old_view.step(o).reason == Reason::StaleAuthority);
}
} // namespace

int main() {
    arithmetic_and_concrete_admission(); supported_kit_bounds(); unknown_is_never_absence();
    geometry_locks_and_shop(); exact_own_bank(); refusal_and_finite_deadline(); all_literal_failures();
    std::printf("service_pressure: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
