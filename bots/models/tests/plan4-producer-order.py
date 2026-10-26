"""Exercise the actual production input-order block with real suite collectors.

The SDK uses synthetic query fuel. This proves the admission mechanism and exact
public-frame parity, not native fuel safety or model behavioral activation.
"""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/arena"))
from wasi import run_case


PRELUDE = r'''
#define main historical_coverage_main
#include "COVERAGE"
#undef main
#define PLAN4_PUBLIC_COMPOSITION 1
#define PLAN4_SPATIAL_EVIDENCE 1
struct ModelTap : r::Foundry {
    r::PublicLayoutFrame layout;
    r::SpatialFrame spatial;
    std::string order;
    void observe_layout(const r::PublicLayoutFrame& frame) {
        layout=frame;order+='L';r::Foundry::observe_layout(frame);
    }
    void observe_spatial(const r::SpatialFrame& frame) {
        spatial=frame;order+='S';r::Foundry::observe_spatial(frame);
    }
};
struct FixtureNavigation : PublicNavigation {
    r::PublicTopology topology()const{return {50,50,true,walkable,nullptr};}
    int vertex_visits()const{return 0;}
};
void production_inputs(OnlineObserver& observer_,ModelTap& runtime_model_,r::SpatialEvidence& spatial_evidence_,
                       const Information& info,const Menu& menu,unsigned optional_floor_) {
    FixtureNavigation navigation_;
    const std::vector<cambc::camp> camps;
    const std::vector<cambc::champion> enemies;
PRODUCTION
}
struct Case {
    CollectorRig rig;
    std::unique_ptr<r::SpatialEvidence> spatial=std::make_unique<r::SpatialEvidence>();
    std::unique_ptr<ModelTap> model=std::make_unique<ModelTap>();
    Menu menu;
    Case(int members=3) {
        rig.setup(1,members);cm::query_cost=100;
        assert(rig.observer->observe_into(*rig.info,{distance,nullptr},40000));
        menu=make_menu(*rig.info);assert(menu.single_count==1&&!menu.truncated);
    }
    void collect(int headroom) {
        cm::fuel=40000+headroom;
        production_inputs(*rig.observer,*model,*spatial,*rig.info,menu,40000);
    }
};
int main() {
    auto high=std::make_unique<Case>();high->collect(20000);
    assert(!high->model->layout.truncated&&high->model->layout.member_count==3);
    assert(high->model->spatial.current_complete&&!cm::unauthorized);
    std::array<r::PublicMember,3> expected;
    std::copy_n(high->model->layout.members,3,expected.begin());
    [[maybe_unused]] const auto expected_group=high->model->layout.groups[0];
    [[maybe_unused]] const auto expected_clock=high->model->layout.clock;
    auto low=std::make_unique<Case>();low->collect(6599);
#if PLAN4_COMPOSITION_FIRST
    assert(low->model->order=="LS"&&!low->model->layout.truncated);
    assert(low->model->layout.group_count==1&&low->model->layout.member_count==3);
    const auto& actual_group=low->model->layout.groups[0];
    assert(std::tie(expected_group.group_id,expected_group.first,expected_group.count,expected_group.complete)==
           std::tie(actual_group.group_id,actual_group.first,actual_group.count,actual_group.complete));
    assert(low->model->layout.clock.round==expected_clock.round&&low->model->layout.clock.own.id==expected_clock.own.id);
    assert(low->model->spatial.current_complete);
    for(int n=0;n<3;++n){const auto& a=expected[n];const auto& b=low->model->layout.members[n];
        assert(std::tie(a.group_id,a.member_id,a.ordinal,a.declared_members,a.kind,a.attack_range,a.spawn.x,a.spawn.y,a.maximum_health)==
               std::tie(b.group_id,b.member_id,b.ordinal,b.declared_members,b.kind,b.attack_range,b.spawn.x,b.spawn.y,b.maximum_health));}
#else
    assert(low->model->order=="SL"&&low->model->layout.truncated);
#endif
    auto tight=std::make_unique<Case>();tight->collect(5999);
    assert(tight->model->layout.truncated); // Copy-order repair never removes reserve admission.
    auto oversized=std::make_unique<Case>(33);oversized->collect(20000);
    assert(oversized->model->layout.truncated);
    auto stale=std::make_unique<Case>();++cm::round;stale->collect(20000);
    assert(stale->model->layout.truncated&&!stale->model->spatial.current_complete);
    assert(!cm::unauthorized);
    std::printf("production composition order %d: high/6599/5999/33-member/stale cases passed; synthetic SDK only\n",PLAN4_COMPOSITION_FIRST);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--node", required=True)
    parser.parse_args()
    source = (ROOT / "bots/models/common/macro-methods.h").read_text()
    block = source.split("// planning-frame-producers-begin\n", 1)[1].split("// planning-frame-producers-end", 1)[0]
    output = ROOT / ".cache/arena/orchestration/models-producer/fixtures"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="order-", dir=output) as scratch:
        cpp = Path(scratch) / "production.cpp"
        cpp.write_text(PRELUDE.replace("COVERAGE", (ROOT / "bots/models/tests/plan4-foundry-coverage.cpp").as_posix())
                       .replace("PRODUCTION", block), encoding="utf-8")
        for flag in (0, 1):
            ok, log = run_case(f"producer-order-{flag}", cpp,
                               includes=[ROOT / "bots/models/tests/fixtures/plan4-online-sdk"],
                               defines=[f"PLAN4_COMPOSITION_FIRST={flag}"],
                               flags=["-O3", "-Wl,-z,stack-size=65536"], out_dir=Path(scratch))
            print(log)
            if not ok:
                raise SystemExit(1)


if __name__ == "__main__":
    main()
