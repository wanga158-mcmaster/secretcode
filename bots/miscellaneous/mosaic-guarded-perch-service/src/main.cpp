// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <iostream>
#include "bot.h"
#include "draft.h"
#include "config.h"
#include "service.h"

int main() {
    Bot bot;
    while (true) {
        try {
            if constexpr (mosaic::cfg::enabled) {
                if (cambc::is_draft()) mosaic::service_draft();
                else mosaic::service_turn();
            } else if (cambc::is_draft())
                play_draft();
            else
                bot.play_turn();
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        cambc::end_turn();
    }
}
