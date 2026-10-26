#include "finite_service.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <limits>

// Immutable YellowBrickRoad A geometry from the retained atlas input, NOT a
// runtime job table. Map SHA13045339..., input SHA29d081a8... (full hashes in
// the owned scratch result). The header contains no coordinates or map labels.
namespace {
using namespace mosaic::finite_service;
constexpr std::array<Tile,26> blocked{{
    117,176,206,264,554,555,556,557,579,594,595,596,597,620,634,635,636,637,674,675,
    676,677,935,993,1023,1082
}};
constexpr std::array<Tile,292> opaque{{
    0,1,33,34,35,36,38,39,41,42,52,53,54,79,82,84,85,86,87,88,
    89,90,91,92,96,97,98,102,103,104,105,106,107,108,109,113,114,119,123,124,
    125,128,129,130,138,139,140,141,144,145,149,153,162,163,169,179,180,185,186,202,
    209,210,215,218,219,226,242,248,250,251,256,257,266,267,271,272,275,276,278,282,
    288,291,292,306,307,308,309,310,311,314,315,317,318,322,323,332,333,341,342,343,
    345,346,351,352,353,354,356,357,363,364,374,375,390,391,409,421,445,446,447,448,
    449,450,451,452,453,454,455,456,457,458,459,460,461,462,463,464,465,466,467,468,
    469,470,471,472,473,474,725,726,727,728,729,730,731,732,733,734,735,736,737,738,
    739,740,741,742,743,744,745,746,747,748,749,750,751,752,753,754,778,790,808,809,
    824,825,835,836,842,843,845,846,847,848,853,854,856,857,858,866,867,876,877,881,
    882,884,885,888,889,890,891,892,893,907,908,911,917,921,923,924,927,928,932,933,
    942,943,948,949,951,957,973,980,981,984,989,990,997,1013,1014,1019,1020,1030,1036,1037,
    1046,1050,1054,1055,1058,1059,1060,1061,1069,1070,1071,1074,1075,1076,1080,1085,1086,1090,1091,1092,
    1093,1094,1095,1096,1097,1101,1102,1103,1107,1108,1109,1110,1111,1112,1113,1114,1115,1117,1120,1145,
    1146,1147,1157,1158,1160,1161,1163,1164,1165,1166,1198,1199
}};
constexpr std::array<Camp,160> camps{{
    {92,3,120.0f,0.0f,0.0f,3},
    {90,4,2400.0f,0.0f,60.0f,4},
    {136,5,120.0f,0.0f,0.0f,3},
    {138,44,120.0f,0.0f,0.0f,3},
    {148,16,120.0f,0.0f,0.0f,3},
    {84,17,1350.0f,0.0f,20.0f,3},
    {150,18,120.0f,0.0f,0.0f,3},
    {98,57,1350.0f,0.0f,20.0f,3},
    {152,58,120.0f,0.0f,0.0f,3},
    {110,29,1350.0f,0.0f,20.0f,3},
    {100,30,120.0f,0.0f,0.0f,3},
    {88,31,1350.0f,0.0f,20.0f,3},
    {154,70,120.0f,0.0f,0.0f,3},
    {106,120,120.0f,0.0f,0.0f,3},
    {104,160,2400.0f,0.0f,60.0f,4},
    {140,161,120.0f,0.0f,0.0f,3},
    {116,200,1350.0f,0.0f,20.0f,3},
    {142,201,120.0f,0.0f,0.0f,3},
    {144,240,120.0f,0.0f,0.0f,3},
    {96,171,120.0f,0.0f,0.0f,3},
    {78,172,1350.0f,0.0f,20.0f,3},
    {146,173,120.0f,0.0f,0.0f,3},
    {82,222,1350.0f,0.0f,20.0f,3},
    {158,262,120.0f,0.0f,0.0f,3},
    {86,228,1350.0f,0.0f,20.0f,3},
    {156,229,120.0f,0.0f,0.0f,3},
    {114,235,120.0f,0.0f,0.0f,3},
    {112,236,1350.0f,0.0f,20.0f,3},
    {102,237,2400.0f,0.0f,60.0f,4},
    {108,326,1350.0f,0.0f,20.0f,3},
    {132,327,120.0f,0.0f,0.0f,3},
    {76,366,1350.0f,0.0f,20.0f,3},
    {134,367,120.0f,0.0f,0.0f,3},
    {94,377,120.0f,0.0f,0.0f,3},
    {80,378,2400.0f,0.0f,60.0f,4},
    {59,485,120.0f,0.0f,0.0f,3},
    {57,486,120.0f,0.0f,0.0f,3},
    {55,487,120.0f,0.0f,0.0f,3},
    {53,488,120.0f,0.0f,0.0f,3},
    {51,489,120.0f,0.0f,0.0f,3},
    {49,490,120.0f,0.0f,0.0f,3},
    {47,491,120.0f,0.0f,0.0f,3},
    {45,492,120.0f,0.0f,0.0f,3},
    {43,493,120.0f,0.0f,0.0f,3},
    {41,494,120.0f,0.0f,0.0f,3},
    {39,495,120.0f,0.0f,0.0f,3},
    {37,496,120.0f,0.0f,0.0f,3},
    {35,497,120.0f,0.0f,0.0f,3},
    {33,498,120.0f,0.0f,0.0f,3},
    {31,499,120.0f,0.0f,0.0f,3},
    {29,500,120.0f,0.0f,0.0f,3},
    {27,501,120.0f,0.0f,0.0f,3},
    {25,502,120.0f,0.0f,0.0f,3},
    {23,503,120.0f,0.0f,0.0f,3},
    {21,504,120.0f,0.0f,0.0f,3},
    {19,505,120.0f,0.0f,0.0f,3},
    {17,506,120.0f,0.0f,0.0f,3},
    {15,507,120.0f,0.0f,0.0f,3},
    {13,508,120.0f,0.0f,0.0f,3},
    {11,509,120.0f,0.0f,0.0f,3},
    {9,510,120.0f,0.0f,0.0f,3},
    {7,511,120.0f,0.0f,0.0f,3},
    {5,512,120.0f,0.0f,0.0f,3},
    {3,513,120.0f,0.0f,0.0f,3},
    {1,514,120.0f,0.0f,0.0f,3},
    {121,568,120.0f,0.0f,0.0f,3},
    {62,569,1350.0f,0.0f,20.0f,3},
    {119,608,120.0f,0.0f,0.0f,3},
    {60,609,1350.0f,0.0f,20.0f,3},
    {125,572,120.0f,0.0f,0.0f,3},
    {66,573,2400.0f,0.0f,60.0f,4},
    {123,612,120.0f,0.0f,0.0f,3},
    {64,613,2400.0f,0.0f,60.0f,4},
    {129,576,120.0f,0.0f,0.0f,3},
    {70,577,1350.0f,0.0f,20.0f,3},
    {74,578,2400.0f,0.0f,60.0f,4},
    {127,616,120.0f,0.0f,0.0f,3},
    {68,617,1350.0f,0.0f,20.0f,3},
    {72,618,2400.0f,0.0f,60.0f,4},
    {131,619,120.0f,0.0f,0.0f,3},
    {130,580,120.0f,0.0f,0.0f,3},
    {73,581,2400.0f,0.0f,60.0f,4},
    {69,582,1350.0f,0.0f,20.0f,3},
    {126,583,120.0f,0.0f,0.0f,3},
    {75,621,2400.0f,0.0f,60.0f,4},
    {71,622,1350.0f,0.0f,20.0f,3},
    {128,623,120.0f,0.0f,0.0f,3},
    {65,586,2400.0f,0.0f,60.0f,4},
    {122,587,120.0f,0.0f,0.0f,3},
    {67,626,2400.0f,0.0f,60.0f,4},
    {124,627,120.0f,0.0f,0.0f,3},
    {61,590,1350.0f,0.0f,20.0f,3},
    {118,591,120.0f,0.0f,0.0f,3},
    {63,630,1350.0f,0.0f,20.0f,3},
    {120,631,120.0f,0.0f,0.0f,3},
    {0,685,120.0f,0.0f,0.0f,3},
    {2,686,120.0f,0.0f,0.0f,3},
    {4,687,120.0f,0.0f,0.0f,3},
    {6,688,120.0f,0.0f,0.0f,3},
    {8,689,120.0f,0.0f,0.0f,3},
    {10,690,120.0f,0.0f,0.0f,3},
    {12,691,120.0f,0.0f,0.0f,3},
    {14,692,120.0f,0.0f,0.0f,3},
    {16,693,120.0f,0.0f,0.0f,3},
    {18,694,120.0f,0.0f,0.0f,3},
    {20,695,120.0f,0.0f,0.0f,3},
    {22,696,120.0f,0.0f,0.0f,3},
    {24,697,120.0f,0.0f,0.0f,3},
    {26,698,120.0f,0.0f,0.0f,3},
    {28,699,120.0f,0.0f,0.0f,3},
    {30,700,120.0f,0.0f,0.0f,3},
    {32,701,120.0f,0.0f,0.0f,3},
    {34,702,120.0f,0.0f,0.0f,3},
    {36,703,120.0f,0.0f,0.0f,3},
    {38,704,120.0f,0.0f,0.0f,3},
    {40,705,120.0f,0.0f,0.0f,3},
    {42,706,120.0f,0.0f,0.0f,3},
    {44,707,120.0f,0.0f,0.0f,3},
    {46,708,120.0f,0.0f,0.0f,3},
    {48,709,120.0f,0.0f,0.0f,3},
    {50,710,120.0f,0.0f,0.0f,3},
    {52,711,120.0f,0.0f,0.0f,3},
    {54,712,120.0f,0.0f,0.0f,3},
    {56,713,120.0f,0.0f,0.0f,3},
    {58,714,120.0f,0.0f,0.0f,3},
    {81,821,2400.0f,0.0f,60.0f,4},
    {95,822,120.0f,0.0f,0.0f,3},
    {135,832,120.0f,0.0f,0.0f,3},
    {77,833,1350.0f,0.0f,20.0f,3},
    {133,872,120.0f,0.0f,0.0f,3},
    {109,873,1350.0f,0.0f,20.0f,3},
    {159,937,120.0f,0.0f,0.0f,3},
    {83,977,1350.0f,0.0f,20.0f,3},
    {145,959,120.0f,0.0f,0.0f,3},
    {143,998,120.0f,0.0f,0.0f,3},
    {117,999,1350.0f,0.0f,20.0f,3},
    {141,1038,120.0f,0.0f,0.0f,3},
    {105,1039,2400.0f,0.0f,60.0f,4},
    {107,1079,120.0f,0.0f,0.0f,3},
    {103,962,2400.0f,0.0f,60.0f,4},
    {113,963,1350.0f,0.0f,20.0f,3},
    {115,964,120.0f,0.0f,0.0f,3},
    {157,970,120.0f,0.0f,0.0f,3},
    {87,971,1350.0f,0.0f,20.0f,3},
    {147,1026,120.0f,0.0f,0.0f,3},
    {79,1027,1350.0f,0.0f,20.0f,3},
    {97,1028,120.0f,0.0f,0.0f,3},
    {155,1129,120.0f,0.0f,0.0f,3},
    {89,1168,1350.0f,0.0f,20.0f,3},
    {101,1169,120.0f,0.0f,0.0f,3},
    {111,1170,1350.0f,0.0f,20.0f,3},
    {153,1141,120.0f,0.0f,0.0f,3},
    {99,1142,1350.0f,0.0f,20.0f,3},
    {151,1181,120.0f,0.0f,0.0f,3},
    {85,1182,1350.0f,0.0f,20.0f,3},
    {149,1183,120.0f,0.0f,0.0f,3},
    {139,1155,120.0f,0.0f,0.0f,3},
    {137,1194,120.0f,0.0f,0.0f,3},
    {91,1195,2400.0f,0.0f,60.0f,4},
    {93,1196,120.0f,0.0f,0.0f,3}
}};
constexpr std::array<int,3> members{96,78,146};
struct Expected {Tile before,after;std::uint32_t loss,health;float target_hp;};
constexpr std::array<Expected,29> expected{{
    {525,484,0,1000,1350.0f},
    {484,445,20,980,1350.0f},
    {445,406,40,940,1350.0f},
    {406,407,40,900,1350.0f},
    {407,368,40,860,1350.0f},
    {368,329,40,820,1350.0f},
    {329,290,40,780,1350.0f},
    {290,251,20,760,1350.0f},
    {251,212,20,740,1350.0f},
    {212,212,20,720,1280.0f},
    {212,212,20,700,1210.0f},
    {212,212,20,680,1140.0f},
    {212,212,20,660,1070.0f},
    {212,212,20,640,1000.0f},
    {212,212,20,620,930.0f},
    {212,212,20,600,860.0f},
    {212,212,20,580,790.0f},
    {212,212,20,560,720.0f},
    {212,212,20,540,650.0f},
    {212,212,20,520,580.0f},
    {212,212,20,500,510.0f},
    {212,212,20,480,440.0f},
    {212,212,20,460,370.0f},
    {212,212,20,440,300.0f},
    {212,212,20,420,230.0f},
    {212,212,20,400,160.0f},
    {212,212,20,380,90.0f},
    {212,212,20,360,20.0f},
    {212,212,20,340,0.0f}
}};
struct Fixture {
    std::array<Node,1200> nodes{};
    std::array<Tile,1200> heap{},route{};
    std::array<CampResidual,160> residual{};
    std::array<ModelStep,512> steps{};
    Workspace workspace() {return {nodes,heap,route,residual,steps};}
};
Problem ybr() {
    return {{0x29d081a8,1},40,30,525,212,blocked,opaque,camps,10,members,4,0,2000,10000,
            {1000.0f,1000.0f,70.0f,1,true}};
}
bool same_float(float a,float b) {return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
void finish(CandidateBuilder& builder,std::size_t quota) {
    std::size_t calls=0;auto previous=builder.summary();
    while(builder.advance(quota)==Status::Working) {
        const auto now=builder.summary();
        assert(now.work_units>previous.work_units&&now.work_units-previous.work_units<=quota);
        assert(now.inputs>=previous.inputs);previous=now;
        assert(++calls<200000);
    }
}
struct Tiny {
    std::array<Camp,1> camps{{{42,7,120.0f,0.0f,1.0f,0}}};
    std::array<int,1> ids{42};
    Problem problem() {return {{7,1},3,3,0,4,{},{},camps,7,ids,1,0,2000,10000,{1000,1000,70,1,true}};}
};
}
int main() {
    int checks=0;
    Fixture first;CandidateBuilder a;auto p=ybr();
    assert(a.begin(p,first.workspace()));++checks;
    assert(a.advance(0)==Status::Working&&a.summary().work_units==0);++checks;
    assert(!a.begin(p,first.workspace())&&a.summary().work_units==0);++checks;
    finish(a,1);const auto summary=a.summary();
    assert(summary.status==Status::PrefixReady&&summary.reason==Reason::None);++checks;
    assert(summary.inputs==29&&summary.moves==9&&summary.basics==20);++checks;
    assert(summary.nominal_loss==660&&summary.nominal_health==340);++checks;
    assert(summary.two_delay_reserve==80&&summary.reserved_health==260);++checks;
    assert(summary.target_native_id==78&&summary.identity.problem==0x29d081a8);++checks;
    for(std::size_t i=0;i<expected.size();++i) {
        const auto& step=a.word()[i];const auto& e=expected[i];
        assert(step.before.tile==e.before&&step.after.tile==e.after&&step.pre_action_camp_loss==e.loss&&step.after.health_lower==e.health);
        assert(step.before.elapsed_inputs==i&&step.after.elapsed_inputs==i+1&&step.after.bag==0);
        assert(same_float(step.after.target_hp,e.target_hp)&&step.after.target_shield==0);
        assert(step.all_potential_threats&&(step.actual_guards&common_guards)==common_guards);
        assert(step.op==(i<9?Op::Move:Op::Basic)&&step.phase==(i<9?Phase::Ingress:Phase::HarmfulRemoval));
        assert(step.before.selected_model_dead==0&&step.after.selected_model_dead==(i==28?1u:0u));
        if(i<9) assert((step.actual_guards&FreshMoveLegal)!=0);
        else assert((step.actual_guards&(FreshBasicLegalAndReady|ActualTargetVisible))==(FreshBasicLegalAndReady|ActualTargetVisible));
        ++checks;
    }
    assert(a.word().back().fresh_death_required_after==78&&(a.word().back().actual_guards&FreshDeathBeforeThreatReduction));++checks;
    for(std::size_t i=0;i<camps.size();++i) {
        const auto& actual=a.camp_residuals()[i];assert(actual.native_id==camps[i].native_id);
        assert(actual.model_dead==(camps[i].native_id==78));
        assert(actual.hp==(camps[i].native_id==78?0.0f:camps[i].hp)&&actual.shield==camps[i].shield);
    }++checks;
    assert(a.whole_service_status()==Status::Unsupported&&a.whole_service_reason()==Reason::CaptureRecallBankRenewalNotPorted);++checks;
    Fixture second;CandidateBuilder b;assert(b.begin(p,second.workspace()));finish(b,17);
    assert(b.summary().work_units==summary.work_units&&b.word().size()==a.word().size());
    for(std::size_t i=0;i<a.word().size();++i) {
        assert(a.word()[i].after.tile==b.word()[i].after.tile&&a.word()[i].after.health_lower==b.word()[i].after.health_lower);
        assert(same_float(a.word()[i].after.target_hp,b.word()[i].after.target_hp));
    }++checks;
    assert(a.advance(100)==Status::PrefixReady&&a.summary().work_units==summary.work_units);++checks;
    {
        Fixture f;CandidateBuilder c;auto q=ybr();q.body.health=740;
        assert(c.begin(q,f.workspace()));finish(c,23);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::HealthBound);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();q.remaining_inputs=30;
        assert(c.begin(q,f.workspace()));finish(c,31);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::Horizon);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();auto ws=f.workspace();ws.steps=ws.steps.first(28);
        assert(c.begin(q,ws));finish(c,100);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::WorkspaceCapacity&&c.word().size()==28);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();q.initial_bag=1;
        assert(!c.begin(q,f.workspace())&&c.summary().status==Status::Unsupported&&c.summary().reason==Reason::InitialBag);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();q.body.single_target_basic=false;
        assert(!c.begin(q,f.workspace())&&c.summary().status==Status::Unsupported&&c.summary().reason==Reason::UnsupportedClass);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();auto ws=f.workspace();ws.nodes=ws.nodes.first(10);
        assert(!c.begin(q,ws)&&c.summary().reason==Reason::WorkspaceCapacity&&c.camp_residuals().empty());++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        assert(c.begin(q,f.workspace()));finish(c,7);
        assert(c.summary().status==Status::PrefixReady&&c.summary().moves==1&&c.word()[0].before.tile==0&&c.word()[0].after.tile==4);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;tiny.camps[0].hp=649;auto q=tiny.problem();q.body.strength=129.8f;
        tiny.camps[0].strength=0.1f;tiny.camps[0].range=1;
        assert(c.begin(q,f.workspace()));finish(c,5);assert(c.summary().status==Status::PrefixReady&&c.summary().basics==6);++checks;
        assert(c.summary().nominal_health==994&&c.word()[1].pre_action_camp_loss==1);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;tiny.camps[0].shield=70;auto q=tiny.problem();
        assert(c.begin(q,f.workspace()));finish(c,5);assert(c.summary().status==Status::PrefixReady&&c.summary().basics==3);++checks;
        assert(c.word()[1].after.target_hp==120&&c.word()[1].after.target_shield==0);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();q.max_basic_inputs=1;
        assert(c.begin(q,f.workspace()));finish(c,13);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::RemovalLimit);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;tiny.camps[0].tile=8;auto q=tiny.problem();q.station=5;
        constexpr std::array<Tile,3> isolated{1,3,4};q.static_blocked=isolated;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::NoRoute);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();q.station=1;q.body.range=2;
        constexpr std::array<Tile,1> middle{4};q.opaque=middle;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::Unsupported&&c.summary().reason==Reason::StationNotFrontier);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();q.station=3;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().reason==Reason::StationNotFrontier);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();tiny.camps[0].hp=1048576;q.body.strength=0.000001f;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::Unknown&&c.summary().reason==Reason::NoFloatProgress&&c.word().size()==1);++checks;
    }
    {
        Fixture f;CandidateBuilder c;auto q=ybr();q.remaining_inputs=79;
        assert(c.begin(q,f.workspace()));finish(c,19);assert(c.summary().status==Status::PrefixReady);
        assert(c.whole_service_status()==Status::Unsupported);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();q.body.health=std::numeric_limits<float>::quiet_NaN();
        assert(!c.begin(q,f.workspace())&&c.summary().reason==Reason::InvalidBounds);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        constexpr std::array<Camp,2> duplicate{{{42,7,120,0,1,0},{42,8,120,0,0,0}}};q.camps=duplicate;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().reason==Reason::InvalidBounds);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        constexpr std::array<Camp,2> both{{{42,7,120,0,1,0},{43,8,120,0,1,0}}};
        constexpr std::array<int,2> ids{42,43};q.camps=both;q.member_native_ids=ids;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::Unsupported&&c.summary().reason==Reason::UnsupportedClass);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        // An unselected potential camp still contributes its full ceil loss.
        constexpr std::array<Camp,2> both{{{42,7,120,0,1,0},{43,8,120,0,1.1f,2}}};q.camps=both;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::PrefixReady);
        assert(c.summary().nominal_loss==6&&c.word()[0].pre_action_camp_loss==2&&c.summary().two_delay_reserve==4);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        // A zero-strength, unselected camp is still physically occupied.
        constexpr std::array<Camp,2> both{{{42,7,120,0,1,0},{43,4,120,0,0,0}}};q.camps=both;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().reason==Reason::StationNotFrontier);++checks;
    }
    {
        Fixture f;CandidateBuilder c;Tiny tiny;auto q=tiny.problem();
        // Never narrow a raw incoming double bound before applying ceil.
        constexpr std::array<Camp,2> both{{{42,7,120,0,1,0},{43,8,120,0,20.0000001,2}}};q.camps=both;
        assert(c.begin(q,f.workspace()));finish(c,3);assert(c.summary().status==Status::PrefixReady);
        assert(c.word()[0].pre_action_camp_loss==21&&c.summary().nominal_loss==63);++checks;
    }
    {
        const auto invalid=basic_effect(std::numeric_limits<float>::quiet_NaN(),0,70);
        assert(!invalid.valid&&std::isnan(invalid.hp));++checks;
        assert(!basic_effect(120,-1,70).valid&&!basic_effect(120,0,-1).valid);++checks;
    }
    std::printf("YBR prefix: %zu inputs, %llu work units, loss %llu, reserved HP %u\n",summary.inputs,
                static_cast<unsigned long long>(summary.work_units),static_cast<unsigned long long>(summary.nominal_loss),summary.reserved_health);
    std::printf("finite service checks: %d passed; SDK calls: 0; prefix only\n",checks);
}
