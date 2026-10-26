// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <cstdio>
#include <iostream>
#include "bot.h"
#include "draft.h"

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // the bot never exits: line-buffer so logs reach the runner
    Bot bot;
    while (true) {
        try {
            if (cambc::is_draft())
                play_draft();
            else
            {
                const auto r0 = cambc::round_number();  // TEMP suspension probe
                bot.play_turn();
                if (cambc::round_number() != r0) std::printf("suspended in turn r%d\n", static_cast<int>(r0));
            }
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        cambc::end_turn();
    }
}
