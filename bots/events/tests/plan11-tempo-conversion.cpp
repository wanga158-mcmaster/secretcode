#include <array>
#include <cassert>
#include <span>
#include "../tempo/src/conversion.h"

int main(){
    using p11_tempo::conversion::Input;
    using p11_tempo::conversion::Point;
    using p11_tempo::conversion::Threat;
    using p11_tempo::conversion::unpaid_lethal_step;

    // Packs A, Varg's owner turn R1273: a personally visible Heart can
    // dash next turn. Its current global cast was spent and
    // slot 0 had cooldown 7; neither prevents the ready slot 1 ground cast.
    // The optimistic shield includes the ready shield's full 250 HP.
    const Input varg{14,1080.0f,250.0f,6};
    const std::array heart{Threat{{20,12},4,1554.0f,true}};
    assert(unpaid_lethal_step(varg,Point{17,11},heart)); // actual farm step
    assert(unpaid_lethal_step(varg,Point{16,10},heart)); // staying also exposed
    assert(!unpaid_lethal_step(varg,Point{15,9},heart)); // legal west escape

    // The admission adapter additionally requires a presently legal direct
    // dash word (range 2 plus damage radius 1). The wider walk+dash envelope
    // above protects the exit; walking alone cannot license the refusal.
    const std::array direct_heart{Threat{{20,12},3,1554.0f,true}};
    assert(unpaid_lethal_step(varg,Point{17,11},direct_heart));
    assert(!unpaid_lethal_step(varg,Point{16,10},direct_heart));

    // This word only rejects conversion before a demonstrated lethal cast.
    // It does not lower Tempo's bank threshold or require an empty actor flee.
    assert(!unpaid_lethal_step(Input{0,1080.0f,250.0f,6},{17,11},heart));
    assert(!unpaid_lethal_step(Input{14,0.0f,250.0f,6},{17,11},heart));
    assert(!unpaid_lethal_step(Input{14,1080.0f,250.0f,1},{17,11},heart));
    assert(unpaid_lethal_step(Input{14,1080.0f,250.0f,2},{17,11},heart));
    assert(!unpaid_lethal_step(varg,{17,11},std::span<const Threat>{}));

    const std::array cooling{Threat{{20,12},4,1554.0f,false}};
    assert(!unpaid_lethal_step(varg,{17,11},cooling));
    const std::array survivable{Threat{{20,12},4,1329.0f,true}};
    assert(!unpaid_lethal_step(varg,{17,11},survivable));
    const std::array exactly_lethal{Threat{{20,12},4,1330.0f,true}};
    assert(unpaid_lethal_step(varg,{16,10},exactly_lethal));

    // float32 addition rounds this defence down to 2048. The shield leaves
    // 1024 damage, so the extra representable HP must prevent certification.
    const Input rounded_defence{14,1024.0001220703125f,1024.0f,6};
    const std::array rounded_damage{Threat{{20,12},4,2048.0f,true}};
    assert(!unpaid_lethal_step(rounded_defence,{17,11},rounded_damage));

    // Separate enemies must not lend each other missing evidence: the
    // lethal one is outside the footprint; the close one is nonlethal.
    const std::array incomplete{
        Threat{{20,12},4,1554.0f,true},
        Threat{{15,9},4,1329.0f,true},
        Threat{{15,9},4,1554.0f,false}
    };
    assert(!unpaid_lethal_step(varg,{15,9},incomplete));
}
