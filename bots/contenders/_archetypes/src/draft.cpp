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
    // Maps fielding at most four champions may use their own order (cfg::small_draft_mode).
    const int mode = cfg::small_draft_mode > 0 && cambc::draft::team_size() <= 4 ? cfg::small_draft_mode : cfg::draft_mode;
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
    if (mode == 1)
        return { champion_draft::heart_of_ruin, champion_draft::ryth, champion_draft::marcella, champion_draft::ren,
                 champion_draft::stellar, champion_draft::vesper_vale, champion_draft::vendetta, champion_draft::varg,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (mode == 3)
        return { champion_draft::vendetta, champion_draft::heart_of_ruin, champion_draft::vesper_vale, champion_draft::varg,
                 champion_draft::ryth, champion_draft::marcella, champion_draft::ren, champion_draft::stellar,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    // Sustain picks (dromedaries' online comp): 12 = Willow 4th, Cassi&Issac 5th; 14 = Willow and
    // 15 = Cassi&Issac in place of Varg (5-pick maps only).
    if (mode == 16)  // burst comp seen online (r120): Heart, Ren, Ryth, Stellar, Cassi&Issac
        return { champion_draft::heart_of_ruin, champion_draft::ren, champion_draft::ryth, champion_draft::stellar,
                 champion_draft::cassi_and_issac, champion_draft::marcella, champion_draft::vendetta, champion_draft::varg,
                 champion_draft::vesper_vale, champion_draft::tavi, champion_draft::prism, champion_draft::willow };
    if (mode == 12)
        return { champion_draft::heart_of_ruin, champion_draft::marcella, champion_draft::ryth, champion_draft::willow,
                 champion_draft::cassi_and_issac, champion_draft::vendetta, champion_draft::varg, champion_draft::vesper_vale,
                 champion_draft::stellar, champion_draft::tavi, champion_draft::prism, champion_draft::ren };
    if (mode == 14 || mode == 15)
        return { champion_draft::heart_of_ruin, champion_draft::marcella, champion_draft::ryth, champion_draft::vendetta,
                 mode == 14 ? champion_draft::willow : champion_draft::cassi_and_issac, champion_draft::varg,
                 champion_draft::vesper_vale, champion_draft::stellar, champion_draft::tavi, champion_draft::prism, champion_draft::ren };
    // A ranged harasser among the first three (it plays on every map size): 18 = Vesper third,
    // 19 = Vesper second (the order two top-25 teams draft).
    if (mode == 18 || mode == 19)
        return { champion_draft::heart_of_ruin, mode == 18 ? champion_draft::marcella : champion_draft::vesper_vale,
                 mode == 18 ? champion_draft::vesper_vale : champion_draft::marcella, champion_draft::ryth, champion_draft::vendetta,
                 champion_draft::varg, champion_draft::stellar, champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism,
                 champion_draft::willow, champion_draft::ren };
    // Vendetta (strength 70, kill resets) into the three picks of the smallest maps: 20 = third,
    // 21 = second; Ryth moves to fourth.
    if (mode == 20 || mode == 21)
        return { champion_draft::heart_of_ruin, mode == 20 ? champion_draft::marcella : champion_draft::vendetta,
                 mode == 20 ? champion_draft::vendetta : champion_draft::marcella, champion_draft::ryth, champion_draft::varg,
                 champion_draft::vesper_vale, champion_draft::stellar, champion_draft::tavi, champion_draft::cassi_and_issac,
                 champion_draft::prism, champion_draft::willow, champion_draft::ren };
    if (mode == 22)  // mode 8 with the fourth and fifth picks swapped (Varg on 4-pick maps)
        return { champion_draft::heart_of_ruin, champion_draft::marcella, champion_draft::ryth, champion_draft::varg,
                 champion_draft::vendetta, champion_draft::vesper_vale, champion_draft::stellar, champion_draft::tavi,
                 champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow, champion_draft::ren };
    if (mode == 17)  // mode 8 in the p1 permutation's order (Ryth before Marcella)
        return { champion_draft::heart_of_ruin, champion_draft::ryth, champion_draft::marcella, champion_draft::vendetta,
                 champion_draft::varg, champion_draft::vesper_vale, champion_draft::stellar, champion_draft::tavi,
                 champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (mode == 7 || mode == 8)  // mode 2 with Ren / Varg as the fifth pick
        return { champion_draft::heart_of_ruin, champion_draft::marcella, champion_draft::ryth, champion_draft::vendetta,
                 mode == 7 ? champion_draft::ren : champion_draft::varg, champion_draft::vesper_vale, champion_draft::stellar,
                 champion_draft::tavi, champion_draft::cassi_and_issac, champion_draft::prism, champion_draft::willow };
    if (mode == 2)
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
