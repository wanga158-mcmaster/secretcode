#include "opt/common.hpp"
#include <exception>
#include <iostream>
namespace opt { int c6_self_test(); }
int main(int argc, char** argv) {
    try {
        if (argc > 1) return opt::run_c6(opt::parse_options(argc, argv));
        const int result = opt::c6_self_test();
        std::cout << "C6 learning, family-leakage, duplicate and checkpoint checks passed\n";
        return result;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
