// Dispatch draft and gameplay; a failed turn is logged (with "error") and still yields.
#include <cambc.h>
#include <cstdio>
#include "bot.h"
#include "draft.h"

int main() {
    wb::Bot bot;
    while (true) {
        try {
            if (cambc::is_draft())
                play_draft();
            else
                bot.play_turn();
        } catch (const std::exception& error) {
            wb::log_error("warband error: %s\n", error.what());
        } catch (...) {
            wb::log_error("warband error: unknown exception\n");
        }
        cambc::end_turn();
    }
}
