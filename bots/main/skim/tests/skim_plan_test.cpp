#include "../src/skim_plan.h"

#include <array>
#include <cstdint>

namespace {

using mb::skim::Resources;

constexpr Resources ready()
{
    Resources r{};
    r.ordinary_farm = r.melee = r.alive = true;
    r.can_move = r.can_attack = r.quiet = r.healthy = true;
    return r;
}

constexpr bool with_resource(bool Resources::*field, bool value)
{
    auto r = ready();
    r.*field = value;
    return mb::skim::may_propose(r);
}

static_assert(mb::skim::may_propose(ready()), "ordinary healthy melee FARM may propose");
static_assert(!with_resource(&Resources::ordinary_farm, false), "no fight, escort or other phase");
static_assert(!with_resource(&Resources::melee, false), "a spare basic must preserve walking");
static_assert(!with_resource(&Resources::alive, false), "dead actors cannot act");
static_assert(!with_resource(&Resources::channeling, true), "do not cancel a channel");
static_assert(!with_resource(&Resources::stunned, true), "stun excludes a proposal");
static_assert(!with_resource(&Resources::capturing, true), "do not disturb capture");
static_assert(!with_resource(&Resources::moved, true), "do not add a second movement phase");
static_assert(!with_resource(&Resources::attacked, true), "do not spend a second basic");
static_assert(!with_resource(&Resources::cast, true), "spent actions exclude incidental work");
static_assert(!with_resource(&Resources::can_move, false), "the original walk must remain available");
static_assert(!with_resource(&Resources::can_attack, false), "basic must be live-ready");
static_assert(!with_resource(&Resources::quiet, false), "threat handling keeps priority");
static_assert(!with_resource(&Resources::healthy, false), "health handling keeps priority");

constexpr bool candidate(int group = 2, int own = 1, bool visible = true,
                         bool awake = true, float hp = 10.0f, int distance = 1,
                         int range = 1, bool assisted = true)
{
    return mb::skim::eligible(group, own, visible, awake, hp, distance, range, assisted);
}

static_assert(candidate(), "adjacent awake assisted other-group camp is eligible");
static_assert(!candidate(-1), "invalid group is not a task");
static_assert(!candidate(1), "ordinary target combat owns its group");
static_assert(!candidate(2, 1, false), "personal vision is required");
static_assert(!candidate(2, 1, true, false), "never wake an incidental camp");
static_assert(!candidate(2, 1, true, true, 0.0f), "dead camp cannot receive a basic");
static_assert(!candidate(2, 1, true, true, -1.0f), "unknown or invalid HP is rejected");
static_assert(!candidate(2, 1, true, true, 10.0f, 2), "range-one basic cannot reach distance two");
static_assert(!candidate(2, 1, true, true, 10.0f, 1, 1, false), "published ally assistance is required");

constexpr std::uint64_t start = 50000;
constexpr std::uint64_t reserve = 30000;
static_assert(mb::skim::budget_open(start, start, reserve), "entry can begin within reserve");
static_assert(mb::skim::budget_open(start, start - mb::skim::kProposalBudget + 1, reserve),
              "last fuel unit before cutoff remains available");
static_assert(!mb::skim::budget_open(start, start - mb::skim::kProposalBudget, reserve),
              "exact proposal budget is exhausted");
static_assert(!mb::skim::budget_open(start, start - mb::skim::kProposalBudget - 1, reserve),
              "overspent proposal must stop");
static_assert(!mb::skim::budget_open(start, reserve, reserve), "never spend the walk reserve");
static_assert(!mb::skim::budget_open(start, reserve - 1, reserve), "reserve breach stops immediately");
static_assert(!mb::skim::budget_open(start, start + 1, reserve), "fuel increase rejects an invalid sample");

struct Camp
{
    float hp;
    std::uint32_t id;
};

template<std::size_t N>
constexpr std::uint32_t select(const std::array<Camp, N>& camps)
{
    auto best = camps[0];
    for (std::size_t i = 1; i < N; ++i)
        if (mb::skim::better(camps[i].hp, camps[i].id, best.hp, best.id)) best = camps[i];
    return best.id;
}

static_assert(mb::skim::better(5.0f, 99, 6.0f, 1), "lower HP wins before identity");
static_assert(!mb::skim::better(6.0f, 1, 5.0f, 99), "identity cannot outweigh HP");
static_assert(mb::skim::better(5.0f, 2, 5.0f, 3), "lower stable ID resolves equal HP");
static_assert(!mb::skim::better(5.0f, 2, 5.0f, 2), "identical candidate does not replace itself");
static_assert(select(std::array<Camp, 4>{{ {8.0f, 1}, {5.0f, 7}, {5.0f, 3}, {6.0f, 2} }}) == 3,
              "selection chooses lowest HP then lowest ID");
static_assert(select(std::array<Camp, 4>{{ {6.0f, 2}, {5.0f, 3}, {5.0f, 7}, {8.0f, 1} }}) == 3,
              "selection is independent of observation order");

} // namespace

int main() {}
