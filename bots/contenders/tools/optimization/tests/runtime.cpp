#include "opt/common.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function fn, const std::string& message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

void json_tests() {
    using opt::Json;
    auto data = Json::parse(" {\"a\":[1,-2,0.125,true,false,null,\"a\\nb\"],\"unicode\":\"\\u03bb\\ud83d\\ude00\"} ");
    check(data.is_object() && data.size() == 2, "object parse");
    check(data["a"].is_array() && data["a"].size() == 7, "array parse");
    check(data["a"][1].get<int>() == -2, "negative integer");
    check(data["a"][2].get<double>() == 0.125, "fractional number");
    check(data["a"][3].get<bool>() && !data["a"][4].get<bool>(), "boolean parse");
    check(data["a"][5].is_null(), "null parse");
    check(data["a"][6].get<std::string>() == "a\nb", "escaped newline");
    check(data["unicode"].get<std::string>() == "\xce\xbb\xf0\x9f\x98\x80", "surrogate pair UTF-8");
    check(Json::parse(data.dump()) == data, "compact round trip");
    check(Json::parse(data.dump(2)) == data, "indented round trip");
    Json made{{"str", "path with spaces\\quote\""}, {"list", Json::array({1, 2, 3})}};
    made["number"] = 1.2345678901234567;
    check(Json::parse(made.dump())["number"].get<double>() == made["number"].get<double>(), "double precision round trip");
    check(made.value("missing", "fallback") == "fallback", "default string");
    check(made.value("missing", 7) == 7, "default integer");
    check(made.contains("list") && !made.contains("missing"), "contains");
    made["list"].push_back("tail");
    check(made["list"].size() == 4, "array append");
    int temporary_sum = 0;
    for (const auto& number : Json::array({1, 2, 3}).as_array()) temporary_sum += number.get<int>();
    check(temporary_sum == 6, "temporary array traversal lifetime");
    temporary_sum = 0;
    for (const auto& number : Json{{"nested", Json::array({4, 5})}}.at("nested").as_array()) temporary_sum += number.get<int>();
    check(temporary_sum == 9, "chained temporary traversal lifetime");
    temporary_sum = 0;
    for (const auto& entry : Json{{"a", 6}, {"b", 7}}.as_object()) temporary_sum += entry.second.get<int>();
    check(temporary_sum == 13, "temporary object traversal lifetime");
    check(Json::parse(Json(std::uint64_t{7}).dump()) == Json(std::uint64_t{7}), "signed/unsigned JSON round trip equality");
    check(Json(7) == Json(7.0), "integral floating equality");
    check(Json::parse(Json(7.0).dump()).dump()==Json(7.0).dump(), "integral double checkpoint canonical form");
    check(Json::parse(Json(-0.0).dump()).dump()==Json(-0.0).dump(), "negative zero checkpoint canonical form");
    check(Json::parse("9007199254740993") != Json(9007199254740992.0), "integer precision preserved in equality");
    check(Json(std::numeric_limits<std::uint64_t>::max()) == Json::parse("18446744073709551615"), "full uint64 JSON range");
    check(Json(std::numeric_limits<std::uint64_t>::max()) != Json(std::ldexp(1.0, 64)), "uint64/double upper boundary equality");
    rejects([] { Json(-1).get<unsigned>(); }, "negative number converted to unsigned");
    rejects([] { Json(1.5).get<int>(); }, "fractional number converted to integer");
    for (const std::string bad : {"", "[", "{", "[1,]", "{\"a\":1,}", "{\"a\":1,\"a\":2}",
        "01", "+1", "-.1", "1.", "1e", "1e+", "1e9999", "NaN", "Infinity", "null true",
        "\"\\x41\"", "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800x\"", "\"\\uD800\\u0041\""}) {
        rejects([&] { Json::parse(bad); }, "invalid JSON accepted: " + bad);
    }
    rejects([] { Json::parse(std::string("\"x\nq\"")); }, "raw control in string accepted");
    rejects([] { Json(std::numeric_limits<double>::infinity()); }, "infinite JSON number accepted");
    rejects([&] { data.at("missing"); }, "missing key accepted by at");
    rejects([&] { data["a"].at(100); }, "out-of-bounds array accepted");
    rejects([&] { data["a"].get<std::string>(); }, "wrong JSON type silently converted");
}

