// Parity gate (DESIGN.md 3.3): logits and value from the bot's own inference code (src/core/net.cpp) for the first
// records of an imitation file, with the weights fastrun would inject. tools/parity.py compares them with numpy.
//   netcheck WEIGHTS.txt RECORDS.bin [max]   -> lines "head n logit... | value"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include "../src/core/cand.h"
#include "../src/core/net.h"

using namespace zero;

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    std::vector<float> w;
    {
        std::ifstream in(argv[1]);
        float x;
        while (in >> x) w.push_back(x);
    }
    const float* weights = w.data() + 16;  // skip the config block
    FILE* f = std::fopen(argv[2], "rb");
    if (!f) return 1;
    const int max = argc > 3 ? std::atoi(argv[3]) : 200;
    for (int r = 0; r < max; ++r)
    {
        int hdr[9];
        if (std::fread(hdr, sizeof(int), 9, f) != 9) break;
        float sv[kState];
        if (std::fread(sv, sizeof(float), kState, f) != static_cast<std::size_t>(kState)) break;
        CandSet set;
        set.head = static_cast<Head>(hdr[0]);
        set.n = hdr[1];
        for (int i = 0; i < set.n; ++i)
        {
            set.c[i] = Cand{};
            if (std::fread(set.c[i].phi, sizeof(float), static_cast<std::size_t>(hdr[3]), f) != static_cast<std::size_t>(hdr[3])) return 1;
        }
        net::Trunk t;
        if (set.head == h_draft) net::draft_trunk(weights, sv, t);
        else net::trunk(weights, sv, t);
        float lg[kMaxCands];
        net::logits(weights, t, set, lg);
        std::printf("%d %d", hdr[0], set.n);
        for (int i = 0; i < set.n; ++i) std::printf(" %.6g", static_cast<double>(lg[i]));
        std::printf(" | %.6g\n", static_cast<double>(net::value(weights, t)));
    }
    std::fclose(f);
    return 0;
}
