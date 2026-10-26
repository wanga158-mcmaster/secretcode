#pragma once
#include "../../earned-center/src/trace.h"
namespace p8::banded_cells {
// The typed own-action trace is valid for either named collector. Banded Cells
// does not impose P8-02's spawn/shop endpoint: declarations leave that optional.
using CycleTrace=p8::earned_center::CycleTrace;
} // namespace p8::banded_cells
