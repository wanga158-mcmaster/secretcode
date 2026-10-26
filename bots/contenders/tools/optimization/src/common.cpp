#include "opt/common.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <exception>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace opt {
namespace {
volatile std::sig_atomic_t stop_signal = 0;
thread_local Run* active_run = nullptr;
thread_local bool invocation_time_stop = false;
void signal_handler(int) { stop_signal = 1; }
std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
unsigned long process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}
void append_utf8(std::string& out, std::uint32_t code) {
    if (code <= 0x7f) out.push_back(static_cast<char>(code));
    else if (code <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (code >> 6))); out.push_back(static_cast<char>(0x80 | (code & 63))); }
    else if (code <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (code >> 12))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (code & 63))); }
    else { out.push_back(static_cast<char>(0xf0 | (code >> 18))); out.push_back(static_cast<char>(0x80 | ((code >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (code & 63))); }
}
bool valid_utf8(const std::string& s) {
    for (std::size_t p=0; p<s.size();) {
        const auto lead = static_cast<unsigned char>(s[p++]);
        if (lead < 0x80) continue;
        unsigned n; std::uint32_t c, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { n=1; c=lead & 31; minimum=0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { n=2; c=lead & 15; minimum=0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { n=3; c=lead & 7; minimum=0x10000; }
        else return false;
        if (p+n>s.size()) return false;
        while(n--) { const auto b=static_cast<unsigned char>(s[p++]); if ((b&0xc0)!=0x80) return false; c=(c<<6)|(b&63); }
        if (c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return false;
    }
    return true;
}
class Parser {
    const std::string& text; std::size_t pos{};
    [[noreturn]] void fail(const std::string& reason) const { throw std::runtime_error("JSON at byte " + std::to_string(pos) + ": " + reason); }
    void space() { while(pos<text.size() && (text[pos]==' ' || text[pos]=='\t' || text[pos]=='\r' || text[pos]=='\n')) ++pos; }
    bool take(char c) { if(pos<text.size() && text[pos]==c) { ++pos; return true; } return false; }
    std::uint32_t hex4() {
        std::uint32_t code=0;
        for(int i=0;i<4;++i) { if(pos>=text.size()) fail("short Unicode escape"); const char c=text[pos++]; unsigned v;
            if(c>='0'&&c<='9') v=c-'0'; else if(c>='a'&&c<='f') v=c-'a'+10; else if(c>='A'&&c<='F') v=c-'A'+10; else fail("bad Unicode escape"); code=(code<<4)|v; }
        return code;
    }
    std::string string() {
        if(!take('"')) fail("expected string");
        std::string out;
        while(pos<text.size()) {
            unsigned char c=static_cast<unsigned char>(text[pos++]);
            if(c=='"') { if(!valid_utf8(out)) fail("invalid UTF-8 string"); return out; }
            if(c<0x20) fail("unescaped control character");
            if(c!='\\') { out.push_back(static_cast<char>(c)); continue; }
            if(pos>=text.size()) fail("unfinished escape");
            switch(text[pos++]) {
            case '"': out+='"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break;
            case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break;
            case 'u': {
                auto code=hex4();
                if(code>=0xd800 && code<=0xdbff) { if(!take('\\') || !take('u')) fail("unpaired high surrogate"); auto low=hex4(); if(low<0xdc00 || low>0xdfff) fail("invalid low surrogate"); code=0x10000+((code-0xd800)<<10)+(low-0xdc00); }
                else if(code>=0xdc00 && code<=0xdfff) fail("unpaired low surrogate");
                append_utf8(out,code); break;
            }
            default: fail("unknown escape");
            }
        }
        fail("unterminated string");
    }
    Json number() {
        const auto start=pos; bool floating=false;
        take('-');
        if(take('0')) { if(pos<text.size() && text[pos]>='0'&&text[pos]<='9') fail("leading zero"); }
        else { if(pos>=text.size()||text[pos]<'1'||text[pos]>'9') fail("expected digit"); while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9') ++pos; }
        if(take('.')) { floating=true; const auto first=pos; while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9') ++pos; if(first==pos) fail("empty fraction"); }
        if(pos<text.size()&&(text[pos]=='e'||text[pos]=='E')) { floating=true; ++pos; if(pos<text.size()&&(text[pos]=='+'||text[pos]=='-')) ++pos; const auto first=pos; while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9') ++pos; if(first==pos) fail("empty exponent"); }
        const auto token=text.substr(start,pos-start);
        if(!floating) {
            if(token[0]=='-') { std::int64_t v; const auto r=std::from_chars(token.data(),token.data()+token.size(),v); if(r.ec!=std::errc{}) fail("integer overflow"); return v; }
            std::uint64_t v; const auto r=std::from_chars(token.data(),token.data()+token.size(),v); if(r.ec!=std::errc{}) fail("integer overflow");
            if(v<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return static_cast<std::int64_t>(v);
            return v;
        }
        double v{}; const auto r=std::from_chars(token.data(),token.data()+token.size(),v,std::chars_format::general);
        if(r.ec!=std::errc{} || r.ptr!=token.data()+token.size() || !std::isfinite(v)) fail("nonfinite or out-of-range number");
        return v;
    }
    Json value(unsigned depth) {
        if(depth>256) fail("nesting limit exceeded");
        space();
        if(pos>=text.size()) fail("expected value");
        const char c=text[pos];
        if(c=='"') return string();
        if(c=='{' ) { ++pos; auto out=Json::object(); space(); if(take('}')) return out;
            for(;;) { space(); auto key=string(); space(); if(!take(':')) fail("expected colon"); if(out.contains(key)) fail("duplicate object key"); out[key]=value(depth+1); space(); if(take('}')) return out; if(!take(',')) fail("expected comma"); }
        }
        if(c=='[') { ++pos; auto out=Json::array(); space(); if(take(']')) return out;
            for(;;) { out.push_back(value(depth+1)); space(); if(take(']')) return out; if(!take(',')) fail("expected comma"); }
        }
        for(const auto& [literal,result] : std::array<std::pair<const char*,Json>,3>{{{"true",true},{"false",false},{"null",nullptr}}}) {
            const auto length=std::strlen(literal); if(text.compare(pos,length,literal)==0) { pos+=length; return result; }
        }
        if(c=='-'||(c>='0'&&c<='9')) return number();
        fail("unexpected token");
    }
public:
    explicit Parser(const std::string& t):text(t){}
    Json parse() { auto out=value(0); space(); if(pos!=text.size()) fail("trailing content"); return out; }
};
std::string escaped(const std::string& s) {
    if(!valid_utf8(s)) throw std::runtime_error("cannot serialize invalid UTF-8");
    std::string out="\""; const char* hex="0123456789abcdef";
    for(unsigned char c:s) { switch(c) {
        case '"': out+="\\\""; break; case '\\': out+="\\\\"; break; case '\b': out+="\\b"; break; case '\f': out+="\\f"; break;
        case '\n': out+="\\n"; break; case '\r': out+="\\r"; break; case '\t': out+="\\t"; break;
        default: if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; } else out+=static_cast<char>(c);
    }} return out+'"';
}
void dump_json(const Json& j,std::string& out,int indent,unsigned level) {
    const auto pad=[&](unsigned l){ if(indent>=0) { out+='\n'; out.append(static_cast<std::size_t>(indent)*l,' '); }};
    if(j.is_null()) out+="null";
    else if(j.is_boolean()) out+=j.get<bool>()?"true":"false";
    else if(j.is_number_unsigned()) out+=std::to_string(j.get<std::uint64_t>());
    else if(j.is_number_integer()) out+=std::to_string(j.get<std::int64_t>());
    else if(j.is_number()) {
        std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(std::numeric_limits<double>::max_digits10)<<j.get<double>();
        auto number=s.str();
        if(number.find_first_of(".eE")==std::string::npos)number+=".0";
        out+=number;
    }
    else if(j.is_string()) out+=escaped(j.get<std::string>());
    else if(j.is_array()) { out+='['; bool first=true; for(const auto& v:j.as_array()) { if(!first) out+=','; pad(level+1); dump_json(v,out,indent,level+1); first=false; } if(!first) pad(level); out+=']'; }
    else { out+='{'; bool first=true; for(const auto& [k,v]:j.as_object()) { if(!first) out+=','; pad(level+1); out+=escaped(k); out+=indent>=0?": ":":"; dump_json(v,out,indent,level+1); first=false; } if(!first) pad(level); out+='}'; }
}
// Streaming SHA-256, so identity checks do not load executable/dataset bodies twice.
class Sha256 {
    std::array<std::uint32_t,8> h{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<unsigned char,64> buffer{}; std::size_t used{}; std::uint64_t bytes{};
    void block(const unsigned char* p) {
        static constexpr std::uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64]; for(unsigned i=0;i<16;++i) w[i]=(static_cast<std::uint32_t>(p[i*4])<<24)|(static_cast<std::uint32_t>(p[i*4+1])<<16)|(static_cast<std::uint32_t>(p[i*4+2])<<8)|p[i*4+3];
        for(unsigned i=16;i<64;++i) { const auto s0=std::rotr(w[i-15],7)^std::rotr(w[i-15],18)^(w[i-15]>>3); const auto s1=std::rotr(w[i-2],17)^std::rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(unsigned i=0;i<64;++i) { const auto s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25); const auto ch=(e&f)^(~e&g); const auto t1=hh+s1+ch+k[i]+w[i]; const auto s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22); const auto maj=(a&b)^(a&c)^(b&c); const auto t2=s0+maj; hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
public:
    void update(const char* p,std::size_t n) { bytes+=n; while(n) { auto take=std::min(n,64-used); std::memcpy(buffer.data()+used,p,take);used+=take;p+=take;n-=take;if(used==64){block(buffer.data());used=0;} } }
    std::string finish() { const auto bits=bytes*8; buffer[used++]=0x80; if(used>56){while(used<64)buffer[used++]=0;block(buffer.data());used=0;}while(used<56)buffer[used++]=0;for(int i=7;i>=0;--i)buffer[used++]=static_cast<unsigned char>(bits>>(i*8));block(buffer.data());std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto v:h)out<<std::setw(8)<<v;return out.str(); }
};
fs::path discover_repo(fs::path start) {
    std::error_code ec; start=fs::absolute(start,ec); if(ec) return {};
    if(fs::is_regular_file(start,ec)) start=start.parent_path();
    for(auto p=start;!p.empty();p=p.parent_path()) { if(fs::exists(p/"AGENTS.md") && fs::exists(p/"bots"/"contenders")) return p; if(p==p.parent_path()) break; } return {};
}
fs::path executable_path() {
#ifdef _WIN32
    std::wstring s(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n) return {};s.resize(n);return fs::path(s);
#else
    std::array<char,32768> s{};const auto n=readlink("/proc/self/exe",s.data(),s.size()-1);return n>0?fs::path(std::string(s.data(),static_cast<std::size_t>(n))):fs::path{};
#endif
}
void collect_bindings(const Json& value,const fs::path& base,const fs::path& repo,const std::string& pointer,Json& bindings) {
    if(value.is_object()) { for(const auto& [k,v]:value.as_object()) collect_bindings(v,base,repo,pointer+"/"+k,bindings); }
    else if(value.is_array()) { for(std::size_t i=0;i<value.size();++i) collect_bindings(value[i],base,repo,pointer+"/"+std::to_string(i),bindings); }
    else if(value.is_string()) {
        const auto text=value.get<std::string>(); if(text.empty()||text.size()>32768) return; const fs::path p(text);
        auto resolved=p.is_absolute()?p:base/p;std::error_code ec;
        if(!fs::is_regular_file(resolved,ec) && !repo.empty()) resolved=repo/p;
        if(fs::is_regular_file(resolved,ec)) bindings[pointer]=file_sha256(resolved);
    }
}
std::string csv_quote(std::string s) { std::string out="\"";for(char c:s){if(c=='"')out+='"';out+=c;}return out+'"'; }
std::uint64_t last_event_sequence(const fs::path& path,const std::string& identity) {
    std::ifstream events(path,std::ios::binary);
    std::uint64_t sequence=0;std::string line;
    while(std::getline(events,line)){
        try{
            const auto event=Json::parse(line);
            if(event.value("identity","")==identity)sequence=std::max(sequence,event.value<std::uint64_t>("sequence",0));
        }catch(const std::exception&){ /* A torn advisory log line cannot replace a checkpoint. */ }
    }
    if(events.bad())throw std::runtime_error("cannot inspect prior event sequence: "+path.string());
    return sequence;
}
std::string markdown_text(const std::string& text) {
    std::string out;
    for(char c:text){if(c=='\n'||c=='\r')out+=' ';else{if(c=='|'||c=='['||c==']'||c=='`')out+='\\';out+=c;}}
    return out;
}
std::string readable_label(std::string text) {
    for(char& c:text)if(c=='_')c=' ';
    if(!text.empty()&&text[0]>='a'&&text[0]<='z')text[0]-='a'-'A';
    return markdown_text(text);
}
std::string scalar_text(const Json& value) {
    if(value.is_string())return markdown_text(value.get<std::string>());
    if(value.is_number()&&!value.is_number_integer()){std::ostringstream out;out<<std::setprecision(9)<<value.get<double>();return out.str();}
    return value.dump();
}
void report_details(std::ostringstream& text,const std::string& heading,const Json& value,unsigned depth,unsigned& remaining) {
    if(remaining==0)return;
    if(value.is_object()){
        for(const auto& [key,item]:value.as_object()){
            if(remaining==0)break;
            if(item.is_object()&&depth<2)report_details(text,heading.empty()?readable_label(key):heading+" / "+readable_label(key),item,depth+1,remaining);
            else if(!item.is_object()&&!item.is_array()){
                text<<"- **"<<(heading.empty()?readable_label(key):heading+" / "+readable_label(key))<<":** "<<scalar_text(item)<<"\n";
                --remaining;
            }
        }
    }else if(value.is_array()){
        for(const auto& item:value.as_array()){
            if(remaining==0)break;
            if(item.is_string()||item.is_number()||item.is_boolean()){text<<"- "<<scalar_text(item)<<"\n";--remaining;}
        }
    }else{text<<scalar_text(value)<<"\n";--remaining;}
}
std::string human_report(const Run& run,const Json& result,bool separate_report) {
    std::ostringstream text;
    text<<"<!-- optimizer-generic-report/v1 -->\n# "<<markdown_text(run.engine)<<" result\n\n";
    if(result.contains("status"))text<<"Status: **"<<scalar_text(result.at("status"))<<"**.\n\n";
    for(const auto& key:{"summary","scope","domain","qualification","objective","search_objective","algorithm","search","selection"}){
        if(!result.contains(key))continue;
        const auto& value=result.at(key);
        if(!value.is_object()&&!value.is_array())text<<"**"<<readable_label(key)<<":** "<<scalar_text(value)<<"\n\n";
    }
    text<<"Work completed: "<<run.iteration<<" iterations; "<<std::fixed<<std::setprecision(2)<<run.elapsed_seconds()<<" cumulative seconds.\n\n";
    text<<"## Result metrics\n\nMetric | Value\n--- | ---\n";
    const std::set<std::string> hidden{"schema","engine","version","identity","completed_utc","interrupted","elapsed_seconds","iteration","iterations","status","scope","domain","qualification","objective","search_objective","algorithm","search","selection","summary","provenance","resume","dataset_digest","environment_identity","oracle_identity","catalog_sha256","resolved_identity"};
    unsigned count=0;
    for(const auto& [key,value]:result.as_object()){
        if(hidden.contains(key)||value.is_object()||value.is_array()||value.is_null())continue;
        if(value.is_string()&&value.get<std::string>().size()>160)continue;
        text<<readable_label(key)<<" | "<<scalar_text(value)<<"\n";++count;
    }
    if(count==0)text<<"Iterations | "<<run.iteration<<"\n";
    for(const auto& key:{"best","bounds","metrics","active","current","validation","train","test_report_only","health_failure","notes"}){
        if(!result.contains(key))continue;
        text<<"\n## "<<readable_label(key)<<"\n\n";unsigned remaining=30;report_details(text,"",result.at(key),0,remaining);
    }
    if(result.contains("cases")&&result.at("cases").is_array()&&!result.at("cases").empty()){
        text<<"\n## Case results\n\n";unsigned listed=0;
        for(const auto& item:result.at("cases").as_array()){
            if(listed++==8){text<<"Further cases are in report.json.\n";break;}
            if(!item.is_object())continue;
            text<<"### "<<markdown_text(item.value("case_id",item.value("id",std::string("Case ")+std::to_string(listed))))<<"\n\n";
            unsigned remaining=16;report_details(text,"",item,0,remaining);text<<'\n';
        }
    }
    text<<"\n## Output files\n\n";
    if(separate_report)text<<"- [Engine-specific report](report.md)\n";
    std::vector<fs::path> files;
    for(const auto& item:fs::directory_iterator(run.options.out))if(item.is_regular_file()&&item.path().filename()!="run.lock"&&item.path().filename()!="report.md"&&item.path().filename()!="run-summary.md"&&item.path().filename().string().find(".tmp.")==std::string::npos)files.push_back(item.path().filename());
    std::sort(files.begin(),files.end());
    for(const auto& file:files)text<<"- ["<<markdown_text(file.generic_string())<<"](<"<<file.generic_string()<<">)\n";
    if(result.contains("artifacts")){text<<"\nDeclared artifacts:\n\n";unsigned remaining=40;report_details(text,"",result.at("artifacts"),0,remaining);}
    if(result.contains("limitations")){text<<"\n## Limits and qualification\n\n";unsigned remaining=100;report_details(text,"",result.at("limitations"),0,remaining);}
    text<<"\nThe complete structured result is [report.json](report.json). The checkpoint retains the current search/training state; reported domain results do not automatically qualify a bot for release.\n";
    return text.str();
}
#ifdef _WIN32
std::wstring wide(const std::string& s) { if(s.empty()) return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("invalid UTF-8 process argument");std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);return w; }
std::wstring quote_arg(const std::wstring& arg) { std::wstring out=L"\"";unsigned slashes=0;for(wchar_t c:arg){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');out+=L'\"';return out; }
#endif
} // namespace

Json::Json(double v):value_(v) { if(!std::isfinite(v)) throw std::runtime_error("JSON cannot represent NaN or Infinity"); }
bool Json::operator==(const Json& other) const {
    if(!is_number()||!other.is_number())return value_==other.value_;
    if(is_number_integer()&&other.is_number_integer()) {
        if(is_number_unsigned()&&other.is_number_unsigned())return get<std::uint64_t>()==other.get<std::uint64_t>();
        if(!is_number_unsigned()&&!other.is_number_unsigned())return get<std::int64_t>()==other.get<std::int64_t>();
        const auto& signed_value=is_number_unsigned()?other:*this;
        const auto& unsigned_value=is_number_unsigned()?*this:other;
        return signed_value.get<std::int64_t>()>=0&&static_cast<std::uint64_t>(signed_value.get<std::int64_t>())==unsigned_value.get<std::uint64_t>();
    }
    if(!is_number_integer()&&!other.is_number_integer())return get<double>()==other.get<double>();
    const auto& integer=is_number_integer()?*this:other;
    const double floating=(is_number_integer()?other:*this).get<double>();
    if(std::trunc(floating)!=floating)return false;
    if(integer.is_number_unsigned())return floating>=0&&floating<std::ldexp(1.0,64)&&static_cast<std::uint64_t>(floating)==integer.get<std::uint64_t>();
    return floating>=-std::ldexp(1.0,63)&&floating<std::ldexp(1.0,63)&&static_cast<std::int64_t>(floating)==integer.get<std::int64_t>();
}
Json Json::parse(const std::string& t) { return Parser(t).parse(); }
std::string Json::dump(int indent) const { if(indent>32||indent< -1)throw std::runtime_error("invalid JSON indentation");std::string out;dump_json(*this,out,indent,0);return out; }
bool Json::contains(const std::string& key) const { return is_object()&&as_object().find(key)!=as_object().end(); }
std::size_t Json::size() const { if(is_object())return as_object().size();if(is_array())return as_array().size();if(is_string())return std::get<std::string>(value_).size();return 0; }
Json& Json::operator[](const std::string& key) & { if(is_null())value_=object_t{};return as_object()[key]; }
const Json& Json::operator[](const std::string& key) const & { return at(key); }
Json Json::operator[](const std::string& key) && { return std::move(at(key)); }
Json Json::operator[](const std::string& key) const && { return at(key); }
Json& Json::operator[](std::size_t index) & { return as_array().at(index); }
const Json& Json::operator[](std::size_t index) const & { return as_array().at(index); }
Json Json::operator[](std::size_t index) && { return std::move(at(index)); }
Json Json::operator[](std::size_t index) const && { return at(index); }
Json& Json::at(const std::string& key) & { return as_object().at(key); }
const Json& Json::at(const std::string& key) const & { return as_object().at(key); }
Json Json::at(const std::string& key) && { return std::move(as_object().at(key)); }
Json Json::at(const std::string& key) const && { return as_object().at(key); }
Json& Json::at(std::size_t index) & { return as_array().at(index); }
const Json& Json::at(std::size_t index) const & { return as_array().at(index); }
Json Json::at(std::size_t index) && { return std::move(as_array().at(index)); }
Json Json::at(std::size_t index) const && { return as_array().at(index); }
Json::array_t& Json::as_array() & {if(!is_array())throw std::runtime_error("JSON value is not an array");return std::get<array_t>(value_);}
const Json::array_t& Json::as_array() const & {if(!is_array())throw std::runtime_error("JSON value is not an array");return std::get<array_t>(value_);}
Json::array_t Json::as_array() && {if(!is_array())throw std::runtime_error("JSON value is not an array");return std::move(std::get<array_t>(value_));}
Json::array_t Json::as_array() const && {if(!is_array())throw std::runtime_error("JSON value is not an array");return std::get<array_t>(value_);}
Json::object_t& Json::as_object() & {if(!is_object())throw std::runtime_error("JSON value is not an object");return std::get<object_t>(value_);}
const Json::object_t& Json::as_object() const & {if(!is_object())throw std::runtime_error("JSON value is not an object");return std::get<object_t>(value_);}
Json::object_t Json::as_object() && {if(!is_object())throw std::runtime_error("JSON value is not an object");return std::move(std::get<object_t>(value_));}
Json::object_t Json::as_object() const && {if(!is_object())throw std::runtime_error("JSON value is not an object");return std::get<object_t>(value_);}

bool Options::has(const std::string& key)const{return extra.contains(key);}
std::string Options::get(const std::string& key,const std::string& fallback)const{const auto p=extra.find(key);return p==extra.end()||p->second.empty()?fallback:p->second.back();}
std::int64_t Options::integer(const std::string& key,std::int64_t fallback)const{if(!has(key))return fallback;const auto s=get(key);std::int64_t n;auto r=std::from_chars(s.data(),s.data()+s.size(),n);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw std::runtime_error("--"+key+" requires an integer");return n;}
double Options::real(const std::string& key,double fallback)const{if(!has(key))return fallback;const auto s=get(key);double n;auto r=std::from_chars(s.data(),s.data()+s.size(),n);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size()||!std::isfinite(n))throw std::runtime_error("--"+key+" requires a finite number");return n;}
std::vector<std::string> Options::values(const std::string& key)const{const auto p=extra.find(key);return p==extra.end()?std::vector<std::string>{}:p->second;}

Options parse_options(int argc,char** argv) {
    Options o;o.executable=executable_path();
    if(argc<2){o.mode="help";return o;}o.mode=argv[1];
    if(o.mode=="--help"||o.mode=="-h")o.mode="help";
    for(int i=2;i<argc;++i){std::string key=argv[i];if(!key.starts_with("--"))throw std::runtime_error("unexpected positional argument: "+key);key.erase(0,2);std::string val;
        const auto eq=key.find('=');
        if(eq!=std::string::npos){val=key.substr(eq+1);key.resize(eq);}
        else if(key=="evaluator-arg"||key=="oracle-arg"){
            if(i+1>=argc)throw std::runtime_error("--"+key+" requires an argument");
            val=argv[++i];
        }
        else if(i+1<argc&&!std::string(argv[i+1]).starts_with("--")){val=argv[++i];}
        else val="true";
        if(key.empty())throw std::runtime_error("empty option");
        if(key=="help")o.mode="help";
        else if(key=="input")o.input=fs::path(std::u8string(reinterpret_cast<const char8_t*>(val.data()),val.size()));
        else if(key=="out")o.out=fs::path(std::u8string(reinterpret_cast<const char8_t*>(val.data()),val.size()));
        else if(key=="repo")o.repo=fs::path(std::u8string(reinterpret_cast<const char8_t*>(val.data()),val.size()));
        else if(key=="resume"){if(val!="true"&&val!="false")throw std::runtime_error("--resume requires true or false");o.resume=val=="true";}
        else if(key=="seed"||key=="iterations"||key=="threads"){std::uint64_t n{};auto r=std::from_chars(val.data(),val.data()+val.size(),n);if(r.ec!=std::errc{}||r.ptr!=val.data()+val.size())throw std::runtime_error("--"+key+" requires a nonnegative integer");if(key=="seed")o.seed=n;else if(key=="iterations")o.iterations=n;else{if(n==0||n>65536)throw std::runtime_error("--threads outside 1..65536");o.threads=static_cast<unsigned>(n);}}
        else if(key=="seconds"){auto r=std::from_chars(val.data(),val.data()+val.size(),o.seconds);if(r.ec!=std::errc{}||r.ptr!=val.data()+val.size()||!std::isfinite(o.seconds)||o.seconds<0)throw std::runtime_error("--seconds requires a finite nonnegative number");}
        else o.extra[key].push_back(val);
    }
    if(o.repo.empty())o.repo=discover_repo(fs::current_path());
    if(o.repo.empty())o.repo=discover_repo(o.executable);
    if(!o.repo.empty())o.repo=fs::absolute(o.repo).lexically_normal();
    if(o.out.empty())o.out=(o.repo.empty()?fs::current_path():o.repo)/".cache"/"contenders"/"optimization"/o.mode;
    o.out=fs::absolute(o.out).lexically_normal();if(!o.input.empty())o.input=fs::absolute(o.input).lexically_normal();
    return o;
}

std::string help_text(){return
"Contenders optimization engines (C++20, local/offline)\n"
"Usage: optimizer MODE --input CONFIG.json --out RUN [--resume] [--iterations N]\n"
"       [--seconds N] [--seed N] [--threads N] [--repo ROOT]\n\n"
"Modes: c1 program-search; c2 configuration; c3 economy; c4 clearing-atlas;\n"
"       c5 population-learning; c6 imitation; c7 league; c8 adversarial;\n"
"       c9 endgame; c10 fuel; c11 model-falsification;\n"
"       c12 (alias micro) native primitive micro-control search.\n"
"Utilities: evaluate --input REPRODUCE.json --out RUN replays an exported request;\n"
"           status --out RUN reads saved progress and result paths;\n"
"           verify-packet --repo PACKET_ROOT verifies an offline packet.\n"
"           qualify --input CONFIG.json --out RUN qualifies an exported candidate.\n"
"Run limits bound useful algorithm work; they are not local-game launch budgets.\n"
"--iterations is the cumulative target, including resumed work. --seconds limits\n"
"this invocation, including owned external evaluation. Ctrl+C stops at a safe boundary; an\n"
"interrupted external evaluator is terminated and its cache can be reused.\n"
"Every engine writes checkpoint.json, report.json, progress.csv and events.jsonl.\n"
"run-status.json records the latest invocation; failures preserve committed checkpoints.\n"
"Use each engine's documented input/profile for its native/model/data scope.\n"
"External evaluation: --evaluator EXE [--evaluator-arg ARG] or default native\n"
"adapter with --python PYTHON. No online game/server actions are performed.\n";}

std::string read_text(const fs::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+path.string());f.seekg(0,std::ios::end);const auto n=f.tellg();if(n<0||static_cast<std::uint64_t>(n)>1024ull*1024*1024)throw std::runtime_error("file exceeds 1 GiB JSON/text input limit: "+path.string());std::string s(static_cast<std::size_t>(n),'\0');f.seekg(0);if(n&&!f.read(s.data(),n))throw std::runtime_error("short read "+path.string());return s;}
Json read_json(const fs::path& path){try{return Json::parse(read_text(path));}catch(const std::exception& e){throw std::runtime_error(path.string()+": "+e.what());}}
std::string sha256(const std::string& s){Sha256 h;h.update(s.data(),s.size());return h.finish();}
std::string file_sha256(const fs::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot hash "+path.string());Sha256 h;std::array<char,65536>b;while(f){f.read(b.data(),b.size());const auto n=f.gcount();if(n>0)h.update(b.data(),static_cast<std::size_t>(n));}if(!f.eof())throw std::runtime_error("failed hash read "+path.string());return h.finish();}
void atomic_write(const fs::path& path,const std::string& text){
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());
    static std::atomic<std::uint64_t> counter{};
    auto temp=path;temp += ".tmp."+std::to_string(process_id())+"."+std::to_string(counter.fetch_add(1)+1);
#ifdef _WIN32
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot create atomic temporary "+temp.string());
    std::size_t done=0;bool ok=true;while(done<text.size()){DWORD n=0;const auto chunk=static_cast<DWORD>(std::min<std::size_t>(text.size()-done,1u<<26));if(!WriteFile(h,text.data()+done,chunk,&n,nullptr)||n!=chunk){ok=false;break;}done+=n;}if(ok)ok=FlushFileBuffers(h)!=0;CloseHandle(h);if(!ok)throw std::runtime_error("atomic write failed for "+path.string());
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("atomic replacement failed for "+path.string());
#else
    int fd=open(temp.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)throw std::runtime_error("cannot create atomic temporary "+temp.string());std::size_t done=0;while(done<text.size()){const auto n=write(fd,text.data()+done,text.size()-done);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);throw std::runtime_error("atomic write failed");}done+=static_cast<std::size_t>(n);}if(fsync(fd)!=0){close(fd);throw std::runtime_error("atomic fsync failed");}close(fd);if(rename(temp.c_str(),path.c_str())!=0)throw std::runtime_error("atomic rename failed");int dir=open(path.parent_path().empty()?".":path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(dir>=0){fsync(dir);close(dir);}
#endif
}
void write_json(const fs::path& path,const Json& value){atomic_write(path,value.dump(2)+"\n");}
std::string utc_now(){const auto now=std::chrono::system_clock::now();const auto t=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
gmtime_s(&tm,&t);
#else
gmtime_r(&t,&tm);
#endif
std::ostringstream s;s<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%SZ");return s.str();}
bool interrupted(){return stop_signal!=0;}
bool time_limit_reached(){return invocation_time_stop;}
void install_signal_handlers(){std::signal(SIGINT,signal_handler);std::signal(SIGTERM,signal_handler);}

Run::Run(const Options& opts,std::string name,std::string ver):options(opts),rng(opts.seed),engine(std::move(name)),version(std::move(ver)),started_ns_(steady_ns()),uncaught_on_entry_(std::uncaught_exceptions()){
    if(!active_run)invocation_time_stop=false;
    if(options.out.empty())throw std::runtime_error("output directory is required");
    fs::create_directories(options.out);
    const auto lockpath=options.out/"run.lock";
#ifdef _WIN32
    auto h=CreateFileW(lockpath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot open output lock");OVERLAPPED overlap{};if(!LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)){CloseHandle(h);throw std::runtime_error("another engine owns output directory: "+options.out.string());}lock_handle_=reinterpret_cast<std::intptr_t>(h);
#else
    int fd=open(lockpath.c_str(),O_CREAT|O_RDWR,0600);if(fd<0||flock(fd,LOCK_EX|LOCK_NB)!=0){if(fd>=0)close(fd);throw std::runtime_error("another engine owns output directory: "+options.out.string());}lock_handle_=fd;
#endif
    try{
        config=options.input.empty()?Json::object():read_json(options.input);if(!config.is_object())throw std::runtime_error("engine configuration must be a JSON object");
        auto bindings=Json::object();collect_bindings(config,options.input.empty()?fs::current_path():options.input.parent_path(),options.repo,"",bindings);
        for(const auto& rel:{"docs/reference/current-version.json","docs/reference/runtime-catalog.json","sdk/cpp/cambc.h"}){if(!options.repo.empty()&&fs::is_regular_file(options.repo/rel))bindings[std::string("repo:")+rel]=file_sha256(options.repo/rel);}
        auto semantic=Json::object();for(const auto& [k,v]:options.extra){if(k=="python"||k=="evaluation-timeout"||k=="timeout"||k=="checkpoint-every"||k=="progress-every")continue;auto a=Json::array();for(const auto& s:v)a.push_back(s);semantic[k]=std::move(a);}
        // Bind executable/script inputs, not arbitrary evaluator argument files: an
        // argument may intentionally name a result or one-shot interruption marker.
        // Native engines separately freeze their adapter's complete environment ID.
        for(const auto& kind:{"evaluator","validator","oracle"}){
            for(const auto& value:options.values(kind))if(fs::is_regular_file(fs::path(value)))bindings[std::string(kind)+"_executable"]=file_sha256(fs::path(value));
            for(const auto& value:options.values(std::string(kind)+"-arg")){
                const fs::path path(value);const auto ext=path.extension();
                if((ext==".py"||ext==".mjs"||ext==".js"||ext==".exe"||ext==".wasm")&&fs::is_regular_file(path))bindings[std::string(kind)+"_script:"+value]=file_sha256(path);
            }
        }
        const auto adapter=options.repo/"bots"/"contenders"/"tools"/"optimization"/"adapters"/"native_evaluator.py";
        const std::set<std::string> native_modes{"c1","c2","c5","c6","c7","c10","evaluate","qualify"};
        if(native_modes.contains(options.mode)&&!options.has("evaluator")&&!options.repo.empty()&&fs::is_regular_file(adapter))bindings["default_native_evaluator"]=file_sha256(adapter);
        auto executable=options.executable.empty()?executable_path():options.executable;
        Json specification{{"schema","contenders-run-identity/v1"},{"engine",engine},{"version",version},{"seed",options.seed},{"config",config},{"bindings",bindings},{"semantic_options",semantic},{"binary_sha256",fs::is_regular_file(executable)?file_sha256(executable):"unavailable"}};
        identity=sha256(specification.dump());
        const auto manifest_path=options.out/"run.json";const auto checkpoint_path=options.out/"checkpoint.json";
        bool occupied=false;for(const auto& p:fs::directory_iterator(options.out))if(p.path().filename()!="run.lock"){occupied=true;break;}
        if(occupied&&!options.resume)throw std::runtime_error("output is not empty; use --resume or a new --out: "+options.out.string());
        if(options.resume&&fs::exists(manifest_path)){const auto old=read_json(manifest_path);if(old.value("identity","")!=identity)throw std::runtime_error("resume refused: engine, executable, input, seed or bound file identity changed");}
        else if(options.resume&&occupied)throw std::runtime_error("resume refused: no valid run.json identity");
        if(options.resume&&fs::exists(checkpoint_path)){
            const auto saved=read_json(checkpoint_path);
            if(saved.value("schema","")!="contenders-checkpoint/v1"||saved.value("identity","")!=identity)throw std::runtime_error("resume refused: incompatible checkpoint");
            auto body=saved;body.as_object().erase("checkpoint_sha256");
            if(saved.value("checkpoint_sha256","")!=sha256(body.dump()))throw std::runtime_error("resume refused: checkpoint content checksum mismatch");
            state=saved.at("state");iteration=saved.at("iteration").get<std::uint64_t>();prior_seconds_=saved.value("elapsed_seconds",0.0);event_sequence_=saved.value<std::uint64_t>("event_sequence",0);
            std::istringstream random(saved.at("rng").get<std::string>());random>>rng;
            if(!random)throw std::runtime_error("invalid checkpoint RNG state");
            resumed=true;
            committed_iteration_=iteration;has_checkpoint_=true;
        }
        if(options.resume)event_sequence_=std::max(event_sequence_,last_event_sequence(options.out/"events.jsonl",identity));
        Json manifest{{"schema","contenders-run/v1"},{"identity",identity},{"engine",engine},{"version",version},{"seed",options.seed},{"specification",specification},{"started_utc",utc_now()},{"iterations_target",options.iterations},{"seconds_this_invocation",options.seconds},{"threads",options.threads}};
        if(!fs::exists(manifest_path))write_json(manifest_path,manifest);
        event({{"type",resumed?"resumed":"started"},{"iteration_target",options.iterations},{"resumed_checkpoint",resumed}});
        write_status("running",false);
        previous_run_=active_run;active_run=this;
        if(!previous_run_)invocation_time_stop=false;
    }catch(...){
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(lock_handle_));
#else
        close(static_cast<int>(lock_handle_));
#endif
        lock_handle_=-1;throw;
    }
}
Run::~Run(){
    if(lock_handle_!=-1&&!finished_&&std::uncaught_exceptions()>uncaught_on_entry_){
        try{write_status(interrupted()?"interrupted_checkpointed":time_limit_reached()?"time_limit_checkpointed":"failed_checkpoint_preserved",true);}
        catch(const std::exception& error){std::cerr<<"optimizer error: cannot persist final run status: "<<error.what()<<'\n';}
    }
    if(active_run==this)active_run=previous_run_;
    if(lock_handle_!=-1){
#ifdef _WIN32
CloseHandle(reinterpret_cast<HANDLE>(lock_handle_));
#else
close(static_cast<int>(lock_handle_));
#endif
}}
bool Run::stop()const{
    if(options.seconds>0&&remaining_seconds()<=0)invocation_time_stop=true;
    return interrupted()||iteration>=options.iterations||time_limit_reached();
}
double Run::elapsed_seconds()const{return prior_seconds_+static_cast<double>(steady_ns()-started_ns_)/1e9;}
double Run::remaining_seconds()const{return options.seconds>0?std::max(0.0,options.seconds-static_cast<double>(steady_ns()-started_ns_)/1e9):std::numeric_limits<double>::infinity();}
void Run::write_status(const std::string& status,bool failed){
    Json value{{"schema","contenders-run-status/v1"},{"engine",engine},{"version",version},{"identity",identity},{"status",status},{"updated_utc",utc_now()},
        {"checkpoint_available",has_checkpoint_},{"committed_iteration",committed_iteration_},{"interrupted",interrupted()},{"time_limit_reached",time_limit_reached()}};
    if(failed)value["scope"]="The invocation stopped before finishing its current work. Only the last committed checkpoint is restartable; changes after that checkpoint were not committed. Inspect launcher stderr and evaluator call logs for the cause.";
    write_json(options.out/"run-status.json",value);
    if(failed){
        std::ostringstream text;text<<"<!-- optimizer-generic-report/v1 -->\n# "<<markdown_text(engine)<<" run status\n\nStatus: **"<<status<<"**.\n\n"<<value.at("scope").get<std::string>()<<"\n\n";
        if(has_checkpoint_)text<<"Committed iteration: "<<committed_iteration_<<". Resume with the same input and seed plus `--resume`.\n\n[Checkpoint](checkpoint.json)\n\n";
        else text<<"No algorithm checkpoint was committed yet. `--resume` can retry startup and reuse completed native cache entries.\n\n";
        text<<"[Run status](run-status.json) · [Events](events.jsonl)\n";
        atomic_write(options.out/"run-status.md",text.str());
        if(!fs::is_regular_file(options.out/"report.json")){
            value["schema"]="contenders-result/v1";value["iteration"]=committed_iteration_;value["elapsed_seconds"]=elapsed_seconds();
            write_json(options.out/"report.json",value);atomic_write(options.out/"report.md",text.str());
        }
    }else{
        std::ostringstream text;text<<"<!-- optimizer-generic-report/v1 -->\n# "<<markdown_text(engine)<<" run status\n\nStatus: **"<<markdown_text(status)<<"**.\n\nLatest committed iteration: "<<committed_iteration_<<".\n\n[Structured status](run-status.json) · [Result](report.json)\n";
        atomic_write(options.out/"run-status.md",text.str());
    }
}
void Run::checkpoint(){
    std::ostringstream random;random<<rng;
    Json checkpoint{{"schema","contenders-checkpoint/v1"},{"identity",identity},{"engine",engine},{"version",version},{"iteration",iteration},{"elapsed_seconds",elapsed_seconds()},{"event_sequence",event_sequence_},{"rng",random.str()},{"state",state},{"saved_utc",utc_now()}};
    checkpoint["checkpoint_sha256"]=sha256(checkpoint.dump());
    write_json(options.out/"checkpoint.json",checkpoint);
    committed_iteration_=iteration;has_checkpoint_=true;
}
void Run::event(Json data){if(!data.is_object())data=Json{{"data",data}};data["identity"]=identity;data["engine"]=engine;data["iteration"]=iteration;data["elapsed_seconds"]=elapsed_seconds();data["sequence"]=++event_sequence_;data["utc"]=utc_now();std::ofstream out(options.out/"events.jsonl",std::ios::binary|std::ios::app);if(!out||!(out<<data.dump()<<'\n'))throw std::runtime_error("cannot append engine events");out.flush();}
void Run::progress(Json metrics){const auto payload=metrics.dump();metrics["type"]="progress";event(metrics);const auto path=options.out/"progress.csv";const bool header=!fs::exists(path)||fs::file_size(path)==0;std::ofstream out(path,std::ios::binary|std::ios::app);if(header)out<<"iteration,elapsed_seconds,metrics_json\n";out<<iteration<<','<<std::setprecision(12)<<elapsed_seconds()<<','<<csv_quote(payload)<<'\n';if(!out)throw std::runtime_error("cannot append progress CSV");std::cout<<engine<<" iteration="<<iteration<<" elapsed="<<std::fixed<<std::setprecision(1)<<elapsed_seconds()<<"s "<<payload<<'\n';}
void Run::result(Json result){
    if(!result.is_object())result=Json{{"result",result}};
    result["schema"]="contenders-result/v1";result["engine"]=engine;result["version"]=version;result["identity"]=identity;
    result["iteration"]=iteration;result["elapsed_seconds"]=elapsed_seconds();result["interrupted"]=interrupted();result["completed_utc"]=utc_now();
    result["invocation_stop_reason"]=interrupted()?"interrupted":time_limit_reached()?"time_limit":"completed_work";
    checkpoint();write_json(options.out/"report.json",result);
    bool separate_report=false;
    if(fs::is_regular_file(options.out/"report.md")){
        const auto previous=read_text(options.out/"report.md");
        separate_report=!previous.starts_with("<!-- optimizer-generic-report/v1 -->")&&!previous.starts_with("# "+engine+" result\n");
    }
    const auto report_path=options.out/(separate_report?"run-summary.md":"report.md");
    atomic_write(report_path,human_report(*this,result,separate_report));
    event({{"type","finished"},{"report","report.json"},{"human_report",report_path.filename().string()}});
    write_status(result.value("status","finished"),false);finished_=true;
    std::cout<<"Report: "<<report_path.string()<<"\nData: "<<(options.out/"report.json").string()<<'\n';
}

ProcessResult run_process(const std::vector<std::string>& args,const fs::path& cwd,const fs::path& stdout_path,const fs::path& stderr_path,double timeout){
    if(args.empty()||args[0].empty())throw std::runtime_error("empty process executable");
    if(timeout<0||!std::isfinite(timeout))throw std::runtime_error("invalid process timeout");
    bool invocation_bound=false;
    if(active_run&&active_run->options.seconds>0){
        const auto remaining=active_run->remaining_seconds();
        if(remaining<=0){invocation_time_stop=true;atomic_write(stdout_path,"");atomic_write(stderr_path,"Invocation time limit reached before external process launch.\n");return ProcessResult{124,true,0};}
        if(timeout==0||remaining<=timeout){timeout=remaining;invocation_bound=true;}
    }
    if(!stdout_path.parent_path().empty())fs::create_directories(stdout_path.parent_path());
    if(!stderr_path.parent_path().empty())fs::create_directories(stderr_path.parent_path());
    const auto begin=steady_ns();ProcessResult result;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE out=CreateFileW(stdout_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);HANDLE err=CreateFileW(stderr_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);if(err!=INVALID_HANDLE_VALUE)CloseHandle(err);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);throw std::runtime_error("cannot open process streams");}
    HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job){CloseHandle(out);CloseHandle(err);CloseHandle(input);throw std::runtime_error("cannot create process Job");}JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(job);CloseHandle(out);CloseHandle(err);CloseHandle(input);throw std::runtime_error("cannot configure process Job");}
    std::wstring command;for(const auto& arg:args){if(!command.empty())command+=L' ';command+=quote_arg(wide(arg));}STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=out;startup.hStdError=err;startup.hStdInput=input;PROCESS_INFORMATION process{};const auto cwd_w=cwd.empty()?std::wstring{}:cwd.wstring();const bool created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,cwd_w.empty()?nullptr:cwd_w.c_str(),&startup,&process)!=0;CloseHandle(out);CloseHandle(err);CloseHandle(input);if(!created){CloseHandle(job);throw std::runtime_error("cannot start process: "+args[0]+" (Windows error "+std::to_string(GetLastError())+")");}
    if(!AssignProcessToJobObject(job,process.hProcess)){TerminateProcess(process.hProcess,125);WaitForSingleObject(process.hProcess,INFINITE);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);throw std::runtime_error("cannot contain child process in owned Job");}ResumeThread(process.hThread);CloseHandle(process.hThread);
    for(;;){const auto wait=WaitForSingleObject(process.hProcess,50);if(wait==WAIT_OBJECT_0)break;if(wait==WAIT_FAILED){TerminateJobObject(job,125);WaitForSingleObject(process.hProcess,INFINITE);CloseHandle(process.hProcess);CloseHandle(job);throw std::runtime_error("process wait failed");}const double elapsed=static_cast<double>(steady_ns()-begin)/1e9;if(interrupted()||(timeout>0&&elapsed>=timeout)){result.timed_out=!interrupted();if(result.timed_out&&invocation_bound)invocation_time_stop=true;TerminateJobObject(job,interrupted()?130:124);WaitForSingleObject(process.hProcess,INFINITE);break;}}
    DWORD code=125;GetExitCodeProcess(process.hProcess,&code);result.exit_code=static_cast<int>(code);CloseHandle(process.hProcess);
    // Wait for the whole owned tree, not only the adapter parent, before releasing its Job.
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    bool confirmed_empty=false;
    for(unsigned attempt=0;attempt<100;++attempt){
        if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))break;
        if(accounting.ActiveProcesses==0){confirmed_empty=true;break;}
        // Job accounting can briefly lag a signaled parent process. Give naturally
        // exiting descendants a short grace period before treating them as leaks.
        if(attempt==50){
            struct JobIds { DWORD assigned{}, listed{}; ULONG_PTR ids[64]{}; } ids;
            const bool listed=QueryInformationJobObject(job,JobObjectBasicProcessIdList,&ids,sizeof(ids),nullptr)!=0;
            std::ofstream diagnostic(stderr_path,std::ios::binary|std::ios::app);
            diagnostic<<"optimizer owned Job cleanup: parent="<<process.dwProcessId<<" active="<<accounting.ActiveProcesses<<" listed="<<(listed?ids.listed:0)<<'\n';
            if(listed)for(DWORD i=0;i<std::min<DWORD>(ids.listed,64);++i){
                const auto pid=static_cast<DWORD>(ids.ids[i]);
                HANDLE observed=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
                DWORD code=0, length=32768;std::wstring path(length,L'\0');
                const bool got_exit=observed&&GetExitCodeProcess(observed,&code);
                const bool got_name=observed&&QueryFullProcessImageNameW(observed,0,path.data(),&length);
                diagnostic<<"  pid="<<pid<<" queried="<<got_exit<<" exit="<<code;
                if(got_name){path.resize(length);int n=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);std::string name(n,'\0');WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),name.data(),n,nullptr,nullptr);diagnostic<<" image="<<name;}
                diagnostic<<'\n';if(observed)CloseHandle(observed);
            }
            TerminateJobObject(job,125);if(result.exit_code==0)result.exit_code=125;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CloseHandle(job);
    if(!confirmed_empty)throw std::runtime_error("owned process-tree cleanup could not be confirmed");
