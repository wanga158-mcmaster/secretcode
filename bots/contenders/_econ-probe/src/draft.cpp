// Economy probe draft: picks P_PICK0.. in order (draft enum codes), the first free one otherwise.
#include <cambc.h>
#include "draft.h"
#include "knobs.h"
#include <algorithm>
#include <vector>

void play_draft() {
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    const int want[5] = {P_PICK0, P_PICK1, P_PICK2, P_PICK3, P_PICK4};
    const auto taken = [&](int code) {
        return std::find(chosen.begin(), chosen.end(), static_cast<cambc::champion_draft>(code)) != chosen.end();
    };
    int pick = -1;
    const std::size_t k = chosen.size();
    if (k < 5 && want[k] >= 0 && !taken(want[k])) pick = want[k];
    for (int code = 0; pick < 0 && code < 12; ++code)
        if (!taken(code)) pick = code;
    cambc::draft_memory = std::string_view("econ-probe");
    cambc::draft::pick(static_cast<cambc::champion_draft>(pick));
}
