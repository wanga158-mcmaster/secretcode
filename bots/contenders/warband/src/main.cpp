// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <iostream>
#include "bot.h"
#include "draft.h"
#include "config.h"
#include "service_runtime.h"
#include "capital_runtime.h"

int main() {
    Bot bot;
    while (true) {
        try {
            if (cambc::is_draft()) {
                if constexpr (wb::config::capital_work_controller) wb::capital::draft();
                else if constexpr (wb::config::production_controller) wb::play_service_draft();
                else play_draft();
            } else {
                if constexpr (wb::config::capital_work_controller) wb::capital::play();
                else if constexpr (wb::config::production_controller) wb::play_service_turn();
                else bot.play_turn();
            }
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        cambc::end_turn();
    }
}
