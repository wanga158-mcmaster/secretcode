#include "staging_egress.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace s=wb::staging;

static void tavi_saved_geometry() {
    // Native wolf87, YBR group19/g_970; frozen R1000 Tavi4 is8,21 HP900.
    // This checks the pure helper, not a live action or private planning state.
    const std::array supports{s::Support{11,24,3,20}};
    const std::array candidates{s::Landing{8,20}};
    auto q=s::quote(8,21,900,supports,candidates);
    assert(q.kind==s::Kind::Chosen&&q.candidate_index==0);
    assert(q.landing.x==8&&q.landing.y==20);
    assert(q.origin_raw==20&&q.failure_remaining==880);
    assert(!s::covers(supports[0],q.landing));
    const std::array all{s::Landing{7,22},s::Landing{9,20},s::Landing{8,20},
                         s::Landing{7,21},s::Landing{7,20}};
    q=s::quote(8,21,900,supports,all);
    assert(q.kind==s::Kind::Chosen&&q.landing.x==7&&q.landing.y==20);
    assert(q.candidate_index==4); // Stable row-major, not input order.
}

static void overlap_and_failure_boundary() {
    const std::array supports{s::Support{3,3,1,20},s::Support{3,4,1,60}};
    const std::array candidates{s::Landing{5,5},s::Landing{4,3},s::Landing{5,3},s::Landing{5,4}};
    auto q=s::quote(4,4,100,supports,candidates);
    assert(q.kind==s::Kind::Chosen&&q.origin_raw==80&&q.failure_remaining==20);
    assert(q.landing.x==5&&q.landing.y==3&&q.candidate_index==2);
    const std::array reversed{s::Landing{5,4},s::Landing{5,3},s::Landing{4,3},s::Landing{5,5}};
    q=s::quote(4,4,100,supports,reversed);
    assert(q.kind==s::Kind::Chosen&&q.landing.x==5&&q.landing.y==3&&q.candidate_index==1);
    q=s::quote(4,4,80,supports,candidates);
    assert(q.kind==s::Kind::UnsafeOriginFailure&&q.failure_remaining==0);
    assert(q.candidate_index==-1); // A quiet endpoint cannot erase failed-move risk.
    q=s::quote(4,4,79,supports,candidates);
    assert(q.kind==s::Kind::UnsafeOriginFailure&&q.failure_remaining==-1);
    q=s::quote(4,4,double(std::nextafterf(80.0f,81.0f)),supports,candidates);
    assert(q.kind==s::Kind::Chosen&&q.failure_remaining>0);
    const float hp=900.125f;
    q=s::quote(4,4,double(hp),supports,candidates);
    float actual=hp;actual-=20.0f;actual-=60.0f;
    assert(q.kind==s::Kind::Chosen&&q.failure_remaining==double(actual));
}

static void quiet_and_represented_empty() {
    const std::array wolf{s::Support{11,24,3,20}};
    const std::array unsafe_destinations{s::Landing{9,21},s::Landing{8,22}};
    auto q=s::quote(8,21,900,wolf,unsafe_destinations);
    assert(q.kind==s::Kind::NoRepresentedQuietLanding&&q.candidate_index==-1);
    q=s::quote(8,21,900,wolf,{});
    assert(q.kind==s::Kind::NoRepresentedQuietLanding);
    q=s::quote(8,20,1,wolf,{});
    assert(q.kind==s::Kind::AlreadyQuiet&&q.origin_raw==0&&q.failure_remaining==1);
    assert(q.candidate_index==-1&&q.landing.x==8&&q.landing.y==20);
    q=s::quote(8,21,1,{},unsafe_destinations);
    assert(q.kind==s::Kind::AlreadyQuiet);
    const std::array harmless{s::Support{8,21,3,0}};
    q=s::quote(8,21,1,harmless,{});
    assert(q.kind==s::Kind::AlreadyQuiet);
}

static void malformed_and_overflow() {
    const std::array wolf{s::Support{11,24,3,20}};
    const std::array candidate{s::Landing{8,20}};
    for(double hp:{0.0,-1.0,900.1,16777218.0,std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN()})
        assert(s::quote(8,21,hp,wolf,candidate).kind==s::Kind::Unsupported);
    assert(s::quote(-1,21,900,wolf,candidate).kind==s::Kind::Unsupported);
    const std::array bad_supports{s::Support{-1,24,3,20},s::Support{11,-1,3,20},
        s::Support{11,24,-1,20},s::Support{11,24,3,-1},
        s::Support{11,24,3,20.5},s::Support{11,24,3,40},
        s::Support{11,24,3,std::numeric_limits<double>::infinity()},
        s::Support{11,24,3,std::numeric_limits<double>::quiet_NaN()}};
    for(const auto bad:bad_supports) {
        const std::array one{bad};
        assert(s::quote(8,21,900,one,candidate).kind==s::Kind::Unsupported);
    }
    for(const auto bad:std::array{s::Landing{-1,20},s::Landing{8,21},s::Landing{8,19}}) {
        const std::array one{bad};
        assert(s::quote(8,21,900,wolf,one).kind==s::Kind::Unsupported);
    }
    const std::array duplicate{s::Landing{8,20},s::Landing{8,20}};
    assert(s::quote(8,21,900,wolf,duplicate).kind==s::Kind::Unsupported);
    const std::vector<s::Support> too_many(s::max_supports+1,s::Support{11,24,3,20});
    assert(s::quote(8,21,900,too_many,candidate).kind==s::Kind::Unsupported);
    const std::vector<s::Landing> too_many_landings(s::max_landings+1,s::Landing{8,20});
    assert(s::quote(8,21,900,wolf,too_many_landings).kind==s::Kind::Unsupported);
    const double huge=std::numeric_limits<double>::max();
    const std::array overflowing{s::Support{8,21,1,huge},s::Support{8,21,1,huge}};
    assert(s::quote(8,21,huge,overflowing,candidate).kind==s::Kind::Unsupported);
    // Wide subtraction/range comparison and y/x ordering avoid signed overflow.
    const int edge=std::numeric_limits<int>::max();
    const std::array edge_support{s::Support{edge,edge,1,20}};
    const std::array edge_candidate{s::Landing{edge-2,edge}};
    const auto q=s::quote(edge-1,edge,100,edge_support,edge_candidate);
    assert(q.kind==s::Kind::Chosen&&q.landing.x==edge-2&&q.origin_raw==20);
    const std::array wide_support{s::Support{0,0,edge,20}};
    assert(s::quote(edge,edge,100,wide_support,{}).kind==s::Kind::NoRepresentedQuietLanding);
    const std::vector<s::Support> maximum(s::max_supports,s::Support{3,4,1,200});
    const std::array maximum_landing{s::Landing{5,4}};
    const auto max_q=s::quote(4,4,51201,maximum,maximum_landing);
    assert(max_q.kind==s::Kind::Chosen&&max_q.origin_raw==51200&&max_q.failure_remaining==1);
    assert(s::quote(4,4,51200,maximum,maximum_landing).kind==s::Kind::UnsafeOriginFailure);
}

int main() {
    tavi_saved_geometry();overlap_and_failure_boundary();quiet_and_represented_empty();
    malformed_and_overflow();
    std::cout<<"staging_egress: concrete Tavi, overlap, strict failure reserve, quiet menus, bounds and overflow pass\n";
}
