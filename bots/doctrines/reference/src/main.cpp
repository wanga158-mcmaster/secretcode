#include "runtime.h"

int main() {
    p5::native::Runtime bot;
    p5::DraftRequest request;
    request.roles.fill({5, .05, 0, 1, 5});
    while (true) {
        bot.guard_turn([&] {
            if (cambc::is_draft()) p5::native::Runtime::draft(request);
            else bot.reference_turn();
        });
        cambc::end_turn();
    }
}
