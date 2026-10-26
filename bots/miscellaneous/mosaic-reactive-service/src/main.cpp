// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <iostream>
#include "bot.h"
#include "draft.h"
#include "config.h"
#include "architecture.h"
#include "service_architecture.h"
#include "probe_config.h"
#include "geometry_probe.h"

int main() {
    Bot bot;
    while (true) {
        try {
            if constexpr (mosaic::cfg::geometry_probe) {
                if (cambc::is_draft()) mosaic::probe_draft();
                else mosaic::probe_turn();
            } else if constexpr (mosaic::cfg::enabled) {
                if (cambc::is_draft()) mosaic::play_draft();
                else if constexpr (mosaic::cfg::selected_service) mosaic::service_turn();
                else mosaic::play_turn();
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
