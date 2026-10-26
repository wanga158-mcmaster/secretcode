// Draft: a pack built for group fights and group clears. Two 1200-health tanks with stuns in
// front (Ryth: area stun; Marcella: stun-immune, stunning strike), then ranged stunners and bodies.
#include <cambc.h>
#include <algorithm>
#include <vector>
#include "config.h"
#include "draft.h"

void play_draft() {
    using D = cambc::champion_draft;
    std::vector<D> order;
    switch (wb::cfg::draft_mode) {
    case 1: order = {D::ryth, D::marcella, D::cassi_and_issac, D::tavi, D::heart_of_ruin}; break;
    case 2: order = {D::ryth, D::marcella, D::tavi, D::vesper_vale, D::cassi_and_issac}; break;
    default: order = {D::ryth, D::marcella, D::tavi, D::cassi_and_issac, D::vesper_vale}; break;
    }
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    for (auto c : order)
        if (std::find(chosen.begin(), chosen.end(), c) == chosen.end()) {
            cambc::draft::pick(c);
            return;
        }
    for (int i = 0; i < 12; ++i) {
        const auto c = static_cast<D>(i);
        if (std::find(chosen.begin(), chosen.end(), c) == chosen.end()) {
            cambc::draft::pick(c);
            return;
        }
    }
}
