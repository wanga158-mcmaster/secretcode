#pragma once
// Draft strategy hooks are intentionally actor-independent. Per-core specialists
// may replace this file to declare their edge or static opening in draft memory.
void draft() {
    constexpr auto first = profile::core_number == 15 ? cambc::champion_draft::prism : cambc::champion_draft::tavi;
    constexpr auto second = profile::core_number == 14 ? cambc::champion_draft::cassi_and_issac : cambc::champion_draft::varg;
    constexpr std::array order{first, second, cambc::champion_draft::vendetta,
        cambc::champion_draft::vesper_vale, cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order) if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
        cambc::draft::pick(choice); return;
    }
}
