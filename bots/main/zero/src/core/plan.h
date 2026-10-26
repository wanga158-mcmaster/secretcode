#pragma once
// Full macro-goal space for the explicit controller. Each conditional decision uses the existing h_goal.
#include <array>
#include <vector>
#include "cand.h"

namespace zero {

enum class GoalStage : std::uint8_t { kind, range, target, done };
enum class GoalPlanStatus : std::uint8_t { ok, complete, stale, unavailable, overflow, invalid };

struct GoalCursor
{
    GoalStage stage{ GoalStage::kind };
    int kind{ -1 }, begin{}, end{}, depth{}, target{ -1 };
    const Obs* observation{};
    const Map* map{};
    std::uint64_t revision{};
};

struct GoalPlanBuild
{
    GoalPlanStatus status{ GoalPlanStatus::ok };
    int total{};
    bool usable() const { return status == GoalPlanStatus::ok; }
};

// Keep the same observation and trunk throughout a traversal. Only the cursor changes; the caller commits
// cursor.kind/target to Memory after stage==done and retains the log probability of each selected decision.
void plan_begin(const Context& ctx, GoalCursor& cursor);
GoalPlanBuild plan_candidates(const Context& ctx, const GoalCursor& cursor, CandSet& out);
bool plan_select(const Context& ctx, GoalCursor& cursor, const Cand& chosen);

// Teacher routing uses only intended kind/target membership. It never changes candidate facts or expands
// visibility. -1 means the intent is absent at this observation; callers must not substitute another label.
int plan_teacher_index(const Context& ctx, const GoalCursor& cursor, GoalKind intended_kind, int intended_target);

// phi[0..6] are the existing goal-kind one-hots. phi[7] is 0 at exact targets, 1 at the kind choice, and 2 at
// range choices; the old goal builder remains untouched. phi[23] means the current goal belongs to the node.
// Farm ranges partition stable map group IDs into four balanced half-open ranges. Target nodes have <=4
// groups and use the old exact farm features, including the direct Paths distance/sentinel divided by40.
// Range facts: means of distance/rewards/public full HP/strength in8..12; count/total groups in13;
// visible camps/(64*groups) in14; public members/(64*groups) in15; known-status fraction in16;
// mean remembered respawn/claims/enemy recency/enemy-spawn distance in17..20; status fractions in21..22.
// Internal stages call neither Paths nor clear forecasts. Every static Farm ID remains an intent, even
// when its present path is unknown/unreachable; only exact target facts query Paths. Other target stages
// enumerate all altars/visible enemies/visible allies in stable input order, with explicit overflow >24.

// One caller-owned immutable decision loan. The Context value fixes its borrowed Map/Obs/Memory/Paths
// pointers. Destroy this session BEFORE changing Memory, refreshing Obs, executing a primitive or yielding.
// There are no persistent caches, snapshots or per-node loan-validity checks: enforce this lifetime in the
// caller's scope and its offline/debug harness. The uncached functions above remain the reference path.
class GoalPlanSession
{
public:
    explicit GoalPlanSession(Context context);
    GoalPlanSession(const GoalPlanSession&) = delete;
    GoalPlanSession& operator=(const GoalPlanSession&) = delete;
    GoalPlanSession(GoalPlanSession&&) = delete;
    GoalPlanSession& operator=(GoalPlanSession&&) = delete;

    GoalPlanBuild candidates(CandSet& out);
    bool select(const Cand& chosen);
    int teacherIndex(GoalKind intended_kind, int intended_target) const;
    GoalStage stage() const { return cursor_.stage; }
    int kind() const { return cursor_.kind; }
    int target() const { return cursor_.target; }
    const GoalCursor& cursor() const { return cursor_; }

private:
    struct GroupRow
    {
        float distance, gold, gems, hp, strength, members, wait, recency, enemy_distance;
        float known, reward, respawning;
        int claims, visible;
    };
    // Inline capacities affect storage only. Larger inputs spill without removing any intent or member.
    static constexpr std::size_t kInlineGroups = 32, kInlineCamps = 64;
    const Context context_;
    GoalCursor cursor_;
    int current_kind_{ -1 }, current_target_{ -1 };
    bool valid_{}, groups_ready_{}, lookup_ready_{}, gold_ready_{}, farm_rules_ready_{};
    float gold_mult_{}, dps_{}, camp_taken_{};
    std::array<GroupRow, kInlineGroups> inline_groups_;
    std::vector<GroupRow> extra_groups_;
    GroupRow* groups_{};
    std::array<int, 2 * kInlineCamps> inline_buckets_;
    std::array<int, kInlineCamps> inline_next_;
    std::vector<int> extra_buckets_, extra_next_;
    int* buckets_{};
    int* next_{};
    std::size_t bucket_count_{};
    std::array<float, kInlineCamps> inline_hp_;
    std::vector<float> extra_hp_;

    float goldMultiplier();
    void prepareGroups();
    void prepareCampLookup();
    Cand& add(CandSet& out, int kind, int target, float stage);
    void farmRange(int begin, int end, Cand& candidate);
    void farmTargets(CandSet& out);
};

}  // namespace zero