#else
    const pid_t pid=fork();if(pid<0)throw std::runtime_error("fork failed");if(pid==0){setpgid(0,0);if(!cwd.empty()&&chdir(cwd.c_str())!=0)_exit(126);const int out=open(stdout_path.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600),err=open(stderr_path.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600),input=open("/dev/null",O_RDONLY);if(out<0||err<0||input<0)_exit(126);dup2(out,STDOUT_FILENO);dup2(err,STDERR_FILENO);dup2(input,STDIN_FILENO);close(out);close(err);close(input);std::vector<char*> av;for(const auto& a:args)av.push_back(const_cast<char*>(a.c_str()));av.push_back(nullptr);execvp(av[0],av.data());_exit(127);}setpgid(pid,pid);int status=0;
    for(;;){const auto got=waitpid(pid,&status,WNOHANG);if(got==pid)break;if(got<0&&errno!=EINTR)throw std::runtime_error("waitpid failed");const double elapsed=static_cast<double>(steady_ns()-begin)/1e9;if(interrupted()||(timeout>0&&elapsed>=timeout)){result.timed_out=!interrupted();if(result.timed_out&&invocation_bound)invocation_time_stop=true;kill(-pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(100));kill(-pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}break;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}kill(-pid,SIGKILL);result.exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
    result.seconds=static_cast<double>(steady_ns()-begin)/1e9;return result;
}

