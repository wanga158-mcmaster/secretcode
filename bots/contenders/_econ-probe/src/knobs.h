#pragma once
// Economy probe knobs (arena --set NAME=VALUE). Defaults make the bot passive: it drafts and
// then never acts, so it serves as the do-nothing opponent of every probe.

// 0 passive; 1 probe (only champions whose champion_type bit is in P_ACTIVE act).
constexpr int P_MODE = 0;
// Draft picks in order (cambc::champion_draft codes: ryth 0, vesper 1, vendetta 2, cassi&issac 3,
// stellar 4, prism 5, marcella 6, willow 7, ren 8, varg 9, tavi 10, heart 11).
constexpr int P_PICK0 = 2;
constexpr int P_PICK1 = 6;
constexpr int P_PICK2 = 0;
constexpr int P_PICK3 = 1;
constexpr int P_PICK4 = 11;
// Bitmask over cambc::champion_type values (ryth 0, vesper 1, vendetta 2, cassi 3, issac 4,
// stellar 5, prism 6, marcella 7, willow 8, ren 9, varg 10, tavi 11, heart 12) that act.
constexpr int P_ACTIVE = 0;
// A member tile of the target group, and of an optional gold group farmed first to buy items.
constexpr int P_GX = -1;
constexpr int P_GY = -1;
constexpr int P_FX = -1;
constexpr int P_FY = -1;
// Item id (catalog order, 0..41) bought P_BUYN times at spawn once affordable; -1 none.
constexpr int P_BUY = -1;
constexpr int P_BUYN = 5;
// Second item bought after the first (same count); -1 none.
constexpr int P_BUY2 = -1;
// Ability use: 1 attack resets, 2 unit spells (damage, cc) on the target camp, 4 ground AoE on
// the target camp, 8 damaging dash next to the target, 16 self shield while monsters are awake.
constexpr int P_SPELLS = 0;
// Clears of the target group (respawns are awaited off the member tiles in between).
constexpr int P_LOOPS = 1;
// After the loops: 1 walk to the nearest altar and tribute, 0 stay.
constexpr int P_TRIBUTE = 1;
// Kill order: 0 Smith (strength/health, descending), 1 lowest health, 2 nearest.
constexpr int P_ORDER = 0;
// Retreat out of range and recall below this percentage of max health (0 never).
constexpr int P_RECALLPCT = 0;
// Ranged champions keep this Chebyshev distance to the target (<= attack range).
constexpr int P_RANGE = 3;
// 1: log every visible camp of the target group each turn.
constexpr int P_VERBOSE = 1;
// Rounds to wait at spawn before starting (lets partners line up).
constexpr int P_DELAY = 0;
// 1: every active champion stands on a member tile for the capture (reward split test).
constexpr int P_ALLCAP = 0;
