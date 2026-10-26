#pragma once
void draft() {
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale, cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order) if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
        cambc::draft::pick(choice); return;
    }
}
