#define PLAN4_MACRO_MODE 1
#define PLAN4_DRAFT_PROFILE plan4::drafting::Profile::reference
#define PLAN4_REFERENCE_VERSION "reference-v8"
#include "../../common/legacy.h"

int main() {
    auto bot = std::make_unique<plan4_legacy::Bot>();
    while (true) {
        try {
            if (cambc::is_draft()) {
                plan4_legacy::draft();
            } else bot->turn();
        } catch (const std::exception& error) {
            std::cout << "plan4 error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "plan4 error: unknown exception\n";
        }
        cambc::end_turn();
    }
}
