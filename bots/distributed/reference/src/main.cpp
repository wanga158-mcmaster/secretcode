#include "../../common/src/organization.h"
#include "../../common/src/runtime.h"
namespace {
struct ReferencePolicy {
    static const p6::organization::Profile& profile() {
        static const p6::organization::Profile value{"reference",
            {p6::Champion::marcella,p6::Champion::varg,p6::Champion::tavi,
             p6::Champion::vendetta,p6::Champion::vesper_vale}};
        return value;
    }
    p6::organization::Decision choose(const p6::organization::Input& in,bool) {
        return p6::organization::independent(in);
    }
};
}
int main() { return p6::runtime::run<ReferencePolicy>(); }
