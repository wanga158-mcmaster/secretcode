#pragma once
// Reuse the observed game value types, not R20's controller or owner stack.
#include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/native_service_types.h"
namespace mosaic::adaptive {
namespace game=::mosaic::native_service;
using Point=game::Point;
using Epoch=game::Epoch;
}
