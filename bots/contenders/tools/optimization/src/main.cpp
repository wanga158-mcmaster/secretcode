#include "opt/common.hpp"
#include "opt/qualification.hpp"
#include "opt/micro.hpp"
#include <iostream>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using opt::Json;
namespace fs = opt::fs;

int replay_evaluation(const opt::Options& options) {
    if(options.input.empty())throw std::runtime_error("evaluate requires --input REPRODUCE.json");
    opt::Run run(options,"evaluate","1");
    const auto request=run.config.contains("request")?run.config.at("request"):run.config;
    if(!request.is_object()||request.value("schema","")!="contenders-eval/v1"||!request.contains("kind"))
        throw std::runtime_error("evaluate requires a raw contenders-eval/v1 reproduction request, or an object containing request");
    opt::write_json(options.out/"request.json",request);
    const auto response=opt::evaluate(options,request,options.out/"evaluation");
    opt::write_json(options.out/"response.json",response);
    const bool healthy=response.value("healthy",false);
    run.state={{"request_sha256",opt::sha256(request.dump())},{"response",response}};
    run.advance();
    auto report=response;
    report["status"]=healthy?"reproduced_healthy":"reproduced_unhealthy";
    report["scope"]="One exported evaluator request; inspect per-case coverage and health in response.json.";
    report["artifacts"]=Json{{"request","request.json"},{"response","response.json"}};
    run.finish(report);
    return healthy?0:2;
}

int show_status(const opt::Options& options) {
    if(!fs::is_directory(options.out))throw std::runtime_error("run directory does not exist: "+options.out.string());
    std::cout<<"Run: "<<options.out.string()<<'\n';
    bool found=false;
    if(fs::is_regular_file(options.out/"run.json")){
        const auto run=opt::read_json(options.out/"run.json");found=true;
        std::cout<<"Engine: "<<run.value("engine","")<<" version "<<run.value("version","")<<'\n';
        std::cout<<"Identity: "<<run.value("identity","")<<'\n';
    }
    if(fs::is_regular_file(options.out/"checkpoint.json")){
        const auto saved=opt::read_json(options.out/"checkpoint.json");
        auto body=saved;body.as_object().erase("checkpoint_sha256");
        if(saved.value("checkpoint_sha256","")!=opt::sha256(body.dump()))throw std::runtime_error("checkpoint content checksum mismatch");
        std::cout<<"Checkpoint: iteration "<<saved.value<std::uint64_t>("iteration",0)<<", saved "<<saved.value("saved_utc","")<<'\n';found=true;
    }
    if(fs::is_regular_file(options.out/"run-status.json")){
        const auto status=opt::read_json(options.out/"run-status.json");found=true;
        std::cout<<"Latest invocation: "<<status.value("status","")<<", committed iteration "<<status.value<std::uint64_t>("committed_iteration",0)<<'\n';
        if(fs::is_regular_file(options.out/"run-status.md"))std::cout<<"Status detail: "<<(options.out/"run-status.md").string()<<'\n';
    }
    if(fs::is_regular_file(options.out/"report.json")){
        const auto report=opt::read_json(options.out/"report.json");found=true;
        for(const auto& key:{"status","scope","objective","iteration","elapsed_seconds"})if(report.contains(key))std::cout<<key<<": "<<report.at(key).dump()<<'\n';
        for(const auto& name:{"report.md","run-summary.md","report.json"})if(fs::is_regular_file(options.out/name))std::cout<<"Output: "<<(options.out/name).string()<<'\n';
    }
    if(!found)throw std::runtime_error("no run manifest, checkpoint or report found");
    return 0;
}

