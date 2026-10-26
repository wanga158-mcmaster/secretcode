// Draft: pick by a fixed preference order adjusted by map features.
//
// Mechanics assumed (GameRules "Drafting"): picks are simultaneous and one at a time; our
// own picks must be distinct; the opponent's picks do not restrict ours; Cassi & Issac is
// one pick that fields two champions. A missing pick is filled randomly by the game.
#include "draft.h"
#include "config.h"
#include <algorithm>
#include <array>
#include <cambc.h>
#include <vector>

namespace mb {

namespace {

using cambc::champion_draft;

std::vector<champion_draft> preference()
{
    // Map feature: how clustered the camps are (splash and area damage value).
    float neighbours = 0.0f;
    int members = 0;
    for (const auto& g : cambc::map::camp_groups())
    {
        const auto camps = g.camps();
        for (std::size_t i = 0; i < camps.size(); ++i)
        {
            ++members;
            for (std::size_t j = 0; j < camps.size(); ++j)
                if (i != j && camps[i].spawn_position().chebyshev_distance(camps[j].spawn_position()) <= 1) neighbours += 1.0f;
        }
    }
    const float clustered = members ? neighbours / static_cast<float>(members) : 0.0f;
    if (cfg::draft_mode == 1)
        return { champion_draft::heart_of_ruin, champion_draft::ryth, champion_draft::marcella, champion_draft::ren,
                 champion_draft::stellar, champion_draft::vesper_vale, champion_draft::vendetta, champion_draft::varg,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (cfg::draft_mode == 3)
        return { champion_draft::vendetta, champion_draft::heart_of_ruin, champion_draft::vesper_vale, champion_draft::varg,
                 champion_draft::ryth, champion_draft::marcella, champion_draft::ren, champion_draft::stellar,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (cfg::draft_mode == 7 || cfg::draft_mode == 8)  // mode 2 with Ren / Varg as the fifth pick
        return { champion_draft::heart_of_ruin, champion_draft::ryth, champion_draft::marcella, champion_draft::vendetta,
                 cfg::draft_mode == 7 ? champion_draft::ren : champion_draft::varg, champion_draft::vesper_vale, champion_draft::stellar,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (cfg::draft_mode == 2)
        return { champion_draft::heart_of_ruin, champion_draft::marcella, champion_draft::ryth, champion_draft::vendetta,
                 champion_draft::vesper_vale, champion_draft::ren, champion_draft::stellar, champion_draft::varg,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    std::vector<champion_draft> order{ champion_draft::cassi_and_issac };
    if (clustered >= 1.0f)
        order.insert(order.end(), { champion_draft::varg, champion_draft::vendetta });
    else
        order.insert(order.end(), { champion_draft::vendetta, champion_draft::varg });
    order.insert(order.end(), { champion_draft::tavi, champion_draft::marcella, champion_draft::vesper_vale,
                                champion_draft::ryth, champion_draft::stellar, champion_draft::willow,
                                champion_draft::prism, champion_draft::ren, champion_draft::heart_of_ruin });
    return order;
}

} // namespace

void play_draft()
{
    static const std::vector<champion_draft> order = preference();
    const auto mine = cambc::draft::current_draft(cambc::team::ally);
    for (const auto pick : order)
    {
        if (std::find(mine.begin(), mine.end(), pick) != mine.end()) continue;
        try
        {
            cambc::draft::pick(pick);
            return;
        }
        catch (const cambc::error&)
        {
            // Rejected (e.g. duplicate): try the next preference.
        }
    }
}

} // namespace mb
