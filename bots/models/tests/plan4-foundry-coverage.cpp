#include "../common/actor-evidence.h"
#include "../common/online-evidence.h"
#include "../common/observer.h"
#include "../common/nav.h"
#include "../common/spatial-evidence.h"
#include "../foundry/src/runtime.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace plan4;
namespace r = plan4::runtime;
namespace cm = cambc::mock;
struct Entry { cambc::map::camp_group group; cambc::vec2 anchor; };
int distance(Point a, Point b, const void*) { return std::max(std::abs(a.x-b.x), std::abs(a.y-b.y)); }

// The original collector-cost cases use representative sealed topology/travel,
// not the native 00rows navigation cache. The separately labeled navigation
// caller case below uses PublicNavigation. All SDK fuel is a synthetic input,
// not measured Wasmtime cost. Collectors, menu, layout, and Foundry are production.
bool walkable(Point p, const void*) { return p.x >= 0 && p.y >= 0 && p.x < 50 && p.y < 50 && p != (Point{1,1}); }
int routes(Point a, Point b, Point* out, int capacity, const void*) {
    const int count = std::min(distance(a, b, nullptr), capacity);
    for (int n = 0; n < count; ++n) {
        Point next{a.x + (b.x > a.x) - (b.x < a.x), a.y + (b.y > a.y) - (b.y < a.y)};
        if (next == (Point{1,1}) && next != b) {
            if (std::abs(b.x-a.x) >= std::abs(b.y-a.y)) next.y = a.y; else next.x = a.x;
        }
        a = next; out[n] = a;
    }
    return count;
}
bool macro_budget(const void* floor) { return cambc::fuel_remaining() >= *static_cast<const unsigned*>(floor); }

// Same SDK projection/order as production_observe_kit. Its surrounding executor
// member include has unrelated action dependencies, so this is explicitly not
// a test of that method's signature/epoch bookkeeping or production_poll.
r::CombatKit projected_kit() {
    const auto& me = cambc::self();
    const int champion = me.champion_name(), range = me.attack_range();
    const float strength = me.strength(), focus = me.focus(); (void)me.max_health();
    return {{cm::round, {0}}, champion, range, strength, focus, 0, 0, 1, 1, true};
}

struct Boundary {
    int fuel, queries, groups, members, kinds, spawns;
    static Boundary now() { return {cm::fuel, cm::queries, cm::group_queries, cm::member_queries,
        cm::kind_accesses, cm::spawn_accesses}; }
};
void print_boundary(const char* label, const char* stage, Boundary b) {
    std::printf("collector-cost case=%s stage=%s fuel=%d queries=%d groupQueries=%d memberQueries=%d kindAccesses=%d spawnAccesses=%d\n",
        label, stage, b.fuel, b.queries, b.groups, b.members, b.kinds, b.spawns);
}

struct CollectorRig {
    std::vector<Entry> entries;
    std::vector<int> lookup = std::vector<int>(2500, -1);
    std::unique_ptr<OnlineObserver> observer = std::make_unique<OnlineObserver>();
    std::unique_ptr<Information> info = std::make_unique<Information>();
    void setup(int groups, int members = 3) {
        cm::reset(); cambc::map::mock_groups.clear(); cm::query_cost = 1; cm::fuel = 200000;
        cm::visible.fill(true); cm::altar_positions = {{1, 1}}; cm::allied_spawns = {{0, 0}};
        for (int n = 1; n <= 4; ++n) cm::actors.push_back({n, {40+n, 10}, true, true, cambc::team::ally});
        for (int n = 5; n <= 8; ++n) {
            cm::actors.push_back({n, {40+n, 40}, true, false, cambc::team::enemy});
            cm::visible[cm::tile({40+n, 40})] = false;
        }
        // Bounded representative layout: twelve distinct three-member groups
        // exercises the eight-source model menu without pretending to be00rows.
        for (int g = 0; g < groups; ++g) {
            std::vector<cambc::camp> camps;
            for (int m = 0; m < members; ++m) {
                const cambc::vec2 p{5+m, 3+3*g};
                camps.emplace_back(g*100+m, p, 0); lookup[cm::tile(p)] = g;
            }
            entries.push_back({cambc::map::camp_group{g, std::move(camps)}, {5, 3+3*g}});
            cambc::map::mock_groups.push_back(entries.back().group);
        }
        assert(observer->observe_into(*info, {distance, nullptr}, 40000));
        assert(!info->observation_truncated && !info->public_layout_truncated && info->group_count == groups);
        print_boundary("warm-public-layout", "observer-cold", Boundary::now());
        ++cm::round; cm::fuel = 100000; cm::queries = cm::group_queries = cm::member_queries = 0;
        cm::kind_accesses = cm::spawn_accesses = 0;
    }
};

