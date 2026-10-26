#include "weights.h"

namespace zero {

// Zero weights until training writes them (fastrun injection) or a release bakes them (generated initialiser).
Params params{ { 'm', 'b', '.', 'l', 'e', 'a', 'r', 'n', '.', 'p', 'a', 'r', 'a', 'm', 's', '\0' }, kConfig + kWeights, 1, {}, {} };

}  // namespace zero
