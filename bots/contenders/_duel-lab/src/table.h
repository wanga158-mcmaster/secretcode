// Group scenarios for _duel-lab (table mode, map bit 28; bits 0-11 select the row).
#pragma once
#include <cambc.h>

struct Side
{
    int pick, build, pol;
};
struct Unit
{
    bool active;  // takes part in the fight (else rests on spawn)
    int pol, build, slot;
};
struct TPick
{
    int pick, pol, build;
};
struct TScn
{
    int n[2];
    TPick p[2][5];
};

// picks: 0 ryth 1 vesper 2 vendetta 3 cassi&issac 4 stellar 5 prism 6 marcella 7 willow 8 ren
// 9 varg 10 tavi 11 heart. pol: 0 fight 4 basic-only. Unused picks of a side are filled by the
// draft's filler list and rest on spawn.
inline const TScn kTable[] = {
    // --- numbers, basic attacks only (Ryth and Marcella have identical basic stats; Varg 1000/40)
    /* 0 */ { { 1, 1 }, { { { 0, 4, 0 } }, { { 6, 4, 0 } } } },
    /* 1 */ { { 2, 1 }, { { { 0, 4, 0 }, { 6, 4, 0 } }, { { 0, 4, 0 } } } },
    /* 2 */ { { 2, 2 }, { { { 0, 4, 0 }, { 6, 4, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 } } } },
    /* 3 */ { { 3, 2 }, { { { 0, 4, 0 }, { 6, 4, 0 }, { 9, 4, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 } } } },
    /* 4 */ { { 3, 3 }, { { { 0, 4, 0 }, { 6, 4, 0 }, { 9, 4, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 }, { 9, 4, 0 } } } },
    /* 5 */ { { 3, 1 }, { { { 0, 4, 0 }, { 6, 4, 0 }, { 9, 4, 0 } }, { { 0, 4, 0 } } } },
    // --- focus against spread (A spreads, B focuses)
    /* 6 */ { { 3, 2 }, { { { 0, 9, 0 }, { 6, 9, 0 }, { 9, 9, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 } } } },
    /* 7 */ { { 2, 2 }, { { { 0, 9, 0 }, { 6, 9, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 } } } },
    /* 8 */ { { 3, 3 }, { { { 0, 9, 0 }, { 6, 9, 0 }, { 9, 9, 0 } }, { { 0, 4, 0 }, { 6, 4, 0 }, { 9, 4, 0 } } } },
    // --- full kits, the online meta five (Heart, Vesper, Marcella, Ryth, Vendetta)
    /* 9 */ { { 5, 5 }, { { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } },
                          { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } } } },
    /* 10 */ { { 5, 4 }, { { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } },
                           { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 } } } },
    /* 11 */ { { 5, 3 }, { { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } },
                           { { 11, 0, 0 }, { 1, 0, 0 }, { 6, 0, 0 } } } },
    /* 12 */ { { 3, 3 }, { { { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } }, { { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } } } },
    /* 13 */ { { 3, 2 }, { { { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } }, { { 6, 0, 0 }, { 0, 0, 0 } } } },
    /* 14 */ { { 4, 4 }, { { { 11, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } }, { { 11, 0, 0 }, { 6, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 } } } },
    /* 15 */ { { 2, 1 }, { { { 6, 0, 0 }, { 2, 0, 0 } }, { { 11, 0, 0 } } } },
    // --- stun chains on one tanky victim (Ryth with 15 Health shards, passive)
    /* 16 */ { { 3, 1 }, { { { 10, 0, 0 }, { 5, 0, 0 }, { 3, 0, 0 } }, { { 0, 3, 3 } } } },
    /* 17 */ { { 3, 1 }, { { { 10, 10, 0 }, { 5, 10, 0 }, { 3, 10, 0 } }, { { 0, 3, 3 } } } },
    /* 18 */ { { 5, 1 }, { { { 10, 10, 0 }, { 5, 10, 0 }, { 3, 10, 0 }, { 8, 10, 0 }, { 0, 10, 0 } }, { { 0, 3, 3 } } } },
    /* 19 */ { { 5, 1 }, { { { 10, 0, 0 }, { 5, 0, 0 }, { 3, 0, 0 }, { 8, 0, 0 }, { 0, 0, 0 } }, { { 0, 3, 3 } } } },
    // --- area burst on a passive clump of three (B column at x = 24, y = 13..15)
    /* 20 */ { { 1, 3 }, { { { 11, 0, 0 } }, { { 0, 3, 0 }, { 6, 3, 0 }, { 9, 3, 0 } } } },
    /* 21 */ { { 1, 3 }, { { { 4, 0, 0 } }, { { 0, 3, 0 }, { 6, 3, 0 }, { 9, 3, 0 } } } },
    /* 22 */ { { 1, 3 }, { { { 5, 0, 0 } }, { { 0, 3, 0 }, { 6, 3, 0 }, { 9, 3, 0 } } } },
    /* 23 */ { { 1, 3 }, { { { 9, 0, 0 } }, { { 0, 3, 0 }, { 6, 3, 0 }, { 2, 3, 0 } } } },
    // --- healing and shields in 2v2
    /* 24 */ { { 2, 2 }, { { { 2, 0, 0 }, { 7, 0, 0 } }, { { 2, 0, 0 }, { 1, 0, 0 } } } },
    /* 25 */ { { 2, 2 }, { { { 6, 0, 0 }, { 7, 0, 0 } }, { { 6, 0, 0 }, { 10, 0, 0 } } } },
    /* 26 */ { { 2, 2 }, { { { 9, 0, 0 }, { 7, 0, 0 } }, { { 9, 0, 0 }, { 1, 0, 0 } } } },
    // --- an elk pair on the centre line (map terrain "elk"): A wakes it and holds 6 tiles away
    /* 27 */ { { 1, 1 }, { { { 1, 11, 0 } }, { { 2, 0, 0 } } } },
    /* 28 */ { { 1, 1 }, { { { 1, 6, 0 } }, { { 2, 0, 0 } } } },
    /* 29 */ { { 1, 1 }, { { { 10, 11, 0 } }, { { 0, 0, 0 } } } },
    /* 30 */ { { 1, 1 }, { { { 10, 6, 0 } }, { { 0, 0, 0 } } } },
    /* 31 */ { { 1, 1 }, { { { 6, 11, 0 } }, { { 6, 0, 0 } } } },
    /* 32 */ { { 1, 1 }, { { { 6, 6, 0 } }, { { 6, 0, 0 } } } },
};
inline constexpr int kTableSize = int(sizeof(kTable) / sizeof(kTable[0]));

