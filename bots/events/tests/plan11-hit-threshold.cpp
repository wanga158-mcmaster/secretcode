#include <cassert>
#include <cmath>
#include <limits>
#include "../common/src/events.h"
int main() {
    // Independent sequential oracle: one rounded subtraction per basic hit.
    int cases=0;
    for(float damage:{.1f,.3f,1.1f,44.8f,49.5f,77.f})for(int n=1;n<=64;++n) {
        const float boundary=float(n)*damage;
        for(const float hp:{std::nextafter(boundary,0.f),boundary,
                            std::nextafter(boundary,std::numeric_limits<float>::infinity())}) {
            int expected=0;volatile float remaining=hp;
            while(remaining>0&&expected<10000) {remaining=remaining-damage;++expected;}
            assert(p11::attacks(hp,damage)==expected);++cases;
        }
    }
    assert(cases==1152);
    // A product reaches the threshold in7, but sequential subtraction needs8.
    assert(7*44.8f>=313.6f&&p11::attacks(313.6f,44.8f)==8);
    assert(p11::attacks(100000.f,.01f)==10000);
}