int collector_cost(const char* label, int cost, int groups, int post_menu_headroom = -1, int following_cost = 100,
                   bool expect_observer_truncated = false) {
    CollectorRig rig; rig.setup(groups); cm::query_cost = cost;
    auto actors = std::make_unique<r::ActorEvidence>(); actors->start();
    const auto actor_boundary = Boundary::now();
    cm::query_cost = following_cost; // Isolate actor cost from observer/layout cost.
    const auto kit = projected_kit(); const auto kit_boundary = Boundary::now();
    auto online = std::make_unique<r::OnlineEvidence>();
    // Production caller queries width between kit and OnlineEvidence.
    online->start(rig.entries, rig.lookup, cambc::map::width(), 0);
    const auto online_boundary = Boundary::now();
    assert(cm::fuel >= 75000); // Actual macro_decide admission.
    const unsigned floor = std::max(40000u, unsigned(cm::fuel) > 65000 ? unsigned(cm::fuel)-65000 : 40000u);
    assert(rig.observer->observe_into(*rig.info, {distance, nullptr}, floor));
    auto& info = *rig.info; info.work_budget = {macro_budget, &floor};
    const auto observer_boundary = Boundary::now();
    const auto menu = make_menu(info); const auto menu_boundary = Boundary::now();
    assert(info.group_count == groups && info.altar_count == 1 && !info.public_layout_truncated);
    assert(info.own.id == 0 && info.own.position == (Point{0, 0}) && info.own.health == 100);
    assert(menu.single_count > 0 && menu.single_count <= 8 && menu.count >= menu.single_count);
    assert(actors->frame().clock.round == cm::round && actors->frame().clock.own.id == 0);
    assert(actors->frame().roster_complete && actors->frame().own_observed);
    assert(actors->frame().allied_count == 5 && actors->frame().enemy_count == 4);
    if (!actors->frame().truncated) assert(actors->frame().count == 4);
    assert(online->members().count == 3 && !online->members().truncated && !online->producers().truncated);
    for (int m = 0; m < online->members().count; ++m) {
        const auto& sample = online->members().samples[m];
        assert(sample.group_id == 0 && sample.health == 0 && sample.ordinal == m);
        assert(sample.provenance.observed_round == cm::round && sample.provenance.status == Status::exact_now);
    }
    // A controlled residual headroom sweep isolates Spatial-before-layout.
    // This debit represents an input boundary, not measured navigation work.
    if (post_menu_headroom >= 0) {
        assert(cm::fuel >= int(floor) + post_menu_headroom);
        cm::fuel = int(floor) + post_menu_headroom;
    }
    assert(cm::fuel >= int(floor) + 6000); // Actual pre-Spatial admission.
    const auto spatial_before = Boundary::now();
    auto spatial = std::make_unique<r::SpatialEvidence>();
    spatial->start(info, menu, {50,50,true,walkable,nullptr}, std::vector<cambc::camp>{},
        std::vector<cambc::champion>{}, routes, nullptr, 192, floor);
    const auto spatial_boundary = Boundary::now();
    const auto layout = rig.observer->layout_for_menu(info, menu); const auto layout_boundary = Boundary::now();
    assert(spatial->frame().clock.round == cm::round && spatial->frame().clock.own.id == 0);
    assert(spatial->frame().information == &info && spatial->frame().menu == &menu && spatial->frame().current_complete);
    if (!layout.truncated) {
        assert(layout.group_count == menu.single_count && layout.member_count == menu.single_count * 3);
        for (int g = 0; g < layout.group_count; ++g) {
            assert(layout.groups[g].complete && layout.groups[g].count == 3);
            for (int m = 0; m < 3; ++m) {
                const auto& member = layout.members[layout.groups[g].first + m];
                assert(member.group_id == layout.groups[g].group_id && member.ordinal == m && member.declared_members == 3);
                assert(member.kind == 0 && member.maximum_health == 100);
            }
        }
    }
    auto model = std::make_unique<r::Foundry>();
    model->observe_actors(actors->frame()); model->observe_producers(online->producers());
    model->observe_combat(kit); model->observe_spatial(spatial->frame()); model->observe_layout(layout);
    model->observe_snapshot(info, online->members());
    const int before_evaluation_queries = cm::queries;
    auto actual = std::make_unique<Evaluation>(); r::evaluate_model(info, menu, *model, *actual);
    const int mask = model->truncation_mask();
    const int expected = (menu.truncated ? 2 : 0) | (layout.truncated ? 4 : 0) | (actors->frame().truncated ? 16 : 0);
    assert(mask == expected && !cm::unauthorized && cm::queries == before_evaluation_queries);
    if (mask) {
        assert(model->unsupported() == r::Foundry::Unsupported::truncated && !model->used_foundry());
        auto fallback = std::make_unique<Evaluation>(); evaluate_into(info, menu, *fallback);
        assert(actual->evaluated_count == fallback->evaluated_count && actual->chosen == fallback->chosen);
        assert(actual->menu.count == menu.count && actual->budget_exhausted == fallback->budget_exhausted);
        for (int n = 0; n < actual->evaluated_count; ++n) {
            assert(actual->forecasts[n].our_secured == fallback->forecasts[n].our_secured);
            assert(actual->forecasts[n].first_conversion_round == fallback->forecasts[n].first_conversion_round);
            assert(actual->forecasts[n].final_bag == fallback->forecasts[n].final_bag);
            assert(actual->forecasts[n].truncated == fallback->forecasts[n].truncated);
        }
    }
    print_boundary(label, "actors", actor_boundary); print_boundary(label, "kit-projection", kit_boundary);
    print_boundary(label, "online", online_boundary); print_boundary(label, "observer", observer_boundary);
    print_boundary(label, "menu", menu_boundary); print_boundary(label, "spatial-before", spatial_before);
    print_boundary(label, "spatial", spatial_boundary); print_boundary(label, "layout", layout_boundary);
    std::printf("collector-cost case=%s actorCost=%d followingCost=%d actorCount=%d actorTruncated=%d observerTruncated=%d menuTruncated=%d layoutTruncated=%d mask=%d hiddenQueries=%d\n",
        label, cost, following_cost, actors->frame().count, actors->frame().truncated, info.observation_truncated,
        menu.truncated, layout.truncated, mask, cm::unauthorized);
    assert(info.observation_truncated == expect_observer_truncated);
    return mask;
}

