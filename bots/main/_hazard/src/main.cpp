// bots/main entrypoint: dispatch draft or gameplay; any failed turn is logged and yields.
#include <cambc.h>
#include <cstdio>
#include <exception>
#include "brain.h"
#include "draft.h"
#include "config.h"
#include "util.h"

namespace mb {

namespace {
int log_lines = 0;
}

bool fuel_ok(std::uint64_t need) { return cambc::fuel_remaining() > need + cfg::fuel_reserve; }

bool log_allowed() { return ++log_lines <= 400; }

void FuelTrace::report(int round, std::uint64_t threshold) const
{
    const auto total = start > last ? start - last : 0;
    if (!suspended && total < threshold) return;
    if (!log_allowed()) return;
    std::printf("fuel r%d %s%s total %llu:", round, suspended ? "SUSPENDED in " : "", suspended ? suspended_in : "",
                static_cast<unsigned long long>(total));
    for (int i = 0; i < n; ++i) std::printf(" %s=%llu", names[i], static_cast<unsigned long long>(used[i]));
    std::printf("\n");
}

} // namespace mb

int main()
{
    mb::Brain* brain = nullptr;
    while (true)
    {
        try
        {
            if (cambc::is_draft())
                mb::play_draft();
            else
            {
                if (!brain) brain = new mb::Brain();
                brain->turn();
            }
        }
        catch (const std::exception& error)
        {
            MB_LOG("turn error r%u: %s\n", cambc::is_draft() ? 0u : cambc::round_number(), error.what());
        }
        catch (...)
        {
            MB_LOG("turn error: unknown exception\n");
        }
        cambc::end_turn();
    }
}
