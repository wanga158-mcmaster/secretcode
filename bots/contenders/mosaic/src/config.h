#pragma once
// Narrow compatibility control: these are our own retired inputs, captured by
// core.read_closure before any build. The adaptive ON translation unit does
// not compile or call the retired local controller.
#include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/config.h"
#ifndef MOSAIC_ADAPTIVE_SERVICE
#define MOSAIC_ADAPTIVE_SERVICE 0
#endif