void golem_metadata() {
    cm::reset(); cm::query_cost = 25; cm::visible.fill(true);
    cm::altar_positions = {{1,1}}; cm::allied_spawns = {{0,0}};
    cambc::map::mock_groups = {{0, {{100,{5,5},210,47,cambc::camp_type::golem},
                                  {101,{6,5},190,47,cambc::camp_type::golem}}}};
    cm::states[0] = 0;
    auto observer = std::make_unique<OnlineObserver>(); auto info = std::make_unique<Information>();
    assert(observer->observe_into(*info, {distance,nullptr}, 40000));
    assert(info->group_count == 1 && !info->public_layout_truncated && !info->observation_truncated);
    assert(info->groups[0].full_health == 650 && info->groups[0].health == 400);
    assert(info->groups[0].full_incoming_damage == 94 && info->groups[0].incoming_damage == 94);
    assert(cm::base_stats_queries == 1 && cm::stats_by_kind[5] == 1 && cm::stats_by_kind[0] == 0);
    const auto menu = make_menu(*info); const int before_layout = cm::queries;
    auto layout = observer->layout_for_menu(*info, menu);
    assert(!layout.truncated && layout.group_count == 1 && layout.member_count == 2);
    for (int m = 0; m < 2; ++m) {
        const auto& member = layout.members[m];
        assert(member.kind == 5 && member.maximum_health == 325 && member.attack_range == 4);
        assert(member.member_id == 100+m && member.ordinal == m && member.declared_members == 2);
        assert(member.spawn == (Point{5+m,5}));
    }
    assert(cm::queries == before_layout && !cm::unauthorized);
    ++cm::round; cm::fuel = 100000; cm::camp_health_overrides = {{100,123},{101,0}};
    const int before_dynamic = cm::member_queries;
    assert(observer->observe_into(*info, {distance,nullptr}, 40000));
    assert(info->groups[0].full_health == 650 && info->groups[0].health == 123);
    assert(info->groups[0].incoming_damage == 47 && cm::member_queries > before_dynamic);
    assert(info->groups[0].health_provenance.observed_round == cm::round &&
        info->groups[0].health_provenance.status == Status::exact_now);
    assert(cm::base_stats_queries == 1 && cm::stats_by_kind[5] == 1 && !cm::unauthorized);
    const auto warm_menu = make_menu(*info); const auto warm_layout = observer->layout_for_menu(*info, warm_menu);
    assert(!warm_layout.truncated && warm_layout.member_count == 2 && warm_layout.members[0].maximum_health == 325);
    // Hiding the entire group must suppress dynamic reads while preserving
    // the immutable Golem composition and reverting HP to a public quote.
    ++cm::round; cm::fuel = 100000; cm::visible[cm::tile({5,5})] = cm::visible[cm::tile({6,5})] = false;
    const int hidden_groups = cm::group_queries, hidden_members = cm::member_queries;
    assert(observer->observe_into(*info, {distance,nullptr}, 40000));
    assert(info->groups[0].full_health == 650 && info->groups[0].health == 650 && !info->groups[0].visible);
    assert(cm::group_queries == hidden_groups && cm::member_queries == hidden_members && !cm::unauthorized);
    assert(cm::base_stats_queries == 1);
    std::puts("golem-metadata: cold/warm kind5 stats cached once; live HP refreshed; hidden dynamic reads0");
    for (const int invalid : {-1,6}) {
        cm::reset(); cm::query_cost = 25;
        cambc::map::mock_groups = {{0, {{100,{5,5},100,20,static_cast<cambc::camp_type>(invalid)}}}};
        observer = std::make_unique<OnlineObserver>();
        for (int attempt = 0; attempt < 2; ++attempt) {
            cm::fuel = 100000; ++cm::round;
            assert(!observer->observe_into(*info, {distance,nullptr}, 40000));
            assert(info->group_count == 0 && info->public_layout_truncated && info->observation_truncated);
            assert(cm::base_stats_queries == 0 && cm::member_queries == 0 && !cm::unauthorized);
        }
    }
    std::puts("invalid-camp-kind: -1/6 rejected before design-array access on first/repeated initialization");
}

