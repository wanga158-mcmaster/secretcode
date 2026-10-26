#include "../src/policy.h"
using integral::Candidate;
using integral::basic_work;
using integral::precedes;
// Work must not erase fractional HP immediately above an exact attack boundary.
static_assert(basic_work(140, 70) == 2);
static_assert(basic_work(140.00002f, 70) == 3);
static_assert(basic_work(139.99998f, 70) == 2);
static_assert(basic_work(0.01f, 70) == 1);
static_assert(basic_work(1, 0) >= 10000);
// Predeclared legal isolated probe: B0 lower HP chooses wolf; weighted work
// chooses elk. These exact predictions do not stand in for replay evidence.
constexpr Candidate wolf{140,20,2,319}, elk{222,60,4,320};
static_assert(wolf.health < elk.health);
static_assert(precedes(elk, wolf));
static_assert(!precedes(wolf, elk));
static_assert(integral::isolated_loss(wolf,elk) == 320);
static_assert(integral::isolated_loss(elk,wolf) == 280);
static_assert(800-integral::isolated_loss(wolf,elk) > 800*0.25f);
static_assert(!fundamentals::should_retreat(480,800,80));
// Ratio tie, work tie, HP tie and tile tie each have a separate observable
// consequence. The non-threatening case becomes shortest remaining work.
static_assert(precedes(Candidate{10,20,1,9}, Candidate{15,40,2,1}));
static_assert(precedes(Candidate{8,20,1,9}, Candidate{10,20,1,1}));
static_assert(precedes(Candidate{8,20,1,2}, Candidate{8,20,1,9}));
static_assert(precedes(Candidate{500,0,2,9}, Candidate{20,0,3,1}));
// Movement can replace a distant zero weight with a current threat. A fresh
// Varg splash observation can also turn the surviving low-work target into
// the next choice; keeping the old Candidate would fail these comparisons.
static_assert(precedes(Candidate{100,0,2,1}, Candidate{200,0,3,2}));
static_assert(precedes(Candidate{200,60,3,2}, Candidate{100,0,2,1}));
static_assert(precedes(Candidate{15,20,1,1}, Candidate{150,60,3,2}));
static_assert(integral::late_round(1900,2000));
static_assert(!integral::late_round(1899,2000));
int main() {}