void require_plain_path(const fs::path& path) {
    const auto absolute=fs::absolute(path).lexically_normal();
    fs::path prefix=absolute.root_path();
    for(const auto& part:absolute.relative_path()){
        prefix/=part;
        std::error_code ec;
        if(fs::is_symlink(fs::symlink_status(prefix,ec)))throw std::runtime_error("packet path crosses a symbolic link: "+prefix.string());
        if(ec)throw std::runtime_error("cannot inspect packet path: "+prefix.string());
#ifdef _WIN32
        const DWORD attributes=GetFileAttributesW(prefix.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("cannot inspect packet path: "+prefix.string());
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("packet path crosses a reparse point: "+prefix.string());
#endif
    }
}

int verify_packet(const opt::Options& options) {
    if(options.repo.empty())throw std::runtime_error("verify-packet requires --repo PACKET_ROOT");
    const auto root=fs::absolute(options.repo).lexically_normal();
    require_plain_path(root);
    if(fs::exists(root/".packet-incomplete"))throw std::runtime_error("packet has an incomplete export marker");
    const auto manifest_path=options.input.empty()?root/"packet-manifest.json":options.input;
    require_plain_path(manifest_path);
    const auto manifest=opt::read_json(manifest_path);
    if(manifest.value("schema","")!="optimizer-packet/v1")throw std::runtime_error("unsupported packet manifest schema");
    const auto kind=manifest.value("kind","");
    if(kind!="model"&&kind!="native")throw std::runtime_error("invalid packet kind");
    const auto& files=manifest.at("files");
    if(!files.is_array()||files.empty())throw std::runtime_error("packet manifest has no payload files");
    if(manifest.value("payload_identity","")!=opt::sha256(files.dump()))throw std::runtime_error("packet payload identity mismatch");
    std::set<std::string> paths;
    std::uint64_t total=0;
    for(const auto& entry:files.as_array()){
        const auto name=entry.at("path").get<std::string>();
        if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)
            throw std::runtime_error("invalid packet relative path");
        std::size_t begin=0;
        while(begin<=name.size()){
            const auto end=name.find('/',begin);const auto component=name.substr(begin,end==std::string::npos?end:end-begin);
            if(component.empty()||component=="."||component=="..")throw std::runtime_error("invalid packet path component: "+name);
            if(end==std::string::npos)break;
            begin=end+1;
        }
        auto key=name;
#ifdef _WIN32
        for(auto& c:key)if(c>='A'&&c<='Z')c+=('a'-'A');
#endif
        if(!paths.insert(key).second)throw std::runtime_error("duplicate packet payload path: "+name);
        const auto relative=fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
        const auto file=root/relative;require_plain_path(file);
        if(!fs::is_regular_file(file))throw std::runtime_error("packet payload is not a regular file: "+name);
        const auto size=entry.at("size").get<std::uint64_t>();
        if(fs::file_size(file)!=size)throw std::runtime_error("packet file size mismatch: "+name);
        if(opt::file_sha256(file)!=entry.at("sha256").get<std::string>())throw std::runtime_error("packet file hash mismatch: "+name);
        if(size>std::numeric_limits<std::uint64_t>::max()-total)throw std::runtime_error("packet payload size overflow");
        total+=size;
    }
    if(total!=manifest.at("payload_bytes").get<std::uint64_t>())throw std::runtime_error("packet total size mismatch");
    if(options.has("strict")){
        for(auto entry=fs::recursive_directory_iterator(root);entry!=fs::recursive_directory_iterator();++entry){
            const auto name=entry->path().filename().string();
            require_plain_path(entry->path());
            if(entry->is_directory()&&(name==".cache"||name=="results")){entry.disable_recursion_pending();continue;}
            if(!entry->is_regular_file())continue;
            if(fs::equivalent(entry->path(),manifest_path))continue;
            const auto relative=fs::relative(entry->path(),root).generic_u8string();
            std::string key(reinterpret_cast<const char*>(relative.data()),relative.size());
#ifdef _WIN32
            for(auto& c:key)if(c>='A'&&c<='Z')c+=('a'-'A');
#endif
            if(!paths.contains(key))throw std::runtime_error("unlisted packet file: "+key);
        }
    }
    std::cout<<"Verified "<<kind<<" packet: "<<files.size()<<" files, "<<total<<" bytes\nIdentity: "<<manifest.at("payload_identity").get<std::string>()<<'\n';
    return 0;
}
} // namespace

int optimizer_main(int argc,char** argv){
    opt::install_signal_handlers();
    try{
        const auto options=opt::parse_options(argc,argv);
        if(options.mode=="help"||options.mode=="list"){std::cout<<opt::help_text();return 0;}
        if(options.mode=="evaluate")return replay_evaluation(options);
        if(options.mode=="status")return show_status(options);
        if(options.mode=="verify-packet")return verify_packet(options);
        if(options.mode=="qualify")return opt::run_qualification(options);
        if(options.mode=="c12"||options.mode=="micro")return opt::run_micro(options);
        using Entry=int(*)(const opt::Options&);
        const std::map<std::string,Entry> entries{{"c1",opt::run_c1},{"c2",opt::run_c2},{"c3",opt::run_c3},{"c4",opt::run_c4},{"c5",opt::run_c5},{"c6",opt::run_c6},{"c7",opt::run_c7},{"c8",opt::run_c8},{"c9",opt::run_c9},{"c10",opt::run_c10},{"c11",opt::run_c11}};
        const auto entry=entries.find(options.mode);if(entry==entries.end())throw std::runtime_error("unknown engine '"+options.mode+"'; run optimizer help");
        return entry->second(options);
    }catch(const std::exception& error){std::cerr<<"optimizer "<<(opt::interrupted()||opt::time_limit_reached()?"stopped: ":"error: ")<<error.what()<<'\n';return opt::interrupted()?130:opt::time_limit_reached()?0:1;}
}

#ifdef _WIN32
int wmain(int argc,wchar_t** wide_argv){
    std::vector<std::string> text;
    text.reserve(static_cast<std::size_t>(argc));
    for(int i=0;i<argc;++i){
        const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_argv[i],-1,nullptr,0,nullptr,nullptr);
        if(size<=0){std::cerr<<"optimizer error: invalid Unicode command line\n";return 1;}
        std::string arg(static_cast<std::size_t>(size),'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_argv[i],-1,arg.data(),size,nullptr,nullptr);
        arg.pop_back();text.push_back(std::move(arg));
    }
    std::vector<char*> argv;argv.reserve(text.size()+1);
    for(auto& arg:text)argv.push_back(arg.data());
    argv.push_back(nullptr);
    return optimizer_main(argc,argv.data());
}
#else
int main(int argc,char** argv){return optimizer_main(argc,argv);}
#endif
