#pragma once
// One camp-indexed task, all other heroes retain their private FARM policy. The mailbox
// borrows target slots only during a bounded auction; no state is added to Brain/Knowledge.
#include "knowledge.h"
#include "joint_model.h"

namespace mb::joint {
bool enabled();
void begin(Knowledge& kn);
void emit_admission_audit(); // cached record only, at Brain turn entry before observation
bool frozen();
bool publication(int group, SlotStatus status);
bool news(int group, int until);
bool tagged_news();
void observe(const Ctx& ctx, int farm_group, bool capturing, float farm_gold_mult, float farm_gem_gold);
void finish(int farm_group, bool capturing);
const Packet* task();
void stop();
bool execution();
bool rejected_group(int group);
bool winning_bank(const Ctx& ctx);
bool arrived();
void mark_arrived();
void veto();
// Cheap counters only; no formatted combat-turn output. The protocol/planner work runs in
// mailbox-only controls too. Executor helpers necessarily have additional execution cost.
struct Counters {
    unsigned requests{}, bids{}, commits{}, aborts{}, assigned{}, arrivals{}, helper_arrivals{};
    unsigned moves{}, attacks{}, casts{}, expired{}, missed{}, noeffect{}, farm{}, rejects{};
};
Counters& counters();
}
