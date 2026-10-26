#include "../common/src/observe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { std::fprintf(stderr, "Draft check %d failed at line %d: %s\n", checks, __LINE__, #condition); std::abort(); } } while (false)
using namespace p5;

static DraftContext fixture() {
    // Synthetic role differences exercise selection, not copied game balances.
    DraftContext context;
    context.team_size = 5; context.pick_index = 0; context.independent_cells = 5;
    context.pool_count = context.pool.size();
    for (std::size_t i = 0; i < context.pool_count; ++i)
        context.pool[i] = {draft_aliases[i], true, draft_actor_count(draft_aliases[i]), 0, 10, 0, 0, 0};
    context.pool[0].health = 80;
    context.pool[1].productivity = 40;
    context.pool[2].productivity = 55;
    context.pool[3].productivity = 30;
    context.pool[4].productivity = 90;
    context.pool[5].productivity = 35;
    context.pool[6].health = 85;
    context.pool[7].healing = 60;
    context.pool[8].mobility = 80;
    context.pool[9].health = 60; context.pool[9].shielding = 30;
    context.pool[10].productivity = 45;
    context.pool[11].health = 90; context.pool[11].shielding = 70;
    return context;
}
static DraftRequest role(DraftWeights weights) {
    DraftRequest request;
    request.roles.fill(weights);
    return request;
}
static void chosen(const DraftContext& context, const DraftRequest& request, std::string_view alias) {
    const auto decision = choose_draft(context, request);
    CHECK(decision.feasible); CHECK(decision.alias == alias); CHECK(std::isfinite(decision.score));
}
static void rejected(const DraftContext& context, const DraftRequest& request, const char* reason) {
    const auto decision = choose_draft(context, request);
    CHECK(!decision.feasible); CHECK(decision.alias.empty()); CHECK(std::strcmp(decision.reason, reason) == 0);
}
int main() {
    const auto production = role({1, 0, 0, 0, 0});
    const auto survival = role({0, 1, 0, 0, 0});
    const auto healing = role({0, 0, 1, 0, 0});
    const auto mobility = role({0, 0, 0, 1, 0});
    auto context = fixture();
    chosen(context, production, "stellar");
    chosen(context, survival, "heart_of_ruin");
    chosen(context, healing, "willow");
    chosen(context, mobility, "ren");
    CHECK(choose_draft(context, survival).score == 160);
    CHECK(draft_aliases.size() == 12 && draft_actor_count("cassi_and_issac") == 2);
    CHECK(draft_actor_count("cassi") == 0 && draft_actor_count("issac") == 0);
    // Every legal alias can win on supplied traits; no preferred five-pick prefix.
    for (std::size_t i = 0; i < context.pool_count; ++i) {
        auto candidate = fixture();
        for (auto& traits : candidate.pool) traits.productivity = 0;
        candidate.pool[i].productivity = 1;
        chosen(candidate, production, candidate.pool[i].alias);
    }

    // Coverage uses available independent cells, including previous actors.
    auto coverage = role({1, 0, 0, 0, 10});
    for (auto& traits : context.pool) traits.productivity = 0;
    context.pool[8].productivity = 1;
    context.independent_cells = 0;
    chosen(context, coverage, "ren");
    context.independent_cells = 1;
    chosen(context, coverage, "ren");
    CHECK(choose_draft(context, coverage).score == 11);
    context.independent_cells = 2;
    chosen(context, coverage, "cassi_and_issac");
    CHECK(choose_draft(context, coverage).score == 20);
    context.own_picks[0] = "willow"; context.own_count = 1; context.pick_index = 1;
    chosen(context, coverage, "ren");
    context.independent_cells = 3;
    chosen(context, coverage, "cassi_and_issac");
    context.own_picks[0] = "cassi_and_issac";
    chosen(context, coverage, "ren");
    CHECK(choose_draft(context, coverage).score == 11);
    context.independent_cells = std::numeric_limits<int>::max();
    chosen(context, coverage, "ren");
    CHECK(choose_draft(context, coverage).score == 11);

    DraftRequest request;
    request.required_anchor = "tavi";
    request.roles[0] = mobility.roles[0]; request.roles[1] = production.roles[0];
    request.roles[2] = survival.roles[0]; request.roles[3] = healing.roles[0];
    request.roles[4] = mobility.roles[0];
    constexpr std::array<std::string_view, 5> expected{"tavi", "stellar", "heart_of_ruin", "willow", "ren"};
    for (const int size : {3, 4, 5}) {
        auto roster = fixture(); roster.team_size = size;
        for (int i = 0; i < size; ++i) {
            roster.pick_index = i; roster.own_count = static_cast<std::size_t>(i);
            chosen(roster, request, expected[i]);
            const auto decision = choose_draft(roster, request);
            CHECK(std::strcmp(decision.reason, i == 0 ? "required-anchor" : "weighted-public-traits") == 0);
            roster.own_picks[i] = decision.alias;
        }
    }
    context = fixture();
    context.pool[10].known = false;
    rejected(context, request, "unknown-required-anchor");
    context = fixture(); context.pool_count = 10;
    rejected(context, request, "unknown-required-anchor");
    context = fixture(); context.pool[4].known = false;
    chosen(context, production, "vendetta");
    for (auto& traits : context.pool) traits.known = false;
    rejected(context, production, "no-known-legal-candidate");
    context = fixture(); context.pool_count = 0;
    rejected(context, production, "no-known-legal-candidate");
    context = fixture(); context.pool_count = 2; context.own_count = 2; context.pick_index = 2;
    context.own_picks[0] = "ryth"; context.own_picks[1] = "vesper_vale";
    rejected(context, production, "no-known-legal-candidate");
    // Opposing selections cannot affect this selector: only our picks exclude aliases.
    context = fixture(); context.own_count = 1; context.pick_index = 1; context.own_picks[0] = "stellar";
    chosen(context, production, "vendetta");
    context.own_picks[0] = "ren";
    chosen(context, production, "stellar");

    // Pool order never breaks score ties. The exact public alias is the tie key.
    context = fixture();
    const auto tie = role({0, 0, 0, 0, 0});
    chosen(context, tie, "cassi_and_issac");
    std::reverse(context.pool.begin(), context.pool.end());
    chosen(context, tie, "cassi_and_issac");
    chosen(context, production, "stellar");
    chosen(context, survival, "heart_of_ruin");

    for (const int size : {-1, 0, 2, 6}) {
        context = fixture(); context.team_size = size;
        rejected(context, production, "invalid-draft-context");
    }
    for (const int index : {-1, 1, 5}) {
        context = fixture(); context.pick_index = index;
        rejected(context, production, "invalid-draft-context");
    }
    context = fixture(); context.independent_cells = -1;
    rejected(context, production, "invalid-draft-context");
    context = fixture(); context.pool_count = 13;
    rejected(context, production, "invalid-draft-context");
    context = fixture(); context.own_count = 6;
    rejected(context, production, "invalid-draft-context");
    context = fixture(); context.pick_index = 2; context.own_count = 2;
    context.own_picks[0] = context.own_picks[1] = "tavi";
    rejected(context, production, "invalid-draft-context");
    context.own_picks[1] = "cassi";
    rejected(context, production, "invalid-draft-context");
    context = fixture(); context.pool[0].alias = "issac";
    rejected(context, production, "invalid-draft-pool");
    context = fixture(); context.pool[0].alias = context.pool[1].alias;
    rejected(context, production, "invalid-draft-pool");
    context = fixture(); context.pool[3].actors = 1;
    rejected(context, production, "invalid-draft-pool");
    context = fixture(); context.pool[0].actors = 2;
    rejected(context, production, "invalid-draft-pool");
    auto bad_request = production; bad_request.required_anchor = "cassi";
    rejected(fixture(), bad_request, "invalid-draft-request");
    const std::array<double, 3> bad_values{-1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    const std::array<double DraftTraits::*, 5> trait_fields{
        &DraftTraits::productivity, &DraftTraits::health, &DraftTraits::shielding, &DraftTraits::healing, &DraftTraits::mobility};
    for (const auto field : trait_fields) for (const double value : bad_values) {
        context = fixture(); context.pool[0].*field = value;
        rejected(context, production, "invalid-draft-pool");
    }
    const std::array<double DraftWeights::*, 5> weight_fields{
        &DraftWeights::productivity, &DraftWeights::survival, &DraftWeights::healing, &DraftWeights::mobility, &DraftWeights::coverage};
    for (const auto field : weight_fields) for (const double value : bad_values) {
        bad_request = production; bad_request.roles[4].*field = value;
        rejected(fixture(), bad_request, "invalid-draft-request");
    }
    context = fixture(); context.pool[4].productivity = std::numeric_limits<double>::max();
    bad_request = role({2, 0, 0, 0, 0});
    rejected(context, bad_request, "unrepresentable-draft-score");
    context = fixture(); context.pool[11].health = context.pool[11].shielding = std::numeric_limits<double>::max();
    rejected(context, survival, "unrepresentable-draft-score");
    chosen(context, production, "stellar"); // Zero-weight survival never overflows.
    std::printf("Plan5 draft: %d checks passed\n", checks);
}
