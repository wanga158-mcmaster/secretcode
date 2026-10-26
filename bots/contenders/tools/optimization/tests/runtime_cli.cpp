// Link the actual CLI/common runtime for process-failure probes without rebuilding
// or claiming to exercise any optimization algorithm. Production builds exclude tests/.
#include "opt/common.hpp"
namespace opt {
int unavailable(const Options&){throw std::runtime_error("runtime CLI fixture supports utilities only");}
int run_c1(const Options& o){return unavailable(o);} int run_c2(const Options& o){return unavailable(o);}
int run_c3(const Options& o){return unavailable(o);} int run_c4(const Options& o){return unavailable(o);}
int run_c5(const Options& o){return unavailable(o);} int run_c6(const Options& o){return unavailable(o);}
int run_c7(const Options& o){return unavailable(o);} int run_c8(const Options& o){return unavailable(o);}
int run_c9(const Options& o){return unavailable(o);} int run_c10(const Options& o){return unavailable(o);}
int run_c11(const Options& o){return unavailable(o);} int run_qualification(const Options& o){return unavailable(o);}
int run_micro(const Options& o){return unavailable(o);}
}
