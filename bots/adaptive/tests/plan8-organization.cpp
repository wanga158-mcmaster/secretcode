#include "../common/src/organization.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace p8;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
Route path(std::initializer_list<p6::Point> tiles) {
    Route r; r.verified = true; r.count = static_cast<int>(tiles.size());
    int i = 0; for (auto tile : tiles) r.tiles[i++] = tile;
    return r;
}
Plan solo(Organization organization = Organization::U) {
    Plan p; p.organization = organization; p.group = 7; p.collector = 1; p.altar = 9;
    p.crew[0] = 1; p.crew_count = 1;
    p.ingress = path({{0,0},{1,0},{2,0}});
    p.capture = path({{2,0},{3,0}});
    p.bank = path({{3,0},{4,0},{5,0}});
    p.renewal = path({{5,0},{6,0},{7,0}});
    p.forecast_rounds = 20; p.remaining_rounds = 500; p.capture_window = 5;
    p.gem_bearing = p.complete_verified = true; return p;
}
Plan joint(Organization org = Organization::H) {
    auto p = solo(org); p.crew[1] = 2; p.crew_count = 2;
    p.pads[0] = {2,0}; p.pads[1] = {2,1}; p.rally_legal = true; return p;
}
LocalFrame frame(const Plan& p, int self = 1) {
    LocalFrame in; in.frame.clock = {0,self,0}; in.current_round = 0;
    auto& a = in.frame.own.actor;
    a.id = self; a.type = self == 1 ? p6::Champion::tavi : p6::Champion::varg;
    a.team = p6::Team::ally;
    a.alive = a.can_move = a.can_attack = true;
    a.health = a.max_health = 100; a.position = p.ingress.tiles[0];
    p6::Group g; g.id = p.group; g.original_spawn_visible = g.all_spawns_visible = true;
    g.uncontested_now = true; g.lifecycle = p6::Lifecycle::living;
    in.frame.group = {g,in.frame.clock,p.group,p6::Knowledge::exact};
    in.legal = {{},in.frame.clock,self,p6::Knowledge::exact};
    in.legal.value.safe_now = in.legal.value.attack_legal = in.legal.value.altar_legal = true;
    in.legal.value.recall_safe = in.legal.value.renewal_productive = true;
    for (int i = 0; i < p.crew_count; ++i) if (p.crew[i] != self) {
        p6::Actor other = a; other.id = p.crew[i];
        other.type = other.id == 1 ? p6::Champion::tavi : p6::Champion::varg;
        other.position = p.pads[i];
        in.frame.actors[in.frame.actor_count++] = {other,in.frame.clock,other.id,p6::Knowledge::exact};
    }
    return in;
}
void refresh(LocalFrame& in, const Plan& p) {
    in.current_round = in.frame.clock.round;
    in.legal.clock = in.frame.group.clock = in.frame.clock;
    for (int i = 0; i < in.frame.actor_count; ++i) in.frame.actors[i].clock = in.frame.clock;
    in.safety_count = 0;
    for (int i = 0; i < p.crew_count; ++i) {
        const auto* a = in.frame.visible(p.crew[i]);
        if (!a) continue;
        p6::CycleCertificate c; c.clock = in.frame.clock; c.actor = a->id; c.group = p.group;
        c.clear = c.hold = c.bank = c.recovery = {a->health,a->max_health,0,0,0,0,true};
        c.route_known = c.bank_executable = c.renewal_reachable = c.solo = true;
        in.safety[in.safety_count++] = c;
    }
    for (Leg leg : {Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
        auto& access = in.legal.value.routes[static_cast<int>(leg)];
        const auto& route = route_for(p,leg);
        access = {}; access.assumptions_hold = route_valid(route);
        access.next_move_legal = true;
        for (int i = 0; i < route.count; ++i) if (route.tiles[i] == in.frame.own.actor.position) {
            access.next_tile = route.tiles[std::min(i+1,route.count-1)]; break;
        }
    }
}
void next(LocalFrame& in, const Plan& p, p6::Point position) {
    ++in.frame.clock.round; in.frame.own.actor.position = position;
    in.milestones = {}; refresh(in,p);
}
ActionReceipt receipt(const Command& c, bool success = true) {
    return {c.clock,c.clock,c.token,c.action,true,success};
}
void payout(LocalFrame& in, int group, int amount) {
    in.frame.own.actor.bag = amount;
    in.milestones = {{group,true,false,amount,0},in.frame.clock,in.frame.clock.self,p6::Knowledge::exact};
}
// These wrappers intentionally remain separate machine stack frames. The
// optimized offline fixture retains several alternative Plan/Work aggregates;
// inlining every case into main would exceed default WASI's small stack even
// though each bounded executor is independently small.
__attribute__((noinline)) void full_cycle(Organization org, bool actual_attack = true, bool helper_attack = false) {
    auto p = org == Organization::H || org == Organization::J ? joint(org) : solo(org);
    auto in = frame(p); refresh(in,p); Work w;
    check(w.start(p,in),"fresh verified complete solo cycle admits");
    auto c = w.step(in);
    check(c.action == PhysicalAction::move && c.target == p6::Point{1,0},"retained ingress governs first movement");
    check(w.confirm(c,receipt(c)),"actual movement result accepted separately");
    check(w.cursor[0] == 0,"success return alone never advances route cursor");
    next(in,p,{0,0}); c = w.step(in);
    check(c.target == p6::Point{1,0} && w.cursor[0] == 0,"no observed arrival repeats same retained tile");
    next(in,p,{1,0}); c = w.step(in);
    check(c.target == p6::Point{2,0} && w.cursor[0] == 1,"actual next-tile arrival advances exactly once");
    next(in,p,{2,0}); c = w.step(in);
    check(c.action == PhysicalAction::attack && !w.clearance_seen,"observed living group produces legal attack proposal");
    check(!w.productive_action_seen,"proposed attack is not evidence of actual production");
    if (actual_attack) check(w.confirm(c,receipt(c)),"actual successful own attack receipt supplies production evidence");
    check(!w.payout_seen && !w.bank_seen,"attack proposal invents no reward or conversion");
    next(in,p,{2,0}); in.frame.group.value.lifecycle = p6::Lifecycle::reward_available;
    if (helper_attack) {
        in.frame.actors[0].value.action = p6::WitnessAction::useful_attack;
        in.frame.actors[0].value.action_group = p.group;
    }
    c = w.step(in);
    check(c.action == PhysicalAction::move && c.target == p6::Point{3,0},"observed clearance starts retained capture entry");
    next(in,p,{3,0}); in.frame.own.actor.spawn_group = p.group; c = w.step(in);
    check(c.action == PhysicalAction::hold && !w.payout_seen,"actual spawn arrival holds without invented capture timer");
    next(in,p,{3,0}); payout(in,p.group,12); c = w.step(in);
    check(w.payout_seen && c.action == PhysicalAction::move && c.target == p6::Point{4,0},"own payout receipt vacates spawn along retained bank route");
    if (org == Organization::H) in.frame.actor_count = 0;
    next(in,p,{4,0}); in.frame.own.actor.spawn_group = -1; c = w.step(in);
    check(c.target == p6::Point{5,0},"loaded return uses next retained tile");
    next(in,p,{5,0}); c = w.step(in);
    check(c.action == PhysicalAction::tribute && !w.tribute_started,"tribute proposal is not a started channel receipt");
    next(in,p,{5,0}); c = w.step(in);
    check(!w.bank_seen && c.action == PhysicalAction::tribute,"unconfirmed proposal cannot establish a bank");
    check(w.confirm(c,receipt(c)),"successful actual tribute result confirms start");
    next(in,p,{5,0}); in.frame.own.actor.channel = p6::Channel::tribute;
    in.frame.own.actor.channel_valid = true; c = w.step(in);
    check(c.action == PhysicalAction::preserve_channel,"safe valid tribute is preserved");
    next(in,p,{5,0}); in.frame.own.actor.channel = p6::Channel::none;
    in.frame.own.actor.bag = 0;
    in.milestones = {{p.group,false,true,0,12},in.frame.clock,1,p6::Knowledge::exact};
    c = w.step(in);
    check(w.bank_seen && c.target == p6::Point{6,0},"confirmed own conversion starts actual productive return");
    next(in,p,{6,0}); c = w.step(in);
    check(c.target == p6::Point{7,0} && !w.cycle_completed,"bank alone does not complete renewed production");
    next(in,p,{7,0}); c = w.step(in);
    check(w.cycle_completed == (actual_attack || helper_attack) && w.renewal_seen && !w.active,
          "full-cycle credit requires actual observed production as well as payout bank renewal");
}
void to_hold(Work& w, LocalFrame& in, const Plan& p) {
    check(w.start(p,in),"hold fixture physical admission");
    w.step(in); next(in,p,{1,0}); w.step(in); next(in,p,{2,0}); auto attack = w.step(in);
    check(w.confirm(attack,receipt(attack)),"hold fixture records an actual productive attack");
    next(in,p,{2,0}); in.frame.group.value.lifecycle = p6::Lifecycle::reward_available; w.step(in);
    next(in,p,{3,0}); in.frame.own.actor.spawn_group = p.group; w.step(in);
}
__attribute__((noinline)) void unrecorded_movement_identity() {
    auto p=solo();auto in=frame(p);refresh(in,p);
    Work manual;manual.plan=p;
    auto inspection=manual.emit(in,PhysicalAction::move,p.ingress.tiles[1]);
    check(inspection.route_hash==plan_route_hash(manual.plan)&&!manual.active&&!manual.route_witness_active,
          "unrecorded inspection move retains SDK dispatch identity without granting route evidence");
    check(manual.confirm(inspection,receipt(inspection))&&!manual.cycle_completed&&!manual.route_witness_active,
          "confirmed manual movement alone cannot complete or qualify a physical cycle");
    in.frame.own.actor.position=p.bank.tiles[0];refresh(in,p);
    manual.plan=p;auto bank=manual.emit(in,PhysicalAction::move,p.bank.tiles[1],Leg::bank);
    check(bank.route_hash!=0&&bank.route_hash==plan_route_hash(manual.plan)&&bank.leg==Leg::bank&&
          bank.target==in.legal.value.routes[int(Leg::bank)].next_tile&&!manual.route_witness_active,
          "unrecorded owned-bag return move retains the exact bank dispatch identity");
    in.frame.own.actor.position=p.ingress.tiles[0];refresh(in,p);
    Plan partial;partial.collector=p.collector;partial.group=p.group;partial.ingress=p.ingress;manual.plan=partial;
    auto partial_move=manual.emit(in,PhysicalAction::move,p.ingress.tiles[1]);
    check(partial_move.route_hash==0&&plan_route_hash(partial)==0&&!manual.route_witness_active,
          "partial inspection plan keeps its existing zero full-plan identity and remains unqualified");
}
__attribute__((noinline)) void routes_and_interruptions() {
    auto p = solo(); auto changed = p;
    changed.bank = path({{3,0},{4,1},{5,0}});
    check(plan_route_hash(p) != plan_route_hash(changed),"return route payload changes evidence context hash");
    changed = p; changed.renewal.tiles[1] = {6,1};
    check(plan_route_hash(p) != plan_route_hash(changed),"productive renewal payload changes evidence context hash");
    changed = p; changed.ingress.tiles[1] = {8,0};
    check(!plan_route_hash(changed) && !plan_valid(changed,1),"unwalkable retained route excluded rather than used as a shortcut");
    changed = p; changed.ingress.count = max_route_tiles+1;
    check(!route_valid(changed.ingress),"oversized route excluded without truncation");
    changed = p; changed.forecast_rounds = 14;
    check(!plan_valid(changed,1),"forecast cannot underprice retained walking legs and per-leg collision slack");

    auto in = frame(p); refresh(in,p); Work blocked;
    check(blocked.start(p,in),"blocked fixture starts");
    auto c = blocked.step(in); auto stale_receipt = receipt(c); ++stale_receipt.current_clock.round;
    check(!blocked.confirm(c,stale_receipt),"dispatch on changed round cannot confirm stale proposal");
    check(blocked.confirm(c,receipt(c,false)),"hidden collision returns an actual failed result");
    check(!blocked.active && !blocked.route_witness_active && blocked.failure == PhysicalFailure::blocked_route,
          "hidden collision invalidates route before any unconstrained replan");
    next(in,p,{0,0}); c = blocked.step(in);
    check(c.action == PhysicalAction::recall && c.route_hash == 0,"safe fallback leaves claimed route mechanism inactive");

    in = frame(p); refresh(in,p); Work obstruction; check(obstruction.start(p,in),"blocker fixture starts");
    in.legal.value.routes[0].assumptions_hold = false; c = obstruction.step(in);
    check(!obstruction.active && c.action != PhysicalAction::move,"fresh known blocker invalidates selected route");
    in = frame(p); refresh(in,p); Work stale; check(stale.start(p,in),"stale fixture starts");
    ++in.current_round; c = stale.step(in);
    check(c.token == 0 && !stale.route_witness_active,"stale frame cannot issue an action or retain qualified route witness");

    in = frame(p); refresh(in,p); Work duplicate; check(duplicate.start(p,in),"duplicate fixture starts");
    c = duplicate.step(in); auto repeated = duplicate.step(in);
    check(repeated.token == 0 && duplicate.owner_turn == 1 && duplicate.confirm(c,receipt(c)),
          "duplicate owner-turn snapshot produces no second action or timing sample");
    next(in,p,{1,0}); in.frame.own.actor.alive = false; duplicate.step(in);
    check(!duplicate.active && duplicate.failure == PhysicalFailure::dead,"death clears active job without fictional completion");

    in = frame(p); refresh(in,p); Work hold; to_hold(hold,in,p);
    hold.request_stop(); next(in,p,{3,0}); c = hold.step(in);
    check(c.action == PhysicalAction::hold && hold.active,"role rollback preserves safe capture until actual endpoint");
    next(in,p,{3,0}); in.fuel_resumed = true; c = hold.step(in);
    check(c.action == PhysicalAction::hold && !hold.route_witness_active && !hold.cycle_completed,
          "fuel resume preserves safe hold while invalidating mechanism evidence");
    in.fuel_resumed = false; next(in,p,{3,0}); in.frame.own.actor.channel = p6::Channel::tribute;
    in.frame.own.actor.channel_valid = true; in.fuel_remaining = 0; c = hold.step(in);
    check(c.action == PhysicalAction::preserve_channel,"safe channel survives fuel guard and pending rollback");
    next(in,p,{3,0}); in.legal.value.safe_now = false; in.legal.value.escape_legal = true;
    in.legal.value.escape_tile = {3,1}; c = hold.step(in);
    check(c.action == PhysicalAction::escape && c.target == p6::Point{3,1},"immediate safety escape overrides channel and capture");

}
__attribute__((noinline)) void cooperation() {
    const auto p = solo(); auto hp = joint(); auto in = frame(hp); refresh(in,hp); Work missing;
    check(missing.start(hp,in),"visible currently safe H collector admits");
    missing.step(in); next(in,hp,{1,0}); in.frame.actor_count = 0; refresh(in,hp); auto c = missing.step(in);
    check(!missing.active && missing.failure == PhysicalFailure::missing_member,
          "hidden helper cancels dependent travel and supplies no future work");
    in = frame(hp); refresh(in,hp); Work hold_missing; to_hold(hold_missing,in,hp);
    next(in,hp,{3,0}); in.frame.actor_count = 0; refresh(in,hp);
    in.safety[0].solo = false;
    for (auto* e : {&in.safety[0].clear,&in.safety[0].hold,&in.safety[0].bank,&in.safety[0].recovery})
        e->assigned_timed_heal = 10;
    hold_missing.step(in);
    check(!hold_missing.active && hold_missing.failure == PhysicalFailure::missing_member,
          "vanished healer cannot supply cooperative hold survival credit");
    in = frame(hp); refresh(in,hp); Work solo_hold; to_hold(solo_hold,in,hp);
    next(in,hp,{3,0}); in.frame.actor_count = 0; refresh(in,hp); c = solo_hold.step(in);
    check(c.action == PhysicalAction::hold && solo_hold.active,"safe solo holder remains when a helper disappears");

    // A helper's evidence is only its own present visible proposer/holder. No
    // remote qualification field exists, and no proposed arrival is sufficient.
    auto helper_plan = joint(); helper_plan.pads[0] = {2,1}; helper_plan.pads[1] = {2,0};
    helper_plan.release = path({{3,0},{4,1},{5,1}});
    in = frame(helper_plan,2); refresh(in,helper_plan); Work helper;
    check(helper.start(helper_plan,in),"helper admits from personally visible named collector proposer");
    helper.step(in); next(in,helper_plan,{1,0}); helper.step(in); next(in,helper_plan,{2,0}); helper.step(in);
    next(in,helper_plan,{2,0}); in.frame.group.value.lifecycle = p6::Lifecycle::reward_available; helper.step(in);
    next(in,helper_plan,{3,0}); in.frame.own.actor.spawn_group = p.group;
    c = helper.step(in);
    check(c.action == PhysicalAction::hold && !helper.helper_released,"predicted holder without observed spawn occupancy does not release helper");
    next(in,helper_plan,{3,0}); in.frame.actors[0].value.spawn_group = p.group;
    in.frame.actors[0].value.position = {3,1}; refresh(in,helper_plan);
    // The remaining-holder certificate is fresh and individually safe.
    c = helper.step(in);
    check(c.action == PhysicalAction::move && c.target == p6::Point{4,1},"actual safe holder on valid spawn licenses physical helper withdrawal");
    next(in,helper_plan,{4,1}); in.frame.own.actor.spawn_group = -1; c = helper.step(in);
    check(c.action == PhysicalAction::move && c.target == p6::Point{5,1},"multi-step release continues its own retained route");
    next(in,helper_plan,{5,1}); helper.step(in);
    check(helper.helper_released && helper.renewal_seen && !helper.cycle_completed,
          "observed helper departure renews independent work without claiming collector payout");

    auto jp = joint(Organization::J); jp.ingress = path({{0,0}}); jp.capture = path({{0,0},{1,0},{2,0},{3,0}});
    jp.pads[0] = {0,0}; jp.pads[1] = {0,1};
    in = frame(jp); in.frame.actors[0].value.position = {4,1}; refresh(in,jp); Work assembly;
    check(assembly.start(jp,in),"control J can begin safe visible bounded assembly");
    for (int turn = 0; turn < 8; ++turn) {
        c = assembly.step(in);
        if (turn < 7) { check(c.action == PhysicalAction::hold,"assembly waits without inventing arrival"); next(in,jp,{0,0}); }
    }
    check(!assembly.active && assembly.assembly_turns == 8,"J abandons absent assembly at eight owner turns");

}
__attribute__((noinline)) void diverse_collectors() {
    auto p = joint(); auto in = frame(p);
    in.frame.own.actor.type = p6::Champion::vendetta; refresh(in,p); Work collector;
    check(collector.start(p,in),"user-authorized diversified named collector is not restricted to a prescribed champion");
    p.pads[0] = {2,1}; p.pads[1] = {2,0}; p.release = path({{3,0},{4,1}});
    in = frame(p,2); in.frame.actors[0].value.id = 3; in.frame.actors[0].subject = 3;
    refresh(in,p); Work wrong_name;
    check(!wrong_name.start(p,in),"visible unrelated champion cannot impersonate the named collector");
    in = frame(p,2); in.frame.actor_count = 0; refresh(in,p); Work hidden;
    check(!hidden.start(p,in),"missing named collector cannot admit helper through private or remote evidence");
    for (const auto team : {p6::Team::enemy,p6::Team::unknown}) {
        in = frame(p,2); in.frame.actors[0].value.team = team; refresh(in,p); Work outsider;
        check(!outsider.start(p,in),"enemy or unknown-team named proposer cannot admit an allied helper");
    }
}
__attribute__((noinline)) void helper_already_outside_capture() {
    auto p=joint();p.pads[0]={2,1};p.pads[1]={2,0};
    p.ingress=path({{2,0}});p.capture=path({{2,0}});
    p.bank=path({{2,0},{3,0},{4,0},{5,0}});
    p.release=path({{2,0},{2,1},{1,1}});
    auto in=frame(p,2);refresh(in,p);Work w;
    check(w.start(p,in),"off-region helper admits its retained clear/release paths");
    auto c=w.step(in);check(c.action==PhysicalAction::attack,"helper actually contributes from its separate stationary pad");
    w.confirm(c,receipt(c));next(in,p,{2,0});in.frame.group.value.lifecycle=p6::Lifecycle::reward_available;
    c=w.step(in);
    check(c.action==PhysicalAction::hold && w.active && !w.release_admitted,"off-region helper waits for an actually observed safe holder");
    next(in,p,{2,0});in.frame.actors[0].value.position={3,1};in.frame.actors[0].value.spawn_group=p.group;refresh(in,p);
    c=w.step(in);
    check(c.action==PhysicalAction::move && c.target==p6::Point{2,1} && w.release_admitted,"helper leaves directly without entering a capture tile or diluting its collector reward");
    next(in,p,{2,1});c=w.step(in);check(c.target==p6::Point{1,1},"off-region helper follows its exact productive withdrawal route");
    next(in,p,{1,1});w.step(in);
    check(w.helper_released && w.renewal_seen && !w.cycle_completed && !w.payout_seen,"helper resumes useful independent work without fabricated capture or tribute proof");
}
__attribute__((noinline)) void rollback_payout() {
    auto p = solo(); auto in = frame(p); refresh(in,p); Work w; to_hold(w,in,p);
    w.request_stop(); next(in,p,{3,0});
    in.frame.group.value.lifecycle = p6::Lifecycle::respawning; payout(in,p.group,12);
    const auto c = w.step(in);
    check(w.payout_seen && w.payout_lower == 12 && c.action == PhysicalAction::move &&
          c.target == p6::Point{4,0} && w.stage == PhysicalStage::bank,
          "safe rollback retains current payout receipt before switching to own bag conversion");
}
__attribute__((noinline)) void joint_disappearance() {
    auto p = joint(Organization::J); auto in = frame(p); refresh(in,p); Work w;
    to_hold(w,in,p);
    next(in,p,{3,0}); payout(in,p.group,12); auto c = w.step(in);
    check(c.target == p6::Point{4,0} && w.route_witness_active,"persistent J starts observed loaded return with visible members");
    next(in,p,{4,0}); in.frame.own.actor.spawn_group = -1;
    in.frame.actor_count = 0; refresh(in,p); c = w.step(in);
    check(c.action == PhysicalAction::move && c.target == p6::Point{5,0} &&
          w.failure == PhysicalFailure::missing_member && !w.route_witness_active,
          "missing J member withdraws convoy claim while safe solo bag follows retained bank path");
    next(in,p,{5,0}); c = w.step(in); check(w.confirm(c,receipt(c)),"fallback own tribute actually starts");
    next(in,p,{5,0}); in.frame.own.actor.channel = p6::Channel::tribute;
    in.frame.own.actor.channel_valid = true; c = w.step(in);
    check(c.action == PhysicalAction::preserve_channel,"lost J contact never cancels a safe actual tribute");
    next(in,p,{5,0}); in.frame.own.actor.channel = p6::Channel::none;
    in.frame.own.actor.bag = 0;
    in.milestones = {{p.group,false,true,0,12},in.frame.clock,1,p6::Knowledge::exact};
    w.step(in); next(in,p,{6,0}); w.step(in); next(in,p,{7,0}); w.step(in);
    check(w.bank_seen && w.renewal_seen && !w.cycle_completed,"safe independent completion after convoy loss cannot qualify J");
}
__attribute__((noinline)) void startup_cases() {
    // Startup uses the same eight-group/two-altar bounded menu in both arms.
    const auto p = solo(); auto in = frame(p); refresh(in,p); Startup startup;
    std::array<StartupCandidate,2> menu{{{p,30,4,true,true},{solo(Organization::G),20,4,true,true}}};
    auto decision = startup.step(in,menu,2);
    check(decision.kind == StartupKind::solo && decision.plan.organization == Organization::G,
          "startup selects cheapest currently verified solo complete cycle");
    // This rejected-capacity object is never an admitted runtime pool. Keep it
    // off the default WASI stack; production represents at most eight groups.
    static const std::array<StartupCandidate,9> oversized{}; Startup cap;
    check(cap.step(in,oversized,2).kind == StartupKind::waiting,"ninth represented group rejected instead of silently truncated");
    Startup altar_cap; check(altar_cap.step(in,menu,3).kind == StartupKind::waiting,"third represented altar rejected");
    Startup inspect; menu[0].currently_verified = menu[1].currently_verified = false;
    menu[1].static_distance = 8;
    for (int turn = 0; turn < 8; ++turn) {
        decision = inspect.step(in,menu,2);
        check(decision.kind == StartupKind::inspect && decision.plan.group == p.group,"startup inspects one nearest retained static route within its owner-turn bound");
        const auto pos = turn == 0 ? p6::Point{1,0} : p6::Point{2,0}; next(in,p,pos);
    }
    decision = inspect.step(in,menu,2);
    check(decision.kind == StartupKind::recover && inspect.opportunity_unavailable,"unverified first income stops after eight additional inspection turns");
    for (int wait = 0; wait < 7; ++wait) {
        next(in,p,{2,0}); decision = inspect.step(in,menu,2);
        check(decision.kind == StartupKind::waiting,"unavailable startup rescans no faster than eight owner turns");
    }
    next(in,p,{2,0}); decision = inspect.step(in,menu,2);
    check(decision.kind == StartupKind::recover,"rescan without a new legal route stays unavailable");
    Startup safety; in = frame(p); refresh(in,p); in.legal.value.safe_now = false;
    in.legal.value.escape_legal = true; in.legal.value.escape_tile = {0,1};
    decision = safety.step(in,menu,2);
    check(decision.action == PhysicalAction::escape && safety.inspection_turns == 0,
          "startup inspection cannot override immediate safety");
    Startup fuel; in.legal.value.safe_now = true; in.fuel_remaining = 0;
    decision = fuel.step(in,menu,2);
    check(decision.kind == StartupKind::waiting && decision.action == PhysicalAction::yield,
          "startup cannot start route search or inspection below current fuel reserve");
}
}
void friendly_bank_queue() {
    const auto p=solo();
    auto prepare=[&](LocalFrame& in,Work& work) {
        in=frame(p);refresh(in,p);check(work.start(p,in),"bank queue fixture starts only from an admitted retained route");
        in.frame.own.actor.position=p.bank.tiles[0];in.frame.own.actor.bag=12;
        p6::Actor ally=in.frame.own.actor;ally.id=9;ally.position=p.bank.tiles[1];
        in.frame.actors[in.frame.actor_count++]={ally,in.frame.clock,9,p6::Knowledge::exact};
        work.stage=PhysicalStage::bank;refresh(in,p);
        auto& step=in.legal.value.routes[int(Leg::bank)];step.next_move_legal=false;step.friendly_queue=true;
    };
    {
        LocalFrame in;Work work;prepare(in,work);
        const auto waiting=work.step(in);
        check(waiting.action==PhysicalAction::hold&&work.active&&work.bank_queue_turns==1&&work.route_witness_active,
              "personally visible friendly bank occupancy waits without invalidating its physical episode");
        next(in,p,p.bank.tiles[0]);in.frame.actors[0].value.position={4,1};
        const auto released=work.step(in);
        check(released.action==PhysicalAction::move&&released.target==p.bank.tiles[1]&&work.bank_queue_turns==1,
              "actual friendly departure releases the next retained bank step without a guessed arrival");
    }
    {
        LocalFrame in;Work work;prepare(in,work);bool bounded=true;
        for(int turn=0;turn<p.forecast_rounds;++turn) {
            auto& step=in.legal.value.routes[int(Leg::bank)];step.next_move_legal=false;step.friendly_queue=true;
            bounded&=work.step(in).action==PhysicalAction::hold&&work.active;
            next(in,p,p.bank.tiles[0]);
        }
        auto& step=in.legal.value.routes[int(Leg::bank)];step.next_move_legal=false;step.friendly_queue=true;
        work.step(in);
        check(bounded&&!work.active&&work.bank_queue_turns==p.forecast_rounds&&work.failure==PhysicalFailure::blocked_route,
              "friendly bank waiting cannot exceed the bounded planned owner-turn allowance");
    }
    {
        LocalFrame in;Work work;prepare(in,work);
        in.frame.actors[0].knowledge=p6::Knowledge::unknown;
        work.step(in);
        check(!work.active&&work.failure==PhysicalFailure::blocked_route&&work.bank_queue_turns==0,
              "a queue flag without a personally visible allied occupant does not excuse an unknown blocker");
    }
    {
        LocalFrame in;Work work;prepare(in,work);
        in.legal.value.safe_now=false;in.legal.value.escape_legal=true;in.legal.value.escape_tile={3,1};
        const auto emergency=work.step(in);
        check(emergency.action==PhysicalAction::escape&&!work.active&&work.bank_queue_turns==0,
              "immediate safety overrides friendly bank waiting");
    }
}
int main() {
    {
        const auto p=solo();auto in=frame(p);refresh(in,p);Work work;
        check(work.start(p,in),"first job starts from a personally fresh frame");
        const auto first=work.step(in);
        check(work.confirm(first,receipt(first,false))&&!work.active,"actual blocker ends only the first job");
        const auto generation=work.generation,serial=work.serial;
        check(!work.start(p,in),"a new generation cannot issue a second action in the same owner turn");
        in.frame.clock.round+=5;refresh(in,p);
        check(work.start(p,in)&&work.generation==generation+1&&work.serial==serial,
            "fresh job after inactive rounds retains monotonic generation and receipt history");
        const auto next_job=work.step(in);
        check(next_job.action==PhysicalAction::move&&work.route_witness_active&&
            work.failure==PhysicalFailure::none&&next_job.token>first.token,
            "inactive gap is not a stale continuation or a reset receipt sequence");
        check(work.confirm(next_job,receipt(next_job)),"new job still requires its actual own action receipt");
        in.frame.clock.round+=2;in.frame.own.actor.position={1,0};refresh(in,p);work.step(in);
        check(work.failure==PhysicalFailure::stale&&!work.route_witness_active,
            "skipping a round within an active job still invalidates its physical witness");
    }
    full_cycle(Organization::U); full_cycle(Organization::G);
    full_cycle(Organization::H); full_cycle(Organization::J);
    full_cycle(Organization::U,false); full_cycle(Organization::H,false,true);
    unrecorded_movement_identity(); routes_and_interruptions(); cooperation(); diverse_collectors(); helper_already_outside_capture(); rollback_payout();
    joint_disappearance(); startup_cases(); friendly_bank_queue();
    std::printf("Plan8 physical organization: %d explicit checks passed; pure offline only.\n",checks);
}
