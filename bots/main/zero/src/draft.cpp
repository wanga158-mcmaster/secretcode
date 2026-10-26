// Draft: the draft head over the champions still available (DESIGN.md 2.2). Facts: map statistics, our and their
// picks so far, each candidate's design and kit. No preference order exists in code.
#include "draft.h"
#include <cambc.h>
#include <cstdio>
#include <vector>
#include "core/catalog.h"
#include "core/draft_facts.h"
#include "core/net.h"
#include "core/student_net.h"
#include "core/student_probe.h"
#include "weights.h"

namespace {

using namespace zero;

const float* weights()
{
    float* p = const_cast<float*>(params.w);
    asm volatile("" : "+r"(p) : : "memory");
    return p + kConfig;
}

}  // namespace

void play_draft()
{
    std::vector<int> ours, theirs;
    for (auto pick : cambc::draft::current_draft(cambc::team::ally)) ours.push_back(static_cast<int>(pick));
    for (auto pick : cambc::draft::current_draft(cambc::team::enemy)) theirs.push_back(static_cast<int>(pick));
    // Keep the existing cheap map queries; feature construction is shared with native exact records.
    DraftMapFacts map;
    {
        const auto groups = cambc::map::camp_groups();
        for (const auto& g : groups)
        {
            map.gems += static_cast<float>(g.gem_value());
            map.gold += static_cast<float>(g.gold_value());
            for (const auto& c : g.camps()) { map.members += 1.0f; map.strength += catalog().camp(static_cast<int>(c.kind())).strength; }
        }
        map.area = static_cast<std::uint32_t>(cambc::map::width() * cambc::map::height());
        map.group_count = static_cast<std::uint32_t>(groups.size());
        map.altar_count = static_cast<std::uint32_t>(cambc::map::altars().count());
    }
    float sv[kState];
    CandSet set;
    draft_facts(map, static_cast<int>(cambc::draft::team_size()), ours, theirs, sv, set);
    float lg[kMaxCands];
    const float* cfg = weights() - kConfig;
    if (cfg[5] > 0.0f && cfg[6] > 0.0f)
    {
        student::net::Embedding embedding;
        student::net::Projection projection;
        student::net::embed(weights(), sv, embedding);
        if (!student::net::project(weights(), embedding, h_draft, projection))
        {
            std::printf("error student draft projection unavailable\n");
            return;
        }
        for (int i = 0; i < set.n; ++i) lg[i] = student::net::score(projection, set.c[i].phi);
    }
    else
    {
        net::Trunk t;
        net::draft_trunk(weights(), sv, t);
        net::logits(weights(), t, set, lg);
    }
    std::uint64_t rng = net::mix(static_cast<std::uint64_t>(ours.size() * 977 + theirs.size()));
    const auto p = net::choose(lg, set.n, cfg[0], rng);
    int selected = p.index;
#if defined(ZERO_STUDENT_PROBE)
    selected = student::probe::draft_pick(sv, set, lg, selected);
    if (selected < 0 || selected >= set.n)
    {
        std::printf("error student draft probe index unavailable\n");
        return;
    }
#endif
    if (set.n > 0)
    {
        cambc::draft::pick(static_cast<cambc::champion_draft>(set.c[selected].target));
#if defined(ZERO_STUDENT_PROBE)
        student::probe::drafted(selected, set.c[selected].target);
#endif
    }
}
