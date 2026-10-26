#pragma once
#include "profile.h"
#include "organization.h"
#include <span>

namespace p8::common {
enum class DraftRole { canary, home_conversion, home_production, independent, collector, safe_helper };
inline DraftRole draft_role(int ordinal) {
    return ordinal == 0 ? DraftRole::canary : ordinal == 1 ? DraftRole::home_conversion :
        ordinal == 2 ? DraftRole::home_production : DraftRole::independent;
}
struct DraftCandidate {
    p6::Champion champion = p6::Champion::other;
    Plan first_income{};
    p6::Schedule complete_schedule{};
    // Public starting-stat scenarios, never another actor's private runtime data.
    std::array<p6::Exposure,4> cumulative_survival{};
    p6::Fact<double> starting_health{}, starting_max_health{};
    bool public_geometry_verified = false, ability_schedule_legal = false;
};
struct DraftInputs {
    int ordinal = 0, picks = 3, prospective_actor = -1;
    std::array<p6::Champion,5> own_picks{p6::Champion::other,p6::Champion::other,
        p6::Champion::other,p6::Champion::other,p6::Champion::other};
    p6::Settings settings{};
    p6::ScenarioWindows windows{};
    p6::Fact<std::uint64_t> settings_identity{};
};
struct DraftDecision {
    int candidate = -1;
    p6::Champion champion = p6::Champion::other;
    DraftRole role = DraftRole::canary;
    p6::ScoreEvaluation rank{};
};
inline int role_tie(p6::Champion champion, DraftRole role) {
    // These resolve equal complete-cycle forecasts only. No speculative gold,
    // Focus, healing or cooperation credit is added to the economic score.
    if (role == DraftRole::canary) {
        if (champion == p6::Champion::tavi) return 0; // Field shopping simplifies return.
        if (champion == p6::Champion::vendetta) return 1;
        if (champion == p6::Champion::varg) return 2;
    } else if (role == DraftRole::home_conversion && champion == p6::Champion::marcella) return 0;
    else if (role == DraftRole::home_production && champion == p6::Champion::vesper_vale) return 0;
    return 3 + static_cast<int>(champion);
}
inline DraftDecision select_draft(const DraftInputs& in, std::span<const DraftCandidate> candidates,
    const std::array<p6::Champion,8>* tie_priority = nullptr) {
    DraftDecision out; out.role = draft_role(in.ordinal);
    auto tie = [&](p6::Champion type) {
        if (tie_priority) {
            for (int i=0;i<8;++i) if ((*tie_priority)[i]==type) return i;
            return 8;
        }
        return role_tie(type,out.role);
    };
    if (in.picks < 3 || in.picks > 5 || in.ordinal < 0 || in.ordinal >= in.picks ||
        in.prospective_actor < 0 || candidates.size() > champion_pool.size() ||
        in.settings_identity.source != p6::Source::queried_setting || !in.settings_identity.value)
        return out;
    for (int i = 0; i < in.ordinal; ++i) {
        if (!supported_champion(in.own_picks[i])) return out;
        for (int j = 0; j < i; ++j) if (in.own_picks[i] == in.own_picks[j]) return out;
    }
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (!supported_champion(c.champion) || !c.public_geometry_verified || !c.ability_schedule_legal ||
            c.starting_health.source != p6::Source::queried_setting ||
            c.starting_max_health.source != p6::Source::queried_setting ||
            !plan_valid(c.first_income,in.prospective_actor) ||
            c.first_income.organization != Organization::U || c.first_income.crew_count != 1 ||
            c.complete_schedule.actor_count != 1 ||
            c.complete_schedule.actors[0].actor_id != in.prospective_actor ||
            c.complete_schedule.actors[0].initial_bag.value != 0 ||
            c.complete_schedule.target_id != c.first_income.group) continue;
        bool taken = false;
        for (int j = 0; j < in.ordinal; ++j) taken |= c.champion == in.own_picks[j];
        if (taken) continue;
        bool safe = true; double previous = 0;
        for (const auto& exposure : c.cumulative_survival) {
            safe &= p6::survives(exposure) && exposure.health == c.starting_health.value &&
                exposure.max_health == c.starting_max_health.value && exposure.assigned_timed_heal == 0 &&
                exposure.incoming_total >= previous;
            previous = exposure.incoming_total;
        }
        if (!safe) continue;
        const auto rank = p6::evaluate_schedule(c.complete_schedule,in.windows,in.settings,
            {0,in.prospective_actor},0);
        if (!rank.eligible() || rank.min_margin <= 0 || rank.productive_arrival > forecast_window) continue;
        const int comparison = out.candidate < 0 ? 1 : p6::compare(rank,out.rank);
        if (comparison > 0 || (comparison == 0 &&
            tie(c.champion) < tie(out.champion))) {
            out.candidate = static_cast<int>(i); out.champion = c.champion; out.rank = rank;
        }
    }
    return out; // None certified: unresolved draft input, never invented first income.
}
} // namespace p8::common
