"""Compile actual production startup/after-turn handoff guards with a fuel/clock double."""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/arena"))
from wasi import run_case

CPP = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
namespace cambc {
unsigned round=1,fuel=200000;
unsigned round_number(){return round;}
unsigned fuel_remaining(){return fuel;}
}
struct Bot {
    bool initialized_=false,startup_yielded_=false;
    bool jump=false;
    unsigned cost=0,initializations=0,actions=0,polls=0;
    void initialize(){++initializations;cambc::fuel-=cost;if(jump)++cambc::round;initialized_=true;}
    void turn(){
HANDOFF
        ++actions;
    }
    void after_turn(){
AFTER
        ++polls;
    }
};
int main(){
    for(unsigned remaining:{4200u,29999u,30000u,80000u}) {
        cambc::round=1;cambc::fuel=200000;Bot bot;bot.cost=200000-remaining;
        bot.turn();bot.after_turn();
#if PLAN4_STARTUP_YIELD
        assert(bot.actions==unsigned(remaining>=30000)&&bot.polls==unsigned(remaining>=30000));
#else
        assert(bot.actions==1&&bot.polls==1);
#endif
        assert(bot.initializations==1);
        cambc::round=2;cambc::fuel=100000;const auto before=bot.actions;
        bot.turn();bot.after_turn();assert(bot.initializations==1&&bot.actions==before+1&&!bot.startup_yielded_);
    }
    cambc::round=1;cambc::fuel=200000;Bot stale;stale.jump=true;
    stale.turn();stale.after_turn();
#if PLAN4_STARTUP_YIELD
    assert(stale.actions==0&&stale.polls==0&&stale.startup_yielded_);
#else
    assert(stale.actions==1&&stale.polls==1);
#endif
    std::printf("production startup handoff %d: low/reserve-boundary/current/stale/resume cases passed; synthetic fuel only\n",PLAN4_STARTUP_YIELD);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--node", required=True)
    parser.parse_args()
    source = (ROOT / "bots/models/common/legacy.h").read_text()
    handoff = source.split("// startup-handoff-begin\n", 1)[1].split("// startup-handoff-end", 1)[0]
    after = source.split("// startup-after-turn-begin\n", 1)[1].split("// startup-after-turn-end", 1)[0]
    output = ROOT / ".cache/arena/orchestration/models-producer/fixtures"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="handoff-", dir=output) as scratch:
        cpp = Path(scratch) / "production.cpp"
        cpp.write_text(CPP.replace("HANDOFF", handoff).replace("AFTER", after), encoding="utf-8")
        for flag in (0, 1):
            ok, log = run_case(f"startup-handoff-{flag}", cpp, defines=[f"PLAN4_STARTUP_YIELD={flag}"],
                               flags=["-O3", "-Wl,-z,stack-size=65536"], out_dir=Path(scratch))
            print(log)
            if not ok:
                raise SystemExit(1)


if __name__ == "__main__":
    main()
