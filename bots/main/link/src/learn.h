#pragma once
// Learned decision residuals (D340, cfg::learned): feature-weighted adjustments to v44's decision
// quantities at six points - each group's target score, the fight verdict, the banking threshold,
// the recall threshold, the draft order and the shop plan's order - trained by self-play
// (bots/main/tools/train). Every weight zero reproduces the hand-written decisions exactly; with
// cfg::learned 0 nothing here is used and the linker drops it. New blocks go at the end, so trained
// weights of an earlier layout stay a valid prefix (zeros for the rest).
#include <cstdint>

namespace mb::learn {

enum Block : int { group, fight, bank, recall, draft, shop, spawn, group2, press, pressg, pull, ambush, knobs, cohesion, support, block_count };
inline constexpr int kSize[block_count] = { 16, 16, 12, 12, 12, 12, 8, 6, 10, 10, 8, 6, 8, 8, 16 };
inline constexpr int kOffset[block_count] = { 0, 16, 32, 44, 56, 68, 80, 88, 94, 104, 114, 122, 128, 136, 144 };
inline constexpr int kCount = 160;
inline constexpr int kMaxFeatures = 16;

// The local runner (bots/main/tools/fastrun --params-a/-b) finds this block by its magic and writes
// candidate weights before the bot starts; a submitted bot carries its trained weights here.
struct Params
{
    char magic[16];
    std::uint32_t count;
    std::uint32_t version;
    std::uint32_t reserved[2];
    volatile float w[kCount];  // volatile: read at run time, never folded from the initialiser
};
extern Params params;

// Weighted sum of one block's features (`x` holds kSize[b] values), or of `count` of them from `from`.
float dot(Block b, const float* x);
// Whether any weight of a block is non-zero (callers skip a block's feature work when it is all zero).
bool any(Block b);
float dot(Block b, int from, const float* x, int count);
// About exp(logit) with the logit clamped to [-3, 3] (factors 0.023-12.8): a bounded multiplicative
// adjustment, exactly 1 for a zero logit.
float factor(float logit);
// One weight of the draft block (a champion_draft value 0-11).
float draft_weight(int champion);
// One weight of the shop block: 0-6 item families, 7-9 stats (strength, focus, health); in plan places.
float shop_weight(int index);
// About ln(1 + x) for x >= 0 from the float's exponent and mantissa (no libm call; within 0.06).
float log1p_fast(float x);
// A smooth yes/no for decisions that are "no" in the hand-written rules: yes with probability
// max(0, sigmoid(logit) - 0.5), drawn deterministically from `key` (games stay reproducible), so a
// zero logit is always "no" and small weights give rare, graded "yes" (a usable ES gradient).
bool soft_yes(float logit, std::uint32_t key);

} // namespace mb::learn