void observer_packet_boundaries() {
    cm::reset();cm::query_cost=25;cm::fuel=200000;cm::visible.fill(true);
    cm::altar_positions={{1,1}};cm::allied_spawns={{0,0}};cm::states[0]=0;
    auto& own=cm::actor(0);own.position={2,2};own.hp=73;own.maximum=140;
    own.strength=32;own.focus=7;own.bag=61;own.gold=19;own.monster_items=own.jungle_items=1;
    own.state_bits=cambc::champion_state::recalling.bits()|cambc::champion_state::submitting_gems.bits()
        |cambc::champion_state::teleporting.bits();own.channel_ticks=3;
    cambc::map::mock_groups={{0,{{100,{5,5},80,17},{101,{6,5},0,23}}}};
    auto observer=std::make_unique<OnlineObserver>();auto info=std::make_unique<Information>();
    // These are the exact six old SDK info getters and the two old living
    // camp getters. Query units compare the wrapper packets, not native fuel.
    const auto& me=cambc::self();const int before_own=cm::queries;
    const auto old_position=me.position();const float old_health=me.health(),old_maximum=me.max_health();
    const auto old_state=me.state();const float old_strength=me.strength(),old_focus=me.focus();
    const int own_getter_queries=cm::queries-before_own;
    const auto& camp=cambc::map::mock_groups[0].camps()[0];const int before_camp=cm::queries;
    const float old_camp_health=camp.health(),old_camp_strength=camp.strength();
    const int camp_getter_queries=cm::queries-before_camp;
    const int raw_before=cm::raw_queries,camp_raw_before=cm::camp_raw_queries;
    const int state_before=cm::camp_state_queries,members_before=cm::member_queries;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(own_getter_queries==6 && camp_getter_queries==2);
    assert(cm::raw_queries==raw_before+1 && cm::camp_raw_queries==camp_raw_before+1);
    assert(cm::camp_state_queries==state_before+2 && cm::member_queries==members_before+3);
    assert(info->own.position==(Point{old_position.x,old_position.y}) && info->own.health==old_health);
    assert(info->own.maximum_health==old_maximum && info->own.bag==61 && info->own.gold==19);
    assert(old_state.contains(cambc::champion_state::recalling) && info->own.channel==Channel::recall && info->own.channel_ticks==3);
    const float dps=(old_strength*1.125f+old_focus*.12f)*1.1f;
    assert(info->groups[0].health==old_camp_health && info->groups[0].incoming_damage==old_camp_strength*.9f);
    assert(info->groups[0].clear_damage_per_round==dps && info->groups[0].clear_rounds==clear_quote(old_camp_health,dps));
    assert(info->groups[0].full_health==200 && info->groups[0].full_incoming_damage==40*.9f);
    assert(info->groups[0].health_provenance.status==Status::exact_now && !info->observation_truncated && !cm::unauthorized);
    int cases=1;
    const auto real_menu=[&] {
        const int queries=cm::queries;const auto menu=make_menu(*info);
        assert(cm::queries==queries && !menu.truncated && menu.single_count==1 && menu.count>=4);
        std::array<bool,4> kinds{};
        for(int n=0;n<menu.count;++n){kinds[int(menu.jobs[n].kind)]=true;assert(menu.jobs[n].menu_rank==n);}
        for(const bool present:kinds)assert(present); // farm, bank, renew and hold all remain real alternatives.
        return menu;
    };
    const auto cold_menu=real_menu();
    assert(cold_menu.jobs[0].kind==JobKind::farm && cold_menu.jobs[0].group==0);
    // Reuse the observer after all packet fields change. No packet survives
    // this boundary; group lifecycle remains a separate fresh SDK query.
    ++cm::round;cm::fuel=200000;own.position={3,2};own.hp=48;own.maximum=141;
    own.strength=44;own.focus=9;own.bag=62;own.gold=20;own.channel_ticks=4;
    own.state_bits=cambc::champion_state::submitting_gems.bits()|cambc::champion_state::teleporting.bits();
    cm::camp_health_overrides={{100,35},{101,30}};cm::camp_strength_overrides={{100,11},{101,13}};
    const int warm_own=cm::raw_queries,warm_camp=cm::camp_raw_queries,warm_groups=cm::group_queries;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(cm::raw_queries==warm_own+1 && cm::camp_raw_queries==warm_camp+2 && cm::group_queries==warm_groups+1);
    assert(info->own.position==(Point{3,2}) && info->own.health==48 && info->own.maximum_health==141);
    assert(info->own.bag==62 && info->own.gold==20 && info->own.channel==Channel::tribute && info->own.channel_ticks==4);
    assert(info->groups[0].health==65 && info->groups[0].incoming_damage==24*.9f);
    assert(info->groups[0].clear_damage_per_round==(44*1.125f+9*.12f)*1.1f);
    assert(info->groups[0].health_provenance.observed_round==cm::round && !cm::unauthorized);real_menu();++cases;
    // A dead body is settled by state(), before a raw camp packet is allowed.
    ++cm::round;cm::fuel=200000;cm::camp_health_overrides={{100,0},{101,0}};
    cm::camp_raw_error_id=100;cm::camp_raw_error=43;cm::camp_raw_error_payload=true;
    const int dead_packets=cm::camp_raw_queries,dead_states=cm::camp_state_queries;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(cm::camp_raw_queries==dead_packets && cm::camp_state_queries==dead_states+2);
    assert(info->groups[0].health==0 && info->groups[0].incoming_damage==0);real_menu();++cases;
    for(const int code:{43,97}) {
        ++cm::round;cm::fuel=200000;cm::camp_health_overrides={{100,45},{101,0}};cm::camp_raw_error=code;
        const int errors=cm::camp_raw_queries;bool threw=false;
        try { observer->observe_into(*info,{distance,nullptr},40000); } catch(const std::runtime_error&) { threw=true; }
        assert(threw && cm::camp_raw_queries==errors+1 && !cm::unauthorized);
        assert(info->groups[0].health==200 && info->groups[0].incoming_damage==40*.9f);++cases;
    }
    // Hidden groups never reach lifecycle, dead-state or raw-packet imports,
    // including when a deliberately poisoned packet hook remains armed.
    ++cm::round;cm::fuel=200000;cm::visible[cm::tile({5,5})]=cm::visible[cm::tile({6,5})]=false;
    const int hidden_groups=cm::group_queries,hidden_states=cm::camp_state_queries,hidden_packets=cm::camp_raw_queries;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(cm::group_queries==hidden_groups && cm::camp_state_queries==hidden_states && cm::camp_raw_queries==hidden_packets);
    assert(!info->groups[0].visible && info->groups[0].health==200 && info->groups[0].health_provenance.status==Status::unknown);
    assert(!cm::unauthorized);real_menu();++cases;
    for(const int code:{43,97}) {
        ++cm::round;cm::fuel=200000;cm::raw_error_actor=0;cm::raw_error=code;cm::raw_error_payload=true;
        const int errors=cm::raw_queries,previous_camps=cm::camp_raw_queries;bool threw=false;
        try { observer->observe_into(*info,{distance,nullptr},40000); } catch(const std::runtime_error&) { threw=true; }
        assert(threw && cm::raw_queries==errors+1 && cm::camp_raw_queries==previous_camps);
        assert(info->own.position==(Point{}) && info->own.health==0 && info->own.maximum_health==0 && !info->group_count);++cases;
    }
    cm::raw_error=cm::camp_raw_error=0;cm::raw_error_actor=cm::camp_raw_error_id=-1;
    ++cm::round;cm::fuel=200000;cm::visible.fill(true);own.state_bits=cambc::champion_state::teleporting.bits();
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(info->own.channel==Channel::other && info->own.channel_ticks==4 && info->groups[0].health==45);
    assert(!info->observation_truncated && !cm::unauthorized);real_menu();++cases;
    ++cm::round;cm::fuel=200000;own.alive=false;own.state_bits=0;
    const int dead_own=cm::raw_queries;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(cm::raw_queries==dead_own+1 && !info->own.alive && info->own.health==48);
    assert(info->own.channel==Channel::none && info->own.channel_ticks==0 && !info->own.on_spawn && !info->own.recall_safe);
    assert(!cm::unauthorized);real_menu();++cases;
    std::printf("observer-packet-boundaries: %d cases; ownGetterQueries=%d ownPackets=1 livingCampGetterQueries=%d livingCampPackets=1; real menu alternatives retained; synthetic SDK only\n",
        cases,own_getter_queries,camp_getter_queries);
}

