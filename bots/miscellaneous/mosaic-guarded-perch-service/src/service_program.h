#pragma once
// Control order only. Ops owns admission, observation, cursor progress, physical
// mutation and fresh authority. A callback's bool permits another iteration; it
// does not assert success. Registration/layout are handled by Actor before run.
namespace mosaic::program {
enum class Phase {registration,layout,discovery,route_capture,goals,compile,dispatch,
    inspect,capture,tribute,channel,recall,home,unsupported};
enum class Purpose {work,capture,bank,home};
template<class Ops>
void run(Phase& phase,Ops& ops,unsigned limit=6) {
    for(unsigned continuation=0;continuation<limit&&ops.current();++continuation) {
        switch(phase) {
        case Phase::discovery:ops.discovery();return;
        case Phase::route_capture:ops.route_capture();return;
        case Phase::goals:ops.goals();return;
        case Phase::compile:
            if(ops.compile()&&phase==Phase::dispatch)continue;
            return;
        case Phase::dispatch:
            if(ops.dispatch())continue;
            return;
        case Phase::inspect:ops.inspect();return;
        case Phase::capture:ops.capture();return;
        case Phase::tribute:case Phase::channel:case Phase::recall:case Phase::home:
            ops.channel();return;
        default:ops.unsupported();return;
        }
    }
}
} // namespace mosaic::program
