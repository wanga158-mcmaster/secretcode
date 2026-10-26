#include "../src/hoard-policy.h"
#include <cassert>
#include <limits>
using hoard::Reason;
static_assert(hoard::request(true,0,1900,2000,0,1000,1,true,0,1,0)==Reason::none);
// Nearby altars never erase the inventory treatment; ordinary controls bank.
static_assert(hoard::request(true,143,500,2000,0,1000,1,true,0,15,0)==Reason::none);
static_assert(hoard::request(false,1,500,2000,0,1000,1,true,0,1,0)==Reason::ordinary);
static_assert(hoard::request(true,144,500,2000,0,1000,8,true,7,15,0)==Reason::threshold);
static_assert(hoard::request(true,1,500,2000,999,1000,8,true,7,1,0)==Reason::winning);
static_assert(hoard::request(false,1,500,2000,999,1000,8,true,7,1,0)==Reason::winning);
// Hard cutoff clamps at zero on a short configured game.
static_assert(hoard::deadline(2000,128)==1872);
static_assert(hoard::deadline(100,128)==0);
static_assert(hoard::request(true,1,0,100,0,1000,8,true,7,1,0)==Reason::hard_deadline);
// Exact projected completion boundary and unknown-lock allowance.
static_assert(!hoard::projection_due(1800,2000,true,160,15,3,16));
static_assert(hoard::projection_due(1806,2000,true,160,15,3,16));
static_assert(!hoard::projection_due(1805,2000,true,160,15,3,16));
static_assert(!hoard::projection_due(1806,2000,false,160,15,3,16));
static_assert(hoard::projection_due(1,2000,true,std::numeric_limits<unsigned>::max(),15,3,16));
static_assert(hoard::request(true,143,1871,2000,0,1000,8,true,0,15,0)==Reason::none);
static_assert(hoard::request(true,143,1872,2000,0,1000,8,true,0,15,0)==Reason::hard_deadline);
static_assert(hoard::request(false,23,1899,2000,0,1000,5,true,4,3,0)==Reason::none);
static_assert(hoard::request(false,23,1900,2000,0,1000,5,true,4,3,0)==Reason::ordinary);
// A loaded early Prism need not spend alternate owner turns preparing a field.
static_assert(!hoard::projection_required(58,810,2000,0,1000,6,5));
static_assert(!hoard::projection_required(143,500,2000,0,1000,15,5));
static_assert(!hoard::projection_required(0,1999,2000,0,1000,1,5));
static_assert(hoard::projection_required(144,500,2000,0,1000,15,5));
static_assert(hoard::projection_required(1,500,2000,999,1000,1,5));
static_assert(hoard::projection_required(58,1717,2000,0,1000,6,5));
static_assert(!hoard::projection_required(58,1716,2000,0,1000,6,5));
static_assert(hoard::projection_required(1,0,100,0,1000,1,5));
// The R736 own view supplies HP loss100 and a visible 130-strength melee
// reset user at(42,20). No hidden camp coordinates enter this decision.
constexpr hoard::EscapeContact vendetta{42,20,2,260};
static_assert(hoard::basic_exposure(40,17,vendetta)==0);
static_assert(hoard::basic_exposure(40,18,vendetta)==260);
static_assert(!hoard::capital_step(true,400,700,100,0,260));
static_assert(hoard::capital_step(true,400,700,100,0,0));
static_assert(hoard::capital_step(false,400,700,100,0,260));
static_assert(hoard::capital_step(true,700,700,0,0,260));
static_assert(hoard::capital_step(true,120,700,100,260,130));
constexpr hoard::PublicMonolith monolith{25,19,5,200};
static_assert(!hoard::monolith_risk(20,13,monolith));
static_assert(hoard::monolith_risk(21,14,monolith));
static_assert(hoard::monolith_risk(21,19,monolith));
static_assert(!hoard::monolith_risk(21,14,{25,19,4,200}));
static_assert(!hoard::monolith_risk(21,14,{25,19,5,0}));
static_assert(hoard::capital_transit(true,true,true)==hoard::CapitalTransit::egress);
static_assert(hoard::capital_transit(true,true,false)==hoard::CapitalTransit::avoid);
static_assert(hoard::capital_transit(false,true,true)==hoard::CapitalTransit::ordinary);
int main() {
    const auto float_boundary=hoard::required_basics(649.0f,129.8f);
    assert(float_boundary.certain&&float_boundary.count==6);
    assert(!hoard::required_basics(10000,1).certain);
    const hoard::AreaVictim long_group[]={{64,false,false},{64,false,false},{1,false,true}};
    assert(!hoard::area_outcome(long_group,0,1,true,false).certain);
    // Real Main52 Lanes B R609: the retained 27 gems make the area spell
    // lethal to the 120-HP cluster while also shortening its visible wolf.
    const hoard::AreaVictim lanes[]={{1233.4f,true,false},{120,true,true}};
    const auto ordinary=hoard::area_outcome(lanes,198.825f,52.25f,false,false);
    const auto capital=hoard::area_outcome(lanes,132.55f,52.25f,true,false);
    const auto bagless=hoard::area_outcome(lanes,88.0f,52.25f,true,false);
    assert(ordinary.remaining_basics==24&&capital.remaining_basics==22&&bagless.remaining_basics==23);
    assert(hoard::prefer_area(capital,ordinary));
    assert(hoard::prefer_area(bagless,ordinary)); // initial choice is also a kit gain
    assert(hoard::no_new_camp_hit(false,20,5,3));
    assert(!hoard::no_new_camp_hit(false,20,3,3));
    assert(hoard::no_new_camp_hit(true,20,3,3));
    const hoard::AreaVictim single[]={{120,true,true}};
    assert(!hoard::prefer_area(hoard::area_outcome(single,132.55f,52.25f,true,true),ordinary));
    // Preserve the ordinary selected basic in the comparison; spells that
    // kill it cannot silently redirect that basic to another victim.
    const hoard::AreaVictim equal[]={{60,true,true},{120,true,false}};
    assert(!hoard::prefer_area(hoard::area_outcome(equal,30,60,true,true),
        hoard::area_outcome(equal,60,60,false,true)));
    // The real capital phase survives the R737 loss of enemy vision. Visible
    // silence does not prove recall safety while personal HP still declines.
    hoard::CapitalRecovery recovery;
    recovery.observe(true,true,false);assert(!recovery.active());
    recovery.reject_approach();assert(recovery.active());
    recovery.observe(true,true,false);assert(recovery.active());
    assert(!recovery.may_recall(true,140));
    assert(recovery.may_recall(true,0));
    assert(!recovery.may_recall(false,0));
    recovery.observe(true,true,true);assert(!recovery.active());
    recovery.reject_approach();recovery.observe(false,true,false);assert(!recovery.active());
    recovery.reject_approach();recovery.observe(true,false,false);assert(!recovery.active());
    assert(hoard::channel(1,1,0,0.1)==1);
    assert(hoard::channel(24,1,0,0.1)==3);
    assert(hoard::channel(120,1,0,0.1)==12);
    assert(hoard::channel(144,1,0,0.1)==15);
    assert(hoard::channel(24,2,5,0.2)==10);
    assert(hoard::channel(0,3,0,0.1)==3);
    // Whenever preparation is skipped, every allowed complete route and lock
    // state gives exactly the old non-banking decision. Check near each real
    // deadline and both winning/threshold boundaries, including short games.
    for(unsigned maximum : {100U,2000U}) for(unsigned round : {0U,500U,810U,1700U,1716U,1717U,1856U,1871U,1872U,1999U})
        for(unsigned bag : {0U,1U,24U,58U,143U,144U}) for(unsigned score : {0U,856U,999U,1000U}) {
            const auto duration=static_cast<unsigned>(hoard::channel(bag,1,0,0.1));
            if(hoard::projection_required(bag,round,maximum,score,1000,duration,5))continue;
            for(unsigned travel=0;travel<=256;++travel) for(unsigned hits=0;hits<=5;++hits)
                assert(hoard::request(true,bag,round,maximum,score,1000,0,true,travel,duration,hits)==Reason::none);
        }
}
