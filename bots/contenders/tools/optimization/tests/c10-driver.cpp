#include "opt/common.hpp"
#include <iostream>
int main(int argc,char** argv) {
    opt::install_signal_handlers();
    try { return opt::run_c10(opt::parse_options(argc,argv)); }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
