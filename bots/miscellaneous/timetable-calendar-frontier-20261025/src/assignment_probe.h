#pragma once
// Diagnostic transport only. Exactly40 uint32 words, manual lowercase ASCII hex.
#include <array>
#include <cstddef>
#include <cstdint>
namespace tt::assignment_probe_detail {
inline constexpr std::uint32_t magic=0x54544132u,none=0xffffffffu;
inline constexpr std::size_t word_count=40,line_size=word_count*8+1;
inline constexpr std::uint64_t tail_reserve=30000;
using Words=std::array<std::uint32_t,word_count>;
inline bool selected_epoch(int epoch) {return epoch==512 || epoch==1024 || epoch==1536;}
inline std::array<char,line_size> encode(const Words& words) {
    constexpr char digits[]="0123456789abcdef";
    std::array<char,line_size> output{};
    for(std::size_t word=0;word<word_count;++word)
        for(unsigned nibble=0;nibble<8;++nibble)
            output[word*8+nibble]=digits[(words[word]>>(28-4*nibble))&15u];
    output[word_count*8]='\n';return output;
}
} // namespace tt::assignment_probe_detail
