#include "config.h"
#if MOSAIC_ADAPTIVE_SERVICE
#include "runtime.h"
#include <cstdio>
int main(){
    static constinit mosaic::adaptive::Runtime runtime{};
    for(;;){
        const auto whole_entry=cambc::fuel_remaining();
        try{
            if(cambc::is_draft())mosaic::adaptive::draft();
            else runtime.turn(whole_entry);
        }catch(const std::exception& error){std::printf("bot error: %s\n",error.what());}
        catch(...){std::printf("bot error: unknown exception\n");}
        cambc::end_turn();
    }
}
#else
#include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/main.cpp"
#endif
