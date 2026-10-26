#include <cassert>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include "../common/src/planning_epoch.h"
#include "../capital/src/policy.h"
#include "../contingency/src/policy.h"

namespace {
bool same(p11_capital::Purchase a,p11_capital::Purchase b) {
    return a.project==b.project&&a.count==b.count;
}
}

int main() {
    // Use the actual Capital policy with a job whose integer hit threshold
    // requires the two-shard bundle. The wrapper does not receive a canned
    // purchase, and publication happens only after its final epoch read.
    p11_capital::Input capital;
    capital.strength=100;capital.maximum=1000;
    capital.strength_growth=10;capital.gold=60;
    std::array<p11_capital::Job,1> jobs{};
    jobs[0].count=1;jobs[0].monsters[0]={240,0,0};
    const p11_capital::Purchase expected{p11_capital::Project::monster,2};
    int round=41,reads=0,decisions=0;
    auto read_round=[&] {++reads;return round;};
    auto purchase=[&] {++decisions;return p11_capital::Policy::choose_capital(capital,jobs);};
    auto stable=p11::plan_in_round(41,read_round,purchase);
    assert(stable&&same(*stable,expected));
    assert(reads==2&&decisions==1);
    p11_capital::Purchase published{};
    if(stable)published=*stable;
    assert(same(published,expected));

    round=42;reads=decisions=0;
    const auto initially_stale=p11::plan_in_round(41,read_round,purchase);
    assert(!initially_stale&&reads==1&&decisions==0);

    // An expensive policy may finish on a later turn after suspension. Its
    // locally computed answer is positive, but cannot become a live purchase.
    round=41;reads=decisions=0;published={};
    p11_capital::Purchase computed{};
    const auto interrupted_purchase=p11::plan_in_round(41,read_round,[&] {
        computed=purchase();++round;return computed;
    });
    if(interrupted_purchase)published=*interrupted_purchase;
    assert(same(computed,expected)&&!interrupted_purchase);
    assert(same(published,{})&&reads==2&&decisions==1);

    // Run the common selector used by the SDK farm caller over the complete
    // menu. Both entries are feasible; the larger capture payout wins.
    p11::State state;state.round=41;state.field_shop=true;
    std::array<p11::Candidate,2> farms{};
    farms[0].id=0;farms[0].gold=10;farms[0].capture=1;
    farms[1].id=7;farms[1].gold=30;farms[1].capture=1;
    auto farm=[&] {++decisions;return p11::choose<p11_tempo::Policy>(state,farms);};
    round=41;reads=decisions=0;
    const auto stable_farm=p11::plan_in_round(41,read_round,farm);
    assert(stable_farm&&*stable_farm==7&&reads==2&&decisions==1);

    // Inject advancement at the final SDK read itself, rather than inside
    // the decision callback. The otherwise valid farm cannot be published.
    reads=decisions=0;
    int selected=99;
    const auto advanced_at_read=p11::plan_in_round(41,[&] {
        if(++reads==2)++round;
        return round;
    },farm);
    if(advanced_at_read)selected=*advanced_at_read;
    assert(!advanced_at_read&&selected==99&&reads==2&&decisions==1);

    round=41;reads=decisions=0;
    const auto interrupted_farm=p11::plan_in_round(41,read_round,[&] {
        const int answer=farm();++round;return answer;
    });
    assert(!interrupted_farm&&reads==2&&decisions==1);

    // Candidate zero is a real job, and negative one is a real private wait.
    // Neither value may be mistaken for a missing/stale optional result.
    round=41;farms[0].gold=50;
    const auto zero=p11::plan_in_round(41,read_round,farm);
    assert(zero&&*zero==0);
    const auto empty=p11::plan_in_round(41,read_round,[&] {
        return p11::choose<p11_tempo::Policy>(state,std::span<const p11::Candidate>{});
    });
    assert(empty&&*empty==-1);
    std::array<p11_contingency::RouteJob,1> wait_menu{};
    wait_menu[0].candidate.id=4;
    const auto private_wait=p11::plan_in_round(41,read_round,[&] {
        return p11_contingency::Policy::select_route(state,wait_menu);
    });
    assert(private_wait&&*private_wait==-1);

    // Exceptions must still reach the existing turn-boundary handler. A
    // failed decision is not converted into a valid default/wait action.
    reads=0;bool caught=false;
    try {
        (void)p11::plan_in_round(41,read_round,[]() -> int {
            throw std::runtime_error("injected planning failure");
        });
    } catch(const std::runtime_error&) {caught=true;}
    assert(caught&&reads==1);

    const auto moved=p11::plan_in_round(41,read_round,[] {
        return std::make_unique<int>(73);
    });
    static_assert(std::is_same_v<std::remove_cv_t<decltype(moved)>,std::optional<std::unique_ptr<int>>>);
    assert(moved&&*moved&&**moved==73);
    // These injected epoch changes verify publication safety only; no native
    // engine launch or live fuel-budget fit is represented by this fixture.
}
