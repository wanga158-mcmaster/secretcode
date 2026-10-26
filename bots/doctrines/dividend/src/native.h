#pragma once
#include "policy.h"
#include "basic-quotes.h"
#include "../../reference/src/runtime.h"

namespace p5::dividend::native_binding {
namespace n = p5::native;
namespace e = p5::execution;

// Each champion has its own WASM instance. Constant-initialize plain economic
// payloads in that private memory instead of constructing their unused array
// tails inside the first fuel-limited owner callback.
struct EconomicScratch {
    Project project{};
    n::Snapshot snapshot{};
    Observation planning{};
    Decision selection{};
    basic_quotes::CostTable costs{};
    CombatInput quote_input{};
    std::array<RouteLeg, 8> altar_routes{};
    std::array<CombatActor, 4> variants{};
    std::array<int, 4> variant_profiles{};
    n::CardBuildTrace build_trace{};
};
inline constinit EconomicScratch economic_scratch{};

// One deterministic altar is frozen before either arm compares production.
// All fields below come from this champion's fresh native snapshot.
class Bot {
    n::Runtime runtime_{};
    Project& project_ = economic_scratch.project;
    bool running_ = false;
    int altar_ = -1, renewal_ = -1;
    int project_renewal_entry_ = -1;
    bool card_life_started_ = false, card_life_active_ = false;
    int card_life_actor_ = -1, card_life_round_ = -1;
    Profile profile_{};
    // Each champion has private module memory. Keep large planning payloads
    // here rather than nesting them on the native64KiB linear stack.
    n::Snapshot& snapshot_ = economic_scratch.snapshot;
    Observation& planning_ = economic_scratch.planning;
    Decision& selection_ = economic_scratch.selection;
    basic_quotes::CostTable& costs_ = economic_scratch.costs;
    n::CardBuildTrace& build_trace_ = economic_scratch.build_trace;
    bool tracing_builder_ = false;
    // A miss is one <=8-monster primitive, never an unbounded nested search.
    // Learn its cost independently of the complete builder. Native validation
    // still has to establish the initial envelope and chooser budget.
    unsigned quote_peak_ = 6000;
    bool build_complete_ = true;
    CombatQuoteProvider* active_provider_ = nullptr;
    static constexpr unsigned action_reserve_ = 30000;
    static constexpr unsigned cached_certificate_reserve_ = 1500;

    // Query-free first rejection of this one synchronous builder. Ordinary
    // unsafe alternatives are not incomplete-menu failures and leave reason0.
    enum class BuildStage { input = 1, profiles, group_setup, prepare, candidate,
                            continuation, bank_return, final_check, complete, quote };
    enum class BuildReason { inherited = 1, round_or_fuel, invalid_profile,
                             miss_round_or_reserve, compute_round_or_fuel,
                             reset_or_group_count, invalid_baseline_profile,
                             invalid_purchase_profile, invalid_group, final_round };
    void trace_reject(BuildStage stage, BuildReason reason, int group_id = -1, int profile = -1) {
        if (!tracing_builder_ || build_trace_.reason != 0) return;
        build_trace_.stage = int(stage); build_trace_.reason = int(reason);
        build_trace_.group = group_id; build_trace_.profile = profile;
    }
    unsigned trace_quote_fuel(unsigned fuel, BuildStage stage, int group_id) {
        if (tracing_builder_) {
            if (stage == BuildStage::prepare && build_trace_.prepare_fuel < 0)
                build_trace_.prepare_fuel = int(fuel);
            if (stage == BuildStage::candidate && group_id == build_trace_.candidate_group &&
                build_trace_.candidate_quote_fuel < 0) {
                build_trace_.candidate_quote_fuel = int(fuel);
                if (group_id == build_trace_.first_candidate_group && build_trace_.first_candidate_quote_fuel < 0)
                    build_trace_.first_candidate_quote_fuel = int(fuel);
            }
        }
        return fuel;
    }
    void trace_candidate(int group_id) {
        if (!tracing_builder_) return;
        build_trace_.candidate_group = group_id;
        build_trace_.candidate_entry_fuel = build_trace_.candidate_quote_fuel = -1;
        if (build_trace_.first_candidate_group < 0) build_trace_.first_candidate_group = group_id;
    }
    unsigned trace_candidate_fuel(unsigned fuel) {
        if (tracing_builder_) {
            build_trace_.candidate_entry_fuel = int(fuel);
            if (build_trace_.candidate_group == build_trace_.first_candidate_group && build_trace_.first_candidate_entry_fuel < 0)
                build_trace_.first_candidate_entry_fuel = int(fuel);
        }
        return fuel;
    }
    bool finish_builder(bool complete) {
        if (complete) build_trace_.stage = int(BuildStage::complete);
        tracing_builder_ = false;
        return complete;
    }

