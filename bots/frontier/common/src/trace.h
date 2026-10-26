#pragma once
#include <charconv>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <algorithm>
namespace p10 {
// Integer-only, fixed-storage diagnostics. General printf formatting consumed
// nearly10k guest fuel per line in native calibration, before any actual work.
struct TraceLine {
 char bytes[768]{};std::size_t size=0;
 void append(std::string_view value){const auto n=std::min(value.size(),sizeof(bytes)-1-size);std::copy_n(value.data(),n,bytes+size);size+=n;}
 template<class T> requires std::is_integral_v<T>
 void append(T value){char number[32];const auto result=std::to_chars(number,number+sizeof(number),value);if(result.ec==std::errc{})append(std::string_view(number,result.ptr-number));}
 template<class... T> explicit TraceLine(const T&...values){(append(values),...);bytes[size++]='\n';}
};
template<class... T> void trace(const T&...values){const TraceLine line(values...);std::fwrite(line.bytes,1,line.size,stdout);}
}