inline const TScn& trow(int i) { return kTable[(i >= 0 && i < kTableSize) ? i : 0]; }

inline bool table_side_build(int i, int side)
{
    const auto& t = trow(i);
    for (int k = 0; k < t.n[side]; ++k)
        if (t.p[side][k].build) return true;
    return false;
}
inline bool table_any_build(int i) { return table_side_build(i, 0) || table_side_build(i, 1); }
inline int table_pick(int i, int side, int k)
{
    const auto& t = trow(i);
    return k < t.n[side] ? t.p[side][k].pick : -1;
}
inline bool table_pick_is(int pick, cambc::champion_type c)
{
    using cambc::champion_type;
    static const champion_type map[] = { champion_type::ryth, champion_type::vesper_vale, champion_type::vendetta,
                                         champion_type::cassi, champion_type::stellar, champion_type::prism,
                                         champion_type::marcella, champion_type::willow, champion_type::ren,
                                         champion_type::varg, champion_type::tavi, champion_type::heart_of_ruin };
    if (pick == 3) return c == champion_type::cassi || c == champion_type::issac;
    return pick >= 0 && pick < 12 && map[pick] == c;
}
inline Unit table_unit(int i, int side, cambc::champion_type c)
{
    const auto& t = trow(i);
    for (int k = 0; k < t.n[side]; ++k)
        if (table_pick_is(t.p[side][k].pick, c))
            return { true, t.p[side][k].pol, t.p[side][k].build, (c == cambc::champion_type::issac) ? 5 : k };
    return { false, 3, 0, 0 };
}
// Formation in the A frame (A left of the centre column x = 20, B mirrored): a column at x = 16.
inline cambc::vec2 table_prepos(int i, int side, int slot, int W, int H)
{
    static const cambc::vec2 a[] = { { 16, 14 }, { 16, 13 }, { 16, 15 }, { 16, 12 }, { 16, 16 }, { 15, 14 } };
    cambc::vec2 p = a[(slot >= 0 && slot < 6) ? slot : 0];
    if (i >= 27 && i <= 32) p = cambc::vec2{ 14, 14 };  // camp rows: hold 6 tiles from the elk pair
    return side == 0 ? p : cambc::vec2{ W - 1 - p.x, H - 1 - p.y };
}
