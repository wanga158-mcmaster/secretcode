#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace opt {
namespace fs = std::filesystem;

// Deliberately small, strict JSON API shared by the standalone engines.
class Json {
public:
    using array_t = std::vector<Json>;
    using object_t = std::map<std::string, Json>;
    using value_t = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, array_t, object_t>;
    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool v) : value_(v) {}
    template<class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T,bool>, int> = 0>
    Json(T v) {
        if constexpr (std::is_unsigned_v<T>) value_ = static_cast<std::uint64_t>(v);
        else value_ = static_cast<std::int64_t>(v);
    }
    Json(double v);
    Json(float v) : Json(static_cast<double>(v)) {}
    Json(const char* v) : value_(std::string(v)) {}
    Json(std::string v) : value_(std::move(v)) {}
    Json(array_t v) : value_(std::move(v)) {}
    Json(object_t v) : value_(std::move(v)) {}
    Json(std::initializer_list<std::pair<const std::string, Json>> v) : value_(object_t(v)) {}
    static Json object() { return object_t{}; }
    static Json array(std::initializer_list<Json> v = {}) { return array_t(v); }
    static Json parse(const std::string& text);
    std::string dump(int indent = -1) const;
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_boolean() const { return std::holds_alternative<bool>(value_); }
    bool is_number_integer() const { return std::holds_alternative<std::int64_t>(value_) || is_number_unsigned(); }
    bool is_number_unsigned() const { return std::holds_alternative<std::uint64_t>(value_); }
    bool is_number() const { return is_number_integer() || std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<array_t>(value_); }
    bool is_object() const { return std::holds_alternative<object_t>(value_); }
    bool contains(const std::string& key) const;
    std::size_t size() const;
    bool empty() const { return size() == 0; }
    Json& operator[](const std::string& key) &;
    const Json& operator[](const std::string& key) const &;
    Json operator[](const std::string& key) &&;
    Json operator[](const std::string& key) const &&;
    Json& operator[](std::size_t index) &;
    const Json& operator[](std::size_t index) const &;
    Json operator[](std::size_t index) &&;
    Json operator[](std::size_t index) const &&;
    Json& at(const std::string& key) &;
    const Json& at(const std::string& key) const &;
    Json at(const std::string& key) &&;
    Json at(const std::string& key) const &&;
    Json& at(std::size_t index) &;
    const Json& at(std::size_t index) const &;
    Json at(std::size_t index) &&;
    Json at(std::size_t index) const &&;
    array_t& as_array() &;
    const array_t& as_array() const &;
    array_t as_array() &&;
    array_t as_array() const &&;
    object_t& as_object() &;
    const object_t& as_object() const &;
    object_t as_object() &&;
    object_t as_object() const &&;
    void push_back(Json v) { as_array().push_back(std::move(v)); }
    template<class T> T get() const {
        if constexpr (std::is_same_v<T, Json>) return *this;
        else if constexpr (std::is_same_v<T, std::string>) {
            if (!is_string()) throw std::runtime_error("JSON value is not a string");
            return std::get<std::string>(value_);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!is_boolean()) throw std::runtime_error("JSON value is not a boolean");
            return std::get<bool>(value_);
        } else if constexpr (std::is_integral_v<T>) {
            if (!is_number()) throw std::runtime_error("JSON value is not a number");
            const long double n = std::holds_alternative<std::int64_t>(value_) ? static_cast<long double>(std::get<std::int64_t>(value_))
                : is_number_unsigned() ? static_cast<long double>(std::get<std::uint64_t>(value_))
                : static_cast<long double>(std::get<double>(value_));
            if (std::trunc(n) != n || n < static_cast<long double>(std::numeric_limits<T>::lowest())
                || n > static_cast<long double>(std::numeric_limits<T>::max()))
                throw std::runtime_error("JSON integer conversion is out of range or fractional");
            return static_cast<T>(n);
        } else if constexpr (std::is_floating_point_v<T>) {
            if (!is_number()) throw std::runtime_error("JSON value is not a number");
            if (std::holds_alternative<std::int64_t>(value_)) return static_cast<T>(std::get<std::int64_t>(value_));
            if (is_number_unsigned()) return static_cast<T>(std::get<std::uint64_t>(value_));
            return static_cast<T>(std::get<double>(value_));
        } else if constexpr (std::is_same_v<T,array_t>) return as_array();
        else if constexpr (std::is_same_v<T,object_t>) return as_object();
        else { static_assert(!sizeof(T), "Unsupported Json::get type"); }
    }
    template<class T> T value(const std::string& key, T fallback) const {
        return contains(key) ? at(key).template get<T>() : fallback;
    }
    std::string value(const std::string& key, const char* fallback) const {
        return value<std::string>(key, fallback);
    }
    bool operator==(const Json& other) const;
    bool operator!=(const Json& other) const { return !(*this == other); }
    const value_t& raw() const { return value_; }
