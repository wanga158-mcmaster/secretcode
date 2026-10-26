// Diagnostic qualification harness; no native game or strength evidence.
#include "opt/qualification.hpp"
#include <iostream>

int main(int argc,char** argv) {
    try {
        opt::install_signal_handlers();
        const auto options=opt::parse_options(argc,argv);
        if(options.mode=="summary") {
            const auto input=opt::read_json(options.input);
            opt::write_json(options.out/"summary.json",opt::qualification::summarize(
                input.at("rows"),options.seed,input.value<std::size_t>("samples",2000)));
            return 0;
        }
        if(options.mode=="plan") {
            opt::write_json(options.out/"plan.json",opt::qualification::case_plan(opt::read_json(options.input),options.repo));
            return 0;
        }
        if(options.mode!="qualify")throw std::runtime_error("unknown qualification harness mode");
        return opt::run_qualification(options);
    } catch(const std::exception& error) {
        std::cerr<<"error: "<<error.what()<<'\n';return 1;
    }
}
