#include "runtime.h"
int main() {
    Bot bot;
    while (true) {
        try {
            if (cambc::is_draft()) draft();
            else bot.turn();
        } catch (const std::exception& error) {
            std::printf("bot error: %s\n",error.what());
        } catch (...) {
            std::puts("bot error: unknown exception");
        }
        cambc::end_turn();
    }
}
