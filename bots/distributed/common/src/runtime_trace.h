#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace p6::runtime_trace {
// Fixed numeric output avoids locale/variadic formatting in the action tail.
// Callers check their quota and fuel reserve before constructing the fields.
inline void emit(std::span<const std::int64_t> fields,char record='c') {
    std::array<char,512> line{};
    std::size_t count=0;
    for(char c : {'p','6',record})line[count++]=c;
    for(const auto value:fields) {
        line[count++]=' ';
        auto magnitude=static_cast<std::uint64_t>(value);
        if(value<0) {line[count++]='-';magnitude=std::uint64_t{0}-magnitude;}
        char digits[20];int size=0;
        do {digits[size++]=static_cast<char>('0'+magnitude%10);magnitude/=10;}while(magnitude);
        if(count+static_cast<std::size_t>(size)+1>=line.size())return;
        while(size)line[count++]=digits[--size];
    }
    line[count++]='\n';
    std::fwrite(line.data(),1,count,stdout);
    std::fflush(stdout);
}
} // namespace p6::runtime_trace
