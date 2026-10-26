#include <cstdio>
#include "execution.h"

struct Buffs
{
    bool present;
    int duration;
    bool has_stun() const { return present; }
};

struct Actor
{
    Buffs current{};
    int queries{}, dispatches{}, invalid{}, last_altar{-1};
    Buffs buffs() { ++queries; return current; }
    void submit_gems(int altar)
    {
        if (current.present) ++invalid;
        ++dispatches;
        last_altar = altar;
    }
};

int main()
{
    Actor actor;
    actor.current = { true, 1 };
    if (ce::try_start_tribute(actor, 7) || actor.dispatches || actor.invalid) return 1;
    // End-turn decrements to zero; this remains an active stun until a fresh owner start.
    actor.current.duration = 0;
    if (ce::try_start_tribute(actor, 7) || actor.dispatches || actor.invalid) return 2;
    // The same committed bank resumes once a new query sees the buff actually absent.
    actor.current.present = false;
    if (!ce::try_start_tribute(actor, 7) || actor.dispatches != 1 || actor.last_altar != 7) return 3;
    // A later fresh stun must not reuse the earlier readiness result.
    actor.current = { true, 3 };
    if (ce::try_start_tribute(actor, 4) || actor.dispatches != 1 || actor.invalid || actor.queries != 4) return 4;
    std::puts("execution: fresh stun boundaries passed");
    return 0;
}
