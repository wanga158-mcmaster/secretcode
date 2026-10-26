// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <iostream>
#include "bot.h"
#include "draft.h"
#include "config.h"
#include "coop_controller.h"

int main() {
    Bot bot;
    while (true) {
        int input_round=-1,input_actor=-1;
        try {
            if constexpr (wb::config::production_controller) {
                static wb::Production production;
                if (cambc::is_draft()) wb::production_draft();
                else {
                    input_round=int(cambc::round_number());input_actor=wb::native(cambc::self());
                    production.play_turn();
                    if constexpr(wb::config::phase_audit) if(cambc::round_number()!=input_round)
                        std::cout<<"wb suspended r"<<input_round<<" -> "<<cambc::round_number()
                            <<" stage "<<wb::error_stage<<" micro "<<wb::micro_stage
                            <<" actor "<<input_actor<<'\n';
                }
            } else {
                if (cambc::is_draft()) play_draft();
                else bot.play_turn();
            }
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what();
            if constexpr(wb::config::production_controller)
                std::cout << " stage " << wb::error_stage << " actor " << input_actor << " input-round " << input_round;
            std::cout << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        cambc::end_turn();
    }
}
