#include "opt/oracle.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        opt::Options options;
        std::string python;
        if (argc==4) {
            python=argv[1]; options.repo=opt::fs::absolute(argv[2]); options.out=opt::fs::absolute(argv[3]);
        } else if (argc==1) {
            options.repo=opt::fs::current_path();
            while (!opt::fs::is_regular_file(options.repo/"bots/contenders/tools/optimization/tests/c11_oracle_fixture.py")) {
                if (options.repo==options.repo.root_path()) throw std::runtime_error("Cannot locate oracle fixture repository");
                options.repo=options.repo.parent_path();
            }
            if (const char* configured=std::getenv("OPT_PYTHON")) python=configured;
            const char* user=std::getenv("USERPROFILE");
            for (const auto& candidate:{options.repo/"runtime/python/python.exe",
                options.repo/"bots/contenders/tools/optimization/runtime/python/python.exe",
                opt::fs::path(user?user:"")/".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"})
                if (python.empty() && opt::fs::is_regular_file(candidate)) python=candidate.string();
            if (python.empty()) python="python3";
            options.out=options.repo/".cache/contenders/optimization/c11/oracle-guard";
        } else throw std::runtime_error("Usage: c11_oracle_guard [PYTHON REPO OUT]");
        options.extra["oracle"]={python};
        options.extra["oracle-arg"]={(options.repo/"bots/contenders/tools/optimization/tests/c11_oracle_fixture.py").string()};
        opt::fs::create_directories(options.out);
        opt::write_json(options.out/"response.json",opt::Json{{"schema","contenders-native-oracle/v1"},{"native",true}});
        for (const auto* behavior:{"no_write","wrong_hash"}) {
            bool rejected=false;
            try { (void)opt::oracle::invoke(options,opt::Json{{"kind","identity"},{"behavior",behavior}},options.out); }
            catch (const std::exception&) { rejected=true; }
            if (!rejected) throw std::runtime_error(std::string("Stale/mismatched oracle response accepted: ")+behavior);
        }
        const auto valid=opt::oracle::invoke(options,opt::Json{{"kind","identity"},{"behavior","valid"}},options.out);
        if (valid.value("native",true)) throw std::runtime_error("Fixture must not claim native execution");
        std::cout<<"C11 fresh response and request binding tests passed\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
