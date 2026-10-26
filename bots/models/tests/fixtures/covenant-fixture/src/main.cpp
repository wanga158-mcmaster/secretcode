#ifndef PLAN4_REFERENCE_VERSION
#define PLAN4_REFERENCE_VERSION "first-wave-runtime-v2"
#endif
#include "setup.h"
#ifndef PLAN4_COVENANT_FIXTURE_NEGATIVE
#define PLAN4_COVENANT_FIXTURE_NEGATIVE 0
#endif
int main() {
    std::unique_ptr<plan4_covenant_fixture::Opponent> opponent;
    while(true) {
        try {
            if(cambc::is_draft())plan4_covenant_fixture::draft();
            else {
                if(!opponent)opponent=std::make_unique<plan4_covenant_fixture::Opponent>(
                    PLAN4_COVENANT_FIXTURE_NEGATIVE?plan4_covenant_fixture::Condition::parked_negative:
                    plan4_covenant_fixture::Condition::encounter);
                opponent->turn();
            }
        } catch(const std::exception& error) {std::printf("plan4 error: covenant fixture %s\n",error.what());}
          catch(...) {std::printf("plan4 error: covenant fixture unknown\n");}
        cambc::end_turn();
    }
}