private:
    value_t value_;
};

using J = Json;
struct Options {
    std::string mode;
    fs::path input, out, repo, executable;
    std::uint64_t seed{1}, iterations{1000};
    double seconds{0};
    unsigned threads{1};
    bool resume{false};
    std::map<std::string, std::vector<std::string>> extra;
    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& fallback = "") const;
    std::int64_t integer(const std::string& key, std::int64_t fallback) const;
    double real(const std::string& key, double fallback) const;
    std::vector<std::string> values(const std::string& key) const;
};
Options parse_options(int argc, char** argv);
std::string help_text();
std::string read_text(const fs::path& path);
Json read_json(const fs::path& path);
void atomic_write(const fs::path& path, const std::string& text);
void write_json(const fs::path& path, const Json& value);
std::string sha256(const std::string& text);
std::string file_sha256(const fs::path& path);
std::string utc_now();
bool interrupted();
bool time_limit_reached();
void install_signal_handlers();

class Run {
public:
    Options options;
    Json config;
    Json state{Json::object()};
    std::mt19937_64 rng;
    std::uint64_t iteration{0};
    bool resumed{false};
    std::string engine, version, identity;
    Run(const Options& opts, std::string engine_name, std::string engine_version = "1");
    ~Run();
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    bool stop() const;
    double elapsed_seconds() const;
    double remaining_seconds() const;
    void advance(std::uint64_t count = 1) { iteration += count; }
    void checkpoint();
    void checkpoint(const Json& value) { state = value; checkpoint(); }
    void event(Json data);
    void progress(Json metrics);
    void result(Json result);
    void finish(Json result) { this->result(std::move(result)); }
private:
    std::int64_t started_ns_{};
    double prior_seconds_{};
    std::uint64_t event_sequence_{};
    std::intptr_t lock_handle_{-1};
    Run* previous_run_{};
    std::uint64_t committed_iteration_{};
    bool has_checkpoint_{false}, finished_{false};
    int uncaught_on_entry_{};
    void write_status(const std::string& status, bool failed);
};

struct ProcessResult { int exit_code{-1}; bool timed_out{false}; double seconds{}; };
ProcessResult run_process(const std::vector<std::string>& argv, const fs::path& cwd,
                          const fs::path& stdout_file, const fs::path& stderr_file,
                          double timeout_seconds = 0);
// External evaluator protocol: arguments --request FILE --response FILE. The default
// adapter is native_evaluator.py; custom --evaluator / --evaluator-arg bypass it.
Json evaluate(const Options& opts, const Json& request, const fs::path& work);

int run_c1(const Options&); int run_c2(const Options&); int run_c3(const Options&);
int run_c4(const Options&); int run_c5(const Options&); int run_c6(const Options&);
int run_c7(const Options&); int run_c8(const Options&); int run_c9(const Options&);
int run_c10(const Options&); int run_c11(const Options&);
} // namespace opt
