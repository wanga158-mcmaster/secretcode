#include "source_intent.h"
#include <limits>
#include "../src/core/catalog.h"

namespace zero::source {
namespace {

InverseResult stop(Status status, Reason reason, std::uint64_t missing = 0)
{
    InverseResult r; r.status = status; r.reason = reason; r.missing_facts = missing; return r;
}

Intent make(ActionType type, std::uint8_t kind, TargetKind target = TargetKind::none, int id = -1)
{
    Intent i; i.type = type; i.argument.kind = kind; i.target_kind = target; i.target_id = id; return i;
}

InverseResult resolved(Intent i, Reason reason = Reason::observed_command)
{
    InverseResult r; r.status = Status::resolved; r.reason = reason;
    r.hypotheses = r.matches = 1; r.intent = i; return r;
}

struct Matches
{
    std::uint32_t hypotheses{}, count{};
    std::optional<Intent> first;
    void add(Intent i) { ++count; if (count == 1) first = i; else first.reset(); }
    InverseResult finish(Reason none, Reason many) const
    {
        auto r = count == 1 ? resolved(*first) : stop(count ? Status::ambiguous : Status::no_match, count ? many : none);
        r.hypotheses = hypotheses; r.matches = count; return r;
    }
};

std::uint64_t inventory_missing(const SourceEvent& e, const InverseFacts& f)
{
    return (!f.before_gold_known ? mf_before_gold : 0ull) | (!f.before_items_known ? mf_before_items : 0ull) |
        (!f.before_consumables_known ? mf_before_consumables : 0ull) | (!e.after_gold_known ? mf_after_gold : 0ull) |
        (!e.after_items_known ? mf_after_items : 0ull) | (!e.after_consumables_known ? mf_after_consumables : 0ull);
}

InverseResult purchase(const SourceEvent& e, const Obs& o, const InverseFacts& f, bool sell)
{
    auto missing = inventory_missing(e, f);
    if (!sell && (!f.consumable_types_known || !f.consumable_cost_known[0] || !f.consumable_cost_known[2]))
        missing |= mf_consumable_rules;
    if (missing) return stop(Status::missing_facts, Reason::missing_source_facts, missing);
    if (e.after_gold < 0 || e.after_gold > std::numeric_limits<std::uint32_t>::max())
        return stop(Status::invalid_event, Reason::invalid_source_field);
    const auto& cat = catalog();
    if (cat.n_items < 0 || cat.n_items > kMaxItems) return stop(Status::unsupported, Reason::unsupported_purchase_rule);
    Matches matches; bool overflow = false, unsupported = false;
    for (int id = 0; id < cat.n_items; ++id)
    {
        const auto& item = cat.item(id); ++matches.hypotheses;
        auto after = o.item_counts;
        std::uint64_t gold = o.self.gold, quantity = 1;
        if (sell)
        {
            if (!after[id]) continue;
            --after[id]; gold += item.cost;
        }
        else
        {
            if (!item.purchasable || e.after_gold >= static_cast<std::int64_t>(gold)) continue;
            if (!item.cost) { unsupported = true; continue; }
            const auto spent = gold - static_cast<std::uint64_t>(e.after_gold);
            if (spent % item.cost) continue;
            quantity = spent / item.cost;
            if (!quantity) continue;
            const auto total = static_cast<std::uint64_t>(after[id]) + quantity;
            if (total > std::numeric_limits<std::uint32_t>::max()) { overflow = true; continue; }
            after[id] = static_cast<std::uint32_t>(total); gold -= quantity * item.cost;
            if (item.combine_count && after[id] >= item.combine_count)
            {
                if (item.result < 0 || item.result >= cat.n_items || item.result == id || cat.item(item.result).combine_count)
                { unsupported = true; continue; }
                const auto combined = after[id] / item.combine_count;
                const auto result_count = static_cast<std::uint64_t>(after[item.result]) + combined;
                if (result_count > std::numeric_limits<std::uint32_t>::max()) { overflow = true; continue; }
                after[id] %= item.combine_count;
                after[item.result] = static_cast<std::uint32_t>(result_count);
            }
        }
        if (after != e.after_items || f.before_consumables != e.after_consumables || gold != static_cast<std::uint64_t>(e.after_gold)) continue;
        auto i = make(sell ? a_sell : a_buy, sell ? static_cast<std::uint8_t>(a_sell) : 1, TargetKind::item, id);
        i.argument.target = id;
        if (!sell) { i.scalar = static_cast<std::int64_t>(quantity); i.scalar_known = true; }
        matches.add(i);
    }
    if (!sell)
    {
        // Item and consumable hypotheses share ONE uniqueness count. Raw slots 1/3 must stay unchanged.
        for (int typed = 0; typed < 2; ++typed)
        {
            const int raw = typed == 0 ? 0 : 2; ++matches.hypotheses;
            if (e.after_consumables[raw] <= f.before_consumables[raw]) continue;
            const auto quantity = static_cast<std::uint64_t>(e.after_consumables[raw]) - f.before_consumables[raw];
            const auto cost = f.consumable_cost[raw];
            if (!cost) { unsupported = true; continue; }
            const auto spent = quantity * cost;
            if (spent > o.self.gold) continue;
            auto after = f.before_consumables;
            after[raw] = e.after_consumables[raw];
            if (after != e.after_consumables || o.item_counts != e.after_items ||
                o.self.gold - spent != static_cast<std::uint64_t>(e.after_gold)) continue;
            auto i = make(a_buy_consumable, a_buy_consumable, TargetKind::consumable, typed);
            i.argument.target = typed; i.scalar = static_cast<std::int64_t>(quantity); i.scalar_known = true;
            matches.add(i);
        }
    }
    auto r = matches.finish(Reason::purchase_no_match, Reason::multiple_purchases);
    if (!matches.count)
    {
        if (unsupported) { r.status = Status::unsupported; r.reason = Reason::unsupported_purchase_rule; }
        else if (overflow) { r.status = Status::overflow; r.reason = Reason::purchase_overflow; }
        else if (e.after_gold == o.self.gold && e.after_items == o.item_counts && e.after_consumables == f.before_consumables)
        { r.status = Status::no_effect; r.reason = Reason::purchase_no_effect; }
    }
    return r;
}

std::uint64_t target_missing(const InverseFacts& f, bool units, bool camps)
{
    return (units && !f.visible_units_complete ? mf_visible_units : 0ull) |
        (camps && !f.visible_camps_complete ? mf_visible_camps : 0ull);
}

InverseResult attack(const SourceEvent& e, const Map& map, const Obs& o, const InverseFacts& f)
{
    auto missing = (!e.to_known ? mf_to : 0ull) | target_missing(f, true, true);
    for (std::size_t a = 0; a < map.altars.size(); ++a)
        if (map.altars[a] == e.to && (a >= f.lock_personally_known.size() || !f.lock_personally_known[a] || a >= o.altar_lock.size()))
            missing |= mf_visible_locks;
    if (missing) return stop(Status::missing_facts, Reason::missing_source_facts, missing);
    Matches matches;
    for (const auto& enemy : o.enemies)
    {
        ++matches.hypotheses;
        if (enemy.hp <= 0.0f || enemy.at != e.to) continue;
        if (enemy.id < 0) return stop(Status::missing_facts, Reason::missing_source_facts, mf_visible_units);
        auto i = make(a_attack, 2, TargetKind::champion, enemy.id);
        i.argument.target = enemy.id; i.argument.at = enemy.at; matches.add(i);
    }
    for (const auto& camp : o.camps)
    {
        ++matches.hypotheses;
        if (camp.hp <= 0.0f || camp.at != e.to) continue;
        if (camp.id < 0) return stop(Status::missing_facts, Reason::missing_source_facts, mf_visible_camps);
        auto i = make(a_attack, 1, TargetKind::camp, camp.id);
        i.argument.target = camp.group; i.argument.at = camp.at; matches.add(i);
    }
    for (std::size_t a = 0; a < map.altars.size(); ++a)
    {
        ++matches.hypotheses;
        if (map.altars[a] != e.to || o.altar_lock[a] != lk_theirs) continue;
        auto i = make(a_attack, 3, TargetKind::altar, static_cast<int>(a));
        i.argument.target = static_cast<std::int32_t>(a); i.argument.at = map.altars[a]; matches.add(i);
    }
    return matches.finish(Reason::target_not_identified, Reason::multiple_targets);
}

InverseResult cast(const SourceEvent& e, const Map& map, const Obs& o, const InverseFacts& f)
{
    if (!e.slot_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_slot);
    const auto& cat = catalog();
    if (e.slot > 1 || o.self.type >= cat.n_champions) return stop(Status::invalid_event, Reason::invalid_ability);
    const auto& ability = cat.champion(o.self.type).ability[e.slot];
    if (ability.kind == ab_none || ability.kind > ab_shield ||
        (e.ability_type_known && e.ability_type != ability.kind)) return stop(Status::invalid_event, Reason::invalid_ability);
    const auto type = e.slot == 0 ? a_cast0 : a_cast1;
    if (ability.kind == ab_reset || ability.kind == ab_shield)
    {
        auto i = make(type, ability.kind, TargetKind::self, o.self.id);
        i.argument.target = o.self.id; i.argument.at = o.self.at; return resolved(i);
    }
    if (ability.kind == ab_dash)
    {
        if (!e.to_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_to);
        if (!map.inside(e.to) || (e.from_known && e.from != o.self.at)) return stop(Status::invalid_event, Reason::invalid_source_field);
        auto i = make(type, ability.kind, TargetKind::tile); i.argument.at = e.to; return resolved(i);
    }
    const bool ground = ability.kind == ab_aoe || (ability.targets & tg_ground);
    const bool self = (ability.targets & tg_self) != 0;
    const bool units = (ability.targets & (tg_enemies | tg_allies)) != 0;
    const bool camps = (ability.targets & tg_camps) != 0;
    auto missing = target_missing(f, units, camps);
    if ((ground || units || camps) && !e.to_known) missing |= mf_to;
    if (missing) return stop(Status::missing_facts, Reason::missing_source_facts, missing);
    Matches matches;
    if (self)
    {
        ++matches.hypotheses;
        auto i = make(type, ability.kind, TargetKind::self, o.self.id);
        i.argument.target = o.self.id; i.argument.at = o.self.at; matches.add(i);
    }
    if (ground)
    {
        ++matches.hypotheses;
        if (!map.inside(e.to)) return stop(Status::invalid_event, Reason::invalid_source_field);
        auto i = make(type, ability.kind, TargetKind::tile); i.argument.at = e.to; matches.add(i);
    }
    auto champion = [&](const UnitView& u)
    {
        ++matches.hypotheses;
        if (u.hp <= 0.0f || u.at != e.to) return;
        if (u.id < 0) { missing |= mf_visible_units; return; }
        auto i = make(type, ability.kind, TargetKind::champion, u.id);
        i.argument.target = u.id; i.argument.at = u.at; matches.add(i);
    };
    if (ability.targets & tg_enemies) for (const auto& u : o.enemies) champion(u);
    if (ability.targets & tg_allies) for (const auto& u : o.allies) champion(u);
    if (camps) for (const auto& camp : o.camps)
    {
        ++matches.hypotheses;
        if (camp.hp <= 0.0f || camp.at != e.to) continue;
        if (camp.id < 0) { missing |= mf_visible_camps; continue; }
        auto i = make(type, ability.kind, TargetKind::camp, camp.id);
        // The executor only tests <=-2 and resolves the real camp by tile. No group is invented.
        i.argument.target = camp.group >= 0 && camp.group <= std::numeric_limits<std::int32_t>::max() - 2 ? -2 - camp.group : -2;
        i.argument.at = camp.at; matches.add(i);
    }
    if (missing) return stop(Status::missing_facts, Reason::missing_source_facts, missing);
    return matches.finish(Reason::target_not_identified, self ? Reason::self_unit_form_ambiguous : Reason::multiple_targets);
}

}  // namespace

InverseResult inverse(const SourceEvent& e, const Map& map, const Obs& o, const InverseFacts& f)
{
    if (e.sequence < 0 || e.input < 1 || e.round < 0) return stop(Status::invalid_event, Reason::invalid_source_field);
    if (e.kind == 26) return stop(Status::exogenous, Reason::fuel_boundary);
    if (e.kind == 23) return stop(Status::ambiguous, Reason::inverse_ambiguous_kind23);
    if (e.kind == 7 || e.kind == 19) return stop(Status::phase_event, Reason::phase_specific_pair);
    switch (e.kind)
    {
    case 4: case 5: case 6: case 8: case 9: case 11: case 12: case 13: case 14: case 16: case 20: case 21: case 25: break;
    default: return stop(Status::unsupported, Reason::unknown_kind);
    }
    if (!f.self_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_self);
    if (e.actor < 0 || e.actor != o.self.id || e.round != o.round) return stop(Status::invalid_event, Reason::source_identity_mismatch);
    switch (e.kind)
    {
    case 8: return resolved(make(a_end, a_end), Reason::observed_end);
    case 11: return resolved(make(a_recall, a_recall));
    case 16: return resolved(make(a_cancel, a_cancel));
    case 9: {
        if (!e.priority_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_priority);
        auto i = make(a_priority, a_priority, TargetKind::priority);
        i.argument.target = 0; i.scalar = e.priority; i.scalar_known = true; return resolved(i);
    }
    case 4: {
        if (!e.to_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_to);
        if (!map.inside(e.to) || (e.from_known && e.from != o.self.at)) return stop(Status::invalid_event, Reason::invalid_source_field);
        auto i = make(a_move, 0, TargetKind::tile); i.argument.at = e.to; return resolved(i);
    }
    case 5: return attack(e, map, o, f);
    case 6: return cast(e, map, o, f);
    case 13: return purchase(e, o, f, false);
    case 25: return purchase(e, o, f, true);
    case 12: {
        if (!e.to_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_to);
        Matches matches;
        for (std::size_t a = 0; a < map.altars.size(); ++a)
        {
            ++matches.hypotheses; if (map.altars[a] != e.to) continue;
            auto i = make(a_tribute, a_tribute, TargetKind::altar, static_cast<int>(a));
            i.argument.target = static_cast<std::int32_t>(a); i.argument.at = e.to; matches.add(i);
        }
        return matches.finish(Reason::target_not_identified, Reason::multiple_targets);
    }
    case 14: {
        if (!e.to_known) return stop(Status::missing_facts, Reason::missing_source_facts, mf_to);
        auto i = make(a_ward, a_ward, TargetKind::tile); i.argument.at = e.to; return resolved(i);
    }
    case 20: case 21: {
        const auto missing = (!e.to_known ? mf_to : 0ull) | (!f.consumable_types_known ? mf_consumable_rules : 0ull);
        if (missing) return stop(Status::missing_facts, Reason::missing_source_facts, missing);
        // Native source vocabulary: 20 = altar-lock, 21 = teleport-channel.
        // SDK typed consumables are the opposite order: crystal=0, lock=1.
        const int typed = e.kind == 20 ? 1 : 0;
        auto i = make(a_use_consumable, a_use_consumable, TargetKind::consumable, typed);
        i.argument.target = typed; i.argument.at = e.to; return resolved(i);
    }
    default: return stop(Status::unsupported, Reason::unknown_kind);
    }
}

bool match_argument(const Intent& i, const Cand& c)
{
    if (i.type >= a_count || c.kind != i.argument.kind) return false;
    switch (i.type)
    {
    case a_end: case a_recall: case a_cancel: case a_priority: return true;
    case a_buy: case a_buy_consumable: case a_sell: return c.target == i.argument.target;
    case a_move: case a_ward: return c.at == i.argument.at;
    case a_use_consumable: case a_tribute: return c.target == i.argument.target && c.at == i.argument.at;
    case a_attack:
        return i.target_kind == TargetKind::champion ? c.target == i.argument.target : c.at == i.argument.at;
    case a_cast0: case a_cast1:
        if (i.target_kind == TargetKind::self || i.target_kind == TargetKind::champion) return c.target == i.argument.target;
        if (i.target_kind == TargetKind::camp) return c.target <= -2 && c.at == i.argument.at;
        return i.target_kind == TargetKind::tile && c.target == -1 && c.at == i.argument.at;
    default: return false;
    }
}

const char* status_name(Status s)
{
    switch (s)
    {
    case Status::resolved: return "resolved"; case Status::exogenous: return "exogenous";
    case Status::phase_event: return "phase_event"; case Status::ambiguous: return "ambiguous";
    case Status::missing_facts: return "missing_facts"; case Status::no_match: return "no_match";
    case Status::no_effect: return "no_effect"; case Status::unsupported: return "unsupported";
    case Status::invalid_event: return "invalid_event"; case Status::overflow: return "overflow";
    }
    return "invalid_status";
}

const char* reason_name(Reason r)
{
    switch (r)
    {
    case Reason::observed_command: return "observed_command"; case Reason::observed_end: return "observed_end";
    case Reason::fuel_boundary: return "fuel_boundary"; case Reason::inverse_ambiguous_kind23: return "inverse_ambiguous_kind23";
    case Reason::phase_specific_pair: return "phase_specific_pair"; case Reason::unknown_kind: return "unknown_kind";
    case Reason::missing_source_facts: return "missing_source_facts"; case Reason::source_identity_mismatch: return "source_identity_mismatch";
    case Reason::invalid_source_field: return "invalid_source_field"; case Reason::target_not_identified: return "target_not_identified";
    case Reason::multiple_targets: return "multiple_targets"; case Reason::invalid_ability: return "invalid_ability";
    case Reason::self_unit_form_ambiguous: return "self_unit_form_ambiguous"; case Reason::purchase_no_match: return "purchase_no_match";
    case Reason::purchase_no_effect: return "purchase_no_effect"; case Reason::multiple_purchases: return "multiple_purchases";
    case Reason::unsupported_purchase_rule: return "unsupported_purchase_rule"; case Reason::purchase_overflow: return "purchase_overflow";
    }
    return "invalid_reason";
}

}  // namespace zero::source