void menu_work_admission() {
    CollectorRig rig; rig.setup(4);
    const unsigned floor = 40000; rig.info->work_budget = {macro_budget, &floor};
    cm::fuel = 39999; const auto menu = make_menu(*rig.info);
    assert(menu.truncated && !rig.info->observation_truncated);
    // make_menu can truncate through its own work guard. With the actual
    // monotone fuel predicate, the later6k caller admission then skips Spatial,
    // layout and Foundry, so this fixture must not manufacture a mask row.
    assert(cm::fuel < int(floor)+6000 && !cm::unauthorized);
    std::puts("menu-work-admission: own work guard truncates; actual caller skips evaluation; no mask row");
}

r::VisibleActor old_getter_actor(const cambc::champion& actor, bool own, bool ally) {
    r::VisibleActor value;
    if (!own && !actor.is_visible()) return value;
    value.id = int(static_cast<cambc::handle_champion_t>(actor)); value.ally = ally;
    value.provenance = {cm::round, own ? Source::own_state : Source::personal_vision, Status::exact_now};
    value.alive = actor.is_alive(); if (!value.alive) return {};
    const auto p = actor.position(); value.position = {p.x,p.y}; value.champion = actor.champion_name();
    value.health = actor.health(); value.maximum_health = actor.max_health(); value.strength = actor.strength();
    value.attack_range = actor.attack_range(); value.bag = actor.gem_count();
    value.current_priority = actor.prev_priority(); value.next_priority = actor.priority();
    value.completed_turn = actor.has_completed_turn();
    return value;
}
r::ActorFrame old_getter_frame() {
    r::ActorFrame frame; frame.clock = {cm::round,{0}};
    const auto allies = cambc::champions(cambc::team::ally), enemies = cambc::champions(cambc::team::enemy);
    frame.allied_count = int(allies.size()); frame.enemy_count = int(enemies.size());
    for (int n = 0; n < frame.allied_count; ++n) frame.allied_roster[n] = int(static_cast<cambc::handle_champion_t>(allies[n]));
    for (int n = 0; n < frame.enemy_count; ++n) frame.enemy_roster[n] = int(static_cast<cambc::handle_champion_t>(enemies[n]));
    frame.roster_complete = true; frame.own = old_getter_actor(cambc::self(),true,true); frame.own_observed = true;
    for (int side = 0; side < 2; ++side) for (const auto& actor : side ? enemies : allies) {
        if (int(static_cast<cambc::handle_champion_t>(actor)) == 0) continue;
        const auto value = old_getter_actor(actor,false,side==0);
        if (value.id >= 0 && value.alive) frame.samples[frame.count++] = value;
    }
    return frame;
}
void same_actor(const r::VisibleActor& a, const r::VisibleActor& b) {
    assert(a.id == b.id && a.draft_ordinal == b.draft_ordinal && a.champion == b.champion && a.attack_range == b.attack_range);
    assert(a.position == b.position && a.health == b.health && a.maximum_health == b.maximum_health && a.strength == b.strength);
    assert(a.bag == b.bag && a.current_priority == b.current_priority && a.next_priority == b.next_priority);
    assert(a.ally == b.ally && a.alive == b.alive && a.completed_turn == b.completed_turn);
    assert(a.provenance.observed_round == b.provenance.observed_round && a.provenance.source == b.provenance.source &&
        a.provenance.status == b.provenance.status);
}
void packet_boundaries() {
    auto setup = [] {
        cm::reset(); cm::query_cost = 25;
        for (int n = 1; n <= 4; ++n)
            cm::actors.push_back({n,{n+3,4},true,true,cambc::team::ally,unsigned(20+n),float(70+n),120,float(17+n),3,9,4,true});
        for (int n = 5; n <= 8; ++n) cm::actors.push_back({n,{n+3,10},true,false,cambc::team::enemy,777});
    };
    int cases = 0; setup(); const auto expected = old_getter_frame(); const int old_queries = cm::queries;
    setup(); auto actors = std::make_unique<r::ActorEvidence>(); actors->start();
    const auto& actual = actors->frame(); const int packet_queries = cm::queries;
    assert(actual.clock.round == expected.clock.round && actual.clock.own.id == expected.clock.own.id);
    assert(actual.allied_count == expected.allied_count && actual.enemy_count == expected.enemy_count);
    assert(actual.allied_roster == expected.allied_roster && actual.enemy_roster == expected.enemy_roster);
    assert(actual.count == expected.count && actual.roster_complete == expected.roster_complete && actual.own_observed == expected.own_observed);
    assert(actual.truncated == expected.truncated && actual.draft_order_known == expected.draft_order_known);
    same_actor(actual.own,expected.own); for (int n = 0; n < actual.count; ++n) same_actor(actual.samples[n],expected.samples[n]);
    assert(old_queries == 65 && packet_queries == 36 && cm::raw_queries == 9 && !cm::unauthorized); ++cases;
    ++cm::round; cm::fuel = 100000; cm::actor(1).hp = 19; cm::actor(1).bag = 91; actors->start();
    assert(actors->frame().samples[0].health == 19 && actors->frame().samples[0].bag == 91);
    assert(actors->frame().samples[0].provenance.observed_round == cm::round && !actors->frame().truncated); ++cases;
    // A failed visibility packet may contain arbitrary bytes;97 must veto it.
    setup(); cm::raw_error_actor = 1; cm::raw_error = 97; cm::raw_error_payload = true;
    actors = std::make_unique<r::ActorEvidence>(); actors->start();
    assert(actors->frame().count == 3 && !actors->frame().truncated && !cm::unauthorized); ++cases;
    for (const int code : {43,97}) {
        setup(); cm::raw_error_actor = code == 43 ? 1 : 0; cm::raw_error = code;
        actors = std::make_unique<r::ActorEvidence>(); bool threw = false;
        try { actors->start(); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && !cm::unauthorized); ++cases;
    }
    setup(); cm::negative_actor = 1; actors = std::make_unique<r::ActorEvidence>(); actors->start();
    assert(actors->frame().count == 3 && !actors->frame().truncated && !cm::unauthorized); ++cases;
    setup(); cm::actor(1).alive = false; actors = std::make_unique<r::ActorEvidence>(); actors->start();
    assert(actors->frame().count == 3 && !actors->frame().truncated && !cm::unauthorized); ++cases;
    setup(); cm::actor(0).alive = false; actors = std::make_unique<r::ActorEvidence>(); actors->start();
    assert(!actors->frame().own_observed && actors->frame().count == 0 && cm::raw_queries == 0 && !cm::unauthorized); ++cases;
    for (int cut = 1; cut <= packet_queries; ++cut) {
        setup(); cm::stop_after = cut; actors = std::make_unique<r::ActorEvidence>(); actors->start();
        assert(actors->frame().truncated && !cm::unauthorized);
        for (int n = 0; n < actors->frame().count; ++n) same_actor(actors->frame().samples[n],expected.samples[n]);
        ++cases;
        setup(); cm::shift_after = cut; actors = std::make_unique<r::ActorEvidence>(); actors->start();
        assert(actors->frame().truncated && actors->frame().count == 0 && !actors->frame().own_observed && !cm::unauthorized); ++cases;
    }
    std::printf("actor-packet-boundaries: %d cases; identical complete frame oldGetterQueries=%d packetQueries=%d; synthetic SDK only\n",
        cases, old_queries, packet_queries);
}

