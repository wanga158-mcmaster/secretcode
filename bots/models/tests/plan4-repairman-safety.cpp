#include "../repairman/src/repairman.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace s = plan4::safe_segment;
namespace r = plan4::repairman;
int checks = 0;
void check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
struct Fixture {
    std::array<std::uint8_t,32> visible{}, occupied{};
    s::AttackContext actor;
    s::EscapeScene scene;
    Settings settings;
    bool corridor = true;
    Fixture() {
        visible.fill(255); actor.observed_round = 10; actor.actor_id = 4; actor.target_id = 30;
        actor.position = {1,0}; actor.health = 500; actor.maximum_health = 1000;
        actor.predicted_incoming = 20; actor.already_attackable = true;
        scene.observed_round = 10; scene.actor_id = 4; scene.topology_complete = true;
        scene.walkable = walkable; scene.topology_context = this;
        scene.occupancy = {16,16,visible.data(),occupied.data(),visible.size(),true};
        scene.threat_count = 1; scene.relevant_threats_complete = true;
        scene.threats[0] = {30,3,{0,0},20,{10,Source::personal_vision,Status::exact_now},true,true};
        occupied[0] = 1;
        scene.enemies = {{10,Source::personal_vision,Status::exact_now},true,true};
        scene.own_spawn = {9,0}; scene.physical_spawn_known = scene.recall_arrival_supported = true;
        scene.spawn_quote = {10,Source::own_state,Status::derived};
    }
    static bool walkable(Point p,const void* c) {
        return p.x>=0 && p.x<16 && p.y>=0 && p.y<16 && (!static_cast<const Fixture*>(c)->corridor || p.y==0);
    }
};
struct Budget { int calls = 0; static bool available(const void* c) { return ++const_cast<Budget*>(static_cast<const Budget*>(c))->calls < 5; } };
int main() {
    static_assert(std::is_same_v<r::AttackContext,s::AttackContext>);
    static_assert(std::is_same_v<r::EscapeProof,s::EscapeProof>);
    auto f = std::make_unique<Fixture>(); auto built = std::make_unique<s::EscapeBuild>();
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::none && built->proof.supported && built->path_count==3 && built->vertices<=64,
          "bounded eight-neighbor search finds an observed escape in three ordinary moves");
    check(built->path[0]==Point{2,0} && built->path[1]==Point{3,0} && built->path[2]==Point{4,0},
          "returned path contains each legal edge after the current actor tile");
    check(built->proof.escape[0].incoming==20 && built->proof.escape[1].incoming==20 && built->proof.escape[2].incoming==0,
          "every following camp phase is quoted separately instead of using only attack exposure");
    const auto quote = s::safe_segment_quote(f->actor,built->proof,f->settings);
    check(quote.attacks==4 && quote.health_at_recall_arrival==360 && quote.elapsed_rounds==13,
          "k-plus-one reserve, three escape phases and real recall countdown are conserved");
    const auto restored = s::quote_restore(f->actor,quote,*built,f->settings,10);
    check(restored.supported && restored.destination==Point{9,0} && restored.arrival_round==23 &&
          restored.healing_turn_ends==4 && restored.completion_round==27 && restored.final_health==1000,
          "restore ends at physical spawn only after the queried healing turn ends");
    f->settings.max_rounds=26;
    check(!s::quote_restore(f->actor,quote,*built,f->settings,10).supported,"cutoff cannot expose full health before the last spawn heal");
    f->settings.max_rounds=std::numeric_limits<int>::max();
    check(!s::quote_restore(f->actor,quote,*built,f->settings,std::numeric_limits<int>::max()-quote.elapsed_rounds).supported,
          "a final healing duration cannot wrap the round counter past the match cutoff");
    f->settings.max_rounds=2000;
    f->visible[0] &= std::uint8_t(~4u);
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(!built->proof.supported && built->failure==s::BuildFailure::exhausted,"unknown intermediate occupancy is never treated as a free route");
    f = std::make_unique<Fixture>(); f->occupied[0] |= 4u;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(!built->proof.supported,"a fresh occupied connector does not become passable");
    f = std::make_unique<Fixture>(); f->occupied[0] |= 2u;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->proof.supported,"fresh own body is exempted from its own occupancy bit");
    f->scene.threats[0].provenance.observed_round=9;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::threats && !built->proof.supported,"stale threat damage cannot certify a current escape");
    f = std::make_unique<Fixture>(); f->scene.relevant_threats_complete=false;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::threats,"an incomplete public-composition join remains unsupported");
    f = std::make_unique<Fixture>(); f->actor.predicted_incoming=19;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::threats,"stationary admission cannot underquote the fresh current threat sum");
    f = std::make_unique<Fixture>(); f->scene.enemies.visible_set_complete=false;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::enemies,"unknown visible enemy enumeration is not an exclusion proof");
    f = std::make_unique<Fixture>(); f->scene.enemies.visible_threats_excluded=false;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::enemies,"a visible unresolved enemy prevents stationary safe recall");
    f = std::make_unique<Fixture>(); f->scene.enemies.provenance.observed_round=9;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::enemies,"previous-round enemy absence cannot authorize current escape");
    f = std::make_unique<Fixture>(); f->scene.recall_arrival_supported=false;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::spawn,"unavailable recall arrival premise cannot create a spawn endpoint");
    f = std::make_unique<Fixture>(); f->scene.occupancy.bytes=0;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::occupancy,"packed bitmap size is checked before indexing");
    f = std::make_unique<Fixture>(); f->settings.recall_rounds=9;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->proof.recall_ticks==9 && built->proof.recall_phase_count==9 && built->proof.recall[8].incoming==0,
          "custom queried recall duration contributes all damage-free channel phases");
    f->actor.legal_recall_ticks=3; f->actor.recall_quote={10,Source::own_state,Status::derived};
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->proof.recall_ticks==3,"fresh actual kit quote can adjust recall without assuming an optional ability");
    f = std::make_unique<Fixture>(); f->scene.threats[0].attack_range=10;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::exhausted && built->vertices<=64 && !built->proof.supported,
          "a safe tile beyond depth eight does not escape the strict search bound");
    f->corridor=false; f->scene.threats[0].attack_range=128;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::exhausted && built->vertices==64,"wide searches stop at exactly the 64-vertex allowance");
    f = std::make_unique<Fixture>(); Budget budget; f->actor.work_budget={Budget::available,&budget};
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->failure==s::BuildFailure::budget && !built->proof.supported && budget.calls==5,
          "budget loss refuses the whole partial proof without more optional work");
    f = std::make_unique<Fixture>(); f->scene.threat_count=0; f->actor.predicted_incoming=0;
    s::build_escape_into(f->actor,f->settings,f->scene,*built);
    check(built->proof.supported && built->path_count==0 && built->proof.recall_ticks==5,
          "already safe posture permits a zero-escape recall under explicit complete threat premises");
    std::printf("plan4-repairman-safety: %d checks passed (synthetic immutable inputs only)\n",checks);
}
