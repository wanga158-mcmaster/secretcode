#include "assignment_probe.h"
#include <cassert>
#include <charconv>
#include <cstdio>
namespace probe=tt::assignment_probe_detail;
int main() {
    static_assert(probe::line_size==321 && probe::word_count==40);
    assert(!probe::selected_epoch(511) && probe::selected_epoch(512));
    assert(probe::selected_epoch(1024) && probe::selected_epoch(1536) && !probe::selected_epoch(1537));
    for(std::uint32_t sample=0;sample<100;++sample) {
        probe::Words words{};
        for(std::size_t i=0;i<words.size();++i)words[i]=sample==0?0:sample==1?0xffffffffu:
            (sample*0x9e3779b9u)^static_cast<std::uint32_t>(i*0x8a5cd789u);
        words[0]=probe::magic;
        const auto line=probe::encode(words);assert(line.back()=='\n');
        for(std::size_t i=0;i<words.size();++i) {
            std::uint32_t decoded{};const auto* begin=line.data()+8*i;
            const auto result=std::from_chars(begin,begin+8,decoded,16);
            assert(result.ec==std::errc{} && result.ptr==begin+8 && decoded==words[i]);
            for(unsigned n=0;n<8;++n)assert((begin[n]>='0'&&begin[n]<='9')||(begin[n]>='a'&&begin[n]<='f'));
        }
    }
    std::puts("assignment probe codec passed");
}