void collect_and_bind(bool fifth_visible) {
    cm::reset(); cm::query_cost = 1; // Separate the physical producer cap from synthetic fuel costs.
    for (int n = 1; n <= 4; ++n) cm::actors.push_back({n, {10+n, 10}, true, true, cambc::team::ally});
    for (int n = 5; n <= 8; ++n)
        cm::actors.push_back({n, {10+n, 11}, true, fifth_visible && n == 5, cambc::team::enemy});
    cm::visible[cm::tile({5, 5})] = true;
    std::vector<Entry> entries{{cambc::map::camp_group{0, {{100, {5, 5}, 0}}}, {5, 5}}};
    std::vector<int> lookup(2500, -1); lookup[cm::tile({5, 5})] = 0;
    auto actors = std::make_unique<r::ActorEvidence>(); actors->start();
    auto online = std::make_unique<r::OnlineEvidence>(); online->start(entries, lookup, 50, 0);
    assert(actors->frame().roster_complete && actors->frame().own_observed && !actors->frame().truncated);
    assert(actors->frame().allied_count == 5 && actors->frame().enemy_count == 4);
    assert(actors->frame().count == 4 + int(fifth_visible));
    assert(online->producers().count == 4 && online->producers().truncated == fifth_visible);
    assert(online->members().count == 1 && online->members().truncated == fifth_visible);

    auto info = std::make_unique<Information>(); info->round = cm::round; info->settings.max_rounds = 80;
    info->own.id = 0; info->own.position = {0, 0}; info->own.health = info->own.maximum_health = 100;
    info->group_count = 1; info->altar_count = 1; info->travel = {distance, nullptr};
    info->altars[0].id = 9; info->altars[0].position = {0, 0}; info->altars[0].altar_position = {0, 1};
    info->altars[0].lock_known = true;
    auto& group = info->groups[0]; group.id = 0; group.position = {5, 5};
    group.health = 0; group.full_health = 100; group.gems = 24; group.gold = 10;
    group.clear_rounds = 0; group.full_clear_rounds = 1; group.respawn_rounds = 30;
    group.lifecycle = Lifecycle::cleared; group.reward_available = true;
    group.visible = group.all_spawns_visible = group.occupancy_known = true;
    group.provenance = group.health_provenance = {cm::round, Source::personal_vision, Status::exact_now};
    const auto menu = make_menu(*info); assert(menu.single_count > 0 && menu.count > 0 && !menu.truncated);
    const r::ObservationClock clock{cm::round, {0}};
    r::CombatKit kit; kit.clock = clock; kit.strength = 10; kit.attack_range = 1; kit.exact = true;
    r::SpatialFrame spatial; spatial.clock = clock; spatial.information = info.get(); spatial.menu = &menu;
    // A later independent geometry rejection distinguishes a clear truncation
    // guard from a supported forecast, without inventing spatial evidence.
    spatial.current_complete = false;
    const r::PublicGroup layout{0, 0, 1, true};
    const r::PublicMember member{0, 100, 0, 1, 0, 1, {5, 5}, 100};
    auto model = std::make_unique<r::Foundry>();
    const int collected_queries = cm::queries;
    model->observe_actors(actors->frame()); model->observe_producers(online->producers());
    model->observe_combat(kit); model->observe_spatial(spatial);
    model->observe_layout({clock, &layout, 1, &member, 1, false});
    model->observe_snapshot(*info, online->members());
    assert(model->truncation_mask() == -1);
    auto actual = std::make_unique<Evaluation>(); r::evaluate_model(*info, menu, *model, *actual);
    const int expected_mask = fifth_visible ? 40 : 0;
    assert(model->truncation_mask() == expected_mask && !model->used_foundry());
    assert(model->unsupported() == (fifth_visible ? r::Foundry::Unsupported::truncated : r::Foundry::Unsupported::geometry));
    auto expected = std::make_unique<Evaluation>(); plan4::evaluate_into(*info, menu, *expected);
    assert(actual->evaluated_count == expected->evaluated_count && actual->chosen == expected->chosen);
    assert(actual->budget_exhausted == expected->budget_exhausted && actual->menu.count == menu.count);
    for (int n = 0; n < actual->evaluated_count; ++n) {
        const auto& a = actual->forecasts[n]; const auto& b = expected->forecasts[n];
        assert(a.our_secured == b.our_secured && a.first_conversion_round == b.first_conversion_round);
        assert(a.final_bag == b.final_bag && a.truncated == b.truncated && a.terminal_unknown == b.terminal_unknown);
    }
    model->selected(*info, menu.jobs[0], 1, false);
    for (int n = 0; n < 8; ++n) assert(model->truncation_mask() == expected_mask);
    assert(!model->model_evidence().supported && cm::queries == collected_queries && !cm::unauthorized);
}