void hash_tests() {
    check(opt::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 empty vector");
    check(opt::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 abc vector");
    check(opt::sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA256 million-a vector");
}

void option_tests() {
    std::vector<std::string> args{"optimizer", "c4", "--input", "input with spaces.json", "--out", "output with spaces",
        "--iterations", "123", "--seed", "42", "--seconds", "2.5", "--threads", "3", "--resume",
        "--evaluator-arg", "first", "--evaluator-arg", "second", "--evaluator-arg", "--state", "--oracle-arg=--native"};
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    auto options = opt::parse_options(static_cast<int>(argv.size()), argv.data());
    check(options.mode == "c4", "CLI mode");
    check(options.input.filename() == "input with spaces.json", "CLI input spaces");
    check(options.out.filename() == "output with spaces", "CLI output spaces");
    check(options.iterations == 123 && options.seed == 42 && options.threads == 3, "CLI numeric options");
    check(options.seconds == 2.5 && options.resume, "CLI duration/resume");
    check(options.values("evaluator-arg") == std::vector<std::string>({"first", "second", "--state"}), "repeatable evaluator args");
    check(!options.has("state") && options.values("oracle-arg")==std::vector<std::string>({"--native"}), "flag-valued evaluator/oracle arguments");
}

void atomic_tests(const opt::fs::path& root) {
    auto path = root / "atomic directory" / "data.json";
    opt::atomic_write(path, "first\n");
    check(opt::read_text(path) == "first\n", "atomic first write");
    std::string replacement(1024 * 1024, 'b');
    replacement += "\n";
    opt::atomic_write(path, replacement);
    check(opt::read_text(path) == replacement, "atomic replacement is complete");
    check(opt::file_sha256(path) == opt::sha256(replacement), "file SHA equals bytes SHA");
    rejects([&] { opt::read_text(root / "does-not-exist"); }, "missing input read accepted");
}

opt::Options run_options(const opt::fs::path& root, const std::string& name) {
    opt::Options options;
    options.mode = "runtime-test";
    options.input = root / "input.json";
    options.out = root / name;
    options.repo = root / "isolated repository";
    opt::fs::create_directories(options.repo);
    options.seed = 1234567;
    options.iterations = 8;
    return options;
}

void draw_to(opt::Run& run, std::uint64_t target) {
    if (!run.state.contains("draws")) run.state["draws"] = opt::Json::array();
    while (run.iteration < target && !run.stop()) {
        run.state["draws"].push_back(static_cast<std::int64_t>(run.rng() % 1000000));
        run.advance();
        run.checkpoint();
    }
}

void recovery_tests(const opt::fs::path& root) {
    const opt::Json input{{"name", "deterministic restart"}, {"nested", opt::Json{{"alpha", 1}}}};
    opt::write_json(root / "input.json", input);
    opt::Json expected;
    std::string expected_identity;
    {
        opt::Run run(run_options(root, "uninterrupted"), "runtime-test", "test-v1");
        check(!run.resumed && run.iteration == 0, "fresh run state");
        draw_to(run, 8);
        check(run.stop(), "cumulative work limit");
        expected = run.state;
        expected_identity = run.identity;
        run.progress(opt::Json{{"quality", 17.5}});
        run.result(opt::Json{{"status", "complete"}, {"best", 17.5}});
        const auto report=opt::read_text(run.options.out/"report.md");
        check(report.find("**complete**")!=std::string::npos && report.find("17.5")!=std::string::npos, "human result summary");
        check(report.find("[report.json]")!=std::string::npos, "human report links structured result");
    }
    auto split = run_options(root, "split");
    split.iterations = 3;
    {
        opt::Run run(split, "runtime-test", "test-v1");
        draw_to(run, 3);
        check(run.stop(), "first segment cap");
        auto concurrent = split;
        concurrent.resume = true;
        rejects([&] { opt::Run duplicate(concurrent, "runtime-test", "test-v1"); }, "concurrent writer accepted");
    }
    auto previous_hash = opt::file_sha256(split.out / "checkpoint.json");
    rejects([&] { opt::Run accidental(split, "runtime-test", "test-v1"); }, "existing output accepted without resume");
    check(opt::file_sha256(split.out / "checkpoint.json") == previous_hash, "refused fresh run changed checkpoint");
    // A torn temporary is not a committed checkpoint and must not be selected on resume.
    opt::atomic_write(split.out / "checkpoint.json.tmp", "{\"torn\":");
    split.resume = true;
    split.iterations = 8;
    {
        opt::Run run(split, "runtime-test", "test-v1");
        check(run.resumed && run.iteration == 3, "checkpoint iteration restored");
        check(run.identity == expected_identity, "output path excluded from semantic identity");
        draw_to(run, 8);
        check(run.state == expected, "checkpoint RNG/state continuation differs from uninterrupted run");
    }
    previous_hash = opt::file_sha256(split.out / "checkpoint.json");
    auto changed_seed = split;
    changed_seed.seed++;
    rejects([&] { opt::Run changed(changed_seed, "runtime-test", "test-v1"); }, "changed seed accepted on resume");
    rejects([&] { opt::Run changed(split, "runtime-test", "test-v2"); }, "changed algorithm version accepted on resume");
    rejects([&] { opt::Run changed(split, "other-engine", "test-v1"); }, "changed engine accepted on resume");
    auto altered = input;
    altered["nested"]["alpha"] = 2;
    opt::write_json(root / "input.json", altered);
    rejects([&] { opt::Run changed(split, "runtime-test", "test-v1"); }, "changed input accepted on resume");
    check(opt::file_sha256(split.out / "checkpoint.json") == previous_hash, "failed resume changed checkpoint");
    opt::write_json(root / "input.json", input);
    const auto intact=opt::read_json(split.out/"checkpoint.json");
    auto damaged=intact;damaged["state"]["draws"][0]=123;
    opt::write_json(split.out/"checkpoint.json",damaged);
    rejects([&] { opt::Run changed(split,"runtime-test","test-v1"); }, "valid JSON checkpoint state corruption accepted");
    damaged=intact;damaged["iteration"]=7;
    opt::write_json(split.out/"checkpoint.json",damaged);
    rejects([&] { opt::Run changed(split,"runtime-test","test-v1"); }, "checkpoint iteration corruption accepted");
    opt::write_json(split.out/"checkpoint.json",intact);
    // Startup metadata is enough to recover an evaluation cache before the first checkpoint.
    auto precheckpoint = run_options(root, "precheckpoint");
    {
        opt::Run run(precheckpoint, "runtime-test", "test-v1");
        run.event(opt::Json{{"event", "before-first-candidate"}});
    }
    precheckpoint.resume = true;
    {
        opt::Run run(precheckpoint, "runtime-test", "test-v1");
        check(!run.resumed && run.iteration == 0, "pre-checkpoint restart must start algorithm fresh");
        check(run.identity == expected_identity, "pre-checkpoint restart identity");
    }
    auto finished=run_options(root,"uninterrupted");finished.resume=true;
    {
        opt::Run run(finished,"runtime-test","test-v1");
        run.finish({{"status","complete"}});
    }
    std::ifstream events(finished.out/"events.jsonl");
    std::string line;std::uint64_t sequence=0;
    while(std::getline(events,line)){
        const auto next=opt::Json::parse(line).at("sequence").get<std::uint64_t>();
        check(next>sequence,"event sequence duplicated or regressed after finished-run resume");sequence=next;
    }
}

void process_tests(const opt::fs::path& root, const opt::fs::path& executable) {
    auto directory = root / "process working directory";
    opt::fs::create_directories(directory);
    std::vector<std::string> payload{"a b", "quote\" and slash\\", "", "trailing\\", "\xce\xbb"};
    std::vector<std::string> command{executable.string(), "--echo"};
    command.insert(command.end(), payload.begin(), payload.end());
    const auto output = directory / "stdout file.json";
    const auto error = directory / "stderr file.txt";
    auto done = opt::run_process(command, directory, output, error, 5);
    check(done.exit_code == 0 && !done.timed_out, "process echo failed");
    auto observed = opt::read_json(output);
    check(opt::fs::equivalent(observed["cwd"].get<std::string>(), directory), "process working directory");
    auto args = observed["arguments"].as_array();
    check(args.size() == payload.size(), "process argument count");
    for (std::size_t i = 0; i < args.size(); ++i)
        check(args[i].get<std::string>() == payload[i], "process argument damaged: " + std::to_string(i));
    auto timed = opt::run_process({executable.string(), "--sleep", "1200"}, directory, output, error, 0.05);
    check(timed.timed_out && timed.exit_code != 0, "process timeout did not terminate child");
    check(timed.seconds < 5, "process timeout returned too late");
    const auto orphan_marker=directory/"descendant must not survive.txt";
    const auto leaked=opt::run_process({executable.string(),"--orphan-helper",orphan_marker.string()},directory,output,error,5);
#ifdef _WIN32
    check(leaked.exit_code==125,"unexpected surviving descendant did not fail Windows process health");
#else
    check(leaked.exit_code==0,"orphan parent failed unexpectedly");
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    check(!opt::fs::exists(orphan_marker),"owned descendant survived parent exit cleanup");
}

void evaluator_tests(const opt::fs::path& root, const opt::fs::path& executable) {
    opt::Options options;
    options.repo = root;
    options.extra["evaluator"] = {executable.string()};
    options.extra["evaluator-arg"] = {"--eval-helper"};
    const auto work = root / "evaluation";
    opt::fs::create_directories(work);
    opt::write_json(work / "response.json", {{"objective", 999}, {"healthy", true}});
    auto response = opt::evaluate(options, {{"kind", "good"}}, work);
    check(response.value("objective", 0) == 1 && response.value("healthy", false), "fresh evaluator response");
    check(opt::fs::is_regular_file(work / "evaluation-last.json"), "evaluator IO provenance");
    rejects([&] { opt::evaluate(options, {{"kind", "missing"}}, work); }, "stale evaluator response accepted");
    rejects([&] { opt::evaluate(options, {{"kind", "mismatch"}}, work); }, "mismatched evaluator request identity accepted");
    check(opt::read_json(work / "response.json").value("objective", 0) == 999, "prior response was not preserved");
}

void unattended_tests(const opt::fs::path& root,const opt::fs::path& executable){
    auto options=run_options(root,"deadline-cancel");options.seconds=0.15;
    options.extra["evaluator"]={executable.string()};options.extra["evaluator-arg"]={"--eval-helper"};
    const auto started=std::chrono::steady_clock::now();
    rejects([&]{
        opt::Run run(options,"runtime-test","test-v1");
        run.state={{"safe",1}};run.advance();run.checkpoint();
        run.state["safe"]=2;
        (void)opt::evaluate(options,{{"kind","slow"}},options.out/"evaluation");
    },"in-flight evaluator exceeded invocation limit without cancelling");
    check(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<2,"deadline cancellation returned too late");
    check(opt::time_limit_reached(),"invocation deadline was not distinguished from evaluator timeout");
    check(opt::read_json(options.out/"checkpoint.json").at("state").at("safe").get<int>()==1,"exception saved partially updated algorithm state");
    const auto status=opt::read_json(options.out/"run-status.json");
    check(status.value("status","")=="time_limit_checkpointed"&&status.value<std::uint64_t>("committed_iteration",0)==1,"deadline stop status lost committed work");
    check(opt::read_json(options.out/"report.json").value("status","")=="time_limit_checkpointed","first cancelled evaluator omitted report");
    options.seconds=5;options.resume=true;
    {
        opt::Run run(options,"runtime-test","test-v1");
        check(!opt::time_limit_reached()&&run.state.at("safe").get<int>()==1,"deadline restart did not reset invocation stop flag");
        run.finish({{"status","resumed_done"}});
    }
    check(opt::read_text(options.out/"report.md").find("**resumed_done**")!=std::string::npos,"resumed success retained stale failed human report");
    check(opt::read_text(options.out/"run-status.md").find("**resumed_done**")!=std::string::npos,"human invocation status remained stale after successful resume");
    auto timed=run_options(root,"evaluation-timeout");timed.seconds=5;
    timed.extra=options.extra;timed.extra["evaluation-timeout"]={"0.05"};
    rejects([&]{opt::Run run(timed,"runtime-test","test-v1");run.checkpoint();(void)opt::evaluate(timed,{{"kind","slow"}},timed.out/"evaluation");},"explicit evaluator timeout did not cancel");
    check(!opt::time_limit_reached(),"explicit evaluator timeout was mislabeled invocation completion");
    check(opt::read_json(timed.out/"run-status.json").value("status","")=="failed_checkpoint_preserved","infrastructure timeout did not produce failure status");
}
} // namespace

int test_main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--echo") {
            opt::Json output{{"cwd", opt::fs::current_path().string()}, {"arguments", opt::Json::array()}};
            for (int i = 2; i < argc; ++i) output["arguments"].push_back(argv[i]);
            std::cout << output.dump() << '\n';
            return 0;
        }
        if (argc > 2 && std::string(argv[1]) == "--sleep") {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));
            return 0;
        }
        if(argc>2&&std::string(argv[1])=="--sleep-write"){
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            opt::atomic_write(opt::fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[2]))),"leaked descendant\n");return 0;
        }
        if(argc>2&&std::string(argv[1])=="--orphan-helper"){
#ifdef _WIN32
            auto command=L"\""+opt::fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[0]))).wstring()+L"\" --sleep-write \""+opt::fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[2]))).wstring()+L"\"";
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
            if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return 126;
            CloseHandle(child.hThread);CloseHandle(child.hProcess);