    static int return_id(const Decision& d) {
        for (int i = 0; i < d.schedule.event_count; ++i)
            if (d.schedule.events[i].kind == AccountingEventKind::return_to_work)
                return d.schedule.events[i].waypoint;
        return -1;
    }
    static const n::GroupFacts* group(const n::Snapshot& s, int id) {
        for (int i = 0; i < s.group_count; ++i)
            if (s.groups[i].id == id) return &s.groups[i];
        return nullptr;
    }
    static const Purchase* purchase(const Observation& o, int family) {
        for (std::size_t i = 0; i < o.purchase_count; ++i)
            if (o.purchases[i].family == family) return &o.purchases[i];
        return nullptr;
    }
    static CombatActor conservative(CombatActor actor) {
        // No future healing, expiring shield or unbound offensive cast credit.
        actor.shield = 0; actor.shield_expiry_tick = 0;
        actor.spell_known_ready = actor.legal_first_cast = actor.reserved_cast = false;
        actor.direct_or_reset_damage = 0; actor.heal_legal_ready = false;
        actor.heal_amount = 0; actor.heal_recipient = -1;
        actor.visible_enemy_next_phase = 0;
        return actor;
    }
    RouteLeg leg(n::Node from, n::Node to, bool altar_stress = false) const {
        auto out = runtime_.route(from, to);
        // Unknown/hostile locks are costed as the full queried removal budget.
        if (out.distance >= 0 && altar_stress)
            out.distance += int(cambc::constants::altar_lock_hits);
        // Provisional model inputs only: certify() checks the complete selected
        // schedule and successor before these flags can authorize a project.
        out.safety_checked = out.turns() >= 0;
        return out;
    }
    bool usable(const n::GroupFacts& g) const {
        return g.known && !g.respawning && !g.other_holder && g.gems > 0 &&
            g.combat.actor_count == 1 && g.combat.monster_count > 0 &&
            g.combat.monster_count <= g.combat.monsters.size();
    }
    static bool purchased_target_unavailable(const n::GroupFacts* g) {
        // Unknown/partial combat packets do not establish changed supply.
        // An already-cleared source is handed back to fresh Reference holding,
        // rather than pretending its payout has completed this fight-tail.
        return g && g->known && (g->respawning || g->reward || g->other_holder || g->gems <= 0);
    }
    int nearest_altar(const n::Snapshot& s) const {
        int selected = -1, distance = 1000000;
        for (int i = 0; i < s.altar_count; ++i) {
            const auto& a = s.altars[i];
            const auto route = runtime_.route({}, {n::NodeKind::altar_entry, a.tile_id});
            if (route.turns() < 0) continue;
            const int cost = route.distance + ((a.visible && !a.hostile_lock) ? 0 :
                int(cambc::constants::altar_lock_hits));
            if (cost < distance || (cost == distance && a.tile_id < selected)) {
                selected = a.tile_id; distance = cost;
            }
        }
        return selected;
    }
    CombatForecast basic_quote(const n::Snapshot& s, int slot, int profile,
                              double health, double maximum, double first_loss = 0,
                              BuildStage stage = BuildStage::quote) {
        CombatForecast rejected;
        if (!build_complete_) {
            trace_reject(stage, BuildReason::inherited, s.groups[slot].id, profile);
            return rejected;
        }
        // Cache hits still pay for routing and repricing. Stop the complete
        // menu before either kind of quote consumes the fallback reserve.
        if (int(cambc::round_number()) != s.round || trace_quote_fuel(cambc::fuel_remaining(), stage, s.groups[slot].id) < 46500) {
            trace_reject(stage, BuildReason::round_or_fuel, s.groups[slot].id, profile);
            build_complete_ = false; return rejected;
        }
        if (profile < 0) {
            trace_reject(stage, BuildReason::invalid_profile, s.groups[slot].id, profile);
            build_complete_ = false; return rejected;
        }
        if (!costs_.is_cached(slot, profile)) {
            if (tracing_builder_) ++build_trace_.cache_misses;
            const unsigned before = unsigned(cambc::fuel_remaining());
            if (int(cambc::round_number()) != s.round || before < 45000u + quote_peak_ + 1500u) {
                trace_reject(stage, BuildReason::miss_round_or_reserve, s.groups[slot].id, profile);
                build_complete_ = false; return rejected;
            }
            if (tracing_builder_) ++build_trace_.kernel_calls;
            (void)costs_.compute(slot, profile);
            if (tracing_builder_) ++build_trace_.prepared_cells;
            const unsigned after = unsigned(cambc::fuel_remaining());
            if (int(cambc::round_number()) != s.round || after > before) {
                trace_reject(stage, BuildReason::compute_round_or_fuel, s.groups[slot].id, profile);
                build_complete_ = false; return rejected;
            }
            quote_peak_ = std::max(quote_peak_, before - after);
            if (tracing_builder_) {
                // Existing admitted-call interval, not isolated kernel fuel.
                build_trace_.miss_fuel_total += int(before - after);
                build_trace_.miss_fuel_max = std::max(build_trace_.miss_fuel_max, int(before - after));
                build_trace_.quote_peak = int(quote_peak_);
            }
        }
        return basic_quotes::reprice(costs_.get(slot, profile), health, maximum, first_loss);
    }
    RouteLeg continuation(const n::Snapshot& s, int excluded, double first_loss,
                          const CombatActor& actor, int profile,
                          BuildStage stage = BuildStage::continuation) {
        RouteLeg best;
        if (actor.health - first_loss <= 0) return best;
        for (int i = 0; i < s.group_count; ++i) {
            const auto& next = s.groups[i];
            // Same-source respawn needs an explicit timer model; use another
            // currently observed source rather than inventing immediate work.
            if (next.id == excluded || !usable(next)) continue;
            const auto& route = economic_scratch.altar_routes[i];
            if (route.turns() < 0) continue;
            if (best.turns() >= 0 && (route.distance > best.distance ||
                (route.distance == best.distance && next.id >= best.destination))) continue;
            if (basic_quote(s, i, profile, actor.health, actor.max_health, first_loss, stage).state == EstimateState::feasible) best = route;
            if (!build_complete_) {
                trace_reject(stage, BuildReason::inherited, s.groups[i].id, profile);
                return {};
            }
        }
        return best;
    }
    bool observation_into(const n::Snapshot& s, Observation& o) {
        build_trace_ = {}; tracing_builder_ = true;
        build_trace_.quote_peak = int(quote_peak_);
        build_complete_ = costs_.reset(s.self.id, true);
        o.count = o.purchase_count = 0;
        if (!build_complete_ || s.group_count < 0 || s.group_count > 8) {
            trace_reject(BuildStage::input, BuildReason::reset_or_group_count);
            return finish_builder(false);
        }
        o.round = s.round; o.team_score = s.team_score;
        // This binding is invoked only for actual Marcella. Draft ordinal zero
        // is the pure policy's financing role, never an inferred ally identity.
        o.pick_ordinal = 0; o.self = s.self; o.stats = conservative(s.stats);
        o.settings = s.settings; o.windows = s.windows; o.fuel_remaining = s.fuel;
        o.safe_hold = s.safe_hold;
        o.safe_channel = s.channel != e::Channel::none && s.channel_safe;
        bool immediate_unlocked = false;
        for (int i = 0; i < s.altar_count; ++i) if (s.altars[i].tile_id == altar_)
            immediate_unlocked = s.altars[i].adjacent && s.altars[i].visible && !s.altars[i].hostile_lock;
        const n::Node altar{n::NodeKind::altar_entry, altar_};
        o.current_to_altar = leg({}, altar, !immediate_unlocked);
        if (immediate_unlocked) o.current_to_altar.distance = 0;
        o.altar_to_spawn = leg(altar, {n::NodeKind::spawn, 0});
        o.current_to_spawn = leg({}, {n::NodeKind::spawn, 0});
        if (s.at_spawn) o.current_to_spawn.distance = 0;
        o.bank_return = {};
        o.bank_legal = altar_ >= 0; o.bank_route_safe = o.current_to_altar.turns() >= 0;
        for (const auto& q : s.purchases) {
            if (!q.purchasable || q.family < 0 || q.family >= 3 || q.cost <= 0) continue;
            auto& p = o.purchases[o.purchase_count++];
            p.family = q.family; p.shards = 1; p.cost = q.cost;
            p.legal_at_spawn = true; p.strength_gain = q.strength_gain;
            p.health_gain = q.health_gain; p.actual_spawn_heal = 0; p.spell_damage_gain = 0;
        }
        // Resolve at most four actual variants once. Health/Focus often share
        // the same offense profile, but keep their separate HP/maxHP repricing
        // and all three purchase alternatives in the policy's menu.
        auto& variants = economic_scratch.variants;
        auto& profiles = economic_scratch.variant_profiles;
        profiles.fill(-1); variants[0] = o.stats; profiles[0] = costs_.profile(variants[0]);
        build_trace_.profile_count = costs_.profile_count();
        if (profiles[0] < 0) {
            trace_reject(BuildStage::profiles, BuildReason::invalid_baseline_profile, -1, profiles[0]);
            o.purchase_count = 0; return finish_builder(false);
        }
        for (std::size_t gear = 1; gear <= o.purchase_count; ++gear) {
            const auto& q = o.purchases[gear - 1];
            const bool cash = s.self.gold.value >= q.cost;
            const bool finance = s.self.bag.value > 0 && s.self.gold.value < q.cost &&
                AccountingStock(s.self.gold.value) + AccountingStock(s.self.bag.value) *
                    s.settings.marcella_gold_per_gem >= q.cost;
            if (!cash && !finance) continue;
            variants[gear] = o.stats;
            variants[gear].strength += q.strength_gain; variants[gear].max_health += q.health_gain;
            profiles[gear] = costs_.profile(variants[gear]);
            build_trace_.profile_count = costs_.profile_count();
            if (profiles[gear] < 0) {
                trace_reject(BuildStage::profiles, BuildReason::invalid_purchase_profile, -1, profiles[gear]);
                o.purchase_count = 0; return finish_builder(false);
            }
        }
        economic_scratch.altar_routes.fill({});
        for (int i = 0; i < s.group_count; ++i) {
            const auto& g = s.groups[i];
            if (!usable(g)) continue;
            if (int(cambc::round_number()) != s.round || cambc::fuel_remaining() < 46500) {
                trace_reject(BuildStage::group_setup, BuildReason::round_or_fuel, s.groups[i].id);
                build_complete_ = false; o.purchase_count = 0; return finish_builder(false);
            }
            auto& quote = economic_scratch.quote_input;
            quote.actor_count = 1; quote.actors[0] = o.stats;
            quote.monster_count = g.combat.monster_count;
            quote.hold_ticks = s.settings.capture_rounds + 1;
            for (std::size_t m = 0; m < quote.monster_count; ++m) quote.monsters[m] = g.combat.monsters[m];
            if (!costs_.set_group(i, g.id, true, quote)) {
                trace_reject(BuildStage::group_setup, BuildReason::invalid_group, g.id);
                build_complete_ = false; o.purchase_count = 0; return finish_builder(false);
            }
            economic_scratch.altar_routes[i] = leg(altar, {n::NodeKind::group_entry, g.id});
        }
        // Populate the complete <=8x4 numeric table before selecting each
        // variant's own return, so neither chooser nor final certification can
        // miss a legal variant's tail or borrow another variant's capability.
        for (int i = 0; i < s.group_count; ++i) if (usable(s.groups[i])) {
            for (std::size_t gear = 0; gear <= o.purchase_count; ++gear) if (profiles[gear] >= 0 && !costs_.is_cached(i, profiles[gear])) {
                const auto& actor = variants[gear];
                (void)basic_quote(s, i, profiles[gear], actor.health, actor.max_health, 0, BuildStage::prepare);
                if (!build_complete_) {
                    trace_reject(BuildStage::prepare, BuildReason::inherited, s.groups[i].id, profiles[gear]);
                    o.count = o.purchase_count = 0; return finish_builder(false);
                }
            }
        }
        for (int i = 0; i < s.group_count; ++i) {
            const auto& g = s.groups[i];
            if (!usable(g)) continue;
            trace_candidate(g.id);
            if (int(cambc::round_number()) != s.round || trace_candidate_fuel(cambc::fuel_remaining()) < 46500) {
                trace_reject(BuildStage::candidate, BuildReason::round_or_fuel, s.groups[i].id);
                build_complete_ = false; o.count = o.purchase_count = 0; return finish_builder(false);
            }
            auto& candidate = o.opportunities[o.count];
            candidate.next_productive = {};
            candidate.variant_returns_known = true; candidate.variant_returns.fill({});
            candidate.group = g.id; candidate.gems = g.gems; candidate.gold = g.gold;
            candidate.live_observed = candidate.reward_or_alive = true;
            candidate.combat.actor_count = 1;
            candidate.combat.monster_count = g.combat.monster_count;
            candidate.combat.hold_ticks = g.combat.hold_ticks;
            for (std::size_t m = 0; m < g.combat.monster_count; ++m)
                candidate.combat.monsters[m] = g.combat.monsters[m];
            candidate.combat.actors[0] = o.stats;
            candidate.from_current = leg({}, {n::NodeKind::group_entry, g.id});
            candidate.from_spawn = leg({n::NodeKind::spawn, 0}, {n::NodeKind::group_entry, g.id});
            candidate.from_altar = economic_scratch.altar_routes[i];
            candidate.to_altar = leg({n::NodeKind::group_entry, g.id}, altar, true);
            // A financed first fight OR successor may be unsafe before its
            // enabling shard. Keep every variant's own complete return, rather
            // than letting one shard lend its safe successor to another kit.
            bool any_return = false;
            for (std::size_t gear = 0; gear <= o.purchase_count; ++gear) {
                if (profiles[gear] < 0) continue;
                const auto& actor = variants[gear];
                const auto fight = basic_quote(s, i, profiles[gear], actor.health, actor.max_health, 0, BuildStage::candidate);
                if (!build_complete_) {
                    trace_reject(BuildStage::candidate, BuildReason::inherited, g.id, profiles[gear]);
                    o.count = o.purchase_count = 0; return finish_builder(false);
                }
                if (fight.state != EstimateState::feasible) continue;
                const auto next = continuation(s, g.id, fight.worst_camp_damage, actor, profiles[gear]);
                if (!build_complete_) {
                    trace_reject(BuildStage::continuation, BuildReason::inherited, g.id, profiles[gear]);
                    o.count = o.purchase_count = 0; return finish_builder(false);
                }
                const int variant = gear ? o.purchases[gear - 1].family + 1 : 0;
                candidate.variant_returns[variant] = next;
                any_return |= next.turns() >= 0;
            }
            candidate.next_productive = candidate.variant_returns[0];
            if (any_return) ++o.count;
        }
        // Bank-only is an alternative to the complete production menu. Reuse
        // the quotes populated above instead of spending the first misses on
        // this continuation before any productive candidate has been built.
        if (s.self.bag.value > 0) o.bank_return = continuation(s, -1, 0, variants[0], profiles[0], BuildStage::bank_return);
        if (!build_complete_) {
            trace_reject(BuildStage::bank_return, BuildReason::inherited);
            o.count = o.purchase_count = 0; return finish_builder(false);
        }
        if (s.round != int(cambc::round_number())) {
            trace_reject(BuildStage::final_check, BuildReason::final_round);
            o.count = o.purchase_count = 0; return finish_builder(false);
        }
        return finish_builder(true);
    }
    bool prepared_cost(int slot, const CombatActor& actor, basic_quotes::Cost& cost) const {
        for (std::size_t gear = 0; gear <= planning_.purchase_count; ++gear) {
            const int profile = economic_scratch.variant_profiles[gear];
            if (profile < 0) continue;
            const auto& known = economic_scratch.variants[gear];
            if (actor.health != known.health || actor.max_health != known.max_health || actor.strength != known.strength ||
                actor.basic_modifier != known.basic_modifier || actor.ranged != known.ranged) continue;
            if (!costs_.is_cached(slot, profile)) return false;
            cost = costs_.get(slot, profile); return true;
        }
        return false;
    }
    static CombatForecast cached_policy_quote(void* context, const Opportunity& opportunity,
                                              const CombatInput& input) {
        return static_cast<Bot*>(context)->policy_quote(opportunity, input);
    }
    CombatForecast policy_quote(const Opportunity& opportunity, const CombatInput& input) {
        auto reject = [&] {
            build_complete_ = false;
            if (active_provider_) active_provider_->complete = false;
            return CombatForecast{};
        };
        const auto& s = snapshot_;
        // choose_job is synchronous and performs no SDK action. A round change
        // is the only way that its private current packet can age during it.
        if (!active_provider_ || !build_complete_ || !s.complete ||
            int(cambc::round_number()) != s.round ||
            cambc::fuel_remaining() < action_reserve_ + cached_certificate_reserve_ ||
            input.actor_count != 1 || input.hold_ticks != s.settings.capture_rounds + 1) return reject();
        int slot = -1;
        for (int i = 0; i < s.group_count; ++i) if (s.groups[i].id == opportunity.group) { slot = i; break; }
        if (slot < 0 || !usable(s.groups[slot])) return reject();
        const auto& source = s.groups[slot].combat;
        if (input.monster_count != source.monster_count) return reject();
        for (std::size_t i = 0; i < source.monster_count; ++i) {
            const auto& a = input.monsters[i]; const auto& b = source.monsters[i];
            if (a.id != b.id || a.public_health != b.public_health || a.observed_health != b.observed_health ||
                a.effective_strength != b.effective_strength || a.visible != b.visible) return reject();
        }
        auto actor = input.actors[0];
        // The policy ages a nonexistent shield through prefix/travel. Its
        // negative expiry has no effect; every other sanitized fact must match.
        if (actor.shield != 0) return reject();
        actor.shield_expiry_tick = 0;
        if (!basic_quotes::sanitized_actor(actor, s.self.id)) return reject();
        basic_quotes::Cost cost;
        // Cache-only: no late kernel execution or partially quoted menu.
        if (!prepared_cost(slot, actor, cost)) return reject();
        return basic_quotes::reprice(cost, actor.health, actor.max_health);
    }
    bool certify(const n::Snapshot& s, const Observation& o, const Decision& d) const {
        if (!d.feasible || d.rank.status != AccountingStatus::feasible ||
            d.rank.occupied_turns < 0 || d.rank.occupied_turns > s.windows.official_remaining) return false;
        if (d.schedule.terminal_win) {
            const double loss = s.route_stress * d.rank.occupied_turns;
            return s.stats.health - loss >= std::max(.2 * s.stats.max_health, 2 * s.route_stress);
        }
        const auto* next = group(s, return_id(d));
        if (!next || !usable(*next)) return false;
        auto actor = conservative(s.stats);
        if (d.purchase_family >= 0) {
            const auto* bought = purchase(o, d.purchase_family);
            if (!bought) return false;
            actor.strength += bought->strength_gain;
            actor.max_health += bought->health_gain;
        }
        if (!std::isfinite(s.route_stress) || s.route_stress < 0) return false;
        const auto cached = [&](const n::GroupFacts* target, basic_quotes::Cost& cost) {
            for (int i = 0; i < s.group_count; ++i) if (&s.groups[i] == target)
                return prepared_cost(i, actor, cost);
            return false;
        };
        double first_loss = 0;
        if (d.group >= 0) {
            const auto* first = group(s, d.group);
            if (!first || !usable(*first)) return false;
            basic_quotes::Cost cost;
            if (!cached(first, cost)) return false;
            const auto f = basic_quotes::reprice(cost, actor.health, actor.max_health);
            if (f.state != EstimateState::feasible) return false;
            first_loss = f.worst_camp_damage;
        }
        // Charge all visible awake sources over the WHOLE prefix, production,
        // bank and return. This deliberately double-charges an awake target.
        basic_quotes::Cost successor;
        if (!cached(next, successor)) return false;
        actor.health -= first_loss + s.route_stress * d.rank.occupied_turns;
        if (actor.health <= 0 || !std::isfinite(actor.health)) return false;
        return basic_quotes::reprice_exposed(successor, actor.health, actor.max_health,
            s.route_stress, s.settings.capture_rounds + 1).state == EstimateState::feasible;
    }
    bool purchased_tail_safe(const n::Snapshot& s, bool& checked) {
        checked = false;
        const auto finish = [&](bool safe) {
            checked = s.round == int(cambc::round_number());
            return checked && safe;
        };
        const auto* first = group(s, project_.selected.group);
        const auto* next = group(s, renewal_);
        if (!s.complete || s.round != int(cambc::round_number()) || !first || !next || first == next ||
            !usable(*first) || !usable(*next) || !std::isfinite(s.route_stress) || s.route_stress < 0) return false;
        if (!runtime_.prepare_selection() || s.round != int(cambc::round_number())) return false;
        const int travel = leg({}, {n::NodeKind::group_entry, first->id}).turns();
        const int conversion = leg({n::NodeKind::group_entry, first->id}, {n::NodeKind::altar_entry, altar_}, true).turns();
        const int renewal = leg({n::NodeKind::altar_entry, altar_}, {n::NodeKind::group_entry, next->id}).turns();
        if (travel < 0 || conversion < 0 || renewal < 0) return false;
        // The selection table is not reused across rounds. Rebuild only these
        // two actual current groups with the newly observed own kit/HP.
        build_complete_ = costs_.reset(s.self.id, true);
        const auto actual = conservative(s.stats); const int profile = costs_.profile(actual);
        if (profile < 0) return false;
        const n::GroupFacts* targets[2]{first, next};
        for (int i = 0; i < 2; ++i) {
            auto& input = economic_scratch.quote_input;
            input.actor_count = 1; input.actors[0] = actual; input.monster_count = targets[i]->combat.monster_count;
            input.hold_ticks = s.settings.capture_rounds + 1;
            for (std::size_t m = 0; m < input.monster_count; ++m) input.monsters[m] = targets[i]->combat.monsters[m];
            if (!costs_.set_group(i, targets[i]->id, true, input)) return false;
            (void)basic_quote(s, i, profile, actual.health, actual.max_health);
            if (!build_complete_) return false;
        }
        const auto initial = basic_quotes::reprice(costs_.get(0, profile), actual.health, actual.max_health);
        if (initial.state == EstimateState::unsafe) return finish(false);
        if (initial.state != EstimateState::feasible) return false;
        const int channel = s.settings.channel_ticks(s.self.bag.value + first->gems);
        const int occupied = travel + initial.clear_ticks + s.settings.capture_rounds + 1 + conversion + channel + renewal;
        if (channel < 0 || occupied < 0) return false;
        if (occupied > s.windows.official_remaining) return finish(false);
        const double health = actual.health - (initial.worst_camp_damage + s.route_stress * occupied);
        const auto tail = basic_quotes::reprice_exposed(costs_.get(1, profile),
            health, actual.max_health, s.route_stress, s.settings.capture_rounds + 1);
        if (tail.state != EstimateState::feasible && tail.state != EstimateState::unsafe) return false;
        return finish(tail.state == EstimateState::feasible);
    }
    ProjectFacts facts(const n::Snapshot& s, e::Observation* observed = nullptr) {
        ProjectFacts f;
        f.observation_round = s.round; f.own_gold = s.self.gold.value; f.own_bag = s.self.bag.value;
        f.own_bank_receipt = s.receipts.bank; f.own_capture_receipt = s.receipts.capture;
        f.own_purchase_receipt = s.receipts.purchase;
        f.purchased_family = s.receipts.purchase_family; f.purchased_cost = s.receipts.purchase_cost;
        f.purchased_count = s.receipts.purchase_count; f.own_stats = s.stats; f.own_stats_known = s.complete;
        f.own_purchase_confirmed = s.receipts.purchase_count == 1;
        if ((project_.stage == ProjectStage::purchase || project_.stage == ProjectStage::purchase_first) &&
            project_.purchase_intent_outstanding && s.receipts.purchase > project_.purchase_baseline) {
            f.own_purchase_confirmed = f.own_purchase_confirmed && s.receipts.purchase_family == project_.selected.purchase_family &&
                s.self.gold.value == project_.purchase_intent_gold - s.receipts.purchase_cost &&
                s.self.bag.value == project_.purchase_intent_bag;
        }
        // A remote camp disappearing from vision cannot undo an actual item
        // purchase. Travel is nonproduction; only the fresh named-source packet
        // may admit the current source and successor with the bought kit.
        if (project_.stage == ProjectStage::purchase_inspection && observed &&
            observed->owner_turn == s.round && observed->group_id == project_.selected.group &&
            s.complete && s.settings_valid && s.round == int(cambc::round_number())) {
            f.purchase_targets_unavailable = purchased_target_unavailable(group(s, project_.selected.group)) ||
                purchased_target_unavailable(group(s, renewal_));
            if (observed->at_target) {
                f.purchase_source_reached = true;
                if (!f.purchase_targets_unavailable)
                    f.purchase_tail_safe = purchased_tail_safe(s, f.purchase_tail_checked);
                observed->fuel = int(cambc::fuel_remaining());
            }
        }
        f.productive_waypoint = renewal_;
        f.at_named_productive_waypoint = project_.stage == ProjectStage::renewal &&
            renewal_ >= 0 && runtime_.productive_ready(renewal_);
        return f;
    }
    n::Goal goal() const {
        n::Goal out;
        out.controller_stage = int(project_.stage); out.phase = project_.execution_state.phase;
        out.group_id = project_.selected.group; out.altar_tile_id = altar_;
        out.forecast_turns = std::max(1, project_.selected.rank.occupied_turns);
        out.purchase_family = project_.selected.purchase_family;
        out.production = project_.stage == ProjectStage::production;
        if (out.phase == e::Phase::recover) out.kind = n::GoalKind::recover;
        else if (project_.stage == ProjectStage::renewal) {
            out.kind = n::GoalKind::productive_entry; out.group_id = renewal_;
        } else if (project_.stage == ProjectStage::purchase || project_.stage == ProjectStage::purchase_first) out.kind = n::GoalKind::spawn;
        else if (project_.stage == ProjectStage::initial_bank || project_.stage == ProjectStage::output_bank)
            out.kind = out.phase == e::Phase::allocate ? n::GoalKind::camp_entry : n::GoalKind::altar;
        else if (out.phase == e::Phase::hold) out.kind = n::GoalKind::camp_spawn;
        else if (out.phase == e::Phase::clear) out.kind = n::GoalKind::camp_attack;
        else out.kind = n::GoalKind::camp_entry;
        // Inspection has not admitted production. Cost every visible external
        // threat over the fresh remaining entry leg, not the frozen whole
        // purchase/capture/bank/return schedule. Unknown geometry stays unknown.
        if (project_.stage == ProjectStage::purchase_inspection)
            out.forecast_turns = runtime_.route({}, {n::NodeKind::group_entry, out.group_id}).turns();
        return out;
    }
    static bool movement(e::ActionKind k) {
        return k == e::ActionKind::move || k == e::ActionKind::withdraw || k == e::ActionKind::vacate;
    }
    bool receipt_transition_due(const n::Snapshot& s) const {
        const auto& p = project_;
        const bool capture = p.stage == ProjectStage::production &&
            (p.execution_state.phase == e::Phase::clear || p.execution_state.phase == e::Phase::hold) &&
            s.receipts.capture_round == s.round && s.receipts.capture_group == p.selected.group &&
            s.receipts.capture > p.payout_baseline;
        const bool bank = (p.stage == ProjectStage::initial_bank || p.stage == ProjectStage::output_bank) &&
            p.bank_intent_outstanding && s.round > p.bank_intent_round && s.self.bag.value == 0 &&
            s.receipts.bank > p.bank_baseline;
        const bool buy = (p.stage == ProjectStage::purchase || p.stage == ProjectStage::purchase_first) && p.purchase_intent_outstanding &&
            s.receipts.purchase > p.purchase_baseline && s.receipts.purchase_count == 1 &&
            s.receipts.purchase_family == p.selected.purchase_family;
        return capture || bank || buy;
    }
    void trace_life(p5::diagnostics::CardLifeEvent event, const n::Snapshot& s,
        ProjectStage before, int reason = -1, int action = -1, int outcome = 0,
        const e::Observation* observed = nullptr, const ProjectFacts* facts = nullptr) noexcept {
        if (!card_life_active_) return;
        p5::diagnostics::CardLifeRecord record;
        const bool cached_snapshot = s.complete && s.self.id == card_life_actor_ && s.round >= card_life_round_;
        if (cached_snapshot) card_life_round_ = s.round;
        record.event = event; record.actor = card_life_actor_; record.round = card_life_round_;
        record.job = int(project_.selected.job); record.group = project_.selected.group;
        record.renewal = renewal_; record.altar = altar_; record.family = project_.selected.purchase_family;
        record.renewal_entry = project_renewal_entry_; record.from_stage = int(before);
        record.stage = int(project_.stage); record.phase = int(project_.execution_state.phase);
        record.reason = reason; record.action = action; record.outcome = outcome;
        if (cached_snapshot) {
            record.position = s.position_tile_id; record.gold = s.self.gold.value; record.bag = s.self.bag.value;
            record.purchase = s.receipts.purchase; record.capture = s.receipts.capture; record.bank = s.receipts.bank;
            record.budget = project_.execution_state.phase_budget - (s.round - project_.execution_state.phase_start);
        }
        record.flags = (cached_snapshot && s.alive ? 1 : 0) | (cached_snapshot ? 1 << 11 : 0) |
            (cached_snapshot && s.settings_valid ? 1 << 12 : 0) |
            (project_.bank_intent_outstanding ? 1 << 8 : 0) | (project_.purchase_intent_outstanding ? 1 << 9 : 0) |
            (cached_snapshot && record.budget <= 0 ? 1 << 10 : 0);
        if (observed) {
            record.observed_group = observed->group_id;
            record.flags |= (1 << 13) | (observed->safe ? 1 << 1 : 0) |
                (observed->needs_recovery ? 1 << 2 : 0) | (observed->at_target ? 1 << 7 : 0);
        }
        if (facts) record.flags |= (1 << 14) | (facts->purchase_source_reached ? 1 << 3 : 0) |
            (facts->purchase_tail_checked ? 1 << 4 : 0) | (facts->purchase_tail_safe ? 1 << 5 : 0) |
            (facts->at_named_productive_waypoint ? 1 << 6 : 0) |
            (facts->purchase_targets_unavailable ? 1 << 15 : 0);
        (void)runtime_.record_card_life(record);
        if (event == p5::diagnostics::CardLifeEvent::terminal) card_life_active_ = false;
    }
    e::Decision execute_project(const n::Snapshot& s, const e::Observation& observed, const ProjectFacts& facts) {
        using Event = p5::diagnostics::CardLifeEvent;
        const auto before = project_.stage;
        const auto result = execute_job(observed, facts, project_);
        if (!card_life_started_ && (before == ProjectStage::purchase || before == ProjectStage::purchase_first) &&
            project_.stage == ProjectStage::purchase_inspection) {
            card_life_started_ = card_life_active_ = true;
            card_life_actor_ = s.self.id; card_life_round_ = s.round;
            trace_life(Event::purchase, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        }
        if (!card_life_active_) return result;
        if (project_.stage == ProjectStage::aborted || project_.stage == ProjectStage::complete)
            trace_life(Event::terminal, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        else if (before == ProjectStage::purchase_inspection && project_.stage == ProjectStage::production)
            trace_life(Event::production, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        else if (before == ProjectStage::production && project_.stage == ProjectStage::output_bank)
            trace_life(Event::payout, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        else if (before == ProjectStage::output_bank && project_.stage == ProjectStage::renewal)
            trace_life(Event::renewal, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        else if (project_.stage == ProjectStage::purchase_inspection && facts.purchase_source_reached && !facts.purchase_tail_checked)
            trace_life(Event::proof_wait, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        if (project_.stage == ProjectStage::output_bank && result.action.kind == e::ActionKind::start_tribute)
            trace_life(Event::bank_intent, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        if (project_.stage == ProjectStage::renewal && project_renewal_entry_ >= 0 && s.position_tile_id == project_renewal_entry_)
            trace_life(Event::renewal_arrival, s, before, int(result.reason), int(result.action.kind), 0, &observed, &facts);
        return result;
    }
    void abort_project(int outcome = 0, const e::Observation* observed = nullptr) noexcept {
        const auto before = project_.stage;
        active_provider_ = nullptr; running_ = false; project_.stage = ProjectStage::aborted;
        trace_life(p5::diagnostics::CardLifeEvent::terminal, snapshot_, before, -1, -1, outcome, observed);
    }
    void finish_project() {
        if (project_.stage == ProjectStage::complete &&
            (project_.selected.job == Job::cash_produce || project_.selected.job == Job::finance_produce))
            runtime_.record_card_completion(int(project_.selected.job), project_.selected.group,
                project_.selected.purchase_family, renewal_);
        running_ = false;
    }
    void stale_turn() {
        // A suspended operation may already have changed real stock. Discard
        // its controller intents; a future project starts from actual stock.
        active_provider_ = nullptr; running_ = false;
        // The retained forecast payload is never used while stopped. Avoid
        // constructing another large empty schedule on a depleted callback.
        const auto before = project_.stage; project_.stage = ProjectStage::aborted;
        trace_life(p5::diagnostics::CardLifeEvent::terminal, snapshot_, before, -1, -1, 3);
        runtime_.finish_turn();
    }
    void fallback(const n::Snapshot& s) {
        if (s.round != int(cambc::round_number())) { stale_turn(); return; }
        // A deferred menu must leave the accepted capture-to-bank plan intact.
        // Reference may discard its plan when entered with depleted fuel.
        if (cambc::fuel_remaining() < 45000) runtime_.finish_turn();
        else runtime_.reference_turn(&s);
    }
public:
    explicit Bot(Arm arm) { profile_.arm = arm; }
    template<class Function> void guarded_turn(Function&& function) {
        if (!runtime_.guard_turn(static_cast<Function&&>(function))) abort_project(4);
    }
    void turn() {
        // Companion dispatch needs one actual type query, not a complete card
        // snapshot followed by the reference's own fresh observation packet.
        if (cambc::self().champion_name() != cambc::champion_type::marcella) {
            runtime_.reference_turn(); return;
        }
        if (!runtime_.begin_turn()) return;
        if (!running_ && runtime_.reference_busy() && !runtime_.reference_capture_offer_hint()) {
            runtime_.reference_turn(); return;
        }
        // With no own cash or bankable inventory, neither arm can finance a
        // purchase. Bootstrap through the ordinary fresh reference caller
        // before paying for the large economic export and chooser.
        if (!running_ && cambc::self().gold_count() == 0 && cambc::self().gem_count() == 0) {
            runtime_.reference_turn(); return;
        }
        auto& s = snapshot_;
        if (!runtime_.snapshot_into(s) || !s.complete || s.round != int(cambc::round_number())) { stale_turn(); return; }
        if (!s.alive || !s.settings_valid) {
            trace_life(p5::diagnostics::CardLifeEvent::terminal, s, project_.stage, -1, -1, 5);
            running_ = false;
        }
        if (!running_ && runtime_.reference_busy() && !runtime_.accept_reference_capture_offer(s)) {
            fallback(s); return;
        }
        if (s.self.bag.value > 0 && AccountingStock(s.team_score) + s.self.bag.value >= s.settings.winning_gems) {
            abort_project(6); fallback(s); return;
        }
        if (!running_) {
            // This adapter requires a different personally known productive
            // successor. Let reference finish ordinary banking, holding and
            // exploration until such a card project can exist.
            int usable_groups = 0;
            for (int i = 0; i < s.group_count; ++i) usable_groups += usable(s.groups[i]);
            if (usable_groups < 2) { fallback(s); return; }
            if (!s.alive || !s.settings_valid || !runtime_.prepare_selection()) {
                if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                fallback(s); return;
            }
            // The checked field call performs no action; preserve the same
            // observation packet and query only its actual remaining budget.
            s.fuel = int(cambc::fuel_remaining()); altar_ = nearest_altar(s);
            const auto fuel_start = cambc::fuel_remaining();
            auto& input = planning_;
            const bool complete_menu = observation_into(s, input);
            const auto fuel_built = cambc::fuel_remaining();
            input.fuel_remaining = int(fuel_built);
            const int built_round = int(cambc::round_number());
            if (built_round != s.round) { stale_turn(); return; }
            if (!complete_menu || input.count == 0 || fuel_built < 45000) {
                runtime_.record_card_plan(fuel_start, fuel_built, fuel_built,
                    fuel_built, input.count, false, built_round, complete_menu, &build_trace_);
                fallback(s); return;
            }
            auto& chosen = selection_;
            CombatQuoteProvider quotes{this, cached_policy_quote, true};
            active_provider_ = &quotes;
            chosen = choose_job(input, {}, profile_, &quotes);
            active_provider_ = nullptr;
            quotes.complete = quotes.complete && build_complete_;
            if (!quotes.complete) chosen = {};
            const auto fuel_chosen = cambc::fuel_remaining();
            const int chosen_round = int(cambc::round_number());
            if (chosen_round != s.round) {
                runtime_.record_card_plan(fuel_start, fuel_built, fuel_chosen,
                    fuel_chosen, input.count, false, chosen_round, complete_menu && quotes.complete);
                stale_turn(); return;
            }
            if (fuel_chosen < 16000u + 2 * quote_peak_) {
                runtime_.record_card_plan(fuel_start, fuel_built, fuel_chosen,
                    fuel_chosen, input.count, false, chosen_round, complete_menu && quotes.complete);
                fallback(s); return;
            }
            if (!quotes.complete) {
                runtime_.record_card_plan(fuel_start, fuel_built, fuel_chosen,
                    fuel_chosen, input.count, false, chosen_round, false);
                fallback(s); return;
            }
            const bool certified = certify(s, input, chosen);
            const auto fuel_certified = cambc::fuel_remaining();
            const int selection_end_round = int(cambc::round_number());
            runtime_.record_card_plan(fuel_start, fuel_built, fuel_chosen,
                fuel_certified, input.count, certified && fuel_certified >= 30000, selection_end_round,
                complete_menu && quotes.complete);
            if (selection_end_round != s.round) { stale_turn(); return; }
            if (!certified || fuel_certified < 30000) { fallback(s); return; }
            renewal_ = return_id(chosen);
            const auto* next_group = group(s, renewal_);
            project_renewal_entry_ = next_group ? next_group->entry_tile_id : -1;
            project_ = start_project(chosen, facts(s));
            running_ = project_.stage != ProjectStage::aborted;
        }
        // One current packet and at most one native action attempt per owner
        // turn. Internal no-action stage transitions remain bounded below.
        bool dispatched = false;
        if (running_) {
            s.fuel = int(cambc::fuel_remaining());
            if (!s.complete || s.round != int(cambc::round_number())) { stale_turn(); return; }
            for (int transition = 0; transition < 4; ++transition) {
                // Apply observed receipts without asking an old, now respawning
                // production target or old shop goal to authorize the next step.
                // Fuel0 allows receipt transitions but emits no new real intent.
                if (receipt_transition_due(s)) {
                    auto receipt_goal = goal(); receipt_goal.production = false;
                    auto observed = runtime_.execution_observation(project_.execution_state, receipt_goal);
                    if (observed.owner_turn != int(cambc::round_number())) { stale_turn(); return; }
                    observed.fuel = 0;
                    const auto current_facts = facts(s, &observed);
                    if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                    (void)execute_project(s, observed, current_facts);
                    if (project_.stage == ProjectStage::complete || project_.stage == ProjectStage::aborted) {
                        finish_project(); break;
                    }
                    continue;
                }
                const bool inspecting = project_.stage == ProjectStage::purchase_inspection;
                if (inspecting || (project_.stage == ProjectStage::production &&
                    project_.execution_state.phase == e::Phase::bootstrap)) {
                    if (!runtime_.prepare_selection()) {
                        if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                        // Inspection still observes channels, point safety and
                        // its original deadline. A missing route cannot move.
                        if (!inspecting) break;
                    }
                    s.fuel = int(cambc::fuel_remaining());
                }
                const auto before = goal();
                auto observed = runtime_.execution_observation(project_.execution_state, before);
                if (observed.owner_turn != int(cambc::round_number())) { stale_turn(); return; }
                const auto current_facts = facts(s, &observed);
                if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                auto selected = execute_project(s, observed, current_facts);
                const auto after = goal();
                if (project_.stage == ProjectStage::complete || project_.stage == ProjectStage::aborted) {
                    finish_project(); break;
                }
                // A changed stage/phase/goal always gets a new zero-move snapshot.
                if (!(before == after)) continue;
                if (selected.action.kind == e::ActionKind::yield || selected.action.kind == e::ActionKind::refresh ||
                    movement(selected.action.kind)) {
                    runtime_.add_moves(observed, project_.execution_state, after);
                    if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                    const auto move_facts = facts(s, &observed);
                    if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                    selected = execute_project(s, observed, move_facts);
                    if (!(goal() == after)) continue;
                }
                dispatched = true;
                const auto result = runtime_.apply(selected, observed, project_.execution_state, after);
                const auto kind = selected.action.kind;
                const bool harmless = kind == e::ActionKind::hold || kind == e::ActionKind::preserve_channel ||
                    kind == e::ActionKind::yield || kind == e::ActionKind::refresh || kind == e::ActionKind::inspect;
                if (result == n::ApplyResult::stale || (result == n::ApplyResult::stopped && !harmless))
                    abort_project(20 + int(result), &observed);
                if (s.round != int(cambc::round_number())) { stale_turn(); return; }
                break;
            }
        }
        if (!running_ && !dispatched) { fallback(s); return; }
        runtime_.finish_turn();
    }
};

inline int run(Arm arm) {
    static Bot bot(arm);
    while (true) {
        bot.guarded_turn([&] {
            if (cambc::is_draft()) n::Runtime::draft(Profile::draft_request()); else bot.turn();
        });
        cambc::end_turn();
    }
}
} // namespace p5::dividend::native_binding
