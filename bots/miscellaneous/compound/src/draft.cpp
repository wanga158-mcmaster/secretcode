// Draft: durable farmers plus the gold passive. Picks are simultaneous and duplicates across
// teams are allowed, so the pick order only sets our own turn order (priority ties).
#include "draft.h"

#include <algorithm>
#include <array>
#include <cambc.h>
#include <vector>

#include "config.h"

void play_draft()
{
    using cambc::champion_draft;
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    const auto picks = cambc::draft::team_size();
    std::vector<champion_draft> order;
    if (picks <= 3)
        order = { champion_draft::ryth, champion_draft::marcella, champion_draft::vesper_vale };
    else
        order = { champion_draft::vesper_vale, champion_draft::ryth, champion_draft::marcella, champion_draft::vendetta,
                  champion_draft::heart_of_ruin };
    // Fallbacks if the list is exhausted.
    for (auto extra : { champion_draft::varg, champion_draft::ren, champion_draft::tavi, champion_draft::stellar })
        order.push_back(extra);
    for (const auto pick : order)
    {
        if (std::find(chosen.begin(), chosen.end(), pick) != chosen.end()) continue;
        cambc::draft::pick(pick);
        return;
    }
}