#else
            const auto child=fork();if(child<0)return 126;
            if(child==0){std::this_thread::sleep_for(std::chrono::milliseconds(1500));opt::atomic_write(argv[2],"leaked descendant\n");_exit(0);}
#endif
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--eval-helper") {
            opt::fs::path request, response;
            for (int i = 2; i + 1 < argc; i += 2) {
                if (std::string(argv[i]) == "--request") request = argv[i + 1];
                if (std::string(argv[i]) == "--response") response = argv[i + 1];
            }
            const auto value = opt::read_json(request);
            const auto kind = value.value("kind", "");
            if (kind == "missing") return 0;
            if(kind=="slow")std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            opt::write_json(response, {{"objective", 1}, {"healthy", true}, {"request_id", kind == "mismatch" ? opt::Json("wrong-id") : value.at("request_id")}});
            return 0;
        }
        opt::fs::path scratch = ".cache/contenders/optimization/runtime-tests";
        if (argc > 2 && std::string(argv[1]) == "--scratch") scratch = argv[2];
        auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        auto root = opt::fs::absolute(scratch / ("case-" + std::to_string(unique)));
        opt::fs::create_directories(root);
        auto self = opt::fs::absolute(argv[0]);
        opt::install_signal_handlers();
        json_tests();
        hash_tests();
        option_tests();
        atomic_tests(root);
        recovery_tests(root);
        process_tests(root, self);
        evaluator_tests(root, self);
        unattended_tests(root,self);
        std::cout << checks << " runtime checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime test error: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wide_argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (required <= 0) return 2;
        std::string converted(static_cast<std::size_t>(required), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, converted.data(), required, nullptr, nullptr)) return 2;
        converted.pop_back();
        arguments.push_back(std::move(converted));
    }
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    return test_main(argc, argv.data());
}
#else
int main(int argc, char** argv) { return test_main(argc, argv); }
#endif
