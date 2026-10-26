// Dispatch draft and gameplay; an unexpected failure is logged with "error" and the turn yields.
#include <cambc.h>
#include <cstdio>
#include <exception>
#include "bot.h"
#include "draft.h"
#include "util.h"

int main()
{
    tt::Bot bot;
    while (true) {
        try {
            if (cambc::is_draft())
                tt::play_draft();
            else
                bot.play_turn();
        } catch (const std::exception& e) {
            std::printf("timetable error s%d r%u: %s\n", tt::g_log_slot, cambc::is_draft() ? 0u : cambc::round_number(), e.what());
        } catch (...) {
            std::printf("timetable error: unknown exception\n");
        }
        cambc::end_turn();
    }
}
