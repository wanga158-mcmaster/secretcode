// Pure geometric certificate/oracle, not a service/price/native fuel claim.
#include "../src/assignment_static_reach.h"
#include <array>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

namespace {
namespace r=tt::assignment_static_reach;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
std::int64_t distance(r::Point a,r::Point b) {
    const auto x=a.x>b.x?a.x-b.x:b.x-a.x,y=a.y>b.y?a.y-b.y:b.y-a.y;return x>y?x:y;
}
// Independent integer-cell oracle: no rectangle helper/interval arithmetic.
// Area checks EVERY legal perch and EVERY legal aim, including both spells.
bool oracle(const r::Input& input,std::span<const r::Point> members) {
    for(std::int64_t py=0;py<input.height;++py)for(std::int64_t px=0;px<input.width;++px) {
        const r::Point perch{px,py};
        if(input.family==r::Family::Basic) {
            bool all=true;for(const auto target:members)all&=distance(perch,target)<=input.basic_range;
            if(all)return true;continue;
        }
        for(std::size_t spell=0;spell<input.spell_count;++spell) {
            if(input.family==r::Family::Targeted) {
                bool all=true;for(const auto target:members)all&=distance(perch,target)<=input.spells[spell].range;
                if(all)return true;continue;
            }
            for(std::int64_t ay=0;ay<input.height;++ay)for(std::int64_t ax=0;ax<input.width;++ax) {
                const r::Point aim{ax,ay};if(distance(perch,aim)>input.spells[spell].range)continue;
                bool all=true;for(const auto target:members)all&=distance(aim,target)<=input.spells[spell].radius;
                if(all)return true;
            }
        }
    }
    return false;
}
void compare(const r::Input& input,std::span<const r::Point> members) {
    const auto expected=oracle(input,members)?r::Verdict::MayHavePerch:r::Verdict::Empty;
    std::size_t reads{};
    const auto actual=r::certify(input,members.size(),[&](std::size_t i){++reads;return members[i];});
    check(actual==expected,"rectangle certificate equals independent all-perch/all-aim oracle");
    check(reads==members.size(),"one bounded call reads each member once despite two spell alternatives");
    check(r::certify(input,members)==actual,"span and callback use the same actual certificate");
}
void all_families(int width,int height,std::span<const r::Point> members) {
    r::Input input;input.width=width;input.height=height;
    for(int range=0;range<=3;++range){input.basic_range=range;compare(input,members);}
    input.family=r::Family::Targeted;input.basic_range=1;input.spell_count=1;
    for(int a=0;a<=3;++a){input.spells[0]={a,0};compare(input,members);}
    input.spell_count=2;
    for(int a=0;a<=3;++a)for(int b=0;b<=3;++b){input.spells={r::Spell{a,0},r::Spell{b,0}};compare(input,members);}
    input.family=r::Family::Area;input.spell_count=1;
    for(int range=0;range<=2;++range)for(int radius=0;radius<=2;++radius){input.spells[0]={range,radius};compare(input,members);}
    input.spell_count=2;
    for(int ar=0;ar<=2;++ar)for(int az=0;az<=2;++az)for(int br=0;br<=2;++br)for(int bz=0;bz<=2;++bz) {
        input.spells={r::Spell{ar,az},r::Spell{br,bz}};compare(input,members);
    }
}
void exhaustive_small_grids() {
    for(int height=1;height<=3;++height)for(int width=1;width<=3;++width) {
        for(int a=0;a<width*height;++a) {
            const std::array<r::Point,1> single{{{a%width,a/width}}};all_families(width,height,single);
            for(int b=0;b<width*height;++b) {
                const std::array<r::Point,2> pair{{{a%width,a/width},{b%width,b/width}}};all_families(width,height,pair);
            }
        }
    }
    // Every ordered triple (including coincident members) on the 2x2 map.
    for(int a=0;a<4;++a)for(int b=0;b<4;++b)for(int c=0;c<4;++c) {
        const std::array<r::Point,3> triple{{{a%2,a/2},{b%2,b/2},{c%2,c/2}}};all_families(2,2,triple);
    }
}
void invalid_and_capacity_are_unknown() {
    const std::array<r::Point,3> invalid_tail{{{0,0},{3,0},{4,0}}};r::Input input;input.width=4;input.height=1;
    check(r::certify(input,invalid_tail)==r::Verdict::Unknown,
        "invalid late member overrides an already-empty earlier intersection instead of becoming a semantic refutation");
    const std::array<r::Point,1> one{{{0,0}}};input.width=0;
    check(r::certify(input,one)==r::Verdict::Unknown,"zero map dimension is fallback");input.width=4;input.height=-1;
    check(r::certify(input,one)==r::Verdict::Unknown,"negative map dimension is fallback");input.height=1;input.basic_range=-1;
    check(r::certify(input,one)==r::Verdict::Unknown,"negative basic range is fallback");input.basic_range=0;
    input.family=static_cast<r::Family>(99);check(r::certify(input,one)==r::Verdict::Unknown,"unknown family is fallback");
    input.family=r::Family::Targeted;check(r::certify(input,one)==r::Verdict::Unknown,"missing eligible spell specification is not an empty-family proof");
    input.spell_count=3;check(r::certify(input,one)==r::Verdict::Unknown,"more than two spell geometries is fallback");input.spell_count=1;
    input.spells[0]={-1,0};check(r::certify(input,one)==r::Verdict::Unknown,"negative spell range is fallback");
    input.spells[0]={1,-1};check(r::certify(input,one)==r::Verdict::Unknown,"negative spell radius is fallback");input.spells[0]={1,0};
    input.spell_count=2;input.spells[1]={-1,0};
    check(r::certify(input,one)==r::Verdict::Unknown,"a viable first alternative cannot conceal an invalid second spell");
    std::size_t reads{};input={r::Family::Basic,4,1,0};
    check(r::certify(input,r::max_members+1,[&](std::size_t){++reads;return r::Point{};})==r::Verdict::Unknown&&reads==0,
        "unsupported member capacity falls back before any callback reads");
    check(r::certify(input,std::span<const r::Point>{})==r::Verdict::Unknown,"empty group is invalid rather than a vacuous executable offer");
    struct Wide {std::uint64_t x,y;};
    check(r::certify(input,1,[](std::size_t){return Wide{std::numeric_limits<std::uint64_t>::max(),0};})==r::Verdict::Unknown,
        "wide unsigned coordinates cannot silently wrap into signed map bounds");
    struct Floating {double x,y;};
    check(r::certify(input,1,[](std::size_t){return Floating{0,0};})==r::Verdict::Unknown,"nonintegral coordinate representation is explicitly unsupported");
}
void numeric_edges_and_union() {
    constexpr auto imax=std::numeric_limits<int>::max();constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    r::Input input;input.width=input.height=imax;input.basic_range=imax;
    const std::array<r::Point,2> far{{{0,0},{imax-1,imax-1}}};
    check(r::certify(input,far)==r::Verdict::MayHavePerch,"signed32 maximum coordinates/ranges use safe signed64 sums");
    input.basic_range=0;check(r::certify(input,far)==r::Verdict::Empty,"maximum-size map can have an exact empty intersection without scanning it");
    input.family=r::Family::Area;input.spell_count=2;input.spells={r::Spell{imax,imax},r::Spell{0,0}};
    check(r::certify(input,far)==r::Verdict::MayHavePerch,"area union retains one supported large-radius alternative");
    input={r::Family::Basic,maximum,1,0};const std::array<r::Point,1> edge{{{maximum-1,0}}};
    check(r::certify(input,edge)==r::Verdict::MayHavePerch,"signed64 maximum map bound itself does not overflow");input.basic_range=1;
    check(r::certify(input,edge)==r::Verdict::MayHavePerch,"exact signed64 maximum sum remains representable and clipped");input.basic_range=2;
    check(r::certify(input,edge)==r::Verdict::Unknown,"unrepresentable positive sum yields Unknown before signed overflow");
    input={r::Family::Area,maximum,1,0};input.spell_count=1;input.spells[0]={2,0};
    check(r::certify(input,edge)==r::Verdict::Unknown,"area cast-region expansion checks its own overflow");
    const std::array<r::Point,1> negative{{{std::numeric_limits<std::int64_t>::min(),0}}};
    check(r::certify(input,negative)==r::Verdict::Unknown,"signed minimum coordinate is rejected before radius subtraction");
    input={r::Family::Targeted,7,1,0};input.spell_count=2;input.spells={r::Spell{1,0},r::Spell{3,0}};
    const std::array<r::Point,2> ends{{{0,0},{6,0}}};
    check(r::certify(input,ends)==r::Verdict::MayHavePerch,"two-spell union is nonempty when only the second range can serve the group");
    input.spells[1].range=2;check(r::certify(input,ends)==r::Verdict::Empty,"union is empty only after both valid alternatives are empty");
}
void exact_ybr_gold_lines() {
    // Exact map 130453395278cdabb1ff80564357a99ca8394afaef006fdede8beb88924a8fa0:
    // the two 30-gold-cluster groups are x5..34 at y12 and y17. This proves only
    // that current restricted standing families have no joint reach/perch.
    for(const int y:{12,17}) {
        std::array<r::Point,30> line;for(std::size_t i=0;i<line.size();++i)line[i]={static_cast<std::int64_t>(i)+5,y};
        r::Input input;input.width=40;input.height=30;
        for(int range=0;range<=4;++range) {
            input.basic_range=range;check(r::certify(input,line)==r::Verdict::Empty,"YBR30-member Basic standing family is exactly empty for range<=4");
            input.family=r::Family::Targeted;input.spell_count=2;input.spells={r::Spell{range,0},r::Spell{4,0}};
            check(r::certify(input,line)==r::Verdict::Empty,"YBR30-member Targeted union is exactly empty for range<=4");
            input.family=r::Family::Area;input.spells={r::Spell{4,1},r::Spell{4,range}};
            check(r::certify(input,line)==r::Verdict::Empty,"YBR30-member Area aim intersection is empty for radius<=4 despite cast reach");
            input.family=r::Family::Basic;input.spell_count=0;
        }
        input.family=r::Family::Area;input.spell_count=1;input.spells[0]={0,15};
        check(r::certify(input,line)==r::Verdict::MayHavePerch,"a different wide-area family remains possible; no whole-group ban is implied");
    }
}
}
int main() {
    exhaustive_small_grids();invalid_and_capacity_are_unknown();numeric_edges_and_union();exact_ybr_gold_lines();
    std::printf("assignment static reach (pure): %d checks, %d failures\n",checks,failures);return failures?1:0;
}