Json evaluate(const Options& opts,const Json& request,const fs::path& work){
    if(!request.is_object())throw std::runtime_error("evaluation request must be an object");
    fs::create_directories(work/"calls");
    auto req=request;
    if(!req.contains("schema"))req["schema"]="contenders-eval/v1";
    if(!req.contains("request_id"))req["request_id"]=sha256(req.dump());
    fs::path call;
    static std::atomic<std::uint64_t> invocation{};
    for(unsigned attempt=0;attempt<100;++attempt){
        call=work/"calls"/(std::to_string(process_id())+"-"+std::to_string(steady_ns())+"-"+std::to_string(invocation.fetch_add(1)));
        if(fs::create_directory(call))break;
        if(attempt==99)throw std::runtime_error("could not allocate unique evaluator invocation directory");
    }
    const auto request_path=fs::absolute(call/"request.json");
    const auto response_path=fs::absolute(call/"response.json");
    write_json(request_path,req);
    std::vector<std::string> command;const auto custom=opts.get("evaluator");
    if(!custom.empty()){command.push_back(custom);const auto extra=opts.values("evaluator-arg");command.insert(command.end(),extra.begin(),extra.end());}
    else{
        auto python=opts.get("python");if(python.empty()){
            for(const auto& p:{opts.repo/".tools"/"python"/"python.exe",opts.repo/"runtime"/"python"/"python.exe"})if(fs::is_regular_file(p)){python=p.string();break;}
#ifdef _WIN32
            if(python.empty()){wchar_t* profile=nullptr;std::size_t size=0;if(_wdupenv_s(&profile,&size,L"USERPROFILE")==0&&profile){const auto p=fs::path(profile)/".cache"/"codex-runtimes"/"codex-primary-runtime"/"dependencies"/"python"/"python.exe";free(profile);if(fs::is_regular_file(p))python=p.string();}}
#endif
            if(python.empty())python="python3";
        }
        command={python,"-B","-I","-X","utf8",(opts.repo/"bots"/"contenders"/"tools"/"optimization"/"adapters"/"native_evaluator.py").string()};
    }
    command.insert(command.end(),{"--request",request_path.string(),"--response",response_path.string(),"--repo",opts.repo.string()});
    double timeout=opts.real("evaluation-timeout",opts.real("timeout",0));
    if(!opts.has("evaluation-timeout")&&!opts.has("timeout")&&!opts.input.empty()){
        const auto input=read_json(opts.input);
        if(input.contains("evaluation")&&input.at("evaluation").is_object())timeout=input.at("evaluation").value("timeout_seconds",0.0);
    }
    const auto execution=run_process(command,opts.repo,fs::absolute(call/"stdout.log"),fs::absolute(call/"stderr.log"),timeout);
    if(execution.exit_code!=0||execution.timed_out){throw std::runtime_error(std::string(interrupted()?"evaluation interrupted":execution.timed_out?"evaluation timed out":"evaluation process failed with exit "+std::to_string(execution.exit_code))+"; inspect "+(call/"stderr.log").string());}
    if(!fs::is_regular_file(response_path))throw std::runtime_error("evaluator produced no response.json");
    auto response=read_json(response_path);
    if(!response.is_object())throw std::runtime_error("evaluation response must be an object");
    if(response.contains("request_id")&&response.at("request_id")!=req.at("request_id"))throw std::runtime_error("evaluation response request_id does not match");
    // I/O paths are provenance, not fitness. Keep them outside the returned response
    // so exact algorithm state can match across different output roots/restarts.
    write_json(work/"evaluation-last.json",Json{{"request",request_path.string()},{"response",response_path.string()},{"request_sha256",file_sha256(request_path)}});
    return response;
}
} // namespace opt
