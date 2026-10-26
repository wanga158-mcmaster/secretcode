#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace plan4 {
// One bounded write per row avoids locale/stream formatting for every field.
// The buffer is deliberately uninitialized; only its written prefix is read.
class LogRow {
    char bytes_[2048];
    std::size_t size_ = 0;
    bool overflow_ = false;

    void text(std::string_view value) {
        if (value.empty()) return;
        if (value.size() > sizeof(bytes_) - size_) { overflow_ = true; return; }
        std::memcpy(bytes_ + size_, value.data(), value.size());
        size_ += value.size();
    }
    void key(std::string_view name) { text(" "); text(name); text("="); }
public:
    explicit LogRow(std::string_view kind, const char* build = nullptr, unsigned protocol = 2) {
        text("p4 "); text(kind);
        if (build) { field("v", protocol); field("build", build); }
    }
    LogRow& field(std::string_view name, std::string_view value) {
        key(name); text(value); return *this;
    }
    template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    LogRow& field(std::string_view name, T value) {
        key(name);
        if (overflow_) return *this;
        // bool is intentionally widened: std::to_chars(bool) is deleted.
        const auto result = [&] {
            if constexpr (std::is_signed_v<T>) return std::to_chars(bytes_ + size_, bytes_ + sizeof(bytes_), std::int64_t(value));
            else return std::to_chars(bytes_ + size_, bytes_ + sizeof(bytes_), std::uint64_t(value));
        }();
        if (result.ec != std::errc{}) overflow_ = true;
        else size_ = std::size_t(result.ptr - bytes_);
        return *this;
    }
    bool valid() const { return !overflow_; }
    std::string_view view() const { return {bytes_, size_}; }
    void emit(std::ostream& output = std::cout) const {
        if (overflow_) output << "plan4 error: telemetry capacity\n";
        else { output.write(bytes_, std::streamsize(size_)); output.put('\n'); }
    }
};
} // namespace plan4
