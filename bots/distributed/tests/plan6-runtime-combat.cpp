// Focused helper checks. Native Bot::turn caller probes remain required; this
// does not replace the runtime's policy, observation, forecast or executor.
#include "../common/src/runtime_combat.h"
#include "../common/src/runtime_threat.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
namespace combat = p6::runtime_combat;
namespace threat = p6::runtime_threat;
struct Position { int x = 0, y = 0; };
struct Mask {
    Position origin{};
    int radius = -1;
    bool is_set(Position p) const { return combat::distance(origin, p) <= radius; }
};
struct Flags {
    enum { camps = 1, enemies = 2, ground = 4, self = 8 };
    unsigned bits = 0;
    bool contains(unsigned bit) const { return (bits & bit) != 0; }
};
struct DirectInfo { bool exclusive_with_movement = false; };
struct AreaInfo { bool exclusive_with_movement = false; double radius = 1; };
struct Camp {
    int id = 0;
    Position tile{};
    double hp = 1;
    bool visible = true, alive = true, member = true;
    Position position() const { return tile; }
    bool is_visible() const { return visible; }
    bool is_alive() const { return alive; }
    double health() const { return hp; }
};
struct Ability {
    enum Kind { unused, direct, area, cc, reset, dash } kind = unused;
    std::optional<std::variant<DirectInfo, AreaInfo>> metadata;
    Flags flags{};
    Mask range{};
    double amount = 0;
    bool ready = true;
    bool* cast_available = nullptr;
    mutable int masks = 0, damage_queries = 0, units = 0, grounds = 0, selfs = 0;
    const auto& info() const { return metadata; }
    bool is_damage() const { return kind == direct; }
    bool is_aoe_damage() const { return kind == area; }
    bool is_cc() const { return kind == cc; }
    bool is_dash() const { return kind == dash; }
    bool is_attack_reset() const { return kind == reset; }
    bool is_ready() const { return ready && cast_available && *cast_available; }
    double damage_amount() const { ++damage_queries; return amount; }
    Flags valid_targets() const { return flags; }
    Mask valid_tiles() const { ++masks; return range; }
    bool consume() const { *cast_available = false; return true; }
    bool use(const Camp&) const { ++units; return consume(); }
    bool use(Position) const { ++grounds; return consume(); }
    bool use() const { ++selfs; return consume(); }
};
struct Champion {
    bool attack_available = true, cast_available = true;
    std::array<Ability, 2> slots{};
    Position position() const { return {}; }
    bool can_attack() const { return attack_available; }
    bool can_cast() const { return cast_available; }
    Mask attack_tiles() const { return {{}, 1}; }
    const auto& abilities() const { return slots; }
    void spell(int slot, Ability::Kind kind, unsigned flags, int range, double amount,
               bool exclusive = false) {
        auto& a = slots[slot];
        a.kind = kind; a.flags.bits = flags; a.range.radius = range; a.amount = amount;
        a.cast_available = &cast_available;
        if (kind == Ability::area) a.metadata = AreaInfo{exclusive, 1};
        else a.metadata = DirectInfo{exclusive};
    }
};
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL runtime combat: %s\n", message); std::exit(1); }
}
template<class Options> const Camp* choose(const std::vector<Camp>& camps, const Options& options) {
    return combat::select_group_target(camps, [](const Camp& c) { return c.member; },
                                       options, [](const Camp& c) { return c.id; });
}
void target_and_spell_options() {
    Champion champion;
    std::vector<Camp> camps{{10,{1,0},100}, {11,{4,0},5}};
    auto basic = combat::observe_options(champion, false);
    check(choose(camps, basic)->id == 10, "legal basic target precedes distant low health");
    champion.spell(0, Ability::cc, Flags::camps, 4, 140, true);
    auto options = combat::observe_options(champion, false);
    check(choose(camps, options)->id == 11, "damaging CC makes spell-range target actionable");
    check(options.choose_cast({4,0}, combat::TargetKind::camp).mode == combat::CastMode::unit,
          "ordinary camp work includes damaging unit CC");
    check(champion.slots[0].masks == 1 && champion.slots[0].damage_queries == 1,
          "one live mask and damage query per ready offer");
    choose(camps, options); choose(camps, options);
    check(champion.slots[0].masks == 1, "candidate selection performs no further live mask query");
    auto walked = combat::observe_options(champion, true);
    check(choose(camps, walked)->id == 10, "walking excludes incompatible damaging spell");
    champion.spell(1, Ability::direct, Flags::enemies, 4, 50);
    auto category = combat::observe_options(champion, true);
    check(!category.choose_cast({4,0}, combat::TargetKind::camp), "enemy-only spell cannot target camp");
    check(category.choose_cast({4,0}, combat::TargetKind::enemy).slot == 1,
          "walking-compatible direct spell remains available for enemy");
    champion.cast_available = false;
    auto spent = combat::observe_options(champion, false);
    check(!spent.choose_cast({4,0}, combat::TargetKind::enemy), "spent shared cast allowance removes offers");
}
void cast_modes_and_budget() {
    Champion champion;
    champion.attack_available = false;
    champion.spell(0, Ability::area, Flags::ground, 4, 98, true);
    champion.spell(1, Ability::direct, Flags::camps, 4, 140, true);
    Camp target{10,{4,0},200};
    auto options = combat::observe_options(champion, false);
    const auto choice = options.choose_cast(target.position(), combat::TargetKind::camp);
    check(choice.slot == 0 && choice.mode == combat::CastMode::ground, "area damage selects ground cast");
    check(combat::use_cast(champion.slots[0], target, choice.mode, false), "ground cast succeeds");
    check(champion.slots[0].grounds == 1 && champion.slots[0].units == 0,
          "ground area uses tile overload, not unit overload");
    check(!combat::use_cast(champion.slots[1], target, combat::CastMode::unit, false),
          "live readiness prevents second successful cast from old options");
    champion.cast_available = true;
    check(!combat::use_cast(champion.slots[1], target, combat::CastMode::unit, true),
          "execution rechecks walking exclusion");
    champion.spell(0, Ability::area, Flags::self, 4, 30);
    options = combat::observe_options(champion, false);
    check(options.mode(0,{1,0},combat::TargetKind::camp) == combat::CastMode::self,
          "self area requires represented effect radius");
    check(options.mode(0,{4,0},combat::TargetKind::camp) == combat::CastMode::none,
          "self area cannot hit arbitrary targets in casting range");
    target.tile = {1,0};
    check(combat::use_cast(champion.slots[0], target, combat::CastMode::self, false) &&
          champion.slots[0].selfs == 1, "self cast uses no-argument overload");

    Champion dual;
    dual.attack_available = false;
    dual.spell(0, Ability::area, Flags::camps | Flags::enemies | Flags::ground, 4, 98, true);
    const auto dual_options = combat::observe_options(dual, false);
    target.tile = {4,0};
    const auto dual_choice = dual_options.choose_cast(target.position(), combat::TargetKind::camp);
    check(dual_choice.slot == 0 && dual_choice.mode == combat::CastMode::unit &&
          dual_options.mode(0, target.position(), combat::TargetKind::enemy) == combat::CastMode::unit,
          "dual unit/ground AoE prefers a legal unit cast for camps and enemies");
    check(combat::use_cast(dual.slots[0], target, dual_choice.mode, false) &&
          dual.slots[0].units == 1 && dual.slots[0].grounds == 0,
          "dual target flags dispatch the selected unit overload");
    check(!combat::use_cast(dual.slots[0], target, combat::CastMode::ground, false),
          "dual unit/ground flags do not grant a second successful cast");
}
void enemy_damage_bound_and_dash_dispatch() {
    Champion enemy;
    enemy.spell(0, Ability::direct, Flags::camps | Flags::enemies, 4, 140, true);
    enemy.spell(1, Ability::dash, Flags::ground, 2, 150);
    check(combat::enemy_spell_bound(enemy.abilities(), 30) == 150,
          "damaging dash landing is included and two spell slots are not added");
    check(enemy.slots[0].damage_queries == 1 && enemy.slots[1].damage_queries == 1 &&
          enemy.slots[0].masks == 0 && enemy.slots[1].masks == 0,
          "enemy bound queries each damage once without geometric mask work");
    enemy.cast_available = false;
    enemy.slots[0].ready = enemy.slots[1].ready = false;
    check(combat::enemy_spell_bound(enemy.abilities(), 30) == 150,
          "unknown enemy readiness cannot discount the conservative damage bound");
    enemy.slots[1].amount = 0;
    check(combat::enemy_spell_bound(enemy.abilities(), 30) == 140,
          "zero-damage dash adds no spell damage");
    enemy.slots[0].kind = Ability::cc;
    enemy.slots[0].amount = 75;
    check(combat::enemy_spell_bound(enemy.abilities(), 30) == 75,
          "damaging control remains in the enemy bound");
    enemy.slots[0].kind = Ability::area;
    enemy.slots[0].amount = 98;
    check(combat::enemy_spell_bound(enemy.abilities(), 30) == 98,
          "area damage remains in the enemy bound");
    enemy.slots[0].kind = Ability::reset;
    enemy.slots[0].amount = 999;
    check(55 + combat::enemy_spell_bound(enemy.abilities(), 55) == 110,
          "basic then reset then basic includes one extra Strength attack");
    check(enemy.slots[0].damage_queries == 5,
          "reset candidate uses Strength rather than querying direct spell damage");
    enemy.slots[1].kind = Ability::reset;
    check(combat::enemy_spell_bound(enemy.abilities(), 55) == 55,
          "dual reset slots cannot add two extra attacks with one successful cast");
    enemy.slots[1].kind = Ability::direct;
    enemy.slots[1].amount = 140;
    check(55 + combat::enemy_spell_bound(enemy.abilities(), 55) == 195,
          "damage versus reset uses the larger legal single-cast alternative");
    enemy.slots[1].amount = 45;
    check(combat::enemy_spell_bound(enemy.abilities(), 55) == 55,
          "stronger reset alternative precedes a weaker damaging spell");
    check(std::isinf(combat::enemy_spell_bound(enemy.abilities(), -1)) &&
          std::isinf(combat::enemy_spell_bound(enemy.abilities(),
              std::numeric_limits<double>::quiet_NaN())),
          "invalid Strength fails closed for the reset bound");
    enemy.slots[1].kind = Ability::dash;
    enemy.slots[1].amount = std::numeric_limits<double>::quiet_NaN();
    check(std::isinf(combat::enemy_spell_bound(enemy.abilities(), 55)),
          "invalid queried damaging amount fails closed");

    Champion dasher;
    dasher.attack_available = false;
    dasher.spell(0, Ability::dash, Flags::camps | Flags::enemies | Flags::ground, 2, 150);
    Camp target{10,{1,0},200};
    const auto options = combat::observe_options(dasher, false);
    check(!options.actionable(target.position(), combat::TargetKind::camp) &&
          dasher.slots[0].masks == 0 && dasher.slots[0].damage_queries == 0,
          "positive dash damage alone creates no ordinary target offer");
    check(!combat::use_cast(dasher.slots[0], target, combat::CastMode::unit, false) &&
          !combat::use_cast(dasher.slots[0], target, combat::CastMode::ground, false) &&
          dasher.cast_available && dasher.slots[0].units == 0 && dasher.slots[0].grounds == 0,
          "dash cannot be dispatched through ordinary unit or target-tile casts");
}
void fallback_and_control() {
    Champion champion;
    champion.attack_available = false;
    champion.spell(0, Ability::cc, Flags::camps | Flags::enemies, 4, 0);
    champion.spell(1, Ability::direct, Flags::camps, 4, 30);
    auto options = combat::observe_options(champion, false);
    check(options.choose_cast({2,0},combat::TargetKind::camp).slot == 1,
          "zero-damage CC earns no ordinary damage activity");
    check(options.choose_cast({2,0},combat::TargetKind::camp,true,true).slot == 0,
          "control watch can retain eligible non-damaging control");
    check(options.choose_cast({2,0},combat::TargetKind::camp,true,false).slot == 1,
          "ineligible control falls through to damaging alternative");
    champion.cast_available = false;
    options = combat::observe_options(champion, false);
    std::vector<Camp> camps{{40,{4,0},1},{30,{2,0},30},{20,{2,0},20},{10,{2,0},20},
                            {1,{1,0},1,false},{2,{1,0},1,true,false},{3,{1,0},1,true,true,false}};
    check(choose(camps,options)->id == 10, "fallback sorts distance, health and identity; filters invalid members");
    camps[3].hp = 0;
    check(choose(camps,options)->id == 20, "zero-health member cannot become approach target");
}
threat::Enemy numeric_enemy(int id=20) {
    threat::Enemy enemy;
    enemy.id=id;enemy.stress=30;enemy.move_speed=1;enemy.attack_range=1;enemy.complete=true;
    return enemy;
}
threat::Enemy numeric_tavi() {
    auto enemy=numeric_enemy();enemy.ranged=true;enemy.attack_range=3;
    enemy.slots[0]={.reset=true};
    enemy.slots[1]={.harmful=true,.exclusive_movement=true,.range=4};
    return enemy;
}
p6::Actor numeric_actor(p6::Point position={}) {
    p6::Actor actor;actor.id=10;actor.team=p6::Team::ally;actor.alive=true;
    actor.position=position;actor.health=actor.max_health=1000;
    return actor;
}
void next_boundary_reach() {
    auto enemy=numeric_enemy();enemy.ranged=true;enemy.attack_range=3;
    check(threat::max_reach(enemy,1)==3 && threat::max_reach(enemy,2)==4,
          "ranged basic reaches3 now or walk1 then basic3 next turn, not walk and basic together");
    enemy.slots[0]={.reset=true};
    check(threat::max_reach(enemy,1)==4 && threat::max_reach(enemy,2)==5,
          "ranged reset permits walk1 plus basic3, with one earlier walk reaching5 over two turns");
    enemy.slots[0].exclusive_movement=true;
    check(threat::max_reach(enemy,1)==3 && threat::max_reach(enemy,2)==4,
          "movement-exclusive reset does not supply same-turn walking reach");
    enemy=numeric_tavi();
    check(threat::max_reach(enemy,1)==4 && threat::max_reach(enemy,2)==5,
          "Tavi's exclusive range4 cast or reset reaches at most5 over two turns");

    enemy=numeric_enemy();enemy.ranged=true;
    enemy.slots[0]={.harmful=true,.exclusive_movement=true,.range=4};
    check(threat::max_reach(enemy,1)==4 && threat::max_reach(enemy,2)==5,
          "exclusive spell reaches4 or an earlier walk followed by range4");
    enemy.slots[0].exclusive_movement=false;
    check(threat::max_reach(enemy,1)==5 && threat::max_reach(enemy,2)==6,
          "compatible spell can follow walking, extending each literal range boundary by one");
    enemy.slots[0].exclusive_movement=true;enemy.slots[0].radius=1;
    check(threat::max_reach(enemy,1)==5 && threat::max_reach(enemy,2)==6,
          "range4 AoE reaches a victim one tile beyond its legal cast center");

    enemy=numeric_enemy();enemy.slots[0]={.reset=true};enemy.slots[1]={.dash=true,.range=2};
    check(threat::max_reach(enemy,1)==4 && threat::max_reach(enemy,2)==7,
          "Vendetta can walk1 dash2 basic1, then repeat to reach7 without adding a reset cast");
    enemy=numeric_enemy();enemy.slots[0]={.dash=true,.exclusive_movement=true,.range=2};
    enemy.slots[1]={.harmful=true,.exclusive_movement=true,.range=3};
    // A first-turn dash2 followed by a second-turn spell3 reaches5. A second
    // dash plus that spell would reach7, but would require three casts in2turns.
    check(threat::max_reach(enemy,1)==3 && threat::max_reach(enemy,2)==5,
          "earlier dash can extend a later spell, without inserting another same-phase dash");
    const p6::ViewClock clock{40,10,2};
    const std::array cast_sequence{p6::Observation<threat::Enemy>{enemy,clock,enemy.id,p6::Knowledge::exact}};
    check(threat::exposure(numeric_actor({6,0}),clock,cast_sequence).incoming_total==0,
          "a victim at6 cannot be reached by the phantom third dash-plus-spell cast sequence");
    enemy=numeric_enemy();enemy.slots[0]={.dash=true,.harmful=true,.range=2,.radius=2};
    check(threat::max_reach(enemy,1)==5 && threat::max_reach(enemy,2)==8,
          "damaging dash reaches walk1 dash2 landing-radius2, after an earlier displacement of3");
    enemy=numeric_enemy();enemy.splash_radius=1;enemy.slots[0]={.reset=true};
    check(threat::max_reach(enemy,1)==3 && threat::max_reach(enemy,2)==4,
          "Varg can hit a nearer primary and splash a victim beyond direct basic range");
    enemy.splash_radius=0;
    check(threat::max_reach(enemy,2)==3,"removing splash removes that extra victim reach");
}
void next_boundary_exposure() {
    const p6::ViewClock clock{40,10,2};
    const auto observed=[&](threat::Enemy value) {
        return p6::Observation<threat::Enemy>{value,clock,value.id,p6::Knowledge::exact};
    };
    auto tavi=numeric_tavi();
    std::array enemies{observed(tavi)};
    auto actor=numeric_actor({5,0});
    auto exposure=threat::exposure(actor,clock,enemies);
    check(exposure.observations_complete && exposure.incoming_total==60 && exposure.next_phase_incoming==30,
          "fresh Tavi at distance5 retains two complete30-damage enemy turns and one-phase reserve");
    actor.position={6,0};exposure=threat::exposure(actor,clock,enemies);
    check(exposure.observations_complete && exposure.incoming_total==0 && exposure.next_phase_incoming==0,
          "the next tile beyond exact two-turn reach excludes that enemy, without changing stress");
    enemies[0].value.completed_turn=true;actor.position={5,0};
    exposure=threat::exposure(actor,clock,enemies);
    check(exposure.observations_complete && exposure.incoming_total==0,
          "a fresh completed-turn receipt leaves only one enemy turn and cannot reach distance5");
    actor.position={4,0};exposure=threat::exposure(actor,clock,enemies);
    check(exposure.incoming_total==30 && exposure.next_phase_incoming==30,
          "completed-turn receipt retains the full one-turn damage at its inclusive reach boundary");
    enemies[0].value.complete=false;actor.position={100,100};
    exposure=threat::exposure(actor,clock,enemies);
    check(exposure.observations_complete && exposure.incoming_total==30,
          "unknown geometry retains full stress even when a completed enemy is far away");
    enemies[0].value.completed_turn=false;
    check(threat::exposure(actor,clock,enemies).incoming_total==60,
          "unknown geometry without completed-turn receipt retains both enemy turns");

    enemies[0]=observed(tavi);enemies[0].value.slots[0].known=false;
    check(std::isinf(threat::max_reach(enemies[0].value,2)) &&
          threat::exposure(actor,clock,enemies).incoming_total==60,
          "an unknown slot cannot be treated as absent mobility or absent offense");
    enemies[0]=observed(tavi);++enemies[0].clock.round;
    check(!threat::exposure(actor,clock,enemies).observations_complete,"stale enemy clock fails closed before reach exclusion");
    enemies[0]=observed(tavi);++enemies[0].clock.revision;
    check(!threat::exposure(actor,clock,enemies).observations_complete,"another action revision cannot restamp enemy geometry");
    enemies[0]=observed(tavi);++enemies[0].subject;
    check(!threat::exposure(actor,clock,enemies).observations_complete,"wrong enemy subject fails closed");
    enemies[0]=observed(tavi);enemies[0].knowledge=p6::Knowledge::stale;
    check(!threat::exposure(actor,clock,enemies).observations_complete,"stale knowledge is not a fresh scheduling receipt");
    const std::array duplicates{observed(tavi),observed(tavi)};
    check(!threat::exposure(actor,clock,duplicates).observations_complete,"duplicate enemy IDs cannot form a complete threat view");
    enemies[0]=observed(tavi);enemies[0].value.stress=std::numeric_limits<double>::quiet_NaN();
    check(!threat::exposure(actor,clock,enemies).observations_complete,"invalid stress fails closed even beyond known reach");

    tavi.completed_turn=true;tavi.position={4,0};
    auto vendetta=numeric_enemy(21);vendetta.position={7,0};vendetta.stress=40;
    vendetta.slots[0]={.reset=true};vendetta.slots[1]={.dash=true,.range=2};
    const std::array pair{observed(tavi),observed(vendetta)};actor=numeric_actor();
    exposure=threat::exposure(actor,clock,pair,7);
    check(exposure.observations_complete && exposure.incoming_total==117 && exposure.next_phase_incoming==77,
          "one Tavi turn30 plus two Vendetta turns80 plus one camp boundary7 aggregate independently");
    check(p6::survives(exposure),"aggregate exposure uses the existing health and two-next-phase reserve rule");
    actor.health=200;
    check(!p6::survives(threat::exposure(actor,clock,pair,7)),"aggregate cannot assign away damage to satisfy the health reserve");
    actor=numeric_actor({0,10});exposure=threat::exposure(actor,clock,pair,7);
    check(exposure.observations_complete && exposure.incoming_total==7 && exposure.next_phase_incoming==7,
          "a displaced candidate point recomputes both enemy distances while charging camp only once");
    auto wrong_clock=clock;++wrong_clock.self;
    check(!threat::exposure(actor,wrong_clock,pair).observations_complete,"another actor cannot borrow the threat exposure clock");
    check(!threat::exposure(actor,clock,pair,-1).observations_complete,"negative camp incoming cannot improve exposure");
}
void invalid_threat_geometry() {
    auto enemy=numeric_enemy();
    check(std::isinf(threat::max_reach(enemy,0)) && std::isinf(threat::max_reach(enemy,3)),
          "unsupported horizons provide no local reach discount");
    enemy.move_speed=-1;
    check(std::isinf(threat::max_reach(enemy,2)),"unknown movement speed provides no reach discount");
    enemy=numeric_enemy();enemy.slots[0].range=std::numeric_limits<double>::infinity();
    check(std::isinf(threat::max_reach(enemy,2)),"nonfinite slot geometry provides no reach discount");
    enemy=numeric_enemy();enemy.slots[1].radius=-1;
    check(std::isinf(threat::max_reach(enemy,2)),"negative radius provides no reach discount");
}
void prepared_threat_lifetime() {
    const p6::ViewClock clock{40,10,2};
    auto enemy=numeric_tavi();
    std::array observed{p6::Observation<threat::Enemy>{enemy,clock,enemy.id,p6::Knowledge::exact}};
    const auto prepared=threat::prepare(clock,observed);
    check(prepared.complete && prepared.count==1,"fresh numeric preparation publishes a complete checked cache");
    auto actor=numeric_actor({5,0});
    auto exposure=threat::exposure(actor,clock,prepared,7);
    check(exposure.observations_complete && exposure.incoming_total==67 && exposure.next_phase_incoming==37,
          "prepared distance5 retains two30-damage turns plus one7-damage camp boundary");
    actor.position={6,0};exposure=threat::exposure(actor,clock,prepared,7);
    check(exposure.observations_complete && exposure.incoming_total==7 && exposure.next_phase_incoming==7,
          "the same frozen preparation excludes distance6 while preserving the camp boundary");
    observed[0].value.position={100,100};observed[0].value.stress=999;
    observed[0].value.move_speed=100;observed[0].value.slots[1].range=100;
    observed[0].value.completed_turn=true;++observed[0].clock.revision;
    actor.position={5,0};exposure=threat::exposure(actor,clock,prepared,7);
    check(exposure.observations_complete && exposure.incoming_total==67 && exposure.next_phase_incoming==37,
          "prepared positions stress reach and phase count own their numeric values rather than aliasing mutable inputs");
    auto later=clock;++later.round;
    check(!threat::exposure(actor,later,prepared).observations_complete,"prepared exposure cannot cross an owner round");
    later=clock;++later.revision;
    check(!threat::exposure(actor,later,prepared).observations_complete,"prepared exposure cannot cross an action revision");
    auto invalid=prepared;invalid.complete=false;
    check(!threat::exposure(actor,clock,invalid).observations_complete,"incomplete preparation cannot expose a usable numeric prefix");
    invalid=prepared;invalid.count=-1;
    check(!threat::exposure(actor,clock,invalid).observations_complete,"negative prepared count is rejected before entry access");
    invalid.count=p6::max_actors+1;
    check(!threat::exposure(actor,clock,invalid).observations_complete,"oversized prepared count cannot overrun bounded storage");
    auto second=numeric_enemy(21);second.stress=std::numeric_limits<double>::quiet_NaN();
    const std::array partial{
        p6::Observation<threat::Enemy>{enemy,clock,enemy.id,p6::Knowledge::exact},
        p6::Observation<threat::Enemy>{second,clock,second.id,p6::Knowledge::exact}};
    const auto failed=threat::prepare(clock,partial);
    check(!failed.complete && !threat::exposure(actor,clock,failed).observations_complete,
          "failure after a valid first enemy never publishes partial threat preparation");
}
} // namespace
int main() {
    target_and_spell_options(); cast_modes_and_budget(); fallback_and_control();
    enemy_damage_bound_and_dash_dispatch();
    next_boundary_reach();next_boundary_exposure();invalid_threat_geometry();
    prepared_threat_lifetime();
    std::printf("runtime combat: %d checks passed\n",checks);
}

#ifdef P6_COMBAT_SDK_CHECK
#include <cambc.h>
// Compile the same helper methods against the actual pinned public SDK. This
// function is syntax-checked only; native caller probes exercise the real bot.
void pinned_sdk_instantiation(const std::vector<cambc::camp>& camps) {
    const auto& me = cambc::self();
    [[maybe_unused]] const double enemy_bound = combat::enemy_spell_bound(me.abilities(), me.strength());
    const auto options = combat::observe_options(me, false);
    const auto* target = combat::select_group_target(camps, [](const auto&) { return true; },
        options, [](const auto& camp) { return int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp))); });
    if (target) {
        const auto choice = options.choose_cast(target->position(),combat::TargetKind::camp);
        if (choice) combat::use_cast(me.abilities()[choice.slot],*target,choice.mode,false);
    }
}
#endif
