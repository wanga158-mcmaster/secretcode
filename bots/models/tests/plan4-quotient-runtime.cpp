// SDK-free DTO and full physical-path tests; no engine witness is claimed.
#include "../quotient/src/runtime.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace plan4;
namespace rt = plan4::runtime;
namespace {
int metric(Point a, Point b, const void*) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
struct Fixture {
    std::unique_ptr<Information> info = std::make_unique<Information>();
    Menu menu{};
    std::array<rt::PublicGroup, 8> groups{};
    std::array<rt::PublicMember, 256> public_members{};
    std::array<rt::VisibleMember, 256> members{};
    int group_count = 2, member_count = 2, visible_count = 2;
    bool layout_truncated = false, members_truncated = false;
    rt::CombatKit combat{};
    Fixture() {
        auto& i = *info; i.round = 10; i.own.id = 3; i.own.health = i.own.maximum_health = 1000; i.own.bag = 2;
        i.group_count = 2; i.altar_count = 1; i.travel = {metric, nullptr};
        i.altars[0].id = 90; i.altars[0].position = {5, 0}; i.altars[0].altar_position = {5, 1}; i.altars[0].lock_known = true;
        for (int g = 0; g < 2; ++g) {
            auto& group = i.groups[g]; group.id = g ? 10 : 30; group.position = {g ? 4 : 1, 0};
            group.health = g ? 49 : 26; group.full_health = 100; group.clear_rounds = g ? 5 : 3; group.full_clear_rounds = 10;
            group.gems = 8; group.gold = 4; group.respawn_rounds = 50; group.lifecycle = Lifecycle::living;
            group.visible = group.all_spawns_visible = group.occupancy_known = true;
            group.provenance = group.health_provenance = {10, Source::personal_vision, Status::exact_now};
            groups[g] = {group.id, g, 1, true};
            public_members[g] = {group.id, 100 + g, 0, 1, 0, 3, group.position, 100};
            members[g] = {group.id, 100 + g, 0, 1, group.position, group.health, group.health_provenance};
            menu.jobs[g].kind = JobKind::farm; menu.jobs[g].group = g; menu.jobs[g].altar = 0; menu.jobs[g].menu_rank = g;
        }
        menu.count = menu.single_count = 2;
        combat = {{10, {3}}, 1, 1, 10, 2, 0, 0, 1234, 1, true};
    }
    rt::PublicLayoutFrame layout() const { return {{info->round, {info->own.id}}, groups.data(), group_count, public_members.data(), member_count, layout_truncated}; }
    rt::MemberFrame observed() const { return {members.data(), visible_count, members_truncated}; }
    void feed(rt::Quotient& model) const {
        model.observe_kit(combat.version, combat.stat_epoch); model.observe_combat(combat);
        model.observe_layout(layout()); model.observe_snapshot(*info, observed());
    }
};
bool same(const Forecast& a, const Forecast& b) {
    if (a.event_count != b.event_count || a.macros_started != b.macros_started || a.final_round != b.final_round ||
        a.first_conversion_round != b.first_conversion_round || a.our_secured != b.our_secured || a.enemy_secured != b.enemy_secured ||
        a.final_bag != b.final_bag || a.final_gold != b.final_gold || a.final_health != b.final_health || a.terminal_z != b.terminal_z ||
        a.terminal_unknown != b.terminal_unknown || a.stock_unknown != b.stock_unknown || a.travel_unknown != b.travel_unknown ||
        a.lock_unknown != b.lock_unknown || a.truncated != b.truncated || a.completed_conversion != b.completed_conversion ||
        a.no_conversion != b.no_conversion || a.budget_exhausted != b.budget_exhausted || a.abort_reason != b.abort_reason) return false;
    for (int e = 0; e < a.event_count; ++e) {
        const auto& x = a.events[e]; const auto& y = b.events[e];
        if (x.kind != y.kind || x.start.round != y.start.round || x.start.phase != y.start.phase ||
            x.end.round != y.end.round || x.end.phase != y.end.phase || x.group_id != y.group_id ||
            x.bag_before != y.bag_before || x.bag_after != y.bag_after || x.secured != y.secured ||
            x.gold_after != y.gold_after || x.health_after != y.health_after || x.status != y.status) return false;
    }
    return true;
}
std::unique_ptr<Evaluation> evaluate(rt::Quotient& model, Fixture& f) {
    auto result = std::make_unique<Evaluation>(); rt::evaluate_model(*f.info, f.menu, model, *result); return result;
}
void fresh_rosters_use_full_paths_and_control_is_exact() {
    auto f = std::make_unique<Fixture>(); auto active = std::make_unique<rt::Quotient>(), control = std::make_unique<rt::Quotient>(false);
    f->feed(*active); f->feed(*control); auto a = evaluate(*active, *f), c = evaluate(*control, *f);
    assert(a->evaluated_count == 2 && active->batches() == 1 && active->statistics().classes == 1 && active->statistics().cache_hits == 1);
    assert(active->statistics().full_paths == 2 && !active->statistics().first_macro_only && active->evidence().exact_members == 2);
    assert(a->forecasts[0].macros_started > 1 && active->evidence().unknown_members == 0);
    assert(control->statistics().exact_jobs == 2 && control->statistics().cache_hits == 0 && control->evidence().public_groups == 2);
    for (int n = 0; n < 2; ++n) {
        auto exact = std::make_unique<Forecast>(); predict_into(*f->info, f->menu.jobs[n], f->menu, *exact);
        assert(same(c->forecasts[n], *exact));
    }
    Menu completed = f->menu; assert(a->chosen == choose(*f->info, completed, a->forecasts));
}
void compatibility_batch_reuse_and_source_mutations_cannot_reuse_forecasts() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>(); f->feed(*model);
    auto first = std::make_unique<Forecast>(), second = std::make_unique<Forecast>();
    model->predict_into(*f->info, f->menu.jobs[0], f->menu, *first);
    model->predict_into(*f->info, f->menu.jobs[1], f->menu, *second); assert(model->batches() == 1);
    ++f->info->own.bag;
    model->predict_into(*f->info, f->menu.jobs[0], f->menu, *first);
    assert(model->batches() == 2 && model->evidence().source_mismatch && model->statistics().classes == 0);
    auto exact = std::make_unique<Forecast>(); predict_into(*f->info, f->menu.jobs[0], f->menu, *exact);
    assert(same(*first, *exact));
    model->observe_snapshot(*f->info, f->observed());
    auto fresh = evaluate(*model, *f); assert(fresh->evaluated_count == 2 && model->statistics().classes == 1);
    auto menu = f->menu; menu.jobs[0].second_group = 1;
    model->predict_into(*f->info, menu.jobs[0], menu, *first);
    predict_into(*f->info, menu.jobs[0], menu, *exact); assert(same(*first, *exact));
}
void missing_or_stale_kit_and_public_metadata_mean_exact_fallback() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    model->observe_snapshot(*f->info, f->observed()); auto result = evaluate(*model, *f);
    assert(model->statistics().classes == 0 && model->statistics().exact_jobs == 2 && !model->evidence().combat_current);
    f->feed(*model); model->observe_kit(999, 2); result = evaluate(*model, *f);
    assert(model->statistics().classes == 0 && !model->evidence().combat_current);
    f->combat.version = 999; f->combat.stat_epoch = 2; f->feed(*model);
    result = evaluate(*model, *f); assert(model->statistics().classes == 1);
    auto layout = f->layout(); --layout.clock.round; model->observe_layout(layout);
    result = evaluate(*model, *f); assert(model->statistics().classes == 0 && !model->evidence().layout_valid);
    f->feed(*model); auto combat = f->combat; ++combat.clock.own.id; model->observe_combat(combat);
    result = evaluate(*model, *f); assert(model->statistics().classes == 0 && !model->evidence().combat_current);
}
void missing_members_stay_unknown_and_never_fake_exact_zero_health() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->visible_count = 1; f->feed(*model); auto result = evaluate(*model, *f);
    assert(result->evaluated_count == 2 && model->statistics().classes == 1 && model->statistics().exact_jobs == 1);
    assert(model->evidence().exact_members == 1 && model->evidence().unknown_members == 1);
    f->info->groups[1].health_provenance.status = Status::unknown;
    f->feed(*model); result = evaluate(*model, *f);
    assert(model->statistics().classes == 2 && model->evidence().unknown_members == 1);
    f->visible_count = 2; f->members[1].provenance.status = Status::predicted;
    f->info->groups[1].health_provenance.status = Status::exact_now; f->feed(*model); result = evaluate(*model, *f);
    assert(model->statistics().exact_jobs == 1 && model->evidence().unknown_members == 1);
}
void cleared_groups_require_fresh_queried_dead_member_receipts() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->info->groups[0].lifecycle = Lifecycle::cleared; f->info->groups[0].health = 0;
    f->info->groups[0].reward_available = true; f->members[0].health = 0;
    f->feed(*model); auto result = evaluate(*model, *f);
    assert(model->statistics().classes == 2 && model->statistics().exact_jobs == 0 && model->evidence().exact_members == 2);
    f->members[0] = f->members[1]; f->visible_count = 1;
    f->feed(*model); result = evaluate(*model, *f);
    assert(model->statistics().classes == 1 && model->statistics().exact_jobs == 1 && model->evidence().unknown_members == 1);
}
void prefix_truncation_does_not_invent_unseen_groups_or_erase_valid_rows() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->group_count = f->member_count = f->visible_count = 1; f->layout_truncated = f->members_truncated = true;
    f->feed(*model); auto result = evaluate(*model, *f);
    assert(result->evaluated_count == 2 && model->statistics().classes == 1 && model->statistics().exact_jobs == 1);
    assert(model->evidence().layout_truncated && model->evidence().members_truncated && model->evidence().public_groups == 1);
}
void duplicates_wrong_identity_and_fabricated_public_stats_are_rejected() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->members[2] = f->members[0]; f->visible_count = 3; f->feed(*model); auto result = evaluate(*model, *f);
    assert(model->evidence().malformed_groups == 1 && model->statistics().exact_jobs == 1);
    f = std::make_unique<Fixture>(); f->members[0].spawn.x = 9; f->feed(*model); result = evaluate(*model, *f);
    assert(model->evidence().malformed_groups == 1 && model->statistics().exact_jobs == 1);
    f = std::make_unique<Fixture>(); f->public_members[0].maximum_health = 200; f->feed(*model); result = evaluate(*model, *f);
    assert(model->statistics().exact_jobs == 1); // Source full-health sum is100; public metadata cannot silently replace it.
    f = std::make_unique<Fixture>(); f->public_members[1].member_id = f->public_members[0].member_id;
    f->feed(*model); result = evaluate(*model, *f);
    assert(!model->evidence().layout_valid && model->statistics().classes == 0);
}
void maximum_roster_is_bounded_and_member_enumeration_is_irrelevant() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->member_count = f->visible_count = 64;
    for (int g = 0; g < 2; ++g) {
        f->groups[g] = {f->info->groups[g].id, g * 32, 32, true};
        f->info->groups[g].health = 32; f->info->groups[g].full_health = 3200;
        f->info->groups[g].clear_rounds = 5; f->info->groups[g].full_clear_rounds = 320;
        for (int m = 0; m < 32; ++m) {
            const int index = g * 32 + m; const Point spawn{f->info->groups[g].position.x, m};
            f->public_members[index] = {f->groups[g].group_id, 100 + index, m, 32, m % 4, 3, spawn, 100};
            f->members[index] = {f->groups[g].group_id, 100 + index, m, 32, spawn, 1, {10, Source::personal_vision, Status::exact_now}};
        }
    }
    f->feed(*model); auto original = evaluate(*model, *f);
    assert(original->evaluated_count == 2 && model->evidence().exact_members == 64 && model->statistics().classes == 1);
    std::reverse(f->members.begin(), f->members.begin() + 64);
    std::reverse(f->public_members.begin(), f->public_members.begin() + 32);
    std::reverse(f->public_members.begin() + 32, f->public_members.begin() + 64);
    f->feed(*model); auto reordered = evaluate(*model, *f);
    assert(same(original->forecasts[0], reordered->forecasts[0]) && same(original->forecasts[1], reordered->forecasts[1]));
}
bool deny(const void*) { return false; }
void common_batch_fuel_gate_cannot_publish_a_partial_forecast() {
    auto f = std::make_unique<Fixture>(); auto model = std::make_unique<rt::Quotient>();
    f->info->work_budget = {deny, nullptr}; f->feed(*model); auto result = evaluate(*model, *f);
    assert(result->evaluated_count == 0 && result->chosen == -1 && result->budget_exhausted);
}
void diagnostics_report_actual_class_paths_and_invalidate_with_inputs() {
    auto f=std::make_unique<Fixture>(); auto active=std::make_unique<rt::Quotient>(),control=std::make_unique<rt::Quotient>(false);
    assert(active->model_evidence().reported && !active->model_evidence().supported && !active->model_evidence().coupling_enabled);
    f->feed(*active);f->feed(*control);auto a=evaluate(*active,*f),c=evaluate(*control,*f);
    const auto evidence=active->model_evidence(),disabled=control->model_evidence();
    assert(evidence.supported && evidence.coupling_enabled && evidence.paths==2 && evidence.samples==2 && evidence.work>0 && evidence.revision==1);
    assert(disabled.reported && disabled.supported && !disabled.coupling_enabled && disabled.paths==0 && disabled.samples==2);
    a=evaluate(*active,*f);
    assert(active->model_evidence().work==evidence.work && active->model_evidence().paths==2 && active->model_evidence().revision==2);
    f->feed(*active);
    assert(!active->model_evidence().supported && !active->model_evidence().coupling_enabled && active->model_evidence().paths==0 && active->model_evidence().work==0);
    f->info->work_budget={deny,nullptr}; f->feed(*active);a=evaluate(*active,*f);
    assert(a->budget_exhausted && !active->model_evidence().supported && !active->model_evidence().coupling_enabled && active->model_evidence().paths==0);
    f->info->work_budget={}; f->visible_count=1;f->feed(*active);a=evaluate(*active,*f);
    assert(active->model_evidence().supported && active->model_evidence().unknown>0 && active->model_evidence().paths==1);
    auto stale=f->combat;--stale.clock.round;active->observe_combat(stale);a=evaluate(*active,*f);
    assert(!active->model_evidence().supported && !active->model_evidence().coupling_enabled && active->model_evidence().reason==2);
    f->visible_count=2;f->feed(*active);f->menu.jobs[0].second_group=1;f->menu.count=1;f->menu.single_count=0;a=evaluate(*active,*f);
    assert(active->statistics().exact_jobs==1 && active->statistics().full_paths==1 && active->statistics().classes==0);
    assert(!active->model_evidence().supported && !active->model_evidence().coupling_enabled && active->model_evidence().reason==5 && active->model_evidence().paths==0);
    auto exact=std::make_unique<Forecast>();predict_into(*f->info,f->menu.jobs[0],f->menu,*exact);assert(same(a->forecasts[0],*exact));
}
}
int main() {
    fresh_rosters_use_full_paths_and_control_is_exact();
    compatibility_batch_reuse_and_source_mutations_cannot_reuse_forecasts();
    missing_or_stale_kit_and_public_metadata_mean_exact_fallback();
    missing_members_stay_unknown_and_never_fake_exact_zero_health();
    cleared_groups_require_fresh_queried_dead_member_receipts();
    prefix_truncation_does_not_invent_unseen_groups_or_erase_valid_rows();
    duplicates_wrong_identity_and_fabricated_public_stats_are_rejected();
    maximum_roster_is_bounded_and_member_enumeration_is_irrelevant();
    common_batch_fuel_gate_cannot_publish_a_partial_forecast();
    diagnostics_report_actual_class_paths_and_invalidate_with_inputs();
    std::puts("plan4 quotient runtime: 10 pure scenarios passed (native behavior/fuel validation pending)");
}
