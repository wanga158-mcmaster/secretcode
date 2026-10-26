#include "opt/fuel.hpp"
#include <cassert>
#include <set>
int main() {
    const auto all=opt::fuel::family();
    assert(all.size()==640);
    std::set<std::string> ids;
    for(const auto& j:all) { const auto c=opt::fuel::decode(j); assert(opt::fuel::encode(c)==j); ids.insert(j.dump()); }
    assert(ids.size()==all.size());
    bool bad=false; try { (void)opt::fuel::decode(opt::Json{{"unroll",3}}); } catch(...) { bad=true; }
    assert(bad);
    auto small=opt::fuel::family(opt::Json{{"layouts",opt::Json::array({"packed16"})},
        {"distances",opt::Json::array({"lut"})},{"reductions",opt::Json::array({"mask"})},
        {"unrolls",opt::Json::array({8})},{"early_zero",opt::Json::array({true})},
        {"axis_prune",opt::Json::array({false})}});
    assert(small.size()==1);
    return 0;
}
