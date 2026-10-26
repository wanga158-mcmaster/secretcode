#include "setup.h"
int main() {
    std::unique_ptr<plan4_archipelago_fixture::Parking> parking;
    while(true) {
        try {
            if(cambc::is_draft())plan4_archipelago_fixture::draft();
            else {if(!parking)parking=std::make_unique<plan4_archipelago_fixture::Parking>();parking->turn();}
        } catch(const std::exception& error) {std::printf("plan4 error: archipelago fixture %s\n",error.what());}
          catch(...) {std::printf("plan4 error: archipelago fixture unknown\n");}
        cambc::end_turn();
    }
}
