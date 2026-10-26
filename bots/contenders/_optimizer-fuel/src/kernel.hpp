#pragma once
// C10 generated kernel. Preconditions: 0<=n<=256; all coordinates in [0,99].
// Result high16=Chebyshev distance, low16=first nearest index+1 (0 for n=0).
// Empty distance=10000. Integer-only; no gameplay or SDK calls.
// Candidate: {"axis_prune":true,"distance":"ternary","early_zero":true,"layout":"soa16","reduction":"branch","unroll":4}
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#if defined(__GNUC__) || defined(__clang__)
#define C10_NOINLINE __attribute__((noinline))
#else
#define C10_NOINLINE
#endif
namespace fuel_kernel {
struct Data { std::int16_t x[256],y[256]; void set(int i,int a,int b) { x[i]=static_cast<std::int16_t>(a); y[i]=static_cast<std::int16_t>(b); } };
inline constexpr unsigned char absolute[199]={99,98,97,96,95,94,93,92,91,90,89,88,87,86,85,84,83,82,81,80,79,78,77,76,75,74,73,72,71,70,69,68,67,66,65,64,63,62,61,60,59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99};
inline std::uint32_t packed(int best,int encoded) { return (static_cast<std::uint32_t>(best)<<16)|static_cast<std::uint32_t>(encoded); }
inline int distance(int dx,int dy) { const int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy; return ax>ay?ax:ay; }
C10_NOINLINE inline std::uint32_t nearest(const Data& data,int n,int qx,int qy) {
    int best=10000,encoded=0;
    auto visit=[&](int i) { const int dx=data.x[i]-qx,dy=data.y[i]-qy; const int ax=dx<0?-dx:dx; if(ax>=best) return; const int d=distance(dx,dy); if(d<best) { best=d; encoded=i+1; } };
    int i=0;
    for(;i+4<=n;i+=4) { visit(i+0); if(best==0) return packed(best,encoded); visit(i+1); if(best==0) return packed(best,encoded); visit(i+2); if(best==0) return packed(best,encoded); visit(i+3); if(best==0) return packed(best,encoded); }
    for(;i<n;++i) { visit(i); if(best==0) return packed(best,encoded); }
    return packed(best,encoded);
}
} // namespace fuel_kernel
#undef C10_NOINLINE