struct NavigationCallerResult {
    int single_count = 0, candidates = 0;
    bool routes_complete = false;
};

NavigationCallerResult navigation_caller(int navigation_headroom, bool expect_deferral = false) {
    cm::reset(); cm::query_cost = 100; cm::visible.fill(true);
    cm::altar_positions = {{1,4}}; cm::allied_spawns = {{0,0}};
    cambc::map::mock_groups = {{0, {{100,{5,3},0}}}};
    std::vector<Entry> entries{{cambc::map::mock_groups[0], {5,3}}};
    std::vector<int> lookup(2500, -1); lookup[cm::tile({5,3})] = 0;
    auto navigation = std::make_unique<PublicNavigation>();
    auto observer = std::make_unique<OnlineObserver>();
    auto info = std::make_unique<Information>();
    NavigationCallerResult result;
    const unsigned optional_floor = 40000, phase_allowance = 15000;
    // These are synthetic stage inputs. Resetting fuel only at the turn start
    // and the labeled post-observer boundary isolates navigation admission;
    // it does not calibrate native costs or claim this collection costs 52.5k.
    // The 50k navigation minimum preserves planning reserve above execution's
    // 40k floor. Low inputs must defer safely; 63.5k tests productive admission.
    // One prepare per turn, four turns, persistent initialization/search state.
    for (int attempt = 0; attempt < 4; ++attempt) {
        cm::fuel = 100000; ++cm::round; navigation->begin_turn();
        auto actors = std::make_unique<r::ActorEvidence>(); actors->start();
        const auto kit = projected_kit();
        auto online = std::make_unique<r::OnlineEvidence>();
        online->start(entries, lookup, cambc::map::width(), 0);
        assert(observer->observe_into(*info, navigation->lookup(), optional_floor));
        assert(!info->observation_truncated && !info->public_layout_truncated);
        assert(info->group_count == 1 && info->altar_count == 1 && info->altars[0].accessible);
        const auto& group = info->groups[0];
        assert(group.visible && group.all_spawns_visible && group.health == 0 && group.reward_available);
        assert(group.lifecycle == Lifecycle::cleared && group.health_provenance.observed_round == cm::round &&
            group.health_provenance.source == Source::personal_vision && group.health_provenance.status == Status::exact_now);
        info->work_budget = {macro_budget, &optional_floor};
        assert(cm::fuel >= navigation_headroom);
        cm::fuel = navigation_headroom; // Explicit post-observer scope, not a collector or navigation debit.
        const unsigned nav_before = cambc::fuel_remaining();
        const unsigned nav_floor = std::max(optional_floor, nav_before > phase_allowance ? nav_before-phase_allowance : optional_floor);
        NavigationPhaseBudget policy; policy.begin(nav_before, nav_floor, phase_allowance);
        navigation->prepare(*info, nav_floor, phase_allowance); navigation->seal();
        const int nav_after = cm::fuel;
        const auto menu = make_menu(*info); // Keep every real alternative, including BANK/RENEW/HOLD.
        int farms = 0, banks = 0, renews = 0, holds = 0;
        for (int n = 0; n < menu.count; ++n) {
            farms += menu.jobs[n].kind == JobKind::farm; banks += menu.jobs[n].kind == JobKind::bank;
            renews += menu.jobs[n].kind == JobKind::renew; holds += menu.jobs[n].kind == JobKind::hold;
        }
        assert(!menu.truncated && renews == 1 && holds == 1 && navigation->visits() <= 192);
        bool routes_complete = false;
        if (menu.single_count) {
            const auto& job = menu.jobs[0];
            assert(job.kind == JobKind::farm && job.group >= 0 && job.altar >= 0);
            const Point endpoints[3] = {info->own.position, info->groups[job.group].position, info->altars[job.altar].position};
            routes_complete = true;
            for (int leg = 0; leg < 2; ++leg) {
                Point path[32];
                const int expected = info->travel.distance(endpoints[leg], endpoints[leg+1], info->travel.context);
                const int count = PublicNavigation::route_prefix(endpoints[leg], endpoints[leg+1], path, 32, navigation.get());
                routes_complete &= expected >= 0 && expected <= 32 && count == expected;
                Point previous = endpoints[leg];
                for (int p = 0; p < count; ++p) {
                    const auto topology = navigation->topology();
                    assert(topology.complete && topology.walkable(path[p], topology.context));
                    assert(std::max(std::abs(path[p].x-previous.x),std::abs(path[p].y-previous.y)) == 1);
                    previous = path[p];
                }
                routes_complete &= previous == endpoints[leg+1];
            }
        } else if (!navigation->initialized()) {
            assert(menu.count == 2 && farms == 0 && banks == 0);
        }
        if (expect_deferral) {
            assert(!navigation->initialized() && !navigation->topology().complete && navigation->visits() == 0);
            assert(nav_after == int(nav_before) && nav_after >= int(optional_floor));
            assert(menu.single_count == 0 && menu.count == 2 && farms == 0 && banks == 0 && !routes_complete);
            assert(menu.jobs[0].kind == JobKind::renew && menu.jobs[1].kind == JobKind::hold);
            const Point endpoints[3] = {info->own.position, group.position, info->altars[0].position};
            for (int leg = 0; leg < 2; ++leg) {
                Point path[32];
                assert(info->travel.distance(endpoints[leg], endpoints[leg+1], info->travel.context) == unreachable);
                assert(PublicNavigation::route_prefix(endpoints[leg], endpoints[leg+1], path, 32, navigation.get()) == -1);
            }
        }
        auto spatial = std::make_unique<r::SpatialEvidence>();
        spatial->start(*info, menu, navigation->topology(), entries[0].group.camps(), std::vector<cambc::champion>{},
            PublicNavigation::route_prefix, navigation.get(), 192, optional_floor, navigation->visits());
        const auto layout = observer->layout_for_menu(*info, menu);
        auto model = std::make_unique<r::Foundry>();
        model->observe_actors(actors->frame()); model->observe_producers(online->producers());
        model->observe_combat(kit); model->observe_spatial(spatial->frame()); model->observe_layout(layout);
        model->observe_snapshot(*info, online->members());
        const int before_evaluation_queries = cm::queries;
        auto evaluation = std::make_unique<Evaluation>(); r::evaluate_model(*info, menu, *model, *evaluation);
        assert(cm::queries == before_evaluation_queries && !cm::unauthorized);
        std::printf("navigation-caller control=%s syntheticSDK=100 stageBoundary=post-observer navBefore=%u callerFloor=%u effectiveFloor=%llu allowance=%u turn=%d navAfter=%d initialized=%d visits=%d singles=%d candidates=%d farm=%d bank=%d renew=%d hold=%d routes=%d spatial=%d foundryReason=%d usedFoundry=%d\n",
            expect_deferral ? "safe-deferral" : "productive", nav_before, nav_floor, static_cast<unsigned long long>(policy.floor), phase_allowance, attempt+1, nav_after,
            navigation->initialized(), navigation->visits(), menu.single_count, menu.count, farms, banks, renews, holds,
            routes_complete, spatial->frame().current_complete, int(model->unsupported()), model->used_foundry());
        result = {menu.single_count, menu.count, routes_complete};
        if (result.single_count >= 1 && result.candidates >= 3 && result.routes_complete) break;
    }
    return result;
}

