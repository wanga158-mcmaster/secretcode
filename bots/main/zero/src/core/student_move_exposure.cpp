#include "student_move_exposure.h"
#include "catalog.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zero::student {
namespace {

void require(bool valid, const char* message)
{
    if (!valid) throw std::invalid_argument(message);
}

void add_strength(double& sum, float strength)
{
    sum += static_cast<double>(strength);
    if (!std::isfinite(sum)) throw std::overflow_error("MOVE coverage Strength sum overflow");
}

void increment(std::uint64_t& count)
{
    if (count == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("MOVE coverage count overflow");
    ++count;
}

}  // namespace

void move_observed_design_coverage(const Context& ctx, std::span<const Cand> moves,
                                   std::vector<MoveObservedDesignCoverage>& out)
{
    out.clear();
    require(ctx.map && ctx.obs, "MOVE coverage requires Map and Obs");
    const auto& map = *ctx.map;
    const auto& obs = *ctx.obs;
    const auto& cat = catalog();
    require(map.w > 0 && map.h > 0, "MOVE coverage requires positive map dimensions");
    const auto width = static_cast<std::size_t>(map.w);
    const auto height = static_cast<std::size_t>(map.h);
    require(width <= std::numeric_limits<std::size_t>::max() / height &&
            map.flags.size() == width * height, "MOVE coverage map shape mismatch");
    require(map.inside(obs.self.at), "MOVE coverage self coordinate outside map");
    require(obs.self.type < cat.n_champions, "MOVE coverage self type outside catalog");
    require(std::isfinite(obs.self.hp), "MOVE coverage nonfinite self HP");

    // The complete ordinary destination set lies in this fixed geometric neighbourhood. This mask
    // only checks membership in the supplied observation; it does not recompute movement legality.
    bool observed_step[3][3]{};
    for (const Tile tile : obs.step_ok)
    {
        require(map.inside(tile) && cheb(tile, obs.self.at) == 1,
                "MOVE coverage observed destination is not an inside adjacent tile");
        observed_step[tile.y - obs.self.at.y + 1][tile.x - obs.self.at.x + 1] = true;
    }

    // Validate every supplied source before aggregation, even if dead, asleep or outside every row's
    // range. Catalog indexing therefore never depends on a source's inclusion in a particular sum.
    for (const auto& enemy : obs.enemies)
    {
        require(map.inside(enemy.at), "MOVE coverage enemy coordinate outside map");
        require(enemy.type < cat.n_champions, "MOVE coverage enemy type outside catalog");
        require(std::isfinite(enemy.hp) && std::isfinite(enemy.strength),
                "MOVE coverage nonfinite enemy HP or Strength");
        require(std::isfinite(cat.champion(enemy.type).attack_range),
                "MOVE coverage nonfinite enemy design range");
    }
    for (const auto& camp : obs.camps)
    {
        require(map.inside(camp.at), "MOVE coverage camp coordinate outside map");
        require(camp.type < cat.n_camps, "MOVE coverage camp type outside catalog");
        const auto& design = cat.camp(camp.type);
        require(std::isfinite(camp.hp), "MOVE coverage nonfinite camp HP");
        require(std::isfinite(design.attack_range) && std::isfinite(design.strength),
                "MOVE coverage nonfinite camp design range or Strength");
    }

    std::vector<MoveObservedDesignCoverage> complete;
    complete.reserve(moves.size());
    for (const auto& move : moves)
    {
        require(move.kind == 0 && move.target == -1 && move.count == 1,
                "MOVE coverage invalid MOVE payload");
        require(map.inside(move.at) && cheb(move.at, obs.self.at) == 1,
                "MOVE coverage destination is not an inside adjacent tile");
        require(observed_step[move.at.y - obs.self.at.y + 1][move.at.x - obs.self.at.x + 1],
                "MOVE coverage destination absent from observed legal MOVE set");
        for (const float value : move.phi)
            require(std::isfinite(value), "MOVE coverage nonfinite MOVE descriptor");

        complete.emplace_back();
        auto& row = complete.back();
        row.move = move;
        for (const auto& enemy : obs.enemies)
            if (enemy.hp > 0.0f && static_cast<float>(cheb(enemy.at, move.at)) <=
                                      cat.champion(enemy.type).attack_range)
            {
                increment(row.living_visible_enemy_champions_in_design_range);
                add_strength(row.observed_enemy_strength_sum, enemy.strength);
            }
        for (const auto& camp : obs.camps)
            if (camp.hp > 0.0f && camp.awake && static_cast<float>(cheb(camp.at, move.at)) <=
                                                 cat.camp(camp.type).attack_range)
            {
                increment(row.living_visible_awake_camps_in_design_range);
                add_strength(row.queried_awake_camp_base_strength_sum, cat.camp(camp.type).strength);
            }
    }
    out.swap(complete);
}

}  // namespace zero::student
