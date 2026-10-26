// Formatting and capacity boundaries only; no SDK or engine imports.
#include "../common/telemetry.h"
#include "../common/model-runtime.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <streambuf>
#include <string>

namespace {
constexpr auto capacity_error = "plan4 error: telemetry capacity\n";

std::string emitted(const plan4::LogRow& row) {
    std::ostringstream output;
    row.emit(output);
    return output.str();
}

void integer_limits_and_booleans() {
    plan4::LogRow row("numbers");
    row.field("min64", std::numeric_limits<std::int64_t>::min())
        .field("max64", std::numeric_limits<std::int64_t>::max())
        .field("maxUnsigned64", std::numeric_limits<std::uint64_t>::max())
        .field("negative", -1)
        .field("zero", 0u)
        .field("signed8", std::numeric_limits<std::int8_t>::min())
        .field("unsigned8", std::numeric_limits<std::uint8_t>::max())
        .field("false", false)
        .field("true", true);
    const std::string expected =
        "p4 numbers min64=-9223372036854775808 max64=9223372036854775807 "
        "maxUnsigned64=18446744073709551615 negative=-1 zero=0 signed8=-128 unsigned8=255 false=0 true=1";
    assert(row.valid());
    assert(row.view() == expected);
    assert(emitted(row) == expected + '\n');
}

void string_fields_and_build_envelope() {
    plan4::LogRow row("decision", "reference-v8");
    row.field("model", std::string_view("m0"))
        .field("fallback", "bootstrap")
        .field("censor", std::string_view("death-observed"))
        .field("actor", 7);
    const std::string expected =
        "p4 decision v=2 build=reference-v8 model=m0 fallback=bootstrap censor=death-observed actor=7";
    assert(row.valid() && row.view() == expected);
    assert(emitted(row) == expected + '\n');
}

void only_the_written_prefix_is_exposed() {
    alignas(plan4::LogRow) std::array<unsigned char, sizeof(plan4::LogRow)> storage;
    std::fill(storage.begin(), storage.end(), 0x7f);
    auto* row = new (storage.data()) plan4::LogRow("summary");
    assert(row->view() == "p4 summary");
    assert(emitted(*row) == "p4 summary\n");
    row->field("payload", std::string(1000, 'Q'));
    assert(row->valid());
    row->~LogRow();
    // Reuse bytes containing a previous long row; a shorter row must not expose
    // either its suffix or the untouched poison at the end of the allocation.
    row = new (storage.data()) plan4::LogRow("x");
    row->field("n", 4);
    assert(row->view() == "p4 x n=4");
    assert(emitted(*row) == "p4 x n=4\n");
    row->~LogRow();
}

void exact_capacity_and_subsequent_overflow() {
    plan4::LogRow full("x");
    // Four prefix bytes plus seven bytes for " value=" leave 2,037 bytes.
    full.field("value", std::string(2037, 'a'));
    assert(full.valid());
    assert(full.view().size() == 2048);
    const auto complete = emitted(full);
    assert(complete.size() == 2049 && complete.back() == '\n');
    assert(complete.substr(0, 11) == "p4 x value=");
    assert(std::count(complete.begin(), complete.end(), 'a') == 2038); // Includes the key's a.
    full.field("next", 1);
    assert(!full.valid());
    assert(emitted(full) == capacity_error);
}

void constructor_string_and_numeric_overflows_fail_closed() {
    const std::string oversized(3000, 'X');
    plan4::LogRow oversized_kind(oversized);
    assert(!oversized_kind.valid() && emitted(oversized_kind) == capacity_error);
    plan4::LogRow oversized_build("summary", oversized.c_str());
    assert(!oversized_build.valid() && emitted(oversized_build) == capacity_error);
    plan4::LogRow oversized_value("decision");
    oversized_value.field("payload", oversized).field("later", 12);
    assert(!oversized_value.valid() && emitted(oversized_value) == capacity_error);
    // Leave too little space specifically for to_chars, after its key fits.
    plan4::LogRow oversized_integer("x");
    oversized_integer.field("s", std::string(2033, 'b'));
    assert(oversized_integer.valid() && oversized_integer.view().size() == 2040);
    oversized_integer.field("n", std::numeric_limits<std::uint64_t>::max());
    assert(!oversized_integer.valid());
    assert(emitted(oversized_integer) == capacity_error);
    assert(emitted(oversized_integer).find("p4 x") == std::string::npos);
}

class CountingBuffer : public std::streambuf {
public:
    std::string bytes;
    unsigned writes = 0, single_characters = 0;
protected:
    std::streamsize xsputn(const char* source, std::streamsize count) override {
        ++writes;
        bytes.append(source, std::size_t(count));
        return count;
    }
    int_type overflow(int_type character) override {
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            ++single_characters;
            bytes.push_back(traits_type::to_char_type(character));
        }
        return traits_type::not_eof(character);
    }
};

void one_payload_write_and_newline() {
    CountingBuffer buffer;
    std::ostream output(&buffer);
    plan4::LogRow row("summary", "reference-v8");
    row.field("round", 32).field("active", false);
    row.emit(output);
    assert(buffer.writes == 1 && buffer.single_characters == 1);
    assert(buffer.bytes == "p4 summary v=2 build=reference-v8 round=32 active=0\n");
}

void optional_coverage_keeps_other_models_on_their_existing_protocol() {
    struct LegacyModel {};
    struct CoveredModel {
        int mask;
        mutable int reads = 0;
        int truncation_mask() const { ++reads; return mask; }
    };
    static_assert(plan4::runtime::model_record_version<LegacyModel>() == 2);
    static_assert(plan4::runtime::model_record_version<CoveredModel>() == 3);
    LegacyModel legacy;
    plan4::LogRow old("model", "model-other-v1", plan4::runtime::model_record_version<LegacyModel>());
    old.field("model", "other");
    plan4::runtime::append_model_coverage(old, legacy);
    assert(emitted(old) == "p4 model v=2 build=model-other-v1 model=other\n");
    for (int mask : {-1, 0, 40}) {
        CoveredModel model{mask};
        plan4::LogRow row("model", "model-foundry-v4", plan4::runtime::model_record_version<CoveredModel>());
        row.field("model", "foundry");
        plan4::runtime::append_model_coverage(row, model);
        assert(model.reads == 1);
        assert(emitted(row) == "p4 model v=3 build=model-foundry-v4 model=foundry truncationMask=" +
            std::to_string(mask) + '\n');
    }
}
} // namespace

int main() {
    integer_limits_and_booleans();
    string_fields_and_build_envelope();
    only_the_written_prefix_is_exposed();
    exact_capacity_and_subsequent_overflow();
    constructor_string_and_numeric_overflows_fail_closed();
    one_payload_write_and_newline();
    optional_coverage_keeps_other_models_on_their_existing_protocol();
    std::cout << "PLAN4_TELEMETRY {\"passed\":true,\"scenarios\":7,\"engineLaunches\":0}\n";
}