int main() {
    collect_and_bind(false); collect_and_bind(true);
    assert(collector_cost("uniform25-layout12x3", 25, 12, -1, 25) == 0);
    assert(collector_cost("actor25-observer100-layout12x3", 25, 12) == 0);
    assert(collector_cost("actor50-observer100-layout12x3", 50, 12) == 0);
    assert(collector_cost("actor100-observer100-layout12x3", 100, 12) == 0);
    assert(collector_cost("actor100-observer100-layout4x3", 100, 4) == 0);
    assert(collector_cost("headroom6600", 100, 12, 6600) == 0);
    assert(collector_cost("headroom6599", 100, 12, 6599) == 4);
    assert(collector_cost("actor200-observer100-layout12x3", 200, 12) == 16);
    assert(collector_cost("actor200-headroom6599", 200, 12, 6599) == 20);
    // Retain an explicit observer-truncation stress beyond the original
    // sensitivity inputs: a larger layout and doubled following query cost.
    assert(collector_cost("stress-observer-truncation-actor100-observer200-layout16x3", 100, 16, -1, 200, true) == 2);
    golem_metadata(); observer_packet_boundaries(); menu_work_admission(); packet_boundaries();
    const auto navigation_low = navigation_caller(47500, true);
    const auto navigation_mid = navigation_caller(54000, true);
    const auto navigation_high = navigation_caller(63500);
    std::fflush(stdout); // Retain controls even if productive convergence fails.
    assert(navigation_low.single_count == 0 && navigation_low.candidates == 2 && !navigation_low.routes_complete);
    assert(navigation_mid.single_count == 0 && navigation_mid.candidates == 2 && !navigation_mid.routes_complete);
    assert(navigation_high.single_count >= 1 && navigation_high.candidates >= 3 && navigation_high.routes_complete);
    std::puts("plan4-foundry-coverage: actor/online/observer/menu/spatial/layout -> Foundry passed; projected kit and synthetic SDK, no native witness");
    return 0;
}
