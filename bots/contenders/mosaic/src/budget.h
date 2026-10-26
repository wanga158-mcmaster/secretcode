#pragma once
// Reuse only measured import/whole-entry clock accounting. The R21 controller
// does not use the retired executor, plan quotes or its ownership structures.
#include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/native_service_execution.h"
namespace mosaic::adaptive {using TurnBudget=::mosaic::native_service::Budget;}
