// Included after a private runtime adapter and PLAN4_RUNTIME_TYPE/ENABLED.
#define PLAN4_MACRO_MODE 1
#define PLAN4_ONLINE_EVIDENCE 1
#ifndef PLAN4_REFERENCE_VERSION
#define PLAN4_REFERENCE_VERSION "first-wave-runtime-v2"
#endif
#include "legacy.h"

int main() {
#ifdef PLAN4_SETUP_TYPE
    // Both arms acquire a legal setup with the same inherited farmer and Bot.
    std::unique_ptr<plan4_legacy::Bot> bot;
    auto setup = std::make_unique<PLAN4_SETUP_TYPE>();
    bool setup_paused = false, released = false;
#else
    auto bot = std::make_unique<plan4_legacy::Bot>();
#endif
    while (true) {
        try {
            if (cambc::is_draft()) plan4_legacy::draft();
            else {
#ifdef PLAN4_SETUP_TYPE
                using Action = PLAN4_SETUP_TYPE::Action;
                const auto action = setup->turn();
                if (released && action != Action::model) throw std::runtime_error("fixture resumed setup after release");
                if (action == Action::handled) {
                    if (bot && !setup_paused) bot->fixture_suspend();
                    setup_paused = true;
                } else {
                    if (!bot) bot = std::make_unique<plan4_legacy::Bot>(false);
                    if (action == Action::warmup) {
                        setup_paused = false;
                        bot->fixture_warmup();
                    } else if (action == Action::prepare) {
                        setup_paused = false;
                        struct Commit { PLAN4_SETUP_TYPE* setup; bool* released; } commit{setup.get(), &released};
                        bot->fixture_prepare([](void* raw) {
                            auto& state = *static_cast<Commit*>(raw);
                            if (!state.setup->commit_release()) return false;
                            // Commit the dispatcher before selection/execution,
                            // including when a later action throws or yields.
                            *state.released = true;
                            return true;
                        }, &commit);
                        bot->after_turn();
                    } else {
                        if (!released) throw std::runtime_error("fixture model action without admission");
                        bot->turn(); bot->after_turn();
                    }
                }
#else
                bot->turn(); bot->after_turn();
#endif
            }
        } catch (const std::exception& error) {
            std::cout << "plan4 error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "plan4 error: unknown exception\n";
        }
        cambc::end_turn();
    }
}
