// Campaign orchestration fixture. Synthetic child progress is not game evidence.
#include "opt/common.hpp"
#include "opt/micro.hpp"
#include "opt/micro_campaign.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

namespace opt::micro {
Options native_options(const Options& options) { return options; }
}

namespace {
int child(const opt::Options& options) {
    using namespace opt;
    const auto marker_pending = [](const char* name) {
        const auto value = std::getenv(name);
        return value && *value && !fs::exists(value);
    };
    const bool startup_deadline = marker_pending("MICRO_CAMPAIGN_FIXTURE_STARTUP_DEADLINE_ONCE");
    const bool startup_failure = marker_pending("MICRO_CAMPAIGN_FIXTURE_STARTUP_FAILURE_ONCE");
    Options child_options = options;
    // Give this synthetic child an earlier genuine runtime deadline, leaving the
    // parent ample time to consume its status instead of relying on sleep races.
    if (startup_deadline) {
        if (options.seconds <= 0) throw std::runtime_error("startup deadline fixture needs --seconds");
        child_options.seconds = std::min(options.seconds, 0.05);
    }
    Run run(child_options, "micro-campaign-fixture-child", "1");
    const Json search = run.config.value<Json>("search", Json::object());
    const auto cap = search.value<std::uint64_t>("max_nodes", 4);
    const auto previous = run.iteration;
    const Json invocation{{"previous", previous}, {"target", options.iterations},
        {"resume", options.resume}, {"test_fixture", true}};
    std::ofstream log(options.out / "fixture-invocations.jsonl", std::ios::binary | std::ios::app);
    log << invocation.dump() << '\n';
    log.close();
    if (startup_deadline) {
        atomic_write(std::getenv("MICRO_CAMPAIGN_FIXTURE_STARTUP_DEADLINE_ONCE"), "attempted\n");
        while (!run.stop()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        throw std::runtime_error("synthetic startup deadline before first algorithm checkpoint");
    }
    if (startup_failure) {
        atomic_write(std::getenv("MICRO_CAMPAIGN_FIXTURE_STARTUP_FAILURE_ONCE"), "attempted\n");
        throw std::runtime_error("synthetic ordinary startup failure before first algorithm checkpoint");
    }
    while (!run.stop() && run.iteration < cap) {
        run.advance();
        run.state["synthetic_prefixes"] = run.iteration;
        run.checkpoint();
    }
    const Json baseline{{"value", 10}, {"metrics", Json{{"fixture_only", true}}}};
    const Json best{{"value", 10 + run.iteration}, {"task_complete", run.iteration >= 2},
        {"metrics", Json{{"fixture_only", true}, {"prefixes", run.iteration}}}};
    const Json pv = Json::array({Json{{"owner", run.config.at("scenario").at("actor")},
        {"action", Json{{"type", "end"}}}}});
    write_json(options.out / "frontier.json", Json{{"test_fixture", true}, {"nodes", run.iteration}});
    write_json(options.out / "policy.json", Json{{"test_fixture", true}, {"principal_variation", pv}});
    write_json(options.out / "native-trace.json", Json{{"test_fixture", true}, {"native", false}});
    const auto fixture = run.config.value<Json>("fixture", Json::object());
    run.finish(Json{{"native_evaluations", run.iteration}, {"nodes", run.iteration},
        {"status", run.iteration >= cap ? fixture.value("cap_status", "exhausted_declared_tree") : "checkpointed"},
        {"baseline", baseline}, {"best", best}, {"principal_variation", pv},
        {"test_fixture", true}, {"scope", "Synthetic child accounting; no game physics or strength claim"}});
    return 0;
}

int rewind_checkpoint(int argc, char** argv) {
    using namespace opt;
    if (argc != 5) throw std::runtime_error("fixture-rewind CHECKPOINT KEY CURSOR");
    const fs::path path = argv[2];
    Json saved = read_json(path);
    auto& state = saved["state"];
    auto& selected = state["case_states"][argv[3]];
    const auto count = selected.at("evaluations").get<std::uint64_t>();
    saved["iteration"] = saved.at("iteration").get<std::uint64_t>() - count;
    selected["evaluations"] = 0;
    selected["status"] = "running";
    state["in_flight"] = argv[3];
    state["next_case"] = std::stoull(argv[4]);
    saved.as_object().erase("checkpoint_sha256");
    saved["checkpoint_sha256"] = sha256(saved.dump());
    write_json(path, saved);
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        opt::install_signal_handlers();
        if (argc > 1 && std::string(argv[1]) == "fixture-rewind") return rewind_checkpoint(argc, argv);
        const auto options = opt::parse_options(argc, argv);
        const auto config = opt::read_json(options.input);
        return opt::micro::campaign_requested(config) ? opt::run_micro_campaign(options) : child(options);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
