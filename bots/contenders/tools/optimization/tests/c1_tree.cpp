#include "opt/program.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> static void rejects(F f) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, "invalid tree was accepted");
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--export-fixture") {
            const opt::Json fixture{{"feature", 1}, {"threshold", static_cast<double>(0.33333334f)},
                                    {"left", opt::Json{{"factor", 0.125}}}, {"right", opt::Json{{"factor", 8.0}}}};
            opt::atomic_write(argv[2], opt::program::cpp_header(fixture));
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) != "--self-test") return opt::run_c1(opt::parse_options(argc, argv));
        using opt::Json;
        const Json neutral{{"factor", 1.0}};
        const Json branch{{"feature", 1}, {"threshold", 0.5}, {"left", Json{{"factor", 2.0}}}, {"right", Json{{"factor", 0.5}}}};
        opt::program::validate(branch);
        std::vector<float> x(12, 0.5f);
        check(opt::program::factor(neutral, x) == 1.0f, "identity leaf changed score");
        check(opt::program::factor(branch, x) == 2.0f, "threshold equality must take left");
        x[1] = std::nextafter(0.5f, 1.0f);
        check(opt::program::factor(branch, x) == 0.5f, "next float above threshold must take right");
        check(opt::program::nodes(branch) == 3 && opt::program::depth(branch) == 1, "tree size/depth wrong");
        rejects([&] { opt::program::validate(Json{{"factor", 0.0}}); });
        rejects([&] { opt::program::validate(Json{{"factor", 9.0}}); });
        rejects([&] { opt::program::validate(Json{{"factor", 1.0}, {"unused", 0}}); });
        rejects([&] { auto bad = branch; bad["feature"] = 12; opt::program::validate(bad); });
        rejects([&] { auto bad = branch; bad["feature"] = 0.5; opt::program::validate(bad); });
        rejects([&] { auto bad = branch; bad["threshold"] = -0.1; opt::program::validate(bad); });
        rejects([&] { opt::program::validate(branch, 0); });
        rejects([&] { opt::program::factor(neutral, std::vector<float>(11)); });
        std::mt19937_64 rng(123);
        Json a = neutral, b = branch;
        for (int i = 0; i < 5000; ++i) {
            a = opt::program::mutate(a, rng, 6);
            b = opt::program::crossover(b, opt::program::random_tree(rng, 6), rng, 6);
            opt::program::validate(a); opt::program::validate(b);
            check(opt::program::nodes(a) <= 127 && opt::program::depth(a) <= 6, "mutation escaped grammar");
            check(opt::program::nodes(b) <= 127 && opt::program::depth(b) <= 6, "crossover escaped grammar");
            const float result = opt::program::factor(a, x);
            check(std::isfinite(result) && result >= 0.125f && result <= 8.0f, "mutation generated invalid factor");
        }
        check(opt::program::cpp_header(branch).find("features[1]") != std::string::npos, "export omitted feature");
        check(opt::program::ranking_signature(neutral)==opt::program::ranking_signature(Json{{"factor",4.0}}),"common multiplier changed the job-ranking diversity proxy");
        const Json job_branch{{"feature",6},{"threshold",.5},{"left",Json{{"factor",.125}}},{"right",Json{{"factor",8.0}}}};
        check(opt::program::ranking_signature(neutral)!=opt::program::ranking_signature(job_branch),"ranking probes missed opposing job priorities");
        std::cout << "C1 tree grammar, float boundary, bounded evolution: passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "C1 error: " << error.what() << '\n'; return 1; }
}
