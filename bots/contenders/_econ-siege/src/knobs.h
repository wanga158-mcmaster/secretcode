#pragma once
// Coordinated siege probe knobs (arena --set NAME=VALUE through tools/economy/run_siege.py).
// Defaults make the bot passive (it drafts, then never acts): the do-nothing opponent.

// 0 passive; 1 siege.
constexpr int S_MODE = 0;
// Draft picks in order (cambc::champion_draft codes: ryth 0, vesper 1, vendetta 2, cassi&issac 3,
// stellar 4, prism 5, marcella 6, willow 7, ren 8, varg 9, tavi 10, heart 11).
constexpr int S_PICK0 = 11;
constexpr int S_PICK1 = 6;
constexpr int S_PICK2 = 0;
constexpr int S_PICK3 = 2;
constexpr int S_PICK4 = 9;
// Bitmask over cambc::champion_type values (ryth 0, vesper 1, vendetta 2, cassi 3, issac 4,
// stellar 5, prism 6, marcella 7, willow 8, ren 9, varg 10, tavi 11, heart 12) that take part.
constexpr int S_ACTIVE = 0;
// A member tile of the target group and the rally tile (out of the group's reach).
constexpr int S_GX = -1;
constexpr int S_GY = -1;
constexpr int S_RX = -1;
constexpr int S_RY = -1;
// 1: before the siege each champion farms its own gold group at (S_FX, S_FY0 + S_FSTEP x slot)
// until it can buy S_BUYN of item S_BUY (catalog id), then shops on spawn.
constexpr int S_FARM = 0;
constexpr int S_FX = 1;
constexpr int S_FY0 = 6;
constexpr int S_FSTEP = 2;
constexpr int S_BUY = -1;
constexpr int S_BUYN = 5;
// Ability use: 1 attack resets, 2 unit spells on the target, 4 ground area on the target, 8 damaging
// dash next to the target, 16 self shield while being hit.
constexpr int S_SPELLS = 31;
// A tank leaves when its health <= last round's damage x this (covered), or x (1 + strikes on the
// way out) when no other melee covers it.
constexpr int S_LEAVE100 = 100;
// Health fraction (percent) needed to count as ready at the rally.
constexpr int S_READY = 98;
// 1: wave policy (returning champions wait for the wave to end); 0: rejoin as soon as back.
constexpr int S_WAVE = 1;
// 1: a covered tank recalls where it stands (inside the camp's reach); 0: it walks out under cover
// first and recalls out of reach.
constexpr int S_RECALLIN = 0;
// Tank order among adjacent melee (priority): 0 most health first, 1 least health first.
constexpr int S_TANKORDER = 1;
// Log each turn's status line (1) or only events (0).
constexpr int S_VERBOSE = 1;
